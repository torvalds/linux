/*-
 * Copyright (c) 2011-2014 Jung-uk Kim <jkim@FreeBSD.org>
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

#ifndef _X86_VMWARE_H_
#define	_X86_VMWARE_H_

#define	VMW_HVMAGIC		0x564d5868
#define	VMW_HVPORT		0x5658

#define	VMW_HVCMD_GETVERSION	10
#define	VMW_HVCMD_GETHZ		45
#define	VMW_HVCMD_GETVCPU_INFO	68

#define	VMW_VCPUINFO_LEGACY_X2APIC	(1 << 3)
#define	VMW_VCPUINFO_VCPU_RESERVED	(1 << 31)

static __inline void
vmware_hvcall(u_int cmd, u_int *p)
{

	__asm __volatile("inl %w3, %0"
	: "=a" (p[0]), "=b" (p[1]), "=c" (p[2]), "=d" (p[3])
	: "0" (VMW_HVMAGIC), "1" (UINT_MAX), "2" (cmd), "3" (VMW_HVPORT)
	: "memory");
}

#endif /* !_X86_VMWARE_H_ */
