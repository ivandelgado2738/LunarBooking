# Corrección de la iluminación (~100 % con el Sol a ~1°) — 15/11/2026

## Causa
En el modo de datos reales, `motor_datos_lunares.h` (`calcularIluminacionSolarPunto`) hacía
`iluminacion_pct = 100 * frac_sol`, donde `frac_sol` es la **fracción visible del disco solar**
sobre el horizonte. Esa fracción vale 1 en cuanto el Sol asoma entero, aunque esté a ~1° de
elevación, así que la gráfica, las medias y la métrica de la mejor ventana salían siempre ~100 %.
El coseno del ángulo de incidencia sobre la cara de apoyo no intervenía en ningún sitio:
`fisica/illumination.cpp` no se compila en el servidor (y además devolvía el ángulo en radianes
en vez de su coseno).

## Solución (no cambia el objetivo de ningún algoritmo)
`iluminacion_pct = 100 * frac_sol * cos(ángulo de incidencia)`

- 0 = sombra total (el terreno tapa el Sol: `frac_sol = 0`) o Sol por detrás de la cara.
- 100 = luz normal a la cara (incidencia 0°).
- La cara de apoyo es el triángulo del parche del DEM que tiene por vértice el nodo del sitio
  (misma triangulación que `lunar::Patch::triangles()`); sin DEM, el plano horizontal local.

## Archivos modificados
| Archivo | Cambio |
|---|---|
| `motor_real.hpp` | `Geometria` gana `cos_incidencia` e `incidencia_sol_deg`. |
| `motor_real.cpp` | Cada sitio guarda la normal de su cara de apoyo (`normalCaraApoyo`) y `calcular()` obtiene el coseno de incidencia. |
| `motor_datos_lunares.h` | La iluminación pasa a ser `100 * frac_sol * cos_incidencia`. |
| `fisica/illumination.cpp` | `illumination()` devuelve el coseno (antes el ángulo en radianes); el punto se eleva hacia el cielo (antes hacia el lado opuesto al Sol, y todo salía en sombra); `communication()` usa la misma elevación. |
| `fisica/algebra.cpp` | `intersectsTriangle` comprueba que Q1 y Q2 están a lados opuestos del plano (segmento, no recta infinita); `signedTetrahedronVolume` usa coordenadas relativas a un vértice (precisión). |

No se han tocado: irradiancia/potencia (paneles orientados al Sol, sin factor coseno), enlace con
la Tierra, ventana de máxima media, modelo simulado de respaldo ni el motor espejo en JavaScript.

## Hay que volver a compilar
```bash
docker build -t lunar-booking .
# o, en local:
g++ -std=c++17 -O2 -static -pthread servidor_lunar_booking.cpp motor_real.cpp fisica/algebra.cpp fisica/maximum_average.cpp -o servidor_lunar_booking
```

# Interfaz en inglés — 15/11/2026

Todo el texto visible está traducido al inglés: `index.html`, `app.js`, `style.css` (y sus copias en `docs/`)
y los textos que el servidor envía a la pantalla (`motor_datos_lunares.h`, `servidor_lunar_booking.cpp`:
catálogo de sitios, valoraciones, recomendación del comparador y origen de los datos).
No se ha tocado la lógica: identificadores, claves JSON, valores de los `<option>`, clases CSS y nombres de
funciones siguen igual. Los comentarios del código, los mensajes de consola del servidor y la documentación
(README, PDF) siguen en español. La cabecera del CSV exportado tampoco se ha cambiado.
