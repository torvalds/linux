/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Matthew R. Green
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
 *	from: NetBSD: iommuvar.h,v 1.6 2008/05/29 14:51:26 mrg Exp
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_IOMMUVAR_H_
#define	_MACHINE_IOMMUVAR_H_

#define	IO_PAGE_SIZE		PAGE_SIZE_8K
#define	IO_PAGE_MASK		PAGE_MASK_8K
#define	IO_PAGE_SHIFT		PAGE_SHIFT_8K
#define	round_io_page(x)	round_page(x)
#define	trunc_io_page(x)	trunc_page(x)

/*
 * LRU queue handling for lazy resource allocation
 */
TAILQ_HEAD(iommu_maplruq_head, bus_dmamap);

/*
 * Per-IOMMU state; the parenthesized comments indicate the locking strategy:
 *	i - protected by is_mtx.
 *	r - read-only after initialization.
 *	* - comment refers to pointer target / target hardware registers
 *	    (for bus_addr_t).
 * is_maplruq is also locked by is_mtx.  Elements of is_tsb may only be
 * accessed from functions operating on the map owning the corresponding
 * resource, so the locking the user is required to do to protect the
 * map is sufficient.
 * dm_reslist of all maps are locked by is_mtx as well.
 * is_dvma_rman has its own internal lock.
 */
struct iommu_state {
	struct mtx		is_mtx;
	struct rman		is_dvma_rman;	/* DVMA space rman */
	struct iommu_maplruq_head is_maplruq;	/* (i) LRU queue */
	vm_paddr_t		is_ptsb;	/* (r) TSB physical address */
	uint64_t		*is_tsb;	/* (*i) TSB virtual address */
	int			is_tsbsize;	/* (r) 0 = 8K, ... */
	uint64_t		is_pmaxaddr;	/* (r) max. physical address */
	uint64_t		is_dvmabase;	/* (r) */
	uint64_t		is_cr;		/* (r) Control reg value */

	vm_paddr_t		is_flushpa[2];	/* (r) */
	volatile uint64_t	*is_flushva[2];	/* (r, *i) */
	/*
	 * (i)
	 * When a flush is completed, 64 bytes will be stored at the given
	 * location, the first double word being 1, to indicate completion.
	 * The lower 6 address bits are ignored, so the addresses need to be
	 * suitably aligned; over-allocate a large enough margin to be able
	 * to adjust it.
	 * Two such buffers are needed.
	 */
	volatile char		is_flush[STRBUF_FLUSHSYNC_NBYTES * 3 - 1];

	/* copies of our parent's state, to allow us to be self contained */
	bus_space_tag_t		is_bustag;	/* (r) Our bus tag */
	bus_space_handle_t	is_bushandle;	/* (r) */
	bus_addr_t		is_iommu;	/* (r, *i) IOMMU registers */
	bus_addr_t		is_sb[2];	/* (r, *i) Streaming buffer */
	/* Tag diagnostics access */
	bus_addr_t		is_dtag;	/* (r, *r) */
	/* Data RAM diagnostic access */
	bus_addr_t		is_ddram;	/* (r, *r) */
	/* LRU queue diag. access */
	bus_addr_t		is_dqueue;	/* (r, *r) */
	/* Virtual address diagnostics register */
	bus_addr_t		is_dva;		/* (r, *r) */
	/* Tag compare diagnostics access */
	bus_addr_t		is_dtcmp;	/* (r, *r) */
	/* behavior flags */
	u_int			is_flags;	/* (r) */
#define	IOMMU_RERUN_DISABLE	(1 << 0)
#define	IOMMU_FIRE		(1 << 1)
#define	IOMMU_FLUSH_CACHE	(1 << 2)
#define	IOMMU_PRESERVE_PROM	(1 << 3)
};

/* interfaces for PCI/SBus code */
void iommu_init(const char *name, struct iommu_state *is, u_int tsbsize,
    uint32_t iovabase, u_int resvpg);
void iommu_reset(struct iommu_state *is);
void iommu_decode_fault(struct iommu_state *is, vm_offset_t phys);

extern struct bus_dma_methods iommu_dma_methods;

#endif /* !_MACHINE_IOMMUVAR_H_ */
