#ifndef SBLIB_KNX_USEREEPROM_BCU2_H_
#define SBLIB_KNX_USEREEPROM_BCU2_H_

#include "sblib/eib/userEepromBCU1.h"
#include "sblib/eib/serial_number.h"

/**
 * The BCU 2 user EEPROM
 * @details Can be accessed by name, like userEeprom.manuDataH() and as an array, like
 *          userEeprom[addr]. Please note that the @ref startAddress is subtracted.
 *          E.g. userEeprom[0x107] is the correct address for userEeprom.version() not userEeprom[0x07].
 *
 * @note KNX Spec. 3.0 9/4/1 Basic and System Components 5.1.2.12 Memory Map p.43ff
 *       - EEPROM: 0x100 - 0x4df (992 bytes)
 *       - single chip protected EEPROM: 0x4e0 - 0x4ff (32 bytes)
 *       - total: 0x100 - 0x4ff (1024 bytes)
 * @note KNX Spec. 3.0 06 Profiles 4.2.10 User EEPROM p. 41
 *       - User EEPROM: 0x119 - 0x46f (855 bytes)
 */
class UserEepromBCU2 : public UserEepromBCU1
{
public:
    /// The total BCU2 EEPROM range is 0x100-0x4ff (System EEPROM, single chip protected EEPROM, User EEPROM combined).
    /// UserEepromBCU2 is implemented with this total BCU2 EEPROM range, which is fine,
    /// but exposes BCU2 system EEPROM to the user, which is not great.

    explicit UserEepromBCU2() : UserEepromBCU2(0x100, 1024, 1024) {}

    [[nodiscard]] virtual uint32_t serialSize() const { return KNX_SERIAL_NUMBER_LENGTH; };
    [[nodiscard]] virtual uint32_t orderSize() const { return 10; };
    [[nodiscard]] virtual uint32_t orderInfoSize() const { return 10; };

    [[nodiscard]] virtual uint8_t& appType() const { return directAccess_8(appTypeOffset); }
    [[nodiscard]] virtual uint8_t* loadState() const { return &directAccess_8(loadStateOffset); }
    [[nodiscard]] virtual uint16_t& commsTabAddr() const { return directAccess_16(commsTabAddrOffset); }
    [[nodiscard]] virtual uint16_t& commsSeg0Addr() const { return directAccess_16(commsSeg0AddrOffset); }
    [[nodiscard]] virtual uint16_t& commsSeg1Addr() const { return directAccess_16(commsSeg1AddrOffset); }
    [[nodiscard]] virtual uint16_t& eibObjAddr() const { return directAccess_16(eibObjAddrOffset); }
    [[nodiscard]] virtual uint8_t& eibObjCount() const { return directAccess_8(eibObjCountOffset); }
    [[nodiscard]] virtual uint16_t& addrTabAddr() const { return directAccess_16(addrTabAddrOffset); }
    [[nodiscard]] virtual uint16_t& assocTabAddr() const { return directAccess_16(assocTabAddrOffset); }
    [[nodiscard]] virtual uint8_t* serial() const { return &directAccess_8(serialOffset); }
    [[nodiscard]] virtual uint8_t* order() const { return &directAccess_8(orderOffset); }

    [[nodiscard]] virtual uint8_t& padding1() const { return directAccess_8(padding1Offset); }
    [[nodiscard]] virtual uint8_t& serviceControl() const { return directAccess_8(serviceControlOffset); }
    [[nodiscard]] virtual uint8_t& padding2() const { return directAccess_8(padding2Offset); }
    [[nodiscard]] virtual uint8_t* orderInfo() const { return &directAccess_8(orderInfoOffset); }

    /// Setters for the fields that are written by the load state machine, they write through the Memory API.
    /// The 16 bit fields keep the byte order of the getters (low byte first), so the content is unchanged.
    bool setCommsTabAddr(const uint16_t value) { return setShadow16(commsTabAddrOffset, value); }
    bool setCommsSeg0Addr(const uint16_t value) { return setShadow16(commsSeg0AddrOffset, value); }
    bool setCommsSeg1Addr(const uint16_t value) { return setShadow16(commsSeg1AddrOffset, value); }
    bool setEibObjAddr(const uint16_t value) { return setShadow16(eibObjAddrOffset, value); }
    bool setEibObjCount(const uint8_t value) { return setUInt8(startAddr() + eibObjCountOffset, value); }
    bool setAddrTabAddr(const uint16_t value) { return setShadow16(addrTabAddrOffset, value); }
    bool setAssocTabAddr(const uint16_t value) { return setShadow16(assocTabAddrOffset, value); }
    bool setLoadState(const uint32_t objectIdx, const uint8_t value)
    {
        return setUInt8(startAddr() + loadStateOffset + objectIdx, value);
    }

protected:
    // BCU2 has no variable EEPROM start or size, so make this constructor protected
    UserEepromBCU2(const uint32_t start, const uint32_t size, const uint32_t flashSize)
       :
        UserEepromBCU1(start, size, flashSize) {}

private:
    bool setShadow16(const uint32_t offset, const uint16_t value)
    {
        const uint8_t bytes[2] = {static_cast<uint8_t>(value & 0xff), static_cast<uint8_t>(value >> 8)};
        return write(startAddr() + offset, bytes, 2);
    }

    ///\todo refactor PropertiesBCU2, so that it doesn't need to access private members of UserEepromBCU2,
    ///      then remove this friend declaration
    friend class PropertiesBCU2;

    static constexpr uint32_t manufacturerHOffset = 0x03; //!< 0x0103: Software manufacturer high byte
    static constexpr uint32_t portADDROffset = 0x0c; //!< 0x010c: Port A DDR settings

    static constexpr uint32_t appTypeOffset = 0x015; //!< 0x0115: \todo Application program type: 0=BCU2, else BCU1

    static constexpr uint32_t loadStateOffset = 0x370;      //!< 0x0470: Load state of the system interface objects
    static constexpr uint32_t addrTabAddrOffset = 0x378;    //!< 0x0478: Address of the address table
    static constexpr uint32_t assocTabAddrOffset = 0x37A;   //!< 0x047a: Address of the association table
    static constexpr uint32_t commsTabAddrOffset = 0x37C;   //!< 0x047c: Address of the communication object table
    static constexpr uint32_t commsSeg0AddrOffset = 0x37E;  //!< 0x047e: Address of communication object memory segment 0 // TODO needs implementation, see handleTaskCtrl2(...) in properties.cpp
    static constexpr uint32_t commsSeg1AddrOffset = 0x380;  //!< 0x0480: Address of communication object memory segment 1 // TODO needs implementation, see handleTaskCtrl2(...) in properties.cpp
    static constexpr uint32_t eibObjAddrOffset = 0x382;     //!< 0x0482: Address of the application program EIB objects, 0 if unused. // TODO needs implementation, see handleTaskCtrl1(...) in properties.cpp
    static constexpr uint32_t eibObjCountOffset = 0x384;    //!< 0x0484: Number of application program EIB objects. // TODO needs implementation, see handleTaskCtrl1(...) in properties.cpp
    static constexpr uint32_t padding1Offset = 0x385;       //!< 0x0485: Padding 1
    static constexpr uint32_t serviceControlOffset = 0x386; //!< 0x0486: Service control
    static constexpr uint32_t padding2Offset = 0x388;       //!< 0x0488: Padding 2
    static constexpr uint32_t serialOffset = 0x38A;         //!< 0x048A-0x48f: Hardware serial number (4 byte aligned)

    static constexpr uint32_t orderOffset = 0x390;     //!< 0x0490-0x0499: Hardware Type
    static constexpr uint32_t orderInfoOffset = 0x39A; //!< 0x049A-0x04A3: Ordering information

    ///\todo It's here (and maybe invalid offsets) because PropertiesBCU2 needs them, which also needs to be refactored
    static constexpr uint32_t addrTabMcbOffset = 0x3A4;   //!< 0x04A4-0x04AB:
    static constexpr uint32_t assocTabMcbOffset = 0x3AC;  //!< 0x04AC-0x04B3:
    static constexpr uint32_t commsTabMcbOffset = 0x3B4;  //!< 0x04B4-0x04BB:
    static constexpr uint32_t eibObjMcbOffset = 0x3BC;    //!< 0x04BC-0x04C3:
    static constexpr uint32_t commsSeg0McbOffset = 0x3C3; //!< 0x04C4-0x04CB: ///\todo address missmatch?
    static constexpr uint32_t eibObjVerOffset = 0x3CC;    //!< 0x04CC-0x04D0: Application program 1 version
    static constexpr uint32_t commsSeg0VerOffset = 0x3D1; //!< 0x04D1-0x04D5: Application program 2 version
    // end todo

    // Ensure all uint16_t fields are 2-byte aligned within the EEPROM array.
    // The array index is (offset - startAddress); since startAddress=0x100 (even),
    // it is sufficient to check that each offset itself is 2-byte aligned.
    static_assert(addrTabAddrOffset   % 2 == 0, "addrTabAddrOffset must be 2-byte aligned");
    static_assert(assocTabAddrOffset  % 2 == 0, "assocTabAddrOffset must be 2-byte aligned");
    static_assert(commsTabAddrOffset  % 2 == 0, "commsTabAddrOffset must be 2-byte aligned");
    static_assert(commsSeg0AddrOffset % 2 == 0, "commsSeg0AddrOffset must be 2-byte aligned");
    static_assert(commsSeg1AddrOffset % 2 == 0, "commsSeg1AddrOffset must be 2-byte aligned");
    static_assert(eibObjAddrOffset    % 2 == 0, "eibObjAddrOffset must be 2-byte aligned");
};

#endif /* SBLIB_KNX_USEREEPROM_BCU2_H_ */
