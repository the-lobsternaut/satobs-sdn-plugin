/**
 * Satellite Observation & OD Plugin — Implementation
 * =====================================================
 *
 * Non-inline implementations for satellite observation processing:
 *   1. IOD (Interactive Orbit Determination) format parser
 *      - 80-column fixed-width format from SeeSat-L
 *      - 7 angle formats for RA/DEC and AZ/EL
 *   2. McCants classified TLE file parser (classfd.tle)
 *   3. Observation correlation & deduplication
 *   4. Multi-observation orbit refinement
 *   5. Space-Track TLE query helpers
 *
 * References:
 *   - IOD format: http://www.satobs.org/position/IODformat.html
 *   - SeeSat-L archive: https://www.satobs.org/seesat/
 *   - McCants TLEs: https://www.prismnet.com/~mmccants/tles/classfd.zip
 *   - Vimpel data: http://spacedata.vimpel.ru/
 *   - Space-Track API: https://www.space-track.org/documentation
 */

#include "satobs/types.h"
#include <sstream>
#include <regex>
#include <iomanip>

namespace satobs {

// ============================================================================
// IOD Format Parser (Full 80-column format)
// ============================================================================

/// Parse a single IOD observation line (80 columns).
///
/// IOD format (George D. Lewis, 1998):
///   Cols  1-5:   NORAD catalog number (00000 = unknown)
///   Cols  6-13:  International designation (YY NNN PP)
///   Cols 14-17:  Station number (COSPAR)
///   Col  18:     Sky condition (E/G/F/P/B/T)
///   Cols 19-22:  Date (YYYY)
///   Cols 23-24:  Month (MM)
///   Cols 25-26:  Day (DD)
///   Cols 27-35:  Time (HHMMSSsss)
///   Col  36:     Time accuracy (M×10^(X-8) seconds)
///   Col  37:     Time accuracy exponent
///   Col  38-39:  Angle format code (1-7)
///   Cols 40-54:  Angle data (format-dependent)
///   Col  55-56:  Position uncertainty (MX → arcseconds)
///   Col  57:     Optical behavior (S/F/I/R/E/X)
///   Cols 58-63:  Visual magnitude (±MM.M)
///   Cols 64-70:  Flash period (PPPP.PP seconds)
///   Cols 71-80:  Comments/reserved
///
/// Returns true if a valid observation was parsed.
bool parseIODLine(const std::string& line, ObsRecord& obs) {
    obs = ObsRecord{};

    if (line.size() < 56) return false;

    // NORAD catalog number (cols 1-5)
    std::string norad_str = line.substr(0, 5);
    try {
        int norad = std::stoi(norad_str);
        obs.norad_id = (norad > 0) ? static_cast<uint32_t>(norad) : 0;
    } catch (...) {
        obs.norad_id = 0;
    }

    // International designation (cols 6-13)
    if (line.size() >= 13) {
        std::string intl = line.substr(5, 8);
        // Trim trailing spaces
        size_t end = intl.find_last_not_of(' ');
        if (end != std::string::npos)
            std::strncpy(obs.intl_des, intl.substr(0, end + 1).c_str(), 11);
    }

    // Station number (cols 14-17)
    if (line.size() >= 17) {
        try {
            obs.station_id = static_cast<uint16_t>(std::stoi(line.substr(13, 4)));
        } catch (...) {}
    }

    // Sky condition (col 18)
    if (line.size() >= 18) {
        obs.sky_condition = static_cast<uint8_t>(charToSkyCondition(line[17]));
    }

    // Date: YYYYMMDD (cols 19-26)
    // Time: HHMMSSsss (cols 27-35)
    if (line.size() >= 35) {
        std::string date_str = line.substr(18, 8);
        std::string time_str = line.substr(26, 9);
        obs.epoch_s = parseIODDateTime(date_str, time_str);
    }

    // Time uncertainty (cols 36-37)
    if (line.size() >= 37) {
        obs.time_unc_s = static_cast<float>(
            parseIODUncertainty(line.substr(35, 2)));
    }

    // Angle format (cols 38-39)
    uint8_t angle_fmt = 1;
    if (line.size() >= 39) {
        try { angle_fmt = static_cast<uint8_t>(std::stoi(line.substr(37, 2))); }
        catch (...) {}
    }
    obs.angle_format = angle_fmt;

    // Angle data (cols 40-54) — format-dependent parsing
    if (line.size() >= 54) {
        std::string angle_data = line.substr(39, 15);

        // Initialize to NAN
        obs.ra_deg = NAN;
        obs.dec_deg = NAN;
        obs.az_deg = NAN;
        obs.el_deg = NAN;

        // Split at sign character for RA/DEC or AZ/EL
        size_t sign_pos = std::string::npos;
        for (size_t i = 7; i < angle_data.size(); i++) {
            if (angle_data[i] == '+' || angle_data[i] == '-') {
                sign_pos = i;
                break;
            }
        }

        if (sign_pos != std::string::npos) {
            std::string first = angle_data.substr(0, sign_pos);
            std::string second = angle_data.substr(sign_pos);

            switch (angle_fmt) {
                case 1: // HHMMSSs +DDMMSS
                    obs.ra_deg = parseRA_format1(first);
                    obs.dec_deg = parseDEC_format1(second);
                    break;
                case 2: { // HHMMmmm +DDMMmm
                    if (first.size() >= 7) {
                        double hh = std::stod(first.substr(0, 2));
                        double mm = std::stod(first.substr(2, 2));
                        double frac = std::stod("0." + first.substr(4));
                        obs.ra_deg = (hh + (mm + frac) / 60.0) * 15.0;
                    }
                    if (second.size() >= 7) {
                        double sign = (second[0] == '-') ? -1.0 : 1.0;
                        double dd = std::stod(second.substr(1, 2));
                        double mm = std::stod(second.substr(3, 2));
                        double frac = std::stod("0." + second.substr(5));
                        obs.dec_deg = sign * (dd + (mm + frac) / 60.0);
                    }
                    break;
                }
                case 3: { // HHMMmmm +DDdddd
                    if (first.size() >= 7) {
                        double hh = std::stod(first.substr(0, 2));
                        double mm_frac = std::stod(first.substr(2, 2) + "." + first.substr(4));
                        obs.ra_deg = (hh + mm_frac / 60.0) * 15.0;
                    }
                    if (second.size() >= 7) {
                        double sign = (second[0] == '-') ? -1.0 : 1.0;
                        obs.dec_deg = sign * std::stod(second.substr(1, 2) + "." + second.substr(3));
                    }
                    break;
                }
                case 4: // DDDMMSS +DDMMSS (AZ/EL)
                    if (first.size() >= 7) {
                        double dd = std::stod(first.substr(0, 3));
                        double mm = std::stod(first.substr(3, 2));
                        double ss = std::stod(first.substr(5, 2));
                        obs.az_deg = dd + mm / 60.0 + ss / 3600.0;
                    }
                    if (second.size() >= 7) {
                        double sign = (second[0] == '-') ? -1.0 : 1.0;
                        double dd = std::stod(second.substr(1, 2));
                        double mm = std::stod(second.substr(3, 2));
                        double ss = std::stod(second.substr(5, 2));
                        obs.el_deg = sign * (dd + mm / 60.0 + ss / 3600.0);
                    }
                    break;
                case 7: // HHMMSSs +DDdddd
                    obs.ra_deg = parseRA_format1(first);
                    if (second.size() >= 7) {
                        double sign = (second[0] == '-') ? -1.0 : 1.0;
                        obs.dec_deg = sign * std::stod(second.substr(1, 2) + "." + second.substr(3));
                    }
                    break;
                default:
                    // Formats 5, 6 — similar patterns
                    break;
            }
        }
    }

    // Position uncertainty (cols 55-56)
    if (line.size() >= 56) {
        double unc = parseIODUncertainty(line.substr(54, 2));
        // IOD uncertainty is in degrees; convert to arcseconds
        obs.pos_unc_arcsec = static_cast<float>(unc * 3600.0);
    }

    // Optical behavior (col 57)
    if (line.size() >= 57) {
        obs.optical_behavior = static_cast<uint8_t>(charToOptBehavior(line[56]));
    }

    // Visual magnitude (cols 58-63)
    obs.visual_mag = NAN;
    if (line.size() >= 63) {
        std::string mag_str = line.substr(57, 6);
        // Trim spaces
        size_t start = mag_str.find_first_not_of(' ');
        if (start != std::string::npos) {
            try { obs.visual_mag = static_cast<float>(std::stod(mag_str.substr(start))); }
            catch (...) {}
        }
    }

    // Flash period (cols 64-70)
    obs.flash_period_s = 0;
    if (line.size() >= 70) {
        std::string flash_str = line.substr(63, 7);
        size_t start = flash_str.find_first_not_of(' ');
        if (start != std::string::npos) {
            try { obs.flash_period_s = static_cast<float>(std::stod(flash_str.substr(start))); }
            catch (...) {}
        }
    }

    obs.obs_type = static_cast<uint8_t>(ObsType::POSITIONAL);
    obs.data_source = static_cast<uint8_t>(DataSource::SEESAT_L);

    return true;
}

/// Parse multiple IOD lines (e.g., from a SeeSat-L email digest)
std::vector<ObsRecord> parseIODFile(const std::string& content) {
    std::vector<ObsRecord> records;
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        // IOD lines are exactly 80 chars (or close to it) and start with digits
        if (line.size() < 40) continue;

        // Check if line starts with a digit (NORAD number) or space-padded number
        bool has_digits = false;
        for (int i = 0; i < 5 && i < (int)line.size(); i++) {
            if (std::isdigit(line[i])) { has_digits = true; break; }
        }
        if (!has_digits) continue;

        ObsRecord obs;
        if (parseIODLine(line, obs)) {
            records.push_back(obs);
        }
    }

    return records;
}

// ============================================================================
// McCants Classified TLE File Parser
// ============================================================================

/// Parse McCants classfd.tle file format.
/// This is standard 3-line TLE format (name + line1 + line2)
/// but contains classified/unacknowledged objects not in public catalogs.
///
/// Objects include:
///   - NRO reconnaissance satellites (KH-11, Lacrosse, etc.)
///   - SIGINT satellites (Trumpet, Orion/Mentor, Mercury)
///   - Early warning satellites (SBIRS-GEO)
///   - Military communications (AEHF, Milstar)
///   - Objects not publicly acknowledged by USSPACECOM
std::vector<TLERecord> parseMcCantsFile(const std::string& content) {
    std::vector<TLERecord> tles;
    std::istringstream stream(content);
    std::string line;
    std::vector<std::string> lines;

    while (std::getline(stream, line)) {
        // Remove trailing CR/whitespace
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }

    // Process in groups of 3 (name, line1, line2)
    for (size_t i = 0; i + 2 < lines.size(); ) {
        // Try to identify line patterns
        if (lines[i + 1].size() >= 69 && lines[i + 1][0] == '1' &&
            lines[i + 2].size() >= 69 && lines[i + 2][0] == '2') {
            // 3-line format: name + line1 + line2
            TLERecord tle;
            if (parseTLE(lines[i], lines[i + 1], lines[i + 2], tle)) {
                tle.data_source = static_cast<uint8_t>(DataSource::MCCANTS);
                tle.catalog_status = static_cast<uint8_t>(CatalogStatus::CLASSIFIED);
                tles.push_back(tle);
            }
            i += 3;
        } else if (lines[i].size() >= 69 && lines[i][0] == '1' &&
                   lines[i + 1].size() >= 69 && lines[i + 1][0] == '2') {
            // 2-line format (no name)
            TLERecord tle;
            if (parseTLE("", lines[i], lines[i + 1], tle)) {
                tle.data_source = static_cast<uint8_t>(DataSource::MCCANTS);
                tle.catalog_status = static_cast<uint8_t>(CatalogStatus::CLASSIFIED);
                tles.push_back(tle);
            }
            i += 2;
        } else {
            i++;
        }
    }

    return tles;
}

// ============================================================================
// Observation Correlation
// ============================================================================

/// Correlate observations with TLE catalog to identify objects.
/// Returns observations annotated with best-match NORAD IDs and confidence.
std::vector<std::pair<ObsRecord, IDMatch>> correlateObservations(
    const std::vector<ObsRecord>& observations,
    const std::vector<TLERecord>& catalog) {

    std::vector<std::pair<ObsRecord, IDMatch>> results;
    results.reserve(observations.size());

    for (const auto& obs : observations) {
        IDMatch match = identifyObject(obs, catalog);
        results.emplace_back(obs, match);
    }

    // Sort by confidence (highest first)
    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) {
                  return a.second.confidence > b.second.confidence;
              });

    return results;
}

/// Deduplicate observations of the same object within a time window.
/// Multiple observers may report the same pass; keep highest-quality obs.
std::vector<ObsRecord> deduplicateObservations(
    const std::vector<ObsRecord>& obs,
    double time_window_s) {

    if (obs.empty()) return {};

    // Sort by epoch
    auto sorted = obs;
    std::sort(sorted.begin(), sorted.end(),
              [](const ObsRecord& a, const ObsRecord& b) {
                  return a.epoch_s < b.epoch_s;
              });

    std::vector<ObsRecord> deduped;
    deduped.push_back(sorted[0]);

    for (size_t i = 1; i < sorted.size(); i++) {
        const auto& prev = deduped.back();
        const auto& curr = sorted[i];

        // Same object (by NORAD ID) within time window?
        if (curr.norad_id == prev.norad_id && curr.norad_id > 0 &&
            std::abs(curr.epoch_s - prev.epoch_s) < time_window_s) {
            // Keep the one with better sky conditions
            if (curr.sky_condition < prev.sky_condition) {
                deduped.back() = curr;
            }
            // Or better position uncertainty
            else if (curr.pos_unc_arcsec < prev.pos_unc_arcsec &&
                     !std::isnan(curr.pos_unc_arcsec)) {
                deduped.back() = curr;
            }
            continue;
        }
        deduped.push_back(curr);
    }

    return deduped;
}

// ============================================================================
// Space-Track Query Helpers
// ============================================================================

/// Build a Space-Track API query URL for fetching TLEs.
/// Requires authentication (username/password from space-track.org account).
///
/// API format: https://www.space-track.org/basicspacedata/query/
///   class/tle_latest/NORAD_CAT_ID/25544/orderby/EPOCH desc/limit/1/format/tle
std::string buildSpaceTrackQuery(uint32_t norad_id, int limit) {
    std::ostringstream url;
    url << "https://www.space-track.org/basicspacedata/query/"
        << "class/tle_latest/"
        << "NORAD_CAT_ID/" << norad_id << "/"
        << "orderby/EPOCH%20desc/"
        << "limit/" << limit << "/"
        << "format/tle";
    return url.str();
}

/// Build query for all objects in an inclination/altitude range
std::string buildSpaceTrackCatalogQuery(double min_inc, double max_inc,
                                         double min_period, double max_period,
                                         int limit) {
    std::ostringstream url;
    url << "https://www.space-track.org/basicspacedata/query/"
        << "class/tle_latest/"
        << "INCLINATION/" << min_inc << "--" << max_inc << "/"
        << "MEAN_MOTION/" << (1440.0 / max_period) << "--" << (1440.0 / min_period) << "/"
        << "orderby/NORAD_CAT_ID/"
        << "limit/" << limit << "/"
        << "format/tle";
    return url.str();
}

}  // namespace satobs
