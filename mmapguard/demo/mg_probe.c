/* mg_probe.c: sonda de intercepcion de mmap/mprotect para la demostracion de mmapguard.
 *
 * Uso: mg_probe <modo> <archivo>
 *   shared-write    abre O_RDWR y hace mmap MAP_SHARED con PROT_READ|PROT_WRITE
 *   private-write   mmap MAP_PRIVATE con PROT_READ|PROT_WRITE
 *   exec            mmap con PROT_READ|PROT_EXEC
 *   mprotect-write  mmap MAP_SHARED PROT_READ y luego mprotect agregando PROT_WRITE
 *
 * Salida: una linea "PROBE modo=<m> resultado=PERMITIDO" o
 *         "PROBE modo=<m> resultado=DENEGADO errno=<n> (<strerror>)".
 * Codigos de salida: 0 permitido, 10 denegado (EPERM o EACCES), 11 otro error, 1 uso.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#define EXIT_PERMITIDO 0
#define EXIT_USO       1
#define EXIT_DENEGADO  10
#define EXIT_OTRO      11

static int exit_para_errno(int err)
{
    return (err == EPERM || err == EACCES) ? EXIT_DENEGADO : EXIT_OTRO;
}

static int reportar_denegado(const char *modo, int err)
{
    printf("PROBE modo=%s resultado=DENEGADO errno=%d (%s)\n", modo, err, strerror(err));
    return exit_para_errno(err);
}

/* Prueba la escritura real: byte en el mapeo, msync y relectura desde el archivo. */
static void evidenciar_escritura(int fd, char *mapa)
{
    char leido = 0;
    ssize_t n;

    mapa[0] = 'X';
    if (msync(mapa, 1, MS_SYNC) != 0) {
        printf("NOTA shared-write: se escribio un byte en el mapeo pero msync fallo: errno=%d (%s)\n",
               errno, strerror(errno));
        return;
    }
    n = pread(fd, &leido, 1, 0);
    if (n == 1 && leido == 'X')
        printf("NOTA shared-write: byte 'X' escrito en el mapeo, msync ok y releido del archivo: la escritura llego al archivo\n");
    else
        printf("NOTA shared-write: byte escrito en el mapeo y msync ok, pero no se pudo confirmar en el archivo (pread=%zd)\n",
               n);
}

int main(int argc, char **argv)
{
    const char *modo, *ruta;
    long pagina;
    size_t len;
    int fd, oflags, prot, mflags, rc, err;
    char *mapa;

    if (argc != 3) {
        fprintf(stderr, "uso: %s <shared-write|private-write|exec|mprotect-write> <archivo>\n", argv[0]);
        return EXIT_USO;
    }
    modo = argv[1];
    ruta = argv[2];

    if (strcmp(modo, "shared-write") == 0) {
        oflags = O_RDWR;
        prot = PROT_READ | PROT_WRITE;
        mflags = MAP_SHARED;
    } else if (strcmp(modo, "private-write") == 0) {
        oflags = O_RDONLY;
        prot = PROT_READ | PROT_WRITE;
        mflags = MAP_PRIVATE;
    } else if (strcmp(modo, "exec") == 0) {
        oflags = O_RDONLY;
        prot = PROT_READ | PROT_EXEC;
        mflags = MAP_PRIVATE;
    } else if (strcmp(modo, "mprotect-write") == 0) {
        /* O_RDWR para que el kernel permita luego agregar PROT_WRITE a un mapeo compartido */
        oflags = O_RDWR;
        prot = PROT_READ;
        mflags = MAP_SHARED;
    } else {
        fprintf(stderr, "modo desconocido: %s\n", modo);
        fprintf(stderr, "uso: %s <shared-write|private-write|exec|mprotect-write> <archivo>\n", argv[0]);
        return EXIT_USO;
    }

    pagina = sysconf(_SC_PAGESIZE);
    len = (pagina > 0) ? (size_t)pagina : 4096u;

    fd = open(ruta, oflags);
    if (fd < 0) {
        err = errno;
        printf("PROBE modo=%s resultado=ERROR_OPEN errno=%d (%s) archivo=%s\n", modo, err, strerror(err), ruta);
        return EXIT_OTRO;
    }

    mapa = mmap(NULL, len, prot, mflags, fd, 0);
    if (mapa == MAP_FAILED) {
        err = errno;
        rc = reportar_denegado(modo, err);
        close(fd);
        return rc;
    }

    rc = EXIT_PERMITIDO;
    if (strcmp(modo, "mprotect-write") == 0) {
        if (mprotect(mapa, len, PROT_READ | PROT_WRITE) != 0) {
            err = errno;
            rc = reportar_denegado(modo, err);
            munmap(mapa, len);
            close(fd);
            return rc;
        }
    }

    printf("PROBE modo=%s resultado=PERMITIDO\n", modo);
    if (strcmp(modo, "shared-write") == 0)
        evidenciar_escritura(fd, mapa);

    munmap(mapa, len);
    close(fd);
    return rc;
}
