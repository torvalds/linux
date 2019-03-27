/*-
 * Copyright (c) 2016 (Graeme Jenkinson)
 * All rights reserved.
 *
 * This software was developed by BAE Systems, the University of Cambridge
 * Computer Laboratory, and Memorial University under DARPA/AFRL contract
 * FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent Computing
 * (TC) research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/types.h>

#include "dtrace_xoroshiro128_plus.h"

static __inline uint64_t
rotl(const uint64_t x, int k)
{
	return (x << k) | (x >> (64 - k));
}

/*
 * This is the jump function for the generator. It is equivalent to 2^64 calls
 * to next(); it can be used to generate 2^64 non-overlapping subsequences for
 * parallel computations.
 */
void
dtrace_xoroshiro128_plus_jump(uint64_t * const state,
	uint64_t * const jump_state)
{
	static const uint64_t JUMP[] = { 0xbeac0467eba5facb,
		0xd86b048b86aa9922 };

	uint64_t s0 = 0;
	uint64_t s1 = 0;
	int i = 0;
	int b = 0;
	for (i = 0; i < sizeof JUMP / sizeof *JUMP; i++) {
		for (b = 0; b < 64; b++) {
			if (JUMP[i] & 1ULL << b) {
				s0 ^= state[0];
				s1 ^= state[1];
			}
			dtrace_xoroshiro128_plus_next(state);
		}
	}
	jump_state[0] = s0;
	jump_state[1] = s1;
}

/*
 * xoroshiro128+ - XOR/rotate/shift/rotate
 * xorshift.di.unimi.it
 */
uint64_t
dtrace_xoroshiro128_plus_next(uint64_t * const state)
{
	const uint64_t s0 = state[0];
	uint64_t s1 = state[1];
	uint64_t result;
	result = s0 + s1;

	s1 ^= s0;
	state[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14);
	state[1] = rotl(s1, 36);

	return result;
}
