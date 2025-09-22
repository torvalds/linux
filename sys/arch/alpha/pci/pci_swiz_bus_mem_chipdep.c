/*	$OpenBSD: pci_swiz_bus_mem_chipdep.c,v 1.10 2025/06/29 15:55:21 miod Exp $	*/
/*	$NetBSD: pcs_bus_mem_common.c,v 1.15 1996/12/02 22:19:36 cgd Exp $	*/

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
 *	CHIP_D_MEM_BASE	Dense Mem space base to use.
 *	CHIP_S_MEM_BASE	Sparse Mem space base to use.
 */

#include <sys/extent.h>

#define	__C(A,B)	__CONCAT(A,B)
#define	__S(S)		__STRING(S)

#ifndef	CHIP_EXTENT_DNAME
#define	CHIP_EXTENT_DNAME(v)	__S(__C(CHIP,_bus_dmem))
#endif
#ifndef CHIP_EXTENT_SNAME
#define CHIP_EXTENT_SNAME(v)	__S(__C(CHIP,_bus_smem))
#endif

#ifndef	CHIP_EXTENT_DSTORAGE
#define CHIP_EXTENT_DSTORAGE(v)	__C(CHIP,_dmem_ex_storage)
static long
    __C(CHIP,_dmem_ex_storage)[EXTENT_FIXED_STORAGE_SIZE(8) / sizeof(long)];
#endif
#ifndef CHIP_EXTENT_SSTORAGE
#define CHIP_EXTENT_SSTORAGE(v)	__C(CHIP,_smem_ex_storage)
static long
    __C(CHIP,_smem_ex_storage)[EXTENT_FIXED_STORAGE_SIZE(8) / sizeof(long)];

#endif

/* mapping/unmapping */
int		__C(CHIP,_mem_map)(void *, bus_addr_t, bus_size_t, int,
		    bus_space_handle_t *);
void		__C(CHIP,_mem_unmap)(void *, bus_space_handle_t,
		    bus_size_t);
int		__C(CHIP,_mem_subregion)(void *, bus_space_handle_t,
		    bus_size_t, bus_size_t, bus_space_handle_t *);

/* allocation/deallocation */
int		__C(CHIP,_mem_alloc)(void *, bus_addr_t, bus_addr_t,
		    bus_size_t, bus_size_t, bus_addr_t, int, bus_addr_t *,
		    bus_space_handle_t *);
void		__C(CHIP,_mem_free)(void *, bus_space_handle_t,
		    bus_size_t);

/* get kernel virtual address */
void *		__C(CHIP,_mem_vaddr)(void *, bus_space_handle_t);

/* barrier */
inline void	__C(CHIP,_mem_barrier)(void *, bus_space_handle_t,
		    bus_size_t, bus_size_t, int);

/* read (single) */
inline u_int8_t	__C(CHIP,_mem_read_1)(void *, bus_space_handle_t,
		    bus_size_t);
inline u_int16_t __C(CHIP,_mem_read_2)(void *, bus_space_handle_t,
		    bus_size_t);
inline u_int32_t __C(CHIP,_mem_read_4)(void *, bus_space_handle_t,
		    bus_size_t);
inline u_int64_t __C(CHIP,_mem_read_8)(void *, bus_space_handle_t,
		    bus_size_t);

/* read multiple */
void		__C(CHIP,_mem_read_multi_1)(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t *, bus_size_t);
void		__C(CHIP,_mem_read_multi_2)(void *, bus_space_handle_t,
		    bus_size_t, u_int16_t *, bus_size_t);
void		__C(CHIP,_mem_read_multi_4)(void *, bus_space_handle_t,
		    bus_size_t, u_int32_t *, bus_size_t);
void		__C(CHIP,_mem_read_multi_8)(void *, bus_space_handle_t,
		    bus_size_t, u_int64_t *, bus_size_t);

/* read region */
void		__C(CHIP,_mem_read_region_1)(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t *, bus_size_t);
void		__C(CHIP,_mem_read_region_2)(void *, bus_space_handle_t,
		    bus_size_t, u_int16_t *, bus_size_t);
void		__C(CHIP,_mem_read_region_4)(void *, bus_space_handle_t,
		    bus_size_t, u_int32_t *, bus_size_t);
void		__C(CHIP,_mem_read_region_8)(void *, bus_space_handle_t,
		    bus_size_t, u_int64_t *, bus_size_t);

/* write (single) */
inline void	__C(CHIP,_mem_write_1)(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t);
inline void	__C(CHIP,_mem_write_2)(void *, bus_space_handle_t,
		    bus_size_t, u_int16_t);
inline void	__C(CHIP,_mem_write_4)(void *, bus_space_handle_t,
		    bus_size_t, u_int32_t);
inline void	__C(CHIP,_mem_write_8)(void *, bus_space_handle_t,
		    bus_size_t, u_int64_t);

/* write multiple */
void		__C(CHIP,_mem_write_multi_1)(void *, bus_space_handle_t,
		    bus_size_t, const u_int8_t *, bus_size_t);
void		__C(CHIP,_mem_write_multi_2)(void *, bus_space_handle_t,
		    bus_size_t, const u_int16_t *, bus_size_t);
void		__C(CHIP,_mem_write_multi_4)(void *, bus_space_handle_t,
		    bus_size_t, const u_int32_t *, bus_size_t);
void		__C(CHIP,_mem_write_multi_8)(void *, bus_space_handle_t,
		    bus_size_t, const u_int64_t *, bus_size_t);

/* write region */
void		__C(CHIP,_mem_write_region_1)(void *, bus_space_handle_t,
		    bus_size_t, const u_int8_t *, bus_size_t);
void		__C(CHIP,_mem_write_region_2)(void *, bus_space_handle_t,
		    bus_size_t, const u_int16_t *, bus_size_t);
void		__C(CHIP,_mem_write_region_4)(void *, bus_space_handle_t,
		    bus_size_t, const u_int32_t *, bus_size_t);
void		__C(CHIP,_mem_write_region_8)(void *, bus_space_handle_t,
		    bus_size_t, const u_int64_t *, bus_size_t);

/* set multiple */
void		__C(CHIP,_mem_set_multi_1)(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t, bus_size_t);
void		__C(CHIP,_mem_set_multi_2)(void *, bus_space_handle_t,
		    bus_size_t, u_int16_t, bus_size_t);
void		__C(CHIP,_mem_set_multi_4)(void *, bus_space_handle_t,
		    bus_size_t, u_int32_t, bus_size_t);
void		__C(CHIP,_mem_set_multi_8)(void *, bus_space_handle_t,
		    bus_size_t, u_int64_t, bus_size_t);

/* set region */
void		__C(CHIP,_mem_set_region_1)(void *, bus_space_handle_t,
		    bus_size_t, u_int8_t, bus_size_t);
void		__C(CHIP,_mem_set_region_2)(void *, bus_space_handle_t,
		    bus_size_t, u_int16_t, bus_size_t);
void		__C(CHIP,_mem_set_region_4)(void *, bus_space_handle_t,
		    bus_size_t, u_int32_t, bus_size_t);
void		__C(CHIP,_mem_set_region_8)(void *, bus_space_handle_t,
		    bus_size_t, u_int64_t, bus_size_t);

/* copy */
void		__C(CHIP,_mem_copy_1)(void *, bus_space_handle_t,
		    bus_size_t, bus_space_handle_t, bus_size_t, bus_size_t);
void		__C(CHIP,_mem_copy_2)(void *, bus_space_handle_t,
		    bus_size_t, bus_space_handle_t, bus_size_t, bus_size_t);
void		__C(CHIP,_mem_copy_4)(void *, bus_space_handle_t,
		    bus_size_t, bus_space_handle_t, bus_size_t, bus_size_t);
void		__C(CHIP,_mem_copy_8)(void *, bus_space_handle_t,
		    bus_size_t, bus_space_handle_t, bus_size_t, bus_size_t);

/* read multiple raw */
void		__C(CHIP,_mem_read_raw_multi_2)(void *,
		    bus_space_handle_t, bus_size_t, u_int8_t *, bus_size_t);
void		__C(CHIP,_mem_read_raw_multi_4)(void *,
		    bus_space_handle_t, bus_size_t, u_int8_t *, bus_size_t);
void		__C(CHIP,_mem_read_raw_multi_8)(void *,
		    bus_space_handle_t, bus_size_t, u_int8_t *, bus_size_t);

/* write multiple raw */
void		__C(CHIP,_mem_write_raw_multi_2)(void *,
		    bus_space_handle_t, bus_size_t, const u_int8_t *,
		    bus_size_t);
void		__C(CHIP,_mem_write_raw_multi_4)(void *,
		    bus_space_handle_t, bus_size_t, const u_int8_t *,
		    bus_size_t);
void		__C(CHIP,_mem_write_raw_multi_8)(void *,
		    bus_space_handle_t, bus_size_t, const u_int8_t *,
		    bus_size_t);

void
__C(CHIP,_bus_mem_init)(bus_space_tag_t t, void *v)
{
	struct extent *dex, *sex;

	/*
	 * Initialize the bus space tag.
	 */

	/* cookie */
	t->abs_cookie = v;

	/* mapping/unmapping */
	t->abs_map =		__C(CHIP,_mem_map);
	t->abs_unmap =		__C(CHIP,_mem_unmap);
	t->abs_subregion =	__C(CHIP,_mem_subregion);

	/* allocation/deallocation */
	t->abs_alloc =		__C(CHIP,_mem_alloc);
	t->abs_free = 		__C(CHIP,_mem_free);

	/* get kernel virtual address */
	t->abs_vaddr =		__C(CHIP,_mem_vaddr);
	/* barrier */
	t->abs_barrier =	__C(CHIP,_mem_barrier);
	
	/* read (single) */
	t->abs_r_1 =		__C(CHIP,_mem_read_1);
	t->abs_r_2 =		__C(CHIP,_mem_read_2);
	t->abs_r_4 =		__C(CHIP,_mem_read_4);
	t->abs_r_8 =		__C(CHIP,_mem_read_8);
	
	/* read multiple */
	t->abs_rm_1 =		__C(CHIP,_mem_read_multi_1);
	t->abs_rm_2 =		__C(CHIP,_mem_read_multi_2);
	t->abs_rm_4 =		__C(CHIP,_mem_read_multi_4);
	t->abs_rm_8 =		__C(CHIP,_mem_read_multi_8);
	
	/* read region */
	t->abs_rr_1 =		__C(CHIP,_mem_read_region_1);
	t->abs_rr_2 =		__C(CHIP,_mem_read_region_2);
	t->abs_rr_4 =		__C(CHIP,_mem_read_region_4);
	t->abs_rr_8 =		__C(CHIP,_mem_read_region_8);
	
	/* write (single) */
	t->abs_w_1 =		__C(CHIP,_mem_write_1);
	t->abs_w_2 =		__C(CHIP,_mem_write_2);
	t->abs_w_4 =		__C(CHIP,_mem_write_4);
	t->abs_w_8 =		__C(CHIP,_mem_write_8);
	
	/* write multiple */
	t->abs_wm_1 =		__C(CHIP,_mem_write_multi_1);
	t->abs_wm_2 =		__C(CHIP,_mem_write_multi_2);
	t->abs_wm_4 =		__C(CHIP,_mem_write_multi_4);
	t->abs_wm_8 =		__C(CHIP,_mem_write_multi_8);
	
	/* write region */
	t->abs_wr_1 =		__C(CHIP,_mem_write_region_1);
	t->abs_wr_2 =		__C(CHIP,_mem_write_region_2);
	t->abs_wr_4 =		__C(CHIP,_mem_write_region_4);
	t->abs_wr_8 =		__C(CHIP,_mem_write_region_8);

	/* set multiple */
	t->abs_sm_1 =		__C(CHIP,_mem_set_multi_1);
	t->abs_sm_2 =		__C(CHIP,_mem_set_multi_2);
	t->abs_sm_4 =		__C(CHIP,_mem_set_multi_4);
	t->abs_sm_8 =		__C(CHIP,_mem_set_multi_8);
	
	/* set region */
	t->abs_sr_1 =		__C(CHIP,_mem_set_region_1);
	t->abs_sr_2 =		__C(CHIP,_mem_set_region_2);
	t->abs_sr_4 =		__C(CHIP,_mem_set_region_4);
	t->abs_sr_8 =		__C(CHIP,_mem_set_region_8);

	/* copy */
	t->abs_c_1 =		__C(CHIP,_mem_copy_1);
	t->abs_c_2 =		__C(CHIP,_mem_copy_2);
	t->abs_c_4 =		__C(CHIP,_mem_copy_4);
	t->abs_c_8 =		__C(CHIP,_mem_copy_8);

	/* read multiple raw */
	t->abs_rrm_2 =		__C(CHIP,_mem_read_raw_multi_2);
	t->abs_rrm_4 =		__C(CHIP,_mem_read_raw_multi_4);
	t->abs_rrm_8 =		__C(CHIP,_mem_read_raw_multi_8);
	
	/* write multiple raw*/
	t->abs_wrm_2 =		__C(CHIP,_mem_write_raw_multi_2);
	t->abs_wrm_4 =		__C(CHIP,_mem_write_raw_multi_4);
	t->abs_wrm_8 =		__C(CHIP,_mem_write_raw_multi_8);

	/* XXX WE WANT EXTENT_NOCOALESCE, BUT WE CAN'T USE IT. XXX */
	dex = extent_create(CHIP_EXTENT_DNAME(v), 0x0UL,
	    0xffffffffffffffffUL, M_DEVBUF,
	    (caddr_t)CHIP_EXTENT_DSTORAGE(v),
	    sizeof(CHIP_EXTENT_DSTORAGE(v)), EX_NOWAIT);
	extent_alloc_region(dex, 0, 0xffffffffffffffffUL, EX_NOWAIT);

#ifdef CHIP_D_MEM_W1_BUS_START
#ifdef EXTENT_DEBUG
	printf("dmem: freeing from 0x%lx to 0x%lx\n",
	    CHIP_D_MEM_W1_BUS_START(v), CHIP_D_MEM_W1_BUS_END(v));
#endif
	extent_free(dex, CHIP_D_MEM_W1_BUS_START(v),
	    CHIP_D_MEM_W1_BUS_END(v) - CHIP_D_MEM_W1_BUS_START(v) + 1,
	    EX_NOWAIT);
#endif

#ifdef EXTENT_DEBUG
	extent_print(dex);
#endif
	CHIP_D_MEM_EXTENT(v) = dex;

	/* XXX WE WANT EXTENT_NOCOALESCE, BUT WE CAN'T USE IT. XXX */
	sex = extent_create(CHIP_EXTENT_SNAME(v), 0x0UL,
	    0xffffffffffffffffUL, M_DEVBUF,
	    (caddr_t)CHIP_EXTENT_SSTORAGE(v),
	    sizeof(CHIP_EXTENT_SSTORAGE(v)), EX_NOWAIT);
	extent_alloc_region(sex, 0, 0xffffffffffffffffUL, EX_NOWAIT);

#ifdef CHIP_S_MEM_W1_BUS_START
#ifdef EXTENT_DEBUG
	printf("smem: freeing from 0x%lx to 0x%lx\n",
	    CHIP_S_MEM_W1_BUS_START(v), CHIP_S_MEM_W1_BUS_END(v));
#endif
	extent_free(sex, CHIP_S_MEM_W1_BUS_START(v),
	    CHIP_S_MEM_W1_BUS_END(v) - CHIP_S_MEM_W1_BUS_START(v) + 1,
	    EX_NOWAIT);
#endif
#ifdef CHIP_S_MEM_W2_BUS_START
	if (CHIP_S_MEM_W2_BUS_START(v) != CHIP_S_MEM_W1_BUS_START(v)) {
#ifdef EXTENT_DEBUG
		printf("smem: freeing from 0x%lx to 0x%lx\n",
		    CHIP_S_MEM_W2_BUS_START(v), CHIP_S_MEM_W2_BUS_END(v));
#endif
		extent_free(sex, CHIP_S_MEM_W2_BUS_START(v),
		    CHIP_S_MEM_W2_BUS_END(v) - CHIP_S_MEM_W2_BUS_START(v) + 1,
		    EX_NOWAIT);
	} else {
#ifdef EXTENT_DEBUG
		printf("smem: window 2 (0x%lx to 0x%lx) overlaps window 1\n",
		    CHIP_S_MEM_W2_BUS_START(v), CHIP_S_MEM_W2_BUS_END(v));
#endif
	}
#endif
#ifdef CHIP_S_MEM_W3_BUS_START
	if (CHIP_S_MEM_W3_BUS_START(v) != CHIP_S_MEM_W1_BUS_START(v) &&
	    CHIP_S_MEM_W3_BUS_START(v) != CHIP_S_MEM_W2_BUS_START(v)) {
#ifdef EXTENT_DEBUG
		printf("smem: freeing from 0x%lx to 0x%lx\n",
		    CHIP_S_MEM_W3_BUS_START(v), CHIP_S_MEM_W3_BUS_END(v));
#endif
		extent_free(sex, CHIP_S_MEM_W3_BUS_START(v),
		    CHIP_S_MEM_W3_BUS_END(v) - CHIP_S_MEM_W3_BUS_START(v) + 1,
		    EX_NOWAIT);
	} else {
#ifdef EXTENT_DEBUG
		printf("smem: window 2 (0x%lx to 0x%lx) overlaps window 1\n",
		    CHIP_S_MEM_W2_BUS_START(v), CHIP_S_MEM_W2_BUS_END(v));
#endif
	}
#endif

#ifdef EXTENT_DEBUG
	extent_print(sex);
#endif
	CHIP_S_MEM_EXTENT(v) = sex;
}

static int	__C(CHIP,_xlate_addr_to_dense_handle)(void *,
		    bus_addr_t, bus_space_handle_t *);
static int	__C(CHIP,_xlate_dense_handle_to_addr)(void *,
		    bus_space_handle_t, bus_addr_t *);
static int	__C(CHIP,_xlate_addr_to_sparse_handle)(void *,
		    bus_addr_t, bus_space_handle_t *);
static int	__C(CHIP,_xlate_sparse_handle_to_addr)(void *,
		    bus_space_handle_t, bus_addr_t *);

static int
__C(CHIP,_xlate_addr_to_dense_handle)(void *v, bus_addr_t memaddr,
   bus_space_handle_t *memhp)
{
#ifdef CHIP_D_MEM_W1_BUS_START
	if (memaddr >= CHIP_D_MEM_W1_BUS_START(v) &&
	    memaddr <= CHIP_D_MEM_W1_BUS_END(v)) {
		*memhp = ALPHA_PHYS_TO_K0SEG(CHIP_D_MEM_W1_SYS_START(v)) +
		    (memaddr - CHIP_D_MEM_W1_BUS_START(v));
		return (1);
	} else
#endif
		return (0);
}

static int
__C(CHIP,_xlate_dense_handle_to_addr)(void *v, bus_space_handle_t memh,
    bus_addr_t *memaddrp)
{

	memh = ALPHA_K0SEG_TO_PHYS(memh);

#ifdef CHIP_D_MEM_W1_BUS_START
	if (memh >= CHIP_D_MEM_W1_SYS_START(v) &&
	    memh <= CHIP_D_MEM_W1_SYS_END(v)) {
		*memaddrp = CHIP_D_MEM_W1_BUS_START(v) +
		    (memh - CHIP_D_MEM_W1_SYS_START(v));
		return (1);
	} else
#endif
		return (0);
}

static int
__C(CHIP,_xlate_addr_to_sparse_handle)(void *v, bus_addr_t memaddr,
    bus_space_handle_t *memhp)
{

#ifdef CHIP_S_MEM_W1_BUS_START
	if (memaddr >= CHIP_S_MEM_W1_BUS_START(v) &&
	    memaddr <= CHIP_S_MEM_W1_BUS_END(v)) {
		*memhp =
		    (ALPHA_PHYS_TO_K0SEG(CHIP_S_MEM_W1_SYS_START(v)) >> 5) +
		    (memaddr - CHIP_S_MEM_W1_BUS_START(v));
		return (1);
	} else
#endif
#ifdef CHIP_S_MEM_W2_BUS_START
	if (memaddr >= CHIP_S_MEM_W2_BUS_START(v) &&
	    memaddr <= CHIP_S_MEM_W2_BUS_END(v)) {
		*memhp =
		    (ALPHA_PHYS_TO_K0SEG(CHIP_S_MEM_W2_SYS_START(v)) >> 5) +
		    (memaddr - CHIP_S_MEM_W2_BUS_START(v));
		return (1);
	} else
#endif
#ifdef CHIP_S_MEM_W3_BUS_START
	if (memaddr >= CHIP_S_MEM_W3_BUS_START(v) &&
	    memaddr <= CHIP_S_MEM_W3_BUS_END(v)) {
		*memhp =
		    (ALPHA_PHYS_TO_K0SEG(CHIP_S_MEM_W3_SYS_START(v)) >> 5) +
		    (memaddr - CHIP_S_MEM_W3_BUS_START(v));
		return (1);
	} else
#endif
		return (0);
}

static int
__C(CHIP,_xlate_sparse_handle_to_addr)(void *v, bus_space_handle_t memh,
    bus_addr_t *memaddrp)
{

	memh = ALPHA_K0SEG_TO_PHYS(memh << 5) >> 5;

#ifdef CHIP_S_MEM_W1_BUS_START
	if ((memh << 5) >= CHIP_S_MEM_W1_SYS_START(v) &&
	    (memh << 5) <= CHIP_S_MEM_W1_SYS_END(v)) {
		*memaddrp = CHIP_S_MEM_W1_BUS_START(v) +
		    (memh - (CHIP_S_MEM_W1_SYS_START(v) >> 5));
		return (1);
	} else
#endif
#ifdef CHIP_S_MEM_W2_BUS_START
	if ((memh << 5) >= CHIP_S_MEM_W2_SYS_START(v) &&
	    (memh << 5) <= CHIP_S_MEM_W2_SYS_END(v)) {
		*memaddrp = CHIP_S_MEM_W2_BUS_START(v) +
		    (memh - (CHIP_S_MEM_W2_SYS_START(v) >> 5));
		return (1);
	} else
#endif
#ifdef CHIP_S_MEM_W3_BUS_START
	if ((memh << 5) >= CHIP_S_MEM_W3_SYS_START(v) &&
	    (memh << 5) <= CHIP_S_MEM_W3_SYS_END(v)) {
		*memaddrp = CHIP_S_MEM_W3_BUS_START(v) +
		    (memh - (CHIP_S_MEM_W3_SYS_START(v) >> 5));
		return (1);
	} else
#endif
		return (0);
}

int
__C(CHIP,_mem_map)(void *v, bus_addr_t memaddr, bus_size_t memsize, int flags,
    bus_space_handle_t *memhp)
{
	bus_space_handle_t dh = 0, sh = 0;	/* XXX -Wuninitialized */
	int didd, dids, errord, errors, mustd, musts;
	int prefetchable = flags & BUS_SPACE_MAP_PREFETCHABLE;
	int linear = flags & BUS_SPACE_MAP_LINEAR;

	mustd = 1;
	musts = prefetchable == 0;

	/*
	 * We must have dense space to map memory linearly.
	 */
	if (linear && !prefetchable)
		return (EOPNOTSUPP);

#ifdef EXTENT_DEBUG
	printf("mem: allocating 0x%lx to 0x%lx\n", memaddr,
	    memaddr + memsize - 1);
	printf("mem: %s dense, %s sparse\n", mustd ? "need" : "want",
	    musts ? "need" : "want");
#endif
	errord = extent_alloc_region(CHIP_D_MEM_EXTENT(v), memaddr, memsize,
	    EX_NOWAIT | (CHIP_EX_MALLOC_SAFE(v) ? EX_MALLOCOK : 0));
	didd = (errord == 0);
	errors = extent_alloc_region(CHIP_S_MEM_EXTENT(v), memaddr, memsize,
	    EX_NOWAIT | (CHIP_EX_MALLOC_SAFE(v) ? EX_MALLOCOK : 0));
	dids = (errors == 0);

#ifdef EXTENT_DEBUG
	if (!didd)
		printf("mem: failed to get dense (%d)\n", errord);
	if (!dids)
		printf("mem: failed to get sparse (%d)\n", errors);
#endif

	if ((mustd && !didd) || (musts && !dids))
		goto bad;

	if (didd && !__C(CHIP,_xlate_addr_to_dense_handle)(v, memaddr, &dh)) {
		printf("\n");
#ifdef CHIP_D_MEM_W1_BUS_START
		printf("%s: window[1]=0x%lx-0x%lx\n", __S(__C(CHIP,_mem_map)),
		    CHIP_D_MEM_W1_BUS_START(v), CHIP_D_MEM_W1_BUS_END(v));
#endif
		panic("%s: don't know how to map %lx cacheable",
		    __S(__C(CHIP,_mem_map)), memaddr);
	}

	if (dids && !__C(CHIP,_xlate_addr_to_sparse_handle)(v, memaddr, &sh)) {
		printf("\n");
#ifdef CHIP_S_MEM_W1_BUS_START
		printf("%s: window[1]=0x%lx-0x%lx\n", __S(__C(CHIP,_mem_map)),
		    CHIP_S_MEM_W1_BUS_START(v), CHIP_S_MEM_W1_BUS_END(v));
#endif
#ifdef CHIP_S_MEM_W2_BUS_START
		printf("%s: window[2]=0x%lx-0x%lx\n", __S(__C(CHIP,_mem_map)),
		    CHIP_S_MEM_W2_BUS_START(v), CHIP_S_MEM_W2_BUS_END(v));
#endif
#ifdef CHIP_S_MEM_W3_BUS_START
		printf("%s: window[3]=0x%lx-0x%lx\n", __S(__C(CHIP,_mem_map)),
		    CHIP_S_MEM_W3_BUS_START(v), CHIP_S_MEM_W3_BUS_END(v));
#endif
		panic("%s: don't know how to map %lx non-cacheable",
		    __S(__C(CHIP,_mem_map)), memaddr);
	}

	if (prefetchable)
		*memhp = dh;
	else
		*memhp = sh;
	return (0);

bad:
#ifdef EXTENT_DEBUG
	printf("mem: failed\n");
#endif
	if (didd) {
#ifdef EXTENT_DEBUG
	printf("mem: freeing dense\n");
#endif
		if (extent_free(CHIP_D_MEM_EXTENT(v), memaddr, memsize,
		    EX_NOWAIT | (CHIP_EX_MALLOC_SAFE(v) ? EX_MALLOCOK : 0)) != 0) {
			printf("%s: WARNING: couldn't free dense 0x%lx-0x%lx\n",
			    __S(__C(CHIP,_mem_map)), memaddr,
			    memaddr + memsize - 1);
		}
	}
	if (dids) {
#ifdef EXTENT_DEBUG
	printf("mem: freeing sparse\n");
#endif
		if (extent_free(CHIP_S_MEM_EXTENT(v), memaddr, memsize,
		    EX_NOWAIT | (CHIP_EX_MALLOC_SAFE(v) ? EX_MALLOCOK : 0)) != 0) {
			printf("%s: WARNING: couldn't free sparse 0x%lx-0x%lx\n",
			    __S(__C(CHIP,_mem_map)), memaddr,
			    memaddr + memsize - 1);
		}
	}

#ifdef EXTENT_DEBUG
	extent_print(CHIP_D_MEM_EXTENT(v));
	extent_print(CHIP_S_MEM_EXTENT(v));
#endif

	/*
	 * return dense error if we needed it but couldn't get it, else
	 * sparse error.  The error _has_ to be one of the two...
	 */
	return (mustd && !didd ? errord : (musts && !dids ? errors : EINVAL));
}

void
__C(CHIP,_mem_unmap)(void *v, bus_space_handle_t memh, bus_size_t memsize)
{
	bus_addr_t memaddr;
	bus_space_handle_t temph;
	int sparse, haves, haved;

#ifdef EXTENT_DEBUG
	printf("mem: freeing handle 0x%lx for 0x%lx\n", memh, memsize);
#endif

	/*
	 * Find out what space we're in.
	 */
	sparse = ((memh >> 63) == 0);

	/*
	 * Find out what address we're in in that space.
	 */
	haves = haved = 0;
	if (sparse)
		haves = __C(CHIP,_xlate_sparse_handle_to_addr)(v, memh,
		    &memaddr);
	else
		haved = __C(CHIP,_xlate_dense_handle_to_addr)(v, memh,
		    &memaddr);

	if (!haves && !haved)
		panic("%s: couldn't get addr from %s handle 0x%lx",
		    __S(__C(CHIP,_mem_unmap)), sparse ? "sparse" : "dense",
		    memh);

	/*
	 * Find out were/if that address lives in the other space.
	 */
	if (sparse)
		haved = __C(CHIP,_xlate_addr_to_dense_handle)(v, memaddr,
		    &temph);
	else
		haves = __C(CHIP,_xlate_addr_to_sparse_handle)(v, memaddr,
		    &temph);

	/*
	 * Free any ranges we have.
	 */
#ifdef EXTENT_DEBUG
	printf("mem: it's at 0x%lx (%sdense, %ssparse)\n", memaddr,
	    haved ? "" : "not ", haves ? "" : "not ");
#endif
	if (haved && extent_free(CHIP_D_MEM_EXTENT(v), memaddr, memsize,
	    EX_NOWAIT | (CHIP_EX_MALLOC_SAFE(v) ? EX_MALLOCOK : 0)) != 0) {
		printf("%s: WARNING: couldn't free dense 0x%lx-0x%lx\n",
		    __S(__C(CHIP,_mem_map)), memaddr,
		    memaddr + memsize - 1);
	}
	if (haves && extent_free(CHIP_S_MEM_EXTENT(v), memaddr, memsize,
	    EX_NOWAIT | (CHIP_EX_MALLOC_SAFE(v) ? EX_MALLOCOK : 0)) != 0) {
		printf("%s: WARNING: couldn't free sparse 0x%lx-0x%lx\n",
		    __S(__C(CHIP,_mem_map)), memaddr,
		    memaddr + memsize - 1);
	}
}

int
__C(CHIP,_mem_subregion)(void *v, bus_space_handle_t memh, bus_size_t offset,
    bus_size_t size, bus_space_handle_t *nmemh)
{

	*nmemh = memh + offset;
	return (0);
}

int
__C(CHIP,_mem_alloc)(void *v, bus_addr_t rstart, bus_addr_t rend,
    bus_size_t size, bus_size_t align, bus_size_t boundary, int flags,
    bus_addr_t *addrp, bus_space_handle_t *bshp)
{

	/* XXX XXX XXX XXX XXX XXX */
	panic("%s not implemented", __S(__C(CHIP,_mem_alloc)));
}

void
__C(CHIP,_mem_free)(void *v, bus_space_handle_t bsh, bus_size_t size)
{

	/* XXX XXX XXX XXX XXX XXX */
	panic("%s not implemented", __S(__C(CHIP,_mem_free)));
}

void *
__C(CHIP,_mem_vaddr)(void *v, bus_space_handle_t bsh)
{
	/*
	 * XXX should check that the range was mapped
	 * with BUS_SPACE_MAP_LINEAR for sanity
	 */
	if ((bsh >> 63) != 0)
		return ((void *)bsh);

	return (0);
}

inline void
__C(CHIP,_mem_barrier)(void *v, bus_space_handle_t h, bus_size_t o,
    bus_size_t l, int f)
{

	if ((f & BUS_SPACE_BARRIER_READ) != 0)
		alpha_mb();
	else if ((f & BUS_SPACE_BARRIER_WRITE) != 0)
		alpha_wmb();
}

inline u_int8_t
__C(CHIP,_mem_read_1)(void *v, bus_space_handle_t memh, bus_size_t off)
{
	register bus_space_handle_t tmpmemh;
	register u_int32_t *port, val;
	register u_int8_t rval;
	register int offset;

	alpha_mb();

	if ((memh >> 63) != 0)
		return (*(u_int8_t *)(memh + off));

	tmpmemh = memh + off;
	offset = tmpmemh & 3;
	port = (u_int32_t *)((tmpmemh << 5) | (0 << 3));
	val = *port;
	rval = ((val) >> (8 * offset)) & 0xff;

	return rval;
}

inline u_int16_t
__C(CHIP,_mem_read_2)(void *v, bus_space_handle_t memh, bus_size_t off)
{
	register bus_space_handle_t tmpmemh;
	register u_int32_t *port, val;
	register u_int16_t rval;
	register int offset;

	alpha_mb();

	if ((memh >> 63) != 0)
		return (*(u_int16_t *)(memh + off));

	tmpmemh = memh + off;
	offset = tmpmemh & 3;
	port = (u_int32_t *)((tmpmemh << 5) | (1 << 3));
	val = *port;
	rval = ((val) >> (8 * offset)) & 0xffff;

	return rval;
}

inline u_int32_t
__C(CHIP,_mem_read_4)(void *v, bus_space_handle_t memh, bus_size_t off)
{
	register bus_space_handle_t tmpmemh;
	register u_int32_t *port, val;
	register u_int32_t rval;
	register int offset;

	alpha_mb();

	if ((memh >> 63) != 0)
		return (*(u_int32_t *)(memh + off));

	tmpmemh = memh + off;
	offset = tmpmemh & 3;
	port = (u_int32_t *)((tmpmemh << 5) | (3 << 3));
	val = *port;
#if 0
	rval = ((val) >> (8 * offset)) & 0xffffffff;
#else
	rval = val;
#endif

	return rval;
}

inline u_int64_t
__C(CHIP,_mem_read_8)(void *v, bus_space_handle_t memh, bus_size_t off)
{

	alpha_mb();

	if ((memh >> 63) != 0)
		return (*(u_int64_t *)(memh + off));

	/* XXX XXX XXX */
	panic("%s not implemented", __S(__C(CHIP,_mem_read_8)));
}

#define CHIP_mem_read_multi_N(BYTES,TYPE)				\
void									\
__C(__C(CHIP,_mem_read_multi_),BYTES)(void *v, bus_space_handle_t h,	\
    bus_size_t o, TYPE *a, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		__C(CHIP,_mem_barrier)(v, h, o, sizeof *a,		\
		    BUS_SPACE_BARRIER_READ);				\
		*a++ = __C(__C(CHIP,_mem_read_),BYTES)(v, h, o);	\
	}								\
}
CHIP_mem_read_multi_N(1,u_int8_t)
CHIP_mem_read_multi_N(2,u_int16_t)
CHIP_mem_read_multi_N(4,u_int32_t)
CHIP_mem_read_multi_N(8,u_int64_t)

#define CHIP_mem_read_region_N(BYTES,TYPE)				\
void									\
__C(__C(CHIP,_mem_read_region_),BYTES)(void *v, bus_space_handle_t h,	\
    bus_size_t o, TYPE *a, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		*a++ = __C(__C(CHIP,_mem_read_),BYTES)(v, h, o);	\
		o += sizeof *a;						\
	}								\
}
CHIP_mem_read_region_N(1,u_int8_t)
CHIP_mem_read_region_N(2,u_int16_t)
CHIP_mem_read_region_N(4,u_int32_t)
CHIP_mem_read_region_N(8,u_int64_t)

inline void
__C(CHIP,_mem_write_1)(void *v, bus_space_handle_t memh, bus_size_t off,
    u_int8_t val)
{
	register bus_space_handle_t tmpmemh;
	register u_int32_t *port, nval;
	register int offset;

	if ((memh >> 63) != 0)
		(*(u_int8_t *)(memh + off)) = val;
	else {
		tmpmemh = memh + off;
		offset = tmpmemh & 3;
		nval = val << (8 * offset);
		port = (u_int32_t *)((tmpmemh << 5) | (0 << 3));
		*port = nval;
	}
	alpha_mb();
}

inline void
__C(CHIP,_mem_write_2)(void *v, bus_space_handle_t memh, bus_size_t off,
    u_int16_t val)
{
	register bus_space_handle_t tmpmemh;
	register u_int32_t *port, nval;
	register int offset;

	if ((memh >> 63) != 0)
		(*(u_int16_t *)(memh + off)) = val;
	else {
		tmpmemh = memh + off;
		offset = tmpmemh & 3;
		nval = val << (8 * offset);
		port = (u_int32_t *)((tmpmemh << 5) | (1 << 3));
		*port = nval;
	}
	alpha_mb();
}

inline void
__C(CHIP,_mem_write_4)(void *v, bus_space_handle_t memh, bus_size_t off,
    u_int32_t val)
{
	register bus_space_handle_t tmpmemh;
	register u_int32_t *port, nval;
	register int offset;

	if ((memh >> 63) != 0)
		(*(u_int32_t *)(memh + off)) = val;
	else {
		tmpmemh = memh + off;
		offset = tmpmemh & 3;
		nval = val /*<< (8 * offset)*/;
		port = (u_int32_t *)((tmpmemh << 5) | (3 << 3));
		*port = nval;
	}
	alpha_mb();
}

inline void
__C(CHIP,_mem_write_8)(void *v, bus_space_handle_t memh, bus_size_t off,
    u_int64_t val)
{

	if ((memh >> 63) != 0)
		(*(u_int64_t *)(memh + off)) = val;
	else {
		/* XXX XXX XXX */
		panic("%s not implemented",
		    __S(__C(CHIP,_mem_write_8)));
	}
	alpha_mb();
}

#define CHIP_mem_write_multi_N(BYTES,TYPE)				\
void									\
__C(__C(CHIP,_mem_write_multi_),BYTES)(void *v, bus_space_handle_t h,	\
    bus_size_t o, const TYPE *a, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		__C(__C(CHIP,_mem_write_),BYTES)(v, h, o, *a++);	\
		__C(CHIP,_mem_barrier)(v, h, o, sizeof *a,		\
		    BUS_SPACE_BARRIER_WRITE);				\
	}								\
}
CHIP_mem_write_multi_N(1,u_int8_t)
CHIP_mem_write_multi_N(2,u_int16_t)
CHIP_mem_write_multi_N(4,u_int32_t)
CHIP_mem_write_multi_N(8,u_int64_t)

#define CHIP_mem_write_region_N(BYTES,TYPE)				\
void									\
__C(__C(CHIP,_mem_write_region_),BYTES)(void *v, bus_space_handle_t h,	\
    bus_size_t o, const TYPE *a, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		__C(__C(CHIP,_mem_write_),BYTES)(v, h, o, *a++);	\
		o += sizeof *a;						\
	}								\
}
CHIP_mem_write_region_N(1,u_int8_t)
CHIP_mem_write_region_N(2,u_int16_t)
CHIP_mem_write_region_N(4,u_int32_t)
CHIP_mem_write_region_N(8,u_int64_t)

#define CHIP_mem_set_multi_N(BYTES,TYPE)				\
void									\
__C(__C(CHIP,_mem_set_multi_),BYTES)(void *v, bus_space_handle_t h,	\
    bus_size_t o, TYPE val, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		__C(__C(CHIP,_mem_write_),BYTES)(v, h, o, val);		\
		__C(CHIP,_mem_barrier)(v, h, o, sizeof val,		\
		    BUS_SPACE_BARRIER_WRITE);				\
	}								\
}
CHIP_mem_set_multi_N(1,u_int8_t)
CHIP_mem_set_multi_N(2,u_int16_t)
CHIP_mem_set_multi_N(4,u_int32_t)
CHIP_mem_set_multi_N(8,u_int64_t)

#define CHIP_mem_set_region_N(BYTES,TYPE)				\
void									\
__C(__C(CHIP,_mem_set_region_),BYTES)(void *v, bus_space_handle_t h,	\
    bus_size_t o, TYPE val, bus_size_t c)				\
{									\
									\
	while (c-- > 0) {						\
		__C(__C(CHIP,_mem_write_),BYTES)(v, h, o, val);		\
		o += sizeof val;					\
	}								\
}
CHIP_mem_set_region_N(1,u_int8_t)
CHIP_mem_set_region_N(2,u_int16_t)
CHIP_mem_set_region_N(4,u_int32_t)
CHIP_mem_set_region_N(8,u_int64_t)

#define	CHIP_mem_copy_N(BYTES)						\
void									\
__C(__C(CHIP,_mem_copy_),BYTES)(void *v, bus_space_handle_t h1,		\
    bus_size_t o1, bus_space_handle_t h2, bus_size_t o2, bus_size_t c)	\
{									\
	bus_size_t i, o;						\
									\
	if ((h1 >> 63) != 0 && (h2 >> 63) != 0) {			\
		bcopy((void *)(h1 + o1), (void *)(h2 + o2), c * BYTES);	\
		return;							\
	}								\
									\
	/* Circumvent a common case of overlapping problems */		\
	if (h1 == h2 && o2 > o1)					\
		for (i = 0, o = (c - 1) * BYTES; i < c; i++, o -= BYTES)\
			__C(__C(CHIP,_mem_write_),BYTES)(v, h2, o2 + o,	\
			    __C(__C(CHIP,_mem_read_),BYTES)(v, h1, o1 + o));\
	else								\
		for (i = 0, o = 0; i < c; i++, o += BYTES)		\
			__C(__C(CHIP,_mem_write_),BYTES)(v, h2, o2 + o,	\
			    __C(__C(CHIP,_mem_read_),BYTES)(v, h1, o1 + o));\
}
CHIP_mem_copy_N(1)
CHIP_mem_copy_N(2)
CHIP_mem_copy_N(4)
CHIP_mem_copy_N(8)

#define CHIP_mem_read_raw_multi_N(BYTES,TYPE)				\
void									\
__C(__C(CHIP,_mem_read_raw_multi_),BYTES)(void *v, bus_space_handle_t h,\
    bus_size_t o, u_int8_t *a, bus_size_t c)				\
{									\
	TYPE temp;							\
	int i;								\
									\
	while (c > 0) {							\
		__C(CHIP,_mem_barrier)(v, h, o, BYTES,			\
		    BUS_SPACE_BARRIER_READ);				\
		temp = __C(__C(CHIP,_mem_read_),BYTES)(v, h, o);	\
		i = MIN(c, BYTES);					\
		c -= i;							\
		while (i--) {						\
			*a++ = temp & 0xff;				\
			temp >>= 8;					\
		}							\
	}								\
}
CHIP_mem_read_raw_multi_N(2,u_int16_t)
CHIP_mem_read_raw_multi_N(4,u_int32_t)
CHIP_mem_read_raw_multi_N(8,u_int64_t)

#define CHIP_mem_write_raw_multi_N(BYTES,TYPE)				\
void									\
__C(__C(CHIP,_mem_write_raw_multi_),BYTES)(void *v, bus_space_handle_t h,\
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
		__C(__C(CHIP,_mem_write_),BYTES)(v, h, o, temp);	\
		__C(CHIP,_mem_barrier)(v, h, o, BYTES,			\
		    BUS_SPACE_BARRIER_WRITE);				\
		i = MIN(c, BYTES);					\
		c -= i;							\
		a += i;							\
	}								\
}
CHIP_mem_write_raw_multi_N(2,u_int16_t)
CHIP_mem_write_raw_multi_N(4,u_int32_t)
CHIP_mem_write_raw_multi_N(8,u_int64_t)
