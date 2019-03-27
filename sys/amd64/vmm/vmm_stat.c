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
#include <sys/systm.h>
#include <sys/malloc.h>

#include <machine/vmm.h>
#include "vmm_util.h"
#include "vmm_stat.h"

/*
 * 'vst_num_elems' is the total number of addressable statistic elements
 * 'vst_num_types' is the number of unique statistic types
 *
 * It is always true that 'vst_num_elems' is greater than or equal to
 * 'vst_num_types'. This is because a stat type may represent more than
 * one element (for e.g. VMM_STAT_ARRAY).
 */
static int vst_num_elems, vst_num_types;
static struct vmm_stat_type *vsttab[MAX_VMM_STAT_ELEMS];

static MALLOC_DEFINE(M_VMM_STAT, "vmm stat", "vmm stat");

#define	vst_size	((size_t)vst_num_elems * sizeof(uint64_t))

void
vmm_stat_register(void *arg)
{
	struct vmm_stat_type *vst = arg;

	/* We require all stats to identify themselves with a description */
	if (vst->desc == NULL)
		return;

	if (vst->scope == VMM_STAT_SCOPE_INTEL && !vmm_is_intel())
		return;

	if (vst->scope == VMM_STAT_SCOPE_AMD && !vmm_is_amd())
		return;

	if (vst_num_elems + vst->nelems >= MAX_VMM_STAT_ELEMS) {
		printf("Cannot accommodate vmm stat type \"%s\"!\n", vst->desc);
		return;
	}

	vst->index = vst_num_elems;
	vst_num_elems += vst->nelems;

	vsttab[vst_num_types++] = vst;
}

int
vmm_stat_copy(struct vm *vm, int vcpu, int *num_stats, uint64_t *buf)
{
	struct vmm_stat_type *vst;
	uint64_t *stats;
	int i;

	if (vcpu < 0 || vcpu >= VM_MAXCPU)
		return (EINVAL);

	/* Let stats functions update their counters */
	for (i = 0; i < vst_num_types; i++) {
		vst = vsttab[i];
		if (vst->func != NULL)
			(*vst->func)(vm, vcpu, vst);
	}

	/* Copy over the stats */
	stats = vcpu_stats(vm, vcpu);
	for (i = 0; i < vst_num_elems; i++)
		buf[i] = stats[i];
	*num_stats = vst_num_elems;
	return (0);
}

void *
vmm_stat_alloc(void)
{

	return (malloc(vst_size, M_VMM_STAT, M_WAITOK));
}

void
vmm_stat_init(void *vp)
{

	bzero(vp, vst_size);
}

void
vmm_stat_free(void *vp)
{
	free(vp, M_VMM_STAT);
}

int
vmm_stat_desc_copy(int index, char *buf, int bufsize)
{
	int i;
	struct vmm_stat_type *vst;

	for (i = 0; i < vst_num_types; i++) {
		vst = vsttab[i];
		if (index >= vst->index && index < vst->index + vst->nelems) {
			if (vst->nelems > 1) {
				snprintf(buf, bufsize, "%s[%d]",
					 vst->desc, index - vst->index);
			} else {
				strlcpy(buf, vst->desc, bufsize);
			}
			return (0);	/* found it */
		}
	}

	return (EINVAL);
}

/* global statistics */
VMM_STAT(VCPU_MIGRATIONS, "vcpu migration across host cpus");
VMM_STAT(VMEXIT_COUNT, "total number of vm exits");
VMM_STAT(VMEXIT_EXTINT, "vm exits due to external interrupt");
VMM_STAT(VMEXIT_HLT, "number of times hlt was intercepted");
VMM_STAT(VMEXIT_CR_ACCESS, "number of times %cr access was intercepted");
VMM_STAT(VMEXIT_RDMSR, "number of times rdmsr was intercepted");
VMM_STAT(VMEXIT_WRMSR, "number of times wrmsr was intercepted");
VMM_STAT(VMEXIT_MTRAP, "number of monitor trap exits");
VMM_STAT(VMEXIT_PAUSE, "number of times pause was intercepted");
VMM_STAT(VMEXIT_INTR_WINDOW, "vm exits due to interrupt window opening");
VMM_STAT(VMEXIT_NMI_WINDOW, "vm exits due to nmi window opening");
VMM_STAT(VMEXIT_INOUT, "number of times in/out was intercepted");
VMM_STAT(VMEXIT_CPUID, "number of times cpuid was intercepted");
VMM_STAT(VMEXIT_NESTED_FAULT, "vm exits due to nested page fault");
VMM_STAT(VMEXIT_INST_EMUL, "vm exits for instruction emulation");
VMM_STAT(VMEXIT_UNKNOWN, "number of vm exits for unknown reason");
VMM_STAT(VMEXIT_ASTPENDING, "number of times astpending at exit");
VMM_STAT(VMEXIT_REQIDLE, "number of times idle requested at exit");
VMM_STAT(VMEXIT_USERSPACE, "number of vm exits handled in userspace");
VMM_STAT(VMEXIT_RENDEZVOUS, "number of times rendezvous pending at exit");
VMM_STAT(VMEXIT_EXCEPTION, "number of vm exits due to exceptions");
