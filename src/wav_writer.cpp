#include "wav_writer.hpp"

#include <cstdint>
#include <fstream>

namespace {
void write_u32(std::ofstream& out, std::uint32_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void write_u16(std::ofstream& out, std::uint16_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}
}

bool write_wav_file(const std::string& path, const std::vector<float>& samples, int sample_rate) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }

    const std::uint16_t num_channels = 1;
    const std::uint16_t bits_per_sample = 16;
    const std::uint32_t byte_rate = static_cast<std::uint32_t>(sample_rate) * num_channels * bits_per_sample / 8;
    const std::uint16_t block_align = num_channels * bits_per_sample / 8;
    const std::uint32_t data_size = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));

    out.write("RIFF", 4);
    write_u32(out, 36 + data_size);
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    write_u32(out, 16);
    write_u16(out, 1);
    write_u16(out, num_channels);
    write_u32(out, static_cast<std::uint32_t>(sample_rate));
    write_u32(out, byte_rate);
    write_u16(out, block_align);
    write_u16(out, bits_per_sample);

    out.write("data", 4);
    write_u32(out, data_size);

    for (float sample : samples) {
        const float clamped = sample < -1.0f ? -1.0f : (sample > 1.0f ? 1.0f : sample);
        const auto pcm = static_cast<std::int16_t>(clamped * 32767.0f);
        out.write(reinterpret_cast<const char*>(&pcm), sizeof(pcm));
    }

    return out.good();
}
