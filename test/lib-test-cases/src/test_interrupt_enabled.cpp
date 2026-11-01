/*
 * Tests for the interrupt enable status query.
 */
#include <sblib/interrupt.h>
#include <cstdint>
#include <catch.hpp>

namespace
{
    class NvicEnableState
    {
    public:
        NvicEnableState() :
            savedSetEnable(NVIC->ISER[0]),
            savedClearEnable(NVIC->ICER[0])
        {
        }

        ~NvicEnableState()
        {
            NVIC->ISER[0] = savedSetEnable;
            NVIC->ICER[0] = savedClearEnable;
        }

        void set(const uint32_t mask)
        {
            // Both registers read the same enabled-bit state on the LPC11xx.
            // The CPU emulator stores them as independent ordinary RAM words.
            NVIC->ISER[0] = mask;
            NVIC->ICER[0] = mask;
        }

    private:
        const uint32_t savedSetEnable;
        const uint32_t savedClearEnable;
    };
}

TEST_CASE("Interrupt enable status ignores other enabled interrupts", "[interrupt-enabled]")
{
    NvicEnableState state;
    for (uint32_t target = 0; target < 32; ++target)
    {
        const auto irq = static_cast<IRQn_Type>(target);
        const uint32_t targetBit = uint32_t{1} << target;
        INFO("target IRQ=" << target);

        state.set(0);
        CHECK_FALSE(getInterruptEnabled(irq));
        state.set(targetBit);
        CHECK(getInterruptEnabled(irq));
        state.set(UINT32_MAX);
        CHECK(getInterruptEnabled(irq));

        for (uint32_t peer = 0; peer < 32; ++peer)
        {
            const uint32_t peerBit = uint32_t{1} << peer;
            INFO("peer IRQ=" << peer);
            state.set(peerBit);
            CHECK(getInterruptEnabled(irq) == (target == peer));
            state.set(targetBit | peerBit);
            CHECK(getInterruptEnabled(irq));
        }
    }
}
