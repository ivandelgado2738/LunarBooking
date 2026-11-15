// lunar_dem.hpp
// -----------------------------------------------------------------------------
// Reads a section of a lunar polar DEM in GeoTIFF (e.g. ldem_87s_5mpp.tif from LOLA,
// 5 m/pixel, south polar stereographic) around a point and returns it as
// a regular mesh ready for triangulation.
//
//   - A single header, C++14 or later, with no dependencies (neither GDAL nor libtiff).
//   - Supports TIFF and BigTIFF, in tiles or strips, uncompressed / LZW / Deflate,
//     predictors 1, 2, and 3, little- and big-endian, double32/double64/int/uint.
//   - Only decompresses blocks touching the requested region (the file
//     3,3 GB is never loaded in full).
//
// Conventions used by the NASA/PGDA product:
//   - Projection: south polar stereographic on a sphere with R = 1,737,400 m,
//     true scale at the pole. Longitude 0 toward +Y (top of the image),
//     longitude 90E toward +X (right).
//   - Elevation h: meters above the sphere of 1737.4 km, measouthed radially.
//   - XYZ: MOON_ME system (fixed to the Moon), meters from the Moon center.
//     +Z = north pole, +X = (lat 0, lon 0). This is the same system as the ephemerides.
//
// Minimal usage:
//     lunar::Dem dem("ldem_87_5mpp.tif");
//     lunar::Patch p = dem.region_lat_lon(-89.45, 222.69, 1000.0);  // +-1 km
//     auto tris = p.triangles();
//     const lunar::Vec3& A = p.xyz[tris[0].a];
// -----------------------------------------------------------------------------
#ifndef LUNAR_DEM_HPP
#define LUNAR_DEM_HPP

#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS  // Visual Studio: allow fopen
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace lunar {

constexpr double PI = 3.14159265358979323846;
constexpr double GRAD = PI / 180.0;  // degrees -> radians

struct Vec3 {
    double x, y, z;
};

// Indices of the 3 vertices in Patch::xyz / Patch::h
struct Triangle {
    std::uint32_t a, b, c;
};

// -----------------------------------------------------------------------------
// Patch: the structure returned by the reader. Regular mesh rows x columns.
//   Nodo (i, j): i = row (grows toward -Y of the map), j = column (grows toward +X).
//   Everything is stored by rows: k = i * columns + j.
// -----------------------------------------------------------------------------
struct Patch {
    int rows = 0, columns = 0;      // mesh size (always odd x odd)
    int center_row = 0, center_column = 0;    // node at the requested point (the center)
    double spacing_m = 0.0;          // spacing between neighboring nodes [m]
    double center_lat = 0.0, center_lon = 0.0;  // lat/lon [deg] of the central node
    long long row0 = 0, column0 = 0;    // raster pixel of the node (0,0)
    int pixel_step = 1;                  // raster pixels between nodes

    std::vector<double> h;   // elevation [m] above the sphere of 1737.4 km; NaN = no data
    std::vector<Vec3> xyz;  // MOON_ME position [m]; NaN if no data

    std::size_t idx(int i, int j) const { return std::size_t(i) * std::size_t(columns) + std::size_t(j); }
    bool valid(int i, int j) const { return !std::isnan(h[idx(i, j)]); }
    double elevation(int i, int j) const { return h[idx(i, j)]; }
    const Vec3& point(int i, int j) const { return xyz[idx(i, j)]; }

    // 2 triangles per cell. Counterclockwise vertex order viewed from outside the
    // the Moon => the normal (B-A)x(C-A) points skyward. Cells are skipped
    // with any noof thecking data.
    std::vector<Triangle> triangles() const {
        std::vector<Triangle> t;
        if (rows < 2 || columns < 2) return t;
        t.reserve(std::size_t(rows - 1) * std::size_t(columns - 1) * 2);
        for (int i = 0; i + 1 < rows; ++i) {
            for (int j = 0; j + 1 < columns; ++j) {
                const std::uint32_t a = std::uint32_t(idx(i, j));          // top-left
                const std::uint32_t b = std::uint32_t(idx(i, j + 1));      // top-right
                const std::uint32_t c = std::uint32_t(idx(i + 1, j));      // bottom-left
                const std::uint32_t d = std::uint32_t(idx(i + 1, j + 1));  // bottom-right
                const bool va = !std::isnan(h[a]), vb = !std::isnan(h[b]);
                const bool vc = !std::isnan(h[c]), vd = !std::isnan(h[d]);
                if (va && vc && vb) t.push_back({a, c, b});
                if (vb && vc && vd) t.push_back({b, c, d});
            }
        }
        return t;
    }
};

// =============================================================================
// Internal details: 64-bit 64-bit file and decompressors
// =============================================================================
namespace detail {

class FileReader {
    std::FILE* f_ = nullptr;

public:
    explicit FileReader(const std::string& path) {
        f_ = std::fopen(path.c_str(), "rb");
        if (!f_) throw std::runtime_error("Cannot open file: " + path);
    }
    ~FileReader() {
        if (f_) std::fclose(f_);
    }
    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;

    void read(std::uint64_t pos, void* dst, std::size_t n) {
#ifdef _WIN32
        const int r = _fseeki64(f_, static_cast<long long>(pos), SEEK_SET);
#else
        const int r = fseeko(f_, static_cast<off_t>(pos), SEEK_SET);
#endif
        if (r != 0 || std::fread(dst, 1, n, f_) != n)
            throw std::runtime_error("TIFF read error (file truncated or corrupt)");
    }
};

inline bool host_little() {
    const std::uint16_t u = 1;
    unsigned char c;
    std::memcpy(&c, &u, 1);
    return c == 1;
}

template <class T>
inline T decode_integer(const unsigned char* p, bool le) {
    T v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i)
        v = T(v | (T(p[i]) << (8 * (le ? i : sizeof(T) - 1 - i))));
    return v;
}

// ---------------------------- Deflate (zlib) ---------------------------------
struct BitsLsb {
    const unsigned char* p;
    std::size_t n;
    std::size_t pos = 0;
    std::uint32_t buf = 0;
    int cnt = 0;
    int bits(int need) {
        std::uint32_t val = buf;
        while (cnt < need) {
            if (pos == n) throw std::runtime_error("Deflate: truncated data");
            val |= std::uint32_t(p[pos++]) << cnt;
            cnt += 8;
        }
        buf = val >> need;
        cnt -= need;
        return int(val & ((1u << need) - 1u));
    }
};

struct Huff {
    short count[16];
    short symbol[288];
};

inline void build_huffman(Huff& h, const short* length, int n) {
    for (int l = 0; l <= 15; ++l) h.count[l] = 0;
    for (int s = 0; s < n; ++s) h.count[length[s]]++;
    short offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; ++l) offs[l + 1] = short(offs[l] + h.count[l]);
    for (int s = 0; s < n; ++s)
        if (length[s] != 0) h.symbol[offs[length[s]]++] = short(s);
}

inline int decode_huffman(BitsLsb& s, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; ++len) {
        code |= s.bits(1);
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    throw std::runtime_error("Deflate: codigo Huffman invalid");
}

// Decompresses a zlib stream and requires exactly `cap` output bytes.
inline void inflate(const unsigned char* in, std::size_t n, unsigned char* out, std::size_t cap) {
    static const short lens[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                   2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dists[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25,
                                    33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                   6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    if (n < 2) throw std::runtime_error("Deflate: block empty");
    BitsLsb s{in + 2, n - 2};  // skip header zlib (2 bytes)
    std::size_t o = 0;
    int last_block = 0;
    while (!last_block && o < cap) {
        last_block = s.bits(1);
        const int block_type = s.bits(2);
        if (block_type == 0) {  // block almacenado
            s.buf = 0;
            s.cnt = 0;
            if (s.pos + 4 > s.n) throw std::runtime_error("Deflate: truncated data");
            const std::size_t len = std::size_t(s.p[s.pos]) | (std::size_t(s.p[s.pos + 1]) << 8);
            s.pos += 4;
            if (s.pos + len > s.n) throw std::runtime_error("Deflate: truncated data");
            const std::size_t c = std::min(len, cap - o);
            std::memcpy(out + o, s.p + s.pos, c);
            o += c;
            s.pos += len;
        } else if (block_type == 1 || block_type == 2) {
            Huff lc, dc;
            short lengths[320];
            if (block_type == 1) {  // Huffman fixed
                int sym = 0;
                for (; sym < 144; ++sym) lengths[sym] = 8;
                for (; sym < 256; ++sym) lengths[sym] = 9;
                for (; sym < 280; ++sym) lengths[sym] = 7;
                for (; sym < 288; ++sym) lengths[sym] = 8;
                build_huffman(lc, lengths, 288);
                for (sym = 0; sym < 30; ++sym) lengths[sym] = 5;
                build_huffman(dc, lengths, 30);
            } else {  // Huffman dynamic
                const int nlen = s.bits(5) + 257;
                const int ndist = s.bits(5) + 1;
                const int ncode = s.bits(4) + 4;
                if (nlen > 286 || ndist > 30) throw std::runtime_error("Deflate: header invalid");
                int i = 0;
                for (; i < ncode; ++i) lengths[order[i]] = short(s.bits(3));
                for (; i < 19; ++i) lengths[order[i]] = 0;
                build_huffman(lc, lengths, 19);
                i = 0;
                while (i < nlen + ndist) {
                    const int sym = decode_huffman(s, lc);
                    if (sym < 16) {
                        lengths[i++] = short(sym);
                    } else {
                        short len = 0;
                        int rep;
                        if (sym == 16) {
                            if (i == 0) throw std::runtime_error("Deflate: header invalid");
                            len = lengths[i - 1];
                            rep = 3 + s.bits(2);
                        } else if (sym == 17) {
                            rep = 3 + s.bits(3);
                        } else {
                            rep = 11 + s.bits(7);
                        }
                        if (i + rep > nlen + ndist) throw std::runtime_error("Deflate: header invalid");
                        while (rep--) lengths[i++] = len;
                    }
                }
                build_huffman(lc, lengths, nlen);
                build_huffman(dc, lengths + nlen, ndist);
            }
            while (o < cap) {
                int sym = decode_huffman(s, lc);
                if (sym < 256) {
                    out[o++] = static_cast<unsigned char>(sym);
                } else if (sym == 256) {
                    break;
                } else {
                    sym -= 257;
                    if (sym >= 29) throw std::runtime_error("Deflate: symbol invalid");
                    int len = lens[sym] + s.bits(lext[sym]);
                    const int ds = decode_huffman(s, dc);
                    if (ds >= 30) throw std::runtime_error("Deflate: distance invalid");
                    const std::size_t dist = std::size_t(dists[ds]) + std::size_t(s.bits(dext[ds]));
                    if (dist > o) throw std::runtime_error("Deflate: distance out of range");
                    while (len-- > 0 && o < cap) {
                        out[o] = out[o - dist];
                        ++o;
                    }
                }
            }
        } else {
            throw std::runtime_error("Deflate: invalid block type");
        }
    }
    if (o != cap) throw std::runtime_error("Deflate: block shorter than expected");
}

// ------------------------------ LZW (TIFF) -----------------------------------
// Variante TIFF 6.0: codigos MSB-first, 9..12 bits, "early change".
inline void lzw(const unsigned char* in, std::size_t n, unsigned char* out, std::size_t cap) {
    std::vector<std::uint16_t> pref(4096), lon(4096);
    std::vector<unsigned char> suf(4096), prim(4096);
    for (int i = 0; i < 256; ++i) {
        pref[i] = 0;
        lon[i] = 1;
        suf[i] = prim[i] = static_cast<unsigned char>(i);
    }
    std::size_t pos = 0, o = 0;
    std::uint32_t acc = 0;
    int nb = 0;
    int nbits = 9, next = 258, old = -1;
    while (o < cap) {
        while (nb < nbits && pos < n) {
            acc = (acc << 8) | in[pos++];
            nb += 8;
        }
        if (nb < nbits) break;  // data exhausted
        const int code = int((acc >> (nb - nbits)) & ((1u << nbits) - 1u));
        nb -= nbits;
        if (code == 257) break;  // end of information
        if (code == 256) {       // clear table
            nbits = 9;
            next = 258;
            old = -1;
            continue;
        }
        if (old < 0) {
            if (code > 255) throw std::runtime_error("LZW: flujo corrupt");
            out[o++] = static_cast<unsigned char>(code);
            old = code;
            continue;
        }
        if (code > next || (code == next && next >= 4096)) throw std::runtime_error("LZW: flujo corrupt");
        if (next < 4096) {
            pref[next] = std::uint16_t(old);
            prim[next] = prim[old];
            suf[next] = (code < next) ? prim[code] : prim[old];
            lon[next] = std::uint16_t(lon[old] + 1);
            ++next;
        }
        const std::size_t L = lon[code];
        const std::size_t libre = cap - o;
        int c = code;
        for (std::size_t k = L; k-- > 0;) {
            if (k < libre) out[o + k] = suf[c];
            c = pref[c];
        }
        o += std::min(L, libre);
        old = code;
        if (next >= (1 << nbits) - 1 && nbits < 12) ++nbits;
    }
    if (o != cap) throw std::runtime_error("LZW: block shorter than expected");
}

}  // namespace detail

// =============================================================================
// Dem: opens the GeoTIFF (only reads the header) and provides regions on demand
// =============================================================================
class Dem {
public:
    explicit Dem(const std::string& path) : arch_(path) { read_header(); }

    long long width() const { return W_; }
    long long height() const { return H_; }
    double pixel_m() const { return sx_; }
    double reference_radius() const { return R_; }  // 1 737 400 m

    // ---- Projection (south polar stereographic, sphere) ----------------------
    // lat/lon in degrees (east-positive longitude) <-> X/Y of the map en meters
    void lat_lon_to_xy(double lat_deg, double lon_deg, double& x, double& y) const {
        const double rho = 2.0 * R_ * std::tan(0.5 * (PI / 2.0 + lat_deg * GRAD));
        const double dl = lon_deg * GRAD - lon0_;
        x = rho * std::sin(dl);
        y = rho * std::cos(dl);
    }
    void xy_to_lat_lon(double x, double y, double& lat_deg, double& lon_deg) const {
        const double rho = std::hypot(x, y);
        lat_deg = (-PI / 2.0 + 2.0 * std::atan(rho / (2.0 * R_))) / GRAD;
        double lon = (lon0_ + std::atan2(x, y)) / GRAD;
        if (lon < 0.0) lon += 360.0;
        if (lon >= 360.0) lon -= 360.0;
        lon_deg = lon;
    }
    // (lat, lon, elevation above the sphere) -> XYZ MOON_ME in meters
    Vec3 a_moon_me(double lat_deg, double lon_deg, double h) const {
        const double la = lat_deg * GRAD, lo = lon_deg * GRAD, r = R_ + h;
        return {r * std::cos(la) * std::cos(lo), r * std::cos(la) * std::sin(lo), r * std::sin(la)};
    }

    // X/Y of the map [m] the center of a pixel, and the pixel containing an X/Y
    double x_from_column(long long column) const { return xc0_ + double(column) * sx_; }
    double y_from_row(long long row) const { return yc0_ - double(row) * sy_; }
    long long column_from_x(double x) const { return std::llround((x - xc0_) / sx_); }
    long long row_from_y(double y) const { return std::llround((yc0_ - y) / sy_); }

    // ---- Reading ------------------------------------------------------------
    // Square region centered on the pixel nearest to (lat, lon), covering
    // +-radius_m in X and in Y of the map. step = 1 -> full resolution (5 m);
    // step = 4 -> takes one of every four pixels (20 m), for large regions.
    // Data outside the file is returned as NaN.
    Patch region_lat_lon(double lat_deg, double lon_deg, double radius_m, int step = 1) {
        double x, y;
        lat_lon_to_xy(lat_deg, lon_deg, x, y);
        return region_xy(x, y, radius_m, step);
    }

    // Same, but specifying the point in coordinates X/Y of the map [m]
    Patch region_xy(double x_m, double y_m, double radius_m, int step = 1) {
        if (step < 1) throw std::invalid_argument("step must be >= 1");
        if (!(radius_m >= 0.0)) throw std::invalid_argument("radius_m must be >= 0");
        const long long first_column = column_from_x(x_m), first_row = row_from_y(y_m);
        if (first_column < 0 || first_column >= W_ || first_row < 0 || first_row >= H_)
            throw std::out_of_range("The requested point is outside the area covered by the file");
        const double nn = std::floor(radius_m / (sx_ * double(step)) + 1e-9);
        if (nn > 20000.0)
            throw std::invalid_argument("Region too large: increase `step` or decrease `radius_m`");
        return read_window(first_row, first_column, int(nn), step);
    }

    // Elevation [m] of the pixel nearest a (lat, lon). NaN if no data.
    double elevation(double lat_deg, double lon_deg) { return region_lat_lon(lat_deg, lon_deg, 0.0).h[0]; }

    // Readable summary of the file contents (for verification)
    std::string info() const {
        char b[1024];
        const char* comp = comp_ == 1 ? "none" : comp_ == 5 ? "LZW" : "Deflate";
        const char* fmt = fmt_ == 3 ? "double" : fmt_ == 2 ? "int" : "uint";
        std::snprintf(b, sizeof b,
                      "Raster: %lld x %lld px, %s%d, %s\n"
                      "Blocks: %s of %lld x %lld px, compression %s, predictor %d\n"
                      "Pixel: %.6g m | X: [%.1f, %.1f] m | Y: [%.1f, %.1f] m\n"
                      "Projection: south polar stereographic, R = %.1f m, lon0 = %.4f deg\n"
                      "No data: %s",
                      W_, H_, fmt, bps_ * 8, big_ ? "BigTIFF" : "TIFF classic",
                      tiled_ ? "tiles" : "strips", bw_, bh_, comp, pred_,
                      sx_, xc0_ - 0.5 * sx_, x_from_column(W_ - 1) + 0.5 * sx_,
                      y_from_row(H_ - 1) - 0.5 * sy_, yc0_ + 0.5 * sy_,
                      R_, lon0_ / GRAD,
                      has_nodata_ ? std::to_string(nodata_).c_str() : "(not declared)");
        return b;
    }

private:
    detail::FileReader arch_;
    bool le_ = true, big_ = false, swap_ = false;
    long long W_ = 0, H_ = 0;
    int bps_ = 0;      // bytes per sample
    int fmt_ = 1;      // 1 uint, 2 int, 3 double
    int comp_ = 1;     // 1 none, 5 LZW, 8 Deflate
    int pred_ = 1;     // 1 none, 2 horizontal, 3 floating point
    bool tiled_ = false;
    long long bw_ = 0, bh_ = 0, nbx_ = 0, nby_ = 0;  // size y number of blocks
    std::vector<std::uint64_t> off_, cnt_;
    double sx_ = 0, sy_ = 0, xc0_ = 0, yc0_ = 0;  // pixel and pixel center (0,0)
    double R_ = 1737400.0, lon0_ = 0.0;           // lon0 in radians
    bool has_nodata_ = false;
    double nodata_ = 0.0;

    // ---- header TIFF --------------------------------------------------------
    struct Entry {
        int block_type = 0;
        std::uint64_t n = 0;
        std::vector<unsigned char> data;
    };

    std::vector<double> values(const Entry& e) const {
        std::vector<double> v(std::size_t(e.n));
        const unsigned char* d = e.data.data();
        using namespace detail;
        for (std::size_t i = 0; i < v.size(); ++i) {
            switch (e.block_type) {
                case 1: case 2: case 7: v[i] = d[i]; break;
                case 6: v[i] = static_cast<signed char>(d[i]); break;
                case 3: v[i] = decode_integer<std::uint16_t>(d + 2 * i, le_); break;
                case 8: v[i] = static_cast<std::int16_t>(decode_integer<std::uint16_t>(d + 2 * i, le_)); break;
                case 4: case 13: v[i] = decode_integer<std::uint32_t>(d + 4 * i, le_); break;
                case 9: v[i] = static_cast<std::int32_t>(decode_integer<std::uint32_t>(d + 4 * i, le_)); break;
                case 16: case 18: v[i] = double(decode_integer<std::uint64_t>(d + 8 * i, le_)); break;
                case 17: v[i] = double(static_cast<std::int64_t>(decode_integer<std::uint64_t>(d + 8 * i, le_))); break;
                case 11: {
                    const std::uint32_t u = decode_integer<std::uint32_t>(d + 4 * i, le_);
                    double f;
                    std::memcpy(&f, &u, 4);
                    v[i] = f;
                    break;
                }
                case 12: {
                    const std::uint64_t u = decode_integer<std::uint64_t>(d + 8 * i, le_);
                    double f;
                    std::memcpy(&f, &u, 8);
                    v[i] = f;
                    break;
                }
                case 5: v[i] = double(decode_integer<std::uint32_t>(d + 8 * i, le_)) /
                               double(decode_integer<std::uint32_t>(d + 8 * i + 4, le_)); break;
                default: v[i] = 0.0;
            }
        }
        return v;
    }

    void read_header() {
        using namespace detail;
        static const int type_sizes[19] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8, 4, 0, 0, 8, 8, 8};

        unsigned char cab[16];
        arch_.read(0, cab, 16);
        if (cab[0] == 'I' && cab[1] == 'I') le_ = true;
        else if (cab[0] == 'M' && cab[1] == 'M') le_ = false;
        else throw std::runtime_error("The file is not a TIFF");
        swap_ = (le_ != host_little());

        const int magic = decode_integer<std::uint16_t>(cab + 2, le_);
        std::uint64_t ifd;
        if (magic == 42) {
            big_ = false;
            ifd = decode_integer<std::uint32_t>(cab + 4, le_);
        } else if (magic == 43) {
            big_ = true;
            ifd = decode_integer<std::uint64_t>(cab + 8, le_);
        } else {
            throw std::runtime_error("The file is not a TIFF");
        }

        unsigned char nb[8];
        arch_.read(ifd, nb, big_ ? 8 : 2);
        const std::uint64_t n_ent = big_ ? decode_integer<std::uint64_t>(nb, le_) : decode_integer<std::uint16_t>(nb, le_);
        if (n_ent == 0 || n_ent > 65535) throw std::runtime_error("TIFF: directorio corrupt");
        const std::size_t te = big_ ? 20 : 12, tc = big_ ? 8 : 4;
        std::vector<unsigned char> dir(std::size_t(n_ent) * te);
        arch_.read(ifd + (big_ ? 8 : 2), dir.data(), dir.size());

        // Tags of interest
        Entry e_off, e_cnt, e_scale, e_tiepoint, e_mat, e_gk, e_gd, e_nd;
        double rps = 4294967295.0, spp = 1, bits = 1, fmt = 1, comp = 1, pred = 1, tw = 0, th = 0;
        bool has_strips = false, has_tiles = false;

        for (std::uint64_t k = 0; k < n_ent; ++k) {
            const unsigned char* e = dir.data() + std::size_t(k) * te;
            const int tag = decode_integer<std::uint16_t>(e, le_);
            Entry en;
            en.block_type = decode_integer<std::uint16_t>(e + 2, le_);
            en.n = big_ ? decode_integer<std::uint64_t>(e + 4, le_) : decode_integer<std::uint32_t>(e + 4, le_);
            const unsigned char* field_data = e + (big_ ? 12 : 8);
            switch (tag) {
                case 256: case 257: case 258: case 259: case 273: case 277: case 278: case 279:
                case 317: case 322: case 323: case 324: case 325: case 339:
                case 33550: case 33922: case 34264: case 34735: case 34736: case 42113:
                    break;
                default:
                    continue;
            }
            if (en.block_type < 1 || en.block_type > 18 || type_sizes[en.block_type] == 0) continue;
            const std::uint64_t bytes = en.n * std::uint64_t(type_sizes[en.block_type]);
            if (bytes > (std::uint64_t(1) << 30)) throw std::runtime_error("TIFF: tag too large");
            en.data.resize(std::size_t(bytes));
            if (bytes <= tc) {
                std::memcpy(en.data.data(), field_data, std::size_t(bytes));
            } else {
                const std::uint64_t pos = big_ ? decode_integer<std::uint64_t>(field_data, le_)
                                               : decode_integer<std::uint32_t>(field_data, le_);
                arch_.read(pos, en.data.data(), en.data.size());
            }
            auto first_value = [&]() { return en.n ? values(en)[0] : 0.0; };
            switch (tag) {
                case 256: W_ = (long long)first_value(); break;
                case 257: H_ = (long long)first_value(); break;
                case 258: bits = first_value(); break;
                case 259: comp = first_value(); break;
                case 277: spp = first_value(); break;
                case 278: rps = first_value(); break;
                case 317: pred = first_value(); break;
                case 322: tw = first_value(); break;
                case 323: th = first_value(); break;
                case 339: fmt = first_value(); break;
                case 273: e_off = std::move(en); has_strips = true; break;
                case 279: if (!has_tiles) e_cnt = std::move(en); break;
                case 324: e_off = std::move(en); has_tiles = true; break;
                case 325: e_cnt = std::move(en); break;
                case 33550: e_scale = std::move(en); break;
                case 33922: e_tiepoint = std::move(en); break;
                case 34264: e_mat = std::move(en); break;
                case 34735: e_gk = std::move(en); break;
                case 34736: e_gd = std::move(en); break;
                case 42113: e_nd = std::move(en); break;
                default: break;
            }
        }

        if (W_ <= 0 || H_ <= 0) throw std::runtime_error("TIFF: missing dimensions");
        if (spp != 1) throw std::runtime_error("TIFF: expected 1 band (a DEM), got more");
        if (bits != 8 && bits != 16 && bits != 32 && bits != 64)
            throw std::runtime_error("TIFF: unsupported bits per sample");
        bps_ = int(bits) / 8;
        fmt_ = int(fmt);
        const bool fmt_ok = (fmt_ == 3 && (bps_ == 4 || bps_ == 8)) || ((fmt_ == 1 || fmt_ == 2) && bps_ <= 4);
        if (!fmt_ok) throw std::runtime_error("TIFF: unsupported sample format");
        comp_ = int(comp);
        if (comp_ == 32946) comp_ = 8;
        if (comp_ != 1 && comp_ != 5 && comp_ != 8)
            throw std::runtime_error(
                "TIFF: compression unsupported (codigo " + std::to_string(int(comp)) +
                "). Convert the file once with: gdal_translate -co COMPRESS=DEFLATE -co TILED=YES "
                "-co BIGTIFF=YES input.tif output.tif");
        pred_ = int(pred);
        if (pred_ < 1 || pred_ > 3) throw std::runtime_error("TIFF: unsupported predictor");
        if (comp_ == 1) pred_ = 1;

        tiled_ = has_tiles;
        if (tiled_) {
            if (tw <= 0 || th <= 0) throw std::runtime_error("TIFF: invalid tile size");
            bw_ = (long long)tw;
            bh_ = (long long)th;
        } else {
            if (!has_strips) throw std::runtime_error("TIFF: has neither strips nor tiles");
            bw_ = W_;
            bh_ = std::min<long long>((long long)rps, H_);
            if (bh_ <= 0) bh_ = H_;
        }
        nbx_ = (W_ + bw_ - 1) / bw_;
        nby_ = (H_ + bh_ - 1) / bh_;
        {
            const std::vector<double> o = values(e_off), c = values(e_cnt);
            if ((long long)o.size() < nbx_ * nby_ || c.size() != o.size())
                throw std::runtime_error("TIFF: incomplete block table");
            off_.resize(o.size());
            cnt_.resize(c.size());
            for (std::size_t i = 0; i < o.size(); ++i) {
                off_[i] = std::uint64_t(o[i]);
                cnt_[i] = std::uint64_t(c[i]);
            }
        }

        // ---- GeoKeys: raster type, radius and central meridian --------------
        bool pixel_is_point = false;
        if (e_gk.n >= 4) {
            const std::vector<double> gk = values(e_gk), gd = values(e_gd);
            const std::size_t nk = std::size_t(gk[3]);
            bool has_pole = false;
            for (std::size_t k = 0; k < nk && 4 + 4 * k + 3 < gk.size(); ++k) {
                const int id = int(gk[4 + 4 * k]), loc = int(gk[5 + 4 * k]);
                const std::size_t val = std::size_t(gk[7 + 4 * k]);
                if (id == 1025 && loc == 0) pixel_is_point = (val == 2);
                if (loc == 34736 && val < gd.size()) {
                    const double x = gd[val];
                    if (id == 2057 && x > 0) R_ = x;
                    if (id == 3095) { lon0_ = x * GRAD; has_pole = true; }
                    if (id == 3088 && !has_pole) lon0_ = x * GRAD;
                    if ((id == 3089 || id == 3081) && x > 0)
                        throw std::runtime_error("This reader only supports south polar projection");
                }
            }
        }

        // ---- Georeferencing: pixel center (0,0) and pixel size ----
        const double midpoint = pixel_is_point ? 0.0 : 0.5;
        if (e_scale.n >= 2 && e_tiepoint.n >= 6) {
            const std::vector<double> s = values(e_scale), t = values(e_tiepoint);
            sx_ = s[0];
            sy_ = s[1];
            xc0_ = t[3] + (midpoint - t[0]) * sx_;
            yc0_ = t[4] - (midpoint - t[1]) * sy_;
        } else if (e_mat.n >= 16) {
            const std::vector<double> m = values(e_mat);
            if (m[1] != 0.0 || m[4] != 0.0) throw std::runtime_error("GeoTIFF rotated: unsupported");
            sx_ = m[0];
            sy_ = -m[5];
            xc0_ = m[3] + midpoint * sx_;
            yc0_ = m[7] - midpoint * sy_;
        } else {
            throw std::runtime_error("The TIFF has no georeferencing (not a GeoTIFF)");
        }
        if (!(sx_ > 0.0) || !(sy_ > 0.0)) throw std::runtime_error("GeoTIFF: invalid pixel size");

        // ---- Value "no data" -------------------------------------------------
        if (e_nd.n > 0) {
            const std::string s(e_nd.data.begin(), e_nd.data.end());
            char* fin = nullptr;
            const double v = std::strtod(s.c_str(), &fin);
            if (fin != s.c_str() && !std::isnan(v)) {
                has_nodata_ = true;
                nodata_ = v;
            }
        }
    }

    // ---- samples -------------------------------------------------------------
    // p points to a sample in NATIVE byte order
    double sample(const unsigned char* p) const {
        double v;
        switch (fmt_ * 10 + bps_) {
            case 34: { float f; std::memcpy(&f, p, 4); if (has_nodata_ && double(f) == nodata_) return nan_value(); return double(f); }  // float32 (antes se copiaban 4 bytes dentro de un double -> basura)
            case 38: { std::memcpy(&v, p, 8); break; }
            case 11: v = *p; break;
            case 12: { std::uint16_t u; std::memcpy(&u, p, 2); v = u; break; }
            case 14: { std::uint32_t u; std::memcpy(&u, p, 4); v = u; break; }
            case 21: v = static_cast<signed char>(*p); break;
            case 22: { std::int16_t u; std::memcpy(&u, p, 2); v = u; break; }
            case 24: { std::int32_t u; std::memcpy(&u, p, 4); v = u; break; }
            default: return nan_value();
        }
        if (has_nodata_ && v == nodata_) return nan_value();
        return double(v);
    }
    static double nan_value() { return std::numeric_limits<double>::quiet_NaN(); }

    void swap_samples(unsigned char* p, std::size_t n) const {
        if (bps_ == 1) return;
        for (std::size_t i = 0; i < n; ++i) std::reverse(p + i * bps_, p + (i + 1) * bps_);
    }

    // Decompresses block idx (bw_ x rows_b samples) and leaves it in native order
    void read_block(std::size_t idx, std::size_t rows_b, std::vector<unsigned char>& out,
                     std::vector<unsigned char>& tmp) {
        const std::uint64_t nbytes64 = std::uint64_t(bw_) * rows_b * std::uint64_t(bps_);
        if (nbytes64 > (std::uint64_t(1) << 31) || cnt_[idx] > (std::uint64_t(1) << 31))
            throw std::runtime_error(
                "TIFF: huge compressed block. Convert the file to tiles with: gdal_translate "
                "-co TILED=YES -co COMPRESS=DEFLATE -co BIGTIFF=YES input.tif output.tif");
        const std::size_t nbytes = std::size_t(nbytes64);
        out.resize(nbytes);
        tmp.resize(std::size_t(cnt_[idx]));
        arch_.read(off_[idx], tmp.data(), tmp.size());
        if (comp_ == 5) detail::lzw(tmp.data(), tmp.size(), out.data(), nbytes);
        else detail::inflate(tmp.data(), tmp.size(), out.data(), nbytes);

        const std::size_t row_bytes = std::size_t(bw_) * std::size_t(bps_);
        const std::size_t w = std::size_t(bw_), b = std::size_t(bps_);
        if (pred_ == 3) {
            // Predictor floating-point: byte-by-byte differences + byte planes (MSB first_value)
            tmp.resize(row_bytes);
            const bool hl = detail::host_little();
            for (std::size_t r = 0; r < rows_b; ++r) {
                unsigned char* row = out.data() + r * row_bytes;
                for (std::size_t i = 1; i < row_bytes; ++i) row[i] = static_cast<unsigned char>(row[i] + row[i - 1]);
                std::memcpy(tmp.data(), row, row_bytes);
                for (std::size_t c = 0; c < w; ++c)
                    for (std::size_t k = 0; k < b; ++k)
                        row[b * c + k] = tmp[(hl ? (b - 1 - k) : k) * w + c];
            }
        } else {
            if (swap_) swap_samples(out.data(), w * rows_b);
            if (pred_ == 2) {
                // Horizontal predictor: accumulated integer sum per row
                for (std::size_t r = 0; r < rows_b; ++r) {
                    unsigned char* row = out.data() + r * row_bytes;
                    if (b == 1) {
                        for (std::size_t c = 1; c < w; ++c) row[c] = static_cast<unsigned char>(row[c] + row[c - 1]);
                    } else if (b == 2) {
                        std::uint16_t prev, cur;
                        std::memcpy(&prev, row, 2);
                        for (std::size_t c = 1; c < w; ++c) {
                            std::memcpy(&cur, row + 2 * c, 2);
                            prev = std::uint16_t(prev + cur);
                            std::memcpy(row + 2 * c, &prev, 2);
                        }
                    } else if (b == 4) {
                        std::uint32_t prev, cur;
                        std::memcpy(&prev, row, 4);
                        for (std::size_t c = 1; c < w; ++c) {
                            std::memcpy(&cur, row + 4 * c, 4);
                            prev += cur;
                            std::memcpy(row + 4 * c, &prev, 4);
                        }
                    } else {
                        std::uint64_t prev, cur;
                        std::memcpy(&prev, row, 8);
                        for (std::size_t c = 1; c < w; ++c) {
                            std::memcpy(&cur, row + 8 * c, 8);
                            prev += cur;
                            std::memcpy(row + 8 * c, &prev, 8);
                        }
                    }
                }
            }
        }
    }

    // ---- window reading --------------------------------------------------
    // Nodes: row = first_row + (k - n) * step, column = first_column + (m - n) * step, with k and m in [0, 2n]
    Patch read_window(long long first_row, long long first_column, int n, int step) {
        Patch P;
        P.rows = P.columns = 2 * n + 1;
        P.center_row = P.center_column = n;
        P.pixel_step = step;
        P.spacing_m = sx_ * double(step);
        P.row0 = first_row - (long long)n * step;
        P.column0 = first_column - (long long)n * step;
        const std::size_t node_count = std::size_t(P.rows) * std::size_t(P.columns);
        P.h.assign(node_count, nan_value());

        // Range of nodes inside the raster
        const int k0 = int(std::max<long long>(0, n - first_row / step));
        const int k1 = int(std::min<long long>(2LL * n, n + (H_ - 1 - first_row) / step));
        const int m0 = int(std::max<long long>(0, n - first_column / step));
        const int m1 = int(std::min<long long>(2LL * n, n + (W_ - 1 - first_column) / step));
        auto row_at = [&](int k) { return first_row + (long long)(k - n) * step; };
        auto column_at = [&](int m) { return first_column + (long long)(m - n) * step; };

        std::vector<unsigned char> buf, tmp;
        unsigned char s8[8];
        for (int k = k0; k <= k1;) {
            const long long by = row_at(k) / bh_;
            int kf = k;
            while (kf + 1 <= k1 && row_at(kf + 1) / bh_ == by) ++kf;
            const std::size_t rows_b = tiled_ ? std::size_t(bh_) : std::size_t(std::min(bh_, H_ - by * bh_));
            for (int m = m0; m <= m1;) {
                const long long bx = column_at(m) / bw_;
                int mf = m;
                while (mf + 1 <= m1 && column_at(mf + 1) / bw_ == bx) ++mf;
                const std::size_t ib = std::size_t(by * nbx_ + bx);
                if (cnt_[ib] != 0) {  // (block empty = no data)
                    if (comp_ == 1) {
                        // Uncompressed: read only the required row span
                        const long long c_ini = column_at(m) - bx * bw_;
                        const std::size_t span = (std::size_t(mf - m) * std::size_t(step) + 1) * std::size_t(bps_);
                        buf.resize(span);
                        for (int kk = k; kk <= kf; ++kk) {
                            const long long ri = row_at(kk) - by * bh_;
                            arch_.read(off_[ib] + std::uint64_t(ri * bw_ + c_ini) * std::uint64_t(bps_), buf.data(), span);
                            for (int mm = m; mm <= mf; ++mm) {
                                const unsigned char* p = buf.data() + std::size_t(mm - m) * std::size_t(step) * std::size_t(bps_);
                                if (swap_) {
                                    std::reverse_copy(p, p + bps_, s8);
                                    p = s8;
                                }
                                P.h[P.idx(kk, mm)] = sample(p);
                            }
                        }
                    } else {
                        read_block(ib, rows_b, buf, tmp);
                        for (int kk = k; kk <= kf; ++kk) {
                            const std::size_t ri = std::size_t(row_at(kk) - by * bh_);
                            for (int mm = m; mm <= mf; ++mm) {
                                const std::size_t ci = std::size_t(column_at(mm) - bx * bw_);
                                P.h[P.idx(kk, mm)] = sample(buf.data() + (ri * std::size_t(bw_) + ci) * std::size_t(bps_));
                            }
                        }
                    }
                }
                m = mf + 1;
            }
            k = kf + 1;
        }

        // XYZ in MOON_ME for each node
        P.xyz.resize(node_count);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        for (int i = 0; i < P.rows; ++i) {
            const double y = y_from_row(P.row0 + (long long)i * step);
            for (int j = 0; j < P.columns; ++j) {
                const std::size_t q = P.idx(i, j);
                if (std::isnan(P.h[q])) {
                    P.xyz[q] = {nan, nan, nan};
                    continue;
                }
                const double x = x_from_column(P.column0 + (long long)j * step);
                double lat, lon;
                xy_to_lat_lon(x, y, lat, lon);
                P.xyz[q] = a_moon_me(lat, lon, double(P.h[q]));
            }
        }
        xy_to_lat_lon(x_from_column(first_column), y_from_row(first_row), P.center_lat, P.center_lon);
        return P;
    }
};

}  // namespace lunar

#endif  // LUNAR_DEM_HPP
