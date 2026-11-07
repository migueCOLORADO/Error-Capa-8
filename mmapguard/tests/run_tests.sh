#!/usr/bin/env bash
# run_tests.sh: pruebas de mmapguard. Uso: bash tests/run_tests.sh (desde cualquier ruta).
#
# Sin root ejecuta: pruebas unitarias (policy, events), intentos no autorizados sin defensa
# y la carrera de mmap/mprotect/munmap. Los casos con defensa cargada exigen root y el
# binario indicado en la variable MMAPGUARD, por ejemplo:
#   sudo MMAPGUARD=/ruta/a/mmapguard bash tests/run_tests.sh
# Si faltan, se imprimen como [REQUIERE ROOT] y no cuentan como fallo.
# Con root no hace falta un `load` previo: las pruebas cargan la defensa y la descargan solas
# (trap, aunque algo falle). Si MMAPGUARD no se define, usa build/mmapguard cuando existe.
# Si la defensa quedo cargada: sudo ./build/mmapguard unload
#
# Variables: MMAPGUARD (binario de la CLI), MG_RACE_ITERS (iteraciones por hilo, 2000),
#            MG_RACE_THREADS (hilos, 8).
set -u

AQUI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAIZ="$(cd "$AQUI/.." && pwd)"
MMAPGUARD="${MMAPGUARD:-}"
if [ -z "$MMAPGUARD" ] && [ -x "$RAIZ/build/mmapguard" ]; then
    MMAPGUARD="$RAIZ/build/mmapguard"
fi

PASS=0
FAIL=0
ROOT=0
CARGADO=0

TMP="$(mktemp -d /tmp/mmapguard-tests.XXXXXX)" || {
    echo "no se pudo crear el directorio temporal en /tmp" >&2
    exit 2
}
BIN="$TMP/bin"
mkdir -p "$BIN"

limpiar() {
    if [ "$CARGADO" -eq 1 ] && [ -n "$MMAPGUARD" ]; then
        "$MMAPGUARD" unload >/dev/null 2>&1 || echo "AVISO: 'mmapguard unload' fallo; ejecutar: sudo ./build/mmapguard unload" >&2
        CARGADO=0
    fi
    rm -rf "$TMP"
}
trap limpiar EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

pass() { PASS=$((PASS + 1)); echo "[PASS] $*"; }
fail() { FAIL=$((FAIL + 1)); echo "[FAIL] $*"; }
needroot() { ROOT=$((ROOT + 1)); echo "[REQUIERE ROOT] $1: necesita sudo y el binario mmapguard"; }

# compilar <salida> <fuentes...>
compilar() {
    local out="$1"
    shift
    local log="$TMP/cc.$(basename "$out").log"
    if gcc -Wall -Wextra -Werror -std=gnu11 -pthread \
        -I"$RAIZ/include" -I"$RAIZ/src" -I"$AQUI" -o "$out" "$@" >"$log" 2>&1; then
        pass "compilacion de $(basename "$out") con -Wall -Wextra -Werror -std=gnu11"
        return 0
    fi
    fail "compilacion de $(basename "$out")"
    sed 's/^/    /' "$log"
    return 1
}

# run_suite <nombre> <comando...>: ejecuta, muestra la salida y cuenta las lineas [PASS] y [FAIL].
run_suite() {
    local nombre="$1"
    shift
    local out="$TMP/out.$nombre"
    local rc p f
    "$@" >"$out" 2>&1
    rc=$?
    cat "$out"
    p=$(grep -c '^\[PASS\]' "$out")
    f=$(grep -c '^\[FAIL\]' "$out")
    PASS=$((PASS + p))
    FAIL=$((FAIL + f))
    if [ "$rc" -ne 0 ] && [ "$f" -eq 0 ]; then
        fail "$nombre termino con codigo $rc sin informar casos fallidos"
    elif [ "$rc" -eq 0 ] && [ "$p" -eq 0 ]; then
        fail "$nombre no informo ningun caso"
    fi
    return "$rc"
}

echo "== mmapguard: pruebas (dir temporal $TMP) =="

if ! command -v gcc >/dev/null 2>&1; then
    fail "gcc no esta instalado: no se puede compilar ninguna prueba"
    echo "RESUMEN: $PASS PASS, $FAIL FAIL, $ROOT REQUIERE ROOT"
    exit 1
fi

# ---------------------------------------------------------------- unitarias
echo "-- pruebas unitarias --"
if [ -f "$RAIZ/src/policy.c" ]; then
    if compilar "$BIN/unit_policy" "$AQUI/unit_policy.c" "$RAIZ/src/policy.c"; then
        run_suite unit_policy "$BIN/unit_policy"
    fi
else
    fail "falta $RAIZ/src/policy.c"
fi

if [ -f "$RAIZ/src/events.c" ]; then
    if compilar "$BIN/unit_events" "$AQUI/unit_events.c" "$RAIZ/src/events.c"; then
        run_suite unit_events "$BIN/unit_events"
    fi
else
    fail "falta $RAIZ/src/events.c"
fi

# ------------------------------------------------- intentos no autorizados
echo "-- intentos no autorizados, sin defensa cargada --"
BASE="$TMP/base.bin"
head -c 32768 /dev/zero >"$BASE"
if compilar "$BIN/mg_unauth" "$AQUI/mg_unauth.c"; then
    run_suite unauth "$BIN/mg_unauth" "$BASE"
fi

# ------------------------------------------------------------------ carrera
echo "-- carrera de mmap, mprotect y munmap sobre el mismo archivo --"
if compilar "$BIN/mg_race" "$AQUI/mg_race.c"; then
    RACE="$TMP/race.bin"
    head -c 32768 /dev/zero >"$RACE"
    run_suite race timeout 90 "$BIN/mg_race" "$RACE"
fi

# ------------------------------------------------ casos con defensa cargada
echo "-- casos con defensa cargada --"
if [ "$(id -u)" -eq 0 ] && [ -n "$MMAPGUARD" ] && [ -x "$MMAPGUARD" ]; then
    PROT="$TMP/protegido.bin"
    ORIG="$TMP/protegido.orig"
    head -c 32768 /dev/zero >"$PROT"
    cp "$PROT" "$ORIG"
    INO="$(stat -c %i "$PROT")"

    # Una corrida anterior que murio sin trap deja la defensa cargada y load diria "ya esta cargado".
    if [ -e /sys/fs/bpf/mmapguard ]; then
        echo "aviso: quedaba una carga previa en /sys/fs/bpf/mmapguard; se descarga antes de empezar"
        "$MMAPGUARD" unload >/dev/null 2>&1
    fi

    if "$MMAPGUARD" load >"$TMP/load.out" 2>&1; then
        CARGADO=1
        pass "mmapguard load carga y engancha la defensa"
    else
        fail "mmapguard load fallo: $(tr '\n' ' ' <"$TMP/load.out")"
    fi

    if [ "$CARGADO" -eq 1 ]; then
        if "$MMAPGUARD" policy add "$PROT" deny-write-shared,deny-exec >"$TMP/add.out" 2>&1; then
            pass "policy add $PROT deny-write-shared,deny-exec"
        else
            fail "policy add fallo: $(tr '\n' ' ' <"$TMP/add.out")"
        fi

        if "$MMAPGUARD" policy list 2>&1 | grep -q "ino=$INO .*deny-write-shared"; then
            pass "policy list muestra la regla del archivo (ino=$INO)"
        else
            fail "policy list no muestra la regla del archivo (ino=$INO)"
        fi

        # repite el intento (c) y el (d) de mg_unauth con la defensa: esperado EPERM
        run_suite unauth_def "$BIN/mg_unauth" "$PROT" --defended

        # la carrera sobre el archivo protegido no debe cambiar su contenido
        run_suite race_def timeout 90 "$BIN/mg_race" "$PROT" --protected
        if cmp -s "$PROT" "$ORIG"; then
            pass "el archivo protegido es byte a byte igual al original tras los intentos y la carrera"
        else
            fail "el archivo protegido cambio de contenido"
        fi

        # events: al menos un evento de violacion
        "$MMAPGUARD" events --count 1 --timeout 3000 >"$TMP/events.out" 2>&1
        if grep -q 'verdict=deny' "$TMP/events.out"; then
            pass "events muestra al menos un evento: $(head -n 1 "$TMP/events.out")"
        else
            fail "events no mostro ningun evento verdict=deny: $(tr '\n' ' ' <"$TMP/events.out")"
        fi

        # unload y comprobacion de que la defensa dejo de actuar
        if "$MMAPGUARD" unload >"$TMP/unload.out" 2>&1; then
            CARGADO=0
            pass "mmapguard unload retira la defensa"
            if [ ! -e /sys/fs/bpf/mmapguard/lnk_mmap_file ]; then
                pass "unload borro los pines de /sys/fs/bpf/mmapguard"
            else
                fail "unload dejo el pin lnk_mmap_file"
            fi
            run_suite unauth_post "$BIN/mg_unauth" "$PROT"
        else
            fail "mmapguard unload fallo: $(tr '\n' ' ' <"$TMP/unload.out")"
        fi
    else
        echo "(se omiten los demas casos con defensa porque load fallo)"
    fi
else
    needroot "cargar la defensa con mmapguard load"
    needroot "agregar la regla deny-write-shared,deny-exec con policy add y verla en policy list"
    needroot "intento (c) mmap compartido con escritura sobre el archivo protegido devuelve EPERM"
    needroot "intento (d) mprotect a escritura sobre mapeo compartido protegido devuelve EPERM"
    needroot "carrera de mmap, mprotect y munmap sin cambiar el contenido del archivo protegido"
    needroot "events muestra al menos un evento verdict=deny"
    needroot "unload retira la defensa y el intento (c) vuelve a ser permitido"
fi

echo
echo "RESUMEN: $PASS PASS, $FAIL FAIL, $ROOT REQUIERE ROOT"
[ "$FAIL" -eq 0 ]
