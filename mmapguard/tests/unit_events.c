/* unit_events.c: pruebas unitarias de mg_hook_name y mg_event_format
 * (src/events.c). Sin libbpf. */
#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mmapguard_common.h"
#include "events.h"
#include "mg_t.h"

static struct mg_event ev_base(void)
{
    struct mg_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.ts_ns = 123456789ull;
    ev.pid = 4242;
    ev.uid = 1000;
    strcpy(ev.comm, "mg_probe");
    ev.dev = ((__u64)259 << 20) | 3u;
    ev.ino = 1048577ull;
    ev.hook = MG_HOOK_MMAP_FILE;
    ev.prot = 0x3;
    ev.flags = 0x1;
    ev.verdict = MG_VERDICT_DENY;
    return ev;
}

static void test_hook_name(void)
{
    T_CHECK(strcmp(mg_hook_name(MG_HOOK_MMAP_FILE), "mmap_file") == 0, "hook_name: 1 es mmap_file");
    T_CHECK(strcmp(mg_hook_name(MG_HOOK_FILE_MPROTECT), "file_mprotect") == 0, "hook_name: 2 es file_mprotect");
    T_CHECK(strcmp(mg_hook_name(0), "desconocido") == 0, "hook_name borde: 0 es desconocido");
    T_CHECK(strcmp(mg_hook_name(99), "desconocido") == 0, "hook_name invalida: 99 es desconocido");
    T_CHECK(strcmp(mg_hook_name(0xffffffffu), "desconocido") == 0, "hook_name invalida: 0xffffffff es desconocido");
}

static void test_format(void)
{
    char buf[256];
    struct mg_event ev;
    int rc;
    size_t len;
    const char *esperado;

    /* camino feliz: mmap_file denegado, dev 259:3 */
    ev = ev_base();
    esperado = "ts=123456789 pid=4242 uid=1000 comm=mg_probe dev=259:3 ino=1048577 hook=mmap_file prot=0x3 flags=0x1 verdict=deny";
    memset(buf, 'x', sizeof(buf));
    rc = mg_event_format(&ev, buf, sizeof(buf));
    T_CHECK(rc == (int)strlen(esperado) && strcmp(buf, esperado) == 0,
            "event_format: linea completa mmap_file deny (rc=%d)", rc);
    if (strcmp(buf, esperado) != 0)
        printf("       obtenido: %s\n       esperado: %s\n", buf, esperado);

    /* camino feliz: file_mprotect en modo audit */
    ev = ev_base();
    ev.hook = MG_HOOK_FILE_MPROTECT;
    ev.verdict = MG_VERDICT_AUDIT;
    ev.prot = 0x5;
    ev.flags = 0x8;
    esperado = "ts=123456789 pid=4242 uid=1000 comm=mg_probe dev=259:3 ino=1048577 hook=file_mprotect prot=0x5 flags=0x8 verdict=audit";
    rc = mg_event_format(&ev, buf, sizeof(buf));
    T_CHECK(rc == (int)strlen(esperado) && strcmp(buf, esperado) == 0,
            "event_format: linea completa file_mprotect audit (rc=%d)", rc);
    if (strcmp(buf, esperado) != 0)
        printf("       obtenido: %s\n       esperado: %s\n", buf, esperado);

    /* dev decodificado: major = dev >> 20, minor = dev & 0xfffff */
    ev = ev_base();
    ev.dev = ((__u64)8 << 20) | 0xfffffu;
    rc = mg_event_format(&ev, buf, sizeof(buf));
    T_CHECK(rc > 0 && strstr(buf, " dev=8:1048575 ") != NULL, "event_format: dev major=8 minor=1048575 decodificado (buf=%s)", buf);

    ev = ev_base();
    ev.dev = ((__u64)259 << 20) | 3u;
    rc = mg_event_format(&ev, buf, sizeof(buf));
    T_CHECK(rc > 0 && strstr(buf, " dev=259:3 ") != NULL, "event_format: dev major=259 minor=3 decodificado");

    /* borde: valores maximos y comm de 15 caracteres */
    ev = ev_base();
    ev.ts_ns = UINT64_MAX;
    ev.pid = 0xffffffffu;
    ev.uid = 0xffffffffu;
    ev.ino = UINT64_MAX;
    memset(ev.comm, 0, sizeof(ev.comm));
    memcpy(ev.comm, "abcdefghijklmno", 15);
    esperado = "ts=18446744073709551615 pid=4294967295 uid=4294967295 comm=abcdefghijklmno dev=259:3 ino=18446744073709551615 hook=mmap_file prot=0x3 flags=0x1 verdict=deny";
    rc = mg_event_format(&ev, buf, sizeof(buf));
    T_CHECK(rc == (int)strlen(esperado) && strcmp(buf, esperado) == 0,
            "event_format borde: valores maximos y comm de 15 caracteres (rc=%d)", rc);
    if (strcmp(buf, esperado) != 0)
        printf("       obtenido: %s\n       esperado: %s\n", buf, esperado);

    /* entrada invalida: hook desconocido se imprime como desconocido */
    ev = ev_base();
    ev.hook = 99;
    rc = mg_event_format(&ev, buf, sizeof(buf));
    T_CHECK(rc > 0 && strstr(buf, " hook=desconocido ") != NULL, "event_format invalida: hook 99 imprime desconocido (buf=%s)", buf);

    /* buffer: ajuste exacto y buffer pequeno */
    ev = ev_base();
    rc = mg_event_format(&ev, buf, sizeof(buf));
    len = (rc > 0) ? (size_t)rc : 0;
    T_CHECK(len > 0 && strlen(buf) == len, "event_format: el valor devuelto es el largo de la cadena (rc=%d)", rc);

    rc = mg_event_format(&ev, buf, len + 1);
    T_CHECK(rc == (int)len, "event_format borde: buffer de largo+1 cabe exacto (rc=%d)", rc);
    rc = mg_event_format(&ev, buf, len);
    T_CHECK(rc == -1, "event_format borde: buffer de largo (sin lugar al terminador) devuelve -1 (rc=%d)", rc);
    rc = mg_event_format(&ev, buf, 10);
    T_CHECK(rc == -1, "event_format invalida: buffer de 10 bytes devuelve -1 (rc=%d)", rc);
    rc = mg_event_format(&ev, buf, 1);
    T_CHECK(rc == -1, "event_format invalida: buffer de 1 byte devuelve -1 (rc=%d)", rc);
}

int main(void)
{
    test_hook_name();
    test_format();
    return mg_t_fails ? 1 : 0;
}
