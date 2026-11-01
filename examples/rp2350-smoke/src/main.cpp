/*
 * rp2350-smoke - minimal RP2350 build and board check for the sblib RP2350 port.
 *
 * Blinks PIN_RUN of the selected board (Pico 2 test board: on-board LED GPIO25) and
 * prints a counter, millis() and the PROG button state on the default UART (configured
 * in CMakeLists.txt: UART1, TX GPIO8, RX GPIO9, 115200 8N1). Uses the sblib board header,
 * digital pin, interrupt, time and IAP functions. Holding the PROG button at start runs
 * a flash erase/program test on the first sector of the persistent storage.
 * The emulated timer16_0 runs with a match interrupt every 1000 us (match RESET), the
 * printed timer interrupt count should grow by about 500 per line. The KNX bus pins are
 * not used.
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include <cstdio>
#include "pico/stdlib.h"

#include <cstring>
#include <sblib/digital_pin.h>
#include <sblib/internal/iap.h>
#include <sblib/interrupt.h>
#include <sblib/io_pin_names.h>
#include <sblib/timer.h>

static_assert(digitalPinToGpio(GPIO47) == 47, "GPIO encoding");
static_assert(digitalPinToPort(GPIO40) == PIO1 && digitalPinToPinNum(GPIO40) == 8, "GPIO bank encoding");
static_assert(digitalPinToIRQn(GPIO27) == IO_IRQ_BANK0, "GPIO interrupt");

#if defined(SBLIB_BOARD_RP2350_PICO2_TESTBOARD)
static_assert(PIN_EIB_TX == GPIO27 && PIN_EIB_RX == GPIO28 && PIN_PROG == GPIO22, "Pico 2 test board KNX pins");
static_assert(PIN_TX == PICO_DEFAULT_UART_TX_PIN && PIN_RX == PICO_DEFAULT_UART_RX_PIN, "debug UART pins");
static_assert(PIN_RUN == PICO_DEFAULT_LED_PIN, "run LED is the Pico 2 LED");
#endif

extern "C" uint8_t __sblib_storage_start[];
extern "C" uint8_t __sblib_storage_end[];

/**
 * Erase and program the first sector of the persistent storage and check the IAP guards.
 * Only runs if the PROG button is pressed at start, as it destroys data in that sector.
 * The last sector (UserEeprom) is not touched.
 */
static void flashTest()
{
    uint8_t* sector = __sblib_storage_start;
    if (__sblib_storage_end - sector < 2 * FLASH_SECTOR_SIZE)
    {
        std::printf("flash test skipped: storage too small\n");
        return;
    }

    alignas(4) static uint8_t page[FLASH_PAGE_SIZE];
    for (unsigned int i = 0; i < sizeof(page); ++i)
    {
        page[i] = static_cast<uint8_t>(i ^ 0x5a);
    }

    const unsigned int sectorNum = iapSectorOfAddress(sector);
    const unsigned int pageNum = iapPageOfAddress(sector);
    std::printf("flash test sector %u at %p\n", sectorNum, sector);
    std::printf("  erase sector      rc=%d (0 expected)\n", iapEraseSector(sectorNum));
    std::printf("  program page 0+1  rc=%d %d (0 expected)\n", iapProgram(sector, page, sizeof(page)),
                iapProgram(sector + FLASH_PAGE_SIZE, page, sizeof(page)));
    std::printf("  erase page 0      rc=%d (0 expected)\n", iapErasePage(pageNum));
    std::printf("  page 0 erased=%d, page 1 kept=%d (1 1 expected)\n",
                sector[0] == 0xff && sector[FLASH_PAGE_SIZE - 1] == 0xff,
                memcmp(sector + FLASH_PAGE_SIZE, page, sizeof(page)) == 0);
    std::printf("  program firmware  rc=%d (%d expected)\n", iapProgram(FLASH_BASE_ADDRESS, page, sizeof(page)),
                IAP_DST_ADDR_NOT_MAPPED);
    std::printf("  erase sector 0    rc=%d (%d expected)\n", iapEraseSector(0), IAP_INVALID_SECTOR);
    std::printf("  erase sector      rc=%d (0 expected)\n", iapEraseSector(sectorNum));
}

static volatile unsigned int timerInterrupts = 0;

// Interrupt handler of the emulated timer16_0, called on match MAT1
extern "C" void TIMER16_0_IRQHandler()
{
    if (timer16_0.flag(TIMER_MATCH_MAT1))
    {
        timer16_0.resetFlag(TIMER_MATCH_MAT1);
        ++timerInterrupts;
    }
}

static void startTimerTest()
{
    timer16_0.setIRQPriority(InterruptPriority::low);
    timer16_0.begin();
    timer16_0.matchMode(TIMER_MATCH_MAT1, INTERRUPT | RESET);
    timer16_0.match(TIMER_MATCH_MAT1, 1000 - 1);
    timer16_0.start();
    timer16_0.resetFlags();
    timer16_0.interrupts();
}

int main()
{
    stdio_init_all();

    pinMode(PIN_RUN, OUTPUT);
    pinMode(PIN_PROG, INPUT | PULL_UP);
    pinMode(PIN_VBUS, INPUT_ANALOG); // GPIO_NC on the test board, must be ignored

    unsigned int partId = 0;
    uint8_t uid[IAP_UID_LENGTH];
    iapReadPartID(&partId);
    iapReadUID(uid);
    std::printf("rp2350-smoke: flash %u KiB, storage %p..%p, chip id 0x%08x, uid %02x%02x%02x%02x%02x%02x%02x%02x\n",
                iapFlashSize() / 1024, __sblib_storage_start, __sblib_storage_end, partId, uid[0], uid[1], uid[2],
                uid[3], uid[4], uid[5], uid[6], uid[7]);

    delay(10); // pull-up settles
    if (!digitalRead(PIN_PROG))
    {
        flashTest();
    }

    startTimerTest();

    unsigned int counter = 0;
    while (true)
    {
        digitalWrite(PIN_RUN, counter & 1);
        digitalWrite(PIN_VBUS, true); // GPIO_NC on the test board, must be ignored

        noInterrupts();
        const bool inIsr = isInsideInterrupt();
        interrupts();

        std::printf("rp2350-smoke %u millis=%u prog=%d isr=%d timer16_0 irq=%u value=%u\n", counter++, millis(),
                    digitalRead(PIN_PROG), inIsr, timerInterrupts, timer16_0.value());
        delay(500);
    }
}
