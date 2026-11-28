#pragma once
#define EASYMIX_PRO_API 1
// EasyMix Studio: Pro pack (MIDI input / MIDI recording / loudness normalize / auto take path)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef _WIN32
#include <Windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#else
#include <CoreMIDI/CoreMIDI.h>   // macOS MIDI input
#endif
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "PianoRoll.h"
#include "EasyMixExtras.h"

namespace pro
{
    struct MidiMsg { bool on; int pitch; float vel; };
    struct Held { float beat; float vel; };

    struct LiveState
    {
        bool kbdPiano = false;
        bool midiRec = false;
        int octave = 4;
        std::unordered_map<int, Held> held;   // notes currently held while recording
        bool keyOn[16] = {};
        int keyPitch[16] = {};
    };

#ifndef _WIN32
    // Push every MIDI packet of `list` into the queue, then dispose of it.
    // (macOS CoreMIDI only; the Windows build uses winmm callbacks instead.)
    inline void ParseMidiPacketList(const MIDIPacketList* list,
        std::mutex& mtx, std::vector<MidiMsg>& q)
    {
        if (!list) return;

        const MIDIPacket* packet = &list->packet[0];

        for (UInt32 i = 0; i < list->numPackets; ++i, packet = MIDIPacketNext(packet))
        {
            UInt16 k = 0;

            while (k < packet->length)
            {
                const unsigned char status = packet->data[k];

                // Skip system / realtime bytes and running-status data we cannot size.
                if (status < 0x80) { ++k; continue; }

                const int type = status & 0xF0;
                const int nbytes = (type == 0xC0 || type == 0xD0) ? 2 : 3;

                if (type == 0x80 || type == 0x90)
                {
                    if (k + 2 >= packet->length) break;

                    const int d1 = packet->data[k + 1] & 0x7F;
                    const int d2 = packet->data[k + 2] & 0x7F;
                    const bool on = (type == 0x90 && d2 > 0);

                    std::lock_guard<std::mutex> lk(mtx);
                    if (q.size() < 4096)
                        q.push_back({ on, d1, on ? d2 / 127.0f : 0.0f });
                }

                k = static_cast<UInt16>(k + nbytes);
            }
        }
    }
#endif

    class MidiIn
    {
    public:
        std::vector<std::string> names;
        int selected = -1;
        bool scanned = false;

        ~MidiIn() { close(); }

#ifdef _WIN32

        void refresh()
        {
            names.clear();
            const UINT n = midiInGetNumDevs();
            for (UINT i = 0; i < n; ++i)
            {
                MIDIINCAPSA c{};
                names.push_back(midiInGetDevCapsA(i, &c, sizeof(c)) == MMSYSERR_NOERROR ? c.szPname : "?");
            }
            scanned = true;
        }

        bool open(int idx)
        {
            close();
            if (idx < 0 || idx >= static_cast<int>(names.size())) return false;
            if (midiInOpen(&h, static_cast<UINT>(idx), reinterpret_cast<DWORD_PTR>(&MidiIn::Proc),
                reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
            {
                h = nullptr;
                return false;
            }
            midiInStart(h);
            selected = idx;
            return true;
        }

        void close()
        {
            if (h) { midiInStop(h); midiInReset(h); midiInClose(h); h = nullptr; }
            selected = -1;
        }

#else   // macOS: CoreMIDI

        void refresh()
        {
            names.clear();
            sources.clear();

            const ItemCount total = MIDIGetNumberOfSources();

            for (ItemCount i = 0; i < total; ++i)
            {
                MIDIEndpointRef source = MIDIGetSource(i);
                if (!source) continue;

                CFStringRef name = nullptr;
                if (MIDIObjectGetStringProperty(source, kMIDIPropertyDisplayName, &name) != noErr || !name)
                    MIDIObjectGetStringProperty(source, kMIDIPropertyName, &name);

                std::string label = "?";
                if (name)
                {
                    char buf[256] = {};
                    if (CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8))
                        label = buf;
                    CFRelease(name);
                }

                sources.push_back(source);
                names.push_back(label);
            }

            scanned = true;
        }

        bool open(int idx)
        {
            close();
            if (idx < 0 || idx >= static_cast<int>(sources.size())) return false;

            MIDIClientRef newClient = 0;
            if (MIDIClientCreate(CFSTR("EasyMix"), nullptr, nullptr, &newClient) != noErr)
                return false;

            MIDIPortRef newPort = 0;
            if (MIDIInputPortCreate(newClient, CFSTR("EasyMix In"), &MidiIn::ReadProc, this, &newPort) != noErr)
            {
                MIDIClientDispose(newClient);
                return false;
            }

            if (MIDIPortConnectSource(newPort, sources[idx], nullptr) != noErr)
            {
                MIDIPortDispose(newPort);
                MIDIClientDispose(newClient);
                return false;
            }

            client = newClient;
            port = newPort;
            selected = idx;
            return true;
        }

        void close()
        {
            if (port) { MIDIPortDispose(port); port = 0; }
            if (client) { MIDIClientDispose(client); client = 0; }
            selected = -1;
        }

#endif

        std::vector<MidiMsg> drain()
        {
            std::lock_guard<std::mutex> lk(mtx);
            std::vector<MidiMsg> r;
            r.swap(q);
            return r;
        }

    private:
#ifdef _WIN32
        HMIDIIN h = nullptr;
#else
        std::vector<MIDIEndpointRef> sources;
        MIDIClientRef client = 0;
        MIDIPortRef port = 0;

        static void ReadProc(const MIDIPacketList* list, void* refCon, void*)
        {
            auto* self = static_cast<MidiIn*>(refCon);
            if (self) ParseMidiPacketList(list, self->mtx, self->q);
        }
#endif
        std::mutex mtx;
        std::vector<MidiMsg> q;

#ifdef _WIN32
        static void CALLBACK Proc(HMIDIIN, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR)
        {
            if (msg != MIM_DATA) return;
            auto* self = reinterpret_cast<MidiIn*>(inst);
            const int status = static_cast<int>(p1 & 0xFF), cmd = status & 0xF0;
            const int d1 = static_cast<int>((p1 >> 8) & 0x7F), d2 = static_cast<int>((p1 >> 16) & 0x7F);
            if (cmd != 0x80 && cmd != 0x90) return;
            std::lock_guard<std::mutex> lk(self->mtx);
            if (self->q.size() < 4096)
                self->q.push_back({ cmd == 0x90 && d2 > 0, d1, cmd == 0x90 && d2 > 0 ? d2 / 127.0f : 0.0f });
        }
#endif
    };

    // Collects MIDI-in and PC keyboard notes. Call once per frame on the UI thread.
    inline void PollLive(LiveState& L, MidiIn& in, std::vector<MidiMsg>& out)
    {
        for (auto& m : in.drain()) out.push_back(m);

        ImGuiIO& io = ImGui::GetIO();
        const bool allow = L.kbdPiano && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt;
        static const ImGuiKey keys[16] = {
            ImGuiKey_A, ImGuiKey_W, ImGuiKey_S, ImGuiKey_E, ImGuiKey_D, ImGuiKey_F, ImGuiKey_T, ImGuiKey_G,
            ImGuiKey_Y, ImGuiKey_H, ImGuiKey_U, ImGuiKey_J, ImGuiKey_K, ImGuiKey_O, ImGuiKey_L, ImGuiKey_P };

        auto releaseAll = [&]()
            {
                for (int i = 0; i < 16; ++i)
                    if (L.keyOn[i]) { out.push_back({ false, L.keyPitch[i], 0.0f }); L.keyOn[i] = false; }
            };

        if (allow)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) { releaseAll(); L.octave = std::max(0, L.octave - 1); }
            if (ImGui::IsKeyPressed(ImGuiKey_X, false)) { releaseAll(); L.octave = std::min(8, L.octave + 1); }
        }
        for (int i = 0; i < 16; ++i)
        {
            const bool down = allow && ImGui::IsKeyDown(keys[i]);
            if (down == L.keyOn[i]) continue;
            L.keyOn[i] = down;
            if (down)
            {
                L.keyPitch[i] = std::clamp(12 * (L.octave + 1) + i, 0, 127);
                out.push_back({ true, L.keyPitch[i], 0.8f });
            }
            else out.push_back({ false, L.keyPitch[i], 0.0f });
        }
    }

    inline void DrawMidiPanel(LiveState& L, MidiIn& in)
    {
        if (!in.scanned) in.refresh();
        if (ImGui::Button("Refresh devices")) in.refresh();
        const char* prev = (in.selected >= 0 && in.selected < static_cast<int>(in.names.size()))
            ? in.names[in.selected].c_str() : "(none)";
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##midiin", prev))
        {
            if (ImGui::Selectable("(none)", in.selected < 0)) in.close();
            for (int i = 0; i < static_cast<int>(in.names.size()); ++i)
            {
                ImGui::PushID(i);
                if (ImGui::Selectable(in.names[i].c_str(), in.selected == i)) in.open(i);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::Separator();
        ImGui::Checkbox("PC keyboard piano (A W S E D F T G Y H U J K O L P)", &L.kbdPiano);
        ImGui::Text("Octave: %d   (Z: down / X: up)", L.octave);
        ImGui::TextDisabled("While ON, single-key shortcuts (S/D/L/C/M/I/O...) are disabled");
        ImGui::Separator();
        ImGui::Checkbox("Record MIDI while playing (selected track)", &L.midiRec);
        ImGui::TextDisabled("Press Play, play notes, press Stop. Use 'Rec latency' to compensate delay.");
    }

    inline float MeasureLufs(const std::vector<float>& s, unsigned sr)
    {
        extras::LufsMeter m;
        m.setup(sr);
        m.process(s.data(), static_cast<unsigned>(s.size() / 2));
        m.computeIntegrated();
        return m.integrated.load();
    }

    // Returns the applied gain in dB (0 for silence). Never exceeds ceilingDb peak.
    inline float NormalizeToLufs(std::vector<float>& s, unsigned sr, float target, float ceilingDb = -1.0f)
    {
        const float cur = MeasureLufs(s, sr);
        if (cur <= -69.0f) return 0.0f;
        float peak = 1.0e-6f;
        for (float v : s) peak = std::max(peak, std::fabs(v));
        float g = std::pow(10.0f, (target - cur) / 20.0f);
        g = std::min(g, std::pow(10.0f, ceilingDb / 20.0f) / peak);
        for (float& v : s) v *= g;
        return 20.0f * std::log10(g);
    }

    inline std::string NextTakePath(const std::string& projectPath)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path dir = projectPath.empty()
            ? fs::path("Recordings")
            : fs::path(projectPath).parent_path() / (fs::path(projectPath).stem().string() + "_takes");
        fs::create_directories(dir, ec);
        for (int i = 1; i < 10000; ++i)
        {
            fs::path p = dir / ("take_" + std::to_string(i) + ".wav");
            if (!fs::exists(p, ec)) return p.string();
        }
        return (dir / "take.wav").string();
    }
}