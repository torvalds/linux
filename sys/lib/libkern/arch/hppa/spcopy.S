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



#undef _LOCORE
#define _LOCORE
#include <machine/asm.h>
#include <machine/frame.h>


#ifdef _KERNEL
#include <assym.h>

/*
 * int spcopy (pa_space_t ssp, const void *src, pa_space_t dsp, void *dst,
 *              size_t size)
 * do a space to space bcopy.
 *
 * assumes that spaces do not clash, otherwise we lose
 */
	.import	copy_on_fault, code

LEAF_ENTRY(spcopy)
	ldw	HPPA_FRAME_ARG(4)(sp), ret0
	sub,<>	r0, ret0, r0
	bv	r0(rp)
	nop

	ldo	64(sp), sp
	stw	rp, HPPA_FRAME_CRP(sp)
	/* setup fault handler */
	mfctl	cr29, t1
	ldw	CI_CURPROC(t1), t3
	ldil	L%copy_on_fault, t2
	ldw	P_ADDR(t3), r2
	ldo	R%copy_on_fault(t2), t2
	ldw	PCB_ONFAULT+U_PCB(r2), r1
	stw	t2, PCB_ONFAULT+U_PCB(r2)

	mtsp	arg0, sr1
	mtsp	arg2, sr2

	
	comib,>=,n 15, ret0, $spcopy.byte

	extru	arg1, 31, 2, t3
	extru	arg3, 31, 2, t4
	add	ret0, t4, ret0
	comb,<> t3, t4, $spcopy.unaligned
	dep	r0, 31, 2, arg1
	
	addi	-16, ret0, ret0
$spcopy.loop16a
	ldws,ma	4(sr1, arg1), t1
	ldws,ma	4(sr1, arg1), t2
	ldws,ma	4(sr1, arg1), t3
	ldws,ma	4(sr1, arg1), t4
	stbys,b,m t1, 4(sr2, arg3)
	stws,ma	t2, 4(sr2, arg3)
	stws,ma	t3, 4(sr2, arg3)
	addib,>= -16, ret0, $spcopy.loop16a
	stws,ma	t4, 4(sr2, arg3)

	addib,<,n 12, ret0, $spcopy.cleanup
$spcopy.word
	ldws,ma	4(sr1, arg1), t1
	addib,>= -4, ret0, $spcopy.word
	stws,ma	t1, 4(sr2, arg3)

$spcopy.cleanup
	addib,=,n 4, ret0, $spcopy.done
	ldws	0(sr1, arg1), t1
	add	arg3, ret0, arg3
	b	$spcopy.done
	stbys,e	t1, 0(sr2, arg3)

$spcopy.unaligned
	sub,>=	t4, t3, t2
	ldwm	4(sr1, arg1), ret1
	zdep	t2, 28, 29, t1
	mtsar	t1
	
	addi	-16, ret0, ret0
$spcopy.loop16u
	ldws,ma	4(sr1, arg1), t1
	ldws,ma	4(sr1, arg1), t2
	ldws,ma	4(sr1, arg1), t3
	ldws,ma	4(sr1, arg1), t4
	vshd	ret1, t1, r31
	stbys,b,m r31, 4(sr2, arg3)
	vshd	t1, t2, r31
	stws,ma	r31, 4(sr2, arg3)
	vshd	t2, t3, r31
	stws,ma	r31, 4(sr2, arg3)
	vshd	t3, t4, r31
	stws,ma	r31, 4(sr2, arg3)
	addib,>= -16, ret0, $spcopy.loop16u
	copy	t4, ret1

	addib,<,n 12, ret0, $spcopy.cleanup_un
$spcopy.word_un
	ldws,ma	4(sr1, arg1), t1
	vshd	ret1, t1, t2
	addib,<	-4, ret0, $spcopy.cleanup1_un
	stws,ma	t2, 4(sr2, arg3)
	ldws,ma	4(sr1, arg1), ret1
	vshd	t1, ret1, t2
	addib,>= -4, ret0, $spcopy.word_un
	stws,ma	t2, 4(sr2, arg3)

$spcopy.cleanup_un
	addib,<=,n 4, ret0, $spcopy.done
	mfctl	sar, t4
	add	arg3, ret0, arg3
	extru	t4, 28, 2, t4
	sub,<=	ret0, t4, r0
	ldws,ma	4(sr1, arg1), t1
	vshd	ret1, t1, t2
	b	$spcopy.done
	stbys,e	t2, 0(sr2, arg3)

$spcopy.cleanup1_un
	b	$spcopy.cleanup_un
	copy	t1, ret1

$spcopy.byte
	comb,>=,n r0, ret0, $spcopy.done
$spcopy.byte_loop
	ldbs,ma	1(sr1, arg1), t1
	addib,<> -1, ret0, $spcopy.byte_loop
	stbs,ma	t1, 1(sr2, arg3)
$spcopy.done


	mtsp	r0, sr1
	mtsp	r0, sr2
	/* reset fault handler */
	stw	r1, PCB_ONFAULT+U_PCB(r2)
	ldw	HPPA_FRAME_CRP(sp), rp
	ldo	-64(sp), sp
	bv	0(rp)
	copy	r0, ret0
EXIT(spcopy)
#endif

	.end
