/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * pvxs is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */

#include <limits>
#include <system_error>
#include <utility>

#include <osiSock.h>
#include <epicsGuard.h>
#include <epicsAssert.h>

#include <pvxs/log.h>
#include "serverconn.h"

// limit on size of TX buffer above which we suspend RX
static constexpr size_t tcp_tx_limit = 0x100000;

namespace pvxs {namespace impl {

// message related to client state and errors
DEFINE_LOGGER(connsetup, "pvxs.tcp.setup");
// related to low level send/recv
DEFINE_LOGGER(connio, "pvxs.tcp.io");

DEFINE_LOGGER(remote, "pvxs.remote.log");

ServerConn::ServerConn(ServIface* iface, evutil_socket_t sock, struct sockaddr *peer, int socklen)
    :ConnBase(false,
              bufferevent_socket_new(iface->server->acceptor_loop.base, sock, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS),
              SockAddr(peer, socklen))
    ,iface(iface)
    ,nextSID(0)
{
    log_debug_printf(connio, "Client %s connects\n", peerName.c_str());

    bufferevent_setcb(bev.get(), &bevReadS, &bevWriteS, &bevEventS, this);

    timeval timo = {30, 0};
    bufferevent_set_timeouts(bev.get(), &timo, &timo);

    auto tx = bufferevent_get_output(bev.get());

    std::vector<uint8_t> buf(128);

    // queue connection validation message
    {
        uint8_t flags = hostBE ? pva_flags::MSB : 0;
        flags |= pva_flags::Server;

        VectorOutBuf M(hostBE, buf);
        to_wire(M, Header{pva_ctrl_msg::SetEndian, pva_flags::Control|pva_flags::Server, 0});

        auto save = M.save();
        M.skip(8); // placeholder for header
        auto bstart = M.save();

        // serverReceiveBufferSize, not used
        to_wire(M, uint32_t(0x10000));
        // serverIntrospectionRegistryMaxSize, also not used
        to_wire(M, uint16_t(0x7fff));
        to_wire(M, Size{2});
        to_wire(M, "anonymous");
        to_wire(M, "ca");
        auto bend = M.save();

        FixedBuf H(hostBE, save, 8);
        to_wire(H, Header{CMD_CONNECTION_VALIDATION, pva_flags::Server, uint32_t(bend-bstart)});

        assert(M.good() && H.good());

        if(evbuffer_add(tx, buf.data(), M.save()-buf.data()))
            throw std::bad_alloc();
    }

    if(bufferevent_enable(bev.get(), EV_READ|EV_WRITE))
        throw std::logic_error("Unable to enable BEV");
}

ServerConn::~ServerConn()
{}

const std::shared_ptr<ServerChan>& ServerConn::lookupSID(uint32_t sid)
{
    auto it = chanBySID.find(sid);
    if(it==chanBySID.end())
        throw std::runtime_error(SB()<<"Client "<<peerName<<" non-existant SID "<<sid);
    return it->second;
}

void ServerConn::handle_ECHO()
{
    // Client requests echo as a keep-alive check

    auto tx = bufferevent_get_output(bev.get());
    uint32_t len = evbuffer_get_length(segBuf.get());

    to_evbuf(tx, Header{CMD_ECHO, pva_flags::Server, len}, hostBE);

    auto err = evbuffer_add_buffer(tx, segBuf.get());
    assert(!err);

    // maybe help reduce latency
    bufferevent_flush(bev.get(), EV_WRITE, BEV_FLUSH);
}

static
void auth_complete(ServerConn *self, const Status& sts)
{
    (void)evbuffer_drain(self->txBody.get(), evbuffer_get_length(self->txBody.get()));

    {
        EvOutBuf M(hostBE, self->txBody.get());
        to_wire(M, sts);
    }

    self->enqueueTxBody(CMD_CONNECTION_VALIDATED);

    log_debug_printf(connsetup, "%s Auth complete with %d\n", self->peerName.c_str(), sts.code);
}

void ServerConn::handle_CONNECTION_VALIDATION()
{
    // Client begins (restarts?) Auth handshake

    EvInBuf M(peerBE, segBuf.get(), 16);

    std::string selected;
    {
        M.skip(4+2+2); // ignore unused buffer, introspection size, and QoS
        from_wire(M, selected);

        Value auth;
        from_wire_type_value(M, rxRegistry, auth);
        // TODO store credentials

        if(!M.good()) {
            log_err_printf(connio, "Client %s Truncated/Invalid ConnValid from client\n", peerName.c_str());
            bev.reset();
            return;

        } else {
            log_debug_printf(connsetup, "Client %s authenticates using %s and %s\n",
                       peerName.c_str(), selected.c_str(),
                       std::string(SB()<<auth).c_str());
        }
    }

    if(selected!="ca" && selected!="anonymous") {
        log_debug_printf(connsetup, "Client %s selects unadvertised auth \"%s\"", peerName.c_str(), selected.c_str());
        auth_complete(this, Status{Status::Error, "Client selects unadvertised auth"});
        return;

    } else {
        log_debug_printf(connsetup, "Client %s selects auth \"%s\"\n", peerName.c_str(), selected.c_str());
    }

    // remainder of segBuf is payload w/ credentials

    // TODO actually check credentials
    auth_complete(this, Status{Status::Ok});
}

void ServerConn::handle_AUTHNZ()
{
    // ignored (so far no auth plugin actually uses)
}

void ServerConn::handle_PUT_GET()
{}

void ServerConn::handle_CANCEL_REQUEST()
{
    EvInBuf M(peerBE, segBuf.get(), 16);

    uint32_t sid=0, ioid=0;
    from_wire(M, sid);
    from_wire(M, ioid);
    if(!M.good())
        throw std::runtime_error("Error decoding DestroyOp");

    auto it = opByIOID.find(ioid);
    if(it==opByIOID.end()) {
        log_warn_printf(connsetup, "Client %s Cancel of non-existant Op %u\n", peerName.c_str(), unsigned(ioid));
        return;
    }

    const auto& op = it->second;
    auto chan = op->chan.lock();
    if(!chan || chan->sid!=sid) {
        log_err_printf(connsetup, "Client %s Cancel inconsistent Op\n", peerName.c_str());
        return;
    }

    if(op->state==ServerOp::Executing) {
        op->state = ServerOp::Idle;

        if(op->onCancel)
            op->onCancel();

    } else {
        // an allowed race
        log_debug_printf(connsetup, "Client %s Cancel of non-executing Op\n", peerName.c_str());
    }
}

void ServerConn::handle_DESTROY_REQUEST()
{
    EvInBuf M(peerBE, segBuf.get(), 16);

    uint32_t sid=0, ioid=0;
    from_wire(M, sid);
    from_wire(M, ioid);
    if(!M.good())
        throw std::runtime_error("Error decoding DestroyOp");

    auto& chan = lookupSID(sid);

    auto it = opByIOID.find(ioid);
    auto n = chan->opByIOID.erase(ioid);

    if(it==opByIOID.end() || n!=1) {
        log_warn_printf(connsetup, "Client %s can't destroy non-existant op %u:%u\n",
                   peerName.c_str(), unsigned(sid), unsigned(ioid));

    } else {
        auto op = it->second;
        opByIOID.erase(it);
        op->state = ServerOp::Dead;

        if(op->onClose)
            op->onClose("");
    }
}

void ServerConn::handle_MESSAGE()
{
    EvInBuf M(peerBE, segBuf.get(), 16);

    uint32_t ioid = 0;
    uint8_t mtype = 0;
    std::string msg;

    from_wire(M, ioid);
    from_wire(M, mtype);
    from_wire(M, msg);

    if(!M.good())
        throw std::runtime_error("Decode error for Message");

    auto it = opByIOID.find(ioid);
    if(it==opByIOID.end()) {
        log_debug_printf(connsetup, "Client %s Message on non-existant ioid\n", peerName.c_str());
        return;
    }
    auto chan = it->second->chan.lock();

    Level lvl;
    switch(mtype) {
    case 0: lvl = Level::Info;
    case 1: lvl = Level::Warn;
    case 2: lvl = Level::Err;
    default:lvl = Level::Crit;
    }

    if(remote.test(lvl))
        errlogPrintf("Client %s Channel %s Remote message: %s\n",
                     peerName.c_str(), chan ? "<dead>" : chan->name.c_str(),
                     msg.c_str());
}

std::shared_ptr<ConnBase> ServerConn::self_from_this()
{
    return shared_from_this();
}

void ServerConn::cleanup()
{
    log_debug_printf(connsetup, "Client %s Cleanup TCP Connection\n", peerName.c_str());

    auto it = iface->server->connections.find(this);
    if(it!=iface->server->connections.end()) {
        auto self = std::move(it->second);
        iface->server->connections.erase(it);

        for(auto& pair : self->opByIOID) {
            if(pair.second->onClose)
                pair.second->onClose("");
        }
        for(auto& pair : self->chanBySID) {
            if(pair.second->onClose)
                pair.second->onClose("");
        }

        // delete this
    }
}

void ServerConn::bevRead()
{
    ConnBase::bevRead();

    if(!bev) {

    } else if(auto tx = bufferevent_get_output(bev.get())) {
        if(evbuffer_get_length(tx)>=tcp_tx_limit) {
            // write buffer "full".  stop reading until it drains
            // TODO configure
            (void)bufferevent_disable(bev.get(), EV_READ);
            bufferevent_setwatermark(bev.get(), EV_WRITE, tcp_tx_limit/2, 0);
            log_debug_printf(connio, "%s suspend READ\n", peerName.c_str());
        }
    }
}

void ServerConn::bevWrite()
{
    log_debug_printf(connio, "%s process backlog\n", peerName.c_str());

    auto tx = bufferevent_get_output(bev.get());
    // handle pending monitors

    while(!backlog.empty() && evbuffer_get_length(tx)<tcp_tx_limit) {
        auto fn = std::move(backlog.front());
        backlog.pop_front();

        fn();
    }

    // TODO configure
    if(evbuffer_get_length(tx)<tcp_tx_limit) {
        (void)bufferevent_enable(bev.get(), EV_READ);
        bufferevent_setwatermark(bev.get(), EV_WRITE, 0, 0);
        log_debug_printf(connio, "%s resume READ\n", peerName.c_str());
    }
}


ServIface::ServIface(const std::string& addr, unsigned short port, server::Server::Pvt *server, bool fallback)
    :server(server)
    ,bind_addr(AF_INET, addr.c_str(), port)
    ,sock(AF_INET, SOCK_STREAM, 0)
{
    server->acceptor_loop.assertInLoop();

    // try to bind to requested port, then fallback to a random port
    while(true) {
        try {
            sock.bind(bind_addr);
        } catch(std::system_error& e) {
            if(fallback && e.code().value()==SOCK_EADDRINUSE) {
                bind_addr.setPort(0);
                continue;
            }
            throw;
        }
        break;
    }

    name = bind_addr.tostring();

    const int backlog = 4;
    listener = evlisten(evconnlistener_new(server->acceptor_loop.base, onConnS, this, LEV_OPT_DISABLED, backlog, sock.sock));
}

void ServIface::onConnS(struct evconnlistener *listener, evutil_socket_t sock, struct sockaddr *peer, int socklen, void *raw)
{
    auto self = static_cast<ServIface*>(raw);
    try {
        if(peer->sa_family!=AF_INET) {
            log_crit_printf(connsetup, "Interface %s Rejecting !ipv4 client\n", self->name.c_str());
            evutil_closesocket(sock);
            return;
        }
        auto conn(std::make_shared<ServerConn>(self, sock, peer, socklen));
        self->server->connections[conn.get()] = std::move(conn);
    }catch(std::exception& e){
        log_crit_printf(connsetup, "Interface %s Unhandled error in accept callback: %s\n", self->name.c_str(), e.what());
        evutil_closesocket(sock);
    }
}

ServerOp::~ServerOp() {}

}} // namespace pvxs::impl
