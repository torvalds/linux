/*	$OpenBSD: pmap_table.c,v 1.14 2017/03/20 19:42:51 miod Exp $	*/

/* 
 * Mach Operating System
 * Copyright (c) 1993-1992 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon 
 * the rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <machine/board.h>
#include <machine/pmap_table.h>

#define	R	PROT_READ
#define	RW	(PROT_READ | PROT_WRITE)
#define	CW	CACHE_WT
#define	CI	CACHE_INH
#define	CG	CACHE_GLOBAL

/*  start, size, prot, cacheability */
const struct pmap_table
luna88k_board_table[] = {
#if 0
	{ PROM_ADDR,		PROM_SPACE,		R,	CI },
#endif
	{ FUSE_ROM_ADDR,	FUSE_ROM_SPACE,		R,	CI },
	{ NVRAM_ADDR,		NVRAM_SPACE,		RW,	CI },
	{ NVRAM_ADDR_88K2,	PAGE_SIZE,		RW,	CI },
	{ OBIO_PIO0_BASE,	PAGE_SIZE,		RW,	CI },
	{ OBIO_PIO1_BASE,	PAGE_SIZE,		RW,	CI },
	{ OBIO_SIO,		PAGE_SIZE,		RW,	CI },
	{ OBIO_TAS,		PAGE_SIZE,		RW,	CI },
	{ OBIO_CLOCK0,		PAGE_SIZE,		RW,	CI },
	{ INT_ST_MASK0,		PAGE_SIZE,		RW,	CI },
	{ SOFT_INT0,		PAGE_SIZE,		RW,	CI },
	{ SOFT_INT_FLAG0,	PAGE_SIZE,		RW,	CI },
	{ RESET_CPU0,		PAGE_SIZE,		RW,	CI },
	{ TRI_PORT_RAM,		TRI_PORT_RAM_SPACE,	RW,	CI },
#if 0
	{ EXT_A_ADDR,		EXT_A_SPACE,		RW,	CI },
	{ EXT_B_ADDR,		EXT_B_SPACE,		RW,	CI },
#endif
	{ PC_BASE,		PC_SPACE,		RW,	CI },
#if 0
	{ MROM_ADDR,		MROM_SPACE,		R,	CI },
#endif
	{ BMAP_RFCNT,		PAGE_SIZE,		RW,	CI },
	{ BMAP_BMSEL,		PAGE_SIZE,		RW,	CI },
	{ BMAP_BMP,		BMAP_BMAP0 - BMAP_BMP,	RW,	CI, TRUE },
	{ BMAP_BMAP0,		BMAP_BMAP1 - BMAP_BMAP0, RW,	CI, TRUE },
	{ BMAP_BMAP1,		BMAP_BMAP2 - BMAP_BMAP1, RW,	CI, TRUE },
	{ BMAP_BMAP2,		BMAP_BMAP3 - BMAP_BMAP2, RW,	CI, TRUE },
	{ BMAP_BMAP3,		BMAP_BMAP4 - BMAP_BMAP3, RW,	CI, TRUE },
	{ BMAP_BMAP4,		BMAP_BMAP5 - BMAP_BMAP4, RW,	CI, TRUE },
	{ BMAP_BMAP5,		BMAP_BMAP6 - BMAP_BMAP5, RW,	CI, TRUE },
	{ BMAP_BMAP6,		BMAP_BMAP7 - BMAP_BMAP6, RW,	CI, TRUE },
	{ BMAP_BMAP7,		BMAP_FN - BMAP_BMAP7,	RW,	CI, TRUE },
	{ BMAP_FN,		PAGE_SIZE,		RW,	CI },
#if 0
	{ BMAP_FN0,		PAGE_SIZE,		RW,	CI },
	{ BMAP_FN1,		PAGE_SIZE,		RW,	CI },
	{ BMAP_FN2,		PAGE_SIZE,		RW,	CI },
	{ BMAP_FN3,		PAGE_SIZE,		RW,	CI },
	{ BMAP_FN4,		PAGE_SIZE,		RW,	CI },
	{ BMAP_FN5,		PAGE_SIZE,		RW,	CI },
	{ BMAP_FN6,		PAGE_SIZE,		RW,	CI },
	{ BMAP_FN7,		PAGE_SIZE,		RW,	CI },
	{ BMAP_PALLET0,		PAGE_SIZE,		RW,	CI },
	{ BMAP_PALLET1,		PAGE_SIZE,		RW,	CI },
#endif
	{ BMAP_PALLET2,		PAGE_SIZE,		RW,	CI },
#if 0
	{ BOARD_CHECK_REG,	PAGE_SIZE,		RW,	CI },
	{ BMAP_CRTC,		PAGE_SIZE,		RW,	CI },
#endif
	{ SCSI_ADDR,		PAGE_SIZE,		RW,	CI },
	{ LANCE_ADDR,		PAGE_SIZE,		RW,	CI },
	{ 0,			0xffffffff,		0,	0 },
};

const struct pmap_table *
pmap_table_build()
{
	return luna88k_board_table;
}
