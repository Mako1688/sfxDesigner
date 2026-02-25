#pragma once

#include <string>
#include <vector>

bool write_wav_file(const std::string& path, const std::vector<float>& samples, int sample_rate);
