/* $OpenBSD: kern_intrmap.c,v 1.4 2025/06/13 09:48:45 jsg Exp $ */

/*
 * Copyright (c) 1980, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)if.c	8.3 (Berkeley) 1/4/94
 * $FreeBSD: src/sys/net/if.c,v 1.185 2004/03/13 02:35:03 brooks Exp $
 */

/*
 * This code is adapted from the if_ringmap code in DragonflyBSD,
 * but generalised for use by all types of devices, not just network
 * cards.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/rwlock.h>

#include <sys/intrmap.h>

struct intrmap_cpus {
	struct refcnt	  ic_refs;
	unsigned int	  ic_count;
	struct cpu_info **ic_cpumap;
};

struct intrmap {
	unsigned int	 im_count;
	unsigned int	 im_grid;
	struct intrmap_cpus *
			 im_cpus;
	unsigned int	*im_cpumap;
};

/*
 * The CPUs that should be used for interrupts may be a subset of all CPUs.
 */

struct rwlock		 intrmap_lock = RWLOCK_INITIALIZER("intrcpus");
struct intrmap_cpus	*intrmap_cpus = NULL;
int			 intrmap_ncpu = 0;

static void
intrmap_cpus_put(struct intrmap_cpus *ic)
{
	if (ic == NULL)
		return;

	if (refcnt_rele(&ic->ic_refs)) {
		free(ic->ic_cpumap, M_DEVBUF,
		    ic->ic_count * sizeof(*ic->ic_cpumap));
		free(ic, M_DEVBUF, sizeof(*ic));
	}
}

static struct intrmap_cpus *
intrmap_cpus_get(void)
{
	struct intrmap_cpus *oic = NULL;
	struct intrmap_cpus *ic;

	rw_enter_write(&intrmap_lock);
	if (intrmap_ncpu != ncpus) {
		unsigned int icpus = 0;
		struct cpu_info **cpumap;
		CPU_INFO_ITERATOR cii;
		struct cpu_info *ci;

		/*
		 * there's a new "version" of the set of CPUs available, so
		 * we need to figure out which ones we can use for interrupts.
		 */

		cpumap = mallocarray(ncpus, sizeof(*cpumap),
		    M_DEVBUF, M_WAITOK);

		CPU_INFO_FOREACH(cii, ci) {
#ifdef __HAVE_CPU_TOPOLOGY
			if (ci->ci_smt_id > 0)
				continue;
#endif
			cpumap[icpus++] = ci;
		}

		if (icpus < ncpus) {
			/* this is mostly about free(9) needing a size */
			struct cpu_info **icpumap = mallocarray(icpus,
			    sizeof(*icpumap), M_DEVBUF, M_WAITOK);
			memcpy(icpumap, cpumap, icpus * sizeof(*icpumap));
			free(cpumap, M_DEVBUF, ncpus * sizeof(*cpumap));
			cpumap = icpumap;
		}

		ic = malloc(sizeof(*ic), M_DEVBUF, M_WAITOK);
		refcnt_init(&ic->ic_refs);
		ic->ic_count = icpus;
		ic->ic_cpumap = cpumap;

		oic = intrmap_cpus;
		intrmap_cpus = ic; /* give this ref to the global. */
	} else
		ic = intrmap_cpus;

	refcnt_take(&ic->ic_refs); /* take a ref for the caller */
	rw_exit_write(&intrmap_lock);

	intrmap_cpus_put(oic);

	return (ic);
}

static int
intrmap_nintrs(const struct intrmap_cpus *ic, unsigned int nintrs,
    unsigned int maxintrs)
{
	KASSERTMSG(maxintrs > 0, "invalid maximum interrupt count %u",
	    maxintrs);

	if (nintrs == 0 || nintrs > maxintrs)
		nintrs = maxintrs;
	if (nintrs > ic->ic_count)
		nintrs = ic->ic_count;
	return (nintrs);
}

static void
intrmap_set_grid(struct intrmap *im, unsigned int unit, unsigned int grid)
{
	unsigned int i, offset;
	unsigned int *cpumap = im->im_cpumap;
	const struct intrmap_cpus *ic = im->im_cpus;

	KASSERTMSG(grid > 0, "invalid if_ringmap grid %u", grid);
	KASSERTMSG(grid >= im->im_count, "invalid intrmap grid %u, count %u",
	    grid, im->im_count);
	im->im_grid = grid;

	offset = (grid * unit) % ic->ic_count;
	for (i = 0; i < im->im_count; i++) {
		cpumap[i] = offset + i;
		KASSERTMSG(cpumap[i] < ic->ic_count,
		    "invalid cpumap[%u] = %u, offset %u (ncpu %d)", i,
		    cpumap[i], offset, ic->ic_count);
	}
}

struct intrmap *
intrmap_create(const struct device *dv,
    unsigned int nintrs, unsigned int maxintrs, unsigned int flags)
{
	struct intrmap *im;
	unsigned int unit = dv->dv_unit;
	unsigned int i, grid = 0, prev_grid;
	struct intrmap_cpus *ic;

	ic = intrmap_cpus_get();

	nintrs = intrmap_nintrs(ic, nintrs, maxintrs);
	if (ISSET(flags, INTRMAP_POWEROF2))
		nintrs = 1 << (fls(nintrs) - 1);
	im = malloc(sizeof(*im), M_DEVBUF, M_WAITOK | M_ZERO);
	im->im_count = nintrs;
	im->im_cpus = ic; 
	im->im_cpumap = mallocarray(nintrs, sizeof(*im->im_cpumap), M_DEVBUF,
	    M_WAITOK | M_ZERO);

	prev_grid = ic->ic_count;
	for (i = 0; i < ic->ic_count; i++) {
		if (ic->ic_count % (i + 1) != 0)
			continue;

		grid = ic->ic_count / (i + 1);
		if (nintrs > grid) {
			grid = prev_grid;
			break;
		}

		if (nintrs > ic->ic_count / (i + 2))
			break;
		prev_grid = grid;
	}
	intrmap_set_grid(im, unit, grid);

	return (im);
}

void
intrmap_destroy(struct intrmap *im)
{
	free(im->im_cpumap, M_DEVBUF, im->im_count * sizeof(*im->im_cpumap));
	intrmap_cpus_put(im->im_cpus);
	free(im, M_DEVBUF, sizeof(*im));
}

unsigned int
intrmap_count(const struct intrmap *im)
{
	return (im->im_count);
}

struct cpu_info *
intrmap_cpu(const struct intrmap *im, unsigned int ring)
{
	const struct intrmap_cpus *ic = im->im_cpus;
	unsigned int icpu;
	KASSERTMSG(ring < im->im_count, "invalid ring %u", ring);
	icpu = im->im_cpumap[ring];
	KASSERTMSG(icpu < ic->ic_count, "invalid interrupt cpu %u for ring %u"
	    " (intrmap %p)", icpu, ring, im);
	return (ic->ic_cpumap[icpu]);
}
