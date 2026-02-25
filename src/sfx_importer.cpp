#include "sfx_importer.hpp"

#include <algorithm>
#include <cmath>

namespace {
float clampf(float value, float lo, float hi) {
    return std::max(lo, std::min(value, hi));
}

float rms(const std::vector<float>& x, size_t start, size_t end) {
    if (x.empty() || start >= end) {
        return 0.0f;
    }
    end = std::min(end, x.size());
    double s = 0.0;
    size_t count = 0;
    for (size_t i = start; i < end; ++i) {
        s += static_cast<double>(x[i]) * static_cast<double>(x[i]);
        ++count;
    }
    if (count == 0) {
        return 0.0f;
    }
    return static_cast<float>(std::sqrt(s / static_cast<double>(count)));
}

float zero_cross_rate(const std::vector<float>& x, size_t start, size_t end) {
    if (x.empty() || start + 1 >= end) {
        return 0.0f;
    }
    end = std::min(end, x.size());
    int crossings = 0;
    int samples = 0;
    for (size_t i = start + 1; i < end; ++i) {
        const bool a = x[i - 1] >= 0.0f;
        const bool b = x[i] >= 0.0f;
        if (a != b) {
            ++crossings;
        }
        ++samples;
    }
    return samples > 0 ? static_cast<float>(crossings) / static_cast<float>(samples) : 0.0f;
}

float estimate_freq_from_zcr(const std::vector<float>& x, int sample_rate, size_t start, size_t end) {
    const float zcr = zero_cross_rate(x, start, end);
    return clampf(zcr * static_cast<float>(sample_rate) * 0.5f, 20.0f, 2000.0f);
}

float peak_abs(const std::vector<float>& x, size_t start, size_t end) {
    end = std::min(end, x.size());
    float p = 0.0f;
    for (size_t i = start; i < end; ++i) {
        p = std::max(p, std::fabs(x[i]));
    }
    return p;
}
}

SfxDef fit_sfx_from_samples(const std::vector<float>& samples, int sample_rate) {
    SfxDef out = make_default_sfx();
    const auto& ranges = sfx_param_ranges();

    if (samples.empty() || sample_rate <= 1000) {
        return out;
    }

    const size_t n = samples.size();
    const size_t first = 0;
    const size_t mid = n / 2;
    const size_t tail = (n * 3) / 4;

    const float rms_all = rms(samples, 0, n);
    const float rms_first = rms(samples, first, std::max(first + 1, n / 6));
    const float rms_mid = rms(samples, mid / 2, std::max(mid / 2 + 1, mid));
    const float rms_tail = rms(samples, tail, n);

    const float freq_first = estimate_freq_from_zcr(samples, sample_rate, first, std::max(first + 1, n / 4));
    const float freq_late = estimate_freq_from_zcr(samples, sample_rate, tail, n);

    const float zcr_all = zero_cross_rate(samples, 0, n);
    const float crest = peak_abs(samples, 0, n) / std::max(0.0001f, rms_all);

    if (zcr_all > 0.20f) {
        out.wave_type = WaveType::Noise;
    } else if (crest > 2.8f) {
        out.wave_type = WaveType::Square;
    } else if (zcr_all > 0.08f) {
        out.wave_type = WaveType::Sawtooth;
    } else {
        out.wave_type = WaveType::Sine;
    }

    out.base_freq = clampf(freq_first, ranges.base_freq.min, ranges.base_freq.max);

    const float slide_hz = (freq_late - freq_first) * 4.0f;
    out.freq_slide = clampf(slide_hz, ranges.freq_slide.min, ranges.freq_slide.max);
    out.freq_delta_slide = 0.0f;
    out.freq_limit = clampf(std::min(freq_first, freq_late) * 0.6f, ranges.freq_limit.min, ranges.freq_limit.max);

    out.duty_cycle = 0.5f;
    out.duty_sweep = 0.0f;

    const float zcr_delta = std::fabs(freq_late - freq_first) / std::max(1.0f, freq_first);
    out.vibrato_depth = clampf(zcr_delta * 0.12f, ranges.vibrato_depth.min, ranges.vibrato_depth.max);
    out.vibrato_speed = clampf(6.0f + zcr_all * 40.0f, ranges.vibrato_speed.min, ranges.vibrato_speed.max);

    const float total_sec = static_cast<float>(n) / static_cast<float>(sample_rate);
    const float attack_guess = total_sec * 0.08f;
    const float decay_guess = total_sec * 0.22f;
    const float sustain_guess = std::max(0.01f, total_sec - attack_guess - decay_guess);
    out.attack_time = clampf(attack_guess, ranges.attack_time.min, ranges.attack_time.max);
    out.sustain_time = clampf(sustain_guess, ranges.sustain_time.min, ranges.sustain_time.max);
    out.decay_time = clampf(decay_guess, ranges.decay_time.min, ranges.decay_time.max);

    const float punch = (rms_first - rms_mid) / std::max(0.001f, rms_mid);
    out.sustain_punch = clampf(punch * 0.45f, ranges.sustain_punch.min, ranges.sustain_punch.max);

    const float brightness = clampf(zcr_all * 5.0f, 0.0f, 1.0f);
    out.lp_filter_cutoff = clampf(0.35f + brightness * 0.65f, ranges.lp_filter_cutoff.min, ranges.lp_filter_cutoff.max);
    out.lp_filter_resonance = clampf((crest - 1.2f) * 0.2f, ranges.lp_filter_resonance.min, ranges.lp_filter_resonance.max);
    out.hp_filter_cutoff = clampf((1.0f - rms_tail / std::max(0.0001f, rms_first)) * 0.2f, ranges.hp_filter_cutoff.min, ranges.hp_filter_cutoff.max);

    out.phaser_offset = clampf((freq_late - freq_first) / 1200.0f, ranges.phaser_offset.min, ranges.phaser_offset.max);

    return out;
}
