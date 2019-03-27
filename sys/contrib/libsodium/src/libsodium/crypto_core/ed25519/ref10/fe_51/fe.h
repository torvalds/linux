/*
 Ignores top bit of h.
 */

void
fe25519_frombytes(fe25519 h, const unsigned char *s)
{
    const uint64_t mask = 0x7ffffffffffffULL;
    uint64_t h0, h1, h2, h3, h4;

    h0 = (LOAD64_LE(s     )      ) & mask;
    h1 = (LOAD64_LE(s +  6) >>  3) & mask;
    h2 = (LOAD64_LE(s + 12) >>  6) & mask;
    h3 = (LOAD64_LE(s + 19) >>  1) & mask;
    h4 = (LOAD64_LE(s + 24) >> 12) & mask;

    h[0] = h0;
    h[1] = h1;
    h[2] = h2;
    h[3] = h3;
    h[4] = h4;
}

static void
fe25519_reduce(fe25519 h, const fe25519 f)
{
    const uint64_t mask = 0x7ffffffffffffULL;
    uint128_t t[5];

    t[0] = f[0];
    t[1] = f[1];
    t[2] = f[2];
    t[3] = f[3];
    t[4] = f[4];

    t[1] += t[0] >> 51;
    t[0] &= mask;
    t[2] += t[1] >> 51;
    t[1] &= mask;
    t[3] += t[2] >> 51;
    t[2] &= mask;
    t[4] += t[3] >> 51;
    t[3] &= mask;
    t[0] += 19 * (t[4] >> 51);
    t[4] &= mask;

    t[1] += t[0] >> 51;
    t[0] &= mask;
    t[2] += t[1] >> 51;
    t[1] &= mask;
    t[3] += t[2] >> 51;
    t[2] &= mask;
    t[4] += t[3] >> 51;
    t[3] &= mask;
    t[0] += 19 * (t[4] >> 51);
    t[4] &= mask;

    /* now t is between 0 and 2^255-1, properly carried. */
    /* case 1: between 0 and 2^255-20. case 2: between 2^255-19 and 2^255-1. */

    t[0] += 19ULL;

    t[1] += t[0] >> 51;
    t[0] &= mask;
    t[2] += t[1] >> 51;
    t[1] &= mask;
    t[3] += t[2] >> 51;
    t[2] &= mask;
    t[4] += t[3] >> 51;
    t[3] &= mask;
    t[0] += 19ULL * (t[4] >> 51);
    t[4] &= mask;

    /* now between 19 and 2^255-1 in both cases, and offset by 19. */

    t[0] += 0x8000000000000 - 19ULL;
    t[1] += 0x8000000000000 - 1ULL;
    t[2] += 0x8000000000000 - 1ULL;
    t[3] += 0x8000000000000 - 1ULL;
    t[4] += 0x8000000000000 - 1ULL;

    /* now between 2^255 and 2^256-20, and offset by 2^255. */

    t[1] += t[0] >> 51;
    t[0] &= mask;
    t[2] += t[1] >> 51;
    t[1] &= mask;
    t[3] += t[2] >> 51;
    t[2] &= mask;
    t[4] += t[3] >> 51;
    t[3] &= mask;
    t[4] &= mask;

    h[0] = t[0];
    h[1] = t[1];
    h[2] = t[2];
    h[3] = t[3];
    h[4] = t[4];
}

void
fe25519_tobytes(unsigned char *s, const fe25519 h)
{
    fe25519  t;
    uint64_t t0, t1, t2, t3;

    fe25519_reduce(t, h);
    t0 = t[0] | (t[1] << 51);
    t1 = (t[1] >> 13) | (t[2] << 38);
    t2 = (t[2] >> 26) | (t[3] << 25);
    t3 = (t[3] >> 39) | (t[4] << 12);
    STORE64_LE(s +  0, t0);
    STORE64_LE(s +  8, t1);
    STORE64_LE(s + 16, t2);
    STORE64_LE(s + 24, t3);
}
