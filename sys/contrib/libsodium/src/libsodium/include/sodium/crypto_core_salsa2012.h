#ifndef crypto_core_salsa2012_H
#define crypto_core_salsa2012_H

#include <stddef.h>
#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define crypto_core_salsa2012_OUTPUTBYTES 64U
SODIUM_EXPORT
size_t crypto_core_salsa2012_outputbytes(void);

#define crypto_core_salsa2012_INPUTBYTES 16U
SODIUM_EXPORT
size_t crypto_core_salsa2012_inputbytes(void);

#define crypto_core_salsa2012_KEYBYTES 32U
SODIUM_EXPORT
size_t crypto_core_salsa2012_keybytes(void);

#define crypto_core_salsa2012_CONSTBYTES 16U
SODIUM_EXPORT
size_t crypto_core_salsa2012_constbytes(void);

SODIUM_EXPORT
int crypto_core_salsa2012(unsigned char *out, const unsigned char *in,
                          const unsigned char *k, const unsigned char *c);

#ifdef __cplusplus
}
#endif

#endif
