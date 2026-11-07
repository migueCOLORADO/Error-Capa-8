# ReporteConstruccion: Parte III (mmapguard)

Reporte corto de lo que se pidió, lo que se construyó y lo que falta. Las cifras salen de comandos ejecutados en nuestro WSL2 (Ubuntu 24.04, kernel 6.18.54.1-microsoft-standard-WSL2). Nada que dependa de cargar el programa BPF se ejecutó: `sudo -n` falla y cargar exige root o CAP_BPF.

## Tabla 1: Qué se pidió, qué se construyó, check

| Qué se pidió | Qué se construyó | Check |
| --- | --- | --- |
| 4.1.1 Código C modularizado, kernel 5.x o 6.x | `mmapguard/bpf/mmapguard.bpf.c` (hooks `mmap_file` y `file_mprotect`, CO-RE), `mmapguard/src/` (CLI, cargador, `policy`, `events`). Compila con clang 18.1.3 y `gcc -Wall -Wextra -Werror`, 0 advertencias. La carga real no se probó | PARCIAL |
| 4.1.2 Demostración de intercepción | `mmapguard/demo/mg_probe.c` y `demo.sh`. La línea base se ejecutó: los 4 modos salieron PERMITIDO. El modo `defended` sin root imprime la razón y sale con 3; con root no se ejecutó | PARCIAL |
| 4.1.3 Makefile sin advertencias | `mmapguard/Makefile` (`all`, `bpf`, `user`, `test`, `bench`, `clean`, `help`). `make clean && make`: exit 0 y 0 advertencias; sin toolchain BPF avisa y compila lo que puede | OK |
| 4.1.4 Pruebas con intentos no autorizados y carreras | `mmapguard/tests/` con unitarias, `mg_unauth.c`, `mg_race.c` y `run_tests.sh`. `make test`: 49 PASS, 0 FAIL, 7 REQUIERE ROOT | PARCIAL |
| 4.2.a Justificación kernel frente a usuario | Subsección "Por qué decidir en el kernel y no en espacio de usuario" del README (237 palabras) | OK |
| 4.2.b Medición de costos | `mmapguard/bench/` y tabla en el README. Línea base medida con `make bench`; columna "Con defensa": NO MEDIDO (Permiso). Se entrega `bench/bench_con_defensa.sh`, que no se ejecutó | PARCIAL |
| 4.2.c Limitaciones y trabajo futuro | Tabla de 6 filas en el README | OK |
| README (Parte III) | Subsecciones de la Parte III, fila de estado, párrafo inicial e índice con anclas nuevas (63 enlaces del índice resuelven) | OK |
| `actualizarReadme.sh` | Se editaron solo `MENSAJE` y `ARCHIVOS`. `bash -n` ok y `dry-run` probado contra un repo temporal ya borrado. `run` no se probó | OK |

## Tabla 2: Qué falta, razón, qué debe hacer Miguel

Razones permitidas: Manual, Permiso, Contexto, Limitación de la solución escogida. Los comandos son sugeridos y no se ejecutaron.

| Falta | Razón | Qué debe hacer Miguel |
| --- | --- | --- |
| Cargar la defensa y confirmar que el verificador acepta los programas y los helpers LSM (también el `fentry` con `--count-faults`) | Permiso | `cd mmapguard && make && sudo ./build/mmapguard load` y, al terminar, `sudo ./build/mmapguard unload` |
| Demostración con defensa activa | Permiso | `sudo bash demo/demo.sh defended` (con `MMAPGUARD=$PWD/build/mmapguard` si hace falta) |
| 7 casos REQUIERE ROOT de `run_tests.sh` (el bloque con root solo pasó `bash -n`) | Permiso | `sudo MMAPGUARD=$PWD/build/mmapguard bash tests/run_tests.sh` y revisar que el formato de `policy list` y `events` coincida con lo que el script espera |
| Medición con defensa (columna "Con defensa" y "Diferencia") | Permiso | `make bench` y `sudo bash bench/bench_con_defensa.sh`; copiar los números reales a la tabla del README |
| Lista de LSM activos ilegible (`/sys/kernel/security/lsm` no existe) y confirmar `bpf` | Manual | `sudo mount -t securityfs securityfs /sys/kernel/security && cat /sys/kernel/security/lsm`. Si `bpf` no figura, agregar `lsm=...,bpf` en `kernelCommandLine` de `.wslconfig` y ejecutar `wsl --shutdown` (POR CONFIRMAR) |
| `strace` y ftrace para completar la medición | Permiso | `sudo apt install strace`; para tracefs, correr con `sudo` o montarlo con `sudo mount -t tracefs tracefs /sys/kernel/tracing` |
| Toolchain permanente (opcional: hoy vive en `~/.mmapguard-tc`, extraído sin sudo) | Permiso | `sudo apt install clang llvm libbpf-dev linux-tools-generic` |
| Capturas de la demo y de la medición (`src/capturas/`) | Manual | Tomar las capturas de `demo.sh`, `make test` y `make bench` y subirlas |
| Informe final, enlace de sustentación y evidencia individual en la interactiva de EAFIT | Manual | Completarlos según la rúbrica (el README los marca pendientes) |
| Decide al crear o cambiar el mapeo, no por acceso | Limitación de la solución escogida | Ninguna acción; está en la tabla de limitaciones del README |
| Solo cubre `mmap` y `mprotect` nuevos (no mapeos previos ni `read`/`write`) | Limitación de la solución escogida | Ninguna acción; está en la tabla de limitaciones del README |
| Identifica el archivo por (dev, inode), no por ruta | Limitación de la solución escogida | Ninguna acción; está en la tabla de limitaciones del README |
| Un root con CAP_BPF puede descargar el programa y se necesita `bpf` en el LSM | Limitación de la solución escogida | Ninguna acción; está en la tabla de limitaciones del README |
