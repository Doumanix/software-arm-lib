/*
 *  timer.h - Timer manipulation and time functions.
 *
 *  Copyright (c) 2014 Stefan Taferner <stefan.taferner@gmx.at>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#ifndef SBLIB_TIMER_H_
#define SBLIB_TIMER_H_

#include <sblib/interrupt.h>
#include <sblib/platform.h>
#include <sblib/types.h>
#include <climits>

class Timer;


/**
 * Delay the program execution by sleeping a number of milliseconds.
 * SysTick Interrupt must be enabled.
 *
 * @param msec - the number of milliseconds to sleep.
 */
void delay(unsigned int msec);

/**
 * The number of minimal Microseconds possible for delayMicroseconds().
 */
#define MIN_DELAY_MICROSECONDS 2

/**
 * The number of maximal Microseconds possible for delay by delayMicroseconds().
 */
#define MAX_DELAY_MICROSECONDS (4000)

/**
 * The number of maximal Milliseconds possible for delay by delayMicroseconds().
 */
#define MAX_DELAY_MILLISECONDS (MAX_DELAY_MICROSECONDS / 1000)

/**
 * Delay the program execution a number of microseconds. You should use delay() when
 * possible, as this function does a busy wait which consumes more power than delay().
 * The minimum delay is MIN_DELAY_MICROSECONDS ~3μs. Everything below will result in a ~3μs delay.
 * The maximum delay is MAX_DELAY_MICROSECONDS 4ms. Everything above will result in a fatalError().
 *
 * Do not expect high accuracy, as the function can be interrupted at any time.
 * The function is optimized for a SystemCoreClock of 48MHz
 * In case you need higher precision for 0 < usec < 10, try to use
 * EXPERIMENTAL macro DELAY_USEC_HIGH_PRECISION(usec) instead.
 *
 * @param[in] usec - the number of microseconds to wait.
 */
void delayMicroseconds(unsigned int usec);

#ifdef IAP_EMULATION
/**
 * Set the number of milliseconds that elapsed since the last reset or processor start.
 *
 * @param newSystemTime The number of milliseconds.
 */
void setMillis(unsigned int newSystemTime);
#endif

/**
 * Get the number of milliseconds that elapsed since the last reset or processor start.
 * Please note that the system time overflows and restarts at zero after 49,7 days.
 *
 * @return The number of milliseconds.
 */
unsigned int millis();

/**
 * Get the number of milliseconds that elapsed since the reference time.
 *
 * @param ref - the reference time to compare with
 * @return The number of milliseconds since time.
 */
unsigned int elapsed(unsigned int ref);

/**
 * The number of CPU clock cycles per microsecond.
 */
#if defined(SBLIB_PLATFORM_RP2350)
// System clock as configured at compile time (Pico SDK SYS_CLK_HZ, default 150 MHz)
#   define clockCyclesPerMicrosecond() (SYS_CLK_HZ / 1000000)
#else
#   define clockCyclesPerMicrosecond() (SystemCoreClock / 1000000)
#endif

/**
 * Convert the clock cycles cyc to microseconds.
 *
 * @param cyc - the clock cycles to convert.
 * @return The microseconds it takes to get cyc clock cycles.
 */
#define clockCyclesToMicroseconds(cyc) ((cyc) / clockCyclesPerMicrosecond())

/**
 * Convert the microseconds msec to clock cycles.
 *
 * @param msec - the microseconds to convert.
 * @return The clock cycles it takes to get msec microseconds.
 */
#define microsecondsToClockCycles(msec) ((msec) * clockCyclesPerMicrosecond())

/**
 * @def CPU frequency in Hz
 */
#ifndef F_CPU
#   if defined(SBLIB_PLATFORM_RP2350)
#       define F_CPU (SYS_CLK_HZ)
#   else
#       define F_CPU (48000000UL)
#   endif
#endif

#if defined(SBLIB_PLATFORM_LPC11XX)
/**
 * @def DELAY_USEC_HIGH_PRECISION macro for higher precision microsecond delay
 *      0 < usec < 10
 *      works only with fixed F_CPU (default 48MHz)
 *      !! THIS FEATURE IS EXPERIMENTAL !!
 *      from: https://www.mikrocontroller.net/topic/378584#4302525
 */
#define DELAY_USEC_HIGH_PRECISION(usec) delay_cycles(static_cast<float>(F_CPU) * (usec) / 1E6f / 6.f+1)
#endif /* SBLIB_PLATFORM_LPC11XX */

#if defined(SBLIB_PLATFORM_LPC11XX)

/**
 * The 16bit timer #0.
 */
extern Timer timer16_0;

/**
 * The 16bit timer #1.
 */
extern Timer timer16_1;

/**
 * The 32bit timer #0.
 */
extern Timer timer32_0;

/**
 * The 32bit timer #1.
 */
extern Timer timer32_1;


/**
 * A timer.
 *
 * The timer is free running, with either 16 bit or 32 bit resolution. Up to 4 match channels
 * can control the timer or change digital pins of the processor. The timer is incremented with
 * every cycle of the peripheral clock. The prescaler can be used to divide the peripheral
 * clock before it is applied to the timer.
 *
 * You need to powerOn() a timer before it can be used.
 *
 * Use the predefined timers (timer16_0, timer16_1, timer32_0, timer32_1) instead of this class.
 */
class Timer
{
public:
    /**
     * Create a timer object for the specified timer.
     *
     * @param aTimerNum - the number of the timer: TIMER16_0, TIMER32_1, ...
     */
    explicit Timer(uint8_t aTimerNum);

    /**
     * Begin using the timer.
     *
     * This powers the timer on and the system clock is connected to the timer.
     * This method must be called before any other method of the timer can be used.
     */
    void begin() const;

    /**
     * End using the timer.
     *
     * This removes the system clock from the timer. The timer becomes unusable and
     * cannot be accessed with any other timer method until begin() is called again.
     */
    void end() const;

    /**
     * Set the prescaler. The prescaler can be used to divide the peripheral clock before it is
     * applied to the timer. The factor may be 16 bit for a 16 bit timer, and 32 bit for a
     * 32 bit timer. The prescaler causes the timer to increment on every peripheral clock when
     * factor = 0, every 2 peripheral clocks when factor = 1, etc.
     *
     * @param factor - the prescaler division factor.
     */
    void prescaler(unsigned int factor) const;

    /**
     * Get the prescaler division factor. The factor is 16 bit for a 16 bit timer, and 32 bit
     * for a 32 bit timer. The prescaler causes the timer to increment on every peripheral clock
     * when factor = 0, every 2 peripheral clocks when factor = 1, etc.
     */
    unsigned int prescaler() const;

    /**
     * Start the timer.
     */
    void start() const;

    /**
     * Stop the timer.
     */
    void stop() const;

    /**
     * Restart the timer. This is the same as calling reset() and start().
     * If the timer was not previously running, it is started.
     */
    void restart() const;

    /**
     * Rest the timer.
     */
    void reset() const;

    /**
     * Read the current value of the timer.
     *
     * @return The timer value.
     */
    unsigned int value() const;

    /**
     * Set the current value of the timer.
     *
     * @param val - The new timer value.
     */
    void value(unsigned int val) const;

    /**
     * Enable timer interrupts for this timer.
     * Use start() to start the timer.
     */
    void interrupts() const;

    /**
     * Disable timer interrupts for this timer.
     * Use stop() to stop the timer.
     */
    void noInterrupts() const;

    /**
     * Read the interrupt flags of the timer. This register contains the flags that caused an
     * interrupt for the timer.
     *
     * The bits are:
     *
     * 0 - interrupt flag for match channel TIMER_MATCH_MAT0
     * 1 - interrupt flag for match channel TIMER_MATCH_MAT1
     * 2 - interrupt flag for match channel TIMER_MATCH_MAT2
     * 3 - interrupt flag for match channel TIMER_MATCH_MAT3
     * 4 - interrupt flag for capture channel CAP0
     * 5 - interrupt flag for capture channel CAP1
     *
     * @see resetFlags()
     * @see resetFlag(TimerMatch)
     * @see resetFlag(TimerCapture)
     * @see flagSet(TimerMatch)
     * @see flagSet(TimerCapture)
     */
    int flags() const;

    /**
     * Reset the interrupt flags of the timer.
     */
    void resetFlags() const;

    /**
     * Reset interrupt flag of a match channel.
     *
     * @param match - the match channel: TIMER_MATCH_MAT0, TIMER_MATCH_MAT1, TIMER_MATCH_MAT2, TIMER_MATCH_MAT3.
     */
    void resetFlag(TimerMatch match) const;

    /**
     * Reset interrupt flag of a capture channel.
     *
     * @param capture - the capture channel: CAP0, CAP1.
     */
    void resetFlag(TimerCapture capture) const;

    /**
     * Test if the timer flag of a match channel is set.
     *
     * @param match - the match channel: TIMER_MATCH_MAT0, TIMER_MATCH_MAT1, TIMER_MATCH_MAT2, TIMER_MATCH_MAT3.
     * @return True if the flag is set, false if it is not set.
     */
    bool flag(TimerMatch match) const;

    /**
     * Test if the timer flag of a capture channel is set.
     *
     * @param capture - the capture channel: CAP0, CAP1.
     * @return True if the flag is set, false if it is not set.
     */
    bool flag(TimerCapture capture) const;

    /**
     * Get the timer flag mask for flags() for a match channel.
     *
     * @param match - the match channel: TIMER_MATCH_MAT0, TIMER_MATCH_MAT1, TIMER_MATCH_MAT2, TIMER_MATCH_MAT3.
     * @return The timer flag mask.
     */
    static int flagMask(TimerMatch match);

    /**
     * Get the timer flag mask for flags() for a capture channel.
     *
     * @param capture - the capture channel: CAP0, CAP1.
     * @return The timer flag mask.
     */
    static int flagMask(TimerCapture capture);

    /**
     * Configure a match channel.
     *
     * @param channel - the match channel to configure: TIMER_MATCH_MAT0, TIMER_MATCH_MAT1, TIMER_MATCH_MAT2, TIMER_MATCH_MAT3.
     * @param mode - the configuration. Multiple values are combined with "|".
     *
     * @brief
     * The match channels allow the following configuration options:
     *
     * DISABLE:      Disable matching for this match channel.
     * INTERRUPT:    Generate a timer interrupt when the contents of the match channel matches
     *               the value of the timer.
     * RESET:        Reset the timer on match.
     * STOP:         Stop the timer when the contents of the match channel matches the value
     *               of the timer.
     * CLEAR:        Clear the digital pin of the match channel to 0 on match.
     * SET:          Set the digital pin of the match channel to 1 on match.
     * TOGGLE:       Toggle the digital pin of the match channel.
     */
    void matchMode(int channel, int mode) const;

    /**
     * Get the configuration of a match channel.
     *
     * @param channel - the match channel to query: TIMER_MATCH_MAT0, TIMER_MATCH_MAT1, TIMER_MATCH_MAT2, TIMER_MATCH_MAT3.
     *
     * @return The configuration. See matchMode(short,short) above for the possible configuration values.
     */
    int matchMode(int channel) const;

    /**
     * Set the timer value for a match channel. Use matchMode() to configure what shall
     * happen when the timer matches. Use pinMode(short,short) to configure the digital pin as
     * match output pin.
     *
     * @param channel - the match channel: TIMER_MATCH_MAT0, TIMER_MATCH_MAT1, TIMER_MATCH_MAT2, TIMER_MATCH_MAT3.
     * @param value - the timer value when the match channel shall match. The value is 16 bit for
     *                16 bit timers and 32 bit for 32 bit timers.
     */
    void match(int channel, unsigned int value) const;

    /**
     * Get the timer value of a match channel.
     *
     * @param channel - the match channel to query: TIMER_MATCH_MAT0, TIMER_MATCH_MAT1, TIMER_MATCH_MAT2, TIMER_MATCH_MAT3.
     * @return The timer value when the match channel shall match. The value is 16 bit for 16 bit
     *         timers and 32 bit for 32 bit timers.
     */
    unsigned int match(int channel) const;

    /**
     * Configure a capture channel.
     *
     * @param channel - the channel to configure: CAP0, CAP1.
     * @param mode - the configuration. Multiple values are combined with "|".
     *
     * @brief
     * The capture channels allow the following configuration options:
     *
     * DISABLE:      Disable capturing,
     * INTERRUPT:    Generate a timer interrupt when the capture condition is met,
     * RISING_EDGE:  Capture on rising edge: a sequence of 0 then 1 on the capture pin will cause
     *               the capture channel to be loaded with the current timer value.
     * FALLING_EDGE: Capture on falling edge: a sequence of 1 then 0 on the capture pin will cause
     *               the capture channel to be loaded with the current timer value.
     */
    void captureMode(int channel, int mode) const;

    /**
     * Get the configuration of a capture channel.
     *
     * @param channel - the channel to query: CAP0, CAP1.
     *
     * @return The configuration. See mode(short,short) above for the possible configuration values.
     */
    int captureMode(int channel) const;

    /**
     * Get the timer value of a capture channel.
     *
     * @param channel - the channel to query: TIMER_MATCH_MAT0, TIMER_MATCH_MAT1, TIMER_MATCH_MAT2, TIMER_MATCH_MAT3
     *
     * @return The timer value when the capture occurred. The value is 16 bit for 16 bit timers
     *         and 32 bit for 32 bit timers.
     */
    unsigned int capture(int channel) const;

    /**
     * Enable a PWM channel. When enabled, the corresponding match channel (TIMER_MATCH_MAT0 for PWM0, ...)
     * will control the level of the digital pin of the match channel. Use pinMode(short,short)
     * to configure the digital pin as match output pin.
     *
     * @param channel - the PWM channel: PWM0, PWM1, PWM2, PWM3.
     *
     * When PWM output is enabled, the output of the match pin will be:
     * - as long as the timer value is below the match value, the output is low.
     * - when the timer value is equal or higher than the match value, the output is high.
     * - when the timer resets the output is low, except if the match value is 0.
     */
    void pwmEnable(int channel) const;

    /**
     * Disable a PWM channel.
     *
     * @param channel - the PWM channel: PWM0, PWM1, PWM2, PWM3.
     */
    void pwmDisable(int channel) const;

    /**
     * Configure the counter mode.
     *
     * @param mode - the counter mode configuration, see below.
     * @param clearMode - the counter clearing mode configuration, see below.
     *
     * The following flags can be used for mode and clearMode:
     * DISABLE: to disable counter mode (this is the default),
     * CAP0: use capture channel 0 for the counter input,
     * CAP1: use capture channel 1 for the counter input,
     * RISING_EDGE: the timer counter is incremented on every rising edge of the capture channel,
     * FALLING_EDGE: the timer counter is incremented on every falling edge of the capture channel.
     *
     * To activate the counter mode, it is mandatory to select RISING_EDGE, FALLING_EDGE, or both
     * for mode. Otherwise the result is undefined. The clearMode can be set to DISABLE.
     *
     * Use the clearing mode clearMode to reset the timer by a capture channel:
     * DISABLE: to disable clearing mode (this is the default),
     * CAP0: use capture channel 0 for clearing,
     * CAP1: use capture channel 1 for clearing,
     * RISING_EDGE: the rising edge of the selected capture channel clears the timer,
     * FALLING_EDGE: the falling edge of the selected capture channel clears the timer.
     * It is not possible to use both edges for clearing.
     *
     * Example:  timer16_0.counterMode(CAP0|RISING_EDGE, CAP1|FALLING_EDGE);
     */
    void counterMode(int mode, int clearMode) const;

    /**
     * Configure a match pin mode of the channel.
     *
     * @param channel - the match channel to configure: TIMER_MATCH_MAT0, TIMER_MATCH_MAT1, TIMER_MATCH_MAT2, TIMER_MATCH_MAT3.
     * @param mode - the configuration. Multiple values are combined with "|".
     *
     * @brief
     * The match channels allow the following configuration options:
     *
     * CLEAR:        Clear the digital pin of the match channel to 0 on match.
     * SET:          Set the digital pin of the match channel to 1 on match.
     * TOGGLE:       Toggle the digital pin of the match channel.
     */
    void matchModePinConfig(int channel, int mode) const;

    /**
     * Get the info if it is a 32bit timer
     *
     * @return true if the timer is a 32bit timer, otherwise 16bit timer
     */
    bool is32bitTimer(void) const;

    /**
     * Get the digital level of match channel (even if output pin is disabled)
     *
     * @param channel - the match channel to configure: TIMER_MATCH_MAT0, TIMER_MATCH_MAT1, TIMER_MATCH_MAT2, TIMER_MATCH_MAT3.
     *
     * @return - true = high, false = low
     */
    bool getMatchChannelLevel(int channel) const;

    /**
     * Set interrupt priority for the associated hardware timer
     *
     * @param newPriority - new interrupt priority for the timer
     */
    void setIRQPriority(InterruptPriority newPriority) const;

protected:
    LPC_TMR_TypeDef* timer;
    uint8_t timerNum;
};


//
// Inline functions
//


ALWAYS_INLINE void Timer::prescaler(const unsigned int factor) const
{
    timer->PR = factor;
}

ALWAYS_INLINE unsigned int Timer::prescaler() const
{
    return timer->PR;
}

ALWAYS_INLINE void Timer::start() const
{
    timer->TCR |= 1;
}

ALWAYS_INLINE void Timer::stop() const
{
    timer->TCR &= ~1;
}

ALWAYS_INLINE void Timer::end() const
{
    LPC_SYSCON->SYSAHBCLKCTRL &= ~(1 << (7 + timerNum));
}

ALWAYS_INLINE void Timer::restart() const
{
    timer->TCR = 2;
    timer->TCR = 1;
}

ALWAYS_INLINE void Timer::reset() const
{
    timer->TCR |= 2;
    timer->TCR &= ~2;
}

ALWAYS_INLINE unsigned int Timer::value() const
{
    return timer->TC;
}

ALWAYS_INLINE void Timer::value(const unsigned int val) const
{
    timer->TC = val;
}

ALWAYS_INLINE int Timer::flags() const
{
    return timer->IR;
}

ALWAYS_INLINE void Timer::resetFlags() const
{
    timer->IR = 0xff;
}

ALWAYS_INLINE void Timer::resetFlag(const TimerMatch match) const
{
    timer->IR = (1 << match);
}

ALWAYS_INLINE void Timer::resetFlag(const TimerCapture capture) const
{
    timer->IR = (16 << capture);
}

ALWAYS_INLINE bool Timer::flag(const TimerMatch match) const
{
    return timer->IR & (1 << match);
}

ALWAYS_INLINE bool Timer::flag(const TimerCapture capture) const
{
    return timer->IR & (16 << capture);
}

ALWAYS_INLINE int Timer::flagMask(const TimerMatch match)
{
    return 1 << match;
}

ALWAYS_INLINE int Timer::flagMask(const TimerCapture capture)
{
    return 16 << capture;
}

ALWAYS_INLINE void Timer::interrupts() const
{
    NVIC_EnableIRQ((IRQn_Type)(TIMER_16_0_IRQn + timerNum));
}

ALWAYS_INLINE void Timer::noInterrupts() const
{
    NVIC_DisableIRQ((IRQn_Type)(TIMER_16_0_IRQn + timerNum));
}


ALWAYS_INLINE unsigned int Timer::match(const int channel) const
{
    return (&timer->MR0)[channel];
}

ALWAYS_INLINE void Timer::match(const int channel, const unsigned int value) const
{
    (&timer->MR0)[channel] = value;
}

ALWAYS_INLINE unsigned int Timer::capture(const int channel) const
{
    return (&timer->CR0)[channel];
}

ALWAYS_INLINE void Timer::pwmEnable(const int channel) const
{
    timer->PWMC |= 1 << channel;
}

ALWAYS_INLINE void Timer::pwmDisable(const int channel) const
{
    timer->PWMC &= ~(1 << channel);
}

ALWAYS_INLINE void Timer::matchModePinConfig(const int channel, const int mode) const
{
    const int offset = channel << 1;
    timer->EMR = (timer->EMR
                  & ~(0x30 << offset))
                 | ((mode & 0x30) << offset);
}

ALWAYS_INLINE bool Timer::is32bitTimer(void) const
{
    return (bool)(this->timerNum > 1);
}

ALWAYS_INLINE bool Timer::getMatchChannelLevel(const int channel) const
{
    return (bool)(timer->EMR & (1 << channel));
}

#elif defined(SBLIB_PLATFORM_RP2350)

/**
 * The emulated 16bit timer #0.
 */
extern Timer timer16_0;

/**
 * The emulated 16bit timer #1 (KNX bus timer).
 */
extern Timer timer16_1;

/**
 * The emulated 32bit timer #0.
 */
extern Timer timer32_0;

/**
 * The emulated 32bit timer #1.
 */
extern Timer timer32_1;

/**
 * Emulation of the LPC11xx timers on the RP2350, see src/rp2350/timer.cpp.
 *
 * Same interface as the LPC11xx Timer class. The timer counts microseconds (the prescaler
 * is stored but not used, the KNX bus sets it for 1 MHz). Matches are scheduled with a
 * dedicated hardware alarm per timer, the interrupt handler TIMERxx_y_IRQHandler() of the
 * timer is called from the alarm interrupt.
 *
 * Pins as with the LPC11xx 16 bit timer #1 (CT16B1): capture CAP0 is PIN_EIB_RX (falling
 * edge, GPIO interrupt), the PWM output of match MAT0 is PIN_EIB_TX (HIGH from the MAT0
 * match until the MAT2 match). The other timers have no pins.
 */
class Timer
{
public:
    explicit Timer(uint8_t aTimerNum);

    void begin() const;
    void end() const;
    void prescaler(unsigned int factor) const;
    unsigned int prescaler() const;
    void start() const;
    void stop() const;
    void restart() const;
    void reset() const;
    unsigned int value() const;
    void value(unsigned int val) const;
    void interrupts() const;
    void noInterrupts() const;
    int flags() const;
    void resetFlags() const;
    void resetFlag(TimerMatch match) const;
    void resetFlag(TimerCapture capture) const;
    bool flag(TimerMatch match) const;
    bool flag(TimerCapture capture) const;
    static int flagMask(TimerMatch match);
    static int flagMask(TimerCapture capture);
    void matchMode(int channel, int mode) const;
    int matchMode(int channel) const;
    void match(int channel, unsigned int value) const;
    unsigned int match(int channel) const;
    void captureMode(int channel, int mode) const;
    int captureMode(int channel) const;
    unsigned int capture(int channel) const;
    void pwmEnable(int channel) const;
    void pwmDisable(int channel) const;
    void counterMode(int mode, int clearMode) const;
    void matchModePinConfig(int channel, int mode) const;
    bool is32bitTimer() const;
    bool getMatchChannelLevel(int channel) const;
    void setIRQPriority(InterruptPriority newPriority) const;

protected:
    uint8_t timerNum;
};

ALWAYS_INLINE int Timer::flagMask(const TimerMatch match)
{
    return 1 << match;
}

ALWAYS_INLINE int Timer::flagMask(const TimerCapture capture)
{
    return 16 << capture;
}

ALWAYS_INLINE bool Timer::is32bitTimer() const
{
    return timerNum >= TIMER32_0;
}

#if defined(SBLIB_RP2350_TIMER_STATS)
/**
 * Counters of the timer emulation since start, for diagnostics only
 * (CMake option SBLIB_RP2350_TIMER_STATS).
 */
struct TimerEmulationStats
{
    uint32_t alarmIrqs;             //!< hardware alarm interrupts of all timers
    uint32_t alarmIrqsWithoutMatch; //!< alarm interrupts in which no match was due
    uint32_t forcedIrqs;            //!< alarm interrupts forced because the time already passed
    uint32_t shortSchedules;        //!< alarms scheduled at most 2 us ahead
    uint32_t captureEvents;         //!< capture events (falling edges)
    uint32_t handlerCalls;          //!< calls of the TIMERxx_y_IRQHandler functions
    uint32_t lateTxEdges;           //!< bus PWM edges whose alarm came after the edge time
    uint32_t shortGapCaptures;      //!< capture edges closer than 8 us to the previous one (captured as well)
    uint32_t shortGapCapturesInTx;  //!< of these: during an own bus PWM pulse (TX high)
    uint32_t minShortGapUs;         //!< smallest distance of such an edge to the previous one, if any
    uint32_t shortGapUs[8];         //!< capture edges per distance 0..7 us to the previous one

    // TX edges of the bus PWM (system time). Delay classes: [0..6] 0..6 us after the exact edge time, [7] 7 us or more.
    // First pulse of a telegram: no TX pulse in the 1000 us before.
    uint32_t txRiseLateUs[8];        //!< rising TX edges (pulse start) per delay
    uint32_t txFallLateUs[8];        //!< falling TX edges (pulse end) per delay
    uint32_t txFirstFallLateUs[8];   //!< of these: end of the first pulse of a telegram
    uint32_t txWidthUs[8];           //!< TX pulses per width: [0] up to 33 us, [1..6] 34..39 us, [7] 40 us or more
    uint32_t txFirstWidthUs[8];      //!< of these: first pulse of a telegram
    uint32_t lateEdgesScheduledLate; //!< late TX edges whose alarm was programmed at most PwmEdgeLeadUs before the edge
    uint32_t lateEdgesAfterRx;       //!< other late TX edges: the RX edge interrupt ran when the alarm was due
    uint32_t lateEdgesOther;         //!< other late TX edges (interrupts disabled, interrupt entry)
    uint32_t edgeIrqLatencyUs[8];    //!< interrupts of a TX edge per delay after the alarm time ([7] 7 us or more)
    uint32_t txEdgeFallbacks;        //!< TX edges switched by the match processing, not by the TX edge interrupt

    // RX edge interrupt (capture incl. the timer interrupt handler) and timer interrupt handler
    uint32_t rxIrqUs[8]; //!< RX edge interrupts per duration: [0] below 2 us, [n] below 2^(n+1) us, [7] 128 us or more
    uint32_t rxIrqMaxUs; //!< longest RX edge interrupt
    uint32_t rxIrqFirstEchoMaxUs; //!< longest RX edge interrupt during the first pulse of a telegram (own echo)
    uint32_t alarmHandlerMaxUs;   //!< longest call of the timer interrupt handler from the alarm interrupt

    // Interrupt load of the timer emulation (order 052)
    uint32_t txEdgeIrqs; //!< TX edge interrupts (bus PWM without SBLIB_RP2350_PIO_BUS)
    uint32_t rxEdgeIrqs; //!< RX edge interrupts: GPIO interrupts, with SBLIB_RP2350_PIO_BUS DMA interrupts
    uint32_t irqBusyUs;  //!< time in the alarm, TX edge and RX edge interrupts (a nested interrupt counts twice)

    // RX edges by PIO and DMA (SBLIB_RP2350_PIO_BUS)
    uint32_t rxEdgeDelayUs[8]; //!< RX edges per delay to their interrupt: [0] below 2 us, [n] below 2^(n+1) us
    uint32_t rxEdgeDelayMaxUs; //!< longest delay from an RX edge to its interrupt
    uint32_t rxEdgeDuplicates; //!< DMA interrupts without a new edge time

    // TX edges by a PIO state machine (SBLIB_RP2350_PIO_BUS)
    uint32_t pioTxTargets;      //!< edge times given to the state machine
    uint32_t pioTxLateTargets;  //!< of these: moved because they were less than PioTargetLeadUs ahead
    uint32_t pioTxCancels;      //!< planned pulse starts cancelled
    uint32_t pioTxConfirmed;    //!< edges confirmed by the state machine at their time
    uint32_t pioTxResyncs;      //!< missing or wrong confirmation: TX low, state machine synchronised again
    uint32_t pioTxSyncAttempts; //!< attempts to start the state machine within its microsecond (begin() and resyncs)
    uint32_t pioTxStrayHigh;    //!< TX HIGH without a pulse of the emulation, ended by the next edge
};

/**
 * @brief Get the counters of the timer emulation.
 */
const volatile TimerEmulationStats& timerEmulationStats();
#endif

#endif /* Timer */

#if defined(SBLIB_PLATFORM_LPC11XX)
/**
 * @fn void delay_cycles(unsigned int)
 * @brief Delay for "cycles" CPU cycles
 *        The subs and cmp instructions both take one CPU cycle, the jump
 *        back takes two cycles, yielding four CPU cycles per loop iteration.
 *        from: https://www.mikrocontroller.net/topic/378584#4302525
 *
 * @param[in] cycles cpu cycles to delay
 */
static inline void delay_cycles(unsigned int cycles) __attribute__((always_inline));

static inline void delay_cycles(unsigned int cycles)
{
    __asm__ volatile (".syntax unified" "\n\t"
        " .align 4\n"
        "1: subs %[cycles], %[cycles], #1" "\n\t"
        "cmp %[cycles], #0" "\n\t"
        "bne 1b"
        : [cycles] "=l" (cycles)
        // not really used as output, but the register
        // is modified so the compiler must not rely on
        // the old contents
        : "0" (cycles) // this is also an input value
        : "cc");
}
#endif /* SBLIB_PLATFORM_LPC11XX: delay_cycles */
#endif /* SBLIB_TIMER_H_ */
