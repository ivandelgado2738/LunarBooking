// ============================================================================
// ARCHIVO: motor_real.hpp
// PROYECTO: NASA Lunar Booking (Kepler's Collective)
// DESCRIPCION:
//   PUENTE entre la web (motor_datos_lunares.h / servidor_lunar_booking.cpp)
//   y el codigo fisico de la carpeta fisica/:
//     - datos/sun_earth_ephemerids.txt  -> posiciones reales de Sol y Tierra
//                                          (GMAT, sistema fijo a la Luna, km)
//     - fisica/lunar_dem.hpp            -> lectura del DEM LOLA (GeoTIFF 5 m/px)
//     - fisica/algebra.cpp              -> algebra vectorial
//     - fisica/maximum_average.cpp      -> ventana de maxima media
//
//   Esta cabecera NO incluye nada de fisica/ a proposito: esos ficheros usan
//   "using namespace std" y nombres globales (Triangle, sign, bind...) que
//   chocarian con el servidor. Todo queda encapsulado en motor_real.cpp.
// ============================================================================
#ifndef MOTOR_REAL_HPP
#define MOTOR_REAL_HPP

#include <string>
#include <vector>

namespace MotorReal {

struct Geometria {
    // Sol
    double elev_sol_deg = 0.0;        // elevacion geometrica del centro del Sol sobre el plano local
    double az_sol_deg = 0.0;          // azimut (0 = norte, 90 = este)
    double horizonte_sol_deg = 0.0;   // altura del horizonte (DEM) en ese azimut
    double frac_sol = 0.0;            // fraccion visible del disco solar [0, 1]
    double dist_sol_m = 0.0;
    // Incidencia de la luz sobre la cara de apoyo (la cara del parche del DEM
    // que hay bajo el sitio; sin DEM, el plano horizontal local de la esfera).
    double incidencia_sol_deg = 90.0; // angulo entre la direccion al Sol y la normal de la cara
    double cos_incidencia = 0.0;      // cos(incidencia) acotado a [0, 1]: 1 = luz normal a la cara,
                                      // 0 = luz rasante o Sol por detras de la cara
    // Tierra
    double elev_tierra_deg = 0.0;
    double az_tierra_deg = 0.0;
    double horizonte_tierra_deg = 0.0;
    double radio_ang_tierra_deg = 0.0; // radio angular del disco terrestre visto desde el sitio
    double dist_tierra_m = 0.0;
    bool con_dem = false;              // true si el horizonte sale del DEM real
};

// Carga efemerides (obligatorio para modo real) y abre el DEM (opcional).
// No lanza excepciones: si algo falla, lo avisa por consola y sigue.
void inicializar(const std::string& ruta_efemerides, const std::string& ruta_dem);

bool efemeridesCargadas();
bool demCargado();
// true si el instante cae dentro del rango de las efemerides
bool cubre(double dias_j2000);
// true si el punto cae dentro del DEM (y tiene dato)
bool demCubre(double lat_deg, double lon_deg);

// Geometria real de Sol y Tierra vistos desde el sitio en ese instante.
// alt_m: altitud del punto sobre la esfera de 1737.4 km (solo se usa si no hay
// DEM para ese punto). Devuelve false si el instante esta fuera de efemerides.
bool calcular(double lat_deg, double lon_deg, double alt_m, double mastil_m,
              double dias_j2000, Geometria& out);

// Fraccion [0,1] de un disco de radio angular r cuyo centro esta a
// `elev_sobre_horizonte_deg` grados por encima del horizonte.
double fraccionDiscoVisible(double elev_sobre_horizonte_deg, double radio_ang_deg);

// Pendiente (deg) y altitud (m) leidas del DEM. false si no hay DEM ahi.
bool topografia(double lat_deg, double lon_deg, double& out_pendiente_deg, double& out_altitud_m);

// Ventana continua de maxima media (fisica/maximum_average.cpp).
// Devuelve indices [i0, i1] inclusive sobre los vectores de entrada.
bool mejorVentana(const std::vector<std::string>& fechas, const std::vector<double>& valores,
                  int n_objetivo, int margen, int& out_i0, int& out_i1);

// Calcula y guarda en cache el horizonte de un sitio (para que la primera
// busqueda no tarde). No hace nada si no hay DEM.
void precalentar(double lat_deg, double lon_deg);

std::string descripcion();        // texto corto del modo activo
std::string coberturaInicioIso(); // "YYYY-MM-DD HH:MM" o "" si no hay efemerides
std::string coberturaFinIso();

}  // namespace MotorReal

#endif  // MOTOR_REAL_HPP
