/*
   This file is adapted from ref10/fe.h:
   All the redundant functions are removed.
*/

#ifndef fe_H
#define fe_H

#include <stdint.h>
#include <stdlib.h>

typedef uint64_t fe[10];

/*
fe means field element.
Here the field is \Z/(2^255-19).
An element t, entries t[0]...t[9], represents the integer
t[0]+2^26 t[1]+2^51 t[2]+2^77 t[3]+2^102 t[4]+...+2^230 t[9].
Bounds on each t[i] vary depending on context.
*/

#define fe_frombytes crypto_scalarmult_curve25519_sandy2x_fe_frombytes

extern void fe_frombytes(fe, const unsigned char *);

#endif
