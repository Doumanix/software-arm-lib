/******************************************************************************
 * @addtogroup SBLIB_MAIN_GROUP Selfbus KNX-Library
 * @defgroup SBLIB_SUB_GROUP_KNX KNX LPDU Handling
 * @ingroup SBLIB_MAIN_GROUP
 * @brief
 * @details
 *
 *
 * @{
 *
 * @file   knx_lpdu.h
 * @author Darthyson <darth@maptrack.de> Copyright (c) 2022
 * @bug No known bugs.
 ******************************************************************************/

/*
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License version 3 as
 published by the Free Software Foundation.
 ---------------------------------------------------------------------------*/

#ifndef SBLIB_KNX_LPDU_H_
#define SBLIB_KNX_LPDU_H_

#include <cstdint>


/**
 * Default physical address 15.15.255
 */
constexpr uint16_t PHY_ADDR_DEFAULT = 0xffff;

/**
 * Broadcast address 0.0.0
 */
constexpr uint16_t PHY_ADDR_BROADCAST = 0;

/**
 *  KNX Priority
 */
enum KNXPriority : uint8_t
{
    PRIORITY_SYSTEM = 0,
    PRIORITY_HIGH = 1,
    PRIORITY_ALARM = 2,
    PRIORITY_LOW = 3
};

/**
 *  KNX Frame format
 */
enum KNXFrameType
{
    FRAME_STANDARD,
    FRAME_EXTENDED
};

/**
 * Initialize a KNX LPDU telegram
 * @param telegram     Pointer to the telegram buffer
 * @param newPriority  Priority level for the telegram
 * @param newRepeated  Flag indicating if this is a repeated telegram
 * @param newFrameType Frame type (standard or extended)
 */
void initLpdu(uint8_t* telegram, KNXPriority newPriority, bool newRepeated, KNXFrameType newFrameType);

/**
 * Get the control byte from a KNX telegram
 * @param telegram Pointer to the telegram buffer
 * @return The control byte value
 */
uint8_t controlByte(const uint8_t* telegram);

/**
 * Check if a telegram is repeated
 * @param telegram Pointer to the telegram buffer
 * @return true if the telegram is repeated, false otherwise
 */
bool isRepeated(const uint8_t* telegram);

/**
 * Set the repeated flag in a telegram
 * @param telegram Pointer to the telegram buffer
 * @param repeated true to mark as repeated, false otherwise
 */
void setRepeated(uint8_t* telegram, bool repeated);

/**
 * Get the priority of a telegram
 * @param telegram Pointer to the telegram buffer
 * @return The priority level of the telegram
 */
KNXPriority priority(const uint8_t* telegram);

/**
 * Set the priority of a telegram
 * @param telegram Pointer to the telegram buffer
 * @param newPriority The new priority level to set
 */
void setPriority(uint8_t* telegram, KNXPriority newPriority);

/**
 * Get the sender address from a telegram
 * @param telegram Pointer to the telegram buffer
 * @return The sender's physical address
 */
uint16_t senderAddress(const uint8_t* telegram);

/**
 * Set the sender address in a telegram
 * @param telegram Pointer to the telegram buffer
 * @param newSenderAddress The sender's physical address to set
 */
void setSenderAddress(uint8_t* telegram, uint16_t newSenderAddress);

/**
 * Get the destination address from a telegram
 * @param telegram Pointer to the telegram buffer
 * @return The destination address (physical or group)
 */
uint16_t destinationAddress(const uint8_t* telegram);

/**
 * Set the destination address in a telegram
 * @param telegram Pointer to the telegram buffer
 * @param newDestinationAddress The destination address to set
 */
void setDestinationAddress(uint8_t* telegram, uint16_t newDestinationAddress);

/**
 * Get the frame type from a telegram
 * @param telegram Pointer to the telegram buffer
 * @return The frame type (standard or extended)
 */
KNXFrameType frameType(const uint8_t* telegram);

/**
 * Set the frame type in a telegram
 * @param telegram Pointer to the telegram buffer
 * @param newFrameType The frame type to set (standard or extended)
 */
void setFrameType(uint8_t* telegram, KNXFrameType newFrameType);

/*
 * Helpers for both frame formats (KNX 3/2/2 2.2.4 standard frame, 2.2.5 L_Data_Extended frame).
 *
 * Octet  Standard frame                         Extended frame
 *   0    CTRL (frame type bit 7 = 1)            CTRL (frame type bit 7 = 0)
 *   1    source address high                    CTRLE: address type, hop count, extended frame format
 *   2    source address low                     source address high
 *   3    destination address high               source address low
 *   4    destination address low                destination address high
 *   5    address type, hop count, length 0..15  destination address low
 *   6    TPCI                                   length 0..254 (number of octets after the TPCI octet)
 *   7    APDU ...                               TPCI
 */

/**
 * @brief Check whether a telegram is an L_Data_Extended frame.
 * @param telegram Pointer to the telegram buffer
 * @return True if the frame type bit (bit 7 of the control field) is 0
 */
[[nodiscard]] constexpr bool isExtendedFrame(const uint8_t* telegram)
{
    return (telegram[0] & 0x80) == 0;
}

/**
 * @brief Index of the TPCI octet.
 * @param telegram Pointer to the telegram buffer
 * @return 6 for a standard frame, 7 for an extended frame
 */
[[nodiscard]] constexpr uint8_t lpduTpciIndex(const uint8_t* telegram)
{
    if (isExtendedFrame(telegram))
    {
        return 7;
    }
    return 6;
}

/**
 * @brief APDU length of a standard frame: number of octets after the TPCI octet (octet 5, bits 0..3).
 * @param telegram Pointer to the telegram buffer
 * @return The APDU length 0..15
 */
[[nodiscard]] constexpr uint8_t standardFrameApduLength(const uint8_t* telegram)
{
    return telegram[5] & 0x0f;
}

/**
 * @brief APDU length of an extended frame: number of octets after the TPCI octet (octet 6).
 * @param telegram Pointer to the telegram buffer
 * @return The APDU length 0..255 (255 is an escape code and not a valid length)
 */
[[nodiscard]] constexpr uint8_t extendedFrameApduLength(const uint8_t* telegram)
{
    return telegram[6];
}

/**
 * @brief APDU length: number of octets after the TPCI octet.
 * @param telegram Pointer to the telegram buffer
 * @return The APDU length of a standard or an extended frame
 */
[[nodiscard]] constexpr uint8_t lpduApduLength(const uint8_t* telegram)
{
    if (isExtendedFrame(telegram))
    {
        return extendedFrameApduLength(telegram);
    }
    return standardFrameApduLength(telegram);
}

/**
 * @brief Length of a standard frame without the checksum octet.
 * @param telegram Pointer to the telegram buffer
 * @return 7 + APDU length
 */
[[nodiscard]] constexpr uint16_t standardFrameLength(const uint8_t* telegram)
{
    return 7 + standardFrameApduLength(telegram);
}

/**
 * @brief Length of an extended frame without the checksum octet.
 * @param telegram Pointer to the telegram buffer
 * @return 8 + APDU length
 */
[[nodiscard]] constexpr uint16_t extendedFrameLength(const uint8_t* telegram)
{
    return 8 + extendedFrameApduLength(telegram);
}

/**
 * @brief Length of a telegram without the checksum octet.
 * @param telegram Pointer to the telegram buffer
 * @return The length of a standard or an extended frame
 */
[[nodiscard]] constexpr uint16_t lpduLength(const uint8_t* telegram)
{
    if (isExtendedFrame(telegram))
    {
        return extendedFrameLength(telegram);
    }
    return standardFrameLength(telegram);
}

/**
 * @brief Index of the source address high byte.
 * @param telegram Pointer to the telegram buffer
 * @return 1 for a standard frame, 2 for an extended frame
 */
[[nodiscard]] constexpr uint8_t lpduSenderIndex(const uint8_t* telegram)
{
    if (isExtendedFrame(telegram))
    {
        return 2;
    }
    return 1;
}

/**
 * @brief Source address of a telegram.
 * @param telegram Pointer to the telegram buffer
 * @return The sender's physical address of a standard or an extended frame
 */
[[nodiscard]] constexpr uint16_t lpduSenderAddress(const uint8_t* telegram)
{
    const uint8_t index = lpduSenderIndex(telegram);
    return static_cast<uint16_t>(telegram[index] << 8 | telegram[index + 1]);
}

/**
 * @brief Destination address of a telegram.
 * @param telegram Pointer to the telegram buffer
 * @return The destination address (physical or group) of a standard or an extended frame
 */
[[nodiscard]] constexpr uint16_t lpduDestinationAddress(const uint8_t* telegram)
{
    const uint8_t index = lpduSenderIndex(telegram) + 2;
    return static_cast<uint16_t>(telegram[index] << 8 | telegram[index + 1]);
}

/**
 * @brief Octet with the address type and the hop count.
 * @param telegram Pointer to the telegram buffer
 * @return 5 for a standard frame, 1 (CTRLE) for an extended frame
 */
[[nodiscard]] constexpr uint8_t lpduAddressTypeIndex(const uint8_t* telegram)
{
    if (isExtendedFrame(telegram))
    {
        return 1;
    }
    return 5;
}

/**
 * @brief Address type flag (AT).
 * @param telegram Pointer to the telegram buffer
 * @return True if the destination is a group address, false for a physical address
 */
[[nodiscard]] constexpr bool lpduIsGroupAddress(const uint8_t* telegram)
{
    return (telegram[lpduAddressTypeIndex(telegram)] & 0x80) != 0;
}

/**
 * @brief Hop count.
 * @param telegram Pointer to the telegram buffer
 * @return The hop count 0..7
 */
[[nodiscard]] constexpr uint8_t lpduHopCount(const uint8_t* telegram)
{
    return telegram[lpduAddressTypeIndex(telegram)] >> 4 & 0x07;
}

/**
 * @brief Extended frame format (EFF) of an extended frame, CTRLE bits 0..3.
 * @param telegram Pointer to the telegram buffer
 * @return The extended frame format of an extended frame, 0 for a standard frame
 */
[[nodiscard]] constexpr uint8_t lpduExtendedFrameFormat(const uint8_t* telegram)
{
    if (isExtendedFrame(telegram))
    {
        return telegram[1] & 0x0f;
    }
    return 0;
}

/**
 * Return the area number of a given physical KNX address
 * @param address Individual KNX address
 * @return The area number of the individual address
 */
uint8_t physAddressToArea(uint16_t address);

/**
 * Return the line number of a given physical KNX address
 * @param address Individual KNX address
 * @return The line number of the individual address
 */
uint8_t physAddressToLine(uint16_t address);

/**
 * Return the device number of a given physical KNX address
 * @param address Individual KNX address
 * @return The device number of the individual address
 */
uint8_t physAddressToDevice(uint16_t address);

/**
 * Return the main group of a given KNX group address
 * @param address Group KNX address
 * @return The main group of the group address
 */ 
uint8_t mainGroupAddress(uint16_t address);

/**
 * Return the middle group of a given KNX group address
 * @param address Group KNX address
 * @return The middle group of the group address
 */
uint8_t middleGroupAddress(uint16_t address);

/**
 * Return the low group of a given KNX group address
 * @param address Group KNX address
 * @return The low group of the group address
 */
uint8_t lowGroupAddress(uint16_t address);

#endif /* SBLIB_KNX_LPDU_H_ */
/** @}*/
