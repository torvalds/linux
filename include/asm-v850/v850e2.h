/*
 * include/asm-v850/v850e2.h -- Machine-dependent defs for V850E2 CPUs
 *
 *  Copyright (C) 2002,03  NEC Electronics Corporation
 *  Copyright (C) 2002,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_V850E2_H__
#define __V850_V850E2_H__

#include <asm/v850e_intc.h>	/* v850e-style interrupt system.  */


#define CPU_ARCH "v850e2"


/* Control registers.  */

/* Chip area select control */ 
#define V850E2_CSC_ADDR(n)	(0xFFFFF060 + (n) * 2)
#define V850E2_CSC(n)		(*(volatile u16 *)V850E2_CSC_ADDR(n))
/* I/O area select control */
#define V850E2_BPC_ADDR		0xFFFFF064
#define V850E2_BPC		(*(volatile u16 *)V850E2_BPC_ADDR)
/* Bus size configuration */
#define V850E2_BSC_ADDR		0xFFFFF066
#define V850E2_BSC		(*(volatile u16 *)V850E2_BSC_ADDR)
/* Endian configuration */
#define V850E2_BEC_ADDR		0xFFFFF068
#define V850E2_BEC		(*(volatile u16 *)V850E2_BEC_ADDR)
/* Cache configuration */
#define V850E2_BHC_ADDR		0xFFFFF06A
#define V850E2_BHC		(*(volatile u16 *)V850E2_BHC_ADDR)
/* NPB strobe-wait configuration */
#define V850E2_VSWC_ADDR	0xFFFFF06E
#define V850E2_VSWC		(*(volatile u16 *)V850E2_VSWC_ADDR)
/* Bus cycle type */
#define V850E2_BCT_ADDR(n)	(0xFFFFF480 + (n) * 2)
#define V850E2_BCT(n)		(*(volatile u16 *)V850E2_BCT_ADDR(n))
/* Data wait control */
#define V850E2_DWC_ADDR(n)	(0xFFFFF484 + (n) * 2)
#define V850E2_DWC(n)		(*(volatile u16 *)V850E2_DWC_ADDR(n))
/* Bus cycle control */
#define V850E2_BCC_ADDR		0xFFFFF488
#define V850E2_BCC		(*(volatile u16 *)V850E2_BCC_ADDR)
/* Address wait control */
#define V850E2_ASC_ADDR		0xFFFFF48A
#define V850E2_ASC		(*(volatile u16 *)V850E2_ASC_ADDR)
/* Local bus sizing control */
#define V850E2_LBS_ADDR		0xFFFFF48E
#define V850E2_LBS		(*(volatile u16 *)V850E2_LBS_ADDR)
/* Line buffer control */
#define V850E2_LBC_ADDR(n)	(0xFFFFF490 + (n) * 2)
#define V850E2_LBC(n)		(*(volatile u16 *)V850E2_LBC_ADDR(n))
/* SDRAM configuration */
#define V850E2_SCR_ADDR(n)	(0xFFFFF4A0 + (n) * 4)
#define V850E2_SCR(n)		(*(volatile u16 *)V850E2_SCR_ADDR(n))
/* SDRAM refresh cycle control */
#define V850E2_RFS_ADDR(n)	(0xFFFFF4A2 + (n) * 4)
#define V850E2_RFS(n)		(*(volatile u16 *)V850E2_RFS_ADDR(n))


#endif /* __V850_V850E2_H__ */
