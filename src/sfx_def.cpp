#include "sfx_def.hpp"

SfxDef make_default_sfx() {
    return {
        WaveType::Square,
        220.0f,
        20.0f,
        0.0f,
        0.0f,
        0.5f,
        0.0f,
        0.0f,
        0.0f,
        0.01f,
        0.2f,
        0.2f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.001f,
        0.08f,
        0.2f
    };
}

const SfxParamRanges& sfx_param_ranges() {
    static const SfxParamRanges ranges = {
        {20.0f, 6000.0f},
        {1.0f, 4000.0f},
        {-4000.0f, 4000.0f},
        {-4000.0f, 4000.0f},
        {0.01f, 0.99f},
        {-1.0f, 1.0f},
        {0.0f, 2.0f},
        {0.0f, 80.0f},
        {0.0f, 5.0f},
        {0.0f, 5.0f},
        {0.0f, 5.0f},
        {0.0f, 1.5f},
        {0.01f, 1.0f},
        {0.0f, 1.0f},
        {0.0f, 0.99f},
        {-1.0f, 1.0f},
        {0.0f, 1.0f},
        {0.0f, 1.0f},
        {0.0f, 0.08f},
        {0.01f, 0.8f},
        {0.0f, 0.99f}
    };
    return ranges;
}

std::string wave_type_to_string(WaveType wave) {
    switch (wave) {
        case WaveType::Square: return "SQUARE";
        case WaveType::Sawtooth: return "SAWTOOTH";
        case WaveType::Sine: return "SINE";
        case WaveType::Noise: return "NOISE";
    }
    return "SQUARE";
}

const char* wave_type_label(WaveType wave) {
    switch (wave) {
        case WaveType::Square: return "Square";
        case WaveType::Sawtooth: return "Sawtooth";
        case WaveType::Sine: return "Sine";
        case WaveType::Noise: return "Noise";
    }
    return "Square";
}

std::vector<NamedPreset> make_presets_weapons() {
    return {
        {"PISTOL_FIRE", {WaveType::Sawtooth, 220.0f, 20.0f, -160.0f, -20.0f, 0.35f, -0.1f, 0.02f, 10.0f, 0.0f, 0.08f, 0.12f, 0.4f, 0.9f, 0.1f, 0.0f, 0.0f}},
        {"SHOTGUN_BLAST", {WaveType::Noise, 320.0f, 40.0f, -260.0f, -10.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.06f, 0.2f, 0.5f, 0.8f, 0.05f, 0.02f, 0.0f}},
        {"LASER", {WaveType::Square, 880.0f, 180.0f, -480.0f, -40.0f, 0.2f, 0.02f, 0.06f, 13.0f, 0.0f, 0.08f, 0.2f, 0.2f, 1.0f, 0.0f, 0.03f, 0.0f}}
    };
}

std::vector<NamedPreset> make_presets_ui() {
    return {
        {"UI_CONFIRM", {WaveType::Sine, 660.0f, 120.0f, -120.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.03f, 0.1f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f}},
        {"UI_CANCEL", {WaveType::Square, 330.0f, 80.0f, -200.0f, 0.0f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.02f, 0.16f, 0.0f, 0.95f, 0.0f, 0.0f, 0.0f}},
        {"UI_HOVER", {WaveType::Sine, 520.0f, 200.0f, -60.0f, 0.0f, 0.5f, 0.0f, 0.01f, 8.0f, 0.0f, 0.01f, 0.05f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f}}
    };
}

std::vector<NamedPreset> make_presets_combat() {
    return {
        {"HIT_LIGHT", {WaveType::Noise, 280.0f, 20.0f, -180.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.02f, 0.08f, 0.4f, 0.85f, 0.0f, 0.02f, 0.0f}},
        {"HIT_HEAVY", {WaveType::Noise, 180.0f, 10.0f, -120.0f, -5.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.03f, 0.18f, 0.7f, 0.7f, 0.1f, 0.04f, 0.0f}},
        {"BLOCK", {WaveType::Sawtooth, 450.0f, 120.0f, -90.0f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.01f, 0.12f, 0.1f, 0.95f, 0.0f, -0.03f, 0.0f}}
    };
}

std::vector<NamedPreset> make_presets_movement() {
    return {
        {"JUMP", {WaveType::Square, 280.0f, 120.0f, 220.0f, -120.0f, 0.5f, -0.03f, 0.01f, 7.0f, 0.0f, 0.03f, 0.15f, 0.15f, 1.0f, 0.0f, 0.0f, 0.0f}},
        {"DASH", {WaveType::Sawtooth, 500.0f, 80.0f, -300.0f, 40.0f, 0.2f, 0.05f, 0.02f, 13.0f, 0.0f, 0.02f, 0.09f, 0.2f, 0.95f, 0.0f, 0.01f, 0.0f}},
        {"LAND", {WaveType::Noise, 150.0f, 20.0f, -80.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.01f, 0.1f, 0.4f, 0.75f, 0.15f, 0.03f, 0.0f}}
    };
}

std::vector<NamedPreset> make_presets_pickups() {
    return {
        {"PICKUP_SMALL", {WaveType::Sine, 520.0f, 280.0f, 140.0f, -40.0f, 0.5f, 0.0f, 0.03f, 10.0f, 0.0f, 0.02f, 0.12f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f}},
        {"PICKUP_RARE", {WaveType::Sine, 460.0f, 320.0f, 220.0f, -20.0f, 0.5f, 0.0f, 0.06f, 9.0f, 0.0f, 0.03f, 0.22f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f}},
        {"PICKUP_HEALTH", {WaveType::Square, 380.0f, 260.0f, 180.0f, -30.0f, 0.35f, 0.02f, 0.02f, 7.0f, 0.0f, 0.02f, 0.18f, 0.0f, 0.95f, 0.0f, 0.0f, 0.0f}}
    };
}
