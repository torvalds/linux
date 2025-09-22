/*	$OpenBSD: bus.h,v 1.38 2024/10/22 22:01:58 jsg Exp $	*/
/*	$NetBSD: bus.h,v 1.31 2001/09/21 15:30:41 wiz Exp $	*/

/*-
 * Copyright (c) 1996, 1997, 1998, 2001 The NetBSD Foundation, Inc.
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
 * Copyright (c) 1997-1999, 2001 Eduardo E. Horvath. All rights reserved.
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
#define _MACHINE_BUS_H_

#include <sys/atomic.h>

#ifdef _KERNEL

/*
 * Debug hooks
 */

#define	BSDB_ACCESS	0x01
#define BSDB_MAP	0x02
#define BSDB_ASSERT	0x04
#define BSDB_MAPDETAIL	0x08
#define	BSDB_ALL_ACCESS	0x10
extern int bus_space_debug;

#define BSHDB_ACCESS	0x01
#define BSHDB_NO_ACCESS	0x02

#if defined(BUS_SPACE_DEBUG)
#include <sys/systm.h>
#define BUS_SPACE_PRINTF(l, s) do {				\
	if(bus_space_debug & (l)) printf s;			\
} while(0)
#define BUS_SPACE_TRACE(t, h, s) do {				\
	if ( (((bus_space_debug & BSDB_ALL_ACCESS) != 0) &&	\
		(((h).bh_flags & BSHDB_NO_ACCESS) == 0)) ||	\
	     (((bus_space_debug & BSDB_ACCESS) != 0) &&		\
		(((h).bh_flags & BSHDB_ACCESS) != 0)))		\
		printf s;					\
	} while(0)
#define BUS_SPACE_SET_FLAGS(t, h, f) ((h).bh_flags |= (f))
#define BUS_SPACE_CLEAR_FLAGS(t, h, f) ((h).bh_flags &= ~(f))
#define BUS_SPACE_FLAG_DECL(s)	int s
#define BUS_SPACE_SAVE_FLAGS(t, h, s) (s = (h).bh_flags)
#define BUS_SPACE_RESTORE_FLAGS(t, h, s) (s = (h).bh_flags)
#define BUS_SPACE_ASSERT(t, h, o, n) do {			\
	if (bus_space_debug & BSDB_ASSERT)			\
		bus_space_assert(t, &(h), o, n);		\
	} while(0)
#else /* BUS_SPACE_DEBUG */
#define BUS_SPACE_PRINTF(l, s)
#define BUS_SPACE_TRACE(t, h, s)
#define BUS_SPACE_SET_FLAGS(t, h, f)
#define BUS_SPACE_CLEAR_FLAGS(t, h, f)
#define BUS_SPACE_FLAG_DECL(s)
#define BUS_SPACE_SAVE_FLAGS(t, h, s)
#define BUS_SPACE_RESTORE_FLAGS(t, h, s)
#define BUS_SPACE_ASSERT(t, h, o, n)
#endif /* BUS_SPACE_DEBUG */


/*
 * UPA and SBus spaces are non-cached and big endian
 * (except for RAM and PROM)
 *
 * PCI spaces are non-cached and little endian
 */

enum sparc_bus_type { 
	UPA_BUS_SPACE,
	SBUS_BUS_SPACE,
	PCI_CONFIG_BUS_SPACE,
	PCI_IO_BUS_SPACE,
	PCI_MEMORY_BUS_SPACE,
	LAST_BUS_SPACE
}; 
/* For backwards compatibility */
#define SPARC_BUS_SPACE	UPA_BUS_SPACE

/*
 * Bus address and size types
 */
typedef const struct sparc_bus_space_tag	*bus_space_tag_t;
typedef u_long	bus_addr_t;
typedef u_long	bus_size_t;


typedef struct _bus_space_handle {
        paddr_t		bh_ptr;
#ifdef BUS_SPACE_DEBUG
	bus_space_tag_t	bh_tag;
	bus_size_t	bh_size;
	int		bh_flags;
#endif
} bus_space_handle_t;

/* For buses which have an iospace. */
#define BUS_ADDR_IOSPACE(x)     ((x)>>32)
#define BUS_ADDR_PADDR(x)       ((x)&0xffffffff)
#define BUS_ADDR(io, pa)        ((((bus_addr_t)io)<<32)|(pa))

/*
 * Access methods for bus resources and address space.
 */

struct sparc_bus_space_tag {
	void	*cookie;
	bus_space_tag_t	parent;
	enum sparc_bus_type default_type;
        u_int8_t	asi;
        u_int8_t	sasi;
	char	name[32];

	int     (*sparc_bus_alloc)(bus_space_tag_t, 
		bus_space_tag_t,
		bus_addr_t, bus_addr_t,
		bus_size_t, bus_size_t, bus_size_t, 
		int, bus_addr_t *, bus_space_handle_t *);

	void	(*sparc_bus_free)(bus_space_tag_t, 
		bus_space_tag_t,
		bus_space_handle_t, bus_size_t);

	int	(*sparc_bus_map)(bus_space_tag_t,
		bus_space_tag_t,
		bus_addr_t,	bus_size_t,
		int, bus_space_handle_t *);

	int	(*sparc_bus_protect)(bus_space_tag_t,
		bus_space_tag_t,
		bus_space_handle_t, bus_size_t, int);

	int	(*sparc_bus_unmap)(bus_space_tag_t,
		bus_space_tag_t,
		bus_space_handle_t, bus_size_t);

	int	(*sparc_bus_subregion)(bus_space_tag_t,
		bus_space_tag_t,
		bus_space_handle_t, bus_size_t,
		bus_size_t, bus_space_handle_t *);

	paddr_t	(*sparc_bus_mmap)(bus_space_tag_t,
		bus_space_tag_t,
		bus_addr_t, off_t, int, int);

	void	*(*sparc_intr_establish)(bus_space_tag_t,
		bus_space_tag_t,
		int, int, int,
		int (*)(void *), void *,
		const char *);
	void	*(*sparc_intr_establish_cpu)(bus_space_tag_t,
		bus_space_tag_t,
		int, int, int,
		struct cpu_info *,
		int (*)(void *), void *,
		const char *);

	bus_addr_t (*sparc_bus_addr)(bus_space_tag_t,
		bus_space_tag_t, bus_space_handle_t);
};

/*
 * Bus space function prototypes.
 */
int		bus_space_alloc(
				bus_space_tag_t,
				bus_addr_t,		/* reg start */
				bus_addr_t,		/* reg end */
				bus_size_t,		/* size */
				bus_size_t,		/* alignment */
				bus_size_t,		/* boundary */
				int,			/* flags */
				bus_addr_t *, 
				bus_space_handle_t *);
void		bus_space_free(
				bus_space_tag_t,
				bus_space_handle_t,
				bus_size_t);
int		bus_space_map(
				bus_space_tag_t,
				bus_addr_t,
				bus_size_t,
				int,			/*flags*/
				bus_space_handle_t *);
int		bus_space_protect(
				bus_space_tag_t,
				bus_space_handle_t,
				bus_size_t,
				int);			/*flags*/
int		bus_space_unmap(
				bus_space_tag_t,
				bus_space_handle_t,
				bus_size_t);
int		bus_space_subregion(
				bus_space_tag_t,
				bus_space_handle_t,
				bus_size_t,
				bus_size_t,
				bus_space_handle_t *);
static void	bus_space_barrier(
				bus_space_tag_t,
				bus_space_handle_t,
				bus_size_t,
				bus_size_t,
				int);
paddr_t		bus_space_mmap(
				bus_space_tag_t,
				bus_addr_t,		/*addr*/
				off_t,			/*offset*/
				int,			/*prot*/
				int);			/*flags*/
void	       *bus_intr_establish(
				bus_space_tag_t,
				int,			/*bus-specific intr*/
				int,			/*device class level,
							  see machine/intr.h*/
				int,			/*flags*/
				int (*)(void *),	/*handler*/
				void *,			/*handler arg*/
				const char *);		/*what*/
void	       *bus_intr_establish_cpu(
				bus_space_tag_t,
				int,			/*bus-specific intr*/
				int,			/*device class level,
							  see machine/intr.h*/
				int,			/*flags*/
				struct cpu_info *,	/*cpu*/
				int (*)(void *),	/*handler*/
				void *,			/*handler arg*/
				const char *);		/*what*/
void	       *bus_intr_allocate(
				bus_space_tag_t,
				int (*)(void *),	/*handler*/
				void *,			/*handler arg*/
				int,			/*number*/
				int,			/*pil*/
				volatile u_int64_t *,	/*map*/
				volatile u_int64_t *,	/*clr*/
				const char *);		/*what*/
void		bus_intr_free(void *);
void		bus_space_render_tag(
				bus_space_tag_t,
				char *,
				size_t);
void	       *bus_space_vaddr(
				bus_space_tag_t,
				bus_space_handle_t);

#ifdef BUS_SPACE_DEBUG
void bus_space_assert(bus_space_tag_t,
	const bus_space_handle_t *,
	bus_size_t, int);
void bus_space_render_tag(bus_space_tag_t, char*, size_t);
#endif /* BUS_SPACE_DEBUG */


#define _BS_PRECALL(t,f)		\
	while (t->f == NULL)		\
		t = t->parent;
#define _BS_POSTCALL

#define _BS_CALL(t,f)			\
	(*(t)->f)

/* flags for bus_space_barrier() */
#define	BUS_SPACE_BARRIER_READ	0x01		/* force read barrier */
#define	BUS_SPACE_BARRIER_WRITE	0x02		/* force write barrier */

static inline void
bus_space_barrier(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    bus_size_t s, int f)
{
#ifdef notyet
	switch (f) {
	case (BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE):
		__membar("#LoadLoad|#StoreStore");
		break;
	case BUS_SPACE_BARRIER_READ:
		membar("#LoadLoad");
		break;
	case BUS_SPACE_BARRIER_WRITE:
		membar("#StoreStore");
		break;
	default:
		break;
	}
#else
	__membar("#Sync");
#endif
}

#include <sparc64/sparc64/busop.h>

/* flags for bus space map functions */
#define BUS_SPACE_MAP_CACHEABLE		0x0001
#define BUS_SPACE_MAP_LINEAR		0x0002
#define BUS_SPACE_MAP_READONLY		0x0004
#define BUS_SPACE_MAP_PREFETCHABLE	0x0008
#define BUS_SPACE_MAP_PROMADDRESS	0x0010
#define BUS_SPACE_MAP_BUS1	0x0100	/* placeholders for bus functions... */
#define BUS_SPACE_MAP_BUS2	0x0200
#define BUS_SPACE_MAP_BUS3	0x0400
#define BUS_SPACE_MAP_BUS4	0x0800


/* flags for bus_intr_establish() */
#define BUS_INTR_ESTABLISH_MPSAFE	0x0001
#define BUS_INTR_ESTABLISH_SOFTINTR	0x0002

/*
 * Flags used in various bus DMA methods.
 */
#define	BUS_DMA_WAITOK		0x0000	/* safe to sleep (pseudo-flag) */
#define	BUS_DMA_NOWAIT		0x0001	/* not safe to sleep */
#define	BUS_DMA_ALLOCNOW	0x0002	/* perform resource allocation now */
#define	BUS_DMA_COHERENT	0x0004	/* hint: map memory DMA coherent */
#define	BUS_DMA_NOWRITE		0x0008	/* I suppose the following two should default on */
#define	BUS_DMA_BUS1		0x0010	/* placeholders for bus functions... */
#define	BUS_DMA_BUS2		0x0020
#define	BUS_DMA_BUS3		0x0040
#define	BUS_DMA_BUS4		0x0080
#define	BUS_DMA_STREAMING	0x0100	/* hint: sequential, unidirectional */
#define	BUS_DMA_READ		0x0200	/* mapping is device -> memory only */
#define	BUS_DMA_WRITE		0x0400	/* mapping is memory -> device only */
#define	BUS_DMA_ZERO		0x0800	/* zero memory in dmamem_alloc */
#define	BUS_DMA_OVERRUN		0x1000  /* tolerate DMA overruns */
#define	BUS_DMA_64BIT		0x2000	/* device handles 64bit dva */

#define	BUS_DMA_NOCACHE		BUS_DMA_BUS1
#define	BUS_DMA_DVMA		BUS_DMA_BUS2	/* Don't bother with alignment */
#define	BUS_DMA_24BIT		BUS_DMA_BUS3	/* 24bit device */

#define BUS_DMA_RAW	BUS_DMA_STREAMING

/* Forwards needed by prototypes below. */
struct mbuf;
struct uio;

/*
 * Operations performed by bus_dmamap_sync().
 */
#define	BUS_DMASYNC_PREREAD	0x01	/* pre-read synchronization */
#define	BUS_DMASYNC_POSTREAD	0x02	/* post-read synchronization */
#define	BUS_DMASYNC_PREWRITE	0x04	/* pre-write synchronization */
#define	BUS_DMASYNC_POSTWRITE	0x08	/* post-write synchronization */

typedef struct sparc_bus_dma_tag	*bus_dma_tag_t;
typedef struct sparc_bus_dmamap		*bus_dmamap_t;

/*
 *	bus_dma_segment_t
 *
 *	Describes a single contiguous DMA transaction.  Values
 *	are suitable for programming into DMA registers.
 */
struct sparc_bus_dma_segment {
	bus_addr_t	ds_addr;	/* DVMA address */
	bus_size_t	ds_len;		/* length of transfer */
	/*
	 * The following is to support bus_dmamem_alloc()'s
	 * odd interface.  Only the values in the first
	 * segment are used.  This means that 3/5ths of
	 * most segments are useless space (and mbufs use 1024
	 * segments).
	 */
	bus_size_t	_ds_boundary;	/* don't cross this */
	bus_size_t	_ds_align;	/* align to this */
	void		*_ds_mlist;	/* XXX - dmamap_alloc'ed pages */
};
typedef struct sparc_bus_dma_segment	bus_dma_segment_t;


/*
 *	bus_dma_tag_t
 *
 *	A machine-dependent opaque type describing the implementation of
 *	DMA for a given bus.
 */
struct sparc_bus_dma_tag {
	void	*_cookie;		/* cookie used in the guts */
	struct sparc_bus_dma_tag* _parent;

	/*
	 * DMA mapping methods.
	 */
	int	(*_dmamap_create)(bus_dma_tag_t, bus_dma_tag_t, bus_size_t,
		    int, bus_size_t, bus_size_t, int, bus_dmamap_t *);
	void	(*_dmamap_destroy)(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t);
	int	(*_dmamap_load)(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t,
		    void *, bus_size_t, struct proc *, int);
	int	(*_dmamap_load_mbuf)(bus_dma_tag_t, bus_dma_tag_t,
		    bus_dmamap_t, struct mbuf *, int);
	int	(*_dmamap_load_uio)(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t,
		    struct uio *, int);
	int	(*_dmamap_load_raw)(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t,
		    bus_dma_segment_t *, int, bus_size_t, int);
	void	(*_dmamap_unload)(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t);
	void	(*_dmamap_sync)(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t,
		    bus_addr_t, bus_size_t, int);

	/*
	 * DMA memory utility functions.
	 */
	int	(*_dmamem_alloc)(bus_dma_tag_t, bus_dma_tag_t, bus_size_t,
		    bus_size_t, bus_size_t, bus_dma_segment_t *, int, int *,
		    int);
	void	(*_dmamem_free)(bus_dma_tag_t, bus_dma_tag_t,
		    bus_dma_segment_t *, int);
	int	(*_dmamem_map)(bus_dma_tag_t, bus_dma_tag_t,
		    bus_dma_segment_t *, int, size_t, caddr_t *, int);
	void	(*_dmamem_unmap)(bus_dma_tag_t, bus_dma_tag_t, caddr_t,
		    size_t);
	paddr_t	(*_dmamem_mmap)(bus_dma_tag_t, bus_dma_tag_t,
		    bus_dma_segment_t *, int, off_t, int, int);
};

#define _BD_PRECALL(t,f)		\
	while (t->f == NULL) {		\
		t = t->_parent;		\
	}
#define _BD_CALL(t,f)			\
	(*(t)->f)
#define _BD_POSTCALL

static inline int
bus_dmamap_create(bus_dma_tag_t t, bus_size_t s, int n, bus_size_t m,
    bus_size_t b, int f, bus_dmamap_t *p)
{
	int r;
	const bus_dma_tag_t t0 = t;
	_BD_PRECALL(t, _dmamap_create);
	r = _BD_CALL(t, _dmamap_create)(t, t0, s, n, m, b, f, p);
	_BD_POSTCALL;
	return (r);
}
static inline void
bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t p)
{
	const bus_dma_tag_t t0 = t;
	_BD_PRECALL(t, _dmamap_destroy);
	_BD_CALL(t, _dmamap_destroy)(t, t0, p);
	_BD_POSTCALL;
}
static inline int
bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t m, void *b, bus_size_t s,
    struct proc *p, int f)
{
	const bus_dma_tag_t t0 = t;
	int r;
	_BD_PRECALL(t, _dmamap_load);
	r = _BD_CALL(t, _dmamap_load)(t, t0, m, b, s, p, f);
	_BD_POSTCALL;
	return (r);
}
static inline int
bus_dmamap_load_mbuf(bus_dma_tag_t t, bus_dmamap_t m, struct mbuf *b,
    int f)
{
	const bus_dma_tag_t t0 = t;
	int r;
	_BD_PRECALL(t, _dmamap_load_mbuf);
	r = _BD_CALL(t, _dmamap_load_mbuf)(t, t0, m, b, f);
	_BD_POSTCALL;
	return (r);
}
static inline int
bus_dmamap_load_uio(bus_dma_tag_t t, bus_dmamap_t m, struct uio * u, int f)
{
	const bus_dma_tag_t t0 = t;
	int r;
	_BD_PRECALL(t, _dmamap_load_uio);
	r = _BD_CALL(t, _dmamap_load_uio)(t, t0, m, u, f);
	_BD_POSTCALL;
	return (r);
}
static inline int
bus_dmamap_load_raw(bus_dma_tag_t t, bus_dmamap_t m, bus_dma_segment_t *sg,
    int n, bus_size_t s, int f)
{
	const bus_dma_tag_t t0 = t;
	int r;
	_BD_PRECALL(t, _dmamap_load_raw);
	r = _BD_CALL(t, _dmamap_load_raw)(t, t0, m, sg, n, s, f);
	_BD_POSTCALL;
	return (r);
}
static inline void
bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t p)
{
	const bus_dma_tag_t t0 = t;
	_BD_PRECALL(t, _dmamap_unload);
	_BD_CALL(t, _dmamap_unload)(t, t0, p);
	_BD_POSTCALL;
}
static inline void
bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t p, bus_addr_t o, bus_size_t l,
    int ops)
{
	const bus_dma_tag_t t0 = t;
	_BD_PRECALL(t, _dmamap_sync);
	_BD_CALL(t, _dmamap_sync)(t, t0, p, o, l, ops);
	_BD_POSTCALL;
}
static inline int
bus_dmamem_alloc(bus_dma_tag_t t, bus_size_t s, bus_size_t a, bus_size_t b,
    bus_dma_segment_t *sg, int n, int *r, int f)
{
	const bus_dma_tag_t t0 = t;
	int ret;
	_BD_PRECALL(t, _dmamem_alloc);
	ret = _BD_CALL(t, _dmamem_alloc)(t, t0, s, a, b, sg, n, r, f);
	_BD_POSTCALL;
	return (ret);
}
static inline void
bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *sg, int n)
{
	const bus_dma_tag_t t0 = t;
	_BD_PRECALL(t, _dmamem_free);
	_BD_CALL(t, _dmamem_free)(t, t0, sg, n);
	_BD_POSTCALL;
}
static inline int
bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *sg, int n, size_t s,
    caddr_t *k, int f)
{
	const bus_dma_tag_t t0 = t;
	int r;
	_BD_PRECALL(t, _dmamem_map);
	r = _BD_CALL(t, _dmamem_map)(t, t0, sg, n, s, k, f);
	_BD_POSTCALL;
	return (r);
}
static inline void
bus_dmamem_unmap(bus_dma_tag_t t, caddr_t k, size_t s)
{
	const bus_dma_tag_t t0 = t;
	_BD_PRECALL(t, _dmamem_unmap);
	_BD_CALL(t, _dmamem_unmap)(t, t0, k, s);
	_BD_POSTCALL;
}
static inline paddr_t
bus_dmamem_mmap(bus_dma_tag_t t, bus_dma_segment_t *sg, int n, off_t o, int p,
    int f)
{
	const bus_dma_tag_t t0 = t;
	int r;
	_BD_PRECALL(t, _dmamem_mmap);
	r = _BD_CALL(t, _dmamem_mmap)(t, t0, sg, n, o, p, f);
	_BD_POSTCALL;
	return (r);
}

/*
 *	bus_dmamap_t
 *
 *	Describes a DMA mapping.
 */
struct sparc_bus_dmamap {
	/*
	 * PRIVATE MEMBERS: not for use by machine-independent code.
	 */
	bus_addr_t	_dm_dvmastart;	/* start and size of allocated */
	bus_size_t	_dm_dvmasize;	/* DVMA segment for this map */

	bus_size_t	_dm_size;	/* largest DMA transfer mappable */
	bus_size_t	_dm_maxsegsz;	/* largest possible segment */
	bus_size_t	_dm_boundary;	/* don't cross this */
	int		_dm_segcnt;	/* number of segs this map can map */
	int		_dm_flags;	/* misc. flags */
#define _DM_TYPE_LOAD	0
#define _DM_TYPE_SEGS	1
#define _DM_TYPE_UIO	2
#define _DM_TYPE_MBUF	3
	int		_dm_type;	/* mapping type: raw, uio, mbuf, etc */
	void		*_dm_source;	/* source mbuf/uio/etc. for unload */

	void		*_dm_cookie;	/* cookie for bus-specific functions */

	/*
	 * PUBLIC MEMBERS: these are used by machine-independent code.
	 */
	bus_size_t	dm_mapsize;	/* size of the mapping */
	int		dm_nsegs;	/* # valid segments in mapping */

	bus_dma_segment_t dm_segs[1];	/* segments; variable length */
};

#endif /* _KERNEL */

#endif /* _MACHINE_BUS_H_ */

