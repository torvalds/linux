/*	$OpenBSD: profile.h,v 1.7 2013/08/19 08:39:30 mpi Exp $ */

/*
 * Copyright (c) 1998 Dale Rahn.
 * All rights reserved.
 *
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
 */  
#define	MCOUNT \
	__asm__(" \
		.section \".text\" \n\
		.align 2 \n\
		.globl _mcount \n\
		.type	_mcount,@function \n\
	_mcount: \n\
		lwz	11, 4(1) \n\
		mflr	0 \n\
		stw	0, 4(1) \n\
		stwu	1, -48(1) \n\
		stw	3, 8(1) \n\
		stw	4, 12(1) \n\
		stw	5, 16(1) \n\
		stw	6, 20(1) \n\
		stw	7, 24(1) \n\
		stw	8, 28(1) \n\
		stw	9, 32(1) \n\
		stw	10,36(1) \n\
		stw	11,40(1) \n\
		mr	4, 0 \n\
		mr 	3, 11 \n\
		bl __mcount \n\
		lwz	3, 8(1) \n\
		lwz	4, 12(1) \n\
		lwz	5, 16(1) \n\
		lwz	6, 20(1) \n\
		lwz	7, 24(1) \n\
		lwz	8, 28(1) \n\
		lwz	9, 32(1) \n\
		lwz	10,36(1) \n\
		lwz	11,40(1) \n\
		addi	1, 1, 48 \n\
		lwz	0, 4(1) \n\
		mtlr	11 \n\
		stw	11, 4(1) \n\
		mtctr	0 \n\
		bctr \n\
	.Lfe2: \n\
		.size _mcount, .Lfe2-_mcount \n\
	");
#define _MCOUNT_DECL static void __mcount
#ifdef _KERNEL
#define MCOUNT_ENTER						\
	__asm volatile("mfmsr %0" : "=r"(s));			\
	s &= ~PSL_POW;						\
	__asm volatile("mtmsr %0" :: "r"(s & ~PSL_EE))

#define	MCOUNT_EXIT						\
	__asm volatile("mtmsr %0" :: "r"(s))
#endif /* _KERNEL */
