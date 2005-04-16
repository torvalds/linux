/*
 * asm-ppc/harrier_defs.h
 *
 * Definitions for Motorola MCG Harrier North Bridge & Memory controller
 *
 * Author: Dale Farnsworth
 *         dale.farnsworth@mvista.com
 *
 * Extracted from asm-ppc/harrier.h by:
 * 	   Randy Vinson
 * 	   rvinson@mvista.com
 *
 * Copyright 2001-2002 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASMPPC_HARRIER_DEFS_H
#define __ASMPPC_HARRIER_DEFS_H

#define HARRIER_DEFAULT_XCSR_BASE	0xfeff0000

#define HARRIER_VEND_DEV_ID		0x1057480b

#define HARRIER_VENI_OFF		0x00

#define HARRIER_REVI_OFF		0x05
#define HARRIER_UCTL_OFF		0xd0
#define HARRIER_XTAL64_MASK		0x02

#define HARRIER_MISC_CSR_OFF		0x1c
#define HARRIER_RSTOUT			0x01000000
#define HARRIER_SYSCON			0x08000000
#define HARRIER_EREADY			0x10000000
#define HARRIER_ERDYS			0x20000000

/* Function exception registers */
#define HARRIER_FEEN_OFF		0x40	/* enable */
#define HARRIER_FEST_OFF		0x44	/* status */
#define HARRIER_FEMA_OFF		0x48	/* mask */
#define HARRIER_FECL_OFF		0x4c	/* clear */

#define HARRIER_FE_DMA			0x80
#define HARRIER_FE_MIDB			0x40
#define HARRIER_FE_MIM0			0x20
#define HARRIER_FE_MIM1			0x10
#define HARRIER_FE_MIP			0x08
#define HARRIER_FE_UA0			0x04
#define HARRIER_FE_UA1			0x02
#define HARRIER_FE_ABT			0x01

#define HARRIER_SERIAL_0_OFF		0xc0

#define HARRIER_MBAR_OFF		0xe0
#define HARRIER_MBAR_MSK		0xfffc0000
#define HARRIER_MPIC_CSR_OFF		0xe4
#define HARRIER_MPIC_OPI_ENABLE		0x40
#define HARRIER_MPIC_IFEVP_OFF		0x10200
#define HARRIER_MPIC_IFEVP_VECT_MSK	0xff
#define HARRIER_MPIC_IFEDE_OFF		0x10210

/*
 * Define the Memory Controller register offsets.
 */
#define HARRIER_SDBA_OFF		0x110
#define HARRIER_SDBB_OFF		0x114
#define HARRIER_SDBC_OFF		0x118
#define HARRIER_SDBD_OFF		0x11c
#define HARRIER_SDBE_OFF		0x120
#define HARRIER_SDBF_OFF		0x124
#define HARRIER_SDBG_OFF		0x128
#define HARRIER_SDBH_OFF		0x12c

#define HARRIER_SDB_ENABLE		0x00000100
#define HARRIER_SDB_SIZE_MASK		0xf
#define HARRIER_SDB_SIZE_SHIFT		16
#define HARRIER_SDB_BASE_MASK		0xff
#define HARRIER_SDB_BASE_SHIFT		24

/*
 * Define outbound register offsets.
 */
#define HARRIER_OTAD0_OFF		0x220
#define HARRIER_OTOF0_OFF		0x224
#define HARRIER_OTAD1_OFF		0x228
#define HARRIER_OTOF1_OFF		0x22c
#define HARRIER_OTAD2_OFF		0x230
#define HARRIER_OTOF2_OFF		0x234
#define HARRIER_OTAD3_OFF		0x238
#define HARRIER_OTOF3_OFF		0x23c

#define HARRIER_OTADX_START_MSK		0xffff0000UL
#define HARRIER_OTADX_END_MSK		0x0000ffffUL

#define HARRIER_OTOFX_OFF_MSK		0xffff0000UL
#define HARRIER_OTOFX_ENA		0x80UL
#define HARRIER_OTOFX_WPE		0x10UL
#define HARRIER_OTOFX_SGE		0x08UL
#define HARRIER_OTOFX_RAE		0x04UL
#define HARRIER_OTOFX_MEM		0x02UL
#define HARRIER_OTOFX_IOM		0x01UL

/*
 * Define generic message passing register offsets
 */
/* Mirrored registers (visible from both PowerPC and PCI space) */
#define HARRIER_XCSR_MP_BASE_OFF	0x290	/* base offset in XCSR space */
#define HARRIER_PMEP_MP_BASE_OFF	0x100	/* base offset in PMEM space */
#define HARRIER_MGOM0_OFF		0x00	/* outbound msg 0 */
#define HARRIER_MGOM1_OFF		0x04	/* outbound msg 1 */
#define HARRIER_MGOD_OFF		0x08	/* outbound doorbells */

#define HARRIER_MGIM0_OFF		0x10	/* inbound msg 0 */
#define HARRIER_MGIM1_OFF		0x14	/* inbound msg 1 */
#define HARRIER_MGID_OFF		0x18	/* inbound doorbells */

/* PowerPC-only registers */
#define HARRIER_MGIDM_OFF		0x20	/* inbound doorbell mask */

/* PCI-only registers */
#define HARRIER_PMEP_MGST_OFF		0x20	/* (outbound) interrupt status */
#define HARRIER_PMEP_MGMS_OFF		0x24	/* (outbound) interrupt mask */
#define HARRIER_MG_OMI0			(1<<4)
#define HARRIER_MG_OMI1			(1<<5)

#define HARRIER_PMEP_MGODM_OFF		0x28	/* outbound doorbell mask */

/*
 * Define PCI configuration space register offsets
 */
#define HARRIER_XCSR_TO_PCFS_OFF	0x300

/*
 * Define message passing attribute register offset
 */
#define HARRIER_MPAT_OFF		0x44

/*
 * Define inbound attribute register offsets.
 */
#define HARRIER_ITSZ0_OFF		0x48
#define HARRIER_ITAT0_OFF		0x4c

#define HARRIER_ITSZ1_OFF		0x50
#define HARRIER_ITAT1_OFF		0x54

#define HARRIER_ITSZ2_OFF		0x58
#define HARRIER_ITAT2_OFF		0x5c

#define HARRIER_ITSZ3_OFF		0x60
#define HARRIER_ITAT3_OFF		0x64

/* inbound translation size constants */
#define HARRIER_ITSZ_MSK		0xff
#define HARRIER_ITSZ_4KB		0x00
#define HARRIER_ITSZ_8KB		0x01
#define HARRIER_ITSZ_16KB		0x02
#define HARRIER_ITSZ_32KB		0x03
#define HARRIER_ITSZ_64KB		0x04
#define HARRIER_ITSZ_128KB		0x05
#define HARRIER_ITSZ_256KB		0x06
#define HARRIER_ITSZ_512KB		0x07
#define HARRIER_ITSZ_1MB		0x08
#define HARRIER_ITSZ_2MB		0x09
#define HARRIER_ITSZ_4MB		0x0A
#define HARRIER_ITSZ_8MB		0x0B
#define HARRIER_ITSZ_16MB		0x0C
#define HARRIER_ITSZ_32MB		0x0D
#define HARRIER_ITSZ_64MB		0x0E
#define HARRIER_ITSZ_128MB		0x0F
#define HARRIER_ITSZ_256MB		0x10
#define HARRIER_ITSZ_512MB		0x11
#define HARRIER_ITSZ_1GB		0x12
#define HARRIER_ITSZ_2GB		0x13

/* inbound translation offset */
#define HARRIER_ITOF_SHIFT		0x10
#define HARRIER_ITOF_MSK		0xffff

/* inbound translation atttributes */
#define HARRIER_ITAT_PRE		(1<<3)
#define HARRIER_ITAT_RAE		(1<<4)
#define HARRIER_ITAT_WPE		(1<<5)
#define HARRIER_ITAT_MEM		(1<<6)
#define HARRIER_ITAT_ENA		(1<<7)
#define HARRIER_ITAT_GBL		(1<<16)

#define HARRIER_LBA_OFF			0x80
#define HARRIER_LBA_MSK			(1<<31)

#define HARRIER_XCSR_SIZE		1024

/* macros to calculate message passing register offsets */
#define HARRIER_MP_XCSR(x) ((u32)HARRIER_XCSR_MP_BASE_OFF + (u32)x)

#define HARRIER_MP_PMEP(x) ((u32)HARRIER_PMEP_MP_BASE_OFF + (u32)x)

/*
 * Define PCI configuration space register offsets
 */
#define HARRIER_MPBAR_OFF		PCI_BASE_ADDRESS_0
#define HARRIER_ITBAR0_OFF		PCI_BASE_ADDRESS_1
#define HARRIER_ITBAR1_OFF		PCI_BASE_ADDRESS_2
#define HARRIER_ITBAR2_OFF		PCI_BASE_ADDRESS_3
#define HARRIER_ITBAR3_OFF		PCI_BASE_ADDRESS_4

#define HARRIER_XCSR_CONFIG(x) ((u32)HARRIER_XCSR_TO_PCFS_OFF + (u32)x)

#endif				/* __ASMPPC_HARRIER_DEFS_H */
