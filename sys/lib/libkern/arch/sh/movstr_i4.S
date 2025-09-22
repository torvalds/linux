/*	$NetBSD: movstr_i4.S,v 1.5 2006/05/22 21:34:08 uwe Exp $	*/

/*-
 * Copyright (C) 2002 SHIMIZU Ryo.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/asm.h>


NENTRY(__movstr_i4_odd)
	add	#-8,r4
	nop
odd_loop:
	mov.l	@r5+,r0
	add	#8,r4
	mov.l	@r5+,r1
	dt	r6
	mov.l	r0,@(0,r4)
	bf/s	odd_loop
	 mov.l	r1,@(4,r4)

	mov.l	@r5+,r0
	mov.l	@r5+,r1
	mov.l	@r5+,r2
	mov.l	r0,@(8,r4)
	mov.l	r1,@(12,r4)
	rts
	 mov.l	r2,@(16,r4)


NENTRY(__movstr_i4_even)
	add	#-8,r4
	nop
even_loop:
	mov.l	@r5+,r0
	add	#8,r4
	mov.l	@r5+,r1
	dt	r6
	mov.l	r0,@(0,r4)
	bf/s	even_loop
	 mov.l	r1,@(4,r4)

	mov.l	@r5+,r0
	mov.l	@r5+,r1
	mov.l	r0,@(8,r4)
	rts
	 mov.l	r1,@(12,r4)


/* gcc4 uses movmem, older versions use movstr */
STRONG_ALIAS(__movmem_i4_odd, __movstr_i4_odd)
STRONG_ALIAS(__movmem_i4_even, __movstr_i4_even)
