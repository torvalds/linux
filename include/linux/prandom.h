/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/prandom.h
 *
 * Include file for the fast pseudo-random 32-bit
 * generation.
 */
#ifndef _LINUX_PRANDOM_H
#define _LINUX_PRANDOM_H

#include <linux/types.h>
#include <linux/percpu.h>

u32 prandom_u32(void);
void prandom_bytes(void *buf, size_t nbytes);
void prandom_seed(u32 seed);
void prandom_reseed_late(void);

DECLARE_PER_CPU(unsigned long, net_rand_noise);

#define PRANDOM_ADD_NOISE(a, b, c, d) \
	prandom_u32_add_noise((unsigned long)(a), (unsigned long)(b), \
			      (unsigned long)(c), (unsigned long)(d))

#if BITS_PER_LONG == 64
/*
 * The core SipHash round function.  Each line can be executed in
 * parallel given enough CPU resources.
 */
#define PRND_SIPROUND(v0, v1, v2, v3) ( \
	v0 += v1, v1 = rol64(v1, 13),  v2 += v3, v3 = rol64(v3, 16), \
	v1 ^= v0, v0 = rol64(v0, 32),  v3 ^= v2,                     \
	v0 += v3, v3 = rol64(v3, 21),  v2 += v1, v1 = rol64(v1, 17), \
	v3 ^= v0,                      v1 ^= v2, v2 = rol64(v2, 32)  \
)

#define PRND_K0 (0x736f6d6570736575 ^ 0x6c7967656e657261)
#define PRND_K1 (0x646f72616e646f6d ^ 0x7465646279746573)

#elif BITS_PER_LONG == 32
/*
 * On 32-bit machines, we use HSipHash, a reduced-width version of SipHash.
 * This is weaker, but 32-bit machines are not used for high-traffic
 * applications, so there is less output for an attacker to analyze.
 */
#define PRND_SIPROUND(v0, v1, v2, v3) ( \
	v0 += v1, v1 = rol32(v1,  5),  v2 += v3, v3 = rol32(v3,  8), \
	v1 ^= v0, v0 = rol32(v0, 16),  v3 ^= v2,                     \
	v0 += v3, v3 = rol32(v3,  7),  v2 += v1, v1 = rol32(v1, 13), \
	v3 ^= v0,                      v1 ^= v2, v2 = rol32(v2, 16)  \
)
#define PRND_K0 0x6c796765
#define PRND_K1 0x74656462

#else
#error Unsupported BITS_PER_LONG
#endif

static inline void prandom_u32_add_noise(unsigned long a, unsigned long b,
					 unsigned long c, unsigned long d)
{
	/*
	 * This is not used cryptographically; it's just
	 * a convenient 4-word hash function. (3 xor, 2 add, 2 rol)
	 */
	a ^= raw_cpu_read(net_rand_noise);
	PRND_SIPROUND(a, b, c, d);
	raw_cpu_write(net_rand_noise, d);
}

struct rnd_state {
	__u32 s1, s2, s3, s4;
};

u32 prandom_u32_state(struct rnd_state *state);
void prandom_bytes_state(struct rnd_state *state, void *buf, size_t nbytes);
void prandom_seed_full_state(struct rnd_state __percpu *pcpu_state);

#define prandom_init_once(pcpu_state)			\
	DO_ONCE(prandom_seed_full_state, (pcpu_state))

/**
 * prandom_u32_max - returns a pseudo-random number in interval [0, ep_ro)
 * @ep_ro: right open interval endpoint
 *
 * Returns a pseudo-random number that is in interval [0, ep_ro). Note
 * that the result depends on PRNG being well distributed in [0, ~0U]
 * u32 space. Here we use maximally equidistributed combined Tausworthe
 * generator, that is, prandom_u32(). This is useful when requesting a
 * random index of an array containing ep_ro elements, for example.
 *
 * Returns: pseudo-random number in interval [0, ep_ro)
 */
static inline u32 prandom_u32_max(u32 ep_ro)
{
	return (u32)(((u64) prandom_u32() * ep_ro) >> 32);
}

/*
 * Handle minimum values for seeds
 */
static inline u32 __seed(u32 x, u32 m)
{
	return (x < m) ? x + m : x;
}

/**
 * prandom_seed_state - set seed for prandom_u32_state().
 * @state: pointer to state structure to receive the seed.
 * @seed: arbitrary 64-bit value to use as a seed.
 */
static inline void prandom_seed_state(struct rnd_state *state, u64 seed)
{
	u32 i = ((seed >> 32) ^ (seed << 10) ^ seed) & 0xffffffffUL;

	state->s1 = __seed(i,   2U);
	state->s2 = __seed(i,   8U);
	state->s3 = __seed(i,  16U);
	state->s4 = __seed(i, 128U);
	PRANDOM_ADD_NOISE(state, i, 0, 0);
}

/* Pseudo random number generator from numerical recipes. */
static inline u32 next_pseudo_random32(u32 seed)
{
	return seed * 1664525 + 1013904223;
}

#endif
