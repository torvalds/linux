/*	$NetBSD: memcpy.S,v 1.2 2006/04/22 23:53:47 uwe Exp $	*/

/*
 * Copyright (c) 2000 SHIMIZU Ryo <ryo@misakimix.org>
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

#define	REG_DST0	r3
#define	REG_SRC		r5
#define	REG_DST		r4
#define	REG_LEN		r6

ENTRY(bcopy)
	/* swap registers, use DST0 as a temporary */
	mov	REG_DST,REG_DST0
	mov	REG_SRC,REG_DST
	mov	REG_DST0,REG_SRC

ENTRY(memmove)
	mov	REG_DST,REG_DST0
	cmp/hi	REG_DST,REG_SRC
	bf/s	bcopy_overlap

ENTRY(memcpy)
	mov	REG_DST,REG_DST0
	cmp/eq	REG_DST,REG_SRC	/* if ( src == dst ) return; */
	bt/s	bcopy_return
	mov	REG_SRC,r0
	xor	REG_DST,r0
	and	#3,r0
	mov	r0,r1
	tst	r0,r0		/* (src ^ dst) & 3         */
	bf/s	word_align

longword_align:
	tst	REG_LEN,REG_LEN	/* if ( len==0 ) return;   */
	bt/s	bcopy_return


	mov	REG_SRC,r0
	tst	#1,r0		/* if ( src & 1 )          */
	bt	1f
	mov.b	@REG_SRC+,r0	/*    *dst++ = *src++;     */
	add	#-1,REG_LEN
	mov.b	r0,@REG_DST
	add	#1,REG_DST
1:

	mov	#1,r0
	cmp/hi	r0,REG_LEN	/* if ( (len > 1) &&       */
	bf/s	1f
	mov	REG_SRC,r0
	tst	#2,r0		/*      (src & 2) {        */
	bt	1f
	mov.w	@REG_SRC+,r0	/*        *((unsigned short*)dst)++ = *((unsigned short*)src)++; */
	add	#-2,REG_LEN	/*        len -= 2;                                              */
	mov.w	r0,@REG_DST
	add	#2,REG_DST	/* }                       */
1:


	mov	#3,r1
	cmp/hi	r1,REG_LEN	/* while ( len > 3 ) {     */
	bf/s	no_align_delay
	tst	REG_LEN,REG_LEN
2:
	mov.l	@REG_SRC+,r0	/*   *((unsigned long*)dst)++ = *((unsigned long*)src)++;        */
	add	#-4,REG_LEN	/*   len -= 4;                                                   */
	mov.l	r0,@REG_DST
	cmp/hi	r1,REG_LEN
	bt/s	2b
	add	#4,REG_DST	/* }                       */

	bra	no_align_delay
	tst	REG_LEN,REG_LEN


word_align:
	mov	r1,r0
	tst	#1,r0
	bf/s	no_align_delay
	tst	REG_LEN,REG_LEN	/* if ( len == 0 ) return; */
	bt	bcopy_return


	mov	REG_SRC,r0	/* if ( src & 1 )          */
	tst	#1,r0
	bt	1f
	mov.b	@REG_SRC+,r0	/*    *dst++ = *src++;     */
	add	#-1,REG_LEN
	mov.b	r0,@REG_DST
	add	#1,REG_DST
1:


	mov	#1,r1
	cmp/hi	r1,REG_LEN	/* while ( len > 1 ) {     */
	bf/s	no_align_delay
	tst	REG_LEN,REG_LEN
2:
	mov.w	@REG_SRC+,r0	/*   *((unsigned short*)dst)++ = *((unsigned short*)src)++;      */
	add	#-2,REG_LEN	/*   len -= 2;                                                   */
	mov.w	r0,@REG_DST
	cmp/hi	r1,REG_LEN
	bt/s	2b
	add	#2,REG_DST	/* }                       */


no_align:
	tst	REG_LEN,REG_LEN	/* while ( len!= ) {       */
no_align_delay:
	bt	bcopy_return
1:
	mov.b	@REG_SRC+,r0	/*    *dst++ = *src++;     */
	add	#-1,REG_LEN	/*    len--;               */
	mov.b	r0,@REG_DST
	tst	REG_LEN,REG_LEN
	bf/s	1b
	add	#1,REG_DST	/* }                       */
bcopy_return:
	rts
	mov	REG_DST0,r0

bcopy_overlap:
	add	REG_LEN,REG_SRC
	add	REG_LEN,REG_DST

	mov	REG_SRC,r0
	xor	REG_DST,r0
	and	#3,r0
	mov	r0,r1
	tst	r0,r0		/* (src ^ dst) & 3         */
	bf/s	ov_word_align

ov_longword_align:
	tst	REG_LEN,REG_LEN	/* if ( len==0 ) return;   */
	bt/s	bcopy_return


	mov	REG_SRC,r0
	tst	#1,r0		/* if ( src & 1 )          */
	bt	1f
	add	#-1,REG_SRC	/*    *--dst = *--src;     */
	mov.b	@REG_SRC,r0
	mov.b	r0,@-REG_DST
	add	#-1,REG_LEN
1:


	mov	#1,r0
	cmp/hi	r0,REG_LEN	/* if ( (len > 1) &&       */
	bf/s	1f
	mov	REG_SRC,r0
	tst	#2,r0		/*      (src & 2) {        */
	bt	1f
	add	#-2,REG_SRC	/*        *--((unsigned short*)dst) = *--((unsigned short*)src); */
	mov.w	@REG_SRC,r0
	add	#-2,REG_LEN	/*        len -= 2;                                              */
	mov.w	r0,@-REG_DST	/* }                       */
1:


	mov	#3,r1
	cmp/hi	r1,REG_LEN	/* while ( len > 3 ) {     */
	bf/s	ov_no_align_delay
	tst	REG_LEN,REG_LEN
2:
	add	#-4,REG_SRC
	mov.l	@REG_SRC,r0	/*   *((unsigned long*)dst)++ = *((unsigned long*)src)++;        */
	add	#-4,REG_LEN	/*   len -= 4;                                                   */
	cmp/hi	r1,REG_LEN
	bt/s	2b
	mov.l	r0,@-REG_DST	/* }                       */

	bra	ov_no_align_delay
	tst	REG_LEN,REG_LEN


ov_word_align:
	mov	r1,r0
	tst	#1,r0
	bf/s	ov_no_align_delay
	tst	REG_LEN,REG_LEN	/* if ( len == 0 ) return; */
	bt	bcopy_return


	mov	REG_SRC,r0	/* if ( src & 1 )          */
	tst	#1,r0
	bt	1f
	add	#-1,REG_SRC
	mov.b	@REG_SRC,r0	/*    *--dst = *--src;     */
	add	#-1,REG_LEN
	mov.b	r0,@-REG_DST
1:


	mov	#1,r1
	cmp/hi	r1,REG_LEN	/* while ( len > 1 ) {     */
	bf/s	ov_no_align_delay
	tst	REG_LEN,REG_LEN
2:
	add	#-2,REG_SRC
	mov.w	@REG_SRC,r0	/*   *--((unsigned short*)dst) = *--((unsigned short*)src);      */
	add	#-2,REG_LEN	/*   len -= 2;                                                   */
	cmp/hi	r1,REG_LEN
	bt/s	2b
	mov.w	r0,@-REG_DST	/* }                       */


ov_no_align:
	tst	REG_LEN,REG_LEN	/* while ( len!= ) {       */
ov_no_align_delay:
	bt	9f
1:
	add	#-1,REG_SRC
	mov.b	@REG_SRC,r0	/*    *--dst = *--src;     */
	add	#-1,REG_LEN	/*    len--;               */
	tst	REG_LEN,REG_LEN
	bf/s	1b
	mov.b	r0,@-REG_DST	/* }                       */
9:
	rts
	mov	REG_DST0,r0
