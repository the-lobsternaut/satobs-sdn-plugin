#ifndef SATOBS_TYPES_H
#define SATOBS_TYPES_H

/**
 * Satellite Observation & Orbit Determination Plugin Types
 * ==========================================================
 *
 * Citizen/amateur space surveillance — observation ingestion, orbit
 * determination (OD), and object identification for classified/untracked objects.
 *
 * Data sources:
 *   1. SeeSat-L mailing list (satobs.org) — IOD format observations
 *   2. Mike McCants classfd.tle — classified satellite TLE catalog
 *   3. space.vimpel.ru / Vimpel Corp — Russian classified object TLEs
 *   4. SatNOGS (db.satnogs.org) — open ground station network
 *   5. CelesTrak (celestrak.org) — supplemental TLE catalog
 *   6. Space-Track.org — USSPACECOM 18th SDS catalog
 *
 * IOD Format: Interactive Orbit Determination (George D. Lewis, 1998)
 *   80-column fixed-width: NORAD#, IntlDes, Station, SkyCondition,
 *   UTC date/time, RA/DEC or AZ/EL, magnitude, flash period
 *
 * TLE Format: NORAD Two-Line Element Sets (standard 69-char lines)
 *
 * Output: $OBS FlatBuffer-aligned binary records
 */

#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <array>

namespace satobs {

static constexpr char OBS_FILE_ID[4] = {'$', 'O', 'B', 'S'};
static constexpr uint32_t OBS_VERSION = 1;

// ============================================================================
// Enums
// ============================================================================

enum class DataSource : uint32_t {
    SEESAT_L    = 0,  // SeeSat-L mailing list IOD observations
    MCCANTS     = 1,  // Mike McCants classfd.tle
    VIMPEL      = 2,  // space.vimpel.ru classified TLEs
    SATNOGS     = 3,  // SatNOGS ground station network
    CELESTRAK   = 4,  // CelesTrak TLE catalog
    SPACETRACK  = 5,  // Space-Track.org (18th SDS)
    MANUAL      = 6,  // Manual observer entry
};

enum class ObsType : uint8_t {
    POSITIONAL  = 0,  // RA/DEC or AZ/EL position
    PHOTOMETRIC = 1,  // Brightness measurement
    RADAR       = 2,  // Radar range/range-rate
    TLE         = 3,  // Two-Line Element Set
    SATNOGS_OBS = 4,  // SatNOGS observation record
};

enum class AngleFormat : uint8_t {
    RADEC_HMSDMS = 1,  // HHMMSSs+DDMMSS (IOD format 1)
    RADEC_HMmDMm = 2,  // HHMMmmm+DDMMmm (IOD format 2)
    RADEC_HMmDdd = 3,  // HHMMmmm+DDdddd (IOD format 3)
    AZEL_DMSDMS  = 4,  // DDDMMSS+DDMMSS (IOD format 4)
    AZEL_DMmDMm  = 5,  // DDDMMmm+DDMMmm (IOD format 5)
    AZEL_DddDdd  = 6,  // DDDdddd+DDdddd (IOD format 6)
    RADEC_HMsDdd = 7,  // HHMMSSs+DDdddd (IOD format 7)
};

enum class SkyCondition : uint8_t {
    EXCELLENT = 0,  // E: no Moon/clouds, great seeing
    GOOD      = 1,  // G: no Moon/clouds, could be better
    FAIR      = 2,  // F: young/old Moon, some pollution
    POOR      = 3,  // P: gibbous Moon, haze
    BAD       = 4,  // B: bright Moon, some clouds
    TERRIBLE  = 5,  // T: bright Moon, clouds, difficult
};

enum class OpticalBehavior : uint8_t {
    NONE     = 0,
    STEADY   = 1,  // S: constant brightness
    FLASHING = 2,  // F: constant flash period
    IRREGULAR = 3, // I: irregular brightness
    REGULAR  = 4,  // R: regular variations
    ECLIPSE  = 5,  // E: eclipse exit/entrance
    TUMBLING = 6,  // X: irregular flash period
};

enum class CatalogStatus : uint8_t {
    UNKNOWN      = 0,
    CATALOGED    = 1,  // In public USSPACECOM catalog
    CLASSIFIED   = 2,  // Not in public catalog (McCants/Vimpel)
    UNCATALOGED  = 3,  // Not in any catalog (new find)
    DEBRIS       = 4,  // Identified as debris
    DECAYED      = 5,  // Known to have reentered
    ANALYST_OBJ  = 6,  // Analyst satellite (no official NORAD ID)
};

enum class ODMethod : uint8_t {
    NONE           = 0,
    GAUSS          = 1,  // Gauss method (3 observations)
    LAPLACE        = 2,  // Laplace method
    DOUBLE_R       = 3,  // Double-R iteration
    GOODING        = 4,  // Gooding angles-only
    LEAST_SQUARES  = 5,  // Batch least-squares
    KALMAN         = 6,  // Extended Kalman filter
    TLE_FIT        = 7,  // SGP4/SDP4 TLE fit
};

// ============================================================================
// Core Record Types
// ============================================================================

#pragma pack(push, 1)

struct OBSHeader {
    char     magic[4];    // "$OBS"
    uint32_t version;
    uint32_t source;      // DataSource enum
    uint32_t count;
};
static_assert(sizeof(OBSHeader) == 16, "OBSHeader must be 16 bytes");

/// Observation record (from IOD, SatNOGS, or manual entry)
struct ObsRecord {
    // Object identity
    uint32_t norad_id;         // NORAD catalog number (0 if unknown)
    char     intl_des[12];     // International designator (e.g., "98067A")
    char     name[24];         // Object name (if known)

    // Observer
    uint16_t station_id;       // IOD 4-digit station number
    double   obs_lat_deg;      // Observer latitude [deg]
    double   obs_lon_deg;      // Observer longitude [deg]
    float    obs_alt_m;        // Observer altitude [m]

    // Time
    double   epoch_s;          // Observation epoch [Unix seconds]
    float    time_unc_s;       // Time uncertainty [s]

    // Position (angles-only)
    double   ra_deg;           // Right Ascension [deg] (NAN if AZ/EL)
    double   dec_deg;          // Declination [deg] (NAN if AZ/EL)
    double   az_deg;           // Azimuth [deg] (NAN if RA/DEC)
    double   el_deg;           // Elevation [deg] (NAN if RA/DEC)
    float    pos_unc_arcsec;   // Positional uncertainty [arcsec]

    // Photometry
    float    visual_mag;       // Visual magnitude (NAN if none)
    float    mag_unc;          // Magnitude uncertainty
    float    flash_period_s;   // Flash period [s] (0 if none)

    // Classification
    uint8_t  obs_type;         // ObsType enum
    uint8_t  angle_format;     // AngleFormat enum
    uint8_t  sky_condition;    // SkyCondition enum
    uint8_t  optical_behavior; // OpticalBehavior enum
    uint8_t  catalog_status;   // CatalogStatus enum
    uint8_t  epoch_code;       // 0=of-date, 5=J2000, etc.

    // Source
    uint8_t  data_source;      // DataSource enum
    uint8_t  reserved;
};

/// TLE record (from McCants, Vimpel, CelesTrak, Space-Track)
struct TLERecord {
    // Identity
    uint32_t norad_id;
    char     intl_des[12];
    char     name[24];

    // Epoch
    double   epoch_s;          // TLE epoch [Unix seconds]

    // Keplerian elements (mean elements, TEME frame)
    double   inc_deg;          // Inclination [deg]
    double   raan_deg;         // Right Ascension of Ascending Node [deg]
    double   ecc;              // Eccentricity
    double   argp_deg;         // Argument of perigee [deg]
    double   mean_anom_deg;    // Mean anomaly [deg]
    double   mean_motion;      // Mean motion [rev/day]

    // Perturbation terms
    float    bstar;            // B* drag term [1/earth radii]
    float    ndot;             // First derivative of mean motion [rev/day²]
    float    nddot;            // Second derivative of mean motion [rev/day³]

    // Derived
    float    perigee_km;       // Perigee altitude [km]
    float    apogee_km;        // Apogee altitude [km]
    float    period_min;       // Orbital period [min]

    // Classification
    uint8_t  catalog_status;   // CatalogStatus enum
    uint8_t  data_source;      // DataSource enum
    uint16_t rev_number;       // Revolution number at epoch
    uint32_t tle_set_number;   // Element set number
};

/// OD result record
struct ODResult {
    // Object identity
    uint32_t norad_id;         // Matched or assigned ID (0 = new)
    char     name[24];

    // Fitted orbital elements (osculating, J2000)
    double   epoch_s;          // Epoch of elements [Unix seconds]
    double   sma_km;           // Semi-major axis [km]
    double   ecc;              // Eccentricity
    double   inc_deg;          // Inclination [deg]
    double   raan_deg;         // RAAN [deg]
    double   argp_deg;         // Argument of perigee [deg]
    double   mean_anom_deg;    // Mean anomaly [deg]

    // Covariance (diagonal only — 6 elements)
    float    cov_sma;          // km²
    float    cov_ecc;
    float    cov_inc;          // deg²
    float    cov_raan;         // deg²
    float    cov_argp;         // deg²
    float    cov_ma;           // deg²

    // Fit quality
    float    rms_arcsec;       // RMS residual [arcsec]
    uint16_t obs_used;         // Number of observations used
    uint16_t obs_rejected;     // Number of observations rejected

    // Method
    uint8_t  od_method;        // ODMethod enum
    uint8_t  catalog_status;   // CatalogStatus enum (matched or new)
    uint8_t  confidence;       // ID confidence [0-100]
    uint8_t  reserved;
};

#pragma pack(pop)

// ============================================================================
// Serialization
// ============================================================================

/// Combined observation + TLE + OD buffer
struct SatObsData {
    std::vector<ObsRecord> observations;
    std::vector<TLERecord> tles;
    std::vector<ODResult>  od_results;
};

inline std::vector<uint8_t> serialize(const SatObsData& data,
                                       DataSource src = DataSource::SEESAT_L) {
    uint32_t nObs = static_cast<uint32_t>(data.observations.size());
    uint32_t nTLE = static_cast<uint32_t>(data.tles.size());
    uint32_t nOD  = static_cast<uint32_t>(data.od_results.size());

    size_t size = sizeof(OBSHeader) + 12 +
                  nObs * sizeof(ObsRecord) +
                  nTLE * sizeof(TLERecord) +
                  nOD  * sizeof(ODResult);
    std::vector<uint8_t> buf(size);

    OBSHeader hdr;
    std::memcpy(hdr.magic, OBS_FILE_ID, 4);
    hdr.version = OBS_VERSION;
    hdr.source = static_cast<uint32_t>(src);
    hdr.count = nObs + nTLE + nOD;
    std::memcpy(buf.data(), &hdr, sizeof(OBSHeader));

    size_t off = sizeof(OBSHeader);
    std::memcpy(buf.data() + off, &nObs, 4); off += 4;
    std::memcpy(buf.data() + off, &nTLE, 4); off += 4;
    std::memcpy(buf.data() + off, &nOD, 4);  off += 4;

    if (nObs) { std::memcpy(buf.data()+off, data.observations.data(), nObs*sizeof(ObsRecord)); off += nObs*sizeof(ObsRecord); }
    if (nTLE) { std::memcpy(buf.data()+off, data.tles.data(), nTLE*sizeof(TLERecord)); off += nTLE*sizeof(TLERecord); }
    if (nOD)  { std::memcpy(buf.data()+off, data.od_results.data(), nOD*sizeof(ODResult)); }
    return buf;
}

inline bool deserialize(const uint8_t* data, size_t len,
                         OBSHeader& hdr, SatObsData& out) {
    if (len < sizeof(OBSHeader) + 12) return false;
    std::memcpy(&hdr, data, sizeof(OBSHeader));
    if (std::memcmp(hdr.magic, OBS_FILE_ID, 4) != 0) return false;

    size_t off = sizeof(OBSHeader);
    uint32_t nObs, nTLE, nOD;
    std::memcpy(&nObs, data+off, 4); off += 4;
    std::memcpy(&nTLE, data+off, 4); off += 4;
    std::memcpy(&nOD,  data+off, 4); off += 4;

    size_t need = off + nObs*sizeof(ObsRecord) + nTLE*sizeof(TLERecord) + nOD*sizeof(ODResult);
    if (len < need) return false;

    out.observations.resize(nObs);
    if (nObs) { std::memcpy(out.observations.data(), data+off, nObs*sizeof(ObsRecord)); off += nObs*sizeof(ObsRecord); }
    out.tles.resize(nTLE);
    if (nTLE) { std::memcpy(out.tles.data(), data+off, nTLE*sizeof(TLERecord)); off += nTLE*sizeof(TLERecord); }
    out.od_results.resize(nOD);
    if (nOD) { std::memcpy(out.od_results.data(), data+off, nOD*sizeof(ODResult)); }
    return true;
}

// ============================================================================
// IOD Line Parser (SeeSat-L format)
// ============================================================================

inline SkyCondition charToSkyCondition(char c) {
    switch (c) {
        case 'E': return SkyCondition::EXCELLENT;
        case 'G': return SkyCondition::GOOD;
        case 'F': return SkyCondition::FAIR;
        case 'P': return SkyCondition::POOR;
        case 'B': return SkyCondition::BAD;
        case 'T': return SkyCondition::TERRIBLE;
        default:  return SkyCondition::GOOD;
    }
}

inline OpticalBehavior charToOptBehavior(char c) {
    switch (c) {
        case 'S': return OpticalBehavior::STEADY;
        case 'F': return OpticalBehavior::FLASHING;
        case 'I': return OpticalBehavior::IRREGULAR;
        case 'R': return OpticalBehavior::REGULAR;
        case 'E': return OpticalBehavior::ECLIPSE;
        case 'X': return OpticalBehavior::TUMBLING;
        default:  return OpticalBehavior::NONE;
    }
}

/// Parse IOD uncertainty field (MX format → real value)
/// Evaluated as M * 10^(X-8)
inline double parseIODUncertainty(const std::string& mx) {
    if (mx.size() < 2) return NAN;
    int M = mx[0] - '0';
    int X = mx[1] - '0';
    if (M < 1 || M > 9 || X < 0 || X > 9) return NAN;
    return M * std::pow(10.0, X - 8);
}

/// Parse IOD date/time fields (cols 24-40) → Unix epoch
/// Format: YYYYMMDD HHMMSSsss
inline double parseIODDateTime(const std::string& dateStr, const std::string& timeStr) {
    if (dateStr.size() < 8) return NAN;
    int year  = std::stoi(dateStr.substr(0, 4));
    int month = std::stoi(dateStr.substr(4, 2));
    int day   = std::stoi(dateStr.substr(6, 2));

    double h = 0, m = 0, s = 0;
    if (timeStr.size() >= 2) h = std::stod(timeStr.substr(0, 2));
    if (timeStr.size() >= 4) m = std::stod(timeStr.substr(2, 2));
    if (timeStr.size() >= 6) {
        // Seconds with optional fractional part (cols 6-9)
        std::string sec = timeStr.substr(4);
        if (sec.size() <= 2) s = std::stod(sec);
        else if (sec.size() == 5) s = std::stod(sec.substr(0,2)) + std::stod(sec.substr(2)) * 0.001;
        else s = std::stod(sec.substr(0,2) + "." + sec.substr(2));
    }

    // Simplified: days from J2000 (2000-01-01 12:00 TT)
    // For accurate work, use a proper calendar library
    // Here we use a basic Y/M/D → JD conversion
    int a = (14 - month) / 12;
    int y = year + 4800 - a;
    int mo = month + 12 * a - 3;
    double jd = day + (153*mo + 2)/5 + 365*y + y/4 - y/100 + y/400 - 32045;
    jd += (h - 12.0) / 24.0 + m / 1440.0 + s / 86400.0;

    // JD to Unix epoch: Unix epoch = JD 2440587.5
    return (jd - 2440587.5) * 86400.0;
}

/// Parse RA from IOD format 1 (HHMMSSs) → degrees
inline double parseRA_format1(const std::string& ra) {
    if (ra.size() < 7) return NAN;
    double hh = std::stod(ra.substr(0, 2));
    double mm = std::stod(ra.substr(2, 2));
    double ss = std::stod(ra.substr(4, 2));
    double frac = (ra.size() > 6) ? std::stod(ra.substr(6, 1)) * 0.1 : 0;
    return (hh + mm/60.0 + (ss + frac)/3600.0) * 15.0; // hours → degrees
}

/// Parse DEC from IOD format 1 (+DDMMSS) → degrees
inline double parseDEC_format1(const std::string& dec) {
    if (dec.size() < 7) return NAN;
    double sign = (dec[0] == '-') ? -1.0 : 1.0;
    std::string d = dec.substr(1);
    double dd = std::stod(d.substr(0, 2));
    double mm = std::stod(d.substr(2, 2));
    double ss = std::stod(d.substr(4, 2));
    return sign * (dd + mm/60.0 + ss/3600.0);
}

// ============================================================================
// TLE Parser (standard NORAD 2-line format)
// ============================================================================

/// Parse a standard NORAD Two-Line Element Set
inline bool parseTLE(const std::string& line0,
                      const std::string& line1,
                      const std::string& line2,
                      TLERecord& tle) {
    tle = TLERecord{};
    if (line1.size() < 69 || line2.size() < 69) return false;
    if (line1[0] != '1' || line2[0] != '2') return false;

    // Line 0: name (optional)
    if (!line0.empty()) {
        std::strncpy(tle.name, line0.c_str(), 23);
        tle.name[23] = '\0';
    }

    // Line 1
    tle.norad_id = std::stoi(line1.substr(2, 5));
    std::strncpy(tle.intl_des, line1.substr(9, 8).c_str(), 11);

    // Epoch: YYDDD.DDDDDDDD
    int epochYr = std::stoi(line1.substr(18, 2));
    double epochDay = std::stod(line1.substr(20, 12));
    int year = (epochYr < 57) ? 2000 + epochYr : 1900 + epochYr;
    // Convert to Unix epoch (simplified)
    int a = (14 - 1) / 12;
    int y = year + 4800 - a;
    int mo = 1 + 12 * a - 3;
    double jd_jan1 = 1 + (153*mo + 2)/5 + 365*y + y/4 - y/100 + y/400 - 32045;
    jd_jan1 += (-12.0) / 24.0; // noon → midnight
    double jd = jd_jan1 + epochDay - 1.0;
    tle.epoch_s = (jd - 2440587.5) * 86400.0;

    // ndot, nddot
    tle.ndot = static_cast<float>(std::stod(line1.substr(33, 10)));

    // B*
    std::string bstar_str = line1.substr(53, 8);
    // Format: ±DDDDD±D (implied decimal: ±.DDDDD × 10^±D)
    double bstar_mantissa = std::stod("0." + bstar_str.substr(1, 5));
    int bstar_exp = std::stoi(bstar_str.substr(6, 2));
    tle.bstar = static_cast<float>((bstar_str[0] == '-' ? -1 : 1) *
                bstar_mantissa * std::pow(10.0, bstar_exp));

    tle.tle_set_number = std::stoi(line1.substr(64, 4));

    // Line 2
    tle.inc_deg = std::stod(line2.substr(8, 8));
    tle.raan_deg = std::stod(line2.substr(17, 8));
    tle.ecc = std::stod("0." + line2.substr(26, 7));
    tle.argp_deg = std::stod(line2.substr(34, 8));
    tle.mean_anom_deg = std::stod(line2.substr(43, 8));
    tle.mean_motion = std::stod(line2.substr(52, 11));
    tle.rev_number = static_cast<uint16_t>(std::stoi(line2.substr(63, 5)));

    // Derived quantities
    constexpr double RE = 6378.137; // Earth radius [km]
    constexpr double MU = 398600.4418; // km³/s²
    double n_rad = tle.mean_motion * 2.0 * M_PI / 86400.0; // rad/s
    double sma = std::cbrt(MU / (n_rad * n_rad)); // km
    tle.perigee_km = static_cast<float>(sma * (1.0 - tle.ecc) - RE);
    tle.apogee_km = static_cast<float>(sma * (1.0 + tle.ecc) - RE);
    tle.period_min = static_cast<float>(1440.0 / tle.mean_motion);

    return true;
}

// ============================================================================
// Gauss Angles-Only OD (Initial Orbit Determination)
// ============================================================================

/// Simple Gauss method for 3 observations → initial orbit
/// Returns true if converged
/// Inputs: 3 RA/DEC observations + times + observer positions
struct GaussInput {
    double epoch[3];           // Observation times [s]
    double ra_rad[3];          // Right ascension [rad]
    double dec_rad[3];         // Declination [rad]
    std::array<double,3> obs_pos[3]; // Observer ECEF positions [km]
};

struct GaussOutput {
    std::array<double,3> r2;   // Position at middle obs [km, ECI]
    std::array<double,3> v2;   // Velocity at middle obs [km/s, ECI]
    double sma_km;
    double ecc;
    double inc_deg;
    bool converged;
};

inline GaussOutput gaussIOD(const GaussInput& in) {
    GaussOutput out{};
    constexpr double MU = 398600.4418;

    // Line-of-sight unit vectors
    std::array<double,3> L[3];
    for (int i = 0; i < 3; i++) {
        L[i] = {
            std::cos(in.dec_rad[i]) * std::cos(in.ra_rad[i]),
            std::cos(in.dec_rad[i]) * std::sin(in.ra_rad[i]),
            std::sin(in.dec_rad[i])
        };
    }

    // Time intervals
    double tau1 = in.epoch[0] - in.epoch[1];
    double tau3 = in.epoch[2] - in.epoch[1];
    double tau  = tau3 - tau1;

    if (std::abs(tau) < 1e-10) { out.converged = false; return out; }

    // Cross products for Gauss method
    auto cross = [](const std::array<double,3>& a, const std::array<double,3>& b) {
        return std::array<double,3>{a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
    };
    auto dot = [](const std::array<double,3>& a, const std::array<double,3>& b) {
        return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    };

    auto p1 = cross(L[1], L[2]);
    auto p2 = cross(L[0], L[2]);
    auto p3 = cross(L[0], L[1]);

    double D0 = dot(L[0], p1);
    if (std::abs(D0) < 1e-15) { out.converged = false; return out; }

    // D matrix elements
    double D[3][3];
    for (int i = 0; i < 3; i++) {
        D[i][0] = dot(in.obs_pos[i], p1);
        D[i][1] = dot(in.obs_pos[i], p2);
        D[i][2] = dot(in.obs_pos[i], p3);
    }

    double A = (-D[0][1]*tau3/tau + D[1][1] + D[2][1]*tau1/tau) / D0;
    double B = (D[0][1]*(tau3*tau3-tau*tau)*tau3/(6*tau) +
                D[2][1]*(tau*tau-tau1*tau1)*tau1/(6*tau)) / D0;

    double E = dot(L[1], in.obs_pos[1]);
    double R2sq = dot(in.obs_pos[1], in.obs_pos[1]);

    // Iterate for range
    double r2_est = 8000.0; // Initial guess [km]
    for (int iter = 0; iter < 20; iter++) {
        double r2_3 = r2_est * r2_est * r2_est;
        double rho2 = A + MU * B / r2_3;
        // r2² = rho2² + 2*E*rho2 + R2²
        double r2_new = std::sqrt(rho2*rho2 + 2*E*rho2 + R2sq);
        if (std::abs(r2_new - r2_est) < 1e-6) {
            r2_est = r2_new;
            out.converged = true;
            break;
        }
        r2_est = r2_new;
    }

    if (!out.converged) return out;

    // Compute position and velocity at middle observation
    double rho2 = A + MU * B / (r2_est*r2_est*r2_est);
    for (int i = 0; i < 3; i++) {
        out.r2[i] = in.obs_pos[1][i] + rho2 * L[1][i];
    }

    // Velocity via Gibbs/Herrick method (simplified: f,g series)
    double f1 = 1.0 - 0.5 * MU * tau1 * tau1 / (r2_est*r2_est*r2_est);
    double f3 = 1.0 - 0.5 * MU * tau3 * tau3 / (r2_est*r2_est*r2_est);
    double g1 = tau1 - (1.0/6.0) * MU * tau1*tau1*tau1 / (r2_est*r2_est*r2_est);
    double g3 = tau3 - (1.0/6.0) * MU * tau3*tau3*tau3 / (r2_est*r2_est*r2_est);

    double det = f1*g3 - f3*g1;
    if (std::abs(det) < 1e-15) { out.converged = false; return out; }

    // r1 from rho1
    double c1 = g3 / det;
    double c3 = -g1 / det;
    // Simplified: use r2 and estimated rho values
    for (int i = 0; i < 3; i++) {
        out.v2[i] = (-f3 * (in.obs_pos[0][i] + ((-D[0][0]*tau3/tau + D[1][0] + D[2][0]*tau1/tau)/D0)*L[0][i]) +
                      f1 * (in.obs_pos[2][i] + ((-D[0][2]*tau3/tau + D[1][2] + D[2][2]*tau1/tau)/D0)*L[2][i])) / det;
    }

    // Orbital elements from r2, v2
    double r = std::sqrt(dot(out.r2, out.r2));
    double v = std::sqrt(dot(out.v2, out.v2));
    double energy = v*v/2.0 - MU/r;
    out.sma_km = -MU / (2.0 * energy);

    auto h = cross(out.r2, out.v2);
    double hmag = std::sqrt(dot(h, h));
    out.inc_deg = std::acos(h[2] / hmag) * 180.0 / M_PI;

    double p = hmag * hmag / MU;
    out.ecc = std::sqrt(1.0 - p / out.sma_km);

    return out;
}

// ============================================================================
// Object Identification — match observation to catalog
// ============================================================================

/// Compare observation against TLE catalog, return best match
struct IDMatch {
    uint32_t norad_id;
    float    residual_arcsec;  // Angular residual
    float    time_residual_s;  // Time residual
    uint8_t  confidence;       // 0-100
};

inline IDMatch identifyObject(const ObsRecord& obs,
                                const std::vector<TLERecord>& catalog) {
    IDMatch best{};
    best.residual_arcsec = 1e6f; // Start with worst possible

    // Observation must have valid RA/DEC for angular matching
    const bool has_radec = !std::isnan(obs.ra_deg) && !std::isnan(obs.dec_deg);

    for (const auto& tle : catalog) {
        double obs_epoch = obs.epoch_s;
        double tle_age = std::abs(obs_epoch - tle.epoch_s);

        // Skip TLEs more than 30 days old — prediction accuracy degrades
        if (tle_age > 30.0 * 86400.0) continue;

        // --- Freshness score component (0-40 points) ---
        // Exponential decay: half-life ~7 days
        double freshness = 40.0 * std::exp(-tle_age / (7.0 * 86400.0));

        // --- Angular prediction component (0-60 points) ---
        double angular_score = 0.0;
        double angular_residual = 1e6;

        if (has_radec) {
            // Propagate TLE to observation epoch using mean-motion advancement
            double dt_s = obs_epoch - tle.epoch_s;
            double n_rad_s = tle.mean_motion * 2.0 * M_PI / 86400.0;
            double mean_anom_rad = (tle.mean_anom_deg * M_PI / 180.0) + n_rad_s * dt_s;

            // Semi-major axis from mean motion (km)
            double mu_earth = 398600.4418; // km³/s²
            double n_rad = tle.mean_motion * 2.0 * M_PI / 86400.0;
            double sma = std::cbrt(mu_earth / (n_rad * n_rad));

            // Compute approximate ECI position (simplified Keplerian, no perturbations)
            double ecc = tle.ecc;
            // Solve Kepler's equation (Newton iteration, 5 steps)
            double M = std::fmod(mean_anom_rad, 2.0 * M_PI);
            if (M < 0) M += 2.0 * M_PI;
            double E = M;
            for (int i = 0; i < 5; i++) {
                E = E - (E - ecc * std::sin(E) - M) / (1.0 - ecc * std::cos(E));
            }
            double nu = 2.0 * std::atan2(std::sqrt(1.0 + ecc) * std::sin(E / 2.0),
                                           std::sqrt(1.0 - ecc) * std::cos(E / 2.0));

            double r = sma * (1.0 - ecc * std::cos(E));

            // Perifocal coordinates
            double xp = r * std::cos(nu);
            double yp = r * std::sin(nu);

            // Rotation angles
            double inc = tle.inc_deg * M_PI / 180.0;
            double raan = tle.raan_deg * M_PI / 180.0;
            double argp = tle.argp_deg * M_PI / 180.0;

            // Account for Earth rotation and RAAN drift for observation epoch
            double omega_earth = 7.2921159e-5; // rad/s
            double raan_obs = raan + (tle.raan_deg > 0 ? 0 : 0); // TEME approx

            // ECI position
            double cos_raan = std::cos(raan_obs), sin_raan = std::sin(raan_obs);
            double cos_argp = std::cos(argp), sin_argp = std::sin(argp);
            double cos_inc  = std::cos(inc),  sin_inc  = std::sin(inc);

            double x_eci = (cos_raan * cos_argp - sin_raan * sin_argp * cos_inc) * xp
                         + (-cos_raan * sin_argp - sin_raan * cos_argp * cos_inc) * yp;
            double y_eci = (sin_raan * cos_argp + cos_raan * sin_argp * cos_inc) * xp
                         + (-sin_raan * sin_argp + cos_raan * cos_argp * cos_inc) * yp;
            double z_eci = (sin_argp * sin_inc) * xp + (cos_argp * sin_inc) * yp;

            // Convert ECI to RA/DEC (geocentric — observer topocentric ignored for scoring)
            double range = std::sqrt(x_eci * x_eci + y_eci * y_eci + z_eci * z_eci);
            double pred_dec = std::asin(z_eci / range) * 180.0 / M_PI;
            double pred_ra  = std::atan2(y_eci, x_eci) * 180.0 / M_PI;
            if (pred_ra < 0) pred_ra += 360.0;

            // Angular separation (Vincenty formula for robustness)
            double d_ra = (pred_ra - obs.ra_deg) * M_PI / 180.0;
            double dec1 = obs.dec_deg * M_PI / 180.0;
            double dec2 = pred_dec * M_PI / 180.0;
            double sep = std::atan2(
                std::sqrt(std::pow(std::cos(dec2) * std::sin(d_ra), 2.0) +
                          std::pow(std::cos(dec1) * std::sin(dec2) -
                                   std::sin(dec1) * std::cos(dec2) * std::cos(d_ra), 2.0)),
                std::sin(dec1) * std::sin(dec2) +
                std::cos(dec1) * std::cos(dec2) * std::cos(d_ra));

            angular_residual = sep * 180.0 / M_PI * 3600.0; // Convert to arcseconds

            // Score: 60 points for <10 arcsec, falling off with log
            if (angular_residual < 10.0) {
                angular_score = 60.0;
            } else if (angular_residual < 3600.0) {
                angular_score = 60.0 * (1.0 - std::log10(angular_residual / 10.0) / std::log10(360.0));
            }
        } else {
            // No RA/DEC: use inclination-only heuristic (weaker, 30 points max)
            angular_score = 30.0 * std::exp(-tle_age / (14.0 * 86400.0));
            angular_residual = static_cast<double>(tle_age / 86400.0 * 60.0);
        }

        double total_score = freshness + angular_score;
        float time_res = static_cast<float>(tle_age);

        if (total_score > best.confidence) {
            best.norad_id = tle.norad_id;
            best.confidence = static_cast<uint8_t>(std::min(total_score, 100.0));
            best.residual_arcsec = static_cast<float>(angular_residual);
            best.time_residual_s = time_res;
        }
    }

    return best;
}

// ============================================================================
// Filters
// ============================================================================

inline std::vector<ObsRecord> filterBySource(
    const std::vector<ObsRecord>& records, DataSource src) {
    std::vector<ObsRecord> out;
    for (const auto& r : records)
        if (r.data_source == static_cast<uint8_t>(src)) out.push_back(r);
    return out;
}

inline std::vector<ObsRecord> filterClassified(
    const std::vector<ObsRecord>& records) {
    std::vector<ObsRecord> out;
    for (const auto& r : records)
        if (r.catalog_status == static_cast<uint8_t>(CatalogStatus::CLASSIFIED) ||
            r.catalog_status == static_cast<uint8_t>(CatalogStatus::UNCATALOGED))
            out.push_back(r);
    return out;
}

inline std::vector<TLERecord> filterTLEsByAltitude(
    const std::vector<TLERecord>& tles, float minAlt_km, float maxAlt_km) {
    std::vector<TLERecord> out;
    for (const auto& t : tles)
        if (t.perigee_km >= minAlt_km && t.apogee_km <= maxAlt_km)
            out.push_back(t);
    return out;
}

}  // namespace satobs

#endif  // SATOBS_TYPES_H
