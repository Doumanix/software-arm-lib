/*
 * Telegram building blocks for the protocol tests of SYSTEM B (interface objects 1 to 5, connected property services).
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#ifndef PROT_SYSTEMB_TELEGRAMS_H_
#define PROT_SYSTEMB_TELEGRAMS_H_

// SYSTEM B: building blocks for one connected property exchange (request, T_ACK, response, T_ACK) with sequence number seq
#define TPCI_DATA(seq) (0x43 + 4 * (seq))
#define TPCI_ACK(seq) (0xC2 + 4 * (seq))
#define TEL_ACK_TX(seq) {TEL_TX, 7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, TPCI_ACK(seq)}}
#define TEL_ACK_RX(seq) {TEL_RX, 7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, TPCI_ACK(seq)}}
#define TEL_CONNECT {TEL_RX, 7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}}
#define TEL_DISCONNECT {TEL_RX, 7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x81}}

#define TEL_DESC_READ(seq, obj, pid) \
    {TEL_RX, 11, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, TPCI_DATA(seq), 0xD8, obj, pid, 0x00}}, \
    TEL_ACK_TX(seq)
#define TEL_DESC_RESPONSE(seq, obj, pid, type, elems, access) \
    {TEL_TX, 15, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x68, TPCI_DATA(seq), 0xD9, obj, pid, 0x00, type, 0x00, elems, access}}, \
    TEL_ACK_RX(seq)

#define TEL_VALUE_READ(seq, obj, pid) \
    {TEL_RX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, TPCI_DATA(seq), 0xD5, obj, pid, 0x10, 0x01}}, \
    TEL_ACK_TX(seq)
#define TEL_VALUE_WRITE_1(seq, obj, pid, d0) \
    {TEL_RX, 13, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, TPCI_DATA(seq), 0xD7, obj, pid, 0x10, 0x01, d0}}, \
    TEL_ACK_TX(seq)
#define TEL_VALUE_WRITE_5(seq, obj, pid, d0, d1, d2, d3, d4) \
    {TEL_RX, 17, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, TPCI_DATA(seq), 0xD7, obj, pid, 0x10, 0x01, d0, d1, d2, d3, d4}}, \
    TEL_ACK_TX(seq)
#define TEL_VALUE_WRITE_10(seq, obj, pid, d0, d1, d2, d3, d4, d5, d6, d7, d8, d9)     {TEL_RX, 22, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, TPCI_DATA(seq), 0xD7, obj, pid, 0x10, 0x01, d0, d1, d2, d3, d4, d5, d6, d7, d8, d9}},     TEL_ACK_TX(seq)
#define TEL_VALUE_RESPONSE_1(seq, obj, pid, d0) \
    {TEL_TX, 13, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x66, TPCI_DATA(seq), 0xD6, obj, pid, 0x10, 0x01, d0}}, \
    TEL_ACK_RX(seq)
#define TEL_VALUE_RESPONSE_2(seq, obj, pid, d0, d1) \
    {TEL_TX, 14, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x67, TPCI_DATA(seq), 0xD6, obj, pid, 0x10, 0x01, d0, d1}}, \
    TEL_ACK_RX(seq)
#define TEL_VALUE_RESPONSE_4(seq, obj, pid, d0, d1, d2, d3) \
    {TEL_TX, 16, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x69, TPCI_DATA(seq), 0xD6, obj, pid, 0x10, 0x01, d0, d1, d2, d3}}, \
    TEL_ACK_RX(seq)
#define TEL_VALUE_RESPONSE_5(seq, obj, pid, d0, d1, d2, d3, d4) \
    {TEL_TX, 17, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x6A, TPCI_DATA(seq), 0xD6, obj, pid, 0x10, 0x01, d0, d1, d2, d3, d4}}, \
    TEL_ACK_RX(seq)

// PID_LOAD_STATE_CONTROL (05h): write the load event, the response holds the new load state
#define TEL_LOAD_EVENT(seq, obj, event, state)     TEL_VALUE_WRITE_10(seq, obj, 0x05, event, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),     TEL_VALUE_RESPONSE_1(seq, obj, 0x05, state)
// Additional Load Controls with the segment type 0Bh (data relative allocation), requested size 0x10
#define TEL_LOAD_ADDITIONAL(seq, obj, state)     TEL_VALUE_WRITE_10(seq, obj, 0x05, 0x03, 0x0B, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00),     TEL_VALUE_RESPONSE_1(seq, obj, 0x05, state)

// Data relative allocation (segment type 0Bh), the size is 16 bit here: size 4 octets, mode, fill, 2 reserved
#define TEL_START_LOADING(seq, obj) TEL_LOAD_EVENT(seq, obj, 0x01, 0x02)
#define TEL_ALLOC(seq, obj, sizeH, sizeL, mode, fill, state) \
    TEL_VALUE_WRITE_10(seq, obj, 0x05, 0x03, 0x0B, 0x00, 0x00, sizeH, sizeL, mode, fill, 0x00, 0x00), \
    TEL_VALUE_RESPONSE_1(seq, obj, 0x05, state)
#define TEL_TABLE_REFERENCE(seq, obj, a0, a1) \
    TEL_VALUE_READ(seq, obj, 0x07), TEL_VALUE_RESPONSE_4(seq, obj, 0x07, 0x00, 0x00, a0, a1)
#define TEL_LOAD_STATE(seq, obj, state) TEL_VALUE_READ(seq, obj, 0x05), TEL_VALUE_RESPONSE_1(seq, obj, 0x05, state)
// PID_MCB_TABLE: octets 2..5 hold the size, the CRC in the octets 6..7 is still 0
#define TEL_MCB_SIZE(seq, obj, s0, s1) \
    TEL_VALUE_READ(seq, obj, 0x1B), \
    {TEL_TX, 20, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x6D, TPCI_DATA(seq), 0xD6, obj, 0x1B, 0x10, 0x01, \
                              0x00, 0x00, 0x00, 0x00, s0, s1, 0x00, 0x00}}, \
    TEL_ACK_RX(seq)
// PID_MCB_TABLE with the size in the octets 2..5 and the CRC16 of the segment in the octets 6..7
#define TEL_MCB_SIZE_CRC(seq, obj, s0, s1, c0, c1) \
    TEL_VALUE_READ(seq, obj, 0x1B), \
    {TEL_TX, 20, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x6D, TPCI_DATA(seq), 0xD6, obj, 0x1B, 0x10, 0x01, \
                              0x00, 0x00, 0x00, 0x00, s0, s1, c0, c1}}, \
    TEL_ACK_RX(seq)
// A_Memory_Read of 2 octets, the address is relative to the EEPROM start + 0x100 like for BCU1 (telegramPreparation())
#define TEL_MEMORY_READ_2(seq, hi, lo, d0, d1) \
    {TEL_RX, 10, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x64, TPCI_DATA(seq) - 1, 0x02, hi, lo}}, \
    TEL_ACK_TX(seq), \
    {TEL_TX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x65, TPCI_DATA(seq) - 1, 0x42, hi, lo, d0, d1}}, \
    TEL_ACK_RX(seq)
// A_Memory_Write and A_Memory_Read of 8 octets, same addressing as TEL_MEMORY_READ_2. The device is in verify mode
// (DEVCTRL_MEM_AUTO_RESPONSE, see tc_setupVerifyMode()), so a write is answered with the written data.
#define TEL_MEMORY_WRITE_8(seq, hi, lo, d0, d1, d2, d3, d4, d5, d6, d7) \
    {TEL_RX, 18, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x6B, TPCI_DATA(seq) - 1, 0x88, hi, lo, \
                              d0, d1, d2, d3, d4, d5, d6, d7}}, \
    TEL_ACK_TX(seq), \
    {TEL_TX, 18, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x6B, TPCI_DATA(seq) - 1, 0x48, hi, lo, \
                              d0, d1, d2, d3, d4, d5, d6, d7}}, \
    TEL_ACK_RX(seq)
#define TEL_MEMORY_READ_8(seq, hi, lo, d0, d1, d2, d3, d4, d5, d6, d7) \
    {TEL_RX, 10, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x64, TPCI_DATA(seq) - 1, 0x08, hi, lo}}, \
    TEL_ACK_TX(seq), \
    {TEL_TX, 18, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x6B, TPCI_DATA(seq) - 1, 0x48, hi, lo, \
                              d0, d1, d2, d3, d4, d5, d6, d7}}, \
    TEL_ACK_RX(seq)

#endif /* PROT_SYSTEMB_TELEGRAMS_H_ */
