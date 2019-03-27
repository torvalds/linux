/*-
 * Copyright (c) 1997, 1998 Justin T. Gibbs.
 * Copyright (c) 2013, 2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
 *
 * Portions of this software were developed by Semihalf
 * under sponsorship of the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/memdesc.h>
#include <sys/mutex.h>
#include <sys/uio.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <arm64/include/bus_dma_impl.h>

/*
 * Convenience function for manipulating driver locks from busdma (during
 * busdma_swi, for example).  Drivers that don't provide their own locks
 * should specify &Giant to dmat->lockfuncarg.  Drivers that use their own
 * non-mutex locking scheme don't have to use this at all.
 */
void
busdma_lock_mutex(void *arg, bus_dma_lock_op_t op)
{
	struct mtx *dmtx;

	dmtx = (struct mtx *)arg;
	switch (op) {
	case BUS_DMA_LOCK:
		mtx_lock(dmtx);
		break;
	case BUS_DMA_UNLOCK:
		mtx_unlock(dmtx);
		break;
	default:
		panic("Unknown operation 0x%x for busdma_lock_mutex!", op);
	}
}

/*
 * dflt_lock should never get called.  It gets put into the dma tag when
 * lockfunc == NULL, which is only valid if the maps that are associated
 * with the tag are meant to never be defered.
 * XXX Should have a way to identify which driver is responsible here.
 */
void
bus_dma_dflt_lock(void *arg, bus_dma_lock_op_t op)
{

	panic("driver error: busdma dflt_lock called");
}

/*
 * Return true if a match is made.
 *
 * To find a match walk the chain of bus_dma_tag_t's looking for 'paddr'.
 *
 * If paddr is within the bounds of the dma tag then call the filter callback
 * to check for a match, if there is no filter callback then assume a match.
 */
int
bus_dma_run_filter(struct bus_dma_tag_common *tc, bus_addr_t paddr)
{
	int retval;

	retval = 0;
	do {
		if (((paddr > tc->lowaddr && paddr <= tc->highaddr) ||
		    ((paddr & (tc->alignment - 1)) != 0)) &&
		    (tc->filter == NULL ||
		    (*tc->filter)(tc->filterarg, paddr) != 0))
			retval = 1;

		tc = tc->parent;		
	} while (retval == 0 && tc != NULL);
	return (retval);
}

int
common_bus_dma_tag_create(struct bus_dma_tag_common *parent,
    bus_size_t alignment, bus_addr_t boundary, bus_addr_t lowaddr,
    bus_addr_t highaddr, bus_dma_filter_t *filter, void *filterarg,
    bus_size_t maxsize, int nsegments, bus_size_t maxsegsz, int flags,
    bus_dma_lock_t *lockfunc, void *lockfuncarg, size_t sz, void **dmat)
{
	void *newtag;
	struct bus_dma_tag_common *common;

	KASSERT(sz >= sizeof(struct bus_dma_tag_common), ("sz"));
	/* Return a NULL tag on failure */
	*dmat = NULL;
	/* Basic sanity checking */
	if (boundary != 0 && boundary < maxsegsz)
		maxsegsz = boundary;
	if (maxsegsz == 0)
		return (EINVAL);

	newtag = malloc(sz, M_DEVBUF, M_ZERO | M_NOWAIT);
	if (newtag == NULL) {
		CTR4(KTR_BUSDMA, "%s returned tag %p tag flags 0x%x error %d",
		    __func__, newtag, 0, ENOMEM);
		return (ENOMEM);
	}

	common = newtag;
	common->impl = &bus_dma_bounce_impl;
	common->parent = parent;
	common->alignment = alignment;
	common->boundary = boundary;
	common->lowaddr = trunc_page((vm_paddr_t)lowaddr) + (PAGE_SIZE - 1);
	common->highaddr = trunc_page((vm_paddr_t)highaddr) + (PAGE_SIZE - 1);
	common->filter = filter;
	common->filterarg = filterarg;
	common->maxsize = maxsize;
	common->nsegments = nsegments;
	common->maxsegsz = maxsegsz;
	common->flags = flags;
	common->ref_count = 1; /* Count ourself */
	if (lockfunc != NULL) {
		common->lockfunc = lockfunc;
		common->lockfuncarg = lockfuncarg;
	} else {
		common->lockfunc = bus_dma_dflt_lock;
		common->lockfuncarg = NULL;
	}

	/* Take into account any restrictions imposed by our parent tag */
	if (parent != NULL) {
		common->impl = parent->impl;
		common->lowaddr = MIN(parent->lowaddr, common->lowaddr);
		common->highaddr = MAX(parent->highaddr, common->highaddr);
		if (common->boundary == 0)
			common->boundary = parent->boundary;
		else if (parent->boundary != 0) {
			common->boundary = MIN(parent->boundary,
			    common->boundary);
		}
		if (common->filter == NULL) {
			/*
			 * Short circuit looking at our parent directly
			 * since we have encapsulated all of its information
			 */
			common->filter = parent->filter;
			common->filterarg = parent->filterarg;
			common->parent = parent->parent;
		}
		atomic_add_int(&parent->ref_count, 1);
	}
	*dmat = common;
	return (0);
}

/*
 * Allocate a device specific dma_tag.
 */
int
bus_dma_tag_create(bus_dma_tag_t parent, bus_size_t alignment,
    bus_addr_t boundary, bus_addr_t lowaddr, bus_addr_t highaddr,
    bus_dma_filter_t *filter, void *filterarg, bus_size_t maxsize,
    int nsegments, bus_size_t maxsegsz, int flags, bus_dma_lock_t *lockfunc,
    void *lockfuncarg, bus_dma_tag_t *dmat)
{
	struct bus_dma_tag_common *tc;
	int error;

	if (parent == NULL) {
		error = bus_dma_bounce_impl.tag_create(parent, alignment,
		    boundary, lowaddr, highaddr, filter, filterarg, maxsize,
		    nsegments, maxsegsz, flags, lockfunc, lockfuncarg, dmat);
	} else {
		tc = (struct bus_dma_tag_common *)parent;
		error = tc->impl->tag_create(parent, alignment,
		    boundary, lowaddr, highaddr, filter, filterarg, maxsize,
		    nsegments, maxsegsz, flags, lockfunc, lockfuncarg, dmat);
	}
	return (error);
}

int
bus_dma_tag_destroy(bus_dma_tag_t dmat)
{
	struct bus_dma_tag_common *tc;

	tc = (struct bus_dma_tag_common *)dmat;
	return (tc->impl->tag_destroy(dmat));
}

int
bus_dma_tag_set_domain(bus_dma_tag_t dmat, int domain)
{

	return (0);
}
