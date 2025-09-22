/*	$OpenBSD: bus.h,v 1.32 2017/05/08 00:27:45 dlg Exp $	*/

/*
 * Copyright (c) 1998-2004 Michael Shalayeff
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
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MACHINE_BUS_H_
#define _MACHINE_BUS_H_

#include <machine/cpufunc.h>

/* addresses in bus space */
typedef u_long bus_addr_t;
typedef u_long bus_size_t;

/* access methods for bus space */
typedef u_long bus_space_handle_t;

struct hppa_bus_space_tag {
	void *hbt_cookie;

	int  (*hbt_map)(void *v, bus_addr_t addr, bus_size_t size,
		    int flags, bus_space_handle_t *bshp);
	void (*hbt_unmap)(void *v, bus_space_handle_t bsh, bus_size_t size);
	int  (*hbt_subregion)(void *v, bus_space_handle_t bsh,
		    bus_size_t offset, bus_size_t size,
		    bus_space_handle_t *nbshp);
	int  (*hbt_alloc)(void *v, bus_addr_t rstart, bus_addr_t rend,
		    bus_size_t size, bus_size_t align, bus_size_t boundary,
		    int flags, bus_addr_t *addrp, bus_space_handle_t *bshp);
	void (*hbt_free)(void *, bus_space_handle_t, bus_size_t);
	void (*hbt_barrier)(void *v, bus_space_handle_t h,
		    bus_size_t o, bus_size_t l, int op);
	void *(*hbt_vaddr)(void *v, bus_space_handle_t h);

	u_int8_t  (*hbt_r1)(void *, bus_space_handle_t, bus_size_t);
	u_int16_t (*hbt_r2)(void *, bus_space_handle_t, bus_size_t);
	u_int32_t (*hbt_r4)(void *, bus_space_handle_t, bus_size_t);
	u_int64_t (*hbt_r8)(void *, bus_space_handle_t, bus_size_t);

	void (*hbt_w1)(void *, bus_space_handle_t, bus_size_t, u_int8_t);
	void (*hbt_w2)(void *, bus_space_handle_t, bus_size_t, u_int16_t);
	void (*hbt_w4)(void *, bus_space_handle_t, bus_size_t, u_int32_t);
	void (*hbt_w8)(void *, bus_space_handle_t, bus_size_t, u_int64_t);

	void (*hbt_rm_1)(void *v, bus_space_handle_t h,
	      bus_size_t o, u_int8_t *a, bus_size_t c);
	void (*hbt_rm_2)(void *v, bus_space_handle_t h,
	      bus_size_t o, u_int16_t *a, bus_size_t c);
	void (*hbt_rm_4)(void *v, bus_space_handle_t h,
	      bus_size_t o, u_int32_t *a, bus_size_t c);
	void (*hbt_rm_8)(void *v, bus_space_handle_t h,
	      bus_size_t o, u_int64_t *a, bus_size_t c);

	void (*hbt_wm_1)(void *v, bus_space_handle_t h, bus_size_t o,
	      const u_int8_t *a, bus_size_t c);
	void (*hbt_wm_2)(void *v, bus_space_handle_t h, bus_size_t o,
	      const u_int16_t *a, bus_size_t c);
	void (*hbt_wm_4)(void *v, bus_space_handle_t h, bus_size_t o,
	      const u_int32_t *a, bus_size_t c);
	void (*hbt_wm_8)(void *v, bus_space_handle_t h, bus_size_t o,
	      const u_int64_t *a, bus_size_t c);

	void (*hbt_sm_1)(void *v, bus_space_handle_t h, bus_size_t o,
	      u_int8_t  vv, bus_size_t c);
	void (*hbt_sm_2)(void *v, bus_space_handle_t h, bus_size_t o,
	      u_int16_t vv, bus_size_t c);
	void (*hbt_sm_4)(void *v, bus_space_handle_t h, bus_size_t o,
	      u_int32_t vv, bus_size_t c);
	void (*hbt_sm_8)(void *v, bus_space_handle_t h, bus_size_t o,
	      u_int64_t vv, bus_size_t c);

	void (*hbt_rrm_2)(void *v, bus_space_handle_t h,
	       bus_size_t o, u_int8_t *a, bus_size_t c);
	void (*hbt_rrm_4)(void *v, bus_space_handle_t h,
	       bus_size_t o, u_int8_t *a, bus_size_t c);
	void (*hbt_rrm_8)(void *v, bus_space_handle_t h,
	       bus_size_t o, u_int8_t *a, bus_size_t c);

	void (*hbt_wrm_2)(void *v, bus_space_handle_t h,
	       bus_size_t o, const u_int8_t *a, bus_size_t c);
	void (*hbt_wrm_4)(void *v, bus_space_handle_t h,
	       bus_size_t o, const u_int8_t *a, bus_size_t c);
	void (*hbt_wrm_8)(void *v, bus_space_handle_t h,
	       bus_size_t o, const u_int8_t *a, bus_size_t c);

	void (*hbt_rr_1)(void *v, bus_space_handle_t h,
	      bus_size_t o, u_int8_t *a, bus_size_t c);
	void (*hbt_rr_2)(void *v, bus_space_handle_t h,
	      bus_size_t o, u_int16_t *a, bus_size_t c);
	void (*hbt_rr_4)(void *v, bus_space_handle_t h,
	      bus_size_t o, u_int32_t *a, bus_size_t c);
	void (*hbt_rr_8)(void *v, bus_space_handle_t h,
	      bus_size_t o, u_int64_t *a, bus_size_t c);

	void (*hbt_wr_1)(void *v, bus_space_handle_t h,
	      bus_size_t o, const u_int8_t *a, bus_size_t c);
	void (*hbt_wr_2)(void *v, bus_space_handle_t h,
	      bus_size_t o, const u_int16_t *a, bus_size_t c);
	void (*hbt_wr_4)(void *v, bus_space_handle_t h,
	      bus_size_t o, const u_int32_t *a, bus_size_t c);
	void (*hbt_wr_8)(void *v, bus_space_handle_t h,
	      bus_size_t o, const u_int64_t *a, bus_size_t c);

	void (*hbt_rrr_2)(void *v, bus_space_handle_t h,
	       bus_size_t o, u_int8_t *a, bus_size_t c);
	void (*hbt_rrr_4)(void *v, bus_space_handle_t h,
	       bus_size_t o, u_int8_t *a, bus_size_t c);
	void (*hbt_rrr_8)(void *v, bus_space_handle_t h,
	       bus_size_t o, u_int8_t *a, bus_size_t c);

	void (*hbt_wrr_2)(void *v, bus_space_handle_t h,
	       bus_size_t o, const u_int8_t *a, bus_size_t c);
	void (*hbt_wrr_4)(void *v, bus_space_handle_t h,
	       bus_size_t o, const u_int8_t *a, bus_size_t c);
	void (*hbt_wrr_8)(void *v, bus_space_handle_t h,
	       bus_size_t o, const u_int8_t *a, bus_size_t c);

	void (*hbt_sr_1)(void *v, bus_space_handle_t h,
	       bus_size_t o, u_int8_t vv, bus_size_t c);
	void (*hbt_sr_2)(void *v, bus_space_handle_t h,
	       bus_size_t o, u_int16_t vv, bus_size_t c);
	void (*hbt_sr_4)(void *v, bus_space_handle_t h,
	       bus_size_t o, u_int32_t vv, bus_size_t c);
	void (*hbt_sr_8)(void *v, bus_space_handle_t h,
	       bus_size_t o, u_int64_t vv, bus_size_t c);

	void (*hbt_cp_1)(void *v, bus_space_handle_t h1, bus_size_t o1,
	      bus_space_handle_t h2, bus_size_t o2, bus_size_t c);
	void (*hbt_cp_2)(void *v, bus_space_handle_t h1, bus_size_t o1,
	      bus_space_handle_t h2, bus_size_t o2, bus_size_t c);
	void (*hbt_cp_4)(void *v, bus_space_handle_t h1, bus_size_t o1,
	      bus_space_handle_t h2, bus_size_t o2, bus_size_t c);
	void (*hbt_cp_8)(void *v, bus_space_handle_t h1, bus_size_t o1,
	      bus_space_handle_t h2, bus_size_t o2, bus_size_t c);
};
typedef const struct hppa_bus_space_tag *bus_space_tag_t;
extern const struct hppa_bus_space_tag hppa_bustag;

/* flags for bus space map functions */
#define	BUS_SPACE_MAP_CACHEABLE		0x0001
#define	BUS_SPACE_MAP_LINEAR		0x0002
#define	BUS_SPACE_MAP_READONLY		0x0004
#define	BUS_SPACE_MAP_PREFETCHABLE	0x0008

/* bus access routines */

#define	bus_space_map(t,a,c,ca,hp) \
	(((t)->hbt_map)((t)->hbt_cookie,(a),(c),(ca),(hp)))
#define	bus_space_unmap(t,h,c) \
	(((t)->hbt_unmap)((t)->hbt_cookie,(h),(c)))
#define	bus_space_subregion(t,h,o,c,hp) \
	(((t)->hbt_subregion)((t)->hbt_cookie,(h),(o),(c),(hp)))
#define	bus_space_alloc(t,b,e,c,al,bn,ca,ap,hp) \
	(((t)->hbt_alloc)((t)->hbt_cookie,(b),(e),(c),(al),(bn),(ca),(ap),(hp)))
#define	bus_space_free(t,h,c) \
	(((t)->hbt_free)((t)->hbt_cookie,(h),(c)))

#define	bus_space_read_1(t,h,o) (((t)->hbt_r1)((t)->hbt_cookie,(h),(o)))
#define	bus_space_read_2(t,h,o) (((t)->hbt_r2)((t)->hbt_cookie,(h),(o)))
#define	bus_space_read_4(t,h,o) (((t)->hbt_r4)((t)->hbt_cookie,(h),(o)))
#define	bus_space_read_8(t,h,o) (((t)->hbt_r8)((t)->hbt_cookie,(h),(o)))

#define	bus_space_write_1(t,h,o,v) (((t)->hbt_w1)((t)->hbt_cookie,(h),(o),(v)))
#define	bus_space_write_2(t,h,o,v) (((t)->hbt_w2)((t)->hbt_cookie,(h),(o),(v)))
#define	bus_space_write_4(t,h,o,v) (((t)->hbt_w4)((t)->hbt_cookie,(h),(o),(v)))
#define	bus_space_write_8(t,h,o,v) (((t)->hbt_w8)((t)->hbt_cookie,(h),(o),(v)))

#define	bus_space_read_multi_1(t,h,o,a,c) \
	(((t)->hbt_rm_1)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_read_multi_2(t,h,o,a,c) \
	(((t)->hbt_rm_2)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_read_multi_4(t,h,o,a,c) \
	(((t)->hbt_rm_4)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_read_multi_8(t,h,o,a,c) \
	(((t)->hbt_rm_8)((t)->hbt_cookie, (h), (o), (a), (c)))

#define	bus_space_write_multi_1(t,h,o,a,c) \
	(((t)->hbt_wm_1)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_write_multi_2(t,h,o,a,c) \
	(((t)->hbt_wm_2)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_write_multi_4(t,h,o,a,c) \
	(((t)->hbt_wm_4)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_write_multi_8(t,h,o,a,c) \
	(((t)->hbt_wm_8)((t)->hbt_cookie, (h), (o), (a), (c)))

#define	bus_space_set_multi_1(t,h,o,v,c) \
	(((t)->hbt_sm_1)((t)->hbt_cookie, (h), (o), (v), (c)))
#define	bus_space_set_multi_2(t,h,o,v,c) \
	(((t)->hbt_sm_2)((t)->hbt_cookie, (h), (o), (v), (c)))
#define	bus_space_set_multi_4(t,h,o,v,c) \
	(((t)->hbt_sm_4)((t)->hbt_cookie, (h), (o), (v), (c)))
#define	bus_space_set_multi_8(t,h,o,v,c) \
	(((t)->hbt_sm_8)((t)->hbt_cookie, (h), (o), (v), (c)))

#define	bus_space_read_raw_multi_2(t, h, o, a, c) \
	(((t)->hbt_rrm_2)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_read_raw_multi_4(t, h, o, a, c) \
	(((t)->hbt_rrm_4)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_read_raw_multi_8(t, h, o, a, c) \
	(((t)->hbt_rrm_8)((t)->hbt_cookie, (h), (o), (a), (c)))

#define	bus_space_write_raw_multi_2(t, h, o, a, c) \
	(((t)->hbt_wrm_2)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_write_raw_multi_4(t, h, o, a, c) \
	(((t)->hbt_wrm_4)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_write_raw_multi_8(t, h, o, a, c) \
	(((t)->hbt_wrm_8)((t)->hbt_cookie, (h), (o), (a), (c)))

#define	bus_space_read_region_1(t, h, o, a, c) \
	(((t)->hbt_rr_1)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_read_region_2(t, h, o, a, c) \
	(((t)->hbt_rr_2)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_read_region_4(t, h, o, a, c) \
	(((t)->hbt_rr_4)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_read_region_8(t, h, o, a, c) \
	(((t)->hbt_rr_8)((t)->hbt_cookie, (h), (o), (a), (c)))

#define	bus_space_write_region_1(t, h, o, a, c) \
	(((t)->hbt_wr_1)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_write_region_2(t, h, o, a, c) \
	(((t)->hbt_wr_2)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_write_region_4(t, h, o, a, c) \
	(((t)->hbt_wr_4)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_write_region_8(t, h, o, a, c) \
	(((t)->hbt_wr_8)((t)->hbt_cookie, (h), (o), (a), (c)))

#define	bus_space_read_raw_region_2(t, h, o, a, c) \
	(((t)->hbt_rrr_2)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_read_raw_region_4(t, h, o, a, c) \
	(((t)->hbt_rrr_4)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_read_raw_region_8(t, h, o, a, c) \
	(((t)->hbt_rrr_8)((t)->hbt_cookie, (h), (o), (a), (c)))

#define	bus_space_write_raw_region_2(t, h, o, a, c) \
	(((t)->hbt_wrr_2)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_write_raw_region_4(t, h, o, a, c) \
	(((t)->hbt_wrr_4)((t)->hbt_cookie, (h), (o), (a), (c)))
#define	bus_space_write_raw_region_8(t, h, o, a, c) \
	(((t)->hbt_wrr_8)((t)->hbt_cookie, (h), (o), (a), (c)))

#define	bus_space_set_region_1(t, h, o, v, c) \
	(((t)->hbt_sr_1)((t)->hbt_cookie, (h), (o), (v), (c)))
#define	bus_space_set_region_2(t, h, o, v, c) \
	(((t)->hbt_sr_2)((t)->hbt_cookie, (h), (o), (v), (c)))
#define	bus_space_set_region_4(t, h, o, v, c) \
	(((t)->hbt_sr_4)((t)->hbt_cookie, (h), (o), (v), (c)))
#define	bus_space_set_region_8(t, h, o, v, c) \
	(((t)->hbt_sr_8)((t)->hbt_cookie, (h), (o), (v), (c)))

#define	bus_space_copy_1(t, h1, o1, h2, o2, c) \
	(((t)->hbt_cp_1)((t)->hbt_cookie, (h1), (o1), (h2), (o2), (c)))
#define	bus_space_copy_2(t, h1, o1, h2, o2, c) \
	(((t)->hbt_cp_2)((t)->hbt_cookie, (h1), (o1), (h2), (o2), (c)))
#define	bus_space_copy_4(t, h1, o1, h2, o2, c) \
	(((t)->hbt_cp_4)((t)->hbt_cookie, (h1), (o1), (h2), (o2), (c)))
#define	bus_space_copy_8(t, h1, o1, h2, o2, c) \
	(((t)->hbt_cp_8)((t)->hbt_cookie, (h1), (o1), (h2), (o2), (c)))

#define	BUS_SPACE_BARRIER_READ	0x01
#define	BUS_SPACE_BARRIER_WRITE	0x02

#define	bus_space_barrier(t,h,o,l,op) \
	((t)->hbt_barrier((t)->hbt_cookie, (h), (o), (l), (op)))
#define	bus_space_vaddr(t,h) \
	((t)->hbt_vaddr((t)->hbt_cookie, (h)))

#define	BUS_DMA_WAITOK		0x0000	/* safe to sleep (pseudo-flag) */
#define	BUS_DMA_NOWAIT		0x0001	/* not safe to sleep */
#define	BUS_DMA_ALLOCNOW	0x0002	/* perform resource allocation now */
#define	BUS_DMA_COHERENT	0x0004	/* hint: map memory DMA coherent */
#define	BUS_DMA_BUS1		0x0020	/* placeholders for bus functions... */
#define	BUS_DMA_BUS2		0x0040
#define	BUS_DMA_BUS3		0x0080
#define	BUS_DMA_BUS4		0x0100
#define	BUS_DMA_STREAMING	0x0200	/* hint: sequential, unidirectional */
#define	BUS_DMA_READ		0x0400	/* mapping is device -> memory only */
#define	BUS_DMA_WRITE		0x0800	/* mapping is memory -> device only */
#define	BUS_DMA_ZERO		0x1000	/* zero memory in dmamem_alloc */
#define	BUS_DMA_64BIT		0x2000	/* device handles 64bit dva */

/* Forwards needed by prototypes below. */
struct mbuf;
struct proc;
struct uio;

/* Operations performed by bus_dmamap_sync().  */
#define BUS_DMASYNC_POSTREAD	0x01
#define BUS_DMASYNC_POSTWRITE	0x02
#define BUS_DMASYNC_PREREAD	0x04
#define BUS_DMASYNC_PREWRITE	0x08

typedef const struct hppa_bus_dma_tag	*bus_dma_tag_t;
typedef struct hppa_bus_dmamap	*bus_dmamap_t;

/*
 *	bus_dma_segment_t
 *
 *	Describes a single contiguous DMA transaction.  Values
 *	are suitable for programming into DMA registers.
 */
struct hppa_bus_dma_segment {
	vaddr_t		_ds_va;		/* needed for syncing */

	bus_addr_t	ds_addr;	/* DMA address */
	bus_size_t	ds_len;		/* length of transfer */
};
typedef struct hppa_bus_dma_segment	bus_dma_segment_t;

/*
 *	bus_dma_tag_t
 *
 *	A machine-dependent opaque type describing the implementation of
 *	DMA for a given bus.
 */

struct hppa_bus_dma_tag {
	void	*_cookie;		/* cookie used in the guts */

	/*
	 * DMA mapping methods.
	 */
	int	(*_dmamap_create)(void *, bus_size_t, int,
		    bus_size_t, bus_size_t, int, bus_dmamap_t *);
	void	(*_dmamap_destroy)(void *, bus_dmamap_t);
	int	(*_dmamap_load)(void *, bus_dmamap_t, void *,
		    bus_size_t, struct proc *, int);
	int	(*_dmamap_load_mbuf)(void *, bus_dmamap_t,
		    struct mbuf *, int);
	int	(*_dmamap_load_uio)(void *, bus_dmamap_t,
		    struct uio *, int);
	int	(*_dmamap_load_raw)(void *, bus_dmamap_t,
		    bus_dma_segment_t *, int, bus_size_t, int);
	void	(*_dmamap_unload)(void *, bus_dmamap_t);
	void	(*_dmamap_sync)(void *, bus_dmamap_t, bus_addr_t,
		    bus_size_t, int);

	/*
	 * DMA memory utility functions.
	 */
	int	(*_dmamem_alloc)(void *, bus_size_t, bus_size_t,
		    bus_size_t, bus_dma_segment_t *, int, int *, int);
	void	(*_dmamem_free)(void *, bus_dma_segment_t *, int);
	int	(*_dmamem_map)(void *, bus_dma_segment_t *,
		    int, size_t, caddr_t *, int);
	void	(*_dmamem_unmap)(void *, caddr_t, size_t);
	paddr_t	(*_dmamem_mmap)(void *, bus_dma_segment_t *,
		    int, off_t, int, int);
};

#define	bus_dmamap_create(t, s, n, m, b, f, p)			\
	(*(t)->_dmamap_create)((t)->_cookie, (s), (n), (m), (b), (f), (p))
#define	bus_dmamap_destroy(t, p)				\
	(*(t)->_dmamap_destroy)((t)->_cookie, (p))
#define	bus_dmamap_load(t, m, b, s, p, f)			\
	(*(t)->_dmamap_load)((t)->_cookie, (m), (b), (s), (p), (f))
#define	bus_dmamap_load_mbuf(t, m, b, f)			\
	(*(t)->_dmamap_load_mbuf)((t)->_cookie, (m), (b), (f))
#define	bus_dmamap_load_uio(t, m, u, f)				\
	(*(t)->_dmamap_load_uio)((t)->_cookie, (m), (u), (f))
#define	bus_dmamap_load_raw(t, m, sg, n, s, f)			\
	(*(t)->_dmamap_load_raw)((t)->_cookie, (m), (sg), (n), (s), (f))
#define	bus_dmamap_unload(t, p)					\
	(*(t)->_dmamap_unload)((t)->_cookie, (p))
#define	bus_dmamap_sync(t, p, a, l, o)				\
	(void)((t)->_dmamap_sync ?				\
	    (*(t)->_dmamap_sync)((t)->_cookie, (p), (a), (l), (o)) : (void)0)

#define	bus_dmamem_alloc(t, s, a, b, sg, n, r, f)		\
	(*(t)->_dmamem_alloc)((t)->_cookie, (s), (a), (b), (sg), (n), (r), (f))
#define	bus_dmamem_free(t, sg, n)				\
	(*(t)->_dmamem_free)((t)->_cookie, (sg), (n))
#define	bus_dmamem_map(t, sg, n, s, k, f)			\
	(*(t)->_dmamem_map)((t)->_cookie, (sg), (n), (s), (k), (f))
#define	bus_dmamem_unmap(t, k, s)				\
	(*(t)->_dmamem_unmap)((t)->_cookie, (k), (s))
#define	bus_dmamem_mmap(t, sg, n, o, p, f)			\
	(*(t)->_dmamem_mmap)((t)->_cookie, (sg), (n), (o), (p), (f))

/*
 *	bus_dmamap_t
 *
 *	Describes a DMA mapping.
 */
struct hppa_bus_dmamap {
	/*
	 * PRIVATE MEMBERS: not for use by machine-independent code.
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

#endif /* _MACHINE_BUS_H_ */
