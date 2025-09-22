/*	$OpenBSD: board.h,v 1.15 2017/11/03 06:55:08 aoyama Exp $	*/
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
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

#ifndef _MACHINE_BOARD_H_
#define _MACHINE_BOARD_H_

/*
 *      OMRON SX9100DT CPU board constants
 */

/*
 * Something to put append a 'U' to a long constant if it's C so that
 * it'll be unsigned in both ANSI and traditional.
 */
#if defined(_LOCORE)
#define U(num)  num
#elif defined(__STDC__)
#define U(num)  num ## U
#else
#define U(num)  num/**/U
#endif

/* machtype values */
#define LUNA_88K	0x1
#define LUNA_88K2	0x2

#define MAXPHYSMEM	U(0x10000000) 	/* max physical memory */

#define PROM_ADDR	U(0x41000000) 	/* PROM */
#define PROM_SPACE	U(0x00040000) 
#define NVRAM_ADDR	U(0x45000000) 	/* Non Volatile */
#define NVRAM_SPACE	U(0x00001FDC) 
#define	FUSE_ROM_ADDR	U(0x43000000) 	/* FUSE_ROM */
#define	FUSE_ROM_SPACE	        1024
#define	OBIO_CAL_CTL	U(0x45001FE0) 	/* calendar control register */
#define OBIO_CAL_SEC	U(0x45001FE4) 	/* seconds */
#define OBIO_CAL_MIN	U(0x45001FE8) 	/* minutes */
#define OBIO_CAL_HOUR	U(0x45001FEC) 	/* hours */
#define OBIO_CAL_DOW	U(0x45001FF0) 	/* Day Of the Week */
#define OBIO_CAL_DAY	U(0x45001FF4) 	/* days */
#define OBIO_CAL_MON	U(0x45001FF8) 	/* months */
#define OBIO_CAL_YEAR	U(0x45001FFC) 	/* years */
#define NVRAM_ADDR_88K2	U(0x47000000)	/* Non Volatile RAM area for LUNA-88K2 */
#define OBIO_PIO0_BASE	U(0x49000000) 	/* PIO-0 */
#define OBIO_PIO0_SPACE	U(0x0000000C) 
#define OBIO_PIO0A	U(0x49000000) 	/* PIO-0 port A */
#define OBIO_PIO0B	U(0x49000004) 	/* PIO-0 port B */
#define OBIO_PIO0C	U(0x49000008) 	/* PIO-0 port C*/
#define OBIO_PIO0	U(0x4900000C) 	/* PIO-0 control */
#define OBIO_PIO1_BASE	U(0x4D000000) 	/* PIO-1 */
#define OBIO_PIO1_SPACE U(0x0000000C) 
#define OBIO_PIO1A	U(0x4D000000) 	/* PIO-1 port A */
#define OBIO_PIO1B	U(0x4D000004) 	/* PIO-1 port B */
#define OBIO_PIO1C	U(0x4D000008) 	/* PIO-1 port C*/
#define OBIO_PIO1	U(0x4D00000C) 	/* PIO-1 control */
#define OBIO_SIO	U(0x51000000) 	/* SIO */
#define	OBIO_TAS	U(0x61000000) 	/* TAS register */
#define OBIO_CLOCK0	U(0x63000000) 	/* system clock CPU 0 */
#define OBIO_CLOCK1	U(0x63000004) 	/* system clock CPU 1 */
#define OBIO_CLOCK2	U(0x63000008) 	/* system clock CPU 2 */
#define OBIO_CLOCK3	U(0x6300000C) 	/* system clock CPU 3 */
#define OBIO_CLK_INTR	          31	/* system clock interrupt flag */
#define INT_ST_MASK0	U(0x65000000) 	/* interrupt status register CPU 0 */
#define INT_ST_MASK1	U(0x65000004) 	/* interrupt status register CPU 1 */
#define INT_ST_MASK2	U(0x65000008) 	/* interrupt status register CPU 2 */
#define INT_ST_MASK3	U(0x6500000C) 	/* interrupt status register CPU 3 */
#define  INT_LEVEL	           8	/* # of interrupt level + 1 */
#define  INT_SET_LV7	U(0x00000000) 	/* disable interrupts */
#define  INT_SET_LV6	U(0x00000000) 	/* enable level 7 */
#define  INT_SET_LV5	U(0x84000000) 	/* enable level 7-6 */
#define  INT_SET_LV4	U(0xC4000000) 	/* enable level 7-5 */
#define  INT_SET_LV3	U(0xE4000000) 	/* enable level 7-4 */
#define  INT_SET_LV2	U(0xF4000000) 	/* enable level 7-3 */
#define  INT_SET_LV1	U(0xFC000000) 	/* enable level 7-2 */
#define  INT_SET_LV0	U(0xFC000000) 	/* enable interrupts */
#define  INT_SLAVE_MASK	U(0x84000000) 	/* slave can only enable 6 and 1 */

#define SOFT_INT0	U(0x69000000) 	/* software interrupt CPU 0 */
#define SOFT_INT1	U(0x69000004) 	/* software interrupt CPU 1 */
#define SOFT_INT2	U(0x69000008) 	/* software interrupt CPU 2 */
#define SOFT_INT3	U(0x6900000C)	/* software interrupt CPU 3 */
#define SOFT_INT_FLAG0	U(0x6B000000) 	/* software interrupt flag CPU 0 */
#define SOFT_INT_FLAG1	U(0x6B000000) 	/* software interrupt flag CPU 1 */
#define SOFT_INT_FLAG2	U(0x6B000000) 	/* software interrupt flag CPU 2 */
#define SOFT_INT_FLAG3	U(0x6B000000) 	/* software interrupt flag CPU 3  */
#define RESET_CPU0	U(0x6D000000) 	/* reset CPU 0 */
#define RESET_CPU1	U(0x6D000004) 	/* reset CPU 1 */
#define RESET_CPU2	U(0x6D000008)	/* reset CPU 2 */
#define RESET_CPU3	U(0x6D00000C) 	/* reset CPU 3 */
#define RESET_CPU_ALL	U(0x6D000010) 	/* reset ALL CPUs */
#define TRI_PORT_RAM	U(0x71000000) 	/* 3 port RAM */
#define TRI_PORT_RAM_SPACE	0x20000
#define EXT_A_ADDR	U(0x81000000) 	/* extension board A */
#define EXT_A_SPACE	U(0x02000000) 
#define EXT_B_ADDR	U(0x83000000) 	/* extension board B */
#define EXT_B_SPACE	U(0x01000000) 
#define	PC_BASE		U(0x90000000) 	/* pc-98 extension board */
#define	PC_SPACE	U(0x02000000) 

#define MROM_ADDR	U(0xA1000000) 	/* Mask ROM address */
#define MROM_SPACE		0x400000
#define	BMAP_START	U(0xB1000000) 	/* Bitmap start address */
#define	BMAP_SPACE	(BMAP_END - BMAP_START)
#define BMAP_RFCNT	U(0xB1000000) 	/* RFCNT register */
#define BMAP_BMSEL	U(0xB1040000) 	/* BMSEL register */
#define BMAP_BMP	U(0xB1080000) 	/* common bitmap plane */
#define BMAP_BMAP0	U(0xB10C0000) 	/* bitmap plane 0 */
#define BMAP_BMAP1	U(0xB1100000) 	/* bitmap plane 1 */
#define BMAP_BMAP2	U(0xB1140000) 	/* bitmap plane 2 */
#define BMAP_BMAP3	U(0xB1180000) 	/* bitmap plane 3 */
#define BMAP_BMAP4	U(0xB11C0000) 	/* bitmap plane 4 */
#define BMAP_BMAP5	U(0xB1200000) 	/* bitmap plane 5 */
#define BMAP_BMAP6	U(0xB1240000) 	/* bitmap plane 6 */
#define BMAP_BMAP7	U(0xB1280000) 	/* bitmap plane 7 */
#define BMAP_FN		U(0xB12C0000) 	/* common bitmap function */
#define BMAP_FN0	U(0xB1300000) 	/* bitmap function 0 */
#define BMAP_FN1	U(0xB1340000) 	/* bitmap function 1 */
#define BMAP_FN2	U(0xB1380000) 	/* bitmap function 2 */
#define BMAP_FN3	U(0xB13C0000) 	/* bitmap function 3 */
#define BMAP_FN4	U(0xB1400000) 	/* bitmap function 4 */
#define BMAP_FN5	U(0xB1440000) 	/* bitmap function 5 */
#define BMAP_FN6	U(0xB1480000) 	/* bitmap function 6 */
#define BMAP_FN7	U(0xB14C0000) 	/* bitmap function 7 */
#define BMAP_END	U(0xB1500000) 
#define BMAP_END24P	U(0xB1800000)	/* end of 24p framemem */
#define BMAP_PALLET0	U(0xC0000000) 	/* color pallet */
#define BMAP_PALLET1	U(0xC1000000) 	/* color pallet */
#define BMAP_PALLET2	U(0xC1100000) 	/* color pallet */
#define BOARD_CHECK_REG	U(0xD0000000) 	/* board check register */
#define BMAP_CRTC	U(0xD1000000) 	/* CRTC-II */
#define BMAP_IDENTROM	U(0xD1800000)	/* bitmap-board identify ROM */
#define SCSI_ADDR	U(0xE1000000) 	/* SCSI address */
#define LANCE_ADDR	U(0xF1000000) 	/* LANCE */

#define CMMU_I0		U(0xFFF07000) 	/* CMMU instruction cpu 0 */
#define CMMU_D0		U(0xFFF06000) 	/* CMMU data cpu 0 */
#define CMMU_I1		U(0xFFF05000) 	/* CMMU instruction cpu 1 */
#define CMMU_D1		U(0xFFF04000) 	/* CMMU data cpu 1 */
#define CMMU_I2		U(0xFFF03000) 	/* CMMU instruction cpu 2 */
#define CMMU_D2		U(0xFFF02000) 	/* CMMU data cpu 2 */
#define CMMU_I3		U(0xFFF01000) 	/* CMMU instruction cpu 3 */
#define CMMU_D3		U(0xFFF00000) 	/* CMMU data cpu 3 */

#endif /* _MACHINE_BOARD_H_ */
