/* This is a generated file. DO NOT EDIT. */
/*
 * Generated from:
 *
 *	OpenBSD: bcopy.m4
 */
/*
 * Copyright (c) 1999 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


#include "SYS.h"


LEAF_ENTRY(bcopy)
	copy	arg0, ret0
	copy	arg1, arg0
	copy	ret0, arg1
ALTENTRY(memmove)
	comb,>,n arg0, arg1, $bcopy.reverse
// ALTENTRY(memcpy)
	copy	arg0, ret0
	
	comib,>=,n 15, arg2, $bcopy_f.byte

	extru	arg1, 31, 2, t3
	extru	arg0, 31, 2, t4
	add	arg2, t4, arg2
	comb,<> t3, t4, $bcopy_f.unaligned
	dep	r0, 31, 2, arg1
	
	addi	-16, arg2, arg2
$bcopy_f.loop16a
	ldws,ma	4(sr0, arg1), t1
	ldws,ma	4(sr0, arg1), t2
	ldws,ma	4(sr0, arg1), t3
	ldws,ma	4(sr0, arg1), t4
	stbys,b,m t1, 4(sr0, arg0)
	stws,ma	t2, 4(sr0, arg0)
	stws,ma	t3, 4(sr0, arg0)
	addib,>= -16, arg2, $bcopy_f.loop16a
	stws,ma	t4, 4(sr0, arg0)

	addib,<,n 12, arg2, $bcopy_f.cleanup
$bcopy_f.word
	ldws,ma	4(sr0, arg1), t1
	addib,>= -4, arg2, $bcopy_f.word
	stws,ma	t1, 4(sr0, arg0)

$bcopy_f.cleanup
	addib,=,n 4, arg2, $bcopy_f.done
	ldws	0(sr0, arg1), t1
	add	arg0, arg2, arg0
	b	$bcopy_f.done
	stbys,e	t1, 0(sr0, arg0)

$bcopy_f.unaligned
	sub,>=	t4, t3, t2
	ldwm	4(sr0, arg1), ret1
	zdep	t2, 28, 29, t1
	mtsar	t1
	
	addi	-16, arg2, arg2
$bcopy_f.loop16u
	ldws,ma	4(sr0, arg1), t1
	ldws,ma	4(sr0, arg1), t2
	ldws,ma	4(sr0, arg1), t3
	ldws,ma	4(sr0, arg1), t4
	vshd	ret1, t1, r31
	stbys,b,m r31, 4(sr0, arg0)
	vshd	t1, t2, r31
	stws,ma	r31, 4(sr0, arg0)
	vshd	t2, t3, r31
	stws,ma	r31, 4(sr0, arg0)
	vshd	t3, t4, r31
	stws,ma	r31, 4(sr0, arg0)
	addib,>= -16, arg2, $bcopy_f.loop16u
	copy	t4, ret1

	addib,<,n 12, arg2, $bcopy_f.cleanup_un
$bcopy_f.word_un
	ldws,ma	4(sr0, arg1), t1
	vshd	ret1, t1, t2
	addib,<	-4, arg2, $bcopy_f.cleanup1_un
	stws,ma	t2, 4(sr0, arg0)
	ldws,ma	4(sr0, arg1), ret1
	vshd	t1, ret1, t2
	addib,>= -4, arg2, $bcopy_f.word_un
	stws,ma	t2, 4(sr0, arg0)

$bcopy_f.cleanup_un
	addib,<=,n 4, arg2, $bcopy_f.done
	mfctl	sar, t4
	add	arg0, arg2, arg0
	extru	t4, 28, 2, t4
	sub,<=	arg2, t4, r0
	ldws,ma	4(sr0, arg1), t1
	vshd	ret1, t1, t2
	b	$bcopy_f.done
	stbys,e	t2, 0(sr0, arg0)

$bcopy_f.cleanup1_un
	b	$bcopy_f.cleanup_un
	copy	t1, ret1

$bcopy_f.byte
	comb,>=,n r0, arg2, $bcopy_f.done
$bcopy_f.byte_loop
	ldbs,ma	1(sr0, arg1), t1
	addib,<> -1, arg2, $bcopy_f.byte_loop
	stbs,ma	t1, 1(sr0, arg0)
$bcopy_f.done

	bv	0(rp)
	nop
$bcopy.reverse
	copy	arg0, ret0
	add	arg1, arg2, arg1
	add	arg0, arg2, arg0


$bcopy_r.byte
	comb,>=,n r0, arg2, $bcopy_r.done
$bcopy_r.byte_loop
	ldbs,mb	-1(sr0, arg1), t1
	addib,<> -1, arg2, $bcopy_r.byte_loop
	stbs,mb	t1, -1(sr0, arg0)
$bcopy_r.done

	bv	0(rp)
	nop
END_WEAK(bcopy)
ALTEND_STRONG(memmove)

	.end
