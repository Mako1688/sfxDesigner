#include "audio_reader.hpp"
#include "json_exporter.hpp"
#include "sfx_def.hpp"
#include "sfx_importer.hpp"
#include "synth_engine.hpp"
#include "wav_writer.hpp"

#include "imgui.h"
#include "backends/imgui_impl_glut.h"
#include "backends/imgui_impl_opengl2.h"

#include <GL/glut.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace {
constexpr int kSampleRate = 44100;
constexpr int kPlotDownsample = 4;
constexpr float kUiFontScale = 1.35f;
constexpr float kUiSizeScale = 1.15f;

struct AppState {
    SfxDef current = make_default_sfx();
    std::string current_name = "NEW_SOUND";
    std::string status = "Ready";
    float render_duration = 0.8f;
    std::vector<float> preview_samples;
    RenderDebugData debug_data;
    float ui_font_scale = kUiFontScale;
    std::string import_path = "";
    float import_source_match = 0.85f;
    float import_fidelity_boost = 0.7f;
    int preview_mode = 0;
    float preview_source_blend = 0.5f;
    float preview_gain = 1.0f;
    bool auto_gain = true;
    bool has_imported_source = false;
    std::vector<float> imported_source_44k;
    float similarity_spectral = 0.0f;
    float similarity_envelope = 0.0f;
    float similarity_score = 0.0f;

    std::vector<SfxDef> undo_stack;
    std::vector<SfxDef> redo_stack;
    bool has_drag_snapshot = false;
    SfxDef drag_snapshot = make_default_sfx();

    int selected_category = 0;
    int selected_preset = 0;
    std::vector<std::string> category_names = {"Weapons", "UI", "Combat", "Movement", "Pickups"};
    std::vector<std::vector<NamedPreset>> presets;
};

AppState g_app;

void update_similarity_metrics();
std::string shell_quote(const std::string& value);

void push_undo_history() {
    g_app.undo_stack.push_back(g_app.current);
    if (g_app.undo_stack.size() > 256) {
        g_app.undo_stack.erase(g_app.undo_stack.begin());
    }
    g_app.redo_stack.clear();
}

void push_history_state(const SfxDef& state) {
    g_app.undo_stack.push_back(state);
    if (g_app.undo_stack.size() > 256) {
        g_app.undo_stack.erase(g_app.undo_stack.begin());
    }
    g_app.redo_stack.clear();
}

void rerender_preview() {
    g_app.preview_samples = render_samples(g_app.current, kSampleRate, g_app.render_duration, &g_app.debug_data);
    update_similarity_metrics();
}

std::vector<float> resample_linear(const std::vector<float>& input, int src_rate, int dst_rate) {
    if (input.empty() || src_rate <= 0 || dst_rate <= 0) {
        return {};
    }
    if (src_rate == dst_rate) {
        return input;
    }

    const double ratio = static_cast<double>(dst_rate) / static_cast<double>(src_rate);
    const size_t out_count = std::max<size_t>(1, static_cast<size_t>(std::llround(static_cast<double>(input.size()) * ratio)));
    std::vector<float> out(out_count, 0.0f);

    for (size_t i = 0; i < out_count; ++i) {
        const double src_pos = static_cast<double>(i) / ratio;
        const size_t i0 = static_cast<size_t>(std::floor(src_pos));
        const size_t i1 = std::min(i0 + 1, input.size() - 1);
        const float t = static_cast<float>(src_pos - static_cast<double>(i0));
        const float a = input[std::min(i0, input.size() - 1)];
        const float b = input[i1];
        out[i] = a + (b - a) * t;
    }
    return out;
}

std::vector<float> make_source_preview_samples() {
    const size_t target_count = static_cast<size_t>(std::max(1, static_cast<int>(g_app.render_duration * static_cast<float>(kSampleRate))));
    std::vector<float> out(target_count, 0.0f);
    if (!g_app.has_imported_source || g_app.imported_source_44k.empty()) {
        return out;
    }
    const size_t copy_count = std::min(target_count, g_app.imported_source_44k.size());
    std::copy_n(g_app.imported_source_44k.begin(), copy_count, out.begin());
    return out;
}

std::vector<float> make_playback_samples() {
    const std::vector<float> synth = g_app.preview_samples;
    if (g_app.preview_mode == 0 || !g_app.has_imported_source) {
        return synth;
    }

    std::vector<float> source = make_source_preview_samples();
    if (g_app.preview_mode == 1) {
        return source;
    }

    const float source_w = std::clamp(g_app.preview_source_blend, 0.0f, 1.0f);
    const float synth_w = 1.0f - source_w;
    const size_t count = std::max(synth.size(), source.size());
    std::vector<float> mixed(count, 0.0f);

    for (size_t i = 0; i < count; ++i) {
        const float sv = i < synth.size() ? synth[i] : 0.0f;
        const float rv = i < source.size() ? source[i] : 0.0f;
        mixed[i] = std::clamp(sv * synth_w + rv * source_w, -1.0f, 1.0f);
    }
    return mixed;
}

float peak_abs_signal(const std::vector<float>& x) {
    float peak = 0.0f;
    for (float v : x) {
        peak = std::max(peak, std::fabs(v));
    }
    return peak;
}

void apply_gain_inplace(std::vector<float>& x, float gain) {
    for (float& v : x) {
        v = std::clamp(v * gain, -1.0f, 1.0f);
    }
}

bool play_wav_with_available_player(const std::string& wav_path, std::string& used_player) {
    struct PlayerCmd {
        const char* name;
        const char* cmd;
    };

    const std::vector<PlayerCmd> players = {
        {"aplay", "aplay -q "},
        {"ffplay", "ffplay -nodisp -autoexit -loglevel quiet "},
        {"cvlc", "cvlc --play-and-exit --quiet "}
    };

    for (const auto& player : players) {
        std::ostringstream check;
        check << "command -v " << player.name << " >/dev/null 2>&1";
        if (std::system(check.str().c_str()) != 0) {
            continue;
        }

        std::ostringstream run;
        run << player.cmd << shell_quote(wav_path) << " >/dev/null 2>&1";
        if (std::system(run.str().c_str()) == 0) {
            used_player = player.name;
            return true;
        }
    }

    used_player.clear();
    return false;
}

std::vector<float> compute_envelope(const std::vector<float>& x) {
    std::vector<float> env(x.size(), 0.0f);
    if (x.empty()) {
        return env;
    }
    float follower = 0.0f;
    for (size_t i = 0; i < x.size(); ++i) {
        const float mag = std::fabs(x[i]);
        const float c = (mag > follower) ? 0.25f : 0.03f;
        follower = c * mag + (1.0f - c) * follower;
        env[i] = follower;
    }
    return env;
}

std::vector<float> stft_band_frame(const std::vector<float>& x, size_t start, size_t end, int sample_rate) {
    const int bands = 10;
    std::vector<float> out(static_cast<size_t>(bands), 0.0f);
    if (end <= start + 8 || sample_rate <= 0) {
        return out;
    }
    const float min_f = 80.0f;
    const float max_f = std::min(12000.0f, static_cast<float>(sample_rate) * 0.45f);
    const size_t n = end - start;

    for (int b = 0; b < bands; ++b) {
        const float t = static_cast<float>(b) / static_cast<float>(bands - 1);
        const float f = min_f * std::pow(max_f / min_f, t);
        float re = 0.0f;
        float im = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            const float w = 0.5f - 0.5f * std::cos(2.0f * 3.14159265358979323846f * static_cast<float>(i) / static_cast<float>(n - 1));
            const float ph = 2.0f * 3.14159265358979323846f * f * static_cast<float>(i) / static_cast<float>(sample_rate);
            const float s = x[start + i] * w;
            re += s * std::cos(ph);
            im -= s * std::sin(ph);
        }
        out[static_cast<size_t>(b)] = std::sqrt(re * re + im * im) / std::max(1.0f, static_cast<float>(n));
    }

    float sum = 0.0f;
    for (float v : out) {
        sum += v;
    }
    if (sum > 1e-8f) {
        for (float& v : out) {
            v /= sum;
        }
    }
    return out;
}

std::tuple<float, float, float> compute_similarity_metrics(const std::vector<float>& source, const std::vector<float>& fitted, int sample_rate) {
    if (source.empty() || fitted.empty()) {
        return {0.0f, 0.0f, 0.0f};
    }

    const size_t n = std::min(source.size(), fitted.size());
    const std::vector<float> src_env = compute_envelope(source);
    const std::vector<float> fit_env = compute_envelope(fitted);

    float env_err = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        env_err += std::fabs(src_env[i] - fit_env[i]);
    }
    env_err /= static_cast<float>(n);

    const size_t window = 1024;
    const size_t hop = 512;
    float spec_err = 0.0f;
    int frames = 0;
    for (size_t start = 0; start + window <= n; start += hop) {
        const auto a = stft_band_frame(source, start, start + window, sample_rate);
        const auto b = stft_band_frame(fitted, start, start + window, sample_rate);
        for (size_t i = 0; i < a.size(); ++i) {
            spec_err += std::fabs(a[i] - b[i]);
        }
        ++frames;
    }
    if (frames > 0) {
        spec_err /= static_cast<float>(frames * 10);
    }

    const float score = std::clamp(100.0f * (1.0f - (spec_err * 2.4f + env_err * 1.6f)), 0.0f, 100.0f);
    return {spec_err, env_err, score};
}

void update_similarity_metrics() {
    if (!g_app.has_imported_source || g_app.imported_source_44k.empty() || g_app.preview_samples.empty()) {
        g_app.similarity_spectral = 0.0f;
        g_app.similarity_envelope = 0.0f;
        g_app.similarity_score = 0.0f;
        return;
    }
    const std::vector<float> source = make_source_preview_samples();
    const auto m = compute_similarity_metrics(source, g_app.preview_samples, kSampleRate);
    g_app.similarity_spectral = std::get<0>(m);
    g_app.similarity_envelope = std::get<1>(m);
    g_app.similarity_score = std::get<2>(m);
}

std::vector<float> downsample_for_plot(const std::vector<float>& source) {
    if (source.empty()) {
        return {};
    }
    std::vector<float> out;
    out.reserve(source.size() / kPlotDownsample + 1);
    for (size_t i = 0; i < source.size(); i += kPlotDownsample) {
        out.push_back(source[i]);
    }
    return out;
}

void randomize_current() {
    const auto& ranges = sfx_param_ranges();
    push_undo_history();
    g_app.current.wave_type = static_cast<WaveType>(std::rand() % 4);
    auto randf = [](const ParameterRange& range) {
        const float t = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        return range.min + t * (range.max - range.min);
    };
    g_app.current.base_freq = randf(ranges.base_freq);
    g_app.current.freq_limit = randf(ranges.freq_limit);
    g_app.current.freq_slide = randf(ranges.freq_slide);
    g_app.current.freq_delta_slide = randf(ranges.freq_delta_slide);
    g_app.current.duty_cycle = randf(ranges.duty_cycle);
    g_app.current.duty_sweep = randf(ranges.duty_sweep);
    g_app.current.vibrato_depth = randf(ranges.vibrato_depth);
    g_app.current.vibrato_speed = randf(ranges.vibrato_speed);
    g_app.current.attack_time = randf(ranges.attack_time);
    g_app.current.sustain_time = randf(ranges.sustain_time);
    g_app.current.decay_time = randf(ranges.decay_time);
    g_app.current.sustain_punch = randf(ranges.sustain_punch);
    g_app.current.lp_filter_cutoff = randf(ranges.lp_filter_cutoff);
    g_app.current.lp_filter_resonance = randf(ranges.lp_filter_resonance);
    g_app.current.hp_filter_cutoff = randf(ranges.hp_filter_cutoff);
    g_app.current.phaser_offset = randf(ranges.phaser_offset);
    g_app.current.tonal_mix = randf(ranges.tonal_mix);
    g_app.current.noise_mix = randf(ranges.noise_mix);
    g_app.current.noise_attack = randf(ranges.noise_attack);
    g_app.current.noise_decay = randf(ranges.noise_decay);
    g_app.current.noise_hp = randf(ranges.noise_hp);
    g_app.status = "Randomized";
    rerender_preview();
}

void do_undo() {
    if (g_app.undo_stack.empty()) {
        g_app.status = "Undo stack empty";
        return;
    }
    g_app.redo_stack.push_back(g_app.current);
    g_app.current = g_app.undo_stack.back();
    g_app.undo_stack.pop_back();
    g_app.status = "Undo";
    rerender_preview();
}

void do_redo() {
    if (g_app.redo_stack.empty()) {
        g_app.status = "Redo stack empty";
        return;
    }
    g_app.undo_stack.push_back(g_app.current);
    g_app.current = g_app.redo_stack.back();
    g_app.redo_stack.pop_back();
    g_app.status = "Redo";
    rerender_preview();
}

void play_preview() {
    rerender_preview();
    std::filesystem::create_directories("exports");
    const std::string temp_wav = "exports/.preview.wav";
    std::vector<float> playback = make_playback_samples();
    const float raw_peak = peak_abs_signal(playback);
    if (raw_peak < 1e-5f) {
        g_app.status = "Preview is silent (very low fitted/source signal)";
        return;
    }

    float gain = std::max(0.0f, g_app.preview_gain);
    if (g_app.auto_gain) {
        const float target_peak = 0.85f;
        const float normalize = target_peak / std::max(1e-6f, raw_peak);
        gain *= std::min(256.0f, normalize);

        if (raw_peak < 0.02f) {
            gain *= 2.0f;
        }
    }
    apply_gain_inplace(playback, gain);

    const float post_peak = peak_abs_signal(playback);
    if (post_peak < 0.01f) {
        g_app.status = "Preview too quiet after gain (increase Preview Gain)";
        return;
    }

    if (!write_wav_file(temp_wav, playback, kSampleRate)) {
        g_app.status = "Failed to write preview WAV";
        return;
    }

    std::string used_player;
    const bool played = play_wav_with_available_player(temp_wav, used_player);
    g_app.status = played ? ("Played preview (" + used_player + ")")
                          : "Preview WAV generated (player failed/unavailable)";
}

void save_exports() {
    rerender_preview();
    std::filesystem::create_directories("exports");
    const std::string safe_name = g_app.current_name.empty() ? "sound" : g_app.current_name;
    const std::string wav_path = "exports/" + safe_name + ".wav";
    const std::string json_path = "exports/" + safe_name + ".json";

    const bool wav_ok = write_wav_file(wav_path, g_app.preview_samples, kSampleRate);
    const bool json_ok = write_sfx_json(json_path, safe_name, g_app.current);

    if (wav_ok && json_ok) {
        g_app.status = "Saved JSON + WAV";
    } else {
        g_app.status = "Save failed";
    }
}

std::string shell_quote(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    out.push_back('\'');
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

bool command_exists(const char* cmd) {
    std::ostringstream oss;
    oss << "command -v " << cmd << " >/dev/null 2>&1";
    return std::system(oss.str().c_str()) == 0;
}

std::filesystem::path resolve_import_path(const std::string& raw_path) {
    std::filesystem::path p(raw_path);
    if (std::filesystem::exists(p)) {
        return p;
    }

    if (p.is_relative()) {
        const std::filesystem::path in_imports = std::filesystem::path("imports") / p;
        if (std::filesystem::exists(in_imports)) {
            return in_imports;
        }
    }

    const std::string typo_prefix = "imprts/";
    if (raw_path.rfind(typo_prefix, 0) == 0) {
        const std::filesystem::path fixed = std::string("imports/") + raw_path.substr(typo_prefix.size());
        if (std::filesystem::exists(fixed)) {
            return fixed;
        }
    }

    return p;
}

void browse_import_file() {
    if (!command_exists("zenity")) {
        g_app.status = "Browse unavailable: install zenity or paste path manually";
        return;
    }

    const char* picker_cmd =
        "zenity --file-selection --title='Select Audio File' "
        "--file-filter='Audio files | *.wav *.WAV *.mp3 *.MP3' "
        "--file-filter='All files | *'";

    FILE* pipe = popen(picker_cmd, "r");
    if (!pipe) {
        g_app.status = "Failed to open file picker";
        return;
    }

    char buffer[1024] = {};
    std::string selected;
    if (fgets(buffer, static_cast<int>(sizeof(buffer)), pipe) != nullptr) {
        selected = buffer;
        while (!selected.empty() && (selected.back() == '\n' || selected.back() == '\r')) {
            selected.pop_back();
        }
    }
    const int rc = pclose(pipe);

    if (rc == 0 && !selected.empty()) {
        g_app.import_path = selected;
        g_app.status = "Selected import file";
    } else {
        g_app.status = "Import selection cancelled";
    }
}

void import_audio_to_sfx() {
    if (g_app.import_path.empty()) {
        g_app.status = "Import path is empty";
        return;
    }

    std::filesystem::path source_path = resolve_import_path(g_app.import_path);
    if (!std::filesystem::exists(source_path)) {
        g_app.status = "Import failed: file not found (try imports/<file>)";
        return;
    }

    std::string ext = source_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    std::filesystem::path wav_path = source_path;
    std::filesystem::path temp_wav;
    if (ext == ".mp3") {
        if (!command_exists("ffmpeg")) {
            g_app.status = "Import failed: MP3 needs ffmpeg (sudo apt install ffmpeg)";
            return;
        }

        std::filesystem::create_directories("exports");
        temp_wav = std::filesystem::path("exports") / ".import_temp.wav";
        std::ostringstream cmd;
        cmd << "ffmpeg -y -v error -i " << shell_quote(source_path.string())
            << " -ac 1 -ar 44100 " << shell_quote(temp_wav.string());
        const int rc = std::system(cmd.str().c_str());
        if (rc != 0 || !std::filesystem::exists(temp_wav)) {
            g_app.status = "Import failed: ffmpeg conversion error";
            return;
        }
        wav_path = temp_wav;
    } else if (ext != ".wav") {
        g_app.status = "Import failed: only WAV/MP3 supported";
        return;
    }

    WavData wav;
    std::string error;
    if (!read_audio_file_best_effort(wav_path.string(), wav, error)) {
        g_app.status = "Import failed: " + error;
        if (!temp_wav.empty()) {
            std::error_code remove_ec;
            std::filesystem::remove(temp_wav, remove_ec);
        }
        return;
    }
    if (wav.samples.empty()) {
        g_app.status = "Import failed: no sample data";
        if (!temp_wav.empty()) {
            std::error_code remove_ec;
            std::filesystem::remove(temp_wav, remove_ec);
        }
        return;
    }

    const float imported_duration = static_cast<float>(wav.samples.size()) / static_cast<float>(std::max(1, wav.sample_rate));
    g_app.render_duration = std::clamp(imported_duration, 0.1f, 10.0f);
    g_app.imported_source_44k = resample_linear(wav.samples, wav.sample_rate, kSampleRate);
    g_app.has_imported_source = !g_app.imported_source_44k.empty();

    push_undo_history();
    g_app.current = fit_sfx_from_samples(wav.samples, wav.sample_rate, g_app.import_source_match, g_app.import_fidelity_boost);

    std::filesystem::path p(source_path);
    if (p.has_stem()) {
        g_app.current_name = p.stem().string();
    }
    g_app.status = "Imported audio -> fitted SfxDef";
    rerender_preview();

    if (!temp_wav.empty()) {
        std::error_code remove_ec;
        std::filesystem::remove(temp_wav, remove_ec);
    }
}

void load_selected_preset() {
    const auto& category = g_app.presets[static_cast<size_t>(g_app.selected_category)];
    if (category.empty()) {
        return;
    }
    g_app.selected_preset = std::clamp(g_app.selected_preset, 0, static_cast<int>(category.size()) - 1);
    push_undo_history();
    g_app.current = category[static_cast<size_t>(g_app.selected_preset)].value;
    g_app.current_name = category[static_cast<size_t>(g_app.selected_preset)].name;
    g_app.status = "Preset loaded";
    rerender_preview();
}

void handle_keyboard(unsigned char key, int, int) {
    switch (key) {
        case ' ':
            play_preview();
            break;
        case 's':
        case 'S':
            save_exports();
            break;
        case 'r':
        case 'R':
            randomize_current();
            break;
        case 'z':
        case 'Z':
            do_undo();
            break;
        case 'y':
        case 'Y':
            do_redo();
            break;
        default:
            break;
    }
    glutPostRedisplay();
}

bool slider_with_history(const char* label, float* value, float min_v, float max_v) {
    const SfxDef before_state = g_app.current;
    const bool changed = ImGui::SliderFloat(label, value, min_v, max_v);
    if (ImGui::IsItemActivated()) {
        g_app.drag_snapshot = before_state;
        g_app.has_drag_snapshot = true;
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && g_app.has_drag_snapshot) {
        push_history_state(g_app.drag_snapshot);
        g_app.has_drag_snapshot = false;
    }
    return changed;
}

bool combo_wave_with_history() {
    int wave = static_cast<int>(g_app.current.wave_type);
    const int before = wave;
    const char* labels[] = {"Square", "Sawtooth", "Sine", "Noise"};
    const bool changed = ImGui::Combo("Wave Type", &wave, labels, 4);
    if (changed && wave != before) {
        push_undo_history();
        g_app.current.wave_type = static_cast<WaveType>(wave);
    }
    return changed;
}

void draw_hotkeys() {
    ImGui::Text("Hotkeys: SPACE=Play  S=Save  R=Randomize  Z=Undo  Y=Redo");
}

void draw_preset_ui() {
    ImGui::SeparatorText("Presets");

    int category = g_app.selected_category;
    std::vector<const char*> category_labels;
    category_labels.reserve(g_app.category_names.size());
    for (const auto& name : g_app.category_names) {
        category_labels.push_back(name.c_str());
    }
    if (ImGui::Combo("Category", &category, category_labels.data(), static_cast<int>(category_labels.size()))) {
        g_app.selected_category = category;
        g_app.selected_preset = 0;
    }

    auto& presets = g_app.presets[static_cast<size_t>(g_app.selected_category)];
    std::vector<const char*> preset_labels;
    preset_labels.reserve(presets.size());
    for (const auto& preset : presets) {
        preset_labels.push_back(preset.name.c_str());
    }
    if (!preset_labels.empty()) {
        ImGui::Combo("Preset", &g_app.selected_preset, preset_labels.data(), static_cast<int>(preset_labels.size()));
    }

    if (ImGui::Button("Load Preset")) {
        load_selected_preset();
    }
}

void draw_parameter_groups() {
    const auto& ranges = sfx_param_ranges();
    bool changed = false;

    ImGui::SeparatorText("Waveform");
    changed |= combo_wave_with_history();
    changed |= slider_with_history("Base Freq", &g_app.current.base_freq, ranges.base_freq.min, ranges.base_freq.max);
    changed |= slider_with_history("Freq Limit", &g_app.current.freq_limit, ranges.freq_limit.min, ranges.freq_limit.max);
    changed |= slider_with_history("Duty Cycle", &g_app.current.duty_cycle, ranges.duty_cycle.min, ranges.duty_cycle.max);
    changed |= slider_with_history("Duty Sweep", &g_app.current.duty_sweep, ranges.duty_sweep.min, ranges.duty_sweep.max);

    ImGui::SeparatorText("Envelope");
    changed |= slider_with_history("Attack", &g_app.current.attack_time, ranges.attack_time.min, ranges.attack_time.max);
    changed |= slider_with_history("Sustain", &g_app.current.sustain_time, ranges.sustain_time.min, ranges.sustain_time.max);
    changed |= slider_with_history("Decay", &g_app.current.decay_time, ranges.decay_time.min, ranges.decay_time.max);
    changed |= slider_with_history("Sustain Punch", &g_app.current.sustain_punch, ranges.sustain_punch.min, ranges.sustain_punch.max);

    ImGui::SeparatorText("Effects");
    changed |= slider_with_history("LP Cutoff", &g_app.current.lp_filter_cutoff, ranges.lp_filter_cutoff.min, ranges.lp_filter_cutoff.max);
    changed |= slider_with_history("LP Resonance", &g_app.current.lp_filter_resonance, ranges.lp_filter_resonance.min, ranges.lp_filter_resonance.max);
    changed |= slider_with_history("HP Cutoff", &g_app.current.hp_filter_cutoff, ranges.hp_filter_cutoff.min, ranges.hp_filter_cutoff.max);
    changed |= slider_with_history("Phaser Offset", &g_app.current.phaser_offset, ranges.phaser_offset.min, ranges.phaser_offset.max);

    ImGui::SeparatorText("Modulation");
    changed |= slider_with_history("Freq Slide", &g_app.current.freq_slide, ranges.freq_slide.min, ranges.freq_slide.max);
    changed |= slider_with_history("Freq Delta Slide", &g_app.current.freq_delta_slide, ranges.freq_delta_slide.min, ranges.freq_delta_slide.max);
    changed |= slider_with_history("Vibrato Depth", &g_app.current.vibrato_depth, ranges.vibrato_depth.min, ranges.vibrato_depth.max);
    changed |= slider_with_history("Vibrato Speed", &g_app.current.vibrato_speed, ranges.vibrato_speed.min, ranges.vibrato_speed.max);

    ImGui::SeparatorText("Layers");
    changed |= slider_with_history("Tonal Mix", &g_app.current.tonal_mix, ranges.tonal_mix.min, ranges.tonal_mix.max);
    changed |= slider_with_history("Noise Mix", &g_app.current.noise_mix, ranges.noise_mix.min, ranges.noise_mix.max);
    changed |= slider_with_history("Noise Attack", &g_app.current.noise_attack, ranges.noise_attack.min, ranges.noise_attack.max);
    changed |= slider_with_history("Noise Decay", &g_app.current.noise_decay, ranges.noise_decay.min, ranges.noise_decay.max);
    changed |= slider_with_history("Noise HP", &g_app.current.noise_hp, ranges.noise_hp.min, ranges.noise_hp.max);

    if (changed) {
        rerender_preview();
    }
}

void draw_visualization() {
    ImGui::SeparatorText("Waveform Visualization");

    std::vector<float> env_plot = downsample_for_plot(g_app.debug_data.envelope);
    std::vector<float> freq_plot = downsample_for_plot(g_app.debug_data.frequency);
    std::vector<float> wave_plot = downsample_for_plot(g_app.debug_data.waveform);

    if (!env_plot.empty()) {
        ImGui::PlotLines("Envelope", env_plot.data(), static_cast<int>(env_plot.size()), 0, nullptr, 0.0f, 1.5f, ImVec2(0, 90));
    }
    if (!freq_plot.empty()) {
        float max_freq = *std::max_element(freq_plot.begin(), freq_plot.end());
        ImGui::PlotLines("Frequency", freq_plot.data(), static_cast<int>(freq_plot.size()), 0, nullptr, 0.0f, std::max(50.0f, max_freq), ImVec2(0, 90));
    }
    if (!wave_plot.empty()) {
        ImGui::PlotLines("Output", wave_plot.data(), static_cast<int>(wave_plot.size()), 0, nullptr, -1.0f, 1.0f, ImVec2(0, 90));
    }

    if (g_app.has_imported_source) {
        ImGui::Text("Similarity Score: %.1f / 100", g_app.similarity_score);
        ImGui::Text("Spectral Error: %.4f", g_app.similarity_spectral);
        ImGui::Text("Envelope Error: %.4f", g_app.similarity_envelope);
    }
}

void display_callback() {
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGLUT_NewFrame();
    ImGui::NewFrame();

    const float pad = 8.0f;
    const float window_w = static_cast<float>(glutGet(GLUT_WINDOW_WIDTH));
    const float window_h = static_cast<float>(glutGet(GLUT_WINDOW_HEIGHT));
    ImGui::SetNextWindowPos(ImVec2(pad, pad), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(std::max(100.0f, window_w - pad * 2.0f), std::max(100.0f, window_h - pad * 2.0f)),
        ImGuiCond_Always
    );
    ImGui::Begin(
        "Sound Designer Tool",
        nullptr,
        ImGuiWindowFlags_NoCollapse
    );

    draw_hotkeys();

    char name_buf[128];
    std::snprintf(name_buf, sizeof(name_buf), "%s", g_app.current_name.c_str());
    if (ImGui::InputText("Sound Name", name_buf, sizeof(name_buf))) {
        g_app.current_name = name_buf;
    }

    if (ImGui::SliderFloat("WAV Duration (sec)", &g_app.render_duration, 0.1f, 10.0f)) {
        rerender_preview();
    }
    if (ImGui::SliderFloat("UI Scale", &g_app.ui_font_scale, 1.0f, 2.5f, "%.2fx")) {
        ImGui::GetIO().FontGlobalScale = g_app.ui_font_scale;
    }

    const char* preview_modes[] = {"Fitted", "Source", "Blend"};
    ImGui::Combo("Preview Mode", &g_app.preview_mode, preview_modes, 3);
    if (g_app.preview_mode == 2) {
        ImGui::SliderFloat("Preview Source Blend", &g_app.preview_source_blend, 0.0f, 1.0f, "%.2f");
    }
    ImGui::Checkbox("Auto Gain", &g_app.auto_gain);
    ImGui::SliderFloat("Preview Gain", &g_app.preview_gain, 0.1f, 8.0f, "%.2fx");

    char import_buf[512];
    std::snprintf(import_buf, sizeof(import_buf), "%s", g_app.import_path.c_str());
    if (ImGui::InputText("Import WAV Path", import_buf, sizeof(import_buf))) {
        g_app.import_path = import_buf;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        browse_import_file();
    }
    ImGui::SliderFloat("Import Source Match", &g_app.import_source_match, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Import Fidelity Boost", &g_app.import_fidelity_boost, 0.0f, 1.0f, "%.2f");
    if (ImGui::Button("Import WAV/MP3 -> Fit SfxDef")) {
        import_audio_to_sfx();
    }

    if (ImGui::Button("Play (SPACE)")) {
        play_preview();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save JSON+WAV (S)")) {
        save_exports();
    }
    ImGui::SameLine();
    if (ImGui::Button("Randomize (R)")) {
        randomize_current();
    }
    ImGui::SameLine();
    if (ImGui::Button("Undo (Z)")) {
        do_undo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Redo (Y)")) {
        do_redo();
    }

    draw_preset_ui();
    draw_parameter_groups();
    draw_visualization();

    ImGui::Separator();
    ImGui::Text("Status: %s", g_app.status.c_str());

    ImGui::End();

    ImGui::Render();
    glViewport(0, 0, glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT));
    glClearColor(0.07f, 0.08f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

    glutSwapBuffers();
}

void idle_callback() {
    glutPostRedisplay();
}

void init_presets() {
    g_app.presets = {
        make_presets_weapons(),
        make_presets_ui(),
        make_presets_combat(),
        make_presets_movement(),
        make_presets_pickups()
    };
}
}

int main(int argc, char** argv) {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    init_presets();
    rerender_preview();

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
    glutInitWindowSize(900, 950);
    glutCreateWindow("sfxDesigner");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(kUiSizeScale);
    ImGui::GetIO().FontGlobalScale = g_app.ui_font_scale;

    ImGui_ImplGLUT_Init();
    ImGui_ImplGLUT_InstallFuncs();
    ImGui_ImplOpenGL2_Init();

    glutDisplayFunc(display_callback);
    glutKeyboardFunc(handle_keyboard);
    glutIdleFunc(idle_callback);

    glutMainLoop();

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGLUT_Shutdown();
    ImGui::DestroyContext();
    return 0;
}
