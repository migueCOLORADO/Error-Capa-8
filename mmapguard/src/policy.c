/* policy.c: banderas y llave (dev, ino). Sin libbpf ni vmlinux. */
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include "policy.h"

static const struct {
    const char *name;
    __u32 bit;
} mg_flag_names[] = {
    { "deny-write-shared", MG_DENY_WRITE_SHARED },
    { "deny-exec",         MG_DENY_EXEC },
    { "audit-only",        MG_AUDIT_ONLY },
};

#define MG_NFLAGS (sizeof(mg_flag_names) / sizeof(mg_flag_names[0]))

int mg_flags_parse(const char *s, __u32 *out)
{
    __u32 acc = 0;
    const char *p = s;

    if (!s || !out || *s == '\0')
        return -1;

    for (;;) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        size_t i;
        int found = 0;

        for (i = 0; i < MG_NFLAGS; i++) {
            if (strlen(mg_flag_names[i].name) == len &&
                strncmp(p, mg_flag_names[i].name, len) == 0) {
                acc |= mg_flag_names[i].bit;
                found = 1;
                break;
            }
        }
        if (!found)
            return -1; /* token vacio o desconocido */
        if (!end)
            break;
        p = end + 1;
    }

    *out = acc;
    return 0;
}

int mg_flags_format(__u32 flags, char *buf, size_t n)
{
    size_t used = 0;
    size_t i;

    if (!buf || n == 0)
        return -1;

    flags &= MG_FLAGS_ALL;
    if (flags == 0) {
        if (n < sizeof("none"))
            return -1;
        memcpy(buf, "none", sizeof("none"));
        return (int)(sizeof("none") - 1);
    }

    for (i = 0; i < MG_NFLAGS; i++) {
        size_t len;

        if (!(flags & mg_flag_names[i].bit))
            continue;
        len = strlen(mg_flag_names[i].name);
        /* coma (si no es el primero) + nombre + terminador */
        if (used + (used ? 1 : 0) + len + 1 > n)
            return -1;
        if (used)
            buf[used++] = ',';
        memcpy(buf + used, mg_flag_names[i].name, len);
        used += len;
    }
    buf[used] = '\0';
    return (int)used;
}

__u64 mg_dev_from_stat(dev_t st_dev)
{
    return ((__u64)major(st_dev) << 20) | (__u64)minor(st_dev);
}

int mg_key_from_path(const char *path, struct mg_key *key)
{
    struct stat st;

    if (!path || !key) {
        errno = EINVAL;
        return -1;
    }
    if (stat(path, &st) < 0)
        return -1;

    key->dev = mg_dev_from_stat(st.st_dev);
    key->ino = (__u64)st.st_ino;
    return 0;
}
