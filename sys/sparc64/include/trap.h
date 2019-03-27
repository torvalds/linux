/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_MACHINE_TRAP_H_
#define	_MACHINE_TRAP_H_

#ifdef _KERNEL

#define	T_RESERVED			0
#define	T_INSTRUCTION_EXCEPTION		1
#define	T_INSTRUCTION_ERROR		2
#define	T_INSTRUCTION_PROTECTION	3
#define	T_ILLTRAP_INSTRUCTION		4
#define	T_ILLEGAL_INSTRUCTION		5
#define	T_PRIVILEGED_OPCODE		6
#define	T_FP_DISABLED			7
#define	T_FP_EXCEPTION_IEEE_754		8
#define	T_FP_EXCEPTION_OTHER		9
#define	T_TAG_OVERFLOW			10
#define	T_DIVISION_BY_ZERO		11
#define	T_DATA_EXCEPTION		12
#define	T_DATA_ERROR			13
#define	T_DATA_PROTECTION		14
#define	T_MEM_ADDRESS_NOT_ALIGNED	15
#define	T_PRIVILEGED_ACTION		16
#define	T_ASYNC_DATA_ERROR		17
#define	T_TRAP_INSTRUCTION_16		18
#define	T_TRAP_INSTRUCTION_17		19
#define	T_TRAP_INSTRUCTION_18		20
#define	T_TRAP_INSTRUCTION_19		21
#define	T_TRAP_INSTRUCTION_20		22
#define	T_TRAP_INSTRUCTION_21		23
#define	T_TRAP_INSTRUCTION_22		24
#define	T_TRAP_INSTRUCTION_23		25
#define	T_TRAP_INSTRUCTION_24		26
#define	T_TRAP_INSTRUCTION_25		27
#define	T_TRAP_INSTRUCTION_26		28
#define	T_TRAP_INSTRUCTION_27		29
#define	T_TRAP_INSTRUCTION_28		30
#define	T_TRAP_INSTRUCTION_29		31
#define	T_TRAP_INSTRUCTION_30		32
#define	T_TRAP_INSTRUCTION_31		33
#define	T_INSTRUCTION_MISS		34
#define	T_DATA_MISS			35

#define	T_INTERRUPT			36
#define	T_PA_WATCHPOINT			37
#define	T_VA_WATCHPOINT			38
#define	T_CORRECTED_ECC_ERROR		39
#define	T_SPILL				40
#define	T_FILL				41
#define	T_FILL_RET			42
#define	T_BREAKPOINT			43
#define	T_CLEAN_WINDOW			44
#define	T_RANGE_CHECK			45
#define	T_FIX_ALIGNMENT			46
#define	T_INTEGER_OVERFLOW		47
#define	T_SYSCALL			48
#define	T_RSTRWP_PHYS			49
#define	T_RSTRWP_VIRT			50
#define	T_KSTACK_FAULT			51

#define	T_MAX				(T_KSTACK_FAULT + 1)

#define	T_KERNEL			64

#ifndef LOCORE
void sun4u_set_traptable(void *tba_addr);
extern const char *const trap_msg[];
#endif

#endif

#endif /* !_MACHINE_TRAP_H_ */
