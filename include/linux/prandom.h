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
#include <linux/random.h>

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
 * Returns a pseudo-random number that is in interval [0, ep_ro). This is
 * useful when requesting a random index of an array containing ep_ro elements,
 * for example. The result is somewhat biased when ep_ro is not a power of 2,
 * so do not use this for cryptographic purposes.
 *
 * Returns: pseudo-random number in interval [0, ep_ro)
 */
static inline u32 prandom_u32_max(u32 ep_ro)
{
	if (__builtin_constant_p(ep_ro <= 1U << 8) && ep_ro <= 1U << 8)
		return (get_random_u8() * ep_ro) >> 8;
	if (__builtin_constant_p(ep_ro <= 1U << 16) && ep_ro <= 1U << 16)
		return (get_random_u16() * ep_ro) >> 16;
	return ((u64)get_random_u32() * ep_ro) >> 32;
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
}

/* Pseudo random number generator from numerical recipes. */
static inline u32 next_pseudo_random32(u32 seed)
{
	return seed * 1664525 + 1013904223;
}

#endif
