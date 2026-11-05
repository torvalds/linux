// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AVX-512 optimized implementation of xor_gen()
 *
 * Copyright 2026 Google LLC
 */

#include <linux/types.h>
#include <asm/fpu/api.h>
#include "xor_impl.h"
#include "xor_arch.h"

/*
 * Implementation notes:
 *
 * Unrolling by the number of buffers (2-5) is very important.
 *
 * Unrolling by length is less important, especially when using register-indexed
 * addressing with negative indices from the end of the buffers.  That approach
 * results in just two loop control instructions being needed per iteration,
 * regardless of the number of buffers.
 *
 * In fact, benchmarks showed that the 2 and 3 buffer cases require only 2x
 * unrolling by length, while the 4 and 5 buffer cases don't require any
 * unrolling by length.  Benchmarks also showed that the register-indexed
 * addressing isn't a bottleneck either; i.e., we can't do any better by
 * incrementing the pointers as we go along, even with more unrolling.
 */

static void xor_avx512_2(long bytes, u8 *p1, const u8 *p2)
{
	long i = -bytes;

	asm volatile("1: vmovdqa64 (%1,%0), %%zmm0\n"
		     "vmovdqa64 64(%1,%0), %%zmm1\n"
		     "vpxorq (%2,%0), %%zmm0, %%zmm0\n"
		     "vpxorq 64(%2,%0), %%zmm1, %%zmm1\n"
		     "vmovdqa64 %%zmm0, (%1,%0)\n"
		     "vmovdqa64 %%zmm1, 64(%1,%0)\n"
		     "add $128, %0\n"
		     "jnz 1b\n"
		     : "+&r"(i)
		     : "r"(p1 + bytes), "r"(p2 + bytes)
		     : "memory", "cc");
}

static void xor_avx512_3(long bytes, u8 *p1, const u8 *p2, const u8 *p3)
{
	long i = -bytes;

	asm volatile("1: vmovdqa64 (%1,%0), %%zmm0\n"
		     "vmovdqa64 64(%1,%0), %%zmm1\n"
		     "vmovdqa64 (%2,%0), %%zmm2\n"
		     "vmovdqa64 64(%2,%0), %%zmm3\n"
		     "vpternlogq $0x96, (%3,%0), %%zmm2, %%zmm0\n"
		     "vpternlogq $0x96, 64(%3,%0), %%zmm3, %%zmm1\n"
		     "vmovdqa64 %%zmm0, (%1,%0)\n"
		     "vmovdqa64 %%zmm1, 64(%1,%0)\n"
		     "add $128, %0\n"
		     "jnz 1b\n"
		     : "+&r"(i)
		     : "r"(p1 + bytes), "r"(p2 + bytes), "r"(p3 + bytes)
		     : "memory", "cc");
}

static void xor_avx512_4(long bytes, u8 *p1, const u8 *p2, const u8 *p3,
			 const u8 *p4)
{
	long i = -bytes;

	asm volatile("1: vmovdqa64 (%1,%0), %%zmm0\n"
		     "vmovdqa64 (%2,%0), %%zmm1\n"
		     "vpxorq (%3,%0), %%zmm0, %%zmm0\n"
		     "vpternlogq $0x96, (%4,%0), %%zmm1, %%zmm0\n"
		     "vmovdqa64 %%zmm0, (%1,%0)\n"
		     "add $64, %0\n"
		     "jnz 1b\n"
		     : "+&r"(i)
		     : "r"(p1 + bytes), "r"(p2 + bytes), "r"(p3 + bytes),
		       "r"(p4 + bytes)
		     : "memory", "cc");
}

static void xor_avx512_5(long bytes, u8 *p1, const u8 *p2, const u8 *p3,
			 const u8 *p4, const u8 *p5)
{
	long i = -bytes;

	asm volatile("1: vmovdqa64 (%1,%0), %%zmm0\n"
		     "vmovdqa64 (%2,%0), %%zmm1\n"
		     "vpternlogq $0x96, (%3,%0), %%zmm1, %%zmm0\n"
		     "vmovdqa64 (%4,%0), %%zmm1\n"
		     "vpternlogq $0x96, (%5,%0), %%zmm1, %%zmm0\n"
		     "vmovdqa64 %%zmm0, (%1,%0)\n"
		     "add $64, %0\n"
		     "jnz 1b\n"
		     : "+&r"(i)
		     : "r"(p1 + bytes), "r"(p2 + bytes), "r"(p3 + bytes),
		       "r"(p4 + bytes), "r"(p5 + bytes)
		     : "memory", "cc");
}

DO_XOR_BLOCKS(avx512_inner, xor_avx512_2, xor_avx512_3, xor_avx512_4,
	      xor_avx512_5);

/*
 * Preconditions: bytes is a nonzero multiple of 512, and all buffers are
 * 64-byte aligned.
 */
static void xor_gen_avx512(void *dest, void **srcs, unsigned int src_cnt,
			   unsigned int bytes)
{
	kernel_fpu_begin();
	xor_gen_avx512_inner(dest, srcs, src_cnt, bytes);
	asm volatile("vzeroupper");
	kernel_fpu_end();
}

struct xor_block_template xor_block_avx512 = {
	.name = "avx512",
	.xor_gen = xor_gen_avx512,
};
