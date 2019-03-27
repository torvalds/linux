/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001 Wind River Systems, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Copyright (c) 2009 Jeffrey Roberson <jeff@freebsd.org>
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 */

/*
 * This module provides MI support for per-cpu data.
 *
 * Each architecture determines the mapping of logical CPU IDs to physical
 * CPUs.  The requirements of this mapping are as follows:
 *  - Logical CPU IDs must reside in the range 0 ... MAXCPU - 1.
 *  - The mapping is not required to be dense.  That is, there may be
 *    gaps in the mappings.
 *  - The platform sets the value of MAXCPU in <machine/param.h>.
 *  - It is suggested, but not required, that in the non-SMP case, the
 *    platform define MAXCPU to be 1 and define the logical ID of the
 *    sole CPU as 0.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/smp.h>
#include <sys/sx.h>
#include <vm/uma.h>
#include <ddb/ddb.h>

static MALLOC_DEFINE(M_PCPU, "Per-cpu", "Per-cpu resource accouting.");

struct dpcpu_free {
	uintptr_t	df_start;
	int		df_len;
	TAILQ_ENTRY(dpcpu_free) df_link;
};

DPCPU_DEFINE_STATIC(char, modspace[DPCPU_MODMIN] __aligned(__alignof(void *)));
static TAILQ_HEAD(, dpcpu_free) dpcpu_head = TAILQ_HEAD_INITIALIZER(dpcpu_head);
static struct sx dpcpu_lock;
uintptr_t dpcpu_off[MAXCPU];
struct pcpu *cpuid_to_pcpu[MAXCPU];
struct cpuhead cpuhead = STAILQ_HEAD_INITIALIZER(cpuhead);

/*
 * Initialize the MI portions of a struct pcpu.
 */
void
pcpu_init(struct pcpu *pcpu, int cpuid, size_t size)
{

	bzero(pcpu, size);
	KASSERT(cpuid >= 0 && cpuid < MAXCPU,
	    ("pcpu_init: invalid cpuid %d", cpuid));
	pcpu->pc_cpuid = cpuid;
	cpuid_to_pcpu[cpuid] = pcpu;
	STAILQ_INSERT_TAIL(&cpuhead, pcpu, pc_allcpu);
	cpu_pcpu_init(pcpu, cpuid, size);
	pcpu->pc_rm_queue.rmq_next = &pcpu->pc_rm_queue;
	pcpu->pc_rm_queue.rmq_prev = &pcpu->pc_rm_queue;
}

void
dpcpu_init(void *dpcpu, int cpuid)
{
	struct pcpu *pcpu;

	pcpu = pcpu_find(cpuid);
	pcpu->pc_dynamic = (uintptr_t)dpcpu - DPCPU_START;

	/*
	 * Initialize defaults from our linker section.
	 */
	memcpy(dpcpu, (void *)DPCPU_START, DPCPU_BYTES);

	/*
	 * Place it in the global pcpu offset array.
	 */
	dpcpu_off[cpuid] = pcpu->pc_dynamic;
}

static void
dpcpu_startup(void *dummy __unused)
{
	struct dpcpu_free *df;

	df = malloc(sizeof(*df), M_PCPU, M_WAITOK | M_ZERO);
	df->df_start = (uintptr_t)&DPCPU_NAME(modspace);
	df->df_len = DPCPU_MODMIN;
	TAILQ_INSERT_HEAD(&dpcpu_head, df, df_link);
	sx_init(&dpcpu_lock, "dpcpu alloc lock");
}
SYSINIT(dpcpu, SI_SUB_KLD, SI_ORDER_FIRST, dpcpu_startup, NULL);

/*
 * UMA_PCPU_ZONE zones, that are available for all kernel
 * consumers. Right now 64 bit zone is used for counter(9)
 * and pointer zone is used by flowtable.
 */

uma_zone_t pcpu_zone_64;
uma_zone_t pcpu_zone_ptr;

static void
pcpu_zones_startup(void)
{

	pcpu_zone_64 = uma_zcreate("64 pcpu", sizeof(uint64_t),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, UMA_ZONE_PCPU);

	if (sizeof(uint64_t) == sizeof(void *))
		pcpu_zone_ptr = pcpu_zone_64;
	else
		pcpu_zone_ptr = uma_zcreate("ptr pcpu", sizeof(void *),
		    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, UMA_ZONE_PCPU);
}
SYSINIT(pcpu_zones, SI_SUB_VM, SI_ORDER_ANY, pcpu_zones_startup, NULL);

/*
 * First-fit extent based allocator for allocating space in the per-cpu
 * region reserved for modules.  This is only intended for use by the
 * kernel linkers to place module linker sets.
 */
void *
dpcpu_alloc(int size)
{
	struct dpcpu_free *df;
	void *s;

	s = NULL;
	size = roundup2(size, sizeof(void *));
	sx_xlock(&dpcpu_lock);
	TAILQ_FOREACH(df, &dpcpu_head, df_link) {
		if (df->df_len < size)
			continue;
		if (df->df_len == size) {
			s = (void *)df->df_start;
			TAILQ_REMOVE(&dpcpu_head, df, df_link);
			free(df, M_PCPU);
			break;
		}
		s = (void *)df->df_start;
		df->df_len -= size;
		df->df_start = df->df_start + size;
		break;
	}
	sx_xunlock(&dpcpu_lock);

	return (s);
}

/*
 * Free dynamic per-cpu space at module unload time. 
 */
void
dpcpu_free(void *s, int size)
{
	struct dpcpu_free *df;
	struct dpcpu_free *dn;
	uintptr_t start;
	uintptr_t end;

	size = roundup2(size, sizeof(void *));
	start = (uintptr_t)s;
	end = start + size;
	/*
	 * Free a region of space and merge it with as many neighbors as
	 * possible.  Keeping the list sorted simplifies this operation.
	 */
	sx_xlock(&dpcpu_lock);
	TAILQ_FOREACH(df, &dpcpu_head, df_link) {
		if (df->df_start > end)
			break;
		/*
		 * If we expand at the end of an entry we may have to
		 * merge it with the one following it as well.
		 */
		if (df->df_start + df->df_len == start) {
			df->df_len += size;
			dn = TAILQ_NEXT(df, df_link);
			if (df->df_start + df->df_len == dn->df_start) {
				df->df_len += dn->df_len;
				TAILQ_REMOVE(&dpcpu_head, dn, df_link);
				free(dn, M_PCPU);
			}
			sx_xunlock(&dpcpu_lock);
			return;
		}
		if (df->df_start == end) {
			df->df_start = start;
			df->df_len += size;
			sx_xunlock(&dpcpu_lock);
			return;
		}
	}
	dn = malloc(sizeof(*df), M_PCPU, M_WAITOK | M_ZERO);
	dn->df_start = start;
	dn->df_len = size;
	if (df)
		TAILQ_INSERT_BEFORE(df, dn, df_link);
	else
		TAILQ_INSERT_TAIL(&dpcpu_head, dn, df_link);
	sx_xunlock(&dpcpu_lock);
}

/*
 * Initialize the per-cpu storage from an updated linker-set region.
 */
void
dpcpu_copy(void *s, int size)
{
#ifdef SMP
	uintptr_t dpcpu;
	int i;

	CPU_FOREACH(i) {
		dpcpu = dpcpu_off[i];
		if (dpcpu == 0)
			continue;
		memcpy((void *)(dpcpu + (uintptr_t)s), s, size);
	}
#else
	memcpy((void *)(dpcpu_off[0] + (uintptr_t)s), s, size);
#endif
}

/*
 * Destroy a struct pcpu.
 */
void
pcpu_destroy(struct pcpu *pcpu)
{

	STAILQ_REMOVE(&cpuhead, pcpu, pcpu, pc_allcpu);
	cpuid_to_pcpu[pcpu->pc_cpuid] = NULL;
	dpcpu_off[pcpu->pc_cpuid] = 0;
}

/*
 * Locate a struct pcpu by cpu id.
 */
struct pcpu *
pcpu_find(u_int cpuid)
{

	return (cpuid_to_pcpu[cpuid]);
}

int
sysctl_dpcpu_quad(SYSCTL_HANDLER_ARGS)
{
	uintptr_t dpcpu;
	int64_t count;
	int i;

	count = 0;
	CPU_FOREACH(i) {
		dpcpu = dpcpu_off[i];
		if (dpcpu == 0)
			continue;
		count += *(int64_t *)(dpcpu + (uintptr_t)arg1);
	}
	return (SYSCTL_OUT(req, &count, sizeof(count)));
}

int
sysctl_dpcpu_long(SYSCTL_HANDLER_ARGS)
{
	uintptr_t dpcpu;
	long count;
	int i;

	count = 0;
	CPU_FOREACH(i) {
		dpcpu = dpcpu_off[i];
		if (dpcpu == 0)
			continue;
		count += *(long *)(dpcpu + (uintptr_t)arg1);
	}
	return (SYSCTL_OUT(req, &count, sizeof(count)));
}

int
sysctl_dpcpu_int(SYSCTL_HANDLER_ARGS)
{
	uintptr_t dpcpu;
	int count;
	int i;

	count = 0;
	CPU_FOREACH(i) {
		dpcpu = dpcpu_off[i];
		if (dpcpu == 0)
			continue;
		count += *(int *)(dpcpu + (uintptr_t)arg1);
	}
	return (SYSCTL_OUT(req, &count, sizeof(count)));
}

#ifdef DDB
DB_SHOW_COMMAND(dpcpu_off, db_show_dpcpu_off)
{
	int id;

	CPU_FOREACH(id) {
		db_printf("dpcpu_off[%2d] = 0x%jx (+ DPCPU_START = %p)\n",
		    id, (uintmax_t)dpcpu_off[id],
		    (void *)(uintptr_t)(dpcpu_off[id] + DPCPU_START));
	}
}

static void
show_pcpu(struct pcpu *pc)
{
	struct thread *td;

	db_printf("cpuid        = %d\n", pc->pc_cpuid);
	db_printf("dynamic pcpu = %p\n", (void *)pc->pc_dynamic);
	db_printf("curthread    = ");
	td = pc->pc_curthread;
	if (td != NULL)
		db_printf("%p: pid %d tid %d \"%s\"\n", td, td->td_proc->p_pid,
		    td->td_tid, td->td_name);
	else
		db_printf("none\n");
	db_printf("curpcb       = %p\n", pc->pc_curpcb);
	db_printf("fpcurthread  = ");
	td = pc->pc_fpcurthread;
	if (td != NULL)
		db_printf("%p: pid %d \"%s\"\n", td, td->td_proc->p_pid,
		    td->td_name);
	else
		db_printf("none\n");
	db_printf("idlethread   = ");
	td = pc->pc_idlethread;
	if (td != NULL)
		db_printf("%p: tid %d \"%s\"\n", td, td->td_tid, td->td_name);
	else
		db_printf("none\n");
	db_show_mdpcpu(pc);

#ifdef VIMAGE
	db_printf("curvnet      = %p\n", pc->pc_curthread->td_vnet);
#endif

#ifdef WITNESS
	db_printf("spin locks held:\n");
	witness_list_locks(&pc->pc_spinlocks, db_printf);
#endif
}

DB_SHOW_COMMAND(pcpu, db_show_pcpu)
{
	struct pcpu *pc;
	int id;

	if (have_addr)
		id = ((addr >> 4) % 16) * 10 + (addr % 16);
	else
		id = PCPU_GET(cpuid);
	pc = pcpu_find(id);
	if (pc == NULL) {
		db_printf("CPU %d not found\n", id);
		return;
	}
	show_pcpu(pc);
}

DB_SHOW_ALL_COMMAND(pcpu, db_show_cpu_all)
{
	struct pcpu *pc;
	int id;

	db_printf("Current CPU: %d\n\n", PCPU_GET(cpuid));
	CPU_FOREACH(id) {
		pc = pcpu_find(id);
		if (pc != NULL) {
			show_pcpu(pc);
			db_printf("\n");
		}
	}
}
DB_SHOW_ALIAS(allpcpu, db_show_cpu_all);
#endif
