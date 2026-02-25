#include "wav_reader.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace {
template <typename T>
bool read_value(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return in.good();
}
}

bool read_wav_file(const std::string& path, WavData& out_data, std::string& error_message) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error_message = "Could not open file";
        return false;
    }

    char riff[4] = {};
    char wave[4] = {};
    std::uint32_t riff_size = 0;

    in.read(riff, 4);
    if (!in.good() || std::string(riff, 4) != "RIFF") {
        error_message = "Not a RIFF file";
        return false;
    }
    if (!read_value(in, riff_size)) {
        error_message = "Invalid RIFF header";
        return false;
    }
    in.read(wave, 4);
    if (!in.good() || std::string(wave, 4) != "WAVE") {
        error_message = "Not a WAVE file";
        return false;
    }

    bool has_fmt = false;
    bool has_data = false;
    std::uint16_t audio_format = 0;
    std::uint16_t num_channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t bits_per_sample = 0;
    std::vector<std::uint8_t> raw_data;

    while (in.good() && !has_data) {
        char chunk_id[4] = {};
        std::uint32_t chunk_size = 0;
        in.read(chunk_id, 4);
        if (!in.good()) {
            break;
        }
        if (!read_value(in, chunk_size)) {
            break;
        }

        const std::string id(chunk_id, 4);
        if (id == "fmt ") {
            has_fmt = true;
            std::uint32_t byte_rate = 0;
            std::uint16_t block_align = 0;
            if (!read_value(in, audio_format) ||
                !read_value(in, num_channels) ||
                !read_value(in, sample_rate) ||
                !read_value(in, byte_rate) ||
                !read_value(in, block_align) ||
                !read_value(in, bits_per_sample)) {
                error_message = "Invalid fmt chunk";
                return false;
            }
            const std::uint32_t remaining = chunk_size > 16 ? (chunk_size - 16) : 0;
            if (remaining > 0) {
                in.seekg(static_cast<std::streamoff>(remaining), std::ios::cur);
            }
        } else if (id == "data") {
            raw_data.resize(chunk_size);
            in.read(reinterpret_cast<char*>(raw_data.data()), static_cast<std::streamsize>(chunk_size));
            if (!in.good()) {
                error_message = "Invalid data chunk";
                return false;
            }
            has_data = true;
        } else {
            in.seekg(static_cast<std::streamoff>(chunk_size), std::ios::cur);
        }

        if (chunk_size % 2 != 0) {
            in.seekg(1, std::ios::cur);
        }
    }

    if (!has_fmt || !has_data) {
        error_message = "Missing fmt/data chunks";
        return false;
    }
    if (audio_format != 1) {
        error_message = "Only PCM WAV is supported";
        return false;
    }
    if (num_channels == 0 || sample_rate == 0) {
        error_message = "Invalid WAV format";
        return false;
    }
    if (bits_per_sample != 8 && bits_per_sample != 16) {
        error_message = "Only 8-bit/16-bit PCM WAV is supported";
        return false;
    }

    const int bytes_per_sample = bits_per_sample / 8;
    const int frame_size = bytes_per_sample * static_cast<int>(num_channels);
    if (frame_size <= 0 || raw_data.size() < static_cast<size_t>(frame_size)) {
        error_message = "Invalid WAV frame data";
        return false;
    }
    const size_t frame_count = raw_data.size() / static_cast<size_t>(frame_size);

    out_data.sample_rate = static_cast<int>(sample_rate);
    out_data.samples.assign(frame_count, 0.0f);

    for (size_t frame = 0; frame < frame_count; ++frame) {
        float mixed = 0.0f;
        for (std::uint16_t ch = 0; ch < num_channels; ++ch) {
            const size_t offset = frame * static_cast<size_t>(frame_size) + static_cast<size_t>(ch * bytes_per_sample);
            float sample = 0.0f;
            if (bits_per_sample == 8) {
                const std::uint8_t v = raw_data[offset];
                sample = (static_cast<float>(v) - 128.0f) / 128.0f;
            } else {
                std::int16_t v = 0;
                v = static_cast<std::int16_t>(raw_data[offset] | (static_cast<std::uint16_t>(raw_data[offset + 1]) << 8));
                sample = static_cast<float>(v) / 32768.0f;
            }
            mixed += sample;
        }
        out_data.samples[frame] = std::clamp(mixed / static_cast<float>(num_channels), -1.0f, 1.0f);
    }

    return true;
}
