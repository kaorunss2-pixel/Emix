#define GLFW_INCLUDE_NONE
#define NOMINMAX

#ifdef _WIN32
#include <Windows.h>

#ifndef CALLBACK
#define CALLBACK __stdcall
#endif
#include "Equalizer.h"
#include <commdlg.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#else
#define GL_SILENCE_DEPRECATION
#include "Platform.h"    // macOS compatibility layer (file dialogs, message box, ...)
#include "Equalizer.h"
#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "PianoRoll.h"   // MidiNote / DrawPianoRoll / RenderMidiBuiltin
#include "Vst3Host.h"    // Vst3Plugin / ScanVst3Folders / VstNote
#include <cctype>
#include "Effects.h"     // built-in effects (streaming)
#include "BuiltinSynth.h" // built-in instruments (113 presets, GM drum kits)
#include "CubaseStyle.h" // Cubase-like look + helpers
#include "MidiFile.h"    // Standard MIDI File import / export
#include "EasyMixUi.h"   // EQ graph / spectrum / meters / toasts / command palette

#include <vector>
#include <string>
#include <cstring>
#include <memory>
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstdint>
#include <unordered_map>
#if defined(_WIN32) && !defined(EASYMIX_CMAKE)
// Visual Studio / vcpkg build (CMake links these libraries itself)
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "comdlg32.lib")
#ifdef _DEBUG
#define VST3_LIB_DIR "C:/vcpkg/installed/x64-windows/debug/lib/vst3sdk/"
#else
#define VST3_LIB_DIR "C:/vcpkg/installed/x64-windows/lib/vst3sdk/"
#endif
#pragma comment(lib, VST3_LIB_DIR "sdk_hosting.lib")
#pragma comment(lib, VST3_LIB_DIR "sdk_common.lib")
#pragma comment(lib, VST3_LIB_DIR "sdk.lib")
#pragma comment(lib, VST3_LIB_DIR "base.lib")
#pragma comment(lib, VST3_LIB_DIR "pluginterfaces.lib")
#pragma comment(lib, "ole32.lib")
#endif

#ifdef __APPLE__
// The Windows build used glfwGetWin32Window(); on macOS the "owner" is the NSWindow.
static inline void* glfwGetWin32Window(GLFWwindow* w)
{
    return (void*)glfwGetCocoaWindow(w);
}
#endif

void ApplyEasyMixStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.CellPadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(9.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(7.0f, 6.0f);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);

    style.WindowRounding = 9.0f;
    style.ChildRounding = 7.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 7.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 12.0f;

    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.93f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.43f, 0.49f, 0.57f, 1.00f);

    colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.067f, 0.090f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.088f, 0.115f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.085f, 0.100f, 0.130f, 0.98f);

    colors[ImGuiCol_Border] = ImVec4(0.17f, 0.20f, 0.25f, 0.75f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.15f, 0.19f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.21f, 0.26f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.25f, 0.30f, 1.00f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.065f, 0.078f, 0.105f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.085f, 0.105f, 0.135f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.055f, 0.067f, 0.090f, 1.00f);

    colors[ImGuiCol_MenuBarBg] = ImVec4(0.075f, 0.090f, 0.115f, 1.00f);

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.045f, 0.055f, 0.075f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.27f, 0.35f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.25f, 0.65f, 0.62f, 1.00f);

    colors[ImGuiCol_CheckMark] = ImVec4(0.25f, 0.85f, 0.75f, 1.00f);

    colors[ImGuiCol_SliderGrab] = ImVec4(0.20f, 0.72f, 0.66f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.30f, 0.90f, 0.80f, 1.00f);

    colors[ImGuiCol_Button] = ImVec4(0.13f, 0.19f, 0.23f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.34f, 0.36f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.55f, 0.52f, 1.00f);

    colors[ImGuiCol_Header] = ImVec4(0.13f, 0.25f, 0.27f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.17f, 0.38f, 0.38f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.48f, 0.45f, 1.00f);

    colors[ImGuiCol_Separator] = ImVec4(0.18f, 0.22f, 0.27f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.22f, 0.65f, 0.60f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.28f, 0.85f, 0.75f, 1.00f);

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.20f, 0.65f, 0.60f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.20f, 0.75f, 0.68f, 0.70f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.25f, 0.90f, 0.80f, 1.00f);

    colors[ImGuiCol_Tab] = ImVec4(0.09f, 0.12f, 0.15f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.18f, 0.40f, 0.39f, 1.00f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.13f, 0.27f, 0.28f, 1.00f);

    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.11f, 0.14f, 0.18f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.20f, 0.24f, 0.29f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.14f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.025f);
}

static constexpr unsigned kEngineRate = 44100;
static constexpr int kBlock = 512;

struct AudioClip
{
    std::string name;
    std::string path;

    float startSeconds = 0.0f;
    float duration = 0.0f;

    int trackIndex = 0;

    ma_uint32 sampleRate = kEngineRate;

    float sourceOffsetSeconds = 0.0f;
    float gain = 1.0f;

    float fadeInSeconds = 0.0f;
    float fadeOutSeconds = 0.0f;

    std::vector<float> waveform;

    std::shared_ptr<std::vector<float>> pcm;
};

struct AutomationPoint
{
    float time = 0.0f;
    float value = 1.0f;
};
struct Marker
{
    float beat = 0.0f;
    std::string name;
};

struct BusSettings
{
    bool on = true;
    int index = 1;          // index into kBusTypes
    float ret = 0.8f;
    float amount = 0.5f;
    float time = 0.3f;
    float feedback = 0.35f;
};

static const EffectType kBusTypes[] =
{
    EffectType::RoomReverb, EffectType::HallReverb, EffectType::PlateReverb, EffectType::SpringReverb,
    EffectType::Delay, EffectType::PingPongDelay, EffectType::Echo
};
static const char* const kBusNames[] =
{
    "Room Reverb", "Hall Reverb", "Plate Reverb", "Spring Reverb",
    "Delay", "Ping Pong Delay", "Echo"
};
static bool BusIsReverb(int index) { return index >= 0 && index < 4; }

struct RecordingState
{
    ma_encoder encoder;
    std::atomic<bool> active{ false };
};

struct RTContext
{
    std::mutex mtx;
    std::function<void(float*, ma_uint32)> render;
};

struct ProjectSnapshot
{
    std::vector<std::string> tracks;
    std::vector<float> volumes;
    std::vector<float> pans;
    std::vector<bool> muted;
    std::vector<bool> solo;
    std::vector<Equalizer> eqs;
    std::vector<EffectGraph> graphs;
    std::vector<std::vector<AutomationPoint>> automation;
    std::vector<std::vector<std::shared_ptr<Vst3Plugin>>> vst;
    std::vector<AudioClip> clips;
    std::vector<MidiNote> notes;
    std::vector<Marker> markers;
    std::vector<float> sends;
    std::vector<synth::Setup> synths;
    BusSettings bus;
    float bpm = 120.0f;
    float master = 1.0f;
};

static std::string BytesToHex(const std::vector<unsigned char>& bytes)
{
    static const char* digits = "0123456789abcdef";
    std::string s;
    s.reserve(bytes.size() * 2);
    for (unsigned char b : bytes)
    {
        s.push_back(digits[b >> 4]);
        s.push_back(digits[b & 15]);
    }
    return s;
}

static std::vector<unsigned char> HexToBytes(const std::string& s)
{
    auto val = [](char c) -> int
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };

    std::vector<unsigned char> bytes;
    if (s == "-")
        return bytes;

    bytes.reserve(s.size() / 2);
    for (size_t i = 0; i + 1 < s.size(); i += 2)
    {
        const int hi = val(s[i]);
        const int lo = val(s[i + 1]);
        if (hi < 0 || lo < 0)
        {
            bytes.clear();
            return bytes;
        }
        bytes.push_back(static_cast<unsigned char>(hi * 16 + lo));
    }
    return bytes;
}

static std::shared_ptr<std::vector<float>> LoadPcm(const std::string& path)
{
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, kEngineRate);
    ma_decoder dec;
    if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS)
        return nullptr;

    auto pcm = std::make_shared<std::vector<float>>();
    std::vector<float> tmp(4096 * 2);

    for (;;)
    {
        ma_uint64 got = 0;
        ma_result r = ma_decoder_read_pcm_frames(&dec, tmp.data(), 4096, &got);
        if (got > 0)
            pcm->insert(pcm->end(), tmp.begin(), tmp.begin() + static_cast<size_t>(got) * 2);
        if (r != MA_SUCCESS || got == 0)
            break;
    }

    ma_decoder_uninit(&dec);

    if (pcm->empty())
        return nullptr;

    return pcm;
}

void recordingCallback(
    ma_device* device,
    void* output,
    const void* input,
    ma_uint32 frameCount
)
{
    auto* state =
        static_cast<RecordingState*>(device->pUserData);

    if (!input || !state->active.load())
        return;

    ma_uint64 framesWritten = 0;

    ma_encoder_write_pcm_frames(
        &state->encoder,
        input,
        frameCount,
        &framesWritten
    );
}

void realtimeAudioCallback(
    ma_device* device,
    void* output,
    const void* input,
    ma_uint32 frameCount)
{
    (void)input;

    float* out = static_cast<float*>(output);
    if (!out)
        return;

    std::memset(out, 0, static_cast<size_t>(frameCount) * 2 * sizeof(float));

    auto* ctx = static_cast<RTContext*>(device->pUserData);
    if (!ctx)
        return;

    std::lock_guard<std::mutex> lock(ctx->mtx);

    if (ctx->render)
        ctx->render(out, frameCount);
}

int main()
{
    float bpm = 120.0f;
    float timelineZoom = 80.0f;
    float playheadPosition = 0.0f;

    int timelineBars = 16;
    int selectedTrack = -1;

    int draggingClipIndex = -1;
    float dragOffsetPixels = 0.0f;
    bool draggingAudioClip = false;

    bool playing = false;
    bool renamingTrack = false;
    bool showNodeEditor = false;

    bool loopEnabled = false;
    float loopStartBeat = 0.0f;
    float loopEndBeat = 16.0f;
    bool metronomeOn = false;
    float metronomeVolume = 0.5f;
    bool followPlayhead = true;
    bool exporting = false;
    int exportBits = 1;
    float recordLatencyMs = 0.0f;
    float prevTimelineZoom = timelineZoom;
    float prevBpm = bpm;
    AudioClip* selectedClipPtr = nullptr;
    int clipDragMode = 0;
    float dragOrigStart = 0.0f;
    float dragOrigDuration = 0.0f;
    float dragOrigOffset = 0.0f;
    bool projectDirty = false;
    bool lastTitleDirty = false;
    bool resetLayoutFlag = false;

    char renameBuffer[128] = {};

    std::vector<std::string> tracks = {
        "Vocal",
        "Drums",
        "Bass"
    };

    if (!glfwInit())
        return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);   // required on macOS
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(
        1280,
        720,
        "EasyMix Studio 1.0",
        nullptr,
        nullptr
    );

    if (!window)
    {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

#ifdef __APPLE__
    // keep the layout file in ~/Library/Application Support/EasyMix (cwd is not writable in an .app)
    static std::string imguiIniPath = plat::SupportDir().empty()
        ? std::string("imgui.ini")
        : plat::SupportDir() + "/imgui.ini";
    io.IniFilename = imguiIniPath.c_str();
#endif

    ApplyCubaseStyle();

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true))
    {
        MessageBoxA(nullptr, "ImGui GLFW initialization failed!", "Error", MB_OK | MB_ICONERROR);
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    if (!ImGui_ImplOpenGL3_Init("#version 330"))
    {
        MessageBoxA(
            nullptr,
            "ImGui OpenGL3 initialization failed!",
            "Error",
            MB_OK | MB_ICONERROR
        );

        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    std::vector<float> trackVolumes(tracks.size(), 1.0f);
    std::vector<bool> trackMuted(tracks.size(), false);
    std::vector<bool> trackSolo(tracks.size(), false);
    std::vector<float> trackPans(tracks.size(), 0.0f);
    std::vector<std::vector<AutomationPoint>> trackAutomation(tracks.size());
    float masterVolume = 1.0f;
    std::string currentProjectPath;
    int contextClipIndex = -1;
    bool showMixer = true;
    bool showAutomation = true;
    std::vector<std::unique_ptr<AudioClip>> audioClips;
    std::vector<MidiNote> midiNotes;
    std::vector<Marker> markers;
    bool showMarkers = true;
    bool showBus = false;

    PianoRollState pianoRoll;
    std::vector<std::vector<std::shared_ptr<Vst3Plugin>>> trackVst(tracks.size());
    std::vector<std::string> vst3Found;
    bool showVstRack = true;
    int pianoRollVstSelection = 0;
    bool showPianoRoll = false;
    bool snapEnabled = true;

    float snapBeat = 0.25f;

    const float snapOptions[] =
    {
        1.0f,
        0.5f,
        0.25f,
        0.125f,
        0.0625f
    };

    const char* snapOptionNames[] =
    {
        "1/4",
        "1/8",
        "1/16",
        "1/32",
        "1/64"
    };
    auto snapToGrid = [&](float beatPosition)
        {
            if (!snapEnabled || snapBeat <= 0.0f)
                return beatPosition;

            return std::round(beatPosition / snapBeat) * snapBeat;
        };

    auto buildWaveform = [&](AudioClip& clip)
        {
            clip.waveform.clear();
            if (!clip.pcm)
                return;

            const long long total = static_cast<long long>(clip.pcm->size() / 2);
            const long long off = static_cast<long long>(std::max(0.0f, clip.sourceOffsetSeconds) * kEngineRate);
            long long len = static_cast<long long>(std::max(0.0f, clip.duration) * kEngineRate);

            if (off >= total || len <= 0)
                return;

            len = std::min(len, total - off);

            const long long bucket = std::max<long long>(1, (len + 511) / 512);
            const float* src = clip.pcm->data();

            for (long long b = 0; b < len; b += bucket)
            {
                const long long e = std::min(len, b + bucket);
                float peak = 0.0f;
                for (long long f = b; f < e; ++f)
                {
                    const size_t i = static_cast<size_t>(off + f) * 2;
                    peak = std::max(peak, std::max(std::abs(src[i]), std::abs(src[i + 1])));
                }
                clip.waveform.push_back(peak);
            }
        };
    std::vector<Equalizer> trackEQs(tracks.size());

    EffectNode masterLimiter;
    masterLimiter.type = EffectType::Limiter;
    masterLimiter.amount = 0.0f;
    masterLimiter.wet = 1.0f;
    bool masterLimiterOn = true;
    std::vector<float> trackSends(tracks.size(), 0.0f);
    BusSettings bus;
    EffectNode busFx;
    int busAppliedIndex = -1;
    std::vector<float> busBuf;
    std::vector<float> busDry;
    float masterPeakL = 0.0f;
    float masterPeakR = 0.0f;
    std::vector<float> trackPeak;
    std::vector<EffectGraph> effectGraphs(tracks.size());
    std::string audioError;
    SpectrumAnalyzer spectrum;
    Toasts toasts;
    CommandPalette palette;
    PeakHold phMasterL, phMasterR;
    std::vector<PeakHold> trackHold;
    bool showHelp = false;
    std::vector<AudioClip> clipClipboard;
    std::vector<synth::Setup> trackSynth;
    synth::Engine previewEng;
    double previewOffTime = 0.0;
    auto synthSetup = [&](int t) -> synth::Setup&
        {
            if (t < 0) t = 0;
            if (static_cast<int>(trackSynth.size()) <= t) trackSynth.resize(static_cast<size_t>(t) + 1);
            return trackSynth[static_cast<size_t>(t)];
        };
    auto applyDefaultInstruments = [&]()
        {
            trackSynth.assign(tracks.size(), synth::Setup());
            for (size_t i = 0; i < tracks.size(); ++i)
            {
                if (tracks[i] == "Drums") trackSynth[i].preset = synth::FindPreset("Acoustic Kit");
                else if (tracks[i] == "Bass") trackSynth[i].preset = synth::FindPreset("Fingered Bass");
            }
        };
    applyDefaultInstruments();
    bool autoSave = true;
    double lastAutoSave = 0.0;

    auto addEffectToSelectedTrack = [&](EffectType type)
        {
            if (selectedTrack < 0 ||
                selectedTrack >= static_cast<int>(effectGraphs.size()))
            {
                audioError = "Select a track before adding an effect.";
                return;
            }

            EffectGraph& graph = effectGraphs[selectedTrack];

            EffectNode node;
            node.id = graph.nextId++;
            node.type = type;
            node.position = ImVec2(
                210.0f + static_cast<float>(graph.nodes.size()) * 180.0f,
                140.0f
            );
            node.amount = 0.5f;
            node.wet = EffectIsTimeBased(type) ? 0.35f : 1.0f;
            node.frequency = 1200.0f;
            node.time = 0.25f;
            node.feedback = 0.25f;

            int predecessor = 1;
            auto outputLink = std::find_if(
                graph.links.begin(),
                graph.links.end(),
                [](const EffectLink& link) { return link.to == 2; }
            );

            if (outputLink != graph.links.end())
            {
                predecessor = outputLink->from;
                outputLink->to = node.id;
            }
            else
            {
                graph.links.push_back({ predecessor, node.id });
            }

            graph.links.push_back({ node.id, 2 });
            graph.nodes.push_back(std::move(node));
            graph.selectedNode = graph.nodes.back().id;
            showNodeEditor = true;
            audioError.clear();
        };

    auto automationGain = [&](int track, float t) -> float
        {
            if (track < 0 || track >= static_cast<int>(trackAutomation.size())) return 1.0f;
            const auto& pts = trackAutomation[track];
            if (pts.empty()) return 1.0f;
            if (t <= pts.front().time) return pts.front().value;
            if (t >= pts.back().time) return pts.back().value;
            for (size_t p = 1; p < pts.size(); ++p)
            {
                if (t <= pts[p].time)
                {
                    float span = std::max(0.0001f, pts[p].time - pts[p - 1].time);
                    float a = (t - pts[p - 1].time) / span;
                    return pts[p - 1].value * (1.0f - a) + pts[p].value * a;
                }
            }
            return 1.0f;
        };

    auto mixGains = [&](int track, float timeSec, bool hasSolo, float& gl, float& gr)
        {
            gl = gr = 0.0f;
            if (track < 0 || track >= static_cast<int>(tracks.size())) return;
            float v = trackVolumes[track] * automationGain(track, timeSec) * masterVolume;
            if (trackMuted[track] || (hasSolo && !trackSolo[track])) v = 0.0f;
            float p = trackPans[track];
            gl = v * (p > 0.0f ? 1.0f - p : 1.0f);
            gr = v * (p < 0.0f ? 1.0f + p : 1.0f);
        };

    RTContext rt;
    std::unique_lock<std::mutex> uiLock(rt.mtx, std::defer_lock);

    auto DialogOpen = [&](OPENFILENAMEA* d) -> BOOL
        {
            const bool wasLocked = uiLock.owns_lock();
            if (wasLocked) uiLock.unlock();
            BOOL r = GetOpenFileNameA(d);
            if (wasLocked) uiLock.lock();
            return r;
        };
    auto DialogSave = [&](OPENFILENAMEA* d) -> BOOL
        {
            const bool wasLocked = uiLock.owns_lock();
            if (wasLocked) uiLock.unlock();
            BOOL r = GetSaveFileNameA(d);
            if (wasLocked) uiLock.lock();
            return r;
        };

    struct ActiveNote { int pitch; long long endFrame; };
    struct TrackRT
    {
        std::vector<ActiveNote> active;
        synth::Engine eng;
    };
    struct MidiEv { int offset; bool on; int pitch; float vel; };

    std::vector<TrackRT> trackRT;
    std::vector<MidiEv> evScratch;
    evScratch.reserve(1024);
    std::vector<std::vector<float>> trackBuf;
    std::vector<float> instBuf(static_cast<size_t>(kBlock) * 2, 0.0f);

    long long playFrame = 0;
    long long contentEndFrame = 0;
    bool seekRequested = false;
    bool needFlush = false;
    bool rtDeviceOk = false;

    auto pixelsPerSecond = [&]() -> double
        {
            return static_cast<double>(timelineZoom) * static_cast<double>(bpm) / 240.0;
        };
    auto frameFromPlayhead = [&]() -> long long
        {
            const double pps = pixelsPerSecond();
            if (pps <= 0.0) return 0;
            return static_cast<long long>(std::llround(
                std::max(0.0, static_cast<double>(playheadPosition)) / pps * kEngineRate));
        };
    auto playheadFromFrame = [&](long long frame) -> float
        {
            return static_cast<float>(static_cast<double>(frame) / kEngineRate * pixelsPerSecond());
        };

    auto computeContentEnd = [&]() -> long long
        {
            double sec = 0.0;
            for (const auto& c : audioClips)
                if (c->trackIndex >= 0)
                    sec = std::max(sec, static_cast<double>(c->startSeconds) + c->duration);
            if (bpm > 0.0f)
                for (const auto& n : midiNotes)
                    sec = std::max(sec, (n.startBeat + n.lengthBeats) * 60.0 / bpm);
            return static_cast<long long>(sec * kEngineRate);
        };

    auto flushPlugins = [&]()
        {
            for (auto& chain : trackVst)
                for (auto& p : chain)
                    if (p && p->isLoaded())
                        p->resetState();

            for (auto& r : trackRT)
            {
                r.active.clear();
                r.eng.Reset();
            }
            for (auto& g : effectGraphs) ResetEffectGraph(g);
            for (auto& eq : trackEQs) eq.reset();
        };

    auto softSeekReset = [&]()
        {
            for (int t = 0; t < static_cast<int>(trackRT.size()); ++t)
            {
                auto& r = trackRT[t];
                if (!r.active.empty() && t < static_cast<int>(trackVst.size()))
                    for (auto& p : trackVst[t])
                        if (p && p->isLoaded() && p->instrument)
                            p->resetState();
                r.active.clear();
                r.eng.Reset();
            }
        };

    auto stopAllClips = [&]()
        {
            playing = false;
            flushPlugins();
        };

    auto clearMidiPlayback = [&]()
        {
            for (auto& r : trackRT)
            {
                r.active.clear();
                r.eng.Reset();
            }
        };
    auto destroyClip = [&](AudioClip&) {};

    auto resetProject = [&]()
        {
            stopAllClips();
            clearMidiPlayback();
            playing = false;
            audioClips.clear();
            midiNotes.clear();
            trackVst.clear();
            trackRT.clear();
            trackBuf.clear();
            markers.clear();
            currentProjectPath.clear();
            playheadPosition = 0.0f;
            tracks = { "Vocal", "Drums", "Bass" };
            trackVolumes.assign(3, 1.0f);
            trackPans.assign(3, 0.0f);
            trackSends.assign(3, 0.0f);
            bus = BusSettings();
            trackMuted.assign(3, false);
            trackSolo.assign(3, false);
            trackEQs.assign(3, Equalizer{});
            effectGraphs.assign(3, EffectGraph{});
            trackAutomation.assign(3, std::vector<AutomationPoint>{});
            trackVst.resize(3);
            applyDefaultInstruments();
            pianoRoll = PianoRollState();
            trackPeak.clear();
            selectedClipPtr = nullptr;
            selectedTrack = 0;
            renamingTrack = false;
            contextClipIndex = -1;
            draggingClipIndex = -1;
            draggingAudioClip = false;
        };

    auto projectHash = [&]() -> std::uint64_t
        {
            std::uint64_t h = 1469598103934665603ULL;
            auto mix = [&](const void* p, size_t n)
                {
                    const unsigned char* b = static_cast<const unsigned char*>(p);
                    for (size_t i = 0; i < n; ++i)
                    {
                        h ^= b[i];
                        h *= 1099511628211ULL;
                    }
                };
            auto mf = [&](float v) { mix(&v, sizeof(v)); };
            auto mi = [&](long long v) { mix(&v, sizeof(v)); };

            mf(bpm);
            mf(masterVolume);
            mi(bus.on ? 1 : 0); mi(bus.index); mf(bus.ret); mf(bus.amount); mf(bus.time); mf(bus.feedback);
            for (const auto& mk : markers)
            {
                mf(mk.beat);
                mix(mk.name.data(), mk.name.size());
            }
            mi(static_cast<long long>(tracks.size()));

            for (size_t t = 0; t < tracks.size(); ++t)
            {
                mix(tracks[t].data(), tracks[t].size());
                if (t < trackVolumes.size()) mf(trackVolumes[t]);
                if (t < trackPans.size()) mf(trackPans[t]);
                if (t < trackSends.size()) mf(trackSends[t]);
                if (t < trackSynth.size())
                {
                    mi(trackSynth[t].preset); mf(trackSynth[t].gain); mf(trackSynth[t].bright);
                    mf(trackSynth[t].attack); mf(trackSynth[t].release);
                }
                if (t < trackMuted.size()) mi(trackMuted[t] ? 1 : 0);
                if (t < trackSolo.size()) mi(trackSolo[t] ? 1 : 0);

                if (t < trackEQs.size())
                {
                    const Equalizer& eq = trackEQs[t];
                    mi(eq.enabled ? 1 : 0);
                    for (int b = 0; b < Equalizer::BandCount; ++b)
                    {
                        mf(eq.frequencies[b]);
                        mf(eq.gains[b]);
                        mf(eq.qValues[b]);
                    }
                }

                if (t < effectGraphs.size())
                {
                    const EffectGraph& g = effectGraphs[t];
                    mi(g.nextId);
                    for (const auto& n : g.nodes)
                    {
                        mi(n.id);
                        mi(static_cast<int>(n.type));
                        mf(n.amount);
                        mf(n.wet);
                        mf(n.frequency);
                        mf(n.time);
                        mf(n.feedback);
                        mi(n.enabled ? 1 : 0);
                        mf(n.position.x);
                        mf(n.position.y);
                    }
                    for (const auto& l : g.links)
                    {
                        mi(l.from);
                        mi(l.to);
                    }
                }

                if (t < trackAutomation.size())
                    for (const auto& a : trackAutomation[t])
                    {
                        mf(a.time);
                        mf(a.value);
                    }

                if (t < trackVst.size())
                    for (const auto& p : trackVst[t])
                        mi(static_cast<long long>(reinterpret_cast<std::uintptr_t>(p.get())));
            }

            for (const auto& c : audioClips)
            {
                mix(c->name.data(), c->name.size());
                mf(c->startSeconds);
                mf(c->duration);
                mi(c->trackIndex);
                mf(c->sourceOffsetSeconds);
                mf(c->gain);
                mf(c->fadeInSeconds);
                mf(c->fadeOutSeconds);
                mi(static_cast<long long>(reinterpret_cast<std::uintptr_t>(c->pcm.get())));
            }

            for (const auto& n : midiNotes)
            {
                mi(n.id);
                mi(n.trackIndex);
                mi(n.pitch);
                mf(n.startBeat);
                mf(n.lengthBeats);
                mf(n.velocity);
            }

            return h;
        };

    auto captureSnapshot = [&]() -> ProjectSnapshot
        {
            ProjectSnapshot s;
            s.tracks = tracks;
            s.volumes = trackVolumes;
            s.pans = trackPans;
            s.sends = trackSends;
            s.synths = trackSynth;
            s.bus = bus;
            s.muted = trackMuted;
            s.solo = trackSolo;
            s.eqs = trackEQs;
            s.graphs = effectGraphs;
            for (auto& g : s.graphs)
                ResetEffectGraph(g);
            s.automation = trackAutomation;
            s.vst = trackVst;
            for (const auto& c : audioClips)
                s.clips.push_back(*c);
            s.notes = midiNotes;
            s.markers = markers;
            s.bpm = bpm;
            s.master = masterVolume;
            return s;
        };

    auto restoreSnapshot = [&](const ProjectSnapshot& s)
        {
            tracks = s.tracks;
            trackVolumes = s.volumes;
            trackPans = s.pans;
            trackSends = s.sends;
            trackSynth = s.synths;
            previewEng.Reset();
            bus = s.bus;
            trackMuted = s.muted;
            trackSolo = s.solo;
            trackEQs = s.eqs;
            effectGraphs = s.graphs;
            trackAutomation = s.automation;
            trackVst = s.vst;

            audioClips.clear();
            for (const auto& c : s.clips)
                audioClips.push_back(std::make_unique<AudioClip>(c));

            midiNotes = s.notes;
            markers = s.markers;
            bpm = s.bpm;
            masterVolume = s.master;

            trackRT.clear();
            trackBuf.clear();
            trackPeak.clear();

            for (auto& chain : trackVst)
                for (auto& p : chain)
                    if (p && p->isLoaded())
                        p->resetState();

            selectedClipPtr = nullptr;
            contextClipIndex = -1;
            draggingClipIndex = -1;
            draggingAudioClip = false;
            renamingTrack = false;
            pianoRoll.selectedId = -1;
            pianoRoll.dragId = -1;

            if (selectedTrack >= static_cast<int>(tracks.size()))
                selectedTrack = static_cast<int>(tracks.size()) - 1;
        };

    std::vector<ProjectSnapshot> undoStack;
    std::vector<ProjectSnapshot> redoStack;
    ProjectSnapshot baseSnapshot;
    std::uint64_t stableHash = 0;
    std::uint64_t savedHash = 0;
    bool undoInit = false;

    auto doUndo = [&]()
        {
            if (undoStack.empty() || !undoInit)
                return;

            redoStack.push_back(std::move(baseSnapshot));
            baseSnapshot = std::move(undoStack.back());
            undoStack.pop_back();
            restoreSnapshot(baseSnapshot);
            stableHash = projectHash();
            audioError = "Undo";
        };

    auto doRedo = [&]()
        {
            if (redoStack.empty() || !undoInit)
                return;

            undoStack.push_back(std::move(baseSnapshot));
            baseSnapshot = std::move(redoStack.back());
            redoStack.pop_back();
            restoreSnapshot(baseSnapshot);
            stableHash = projectHash();
            audioError = "Redo";
        };

    auto splitClipAtPlayhead = [&](AudioClip* c)
        {
            if (!c || !c->pcm)
                return;

            const float splitTime = playheadPosition * 240.0f / (timelineZoom * bpm);
            const float local = splitTime - c->startSeconds;
            if (local <= 0.02f || local >= c->duration - 0.02f)
                return;

            auto right = std::make_unique<AudioClip>();
            right->path = c->path;
            right->name = c->name + " (2)";
            right->startSeconds = splitTime;
            right->duration = c->duration - local;
            right->trackIndex = c->trackIndex;
            right->sampleRate = c->sampleRate;
            right->sourceOffsetSeconds = c->sourceOffsetSeconds + local;
            right->gain = c->gain;
            right->fadeOutSeconds = c->fadeOutSeconds;
            right->pcm = c->pcm;

            c->duration = local;
            c->fadeOutSeconds = 0.0f;
            buildWaveform(*c);
            buildWaveform(*right);
            audioClips.push_back(std::move(right));
        };

    auto duplicateClip = [&](AudioClip* c)
        {
            if (!c || !c->pcm)
                return;

            auto copy = std::make_unique<AudioClip>();
            copy->path = c->path;
            copy->name = c->name + " Copy";
            copy->startSeconds = c->startSeconds + c->duration;
            copy->duration = c->duration;
            copy->trackIndex = c->trackIndex;
            copy->sampleRate = c->sampleRate;
            copy->sourceOffsetSeconds = c->sourceOffsetSeconds;
            copy->gain = c->gain;
            copy->fadeInSeconds = c->fadeInSeconds;
            copy->fadeOutSeconds = c->fadeOutSeconds;
            copy->pcm = c->pcm;
            buildWaveform(*copy);
            selectedClipPtr = copy.get();
            audioClips.push_back(std::move(copy));
        };

    auto renderChunk = [&](long long start, int n, float* out)
        {
            std::fill(out, out + static_cast<size_t>(n) * 2, 0.0f);
            if (n <= 0 || bpm <= 0.0f)
                return;

            const int trackCount = static_cast<int>(tracks.size());
            if (static_cast<int>(trackRT.size()) != trackCount) trackRT.resize(trackCount);
            if (static_cast<int>(trackBuf.size()) != trackCount) trackBuf.resize(trackCount);
            if (static_cast<int>(trackPeak.size()) != trackCount) trackPeak.assign(trackCount, 0.0f);

            const bool hasSolo = std::any_of(trackSolo.begin(), trackSolo.end(), [](bool s) { return s; });
            const long long end = start + n;
            const double bpmD = static_cast<double>(bpm);
            const double framesPerBeat = 60.0 / bpmD * kEngineRate;

            busBuf.assign(static_cast<size_t>(n) * 2, 0.0f);

            for (int t = 0; t < trackCount; ++t)
            {
                if (t >= static_cast<int>(trackEQs.size()) ||
                    t >= static_cast<int>(effectGraphs.size()) ||
                    t >= static_cast<int>(trackVst.size()))
                    continue;

                std::vector<float>& tb = trackBuf[t];
                if (tb.size() < static_cast<size_t>(kBlock) * 2)
                    tb.assign(static_cast<size_t>(kBlock) * 2, 0.0f);
                std::fill(tb.begin(), tb.begin() + static_cast<size_t>(n) * 2, 0.0f);

                for (const auto& cp : audioClips)
                {
                    const AudioClip& c = *cp;
                    if (c.trackIndex != t || !c.pcm)
                        continue;

                    const long long cs = static_cast<long long>(std::llround(c.startSeconds * kEngineRate));
                    const long long cl = static_cast<long long>(std::llround(c.duration * kEngineRate));
                    const long long ce = cs + cl;
                    if (ce <= start || cs >= end)
                        continue;

                    const long long srcOff = static_cast<long long>(std::llround(c.sourceOffsetSeconds * kEngineRate));
                    const long long pcmFrames = static_cast<long long>(c.pcm->size() / 2);
                    const long long fadeIn = static_cast<long long>(c.fadeInSeconds * kEngineRate);
                    const long long fadeOut = static_cast<long long>(c.fadeOutSeconds * kEngineRate);
                    const float* src = c.pcm->data();

                    const long long f0 = std::max(start, cs);
                    const long long f1 = std::min(end, ce);

                    for (long long f = f0; f < f1; ++f)
                    {
                        const long long local = f - cs;
                        const long long si = srcOff + local;
                        if (si < 0 || si >= pcmFrames)
                            continue;

                        float g = c.gain;
                        if (fadeIn > 0 && local < fadeIn)
                            g *= static_cast<float>(local) / static_cast<float>(fadeIn);
                        if (fadeOut > 0 && cl - local < fadeOut)
                            g *= static_cast<float>(cl - local) / static_cast<float>(fadeOut);

                        const size_t o = static_cast<size_t>(f - start) * 2;
                        tb[o] += src[si * 2] * g;
                        tb[o + 1] += src[si * 2 + 1] * g;
                    }
                }

                TrackRT& rtT = trackRT[t];
                evScratch.clear();

                for (const auto& note : midiNotes)
                {
                    if (note.trackIndex != t)
                        continue;

                    const long long sf = static_cast<long long>(note.startBeat * framesPerBeat);
                    if (sf < start || sf >= end)
                        continue;

                    long long ef = static_cast<long long>((note.startBeat + note.lengthBeats) * framesPerBeat);
                    if (ef <= sf) ef = sf + 1;

                    evScratch.push_back({ static_cast<int>(sf - start), true, note.pitch, note.velocity });
                    rtT.active.push_back({ note.pitch, ef });
                }

                for (size_t k = 0; k < rtT.active.size();)
                {
                    if (rtT.active[k].endFrame < end)
                    {
                        evScratch.push_back({
                            static_cast<int>(std::max<long long>(0, rtT.active[k].endFrame - start)),
                            false, rtT.active[k].pitch, 0.0f });
                        rtT.active.erase(rtT.active.begin() + k);
                    }
                    else
                        ++k;
                }

                std::stable_sort(evScratch.begin(), evScratch.end(),
                    [](const MidiEv& a, const MidiEv& b)
                    {
                        if (a.offset != b.offset) return a.offset < b.offset;
                        return !a.on && b.on;
                    });

                std::shared_ptr<Vst3Plugin> inst;
                for (auto& v : trackVst[t])
                    if (v && v->isLoaded() && v->instrument) { inst = v; break; }

                if (inst)
                {
                    inst->beginRealtimeBlock();
                    for (const auto& e : evScratch)
                    {
                        if (e.on)
                            inst->queueNoteOn(e.pitch, e.vel, e.offset);
                        else if (!inst->queueNoteOff(e.pitch, e.offset))
                            rtT.active.push_back({ e.pitch, end });
                    }

                    std::fill(instBuf.begin(), instBuf.begin() + static_cast<size_t>(n) * 2, 0.0f);
                    if (inst->processRealtimeInstrument(instBuf.data(), n, bpmD, start))
                        for (int i = 0; i < n * 2; ++i)
                            tb[i] += instBuf[i];
                }
                else if (!evScratch.empty() || rtT.eng.Active())
                {
                    const synth::Setup su = synthSetup(t);
                    rtT.eng.SetRate(static_cast<float>(kEngineRate));
                    int pos = 0;
                    for (const auto& e : evScratch)
                    {
                        const int off = std::clamp(e.offset, pos, n);
                        if (off > pos)
                        {
                            rtT.eng.Render(tb.data() + static_cast<size_t>(pos) * 2, off - pos);
                            pos = off;
                        }
                        if (e.on) rtT.eng.NoteOn(su, e.pitch, e.vel);
                        else rtT.eng.NoteOff(e.pitch);
                    }
                    if (pos < n)
                        rtT.eng.Render(tb.data() + static_cast<size_t>(pos) * 2, n - pos);
                }

                Equalizer& eq = trackEQs[t];
                eq.update(static_cast<float>(kEngineRate));
                eq.process(tb.data(), static_cast<unsigned int>(n));

                ProcessEffectGraph(effectGraphs[t], tb.data(), static_cast<size_t>(n), kEngineRate,
                    static_cast<unsigned long long>(start),
                    static_cast<unsigned long long>(contentEndFrame));

                for (auto& v : trackVst[t])
                    if (v && v->isLoaded() && !v->instrument)
                        v->processRealtimeEffect(tb.data(), n, bpmD, start);

                for (int i = 0; i < n * 2; ++i)
                    if (!std::isfinite(tb[i])) tb[i] = 0.0f;

                float gl0, gr0, gl1, gr1;
                mixGains(t, static_cast<float>(static_cast<double>(start) / kEngineRate), hasSolo, gl0, gr0);
                mixGains(t, static_cast<float>(static_cast<double>(end) / kEngineRate), hasSolo, gl1, gr1);

                const float sendAmt = (bus.on && t < static_cast<int>(trackSends.size())) ? trackSends[t] : 0.0f;
                float pk = 0.0f;
                for (int f = 0; f < n; ++f)
                {
                    const float a = static_cast<float>(f) / static_cast<float>(n);
                    const float gl = gl0 + (gl1 - gl0) * a;
                    const float gr = gr0 + (gr1 - gr0) * a;
                    const float oL = tb[static_cast<size_t>(f) * 2] * gl;
                    const float oR = tb[static_cast<size_t>(f) * 2 + 1] * gr;
                    out[f * 2] += oL;
                    out[f * 2 + 1] += oR;
                    if (sendAmt > 0.0f)
                    {
                        busBuf[static_cast<size_t>(f) * 2] += oL * sendAmt;
                        busBuf[static_cast<size_t>(f) * 2 + 1] += oR * sendAmt;
                    }
                    pk = std::max(pk, std::max(std::fabs(oL), std::fabs(oR)));
                }
                trackPeak[t] = std::max(pk, trackPeak[t] * 0.9f);
            }

            if (bus.on)
            {
                const int bi = std::clamp(bus.index, 0, static_cast<int>(sizeof(kBusTypes) / sizeof(kBusTypes[0])) - 1);
                if (busAppliedIndex != bi)
                {
                    busFx.type = kBusTypes[bi];
                    ResetEffectNodeState(busFx);
                    busAppliedIndex = bi;
                }
                busFx.enabled = true;
                busFx.wet = 1.0f;
                busFx.amount = bus.amount;
                busFx.time = bus.time;
                busFx.feedback = bus.feedback;

                busDry = busBuf;
                ProcessEffectNode(busFx, busBuf.data(), static_cast<size_t>(n), kEngineRate,
                    static_cast<unsigned long long>(start),
                    static_cast<unsigned long long>(contentEndFrame));

                const float dryK = BusIsReverb(bi) ? 0.5f : 1.0f;   // the effect nodes mix the dry signal back in
                for (int i = 0; i < n * 2; ++i)
                    out[i] += (busBuf[i] - busDry[i] * dryK) * bus.ret;
            }

            if (metronomeOn && !exporting)
            {
                for (int f = 0; f < n; ++f)
                {
                    const long long gf = start + f;
                    const long long beatIdx = static_cast<long long>(std::floor(static_cast<double>(gf) / framesPerBeat));
                    const double posInBeat = static_cast<double>(gf) - static_cast<double>(beatIdx) * framesPerBeat;
                    const double sec = posInBeat / kEngineRate;
                    if (sec < 0.05)
                    {
                        const double freq = (beatIdx % 4 == 0) ? 1600.0 : 1100.0;
                        const float click = static_cast<float>(
                            std::sin(6.283185307179586 * freq * sec) * std::exp(-sec * 90.0))
                            * metronomeVolume * 0.6f;
                        out[f * 2] += click;
                        out[f * 2 + 1] += click;
                    }
                }
            }

            if (masterLimiterOn)
                ProcessEffectNode(masterLimiter, out, static_cast<size_t>(n), kEngineRate,
                    static_cast<unsigned long long>(start),
                    static_cast<unsigned long long>(contentEndFrame));

            float pkL = 0.0f, pkR = 0.0f;
            for (int f = 0; f < n; ++f)
            {
                pkL = std::max(pkL, std::fabs(out[f * 2]));
                pkR = std::max(pkR, std::fabs(out[f * 2 + 1]));
            }
            masterPeakL = std::max(pkL, masterPeakL * 0.92f);
            masterPeakR = std::max(pkR, masterPeakR * 0.92f);
            spectrum.push(out, static_cast<unsigned>(n));
        };

    rt.render = [&](float* out, ma_uint32 frames)
        {
            if (!playing)
            {
                if (previewEng.Active())
                {
                    previewEng.SetRate(static_cast<float>(kEngineRate));
                    previewEng.Render(out, static_cast<int>(frames));
                    for (ma_uint32 i = 0; i < frames * 2; ++i)
                        out[i] = std::clamp(out[i], -1.0f, 1.0f);
                }
                return;
            }

            const double rtFramesPerBeat = 60.0 / std::max(1.0f, bpm) * kEngineRate;

            long long timelineEnd =
                static_cast<long long>(timelineBars * 4.0 * rtFramesPerBeat);
            timelineEnd = std::max(timelineEnd, contentEndFrame + static_cast<long long>(2.0 * kEngineRate));

            const bool looping = loopEnabled && loopEndBeat > loopStartBeat + 0.24f;
            const long long loopStartF = static_cast<long long>(std::llround(std::max(0.0f, loopStartBeat) * rtFramesPerBeat));
            const long long loopEndF = static_cast<long long>(std::llround(loopEndBeat * rtFramesPerBeat));

            ma_uint32 done = 0;
            while (done < frames && playing)
            {
                int n = static_cast<int>(std::min<ma_uint32>(static_cast<ma_uint32>(kBlock), frames - done));
                if (looping && playFrame < loopEndF)
                    n = static_cast<int>(std::min<long long>(n, loopEndF - playFrame));
                float* o = out + static_cast<size_t>(done) * 2;

                renderChunk(playFrame, n, o);

                for (int i = 0; i < n * 2; ++i)
                    o[i] = std::clamp(o[i], -1.0f, 1.0f);

                playFrame += n;
                done += static_cast<ma_uint32>(n);

                if (looping && playFrame >= loopEndF)
                {
                    playFrame = loopStartF;
                    softSeekReset();
                }
                else if (!looping && playFrame >= timelineEnd)
                {
                    playFrame = timelineEnd;
                    playing = false;
                    needFlush = true;
                }
            }

            if (previewEng.Active())
            {
                previewEng.SetRate(static_cast<float>(kEngineRate));
                previewEng.Render(out, static_cast<int>(frames));
                for (ma_uint32 i = 0; i < frames * 2; ++i)
                    out[i] = std::clamp(out[i], -1.0f, 1.0f);
            }
        };

    ma_device rtDevice = {};
    {
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format = ma_format_f32;
        cfg.playback.channels = 2;
        cfg.sampleRate = kEngineRate;
        cfg.periodSizeInFrames = 1024;
        cfg.periods = 3;
        cfg.dataCallback = realtimeAudioCallback;
        cfg.pUserData = &rt;

        if (ma_device_init(nullptr, &cfg, &rtDevice) == MA_SUCCESS)
        {
            rtDeviceOk = true;
            ma_device_start(&rtDevice);
        }
        else
        {
            audioError = "Audio output device initialization failed.";
        }
    }

    auto startAllClips = [&]()
        {
            if (!rtDeviceOk)
            {
                audioError = "Audio output device is not available.";
                return;
            }

            audioError.clear();
            playFrame = frameFromPlayhead();
            clearMidiPlayback();
            playing = true;
        };

    auto importWav = [&]()
        {
            char filePath[MAX_PATH] = {};

            OPENFILENAMEA dialog = {};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = glfwGetWin32Window(window);
            dialog.lpstrFile = filePath;
            dialog.nMaxFile = MAX_PATH;
            dialog.lpstrFilter =
                "Audio Files\0*.wav;*.mp3\0"
                "WAV Files\0*.wav\0"
                "MP3 Files\0*.mp3\0"
                "All Files\0*.*\0";
            dialog.nFilterIndex = 1;
            dialog.Flags =
                OFN_FILEMUSTEXIST |
                OFN_PATHMUSTEXIST;

            if (!DialogOpen(&dialog))
                return;

            auto clip = std::make_unique<AudioClip>();

            clip->path = filePath;

            std::filesystem::path path(filePath);
            clip->name = path.filename().string();

            clip->pcm = LoadPcm(clip->path);
            if (!clip->pcm)
            {
                audioError = "Audio loading failed: " + clip->name;
                return;
            }

            clip->sampleRate = kEngineRate;
            clip->duration =
                static_cast<float>(clip->pcm->size() / 2) /
                static_cast<float>(kEngineRate);

            clip->startSeconds =
                playheadPosition * 240.0f /
                (timelineZoom * bpm);

            clip->trackIndex =
                selectedTrack >= 0 && selectedTrack < static_cast<int>(tracks.size())
                ? selectedTrack : 0;

            buildWaveform(*clip);
            audioClips.push_back(std::move(clip));

            audioError.clear();
        };

    auto exportWav = [&]()
        {
            const long long contentEnd = computeContentEnd();
            if (contentEnd <= 0)
            {
                audioError = "No audio clips or MIDI notes to export.";
                return;
            }

            char filePath[MAX_PATH] = {};

            OPENFILENAMEA dialog = {};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = glfwGetWin32Window(window);
            dialog.lpstrFile = filePath;
            dialog.nMaxFile = MAX_PATH;
            dialog.lpstrFilter =
                "WAV Files\0*.wav\0All Files\0*.*\0";
            dialog.lpstrDefExt = "wav";
            dialog.Flags =
                OFN_OVERWRITEPROMPT |
                OFN_PATHMUSTEXIST;

            if (!DialogSave(&dialog))
                return;

            stopAllClips();

            const long long totalFrames = contentEnd + static_cast<long long>(2.5 * kEngineRate);

            std::vector<float> mixBuffer(static_cast<size_t>(totalFrames) * 2, 0.0f);

            const long long savedEnd = contentEndFrame;
            contentEndFrame = contentEnd;
            exporting = true;

            for (long long pos = 0; pos < totalFrames; pos += kBlock)
            {
                const int n = static_cast<int>(std::min<long long>(kBlock, totalFrames - pos));
                renderChunk(pos, n, mixBuffer.data() + static_cast<size_t>(pos) * 2);
            }

            contentEndFrame = savedEnd;
            exporting = false;
            flushPlugins();

            for (float& sample : mixBuffer)
                sample = std::clamp(sample, -1.0f, 1.0f);

            const ma_format exportFormats[3] = { ma_format_s16, ma_format_s24, ma_format_f32 };
            const ma_format exportFormat = exportFormats[std::clamp(exportBits, 0, 2)];

            ma_encoder_config encoderConfig =
                ma_encoder_config_init(
                    ma_encoding_format_wav,
                    exportFormat,
                    2,
                    kEngineRate
                );

            std::vector<unsigned char> convertedMix;
            const void* exportData = mixBuffer.data();
            if (exportFormat != ma_format_f32)
            {
                convertedMix.resize(static_cast<size_t>(totalFrames) * 2 * ma_get_bytes_per_sample(exportFormat));
                ma_convert_pcm_frames_format(
                    convertedMix.data(), exportFormat,
                    mixBuffer.data(), ma_format_f32,
                    static_cast<ma_uint64>(totalFrames), 2,
                    exportFormat == ma_format_s16 ? ma_dither_mode_triangle : ma_dither_mode_none);
                exportData = convertedMix.data();
            }

            ma_encoder encoder;

            ma_result result = ma_encoder_init_file(
                filePath,
                &encoderConfig,
                &encoder
            );

            if (result != MA_SUCCESS)
            {
                audioError = "WAV export initialization failed.";
                return;
            }

            ma_uint64 framesWritten = 0;

            result = ma_encoder_write_pcm_frames(
                &encoder,
                exportData,
                static_cast<ma_uint64>(totalFrames),
                &framesWritten
            );

            ma_encoder_uninit(&encoder);

            if (result != MA_SUCCESS || framesWritten != static_cast<ma_uint64>(totalFrames))
            {
                audioError = "WAV export failed.";
                return;
            }

            audioError = "WAV exported successfully!";
        };

    auto exportStems = [&]()
        {
            const long long contentEnd = computeContentEnd();
            if (contentEnd <= 0)
            {
                audioError = "No audio clips or MIDI notes to export.";
                return;
            }

            char filePath[MAX_PATH] = {};
            OPENFILENAMEA dialog = {};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = glfwGetWin32Window(window);
            dialog.lpstrFile = filePath;
            dialog.nMaxFile = MAX_PATH;
            dialog.lpstrFilter = "WAV Files\0*.wav\0All Files\0*.*\0";
            dialog.lpstrDefExt = "wav";
            dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (!DialogSave(&dialog))
                return;

            stopAllClips();

            const std::filesystem::path basePath(filePath);
            const std::filesystem::path dir = basePath.parent_path();
            const std::string stem = basePath.stem().string();
            const long long totalFrames = contentEnd + static_cast<long long>(2.5 * kEngineRate);

            const std::vector<bool> savedSolo = trackSolo;
            const long long savedEnd = contentEndFrame;
            const ma_format exportFormats[3] = { ma_format_s16, ma_format_s24, ma_format_f32 };
            const ma_format exportFormat = exportFormats[std::clamp(exportBits, 0, 2)];
            int written = 0;

            for (size_t t = 0; t < tracks.size(); ++t)
            {
                if (t < trackMuted.size() && trackMuted[t])
                    continue;

                bool hasContent = false;
                for (const auto& c : audioClips)
                    if (c->trackIndex == static_cast<int>(t)) { hasContent = true; break; }
                if (!hasContent)
                    for (const auto& n : midiNotes)
                        if (n.trackIndex == static_cast<int>(t)) { hasContent = true; break; }
                if (!hasContent)
                    continue;

                for (size_t k = 0; k < trackSolo.size(); ++k)
                    trackSolo[k] = (k == t);

                flushPlugins();

                std::vector<float> mix(static_cast<size_t>(totalFrames) * 2, 0.0f);
                contentEndFrame = contentEnd;
                exporting = true;
                for (long long pos = 0; pos < totalFrames; pos += kBlock)
                {
                    const int n = static_cast<int>(std::min<long long>(kBlock, totalFrames - pos));
                    renderChunk(pos, n, mix.data() + static_cast<size_t>(pos) * 2);
                }
                exporting = false;

                for (float& sample : mix)
                    sample = std::clamp(sample, -1.0f, 1.0f);

                std::string trackName = tracks[t];
                for (char& ch : trackName)
                    if (std::strchr("\\/:*?\"<>|", ch)) ch = '_';

                const std::filesystem::path outPath =
                    dir / (stem + "_" + std::to_string(t + 1) + "_" + trackName + ".wav");

                ma_encoder_config encoderConfig =
                    ma_encoder_config_init(ma_encoding_format_wav, exportFormat, 2, kEngineRate);

                std::vector<unsigned char> converted;
                const void* data = mix.data();
                if (exportFormat != ma_format_f32)
                {
                    converted.resize(static_cast<size_t>(totalFrames) * 2 * ma_get_bytes_per_sample(exportFormat));
                    ma_convert_pcm_frames_format(
                        converted.data(), exportFormat,
                        mix.data(), ma_format_f32,
                        static_cast<ma_uint64>(totalFrames), 2,
                        exportFormat == ma_format_s16 ? ma_dither_mode_triangle : ma_dither_mode_none);
                    data = converted.data();
                }

                ma_encoder encoder;
                if (ma_encoder_init_file(outPath.string().c_str(), &encoderConfig, &encoder) != MA_SUCCESS)
                    continue;

                ma_uint64 framesWritten = 0;
                const ma_result r = ma_encoder_write_pcm_frames(
                    &encoder, data, static_cast<ma_uint64>(totalFrames), &framesWritten);
                ma_encoder_uninit(&encoder);

                if (r == MA_SUCCESS)
                    ++written;
            }

            trackSolo = savedSolo;
            contentEndFrame = savedEnd;
            exporting = false;
            flushPlugins();

            audioError = "Stems exported: " + std::to_string(written) + " files";
        };

    auto saveProject = [&](const std::string& requestedPath = std::string(), bool forceDialog = false)
        {
            std::string path = (!forceDialog && requestedPath.empty()) ? currentProjectPath : requestedPath;
            if (forceDialog || path.empty())
            {
                char filePath[MAX_PATH] = {};
                OPENFILENAMEA d = {}; d.lStructSize = sizeof(d); d.hwndOwner = glfwGetWin32Window(window);
                d.lpstrFile = filePath; d.nMaxFile = MAX_PATH; d.lpstrFilter = "EasyMix Project\0*.emix\0All Files\0*.*\0";
                d.lpstrDefExt = "emix"; d.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                if (!DialogSave(&d)) return; path = filePath;
            }
            std::ofstream out(path, std::ios::binary);
            if (!out) { audioError = "Could not save project."; return; }
            out << "EASYMIX 6\n" << std::setprecision(9) << bpm << ' ' << timelineZoom << ' ' << playheadPosition << ' ' << masterVolume << '\n';
            out << (loopEnabled ? 1 : 0) << ' ' << loopStartBeat << ' ' << loopEndBeat << ' '
                << (metronomeOn ? 1 : 0) << ' ' << (masterLimiterOn ? 1 : 0) << ' ' << masterLimiter.amount << ' '
                << timelineBars << '\n';
            out << tracks.size() << '\n';
            for (size_t i = 0; i < tracks.size(); ++i)
            {
                out << std::quoted(tracks[i]) << ' ' << trackVolumes[i] << ' ' << trackPans[i] << ' ' << trackMuted[i] << ' ' << trackSolo[i] << '\n';
                const auto& eq = trackEQs[i]; out << eq.enabled << '\n';
                for (int b = 0; b < Equalizer::BandCount; ++b) out << eq.frequencies[b] << ' ' << eq.gains[b] << ' ' << eq.qValues[b] << '\n';
                const auto& graph = effectGraphs[i]; out << graph.nextId << ' ' << graph.nodes.size() << ' ' << graph.links.size() << '\n';
                for (const auto& n : graph.nodes) out << n.id << ' ' << static_cast<int>(n.type) << ' ' << n.position.x << ' ' << n.position.y << ' ' << n.amount << ' ' << n.wet << ' ' << n.frequency << ' ' << n.time << ' ' << n.feedback << ' ' << n.enabled << '\n';
                for (const auto& l : graph.links) out << l.from << ' ' << l.to << '\n';
                out << trackAutomation[i].size() << '\n';
                for (const auto& a : trackAutomation[i]) out << a.time << ' ' << a.value << '\n';
            }
            out << audioClips.size() << '\n';
            std::filesystem::path projectPath(path);
            std::filesystem::path assetDirectory = projectPath.filename().string() + ".assets";
            std::filesystem::create_directories(projectPath.parent_path().empty() ? std::filesystem::path(".") / assetDirectory : projectPath.parent_path() / assetDirectory);
            std::unordered_map<std::string, std::string> copiedAssets;
            for (size_t i = 0; i < audioClips.size(); ++i)
            {
                const auto& c = audioClips[i];
                std::filesystem::path source(c->path);
                std::string extension = source.extension().string();
                if (extension.empty()) extension = ".audio";
                const std::filesystem::path assetFolder = (projectPath.parent_path().empty() ? std::filesystem::path(".") : projectPath.parent_path()) / assetDirectory;

                std::error_code copyError;
                std::string assetName;
                const std::string sourceKey = source.string();
                auto known = copiedAssets.find(sourceKey);
                if (known != copiedAssets.end())
                {
                    assetName = known->second;
                }
                else
                {
                    const std::filesystem::path sourceParent = source.parent_path();
                    const bool alreadyInAssets =
                        !sourceParent.empty() &&
                        std::filesystem::equivalent(sourceParent, assetFolder, copyError) && !copyError;
                    copyError.clear();

                    if (alreadyInAssets)
                    {
                        assetName = source.filename().string();
                    }
                    else
                    {
                        assetName = "clip_" + std::to_string(std::hash<std::string>{}(sourceKey)) + extension;
                        std::filesystem::copy_file(source, assetFolder / assetName, std::filesystem::copy_options::overwrite_existing, copyError);
                        if (copyError) { audioError = "Could not copy audio asset: " + c->name; return; }
                    }
                    copiedAssets[sourceKey] = assetName;
                }
                const std::filesystem::path assetPath = assetFolder / assetName;
                std::string storedPath = (assetDirectory / assetName).string();
                out << std::quoted(storedPath) << ' ' << std::quoted(c->name) << ' ' << c->startSeconds << ' ' << c->duration << ' ' << c->trackIndex << ' ' << c->sampleRate << ' ' << c->sourceOffsetSeconds << ' ' << c->gain
                    << ' ' << c->fadeInSeconds << ' ' << c->fadeOutSeconds << '\n';
            }

            out << midiNotes.size() << '\n';
            for (const auto& n : midiNotes)
                out << n.id << ' ' << n.trackIndex << ' ' << n.pitch << ' ' << n.startBeat << ' ' << n.lengthBeats << ' ' << n.velocity << '\n';
            for (size_t i = 0; i < tracks.size(); ++i)
            {
                out << trackVst[i].size();
                for (auto& v : trackVst[i])
                {
                    const std::string hex = BytesToHex(v->saveState());
                    const std::string stateText = hex.empty() ? std::string("-") : hex;
                    out << ' ' << std::quoted(v->path) << ' ' << std::quoted(stateText);
                }
                out << '\n';
            }

            out << markers.size() << '\n';
            for (const auto& mk : markers)
                out << mk.beat << ' ' << std::quoted(mk.name) << '\n';

            out << (bus.on ? 1 : 0) << ' ' << bus.index << ' ' << bus.ret << ' ' << bus.amount << ' '
                << bus.time << ' ' << bus.feedback << '\n';
            for (size_t i = 0; i < tracks.size(); ++i)
                out << (i < trackSends.size() ? trackSends[i] : 0.0f) << ' ';
            out << '\n';
            out << "SYNTH";
            for (size_t i = 0; i < tracks.size(); ++i)
            {
                const synth::Setup su = synthSetup(static_cast<int>(i));
                out << ' ' << su.preset << ' ' << su.gain << ' ' << su.bright << ' ' << su.attack << ' ' << su.release;
            }
            out << '\n';

            currentProjectPath = path; audioError = "Project saved: " + path;
            savedHash = projectHash();
        };
    auto loadProject = [&]()
        {
            char filePath[MAX_PATH] = {};
            OPENFILENAMEA d = {}; d.lStructSize = sizeof(d); d.hwndOwner = glfwGetWin32Window(window);
            d.lpstrFile = filePath; d.nMaxFile = MAX_PATH; d.lpstrFilter = "EasyMix Project\0*.emix\0All Files\0*.*\0";
            d.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (!DialogOpen(&d)) return;
            std::ifstream in(filePath, std::ios::binary); std::string magic; int version = 0;
            in >> magic >> version;
            if (!in || magic != "EASYMIX" || (version < 2 || version > 6)) { audioError = "Invalid or unsupported .emix project."; return; }

            resetProject();

            float loadedMaster = 1.0f; in >> bpm >> timelineZoom >> playheadPosition >> loadedMaster; masterVolume = loadedMaster;
            bpm = std::clamp(bpm, 20.0f, 400.0f);
            timelineZoom = std::clamp(timelineZoom, 30.0f, 200.0f);
            prevTimelineZoom = timelineZoom;
            prevBpm = bpm;

            if (version >= 4)
            {
                int le = 0, mo = 0, ml = 1, tb = 16;
                float ls = 0.0f, lend = 16.0f, lamt = 0.0f;
                in >> le >> ls >> lend >> mo >> ml >> lamt >> tb;
                if (in)
                {
                    loopEnabled = le != 0;
                    loopStartBeat = std::max(0.0f, ls);
                    loopEndBeat = std::max(loopStartBeat + 1.0f, lend);
                    metronomeOn = mo != 0;
                    masterLimiterOn = ml != 0;
                    masterLimiter.amount = std::clamp(lamt, 0.0f, 1.0f);
                    timelineBars = std::clamp(tb, 4, 512);
                }
            }

            size_t count = 0; in >> count;
            if (!in || count > 256) { resetProject(); audioError = "Invalid track count."; return; }

            tracks.clear(); trackVolumes.clear(); trackPans.clear(); trackSends.clear(); trackMuted.clear(); trackSolo.clear();
            trackEQs.clear(); effectGraphs.clear(); trackAutomation.clear(); trackVst.clear();
            trackSynth.clear();

            for (size_t i = 0; i < count; ++i)
            {
                std::string name; float vol = 1.0f, pan = 0.0f; bool muted = false, solo = false; in >> std::quoted(name) >> vol >> pan >> muted >> solo;
                tracks.push_back(name); trackVolumes.push_back(vol); trackPans.push_back(pan); trackSends.push_back(0.0f); trackMuted.push_back(muted); trackSolo.push_back(solo);
                trackVst.emplace_back();
                Equalizer eq; in >> eq.enabled; for (int b = 0; b < Equalizer::BandCount; ++b) in >> eq.frequencies[b] >> eq.gains[b] >> eq.qValues[b]; eq.update(44100.0f); trackEQs.push_back(eq);

                EffectGraph graph; size_t nn = 0, nl = 0; in >> graph.nextId >> nn >> nl;
                if (!in || nn > 1024 || nl > 2048)
                {
                    resetProject(); audioError = "Invalid effect graph."; return;
                }
                graph.nodes.clear(); graph.links.clear();
                for (size_t n = 0; n < nn; ++n)
                {
                    EffectNode x; int type = 0;
                    in >> x.id >> type >> x.position.x >> x.position.y >> x.amount >> x.wet >> x.frequency >> x.time >> x.feedback >> x.enabled;
                    if (type < 0 || type >= EffectCount) type = 0;
                    x.type = static_cast<EffectType>(type);
                    graph.nodes.push_back(x);
                }
                for (size_t l = 0; l < nl; ++l) { EffectLink x; in >> x.from >> x.to; graph.links.push_back(x); }
                effectGraphs.push_back(std::move(graph));

                size_t na = 0; in >> na;
                if (!in || na > 100000)
                {
                    resetProject(); audioError = "Invalid automation data."; return;
                }
                std::vector<AutomationPoint> points;
                for (size_t a = 0; a < na; ++a) { AutomationPoint x; in >> x.time >> x.value; points.push_back(x); }
                trackAutomation.push_back(std::move(points));
            }

            size_t clips = 0; in >> clips;
            if (!in || clips > 10000) { resetProject(); audioError = "Invalid clip count."; return; }
            for (size_t i = 0; i < clips; ++i)
            {
                auto c = std::make_unique<AudioClip>();
                in >> std::quoted(c->path) >> std::quoted(c->name) >> c->startSeconds >> c->duration >> c->trackIndex >> c->sampleRate >> c->sourceOffsetSeconds >> c->gain;
                if (version >= 3) in >> c->fadeInSeconds >> c->fadeOutSeconds;
                std::filesystem::path mediaPath(c->path);
                if (mediaPath.is_relative()) mediaPath = std::filesystem::path(filePath).parent_path() / mediaPath;
                c->path = mediaPath.string();
                if (!in || !std::filesystem::exists(c->path)) continue;
                if (c->trackIndex < 0 || c->trackIndex >= static_cast<int>(tracks.size())) continue;
                c->pcm = LoadPcm(c->path);
                if (!c->pcm) continue;
                c->sampleRate = kEngineRate;
                buildWaveform(*c); audioClips.push_back(std::move(c));
            }

            if (version >= 3 && in)
            {
                size_t nm = 0; in >> nm;
                if (in && nm < 1000000)
                {
                    for (size_t i = 0; i < nm; ++i)
                    {
                        MidiNote n;
                        in >> n.id >> n.trackIndex >> n.pitch >> n.startBeat >> n.lengthBeats >> n.velocity;
                        if (!in) break;
                        if (n.trackIndex < 0 || n.trackIndex >= static_cast<int>(tracks.size())) continue;
                        midiNotes.push_back(n);
                        pianoRoll.nextId = std::max(pianoRoll.nextId, n.id + 1);
                    }
                }
                for (size_t i = 0; in && i < tracks.size(); ++i)
                {
                    size_t np = 0; in >> np;
                    for (size_t k = 0; in && k < np && k < 64; ++k)
                    {
                        std::string p; in >> std::quoted(p);
                        std::vector<unsigned char> pluginState;
                        if (version >= 4)
                        {
                            std::string stateText;
                            in >> std::quoted(stateText);
                            pluginState = HexToBytes(stateText);
                        }
                        auto plug = std::make_shared<Vst3Plugin>();
                        if (plug->load(p, 44100.0, 4096))
                        {
                            if (!pluginState.empty())
                                plug->loadState(pluginState);
                            trackVst[i].push_back(plug);
                        }
                        else audioError = "VST3 load failed: " + p;
                    }
                }
            }

            if (version >= 5 && in)
            {
                size_t nmk = 0; in >> nmk;
                if (in && nmk < 100000)
                {
                    for (size_t i = 0; i < nmk; ++i)
                    {
                        Marker mk; in >> mk.beat >> std::quoted(mk.name);
                        if (!in) break;
                        markers.push_back(mk);
                    }
                }
            }

            if (version >= 6 && in)
            {
                int bo = 1, bi = 1;
                float br = 0.8f, ba = 0.5f, bt = 0.3f, bf = 0.35f;
                in >> bo >> bi >> br >> ba >> bt >> bf;
                if (in)
                {
                    bus.on = bo != 0;
                    bus.index = std::clamp(bi, 0, static_cast<int>(sizeof(kBusTypes) / sizeof(kBusTypes[0])) - 1);
                    bus.ret = std::clamp(br, 0.0f, 2.0f);
                    bus.amount = std::clamp(ba, 0.0f, 1.0f);
                    bus.time = std::clamp(bt, 0.01f, 2.0f);
                    bus.feedback = std::clamp(bf, 0.0f, 0.9f);
                }
                for (size_t i = 0; in && i < tracks.size() && i < trackSends.size(); ++i)
                {
                    float sv = 0.0f; in >> sv;
                    if (in) trackSends[i] = std::clamp(sv, 0.0f, 1.0f);
                }
                in.clear();
                std::string synTag;
                if (in >> synTag && synTag == "SYNTH")
                {
                    trackSynth.assign(tracks.size(), synth::Setup());
                    const int presetMax = static_cast<int>(synth::Patches().size()) - 1;
                    for (size_t i = 0; i < tracks.size(); ++i)
                    {
                        synth::Setup su;
                        in >> su.preset >> su.gain >> su.bright >> su.attack >> su.release;
                        if (!in) break;
                        su.preset = std::clamp(su.preset, 0, presetMax);
                        su.gain = std::clamp(su.gain, 0.0f, 2.0f);
                        su.bright = std::clamp(su.bright, 0.0f, 1.0f);
                        su.attack = std::clamp(su.attack, 0.0f, 1.0f);
                        su.release = std::clamp(su.release, 0.0f, 1.0f);
                        trackSynth[i] = su;
                    }
                }
            }

            selectedTrack = tracks.empty() ? -1 : 0; currentProjectPath = filePath;
            undoStack.clear(); redoStack.clear(); undoInit = false;
            if (audioError.rfind("VST3 load failed", 0) != 0)
                audioError = "Project loaded: " + currentProjectPath;
        };

    RecordingState recordingState;
    std::string recordingFilePath;
    float recordingStartSeconds = 0.0f;
    ma_device_config deviceConfig =
        ma_device_config_init(ma_device_type_capture);

    deviceConfig.capture.format = ma_format_f32;
    deviceConfig.capture.channels = 1;
    deviceConfig.sampleRate = 44100;
    deviceConfig.dataCallback = recordingCallback;
    deviceConfig.pUserData = &recordingState;

    ma_device captureDevice;
    ma_result deviceResult = ma_device_init(
        nullptr,
        &deviceConfig,
        &captureDevice
    );

    const bool micAvailable = (deviceResult == MA_SUCCESS);
    if (!micAvailable)
    {
        fprintf(
            stderr,
            "Microphone initialization failed: %s (%d)\n",
            ma_result_description(deviceResult),
            static_cast<int>(deviceResult)
        );
    }

    auto startRecording = [&]()
        {
            if (!micAvailable)
            {
                audioError = "No microphone available.";
                return;
            }

            if (recordingState.active.load())
                return;

            char filePath[MAX_PATH] = {};
            OPENFILENAMEA dialog = {};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = glfwGetWin32Window(window);
            dialog.lpstrFile = filePath;
            dialog.nMaxFile = MAX_PATH;
            dialog.lpstrFilter =
                "WAV Files\0*.wav\0All Files\0*.*\0";
            dialog.lpstrDefExt = "wav";
            dialog.Flags =
                OFN_OVERWRITEPROMPT |
                OFN_PATHMUSTEXIST;

            if (!DialogSave(&dialog))
                return;
            recordingFilePath = filePath;

            recordingStartSeconds =
                playheadPosition * 240.0f /
                (timelineZoom * bpm);
            ma_encoder_config encoderConfig =
                ma_encoder_config_init(
                    ma_encoding_format_wav,
                    ma_format_f32,
                    1,
                    44100
                );

            if (ma_encoder_init_file(
                filePath,
                &encoderConfig,
                &recordingState.encoder
            ) != MA_SUCCESS)
            {
                audioError = "Recording initialization failed.";
                return;
            }

            recordingState.active.store(true);

            if (ma_device_start(&captureDevice) != MA_SUCCESS)
            {
                recordingState.active.store(false);
                ma_encoder_uninit(&recordingState.encoder);
                audioError = "Microphone start failed.";
                return;
            }

            audioError.clear();
        };

    auto stopRecording = [&]()
        {
            if (!recordingState.active.load())
                return;

            recordingState.active.store(false);

            ma_device_stop(&captureDevice);

            ma_encoder_uninit(&recordingState.encoder);

            auto clip = std::make_unique<AudioClip>();

            clip->path = recordingFilePath;

            std::filesystem::path path(recordingFilePath);
            clip->name = path.filename().string();

            clip->pcm = LoadPcm(recordingFilePath);
            if (!clip->pcm)
            {
                audioError = "Recorded audio loading failed.";
                return;
            }

            clip->sampleRate = kEngineRate;
            clip->duration =
                static_cast<float>(clip->pcm->size() / 2) /
                static_cast<float>(kEngineRate);

            clip->startSeconds = std::max(0.0f, recordingStartSeconds - recordLatencyMs / 1000.0f);

            clip->trackIndex =
                selectedTrack >= 0 &&
                selectedTrack < static_cast<int>(tracks.size())
                ? selectedTrack
                : 0;

            buildWaveform(*clip);
            audioClips.push_back(std::move(clip));

            audioError = "Recording saved and added to timeline.";
        };

    auto goToEnd = [&]() { playheadPosition = playheadFromFrame(computeContentEnd()); seekRequested = true; };
    auto nudgePlayhead = [&](float beats)
        {
            playheadPosition = std::max(0.0f, playheadPosition + beats * timelineZoom / 4.0f);
            seekRequested = true;
        };
    auto setLoopStart = [&]()
        {
            loopStartBeat = std::max(0.0f, snapToGrid(playheadPosition * 4.0f / timelineZoom));
            if (loopEndBeat < loopStartBeat + 1.0f) loopEndBeat = loopStartBeat + 4.0f;
            loopEnabled = true;
        };
    auto setLoopEnd = [&]()
        {
            loopEndBeat = std::max(loopStartBeat + 1.0f, snapToGrid(playheadPosition * 4.0f / timelineZoom));
            loopEnabled = true;
        };
    auto zoomBy = [&](float k) { timelineZoom = std::clamp(timelineZoom * k, 30.0f, 200.0f); };
    auto copyClip = [&]()
        {
            if (!selectedClipPtr) { audioError = "Select a clip first."; return; }
            clipClipboard.assign(1, *selectedClipPtr);
            audioError = "Clip copied.";
        };
    auto pasteClip = [&]()
        {
            if (clipClipboard.empty()) { audioError = "Clip clipboard is empty."; return; }
            auto c = std::make_unique<AudioClip>(clipClipboard.front());
            c->startSeconds = playheadPosition * 240.0f / (timelineZoom * bpm);
            if (selectedTrack >= 0 && selectedTrack < static_cast<int>(tracks.size()))
                c->trackIndex = selectedTrack;
            buildWaveform(*c);
            selectedClipPtr = c.get();
            audioClips.push_back(std::move(c));
        };
    auto addTrack = [&]()
        {
            tracks.push_back("Track " + std::to_string(tracks.size() + 1));
            trackVolumes.push_back(1.0f);
            trackMuted.push_back(false);
            trackSolo.push_back(false);
            trackPans.push_back(0.0f);
            trackSends.push_back(0.0f);
            trackAutomation.emplace_back();
            trackEQs.emplace_back();
            effectGraphs.emplace_back();
            trackVst.emplace_back();
            selectedTrack = static_cast<int>(tracks.size()) - 1;
        };
    auto swapTracks = [&](int a, int b)
        {
            const int n = static_cast<int>(tracks.size());
            if (a < 0 || b < 0 || a >= n || b >= n || a == b) return;
            if (static_cast<int>(trackSends.size()) < n) trackSends.resize(n, 0.0f);
            std::swap(tracks[a], tracks[b]);
            std::swap(trackVolumes[a], trackVolumes[b]);
            std::swap(trackPans[a], trackPans[b]);
            std::swap(trackSends[a], trackSends[b]);
            synthSetup(std::max(a, b));
            std::swap(trackSynth[a], trackSynth[b]);
            const bool ma = trackMuted[a], sa = trackSolo[a];
            trackMuted[a] = trackMuted[b]; trackMuted[b] = ma;
            trackSolo[a] = trackSolo[b];   trackSolo[b] = sa;
            std::swap(trackAutomation[a], trackAutomation[b]);
            std::swap(trackEQs[a], trackEQs[b]);
            std::swap(effectGraphs[a], effectGraphs[b]);
            std::swap(trackVst[a], trackVst[b]);
            for (auto& c : audioClips)
            {
                if (c->trackIndex == a) c->trackIndex = b;
                else if (c->trackIndex == b) c->trackIndex = a;
            }
            for (auto& nt : midiNotes)
            {
                if (nt.trackIndex == a) nt.trackIndex = b;
                else if (nt.trackIndex == b) nt.trackIndex = a;
            }
            trackRT.clear();
            trackBuf.clear();
            trackPeak.clear();
            selectedTrack = b;
        };
    auto duplicateTrack = [&](int t)
        {
            const int n = static_cast<int>(tracks.size());
            if (t < 0 || t >= n) { audioError = "Select a track first."; return; }
            if (static_cast<int>(trackSends.size()) < n) trackSends.resize(n, 0.0f);
            const std::string newName = tracks[t] + " Copy";
            tracks.push_back(newName);
            trackVolumes.push_back(trackVolumes[t]);
            trackPans.push_back(trackPans[t]);
            trackSends.push_back(trackSends[t]);
            { const synth::Setup su = synthSetup(t); synthSetup(n) = su; }
            trackMuted.push_back(false);
            trackSolo.push_back(false);
            const auto autoCopy = trackAutomation[t];
            trackAutomation.push_back(autoCopy);
            const Equalizer eqCopy = trackEQs[t];
            trackEQs.push_back(eqCopy);
            EffectGraph gCopy = effectGraphs[t];
            ResetEffectGraph(gCopy);
            effectGraphs.push_back(std::move(gCopy));
            trackVst.emplace_back();

            std::vector<std::unique_ptr<AudioClip>> addedClips;
            for (const auto& cp : audioClips)
                if (cp->trackIndex == t)
                {
                    auto c = std::make_unique<AudioClip>(*cp);
                    c->trackIndex = n;
                    addedClips.push_back(std::move(c));
                }
            for (auto& c : addedClips) audioClips.push_back(std::move(c));

            std::vector<MidiNote> addedNotes;
            for (const auto& nt : midiNotes)
                if (nt.trackIndex == t)
                {
                    MidiNote c = nt;
                    c.id = pianoRoll.nextId++;
                    c.trackIndex = n;
                    addedNotes.push_back(c);
                }
            midiNotes.insert(midiNotes.end(), addedNotes.begin(), addedNotes.end());

            trackRT.clear();
            trackBuf.clear();
            trackPeak.clear();
            selectedTrack = n;
        };
    auto resetFaders = [&]()
        {
            for (auto& v : trackVolumes) v = 1.0f;
            for (auto& v : trackPans) v = 0.0f;
            for (auto& v : trackSends) v = 0.0f;
            masterVolume = 1.0f;
        };
    auto addFx = [&](EffectType type, float amount = -1.0f, float frequency = -1.0f, float wetV = -1.0f)
        {
            if (selectedTrack < 0 || selectedTrack >= static_cast<int>(effectGraphs.size()))
            {
                audioError = "Select a track before adding an effect.";
                return;
            }
            addEffectToSelectedTrack(type);
            EffectNode& nd = effectGraphs[selectedTrack].nodes.back();
            if (amount >= 0.0f) nd.amount = amount;
            if (frequency >= 0.0f) nd.frequency = frequency;
            if (wetV >= 0.0f) nd.wet = wetV;
        };
    auto selectTrackRel = [&](int d)
        {
            if (tracks.empty()) return;
            selectedTrack = std::clamp(selectedTrack + d, 0, static_cast<int>(tracks.size()) - 1);
        };

    pianoRoll.audition = [&](int pitch, float vel, bool add)
        {
            if (selectedTrack < 0 || selectedTrack >= static_cast<int>(tracks.size())) return;
            if (selectedTrack < static_cast<int>(trackVst.size()))
                for (auto& pv : trackVst[selectedTrack])
                    if (pv && pv->isLoaded() && pv->instrument) return;
            if (!add) previewEng.ReleaseAll();
            previewEng.SetRate(static_cast<float>(kEngineRate));
            previewEng.NoteOn(synthSetup(selectedTrack), pitch, vel);
            previewOffTime = ImGui::GetTime() + 0.38;
        };
    auto setInstrument = [&](int idx)
        {
            if (selectedTrack < 0 || selectedTrack >= static_cast<int>(tracks.size()))
            {
                audioError = "Select a track first.";
                return;
            }
            const int n = static_cast<int>(synth::Patches().size());
            synthSetup(selectedTrack).preset = ((idx % n) + n) % n;
            if (pianoRoll.audition) pianoRoll.audition(60, 0.8f, false);
        };

    palette.cmds = {
        { "Play / Stop", "Space", [&] { if (playing) stopAllClips(); else startAllClips(); } },
        { "Go to Start", "Home", [&] { playheadPosition = 0.0f; seekRequested = true; } },
        { "Save Project", "Ctrl+S", [&] { saveProject(); } },
        { "Save Project As...", "", [&] { saveProject(std::string(), true); } },
        { "Open Project", "", [&] { loadProject(); } },
        { "Import Audio", "", [&] { importWav(); } },
        { "Export WAV", "", [&] { exportWav(); } },
        { "Export Stems", "", [&] { exportStems(); } },
        { "Toggle Loop", "L", [&] { loopEnabled = !loopEnabled; } },
        { "Toggle Metronome", "C", [&] { metronomeOn = !metronomeOn; } },
        { "Toggle Follow Playhead", "", [&] { followPlayhead = !followPlayhead; } },
        { "Toggle Mixer", "", [&] { showMixer = !showMixer; } },
        { "Toggle Piano Roll", "", [&] { showPianoRoll = !showPianoRoll; } },
        { "Toggle VST3 Rack", "", [&] { showVstRack = !showVstRack; } },
        { "Toggle Send Bus", "", [&] { showBus = !showBus; } },
        { "Toggle Markers", "", [&] { showMarkers = !showMarkers; } },
        { "Toggle Volume Automation", "", [&] { showAutomation = !showAutomation; } },
        { "Toggle Master Limiter", "", [&] { masterLimiterOn = !masterLimiterOn; } },
        { "Undo", "Ctrl+Z", [&] { doUndo(); } },
        { "Redo", "Ctrl+Y", [&] { doRedo(); } },
        { "Shortcut List", "F1", [&] { showHelp = true; } },
        { "Reset Layout", "", [&] { resetLayoutFlag = true; } },
        { "Go to End", "End", [&] { goToEnd(); } },
        { "Nudge Playhead +1 Beat", ".", [&] { nudgePlayhead(1.0f); } },
        { "Nudge Playhead -1 Beat", ",", [&] { nudgePlayhead(-1.0f); } },
        { "Loop: Set Start at Playhead", "I", [&] { setLoopStart(); } },
        { "Loop: Set End at Playhead", "O", [&] { setLoopEnd(); } },
        { "Zoom In", "Ctrl+=", [&] { zoomBy(1.15f); } },
        { "Zoom Out", "Ctrl+-", [&] { zoomBy(1.0f / 1.15f); } },
        { "Add Marker at Playhead", "M", [&] {
            Marker mk; mk.beat = std::max(0.0f, snapToGrid(playheadPosition * 4.0f / timelineZoom));
            mk.name = "Marker " + std::to_string(markers.size() + 1);
            markers.push_back(mk);
            std::sort(markers.begin(), markers.end(), [](const Marker& a, const Marker& b) { return a.beat < b.beat; }); } },
        { "Clear All Markers", "", [&] { markers.clear(); } },
        { "Copy Selected Clip", "Ctrl+C", [&] { copyClip(); } },
        { "Paste Clip at Playhead", "Ctrl+V", [&] { pasteClip(); } },
        { "Split Clip at Playhead", "S", [&] { if (selectedClipPtr) splitClipAtPlayhead(selectedClipPtr); else audioError = "Select a clip first."; } },
        { "Duplicate Clip", "Ctrl+D", [&] { if (selectedClipPtr) duplicateClip(selectedClipPtr); else audioError = "Select a clip first."; } },
        { "Add Track", "", [&] { addTrack(); } },
        { "Duplicate Track", "", [&] { duplicateTrack(selectedTrack); } },
        { "Move Track Up", "", [&] { swapTracks(selectedTrack, selectedTrack - 1); } },
        { "Move Track Down", "", [&] { swapTracks(selectedTrack, selectedTrack + 1); } },
        { "Select Next Track", "", [&] { selectTrackRel(1); } },
        { "Select Previous Track", "", [&] { selectTrackRel(-1); } },
        { "Rename Selected Track", "", [&] {
            if (selectedTrack >= 0 && selectedTrack < static_cast<int>(tracks.size()))
            {
 strcpy_s(renameBuffer, sizeof(renameBuffer), tracks[selectedTrack].c_str()); renamingTrack = true;
}
else audioError = "Select a track first."; } },
{ "Unmute / Unsolo All", "", [&] { std::fill(trackMuted.begin(), trackMuted.end(), false); std::fill(trackSolo.begin(), trackSolo.end(), false); } },
{ "Reset All Faders", "", [&] { resetFaders(); } },
{ "Toggle Auto-save", "", [&] { autoSave = !autoSave; } },
{ "Clear Effects of Selected Track", "", [&] {
    if (selectedTrack >= 0 && selectedTrack < static_cast<int>(effectGraphs.size())) effectGraphs[selectedTrack] = EffectGraph{};
    else audioError = "Select a track first."; } },
{ "FX Preset: Vocal Chain", "", [&] {
    addFx(EffectType::HighPass, 0.5f, 90.0f); addFx(EffectType::Compressor, 0.4f);
    addFx(EffectType::DeEsser, 0.4f); addFx(EffectType::PlateReverb, 0.5f, -1.0f, 0.2f); } },
{ "FX Preset: Lo-Fi", "", [&] {
    addFx(EffectType::LowPass, 0.5f, 6000.0f); addFx(EffectType::LoFi, 0.5f); addFx(EffectType::Vinyl, 0.3f, -1.0f, 0.6f); } },
{ "FX Preset: Radio Voice", "", [&] { addFx(EffectType::Radio, 0.4f); addFx(EffectType::Compressor, 0.6f); } },
{ "FX Preset: Wide Pad", "", [&] {
    addFx(EffectType::Chorus, 0.5f); addFx(EffectType::StereoEnhancer, 0.4f); addFx(EffectType::HallReverb, 0.5f, -1.0f, 0.35f); } },
{ "FX Preset: Clean Master", "", [&] { addFx(EffectType::Compressor, 0.3f); addFx(EffectType::Limiter, 0.2f); } },
    };
    palette.cmds.push_back({ "Next Instrument", "", [&] { setInstrument(synthSetup(selectedTrack).preset + 1); } });
    palette.cmds.push_back({ "Previous Instrument", "", [&] { setInstrument(synthSetup(selectedTrack).preset - 1); } });
    for (int ii = 0; ii < static_cast<int>(synth::Patches().size()); ++ii)
        palette.cmds.push_back({ std::string("Instrument: ") + synth::Patches()[ii].name + "  [" + synth::Patches()[ii].category + "]", "",
            [&, ii] { setInstrument(ii); } });
    for (int fxi = 0; fxi < EffectCount; ++fxi)
        palette.cmds.push_back({ std::string("Add Effect: ") + EffectNames[fxi], "",
            [&, fxi] { addEffectToSelectedTrack(static_cast<EffectType>(fxi)); } });

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        if (glfwWindowShouldClose(window) && projectDirty)
        {
            const int answer = MessageBoxA(
                glfwGetWin32Window(window),
                "The project has unsaved changes. Save before exit?",
                "EasyMix",
                MB_YESNOCANCEL | MB_ICONQUESTION);

            if (answer == IDCANCEL)
            {
                glfwSetWindowShouldClose(window, GLFW_FALSE);
            }
            else if (answer == IDYES)
            {
                uiLock.lock();
                saveProject();
                const bool stillDirty = projectHash() != savedHash;
                uiLock.unlock();

                if (stillDirty)
                    glfwSetWindowShouldClose(window, GLFW_FALSE);
            }
        }

        uiLock.lock();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ---------------------------------------------------------------------
        // メインバーの表示
        // ---------------------------------------------------------------------
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Project"))
                {
                    resetProject();
                    undoStack.clear(); redoStack.clear(); undoInit = false;
                }
                if (ImGui::MenuItem("Open Project")) loadProject();
                if (ImGui::MenuItem("Save Project", "Ctrl+S")) saveProject();
                if (ImGui::MenuItem("Save Project As...")) saveProject(std::string(), true);
                ImGui::Separator();

                if (ImGui::MenuItem("Import MIDI..."))
                {
                    if (selectedTrack < 0 || selectedTrack >= static_cast<int>(tracks.size()))
                        audioError = "Select a track first.";
                    else
                    {
                        char fp[MAX_PATH] = {};
                        OPENFILENAMEA d = {}; d.lStructSize = sizeof(d);
                        d.hwndOwner = glfwGetWin32Window(window);
                        d.lpstrFile = fp; d.nMaxFile = MAX_PATH;
                        d.lpstrFilter = "MIDI Files\0*.mid;*.midi\0All Files\0*.*\0";
                        d.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                        if (DialogOpen(&d))
                        {
                            auto r = midifile::Import(fp, selectedTrack, pianoRoll.nextId, midiNotes);
                            if (r.ok)
                            {
                                if (r.bpm > 0.0f) bpm = std::clamp(r.bpm, 20.0f, 400.0f);
                                audioError = "MIDI imported: " + std::to_string(r.noteCount) + " notes";
                            }
                            else audioError = r.error;
                        }
                    }
                }
                if (ImGui::MenuItem("Export Stems...")) exportStems();
                if (ImGui::MenuItem("Export MIDI..."))
                {
                    if (selectedTrack < 0 || selectedTrack >= static_cast<int>(tracks.size()))
                        audioError = "Select a track first.";
                    else
                    {
                        char fp[MAX_PATH] = {};
                        OPENFILENAMEA d = {}; d.lStructSize = sizeof(d);
                        d.hwndOwner = glfwGetWin32Window(window);
                        d.lpstrFile = fp; d.nMaxFile = MAX_PATH;
                        d.lpstrFilter = "MIDI Files\0*.mid\0All Files\0*.*\0";
                        d.lpstrDefExt = "mid";
                        d.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                        if (DialogSave(&d))
                            audioError = midifile::Export(fp, midiNotes, selectedTrack, bpm)
                            ? "MIDI exported." : "MIDI export failed.";
                    }
                }
                ImGui::Separator();

                if (ImGui::MenuItem("Exit"))
                    glfwSetWindowShouldClose(window, true);

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undoStack.empty())) doUndo();
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redoStack.empty())) doRedo();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Effect Add"))
            {
                if (ImGui::MenuItem("Open Node Editor"))
                {
                    if (selectedTrack >= 0 &&
                        selectedTrack < static_cast<int>(effectGraphs.size()))
                    {
                        showNodeEditor = true;
                    }
                    else
                    {
                        audioError = "Select a track before opening the Node Editor.";
                    }
                }

                ImGui::Separator();

                const struct
                {
                    const char* name;
                    int first;
                    int last;
                } categories[] =
                {
                    { "Utility & Stereo", 0, 5 },
                    { "EQ & Filters", 6, 16 },
                    { "Dynamics", 17, 22 },
                    { "Saturation & Lo-Fi", 23, 34 },
                    { "Delay, Reverb & Modulation", 35, 50 },
                    { "Creative & Utility FX", 51, 58 }
                };

                for (const auto& category : categories)
                {
                    if (ImGui::BeginMenu(category.name))
                    {
                        for (int effectIndex = category.first;
                            effectIndex <= category.last;
                            effectIndex++)
                        {
                            if (ImGui::MenuItem(EffectNames[effectIndex]))
                            {
                                addEffectToSelectedTrack(
                                    static_cast<EffectType>(effectIndex)
                                );
                            }
                        }

                        ImGui::EndMenu();
                    }
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Mixer", nullptr, &showMixer);
                ImGui::MenuItem("Volume Automation", nullptr, &showAutomation);
                ImGui::MenuItem("Markers", nullptr, &showMarkers);
                ImGui::MenuItem("Send Bus", nullptr, &showBus);
                ImGui::MenuItem("Piano Roll", nullptr, &showPianoRoll);
                ImGui::MenuItem("VST3 Rack", nullptr, &showVstRack);
                ImGui::MenuItem("Auto-save (90s)", nullptr, &autoSave);
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Layout"))
                {
                    resetLayoutFlag = true;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("Shortcuts", "F1")) showHelp = true;
                if (ImGui::MenuItem("Command Palette", "Ctrl+P")) { palette.open = true; palette.justOpened = true; palette.filter[0] = 0; palette.sel = 0; }
                ImGui::EndMenu();
            }

            {
                char st[200];
                const double pps0 = pixelsPerSecond();
                const double sec0 = pps0 > 0.0 ? static_cast<double>(playheadPosition) / pps0 : 0.0;
                const std::string pn = currentProjectPath.empty() ? std::string("(unsaved)")
                    : std::filesystem::path(currentProjectPath).filename().string();
                std::snprintf(st, sizeof(st), "%s%s  |  %.1f BPM  |  %02d:%05.2f  |  %d tracks  |  %d clips",
                    projectDirty ? "* " : "", pn.c_str(), bpm,
                    static_cast<int>(sec0 / 60.0), std::fmod(sec0, 60.0),
                    static_cast<int>(tracks.size()), static_cast<int>(audioClips.size()));
                const float tw = ImGui::CalcTextSize(st).x;
                ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - tw - 16.0f));
                ImGui::TextDisabled("%s", st);
            }
            ImGui::EndMainMenuBar();
        }

        // ---------------------------------------------------------------------
        // ドッキング環境の初期化および初期レイアウトの構築（リサイズ完全対応）
        // ---------------------------------------------------------------------
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float menuBarHeight = ImGui::GetFrameHeight();

        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + menuBarHeight)
        );
        ImGui::SetNextWindowSize(
            ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - menuBarHeight)
        );
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGuiWindowFlags hostWindowFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoDocking;

        ImGui::Begin("EasyMix Main DockSpace", nullptr, hostWindowFlags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");

        if (resetLayoutFlag)
        {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            resetLayoutFlag = false;
        }

        if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr)
        {
            ImGui::DockBuilderRemoveNode(dockspaceId);

            ImGui::DockBuilderAddNode(
                dockspaceId,
                ImGuiDockNodeFlags_DockSpace
            );

            ImGui::DockBuilderSetNodeSize(
                dockspaceId,
                ImVec2(
                    viewport->WorkSize.x,
                    viewport->WorkSize.y - menuBarHeight
                )
            );

            ImGuiID dockIdMain = dockspaceId;

            ImGuiID dockIdTop =
                ImGui::DockBuilderSplitNode(
                    dockIdMain,
                    ImGuiDir_Up,
                    0.14f,
                    nullptr,
                    &dockIdMain
                );

            ImGuiID dockIdBottom =
                ImGui::DockBuilderSplitNode(
                    dockIdMain,
                    ImGuiDir_Down,
                    0.35f,
                    nullptr,
                    &dockIdMain
                );

            ImGuiID dockIdLeft =
                ImGui::DockBuilderSplitNode(
                    dockIdMain,
                    ImGuiDir_Left,
                    0.20f,
                    nullptr,
                    &dockIdMain
                );

            ImGuiID dockIdRight =
                ImGui::DockBuilderSplitNode(
                    dockIdMain,
                    ImGuiDir_Right,
                    0.25f,
                    nullptr,
                    &dockIdMain
                );

            ImGui::DockBuilderDockWindow("Transport Bar", dockIdTop);
            ImGui::DockBuilderDockWindow("Track Panel", dockIdLeft);
            ImGui::DockBuilderDockWindow("Timeline", dockIdMain);
            ImGui::DockBuilderDockWindow("Mixer", dockIdBottom);
            ImGui::DockBuilderDockWindow("Track Equalizer", dockIdRight);
            ImGui::DockBuilderDockWindow("VST3 Rack", dockIdRight);
            ImGui::DockBuilderDockWindow("Effect Node Editor", dockIdBottom);
            ImGui::DockBuilderDockWindow("Piano Roll", dockIdBottom);

            ImGui::DockBuilderFinish(dockspaceId);
        }

        ImGui::DockSpace(
            dockspaceId,
            ImVec2(0.0f, 0.0f),
            ImGuiDockNodeFlags_None
        );

        ImGui::End();

        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
            saveProject();

        {
            ImGuiIO& kio = ImGui::GetIO();
            if (!kio.WantTextInput)
            {
                if (kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, true))
                {
                    if (kio.KeyShift) doRedo();
                    else doUndo();
                }
                else if (kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, true))
                {
                    doRedo();
                }
                else if (kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) { loadProject(); }
                else if (kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E, false)) { exportWav(); }
                else if (kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Equal, true)) { zoomBy(1.15f); }
                else if (kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Minus, true)) { zoomBy(1.0f / 1.15f); }
                else if (!kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false))
                {
                    if (playing) stopAllClips();
                    else startAllClips();
                }
                else if (!kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_End, false)) { goToEnd(); }
                else if (!kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Comma, true)) { nudgePlayhead(kio.KeyShift ? -4.0f : -1.0f); }
                else if (!kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Period, true)) { nudgePlayhead(kio.KeyShift ? 4.0f : 1.0f); }
                else if (!kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) { setLoopStart(); }
                else if (!kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) { setLoopEnd(); }
                else if (!kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Home, false))
                {
                    playheadPosition = 0.0f;
                    seekRequested = true;
                }
                else if (!kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L, false))
                {
                    loopEnabled = !loopEnabled;
                }
                else if (!kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
                {
                    metronomeOn = !metronomeOn;
                }
                else if (!kio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_M, false))
                {
                    Marker mk;
                    mk.beat = std::max(0.0f, snapToGrid(playheadPosition * 4.0f / timelineZoom));
                    mk.name = "Marker " + std::to_string(markers.size() + 1);
                    markers.push_back(mk);
                    std::sort(markers.begin(), markers.end(), [](const Marker& a, const Marker& b) { return a.beat < b.beat; });
                }
                else if (!kio.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_PageUp, false) || ImGui::IsKeyPressed(ImGuiKey_PageDown, false)))
                {
                    const float curBeat = playheadPosition * 4.0f / timelineZoom;
                    const bool next = ImGui::IsKeyPressed(ImGuiKey_PageDown, false);
                    int target = -1;
                    for (int mi = 0; mi < static_cast<int>(markers.size()); ++mi)
                    {
                        if (next && markers[mi].beat > curBeat + 0.01f) { target = mi; break; }
                        if (!next && markers[mi].beat < curBeat - 0.01f) target = mi;
                    }
                    if (target >= 0)
                    {
                        playheadPosition = markers[target].beat * timelineZoom / 4.0f;
                        seekRequested = true;
                    }
                }
            }
        }

        {
            if (prevTimelineZoom != timelineZoom || prevBpm != bpm)
            {
                const float oldScale = prevTimelineZoom * prevBpm;
                if (oldScale > 0.0f)
                    playheadPosition *= (timelineZoom * bpm) / oldScale;
                prevTimelineZoom = timelineZoom;
                prevBpm = bpm;
            }

            if (needFlush)
            {
                playheadPosition = playheadFromFrame(playFrame);
                flushPlugins();
                needFlush = false;
            }
            else if (seekRequested)
            {
                playFrame = frameFromPlayhead();
                if (playing)
                    softSeekReset();
            }
            else if (playing)
            {
                playheadPosition = playheadFromFrame(playFrame);
            }
            else
            {
                playFrame = frameFromPlayhead();
            }

            seekRequested = false;
            contentEndFrame = computeContentEnd();
        }

        ImGui::Begin(
            "Transport Bar",
            nullptr,
            ImGuiWindowFlags_NoCollapse
        );

        {
            const ImVec2 tbSize(42.0f, 32.0f);
            const bool isRec = recordingState.active.load();

            if (CubaseToggle("|<", false, tbSize))
            {
                playheadPosition = 0.0f;
                seekRequested = true;
            }
            ImGui::SameLine();
            if (CubaseToggle("[]", false, tbSize))
            {
                stopAllClips();
                playing = false;
                playheadPosition = 0.0f;
            }
            ImGui::SameLine();
            if (CubaseToggle(playing ? "||" : ">", playing, tbSize, IM_COL32(90, 200, 110, 255)))
            {
                if (playing) stopAllClips();
                else startAllClips();
            }
            ImGui::SameLine();
            if (CubaseToggle("REC", isRec, tbSize, IM_COL32(225, 60, 55, 255)))
            {
                if (isRec) stopRecording();
                else startRecording();
            }
            ImGui::SameLine();
            if (CubaseToggle("LOOP", loopEnabled, ImVec2(52.0f, 32.0f), IM_COL32(80, 160, 235, 255)))
                loopEnabled = !loopEnabled;
            ImGui::SameLine();
            if (CubaseToggle("CLICK", metronomeOn, ImVec2(56.0f, 32.0f)))
                metronomeOn = !metronomeOn;

            const double ppsTb = pixelsPerSecond();
            const double secTb = ppsTb > 0.0 ? static_cast<double>(playheadPosition) / ppsTb : 0.0;
            const double beatTb = secTb * static_cast<double>(bpm) / 60.0;

            char timeText[32];
            std::snprintf(timeText, sizeof(timeText), "%02d:%02d.%03d",
                static_cast<int>(secTb / 60.0),
                static_cast<int>(std::fmod(secTb, 60.0)),
                static_cast<int>(std::fmod(secTb, 1.0) * 1000.0));

            char barText[32];
            std::snprintf(barText, sizeof(barText), "%03d.%d.%03d",
                static_cast<int>(beatTb / 4.0) + 1,
                static_cast<int>(std::fmod(beatTb, 4.0)) + 1,
                static_cast<int>(std::fmod(beatTb, 1.0) * 480.0));

            ImGui::SameLine(0.0f, 16.0f);
            CubaseLcd(barText, ImVec2(150.0f, 32.0f), 1.5f, IM_COL32(255, 176, 60, 255));
            ImGui::SameLine();
            CubaseLcd(timeText, ImVec2(150.0f, 32.0f), 1.5f, IM_COL32(110, 225, 170, 255));

            ImGui::SameLine(0.0f, 12.0f);
            ImGui::SetNextItemWidth(92.0f);
            ImGui::DragFloat("##bpmtb", &bpm, 0.1f, 20.0f, 400.0f, "%.2f BPM");
            bpm = std::clamp(bpm, 20.0f, 400.0f);

            ImGui::SameLine();
            if (ImGui::Button("TAP"))
            {
                static double lastTapTime = 0.0;
                static float tapAvg = 0.0f;
                const double nowT = ImGui::GetTime();
                const double dtT = nowT - lastTapTime;
                if (dtT > 0.2 && dtT < 2.0)
                {
                    const float inst = 60.0f / static_cast<float>(dtT);
                    tapAvg = tapAvg > 0.0f ? tapAvg * 0.6f + inst * 0.4f : inst;
                    bpm = std::clamp(tapAvg, 20.0f, 400.0f);
                }
                else tapAvg = 0.0f;
                lastTapTime = nowT;
            }

            ImGui::SameLine();
            ImGui::TextColored(
                isRec ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                : playing ? ImVec4(0.35f, 0.9f, 0.5f, 1.0f)
                : ImVec4(0.6f, 0.6f, 0.64f, 1.0f),
                isRec ? "RECORDING" : playing ? "PLAYING" : "STOPPED");

            if (ImGui::Button("ADD TRACK", ImVec2(96.0f, 26.0f)))
            {
                tracks.push_back("Track " + std::to_string(tracks.size() + 1));
                trackVolumes.push_back(1.0f);
                trackMuted.push_back(false);
                trackSolo.push_back(false);
                trackPans.push_back(0.0f);
                trackSends.push_back(0.0f);
                trackAutomation.emplace_back();
                trackEQs.emplace_back();
                effectGraphs.emplace_back();
                trackVst.emplace_back();
                selectedTrack = static_cast<int>(tracks.size()) - 1;
            }
            ImGui::SameLine();
            if (ImGui::Button("IMPORT", ImVec2(70.0f, 26.0f))) importWav();
            ImGui::SameLine();
            if (ImGui::Button("EXPORT", ImVec2(70.0f, 26.0f))) exportWav();
            ImGui::SameLine();
            if (ImGui::Button("SAVE", ImVec2(60.0f, 26.0f))) saveProject();
            ImGui::SameLine(0.0f, 16.0f);
            if (CubaseToggle("MIXER", showMixer, ImVec2(70.0f, 26.0f))) showMixer = !showMixer;
            ImGui::SameLine();
            if (CubaseToggle("EDITOR", showPianoRoll, ImVec2(70.0f, 26.0f))) showPianoRoll = !showPianoRoll;
            ImGui::SameLine();
            if (CubaseToggle("VST3", showVstRack, ImVec2(60.0f, 26.0f))) showVstRack = !showVstRack;
            ImGui::SameLine();
            if (CubaseToggle("FX", showNodeEditor, ImVec2(46.0f, 26.0f)))
            {
                if (selectedTrack >= 0 && selectedTrack < static_cast<int>(effectGraphs.size()))
                    showNodeEditor = !showNodeEditor;
                else
                    audioError = "Select a track before opening the Node Editor.";
            }
        }
        ImGui::End();

        ImGui::Begin("Transport");
        {
            ImGui::SetNextItemWidth(90.0f);
            ImGui::DragFloat("BPM", &bpm, 0.1f, 20.0f, 400.0f, "%.1f");
            bpm = std::clamp(bpm, 20.0f, 400.0f);

            ImGui::SameLine();
            ImGui::Checkbox("Metronome", &metronomeOn);
            if (metronomeOn)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                ImGui::SliderFloat("##metvol", &metronomeVolume, 0.0f, 1.0f, "vol %.2f");
            }

            ImGui::Checkbox("Loop", &loopEnabled);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::DragFloat("Start (beat)", &loopStartBeat, 0.25f, 0.0f, 4096.0f, "%.2f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::DragFloat("End (beat)", &loopEndBeat, 0.25f, 0.0f, 4096.0f, "%.2f");
            if (loopEndBeat < loopStartBeat + 1.0f)
                loopEndBeat = loopStartBeat + 1.0f;

            ImGui::Checkbox("Follow playhead", &followPlayhead);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::DragInt("Length (bars)", &timelineBars, 0.2f, 4, 512);

            const double pps = pixelsPerSecond();
            const double sec = pps > 0.0 ? static_cast<double>(playheadPosition) / pps : 0.0;
            const double beatPos = sec * static_cast<double>(bpm) / 60.0;
            ImGui::Text("%02d:%05.2f    Bar %d  Beat %d",
                static_cast<int>(sec / 60.0),
                std::fmod(sec, 60.0),
                static_cast<int>(beatPos / 4.0) + 1,
                static_cast<int>(std::fmod(beatPos, 4.0)) + 1);

            ImGui::Separator();
            ImGui::TextUnformatted("MASTER");
            ImGui::ProgressBar(std::min(1.0f, masterPeakL), ImVec2(220.0f, 10.0f), "");
            ImGui::ProgressBar(std::min(1.0f, masterPeakR), ImVec2(220.0f, 10.0f), "");
            ImGui::Checkbox("Master Limiter", &masterLimiterOn);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::SliderFloat("##limamt", &masterLimiter.amount, 0.0f, 1.0f, "strength %.2f");

            ImGui::Separator();
            const char* bitNames[] = { "16-bit", "24-bit", "32-bit float" };
            ImGui::SetNextItemWidth(120.0f);
            ImGui::Combo("Export format", &exportBits, bitNames, IM_ARRAYSIZE(bitNames));
            ImGui::SetNextItemWidth(120.0f);
            ImGui::SliderFloat("Rec latency (ms)", &recordLatencyMs, 0.0f, 300.0f, "%.0f");
        }
        ImGui::End();

        ImGui::Begin("Track Panel");

        ImGui::Text("TRACKS");
        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(tracks.size()); i++)
        {
            if (renamingTrack && selectedTrack == i)
            {
                ImGui::SetNextItemWidth(-1);

                if (ImGui::InputText(
                    "##RenameTrack",
                    renameBuffer,
                    sizeof(renameBuffer),
                    ImGuiInputTextFlags_EnterReturnsTrue
                ))
                {
                    if (renameBuffer[0] != '\0')
                        tracks[i] = renameBuffer;

                    renamingTrack = false;
                }

                if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                    renamingTrack = false;
            }
            else
            {
                if (ImGui::Selectable(
                    tracks[i].c_str(),
                    selectedTrack == i
                ))
                {
                    selectedTrack = i;
                }

                if (ImGui::IsItemHovered() &&
                    ImGui::IsMouseDoubleClicked(
                        ImGuiMouseButton_Left))
                {
                    selectedTrack = i;

                    strcpy_s(
                        renameBuffer,
                        sizeof(renameBuffer),
                        tracks[i].c_str()
                    );

                    renamingTrack = true;
                }
            }
        }

        ImGui::Separator();

        if (ImGui::Button("ADD TRACK"))
        {
            tracks.push_back(
                "Track " + std::to_string(tracks.size() + 1)
            );

            trackVolumes.push_back(1.0f);
            trackMuted.push_back(false);
            trackSolo.push_back(false);
            trackPans.push_back(0.0f);
            trackSends.push_back(0.0f);
            trackAutomation.emplace_back();
            trackEQs.emplace_back();
            effectGraphs.emplace_back();
            trackVst.emplace_back();

            selectedTrack =
                static_cast<int>(tracks.size()) - 1;
        }

        ImGui::SameLine();

        if (ImGui::Button("DELETE TRACK"))
        {
            if (selectedTrack >= 0 &&
                selectedTrack < static_cast<int>(tracks.size()))
            {
                int deletedTrack = selectedTrack;

                stopAllClips();
                clearMidiPlayback();
                playing = false;

                selectedClipPtr = nullptr;
                contextClipIndex = -1;
                draggingClipIndex = -1;
                draggingAudioClip = false;

                audioClips.erase(
                    std::remove_if(
                        audioClips.begin(),
                        audioClips.end(),
                        [deletedTrack](const std::unique_ptr<AudioClip>& c) { return c->trackIndex == deletedTrack; }
                    ),
                    audioClips.end()
                );

                for (auto& clip : audioClips)
                {
                    if (clip->trackIndex > deletedTrack)
                        clip->trackIndex--;
                }

                midiNotes.erase(
                    std::remove_if(
                        midiNotes.begin(),
                        midiNotes.end(),
                        [deletedTrack](const MidiNote& n) { return n.trackIndex == deletedTrack; }
                    ),
                    midiNotes.end()
                );
                for (auto& n : midiNotes)
                    if (n.trackIndex > deletedTrack) n.trackIndex--;
                pianoRoll.selectedId = -1;
                pianoRoll.dragId = -1;

                trackVolumes.erase(trackVolumes.begin() + deletedTrack);
                trackMuted.erase(trackMuted.begin() + deletedTrack);
                trackSolo.erase(trackSolo.begin() + deletedTrack);
                trackPans.erase(trackPans.begin() + deletedTrack);
                if (static_cast<int>(trackSends.size()) > deletedTrack)
                    trackSends.erase(trackSends.begin() + deletedTrack);
                if (static_cast<int>(trackSynth.size()) > deletedTrack)
                    trackSynth.erase(trackSynth.begin() + deletedTrack);
                trackAutomation.erase(trackAutomation.begin() + deletedTrack);
                trackEQs.erase(trackEQs.begin() + deletedTrack);
                effectGraphs.erase(effectGraphs.begin() + deletedTrack);
                trackVst.erase(trackVst.begin() + deletedTrack);
                tracks.erase(tracks.begin() + deletedTrack);
                trackRT.clear();
                trackBuf.clear();

                selectedTrack = -1;
                renamingTrack = false;
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Up")) swapTracks(selectedTrack, selectedTrack - 1);
        ImGui::SameLine();
        if (ImGui::Button("Down")) swapTracks(selectedTrack, selectedTrack + 1);
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) duplicateTrack(selectedTrack);
        if (ImGui::Button("Unmute/Unsolo All")) { std::fill(trackMuted.begin(), trackMuted.end(), false); std::fill(trackSolo.begin(), trackSolo.end(), false); }
        ImGui::SameLine();
        if (ImGui::Button("Reset Faders")) resetFaders();

        ImGui::End();

        if (showMixer)
        {
            ImGui::Begin("Mixer", &showMixer);
            ImGui::Checkbox("Snap", &snapEnabled);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::BeginCombo("Grid", snapOptionNames[
                snapBeat == 1.0f ? 0 :
                    snapBeat == 0.5f ? 1 :
                    snapBeat == 0.25f ? 2 :
                    snapBeat == 0.125f ? 3 : 4
            ]))
            {
                for (int i = 0; i < IM_ARRAYSIZE(snapOptions); i++)
                {
                    bool selected = snapBeat == snapOptions[i];
                    if (ImGui::Selectable(snapOptionNames[i], selected))
                        snapBeat = snapOptions[i];
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::Separator();

            if (trackSends.size() != tracks.size()) trackSends.resize(tracks.size(), 0.0f);
            const float stripW = 96.0f;
            const float stripH = 362.0f;
            const float faderH = 190.0f;

            for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
            {
                ImGui::PushID(i);
                if (i > 0) ImGui::SameLine();

                ImGui::BeginChild("strip", ImVec2(stripW, stripH), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
                ImDrawList* sd = ImGui::GetWindowDrawList();

                const ImVec2 sp = ImGui::GetCursorScreenPos();
                sd->AddRectFilled(sp, ImVec2(sp.x + stripW - 16.0f, sp.y + 4.0f), CubaseTrackColor(i));
                ImGui::Dummy(ImVec2(0.0f, 6.0f));

                if (ImGui::Selectable(tracks[i].c_str(), selectedTrack == i))
                    selectedTrack = i;

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderFloat("##pan", &trackPans[i], -1.0f, 1.0f, "P %.2f");
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) trackPans[i] = 0.0f;

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderFloat("##send", &trackSends[i], 0.0f, 1.0f, "Send %.2f");
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) trackSends[i] = 0.0f;

                const bool mutedNow = trackMuted[i];
                const bool soloNow = trackSolo[i];
                if (CubaseToggle("M", mutedNow, ImVec2(36.0f, 22.0f), IM_COL32(225, 70, 60, 255)))
                    trackMuted[i] = !mutedNow;
                ImGui::SameLine();
                if (CubaseToggle("S", soloNow, ImVec2(36.0f, 22.0f), IM_COL32(240, 205, 60, 255)))
                    trackSolo[i] = !soloNow;

                ImGui::VSliderFloat("##vol", ImVec2(34.0f, faderH), &trackVolumes[i], 0.0f, 2.0f, "");
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) trackVolumes[i] = 1.0f;
                ImGui::SameLine();

                if (trackHold.size() != tracks.size()) trackHold.resize(tracks.size());
                DrawMeterBar(ImVec2(14.0f, faderH), i < static_cast<int>(trackPeak.size()) ? trackPeak[i] : 0.0f, trackHold[i]);

                ImGui::Text("%.1f dB", 20.0f * std::log10(std::max(trackVolumes[i], 0.0001f)));

                ImGui::EndChild();
                ImGui::PopID();
            }

            ImGui::SameLine();
            ImGui::BeginChild("masterstrip", ImVec2(stripW + 20.0f, stripH), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
            {
                ImDrawList* sd = ImGui::GetWindowDrawList();
                const ImVec2 sp = ImGui::GetCursorScreenPos();
                sd->AddRectFilled(sp, ImVec2(sp.x + stripW, sp.y + 4.0f), IM_COL32(217, 133, 31, 255));
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::TextUnformatted("MASTER");
                ImGui::Checkbox("Limiter", &masterLimiterOn);
                ImGui::Dummy(ImVec2(0.0f, 4.0f));

                ImGui::VSliderFloat("##master", ImVec2(34.0f, faderH), &masterVolume, 0.0f, 2.0f, "");
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) masterVolume = 1.0f;
                ImGui::SameLine();

                DrawMeterBar(ImVec2(12.0f, faderH), masterPeakL, phMasterL);
                ImGui::SameLine();
                DrawMeterBar(ImVec2(12.0f, faderH), masterPeakR, phMasterR);

                ImGui::Text("%.1f dB", 20.0f * std::log10(std::max(masterVolume, 0.0001f)));
            }
            ImGui::EndChild();
            ImGui::End();
        }

        if (showAutomation && selectedTrack >= 0 && selectedTrack < static_cast<int>(tracks.size()))
        {
            ImGui::Begin("Volume Automation", &showAutomation);
            auto& points = trackAutomation[selectedTrack];
            if (ImGui::Button("Add Point"))
            {
                float t = playheadPosition * 240.0f / (timelineZoom * bpm);
                points.push_back({ t, 1.0f });
                std::sort(points.begin(), points.end(), [](const AutomationPoint& a, const AutomationPoint& b) { return a.time < b.time; });
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Automation")) points.clear();
            for (size_t i = 0; i < points.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                ImGui::SetNextItemWidth(100); ImGui::DragFloat("Time (s)", &points[i].time, 0.01f, 0.0f, 36000.0f);
                ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::SliderFloat("Volume", &points[i].value, 0.0f, 2.0f);
                ImGui::SameLine(); if (ImGui::SmallButton("X")) { points.erase(points.begin() + i); ImGui::PopID(); break; }
                ImGui::PopID();
            }
            std::sort(points.begin(), points.end(), [](const AutomationPoint& a, const AutomationPoint& b) { return a.time < b.time; });
            ImGui::End();
        }

        if (selectedTrack >= 0 &&
            selectedTrack < static_cast<int>(tracks.size()))
        {
            ImGui::Begin("Track Equalizer");

            Equalizer& eq = trackEQs[selectedTrack];

            ImGui::Text(
                "Track: %s",
                tracks[selectedTrack].c_str()
            );

            ImGui::Separator();

            ImGui::Checkbox("EQ Enabled", &eq.enabled);
            spectrum.update();
            DrawEqGraph("eqgraph", eq, ImVec2(-1.0f, 190.0f), &spectrum);

            const char* bandNames[] =
            {
                "LOW",
                "LOW MID",
                "MID",
                "HIGH MID",
                "HIGH"
            };

            for (int i = 0; i < Equalizer::BandCount; i++)
            {
                ImGui::PushID(i);

                ImGui::Text("%s", bandNames[i]);

                ImGui::SetNextItemWidth(-1);

                ImGui::SliderFloat(
                    "Frequency",
                    &eq.frequencies[i],
                    20.0f,
                    20000.0f,
                    "%.0f Hz",
                    ImGuiSliderFlags_Logarithmic
                );

                ImGui::SetNextItemWidth(-1);

                ImGui::SliderFloat(
                    "Gain",
                    &eq.gains[i],
                    -18.0f,
                    18.0f,
                    "%.1f dB"
                );

                ImGui::SetNextItemWidth(-1);

                ImGui::SliderFloat(
                    "Q",
                    &eq.qValues[i],
                    0.1f,
                    10.0f,
                    "%.2f"
                );

                ImGui::Separator();

                ImGui::PopID();
            }

            if (ImGui::Button("RESET EQ"))
                eq.resetParameters();

            ImGui::SameLine();
            {
                static int eqPresetSel = 0;
                static const char* presetNames[] = { "Preset...", "Vocal", "Bass Boost", "Bright", "Telephone", "Mid Scoop", "Warm" };
                static const float presetGains[7][5] = {
                    { 0, 0, 0, 0, 0 }, { -3, -2, 0, 3, 2 }, { 6, 2, 0, 0, 0 }, { 0, 0, 0, 3, 5 },
                    { -12, -6, 4, 5, -10 }, { 2, -3, -5, -3, 2 }, { 3, 2, 0, -2, -3 } };
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::Combo("##eqpreset", &eqPresetSel, presetNames, IM_ARRAYSIZE(presetNames)) && eqPresetSel > 0)
                {
                    eq.resetParameters();
                    for (int b = 0; b < Equalizer::BandCount; ++b)
                        eq.gains[b] = presetGains[eqPresetSel][b];
                    eq.enabled = true;
                    eqPresetSel = 0;
                }
            }

            ImGui::End();
        }

        if (showPianoRoll)
        {
            ImGui::SetNextWindowSize(ImVec2(900, 450), ImGuiCond_FirstUseEver);
            ImGui::Begin("Piano Roll", &showPianoRoll);

            if (selectedTrack >= 0 && selectedTrack < static_cast<int>(tracks.size()))
            {
                ImGui::Text("Track: %s", tracks[selectedTrack].c_str());

                if (ImGui::Button("Scan VST3"))
                {
                    vst3Found = ScanVst3Folders();
                    pianoRollVstSelection = 0;
                }

                ImGui::SameLine();

                ImGui::TextDisabled(
                    "%d VST3 plugins found",
                    static_cast<int>(vst3Found.size())
                );

                if (!vst3Found.empty())
                {
                    pianoRollVstSelection = std::clamp(
                        pianoRollVstSelection,
                        0,
                        static_cast<int>(vst3Found.size()) - 1
                    );

                    std::string preview =
                        std::filesystem::path(
                            vst3Found[pianoRollVstSelection]
                        ).stem().string();

                    if (ImGui::BeginCombo(
                        "VST3 Instrument",
                        preview.c_str()))
                    {
                        for (int i = 0; i < static_cast<int>(vst3Found.size()); ++i)
                        {
                            std::string pluginName =
                                std::filesystem::path(vst3Found[i]).stem().string();

                            bool selected = pianoRollVstSelection == i;

                            if (ImGui::Selectable(pluginName.c_str(), selected))
                                pianoRollVstSelection = i;

                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }

                        ImGui::EndCombo();
                    }

                    ImGui::SameLine();

                    if (ImGui::Button("Load Instrument"))
                    {
                        if (selectedTrack >= 0 &&
                            selectedTrack < static_cast<int>(trackVst.size()))
                        {
                            auto plugin = std::make_shared<Vst3Plugin>();

                            if (plugin->load(vst3Found[pianoRollVstSelection], 44100.0, 4096))
                            {
                                if (!plugin->instrument)
                                {
                                    audioError = "Selected VST3 is an effect, not an instrument.";
                                }
                                else
                                {
                                    stopAllClips();
                                    clearMidiPlayback();

                                    auto& chain = trackVst[selectedTrack];

                                    chain.erase(
                                        std::remove_if(
                                            chain.begin(),
                                            chain.end(),
                                            [](const std::shared_ptr<Vst3Plugin>& p)
                                            {
                                                return p && p->instrument;
                                            }
                                        ),
                                        chain.end()
                                    );

                                    chain.insert(chain.begin(), plugin);

                                    audioError.clear();
                                }
                            }
                            else
                            {
                                audioError = "VST3 load failed: " + plugin->lastError;
                            }
                        }
                    }
                }

                if (selectedTrack >= 0 &&
                    selectedTrack < static_cast<int>(trackVst.size()))
                {
                    std::string instrumentName = "Built-in Synth";

                    for (const auto& plugin : trackVst[selectedTrack])
                    {
                        if (plugin && plugin->instrument)
                        {
                            instrumentName = plugin->name;
                            break;
                        }
                    }

                    if (instrumentName != "Built-in Synth")
                    {
                        ImGui::Text("Current Instrument: %s (VST3)", instrumentName.c_str());
                    }
                    else
                    {
                        synth::Setup& su = synthSetup(selectedTrack);
                        const auto& bank = synth::Patches();
                        su.preset = std::clamp(su.preset, 0, static_cast<int>(bank.size()) - 1);
                        static char instFilter[40] = "";

                        ImGui::SetNextItemWidth(230.0f);
                        if (ImGui::BeginCombo("Instrument", bank[su.preset].name, ImGuiComboFlags_HeightLargest))
                        {
                            ImGui::SetNextItemWidth(-1.0f);
                            ImGui::InputTextWithHint("##instfilter", "search...", instFilter, sizeof(instFilter));
                            std::string flt = instFilter;
                            std::transform(flt.begin(), flt.end(), flt.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            const char* lastCat = nullptr;
                            for (int pi = 0; pi < static_cast<int>(bank.size()); ++pi)
                            {
                                std::string nm = std::string(bank[pi].name) + " " + bank[pi].category;
                                std::transform(nm.begin(), nm.end(), nm.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                                if (!flt.empty() && nm.find(flt) == std::string::npos) continue;
                                if (!lastCat || std::strcmp(lastCat, bank[pi].category) != 0)
                                {
                                    lastCat = bank[pi].category;
                                    ImGui::TextDisabled("-- %s --", lastCat);
                                }
                                const bool selP = (pi == su.preset);
                                if (ImGui::Selectable(bank[pi].name, selP))
                                {
                                    su.preset = pi;
                                    if (pianoRoll.audition) pianoRoll.audition(60, 0.8f, false);
                                }
                                if (selP) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("<##inst")) setInstrument(su.preset - 1);
                        ImGui::SameLine();
                        if (ImGui::Button(">##inst")) setInstrument(su.preset + 1);
                        ImGui::SameLine();
                        ImGui::TextDisabled("%d / %d", su.preset + 1, static_cast<int>(bank.size()));
                        ImGui::SameLine();
                        if (ImGui::Button("Audition")) pianoRoll.audition(60, 0.8f, false);

                        ImGui::SetNextItemWidth(90.0f);
                        ImGui::SliderFloat("Vol##syn", &su.gain, 0.0f, 2.0f, "%.2f");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(90.0f);
                        ImGui::SliderFloat("Tone##syn", &su.bright, 0.0f, 1.0f, "%.2f");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(90.0f);
                        ImGui::SliderFloat("Attack##syn", &su.attack, 0.0f, 1.0f, "%.2f");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(90.0f);
                        ImGui::SliderFloat("Release##syn", &su.release, 0.0f, 1.0f, "%.2f");
                        ImGui::SameLine();
                        if (ImGui::Button("Reset##syn")) { su.gain = 1.0f; su.bright = 0.5f; su.attack = 0.5f; su.release = 0.5f; }
                    }
                }

                ImGui::TextDisabled("(Snap / Grid use the Mixer window settings)");

                float playheadBeat = playheadPosition * 4.0f / timelineZoom;
                DrawPianoRoll(
                    pianoRoll,
                    midiNotes,
                    selectedTrack,
                    timelineBars * 4.0f,
                    snapEnabled,
                    snapBeat,
                    playheadBeat,
                    playing
                );
                if (pianoRoll.seekBeat >= 0.0f)
                {
                    playheadPosition = pianoRoll.seekBeat * timelineZoom / 4.0f;
                    seekRequested = true;
                    pianoRoll.seekBeat = -1.0f;
                }
            }
            else
            {
                ImGui::Text("Select a track in Track Panel.");
            }

            ImGui::End();
        }

        if (showVstRack)
        {
            ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
            ImGui::Begin("VST3 Rack", &showVstRack);

            if (ImGui::Button("Scan VST3"))
                vst3Found = ScanVst3Folders();
            ImGui::SameLine();
            ImGui::TextDisabled("%d plugins found", static_cast<int>(vst3Found.size()));

            ImGui::BeginChild("VstList", ImVec2(0, 160), ImGuiChildFlags_Borders);
            for (int i = 0; i < static_cast<int>(vst3Found.size()); ++i)
            {
                ImGui::PushID(i);
                if (ImGui::Button("Add"))
                {
                    if (selectedTrack >= 0 && selectedTrack < static_cast<int>(trackVst.size()))
                    {
                        auto plug = std::make_shared<Vst3Plugin>();
                        if (plug->load(vst3Found[i], 44100.0, 4096))
                        {
                            trackVst[selectedTrack].push_back(plug);
                            audioError.clear();
                        }
                        else
                        {
                            audioError = "VST3: " + plug->lastError;
                        }
                    }
                    else
                    {
                        audioError = "Select a track before adding a VST3 plugin.";
                    }
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(
                    std::filesystem::path(vst3Found[i]).stem().string().c_str());
                ImGui::PopID();
            }
            ImGui::EndChild();

            ImGui::Separator();

            if (selectedTrack >= 0 && selectedTrack < static_cast<int>(trackVst.size()))
            {
                ImGui::Text("Track: %s", tracks[selectedTrack].c_str());
                auto& chain = trackVst[selectedTrack];
                int removeIndex = -1;

                for (int i = 0; i < static_cast<int>(chain.size()); ++i)
                {
                    ImGui::PushID(1000 + i);
                    ImGui::Text("%s [%s]", chain[i]->name.c_str(),
                        chain[i]->instrument ? "Instrument" : "Effect");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Editor"))
                    {
                        if (!chain[i]->openEditor(glfwGetWin32Window(window)))
                            audioError = "VST3: " + chain[i]->lastError;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove"))
                        removeIndex = i;
                    ImGui::PopID();
                }

                if (removeIndex >= 0)
                {
                    stopAllClips();
                    playing = false;
                    chain.erase(chain.begin() + removeIndex);
                }
            }
            else
            {
                ImGui::TextDisabled("Select a track to see its plugins.");
            }

            ImGui::End();
        }

        if (showNodeEditor &&
            (selectedTrack < 0 ||
                selectedTrack >= static_cast<int>(effectGraphs.size())))
        {
            ImGui::Begin("Effect Node Editor", &showNodeEditor);
            ImGui::Text("Select a track in Track Panel to edit its effects.");
            ImGui::End();
        }

        if (showNodeEditor &&
            selectedTrack >= 0 &&
            selectedTrack < static_cast<int>(effectGraphs.size()))
        {
            ImGui::Begin("Effect Node Editor", &showNodeEditor);

            ImGui::Text(
                "Track: %s",
                tracks[selectedTrack].c_str()
            );
            ImGui::SameLine();
            ImGui::TextDisabled("Connect output pins to input pins");

            EffectGraph& graph = effectGraphs[selectedTrack];

            ImGui::BeginChild(
                "EffectNodeCanvas",
                ImVec2(0.0f, 390.0f),
                ImGuiChildFlags_Borders,
                ImGuiWindowFlags_HorizontalScrollbar |
                ImGuiWindowFlags_AlwaysHorizontalScrollbar
            );

            ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
            ImVec2 canvasSize(
                std::max(
                    std::max(1400.0f, ImGui::GetContentRegionAvail().x),
                    420.0f + static_cast<float>(graph.nodes.size()) * 180.0f
                ),
                370.0f
            );

            ImGui::InvisibleButton(
                "EffectNodeCanvasInput",
                canvasSize,
                ImGuiButtonFlags_MouseButtonLeft |
                ImGuiButtonFlags_MouseButtonRight
            );

            ImDrawList* nodeDraw = ImGui::GetWindowDrawList();
            ImVec2 canvasEnd(
                canvasOrigin.x + canvasSize.x,
                canvasOrigin.y + canvasSize.y
            );

            nodeDraw->AddRectFilled(
                canvasOrigin,
                canvasEnd,
                IM_COL32(25, 27, 32, 255)
            );

            for (float gx = 0.0f; gx < canvasSize.x; gx += 40.0f)
            {
                nodeDraw->AddLine(
                    ImVec2(canvasOrigin.x + gx, canvasOrigin.y),
                    ImVec2(canvasOrigin.x + gx, canvasEnd.y),
                    IM_COL32(42, 45, 52, 255)
                );
            }

            for (float gy = 0.0f; gy < canvasSize.y; gy += 40.0f)
            {
                nodeDraw->AddLine(
                    ImVec2(canvasOrigin.x, canvasOrigin.y + gy),
                    ImVec2(canvasEnd.x, canvasOrigin.y + gy),
                    IM_COL32(42, 45, 52, 255)
                );
            }

            constexpr float nodeWidth = 154.0f;
            constexpr float nodeHeight = 92.0f;

            auto getInput = [&](int id) -> ImVec2
                {
                    if (id == 1)
                        return ImVec2(canvasOrigin.x + 25.0f, canvasOrigin.y + 185.0f);
                    if (id == 2)
                        return ImVec2(canvasEnd.x - 145.0f, canvasOrigin.y + 185.0f);

                    for (const auto& n : graph.nodes)
                        if (n.id == id)
                            return ImVec2(
                                canvasOrigin.x + n.position.x,
                                canvasOrigin.y + n.position.y + nodeHeight * 0.5f
                            );

                    return ImVec2(0, 0);
                };

            auto getOutput = [&](int id) -> ImVec2
                {
                    if (id == 1)
                        return ImVec2(canvasOrigin.x + 145.0f, canvasOrigin.y + 185.0f);
                    if (id == 2)
                        return ImVec2(canvasEnd.x - 25.0f, canvasOrigin.y + 185.0f);

                    for (const auto& n : graph.nodes)
                        if (n.id == id)
                            return ImVec2(
                                canvasOrigin.x + n.position.x + nodeWidth,
                                canvasOrigin.y + n.position.y + nodeHeight * 0.5f
                            );

                    return ImVec2(0, 0);
                };

            for (const auto& link : graph.links)
            {
                ImVec2 a = getOutput(link.from);
                ImVec2 b = getInput(link.to);
                nodeDraw->AddBezierCubic(
                    a,
                    ImVec2(a.x + 65.0f, a.y),
                    ImVec2(b.x - 65.0f, b.y),
                    b,
                    IM_COL32(80, 190, 255, 255),
                    3.0f
                );
            }

            ImVec2 sourceMin(canvasOrigin.x + 25.0f, canvasOrigin.y + 145.0f);
            ImVec2 sourceMax(sourceMin.x + 120.0f, sourceMin.y + 80.0f);

            nodeDraw->AddRectFilled(
                sourceMin, sourceMax, IM_COL32(55, 90, 125, 255), 7.0f
            );
            nodeDraw->AddRect(sourceMin, sourceMax, IM_COL32(110, 175, 230, 255), 7.0f);
            nodeDraw->AddText(
                ImVec2(sourceMin.x + 30.0f, sourceMin.y + 30.0f),
                IM_COL32(255, 255, 255, 255),
                "TRACK IN"
            );

            ImVec2 outputMin(canvasEnd.x - 145.0f, canvasOrigin.y + 145.0f);
            ImVec2 outputMax(outputMin.x + 120.0f, outputMin.y + 80.0f);

            nodeDraw->AddRectFilled(
                outputMin, outputMax, IM_COL32(65, 105, 75, 255), 7.0f
            );
            nodeDraw->AddRect(outputMin, outputMax, IM_COL32(125, 205, 140, 255), 7.0f);
            nodeDraw->AddText(
                ImVec2(outputMin.x + 32.0f, outputMin.y + 30.0f),
                IM_COL32(255, 255, 255, 255),
                "TRACK OUT"
            );

            nodeDraw->AddCircleFilled(getOutput(1), 6.0f, IM_COL32(100, 200, 255, 255));
            nodeDraw->AddCircleFilled(getInput(2), 6.0f, IM_COL32(130, 220, 150, 255));

            for (auto& node : graph.nodes)
            {
                ImVec2 p(
                    canvasOrigin.x + node.position.x,
                    canvasOrigin.y + node.position.y
                );

                ImVec2 pMax(p.x + nodeWidth, p.y + nodeHeight);

                ImU32 fill = node.id == graph.selectedNode
                    ? IM_COL32(65, 85, 115, 255)
                    : IM_COL32(52, 56, 66, 255);

                nodeDraw->AddRectFilled(p, pMax, fill, 7.0f);
                nodeDraw->AddRect(
                    p,
                    pMax,
                    node.enabled
                    ? IM_COL32(115, 145, 185, 255)
                    : IM_COL32(95, 95, 100, 255),
                    7.0f,
                    0,
                    2.0f
                );

                nodeDraw->AddText(
                    ImVec2(p.x + 10.0f, p.y + 12.0f),
                    IM_COL32(245, 245, 250, 255),
                    EffectNames[static_cast<int>(node.type)]
                );

                nodeDraw->AddText(
                    ImVec2(p.x + 10.0f, p.y + 38.0f),
                    IM_COL32(170, 180, 195, 255),
                    node.enabled ? "ENABLED" : "BYPASS"
                );

                ImVec2 input = getInput(node.id);
                ImVec2 output = getOutput(node.id);

                nodeDraw->AddCircleFilled(
                    input, 6.0f, IM_COL32(130, 220, 150, 255)
                );
                nodeDraw->AddCircleFilled(
                    output, 6.0f, IM_COL32(100, 200, 255, 255)
                );
            }

            if (graph.pendingConnection != -1)
            {
                ImVec2 a = getOutput(graph.pendingConnection);
                ImVec2 b = ImGui::GetIO().MousePos;
                nodeDraw->AddLine(
                    a, b, IM_COL32(255, 210, 90, 255), 2.5f
                );
            }

            ImVec2 mouse = ImGui::GetIO().MousePos;

            auto nearPoint = [&](ImVec2 a, ImVec2 b, float radius)
                {
                    float dx = a.x - b.x;
                    float dy = a.y - b.y;
                    return dx * dx + dy * dy <= radius * radius;
                };

            if (ImGui::IsItemHovered() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                bool clickedPort = false;

                if (nearPoint(mouse, getOutput(1), 12.0f))
                {
                    graph.pendingConnection = 1;
                    clickedPort = true;
                }
                else if (nearPoint(mouse, getInput(2), 12.0f))
                {
                    if (graph.pendingConnection != -1 &&
                        graph.pendingConnection != 2)
                    {
                        int from = graph.pendingConnection;
                        graph.links.erase(
                            std::remove_if(
                                graph.links.begin(),
                                graph.links.end(),
                                [from](const EffectLink& l)
                                {
                                    return l.to == 2 || l.from == from;
                                }
                            ),
                            graph.links.end()
                        );
                        graph.links.push_back({ from, 2 });
                    }
                    graph.pendingConnection = -1;
                    clickedPort = true;
                }

                for (auto& node : graph.nodes)
                {
                    if (nearPoint(mouse, getOutput(node.id), 12.0f))
                    {
                        graph.pendingConnection = node.id;
                        clickedPort = true;
                        break;
                    }

                    if (nearPoint(mouse, getInput(node.id), 12.0f))
                    {
                        if (graph.pendingConnection != -1 &&
                            graph.pendingConnection != node.id)
                        {
                            int from = graph.pendingConnection;
                            int to = node.id;
                            graph.links.erase(
                                std::remove_if(
                                    graph.links.begin(),
                                    graph.links.end(),
                                    [from, to](const EffectLink& l)
                                    {
                                        return l.from == from || l.to == to;
                                    }
                                ),
                                graph.links.end()
                            );
                            graph.links.push_back({ from, to });
                        }
                        graph.pendingConnection = -1;
                        clickedPort = true;
                        break;
                    }
                }

                if (!clickedPort)
                {
                    graph.movingNode = -1;

                    for (auto& node : graph.nodes)
                    {
                        ImVec2 p(
                            canvasOrigin.x + node.position.x,
                            canvasOrigin.y + node.position.y
                        );

                        bool inside =
                            mouse.x >= p.x && mouse.x <= p.x + nodeWidth &&
                            mouse.y >= p.y && mouse.y <= p.y + nodeHeight;

                        if (inside)
                        {
                            graph.selectedNode = node.id;
                            graph.movingNode = node.id;
                            graph.dragOffset = ImVec2(
                                mouse.x - p.x,
                                mouse.y - p.y
                            );
                            break;
                        }
                    }
                }
            }

            if (graph.movingNode != -1 &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                auto node = std::find_if(
                    graph.nodes.begin(),
                    graph.nodes.end(),
                    [&](const EffectNode& n) { return n.id == graph.movingNode; }
                );

                if (node != graph.nodes.end())
                {
                    node->position.x = std::max(
                        5.0f,
                        mouse.x - canvasOrigin.x - graph.dragOffset.x
                    );
                    node->position.y = std::clamp(
                        mouse.y - canvasOrigin.y - graph.dragOffset.y,
                        5.0f,
                        canvasSize.y - nodeHeight - 5.0f
                    );
                }
            }

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                graph.movingNode = -1;

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
                ImGui::IsItemHovered())
            {
                graph.pendingConnection = -1;
            }

            ImGui::EndChild();

            if (graph.selectedNode != -1)
            {
                auto node = std::find_if(
                    graph.nodes.begin(),
                    graph.nodes.end(),
                    [&](const EffectNode& n) { return n.id == graph.selectedNode; }
                );

                if (node != graph.nodes.end())
                {
                    ImGui::Separator();
                    ImGui::Text(
                        "Selected: %s",
                        EffectNames[static_cast<int>(node->type)]
                    );
                    ImGui::Checkbox("Enabled", &node->enabled);
                    ImGui::SliderFloat("Amount", &node->amount, 0.0f, 1.0f);
                    ImGui::SliderFloat("Wet", &node->wet, 0.0f, 1.0f);
                    ImGui::SliderFloat("Frequency", &node->frequency, 20.0f, 20000.0f, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
                    ImGui::SliderFloat("Time", &node->time, 0.01f, 2.0f, "%.2f s");
                    ImGui::SliderFloat("Feedback", &node->feedback, 0.0f, 0.9f);

                    if (ImGui::Button("DELETE NODE"))
                    {
                        int predecessor = -1;
                        int successor = -1;

                        for (const auto& link : graph.links)
                        {
                            if (link.to == node->id)
                                predecessor = link.from;
                            if (link.from == node->id)
                                successor = link.to;
                        }

                        int removeId = node->id;

                        graph.links.erase(
                            std::remove_if(
                                graph.links.begin(),
                                graph.links.end(),
                                [removeId](const EffectLink& l)
                                {
                                    return l.from == removeId || l.to == removeId;
                                }
                            ),
                            graph.links.end()
                        );

                        graph.nodes.erase(node);

                        if (predecessor != -1 && successor != -1)
                            graph.links.push_back({ predecessor, successor });

                        graph.selectedNode = -1;
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("Select a node to edit its parameters.");
            }

            ImGui::End();
        }

        if (showBus)
        {
            ImGui::SetNextWindowSize(ImVec2(320.0f, 220.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("Send Bus", &showBus);

            ImGui::Checkbox("Enabled", &bus.on);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo("##busfx", &bus.index, kBusNames, static_cast<int>(sizeof(kBusNames) / sizeof(kBusNames[0])));
            ImGui::SliderFloat("Return", &bus.ret, 0.0f, 2.0f);
            ImGui::SliderFloat("Amount", &bus.amount, 0.0f, 1.0f);
            if (!BusIsReverb(bus.index))
            {
                ImGui::SliderFloat("Time", &bus.time, 0.01f, 2.0f, "%.2f s");
                ImGui::SliderFloat("Feedback", &bus.feedback, 0.0f, 0.9f);
            }
            ImGui::Separator();
            ImGui::TextDisabled("Per-track send: Send slider in the Mixer strip.");

            ImGui::End();
        }

        if (showMarkers)
        {
            ImGui::SetNextWindowSize(ImVec2(340.0f, 260.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("Markers", &showMarkers);

            if (ImGui::Button("Add at Playhead (M)"))
            {
                Marker mk;
                mk.beat = std::max(0.0f, snapToGrid(playheadPosition * 4.0f / timelineZoom));
                mk.name = "Marker " + std::to_string(markers.size() + 1);
                markers.push_back(mk);
                std::sort(markers.begin(), markers.end(), [](const Marker& a, const Marker& b) { return a.beat < b.beat; });
            }
            ImGui::SameLine();
            ImGui::TextDisabled("PageUp / PageDown: prev / next");
            ImGui::Separator();

            int removeMarker = -1;
            for (int mi = 0; mi < static_cast<int>(markers.size()); ++mi)
            {
                ImGui::PushID(mi);
                if (ImGui::SmallButton(">"))
                {
                    playheadPosition = markers[mi].beat * timelineZoom / 4.0f;
                    seekRequested = true;
                }
                ImGui::SameLine();

                char nameBuf[64];
                std::snprintf(nameBuf, sizeof(nameBuf), "%s", markers[mi].name.c_str());
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::InputText("##mname", nameBuf, sizeof(nameBuf)))
                    markers[mi].name = nameBuf;

                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragFloat("##mbeat", &markers[mi].beat, 0.25f, 0.0f, 4096.0f, "beat %.2f");

                ImGui::SameLine();
                if (ImGui::SmallButton("X")) removeMarker = mi;
                ImGui::PopID();
            }
            if (removeMarker >= 0)
                markers.erase(markers.begin() + removeMarker);

            ImGui::End();
        }

        ImGui::Begin("Timeline");

        ImGui::Text("ARRANGEMENT");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(150);

        ImGui::SliderFloat(
            "Zoom",
            &timelineZoom,
            30.0f,
            200.0f
        );

        ImGui::Separator();

        ImGui::BeginChild(
            "TimelineArea",
            ImVec2(0, 0),
            ImGuiChildFlags_Borders,
            ImGuiWindowFlags_HorizontalScrollbar
        );

        ImDrawList* draw = ImGui::GetWindowDrawList();

        ImVec2 origin = ImGui::GetCursorScreenPos();
        int displayBars = std::max(64, timelineBars);

        for (const auto& clip : audioClips)
        {
            if (!clip) continue;

            float endBeat =
                (clip->startSeconds + clip->duration) * bpm / 60.0f;

            int requiredBars =
                static_cast<int>(std::ceil(endBeat / 4.0f)) + 8;

            displayBars = std::max(displayBars, requiredBars);
        }

        for (const auto& note : midiNotes)
        {
            float endBeat = note.startBeat + note.lengthBeats;

            int requiredBars =
                static_cast<int>(std::ceil(endBeat / 4.0f)) + 8;

            displayBars = std::max(displayBars, requiredBars);
        }
        float rowHeight = 55.0f;
        float leftWidth = 130.0f;

        float totalWidth =
            leftWidth + displayBars * timelineZoom;

        float totalHeight =
            static_cast<float>(tracks.size()) * rowHeight + 35.0f;

        ImGui::InvisibleButton(
            "TimelineInput",
            ImVec2(totalWidth, totalHeight),
            ImGuiButtonFlags_MouseButtonLeft
        );
        bool timelineHovered = ImGui::IsItemHovered();

        if (timelineHovered && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f)
            timelineZoom = std::clamp(timelineZoom * (1.0f + 0.1f * ImGui::GetIO().MouseWheel), 30.0f, 200.0f);

        static bool draggingPlayhead = false;
        float pixelsPerSecond =
            timelineZoom * bpm / 240.0f;

        ImVec2 mousePos = ImGui::GetIO().MousePos;

        const float timelineScrollX = ImGui::GetScrollX();
        const bool onHeader =
            mousePos.x < origin.x + timelineScrollX + leftWidth &&
            mousePos.y > origin.y + 35.0f;
        if (ImGui::IsItemHovered() && ImGui::GetIO().KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            const int arow = static_cast<int>(std::floor((mousePos.y - origin.y - 35.0f) / rowHeight));
            if (arow >= 0 && arow < static_cast<int>(trackAutomation.size()))
            {
                auto& pts = trackAutomation[arow];
                int best = -1;
                float bestDx = 10.0f;
                for (int k = 0; k < static_cast<int>(pts.size()); ++k)
                {
                    const float px = origin.x + leftWidth + pts[k].time * pixelsPerSecond;
                    const float dx = std::fabs(mousePos.x - px);
                    if (dx < bestDx) { bestDx = dx; best = k; }
                }
                if (best >= 0) pts.erase(pts.begin() + best);
            }
        }
        if (ImGui::IsItemHovered() && !ImGui::GetIO().KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            contextClipIndex = -1;
            for (int ci = static_cast<int>(audioClips.size()) - 1; ci >= 0; --ci)
            {
                auto& c = audioClips[ci];
                float cx = origin.x + leftWidth + c->startSeconds * pixelsPerSecond;
                float cy = origin.y + 35.0f + c->trackIndex * rowHeight + 8.0f;
                float cw = c->duration * pixelsPerSecond;
                if (mousePos.x >= cx && mousePos.x <= cx + cw && mousePos.y >= cy && mousePos.y <= cy + rowHeight - 16.0f) { contextClipIndex = ci; break; }
            }
            if (contextClipIndex >= 0) ImGui::OpenPopup("ClipContextMenu");
        }

        if (ImGui::BeginPopup("ClipContextMenu"))
        {
            bool closePopup = false;

            if (contextClipIndex >= 0 &&
                contextClipIndex < static_cast<int>(audioClips.size()))
            {
                AudioClip* c = audioClips[contextClipIndex].get();
                bool deleted = false;

                if (ImGui::MenuItem("Split at Playhead"))
                {
                    float splitTime = playheadPosition * 240.0f / (timelineZoom * bpm);
                    float local = splitTime - c->startSeconds;
                    if (local > 0.02f && local < c->duration - 0.02f)
                    {
                        auto right = std::make_unique<AudioClip>();
                        right->path = c->path;
                        right->name = c->name + " (2)";
                        right->startSeconds = splitTime;
                        right->duration = c->duration - local;
                        right->trackIndex = c->trackIndex;
                        right->sampleRate = c->sampleRate;
                        right->sourceOffsetSeconds = c->sourceOffsetSeconds + local;
                        right->gain = c->gain;
                        right->fadeOutSeconds = c->fadeOutSeconds;

                        right->pcm = c->pcm;
                        if (right->pcm)
                        {
                            c->duration = local;
                            c->fadeOutSeconds = 0.0f;
                            buildWaveform(*c);
                            buildWaveform(*right);
                            audioClips.push_back(std::move(right));
                        }
                    }
                    closePopup = true;
                }

                if (ImGui::MenuItem("Duplicate Clip"))
                {
                    auto copy = std::make_unique<AudioClip>();
                    copy->path = c->path;
                    copy->name = c->name + " Copy";
                    copy->startSeconds = c->startSeconds + c->duration;
                    copy->duration = c->duration;
                    copy->trackIndex = c->trackIndex;
                    copy->sampleRate = c->sampleRate;
                    copy->sourceOffsetSeconds = c->sourceOffsetSeconds;
                    copy->gain = c->gain;
                    copy->fadeInSeconds = c->fadeInSeconds;
                    copy->fadeOutSeconds = c->fadeOutSeconds;

                    copy->pcm = c->pcm;
                    if (copy->pcm)
                    {
                        buildWaveform(*copy);
                        audioClips.push_back(std::move(copy));
                    }
                    closePopup = true;
                }

                if (ImGui::MenuItem("Delete Clip"))
                {
                    stopAllClips();
                    playing = false;

                    destroyClip(*c);
                    audioClips.erase(audioClips.begin() + contextClipIndex);

                    draggingClipIndex = -1;
                    draggingAudioClip = false;
                    contextClipIndex = -1;

                    selectedClipPtr = nullptr;
                    deleted = true;
                    closePopup = true;
                }

                if (!deleted)
                {
                    if (ImGui::MenuItem("Auto Volume (Normalize)"))
                    {
                        if (c->pcm)
                        {
                            const size_t total = c->pcm->size() / 2;
                            const size_t from = std::min(total, static_cast<size_t>(std::max(0.0f, c->sourceOffsetSeconds) * kEngineRate));
                            const size_t to = std::min(total, from + static_cast<size_t>(std::max(0.0f, c->duration) * kEngineRate));
                            float peak = 0.0f;
                            for (size_t k = from * 2; k < to * 2; ++k)
                                peak = std::max(peak, std::abs((*c->pcm)[k]));
                            if (peak > 0.00001f)
                                c->gain = std::clamp(0.98f / peak, 0.0f, 4.0f);
                        }
                    }

                    float maxFade = std::max(0.01f, c->duration);

                    ImGui::SetNextItemWidth(180.0f);
                    ImGui::SliderFloat("Clip Gain", &c->gain, 0.0f, 4.0f, "%.2fx");

                    ImGui::SeparatorText("Fade");

                    ImGui::SetNextItemWidth(180.0f);
                    ImGui::SliderFloat(
                        "Fade In",
                        &c->fadeInSeconds,
                        0.0f,
                        maxFade,
                        "%.2f sec"
                    );

                    ImGui::SetNextItemWidth(180.0f);
                    ImGui::SliderFloat(
                        "Fade Out",
                        &c->fadeOutSeconds,
                        0.0f,
                        maxFade,
                        "%.2f sec"
                    );

                    ImGui::Separator();
                }
            }

            if (closePopup)
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }

        if (ImGui::IsItemHovered() &&
            !ImGui::GetIO().KeyAlt &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            draggingClipIndex = -1;
            draggingAudioClip = false;
            selectedClipPtr = nullptr;
            clipDragMode = 0;

            for (int i = onHeader ? -1 : static_cast<int>(audioClips.size()) - 1; i >= 0; i--)
            {
                auto& clip = audioClips[i];

                if (clip->trackIndex < 0 ||
                    clip->trackIndex >= static_cast<int>(tracks.size()))
                    continue;

                float clipX =
                    origin.x + leftWidth +
                    clip->startSeconds * pixelsPerSecond;

                float clipY =
                    origin.y + 35.0f +
                    clip->trackIndex * rowHeight + 8.0f;

                float clipWidth =
                    clip->duration * pixelsPerSecond;

                float clipHeight = rowHeight - 16.0f;

                bool insideClip =
                    mousePos.x >= clipX &&
                    mousePos.x <= clipX + clipWidth &&
                    mousePos.y >= clipY &&
                    mousePos.y <= clipY + clipHeight;

                if (insideClip)
                {
                    draggingClipIndex = i;
                    draggingAudioClip = true;
                    selectedClipPtr = clip.get();

                    dragOffsetPixels = mousePos.x - clipX;

                    clipDragMode = 0;
                    if (clipWidth > 24.0f)
                    {
                        if (mousePos.x <= clipX + 6.0f) clipDragMode = 1;
                        else if (mousePos.x >= clipX + clipWidth - 6.0f) clipDragMode = 2;
                    }
                    dragOrigStart = clip->startSeconds;
                    dragOrigDuration = clip->duration;
                    dragOrigOffset = clip->sourceOffsetSeconds;
                    break;
                }
            }
        }
        if (draggingClipIndex < 0 ||
            draggingClipIndex >= static_cast<int>(audioClips.size()))
        {
            draggingClipIndex = -1;
            draggingAudioClip = false;
        }
        if (draggingAudioClip &&
            draggingClipIndex >= 0 &&
            draggingClipIndex < static_cast<int>(audioClips.size()) &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            auto& clip = audioClips[draggingClipIndex];

            if (clipDragMode != 0)
            {
                const float mouseSec = std::max(0.0f, (mousePos.x - origin.x - leftWidth) / pixelsPerSecond);
                const float snappedBeat = snapToGrid(mouseSec * bpm / 60.0f);
                const float snappedSec = snappedBeat * 60.0f / bpm;
                const float pcmSec = clip->pcm
                    ? static_cast<float>(clip->pcm->size() / 2) / static_cast<float>(kEngineRate)
                    : clip->duration;
                const float origEnd = dragOrigStart + dragOrigDuration;

                if (clipDragMode == 2)
                {
                    const float maxDur = std::max(0.02f, pcmSec - clip->sourceOffsetSeconds);
                    clip->duration = std::clamp(snappedSec - clip->startSeconds, 0.02f, maxDur);
                }
                else
                {
                    float newStart = std::clamp(snappedSec, dragOrigStart - dragOrigOffset, origEnd - 0.02f);
                    newStart = std::max(0.0f, newStart);
                    clip->startSeconds = newStart;
                    clip->sourceOffsetSeconds = dragOrigOffset + (newStart - dragOrigStart);
                    clip->duration = origEnd - newStart;
                }

                clip->fadeInSeconds = std::min(clip->fadeInSeconds, clip->duration);
                clip->fadeOutSeconds = std::min(clip->fadeOutSeconds, clip->duration);
            }
            else
            {
                float newStartSeconds = std::max(
                    0.0f,
                    (mousePos.x - origin.x - leftWidth - dragOffsetPixels)
                    / pixelsPerSecond
                );

                float newStartBeat = newStartSeconds * bpm / 60.0f;

                newStartBeat = snapToGrid(newStartBeat);

                clip->startSeconds = newStartBeat * 60.0f / bpm;

                int targetTrack = static_cast<int>(
                    std::floor(
                        (mousePos.y - origin.y - 35.0f) / rowHeight
                    )
                    );

                if (targetTrack >= 0 &&
                    targetTrack < static_cast<int>(tracks.size()))
                {
                    clip->trackIndex = targetTrack;
                }
            }
        }
        if (timelineHovered && ImGui::GetIO().KeyAlt &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !onHeader)
        {
            const int arow = static_cast<int>(std::floor((mousePos.y - origin.y - 35.0f) / rowHeight));
            if (arow >= 0 && arow < static_cast<int>(trackAutomation.size()))
            {
                selectedTrack = arow;
                const float rowTop = origin.y + 35.0f + arow * rowHeight + 8.0f;
                AutomationPoint ap;
                ap.time = std::max(0.0f, (mousePos.x - origin.x - leftWidth) / pixelsPerSecond);
                ap.value = std::clamp((1.0f - (mousePos.y - rowTop) / (rowHeight - 16.0f)) * 2.0f, 0.0f, 2.0f);
                auto& pts = trackAutomation[arow];
                pts.push_back(ap);
                std::sort(pts.begin(), pts.end(), [](const AutomationPoint& a, const AutomationPoint& b) { return a.time < b.time; });
            }
        }

        if (timelineHovered &&
            !ImGui::GetIO().KeyAlt &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !draggingAudioClip)
        {
            if (onHeader)
            {
                const int row = static_cast<int>(std::floor((mousePos.y - origin.y - 35.0f) / rowHeight));
                if (row >= 0 && row < static_cast<int>(tracks.size()))
                    selectedTrack = row;
            }
            else
            {
                draggingPlayhead = true;
            }
        }

        if (draggingPlayhead &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !draggingAudioClip)
        {
            float clickedPosition =
                mousePos.x - origin.x - leftWidth;

            clickedPosition = std::clamp(
                clickedPosition,
                0.0f,
                displayBars * timelineZoom
            );

            float clickedSeconds =
                clickedPosition / pixelsPerSecond;

            float clickedBeat =
                clickedSeconds * bpm / 60.0f;

            if (snapEnabled)
                clickedBeat = snapToGrid(clickedBeat);

            playheadPosition =
                clickedBeat * timelineZoom / 4.0f;

            seekRequested = true;
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            draggingPlayhead = false;
        }

        if (draggingAudioClip &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (clipDragMode != 0 &&
                draggingClipIndex >= 0 &&
                draggingClipIndex < static_cast<int>(audioClips.size()))
            {
                buildWaveform(*audioClips[draggingClipIndex]);
            }

            clipDragMode = 0;
            draggingAudioClip = false;
            draggingClipIndex = -1;
        }

        ImVec2 areaMin = origin;

        ImVec2 areaMax(
            origin.x + totalWidth,
            origin.y + totalHeight
        );

        draw->AddRectFilled(
            areaMin,
            areaMax,
            IM_COL32(30, 30, 34, 255)
        );
        float beatsPerBar = 4.0f;
        float pixelsPerBeat = timelineZoom / beatsPerBar;

        if (snapBeat > 0.0f)
        {
            int totalGridLines = static_cast<int>(
                displayBars * beatsPerBar / snapBeat
                );

            for (int i = 0; i <= totalGridLines; i++)
            {
                float beatPosition = i * snapBeat;

                float x =
                    origin.x + leftWidth +
                    beatPosition * pixelsPerBeat;

                bool isBeatLine =
                    std::abs(beatPosition - std::round(beatPosition)) < 0.001f;

                draw->AddLine(
                    ImVec2(x, origin.y + 35.0f),
                    ImVec2(x, areaMax.y),
                    isBeatLine
                    ? IM_COL32(65, 65, 75, 255)
                    : IM_COL32(48, 48, 55, 255),
                    isBeatLine ? 1.0f : 0.7f
                );
            }
        }

        for (int bar = 0; bar <= displayBars; bar++)
        {
            float x =
                origin.x + leftWidth + bar * timelineZoom;

            draw->AddLine(
                ImVec2(x, origin.y),
                ImVec2(x, areaMax.y),
                IM_COL32(75, 75, 82, 255)
            );

            draw->AddText(
                ImVec2(x + 5, origin.y + 5),
                IM_COL32(220, 220, 220, 255),
                std::to_string(bar + 1).c_str()
            );
        }

        for (int i = 0; i < static_cast<int>(tracks.size()); i++)
        {
            float y =
                origin.y + 35.0f + i * rowHeight;

            draw->AddRectFilled(
                ImVec2(origin.x, y),
                ImVec2(
                    origin.x + totalWidth,
                    y + rowHeight
                ),
                i % 2 == 0
                ? IM_COL32(42, 42, 48, 255)
                : IM_COL32(36, 36, 42, 255)
            );

            draw->AddText(
                ImVec2(origin.x + 10, y + 18),
                IM_COL32(230, 230, 230, 255),
                tracks[i].c_str()
            );

            draw->AddLine(
                ImVec2(origin.x, y + rowHeight),
                ImVec2(
                    origin.x + totalWidth,
                    y + rowHeight
                ),
                IM_COL32(65, 65, 72, 255)
            );
        }

        for (auto& clip : audioClips)
        {
            if (clip->trackIndex < 0 ||
                clip->trackIndex >= static_cast<int>(tracks.size()))
                continue;

            float x =
                origin.x +
                leftWidth +
                clip->startSeconds * pixelsPerSecond;

            float y =
                origin.y +
                35.0f +
                clip->trackIndex * rowHeight +
                8.0f;

            float clipWidth =
                clip->duration * pixelsPerSecond;

            ImVec2 clipMin(x, y);

            ImVec2 clipMax(
                x + clipWidth,
                y + rowHeight - 16.0f
            );

            draw->AddRectFilled(
                clipMin,
                clipMax,
                CubaseTrackColor(clip->trackIndex),
                5.0f
            );

            const bool clipSelected = clip.get() == selectedClipPtr;
            draw->AddRect(
                clipMin,
                clipMax,
                clipSelected ? IM_COL32(255, 230, 120, 255) : IM_COL32(100, 190, 255, 255),
                5.0f,
                0,
                clipSelected ? 2.5f : 1.0f
            );

            if (!clip->waveform.empty() && clipWidth > 4.0f)
            {
                float midY = y + (rowHeight - 16.0f) * 0.58f;
                float usableW = std::max(1.0f, clipWidth - 8.0f);
                for (size_t wi = 0; wi < clip->waveform.size(); ++wi)
                {
                    float wx = x + 4.0f + usableW * static_cast<float>(wi) / static_cast<float>(clip->waveform.size());
                    float amp = clip->waveform[wi] * (rowHeight - 25.0f) * 0.38f;
                    draw->AddLine(ImVec2(wx, midY - amp), ImVec2(wx, midY + amp), IM_COL32(165, 220, 255, 220));
                }
            }
            float clipHeight = rowHeight - 16.0f;
            float fadeInWidth = std::min(
                clipWidth,
                clip->fadeInSeconds * pixelsPerSecond
            );

            float fadeOutWidth = std::min(
                clipWidth,
                clip->fadeOutSeconds * pixelsPerSecond
            );

            float topY = y + 3.0f;
            float bottomY = y + clipHeight - 3.0f;

            if (fadeInWidth > 1.0f)
            {
                draw->AddLine(
                    ImVec2(x, bottomY),
                    ImVec2(x + fadeInWidth, topY),
                    IM_COL32(255, 230, 120, 255),
                    2.0f
                );

                draw->AddLine(
                    ImVec2(x, bottomY),
                    ImVec2(x + fadeInWidth, bottomY),
                    IM_COL32(255, 230, 120, 120),
                    1.0f
                );
            }

            if (fadeOutWidth > 1.0f)
            {
                draw->AddLine(
                    ImVec2(x + clipWidth - fadeOutWidth, topY),
                    ImVec2(x + clipWidth, bottomY),
                    IM_COL32(255, 230, 120, 255),
                    2.0f
                );

                draw->AddLine(
                    ImVec2(x + clipWidth - fadeOutWidth, bottomY),
                    ImVec2(x + clipWidth, bottomY),
                    IM_COL32(255, 230, 120, 120),
                    1.0f
                );
            }
            draw->AddText(
                ImVec2(x + 7.0f, y + 7.0f),
                IM_COL32(255, 255, 255, 255),
                clip->name.c_str()
            );
        }

        if (loopEnabled && loopEndBeat > loopStartBeat)
        {
            const float lx1 = origin.x + leftWidth + loopStartBeat * pixelsPerBeat;
            const float lx2 = origin.x + leftWidth + loopEndBeat * pixelsPerBeat;
            draw->AddRectFilled(ImVec2(lx1, origin.y), ImVec2(lx2, origin.y + 35.0f), IM_COL32(60, 160, 255, 90));
            draw->AddRectFilled(ImVec2(lx1, origin.y + 35.0f), ImVec2(lx2, areaMax.y), IM_COL32(60, 160, 255, 28));
            draw->AddLine(ImVec2(lx1, origin.y), ImVec2(lx1, areaMax.y), IM_COL32(90, 190, 255, 200), 1.5f);
            draw->AddLine(ImVec2(lx2, origin.y), ImVec2(lx2, areaMax.y), IM_COL32(90, 190, 255, 200), 1.5f);
        }

        for (const auto& n : midiNotes)
        {
            if (n.trackIndex < 0 || n.trackIndex >= static_cast<int>(tracks.size()))
                continue;

            float nx1 = origin.x + leftWidth + n.startBeat * pixelsPerBeat;
            float nx2 = origin.x + leftWidth + (n.startBeat + n.lengthBeats) * pixelsPerBeat;
            float rowTop = origin.y + 35.0f + n.trackIndex * rowHeight + 4.0f;
            float ny = rowTop + (1.0f - n.pitch / 127.0f) * (rowHeight - 12.0f);

            draw->AddRectFilled(
                ImVec2(nx1, ny),
                ImVec2(std::max(nx2, nx1 + 2.0f), ny + 3.0f),
                CubaseTrackColor(n.trackIndex, 235)
            );
        }

        if (selectedTrack >= 0 && selectedTrack < static_cast<int>(trackAutomation.size()))
        {
            const auto& points = trackAutomation[selectedTrack];
            for (size_t ai = 1; ai < points.size(); ++ai)
            {
                float x1 = origin.x + leftWidth + points[ai - 1].time * pixelsPerSecond;
                float x2 = origin.x + leftWidth + points[ai].time * pixelsPerSecond;
                float y1 = origin.y + 35.0f + selectedTrack * rowHeight + 8.0f + (1.0f - points[ai - 1].value / 2.0f) * (rowHeight - 16.0f);
                float y2 = origin.y + 35.0f + selectedTrack * rowHeight + 8.0f + (1.0f - points[ai].value / 2.0f) * (rowHeight - 16.0f);
                draw->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(255, 210, 70, 255), 2.0f);
                draw->AddCircleFilled(ImVec2(x1, y1), 4.0f, IM_COL32(255, 220, 90, 255));
            }
            if (!points.empty())
            {
                const auto& a = points.back(); float ax = origin.x + leftWidth + a.time * pixelsPerSecond;
                float ay = origin.y + 35.0f + selectedTrack * rowHeight + 8.0f + (1.0f - a.value / 2.0f) * (rowHeight - 16.0f);
                draw->AddCircleFilled(ImVec2(ax, ay), 4.0f, IM_COL32(255, 220, 90, 255));
            }
        }

        {
            const float hx = origin.x + timelineScrollX;

            for (int i = 0; i < static_cast<int>(tracks.size()); i++)
            {
                const float y = origin.y + 35.0f + i * rowHeight;
                const bool sel = selectedTrack == i;

                draw->AddRectFilled(
                    ImVec2(hx, y), ImVec2(hx + leftWidth, y + rowHeight),
                    sel ? IM_COL32(38, 72, 82, 255) : IM_COL32(34, 34, 40, 255));
                draw->AddRect(
                    ImVec2(hx, y), ImVec2(hx + leftWidth, y + rowHeight),
                    IM_COL32(65, 65, 72, 255));
                draw->AddRectFilled(ImVec2(hx, y), ImVec2(hx + 5.0f, y + rowHeight), CubaseTrackColor(i));
                draw->AddText(ImVec2(hx + 12.0f, y + 8.0f), IM_COL32(235, 235, 235, 255), tracks[i].c_str());

                if (i < static_cast<int>(trackMuted.size()) && trackMuted[i])
                    draw->AddText(ImVec2(hx + leftWidth - 38.0f, y + 8.0f), IM_COL32(255, 90, 90, 255), "M");
                if (i < static_cast<int>(trackSolo.size()) && trackSolo[i])
                    draw->AddText(ImVec2(hx + leftWidth - 22.0f, y + 8.0f), IM_COL32(255, 220, 80, 255), "S");

                const float pkv = i < static_cast<int>(trackPeak.size()) ? std::min(1.0f, trackPeak[i]) : 0.0f;
                draw->AddRectFilled(
                    ImVec2(hx + 10.0f, y + rowHeight - 14.0f),
                    ImVec2(hx + leftWidth - 10.0f, y + rowHeight - 8.0f),
                    IM_COL32(20, 22, 26, 255));
                draw->AddRectFilled(
                    ImVec2(hx + 10.0f, y + rowHeight - 14.0f),
                    ImVec2(hx + 10.0f + (leftWidth - 20.0f) * pkv, y + rowHeight - 8.0f),
                    pkv > 0.9f ? IM_COL32(255, 80, 70, 255) : IM_COL32(80, 220, 120, 255));
            }

            draw->AddRectFilled(
                ImVec2(hx, origin.y), ImVec2(hx + leftWidth, origin.y + 35.0f),
                IM_COL32(30, 30, 34, 255));
            draw->AddText(ImVec2(hx + 10.0f, origin.y + 10.0f), IM_COL32(170, 170, 180, 255), "TRACKS");
        }

        for (const auto& mk : markers)
        {
            const float mx = origin.x + leftWidth + mk.beat * pixelsPerBeat;
            draw->AddLine(ImVec2(mx, origin.y), ImVec2(mx, areaMax.y), IM_COL32(120, 200, 255, 140), 1.0f);
            draw->AddTriangleFilled(ImVec2(mx, origin.y + 28.0f), ImVec2(mx + 8.0f, origin.y + 18.0f),
                ImVec2(mx - 0.0f, origin.y + 18.0f), IM_COL32(120, 200, 255, 255));
            draw->AddText(ImVec2(mx + 10.0f, origin.y + 17.0f), IM_COL32(170, 220, 255, 255), mk.name.c_str());
        }

        float playheadX =
            origin.x + leftWidth + playheadPosition;

        draw->AddLine(
            ImVec2(playheadX, origin.y),
            ImVec2(playheadX, areaMax.y),
            IM_COL32(255, 70, 70, 255),
            2.0f
        );

        if (followPlayhead && playing)
        {
            const float visibleW = ImGui::GetWindowWidth();
            const float sx = ImGui::GetScrollX();
            const float px = playheadPosition + leftWidth;
            if (px > sx + visibleW - 60.0f || px < sx + leftWidth)
                ImGui::SetScrollX(std::max(0.0f, playheadPosition - 40.0f));
        }

        if (selectedClipPtr)
        {
            auto selIt = std::find_if(
                audioClips.begin(), audioClips.end(),
                [&](const std::unique_ptr<AudioClip>& c) { return c.get() == selectedClipPtr; });

            if (selIt == audioClips.end())
            {
                selectedClipPtr = nullptr;
            }
            else if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                !ImGui::GetIO().WantTextInput &&
                !draggingAudioClip)
            {
                const ImGuiIO& cio = ImGui::GetIO();

                if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
                {
                    audioClips.erase(selIt);
                    selectedClipPtr = nullptr;
                    contextClipIndex = -1;
                }
                else if (cio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
                {
                    duplicateClip(selectedClipPtr);
                }
                else if (!cio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
                {
                    splitClipAtPlayhead(selectedClipPtr);
                }
                else if (cio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
                {
                    copyClip();
                }
                else if (cio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
                {
                    pasteClip();
                }
            }
        }

        ImGui::EndChild();
        ImGui::End();

        {
            const std::uint64_t h = projectHash();

            if (!undoInit)
            {
                baseSnapshot = captureSnapshot();
                stableHash = h;
                savedHash = h;
                undoInit = true;
            }
            else if (h != stableHash &&
                !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
                !ImGui::IsAnyItemActive())
            {
                undoStack.push_back(std::move(baseSnapshot));
                if (undoStack.size() > 64)
                    undoStack.erase(undoStack.begin());
                redoStack.clear();
                baseSnapshot = captureSnapshot();
                stableHash = h;
            }

            projectDirty = (h != savedHash);

            if (projectDirty != lastTitleDirty)
            {
                glfwSetWindowTitle(window, projectDirty ? "*EasyMix Studio 1.0" : "EasyMix Studio 1.0");
                lastTitleDirty = projectDirty;
            }
        }

        if (autoSave && projectDirty && !currentProjectPath.empty() &&
            ImGui::GetTime() - lastAutoSave > 90.0 && !ImGui::IsAnyItemActive() &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !recordingState.active.load())
        {
            saveProject();
            lastAutoSave = ImGui::GetTime();
        }
        if (previewOffTime > 0.0 && ImGui::GetTime() > previewOffTime)
        {
            previewEng.ReleaseAll();
            previewOffTime = 0.0;
        }
        if (!audioError.empty()) { toasts.Push(audioError); audioError.clear(); }
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F1, false)) showHelp = !showHelp;
        palette.Draw();
        DrawShortcutsWindow(&showHelp);
        toasts.Draw();

        ImGui::Render();

        uiLock.unlock();

        int width;
        int height;

        glfwGetFramebufferSize(
            window,
            &width,
            &height
        );

        glViewport(0, 0, width, height);

        glClearColor(
            0.10f,
            0.10f,
            0.12f,
            1.0f
        );

        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(
            ImGui::GetDrawData()
        );

        glfwSwapBuffers(window);
    }

    if (rtDeviceOk)
        ma_device_uninit(&rtDevice);
    playing = false;

    trackVst.clear();

    for (auto& clip : audioClips)
    {
        destroyClip(*clip);
    }

    audioClips.clear();

    if (recordingState.active.load())
    {
        recordingState.active.store(false);

        if (micAvailable)
            ma_device_stop(&captureDevice);

        ma_encoder_uninit(&recordingState.encoder);
    }

    if (micAvailable)
    {
        ma_device_uninit(&captureDevice);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}