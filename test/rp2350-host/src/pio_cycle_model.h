/*
 *  pio_cycle_model.h - Cycle accurate model of one PIO state machine for the host tests of the PIO
 *                      programs of the timer emulation (SBLIB_RP2350_PIO_BUS).
 *
 *  Executes the instructions the programs use, as described in the RP2350 datasheet (chapter PIO):
 *  JMP (all conditions), WAIT PIN, PUSH/PULL (block and noblock, PULL noblock of an empty FIFO copies X),
 *  MOV (destinations PINS, X, Y, ISR, OSR; operations none and invert; sources PINS, X, Y, NULL, STATUS,
 *  ISR, OSR), SET PINS/X/Y and the delay field (no side set). One call of step() is one cycle of the state
 *  machine. A stalled instruction is repeated without delay; after the instruction at the wrap address the
 *  program continues at the wrap target unless a jump was taken. One input pin and one output pin (the input
 *  can be the output pin itself, without the 2 cycles of input synchronisation).
 *  Other instructions throw: the programs must not use them.
 *
 *  @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#ifndef SBLIB_TEST_RP2350_PIO_CYCLE_MODEL_H_
#define SBLIB_TEST_RP2350_PIO_CYCLE_MODEL_H_

#include <cstdint>
#include <deque>
#include <stdexcept>
#include <vector>

namespace piomodel
{

    class StateMachine
    {
    public:
        static constexpr unsigned FifoDepth = 4;

        StateMachine(const uint16_t* program, const unsigned length, const unsigned wrapTarget, const unsigned wrap,
                     const unsigned startPc) :
            memory_(program, program + length),
            wrapTarget_(wrapTarget),
            wrap_(wrap),
            pc_(startPc)
        {}

        // EXECCTRL STATUS: TX FIFO level below statusN (all ones) or not (0)
        void statusTxLessThan(const unsigned statusN) { statusN_ = statusN; }

        // One cycle of the state machine
        void step()
        {
            ++cycle_;
            if (delay_ > 0)
            {
                --delay_;
                return;
            }
            const uint16_t instr = memory_.at(pc_);
            bool jumped = false;
            if (!execute(instr, jumped))
            {
                return; // stalled: the same instruction again in the next cycle, no delay
            }
            delay_ = (instr >> 8) & 0x1fu;
            if (!jumped)
            {
                if (pc_ == wrap_)
                {
                    pc_ = wrapTarget_;
                }
                else
                {
                    ++pc_;
                }
            }
        }

        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t isr = 0;
        uint32_t osr = 0;
        bool inputPin = false;      // level of the input pin (IN base, JMP pin)
        bool outputPin = false;     // level of the output pin (OUT and SET base)
        bool inputIsOutput = false; // IN base is the output pin (reads its level)
        std::deque<uint32_t> txFifo;
        std::deque<uint32_t> rxFifo;
        std::vector<uint64_t> outputChanges; // cycles in which the output pin changed

        [[nodiscard]] unsigned pc() const { return pc_; }
        [[nodiscard]] uint64_t cycle() const { return cycle_; }

    private:
        uint32_t movSource(const unsigned src) const
        {
            switch (src)
            {
                case 0:
                    if (inputIsOutput)
                    {
                        return static_cast<uint32_t>(outputPin);
                    }
                    return static_cast<uint32_t>(inputPin);
                case 1:
                    return x;
                case 2:
                    return y;
                case 3:
                    return 0;
                case 5:
                    if (txFifo.size() < statusN_)
                    {
                        return 0xffffffffu;
                    }
                    return 0;
                case 6:
                    return isr;
                case 7:
                    return osr;
                default:
                    throw std::logic_error("pio model: MOV source not modelled");
            }
        }

        void writeOutput(const bool level)
        {
            if (level != outputPin)
            {
                outputChanges.push_back(cycle_);
            }
            outputPin = level;
        }

        // Returns false if the instruction stalls
        bool execute(const uint16_t instr, bool& jumped)
        {
            const unsigned op = instr >> 13;
            const unsigned arg1 = (instr >> 5) & 0x7u;
            const unsigned arg2 = instr & 0x1fu;
            switch (op)
            {
                case 0: // JMP
                {
                    bool take = false;
                    switch (arg1)
                    {
                        case 0:
                            take = true;
                            break;
                        case 1:
                            take = (x == 0);
                            break;
                        case 2:
                            take = (x != 0);
                            --x;
                            break;
                        case 3:
                            take = (y == 0);
                            break;
                        case 4:
                            take = (y != 0);
                            --y;
                            break;
                        case 5:
                            take = (x != y);
                            break;
                        case 6:
                            take = inputPin;
                            break;
                        default:
                            throw std::logic_error("pio model: JMP !OSRE not modelled");
                    }
                    if (take)
                    {
                        pc_ = arg2;
                        jumped = true;
                    }
                    return true;
                }
                case 1: // WAIT
                {
                    const bool polarity = (arg1 & 0x4u) != 0;
                    if ((arg1 & 0x3u) != 1 || arg2 != 0)
                    {
                        throw std::logic_error("pio model: only WAIT PIN 0 modelled");
                    }
                    return inputPin == polarity;
                }
                case 4: // PUSH, PULL
                {
                    const bool block = (instr & 0x20u) != 0;
                    if (instr & 0x40u)
                    {
                        throw std::logic_error("pio model: IFFULL/IFEMPTY not modelled");
                    }
                    if ((instr & 0x80u) == 0) // PUSH
                    {
                        if (rxFifo.size() >= FifoDepth)
                        {
                            if (block)
                            {
                                return false;
                            }
                        }
                        else
                        {
                            rxFifo.push_back(isr);
                        }
                        isr = 0;
                        return true;
                    }
                    if (txFifo.empty()) // PULL
                    {
                        if (block)
                        {
                            return false;
                        }
                        osr = x;
                        return true;
                    }
                    osr = txFifo.front();
                    txFifo.pop_front();
                    return true;
                }
                case 5: // MOV
                {
                    const unsigned operation = (instr >> 3) & 0x3u;
                    const unsigned src = instr & 0x7u;
                    uint32_t value = movSource(src);
                    if (operation == 1)
                    {
                        value = ~value;
                    }
                    else if (operation != 0)
                    {
                        throw std::logic_error("pio model: MOV bit reverse not modelled");
                    }
                    switch (arg1)
                    {
                        case 0:
                            writeOutput((value & 1u) != 0);
                            break;
                        case 1:
                            x = value;
                            break;
                        case 2:
                            y = value;
                            break;
                        case 6:
                            isr = value;
                            break;
                        case 7:
                            osr = value;
                            break;
                        default:
                            throw std::logic_error("pio model: MOV destination not modelled");
                    }
                    return true;
                }
                case 7: // SET
                    switch (arg1)
                    {
                        case 0:
                            writeOutput((arg2 & 1u) != 0);
                            break;
                        case 1:
                            x = arg2;
                            break;
                        case 2:
                            y = arg2;
                            break;
                        default:
                            throw std::logic_error("pio model: SET destination not modelled");
                    }
                    return true;
                default:
                    throw std::logic_error("pio model: instruction not modelled");
            }
        }

        std::vector<uint16_t> memory_;
        unsigned wrapTarget_;
        unsigned wrap_;
        unsigned pc_;
        unsigned statusN_ = 1;
        unsigned delay_ = 0;
        uint64_t cycle_ = 0;
    };

} // namespace piomodel

#endif /* SBLIB_TEST_RP2350_PIO_CYCLE_MODEL_H_ */
