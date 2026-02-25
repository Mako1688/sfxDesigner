#pragma once

#include <string>
#include <vector>

struct WavData {
    int sample_rate = 0;
    std::vector<float> samples;
};

bool read_wav_file(const std::string& path, WavData& out_data, std::string& error_message);
