/*	$OpenBSD: bus.h,v 1.33 2022/01/04 20:41:42 deraadt Exp $	*/
/*	$NetBSD: bus.h,v 1.10 1996/12/02 22:19:32 cgd Exp $	*/

/*
 * Copyright (c) 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#ifndef _MACHINE_BUS_H_
#define	_MACHINE_BUS_H_

#ifdef _KERNEL

/*
 * Addresses (in bus space).
 */
typedef u_long bus_addr_t;
typedef u_long bus_size_t;

/*
 * Access methods for bus space.
 */
typedef struct alpha_bus_space *bus_space_tag_t;
typedef u_long bus_space_handle_t;

struct alpha_bus_space {
	/* cookie */
	void		*abs_cookie;

	/* mapping/unmapping */
	int		(*abs_map)(void *, bus_addr_t, bus_size_t,
			    int, bus_space_handle_t *);
	void		(*abs_unmap)(void *, bus_space_handle_t,
			    bus_size_t);
	int		(*abs_subregion)(void *, bus_space_handle_t,
			    bus_size_t, bus_size_t, bus_space_handle_t *);

	/* allocation/deallocation */
	int		(*abs_alloc)(void *, bus_addr_t, bus_addr_t,
			    bus_size_t, bus_size_t, bus_size_t, int,
			    bus_addr_t *, bus_space_handle_t *);
	void		(*abs_free)(void *, bus_space_handle_t,
			    bus_size_t);

	/* get kernel virtual address */
	void *		(*abs_vaddr)(void *, bus_space_handle_t);

	/* barrier */
	void		(*abs_barrier)(void *, bus_space_handle_t,
			    bus_size_t, bus_size_t, int);

	/* read (single) */
	u_int8_t	(*abs_r_1)(void *, bus_space_handle_t,
			    bus_size_t);
	u_int16_t	(*abs_r_2)(void *, bus_space_handle_t,
			    bus_size_t);
	u_int32_t	(*abs_r_4)(void *, bus_space_handle_t,
			    bus_size_t);
	u_int64_t	(*abs_r_8)(void *, bus_space_handle_t,
			    bus_size_t);

	/* read multiple */
	void		(*abs_rm_1)(void *, bus_space_handle_t,
			    bus_size_t, u_int8_t *, bus_size_t);
	void		(*abs_rm_2)(void *, bus_space_handle_t,
			    bus_size_t, u_int16_t *, bus_size_t);
	void		(*abs_rm_4)(void *, bus_space_handle_t,
			    bus_size_t, u_int32_t *, bus_size_t);
	void		(*abs_rm_8)(void *, bus_space_handle_t,
			    bus_size_t, u_int64_t *, bus_size_t);
					
	/* read region */
	void		(*abs_rr_1)(void *, bus_space_handle_t,
			    bus_size_t, u_int8_t *, bus_size_t);
	void		(*abs_rr_2)(void *, bus_space_handle_t,
			    bus_size_t, u_int16_t *, bus_size_t);
	void		(*abs_rr_4)(void *, bus_space_handle_t,
			    bus_size_t, u_int32_t *, bus_size_t);
	void		(*abs_rr_8)(void *, bus_space_handle_t,
			    bus_size_t, u_int64_t *, bus_size_t);
					
	/* write (single) */
	void		(*abs_w_1)(void *, bus_space_handle_t,
			    bus_size_t, u_int8_t);
	void		(*abs_w_2)(void *, bus_space_handle_t,
			    bus_size_t, u_int16_t);
	void		(*abs_w_4)(void *, bus_space_handle_t,
			    bus_size_t, u_int32_t);
	void		(*abs_w_8)(void *, bus_space_handle_t,
			    bus_size_t, u_int64_t);

	/* write multiple */
	void		(*abs_wm_1)(void *, bus_space_handle_t,
			    bus_size_t, const u_int8_t *, bus_size_t);
	void		(*abs_wm_2)(void *, bus_space_handle_t,
			    bus_size_t, const u_int16_t *, bus_size_t);
	void		(*abs_wm_4)(void *, bus_space_handle_t,
			    bus_size_t, const u_int32_t *, bus_size_t);
	void		(*abs_wm_8)(void *, bus_space_handle_t,
			    bus_size_t, const u_int64_t *, bus_size_t);
					
	/* write region */
	void		(*abs_wr_1)(void *, bus_space_handle_t,
			    bus_size_t, const u_int8_t *, bus_size_t);
	void		(*abs_wr_2)(void *, bus_space_handle_t,
			    bus_size_t, const u_int16_t *, bus_size_t);
	void		(*abs_wr_4)(void *, bus_space_handle_t,
			    bus_size_t, const u_int32_t *, bus_size_t);
	void		(*abs_wr_8)(void *, bus_space_handle_t,
			    bus_size_t, const u_int64_t *, bus_size_t);

	/* set multiple */
	void		(*abs_sm_1)(void *, bus_space_handle_t,
			    bus_size_t, u_int8_t, bus_size_t);
	void		(*abs_sm_2)(void *, bus_space_handle_t,
			    bus_size_t, u_int16_t, bus_size_t);
	void		(*abs_sm_4)(void *, bus_space_handle_t,
			    bus_size_t, u_int32_t, bus_size_t);
	void		(*abs_sm_8)(void *, bus_space_handle_t,
			    bus_size_t, u_int64_t, bus_size_t);

	/* set region */
	void		(*abs_sr_1)(void *, bus_space_handle_t,
			    bus_size_t, u_int8_t, bus_size_t);
	void		(*abs_sr_2)(void *, bus_space_handle_t,
			    bus_size_t, u_int16_t, bus_size_t);
	void		(*abs_sr_4)(void *, bus_space_handle_t,
			    bus_size_t, u_int32_t, bus_size_t);
	void		(*abs_sr_8)(void *, bus_space_handle_t,
			    bus_size_t, u_int64_t, bus_size_t);

	/* copy */
	void		(*abs_c_1)(void *, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);
	void		(*abs_c_2)(void *, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);
	void		(*abs_c_4)(void *, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);
	void		(*abs_c_8)(void *, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);

	/* OpenBSD extensions follows */

	/* read multiple raw */
	void		(*abs_rrm_2)(void *, bus_space_handle_t,
			    bus_size_t, u_int8_t *, bus_size_t);
	void		(*abs_rrm_4)(void *, bus_space_handle_t,
			    bus_size_t, u_int8_t *, bus_size_t);
	void		(*abs_rrm_8)(void *, bus_space_handle_t,
			    bus_size_t, u_int8_t *, bus_size_t);

	/* write multiple raw */
	void		(*abs_wrm_2)(void *, bus_space_handle_t,
			    bus_size_t, const u_int8_t *, bus_size_t);
	void		(*abs_wrm_4)(void *, bus_space_handle_t,
			    bus_size_t, const u_int8_t *, bus_size_t);
	void		(*abs_wrm_8)(void *, bus_space_handle_t,
			    bus_size_t, const u_int8_t *, bus_size_t);
};


/*
 * Utility macros; INTERNAL USE ONLY.
 */
#define	__abs_c(a,b)		__CONCAT(a,b)
#define	__abs_opname(op,size)	__abs_c(__abs_c(__abs_c(abs_,op),_),size)

#define	__abs_rs(sz, t, h, o)						\
	(*(t)->__abs_opname(r,sz))((t)->abs_cookie, h, o)
#define	__abs_ws(sz, t, h, o, v)					\
	(*(t)->__abs_opname(w,sz))((t)->abs_cookie, h, o, v)
#define	__abs_nonsingle(type, sz, t, h, o, a, c)			\
	(*(t)->__abs_opname(type,sz))((t)->abs_cookie, h, o, a, c)
#ifndef DEBUG
#define	__abs_aligned_nonsingle(type, sz, t, h, o, a, c)		\
	__abs_nonsingle(type, sz, (t), (h), (o), (a), (c))

#else
#define	__abs_aligned_nonsingle(type, sz, t, h, o, a, c)		\
    do {								\
	if (((unsigned long)a & (sz - 1)) != 0)				\
		panic("bus non-single %d-byte unaligned (to %p) at %s:%d", \
		    sz, a, __FILE__, __LINE__);				\
	(*(t)->__abs_opname(type,sz))((t)->abs_cookie, h, o, a, c);	\
    } while (0)
#endif
#define	__abs_set(type, sz, t, h, o, v, c)				\
	(*(t)->__abs_opname(type,sz))((t)->abs_cookie, h, o, v, c)
#define	__abs_copy(sz, t, h1, o1, h2, o2, cnt)			\
	(*(t)->__abs_opname(c,sz))((t)->abs_cookie, h1, o1, h2, o2, cnt)

/*
 * Mapping and unmapping operations.
 */
#define	bus_space_map(t, a, s, c, hp)					\
	(*(t)->abs_map)((t)->abs_cookie, (a), (s), (c), (hp))
#define alpha_bus_space_map_noacct bus_space_map
#define	bus_space_unmap(t, h, s)					\
	(*(t)->abs_unmap)((t)->abs_cookie, (h), (s))
#define alpha_bus_space_unmap_noacct bus_space_unmap
#define	bus_space_subregion(t, h, o, s, hp)				\
	(*(t)->abs_subregion)((t)->abs_cookie, (h), (o), (s), (hp))

#define	BUS_SPACE_MAP_CACHEABLE		0x01
#define	BUS_SPACE_MAP_LINEAR		0x02
#define	BUS_SPACE_MAP_PREFETCHABLE	0x04

/*
 * Allocation and deallocation operations.
 */
#define	bus_space_alloc(t, rs, re, s, a, b, c, ap, hp)			\
	(*(t)->abs_alloc)((t)->abs_cookie, (rs), (re), (s), (a), (b),	\
	    (c), (ap), (hp))
#define	bus_space_free(t, h, s)						\
	(*(t)->abs_free)((t)->abs_cookie, (h), (s))

/*
 * Get kernel virtual address for ranges mapped BUS_SPACE_MAP_LINEAR.
 */
#define bus_space_vaddr(t, h)						\
	(*(t)->abs_vaddr)((t)->abs_cookie, (h))

/*
 * Bus barrier operations.
 */
#define	bus_space_barrier(t, h, o, l, f)				\
	(*(t)->abs_barrier)((t)->abs_cookie, (h), (o), (l), (f))

#define BUS_SPACE_BARRIER_READ	0x01
#define BUS_SPACE_BARRIER_WRITE	0x02


/*
 * Bus read (single) operations.
 */
#define	bus_space_read_1(t, h, o)	__abs_rs(1,(t),(h),(o))
#define	bus_space_read_2(t, h, o)	__abs_rs(2,(t),(h),(o))
#define	bus_space_read_4(t, h, o)	__abs_rs(4,(t),(h),(o))
#define	bus_space_read_8(t, h, o)	__abs_rs(8,(t),(h),(o))


/*
 * Bus read (single) operations.
 */
#define	bus_space_read_raw_1(t, h, o)	__abs_rs(1,(t),(h),(o))
#define	bus_space_read_raw_2(t, h, o)	__abs_rs(2,(t),(h),(o))
#define	bus_space_read_raw_4(t, h, o)	__abs_rs(4,(t),(h),(o))
#define	bus_space_read_raw_8(t, h, o)	__abs_rs(8,(t),(h),(o))


/*
 * Bus read multiple operations.
 */
#define	bus_space_read_multi_1(t, h, o, a, c)				\
	__abs_nonsingle(rm,1,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_2(t, h, o, a, c)				\
	__abs_aligned_nonsingle(rm,2,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_4(t, h, o, a, c)				\
	__abs_aligned_nonsingle(rm,4,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_8(t, h, o, a, c)				\
	__abs_aligned_nonsingle(rm,8,(t),(h),(o),(a),(c))


/*
 *	void bus_space_read_raw_multi_N(bus_space_tag_t tag,
 *	    bus_space_handle_t bsh, bus_size_t offset,
 *	    u_int8_t *addr, size_t count);
 *
 * Read `count' bytes in 2, 4 or 8 byte wide quantities from bus space
 * described by tag/handle/offset and copy into buffer provided.  The buffer
 * must have proper alignment for the N byte wide entities.  Furthermore
 * possible byte-swapping should be done by these functions.
 */

#define	bus_space_read_raw_multi_2(t, h, o, a, c)			\
	__abs_nonsingle(rrm,2,(t),(h),(o),(a),(c))
#define	bus_space_read_raw_multi_4(t, h, o, a, c)			\
	__abs_nonsingle(rrm,4,(t),(h),(o),(a),(c))
#define	bus_space_read_raw_multi_8(t, h, o, a, c)			\
	__abs_nonsingle(rrm,8,(t),(h),(o),(a),(c))

/*
 * Bus read region operations.
 */
#define	bus_space_read_region_1(t, h, o, a, c)				\
	__abs_nonsingle(rr,1,(t),(h),(o),(a),(c))
#define	bus_space_read_region_2(t, h, o, a, c)				\
	__abs_aligned_nonsingle(rr,2,(t),(h),(o),(a),(c))
#define	bus_space_read_region_4(t, h, o, a, c)				\
	__abs_aligned_nonsingle(rr,4,(t),(h),(o),(a),(c))
#define	bus_space_read_region_8(t, h, o, a, c)				\
	__abs_aligned_nonsingle(rr,8,(t),(h),(o),(a),(c))


/*
 *	void bus_space_read_raw_region_N(bus_space_tag_t tag,
 *	    bus_space_handle_t bsh, bus_size_t offset,
 *	    u_int8_t *addr, size_t count);
 *
 * Read `count' bytes in 2, 4 or 8 byte wide quantities from bus space
 * described by tag/handle and starting at `offset' from the
 * buffer provided.  The buffer must have proper alignment for the N byte
 * wide entities.  Furthermore possible byte-swapping should be done by
 * these functions.
 */

#define bus_space_read_raw_region_2(t, h, o, a, c)			\
    bus_space_read_region_2((t), (h), (o), (u_int16_t *)(a), (c) >> 1)
#define bus_space_read_raw_region_4(t, h, o, a, c)			\
    bus_space_read_region_4((t), (h), (o), (u_int32_t *)(a), (c) >> 2)

/*
 * Bus write (single) operations.
 */
#define	bus_space_write_1(t, h, o, v)	__abs_ws(1,(t),(h),(o),(v))
#define	bus_space_write_2(t, h, o, v)	__abs_ws(2,(t),(h),(o),(v))
#define	bus_space_write_4(t, h, o, v)	__abs_ws(4,(t),(h),(o),(v))
#define	bus_space_write_8(t, h, o, v)	__abs_ws(8,(t),(h),(o),(v))


/*
 * Bus write raw (single) operations.
 */
#define	bus_space_write_raw_1(t, h, o, v)	__abs_ws(1,(t),(h),(o),(v))
#define	bus_space_write_raw_2(t, h, o, v)	__abs_ws(2,(t),(h),(o),(v))
#define	bus_space_write_raw_4(t, h, o, v)	__abs_ws(4,(t),(h),(o),(v))
#define	bus_space_write_raw_8(t, h, o, v)	__abs_ws(8,(t),(h),(o),(v))


/*
 * Bus write multiple operations.
 */
#define	bus_space_write_multi_1(t, h, o, a, c)				\
	__abs_nonsingle(wm,1,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_2(t, h, o, a, c)				\
	__abs_aligned_nonsingle(wm,2,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_4(t, h, o, a, c)				\
	__abs_aligned_nonsingle(wm,4,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_8(t, h, o, a, c)				\
	__abs_aligned_nonsingle(wm,8,(t),(h),(o),(a),(c))

/*
 *	void bus_space_write_raw_multi_N(bus_space_tag_t tag,
 *	    bus_space_handle_t bsh, bus_size_t offset,
 *	    u_int8_t *addr, size_t count);
 *
 * Write `count' bytes in 2, 4 or 8 byte wide quantities from the buffer
 * provided to bus space described by tag/handle/offset.  The buffer
 * must have proper alignment for the N byte wide entities.  Furthermore
 * possible byte-swapping should be done by these functions.
 */

#define	bus_space_write_raw_multi_2(t, h, o, a, c)			\
	__abs_nonsingle(wrm,2,(t),(h),(o),(a),(c))
#define	bus_space_write_raw_multi_4(t, h, o, a, c)			\
	__abs_nonsingle(wrm,4,(t),(h),(o),(a),(c))
#define	bus_space_write_raw_multi_8(t, h, o, a, c)			\
	__abs_nonsingle(wrm,8,(t),(h),(o),(a),(c))

/*
 * Bus write region operations.
 */
#define	bus_space_write_region_1(t, h, o, a, c)				\
	__abs_nonsingle(wr,1,(t),(h),(o),(a),(c))
#define	bus_space_write_region_2(t, h, o, a, c)				\
	__abs_aligned_nonsingle(wr,2,(t),(h),(o),(a),(c))
#define	bus_space_write_region_4(t, h, o, a, c)				\
	__abs_aligned_nonsingle(wr,4,(t),(h),(o),(a),(c))
#define	bus_space_write_region_8(t, h, o, a, c)				\
	__abs_aligned_nonsingle(wr,8,(t),(h),(o),(a),(c))


/*
 *	void bus_space_write_raw_region_N(bus_space_tag_t tag,
 *	    bus_space_handle_t bsh, bus_size_t offset,
 *	    const u_int8_t *addr, size_t count);
 *
 * Write `count' bytes in 2, 4 or 8 byte wide quantities to bus space
 * described by tag/handle and starting at `offset' from the
 * buffer provided.  The buffer must have proper alignment for the N byte
 * wide entities.  Furthermore possible byte-swapping should be done by
 * these functions.
 */

#define bus_space_write_raw_region_2(t, h, o, a, c)			\
    bus_space_write_region_2((t), (h), (o), (const u_int16_t *)(a), (c) >> 1)
#define bus_space_write_raw_region_4(t, h, o, a, c)			\
    bus_space_write_region_4((t), (h), (o), (const u_int32_t *)(a), (c) >> 2)

/*
 * Set multiple operations.
 */
#define	bus_space_set_multi_1(t, h, o, v, c)				\
	__abs_set(sm,1,(t),(h),(o),(v),(c))
#define	bus_space_set_multi_2(t, h, o, v, c)				\
	__abs_set(sm,2,(t),(h),(o),(v),(c))
#define	bus_space_set_multi_4(t, h, o, v, c)				\
	__abs_set(sm,4,(t),(h),(o),(v),(c))
#define	bus_space_set_multi_8(t, h, o, v, c)				\
	__abs_set(sm,8,(t),(h),(o),(v),(c))


/*
 * Set region operations.
 */
#define	bus_space_set_region_1(t, h, o, v, c)				\
	__abs_set(sr,1,(t),(h),(o),(v),(c))
#define	bus_space_set_region_2(t, h, o, v, c)				\
	__abs_set(sr,2,(t),(h),(o),(v),(c))
#define	bus_space_set_region_4(t, h, o, v, c)				\
	__abs_set(sr,4,(t),(h),(o),(v),(c))
#define	bus_space_set_region_8(t, h, o, v, c)				\
	__abs_set(sr,8,(t),(h),(o),(v),(c))


/*
 * Copy operations.
 */
#define	bus_space_copy_1(t, h1, o1, h2, o2, c)				\
	__abs_copy(1, t, h1, o1, h2, o2, c)
#define	bus_space_copy_2(t, h1, o1, h2, o2, c)				\
	__abs_copy(2, t, h1, o1, h2, o2, c)
#define	bus_space_copy_4(t, h1, o1, h2, o2, c)				\
	__abs_copy(4, t, h1, o1, h2, o2, c)
#define	bus_space_copy_8(t, h1, o1, h2, o2, c)				\
	__abs_copy(8, t, h1, o1, h2, o2, c)

/*
 * Bus DMA methods.
 */

/*
 * Flags used in various bus DMA methods.
 */
#define	BUS_DMA_WAITOK		0x0000	/* safe to sleep (pseudo-flag) */
#define	BUS_DMA_NOWAIT		0x0001	/* not safe to sleep */
#define	BUS_DMA_ALLOCNOW	0x0002	/* perform resource allocation now */
#define	BUS_DMA_COHERENT	0x0004	/* hint: map memory DMA coherent */
#define	BUS_DMA_BUS1		0x0010	/* placeholders for bus functions... */
#define	BUS_DMA_BUS2		0x0020
#define	BUS_DMA_BUS3		0x0040
#define	BUS_DMA_24BIT		0x0080	/* isadma map */
#define	BUS_DMA_STREAMING	0x0100	/* hint: sequential, unidirectional */
#define	BUS_DMA_READ		0x0200	/* mapping is device -> memory only */
#define	BUS_DMA_WRITE		0x0400	/* mapping is memory -> device only */
#define	BUS_DMA_ZERO		0x1000	/* zero memory in dmamem_alloc */
#define	BUS_DMA_64BIT		0x2000	/* device handles 64bit dva */

/*
 * Private flags stored in the DMA map.
 */
#define	DMAMAP_NO_COALESCE	0x40000000	/* don't coalesce adjacent
						   segments */

/* Forwards needed by prototypes below. */
struct mbuf;
struct uio;
struct alpha_sgmap;

/*
 * Operations performed by bus_dmamap_sync().
 */
#define BUS_DMASYNC_PREREAD	0x01
#define BUS_DMASYNC_POSTREAD	0x02
#define BUS_DMASYNC_PREWRITE	0x04
#define BUS_DMASYNC_POSTWRITE	0x08

/*
 *	alpha_bus_t
 *
 *	Busses supported by NetBSD/alpha, used by internal
 *	utility functions.  NOT TO BE USED BY MACHINE-INDEPENDENT
 *	CODE!
 */
typedef enum {
	ALPHA_BUS_TURBOCHANNEL,
	ALPHA_BUS_PCI,
	ALPHA_BUS_EISA,
	ALPHA_BUS_ISA,
	ALPHA_BUS_TLSB,
} alpha_bus_t;

typedef struct alpha_bus_dma_tag	*bus_dma_tag_t;
typedef struct alpha_bus_dmamap		*bus_dmamap_t;

/*
 *	bus_dma_segment_t
 *
 *	Describes a single contiguous DMA transaction.  Values
 *	are suitable for programming into DMA registers.
 */
struct alpha_bus_dma_segment {
	bus_addr_t	ds_addr;	/* DMA address */
	bus_size_t	ds_len;		/* length of transfer */
};
typedef struct alpha_bus_dma_segment	bus_dma_segment_t;

/*
 *	bus_dma_tag_t
 *
 *	A machine-dependent opaque type describing the implementation of
 *	DMA for a given bus.
 */
struct alpha_bus_dma_tag {
	void	*_cookie;		/* cookie used in the guts */
	bus_addr_t _wbase;		/* DMA window base */

	/*
	 * The following two members are used to chain DMA windows
	 * together.  If, during the course of a map load, the
	 * resulting physical memory address is too large to
	 * be addressed by the window, the next window will be
	 * attempted.  These would be chained together like so:
	 *
	 *	direct -> sgmap -> NULL
	 *  or
	 *	sgmap -> NULL
	 *  or
	 *	direct -> NULL
	 *
	 * If the window size is 0, it will not be checked (e.g.
	 * TurboChannel DMA).
	 */
	bus_size_t _wsize;
	struct alpha_bus_dma_tag *_next_window;

	/*
	 * Some chipsets have a built-in boundary constraint, independent
	 * of what the device requests.  This allows that boundary to
	 * be specified.  If the device has a more restrictive constraint,
	 * the map will use that, otherwise this boundary will be used.
	 * This value is ignored if 0.
	 */
	bus_size_t _boundary;

	/*
	 * A chipset may have more than one SGMAP window, so SGMAP
	 * windows also get a pointer to their SGMAP state.
	 */
	struct alpha_sgmap *_sgmap;

	/*
	 * Internal-use only utility methods.  NOT TO BE USED BY
	 * MACHINE-INDEPENDENT CODE!
	 */
	bus_dma_tag_t (*_get_tag)(bus_dma_tag_t, alpha_bus_t);

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

#define	alphabus_dma_get_tag(t, b)				\
	(*(t)->_get_tag)(t, b)

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
	(void)(t),						\
	(*(p)->_dm_window->_dmamap_unload)((p)->_dm_window, (p))
#define	bus_dmamap_sync(t, p, a, s, op)				\
	(void)(t), 						\
	(*(p)->_dm_window->_dmamap_sync)((p)->_dm_window, (p), (a), (s), (op))
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
struct alpha_bus_dmamap {
	/*
	 * PRIVATE MEMBERS: not for use by machine-independent code.
	 */
	bus_size_t	_dm_size;	/* largest DMA transfer mappable */
	int		_dm_segcnt;	/* number of segs this map can map */
	bus_size_t	_dm_maxsegsz;	/* largest possible segment */
	bus_size_t	_dm_boundary;	/* don't cross this */
	int		_dm_flags;	/* misc. flags */

	/*
	 * Private cookie to be used by the DMA back-end.
	 */
	void		*_dm_cookie;
	size_t		_dm_cookiesize;	/* size allocated for _dm_cookie */

	/*
	 * The DMA window that we ended up being mapped in.
	 */
	bus_dma_tag_t	_dm_window;

	/*
	 * PUBLIC MEMBERS: these are used by machine-independent code.
	 */
	bus_size_t	dm_mapsize;	/* size of the mapping */
	int		dm_nsegs;	/* # valid segments in mapping */
	bus_dma_segment_t dm_segs[1];	/* segments; variable length */
};

#ifdef _ALPHA_BUS_DMA_PRIVATE
int	_bus_dmamap_create(bus_dma_tag_t, bus_size_t, int, bus_size_t,
	    bus_size_t, int, bus_dmamap_t *);
void	_bus_dmamap_destroy(bus_dma_tag_t, bus_dmamap_t);

int	_bus_dmamap_load_direct(bus_dma_tag_t, bus_dmamap_t,
	    void *, bus_size_t, struct proc *, int);
int	_bus_dmamap_load_mbuf_direct(bus_dma_tag_t,
	    bus_dmamap_t, struct mbuf *, int);
int	_bus_dmamap_load_uio_direct(bus_dma_tag_t,
	    bus_dmamap_t, struct uio *, int);
int	_bus_dmamap_load_raw_direct(bus_dma_tag_t,
	    bus_dmamap_t, bus_dma_segment_t *, int, bus_size_t, int);

void	_bus_dmamap_unload(bus_dma_tag_t, bus_dmamap_t);
void	_bus_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, bus_addr_t,
	    bus_size_t, int);

int	_bus_dmamem_alloc(bus_dma_tag_t tag, bus_size_t size,
	    bus_size_t alignment, bus_size_t boundary,
	    bus_dma_segment_t *segs, int nsegs, int *rsegs, int flags);
int	_bus_dmamem_alloc_range(bus_dma_tag_t tag, bus_size_t size,
	    bus_size_t alignment, bus_size_t boundary,
	    bus_dma_segment_t *segs, int nsegs, int *rsegs, int flags,
	    paddr_t low, paddr_t high);
void	_bus_dmamem_free(bus_dma_tag_t tag, bus_dma_segment_t *segs,
	    int nsegs);
int	_bus_dmamem_map(bus_dma_tag_t tag, bus_dma_segment_t *segs,
	    int nsegs, size_t size, caddr_t *kvap, int flags);
void	_bus_dmamem_unmap(bus_dma_tag_t tag, caddr_t kva,
	    size_t size);
paddr_t	_bus_dmamem_mmap(bus_dma_tag_t tag, bus_dma_segment_t *segs,
	    int nsegs, off_t off, int prot, int flags);
#endif /* _ALPHA_BUS_DMA_PRIVATE */

#endif /* _KERNEL */

#endif /* _MACHINE_BUS_H_ */
