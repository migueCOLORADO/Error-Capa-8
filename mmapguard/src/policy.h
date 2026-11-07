/* policy.h: banderas y llave (dev, ino) de mmapguard. Sin libbpf. */
#ifndef MMAPGUARD_POLICY_H
#define MMAPGUARD_POLICY_H

#include <stddef.h>
#include <sys/types.h>
#include <linux/types.h>
#include "mmapguard_common.h"

/* Convierte una lista separada por comas (deny-write-shared, deny-exec, audit-only)
 * en bits. Devuelve 0 si es valida (y escribe *out) o -1 si es invalida (cadena
 * nula o vacia, token vacio, token desconocido). Un token repetido es valido. */
int mg_flags_parse(const char *s, __u32 *out);

/* Escribe las banderas como lista separada por comas, "none" si no hay ninguna.
 * Devuelve el largo escrito o -1 si no cabe en n bytes (incluido el terminador). */
int mg_flags_format(__u32 flags, char *buf, size_t n);

/* Codificacion interna del kernel para st_dev: (major << 20) | minor. */
__u64 mg_dev_from_stat(dev_t st_dev);

/* stat(2) sobre la ruta (sigue enlaces simbolicos) y llena la llave (dev, ino).
 * Devuelve 0 o -1 con errno de stat. */
int mg_key_from_path(const char *path, struct mg_key *key);

#endif /* MMAPGUARD_POLICY_H */
