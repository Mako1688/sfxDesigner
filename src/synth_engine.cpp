#include "synth_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
constexpr float kPi = 3.14159265358979323846f;

float clampf(float value, float lo, float hi) {
    return std::max(lo, std::min(value, hi));
}

float oscillator(WaveType wave, float phase, float duty) {
    switch (wave) {
        case WaveType::Square:
            return (phase < duty ? 1.0f : -1.0f);
        case WaveType::Sawtooth:
            return 2.0f * phase - 1.0f;
        case WaveType::Sine:
            return std::sin(phase * kPi * 2.0f);
        case WaveType::Noise:
            return (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 2.0f - 1.0f;
    }
    return 0.0f;
}
}

std::vector<float> render_samples(const SfxDef& sfx, int sample_rate, float duration_seconds, RenderDebugData* debug) {
    const int sample_count = std::max(1, static_cast<int>(duration_seconds * static_cast<float>(sample_rate)));
    std::vector<float> samples;
    samples.resize(static_cast<size_t>(sample_count), 0.0f);

    if (debug) {
        debug->envelope.assign(static_cast<size_t>(sample_count), 0.0f);
        debug->frequency.assign(static_cast<size_t>(sample_count), 0.0f);
        debug->waveform.assign(static_cast<size_t>(sample_count), 0.0f);
    }

    const float attack = std::max(0.0f, sfx.attack_time);
    const float sustain = std::max(0.0f, sfx.sustain_time);
    const float decay = std::max(0.0f, sfx.decay_time);
    const float total_env = std::max(0.0001f, attack + sustain + decay);

    float freq = std::max(1.0f, sfx.base_freq);
    float freq_slide = sfx.freq_slide;
    float duty = clampf(sfx.duty_cycle, 0.01f, 0.99f);
    float phase = 0.0f;

    float lp_state = 0.0f;
    float hp_state = 0.0f;
    float prev_lp = 0.0f;
    float phaser_phase = 0.0f;

    const float vibrato_depth = std::max(0.0f, sfx.vibrato_depth);
    const float vibrato_speed = std::max(0.0f, sfx.vibrato_speed);
    const float lp_cutoff = clampf(sfx.lp_filter_cutoff, 0.01f, 1.0f);
    const float hp_cutoff = clampf(sfx.hp_filter_cutoff, 0.0f, 0.99f);
    const float resonance = clampf(sfx.lp_filter_resonance, 0.0f, 1.0f);

    for (int i = 0; i < sample_count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sample_rate);
        const float normalized_t = t / total_env;

        float env = 0.0f;
        if (t < attack) {
            env = t / std::max(attack, 0.0001f);
        } else if (t < attack + sustain) {
            env = 1.0f + sfx.sustain_punch * (1.0f - ((t - attack) / std::max(sustain, 0.0001f)));
        } else if (t < attack + sustain + decay) {
            env = 1.0f - ((t - attack - sustain) / std::max(decay, 0.0001f));
        }
        env = clampf(env, 0.0f, 1.5f);

        freq += freq_slide / static_cast<float>(sample_rate);
        freq_slide += sfx.freq_delta_slide / static_cast<float>(sample_rate);
        freq = std::max(std::max(1.0f, sfx.freq_limit), freq);

        duty += sfx.duty_sweep / static_cast<float>(sample_rate);
        duty = clampf(duty, 0.01f, 0.99f);

        const float vib = std::sin(t * vibrato_speed * 2.0f * kPi) * vibrato_depth;
        const float current_freq = std::max(1.0f, freq + vib * freq);

        phase += current_freq / static_cast<float>(sample_rate);
        phase -= std::floor(phase);

        float raw = oscillator(sfx.wave_type, phase, duty);

        const float lpf_alpha = clampf(lp_cutoff * (1.0f - resonance * 0.5f), 0.01f, 1.0f);
        lp_state += (raw - lp_state) * lpf_alpha;
        const float hp_input = lp_state - prev_lp;
        prev_lp = lp_state;
        hp_state += (hp_input - hp_state) * (1.0f - hp_cutoff);

        phaser_phase += std::max(0.0f, sfx.phaser_offset) / static_cast<float>(sample_rate);
        const float phaser = std::sin(phaser_phase * 2.0f * kPi) * 0.2f;

        float sample = (hp_state + phaser) * env;
        sample *= std::max(0.0f, 1.0f - normalized_t * 0.1f);
        sample = clampf(sample, -1.0f, 1.0f);

        samples[static_cast<size_t>(i)] = sample;

        if (debug) {
            debug->envelope[static_cast<size_t>(i)] = env;
            debug->frequency[static_cast<size_t>(i)] = current_freq;
            debug->waveform[static_cast<size_t>(i)] = sample;
        }
    }

    return samples;
}
