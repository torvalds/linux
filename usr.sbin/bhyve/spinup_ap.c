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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>

#include <machine/vmm.h>
#include <vmmapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "bhyverun.h"
#include "spinup_ap.h"

static void
spinup_ap_realmode(struct vmctx *ctx, int newcpu, uint64_t *rip)
{
	int vector, error;
	uint16_t cs;
	uint64_t desc_base;
	uint32_t desc_limit, desc_access;

	vector = *rip >> PAGE_SHIFT;
	*rip = 0;

	/*
	 * Update the %cs and %rip of the guest so that it starts
	 * executing real mode code at at 'vector << 12'.
	 */
	error = vm_set_register(ctx, newcpu, VM_REG_GUEST_RIP, *rip);
	assert(error == 0);

	error = vm_get_desc(ctx, newcpu, VM_REG_GUEST_CS, &desc_base,
			    &desc_limit, &desc_access);
	assert(error == 0);

	desc_base = vector << PAGE_SHIFT;
	error = vm_set_desc(ctx, newcpu, VM_REG_GUEST_CS,
			    desc_base, desc_limit, desc_access);
	assert(error == 0);

	cs = (vector << PAGE_SHIFT) >> 4;
	error = vm_set_register(ctx, newcpu, VM_REG_GUEST_CS, cs);
	assert(error == 0);
}

int
spinup_ap(struct vmctx *ctx, int vcpu, int newcpu, uint64_t rip)
{
	int error;

	assert(newcpu != 0);
	assert(newcpu < guest_ncpus);

	error = vcpu_reset(ctx, newcpu);
	assert(error == 0);

	fbsdrun_set_capabilities(ctx, newcpu);

	/*
	 * Enable the 'unrestricted guest' mode for 'newcpu'.
	 *
	 * Set up the processor state in power-on 16-bit mode, with the CS:IP
	 * init'd to the specified low-mem 4K page.
	 */
	error = vm_set_capability(ctx, newcpu, VM_CAP_UNRESTRICTED_GUEST, 1);
	assert(error == 0);

	spinup_ap_realmode(ctx, newcpu, &rip);

	fbsdrun_addcpu(ctx, vcpu, newcpu, rip);

	return (newcpu);
}
