# reporteBase.md: Error-Capa-8, estructura base y pendientes (2026-11-06)

## 1. Resumen ejecutivo

- Repo `Error-Capa-8`, rama `main`, 4 commits (el prompt decia 1); 19 archivos aparecen modificados en WSL solo por saltos de linea (CRLF en el arbol, LF en el indice); `git status --porcelain` desde Windows sale vacio.
- Wiki local: rama `master`, 2 commits, 2 paginas de 35 bytes ("Welcome to the Error-Capa-8 wiki!"), ambas "modificadas" por CRLF.
- Compilacion limpia: editor con `-Wall -Wextra -Werror`, 0 advertencias, exit 0; shell con `-Wall -Wextra`, 0 advertencias, exit 0.
- Cifra vigente de pruebas: `make test` 73 PASS, 0 FAIL; `make test-huf`, `test-asan` y `test-tsan` 52 PASS, 0 FAIL cada una. 62 y 66 no aparecen en README ni docs.
- Pool automatico: min(CPUs en linea, 8); con 12 logicos son 8 workers (10 hilos medidos con REPL y orquestador).
- Entorno Parte III: kernel 6.18.54.1-microsoft-standard-WSL2 con BPF_LSM, BTF y "bpf" en CONFIG_LSM; faltan clang, bpftool, strace, libbpf-dev, Makefile de kbuild y sudo sin contrasena.
- Veredicto de viabilidad: C (eBPF-LSM) viable en el kernel, bloqueada por toolchain; A, B y D no viables hoy; E viable pero descartada.
- Pendientes (22): 19 CONFIRMADO, 1 DESMENTIDO, 1 NO DETERMINABLE, 1 REQUIERE A MIGUEL.
- Parte III: no existe codigo de kernel; README.md:458-464 la marca Pendiente.

## 2. Arbol y proposito de cada carpeta y archivo

Arbol de lo versionado (`git ls-files`, 36 archivos):

```
Error-Capa-8/
|-- .gitignore
|-- Parte1_Analisis_Conceptual.md
|-- README.md
|-- capturas/3.1/.gitkeep
|-- editor/
|   |-- .gitattributes
|   |-- Makefile
|   |-- editor.c  editor.h  lista.c  lista.h  main.c
|   |-- hilos.c  hilos.h  huf_editor.c  huf_editor.h  huffman.c  huffman.h
|   |-- docs/ CONCURRENCIA.md  FORMATO_HUF.md  REPORTE_TECNICO.md
|   `-- tests/ huf_cli.c  run_tests.sh  run_tests_huf.sh
`-- shell/
    |-- .gitignore  Makefile  README.md  GUIA_DIDACTICA.md
    |-- algo  sys_shell2
    |-- main.c  shell.h  cat_datos.c  cat_editor.c  cat_memoria.c  cat_monitoreo.c  cat_util.c
```

Arbol de lo no versionado (presente en disco, ignorado; `git status --ignored`):

```
shell/
|-- eafitOS                      (binario, .gitignore:7)
|-- main.o  cat_datos.o  cat_editor.o  cat_memoria.o  cat_monitoreo.o  cat_util.o
`-- editor_core.o  editor_lista.o  editor_huffman.o  editor_hilos.o  editor_huf_editor.o
```

No hay archivos sin versionar y no ignorados (`git ls-files --others --exclude-standard` vacio). `editor/` en disco no tiene `.o` ni binarios.

| Ruta | Proposito | Lineas | Versionado |
| --- | --- | --- | --- |
| .gitignore | Ignora `shell/*.o`, `editor/*.o`, binarios del editor, `shell/eafitOS`, temporales de pruebas | 14 | si |
| Parte1_Analisis_Conceptual.md | Parte I; sin encabezados Markdown (grep vacio) | 19 | si |
| README.md | README del Parcial 3: rubrica, Parte II (IA), diseno, base heredada, pruebas | 836 | si |
| capturas/3.1/.gitkeep | Marcador de carpeta de evidencia de la Parte II; sin imagenes | 0 (0 bytes) | si |
| editor/.gitattributes | `*.sh text eol=lf` y `Makefile text eol=lf` | 2 | si |
| editor/Makefile | Compila editor, variantes asan/tsan/lento, `huf_cli`, targets de prueba | 74 | si |
| editor/main.c | Punto de entrada del editor independiente (`./editor [-t] [archivo]`) | 27 | si |
| editor/editor.c, editor.h | REPL del editor de lineas por syscalls (o p a d i s c x t k w q h) | 803, 73 | si |
| editor/lista.c, lista.h | Lista doblemente enlazada de lineas | 131, 55 | si |
| editor/huffman.c, huffman.h | Compresor/descompresor Huffman por bloques, concurrente | 1843, 174 | si |
| editor/hilos.c, hilos.h | Pool de hilos POSIX generico | 313, 66 | si |
| editor/huf_editor.c, huf_editor.h | Orquestador del trabajo Huffman en segundo plano (estado, progreso, cancelacion) | 431, 98 | si |
| editor/docs/CONCURRENCIA.md | Protocolo de concurrencia (hilos, locks, diagramas, preguntas) | 356 | si |
| editor/docs/FORMATO_HUF.md | Especificacion del formato `.huf` (HUF1) | 89 | si |
| editor/docs/REPORTE_TECNICO.md | Reporte tecnico del editor (Parcial 1/2) | 238 | si |
| editor/tests/huf_cli.c | CLI de prueba del modulo `huf_` (c, x, C, X, crc) | 86 | si |
| editor/tests/run_tests.sh | Suite editor + shell + integracion (`make test`) | 239 | si |
| editor/tests/run_tests_huf.sh | Suite del compresor (`test-huf/asan/tsan`) | 325 | si |
| shell/Makefile | Compila `eafitOS` incluyendo fuentes de `../editor` | 51 | si |
| shell/README.md, GUIA_DIDACTICA.md | Documentacion del shell y guia didactica de syscalls | 123, 109 | si |
| shell/main.c, shell.h | Tabla de comandos, `help`, bucle REPL, macros de traza | 335, 99 | si |
| shell/cat_datos.c, cat_memoria.c, cat_monitoreo.c, cat_util.c, cat_editor.c | Comandos por categoria | 252, 177, 223, 135, 89 | si |
| shell/.gitignore | Ignora `*.o` y `sys_shell` | 2 | si |
| shell/algo | Fragmento Java suelto (`public static void main`), ASCII con CRLF, 82 bytes; no es carpeta ni parte del shell | 3 | si |
| shell/sys_shell2 | Binario ELF x86-64 de 60384 bytes con debug_info, versionado y no ignorado | n/a (binario) | si |
| shell/eafitOS | Binario del shell (179384 bytes) | n/a (binario) | no (ignorado) |
| shell/*.o (11 archivos) | Objetos de compilacion previa | n/a (binarios) | no (ignorado) |

### 2.1 Wiki local

| Dato | Valor | Comando |
| --- | --- | --- |
| Ruta | `C:\Users\migue\SOU\Error-Capa-8.wiki` | `ls /mnt/c/Users/migue/SOU` |
| Rama | `master` | `git branch --show-current` |
| Historial | `2efe37c` y `cc0c433`, ambos "Initial Home page" (autor relaxingEnd, 2026-11-06) | `git log --format='%h %an %ad %s'` |
| Paginas | `Home.md` y `Error-Capa-8-‐-Wiki.md` (el guion del nombre es U+2010, `\342\200\220` en octal) | `git -c core.quotepath=false ls-files` |
| Contenido | Ambas: "Welcome to the Error-Capa-8 wiki!", 35 bytes, sin encabezados | `wc -c *.md`, `cat -A` |
| Estado | Ambas modificadas | `git status --porcelain` |
| Causa | Arbol con CRLF (`file`: "ASCII text, with CRLF line terminators"), indice con LF; `git diff` solo agrega `^M` al final de la unica linea | `git diff \| cat -A`, `file *.md` |

No se corrigio (solo lectura). La wiki no contiene documentacion todavia.

## 3. Shell eafitOS (comandos reales via help)

Binario compilado en la copia temporal; `help` y `help <categoria>` ejecutados con `timeout 10`, colores ANSI removidos.

| Categoria | Comando | Descripcion (salida de help) | Tabla main.c / funcion |
| --- | --- | --- | --- |
| datos | d_create | Crea un archivo escribiendo un texto en el | main.c:19 / cat_datos.c:25 |
| datos | d_read | Lee y muestra el contenido de un archivo | main.c:26 / cat_datos.c:80 |
| datos | d_info | Muestra metadatos detallados de un archivo | main.c:33 / cat_datos.c:134 |
| datos | d_copy | Copia recursiva o lineal de bytes entre archivos | main.c:40 / cat_datos.c:186 |
| memoria | m_sbrk | Modifica el program break de la seccion heap | main.c:49 / cat_memoria.c:25 |
| memoria | m_mmap | Mapea una zona de memoria anonima y escribe un patron | main.c:56 / cat_memoria.c:87 |
| memoria | m_info | Muestra el estado del mapa de memoria del proceso actual | main.c:63 / cat_memoria.c:147 |
| monitoreo | p_fork | Crea un proceso hijo, demuestra sincronizacion y codigos de salida | main.c:72 / cat_monitoreo.c:31 |
| monitoreo | p_exec | Crea un proceso hijo y ejecuta un comando externo del sistema | main.c:79 / cat_monitoreo.c:94 |
| monitoreo | p_kill | Envia una senal especifica a un proceso en ejecucion | main.c:86 / cat_monitoreo.c:158 |
| monitoreo | p_monitor | Muestra el uso detallado de recursos de la CPU y memoria del shell | main.c:93 / cat_monitoreo.c:189 |
| utilidades | saludar | Muestra un saludo personalizado para el usuario actual | main.c:102 / cat_util.c:21 |
| utilidades | despedir | Muestra un mensaje de despedida personalizado | main.c:109 / cat_util.c:121 |
| utilidades | hora | Muestra la hora actual del sistema | main.c:116 / cat_util.c:52 |
| utilidades | fecha | Muestra la fecha actual del sistema | main.c:123 / cat_util.c:83 |
| editor | e_edit | Abre el editor de lineas (o, p, a, d, i, s, q) con compresor Huffman concurrente (c, x, t, k, w) en un proceso hijo | main.c:132 / cat_editor.c:29 |

Comandos internos del bucle (main.c:301-311): `exit`, `clear`, `help`. Salida con `exit` (main.c:301); el REPL tambien termina con Ctrl+D (README.md:526). Los nombres de memoria y utilidades coinciden con `shell/README.md:28-32` y con `cmd_*` en `cat_memoria.c` y `cat_util.c`.

Texto crudo de `help`:

```
--- Shell de Aprendizaje de Syscalls (SO2026B) ---
Este shell te permite explorar cómo funcionan las llamadas al sistema en Linux.
Los comandos están clasificados en categorías.

Categorías disponibles:
  datos      - Comandos de archivos y datos (open, read, write, stat, ...)
  memoria    - Comandos de control de heap y memoria (sbrk, mmap, ...)
  monitoreo  - Comandos de procesos, señales y recursos (fork, exec, kill, getrusage)
  utilidades - Comandos útiles del sistema (saludar, hora, fecha, despedir)
  editor     - Editor de texto por líneas sobre syscalls (open, read, write, lseek, ftruncate) con compresor Huffman concurrente

Uso general:
  help <categoria>  - Muestra comandos específicos de una categoría.
  help <comando>    - Explica el uso y las syscalls de un comando específico.
  clear             - Limpia la pantalla.
  exit              - Cierra el shell.
```

## 4. Editor y compresor

### 4.1 Comando `w` (P1)

- Despacho: `editor/editor.c:720` (`case 'w': if (ed->huf) he_esperar(ed->huf);`); ayuda en `editor.c:664` ("espera a que el trabajo Huffman termine").
- Rama leida: `editor/huf_editor.c:397-404`: toma `estado_mtx` y hace `pthread_cond_wait(&estado_cv)` mientras el estado sea `HE_RUNNING` o `HE_CANCELLING`; al salir llama `unir_hilo(he)` (recoge el hilo orquestador).
- Conclusion: `w` no guarda nada; bloquea el REPL hasta que termine el trabajo Huffman en segundo plano y une su hilo. Los comandos a/d/i se rechazan mientras hay trabajo (`editor.c:244-248`).
- Ejecucion real: `o big.txt`, `c`, `t` (0 %), `w` (imprimio 10 % a 90 % y "[huf] 100 % - compresion terminada"), `t` ("terminado ... 100 %").

### 4.2 Formato .huf (P2)

Documento `editor/docs/FORMATO_HUF.md:19-45` y comentario `editor/huffman.h:43-58`: encabezado de 276 bytes (magic HUF1 en 0, u64 tamano original en 4, u32 tamano de bloque en 12, u32 nbloques en 16, 256 longitudes u8 en 20), tabla de nbloques entradas de 32 bytes (u64 offset, u64 longitud comprimida, u64 longitud original, u32 CRC32, u32 nbits) y flujos de bits. Escritura en `huffman.c:675` (`le_poner32(cab + 12, tam_bloque)`).

Generado con `huf_cli c small.txt small.huf` sobre 25 bytes ("hola mundo\nsegunda linea\n"):

```
00000000: 4855 4631 1900 0000 0000 0000 0000 0100  HUF1............
00000010: 0100 0000 0000 0000 0000 0000 0000 0400  ................
00000114: 3401 0000 0000 0000 0c00 0000 0000 0000  4...............
00000124: 1900 0000 0000 0000 bcbe 6f68 5e00 0000  ..........oh^...
```

| Campo | Documento | Bytes reales | Coincide |
| --- | --- | --- | --- |
| magic | "HUF1" | `48 55 46 31` | si |
| tamano original (u64 @4) | 25 | `19 00 00 00 00 00 00 00` | si |
| tamano de bloque (u32 @12) | 65536 (por defecto) | `00 00 01 00` | si |
| nbloques (u32 @16) | 1 | `01 00 00 00` | si |
| longitudes de codigo (256 B @20) | 0 = ausente | 14 simbolos presentes (p. ej. byte 10 y 32 con 4 bits) | si |
| entrada 0 @276: offset | 276 + 32 = 308 | `34 01 ...` = 0x134 = 308 | si |
| entrada 0: comprimido / original | n/a | 12 / 25 | si |
| entrada 0: CRC32 / nbits | n/a | `bcbe6f68` (0x686fbebc) / 94 | si |
| tamano total | 308 + 12 | 320 bytes (`ls -l small.huf`) | si |

Con bloque de 16384 (`huf_cli c a.txt a.huf 16384`) sobre 270177 bytes: tamano de bloque 16384, nbloques 17, archivo de 204842 bytes.

### 4.3 Pool de hilos y su regla (P3)

- Regla: `editor/hilos.c:129-136`: si `n <= 0`, `cpus = sysconf(_SC_NPROCESSORS_ONLN)` (minimo 1) y `n = min(cpus, HILOS_MAX_DEFECTO)` con `#define HILOS_MAX_DEFECTO 8` (`hilos.c:16`). El editor pide 0 (`huf_editor.c:191`); el motor llama `hilos_crear(opc->nhilos)` en `huffman.c:1333` y `huffman.c:1749`.
- Evaluacion: nproc = 12 da min(12, 8) = 8; nproc = 4 daria 4 (aritmetica de la regla, no ejecutada). Los 8 hilos con 12 logicos salen de la regla (tope 8), no de otro mecanismo.
- Medicion: compresion de 202631579 bytes en `editor`, maximo 10 entradas en `/proc/<pid>/task` = 8 workers + hilo REPL + hilo orquestador (`huf_editor.c:327`).
- Modelo: `Pool` con un mutex que protege la cola, el contador de tareas y `en_curso`, y dos variables de condicion (`hay_tareas`, `cola_vacia`, `hilos.c:151-168`); los workers se crean con `pthread_create` (`hilos.c:179`) y se unen al apagar; si uno falla se apagan los ya creados. Fases con ventana W >= nhilos para acotar memoria sin interbloqueo (`huffman.c:890`, `1155`). Estado del trabajo bajo `estado_mtx`/`estado_cv`; salida serializada con `salida_mtx` (`huf_editor.c:56-80`). Un solo trabajo activo a la vez (`huf_editor.c:296`). Limites: cancelacion cooperativa entre bloques (README.md:688).

### 4.4 Progreso en porcentaje (P4)

- Callback `cb_progreso` (`huf_editor.c:138-170`): guarda `hechos/total` solo si avanza (nunca retrocede, `:145-148`), calcula `pct`, redondea a multiplo de 10 y, si es nuevo y menor que 100, imprime `[huf] N % (comprimiendo|descomprimiendo ruta)` (`:169`). El 100 % lo imprime la finalizacion (`huf_editor.c:226`). El comando `t` imprime `estado ... N %` (`huf_editor.c:370`).
- Ejecucion real (editor normal y `editor_lento`, archivo de 2431593 bytes): salida con 10 %, 20 % ... 90 %, luego "[huf] 100 % - compresion terminada: big.txt.huf" y `t` = "terminado (compresion big.txt) 100 %".
- Resultado: la salida es por hitos de 10 %, no un contador continuo.

## 5. Pruebas

Ejecutadas en copia temporal con `timeout 300 make <target>`.

| Suite | Comando | Total | Pasaron | Fallaron | Exit |
| --- | --- | --- | --- | --- | --- |
| Editor + shell + integracion | `make test` | 73 | 73 | 0 | 0 |
| Integracion shell (e_edit) | seccion de `make test` | 10 | 10 | 0 | n/a |
| Integracion shell -> editor -> compresor | seccion de `make test` | 7 | 7 | 0 | n/a |
| Compresor (suite Huffman) | `make test-huf` | 52 | 52 | 0 | 0 |
| Compresor con ASan/UBSan/LSan | `make test-asan` | 52 | 52 | 0 | 0 |
| Compresor con TSan | `make test-tsan` | 52 | 52 | 0 | 0 |

Secciones de `make test` (73): o 12, i 7, p 7, s 6, d 6, archivo vacio 6, q/invalidos 5, a 3, compilacion 3, fugas de memoria 1, integracion shell 10, integracion compresor 7. Secciones de `test-huf` (52): correccion 25, errores 7, carreras 7, cancelacion 9, progreso monotono 2, estres 1, rendimiento 1. No existe una suite independiente solo del shell: el shell se prueba dentro de `run_tests.sh`.

Compilacion y advertencias:

| Proyecto | Banderas | Advertencias | Exit |
| --- | --- | --- | --- |
| editor | `-Wall -Wextra -Werror -std=gnu99 -g -D_GNU_SOURCE`, `-pthread` (`editor/Makefile:2-4`) | 0 | 0 |
| shell | `-Wall -Wextra -std=gnu99 -g -D_GNU_SOURCE -I../editor`, `-pthread` (`shell/Makefile:1-6`) | 0 | 0 |

Cifras historicas: `README.md:788` 73 (make test) y `README.md:789-791` 52 (test-huf, test-asan, test-tsan); `editor/docs/REPORTE_TECNICO.md:199,210` 73. Las cifras 62 y 66 no aparecen en README.md, editor/docs ni shell/README.md (grep). Cifra vigente: 73 para `make test` y 52 para las suites del compresor; coinciden con 73 y 52, y 62 y 66 quedan NO DETERMINABLE como origen (no hay fuente en el repo).

Hallazgo de saltos de linea: 19 archivos en WSL con CRLF (`file`), entre ellos `shell/Makefile`, `editor/editor.c`, `editor/lista.c`, `editor/main.c` y todo `shell/*.c`. No se convirtio nada: los `.sh` estan en LF, `make` y `gcc` aceptaron los CRLF y los builds terminaron en 0.

## 6. Entorno y viabilidad para Parte III

| ID | Componente | Valor | Comando | Salida |
| --- | --- | --- | --- | --- |
| D1 | Kernel | 6.18.54.1-microsoft-standard-WSL2 | `uname -r` | `6.18.54.1-microsoft-standard-WSL2` |
| D2 | WSL | Distro "Ubuntu" (WSL 2), Running; tambien fedoraremix y docker-desktop | `wsl.exe -l -v` | `* Ubuntu Running 2` |
| D3 | Configuracion | BPF_LSM=y, DEBUG_INFO_BTF=y, MODULES=y, SECURITY=y, KPROBES=y, FTRACE=y; ademas BPF_SYSCALL=y, BPF_JIT=y, FUNCTION_TRACER=y, KPROBE_EVENTS=y | `zcat /proc/config.gz \| grep ...` | ver valores |
| D4 | LSM activos | `CONFIG_LSM="landlock,lockdown,yama,loadpin,safesetid,integrity,selinux,apparmor,tomoyo,bpf"`; cmdline sin `lsm=`; `/sys/kernel/security/lsm` no encontrado | `cat /sys/kernel/security/lsm; cat /proc/cmdline` | "No such file or directory" |
| D5 | BTF | `/sys/kernel/btf/vmlinux` existe, 6018902 bytes | `ls -l /sys/kernel/btf/vmlinux` | `-r--r--r-- 1 root root 6018902` |
| D6 | Modulos y fuentes | `/lib/modules`: 6.18.33.2, 6.18.40.1, 6.18.54.1; `/usr/src`: `linux-headers-6.18.54.1-microsoft-standard-WSL2` con solo `include/` (9.5 MB); sin Makefile, Kbuild, Module.symvers ni .config a profundidad 2 | `ls /lib/modules; ls /usr/src; find -L ...` | ver valores |
| D7 | Herramientas | `perf` presente (`/usr/lib/linux-tools/6.18.54.1-microsoft-standard-WSL2/bin/perf`, version 6.18.54.1); clang, llc, bpftool, strace, gdb, ltrace, valgrind, pkg-config: no encontrado | `which clang llc bpftool perf strace gdb` | solo perf |
| D8 | Paquetes | `libbpf1:amd64 1:1.3.0-2build2` (solo runtime); sin linux-headers ni clang en dpkg | `dpkg -l \| grep -E ...` | 1 linea |
| D9 | Compilador | gcc 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1), GNU Make 4.3 | `gcc --version; make --version` | ver valores |
| D10 | Trazas | `tracefs` en `/sys/kernel/tracing` y `debugfs` en `/sys/kernel/debug` montados; `ls` da "Permission denied" sin root; `perf_event_paranoid=2`, `kptr_restrict=1`, `unprivileged_bpf_disabled=2`, `ptrace_scope=1` | `ls /sys/kernel/tracing; mount \| grep ...` | ver valores |
| D11 | Hooks | grep en `/usr/include` y `/usr/src`: sin coincidencias. `/proc/kallsyms` lista `security_mmap_file`, `security_file_mprotect`, `bpf_lsm_mmap_file`, `bpf_lsm_file_mprotect`, `handle_mm_fault` (direcciones en cero por `kptr_restrict`); sin bpftool ni `lsm_hook_defs.h`, las firmas no se pueden dumpear | `grep -rnE 'mmap_file\|file_mprotect' /usr/include /usr/src; grep ... /proc/kallsyms` | ver valores |
| D12 | sudo | `sudo: a password is required`, codigo 1; usuario `relaxingend` (uid 1000) | `sudo -n true; echo $?` | `sudo: a password is required` |

Viabilidad de las alternativas (requisitos de C tomados de las salidas D3 a D8, no de memoria):

| Alt. | Requisito | Valor medido | Estado |
| --- | --- | --- | --- |
| A LKM + System Call Table | Modulos habilitados | CONFIG_MODULES=y (D3) | CUMPLE |
| A | Construir modulo (kbuild) | `build/` es un symlink a un directorio con solo `include/`, sin Makefile ni Module.symvers (D6) | NO CUMPLE |
| A | Cargar modulo | Requiere root; `sudo -n` falla (D12) | NO CUMPLE (sin intervencion de Miguel) |
| A | Acceso a `sys_call_table` | No verificado en `/proc/kallsyms` | NO DETERMINABLE |
| B LKM + kprobes/ftrace | CONFIG_KPROBES, FTRACE, KPROBE_EVENTS | y, y, y (D3) | CUMPLE |
| B | Construir y cargar modulo | Igual que A (D6, D12) | NO CUMPLE |
| B | Leer tracefs/debugfs | Montados, sin permiso sin root (D10) | NO CUMPLE sin root |
| C eBPF-LSM + libbpf | CONFIG_BPF_LSM | y (D3) | CUMPLE |
| C | CONFIG_DEBUG_INFO_BTF y archivo BTF | y; `/sys/kernel/btf/vmlinux` presente (D3, D5) | CUMPLE |
| C | Kernel 5.7 o superior | 6.18.54.1 (D1) | CUMPLE |
| C | "bpf" en lsm= | En `CONFIG_LSM` por defecto; cmdline sin `lsm=`; lista activa ilegible (D4) | NO DETERMINABLE |
| C | clang/llc, bpftool, libbpf-dev | No encontrados; solo `libbpf1` runtime (D7, D8) | NO CUMPLE |
| C | Hooks `mmap_file`, `file_mprotect` | Simbolos presentes en kallsyms (D11); firmas sin verificar | CUMPLE (nombres), firmas NO DETERMINABLE |
| C | Instalar toolchain / cargar BPF | Sin sudo sin contrasena (D12); `unprivileged_bpf_disabled=2` (D10) | NO CUMPLE sin intervencion de Miguel |
| D LSM clasico (recompila kernel) | Fuentes y configuracion del kernel | `/usr/src` solo `include/`; no hay arbol de fuentes (D6) | NO CUMPLE |
| E LD_PRELOAD (linea base) | gcc y make | D9 | CUMPLE |

## 7. Tabla de pendientes

| ID | Pendiente | Metodo | Evidencia | Veredicto |
| --- | --- | --- | --- | --- |
| P1 | Que hace `w` | Lectura de rama + ejecucion | `editor.c:720`, `huf_editor.c:397-404`; sesion con `o/c/t/w/t` | CONFIRMADO |
| P2 | Formato .huf campo a campo | Doc + codigo + `xxd` | `FORMATO_HUF.md:19-45`, `huffman.h:43-58`, volcado de `small.huf` (320 bytes) | CONFIRMADO |
| P3 | Regla del pool automatico | Lectura + medicion de hilos | `hilos.c:16,129-136`; 10 hilos medidos con nproc 12 | CONFIRMADO |
| P4 | Progreso en porcentaje | Lectura + ejecucion | `huf_editor.c:138-170,226,370`; salida 10 % a 100 % | CONFIRMADO |
| P5 | Cifra vigente de pruebas | Ejecucion de las 4 suites | 73 / 52 / 52 / 52 PASS, 0 FAIL; 62 y 66 sin fuente | CONFIRMADO |
| P6 | Comandos reales del shell | `help` ejecutado + tabla main.c | 16 comandos, 5 categorias (seccion 3) | CONFIRMADO |
| P7 | Discrepancias documento/codigo | Grep + contraste | 11 casos en la seccion 8 | CONFIRMADO |
| P8 | Que son `shell/algo` y `GUIA_DIDACTICA.md` | `file`, `cat`, encabezados | `algo` es un archivo Java de 82 bytes, no carpeta; la guia (109 lineas) es documentacion con nombre viejo `sys_shell` (`GUIA_DIDACTICA.md:3,19`) | DESMENTIDO |
| P9 | `.gitignore` frente a `.o` y binarios | `git check-ignore -v` | 11 `.o` por `shell/.gitignore:1`; `eafitOS` por `.gitignore:7`; `sys_shell2` versionado y no ignorado | CONFIRMADO |
| P10 | Fechas, sustentacion, interactiva, enlace del informe | Grep en README y docs | `README.md:62` "Enlace POR CONFIRMAR"; `:63` buzon de interactiva "Pendiente"; sin fechas de entrega ni de sustentacion en el repo | REQUIERE A MIGUEL |
| D1 | Version de kernel | `uname -r` | 6.18.54.1-microsoft-standard-WSL2 | CONFIRMADO |
| D2 | Distros WSL | `wsl.exe -l -v` | Ubuntu (2, Running), fedoraremix, docker-desktop | CONFIRMADO |
| D3 | CONFIG_* clave | `zcat /proc/config.gz` | BPF_LSM, DEBUG_INFO_BTF, MODULES, SECURITY, KPROBES, FTRACE = y | CONFIRMADO |
| D4 | LSM activos y `lsm=` | `cat /sys/kernel/security/lsm`, `/proc/cmdline` | Archivo ausente; cmdline sin `lsm=`; `CONFIG_LSM` incluye `bpf` | NO DETERMINABLE |
| D5 | BTF disponible | `ls -l` | `/sys/kernel/btf/vmlinux`, 6018902 bytes | CONFIRMADO |
| D6 | Modulos y headers | `ls`, `find` | Headers con solo `include/`; sin Makefile/Kbuild | CONFIRMADO |
| D7 | Herramientas | `which` | Solo perf; clang, llc, bpftool, strace, gdb no encontrados | CONFIRMADO |
| D8 | Paquetes libbpf/headers/clang | `dpkg -l` | Solo `libbpf1` | CONFIRMADO |
| D9 | gcc y make | `--version` | gcc 13.3.0, Make 4.3 | CONFIRMADO |
| D10 | tracefs/debugfs | `ls`, `mount` | Montados, "Permission denied" | CONFIRMADO |
| D11 | Hooks mmap/mprotect | grep headers + kallsyms | Simbolos presentes; firmas no dumpeables | CONFIRMADO |
| D12 | sudo sin contrasena | `sudo -n true` | "a password is required", codigo 1 | CONFIRMADO |

Conteo: CONFIRMADO 19, DESMENTIDO 1, NO DETERMINABLE 1, REQUIERE A MIGUEL 1.

## 8. Discrepancias entre documentos y codigo (gana el codigo)

| Documento:linea | Afirmacion | Codigo archivo:linea | Valor real |
| --- | --- | --- | --- |
| promptGenerarReporte.md:12 | Un solo commit "Initial commit"; README.md modificado; .gitignore, capturas/, editor/ y shell/ sin versionar | `git log`, `git ls-files` | 4 commits (`7d42d7e`, `af4771a`, `61abe3d`, `462e050`); README.md sin cambios; .gitignore, capturas/.gitkeep, editor/ y shell/ versionados; 19 archivos con CRLF difieren del indice |
| promptGenerarReporte.md:46 | Distro `Ubuntu-24.04` | `wsl -l -v` | El nombre real es `Ubuntu` (24.04.1 LTS en `/etc/os-release`); con `Ubuntu-24.04` WSL responde WSL_E_DISTRO_NOT_FOUND |
| promptGenerarReporte.md:11,53 | `shell/algo/` es una carpeta con contenido | `shell/algo` (archivo) | Archivo de 82 bytes con un fragmento Java de 3 lineas |
| promptGenerarReporte.md:11 | `capturas/` contiene evidencia de la Parte II | `capturas/3.1/.gitkeep` | Solo un `.gitkeep` de 0 bytes, sin imagenes |
| README.md:785 | Resultados medidos en kernel 6.18.40.1 | `uname -r` | 6.18.54.1; el 6.18.40.1 sigue en `/lib/modules` pero no es el activo |
| README.md:190 | "versión del kernel de WSL2 desconocida" | `uname -r` | 6.18.54.1-microsoft-standard-WSL2 (D1) |
| README.md:109 | La IA "no confirma que el kernel de WSL2 soporte BPF LSM" | `/proc/config.gz` | CONFIG_BPF_LSM=y, `bpf` en CONFIG_LSM, BTF presente (D3 a D5); la lista de LSM activos no se pudo leer (D4) |
| shell/GUIA_DIDACTICA.md:3,19 y shell/README.md:96 | El proyecto y el prompt se llaman `sys_shell` | `shell/Makefile:6` (`ARCHSALIDA = eafitOS`), `shell/main.c:278` | El binario es `eafitOS`; `sys_shell` solo sobrevive en `.gitignore:2`, `shell/.gitignore:2` y el binario versionado `shell/sys_shell2` |
| shell/README.md:65 | `e_edit` ejecuta el editor con `o, p, a, d, i, s, q` | `shell/main.c:132` | El help incluye tambien `c, x, t, k, w` (compresor Huffman) |
| shell/README.md:122 | `help <categoria>` acepta datos, memoria, monitoreo o utilidades | `shell/main.c:228-230` | Tambien acepta `editor` (README.md:524 si lo lista) |
| README.md:818-824 | Tabla de estructura del repositorio con shell, editor, tests, docs y capturas | `git ls-files` | Omite `Parte1_Analisis_Conceptual.md`, `shell/algo`, `shell/sys_shell2` y los `.gitignore/.gitattributes` |

Sin discrepancia (verificadas): hilos 8 con 12 CPUs (`README.md:706`, `CONCURRENCIA.md:13,231` frente a `hilos.c:129-136`); formato .huf (`README.md:741-751`); 73 y 52 pruebas (`README.md:788-791`, `REPORTE_TECNICO.md:199,210`); progreso por hitos de 10 % (`README.md:691`).

## 9. Lo que falta para la Parte III

| Entregable de la rubrica | Estado | Evidencia |
| --- | --- | --- |
| Codigo C modularizado compatible con kernel 5.x o 6.x | falta | `README.md:458-460` ("Estado: Pendiente"); `git ls-files` sin fuentes de kernel, eBPF ni modulos; no se puede construir un LKM (D6) |
| Demostracion de interceptacion o cifrado de regiones mmap | falta | Solo existe `m_mmap` (`shell/cat_memoria.c:87`), un mapeo anonimo con patron, sin interceptacion; `README.md:141-142` describe pruebas planeadas con errno "por confirmar" |
| Makefile sin advertencias | existe para la base; falta el de la Parte III | `editor/Makefile` con `-Werror` y `shell/Makefile`: 0 advertencias (seccion 5); ningun Makefile compila codigo de kernel o BPF |
| Scripts de prueba con intentos no autorizados y carreras | existe solo para el compresor; falta para mmap | `editor/tests/run_tests_huf.sh` (carreras con la edicion, cancelacion); ningun script de accesos mmap no autorizados |
| Justificacion kernel frente a usuario | existe como diseno, sin medicion | `README.md:337` (seleccion C), `:410-419` (anillos y limite de cobertura), `:427-435` (matriz A a E); el propio README la marca cualitativa (`:425`) |
| Medicion de overhead con perf, strace o ftrace | falta | `perf` instalado (D7) pero `perf_event_paranoid=2`; `strace` no encontrado; `tracefs` sin permiso sin root (D10); `README.md:425` dice "se medira en la implementacion" |
| Limitaciones | existe parcialmente | `README.md:419-421` (cobertura al crear el mapeo, no por acceso; hooks "por confirmar"); sin limitaciones medidas |
| Toolchain para la alternativa C | falta | clang, llc, bpftool, libbpf-dev no encontrados (D7, D8); instalarlos exige sudo con contrasena (D12): REQUIERE A MIGUEL |
| Informe, enlace de sustentacion y buzon de interactiva | falta | `README.md:62-63`: "POR CONFIRMAR" y "Pendiente" (P10) |
