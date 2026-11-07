#!/usr/bin/env bash
# demo.sh: demostracion de mmapguard con mg_probe.
#
# Uso: demo.sh [baseline|defended]      (por defecto: baseline)
#
#   baseline  Sin defensa. No exige root. Crea un directorio temporal, compila mg_probe
#             (o usa MG_PROBE), corre los 4 modos y muestra las salidas reales.
#   defended  Con defensa. Exige root y el binario mmapguard (variable MMAPGUARD,
#             ../mmapguard o build/mmapguard). Carga, agrega la politica, corre los 4 modos,
#             muestra los eventos y descarga (trap, aunque algo falle). No exige un `load`
#             manual previo; si quedaba una carga de otra corrida, la descarga antes de empezar.
#             Si la defensa quedo cargada: sudo ./build/mmapguard unload
#
# Codigos de salida: 0 ok, 1 uso, 2 error de ejecucion (compilacion, load, policy),
#                    3 faltan requisitos de defended (root o binario), 4 resultado de baseline inesperado.
#
# ESPERADO CON DEFENSA (referencia del diseno, NO MEDIDO por este script):
#   shared-write    DENEGADO errno=1 (Operation not permitted)
#   exec            DENEGADO errno=1 (Operation not permitted)
#   mprotect-write  DENEGADO errno=1 (Operation not permitted)
#   private-write   PERMITIDO

set -u

MODOS="shared-write private-write exec mprotect-write"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODO_DEMO="${1:-baseline}"

TMPD=""
LOADED=0
BIN=""

limpiar() {
    if [ "$LOADED" -eq 1 ] && [ -n "$BIN" ]; then
        "$BIN" unload >/dev/null 2>&1 || echo "AVISO: 'mmapguard unload' fallo; revisar /sys/fs/bpf/mmapguard y ejecutar: sudo ./build/mmapguard unload" >&2
        LOADED=0
    fi
    if [ -n "$TMPD" ] && [ -d "$TMPD" ]; then
        rm -rf "$TMPD"
    fi
}
trap limpiar EXIT
trap 'exit 130' INT TERM

# Prepara TMPD, el archivo de prueba y PROBE. Devuelve 0 si todo listo.
preparar() {
    TMPD="$(mktemp -d /tmp/mmapguard-demo.XXXXXX)" || { echo "ERROR: no se pudo crear el directorio temporal en /tmp" >&2; return 2; }
    ARCHIVO="$TMPD/prueba.txt"
    # 4096 bytes de texto
    head -c 4096 < <(yes 'mmapguard archivo de prueba' ) > "$ARCHIVO" 2>/dev/null
    if [ ! -s "$ARCHIVO" ]; then
        echo "ERROR: no se pudo crear el archivo de prueba" >&2
        return 2
    fi
    if [ -n "${MG_PROBE:-}" ]; then
        PROBE="$MG_PROBE"
        if [ ! -x "$PROBE" ]; then
            echo "ERROR: MG_PROBE=$PROBE no es ejecutable" >&2
            return 2
        fi
    else
        PROBE="$TMPD/mg_probe"
        if ! gcc -Wall -Wextra -Werror -std=gnu11 -O2 -o "$PROBE" "$SCRIPT_DIR/mg_probe.c"; then
            echo "ERROR: fallo la compilacion de mg_probe.c con gcc" >&2
            return 2
        fi
    fi
    return 0
}

# Corre los 4 modos; guarda cada codigo de salida en RC_<modo con _ en lugar de -> .
correr_modos() {
    local m rc var
    for m in $MODOS; do
        "$PROBE" "$m" "$ARCHIVO"
        rc=$?
        var="RC_${m//-/_}"
        printf -v "$var" '%s' "$rc"
        echo "  (codigo de salida de mg_probe $m: $rc)"
    done
}

demo_baseline() {
    preparar || exit 2
    echo "=== BASELINE: sin defensa (mmapguard no cargado) ==="
    echo "archivo de prueba: $ARCHIVO ($(stat -c %s "$ARCHIVO") bytes)"
    correr_modos
    echo "---"
    if [ "${RC_shared_write:-x}" = "0" ]; then
        echo "RESULTADO: sin defensa, el mapeo compartido con escritura (shared-write) fue PERMITIDO (mg_probe salio con 0)."
        return 0
    fi
    echo "RESULTADO INESPERADO: shared-write no fue permitido en baseline (codigo de salida ${RC_shared_write:-?}); revisar el entorno (por ejemplo otra defensa activa)." >&2
    exit 4
}

demo_defended() {
    local falta="" accion="" cand
    if [ "$(id -u)" -ne 0 ]; then
        falta="no se ejecuta como root (id -u != 0)"
        accion="correr como root: sudo bash demo/demo.sh defended"
    fi
    if [ -n "${MMAPGUARD:-}" ]; then
        if [ -x "$MMAPGUARD" ]; then BIN="$MMAPGUARD"; fi
    else
        for cand in "$SCRIPT_DIR/../mmapguard" "$SCRIPT_DIR/../build/mmapguard"; do
            if [ -f "$cand" ] && [ -x "$cand" ]; then BIN="$cand"; break; fi
        done
    fi
    if [ -z "$BIN" ]; then
        falta="${falta:+$falta; }no se encontro el binario mmapguard (MMAPGUARD, ../mmapguard o build/mmapguard)"
        accion="${accion:+$accion; }compilar con make en una copia de trabajo o definir MMAPGUARD=/ruta/a/mmapguard"
    fi
    if [ -n "$falta" ]; then
        echo "DEFENDED no se puede ejecutar: $falta. Accion manual para Miguel: $accion."
        BIN=""
        exit 3
    fi

    preparar || exit 2
    echo "=== DEFENDED: con mmapguard ($BIN) ==="
    echo "archivo de prueba: $ARCHIVO ($(stat -c %s "$ARCHIVO") bytes)"

    echo "aviso: si quedo cargada: sudo ./build/mmapguard unload"
    # Una corrida anterior que murio sin trap deja la defensa cargada y load diria "ya esta cargado".
    if [ -e /sys/fs/bpf/mmapguard ]; then
        echo "aviso: quedaba una carga previa en /sys/fs/bpf/mmapguard; se descarga antes de empezar"
        "$BIN" unload || { echo "ERROR: no se pudo descargar la carga previa; ejecutar: sudo ./build/mmapguard unload" >&2; exit 2; }
    fi

    LOADED=1
    if ! "$BIN" load; then
        echo "ERROR: 'mmapguard load' fallo" >&2
        exit 2
    fi
    if ! "$BIN" policy add "$ARCHIVO" deny-write-shared,deny-exec; then
        echo "ERROR: 'mmapguard policy add' fallo" >&2
        exit 2
    fi
    echo "--- politica activa ---"
    "$BIN" policy list
    echo "--- sondas (salida real) ---"
    correr_modos
    echo "--- eventos (mmapguard events --count 8 --timeout 2000; salida real) ---"
    "$BIN" events --count 8 --timeout 2000
    echo "(codigo de salida de events: $?)"
    echo "--- comparacion medida contra el diseno (por codigo de salida) ---"
    echo "shared-write=${RC_shared_write:-?} exec=${RC_exec:-?} mprotect-write=${RC_mprotect_write:-?} private-write=${RC_private_write:-?}"
    echo "--- ESPERADO (referencia del diseno; NO MEDIDO, no es salida de esta ejecucion) ---"
    echo "ESPERADO shared-write=DENEGADO errno=1; exec=DENEGADO errno=1; mprotect-write=DENEGADO errno=1; private-write=PERMITIDO"
    echo "(codigos esperados: 10, 10, 10 y 0 respectivamente)"
    echo "(la defensa se descarga al salir con 'mmapguard unload'; si quedo cargada: sudo ./build/mmapguard unload)"
    # limpiar() descarga con 'mmapguard unload' al salir
}

case "$MODO_DEMO" in
    baseline) demo_baseline ;;
    defended) demo_defended ;;
    *)
        echo "uso: $0 [baseline|defended]" >&2
        exit 1
        ;;
esac
