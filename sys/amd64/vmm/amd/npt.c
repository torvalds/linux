/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Anish Gupta (akgupt3@gmail.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include "npt.h"

SYSCTL_DECL(_hw_vmm);
SYSCTL_NODE(_hw_vmm, OID_AUTO, npt, CTLFLAG_RW, NULL, NULL);

static int npt_flags;
SYSCTL_INT(_hw_vmm_npt, OID_AUTO, pmap_flags, CTLFLAG_RD,
	&npt_flags, 0, NULL);

#define NPT_IPIMASK	0xFF

/*
 * AMD nested page table init.
 */
int
svm_npt_init(int ipinum)
{
	int enable_superpage = 1;

	npt_flags = ipinum & NPT_IPIMASK;
	TUNABLE_INT_FETCH("hw.vmm.npt.enable_superpage", &enable_superpage);
	if (enable_superpage)
		npt_flags |= PMAP_PDE_SUPERPAGE; 
	
	return (0);
}

static int
npt_pinit(pmap_t pmap)
{

	return (pmap_pinit_type(pmap, PT_RVI, npt_flags));
}

struct vmspace *
svm_npt_alloc(vm_offset_t min, vm_offset_t max)
{
	
	return (vmspace_alloc(min, max, npt_pinit));
}

void
svm_npt_free(struct vmspace *vmspace)
{

	vmspace_free(vmspace);
}
