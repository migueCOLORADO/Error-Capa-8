# ReporteConstruccion: Parte III (mmapguard)

Reporte corto de lo que se pidió, lo que se construyó y lo que falta. Las cifras salen de comandos ejecutados en nuestro WSL2 (Ubuntu 24.04, kernel 6.18.54.1-microsoft-standard-WSL2). La carga, la demo con defensa, las pruebas con root y la medición con defensa se ejecutaron el sábado 2026-11-07 (nproc=12) y los números se transcribieron de las capturas de `src/capturas/p3/` y `src/capturas/3.2/`, sin redondear. Desde este entorno `sudo -n true` falla ("a password is required") y `/sys/fs/bpf/mmapguard` no existe, así que no intentamos descargar nada; Miguel debe verificar `ls /sys/fs/bpf/mmapguard` y, si existe, ejecutar `sudo ./build/mmapguard unload` (ver Tabla 2).

## Tabla 1: Qué se pidió, qué se construyó, check

| Qué se pidió | Qué se construyó | Check |
| --- | --- | --- |
| 4.1.1 Código C modularizado, kernel 5.x o 6.x | `mmapguard/bpf/mmapguard.bpf.c` (hooks `mmap_file` y `file_mprotect`, CO-RE), `mmapguard/src/` (CLI, cargador, `policy`, `events`). Compila con clang 18.1.3 y `gcc -Wall -Wextra -Werror`, 0 advertencias. `sudo ./build/mmapguard load --count-faults` cargó: el verificador aceptó los programas y el `fentry`; `policy list` salió sin reglas y `unload` correcto (`p3/02`) | OK |
| 4.1.2 Demostración de intercepción | `mmapguard/demo/mg_probe.c` y `demo.sh`. Línea base: `shared-write`, `private-write`, `exec` y `mprotect-write` PERMITIDO (código 0). Con la regla `deny-write-shared,deny-exec` (`defended`): `shared-write`, `exec` y `mprotect-write` DENEGADO errno=1 (código 10) y `private-write` PERMITIDO (0), con 3 eventos `deny` (`p3/03` y `p3/04`) | OK |
| 4.1.3 Makefile sin advertencias | `mmapguard/Makefile` (`all`, `bpf`, `user`, `test`, `bench`, `clean`, `help`). `make clean && make`: exit 0 y 0 advertencias; sin toolchain BPF avisa y compila lo que puede | OK |
| 4.1.4 Pruebas con intentos no autorizados y carreras | `mmapguard/tests/` con unitarias, `mg_unauth.c`, `mg_race.c` y `run_tests.sh`. Con root (`run_tests.sh`): 64 PASS, 0 FAIL, 0 REQUIERE ROOT (`p3/05`). `make test` sin root: 49 PASS, 0 FAIL, 7 REQUIERE ROOT (`3.2/04` y `3.2/05`) | OK |
| 4.2.a Justificación kernel frente a usuario | Subsección "Por qué decidir en el kernel y no en espacio de usuario" del README (235 palabras, con el último párrafo ajustado a los resultados con defensa) | OK |
| 4.2.b Medición de costos | `mmapguard/bench/` y tabla en el README. Línea base con `make bench` (dos corridas) y `bench/bench_con_defensa.sh` en dos sesiones (A y B) con caso 0, caso i y caso ii, más conteo con `strace -c` (3060 llamadas) y ftrace de `security_mmap_file`. La guía pedía 3 sesiones; la tercera queda en la Tabla 2 | OK |
| 4.2.c Limitaciones y trabajo futuro | Tabla de 6 filas en el README | OK |
| LSM activos con `bpf` | `lsm=capability,landlock,yama,safesetid,selinux,bpf,ima` en `dmesg` y en `/sys/kernel/security/lsm`; `securityfs` ya estaba montado y no se tocó `.wslconfig` (`p3/01`) | OK |
| `strace` y ftrace | `strace -c -f -e trace=mmap,munmap,mprotect` sobre `mmap_mprotect`: mmap 1024, munmap 1017, mprotect 1019, total 3060 llamadas. ftrace `function_graph` de `security_mmap_file` sin defensa cargada: `bpf_lsm_mmap_file()` 0.066 us (sin programa BPF enganchado) (`p3/13` y `p3/14`) | OK |
| Toolchain permanente | clang 18.1.3 y bpftool v7.4.0 con libbpf v1.4 (binario en `/usr/lib/linux-tools/*/bpftool`) (`3.2/03`) | OK |
| Capturas de la demo, las pruebas y la medición | `src/capturas/p3/` y `src/capturas/3.2/` | OK |
| README (Parte III) | Subsecciones de la Parte III, fila de estado, párrafo inicial e índice con anclas nuevas (63 enlaces del índice resuelven) | OK |
| Corrección de `perf`, `bpftool` y carga automática | `bench.sh` y `bench_con_defensa.sh` buscan el `perf` real (PATH, `/usr/lib/linux-tools/*/bin/perf`, `*/perf`) y avisan si ninguno sirve; el `Makefile` detecta `bpftool` en `/usr/lib/linux-tools/*/bpftool`; `demo.sh`, `run_tests.sh` y `bench_con_defensa.sh` cargan y descargan solos con `trap` y avisan "si quedo cargada: sudo ./build/mmapguard unload". Verificado con falsos `perf` y `bpftool` y `make test`: 49 PASS, 0 FAIL, 7 REQUIERE ROOT; con root no se pudo reejecutar | OK |
| `actualizarReadme.sh` | Reescrito para 3 commits en orden (mmapguard; shell y editor; README, reporte y `src`) y un solo `push`; omite los grupos sin cambios. `bash -n` ok; `dry-run` y `run` probados solo contra un repo temporal ya borrado. `run` no se ejecutó contra el repo real | OK |

## Tabla 2: Qué falta, razón, qué debe hacer Miguel

Razones permitidas: Manual, Permiso, Contexto, Limitación de la solución escogida. Los comandos son sugeridos y no se ejecutaron.

| Falta | Razón | Qué debe hacer Miguel |
| --- | --- | --- |
| Descargar la defensa si quedó cargada (el segundo `load` salió "ya está cargado", E-03). Desde aquí `/sys/fs/bpf/mmapguard` no existe y `sudo -n true` falla | Manual | `ls /sys/fs/bpf/mmapguard` y, si existe, `cd mmapguard && sudo ./build/mmapguard unload` |
| Repetir `bpftool prog show` con la ruta correcta y capturarlo (el wrapper de Ubuntu lo rompió, E-02) | Manual | Con la defensa cargada: `sudo $(ls /usr/lib/linux-tools/*/bpftool \| head -1) prog show`; guardar la captura en `src/capturas/p3/` y luego `sudo ./build/mmapguard unload` |
| Repetir `make bench` para recuperar `perf stat` tras el arreglo (E-01: el `perf` del PATH es el wrapper de Ubuntu) | Manual | `cd mmapguard && make bench` y copiar las líneas de `perf stat` a la tabla del README |
| Una tercera sesión de `bench_con_defensa` si se quiere cumplir las 3 de la guía (hoy hay 2) | Manual | `cd mmapguard && sudo bash bench/bench_con_defensa.sh`; agregar la sesión C a la tabla del README |
| Borrar tres archivos sueltos en `mmapguard/` (salida de la sesión ftrace en la carpeta equivocada, sin valor para el repo) | Manual | Antes de `actualizarReadme.sh run`: `rm mmapguard/current_tracer mmapguard/set_graph_function mmapguard/tracing_on` |
| Informe final, enlace de sustentación y evidencia individual en la interactiva de EAFIT | Manual | Completarlos según la rúbrica (POR CONFIRMAR con el profesor) |
| Decide al crear o cambiar el mapeo, no por acceso | Limitación de la solución escogida | Ninguna acción; está en la tabla de limitaciones del README |
| Solo cubre `mmap` y `mprotect` nuevos (no mapeos previos ni `read`/`write`) | Limitación de la solución escogida | Ninguna acción; está en la tabla de limitaciones del README |
| Identifica el archivo por (dev, inode), no por ruta | Limitación de la solución escogida | Ninguna acción; está en la tabla de limitaciones del README |
| Un root con CAP_BPF puede descargar el programa y se necesita `bpf` en el LSM | Limitación de la solución escogida | Ninguna acción; está en la tabla de limitaciones del README |
