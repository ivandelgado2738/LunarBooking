# NASA LunarBooking & eDreams Artemis (Polo Sur Lunar 84°S–90°S)

Planificador visual de misiones lunares al **Polo Sur de la Luna** con motor físico en **C++17** y versión web pública automática para **GitHub Pages**.

## Cómo publicar para que todo el mundo acceda con un enlace (GitHub Pages)
1. Ejecuta `Convertir_Para_GitHub.bat` (o `python Preparar_Para_GitHub.py`).
2. Sube el proyecto completo a tu repositorio de GitHub (`git push`).
3. En GitHub ve a **Settings → Pages → Build and deployment**:
   - **Source**: `Deploy from a branch`
   - **Branch**: `main` y carpeta **`/docs`** (o **`/ (root)`**, ambos funcionan).
4. En ~1 minuto tendrás tu enlace público `https://<tu-usuario>.github.io/<tu-repositorio>/`.

> En el enlace público de GitHub Pages la web detecta automáticamente el entorno estático y ejecuta el motor espejo en JavaScript sin errores ni esperas de red.

## Docker (web + motor C++ con datos reales)
```bash
docker build -t lunar-booking .
docker run --rm -p 8085:8085 lunar-booking
```
Después abre `http://localhost:8085`. Si ya tienes la imagen exportada: `docker load -i lunar-booking_docker-image.tar.gz`.

Para usar el **DEM LOLA real** (`ldem_87s_5mpp.tif`, 3,3 GB, no va dentro de la imagen) móntalo como volumen:
```bash
docker run --rm -p 8085:8085 -v /ruta/a/ldem_87s_5mpp.tif:/datos/ldem_87s_5mpp.tif:ro lunar-booking
```
`GET /api/estado` dice qué datos están cargados (efemérides, DEM).

## Qué datos son reales y cuáles no
| Dato | Origen |
|---|---|
| Posición de Sol y Tierra (elevación, azimut, distancia) | **Real**: `datos/sun_earth_ephemerids.txt` (GMAT, sistema fijo a la Luna, cada 15 min, 2027-01-01 → 2038-05-24, interpolado) |
| Iluminación (%) y enlace directo a Tierra (%) | **Real**: fracción visible del disco solar / terrestre sobre el horizonte |
| Horizonte y pendiente del terreno | **Real solo con el DEM montado** y para puntos dentro del DEM (87°S–90°S). Sin DEM: horizonte esférico a 0° |
| Mejor ventana (máxima media) | `fisica/maximum_average.cpp` |
| Cobertura por satélite relé, temperatura, pendiente sin DEM | Modelo simulado original |
| Fechas fuera de 2027–2038 | Modelo simulado original (la web lo avisa en la etiqueta del motor) |

El puente entre la web y la física está en `motor_real.hpp` / `motor_real.cpp`.

## Ejecución local con el Motor Nativo C++17
```bash
# Windows (MSYS2)
g++ -std=c++17 -O2 -static servidor_lunar_booking.cpp motor_real.cpp fisica/algebra.cpp fisica/maximum_average.cpp -o servidor_lunar_booking.exe -lws2_32 -lshell32 -pthread
./servidor_lunar_booking.exe
# Linux
g++ -std=c++17 -O2 -static -pthread servidor_lunar_booking.cpp motor_real.cpp fisica/algebra.cpp fisica/maximum_average.cpp -o servidor_lunar_booking
./servidor_lunar_booking
```
Después abre `http://127.0.0.1:8085/index.html`. El servidor busca `datos/sun_earth_ephemerids.txt` y `datos/ldem_87s_5mpp.tif` junto al ejecutable (o en `LUNAR_EFEMERIDES` / `LUNAR_DEM`).

## Documentación e Integración de Datos Reales
- [DOCUMENTACION_CODIGO_Y_FUNCIONES_NASA_LUNAR.pdf](DOCUMENTACION_CODIGO_Y_FUNCIONES_NASA_LUNAR.pdf)
- [POSICIONES_LLAMADA_DATOS.txt](POSICIONES_LLAMADA_DATOS.txt)
