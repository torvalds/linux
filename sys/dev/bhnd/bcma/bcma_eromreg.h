/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2015 Landon Fuller <landon@landonf.org>
 * Copyright (c) 2010 Broadcom Corporation
 * 
 * Portions of this file were derived from the aidmp.h header
 * distributed with Broadcom's initial brcm80211 Linux driver release, as
 * contributed to the Linux staging repository.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 * 
 * $FreeBSD$
 */

#ifndef	_BCMA_BCMA_EROM_REG_H_
#define	_BCMA_BCMA_EROM_REG_H_

/* Enumeration ROM device registers */
#define	BCMA_EROM_TABLE_START	0x000	/**< device enumeration table offset */
#define	BCMA_EROM_REMAPCONTROL	0xe00
#define	BCMA_EROM_REMAPSELECT	0xe04
#define	BCMA_EROM_MASTERSELECT	0xe10
#define	BCMA_EROM_ITCR		0xf00
#define	BCMA_EROM_ITIP		0xf04
#define	BCMA_EROM_TABLE_SIZE	BCMA_EROM_REMAPCONTROL - BCMA_EROM_TABLE_START

/**
 * Extract an entry attribute by applying _MASK and _SHIFT defines.
 * 
 * @param _entry The entry containing the desired attribute
 * @param _attr The BCMA EROM attribute name (e.g. ENTRY_ISVALID), to be
 * concatenated with the `BCMA_EROM_` prefix and `_MASK`/`_SHIFT` suffixes.
 */
#define	BCMA_EROM_GET_ATTR(_entry, _attr)			\
	((_entry & BCMA_EROM_ ## _attr ## _MASK)	\
	>> BCMA_EROM_ ## _attr ## _SHIFT)

/**
 * Test an EROM entry's validity and type.
 *
 * @param _entry The entry to test.
 * @param _type The required type
 * @retval true if the entry type matches and the BCMA_EROM_ENTRY_ISVALID flag
 * is set.
 * @retval false if the entry is not valid, or if the type does not match.
 */
#define	BCMA_EROM_ENTRY_IS(_entry, _type)					\
	(BCMA_EROM_GET_ATTR(_entry, ENTRY_ISVALID) &&			\
	 BCMA_EROM_GET_ATTR(_entry, ENTRY_TYPE) == BCMA_EROM_ENTRY_TYPE_ ## _type)

/*
 * Enumeration ROM Constants
 */
#define	BCMA_EROM_TABLE_EOF		0xF		/* end of EROM table */

#define	BCMA_EROM_ENTRY_ISVALID_MASK	0x1		/* is entry valid? */
#define	BCMA_EROM_ENTRY_ISVALID_SHIFT	0

/* EROM Entry Types */
#define	BCMA_EROM_ENTRY_TYPE_MASK	0x6		/* entry type mask */
#define	BCMA_EROM_ENTRY_TYPE_SHIFT	0
#  define BCMA_EROM_ENTRY_TYPE_CORE	0x0		/* core descriptor */
#  define BCMA_EROM_ENTRY_TYPE_MPORT	0x2		/* master port descriptor */
#  define BCMA_EROM_ENTRY_TYPE_REGION	0x4		/* address region descriptor */

/* EROM Core DescriptorA (31:0) */
#define	BCMA_EROM_COREA_DESIGNER_MASK	0xFFF00000	/* core designer (JEP-106 mfg id) */
#define	BCMA_EROM_COREA_DESIGNER_SHIFT	20
#define	BCMA_EROM_COREA_ID_MASK		0x000FFF00	/* broadcom-assigned core id */
#define	BCMA_EROM_COREA_ID_SHIFT	8
#define	BCMA_EROM_COREA_CLASS_MASK	0x000000F0	/* core class */
#define	BCMA_EROM_COREA_CLASS_SHIFT	4

/* EROM Core DescriptorB (63:32) */
#define	BCMA_EROM_COREB_NUM_MP_MASK	0x000001F0	/* master port count */
#define	BCMA_EROM_COREB_NUM_MP_SHIFT	4
#define	BCMA_EROM_COREB_NUM_DP_MASK	0x00003E00	/* device/bridge port count */
#define	BCMA_EROM_COREB_NUM_DP_SHIFT	9
#define	BCMA_EROM_COREB_NUM_WMP_MASK	0x0007C000	/* master wrapper port count */
#define	BCMA_EROM_COREB_NUM_WMP_SHIFT	14
#define	BCMA_EROM_COREB_NUM_WSP_MASK	0x00F80000	/* slave wrapper port count */
#define	BCMA_EROM_COREB_NUM_WSP_SHIFT	19
#define	BCMA_EROM_COREB_REV_MASK	0xFF000000	/* broadcom-assigned core revision */
#define	BCMA_EROM_COREB_REV_SHIFT	24

/* EROM Master Port Descriptor 
 * 
 * The attribute descriptions are derived from background information
 * on the AXI bus and PL301 interconnect, but are undocumented
 * by Broadcom and may be incorrect.
 */
#define	BCMA_EROM_MPORT_NUM_MASK	0x0000FF00	/* AXI master number (unique per interconnect) */
#define	BCMA_EROM_MPORT_NUM_SHIFT	8
#define	BCMA_EROM_MPORT_ID_MASK		0x000000F0	/* AXI master ID (unique per master). */
#define	BCMA_EROM_MPORT_ID_SHIFT	4


/* EROM Slave Port MMIO Region Descriptor */
#define	BCMA_EROM_REGION_BASE_MASK	0xFFFFF000	/* region base address */
#define	BCMA_EROM_REGION_BASE_SHIFT	0
#define	BCMA_EROM_REGION_64BIT_MASK	0x00000008	/* base address spans two 32-bit entries */
#define	BCMA_EROM_REGION_64BIT_SHIFT	0
#define	BCMA_EROM_REGION_PORT_MASK	0x00000F00	/* region's associated port */
#define	BCMA_EROM_REGION_PORT_SHIFT	8
#define	BCMA_EROM_REGION_TYPE_MASK	0x000000C0	/* region type */
#define	BCMA_EROM_REGION_TYPE_SHIFT	6
#define	  BCMA_EROM_REGION_TYPE_DEVICE	0		/* region maps to a device */
#define	  BCMA_EROM_REGION_TYPE_BRIDGE	1		/* region maps to a bridge (e.g. AXI2APB) */
#define	  BCMA_EROM_REGION_TYPE_SWRAP	2		/* region maps to a slave port's DMP agent/wrapper */
#define	  BCMA_EROM_REGION_TYPE_MWRAP	3		/* region maps to a master port's DMP agent/wrapper */

#define	BCMA_EROM_REGION_SIZE_MASK	0x00000030	/* region size encoding */
#define	BCMA_EROM_REGION_SIZE_SHIFT	4
#define	  BCMA_EROM_REGION_SIZE_4K	0		/* 4K region */
#define	  BCMA_EROM_REGION_SIZE_8K	1		/* 8K region */
#define	  BCMA_EROM_REGION_SIZE_16K	2		/* 16K region */
#define	  BCMA_EROM_REGION_SIZE_OTHER	3		/* defined by an additional size descriptor entry. */
#define	BCMA_EROM_REGION_SIZE_BASE	0x1000

/* Region Size Descriptor */
#define	BCMA_EROM_RSIZE_VAL_MASK	0xFFFFF000	/* region size */
#define	BCMA_EROM_RSIZE_VAL_SHIFT	0
#define	BCMA_EROM_RSIZE_64BIT_MASK	0x00000008	/* size spans two 32-bit entries */
#define	BCMA_EROM_RSIZE_64BIT_SHIFT	0

#endif /* _BCMA_BCMA_EROM_REG_H_ */
