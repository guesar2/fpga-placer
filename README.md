# IE0411 – Placement para FPGA

Placer de línea de comandos en C++17: placement inicial analítico (cuadrático
con dispersión por anclas), *simulated annealing* a baja temperatura con
actualización incremental del HPWL y refinamiento greedy por medianas. Ejecuta varias corridas independientes en paralelo (una semilla por
corrida) y conserva la mejor.



## Compilación

Requiere `g++` (≥ 7) o `clang++` con soporte de C++17 y `make`.

```bash
make            # genera el ejecutable ./placer
```

## Ejecución

```bash
./placer <archivo.sitemap> <archivo.nets> <archivo.pl> <salida.pl> [opciones]
```

Ejemplo:

```bash
./placer benchmarks/FPGA01/FPGA01.sitemap benchmarks/FPGA01/FPGA01.nets \
         benchmarks/FPGA01/FPGA01.pl outputs/FPGA01/new_FPGA01.pl
./evaluator benchmarks/FPGA01/FPGA01.sitemap benchmarks/FPGA01/FPGA01.nets \
         benchmarks/FPGA01/FPGA01.pl outputs/FPGA01/new_FPGA01.pl
```

| Opción | Significado | Valor por defecto |
|---|---|---|
| `--seed S` | semilla base; la corrida *i* usa `S+i` | 1 |
| `--runs K` | corridas independientes; se guarda la mejor | nº de núcleos (máx. 4) |
| `--threads T` | hilos de trabajo | `min(runs, núcleos)` |
| `--time-budget S` | tiempo objetivo total en segundos | `20 + 0.012 · celdas móviles` |
| `--effort E` | esfuerzo fijo del annealing (`inner_num` de VPR); desactiva la calibración y, si no se da `--time-budget`, el límite de tiempo | calibrado |
| `--init MODO` | placement inicial: `qp` (analítico) o `given` (el `.pl` de entrada) | `qp` |
| `--t0-scale F` | T0 = F · σ(ΔHPWL) de movimientos aleatorios | 1 (`qp`), 20 (`given`) |
| `--r0 F` | ventana inicial de movimiento, fracción de la rejilla | 0.05 (`qp`), 1 (`given`) |
| `--qp-iters K` | iteraciones de dispersión del placement analítico | 24 |
| `--qp-growth G` | factor de crecimiento del peso de las anclas | 1.6 |
| `--warm-runs W` | con `--init qp`, las últimas W corridas usan un arranque más caliente (`t0-scale 4`, `r0 0.15`); se ignora si se da `--t0-scale` o `--r0` | 0 |
| `--fixed-schedule` | con presupuesto de tiempo, mantiene fijos los movimientos por temperatura calibrados (sin re-planificar en cada paso) | re-planificación activa |
| `--verbose` | muestra el progreso | – |

Al terminar se imprime en `stdout` una línea
`RESULT hpwl=... best_seed=... mean_hpwl=... runs=... effort=... time=...s`.

### Reproducibilidad

Con `--effort` fijo, la misma combinación de `--seed`, `--runs` y `--effort`
produce exactamente el mismo placement en cualquier máquina. En modo
`--time-budget` el esfuerzo se calibra midiendo la velocidad de la máquina y,
además, los movimientos por temperatura se re-planifican durante la corrida a
partir de la velocidad medida, por lo que el resultado depende de la máquina;
el esfuerzo efectivo usado se imprime (`eff.effort ...`) para poder repetir la
corrida con `--effort`. Si una corrida llega al límite de tiempo antes de terminar el
enfriamiento, se marca como `[deadline hit: not reproducible]`.

### Scripts

```bash
scripts/run_benchmarks.sh <dir_benchmarks> <dir_salida> [opciones]   # tabla HPWL / legalidad / tiempo
scripts/seed_study.sh <dir_benchmark> 5 [opciones]                    # mejor, promedio, desviación, tiempo
```

### Barridos de parámetros (`scripts/sweep.py`, `scripts/summarize.py`)

Cada trabajo es una corrida de un solo hilo con `--effort` fijo, así que el HPWL
es reproducible en cualquier máquina y sólo el tiempo depende del hardware. Los
trabajos corren en paralelo (uno por núcleo) y cada resultado se agrega al CSV
al terminar; un barrido interrumpido se continúa con `--resume`.

```bash
# Plan completo: todos los benchmarks, esfuerzo 0.5–8, 5 semillas
scripts/sweep.py --bench-dir <dir_benchmarks> --dry-run        # lista de trabajos y estimación
scripts/sweep.py --bench-dir <dir_benchmarks> --out sweep.csv
# Los gráficos PNG requieren matplotlib; se instala en un entorno virtual:
python3 -m venv .venv && .venv/bin/pip install matplotlib
.venv/bin/python scripts/summarize.py sweep.csv            # tablas, ajuste tiempo~esfuerzo, "codo", gráficos PNG
.venv/bin/python scripts/summarize.py sweep.csv --markdown # tablas listas para el informe

# Hipótesis de la temperatura inicial (varias configuraciones a la vez)
scripts/sweep.py --bench-dir <dir_benchmarks> --benchmarks FPGA01,FPGA03,FPGA04 \
    --efforts 1 --seeds 5 --config cold: --config "warm:--t0-scale 4 --r0 0.15" \
    --config "given:--init given" --out t0.csv
```

`summarize.py` ajusta `tiempo ≈ a + b·esfuerzo` por benchmark: con eso se
convierte un esfuerzo en presupuesto de tiempo para cualquier máquina (midiendo
`b` en ella). El "codo" es el menor esfuerzo cuyo HPWL promedio queda dentro de
`--tol` (1 %) del mejor promedio. Para medir el tiempo sin competencia entre
procesos, use `--parallel 1`. Si el CSV tiene varias configuraciones, también
compara cada una contra la de referencia (`--ref`, por defecto `cold`) con el
estadístico t de Welch, y estima el HPWL esperado del mejor de K corridas
frente a una sola corrida con K veces el esfuerzo.

## Algoritmo

1. **Lectura y estructuras de datos.** Celdas y nets con identificadores enteros
   y arreglos CSR (net→celdas, celda→nets). Para cada tipo de recurso se guarda
   la lista de columnas y, por columna, las coordenadas *y* ordenadas de sus
   sitios; así un movimiento siempre propone un sitio compatible.
2. **HPWL incremental.** Cada net guarda su caja y cuántas celdas tocan cada
   borde. Mover una celda actualiza la caja en O(1), salvo cuando la única celda
   de un borde se mueve hacia adentro (entonces se recalcula esa net). Los
   movimientos se evalúan sin modificar el estado y sólo se aplican si se
   aceptan. Las nets que contienen a las dos celdas de un intercambio no cambian
   y se omiten.
3. **Placement inicial analítico** (diapositivas del curso, *Algoritmos
   analíticos*). Modelo cuadrático: nets de 2–16 pines como clique con peso
   1/(k−1), nets mayores con un nodo estrella de peso k/(k−1) (mismo óptimo que
   la clique); nets de más de 1000 pines (reloj/reset) se ignoran. Las IO fijas
   son constantes. Se resuelven x e y por separado con gradiente conjugado
   precondicionado (Jacobi). Como la solución colapsa alrededor de las pocas IO,
   se itera al estilo SimPL: legalizar → anclar cada celda a su sitio legal con
   un peso creciente → resolver de nuevo; se conserva el mejor placement legal.
   La legalización asigna a cada celda (en orden de distancia al centroide) el
   sitio libre compatible más cercano, usando arreglos *union-find* de
   "siguiente sitio libre" por columna.
4. **Simulated annealing** con el esquema adaptativo de VPR: temperatura inicial
   20·σ(ΔHPWL) de movimientos aleatorios; `effort·N^(4/3)` movimientos por
   temperatura; factor de enfriamiento según la tasa de aceptación; ventana de
   movimiento que se ajusta para mantener ≈44 % de aceptación (ensanchada por
   tipo para que las columnas dispersas de RAM/DSP sigan siendo alcanzables);
   termina cuando `T < 0.005·HPWL/#nets`, seguido de una pasada greedy.
   Partiendo del placement analítico se arranca "frío" (T0 = 1·σ, ventana
   inicial 5 % de la rejilla) para conservar su estructura global; con
   `--init given` se usa el arranque clásico de VPR (20·σ, ventana completa).
   **Con presupuesto de tiempo**, el esfuerzo inicial sale de una medición
   rápida de la velocidad, y antes de cada temperatura se recalcula el número
   de movimientos: velocidad medida en las últimas 8 temperaturas × tiempo
   restante ÷ temperaturas restantes estimadas (conteo típico medido menos las
   ya hechas). Así el enfriamiento termina cerca del límite aunque la máquina
   sea más lenta o más rápida de lo previsto (p. ej. por otros procesos).
5. **Refinamiento por medianas** (*optimal region*): cada celda se intenta mover
   al sitio compatible más cercano a la región mediana de las cajas de sus nets
   (calculadas sin la celda), aceptando sólo mejoras.
6. **Varias corridas** independientes en hilos; se verifica legalidad y HPWL
   completo de la mejor antes de escribir el archivo. Opcionalmente (`--warm-runs W`),
   W corridas pueden usar un arranque más caliente (T0 = 4·σ, ventana 15 %).
   En el barrido sólo mejoró FPGA04 con esfuerzo 1 (−1.6 %); con esfuerzo 2–4
   empata y en los demás benchmarks es igual o peor, por lo que está
   desactivado por defecto.

## Referencias

- V. Betz y J. Rose, "VPR: A New Packing, Placement and Routing Tool for FPGA
  Research", *FPL*, 1997. (Esquema de annealing y ventana adaptativa.)
- S. Kirkpatrick, C. D. Gelatt y M. P. Vecchi, "Optimization by Simulated
  Annealing", *Science*, 1983.
- K. Shahookar y P. Mazumder, "VLSI Cell Placement Techniques", *ACM Computing
  Surveys*, 1991. (Movimientos a la región óptima / mediana.)
- M.-C. Kim, D.-J. Lee e I. L. Markov, "SimPL: An Effective Placement
  Algorithm", *ICCAD*, 2010. (Dispersión con anclas.)
- D. Blackman y S. Vigna, generador xoshiro256** (dominio público).
- E. Carvajal Barboza, *IE0411 – Diseño Físico Parte 2: Posicionamiento* (presentación del curso).

## Experimentos (máquina de desarrollo, 2 núcleos, 2 corridas en paralelo)

Configuración por defecto (`--time-budget` automático). Todos los resultados
pasan `integrity` y `legality` del evaluador.

| Benchmark | HPWL meta | v1: inicio aleatorio, T0 = 20σ | v2: inicio analítico, T0 = 1σ |
|---|---|---|---|
| FPGA00 | 52 880 | 11 466 (16 s) | 10 398 (10 s) |
| FPGA01 | 1 453 168 | 411 501 (108 s) | 390 763 (125 s) |
| FPGA02 | 2 716 304 | 739 954 (135 s) | 741 909 (168 s) |
| FPGA03 | 11 528 160 | 3 153 244 (386 s) | 2 942 684 (411 s) |
| FPGA04 | 21 846 668 | 5 496 905 (397 s) | 5 535 989 (452 s) |

Observaciones:

- El placement analítico solo (sin annealing) ya obtiene ≈ 1.1 M en FPGA01,
  por debajo de la meta, en < 1 s, pero es ≈ 2.7× peor que el resultado final.
- A esfuerzo fijo (`--effort 1`, 3 semillas) en FPGA01, arrancar del analítico
  con T0 = 1σ y ventana 5 % da 421.9 k promedio en 46 s contra 437.9 k en 58 s
  del arranque aleatorio (−3.7 % HPWL, −20 % tiempo). Con T0 = 2σ el promedio
  empeoró (≈ 442 k).
- En FPGA04 (el más denso, 68 % de CLB) un arranque más tibio
  (`--t0-scale 4 --r0 0.15`) dio 5 481 050, mejor que ambos. La temperatura
  inicial óptima parece depender de la utilización; falta confirmarlo con más
  semillas.
- Variación entre semillas: ≈ ±4 % (FPGA01), σ/μ ≈ 4–6 % (FPGA00).
