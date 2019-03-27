/*
   This file is basically ref10/fe_frombytes.h.
*/

#include "fe.h"

#ifdef HAVE_AVX_ASM

static uint64_t
load_3(const unsigned char *in)
{
  uint64_t result;
  result = (uint64_t) in[0];
  result |= ((uint64_t) in[1]) << 8;
  result |= ((uint64_t) in[2]) << 16;
  return result;
}

static uint64_t
load_4(const unsigned char *in)
{
  uint64_t result;
  result = (uint64_t) in[0];
  result |= ((uint64_t) in[1]) << 8;
  result |= ((uint64_t) in[2]) << 16;
  result |= ((uint64_t) in[3]) << 24;
  return result;
}

void
fe_frombytes(fe h, const unsigned char *s)
{
  uint64_t h0 = load_4(s);
  uint64_t h1 = load_3(s + 4) << 6;
  uint64_t h2 = load_3(s + 7) << 5;
  uint64_t h3 = load_3(s + 10) << 3;
  uint64_t h4 = load_3(s + 13) << 2;
  uint64_t h5 = load_4(s + 16);
  uint64_t h6 = load_3(s + 20) << 7;
  uint64_t h7 = load_3(s + 23) << 5;
  uint64_t h8 = load_3(s + 26) << 4;
  uint64_t h9 = (load_3(s + 29) & 8388607) << 2;
  uint64_t carry0;
  uint64_t carry1;
  uint64_t carry2;
  uint64_t carry3;
  uint64_t carry4;
  uint64_t carry5;
  uint64_t carry6;
  uint64_t carry7;
  uint64_t carry8;
  uint64_t carry9;

  carry9 = h9 >> 25; h0 += carry9 * 19; h9 &= 0x1FFFFFF;
  carry1 = h1 >> 25; h2 += carry1; h1 &= 0x1FFFFFF;
  carry3 = h3 >> 25; h4 += carry3; h3 &= 0x1FFFFFF;
  carry5 = h5 >> 25; h6 += carry5; h5 &= 0x1FFFFFF;
  carry7 = h7 >> 25; h8 += carry7; h7 &= 0x1FFFFFF;

  carry0 = h0 >> 26; h1 += carry0; h0 &= 0x3FFFFFF;
  carry2 = h2 >> 26; h3 += carry2; h2 &= 0x3FFFFFF;
  carry4 = h4 >> 26; h5 += carry4; h4 &= 0x3FFFFFF;
  carry6 = h6 >> 26; h7 += carry6; h6 &= 0x3FFFFFF;
  carry8 = h8 >> 26; h9 += carry8; h8 &= 0x3FFFFFF;

  h[0] = h0;
  h[1] = h1;
  h[2] = h2;
  h[3] = h3;
  h[4] = h4;
  h[5] = h5;
  h[6] = h6;
  h[7] = h7;
  h[8] = h8;
  h[9] = h9;
}

#endif
