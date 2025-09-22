/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from locore.s.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asm.h>

	/*
	 * memmove (dst,src,cnt)
	 *  ws@tools.de     (Wolfgang Solfrank, TooLs GmbH) +49-228-985800
	 */

ENTRY(bcopy)
	xchgq	%rdi,%rsi
	/* fall into memmove */

NENTRY(memmove)
	endbr64
	RETGUARD_SETUP(memmove, r10)
	movq	%rdi,%r11	/* save dest */
	movq	%rdx,%rcx
	movq	%rdi,%rax
	subq	%rsi,%rax
	cmpq	%rcx,%rax	/* overlapping? */
	jb	1f
	jmp	2f		/* nope */

ENTRY(memcpy)
	RETGUARD_SETUP(memmove, r10)
	movq	%rdi,%r11	/* save dest */
	movq	%rdx,%rcx
2:
	shrq	$3,%rcx		/* copy by words */
	rep
	movsq
	movq	%rdx,%rcx
	andq	$7,%rcx		/* any bytes left? */
	rep
	movsb
	movq	%r11,%rax
	jmp 3f
1:
	addq	%rcx,%rdi	/* copy backwards. */
	addq	%rcx,%rsi
	std
	andq	$7,%rcx		/* any fractional bytes? */
	decq	%rdi
	decq	%rsi
	rep
	movsb
	movq	%rdx,%rcx	/* copy remainder by words */
	shrq	$3,%rcx
	subq	$7,%rsi
	subq	$7,%rdi
	rep
	movsq
	movq	%r11,%rax
	cld
3:	RETGUARD_CHECK(memmove, r10)
	ret
	lfence
END(bcopy)
END(memmove)
END(memcpy)
