/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/libkern.h>

#include <machine/md_var.h>

#include "vmm_util.h"

boolean_t
vmm_is_intel(void)
{

	if (strcmp(cpu_vendor, "GenuineIntel") == 0)
		return (TRUE);
	else
		return (FALSE);
}

boolean_t
vmm_is_amd(void)
{
	if (strcmp(cpu_vendor, "AuthenticAMD") == 0)
		return (TRUE);
	else
		return (FALSE);
}

boolean_t
vmm_supports_1G_pages(void)
{
	unsigned int regs[4];

	/*
	 * CPUID.80000001:EDX[bit 26] = 1 indicates support for 1GB pages
	 *
	 * Both Intel and AMD support this bit.
	 */
	if (cpu_exthigh >= 0x80000001) {
		do_cpuid(0x80000001, regs);
		if (regs[3] & (1 << 26))
			return (TRUE);
	}
	return (FALSE);
}

#include <sys/proc.h>
#include <machine/frame.h>
#define	DUMP_REG(x)	printf(#x "\t\t0x%016lx\n", (long)(tf->tf_ ## x))
#define	DUMP_SEG(x)	printf(#x "\t\t0x%04x\n", (unsigned)(tf->tf_ ## x))
void
dump_trapframe(struct trapframe *tf)
{
	DUMP_REG(rdi);
	DUMP_REG(rsi);
	DUMP_REG(rdx);
	DUMP_REG(rcx);
	DUMP_REG(r8);
	DUMP_REG(r9);
	DUMP_REG(rax);
	DUMP_REG(rbx);
	DUMP_REG(rbp);
	DUMP_REG(r10);
	DUMP_REG(r11);
	DUMP_REG(r12);
	DUMP_REG(r13);
	DUMP_REG(r14);
	DUMP_REG(r15);
	DUMP_REG(trapno);
	DUMP_REG(addr);
	DUMP_REG(flags);
	DUMP_REG(err);
	DUMP_REG(rip);
	DUMP_REG(rflags);
	DUMP_REG(rsp);
	DUMP_SEG(cs);
	DUMP_SEG(ss);
	DUMP_SEG(fs);
	DUMP_SEG(gs);
	DUMP_SEG(es);
	DUMP_SEG(ds);
}
