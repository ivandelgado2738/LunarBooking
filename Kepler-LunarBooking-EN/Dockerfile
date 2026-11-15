# ============================================================================
# NASA LunarBooking (Kepler's Collective) - web + motor C++ con datos reales
#
#   docker build -t lunar-booking .
#   docker run --rm -p 8085:8085 lunar-booking
#   -> http://localhost:8085
#
# Con el DEM LOLA (3,3 GB, no va dentro de la imagen):
#   docker run --rm -p 8085:8085 \
#     -v /ruta/a/ldem_87s_5mpp.tif:/datos/ldem_87s_5mpp.tif:ro lunar-booking
# ============================================================================

# ---- Etapa 1: compilar el servidor (binario estatico, sin dependencias) ----
FROM gcc:13-bookworm AS compilar
WORKDIR /src
COPY servidor_lunar_booking.cpp motor_datos_lunares.h motor_real.hpp motor_real.cpp ./
COPY fisica/ ./fisica/
RUN g++ -std=c++17 -O2 -static -pthread \
        servidor_lunar_booking.cpp motor_real.cpp \
        fisica/algebra.cpp fisica/maximum_average.cpp \
        -o servidor_lunar_booking

# ---- Etapa 2: imagen final minima (solo binario + web + efemerides) --------
FROM scratch
COPY --from=compilar /src/servidor_lunar_booking /app/servidor_lunar_booking
COPY index.html app.js style.css DOCUMENTACION_CODIGO_Y_FUNCIONES_NASA_LUNAR.pdf /app/web/
COPY img/ /app/web/img/
COPY datos/sun_earth_ephemerids.txt /app/datos/sun_earth_ephemerids.txt

ENV LUNAR_HOST=0.0.0.0 \
    LUNAR_PORT=8085 \
    LUNAR_WEB_DIR=/app/web \
    LUNAR_EFEMERIDES=/app/datos/sun_earth_ephemerids.txt \
    LUNAR_DEM=/datos/ldem_87s_5mpp.tif

EXPOSE 8085
USER 65534:65534
ENTRYPOINT ["/app/servidor_lunar_booking", "--no-browser"]
