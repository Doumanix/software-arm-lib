/*
 *  pico_sim.cpp - Simulation of the Pico SDK functions for host unit tests, see pico_sim.h.
 *
 *  Alarms by register as on the RP2350 (TIMER: ALARMx, ARMED, INTR, INTE, INTF), interrupt
 *  handlers from the vector table (irq_set_exclusive_handler), NVIC priorities with preemption.
 *
 *  Time advances in sim::runUntil()/advance() and, with the busy wait model, while the code
 *  under test polls the time. Hardware events (bus level changes, sim::atHardware) happen in time
 *  order with the alarms, also during busy waiting; test callbacks (sim::at) only in runUntil().
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include "pico_sim.h"

#include <algorithm>
#include <deque>
#include <stdexcept>

sim_timer_hw_t sim_timer_hw = {};
sim_pio_hw_t sim_pio0_hw = {};

namespace
{

    constexpr unsigned AlarmCount = NUM_GENERIC_TIMERS * NUM_ALARMS;
    constexpr unsigned GpioCount = NUM_BANK0_GPIOS;
    constexpr unsigned MaxStepsWithoutTime = 100000; // more = interrupt storm
    constexpr unsigned BusyWaitReads = 16;           // time reads without progress until +1 us

    struct Alarm
    {
        bool claimed = false;
        bool armed = false;
        bool irqRaw = false;  // alarm fired (INTR)
        bool enabled = false; // INTE
        bool forced = false;  // forced IRQ (INTF)
        uint64_t target = 0;
        uint64_t releaseUs = 0; // interrupt latency: the interrupt runs from this time on
    };

    // Behaviour model of a state machine, chosen by its program (pio_sm_init)
    enum class SmModel : uint8_t
    {
        None,
        RxEdge, // timer.cpp RX program: one word into the RX FIFO per falling edge of the input pin
        TxEdge  // timer.cpp TX program: X counts microseconds, the pin toggles when X reaches ISR
    };

    constexpr unsigned FifoDepth = 4;

    struct PioSm
    {
        bool claimed = false;
        bool enabled = false;
        uint pc = 0;
        pio_sm_config config = {};
        SmModel model = SmModel::None;
        std::deque<uint32_t> rxFifo;
        std::deque<uint32_t> txFifo;

        // TxEdge model
        uint32_t x = 0;   // X when enabled (counts down from enabledAtUs on)
        uint32_t isr = 0; // edge time, 0 = none
        uint32_t osr = 0;
        uint64_t enabledAtUs = 0;
        uint64_t lastCommandUs = 0; // time the last pair of the TX FIFO became the edge times
        unsigned pendingPairs = 0;  // pairs in the TX FIFO with a scheduled take over
        uint64_t edgeToken = 0;     // changes with the edge time (a scheduled edge of an old one is void)
        uint64_t commandToken = 0;  // changes when the FIFO is cleared or the state machine stops
    };

    struct DmaChannel
    {
        bool claimed = false;
        dma_channel_config config = {};
        volatile void* writeAddr = nullptr;
        const volatile void* readAddr = nullptr;
        uint32_t reload = 0;    // transfer count copied on every trigger
        uint32_t remaining = 0; // transfers left in the running sequence
        bool busy = false;
        bool irq0Enabled = false;
    };

    struct Event
    {
        uint64_t timeUs;
        uint64_t seq;
        bool hardware; // hardware events also run during busy waiting
        std::function<void()> fn;
    };

    struct State
    {
        uint64_t nowUs = 0;
        uint32_t alarmLatencyUs = 0;
        bool globalEnabled = true;
        unsigned wfiSpuriousReturns = 0;
        std::function<void(sim::InterruptBoundary)> interruptBoundaryHook;
        std::vector<uint> running; // IRQ numbers of the running handlers, innermost last
        unsigned alarmCallbacks = 0;
        Alarm alarms[AlarmCount];
        bool irqEnabled[IRQ_COUNT] = {};
        bool nvicPending[IRQ_COUNT] = {}; // stays set when the source is cleared before the handler runs
        uint8_t irqPrio[IRQ_COUNT] = {};
        irq_handler_t vector[IRQ_COUNT] = {};
        sim::PinConfig config[GpioCount];
        unsigned gpioCalls = 0;
        bool level[GpioCount] = {};
        std::vector<sim::PinEvent> log[GpioCount];
        uint32_t latched[GpioCount] = {};
        uint32_t enabledEvents[GpioCount] = {};
        std::vector<std::pair<uint, irq_handler_t>> rawHandlers;
        std::vector<std::pair<uint, std::function<void(bool)>>> pinObservers;

        bool busyWaitAdvance = false;
        unsigned readsWithoutProgress = 0;
        bool processingEvents = false;
        unsigned processingIsrLevel = 0; // insideIsr when the busy wait processing started

        std::vector<Event> events;
        uint64_t eventSeq = 0;

        bool busConnected = false;
        uint busTx = 0;
        uint busRx = 0;
        uint32_t echoDelayUs = 0;
        bool ownLow = false; // our transmitter pulls the bus low (after the echo delay)
        int remoteLows = 0;  // number of other senders pulling the bus low
        bool busLow = false;
        std::vector<sim::PinEvent> busLog;

        uint32_t sysClockHz = SYS_CLK_HZ;
        uint16_t pioMemory[PIO_INSTRUCTION_COUNT] = {};
        uint32_t pioMemoryUsed = 0;                           // bit per instruction word
        uint8_t pioProgramLength[PIO_INSTRUCTION_COUNT] = {}; // length of the program loaded at an offset
        bool pioOwned[GpioCount] = {};                        // GPIO with a PIO function
        bool sioLatch[GpioCount] = {};                        // SIO output of such a GPIO
        PioSm sms[NUM_PIO_STATE_MACHINES];
        DmaChannel dma[NUM_DMA_CHANNELS];
        uint32_t dmaInts0 = 0;
    };

    State s;

    void interruptBoundary(const sim::InterruptBoundary boundary)
    {
        if (s.interruptBoundaryHook)
        {
            s.interruptBoundaryHook(boundary);
        }
    }

    void setNow(const uint64_t t)
    {
        if (t > s.nowUs)
        {
            s.nowUs = t;
            s.readsWithoutProgress = 0;
        }
    }

    bool gpioIrqAsserted()
    {
        for (unsigned g = 0; g < GpioCount; ++g)
        {
            if (s.latched[g] & s.enabledEvents[g])
            {
                return true;
            }
        }
        return false;
    }

    unsigned insideIsr()
    {
        return static_cast<unsigned>(s.running.size());
    }

    // Interrupt source active and enabled in the NVIC
    bool irqAsserted(const uint num)
    {
        if (!s.irqEnabled[num])
        {
            return false;
        }
        if (num >= TIMER0_IRQ_0 && num < TIMER0_IRQ_0 + AlarmCount)
        {
            const Alarm& al = s.alarms[num - TIMER0_IRQ_0];
            return ((al.irqRaw && al.enabled) || al.forced) && s.nowUs >= al.releaseUs;
        }
        if (num == IO_IRQ_BANK0)
        {
            return gpioIrqAsserted();
        }
        if (num == DMA_IRQ_0)
        {
            uint32_t enabledMask = 0;
            for (unsigned ch = 0; ch < NUM_DMA_CHANNELS; ++ch)
            {
                if (s.dma[ch].irq0Enabled)
                {
                    enabledMask |= 1u << ch;
                }
            }
            return (s.dmaInts0 & enabledMask) != 0;
        }
        return false;
    }

    // The interrupt may run now: interrupts enabled and a higher priority than the running handler
    bool canPreempt(const uint num)
    {
        return s.globalEnabled && (s.running.empty() || s.irqPrio[num] < s.irqPrio[s.running.back()]);
    }

    void runIrq(const uint num)
    {
        s.nvicPending[num] = false;
        s.running.push_back(num);
        if (num == IO_IRQ_BANK0)
        {
            const auto handlers = s.rawHandlers; // a handler may remove itself
            for (const auto& h: handlers)
            {
                if (s.latched[h.first] & s.enabledEvents[h.first])
                {
                    h.second();
                }
            }
        }
        else
        {
            if (!s.vector[num])
            {
                throw std::logic_error("pico_sim: interrupt without handler");
            }
            ++s.alarmCallbacks;
            s.vector[num]();
        }
        s.running.pop_back();
    }

    // Execute all pending interrupts that may run now: highest priority first, then lowest number
    void deliverPending()
    {
        uint64_t lastTime = s.nowUs;
        unsigned steps = 0;
        for (;;)
        {
            int next = -1;
            for (uint num = 0; num < IRQ_COUNT; ++num)
            {
                // Level sensitive: an active source makes an interrupt pending, but not while its handler
                // runs (pending again only if the source is still active after the handler)
                if (irqAsserted(num) && std::find(s.running.begin(), s.running.end(), num) == s.running.end())
                {
                    s.nvicPending[num] = true;
                }
                if (s.irqEnabled[num] && s.nvicPending[num] && (next < 0 || s.irqPrio[num] < s.irqPrio[next]))
                {
                    next = static_cast<int>(num);
                }
            }
            if (next < 0 || !canPreempt(static_cast<uint>(next)))
            {
                return;
            }
            runIrq(static_cast<uint>(next));

            if (s.nowUs != lastTime)
            {
                lastTime = s.nowUs;
                steps = 0;
            }
            else if (++steps > MaxStepsWithoutTime)
            {
                throw std::runtime_error("pico_sim: interrupt storm (100000 interrupts without time passing)");
            }
        }
    }

    void schedule(const uint64_t t, const bool hardware, std::function<void()> fn)
    {
        s.events.push_back({t, s.eventSeq++, hardware, std::move(fn)});
    }

    // Index of the next event due at or before endUs (hardware events only if hardwareOnly)
    int nextEvent(const uint64_t endUs, const bool hardwareOnly)
    {
        int best = -1;
        for (unsigned i = 0; i < s.events.size(); ++i)
        {
            const Event& e = s.events[i];
            if (e.timeUs > endUs || (hardwareOnly && !e.hardware))
            {
                continue;
            }
            if (best < 0 || e.timeUs < s.events[best].timeUs ||
                (e.timeUs == s.events[best].timeUs && e.seq < s.events[best].seq))
            {
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    int nextAlarm(const uint64_t endUs)
    {
        int next = -1;
        for (unsigned a = 0; a < AlarmCount; ++a)
        {
            const Alarm& al = s.alarms[a];
            if (al.armed && al.target <= endUs && (next < 0 || al.target < s.alarms[next].target))
            {
                next = static_cast<int>(a);
            }
        }
        return next;
    }

    /**
     * Process alarms and events up to endUs in time order. Events before alarms at the same time.
     * Returns when nothing is due anymore; the time is then <= endUs.
     */
    void processUntil(const uint64_t endUs, const bool hardwareOnly)
    {
        uint64_t lastTime = s.nowUs;
        unsigned steps = 0;
        for (;;)
        {
            deliverPending();

            const int ev = nextEvent(endUs, hardwareOnly);
            const int al = nextAlarm(endUs);
            if (ev < 0 && al < 0)
            {
                return;
            }

            if (ev >= 0 && (al < 0 || s.events[ev].timeUs <= s.alarms[al].target))
            {
                Event e = std::move(s.events[ev]);
                s.events.erase(s.events.begin() + ev);
                setNow(e.timeUs);
                e.fn();
            }
            else
            {
                Alarm& alarm = s.alarms[al];
                setNow(alarm.target);
                alarm.armed = false;
                alarm.irqRaw = true;
                const uint num = TIMER0_IRQ_0 + static_cast<uint>(al);
                if (irqAsserted(num) && canPreempt(num) && s.alarmLatencyUs)
                {
                    // The interrupt runs later, hardware events (bus, PIO) go on meanwhile
                    alarm.releaseUs = s.nowUs + s.alarmLatencyUs;
                    schedule(alarm.releaseUs, true, []() {});
                }
            }

            if (s.nowUs != lastTime)
            {
                lastTime = s.nowUs;
                steps = 0;
            }
            else if (++steps > MaxStepsWithoutTime)
            {
                throw std::runtime_error("pico_sim: no progress (100000 steps without time passing)");
            }
        }
    }

    /**
     * The code under test polls the time: with the busy wait model the time moves on. An interrupt
     * that runs during the busy wait of the main context (e.g. Bus::pause()) may busy wait itself.
     */
    uint64_t readTime()
    {
        const bool mayProcess = !s.processingEvents || insideIsr() > s.processingIsrLevel;
        if (s.busyWaitAdvance && mayProcess && ++s.readsWithoutProgress >= BusyWaitReads)
        {
            const bool outerProcessing = s.processingEvents;
            const unsigned outerIsrLevel = s.processingIsrLevel;
            s.processingEvents = true;
            s.processingIsrLevel = insideIsr();
            const uint64_t target = s.nowUs + 1;
            processUntil(target, true);
            setNow(target);
            s.processingEvents = outerProcessing;
            s.processingIsrLevel = outerIsrLevel;
            deliverPending();
        }
        return s.nowUs;
    }

    void pioFallingEdge(uint gpio);

    void updateBus()
    {
        const bool low = s.ownLow || s.remoteLows > 0;
        if (low == s.busLow)
        {
            return;
        }
        s.busLow = low;
        s.busLog.push_back({s.nowUs, !low});
        s.level[s.busRx] = !low;
        if (low)
        {
            s.latched[s.busRx] |= GPIO_IRQ_EDGE_FALL;
            pioFallingEdge(s.busRx);
        }
        else
        {
            s.latched[s.busRx] |= GPIO_IRQ_EDGE_RISE;
        }
    }

} // namespace

sim_timerawl_t::operator uint32_t() const volatile
{
    return static_cast<uint32_t>(readTime());
}

// ---- pico/platform.h ----
void __nop()
{
    interruptBoundary(sim::InterruptBoundary::BeforeWait);
    (void)readTime();
}

void __wfi()
{
    interruptBoundary(sim::InterruptBoundary::BeforeWait);
    // Bounded behavioural model for Thread mode, BASEPRI/FAULTMASK clear, normal sleep.
    // PRIMASK prevents dispatch, but does not prevent an enabled IRQ from waking WFI.
    // No clock gating, power states, security extensions or actual CPU sleep are modelled.
    if (s.globalEnabled || !s.running.empty())
    {
        throw std::logic_error("pico_sim: WFI model requires PRIMASK-set Thread mode");
    }
    if (s.wfiSpuriousReturns > 0)
    {
        --s.wfiSpuriousReturns;
        return;
    }
    const uint64_t deadline = s.nowUs + 10000;
    for (;;)
    {
        deliverPending();
        for (uint num = 0; num < IRQ_COUNT; ++num)
        {
            if (s.irqEnabled[num] && s.nvicPending[num])
            {
                return;
            }
        }
        if (s.nowUs >= deadline)
        {
            throw std::runtime_error("pico_sim: WFI without wake IRQ within 10000 us");
        }
        sim::advance(1);
    }
}

uint __get_current_exception()
{
    if (s.running.empty())
    {
        return 0u;
    }
    return VTABLE_FIRST_IRQ + s.running.back();
}

// ---- hardware/irq.h ----
void irq_set_enabled(const uint num, const bool enabled)
{
    if (enabled && !s.irqEnabled[num])
    {
        s.nvicPending[num] = false; // the SDK clears the pending state before enabling
    }
    s.irqEnabled[num] = enabled;
    deliverPending();
}

bool irq_is_enabled(const uint num)
{
    return s.irqEnabled[num];
}

void irq_set_priority(const uint num, const uint8_t hardware_priority)
{
    s.irqPrio[num] = hardware_priority;
}

void irq_clear(const uint num)
{
    s.nvicPending[num] = false;
}
void irq_set_pending(const uint num)
{
    s.nvicPending[num] = true;
    deliverPending();
}

void irq_set_exclusive_handler(const uint num, const irq_handler_t handler)
{
    if (s.vector[num] && s.vector[num] != handler)
    {
        throw std::logic_error("pico_sim: interrupt handler already set");
    }
    s.vector[num] = handler;
}

void irq_remove_handler(const uint num, const irq_handler_t handler)
{
    if (s.vector[num] == handler)
    {
        s.vector[num] = nullptr;
    }
}

// ---- hardware/sync.h ----
uint32_t save_and_disable_interrupts()
{
    uint32_t status = 1u; // PRIMASK
    if (s.globalEnabled)
    {
        status = 0u;
    }
    s.globalEnabled = false;
    return status;
}

void restore_interrupts(const uint32_t status)
{
    s.globalEnabled = (status == 0u);
    deliverPending();
}

void disable_interrupts()
{
    interruptBoundary(sim::InterruptBoundary::BeforeDisable);
    s.globalEnabled = false;
    interruptBoundary(sim::InterruptBoundary::Disabled);
}

void enable_interrupts()
{
    interruptBoundary(sim::InterruptBoundary::BeforeEnable);
    s.globalEnabled = true;
    deliverPending();
}

// ---- hardware/gpio.h ----
namespace
{

    // Output level of a GPIO (SIO or PIO): log, observers, bus
    void driveLevel(const uint gpio, const bool value)
    {
        const bool changed = (s.level[gpio] != value);
        if (changed)
        {
            s.log[gpio].push_back({s.nowUs, value});
        }
        s.level[gpio] = value;
        if (changed)
        {
            for (const auto& o: s.pinObservers)
            {
                if (o.first == gpio)
                {
                    o.second(value);
                }
            }
        }

        if (s.busConnected && gpio == s.busTx)
        {
            auto apply = [value]()
            {
                s.ownLow = value;
                updateBus();
            };
            if (s.echoDelayUs == 0)
            {
                apply();
            }
            else
            {
                schedule(s.nowUs + s.echoDelayUs, true, apply);
            }
        }
    }

} // namespace

void gpio_set_function(const uint gpio, const gpio_function_t fn)
{
    ++s.gpioCalls;
    ++s.config[gpio].calls;
    s.config[gpio].function = fn;
    s.config[gpio].inputEnabled = true;
    s.pioOwned[gpio] = (fn == GPIO_FUNC_PIO0);
}

void gpio_put(const uint gpio, const bool value)
{
    ++s.gpioCalls;
    if (s.pioOwned[gpio])
    {
        s.sioLatch[gpio] = value; // the pin is driven by PIO
        return;
    }
    driveLevel(gpio, value);
}

bool gpio_get(const uint gpio)
{
    ++s.gpioCalls;
    return s.level[gpio];
}

void gpio_set_mask64(const uint64_t mask)
{
    for (uint g = 0; g < GpioCount; ++g)
    {
        if (mask & (static_cast<uint64_t>(1) << g))
        {
            gpio_put(g, true);
        }
    }
}

void gpio_clr_mask64(const uint64_t mask)
{
    for (uint g = 0; g < GpioCount; ++g)
    {
        if (mask & (static_cast<uint64_t>(1) << g))
        {
            gpio_put(g, false);
        }
    }
}

uint32_t gpio_get_irq_event_mask(const uint gpio)
{
    return s.latched[gpio] & s.enabledEvents[gpio];
}

void gpio_acknowledge_irq(const uint gpio, const uint32_t event_mask)
{
    ++s.gpioCalls;
    s.latched[gpio] &= ~event_mask;
}

void gpio_set_irq_enabled(const uint gpio, const uint32_t event_mask, const bool enabled)
{
    ++s.gpioCalls;
    if (enabled)
    {
        s.enabledEvents[gpio] |= event_mask;
    }
    else
    {
        s.enabledEvents[gpio] &= ~event_mask;
    }
    deliverPending();
}

void gpio_add_raw_irq_handler(const uint gpio, const irq_handler_t handler)
{
    ++s.gpioCalls;
    s.rawHandlers.emplace_back(gpio, handler);
}

void gpio_remove_raw_irq_handler(const uint gpio, const irq_handler_t handler)
{
    ++s.gpioCalls;
    s.rawHandlers.erase(std::remove(s.rawHandlers.begin(), s.rawHandlers.end(), std::make_pair(gpio, handler)),
                        s.rawHandlers.end());
}

// ---- hardware/timer.h ----
uint64_t time_us_64()
{
    return readTime();
}

namespace
{

    uint32_t alarmMask(const unsigned a)
    {
        return 1u << a;
    }

    // ALARMx written: arm the alarm, it fires when TIMERAWL reaches the value (32 bit wrap)
    void writeAlarm(const unsigned a, const uint32_t value)
    {
        Alarm& al = s.alarms[a];
        uint64_t delta = static_cast<uint32_t>(value - static_cast<uint32_t>(s.nowUs));
        if (delta == 0)
        {
            delta = static_cast<uint64_t>(1) << 32; // the time is reached already: after the wrap
        }
        al.target = s.nowUs + delta;
        al.armed = true;
    }

    uint32_t readAlarmBits(bool Alarm::* bit)
    {
        uint32_t value = 0;
        for (unsigned a = 0; a < AlarmCount; ++a)
        {
            if (s.alarms[a].*bit)
            {
                value |= alarmMask(a);
            }
        }
        return value;
    }

    void writeAlarmBits(bool Alarm::* bit, const uint32_t value)
    {
        for (unsigned a = 0; a < AlarmCount; ++a)
        {
            s.alarms[a].*bit = (value & alarmMask(a)) != 0;
        }
    }

    // Write 1 clears
    void clearAlarmBits(bool Alarm::* bit, const uint32_t mask)
    {
        for (unsigned a = 0; a < AlarmCount; ++a)
        {
            if (mask & alarmMask(a))
            {
                s.alarms[a].*bit = false;
            }
        }
    }

} // namespace

sim_timer_reg_t& sim_timer_reg_t::operator=(const uint32_t value)
{
    switch (kind)
    {
        case Alarm:
            writeAlarm(index, value);
            break;
        case Armed:
            clearAlarmBits(&::Alarm::armed, value);
            break;
        case Intr:
            clearAlarmBits(&::Alarm::irqRaw, value);
            break;
        case Inte:
            writeAlarmBits(&::Alarm::enabled, value);
            break;
        case Intf:
            writeAlarmBits(&::Alarm::forced, value);
            break;
        case Ints:
            break; // read only
    }
    deliverPending();
    return *this;
}

sim_timer_reg_t::operator uint32_t() const
{
    switch (kind)
    {
        case Alarm:
            return static_cast<uint32_t>(s.alarms[index].target);
        case Armed:
            return readAlarmBits(&::Alarm::armed);
        case Intr:
            return readAlarmBits(&::Alarm::irqRaw);
        case Inte:
            return readAlarmBits(&::Alarm::enabled);
        case Intf:
            return readAlarmBits(&::Alarm::forced);
        case Ints:
        {
            uint32_t value = 0;
            for (unsigned a = 0; a < AlarmCount; ++a)
            {
                const ::Alarm& al = s.alarms[a];
                if ((al.irqRaw && al.enabled) || al.forced)
                {
                    value |= alarmMask(a);
                }
            }
            return value;
        }
    }
    return 0;
}

void hw_set_bits(sim_timer_reg_t* reg, const uint32_t mask)
{
    *reg = static_cast<uint32_t>(*reg) | mask;
}

void hw_clear_bits(sim_timer_reg_t* reg, const uint32_t mask)
{
    *reg = static_cast<uint32_t>(*reg) & ~mask;
}

int hardware_alarm_claim_unused(const bool required)
{
    for (unsigned a = 0; a < AlarmCount; ++a)
    {
        if (!s.alarms[a].claimed)
        {
            s.alarms[a].claimed = true;
            return static_cast<int>(a);
        }
    }
    if (required)
    {
        throw std::runtime_error("pico_sim: no free hardware alarm");
    }
    return -1;
}

void hardware_alarm_unclaim(const uint alarm_num)
{
    s.alarms[alarm_num] = Alarm{};
}

uint hardware_alarm_get_irq_num(const uint alarm_num)
{
    return TIMER0_IRQ_0 + alarm_num;
}

// ---- hardware/clocks.h ----
uint32_t clock_get_hz(const clock_handle_t clock)
{
    if (clock != clk_sys)
    {
        throw std::logic_error("pico_sim: clock not simulated");
    }
    return s.sysClockHz;
}

// ---- hardware/pio.h, hardware/dma.h ----
namespace
{

    constexpr uint16_t RxEdgeProgram[] = {0x20a0, 0x2020, 0x8000}; // wait 1 pin 0, wait 0 pin 0, push noblock

    PioSm& smOf(const PIO pio, const uint sm)
    {
        if (pio != pio0 || sm >= NUM_PIO_STATE_MACHINES)
        {
            throw std::logic_error("pico_sim: only PIO0 SM 0..3 simulated");
        }
        return s.sms[sm];
    }

    bool programAt(const uint pc, const uint16_t* words, const unsigned count)
    {
        for (unsigned i = 0; i < count; ++i)
        {
            if (pc + i >= PIO_INSTRUCTION_COUNT || s.pioMemory[pc + i] != words[i])
            {
                return false;
            }
        }
        return true;
    }

    // Source of a DMA read: TIMERAWL, a PIO RX FIFO (pops it) or plain memory
    uint32_t dmaRead(const volatile void* addr)
    {
        if (addr == static_cast<const volatile void*>(&sim_timer_hw.timerawl))
        {
            return static_cast<uint32_t>(s.nowUs);
        }
        for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm)
        {
            if (addr == static_cast<const volatile void*>(&sim_pio0_hw.rxf[sm]))
            {
                PioSm& st = s.sms[sm];
                if (st.rxFifo.empty())
                {
                    throw std::logic_error("pico_sim: DMA read of an empty PIO RX FIFO");
                }
                const uint32_t value = st.rxFifo.front();
                st.rxFifo.pop_front();
                return value;
            }
        }
        return *static_cast<const volatile uint32_t*>(addr);
    }

    void dmaWrite(volatile void* addr, const uint32_t value)
    {
        *static_cast<volatile uint32_t*>(addr) = value;
    }

    bool dreqActive(const uint dreq)
    {
        if (dreq == DREQ_FORCE)
        {
            return true;
        }
        if (dreq >= DREQ_PIO0_RX0 && dreq < DREQ_PIO0_RX0 + NUM_PIO_STATE_MACHINES)
        {
            return !s.sms[dreq - DREQ_PIO0_RX0].rxFifo.empty();
        }
        return false;
    }

    // Run the DMA channels until none can transfer anymore (transfers take no time)
    void dmaService()
    {
        bool progress = true;
        while (progress)
        {
            progress = false;
            for (uint ch = 0; ch < NUM_DMA_CHANNELS; ++ch)
            {
                DmaChannel& c = s.dma[ch];
                if (!c.busy || !dreqActive(c.config.dreq))
                {
                    continue;
                }
                if (c.config.size != DMA_SIZE_32 || c.config.readIncrement || c.config.writeIncrement)
                {
                    throw std::logic_error("pico_sim: only 32 bit DMA without address increment simulated");
                }
                dmaWrite(c.writeAddr, dmaRead(c.readAddr));
                progress = true;
                if (--c.remaining != 0)
                {
                    continue;
                }
                c.busy = false;
                if (c.irq0Enabled)
                {
                    s.dmaInts0 |= 1u << ch;
                }
                if (c.config.chainTo != ch)
                {
                    DmaChannel& next = s.dma[c.config.chainTo];
                    next.busy = true;
                    next.remaining = next.reload;
                }
            }
        }
        deliverPending();
    }

    constexpr uint16_t TxEdgeLoopWord = 0xa046;   // mov y, isr
    constexpr uint16_t TxEdgeToggleWord = 0xa008; // mov pins, ~pins
    constexpr unsigned TxEdgeLength = 17;
    constexpr unsigned TxEdgeStart = 4;
    constexpr unsigned TxEdgeWrap = 12;
    constexpr uint32_t MaxEdgeWaitUs = 60000000; // more: an edge time X passed already (wait for the wrap)

    uint32_t txX(const PioSm& st, const uint64_t atUs)
    {
        return st.x - static_cast<uint32_t>(atUs - st.enabledAtUs);
    }

    // The edge time ISR is compared from the microsecond fromUs on
    void txScheduleEdge(const uint sm, const uint64_t fromUs)
    {
        PioSm& st = s.sms[sm];
        ++st.edgeToken;
        if (st.isr == 0)
        {
            return;
        }
        const uint32_t waitUs = txX(st, fromUs) - st.isr;
        if (waitUs > MaxEdgeWaitUs)
        {
            throw std::logic_error("pico_sim: PIO TX edge time already passed (the state machine would wait 71 min)");
        }
        const uint64_t token = st.edgeToken;
        schedule(fromUs + waitUs, true,
                 [sm, token]()
                 {
                     PioSm& t = s.sms[sm];
                     if (!t.enabled || t.edgeToken != token)
                     {
                         return;
                     }
                     driveLevel(t.config.outBase, !s.level[t.config.outBase]);
                     if (t.rxFifo.size() < FifoDepth)
                     {
                         t.rxFifo.push_back(t.isr);
                     }
                     t.isr = t.osr; // the edge after it, compared from the next microsecond on
                     t.osr = 0;
                     txScheduleEdge(sm, s.nowUs + 1);
                 });
    }

    /**
     * The next pair of the TX FIFO becomes the edge times. Worst case of the real program: a pair written in the
     * microsecond w after the FIFO check of that microsecond is pulled in w + 1 (after its edge compare) and
     * compared from w + 2 on. One pair per microsecond.
     */
    void txScheduleCommand(const uint sm)
    {
        PioSm& st = s.sms[sm];
        uint64_t atUs = s.nowUs + 1;
        if (atUs <= st.lastCommandUs)
        {
            atUs = st.lastCommandUs + 1;
        }
        st.lastCommandUs = atUs;
        ++st.pendingPairs;
        const uint64_t token = st.commandToken;
        schedule(atUs, true,
                 [sm, token]()
                 {
                     PioSm& t = s.sms[sm];
                     if (!t.enabled || t.commandToken != token || t.txFifo.size() < 2)
                     {
                         return;
                     }
                     --t.pendingPairs;
                     t.isr = t.txFifo.front();
                     t.txFifo.pop_front();
                     t.osr = t.txFifo.front();
                     t.txFifo.pop_front();
                     txScheduleEdge(sm, s.nowUs + 1);
                 });
    }

    // Schedule the take over of the complete pairs in the TX FIFO
    void txScheduleCommands(const uint sm)
    {
        PioSm& st = s.sms[sm];
        while (st.txFifo.size() >= 2 * (st.pendingPairs + 1))
        {
            txScheduleCommand(sm);
        }
    }

    void txEnable(const uint sm)
    {
        PioSm& st = s.sms[sm];
        st.enabled = true;
        st.enabledAtUs = s.nowUs;
        st.lastCommandUs = 0;
        st.pendingPairs = 0;
        ++st.commandToken;
        txScheduleCommands(sm);
        txScheduleEdge(sm, s.nowUs);
    }

    void txDisable(const uint sm)
    {
        PioSm& st = s.sms[sm];
        st.x = txX(st, s.nowUs);
        st.enabled = false;
        ++st.edgeToken;
        ++st.commandToken;
        st.pendingPairs = 0;
    }

    void pioFallingEdge(const uint gpio)
    {
        bool pushed = false;
        for (auto& st: s.sms)
        {
            if (st.enabled && st.model == SmModel::RxEdge && st.config.inBase == gpio && st.rxFifo.size() < FifoDepth)
            {
                st.rxFifo.push_back(0);
                pushed = true;
            }
        }
        if (pushed)
        {
            dmaService();
        }
    }

} // namespace

int pio_claim_unused_sm(const PIO pio, const bool required)
{
    for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm)
    {
        PioSm& st = smOf(pio, sm);
        if (!st.claimed)
        {
            st = PioSm{};
            st.claimed = true;
            return static_cast<int>(sm);
        }
    }
    if (required)
    {
        throw std::runtime_error("pico_sim: no free PIO state machine");
    }
    return -1;
}

void pio_sm_unclaim(const PIO pio, const uint sm)
{
    smOf(pio, sm) = PioSm{};
}

int pio_add_program(const PIO pio, const pio_program_t* program)
{
    (void)smOf(pio, 0);
    const uint32_t mask = (1u << program->length) - 1u;
    // like the SDK: the highest free offset
    for (int offset = static_cast<int>(PIO_INSTRUCTION_COUNT - program->length); offset >= 0; --offset)
    {
        if (s.pioMemoryUsed & (mask << offset))
        {
            continue;
        }
        for (unsigned i = 0; i < program->length; ++i)
        {
            uint16_t instr = program->instructions[i];
            if ((instr & 0xe000u) == 0) // JMP: relocated like by the SDK
            {
                instr = static_cast<uint16_t>(instr + offset);
            }
            s.pioMemory[offset + i] = instr;
        }
        s.pioMemoryUsed |= mask << offset;
        s.pioProgramLength[offset] = program->length;
        return offset;
    }
    throw std::runtime_error("pico_sim: no space for the PIO program");
}

void pio_remove_program(const PIO pio, const pio_program_t* program, const uint loaded_offset)
{
    (void)smOf(pio, 0);
    s.pioMemoryUsed &= ~(((1u << program->length) - 1u) << loaded_offset);
}

pio_sm_config pio_get_default_sm_config()
{
    pio_sm_config c = {};
    c.wrap = PIO_INSTRUCTION_COUNT - 1;
    c.inCount = 32;
    c.outCount = 32;
    c.clkdivInt = 1;
    return c;
}

void sm_config_set_wrap(pio_sm_config* c, const uint wrap_target, const uint wrap)
{
    c->wrapTarget = wrap_target;
    c->wrap = wrap;
}

void sm_config_set_in_pins(pio_sm_config* c, const uint in_base)
{
    c->inBase = in_base;
}

void sm_config_set_in_pin_count(pio_sm_config* c, const uint in_count)
{
    c->inCount = in_count;
}

void sm_config_set_out_pins(pio_sm_config* c, const uint out_base, const uint out_count)
{
    c->outBase = out_base;
    c->outCount = out_count;
}

void sm_config_set_set_pins(pio_sm_config* c, const uint set_base, const uint set_count)
{
    c->setBase = set_base;
    c->setCount = set_count;
}

void sm_config_set_jmp_pin(pio_sm_config* c, const uint pin)
{
    c->jmpPin = pin;
}

void sm_config_set_clkdiv_int_frac(pio_sm_config* c, const uint16_t div_int, const uint8_t div_frac8)
{
    c->clkdivInt = div_int;
    c->clkdivFrac = div_frac8;
}

void sm_config_set_mov_status(pio_sm_config* c, const enum pio_mov_status_type status_sel, const uint status_n)
{
    c->statusSel = status_sel;
    c->statusN = status_n;
}

int pio_sm_init(const PIO pio, const uint sm, const uint initial_pc, const pio_sm_config* config)
{
    PioSm& st = smOf(pio, sm);
    st.enabled = false;
    st.pc = initial_pc;
    st.config = *config;
    st.rxFifo.clear();
    st.txFifo.clear();
    st.model = SmModel::None;
    if (programAt(initial_pc, RxEdgeProgram, 3) && config->wrapTarget == initial_pc && config->wrap == initial_pc + 2)
    {
        st.model = SmModel::RxEdge;
    }
    if (initial_pc >= TxEdgeStart && s.pioProgramLength[initial_pc - TxEdgeStart] == TxEdgeLength &&
        s.pioMemory[initial_pc] == TxEdgeLoopWord && s.pioMemory[initial_pc + 3] == TxEdgeToggleWord &&
        config->wrapTarget == initial_pc && config->wrap == initial_pc - TxEdgeStart + TxEdgeWrap)
    {
        if (config->clkdivInt * 10000000ull != s.sysClockHz || config->clkdivFrac != 0 ||
            config->statusSel != STATUS_TX_LESSTHAN || config->statusN != 2 || config->outBase != config->inBase ||
            config->outCount != 1)
        {
            throw std::logic_error("pico_sim: TX state machine configuration not as the model assumes");
        }
        st.model = SmModel::TxEdge;
        st.x = 0;
        st.isr = 0;
        st.osr = 0;
    }
    return 0;
}

void pio_sm_set_enabled(const PIO pio, const uint sm, const bool enabled)
{
    PioSm& st = smOf(pio, sm);
    if (st.model == SmModel::TxEdge && enabled != st.enabled)
    {
        if (enabled)
        {
            txEnable(sm);
        }
        else
        {
            txDisable(sm);
        }
        return;
    }
    st.enabled = enabled;
}

void pio_enable_sm_mask_in_sync(const PIO pio, const uint32_t mask)
{
    for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm)
    {
        if (mask & (1u << sm))
        {
            pio_sm_set_enabled(pio, sm, true);
        }
    }
}

int pio_sm_set_consecutive_pindirs(const PIO pio, const uint sm, uint, uint, bool)
{
    (void)smOf(pio, sm);
    return 0;
}

void pio_gpio_init(const PIO pio, const uint pin)
{
    (void)smOf(pio, 0);
    gpio_set_function(pin, GPIO_FUNC_PIO0);
}

uint pio_get_dreq(const PIO pio, const uint sm, const bool is_tx)
{
    (void)smOf(pio, sm);
    if (is_tx)
    {
        return DREQ_PIO0_TX0 + sm;
    }
    return DREQ_PIO0_RX0 + sm;
}

void pio_sm_exec(const PIO pio, const uint sm, const uint instr)
{
    PioSm& st = smOf(pio, sm);
    if (st.model != SmModel::TxEdge || st.enabled)
    {
        throw std::logic_error("pico_sim: pio_sm_exec only on the stopped TX state machine");
    }
    if ((instr & 0xffe0u) == 0xe000u) // set pins, v
    {
        driveLevel(st.config.setBase, (instr & 1u) != 0);
    }
    else if (instr == 0xa0c3u) // mov isr, null
    {
        st.isr = 0;
    }
    else if ((instr & 0xffe0u) == 0x0000u) // jmp addr
    {
        st.pc = instr & 0x1fu;
    }
    else if (instr == 0x80a0u) // pull block
    {
        if (st.txFifo.empty())
        {
            throw std::logic_error("pico_sim: pull of an empty TX FIFO would stall");
        }
        st.osr = st.txFifo.front();
        st.txFifo.pop_front();
    }
    else if (instr == 0xa027u) // mov x, osr
    {
        st.x = st.osr;
    }
    else if (instr == 0xa0e3u) // mov osr, null
    {
        st.osr = 0;
    }
    else
    {
        throw std::logic_error("pico_sim: instruction not simulated for pio_sm_exec");
    }
}

void pio_sm_put(const PIO pio, const uint sm, const uint32_t data)
{
    PioSm& st = smOf(pio, sm);
    if (st.txFifo.size() >= FifoDepth)
    {
        throw std::logic_error("pico_sim: write to a full PIO TX FIFO (the word would be lost)");
    }
    st.txFifo.push_back(data);
    if (st.model == SmModel::TxEdge && st.enabled)
    {
        txScheduleCommands(sm);
    }
}

void pio_sm_restart(const PIO pio, const uint sm)
{
    (void)smOf(pio, sm);
}

bool pio_sm_is_tx_fifo_full(const PIO pio, const uint sm)
{
    return smOf(pio, sm).txFifo.size() >= FifoDepth;
}

uint32_t pio_sm_get(const PIO pio, const uint sm)
{
    PioSm& st = smOf(pio, sm);
    if (st.rxFifo.empty())
    {
        return 0; // the hardware returns 0 from an empty FIFO
    }
    const uint32_t value = st.rxFifo.front();
    st.rxFifo.pop_front();
    return value;
}

bool pio_sm_is_rx_fifo_empty(const PIO pio, const uint sm)
{
    return smOf(pio, sm).rxFifo.empty();
}

uint pio_sm_get_tx_fifo_level(const PIO pio, const uint sm)
{
    return static_cast<uint>(smOf(pio, sm).txFifo.size());
}

void pio_sm_clear_fifos(const PIO pio, const uint sm)
{
    PioSm& st = smOf(pio, sm);
    st.rxFifo.clear();
    st.txFifo.clear();
    ++st.commandToken;
    st.pendingPairs = 0;
}

int dma_claim_unused_channel(const bool required)
{
    for (uint ch = 0; ch < NUM_DMA_CHANNELS; ++ch)
    {
        if (!s.dma[ch].claimed)
        {
            s.dma[ch] = DmaChannel{};
            s.dma[ch].claimed = true;
            return static_cast<int>(ch);
        }
    }
    if (required)
    {
        throw std::runtime_error("pico_sim: no free DMA channel");
    }
    return -1;
}

void dma_channel_unclaim(const uint channel)
{
    s.dma[channel] = DmaChannel{};
}

dma_channel_config dma_channel_get_default_config(const uint channel)
{
    dma_channel_config c = {};
    c.size = DMA_SIZE_32;
    c.readIncrement = true;
    c.writeIncrement = false;
    c.dreq = DREQ_FORCE;
    c.chainTo = channel;
    return c;
}

dma_channel_config dma_get_channel_config(const uint channel)
{
    return s.dma[channel].config;
}

void channel_config_set_transfer_data_size(dma_channel_config* c, const dma_channel_transfer_size_t size)
{
    c->size = size;
}

void channel_config_set_read_increment(dma_channel_config* c, const bool incr)
{
    c->readIncrement = incr;
}

void channel_config_set_write_increment(dma_channel_config* c, const bool incr)
{
    c->writeIncrement = incr;
}

void channel_config_set_dreq(dma_channel_config* c, const uint dreq)
{
    c->dreq = dreq;
}

void channel_config_set_chain_to(dma_channel_config* c, const uint chain_to)
{
    c->chainTo = chain_to;
}

void channel_config_set_high_priority(dma_channel_config* c, const bool high_priority)
{
    c->highPriority = high_priority;
}

void dma_channel_configure(const uint channel, const dma_channel_config* config, volatile void* write_addr,
                           const volatile void* read_addr, const uint transfer_count, const bool trigger)
{
    DmaChannel& c = s.dma[channel];
    c.config = *config;
    c.writeAddr = write_addr;
    c.readAddr = read_addr;
    c.reload = transfer_count;
    if (trigger)
    {
        dma_channel_start(channel);
    }
}

void dma_channel_set_config(const uint channel, const dma_channel_config* config, const bool trigger)
{
    s.dma[channel].config = *config;
    if (trigger)
    {
        dma_channel_start(channel);
    }
}

void dma_channel_start(const uint channel)
{
    DmaChannel& c = s.dma[channel];
    c.busy = true;
    c.remaining = c.reload;
    dmaService();
}

void dma_channel_cleanup(const uint channel)
{
    DmaChannel& c = s.dma[channel];
    c.irq0Enabled = false;
    c.busy = false;
    s.dmaInts0 &= ~(1u << channel);
}

void dma_channel_set_irq0_enabled(const uint channel, const bool enabled)
{
    s.dma[channel].irq0Enabled = enabled;
    deliverPending();
}

void dma_channel_acknowledge_irq0(const uint channel)
{
    s.dmaInts0 &= ~(1u << channel);
}

// ---- simulation control ----
namespace sim
{

    void setWfiSpuriousReturns(const unsigned count)
    {
        s.wfiSpuriousReturns = count;
    }

    void setInterruptBoundaryHook(std::function<void(InterruptBoundary)> hook)
    {
        s.interruptBoundaryHook = std::move(hook);
    }

    void reset(const uint64_t startUs)
    {
        s = State{};
        s.nowUs = startUs;
        for (auto& prio: s.irqPrio)
        {
            prio = 0x80; // PICO_DEFAULT_IRQ_PRIORITY
        }
    }

    uint64_t now()
    {
        return s.nowUs;
    }

    void advance(const uint64_t us)
    {
        runUntil(s.nowUs + us);
    }

    void runUntil(const uint64_t endUs)
    {
        if (endUs < s.nowUs)
        {
            throw std::logic_error("pico_sim: time must not run backwards");
        }
        processUntil(endUs, false);
        deliverPending();
        setNow(endUs);
    }

    void setAlarmLatency(const uint32_t us)
    {
        s.alarmLatencyUs = us;
    }

    void fallingEdge(const uint gpio)
    {
        s.latched[gpio] |= GPIO_IRQ_EDGE_FALL;
        pioFallingEdge(gpio);
        deliverPending();
    }

    const std::vector<PinEvent>& pinLog(const uint gpio)
    {
        return s.log[gpio];
    }

    bool pinLevel(const uint gpio)
    {
        return s.level[gpio];
    }

    unsigned alarmIrqCount()
    {
        return s.alarmCallbacks;
    }

    bool alarmArmed(const uint alarm_num)
    {
        return s.alarms[alarm_num].armed;
    }

    bool interruptsEnabled()
    {
        return s.globalEnabled;
    }

    bool insideInterrupt()
    {
        return insideIsr() != 0;
    }

    uint8_t irqPriority(const uint num)
    {
        return s.irqPrio[num];
    }

    void setBusyWaitAdvance(const bool enabled)
    {
        s.busyWaitAdvance = enabled;
    }

    void at(const uint64_t atUs, std::function<void()> fn)
    {
        schedule(atUs, false, std::move(fn));
    }

    void atHardware(const uint64_t atUs, std::function<void()> fn)
    {
        if (atUs < s.nowUs)
        {
            throw std::logic_error("pico_sim: hardware event in the past");
        }
        schedule(atUs, true, std::move(fn));
    }

    void onPinChange(const uint gpio, std::function<void(bool level)> fn)
    {
        s.pinObservers.emplace_back(gpio, std::move(fn));
    }

    void connectBus(const uint txPin, const uint rxPin, const uint32_t echoDelayUs)
    {
        s.busConnected = true;
        s.busTx = txPin;
        s.busRx = rxPin;
        s.echoDelayUs = echoDelayUs;
        s.level[rxPin] = true; // idle bus
    }

    void remoteLow(const uint64_t startUs, const uint32_t durationUs)
    {
        schedule(startUs, true,
                 []()
                 {
                     ++s.remoteLows;
                     updateBus();
                 });
        schedule(startUs + durationUs, true,
                 []()
                 {
                     --s.remoteLows;
                     updateBus();
                 });
    }

    const std::vector<PinEvent>& busLog()
    {
        return s.busLog;
    }

    bool pioSmClaimed(const uint sm)
    {
        return s.sms[sm].claimed;
    }

    bool pioSmEnabled(const uint sm)
    {
        return s.sms[sm].enabled;
    }

    unsigned pioProgramWordsUsed()
    {
        unsigned count = 0;
        for (unsigned i = 0; i < PIO_INSTRUCTION_COUNT; ++i)
        {
            if (s.pioMemoryUsed & (1u << i))
            {
                ++count;
            }
        }
        return count;
    }

    bool dmaChannelClaimed(const uint channel)
    {
        return s.dma[channel].claimed;
    }

    void setSysClockHz(const uint32_t hz)
    {
        s.sysClockHz = hz;
    }

} // namespace sim

void gpio_set_dir(const uint gpio, const bool output)
{
    ++s.gpioCalls;
    ++s.config[gpio].calls;
    s.config[gpio].output = output;
}
void gpio_set_pulls(const uint gpio, const bool up, const bool down)
{
    ++s.gpioCalls;
    ++s.config[gpio].calls;
    s.config[gpio].pullUp = up;
    s.config[gpio].pullDown = down;
}
void gpio_disable_pulls(const uint gpio)
{
    gpio_set_pulls(gpio, false, false);
}
void gpio_set_input_enabled(const uint gpio, const bool enabled)
{
    ++s.gpioCalls;
    ++s.config[gpio].calls;
    s.config[gpio].inputEnabled = enabled;
}
uint32_t time_us_32()
{
    return static_cast<uint32_t>(time_us_64());
}
uint32_t gpio_get_all()
{
    uint32_t value = 0;
    for (uint gpio = 0; gpio < 32; ++gpio)
    {
        if (gpio_get(gpio))
        {
            value |= 1u << gpio;
        }
    }
    return value;
}
namespace sim
{
    const PinConfig& pinConfig(const uint gpio)
    {
        return s.config[gpio];
    }
    uint32_t pinIrqEvents(const uint gpio)
    {
        return s.enabledEvents[gpio];
    }
    unsigned gpioCalls()
    {
        return s.gpioCalls;
    }
} // namespace sim
