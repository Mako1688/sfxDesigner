#pragma once

#include "sfx_def.hpp"

#include <vector>

SfxDef fit_sfx_from_samples(const std::vector<float>& samples, int sample_rate);
