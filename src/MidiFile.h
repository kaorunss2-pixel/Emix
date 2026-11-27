
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <new>
#include <string>
#include <vector>
#include <cmath>

#include "PianoRoll.h"

namespace midifile
{
    struct ImportOptions
    {
        uint64_t maxFileBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
        uint64_t maxNotes = 100000000ULL;
    };

    struct ImportResult
    {
        bool ok = false;
        std::string error;

        float bpm = 120.0f;

        int noteCount = 0;
        uint64_t noteCount64 = 0;
        uint64_t totalEvents = 0;
        uint64_t unmatchedNoteOffs = 0;
        uint64_t unfinishedNotes = 0;

        uint64_t fileBytes = 0;

        int format = 0;
        int tracks = 0;
        int ticksPerBeat = 480;
    };

    struct TrackReader
    {
        std::ifstream& file;
        uint64_t remaining;

        bool ReadByte(uint8_t& value)
        {
            if (remaining == 0)
                return false;

            char c = 0;

            if (!file.get(c))
                return false;

            value = static_cast<uint8_t>(
                static_cast<unsigned char>(c));

            --remaining;
            return true;
        }

        bool Skip(uint64_t count)
        {
            if (count > remaining)
                return false;

            char buffer[4096];

            while (count > 0)
            {
                const uint64_t amount =
                    std::min<uint64_t>(count, sizeof(buffer));

                file.read(buffer, static_cast<std::streamsize>(amount));

                if (!file)
                    return false;

                count -= amount;
                remaining -= amount;
            }

            return true;
        }

        bool ReadVlq(uint32_t& value)
        {
            value = 0;

            for (int i = 0; i < 4; ++i)
            {
                uint8_t b = 0;

                if (!ReadByte(b))
                    return false;

                value = (value << 7) | (b & 0x7F);

                if ((b & 0x80) == 0)
                    return true;
            }

            return false;
        }
    };

    struct PendingNote
    {
        uint64_t startTick = 0;
        float velocity = 1.0f;
    };

    inline bool ReadExact(
        std::ifstream& file,
        void* destination,
        size_t size)
    {
        file.read(
            static_cast<char*>(destination),
            static_cast<std::streamsize>(size));

        return static_cast<bool>(file);
    }

    inline uint16_t ReadBe16(const uint8_t* p)
    {
        return
            (static_cast<uint16_t>(p[0]) << 8) |
            static_cast<uint16_t>(p[1]);
    }

    inline uint32_t ReadBe32(const uint8_t* p)
    {
        return
            (static_cast<uint32_t>(p[0]) << 24) |
            (static_cast<uint32_t>(p[1]) << 16) |
            (static_cast<uint32_t>(p[2]) << 8) |
            static_cast<uint32_t>(p[3]);
    }

    inline bool ReadTrack(
        std::ifstream& file,
        uint32_t trackSize,
        int destinationTrack,
        int& nextId,
        std::vector<MidiNote>& output,
        ImportResult& result,
        const ImportOptions& options,
        bool& tempoFound)
    {
        TrackReader reader{ file, trackSize };

        std::array<
            std::array<std::vector<PendingNote>, 128>,
            16
        > pending;

        std::array<
            std::array<size_t, 128>,
            16
        > pendingHead{};

        uint8_t runningStatus = 0;
        uint64_t absoluteTick = 0;

        while (reader.remaining > 0)
        {
            uint32_t delta = 0;

            if (!reader.ReadVlq(delta))
            {
                result.error = "Invalid MIDI delta time.";
                return false;
            }

            if (absoluteTick >
                std::numeric_limits<uint64_t>::max() - delta)
            {
                result.error = "MIDI tick overflow.";
                return false;
            }

            absoluteTick += delta;

            uint8_t first = 0;

            if (!reader.ReadByte(first))
            {
                result.error = "Unexpected end of MIDI track.";
                return false;
            }

            uint8_t status = 0;
            uint8_t firstData = 0;
            bool hasFirstData = false;

            if (first & 0x80)
            {
                status = first;
            }
            else
            {
                if (runningStatus == 0)
                {
                    result.error = "Invalid MIDI running status.";
                    return false;
                }

                status = runningStatus;
                firstData = first;
                hasFirstData = true;
            }

            ++result.totalEvents;

            if (status == 0xFF)
            {
                runningStatus = 0;

                uint8_t metaType = 0;
                uint32_t length = 0;

                if (!reader.ReadByte(metaType) ||
                    !reader.ReadVlq(length))
                {
                    result.error = "Invalid MIDI meta event.";
                    return false;
                }

                if (metaType == 0x51 && length >= 3)
                {
                    uint8_t tempoBytes[3];

                    for (int i = 0; i < 3; ++i)
                    {
                        if (!reader.ReadByte(tempoBytes[i]))
                        {
                            result.error = "Invalid tempo event.";
                            return false;
                        }
                    }

                    const uint32_t microseconds =
                        (static_cast<uint32_t>(tempoBytes[0]) << 16) |
                        (static_cast<uint32_t>(tempoBytes[1]) << 8) |
                        static_cast<uint32_t>(tempoBytes[2]);

                    if (microseconds > 0 && !tempoFound)
                    {
                        result.bpm =
                            60000000.0f /
                            static_cast<float>(microseconds);

                        tempoFound = true;
                    }

                    if (!reader.Skip(length - 3))
                    {
                        result.error = "Invalid tempo data length.";
                        return false;
                    }
                }
                else
                {
                    if (!reader.Skip(length))
                    {
                        result.error = "Invalid MIDI meta data length.";
                        return false;
                    }
                }

                if (metaType == 0x2F)
                {
                    if (!reader.Skip(reader.remaining))
                    {
                        result.error = "Invalid end-of-track event.";
                        return false;
                    }

                    break;
                }

                continue;
            }

            if (status == 0xF0 || status == 0xF7)
            {
                runningStatus = 0;

                uint32_t length = 0;

                if (!reader.ReadVlq(length) ||
                    !reader.Skip(length))
                {
                    result.error = "Invalid MIDI SysEx event.";
                    return false;
                }

                continue;
            }

            if (status < 0x80 || status >= 0xF0)
            {
                result.error = "Unsupported MIDI system event.";
                return false;
            }

            runningStatus = status;

            const uint8_t command = status & 0xF0;
            const uint8_t channel = status & 0x0F;

            const int dataCount =
                (command == 0xC0 || command == 0xD0) ? 1 : 2;

            uint8_t data1 = 0;
            uint8_t data2 = 0;

            if (hasFirstData)
            {
                data1 = firstData;
            }
            else if (!reader.ReadByte(data1))
            {
                result.error = "Invalid MIDI channel event.";
                return false;
            }

            if (dataCount == 2)
            {
                if (!reader.ReadByte(data2))
                {
                    result.error = "Invalid MIDI channel data.";
                    return false;
                }
            }

            if (data1 >= 128 || data2 >= 128)
            {
                result.error = "Invalid MIDI data byte.";
                return false;
            }

            const bool noteOn =
                command == 0x90 && data2 != 0;

            const bool noteOff =
                command == 0x80 ||
                (command == 0x90 && data2 == 0);

            if (noteOn)
            {
                auto& queue = pending[channel][data1];

                queue.push_back({
                    absoluteTick,
                    static_cast<float>(data2) / 127.0f
                });
            }
            else if (noteOff)
            {
                auto& queue = pending[channel][data1];
                size_t& head = pendingHead[channel][data1];

                if (head >= queue.size())
                {
                    ++result.unmatchedNoteOffs;
                    continue;
                }

                const PendingNote start = queue[head++];

                if (result.noteCount64 >= options.maxNotes)
                {
                    result.error = "Maximum MIDI note limit exceeded.";
                    return false;
                }

                const uint64_t duration =
                    absoluteTick >= start.startTick
                    ? absoluteTick - start.startTick
                    : 0;

                const double ticksPerBeat =
                    static_cast<double>(result.ticksPerBeat);

                MidiNote note;

                note.id = nextId++;
                note.trackIndex = destinationTrack;
                note.pitch = static_cast<int>(data1);

                note.startBeat =
                    static_cast<float>(
                        static_cast<double>(start.startTick) /
                        ticksPerBeat);

                note.lengthBeats =
                    static_cast<float>(
                        static_cast<double>(duration) /
                        ticksPerBeat);

                note.lengthBeats =
                    std::max(0.0625f, note.lengthBeats);

                note.velocity = start.velocity;

                output.push_back(note);

                ++result.noteCount64;

                if (result.noteCount64 >
                    static_cast<uint64_t>(
                        std::numeric_limits<int>::max()))
                {
                    result.noteCount =
                        std::numeric_limits<int>::max();
                }
                else
                {
                    result.noteCount =
                        static_cast<int>(result.noteCount64);
                }

                if (head == queue.size())
                {
                    queue.clear();
                    head = 0;
                }
            }
        }

        for (int channel = 0; channel < 16; ++channel)
        {
            for (int pitch = 0; pitch < 128; ++pitch)
            {
                const auto& queue = pending[channel][pitch];
                const size_t head = pendingHead[channel][pitch];

                if (queue.size() > head)
                {
                    result.unfinishedNotes +=
                        static_cast<uint64_t>(queue.size() - head);
                }
            }
        }

        return true;
    }

    inline ImportResult Import(
        const std::string& path,
        int trackIndex,
        int& nextId,
        std::vector<MidiNote>& output,
        const ImportOptions& options)
    {
        ImportResult result;

        const size_t originalSize = output.size();
        const int originalNextId = nextId;

        try
        {
            std::ifstream file(path, std::ios::binary);

            if (!file)
            {
                result.error = "Cannot open MIDI file.";
                return result;
            }

            file.seekg(0, std::ios::end);

            const std::streamoff fileSize = file.tellg();

            if (fileSize < 14)
            {
                result.error = "MIDI file is too small.";
                return result;
            }

            result.fileBytes = static_cast<uint64_t>(fileSize);

            if (result.fileBytes > options.maxFileBytes)
            {
                result.error = "MIDI file exceeds maximum file size.";
                return result;
            }

            file.seekg(0, std::ios::beg);

            uint8_t header[8];

            if (!ReadExact(file, header, sizeof(header)))
            {
                result.error = "Cannot read MIDI header.";
                return result;
            }

            if (header[0] != 'M' ||
                header[1] != 'T' ||
                header[2] != 'h' ||
                header[3] != 'd')
            {
                result.error = "Invalid MIDI header.";
                return result;
            }

            const uint32_t headerLength = ReadBe32(header + 4);

            if (headerLength < 6 ||
                headerLength > result.fileBytes - 8)
            {
                result.error = "Invalid MIDI header length.";
                return result;
            }

            uint8_t headerData[6];

            if (!ReadExact(file, headerData, 6))
            {
                result.error = "Cannot read MIDI header data.";
                return result;
            }

            result.format = ReadBe16(headerData);
            result.tracks = ReadBe16(headerData + 2);

            const uint16_t division = ReadBe16(headerData + 4);

            if (result.format > 1)
            {
                result.error =
                    "Format 2 MIDI is not supported. "
                    "Please convert it to Format 0 or Format 1.";

                return result;
            }

            if (result.tracks <= 0)
            {
                result.error = "MIDI contains no tracks.";
                return result;
            }

            if (division & 0x8000)
            {
                result.error =
                    "SMPTE time division is not supported.";

                return result;
            }

            if (division == 0)
            {
                result.error = "Invalid MIDI ticks per beat.";
                return result;
            }

            result.ticksPerBeat = division;

            if (headerLength > 6)
            {
                file.seekg(
                    static_cast<std::streamoff>(headerLength - 6),
                    std::ios::cur);

                if (!file)
                {
                    result.error = "Invalid MIDI header extension.";
                    return result;
                }
            }

            bool tempoFound = false;

            int loadedTracks = 0;

            while (file &&
                static_cast<uint64_t>(file.tellg()) <
                result.fileBytes)
            {
                uint8_t chunkHeader[8];

                if (!ReadExact(file, chunkHeader, 8))
                    break;

                const uint32_t chunkSize =
                    ReadBe32(chunkHeader + 4);

                const uint64_t currentPosition =
                    static_cast<uint64_t>(file.tellg());

                if (chunkSize > result.fileBytes - currentPosition)
                {
                    result.error = "Invalid MIDI chunk size.";
                    throw std::runtime_error(result.error);
                }

                const bool isTrack =
                    chunkHeader[0] == 'M' &&
                    chunkHeader[1] == 'T' &&
                    chunkHeader[2] == 'r' &&
                    chunkHeader[3] == 'k';

                if (!isTrack)
                {
                    file.seekg(
                        static_cast<std::streamoff>(chunkSize),
                        std::ios::cur);

                    if (!file)
                    {
                        result.error = "Cannot skip MIDI chunk.";
                        throw std::runtime_error(result.error);
                    }

                    continue;
                }

                if (!ReadTrack(
                    file,
                    chunkSize,
                    trackIndex,
                    nextId,
                    output,
                    result,
                    options,
                    tempoFound))
                {
                    throw std::runtime_error(result.error);
                }

                ++loadedTracks;
            }

            if (loadedTracks == 0)
            {
                result.error = "No MIDI tracks found.";
                throw std::runtime_error(result.error);
            }

            result.ok = true;
        }
        catch (const std::bad_alloc&)
        {
            result.error =
                "Not enough memory to load this MIDI file.";
        }
        catch (const std::exception& e)
        {
            if (result.error.empty())
                result.error = e.what();
        }
        catch (...)
        {
            result.error = "Unknown MIDI import error.";
        }

        if (!result.ok)
        {
            output.resize(originalSize);
            nextId = originalNextId;
            result.noteCount64 = 0;
            result.noteCount = 0;
        }

        return result;
    }

    inline ImportResult Import(
        const std::string& path,
        int trackIndex,
        int& nextId,
        std::vector<MidiNote>& output)
    {
        return Import(
            path,
            trackIndex,
            nextId,
            output,
            ImportOptions{});
    }

    inline void PutVlq(
        std::vector<uint8_t>& output,
        uint32_t value)
    {
        uint8_t buffer[4];

        int count = 0;

        buffer[count++] = static_cast<uint8_t>(value & 0x7F);

        while ((value >>= 7) != 0)
        {
            buffer[count++] =
                static_cast<uint8_t>((value & 0x7F) | 0x80);
        }

        while (count > 0)
            output.push_back(buffer[--count]);
    }

    inline void PutBe16(
        std::vector<uint8_t>& output,
        uint16_t value)
    {
        output.push_back(static_cast<uint8_t>(value >> 8));
        output.push_back(static_cast<uint8_t>(value));
    }

    inline void PutBe32(
        std::vector<uint8_t>& output,
        uint32_t value)
    {
        output.push_back(static_cast<uint8_t>(value >> 24));
        output.push_back(static_cast<uint8_t>(value >> 16));
        output.push_back(static_cast<uint8_t>(value >> 8));
        output.push_back(static_cast<uint8_t>(value));
    }

    struct ExportEvent
    {
        uint64_t tick = 0;
        uint8_t pitch = 60;
        uint8_t velocity = 100;
        bool noteOn = false;
    };

    inline bool Export(
        const std::string& path,
        const std::vector<MidiNote>& notes,
        int trackIndex,
        float bpm)
    {
        try
        {
            constexpr uint32_t TICKS_PER_BEAT = 480;

            if (!std::isfinite(bpm) || bpm <= 0.0f)
                bpm = 120.0f;

            std::vector<ExportEvent> events;

            for (const MidiNote& note : notes)
            {
                if (note.trackIndex != trackIndex)
                    continue;

                if (!std::isfinite(note.startBeat) ||
                    !std::isfinite(note.lengthBeats))
                    continue;

                const double start =
                    std::max(0.0, static_cast<double>(note.startBeat));

                const double length =
                    std::max(0.0625, static_cast<double>(note.lengthBeats));

                const uint64_t startTick =
                    static_cast<uint64_t>(
                        start * TICKS_PER_BEAT);

                const uint64_t endTick =
                    static_cast<uint64_t>(
                        (start + length) * TICKS_PER_BEAT);

                const uint8_t pitch =
                    static_cast<uint8_t>(
                        std::clamp(note.pitch, 0, 127));

                const uint8_t velocity =
                    static_cast<uint8_t>(
                        std::clamp(
                            static_cast<int>(note.velocity * 127.0f),
                            1,
                            127));

                events.push_back({
                    startTick,
                    pitch,
                    velocity,
                    true
                });

                events.push_back({
                    std::max(startTick + 1, endTick),
                    pitch,
                    velocity,
                    false
                });
            }

            std::stable_sort(
                events.begin(),
                events.end(),
                [](const ExportEvent& a, const ExportEvent& b)
                {
                    if (a.tick != b.tick)
                        return a.tick < b.tick;

                    return !a.noteOn && b.noteOn;
                });

            std::vector<uint8_t> track;

            const uint32_t microseconds =
                static_cast<uint32_t>(
                    60000000.0 / static_cast<double>(bpm));

            track.push_back(0x00);
            track.push_back(0xFF);
            track.push_back(0x51);
            track.push_back(0x03);

            track.push_back(
                static_cast<uint8_t>(microseconds >> 16));

            track.push_back(
                static_cast<uint8_t>(microseconds >> 8));

            track.push_back(
                static_cast<uint8_t>(microseconds));

            track.push_back(0x00);
            track.push_back(0xFF);
            track.push_back(0x58);
            track.push_back(0x04);
            track.push_back(0x04);
            track.push_back(0x02);
            track.push_back(0x18);
            track.push_back(0x08);

            uint64_t previousTick = 0;

            for (const ExportEvent& event : events)
            {
                const uint64_t delta =
                    event.tick >= previousTick
                    ? event.tick - previousTick
                    : 0;

                if (delta > 0x0FFFFFFF)
                    return false;

                PutVlq(
                    track,
                    static_cast<uint32_t>(delta));

                track.push_back(
                    event.noteOn ? 0x90 : 0x80);

                track.push_back(event.pitch);

                track.push_back(
                    event.noteOn ? event.velocity : 0);

                previousTick = event.tick;
            }

            track.push_back(0x00);
            track.push_back(0xFF);
            track.push_back(0x2F);
            track.push_back(0x00);

            std::vector<uint8_t> file;

            file.insert(file.end(), {
                'M', 'T', 'h', 'd'
            });

            PutBe32(file, 6);
            PutBe16(file, 0);
            PutBe16(file, 1);
            PutBe16(file, TICKS_PER_BEAT);

            file.insert(file.end(), {
                'M', 'T', 'r', 'k'
            });

            PutBe32(
                file,
                static_cast<uint32_t>(track.size()));

            file.insert(
                file.end(),
                track.begin(),
                track.end());

            std::ofstream output(
                path,
                std::ios::binary);

            if (!output)
                return false;

            output.write(
                reinterpret_cast<const char*>(file.data()),
                static_cast<std::streamsize>(file.size()));

            return static_cast<bool>(output);
        }
        catch (...)
        {
            return false;
        }
    }
}
