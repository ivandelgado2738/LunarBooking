// ============================================================================
// ARCHIVO: servidor_lunar_booking.cpp
// PROYECTO: NASA Lunar Booking & Mission Planner (Servidor Web C++ Nativo)
// COMPILACION ESTATICA (100% independiente sin DLLs externas):
//   Windows (MSYS2):
//     g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ servidor_lunar_booking.cpp motor_real.cpp fisica/algebra.cpp fisica/maximum_average.cpp -o servidor_lunar_booking.exe -lws2_32 -lshell32
//   Linux / Docker:
//     g++ -std=c++17 -O2 -static -pthread servidor_lunar_booking.cpp motor_real.cpp fisica/algebra.cpp fisica/maximum_average.cpp -o servidor_lunar_booking
// VARIABLES DE ENTORNO (todas opcionales):
//   LUNAR_HOST        IP de escucha (127.0.0.1 por defecto; 0.0.0.0 en Docker)
//   LUNAR_PORT        Puerto (8085 por defecto)
//   LUNAR_WEB_DIR     Carpeta con index.html, style.css, img/ ...
//   LUNAR_EFEMERIDES  Ruta de sun_earth_ephemerids.txt
//   LUNAR_DEM         Ruta de ldem_87s_5mpp.tif (si no existe se trabaja sin DEM)
// USO:
//   servidor_lunar_booking.exe          -> Inicia servidor HTTP y abre navegador
//   servidor_lunar_booking.exe --test   -> Ejecuta bateria de pruebas C++ y sale
// ============================================================================

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#else
// --- Linux / Docker: sockets POSIX con los mismos nombres que WinSock ---
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <csignal>
#include <climits>
#include <thread>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define SD_SEND SHUT_WR
inline int closesocket(SOCKET s) { return ::close(s); }
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <iomanip>
#include <cstdlib>
#include <filesystem>
#include <memory>

#include "motor_datos_lunares.h"

namespace {

std::string leerEntorno(const char* nombre, const std::string& defecto = "") {
    const char* v = std::getenv(nombre);
    return (v && *v) ? std::string(v) : defecto;
}

std::filesystem::path obtenerDirectorioEjecutable() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, buffer, MAX_PATH);
    if (len == 0) return std::filesystem::current_path();
    return std::filesystem::path(std::wstring(buffer, len)).parent_path();
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0) return std::filesystem::current_path();
    return std::filesystem::path(std::string(buffer, static_cast<size_t>(len))).parent_path();
#endif
}

// Primera ruta existente de la lista (o "" si ninguna existe)
std::string primeraRutaExistente(const std::vector<std::filesystem::path>& candidatas) {
    for (const auto& c : candidatas) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(c, ec)) return c.string();
    }
    return "";
}

// Conecta el motor fisico real (efemerides + DEM) con el motor de la web
void inicializarMotorReal(const std::filesystem::path& dir_exe) {
    std::string efem = leerEntorno("LUNAR_EFEMERIDES");
    if (efem.empty()) {
        efem = primeraRutaExistente({
            dir_exe / "datos" / "sun_earth_ephemerids.txt",
            dir_exe / "sun_earth_ephemerids.txt",
            std::filesystem::current_path() / "datos" / "sun_earth_ephemerids.txt",
            std::filesystem::current_path() / "sun_earth_ephemerids.txt",
        });
    }
    std::string dem = leerEntorno("LUNAR_DEM");
    if (!dem.empty()) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(dem, ec)) dem.clear();
    }
    if (dem.empty()) {
        dem = primeraRutaExistente({
            dir_exe / "datos" / "ldem_87s_5mpp.tif",
            dir_exe / "ldem_87s_5mpp.tif",
            dir_exe / "datos" / "ldem_87_5mpp.tif",
            dir_exe / "ldem_87_5mpp.tif",
            std::filesystem::current_path() / "datos" / "ldem_87s_5mpp.tif",
            std::filesystem::current_path() / "ldem_87s_5mpp.tif",
        });
    }
    MotorReal::inicializar(efem, dem);
}

bool enviarTodo(SOCKET sock, const std::string& datos) {
    size_t total = datos.size();
    size_t enviados = 0;
    while (enviados < total) {
        int n = send(
            sock,
            datos.data() + enviados,
            static_cast<int>(std::min<size_t>(65536, total - enviados)),
#ifdef _WIN32
            0
#else
            MSG_NOSIGNAL
#endif
        );
        if (n <= 0) return false;
        enviados += static_cast<size_t>(n);
    }
    return true;
}

void cerrarSocketLimpio(SOCKET sock) {
    shutdown(sock, SD_SEND);
    closesocket(sock);
}

std::string escaparJson(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b";  break;
            case '\f': o << "\\f";  break;
            case '\n': o << "\\n";  break;
            case '\r': o << "\\r";  break;
            case '\t': o << "\\t";  break;
            default:   o << c;      break;
        }
    }
    return o.str();
}

// Extractor ligero de campos JSON planos (strings y numeros) sin dependencias externas
std::string extraerJsonString(const std::string& json, const std::string& clave, const std::string& defecto = "") {
    std::string patron = "\"" + clave + "\"";
    size_t pos = json.find(patron);
    if (pos == std::string::npos) return defecto;
    pos = json.find(':', pos + patron.size());
    if (pos == std::string::npos) return defecto;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;
    if (pos >= json.size()) return defecto;
    if (json[pos] == '"') {
        pos++;
        std::ostringstream out;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++;
            }
            out << json[pos++];
        }
        return out.str();
    }
    size_t fin = pos;
    while (fin < json.size() && json[fin] != ',' && json[fin] != '}' && json[fin] != ' ' && json[fin] != '\r' && json[fin] != '\n') {
        fin++;
    }
    std::string raw = json.substr(pos, fin - pos);
    if (raw == "undefined" || raw == "null") return defecto;
    return raw;
}

double extraerJsonDouble(const std::string& json, const std::string& clave, double defecto = 0.0) {
    std::string val = extraerJsonString(json, clave, "");
    if (val.empty()) return defecto;
    try {
        return std::stod(val);
    } catch (...) {
        return defecto;
    }
}

int extraerJsonInt(const std::string& json, const std::string& clave, int defecto = 0) {
    std::string val = extraerJsonString(json, clave, "");
    if (val.empty()) return defecto;
    try {
        return std::stoi(val);
    } catch (...) {
        return defecto;
    }
}

LunarBooking::CoordenadaLunar resolverPuntoDesdeJson(
    const std::string& body,
    const std::string& sufijo = ""
) {
    double mastil_m = extraerJsonDouble(body, "altura_mastil_m", 0.0);
    auto catalogo = LunarBooking::obtenerCatalogoPuntosLunares();
    std::string id = extraerJsonString(body, "id_punto" + sufijo, "");
    for (auto p : catalogo) {
        if (p.id == id) {
            p.altitud_m += mastil_m;
            return p;
        }
    }

    LunarBooking::CoordenadaLunar custom{};
    custom.id = id.empty() ? ("custom" + sufijo) : id;
    custom.nombre = extraerJsonString(body, "nombre" + sufijo, "Custom Lunar Coordinate");
    custom.region = "User-Selected Coordinate";

    std::string tipo_coord = extraerJsonString(body, "tipo_coord" + sufijo, "SELENOGRAFICA");
    if (tipo_coord == "ESFERICAS" && !extraerJsonString(body, "theta_deg" + sufijo, "").empty()) {
        double r_km = extraerJsonDouble(body, "r_km" + sufijo, 1738.9);
        double theta_deg = extraerJsonDouble(body, "theta_deg" + sufijo, 179.5);
        double phi_deg = extraerJsonDouble(body, "phi_deg" + sufijo, 45.0);
        LunarBooking::esfericas_a_selenograficas(
            r_km, theta_deg, phi_deg,
            custom.latitud_deg, custom.longitud_deg, custom.altitud_m
        );
        custom.altitud_m += mastil_m;
        LunarBooking::latlon_a_estereografica(
            custom.latitud_deg, custom.longitud_deg,
            custom.x_mapa_m, custom.y_mapa_m
        );
    } else {
        custom.latitud_deg = extraerJsonDouble(body, "latitud_deg" + sufijo, -89.5);
        custom.longitud_deg = extraerJsonDouble(body, "longitud_deg" + sufijo, 45.0);
        custom.altitud_m = extraerJsonDouble(body, "altitud_m" + sufijo, 1500.0) + mastil_m;

        if (!extraerJsonString(body, "x_mapa_m" + sufijo, "").empty()) {
            custom.x_mapa_m = extraerJsonDouble(body, "x_mapa_m" + sufijo, 0.0);
            custom.y_mapa_m = extraerJsonDouble(body, "y_mapa_m" + sufijo, 0.0);
            if (extraerJsonString(body, "latitud_deg" + sufijo, "").empty()) {
                LunarBooking::estereografica_a_latlon(
                    custom.x_mapa_m, custom.y_mapa_m,
                    custom.latitud_deg, custom.longitud_deg
                );
            }
        } else {
            LunarBooking::latlon_a_estereografica(
                custom.latitud_deg, custom.longitud_deg,
                custom.x_mapa_m, custom.y_mapa_m
            );
        }
    }
    custom.descripcion_corta = "Lunar point defined by exact mission coordinates.";
    return custom;
}

std::string serializarEvaluacionJson(const LunarBooking::EvaluacionLocalizacion& ev) {
    double r_km = 0.0, theta_deg = 180.0, phi_deg = 0.0;
    double x3_km = 0.0, y3_km = 0.0, z3_km = 0.0;
    LunarBooking::selenograficas_a_esfericas(
        ev.punto.latitud_deg, ev.punto.longitud_deg, ev.punto.altitud_m,
        r_km, theta_deg, phi_deg
    );
    LunarBooking::esfericas_a_cartesianas_3d(r_km, theta_deg, phi_deg, x3_km, y3_km, z3_km);
    std::ostringstream o;
    o << std::fixed << std::setprecision(4);
    o << "{"
      << "\"punto\":{"
      << "\"id\":\"" << escaparJson(ev.punto.id) << "\","
      << "\"nombre\":\"" << escaparJson(ev.punto.nombre) << "\","
      << "\"region\":\"" << escaparJson(ev.punto.region) << "\","
      << "\"latitud_deg\":" << ev.punto.latitud_deg << ","
      << "\"longitud_deg\":" << ev.punto.longitud_deg << ","
      << "\"x_mapa_m\":" << ev.punto.x_mapa_m << ","
      << "\"y_mapa_m\":" << ev.punto.y_mapa_m << ","
      << "\"altitud_m\":" << ev.punto.altitud_m << ","
      << "\"r_km\":" << r_km << ","
      << "\"theta_deg\":" << theta_deg << ","
      << "\"phi_deg\":" << phi_deg << ","
      << "\"x_3d_km\":" << x3_km << ","
      << "\"y_3d_km\":" << y3_km << ","
      << "\"z_3d_km\":" << z3_km << ","
      << "\"descripcion\":\"" << escaparJson(ev.punto.descripcion_corta) << "\""
      << "},"
      << "\"media_iluminacion_pct\":" << ev.media_iluminacion_pct << ","
      << "\"max_elevacion_solar_deg\":" << ev.max_elevacion_solar_deg << ","
      << "\"min_elevacion_solar_deg\":" << ev.min_elevacion_solar_deg << ","
      << "\"media_comunicacion_pct\":" << ev.media_comunicacion_pct << ","
      << "\"media_elevacion_tierra_deg\":" << ev.media_elevacion_tierra_deg << ","
      << "\"pendiente_terreno_deg\":" << ev.pendiente_terreno_deg << ","
      << "\"temperatura_media_k\":" << ev.temperatura_media_k << ","
      << "\"media_potencia_kw\":" << ev.media_potencia_kw << ","
      << "\"min_bateria_soc_pct\":" << ev.min_bateria_soc_pct << ","
      << "\"puntuacion_booking_10\":" << ev.puntuacion_booking_10 << ","
      << "\"cumple_todos_los_filtros\":" << (ev.cumple_todos_los_filtros ? "true" : "false") << ","
      << "\"etiqueta_valoracion\":\"" << escaparJson(ev.etiqueta_valoracion) << "\","
      << "\"fuente_datos\":\"" << escaparJson(ev.fuente_datos) << "\","
      << "\"mejor_ventana\":{"
      << "\"encontrada\":" << (ev.mejor_ventana.encontrada ? "true" : "false") << ","
      << "\"fecha_inicio_optima\":\"" << escaparJson(ev.mejor_ventana.fecha_inicio_optima) << "\","
      << "\"fecha_fin_optima\":\"" << escaparJson(ev.mejor_ventana.fecha_fin_optima) << "\","
      << "\"num_muestras\":" << ev.mejor_ventana.num_muestras << ","
      << "\"duracion_horas\":" << ev.mejor_ventana.duracion_horas << ","
      << "\"media_maxima_valor\":" << ev.mejor_ventana.media_maxima_valor << ","
      << "\"media_iluminacion_pct\":" << ev.mejor_ventana.media_iluminacion_pct << ","
      << "\"media_comunicacion_pct\":" << ev.mejor_ventana.media_comunicacion_pct << ","
      << "\"media_elevacion_solar_deg\":" << ev.mejor_ventana.media_elevacion_solar_deg << ","
      << "\"media_elevacion_tierra_deg\":" << ev.mejor_ventana.media_elevacion_tierra_deg << ","
      << "\"media_potencia_kw\":" << ev.mejor_ventana.media_potencia_kw << ","
      << "\"cumple_filtros\":" << (ev.mejor_ventana.cumple_filtros ? "true" : "false")
      << "},"
      << "\"top_ventanas_fechas\":[";

    for (size_t i = 0; i < ev.top_ventanas_fechas.size(); ++i) {
        const auto& v = ev.top_ventanas_fechas[i];
        if (i > 0) o << ",";
        o << "{"
          << "\"fecha_inicio_optima\":\"" << escaparJson(v.fecha_inicio_optima) << "\","
          << "\"fecha_fin_optima\":\"" << escaparJson(v.fecha_fin_optima) << "\","
          << "\"duracion_horas\":" << v.duracion_horas << ","
          << "\"media_maxima_valor\":" << v.media_maxima_valor << ","
          << "\"media_iluminacion_pct\":" << v.media_iluminacion_pct << ","
          << "\"media_comunicacion_pct\":" << v.media_comunicacion_pct
          << "}";
    }
    o << "],\"serie_temporal\":[";
    for (size_t i = 0; i < ev.serie_temporal.size(); ++i) {
        const auto& s = ev.serie_temporal[i];
        if (i > 0) o << ",";
        o << "{"
          << "\"indice\":" << s.indice << ","
          << "\"fecha\":\"" << escaparJson(s.fecha_hora_iso) << "\","
          << "\"elevacion_solar_deg\":" << s.elevacion_solar_deg << ","
          << "\"horizonte_topografico_deg\":" << s.horizonte_topografico_deg << ","
          << "\"azimut_solar_deg\":" << s.azimut_solar_deg << ","
          << "\"iluminacion_pct\":" << s.iluminacion_pct << ","
          << "\"irradiancia_wm2\":" << s.irradiancia_wm2 << ","
          << "\"elevacion_tierra_deg\":" << s.elevacion_tierra_deg << ","
          << "\"azimut_tierra_deg\":" << s.azimut_tierra_deg << ","
          << "\"comunicacion_dte_pct\":" << s.comunicacion_dte_pct << ","
          << "\"comunicacion_rele_pct\":" << s.comunicacion_rele_pct << ","
          << "\"comunicacion_efectiva\":" << s.comunicacion_efectiva << ","
          << "\"temperatura_k\":" << s.temperatura_k << ","
          << "\"potencia_generada_kw\":" << s.potencia_generada_kw << ","
          << "\"bateria_soc_pct\":" << s.bateria_soc_pct
          << "}";
    }
    o << "]}";
    return o.str();
}

// === [PUNTO_INTEGRACION_08] : DESPACHADOR DE ENDPOINTS HTTP DE DATOS ===
std::string procesarEndpointApi(const std::string& ruta, const std::string& body) {
    if (ruta == "/api/catalogo") {
        auto cat = LunarBooking::obtenerCatalogoPuntosLunares();
        std::ostringstream o;
        o << std::fixed << std::setprecision(4);
        o << "{\"ok\":true,\"motor\":\"" << escaparJson(MotorReal::descripcion()) << "\","
          << "\"efemerides_inicio\":\"" << escaparJson(MotorReal::coberturaInicioIso()) << "\","
          << "\"efemerides_fin\":\"" << escaparJson(MotorReal::coberturaFinIso()) << "\","
          << "\"dem\":" << (MotorReal::demCargado() ? "true" : "false") << ","
          << "\"catalogo\":[";
        for (size_t i = 0; i < cat.size(); ++i) {
            if (i > 0) o << ",";
            o << "{"
              << "\"id\":\"" << escaparJson(cat[i].id) << "\","
              << "\"nombre\":\"" << escaparJson(cat[i].nombre) << "\","
              << "\"region\":\"" << escaparJson(cat[i].region) << "\","
              << "\"latitud_deg\":" << cat[i].latitud_deg << ","
              << "\"longitud_deg\":" << cat[i].longitud_deg << ","
              << "\"x_mapa_m\":" << cat[i].x_mapa_m << ","
              << "\"y_mapa_m\":" << cat[i].y_mapa_m << ","
              << "\"altitud_m\":" << cat[i].altitud_m << ","
              << "\"descripcion\":\"" << escaparJson(cat[i].descripcion_corta) << "\""
              << "}";
        }
        o << "]}";
        return o.str();
    }

    if (ruta == "/api/estado") {
        std::ostringstream o;
        o << "{\"ok\":true,\"motor\":\"" << escaparJson(MotorReal::descripcion()) << "\","
          << "\"efemerides\":" << (MotorReal::efemeridesCargadas() ? "true" : "false") << ","
          << "\"efemerides_inicio\":\"" << escaparJson(MotorReal::coberturaInicioIso()) << "\","
          << "\"efemerides_fin\":\"" << escaparJson(MotorReal::coberturaFinIso()) << "\","
          << "\"dem\":" << (MotorReal::demCargado() ? "true" : "false") << "}";
        return o.str();
    }

    LunarBooking::FiltrosMision filtros{};
    filtros.iluminacion_min_pct      = extraerJsonDouble(body, "iluminacion_min_pct", 50.0);
    filtros.elevacion_solar_min_deg  = extraerJsonDouble(body, "elevacion_solar_min_deg", 0.0);
    filtros.comunicacion_min_pct     = extraerJsonDouble(body, "comunicacion_min_pct", 50.0);
    filtros.elevacion_tierra_min_deg = extraerJsonDouble(body, "elevacion_tierra_min_deg", 0.0);
    filtros.modo_enlace              = extraerJsonString(body, "modo_enlace", "DTE");
    filtros.pendiente_max_deg        = extraerJsonDouble(body, "pendiente_max_deg", 15.0);
    filtros.prioridad_optimizacion   = extraerJsonString(body, "prioridad_optimizacion", "EQUILIBRADO");
    filtros.altura_mastil_m          = extraerJsonDouble(body, "altura_mastil_m", 0.0);
    filtros.banda_com                = extraerJsonString(body, "banda_com", "X");
    filtros.diam_antena_m            = extraerJsonDouble(body, "diam_antena_m", 1.2);
    filtros.area_paneles_m2          = extraerJsonDouble(body, "area_paneles_m2", 40.0);
    filtros.eficiencia_solar_pct     = extraerJsonDouble(body, "eficiencia_solar_pct", 32.0);
    filtros.consumo_hab_kw           = extraerJsonDouble(body, "consumo_hab_kw", 3.5);

    if (ruta == "/api/buscar") {
        double mastil_m = filtros.altura_mastil_m;
        bool sin_lugar = (extraerJsonString(body, "sin_lugar_fijo", "false") == "true") ||
                         (extraerJsonString(body, "id_punto", "") == "NINGUNO");
        bool sin_fecha = (extraerJsonString(body, "sin_fecha_fija", "false") == "true");

        std::string modo_busqueda = "AMBAS";
        if (sin_lugar && sin_fecha) modo_busqueda = "SIN_ESPECIFICAR";
        else if (sin_lugar) modo_busqueda = "SOLO_FECHAS";
        else if (sin_fecha) modo_busqueda = "SOLO_LUGAR";

        LunarBooking::RangoFechas fechas{};
        if (sin_fecha) {
            // Horizonte de exploracion semestral (6 lunaciones) para recomendar las mejores fechas
            fechas.fecha_inicio_iso = "2027-01-01T00:00";
            fechas.fecha_fin_iso    = "2027-07-01T00:00";
            double paso_req = std::max(0.25, extraerJsonDouble(body, "paso_horas", 6.0));
            fechas.paso_horas = std::max(6.0, paso_req); // Mantiene <= 730 muestras para velocidad y claridad estacional
        } else {
            fechas.fecha_inicio_iso = extraerJsonString(body, "fecha_inicio", "2027-01-01T00:00");
            fechas.fecha_fin_iso    = extraerJsonString(body, "fecha_fin", "2027-01-31T00:00");
            fechas.paso_horas       = std::max(0.25, extraerJsonDouble(body, "paso_horas", 6.0));
        }
        fechas.n_intervalo_deseado = std::max(1, extraerJsonInt(body, "n_intervalo_deseado", 16));
        fechas.margen_intervalo    = std::max(0, extraerJsonInt(body, "margen_intervalo", 4));

        auto catalogo = LunarBooking::obtenerCatalogoPuntosLunares();
        std::vector<LunarBooking::EvaluacionLocalizacion> evals_catalogo;
        evals_catalogo.reserve(catalogo.size());
        for (size_t i = 0; i < catalogo.size(); ++i) {
            auto p_cat = catalogo[i];
            p_cat.altitud_m += mastil_m;
            evals_catalogo.push_back(LunarBooking::evaluarLocalizacionLunar(p_cat, fechas, filtros));
        }

        LunarBooking::EvaluacionLocalizacion eval_principal{};
        if (sin_lugar) {
            // Elegir automaticamente el mejor emplazamiento (priorizando los que cumplen filtros y mayor nota)
            size_t idx_best = 0;
            double best_rank = -1.0;
            for (size_t i = 0; i < evals_catalogo.size(); ++i) {
                double rank = evals_catalogo[i].puntuacion_booking_10 +
                              (evals_catalogo[i].cumple_todos_los_filtros ? 100.0 : 0.0);
                if (rank > best_rank) {
                    best_rank = rank;
                    idx_best = i;
                }
            }
            eval_principal = evals_catalogo[idx_best];
        } else {
            LunarBooking::CoordenadaLunar punto = resolverPuntoDesdeJson(body, "");
            eval_principal = LunarBooking::evaluarLocalizacionLunar(punto, fechas, filtros);
        }

        std::ostringstream o;
        o << "{\"ok\":true,\"modo_busqueda\":\"" << modo_busqueda
          << "\",\"evaluacion_principal\":" << serializarEvaluacionJson(eval_principal)
          << ",\"ofertas_recomendadas\":[";

        for (size_t i = 0; i < evals_catalogo.size(); ++i) {
            if (i > 0) o << ",";
            o << serializarEvaluacionJson(evals_catalogo[i]);
        }
        o << "]}";
        return o.str();
    }

    if (ruta == "/api/comparar") {
        std::string modo = extraerJsonString(body, "modo_comparacion", "SITIOS_FECHAS_PARAMETROS");
        LunarBooking::CoordenadaLunar punto_a = resolverPuntoDesdeJson(body, "_a");
        LunarBooking::CoordenadaLunar punto_b = resolverPuntoDesdeJson(body, "_b");

        LunarBooking::RangoFechas fechas_a{};
        fechas_a.fecha_inicio_iso    = extraerJsonString(body, "fecha_inicio_a", "2027-01-01T00:00");
        fechas_a.fecha_fin_iso       = extraerJsonString(body, "fecha_fin_a", "2027-01-31T00:00");
        fechas_a.paso_horas          = std::max(0.25, extraerJsonDouble(body, "paso_horas", 6.0));
        fechas_a.n_intervalo_deseado = std::max(1, extraerJsonInt(body, "n_intervalo_deseado", 16));
        fechas_a.margen_intervalo    = std::max(0, extraerJsonInt(body, "margen_intervalo", 4));

        LunarBooking::RangoFechas fechas_b = fechas_a;
        fechas_b.fecha_inicio_iso = extraerJsonString(body, "fecha_inicio_b", fechas_a.fecha_inicio_iso);
        fechas_b.fecha_fin_iso    = extraerJsonString(body, "fecha_fin_b", fechas_a.fecha_fin_iso);

        if (modo == "FECHAS_POR_FILTROS") {
            if (extraerJsonString(body, "mismo_lugar_fechas", "false") == "true") {
                punto_b = punto_a;
            }
        }

        auto comp = LunarBooking::compararLocalizacionesYFechas(
            punto_a, fechas_a, punto_b, fechas_b, filtros, modo
        );

        std::ostringstream o;
        o << "{"
          << "\"ok\":true,"
          << "\"modo_comparacion\":\"" << escaparJson(comp.modo_comparacion) << "\","
          << "\"ganador_iluminacion\":\"" << escaparJson(comp.ganador_iluminacion) << "\","
          << "\"ganador_comunicacion\":\"" << escaparJson(comp.ganador_comunicacion) << "\","
          << "\"ganador_global\":\"" << escaparJson(comp.ganador_global) << "\","
          << "\"recomendacion_sintesis\":\"" << escaparJson(comp.recomendacion_sintesis) << "\","
          << "\"eval_a\":" << serializarEvaluacionJson(comp.eval_a) << ","
          << "\"eval_b\":" << serializarEvaluacionJson(comp.eval_b)
          << "}";
        return o.str();
    }

    return "{\"ok\":false,\"error\":\"Endpoint not recognized by the C++ server\"}";
}

std::string obtenerMimeType(const std::string& ruta) {
    if (ruta.rfind(".html") != std::string::npos) return "text/html; charset=utf-8";
    if (ruta.rfind(".css")  != std::string::npos) return "text/css; charset=utf-8";
    if (ruta.rfind(".js")   != std::string::npos) return "application/javascript; charset=utf-8";
    if (ruta.rfind(".json") != std::string::npos) return "application/json; charset=utf-8";
    if (ruta.rfind(".png")  != std::string::npos) return "image/png";
    if (ruta.rfind(".jpg")  != std::string::npos || ruta.rfind(".jpeg") != std::string::npos) return "image/jpeg";
    if (ruta.rfind(".svg")  != std::string::npos) return "image/svg+xml";
    if (ruta.rfind(".pdf")  != std::string::npos) return "application/pdf";
    if (ruta.rfind(".txt")  != std::string::npos) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

void manejarClienteHttp(SOCKET client_sock, const std::filesystem::path& base_dir) {
    std::string request;
    char buf[8192];
    int recibidos = recv(client_sock, buf, sizeof(buf) - 1, 0);
    if (recibidos <= 0) {
        closesocket(client_sock);
        return;
    }
    buf[recibidos] = '\0';
    request.assign(buf, recibidos);

    // Las cabeceras pueden llegar troceadas (proxy / red de Docker): leer hasta el fin de cabeceras
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 65536) {
        int r = recv(client_sock, buf, sizeof(buf), 0);
        if (r <= 0) break;
        request.append(buf, r);
    }

    size_t header_end = request.find("\r\n\r\n");
    size_t content_len = 0;
    if (header_end != std::string::npos) {
        std::string headers = request.substr(0, header_end);
        size_t cl_pos = headers.find("Content-Length:");
        if (cl_pos == std::string::npos) cl_pos = headers.find("content-length:");
        if (cl_pos != std::string::npos) {
            size_t val_start = headers.find(':', cl_pos) + 1;
            size_t val_end = headers.find("\r\n", val_start);
            try {
                content_len = static_cast<size_t>(std::stoul(headers.substr(val_start, val_end - val_start)));
            } catch (...) {}
        }
        while (request.size() < header_end + 4 + content_len) {
            int r = recv(client_sock, buf, sizeof(buf), 0);
            if (r <= 0) break;
            request.append(buf, r);
        }
    }

    std::istringstream iss(request);
    std::string metodo, url, version;
    iss >> metodo >> url >> version;

    // Soporte preflight CORS para llamadas desde file:// o localhost
    if (metodo == "OPTIONS") {
        std::string out =
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Connection: close\r\n\r\n";
        enviarTodo(client_sock, out);
        cerrarSocketLimpio(client_sock);
        return;
    }

    size_t qpos = url.find('?');
    std::string ruta_limpia = (qpos == std::string::npos) ? url : url.substr(0, qpos);
    std::string body = (header_end != std::string::npos) ? request.substr(header_end + 4) : "";

    if (ruta_limpia.rfind("/api/", 0) == 0) {
        std::string json_resp = procesarEndpointApi(ruta_limpia, body);
        std::ostringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Cache-Control: no-store\r\n"
             << "Content-Length: " << json_resp.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << json_resp;
        enviarTodo(client_sock, resp.str());
        cerrarSocketLimpio(client_sock);
        return;
    }

    if (ruta_limpia == "/" || ruta_limpia.empty()) {
        ruta_limpia = "/index.html";
    }
    if (ruta_limpia.find("..") != std::string::npos) {
        cerrarSocketLimpio(client_sock);
        return;
    }

    std::string nombre_rel = ruta_limpia;
    while (!nombre_rel.empty() && (nombre_rel[0] == '/' || nombre_rel[0] == '\\')) {
        nombre_rel.erase(nombre_rel.begin());
    }
    std::filesystem::path ruta_archivo = base_dir / nombre_rel;

    std::ifstream f(ruta_archivo, std::ios::binary);
    if (!f) {
        std::string msg = "404 File not found";
        std::ostringstream resp;
        resp << "HTTP/1.1 404 Not Found\r\n"
             << "Content-Type: text/plain; charset=utf-8\r\n"
             << "Content-Length: " << msg.size() << "\r\n"
             << "Connection: close\r\n\r\n" << msg;
        enviarTodo(client_sock, resp.str());
        cerrarSocketLimpio(client_sock);
        return;
    }

    std::ostringstream contenido_ss;
    contenido_ss << f.rdbuf();
    std::string contenido = contenido_ss.str();

    std::ostringstream resp;
    resp << "HTTP/1.1 200 OK\r\n"
         << "Content-Type: " << obtenerMimeType(ruta_limpia) << "\r\n"
         << "Access-Control-Allow-Origin: *\r\n"
         << "Cache-Control: no-store\r\n"
         << "Content-Length: " << contenido.size() << "\r\n"
         << "Connection: close\r\n\r\n";
    enviarTodo(client_sock, resp.str());
    enviarTodo(client_sock, contenido);
    cerrarSocketLimpio(client_sock);
}

struct ClienteContexto {
    SOCKET sock;
    std::filesystem::path base_dir;
};

#ifdef _WIN32
DWORD WINAPI hiloClienteHttp(LPVOID param) {
    std::unique_ptr<ClienteContexto> ctx(static_cast<ClienteContexto*>(param));
    DWORD timeout_ms = 3000;
    setsockopt(ctx->sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
    setsockopt(ctx->sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
    manejarClienteHttp(ctx->sock, ctx->base_dir);
    return 0;
}
#else
void hiloClienteHttp(ClienteContexto* param) {
    std::unique_ptr<ClienteContexto> ctx(param);
    struct timeval tv_rcv{3, 0};
    struct timeval tv_snd{30, 0};
    setsockopt(ctx->sock, SOL_SOCKET, SO_RCVTIMEO, &tv_rcv, sizeof(tv_rcv));
    setsockopt(ctx->sock, SOL_SOCKET, SO_SNDTIMEO, &tv_snd, sizeof(tv_snd));
    try {
        manejarClienteHttp(ctx->sock, ctx->base_dir);
    } catch (...) {
        closesocket(ctx->sock);
    }
}

void terminarLimpio(int) { _exit(0); }  // docker stop / Ctrl+C
#endif

#ifdef _WIN32
DWORD WINAPI hiloPrecalentar(LPVOID) {
    for (const auto& p : LunarBooking::obtenerCatalogoPuntosLunares()) MotorReal::precalentar(p.latitud_deg, p.longitud_deg);
    return 0;
}
#endif

int ejecutarPruebasCpp() {
    std::cout << "=== Verificacion del Motor C++ NASA Lunar Booking ===" << std::endl;

    auto cat = LunarBooking::obtenerCatalogoPuntosLunares();
    if (cat.size() < 6) {
        std::cerr << "[ERROR] Catalogo insuficiente" << std::endl;
        return 1;
    }
    std::cout << "[OK] Catalogo cargado con " << cat.size() << " localizaciones lunares." << std::endl;

    std::cout << "[OK] Motor: " << MotorReal::descripcion() << std::endl;
    LunarBooking::RangoFechas fechas{"2027-01-01T00:00", "2027-01-31T00:00", 6, 16, 4};
    LunarBooking::FiltrosMision filtros{60.0, 0.0, 50.0, 0.0, "DTE", 15.0, "EQUILIBRADO"};

    auto ev = LunarBooking::evaluarLocalizacionLunar(cat[0], fechas, filtros);
    if (!ev.mejor_ventana.encontrada || ev.serie_temporal.empty()) {
        std::cerr << "[ERROR] Fallo en evaluacion o algoritmo de ventana optima" << std::endl;
        return 1;
    }
    std::cout << "[OK] Evaluacion de '" << cat[0].nombre << "': Nota Booking = "
              << std::fixed << std::setprecision(2) << ev.puntuacion_booking_10
              << "/10 | Mejor ventana: " << ev.mejor_ventana.fecha_inicio_optima
              << " -> " << ev.mejor_ventana.fecha_fin_optima
              << " (Media ilu: " << ev.mejor_ventana.media_iluminacion_pct << "%)" << std::endl;
    std::cout << "[OK] Fuente de datos: " << ev.fuente_datos << std::endl;

    auto comp = LunarBooking::compararLocalizacionesYFechas(
        cat[0], fechas, cat[1], fechas, filtros, "SITIOS_FECHAS_PARAMETROS"
    );
    std::cout << "[OK] Comparador C++: " << comp.recomendacion_sintesis << std::endl;

    std::string json_test = procesarEndpointApi("/api/catalogo", "");
    if (json_test.find("\"ok\":true") == std::string::npos) {
        std::cerr << "[ERROR] Fallo en serializacion JSON C++" << std::endl;
        return 1;
    }
    std::cout << "=== TODAS LAS PRUEBAS C++ SUPERADAS ===" << std::endl;
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    bool abrir_navegador = true;
    bool modo_test = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--test") {
            modo_test = true;
        }
        if (arg == "--no-browser") {
            abrir_navegador = false;
        }
    }

    std::filesystem::path dir_exe = obtenerDirectorioEjecutable();
    std::string web_env = leerEntorno("LUNAR_WEB_DIR");
    std::filesystem::path base_dir = web_env.empty() ? dir_exe : std::filesystem::path(web_env);

    // Motor fisico real: efemerides de Sol/Tierra + DEM LOLA (si esta el fichero)
    inicializarMotorReal(dir_exe);

    if (modo_test) {
        return ejecutarPruebasCpp();
    }

    std::string host = leerEntorno("LUNAR_HOST", "127.0.0.1");
    int puerto_base = 8085;
    try {
        puerto_base = std::stoi(leerEntorno("LUNAR_PORT", "8085"));
    } catch (...) {}
    if (puerto_base < 1 || puerto_base > 65535) puerto_base = 8085;
    std::string host_local = (host == "0.0.0.0") ? "127.0.0.1" : host;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "Error inicializando WinSock2." << std::endl;
        return 1;
    }

    // Comprobar si ya hay una instancia activa en el puerto
    {
        SOCKET probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (probe != INVALID_SOCKET) {
            sockaddr_in paddr{};
            paddr.sin_family = AF_INET;
            paddr.sin_addr.s_addr = inet_addr(host_local.c_str());
            paddr.sin_port = htons(static_cast<u_short>(puerto_base));
            if (connect(probe, reinterpret_cast<sockaddr*>(&paddr), sizeof(paddr)) == 0) {
                closesocket(probe);
                std::string url_activa = "http://" + host_local + ":" + std::to_string(puerto_base) + "/index.html";
                std::cout << "[OK] Servidor C++ ya activo en " << url_activa << std::endl;
                if (abrir_navegador) {
                    ShellExecuteA(NULL, "open", url_activa.c_str(), NULL, NULL, SW_SHOWNORMAL);
                }
                WSACleanup();
                return 0;
            }
            closesocket(probe);
        }
    }
#else
    (void)abrir_navegador;
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGTERM, terminarLimpio);
    std::signal(SIGINT, terminarLimpio);
#endif

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
#ifndef _WIN32
    {
        int uno = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof(uno));
    }
#endif

    int puerto = puerto_base;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host.c_str());

    bool enlazado = false;
    for (int p = puerto_base; p <= puerto_base + 35 && p <= 65535; ++p) {
        addr.sin_port = htons(static_cast<unsigned short>(p));
        if (::bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            puerto = p;
            enlazado = true;
            break;
        }
    }

    if (!enlazado || listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "No se pudo abrir puerto local para el servidor C++." << std::endl;
        closesocket(listen_sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::ostringstream url_ss;
    url_ss << "http://" << host_local << ":" << puerto << "/index.html";
    std::string url = url_ss.str();

    std::cout << "================================================================" << std::endl;
    std::cout << "  NASA LUNAR BOOKING & MISSION PLANNER - SERVIDOR C++ ACTIVO" << std::endl;
    std::cout << "  URL: " << url << "  (escuchando en " << host << ":" << puerto << ")" << std::endl;
    std::cout << "  Web: " << base_dir.string() << std::endl;
    std::cout << "  Motor: " << MotorReal::descripcion() << std::endl;
    std::cout << "  (Cierra esta ventana o pulsa Ctrl+C para detener el servidor)" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Con DEM: dejar calculado el horizonte de los sitios del catalogo en segundo plano
    if (MotorReal::demCargado()) {
#ifdef _WIN32
        HANDLE hp = CreateThread(NULL, 0, hiloPrecalentar, NULL, 0, NULL);
        if (hp != NULL) CloseHandle(hp);
#else
        std::thread([]() {
            for (const auto& p : LunarBooking::obtenerCatalogoPuntosLunares()) MotorReal::precalentar(p.latitud_deg, p.longitud_deg);
            std::cout << "[OK] Horizontes del catalogo calculados." << std::endl;
        }).detach();
#endif
    }

#ifdef _WIN32
    if (abrir_navegador) {
        ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
#endif

    while (true) {
        SOCKET client_sock = accept(listen_sock, NULL, NULL);
        if (client_sock == INVALID_SOCKET) continue;
        auto* ctx = new ClienteContexto{client_sock, base_dir};
#ifdef _WIN32
        HANDLE h = CreateThread(NULL, 0, hiloClienteHttp, ctx, 0, NULL);
        if (h != NULL) {
            CloseHandle(h);
        } else {
            hiloClienteHttp(ctx);
        }
#else
        try {
            std::thread(hiloClienteHttp, ctx).detach();
        } catch (...) {
            hiloClienteHttp(ctx);
        }
#endif
    }

    closesocket(listen_sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
