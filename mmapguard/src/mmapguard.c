/* mmapguard.c: CLI y cargador libbpf de mmapguard.
 *
 *   mmapguard load [--count-faults]
 *   mmapguard unload
 *   mmapguard policy add <ruta> <banderas>
 *   mmapguard policy del <ruta>
 *   mmapguard policy list
 *   mmapguard events [--count N] [--timeout MS]
 *
 * Codigos de salida: 0 ok, 1 uso incorrecto, 2 error del kernel o de permisos.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "mmapguard_common.h"
#include "policy.h"
#include "events.h"
#include "mmapguard.skel.h"

#define EXIT_OK    0
#define EXIT_USAGE 1
#define EXIT_KERN  2

#ifndef BPF_FS_MAGIC
#define BPF_FS_MAGIC 0xcafe4a11
#endif

#define BPFFS_DIR "/sys/fs/bpf"

#define PIN_POLICY MG_PIN_DIR "/" MG_MAP_POLICY
#define PIN_EVENTS MG_PIN_DIR "/" MG_MAP_EVENTS
#define PIN_STATS  MG_PIN_DIR "/" MG_MAP_STATS

static const char *const all_pins[] = {
    MG_PIN_LNK_MMAP, MG_PIN_LNK_MPRO, MG_PIN_LNK_FLT,
    PIN_POLICY, PIN_EVENTS, PIN_STATS,
};
#define N_PINS (sizeof(all_pins) / sizeof(all_pins[0]))

static volatile sig_atomic_t g_stop;

/* ---------- utilidades ---------- */

static void usage(FILE *f)
{
    fprintf(f,
            "uso: mmapguard load [--count-faults]\n"
            "     mmapguard unload\n"
            "     mmapguard policy add <ruta> <banderas>\n"
            "     mmapguard policy del <ruta>\n"
            "     mmapguard policy list\n"
            "     mmapguard events [--count N] [--timeout MS]\n"
            "banderas: lista separada por comas de deny-write-shared, deny-exec, audit-only\n");
}

static int fail_usage(const char *msg)
{
    fprintf(stderr, "mmapguard: %s (ver 'mmapguard --help')\n", msg);
    return EXIT_USAGE;
}

/* Error de kernel o permisos en una linea, con la accion manual si aplica. */
static int fail_errno(const char *what, int err)
{
    const char *hint = "";

    if (err < 0)
        err = -err;
    if (err == EPERM || err == EACCES)
        hint = "; ejecutar con sudo";
    fprintf(stderr, "mmapguard: %s: %s%s\n", what, strerror(err), hint);
    return EXIT_KERN;
}

static int libbpf_print(enum libbpf_print_level level, const char *fmt, va_list ap)
{
    (void)level;
    /* El registro de libbpf es multilinea: solo se muestra con MMAPGUARD_DEBUG. */
    if (!getenv("MMAPGUARD_DEBUG"))
        return 0;
    return vfprintf(stderr, fmt, ap);
}

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static long long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int parse_nonneg(const char *s, long *out)
{
    char *end = NULL;
    long v;

    if (!s || *s == '\0')
        return -1;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || *end != '\0' || v < 0)
        return -1;
    *out = v;
    return 0;
}

/* Abre un mapa pineado. Devuelve fd >= 0 o un codigo de salida ya impreso en *rc. */
static int open_pinned(const char *path, int *rc)
{
    int fd = bpf_obj_get(path);

    if (fd < 0) {
        int err = -fd;

        if (err == ENOENT) {
            fprintf(stderr,
                    "mmapguard: no existe el pin %s; mmapguard no esta cargado "
                    "(ejecutar 'sudo mmapguard load')\n", path);
            *rc = EXIT_KERN;
        } else {
            *rc = fail_errno(path, err);
        }
        return -1;
    }
    return fd;
}

/* ---------- bpffs y pines ---------- */

static int ensure_bpffs(void)
{
    struct statfs sfs;

    if (statfs(BPFFS_DIR, &sfs) == 0 && (unsigned long)sfs.f_type == (unsigned long)BPF_FS_MAGIC)
        return 0;
    if (mount("bpf", BPFFS_DIR, "bpf", 0, NULL) < 0) {
        int err = errno;

        fprintf(stderr,
                "mmapguard: %s no es bpffs y mount fallo: %s; montar bpffs con "
                "'sudo mount -t bpf bpf %s' o ejecutar con sudo\n",
                BPFFS_DIR, strerror(err), BPFFS_DIR);
        return -1;
    }
    return 0;
}

/* Borra los pines conocidos. Devuelve cuantos existian. */
static int remove_pins(void)
{
    int removed = 0;
    size_t i;

    for (i = 0; i < N_PINS; i++) {
        if (unlink(all_pins[i]) == 0)
            removed++;
    }
    rmdir(MG_PIN_DIR);
    return removed;
}

/* ---------- load / unload ---------- */

static int cmd_load(int argc, char **argv)
{
    int count_faults = 0;
    struct mmapguard *skel = NULL;
    struct bpf_link *lnk_mmap = NULL, *lnk_mpro = NULL, *lnk_flt = NULL;
    int rc = EXIT_KERN;
    int err;
    int i;

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--count-faults") == 0)
            count_faults = 1;
        else
            return fail_usage("opcion desconocida para 'load'");
    }

    libbpf_set_print(libbpf_print);

    if (ensure_bpffs() < 0)
        return EXIT_KERN;

    if (mkdir(MG_PIN_DIR, 0700) < 0) {
        if (errno != EEXIST)
            return fail_errno("crear " MG_PIN_DIR, errno);
        if (access(PIN_POLICY, F_OK) == 0) {
            fprintf(stderr,
                    "mmapguard: ya esta cargado (existe %s); ejecutar 'sudo mmapguard unload' antes\n",
                    PIN_POLICY);
            return EXIT_KERN;
        }
    }

    skel = mmapguard__open();
    if (!skel) {
        rc = fail_errno("abrir el objeto BPF", errno);
        goto out;
    }

    /* El fentry opcional queda apagado salvo --count-faults. */
    if (bpf_program__set_autoload(skel->progs.mg_count_fault, count_faults ? true : false) < 0) {
        rc = fail_errno("configurar autoload de mg_count_fault", EINVAL);
        goto out;
    }

    err = mmapguard__load(skel);
    if (err) {
        char what[96];

        snprintf(what, sizeof(what), "cargar programas BPF%s (MMAPGUARD_DEBUG=1 muestra el registro de libbpf)",
                 count_faults ? " con --count-faults" : "");
        rc = fail_errno(what, err);
        goto out;
    }

    lnk_mmap = bpf_program__attach(skel->progs.mg_mmap_file);
    if (!lnk_mmap) {
        rc = fail_errno("enganchar lsm/mmap_file (verificar que 'bpf' figure en /sys/kernel/security/lsm)", errno);
        goto out;
    }
    lnk_mpro = bpf_program__attach(skel->progs.mg_file_mprotect);
    if (!lnk_mpro) {
        rc = fail_errno("enganchar lsm/file_mprotect (verificar que 'bpf' figure en /sys/kernel/security/lsm)", errno);
        goto out;
    }
    if (count_faults) {
        lnk_flt = bpf_program__attach(skel->progs.mg_count_fault);
        if (!lnk_flt) {
            rc = fail_errno("enganchar fentry/handle_mm_fault", errno);
            goto out;
        }
    }

    err = bpf_map__pin(skel->maps.mg_policy, PIN_POLICY);
    if (err) {
        rc = fail_errno("pinear " PIN_POLICY, err);
        goto out;
    }
    err = bpf_map__pin(skel->maps.mg_events, PIN_EVENTS);
    if (err) {
        rc = fail_errno("pinear " PIN_EVENTS, err);
        goto out;
    }
    err = bpf_map__pin(skel->maps.mg_stats, PIN_STATS);
    if (err) {
        rc = fail_errno("pinear " PIN_STATS, err);
        goto out;
    }
    err = bpf_link__pin(lnk_mmap, MG_PIN_LNK_MMAP);
    if (err) {
        rc = fail_errno("pinear " MG_PIN_LNK_MMAP, err);
        goto out;
    }
    err = bpf_link__pin(lnk_mpro, MG_PIN_LNK_MPRO);
    if (err) {
        rc = fail_errno("pinear " MG_PIN_LNK_MPRO, err);
        goto out;
    }
    if (lnk_flt) {
        err = bpf_link__pin(lnk_flt, MG_PIN_LNK_FLT);
        if (err) {
            rc = fail_errno("pinear " MG_PIN_LNK_FLT, err);
            goto out;
        }
    }

    printf("mmapguard cargado: pines en %s%s\n", MG_PIN_DIR,
           lnk_flt ? " (con conteo de fallos de pagina)" : "");
    rc = EXIT_OK;

out:
    if (rc != EXIT_OK)
        remove_pins();
    /* Los pines mantienen vivos programas, enlaces y mapas; se suelta lo local. */
    if (lnk_mmap) {
        bpf_link__disconnect(lnk_mmap);
        bpf_link__destroy(lnk_mmap);
    }
    if (lnk_mpro) {
        bpf_link__disconnect(lnk_mpro);
        bpf_link__destroy(lnk_mpro);
    }
    if (lnk_flt) {
        bpf_link__disconnect(lnk_flt);
        bpf_link__destroy(lnk_flt);
    }
    if (skel)
        mmapguard__destroy(skel);
    return rc;
}

static int cmd_unload(int argc, char **argv)
{
    (void)argv;
    if (argc != 0)
        return fail_usage("'unload' no admite argumentos");

    if (access(MG_PIN_DIR, F_OK) < 0) {
        if (errno == ENOENT)
            fprintf(stderr, "mmapguard: no hay nada que descargar (no existe %s)\n", MG_PIN_DIR);
        else
            return fail_errno("acceder a " MG_PIN_DIR, errno);
        return EXIT_KERN;
    }
    if (remove_pins() == 0 && access(MG_PIN_DIR, F_OK) == 0) {
        /* El directorio existe pero no se pudo vaciar ni borrar. */
        int err = errno ? errno : EPERM;

        return fail_errno("borrar pines en " MG_PIN_DIR, err);
    }
    printf("mmapguard descargado: pines borrados de %s\n", MG_PIN_DIR);
    return EXIT_OK;
}

/* ---------- policy ---------- */

static int policy_add(const char *path, const char *flagstr)
{
    struct mg_key key;
    struct mg_rule rule = {0};
    __u32 flags;
    int fd, rc = EXIT_OK, err;

    if (mg_flags_parse(flagstr, &flags) < 0) {
        fprintf(stderr,
                "mmapguard: banderas invalidas '%s' (validas: deny-write-shared, deny-exec, audit-only, separadas por comas)\n",
                flagstr);
        return EXIT_USAGE;
    }
    if (mg_key_from_path(path, &key) < 0)
        return fail_errno(path, errno);

    fd = open_pinned(PIN_POLICY, &rc);
    if (fd < 0)
        return rc;

    rule.flags = flags;
    err = bpf_map_update_elem(fd, &key, &rule, BPF_ANY);
    if (err)
        rc = fail_errno("insertar la regla", err);
    close(fd);
    return rc;
}

static int policy_del(const char *path)
{
    struct mg_key key;
    int fd, rc = EXIT_OK, err;

    if (mg_key_from_path(path, &key) < 0)
        return fail_errno(path, errno);

    fd = open_pinned(PIN_POLICY, &rc);
    if (fd < 0)
        return rc;

    err = bpf_map_delete_elem(fd, &key);
    if (err == -ENOENT) {
        fprintf(stderr, "mmapguard: no hay regla para %s\n", path);
        rc = EXIT_KERN;
    } else if (err) {
        rc = fail_errno("borrar la regla", err);
    }
    close(fd);
    return rc;
}

static int policy_list(void)
{
    struct mg_key cur, nxt;
    int have = 0, fd, rc = EXIT_OK;

    fd = open_pinned(PIN_POLICY, &rc);
    if (fd < 0)
        return rc;

    for (;;) {
        struct mg_rule rule;
        char fl[64];
        int e = bpf_map_get_next_key(fd, have ? &cur : NULL, &nxt);

        if (e == -ENOENT)
            break;
        if (e) {
            rc = fail_errno("recorrer la politica", e);
            break;
        }
        have = 1;
        cur = nxt;
        if (bpf_map_lookup_elem(fd, &nxt, &rule) != 0)
            continue; /* la regla se borro entre la iteracion y la lectura */
        if (mg_flags_format(rule.flags, fl, sizeof(fl)) < 0)
            snprintf(fl, sizeof(fl), "0x%x", (unsigned)rule.flags);
        printf("dev=%u:%u ino=%llu flags=%s\n",
               (unsigned)(nxt.dev >> 20), (unsigned)(nxt.dev & 0xfffffu),
               (unsigned long long)nxt.ino, fl);
    }
    close(fd);
    return rc;
}

static int cmd_policy(int argc, char **argv)
{
    libbpf_set_print(libbpf_print);

    if (argc < 1)
        return fail_usage("falta la accion de 'policy' (add, del, list)");
    if (strcmp(argv[0], "add") == 0) {
        if (argc != 3)
            return fail_usage("uso: policy add <ruta> <banderas>");
        return policy_add(argv[1], argv[2]);
    }
    if (strcmp(argv[0], "del") == 0) {
        if (argc != 2)
            return fail_usage("uso: policy del <ruta>");
        return policy_del(argv[1]);
    }
    if (strcmp(argv[0], "list") == 0) {
        if (argc != 1)
            return fail_usage("uso: policy list");
        return policy_list();
    }
    return fail_usage("accion desconocida para 'policy'");
}

/* ---------- events ---------- */

struct ev_ctx {
    long limit;   /* 0 = sin limite */
    long seen;
};

static int on_event(void *ctx, void *data, size_t sz)
{
    struct ev_ctx *c = ctx;
    char line[256];

    if (sz < sizeof(struct mg_event))
        return 0;
    if (c->limit > 0 && c->seen >= c->limit)
        return 0;
    if (mg_event_format((const struct mg_event *)data, line, sizeof(line)) < 0)
        return 0;
    puts(line);
    fflush(stdout);
    c->seen++;
    if (c->limit > 0 && c->seen >= c->limit)
        g_stop = 1;
    return 0;
}

static int cmd_events(int argc, char **argv)
{
    struct ev_ctx ctx = { 0, 0 };
    long timeout_ms = -1;
    struct ring_buffer *rb;
    struct sigaction sa;
    long long deadline = 0;
    int fd, rc = EXIT_OK;
    int i;

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            if (parse_nonneg(argv[++i], &ctx.limit) < 0)
                return fail_usage("--count espera un entero no negativo");
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            if (parse_nonneg(argv[++i], &timeout_ms) < 0)
                return fail_usage("--timeout espera milisegundos como entero no negativo");
        } else {
            return fail_usage("opcion desconocida para 'events'");
        }
    }

    libbpf_set_print(libbpf_print);

    fd = open_pinned(PIN_EVENTS, &rc);
    if (fd < 0)
        return rc;

    rb = ring_buffer__new(fd, on_event, &ctx, NULL);
    if (!rb) {
        rc = fail_errno("abrir el ringbuf " PIN_EVENTS, errno);
        close(fd);
        return rc;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (timeout_ms >= 0)
        deadline = now_ms() + timeout_ms;

    while (!g_stop) {
        long step = 500;
        int n;

        if (timeout_ms >= 0) {
            long long left = deadline - now_ms();

            if (left < 0)
                left = 0;
            if (left < step)
                step = (long)left;
        }
        n = ring_buffer__poll(rb, (int)step);
        if (n < 0 && n != -EINTR) {
            rc = fail_errno("leer el ringbuf", n);
            break;
        }
        if (timeout_ms >= 0 && now_ms() >= deadline)
            break;
    }

    ring_buffer__free(rb);
    close(fd);
    return rc;
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr);
        return EXIT_USAGE;
    }
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "help") == 0) {
        usage(stdout);
        return EXIT_OK;
    }
    if (strcmp(argv[1], "load") == 0)
        return cmd_load(argc - 2, argv + 2);
    if (strcmp(argv[1], "unload") == 0)
        return cmd_unload(argc - 2, argv + 2);
    if (strcmp(argv[1], "policy") == 0)
        return cmd_policy(argc - 2, argv + 2);
    if (strcmp(argv[1], "events") == 0)
        return cmd_events(argc - 2, argv + 2);

    usage(stderr);
    fprintf(stderr, "mmapguard: comando desconocido '%s'\n", argv[1]);
    return EXIT_USAGE;
}
