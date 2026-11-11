# Comparación de conservación del mejor estado

Cinco semillas (1–5) por benchmark y por variante: 50 ejecuciones en total.
Inicialización cuadrática, un run, un hilo, esfuerzo fijo 0.005, sin límite de tiempo explícito.
La variante original se ejecutó desde el binario compilado antes de la modificación.
En la versión entregada, la opción `--keep-best` reproduce la variante modificada;
la ejecución sin esa opción conserva el comportamiento anterior.

Todas las 50 salidas pasan integridad y legalidad del evaluador del curso.
En todas coincide el HPWL incremental con el cálculo completo interno y con el evaluador.
Se verificó además repetibilidad de FPGA00 con semilla 1 y esfuerzo fijo, y equivalencia
de la opción de desactivación con el binario original para ese caso.
Tres pruebas adicionales con temperatura inicial elevada también pasan el evaluador
y ejercitan la restauración del mejor estado, incluyendo el estado inicial.

| Benchmark | HPWL promedio original | HPWL promedio keep-best | Mejora (%) | Tiempo original (s) | Tiempo keep-best (s) |
|---|---:|---:|---:|---:|---:|
| FPGA00 | 12867.2 | 11749.2 | +8.689 | 0.080 | 0.082 |
| FPGA01 | 657101.0 | 660858.0 | -0.572 | 3.984 | 4.173 |
| FPGA02 | 886054.0 | 873377.6 | +1.431 | 6.261 | 4.943 |
| FPGA03 | 3879144.6 | 3875123.4 | +0.104 | 23.745 | 24.422 |
| FPGA04 | 7080879.2 | 7080879.2 | +0.000 | 31.618 | 32.749 |

Un porcentaje positivo representa reducción del HPWL promedio; uno negativo representa empeoramiento.

## Interpretación

Los resultados son mixtos. Por eso la modificación queda como opción experimental,
desactivada por defecto. Recuperar el mejor estado del annealing no garantiza
mejorar el resultado del refinamiento posterior, que puede seguir otro camino.
El esfuerzo es deliberadamente bajo para esta primera comparación. Los tiempos
incluyen lectura, inicialización, annealing, refinamiento y escritura, pero no el evaluador.
Estas mediciones no sustituyen pruebas con más esfuerzo o con igual presupuesto de tiempo.
No se deben comparar directamente con la prueba previa de 15 segundos por benchmark.

## Archivos

- `results.csv`: las 50 mediciones individuales.
- `summary.csv`: promedios, desviación estándar muestral y tiempos.
- `logs/`: trazas del programa y del evaluador para todas las ejecuciones.
- `placements/`: mejor salida de cada variante en cada benchmark.
- `change.patch`: diferencia de código y README respecto al proyecto recibido.
- `../../tests/compare_best.py`: script para repetir la comparación.
