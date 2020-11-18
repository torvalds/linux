/*
 * xxHash - Extremely Fast Hash algorithm
 * Copyright (C) 2012-2016, Yann Collet.
 *
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation. This program is dual-licensed; you may select
 * either version 2 of the GNU General Public License ("GPL") or BSD license
 * ("BSD").
 *
 * You can contact the author at:
 * - xxHash homepage: https://cyan4973.github.io/xxHash/
 * - xxHash source repository: https://github.com/Cyan4973/xxHash
 */

/*
 * Notice extracted from xxHash homepage:
 *
 * xxHash is an extremely fast Hash algorithm, running at RAM speed limits.
 * It also successfully passes all tests from the SMHasher suite.
 *
 * Comparison (single thread, Windows Seven 32 bits, using SMHasher on a Core 2
 * Duo @3GHz)
 *
 * Name            Speed       Q.Score   Author
 * xxHash          5.4 GB/s     10
 * CrapWow         3.2 GB/s      2       Andrew
 * MumurHash 3a    2.7 GB/s     10       Austin Appleby
 * SpookyHash      2.0 GB/s     10       Bob Jenkins
 * SBox            1.4 GB/s      9       Bret Mulvey
 * Lookup3         1.2 GB/s      9       Bob Jenkins
 * SuperFastHash   1.2 GB/s      1       Paul Hsieh
 * CityHash64      1.05 GB/s    10       Pike & Alakuijala
 * FNV             0.55 GB/s     5       Fowler, Noll, Vo
 * CRC32           0.43 GB/s     9
 * MD5-32          0.33 GB/s    10       Ronald L. Rivest
 * SHA1-32         0.28 GB/s    10
 *
 * Q.Score is a measure of quality of the hash function.
 * It depends on successfully passing SMHasher test set.
 * 10 is a perfect score.
 *
 * A 64-bits version, named xxh64 offers much better speed,
 * but for 64-bits applications only.
 * Name     Speed on 64 bits    Speed on 32 bits
 * xxh64       13.8 GB/s            1.9 GB/s
 * xxh32        6.8 GB/s            6.0 GB/s
 */

#ifndef XXHASH_H
#define XXHASH_H

#include <linux/types.h>

/*-****************************
 * Simple Hash Functions
 *****************************/

/**
 * xxh32() - calculate the 32-bit hash of the input with a given seed.
 *
 * @input:  The data to hash.
 * @length: The length of the data to hash.
 * @seed:   The seed can be used to alter the result predictably.
 *
 * Speed on Core 2 Duo @ 3 GHz (single thread, SMHasher benchmark) : 5.4 GB/s
 *
 * Return:  The 32-bit hash of the data.
 */
uint32_t xxh32(const void *input, size_t length, uint32_t seed);

/**
 * xxh64() - calculate the 64-bit hash of the input with a given seed.
 *
 * @input:  The data to hash.
 * @length: The length of the data to hash.
 * @seed:   The seed can be used to alter the result predictably.
 *
 * This function runs 2x faster on 64-bit systems, but slower on 32-bit systems.
 *
 * Return:  The 64-bit hash of the data.
 */
uint64_t xxh64(const void *input, size_t length, uint64_t seed);

/**
 * xxhash() - calculate wordsize hash of the input with a given seed
 * @input:  The data to hash.
 * @length: The length of the data to hash.
 * @seed:   The seed can be used to alter the result predictably.
 *
 * If the hash does not need to be comparable between machines with
 * different word sizes, this function will call whichever of xxh32()
 * or xxh64() is faster.
 *
 * Return:  wordsize hash of the data.
 */

static inline unsigned long xxhash(const void *input, size_t length,
				   uint64_t seed)
{
#if BITS_PER_LONG == 64
       return xxh64(input, length, seed);
#else
       return xxh32(input, length, seed);
#endif
}

/*-****************************
 * Streaming Hash Functions
 *****************************/

/*
 * These definitions are only meant to allow allocation of XXH state
 * statically, on stack, or in a struct for example.
 * Do not use members directly.
 */

/**
 * struct xxh32_state - private xxh32 state, do not use members directly
 */
struct xxh32_state {
	uint32_t total_len_32;
	uint32_t large_len;
	uint32_t v1;
	uint32_t v2;
	uint32_t v3;
	uint32_t v4;
	uint32_t mem32[4];
	uint32_t memsize;
};

/**
 * struct xxh32_state - private xxh64 state, do not use members directly
 */
struct xxh64_state {
	uint64_t total_len;
	uint64_t v1;
	uint64_t v2;
	uint64_t v3;
	uint64_t v4;
	uint64_t mem64[4];
	uint32_t memsize;
};

/**
 * xxh32_reset() - reset the xxh32 state to start a new hashing operation
 *
 * @state: The xxh32 state to reset.
 * @seed:  Initialize the hash state with this seed.
 *
 * Call this function on any xxh32_state to prepare for a new hashing operation.
 */
void xxh32_reset(struct xxh32_state *state, uint32_t seed);

/**
 * xxh32_update() - hash the data given and update the xxh32 state
 *
 * @state:  The xxh32 state to update.
 * @input:  The data to hash.
 * @length: The length of the data to hash.
 *
 * After calling xxh32_reset() call xxh32_update() as many times as necessary.
 *
 * Return:  Zero on success, otherwise an error code.
 */
int xxh32_update(struct xxh32_state *state, const void *input, size_t length);

/**
 * xxh32_digest() - produce the current xxh32 hash
 *
 * @state: Produce the current xxh32 hash of this state.
 *
 * A hash value can be produced at any time. It is still possible to continue
 * inserting input into the hash state after a call to xxh32_digest(), and
 * generate new hashes later on, by calling xxh32_digest() again.
 *
 * Return: The xxh32 hash stored in the state.
 */
uint32_t xxh32_digest(const struct xxh32_state *state);

/**
 * xxh64_reset() - reset the xxh64 state to start a new hashing operation
 *
 * @state: The xxh64 state to reset.
 * @seed:  Initialize the hash state with this seed.
 */
void xxh64_reset(struct xxh64_state *state, uint64_t seed);

/**
 * xxh64_update() - hash the data given and update the xxh64 state
 * @state:  The xxh64 state to update.
 * @input:  The data to hash.
 * @length: The length of the data to hash.
 *
 * After calling xxh64_reset() call xxh64_update() as many times as necessary.
 *
 * Return:  Zero on success, otherwise an error code.
 */
int xxh64_update(struct xxh64_state *state, const void *input, size_t length);

/**
 * xxh64_digest() - produce the current xxh64 hash
 *
 * @state: Produce the current xxh64 hash of this state.
 *
 * A hash value can be produced at any time. It is still possible to continue
 * inserting input into the hash state after a call to xxh64_digest(), and
 * generate new hashes later on, by calling xxh64_digest() again.
 *
 * Return: The xxh64 hash stored in the state.
 */
uint64_t xxh64_digest(const struct xxh64_state *state);

/*-**************************
 * Utils
 ***************************/

/**
 * xxh32_copy_state() - copy the source state into the destination state
 *
 * @src: The source xxh32 state.
 * @dst: The destination xxh32 state.
 */
void xxh32_copy_state(struct xxh32_state *dst, const struct xxh32_state *src);

/**
 * xxh64_copy_state() - copy the source state into the destination state
 *
 * @src: The source xxh64 state.
 * @dst: The destination xxh64 state.
 */
void xxh64_copy_state(struct xxh64_state *dst, const struct xxh64_state *src);

#endif /* XXHASH_H */
