#ifndef crypto_core_hsalsa20_H
#define crypto_core_hsalsa20_H

#include <stddef.h>
#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define crypto_core_hsalsa20_OUTPUTBYTES 32U
SODIUM_EXPORT
size_t crypto_core_hsalsa20_outputbytes(void);

#define crypto_core_hsalsa20_INPUTBYTES 16U
SODIUM_EXPORT
size_t crypto_core_hsalsa20_inputbytes(void);

#define crypto_core_hsalsa20_KEYBYTES 32U
SODIUM_EXPORT
size_t crypto_core_hsalsa20_keybytes(void);

#define crypto_core_hsalsa20_CONSTBYTES 16U
SODIUM_EXPORT
size_t crypto_core_hsalsa20_constbytes(void);

SODIUM_EXPORT
int crypto_core_hsalsa20(unsigned char *out, const unsigned char *in,
                         const unsigned char *k, const unsigned char *c);

#ifdef __cplusplus
}
#endif

#endif
