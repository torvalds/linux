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

#ifndef _X86_H_
#define	_X86_H_

#define CPUID_0000_0000 (0x0)
#define CPUID_0000_0001	(0x1)
#define CPUID_0000_0002 (0x2)
#define CPUID_0000_0003 (0x3)
#define CPUID_0000_0004 (0x4)
#define CPUID_0000_0006 (0x6)
#define CPUID_0000_0007 (0x7)
#define	CPUID_0000_000A	(0xA)
#define	CPUID_0000_000B	(0xB)
#define	CPUID_0000_000D	(0xD)
#define CPUID_8000_0000	(0x80000000)
#define CPUID_8000_0001	(0x80000001)
#define CPUID_8000_0002	(0x80000002)
#define CPUID_8000_0003	(0x80000003)
#define CPUID_8000_0004	(0x80000004)
#define CPUID_8000_0006	(0x80000006)
#define CPUID_8000_0007	(0x80000007)
#define CPUID_8000_0008	(0x80000008)
#define CPUID_8000_001D	(0x8000001D)
#define CPUID_8000_001E	(0x8000001E)

/*
 * CPUID instruction Fn0000_0001:
 */
#define CPUID_0000_0001_APICID_MASK			(0xff<<24)
#define CPUID_0000_0001_APICID_SHIFT			24

/*
 * CPUID instruction Fn0000_0001 ECX
 */
#define CPUID_0000_0001_FEAT0_VMX	(1<<5)

int x86_emulate_cpuid(struct vm *vm, int vcpu_id, uint32_t *eax, uint32_t *ebx,
		      uint32_t *ecx, uint32_t *edx);

enum vm_cpuid_capability {
	VCC_NONE,
	VCC_NO_EXECUTE,
	VCC_FFXSR,
	VCC_TCE,
	VCC_LAST
};

/*
 * Return 'true' if the capability 'cap' is enabled in this virtual cpu
 * and 'false' otherwise.
 */
bool vm_cpuid_capability(struct vm *vm, int vcpuid, enum vm_cpuid_capability);
#endif
