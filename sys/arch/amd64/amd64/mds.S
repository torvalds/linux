/*	$OpenBSD: mds.S,v 1.5 2024/04/14 09:59:04 kettenis Exp $	*/
/*
 * Copyright (c) 2019 Philip Guenther <guenther@openbsd.org>
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
 * ASM sequences for mitigating MDS on different Intel CPU models, taken from
 *	https://software.intel.com/security-software-guidance/insights/deep-dive-intel-analysis-microarchitectural-data-sampling
 * and adjusted to fit OpenBSD style and kernel usage.
 * Some naming inspired by FreeBSD's usage of these sequences.
 */

#include "assym.h"

#include <machine/asm.h>
#include <machine/specialreg.h>

ENTRY(mds_handler_ivb)
	RETGUARD_SETUP(mds_handler_ivb, r11)
	movq	CPUVAR(MDS_BUF),%rax
	movdqa	%xmm0,CPUVAR(MDS_TMP)
	pxor	%xmm0,%xmm0

	lfence
	orpd	(%rax),%xmm0
	orpd	(%rax),%xmm0
	mfence
	movl	$40,%ecx
	addq	$16,%rax
1:	movntdq	%xmm0,(%rax)
	addq	$16,%rax
	decl	%ecx
	jnz	1b
	mfence

	movdqa	CPUVAR(MDS_TMP),%xmm0
	RETGUARD_CHECK(mds_handler_ivb, r11)
	retq
	lfence
END(mds_handler_ivb)

ENTRY(mds_handler_bdw)
	RETGUARD_SETUP(mds_handler_bdw, r11)
	movq	CPUVAR(MDS_BUF),%rax
	movdqa	%xmm0,CPUVAR(MDS_TMP)
	pxor	%xmm0,%xmm0

	movq	%rax,%rdi
	movq	%rax,%rsi
	movl	$40,%ecx
1:	movntdq	%xmm0,(%rax)
	addq	$16,%rax
	decl	%ecx
	jnz	1b
	mfence
	movl	$1536,%ecx
	rep movsb
	lfence

	movdqa	CPUVAR(MDS_TMP),%xmm0
	RETGUARD_CHECK(mds_handler_bdw, r11)
	retq
	lfence
END(mds_handler_bdw)

ENTRY(mds_handler_skl)
	xorl	%ecx,%ecx
	xgetbv
	testb	$XFEATURE_AVX512,%al
	jne	mds_handler_skl_avx512
	testb	$XFEATURE_AVX,%al
	jne	mds_handler_skl_avx
	jmp	mds_handler_skl_sse
END(mds_handler_skl)

ENTRY(mds_handler_skl_sse)
	RETGUARD_SETUP(mds_handler_skl_sse, r11)
	movq	CPUVAR(MDS_BUF),%rax
	leaq	64(%rax),%rdi
	movdqa	%xmm0,CPUVAR(MDS_TMP)
	pxor	%xmm0,%xmm0

	lfence
	orpd	(%rax),%xmm0
	orpd	(%rax),%xmm0
	xorl	%eax,%eax
1:	clflushopt	5376(%rdi,%rax,8)
	addl	$8,%eax
	cmpl	$8*12,%eax
	jb	1b
	sfence
	movl	$6144,%ecx
	xorl	%eax,%eax
	rep stosb
	mfence

	movdqa	CPUVAR(MDS_TMP),%xmm0
	RETGUARD_CHECK(mds_handler_skl_sse, r11)
	retq
	lfence
END(mds_handler_skl_sse)

ENTRY(mds_handler_skl_avx)
	RETGUARD_SETUP(mds_handler_skl_avx, r11)
	movq	CPUVAR(MDS_BUF),%rax
	leaq	64(%rax),%rdi
	vmovdqa	%ymm0,CPUVAR(MDS_TMP)
	vpxor	%ymm0,%ymm0,%ymm0

	lfence
	vorpd	(%rax),%ymm0,%ymm0
	vorpd	(%rax),%ymm0,%ymm0
	xorl	%eax,%eax
1:	clflushopt	5376(%rdi,%rax,8)
	addl	$8,%eax
	cmpl	$8*12,%eax
	jb	1b
	sfence
	movl	$6144,%ecx
	xorl	%eax,%eax
	rep stosb
	mfence

	vmovdqa	CPUVAR(MDS_TMP),%ymm0
	RETGUARD_CHECK(mds_handler_skl_avx, r11)
	retq
	lfence
END(mds_handler_skl_avx)

ENTRY(mds_handler_skl_avx512)
	RETGUARD_SETUP(mds_handler_skl_avx512, r11)
	movq	CPUVAR(MDS_BUF),%rax
	leaq	64(%rax),%rdi
	vmovdqa64	%zmm0,CPUVAR(MDS_TMP)
	vpxorq	%zmm0,%zmm0,%zmm0

	lfence
	vorpd	(%rax),%zmm0,%zmm0
	vorpd	(%rax),%zmm0,%zmm0
	xorl	%eax,%eax
1:	clflushopt	5376(%rdi,%rax,8)
	addl	$8,%eax
	cmpl	$8*12,%eax
	jb	1b
	sfence
	movl	$6144,%ecx
	xorl	%eax,%eax
	rep stosb
	mfence

	vmovdqa64	CPUVAR(MDS_TMP),%zmm0
	RETGUARD_CHECK(mds_handler_skl_avx512, r11)
	retq
	lfence
END(mds_handler_skl_avx512)

ENTRY(mds_handler_silvermont)
	RETGUARD_SETUP(mds_handler_silvermont, r11)
	movq	CPUVAR(MDS_BUF),%rax
	movdqa	%xmm0,CPUVAR(MDS_TMP)
	pxor	%xmm0,%xmm0

	movl	$16,%ecx
1:	movntdq	%xmm0,(%rax)
	addq	$16,%rax
	decl	%ecx
	jnz	1b
	mfence

	movdqa	CPUVAR(MDS_TMP),%xmm0
	RETGUARD_CHECK(mds_handler_silvermont, r11)
	retq
	lfence
END(mds_handler_silvermont)

ENTRY(mds_handler_knights)
	RETGUARD_SETUP(mds_handler_knights, r11)
	movq	CPUVAR(MDS_BUF),%rdi
	xorl	%eax,%eax
	movl	$16,%ecx
	rep stosq
	movl	$128,%ecx
	rep stosq
	mfence
	RETGUARD_CHECK(mds_handler_knights, r11)
	retq
	lfence
END(mds_handler_knights)
