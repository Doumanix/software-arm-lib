/*
 *  test_bus_t9.cpp - More host tests of the KNX TP1 bus (sblib src/eib/bus.cpp, unchanged) on the
 *                    simulated bus line: collision and arbitration with a second
 *                    sender, reception with bit jitter and spikes, BUSY and NACK acknowledge, a
 *                    download-like load, pause()/resume().
 *
 *  The other device on the bus is the Peer model. It watches our TX pin (sim::onPinChange), decodes
 *  our frames and acknowledges them (ACK, NACK, BUSY or nothing), can start its own frame together
 *  with ours (collision), and can send a series of telegrams like a download with repetitions when
 *  our acknowledge is missing. All its actions are hardware events of the simulation, so it keeps
 *  working while the code under test busy waits (Bus::pause()).
 *
 *  Expectations cite the KNX specification as quoted in bus.cpp/bus_const.h, or bus.cpp itself
 *  (line numbers of sblib/src/eib/bus.cpp, inc/sblib/eib/bus_const.h at the time of writing).
 *  Observed deviations are tagged [!shouldfail] and described at the test case.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include "bus_test_support.h"

#include <sblib/eib/bus.h>
#include <sblib/eib/bus_const.h>
#include <sblib/io_pin_names.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <hardware/gpio.h> // simulation: sim::

#include <catch.hpp> // If possible, include catch.hpp as last header

using namespace bustest;

namespace
{

    constexpr uint64_t RemoteCharUs = CharBits * BitTime; // 1352 us per character of a remote sender
    constexpr uint64_t StopEndUs = 11 * BitTime;          // start bit to end of stop bit
    constexpr uint64_t FrameGapUs = CharTimeUs + 120;     // no TX pulse for this long: our frame has ended
    constexpr int64_t AckSlotToleranceUs = 150;           // our acknowledge must start within AckDelayUs +- this

    uint64_t endOfStop(const uint64_t charStartUs)
    {
        return charStartUs + StopEndUs;
    }

    // Offset in us of bit k (0 start bit, 1..8 data bits, 9 parity bit) of remote character n
    using BitOffset = std::function<int(size_t n, unsigned k)>;
    const BitOffset noOffset = [](size_t, unsigned) { return 0; };
    // BitOffset result: do not send this 0 bit (the test sends its own pulses instead)
    constexpr int OmitBit = -100000;

    // Positions (0 start bit .. 9 parity bit) of the 0 bits of a character
    std::vector<unsigned> zeroBits(const uint8_t value)
    {
        std::vector<unsigned> zeros = {0};
        int ones = 0;
        for (unsigned bit = 0; bit < 8; ++bit)
        {
            if (value & (1u << bit))
            {
                ++ones;
            }
            else
            {
                zeros.push_back(bit + 1);
            }
        }
        if (ones % 2 == 0) // even parity: parity bit 0
        {
            zeros.push_back(9);
        }
        return zeros;
    }

    // Another device sends the bytes with shifted bit edges; character n nominally at startUs + n * 1352 us
    void remoteSendTimed(const uint64_t startUs, const std::vector<uint8_t>& bytes, const BitOffset& offset,
                         const unsigned pulseUs = PulseTime)
    {
        for (size_t n = 0; n < bytes.size(); ++n)
        {
            const int64_t charStart = static_cast<int64_t>(startUs + n * RemoteCharUs);
            for (const unsigned k: zeroBits(bytes[n]))
            {
                const int o = offset(n, k);
                if (o != OmitBit)
                {
                    sim::remoteLow(static_cast<uint64_t>(charStart + static_cast<int64_t>(k * BitTime) + o), pulseUs);
                }
            }
        }
    }

    std::string hex(const std::vector<uint8_t>& bytes)
    {
        std::string s;
        char buf[4];
        for (const auto b: bytes)
        {
            if (!s.empty())
            {
                s += ' ';
            }
            std::snprintf(buf, sizeof(buf), "%02X", b);
            s += buf;
        }
        return s;
    }

    // The frame with the repeat flag cleared (repeated frame) and the checksum corrected
    std::vector<uint8_t> repeated(std::vector<uint8_t> frame)
    {
        frame[0] &= ~SB_TEL_REPEAT_FLAG;
        frame.pop_back();
        return withChecksum(frame);
    }

    // Telegrams of another device (1.1.2), with checksum
    std::vector<uint8_t> remoteGroupWrite(const uint8_t value = 0)
    {
        return withChecksum({0xBC, 0x11, 0x02, 0x00, GroupAddress, 0xE1, 0x00, static_cast<uint8_t>(0x80 | value)});
    }

    std::vector<uint8_t> remoteConnect(const uint8_t control = 0xB0)
    {
        return withChecksum({control, 0x11, 0x02, OwnAddress >> 8, OwnAddress & 0xff, 0x60, 0x80});
    }

    // MemoryWrite of 4 bytes to us (connection oriented, sequence number seq), 15 bytes with checksum
    std::vector<uint8_t> memoryWrite(const unsigned seq, const uint16_t address)
    {
        return withChecksum({0xB0, 0x11, 0x02, OwnAddress >> 8, OwnAddress & 0xff, 0x67,
                             static_cast<uint8_t>(0x42 | ((seq & 15) << 2)), 0x84, static_cast<uint8_t>(address >> 8),
                             static_cast<uint8_t>(address), static_cast<uint8_t>(seq), static_cast<uint8_t>(seq * 7),
                             static_cast<uint8_t>(address), 0xA5});
    }

    // One of our frames as seen on the TX pin
    struct TxFrame
    {
        std::vector<SentChar> chars;
        bool collided = false;    // overlaps a frame of the peer: we lost the arbitration or both sent
        bool acknowledge = false; // one character in the acknowledge slot after a frame of the peer

        [[nodiscard]] uint64_t startUs() const { return chars.front().startUs; }
        [[nodiscard]] uint64_t endOfStopUs() const { return endOfStop(chars.back().startUs); }
        [[nodiscard]] bool isAcknowledge() const { return acknowledge; }
        [[nodiscard]] std::vector<uint8_t> bytes() const
        {
            std::vector<uint8_t> b;
            for (const auto& c: chars)
            {
                b.push_back(c.value);
            }
            return b;
        }
    };

    enum class Reply : int
    {
        Ack = SB_BUS_ACK,
        Nack = SB_BUS_NACK,
        Busy = SB_BUS_BUSY,
        NackBusy = SB_BUS_NACK_BUSY,
        None = -1,
    };

    // Model of another bus device, see the file comment
    class Peer
    {
    public:
        struct Frame
        {
            uint64_t startUs;
            std::vector<uint8_t> bytes;

            [[nodiscard]] uint64_t endOfStopUs() const
            {
                return endOfStop(startUs + (bytes.size() - 1) * RemoteCharUs);
            }
        };

        // Acknowledges of our data frames in order, afterwards always `otherwise`
        std::vector<Reply> replies;
        Reply otherwise = Reply::Ack;
        // Start these frames together with our next data frames (offset after our start bit)
        std::vector<std::vector<uint8_t>> collideWith;
        unsigned collideOffsetUs = 0;

        std::vector<TxFrame> ours; // our complete frames: data frames and acknowledges
        std::vector<Frame> sent;   // frames of the peer

        // Download: telegrams sent one after the other, each repeated up to 3 times without our ACK
        struct Download
        {
            std::vector<std::vector<uint8_t>> telegrams; // with checksum, repeat flag set
            std::vector<size_t> missFirstAck;            // indices: the peer does not hear our first ACK
            size_t current = 0;
            unsigned attempt = 0;
            std::vector<unsigned> attempts; // transmissions per telegram
            std::vector<bool> acknowledged;
            bool done = false;
            uint64_t doneUs = 0;
        } download;

        void attach()
        {
            sim::onPinChange(PIN_EIB_TX, [this](const bool level) { txChanged(level); });
        }

        void send(const uint64_t startUs, const std::vector<uint8_t>& bytes, const BitOffset& offset = noOffset)
        {
            remoteSendTimed(startUs, bytes, offset);
            sent.push_back({startUs, bytes});
        }

        void startDownload(const uint64_t startUs)
        {
            download.attempts.assign(download.telegrams.size(), 0);
            download.acknowledged.assign(download.telegrams.size(), false);
            transmit(startUs);
        }

        [[nodiscard]] std::vector<TxFrame> dataFrames() const
        {
            std::vector<TxFrame> result;
            for (const auto& f: ours)
            {
                if (!f.isAcknowledge())
                {
                    result.push_back(f);
                }
            }
            return result;
        }

        // Our acknowledge in the acknowledge slot after frame f of the peer, or nullptr
        [[nodiscard]] const TxFrame* acknowledgeOf(const Frame& f) const
        {
            for (const auto& o: ours)
            {
                const int64_t d = static_cast<int64_t>(o.startUs()) - static_cast<int64_t>(f.endOfStopUs());
                if (o.isAcknowledge() && d >= static_cast<int64_t>(AckDelayUs) - AckSlotToleranceUs &&
                    d <= static_cast<int64_t>(AckDelayUs) + AckSlotToleranceUs)
                {
                    return &o;
                }
            }
            return nullptr;
        }

    private:
        bool pulseOpen = false;
        uint64_t pulseStartUs = 0;
        std::vector<Pulse> pending; // pulses of our current frame
        size_t replyIndex = 0;

        [[nodiscard]] bool inAckSlot(const uint64_t t) const
        {
            for (const auto& f: sent)
            {
                const uint64_t slot = f.endOfStopUs() + AckDelayUs;
                if (t + 300 >= slot && t <= slot + 300)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool overlapsPeer(const uint64_t from, const uint64_t to) const
        {
            for (const auto& f: sent)
            {
                if (from <= f.endOfStopUs() && f.startUs <= to)
                {
                    return true;
                }
            }
            return false;
        }

        void txChanged(const bool level)
        {
            const uint64_t now = sim::now();
            if (level)
            {
                if (pending.empty() && !collideWith.empty() && !inAckSlot(now))
                {
                    send(now + collideOffsetUs, collideWith.front());
                    collideWith.erase(collideWith.begin());
                }
                pulseOpen = true;
                pulseStartUs = now;
                return;
            }
            if (!pulseOpen)
            {
                return;
            }
            pulseOpen = false;
            pending.push_back({pulseStartUs, static_cast<unsigned int>(now - pulseStartUs)});
            const size_t count = pending.size();
            sim::atHardware(std::max(pulseStartUs + FrameGapUs, now),
                            [this, count]()
                            {
                                if (pending.size() == count)
                                {
                                    frameEnded();
                                }
                            });
        }

        void frameEnded()
        {
            TxFrame f{decodePulses(pending)};
            pending.clear();
            f.collided = overlapsPeer(f.startUs(), f.endOfStopUs());
            f.acknowledge = f.chars.size() == 1 && inAckSlot(f.startUs());
            ours.push_back(f);
            if (f.collided || f.isAcknowledge())
            {
                return;
            }

            Reply r = otherwise;
            if (replyIndex < replies.size())
            {
                r = replies[replyIndex];
            }
            ++replyIndex;
            if (r != Reply::None)
            {
                send(f.endOfStopUs() + AckDelayUs, {static_cast<uint8_t>(r)});
            }
        }

        void transmit(const uint64_t startUs)
        {
            auto bytes = download.telegrams[download.current];
            if (download.attempt > 0)
            {
                bytes = repeated(bytes);
            }
            ++download.attempts[download.current];
            send(startUs, bytes);
            const size_t index = sent.size() - 1;
            sim::atHardware(sent.back().endOfStopUs() + AckDelayUs + 3000, [this, index]() { checkAck(index); });
        }

        void checkAck(const size_t index)
        {
            const Frame f = sent[index];
            const TxFrame* ack = acknowledgeOf(f);
            bool heard = ack != nullptr && ack->chars[0].value == SB_BUS_ACK;
            const auto& miss = download.missFirstAck;
            if (heard && download.attempt == 0 && std::find(miss.begin(), miss.end(), download.current) != miss.end())
            {
                heard = false;
            }

            // KNX: next frame or repetition after 50 bit times line idle after the acknowledge (slot)
            if (heard)
            {
                download.acknowledged[download.current] = true;
                next(ack->startUs() + StopEndUs + SEND_WAIT_TIME);
            }
            else if (download.attempt < NACK_RETRY_DEFAULT)
            {
                ++download.attempt;
                transmit(f.endOfStopUs() + AckDelayUs + StopEndUs + SEND_WAIT_TIME);
            }
            else
            {
                next(f.endOfStopUs() + AckDelayUs + StopEndUs + SEND_WAIT_TIME);
            }
        }

        void next(const uint64_t startUs)
        {
            download.attempt = 0;
            if (++download.current < download.telegrams.size())
            {
                transmit(startUs);
            }
            else
            {
                download.done = true;
                download.doneUs = startUs;
            }
        }
    };

    // The application: takes received telegrams in its main loop, needs processUs per telegram
    struct Consumer
    {
        unsigned processUs = 0;
        std::function<void()> onTelegram; // e.g. a flash write with bus pause, runs in the main loop
        std::vector<std::vector<uint8_t>> received;
        std::vector<uint64_t> receivedUs;
        bool holding = false;
        uint64_t busyUntilUs = 0;

        void poll()
        {
            if (holding && sim::now() >= busyUntilUs)
            {
                bus->discardReceivedTelegram();
                holding = false;
            }
            if (!holding && bus->telegramReceived())
            {
                received.emplace_back(bus->telegram, bus->telegram + bus->telegramLen);
                receivedUs.push_back(sim::now());
                if (onTelegram)
                {
                    onTelegram();
                }
                holding = true;
                busyUntilUs = sim::now() + processUs;
            }
        }
    };

    // Main loop: run the simulation for us microseconds, the consumer polls every stepUs
    void runMain(Consumer& consumer, const uint64_t us, const unsigned stepUs = 50)
    {
        const uint64_t end = sim::now() + us;
        while (sim::now() < end)
        {
            sim::advance(std::min<uint64_t>(stepUs, end - sim::now()));
            consumer.poll();
        }
    }

    void printFrames(const char* title, const Peer& peer)
    {
        std::printf("%s\n", title);
        struct Line
        {
            uint64_t t;
            std::string text;
        };
        std::vector<Line> lines;
        char buf[256];
        for (const auto& f: peer.ours)
        {
            int maxOffset = 0;
            for (const auto& c: f.chars)
            {
                maxOffset = std::max(maxOffset, c.maxBitOffsetUs);
            }
            const char* lost = "";
            if (f.collided)
            {
                lost = " lost";
            }
            std::snprintf(buf, sizeof(buf), "  ours%s  %s  (end of stop %llu, raster max %d us)", lost,
                          hex(f.bytes()).c_str(), static_cast<unsigned long long>(f.endOfStopUs()), maxOffset);
            lines.push_back({f.startUs(), buf});
        }
        for (const auto& f: peer.sent)
        {
            std::snprintf(buf, sizeof(buf), "  peer  %s  (end of stop %llu)", hex(f.bytes).c_str(),
                          static_cast<unsigned long long>(f.endOfStopUs()));
            lines.push_back({f.startUs, buf});
        }
        std::stable_sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.t < b.t; });
        uint64_t last = 0;
        for (const auto& l: lines)
        {
            uint64_t deltaUs = 0;
            if (last)
            {
                deltaUs = l.t - last;
            }
            std::printf("  t=%llu dt=%llu%s\n", static_cast<unsigned long long>(l.t),
                        static_cast<unsigned long long>(deltaUs), l.text.c_str());
            last = l.t;
        }
        std::printf("  finished=%d ok=%d\n", callback.finished, callback.lastSuccessful);
    }

    // Time of our first start bit when sending groupWrite() on a fresh bus (the simulation is deterministic)
    uint64_t firstStartBitUs()
    {
        startBus();
        send(groupWrite());
        sim::advance(20000);
        const auto chars = decodeTx();
        REQUIRE_FALSE(chars.empty());
        return chars.front().startUs;
    }

    // ------------------------------------------------------------------ collision

    /**
     * We send groupWrite(); the peer starts `remote` offsetUs after our start bit (negative: before),
     * acknowledges our frames afterwards. Fresh bus, peer attached.
     */
    void runCollision(Peer& peer, const std::vector<uint8_t>& remote, const int offsetUs, const uint64_t runUs = 150000)
    {
        const uint64_t t0 = firstStartBitUs();
        startBus();
        peer.attach();
        send(groupWrite());
        peer.send(static_cast<uint64_t>(static_cast<int64_t>(t0) + offsetUs), remote);
        sim::advance(runUs);
    }

    // Position (us after the start bit of the frame) of the first bit where the peer sends 0 and we send 1
    uint64_t collisionPointUs(const std::vector<uint8_t>& ours, const std::vector<uint8_t>& remote)
    {
        for (size_t n = 0; n < std::min(ours.size(), remote.size()); ++n)
        {
            const auto zo = zeroBits(ours[n]);
            for (const unsigned k: zeroBits(remote[n]))
            {
                if (std::find(zo.begin(), zo.end(), k) == zo.end())
                {
                    return n * CharTimeUs + k * BitTime;
                }
            }
        }
        return UINT64_MAX;
    }

    void checkCollision(Peer& peer, const std::vector<uint8_t>& remote, const std::vector<uint8_t>& expected,
                        const uint64_t remoteStartUs)
    {
        // The peer's frame is complete on the bus and was received by us
        REQUIRE(bus->telegramReceived());
        CHECK(std::vector<uint8_t>(bus->telegram, bus->telegram + bus->telegramLen) == remote);

        const auto data = peer.dataFrames();
        REQUIRE(data.size() >= 2);

        // 1st attempt: we stop sending at the collided bit (bus.cpp:1193-1199, KNX collision avoidance)
        const TxFrame& lost = data[0];
        CHECK(lost.collided);
        const uint64_t point = collisionPointUs(expected, remote);
        INFO("collision at " << point << " us after the start bit, our frame " << hex(lost.bytes()));
        for (const auto& c: lost.chars)
        {
            CHECK(c.startUs <= lost.startUs() + point);
        }
        const auto pulses = txPulses();
        for (const auto& p: pulses)
        {
            if (p.startUs > lost.startUs() + point + 20 && p.startUs < remoteStartUs + remote.size() * RemoteCharUs)
            {
                FAIL("TX pulse at " << p.startUs << " after the collision, during the frame of the peer");
            }
        }

        // We acknowledge the peer's frame 15 bit times after its end (bus.cpp:514-519)
        const Peer::Frame remoteFrame{remoteStartUs, remote};
        const TxFrame* ack = peer.acknowledgeOf(remoteFrame);
        REQUIRE(ack != nullptr);
        CHECK(ack->chars[0].value == SB_BUS_ACK);
        CHECK(std::llabs(static_cast<int64_t>(ack->startUs()) -
                         static_cast<int64_t>(remoteFrame.endOfStopUs() + AckDelayUs)) <= 5);

        // 2nd attempt: same frame, repeat flag unchanged (bus.cpp:1212-1223, KNX 3/2/2 2.3.1 and 2.4.1),
        // after at least 50 bit times line idle after our acknowledge
        const TxFrame& again = data[1];
        CHECK_FALSE(again.collided);
        CHECK(again.bytes() == expected);
        CHECK(again.startUs() >= ack->endOfStopUs() + 50 * BitTime);
        CHECK(callback.finished == 1);
        CHECK(callback.lastSuccessful);
    }

} // namespace

TEST_CASE("bus collision: the peer wins in the control field, we receive, acknowledge and repeat",
          "[rp2350][bus][collision]")
{
    // Peer 0xB0 (system priority) vs. our 0xBC (low priority): bit 2 of the control field.
    // S2 (orders 041/042): the former spike filter of the emulation compared timer values; the peer's
    // 0 bit right after our 0 bit had the same timer value (69 us) and was dropped, bus.cpp detected the
    // collision one bit late and received 0xB4 instead of 0xB0. Fixed with the filter on system
    // time, removed in order 050.
    const auto remote = remoteConnect(0xB0);
    for (const int offset: {-20, 0, 3})
    {
        DYNAMIC_SECTION("peer starts " << offset << " us after our start bit")
        {
            Peer peer;
            runCollision(peer, remote, offset);
            const auto expected =
                    withChecksum({0xBC, OwnAddress >> 8, OwnAddress & 0xff, 0x00, GroupAddress, 0xE1, 0x00, 0x81});
            checkCollision(peer, remote, expected, peer.sent[0].startUs);
            CHECK(peer.dataFrames().size() == 2);
        }
    }
}

TEST_CASE("bus collision: the peer wins later in the frame (source address, last data byte)",
          "[rp2350][bus][collision]")
{
    const auto expected =
            withChecksum({0xBC, OwnAddress >> 8, OwnAddress & 0xff, 0x00, GroupAddress, 0xE1, 0x00, 0x81});
    SECTION("byte 2 (source address 1.1.2 vs. 1.1.101), bit 0")
    {
        Peer peer;
        const auto remote = remoteGroupWrite(0);
        runCollision(peer, remote, 0);
        checkCollision(peer, remote, expected, peer.sent[0].startUs);
    }
    SECTION("byte 7 (data), bit 0: second device with our address 1.1.101")
    {
        Peer peer;
        const auto remote =
                withChecksum({0xBC, OwnAddress >> 8, OwnAddress & 0xff, 0x00, GroupAddress, 0xE1, 0x00, 0x80});
        runCollision(peer, remote, 0);
        checkCollision(peer, remote, expected, peer.sent[0].startUs);
    }
}

#if defined(SBLIB_RP2350_PIO_BUS)
TEST_CASE("bus collision PIO: start bit synced to a foreign edge before ours ends 35 us after that edge",
          "[rp2350][bus][collision][pio]")
{
    // bus.cpp:1098-1103: a foreign edge up to STARTBIT_OFFSET_MIN before our start bit -> MAT0 now,
    // MAT2 = capture + 34. As on the LPC11xx (PWM output LOW with the reset by MR2) our pulse ends at
    // capture + 35, also if it is then shorter than 35 us (order 062, finding 052 3.3: 1, 5.5, 9.2 us).
    const auto remote = remoteConnect(0xB0);
    for (const int offset: {-1, -5, -6, -9, -10, -20})
    {
        DYNAMIC_SECTION("peer starts " << -offset << " us before our start bit")
        {
            Peer peer;
            runCollision(peer, remote, offset);
            const uint64_t foreignUs = peer.sent[0].startUs;
            const auto pulses = txPulses();
            REQUIRE_FALSE(pulses.empty());
            const Pulse& first = pulses.front();
            INFO("foreign edge " << foreignUs << ", our pulse " << first.startUs << " + " << first.widthUs);
            CHECK(first.startUs > foreignUs);
            CHECK(first.startUs + first.widthUs == foreignUs + PulseTime);
            // The rest of our frame up to the collision and the repetition: 35 us pulses
            for (size_t i = 1; i < pulses.size(); ++i)
            {
                CHECK(pulses[i].widthUs == PulseTime);
            }
            const auto expected =
                    withChecksum({0xBC, OwnAddress >> 8, OwnAddress & 0xff, 0x00, GroupAddress, 0xE1, 0x00, 0x81});
            checkCollision(peer, remote, expected, peer.sent[0].startUs);
        }
    }
}
#endif

TEST_CASE("bus collision: a peer that starts more than 30 us before us is no collision, we defer",
          "[rp2350][bus][collision]")
{
    // bus.cpp:1062-1088: start bit captured earlier than STARTBIT_OFFSET_MIN before ours -> receive
    Peer peer;
    const auto remote = remoteConnect(0xB0);
    runCollision(peer, remote, -40);
    REQUIRE(bus->telegramReceived());
    const auto data = peer.dataFrames();
    REQUIRE(data.size() == 1);
    CHECK_FALSE(data[0].collided);
    CHECK(data[0].startUs() > peer.sent[0].endOfStopUs());
    CHECK(callback.lastSuccessful);
}

TEST_CASE("bus collision: after 4 lost arbitrations the telegram is given up", "[rp2350][bus][collision]")
{
    // bus.cpp:970-976 with COLLISION_RETRY_MAX = 3 (bus_const.h:94): collisions > 3 -> TX_RETRY_ERROR.
    // The peer wins in byte 2.
    startBus();
    Peer peer;
    peer.attach();
    for (int i = 0; i < 6; ++i)
    {
        peer.collideWith.push_back(remoteGroupWrite(0));
    }
    Consumer consumer;
    send(groupWrite());
    runMain(consumer, 400000);

    const auto data = peer.dataFrames();
    size_t attempts = 0;
    for (const auto& f: data)
    {
        CHECK(f.collided);
        CHECK(f.chars[0].value == 0xBC); // repeat flag never cleared by a collision
        ++attempts;
    }
    CHECK(attempts == COLLISION_RETRY_MAX + 1);
    CHECK(consumer.received.size() == attempts); // every frame of the peer was received
    CHECK(callback.finished == 1);
    CHECK_FALSE(callback.lastSuccessful);
}

TEST_CASE("bus collision: an acknowledge collides with the NACK of another receiver", "[rp2350][bus][collision]")
{
    // KNX: on the wire NACK (0x0C) wins over ACK (0xCC) at bit 6; ack_char is not repeated
    // (bus.cpp:1064-1067, 1205-1209). Afterwards the bus must work normally.
    startBus();
    Peer peer;
    peer.attach();
    const auto remote = remoteGroupWrite(1);
    const uint64_t t0 = sim::now() + 1000;
    peer.send(t0, remote);
    const Peer::Frame frame{t0, remote};
    peer.send(frame.endOfStopUs() + AckDelayUs, {SB_BUS_NACK});
    sim::advance(40000);

    REQUIRE(bus->telegramReceived());
    REQUIRE(peer.ours.size() == 1);
    const TxFrame& ack = peer.ours[0];
    CHECK(ack.collided);
    // our pulses: start bit and data bits 0, 1, 4, 5 of 0xCC; no pulse after bit 6 (no parity pulse)
    const auto pulses = txPulses();
    REQUIRE_FALSE(pulses.empty());
    CHECK(pulses.back().startUs < ack.startUs() + 7 * BitTime);
    CHECK(ack.chars[0].widths.size() == 5);

    // the bus still receives and acknowledges
    bus->discardReceivedTelegram();
    const uint64_t t1 = sim::now() + 1000;
    peer.send(t1, remoteGroupWrite(0));
    sim::advance(40000);
    CHECK(bus->telegramReceived());
    CHECK(peer.acknowledgeOf({t1, remoteGroupWrite(0)}) != nullptr);
}

// ------------------------------------------------------------------ jitter and spikes

namespace
{

    struct RxResult
    {
        bool received = false;
        std::vector<uint8_t> bytes;
        bool acknowledged = false;
        int64_t ackDelayUs = 0; // after the end of the stop bit of the last character as sent
        bool recovered = false; // a clean telegram afterwards is received and acknowledged
    };

    /**
     * The peer sends tel to us with shifted bit edges; extra() adds disturbances (spikes) relative to the
     * start t0. Afterwards a clean telegram checks that the bus is not stuck.
     */
    RxResult receiveWith(const std::vector<uint8_t>& tel, const BitOffset& offset,
                         const std::function<void(uint64_t t0)>& extra = {}, const unsigned pulseUs = PulseTime)
    {
        startBus();
        Peer peer;
        peer.attach();
        const uint64_t t0 = sim::now() + 1000;
        remoteSendTimed(t0, tel, offset, pulseUs);
        peer.sent.push_back({t0, tel});
        if (extra)
        {
            extra(t0);
        }
        sim::advance(40000);

        RxResult r;
        r.received = bus->telegramReceived();
        if (r.received)
        {
            r.bytes.assign(bus->telegram, bus->telegram + bus->telegramLen);
        }
        const uint64_t lastChar = t0 + (tel.size() - 1) * RemoteCharUs + offset(tel.size() - 1, 0);
        for (const auto& f: peer.ours)
        {
            if (f.isAcknowledge() && f.chars[0].value == SB_BUS_ACK)
            {
                r.acknowledged = true;
                r.ackDelayUs = static_cast<int64_t>(f.startUs()) - static_cast<int64_t>(endOfStop(lastChar));
            }
        }

        bus->discardReceivedTelegram();
        const auto clean = remoteGroupWrite(1);
        const uint64_t t1 = sim::now() + 1000;
        peer.send(t1, clean);
        sim::advance(40000);
        r.recovered = bus->telegramReceived() &&
                      std::vector<uint8_t>(bus->telegram, bus->telegram + bus->telegramLen) == clean &&
                      peer.acknowledgeOf({t1, clean}) != nullptr;
        return r;
    }

    const auto toUs = withChecksum({0xB0, 0x11, 0x02, OwnAddress >> 8, OwnAddress & 0xff, 0x60, 0x80});

    // All data and parity bits (not the start bits) shifted by d us
    BitOffset dataBits(const int d)
    {
        return [d](size_t, const unsigned k)
        {
            if (k == 0)
            {
                return 0;
            }
            return d;
        };
    }

    // Data and parity bits alternately early by e and late by l
    BitOffset alternating(const int e, const int l)
    {
        return [e, l](const size_t n, const unsigned k)
        {
            if (k == 0)
            {
                return 0;
            }
            if ((n + k) % 2)
            {
                return l;
            }
            return -e;
        };
    }

    // Character 1 (start bit and all bits) shifted by d: the pause after character 0 is longer by d
    BitOffset secondCharacter(const int d)
    {
        return [d](const size_t n, unsigned)
        {
            if (n == 1)
            {
                return d;
            }
            return 0;
        };
    }

    void checkReceived(const RxResult& r)
    {
        CHECK(r.received);
        CHECK(r.bytes == toUs);
        CHECK(r.acknowledged);
        CHECK(std::llabs(r.ackDelayUs - static_cast<int64_t>(AckDelayUs)) <= 5);
        CHECK(r.recovered);
    }

    void checkRejected(const RxResult& r)
    {
        CHECK_FALSE(r.received);
        CHECK_FALSE(r.acknowledged);
        CHECK(r.recovered);
    }

    /**
     * Read access to the private member Bus::rx_error: an explicit template instantiation may name a
     * private member, so the test reads the error flags without a change of bus.h.
     */
    using RxErrorMember = uint16_t Bus::*;
    RxErrorMember rxErrorMember();

    template<RxErrorMember Member>
    struct RxErrorAccess
    {
        friend RxErrorMember rxErrorMember() { return Member; }
    };

    template struct RxErrorAccess<&Bus::rx_error>;

    // All data and parity bits d us late, outside the KNX window: received, acknowledged, RX_TIMING_ERROR_SPIKE set
    void checkAcceptedLate(const int d)
    {
        uint16_t rxError = RX_OK;
        const auto sample = [&rxError](const uint64_t t0)
        {
            // bus.cpp:501 clears rx_error when it takes the telegram: read it after the stop bit of the
            // last character, before the end of the telegram (2 bit times + 30 us)
            const uint64_t lastChar = t0 + (toUs.size() - 1) * RemoteCharUs;
            sim::atHardware(endOfStop(lastChar) + 50, [&rxError]() { rxError = bus.get()->*rxErrorMember(); });
        };
        checkReceived(receiveWith(toUs, dataBits(d), sample));
        CHECK((rxError & RX_TIMING_ERROR_SPIKE) != 0);
    }

} // namespace

TEST_CASE("bus jitter: bit edges within the KNX window -7..+33 us are received and acknowledged",
          "[rp2350][bus][jitter]")
{
    // Window n*104 -7/+33 us: bus_const.h:37-38 (RX_TIMING_ERROR), 106-111, bus.cpp:831-833
    SECTION("all bits 33 us late")
    {
        checkReceived(receiveWith(toUs, dataBits(33)));
    }
    SECTION("all bits 7 us early")
    {
        checkReceived(receiveWith(toUs, dataBits(-7)));
    }
    SECTION("alternating 7 us early and 33 us late")
    {
        checkReceived(receiveWith(toUs, alternating(7, 33)));
    }
    SECTION("pulses 25 and 50 us wide") // KNX: 0 bit 35 us +-, bus.cpp only uses falling edges
    {
        checkReceived(receiveWith(toUs, noOffset, {}, 25));
        checkReceived(receiveWith(toUs, noOffset, {}, 50));
    }
}

TEST_CASE("bus jitter: bit edges up to 35 us early are still accepted (bus.cpp tolerance)", "[rp2350][bus][jitter]")
{
    // bus.cpp:836 "check window should be at least n*104-7us *** we use -35us to be more tolerant"
    checkReceived(receiveWith(toUs, dataBits(-20)));
    checkReceived(receiveWith(toUs, dataBits(-35)));
}

TEST_CASE("bus jitter: bit edges far outside the window are rejected, the bus recovers", "[rp2350][bus][jitter]")
{
    // >= 69 us late: the edge belongs to the next bit (bus.cpp:840), more than 35 us early: ignored
    // (bus.cpp:857-862); both give a wrong character, parity or checksum fails
    SECTION("all bits 70 us late")
    {
        checkRejected(receiveWith(toUs, dataBits(70)));
    }
    SECTION("all bits 40 us early")
    {
        checkRejected(receiveWith(toUs, dataBits(-40)));
    }
}

TEST_CASE("bus jitter: bit edges 34..68 us late are accepted (R13, tolerated like Darth's bus.cpp)",
          "[rp2350][bus][jitter]")
{
    // Accepted behaviour (R13, decision 25.10.2026, question to Darth open): the KNX window is
    // n*104 -7/+33 us (bus_const.h:37, 111), bus.cpp:848-852 only sets RX_TIMING_ERROR_SPIKE for an
    // edge later than BIT_OFFSET_MAX (33 us). rx_error is not part of `valid` (bus.cpp:783, 886), so
    // the telegram is taken and acknowledged; edges up to about +68 us still belong to their bit
    // (>= 69 us: see "far outside the window"). The LPC11xx build runs the same bus.cpp.
    SECTION("all bits 40 us late")
    {
        checkAcceptedLate(40);
    }
    SECTION("all bits 60 us late")
    {
        checkAcceptedLate(60);
    }
}

TEST_CASE("bus jitter: a longer pause between characters ends the telegram after 2 bit times + 30 us",
          "[rp2350][bus][jitter]")
{
    // MAX_INTER_CHAR_TIME = 2 bit times + STARTBIT_OFFSET_MAX (30 us) after the stop bit
    // (bus_const.h:108, 119-123); bus.cpp:896 sets the timeout to MAX_INTER_CHAR_TIME - 1, so a start
    // bit 30 us late already comes after the end of the telegram (bus_const.h:109: "<=30 us ... ok").
    SECTION("20 us longer: one telegram")
    {
        checkReceived(receiveWith(toUs, secondCharacter(20)));
    }
    SECTION("40 us longer: the telegram is split and rejected")
    {
        checkRejected(receiveWith(toUs, secondCharacter(40)));
    }
}

namespace
{

    // Short low pulses on the bus at t0 + atUs
    std::function<void(uint64_t)> spikes(const std::vector<std::pair<int64_t, unsigned>>& atAndWidth)
    {
        return [atAndWidth](const uint64_t t0)
        {
            for (const auto& s: atAndWidth)
            {
                sim::remoteLow(static_cast<uint64_t>(static_cast<int64_t>(t0) + s.first), s.second);
            }
        };
    }

    // A 0 bit of toUs in character 1: data bit 2 of 0x11 is 0 (position 3)
    constexpr int64_t ZeroBitUs = static_cast<int64_t>(RemoteCharUs) + 3 * BitTime;
    // A 1 bit of toUs in character 1: data bit 0 of 0x11 is 1 (position 1)
    constexpr int64_t OneBitUs = static_cast<int64_t>(RemoteCharUs) + 1 * BitTime;

} // namespace

TEST_CASE("bus spikes: short pulses on the idle bus do not disturb the next telegram", "[rp2350][bus][spike]")
{
    // bus.cpp:651-683 ignores a falling edge if the bus is high again within ZERO_BIT_MIN_TIME (3 us,
    // bus_const.h:106); a longer pulse starts the reception of a (wrong) character
    for (const unsigned width: {1u, 2u, 3u, 4u, 6u, 10u})
    {
        DYNAMIC_SECTION("spike " << width << " us, 3 ms before the telegram")
        {
            checkReceived(receiveWith(toUs, noOffset, spikes({{-3000, width}})));
        }
    }
}

TEST_CASE("bus spikes: inside a telegram", "[rp2350][bus][spike]")
{
    SECTION("2 us in the middle of a 1 bit: ignored (bus.cpp:665-669)")
    {
        checkReceived(receiveWith(toUs, noOffset, spikes({{OneBitUs + 50, 2}})));
    }
    SECTION("6 us in the middle of a 1 bit: a 0 bit, parity error, rejected")
    {
        checkRejected(receiveWith(toUs, noOffset, spikes({{OneBitUs + 50, 6}})));
    }
    for (const int64_t at: {5, 10, 20})
    {
        DYNAMIC_SECTION("bus high for 1 us inside a 0 bit pulse at +" << at << " us: ignored")
        {
            // 2nd falling edge: captured, but too early for the next bit, bus.cpp ignores it
            // (bus.cpp:857-862)
            const BitOffset omit = [](const size_t n, const unsigned k)
            {
                if (n == 1 && k == 3)
                {
                    return OmitBit;
                }
                return 0;
            };
            checkReceived(receiveWith(toUs, omit,
                                      [at](const uint64_t t0)
                                      {
                                          sim::remoteLow(static_cast<uint64_t>(static_cast<int64_t>(t0) + ZeroBitUs),
                                                         static_cast<unsigned int>(at));
                                          sim::remoteLow(
                                                  static_cast<uint64_t>(static_cast<int64_t>(t0) + ZeroBitUs + at + 1),
                                                  static_cast<unsigned int>(PulseTime - at - 1));
                                      }));
        }
    }
    SECTION("2 us spike ending 8 us before a 0 bit: both seen, the 0 bit is used")
    {
        checkReceived(receiveWith(toUs, noOffset, spikes({{ZeroBitUs - 10, 2}})));
    }
}

TEST_CASE("bus spikes: a spike shortly before a 0 bit must not hide the 0 bit", "[rp2350][bus][spike]")
{
    // S3 (orders 042/047/050): the former spike filter of the timer emulation dropped a falling edge
    // less than 8 us after the last accepted one. bus.cpp has rejected the spike (bus high again,
    // bus.cpp:665-669), but the real 0 bit edge was filtered out: the bit read as 1, parity error, the
    // telegram was lost. Like the LPC11xx the emulation now captures both edges.
    checkReceived(receiveWith(toUs, noOffset, spikes({{ZeroBitUs - 6, 2}})));
}

// ------------------------------------------------------------------ BUSY and NACK

namespace
{

    // We send groupWrite(), the peer acknowledges our data frames with replies (afterwards: nothing)
    std::vector<TxFrame> runReplies(Peer& peer, const std::vector<Reply>& replies, const uint64_t runUs = 600000)
    {
        startBus();
        peer.attach();
        peer.replies = replies;
        peer.otherwise = Reply::None;
        send(groupWrite());
        sim::advance(runUs);
        return peer.dataFrames();
    }

    // Time from the end of the stop bit of the peer's acknowledge to the start of our next data frame
    std::vector<int64_t> repetitionGaps(const Peer& peer, const std::vector<TxFrame>& data)
    {
        std::vector<int64_t> gaps;
        for (size_t i = 1; i < data.size(); ++i)
        {
            int64_t gap = -1;
            for (const auto& f: peer.sent)
            {
                if (f.startUs > data[i - 1].endOfStopUs() && f.startUs < data[i].startUs())
                {
                    gap = static_cast<int64_t>(data[i].startUs()) - static_cast<int64_t>(f.endOfStopUs());
                }
            }
            if (gap < 0) // no acknowledge: from the end of our previous frame
            {
                gap = static_cast<int64_t>(data[i].startUs()) - static_cast<int64_t>(data[i - 1].endOfStopUs());
            }
            gaps.push_back(gap);
        }
        return gaps;
    }

    const auto ourFrame =
            withChecksum({0xBC, OwnAddress >> 8, OwnAddress & 0xff, 0x00, GroupAddress, 0xE1, 0x00, 0x81});

    // Random extra delay of bus.cpp:1007-1010 (up to RANDOMIZE_MODULUS) plus the end of frame detection
    // (MAX_INTER_CHAR_TIME) plus a few us
    constexpr int64_t GapMarginUs = RANDOMIZE_MODULUS + MAX_INTER_CHAR_TIME + 10;
    // bus.cpp measures from its own end of stop bit detection: the last repetition (no random delay)
    // starts 2 us before the nominal 50 or 150 bit times after the peer's acknowledge
    constexpr int64_t GapEarlyUs = 5;

} // namespace

TEST_CASE("bus acknowledge: BUSY repeats after 150 bit times, then ACK finishes", "[rp2350][bus][busy]")
{
    // bus.cpp:540-546: BUSY (also NACK+BUSY 0x00, bus_const.h:27) -> wait BUSY_WAIT_150BIT
    // (bus_const.h:140-141), repeat with cleared repeat flag (bus.cpp:987-994)
    for (const Reply busy: {Reply::Busy, Reply::NackBusy})
    {
        DYNAMIC_SECTION("reply 0x" << std::hex << static_cast<int>(busy))
        {
            Peer peer;
            const auto data = runReplies(peer, {busy, Reply::Ack});
            REQUIRE(data.size() == 2);
            CHECK(data[0].bytes() == ourFrame);
            CHECK(data[1].bytes() == repeated(ourFrame));
            const auto gaps = repetitionGaps(peer, data);
            CHECK(gaps[0] >= BUSY_WAIT_150BIT - GapEarlyUs);
            CHECK(gaps[0] <= BUSY_WAIT_150BIT + GapMarginUs);
            CHECK(callback.finished == 1);
            CHECK(callback.lastSuccessful);
        }
    }
}

TEST_CASE("bus acknowledge: NACK repeats after 50 bit times, then ACK finishes", "[rp2350][bus][busy]")
{
    // bus.cpp:547-553: NACK -> repeat after SEND_WAIT_TIME (50 bit times, bus_const.h:136, bus.cpp:435)
    Peer peer;
    const auto data = runReplies(peer, {Reply::Nack, Reply::Ack});
    REQUIRE(data.size() == 2);
    CHECK(data[1].bytes() == repeated(ourFrame));
    const auto gaps = repetitionGaps(peer, data);
    CHECK(gaps[0] >= SEND_WAIT_TIME - GapEarlyUs);
    CHECK(gaps[0] <= SEND_WAIT_TIME + GapMarginUs);
    CHECK(callback.lastSuccessful);
}

TEST_CASE("bus acknowledge: 3 repetitions on BUSY or NACK, then reported as failed", "[rp2350][bus][busy]")
{
    // NACK_RETRY_DEFAULT, BUSY_RETRY_DEFAULT = 3 (bus_const.h:92-93); counters bus.cpp:1375-1381,
    // finish at bus.cpp:532-539 when the next acknowledge comes after the last repetition
    for (const Reply r: {Reply::Busy, Reply::Nack})
    {
        DYNAMIC_SECTION("always 0x" << std::hex << static_cast<int>(r))
        {
            Peer peer;
            const auto data = runReplies(peer, {r, r, r, r, r, r});
            CHECK(data.size() == 4);
            for (size_t i = 1; i < data.size(); ++i)
            {
                CHECK(data[i].bytes() == repeated(ourFrame));
            }
            int64_t minGap = SEND_WAIT_TIME;
            if (r == Reply::Busy)
            {
                minGap = BUSY_WAIT_150BIT;
            }
            for (const auto gap: repetitionGaps(peer, data))
            {
                CHECK(gap >= minGap - GapEarlyUs);
                CHECK(gap <= minGap + GapMarginUs);
            }
            CHECK(callback.finished == 1);
            CHECK_FALSE(callback.lastSuccessful);
        }
    }
}

TEST_CASE("bus acknowledge: BUSY and NACK alternately use separate counters", "[rp2350][bus][busy]")
{
    // sendRetries and sendBusyRetries count separately (bus.cpp:1375-1381); the telegram is given up
    // when one of them reached 3 and another negative acknowledge comes (bus.cpp:532)
    Peer peer;
    const auto data = runReplies(peer, {Reply::Nack, Reply::Busy, Reply::Nack, Reply::Busy, Reply::Nack, Reply::Busy,
                                        Reply::Nack, Reply::Busy});
    CHECK(data.size() == 6);
    CHECK(callback.finished == 1);
    CHECK_FALSE(callback.lastSuccessful);
}

// ------------------------------------------------------------------ download-like load

namespace
{

    struct DownloadResult
    {
        std::unique_ptr<Peer> peer = std::make_unique<Peer>();
        Consumer consumer;
    };

    /**
     * The peer sends count MemoryWrite telegrams to us, each 50 bit times after our acknowledge of the
     * previous one (the shortest spacing for the same priority), and repeats a telegram up to 3 times
     * without our acknowledge. The application takes each telegram after processUs.
     */
    void runDownload(DownloadResult& r, const size_t count, const unsigned processUs,
                     const std::vector<size_t>& missFirstAck = {}, const std::function<void()>& onTelegram = {})
    {
        startBus();
        r.peer->attach();
        for (size_t i = 0; i < count; ++i)
        {
            r.peer->download.telegrams.push_back(
                    memoryWrite(static_cast<unsigned int>(i), static_cast<uint16_t>(0x4000 + 4 * i)));
        }
        r.peer->download.missFirstAck = missFirstAck;
        r.consumer.processUs = processUs;
        r.consumer.onTelegram = onTelegram;
        r.peer->startDownload(sim::now() + 1000);
        const uint64_t limit = sim::now() + count * 200000;
        while (!r.peer->download.done && sim::now() < limit)
        {
            runMain(r.consumer, 10000);
        }
        runMain(r.consumer, 50000);
    }

    // Every telegram was received exactly once, in order
    void checkDelivered(const DownloadResult& r, const size_t count)
    {
        REQUIRE(r.peer->download.done);
        CHECK(r.consumer.received.size() == count);
        for (size_t i = 0; i < std::min(count, r.consumer.received.size()); ++i)
        {
            INFO("telegram " << i);
            auto got = r.consumer.received[i];
            got[0] |= SB_TEL_REPEAT_FLAG; // a repetition may have been taken
            got.back() = withChecksum(std::vector<uint8_t>(got.begin(), got.end() - 1)).back();
            CHECK(got == r.peer->download.telegrams[i]);
        }
    }

} // namespace

TEST_CASE("bus load: 50 telegrams at the shortest spacing are all received and acknowledged once",
          "[rp2350][bus][load]")
{
    DownloadResult r;
    runDownload(r, 50, 0);
    checkDelivered(r, 50);
    for (size_t i = 0; i < 50; ++i)
    {
        INFO("telegram " << i);
        CHECK(r.peer->download.attempts[i] == 1);
        CHECK(r.peer->download.acknowledged[i]);
    }
    CHECK(r.peer->dataFrames().empty()); // we sent only acknowledges
}

TEST_CASE("bus load: a slow application gets every telegram once through the repetitions", "[rp2350][bus][load]")
{
    // While telegram[] is taken (telegramLen != 0) bus.cpp sends no acknowledge (bus.cpp:485-492,
    // KNX 3/2/2 2.4.1), the peer repeats; the repetition is taken when the buffer is free.
    // 35 ms per telegram: longer than one telegram with acknowledge and pause (about 28 ms).
    DownloadResult r;
    runDownload(r, 20, 35000);
    checkDelivered(r, 20);
    unsigned repetitions = 0;
    for (size_t i = 0; i < 20; ++i)
    {
        CHECK(r.peer->download.acknowledged[i]);
        repetitions += r.peer->download.attempts[i] - 1;
    }
    CHECK(repetitions > 0);
}

TEST_CASE("bus load: a repetition after a lost acknowledge is acknowledged but not taken twice", "[rp2350][bus][load]")
{
    // bus.cpp:468-482: a repeated frame equal to the last taken one is acknowledged, not taken again
    DownloadResult r;
    runDownload(r, 10, 0, {3, 7});
    checkDelivered(r, 10);
    CHECK(r.peer->download.attempts[3] == 2);
    CHECK(r.peer->download.attempts[7] == 2);
}

// ------------------------------------------------------------------ pause() / resume()

TEST_CASE("bus pause: a telegram while paused is not taken nor acknowledged, its repetition after resume() is",
          "[rp2350][bus][pause]")
{
    // bus.cpp:169-218: pause() in IDLE returns at once; paused, the capture has no interrupt.
    // resume() restarts in INIT (42 bit times silence, bus.cpp:307-340).
    for (const uint64_t resumeAfterUs: {15000u, 6000u})
    {
        DYNAMIC_SECTION("resume() " << resumeAfterUs << " us after the start of the telegram")
        {
            startBus();
            Peer peer;
            peer.attach();
            peer.download.telegrams = {remoteConnect()};
            Consumer consumer;

            const uint64_t before = sim::now();
            bus->pause();
            CHECK(sim::now() - before <= 2);
            const uint64_t t0 = sim::now() + 1000;
            peer.startDownload(t0);
            runMain(consumer, t0 + resumeAfterUs - sim::now());
            CHECK(consumer.received.empty());
            CHECK(peer.ours.empty()); // nothing sent while paused
            bus->resume();
            runMain(consumer, 60000);

            CHECK(consumer.received.size() == 1);
            CHECK(peer.download.attempts[0] >= 2);
            CHECK(peer.download.acknowledged[0]);

            // sending works after resume()
            send(groupWrite());
            runMain(consumer, 60000);
            CHECK(callback.finished == 1);
            CHECK(callback.lastSuccessful);
        }
    }
}

TEST_CASE("bus pause: pause() waits for the acknowledge of a received telegram, a queued telegram is sent "
          "after resume()",
          "[rp2350][bus][pause]")
{
    // canPause() (bus.cpp:220-244): only IDLE or WAIT_50BT...; with waitForTelegramSent = false a
    // queued, not yet sent telegram stays queued
    startBus();
    Peer peer;
    peer.attach();
    Consumer consumer;
    const auto remote = remoteGroupWrite(1);
    const uint64_t t0 = sim::now() + 1000;
    peer.send(t0, remote);
    sim::advance(t0 + 3000 - sim::now());
    send(groupWrite()); // queued during the reception
    sim::advance(3000);

    bus->pause(false);
    const uint64_t paused = sim::now();

    sim::advance(20000); // e.g. a flash write
    const TxFrame* ack = peer.acknowledgeOf({t0, remote});
    REQUIRE(ack != nullptr);
    CHECK(paused >= ack->endOfStopUs() - 5);
    CHECK(paused <= ack->endOfStopUs() + 20);
    CHECK(peer.dataFrames().empty());
    const uint64_t resumed = sim::now();
    bus->resume();
    sim::advance(60000);
    const auto data = peer.dataFrames();
    REQUIRE(data.size() == 1);
    // INIT 42 bit times, then "9 bit times more" (bus.cpp:331, 702-707) and the start of sending
    // (PRE_SEND_TIME, + 3 bit times for low priority, + random, bus.cpp:995-1010): KNX needs 50 bit times
    constexpr uint64_t initAndWaitUs = BIT_TIMES_DELAY(2) + WAIT_50BIT_FOR_IDLE - PRE_SEND_TIME + 1;
    CHECK(data[0].startUs() >= resumed + SEND_WAIT_TIME);
    CHECK(data[0].startUs() <= resumed + initAndWaitUs + PRE_SEND_TIME + BIT_TIMES_DELAY(3) + RANDOMIZE_MODULUS + 5);
    CHECK(callback.lastSuccessful);
}

TEST_CASE("bus pause: pause(true) returns when the queued telegram is acknowledged", "[rp2350][bus][pause]")
{
    startBus();
    Peer peer;
    peer.attach();
    send(groupWrite());
    bus->pause(true);
    const uint64_t after = sim::now();
    REQUIRE(callback.finished == 1);
    CHECK(callback.lastSuccessful);
    REQUIRE(peer.sent.size() == 1); // the peer's ACK of our telegram
    // the end of the acknowledge is detected MAX_INTER_CHAR_TIME after its stop bit (bus.cpp:772-785)
    CHECK(after >= peer.sent[0].endOfStopUs() + MAX_INTER_CHAR_TIME - 5);
    CHECK(after <= peer.sent[0].endOfStopUs() + MAX_INTER_CHAR_TIME + 20);
}

TEST_CASE("bus pause: pause() during repetitions waits until the telegram is finished", "[rp2350][bus][pause]")
{
    // canPause(): no pause while repeatTelegram (bus.cpp:235-241, "we must retry within a certain time")
    startBus();
    Peer peer;
    peer.attach();
    peer.otherwise = Reply::None;
    send(groupWrite());
    sim::advance(20000); // first transmission done, no acknowledge
    const uint64_t before = sim::now();
    bus->pause(false);
    const uint64_t after = sim::now();
    CHECK(callback.finished == 1);
    CHECK_FALSE(callback.lastSuccessful);
    REQUIRE(peer.dataFrames().size() == 4);
    // after the last repetition: acknowledge timeout, then 50 bit times (bus.cpp:1416-1433, 970-976)
    const uint64_t last = peer.dataFrames().back().endOfStopUs();
    INFO("pause() took " << after - before << " us, returned " << after - last << " us after the last frame");
    CHECK(after >= last + SEND_WAIT_TIME - PRE_SEND_TIME - 5);
    CHECK(after <= last + SEND_WAIT_TIME + 5);
}

TEST_CASE("bus pause: download with a flash write (pause) after some telegrams loses nothing", "[rp2350][bus][pause]")
{
    // R5: the application pauses the bus for the flash write when a telegram arrives. The peer repeats
    // the telegrams that came during the pause; with 3 repetitions this covers about 3 x 28 ms.
    for (const unsigned flashUs: {2000u, 20000u, 50000u})
    {
        DYNAMIC_SECTION("flash write " << flashUs << " us after every 4th telegram")
        {
            DownloadResult r;
            unsigned count = 0;
            runDownload(r, 12, 0, {},
                        [&count, flashUs]()
                        {
                            if (++count % 4 == 0)
                            {
                                bus->pause();
                                sim::advance(flashUs);
                                bus->resume();
                            }
                        });
            checkDelivered(r, 12);
            for (size_t i = 0; i < 12; ++i)
            {
                CHECK(r.peer->download.acknowledged[i]);
            }
        }
    }
}

TEST_CASE("bus pause: a pause longer than the peer's 3 repetitions loses the telegram on layer 2",
          "[rp2350][bus][pause]")
{
    // Not a firmware deviation, the limit for R5: telegram, acknowledge slot and 50 bit times take
    // about 28 ms here (15 byte MemoryWrite), 3 repetitions (NACK_RETRY_DEFAULT) cover about 85 ms.
    // A longer pause loses the telegram that arrives meanwhile (the transport layer of the sender
    // would have to repeat it).
    DownloadResult r;
    unsigned count = 0;
    runDownload(r, 6, 0, {},
                [&count]()
                {
                    if (++count == 2)
                    {
                        bus->pause();
                        sim::advance(100000);
                        bus->resume();
                    }
                });
    REQUIRE(r.peer->download.done);
    CHECK(r.consumer.received.size() == 5);
    CHECK(r.peer->download.attempts[2] == NACK_RETRY_DEFAULT + 1);
    CHECK_FALSE(r.peer->download.acknowledged[2]);
    for (const size_t i: {0u, 1u, 3u, 4u, 5u})
    {
        CHECK(r.peer->download.acknowledged[i]);
    }
}

// ------------------------------------------------------------------ explore: observed timing

TEST_CASE("bus explore t9: collision", "[.explore]")
{
    for (const int offset: {-40, -20, 0, 3})
    {
        Peer peer;
        runCollision(peer, remoteConnect(0xB0), offset);
        char title[80];
        std::snprintf(title, sizeof(title), "collision in the control field, peer %+d us:", offset);
        printFrames(title, peer);
    }
    {
        Peer peer;
        runCollision(peer, remoteGroupWrite(0), 0);
        printFrames("collision in byte 2:", peer);
    }
    {
        startBus();
        Peer peer;
        peer.attach();
        for (int i = 0; i < 6; ++i)
        {
            peer.collideWith.push_back(remoteConnect(0xB0));
        }
        Consumer consumer;
        send(groupWrite());
        runMain(consumer, 400000);
        printFrames("always collided:", peer);
    }
}

TEST_CASE("bus explore t9: jitter and spikes", "[.explore]")
{
    std::printf("receive with shifted data bits: offset received acknowledged ackDelay recovered\n");
    for (const int d: {-40, -35, -20, -7, 0, 20, 33, 34, 40, 60, 68, 69, 70})
    {
        const auto r = receiveWith(toUs, dataBits(d));
        std::printf("  %+4d us  %d %d %lld %d\n", d, r.received, r.acknowledged, static_cast<long long>(r.ackDelayUs),
                    r.recovered);
    }
    std::printf("longer pause after character 0:\n");
    for (const int d: {0, 20, 25, 27, 28, 29, 30, 31, 35, 40})
    {
        const auto r = receiveWith(toUs, secondCharacter(d));
        std::printf("  %+4d us  %d %d %lld %d\n", d, r.received, r.acknowledged, static_cast<long long>(r.ackDelayUs),
                    r.recovered);
    }
    std::printf("spike of 2 us before the 0 bit at character 1 position 3: start relative to the bit\n");
    for (const int64_t at: {-20, -12, -10, -9, -8, -7, -6, -5, -4, -3})
    {
        const auto r = receiveWith(toUs, noOffset, spikes({{ZeroBitUs + at, 2}}));
        std::printf("  %+4lld us  %d %d %lld %d\n", static_cast<long long>(at), r.received, r.acknowledged,
                    static_cast<long long>(r.ackDelayUs), r.recovered);
    }
    std::printf("spike in a 1 bit (+50 us): width received\n");
    for (const unsigned w: {1u, 2u, 3u, 4u, 5u, 6u})
    {
        const auto r = receiveWith(toUs, noOffset, spikes({{OneBitUs + 50, w}}));
        std::printf("  %u us  %d %d\n", w, r.received, r.recovered);
    }
}

TEST_CASE("bus explore t9: BUSY and NACK", "[.explore]")
{
    const std::vector<std::pair<const char*, std::vector<Reply>>> cases = {
            {"BUSY, ACK", {Reply::Busy, Reply::Ack}},
            {"NACK, ACK", {Reply::Nack, Reply::Ack}},
            {"BUSY x4", {Reply::Busy, Reply::Busy, Reply::Busy, Reply::Busy}},
            {"NACK x4", {Reply::Nack, Reply::Nack, Reply::Nack, Reply::Nack}},
            {"NACK/BUSY alternating",
             {Reply::Nack, Reply::Busy, Reply::Nack, Reply::Busy, Reply::Nack, Reply::Busy, Reply::Nack}},
    };
    for (const auto& c: cases)
    {
        Peer peer;
        const auto data = runReplies(peer, c.second);
        printFrames(c.first, peer);
        std::printf("  gaps (end of the acknowledge's stop bit to our next start bit):");
        for (const auto g: repetitionGaps(peer, data))
        {
            std::printf(" %lld", static_cast<long long>(g));
        }
        std::printf("\n");
    }
}

TEST_CASE("bus explore t9: download and pause", "[.explore]")
{
    for (const unsigned processUs: {0u, 35000u})
    {
        DownloadResult r;
        runDownload(r, 50, processUs, {3});
        unsigned frames = 0;
        for (const auto a: r.peer->download.attempts)
        {
            frames += a;
        }
        std::printf("download 50 x MemoryWrite, application %u us per telegram, peer misses ACK of #3: %llu us, "
                    "%u frames, taken %zu\n",
                    processUs, static_cast<unsigned long long>(r.peer->download.doneUs - r.peer->sent[0].startUs),
                    frames, r.consumer.received.size());
        std::printf("  attempts:");
        for (const auto a: r.peer->download.attempts)
        {
            std::printf(" %u", a);
        }
        std::printf("\n");
    }
    for (const unsigned flashUs: {2000u, 20000u, 50000u, 80000u, 100000u, 150000u})
    {
        DownloadResult r;
        unsigned count = 0;
        std::vector<uint64_t> pauseUs;
        runDownload(r, 12, 0, {},
                    [&count, &pauseUs, flashUs]()
                    {
                        if (++count % 4 == 0)
                        {
                            const uint64_t t = sim::now();
                            bus->pause();
                            pauseUs.push_back(sim::now() - t);
                            sim::advance(flashUs);
                            bus->resume();
                        }
                    });
        std::printf("download 12 telegrams, flash write %u us after every 4th: taken %zu, attempts:", flashUs,
                    r.consumer.received.size());
        for (const auto a: r.peer->download.attempts)
        {
            std::printf(" %u", a);
        }
        std::printf(", not acknowledged:");
        for (size_t i = 0; i < r.peer->download.acknowledged.size(); ++i)
        {
            if (!r.peer->download.acknowledged[i])
            {
                std::printf(" #%zu", i);
            }
        }
        std::printf(", pause() waited:");
        for (const auto p: pauseUs)
        {
            std::printf(" %llu", static_cast<unsigned long long>(p));
        }
        std::printf(" us\n");
    }
    {
        startBus();
        Peer peer;
        peer.attach();
        peer.otherwise = Reply::None;
        send(groupWrite());
        sim::advance(20000);
        const uint64_t before = sim::now();
        bus->pause(false);
        std::printf("pause() during the repetitions without acknowledge took %llu us\n",
                    static_cast<unsigned long long>(sim::now() - before));
    }
}
