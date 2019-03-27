/*
   This file is adapted from ref10/scalarmult.c:
   The code for Mongomery ladder is replace by the ladder assembly function;
   Inversion is done in the same way as amd64-51/.
   (fe is first converted into fe51 after Mongomery ladder)
*/

#include <stddef.h>

#ifdef HAVE_AVX_ASM

#include "utils.h"
#include "curve25519_sandy2x.h"
#include "../scalarmult_curve25519.h"
#include "fe.h"
#include "fe51.h"
#include "ladder.h"
#include "ladder_base.h"

#define x1 var[0]
#define x2 var[1]
#define z2 var[2]

static int
crypto_scalarmult_curve25519_sandy2x(unsigned char *q, const unsigned char *n,
                                     const unsigned char *p)
{
  unsigned char *t = q;
  fe             var[3];
  fe51           x_51;
  fe51           z_51;
  unsigned int   i;

  for (i = 0; i < 32; i++) {
      t[i] = n[i];
  }
  t[0] &= 248;
  t[31] &= 127;
  t[31] |= 64;

  fe_frombytes(x1, p);

  ladder(var, t);

  z_51.v[0] = (z2[1] << 26) + z2[0];
  z_51.v[1] = (z2[3] << 26) + z2[2];
  z_51.v[2] = (z2[5] << 26) + z2[4];
  z_51.v[3] = (z2[7] << 26) + z2[6];
  z_51.v[4] = (z2[9] << 26) + z2[8];

  x_51.v[0] = (x2[1] << 26) + x2[0];
  x_51.v[1] = (x2[3] << 26) + x2[2];
  x_51.v[2] = (x2[5] << 26) + x2[4];
  x_51.v[3] = (x2[7] << 26) + x2[6];
  x_51.v[4] = (x2[9] << 26) + x2[8];

  fe51_invert(&z_51, &z_51);
  fe51_mul(&x_51, &x_51, &z_51);
  fe51_pack(q, &x_51);

  return 0;
}

#undef x2
#undef z2

#define x2 var[0]
#define z2 var[1]

static int
crypto_scalarmult_curve25519_sandy2x_base(unsigned char *q,
                                          const unsigned char *n)
{
  unsigned char *t = q;
  fe             var[3];
  fe51           x_51;
  fe51           z_51;
  unsigned int   i;

  for (i = 0;i < 32; i++) {
      t[i] = n[i];
  }
  t[0] &= 248;
  t[31] &= 127;
  t[31] |= 64;

  ladder_base(var, t);

  z_51.v[0] = (z2[1] << 26) + z2[0];
  z_51.v[1] = (z2[3] << 26) + z2[2];
  z_51.v[2] = (z2[5] << 26) + z2[4];
  z_51.v[3] = (z2[7] << 26) + z2[6];
  z_51.v[4] = (z2[9] << 26) + z2[8];

  x_51.v[0] = (x2[1] << 26) + x2[0];
  x_51.v[1] = (x2[3] << 26) + x2[2];
  x_51.v[2] = (x2[5] << 26) + x2[4];
  x_51.v[3] = (x2[7] << 26) + x2[6];
  x_51.v[4] = (x2[9] << 26) + x2[8];

  fe51_invert(&z_51, &z_51);
  fe51_mul(&x_51, &x_51, &z_51);
  fe51_pack(q, &x_51);

  return 0;
}

struct crypto_scalarmult_curve25519_implementation
crypto_scalarmult_curve25519_sandy2x_implementation = {
    SODIUM_C99(.mult = ) crypto_scalarmult_curve25519_sandy2x,
    SODIUM_C99(.mult_base = ) crypto_scalarmult_curve25519_sandy2x_base
};

#endif
