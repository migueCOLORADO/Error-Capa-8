#!/usr/bin/env bash
# bench.sh: linea base (sin defensa) de mmap, munmap y mprotect.
#
# Uso: bash bench/bench.sh        (o `make bench`, que compila build/bench_mmap antes)
#
# Por escenario ejecuta RUNS corridas de ITER iteraciones (por defecto 10 y 1000),
# intercaladas entre escenarios, y calcula mediana y rango (min a max) de ns por
# iteracion y la mediana (y rango) de fallos de pagina menores por corrida.
# Usa un archivo de 64 KiB en un directorio temporal de /tmp que se borra al salir.
# Como segunda lectura usa `perf stat -e page-faults,minor-faults` solo si funciona sin root.
# El perf se detecta de verdad (ver detectar_perf): el wrapper de Ubuntu no cuenta; si ningun
# perf sirve, avisa en una linea y sigue sin perf.
# Informa tambien si strace y ftrace/tracefs estan disponibles; no instala nada.
#
# Variables: BENCH_ITER (1000), BENCH_RUNS (10), BENCH_BIN (build/bench_mmap),
#   BENCH_ARCHIVO (usar este archivo existente en vez de crear uno; lo usa
#   bench_con_defensa.sh para medir sobre un archivo con regla; omite las sondas de
#   herramientas y perf), BENCH_ETIQUETA (texto del encabezado de la tabla).
#
# Codigos de salida: 0 ok, 2 error de medicion o de entorno, 3 falta el binario.
set -u

AQUI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAIZ="$(cd "$AQUI/.." && pwd)"
ITER="${BENCH_ITER:-1000}"
RUNS="${BENCH_RUNS:-10}"
BIN="${BENCH_BIN:-$RAIZ/build/bench_mmap}"
ETIQUETA="${BENCH_ETIQUETA:-linea base, sin defensa}"
ESCENARIOS="mmap_munmap mmap_touch mmap_mprotect"

if [ ! -x "$BIN" ]; then
    echo "bench.sh: falta $BIN; ejecutar 'make bench' (o 'make build/bench_mmap') primero" >&2
    exit 3
fi

TMPDIR_B=""
limpiar() {
    [ -n "$TMPDIR_B" ] && rm -rf "$TMPDIR_B"
}
trap limpiar EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Busca un perf que funcione de verdad: `perf --version` con codigo 0 y sin la linea WARNING
# del wrapper de Ubuntu (/usr/bin/perf cuando falta el paquete de este kernel). Prueba el PATH,
# luego el del kernel en uso y despues /usr/lib/linux-tools/*/perf y /usr/lib/linux-tools/*/bin/perf.
# Deja la ruta elegida en PERF (vacia si ninguno sirve) y devuelve 0 solo si hay uno.
detectar_perf() {
    local c v
    PERF=""
    for c in "$(command -v perf 2>/dev/null)" \
        "/usr/lib/linux-tools/$(uname -r)/bin/perf" \
        /usr/lib/linux-tools/*/perf /usr/lib/linux-tools/*/bin/perf; do
        { [ -n "$c" ] && [ -x "$c" ]; } || continue
        v="$(timeout 10 "$c" --version 2>&1)" || continue
        case "$v" in *WARNING*) continue ;; esac
        PERF="$c"
        return 0
    done
    return 1
}

TMPDIR_B="$(mktemp -d /tmp/mmapguard-bench.XXXXXX)" || {
    echo "bench.sh: no se pudo crear el directorio temporal en /tmp" >&2
    exit 2
}
RAW="$(mktemp /tmp/mmapguard-bench-raw.XXXXXX)" || {
    echo "bench.sh: no se pudo crear el archivo de lineas crudas en /tmp" >&2
    exit 2
}

if [ -n "${BENCH_ARCHIVO:-}" ]; then
    ARCHIVO="$BENCH_ARCHIVO"
    SONDAS=0
else
    ARCHIVO="$TMPDIR_B/prueba.bin"
    head -c 65536 /dev/zero >"$ARCHIVO" || {
        echo "bench.sh: no se pudo crear $ARCHIVO" >&2
        exit 2
    }
    SONDAS=1
fi

echo "== bench: $ETIQUETA =="
echo "kernel=$(uname -r) nproc=$(nproc) iteraciones=$ITER corridas=$RUNS archivo=$ARCHIVO"

# ---- corridas, intercaladas entre escenarios ----
r=1
while [ "$r" -le "$RUNS" ]; do
    for e in $ESCENARIOS; do
        if ! timeout 120 "$BIN" "$e" "$ARCHIVO" "$ITER" >>"$RAW"; then
            echo "bench.sh: fallo la corrida $r del escenario $e" >&2
            exit 2
        fi
    done
    r=$((r + 1))
done

# ---- tabla: mediana y rango por escenario ----
awk -v escs="$ESCENARIOS" '
function ordenar(a, n,    i, j, t) {
    for (i = 2; i <= n; i++) {
        t = a[i]
        for (j = i - 1; j >= 1 && a[j] > t; j--) a[j + 1] = a[j]
        a[j + 1] = t
    }
}
function mediana(a, n) {
    ordenar(a, n)
    return (n % 2) ? a[(n + 1) / 2] : (a[n / 2] + a[n / 2 + 1]) / 2
}
/^RESULT / {
    for (i = 2; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
    e = v["escenario"]; n[e]++
    ns[e, n[e]] = v["ns_por_iter"] + 0
    fl[e, n[e]] = v["minflt"] + 0
}
END {
    ne = split(escs, E, " ")
    printf "%-14s %12s %12s %12s %12s %10s %10s\n", "escenario", "ns/iter med", "ns min", "ns max", "minflt med", "flt min", "flt max"
    for (k = 1; k <= ne; k++) {
        e = E[k]
        if (n[e] == 0) { printf "%-14s sin datos\n", e; continue }
        delete a; delete b
        for (i = 1; i <= n[e]; i++) { a[i] = ns[e, i]; b[i] = fl[e, i] }
        mn = mediana(a, n[e]); mf = mediana(b, n[e])
        printf "%-14s %12.1f %12.1f %12.1f %12g %10g %10g\n", e, mn, a[1], a[n[e]], mf, b[1], b[n[e]]
    }
}' "$RAW"
echo "lineas crudas (una por corrida): $RAW"
cat "$RAW"

[ "$SONDAS" -eq 1 ] || exit 0

# ---- segunda lectura con perf stat, solo si funciona sin root ----
echo "-- perf stat (segunda lectura) --"
if detectar_perf; then
    echo "perf: usando $PERF ($(timeout 10 "$PERF" --version 2>&1 | head -n 1))"
    if [ "$(id -u)" -eq 0 ]; then
        echo "perf stat: se omite porque se ejecuta como root; la segunda lectura se pide sin root"
    elif timeout 20 "$PERF" stat -x, -e page-faults,minor-faults -o "$TMPDIR_B/perf.probe" true >/dev/null 2>&1 \
        && grep -q 'page-faults' "$TMPDIR_B/perf.probe"; then
        echo "perf stat funciona sin root; una corrida adicional por escenario (cuenta el proceso completo, incluido el arranque)"
        printf "%-14s %14s %14s\n" "escenario" "page-faults" "minor-faults"
        for e in $ESCENARIOS; do
            if timeout 120 "$PERF" stat -x, -e page-faults,minor-faults -o "$TMPDIR_B/perf.$e" -- \
                "$BIN" "$e" "$ARCHIVO" "$ITER" >/dev/null 2>&1; then
                pf="$(awk -F, '$3 ~ /^page-faults/ { print $1 }' "$TMPDIR_B/perf.$e")"
                mf="$(awk -F, '$3 ~ /^minor-faults/ { print $1 }' "$TMPDIR_B/perf.$e")"
                printf "%-14s %14s %14s\n" "$e" "${pf:-NA}" "${mf:-NA}"
            else
                printf "%-14s %s\n" "$e" "perf stat fallo en esta corrida"
            fi
        done
    else
        echo "perf stat: no funciona sin root en este sistema; se omite"
    fi
else
    echo "aviso: no hay un perf que funcione (el del PATH falta o es el wrapper de Ubuntu, y /usr/lib/linux-tools/*/perf y */bin/perf no sirven); se sigue sin perf, sin contadores de page-faults de perf"
fi

# ---- herramientas opcionales ----
echo "-- herramientas de trazado --"
if p="$(command -v strace 2>/dev/null)"; then
    echo "strace: disponible en $p (no se usa en esta medicion)"
else
    echo "strace: no encontrado"
fi
if tr_out="$(ls /sys/kernel/tracing 2>&1)"; then
    echo "ftrace/tracefs: /sys/kernel/tracing accesible (no se usa en esta medicion)"
elif printf '%s' "$tr_out" | grep -qi 'permission denied'; then
    echo "ftrace/tracefs: sin permiso (ls /sys/kernel/tracing: Permission denied)"
else
    echo "ftrace/tracefs: no disponible (ls /sys/kernel/tracing: $(printf '%s' "$tr_out" | head -n 1))"
fi
exit 0
