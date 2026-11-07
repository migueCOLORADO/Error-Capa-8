#!/usr/bin/env bash
# bench_con_defensa.sh: costo de la defensa mmapguard sobre mmap, munmap y mprotect.
#
# Uso (exige root; sudo no conserva las variables, se pasan en la linea):
#   sudo bash bench/bench_con_defensa.sh
#   sudo MMAPGUARD=/ruta/a/mmapguard BENCH_BIN=/ruta/a/bench_mmap bash bench/bench_con_defensa.sh
#
# Requiere build/mmapguard (o MMAPGUARD) y build/bench_mmap (o BENCH_BIN); se construyen
# sin root con `make` y `make bench` (esta ultima tambien mide la linea base).
#
# Mide los mismos escenarios y con el mismo metodo que bench.sh (BENCH_ITER=1000 iteraciones,
# BENCH_RUNS=10 corridas, mediana y rango) en tres casos sobre un archivo de 64 KiB:
#   (0) sin defensa cargada (referencia de esta misma sesion, antes de cargar)
#   (i) defensa cargada, archivo SIN regla en mg_policy (el hook hace una busqueda fallida)
#   (ii) defensa cargada, archivo CON la regla deny-exec (busqueda exitosa; el mapeo
#        compartido con escritura esta permitido con esa regla, por lo que no hay violacion)
# No exige un `load` manual previo: el script carga la defensa solo (si quedaba una carga previa
# de otra corrida, la descarga antes de empezar) y la descarga al salir (trap), tambien ante
# error o Ctrl+C.
#
# AVISO: si el script muere sin poder ejecutar el trap y la defensa quedo cargada:
#   sudo ./build/mmapguard unload
#
# Codigos de salida: 0 ok, 2 error al cargar o medir, 3 falta root o un binario.
set -u

AQUI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAIZ="$(cd "$AQUI/.." && pwd)"
MG="${MMAPGUARD:-$RAIZ/build/mmapguard}"
BENCH_BIN="${BENCH_BIN:-$RAIZ/build/bench_mmap}"
export BENCH_BIN

if [ "$(id -u)" -ne 0 ]; then
    echo "bench_con_defensa: se necesita root para cargar la defensa; ejecutar con 'sudo bash bench/bench_con_defensa.sh'"
    exit 3
fi
if [ ! -x "$MG" ]; then
    echo "bench_con_defensa: no se encuentra el binario mmapguard en $MG; ejecutar 'make' o fijar MMAPGUARD=<ruta>"
    exit 3
fi
if [ ! -x "$BENCH_BIN" ]; then
    echo "bench_con_defensa: no se encuentra $BENCH_BIN; ejecutar 'make bench' (sin root) o fijar BENCH_BIN=<ruta>"
    exit 3
fi

# Mismo criterio que bench.sh: un perf que funcione de verdad (no el wrapper de Ubuntu).
detectar_perf() {
    local c v
    PERF=""
    for c in "$(command -v perf 2>/dev/null)"         "/usr/lib/linux-tools/$(uname -r)/bin/perf"         /usr/lib/linux-tools/*/perf /usr/lib/linux-tools/*/bin/perf; do
        { [ -n "$c" ] && [ -x "$c" ]; } || continue
        v="$(timeout 10 "$c" --version 2>&1)" || continue
        case "$v" in *WARNING*) continue ;; esac
        PERF="$c"
        return 0
    done
    return 1
}

CARGADO=0
TMP=""
limpiar() {
    if [ "$CARGADO" -eq 1 ]; then
        "$MG" unload >/dev/null 2>&1 || echo "AVISO: 'mmapguard unload' fallo; ejecutar: sudo ./build/mmapguard unload"
        CARGADO=0
    fi
    [ -n "$TMP" ] && rm -rf "$TMP"
}
trap limpiar EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

TMP="$(mktemp -d /tmp/mmapguard-benchdef.XXXXXX)" || {
    echo "bench_con_defensa: no se pudo crear el directorio temporal en /tmp"
    exit 2
}
ARCHIVO="$TMP/prueba.bin"
head -c 65536 /dev/zero >"$ARCHIVO" || {
    echo "bench_con_defensa: no se pudo crear $ARCHIVO"
    exit 2
}

medir() {
    BENCH_ARCHIVO="$ARCHIVO" BENCH_ETIQUETA="$1" bash "$AQUI/bench.sh" || {
        echo "bench_con_defensa: la medicion '$1' fallo"
        exit 2
    }
}

echo "kernel=$(uname -r) nproc=$(nproc)"
echo "aviso: si quedo cargada: sudo ./build/mmapguard unload"

# Informa del perf que usaria bench.sh (aqui corre como root, asi que bench.sh no lo usa).
if detectar_perf; then
    echo "perf: $PERF funciona, pero con root no se usa en esta medicion"
else
    echo "aviso: no hay un perf que funcione (wrapper de Ubuntu o ausente); se sigue sin perf"
fi

# Si una corrida anterior dejo la defensa cargada, load fallaria con "ya esta cargado": se descarga antes.
if [ -e /sys/fs/bpf/mmapguard ]; then
    echo "aviso: quedaba una carga previa en /sys/fs/bpf/mmapguard; se descarga antes de empezar"
    "$MG" unload || {
        echo "bench_con_defensa: no se pudo descargar la carga previa; ejecutar: sudo ./build/mmapguard unload"
        exit 2
    }
fi

medir "caso 0: sin defensa cargada (referencia de esta sesion)"

if ! "$MG" load; then
    echo "bench_con_defensa: mmapguard load fallo (ver el mensaje anterior)"
    exit 2
fi
CARGADO=1

medir "caso i: con defensa, archivo SIN regla (busqueda fallida en mg_policy)"

if ! "$MG" policy add "$ARCHIVO" deny-exec; then
    echo "bench_con_defensa: policy add fallo (ver el mensaje anterior)"
    exit 2
fi
echo "regla cargada: $("$MG" policy list | tail -n 1)"

medir "caso ii: con defensa, archivo CON regla deny-exec (busqueda exitosa, sin violacion)"

# Comprobacion: si el ringbuf tiene eventos, algun escenario violo la regla y la medicion no es la prevista.
EV="$("$MG" events --timeout 300 2>&1 | wc -l)"
echo "eventos de violacion en el ringbuf tras la medicion: $EV (esperado 0)"

"$MG" policy del "$ARCHIVO" >/dev/null 2>&1
echo "la defensa se descarga al salir; si quedo cargada: sudo ./build/mmapguard unload"
exit 0
