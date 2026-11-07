/* events.c: formato de eventos. Sin libbpf ni vmlinux. */
#include <stdio.h>
#include <string.h>
#include "events.h"

const char *mg_hook_name(__u32 hook)
{
    switch (hook) {
    case MG_HOOK_MMAP_FILE:
        return "mmap_file";
    case MG_HOOK_FILE_MPROTECT:
        return "file_mprotect";
    default:
        return "desconocido";
    }
}

int mg_event_format(const struct mg_event *ev, char *buf, size_t n)
{
    char comm[sizeof(ev->comm) + 1];
    size_t i;
    int w;

    if (!ev || !buf || n == 0)
        return -1;

    /* comm puede no traer terminador; se copia acotado y se limpian no imprimibles. */
    memcpy(comm, ev->comm, sizeof(ev->comm));
    comm[sizeof(ev->comm)] = '\0';
    for (i = 0; comm[i] != '\0'; i++) {
        if ((unsigned char)comm[i] < 0x20 || (unsigned char)comm[i] > 0x7e)
            comm[i] = '?';
    }

    w = snprintf(buf, n,
                 "ts=%llu pid=%u uid=%u comm=%s dev=%u:%u ino=%llu hook=%s "
                 "prot=0x%x flags=0x%x verdict=%s",
                 (unsigned long long)ev->ts_ns, (unsigned)ev->pid, (unsigned)ev->uid,
                 comm,
                 (unsigned)(ev->dev >> 20), (unsigned)(ev->dev & 0xfffffu),
                 (unsigned long long)ev->ino, mg_hook_name(ev->hook),
                 (unsigned)ev->prot, (unsigned)ev->flags,
                 ev->verdict < 0 ? "deny" : "audit");
    if (w < 0 || (size_t)w >= n)
        return -1;
    return w;
}
