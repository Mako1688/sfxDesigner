#include "sfx_importer.hpp"
#include "synth_engine.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#if defined(USE_AUBIO_SNDFILE)
#include <aubio/aubio.h>
#endif

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

float estimate_freq_autocorr(const std::vector<float>& x, int sample_rate, size_t start, size_t end) {
    if (x.empty() || sample_rate <= 0 || start >= end) {
        return 0.0f;
    }

    end = std::min(end, x.size());
    const size_t count = end - start;
    if (count < 256) {
        return 0.0f;
    }

    const int min_lag = std::max(2, sample_rate / 2400);
    const int max_lag = std::max(min_lag + 1, std::min<int>(sample_rate / 35, static_cast<int>(count / 2)));

    double mean = 0.0;
    for (size_t i = start; i < end; ++i) {
        mean += x[i];
    }
    mean /= static_cast<double>(count);

    double best_corr = -1.0;
    int best_lag = 0;
    for (int lag = min_lag; lag <= max_lag; ++lag) {
        double num = 0.0;
        double den_a = 0.0;
        double den_b = 0.0;
        for (size_t i = start; i + static_cast<size_t>(lag) < end; ++i) {
            const double a = static_cast<double>(x[i]) - mean;
            const double b = static_cast<double>(x[i + static_cast<size_t>(lag)]) - mean;
            num += a * b;
            den_a += a * a;
            den_b += b * b;
        }
        const double den = std::sqrt(std::max(1e-12, den_a * den_b));
        const double corr = num / den;
        if (corr > best_corr) {
            best_corr = corr;
            best_lag = lag;
        }
    }

    if (best_lag <= 0 || best_corr < 0.08) {
        return 0.0f;
    }
    return static_cast<float>(sample_rate) / static_cast<float>(best_lag);
}

#if defined(USE_AUBIO_SNDFILE)
float estimate_freq_with_aubio(const std::vector<float>& x, int sample_rate, size_t start, size_t end) {
    if (x.empty() || sample_rate <= 1000) {
        return 0.0f;
    }
    end = std::min(end, x.size());
    if (start + 256 >= end) {
        return 0.0f;
    }

    const uint_t buffer_size = 2048;
    const uint_t hop_size = 256;
    aubio_pitch_t* pitch = new_aubio_pitch("yinfft", buffer_size, hop_size, static_cast<uint_t>(sample_rate));
    if (pitch == nullptr) {
        return 0.0f;
    }
    aubio_pitch_set_unit(pitch, "Hz");
    aubio_pitch_set_tolerance(pitch, 0.75f);

    fvec_t* in = new_fvec(hop_size);
    fvec_t* out = new_fvec(1);
    if (in == nullptr || out == nullptr) {
        if (in) del_fvec(in);
        if (out) del_fvec(out);
        del_aubio_pitch(pitch);
        return 0.0f;
    }

    std::vector<float> freqs;
    freqs.reserve((end - start) / hop_size + 1);

    size_t cursor = start;
    while (cursor + hop_size <= end) {
        for (uint_t i = 0; i < hop_size; ++i) {
            in->data[i] = x[cursor + i];
        }
        aubio_pitch_do(pitch, in, out);
        const float hz = out->data[0];
        if (hz > 20.0f && hz < 8000.0f) {
            freqs.push_back(hz);
        }
        cursor += hop_size;
    }

    del_fvec(in);
    del_fvec(out);
    del_aubio_pitch(pitch);

    if (freqs.empty()) {
        return 0.0f;
    }

    std::nth_element(freqs.begin(), freqs.begin() + freqs.size() / 2, freqs.end());
    return freqs[freqs.size() / 2];
}
#endif

float peak_abs(const std::vector<float>& x, size_t start, size_t end) {
    end = std::min(end, x.size());
    float p = 0.0f;
    for (size_t i = start; i < end; ++i) {
        p = std::max(p, std::fabs(x[i]));
    }
    return p;
}

std::vector<float> amplitude_envelope(const std::vector<float>& x, int sample_rate) {
    std::vector<float> env;
    env.resize(x.size(), 0.0f);
    if (x.empty() || sample_rate <= 0) {
        return env;
    }

    const float attack_t = 0.002f;
    const float release_t = 0.025f;
    const float atk = std::exp(-1.0f / std::max(1.0f, attack_t * static_cast<float>(sample_rate)));
    const float rel = std::exp(-1.0f / std::max(1.0f, release_t * static_cast<float>(sample_rate)));

    float follower = 0.0f;
    for (size_t i = 0; i < x.size(); ++i) {
        const float mag = std::fabs(x[i]);
        const float coeff = (mag > follower) ? atk : rel;
        follower = coeff * follower + (1.0f - coeff) * mag;
        env[i] = follower;
    }
    return env;
}

void estimate_adsr_from_envelope(
    const std::vector<float>& env,
    int sample_rate,
    float& attack_s,
    float& sustain_s,
    float& decay_s
) {
    attack_s = 0.01f;
    sustain_s = 0.2f;
    decay_s = 0.2f;
    if (env.empty() || sample_rate <= 0) {
        return;
    }

    float peak = 0.0f;
    for (float v : env) {
        peak = std::max(peak, v);
    }
    if (peak <= 1e-6f) {
        return;
    }

    const float on_thresh = peak * 0.08f;
    const float peak_thresh = peak * 0.9f;
    const float end_thresh = peak * 0.06f;

    size_t on_idx = 0;
    while (on_idx < env.size() && env[on_idx] < on_thresh) {
        ++on_idx;
    }
    size_t atk_idx = on_idx;
    while (atk_idx < env.size() && env[atk_idx] < peak_thresh) {
        ++atk_idx;
    }

    size_t end_idx = env.size() - 1;
    while (end_idx > on_idx && env[end_idx] < end_thresh) {
        --end_idx;
    }
    if (end_idx <= atk_idx) {
        end_idx = std::min(env.size() - 1, atk_idx + static_cast<size_t>(sample_rate / 8));
    }

    const float total_active = static_cast<float>(end_idx - on_idx + 1) / static_cast<float>(sample_rate);
    const float attack_raw = static_cast<float>(atk_idx > on_idx ? (atk_idx - on_idx) : 1) / static_cast<float>(sample_rate);
    attack_s = std::max(0.0f, attack_raw);
    sustain_s = std::max(0.01f, total_active * 0.55f);
    decay_s = std::max(0.01f, total_active * 0.35f);
}

float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

struct SeriesFeatures {
    std::vector<float> rms;
    std::vector<float> zcr;
    std::vector<std::vector<float>> bands;
};

std::vector<float> stft_band_energies(const std::vector<float>& x, size_t start, size_t end, int sample_rate) {
    const int band_count = 10;
    std::vector<float> energies(static_cast<size_t>(band_count), 0.0f);
    if (end <= start + 8) {
        return energies;
    }

    const size_t n = end - start;
    const float min_f = 80.0f;
    const float max_f = std::min(12000.0f, static_cast<float>(sample_rate) * 0.45f);
    if (max_f <= min_f) {
        return energies;
    }

    std::vector<float> centers(static_cast<size_t>(band_count), 0.0f);
    for (int b = 0; b < band_count; ++b) {
        const float t = static_cast<float>(b) / static_cast<float>(band_count - 1);
        centers[static_cast<size_t>(b)] = min_f * std::pow(max_f / min_f, t);
    }

    for (int b = 0; b < band_count; ++b) {
        const float f = centers[static_cast<size_t>(b)];
        float re = 0.0f;
        float im = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            const float w = 0.5f - 0.5f * std::cos(2.0f * 3.14159265358979323846f * static_cast<float>(i) / static_cast<float>(n - 1));
            const float ph = 2.0f * 3.14159265358979323846f * f * static_cast<float>(i) / static_cast<float>(sample_rate);
            const float s = x[start + i] * w;
            re += s * std::cos(ph);
            im -= s * std::sin(ph);
        }
        energies[static_cast<size_t>(b)] = std::sqrt(re * re + im * im) / std::max(1.0f, static_cast<float>(n));
    }

    float sum = 0.0f;
    for (float v : energies) {
        sum += v;
    }
    if (sum > 1e-8f) {
        for (float& v : energies) {
            v /= sum;
        }
    }
    return energies;
}

SeriesFeatures make_features(const std::vector<float>& x, int sample_rate) {
    SeriesFeatures f;
    if (x.empty()) {
        return f;
    }
    const size_t window = 1024;
    const size_t hop = 512;
    for (size_t start = 0; start < x.size(); start += hop) {
        const size_t end = std::min(x.size(), start + window);
        if (end <= start + 4) {
            break;
        }
        f.rms.push_back(rms(x, start, end));
        f.zcr.push_back(zero_cross_rate(x, start, end));
        f.bands.push_back(stft_band_energies(x, start, end, sample_rate));
        if (end == x.size()) {
            break;
        }
    }
    return f;
}

float features_distance(const SeriesFeatures& a, const SeriesFeatures& b) {
    const size_t n = std::min(a.rms.size(), b.rms.size());
    if (n == 0) {
        return 1e9f;
    }
    float e = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        e += std::fabs(a.rms[i] - b.rms[i]) * 1.4f;
        e += std::fabs(a.zcr[i] - b.zcr[i]) * 0.5f;
        const size_t bn = std::min(a.bands[i].size(), b.bands[i].size());
        for (size_t k = 0; k < bn; ++k) {
            e += std::fabs(a.bands[i][k] - b.bands[i][k]) * 2.0f;
        }
    }
    return e / static_cast<float>(n);
}

SfxDef clamp_sfx_to_ranges(const SfxDef& in) {
    const auto& r = sfx_param_ranges();
    SfxDef o = in;
    o.base_freq = clampf(o.base_freq, r.base_freq.min, r.base_freq.max);
    o.freq_limit = clampf(o.freq_limit, r.freq_limit.min, r.freq_limit.max);
    o.freq_slide = clampf(o.freq_slide, r.freq_slide.min, r.freq_slide.max);
    o.freq_delta_slide = clampf(o.freq_delta_slide, r.freq_delta_slide.min, r.freq_delta_slide.max);
    o.duty_cycle = clampf(o.duty_cycle, r.duty_cycle.min, r.duty_cycle.max);
    o.duty_sweep = clampf(o.duty_sweep, r.duty_sweep.min, r.duty_sweep.max);
    o.vibrato_depth = clampf(o.vibrato_depth, r.vibrato_depth.min, r.vibrato_depth.max);
    o.vibrato_speed = clampf(o.vibrato_speed, r.vibrato_speed.min, r.vibrato_speed.max);
    o.attack_time = clampf(o.attack_time, r.attack_time.min, r.attack_time.max);
    o.sustain_time = clampf(o.sustain_time, r.sustain_time.min, r.sustain_time.max);
    o.decay_time = clampf(o.decay_time, r.decay_time.min, r.decay_time.max);
    o.sustain_punch = clampf(o.sustain_punch, r.sustain_punch.min, r.sustain_punch.max);
    o.lp_filter_cutoff = clampf(o.lp_filter_cutoff, r.lp_filter_cutoff.min, r.lp_filter_cutoff.max);
    o.lp_filter_resonance = clampf(o.lp_filter_resonance, r.lp_filter_resonance.min, r.lp_filter_resonance.max);
    o.hp_filter_cutoff = clampf(o.hp_filter_cutoff, r.hp_filter_cutoff.min, r.hp_filter_cutoff.max);
    o.phaser_offset = clampf(o.phaser_offset, r.phaser_offset.min, r.phaser_offset.max);
    o.tonal_mix = clampf(o.tonal_mix, r.tonal_mix.min, r.tonal_mix.max);
    o.noise_mix = clampf(o.noise_mix, r.noise_mix.min, r.noise_mix.max);
    o.noise_attack = clampf(o.noise_attack, r.noise_attack.min, r.noise_attack.max);
    o.noise_decay = clampf(o.noise_decay, r.noise_decay.min, r.noise_decay.max);
    o.noise_hp = clampf(o.noise_hp, r.noise_hp.min, r.noise_hp.max);
    return o;
}

float evaluate_candidate(
    const SfxDef& candidate,
    const SeriesFeatures& source_features,
    int sample_rate,
    float duration
) {
    std::vector<float> synth = render_samples(candidate, sample_rate, duration, nullptr);
    const SeriesFeatures synth_features = make_features(synth, sample_rate);
    return features_distance(source_features, synth_features);
}

SfxDef refine_fit(
    const SfxDef& seed,
    const std::vector<float>& source,
    int sample_rate,
    float duration,
    float fidelity_boost
) {
    const SeriesFeatures source_features = make_features(source, sample_rate);
    if (source_features.rms.empty()) {
        return seed;
    }

    SfxDef best = clamp_sfx_to_ranges(seed);
    float best_err = evaluate_candidate(best, source_features, sample_rate, duration);

    const int passes = 1 + static_cast<int>(std::round(clampf(fidelity_boost, 0.0f, 1.0f) * 3.0f));
    const float decay = 0.55f;
    float step_freq = 500.0f;
    float step_env = 0.6f;
    float step_misc = 0.4f;

    for (int p = 0; p < passes; ++p) {
        const float mult = std::pow(decay, static_cast<float>(p));
        const float df = step_freq * mult;
        const float de = step_env * mult;
        const float dm = step_misc * mult;

        std::vector<std::pair<float*, float>> params = {
            {&best.base_freq, df},
            {&best.freq_limit, df},
            {&best.freq_slide, df},
            {&best.freq_delta_slide, df * 0.6f},
            {&best.attack_time, de},
            {&best.sustain_time, de},
            {&best.decay_time, de},
            {&best.sustain_punch, dm},
            {&best.vibrato_depth, dm},
            {&best.vibrato_speed, df * 0.03f},
            {&best.lp_filter_cutoff, dm},
            {&best.lp_filter_resonance, dm},
            {&best.hp_filter_cutoff, dm},
            {&best.phaser_offset, dm},
            {&best.tonal_mix, dm},
            {&best.noise_mix, dm},
            {&best.noise_attack, dm * 0.1f},
            {&best.noise_decay, dm * 0.2f},
            {&best.noise_hp, dm}
        };

        for (auto& entry : params) {
            float* ptr = entry.first;
            const float step = entry.second;
            const float original = *ptr;

            for (int dir = -1; dir <= 1; dir += 2) {
                SfxDef candidate = best;
                float* candidate_ptr = nullptr;

                if (ptr == &best.base_freq) candidate_ptr = &candidate.base_freq;
                else if (ptr == &best.freq_limit) candidate_ptr = &candidate.freq_limit;
                else if (ptr == &best.freq_slide) candidate_ptr = &candidate.freq_slide;
                else if (ptr == &best.freq_delta_slide) candidate_ptr = &candidate.freq_delta_slide;
                else if (ptr == &best.attack_time) candidate_ptr = &candidate.attack_time;
                else if (ptr == &best.sustain_time) candidate_ptr = &candidate.sustain_time;
                else if (ptr == &best.decay_time) candidate_ptr = &candidate.decay_time;
                else if (ptr == &best.sustain_punch) candidate_ptr = &candidate.sustain_punch;
                else if (ptr == &best.vibrato_depth) candidate_ptr = &candidate.vibrato_depth;
                else if (ptr == &best.vibrato_speed) candidate_ptr = &candidate.vibrato_speed;
                else if (ptr == &best.lp_filter_cutoff) candidate_ptr = &candidate.lp_filter_cutoff;
                else if (ptr == &best.lp_filter_resonance) candidate_ptr = &candidate.lp_filter_resonance;
                else if (ptr == &best.hp_filter_cutoff) candidate_ptr = &candidate.hp_filter_cutoff;
                else if (ptr == &best.phaser_offset) candidate_ptr = &candidate.phaser_offset;
                else if (ptr == &best.tonal_mix) candidate_ptr = &candidate.tonal_mix;
                else if (ptr == &best.noise_mix) candidate_ptr = &candidate.noise_mix;
                else if (ptr == &best.noise_attack) candidate_ptr = &candidate.noise_attack;
                else if (ptr == &best.noise_decay) candidate_ptr = &candidate.noise_decay;
                else if (ptr == &best.noise_hp) candidate_ptr = &candidate.noise_hp;

                if (!candidate_ptr) {
                    continue;
                }
                *candidate_ptr = original + step * static_cast<float>(dir);
                candidate = clamp_sfx_to_ranges(candidate);
                const float err = evaluate_candidate(candidate, source_features, sample_rate, duration);
                if (err < best_err) {
                    best = candidate;
                    best_err = err;
                }
            }
        }

        for (int wave = 0; wave < 4; ++wave) {
            SfxDef candidate = best;
            candidate.wave_type = static_cast<WaveType>(wave);
            const float err = evaluate_candidate(candidate, source_features, sample_rate, duration);
            if (err < best_err) {
                best = candidate;
                best_err = err;
            }
        }
    }

    return clamp_sfx_to_ranges(best);
}

float snap_to_step(float value, float step) {
    if (step <= 0.0f) {
        return value;
    }
    return std::round(value / step) * step;
}

SfxDef blend_sfx(const SfxDef& retro, const SfxDef& source_like, float t) {
    const auto& ranges = sfx_param_ranges();
    SfxDef out = source_like;

    out.wave_type = (t >= 0.5f) ? source_like.wave_type : retro.wave_type;
    out.base_freq = clampf(lerpf(retro.base_freq, source_like.base_freq, t), ranges.base_freq.min, ranges.base_freq.max);
    out.freq_limit = clampf(lerpf(retro.freq_limit, source_like.freq_limit, t), ranges.freq_limit.min, ranges.freq_limit.max);
    out.freq_slide = clampf(lerpf(retro.freq_slide, source_like.freq_slide, t), ranges.freq_slide.min, ranges.freq_slide.max);
    out.freq_delta_slide = clampf(lerpf(retro.freq_delta_slide, source_like.freq_delta_slide, t), ranges.freq_delta_slide.min, ranges.freq_delta_slide.max);
    out.duty_cycle = clampf(lerpf(retro.duty_cycle, source_like.duty_cycle, t), ranges.duty_cycle.min, ranges.duty_cycle.max);
    out.duty_sweep = clampf(lerpf(retro.duty_sweep, source_like.duty_sweep, t), ranges.duty_sweep.min, ranges.duty_sweep.max);
    out.vibrato_depth = clampf(lerpf(retro.vibrato_depth, source_like.vibrato_depth, t), ranges.vibrato_depth.min, ranges.vibrato_depth.max);
    out.vibrato_speed = clampf(lerpf(retro.vibrato_speed, source_like.vibrato_speed, t), ranges.vibrato_speed.min, ranges.vibrato_speed.max);
    out.attack_time = clampf(lerpf(retro.attack_time, source_like.attack_time, t), ranges.attack_time.min, ranges.attack_time.max);
    out.sustain_time = clampf(lerpf(retro.sustain_time, source_like.sustain_time, t), ranges.sustain_time.min, ranges.sustain_time.max);
    out.decay_time = clampf(lerpf(retro.decay_time, source_like.decay_time, t), ranges.decay_time.min, ranges.decay_time.max);
    out.sustain_punch = clampf(lerpf(retro.sustain_punch, source_like.sustain_punch, t), ranges.sustain_punch.min, ranges.sustain_punch.max);
    out.lp_filter_cutoff = clampf(lerpf(retro.lp_filter_cutoff, source_like.lp_filter_cutoff, t), ranges.lp_filter_cutoff.min, ranges.lp_filter_cutoff.max);
    out.lp_filter_resonance = clampf(lerpf(retro.lp_filter_resonance, source_like.lp_filter_resonance, t), ranges.lp_filter_resonance.min, ranges.lp_filter_resonance.max);
    out.hp_filter_cutoff = clampf(lerpf(retro.hp_filter_cutoff, source_like.hp_filter_cutoff, t), ranges.hp_filter_cutoff.min, ranges.hp_filter_cutoff.max);
    out.phaser_offset = clampf(lerpf(retro.phaser_offset, source_like.phaser_offset, t), ranges.phaser_offset.min, ranges.phaser_offset.max);
    out.tonal_mix = clampf(lerpf(retro.tonal_mix, source_like.tonal_mix, t), ranges.tonal_mix.min, ranges.tonal_mix.max);
    out.noise_mix = clampf(lerpf(retro.noise_mix, source_like.noise_mix, t), ranges.noise_mix.min, ranges.noise_mix.max);
    out.noise_attack = clampf(lerpf(retro.noise_attack, source_like.noise_attack, t), ranges.noise_attack.min, ranges.noise_attack.max);
    out.noise_decay = clampf(lerpf(retro.noise_decay, source_like.noise_decay, t), ranges.noise_decay.min, ranges.noise_decay.max);
    out.noise_hp = clampf(lerpf(retro.noise_hp, source_like.noise_hp, t), ranges.noise_hp.min, ranges.noise_hp.max);

    return out;
}

SfxDef make_retro_variant(const SfxDef& base) {
    const auto& ranges = sfx_param_ranges();
    SfxDef r = base;

    if (base.wave_type == WaveType::Sine) {
        r.wave_type = WaveType::Square;
    } else if (base.wave_type == WaveType::Sawtooth) {
        r.wave_type = WaveType::Square;
    }

    if (r.wave_type == WaveType::Square || r.wave_type == WaveType::Sawtooth) {
        const float options[] = {0.25f, 0.5f, 0.75f};
        float best = options[0];
        float best_dist = std::fabs(r.duty_cycle - best);
        for (float candidate : options) {
            const float d = std::fabs(r.duty_cycle - candidate);
            if (d < best_dist) {
                best_dist = d;
                best = candidate;
            }
        }
        r.duty_cycle = best;
    } else {
        r.duty_cycle = 0.5f;
    }

    r.base_freq = clampf(snap_to_step(r.base_freq, 20.0f), ranges.base_freq.min, ranges.base_freq.max);
    r.freq_slide = clampf(snap_to_step(r.freq_slide * 1.6f, 24.0f), ranges.freq_slide.min, ranges.freq_slide.max);
    r.freq_delta_slide = clampf(snap_to_step(r.freq_delta_slide * 1.5f, 8.0f), ranges.freq_delta_slide.min, ranges.freq_delta_slide.max);
    r.attack_time = std::max(ranges.attack_time.min, r.attack_time * 0.35f);
    r.sustain_time = std::max(ranges.sustain_time.min, r.sustain_time * 0.65f);
    r.decay_time = std::max(ranges.decay_time.min, r.decay_time * 0.7f);
    r.sustain_punch = clampf(r.sustain_punch + 0.35f, ranges.sustain_punch.min, ranges.sustain_punch.max);
    r.vibrato_depth = clampf(r.vibrato_depth * 0.45f, ranges.vibrato_depth.min, ranges.vibrato_depth.max);
    r.lp_filter_cutoff = clampf(r.lp_filter_cutoff * 0.72f, ranges.lp_filter_cutoff.min, ranges.lp_filter_cutoff.max);
    r.lp_filter_resonance = clampf(r.lp_filter_resonance + 0.2f, ranges.lp_filter_resonance.min, ranges.lp_filter_resonance.max);
    r.hp_filter_cutoff = clampf(r.hp_filter_cutoff + 0.06f, ranges.hp_filter_cutoff.min, ranges.hp_filter_cutoff.max);
    r.noise_mix = clampf(r.noise_mix + 0.25f, ranges.noise_mix.min, ranges.noise_mix.max);
    r.tonal_mix = clampf(r.tonal_mix * 0.82f, ranges.tonal_mix.min, ranges.tonal_mix.max);
    r.noise_attack = clampf(r.noise_attack * 0.6f, ranges.noise_attack.min, ranges.noise_attack.max);
    r.noise_decay = clampf(r.noise_decay * 0.8f, ranges.noise_decay.min, ranges.noise_decay.max);
    r.noise_hp = clampf(r.noise_hp + 0.1f, ranges.noise_hp.min, ranges.noise_hp.max);

    return r;
}

SfxDef make_source_variant(const SfxDef& base) {
    const auto& ranges = sfx_param_ranges();
    SfxDef s = base;

    if (base.wave_type == WaveType::Square) {
        s.wave_type = WaveType::Sawtooth;
    }

    s.duty_cycle = 0.5f;
    s.duty_sweep = clampf(s.duty_sweep * 0.5f, ranges.duty_sweep.min, ranges.duty_sweep.max);
    s.freq_slide = clampf(s.freq_slide * 0.65f, ranges.freq_slide.min, ranges.freq_slide.max);
    s.freq_delta_slide = clampf(s.freq_delta_slide * 0.5f, ranges.freq_delta_slide.min, ranges.freq_delta_slide.max);
    s.attack_time = clampf(s.attack_time * 1.35f + 0.004f, ranges.attack_time.min, ranges.attack_time.max);
    s.sustain_time = clampf(s.sustain_time * 1.25f + 0.01f, ranges.sustain_time.min, ranges.sustain_time.max);
    s.decay_time = clampf(s.decay_time * 1.2f + 0.01f, ranges.decay_time.min, ranges.decay_time.max);
    s.sustain_punch = clampf(s.sustain_punch * 0.55f, ranges.sustain_punch.min, ranges.sustain_punch.max);
    s.vibrato_depth = clampf(s.vibrato_depth * 0.85f, ranges.vibrato_depth.min, ranges.vibrato_depth.max);
    s.lp_filter_cutoff = clampf(0.45f + s.lp_filter_cutoff * 0.55f, ranges.lp_filter_cutoff.min, ranges.lp_filter_cutoff.max);
    s.lp_filter_resonance = clampf(s.lp_filter_resonance * 0.45f, ranges.lp_filter_resonance.min, ranges.lp_filter_resonance.max);
    s.hp_filter_cutoff = clampf(s.hp_filter_cutoff * 0.6f, ranges.hp_filter_cutoff.min, ranges.hp_filter_cutoff.max);
    s.noise_mix = clampf(s.noise_mix * 0.75f, ranges.noise_mix.min, ranges.noise_mix.max);
    s.tonal_mix = clampf(1.0f - s.noise_mix * 0.35f, ranges.tonal_mix.min, ranges.tonal_mix.max);
    s.noise_attack = clampf(s.noise_attack * 1.3f, ranges.noise_attack.min, ranges.noise_attack.max);
    s.noise_decay = clampf(s.noise_decay * 1.2f, ranges.noise_decay.min, ranges.noise_decay.max);
    s.noise_hp = clampf(s.noise_hp * 0.75f, ranges.noise_hp.min, ranges.noise_hp.max);

    return s;
}
}

SfxDef fit_sfx_from_samples(const std::vector<float>& samples, int sample_rate, float source_match, float fidelity_boost) {
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

    const std::vector<float> env = amplitude_envelope(samples, sample_rate);

    const size_t first_end = std::max(first + 1, n / 4);
    float freq_first = estimate_freq_autocorr(samples, sample_rate, first, first_end);
    float freq_late = estimate_freq_autocorr(samples, sample_rate, tail, n);
    if (freq_first <= 0.0f) {
        freq_first = estimate_freq_from_zcr(samples, sample_rate, first, first_end);
    }
    if (freq_late <= 0.0f) {
        freq_late = estimate_freq_from_zcr(samples, sample_rate, tail, n);
    }

#if defined(USE_AUBIO_SNDFILE)
    const float aubio_first = estimate_freq_with_aubio(samples, sample_rate, first, first_end);
    const float aubio_late = estimate_freq_with_aubio(samples, sample_rate, tail, n);
    if (aubio_first > 0.0f) {
        freq_first = aubio_first;
    }
    if (aubio_late > 0.0f) {
        freq_late = aubio_late;
    }
#endif

    const float zcr_all = zero_cross_rate(samples, 0, n);
    const float crest = peak_abs(samples, 0, n) / std::max(0.0001f, rms_all);

    const float tonal_conf = (freq_first > 30.0f ? 1.0f : 0.0f);

    if (zcr_all > 0.24f && tonal_conf < 0.5f) {
        out.wave_type = WaveType::Noise;
    } else if (crest > 3.0f) {
        out.wave_type = WaveType::Square;
    } else if (zcr_all > 0.11f) {
        out.wave_type = WaveType::Sawtooth;
    } else {
        out.wave_type = WaveType::Sine;
    }

    out.base_freq = clampf(std::max(35.0f, freq_first), ranges.base_freq.min, ranges.base_freq.max);

    const float duration_s = static_cast<float>(n) / static_cast<float>(sample_rate);
    const float slide_hz = (freq_late - freq_first) / std::max(0.06f, duration_s * 0.7f);
    out.freq_slide = clampf(slide_hz, ranges.freq_slide.min, ranges.freq_slide.max);
    out.freq_delta_slide = clampf(slide_hz * 0.15f, ranges.freq_delta_slide.min, ranges.freq_delta_slide.max);
    out.freq_limit = clampf(std::min(freq_first, freq_late) * 0.85f, ranges.freq_limit.min, ranges.freq_limit.max);

    out.duty_cycle = 0.5f;
    out.duty_sweep = 0.0f;

    const float zcr_delta = std::fabs(freq_late - freq_first) / std::max(1.0f, freq_first);
    out.vibrato_depth = clampf(zcr_delta * 0.12f, ranges.vibrato_depth.min, ranges.vibrato_depth.max);
    out.vibrato_speed = clampf(4.0f + zcr_all * 28.0f, ranges.vibrato_speed.min, ranges.vibrato_speed.max);

    float attack_guess = 0.01f;
    float sustain_guess = 0.2f;
    float decay_guess = 0.2f;
    estimate_adsr_from_envelope(env, sample_rate, attack_guess, sustain_guess, decay_guess);
    out.attack_time = clampf(attack_guess, ranges.attack_time.min, ranges.attack_time.max);
    out.sustain_time = clampf(sustain_guess, ranges.sustain_time.min, ranges.sustain_time.max);
    out.decay_time = clampf(decay_guess, ranges.decay_time.min, ranges.decay_time.max);

    const float punch = (rms_first - rms_mid) / std::max(0.001f, rms_mid);
    out.sustain_punch = clampf(punch * 0.45f, ranges.sustain_punch.min, ranges.sustain_punch.max);

    const float brightness = clampf(zcr_all * 4.0f, 0.0f, 1.0f);
    out.lp_filter_cutoff = clampf(0.5f + brightness * 0.5f, ranges.lp_filter_cutoff.min, ranges.lp_filter_cutoff.max);
    out.lp_filter_resonance = clampf((crest - 1.2f) * 0.2f, ranges.lp_filter_resonance.min, ranges.lp_filter_resonance.max);
    out.hp_filter_cutoff = clampf((1.0f - rms_tail / std::max(0.0001f, rms_first)) * 0.14f, ranges.hp_filter_cutoff.min, ranges.hp_filter_cutoff.max);

    out.phaser_offset = clampf((freq_late - freq_first) / 1200.0f, ranges.phaser_offset.min, ranges.phaser_offset.max);

    const float onset_ratio = (rms_first + 1e-5f) / (rms_mid + 1e-5f);
    out.noise_mix = clampf((zcr_all - 0.06f) * 2.4f + (onset_ratio - 1.0f) * 0.18f, ranges.noise_mix.min, ranges.noise_mix.max);
    out.tonal_mix = clampf(1.0f - out.noise_mix * 0.45f, ranges.tonal_mix.min, ranges.tonal_mix.max);
    out.noise_attack = clampf(0.001f + (1.0f / std::max(1.0f, freq_first)) * 0.01f, ranges.noise_attack.min, ranges.noise_attack.max);
    out.noise_decay = clampf(0.03f + duration_s * 0.15f, ranges.noise_decay.min, ranges.noise_decay.max);
    out.noise_hp = clampf(0.25f + zcr_all * 0.9f, ranges.noise_hp.min, ranges.noise_hp.max);

    const SfxDef retro_variant = make_retro_variant(out);
    const SfxDef source_variant = make_source_variant(out);

    const float t = clampf(source_match, 0.0f, 1.0f);
    const float shaped_t = std::pow(t, 0.65f);
    const SfxDef blended = blend_sfx(retro_variant, source_variant, shaped_t);

    if (fidelity_boost <= 0.01f) {
        return blended;
    }
    const float duration = static_cast<float>(samples.size()) / static_cast<float>(std::max(1, sample_rate));
    return refine_fit(blended, samples, sample_rate, std::max(0.05f, duration), fidelity_boost);
}
