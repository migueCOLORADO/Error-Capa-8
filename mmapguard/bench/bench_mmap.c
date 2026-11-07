/* bench_mmap.c: mide el costo por iteracion de mmap/munmap/mprotect sobre un archivo.
 *
 * Uso: bench_mmap <escenario> <archivo> [iteraciones=1000]
 *
 * Escenarios (siempre 64 KiB del archivo, abierto O_RDWR):
 *   mmap_munmap   mmap MAP_SHARED PROT_READ|PROT_WRITE y munmap, sin tocar paginas
 *   mmap_touch    igual, pero escribe un byte en cada pagina antes de munmap
 *   mmap_mprotect mmap MAP_SHARED PROT_READ, mprotect a PROT_READ|PROT_WRITE, munmap
 *
 * Mide con clock_gettime(CLOCK_MONOTONIC) el tiempo total de las iteraciones y los
 * fallos de pagina menores con getrusage (diferencia de ru_minflt) alrededor del
 * bucle medido. Antes del bucle hace unas iteraciones de calentamiento que no se
 * cuentan. Imprime una sola linea parseable:
 *   RESULT escenario=<e> iter=<n> ns_por_iter=<x> minflt=<y>
 *
 * Codigos de salida: 0 ok, 1 uso incorrecto, 2 error del sistema.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>

#define MAP_LEN ((size_t)64 * 1024)
#define WARMUP_ITERS 16

enum escenario { ESC_MUNMAP, ESC_TOUCH, ESC_MPROTECT };

static long g_page;

/* Una iteracion. Devuelve 0 o -1 con errno fijado. */
static int una_iteracion(enum escenario esc, int fd)
{
    void *p;
    int saved;

    if (esc == ESC_MPROTECT) {
        p = mmap(NULL, MAP_LEN, PROT_READ, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED)
            return -1;
        if (mprotect(p, MAP_LEN, PROT_READ | PROT_WRITE) != 0) {
            saved = errno;
            munmap(p, MAP_LEN);
            errno = saved;
            return -1;
        }
    } else {
        p = mmap(NULL, MAP_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED)
            return -1;
        if (esc == ESC_TOUCH) {
            volatile unsigned char *b = p;
            size_t off;

            for (off = 0; off < MAP_LEN; off += (size_t)g_page)
                b[off] = 1;
        }
    }
    return munmap(p, MAP_LEN);
}

static long long ns_entre(const struct timespec *a, const struct timespec *b)
{
    return (long long)(b->tv_sec - a->tv_sec) * 1000000000LL + (b->tv_nsec - a->tv_nsec);
}

static int uso(void)
{
    fprintf(stderr, "uso: bench_mmap <mmap_munmap|mmap_touch|mmap_mprotect> <archivo> [iteraciones=1000]\n");
    return 1;
}

int main(int argc, char **argv)
{
    enum escenario esc;
    struct stat st;
    struct rusage r0, r1;
    struct timespec t0, t1;
    long iters = 1000;
    int fd, i;
    long n;

    if (argc < 3 || argc > 4)
        return uso();
    if (strcmp(argv[1], "mmap_munmap") == 0)
        esc = ESC_MUNMAP;
    else if (strcmp(argv[1], "mmap_touch") == 0)
        esc = ESC_TOUCH;
    else if (strcmp(argv[1], "mmap_mprotect") == 0)
        esc = ESC_MPROTECT;
    else
        return uso();
    if (argc == 4) {
        char *end = NULL;

        errno = 0;
        iters = strtol(argv[3], &end, 10);
        if (errno != 0 || *end != '\0' || iters <= 0) {
            fprintf(stderr, "bench_mmap: iteraciones debe ser un entero positivo\n");
            return 1;
        }
    }

    g_page = sysconf(_SC_PAGESIZE);
    if (g_page <= 0)
        g_page = 4096;

    fd = open(argv[2], O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "bench_mmap: abrir %s: %s\n", argv[2], strerror(errno));
        return 2;
    }
    if (fstat(fd, &st) != 0 || (size_t)st.st_size < MAP_LEN) {
        fprintf(stderr, "bench_mmap: %s debe tener al menos %zu bytes\n", argv[2], MAP_LEN);
        close(fd);
        return 2;
    }

    for (i = 0; i < WARMUP_ITERS; i++) {
        if (una_iteracion(esc, fd) != 0) {
            fprintf(stderr, "bench_mmap: calentamiento: %s\n", strerror(errno));
            close(fd);
            return 2;
        }
    }

    if (getrusage(RUSAGE_SELF, &r0) != 0 || clock_gettime(CLOCK_MONOTONIC, &t0) != 0) {
        fprintf(stderr, "bench_mmap: getrusage/clock_gettime: %s\n", strerror(errno));
        close(fd);
        return 2;
    }
    for (n = 0; n < iters; n++) {
        if (una_iteracion(esc, fd) != 0) {
            fprintf(stderr, "bench_mmap: iteracion %ld: %s\n", n, strerror(errno));
            close(fd);
            return 2;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    getrusage(RUSAGE_SELF, &r1);
    close(fd);

    printf("RESULT escenario=%s iter=%ld ns_por_iter=%.1f minflt=%ld\n", argv[1], iters,
           (double)ns_entre(&t0, &t1) / (double)iters, r1.ru_minflt - r0.ru_minflt);
    return 0;
}
