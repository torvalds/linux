
#ifndef randombytes_nativeclient_H
#define randombytes_nativeclient_H

#ifdef __native_client__

# include "export.h"
# include "randombytes.h"

# ifdef __cplusplus
extern "C" {
# endif

SODIUM_EXPORT
extern struct randombytes_implementation randombytes_nativeclient_implementation;

# ifdef __cplusplus
}
# endif

#endif

#endif
