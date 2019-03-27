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
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/smp.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include <machine/vmm.h>

#include "vmx_cpufunc.h"
#include "ept.h"

#define	EPT_SUPPORTS_EXEC_ONLY(cap)	((cap) & (1UL << 0))
#define	EPT_PWL4(cap)			((cap) & (1UL << 6))
#define	EPT_MEMORY_TYPE_WB(cap)		((cap) & (1UL << 14))
#define	EPT_PDE_SUPERPAGE(cap)		((cap) & (1UL << 16))	/* 2MB pages */
#define	EPT_PDPTE_SUPERPAGE(cap)	((cap) & (1UL << 17))	/* 1GB pages */
#define	INVEPT_SUPPORTED(cap)		((cap) & (1UL << 20))
#define	AD_BITS_SUPPORTED(cap)		((cap) & (1UL << 21))
#define	INVVPID_SUPPORTED(cap)		((cap) & (1UL << 32))

#define	INVVPID_ALL_TYPES_MASK		0xF0000000000UL
#define	INVVPID_ALL_TYPES_SUPPORTED(cap)	\
	(((cap) & INVVPID_ALL_TYPES_MASK) == INVVPID_ALL_TYPES_MASK)

#define	INVEPT_ALL_TYPES_MASK		0x6000000UL
#define	INVEPT_ALL_TYPES_SUPPORTED(cap)		\
	(((cap) & INVEPT_ALL_TYPES_MASK) == INVEPT_ALL_TYPES_MASK)

#define	EPT_PWLEVELS		4		/* page walk levels */
#define	EPT_ENABLE_AD_BITS	(1 << 6)

SYSCTL_DECL(_hw_vmm);
SYSCTL_NODE(_hw_vmm, OID_AUTO, ept, CTLFLAG_RW, NULL, NULL);

static int ept_enable_ad_bits;

static int ept_pmap_flags;
SYSCTL_INT(_hw_vmm_ept, OID_AUTO, pmap_flags, CTLFLAG_RD,
    &ept_pmap_flags, 0, NULL);

int
ept_init(int ipinum)
{
	int use_hw_ad_bits, use_superpages, use_exec_only;
	uint64_t cap;

	cap = rdmsr(MSR_VMX_EPT_VPID_CAP);

	/*
	 * Verify that:
	 * - page walk length is 4 steps
	 * - extended page tables can be laid out in write-back memory
	 * - invvpid instruction with all possible types is supported
	 * - invept instruction with all possible types is supported
	 */
	if (!EPT_PWL4(cap) ||
	    !EPT_MEMORY_TYPE_WB(cap) ||
	    !INVVPID_SUPPORTED(cap) ||
	    !INVVPID_ALL_TYPES_SUPPORTED(cap) ||
	    !INVEPT_SUPPORTED(cap) ||
	    !INVEPT_ALL_TYPES_SUPPORTED(cap))
		return (EINVAL);

	ept_pmap_flags = ipinum & PMAP_NESTED_IPIMASK;

	use_superpages = 1;
	TUNABLE_INT_FETCH("hw.vmm.ept.use_superpages", &use_superpages);
	if (use_superpages && EPT_PDE_SUPERPAGE(cap))
		ept_pmap_flags |= PMAP_PDE_SUPERPAGE;	/* 2MB superpage */

	use_hw_ad_bits = 1;
	TUNABLE_INT_FETCH("hw.vmm.ept.use_hw_ad_bits", &use_hw_ad_bits);
	if (use_hw_ad_bits && AD_BITS_SUPPORTED(cap))
		ept_enable_ad_bits = 1;
	else
		ept_pmap_flags |= PMAP_EMULATE_AD_BITS;

	use_exec_only = 1;
	TUNABLE_INT_FETCH("hw.vmm.ept.use_exec_only", &use_exec_only);
	if (use_exec_only && EPT_SUPPORTS_EXEC_ONLY(cap))
		ept_pmap_flags |= PMAP_SUPPORTS_EXEC_ONLY;

	return (0);
}

#if 0
static void
ept_dump(uint64_t *ptp, int nlevels)
{
	int i, t, tabs;
	uint64_t *ptpnext, ptpval;

	if (--nlevels < 0)
		return;

	tabs = 3 - nlevels;
	for (t = 0; t < tabs; t++)
		printf("\t");
	printf("PTP = %p\n", ptp);

	for (i = 0; i < 512; i++) {
		ptpval = ptp[i];

		if (ptpval == 0)
			continue;
		
		for (t = 0; t < tabs; t++)
			printf("\t");
		printf("%3d 0x%016lx\n", i, ptpval);

		if (nlevels != 0 && (ptpval & EPT_PG_SUPERPAGE) == 0) {
			ptpnext = (uint64_t *)
				  PHYS_TO_DMAP(ptpval & EPT_ADDR_MASK);
			ept_dump(ptpnext, nlevels);
		}
	}
}
#endif

static void
invept_single_context(void *arg)
{
	struct invept_desc desc = *(struct invept_desc *)arg;

	invept(INVEPT_TYPE_SINGLE_CONTEXT, desc);
}

void
ept_invalidate_mappings(u_long eptp)
{
	struct invept_desc invept_desc = { 0 };

	invept_desc.eptp = eptp;

	smp_rendezvous(NULL, invept_single_context, NULL, &invept_desc);
}

static int
ept_pinit(pmap_t pmap)
{

	return (pmap_pinit_type(pmap, PT_EPT, ept_pmap_flags));
}

struct vmspace *
ept_vmspace_alloc(vm_offset_t min, vm_offset_t max)
{

	return (vmspace_alloc(min, max, ept_pinit));
}

void
ept_vmspace_free(struct vmspace *vmspace)
{

	vmspace_free(vmspace);
}

uint64_t
eptp(uint64_t pml4)
{
	uint64_t eptp_val;

	eptp_val = pml4 | (EPT_PWLEVELS - 1) << 3 | PAT_WRITE_BACK;
	if (ept_enable_ad_bits)
		eptp_val |= EPT_ENABLE_AD_BITS;

	return (eptp_val);
}
