/*	$OpenBSD: xxboot.S,v 1.2 2022/12/08 02:11:27 guenther Exp $	*/
/*	$NetBSD: xxboot.S,v 1.1 2006/09/01 21:26:19 uwe Exp $	*/

/*-
 * Copyright (c) 2005 NONAKA Kimihiro
 * All rights reserved.
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

ENTRY(xxboot_start)
	bra	xxboot_start1
	 nop
	.balign 4
ENTRY(xxboot_magic)
	.long	0x20031125
boot_params:
	.long	1f - boot_params

	. = xxboot_start + 0x80
xxboot_start1:
	mov.l	.L._end, r0		/* zero bss */
	mov.l	.L.__bss_start, r1
	sub	r1, r0
	shlr2	r0			/* _end and __bss_start are aligned */
	mov	#0, r2
1:	mov.l	r2, @r1
	dt	r0
	bf/s	1b
	 add	#4, r1

	mov.l	.L.2nd_loadaddr, r15	/* new stack pointer */
	mov.l	r4, @-r15
	mov.l	.L.boot1, r0
	jsr	@r0
	 mov	r15, r4

	tst	r0, r0
	bf/s	boot_fail
	 mov.l	@r15+, r5		/* pass boot sector to boot */

	/* flush cache */
	mov	#0, r4
	mov	#6, r0
	trapa	#0x3f

	mov.l	.L.2nd_loadaddr, r0
	mov.l	.L.boot_params, r4
	jmp	@r0
	 nop

boot_fail:
	mov	r0, r1
	mova	.L.errtxt, r0
	mov	r0, r4
	mov	#32, r0
	trapa	#0x3f
	mov	r1, r4
	mov	#32, r0
	trapa	#0x3f
	mova	.L.crlf, r0
	mov	r0, r4
	mov	#32, r0
	trapa	#0x3f
99:	bra	99b
	 nop


	.align	2
.L._end:
	.long	_end
.L.__bss_start:
	.long	__bss_start
.L.boot1:
	.long	boot1
.L.2nd_loadaddr:
	.long	LOADADDRESS
.L.boot_params:
	.long	boot_params

	.align	2
.L.errtxt:	.asciz	"Error: "
	.align	2
.L.crlf:	.asciz	"\r\n"


/*
 * int readsects(int dev, uint32_t lba, void *buf, size_t size);
 */
ENTRY(readsects)
	mov	#2, r0
	trapa	#0x3f
	rts
	 nop
argerror:
	rts
	 mov	#-1, r0

/*
 * void putstr(const char *str);
 */
ENTRY(putstr)
	mov	#32, r0
	trapa	#0x3f
	rts
	 nop

/*
 * void putchar(int c);
 */
ENTRY(putchar)
	mov	#31, r0
	trapa	#0x3f
	rts
	 nop

