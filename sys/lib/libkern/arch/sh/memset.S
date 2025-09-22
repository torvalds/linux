/*	$NetBSD: memset.S,v 1.1 2005/12/20 19:28:50 christos Exp $	*/

/*-
 * Copyright (c) 2002 SHIMIZU Ryo.  All rights reserved.
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

#define	REG_PTR				r0
#define	REG_TMP1			r1

#ifdef BZERO
# define	REG_C			r2
# define	REG_DST			r4
# define	REG_LEN			r5
#else
# define	REG_DST0		r3
# define	REG_DST			r4
# define	REG_C			r5
# define	REG_LEN			r6
#endif

#ifdef BZERO
ENTRY(bzero)
#else
ENTRY(memset)
	mov	REG_DST,REG_DST0	/* for return value */
#endif
	/* small amount to fill ? */
	mov	#28,REG_TMP1
	cmp/hs	REG_TMP1,REG_LEN	/* if (len >= 28) goto large; */
	bt/s	large
	mov	#12,REG_TMP1		/* if (len >= 12) goto small; */
	cmp/hs	REG_TMP1,REG_LEN
	bt/s	small
#ifdef BZERO
	mov	#0,REG_C
#endif
	/* very little fill (0 ~ 11 bytes) */
	tst	REG_LEN,REG_LEN
	add	REG_DST,REG_LEN
	bt/s	done
	add	#1,REG_DST

	/* unroll 4 loops */
	cmp/eq	REG_DST,REG_LEN
1:	mov.b	REG_C,@-REG_LEN
	bt/s	done
	cmp/eq	REG_DST,REG_LEN
	mov.b	REG_C,@-REG_LEN
	bt/s	done
	cmp/eq	REG_DST,REG_LEN
	mov.b	REG_C,@-REG_LEN
	bt/s	done
	cmp/eq	REG_DST,REG_LEN
	mov.b	REG_C,@-REG_LEN
	bf/s	1b
	cmp/eq	REG_DST,REG_LEN
done:
#ifdef BZERO
	rts
	nop
#else
	rts
	mov	REG_DST0,r0
#endif


small:
	mov	REG_DST,r0
	tst	#1,r0
	bt/s	small_aligned
	mov	REG_DST,REG_TMP1
	shll	REG_LEN
	mova	1f,r0			/* 1f must be 4bytes aligned! */
	add	#16,REG_TMP1		/* REG_TMP1 = dst+16; */
	sub	REG_LEN,r0
	jmp	@r0
	mov	REG_C,r0

	.align	2
	mov.b	r0,@(15,REG_TMP1)
	mov.b	r0,@(14,REG_TMP1)
	mov.b	r0,@(13,REG_TMP1)
	mov.b	r0,@(12,REG_TMP1)
	mov.b	r0,@(11,REG_TMP1)
	mov.b	r0,@(10,REG_TMP1)
	mov.b	r0,@(9,REG_TMP1)
	mov.b	r0,@(8,REG_TMP1)
	mov.b	r0,@(7,REG_TMP1)
	mov.b	r0,@(6,REG_TMP1)
	mov.b	r0,@(5,REG_TMP1)
	mov.b	r0,@(4,REG_TMP1)
	mov.b	r0,@(3,REG_TMP1)
	mov.b	r0,@(2,REG_TMP1)
	mov.b	r0,@(1,REG_TMP1)
	mov.b	r0,@REG_TMP1
	mov.b	r0,@(15,REG_DST)
	mov.b	r0,@(14,REG_DST)
	mov.b	r0,@(13,REG_DST)
	mov.b	r0,@(12,REG_DST)
	mov.b	r0,@(11,REG_DST)
	mov.b	r0,@(10,REG_DST)
	mov.b	r0,@(9,REG_DST)
	mov.b	r0,@(8,REG_DST)
	mov.b	r0,@(7,REG_DST)
	mov.b	r0,@(6,REG_DST)
	mov.b	r0,@(5,REG_DST)
	mov.b	r0,@(4,REG_DST)
	mov.b	r0,@(3,REG_DST)
	mov.b	r0,@(2,REG_DST)
	mov.b	r0,@(1,REG_DST)
#ifdef BZERO
	rts
1:	mov.b	r0,@REG_DST
#else
	mov.b	r0,@REG_DST
1:	rts
	mov	REG_DST0,r0
#endif


/* 2 bytes aligned small fill */
small_aligned:
#ifndef BZERO
	extu.b	REG_C,REG_TMP1		/* REG_C = ??????xx, REG_TMP1 = ????00xx */
	shll8	REG_C			/* REG_C = ????xx00, REG_TMP1 = ????00xx */
	or	REG_TMP1,REG_C		/* REG_C = ????xxxx */
#endif

	mov	REG_LEN,r0
	tst	#1,r0			/* len is aligned? */
	bt/s	1f
	add	#-1,r0
	mov.b	REG_C,@(r0,REG_DST)	/* fill last a byte */
	mov	r0,REG_LEN
1:

	mova	1f,r0			/* 1f must be 4bytes aligned! */
	sub	REG_LEN,r0
	jmp	@r0
	mov	REG_C,r0

	.align	2
	mov.w	r0,@(30,REG_DST)
	mov.w	r0,@(28,REG_DST)
	mov.w	r0,@(26,REG_DST)
	mov.w	r0,@(24,REG_DST)
	mov.w	r0,@(22,REG_DST)
	mov.w	r0,@(20,REG_DST)
	mov.w	r0,@(18,REG_DST)
	mov.w	r0,@(16,REG_DST)
	mov.w	r0,@(14,REG_DST)
	mov.w	r0,@(12,REG_DST)
	mov.w	r0,@(10,REG_DST)
	mov.w	r0,@(8,REG_DST)
	mov.w	r0,@(6,REG_DST)
	mov.w	r0,@(4,REG_DST)
	mov.w	r0,@(2,REG_DST)
#ifdef BZERO
	rts
1:	mov.w	r0,@REG_DST
#else
	mov.w	r0,@REG_DST
1:	rts
	mov	REG_DST0,r0
#endif



	.align	2
large:
#ifdef BZERO
	mov	#0,REG_C
#else
	extu.b	REG_C,REG_TMP1		/* REG_C = ??????xx, REG_TMP1 = ????00xx */
	shll8	REG_C			/* REG_C = ????xx00, REG_TMP1 = ????00xx */
	or	REG_C,REG_TMP1		/* REG_C = ????xx00, REG_TMP1 = ????xxxx */
	swap.w	REG_TMP1,REG_C		/* REG_C = xxxx????, REG_TMP1 = ????xxxx */
	xtrct	REG_TMP1,REG_C		/* REG_C = xxxxxxxx */
#endif

	mov	#3,REG_TMP1
	tst	REG_TMP1,REG_DST
	mov	REG_DST,REG_PTR
	bf/s	unaligned_dst
	add	REG_LEN,REG_PTR		/* REG_PTR = dst + len; */
	tst	REG_TMP1,REG_LEN
	bf/s	unaligned_len

aligned:
	/* fill 32*n bytes */
	mov	#32,REG_TMP1
	cmp/hi	REG_LEN,REG_TMP1
	bt	9f
	.align	2
1:	sub	REG_TMP1,REG_PTR
	mov.l	REG_C,@REG_PTR
	sub	REG_TMP1,REG_LEN
	mov.l	REG_C,@(4,REG_PTR)
	cmp/hi	REG_LEN,REG_TMP1
	mov.l	REG_C,@(8,REG_PTR)
	mov.l	REG_C,@(12,REG_PTR)
	mov.l	REG_C,@(16,REG_PTR)
	mov.l	REG_C,@(20,REG_PTR)
	mov.l	REG_C,@(24,REG_PTR)
	bf/s	1b
	mov.l	REG_C,@(28,REG_PTR)
9:

	/* fill left 4*n bytes */
	cmp/eq	REG_DST,REG_PTR
	bt	9f
	add	#4,REG_DST
	cmp/eq	REG_DST,REG_PTR
1:	mov.l	REG_C,@-REG_PTR
	bt/s	9f
	cmp/eq	REG_DST,REG_PTR
	mov.l	REG_C,@-REG_PTR
	bt/s	9f
	cmp/eq	REG_DST,REG_PTR
	mov.l	REG_C,@-REG_PTR
	bt/s	9f
	cmp/eq	REG_DST,REG_PTR
	mov.l	REG_C,@-REG_PTR
	bf/s	1b
	cmp/eq	REG_DST,REG_PTR
9:
#ifdef BZERO
	rts
	nop
#else
	rts
	mov	REG_DST0,r0
#endif


unaligned_dst:
	mov	#1,REG_TMP1
	tst	REG_TMP1,REG_DST	/* if (dst & 1) {               */
	add	#1,REG_TMP1
	bt/s	2f
	tst	REG_TMP1,REG_DST
	mov.b	REG_C,@REG_DST		/*   *dst++ = c;                */
	add	#1,REG_DST
	tst	REG_TMP1,REG_DST
2:					/* }                            */
					/* if (dst & 2) {               */
	bt	4f
	mov.w	REG_C,@REG_DST		/*   *(u_int16_t*)dst++ = c;    */
	add	#2,REG_DST
4:					/* }                            */


	tst	#3,REG_PTR		/* if (ptr & 3) {               */
	bt/s	4f			/*                              */
unaligned_len:
	tst	#1,REG_PTR		/*   if (ptr & 1) {             */
	bt/s	2f
	tst	#2,REG_PTR
	mov.b	REG_C,@-REG_PTR		/*     --ptr = c;               */
2:					/*   }                          */
					/*   if (ptr & 2) {             */
	bt	4f
	mov.w	REG_C,@-REG_PTR		/*     *--(u_int16_t*)ptr = c;  */
4:					/*   }                          */
					/* }                            */

	mov	REG_PTR,REG_LEN
	bra	aligned
	sub	REG_DST,REG_LEN

