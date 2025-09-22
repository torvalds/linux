/* $OpenBSD: tc_bus_mem.c,v 1.20 2025/06/29 15:55:22 miod Exp $ */
/* $NetBSD: tc_bus_mem.c,v 1.25 2001/09/04 05:31:28 thorpej Exp $ */

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

/*
 * Common TurboChannel Chipset "bus memory" functions.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
#include <sys/device.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <dev/tc/tcvar.h>

#define	__C(A,B)	__CONCAT(A,B)

/* mapping/unmapping */
int		tc_mem_map(void *, bus_addr_t, bus_size_t, int,
		    bus_space_handle_t *);
void		tc_mem_unmap(void *, bus_space_handle_t, bus_size_t);
int		tc_mem_subregion(void *, bus_space_handle_t, bus_size_t,
		    bus_size_t, bus_space_handle_t *);

/* allocation/deallocation */
int		tc_mem_alloc(void *, bus_addr_t, bus_addr_t, bus_size_t,
		    bus_size_t, bus_addr_t, int, bus_addr_t *,
		    bus_space_handle_t *);
void		tc_mem_free(void *, bus_space_handle_t, bus_size_t);

/* get kernel virtual address */
void *		tc_mem_vaddr(void *, bus_space_handle_t);

/* barrier */
inline void	tc_mem_barrier(void *, bus_space_handle_t,
		    bus_size_t, bus_size_t, int);

/* read (single) */
inline u_int8_t	tc_mem_read_1(void *, bus_space_handle_t, bus_size_t);
inline u_int16_t tc_mem_read_2(void *, bus_space_handle_t, bus_size_t);
inline u_int32_t tc_mem_read_4(void *, bus_space_handle_t, bus_size_t);
inline u_int64_t tc_mem_read_8(void *, bus_space_handle_t, bus_size_t);

/* read multiple */
void		tc_mem_read_multi_1(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t *, bus_size_t);
void		tc_mem_read_multi_2(void *, bus_space_handle_t,
		    bus_size_t, u_int16_t *, bus_size_t);
void		tc_mem_read_multi_4(void *, bus_space_handle_t,
		    bus_size_t, u_int32_t *, bus_size_t);
void		tc_mem_read_multi_8(void *, bus_space_handle_t,
		    bus_size_t, u_int64_t *, bus_size_t);

/* read region */
void		tc_mem_read_region_1(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t *, bus_size_t);
void		tc_mem_read_region_2(void *, bus_space_handle_t,
		    bus_size_t, u_int16_t *, bus_size_t);
void		tc_mem_read_region_4(void *, bus_space_handle_t,
		    bus_size_t, u_int32_t *, bus_size_t);
void		tc_mem_read_region_8(void *, bus_space_handle_t,
		    bus_size_t, u_int64_t *, bus_size_t);

/* write (single) */
inline void	tc_mem_write_1(void *, bus_space_handle_t, bus_size_t,
		    u_int8_t);
inline void	tc_mem_write_2(void *, bus_space_handle_t, bus_size_t,
		    u_int16_t);
inline void	tc_mem_write_4(void *, bus_space_handle_t, bus_size_t,
		    u_int32_t);
inline void	tc_mem_write_8(void *, bus_space_handle_t, bus_size_t,
		    u_int64_t);

/* write multiple */
void		tc_mem_write_multi_1(void *, bus_space_handle_t,
		    bus_size_t, const u_int8_t *, bus_size_t);
void		tc_mem_write_multi_2(void *, bus_space_handle_t,
		    bus_size_t, const u_int16_t *, bus_size_t);
void		tc_mem_write_multi_4(void *, bus_space_handle_t,
		    bus_size_t, const u_int32_t *, bus_size_t);
void		tc_mem_write_multi_8(void *, bus_space_handle_t,
		    bus_size_t, const u_int64_t *, bus_size_t);

/* write region */
void		tc_mem_write_region_1(void *, bus_space_handle_t,
		    bus_size_t, const u_int8_t *, bus_size_t);
void		tc_mem_write_region_2(void *, bus_space_handle_t,
		    bus_size_t, const u_int16_t *, bus_size_t);
void		tc_mem_write_region_4(void *, bus_space_handle_t,
		    bus_size_t, const u_int32_t *, bus_size_t);
void		tc_mem_write_region_8(void *, bus_space_handle_t,
		    bus_size_t, const u_int64_t *, bus_size_t);

/* set multiple */
void		tc_mem_set_multi_1(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t, bus_size_t);
void		tc_mem_set_multi_2(void *, bus_space_handle_t,
		    bus_size_t, u_int16_t, bus_size_t);
void		tc_mem_set_multi_4(void *, bus_space_handle_t,
		    bus_size_t, u_int32_t, bus_size_t);
void		tc_mem_set_multi_8(void *, bus_space_handle_t,
		    bus_size_t, u_int64_t, bus_size_t);

/* set region */
void		tc_mem_set_region_1(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t, bus_size_t);
void		tc_mem_set_region_2(void *, bus_space_handle_t,
		    bus_size_t, u_int16_t, bus_size_t);
void		tc_mem_set_region_4(void *, bus_space_handle_t,
		    bus_size_t, u_int32_t, bus_size_t);
void		tc_mem_set_region_8(void *, bus_space_handle_t,
		    bus_size_t, u_int64_t, bus_size_t);

/* copy */
void		tc_mem_copy_1(void *, bus_space_handle_t,
		    bus_size_t, bus_space_handle_t, bus_size_t, bus_size_t);
void		tc_mem_copy_2(void *, bus_space_handle_t,
		    bus_size_t, bus_space_handle_t, bus_size_t, bus_size_t);
void		tc_mem_copy_4(void *, bus_space_handle_t,
		    bus_size_t, bus_space_handle_t, bus_size_t, bus_size_t);
void		tc_mem_copy_8(void *, bus_space_handle_t,
		    bus_size_t, bus_space_handle_t, bus_size_t, bus_size_t);

struct alpha_bus_space tc_mem_space = {
	/* cookie */
	NULL,

	/* mapping/unmapping */
	tc_mem_map,
	tc_mem_unmap,
	tc_mem_subregion,

	/* allocation/deallocation */
	tc_mem_alloc,
	tc_mem_free,

	/* get kernel virtual address */
	tc_mem_vaddr,

	/* barrier */
	tc_mem_barrier,

	/* read (single) */
	tc_mem_read_1,
	tc_mem_read_2,
	tc_mem_read_4,
	tc_mem_read_8,

	/* read multiple */
	tc_mem_read_multi_1,
	tc_mem_read_multi_2,
	tc_mem_read_multi_4,
	tc_mem_read_multi_8,

	/* read region */
	tc_mem_read_region_1,
	tc_mem_read_region_2,
	tc_mem_read_region_4,
	tc_mem_read_region_8,

	/* write (single) */
	tc_mem_write_1,
	tc_mem_write_2,
	tc_mem_write_4,
	tc_mem_write_8,

	/* write multiple */
	tc_mem_write_multi_1,
	tc_mem_write_multi_2,
	tc_mem_write_multi_4,
	tc_mem_write_multi_8,

	/* write region */
	tc_mem_write_region_1,
	tc_mem_write_region_2,
	tc_mem_write_region_4,
	tc_mem_write_region_8,

	/* set multiple */
	tc_mem_set_multi_1,
	tc_mem_set_multi_2,
	tc_mem_set_multi_4,
	tc_mem_set_multi_8,

	/* set region */
	tc_mem_set_region_1,
	tc_mem_set_region_2,
	tc_mem_set_region_4,
	tc_mem_set_region_8,

	/* copy */
	tc_mem_copy_1,
	tc_mem_copy_2,
	tc_mem_copy_4,
	tc_mem_copy_8,
};

bus_space_tag_t
tc_bus_mem_init(void *memv)
{
	bus_space_tag_t h = &tc_mem_space;

	h->abs_cookie = memv;
	return (h);
}

int
tc_mem_map(void *v, bus_addr_t memaddr, bus_size_t memsize, int flags,
    bus_space_handle_t *memhp)
{
	int cacheable = flags & BUS_SPACE_MAP_CACHEABLE;
	int linear = flags & BUS_SPACE_MAP_LINEAR;

	/* Requests for linear uncacheable space can't be satisfied. */
	if (linear && !cacheable)
		return (EOPNOTSUPP);

	if (memaddr & 0x7)
		panic("tc_mem_map needs 8 byte alignment");
	if (cacheable)
		*memhp = ALPHA_PHYS_TO_K0SEG(memaddr);
	else
		*memhp = ALPHA_PHYS_TO_K0SEG(TC_DENSE_TO_SPARSE(memaddr));
	return (0);
}

void
tc_mem_unmap(void *v, bus_space_handle_t memh, bus_size_t memsize)
{

	/* XXX XX XXX nothing to do. */
}

int
tc_mem_subregion(void *v, bus_space_handle_t memh, bus_size_t offset,
    bus_size_t size, bus_space_handle_t *nmemh)
{

	/* Disallow subregioning that would make the handle unaligned. */
	if ((offset & 0x7) != 0)
		return (1);

	if ((memh & TC_SPACE_SPARSE) != 0)
		*nmemh = memh + (offset << 1);
	else
		*nmemh = memh + offset;

	return (0);
}

int
tc_mem_alloc(void *v, bus_addr_t rstart, bus_addr_t rend, bus_size_t size,
    bus_size_t align, bus_size_t boundary, int flags, bus_addr_t *addrp,
    bus_space_handle_t *bshp)
{

	/* XXX XXX XXX XXX XXX XXX */
	panic("tc_mem_alloc unimplemented");
}

void
tc_mem_free(void *v, bus_space_handle_t bsh, bus_size_t size)
{

	/* XXX XXX XXX XXX XXX XXX */
	panic("tc_mem_free unimplemented");
}

void *
tc_mem_vaddr(void *v, bus_space_handle_t bsh)
{
#ifdef DIAGNOSTIC
	if ((bsh & TC_SPACE_SPARSE) != 0) {
		/*
		 * tc_mem_map() catches linear && !cacheable,
		 * so we shouldn't come here
		 */
		panic("tc_mem_vaddr");
	}
#endif
	return ((void *)bsh);
}

inline void
tc_mem_barrier(void *v, bus_space_handle_t h, bus_size_t o, bus_size_t l, int f)
{

	if ((f & BUS_SPACE_BARRIER_READ) != 0)
		alpha_mb();
	else if ((f & BUS_SPACE_BARRIER_WRITE) != 0)
		alpha_wmb();
}

inline u_int8_t
tc_mem_read_1(void *v, bus_space_handle_t memh, bus_size_t off)
{
	volatile u_int8_t *p;

	alpha_mb();		/* XXX XXX XXX */

	if ((memh & TC_SPACE_SPARSE) != 0)
		panic("tc_mem_read_1 not implemented for sparse space");

	p = (u_int8_t *)(memh + off);
	return (*p);
}

inline u_int16_t
tc_mem_read_2(void *v, bus_space_handle_t memh, bus_size_t off)
{
	volatile u_int16_t *p;

	alpha_mb();		/* XXX XXX XXX */

	if ((memh & TC_SPACE_SPARSE) != 0)
		panic("tc_mem_read_2 not implemented for sparse space");

	p = (u_int16_t *)(memh + off);
	return (*p);
}

inline u_int32_t
tc_mem_read_4(void *v, bus_space_handle_t memh, bus_size_t off)
{
	volatile u_int32_t *p;

	alpha_mb();		/* XXX XXX XXX */

	if ((memh & TC_SPACE_SPARSE) != 0)
		/* Nothing special to do for 4-byte sparse space accesses */
		p = (u_int32_t *)(memh + (off << 1));
	else
		p = (u_int32_t *)(memh + off);
	return (*p);
}

inline u_int64_t
tc_mem_read_8(void *v, bus_space_handle_t memh, bus_size_t off)
{
	volatile u_int64_t *p;

	alpha_mb();		/* XXX XXX XXX */

	if ((memh & TC_SPACE_SPARSE) != 0)
		panic("tc_mem_read_8 not implemented for sparse space");

	p = (u_int64_t *)(memh + off);
	return (*p);
}

#define	tc_mem_read_multi_N(BYTES,TYPE)					\
void									\
__C(tc_mem_read_multi_,BYTES)(void *v, bus_space_handle_t h,		\
    bus_size_t o, TYPE *a, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		tc_mem_barrier(v, h, o, sizeof *a, BUS_SPACE_BARRIER_READ); \
		*a++ = __C(tc_mem_read_,BYTES)(v, h, o);		\
	}								\
}
tc_mem_read_multi_N(1,u_int8_t)
tc_mem_read_multi_N(2,u_int16_t)
tc_mem_read_multi_N(4,u_int32_t)
tc_mem_read_multi_N(8,u_int64_t)

#define	tc_mem_read_region_N(BYTES,TYPE)				\
void									\
__C(tc_mem_read_region_,BYTES)(void *v, bus_space_handle_t h,		\
    bus_size_t o, TYPE *a, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		*a++ = __C(tc_mem_read_,BYTES)(v, h, o);		\
		o += sizeof *a;						\
	}								\
}
tc_mem_read_region_N(1,u_int8_t)
tc_mem_read_region_N(2,u_int16_t)
tc_mem_read_region_N(4,u_int32_t)
tc_mem_read_region_N(8,u_int64_t)

inline void
tc_mem_write_1(void *v, bus_space_handle_t memh, bus_size_t off, u_int8_t val)
{

	if ((memh & TC_SPACE_SPARSE) != 0) {
		volatile u_int64_t *p, v;
		u_int64_t shift, msk;

		shift = off & 0x3;
		off &= 0x3;

		p = (u_int64_t *)(memh + (off << 1));

		msk = ~(0x1 << shift) & 0xf;
		v = (msk << 32) | (((u_int64_t)val) << (shift * 8));

		*p = val;
	} else {
		volatile u_int8_t *p;

		p = (u_int8_t *)(memh + off);
		*p = val;
	}
	alpha_mb();		/* XXX XXX XXX */
}

inline void
tc_mem_write_2(void *v, bus_space_handle_t memh, bus_size_t off, u_int16_t val)
{

	if ((memh & TC_SPACE_SPARSE) != 0) {
		volatile u_int64_t *p, v;
		u_int64_t shift, msk;

		shift = off & 0x2;
		off &= 0x3;

		p = (u_int64_t *)(memh + (off << 1));

		msk = ~(0x3 << shift) & 0xf;
		v = (msk << 32) | (((u_int64_t)val) << (shift * 8));

		*p = val;
	} else {
		volatile u_int16_t *p;

		p = (u_int16_t *)(memh + off);
		*p = val;
	}
	alpha_mb();		/* XXX XXX XXX */
}

inline void
tc_mem_write_4(void *v, bus_space_handle_t memh, bus_size_t off, u_int32_t val)
{
	volatile u_int32_t *p;

	if ((memh & TC_SPACE_SPARSE) != 0)
		/* Nothing special to do for 4-byte sparse space accesses */
		p = (u_int32_t *)(memh + (off << 1));
	else
		p = (u_int32_t *)(memh + off);
	*p = val;
	alpha_mb();		/* XXX XXX XXX */
}

inline void
tc_mem_write_8(void *v, bus_space_handle_t memh, bus_size_t off, u_int64_t val)
{
	volatile u_int64_t *p;

	if ((memh & TC_SPACE_SPARSE) != 0)
		panic("tc_mem_read_8 not implemented for sparse space");

	p = (u_int64_t *)(memh + off);
	*p = val;
	alpha_mb();		/* XXX XXX XXX */
}

#define	tc_mem_write_multi_N(BYTES,TYPE)				\
void									\
__C(tc_mem_write_multi_,BYTES)(void *v, bus_space_handle_t h,		\
    bus_size_t o, const TYPE *a, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		__C(tc_mem_write_,BYTES)(v, h, o, *a++);		\
		tc_mem_barrier(v, h, o, sizeof *a, BUS_SPACE_BARRIER_WRITE); \
	}								\
}
tc_mem_write_multi_N(1,u_int8_t)
tc_mem_write_multi_N(2,u_int16_t)
tc_mem_write_multi_N(4,u_int32_t)
tc_mem_write_multi_N(8,u_int64_t)

#define	tc_mem_write_region_N(BYTES,TYPE)				\
void									\
__C(tc_mem_write_region_,BYTES)(void *v, bus_space_handle_t h,		\
    bus_size_t o, const TYPE *a, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		__C(tc_mem_write_,BYTES)(v, h, o, *a++);		\
		o += sizeof *a;						\
	}								\
}
tc_mem_write_region_N(1,u_int8_t)
tc_mem_write_region_N(2,u_int16_t)
tc_mem_write_region_N(4,u_int32_t)
tc_mem_write_region_N(8,u_int64_t)

#define	tc_mem_set_multi_N(BYTES,TYPE)					\
void									\
__C(tc_mem_set_multi_,BYTES)(void *v, bus_space_handle_t h,		\
    bus_size_t o, TYPE val, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		__C(tc_mem_write_,BYTES)(v, h, o, val);			\
		tc_mem_barrier(v, h, o, sizeof val, BUS_SPACE_BARRIER_WRITE); \
	}								\
}
tc_mem_set_multi_N(1,u_int8_t)
tc_mem_set_multi_N(2,u_int16_t)
tc_mem_set_multi_N(4,u_int32_t)
tc_mem_set_multi_N(8,u_int64_t)

#define	tc_mem_set_region_N(BYTES,TYPE)					\
void									\
__C(tc_mem_set_region_,BYTES)(void *v, bus_space_handle_t h,		\
    bus_size_t o, TYPE val, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		__C(tc_mem_write_,BYTES)(v, h, o, val);			\
		o += sizeof val;					\
	}								\
}
tc_mem_set_region_N(1,u_int8_t)
tc_mem_set_region_N(2,u_int16_t)
tc_mem_set_region_N(4,u_int32_t)
tc_mem_set_region_N(8,u_int64_t)

#define	tc_mem_copy_N(BYTES)						\
void									\
__C(tc_mem_copy_,BYTES)(void *v, bus_space_handle_t h1, bus_size_t o1,	\
    bus_space_handle_t h2, bus_size_t o2, bus_size_t c)			\
{									\
	bus_size_t o;							\
									\
	if ((h1 & TC_SPACE_SPARSE) != 0 &&				\
	    (h2 & TC_SPACE_SPARSE) != 0) {				\
		bcopy((void *)(h1 + o1), (void *)(h2 + o2), c * BYTES); \
		return;							\
	}								\
									\
	if (h1 + o1 >= h2 + o2)						\
		/* src after dest: copy forward */			\
		for (o = 0; c > 0; c--, o += BYTES)			\
			__C(tc_mem_write_,BYTES)(v, h2, o2 + o,		\
			    __C(tc_mem_read_,BYTES)(v, h1, o1 + o));	\
	else								\
		/* dest after src: copy backwards */			\
		for (o = (c - 1) * BYTES; c > 0; c--, o -= BYTES)	\
			__C(tc_mem_write_,BYTES)(v, h2, o2 + o,		\
			    __C(tc_mem_read_,BYTES)(v, h1, o1 + o));	\
}
tc_mem_copy_N(1)
tc_mem_copy_N(2)
tc_mem_copy_N(4)
tc_mem_copy_N(8)
