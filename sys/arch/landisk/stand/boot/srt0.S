/*	$OpenBSD: srt0.S,v 1.6 2022/12/08 02:11:27 guenther Exp $	*/
/*	$NetBSD: boot.S,v 1.1 2006/09/01 21:26:18 uwe Exp $	*/

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

ENTRY(start)
	bra	boot_start1
	 nop
	.balign 4
ENTRY(boot_magic)
	.long	0x20041110
ENTRY(boot_params)
	.long	boot_start1 - boot_params

	. = start + 0x80
boot_start1:
	mov	r4, r0
	add	#-4, r0
	mov.l	@(0, r0), r0
	mov.l	.L.boot_magic1, r1
	cmp/eq	r0, r1
	bf	2f
	mov	r4, r0
	mov.l	.L.boot_params_size, r3
	mov.l	@r0, r2
	mov.l	.L.boot_params, r1
	cmp/hi	r3, r2
	bf	1f
	mov	r3, r2
1:	mov.b	@r0+, r3
	mov.b	r3, @r1
	dt	r2
	bf/s	1b
	 add	#1, r1
2:
	mov.l	.L._end, r0		/* zero bss */
	mov.l	.L.__bss_start, r1
	sub	r1, r0
	shlr2	r0			/* _end and __bss_start are aligned */
	mov	#0, r2
1:	mov.l	r2, @r1
	dt	r0
	bf/s	1b
	 add	#4, r1

	mov.l	.L.boot, r0
	jsr	@r0
	 mov	r5, r4

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


ENTRY(halt)
	mova	.L.pwrctl, r0
	mov	#1, r1
	mov.b	@r1, r0
	rts
	 nop

ENTRY(reboot)
ENTRY(_rtt)
	mov	#1, r4			/* reboot */
	mov	#11, r0
	trapa	#0x3f
	mov.l	.L.start, r0
	jmp	@r0
	 nop

/*
 * int readsects(int dev, uint32_t lba, void *buf, size_t size);
 */
ENTRY(readsects)
	mov	#2, r0
	trapa	#0x3f
	rts
	 nop

/*
 * void cache_flush(void);
 */
ENTRY(cache_flush)
	/* flush cache */
	mov	#0, r4
	mov	#6, r0
	trapa	#0x3f
	rts
	 nop

/*
 * void cache_disable(void);
 */
ENTRY(cache_disable)
	mov     #1, r4
	mov     #6, r0
	trapa   #0x3f
	rts
	 nop

        .align  2
.L.boot_magic1:
	.long	0x20031125
.L.boot_params:
	.long	boot_params
.L.boot_params_size:
	.long	boot_start1 - boot_params
.L._end:
	.long   _end
.L.__bss_start:
	.long   __bss_start
.L.boot:
	.long	boot
.L.start:
	.long	0xc0000000
.L.pwrctl:
	.long	0xb0000003

	.align	2
.L.errtxt:	.asciz  ">>BOOT FAILED: "
	.align	2
.L.crlf:	.asciz  "\r\n"
