# Primera modificación: conservar el mejor estado

## El problema

Simulated Annealing acepta algunos movimientos que aumentan el HPWL. Eso permite
explorar, pero el último estado visitado no tiene por qué ser el mejor. La versión
anterior comenzaba la pasada greedy desde el último estado.

## El cambio

Al activar `--keep-best`, se recuerda el placement con menor HPWL desde el inicio de cada búsqueda.
Al acabar el enfriamiento, se recupera antes de la pasada greedy. El refinamiento
por medianas se ejecuta después, igual que antes.

Ejemplo: si durante la búsqueda aparecen HPWL 100, 80, 90 y 85, se recupera el
placement con HPWL 80. Se guardan sus coordenadas, no solamente el número 80.

## Dónde está implementado

- `src/optimize.hpp`: `AnnealOptions::keepBest` activa la opción.
- `src/optimize.cpp`: `bestCost`, `bestX` y `bestY` guardan el mejor resultado.
- `src/optimize.cpp`: `dirty` y `touched` registran las celdas modificadas desde
  el último mínimo. En un nuevo mínimo solo se copian esas coordenadas. Esto
  evita copiar todas las celdas en cada mejora.
- `src/optimize.cpp`: `setPositions(bestX, bestY)` restaura el mejor placement y
  reconstruye ocupación y bounding boxes.
- `src/main.cpp`: `--keep-best` activa el cambio; por defecto se mantiene el comportamiento anterior.

La memoria adicional es O(número de celdas). El registro no consume números
aleatorios ni modifica las decisiones de aceptación del enfriamiento. Después
se puede seguir otro camino porque la pasada greedy parte de otro placement.

## Cómo comparar

Compilar con `make`. Ejecutar normalmente conserva el comportamiento anterior.
Repetir la misma orden añadiendo `--keep-best` activa la modificación.
Para una comparación reproducible, mantener `--seed`, `--runs`, `--threads` y
`--effort` y no especificar `--time-budget`.

El script `tests/compare_best.py` compara las dos variantes usando cinco semillas
por benchmark y valida los resultados con el evaluador. Las mediciones de esta
primera comparación están en `experiments/best_state/`.

## Qué garantiza y qué no

La pasada greedy empieza con el menor HPWL visitado durante el enfriamiento,
incluyendo el estado inicial. La pasada greedy y el refinamiento posterior solo
aceptan mejoras o, en la pasada greedy, también movimientos de igual costo.

Eso no garantiza superar el resultado final de la versión anterior: el mismo
refinamiento puede llegar a mínimos distintos desde estados iniciales distintos.
Tampoco garantiza un óptimo global. Una comparación con esfuerzo fijo no prueba
que el cambio sea mejor con igual tiempo total; esa evaluación queda pendiente.
