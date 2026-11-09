/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#ifndef SBLIB_KNX_BCU_CONST_H_
#define SBLIB_KNX_BCU_CONST_H_

#include <cstdint>

#if !defined(SBLIB_MAX_APDU_LENGTH)
    /**
     * Maximum APDU length of a received telegram: number of octets after the TPCI octet (KNX 3/2/2 2.2.5).
     * 15 accepts standard frames only, 16..254 also L_Data_Extended frames. Set by the CMake option of the same name.
     */
#   define SBLIB_MAX_APDU_LENGTH 15
#endif

/**
 * Maximum APDU length of a standard frame: number of octets after the TPCI octet (KNX 3/2/2 2.2.5).
 * Above this length an L_Data_Extended frame is used. A #define, because it is used in #if.
 */
#define SBLIB_STANDARD_APDU_MAX_LENGTH 15

constexpr uint8_t LPDU_STANDARD_HEADER_LENGTH = 7;  //!< Octets of a standard frame up to and including the TPCI octet
constexpr uint8_t LPDU_EXTENDED_HEADER_LENGTH = 8;  //!< Octets of an extended frame up to and including the TPCI octet
constexpr uint8_t LPDU_CHECKSUM_LENGTH = 1;         //!< Checksum octet

static_assert(SBLIB_MAX_APDU_LENGTH >= SBLIB_STANDARD_APDU_MAX_LENGTH && SBLIB_MAX_APDU_LENGTH <= 254,
              "SBLIB_MAX_APDU_LENGTH must be 15..254");

#if SBLIB_MAX_APDU_LENGTH > SBLIB_STANDARD_APDU_MAX_LENGTH
    // CTRL, CTRLE, source, destination, length, TPCI, APDU, checksum
    /** TL4, Tx/Rx KNX telegram buffer size in bytes to allocate */
    constexpr uint16_t TelegramBufferSize = LPDU_EXTENDED_HEADER_LENGTH + SBLIB_MAX_APDU_LENGTH + LPDU_CHECKSUM_LENGTH;
#else
    /**
     * TL4, Tx/Rx KNX telegram buffer size in bytes to allocate, 23 octets like STANDARD_FRAME_MAX_SIZE in bus_const.h
     */
    constexpr uint16_t TelegramBufferSize =
        LPDU_STANDARD_HEADER_LENGTH + SBLIB_STANDARD_APDU_MAX_LENGTH + LPDU_CHECKSUM_LENGTH;
#endif

/**
 * BCU status bits for @ref status()
 * See BCU1 / BCU2 help for detailed description.
 */
enum BcuStatus
{
    BCU_STATUS_PROGRAMMING_MODE = 0x01,  //!< Programming mode: 0=normal mode, 1=programming mode
    BCU_STATUS_LINK_LAYER = 0x02,        //!< Link layer mode (1), or bus monitor mode (0)
    BCU_STATUS_TRANSPORT_LAYER = 0x04,   //!< If bit is set, transport layer is enabled
    BCU_STATUS_APPLICATION_LAYER = 0x08, //!< If bit is set, application layer is enabled
    BCU_STATUS_SERIAL_PEI = 0x10,        //!< If bit is set, serial PEI is enabled
    BCU_STATUS_USER_MODE = 0x20,         //!< If bit is set, application layer/program is enabled
    BCU_STATUS_DOWNLOAD_MODE = 0x40,     //!< If bit is set, BCU is in download mode, else normal operation
    BCU_STATUS_PARITY = 0x80,            //!< Parity bit: even parity for bits 0..6
};

#endif /* SBLIB_KNX_BCU_CONST_H_ */
