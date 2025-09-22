/* Copyright (C) 2006 Free Software Foundation, Inc.

This file is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

This file is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* Moderately Space-optimized libgcc routines for the Renesas SH /
   STMicroelectronics ST40 CPUs.
   Contributed by J"orn Rennecke joern.rennecke@st.com.  */

#include "lib1funcs.h"

#if !__SHMEDIA__
#ifdef L_udivsi3_i4i

/* 88 bytes; sh4-200 cycle counts:
   divisor  >= 2G: 11 cycles
   dividend <  2G: 48 cycles
   dividend >= 2G: divisor != 1: 54 cycles
   dividend >= 2G, divisor == 1: 22 cycles */
#if defined (__SH_FPU_DOUBLE__) || defined (__SH4_SINGLE_ONLY__)
!! args in r4 and r5, result in r0, clobber r1

	.global GLOBAL(udivsi3_i4i)
	FUNC(GLOBAL(udivsi3_i4i))
GLOBAL(udivsi3_i4i):
	mova L1,r0
	cmp/pz r5
	sts fpscr,r1
	lds.l @r0+,fpscr
	sts.l fpul,@-r15
	bf LOCAL(huge_divisor)
	mov.l r1,@-r15
	lds r4,fpul
	cmp/pz r4
#ifdef FMOVD_WORKS
	fmov.d dr0,@-r15
	float fpul,dr0
	fmov.d dr2,@-r15
	bt LOCAL(dividend_adjusted)
	mov #1,r1
	fmov.d @r0,dr2
	cmp/eq r1,r5
	bt LOCAL(div_by_1)
	fadd dr2,dr0
LOCAL(dividend_adjusted):
	lds r5,fpul
	float fpul,dr2
	fdiv dr2,dr0
LOCAL(div_by_1):
	fmov.d @r15+,dr2
	ftrc dr0,fpul
	fmov.d @r15+,dr0
#else /* !FMOVD_WORKS */
	fmov.s DR01,@-r15
	mov #1,r1
	fmov.s DR00,@-r15
	float fpul,dr0
	fmov.s DR21,@-r15
	bt/s LOCAL(dividend_adjusted)
	fmov.s DR20,@-r15
	cmp/eq r1,r5
	bt LOCAL(div_by_1)
	fmov.s @r0+,DR20
	fmov.s @r0,DR21
	fadd dr2,dr0
LOCAL(dividend_adjusted):
	lds r5,fpul
	float fpul,dr2
	fdiv dr2,dr0
LOCAL(div_by_1):
	fmov.s @r15+,DR20
	fmov.s @r15+,DR21
	ftrc dr0,fpul
	fmov.s @r15+,DR00
	fmov.s @r15+,DR01
#endif /* !FMOVD_WORKS */
	lds.l @r15+,fpscr
	sts fpul,r0
	rts
	lds.l @r15+,fpul

#ifdef FMOVD_WORKS
	.p2align 3        ! make double below 8 byte aligned.
#endif
LOCAL(huge_divisor):
	lds r1,fpscr
	add #4,r15
	cmp/hs r5,r4
	rts
	movt r0

	.p2align 2
L1:
#ifndef FMOVD_WORKS
	.long 0x80000
#else
	.long 0x180000
#endif
	.double 4294967296

	ENDFUNC(GLOBAL(udivsi3_i4i))
#elif !defined (__sh1__)  /* !__SH_FPU_DOUBLE__ */

#if 0
/* With 36 bytes, the following would probably be the most compact
   implementation, but with 139 cycles on an sh4-200, it is extremely slow.  */
GLOBAL(udivsi3_i4i):
	mov.l r2,@-r15
	mov #0,r1
	div0u
	mov r1,r2
	mov.l r3,@-r15
	mov r1,r3
	sett
	mov r4,r0
LOCAL(loop):
	rotcr r2
	;
	bt/s LOCAL(end)
	cmp/gt r2,r3
	rotcl r0
	bra LOCAL(loop)
	div1 r5,r1
LOCAL(end):
	rotcl r0
	mov.l @r15+,r3
	rts
	mov.l @r15+,r2
#endif /* 0 */

/* Size: 186 bytes jointly for udivsi3_i4i and sdivsi3_i4i
   sh4-200 run times:
   udiv small divisor: 55 cycles
   udiv large divisor: 52 cycles
   sdiv small divisor, positive result: 59 cycles
   sdiv large divisor, positive result: 56 cycles
   sdiv small divisor, negative result: 65 cycles (*)
   sdiv large divisor, negative result: 62 cycles (*)
   (*): r2 is restored in the rts delay slot and has a lingering latency
        of two more cycles.  */
	.balign 4
	.global	GLOBAL(udivsi3_i4i)
	FUNC(GLOBAL(udivsi3_i4i))
	FUNC(GLOBAL(sdivsi3_i4i))
GLOBAL(udivsi3_i4i):
	sts pr,r1
	mov.l r4,@-r15
	extu.w r5,r0
	cmp/eq r5,r0
	swap.w r4,r0
	shlr16 r4
	bf/s LOCAL(large_divisor)
	div0u
	mov.l r5,@-r15
	shll16 r5
LOCAL(sdiv_small_divisor):
	div1 r5,r4
	bsr LOCAL(div6)
	div1 r5,r4
	div1 r5,r4
	bsr LOCAL(div6)
	div1 r5,r4
	xtrct r4,r0
	xtrct r0,r4
	bsr LOCAL(div7)
	swap.w r4,r4
	div1 r5,r4
	bsr LOCAL(div7)
	div1 r5,r4
	xtrct r4,r0
	mov.l @r15+,r5
	swap.w r0,r0
	mov.l @r15+,r4
	jmp @r1
	rotcl r0
LOCAL(div7):
	div1 r5,r4
LOCAL(div6):
	            div1 r5,r4; div1 r5,r4; div1 r5,r4
	div1 r5,r4; div1 r5,r4; rts;        div1 r5,r4

LOCAL(divx3):
	rotcl r0
	div1 r5,r4
	rotcl r0
	div1 r5,r4
	rotcl r0
	rts
	div1 r5,r4

LOCAL(large_divisor):
	mov.l r5,@-r15
LOCAL(sdiv_large_divisor):
	xor r4,r0
	.rept 4
	rotcl r0
	bsr LOCAL(divx3)
	div1 r5,r4
	.endr
	mov.l @r15+,r5
	mov.l @r15+,r4
	jmp @r1
	rotcl r0
	ENDFUNC(GLOBAL(udivsi3_i4i))

	.global	GLOBAL(sdivsi3_i4i)
GLOBAL(sdivsi3_i4i):
	mov.l r4,@-r15
	cmp/pz r5
	mov.l r5,@-r15
	bt/s LOCAL(pos_divisor)
	cmp/pz r4
	neg r5,r5
	extu.w r5,r0
	bt/s LOCAL(neg_result)
	cmp/eq r5,r0
	neg r4,r4
LOCAL(pos_result):
	swap.w r4,r0
	bra LOCAL(sdiv_check_divisor)
	sts pr,r1
LOCAL(pos_divisor):
	extu.w r5,r0
	bt/s LOCAL(pos_result)
	cmp/eq r5,r0
	neg r4,r4
LOCAL(neg_result):
	mova LOCAL(negate_result),r0
	;
	mov r0,r1
	swap.w r4,r0
	lds r2,macl
	sts pr,r2
LOCAL(sdiv_check_divisor):
	shlr16 r4
	bf/s LOCAL(sdiv_large_divisor)
	div0u
	bra LOCAL(sdiv_small_divisor)
	shll16 r5
	.balign 4
LOCAL(negate_result):
	neg r0,r0
	jmp @r2
	sts macl,r2
	ENDFUNC(GLOBAL(sdivsi3_i4i))
#endif /* !__SH_FPU_DOUBLE__ */
#endif /* L_udivsi3_i4i */

#ifdef L_sdivsi3_i4i
#if defined (__SH_FPU_DOUBLE__) || defined (__SH4_SINGLE_ONLY__)
/* 48 bytes, 45 cycles on sh4-200  */
!! args in r4 and r5, result in r0, clobber r1

	.global GLOBAL(sdivsi3_i4i)
	FUNC(GLOBAL(sdivsi3_i4i))
GLOBAL(sdivsi3_i4i):
	sts.l fpscr,@-r15
	sts fpul,r1
	mova L1,r0
	lds.l @r0+,fpscr
	lds r4,fpul
#ifdef FMOVD_WORKS
	fmov.d dr0,@-r15
	float fpul,dr0
	lds r5,fpul
	fmov.d dr2,@-r15
#else
	fmov.s DR01,@-r15
	fmov.s DR00,@-r15
	float fpul,dr0
	lds r5,fpul
	fmov.s DR21,@-r15
	fmov.s DR20,@-r15
#endif
	float fpul,dr2
	fdiv dr2,dr0
#ifdef FMOVD_WORKS
	fmov.d @r15+,dr2
#else
	fmov.s @r15+,DR20
	fmov.s @r15+,DR21
#endif
	ftrc dr0,fpul
#ifdef FMOVD_WORKS
	fmov.d @r15+,dr0
#else
	fmov.s @r15+,DR00
	fmov.s @r15+,DR01
#endif
	lds.l @r15+,fpscr
	sts fpul,r0
	rts
	lds r1,fpul

	.p2align 2
L1:
#ifndef FMOVD_WORKS
	.long 0x80000
#else
	.long 0x180000
#endif

	ENDFUNC(GLOBAL(sdivsi3_i4i))
#endif /* __SH_FPU_DOUBLE__ */
#endif /* L_sdivsi3_i4i */
#endif /* !__SHMEDIA__ */
