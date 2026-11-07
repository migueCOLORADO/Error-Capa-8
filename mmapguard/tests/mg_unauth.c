/* mg_unauth.c: intentos no autorizados de mapeo sobre un archivo de prueba.
 *
 * Uso: mg_unauth <archivo> [--defended]
 *   El archivo debe existir y medir al menos una pagina (lo crea run_tests.sh).
 *   Nunca escribe en el archivo: solo pide mapeos y cambios de proteccion.
 *
 * Modo base (sin defensa cargada):
 *   (a) fd O_RDONLY + mmap MAP_SHARED PROT_WRITE   -> EACCES (permisos POSIX)
 *   (b) fd O_RDONLY + mmap PROT_READ compartido y luego mprotect(PROT_WRITE) -> EACCES
 *   (c) control: fd O_RDWR + mmap MAP_SHARED PROT_WRITE -> PERMITIDO en la linea base
 *       (justo lo que mmapguard deniega cuando hay regla deny-write-shared)
 *
 * Modo --defended (con mmapguard cargado y regla deny-write-shared sobre el archivo):
 *   (c) fd O_RDWR + mmap MAP_SHARED PROT_WRITE -> EPERM
 *   (d) fd O_RDWR + mmap PROT_READ compartido y luego mprotect(PROT_WRITE) -> EPERM
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mg_t.h"

static void caso_a(const char *path, size_t len)
{
    int fd = open(path, O_RDONLY);
    void *p;
    int e;

    if (fd < 0) {
        T_CHECK(0, "(a) no se pudo abrir %s en O_RDONLY (%s)", path, strerror(errno));
        return;
    }
    p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    e = errno;
    T_CHECK(p == MAP_FAILED && e == EACCES, "(a) O_RDONLY + mmap MAP_SHARED PROT_WRITE falla con EACCES (errno=%d %s)", p == MAP_FAILED ? e : 0,
            p == MAP_FAILED ? strerror(e) : "mmap tuvo exito");
    if (p != MAP_FAILED)
        munmap(p, len);
    close(fd);
}

static void caso_b(const char *path, size_t len)
{
    int fd = open(path, O_RDONLY);
    void *p;
    int rc, e;

    if (fd < 0) {
        T_CHECK(0, "(b) no se pudo abrir %s en O_RDONLY (%s)", path, strerror(errno));
        return;
    }
    p = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        T_CHECK(0, "(b) mmap PROT_READ MAP_SHARED debia funcionar (%s)", strerror(errno));
        close(fd);
        return;
    }
    rc = mprotect(p, len, PROT_READ | PROT_WRITE);
    e = errno;
    T_CHECK(rc == -1 && e == EACCES, "(b) mprotect(PROT_WRITE) sobre mapeo compartido de fd O_RDONLY falla con EACCES (rc=%d errno=%d %s)", rc,
            rc == -1 ? e : 0, rc == -1 ? strerror(e) : "mprotect tuvo exito");
    munmap(p, len);
    close(fd);
}

static void caso_c_base(const char *path, size_t len)
{
    int fd = open(path, O_RDWR);
    void *p;
    int e;

    if (fd < 0) {
        T_CHECK(0, "(c) no se pudo abrir %s en O_RDWR (%s)", path, strerror(errno));
        return;
    }
    p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    e = errno;
    T_CHECK(p != MAP_FAILED, "(c) control: O_RDWR + mmap MAP_SHARED PROT_WRITE es PERMITIDO en la linea base (%s)",
            p != MAP_FAILED ? "permitido" : strerror(e));
    if (p != MAP_FAILED)
        munmap(p, len);
    close(fd);
}

static void caso_c_def(const char *path, size_t len)
{
    int fd = open(path, O_RDWR);
    void *p;
    int e;

    if (fd < 0) {
        T_CHECK(0, "(c) no se pudo abrir %s en O_RDWR (%s)", path, strerror(errno));
        return;
    }
    p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    e = errno;
    T_CHECK(p == MAP_FAILED && e == EPERM, "(c) con defensa: O_RDWR + mmap MAP_SHARED PROT_WRITE falla con EPERM (errno=%d %s)",
            p == MAP_FAILED ? e : 0, p == MAP_FAILED ? strerror(e) : "mmap tuvo exito");
    if (p != MAP_FAILED)
        munmap(p, len);
    close(fd);
}

static void caso_d_def(const char *path, size_t len)
{
    int fd = open(path, O_RDWR);
    void *p;
    int rc, e;

    if (fd < 0) {
        T_CHECK(0, "(d) no se pudo abrir %s en O_RDWR (%s)", path, strerror(errno));
        return;
    }
    p = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        T_CHECK(0, "(d) mmap PROT_READ MAP_SHARED debia funcionar con la regla deny-write-shared (%s)", strerror(errno));
        close(fd);
        return;
    }
    rc = mprotect(p, len, PROT_READ | PROT_WRITE);
    e = errno;
    T_CHECK(rc == -1 && e == EPERM, "(d) con defensa: mprotect(PROT_WRITE) sobre mapeo compartido falla con EPERM (rc=%d errno=%d %s)", rc,
            rc == -1 ? e : 0, rc == -1 ? strerror(e) : "mprotect tuvo exito");
    munmap(p, len);
    close(fd);
}

int main(int argc, char **argv)
{
    struct stat st;
    size_t len;
    int defended = 0;

    if (argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[2], "--defended") != 0)) {
        fprintf(stderr, "uso: %s <archivo> [--defended]\n", argv[0]);
        return 2;
    }
    defended = (argc == 3);

    if (stat(argv[1], &st) != 0) {
        fprintf(stderr, "no se puede leer %s: %s\n", argv[1], strerror(errno));
        return 2;
    }
    len = (size_t)sysconf(_SC_PAGESIZE);
    if ((size_t)st.st_size < len) {
        fprintf(stderr, "%s debe medir al menos %zu bytes\n", argv[1], len);
        return 2;
    }

    if (defended) {
        caso_c_def(argv[1], len);
        caso_d_def(argv[1], len);
    } else {
        caso_a(argv[1], len);
        caso_b(argv[1], len);
        caso_c_base(argv[1], len);
    }
    return mg_t_fails ? 1 : 0;
}
