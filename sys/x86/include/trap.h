/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	from: @(#)trap.h	5.4 (Berkeley) 5/9/91
 * $FreeBSD$
 */

#ifndef _MACHINE_TRAP_H_
#define	_MACHINE_TRAP_H_

/*
 * Trap type values
 * also known in trap.c for name strings
 */

#define	T_PRIVINFLT	1	/* privileged instruction */
#define	T_BPTFLT	3	/* breakpoint instruction */
#define	T_ARITHTRAP	6	/* arithmetic trap */
#define	T_PROTFLT	9	/* protection fault */
#define	T_TRCTRAP	10	/* debug exception (sic) */
#define	T_PAGEFLT	12	/* page fault */
#define	T_ALIGNFLT	14	/* alignment fault */

#define	T_DIVIDE	18	/* integer divide fault */
#define	T_NMI		19	/* non-maskable trap */
#define	T_OFLOW		20	/* overflow trap */
#define	T_BOUND		21	/* bound instruction fault */
#define	T_DNA		22	/* device not available fault */
#define	T_DOUBLEFLT	23	/* double fault */
#define	T_FPOPFLT	24	/* fp coprocessor operand fetch fault */
#define	T_TSSFLT	25	/* invalid tss fault */
#define	T_SEGNPFLT	26	/* segment not present fault */
#define	T_STKFLT	27	/* stack fault */
#define	T_MCHK		28	/* machine check trap */
#define	T_XMMFLT	29	/* SIMD floating-point exception */
#define	T_RESERVED	30	/* reserved (unknown) */
#define	T_DTRACE_RET	32	/* DTrace pid return */

/* Trap's coming from user mode */
#define	T_USER	0x100

#endif /* !_MACHINE_TRAP_H_ */
