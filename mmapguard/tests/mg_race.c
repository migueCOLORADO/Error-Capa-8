/* mg_race.c: carrera de mmap, mprotect y munmap concurrentes sobre EL MISMO
 * archivo de prueba. Nada mas: no reproduce ningun exploit.
 *
 * Uso: mg_race <archivo> [--protected]
 *   Variables de entorno: MG_RACE_ITERS (por defecto 2000), MG_RACE_THREADS (por defecto 8).
 *
 * Modo base: el programa reinicia el archivo a NPAGES paginas de ceros. Todos los hilos
 * escriben por el mapeo el MISMO patron por pagina (byte 0xA0 + indice de pagina); el hilo 0
 * reescribe todas las paginas en cada iteracion. Al final el archivo, leido con pread, debe
 * ser exactamente el patron esperado.
 *
 * Modo --protected (archivo con regla deny-write-shared de mmapguard): el contenido previo se
 * guarda; ningun mapeo compartido escribible debe tener exito y al final el archivo debe ser
 * identico al guardado.
 *
 * Comprobaciones: (1) sin cuelgue (alarm de 60 s), (2) todo resultado es exito o errno en
 * {EACCES, EPERM, ENOMEM, EINVAL, EAGAIN}, (3) integridad del contenido. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "mg_t.h"

#define NPAGES_BASE 8
#define NSLOTS      4

static long g_ps;
static int g_npages;
static const char *g_path;
static int g_protected;
static long g_iters = 2000;

static atomic_long g_hist[6]; /* 0 exito, 1 EACCES, 2 EPERM, 3 ENOMEM, 4 EINVAL, 5 EAGAIN */
static atomic_long g_bad;         /* errno fuera del conjunto esperado o apertura fallida */
static atomic_long g_unexpected;  /* exito donde el kernel debia fallar */
static atomic_long g_writable_ok; /* mapeo compartido escribible propio con exito */
static atomic_long g_content_bad; /* byte leido que no es 0 ni el patron */
static atomic_uintptr_t g_slot[NSLOTS];

static unsigned char pat(int page)
{
    return (unsigned char)(0xA0 + page);
}

static uint64_t rnd(uint64_t *s)
{
    uint64_t x = *s;

    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

static void on_alarm(int sig)
{
    static const char msg[] = "[FAIL] carrera: cuelgue, el temporizador de 60 s expiro\n";

    (void)sig;
    if (write(STDOUT_FILENO, msg, sizeof(msg) - 1) < 0)
        _exit(3);
    _exit(3);
}

static void on_fault(int sig)
{
    static const char msg[] = "[FAIL] carrera: senal fatal (SIGSEGV o SIGBUS) en un hilo\n";

    (void)sig;
    if (write(STDOUT_FILENO, msg, sizeof(msg) - 1) < 0)
        _exit(4);
    _exit(4);
}

/* Registra el resultado de una llamada al sistema. */
static void note(int ok, int e, const char *que)
{
    int idx = -1;

    if (ok) {
        atomic_fetch_add(&g_hist[0], 1);
        return;
    }
    switch (e) {
    case EACCES: idx = 1; break;
    case EPERM:  idx = 2; break;
    case ENOMEM: idx = 3; break;
    case EINVAL: idx = 4; break;
    case EAGAIN: idx = 5; break;
    default: break;
    }
    if (idx > 0) {
        atomic_fetch_add(&g_hist[idx], 1);
        return;
    }
    if (atomic_fetch_add(&g_bad, 1) == 0)
        fprintf(stderr, "  info: errno inesperado %d (%s) en %s\n", e, strerror(e), que);
}

static void check_byte(unsigned char b, int page)
{
    if (g_protected)
        return; /* en modo protegido el contenido inicial es el que dejo el script */
    if (b != 0 && b != pat(page))
        atomic_fetch_add(&g_content_bad, 1);
}

static void writer_pass(int fdrw, size_t len)
{
    char *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fdrw, 0);
    int e = errno;
    int pg, rc;

    note(p != MAP_FAILED, e, "mmap escritor");
    if (p == MAP_FAILED)
        return;
    if (g_protected)
        atomic_fetch_add(&g_writable_ok, 1);
    for (pg = 0; pg < g_npages; pg++)
        memset(p + (size_t)pg * (size_t)g_ps, pat(pg), (size_t)g_ps);
    rc = munmap(p, len);
    note(rc == 0, errno, "munmap escritor");
}

static void *worker(void *arg)
{
    int id = (int)(intptr_t)arg;
    uint64_t rng = 0x9E3779B97F4A7C15ull * (uint64_t)(id + 1);
    size_t ps = (size_t)g_ps;
    size_t len = (size_t)g_npages * ps;
    int fdrw = open(g_path, O_RDWR);
    int fdro = open(g_path, O_RDONLY);
    long it;

    if (fdrw < 0 || fdro < 0) {
        atomic_fetch_add(&g_bad, 1);
        fprintf(stderr, "  info: hilo %d no pudo abrir %s (%s)\n", id, g_path, strerror(errno));
        if (fdrw >= 0)
            close(fdrw);
        if (fdro >= 0)
            close(fdro);
        return NULL;
    }

    for (it = 0; it < g_iters; it++) {
        int op, pg, rc, e;
        char *p;

        if (id == 0)
            writer_pass(fdrw, len);

        op = (int)(rnd(&rng) % 6);
        pg = (int)((rnd(&rng) >> 16) % (uint64_t)g_npages);

        switch (op) {
        case 0: /* mapeo compartido escribible de una pagina, escribe el patron */
            p = mmap(NULL, ps, PROT_READ | PROT_WRITE, MAP_SHARED, fdrw, (off_t)pg * (off_t)ps);
            e = errno;
            note(p != MAP_FAILED, e, "mmap compartido escribible");
            if (p != MAP_FAILED) {
                if (g_protected)
                    atomic_fetch_add(&g_writable_ok, 1);
                memset(p, pat(pg), ps);
                rc = munmap(p, ps);
                note(rc == 0, errno, "munmap");
            }
            break;

        case 1: /* mapeo de solo lectura y luego mprotect a escritura */
            p = mmap(NULL, len, PROT_READ, MAP_SHARED, fdrw, 0);
            e = errno;
            note(p != MAP_FAILED, e, "mmap compartido lectura");
            if (p == MAP_FAILED)
                break;
            rc = mprotect(p, len, PROT_READ | PROT_WRITE);
            e = errno;
            note(rc == 0, e, "mprotect a lectura y escritura");
            if (rc == 0) {
                if (g_protected)
                    atomic_fetch_add(&g_writable_ok, 1);
                memset(p + (size_t)pg * ps, pat(pg), ps);
                rc = mprotect(p, len, PROT_READ);
                note(rc == 0, errno, "mprotect a solo lectura");
            }
            check_byte((unsigned char)p[(size_t)pg * ps], pg);
            check_byte((unsigned char)p[(size_t)pg * ps + ps - 1], pg);
            rc = munmap(p, len);
            note(rc == 0, errno, "munmap");
            break;

        case 2: /* mapeo privado: mprotect a escritura siempre es legitimo y no toca el archivo */
            p = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fdro, 0);
            e = errno;
            note(p != MAP_FAILED, e, "mmap privado");
            if (p == MAP_FAILED)
                break;
            rc = mprotect(p, len, PROT_READ | PROT_WRITE);
            e = errno;
            note(rc == 0, e, "mprotect privado");
            if (rc == 0)
                memset(p, 0xEE, ps);
            rc = munmap(p, len);
            note(rc == 0, errno, "munmap");
            break;

        case 3: /* agujero en medio del mapeo y mprotect sobre el rango con hueco */
            if (g_npages < 3)
                break;
            p = mmap(NULL, len, PROT_READ, MAP_SHARED, fdro, 0);
            e = errno;
            note(p != MAP_FAILED, e, "mmap con agujero");
            if (p == MAP_FAILED)
                break;
            rc = munmap(p + ps, ps);
            note(rc == 0, errno, "munmap parcial");
            rc = mprotect(p, len, PROT_READ | PROT_WRITE);
            e = errno;
            note(rc == 0, e, "mprotect sobre rango con agujero");
            if (rc == 0)
                atomic_fetch_add(&g_unexpected, 1); /* fd de solo lectura y hueco: debe fallar */
            rc = munmap(p, ps);
            note(rc == 0, errno, "munmap primer tramo");
            rc = munmap(p + 2 * ps, len - 2 * ps);
            note(rc == 0, errno, "munmap segundo tramo");
            break;

        case 4: /* fd de solo lectura: mprotect de un mapeo compartido a escritura debe fallar */
            p = mmap(NULL, ps, PROT_READ, MAP_SHARED, fdro, 0);
            e = errno;
            note(p != MAP_FAILED, e, "mmap compartido de fd solo lectura");
            if (p == MAP_FAILED)
                break;
            rc = mprotect(p, ps, PROT_READ | PROT_WRITE);
            e = errno;
            note(rc == 0, e, "mprotect de fd solo lectura");
            if (rc == 0)
                atomic_fetch_add(&g_unexpected, 1);
            rc = munmap(p, ps);
            note(rc == 0, errno, "munmap");
            break;

        default: { /* casillas compartidas: reemplazo (mmap + munmap del anterior) y mprotect ajeno */
            int slot = (int)((rnd(&rng) >> 8) % NSLOTS);

            if (rnd(&rng) & 1) {
                void *nuevo = mmap(NULL, ps, PROT_READ, MAP_SHARED, fdrw, 0);

                e = errno;
                note(nuevo != MAP_FAILED, e, "mmap de casilla");
                if (nuevo != MAP_FAILED) {
                    uintptr_t viejo = atomic_exchange(&g_slot[slot], (uintptr_t)nuevo);

                    if (viejo) {
                        rc = munmap((void *)viejo, ps);
                        note(rc == 0, errno, "munmap de casilla");
                    }
                }
            } else {
                uintptr_t ajeno = atomic_load(&g_slot[slot]);

                if (ajeno) {
                    /* solo hacia lectura y escritura: nunca quita permisos a un mapeo ajeno */
                    rc = mprotect((void *)ajeno, ps, PROT_READ | PROT_WRITE);
                    note(rc == 0, errno, "mprotect de casilla");
                }
            }
            break;
        }
        }
    }

    close(fdrw);
    close(fdro);
    return NULL;
}

static double ahora(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    struct stat st;
    unsigned char *esperado = NULL, *actual = NULL;
    size_t len, tam;
    pthread_t *th;
    int nthreads = 8, i, fd;
    const char *env;
    double t0, t1;
    long total_ops, bad, unexp;

    if (argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[2], "--protected") != 0)) {
        fprintf(stderr, "uso: %s <archivo> [--protected]\n", argv[0]);
        return 2;
    }
    g_path = argv[1];
    g_protected = (argc == 3);

    env = getenv("MG_RACE_ITERS");
    if (env && *env) {
        long v = strtol(env, NULL, 10);

        if (v > 0)
            g_iters = v;
    }
    env = getenv("MG_RACE_THREADS");
    if (env && *env) {
        long v = strtol(env, NULL, 10);

        if (v > 0 && v <= 256)
            nthreads = (int)v;
    }

    g_ps = sysconf(_SC_PAGESIZE);

    if (g_protected) {
        if (stat(g_path, &st) != 0 || st.st_size < g_ps) {
            fprintf(stderr, "archivo protegido ausente o menor a una pagina: %s\n", g_path);
            return 2;
        }
        tam = (size_t)st.st_size;
        g_npages = (int)((tam / (size_t)g_ps) < NPAGES_BASE ? (tam / (size_t)g_ps) : NPAGES_BASE);
        esperado = malloc(tam);
        actual = malloc(tam);
        fd = open(g_path, O_RDONLY);
        if (!esperado || !actual || fd < 0 || pread(fd, esperado, tam, 0) != (ssize_t)tam) {
            fprintf(stderr, "no se pudo guardar el contenido previo de %s\n", g_path);
            return 2;
        }
        close(fd);
    } else {
        g_npages = NPAGES_BASE;
        tam = (size_t)g_npages * (size_t)g_ps;
        esperado = malloc(tam);
        actual = malloc(tam);
        if (!esperado || !actual) {
            fprintf(stderr, "sin memoria\n");
            return 2;
        }
        for (i = 0; i < g_npages; i++)
            memset(esperado + (size_t)i * (size_t)g_ps, pat(i), (size_t)g_ps);
        fd = open(g_path, O_RDWR);
        if (fd < 0 || ftruncate(fd, 0) != 0 || ftruncate(fd, (off_t)tam) != 0) {
            fprintf(stderr, "no se pudo reiniciar %s: %s\n", g_path, strerror(errno));
            return 2;
        }
        close(fd);
    }
    len = (size_t)g_npages * (size_t)g_ps;
    (void)len;

    signal(SIGALRM, on_alarm);
    signal(SIGSEGV, on_fault);
    signal(SIGBUS, on_fault);
    alarm(60);

    th = calloc((size_t)nthreads, sizeof(*th));
    if (!th) {
        fprintf(stderr, "sin memoria\n");
        return 2;
    }
    t0 = ahora();
    for (i = 0; i < nthreads; i++) {
        if (pthread_create(&th[i], NULL, worker, (void *)(intptr_t)i) != 0) {
            fprintf(stderr, "pthread_create fallo en el hilo %d\n", i);
            return 2;
        }
    }
    for (i = 0; i < nthreads; i++)
        pthread_join(th[i], NULL);
    t1 = ahora();
    alarm(0);

    /* libera las casillas que queden */
    for (i = 0; i < NSLOTS; i++) {
        uintptr_t s = atomic_exchange(&g_slot[i], 0);

        if (s)
            munmap((void *)s, (size_t)g_ps);
    }

    total_ops = 0;
    for (i = 0; i < 6; i++)
        total_ops += atomic_load(&g_hist[i]);
    printf("  info: %d hilos, %ld iteraciones por hilo, %.2f s; llamadas: exito=%ld EACCES=%ld EPERM=%ld ENOMEM=%ld EINVAL=%ld EAGAIN=%ld\n",
           nthreads, g_iters, t1 - t0, atomic_load(&g_hist[0]), atomic_load(&g_hist[1]), atomic_load(&g_hist[2]),
           atomic_load(&g_hist[3]), atomic_load(&g_hist[4]), atomic_load(&g_hist[5]));

    T_CHECK(1, "carrera%s: sin cuelgue (%d hilos x %ld iteraciones, %ld llamadas en %.2f s)", g_protected ? " con defensa" : "", nthreads,
            g_iters, total_ops, t1 - t0);

    bad = atomic_load(&g_bad);
    unexp = atomic_load(&g_unexpected);
    T_CHECK(bad == 0 && unexp == 0,
            "carrera%s: todos los resultados son exito o errno en {EACCES,EPERM,ENOMEM,EINVAL,EAGAIN} (errno inesperado=%ld, exito inesperado=%ld)",
            g_protected ? " con defensa" : "", bad, unexp);

    fd = open(g_path, O_RDONLY);
    if (fd < 0 || fstat(fd, &st) != 0) {
        T_CHECK(0, "carrera: no se pudo reabrir %s para verificar (%s)", g_path, strerror(errno));
    } else if ((size_t)st.st_size != tam || pread(fd, actual, tam, 0) != (ssize_t)tam) {
        T_CHECK(0, "carrera: el tamano del archivo cambio (esperado %zu, actual %lld)", tam, (long long)st.st_size);
    } else if (g_protected) {
        T_CHECK(memcmp(actual, esperado, tam) == 0 && atomic_load(&g_writable_ok) == 0,
                "carrera con defensa: el contenido del archivo protegido no cambio y ningun mapeo compartido escribible tuvo exito (escribibles=%ld)",
                atomic_load(&g_writable_ok));
    } else {
        T_CHECK(memcmp(actual, esperado, tam) == 0 && atomic_load(&g_content_bad) == 0,
                "carrera: integridad del archivo, coincide con el patron por pagina segun pread (lecturas corruptas=%ld)",
                atomic_load(&g_content_bad));
    }
    if (fd >= 0)
        close(fd);

    free(esperado);
    free(actual);
    free(th);
    return mg_t_fails ? 1 : 0;
}
