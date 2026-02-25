#include "audio_reader.hpp"

#include <algorithm>
#include <vector>

#if defined(USE_AUBIO_SNDFILE)
#include <sndfile.h>
#endif

bool read_audio_file_best_effort(const std::string& path, WavData& out_data, std::string& error_message) {
#if defined(USE_AUBIO_SNDFILE)
    SF_INFO sfinfo{};
    SNDFILE* snd = sf_open(path.c_str(), SFM_READ, &sfinfo);
    if (snd != nullptr) {
        if (sfinfo.frames <= 0 || sfinfo.channels <= 0 || sfinfo.samplerate <= 0) {
            sf_close(snd);
            error_message = "Unsupported audio stream";
            return false;
        }

        std::vector<float> interleaved(static_cast<size_t>(sfinfo.frames) * static_cast<size_t>(sfinfo.channels), 0.0f);
        const sf_count_t read_count = sf_readf_float(snd, interleaved.data(), sfinfo.frames);
        sf_close(snd);
        if (read_count <= 0) {
            error_message = "Failed reading audio samples";
            return false;
        }

        out_data.sample_rate = sfinfo.samplerate;
        out_data.samples.assign(static_cast<size_t>(read_count), 0.0f);

        for (sf_count_t frame = 0; frame < read_count; ++frame) {
            float mixed = 0.0f;
            const size_t base = static_cast<size_t>(frame) * static_cast<size_t>(sfinfo.channels);
            for (int ch = 0; ch < sfinfo.channels; ++ch) {
                mixed += interleaved[base + static_cast<size_t>(ch)];
            }
            out_data.samples[static_cast<size_t>(frame)] = std::clamp(mixed / static_cast<float>(sfinfo.channels), -1.0f, 1.0f);
        }

        return true;
    }
#endif

    return read_wav_file(path, out_data, error_message);
}
