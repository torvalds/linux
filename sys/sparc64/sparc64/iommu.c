/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD AND BSD-3-Clause
 *
 * Copyright (c) 1999, 2000 Matthew R. Green
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: NetBSD: iommu.c,v 1.82 2008/05/30 02:29:37 mrg Exp
 */
/*-
 * Copyright (c) 1999-2002 Eduardo Horvath
 * Copyright (c) 2001-2003 Thomas Moestl
 * Copyright (c) 2007, 2009 Marius Strobl <marius@FreeBSD.org>
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: NetBSD: sbus.c,v 1.50 2002/06/20 18:26:24 eeh Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * UltraSPARC IOMMU support; used by both the PCI and SBus code.
 *
 * TODO:
 * - Support sub-page boundaries.
 * - Fix alignment handling for small allocations (the possible page offset
 *   of malloc()ed memory is not handled at all).  Revise interaction of
 *   alignment with the load_mbuf and load_uio functions.
 * - Handle lowaddr and highaddr in some way, and try to work out a way
 *   for filter callbacks to work.  Currently, only lowaddr is honored
 *   in that no addresses above it are considered at all.
 * - Implement BUS_DMA_ALLOCNOW in bus_dma_tag_create as far as possible.
 * - Check the possible return values and callback error arguments;
 *   the callback currently gets called in error conditions where it should
 *   not be.
 * - When running out of DVMA space, return EINPROGRESS in the non-
 *   BUS_DMA_NOWAIT case and delay the callback until sufficient space
 *   becomes available.
 */

#include "opt_iommu.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <machine/asi.h>
#include <machine/bus.h>
#include <machine/bus_private.h>
#include <machine/iommureg.h>
#include <machine/resource.h>
#include <machine/ver.h>

#include <sys/rman.h>

#include <machine/iommuvar.h>

/*
 * Tuning constants
 */
#define	IOMMU_MAX_PRE		(32 * 1024)
#define	IOMMU_MAX_PRE_SEG	3

/* Threshold for using the streaming buffer */
#define	IOMMU_STREAM_THRESH	128

static MALLOC_DEFINE(M_IOMMU, "dvmamem", "IOMMU DVMA Buffers");

static	int iommu_strbuf_flush_sync(struct iommu_state *);
#ifdef IOMMU_DIAG
static	void iommu_diag(struct iommu_state *, vm_offset_t va);
#endif

/*
 * Helpers
 */
#define	IOMMU_READ8(is, reg, off)					\
	bus_space_read_8((is)->is_bustag, (is)->is_bushandle,		\
	    (is)->reg + (off))
#define	IOMMU_WRITE8(is, reg, off, v)					\
	bus_space_write_8((is)->is_bustag, (is)->is_bushandle,		\
	    (is)->reg + (off), (v))

#define	IOMMU_HAS_SB(is)						\
	((is)->is_sb[0] != 0 || (is)->is_sb[1] != 0)

/*
 * Always overallocate one page; this is needed to handle alignment of the
 * buffer, so it makes sense using a lazy allocation scheme.
 */
#define	IOMMU_SIZE_ROUNDUP(sz)						\
	(round_io_page(sz) + IO_PAGE_SIZE)

#define	IOMMU_SET_TTE(is, va, tte)					\
	((is)->is_tsb[IOTSBSLOT(va)] = (tte))
#define	IOMMU_GET_TTE(is, va)						\
	(is)->is_tsb[IOTSBSLOT(va)]

/* Resource helpers */
#define	IOMMU_RES_START(res)						\
	((bus_addr_t)rman_get_start(res) << IO_PAGE_SHIFT)
#define	IOMMU_RES_END(res)						\
	((bus_addr_t)(rman_get_end(res) + 1) << IO_PAGE_SHIFT)
#define	IOMMU_RES_SIZE(res)						\
	((bus_size_t)rman_get_size(res) << IO_PAGE_SHIFT)

/* Helpers for struct bus_dmamap_res */
#define	BDR_START(r)	IOMMU_RES_START((r)->dr_res)
#define	BDR_END(r)	IOMMU_RES_END((r)->dr_res)
#define	BDR_SIZE(r)	IOMMU_RES_SIZE((r)->dr_res)

/* Locking macros */
#define	IS_LOCK(is)	mtx_lock(&is->is_mtx)
#define	IS_LOCK_ASSERT(is)	mtx_assert(&is->is_mtx, MA_OWNED)
#define	IS_UNLOCK(is)	mtx_unlock(&is->is_mtx)

/* Flush a page from the TLB.  No locking required, since this is atomic. */
static __inline void
iommu_tlb_flush(struct iommu_state *is, bus_addr_t va)
{

	if ((is->is_flags & IOMMU_FIRE) != 0)
		/*
		 * Direct page flushing is not supported and also not
		 * necessary due to cache snooping.
		 */
		return;
	IOMMU_WRITE8(is, is_iommu, IMR_FLUSH, va);
}

/*
 * Flush a page from the streaming buffer.  No locking required, since this
 * is atomic.
 */
static __inline void
iommu_strbuf_flushpg(struct iommu_state *is, bus_addr_t va)
{
	int i;

	for (i = 0; i < 2; i++)
		if (is->is_sb[i] != 0)
			IOMMU_WRITE8(is, is_sb[i], ISR_PGFLUSH, va);
}

/*
 * Flush an address from the streaming buffer(s); this is an asynchronous
 * operation.  To make sure that it has completed, iommu_strbuf_sync() needs
 * to be called.  No locking required.
 */
static __inline void
iommu_strbuf_flush(struct iommu_state *is, bus_addr_t va)
{

	iommu_strbuf_flushpg(is, va);
}

/* Synchronize all outstanding flush operations. */
static __inline void
iommu_strbuf_sync(struct iommu_state *is)
{

	IS_LOCK_ASSERT(is);
	iommu_strbuf_flush_sync(is);
}

/* LRU queue handling for lazy resource allocation. */
static __inline void
iommu_map_insq(struct iommu_state *is, bus_dmamap_t map)
{

	IS_LOCK_ASSERT(is);
	if (!SLIST_EMPTY(&map->dm_reslist)) {
		if (map->dm_onq)
			TAILQ_REMOVE(&is->is_maplruq, map, dm_maplruq);
		TAILQ_INSERT_TAIL(&is->is_maplruq, map, dm_maplruq);
		map->dm_onq = 1;
	}
}

static __inline void
iommu_map_remq(struct iommu_state *is, bus_dmamap_t map)
{

	IS_LOCK_ASSERT(is);
	if (map->dm_onq)
		TAILQ_REMOVE(&is->is_maplruq, map, dm_maplruq);
	map->dm_onq = 0;
}

/*
 * initialise the UltraSPARC IOMMU (PCI or SBus):
 *	- allocate and setup the iotsb.
 *	- enable the IOMMU
 *	- initialise the streaming buffers (if they exist)
 *	- create a private DVMA map.
 */
void
iommu_init(const char *name, struct iommu_state *is, u_int tsbsize,
    uint32_t iovabase, u_int resvpg)
{
	vm_size_t size;
	vm_offset_t offs;
	uint64_t end, obpmap, obpptsb, tte;
	u_int maxtsbsize, obptsbentries, obptsbsize, slot, tsbentries;
	int i;

	/*
	 * Setup the IOMMU.
	 *
	 * The sun4u IOMMU is part of the PCI or SBus controller so we
	 * will deal with it here..
	 *
	 * The IOMMU address space always ends at 0xffffe000, but the starting
	 * address depends on the size of the map.  The map size is 1024 * 2 ^
	 * is->is_tsbsize entries, where each entry is 8 bytes.  The start of
	 * the map can be calculated by (0xffffe000 << (8 + is->is_tsbsize)).
	 */
	if ((is->is_flags & IOMMU_FIRE) != 0) {
		maxtsbsize = IOMMU_TSB512K;
		/*
		 * We enable bypass in order to be able to use a physical
		 * address for the event queue base.
		 */
		is->is_cr = IOMMUCR_SE | IOMMUCR_CM_C_TLB_TBW | IOMMUCR_BE;
	} else {
		maxtsbsize = IOMMU_TSB128K;
		is->is_cr = (tsbsize << IOMMUCR_TSBSZ_SHIFT) | IOMMUCR_DE;
	}
	if (tsbsize > maxtsbsize)
		panic("%s: unsupported TSB size	", __func__);
	tsbentries = IOMMU_TSBENTRIES(tsbsize);
	is->is_cr |= IOMMUCR_EN;
	is->is_tsbsize = tsbsize;
	is->is_dvmabase = iovabase;
	if (iovabase == -1)
		is->is_dvmabase = IOTSB_VSTART(is->is_tsbsize);

	size = IOTSB_BASESZ << is->is_tsbsize;
	printf("%s: DVMA map: %#lx to %#lx %d entries%s\n", name,
	    is->is_dvmabase, is->is_dvmabase +
	    (size << (IO_PAGE_SHIFT - IOTTE_SHIFT)) - 1, tsbentries,
	    IOMMU_HAS_SB(is) ? ", streaming buffer" : "");

	/*
	 * Set up resource mamangement.
	 */
	mtx_init(&is->is_mtx, "iommu", NULL, MTX_DEF);
	end = is->is_dvmabase + (size << (IO_PAGE_SHIFT - IOTTE_SHIFT));
	is->is_dvma_rman.rm_type = RMAN_ARRAY;
	is->is_dvma_rman.rm_descr = "DVMA Memory";
	if (rman_init(&is->is_dvma_rman) != 0 ||
	    rman_manage_region(&is->is_dvma_rman,
	    (is->is_dvmabase >> IO_PAGE_SHIFT) + resvpg,
	    (end >> IO_PAGE_SHIFT) - 1) != 0)
		panic("%s: could not initialize DVMA rman", __func__);
	TAILQ_INIT(&is->is_maplruq);

	/*
	 * Allocate memory for I/O page tables.  They need to be
	 * physically contiguous.
	 */
	is->is_tsb = contigmalloc(size, M_DEVBUF, M_NOWAIT, 0, ~0UL,
	    PAGE_SIZE, 0);
	if (is->is_tsb == NULL)
		panic("%s: contigmalloc failed", __func__);
	is->is_ptsb = pmap_kextract((vm_offset_t)is->is_tsb);
	bzero(is->is_tsb, size);

	/*
	 * Add the PROM mappings to the kernel IOTSB if desired.
	 * Note that the firmware of certain Darwin boards doesn't set
	 * the TSB size correctly.
	 */
	if ((is->is_flags & IOMMU_FIRE) != 0)
		obptsbsize = (IOMMU_READ8(is, is_iommu, IMR_TSB) &
		    IOMMUTB_TSBSZ_MASK) >> IOMMUTB_TSBSZ_SHIFT;
	else
		obptsbsize = (IOMMU_READ8(is, is_iommu, IMR_CTL) &
		    IOMMUCR_TSBSZ_MASK) >> IOMMUCR_TSBSZ_SHIFT;
	obptsbentries = IOMMU_TSBENTRIES(obptsbsize);
	if (bootverbose)
		printf("%s: PROM IOTSB size: %d (%d entries)\n", name,
		    obptsbsize, obptsbentries);
	if ((is->is_flags & IOMMU_PRESERVE_PROM) != 0 &&
	    !(PCPU_GET(impl) == CPU_IMPL_ULTRASPARCIIi && obptsbsize == 7)) {
		if (obptsbentries > tsbentries)
			panic("%s: PROM IOTSB entries exceed kernel",
			    __func__);
		obpptsb = IOMMU_READ8(is, is_iommu, IMR_TSB) &
		    IOMMUTB_TB_MASK;
		for (i = 0; i < obptsbentries; i++) {
			tte = ldxa(obpptsb + i * 8, ASI_PHYS_USE_EC);
			if ((tte & IOTTE_V) == 0)
				continue;
			slot = tsbentries - obptsbentries + i;
			if (bootverbose)
				printf("%s: adding PROM IOTSB slot %d "
				    "(kernel slot %d) TTE: %#lx\n", name,
				    i, slot, tte);
			obpmap = (is->is_dvmabase + slot * IO_PAGE_SIZE) >>
			    IO_PAGE_SHIFT;
			if (rman_reserve_resource(&is->is_dvma_rman, obpmap,
			    obpmap, IO_PAGE_SIZE >> IO_PAGE_SHIFT, RF_ACTIVE,
			    NULL) == NULL)
				panic("%s: could not reserve PROM IOTSB slot "
				    "%d (kernel slot %d)", __func__, i, slot);
			is->is_tsb[slot] = tte;
		}
	}

	/*
	 * Initialize streaming buffer, if it is there.
	 */
	if (IOMMU_HAS_SB(is)) {
		/*
		 * Find two 64-byte blocks in is_flush that are aligned on
		 * a 64-byte boundary for flushing.
		 */
		offs = roundup2((vm_offset_t)is->is_flush,
		    STRBUF_FLUSHSYNC_NBYTES);
		for (i = 0; i < 2; i++, offs += STRBUF_FLUSHSYNC_NBYTES) {
			is->is_flushva[i] = (uint64_t *)offs;
			is->is_flushpa[i] = pmap_kextract(offs);
		}
	}

	/*
	 * Now actually start up the IOMMU.
	 */
	iommu_reset(is);
}

/*
 * Streaming buffers don't exist on the UltraSPARC IIi; we should have
 * detected that already and disabled them.  If not, we will notice that
 * they aren't there when the STRBUF_EN bit does not remain.
 */
void
iommu_reset(struct iommu_state *is)
{
	uint64_t tsb;
	int i;

	tsb = is->is_ptsb;
	if ((is->is_flags & IOMMU_FIRE) != 0) {
		tsb |= is->is_tsbsize;
		IOMMU_WRITE8(is, is_iommu, IMR_CACHE_INVAL, ~0ULL);
	}
	IOMMU_WRITE8(is, is_iommu, IMR_TSB, tsb);
	IOMMU_WRITE8(is, is_iommu, IMR_CTL, is->is_cr);

	for (i = 0; i < 2; i++) {
		if (is->is_sb[i] != 0) {
			IOMMU_WRITE8(is, is_sb[i], ISR_CTL, STRBUF_EN |
			    ((is->is_flags & IOMMU_RERUN_DISABLE) != 0 ?
			    STRBUF_RR_DIS : 0));

			/* No streaming buffers?  Disable them. */
			if ((IOMMU_READ8(is, is_sb[i], ISR_CTL) &
			    STRBUF_EN) == 0)
				is->is_sb[i] = 0;
		}
	}

	(void)IOMMU_READ8(is, is_iommu, IMR_CTL);
}

/*
 * Enter a mapping into the TSB.  No locking required, since each TSB slot is
 * uniquely assigned to a single map.
 */
static void
iommu_enter(struct iommu_state *is, vm_offset_t va, vm_paddr_t pa,
    int stream, int flags)
{
	uint64_t tte;

	KASSERT(va >= is->is_dvmabase,
	    ("%s: va %#lx not in DVMA space", __func__, va));
	KASSERT(pa <= is->is_pmaxaddr,
	    ("%s: XXX: physical address too large (%#lx)", __func__, pa));

	tte = MAKEIOTTE(pa, !(flags & BUS_DMA_NOWRITE),
	    !(flags & BUS_DMA_NOCACHE), stream);

	IOMMU_SET_TTE(is, va, tte);
	iommu_tlb_flush(is, va);
#ifdef IOMMU_DIAG
	IS_LOCK(is);
	iommu_diag(is, va);
	IS_UNLOCK(is);
#endif
}

/*
 * Remove mappings created by iommu_enter().  Flush the streaming buffer,
 * but do not synchronize it.  Returns whether a streaming buffer flush
 * was performed.
 */
static int
iommu_remove(struct iommu_state *is, vm_offset_t va, vm_size_t len)
{
	int slot, streamed = 0;

#ifdef IOMMU_DIAG
	iommu_diag(is, va);
#endif

	KASSERT(va >= is->is_dvmabase,
	    ("%s: va 0x%lx not in DVMA space", __func__, (u_long)va));
	KASSERT(va + len >= va,
	    ("%s: va 0x%lx + len 0x%lx wraps", __func__, (long)va, (long)len));

	va = trunc_io_page(va);
	while (len > 0) {
		if ((IOMMU_GET_TTE(is, va) & IOTTE_STREAM) != 0) {
			streamed = 1;
			iommu_strbuf_flush(is, va);
		}
		len -= ulmin(len, IO_PAGE_SIZE);
		IOMMU_SET_TTE(is, va, 0);
		iommu_tlb_flush(is, va);
		if ((is->is_flags & IOMMU_FLUSH_CACHE) != 0) {
			slot = IOTSBSLOT(va);
			if (len <= IO_PAGE_SIZE || slot % 8 == 7)
				IOMMU_WRITE8(is, is_iommu, IMR_CACHE_FLUSH,
				    is->is_ptsb + slot * 8);
		}
		va += IO_PAGE_SIZE;
	}
	return (streamed);
}

/* Decode an IOMMU fault for host bridge error handlers. */
void
iommu_decode_fault(struct iommu_state *is, vm_offset_t phys)
{
	bus_addr_t va;
	long idx;

	idx = phys - is->is_ptsb;
	if (phys < is->is_ptsb ||
	    idx > (PAGE_SIZE << is->is_tsbsize))
		return;
	va = is->is_dvmabase +
	    (((bus_addr_t)idx >> IOTTE_SHIFT) << IO_PAGE_SHIFT);
	printf("IOMMU fault virtual address %#lx\n", (u_long)va);
}

/*
 * A barrier operation which makes sure that all previous streaming buffer
 * flushes complete before it returns.
 */
static int
iommu_strbuf_flush_sync(struct iommu_state *is)
{
	struct timeval cur, end;
	int i;

	IS_LOCK_ASSERT(is);
	if (!IOMMU_HAS_SB(is))
		return (0);

	/*
	 * Streaming buffer flushes:
	 *
	 *   1 Tell strbuf to flush by storing va to strbuf_pgflush.  If
	 *     we're not on a cache line boundary (64-bits):
	 *   2 Store 0 in flag
	 *   3 Store pointer to flag in flushsync
	 *   4 wait till flushsync becomes 0x1
	 *
	 * If it takes more than .5 sec, something went wrong.
	 */
	*is->is_flushva[0] = 1;
	*is->is_flushva[1] = 1;
	membar(StoreStore);
	for (i = 0; i < 2; i++) {
		if (is->is_sb[i] != 0) {
			*is->is_flushva[i] = 0;
			IOMMU_WRITE8(is, is_sb[i], ISR_FLUSHSYNC,
			    is->is_flushpa[i]);
		}
	}

	microuptime(&cur);
	end.tv_sec = 0;
	/*
	 * 0.5s is the recommended timeout from the U2S manual.  The actual
	 * time required should be smaller by at least a factor of 1000.
	 * We have no choice but to busy-wait.
	 */
	end.tv_usec = 500000;
	timevaladd(&end, &cur);

	while ((!*is->is_flushva[0] || !*is->is_flushva[1]) &&
	    timevalcmp(&cur, &end, <=))
		microuptime(&cur);

	if (!*is->is_flushva[0] || !*is->is_flushva[1]) {
		panic("%s: flush timeout %ld, %ld at %#lx", __func__,
		    *is->is_flushva[0], *is->is_flushva[1], is->is_flushpa[0]);
	}

	return (1);
}

/* Determine whether we may enable streaming on a mapping. */
static __inline int
iommu_use_streaming(struct iommu_state *is, bus_dmamap_t map, bus_size_t size)
{

	return (size >= IOMMU_STREAM_THRESH && IOMMU_HAS_SB(is) &&
	    (map->dm_flags & DMF_COHERENT) == 0);
}

/*
 * Allocate DVMA virtual memory for a map.  The map may not be on a queue,
 * so that it can be freely modified.
 */
static int
iommu_dvma_valloc(bus_dma_tag_t t, struct iommu_state *is, bus_dmamap_t map,
    bus_size_t size)
{
	struct resource *res;
	struct bus_dmamap_res *bdr;
	bus_size_t align, sgsize;

	KASSERT(!map->dm_onq, ("%s: map on queue!", __func__));
	if ((bdr = malloc(sizeof(*bdr), M_IOMMU, M_NOWAIT)) == NULL)
		return (EAGAIN);
	/*
	 * If a boundary is specified, a map cannot be larger than it; however
	 * we do not clip currently, as that does not play well with the lazy
	 * allocation code.
	 * Alignment to a page boundary is always enforced.
	 */
	align = (t->dt_alignment + IO_PAGE_MASK) >> IO_PAGE_SHIFT;
	sgsize = round_io_page(size) >> IO_PAGE_SHIFT;
	if (t->dt_boundary > 0 && t->dt_boundary < IO_PAGE_SIZE)
		panic("%s: illegal boundary specified", __func__);
	res = rman_reserve_resource_bound(&is->is_dvma_rman, 0L,
	    t->dt_lowaddr >> IO_PAGE_SHIFT, sgsize,
	    t->dt_boundary >> IO_PAGE_SHIFT,
	    RF_ACTIVE | rman_make_alignment_flags(align), NULL);
	if (res == NULL) {
		free(bdr, M_IOMMU);
		return (ENOMEM);
	}

	bdr->dr_res = res;
	bdr->dr_used = 0;
	SLIST_INSERT_HEAD(&map->dm_reslist, bdr, dr_link);
	return (0);
}

/* Unload the map and mark all resources as unused, but do not free them. */
static void
iommu_dvmamap_vunload(struct iommu_state *is, bus_dmamap_t map)
{
	struct bus_dmamap_res *r;
	int streamed = 0;

	IS_LOCK_ASSERT(is);	/* for iommu_strbuf_sync() below */
	SLIST_FOREACH(r, &map->dm_reslist, dr_link) {
		streamed |= iommu_remove(is, BDR_START(r), r->dr_used);
		r->dr_used = 0;
	}
	if (streamed)
		iommu_strbuf_sync(is);
}

/* Free a DVMA virtual memory resource. */
static __inline void
iommu_dvma_vfree_res(bus_dmamap_t map, struct bus_dmamap_res *r)
{

	KASSERT(r->dr_used == 0, ("%s: resource busy!", __func__));
	if (r->dr_res != NULL && rman_release_resource(r->dr_res) != 0)
		printf("warning: DVMA space lost\n");
	SLIST_REMOVE(&map->dm_reslist, r, bus_dmamap_res, dr_link);
	free(r, M_IOMMU);
}

/* Free all DVMA virtual memory for a map. */
static void
iommu_dvma_vfree(struct iommu_state *is, bus_dmamap_t map)
{

	IS_LOCK(is);
	iommu_map_remq(is, map);
	iommu_dvmamap_vunload(is, map);
	IS_UNLOCK(is);
	while (!SLIST_EMPTY(&map->dm_reslist))
		iommu_dvma_vfree_res(map, SLIST_FIRST(&map->dm_reslist));
}

/* Prune a map, freeing all unused DVMA resources. */
static bus_size_t
iommu_dvma_vprune(struct iommu_state *is, bus_dmamap_t map)
{
	struct bus_dmamap_res *r, *n;
	bus_size_t freed = 0;

	IS_LOCK_ASSERT(is);
	for (r = SLIST_FIRST(&map->dm_reslist); r != NULL; r = n) {
		n = SLIST_NEXT(r, dr_link);
		if (r->dr_used == 0) {
			freed += BDR_SIZE(r);
			iommu_dvma_vfree_res(map, r);
		}
	}
	if (SLIST_EMPTY(&map->dm_reslist))
		iommu_map_remq(is, map);
	return (freed);
}

/*
 * Try to find a suitably-sized (and if requested, -aligned) slab of DVMA
 * memory with IO page offset voffs.
 */
static bus_addr_t
iommu_dvma_vfindseg(bus_dmamap_t map, vm_offset_t voffs, bus_size_t size,
    bus_addr_t amask)
{
	struct bus_dmamap_res *r;
	bus_addr_t dvmaddr, dvmend;

	KASSERT(!map->dm_onq, ("%s: map on queue!", __func__));
	SLIST_FOREACH(r, &map->dm_reslist, dr_link) {
		dvmaddr = round_io_page(BDR_START(r) + r->dr_used);
		/* Alignment can only work with voffs == 0. */
		dvmaddr = (dvmaddr + amask) & ~amask;
		dvmaddr += voffs;
		dvmend = dvmaddr + size;
		if (dvmend <= BDR_END(r)) {
			r->dr_used = dvmend - BDR_START(r);
			return (dvmaddr);
		}
	}
	return (0);
}

/*
 * Try to find or allocate a slab of DVMA space; see above.
 */
static int
iommu_dvma_vallocseg(bus_dma_tag_t dt, struct iommu_state *is, bus_dmamap_t map,
    vm_offset_t voffs, bus_size_t size, bus_addr_t amask, bus_addr_t *addr)
{
	bus_dmamap_t tm, last;
	bus_addr_t dvmaddr, freed;
	int error, complete = 0;

	dvmaddr = iommu_dvma_vfindseg(map, voffs, size, amask);

	/* Need to allocate. */
	if (dvmaddr == 0) {
		while ((error = iommu_dvma_valloc(dt, is, map,
			voffs + size)) == ENOMEM && !complete) {
			/*
			 * Free the allocated DVMA of a few maps until
			 * the required size is reached. This is an
			 * approximation to not have to call the allocation
			 * function too often; most likely one free run
			 * will not suffice if not one map was large enough
			 * itself due to fragmentation.
			 */
			IS_LOCK(is);
			freed = 0;
			last = TAILQ_LAST(&is->is_maplruq, iommu_maplruq_head);
			do {
				tm = TAILQ_FIRST(&is->is_maplruq);
				complete = tm == last;
				if (tm == NULL)
					break;
				freed += iommu_dvma_vprune(is, tm);
				/* Move to the end. */
				iommu_map_insq(is, tm);
			} while (freed < size && !complete);
			IS_UNLOCK(is);
		}
		if (error != 0)
			return (error);
		dvmaddr = iommu_dvma_vfindseg(map, voffs, size, amask);
		KASSERT(dvmaddr != 0, ("%s: allocation failed unexpectedly!",
		    __func__));
	}
	*addr = dvmaddr;
	return (0);
}

static int
iommu_dvmamem_alloc(bus_dma_tag_t dt, void **vaddr, int flags,
    bus_dmamap_t *mapp)
{
	struct iommu_state *is = dt->dt_cookie;
	int error, mflags;

	/*
	 * XXX: This will break for 32 bit transfers on machines with more
	 * than is->is_pmaxaddr memory.
	 */
	if ((error = sparc64_dma_alloc_map(dt, mapp)) != 0)
		return (error);

	if ((flags & BUS_DMA_NOWAIT) != 0)
		mflags = M_NOWAIT;
	else
		mflags = M_WAITOK;
	if ((flags & BUS_DMA_ZERO) != 0)
		mflags |= M_ZERO;

	if ((*vaddr = malloc(dt->dt_maxsize, M_IOMMU, mflags)) == NULL) {
		error = ENOMEM;
		sparc64_dma_free_map(dt, *mapp);
		return (error);
	}
	if ((flags & BUS_DMA_COHERENT) != 0)
		(*mapp)->dm_flags |= DMF_COHERENT;
	/*
	 * Try to preallocate DVMA space.  If this fails, it is retried at
	 * load time.
	 */
	iommu_dvma_valloc(dt, is, *mapp, IOMMU_SIZE_ROUNDUP(dt->dt_maxsize));
	IS_LOCK(is);
	iommu_map_insq(is, *mapp);
	IS_UNLOCK(is);
	return (0);
}

static void
iommu_dvmamem_free(bus_dma_tag_t dt, void *vaddr, bus_dmamap_t map)
{
	struct iommu_state *is = dt->dt_cookie;

	iommu_dvma_vfree(is, map);
	sparc64_dma_free_map(dt, map);
	free(vaddr, M_IOMMU);
}

static int
iommu_dvmamap_create(bus_dma_tag_t dt, int flags, bus_dmamap_t *mapp)
{
	struct iommu_state *is = dt->dt_cookie;
	bus_size_t totsz, presz, currsz;
	int error, i, maxpre;

	if ((error = sparc64_dma_alloc_map(dt, mapp)) != 0)
		return (error);
	if ((flags & BUS_DMA_COHERENT) != 0)
		(*mapp)->dm_flags |= DMF_COHERENT;
	/*
	 * Preallocate DVMA space; if this fails now, it is retried at load
	 * time.  Through bus_dmamap_load_mbuf() and bus_dmamap_load_uio(),
	 * it is possible to have multiple discontiguous segments in a single
	 * map, which is handled by allocating additional resources, instead
	 * of increasing the size, to avoid fragmentation.
	 * Clamp preallocation to IOMMU_MAX_PRE.  In some situations we can
	 * handle more; that case is handled by reallocating at map load time.
	 */
	totsz = ulmin(IOMMU_SIZE_ROUNDUP(dt->dt_maxsize), IOMMU_MAX_PRE);
	error = iommu_dvma_valloc(dt, is, *mapp, totsz);
	if (error != 0)
		return (0);
	/*
	 * Try to be smart about preallocating some additional segments if
	 * needed.
	 */
	maxpre = imin(dt->dt_nsegments, IOMMU_MAX_PRE_SEG);
	presz = dt->dt_maxsize / maxpre;
	for (i = 1; i < maxpre && totsz < IOMMU_MAX_PRE; i++) {
		currsz = round_io_page(ulmin(presz, IOMMU_MAX_PRE - totsz));
		error = iommu_dvma_valloc(dt, is, *mapp, currsz);
		if (error != 0)
			break;
		totsz += currsz;
	}
	IS_LOCK(is);
	iommu_map_insq(is, *mapp);
	IS_UNLOCK(is);
	return (0);
}

static int
iommu_dvmamap_destroy(bus_dma_tag_t dt, bus_dmamap_t map)
{
	struct iommu_state *is = dt->dt_cookie;

	iommu_dvma_vfree(is, map);
	sparc64_dma_free_map(dt, map);
	return (0);
}

/*
 * Utility function to load a physical buffer.  segp contains
 * the starting segment on entrace, and the ending segment on exit.
 */
static int
iommu_dvmamap_load_phys(bus_dma_tag_t dt, bus_dmamap_t map, vm_paddr_t buf,
    bus_size_t buflen, int flags, bus_dma_segment_t *segs, int *segp)
{
	bus_addr_t amask, dvmaddr, dvmoffs;
	bus_size_t sgsize, esize;
	struct iommu_state *is;
	vm_offset_t voffs;
	vm_paddr_t curaddr;
	int error, firstpg, sgcnt;
	u_int slot;

	is = dt->dt_cookie;
	if (*segp == -1) {
		if ((map->dm_flags & DMF_LOADED) != 0) {
#ifdef DIAGNOSTIC
			printf("%s: map still in use\n", __func__);
#endif
			bus_dmamap_unload(dt, map);
		}

		/*
		 * Make sure that the map is not on a queue so that the
		 * resource list may be safely accessed and modified without
		 * needing the lock to cover the whole operation.
		 */
		IS_LOCK(is);
		iommu_map_remq(is, map);
		IS_UNLOCK(is);

		amask = dt->dt_alignment - 1;
	} else
		amask = 0;
	KASSERT(buflen != 0, ("%s: buflen == 0!", __func__));
	if (buflen > dt->dt_maxsize)
		return (EINVAL);

	if (segs == NULL)
		segs = dt->dt_segments;

	voffs = buf & IO_PAGE_MASK;

	/* Try to find a slab that is large enough. */
	error = iommu_dvma_vallocseg(dt, is, map, voffs, buflen, amask,
	    &dvmaddr);
	if (error != 0)
		return (error);

	sgcnt = *segp;
	firstpg = 1;
	map->dm_flags &= ~DMF_STREAMED;
	map->dm_flags |= iommu_use_streaming(is, map, buflen) != 0 ?
	    DMF_STREAMED : 0;
	for (; buflen > 0; ) {
		curaddr = buf;

		/*
		 * Compute the segment size, and adjust counts.
		 */
		sgsize = IO_PAGE_SIZE - ((u_long)buf & IO_PAGE_MASK);
		if (buflen < sgsize)
			sgsize = buflen;

		buflen -= sgsize;
		buf += sgsize;

		dvmoffs = trunc_io_page(dvmaddr);
		iommu_enter(is, dvmoffs, trunc_io_page(curaddr),
		    (map->dm_flags & DMF_STREAMED) != 0, flags);
		if ((is->is_flags & IOMMU_FLUSH_CACHE) != 0) {
			slot = IOTSBSLOT(dvmoffs);
			if (buflen <= 0 || slot % 8 == 7)
				IOMMU_WRITE8(is, is_iommu, IMR_CACHE_FLUSH,
				    is->is_ptsb + slot * 8);
		}

		/*
		 * Chop the chunk up into segments of at most maxsegsz, but try
		 * to fill each segment as well as possible.
		 */
		if (!firstpg) {
			esize = ulmin(sgsize,
			    dt->dt_maxsegsz - segs[sgcnt].ds_len);
			segs[sgcnt].ds_len += esize;
			sgsize -= esize;
			dvmaddr += esize;
		}
		while (sgsize > 0) {
			sgcnt++;
			if (sgcnt >= dt->dt_nsegments)
				return (EFBIG);
			/*
			 * No extra alignment here - the common practice in
			 * the busdma code seems to be that only the first
			 * segment needs to satisfy the alignment constraints
			 * (and that only for bus_dmamem_alloc()ed maps).
			 * It is assumed that such tags have maxsegsize >=
			 * maxsize.
			 */
			esize = ulmin(sgsize, dt->dt_maxsegsz);
			segs[sgcnt].ds_addr = dvmaddr;
			segs[sgcnt].ds_len = esize;
			sgsize -= esize;
			dvmaddr += esize;
		}

		firstpg = 0;
	}
	*segp = sgcnt;
	return (0);
}

/*
 * IOMMU DVMA operations, common to PCI and SBus
 */
static int
iommu_dvmamap_load_buffer(bus_dma_tag_t dt, bus_dmamap_t map, void *buf,
    bus_size_t buflen, pmap_t pmap, int flags, bus_dma_segment_t *segs,
    int *segp)
{
	bus_addr_t amask, dvmaddr, dvmoffs;
	bus_size_t sgsize, esize;
	struct iommu_state *is;
	vm_offset_t vaddr, voffs;
	vm_paddr_t curaddr;
	int error, firstpg, sgcnt;
	u_int slot;

	is = dt->dt_cookie;
	if (*segp == -1) {
		if ((map->dm_flags & DMF_LOADED) != 0) {
#ifdef DIAGNOSTIC
			printf("%s: map still in use\n", __func__);
#endif
			bus_dmamap_unload(dt, map);
		}

		/*
		 * Make sure that the map is not on a queue so that the
		 * resource list may be safely accessed and modified without
		 * needing the lock to cover the whole operation.
		 */
		IS_LOCK(is);
		iommu_map_remq(is, map);
		IS_UNLOCK(is);

		amask = dt->dt_alignment - 1;
	} else
		amask = 0;
	KASSERT(buflen != 0, ("%s: buflen == 0!", __func__));
	if (buflen > dt->dt_maxsize)
		return (EINVAL);

	if (segs == NULL)
		segs = dt->dt_segments;

	vaddr = (vm_offset_t)buf;
	voffs = vaddr & IO_PAGE_MASK;

	/* Try to find a slab that is large enough. */
	error = iommu_dvma_vallocseg(dt, is, map, voffs, buflen, amask,
	    &dvmaddr);
	if (error != 0)
		return (error);

	sgcnt = *segp;
	firstpg = 1;
	map->dm_flags &= ~DMF_STREAMED;
	map->dm_flags |= iommu_use_streaming(is, map, buflen) != 0 ?
	    DMF_STREAMED : 0;
	for (; buflen > 0; ) {
		/*
		 * Get the physical address for this page.
		 */
		if (pmap == kernel_pmap)
			curaddr = pmap_kextract(vaddr);
		else
			curaddr = pmap_extract(pmap, vaddr);

		/*
		 * Compute the segment size, and adjust counts.
		 */
		sgsize = IO_PAGE_SIZE - ((u_long)vaddr & IO_PAGE_MASK);
		if (buflen < sgsize)
			sgsize = buflen;

		buflen -= sgsize;
		vaddr += sgsize;

		dvmoffs = trunc_io_page(dvmaddr);
		iommu_enter(is, dvmoffs, trunc_io_page(curaddr),
		    (map->dm_flags & DMF_STREAMED) != 0, flags);
		if ((is->is_flags & IOMMU_FLUSH_CACHE) != 0) {
			slot = IOTSBSLOT(dvmoffs);
			if (buflen <= 0 || slot % 8 == 7)
				IOMMU_WRITE8(is, is_iommu, IMR_CACHE_FLUSH,
				    is->is_ptsb + slot * 8);
		}

		/*
		 * Chop the chunk up into segments of at most maxsegsz, but try
		 * to fill each segment as well as possible.
		 */
		if (!firstpg) {
			esize = ulmin(sgsize,
			    dt->dt_maxsegsz - segs[sgcnt].ds_len);
			segs[sgcnt].ds_len += esize;
			sgsize -= esize;
			dvmaddr += esize;
		}
		while (sgsize > 0) {
			sgcnt++;
			if (sgcnt >= dt->dt_nsegments)
				return (EFBIG);
			/*
			 * No extra alignment here - the common practice in
			 * the busdma code seems to be that only the first
			 * segment needs to satisfy the alignment constraints
			 * (and that only for bus_dmamem_alloc()ed maps).
			 * It is assumed that such tags have maxsegsize >=
			 * maxsize.
			 */
			esize = ulmin(sgsize, dt->dt_maxsegsz);
			segs[sgcnt].ds_addr = dvmaddr;
			segs[sgcnt].ds_len = esize;
			sgsize -= esize;
			dvmaddr += esize;
		}

		firstpg = 0;
	}
	*segp = sgcnt;
	return (0);
}

static void
iommu_dvmamap_waitok(bus_dma_tag_t dmat, bus_dmamap_t map,
    struct memdesc *mem, bus_dmamap_callback_t *callback, void *callback_arg)
{
}

static bus_dma_segment_t *
iommu_dvmamap_complete(bus_dma_tag_t dt, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, int error)
{
	struct iommu_state *is = dt->dt_cookie;

	IS_LOCK(is);
	iommu_map_insq(is, map);
	if (error != 0) {
		iommu_dvmamap_vunload(is, map);
		IS_UNLOCK(is);
	} else {
		IS_UNLOCK(is);
		map->dm_flags |= DMF_LOADED;
	}
	if (segs == NULL)
		segs = dt->dt_segments;
	return (segs);
}

static void
iommu_dvmamap_unload(bus_dma_tag_t dt, bus_dmamap_t map)
{
	struct iommu_state *is = dt->dt_cookie;

	if ((map->dm_flags & DMF_LOADED) == 0)
		return;
	IS_LOCK(is);
	iommu_dvmamap_vunload(is, map);
	iommu_map_insq(is, map);
	IS_UNLOCK(is);
	map->dm_flags &= ~DMF_LOADED;
}

static void
iommu_dvmamap_sync(bus_dma_tag_t dt, bus_dmamap_t map, bus_dmasync_op_t op)
{
	struct iommu_state *is = dt->dt_cookie;
	struct bus_dmamap_res *r;
	vm_offset_t va;
	vm_size_t len;
	int streamed = 0;

	if ((map->dm_flags & DMF_LOADED) == 0)
		return;
	if ((map->dm_flags & DMF_STREAMED) != 0 &&
	    ((op & BUS_DMASYNC_POSTREAD) != 0 ||
	    (op & BUS_DMASYNC_PREWRITE) != 0)) {
		IS_LOCK(is);
		SLIST_FOREACH(r, &map->dm_reslist, dr_link) {
			va = (vm_offset_t)BDR_START(r);
			len = r->dr_used;
			/*
			 * If we have a streaming buffer, flush it here
			 * first.
			 */
			while (len > 0) {
				if ((IOMMU_GET_TTE(is, va) &
				    IOTTE_STREAM) != 0) {
					streamed = 1;
					iommu_strbuf_flush(is, va);
				}
				len -= ulmin(len, IO_PAGE_SIZE);
				va += IO_PAGE_SIZE;
			}
		}
		if (streamed)
			iommu_strbuf_sync(is);
		IS_UNLOCK(is);
	}
	if ((op & BUS_DMASYNC_PREWRITE) != 0)
		membar(Sync);
}

#ifdef IOMMU_DIAG

/*
 * Perform an IOMMU diagnostic access and print the tag belonging to va.
 */
static void
iommu_diag(struct iommu_state *is, vm_offset_t va)
{
	int i;
	uint64_t data, tag;

	if ((is->is_flags & IOMMU_FIRE) != 0)
		return;
	IS_LOCK_ASSERT(is);
	IOMMU_WRITE8(is, is_dva, 0, trunc_io_page(va));
	membar(StoreStore | StoreLoad);
	printf("%s: tte entry %#lx", __func__, IOMMU_GET_TTE(is, va));
	if (is->is_dtcmp != 0) {
		printf(", tag compare register is %#lx\n",
		    IOMMU_READ8(is, is_dtcmp, 0));
	} else
		printf("\n");
	for (i = 0; i < 16; i++) {
		tag = IOMMU_READ8(is, is_dtag, i * 8);
		data = IOMMU_READ8(is, is_ddram, i * 8);
		printf("%s: tag %d: %#lx, vpn %#lx, err %lx; "
		    "data %#lx, pa %#lx, v %d, c %d\n", __func__, i,
		    tag, (tag & IOMMU_DTAG_VPNMASK) << IOMMU_DTAG_VPNSHIFT,
		    (tag & IOMMU_DTAG_ERRMASK) >> IOMMU_DTAG_ERRSHIFT, data,
		    (data & IOMMU_DDATA_PGMASK) << IOMMU_DDATA_PGSHIFT,
		    (data & IOMMU_DDATA_V) != 0, (data & IOMMU_DDATA_C) != 0);
	}
}

#endif /* IOMMU_DIAG */

struct bus_dma_methods iommu_dma_methods = {
	iommu_dvmamap_create,
	iommu_dvmamap_destroy,
	iommu_dvmamap_load_phys,
	iommu_dvmamap_load_buffer,
	iommu_dvmamap_waitok,
	iommu_dvmamap_complete,
	iommu_dvmamap_unload,
	iommu_dvmamap_sync,
	iommu_dvmamem_alloc,
	iommu_dvmamem_free,
};
