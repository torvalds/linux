/*
 * include/asm-x86_64/xor.h
 *
 * Optimized RAID-5 checksumming functions for MMX and SSE.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


/*
 * Cache avoiding checksumming functions utilizing KNI instructions
 * Copyright (C) 1999 Zach Brown (with obvious credit due Ingo)
 */

/*
 * Based on
 * High-speed RAID5 checksumming functions utilizing SSE instructions.
 * Copyright (C) 1998 Ingo Molnar.
 */

/*
 * x86-64 changes / gcc fixes from Andi Kleen. 
 * Copyright 2002 Andi Kleen, SuSE Labs.
 *
 * This hasn't been optimized for the hammer yet, but there are likely
 * no advantages to be gotten from x86-64 here anyways.
 */

typedef struct { unsigned long a,b; } __attribute__((aligned(16))) xmm_store_t;

/* Doesn't use gcc to save the XMM registers, because there is no easy way to 
   tell it to do a clts before the register saving. */
#define XMMS_SAVE do {				\
	preempt_disable();			\
	asm volatile (				\
		"movq %%cr0,%0		;\n\t"	\
		"clts			;\n\t"	\
		"movups %%xmm0,(%1)	;\n\t"	\
		"movups %%xmm1,0x10(%1)	;\n\t"	\
		"movups %%xmm2,0x20(%1)	;\n\t"	\
		"movups %%xmm3,0x30(%1)	;\n\t"	\
		: "=&r" (cr0)			\
		: "r" (xmm_save) 		\
		: "memory");			\
} while(0)

#define XMMS_RESTORE do {			\
	asm volatile (				\
		"sfence			;\n\t"	\
		"movups (%1),%%xmm0	;\n\t"	\
		"movups 0x10(%1),%%xmm1	;\n\t"	\
		"movups 0x20(%1),%%xmm2	;\n\t"	\
		"movups 0x30(%1),%%xmm3	;\n\t"	\
		"movq 	%0,%%cr0	;\n\t"	\
		:				\
		: "r" (cr0), "r" (xmm_save)	\
		: "memory");			\
	preempt_enable();			\
} while(0)

#define OFFS(x)		"16*("#x")"
#define PF_OFFS(x)	"256+16*("#x")"
#define	PF0(x)		"	prefetchnta "PF_OFFS(x)"(%[p1])		;\n"
#define LD(x,y)		"       movaps   "OFFS(x)"(%[p1]), %%xmm"#y"	;\n"
#define ST(x,y)		"       movaps %%xmm"#y",   "OFFS(x)"(%[p1])	;\n"
#define PF1(x)		"	prefetchnta "PF_OFFS(x)"(%[p2])		;\n"
#define PF2(x)		"	prefetchnta "PF_OFFS(x)"(%[p3])		;\n"
#define PF3(x)		"	prefetchnta "PF_OFFS(x)"(%[p4])		;\n"
#define PF4(x)		"	prefetchnta "PF_OFFS(x)"(%[p5])		;\n"
#define PF5(x)		"	prefetchnta "PF_OFFS(x)"(%[p6])		;\n"
#define XO1(x,y)	"       xorps   "OFFS(x)"(%[p2]), %%xmm"#y"	;\n"
#define XO2(x,y)	"       xorps   "OFFS(x)"(%[p3]), %%xmm"#y"	;\n"
#define XO3(x,y)	"       xorps   "OFFS(x)"(%[p4]), %%xmm"#y"	;\n"
#define XO4(x,y)	"       xorps   "OFFS(x)"(%[p5]), %%xmm"#y"	;\n"
#define XO5(x,y)	"       xorps   "OFFS(x)"(%[p6]), %%xmm"#y"	;\n"


static void
xor_sse_2(unsigned long bytes, unsigned long *p1, unsigned long *p2)
{
        unsigned int lines = bytes >> 8;
	unsigned long cr0;
	xmm_store_t xmm_save[4];

	XMMS_SAVE;

        asm volatile (
#undef BLOCK
#define BLOCK(i) \
		LD(i,0)					\
			LD(i+1,1)			\
		PF1(i)					\
				PF1(i+2)		\
				LD(i+2,2)		\
					LD(i+3,3)	\
		PF0(i+4)				\
				PF0(i+6)		\
		XO1(i,0)				\
			XO1(i+1,1)			\
				XO1(i+2,2)		\
					XO1(i+3,3)	\
		ST(i,0)					\
			ST(i+1,1)			\
				ST(i+2,2)		\
					ST(i+3,3)	\


		PF0(0)
				PF0(2)

	" .align 32			;\n"
        " 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

        "       addq %[inc], %[p1]           ;\n"
        "       addq %[inc], %[p2]           ;\n"
		"		decl %[cnt] ; jnz 1b"
	: [p1] "+r" (p1), [p2] "+r" (p2), [cnt] "+r" (lines)
	: [inc] "r" (256UL) 
        : "memory");

	XMMS_RESTORE;
}

static void
xor_sse_3(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	  unsigned long *p3)
{
	unsigned int lines = bytes >> 8;
	xmm_store_t xmm_save[4];
	unsigned long cr0;

	XMMS_SAVE;

        __asm__ __volatile__ (
#undef BLOCK
#define BLOCK(i) \
		PF1(i)					\
				PF1(i+2)		\
		LD(i,0)					\
			LD(i+1,1)			\
				LD(i+2,2)		\
					LD(i+3,3)	\
		PF2(i)					\
				PF2(i+2)		\
		PF0(i+4)				\
				PF0(i+6)		\
		XO1(i,0)				\
			XO1(i+1,1)			\
				XO1(i+2,2)		\
					XO1(i+3,3)	\
		XO2(i,0)				\
			XO2(i+1,1)			\
				XO2(i+2,2)		\
					XO2(i+3,3)	\
		ST(i,0)					\
			ST(i+1,1)			\
				ST(i+2,2)		\
					ST(i+3,3)	\


		PF0(0)
				PF0(2)

	" .align 32			;\n"
        " 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

        "       addq %[inc], %[p1]           ;\n"
        "       addq %[inc], %[p2]          ;\n"
        "       addq %[inc], %[p3]           ;\n"
		"		decl %[cnt] ; jnz 1b"
	: [cnt] "+r" (lines),
	  [p1] "+r" (p1), [p2] "+r" (p2), [p3] "+r" (p3)
	: [inc] "r" (256UL)
	: "memory"); 
	XMMS_RESTORE;
}

static void
xor_sse_4(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	  unsigned long *p3, unsigned long *p4)
{
	unsigned int lines = bytes >> 8;
	xmm_store_t xmm_save[4]; 
	unsigned long cr0;

	XMMS_SAVE;

        __asm__ __volatile__ (
#undef BLOCK
#define BLOCK(i) \
		PF1(i)					\
				PF1(i+2)		\
		LD(i,0)					\
			LD(i+1,1)			\
				LD(i+2,2)		\
					LD(i+3,3)	\
		PF2(i)					\
				PF2(i+2)		\
		XO1(i,0)				\
			XO1(i+1,1)			\
				XO1(i+2,2)		\
					XO1(i+3,3)	\
		PF3(i)					\
				PF3(i+2)		\
		PF0(i+4)				\
				PF0(i+6)		\
		XO2(i,0)				\
			XO2(i+1,1)			\
				XO2(i+2,2)		\
					XO2(i+3,3)	\
		XO3(i,0)				\
			XO3(i+1,1)			\
				XO3(i+2,2)		\
					XO3(i+3,3)	\
		ST(i,0)					\
			ST(i+1,1)			\
				ST(i+2,2)		\
					ST(i+3,3)	\


		PF0(0)
				PF0(2)

	" .align 32			;\n"
        " 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

        "       addq %[inc], %[p1]           ;\n"
        "       addq %[inc], %[p2]           ;\n"
        "       addq %[inc], %[p3]           ;\n"
        "       addq %[inc], %[p4]           ;\n"
	"	decl %[cnt] ; jnz 1b"
	: [cnt] "+c" (lines),
	  [p1] "+r" (p1), [p2] "+r" (p2), [p3] "+r" (p3), [p4] "+r" (p4)
	: [inc] "r" (256UL)
        : "memory" );

	XMMS_RESTORE;
}

static void
xor_sse_5(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	  unsigned long *p3, unsigned long *p4, unsigned long *p5)
{
        unsigned int lines = bytes >> 8;
	xmm_store_t xmm_save[4];
	unsigned long cr0;

	XMMS_SAVE;

        __asm__ __volatile__ (
#undef BLOCK
#define BLOCK(i) \
		PF1(i)					\
				PF1(i+2)		\
		LD(i,0)					\
			LD(i+1,1)			\
				LD(i+2,2)		\
					LD(i+3,3)	\
		PF2(i)					\
				PF2(i+2)		\
		XO1(i,0)				\
			XO1(i+1,1)			\
				XO1(i+2,2)		\
					XO1(i+3,3)	\
		PF3(i)					\
				PF3(i+2)		\
		XO2(i,0)				\
			XO2(i+1,1)			\
				XO2(i+2,2)		\
					XO2(i+3,3)	\
		PF4(i)					\
				PF4(i+2)		\
		PF0(i+4)				\
				PF0(i+6)		\
		XO3(i,0)				\
			XO3(i+1,1)			\
				XO3(i+2,2)		\
					XO3(i+3,3)	\
		XO4(i,0)				\
			XO4(i+1,1)			\
				XO4(i+2,2)		\
					XO4(i+3,3)	\
		ST(i,0)					\
			ST(i+1,1)			\
				ST(i+2,2)		\
					ST(i+3,3)	\


		PF0(0)
				PF0(2)

	" .align 32			;\n"
        " 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

        "       addq %[inc], %[p1]           ;\n"
        "       addq %[inc], %[p2]           ;\n"
        "       addq %[inc], %[p3]           ;\n"
        "       addq %[inc], %[p4]           ;\n"
        "       addq %[inc], %[p5]           ;\n"
	"	decl %[cnt] ; jnz 1b"
	: [cnt] "+c" (lines),
  	  [p1] "+r" (p1), [p2] "+r" (p2), [p3] "+r" (p3), [p4] "+r" (p4), 
	  [p5] "+r" (p5)
	: [inc] "r" (256UL)
	: "memory");

	XMMS_RESTORE;
}

static struct xor_block_template xor_block_sse = {
        .name = "generic_sse",
        .do_2 = xor_sse_2,
        .do_3 = xor_sse_3,
        .do_4 = xor_sse_4,
        .do_5 = xor_sse_5,
};

#undef XOR_TRY_TEMPLATES
#define XOR_TRY_TEMPLATES				\
	do {						\
		xor_speed(&xor_block_sse);	\
	} while (0)

/* We force the use of the SSE xor block because it can write around L2.
   We may also be able to load into the L1 only depending on how the cpu
   deals with a load to a line that is being prefetched.  */
#define XOR_SELECT_TEMPLATE(FASTEST) (&xor_block_sse)
