/*-
 * SPDX-License-Identifier: BSD-3-Clause
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

#ifndef _VMM_STAT_H_
#define	_VMM_STAT_H_

struct vm;

#define	MAX_VMM_STAT_ELEMS	64		/* arbitrary */

enum vmm_stat_scope {
	VMM_STAT_SCOPE_ANY,
	VMM_STAT_SCOPE_INTEL,		/* Intel VMX specific statistic */
	VMM_STAT_SCOPE_AMD,		/* AMD SVM specific statistic */
};

struct vmm_stat_type;
typedef void (*vmm_stat_func_t)(struct vm *vm, int vcpu,
    struct vmm_stat_type *stat);

struct vmm_stat_type {
	int	index;			/* position in the stats buffer */
	int	nelems;			/* standalone or array */
	const char *desc;		/* description of statistic */
	vmm_stat_func_t func;
	enum vmm_stat_scope scope;
};

void	vmm_stat_register(void *arg);

#define	VMM_STAT_FDEFINE(type, nelems, desc, func, scope)		\
	struct vmm_stat_type type[1] = {				\
		{ -1, nelems, desc, func, scope }			\
	};								\
	SYSINIT(type##_stat, SI_SUB_KLD, SI_ORDER_ANY, vmm_stat_register, type)

#define VMM_STAT_DEFINE(type, nelems, desc, scope) 			\
	VMM_STAT_FDEFINE(type, nelems, desc, NULL, scope)

#define	VMM_STAT_DECLARE(type)						\
	extern struct vmm_stat_type type[1]

#define	VMM_STAT(type, desc)		\
	VMM_STAT_DEFINE(type, 1, desc, VMM_STAT_SCOPE_ANY)
#define	VMM_STAT_INTEL(type, desc)	\
	VMM_STAT_DEFINE(type, 1, desc, VMM_STAT_SCOPE_INTEL)
#define	VMM_STAT_AMD(type, desc)	\
	VMM_STAT_DEFINE(type, 1, desc, VMM_STAT_SCOPE_AMD)

#define	VMM_STAT_FUNC(type, desc, func)	\
	VMM_STAT_FDEFINE(type, 1, desc, func, VMM_STAT_SCOPE_ANY)

#define	VMM_STAT_ARRAY(type, nelems, desc)	\
	VMM_STAT_DEFINE(type, nelems, desc, VMM_STAT_SCOPE_ANY)

void	*vmm_stat_alloc(void);
void	vmm_stat_init(void *vp);
void 	vmm_stat_free(void *vp);

/*
 * 'buf' should be at least fit 'MAX_VMM_STAT_TYPES' entries
 */
int	vmm_stat_copy(struct vm *vm, int vcpu, int *num_stats, uint64_t *buf);
int	vmm_stat_desc_copy(int index, char *buf, int buflen);

static void __inline
vmm_stat_array_incr(struct vm *vm, int vcpu, struct vmm_stat_type *vst,
		    int statidx, uint64_t x)
{
#ifdef VMM_KEEP_STATS
	uint64_t *stats;
	
	stats = vcpu_stats(vm, vcpu);

	if (vst->index >= 0 && statidx < vst->nelems)
		stats[vst->index + statidx] += x;
#endif
}

static void __inline
vmm_stat_array_set(struct vm *vm, int vcpu, struct vmm_stat_type *vst,
		   int statidx, uint64_t val)
{
#ifdef VMM_KEEP_STATS
	uint64_t *stats;
	
	stats = vcpu_stats(vm, vcpu);

	if (vst->index >= 0 && statidx < vst->nelems)
		stats[vst->index + statidx] = val;
#endif
}
		   
static void __inline
vmm_stat_incr(struct vm *vm, int vcpu, struct vmm_stat_type *vst, uint64_t x)
{

#ifdef VMM_KEEP_STATS
	vmm_stat_array_incr(vm, vcpu, vst, 0, x);
#endif
}

static void __inline
vmm_stat_set(struct vm *vm, int vcpu, struct vmm_stat_type *vst, uint64_t val)
{

#ifdef VMM_KEEP_STATS
	vmm_stat_array_set(vm, vcpu, vst, 0, val);
#endif
}

VMM_STAT_DECLARE(VCPU_MIGRATIONS);
VMM_STAT_DECLARE(VMEXIT_COUNT);
VMM_STAT_DECLARE(VMEXIT_EXTINT);
VMM_STAT_DECLARE(VMEXIT_HLT);
VMM_STAT_DECLARE(VMEXIT_CR_ACCESS);
VMM_STAT_DECLARE(VMEXIT_RDMSR);
VMM_STAT_DECLARE(VMEXIT_WRMSR);
VMM_STAT_DECLARE(VMEXIT_MTRAP);
VMM_STAT_DECLARE(VMEXIT_PAUSE);
VMM_STAT_DECLARE(VMEXIT_INTR_WINDOW);
VMM_STAT_DECLARE(VMEXIT_NMI_WINDOW);
VMM_STAT_DECLARE(VMEXIT_INOUT);
VMM_STAT_DECLARE(VMEXIT_CPUID);
VMM_STAT_DECLARE(VMEXIT_NESTED_FAULT);
VMM_STAT_DECLARE(VMEXIT_INST_EMUL);
VMM_STAT_DECLARE(VMEXIT_UNKNOWN);
VMM_STAT_DECLARE(VMEXIT_ASTPENDING);
VMM_STAT_DECLARE(VMEXIT_USERSPACE);
VMM_STAT_DECLARE(VMEXIT_RENDEZVOUS);
VMM_STAT_DECLARE(VMEXIT_EXCEPTION);
VMM_STAT_DECLARE(VMEXIT_REQIDLE);
#endif
