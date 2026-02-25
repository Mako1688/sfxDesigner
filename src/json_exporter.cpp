#include "json_exporter.hpp"

#include <fstream>

bool write_sfx_json(const std::string& path, const std::string& name, const SfxDef& sfx) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }

    out << "{\n";
    out << "  \"name\": \"" << name << "\",\n";
    out << "  \"wave_type\": \"" << wave_type_to_string(sfx.wave_type) << "\",\n";
    out << "  \"base_freq\": " << sfx.base_freq << ",\n";
    out << "  \"freq_limit\": " << sfx.freq_limit << ",\n";
    out << "  \"freq_slide\": " << sfx.freq_slide << ",\n";
    out << "  \"freq_delta_slide\": " << sfx.freq_delta_slide << ",\n";
    out << "  \"duty_cycle\": " << sfx.duty_cycle << ",\n";
    out << "  \"duty_sweep\": " << sfx.duty_sweep << ",\n";
    out << "  \"vibrato_depth\": " << sfx.vibrato_depth << ",\n";
    out << "  \"vibrato_speed\": " << sfx.vibrato_speed << ",\n";
    out << "  \"attack_time\": " << sfx.attack_time << ",\n";
    out << "  \"sustain_time\": " << sfx.sustain_time << ",\n";
    out << "  \"decay_time\": " << sfx.decay_time << ",\n";
    out << "  \"sustain_punch\": " << sfx.sustain_punch << ",\n";
    out << "  \"lp_filter_cutoff\": " << sfx.lp_filter_cutoff << ",\n";
    out << "  \"lp_filter_resonance\": " << sfx.lp_filter_resonance << ",\n";
    out << "  \"hp_filter_cutoff\": " << sfx.hp_filter_cutoff << ",\n";
    out << "  \"phaser_offset\": " << sfx.phaser_offset << "\n";
    out << "}\n";

    return out.good();
}
