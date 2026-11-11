# IE0411 – Placement para FPGA

Placer de línea de comandos en C++17: placement inicial analítico (cuadrático
con dispersión por anclas), *simulated annealing* en dos niveles (primero
sobre *clusters* de celdas muy conectadas, luego sobre la netlist completa a
baja temperatura) con actualización incremental del HPWL, y refinamiento
greedy por medianas. Ejecuta varias corridas independientes en paralelo (una semilla por
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
| `--time-budget S` | tiempo objetivo total en segundos | `min(3 h, 60 + 3.8·10⁻⁷ · N^2.195)`, N = celdas móviles (ver abajo) |
| `--effort E` | esfuerzo fijo del annealing (`inner_num` de VPR); desactiva la calibración y, si no se da `--time-budget`, el límite de tiempo | calibrado |
| `--init MODO` | placement inicial: `qp` (analítico) o `given` (el `.pl` de entrada) | `qp` |
| `--t0-scale F` | T0 = F · σ(ΔHPWL) de movimientos aleatorios | 1 (`qp`), 20 (`given`) |
| `--r0 F` | ventana inicial de movimiento, fracción de la rejilla | 0.05 (`qp`), 1 (`given`) |
| `--qp-iters K` | iteraciones de dispersión del placement analítico | 24 |
| `--qp-growth G` | factor de crecimiento del peso de las anclas | 1.6 |
| `--warm-runs W` | con `--init qp`, las últimas W corridas usan un arranque más caliente (`t0-scale 4`, `r0 0.15`); se ignora si se da `--t0-scale` o `--r0` | 0 |
| `--cluster K` | tamaño máximo de los clusters (bloques de K sitios); 0 = sin clustering | 8 |
| `--coarse-share F` | con presupuesto de tiempo, fracción del tiempo de annealing para la fase de clusters | 0.5 |
| `--coarse-effort E` | esfuerzo de la fase de clusters con `--effort` fijo | igual a `--effort` |
| `--fine-t0 F`, `--fine-r0 F` | arranque del annealing plano después de deshacer los clusters | 0.3, 0.02 |
| `--fixed-schedule` | con presupuesto de tiempo, mantiene fijos los movimientos por temperatura calibrados (sin re-planificar en cada paso) | re-planificación activa |
| `--verbose` | muestra el progreso | – |

Al terminar se imprime en `stdout` una línea
`RESULT hpwl=... best_seed=... mean_hpwl=... runs=... effort=... time=...s`.

### Presupuesto de tiempo por defecto

Es una sola ley de potencia en el número de celdas móviles N, ajustada con
`scripts/budget_fit.py` para quedar en el 70 % del límite de tiempo de los
benchmarks cuyo límite es el más restrictivo (FPGA01–03), con un tope de 3 h.
No hay valores por benchmark; FPGA05 recibe el valor de la misma fórmula.

| Benchmark | N | presupuesto | límite |
|---|---|---|---|
| FPGA00 | 245 | 1.0 min | 5 min |
| FPGA01 | 9 470 | 4.4 min | 6 min |
| FPGA02 | 14 634 | 9.8 min | 15 min |
| FPGA03 | 45 537 | 107 min | 150 min |
| FPGA04 | 46 503 | 112 min | 240 min |

La re-planificación del annealing (ver *Algoritmo*) hace que una corrida
termine al ≈ 94 % de su presupuesto en cualquier máquina; fuera del
presupuesto sólo quedan unos segundos de lectura y placement analítico.
Gráfico del ajuste: `budget_fit.png`.

### Reproducibilidad

Con `--effort` fijo, la misma combinación de `--seed`, `--runs` y `--effort`
produce exactamente el mismo placement con el mismo ejecutable, en cualquier
máquina. Otro compilador u otras opciones de compilación pueden cambiar el
redondeo de punto flotante del placement analítico inicial y, con él, el
resultado final (en las pruebas sólo cambió FPGA00, dentro de la variación
entre semillas). Las salidas de `outputs/` se generaron con `bin/placer-v5`,
compilado con `-march=native`; el `Makefile` ya no usa `-march=native` para que
el ejecutable funcione en cualquier procesador x86-64 (misma velocidad medida). En modo
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
scripts/ab.sh <dir_benchmarks> <binario_A> <binario_B> FPGA03,FPGA04 [opciones]  # A/B secuencial con el mismo presupuesto
scripts/budget_fit.py --bench-dir <dir_benchmarks> --sweep sweep.csv  # ajuste del presupuesto por defecto + gráfico
scripts/budget_ab.sh <dir_benchmarks> FPGA03 600 2 salida.csv ./placer "plano:--cluster 0" "s05:--coarse-share 0.5"
                                                                      # configuraciones con igual presupuesto, 1 hilo, CSV
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

## Estructura del código

Dependencias entre módulos (cada uno usa los de su izquierda):
`design` → `placement` → `optimize` / `analytic` → `cluster` → `main`.

| Archivo (`src/`) | Descripción | Usa |
|---|---|---|
| `design.{hpp,cpp}` | Lectura de `.sitemap`, `.nets` y `.pl`; ids enteros, listas red→celdas y celda→redes, índice de sitios por tipo. | — |
| `placement.{hpp,cpp}` | Estado de la colocación: posiciones, ocupación de sitios, cajas de cada red con conteo de bordes, HPWL incremental (`evaluate`/`commit`), verificación de legalidad. | `design` |
| `optimize.{hpp,cpp}` | RNG con semilla, recocido simulado tipo VPR, refinamiento greedy por medianas, medición de la tasa de movimientos. | `placement` |
| `analytic.{hpp,cpp}` | Colocación inicial cuadrática (gradiente conjugado), dispersión por anclas y legalizador. | `placement` |
| `cluster.{hpp,cpp}` | Agrupamiento de celdas (heavy-edge) en bloques verticales, problema grueso y desagrupamiento legal. | `design`, `optimize`, `analytic` |
| `main.cpp` | CLI y flujo completo: carga → analítico → agrupamiento → recocidos en paralelo → refinamiento → verificación → escritura. | todos |

### Flujo de ejecución

```mermaid
flowchart TD
    A["Lectura de .sitemap / .nets / .pl<br/>(design)"] --> B["Colocación cuadrática + dispersión por anclas<br/>+ legalización (analytic)"]
    B --> C["Calibración del esfuerzo según el<br/>presupuesto de tiempo (main)"]
    C --> D{"¿Agrupamiento aplicable?<br/>(--cluster K, tipos con muchas celdas)"}
    D -- sí --> E["Construir clústeres heavy-edge<br/>(cluster, compartido por todas las corridas)"]
    D -- no --> G
    E --> F
    subgraph R["K corridas en paralelo, semillas S, S+1, … (main + optimize)"]
        F["Recocido del problema grueso"] --> F2["Desagrupar (legal por construcción)"]
        F2 --> G["Recocido plano<br/>(frío: --fine-t0 / --fine-r0)"]
        G --> H["Refinamiento greedy por medianas"]
    end
    H --> I["Elegir la mejor corrida"]
    I --> J["Verificar legalidad y recalcular HPWL<br/>(placement)"]
    J --> K["Escribir new_*.pl y línea RESULT"]
```

Sin agrupamiento (o con `--cluster 0`), cada corrida empieza directamente en el
recocido plano desde la colocación analítica.

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
4. **Clustering y annealing en dos niveles** (idea de la colocación guiada por
   clustering; Kahng et al., DAC 2024). Se agrupan celdas del mismo tipo con
   *heavy-edge matching* (Karypis y Kumar): el peso entre dos celdas es
   Σ 1/(|e|−1) sobre las nets que comparten (nets de ≤16 pines), y se unen
   pares repetidamente hasta clusters de ≤K celdas. La parte propia de FPGA:
   un cluster ocupa un **bloque** de K sitios consecutivos de su tipo en una
   columna, así que deshacer el cluster es legal por construcción (sus celdas
   ocupan los K sitios del bloque). El problema reducido es un diseño
   ordinario (celdas = clusters, sitios = centros de bloque), por lo que el
   mismo annealing y legalizador se reutilizan sin cambios. Sólo se agrupan
   tipos con sitios contiguos en la columna y ≥1000 celdas móviles (CLB en
   estos benchmarks); RAM, DSP e IO quedan como celdas individuales. Si un
   diseño no tiene bloques suficientes, K se reduce a la mitad hasta que
   quepa. Flujo: placement analítico → cada cluster en el bloque libre más
   cercano al centroide de sus celdas → annealing de clusters → se deshacen
   → annealing plano frío (T0 = 0.3·σ, ventana 2 %) → refinamiento.
5. **Simulated annealing** con el esquema adaptativo de VPR: temperatura inicial
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
6. **Refinamiento por medianas** (*optimal region*): cada celda se intenta mover
   al sitio compatible más cercano a la región mediana de las cajas de sus nets
   (calculadas sin la celda), aceptando sólo mejoras.
7. **Varias corridas** independientes en hilos; se verifica legalidad y HPWL
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
- A. B. Kahng et al., DAC 2024, doi:10.1145/3649329.3655991. (Colocación
  guiada por clustering.)
- G. Karypis y V. Kumar, "A Fast and High Quality Multilevel Scheme for
  Partitioning Irregular Graphs", *SIAM J. Sci. Comput.*, 1998.
  (*Heavy-edge matching*.)
- S. Goto, "An Efficient Algorithm for the Two-Dimensional Placement Problem
  in Electrical Circuit Layout", *IEEE Trans. Circuits and Systems*, 1981.
  (Región óptima por medianas.)
- D. Blackman y S. Vigna, generador xoshiro256** (dominio público).
- E. Carvajal Barboza, *IE0411 – Diseño Físico Parte 2: Posicionamiento* (presentación del curso).

## Experimentos

### Primeros resultados (máquina de desarrollo, 2 núcleos, 2 corridas en paralelo)

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
- Variación entre semillas: ≈ ±4 % (FPGA01), σ/μ ≈ 4–6 % (FPGA00).

### Barridos en una máquina de 6 núcleos (i5-9600K)

Cada trabajo es una corrida de un hilo con `--effort` fijo y 5 semillas
(`scripts/sweep.py`), con 5 trabajos en paralelo; los tiempos incluyen la
competencia entre procesos por memoria, que los hace ≈ 1.5–2× más lentos que
una corrida sola. Todos los resultados pasan `integrity` y `legality`. Datos:
`t0.csv`, `t0_eqtime.csv`, `sweep.csv`; tablas y gráficos con
`.venv/bin/python scripts/summarize.py sweep.csv`.

Utilización de CLB: FPGA00 0.4 %, FPGA01 14.1 %, FPGA02 21.5 %, FPGA03 66.1 %,
FPGA04 67.6 % (FPGA03 y FPGA04 tienen además la misma ocupación de RAM, DSP e IO).

**HPWL promedio vs. esfuerzo** (arranque frío, que es el de por defecto;
tiempo promedio de una corrida):

| Benchmark (límite) | esfuerzo 1 | esfuerzo 2 | esfuerzo 4 | esfuerzo 8 |
|---|---|---|---|---|
| FPGA00 (5 min) | 11 518 (0.3 s) | 10 986 (0.5 s) | 10 716 (1.0 s) | 11 269 (1.9 s) |
| FPGA01 (6 min) | 425 237 (45 s) | 407 033 (90 s) | 388 660 (3.0 min) | 371 177 (6.1 min) |
| FPGA02 (15 min) | 757 327 (103 s) | 737 500 (3.4 min) | 719 579 (6.7 min) | 699 296 (14.2 min) |
| FPGA03 (2 h 30) | 2 939 303 (10.4 min) | 2 902 138 (20.4 min) | 2 874 145 (41 min) | – |
| FPGA04 (4 h) | 5 495 934 (14.8 min) | 5 338 044 (26.2 min) | 5 333 658 (52 min) | – |

Variación entre semillas (σ/μ): 0.5–1.5 % en FPGA01–04. El tiempo crece casi
linealmente con el esfuerzo: ≈ 44, 100, 611 y 768 s por unidad de esfuerzo en
FPGA01–04. FPGA01 y FPGA02 siguen mejorando 2–5 % por cada duplicación del
esfuerzo; FPGA03 ≈ 1 %; FPGA04 deja de mejorar a partir de esfuerzo 2.

**Temperatura inicial (arranque frío vs. tibio).** Se probó si los diseños
densos necesitan un arranque más caliente: frío (T0 = 1σ, ventana 5 %) contra
tibio (T0 = 4σ, ventana 15 %), caliente (8σ, 30 %) y el arranque aleatorio de
VPR (`--init given`). Con esfuerzo 1, el frío fue el mejor en FPGA01–03 (el
tibio fue 4.5–6.4 % peor, estadísticamente significativo con 5 semillas),
mientras que en FPGA04 el tibio fue 1.6 % mejor, incluso a igual tiempo. Como
FPGA03 y FPGA04 tienen la misma utilización, **la hipótesis de que la
temperatura óptima depende de la utilización quedó descartada**. El barrido
completo mostró además que la ventaja en FPGA04 sólo existe con esfuerzo 1:
con esfuerzo 2 y 4 ambos empatan, y en los demás benchmarks el tibio es igual
o peor (hasta +88 % en FPGA00). Por eso el arranque frío es el de por defecto
y `--warm-runs` (mezclar corridas tibias entre las paralelas) queda desactivado.

**Varias corridas en paralelo.** En los diseños grandes la variación entre
semillas es pequeña: el mejor de 4 corridas es sólo ≈ 0.5 % mejor que el
promedio en FPGA03–04. Rinde más dar más esfuerzo a cada corrida que agregar
corridas.

**Re-planificación del esfuerzo con presupuesto de tiempo.** En esta máquina,
con 4 hilos, la calibración inicial sobreestimaba la velocidad y todas las
corridas de FPGA03/04 llegaban al límite antes de terminar el enfriamiento (en
la máquina de 2 núcleos ocurría lo contrario: usaban sólo 70–80 % del
presupuesto). Con la re-planificación por temperatura, las corridas terminan
al ≈ 94 % del presupuesto sin cortar el enfriamiento, con HPWL equivalente
(FPGA03 2.951 M vs. 2.942 M; FPGA04 5.433 M vs. 5.428 M; diferencia dentro del
ruido entre semillas).

**Clustering (annealing en dos niveles).** Comparación contra el flujo plano
(sin clusters), todas las salidas legales y sin cortes por tiempo. Datos:
`cluster_*.csv`, `share_default.txt`.

| Prueba | FPGA01 | FPGA02 | FPGA03 | FPGA04 |
|---|---|---|---|---|
| Esfuerzo 1 fijo, 3–5 semillas, K = 8 | −6.5 % | −8.6 % | −2.6 % | −2.2 % |
| Presupuesto de 10 min, 1 hilo, 2 semillas | – | – | −3.8 % | −2.9 % |
| Configuración por defecto completa (corrida final, semilla 1) | −7.1 % | −4.7 % | −3.6 % | −1.3 % |

- **Tamaño de cluster** (FPGA01/02, esfuerzo 1): K = 2, 4, 8 y 16 mejoran
  todos; K = 8 fue el mejor o empatado en ambos.
- **Reparto del tiempo** (`--coarse-share`): con presupuestos cortos importa
  (FPGA03/04 a 10 min: 0.5 fue ≈ 1 % mejor que 0.8; FPGA01 a 60 s fue al
  revés); con 30 min en FPGA03/04 las fracciones 0.5, 0.65 y 0.8 quedan dentro
  del ruido (≤ 0.6 %). Se eligió 0.5, que gana o empata en las configuraciones
  realistas y deja más tiempo al annealing plano.
- Una sola corrida de 30 min con clustering (FPGA03 2.80 M, FPGA04 5.22 M) ya
  supera al flujo plano por defecto (4 corridas, ≈ 100 min: 2.82 M y 5.26 M).

### Resultados finales (configuración por defecto, 6 núcleos)

`scripts/run_benchmarks.sh benchmarks__0411_entrega outputs`, semilla 1,
4 corridas en paralelo, presupuesto de tiempo por defecto. Todas las salidas
pasan `integrity` y `legality` del evaluador.

| Benchmark | Objetivo HPWL | Límite | Sin clustering (v4) | Con clustering (v5, por defecto) |
|---|---|---|---|---|
| FPGA00 | 52 880 | 5 min | 10 359 (7 s) | **10 359** (7 s; sin clusters, diseño muy pequeño) |
| FPGA01 | 1 453 168 | 6 min | 369 584 (4.0 min) | **343 305** (3.6 min) |
| FPGA02 | 2 716 304 | 15 min | 693 350 (9.2 min) | **660 839** (9.2 min) |
| FPGA03 | 11 528 160 | 2 h 30 | 2 820 491 (100 min) | **2 719 649** (101 min) |
| FPGA04 | 21 846 668 | 4 h | 5 255 568 (104 min) | **5 186 448** (105 min) |

Las salidas entregadas en `outputs/` son las de la versión con clustering
(binario congelado `bin/placer-v5`); las de la versión sin clustering quedan en
`outputs_v4/`. Ninguna corrida llegó al límite de su presupuesto: FPGA01–04 usan entre 44 %
y 68 % del límite de tiempo de su benchmark (FPGA00, 2 %).
