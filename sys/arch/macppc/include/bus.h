/*	$OpenBSD: bus.h,v 1.27 2024/05/22 05:51:49 jsg Exp $	*/

/*
 * Copyright (c) 1997 Per Fogelstrom.  All rights reserved.
 * Copyright (c) 1996 Niklas Hallqvist.  All rights reserved.
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

#include <machine/pio.h>

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
typedef u_long bus_addr_t;
typedef u_long bus_size_t;
typedef u_long bus_space_handle_t;
typedef struct ppc_bus_space *bus_space_tag_t;

struct ppc_bus_space {
	u_int32_t	bus_base;
	u_int32_t	bus_size;
	u_int8_t	bus_io;		/* IO or memory */
};
#define POWERPC_BUS_TAG_BASE(x)  ((x)->bus_base)

/*
 * Access methods for bus resources
 */
int	bus_space_map(bus_space_tag_t t, bus_addr_t addr,
	    bus_size_t size, int flags, bus_space_handle_t *bshp);
void	bus_space_unmap(bus_space_tag_t t, bus_space_handle_t bsh,
	    bus_size_t size);
int	bus_space_subregion(bus_space_tag_t t, bus_space_handle_t bsh,
	    bus_size_t offset, bus_size_t size, bus_space_handle_t *nbshp);
int	bus_space_alloc(bus_space_tag_t tag, bus_addr_t rstart,
	    bus_addr_t rend, bus_size_t size, bus_size_t alignment,
	    bus_size_t boundary, int flags, bus_addr_t *addrp,
	    bus_space_handle_t *handlep);
void	bus_space_free(bus_space_tag_t tag, bus_space_handle_t handle,
	    bus_size_t size);
paddr_t	bus_space_mmap(bus_space_tag_t, bus_addr_t, off_t, int, int);

#define	BUS_SPACE_MAP_CACHEABLE		0x01
#define	BUS_SPACE_MAP_LINEAR		0x02
#define	BUS_SPACE_MAP_PREFETCHABLE	0x04

/*
 *	void *bus_space_vaddr(bus_space_tag_t, bus_space_handle_t);
 *
 * Get the kernel virtual address for the mapped bus space.
 * Only allowed for regions mapped with BUS_SPACE_MAP_LINEAR.
 */
#define bus_space_vaddr(t, h) ((void *)(h))

#define bus_space_read(n,m)						      \
static __inline CAT3(u_int,m,_t)					      \
CAT(bus_space_read_,n)(bus_space_tag_t bst, bus_space_handle_t bsh,	      \
     bus_addr_t ba)							      \
{									      \
	return CAT3(in,m,rb)((volatile CAT3(u_int,m,_t) *)(bsh + (ba)));      \
}

bus_space_read(1,8)
bus_space_read(2,16)
bus_space_read(4,32)

#define	bus_space_read_8	!!! bus_space_read_8 unimplemented !!!

#define bus_space_write(n,m)						      \
static __inline void							      \
CAT(bus_space_write_,n)(bus_space_tag_t bst, bus_space_handle_t bsh,	      \
     bus_addr_t ba, CAT3(u_int,m,_t) x)					      \
{									      \
	CAT3(out,m,rb)((volatile CAT3(u_int,m,_t) *)(bsh + (ba)), x);	      \
}

bus_space_write(1,8)
bus_space_write(2,16)
bus_space_write(4,32)

#define	bus_space_write_8	!!! bus_space_write_8 unimplemented !!!

#define bus_space_read_raw(n,m)						      \
static __inline CAT3(u_int,m,_t)					      \
CAT(bus_space_read_raw_,n)(bus_space_tag_t bst, bus_space_handle_t bsh,	      \
    bus_addr_t ba)							      \
{									      \
	return CAT(in,m)((volatile CAT3(u_int,m,_t) *)(bsh + (ba)));	      \
}

bus_space_read_raw(1,8)
bus_space_read_raw(2,16)
bus_space_read_raw(4,32)

#define	bus_space_read_raw_8	!!! bus_space_read_raw_8 unimplemented !!!

#define bus_space_write_raw(n,m)					      \
static __inline void							      \
CAT(bus_space_write_raw_,n)(bus_space_tag_t bst, bus_space_handle_t bsh,      \
    bus_addr_t ba, CAT3(u_int,m,_t) x)					      \
{									      \
	CAT(out,m)((volatile CAT3(u_int,m,_t) *)(bsh + (ba)), x);	      \
}

bus_space_write_raw(1,8)
bus_space_write_raw(2,16)
bus_space_write_raw(4,32)

#define	bus_space_write_raw_8	!!! bus_space_write_raw_8 unimplemented !!!

#define bus_space_read_multi(n, m)					      \
static __inline void						       	      \
CAT(bus_space_read_multi_,n)(bus_space_tag_t bst, bus_space_handle_t bsh,     \
    bus_size_t ba, CAT3(u_int,m,_t) *buf, bus_size_t cnt)		      \
{									      \
	while (cnt--)							      \
		*buf++ = CAT(bus_space_read_,n)(bst, bsh, ba);		      \
}

bus_space_read_multi(1,8)
bus_space_read_multi(2,16)
bus_space_read_multi(4,32)

#define	bus_space_read_multi_8	!!! bus_space_read_multi_8 not implemented !!!


#define	bus_space_write_multi_8	!!! bus_space_write_multi_8 not implemented !!!

#define bus_space_write_multi(n, m)					      \
static __inline void								      \
CAT(bus_space_write_multi_,n)(bus_space_tag_t bst, bus_space_handle_t bsh,    \
    bus_size_t ba, const CAT3(u_int,m,_t) *buf, bus_size_t cnt)		      \
{									      \
	while (cnt--)							      \
		CAT(bus_space_write_,n)(bst, bsh, ba, *buf++);		      \
}

bus_space_write_multi(1,8)
bus_space_write_multi(2,16)
bus_space_write_multi(4,32)

#define	bus_space_write_multi_8	!!! bus_space_write_multi_8 not implemented !!!

/*
 *	void bus_space_read_region_N(bus_space_tag_t tag,
 *	    bus_space_handle_t bsh, bus_size_t offset,
 *	    u_intN_t *addr, size_t count);
 *
 * Read `count' 1, 2, 4, or 8 byte quantities from bus space
 * described by tag/handle and starting at `offset' and copy into
 * buffer provided.
 */
#define __BA(t, h, o) ((void *)((h) + (o)))

static __inline void
bus_space_read_region_1(bus_space_tag_t tag, bus_space_handle_t bsh,
	bus_size_t offset, u_int8_t *addr, size_t count)
{
	volatile u_int8_t *s = __BA(tag, bsh, offset);

	while (count--)
		*addr++ = *s++;
	__asm volatile("eieio; sync");
}

static __inline void
bus_space_read_region_2(bus_space_tag_t tag, bus_space_handle_t bsh,
	bus_size_t offset, u_int16_t *addr, size_t count)
{
	volatile u_int16_t *s = __BA(tag, bsh, offset);

	while (count--)
		__asm volatile("lhbrx %0, 0, %1" :
			"=r"(*addr++) : "r"(s++));
	__asm volatile("eieio; sync");
}

static __inline void
bus_space_read_region_4(bus_space_tag_t tag, bus_space_handle_t bsh,
	bus_size_t offset, u_int32_t *addr, size_t count)
{
	volatile u_int32_t *s = __BA(tag, bsh, offset);

	while (count--)
		__asm volatile("lwbrx %0, 0, %1" :
			"=r"(*addr++) : "r"(s++));
	__asm volatile("eieio; sync");
}

#if 0	/* Cause a link error for bus_space_read_region_8 */
#define	bus_space_read_region_8		!!! unimplemented !!!
#endif


/*
 *	void bus_space_write_region_N(bus_space_tag_t tag,
 *	    bus_space_handle_t bsh, bus_size_t offset,
 *	    const u_intN_t *addr, size_t count);
 *
 * Write `count' 1, 2, 4, or 8 byte quantities from the buffer provided
 * to bus space described by tag/handle starting at `offset'.
 */

static __inline void
bus_space_write_region_1(bus_space_tag_t tag, bus_space_handle_t bsh,
	bus_size_t offset, const u_int8_t *addr, size_t count)
{
	volatile u_int8_t *d = __BA(tag, bsh, offset);

	while (count--)
		*d++ = *addr++;
	__asm volatile("eieio; sync");
}

static __inline void
bus_space_write_region_2(bus_space_tag_t tag, bus_space_handle_t bsh,
	bus_size_t offset, const u_int16_t *addr, size_t count)
{
	volatile u_int16_t *d = __BA(tag, bsh, offset);

	while (count--)
		__asm volatile("sthbrx %0, 0, %1" ::
			"r"(*addr++), "r"(d++));
	__asm volatile("eieio; sync");
}

static __inline void
bus_space_write_region_4(bus_space_tag_t tag, bus_space_handle_t bsh,
	bus_size_t offset, const u_int32_t *addr, size_t count)
{
	volatile u_int32_t *d = __BA(tag, bsh, offset);

	while (count--)
		__asm volatile("stwbrx %0, 0, %1" ::
			"r"(*addr++), "r"(d++));
	__asm volatile("eieio; sync");
}

#if 0
#define	bus_space_write_region_8 !!! bus_space_write_region_8 unimplemented !!!
#endif

/*
 *	void bus_space_read_raw_region_N(bus_space_tag_t tag,
 *	    bus_space_handle_t bsh, bus_size_t offset,
 *	    u_intN_t *addr, size_t count);
 *
 * Read `count' bytes from bus space described by tag/handle and starting
 * at `offset' and copy into buffer provided w/o bus-host byte swapping.
 */

static __inline void
bus_space_read_raw_region_2(bus_space_tag_t tag, bus_space_handle_t bsh,
	bus_size_t offset, u_int8_t *addr, size_t count)
{
	volatile u_int16_t *s = __BA(tag, bsh, offset);
	u_int16_t *laddr = (void *)addr;

	count = count >> 1;

	while (count--)
		*laddr++ = *s++;
	__asm volatile("eieio; sync");
}

static __inline void
bus_space_read_raw_region_4(bus_space_tag_t tag, bus_space_handle_t bsh,
	bus_size_t offset, u_int8_t *addr, size_t count)
{
	volatile u_int32_t *s = __BA(tag, bsh, offset);
	u_int32_t *laddr = (void *)addr;

	count = count >> 2;

	while (count--)
		*laddr++ = *s++;
	__asm volatile("eieio; sync");
}

#if 0	/* Cause a link error for bus_space_read_raw_region_8 */
#define	bus_space_read_raw_region_8	\
    !!! bus_space_read_raw_region_8		unimplemented !!!
#endif


/*
 *	void bus_space_write_raw_region_N(bus_space_tag_t tag,
 *	    bus_space_handle_t bsh, bus_size_t offset,
 *	    const u_intN_t *addr, size_t count);
 *
 * Write `count' bytes from the buffer provided to bus space described
 * by tag/handle starting at `offset' w/o host-bus byte swapping.
 */

static __inline void
bus_space_write_raw_region_2(bus_space_tag_t tag, bus_space_handle_t bsh,
	bus_size_t offset, const u_int8_t *addr, size_t count)
{
	volatile u_int16_t *d = __BA(tag, bsh, offset);
	const u_int16_t *laddr = (void *)addr;

	count = count >> 1;

	while (count--)
		*d++ = *laddr++;
	__asm volatile("eieio; sync");
}

static __inline void
bus_space_write_raw_region_4(bus_space_tag_t tag, bus_space_handle_t bsh,
	bus_size_t offset, const u_int8_t *addr, size_t count)
{
	volatile u_int32_t *d = __BA(tag, bsh, offset);
	const u_int32_t *laddr = (void *)addr;

	count = count >> 2;

	while (count--)
		*d++ = *laddr++;
	__asm volatile("eieio; sync");
}

#if 0
#define	bus_space_write_raw_region_8 \
    !!! bus_space_write_raw_region_8 unimplemented !!!
#endif

/*
 *	void bus_space_set_multi_N(bus_space_tag_t tag,
 *	    bus_space_handle_t bsh, bus_size_t offset, u_intN_t val,
 *	    size_t count);
 *
 * Write the 1, 2, 4, or 8 byte value `val' to bus space described
 * by tag/handle/offset `count' times.
 */
static __inline void bus_space_set_multi_1(bus_space_tag_t,
	bus_space_handle_t, bus_size_t, u_int8_t, size_t);
static __inline void bus_space_set_multi_2(bus_space_tag_t,
	bus_space_handle_t, bus_size_t, u_int16_t, size_t);
static __inline void bus_space_set_multi_4(bus_space_tag_t,
	bus_space_handle_t, bus_size_t, u_int32_t, size_t);

static __inline void
bus_space_set_multi_1(bus_space_tag_t tag, bus_space_handle_t bsh,
    bus_size_t offset, u_int8_t val, size_t count)
{
	volatile u_int8_t *d = __BA(tag, bsh, offset);

	while (count--)
		*d = val;
	__asm__ volatile("eieio; sync");
}

static __inline void
bus_space_set_multi_2(bus_space_tag_t tag, bus_space_handle_t bsh,
    bus_size_t offset, u_int16_t val, size_t count)
{
	volatile u_int16_t *d = __BA(tag, bsh, offset);

	while (count--)
		__asm__ volatile("sthbrx %0, 0, %1" ::
			"r"(val), "r"(d));
	__asm__ volatile("eieio; sync");
}

static __inline void
bus_space_set_multi_4(bus_space_tag_t tag, bus_space_handle_t bsh,
    bus_size_t offset, u_int32_t val, size_t count)
{
	volatile u_int32_t *d = __BA(tag, bsh, offset);

	while (count--)
		__asm__ volatile("stwbrx %0, 0, %1" ::
			"r"(val), "r"(d));
	__asm__ volatile("eieio; sync");
}

#define	bus_space_set_multi_8 !!! bus_space_set_multi_8 unimplemented !!!

/* These are OpenBSD extensions to the general NetBSD bus interface.  */
void
bus_space_read_raw_multi_2(bus_space_tag_t bst, bus_space_handle_t bsh,
	bus_addr_t ba, u_int8_t *dst, bus_size_t size);
void
bus_space_read_raw_multi_4(bus_space_tag_t bst, bus_space_handle_t bsh,
	bus_addr_t ba, u_int8_t *dst, bus_size_t size);
#define	bus_space_read_raw_multi_8 \
    !!! bus_space_read_raw_multi_8 not implemented !!!

void
bus_space_write_raw_multi_2(bus_space_tag_t bst, bus_space_handle_t bsh,
	bus_addr_t ba, const u_int8_t *src, bus_size_t size);
void
bus_space_write_raw_multi_4(bus_space_tag_t bst, bus_space_handle_t bsh,
	bus_addr_t ba, const u_int8_t *src, bus_size_t size);
#define	bus_space_write_raw_multi_8 \
    !!! bus_space_write_raw_multi_8 not implemented !!!

void
bus_space_set_region_1(bus_space_tag_t bst, bus_space_handle_t h, bus_size_t o,
    u_int8_t val, bus_size_t c);
void
bus_space_set_region_2(bus_space_tag_t bst, bus_space_handle_t h, bus_size_t o,
    u_int16_t val, bus_size_t c);
void
bus_space_set_region_4(bus_space_tag_t bst, bus_space_handle_t h, bus_size_t o,
    u_int32_t val, bus_size_t c);
#define	bus_space_set_region_8 \
    !!! bus_space_set_region_8 not implemented !!!

void
bus_space_copy_1(void *v, bus_space_handle_t h1, bus_space_handle_t h2,
    bus_size_t o1, bus_size_t o2, bus_size_t c);
void
bus_space_copy_2(void *v, bus_space_handle_t h1, bus_space_handle_t h2,
    bus_size_t o1, bus_size_t o2, bus_size_t c);
void
bus_space_copy_4(void *v, bus_space_handle_t h1, bus_space_handle_t h2,
    bus_size_t o1, bus_size_t o2, bus_size_t c);
#define	bus_space_copy_8 \
    !!! bus_space_copy_8 not implemented !!!

/*
 * Bus read/write barrier methods.
 *
 *	void bus_space_barrier(bus_space_tag_t tag,
 *	    bus_space_handle_t bsh, bus_size_t offset,
 *	    bus_size_t len, int flags);
 * 
 * Note: powerpc does not currently implement barriers, but we must
 * provide the flags to MI code.
 * the processor does have eieio which is effectively the barrier
 * operator, however due to how memory is mapped this should? not
 * be required.
 */
#define bus_space_barrier(t, h, o, l, f)	\
	((void)((void)(t), (void)(h), (void)(o), (void)(l), (void)(f)))  
#define BUS_SPACE_BARRIER_READ  0x01		/* force read barrier */ 
#define BUS_SPACE_BARRIER_WRITE 0x02		/* force write barrier */

#define	BUS_DMA_WAITOK		0x0000	/* safe to sleep (pseudo-flag) */
#define	BUS_DMA_NOWAIT		0x0001	/* not safe to sleep */
#define	BUS_DMA_ALLOCNOW	0x0002	/* perform resource allocation now */
#define	BUS_DMA_COHERENT	0x0008	/* hint: map memory DMA coherent */
#define	BUS_DMA_BUS1		0x0010	/* placeholders for bus functions... */
#define	BUS_DMA_BUS2		0x0020
#define	BUS_DMA_BUS3		0x0040
#define	BUS_DMA_BUS4		0x0080
#define	BUS_DMA_READ		0x0100	/* mapping is device -> memory only */
#define	BUS_DMA_WRITE		0x0200	/* mapping is memory -> device only */
#define	BUS_DMA_STREAMING	0x0400	/* hint: sequential, unidirectional */
#define	BUS_DMA_ZERO		0x0800	/* zero memory in dmamem_alloc */
#define	BUS_DMA_NOCACHE		0x1000	/* map memory uncached */
#define	BUS_DMA_64BIT		0x2000	/* device handles 64bit dva */


/* Forwards needed by prototypes below. */
struct mbuf;
struct proc;
struct uio;

#define BUS_DMASYNC_POSTREAD	0x01
#define BUS_DMASYNC_POSTWRITE	0x02
#define BUS_DMASYNC_PREREAD	0x04
#define BUS_DMASYNC_PREWRITE	0x08

typedef struct powerpc_bus_dma_tag	*bus_dma_tag_t;
typedef struct powerpc_bus_dmamap	*bus_dmamap_t;

/*
 *	bus_dma_segment_t
 *
 *	Describes a single contiguous DMA transaction.  Values
 *	are suitable for programming into DMA registers.
 */
struct powerpc_bus_dma_segment {
	bus_addr_t	ds_addr;	/* DMA address */
	bus_size_t	ds_len;		/* length of transfer */
};
typedef struct powerpc_bus_dma_segment	bus_dma_segment_t;

/*
 *	bus_dma_tag_t
 *
 *	A machine-dependent opaque type describing the implementation of
 *	DMA for a given bus.
 */

struct powerpc_bus_dma_tag {
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
	void	(*_dmamap_unload)(bus_dma_tag_t , bus_dmamap_t);
	void	(*_dmamap_sync)(bus_dma_tag_t , bus_dmamap_t,
		    bus_addr_t, bus_size_t, int);

	/*
	 * DMA memory utility functions.
	 */
	int	(*_dmamem_alloc)(bus_dma_tag_t, bus_size_t, bus_size_t,
		    bus_size_t, bus_dma_segment_t *, int, int *, int);
	int	(*_dmamem_alloc_range)(bus_dma_tag_t, bus_size_t, bus_size_t,
		    bus_size_t, bus_dma_segment_t *, int, int *, int,
		    bus_addr_t, bus_addr_t);
	void	(*_dmamem_free)(bus_dma_tag_t, bus_dma_segment_t *, int);
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
#define	bus_dmamap_sync(t, p, a, l, o)				\
	(void)((t)->_dmamap_sync ?				\
	    (*(t)->_dmamap_sync)((t), (p), (a), (l), (o)) : (void)0)

#define	bus_dmamem_alloc(t, s, a, b, sg, n, r, f)		\
	(*(t)->_dmamem_alloc)((t)->_cookie, (s), (a), (b), (sg), (n), (r), (f))
#define	bus_dmamem_alloc_range(t, s, a, b, sg, n, r, f, l, h)	\
	(*(t)->_dmamem_alloc_range)((t), (s), (a), (b), (sg),	\
		(n), (r), (f), (l), (h))
#define	bus_dmamem_free(t, sg, n)				\
	(*(t)->_dmamem_free)((t)->_cookie, (sg), (n))
#define	bus_dmamem_map(t, sg, n, s, k, f)			\
	(*(t)->_dmamem_map)((t)->_cookie, (sg), (n), (s), (k), (f))
#define	bus_dmamem_unmap(t, k, s)				\
	(*(t)->_dmamem_unmap)((t)->_cookie, (k), (s))
#define	bus_dmamem_mmap(t, sg, n, o, p, f)			\
	(*(t)->_dmamem_mmap)((t)->_cookie, (sg), (n), (o), (p), (f))

int	_dmamap_create(bus_dma_tag_t, bus_size_t, int,
	    bus_size_t, bus_size_t, int, bus_dmamap_t *);
void	_dmamap_destroy(bus_dma_tag_t, bus_dmamap_t);
int	_dmamap_load(bus_dma_tag_t, bus_dmamap_t, void *,
	    bus_size_t, struct proc *, int);
int	_dmamap_load_mbuf(bus_dma_tag_t, bus_dmamap_t, struct mbuf *, int);
int	_dmamap_load_uio(bus_dma_tag_t, bus_dmamap_t, struct uio *, int);
int	_dmamap_load_raw(bus_dma_tag_t, bus_dmamap_t,
	    bus_dma_segment_t *, int, bus_size_t, int);
void	_dmamap_unload(bus_dma_tag_t, bus_dmamap_t);
void	_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, bus_addr_t, bus_size_t,
	    int);

int	_dmamem_alloc(bus_dma_tag_t, bus_size_t, bus_size_t,
	    bus_size_t, bus_dma_segment_t *, int, int *, int);
int	_dmamem_alloc_range( bus_dma_tag_t, bus_size_t, bus_size_t,
	    bus_size_t, bus_dma_segment_t *, int, int *, int,
	    bus_addr_t, bus_addr_t);
void	_dmamem_free(bus_dma_tag_t, bus_dma_segment_t *, int);
int	_dmamem_map(bus_dma_tag_t, bus_dma_segment_t *,
	    int, size_t, caddr_t *, int);
void	_dmamem_unmap(bus_dma_tag_t, caddr_t, size_t);
paddr_t	_dmamem_mmap(bus_dma_tag_t, bus_dma_segment_t *, int, off_t, int, int);

/*
 *	bus_dmamap_t
 *
 *	Describes a DMA mapping.
 */
struct powerpc_bus_dmamap {
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
