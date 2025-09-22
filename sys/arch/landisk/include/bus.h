/*	$OpenBSD: bus.h,v 1.10 2022/01/04 20:41:44 deraadt Exp $	*/
/*	$NetBSD: bus.h,v 1.1 2006/09/01 21:26:18 uwe Exp $	*/

/*-
 * Copyright (c) 1996, 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1996 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MACHINE_BUS_H_
#define	_MACHINE_BUS_H_

#ifdef _KERNEL

#include <sys/types.h>

typedef	u_long	bus_addr_t;
typedef	u_long	bus_size_t;

typedef struct _bus_space *bus_space_tag_t;
typedef u_long bus_space_handle_t;

struct _bus_space {
	/* cookie */
	void		*bs_cookie;

	/* mapping/unmapping */
	int		(*bs_map)(void *, bus_addr_t, bus_size_t,
			    int, bus_space_handle_t *);
	void		(*bs_unmap)(void *, bus_space_handle_t,
			    bus_size_t);
	int		(*bs_subregion)(void *, bus_space_handle_t,
			    bus_size_t, bus_size_t, bus_space_handle_t *);

	/* allocation/deallocation */
	int		(*bs_alloc)(void *, bus_addr_t, bus_addr_t,
			    bus_size_t, bus_size_t, bus_size_t, int,
			    bus_addr_t *, bus_space_handle_t *);
	void		(*bs_free)(void *, bus_space_handle_t,
			    bus_size_t);

	/* get kernel virtual address */
	void *		(*bs_vaddr)(void *, bus_space_handle_t);

	/* read (single) */
	uint8_t		(*bs_r_1)(void *, bus_space_handle_t,
			    bus_size_t);
	uint16_t	(*bs_r_2)(void *, bus_space_handle_t,
			    bus_size_t);
	uint32_t	(*bs_r_4)(void *, bus_space_handle_t,
			    bus_size_t);
	uint64_t	(*bs_r_8)(void *, bus_space_handle_t,
			    bus_size_t);

	/* read multiple */
	void		(*bs_rm_1)(void *, bus_space_handle_t,
			    bus_size_t, uint8_t *, bus_size_t);
	void		(*bs_rm_2)(void *, bus_space_handle_t,
			    bus_size_t, uint16_t *, bus_size_t);
	void		(*bs_rm_4)(void *, bus_space_handle_t,
			    bus_size_t, uint32_t *, bus_size_t);
	void		(*bs_rm_8)(void *, bus_space_handle_t,
			    bus_size_t, uint64_t *, bus_size_t);

	void		(*bs_rrm_2)(void *, bus_space_handle_t,
			    bus_size_t, uint8_t *, bus_size_t);
	void		(*bs_rrm_4)(void *, bus_space_handle_t,
			    bus_size_t, uint8_t *, bus_size_t);
	void		(*bs_rrm_8)(void *, bus_space_handle_t,
			    bus_size_t, uint8_t *, bus_size_t);

	/* read region */
	void		(*bs_rr_1)(void *, bus_space_handle_t,
			    bus_size_t, uint8_t *, bus_size_t);
	void		(*bs_rr_2)(void *, bus_space_handle_t,
			    bus_size_t, uint16_t *, bus_size_t);
	void		(*bs_rr_4)(void *, bus_space_handle_t,
			    bus_size_t, uint32_t *, bus_size_t);
	void		(*bs_rr_8)(void *, bus_space_handle_t,
			    bus_size_t, uint64_t *, bus_size_t);

	void		(*bs_rrr_2)(void *, bus_space_handle_t,
			    bus_size_t, uint8_t *, bus_size_t);
	void		(*bs_rrr_4)(void *, bus_space_handle_t,
			    bus_size_t, uint8_t *, bus_size_t);
	void		(*bs_rrr_8)(void *, bus_space_handle_t,
			    bus_size_t, uint8_t *, bus_size_t);

	/* write (single) */
	void		(*bs_w_1)(void *, bus_space_handle_t,
			    bus_size_t, uint8_t);
	void		(*bs_w_2)(void *, bus_space_handle_t,
			    bus_size_t, uint16_t);
	void		(*bs_w_4)(void *, bus_space_handle_t,
			    bus_size_t, uint32_t);
	void		(*bs_w_8)(void *, bus_space_handle_t,
			    bus_size_t, uint64_t);

	/* write multiple */
	void		(*bs_wm_1)(void *, bus_space_handle_t,
			    bus_size_t, const uint8_t *, bus_size_t);
	void		(*bs_wm_2)(void *, bus_space_handle_t,
			    bus_size_t, const uint16_t *, bus_size_t);
	void		(*bs_wm_4)(void *, bus_space_handle_t,
			    bus_size_t, const uint32_t *, bus_size_t);
	void		(*bs_wm_8)(void *, bus_space_handle_t,
			    bus_size_t, const uint64_t *, bus_size_t);

	void		(*bs_wrm_2)(void *, bus_space_handle_t,
			    bus_size_t, const uint8_t *, bus_size_t);
	void		(*bs_wrm_4)(void *, bus_space_handle_t,
			    bus_size_t, const uint8_t *, bus_size_t);
	void		(*bs_wrm_8)(void *, bus_space_handle_t,
			    bus_size_t, const uint8_t *, bus_size_t);

	/* write region */
	void		(*bs_wr_1)(void *, bus_space_handle_t,
			    bus_size_t, const uint8_t *, bus_size_t);
	void		(*bs_wr_2)(void *, bus_space_handle_t,
			    bus_size_t, const uint16_t *, bus_size_t);
	void		(*bs_wr_4)(void *, bus_space_handle_t,
			    bus_size_t, const uint32_t *, bus_size_t);
	void		(*bs_wr_8)(void *, bus_space_handle_t,
			    bus_size_t, const uint64_t *, bus_size_t);

	void		(*bs_wrr_2)(void *, bus_space_handle_t,
			    bus_size_t, const uint8_t *, bus_size_t);
	void		(*bs_wrr_4)(void *, bus_space_handle_t,
			    bus_size_t, const uint8_t *, bus_size_t);
	void		(*bs_wrr_8)(void *, bus_space_handle_t,
			    bus_size_t, const uint8_t *, bus_size_t);

	/* set multiple */
	void		(*bs_sm_1)(void *, bus_space_handle_t,
			    bus_size_t, uint8_t, bus_size_t);
	void		(*bs_sm_2)(void *, bus_space_handle_t,
			    bus_size_t, uint16_t, bus_size_t);
	void		(*bs_sm_4)(void *, bus_space_handle_t,
			    bus_size_t, uint32_t, bus_size_t);
	void		(*bs_sm_8)(void *, bus_space_handle_t,
			    bus_size_t, uint64_t, bus_size_t);

	/* set region */
	void		(*bs_sr_1)(void *, bus_space_handle_t,
			    bus_size_t, uint8_t, bus_size_t);
	void		(*bs_sr_2)(void *, bus_space_handle_t,
			    bus_size_t, uint16_t, bus_size_t);
	void		(*bs_sr_4)(void *, bus_space_handle_t,
			    bus_size_t, uint32_t, bus_size_t);
	void		(*bs_sr_8)(void *, bus_space_handle_t,
			    bus_size_t, uint64_t, bus_size_t);

	/* copy */
	void		(*bs_c_1)(void *, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);
	void		(*bs_c_2)(void *, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);
	void		(*bs_c_4)(void *, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);
	void		(*bs_c_8)(void *, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);
};

#ifdef _KERNEL
/*
 * Utility macros; INTERNAL USE ONLY.
 */
#define	__bs_c(a,b)		__CONCAT(a,b)
#define	__bs_opname(op,size)	__bs_c(__bs_c(__bs_c(bs_,op),_),size)

#define	__bs_rs(sz, tn, t, h, o)					\
	(*(t)->__bs_opname(r,sz))((t)->bs_cookie, h, o)

#define	__bs_ws(sz, tn, t, h, o, v)					\
	(*(t)->__bs_opname(w,sz))((t)->bs_cookie, h, o, v)

#define	__bs_nonsingle(type, sz, tn, t, h, o, a, c)			\
	(*(t)->__bs_opname(type,sz))((t)->bs_cookie, h, o, a, c)

#define	__bs_set(type, sz, tn, t, h, o, v, c)				\
	(*(t)->__bs_opname(type,sz))((t)->bs_cookie, h, o, v, c)

#define	__bs_copy(sz, tn, t, h1, o1, h2, o2, cnt)			\
	(*(t)->__bs_opname(c,sz))((t)->bs_cookie, h1, o1, h2, o2, cnt)


/*
 * Mapping and unmapping operations.
 */
#define	bus_space_map(t, a, s, f, hp)					\
	(*(t)->bs_map)((t)->bs_cookie, (a), (s), (f), (hp))
#define	bus_space_unmap(t, h, s)					\
	(*(t)->bs_unmap)((t)->bs_cookie, (h), (s))
#define	bus_space_subregion(t, h, o, s, hp)				\
	(*(t)->bs_subregion)((t)->bs_cookie, (h), (o), (s), (hp))

#endif /* _KERNEL */

#define	BUS_SPACE_MAP_CACHEABLE		0x01
#define	BUS_SPACE_MAP_LINEAR		0x02
#define	BUS_SPACE_MAP_PREFETCHABLE     	0x04

#ifdef _KERNEL
/*
 * Allocation and deallocation operations.
 */
#define	bus_space_alloc(t, rs, re, s, a, b, f, ap, hp)			\
	(*(t)->bs_alloc)((t)->bs_cookie, (rs), (re), (s), (a), (b),	\
	    (f), (ap), (hp))
#define	bus_space_free(t, h, s)						\
	(*(t)->bs_free)((t)->bs_cookie, (h), (s))

/*
 * Get kernel virtual address for ranges mapped BUS_SPACE_MAP_LINEAR.
 */
#define bus_space_vaddr(t, h) \
	(*(t)->bs_vaddr)((t)->bs_cookie, (h))

/*
 * Bus barrier operations.  The SH3 does not currently require
 * barriers, but we must provide the flags to MI code.
 */
#define	bus_space_barrier(t, h, o, l, f)				\
	((void)((void)(t), (void)(h), (void)(o), (void)(l), (void)(f)))

#define	BUS_SPACE_BARRIER_READ	0x01
#define	BUS_SPACE_BARRIER_WRITE	0x02


/*
 * Bus read (single) operations.
 */
#define	bus_space_read_1(t, h, o)	__bs_rs(1,uint8_t,(t),(h),(o))
#define	bus_space_read_2(t, h, o)	__bs_rs(2,uint16_t,(t),(h),(o))
#define	bus_space_read_4(t, h, o)	__bs_rs(4,uint32_t,(t),(h),(o))
#define	bus_space_read_8(t, h, o)	__bs_rs(8,uint64_t,(t),(h),(o))


/*
 * Bus read multiple operations.
 */
#define	bus_space_read_multi_1(t, h, o, a, c)				\
	__bs_nonsingle(rm,1,uint8_t,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_2(t, h, o, a, c)				\
	__bs_nonsingle(rm,2,uint16_t,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_4(t, h, o, a, c)				\
	__bs_nonsingle(rm,4,uint32_t,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_8(t, h, o, a, c)				\
	__bs_nonsingle(rm,8,uint64_t,(t),(h),(o),(a),(c))

#define	bus_space_read_raw_multi_2(t, h, o, a, c)			\
	__bs_nonsingle(rrm,2,uint16_t,(t),(h),(o),(a),(c))
#define	bus_space_read_raw_multi_4(t, h, o, a, c)			\
	__bs_nonsingle(rrm,4,uint32_t,(t),(h),(o),(a),(c))
#define	bus_space_read_raw_multi_8(t, h, o, a, c)			\
	__bs_nonsingle(rrm,8,uint64_t,(t),(h),(o),(a),(c))


/*
 * Bus read region operations.
 */
#define	bus_space_read_region_1(t, h, o, a, c)				\
	__bs_nonsingle(rr,1,uint8_t,(t),(h),(o),(a),(c))
#define	bus_space_read_region_2(t, h, o, a, c)				\
	__bs_nonsingle(rr,2,uint16_t,(t),(h),(o),(a),(c))
#define	bus_space_read_region_4(t, h, o, a, c)				\
	__bs_nonsingle(rr,4,uint32_t,(t),(h),(o),(a),(c))
#define	bus_space_read_region_8(t, h, o, a, c)				\
	__bs_nonsingle(rr,8,uint64_t,(t),(h),(o),(a),(c))

#define	bus_space_read_raw_region_2(t, h, o, a, c)			\
	__bs_nonsingle(rrr,2,uint16_t,(t),(h),(o),(a),(c))
#define	bus_space_read_raw_region_4(t, h, o, a, c)			\
	__bs_nonsingle(rrr,4,uint32_t,(t),(h),(o),(a),(c))
#define	bus_space_read_raw_region_8(t, h, o, a, c)			\
	__bs_nonsingle(rrr,8,uint64_t,(t),(h),(o),(a),(c))


/*
 * Bus write (single) operations.
 */
#define	bus_space_write_1(t, h, o, v)	__bs_ws(1,uint8_t,(t),(h),(o),(v))
#define	bus_space_write_2(t, h, o, v)	__bs_ws(2,uint16_t,(t),(h),(o),(v))
#define	bus_space_write_4(t, h, o, v)	__bs_ws(4,uint32_t,(t),(h),(o),(v))
#define	bus_space_write_8(t, h, o, v)	__bs_ws(8,uint64_t,(t),(h),(o),(v))


/*
 * Bus write multiple operations.
 */
#define	bus_space_write_multi_1(t, h, o, a, c)				\
	__bs_nonsingle(wm,1,uint8_t,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_2(t, h, o, a, c)				\
	__bs_nonsingle(wm,2,uint16_t,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_4(t, h, o, a, c)				\
	__bs_nonsingle(wm,4,uint32_t,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_8(t, h, o, a, c)				\
	__bs_nonsingle(wm,8,uint64_t,(t),(h),(o),(a),(c))

#define	bus_space_write_raw_multi_2(t, h, o, a, c)			\
	__bs_nonsingle(wrm,2,uint16_t,(t),(h),(o),(a),(c))
#define	bus_space_write_raw_multi_4(t, h, o, a, c)			\
	__bs_nonsingle(wrm,4,uint32_t,(t),(h),(o),(a),(c))
#define	bus_space_write_raw_multi_8(t, h, o, a, c)			\
	__bs_nonsingle(wrm,8,uint64_t,(t),(h),(o),(a),(c))


/*
 * Bus write region operations.
 */
#define	bus_space_write_region_1(t, h, o, a, c)				\
	__bs_nonsingle(wr,1,uint8_t,(t),(h),(o),(a),(c))
#define	bus_space_write_region_2(t, h, o, a, c)				\
	__bs_nonsingle(wr,2,uint16_t,(t),(h),(o),(a),(c))
#define	bus_space_write_region_4(t, h, o, a, c)				\
	__bs_nonsingle(wr,4,uint32_t,(t),(h),(o),(a),(c))
#define	bus_space_write_region_8(t, h, o, a, c)				\
	__bs_nonsingle(wr,8,uint64_t,(t),(h),(o),(a),(c))

#define	bus_space_write_raw_region_2(t, h, o, a, c)			\
	__bs_nonsingle(wrr,2,uint16_t,(t),(h),(o),(a),(c))
#define	bus_space_write_raw_region_4(t, h, o, a, c)			\
	__bs_nonsingle(wrr,4,uint32_t,(t),(h),(o),(a),(c))
#define	bus_space_write_raw_region_8(t, h, o, a, c)			\
	__bs_nonsingle(wrr,8,uint64_t,(t),(h),(o),(a),(c))


/*
 * Set multiple operations.
 */
#define	bus_space_set_multi_1(t, h, o, v, c)				\
	__bs_set(sm,1,uint8_t,(t),(h),(o),(v),(c))
#define	bus_space_set_multi_2(t, h, o, v, c)				\
	__bs_set(sm,2,uint16_t,(t),(h),(o),(v),(c))
#define	bus_space_set_multi_4(t, h, o, v, c)				\
	__bs_set(sm,4,uint32_t,(t),(h),(o),(v),(c))
#define	bus_space_set_multi_8(t, h, o, v, c)				\
	__bs_set(sm,8,uint64_t,(t),(h),(o),(v),(c))


/*
 * Set region operations.
 */
#define	bus_space_set_region_1(t, h, o, v, c)				\
	__bs_set(sr,1,uint8_t,(t),(h),(o),(v),(c))
#define	bus_space_set_region_2(t, h, o, v, c)				\
	__bs_set(sr,2,uint16_t,(t),(h),(o),(v),(c))
#define	bus_space_set_region_4(t, h, o, v, c)				\
	__bs_set(sr,4,uint32_t,(t),(h),(o),(v),(c))
#define	bus_space_set_region_8(t, h, o, v, c)				\
	__bs_set(sr,8,uint64_t,(t),(h),(o),(v),(c))


/*
 * Copy region operations.
 */
#define	bus_space_copy_1(t, h1, o1, h2, o2, c)				\
	__bs_copy(1, uint8_t, (t), (h1), (o1), (h2), (o2), (c))
#define	bus_space_copy_2(t, h1, o1, h2, o2, c)				\
	__bs_copy(2, uint16_t, (t), (h1), (o1), (h2), (o2), (c))
#define	bus_space_copy_4(t, h1, o1, h2, o2, c)				\
	__bs_copy(4, uint32_t, (t), (h1), (o1), (h2), (o2), (c))
#define	bus_space_copy_8(t, h1, o1, h2, o2, c)				\
	__bs_copy(8, uint64_t, (t), (h1), (o1), (h2), (o2), (c))

#endif /* _KERNEL */

/*
 * Flags used in various bus DMA methods.
 */
#define	BUS_DMA_WAITOK		0x0000	/* safe to sleep (pseudo-flag) */
#define	BUS_DMA_NOWAIT		0x0001	/* not safe to sleep */
#define	BUS_DMA_ALLOCNOW	0x0002	/* perform resource allocation now */
#define	BUS_DMA_COHERENT	0x0004	/* map memory to not require sync */
#define	BUS_DMA_STREAMING	0x0008	/* hint: sequential, unidirectional */
#define	BUS_DMA_BUS1		0x0010	/* placeholders for bus functions... */
#define	BUS_DMA_BUS2		0x0020
#define	BUS_DMA_BUS3		0x0040
#define	BUS_DMA_BUS4		0x0080
#define	BUS_DMA_READ		0x0100	/* mapping is device -> memory only */
#define	BUS_DMA_WRITE		0x0200	/* mapping is memory -> device only */
#define	BUS_DMA_NOCACHE		0x0400	/* hint: map non-cached memory */
#define	BUS_DMA_ZERO		0x0800	/* zero memory in dmamem_alloc */
#define	BUS_DMA_64BIT		0x1000	/* device handles 64bit dva */

/* Forwards needed by prototypes below. */
struct mbuf;
struct uio;

/*
 *	Operations performed by bus_dmamap_sync().
 */
#define	BUS_DMASYNC_PREREAD	0x01
#define	BUS_DMASYNC_POSTREAD	0x02
#define	BUS_DMASYNC_PREWRITE	0x04
#define	BUS_DMASYNC_POSTWRITE	0x08

typedef struct _bus_dma_tag *bus_dma_tag_t;
typedef struct _bus_dmamap *bus_dmamap_t;

#define BUS_DMA_TAG_VALID(t)    ((t) != (bus_dma_tag_t)0)

/*
 *	bus_dma_segment_t
 *
 *	Describes a single contiguous DMA transaction.  Values
 *	are suitable for programming into DMA registers.
 */
struct _bus_dma_segment {
	bus_addr_t	ds_addr;	/* DMA address */
	bus_size_t	ds_len;		/* length of transfer */

	/* private section */
	bus_addr_t	_ds_vaddr;	/* virtual address */
};
typedef struct _bus_dma_segment	bus_dma_segment_t;

/*
 *	bus_dma_tag_t
 *
 *	A machine-dependent opaque type describing the implementation of
 *	DMA for a given bus.
 */

struct _bus_dma_tag {
	void	*_cookie;		/* cookie used in the guts */

	/*
	 * DMA mapping methods.
	 */
	int	(*_dmamap_create)(bus_dma_tag_t, bus_size_t, int,
		    bus_size_t, bus_size_t, int, bus_dmamap_t *);
	void	(*_dmamap_destroy)(bus_dma_tag_t, bus_dmamap_t);
	int	(*_dmamap_load)(bus_dma_tag_t, bus_dmamap_t, void *,
		    bus_size_t, struct proc *, int);
	int	(*_dmamap_load_mbuf)(bus_dma_tag_t, bus_dmamap_t,
		    struct mbuf *, int);
	int	(*_dmamap_load_uio)(bus_dma_tag_t, bus_dmamap_t,
		    struct uio *, int);
	int	(*_dmamap_load_raw)(bus_dma_tag_t, bus_dmamap_t,
		    bus_dma_segment_t *, int, bus_size_t, int);
	void	(*_dmamap_unload)(bus_dma_tag_t, bus_dmamap_t);
	void	(*_dmamap_sync)(bus_dma_tag_t, bus_dmamap_t,
		    bus_addr_t, bus_size_t, int);

	/*
	 * DMA memory utility functions.
	 */
	int	(*_dmamem_alloc)(bus_dma_tag_t, bus_size_t, bus_size_t,
		    bus_size_t, bus_dma_segment_t *, int, int *, int);
	void	(*_dmamem_free)(bus_dma_tag_t,
		    bus_dma_segment_t *, int);
	int	(*_dmamem_map)(bus_dma_tag_t, bus_dma_segment_t *,
		    int, size_t, caddr_t *, int);
	void	(*_dmamem_unmap)(bus_dma_tag_t, caddr_t, size_t);
	paddr_t	(*_dmamem_mmap)(bus_dma_tag_t, bus_dma_segment_t *,
		    int, off_t, int, int);
};

#define	bus_dmamap_create(t, s, n, m, b, f, p)			\
	(*(t)->_dmamap_create)((t), (s), (n), (m), (b), (f), (p))
#define	bus_dmamap_destroy(t, p)				\
	(*(t)->_dmamap_destroy)((t), (p))
#define	bus_dmamap_load(t, m, b, s, p, f)			\
	(*(t)->_dmamap_load)((t), (m), (b), (s), (p), (f))
#define	bus_dmamap_load_mbuf(t, m, b, f)			\
	(*(t)->_dmamap_load_mbuf)((t), (m), (b), (f))
#define	bus_dmamap_load_uio(t, m, u, f)				\
	(*(t)->_dmamap_load_uio)((t), (m), (u), (f))
#define	bus_dmamap_load_raw(t, m, sg, n, s, f)			\
	(*(t)->_dmamap_load_raw)((t), (m), (sg), (n), (s), (f))
#define	bus_dmamap_unload(t, p)					\
	(*(t)->_dmamap_unload)((t), (p))
#define	bus_dmamap_sync(t, m, o, l, op)				\
	(void)((t)->_dmamap_sync ?				\
	    (*(t)->_dmamap_sync)((t), (m), (o), (l), (op)) : (void)0)

#define	bus_dmamem_alloc(t, s, a, b, sg, n, r, f)		\
	(*(t)->_dmamem_alloc)((t), (s), (a), (b), (sg), (n), (r), (f))
#define	bus_dmamem_free(t, sg, n)				\
	(*(t)->_dmamem_free)((t), (sg), (n))
#define	bus_dmamem_map(t, sg, n, s, k, f)			\
	(*(t)->_dmamem_map)((t), (sg), (n), (s), (k), (f))
#define	bus_dmamem_unmap(t, k, s)				\
	(*(t)->_dmamem_unmap)((t), (k), (s))
#define	bus_dmamem_mmap(t, sg, n, o, p, f)			\
	(*(t)->_dmamem_mmap)((t), (sg), (n), (o), (p), (f))

/*
 *	bus_dmamap_t
 *
 *	Describes a DMA mapping.
 */
struct _bus_dmamap {
	/*
	 * PRIVATE MEMBERS: not for use my machine-independent code.
	 */
	bus_size_t	_dm_size;	/* largest DMA transfer mappable */
	int		_dm_segcnt;	/* number of segs this map can map */
	bus_size_t	_dm_maxsegsz;	/* largest possible segment */
	bus_size_t	_dm_boundary;	/* don't cross this */
	int		_dm_flags;	/* misc. flags */

	void		*_dm_cookie;	/* cookie for bus-specific functions */

	/*
	 * PUBLIC MEMBERS: these are used by machine-independent code.
	 */
	bus_size_t	dm_mapsize;	/* size of the mapping */
	int		dm_nsegs;	/* # valid segments in mapping */
	bus_dma_segment_t dm_segs[1];	/* segments; variable length */
};

#if defined(_LANDISK_BUS_DMA_PRIVATE)
int	_bus_dmamap_create(bus_dma_tag_t, bus_size_t, int, bus_size_t,
	    bus_size_t, int, bus_dmamap_t *);
void	_bus_dmamap_destroy(bus_dma_tag_t, bus_dmamap_t);
int	_bus_dmamap_load(bus_dma_tag_t, bus_dmamap_t, void *, bus_size_t,
	    struct proc *, int);
int	_bus_dmamap_load_mbuf(bus_dma_tag_t, bus_dmamap_t, struct mbuf *,int);
int	_bus_dmamap_load_uio(bus_dma_tag_t, bus_dmamap_t, struct uio *, int);
int	_bus_dmamap_load_raw(bus_dma_tag_t, bus_dmamap_t, bus_dma_segment_t *,
	    int, bus_size_t, int);
void	_bus_dmamap_unload(bus_dma_tag_t, bus_dmamap_t);
void	_bus_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, bus_addr_t,
	    bus_size_t, int);

int	_bus_dmamem_alloc(bus_dma_tag_t tag, bus_size_t size,
	    bus_size_t alignment, bus_size_t boundary, bus_dma_segment_t *segs,
	    int nsegs, int *rsegs, int flags);
void	_bus_dmamem_free(bus_dma_tag_t tag, bus_dma_segment_t *segs,
	    int nsegs);
int	_bus_dmamem_map(bus_dma_tag_t tag, bus_dma_segment_t *segs, int nsegs,
	    size_t size, caddr_t *kvap, int flags);
void	_bus_dmamem_unmap(bus_dma_tag_t tag, caddr_t kva, size_t size);
paddr_t	_bus_dmamem_mmap(bus_dma_tag_t tag, bus_dma_segment_t *segs,
	    int nsegs, off_t off, int prot, int flags);
#endif	/* _LANDISK_BUS_DMA_PRIVATE */

#endif /* _KERNEL */

#endif	/* _MACHINE_BUS_H_ */
