/*-
 * Copyright (c) 1989, 1990 William F. Jolitz.
 * Copyright (c) 1990 The Regents of the University of California.
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
 *	from: vector.s, 386BSD 0.1 unknown origin
 * $FreeBSD$
 */

/*
 * Interrupt entry points for external interrupts triggered by the 8259A
 * master and slave interrupt controllers.
 */

#include "assym.inc"
#include <machine/asmacros.h>

/*
 * Macros for interrupt entry, call to handler, and exit.
 */
	.macro	INTR	irq_num, vec_name
	INTR_HANDLER	\vec_name
	FAKE_MCOUNT(TF_RIP(%rsp))
	movq	%rsp, %rsi
	movl	$\irq_num, %edi	 	/* pass the IRQ */
	call	atpic_handle_intr
	MEXITCOUNT
	jmp	doreti
	.endm

	INTR	0, atpic_intr0
	INTR	1, atpic_intr1
	INTR	2, atpic_intr2
	INTR	3, atpic_intr3
	INTR	4, atpic_intr4
	INTR	5, atpic_intr5
	INTR	6, atpic_intr6
	INTR	7, atpic_intr7
	INTR	8, atpic_intr8
	INTR	9, atpic_intr9
	INTR	10, atpic_intr10
	INTR	11, atpic_intr11
	INTR	12, atpic_intr12
	INTR	13, atpic_intr13
	INTR	14, atpic_intr14
	INTR	15, atpic_intr15
