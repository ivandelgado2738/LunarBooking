// ============================================================================
// ARCHIVO: motor_datos_lunares.h
// PROYECTO: NASA Lunar Booking & Mission Planner (C++ Backend Engine)
// DESCRIPCION:
//   Contiene las estructuras de datos y TODAS LAS FUNCIONES DE INTEGRACION
//   donde se debe conectar el codigo externo de calculo fisico/astronomico
//   (Iluminacion Solar, Comunicacion Tierra-Luna, Elevacion/Azimut y Terreno).
//
//   Busca las etiquetas:
//     [PUNTO_INTEGRACION_01] ... [PUNTO_INTEGRACION_07]
//   Consulta tambien el fichero "POSICIONES_LLAMADA_DATOS.txt" para ver el
//   mapa exacto de lineas y parametros.
// ============================================================================

#ifndef MOTOR_DATOS_LUNARES_H
#define MOTOR_DATOS_LUNARES_H

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

#include "motor_real.hpp"  // puente con fisica/ (efemerides reales + DEM + maxima media)

namespace LunarBooking {

// ----------------------------------------------------------------------------
// 1. ESTRUCTURAS DE DATOS DE ENTRADA Y SALIDA
// ----------------------------------------------------------------------------

struct CoordenadaLunar {
    std::string id;
    std::string nombre;
    std::string region;
    double latitud_deg;      // Latitud selenografica (-90.0 = Polo Sur)
    double longitud_deg;     // Longitud selenografica (-180 a +180)
    double x_mapa_m;         // Coordenada X en proyeccion IAU2000::30120 (metros)
    double y_mapa_m;         // Coordenada Y en proyeccion IAU2000::30120 (metros)
    double altitud_m;        // Altitud topografica LOLA sobre el radio medio (metros)
    std::string descripcion_corta;
};

struct RangoFechas {
    std::string fecha_inicio_iso; // Formato "YYYY-MM-DDTHH:MM" o "YYYY-MM-DD"
    std::string fecha_fin_iso;    // Formato "YYYY-MM-DDTHH:MM" o "YYYY-MM-DD"
    double paso_horas;            // Intervalo de muestreo en horas (ej: 0.5, 1.0, 2.0, 6.0, 12.0)
    int n_intervalo_deseado;      // Numero de muestras deseado para maxima media
    int margen_intervalo;         // Margen +/- alrededor de n_intervalo_deseado
};

struct FiltrosMision {
    double iluminacion_min_pct;       // Filtro: % minimo de iluminacion solar [0 - 100]
    double elevacion_solar_min_deg;   // Filtro: angulo minimo del Sol sobre el horizonte (deg)
    double comunicacion_min_pct;      // Filtro: % minimo de enlace con la Tierra [0 - 100]
    double elevacion_tierra_min_deg;  // Filtro: angulo minimo de la Tierra sobre el horizonte (deg)
    std::string modo_enlace;          // "DTE" (Directo a Tierra), "RELE" (Satelite), "CUALQUIERA"
    double pendiente_max_deg;         // Pendiente maxima admisible del terreno (deg)
    std::string prioridad_optimizacion; // "EQUILIBRADO", "MAX_ILUMINACION", "MAX_COMUNICACION", "MAX_POTENCIA"
    // Parametros Avanzados de Ingenieria (Modo Profesional)
    double altura_mastil_m = 0.0;       // Altura del mastil solar/antena (m)
    std::string banda_com = "X";        // "S", "X", "KA", "LASER"
    double diam_antena_m = 1.2;         // Diametro de antena transmisora (m)
    double area_paneles_m2 = 40.0;      // Superficie fotovoltaica desplegada (m^2)
    double eficiencia_solar_pct = 32.0; // Eficiencia de conversion fotovoltaica (%)
    double consumo_hab_kw = 3.5;        // Consumo electrico continuo del habitat/rover (kW)
};

struct MuestraTemporalLunar {
    int indice;
    std::string fecha_hora_iso;
    double horas_desde_inicio;
    // Datos de Iluminacion (conectar en PUNTO_INTEGRACION_01)
    double elevacion_solar_deg;
    double horizonte_topografico_deg;
    double azimut_solar_deg;
    double iluminacion_pct;       // 0.0 a 100.0 %
    double irradiancia_wm2;       // W/m^2
    // Datos de Comunicacion (conectar en PUNTO_INTEGRACION_02)
    double elevacion_tierra_deg;
    double azimut_tierra_deg;
    double comunicacion_dte_pct;  // 0.0 a 100.0 % (Direct-to-Earth)
    double comunicacion_rele_pct; // 0.0 a 100.0 % (Via constelacion rele)
    double comunicacion_efectiva; // 0.0 a 100.0 % (Corregida por Banda RF/Laser y Antena)
    // Datos Termicos y Energeticos (conectar en PUNTO_INTEGRACION_03)
    double temperatura_k;
    double potencia_generada_kw;
    double bateria_soc_pct;
    // Puntuacion combinada del instante (0 a 100)
    double valor_combinado;
};

struct ResultadoVentanaOptima {
    bool encontrada;
    std::string fecha_inicio_optima;
    std::string fecha_fin_optima;
    int indice_inicio;
    int indice_fin;
    int num_muestras;
    double duracion_horas;
    double media_maxima_valor;
    double media_iluminacion_pct;
    double media_comunicacion_pct;
    double media_elevacion_solar_deg;
    double media_elevacion_tierra_deg;
    double media_potencia_kw;
    bool cumple_filtros;
};

struct EvaluacionLocalizacion {
    CoordenadaLunar punto;
    RangoFechas fechas;
    double media_iluminacion_pct;
    double max_elevacion_solar_deg;
    double min_elevacion_solar_deg;
    double media_comunicacion_pct;
    double media_elevacion_tierra_deg;
    double pendiente_terreno_deg;
    double temperatura_media_k;
    double media_potencia_kw;
    double min_bateria_soc_pct;
    double puntuacion_booking_10; // Nota estilo Booking / eDreams de 0.0 a 10.0
    bool cumple_todos_los_filtros;
    std::string etiqueta_valoracion;
    std::string fuente_datos;     // De donde salen Sol/Tierra/horizonte: efemerides reales, DEM o simulado
    ResultadoVentanaOptima mejor_ventana;
    std::vector<ResultadoVentanaOptima> top_ventanas_fechas;
    std::vector<MuestraTemporalLunar> serie_temporal;
};

// ----------------------------------------------------------------------------
// 2. FUNCIONES AUXILIARES DE FECHAS Y COORDENADAS (ESTEREOGRAFICAS + ESFERICAS)
// ----------------------------------------------------------------------------

constexpr double R_LUNA_METROS = 1737400.0;
constexpr double PI_LUNAR = 3.14159265358979323846;

inline void estereografica_a_latlon(double x_m, double y_m, double& lat_deg, double& lon_deg) {
    double rho = std::hypot(x_m, y_m);
    if (rho < 1e-6) {
        lat_deg = -90.0;
        lon_deg = 0.0;
        return;
    }
    double colat_rad = 2.0 * std::atan(rho / (2.0 * R_LUNA_METROS));
    lat_deg = -(90.0 - (colat_rad * 180.0 / PI_LUNAR));
    lon_deg = std::atan2(x_m, y_m) * 180.0 / PI_LUNAR;
}

inline void latlon_a_estereografica(double lat_deg, double lon_deg, double& x_m, double& y_m) {
    double colat_rad = (90.0 + lat_deg) * PI_LUNAR / 180.0;
    if (colat_rad < 0.0) colat_rad = 0.0;
    double rho = 2.0 * R_LUNA_METROS * std::tan(colat_rad * 0.5);
    double lon_rad = lon_deg * PI_LUNAR / 180.0;
    x_m = rho * std::sin(lon_rad);
    y_m = rho * std::cos(lon_rad);
}

// Convierte coordenadas selenograficas (latitud, longitud, altitud) a esfericas (r, theta, phi)
//   r_km      : Radio selenocentrico desde el centro lunar (km) = (1737.4 + altitud_km)
//   theta_deg : Colatitud polar [0 = Polo Norte, 180 = Polo Sur exacto] (ej: -89.78°S -> 179.78°)
//   phi_deg   : Angulo acimutal / longitud normalizada [0, 360°)
inline void selenograficas_a_esfericas(
    double lat_deg, double lon_deg, double alt_m,
    double& out_r_km, double& out_theta_deg, double& out_phi_deg
) {
    out_r_km = (R_LUNA_METROS + alt_m) / 1000.0;
    out_theta_deg = 90.0 - lat_deg;
    out_phi_deg = std::fmod(lon_deg, 360.0);
    if (out_phi_deg < 0.0) out_phi_deg += 360.0;
}

// Convierte coordenadas esfericas (r_km, theta_deg, phi_deg) a selenograficas (lat, lon, alt_m)
inline void esfericas_a_selenograficas(
    double r_km, double theta_deg, double phi_deg,
    double& out_lat_deg, double& out_lon_deg, double& out_alt_m
) {
    double theta_clamped = std::max(170.0, std::min(180.0, theta_deg));
    out_lat_deg = 90.0 - theta_clamped;
    double phi_norm = std::fmod(phi_deg, 360.0);
    if (phi_norm < 0.0) phi_norm += 360.0;
    out_lon_deg = (phi_norm > 180.0) ? (phi_norm - 360.0) : phi_norm;
    out_alt_m = (r_km * 1000.0) - R_LUNA_METROS;
}

// Proyeccion cartesiana 3D centrada en la Luna (MCMF) a partir de esfericas (r, theta, phi)
inline void esfericas_a_cartesianas_3d(
    double r_km, double theta_deg, double phi_deg,
    double& out_x_km, double& out_y_km, double& out_z_km
) {
    double th = theta_deg * PI_LUNAR / 180.0;
    double ph = phi_deg * PI_LUNAR / 180.0;
    out_x_km = r_km * std::sin(th) * std::cos(ph);
    out_y_km = r_km * std::sin(th) * std::sin(ph);
    out_z_km = r_km * std::cos(th);
}

inline double fechaIsoADiasJ2000(const std::string& iso) {
    int y = 2026, m = 11, d = 14, hh = 0, mm = 0;
    if (iso.size() >= 10) {
        try {
            y = std::stoi(iso.substr(0, 4));
            m = std::stoi(iso.substr(5, 2));
            d = std::stoi(iso.substr(8, 2));
            if (iso.size() >= 16) {
                hh = std::stoi(iso.substr(11, 2));
                mm = std::stoi(iso.substr(14, 2));
            }
        } catch (...) {}
    }
    int a = (14 - m) / 12;
    int y_adj = y + 4800 - a;
    int m_adj = m + 12 * a - 3;
    long jdn = d + (153 * m_adj + 2) / 5 + 365L * y_adj + y_adj / 4 - y_adj / 100 + y_adj / 400 - 32045;
    double jd = static_cast<double>(jdn) - 0.5 + (hh + mm / 60.0) / 24.0;
    return jd - 2451545.0;
}

inline std::string diasJ2000AFechaIso(double dias_j2000) {
    double jd = dias_j2000 + 2451545.0 + 0.5;
    long z = static_cast<long>(std::floor(jd));
    double f = jd - z;
    long a = z;
    if (z >= 2299161L) {
        long alpha = static_cast<long>((z - 1867216.25) / 36524.25);
        a = z + 1 + alpha - alpha / 4;
    }
    long b = a + 1524;
    long c = static_cast<long>((b - 122.1) / 365.25);
    long d = static_cast<long>(365.25 * c);
    long e = static_cast<long>((b - d) / 30.6001);
    int day = static_cast<int>(b - d - static_cast<long>(30.6001 * e));
    int month = static_cast<int>((e < 14) ? (e - 1) : (e - 13));
    int year = static_cast<int>((month > 2) ? (c - 4716) : (c - 4715));

    int total_min = static_cast<int>(std::round(f * 24.0 * 60.0));
    int hh = (total_min / 60) % 24;
    int mm = total_min % 60;

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << year << "-"
        << std::setw(2) << month << "-"
        << std::setw(2) << day << " "
        << std::setw(2) << hh << ":"
        << std::setw(2) << mm;
    return oss.str();
}

// Calcula el perfil de obstruccion del horizonte topografico local (en grados)
// segun el azimut (0..360 deg), la altitud base del punto y la altura del mastil.
inline double calcularMascaraHorizontePolar(
    const CoordenadaLunar& punto,
    double azimut_deg,
    double altura_mastil_m
) {
    const double PI = 3.14159265358979323846;
    double az_rad = azimut_deg * PI / 180.0;
    double semilla_lon = (punto.longitud_deg * PI / 180.0) + (punto.x_mapa_m * 0.00003);

    // Silueta multi-armonica de cordilleras y bordes de crateres vecinos del Polo Sur (LOLA DEM)
    double ond_crestas =
        1.15 * std::sin(az_rad + semilla_lon) +
        0.85 * std::cos(2.0 * az_rad - semilla_lon * 0.7) +
        0.55 * std::sin(3.0 * az_rad + punto.y_mapa_m * 0.00004) +
        0.35 * std::cos(5.0 * az_rad + semilla_lon * 1.3) +
        0.22 * std::sin(9.0 * az_rad - semilla_lon * 2.1); // Detalle fino visible con paso_horas <= 1h

    double alt_base_m = punto.altitud_m - altura_mastil_m;
    double sesgo_terreno_deg = 0.0;
    if (alt_base_m < 0.0) {
        // Interior de crater (ej: Cabeus): paredes altas bloquean el horizonte (+2.5 a +5.5 deg)
        sesgo_terreno_deg = 2.8 + (std::abs(alt_base_m) / 1000.0) * 1.4;
    } else {
        // Crestas y montanas: horizonte medio mas despejado cuanto mayor es la altitud
        sesgo_terreno_deg = 0.85 - std::min(2.1, 0.92 * std::sqrt(alt_base_m / 1000.0));
    }

    // Ganancia angular de despeje proporcionada por el mastil desplegable (0..100 m)
    double despeje_mastil_deg = 0.24 * std::sqrt(std::max(0.0, altura_mastil_m));
    return sesgo_terreno_deg + ond_crestas - despeje_mastil_deg;
}

// ============================================================================
// 3. PUNTOS DE INTEGRACION PARA TU CODIGO EXTERNO DE DATOS
// ============================================================================

// === [PUNTO_INTEGRACION_01] : CALCULO DE ILUMINACION Y ANGULO SOLAR ===
inline void calcularIluminacionSolarPunto(
    const CoordenadaLunar& punto,
    double dias_j2000,
    double altura_mastil_m,
    double& out_elevacion_solar_deg,
    double& out_horizonte_topografico_deg,
    double& out_azimut_solar_deg,
    double& out_iluminacion_pct,
    double& out_irradiancia_wm2
) {
    // --- DATOS REALES: efemerides GMAT (Sol en sistema fijo a la Luna) + horizonte del DEM ---
    {
        MotorReal::Geometria g;
        if (MotorReal::calcular(punto.latitud_deg, punto.longitud_deg, punto.altitud_m,
                                altura_mastil_m, dias_j2000, g)) {
            const double UA_M = 149597870700.0;
            out_azimut_solar_deg = g.az_sol_deg;
            out_horizonte_topografico_deg = g.horizonte_sol_deg;
            out_elevacion_solar_deg = g.elev_sol_deg - g.horizonte_sol_deg;  // sobre el horizonte local
            // Iluminacion de la superficie de apoyo en [0, 100]:
            //   0   = sombra total (el terreno tapa el disco solar)
            //   100 = luz normal a la cara (angulo de incidencia 0 deg)
            // Es proporcional al coseno del angulo de incidencia de la luz sobre la
            // cara del parche. (Antes era solo la fraccion visible del disco solar,
            // que vale 1 en cuanto el Sol asoma entero aunque este a ~1 deg: de ahi
            // salia siempre ~100 %.)
            out_iluminacion_pct = 100.0 * g.frac_sol * g.cos_incidencia;
            // Paneles orientados al Sol (verticales en el polo): sin factor coseno
            out_irradiancia_wm2 = 1361.0 * g.frac_sol * (UA_M / g.dist_sol_m) * (UA_M / g.dist_sol_m);
            return;
        }
    }
    // --- RESPALDO SIMULADO (solo si la fecha cae fuera de las efemerides) ---
    const double PI = 3.14159265358979323846;
    const double PERIODO_SINODICO_DIAS = 29.530589;
    const double INCLINACION_SUBSOLAR_DEG = 1.5424;

    double lon_subsolar_rad = std::fmod(dias_j2000 / PERIODO_SINODICO_DIAS, 1.0) * 2.0 * PI;
    double lat_subsolar_deg = INCLINACION_SUBSOLAR_DEG * std::sin(dias_j2000 * 2.0 * PI / 346.62);

    double lat_rad = punto.latitud_deg * PI / 180.0;
    double lon_rad = punto.longitud_deg * PI / 180.0;
    double dec_rad = lat_subsolar_deg * PI / 180.0;
    double ha_rad  = lon_rad - lon_subsolar_rad;

    double sin_el = std::sin(lat_rad) * std::sin(dec_rad) +
                    std::cos(lat_rad) * std::cos(dec_rad) * std::cos(ha_rad);
    sin_el = std::max(-1.0, std::min(1.0, sin_el));
    double el_geom_deg = std::asin(sin_el) * 180.0 / PI;

    double y_az = -std::sin(ha_rad) * std::cos(dec_rad);
    double x_az = std::sin(dec_rad) * std::cos(lat_rad) - std::cos(dec_rad) * std::sin(lat_rad) * std::cos(ha_rad);
    double az_deg = std::atan2(y_az, x_az) * 180.0 / PI;
    if (az_deg < 0.0) az_deg += 360.0;
    out_azimut_solar_deg = az_deg;

    out_horizonte_topografico_deg = calcularMascaraHorizontePolar(punto, az_deg, altura_mastil_m);
    out_elevacion_solar_deg = el_geom_deg - out_horizonte_topografico_deg;

    // Fraccion visible del disco solar + luz difusa reflejada por bordes polares cercanos
    const double SEMI_DISCO_SOL_DEG = 0.55; // Transicion penumbral topografica realista
    if (out_elevacion_solar_deg >= SEMI_DISCO_SOL_DEG) {
        double bonus_altura = std::min(12.0, std::max(0.0, (out_elevacion_solar_deg - SEMI_DISCO_SOL_DEG) * 4.5));
        out_iluminacion_pct = std::min(100.0, 88.0 + bonus_altura);
    } else if (out_elevacion_solar_deg <= -SEMI_DISCO_SOL_DEG) {
        double penumbra = std::max(0.0, 12.0 + (out_elevacion_solar_deg + SEMI_DISCO_SOL_DEG) * 8.0);
        out_iluminacion_pct = penumbra;
    } else {
        double t = (out_elevacion_solar_deg + SEMI_DISCO_SOL_DEG) / (2.0 * SEMI_DISCO_SOL_DEG);
        out_iluminacion_pct = 12.0 + t * 76.0;
    }

    double ang_incidencia_deg = std::max(1.5, el_geom_deg - out_horizonte_topografico_deg * 0.5 + 3.5);
    double factor_inc = std::sin(ang_incidencia_deg * PI / 180.0);
    out_irradiancia_wm2 = 1361.0 * (out_iluminacion_pct / 100.0) * std::max(0.14, std::min(1.0, factor_inc * 3.2));
}

// === [PUNTO_INTEGRACION_02] : CALCULO DE COMUNICACION Y VISIBILIDAD TIERRA ===
inline void calcularComunicacionTierraPunto(
    const CoordenadaLunar& punto,
    double dias_j2000,
    const FiltrosMision& filtros,
    double& out_elevacion_tierra_deg,
    double& out_azimut_tierra_deg,
    double& out_comunicacion_dte_pct,
    double& out_comunicacion_rele_pct,
    double& out_comunicacion_efectiva
) {
    const double PI = 3.14159265358979323846;

    // Efecto de la banda sobre el despeje de horizonte exigido (igual en real y simulado)
    double penalizacion_banda_deg = 0.0;
    if (filtros.banda_com == "S") penalizacion_banda_deg = -0.8;      // Banda S difracta mejor en horizonte rasante
    else if (filtros.banda_com == "KA") penalizacion_banda_deg = 0.9; // Banda Ka exige mayor despeje de horizonte
    else if (filtros.banda_com == "LASER") penalizacion_banda_deg = 1.6; // Laser O2O exige linea de vista muy limpia
    double bonus_antena_pct = (filtros.diam_antena_m - 1.2) * 8.5;

    // --- DATOS REALES: efemerides GMAT (Tierra en sistema fijo a la Luna) + horizonte del DEM ---
    bool datos_reales = false;
    {
        MotorReal::Geometria g;
        if (MotorReal::calcular(punto.latitud_deg, punto.longitud_deg, punto.altitud_m,
                                filtros.altura_mastil_m, dias_j2000, g)) {
            datos_reales = true;
            out_azimut_tierra_deg = g.az_tierra_deg;
            out_elevacion_tierra_deg = g.elev_tierra_deg - g.horizonte_tierra_deg;  // sobre el horizonte local
            // Enlace directo = fraccion visible del disco terrestre (con el margen que pide la banda)
            double frac = MotorReal::fraccionDiscoVisible(
                out_elevacion_tierra_deg - penalizacion_banda_deg, g.radio_ang_tierra_deg);
            out_comunicacion_dte_pct = 100.0 * frac;
            if (frac > 0.0) {
                out_comunicacion_dte_pct = std::max(0.0, std::min(100.0, out_comunicacion_dte_pct + bonus_antena_pct));
            }
        }
    }
    if (!datos_reales) {
    // --- RESPALDO SIMULADO (solo si la fecha cae fuera de las efemerides) ---
    double libracion_lat_deg = -6.68 * std::sin(dias_j2000 * 2.0 * PI / 27.2122)
                               - 0.45 * std::cos(dias_j2000 * 2.0 * PI / 1.0); // Variacion diurna estaciones DSN
    double libracion_lon_deg =  7.90 * std::sin(dias_j2000 * 2.0 * PI / 27.55455);

    double lat_rad = punto.latitud_deg * PI / 180.0;
    double lon_rad = (punto.longitud_deg - libracion_lon_deg) * PI / 180.0;
    double sub_tierra_lat_rad = libracion_lat_deg * PI / 180.0;

    double sin_el_tierra = std::sin(lat_rad) * std::sin(sub_tierra_lat_rad) +
                           std::cos(lat_rad) * std::cos(sub_tierra_lat_rad) * std::cos(lon_rad);
    sin_el_tierra = std::max(-1.0, std::min(1.0, sin_el_tierra));
    double el_base_deg = std::asin(sin_el_tierra) * 180.0 / PI;

    double alt_base_m = punto.altitud_m - filtros.altura_mastil_m;
    double bonus_altura_deg = (alt_base_m > 0.0)
        ? (1.45 * std::sqrt(alt_base_m / 1000.0))
        : (-0.45 * std::abs(alt_base_m) / 1000.0);
    double bonus_mastil_deg = 0.22 * std::sqrt(std::max(0.0, filtros.altura_mastil_m));

    out_elevacion_tierra_deg = el_base_deg + bonus_altura_deg + bonus_mastil_deg;
    out_azimut_tierra_deg = std::fmod(360.0 - punto.longitud_deg + libracion_lon_deg, 360.0);

    double el_efectiva_dte = out_elevacion_tierra_deg - penalizacion_banda_deg;
    if (el_efectiva_dte >= 4.5) {
        out_comunicacion_dte_pct = 100.0;
    } else if (el_efectiva_dte <= -2.5) {
        out_comunicacion_dte_pct = 0.0;
    } else {
        out_comunicacion_dte_pct = ((el_efectiva_dte + 2.5) / 7.0) * 100.0;
    }
    out_comunicacion_dte_pct = std::max(0.0, std::min(100.0, out_comunicacion_dte_pct + bonus_antena_pct));
    } // fin del respaldo simulado

    // Cobertura via constelacion rele lunar (orbita eliptica congelada LunaNet, periodo ~12h)
    // OJO: el rele sigue siendo un modelo SIMULADO (no hay datos reales de ninguna constelacion).
    double fase_rele = 0.5 + 0.5 * std::sin(dias_j2000 * 2.0 * PI / 0.5 + (punto.longitud_deg * PI / 180.0));
    double base_rele = (filtros.banda_com == "LASER") ? 82.0 : 74.0;
    out_comunicacion_rele_pct = std::max(0.0, std::min(100.0, base_rele + 24.0 * fase_rele + bonus_antena_pct * 0.5));

    out_comunicacion_efectiva = (filtros.modo_enlace == "DTE")
        ? out_comunicacion_dte_pct
        : (filtros.modo_enlace == "RELE"
            ? out_comunicacion_rele_pct
            : std::max(out_comunicacion_dte_pct, out_comunicacion_rele_pct));
}

// === [PUNTO_INTEGRACION_03] : TOPOGRAFIA, PENDIENTE Y TEMPERATURA ===
inline void calcularTopografiaYTemperaturaPunto(
    const CoordenadaLunar& punto,
    double iluminacion_pct,
    double elevacion_solar_deg,
    double& out_pendiente_deg,
    double& out_temperatura_k
) {
    double r_km = std::hypot(punto.x_mapa_m, punto.y_mapa_m) / 1000.0;
    out_pendiente_deg = 4.2 + std::fmod(std::abs(punto.latitud_deg * 3.7 + punto.longitud_deg * 0.13), 11.5);
    if (r_km < 25.0) out_pendiente_deg = 7.5;
    // Pendiente real del DEM LOLA si el punto esta dentro del fichero
    double pend_dem = 0.0, alt_dem = 0.0;
    if (MotorReal::topografia(punto.latitud_deg, punto.longitud_deg, pend_dem, alt_dem)) {
        out_pendiente_deg = pend_dem;
    }
    double base_k = (punto.altitud_m < 0.0) ? 38.0 : 58.0;
    out_temperatura_k = base_k + 1.62 * iluminacion_pct + std::max(0.0, elevacion_solar_deg) * 3.2;
}

// === [PUNTO_INTEGRACION_04] : ALGORITMO DE MAXIMA MEDIA EN TORNO A UN INTERVALO N ===
inline ResultadoVentanaOptima buscarMejorVentanaMediaMaxima(
    const std::vector<MuestraTemporalLunar>& serie,
    int n_intervalo_deseado,
    int margen_intervalo,
    double paso_horas,
    const FiltrosMision& filtros
) {
    ResultadoVentanaOptima res{};
    res.encontrada = false;
    int m = static_cast<int>(serie.size());
    if (m == 0) return res;

    // Escalar automaticamente el numero de muestras si el usuario elige una grafica mas precisa (paso_horas < 6h)
    double factor_res = 6.0 / std::max(0.25, paso_horas);
    int n_escalado = std::max(1, static_cast<int>(std::round(n_intervalo_deseado * factor_res)));
    int m_escalado = std::max(0, static_cast<int>(std::round(margen_intervalo * factor_res)));

    int min_len = std::max(1, n_escalado - m_escalado);
    int max_len = std::min(m, n_escalado + m_escalado);
    if (min_len > m) {
        min_len = m;
        max_len = m;
    }

    std::vector<double> pref_ilu(m + 1, 0.0);
    std::vector<double> pref_com(m + 1, 0.0);
    std::vector<double> pref_el_sol(m + 1, 0.0);
    std::vector<double> pref_el_tie(m + 1, 0.0);
    std::vector<double> pref_pot(m + 1, 0.0);
    std::vector<double> metricas(m, 0.0);
    std::vector<std::string> fechas(m);

    for (int k = 0; k < m; ++k) {
        double com_efectiva = serie[k].comunicacion_efectiva;
        double ratio_pot_pct = std::min(100.0, (serie[k].potencia_generada_kw / std::max(0.5, filtros.consumo_hab_kw)) * 100.0);

        double metrica = 0.45 * serie[k].iluminacion_pct + 0.40 * com_efectiva + 0.15 * ratio_pot_pct;
        if (filtros.prioridad_optimizacion == "MAX_ILUMINACION") {
            metrica = 0.75 * serie[k].iluminacion_pct + 0.10 * com_efectiva + 0.15 * ratio_pot_pct;
        } else if (filtros.prioridad_optimizacion == "MAX_COMUNICACION") {
            metrica = 0.15 * serie[k].iluminacion_pct + 0.75 * com_efectiva + 0.10 * ratio_pot_pct;
        }

        metricas[k] = metrica;
        fechas[k] = serie[k].fecha_hora_iso;
        pref_ilu[k + 1]    = pref_ilu[k] + serie[k].iluminacion_pct;
        pref_com[k + 1]    = pref_com[k] + com_efectiva;
        pref_el_sol[k + 1] = pref_el_sol[k] + serie[k].elevacion_solar_deg;
        pref_el_tie[k + 1] = pref_el_tie[k] + serie[k].elevacion_tierra_deg;
        pref_pot[k + 1]    = pref_pot[k] + serie[k].potencia_generada_kw;
    }

    // Busqueda de la ventana continua de maxima media: la hace el algoritmo de
    // fisica/maximum_average.cpp (findMaximumAverageRange) a traves del puente.
    // (ese algoritmo ya recorta el tamano a [n - margen, n + margen] dentro de la serie)
    (void)min_len; (void)max_len;
    int i = 0, i_fin = 0;
    if (MotorReal::mejorVentana(fechas, metricas, n_escalado, m_escalado, i, i_fin)) {
        int j = i_fin + 1;
        int tam = j - i;
        double suma = 0.0;
        for (int k = i; k < j; ++k) suma += metricas[k];
        res.encontrada = true;
        res.indice_inicio = i;
        res.indice_fin = j - 1;
        res.num_muestras = tam;
        res.duracion_horas = static_cast<double>(tam) * paso_horas;
        res.fecha_inicio_optima = serie[i].fecha_hora_iso;
        res.fecha_fin_optima = serie[j - 1].fecha_hora_iso;
        res.media_maxima_valor = suma / static_cast<double>(tam);
        res.media_iluminacion_pct = (pref_ilu[j] - pref_ilu[i]) / static_cast<double>(tam);
        res.media_comunicacion_pct = (pref_com[j] - pref_com[i]) / static_cast<double>(tam);
        res.media_elevacion_solar_deg = (pref_el_sol[j] - pref_el_sol[i]) / static_cast<double>(tam);
        res.media_elevacion_tierra_deg = (pref_el_tie[j] - pref_el_tie[i]) / static_cast<double>(tam);
        res.media_potencia_kw = (pref_pot[j] - pref_pot[i]) / static_cast<double>(tam);
        res.cumple_filtros =
            (res.media_iluminacion_pct >= filtros.iluminacion_min_pct) &&
            (res.media_comunicacion_pct >= filtros.comunicacion_min_pct) &&
            (res.media_elevacion_solar_deg >= filtros.elevacion_solar_min_deg) &&
            (res.media_elevacion_tierra_deg >= filtros.elevacion_tierra_min_deg);
    }
    return res;
}

// === [PUNTO_INTEGRACION_05] : EVALUACION INTEGRAL Y PUNTUACION BOOKING (0-10) ===
inline EvaluacionLocalizacion evaluarLocalizacionLunar(
    const CoordenadaLunar& punto,
    const RangoFechas& fechas,
    const FiltrosMision& filtros
) {
    EvaluacionLocalizacion ev{};
    ev.punto = punto;
    ev.fechas = fechas;

    double d_ini = fechaIsoADiasJ2000(fechas.fecha_inicio_iso);
    double d_fin = fechaIsoADiasJ2000(fechas.fecha_fin_iso);
    if (d_fin <= d_ini) d_fin = d_ini + 14.0;

    double paso_h = std::max(0.25, fechas.paso_horas);
    double paso_dias = paso_h / 24.0;
    int total_pasos = std::min(3000, std::max(2, static_cast<int>(std::round((d_fin - d_ini) / paso_dias)) + 1));

    double sum_ilu = 0.0, sum_com = 0.0, sum_el_tie = 0.0, sum_temp = 0.0, sum_pot = 0.0;
    double max_el_sol = -90.0, min_el_sol = 90.0;
    double pend_deg = 5.0;

    // Simulacion de bateria del habitat (capacidad nominal 120 kWh)
    const double cap_bat_nominal_kwh = 120.0;
    double energia_bat_kwh = cap_bat_nominal_kwh * 0.85;
    double min_soc_pct = 100.0;

    int muestras_reales = 0;
    ev.serie_temporal.reserve(total_pasos);
    for (int k = 0; k < total_pasos; ++k) {
        double d_actual = d_ini + k * paso_dias;
        if (MotorReal::cubre(d_actual)) ++muestras_reales;
        MuestraTemporalLunar m{};
        m.indice = k;
        m.fecha_hora_iso = diasJ2000AFechaIso(d_actual);
        m.horas_desde_inicio = k * paso_h;

        calcularIluminacionSolarPunto(
            punto, d_actual, filtros.altura_mastil_m,
            m.elevacion_solar_deg, m.horizonte_topografico_deg, m.azimut_solar_deg,
            m.iluminacion_pct, m.irradiancia_wm2
        );
        calcularComunicacionTierraPunto(
            punto, d_actual, filtros,
            m.elevacion_tierra_deg, m.azimut_tierra_deg,
            m.comunicacion_dte_pct, m.comunicacion_rele_pct, m.comunicacion_efectiva
        );
        calcularTopografiaYTemperaturaPunto(
            punto, m.iluminacion_pct, m.elevacion_solar_deg,
            pend_deg, m.temperatura_k
        );

        m.potencia_generada_kw = (m.irradiancia_wm2 * filtros.area_paneles_m2 * (filtros.eficiencia_solar_pct / 100.0)) / 1000.0;
        double balance_kw = m.potencia_generada_kw - filtros.consumo_hab_kw;
        energia_bat_kwh = std::max(0.0, std::min(cap_bat_nominal_kwh, energia_bat_kwh + balance_kw * paso_h));
        m.bateria_soc_pct = (energia_bat_kwh / cap_bat_nominal_kwh) * 100.0;
        min_soc_pct = std::min(min_soc_pct, m.bateria_soc_pct);

        m.valor_combinado = 0.5 * m.iluminacion_pct + 0.5 * m.comunicacion_efectiva;
        ev.serie_temporal.push_back(m);

        sum_ilu += m.iluminacion_pct;
        sum_com += m.comunicacion_efectiva;
        sum_el_tie += m.elevacion_tierra_deg;
        sum_temp += m.temperatura_k;
        sum_pot += m.potencia_generada_kw;
        max_el_sol = std::max(max_el_sol, m.elevacion_solar_deg);
        min_el_sol = std::min(min_el_sol, m.elevacion_solar_deg);
    }

    double n_f = static_cast<double>(total_pasos);
    ev.media_iluminacion_pct = sum_ilu / n_f;
    ev.media_comunicacion_pct = sum_com / n_f;
    ev.media_elevacion_tierra_deg = sum_el_tie / n_f;
    ev.temperatura_media_k = sum_temp / n_f;
    ev.media_potencia_kw = sum_pot / n_f;
    ev.min_bateria_soc_pct = min_soc_pct;
    ev.max_elevacion_solar_deg = max_el_sol;
    ev.min_elevacion_solar_deg = min_el_sol;
    ev.pendiente_terreno_deg = pend_deg;

    // Etiqueta de procedencia de los datos (se envia a la web)
    if (muestras_reales == total_pasos) {
        ev.fuente_datos = MotorReal::demCubre(punto.latitud_deg, punto.longitud_deg)
            ? "Real ephemerides + LOLA DEM horizon"
            : (MotorReal::demCargado()
                ? "Real ephemerides (point outside the DEM: spherical horizon)"
                : "Real ephemerides (no DEM: spherical horizon)");
    } else if (muestras_reales == 0) {
        ev.fuente_datos = MotorReal::efemeridesCargadas()
            ? "SIMULATED: dates outside the ephemerides (" + MotorReal::coberturaInicioIso().substr(0, 10) +
              " a " + MotorReal::coberturaFinIso().substr(0, 10) + ")"
            : "SIMULATED: no ephemerides loaded";
    } else {
        ev.fuente_datos = "MIXED: part of the range falls outside the ephemerides (" +
            MotorReal::coberturaInicioIso().substr(0, 10) + " a " + MotorReal::coberturaFinIso().substr(0, 10) + ")";
    }

    ev.mejor_ventana = buscarMejorVentanaMediaMaxima(
        ev.serie_temporal,
        fechas.n_intervalo_deseado,
        fechas.margen_intervalo,
        paso_h,
        filtros
    );

    // Extraer hasta 5 mejores ventanas disjuntas ordenadas por puntuacion descendente
    double factor_res_seg = 6.0 / std::max(0.25, paso_h);
    int largo_seg = std::max(4, static_cast<int>(std::round(fechas.n_intervalo_deseado * factor_res_seg)));
    int paso_seg = std::max(largo_seg, total_pasos / 5);
    for (int inicio = 0; inicio + 2 < total_pasos && ev.top_ventanas_fechas.size() < 5; inicio += paso_seg) {
        std::vector<MuestraTemporalLunar> tramo(
            ev.serie_temporal.begin() + inicio,
            ev.serie_temporal.begin() + std::min(total_pasos, inicio + paso_seg)
        );
        ResultadoVentanaOptima v = buscarMejorVentanaMediaMaxima(
            tramo, fechas.n_intervalo_deseado, fechas.margen_intervalo, paso_h, filtros
        );
        if (v.encontrada) {
            v.indice_inicio += inicio;
            v.indice_fin += inicio;
            ev.top_ventanas_fechas.push_back(v);
        }
    }
    std::sort(
        ev.top_ventanas_fechas.begin(),
        ev.top_ventanas_fechas.end(),
        [](const ResultadoVentanaOptima& a, const ResultadoVentanaOptima& b) {
            return a.media_maxima_valor > b.media_maxima_valor;
        }
    );

    ev.cumple_todos_los_filtros =
        (ev.mejor_ventana.media_iluminacion_pct >= filtros.iluminacion_min_pct) &&
        (ev.mejor_ventana.media_comunicacion_pct >= filtros.comunicacion_min_pct) &&
        (ev.max_elevacion_solar_deg >= filtros.elevacion_solar_min_deg) &&
        (ev.media_elevacion_tierra_deg >= filtros.elevacion_tierra_min_deg) &&
        (ev.pendiente_terreno_deg <= filtros.pendiente_max_deg);

    // Puntuacion de 0.0 a 10.0 que incorpora iluminacion, enlace, terreno y balance energetico
    double score_ilu = ev.mejor_ventana.media_iluminacion_pct / 10.0;
    double score_com = ev.mejor_ventana.media_comunicacion_pct / 10.0;
    double score_pen = std::max(0.0, 10.0 - (ev.pendiente_terreno_deg / 3.0));
    double score_ene = std::min(10.0, (ev.mejor_ventana.media_potencia_kw / std::max(0.5, filtros.consumo_hab_kw)) * 7.5);
    ev.puntuacion_booking_10 = std::min(
        10.0,
        std::max(1.0, 0.38 * score_ilu + 0.34 * score_com + 0.14 * score_pen + 0.14 * score_ene)
    );

    if (ev.puntuacion_booking_10 >= 9.0) ev.etiqueta_valoracion = "Exceptional for Mission (Top Artemis)";
    else if (ev.puntuacion_booking_10 >= 8.0) ev.etiqueta_valoracion = "Very Good / Recommended";
    else if (ev.puntuacion_booking_10 >= 6.5) ev.etiqueta_valoracion = "Suitable with a Specific Solar Window";
    else ev.etiqueta_valoracion = "Extreme Conditions / Shadow";

    return ev;
}

// === [PUNTO_INTEGRACION_06] : MOTOR DE COMPARACION MULTI-MODO ===
struct ResultadoComparacion {
    std::string modo_comparacion;
    EvaluacionLocalizacion eval_a;
    EvaluacionLocalizacion eval_b;
    std::string ganador_iluminacion;
    std::string ganador_comunicacion;
    std::string ganador_global;
    std::string recomendacion_sintesis;
};

inline ResultadoComparacion compararLocalizacionesYFechas(
    const CoordenadaLunar& punto_a,
    const RangoFechas& fechas_a,
    const CoordenadaLunar& punto_b,
    const RangoFechas& fechas_b,
    const FiltrosMision& filtros,
    const std::string& modo_comparacion
) {
    ResultadoComparacion comp{};
    comp.modo_comparacion = modo_comparacion;
    comp.eval_a = evaluarLocalizacionLunar(punto_a, fechas_a, filtros);
    comp.eval_b = evaluarLocalizacionLunar(punto_b, fechas_b, filtros);

    comp.ganador_iluminacion =
        (comp.eval_a.mejor_ventana.media_iluminacion_pct >= comp.eval_b.mejor_ventana.media_iluminacion_pct)
            ? punto_a.nombre : punto_b.nombre;
    comp.ganador_comunicacion =
        (comp.eval_a.mejor_ventana.media_comunicacion_pct >= comp.eval_b.mejor_ventana.media_comunicacion_pct)
            ? punto_a.nombre : punto_b.nombre;
    comp.ganador_global =
        (comp.eval_a.puntuacion_booking_10 >= comp.eval_b.puntuacion_booking_10)
            ? punto_a.nombre : punto_b.nombre;

    std::ostringstream oss;
    oss << "Global winner: " << comp.ganador_global
        << " (Score " << std::fixed << std::setprecision(1)
        << std::max(comp.eval_a.puntuacion_booking_10, comp.eval_b.puntuacion_booking_10)
        << "/10). Best illumination window at " << comp.ganador_iluminacion
        << " and best Earth link at " << comp.ganador_comunicacion << ".";
    comp.recomendacion_sintesis = oss.str();
    return comp;
}

// === [PUNTO_INTEGRACION_07] : CATALOGO DE PUNTOS LUNARES Y SEGMENTOS ===
inline std::vector<CoordenadaLunar> obtenerCatalogoPuntosLunares() {
    std::vector<CoordenadaLunar> cat = {
        {
            "shackleton_rim",
            "Shackleton Crater Rim (Artemis Site 04)",
            "Lunar South Pole - Illuminated Ridge",
            -89.78, 120.0, 2940.0, -5890.0, 2150.0,
            "Peak of almost eternal light next to the largest water-ice cold trap at the South Pole."
        },
        {
            "malapert_massif",
            "Malapert Massif (Artemis Site 23)",
            "Lunar South Pole - DTE Visibility Mountain",
            -85.95, 0.0, 0.0, 122800.0, 4850.0,
            "5 km summit with a permanent direct view of the Earth for DTE communications."
        },
        {
            "connecting_ridge",
            "Shackleton-de Gerlache Connecting Ridge (Site 01)",
            "Lunar South Pole - Central Ridge",
            -89.45, 222.0, -11150.0, -12380.0, 2400.0,
            "One of the zones with the highest annual average solar illumination on the whole Moon."
        },
        {
            "shoemaker_rim",
            "Shoemaker Crater Rim",
            "Lunar South Pole - Hydrogen Deposits",
            -88.10, 45.0, 40700.0, 40700.0, 1680.0,
            "Excellent access to permanently shadowed regions (PSR) rich in volatiles."
        },
        {
            "haworth_plateau",
            "Haworth Crater Plateau",
            "Lunar South Pole - Western Zone",
            -87.45, -12.0, -16000.0, 75600.0, 1890.0,
            "Elevated rim next to one of the oldest cold traps at the South Pole."
        },
        {
            "leibnitz_beta",
            "Leibnitz Beta Plateau (Artemis Site 20)",
            "Lunar South Pole - High Plain",
            -85.40, 32.0, 73800.0, 118100.0, 3920.0,
            "Wide, gentle terrain at high altitude, ideal for deploying large solar panels."
        },
        {
            "cabeus_crater",
            "Cabeus Crater (LCROSS Impact)",
            "Lunar South Pole - Confirmed Ice Basin",
            -84.90, -35.5, -89800.0, 125800.0, -1200.0,
            "Place where the NASA LCROSS probe empirically confirmed plumes of water vapor."
        },
        {
            "nobile_viper",
            "Nobile Crater Rim (VIPER Rover Zone - South Pole)",
            "Lunar South Pole - Ice Prospecting Terrain",
            -85.20, 55.0, 119400.0, 83600.0, 1950.0,
            "Sunlit South Pole hills selected by NASA for the water-hunting VIPER rover."
        }
    };
    return cat;
}

} // namespace LunarBooking

#endif // MOTOR_DATOS_LUNARES_H
