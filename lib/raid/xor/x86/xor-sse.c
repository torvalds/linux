// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Optimized XOR parity functions for SSE.
 *
 * Cache avoiding checksumming functions utilizing KNI instructions
 * Copyright (C) 1999 Zach Brown (with obvious credit due Ingo)
 *
 * Based on
 * High-speed RAID5 checksumming functions utilizing SSE instructions.
 * Copyright (C) 1998 Ingo Molnar.
 *
 * x86-64 changes / gcc fixes from Andi Kleen.
 * Copyright 2002 Andi Kleen, SuSE Labs.
 */
#include <asm/fpu/api.h>
#include "xor_impl.h"
#include "xor_arch.h"

#ifdef CONFIG_X86_32
/* reduce register pressure */
# define XOR_CONSTANT_CONSTRAINT "i"
#else
# define XOR_CONSTANT_CONSTRAINT "re"
#endif

#define OFFS(x)		"16*("#x")"
#define PF_OFFS(x)	"256+16*("#x")"
#define PF0(x)		"	prefetchnta "PF_OFFS(x)"(%[p1])		;\n"
#define LD(x, y)	"	movaps "OFFS(x)"(%[p1]), %%xmm"#y"	;\n"
#define ST(x, y)	"	movaps %%xmm"#y", "OFFS(x)"(%[p1])	;\n"
#define PF1(x)		"	prefetchnta "PF_OFFS(x)"(%[p2])		;\n"
#define PF2(x)		"	prefetchnta "PF_OFFS(x)"(%[p3])		;\n"
#define PF3(x)		"	prefetchnta "PF_OFFS(x)"(%[p4])		;\n"
#define PF4(x)		"	prefetchnta "PF_OFFS(x)"(%[p5])		;\n"
#define XO1(x, y)	"	xorps "OFFS(x)"(%[p2]), %%xmm"#y"	;\n"
#define XO2(x, y)	"	xorps "OFFS(x)"(%[p3]), %%xmm"#y"	;\n"
#define XO3(x, y)	"	xorps "OFFS(x)"(%[p4]), %%xmm"#y"	;\n"
#define XO4(x, y)	"	xorps "OFFS(x)"(%[p5]), %%xmm"#y"	;\n"
#define NOP(x)

#define BLK64(pf, op, i)				\
		pf(i)					\
		op(i, 0)				\
			op(i + 1, 1)			\
				op(i + 2, 2)		\
					op(i + 3, 3)

static void
xor_sse_2(unsigned long bytes, unsigned long * __restrict p1,
	  const unsigned long * __restrict p2)
{
	unsigned long lines = bytes >> 8;

	asm volatile(
#undef BLOCK
#define BLOCK(i)					\
		LD(i, 0)				\
			LD(i + 1, 1)			\
		PF1(i)					\
				PF1(i + 2)		\
				LD(i + 2, 2)		\
					LD(i + 3, 3)	\
		PF0(i + 4)				\
				PF0(i + 6)		\
		XO1(i, 0)				\
			XO1(i + 1, 1)			\
				XO1(i + 2, 2)		\
					XO1(i + 3, 3)	\
		ST(i, 0)				\
			ST(i + 1, 1)			\
				ST(i + 2, 2)		\
					ST(i + 3, 3)	\


		PF0(0)
				PF0(2)

	" .align 32			;\n"
	" 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

	"       add %[inc], %[p1]       ;\n"
	"       add %[inc], %[p2]       ;\n"
	"       dec %[cnt]              ;\n"
	"       jnz 1b                  ;\n"
	: [cnt] "+r" (lines),
	  [p1] "+r" (p1), [p2] "+r" (p2)
	: [inc] XOR_CONSTANT_CONSTRAINT (256UL)
	: "memory");
}

static void
xor_sse_2_pf64(unsigned long bytes, unsigned long * __restrict p1,
	       const unsigned long * __restrict p2)
{
	unsigned long lines = bytes >> 8;

	asm volatile(
#undef BLOCK
#define BLOCK(i)			\
		BLK64(PF0, LD, i)	\
		BLK64(PF1, XO1, i)	\
		BLK64(NOP, ST, i)	\

	" .align 32			;\n"
	" 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

	"       add %[inc], %[p1]       ;\n"
	"       add %[inc], %[p2]       ;\n"
	"       dec %[cnt]              ;\n"
	"       jnz 1b                  ;\n"
	: [cnt] "+r" (lines),
	  [p1] "+r" (p1), [p2] "+r" (p2)
	: [inc] XOR_CONSTANT_CONSTRAINT (256UL)
	: "memory");
}

static void
xor_sse_3(unsigned long bytes, unsigned long * __restrict p1,
	  const unsigned long * __restrict p2,
	  const unsigned long * __restrict p3)
{
	unsigned long lines = bytes >> 8;

	asm volatile(
#undef BLOCK
#define BLOCK(i) \
		PF1(i)					\
				PF1(i + 2)		\
		LD(i, 0)				\
			LD(i + 1, 1)			\
				LD(i + 2, 2)		\
					LD(i + 3, 3)	\
		PF2(i)					\
				PF2(i + 2)		\
		PF0(i + 4)				\
				PF0(i + 6)		\
		XO1(i, 0)				\
			XO1(i + 1, 1)			\
				XO1(i + 2, 2)		\
					XO1(i + 3, 3)	\
		XO2(i, 0)				\
			XO2(i + 1, 1)			\
				XO2(i + 2, 2)		\
					XO2(i + 3, 3)	\
		ST(i, 0)				\
			ST(i + 1, 1)			\
				ST(i + 2, 2)		\
					ST(i + 3, 3)	\


		PF0(0)
				PF0(2)

	" .align 32			;\n"
	" 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

	"       add %[inc], %[p1]       ;\n"
	"       add %[inc], %[p2]       ;\n"
	"       add %[inc], %[p3]       ;\n"
	"       dec %[cnt]              ;\n"
	"       jnz 1b                  ;\n"
	: [cnt] "+r" (lines),
	  [p1] "+r" (p1), [p2] "+r" (p2), [p3] "+r" (p3)
	: [inc] XOR_CONSTANT_CONSTRAINT (256UL)
	: "memory");
}

static void
xor_sse_3_pf64(unsigned long bytes, unsigned long * __restrict p1,
	       const unsigned long * __restrict p2,
	       const unsigned long * __restrict p3)
{
	unsigned long lines = bytes >> 8;

	asm volatile(
#undef BLOCK
#define BLOCK(i)			\
		BLK64(PF0, LD, i)	\
		BLK64(PF1, XO1, i)	\
		BLK64(PF2, XO2, i)	\
		BLK64(NOP, ST, i)	\

	" .align 32			;\n"
	" 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

	"       add %[inc], %[p1]       ;\n"
	"       add %[inc], %[p2]       ;\n"
	"       add %[inc], %[p3]       ;\n"
	"       dec %[cnt]              ;\n"
	"       jnz 1b                  ;\n"
	: [cnt] "+r" (lines),
	  [p1] "+r" (p1), [p2] "+r" (p2), [p3] "+r" (p3)
	: [inc] XOR_CONSTANT_CONSTRAINT (256UL)
	: "memory");
}

static void
xor_sse_4(unsigned long bytes, unsigned long * __restrict p1,
	  const unsigned long * __restrict p2,
	  const unsigned long * __restrict p3,
	  const unsigned long * __restrict p4)
{
	unsigned long lines = bytes >> 8;

	asm volatile(
#undef BLOCK
#define BLOCK(i) \
		PF1(i)					\
				PF1(i + 2)		\
		LD(i, 0)				\
			LD(i + 1, 1)			\
				LD(i + 2, 2)		\
					LD(i + 3, 3)	\
		PF2(i)					\
				PF2(i + 2)		\
		XO1(i, 0)				\
			XO1(i + 1, 1)			\
				XO1(i + 2, 2)		\
					XO1(i + 3, 3)	\
		PF3(i)					\
				PF3(i + 2)		\
		PF0(i + 4)				\
				PF0(i + 6)		\
		XO2(i, 0)				\
			XO2(i + 1, 1)			\
				XO2(i + 2, 2)		\
					XO2(i + 3, 3)	\
		XO3(i, 0)				\
			XO3(i + 1, 1)			\
				XO3(i + 2, 2)		\
					XO3(i + 3, 3)	\
		ST(i, 0)				\
			ST(i + 1, 1)			\
				ST(i + 2, 2)		\
					ST(i + 3, 3)	\


		PF0(0)
				PF0(2)

	" .align 32			;\n"
	" 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

	"       add %[inc], %[p1]       ;\n"
	"       add %[inc], %[p2]       ;\n"
	"       add %[inc], %[p3]       ;\n"
	"       add %[inc], %[p4]       ;\n"
	"       dec %[cnt]              ;\n"
	"       jnz 1b                  ;\n"
	: [cnt] "+r" (lines), [p1] "+r" (p1),
	  [p2] "+r" (p2), [p3] "+r" (p3), [p4] "+r" (p4)
	: [inc] XOR_CONSTANT_CONSTRAINT (256UL)
	: "memory");
}

static void
xor_sse_4_pf64(unsigned long bytes, unsigned long * __restrict p1,
	       const unsigned long * __restrict p2,
	       const unsigned long * __restrict p3,
	       const unsigned long * __restrict p4)
{
	unsigned long lines = bytes >> 8;

	asm volatile(
#undef BLOCK
#define BLOCK(i)			\
		BLK64(PF0, LD, i)	\
		BLK64(PF1, XO1, i)	\
		BLK64(PF2, XO2, i)	\
		BLK64(PF3, XO3, i)	\
		BLK64(NOP, ST, i)	\

	" .align 32			;\n"
	" 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

	"       add %[inc], %[p1]       ;\n"
	"       add %[inc], %[p2]       ;\n"
	"       add %[inc], %[p3]       ;\n"
	"       add %[inc], %[p4]       ;\n"
	"       dec %[cnt]              ;\n"
	"       jnz 1b                  ;\n"
	: [cnt] "+r" (lines), [p1] "+r" (p1),
	  [p2] "+r" (p2), [p3] "+r" (p3), [p4] "+r" (p4)
	: [inc] XOR_CONSTANT_CONSTRAINT (256UL)
	: "memory");
}

static void
xor_sse_5(unsigned long bytes, unsigned long * __restrict p1,
	  const unsigned long * __restrict p2,
	  const unsigned long * __restrict p3,
	  const unsigned long * __restrict p4,
	  const unsigned long * __restrict p5)
{
	unsigned long lines = bytes >> 8;

	asm volatile(
#undef BLOCK
#define BLOCK(i) \
		PF1(i)					\
				PF1(i + 2)		\
		LD(i, 0)				\
			LD(i + 1, 1)			\
				LD(i + 2, 2)		\
					LD(i + 3, 3)	\
		PF2(i)					\
				PF2(i + 2)		\
		XO1(i, 0)				\
			XO1(i + 1, 1)			\
				XO1(i + 2, 2)		\
					XO1(i + 3, 3)	\
		PF3(i)					\
				PF3(i + 2)		\
		XO2(i, 0)				\
			XO2(i + 1, 1)			\
				XO2(i + 2, 2)		\
					XO2(i + 3, 3)	\
		PF4(i)					\
				PF4(i + 2)		\
		PF0(i + 4)				\
				PF0(i + 6)		\
		XO3(i, 0)				\
			XO3(i + 1, 1)			\
				XO3(i + 2, 2)		\
					XO3(i + 3, 3)	\
		XO4(i, 0)				\
			XO4(i + 1, 1)			\
				XO4(i + 2, 2)		\
					XO4(i + 3, 3)	\
		ST(i, 0)				\
			ST(i + 1, 1)			\
				ST(i + 2, 2)		\
					ST(i + 3, 3)	\


		PF0(0)
				PF0(2)

	" .align 32			;\n"
	" 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

	"       add %[inc], %[p1]       ;\n"
	"       add %[inc], %[p2]       ;\n"
	"       add %[inc], %[p3]       ;\n"
	"       add %[inc], %[p4]       ;\n"
	"       add %[inc], %[p5]       ;\n"
	"       dec %[cnt]              ;\n"
	"       jnz 1b                  ;\n"
	: [cnt] "+r" (lines), [p1] "+r" (p1), [p2] "+r" (p2),
	  [p3] "+r" (p3), [p4] "+r" (p4), [p5] "+r" (p5)
	: [inc] XOR_CONSTANT_CONSTRAINT (256UL)
	: "memory");
}

static void
xor_sse_5_pf64(unsigned long bytes, unsigned long * __restrict p1,
	       const unsigned long * __restrict p2,
	       const unsigned long * __restrict p3,
	       const unsigned long * __restrict p4,
	       const unsigned long * __restrict p5)
{
	unsigned long lines = bytes >> 8;

	asm volatile(
#undef BLOCK
#define BLOCK(i)			\
		BLK64(PF0, LD, i)	\
		BLK64(PF1, XO1, i)	\
		BLK64(PF2, XO2, i)	\
		BLK64(PF3, XO3, i)	\
		BLK64(PF4, XO4, i)	\
		BLK64(NOP, ST, i)	\

	" .align 32			;\n"
	" 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

	"       add %[inc], %[p1]       ;\n"
	"       add %[inc], %[p2]       ;\n"
	"       add %[inc], %[p3]       ;\n"
	"       add %[inc], %[p4]       ;\n"
	"       add %[inc], %[p5]       ;\n"
	"       dec %[cnt]              ;\n"
	"       jnz 1b                  ;\n"
	: [cnt] "+r" (lines), [p1] "+r" (p1), [p2] "+r" (p2),
	  [p3] "+r" (p3), [p4] "+r" (p4), [p5] "+r" (p5)
	: [inc] XOR_CONSTANT_CONSTRAINT (256UL)
	: "memory");
}

DO_XOR_BLOCKS(sse_inner, xor_sse_2, xor_sse_3, xor_sse_4, xor_sse_5);

static void xor_gen_sse(void *dest, void **srcs, unsigned int src_cnt,
			unsigned int bytes)
{
	kernel_fpu_begin();
	xor_gen_sse_inner(dest, srcs, src_cnt, bytes);
	kernel_fpu_end();
}

struct xor_block_template xor_block_sse = {
	.name		= "sse",
	.xor_gen	= xor_gen_sse,
};

DO_XOR_BLOCKS(sse_pf64_inner, xor_sse_2_pf64, xor_sse_3_pf64, xor_sse_4_pf64,
		xor_sse_5_pf64);

static void xor_gen_sse_pf64(void *dest, void **srcs, unsigned int src_cnt,
			unsigned int bytes)
{
	kernel_fpu_begin();
	xor_gen_sse_pf64_inner(dest, srcs, src_cnt, bytes);
	kernel_fpu_end();
}

struct xor_block_template xor_block_sse_pf64 = {
	.name		= "prefetch64-sse",
	.xor_gen	= xor_gen_sse_pf64,
};
