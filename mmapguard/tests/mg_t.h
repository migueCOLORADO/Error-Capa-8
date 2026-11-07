/* mg_t.h: ayudante minimo de pruebas. Cada comprobacion imprime una linea
 * "[PASS] ..." o "[FAIL] ..." que run_tests.sh cuenta. */
#ifndef MG_T_H
#define MG_T_H

#include <stdio.h>

static int mg_t_fails;

#define T_CHECK(cond, ...)                      \
    do {                                        \
        if (cond) {                             \
            printf("[PASS] ");                  \
        } else {                                \
            printf("[FAIL] ");                  \
            mg_t_fails++;                       \
        }                                       \
        printf(__VA_ARGS__);                    \
        printf("\n");                           \
        fflush(stdout);                         \
    } while (0)

#endif /* MG_T_H */
