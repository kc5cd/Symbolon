#include "config.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

namespace {

std::string trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

void copy_bounded(char* dest, size_t dest_size, const std::string& src)
{
    std::strncpy(dest, src.c_str(), dest_size - 1);
    dest[dest_size - 1] = '\0';
}

// Local accumulator for [gates] keys -- freq_min_hz/freq_max_hz only become an active gate
// once *both* are seen (a lone freq_min_hz with no freq_max_hz would otherwise gate against
// an unset, defaulted-to-0 upper bound and reject every real decode).
struct GateAccumulator {
    std::optional<float> freq_min_hz;
    std::optional<float> freq_max_hz;
    std::optional<float> min_snr_db;
    std::optional<std::string> band;
};

void apply_gates(const GateAccumulator& gates, cerberus_config_t& cerberus)
{
    if (gates.band.has_value()) {
        cerberus.has_band_gate = true;
        copy_bounded(cerberus.band, sizeof(cerberus.band), *gates.band);
    }
    if (gates.freq_min_hz.has_value() && gates.freq_max_hz.has_value()) {
        cerberus.has_freq_gate = true;
        cerberus.freq_min_hz = *gates.freq_min_hz;
        cerberus.freq_max_hz = *gates.freq_max_hz;
    }
    if (gates.min_snr_db.has_value()) {
        cerberus.has_snr_gate = true;
        cerberus.min_snr_db = *gates.min_snr_db;
    }
}

} // namespace

bool load_config_file(const std::string& path, SymbolonConfig& out)
{
    std::ifstream in(path);
    if (!in) {
        return false;
    }

    GateAccumulator gates;
    std::string section;
    std::string line;
    int line_no = 0;

    while (std::getline(in, line)) {
        ++line_no;
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = trim(trimmed.substr(1, trimmed.size() - 2));
            continue;
        }

        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            std::cerr << path << ":" << line_no << ": expected 'key = value', skipping: " << trimmed << "\n";
            continue;
        }
        std::string key = trim(trimmed.substr(0, eq));
        std::string value = trim(trimmed.substr(eq + 1));
        if (key.empty() || value.empty()) {
            std::cerr << path << ":" << line_no << ": empty key or value, skipping: " << trimmed << "\n";
            continue;
        }

        if (section == "station") {
            if (key == "call") {
                copy_bounded(out.cerberus.my_call, sizeof(out.cerberus.my_call), value);
                copy_bounded(out.qso.my_call, sizeof(out.qso.my_call), value);
            } else if (key == "grid") {
                copy_bounded(out.qso.my_grid, sizeof(out.qso.my_grid), value);
            } else {
                std::cerr << path << ":" << line_no << ": unknown [station] key \"" << key << "\", skipping\n";
            }
        } else if (section == "whitelist") {
            if (key == "calls") {
                std::stringstream ss(value);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    std::string call = trim(item);
                    if (call.empty()) {
                        continue;
                    }
                    if (out.cerberus.whitelist_count >= CERBERUS_MAX_WHITELIST) {
                        std::cerr << path << ":" << line_no << ": whitelist full (max " << CERBERUS_MAX_WHITELIST
                                  << "), dropping \"" << call << "\"\n";
                        continue;
                    }
                    copy_bounded(out.cerberus.whitelist[out.cerberus.whitelist_count],
                        sizeof(out.cerberus.whitelist[0]), call);
                    ++out.cerberus.whitelist_count;
                }
            } else {
                std::cerr << path << ":" << line_no << ": unknown [whitelist] key \"" << key << "\", skipping\n";
            }
        } else if (section == "gates") {
            try {
                if (key == "band") {
                    gates.band = value;
                } else if (key == "freq_min_hz") {
                    gates.freq_min_hz = std::stof(value);
                } else if (key == "freq_max_hz") {
                    gates.freq_max_hz = std::stof(value);
                } else if (key == "min_snr_db") {
                    gates.min_snr_db = std::stof(value);
                } else {
                    std::cerr << path << ":" << line_no << ": unknown [gates] key \"" << key << "\", skipping\n";
                }
            } catch (const std::exception&) {
                std::cerr << path << ":" << line_no << ": couldn't parse \"" << value << "\" as a number, skipping\n";
            }
        } else {
            std::cerr << path << ":" << line_no << ": key outside any known [section], skipping: " << trimmed << "\n";
        }
    }

    apply_gates(gates, out.cerberus);
    return true;
}

bool load_beacon_token_file(const std::string& path, cerberus_config_t& out_cerberus)
{
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string token = trim(buf.str());
    copy_bounded(out_cerberus.beacon_token, sizeof(out_cerberus.beacon_token), token);
    return true;
}
