#include <string.h>

#include "private/common.h"
#include "utils.h"

/*
 h = 0
 */

static inline void
fe25519_0(fe25519 h)
{
    memset(&h[0], 0, 5 * sizeof h[0]);
}

/*
 h = 1
 */

static inline void
fe25519_1(fe25519 h)
{
    h[0] = 1;
    memset(&h[1], 0, 4 * sizeof h[0]);
}

/*
 h = f + g
 Can overlap h with f or g.
 */

static inline void
fe25519_add(fe25519 h, const fe25519 f, const fe25519 g)
{
    uint64_t h0 = f[0] + g[0];
    uint64_t h1 = f[1] + g[1];
    uint64_t h2 = f[2] + g[2];
    uint64_t h3 = f[3] + g[3];
    uint64_t h4 = f[4] + g[4];

    h[0] = h0;
    h[1] = h1;
    h[2] = h2;
    h[3] = h3;
    h[4] = h4;
}

/*
 h = f - g
 */

static void
fe25519_sub(fe25519 h, const fe25519 f, const fe25519 g)
{
    const uint64_t mask = 0x7ffffffffffffULL;
    uint64_t h0, h1, h2, h3, h4;

    h0 = g[0];
    h1 = g[1];
    h2 = g[2];
    h3 = g[3];
    h4 = g[4];

    h1 += h0 >> 51;
    h0 &= mask;
    h2 += h1 >> 51;
    h1 &= mask;
    h3 += h2 >> 51;
    h2 &= mask;
    h4 += h3 >> 51;
    h3 &= mask;
    h0 += 19ULL * (h4 >> 51);
    h4 &= mask;

    h0 = (f[0] + 0xfffffffffffdaULL) - h0;
    h1 = (f[1] + 0xffffffffffffeULL) - h1;
    h2 = (f[2] + 0xffffffffffffeULL) - h2;
    h3 = (f[3] + 0xffffffffffffeULL) - h3;
    h4 = (f[4] + 0xffffffffffffeULL) - h4;

    h[0] = h0;
    h[1] = h1;
    h[2] = h2;
    h[3] = h3;
    h[4] = h4;
}

/*
 h = -f
 */

static inline void
fe25519_neg(fe25519 h, const fe25519 f)
{
    fe25519 zero;

    fe25519_0(zero);
    fe25519_sub(h, zero, f);
}

/*
 Replace (f,g) with (g,g) if b == 1;
 replace (f,g) with (f,g) if b == 0.
 *
 Preconditions: b in {0,1}.
 */

static void
fe25519_cmov(fe25519 f, const fe25519 g, unsigned int b)
{
    const uint64_t mask = (uint64_t) (-(int64_t) b);

    uint64_t f0 = f[0];
    uint64_t f1 = f[1];
    uint64_t f2 = f[2];
    uint64_t f3 = f[3];
    uint64_t f4 = f[4];

    uint64_t x0 = f0 ^ g[0];
    uint64_t x1 = f1 ^ g[1];
    uint64_t x2 = f2 ^ g[2];
    uint64_t x3 = f3 ^ g[3];
    uint64_t x4 = f4 ^ g[4];

    x0 &= mask;
    x1 &= mask;
    x2 &= mask;
    x3 &= mask;
    x4 &= mask;

    f[0] = f0 ^ x0;
    f[1] = f1 ^ x1;
    f[2] = f2 ^ x2;
    f[3] = f3 ^ x3;
    f[4] = f4 ^ x4;
}

/*
Replace (f,g) with (g,f) if b == 1;
replace (f,g) with (f,g) if b == 0.

Preconditions: b in {0,1}.
*/

static void
fe25519_cswap(fe25519 f, fe25519 g, unsigned int b)
{
    const uint64_t mask = (uint64_t) (-(int64_t) b);

    uint64_t f0 = f[0];
    uint64_t f1 = f[1];
    uint64_t f2 = f[2];
    uint64_t f3 = f[3];
    uint64_t f4 = f[4];

    uint64_t g0 = g[0];
    uint64_t g1 = g[1];
    uint64_t g2 = g[2];
    uint64_t g3 = g[3];
    uint64_t g4 = g[4];

    uint64_t x0 = f0 ^ g0;
    uint64_t x1 = f1 ^ g1;
    uint64_t x2 = f2 ^ g2;
    uint64_t x3 = f3 ^ g3;
    uint64_t x4 = f4 ^ g4;

    x0 &= mask;
    x1 &= mask;
    x2 &= mask;
    x3 &= mask;
    x4 &= mask;

    f[0] = f0 ^ x0;
    f[1] = f1 ^ x1;
    f[2] = f2 ^ x2;
    f[3] = f3 ^ x3;
    f[4] = f4 ^ x4;

    g[0] = g0 ^ x0;
    g[1] = g1 ^ x1;
    g[2] = g2 ^ x2;
    g[3] = g3 ^ x3;
    g[4] = g4 ^ x4;
}

/*
 h = f
 */

static inline void
fe25519_copy(fe25519 h, const fe25519 f)
{
    uint64_t f0 = f[0];
    uint64_t f1 = f[1];
    uint64_t f2 = f[2];
    uint64_t f3 = f[3];
    uint64_t f4 = f[4];

    h[0] = f0;
    h[1] = f1;
    h[2] = f2;
    h[3] = f3;
    h[4] = f4;
}

/*
 return 1 if f is in {1,3,5,...,q-2}
 return 0 if f is in {0,2,4,...,q-1}
 */

static inline int
fe25519_isnegative(const fe25519 f)
{
    unsigned char s[32];

    fe25519_tobytes(s, f);

    return s[0] & 1;
}

/*
 return 1 if f == 0
 return 0 if f != 0
 */

static inline int
fe25519_iszero(const fe25519 f)
{
    unsigned char s[32];

    fe25519_tobytes(s, f);

    return sodium_is_zero(s, 32);
}

/*
 h = f * g
 Can overlap h with f or g.
 */

static void
fe25519_mul(fe25519 h, const fe25519 f, const fe25519 g)
{
    const uint64_t mask = 0x7ffffffffffffULL;
    uint128_t r0, r1, r2, r3, r4, carry;
    uint64_t  f0, f1, f2, f3, f4;
    uint64_t  f1_19, f2_19, f3_19, f4_19;
    uint64_t  g0, g1, g2, g3, g4;
    uint64_t  r00, r01, r02, r03, r04;

    f0 = f[0];
    f1 = f[1];
    f2 = f[2];
    f3 = f[3];
    f4 = f[4];

    g0 = g[0];
    g1 = g[1];
    g2 = g[2];
    g3 = g[3];
    g4 = g[4];

    f1_19 = 19ULL * f1;
    f2_19 = 19ULL * f2;
    f3_19 = 19ULL * f3;
    f4_19 = 19ULL * f4;

    r0  = ((uint128_t) f0   ) * ((uint128_t) g0);
    r0 += ((uint128_t) f1_19) * ((uint128_t) g4);
    r0 += ((uint128_t) f2_19) * ((uint128_t) g3);
    r0 += ((uint128_t) f3_19) * ((uint128_t) g2);
    r0 += ((uint128_t) f4_19) * ((uint128_t) g1);

    r1  = ((uint128_t) f0   ) * ((uint128_t) g1);
    r1 += ((uint128_t) f1   ) * ((uint128_t) g0);
    r1 += ((uint128_t) f2_19) * ((uint128_t) g4);
    r1 += ((uint128_t) f3_19) * ((uint128_t) g3);
    r1 += ((uint128_t) f4_19) * ((uint128_t) g2);

    r2  = ((uint128_t) f0   ) * ((uint128_t) g2);
    r2 += ((uint128_t) f1   ) * ((uint128_t) g1);
    r2 += ((uint128_t) f2   ) * ((uint128_t) g0);
    r2 += ((uint128_t) f3_19) * ((uint128_t) g4);
    r2 += ((uint128_t) f4_19) * ((uint128_t) g3);

    r3  = ((uint128_t) f0   ) * ((uint128_t) g3);
    r3 += ((uint128_t) f1   ) * ((uint128_t) g2);
    r3 += ((uint128_t) f2   ) * ((uint128_t) g1);
    r3 += ((uint128_t) f3   ) * ((uint128_t) g0);
    r3 += ((uint128_t) f4_19) * ((uint128_t) g4);

    r4  = ((uint128_t) f0   ) * ((uint128_t) g4);
    r4 += ((uint128_t) f1   ) * ((uint128_t) g3);
    r4 += ((uint128_t) f2   ) * ((uint128_t) g2);
    r4 += ((uint128_t) f3   ) * ((uint128_t) g1);
    r4 += ((uint128_t) f4   ) * ((uint128_t) g0);

    r00    = ((uint64_t) r0) & mask;
    carry  = r0 >> 51;
    r1    += carry;
    r01    = ((uint64_t) r1) & mask;
    carry  = r1 >> 51;
    r2    += carry;
    r02    = ((uint64_t) r2) & mask;
    carry  = r2 >> 51;
    r3    += carry;
    r03    = ((uint64_t) r3) & mask;
    carry  = r3 >> 51;
    r4    += carry;
    r04    = ((uint64_t) r4) & mask;
    carry  = r4 >> 51;
    r00   += 19ULL * (uint64_t) carry;
    carry  = r00 >> 51;
    r00   &= mask;
    r01   += (uint64_t) carry;
    carry  = r01 >> 51;
    r01   &= mask;
    r02   += (uint64_t) carry;

    h[0] = r00;
    h[1] = r01;
    h[2] = r02;
    h[3] = r03;
    h[4] = r04;
}

/*
 h = f * f
 Can overlap h with f.
 */

static void
fe25519_sq(fe25519 h, const fe25519 f)
{
    const uint64_t mask = 0x7ffffffffffffULL;
    uint128_t r0, r1, r2, r3, r4, carry;
    uint64_t  f0, f1, f2, f3, f4;
    uint64_t  f0_2, f1_2, f1_38, f2_38, f3_38, f3_19, f4_19;
    uint64_t  r00, r01, r02, r03, r04;

    f0 = f[0];
    f1 = f[1];
    f2 = f[2];
    f3 = f[3];
    f4 = f[4];

    f0_2 = f0 << 1;
    f1_2 = f1 << 1;

    f1_38 = 38ULL * f1;
    f2_38 = 38ULL * f2;
    f3_38 = 38ULL * f3;

    f3_19 = 19ULL * f3;
    f4_19 = 19ULL * f4;

    r0  = ((uint128_t) f0   ) * ((uint128_t) f0);
    r0 += ((uint128_t) f1_38) * ((uint128_t) f4);
    r0 += ((uint128_t) f2_38) * ((uint128_t) f3);

    r1  = ((uint128_t) f0_2 ) * ((uint128_t) f1);
    r1 += ((uint128_t) f2_38) * ((uint128_t) f4);
    r1 += ((uint128_t) f3_19) * ((uint128_t) f3);

    r2  = ((uint128_t) f0_2 ) * ((uint128_t) f2);
    r2 += ((uint128_t) f1   ) * ((uint128_t) f1);
    r2 += ((uint128_t) f3_38) * ((uint128_t) f4);

    r3  = ((uint128_t) f0_2 ) * ((uint128_t) f3);
    r3 += ((uint128_t) f1_2 ) * ((uint128_t) f2);
    r3 += ((uint128_t) f4_19) * ((uint128_t) f4);

    r4  = ((uint128_t) f0_2 ) * ((uint128_t) f4);
    r4 += ((uint128_t) f1_2 ) * ((uint128_t) f3);
    r4 += ((uint128_t) f2   ) * ((uint128_t) f2);

    r00    = ((uint64_t) r0) & mask;
    carry  = r0 >> 51;
    r1    += carry;
    r01    = ((uint64_t) r1) & mask;
    carry  = r1 >> 51;
    r2    += carry;
    r02    = ((uint64_t) r2) & mask;
    carry  = r2 >> 51;
    r3    += carry;
    r03    = ((uint64_t) r3) & mask;
    carry  = r3 >> 51;
    r4    += carry;
    r04    = ((uint64_t) r4) & mask;
    carry  = r4 >> 51;
    r00   += 19ULL * (uint64_t) carry;
    carry  = r00 >> 51;
    r00   &= mask;
    r01   += (uint64_t) carry;
    carry  = r01 >> 51;
    r01   &= mask;
    r02   += (uint64_t) carry;

    h[0] = r00;
    h[1] = r01;
    h[2] = r02;
    h[3] = r03;
    h[4] = r04;
}

/*
 h = 2 * f * f
 Can overlap h with f.
*/

static void
fe25519_sq2(fe25519 h, const fe25519 f)
{
    const uint64_t mask = 0x7ffffffffffffULL;
    uint128_t r0, r1, r2, r3, r4, carry;
    uint64_t  f0, f1, f2, f3, f4;
    uint64_t  f0_2, f1_2, f1_38, f2_38, f3_38, f3_19, f4_19;
    uint64_t  r00, r01, r02, r03, r04;

    f0 = f[0];
    f1 = f[1];
    f2 = f[2];
    f3 = f[3];
    f4 = f[4];

    f0_2 = f0 << 1;
    f1_2 = f1 << 1;

    f1_38 = 38ULL * f1;
    f2_38 = 38ULL * f2;
    f3_38 = 38ULL * f3;

    f3_19 = 19ULL * f3;
    f4_19 = 19ULL * f4;

    r0  = ((uint128_t) f0   ) * ((uint128_t) f0);
    r0 += ((uint128_t) f1_38) * ((uint128_t) f4);
    r0 += ((uint128_t) f2_38) * ((uint128_t) f3);

    r1  = ((uint128_t) f0_2 ) * ((uint128_t) f1);
    r1 += ((uint128_t) f2_38) * ((uint128_t) f4);
    r1 += ((uint128_t) f3_19) * ((uint128_t) f3);

    r2  = ((uint128_t) f0_2 ) * ((uint128_t) f2);
    r2 += ((uint128_t) f1   ) * ((uint128_t) f1);
    r2 += ((uint128_t) f3_38) * ((uint128_t) f4);

    r3  = ((uint128_t) f0_2 ) * ((uint128_t) f3);
    r3 += ((uint128_t) f1_2 ) * ((uint128_t) f2);
    r3 += ((uint128_t) f4_19) * ((uint128_t) f4);

    r4  = ((uint128_t) f0_2 ) * ((uint128_t) f4);
    r4 += ((uint128_t) f1_2 ) * ((uint128_t) f3);
    r4 += ((uint128_t) f2   ) * ((uint128_t) f2);

    r0 <<= 1;
    r1 <<= 1;
    r2 <<= 1;
    r3 <<= 1;
    r4 <<= 1;

    r00    = ((uint64_t) r0) & mask;
    carry  = r0 >> 51;
    r1    += carry;
    r01    = ((uint64_t) r1) & mask;
    carry  = r1 >> 51;
    r2    += carry;
    r02    = ((uint64_t) r2) & mask;
    carry  = r2 >> 51;
    r3    += carry;
    r03    = ((uint64_t) r3) & mask;
    carry  = r3 >> 51;
    r4    += carry;
    r04    = ((uint64_t) r4) & mask;
    carry  = r4 >> 51;
    r00   += 19ULL * (uint64_t) carry;
    carry  = r00 >> 51;
    r00   &= mask;
    r01   += (uint64_t) carry;
    carry  = r01 >> 51;
    r01   &= mask;
    r02   += (uint64_t) carry;

    h[0] = r00;
    h[1] = r01;
    h[2] = r02;
    h[3] = r03;
    h[4] = r04;
}

static void
fe25519_scalar_product(fe25519 h, const fe25519 f, uint32_t n)
{
    const uint64_t mask = 0x7ffffffffffffULL;
    uint128_t a;
    uint128_t sn = (uint128_t) n;
    uint64_t  h0, h1, h2, h3, h4;

    a  = f[0] * sn;
    h0 = ((uint64_t) a) & mask;
    a  = f[1] * sn + ((uint64_t) (a >> 51));
    h1 = ((uint64_t) a) & mask;
    a  = f[2] * sn + ((uint64_t) (a >> 51));
    h2 = ((uint64_t) a) & mask;
    a  = f[3] * sn + ((uint64_t) (a >> 51));
    h3 = ((uint64_t) a) & mask;
    a  = f[4] * sn + ((uint64_t) (a >> 51));
    h4 = ((uint64_t) a) & mask;

    h0 += (a >> 51) * 19ULL;

    h[0] = h0;
    h[1] = h1;
    h[2] = h2;
    h[3] = h3;
    h[4] = h4;
}
