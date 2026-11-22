#pragma once
// EasyMix Studio: Standard MIDI File (.mid) import / export.
// Import : all channels / tracks of the file are merged into ONE EasyMix track.
// Export : the notes of one EasyMix track are written as a format-0 file (480 ticks per beat).
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "PianoRoll.h"   // MidiNote

namespace midifile
{
    struct ImportResult
    {
        bool ok = false;
        std::string error;
        float bpm = 0.0f;      // first tempo event, 0 = none found
        int noteCount = 0;
    };

    inline uint32_t Be32(const unsigned char* p)
    {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    }
    inline uint16_t Be16(const unsigned char* p)
    {
        return static_cast<uint16_t>((p[0] << 8) | p[1]);
    }

    // Reads a variable length quantity. Returns false on truncated data.
    inline bool ReadVlq(const unsigned char*& p, const unsigned char* end, uint32_t& value)
    {
        value = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (p >= end) return false;
            const unsigned char b = *p++;
            value = (value << 7) | (b & 0x7F);
            if (!(b & 0x80)) return true;
        }
        return false;
    }

    // nextId: first free note id (is advanced). Notes are appended to `out`.
    inline ImportResult Import(
        const std::string& path,
        int trackIndex,
        int& nextId,
        std::vector<MidiNote>& out)
    {
        ImportResult res;

        std::ifstream f(path, std::ios::binary);
        if (!f) { res.error = "Could not open file."; return res; }
        const std::vector<unsigned char> data(
            (std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

        if (data.size() < 14 || std::string(data.begin(), data.begin() + 4) != "MThd")
        {
            res.error = "Not a MIDI file.";
            return res;
        }

        const uint32_t headLen = Be32(&data[4]);
        if (headLen < 6 || 8 + size_t(headLen) > data.size())
        {
            res.error = "Broken MIDI header.";
            return res;
        }

        const uint16_t numTracks = Be16(&data[10]);
        const uint16_t division = Be16(&data[12]);
        if (division & 0x8000)
        {
            res.error = "SMPTE time division is not supported.";
            return res;
        }
        if (division == 0)
        {
            res.error = "Invalid time division.";
            return res;
        }

        const double tpb = static_cast<double>(division);
        size_t pos = 8 + headLen;

        for (uint16_t t = 0; t < numTracks && pos + 8 <= data.size(); ++t)
        {
            const bool isTrk = std::string(data.begin() + pos, data.begin() + pos + 4) == "MTrk";
            const uint32_t len = Be32(&data[pos + 4]);
            pos += 8;

            const size_t chunkEnd = std::min(data.size(), pos + size_t(len));
            if (!isTrk) { pos = chunkEnd; continue; }

            const unsigned char* p = data.data() + pos;
            const unsigned char* end = data.data() + chunkEnd;

            struct Pending { double startTick; float vel; };
            std::vector<Pending> pending[16][128];

            uint64_t tick = 0;
            unsigned char running = 0;

            while (p < end)
            {
                uint32_t delta = 0;
                if (!ReadVlq(p, end, delta)) break;
                tick += delta;
                if (p >= end) break;

                unsigned char status = *p;
                if (status & 0x80) { ++p; if (status < 0xF0) running = status; }
                else
                {
                    if (!running) break;
                    status = running;
                }

                if (status == 0xFF)
                {
                    if (p >= end) break;
                    const unsigned char type = *p++;
                    uint32_t mlen = 0;
                    if (!ReadVlq(p, end, mlen) || p + mlen > end) break;
                    if (type == 0x51 && mlen == 3 && res.bpm <= 0.0f)
                    {
                        const uint32_t us = (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
                        if (us > 0) res.bpm = static_cast<float>(60000000.0 / us);
                    }
                    p += mlen;
                    if (type == 0x2F) break;
                }
                else if (status == 0xF0 || status == 0xF7)
                {
                    uint32_t slen = 0;
                    if (!ReadVlq(p, end, slen) || p + slen > end) break;
                    p += slen;
                }
                else
                {
                    const int kind = status & 0xF0;
                    const int ch = status & 0x0F;
                    const int need = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
                    if (p + need > end) break;
                    const int d1 = p[0] & 0x7F;
                    const int d2 = need == 2 ? (p[1] & 0x7F) : 0;
                    p += need;

                    if (kind == 0x90 && d2 > 0)
                    {
                        pending[ch][d1].push_back({ static_cast<double>(tick), d2 / 127.0f });
                    }
                    else if (kind == 0x80 || (kind == 0x90 && d2 == 0))
                    {
                        auto& v = pending[ch][d1];
                        if (!v.empty())
                        {
                            const Pending on = v.front();
                            v.erase(v.begin());

                            MidiNote n;
                            n.id = nextId++;
                            n.trackIndex = trackIndex;
                            n.pitch = d1;
                            n.startBeat = static_cast<float>(on.startTick / tpb);
                            n.lengthBeats = std::max(0.0625f,
                                static_cast<float>((static_cast<double>(tick) - on.startTick) / tpb));
                            n.velocity = std::clamp(on.vel, 0.01f, 1.0f);
                            out.push_back(n);
                            ++res.noteCount;
                        }
                    }
                }
            }
            pos = chunkEnd;
        }

        res.ok = true;
        return res;
    }

    inline void PutVlq(std::vector<unsigned char>& b, uint32_t v)
    {
        unsigned char tmp[5];
        int n = 0;
        tmp[n++] = v & 0x7F;
        while (v >>= 7) tmp[n++] = static_cast<unsigned char>((v & 0x7F) | 0x80);
        while (n--) b.push_back(tmp[n]);
    }

    inline bool Export(
        const std::string& path,
        const std::vector<MidiNote>& notes,
        int trackIndex,
        float bpm)
    {
        constexpr int kTpb = 480;

        struct Ev { uint32_t tick; bool on; int pitch; int vel; };
        std::vector<Ev> evs;
        for (const auto& n : notes)
        {
            if (n.trackIndex != trackIndex) continue;
            const uint32_t s = static_cast<uint32_t>(std::max(0.0f, n.startBeat) * kTpb + 0.5f);
            const uint32_t e = std::max(s + 1,
                static_cast<uint32_t>((std::max(0.0f, n.startBeat) + std::max(0.0f, n.lengthBeats)) * kTpb + 0.5f));
            const int vel = std::clamp(static_cast<int>(n.velocity * 127.0f + 0.5f), 1, 127);
            const int pit = std::clamp(n.pitch, 0, 127);
            evs.push_back({ s, true, pit, vel });
            evs.push_back({ e, false, pit, 0 });
        }
        std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b)
            {
                if (a.tick != b.tick) return a.tick < b.tick;
                return !a.on && b.on;   // note-off first
            });

        std::vector<unsigned char> trk;

        const uint32_t us = static_cast<uint32_t>(60000000.0 / std::clamp(bpm, 20.0f, 400.0f));
        PutVlq(trk, 0);
        trk.insert(trk.end(), { 0xFF, 0x51, 0x03,
            static_cast<unsigned char>(us >> 16),
            static_cast<unsigned char>(us >> 8),
            static_cast<unsigned char>(us) });
        PutVlq(trk, 0);
        trk.insert(trk.end(), { 0xFF, 0x58, 0x04, 4, 2, 24, 8 });   // 4/4

        uint32_t last = 0;
        for (const auto& e : evs)
        {
            PutVlq(trk, e.tick - last);
            last = e.tick;
            trk.push_back(e.on ? 0x90 : 0x80);
            trk.push_back(static_cast<unsigned char>(e.pitch));
            trk.push_back(static_cast<unsigned char>(e.vel));
        }
        PutVlq(trk, 0);
        trk.insert(trk.end(), { 0xFF, 0x2F, 0x00 });

        std::ofstream out(path, std::ios::binary);
        if (!out) return false;

        auto w32 = [&](uint32_t v)
            {
                const unsigned char b[4] = { static_cast<unsigned char>(v >> 24), static_cast<unsigned char>(v >> 16),
                                             static_cast<unsigned char>(v >> 8), static_cast<unsigned char>(v) };
                out.write(reinterpret_cast<const char*>(b), 4);
            };
        auto w16 = [&](uint16_t v)
            {
                const unsigned char b[2] = { static_cast<unsigned char>(v >> 8), static_cast<unsigned char>(v) };
                out.write(reinterpret_cast<const char*>(b), 2);
            };

        out.write("MThd", 4); w32(6); w16(0); w16(1); w16(kTpb);
        out.write("MTrk", 4); w32(static_cast<uint32_t>(trk.size()));
        out.write(reinterpret_cast<const char*>(trk.data()), static_cast<std::streamsize>(trk.size()));
        return static_cast<bool>(out);
    }
}
