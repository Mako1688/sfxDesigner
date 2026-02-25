#pragma once

#include "wav_reader.hpp"

#include <string>

bool read_audio_file_best_effort(const std::string& path, WavData& out_data, std::string& error_message);
