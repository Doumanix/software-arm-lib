/*
 *  interrupt.h - Functions and classes for interrupt handling
 *
 *  Copyright (c) 2014 Stefan Taferner <stefan.taferner@gmx.at>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#ifndef SBLIB_INTERRUPT_H_
#define SBLIB_INTERRUPT_H_

#include <sblib/platform.h>
#include <sblib/utils.h>

/**
 * Interrupt handlers have fixed names. You need to give your interrupt handler the
 * correct name, then it is used automatically when it's interrupt is enabled. The
 * interrupt handler must be declared as extern "C":
 *
 * extern "C" void TIMER16_1_IRQHandler() { ...handle the interrupt.... }
 *
 * Use the following function names when writing an interrupt handler:
 *
 * ADC_IRQHandler()
 * BOD_IRQHandler()
 * CAN_IRQHandler()
 * I2C_IRQHandler()
 * PIOINT0_IRQHandler()
 * PIOINT1_IRQHandler()
 * PIOINT2_IRQHandler()
 * PIOINT3_IRQHandler()
 * SSP0_IRQHandler()
 * SSP1_IRQHandler()
 * TIMER16_0_IRQHandler()
 * TIMER16_1_IRQHandler()
 * TIMER32_0_IRQHandler()
 * TIMER32_1_IRQHandler()
 * UART_IRQHandler()
 * WAKEUP_IRQHandler()
 * WDT_IRQHandler()
 */

/**
 * @brief Interrupt priorities
 * @details 0 is highest priority, 3 is lowest priority
 */
enum class InterruptPriority : uint8_t
{
    highest = 0,
    high = 1,
    medium = 2,
    low = 3
};

/**
 * Disable all interrupts.
 */
void noInterrupts();

/**
 * Re-enable all interrupts after they have been disabled with noInterrupts().
 */
void interrupts();

/**
 * Wait for an interrupt. Puts the processor to sleep until an interrupt occurs.
 */
void waitForInterrupt();

/**
 * Wait for an enabled interrupt with PRIMASK set, preserving the mask on return.
 * Call from privileged Thread mode with no other priority mask blocking the wake IRQ.
 * The caller must re-enable interrupts to dispatch the handler and recheck its condition.
 * WFI may also return without an interrupt. This does not configure peripheral sleep clocks.
 */
void waitForInterruptMasked();

/**
 * Enable an interrupt.
 *
 * @param interruptType - the interrupt to enable: TIMER_16_0_IRQn, I2C_IRQn, ...
 */
void enableInterrupt(IRQn_Type interruptType);

/**
 * Disable an interrupt.
 *
 * @param interruptType - the interrupt to enable: TIMER_16_0_IRQn, I2C_IRQn, ...
 */
void disableInterrupt(IRQn_Type interruptType);

/**
 * Clear the pending status of an interrupt.
 *
 * @param interruptType - the interrupt to clear: TIMER_16_0_IRQn, I2C_IRQn, ...
 */
void clearPendingInterrupt(IRQn_Type interruptType);

/**
 * Set the pending status of an interrupt.
 *
 * @param interruptType - the interrupt to set: TIMER_16_0_IRQn, I2C_IRQn, ...
 */
void setPendingInterrupt(IRQn_Type interruptType);

/**
 * @fn bool isInsideInterrupt()
 * @brief Returns if within an Isr
 *
 * @return true if called inside an Isr otherwise false
 */
bool isInsideInterrupt();

/**
 * @fn bool getInterruptEnabled(IRQn_Type)
 * @brief Returns the enabled status of an interrupt
 *        doesn't work for NonMaskableInt_IRQn,
 *                        HardFault_IRQn,
 *                        SVCall_IRQn,
 *                        PendSV_IRQn,
 *                        SysTick_IRQn
 *
 * @param interruptType - the interrupt to get enabled status must be >=0
 * @return true if interrupt is enabled, otherwise false
 */
bool getInterruptEnabled(IRQn_Type interruptType);

/**
 * @brief Set the priority of an interrupt.
 *
 * @param interruptType  The interrupt to set the priority for: TIMER_16_0_IRQn, I2C_IRQn, ...
 * @param newPriority    The new priority to set for the interrupt.
 */
void setInterruptPriority(IRQn_Type interruptType, InterruptPriority newPriority);

/**
 * This define creates an interrupt handler that calls a callback function.
 *
 * @param handler - the name of the interrupt handler, e.g. TIMER16_0_IRQHandler
 * @param callback - the function to call in the interrupt handler
 */
#define CREATE_INTERRUPT_HANDLER(handler, callback) \
extern "C" void handler() { callback; }


//
// Inline functions
//

#if defined(SBLIB_PLATFORM_LPC11XX)

FORCE_INLINE void noInterrupts()
{
    // data synchronization barrier and instruction synchronization barrier to ensure that no interrupt occurs after we disabled them
    __DSB();
    __ISB();
    __disable_irq();
}

FORCE_INLINE void interrupts()
{
    __enable_irq();
}

FORCE_INLINE void waitForInterrupt()
{
    __WFI();
}

FORCE_INLINE void waitForInterruptMasked()
{
    __DSB();
    __WFI();
}

FORCE_INLINE void enableInterrupt(const IRQn_Type interruptType)
{
    NVIC->ISER[0] = 1 << (interruptType & 0x1f);
}

FORCE_INLINE void disableInterrupt(const IRQn_Type interruptType)
{
    // data synchronization barrier and instruction synchronization barrier to ensure that no interrupt occurs after we disabled them
    __DSB();
    __ISB();
    NVIC->ICER[0] = 1 << (interruptType & 0x1f);
}

FORCE_INLINE void clearPendingInterrupt(const IRQn_Type interruptType)
{
    NVIC->ICPR[0] = 1 << (interruptType & 0x1f);
}

FORCE_INLINE void setPendingInterrupt(const IRQn_Type interruptType)
{
    NVIC->ISPR[0] = 1 << (interruptType & 0x1f);
}

FORCE_INLINE bool isInsideInterrupt()
{
    return (SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0;
}

FORCE_INLINE bool getInterruptEnabled(const IRQn_Type interruptType)
{
    if (interruptType >= 0)
    {
        return (NVIC->ISER[0] & (uint32_t{1} << (interruptType & 0x1f))) != 0;
    }

    fatalError();
    return false; // Just here to make the linter happy
}

FORCE_INLINE void setInterruptPriority(const IRQn_Type interruptType, const InterruptPriority newPriority)
{
    NVIC_SetPriority(interruptType, static_cast<uint32_t>(newPriority));
}

#elif defined(SBLIB_PLATFORM_RP2350)

// Only the Pico SDK API is used (hardware_sync, hardware_irq). The RP2350 has more than
// 32 interrupts, and the SDK functions keep the SDK's view of the interrupt state consistent.

FORCE_INLINE void noInterrupts()
{
    // data synchronization barrier and instruction synchronization barrier to ensure that no interrupt occurs
    // after we disabled them
    __dsb();
    __isb();
    disable_interrupts();
}

FORCE_INLINE void interrupts()
{
    enable_interrupts();
}

FORCE_INLINE void waitForInterrupt()
{
    // No sleep on the RP2350, a NOP instead of WFI. Polling loops use the pattern
    // "interrupts(); waitForInterrupt();": an interrupt between both calls is handled before
    // the WFI, which then sleeps until the next interrupt. On the LPC11xx the SysTick wakes
    // the core after at most 1 ms; the RP2350 port has no periodic interrupt, so a WFI would
    // sleep until the next bus edge (main loop stalled after an ETS download).
    // Bus::pause() uses waitForInterruptMasked() instead, which has no such window.
    __nop();
}

FORCE_INLINE void waitForInterruptMasked()
{
    __dsb();
    __wfi();
}

FORCE_INLINE bool isInsideInterrupt()
{
    return __get_current_exception() != 0;
}

FORCE_INLINE void enableInterrupt(const IRQn_Type interruptType)
{
    irq_set_enabled(interruptType, true);
}

FORCE_INLINE void disableInterrupt(const IRQn_Type interruptType)
{
    // data synchronization barrier and instruction synchronization barrier to ensure that no interrupt occurs
    // after we disabled them
    __dsb();
    __isb();
    irq_set_enabled(interruptType, false);
}

FORCE_INLINE void clearPendingInterrupt(const IRQn_Type interruptType)
{
    irq_clear(interruptType);
}

FORCE_INLINE void setPendingInterrupt(const IRQn_Type interruptType)
{
    irq_set_pending(interruptType);
}

FORCE_INLINE bool getInterruptEnabled(const IRQn_Type interruptType)
{
    if (interruptType >= 0)
    {
        return irq_is_enabled(interruptType);
    }

    fatalError();
    return false; // Just here to make the linter happy
}

/**
 * The Cortex-M33 of the RP2350 has 16 priority levels (4 bits, upper nibble of the 8 bit
 * hardware priority). The four @ref InterruptPriority levels are spread over the full range:
 * highest = 0x00, high = 0x40, medium = 0x80 (= PICO_DEFAULT_IRQ_PRIORITY), low = 0xc0.
 */
FORCE_INLINE void setInterruptPriority(const IRQn_Type interruptType, const InterruptPriority newPriority)
{
    irq_set_priority(interruptType, static_cast<uint8_t>(static_cast<uint8_t>(newPriority) << 6));
}

#endif

#endif /* SBLIB_INTERRUPT_H_ */
