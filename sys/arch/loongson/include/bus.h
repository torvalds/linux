/*	$OpenBSD: bus.h,v 1.8 2020/04/14 17:35:28 kettenis Exp $	*/

/*
 * Copyright (c) 2003-2004 Opsycon AB Sweden.  All rights reserved.
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MACHINE_BUS_H_
#define _MACHINE_BUS_H_

#ifdef __STDC__
#define CAT(a,b)	a##b
#define CAT3(a,b,c)	a##b##c
#else
#define CAT(a,b)	a/**/b
#define CAT3(a,b,c)	a/**/b/**/c
#endif

/*
 * Bus access types.
 */
struct mips_bus_space;
typedef u_long bus_addr_t;
typedef u_long bus_size_t;
typedef u_long bus_space_handle_t;
typedef struct mips_bus_space *bus_space_tag_t;

struct mips_bus_space {
	bus_addr_t	bus_base;
	void		*bus_private;
	u_int8_t	(*_space_read_1)(bus_space_tag_t , bus_space_handle_t,
			  bus_size_t);
	void		(*_space_write_1)(bus_space_tag_t , bus_space_handle_t,
			  bus_size_t, u_int8_t);
	u_int16_t	(*_space_read_2)(bus_space_tag_t , bus_space_handle_t,
			  bus_size_t);
	void		(*_space_write_2)(bus_space_tag_t , bus_space_handle_t,
			  bus_size_t, u_int16_t);
	u_int32_t	(*_space_read_4)(bus_space_tag_t , bus_space_handle_t,
			  bus_size_t);
	void		(*_space_write_4)(bus_space_tag_t , bus_space_handle_t,
			  bus_size_t, u_int32_t);
	u_int64_t	(*_space_read_8)(bus_space_tag_t , bus_space_handle_t,
			  bus_size_t);
	void		(*_space_write_8)(bus_space_tag_t , bus_space_handle_t,
			  bus_size_t, u_int64_t);
	void		(*_space_read_raw_2)(bus_space_tag_t, bus_space_handle_t,
			  bus_addr_t, u_int8_t *, bus_size_t);
	void		(*_space_write_raw_2)(bus_space_tag_t, bus_space_handle_t,
			  bus_addr_t, const u_int8_t *, bus_size_t);
	void		(*_space_read_raw_4)(bus_space_tag_t, bus_space_handle_t,
			  bus_addr_t, u_int8_t *, bus_size_t);
	void		(*_space_write_raw_4)(bus_space_tag_t, bus_space_handle_t,
			  bus_addr_t, const u_int8_t *, bus_size_t);
	void		(*_space_read_raw_8)(bus_space_tag_t, bus_space_handle_t,
			  bus_addr_t, u_int8_t *, bus_size_t);
	void		(*_space_write_raw_8)(bus_space_tag_t, bus_space_handle_t,
			  bus_addr_t, const u_int8_t *, bus_size_t);
	int		(*_space_map)(bus_space_tag_t , bus_addr_t,
			  bus_size_t, int, bus_space_handle_t *);
	void		(*_space_unmap)(bus_space_tag_t, bus_space_handle_t,
			  bus_size_t);
	int		(*_space_subregion)(bus_space_tag_t, bus_space_handle_t,
			  bus_size_t, bus_size_t, bus_space_handle_t *);
	void *		(*_space_vaddr)(bus_space_tag_t, bus_space_handle_t);
	paddr_t		(*_space_mmap)(bus_space_tag_t, bus_addr_t, off_t,
			    int, int);
};

#define	bus_space_read_1(t, h, o) (*(t)->_space_read_1)((t), (h), (o))
#define	bus_space_read_2(t, h, o) (*(t)->_space_read_2)((t), (h), (o))
#define	bus_space_read_4(t, h, o) (*(t)->_space_read_4)((t), (h), (o))
#define	bus_space_read_8(t, h, o) (*(t)->_space_read_8)((t), (h), (o))

#define	bus_space_write_1(t, h, o, v) (*(t)->_space_write_1)((t), (h), (o), (v))
#define	bus_space_write_2(t, h, o, v) (*(t)->_space_write_2)((t), (h), (o), (v))
#define	bus_space_write_4(t, h, o, v) (*(t)->_space_write_4)((t), (h), (o), (v))
#define	bus_space_write_8(t, h, o, v) (*(t)->_space_write_8)((t), (h), (o), (v))

#define	bus_space_read_raw_multi_2(t, h, a, b, l) \
	(*(t)->_space_read_raw_2)((t), (h), (a), (b), (l))
#define	bus_space_read_raw_multi_4(t, h, a, b, l) \
	(*(t)->_space_read_raw_4)((t), (h), (a), (b), (l))
#define	bus_space_read_raw_multi_8(t, h, a, b, l) \
	(*(t)->_space_read_raw_8)((t), (h), (a), (b), (l))

#define	bus_space_write_raw_multi_2(t, h, a, b, l) \
	(*(t)->_space_write_raw_2)((t), (h), (a), (b), (l))
#define	bus_space_write_raw_multi_4(t, h, a, b, l) \
	(*(t)->_space_write_raw_4)((t), (h), (a), (b), (l))
#define	bus_space_write_raw_multi_8(t, h, a, b, l) \
	(*(t)->_space_write_raw_8)((t), (h), (a), (b), (l))

#define	bus_space_map(t, o, s, c, p) (*(t)->_space_map)((t), (o), (s), (c), (p))
#define	bus_space_unmap(t, h, s) (*(t)->_space_unmap)((t), (h), (s))
#define	bus_space_subregion(t, h, o, s, p) \
    (*(t)->_space_subregion)((t), (h), (o), (s), (p))

#define	BUS_SPACE_MAP_CACHEABLE		0x01
#define	BUS_SPACE_MAP_LINEAR		0x02
#define	BUS_SPACE_MAP_PREFETCHABLE	0x04

#define	bus_space_vaddr(t, h)	(*(t)->_space_vaddr)((t), (h))
#define	bus_space_mmap(t, a, o, p, f) \
	(*(t)->_space_mmap)((t), (a), (o), (p), (f))

/*----------------------------------------------------------------------------*/
#define bus_space_read_multi(n,m)					      \
static __inline void							      \
CAT(bus_space_read_multi_,n)(bus_space_tag_t bst, bus_space_handle_t bsh,     \
     bus_size_t o, CAT3(u_int,m,_t) *x, size_t cnt)			      \
{									      \
	while (cnt--)							      \
		*x++ = CAT(bus_space_read_,n)(bst, bsh, o);		      \
}

bus_space_read_multi(1,8)
bus_space_read_multi(2,16)
bus_space_read_multi(4,32)
bus_space_read_multi(8,64)

/*----------------------------------------------------------------------------*/
#define bus_space_read_region(n,m)					      \
static __inline void							      \
CAT(bus_space_read_region_,n)(bus_space_tag_t bst, bus_space_handle_t bsh,    \
     bus_addr_t ba, CAT3(u_int,m,_t) *x, size_t cnt)			      \
{									      \
	while (cnt--) {							      \
		*x++ = CAT(bus_space_read_,n)(bst, bsh, ba);		      \
		ba += (n);						      \
	}								      \
}

bus_space_read_region(1,8)
bus_space_read_region(2,16)
bus_space_read_region(4,32)
bus_space_read_region(8,64)

/*----------------------------------------------------------------------------*/
#define bus_space_read_raw_region(n,m)					      \
static __inline void							      \
CAT(bus_space_read_raw_region_,n)(bus_space_tag_t bst,			      \
     bus_space_handle_t bsh,						      \
     bus_addr_t ba, u_int8_t *x, size_t cnt)				      \
{									      \
	cnt >>= ((n) >> 1);						      \
	while (cnt--) {							      \
		CAT(bus_space_read_raw_multi_,n)(bst, bsh, ba, x, (n));	      \
		ba += (n);						      \
		x += (n);						      \
	}								      \
}

bus_space_read_raw_region(2,16)
bus_space_read_raw_region(4,32)
bus_space_read_raw_region(8,64)

/*----------------------------------------------------------------------------*/
#define bus_space_write_multi(n,m)					      \
static __inline void							      \
CAT(bus_space_write_multi_,n)(bus_space_tag_t bst, bus_space_handle_t bsh,    \
     bus_size_t o, const CAT3(u_int,m,_t) *x, size_t cnt)		      \
{									      \
	while (cnt--)							      \
		CAT(bus_space_write_,n)(bst, bsh, o, *x++);		      \
}

bus_space_write_multi(1,8)
bus_space_write_multi(2,16)
bus_space_write_multi(4,32)
bus_space_write_multi(8,64)

/*----------------------------------------------------------------------------*/
#define bus_space_write_region(n,m)					      \
static __inline void							      \
CAT(bus_space_write_region_,n)(bus_space_tag_t bst, bus_space_handle_t bsh,   \
     bus_addr_t ba, const CAT3(u_int,m,_t) *x, size_t cnt)		      \
{									      \
	while (cnt--) {							      \
		CAT(bus_space_write_,n)(bst, bsh, ba, *x++);		      \
		ba += (n);						      \
	}								      \
}

bus_space_write_region(1,8)
bus_space_write_region(2,16)
bus_space_write_region(4,32)
bus_space_write_region(8,64)

/*----------------------------------------------------------------------------*/
#define bus_space_write_raw_region(n,m)					      \
static __inline void							      \
CAT(bus_space_write_raw_region_,n)(bus_space_tag_t bst,			      \
     bus_space_handle_t bsh,						      \
     bus_addr_t ba, const u_int8_t *x, size_t cnt)		              \
{									      \
	cnt >>= ((n) >> 1);						      \
	while (cnt--) {							      \
		CAT(bus_space_write_raw_multi_,n)(bst, bsh, ba, x, (n));      \
		ba += (n);						      \
		x += (n);						      \
	}								      \
}

bus_space_write_raw_region(2,16)
bus_space_write_raw_region(4,32)
bus_space_write_raw_region(8,64)

/*----------------------------------------------------------------------------*/
#define bus_space_set_region(n,m)					      \
static __inline void							      \
CAT(bus_space_set_region_,n)(bus_space_tag_t bst, bus_space_handle_t bsh,     \
     bus_addr_t ba, CAT3(u_int,m,_t) x, size_t cnt)			      \
{									      \
	while (cnt--) {							      \
		CAT(bus_space_write_,n)(bst, bsh, ba, x);		      \
		ba += (n);						      \
	}								      \
}

bus_space_set_region(1,8)
bus_space_set_region(2,16)
bus_space_set_region(4,32)
bus_space_set_region(8,64)

/*----------------------------------------------------------------------------*/
static __inline void
bus_space_copy_1(void *v, bus_space_handle_t h1, bus_size_t o1,
	bus_space_handle_t h2, bus_size_t o2, bus_size_t c)
{
	char *s = (char *)(h1 + o1);
	char *d = (char *)(h2 + o2);

	while (c--)
		*d++ = *s++;
}


static __inline void
bus_space_copy_2(void *v, bus_space_handle_t h1, bus_size_t o1,
	bus_space_handle_t h2, bus_size_t o2, bus_size_t c)
{
	short *s = (short *)(h1 + o1);
	short *d = (short *)(h2 + o2);

	while (c--)
		*d++ = *s++;
}

static __inline void
bus_space_copy_4(void *v, bus_space_handle_t h1, bus_size_t o1,
	bus_space_handle_t h2, bus_size_t o2, bus_size_t c)
{
	int *s = (int *)(h1 + o1);
	int *d = (int *)(h2 + o2);

	while (c--)
		*d++ = *s++;
}

static __inline void
bus_space_copy_8(void *v, bus_space_handle_t h1, bus_size_t o1,
	bus_space_handle_t h2, bus_size_t o2, bus_size_t c)
{
	int64_t *s = (int64_t *)(h1 + o1);
	int64_t *d = (int64_t *)(h2 + o2);

	while (c--)
		*d++ = *s++;
}

int	generic_space_map(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
void	generic_space_unmap(bus_space_tag_t, bus_space_handle_t, bus_size_t);
int	generic_space_region(bus_space_tag_t, bus_space_handle_t, bus_size_t,
	    bus_size_t, bus_space_handle_t *);
void	*generic_space_vaddr(bus_space_tag_t, bus_space_handle_t);
uint8_t generic_space_read_1(bus_space_tag_t, bus_space_handle_t, bus_size_t);
uint16_t generic_space_read_2(bus_space_tag_t, bus_space_handle_t, bus_size_t);
uint32_t generic_space_read_4(bus_space_tag_t, bus_space_handle_t, bus_size_t);
uint64_t generic_space_read_8(bus_space_tag_t, bus_space_handle_t, bus_size_t);
void	generic_space_read_raw_2(bus_space_tag_t, bus_space_handle_t,
	    bus_addr_t, uint8_t *, bus_size_t);
void	generic_space_write_1(bus_space_tag_t, bus_space_handle_t, bus_size_t,
	    uint8_t);
void	generic_space_write_2(bus_space_tag_t, bus_space_handle_t, bus_size_t,
	    uint16_t);
void	generic_space_write_4(bus_space_tag_t, bus_space_handle_t, bus_size_t,
	    uint32_t);
void	generic_space_write_8(bus_space_tag_t, bus_space_handle_t, bus_size_t,
	    uint64_t);
void	generic_space_write_raw_2(bus_space_tag_t, bus_space_handle_t,
	    bus_addr_t, const uint8_t *, bus_size_t);
void	generic_space_read_raw_4(bus_space_tag_t, bus_space_handle_t,
	    bus_addr_t, uint8_t *, bus_size_t);
void	generic_space_write_raw_4(bus_space_tag_t, bus_space_handle_t,
	    bus_addr_t, const uint8_t *, bus_size_t);
void	generic_space_read_raw_8(bus_space_tag_t, bus_space_handle_t,
	    bus_addr_t, uint8_t *, bus_size_t);
void	generic_space_write_raw_8(bus_space_tag_t, bus_space_handle_t,
	    bus_addr_t, const uint8_t *, bus_size_t);
paddr_t	generic_space_mmap(bus_space_tag_t, bus_addr_t, off_t, int, int);

/*----------------------------------------------------------------------------*/
/*
 * Bus read/write barrier methods.
 *
 *	void bus_space_barrier(bus_space_tag_t tag,
 *	    bus_space_handle_t bsh, bus_size_t offset,
 *	    bus_size_t len, int flags);
 *
 */
static inline void
bus_space_barrier(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset,
    bus_size_t length, int flags)
{
	__asm__ volatile ("sync" ::: "memory");
}
#define BUS_SPACE_BARRIER_READ  0x01		/* force read barrier */
#define BUS_SPACE_BARRIER_WRITE 0x02		/* force write barrier */

#define	BUS_DMA_WAITOK		0x0000
#define	BUS_DMA_NOWAIT		0x0001
#define	BUS_DMA_ALLOCNOW	0x0002
#define	BUS_DMA_COHERENT	0x0008
#define	BUS_DMA_BUS1		0x0010	/* placeholders for bus functions... */
#define	BUS_DMA_BUS2		0x0020
#define	BUS_DMA_BUS3		0x0040
#define	BUS_DMA_BUS4		0x0080
#define	BUS_DMA_READ		0x0100	/* mapping is device -> memory only */
#define	BUS_DMA_WRITE		0x0200	/* mapping is memory -> device only */
#define	BUS_DMA_STREAMING	0x0400	/* hint: sequential, unidirectional */
#define	BUS_DMA_ZERO		0x0800	/* zero memory in dmamem_alloc */
#define	BUS_DMA_NOCACHE		0x1000
#define	BUS_DMA_64BIT		0x2000	/* device handles 64bit dva */

/* Forwards needed by prototypes below. */
struct mbuf;
struct proc;
struct uio;

#define	BUS_DMASYNC_POSTREAD	0x0001
#define BUS_DMASYNC_POSTWRITE	0x0002
#define BUS_DMASYNC_PREREAD	0x0004
#define BUS_DMASYNC_PREWRITE	0x0008

typedef struct machine_bus_dma_tag	*bus_dma_tag_t;
typedef struct machine_bus_dmamap	*bus_dmamap_t;

/*
 *	bus_dma_segment_t
 *
 *	Describes a single contiguous DMA transaction.  Values
 *	are suitable for programming into DMA registers.
 */
struct machine_bus_dma_segment {
	bus_addr_t	ds_addr;	/* DMA address */
	bus_size_t	ds_len;		/* length of transfer */

	paddr_t		_ds_paddr;	/* CPU address */
	vaddr_t		_ds_vaddr;	/* CPU address */
};
typedef struct machine_bus_dma_segment	bus_dma_segment_t;

/*
 *	bus_dma_tag_t
 *
 *	A machine-dependent opaque type describing the implementation of
 *	DMA for a given bus.
 */

struct machine_bus_dma_tag {
	void	*_cookie;		/* cookie used in the guts */

	/*
	 * DMA mapping methods.
	 */
	int	(*_dmamap_create)(bus_dma_tag_t , bus_size_t, int,
		    bus_size_t, bus_size_t, int, bus_dmamap_t *);
	void	(*_dmamap_destroy)(bus_dma_tag_t , bus_dmamap_t);
	int	(*_dmamap_load)(bus_dma_tag_t , bus_dmamap_t, void *,
		    bus_size_t, struct proc *, int);
	int	(*_dmamap_load_mbuf)(bus_dma_tag_t , bus_dmamap_t,
		    struct mbuf *, int);
	int	(*_dmamap_load_uio)(bus_dma_tag_t , bus_dmamap_t,
		    struct uio *, int);
	int	(*_dmamap_load_raw)(bus_dma_tag_t , bus_dmamap_t,
		    bus_dma_segment_t *, int, bus_size_t, int);
	int	(*_dmamap_load_buffer)(bus_dma_tag_t, bus_dmamap_t, void *,
		    bus_size_t, struct proc *, int, paddr_t *, int *, int);
	void	(*_dmamap_unload)(bus_dma_tag_t , bus_dmamap_t);
	void	(*_dmamap_sync)(bus_dma_tag_t , bus_dmamap_t,
		    bus_addr_t, bus_size_t, int);

	/*
	 * DMA memory utility functions.
	 */
	int	(*_dmamem_alloc)(bus_dma_tag_t, bus_size_t, bus_size_t,
		    bus_size_t, bus_dma_segment_t *, int, int *, int);
	void	(*_dmamem_free)(bus_dma_tag_t, bus_dma_segment_t *, int);
	int	(*_dmamem_map)(bus_dma_tag_t, bus_dma_segment_t *,
		    int, size_t, caddr_t *, int);
	void	(*_dmamem_unmap)(bus_dma_tag_t, caddr_t, size_t);
	paddr_t	(*_dmamem_mmap)(bus_dma_tag_t, bus_dma_segment_t *,
		    int, off_t, int, int);

	/*
	 * internal memory address translation information.
	 */
	bus_addr_t (*_pa_to_device)(paddr_t);
	paddr_t	(*_device_to_pa)(bus_addr_t);
	bus_addr_t _dma_mask;
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
#define	bus_dmamap_sync(t, p, a, l, o)				\
	(void)((t)->_dmamap_sync ?				\
	    (*(t)->_dmamap_sync)((t), (p), (a), (l), (o)) : (void)0)

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

int	_dmamap_create(bus_dma_tag_t, bus_size_t, int,
	    bus_size_t, bus_size_t, int, bus_dmamap_t *);
void	_dmamap_destroy(bus_dma_tag_t, bus_dmamap_t);
int	_dmamap_load(bus_dma_tag_t, bus_dmamap_t, void *,
	    bus_size_t, struct proc *, int);
int	_dmamap_load_mbuf(bus_dma_tag_t, bus_dmamap_t, struct mbuf *, int);
int	_dmamap_load_uio(bus_dma_tag_t, bus_dmamap_t, struct uio *, int);
int	_dmamap_load_raw(bus_dma_tag_t, bus_dmamap_t,
	    bus_dma_segment_t *, int, bus_size_t, int);
int	_dmamap_load_buffer(bus_dma_tag_t, bus_dmamap_t, void *,
	    bus_size_t, struct proc *, int, paddr_t *, int *, int);
void	_dmamap_unload(bus_dma_tag_t, bus_dmamap_t);
void	_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, bus_addr_t,
	    bus_size_t, int);

int	_dmamem_alloc(bus_dma_tag_t, bus_size_t, bus_size_t,
	    bus_size_t, bus_dma_segment_t *, int, int *, int);
void	_dmamem_free(bus_dma_tag_t, bus_dma_segment_t *, int);
int	_dmamem_map(bus_dma_tag_t, bus_dma_segment_t *,
	    int, size_t, caddr_t *, int);
void	_dmamem_unmap(bus_dma_tag_t, caddr_t, size_t);
paddr_t	_dmamem_mmap(bus_dma_tag_t, bus_dma_segment_t *, int, off_t, int, int);
int	_dmamem_alloc_range(bus_dma_tag_t, bus_size_t, bus_size_t, bus_size_t,
	    bus_dma_segment_t *, int, int *, int, paddr_t, paddr_t);

/*
 *	bus_dmamap_t
 *
 *	Describes a DMA mapping.
 */
struct machine_bus_dmamap {
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
