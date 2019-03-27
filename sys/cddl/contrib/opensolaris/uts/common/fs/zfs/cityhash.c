// Copyright (c) 2011 Google, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

/*
 * Copyright (c) 2017 by Delphix. All rights reserved.
 */

#include <sys/cityhash.h>

#define	HASH_K1 0xb492b66fbe98f273ULL
#define	HASH_K2 0x9ae16a3b2f90404fULL

/*
 * Bitwise right rotate.  Normally this will compile to a single
 * instruction.
 */
static inline uint64_t
rotate(uint64_t val, int shift)
{
	// Avoid shifting by 64: doing so yields an undefined result.
	return (shift == 0 ? val : (val >> shift) | (val << (64 - shift)));
}

static inline uint64_t
cityhash_helper(uint64_t u, uint64_t v, uint64_t mul)
{
	uint64_t a = (u ^ v) * mul;
	a ^= (a >> 47);
	uint64_t b = (v ^ a) * mul;
	b ^= (b >> 47);
	b *= mul;
	return (b);
}

uint64_t
cityhash4(uint64_t w1, uint64_t w2, uint64_t w3, uint64_t w4)
{
	uint64_t mul = HASH_K2 + 64;
	uint64_t a = w1 * HASH_K1;
	uint64_t b = w2;
	uint64_t c = w4 * mul;
	uint64_t d = w3 * HASH_K2;
	return (cityhash_helper(rotate(a + b, 43) + rotate(c, 30) + d,
	    a + rotate(b + HASH_K2, 18) + c, mul));

}
