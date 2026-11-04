/*
 *  bcu_base.h - Minimum stuff for a BCU
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#ifndef SBLIB_KNX_BCUBASE_H_
#define SBLIB_KNX_BCUBASE_H_


#include "sblib/eib/com_objects.h"
#include "sblib/timeout.h"
#include "sblib/debounce.h"
#include "sblib/eib/knx_tlayer4.h"
#include "sblib/eib/bus.h"
#include "sblib/eib/userRam.h"
#include "sblib/internal/bootloader_commands.h"


class CallbackBcu;

/**
 * Class for controlling minimum BCU related things.
 */
class BcuBase : public TLayer4
{
public:
    BcuBase(UserRam* userRam, AddrTables* addrTables);
    BcuBase() = delete;

    Bus* bus;

    /**
     * Sets the own physical KNX address of the BCU.
     *
     * @details It also invokes the methods of @ref Bus and @ref TLayer4 with the provided address.
     *
     * @param addr The physical KNX address to be set as the bcu´s own address.
     */
    void setOwnAddress(uint16_t addr) override;

    /**
     * Set ProgPin of board, must be called before begin method
     * @param prgPin Pin definition
     */
    void setProgPin(int prgPin);

    /**
     * End using the EIB bus coupling unit.
     */
    virtual void end();

    /**
     * Test if the programming mode is active. This is also indicated
     * by the programming mode LED.
     *
     * @return True if the programming mode is active, false if not.
     */
    bool programmingMode();

    /**
     * Test if the user application is active. The application is active if the
     * application layer is active in userRam.status, the programming mode is not
     * active, and the run error in userEeprom.runError is 0xff (no error).
     *
     * @return True if the user application is active, false if not.
     */
    virtual bool applicationRunning() const = 0;

    /**
     * The BCU's main processing loop. This is like the application's loop() function,
     * and is called automatically by main() when the BCU is activated with bcu.begin().
     */
    void loop() override;

    /**
     *
     * The pin where the programming LED + button are connected. The default pin
     * is PIO2_0. This variable may be changed in setup(), if required. If set
     * to 0, the programming LED + button are not handled by the library.
     */
    int progPin;

    /**
      * @brief Performs a system reset by calling @ref NVIC_SystemReset
      * @warning This function will never return.
      */
    virtual void softSystemReset();

    virtual uint8_t& layerStatus() = 0;

    UserRam* userRam;
    AddrTables* addrTables;
    ComObjects* comObjects;

protected:
    /**
     * Special initialization for the BCU
     */
    void _begin() override;

    /**
     * @brief Set or unset the programming mode of the bcu
     *
     * @param  newMode programming button state
     * @return true if successful, otherwise false
     */
    bool setProgrammingMode(bool newMode);

    /**
     * Processes @ref APCI_BASIC_RESTART_PDU telegrams.
     * For all other telegrams @ref TLayer4::processApci is invoked.
     *
     * @param apciCmd       @ref ApciCommand of the telegram
     * @param telegram      The APCI-telegram
     * @param telLength     Telegram length
     * @param sendBuffer    Pointer to the buffer for a potential response telegram
     * @return True if a response telegram was prepared, otherwise false
     */
    bool processApci(ApciCommand apciCmd, unsigned char* telegram, TelegramLength telLength,
                     uint8_t* sendBuffer) override;

    /**
     * Handles individual address broadcast telegrams.
     * Processes KNX telegrams related to individual physical address operations sent to the broadcast address.
     *
     * @param apciCmd   The APCI command to process (e.g., @ref APCI_INDIVIDUAL_ADDRESS_WRITE_PDU, @ref APCI_INDIVIDUAL_ADDRESS_READ_PDU)
     * @param telegram  Pointer to the received telegram buffer containing the complete KNX frame
     * @return True if the APCI command was handled, false if the command is not supported.
     */
    [[nodiscard]] bool handleIndividualAddressBroadcast(ApciCommand apciCmd, const uint8_t* telegram);

    void sendApciIndividualAddressReadResponse();

    Debouncer progButtonDebouncer; //!< The debouncer for the programming mode button.

    void discardReceivedTelegram() override;
    void send(unsigned char* telegram, unsigned short length) override;

    void scheduleRestart(RestartType type);

private:
    RestartType restartType;
    bool restartSendDisconnect;
    Timeout restartTimeout;
};

#endif /* SBLIB_KNX_BCUBASE_H_ */
