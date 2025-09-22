/*	$OpenBSD: srt0.S,v 1.11 2011/04/17 09:49:48 kettenis Exp $	*/

/*
 * Copyright (c) 1998-2004 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright 1996 1995 by Open Software Foundation, Inc.   
 *              All Rights Reserved 
 *  
 * Permission to use, copy, modify, and distribute this software and 
 * its documentation for any purpose and without fee is hereby granted, 
 * provided that the above copyright notice appears in all copies and 
 * that both the copyright notice and this permission notice appear in 
 * supporting documentation. 
 *  
 * OSF DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE 
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
 * FOR A PARTICULAR PURPOSE. 
 *  
 * IN NO EVENT SHALL OSF BE LIABLE FOR ANY SPECIAL, INDIRECT, OR 
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM 
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT, 
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION 
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. 
 * 
 */
;
; Copyright (c) 1990 mt Xinu, Inc.  All rights reserved.
; Copyright (c) 1990 University of Utah.  All rights reserved.
;
; This file may be freely distributed in any form as long as
; this copyright notice is included.
; THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
; IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
; WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
;
;	Utah $Hdr: srt0.c 1.3 94/12/13$
;

#define	_LOCORE
#include <machine/asm.h>
#include <machine/cpu.h>
#include <machine/iomod.h>

/*
 * This is the ending of the begin
 */
ENTRY(begin,0)

	blr	%r0,%r5			; Get address of 'boff' into 'r5',
	ldo	begin-boff(%r5),%r5	;   and subtract to get 'begin'.
boff
	ldil	L%RELOC,%r4
	ldo	R%RELOC(%r4),%r4
	ldo	start-begin(%r4),%rp
	ldil	L%edata,%r3
	ldo	R%edata(%r3),%r3	; Get address of edata.
	ldil	L%begin,%r1
	ldo	R%begin(%r1),%r1	; Get address of begin
	sub	%r3,%r1,%r3		; Subtract to get # of bytes to copy
copyloop				; do
	ldwm	4(%r5),%r1		;   *r4++ = *r5++;
	addib,>= -4,%r3,copyloop	; while (--r3 >= 0);
	stwm	%r1,4(%r4)

	; here we zero the .bss
	ldil	L%__bss_start, %r4
	ldo	R%__bss_start(%r4), %r4
	ldil	L%__bss_end, %r3
	ldo	R%__bss_end(%r3), %r3
zeroloop
	combf,<,n %r3,%r4, zeroloop	; while (r4 < r3);
	stwm	%r0,4(%r4)		;	*r4++ = 0;

	ldil	L%$global$,%dp
	ldo	R%$global$(%dp),%dp
	ldil	L%start,%r1
	ldo	R%start(%r1),%r1
	sub	%dp,%r1,%dp		; Subtract to get difference
	add	%rp,%dp,%dp		;   and relocate it.

;
; We have relocated ourself to RELOC.  If we are running on a machine
; with separate instruction and data caches, we must flush our data
; cache before trying to execute the code starting at rp.
;
	ldil	L%RELOC,%r22		; Set %t1 to start of relocated code.
	ldo	R%RELOC(%r22),%r22
	ldil	L%edata,%r21		; Set r21 to address of edata
	ldo	R%edata(%r21),%r21
	ldil	L%begin,%r1		; set %r1 to address of begin
	ldo	R%begin(%r1),%r1
	sub	%r21,%r1,%r21		; Subtract to get length
	mtsp	%r0,%sr0		; Set sr0 to kernel space.
	ldo	-1(%r21),%r21
	fdc	%r21(0,%r22)
loop	addib,>,n -16,%r21,loop		; Decrement by cache line size (16).
	fdc	%r21(%sr0,%r22)
	fdc	0(%sr0,%r22)		; Flush first word at addr to handle
	sync				;   arbitrary cache line boundary.
	nop				; Prevent prefetching.
	nop
	nop
	nop
	nop
	nop
	nop
	bv	0(rp)
	nop
EXIT(begin)				/* jump to relocated code */

start
	ldil	L%HEAP_LIMIT, %sp
	ldo	R%HEAP_LIMIT(%sp), %sp

	comb,=	%r0, %r26, call_boot
	nop
	.import	boottimeout, data
	ldil	L%boottimeout, %r22
	stw	%r0, R%boottimeout(%r22)

call_boot
	b	boot			; Call boot(),
	copy	%r0, arg0		; use default boot device
	nop

/*
 * rtt - restart the box
 */
LEAF_ENTRY(_rtt)
	ldil    L%HPPA_LBCAST, %r25
	ldi     CMD_RESET, %r26
	stw     %r26,R%iomod_command(%r25)
forever                                 ; Loop until bus reset takes effect.
	b,n     forever

	bv	0(rp)
	ldo     -48(sp),sp
EXIT(_rtt)

	.end
