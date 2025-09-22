define(_rcsid,``$OpenBSD: bcopy.m4,v 1.22 2013/06/14 12:45:18 kettenis Exp $'')dnl
dnl
dnl
dnl  This is the source file for bcopy.S, spcopy.S
dnl
dnl
define(`versionmacro',substr(_rcsid,1,eval(len(_rcsid)-2)))dnl
dnl
/* This is a generated file. DO NOT EDIT. */
/*
 * Generated from:
 *
 *	versionmacro
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
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

dnl
dnl    macro: L(`arg1',`arg2')
dnl synopsis: creates an assembly label based on args resulting in $arg1.arg2
dnl
define(`L', `$$1.$2')dnl
dnl
dnl
dnl
define(`STWS',`ifelse($5, `u',dnl
`ifelse($1, `1', `vshd	$4, t`$1', r31
	stbys,B,m r31, F`'4($2, $3)',
`0', `0', `vshd	t`'decr($1), t`$1', r31
	stws,M	r31, F`'4($2, $3)')',dnl
`0', `0',
`ifelse($1, `1',
`stbys,B`'ifelse(B, `b', `,m ', `0', `0', `	')`'t`$1', F`'4($2, $3)',
`0', `0', `stws,M	t`$1', F`'4($2, $3)')')')dnl
define(`STWSS', `ifelse(`$3', `1', `dnl',
`0', `0', `STWSS($1, $2, eval($3 - 1), $4, $5)')
	STWS($3, $1, $2, $4, $5)dnl
')dnl
define(`LDWSS', `ifelse(`$3', `1', `dnl',
`0', `0', `LDWSS($1, $2, eval($3 - 1))')
	ldws,M	F`'4($1, $2), t`'$3`'dnl
')dnl
dnl
dnl copy data in 4-words blocks
dnl
define(`hppa_blcopy',`
	addi	-16, $6, $6
L($1, `loop16'`$7')
dnl	cache hint may not work on some hardware
dnl	ldw	F 32($2, $3), r0
ifelse(F, `-', `dnl
	addi	F`'4, $5, $5', `0', `0', `dnl')
LDWSS($2, $3, 4)
STWSS($4, $5, 3, `ret1', $7)
ifelse($7, `u', `dnl
	STWS(4, $4, $5, `ret1', $7)', $7, `a', `dnl')
	addib,>= -16, $6, L($1, `loop16'`$7')
ifelse($7, `a', `dnl
	STWS(4, $4, $5, `ret1', $7)dnl
', $7, `u', `dnl
	copy	t4, ret1')')dnl
dnl
dnl copy in words
dnl
define(`STWL', `addib,<,n 12, $6, L($1, cleanup)
ifelse($7, `u', `	copy	ret1, t1', $7, `a', `dnl')
L($1, word)
	ldws,M	F`'4($2, $3), t1
	addib,>= -4, $6, L($1, word)
	stws,M	t1, F`'4($4, $5)

L($1, cleanup)
	addib,=,n 4, $6, L($1, done)
	ldws	0($2, $3), t1
	add	$5, $6, $5
	b	L($1, done)
	stbys,E	t1, 0($4, $5)
')
dnl
dnl
dnl parameters:
dnl  $1	name
dnl  $2	source space
dnl  $3	source address
dnl  $4	destination space
dnl  $5	destination address
dnl  $6	length
dnl  $7	direction
dnl
define(hppa_copy,
`dnl
dnl
dnl	if direction is `-' (backwards copy), adjust src, dst
dnl
ifelse($7,`-', `add	$3, $6, $3
	add	$5, $6, $5
define(`F', `-')dnl
define(`R', `')dnl
define(`M', `mb')dnl
define(`B', `e')dnl
define(`E', `b')dnl
',dnl ifelse
`0',`0',
`define(`F', `')dnl
define(`R', `-')dnl
define(`M', `ma')dnl
define(`B', `b')dnl
define(`E', `e')dnl
')dnl ifelse

ifelse($7,`-', `', `0',`0',
`	comib,>=,n 15, $6, L($1, byte)

	extru	$3, 31, 2, t3
	extru	$5, 31, 2, t4
	add	$6, t4, $6
	comb,<> t3, t4, L($1, unaligned)
	dep	r0, 31, 2, $3
	hppa_blcopy($1, $2, $3, $4, $5, $6, `a')

	STWL($1, $2, $3, $4, $5, $6, `a')dnl

L($1, unaligned)
	sub,>=	t4, t3, t2
	ldwm	F`'4($2, $3), ret1
	zdep	t2, 28, 29, t1
	mtsar	t1
	hppa_blcopy($1, $2, $3, $4, $5, $6, `u')

dnl	STWL($1, $2, $3, $4, $5, $6, `u')
	addib,<,n 12, $6, L($1, cleanup_un)
L($1, word_un)
	ldws,M	F`'4($2, $3), t1
	vshd	ret1, t1, t2
	addib,<	-4, $6, L($1, cleanup1_un)
	stws,M	t2, F`'4($4, $5)
	ldws,M	F`'4($2, $3), ret1
	vshd	t1, ret1, t2
	addib,>= -4, $6, L($1, word_un)
	stws,M	t2, F`'4($4, $5)

L($1, cleanup_un)
	addib,<=,n 4, $6, L($1, done)
	mfctl	sar, t4
	add	$5, $6, $5
	extru	t4, 28, 2, t4
	sub,<=	$6, t4, r0
	ldws,M	F`'4($2, $3), t1
	vshd	ret1, t1, t2
	b	L($1, done)
	stbys,E	t2, 0($4, $5)

L($1, cleanup1_un)
	b	L($1, cleanup_un)
	copy	t1, ret1
')dnl ifelse

L($1, byte)
	comb,>=,n r0, $6, L($1, done)
L($1, byte_loop)
	ldbs,M	F`'1($2, $3), t1
	addib,<> -1, $6, L($1, byte_loop)
	stbs,M	t1, F`'1($4, $5)
L($1, done)
')dnl
`
#undef _LOCORE
#define _LOCORE
#include <machine/asm.h>
#include <machine/frame.h>
'
ifelse(NAME, `bcopy',
`
LEAF_ENTRY(bcopy)
	copy	arg0, ret0
	copy	arg1, arg0
	copy	ret0, arg1
ALTENTRY(memmove)
	comb,>,n arg0, arg1, L(bcopy, reverse)
ALTENTRY(memcpy)
	copy	arg0, ret0
	hppa_copy(bcopy_f, sr0, arg1, sr0, arg0, arg2, `+')
	bv	0(rp)
	nop
L(bcopy, reverse)
	copy	arg0, ret0
	hppa_copy(bcopy_r, sr0, arg1, sr0, arg0, arg2, `-')
	bv	0(rp)
	nop
EXIT(bcopy)
')dnl
dnl
ifelse(NAME, `spcopy',
`
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
`
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
'
	mtsp	arg0, sr1
	mtsp	arg2, sr2

	hppa_copy(spcopy, sr1, arg1, sr2, arg3, ret0, `+')

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
')dnl

	.end
