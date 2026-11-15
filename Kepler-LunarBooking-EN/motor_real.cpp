// ============================================================================
// ARCHIVO: motor_real.cpp
// Implementacion del puente web <-> fisica. Ver motor_real.hpp.
//
// Variables de entorno opcionales:
//   LUNAR_DEM_RADIO_CERCANO_M  (por defecto 1000)  parche a resolucion completa
//   LUNAR_DEM_RADIO_LEJANO_M   (por defecto 15000) parche submuestreado
//   LUNAR_ALTURA_SENSOR_M      (por defecto 2)     altura del panel/antena sobre el suelo
// ============================================================================
#include "motor_real.hpp"

#include "fisica/algebra.hpp"          // (incluye lunar_dem.hpp)
#include "fisica/maximum_average.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>

namespace MotorReal {
namespace {

const double RAD = 3.14159265358979323846 / 180.0;
const double R_LUNA_M = 1737400.0;
const double R_SOL_M = 695700000.0;
const double R_TIERRA_M = 6371000.0;
const double DIAS_J2000_EPOCA = 9861.5;  // 01 Jan 2027 00:00:00 UTC (ElapsedSecs = 0)
const int N_BINS = 1440;                 // horizonte cada 0.25 deg de azimut
const double ANCHO_BIN = 360.0 / N_BINS;

// ---------------------------------------------------------------- efemerides
struct Efemerides {
    std::vector<double> t;             // segundos desde la epoca
    std::vector<lunar::Vec3> tierra;   // metros, sistema fijo a la Luna
    std::vector<lunar::Vec3> sol;
    std::string fecha_ini, fecha_fin;
};
Efemerides g_efem;

std::unique_ptr<lunar::Dem> g_dem;
std::string g_ruta_dem;
std::mutex g_mutex;

double envDouble(const char* nombre, double defecto) {
    const char* v = std::getenv(nombre);
    if (!v || !*v) return defecto;
    char* fin = nullptr;
    double x = std::strtod(v, &fin);
    return (fin == v) ? defecto : x;
}

// Linea GMAT: ElapsedSecs  DD Mon YYYY HH:MM:SS.sss  Tx Ty Tz  Sx Sy Sz  (km)
bool cargarEfemerides(const std::string& ruta) {
    FILE* f = std::fopen(ruta.c_str(), "r");
    if (!f) return false;
    Efemerides e;
    e.t.reserve(400000);
    e.tierra.reserve(400000);
    e.sol.reserve(400000);
    char linea[1024];
    std::string primera, ultima;
    while (std::fgets(linea, sizeof linea, f)) {
        char* p = linea;
        char* fin = nullptr;
        double t = std::strtod(p, &fin);
        if (fin == p) continue;  // cabecera o linea vacia
        p = fin;
        // saltar los 4 tokens de la fecha, guardandola
        char fecha[64];
        int nf = 0;
        for (int tok = 0; tok < 4; ++tok) {
            while (*p == ' ' || *p == '\t') ++p;
            if (tok > 0 && nf < 62) fecha[nf++] = ' ';
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                if (nf < 62) fecha[nf++] = *p;
                ++p;
            }
        }
        fecha[nf] = '\0';
        double v[6];
        bool ok = true;
        for (int k = 0; k < 6; ++k) {
            v[k] = std::strtod(p, &fin);
            if (fin == p) { ok = false; break; }
            p = fin;
        }
        if (!ok) continue;
        if (!e.t.empty() && t <= e.t.back()) continue;  // exigir tiempo creciente
        e.t.push_back(t);
        e.tierra.push_back(productByScalar({v[0], v[1], v[2]}, 1000.0));  // km -> m
        e.sol.push_back(productByScalar({v[3], v[4], v[5]}, 1000.0));
        if (primera.empty()) primera = fecha;
        ultima = fecha;
    }
    std::fclose(f);
    if (e.t.size() < 2) return false;
    e.fecha_ini = primera;
    e.fecha_fin = ultima;
    g_efem = std::move(e);
    return true;
}

bool interpolar(double t, lunar::Vec3& sol, lunar::Vec3& tierra) {
    const std::vector<double>& T = g_efem.t;
    if (T.size() < 2 || t < T.front() || t > T.back()) return false;
    // paso casi uniforme: estimar el indice y corregir
    std::size_t n = T.size();
    double dt = (T.back() - T.front()) / double(n - 1);
    std::size_t i = std::size_t(std::min(double(n - 2), std::max(0.0, std::floor((t - T.front()) / dt))));
    while (i > 0 && T[i] > t) --i;
    while (i + 2 < n && T[i + 1] < t) ++i;
    double w = (t - T[i]) / (T[i + 1] - T[i]);
    if (w < 0.0) w = 0.0;
    if (w > 1.0) w = 1.0;
    sol = vectorAddition(productByScalar(g_efem.sol[i], 1.0 - w), productByScalar(g_efem.sol[i + 1], w));
    tierra = vectorAddition(productByScalar(g_efem.tierra[i], 1.0 - w), productByScalar(g_efem.tierra[i + 1], w));
    return true;
}

// ------------------------------------------------------------------- sitios
struct Sitio {
    lunar::Vec3 P{};                  // observador (m, fijo a la Luna)
    lunar::Vec3 up{}, este{}, norte{};
    lunar::Vec3 normal{};             // normal unitaria (hacia el cielo) de la cara de apoyo
    bool con_dem = false;
    std::vector<float> horizonte;     // deg por bin de azimut (vacio = horizonte 0)
    double pendiente_deg = 0.0;
    double altitud_m = 0.0;
};
std::map<std::string, std::shared_ptr<Sitio>> g_cache;

lunar::Vec3 aMoonMe(double lat_deg, double lon_deg, double h) {
    const double la = lat_deg * RAD, lo = lon_deg * RAD, r = R_LUNA_M + h;
    return {r * std::cos(la) * std::cos(lo), r * std::cos(la) * std::sin(lo), r * std::sin(la)};
}

void fijarTriedro(Sitio& s) {
    s.up = normalize(s.P);
    lunar::Vec3 e = vectorCrossProduct({0.0, 0.0, 1.0}, s.up);  // hacia longitudes crecientes
    if (vectorNorm(e) < 1e-9) e = {0.0, 1.0, 0.0};              // polo exacto
    s.este = normalize(e);
    s.norte = vectorCrossProduct(s.up, s.este);
    s.normal = s.up;  // por defecto la cara de apoyo es el plano horizontal local
}

// Normal de la cara del parche sobre la que se apoya el sitio: el triangulo de
// la malla (misma triangulacion y orden de vertices que lunar::Patch::triangles())
// que tiene por vertice el nodo central. La normal se devuelve apuntando al cielo.
// Devuelve false si a esa cara le falta algun dato del DEM.
bool normalCaraApoyo(const lunar::Patch& p, int ci, int cj, const lunar::Vec3& up, lunar::Vec3& out) {
    if (ci < 0 || cj < 0 || ci + 1 >= p.rows || cj + 1 >= p.columns) return false;
    if (!p.valid(ci, cj) || !p.valid(ci + 1, cj) || !p.valid(ci, cj + 1)) return false;
    const Triangle cara = {p.point(ci, cj), p.point(ci + 1, cj), p.point(ci, cj + 1)};
    lunar::Vec3 n = normalVectorTriangle(cara);
    if (std::isnan(n.x) || std::isnan(n.y) || std::isnan(n.z)) return false;  // cara degenerada
    const double hacia_arriba = vectorDotProduct(n, up);
    if (hacia_arriba == 0.0) return false;
    if (hacia_arriba < 0.0) n = productByScalar(n, -1.0);                     // que mire al cielo
    out = n;
    return true;
}

void azimutElevacion(const Sitio& s, const lunar::Vec3& objetivo, double& az_deg, double& el_deg, double& dist) {
    lunar::Vec3 d = vectorSubstraction(objetivo, s.P);
    dist = vectorNorm(d);
    double u = vectorDotProduct(d, s.up);
    double e = vectorDotProduct(d, s.este);
    double n = vectorDotProduct(d, s.norte);
    az_deg = std::atan2(e, n) / RAD;
    if (az_deg < 0.0) az_deg += 360.0;
    el_deg = std::atan2(u, std::hypot(e, n)) / RAD;
}

inline int binDe(double az_deg) {
    double a = std::fmod(az_deg, 360.0);
    if (a < 0.0) a += 360.0;
    int b = int(a / ANCHO_BIN);
    return (b >= N_BINS) ? (N_BINS - 1) : b;
}

// Recorre la malla del parche y se queda, por cada azimut, con el angulo de
// elevacion maximo del terreno visto desde el observador. Las aristas entre
// nodos vecinos se rasterizan para no dejar huecos entre azimuts.
void acumularHorizonte(const lunar::Patch& p, const Sitio& s, std::vector<float>& hor) {
    const std::size_t n = std::size_t(p.rows) * std::size_t(p.columns);
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> az(n, NaN), el(n, NaN);
    const double dist_min = 1.5 * p.spacing_m;
    for (std::size_t q = 0; q < n; ++q) {
        if (std::isnan(p.h[q])) continue;
        lunar::Vec3 d = vectorSubstraction(p.xyz[q], s.P);
        double u = vectorDotProduct(d, s.up);
        double e = vectorDotProduct(d, s.este);
        double nn = vectorDotProduct(d, s.norte);
        double dh = std::hypot(e, nn);
        if (dh < dist_min) continue;
        double a = std::atan2(e, nn) / RAD;
        if (a < 0.0) a += 360.0;
        az[q] = float(a);
        el[q] = float(std::atan2(u, dh) / RAD);
    }
    auto arista = [&](std::size_t a, std::size_t b) {
        if (std::isnan(az[a]) || std::isnan(az[b])) return;
        double d = double(az[b]) - double(az[a]);
        if (d > 180.0) d -= 360.0;
        if (d < -180.0) d += 360.0;
        int pasos = int(std::ceil(std::fabs(d) / ANCHO_BIN));
        if (pasos < 1) pasos = 1;
        for (int k = 0; k <= pasos; ++k) {
            double w = double(k) / double(pasos);
            float e = float(double(el[a]) + (double(el[b]) - double(el[a])) * w);
            int bin = binDe(double(az[a]) + d * w);
            if (e > hor[std::size_t(bin)]) hor[std::size_t(bin)] = e;
        }
    };
    for (int i = 0; i < p.rows; ++i) {
        for (int j = 0; j < p.columns; ++j) {
            const std::size_t q = p.idx(i, j);
            if (j + 1 < p.columns) arista(q, p.idx(i, j + 1));
            if (i + 1 < p.rows) arista(q, p.idx(i + 1, j));
        }
    }
}

// Sitio con horizonte real del DEM. Devuelve nullptr si el punto no tiene dato.
std::shared_ptr<Sitio> construirSitioDem(double lat, double lon, double mastil_m) {
    if (!g_dem) return nullptr;
    try {
        const double r_cerca = std::max(50.0, envDouble("LUNAR_DEM_RADIO_CERCANO_M", 1000.0));
        const double r_lejos = envDouble("LUNAR_DEM_RADIO_LEJANO_M", 15000.0);
        const double sensor = std::max(0.0, envDouble("LUNAR_ALTURA_SENSOR_M", 2.0));
        lunar::Patch cerca = g_dem->region_lat_lon(lat, lon, r_cerca, 1);
        const int ci = cerca.center_row, cj = cerca.center_column;
        if (!cerca.valid(ci, cj)) return nullptr;
        auto s = std::make_shared<Sitio>();
        const double h0 = cerca.elevation(ci, cj);
        s->altitud_m = h0;
        s->P = aMoonMe(cerca.center_lat, cerca.center_lon, h0 + std::max(0.0, mastil_m) + sensor);
        fijarTriedro(*s);
        {
            lunar::Vec3 n{};
            if (normalCaraApoyo(cerca, ci, cj, s->up, n)) s->normal = n;
        }
        s->horizonte.assign(N_BINS, -90.0f);
        acumularHorizonte(cerca, *s, s->horizonte);
        if (r_lejos > r_cerca) {
            // ~500 nodos por lado como mucho
            int paso = std::max(1, int(std::ceil(r_lejos / (g_dem->pixel_m() * 250.0))));
            lunar::Patch lejos = g_dem->region_lat_lon(lat, lon, r_lejos, paso);
            acumularHorizonte(lejos, *s, s->horizonte);
        }
        for (float& v : s->horizonte)
            if (v <= -89.0f) v = 0.0f;
        // pendiente local por diferencias centrales
        if (ci > 0 && cj > 0 && ci + 1 < cerca.rows && cj + 1 < cerca.columns &&
            cerca.valid(ci, cj - 1) && cerca.valid(ci, cj + 1) &&
            cerca.valid(ci - 1, cj) && cerca.valid(ci + 1, cj)) {
            double dx = (cerca.elevation(ci, cj + 1) - cerca.elevation(ci, cj - 1)) / (2.0 * cerca.spacing_m);
            double dy = (cerca.elevation(ci + 1, cj) - cerca.elevation(ci - 1, cj)) / (2.0 * cerca.spacing_m);
            s->pendiente_deg = std::atan(std::hypot(dx, dy)) / RAD;
        }
        s->con_dem = true;
        return s;
    } catch (const std::exception&) {
        return nullptr;  // punto fuera del DEM (p. ej. latitud por encima de -87)
    }
}

// Sitio sin DEM: esfera de 1737.4 km + altitud, horizonte a 0 deg.
std::shared_ptr<Sitio> construirSitioEsfera(double lat, double lon, double alt_m) {
    auto s = std::make_shared<Sitio>();
    s->altitud_m = alt_m;
    s->con_dem = false;
    s->P = aMoonMe(lat, lon, alt_m);
    fijarTriedro(*s);
    return s;
}

// Devuelve el sitio (de la cache o recien calculado). Con DEM manda el DEM;
// si el punto no tiene dato en el DEM se cae a la esfera.
std::shared_ptr<Sitio> obtenerSitio(double lat, double lon, double alt_m, double mastil_m) {
    char clave[160];
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_cache.size() > 512) g_cache.clear();
    if (g_dem) {
        std::snprintf(clave, sizeof clave, "D|%.5f|%.5f|%.1f", lat, lon, mastil_m);
        auto it = g_cache.find(clave);
        std::shared_ptr<Sitio> s;
        if (it != g_cache.end()) {
            s = it->second;  // puede ser nullptr = "ya se probo y no hay dato"
        } else {
            s = construirSitioDem(lat, lon, mastil_m);
            g_cache[clave] = s;
        }
        if (s) return s;
    }
    std::snprintf(clave, sizeof clave, "E|%.5f|%.5f|%.1f", lat, lon, alt_m);
    auto it = g_cache.find(clave);
    if (it != g_cache.end()) return it->second;
    auto s = construirSitioEsfera(lat, lon, alt_m);
    g_cache[clave] = s;
    return s;
}

double horizonteEn(const Sitio& s, double az_deg) {
    if (s.horizonte.empty()) return 0.0;
    return double(s.horizonte[std::size_t(binDe(az_deg))]);
}

std::string fechaGmatAIso(const std::string& g) {
    // "01 Jan 2027 00:00:00.000" -> "2027-01-01 00:00"
    static const char* MESES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int d = 0, y = 0, hh = 0, mm = 0;
    char mes[8] = {0};
    if (std::sscanf(g.c_str(), "%d %3s %d %d:%d", &d, mes, &y, &hh, &mm) != 5) return g;
    int m = 0;
    for (int k = 0; k < 12; ++k)
        if (std::strcmp(mes, MESES[k]) == 0) m = k + 1;
    char b[32];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", y, m, d, hh, mm);
    return b;
}

}  // namespace

// ============================================================== API publica
void inicializar(const std::string& ruta_efemerides, const std::string& ruta_dem) {
    if (!ruta_efemerides.empty() && cargarEfemerides(ruta_efemerides)) {
        std::printf("[OK] Efemerides reales: %zu instantes (%s -> %s) de %s\n", g_efem.t.size(),
                    g_efem.fecha_ini.c_str(), g_efem.fecha_fin.c_str(), ruta_efemerides.c_str());
    } else {
        std::printf("[AVISO] Sin efemerides (%s): se usa el modelo simulado.\n", ruta_efemerides.c_str());
    }
    if (!ruta_dem.empty()) {
        try {
            g_dem.reset(new lunar::Dem(ruta_dem));
            g_ruta_dem = ruta_dem;
            std::printf("[OK] DEM abierto: %s\n%s\n", ruta_dem.c_str(), g_dem->info().c_str());
        } catch (const std::exception& e) {
            g_dem.reset();
            std::printf("[AVISO] Sin DEM (%s): horizonte esferico (0 deg). Motivo: %s\n", ruta_dem.c_str(), e.what());
        }
    } else {
        std::printf("[AVISO] Sin DEM: horizonte esferico (0 deg).\n");
    }
    std::fflush(stdout);
}

bool efemeridesCargadas() { return g_efem.t.size() >= 2; }
bool demCargado() { return bool(g_dem); }

bool cubre(double dias_j2000) {
    if (!efemeridesCargadas()) return false;
    double t = (dias_j2000 - DIAS_J2000_EPOCA) * 86400.0;
    return t >= g_efem.t.front() && t <= g_efem.t.back();
}

bool demCubre(double lat_deg, double lon_deg) {
    if (!g_dem) return false;
    return obtenerSitio(lat_deg, lon_deg, 0.0, 0.0)->con_dem;
}

double fraccionDiscoVisible(double elev_sobre_horizonte_deg, double radio_ang_deg) {
    if (radio_ang_deg <= 0.0) return elev_sobre_horizonte_deg >= 0.0 ? 1.0 : 0.0;
    double x = elev_sobre_horizonte_deg / radio_ang_deg;
    if (x >= 1.0) return 1.0;
    if (x <= -1.0) return 0.0;
    return (std::acos(-x) + x * std::sqrt(1.0 - x * x)) / 3.14159265358979323846;
}

bool calcular(double lat_deg, double lon_deg, double alt_m, double mastil_m, double dias_j2000, Geometria& out) {
    if (!efemeridesCargadas()) return false;
    lunar::Vec3 sol, tierra;
    if (!interpolar((dias_j2000 - DIAS_J2000_EPOCA) * 86400.0, sol, tierra)) return false;
    std::shared_ptr<Sitio> s = obtenerSitio(lat_deg, lon_deg, alt_m, mastil_m);

    azimutElevacion(*s, sol, out.az_sol_deg, out.elev_sol_deg, out.dist_sol_m);
    out.horizonte_sol_deg = horizonteEn(*s, out.az_sol_deg);
    double r_sol = std::asin(R_SOL_M / out.dist_sol_m) / RAD;
    out.frac_sol = fraccionDiscoVisible(out.elev_sol_deg - out.horizonte_sol_deg, r_sol);
    // Angulo de incidencia de la luz sobre la cara de apoyo. La iluminacion es
    // proporcional a su coseno: 1 con luz normal a la cara, 0 con luz rasante.
    {
        const lunar::Vec3 luz = normalize(vectorSubstraction(sol, s->P));
        double c = vectorDotProduct(luz, s->normal);
        if (c > 1.0) c = 1.0;
        if (c < -1.0) c = -1.0;
        out.incidencia_sol_deg = std::acos(c) / RAD;
        out.cos_incidencia = (c > 0.0) ? c : 0.0;
    }

    azimutElevacion(*s, tierra, out.az_tierra_deg, out.elev_tierra_deg, out.dist_tierra_m);
    out.horizonte_tierra_deg = horizonteEn(*s, out.az_tierra_deg);
    out.radio_ang_tierra_deg = std::asin(R_TIERRA_M / out.dist_tierra_m) / RAD;
    out.con_dem = s->con_dem;
    return true;
}

bool topografia(double lat_deg, double lon_deg, double& out_pendiente_deg, double& out_altitud_m) {
    if (!g_dem) return false;
    std::shared_ptr<Sitio> s = obtenerSitio(lat_deg, lon_deg, 0.0, 0.0);
    if (!s->con_dem) return false;
    out_pendiente_deg = s->pendiente_deg;
    out_altitud_m = s->altitud_m;
    return true;
}

bool mejorVentana(const std::vector<std::string>& fechas, const std::vector<double>& valores,
                  int n_objetivo, int margen, int& out_i0, int& out_i1) {
    const std::size_t m = std::min(fechas.size(), valores.size());
    if (m == 0) return false;
    std::vector<DataPoint> datos;
    datos.reserve(m);
    for (std::size_t k = 0; k < m; ++k) datos.push_back({fechas[k], valores[k]});
    if (n_objetivo - margen > int(m)) {  // el intervalo pedido no cabe: usar toda la serie
        n_objetivo = int(m);
        margen = 0;
    }
    std::optional<AverageRange> r = findMaximumAverageRange(datos, n_objetivo, margen);
    if (!r.has_value()) return false;
    out_i0 = int(r->startIndex);
    out_i1 = int(r->endIndex);
    return true;
}

void precalentar(double lat_deg, double lon_deg) {
    if (g_dem) obtenerSitio(lat_deg, lon_deg, 0.0, 0.0);
}

std::string descripcion() {
    if (!efemeridesCargadas()) return "C++17 - modelo simulado (sin efemerides)";
    std::string d = "C++17 + efemerides reales GMAT (" + coberturaInicioIso().substr(0, 10) + " a " +
                    coberturaFinIso().substr(0, 10) + ")";
    d += g_dem ? " + DEM LOLA" : " - sin DEM (horizonte esferico)";
    return d;
}

std::string coberturaInicioIso() { return efemeridesCargadas() ? fechaGmatAIso(g_efem.fecha_ini) : ""; }
std::string coberturaFinIso() { return efemeridesCargadas() ? fechaGmatAIso(g_efem.fecha_fin) : ""; }

}  // namespace MotorReal
