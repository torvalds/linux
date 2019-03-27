/*
   This file is adapted from amd64-51/fe25519.h:
   'fe25519' is renamed as 'fe51';
   All the redundant functions are removed;
   New function fe51_nsquare is introduced.
*/

#ifndef fe51_H
#define fe51_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>

#include "fe51_namespace.h"

typedef struct
{
    uint64_t v[5];
}
fe51;

extern void fe51_pack(unsigned char *, const fe51 *);
extern void fe51_mul(fe51 *, const fe51 *, const fe51 *);
extern void fe51_nsquare(fe51 *, const fe51 *, int);
extern void fe51_invert(fe51 *, const fe51 *);

#ifdef __cplusplus
}
#endif

#endif
