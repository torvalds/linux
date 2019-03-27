/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2008 Semihalf, Rafal Jaworowski
 * Copyright 2006 by Juniper Networks.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MPC85XX_H_
#define _MPC85XX_H_

#include <machine/platformvar.h>

/*
 * Configuration control and status registers
 */
extern vm_offset_t		ccsrbar_va;
extern vm_paddr_t		ccsrbar_pa;
extern vm_size_t		ccsrbar_size;
#define CCSRBAR_VA		ccsrbar_va
#define	OCP85XX_CCSRBAR		(CCSRBAR_VA + 0x0)
#define	OCP85XX_BPTR		(CCSRBAR_VA + 0x20)

#define	OCP85XX_BSTRH		(CCSRBAR_VA + 0x20)
#define	OCP85XX_BSTRL		(CCSRBAR_VA + 0x24)
#define	OCP85XX_BSTAR		(CCSRBAR_VA + 0x28)

#define	OCP85XX_COREDISR	(CCSRBAR_VA + 0xE0094)
#define	OCP85XX_BRR		(CCSRBAR_VA + 0xE00E4)

/*
 * Run Control and Power Management registers
 */
#define CCSR_CTBENR		(CCSRBAR_VA + 0xE2084)
#define CCSR_CTBCKSELR		(CCSRBAR_VA + 0xE208C)
#define CCSR_CTBCHLTCR		(CCSRBAR_VA + 0xE2094)

/*
 * DDR Memory controller.
 */
#define	OCP85XX_DDR1_CS0_CONFIG		(CCSRBAR_VA + 0x8080)

/*
 * E500 Coherency Module registers
 */
#define	OCP85XX_EEBPCR		(CCSRBAR_VA + 0x1010)

/*
 * Local access registers
 */
/* Write order: OCP_LAWBARH -> OCP_LAWBARL -> OCP_LAWSR */
#define	OCP85XX_LAWBARH(n)	(CCSRBAR_VA + 0xc00 + 0x10 * (n))
#define	OCP85XX_LAWBARL(n)	(CCSRBAR_VA + 0xc04 + 0x10 * (n))
#define	OCP85XX_LAWSR_QORIQ(n)	(CCSRBAR_VA + 0xc08 + 0x10 * (n))
#define	OCP85XX_LAWBAR(n)	(CCSRBAR_VA + 0xc08 + 0x10 * (n))
#define	OCP85XX_LAWSR_85XX(n)	(CCSRBAR_VA + 0xc10 + 0x10 * (n))
#define	OCP85XX_LAWSR(n)	(mpc85xx_is_qoriq() ? OCP85XX_LAWSR_QORIQ(n) : \
				 OCP85XX_LAWSR_85XX(n))

/* Attribute register */
#define	OCP85XX_ENA_MASK	0x80000000
#define	OCP85XX_DIS_MASK	0x7fffffff

#define	OCP85XX_TGTIF_LBC_QORIQ	0x1f
#define	OCP85XX_TGTIF_RAM_INTL_QORIQ	0x14
#define	OCP85XX_TGTIF_RAM1_QORIQ	0x10
#define	OCP85XX_TGTIF_RAM2_QORIQ	0x11
#define	OCP85XX_TGTIF_BMAN		0x18
#define	OCP85XX_TGTIF_DCSR		0x1D
#define	OCP85XX_TGTIF_QMAN		0x3C
#define	OCP85XX_TRGT_SHIFT_QORIQ	20

#define	OCP85XX_TGTIF_LBC_85XX	0x04
#define	OCP85XX_TGTIF_RAM_INTL_85XX	0x0b
#define	OCP85XX_TGTIF_RIO_85XX	0x0c
#define	OCP85XX_TGTIF_RAM1_85XX	0x0f
#define	OCP85XX_TGTIF_RAM2_85XX	0x16

#define	OCP85XX_TGTIF_LBC	\
    (mpc85xx_is_qoriq() ? OCP85XX_TGTIF_LBC_QORIQ : OCP85XX_TGTIF_LBC_85XX)
#define	OCP85XX_TGTIF_RAM_INTL	\
     (mpc85xx_is_qoriq() ? OCP85XX_TGTIF_RAM_INTL_QORIQ : OCP85XX_TGTIF_RAM_INTL_85XX)
#define	OCP85XX_TGTIF_RIO	\
      (mpc85xx_is_qoriq() ? OCP85XX_TGTIF_RIO_QORIQ : OCP85XX_TGTIF_RIO_85XX)
#define	OCP85XX_TGTIF_RAM1	\
       (mpc85xx_is_qoriq() ? OCP85XX_TGTIF_RAM1_QORIQ : OCP85XX_TGTIF_RAM1_85XX)
#define	OCP85XX_TGTIF_RAM2	\
	(mpc85xx_is_qoriq() ? OCP85XX_TGTIF_RAM2_QORIQ : OCP85XX_TGTIF_RAM2_85XX)

/*
 * L2 cache registers
 */
#define OCP85XX_L2CTL		(CCSRBAR_VA + 0x20000)

/*
 * L3 CoreNet platform cache (CPC) registers
 */
#define	OCP85XX_CPC_CSR0		(CCSRBAR_VA + 0x10000)
#define	  OCP85XX_CPC_CSR0_CE		  0x80000000
#define	  OCP85XX_CPC_CSR0_PE		  0x40000000
#define	  OCP85XX_CPC_CSR0_FI		  0x00200000
#define	  OCP85XX_CPC_CSR0_WT		  0x00080000
#define	  OCP85XX_CPC_CSR0_FL		  0x00000800
#define	  OCP85XX_CPC_CSR0_LFC		  0x00000400
#define	OCP85XX_CPC_CFG0		(CCSRBAR_VA + 0x10008)
#define	  OCP85XX_CPC_CFG_SZ_MASK	  0x00003fff
#define	  OCP85XX_CPC_CFG0_SZ_K(x)	  (((x) & OCP85XX_CPC_CFG_SZ_MASK) << 6)

/*
 * Power-On Reset configuration
 */
#define	OCP85XX_PORDEVSR	(CCSRBAR_VA + 0xe000c)
#define OCP85XX_PORDEVSR_IO_SEL	0x00780000
#define OCP85XX_PORDEVSR_IO_SEL_SHIFT 19

#define	OCP85XX_PORDEVSR2	(CCSRBAR_VA + 0xe0014)

/*
 * Status Registers.
 */
#define	OCP85XX_RSTCR		(CCSRBAR_VA + 0xe00b0)

#define	OCP85XX_CLKDVDR		(CCSRBAR_VA + 0xe0800)
#define	  OCP85XX_CLKDVDR_PXCKEN	  0x80000000
#define	  OCP85XX_CLKDVDR_SSICKEN	  0x20000000
#define	  OCP85XX_CLKDVDR_PXCKINV	  0x10000000
#define	  OCP85XX_CLKDVDR_PXCLK_MASK	  0x00FF0000
#define	  OCP85XX_CLKDVDR_SSICLK_MASK	  0x000000FF

/*
 * Run Control/Power Management Registers.
 */
#define	OCP85XX_RCPM_CDOZSR	(CCSRBAR_VA + 0xe2004)
#define	OCP85XX_RCPM_CDOZCR	(CCSRBAR_VA + 0xe200c)

/*
 * Prototypes.
 */
uint32_t ccsr_read4(uintptr_t addr);
void ccsr_write4(uintptr_t addr, uint32_t val);
int law_enable(int trgt, uint64_t bar, uint32_t size);
int law_disable(int trgt, uint64_t bar, uint32_t size);
int law_getmax(void);
int law_pci_target(struct resource *, int *, int *);

DECLARE_CLASS(mpc85xx_platform);
int mpc85xx_attach(platform_t);

void mpc85xx_enable_l3_cache(void);
int mpc85xx_is_qoriq(void);
uint32_t mpc85xx_get_platform_clock(void);
uint32_t mpc85xx_get_system_clock(void);

#endif /* _MPC85XX_H_ */
