/* $OpenBSD: pci_bwx_bus_io_chipdep.c,v 1.11 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: pcs_bus_io_common.c,v 1.14 1996/12/02 22:19:35 cgd Exp $ */

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
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
 * Common PCI Chipset "bus I/O" functions, for chipsets which have to
 * deal with only a single PCI interface chip in a machine.
 *
 * uses:
 *	CHIP		name of the 'chip' it's being compiled for.
 *	CHIP_IO_BASE	Sparse I/O space base to use.
 */

#include <sys/extent.h>

#include <machine/bwx.h>

#define	__C(A,B)	__CONCAT(A,B)
#define	__S(S)		__STRING(S)

#ifndef	CHIP_EXTENT_NAME
#define	CHIP_EXTENT_NAME(v)	__S(__C(CHIP,_bus_io))
#endif

#ifndef	CHIP_EXTENT_STORAGE
#define CHIP_EXTENT_STORAGE(v)	__C(CHIP,_io_ex_storage)
static long
    __C(CHIP,_io_ex_storage)[EXTENT_FIXED_STORAGE_SIZE(8) / sizeof(long)];
#endif

/* mapping/unmapping */
int		__C(CHIP,_io_map)(void *, bus_addr_t, bus_size_t, int,
		    bus_space_handle_t *);
void		__C(CHIP,_io_unmap)(void *, bus_space_handle_t,
		    bus_size_t);
int		__C(CHIP,_io_subregion)(void *, bus_space_handle_t,
		    bus_size_t, bus_size_t, bus_space_handle_t *);

/* allocation/deallocation */
int		__C(CHIP,_io_alloc)(void *, bus_addr_t, bus_addr_t,
		    bus_size_t, bus_size_t, bus_addr_t, int, bus_addr_t *,
		    bus_space_handle_t *);
void		__C(CHIP,_io_free)(void *, bus_space_handle_t,
		    bus_size_t);

/* get kernel virtual address */
void *		__C(CHIP,_io_vaddr)(void *, bus_space_handle_t);

/* barrier */
inline void	__C(CHIP,_io_barrier)(void *, bus_space_handle_t,
		    bus_size_t, bus_size_t, int);

/* read (single) */
inline u_int8_t	__C(CHIP,_io_read_1)(void *, bus_space_handle_t,
		    bus_size_t);
inline u_int16_t __C(CHIP,_io_read_2)(void *, bus_space_handle_t,
		    bus_size_t);
inline u_int32_t __C(CHIP,_io_read_4)(void *, bus_space_handle_t,
		    bus_size_t);
inline u_int64_t __C(CHIP,_io_read_8)(void *, bus_space_handle_t,
		    bus_size_t);

/* read multiple */
void		__C(CHIP,_io_read_multi_1)(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t *, bus_size_t);
void		__C(CHIP,_io_read_multi_2)(void *, bus_space_handle_t,
		    bus_size_t, u_int16_t *, bus_size_t);
void		__C(CHIP,_io_read_multi_4)(void *, bus_space_handle_t,
		    bus_size_t, u_int32_t *, bus_size_t);
void		__C(CHIP,_io_read_multi_8)(void *, bus_space_handle_t,
		    bus_size_t, u_int64_t *, bus_size_t);

/* read region */
void		__C(CHIP,_io_read_region_1)(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t *, bus_size_t);
void		__C(CHIP,_io_read_region_2)(void *, bus_space_handle_t,
		    bus_size_t, u_int16_t *, bus_size_t);
void		__C(CHIP,_io_read_region_4)(void *, bus_space_handle_t,
		    bus_size_t, u_int32_t *, bus_size_t);
void		__C(CHIP,_io_read_region_8)(void *, bus_space_handle_t,
		    bus_size_t, u_int64_t *, bus_size_t);

/* write (single) */
inline void	__C(CHIP,_io_write_1)(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t);
inline void	__C(CHIP,_io_write_2)(void *, bus_space_handle_t,
		    bus_size_t, u_int16_t);
inline void	__C(CHIP,_io_write_4)(void *, bus_space_handle_t,
		    bus_size_t, u_int32_t);
inline void	__C(CHIP,_io_write_8)(void *, bus_space_handle_t,
		    bus_size_t, u_int64_t);

/* write multiple */
void		__C(CHIP,_io_write_multi_1)(void *, bus_space_handle_t,
		    bus_size_t, const u_int8_t *, bus_size_t);
void		__C(CHIP,_io_write_multi_2)(void *, bus_space_handle_t,
		    bus_size_t, const u_int16_t *, bus_size_t);
void		__C(CHIP,_io_write_multi_4)(void *, bus_space_handle_t,
		    bus_size_t, const u_int32_t *, bus_size_t);
void		__C(CHIP,_io_write_multi_8)(void *, bus_space_handle_t,
		    bus_size_t, const u_int64_t *, bus_size_t);

/* write region */
void		__C(CHIP,_io_write_region_1)(void *, bus_space_handle_t,
		    bus_size_t, const u_int8_t *, bus_size_t);
void		__C(CHIP,_io_write_region_2)(void *, bus_space_handle_t,
		    bus_size_t, const u_int16_t *, bus_size_t);
void		__C(CHIP,_io_write_region_4)(void *, bus_space_handle_t,
		    bus_size_t, const u_int32_t *, bus_size_t);
void		__C(CHIP,_io_write_region_8)(void *, bus_space_handle_t,
		    bus_size_t, const u_int64_t *, bus_size_t);

/* set multiple */
void		__C(CHIP,_io_set_multi_1)(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t, bus_size_t);
void		__C(CHIP,_io_set_multi_2)(void *, bus_space_handle_t,
		    bus_size_t, u_int16_t, bus_size_t);
void		__C(CHIP,_io_set_multi_4)(void *, bus_space_handle_t,
		    bus_size_t, u_int32_t, bus_size_t);
void		__C(CHIP,_io_set_multi_8)(void *, bus_space_handle_t,
		    bus_size_t, u_int64_t, bus_size_t);

/* set region */
void		__C(CHIP,_io_set_region_1)(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t, bus_size_t);
void		__C(CHIP,_io_set_region_2)(void *, bus_space_handle_t,
		    bus_size_t, u_int16_t, bus_size_t);
void		__C(CHIP,_io_set_region_4)(void *, bus_space_handle_t,
		    bus_size_t, u_int32_t, bus_size_t);
void		__C(CHIP,_io_set_region_8)(void *, bus_space_handle_t,
		    bus_size_t, u_int64_t, bus_size_t);

/* copy */
void		__C(CHIP,_io_copy_1)(void *, bus_space_handle_t,
		    bus_size_t, bus_space_handle_t, bus_size_t, bus_size_t);
void		__C(CHIP,_io_copy_2)(void *, bus_space_handle_t,
		    bus_size_t, bus_space_handle_t, bus_size_t, bus_size_t);
void		__C(CHIP,_io_copy_4)(void *, bus_space_handle_t,
		    bus_size_t, bus_space_handle_t, bus_size_t, bus_size_t);
void		__C(CHIP,_io_copy_8)(void *, bus_space_handle_t,
		    bus_size_t, bus_space_handle_t, bus_size_t, bus_size_t);

/* read multiple raw */
void		__C(CHIP,_io_read_raw_multi_2)(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t *, bus_size_t);
void		__C(CHIP,_io_read_raw_multi_4)(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t *, bus_size_t);
void		__C(CHIP,_io_read_raw_multi_8)(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t *, bus_size_t);

/* write multiple raw */
void		__C(CHIP,_io_write_raw_multi_2)(void *,
		    bus_space_handle_t, bus_size_t, const u_int8_t *,
		    bus_size_t);
void		__C(CHIP,_io_write_raw_multi_4)(void *,
		    bus_space_handle_t, bus_size_t, const u_int8_t *,
		    bus_size_t);
void		__C(CHIP,_io_write_raw_multi_8)(void *,
		    bus_space_handle_t, bus_size_t, const u_int8_t *,
		    bus_size_t);

void
__C(CHIP,_bus_io_init)(bus_space_tag_t t, void *v)
{
	struct extent *ex;

	/*
	 * Initialize the bus space tag.
	 */

	/* cookie */
	t->abs_cookie =		v;

	/* mapping/unmapping */
	t->abs_map =		__C(CHIP,_io_map);
	t->abs_unmap =		__C(CHIP,_io_unmap);
	t->abs_subregion =	__C(CHIP,_io_subregion);

	/* allocation/deallocation */
	t->abs_alloc =		__C(CHIP,_io_alloc);
	t->abs_free = 		__C(CHIP,_io_free);

	/* get kernel virtual address */
	t->abs_vaddr =		__C(CHIP,_io_vaddr);

	/* barrier */
	t->abs_barrier =	__C(CHIP,_io_barrier);
	
	/* read (single) */
	t->abs_r_1 =		__C(CHIP,_io_read_1);
	t->abs_r_2 =		__C(CHIP,_io_read_2);
	t->abs_r_4 =		__C(CHIP,_io_read_4);
	t->abs_r_8 =		__C(CHIP,_io_read_8);
	
	/* read multiple */
	t->abs_rm_1 =		__C(CHIP,_io_read_multi_1);
	t->abs_rm_2 =		__C(CHIP,_io_read_multi_2);
	t->abs_rm_4 =		__C(CHIP,_io_read_multi_4);
	t->abs_rm_8 =		__C(CHIP,_io_read_multi_8);
	
	/* read region */
	t->abs_rr_1 =		__C(CHIP,_io_read_region_1);
	t->abs_rr_2 =		__C(CHIP,_io_read_region_2);
	t->abs_rr_4 =		__C(CHIP,_io_read_region_4);
	t->abs_rr_8 =		__C(CHIP,_io_read_region_8);
	
	/* write (single) */
	t->abs_w_1 =		__C(CHIP,_io_write_1);
	t->abs_w_2 =		__C(CHIP,_io_write_2);
	t->abs_w_4 =		__C(CHIP,_io_write_4);
	t->abs_w_8 =		__C(CHIP,_io_write_8);
	
	/* write multiple */
	t->abs_wm_1 =		__C(CHIP,_io_write_multi_1);
	t->abs_wm_2 =		__C(CHIP,_io_write_multi_2);
	t->abs_wm_4 =		__C(CHIP,_io_write_multi_4);
	t->abs_wm_8 =		__C(CHIP,_io_write_multi_8);
	
	/* write region */
	t->abs_wr_1 =		__C(CHIP,_io_write_region_1);
	t->abs_wr_2 =		__C(CHIP,_io_write_region_2);
	t->abs_wr_4 =		__C(CHIP,_io_write_region_4);
	t->abs_wr_8 =		__C(CHIP,_io_write_region_8);

	/* set multiple */
	t->abs_sm_1 =		__C(CHIP,_io_set_multi_1);
	t->abs_sm_2 =		__C(CHIP,_io_set_multi_2);
	t->abs_sm_4 =		__C(CHIP,_io_set_multi_4);
	t->abs_sm_8 =		__C(CHIP,_io_set_multi_8);
	
	/* set region */
	t->abs_sr_1 =		__C(CHIP,_io_set_region_1);
	t->abs_sr_2 =		__C(CHIP,_io_set_region_2);
	t->abs_sr_4 =		__C(CHIP,_io_set_region_4);
	t->abs_sr_8 =		__C(CHIP,_io_set_region_8);

	/* copy */
	t->abs_c_1 =		__C(CHIP,_io_copy_1);
	t->abs_c_2 =		__C(CHIP,_io_copy_2);
	t->abs_c_4 =		__C(CHIP,_io_copy_4);
	t->abs_c_8 =		__C(CHIP,_io_copy_8);

	/* read multiple raw */
	t->abs_rrm_2 =		__C(CHIP,_io_read_raw_multi_2);
	t->abs_rrm_4 =		__C(CHIP,_io_read_raw_multi_4);
	t->abs_rrm_8 =		__C(CHIP,_io_read_raw_multi_8);
	
	/* write multiple raw*/
	t->abs_wrm_2 =		__C(CHIP,_io_write_raw_multi_2);
	t->abs_wrm_4 =		__C(CHIP,_io_write_raw_multi_4);
	t->abs_wrm_8 =		__C(CHIP,_io_write_raw_multi_8);

	ex = extent_create(CHIP_EXTENT_NAME(v), 0x0UL, 0xffffffffUL,
	    M_DEVBUF, (caddr_t)CHIP_EXTENT_STORAGE(v),
	    sizeof(CHIP_EXTENT_STORAGE(v)), EX_NOWAIT|EX_NOCOALESCE);

	CHIP_IO_EXTENT(v) = ex;
}

int
__C(CHIP,_io_map)(void *v, bus_addr_t ioaddr, bus_size_t iosize, int flags,
    bus_space_handle_t *iohp)
{
	int error;

	/*
	 * Can't map i/o space linearly.
	 */
	if (flags & BUS_SPACE_MAP_LINEAR)
		return (EOPNOTSUPP);

#ifdef EXTENT_DEBUG
	printf("io: allocating 0x%lx to 0x%lx\n", ioaddr, ioaddr + iosize - 1);
#endif
	error = extent_alloc_region(CHIP_IO_EXTENT(v), ioaddr, iosize,
	    EX_NOWAIT | (CHIP_EX_MALLOC_SAFE(v) ? EX_MALLOCOK : 0));
	if (error) {
#ifdef EXTENT_DEBUG
		printf("io: allocation failed (%d)\n", error);
		extent_print(CHIP_IO_EXTENT(v));
#endif
		return (error);
	}

	*iohp = ALPHA_PHYS_TO_K0SEG(CHIP_IO_SYS_START(v)) + ioaddr;

	return (0);
}

void
__C(CHIP,_io_unmap)(void *v, bus_space_handle_t ioh, bus_size_t iosize)
{
	bus_addr_t ioaddr;
	int error;

#ifdef EXTENT_DEBUG
	printf("io: freeing handle 0x%lx for 0x%lx\n", ioh, iosize);
#endif

	ioaddr = ioh - ALPHA_PHYS_TO_K0SEG(CHIP_IO_SYS_START(v));

#ifdef EXTENT_DEBUG
	printf("io: freeing 0x%lx to 0x%lx\n", ioaddr, ioaddr + iosize - 1);
#endif
	error = extent_free(CHIP_IO_EXTENT(v), ioaddr, iosize,
	    EX_NOWAIT | (CHIP_EX_MALLOC_SAFE(v) ? EX_MALLOCOK : 0));
	if (error) {
		printf("%s: WARNING: could not unmap 0x%lx-0x%lx (error %d)\n",
		   __S(__C(CHIP,_io_unmap)), ioaddr, ioaddr + iosize - 1,
		   error);
#ifdef EXTENT_DEBUG
		extent_print(CHIP_IO_EXTENT(v));
#endif
	}	
}

int
__C(CHIP,_io_subregion)(void *v, bus_space_handle_t ioh, bus_size_t offset,
    bus_size_t size, bus_space_handle_t *nioh)
{

	*nioh = ioh + offset;
	return (0);
}

int
__C(CHIP,_io_alloc)(void *v, bus_addr_t rstart, bus_addr_t rend,
    bus_size_t size, bus_size_t align, bus_size_t boundary, int flags,
    bus_addr_t *addrp, bus_space_handle_t *bshp)
{

	/* XXX XXX XXX XXX XXX XXX */
	panic("%s not implemented", __S(__C(CHIP,_io_alloc)));
}

void
__C(CHIP,_io_free)(void *v, bus_space_handle_t bsh, bus_size_t size)
{

	/* XXX XXX XXX XXX XXX XXX */
	panic("%s not implemented", __S(__C(CHIP,_io_free)));
}

void *
__C(CHIP,_io_vaddr)(void *v, bus_space_handle_t bsh)
{
	/*
	 * _io_map() catches BUS_SPACE_MAP_LINEAR,
	 * so we shouldn't get here
	 */
	panic("_io_vaddr");
}

inline void
__C(CHIP,_io_barrier)(void *v, bus_space_handle_t h, bus_size_t o, bus_size_t l,
    int f)
{

	if ((f & BUS_SPACE_BARRIER_READ) != 0)
		alpha_mb();
	else if ((f & BUS_SPACE_BARRIER_WRITE) != 0)
		alpha_wmb();
}

inline u_int8_t
__C(CHIP,_io_read_1)(void *v, bus_space_handle_t ioh, bus_size_t off)
{
	bus_addr_t addr;

	addr = ioh + off;
	alpha_mb();
	return (alpha_ldbu((u_int8_t *)addr));
}

inline u_int16_t
__C(CHIP,_io_read_2)(void *v, bus_space_handle_t ioh, bus_size_t off)
{
	bus_addr_t addr;

	addr = ioh + off;
#ifdef DIAGNOSTIC
	if (addr & 1)
		panic(__S(__C(CHIP,_io_read_2)) ": addr 0x%lx not aligned",
		    addr);
#endif
	alpha_mb();
	return (alpha_ldwu((u_int16_t *)addr));
}

inline u_int32_t
__C(CHIP,_io_read_4)(void *v, bus_space_handle_t ioh, bus_size_t off)
{
	bus_addr_t addr;

	addr = ioh + off;
#ifdef DIAGNOSTIC
	if (addr & 3)
		panic(__S(__C(CHIP,_io_read_4)) ": addr 0x%lx not aligned",
		    addr);
#endif
	alpha_mb();
	return (*(u_int32_t *)addr);
}

inline u_int64_t
__C(CHIP,_io_read_8)(void *v, bus_space_handle_t ioh, bus_size_t off)
{

	/* XXX XXX XXX */
	panic("%s not implemented", __S(__C(CHIP,_io_read_8)));
}

#define CHIP_io_read_multi_N(BYTES,TYPE)				\
void									\
__C(__C(CHIP,_io_read_multi_),BYTES)(void *v, bus_space_handle_t h,	\
    bus_size_t o, TYPE *a, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		__C(CHIP,_io_barrier)(v, h, o, sizeof *a,		\
		    BUS_SPACE_BARRIER_READ);				\
		*a++ = __C(__C(CHIP,_io_read_),BYTES)(v, h, o);		\
	}								\
}
CHIP_io_read_multi_N(1,u_int8_t)
CHIP_io_read_multi_N(2,u_int16_t)
CHIP_io_read_multi_N(4,u_int32_t)
CHIP_io_read_multi_N(8,u_int64_t)

#define CHIP_io_read_region_N(BYTES,TYPE)				\
void									\
__C(__C(CHIP,_io_read_region_),BYTES)(void *v, bus_space_handle_t h,	\
    bus_size_t o, TYPE *a, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		*a++ = __C(__C(CHIP,_io_read_),BYTES)(v, h, o);		\
		o += sizeof *a;						\
	}								\
}
CHIP_io_read_region_N(1,u_int8_t)
CHIP_io_read_region_N(2,u_int16_t)
CHIP_io_read_region_N(4,u_int32_t)
CHIP_io_read_region_N(8,u_int64_t)

inline void
__C(CHIP,_io_write_1)(void *v, bus_space_handle_t ioh, bus_size_t off,
    u_int8_t val)
{
	bus_addr_t addr;

	addr = ioh + off;
	alpha_stb((u_int8_t *)addr, val);
	alpha_mb();
}

inline void
__C(CHIP,_io_write_2)(void *v, bus_space_handle_t ioh, bus_size_t off,
    u_int16_t val)
{
	bus_addr_t addr;

	addr = ioh + off;
#ifdef DIAGNOSTIC
	if (addr & 1)
		panic(__S(__C(CHIP,_io_write_2)) ": addr 0x%lx not aligned",
		    addr);
#endif
	alpha_stw((u_int16_t *)addr, val);
	alpha_mb();
}

inline void
__C(CHIP,_io_write_4)(void *v, bus_space_handle_t ioh, bus_size_t off,
    u_int32_t val)
{
	bus_addr_t addr;

	addr = ioh + off;
#ifdef DIAGNOSTIC
	if (addr & 3)
		panic(__S(__C(CHIP,_io_write_4)) ": addr 0x%lx not aligned",
		    addr);
#endif
	*(u_int32_t *)addr = val;
	alpha_mb();
}

inline void
__C(CHIP,_io_write_8)(void *v, bus_space_handle_t ioh, bus_size_t off,
    u_int64_t val)
{

	/* XXX XXX XXX */
	panic("%s not implemented", __S(__C(CHIP,_io_write_8)));
	alpha_mb();
}

#define CHIP_io_write_multi_N(BYTES,TYPE)				\
void									\
__C(__C(CHIP,_io_write_multi_),BYTES)(void *v, bus_space_handle_t h,	\
    bus_size_t o, const TYPE *a, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		__C(__C(CHIP,_io_write_),BYTES)(v, h, o, *a++);		\
		__C(CHIP,_io_barrier)(v, h, o, sizeof *a,		\
		    BUS_SPACE_BARRIER_WRITE);				\
	}								\
}
CHIP_io_write_multi_N(1,u_int8_t)
CHIP_io_write_multi_N(2,u_int16_t)
CHIP_io_write_multi_N(4,u_int32_t)
CHIP_io_write_multi_N(8,u_int64_t)

#define CHIP_io_write_region_N(BYTES,TYPE)				\
void									\
__C(__C(CHIP,_io_write_region_),BYTES)(void *v, bus_space_handle_t h,	\
    bus_size_t o, const TYPE *a, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		__C(__C(CHIP,_io_write_),BYTES)(v, h, o, *a++);		\
		o += sizeof *a;						\
	}								\
}
CHIP_io_write_region_N(1,u_int8_t)
CHIP_io_write_region_N(2,u_int16_t)
CHIP_io_write_region_N(4,u_int32_t)
CHIP_io_write_region_N(8,u_int64_t)

#define CHIP_io_set_multi_N(BYTES,TYPE)					\
void									\
__C(__C(CHIP,_io_set_multi_),BYTES)(void *v, bus_space_handle_t h,	\
    bus_size_t o, TYPE val, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		__C(__C(CHIP,_io_write_),BYTES)(v, h, o, val);		\
		__C(CHIP,_io_barrier)(v, h, o, sizeof val,		\
		    BUS_SPACE_BARRIER_WRITE);				\
	}								\
}
CHIP_io_set_multi_N(1,u_int8_t)
CHIP_io_set_multi_N(2,u_int16_t)
CHIP_io_set_multi_N(4,u_int32_t)
CHIP_io_set_multi_N(8,u_int64_t)

#define CHIP_io_set_region_N(BYTES,TYPE)				\
void									\
__C(__C(CHIP,_io_set_region_),BYTES)(void *v, bus_space_handle_t h,	\
    bus_size_t o, TYPE val, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		__C(__C(CHIP,_io_write_),BYTES)(v, h, o, val);		\
		o += sizeof val;					\
	}								\
}
CHIP_io_set_region_N(1,u_int8_t)
CHIP_io_set_region_N(2,u_int16_t)
CHIP_io_set_region_N(4,u_int32_t)
CHIP_io_set_region_N(8,u_int64_t)

#define	CHIP_io_copy_N(BYTES)						\
void									\
__C(__C(CHIP,_io_copy_),BYTES)(void *v, bus_space_handle_t h1,		\
    bus_size_t o1, bus_space_handle_t h2, bus_size_t o2, bus_size_t c)	\
{									\
	bus_size_t i, o;						\
									\
	for (i = 0, o = 0; i < c; i++, o += BYTES)			\
		__C(__C(CHIP,_io_write_),BYTES)(v, h2, o2 + o,		\
		    __C(__C(CHIP,_io_read_),BYTES)(v, h1, o1 + o));	\
}
CHIP_io_copy_N(1)
CHIP_io_copy_N(2)
CHIP_io_copy_N(4)
CHIP_io_copy_N(8)

#define CHIP_io_read_raw_multi_N(BYTES,TYPE)				\
void									\
__C(__C(CHIP,_io_read_raw_multi_),BYTES)(void *v, bus_space_handle_t h,	\
    bus_size_t o, u_int8_t *a, bus_size_t c)				\
{									\
	TYPE temp;							\
	int i;								\
									\
	while (c > 0) {							\
		__C(CHIP,_io_barrier)(v, h, o, BYTES,			\
		    BUS_SPACE_BARRIER_READ);				\
		temp = __C(__C(CHIP,_io_read_),BYTES)(v, h, o);		\
		i = MIN(c, BYTES);					\
		c -= i;							\
		while (i--) {						\
			*a++ = temp & 0xff;				\
			temp >>= 8;					\
		}							\
	}								\
}
CHIP_io_read_raw_multi_N(2,u_int16_t)
CHIP_io_read_raw_multi_N(4,u_int32_t)
CHIP_io_read_raw_multi_N(8,u_int64_t)

#define CHIP_io_write_raw_multi_N(BYTES,TYPE)				\
void									\
__C(__C(CHIP,_io_write_raw_multi_),BYTES)(void *v, bus_space_handle_t h,\
    bus_size_t o, const u_int8_t *a, bus_size_t c)			\
{									\
	TYPE temp;							\
	int i;								\
									\
	while (c > 0) {							\
		temp = 0;						\
		for (i = BYTES - 1; i >= 0; i--) {			\
			temp <<= 8;					\
			if (i < c)					\
				temp |= *(a + i);			\
		}							\
		__C(__C(CHIP,_io_write_),BYTES)(v, h, o, temp);		\
		__C(CHIP,_io_barrier)(v, h, o, BYTES,			\
		    BUS_SPACE_BARRIER_WRITE);				\
		i = MIN(c, BYTES); 					\
		c -= i;							\
		a += i;							\
	}								\
}
CHIP_io_write_raw_multi_N(2,u_int16_t)
CHIP_io_write_raw_multi_N(4,u_int32_t)
CHIP_io_write_raw_multi_N(8,u_int64_t)
