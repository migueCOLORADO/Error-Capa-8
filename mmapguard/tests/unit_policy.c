/* unit_policy.c: pruebas unitarias de mg_flags_parse, mg_flags_format y
 * mg_key_from_path (src/policy.c). Sin libbpf. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include "mmapguard_common.h"
#include "policy.h"
#include "mg_t.h"

static void test_parse(void)
{
    __u32 f;
    int rc;

    f = 0xffu;
    rc = mg_flags_parse("deny-write-shared", &f);
    T_CHECK(rc == 0 && f == MG_DENY_WRITE_SHARED, "parse: un token deny-write-shared (rc=%d f=0x%x)", rc, f);

    f = 0xffu;
    rc = mg_flags_parse("deny-write-shared,deny-exec", &f);
    T_CHECK(rc == 0 && f == (MG_DENY_WRITE_SHARED | MG_DENY_EXEC), "parse: dos tokens (rc=%d f=0x%x)", rc, f);

    f = 0xffu;
    rc = mg_flags_parse("deny-write-shared,deny-exec,audit-only", &f);
    T_CHECK(rc == 0 && f == MG_FLAGS_ALL, "parse: los tres tokens (rc=%d f=0x%x)", rc, f);

    f = 0xffu;
    rc = mg_flags_parse("audit-only", &f);
    T_CHECK(rc == 0 && f == MG_AUDIT_ONLY, "parse: solo audit-only (rc=%d f=0x%x)", rc, f);

    /* borde: token repetido es valido y no cambia el resultado */
    f = 0xffu;
    rc = mg_flags_parse("deny-exec,deny-exec", &f);
    T_CHECK(rc == 0 && f == MG_DENY_EXEC, "parse borde: token repetido valido (rc=%d f=0x%x)", rc, f);

    /* invalidas */
    f = 0;
    rc = mg_flags_parse("", &f);
    T_CHECK(rc == -1, "parse invalida: cadena vacia devuelve -1 (rc=%d)", rc);

    f = 0;
    rc = mg_flags_parse("foo", &f);
    T_CHECK(rc == -1, "parse invalida: token desconocido devuelve -1 (rc=%d)", rc);

    f = 0;
    rc = mg_flags_parse("deny-exec,foo", &f);
    T_CHECK(rc == -1, "parse invalida: lista con un token desconocido devuelve -1 (rc=%d)", rc);
}

static void test_format(void)
{
    char buf[128];
    int rc;
    size_t len;

    memset(buf, 'x', sizeof(buf));
    rc = mg_flags_format(0, buf, sizeof(buf));
    T_CHECK(rc == 4 && strcmp(buf, "none") == 0, "format: 0 produce none (rc=%d buf=%s)", rc, buf);

    rc = mg_flags_format(MG_FLAGS_ALL, buf, sizeof(buf));
    T_CHECK(rc == (int)strlen("deny-write-shared,deny-exec,audit-only") &&
                strcmp(buf, "deny-write-shared,deny-exec,audit-only") == 0,
            "format: los tres bits en orden (rc=%d buf=%s)", rc, buf);

    rc = mg_flags_format(MG_DENY_EXEC, buf, sizeof(buf));
    T_CHECK(rc == 9 && strcmp(buf, "deny-exec") == 0, "format: solo deny-exec (rc=%d buf=%s)", rc, buf);

    rc = mg_flags_format(MG_DENY_WRITE_SHARED | MG_AUDIT_ONLY, buf, sizeof(buf));
    T_CHECK(rc > 0 && strcmp(buf, "deny-write-shared,audit-only") == 0,
            "format: deny-write-shared y audit-only (rc=%d buf=%s)", rc, buf);

    /* ida y vuelta para todas las combinaciones validas */
    {
        int ok = 1;
        __u32 v;
        for (v = 1; v <= MG_FLAGS_ALL; v++) {
            __u32 back = 0;
            if (mg_flags_format(v, buf, sizeof(buf)) < 0 || mg_flags_parse(buf, &back) != 0 || back != v)
                ok = 0;
        }
        T_CHECK(ok, "format: ida y vuelta format+parse de las 7 combinaciones");
    }

    /* borde: ajuste exacto (largo + 1 byte del terminador) y uno menos */
    len = strlen("deny-write-shared,deny-exec,audit-only");
    rc = mg_flags_format(MG_FLAGS_ALL, buf, len + 1);
    T_CHECK(rc == (int)len && strlen(buf) == len, "format borde: buffer de largo+1 cabe exacto (rc=%d)", rc);
    rc = mg_flags_format(MG_FLAGS_ALL, buf, len);
    T_CHECK(rc == -1, "format borde: buffer de largo (sin lugar al terminador) devuelve -1 (rc=%d)", rc);

    /* invalidas: buffer pequeno */
    rc = mg_flags_format(0, buf, 4);
    T_CHECK(rc == -1, "format invalida: none no cabe en 4 bytes devuelve -1 (rc=%d)", rc);
    rc = mg_flags_format(MG_FLAGS_ALL, buf, 8);
    T_CHECK(rc == -1, "format invalida: lista completa no cabe en 8 bytes devuelve -1 (rc=%d)", rc);
    rc = mg_flags_format(MG_DENY_EXEC, buf, 1);
    T_CHECK(rc == -1, "format invalida: buffer de 1 byte devuelve -1 (rc=%d)", rc);
}

static __u64 dev_esperado(dev_t d)
{
    return ((__u64)major(d) << 20) | (__u64)minor(d);
}

static void test_key(void)
{
    char path[] = "/tmp/mg_unit_key.XXXXXX";
    char link[64];
    struct stat st, st2;
    struct mg_key k, k2;
    int fd, rc, e;

    fd = mkstemp(path);
    if (fd < 0) {
        T_CHECK(0, "key: no se pudo crear archivo temporal (%s)", strerror(errno));
        return;
    }
    close(fd);

    if (stat(path, &st) != 0) {
        T_CHECK(0, "key: stat del archivo temporal fallo (%s)", strerror(errno));
        unlink(path);
        return;
    }

    memset(&k, 0, sizeof(k));
    rc = mg_key_from_path(path, &k);
    T_CHECK(rc == 0 && k.ino == (__u64)st.st_ino && k.dev == dev_esperado(st.st_dev),
            "key: archivo regular da (dev, ino) de stat (rc=%d dev=0x%llx ino=%llu)", rc,
            (unsigned long long)k.dev, (unsigned long long)k.ino);
    T_CHECK(rc == 0 && (k.dev >> 20) == (__u64)major(st.st_dev) && (k.dev & 0xfffffu) == (__u64)minor(st.st_dev),
            "key: dev decodificado major=%u minor=%u", (unsigned)(k.dev >> 20), (unsigned)(k.dev & 0xfffffu));

    /* borde: el enlace simbolico se sigue y produce la misma llave */
    snprintf(link, sizeof(link), "%s.lnk", path);
    if (symlink(path, link) == 0) {
        memset(&k2, 0, sizeof(k2));
        rc = mg_key_from_path(link, &k2);
        T_CHECK(rc == 0 && k2.dev == k.dev && k2.ino == k.ino, "key borde: el enlace simbolico da la misma llave (rc=%d)", rc);
        unlink(link);
    } else {
        T_CHECK(0, "key borde: no se pudo crear el enlace simbolico (%s)", strerror(errno));
    }

    /* borde: un directorio tambien tiene llave */
    if (stat("/tmp", &st2) == 0) {
        memset(&k2, 0, sizeof(k2));
        rc = mg_key_from_path("/tmp", &k2);
        T_CHECK(rc == 0 && k2.ino == (__u64)st2.st_ino && k2.dev == dev_esperado(st2.st_dev),
                "key borde: directorio /tmp (rc=%d)", rc);
    }

    unlink(path);

    /* invalida: ruta inexistente */
    memset(&k, 0, sizeof(k));
    errno = 0;
    rc = mg_key_from_path(path, &k);
    e = errno;
    T_CHECK(rc == -1 && e == ENOENT, "key invalida: ruta inexistente devuelve -1 con ENOENT (rc=%d errno=%d)", rc, e);
}

int main(void)
{
    test_parse();
    test_format();
    test_key();
    return mg_t_fails ? 1 : 0;
}
