/******************************************************************************
 * @addtogroup SBLIB_MAIN_GROUP Selfbus KNX-Library
 * @defgroup SBLIB_SUB_GROUP_KNX KNX NPDU Handling
 * @ingroup SBLIB_MAIN_GROUP
 * @brief   
 * @details 
 *
 *
 * @{
 *
 * @file   knx_npdu.h
 * @author Darthyson <darth@maptrack.de> Copyright (c) 2022
 * @bug No known bugs.
 ******************************************************************************/

/*
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License version 3 as
 published by the Free Software Foundation.
 ---------------------------------------------------------------------------*/

#ifndef SBLIB_KNX_NPDU_H_
#define SBLIB_KNX_NPDU_H_

#include "sblib/eib/bcu_const.h"
#include "sblib/eib/knx_lpdu.h"

#define NPDU_CONTROL_BYTE   (5)

enum NPDU
{
    N_DATA_INDIVIDUAL,
    N_DATA_GROUP,
    N_DATA_BROADCAST,
    N_DATA_SYSTEM_BROADCAST
};

enum Hop_count_type {};

/**
 * Get the size of a telegram, including the protocol header but excluding
 * the checksum byte. The size is calculated by getting the length from byte 5 of the
 * telegram and adding 7 for the protocol overhead. With SBLIB_MAX_APDU_LENGTH > 15
 * also extended frames, see @ref lpduLength(). With the default 15 the expression is
 * unchanged, so the generated code stays the same.
 *
 * @param tel - the telegram to get the size
 *
 * @return The size of the telegram, excluding the checksum byte.
 */
#if SBLIB_MAX_APDU_LENGTH > 15
#   define telegramSize(tel) lpduLength(tel)
#else
#   define telegramSize(tel) (7 + (tel[5] & 15)) //FIXME telegramSize accesses tel[5] without any check
#endif

/**
 * Get the index of the TPCI octet of a telegram. The APCI starts in this octet and continues
 * in the next one. With SBLIB_MAX_APDU_LENGTH > 15 the index is 6 for a standard frame and 7 for
 * an extended frame, see @ref lpduTpciIndex(). With the default 15 only standard frames are
 * received, the index is the constant 6, so the generated code stays the same.
 *
 * @param tel - the telegram
 *
 * @return The index of the TPCI octet.
 */
#if SBLIB_MAX_APDU_LENGTH > 15
#   define telegramTpciIndex(tel) lpduTpciIndex(tel)
#else
#   define telegramTpciIndex(tel) (6)
#endif

/**
 * Get the source address, the destination address and the address type of a telegram. With
 * SBLIB_MAX_APDU_LENGTH > 15 for both frame formats, see @ref lpduSenderAddress(),
 * @ref lpduDestinationAddress() and @ref lpduIsGroupAddress(). With the default 15 only standard
 * frames are received and the standard frame functions are used, so the generated code stays the same.
 *
 * @param tel - the telegram
 */
#if SBLIB_MAX_APDU_LENGTH > 15
#   define telegramSenderAddress(tel) lpduSenderAddress(tel)
#   define telegramDestinationAddress(tel) lpduDestinationAddress(tel)
#   define telegramIsGroupAddress(tel) lpduIsGroupAddress(tel)
#else
#   define telegramSenderAddress(tel) senderAddress(tel)
#   define telegramDestinationAddress(tel) destinationAddress(tel)
#   define telegramIsGroupAddress(tel) ((tel)[5] & 0x80)
#endif

/**
 * Set the APDU length (number of octets after the TPCI octet) of a telegram to send, hop count 6.
 * telegramSetApduLength() for an individual destination address, telegramSetGroupApduLength() for a group
 * or the broadcast address, telegramAddApduLength() adds octets and keeps the address type. With
 * SBLIB_MAX_APDU_LENGTH > 15 the frame format follows the length: a standard frame up to 15 octets,
 * an extended frame above (KNX 3/2/2 2.2.5.1), see @ref lpduSetFrameApduLength(). Octets of the APDU written
 * before move with the TPCI octet, so the APDU is addressed relative to @ref telegramTpciIndex() of the
 * send buffer. With the default 15 only standard frames are sent and the expressions are unchanged, so the
 * generated code stays the same.
 *
 * @param tel - the telegram to send, with its control field and destination address
 * @param n - the APDU length or the number of octets to add
 */
#if SBLIB_MAX_APDU_LENGTH > 15
#   define telegramSetApduLength(tel, n) lpduSetFrameApduLength((tel), 0x60, static_cast<uint8_t>(n))
#   define telegramSetGroupApduLength(tel, n) lpduSetFrameApduLength((tel), 0xe0, static_cast<uint8_t>(n))
#   define telegramAddApduLength(tel, n) lpduAddFrameApduLength((tel), static_cast<uint8_t>(n))
#else
#   define telegramSetApduLength(tel, n) (tel)[5] = 0x60 + (n)
#   define telegramSetGroupApduLength(tel, n) (tel)[5] = 0xe0 | (n)
#   define telegramAddApduLength(tel, n) (tel)[5] += (n)
#endif


#endif /* SBLIB_KNX_NPDU_H_ */
/** @}*/
