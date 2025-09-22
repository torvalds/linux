/* Library functions.
   Copyright (C) 2000, 2003 Free Software Foundation, Inc.
   Contributed by Red Hat, Inc.
  
   This file is part of GCC.
  
   GCC is free software ; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation * either version 2, or (at your option)
   any later version.
  
   GCC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY ; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
  
   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.  */

#include <frv-asm.h>


#ifdef L_cmpll
/* icc0 = __cmpll (long long a, long long b)  */

	.file	"_cmpll.s"
	.globl	EXT(__cmpll)
	.type	EXT(__cmpll),@function
	.text
	.p2align 4
EXT(__cmpll):
	cmp	gr8, gr10, icc0
	ckeq	icc0, cc4
	P(ccmp)	gr9, gr11, cc4, 1
	ret
.Lend:
	.size	EXT(__cmpll),.Lend-EXT(__cmpll)
#endif /* L_cmpll */

#ifdef L_cmpf
/* icc0 = __cmpf (float a, float b) */
/* Note, because this function returns the result in ICC0, it means it can't
   handle NaNs.  */

	.file	"_cmpf.s"
	.globl	EXT(__cmpf)
	.type	EXT(__cmpf),@function
	.text
	.p2align 4
EXT(__cmpf):
#ifdef __FRV_HARD_FLOAT__	/* floating point instructions available */
	movgf	gr8, fr0
	P(movgf) gr9, fr1
	setlos	#1, gr8
	fcmps	fr0, fr1, fcc0
	P(fcklt) fcc0, cc0
	fckeq	fcc0, cc1
	csub	gr0, gr8, gr8, cc0, 1
	cmov	gr0, gr8, cc1, 1
	cmpi	gr8, 0, icc0
	ret
#else				/* no floating point instructions available */
	movsg	lr, gr4
	addi	sp, #-16, sp
	sti	gr4, @(sp, 8)
	st	fp, @(sp, gr0)
	mov	sp, fp
	call	EXT(__cmpsf2)
	cmpi	gr8, #0, icc0
	ldi	@(sp, 8), gr4
	movgs	gr4, lr
	ld	@(sp,gr0), fp
	addi	sp, #16, sp
	ret
#endif
.Lend:
	.size	EXT(__cmpf),.Lend-EXT(__cmpf)
#endif

#ifdef L_cmpd
/* icc0 = __cmpd (double a, double b) */
/* Note, because this function returns the result in ICC0, it means it can't
   handle NaNs.  */

	.file	"_cmpd.s"
	.globl	EXT(__cmpd)
	.type	EXT(__cmpd),@function
	.text
	.p2align 4
EXT(__cmpd):
	movsg	lr, gr4
	addi	sp, #-16, sp
	sti	gr4, @(sp, 8)
	st	fp, @(sp, gr0)
	mov	sp, fp
	call	EXT(__cmpdf2)
	cmpi	gr8, #0, icc0
	ldi	@(sp, 8), gr4
	movgs	gr4, lr
	ld	@(sp,gr0), fp
	addi	sp, #16, sp
	ret
.Lend:
	.size	EXT(__cmpd),.Lend-EXT(__cmpd)
#endif

#ifdef L_addll
/* gr8,gr9 = __addll (long long a, long long b) */
/* Note, gcc will never call this function, but it is present in case an
   ABI program calls it.  */

	.file	"_addll.s"
	.globl	EXT(__addll)
	.type	EXT(__addll),@function
	.text
	.p2align
EXT(__addll):
	addcc	gr9, gr11, gr9, icc0
	addx	gr8, gr10, gr8, icc0
	ret
.Lend:
	.size	EXT(__addll),.Lend-EXT(__addll)
#endif

#ifdef L_subll
/* gr8,gr9 = __subll (long long a, long long b) */
/* Note, gcc will never call this function, but it is present in case an
   ABI program calls it.  */

	.file	"_subll.s"
	.globl	EXT(__subll)
	.type	EXT(__subll),@function
	.text
	.p2align 4
EXT(__subll):
	subcc	gr9, gr11, gr9, icc0
	subx	gr8, gr10, gr8, icc0
	ret
.Lend:
	.size	EXT(__subll),.Lend-EXT(__subll)
#endif

#ifdef L_andll
/* gr8,gr9 = __andll (long long a, long long b) */
/* Note, gcc will never call this function, but it is present in case an
   ABI program calls it.  */

	.file	"_andll.s"
	.globl	EXT(__andll)
	.type	EXT(__andll),@function
	.text
	.p2align 4
EXT(__andll):
	P(and)	gr9, gr11, gr9
	P2(and)	gr8, gr10, gr8
	ret
.Lend:
	.size	EXT(__andll),.Lend-EXT(__andll)
#endif

#ifdef L_orll
/* gr8,gr9 = __orll (long long a, long long b) */
/* Note, gcc will never call this function, but it is present in case an
   ABI program calls it.  */

	.file	"_orll.s"
	.globl	EXT(__orll)
	.type	EXT(__orll),@function
	.text
	.p2align 4
EXT(__orll):
	P(or)	gr9, gr11, gr9
	P2(or)	gr8, gr10, gr8
	ret
.Lend:
	.size	EXT(__orll),.Lend-EXT(__orll)
#endif

#ifdef L_xorll
/* gr8,gr9 = __xorll (long long a, long long b) */
/* Note, gcc will never call this function, but it is present in case an
   ABI program calls it.  */

	.file	"_xorll.s"
	.globl	EXT(__xorll)
	.type	EXT(__xorll),@function
	.text
	.p2align 4
EXT(__xorll):
	P(xor)	gr9, gr11, gr9
	P2(xor)	gr8, gr10, gr8
	ret
.Lend:
	.size	EXT(__xorll),.Lend-EXT(__xorll)
#endif

#ifdef L_notll
/* gr8,gr9 = __notll (long long a) */
/* Note, gcc will never call this function, but it is present in case an
   ABI program calls it.  */

	.file	"_notll.s"
	.globl	EXT(__notll)
	.type	EXT(__notll),@function
	.text
	.p2align 4
EXT(__notll):
	P(not)	gr9, gr9
	P2(not)	gr8, gr8
	ret
.Lend:
	.size	EXT(__notll),.Lend-EXT(__notll)
#endif

#ifdef L_cmov
/* (void) __cmov (char *dest, const char *src, size_t len) */
/*
 * void __cmov (char *dest, const char *src, size_t len)
 * {
 *   size_t i;
 * 
 *   if (dest < src || dest > src+len)
 *     {
 *	 for (i = 0; i < len; i++)
 *	 dest[i] = src[i];
 *     }
 *   else
 *     {
 *	 while (len-- > 0)
 *	 dest[len] = src[len];
 *     }
 * }
 */

	.file	"_cmov.s"
	.globl	EXT(__cmov)
	.type	EXT(__cmov),@function
	.text
	.p2align 4
EXT(__cmov):
	P(cmp)	gr8, gr9, icc0
	add	gr9, gr10, gr4
	P(cmp)	gr8, gr4, icc1
	bc	icc0, 0, .Lfwd
	bls	icc1, 0, .Lback
.Lfwd:
	/* move bytes in a forward direction */
	P(setlos) #0, gr5
	cmp	gr0, gr10, icc0
	P(subi)	gr9, #1, gr9
	P2(subi) gr8, #1, gr8
	bnc	icc0, 0, .Lret
.Lfloop:
	/* forward byte move loop */
	addi	gr5, #1, gr5
	P(ldsb)	@(gr9, gr5), gr4
	cmp	gr5, gr10, icc0
	P(stb)	gr4, @(gr8, gr5)
	bc	icc0, 0, .Lfloop
	ret
.Lbloop:
	/* backward byte move loop body */
	ldsb	@(gr9,gr10),gr4
	stb	gr4,@(gr8,gr10)
.Lback:
	P(cmpi)	gr10, #0, icc0
	addi	gr10, #-1, gr10
	bne	icc0, 0, .Lbloop
.Lret:
	ret
.Lend:
	.size	 EXT(__cmov),.Lend-EXT(__cmov)
#endif
