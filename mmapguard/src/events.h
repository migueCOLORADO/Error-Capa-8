/* events.h: formato de texto de los eventos de mmapguard. Sin libbpf. */
#ifndef MMAPGUARD_EVENTS_H
#define MMAPGUARD_EVENTS_H

#include <stddef.h>
#include <linux/types.h>
#include "mmapguard_common.h"

/* "mmap_file", "file_mprotect" o "desconocido". */
const char *mg_hook_name(__u32 hook);

/* Escribe una linea (sin salto de linea final):
 *   ts=<ns> pid=<n> uid=<n> comm=<s> dev=<major>:<minor> ino=<n> hook=<nombre>
 *   prot=0x<hex> flags=0x<hex> verdict=<deny|audit>
 * dev se decodifica: major = dev >> 20, minor = dev & 0xfffff.
 * Devuelve los caracteres escritos o -1 si no cabe en n bytes (incluido el terminador). */
int mg_event_format(const struct mg_event *ev, char *buf, size_t n);

#endif /* MMAPGUARD_EVENTS_H */
