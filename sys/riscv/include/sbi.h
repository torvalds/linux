/*-
 * Copyright (c) 2016-2017 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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

#ifndef _MACHINE_SBI_H_
#define	_MACHINE_SBI_H_

#define	SBI_SET_TIMER			0
#define	SBI_CONSOLE_PUTCHAR		1
#define	SBI_CONSOLE_GETCHAR		2
#define	SBI_CLEAR_IPI			3
#define	SBI_SEND_IPI			4
#define	SBI_REMOTE_FENCE_I		5
#define	SBI_REMOTE_SFENCE_VMA		6
#define	SBI_REMOTE_SFENCE_VMA_ASID	7
#define	SBI_SHUTDOWN			8

/*
 * Documentation available at
 * https://github.com/riscv/riscv-sbi-doc/blob/master/riscv-sbi.md
 */

static __inline uint64_t
sbi_call(uint64_t arg7, uint64_t arg0, uint64_t arg1, uint64_t arg2,
    uint64_t arg3)
{
	register uintptr_t a0 __asm ("a0") = (uintptr_t)(arg0);
	register uintptr_t a1 __asm ("a1") = (uintptr_t)(arg1);
	register uintptr_t a2 __asm ("a2") = (uintptr_t)(arg2);
	register uintptr_t a3 __asm ("a3") = (uintptr_t)(arg3);
	register uintptr_t a7 __asm ("a7") = (uintptr_t)(arg7);

	__asm __volatile(			\
		"ecall"				\
		:"+r"(a0)			\
		:"r"(a1), "r"(a2), "r" (a3), "r"(a7)	\
		:"memory");

	return (a0);
}

static __inline void
sbi_console_putchar(int ch)
{

	sbi_call(SBI_CONSOLE_PUTCHAR, ch, 0, 0, 0);
}

static __inline int
sbi_console_getchar(void)
{

	return (sbi_call(SBI_CONSOLE_GETCHAR, 0, 0, 0, 0));
}

static __inline void
sbi_set_timer(uint64_t val)
{

	sbi_call(SBI_SET_TIMER, val, 0, 0, 0);
}

static __inline void
sbi_shutdown(void)
{

	sbi_call(SBI_SHUTDOWN, 0, 0, 0, 0);
}

static __inline void
sbi_clear_ipi(void)
{

	sbi_call(SBI_CLEAR_IPI, 0, 0, 0, 0);
}

static __inline void
sbi_send_ipi(const unsigned long *hart_mask)
{

	sbi_call(SBI_SEND_IPI, (uint64_t)hart_mask, 0, 0, 0);
}

static __inline void
sbi_remote_fence_i(const unsigned long *hart_mask)
{

	sbi_call(SBI_REMOTE_FENCE_I, (uint64_t)hart_mask, 0, 0, 0);
}

static __inline void
sbi_remote_sfence_vma(const unsigned long *hart_mask,
    unsigned long start, unsigned long size)
{

	sbi_call(SBI_REMOTE_SFENCE_VMA, (uint64_t)hart_mask, start, size, 0);
}

static __inline void
sbi_remote_sfence_vma_asid(const unsigned long *hart_mask,
    unsigned long start, unsigned long size,
    unsigned long asid)
{

	sbi_call(SBI_REMOTE_SFENCE_VMA_ASID, (uint64_t)hart_mask, start, size,
	    asid);
}

#endif /* !_MACHINE_SBI_H_ */
