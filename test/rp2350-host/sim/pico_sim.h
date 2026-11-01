/*
 *  pico_sim.h - Minimal simulation of the Pico SDK functions used by the RP2350 timer
 *               emulation (sblib/src/rp2350/timer.cpp), for host unit tests.
 *
 *  Simulated: 64 bit microsecond time (TIMER0, timerawl = low 32 bits), the alarm registers of
 *  TIMER0 (ALARMx, ARMED, INTR, INTE, INTF), exclusive interrupt handlers in the vector table,
 *  GPIO output levels, GPIO edge events with raw IRQ handlers, the global interrupt enable
 *  (save_and_disable_interrupts...).
 *
 *  For SBLIB_RP2350_PIO_BUS (order 052): PIO0 with behaviour models of the state machines of the
 *  timer emulation (recognised by their program), and the DMA channels as far as the emulation uses
 *  them (32 bit single transfers, DREQ of a PIO RX FIFO or unpaced, chaining, IRQ 0). The RX state
 *  machine pushes one word per falling edge of its input pin. The TX state machine counts X down once per
 *  microsecond while enabled. Its TX FIFO takes pairs of edge times (ISR, OSR); a pair written in the
 *  microsecond w is compared from w + 2 on (worst case of the real program), at most one pair per microsecond. When X reaches
 *  ISR, the pin toggles, X is pushed as confirmation and OSR becomes the next edge time. An edge time that X
 *  already passed would wait for the 32 bit wrap of X (71 minutes): the simulation throws. A GPIO with a PIO
 *  function (pio_gpio_init, gpio_set_function) is driven by the state machine only, gpio_put() then changes
 *  just the SIO output latch.
 *
 *  Interrupt model as the NVIC of the Cortex-M33: an interrupt runs if interrupts are enabled
 *  and its priority is higher (lower value) than the one of the running handler, so a higher
 *  priority interrupt interrupts a running handler, equal priorities never nest. Pending
 *  interrupts run as soon as possible, the highest priority first, at equal priority the lower
 *  IRQ number. Interrupt sources are level sensitive (an alarm with INTR and INTE or INTF, a
 *  latched GPIO edge): a handler must clear its source. The NVIC keeps an interrupt pending once
 *  its source was active, also if the source is cleared before the handler runs (irq_clear()
 *  drops it), so a handler can run for an old event. Time only advances in
 *  sim::advance()/runUntil() and in busy waits (handlers take no time otherwise). An optional
 *  interrupt latency delays alarm interrupts; hardware events go on meanwhile (order 052).
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#ifndef SBLIB_TEST_RP2350_PICO_SIM_H_
#define SBLIB_TEST_RP2350_PICO_SIM_H_

#include <cstdint>
#include <functional>
#include <vector>

// ---- pico/types.h, pico/platform.h ----
typedef unsigned int uint;
typedef uint64_t absolute_time_t;

#define SYS_CLK_HZ 150000000u
#define XIP_BASE 0x10000000u
#define NUM_BANK0_GPIOS 48u

// One iteration of a polling loop (waitForInterrupt() in Bus::pause()): counts like a time read
// for the busy wait model (sim::setBusyWaitAdvance), so the loop terminates
void __nop();
void __wfi();
inline void __dsb()
{}
inline void __isb()
{}
uint __get_current_exception();

#define VTABLE_FIRST_IRQ 16u

// ---- hardware/irq.h ----
typedef enum irq_num_rp2350
{
    TIMER0_IRQ_0 = 0,
    TIMER0_IRQ_1 = 1,
    TIMER0_IRQ_2 = 2,
    TIMER0_IRQ_3 = 3,
    TIMER1_IRQ_0 = 4,
    TIMER1_IRQ_1 = 5,
    TIMER1_IRQ_2 = 6,
    TIMER1_IRQ_3 = 7,
    DMA_IRQ_0 = 10,
    IO_IRQ_BANK0 = 21,
    IRQ_COUNT = 52
} irq_num_t;

typedef void (*irq_handler_t)(void);

void irq_set_enabled(uint num, bool enabled);
bool irq_is_enabled(uint num);
void irq_set_priority(uint num, uint8_t hardware_priority);
void irq_clear(uint num);
void irq_set_pending(uint num);
void irq_set_exclusive_handler(uint num, irq_handler_t handler);
void irq_remove_handler(uint num, irq_handler_t handler);

// ---- hardware/sync.h ----
uint32_t save_and_disable_interrupts();
void restore_interrupts(uint32_t status);
void disable_interrupts();
void enable_interrupts();

// ---- hardware/gpio.h ----
enum gpio_irq_level
{
    GPIO_IRQ_LEVEL_LOW = 0x1u,
    GPIO_IRQ_LEVEL_HIGH = 0x2u,
    GPIO_IRQ_EDGE_FALL = 0x4u,
    GPIO_IRQ_EDGE_RISE = 0x8u,
};

enum gpio_function_rp2350
{
    GPIO_FUNC_SPI = 1,
    GPIO_FUNC_UART = 2,
    GPIO_FUNC_I2C = 3,
    GPIO_FUNC_SIO = 5,
    GPIO_FUNC_PIO0 = 6,
    GPIO_FUNC_NULL = 0x1f
};
typedef enum gpio_function_rp2350 gpio_function_t;

void gpio_set_dir(uint gpio, bool output);
void gpio_set_pulls(uint gpio, bool up, bool down);
void gpio_disable_pulls(uint gpio);
void gpio_set_input_enabled(uint gpio, bool enabled);
uint32_t time_us_32();
uint32_t gpio_get_all();
void gpio_set_function(uint gpio, gpio_function_t fn);
void gpio_put(uint gpio, bool value);
bool gpio_get(uint gpio);
void gpio_set_mask64(uint64_t mask);
void gpio_clr_mask64(uint64_t mask);
uint32_t gpio_get_irq_event_mask(uint gpio);
void gpio_acknowledge_irq(uint gpio, uint32_t event_mask);
void gpio_set_irq_enabled(uint gpio, uint32_t event_mask, bool enabled);
void gpio_add_raw_irq_handler(uint gpio, irq_handler_t handler);
void gpio_remove_raw_irq_handler(uint gpio, irq_handler_t handler);

// ---- hardware/timer.h ----
#define NUM_GENERIC_TIMERS 2u
#define NUM_ALARMS 4u

// TIMERAWL: reads the simulated time (optionally advancing it in busy wait loops, see sim::setBusyWaitAdvance)
struct sim_timerawl_t
{
    operator uint32_t() const volatile;
};

/**
 * Alarm registers of TIMER0 (RP2350 datasheet, TIMER): writing ALARMx arms the alarm, it fires when
 * TIMERAWL reaches the value (a value already passed fires only after the 32 bit wrap), sets INTR and
 * disarms; ARMED reads the armed alarms, writing 1 disarms; INTR is cleared by writing 1; the alarm
 * interrupt is (INTR & INTE) | INTF. The simulation has NUM_GENERIC_TIMERS * NUM_ALARMS alarms in this
 * one block (the RP2350 has 4 per timer; timer.cpp claims its alarms from TIMER0).
 */
struct sim_timer_reg_t
{
    enum Kind : uint8_t
    {
        Alarm,
        Armed,
        Intr,
        Inte,
        Intf,
        Ints
    };
    Kind kind;
    uint8_t index; // alarm number of ALARMx

    sim_timer_reg_t& operator=(uint32_t value);
    operator uint32_t() const;
};

struct sim_timer_hw_t
{
    sim_timerawl_t timerawl;
    sim_timer_reg_t alarm[NUM_GENERIC_TIMERS * NUM_ALARMS] = {{sim_timer_reg_t::Alarm, 0}, {sim_timer_reg_t::Alarm, 1},
                                                              {sim_timer_reg_t::Alarm, 2}, {sim_timer_reg_t::Alarm, 3},
                                                              {sim_timer_reg_t::Alarm, 4}, {sim_timer_reg_t::Alarm, 5},
                                                              {sim_timer_reg_t::Alarm, 6}, {sim_timer_reg_t::Alarm, 7}};
    sim_timer_reg_t armed = {sim_timer_reg_t::Armed, 0};
    sim_timer_reg_t intr = {sim_timer_reg_t::Intr, 0};
    sim_timer_reg_t inte = {sim_timer_reg_t::Inte, 0};
    sim_timer_reg_t intf = {sim_timer_reg_t::Intf, 0};
    sim_timer_reg_t ints = {sim_timer_reg_t::Ints, 0};
};
extern sim_timer_hw_t sim_timer_hw;
#define timer_hw (&sim_timer_hw)

// hardware/address_mapped.h: atomic set/clear of register bits
void hw_set_bits(sim_timer_reg_t* reg, uint32_t mask);
void hw_clear_bits(sim_timer_reg_t* reg, uint32_t mask);

#define TIMER_ALARM_NUM_FROM_IRQ(irq_num) ((irq_num) - TIMER0_IRQ_0)

uint64_t time_us_64();
int hardware_alarm_claim_unused(bool required);
void hardware_alarm_unclaim(uint alarm_num);
uint hardware_alarm_get_irq_num(uint alarm_num);

// ---- hardware/clocks.h ----
enum clock_num_rp2350
{
    clk_sys = 5
};
typedef enum clock_num_rp2350 clock_handle_t;
uint32_t clock_get_hz(clock_handle_t clock);

// ---- hardware/pio.h ----
// Addresses of the FIFO registers (DMA source/destination); the contents are held by the simulation
struct sim_pio_hw_t
{
    uint32_t txf[4];
    uint32_t rxf[4];
};
extern sim_pio_hw_t sim_pio0_hw;
typedef sim_pio_hw_t* PIO;
#define pio0 (&sim_pio0_hw)
#define NUM_PIO_STATE_MACHINES 4u
#define PIO_INSTRUCTION_COUNT 32u

typedef struct pio_program
{
    const uint16_t* instructions;
    uint8_t length;
    int8_t origin;
    uint8_t pio_version;
} pio_program_t;

enum pio_mov_status_type
{
    STATUS_TX_LESSTHAN = 0,
    STATUS_RX_LESSTHAN = 1
};

// State machine configuration: the fields the simulation uses, not the register layout
typedef struct
{
    uint wrapTarget;
    uint wrap;
    uint inBase;
    uint inCount;
    uint outBase;
    uint outCount;
    uint setBase;
    uint setCount;
    uint jmpPin;
    uint16_t clkdivInt;
    uint8_t clkdivFrac;
    enum pio_mov_status_type statusSel;
    uint statusN;
} pio_sm_config;

int pio_claim_unused_sm(PIO pio, bool required);
void pio_sm_unclaim(PIO pio, uint sm);
int pio_add_program(PIO pio, const pio_program_t* program);
void pio_remove_program(PIO pio, const pio_program_t* program, uint loaded_offset);
pio_sm_config pio_get_default_sm_config();
void sm_config_set_wrap(pio_sm_config* c, uint wrap_target, uint wrap);
void sm_config_set_in_pins(pio_sm_config* c, uint in_base);
void sm_config_set_in_pin_count(pio_sm_config* c, uint in_count);
void sm_config_set_out_pins(pio_sm_config* c, uint out_base, uint out_count);
void sm_config_set_set_pins(pio_sm_config* c, uint set_base, uint set_count);
void sm_config_set_jmp_pin(pio_sm_config* c, uint pin);
void sm_config_set_clkdiv_int_frac(pio_sm_config* c, uint16_t div_int, uint8_t div_frac8);
void sm_config_set_mov_status(pio_sm_config* c, enum pio_mov_status_type status_sel, uint status_n);
int pio_sm_init(PIO pio, uint sm, uint initial_pc, const pio_sm_config* config);
void pio_sm_set_enabled(PIO pio, uint sm, bool enabled);
void pio_enable_sm_mask_in_sync(PIO pio, uint32_t mask);
int pio_sm_set_consecutive_pindirs(PIO pio, uint sm, uint pins_base, uint pin_count, bool is_out);
void pio_gpio_init(PIO pio, uint pin);
uint pio_get_dreq(PIO pio, uint sm, bool is_tx);
void pio_sm_exec(PIO pio, uint sm, uint instr);
void pio_sm_put(PIO pio, uint sm, uint32_t data);
uint32_t pio_sm_get(PIO pio, uint sm);
void pio_sm_restart(PIO pio, uint sm);
bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm);
bool pio_sm_is_tx_fifo_full(PIO pio, uint sm);
uint pio_sm_get_tx_fifo_level(PIO pio, uint sm);
void pio_sm_clear_fifos(PIO pio, uint sm);

// ---- hardware/dma.h ----
#define NUM_DMA_CHANNELS 16u
#define DREQ_PIO0_TX0 0u
#define DREQ_PIO0_RX0 4u
#define DREQ_FORCE 63u

typedef enum dma_channel_transfer_size
{
    DMA_SIZE_8 = 0,
    DMA_SIZE_16 = 1,
    DMA_SIZE_32 = 2
} dma_channel_transfer_size_t;

typedef struct
{
    dma_channel_transfer_size_t size;
    bool readIncrement;
    bool writeIncrement;
    uint dreq;
    uint chainTo;
    bool highPriority;
} dma_channel_config;

int dma_claim_unused_channel(bool required);
void dma_channel_unclaim(uint channel);
dma_channel_config dma_channel_get_default_config(uint channel);
dma_channel_config dma_get_channel_config(uint channel);
void channel_config_set_transfer_data_size(dma_channel_config* c, dma_channel_transfer_size_t size);
void channel_config_set_read_increment(dma_channel_config* c, bool incr);
void channel_config_set_write_increment(dma_channel_config* c, bool incr);
void channel_config_set_dreq(dma_channel_config* c, uint dreq);
void channel_config_set_chain_to(dma_channel_config* c, uint chain_to);
void channel_config_set_high_priority(dma_channel_config* c, bool high_priority);
void dma_channel_configure(uint channel, const dma_channel_config* config, volatile void* write_addr,
                           const volatile void* read_addr, uint transfer_count, bool trigger);
void dma_channel_set_config(uint channel, const dma_channel_config* config, bool trigger);
void dma_channel_start(uint channel);
void dma_channel_cleanup(uint channel);
void dma_channel_set_irq0_enabled(uint channel, bool enabled);
void dma_channel_acknowledge_irq0(uint channel);

// ---- simulation control for the tests ----
namespace sim
{

    struct PinConfig
    {
        gpio_function_t function = GPIO_FUNC_NULL;
        bool output = false;
        bool pullUp = false;
        bool pullDown = false;
        bool inputEnabled = false;
        unsigned calls = 0;
    };
    const PinConfig& pinConfig(uint gpio);
    uint32_t pinIrqEvents(uint gpio);
    unsigned gpioCalls();

    // Test boundaries in the real Bus::pause() calls, not a replacement pause loop.
    enum class InterruptBoundary : uint8_t
    {
        BeforeDisable,
        Disabled,
        BeforeEnable,
        BeforeWait
    };
    void setInterruptBoundaryHook(std::function<void(InterruptBoundary)> hook);
    // WFI is a hint: exercise a finite number of returns without any pending interrupt.
    void setWfiSpuriousReturns(unsigned count);

    struct PinEvent
    {
        uint64_t timeUs;
        bool level;
    };

    // Reset the simulation: time = startUs, no alarms claimed, all GPIOs LOW, no handlers.
    void reset(uint64_t startUs = 1000000);
    uint64_t now();
    // Run the simulated time forward by us microseconds, executing due interrupts.
    void advance(uint64_t us);
    // Run the simulated time forward to the absolute time endUs (>= now()).
    void runUntil(uint64_t endUs);
    // Delay of alarm interrupts after their alarm time (e.g. interrupt entry from flash, a blocked core).
    // Hardware events (bus, PIO) and other interrupts go on meanwhile.
    void setAlarmLatency(uint32_t us);
    // A falling edge on a GPIO now (latched, interrupt if enabled)
    void fallingEdge(uint gpio);
    // Output level changes of a GPIO written with gpio_put()
    const std::vector<PinEvent>& pinLog(uint gpio);
    bool pinLevel(uint gpio);
    // Number of alarm interrupt handler calls since reset()
    unsigned alarmIrqCount();
    // Alarm armed (ARMED)
    bool alarmArmed(uint alarm_num);
    // Interrupts globally enabled (not inside save_and_disable_interrupts...)
    bool interruptsEnabled();
    bool insideInterrupt();
    // Hardware priority last set with irq_set_priority()
    uint8_t irqPriority(uint num);

    /**
     * Busy wait model: if enabled, every 16th read of the time (TIMERAWL, time_us_64) without the
     * time having advanced moves the time on by 1 us, so polling loops in the code under test
     * terminate. Interrupts that become due meanwhile run (or are pending inside a handler).
     */
    void setBusyWaitAdvance(bool enabled);

    // Run fn at the simulated time atUs (>= now), in main context between interrupts.
    void at(uint64_t atUs, std::function<void()> fn);

    /**
     * Run fn at the simulated time atUs (>= now) like a hardware event: in time order with the
     * alarms, also while the code under test busy waits. For models of other bus devices.
     */
    void atHardware(uint64_t atUs, std::function<void()> fn);

    // Call fn(level) whenever gpio_put() changes the level of gpio (e.g. a model of another bus device)
    void onPinChange(uint gpio, std::function<void(bool level)> fn);

    /**
     * KNX TP1 bus model. txPin HIGH pulls the bus low (0 bit) after echoDelayUs; rxPin reads
     * the bus (HIGH = idle / 1, LOW = 0 bit) and latches a falling edge. Other senders pull the
     * bus low with remoteLow().
     */
    void connectBus(uint txPin, uint rxPin, uint32_t echoDelayUs);
    // Another device pulls the bus low from startUs for durationUs
    void remoteLow(uint64_t startUs, uint32_t durationUs);
    // Times when the bus went low / high (all senders)
    const std::vector<PinEvent>& busLog();

    // PIO0 and DMA (SBLIB_RP2350_PIO_BUS)
    bool pioSmClaimed(uint sm);
    bool pioSmEnabled(uint sm);
    unsigned pioProgramWordsUsed();
    bool dmaChannelClaimed(uint channel);
    // System clock returned by clock_get_hz(clk_sys)
    void setSysClockHz(uint32_t hz);

} // namespace sim

#endif /* SBLIB_TEST_RP2350_PICO_SIM_H_ */
