/*	$OpenBSD: trap.h,v 1.6 2015/06/28 01:11:27 guenther Exp $	*/
/*	$NetBSD: trap.h,v 1.4 1994/10/27 04:16:30 cgd Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)trap.h	5.4 (Berkeley) 5/9/91
 */

/*
 * Trap type values
 * also known in trap.c for name strings
 */

#define	T_PRIVINFLT	 0	/* privileged instruction */
#define	T_BPTFLT	 1	/* breakpoint trap */
#define	T_ARITHTRAP	 2	/* arithmetic trap */
#define	T_RESERVED	 3	/* reserved fault base */
#define	T_PROTFLT	 4	/* protection fault */
#define	T_TRCTRAP	 5	/* trace trap */
#define	T_PAGEFLT	 6	/* page fault */
#define	T_ALIGNFLT	 7	/* alignment fault */
#define	T_DIVIDE	 8	/* integer divide fault */
#define	T_NMI		 9	/* non-maskable interrupt */
#define	T_OFLOW		10	/* overflow trap */
#define	T_BOUND		11	/* bounds check fault */
#define	T_DNA		12	/* device not available fault */
#define	T_DOUBLEFLT	13	/* double fault */
#define	T_FPOPFLT	14	/* fp coprocessor operand fetch fault (![P]Pro)*/
#define	T_TSSFLT	15	/* invalid tss fault */
#define	T_SEGNPFLT	16	/* segment not present fault */
#define	T_STKFLT	17	/* stack fault */
#define	T_MACHK		18	/* machine check ([P]Pro) */
#define	T_XFTRAP	19	/* SIMD FP exception */

/* Trap's coming from user mode */
#define	T_USER	0x100
