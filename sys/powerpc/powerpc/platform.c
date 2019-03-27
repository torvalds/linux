/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Peter Grehan
 * Copyright (c) 2009 Nathan Whitehorn
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Dispatch platform calls to the appropriate platform implementation
 * through a previously registered kernel object.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/ktr.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

#include <machine/cpu.h>
#include <machine/md_var.h>
#include <machine/platform.h>
#include <machine/platformvar.h>
#include <machine/smp.h>

#include "platform_if.h"

static platform_def_t	*plat_def_impl;
static platform_t	plat_obj;
static struct kobj_ops	plat_kernel_kops;
static struct platform_kobj	plat_kernel_obj;

static char plat_name[64] = "";
SYSCTL_STRING(_hw, OID_AUTO, platform, CTLFLAG_RD | CTLFLAG_TUN,
    plat_name, 0, "Platform currently in use");

static struct mem_region pregions[PHYS_AVAIL_SZ];
static struct mem_region aregions[PHYS_AVAIL_SZ];
static int npregions, naregions;

/*
 * Memory region utilities: determine if two regions overlap,
 * and merge two overlapping regions into one
 */
static int
memr_overlap(struct mem_region *r1, struct mem_region *r2)
{
	if ((r1->mr_start + r1->mr_size) < r2->mr_start ||
	    (r2->mr_start + r2->mr_size) < r1->mr_start)
		return (FALSE);

	return (TRUE);
}

static void
memr_merge(struct mem_region *from, struct mem_region *to)
{
	vm_offset_t end;
	end = uqmax(to->mr_start + to->mr_size, from->mr_start + from->mr_size);
	to->mr_start = uqmin(from->mr_start, to->mr_start);
	to->mr_size = end - to->mr_start;
}

/*
 * Quick sort callout for comparing memory regions.
 */
static int
mr_cmp(const void *a, const void *b)
{
	const struct mem_region *regiona, *regionb;

	regiona = a;
	regionb = b;
	if (regiona->mr_start < regionb->mr_start)
		return (-1);
	else if (regiona->mr_start > regionb->mr_start)
		return (1);
	else
		return (0);
}

void
mem_regions(struct mem_region **phys, int *physsz, struct mem_region **avail,
    int *availsz)
{
	int i, j, still_merging;

	if (npregions == 0) {
		PLATFORM_MEM_REGIONS(plat_obj, pregions, &npregions,
		    aregions, &naregions);
		qsort(pregions, npregions, sizeof(*pregions), mr_cmp);
		qsort(aregions, naregions, sizeof(*aregions), mr_cmp);

		/* Remove overlapping available regions */
		do {
			still_merging = FALSE;
			for (i = 0; i < naregions; i++) {
				if (aregions[i].mr_size == 0)
					continue;
				for (j = i+1; j < naregions; j++) {
					if (aregions[j].mr_size == 0)
						continue;
					if (!memr_overlap(&aregions[j],
					    &aregions[i]))
						continue;

					memr_merge(&aregions[j], &aregions[i]);
					/* mark inactive */
					aregions[j].mr_size = 0;
					still_merging = TRUE;
				}
			}
		} while (still_merging == TRUE);

		/* Collapse zero-length available regions */
		for (i = 0; i < naregions; i++) {
			if (aregions[i].mr_size == 0) {
				memcpy(&aregions[i], &aregions[i+1],
				    (naregions - i - 1)*sizeof(*aregions));
				naregions--;
				i--;
			}
		}
	}

	if (phys != NULL)
		*phys = pregions;
	if (avail != NULL)
		*avail = aregions;
	if (physsz != NULL)
		*physsz = npregions;
	if (availsz != NULL)
		*availsz = naregions;
}

int
mem_valid(vm_offset_t addr, int len)
{
	int i;

	if (npregions == 0) {
		struct mem_region *p, *a;
		int na, np;
		mem_regions(&p, &np, &a, &na);
	}

	for (i = 0; i < npregions; i++)
		if ((addr >= pregions[i].mr_start)
		   && (addr + len <= pregions[i].mr_start + pregions[i].mr_size))
			return (0);

	return (EFAULT);
}

vm_offset_t
platform_real_maxaddr(void)
{
	return (PLATFORM_REAL_MAXADDR(plat_obj));
}

const char *
installed_platform()
{
	return (plat_def_impl->name);
}

u_long
platform_timebase_freq(struct cpuref *cpu)
{
	return (PLATFORM_TIMEBASE_FREQ(plat_obj, cpu));
}

/*
 * Put the current CPU, as last step in suspend, to sleep
 */
void
platform_sleep()
{
        PLATFORM_SLEEP(plat_obj);
}

int
platform_smp_first_cpu(struct cpuref *cpu)
{
	return (PLATFORM_SMP_FIRST_CPU(plat_obj, cpu));
}

int
platform_smp_next_cpu(struct cpuref *cpu)
{
	return (PLATFORM_SMP_NEXT_CPU(plat_obj, cpu));
}

int
platform_smp_get_bsp(struct cpuref *cpu)
{
	return (PLATFORM_SMP_GET_BSP(plat_obj, cpu));
}

int
platform_smp_start_cpu(struct pcpu *cpu)
{
	return (PLATFORM_SMP_START_CPU(plat_obj, cpu));
}

void
platform_smp_ap_init()
{
	PLATFORM_SMP_AP_INIT(plat_obj);
}

void
platform_smp_probe_threads(void)
{
	PLATFORM_SMP_PROBE_THREADS(plat_obj);
}

#ifdef SMP
struct cpu_group *
cpu_topo(void)
{
        return (PLATFORM_SMP_TOPO(plat_obj));
}
#endif

/*
 * Reset back to firmware.
 */
void
cpu_reset()
{
        PLATFORM_RESET(plat_obj);
}

void platform_smp_timebase_sync(u_long tb, int ap)
{

	PLATFORM_SMP_TIMEBASE_SYNC(plat_obj, tb, ap);
}

/*
 * Platform install routines. Highest priority wins, using the same
 * algorithm as bus attachment.
 */
SET_DECLARE(platform_set, platform_def_t);

void
platform_probe_and_attach()
{
	platform_def_t	**platpp, *platp;
	int		prio, best_prio;

	plat_obj = &plat_kernel_obj;
	best_prio = 0;

	/*
	 * Try to locate the best platform kobj
	 */
	SET_FOREACH(platpp, platform_set) {
		platp = *platpp;

		/*
		 * Take care of compiling the selected class, and
		 * then statically initialise the MMU object
		 */
		kobj_class_compile_static(platp, &plat_kernel_kops);
		kobj_init_static((kobj_t)plat_obj, platp);

		prio = PLATFORM_PROBE(plat_obj);

		/* Check for errors */
		if (prio > 0)
			continue;

		/*
		 * Check if this module was specifically requested through
		 * the loader tunable we provide.
		 */
		if (strcmp(platp->name,plat_name) == 0) {
			plat_def_impl = platp;
			break;
		}

		/* Otherwise, see if it is better than our current best */
		if (plat_def_impl == NULL || prio > best_prio) {
			best_prio = prio;
			plat_def_impl = platp;
		}

		/*
		 * We can't free the KOBJ, since it is static. Reset the ops
		 * member of this class so that we can come back later.
		 */
		platp->ops = NULL;
	}

	if (plat_def_impl == NULL)
		panic("No platform module found!");

	/*
	 * Recompile to make sure we ended with the
	 * correct one, and then attach.
	 */

	kobj_class_compile_static(plat_def_impl, &plat_kernel_kops);
	kobj_init_static((kobj_t)plat_obj, plat_def_impl);

	strlcpy(plat_name,plat_def_impl->name,sizeof(plat_name));

	PLATFORM_ATTACH(plat_obj);
}

