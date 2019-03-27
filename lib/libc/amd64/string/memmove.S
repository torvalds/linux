/*-
 * Copyright (c) 2018 The FreeBSD Foundation
 *
 * This software was developed by Mateusz Guzik <mjg@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
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
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#define	ALIGN_TEXT	.p2align 4,0x90 /* 16-byte alignment, nop filled */

/*
 * memmove(dst, src, cnt)
 *         rdi, rsi, rdx
 */

/*
 * Register state at entry is supposed to be as follows:
 * rdi - destination
 * rsi - source
 * rdx - count
 *
 * The macro possibly clobbers the above and: rcx, r8, r9, 10
 * It does not clobber rax nor r11.
 */
.macro MEMMOVE erms overlap begin end
	\begin

	/*
	 * For sizes 0..32 all data is read before it is written, so there
	 * is no correctness issue with direction of copying.
	 */
	cmpq	$32,%rcx
	jbe	101632f

.if \overlap == 1
	movq	%rdi,%r8
	subq	%rsi,%r8
	cmpq	%rcx,%r8	/* overlapping && src < dst? */
	jb	2f
.endif

	cmpq	$256,%rcx
	ja	1256f

103200:
	movq	(%rsi),%rdx
	movq	%rdx,(%rdi)
	movq	8(%rsi),%rdx
	movq	%rdx,8(%rdi)
	movq	16(%rsi),%rdx
	movq	%rdx,16(%rdi)
	movq	24(%rsi),%rdx
	movq	%rdx,24(%rdi)
	leaq	32(%rsi),%rsi
	leaq	32(%rdi),%rdi
	subq	$32,%rcx
	cmpq	$32,%rcx
	jae	103200b
	cmpb	$0,%cl
	jne	101632f
	\end
	ret
	ALIGN_TEXT
101632:
	cmpb	$16,%cl
	jl	100816f
	movq	(%rsi),%rdx
	movq	8(%rsi),%r8
	movq	-16(%rsi,%rcx),%r9
	movq	-8(%rsi,%rcx),%r10
	movq	%rdx,(%rdi)
	movq	%r8,8(%rdi)
	movq	%r9,-16(%rdi,%rcx)
	movq	%r10,-8(%rdi,%rcx)
	\end
	ret
	ALIGN_TEXT
100816:
	cmpb	$8,%cl
	jl	100408f
	movq	(%rsi),%rdx
	movq	-8(%rsi,%rcx),%r8
	movq	%rdx,(%rdi)
	movq	%r8,-8(%rdi,%rcx,)
	\end
	ret
	ALIGN_TEXT
100408:
	cmpb	$4,%cl
	jl	100204f
	movl	(%rsi),%edx
	movl	-4(%rsi,%rcx),%r8d
	movl	%edx,(%rdi)
	movl	%r8d,-4(%rdi,%rcx)
	\end
	ret
	ALIGN_TEXT
100204:
	cmpb	$2,%cl
	jl	100001f
	movzwl	(%rsi),%edx
	movzwl	-2(%rsi,%rcx),%r8d
	movw	%dx,(%rdi)
	movw	%r8w,-2(%rdi,%rcx)
	\end
	ret
	ALIGN_TEXT
100001:
	cmpb	$1,%cl
	jl	100000f
	movb	(%rsi),%dl
	movb	%dl,(%rdi)
100000:
	\end
	ret

	ALIGN_TEXT
1256:
	testb	$15,%dil
	jnz	100f
.if \erms == 1
	rep
	movsb
.else
	shrq	$3,%rcx                         /* copy by 64-bit words */
	rep
	movsq
	movq	%rdx,%rcx
	andl	$7,%ecx                         /* any bytes left? */
	jne	100408b
.endif
	\end
	ret
100:
	movq	(%rsi),%r8
	movq	8(%rsi),%r9
	movq	%rdi,%r10
	movq	%rdi,%rcx
	andq	$15,%rcx
	leaq	-16(%rdx,%rcx),%rdx
	neg	%rcx
	leaq	16(%rdi,%rcx),%rdi
	leaq	16(%rsi,%rcx),%rsi
	movq	%rdx,%rcx
.if \erms == 1
	rep
	movsb
	movq	%r8,(%r10)
	movq	%r9,8(%r10)
.else
	shrq	$3,%rcx                         /* copy by 64-bit words */
	rep
	movsq
	movq	%r8,(%r10)
	movq	%r9,8(%r10)
	movq	%rdx,%rcx
	andl	$7,%ecx                         /* any bytes left? */
	jne	100408b
.endif
	\end
	ret

.if \overlap == 1
	/*
	 * Copy backwards.
	 */
        ALIGN_TEXT
2:
	cmpq	$256,%rcx
	ja	2256f

	leaq	-8(%rdi,%rcx),%rdi
	leaq	-8(%rsi,%rcx),%rsi

	cmpq	$32,%rcx
	jb	2016f

2032:
	movq	(%rsi),%rdx
	movq	%rdx,(%rdi)
	movq	-8(%rsi),%rdx
	movq	%rdx,-8(%rdi)
	movq	-16(%rsi),%rdx
	movq	%rdx,-16(%rdi)
	movq	-24(%rsi),%rdx
	movq	%rdx,-24(%rdi)
	leaq	-32(%rsi),%rsi
	leaq	-32(%rdi),%rdi
	subq	$32,%rcx
	cmpq	$32,%rcx
	jae	2032b
	cmpb	$0,%cl
	jne	2016f
	\end
	ret
	ALIGN_TEXT
2016:
	cmpb	$16,%cl
	jl	2008f
	movq	(%rsi),%rdx
	movq	%rdx,(%rdi)
	movq	-8(%rsi),%rdx
	movq	%rdx,-8(%rdi)
	subb	$16,%cl
	jz	2000f
	leaq	-16(%rsi),%rsi
	leaq	-16(%rdi),%rdi
2008:
	cmpb	$8,%cl
	jl	2004f
	movq	(%rsi),%rdx
	movq	%rdx,(%rdi)
	subb	$8,%cl
	jz	2000f
	leaq	-8(%rsi),%rsi
	leaq	-8(%rdi),%rdi
2004:
	cmpb	$4,%cl
	jl	2002f
	movl	4(%rsi),%edx
	movl	%edx,4(%rdi)
	subb	$4,%cl
	jz	2000f
	leaq	-4(%rsi),%rsi
	leaq	-4(%rdi),%rdi
2002:
	cmpb	$2,%cl
	jl	2001f
	movw	6(%rsi),%dx
	movw	%dx,6(%rdi)
	subb	$2,%cl
	jz	2000f
	leaq	-2(%rsi),%rsi
	leaq	-2(%rdi),%rdi
2001:
	cmpb	$1,%cl
	jl	2000f
	movb	7(%rsi),%dl
	movb	%dl,7(%rdi)
2000:
	\end
	ret
	ALIGN_TEXT
2256:
	std
.if \erms == 1
	leaq	-1(%rdi,%rcx),%rdi
	leaq	-1(%rsi,%rcx),%rsi
	rep
	movsb
	cld
.else
	leaq	-8(%rdi,%rcx),%rdi
	leaq	-8(%rsi,%rcx),%rsi
	shrq	$3,%rcx
	rep
	movsq
	cld
	movq	%rdx,%rcx
	andb	$7,%cl
	jne	2004b
.endif
	\end
	ret
.endif
.endm


.macro MEMMOVE_BEGIN
	movq	%rdi,%rax
	movq	%rdx,%rcx
.endm

.macro MEMMOVE_END
.endm

#ifndef MEMCPY
ENTRY(memmove)
	MEMMOVE erms=0 overlap=1 begin=MEMMOVE_BEGIN end=MEMMOVE_END
END(memmove)
#else
ENTRY(memcpy)
	MEMMOVE erms=0 overlap=1 begin=MEMMOVE_BEGIN end=MEMMOVE_END
END(memcpy)
#endif
