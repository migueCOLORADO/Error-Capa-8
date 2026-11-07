/* mmapguard_common.h: contrato compartido entre el programa eBPF-LSM (kernel) y
 * el cargador/CLI (espacio de usuario). NO CAMBIAR sin actualizar CONTRATO.md.
 *
 * Compila en dos contextos:
 *   - BPF: despues de "vmlinux.h" (define __VMLINUX_H__ y los tipos __u32, __u64, __s32).
 *   - Usuario: usa <linux/types.h>.
 */
#ifndef MMAPGUARD_COMMON_H
#define MMAPGUARD_COMMON_H

#ifndef __VMLINUX_H__
#include <linux/types.h>
#endif

/* ---- Nombres de mapas (SEC(".maps") y archivos pineados) ---- */
#define MG_MAP_POLICY   "mg_policy"   /* BPF_MAP_TYPE_HASH: struct mg_key -> struct mg_rule */
#define MG_MAP_EVENTS   "mg_events"   /* BPF_MAP_TYPE_RINGBUF de struct mg_event */
#define MG_MAP_STATS    "mg_stats"    /* BPF_MAP_TYPE_PERCPU_ARRAY[1] de __u64: fallos de pagina (opcional) */

#define MG_MAX_RULES      1024
#define MG_RINGBUF_BYTES  (256 * 1024)

/* ---- Pines en bpffs (el cargador los crea; la CLI los abre) ---- */
#define MG_PIN_DIR      "/sys/fs/bpf/mmapguard"
#define MG_PIN_LNK_MMAP "/sys/fs/bpf/mmapguard/lnk_mmap_file"
#define MG_PIN_LNK_MPRO "/sys/fs/bpf/mmapguard/lnk_file_mprotect"
#define MG_PIN_LNK_FLT  "/sys/fs/bpf/mmapguard/lnk_count_fault"

/* ---- Llave: el archivo se identifica por (dev, inode), nunca por ruta ----
 * dev usa la codificacion INTERNA del kernel: (major << 20) | minor, es decir
 * inode->i_sb->s_dev. Desde usuario se obtiene de stat(2) con
 * mg_dev_from_stat() (policy.c): ((__u64)major(st_dev) << 20) | minor(st_dev). */
struct mg_key {
    __u64 dev;
    __u64 ino;
};

/* ---- Regla ---- */
#define MG_DENY_WRITE_SHARED 0x1u /* niega mapeo MAP_SHARED con PROT_WRITE y mprotect que agregue PROT_WRITE a un VMA compartido del archivo */
#define MG_DENY_EXEC         0x2u /* niega mapeo o mprotect con PROT_EXEC */
#define MG_AUDIT_ONLY        0x4u /* no niega: solo registra la violacion */
#define MG_FLAGS_ALL         (MG_DENY_WRITE_SHARED | MG_DENY_EXEC | MG_AUDIT_ONLY)

struct mg_rule {
    __u32 flags;
};

/* ---- Hooks y veredictos ---- */
#define MG_HOOK_MMAP_FILE     1u
#define MG_HOOK_FILE_MPROTECT 2u

/* Un evento se emite SOLO cuando una regla se viola.
 * verdict = -1 (-EPERM) si se nego; 0 si la regla es AUDIT_ONLY (se permitio y se registro). */
#define MG_VERDICT_DENY  (-1)
#define MG_VERDICT_AUDIT 0

struct mg_event {
    __u64 ts_ns;      /* bpf_ktime_get_ns() */
    __u32 pid;        /* tgid */
    __u32 uid;
    char  comm[16];
    __u64 dev;        /* codificacion del kernel, igual que mg_key.dev */
    __u64 ino;
    __u32 hook;       /* MG_HOOK_* */
    __u32 prot;       /* prot efectivo del hook */
    __u32 flags;      /* mmap_file: flags de mmap; file_mprotect: vm_flags del VMA truncado a 32 bits */
    __s32 verdict;    /* MG_VERDICT_* */
};

/* ---- Constantes ABI de Linux usadas por la politica (estables) ---- */
#define MG_PROT_WRITE   0x2u
#define MG_PROT_EXEC    0x4u
#define MG_MAP_SHARED   0x01u   /* tambien activo en MAP_SHARED_VALIDATE (0x03) */
#define MG_VM_SHARED    0x8ul   /* VM_SHARED en vm_flags */

#endif /* MMAPGUARD_COMMON_H */
