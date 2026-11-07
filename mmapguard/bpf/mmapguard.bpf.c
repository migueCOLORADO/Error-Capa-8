/* mmapguard.bpf.c: programas eBPF-LSM que protegen archivos frente a mmap/mprotect.
 *
 * Politica: ver CONTRATO.md ("Politica de decision"). El archivo se identifica por
 * (inode->i_sb->s_dev, inode->i_ino), leido con CO-RE; no se usa bpf_d_path.
 * Helpers usados: bpf_map_lookup_elem, bpf_get_current_pid_tgid,
 * bpf_get_current_uid_gid, bpf_get_current_comm, bpf_ktime_get_ns y
 * bpf_ringbuf_reserve/submit.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "mmapguard_common.h"

#define MG_EPERM 1

/* Reglas: (dev, ino) -> flags. Las llena la CLI desde espacio de usuario. */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MG_MAX_RULES);
    __type(key, struct mg_key);
    __type(value, struct mg_rule);
} mg_policy SEC(".maps");

/* Eventos de violacion hacia espacio de usuario. */
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, MG_RINGBUF_BYTES);
} mg_events SEC(".maps");

/* Contador opcional de fallos de pagina (solo lo usa el fentry de abajo). */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} mg_stats SEC(".maps");

/* Evalua la regla del inode y devuelve 0 o -EPERM. Emite el evento si hay violacion.
 * shared: el mapeo es compartido (MAP_SHARED o VM_SHARED segun el hook).
 * evflags: valor que se guarda en mg_event.flags. */
static __always_inline int mg_decide(struct inode *inode, __u32 hook, __u32 prot,
                                     __u32 evflags, bool shared)
{
    struct mg_key key = {};
    struct mg_rule *rule;
    struct mg_event *ev;
    __u32 rf;
    bool violation;
    int verdict;

    if (!inode)
        return 0;

    key.dev = BPF_CORE_READ(inode, i_sb, s_dev);
    key.ino = BPF_CORE_READ(inode, i_ino);

    rule = bpf_map_lookup_elem(&mg_policy, &key);
    if (!rule)
        return 0;

    rf = rule->flags;
    violation = ((rf & MG_DENY_WRITE_SHARED) && shared && (prot & MG_PROT_WRITE)) ||
                ((rf & MG_DENY_EXEC) && (prot & MG_PROT_EXEC));
    if (!violation)
        return 0;

    verdict = (rf & MG_AUDIT_ONLY) ? MG_VERDICT_AUDIT : MG_VERDICT_DENY;

    /* Si la reserva falla igual se aplica el veredicto, solo se pierde el evento. */
    ev = bpf_ringbuf_reserve(&mg_events, sizeof(*ev), 0);
    if (ev) {
        ev->ts_ns = bpf_ktime_get_ns();
        ev->pid = bpf_get_current_pid_tgid() >> 32;
        ev->uid = (__u32)bpf_get_current_uid_gid();
        bpf_get_current_comm(&ev->comm, sizeof(ev->comm));
        ev->dev = key.dev;
        ev->ino = key.ino;
        ev->hook = hook;
        ev->prot = prot;
        ev->flags = evflags;
        ev->verdict = verdict;
        bpf_ringbuf_submit(ev, 0);
    }

    return verdict == MG_VERDICT_DENY ? -MG_EPERM : 0;
}

SEC("lsm/mmap_file")
int BPF_PROG(mg_mmap_file, struct file *file, unsigned long reqprot,
             unsigned long prot, unsigned long flags)
{
    struct inode *inode;

    /* Mapeo anonimo: siempre pasa. */
    if (!file)
        return 0;

    inode = BPF_CORE_READ(file, f_inode);
    return mg_decide(inode, MG_HOOK_MMAP_FILE, (__u32)prot, (__u32)flags,
                     (flags & MG_MAP_SHARED) != 0);
}

SEC("lsm/file_mprotect")
int BPF_PROG(mg_file_mprotect, struct vm_area_struct *vma, unsigned long reqprot,
             unsigned long prot)
{
    struct file *file;
    struct inode *inode;
    unsigned long vm_flags;

    file = BPF_CORE_READ(vma, vm_file);
    if (!file)
        return 0;

    inode = BPF_CORE_READ(file, f_inode);
    vm_flags = BPF_CORE_READ(vma, vm_flags);
    return mg_decide(inode, MG_HOOK_FILE_MPROTECT, (__u32)prot, (__u32)vm_flags,
                     (vm_flags & MG_VM_SHARED) != 0);
}

/* Opcional y aislado: solo cuenta fallos de pagina. El cargador lo deja con
 * autoload apagado salvo que se pida "load --count-faults"; su ausencia no
 * afecta a los dos programas LSM. */
SEC("fentry/handle_mm_fault")
int BPF_PROG(mg_count_fault, struct vm_area_struct *vma, unsigned long address,
             unsigned int flags, struct pt_regs *regs)
{
    __u32 k = 0;
    __u64 *v = bpf_map_lookup_elem(&mg_stats, &k);

    if (v)
        *v += 1;
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
