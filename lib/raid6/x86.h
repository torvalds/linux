/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2002-2004 H. Peter Anvin - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

/*
 * raid6/x86.h
 *
 * Definitions common to x86 and x86-64 RAID-6 code only
 */

#ifndef LINUX_RAID_RAID6X86_H
#define LINUX_RAID_RAID6X86_H

#if (defined(__i386__) || defined(__x86_64__)) && !defined(__arch_um__)

#ifdef __KERNEL__ /* Real code */

#include <asm/fpu/api.h>

#else /* Dummy code for user space testing */

static inline void kernel_fpu_begin(void)
{
}

static inline void kernel_fpu_end(void)
{
}

#define __aligned(x) __attribute__((aligned(x)))

#define X86_FEATURE_MMX		(0*32+23) /* Multimedia Extensions */
#define X86_FEATURE_FXSR	(0*32+24) /* FXSAVE and FXRSTOR instructions
					   * (fast save and restore) */
#define X86_FEATURE_XMM		(0*32+25) /* Streaming SIMD Extensions */
#define X86_FEATURE_XMM2	(0*32+26) /* Streaming SIMD Extensions-2 */
#define X86_FEATURE_XMM3	(4*32+ 0) /* "pni" SSE-3 */
#define X86_FEATURE_SSSE3	(4*32+ 9) /* Supplemental SSE-3 */
#define X86_FEATURE_AVX	(4*32+28) /* Advanced Vector Extensions */
#define X86_FEATURE_AVX2        (9*32+ 5) /* AVX2 instructions */
#define X86_FEATURE_AVX512F     (9*32+16) /* AVX-512 Foundation */
#define X86_FEATURE_AVX512DQ    (9*32+17) /* AVX-512 DQ (Double/Quad granular)
					   * Instructions
					   */
#define X86_FEATURE_AVX512BW    (9*32+30) /* AVX-512 BW (Byte/Word granular)
					   * Instructions
					   */
#define X86_FEATURE_AVX512VL    (9*32+31) /* AVX-512 VL (128/256 Vector Length)
					   * Extensions
					   */
#define X86_FEATURE_MMXEXT	(1*32+22) /* AMD MMX extensions */

/* Should work well enough on modern CPUs for testing */
static inline int boot_cpu_has(int flag)
{
	u32 eax, ebx, ecx, edx;

	eax = (flag & 0x100) ? 7 :
		(flag & 0x20) ? 0x80000001 : 1;
	ecx = 0;

	asm volatile("cpuid"
		     : "+a" (eax), "=b" (ebx), "=d" (edx), "+c" (ecx));

	return ((flag & 0x100 ? ebx :
		(flag & 0x80) ? ecx : edx) >> (flag & 31)) & 1;
}

#endif /* ndef __KERNEL__ */

#endif
#endif
