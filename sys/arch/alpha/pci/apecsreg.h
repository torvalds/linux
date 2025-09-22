/* $OpenBSD: apecsreg.h,v 1.6 2001/02/16 08:23:39 jason Exp $ */
/* $NetBSD: apecsreg.h,v 1.5.2.2 1997/06/06 20:26:53 thorpej Exp $ */

/*
 * Copyright (c) 1995 Carnegie-Mellon University.
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
 * APECS Chipset registers and constants.
 *
 * Taken from ``DECchip 21071 and DECchip 21072 Core Logic Chipsets Data
 * Sheet'' (DEC order number EC-QAEMA-TE), pages 4-1 - 4-27, 10-21 - 10-38.
 */

#define	REGVAL(r)	(*(volatile int32_t *)ALPHA_PHYS_TO_K0SEG(r))

/*
 * Base addresses
 */
#define	COMANCHE_BASE	 0x180000000L			/* 21071-CA Regs */
#define	EPIC_BASE	 0x1a0000000L			/* 21071-DA Regs */
#define	APECS_PCI_IACK	 0x1b0000000L			/* PCI Int. Ack. */
#define	APECS_PCI_SIO	 0x1c0000000L			/* PCI Sp. I/O Space */
#define	APECS_PCI_CONF	 0x1e0000000L			/* PCI Conf. Space */
#define	APECS_PCI_SPARSE 0x200000000L			/* PCI Sparse Space */
#define	APECS_PCI_DENSE	 0x300000000L			/* PCI Dense Space */


/*
 * 21071-CA Registers
 */

/*
 * 21071-CA General Registers
 */
#define	COMANCHE_GCR	(COMANCHE_BASE + 0x0000)	/* General Control */
#define		COMANCHE_GCR_RSVD	0xc009
#define		COMANCHE_GCR_SYSARB	0x0006
#define		COMANCHE_GCR_WIDEMEM	0x0010
#define		COMANCHE_GCR_BC_EN	0x0020
#define		COMANCHE_GCR_BC_NOALLOC	0x0040
#define		COMANCHE_GCR_BC_LONGWR	0x0080
#define		COMANCHE_GCR_BC_IGNTAG	0x0100
#define		COMANCHE_GCR_BC_FRCTAG	0x0200
#define		COMANCHE_GCR_BC_FRCD	0x0400
#define		COMANCHE_GCR_BC_FRCV	0x0800
#define		COMANCHE_GCR_BC_FRCP	0x1000
#define		COMANCHE_GCR_BC_BADAP	0x2000

#define	COMANCHE_RSVD	(COMANCHE_BASE + 0x0020)	/* Reserved */

#define	COMANCHE_ED	(COMANCHE_BASE + 0x0040)	/* Err & Diag Status */
#define		COMANCHE_ED_LOSTERR	0x0001
#define		COMANCHE_ED_BC_TAPERR	0x0002
#define		COMANCHE_ED_BC_TCPERR	0x0004
#define		COMANCHE_ED_NXMERR	0x0008
#define		COMANCHE_ED_DMACAUSE	0x0010
#define		COMANCHE_ED_VICCAUSE	0x0020
#define		COMANCHE_ED_CREQCAUSE	0x01c0
#define		COMANCHE_ED_RSVD	0x1e00
#define		COMANCHE_ED_PASS2	0x2000
#define		COMANCHE_ED_IDXLLOCK	0x4000
#define		COMANCHE_ED_WRPEND	0x8000

#define	COMANCHE_TAGENB	(COMANCHE_BASE + 0x0060)	/* Tag Enable */
#define		COMANCHE_TAGENB_RSVD	0x0001

#define		COMANCHE_TAGENB_C_4G	0x0000
#define		COMANCHE_TAGENB_C_2G	0x8000
#define		COMANCHE_TAGENB_C_1G	0xc000
#define		COMANCHE_TAGENB_C_512M	0xe000
#define		COMANCHE_TAGENB_C_256M	0xf000
#define		COMANCHE_TAGENB_C_128M	0xf800
#define		COMANCHE_TAGENB_C_64M	0xfc00
#define		COMANCHE_TAGENB_C_32M	0xfe00
#define		COMANCHE_TAGENB_C_16M	0xff00
#define		COMANCHE_TAGENB_C_8M	0xff80
#define		COMANCHE_TAGENB_C_4M	0xffc0
#define		COMANCHE_TAGENB_C_2M	0xffe0
#define		COMANCHE_TAGENB_C_1M	0xfff0
#define		COMANCHE_TAGENB_C_512K	0xfff8
#define		COMANCHE_TAGENB_C_256K	0xfffc
#define		COMANCHE_TAGENB_C_128K	0xfffe

#define		COMANCHE_TAGENB_M_4G	0xffff
#define		COMANCHE_TAGENB_M_2G	0x7fff
#define		COMANCHE_TAGENB_M_1G	0x3fff
#define		COMANCHE_TAGENB_M_512M	0x1fff
#define		COMANCHE_TAGENB_M_256M	0x0fff
#define		COMANCHE_TAGENB_M_128M	0x07ff
#define		COMANCHE_TAGENB_M_64M	0x03ff
#define		COMANCHE_TAGENB_M_32M	0x01ff
#define		COMANCHE_TAGENB_M_16M	0x00ff
#define		COMANCHE_TAGENB_M_8M	0x007f
#define		COMANCHE_TAGENB_M_4M	0x003f
#define		COMANCHE_TAGENB_M_2M	0x001f
#define		COMANCHE_TAGENB_M_1M	0x000e
#define		COMANCHE_TAGENB_M_512K	0x0006
#define		COMANCHE_TAGENB_M_256K	0x0002
#define		COMANCHE_TAGENB_M_128K	0x0000

#define	COMANCHE_ERR_LO	(COMANCHE_BASE + 0x0080)	/* Error Low Address */

#define	COMANCHE_ERR_HI	(COMANCHE_BASE + 0x00a0)	/* Error High Address */
#define		COMANCHE_ERR_HI_RSVD	0xe000

#define	COMANCHE_LCK_LO	(COMANCHE_BASE + 0x00c0)	/* LDx_L Low Address */

#define	COMANCHE_LCK_HI	(COMANCHE_BASE + 0x00e0)	/* LDx_L High Address */
#define		COMANCHE_LOCK_HI_RSVD	0xe000

/*
 * 21071-CA Memory Registers
 */
#define	COMANCHE_GTIM	(COMANCHE_BASE + 0x0200)	 /* Global Timing */
#define		COMANCHE_LOCK_HI_RSVD	0xe000

#define	COMANCHE_RTIM	(COMANCHE_BASE + 0x0220)	 /* Refresh Timing */

#define	COMANCHE_VFP	(COMANCHE_BASE + 0x0240)	 /* Video Frame Ptr. */
#define		COMANCHE_VFP_COL	0x001f
#define		COMANCHE_VFP_ROW	0x3fe0
#define		COMANCHE_VFP_SUBBANK	0x4000
#define		COMANCHE_VFP_RSVD	0x8000

#define	COMANCHE_PD_LO	(COMANCHE_BASE + 0x0260)	/* Pres Detect Low */

#define	COMANCHE_PD_HI	(COMANCHE_BASE + 0x0280)	/* Pres Detect High */

/*
 * 21071-CA Memory banks' Base Address Register format
 */
#define	COMANCHE_B0_BAR	(COMANCHE_BASE + 0x0800)	/* Bank 0 BA */
#define	COMANCHE_B1_BAR	(COMANCHE_BASE + 0x0820)	/* Bank 1 BA */
#define	COMANCHE_B2_BAR	(COMANCHE_BASE + 0x0840)	/* Bank 2 BA */
#define	COMANCHE_B3_BAR	(COMANCHE_BASE + 0x0860)	/* Bank 3 BA */
#define	COMANCHE_B4_BAR	(COMANCHE_BASE + 0x0880)	/* Bank 4 BA */
#define	COMANCHE_B5_BAR	(COMANCHE_BASE + 0x08a0)	/* Bank 5 BA */
#define	COMANCHE_B6_BAR	(COMANCHE_BASE + 0x08c0)	/* Bank 6 BA */
#define	COMANCHE_B7_BAR	(COMANCHE_BASE + 0x08e0)	/* Bank 7 BA */
#define	COMANCHE_B8_BAR	(COMANCHE_BASE + 0x0900)	/* Bank 8 BA */
#define		COMANCHE_BAR_RSVD	0x001f

/*
 * 21071-CA Memory banks' Configuration Register format
 */
#define	COMANCHE_B0_CR	(COMANCHE_BASE + 0x0a00)	/* Bank 0 Config */
#define	COMANCHE_B1_CR	(COMANCHE_BASE + 0x0a20)	/* Bank 1 Config */
#define	COMANCHE_B2_CR	(COMANCHE_BASE + 0x0a40)	/* Bank 2 Config */
#define	COMANCHE_B3_CR	(COMANCHE_BASE + 0x0a60)	/* Bank 3 Config */
#define	COMANCHE_B4_CR	(COMANCHE_BASE + 0x0a80)	/* Bank 4 Config */
#define	COMANCHE_B5_CR	(COMANCHE_BASE + 0x0aa0)	/* Bank 5 Config */
#define	COMANCHE_B6_CR	(COMANCHE_BASE + 0x0ac0)	/* Bank 6 Config */
#define	COMANCHE_B7_CR	(COMANCHE_BASE + 0x0ae0)	/* Bank 7 Config */
#define	COMANCHE_B8_CR	(COMANCHE_BASE + 0x0b00)	/* Bank 8 Config */
#define		COMANCHE_CR_VALID	0x0001
#define		COMANCHE_CR_SIZE	0x001e
#define		COMANCHE_CR_SUBENA	0x0020
#define		COMANCHE_CR_COLSEL	0x01c0
#define		COMANCHE_CR_S0_RSVD	0xfe00
#define		COMANCHE_CR_S8_CHECK	0x0200
#define		COMANCHE_CR_S8_RSVD	0xfc00

/*
 * 21071-CA Memory banks' Timing Register A format
 */
#define	COMANCHE_B0_TRA	(COMANCHE_BASE + 0x0c00)	/* Bank 0 Timing A */
#define	COMANCHE_B1_TRA	(COMANCHE_BASE + 0x0c20)	/* Bank 1 Timing A */
#define	COMANCHE_B2_TRA	(COMANCHE_BASE + 0x0c40)	/* Bank 2 Timing A */
#define	COMANCHE_B3_TRA	(COMANCHE_BASE + 0x0c60)	/* Bank 3 Timing A */
#define	COMANCHE_B4_TRA	(COMANCHE_BASE + 0x0c80)	/* Bank 4 Timing A */
#define	COMANCHE_B5_TRA	(COMANCHE_BASE + 0x0ca0)	/* Bank 5 Timing A */
#define	COMANCHE_B6_TRA	(COMANCHE_BASE + 0x0cc0)	/* Bank 6 Timing A */
#define	COMANCHE_B7_TRA	(COMANCHE_BASE + 0x0ce0)	/* Bank 7 Timing A */
#define	COMANCHE_B8_TRA	(COMANCHE_BASE + 0x0d00)	/* Bank 8 Timing A */
#define		COMANCHE_TRA_ROWSETUP	0x0003
#define		COMANCHE_TRA_ROWHOLD	0x000c
#define		COMANCHE_TRA_COLSETUP	0x0070
#define		COMANCHE_TRA_COLHOLD	0x0180
#define		COMANCHE_TRA_RDLYROW	0x0e00
#define		COMANCHE_TRA_RDLYCOL	0x7000
#define		COMANCHE_TRA_RSVD	0x8000

/*
 * 21071-CA Memory banks' Timing Register B format
 */
#define	COMANCHE_B0_TRB	(COMANCHE_BASE + 0x0e00)	/* Bank 0 Timing B */
#define	COMANCHE_B1_TRB	(COMANCHE_BASE + 0x0e20)	/* Bank 1 Timing B */
#define	COMANCHE_B2_TRB	(COMANCHE_BASE + 0x0e40)	/* Bank 2 Timing B */
#define	COMANCHE_B3_TRB	(COMANCHE_BASE + 0x0e60)	/* Bank 3 Timing B */
#define	COMANCHE_B4_TRB	(COMANCHE_BASE + 0x0e80)	/* Bank 4 Timing B */
#define	COMANCHE_B5_TRB	(COMANCHE_BASE + 0x0ea0)	/* Bank 5 Timing B */
#define	COMANCHE_B6_TRB	(COMANCHE_BASE + 0x0ec0)	/* Bank 6 Timing B */
#define	COMANCHE_B7_TRB	(COMANCHE_BASE + 0x0ee0)	/* Bank 7 Timing B */
#define	COMANCHE_B8_TRB	(COMANCHE_BASE + 0x0f00)	/* Bank 8 Timing B */
#define		COMANCHE_TRB_RTCAS	0x0007
#define		COMANCHE_TRB_WTCAS	0x0038
#define		COMANCHE_TRB_TCP	0x00c0
#define		COMANCHE_TRB_WHOLD0ROW	0x0700
#define		COMANCHE_TRB_WHOLD0COL	0x3800
#define		COMANCHE_TRB_RSVD	0xc000


/*
 * 21071-DA Registers
 */
#define	EPIC_DCSR	(EPIC_BASE + 0x0000)		/* Diagnostic CSR */
#define		EPIC_DCSR_TENB		0x00000001
#define		EPIC_DCSR_RSVD		0x7fc00082
#define		EPIC_DCSR_PENB		0x00000004
#define		EPIC_DCSR_DCEI		0x00000008
#define		EPIC_DCSR_DPEC		0x00000010
#define		EPIC_DCSR_IORT		0x00000020
#define		EPIC_DCSR_LOST		0x00000040
#define		EPIC_DCSR_DDPE		0x00000100
#define		EPIC_DCSR_IOPE		0x00000200
#define		EPIC_DCSR_TABT		0x00000400
#define		EPIC_DCSR_NDEV		0x00000800
#define		EPIC_DCSR_CMRD		0x00001000
#define		EPIC_DCSR_UMRD		0x00002000
#define		EPIC_DCSR_IPTL		0x00004000
#define		EPIC_DCSR_MERR		0x00008000
#define		EPIC_DCSR_DBYP		0x00030000
#define		EPIC_DCSR_PCMD		0x003c0000
#define		EPIC_DCSR_PASS2		0x80000000

#define	EPIC_PEAR	(EPIC_BASE + 0x0020)		/* PCI Err Addr. */

#define	EPIC_SEAR	(EPIC_BASE + 0x0040)		/* sysBus Err Addr. */
#define		EPIC_SEAR_RSVD		0x0000000f
#define		EPIC_SEAR_SYS_ERR	0xfffffff0

#define	EPIC_DUMMY_1	(EPIC_BASE + 0x0060)		/* Dummy 1 */
#define	EPIC_DUMMY_2	(EPIC_BASE + 0x0080)		/* Dummy 2 */
#define	EPIC_DUMMY_3	(EPIC_BASE + 0x00a0)		/* Dummy 3 */

#define	EPIC_TBASE_1	(EPIC_BASE + 0x00c0)		/* Trans. Base 1 */
#define	EPIC_TBASE_2	(EPIC_BASE + 0x00e0)		/* Trans. Base 2 */
#define		EPIC_TBASE_RSVD		0x000001ff
#define		EPIC_TBASE_T_BASE	0xfffffe00
#define		EPIC_TBASE_SHIFT	1

#define	EPIC_PCI_BASE_1	(EPIC_BASE + 0x0100)		/* PCI Base 1 */
#define	EPIC_PCI_BASE_2	(EPIC_BASE + 0x0120)		/* PCI Base 2 */
#define		EPIC_PCI_BASE_RSVD	0x0003ffff
#define		EPIC_PCI_BASE_SGEN	0x00040000
#define		EPIC_PCI_BASE_WENB	0x00080000
#define		EPIC_PCI_BASE_PCI_BASE	0xfff00000

#define	EPIC_PCI_MASK_1	(EPIC_BASE + 0x0140)		/* PCI Mask 1 */
#define	EPIC_PCI_MASK_2	(EPIC_BASE + 0x0160)		/* PCI Mask 2 */
#define		EPIC_PCI_MASK_RSVD	0x000fffff
#define		EPIC_PCI_MASK_PCI_MASK	0xfff00000
#define		EPIC_PCI_MASK_1M	0x00000000
#define		EPIC_PCI_MASK_2M	0x00100000
#define		EPIC_PCI_MASK_4M	0x00300000
#define		EPIC_PCI_MASK_8M	0x00700000
#define		EPIC_PCI_MASK_16M	0x00f00000
#define		EPIC_PCI_MASK_32M	0x01f00000
#define		EPIC_PCI_MASK_64M	0x03f00000
#define		EPIC_PCI_MASK_128M	0x07f00000
#define		EPIC_PCI_MASK_256M	0x0ff00000
#define		EPIC_PCI_MASK_512M	0x1ff00000
#define		EPIC_PCI_MASK_1G	0x3ff00000
#define		EPIC_PCI_MASK_2G	0x7ff00000
#define		EPIC_PCI_MASK_4G	0xfff00000

#define	EPIC_HAXR0	(EPIC_BASE + 0x0180)		/* Host Addr Extn 0 */

#define	EPIC_HAXR1	(EPIC_BASE + 0x01a0)		/* Host Addr Extn 1 */
#define		EPIC_HAXR1_RSVD		0x07ffffff
#define		EPIC_HAXR1_EADDR	0xf8000000

#define	EPIC_HAXR2	(EPIC_BASE + 0x01c0)		/* Host Addr Extn 2 */
#define		EPIC_HAXR2_CONF_TYPE	0x00000003
#define		EPIC_HAXR2_CONF_TYPO0	0x00000000
#define		EPIC_HAXR2_CONF_TYPE1	0x00000001
#define		EPIC_HAXR2_RSVD		0x00fffffc
#define		EPIC_HAXR2_EADDR	0xff000000

#define	EPIC_PMLT	(EPIC_BASE + 0x01e0)		/* PCI Mstr Lat Tmr */
#define		EPIC_PMLT_PMLC		0x000000ff
#define		EPIC_PMLT_RSVD		0xffffff00

#define	EPIC_TLB_TAG_0	(EPIC_BASE + 0x0200)		/* TLB Tag 0 */
#define	EPIC_TLB_TAG_1	(EPIC_BASE + 0x0220)		/* TLB Tag 1 */
#define	EPIC_TLB_TAG_2	(EPIC_BASE + 0x0240)		/* TLB Tag 2 */
#define	EPIC_TLB_TAG_3	(EPIC_BASE + 0x0260)		/* TLB Tag 3 */
#define	EPIC_TLB_TAG_4	(EPIC_BASE + 0x0280)		/* TLB Tag 4 */
#define	EPIC_TLB_TAG_5	(EPIC_BASE + 0x02a0)		/* TLB Tag 5 */
#define	EPIC_TLB_TAG_6	(EPIC_BASE + 0x02c0)		/* TLB Tag 6 */
#define	EPIC_TLB_TAG_7	(EPIC_BASE + 0x02e0)		/* TLB Tag 7 */
#define		EPIC_TLB_TAG_RSVD	0x00000fff
#define		EPIC_TLB_TAG_EVAL	0x00001000
#define		EPIC_TLB_TAG_PCI_PAGE	0xffffe000

#define	EPIC_TLB_DATA_0	(EPIC_BASE + 0x0300)		/* TLB Data 0 */
#define	EPIC_TLB_DATA_1	(EPIC_BASE + 0x0320)		/* TLB Data 1 */
#define	EPIC_TLB_DATA_2	(EPIC_BASE + 0x0340)		/* TLB Data 2 */
#define	EPIC_TLB_DATA_3	(EPIC_BASE + 0x0360)		/* TLB Data 3 */
#define	EPIC_TLB_DATA_4	(EPIC_BASE + 0x0380)		/* TLB Data 4 */
#define	EPIC_TLB_DATA_5	(EPIC_BASE + 0x03a0)		/* TLB Data 5 */
#define	EPIC_TLB_DATA_6	(EPIC_BASE + 0x03c0)		/* TLB Data 6 */
#define	EPIC_TLB_DATA_7	(EPIC_BASE + 0x03e0)		/* TLB Data 7 */
#define		EPIC_TLB_DATA_RSVD	0xffe00001
#define		EPIC_TLB_DATA_CPU_PAGE	0x001ffffe

#define	EPIC_TBIA	(EPIC_BASE + 0x0400)		/* TLB Invl All */

/*
 * EPIC Scatter-Gather Map Entries
 */

struct sgmapent {
	u_int64_t val;
};
#define	SGMAPENT_EVAL	0x0000000000000001L
#define	SGMAPENT_PFN	0x00000000001ffffeL
#define	SGMAPENT_RSVD	0xffffffffffe00000L

#define	SGMAP_MAKEENTRY(pfn)	(SGMAPENT_EVAL | ((pfn) << 1))
