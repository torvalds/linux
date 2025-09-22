/*	$OpenBSD: in_cksum.S,v 1.2 2006/11/10 19:23:35 miod Exp $	*/
/*	$NetBSD: in_cksum.S,v 1.10 2006/04/11 23:45:13 uwe Exp $	*/

/*-
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
#include "assym.h"


#define	reg_tmp0		r0
#define	reg_byte_swapped	r1
#define	reg_mlen		r2
#define	reg_tmp3		r3
#define	reg_m			r4
#define	reg_len			r5
#define	reg_sum			r6
#define	reg_w			r7


#define	REDUCE	\
	swap.w	reg_sum,reg_tmp0	; \
	extu.w	reg_sum,reg_sum		; \
	extu.w	reg_tmp0,reg_tmp0	; \
	add	reg_tmp0,reg_sum

#define	ROL	\
	shll8	reg_sum

#ifndef __LITTLE_ENDIAN__
#define	ADDB	\
	mov.b	@reg_w+,reg_tmp0	; \
	ROL				; \
	extu.b	reg_tmp0,reg_tmp0	; \
	add	reg_tmp0,reg_sum	; \
	not	reg_byte_swapped,reg_byte_swapped
#else
#define	ADDB	\
	mov.b	@reg_w+,reg_tmp0	; \
	extu.b	reg_tmp0,reg_tmp0	; \
	add	reg_tmp0,reg_sum	; \
	ROL				; \
	not	reg_byte_swapped,reg_byte_swapped
#endif


#define	ADDS	\
	mov.w	@reg_w+,reg_tmp0	; \
	extu.w	reg_tmp0,reg_tmp0	; \
	add	reg_tmp0,reg_sum

#define	ADDCL	\
	mov.l	@reg_w+,reg_tmp0	; \
	addc	reg_tmp0,reg_sum

#define	FORWARD1	\
	add	#-1,reg_mlen

#define	FORWARD2	\
	add	#-2,reg_mlen


/*
 * LINTSTUB: include <sys/param.h>
 * LINTSTUB: include <sys/mbuf.h>
 *
 * LINTSTUB: Func: int in_cksum(struct mbuf *m, int len);
 */
ENTRY(in_cksum)
	sts.l	pr,@-sp


	mov	#0,reg_sum
	mov	#0,reg_byte_swapped


	tst	reg_len,reg_len
	bt/s	mbuf_loop_done
mbuf_loop:
	tst	reg_m,reg_m
	bt	mbuf_loop_done

	mov.l	@(M_LEN,reg_m),reg_mlen
	tst	reg_mlen,reg_mlen
	bt/s	mbuf_loop_continue
	mov.l	@(M_DATA,reg_m),reg_w


	cmp/ge	reg_mlen,reg_len
	bt	1f
	mov	reg_len,reg_mlen
1:
	sub	reg_mlen,reg_len


	mov	reg_w,reg_tmp0
	tst	#1,reg_tmp0
	bt/s	1f
	REDUCE		/* 1st instruction break only reg_tmp0(r0) */
	ADDB
	FORWARD1
1:


	mov	#1,reg_tmp0
	cmp/gt	reg_tmp0,reg_mlen
	bf/s	1f
	mov	reg_w,reg_tmp0
	tst	#2,reg_tmp0
	bt/s	1f
	REDUCE		/* 1st instruction break only reg_tmp0(r0) */
	ADDS
	FORWARD2
1:



	mov	#127,reg_tmp0
	cmp/hi	reg_tmp0,reg_mlen
	bf	1f

do_cksum128:
	bsr	cksum128
	 nop

	mov	#127,reg_tmp0
	cmp/hi	reg_tmp0,reg_mlen
	bt	do_cksum128
1:


	bsr	cksum128mod
	 nop

	REDUCE

	mov	#1,reg_tmp0
	cmp/gt	reg_tmp0,reg_mlen
	bf	1f
	ADDS
	FORWARD2
1:

	mov	reg_mlen,r0
	tst	#1,r0
	bt	1f
	ADDB
1:


mbuf_loop_continue:
	mov.l	@(M_NEXT,reg_m),reg_m

	tst	reg_len,reg_len
	bf/s	mbuf_loop
mbuf_loop_done:


	tst	reg_byte_swapped,reg_byte_swapped
	bt/s	1f
	REDUCE		/* 1st instruction break only reg_tmp0(r0) */
	ROL
1:

	REDUCE
	REDUCE

in_cksum_return:
	not	reg_sum,r0
	lds.l	@sp+,pr
	rts
	 extu.w	r0,r0


	SET_ENTRY_SIZE(in_cksum)


	.align	2
cksum128mod:
	mov	reg_mlen,reg_tmp0
	and	#124,reg_tmp0
	sub	reg_tmp0,reg_mlen

	mov.l	.L_cksum128_tail_p,reg_tmp3
	sub	reg_tmp0,reg_tmp3
	jmp	@reg_tmp3
	 clrt

	.align	2
.L_cksum128_tail_p:
	.long	cksum128_tail


	.align	2
cksum128:
	add	#-128,reg_mlen
	clrt

cksum128_unroll:
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
	ADDCL
cksum128_tail:
	mov	#0,reg_tmp0
	rts
	 addc	reg_tmp0,reg_sum
