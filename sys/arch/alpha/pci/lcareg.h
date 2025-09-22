/*	$OpenBSD: lcareg.h,v 1.8 2013/06/04 19:12:34 miod Exp $	*/
/* $NetBSD: lcareg.h,v 1.7 1997/06/06 23:54:31 thorpej Exp $ */

/*
 * Copyright (c) 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Jeffrey Hsu, Jason R. Thorpe
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
 * 21066 chip registers
 */

#define REGVAL(r)	(*(volatile int32_t *)ALPHA_PHYS_TO_K0SEG(r))
#define REGVAL64(r)	(*(volatile int64_t *)ALPHA_PHYS_TO_K0SEG(r))

/*
 * Base addresses
 */
#define LCA_IOC_BASE	0x180000000L		/* LCA IOC Regs */
#define LCA_PCI_SIO	0x1c0000000L		/* PCI Sp. I/O Space */
#define LCA_PCI_CONF	0x1e0000000L		/* PCI Conf. Space */
#define LCA_PCI_SPARSE	0x200000000L		/* PCI Sparse Space */
#define LCA_PCI_DENSE	0x300000000L		/* PCI Dense Space */

#define LCA_IOC_HAE	LCA_IOC_BASE		/* Host Address Ext. (64) */
#define	IOC_HAE_ADDREXT	0x00000000f8000000UL
#define	IOC_HAE_RSVSD	0xffffffff07ffffffUL

#define LCA_IOC_CONF	(LCA_IOC_BASE + 0x020)	/* Configuration Cycle Type */

#define LCA_IOC_STAT0	(LCA_IOC_BASE + 0x040)	/* Status 0 */
#define	IOC_STAT0_CMD	0x000000000000000fUL	/* PCI command mask */
#define	IOC_STAT0_ERR	0x0000000000000010UL	/* IOC error indicator R/W1C */
#define	IOC_STAT0_LOST	0x0000000000000020UL	/* IOC lose error info R/W1C */
#define	IOC_STAT0_THIT	0x0000000000000040UL	/* test hit */
#define	IOC_STAT0_TREF	0x0000000000000080UL	/* test reference */
#define	IOC_STAT0_CODE	0x0000000000000700UL	/* code mask */
#define	IOC_STAT0_CODESHIFT 8
#define	IOC_STAT0_P_NBR	0x00000000ffffe000UL	/* page number mask */

#define LCA_IOC_STAT1	(LCA_IOC_BASE + 0x060)	/* Status 1 */
#define	IOC_STAT1_ADDR	0x00000000ffffffffUL	/* PCI address mask */

#define	LCA_IOC_TBIA	(LCA_IOC_BASE + 0x080)	/* TLB Invalidate All */
#define	LCA_IOC_TB_ENA	(LCA_IOC_BASE + 0x0a0)	/* TLB Enable */
#define	IOC_TB_ENA_TEN	0x0000000000000080UL

#define	LCA_IOC_PAR_DIS	(LCA_IOC_BASE + 0x0e0)	/* Parity Disable */
#define	IOC_PAR_DISABLE	0x0000000000000020UL

#define LCA_IOC_W_BASE0	(LCA_IOC_BASE + 0x100)	/* Window Base */
#define LCA_IOC_W_MASK0	(LCA_IOC_BASE + 0x140)	/* Window Mask */
#define LCA_IOC_W_T_BASE0 (LCA_IOC_BASE + 0x180) /* Translated Base */

#define LCA_IOC_W_BASE1	(LCA_IOC_BASE + 0x120)	/* Window Base */
#define LCA_IOC_W_MASK1	(LCA_IOC_BASE + 0x160)	/* Window Mask */
#define LCA_IOC_W_T_BASE1 (LCA_IOC_BASE + 0x1a0) /* Translated Base */

#define	IOC_W_BASE_W_BASE 0x00000000fff00000UL	/* Window base value */
#define	IOC_W_BASE_SG	  0x0000000100000000UL	/* Window uses SGMAPs */
#define	IOC_W_BASE_WEN	  0x0000000200000000UL	/* Window enable */

#define	IOC_W_MASK_1M	0x0000000000000000UL	/* 1MB window */
#define	IOC_W_MASK_2M	0x0000000000100000UL	/* 2MB window */
#define	IOC_W_MASK_4M	0x0000000000300000UL	/* 4MB window */
#define	IOC_W_MASK_8M	0x0000000000700000UL	/* 8MB window */
#define	IOC_W_MASK_16M	0x0000000000f00000UL	/* 16MB window */
#define	IOC_W_MASK_32M	0x0000000001f00000UL	/* 32MB window */
#define	IOC_W_MASK_64M	0x0000000003f00000UL	/* 64MB window */
#define	IOC_W_MASK_128M	0x0000000007f00000UL	/* 128M window */
#define	IOC_W_MASK_256M	0x000000000ff00000UL	/* 256M window */
#define	IOC_W_MASK_512M	0x000000001ff00000UL	/* 512M window */
#define	IOC_W_MASK_1G	0x000000003ff00000UL	/* 1GB window */
#define	IOC_W_MASK_2G	0x000000007ff00000UL	/* 2GB window */
#define	IOC_W_MASK_4G	0x00000000fff00000UL	/* 4GB window */

#define	IOC_W_T_BASE	0x00000000fffffc00UL	/* page table base */
