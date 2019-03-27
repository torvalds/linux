/*-
 * Copyright 2000,2001,2002,2003 Broadcom Corporation.
 * All rights reserved.
 *
 * This file is derived from the sbmips32.h header distributed
 * by Broadcom with the CFE 1.4.2 sources.
 *
 * This software is furnished under license and may be used and
 * copied only in accordance with the following terms and
 * conditions.  Subject to these conditions, you may download,
 * copy, install, use, modify and distribute modified or unmodified
 * copies of this software in source and/or binary form.  No title
 * or ownership is transferred hereby.
 *
 * 1) Any source code used, modified or distributed must reproduce
 *    and retain this copyright notice and list of conditions
 *    as they appear in the source file.
 *
 * 2) No right is granted to use any trade name, trademark, or
 *    logo of Broadcom Corporation.  The "Broadcom Corporation"
 *    name may not be used to endorse or promote products derived
 *    from this software without the prior written permission of
 *    Broadcom Corporation.
 *
 * 3) THIS SOFTWARE IS PROVIDED "AS-IS" AND ANY EXPRESS OR
 *    IMPLIED WARRANTIES, INCLUDING BUT NOT LIMITED TO, ANY IMPLIED
 *    WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 *    PURPOSE, OR NON-INFRINGEMENT ARE DISCLAIMED. IN NO EVENT
 *    SHALL BROADCOM BE LIABLE FOR ANY DAMAGES WHATSOEVER, AND IN
 *    PARTICULAR, BROADCOM SHALL NOT BE LIABLE FOR DIRECT, INDIRECT,
 *    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *    GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *    BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 *    OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *    TORT (INCLUDING NEGLIGENCE OR OTHERWISE), EVEN IF ADVISED OF
 *    THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */


/*  *********************************************************************
    *  Broadcom Common Firmware Environment (CFE)
    *  
    *  MIPS32 CPU definitions			File: sbmips32.h
    * 
    *  This module contains constants and macros specific to the
    *  Broadcom MIPS32 core.  In addition to generic MIPS32, it
    *  includes definitions for the MIP32-01 and MIPS3302 OCP cores
    *  for the Silicon Backplane.
    *
    *********************************************************************/

#ifndef _MIPS_BROADCOM_BCM_BMIPS_EXTS_H_
#define _MIPS_BROADCOM_BCM_BMIPS_EXTS_H_

#include <machine/cpufunc.h>

/*
 * The following Broadcom Custom CP0 Registers appear in the Broadcom
 * BMIPS330x MIPS32 core.
 */

#define	BMIPS_COP_0_BCMCFG	22

/*
 * Custom CP0 Accessors
 */

#define	BCM_BMIPS_RW32_COP0_SEL(n,r,s)				\
static __inline uint32_t					\
bcm_bmips_rd_ ## n(void)					\
{								\
	int v0;							\
	__asm __volatile ("mfc0 %[v0], $"__XSTRING(r)", "__XSTRING(s)";"	\
			  : [v0] "=&r"(v0));			\
	mips_barrier();						\
	return (v0);						\
}								\
static __inline void						\
bcm_bmips_wr_ ## n(uint32_t a0)					\
{								\
	__asm __volatile ("mtc0 %[a0], $"__XSTRING(r)", "__XSTRING(s)";"	\
			 __XSTRING(COP0_SYNC)";"		\
			 "nop;"					\
			 "nop;"					\
			 :					\
			 : [a0] "r"(a0));			\
	mips_barrier();						\
} struct __hack

BCM_BMIPS_RW32_COP0_SEL(pllcfg1,	MIPS_COP_0_CONFIG,	1);
BCM_BMIPS_RW32_COP0_SEL(pllcfg2,	MIPS_COP_0_CONFIG,	2);
BCM_BMIPS_RW32_COP0_SEL(clksync,	MIPS_COP_0_CONFIG,	3);
BCM_BMIPS_RW32_COP0_SEL(pllcfg3,	MIPS_COP_0_CONFIG,	4);
BCM_BMIPS_RW32_COP0_SEL(rstcfg,		MIPS_COP_0_CONFIG,	5);

/*
 * Broadcom PLLConfig1 Register (22, select 1)
 */

/* SoftMIPSPLLCfg */
#define	BMIPS_BCMCFG_PLLCFG1_MC_SHIFT	10
#define	BMIPS_BCMCFG_PLLCFG1_MC_MASK	0xFFFFFC00

/* SoftISBPLLCfg */
#define	BMIPS_BCMCFG_PLLCFG1_BC_SHIFT	5
#define	BMIPS_BCMCFG_PLLCFG1_BC_MASK	0x000003E0

/* SoftRefPLLCfg */
#define	BMIPS_BCMCFG_PLLCFG1_PC_SHIFT	0
#define	BMIPS_BCMCFG_PLLCFG1_PC_MASK	0x0000001F

/*
 * Broadcom PLLConfig2 Register (22, select 2)
 */

/* Soft1to1ClkRatio */
#define	BMIPS_BCMCFG_PLLCFG2_CR		(1<<23)

/* SoftUSBxPLLCfg */
#define	BMIPS_BCMCFG_PLLCFG2_UC_SHIFT	15
#define	BMIPS_BCMCFG_PLLCFG2_UC_MASK	0x007F8000

/* SoftIDExPLLCfg */
#define	BMIPS_BCMCFG_PLLCFG2_IC_SHIFT	7
#define	BMIPS_BCMCFG_PLLCFG2_IC_MASK	0x00007F80

#define	BMIPS_BCMCFG_PLLCFG2_BE		(1<<6)	/* ISBxSoftCfgEnable */
#define	BMIPS_BCMCFG_PLLCFG2_UE		(1<<5)	/* USBxSoftCfgEnable */
#define	BMIPS_BCMCFG_PLLCFG2_IE		(1<<4)	/* IDExSoftCfgEnable */
#define	BMIPS_BCMCFG_PLLCFG2_CA		(1<<3)	/* CfgActive */
#define	BMIPS_BCMCFG_PLLCFG2_CF		(1<<2)	/* RefSoftCfgEnable */
#define	BMIPS_BCMCFG_PLLCFG2_CI		(1<<1)	/* ISBSoftCfgEnable */
#define	BMIPS_BCMCFG_PLLCFG2_CC		(1<<0)	/* MIPSSoftCfgEnable */

/*
 * Broadcom ClkSync Register (22, select 3)
 */
/* SoftClkCfgHigh */
#define	BMIPS_BCMCFG_CLKSYNC_CH_SHIFT	16
#define	BMIPS_BCMCFG_CLKSYNC_CH_MASK	0xFFFF0000

/* SoftClkCfgLow */
#define	BMIPS_BCMCFG_CLKSYNC_CL_SHIFT	0
#define	BMIPS_BCMCFG_CLKSYNC_CL_MASK	0x0000FFFF

/*
 * Broadcom ISBxPLLConfig3 Register (22, select 4)
 */

/* AsyncClkRatio */
#define	BMIPS_BCMCFG_PLLCFG3_AR_SHIFT	23
#define	BMIPS_BCMCFG_PLLCFG3_AR_MASK	0x01800000

#define	BMIPS_BCMCFG_PLLCFG3_SM		(1<<22)	/* SyncMode */

/* SoftISBxPLLCfg */
#define	BMIPS_BCMCFG_PLLCFG3_IC_SHIFT	0
#define	BMIPS_BCMCFG_PLLCFG3_IC_MASK	0x003FFFFF

/*
 * Broadcom BRCMRstConfig Register (22, select 5)
 */

#define	BMIPS_BCMCFG_RSTCFG_SR		(1<<18)	/* SSMR */
#define	BMIPS_BCMCFG_RSTCFG_DT		(1<<16)	/* BHTD */

/* RStSt */
#define	BMIPS_BCMCFG_RSTCFG_RS_SHIFT	8
#define	BMIPS_BCMCFG_RSTCFG_RS_MASK	0x00001F00
#define	  BMIPS_BCMCFG_RST_OTHER	0x00
#define	  BMIPS_BCMCFG_RST_SH		0x01
#define	  BMIPS_BCMCFG_RST_SS		0x02
#define	  BMIPS_BCMCFG_RST_EJTAG	0x04
#define	  BMIPS_BCMCFG_RST_WDOG		0x08
#define	  BMIPS_BCMCFG_RST_CRC		0x10

#define	BMIPS_BCMCFG_RSTCFG_CR		(1<<7)	/* RStCr */

/* WBMD */
#define	BMIPS_BCMCFG_RSTCFG_WD_SHIFT	3
#define	BMIPS_BCMCFG_RSTCFG_WD_MASK	0x00000078

#define	BMIPS_BCMCFG_RSTCFG_SS		(1<<2)	/* SSR */
#define	BMIPS_BCMCFG_RSTCFG_SH		(1<<1)	/* SHR */
#define	BMIPS_BCMCFG_RSTCFG_BR		(1<<0)	/* BdR */

#endif /* _MIPS_BROADCOM_BCM_BMIPS_EXTS_H_ */
