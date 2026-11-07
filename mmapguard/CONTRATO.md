# CONTRATO de mmapguard

Este archivo fija las interfaces entre los modulos. Cada modulo tiene un unico dueno y nadie cambia lo que no es suyo.

## Rutas

| Ruta bajo mmapguard/ | Contenido |
| --- | --- |
| include/mmapguard_common.h | Estructuras, constantes, nombres de mapas y pines (compartido kernel y usuario) |
| bpf/mmapguard.bpf.c | Programas `lsm/mmap_file` y `lsm/file_mprotect`; opcional `fentry/handle_mm_fault` (solo conteo, autoload apagado) |
| src/mmapguard.c | CLI y cargador libbpf |
| src/policy.c, src/policy.h | Banderas, llave (dev, ino) desde ruta; sin libbpf |
| src/events.c, src/events.h | Formato de eventos; sin libbpf |
| demo/mg_probe.c, demo/demo.sh | Demostracion de intercepcion |
| tests/ | Pruebas unitarias, intentos no autorizados, carreras, `run_tests.sh` |
| Makefile | `all`, `bpf`, `user`, `test`, `clean` (y `bench` agregado despues) |
| bench/ | `bench_mmap.c`, `bench.sh`, `bench_con_defensa.sh` |

## Firmas verificadas en este kernel (6.18.54.1-microsoft-standard-WSL2)

Obtenidas con `bpftool btf dump file /sys/kernel/btf/vmlinux` (bpftool 7.4.0 extraido en ~/.mmapguard-tc):

| Funcion | Argumentos | Retorno |
| --- | --- | --- |
| `bpf_lsm_mmap_file` (punto de enganche `lsm/mmap_file`) | `struct file *file, unsigned long reqprot, unsigned long prot, unsigned long flags` | int |
| `bpf_lsm_file_mprotect` (`lsm/file_mprotect`) | `struct vm_area_struct *vma, unsigned long reqprot, unsigned long prot` | int |
| `security_mmap_file` (envoltorio del kernel, no es el punto de enganche) | `struct file *file, unsigned long prot, unsigned long flags` (3 argumentos) | int |
| `handle_mm_fault` (opcional, fentry) | `struct vm_area_struct *vma, unsigned long address, unsigned int flags, struct pt_regs *regs` | vm_fault_t (unsigned int) |

Campos CO-RE confirmados en el `vmlinux.h` generado: `file->f_inode`, `inode->i_ino`, `inode->i_sb`, `super_block->s_dev`, `vma->vm_file`, `vma->vm_flags` (miembro `const vm_flags_t`, `unsigned long`).
Helpers permitidos para `BPF_PROG_TYPE_LSM` en este kernel: POR CONFIRMAR (verificarlo exige cargar el programa, que pide root). Usar solo `bpf_get_current_pid_tgid`, `bpf_get_current_uid_gid`, `bpf_get_current_comm`, `bpf_ktime_get_ns`, `bpf_map_lookup_elem`, `bpf_ringbuf_reserve/submit/discard`, que son los usados por los ejemplos oficiales de BPF LSM.

## CLI (`mmapguard`)

| Comando | Accion |
| --- | --- |
| `mmapguard load [--count-faults]` | Abre, carga y engancha el esqueleto; pina mapas y enlaces en `/sys/fs/bpf/mmapguard/` (crea el directorio; si `/sys/fs/bpf` no es bpffs intenta `mount -t bpf`) y termina. `--count-faults` activa el fentry opcional |
| `mmapguard unload` | Borra los pines (el kernel desengancha los programas y libera mapas) |
| `mmapguard policy add <ruta> <banderas>` | Inserta o reemplaza la regla del archivo (stat -> dev, ino). `<banderas>` = lista separada por comas de `deny-write-shared`, `deny-exec`, `audit-only` |
| `mmapguard policy del <ruta>` | Borra la regla |
| `mmapguard policy list` | Una linea por regla: `dev=<major>:<minor> ino=<n> flags=<lista>` |
| `mmapguard events [--count N] [--timeout MS]` | Lee el ringbuf y escribe una linea por evento con `mg_event_format()` |

Codigos de salida: 0 ok, 1 uso incorrecto, 2 error del kernel o de permisos (EPERM, EACCES, ENOENT del pin, etc.). Mensajes de error a stderr, en una linea, con la accion manual si aplica (por ejemplo "ejecutar con sudo").

## API sin libbpf (para pruebas con solo gcc)

```c
/* policy.h */
int  mg_flags_parse(const char *s, __u32 *out);              /* 0 ok, -1 invalido (vacio, token desconocido, token repetido permitido) */
int  mg_flags_format(__u32 flags, char *buf, size_t n);      /* escribe "deny-write-shared,deny-exec,audit-only" segun bits, "none" si 0; devuelve largo o -1 si no cabe */
int  mg_key_from_path(const char *path, struct mg_key *key); /* 0 ok, -1 con errno de stat; dev = (major<<20)|minor del st_dev */

/* events.h */
const char *mg_hook_name(__u32 hook);                        /* "mmap_file", "file_mprotect", "desconocido" */
int  mg_event_format(const struct mg_event *ev, char *buf, size_t n);
/* linea: ts=<ns> pid=<n> uid=<n> comm=<s> dev=<major>:<minor> ino=<n> hook=<nombre> prot=0x<hex> flags=0x<hex> verdict=<deny|audit>
 * devuelve caracteres escritos, -1 si no cabe. dev se imprime decodificado: major = dev >> 20, minor = dev & 0xfffff */
```

## Politica de decision (la implementa bpf/mmapguard.bpf.c)

| Hook | Violacion |
| --- | --- |
| `mmap_file` | `file` no nulo, regla existe para (s_dev, i_ino) y (`DENY_WRITE_SHARED` con `flags & MAP_SHARED` y `prot & PROT_WRITE`) o (`DENY_EXEC` con `prot & PROT_EXEC`) |
| `file_mprotect` | `vma->vm_file` no nulo, regla existe y (`DENY_WRITE_SHARED` con `vm_flags & VM_SHARED` y `prot & PROT_WRITE`) o (`DENY_EXEC` con `prot & PROT_EXEC`) |

Ante violacion: emitir `mg_event`; devolver `-EPERM` si no hay `AUDIT_ONLY`, o 0 si lo hay. Sin violacion: devolver 0 sin evento. Mapeos anonimos (`file == NULL`) siempre pasan.

## Reglas de construccion

- Se compila siempre en una copia dentro de WSL (`~/mg_build/...`), nunca dentro del repo.
- Fuentes, Makefile y .sh con LF. Texto de codigo en ASCII.
- `-Wall -Wextra -Werror -std=gnu11` en todo C de usuario; el skeleton generado se incluye como cabecera de sistema (`-isystem`) para no fallar por codigo generado.
- Toolchain de usuario en `~/.mmapguard-tc` (`TC_PREFIX`): `usr/lib/llvm-18/bin/clang` (18.1.3), `usr/lib/linux-tools-6.8.0-137/bpftool` (7.4.0), cabeceras libbpf en `usr/include/bpf/`. Hace falta `LD_LIBRARY_PATH=$TC_PREFIX/usr/lib/x86_64-linux-gnu:$TC_PREFIX/usr/lib/llvm-18/lib`. La biblioteca de ejecucion es la del sistema (`/usr/lib/x86_64-linux-gnu/libbpf.so.1`, v1.3.0); se enlaza con `-l:libbpf.so.1`. `vmlinux.h` se genera en la compilacion con `bpftool btf dump file /sys/kernel/btf/vmlinux format c`.
- Cargar el programa BPF exige root o CAP_BPF: en este entorno `sudo -n` falla, por eso nada que dependa de la carga se ejecuta aqui.
