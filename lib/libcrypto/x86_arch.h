/*	$OpenBSD: x86_arch.h,v 1.2 2024/10/18 13:36:24 jsing Exp $	*/
/*
 * Copyright (c) 2016 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * The knowledge of the layout of OPENSSL_ia32cap_P is internal to libcrypto
 * (and, to some extent, to libssl), and may change in the future without
 * notice.
 */

/*
 * OPENSSL_ia32cap_P is computed at runtime by OPENSSL_ia32_cpuid().
 *
 * On processors which lack the cpuid instruction, the value is always
 * zero (this only matters on 32-bit processors, of course).
 *
 * On processors which support the cpuid instruction, after running
 * "cpuid 1", the value of %edx is written to the low word of OPENSSL_ia32cap_P,
 * and the value of %ecx is written to its high word.
 *
 * Further processing is done to set or clear specific bits, depending
 * upon the exact processor type.
 *
 * Assembly routines usually address OPENSSL_ia32cap_P as two 32-bit words,
 * hence two sets of bit numbers and masks. OPENSSL_cpu_caps() returns the
 * complete 64-bit word.
 */

/* bit numbers for the low word */
#define	IA32CAP_BIT0_FPU	0
#define	IA32CAP_BIT0_MMX	23
#define	IA32CAP_BIT0_FXSR	24
#define	IA32CAP_BIT0_SSE	25
#define	IA32CAP_BIT0_SSE2	26
#define	IA32CAP_BIT0_HT		28

/* the following bits are not obtained from cpuid */
#define	IA32CAP_BIT0_INTELP4	20
#define	IA32CAP_BIT0_INTEL	30

/* bit numbers for the high word */
#define	IA32CAP_BIT1_PCLMUL	1
#define	IA32CAP_BIT1_SSSE3	9
#define	IA32CAP_BIT1_FMA3	12
#define	IA32CAP_BIT1_AESNI	25
#define	IA32CAP_BIT1_OSXSAVE	27
#define	IA32CAP_BIT1_AVX	28

#define	IA32CAP_BIT1_AMD_XOP	11

/* bit masks for the low word */
#define	IA32CAP_MASK0_MMX	(1 << IA32CAP_BIT0_MMX)
#define	IA32CAP_MASK0_FXSR	(1 << IA32CAP_BIT0_FXSR)
#define	IA32CAP_MASK0_SSE	(1 << IA32CAP_BIT0_SSE)
#define	IA32CAP_MASK0_SSE2	(1 << IA32CAP_BIT0_SSE2)
#define	IA32CAP_MASK0_HT	(1 << IA32CAP_BIT0_HT)

#define	IA32CAP_MASK0_INTELP4	(1 << IA32CAP_BIT0_INTELP4)
#define	IA32CAP_MASK0_INTEL	(1 << IA32CAP_BIT0_INTEL)

/* bit masks for the high word */
#define	IA32CAP_MASK1_PCLMUL	(1 << IA32CAP_BIT1_PCLMUL)
#define	IA32CAP_MASK1_SSSE3	(1 << IA32CAP_BIT1_SSSE3)
#define	IA32CAP_MASK1_FMA3	(1 << IA32CAP_BIT1_FMA3)
#define	IA32CAP_MASK1_AESNI	(1 << IA32CAP_BIT1_AESNI)
#define	IA32CAP_MASK1_OSXSAVE	(1 << IA32CAP_BIT1_OSXSAVE)
#define	IA32CAP_MASK1_AVX	(1 << IA32CAP_BIT1_AVX)

#define	IA32CAP_MASK1_AMD_XOP	(1 << IA32CAP_BIT1_AMD_XOP)

/* bit masks for OPENSSL_cpu_caps() */
#define	CPUCAP_MASK_HT		IA32CAP_MASK0_HT
#define	CPUCAP_MASK_MMX		IA32CAP_MASK0_MMX
#define	CPUCAP_MASK_FXSR	IA32CAP_MASK0_FXSR
#define	CPUCAP_MASK_SSE		IA32CAP_MASK0_SSE
#define	CPUCAP_MASK_SSE2	IA32CAP_MASK0_SSE2
#define	CPUCAP_MASK_INTEL	IA32CAP_MASK0_INTEL
#define	CPUCAP_MASK_INTELP4	IA32CAP_MASK0_INTELP4
#define	CPUCAP_MASK_PCLMUL	(1ULL << (32 + IA32CAP_BIT1_PCLMUL))
#define	CPUCAP_MASK_SSSE3	(1ULL << (32 + IA32CAP_BIT1_SSSE3))
#define	CPUCAP_MASK_AESNI	(1ULL << (32 + IA32CAP_BIT1_AESNI))
#define	CPUCAP_MASK_AVX		(1ULL << (32 + IA32CAP_BIT1_AVX))
