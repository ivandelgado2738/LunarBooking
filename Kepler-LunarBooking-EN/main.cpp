#include <string>

int main(){
    from datetime import datetime


def buscar_rango_maxima_media(datos, n_objetivo, margen=0):
    """
    datos: lista de tuplas [("YYYY-MM-DD HH:MM", valor), ...]
    n_objetivo: número de muestras deseado para el intervalo
    margen: cuánto puede variar el tamaño alrededor de n_objetivo (ej. n ± margen)
    """
    # 1. Ordenar por fecha
    datos_ord = sorted(datos, key=lambda d: d[0])
    m = len(datos_ord)

    min_len = max(1, n_objetivo - margen)
    max_len = min(m, n_objetivo + margen)

    # 2. Sumas prefijas (acumuladas) en O(N)
    pref = [0.0] * (m + 1)
    for k in range(m):
        pref[k + 1] = pref[k] + float(datos_ord[k][1])

    mejor_media = float("-inf")
    mejor_rango = None

    # 3. Buscar el intervalo continuo [i, j) con mayor media
    for i in range(m - min_len + 1):
        for tam in range(min_len, max_len + 1):
            j = i + tam
            if j > m:
                break
            media = (pref[j] - pref[i]) / tam
            if media > mejor_media:
                mejor_media = media
                mejor_rango = {
                    "fecha_inicio": datos_ord[i][0],
                    "fecha_fin": datos_ord[j - 1][0],
                    "num_datos": tam,
                    "media_maxima": media,
                    "indice_inicio": i,
                    "indice_fin": j - 1,
                }

    return mejor_rango
}