#pragma once

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

#include "PianoRoll.h"

namespace openutau_bridge
{
    inline std::string QuoteYaml(const std::string& value)
    {
        std::string out = "\"";
        for (unsigned char c : value)
        {
            if (c == '\\') out += "\\\\";
            else if (c == '"') out += "\\\"";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else out += static_cast<char>(c);
        }
        out += '"';
        return out;
    }

    inline bool ExportUstx(const std::string& path,
        const std::vector<MidiNote>& notes,
        int trackIndex,
        float bpm,
        const std::string& trackName,
        const std::vector<std::string>& lyrics)
    {
        std::vector<MidiNote> selected;
        for (const auto& n : notes)
            if (n.trackIndex == trackIndex && n.pitch >= 0 && n.pitch <= 127 && n.lengthBeats > 0.0f)
                selected.push_back(n);
        if (selected.empty()) return false;

        std::stable_sort(selected.begin(), selected.end(), [](const MidiNote& a, const MidiNote& b)
        {
            return a.startBeat < b.startBeat;
        });

        int maxTick = 1;
        for (const auto& n : selected)
        {
            const int p = std::max(0, static_cast<int>(std::llround(n.startBeat * 480.0)));
            const int d = std::max(1, static_cast<int>(std::llround(n.lengthBeats * 480.0)));
            maxTick = std::max(maxTick, p + d);
        }

        std::ofstream out(path, std::ios::binary);
        if (!out) return false;
        out << "\xEF\xBB\xBF";
        out << "name: EasyMix Vocal Project\n";
        out << "comment: \"Exported from EasyMix Studio\"\n";
        out << "output_dir: Vocal\ncache_dir: UCache\n";
        out << "ustx_version: \"0.6\"\nresolution: 480\n";
        out << "bpm: " << std::clamp(bpm, 20.0f, 400.0f) << "\nbeat_per_bar: 4\nbeat_unit: 4\n";
        out << "expressions:\n";
        out << "  dyn: {name: \"dynamics (curve)\", abbr: dyn, type: Curve, min: -240, max: 120, default_value: 0, is_flag: false, flag: \"\"}\n";
        out << "  pitd: {name: \"pitch deviation (curve)\", abbr: pitd, type: Curve, min: -1200, max: 1200, default_value: 0, is_flag: false, flag: \"\"}\n";
        out << "  clr: {name: \"voice color\", abbr: clr, type: Options, min: 0, max: -1, default_value: 0, is_flag: false, options: []}\n";
        out << "  eng: {name: \"resampler engine\", abbr: eng, type: Options, min: 0, max: 1, default_value: 0, is_flag: false, options: [\"\", worldline]}\n";
        out << "  vel: {name: velocity, abbr: vel, type: Numerical, min: 0, max: 200, default_value: 100, is_flag: false, flag: \"\"}\n";
        out << "  vol: {name: volume, abbr: vol, type: Numerical, min: 0, max: 200, default_value: 100, is_flag: false, flag: \"\"}\n";
        out << "  gen: {name: gender, abbr: gen, type: Numerical, min: -100, max: 100, default_value: 0, is_flag: true, flag: g}\n";
        out << "exp_selectors: [dyn, pitd, clr, eng, vel]\nexp_primary: 0\nexp_secondary: 1\nkey: 0\n";
        out << "time_signatures:\n- bar_position: 0\n  beat_per_bar: 4\n  beat_unit: 4\n";
        out << "tempos:\n- position: 0\n  bpm: " << std::clamp(bpm, 20.0f, 400.0f) << "\n";
        out << "tracks:\n- singer: \"\"\n  phonemizer: OpenUtau.Core.DefaultPhonemizer\n  renderer_settings: {}\n";
        out << "  track_name: " << QuoteYaml(trackName.empty() ? "Vocal" : trackName) << "\n";
        out << "  track_color: Blue\n  mute: false\n  solo: false\n  volume: 0\n  pan: 0\n  voice_color_names:\n  - \"\"\n";
        out << "voice_parts:\n- name: \"Vocal Part\"\n  comment: \"\"\n  track_no: 0\n  position: 0\n  duration: " << maxTick << "\n  notes:\n";

        for (size_t i = 0; i < selected.size(); ++i)
        {
            const auto& n = selected[i];
            const int pos = std::max(0, static_cast<int>(std::llround(n.startBeat * 480.0)));
            const int dur = std::max(1, static_cast<int>(std::llround(n.lengthBeats * 480.0)));
            const std::string lyric = lyrics.empty() ? "あ" : lyrics[std::min(i, lyrics.size() - 1)];
            out << "  - position: " << pos << "\n";
            out << "    duration: " << dur << "\n";
            out << "    tone: " << std::clamp(n.pitch, 0, 127) << "\n";
            out << "    tuning: 0\n";
            out << "    lyric: " << QuoteYaml(lyric) << "\n";
            out << "    pitch:\n      data:\n      - {x: -40, y: 0, shape: io}\n      - {x: 40, y: 0, shape: io}\n      snap_first: true\n";
            out << "    vibrato: {length: 0, period: 180, depth: 25, in: 10, out: 10, shift: 0, drift: 0, vol_link: 0}\n";
            out << "    phoneme_expressions: []\n    phoneme_overrides: []\n";
        }
        out << "wave_parts: []\n";
        return out.good();
    }

    inline bool Launch(const std::string& projectPath, const std::string& executablePath = {})
    {
        if (projectPath.empty()) return false;
        HINSTANCE result = nullptr;
        if (!executablePath.empty())
        {
            const std::string args = "\"" + projectPath + "\"";
            result = ShellExecuteA(nullptr, "open", executablePath.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
        }
        else
        {
            result = ShellExecuteA(nullptr, "open", projectPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return reinterpret_cast<INT_PTR>(result) > 32;
    }
}
