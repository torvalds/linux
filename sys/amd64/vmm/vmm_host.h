/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 NetApp, Inc.
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

#ifndef	_VMM_HOST_H_
#define	_VMM_HOST_H_

#ifndef	_KERNEL
#error "no user-serviceable parts inside"
#endif

struct xsave_limits {
	int		xsave_enabled;
	uint64_t	xcr0_allowed;
	uint32_t	xsave_max_size;
};

void vmm_host_state_init(void);

uint64_t vmm_get_host_pat(void);
uint64_t vmm_get_host_efer(void);
uint64_t vmm_get_host_cr0(void);
uint64_t vmm_get_host_cr4(void);
uint64_t vmm_get_host_xcr0(void);
uint64_t vmm_get_host_datasel(void);
uint64_t vmm_get_host_codesel(void);
uint64_t vmm_get_host_tsssel(void);
uint64_t vmm_get_host_fsbase(void);
uint64_t vmm_get_host_idtrbase(void);
const struct xsave_limits *vmm_get_xsave_limits(void);

/*
 * Inline access to host state that is used on every VM entry
 */
static __inline uint64_t
vmm_get_host_trbase(void)
{

	return ((uint64_t)PCPU_GET(tssp));
}

static __inline uint64_t
vmm_get_host_gdtrbase(void)
{

	return ((uint64_t)&gdt[NGDT * curcpu]);
}

struct pcpu;
extern struct pcpu __pcpu[];

static __inline uint64_t
vmm_get_host_gsbase(void)
{

	return ((uint64_t)&__pcpu[curcpu]);
}

#endif
