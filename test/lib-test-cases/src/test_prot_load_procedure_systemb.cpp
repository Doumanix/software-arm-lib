/*
 * Protocol tests of the ETS load procedures "Load ap1" and "Unload all" of SYSTEM B (mask version 07B0h).
 * The telegram sequence follows the procedures of the mask version in the knx_master.xml of the product database.
 *
 * \author Doumanix <doumanix\gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include <protocol.h>
#include "prot_systemb_telegrams.h"

#include <catch.hpp> // If possible, include catch.hpp as last header

#define MANUFACTURER (0x0004)
#define DEVICE       (0x2060)
#define VERSION      (0x01)

#define OWN_KNX_ADDRESS (0xA000) // own address 10.0.0

// the ETS writes the tables with the verify mode, a write is answered with the written data
static void tc_setup(Telegram* tel, uint16_t telCount)
{
    bcuUnderTest->setOwnAddress(OWN_KNX_ADDRESS);
    telegramPreparation(bcuUnderTest, tel, telCount);
    bcuUnderTest->userRam->deviceControl() |= DEVCTRL_MEM_AUTO_RESPONSE;
}

#define TEL_UNLOAD(seq, obj) TEL_LOAD_EVENT(seq, obj, 0x04, 0x00)
#define TEL_LOAD_COMPLETED(seq, obj) TEL_LOAD_EVENT(seq, obj, 0x02, 0x01)

// Load ap1 (load procedure "ap1"), s is the sequence number of the first telegram, the procedure uses 25 numbers:
// unload the objects 5 to 1, start loading object 4, the objects 3, 1 and 2 with their data relative segments (sizes
// 16, 8 and 8 octets, allocated ascending from 0x3700), write the tables 3, 2 and 1 (the memory addresses 0x0500,
// 0x0518 and 0x0510 are 0x3700, 0x3718 and 0x3710) and read them back, write the version of the application program,
// load completed 4 to 1. The tables hold the octets 30h..3Fh (object 3), 20h..27h (object 2) and 10h..17h (object 1).
#define TEL_LOAD_AP1_N 25
#define TEL_LOAD_AP1(s) \
    TEL_UNLOAD((s) + 0, 5), TEL_UNLOAD((s) + 1, 4), TEL_UNLOAD((s) + 2, 3), TEL_UNLOAD((s) + 3, 2), \
    TEL_UNLOAD((s) + 4, 1), \
    TEL_START_LOADING((s) + 5, 4), \
    TEL_START_LOADING((s) + 6, 3), TEL_ALLOC((s) + 7, 3, 0x00, 0x10, 0x00, 0x00, 0x02), \
    TEL_START_LOADING((s) + 8, 1), TEL_ALLOC((s) + 9, 1, 0x00, 0x08, 0x00, 0x00, 0x02), \
    TEL_START_LOADING((s) + 10, 2), TEL_ALLOC((s) + 11, 2, 0x00, 0x08, 0x00, 0x00, 0x02), \
    TEL_MEMORY_WRITE_8((s) + 12, 0x05, 0x00, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37), \
    TEL_MEMORY_WRITE_8((s) + 13, 0x05, 0x08, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F), \
    TEL_MEMORY_WRITE_8((s) + 14, 0x05, 0x18, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27), \
    TEL_MEMORY_WRITE_8((s) + 15, 0x05, 0x10, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17), \
    TEL_MEMORY_READ_8((s) + 16, 0x05, 0x00, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37), \
    TEL_MEMORY_READ_8((s) + 17, 0x05, 0x08, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F), \
    TEL_MEMORY_READ_8((s) + 18, 0x05, 0x18, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27), \
    TEL_MEMORY_READ_8((s) + 19, 0x05, 0x10, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17), \
    TEL_VALUE_WRITE_5((s) + 20, 4, 0x0D, 0x00, 0x04, 0x20, 0x60, 0x01), \
    TEL_VALUE_RESPONSE_5((s) + 20, 4, 0x0D, 0x00, 0x04, 0x20, 0x60, 0x01), \
    TEL_LOAD_COMPLETED((s) + 21, 4), TEL_LOAD_COMPLETED((s) + 22, 3), TEL_LOAD_COMPLETED((s) + 23, 2), \
    TEL_LOAD_COMPLETED((s) + 24, 1)

// State after Load ap1, uses 12 sequence numbers. The CRC16 of the segments (start value 1D0Fh, see
// test_prot_apci.cpp): octets 10h..17h 9A1Dh, octets 20h..27h 7CA4h, octets 30h..3Fh DB65h. Object 4 has no segment.
#define TEL_LOADED_AP1_N 12
#define TEL_CHECK_LOADED_AP1(s) \
    TEL_LOAD_STATE((s) + 0, 1, 0x01), TEL_LOAD_STATE((s) + 1, 2, 0x01), TEL_LOAD_STATE((s) + 2, 3, 0x01), \
    TEL_LOAD_STATE((s) + 3, 4, 0x01), TEL_LOAD_STATE((s) + 4, 5, 0x00), \
    TEL_TABLE_REFERENCE((s) + 5, 3, 0x37, 0x00), TEL_TABLE_REFERENCE((s) + 6, 1, 0x37, 0x10), \
    TEL_TABLE_REFERENCE((s) + 7, 2, 0x37, 0x18), \
    TEL_MCB_SIZE_CRC((s) + 8, 1, 0x00, 0x08, 0x9A, 0x1D), TEL_MCB_SIZE_CRC((s) + 9, 2, 0x00, 0x08, 0x7C, 0xA4), \
    TEL_MCB_SIZE_CRC((s) + 10, 3, 0x00, 0x10, 0xDB, 0x65), TEL_MCB_SIZE_CRC((s) + 11, 4, 0x00, 0x00, 0x00, 0x00), \
    TEL_CHECK_APPLICATION_RUNNING(1)

// Unload all (load procedure "all"), uses 5 sequence numbers
#define TEL_UNLOAD_ALL_N 5
#define TEL_UNLOAD_ALL(s) \
    TEL_UNLOAD((s) + 0, 1), TEL_UNLOAD((s) + 1, 2), TEL_UNLOAD((s) + 2, 3), TEL_UNLOAD((s) + 3, 4), \
    TEL_UNLOAD((s) + 4, 5)

// State after Unload all, uses 15 sequence numbers
#define TEL_UNLOADED_ALL_N 15
#define TEL_CHECK_UNLOADED_ALL(s) \
    TEL_LOAD_STATE((s) + 0, 1, 0x00), TEL_LOAD_STATE((s) + 1, 2, 0x00), TEL_LOAD_STATE((s) + 2, 3, 0x00), \
    TEL_LOAD_STATE((s) + 3, 4, 0x00), TEL_LOAD_STATE((s) + 4, 5, 0x00), \
    TEL_TABLE_REFERENCE((s) + 5, 1, 0x00, 0x00), TEL_TABLE_REFERENCE((s) + 6, 2, 0x00, 0x00), \
    TEL_TABLE_REFERENCE((s) + 7, 3, 0x00, 0x00), TEL_TABLE_REFERENCE((s) + 8, 4, 0x00, 0x00), \
    TEL_TABLE_REFERENCE((s) + 9, 5, 0x00, 0x00), \
    TEL_MCB_SIZE_CRC((s) + 10, 1, 0x00, 0x00, 0x00, 0x00), TEL_MCB_SIZE_CRC((s) + 11, 2, 0x00, 0x00, 0x00, 0x00), \
    TEL_MCB_SIZE_CRC((s) + 12, 3, 0x00, 0x00, 0x00, 0x00), TEL_MCB_SIZE_CRC((s) + 13, 4, 0x00, 0x00, 0x00, 0x00), \
    TEL_MCB_SIZE_CRC((s) + 14, 5, 0x00, 0x00, 0x00, 0x00), \
    TEL_CHECK_APPLICATION_RUNNING(0)

#define TEL_SEQ_LOADED_AP1 (TEL_LOAD_AP1_N)
#define TEL_SEQ_CHECKED_AP1 (TEL_LOAD_AP1_N + TEL_LOADED_AP1_N)

static Telegram loadAp1Telegrams[] =
{
    TEL_CONNECT,
    TEL_LOAD_AP1(0),
    TEL_CHECK_LOADED_AP1(TEL_SEQ_LOADED_AP1),
    // A_Restart ends the procedure, it is confirmed with a T_ACK
    {TEL_RX, 8, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, TPCI_DATA(TEL_SEQ_CHECKED_AP1), 0x80}},
    TEL_ACK_TX(TEL_SEQ_CHECKED_AP1),
    TEL_CHECK_APPLICATION_RUNNING(1),
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram unloadAllTelegrams[] =
{
    TEL_CONNECT,
    TEL_LOAD_AP1(0),
    TEL_CHECK_LOADED_AP1(TEL_SEQ_LOADED_AP1),
    TEL_UNLOAD_ALL(TEL_SEQ_CHECKED_AP1),
    TEL_CHECK_UNLOADED_ALL(TEL_SEQ_CHECKED_AP1 + TEL_UNLOAD_ALL_N),
    {END, 0, 0, 0, nullptr, {0}}
};

// the ETS repeats the download: the second run gets the same bases and CRCs
static Telegram loadAp1TwiceTelegrams[] =
{
    TEL_CONNECT,
    TEL_LOAD_AP1(0),
    TEL_CHECK_LOADED_AP1(TEL_SEQ_LOADED_AP1),
    TEL_LOAD_AP1(TEL_SEQ_CHECKED_AP1),
    TEL_CHECK_LOADED_AP1(TEL_SEQ_CHECKED_AP1 + TEL_LOAD_AP1_N),
    {END, 0, 0, 0, nullptr, {0}}
};

#define PROCEDURE_TEST_CASE(NAME, TITLE, TELEGRAMS) \
    static Test_Case NAME = {TITLE, MANUFACTURER, DEVICE, VERSION, 0, NULL, tc_setup, NULL, NULL, NULL, TELEGRAMS}
PROCEDURE_TEST_CASE(testCaseLoadAp1, "SYSTEM B procedure Load ap1", loadAp1Telegrams);
PROCEDURE_TEST_CASE(testCaseUnloadAll, "SYSTEM B procedure Unload all", unloadAllTelegrams);
PROCEDURE_TEST_CASE(testCaseLoadAp1Twice, "SYSTEM B procedure Load ap1 twice", loadAp1TwiceTelegrams);

TEST_CASE("SYSTEM B load procedures", "[protocol][load-procedure]")
{
    executeTest(BCU_07B0, &testCaseLoadAp1);
    executeTest(BCU_07B0, &testCaseUnloadAll);
    executeTest(BCU_07B0, &testCaseLoadAp1Twice);
}
