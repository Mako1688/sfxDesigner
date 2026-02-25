#pragma once

#include <string>
#include <vector>

enum class WaveType {
    Square = 0,
    Sawtooth = 1,
    Sine = 2,
    Noise = 3
};

struct SfxDef {
    WaveType wave_type = WaveType::Square;
    float base_freq = 220.0f;
    float freq_limit = 20.0f;
    float freq_slide = 0.0f;
    float freq_delta_slide = 0.0f;
    float duty_cycle = 0.5f;
    float duty_sweep = 0.0f;
    float vibrato_depth = 0.0f;
    float vibrato_speed = 0.0f;
    float attack_time = 0.01f;
    float sustain_time = 0.2f;
    float decay_time = 0.2f;
    float sustain_punch = 0.0f;
    float lp_filter_cutoff = 1.0f;
    float lp_filter_resonance = 0.0f;
    float hp_filter_cutoff = 0.0f;
    float phaser_offset = 0.0f;
    float tonal_mix = 1.0f;
    float noise_mix = 0.0f;
    float noise_attack = 0.001f;
    float noise_decay = 0.08f;
    float noise_hp = 0.2f;
};

struct ParameterRange {
    float min;
    float max;
};

struct SfxParamRanges {
    ParameterRange base_freq;
    ParameterRange freq_limit;
    ParameterRange freq_slide;
    ParameterRange freq_delta_slide;
    ParameterRange duty_cycle;
    ParameterRange duty_sweep;
    ParameterRange vibrato_depth;
    ParameterRange vibrato_speed;
    ParameterRange attack_time;
    ParameterRange sustain_time;
    ParameterRange decay_time;
    ParameterRange sustain_punch;
    ParameterRange lp_filter_cutoff;
    ParameterRange lp_filter_resonance;
    ParameterRange hp_filter_cutoff;
    ParameterRange phaser_offset;
    ParameterRange tonal_mix;
    ParameterRange noise_mix;
    ParameterRange noise_attack;
    ParameterRange noise_decay;
    ParameterRange noise_hp;
};

struct NamedPreset {
    std::string name;
    SfxDef value;
};

std::string wave_type_to_string(WaveType wave);
const char* wave_type_label(WaveType wave);
SfxDef make_default_sfx();
const SfxParamRanges& sfx_param_ranges();
std::vector<NamedPreset> make_presets_weapons();
std::vector<NamedPreset> make_presets_ui();
std::vector<NamedPreset> make_presets_combat();
std::vector<NamedPreset> make_presets_movement();
std::vector<NamedPreset> make_presets_pickups();
