#ifndef crypto_core_salsa208_H
#define crypto_core_salsa208_H

#include <stddef.h>
#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define crypto_core_salsa208_OUTPUTBYTES 64U
SODIUM_EXPORT
size_t crypto_core_salsa208_outputbytes(void)
            __attribute__ ((deprecated));

#define crypto_core_salsa208_INPUTBYTES 16U
SODIUM_EXPORT
size_t crypto_core_salsa208_inputbytes(void)
            __attribute__ ((deprecated));

#define crypto_core_salsa208_KEYBYTES 32U
SODIUM_EXPORT
size_t crypto_core_salsa208_keybytes(void)
            __attribute__ ((deprecated));

#define crypto_core_salsa208_CONSTBYTES 16U
SODIUM_EXPORT
size_t crypto_core_salsa208_constbytes(void)
            __attribute__ ((deprecated));

SODIUM_EXPORT
int crypto_core_salsa208(unsigned char *out, const unsigned char *in,
                         const unsigned char *k, const unsigned char *c);

#ifdef __cplusplus
}
#endif

#endif
