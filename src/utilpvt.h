/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * pvxs is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */
#ifndef UTILPVT_H
#define UTILPVT_H


#ifdef _WIN32
#  include <synchapi.h>
#else
#  include <pthread.h>
#endif

#include <memory>
#include <string>
#include <sstream>
#include <type_traits>

#include <compilerDependencies.h>

#include <pvxs/version.h>
#include <pvxs/util.h>

namespace pvxs {namespace impl {

//! in-line string builder (eg. for exception messages)
//! eg. @code throw std::runtime_error(SB()<<"Some message"<<42); @endcode
struct SB {
    std::ostringstream strm;
    SB() {}
    operator std::string() const { return strm.str(); }
    template<typename T>
    SB& operator<<(const T& i) { strm<<i; return *this; }
};

namespace idetail {
// specific specializations in util.cpp
template <typename T>
struct as_str {PVXS_API static T op(const char *s);};
} // namespace idetail

template <typename T>
inline T lexical_cast(const char *s)
{
    return idetail::as_str<T>::op(s);
}

template <typename T>
inline T lexical_cast(const std::string& s)
{
    return idetail::as_str<T>::op(s.c_str());
}

void indent(std::ostream& strm, unsigned level);

namespace idetail {
template <typename I>
struct Range {
    I a, b;

    struct iterator {
        typedef std::forward_iterator_tag iterator_category;
        typedef I         value_type;
        typedef ptrdiff_t difference_type;
        typedef I*        pointer;
        typedef I&        reference;
        I val;
        explicit constexpr iterator(I val) :val(val) {}
        EPICS_ALWAYS_INLINE I operator*() const { return val; }
        EPICS_ALWAYS_INLINE iterator& operator++() { val++; return *this; }
        EPICS_ALWAYS_INLINE iterator operator++(int) { return iterator{val++}; }
        EPICS_ALWAYS_INLINE bool operator==(const iterator& o) const { return val==o.val; }
        EPICS_ALWAYS_INLINE bool operator!=(const iterator& o) const { return val!=o.val; }
    };

    EPICS_ALWAYS_INLINE iterator begin() const { return iterator{a}; }
    EPICS_ALWAYS_INLINE iterator cbegin() const { return begin(); }
    EPICS_ALWAYS_INLINE iterator end() const { return iterator{b}; }
    EPICS_ALWAYS_INLINE iterator cend() const { return end(); }
};
} // namespace idetail

template<typename I>
constexpr idetail::Range<I> range(I end) { return idetail::Range<I>{I(0), end}; }

template<typename I>
constexpr idetail::Range<I> range(I begin, I end) { return idetail::Range<I>{begin, end}; }

#ifdef _WIN32
#  define RWLOCK_TYPE SRWLOCK
#  define RWLOCK_INIT(PLOCK)    InitializeSRWLock(PLOCK)
#  define RWLOCK_DTOR(PLOCK)    do{(void)(PLOCK);}while(0)
#  define RWLOCK_WLOCK(PLOCK)   AcquireSRWLockExclusive(PLOCK)
#  define RWLOCK_WUNLOCK(PLOCK) ReleaseSRWLockExclusive(PLOCK)
#  define RWLOCK_RLOCK(PLOCK)   AcquireSRWLockShared(PLOCK)
#  define RWLOCK_RUNLOCK(PLOCK) ReleaseSRWLockShared(PLOCK)
#else
#  define RWLOCK_TYPE pthread_rwlock_t
#  define RWLOCK_INIT(PLOCK)    pthread_rwlock_init(PLOCK, nullptr)
#  define RWLOCK_DTOR(PLOCK)    pthread_rwlock_destroy(PLOCK)
#  define RWLOCK_WLOCK(PLOCK)   pthread_rwlock_wrlock(PLOCK)
#  define RWLOCK_WUNLOCK(PLOCK) pthread_rwlock_unlock(PLOCK)
#  define RWLOCK_RLOCK(PLOCK)   pthread_rwlock_rdlock(PLOCK)
#  define RWLOCK_RUNLOCK(PLOCK) pthread_rwlock_unlock(PLOCK)
#endif

class RWLock
{
    RWLOCK_TYPE lock;
public:
    inline RWLock() { RWLOCK_INIT(&lock); }
    inline ~RWLock() { RWLOCK_DTOR(&lock); }

    RWLock(const RWLock&) = delete;
    RWLock(RWLock&&) = delete;
    RWLock& operator=(const RWLock&) = delete;
    RWLock& operator=(RWLock&&) = delete;

    struct UnlockReader {
        inline void operator()(RWLock *plock) { RWLOCK_RUNLOCK(&plock->lock); }
    };
    inline std::unique_ptr<RWLock, UnlockReader> lockReader() {
        RWLOCK_RLOCK(&lock);
        return std::unique_ptr<RWLock, UnlockReader>{this};
    }

    struct UnlockWriter {
        inline void operator()(RWLock *plock) { RWLOCK_WUNLOCK(&plock->lock); }
    };
    inline std::unique_ptr<RWLock, UnlockWriter> lockWriter() {
        RWLOCK_WLOCK(&lock);
        return std::unique_ptr<RWLock, UnlockWriter>{this};
    }
};

#undef RWLOCK_TYPE
#undef RWLOCK_INIT
#undef RWLOCK_DTOR
#undef RWLOCK_WLOCK
#undef RWLOCK_WUNLOCK
#undef RWLOCK_RLOCK
#undef RWLOCK_RUNLOCK

void logger_shutdown();

// std::max() isn't constexpr until c++14 :(
constexpr size_t cmax(size_t A, size_t B) {
    return A>B ? A : B;
}

// gcc 4.9 has aligned_storage but not aligned_union
#if GCC_VERSION && GCC_VERSION<VERSION_INT(4,10,0,0)

template<typename... Types>
struct max_sizeof {
    static const size_t align = 0;
    static const size_t size = 0;
};

template<typename Head, typename... Types>
struct max_sizeof<Head, Types...> {
    static const size_t align = cmax(alignof(Head), max_sizeof<Types...>::align);
    static const size_t size = cmax(sizeof(Head), max_sizeof<Types...>::size);
};

template <size_t Len, typename... Types>
struct aligned_union
{
    using _info = max_sizeof<Types...>;

    typedef typename std::aligned_storage<cmax(Len, _info::size), _info::align>::type type;
};

#else

template <size_t Len, typename... Types>
using aligned_union = std::aligned_union<Len, Types...>;
#endif

} // namespace impl
using namespace impl;
} // namespace pvxs

#endif // UTILPVT_H
