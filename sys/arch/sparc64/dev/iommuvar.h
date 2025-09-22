/*	$OpenBSD: iommuvar.h,v 1.19 2021/03/11 11:17:00 jsg Exp $	*/
/*	$NetBSD: iommuvar.h,v 1.9 2001/10/07 20:30:41 eeh Exp $	*/

/*
 * Copyright (c) 2003 Henric Jungheim
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
 */

#ifndef _SPARC64_DEV_IOMMUVAR_H_
#define _SPARC64_DEV_IOMMUVAR_H_

#ifndef _SYS_TREE_H_
#include <sys/tree.h>
#endif

#include <sys/extent.h>
#include <sys/mutex.h>

/*
 * per-Streaming Buffer state
 */
struct strbuf_ctl {
	bus_space_tag_t		sb_bustag;	/* streaming buffer registers */
	bus_space_handle_t	sb_sb;		/* Handle for our regs */
	struct iommu_state	*sb_iommu;	/* Associated IOMMU */
	/*
	 * Since implementing the per-map IOMMU state, these per-STC
	 * flush areas are not used other than as a boolean flag to indicate
	 * the presence of a working and enabled STC.  For inconsistency's
	 * sake, the "sb" pointers of iommu_state are sometimes used for the
	 * same purpose.  This should be consolidated.  DEFINITELY, since
	 * mutex operations must happen at this level.
	 */
	struct mutex		sb_mtx;		/* one flush at a time */

	paddr_t			sb_flushpa;	/* to flush streaming buffers */
	volatile int64_t	*sb_flush;
};

/*
 * per-map STC flush area
 */
struct strbuf_flush {
	char	sbf_area[0x80];		/* Holds 64-byte long/aligned buffer */
	void	*sbf_flush;		/* Kernel virtual address of buffer */
	paddr_t	sbf_flushpa;		/* Physical address of buffer area */
};

/* 
 * per-map DVMA page table
 */
struct iommu_page_entry {
	SPLAY_ENTRY(iommu_page_entry) ipe_node;
	paddr_t		ipe_pa;
	bus_addr_t	ipe_va;
};
struct iommu_page_map {
	SPLAY_HEAD(iommu_page_tree, iommu_page_entry) ipm_tree;
	int ipm_maxpage;	/* Size of allocated page map */
	int ipm_pagecnt;	/* Number of entries in use */
	struct iommu_page_entry	ipm_map[1];
};

/*
 * per-map IOMMU state
 *
 * This is what bus_dvmamap_t'c _dm_cookie should be pointing to.
 */
struct iommu_map_state {
	struct strbuf_flush ims_flush;	/* flush should be first (alignment) */
	struct strbuf_ctl *ims_sb;	/* Link to parent */
	struct iommu_state *ims_iommu;
	int ims_flags;
	struct extent_region ims_er;
	struct iommu_page_map ims_map;	/* map must be last (array at end) */
};
#define IOMMU_MAP_STREAM	1

struct iommu_hw {
	void			(*ihw_enable)(struct iommu_state *);

	unsigned long		ihw_dvma_pa;

	unsigned long		ihw_bypass;
	unsigned long		ihw_bypass_nc;		/* non-cached */
	unsigned long		ihw_bypass_ro;		/* relaxed ordering */

	unsigned int		ihw_flags;
#define IOMMU_HW_FLUSH_CACHE		(1 << 0)
};

extern const struct iommu_hw iommu_hw_default;

/*
 * per-IOMMU state
 */
struct iommu_state {
	paddr_t			is_ptsb;	/* TSB physical address */
	int64_t			*is_tsb;	/* TSB virtual address */
	int			is_tsbsize;	/* 0 = 8K, ... */
	u_int			is_dvmabase;
	u_int			is_dvmaend;
	int64_t			is_cr;		/* Control register value */
	struct mutex		is_mtx;
	struct extent		*is_dvmamap;	/* DVMA map for this instance */
	const struct iommu_hw	*is_hw;

	struct strbuf_ctl	*is_sb[2];	/* Streaming buffers if any */

	paddr_t			is_scratch;	/* Scratch page */

	/* copies of our parents state, to allow us to be self contained */
	bus_space_tag_t		is_bustag;	/* our bus tag */
	bus_space_handle_t	is_iommu;	/* IOMMU registers */
	uint64_t		is_devhandle;
};

/* interfaces for PCI/SBus code */
void	iommu_init(char *, const struct iommu_hw *, struct iommu_state *,
    int, u_int32_t);
void	iommu_reset(struct iommu_state *);
paddr_t iommu_extract(struct iommu_state *, bus_addr_t);
int64_t iommu_lookup_tte(struct iommu_state *, bus_addr_t);
int64_t iommu_fetch_tte(struct iommu_state *, paddr_t);
/* bus_dma_tag_t implementation functions */
int	iommu_dvmamap_create(bus_dma_tag_t, bus_dma_tag_t, struct strbuf_ctl *,
	    bus_size_t, int, bus_size_t, bus_size_t, int, bus_dmamap_t *);
void	iommu_dvmamap_destroy(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t);
int	iommu_dvmamap_load(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t, void *,
	    bus_size_t, struct proc *, int);
void	iommu_dvmamap_unload(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t);
int	iommu_dvmamap_load_raw(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t,
	    bus_dma_segment_t *, int, bus_size_t, int);
void	iommu_dvmamap_sync(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t,
	    bus_addr_t, bus_size_t, int);
int	iommu_dvmamem_alloc(bus_dma_tag_t, bus_dma_tag_t, bus_size_t,
	    bus_size_t, bus_size_t, bus_dma_segment_t *, int, int *, int);
void	iommu_dvmamem_free(bus_dma_tag_t, bus_dma_tag_t, bus_dma_segment_t *,
	    int);

#define IOMMUREG_READ(is, reg)				\
	bus_space_read_8((is)->is_bustag,		\
		(is)->is_iommu,				\
		IOMMUREG(reg))	

#define IOMMUREG_WRITE(is, reg, v)			\
	bus_space_write_8((is)->is_bustag,		\
		(is)->is_iommu,				\
		IOMMUREG(reg),				\
		(v))

#endif /* _SPARC64_DEV_IOMMUVAR_H_ */

