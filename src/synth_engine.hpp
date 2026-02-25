#pragma once

#include "sfx_def.hpp"

#include <vector>

struct RenderDebugData {
    std::vector<float> envelope;
    std::vector<float> frequency;
    std::vector<float> waveform;
};

std::vector<float> render_samples(const SfxDef& sfx, int sample_rate, float duration_seconds, RenderDebugData* debug = nullptr);
