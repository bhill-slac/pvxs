﻿/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * pvxs is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */

#include <epicsUnitTest.h>
#include <testMain.h>

#include <pvxs/util.h>
#include <pvxs/unittest.h>
#include "dataimpl.h"
#include "pvaproto.h"

namespace {
using namespace pvxs;
using namespace pvxs::impl;

void testDecode1()
{
    testDiag("%s", __func__);
    /*  From PVA proto doc
     *
     * timeStamp_t
     *   long secondsPastEpoch
     *   int nanoSeconds
     *   int userTag
     */
    std::vector<uint8_t> msg({
        // update cache with key 0
        0xFD, 0x00, 0x01,
        // structure
        0x80,
            // ID "timeStamp_t"
            0x0B, 0x74, 0x69, 0x6D, 0x65, 0x53, 0x74, 0x61, 0x6D, 0x70, 0x5F, 0x74,
            // 3 members
            0x03,
                // "secondsPastEpoch"
                0x10, 0x73, 0x65, 0x63, 0x6F, 0x6E, 0x64, 0x73, 0x50, 0x61, 0x73, 0x74, 0x45, 0x70, 0x6F, 0x63, 0x68,
                    // integer signed 8 bytes
                    0x23,
                // "nanoSeconds"
                0x0B, 0x6E, 0x61, 0x6E, 0x6F, 0x53, 0x65, 0x63, 0x6F, 0x6E, 0x64, 0x73,
                    // integer signed 4 bytes
                    0x22,
                // "userTag"
                0x07, 0x75, 0x73, 0x65, 0x72, 0x54, 0x61, 0x67,
                    // integer signed 4 bytes
                    0x22
    });

    std::vector<FieldDesc> descs;
    TypeStore cache;

    {
        FixedBuf buf(true, msg);
        from_wire(buf, descs, cache);
        testOk1(buf.good());
        testEq(buf.size(), 0u)<<"Of "<<msg.size();
    }

    testEq(cache.size(), 1u);
    {
        auto it = cache.find(1);
        if(testOk1(it!=cache.end())) {
            testEq(it->second.size(), 4u);
        }
    }

    if(testOk1(!descs.empty())) {
        testEq(descs.size(), descs.front().size());
    }

    //   cat <<EOF | sed -e 's|"|\\"|g' -e 's|^# |    "|' -e 's|$|\\n"|g'
    // paste in Actual
    testEq(std::string(SB()<<"\n"<<descs.data()),
           R"out(
[0] struct timeStamp_t parent=[0]  [0:4)
    nanoSeconds -> 2 [2]
    secondsPastEpoch -> 1 [1]
    userTag -> 3 [3]
    secondsPastEpoch :  1 [1]
    nanoSeconds :  2 [2]
    userTag :  3 [3]
[1] int64_t  parent=[0]  [1:2)
[2] int32_t  parent=[0]  [2:3)
[3] int32_t  parent=[0]  [3:4)
)out"
     )<<"Actual:\n"<<descs.data();
}

/*  epics:nt/NTScalarArray:1.0
 *      double[] value
 *      alarm_t alarm
 *          int severity
 *          int status
 *          string message
 *      time_t timeStamp
 *          long secondsPastEpoch
 *          int nanoseconds
 *          int userTag
 */
const uint8_t NTScalar[] = "\x80\x1a""epics:nt/NTScalarArray:1.0\x03"
                            "\x05""valueK"
                            "\x05""alarm\x80\x07""alarm_t\x03"
                                "\x08""severity\""
                                "\x06""status\""
                                "\x07""message`"
                            "\ttimeStamp\x80\x06""time_t\x03"
                                "\x10""secondsPastEpoch#"
                                "\x0b""nanoseconds\""
                                "\x07""userTag\"";

void testXCodeNTScalar()
{
    testDiag("%s", __func__);

    std::vector<uint8_t> msg(NTScalar, NTScalar+sizeof(NTScalar)-1);
    std::vector<FieldDesc> descs;
    TypeStore cache;
    {
        FixedBuf buf(true, msg);
        from_wire(buf, descs, cache);
        testOk1(buf.good());
        testEq(buf.size(), 0u)<<"remaining of "<<msg.size();
    }

    if(testOk1(!descs.empty())) {
        testEq(descs.size(), descs.front().size());
    }

    testEq(std::string(SB()<<"\n"<<descs.data()),
           R"out(
[0] struct epics:nt/NTScalarArray:1.0 parent=[0]  [0:10)
    alarm -> 2 [2]
    alarm.message -> 5 [5]
    alarm.severity -> 3 [3]
    alarm.status -> 4 [4]
    timeStamp -> 6 [6]
    timeStamp.nanoseconds -> 8 [8]
    timeStamp.secondsPastEpoch -> 7 [7]
    timeStamp.userTag -> 9 [9]
    value -> 1 [1]
    value :  1 [1]
    alarm :  2 [2]
    timeStamp :  6 [6]
[1] double[]  parent=[0]  [1:2)
[2] struct alarm_t parent=[0]  [2:6)
    message -> 3 [5]
    severity -> 1 [3]
    status -> 2 [4]
    severity :  1 [3]
    status :  2 [4]
    message :  3 [5]
[3] int32_t  parent=[2]  [3:4)
[4] int32_t  parent=[2]  [4:5)
[5] string  parent=[2]  [5:6)
[6] struct time_t parent=[0]  [6:10)
    nanoseconds -> 2 [8]
    secondsPastEpoch -> 1 [7]
    userTag -> 3 [9]
    secondsPastEpoch :  1 [7]
    nanoseconds :  2 [8]
    userTag :  3 [9]
[7] int64_t  parent=[6]  [7:8)
[8] int32_t  parent=[6]  [8:9)
[9] int32_t  parent=[6]  [9:10)
)out"
    )<<"Actual:\n"<<descs.data();

    testDiag("Round trip back to bytes");
    std::vector<uint8_t> out;
    out.reserve(msg.size());

    {
        VectorOutBuf buf(true, out);
        to_wire(buf, descs.data());
        testOk1(buf.good());
        out.resize(out.size()-buf.size());
    }

    testEq(msg.size(), out.size());
    testEq(msg, out);
}

// has a bit of everything...  (except array of union)
const uint8_t NTNDArray[] = "\x80\x16""epics:nt/NTNDArray:1.0\n"
                                "\x05value\x81\x00\x0b"
                                    "\x0c""booleanValue\x08"
                                    "\tbyteValue("
                                    "\nshortValue)"
                                    "\x08intValue*"
                                    "\tlongValue+"
                                    "\nubyteValue,"
                                    "\x0bushortValue-"
                                    "\tuintValue."
                                    "\nulongValue/"
                                    "\nfloatValueJ"
                                    "\x0b""doubleValueK"
                            "\x05""codec\x80\x07""codec_t\x02"
                                "\x04name`"
                                "\nparameters\x82"
                            "\x0e""compressedSize#"
                            "\x10uncompressedSize#"
                            "\x08uniqueId\""
                            "\rdataTimeStamp\x80\x06time_t\x03"
                                "\x10secondsPastEpoch#"
                                "\x0bnanoseconds\""
                                "\x07userTag\""
                            "\x05""alarm\x80\x07""alarm_t\x03"
                                "\x08severity\""
                                "\x06status\""
                                "\x07message`"
                            "\ttimeStamp\x80\x06time_t\x03"
                                "\x10secondsPastEpoch#"
                                "\x0bnanoseconds\""
                                "\x07userTag\""
                            "\tdimension\x88\x80\x0b""dimension_t\x05"
                                "\x04size\""
                                "\x06offset\""
                                "\x08""fullSize\""
                                "\x07""binning\""
                                "\x07reverse\x00"
                            "\tattribute\x88\x80\x18""epics:nt/NTAttribute:1.0\x08"
                                "\x04name`"
                                "\x05value\x82"
                                "\x04tagsh"
                                "\ndescriptor`"
                                "\x05""alarm\x80\x07""alarm_t\x03"
                                    "\x08severity\""
                                    "\x06status\""
                                    "\x07message`"
                                "\ttimestamp\x80\x06time_t\x03"
                                    "\x10secondsPastEpoch#"
                                    "\x0bnanoseconds\""
                                    "\x07userTag\""
                                "\nsourceType\""
                                "\x06source`";

void testXCodeNTNDArray()
{
    testDiag("%s", __func__);

    std::vector<uint8_t> msg(NTNDArray, NTNDArray+sizeof(NTNDArray)-1);
    std::vector<FieldDesc> descs;
    TypeStore cache;
    {
        FixedBuf buf(true, msg);
        from_wire(buf, descs, cache);
        testOk1(buf.good());
        testEq(buf.size(), 0u)<<"remaining of "<<msg.size();
    }

    if(testOk1(!descs.empty())) {
        testEq(descs.size(), descs.front().size());
    }

    testEq(std::string(SB()<<"\n"<<descs.data()),
R"out(
[0] struct epics:nt/NTNDArray:1.0 parent=[0]  [0:22)
    alarm -> 12 [12]
    alarm.message -> 15 [15]
    alarm.severity -> 13 [13]
    alarm.status -> 14 [14]
    attribute -> 21 [21]
    codec -> 2 [2]
    codec.name -> 3 [3]
    codec.parameters -> 4 [4]
    compressedSize -> 5 [5]
    dataTimeStamp -> 8 [8]
    dataTimeStamp.nanoseconds -> 10 [10]
    dataTimeStamp.secondsPastEpoch -> 9 [9]
    dataTimeStamp.userTag -> 11 [11]
    dimension -> 20 [20]
    timeStamp -> 16 [16]
    timeStamp.nanoseconds -> 18 [18]
    timeStamp.secondsPastEpoch -> 17 [17]
    timeStamp.userTag -> 19 [19]
    uncompressedSize -> 6 [6]
    uniqueId -> 7 [7]
    value -> 1 [1]
    value :  1 [1]
    codec :  2 [2]
    compressedSize :  5 [5]
    uncompressedSize :  6 [6]
    uniqueId :  7 [7]
    dataTimeStamp :  8 [8]
    alarm :  12 [12]
    timeStamp :  16 [16]
    dimension :  20 [20]
    attribute :  21 [21]
[1] union  parent=[0]  [1:2)
    booleanValue -> 0 [0]
    byteValue -> 1 [1]
    doubleValue -> 10 [10]
    floatValue -> 9 [9]
    intValue -> 3 [3]
    longValue -> 4 [4]
    shortValue -> 2 [2]
    ubyteValue -> 5 [5]
    uintValue -> 7 [7]
    ulongValue -> 8 [8]
    ushortValue -> 6 [6]
    booleanValue :  0 [0]
    [0] bool[]  parent=[0]  [0:1)
    byteValue :  1 [1]
    [0] int8_t[]  parent=[0]  [0:1)
    shortValue :  2 [2]
    [0] int16_t[]  parent=[0]  [0:1)
    intValue :  3 [3]
    [0] int32_t[]  parent=[0]  [0:1)
    longValue :  4 [4]
    [0] int64_t[]  parent=[0]  [0:1)
    ubyteValue :  5 [5]
    [0] uint8_t[]  parent=[0]  [0:1)
    ushortValue :  6 [6]
    [0] uint16_t[]  parent=[0]  [0:1)
    uintValue :  7 [7]
    [0] uint32_t[]  parent=[0]  [0:1)
    ulongValue :  8 [8]
    [0] uint64_t[]  parent=[0]  [0:1)
    floatValue :  9 [9]
    [0] float[]  parent=[0]  [0:1)
    doubleValue :  10 [10]
    [0] double[]  parent=[0]  [0:1)
[2] struct codec_t parent=[0]  [2:5)
    name -> 1 [3]
    parameters -> 2 [4]
    name :  1 [3]
    parameters :  2 [4]
[3] string  parent=[2]  [3:4)
[4] any  parent=[2]  [4:5)
[5] int64_t  parent=[0]  [5:6)
[6] int64_t  parent=[0]  [6:7)
[7] int32_t  parent=[0]  [7:8)
[8] struct time_t parent=[0]  [8:12)
    nanoseconds -> 2 [10]
    secondsPastEpoch -> 1 [9]
    userTag -> 3 [11]
    secondsPastEpoch :  1 [9]
    nanoseconds :  2 [10]
    userTag :  3 [11]
[9] int64_t  parent=[8]  [9:10)
[10] int32_t  parent=[8]  [10:11)
[11] int32_t  parent=[8]  [11:12)
[12] struct alarm_t parent=[0]  [12:16)
    message -> 3 [15]
    severity -> 1 [13]
    status -> 2 [14]
    severity :  1 [13]
    status :  2 [14]
    message :  3 [15]
[13] int32_t  parent=[12]  [13:14)
[14] int32_t  parent=[12]  [14:15)
[15] string  parent=[12]  [15:16)
[16] struct time_t parent=[0]  [16:20)
    nanoseconds -> 2 [18]
    secondsPastEpoch -> 1 [17]
    userTag -> 3 [19]
    secondsPastEpoch :  1 [17]
    nanoseconds :  2 [18]
    userTag :  3 [19]
[17] int64_t  parent=[16]  [17:18)
[18] int32_t  parent=[16]  [18:19)
[19] int32_t  parent=[16]  [19:20)
[20] struct[]  parent=[0]  [20:21)
    [0] struct dimension_t parent=[0]  [0:6)
        binning -> 4 [4]
        fullSize -> 3 [3]
        offset -> 2 [2]
        reverse -> 5 [5]
        size -> 1 [1]
        size :  1 [1]
        offset :  2 [2]
        fullSize :  3 [3]
        binning :  4 [4]
        reverse :  5 [5]
    [1] int32_t  parent=[0]  [1:2)
    [2] int32_t  parent=[0]  [2:3)
    [3] int32_t  parent=[0]  [3:4)
    [4] int32_t  parent=[0]  [4:5)
    [5] bool  parent=[0]  [5:6)
[21] struct[]  parent=[0]  [21:22)
    [0] struct epics:nt/NTAttribute:1.0 parent=[0]  [0:15)
        alarm -> 5 [5]
        alarm.message -> 8 [8]
        alarm.severity -> 6 [6]
        alarm.status -> 7 [7]
        descriptor -> 4 [4]
        name -> 1 [1]
        source -> 14 [14]
        sourceType -> 13 [13]
        tags -> 3 [3]
        timestamp -> 9 [9]
        timestamp.nanoseconds -> 11 [11]
        timestamp.secondsPastEpoch -> 10 [10]
        timestamp.userTag -> 12 [12]
        value -> 2 [2]
        name :  1 [1]
        value :  2 [2]
        tags :  3 [3]
        descriptor :  4 [4]
        alarm :  5 [5]
        timestamp :  9 [9]
        sourceType :  13 [13]
        source :  14 [14]
    [1] string  parent=[0]  [1:2)
    [2] any  parent=[0]  [2:3)
    [3] string[]  parent=[0]  [3:4)
    [4] string  parent=[0]  [4:5)
    [5] struct alarm_t parent=[0]  [5:9)
        message -> 3 [8]
        severity -> 1 [6]
        status -> 2 [7]
        severity :  1 [6]
        status :  2 [7]
        message :  3 [8]
    [6] int32_t  parent=[5]  [6:7)
    [7] int32_t  parent=[5]  [7:8)
    [8] string  parent=[5]  [8:9)
    [9] struct time_t parent=[0]  [9:13)
        nanoseconds -> 2 [11]
        secondsPastEpoch -> 1 [10]
        userTag -> 3 [12]
        secondsPastEpoch :  1 [10]
        nanoseconds :  2 [11]
        userTag :  3 [12]
    [10] int64_t  parent=[9]  [10:11)
    [11] int32_t  parent=[9]  [11:12)
    [12] int32_t  parent=[9]  [12:13)
    [13] int32_t  parent=[0]  [13:14)
    [14] string  parent=[0]  [14:15)
)out"
    )<<"Actual:\n"<<descs.data();

    testDiag("Round trip back to bytes");
    std::vector<uint8_t> out;
    out.reserve(msg.size());

    {
        VectorOutBuf buf(true, out);
        to_wire(buf, descs.data());
        testOk1(buf.good());
        out.resize(out.size()-buf.size());
    }

    testEq(msg.size(), out.size());
    testEq(msg, out);
}

// test the common case for a pvRequest of caching an empty Struct
void testEmptyRequest()
{
    testDiag("%s", __func__);

    TypeStore registry;

    std::vector<FieldDesc> descs1;
    {
        uint8_t msg[] = "\xfd\x02\x00\x80\x00\x00";
        FixedBuf buf(false, msg);
        from_wire(buf, descs1, registry);
        testOk1(buf.good());
        testEq(buf.size(), 0u)<<"remaining of "<<sizeof(msg-1);
    }

    if(testEq(registry.size(), 1u)) {
        testEq(registry[2].size(), 1u);
    }

    std::vector<FieldDesc> descs2;
    {
        uint8_t msg[] = "\xfe\x02\x00";
        FixedBuf buf(false, msg);
        from_wire(buf, descs2, registry);
        testOk1(buf.good());
        testEq(buf.size(), 0u)<<"remaining of "<<sizeof(msg-1);
    }

    testEq(descs1.size(), 1u);
    testEq(descs2.size(), 1u);

    testEq(std::string(SB()<<descs1.data()),
           "[0] struct  parent=[0]  [0:1)\n")<<"\nActual descs1\n"<<descs1.data();

    testEq(std::string(SB()<<descs2.data()),
           "[0] struct  parent=[0]  [0:1)\n")<<"\nActual descs2\n"<<descs2.data();
}

} // namespace

MAIN(testxcode)
{
    testPlan(34);
    testDecode1();
    testXCodeNTScalar();
    testXCodeNTNDArray();
    testEmptyRequest();
    return testDone();
}
