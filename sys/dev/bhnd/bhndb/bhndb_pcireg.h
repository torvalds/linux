/*-
 * Copyright (c) 2015 Landon Fuller <landon@landonf.org>
 * Copyright (c) 2010 Broadcom Corporation
 * 
 * Portions of this file were derived from the bcmdevs.h header contributed by
 * Broadcom to Android's bcmdhd driver module, and the pcicfg.h header
 * distributed with Broadcom's initial brcm80211 Linux driver release.
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

#ifndef _BHND_BHNDB_PCIREG_H_
#define _BHND_BHNDB_PCIREG_H_

/*
 * Common PCI/PCIE Bridge Configuration Registers.
 * 
 * = MAJOR CORE REVISIONS =
 * 
 * There have been four revisions to the BAR0 memory mappings used
 * in BHND PCI/PCIE bridge cores:
 * 
 * == PCI_V0 ==
 * Applies to:
 * -  PCI (cid=0x804, revision <= 12)
 * BAR0 size: 8KB
 * Address Map:
 *	[offset+  size]	type	description
 * 	[0x0000+0x1000]	dynamic mapped backplane address space (window 0).
 * 	[0x1000+0x0800]	fixed	SPROM shadow
 * 	[0x1800+0x0E00]	fixed	pci core device registers
 *	[0x1E00+0x0200]	fixed	pci core siba config registers
 * 
 * == PCI_V1 ==
 * Applies to:
 * -  PCI (cid=0x804, revision >= 13)
 * -  PCIE (cid=0x820) with ChipCommon (revision <= 31)
 * BAR0 size: 16KB
 * Address Map:
 *	[offset+  size]	type	description
 *	[0x0000+0x1000]	dynamic	mapped backplane address space (window 0).
 *	[0x1000+0x1000]	fixed	SPROM shadow
 *	[0x2000+0x1000]	fixed	pci/pcie core registers
 *	[0x3000+0x1000]	fixed	chipcommon core registers
 *
 * == PCI_V2 ==
 * Applies to:
 * - PCIE (cid=0x820) with ChipCommon (revision >= 32)
 * BAR0 size: 16KB
 * Address Map:
 *	[offset+  size]	type	description
 *	[0x0000+0x1000]	dynamic	mapped backplane address space (window 0).
 *	[0x1000+0x1000]	dynamic	mapped backplane address space (window 1).
 *	[0x2000+0x1000]	fixed	pci/pcie core registers
 *	[0x3000+0x1000]	fixed	chipcommon core registers
 *
 * == PCI_V3 ==
 * Applies to:
 * - PCIE Gen 2 (cid=0x83c)
 * BAR0 size: 32KB
 * Address Map:
 *	[offset+  size]	type	description
 *	[0x0000+0x1000]	dynamic	mapped backplane address space (window 0).
 *	[0x1000+0x1000]	dynamic	mapped backplane address space (window 1).
 *	[0x2000+0x1000]	fixed	pci/pcie core registers
 *	[0x3000+0x1000]	fixed	chipcommon core registers
 *	[???]
 * BAR1 size: varies
 * Address Map:
 *	[offset+  size]	type	description
 *	[0x0000+0x????]	fixed	ARM tightly-coupled memory (TCM).
 *				While fullmac chipsets provided a fixed
 *				4KB mapping, newer devices will vary.
 * 
 * = MINOR CORE REVISIONS =
 * 
 * == PCI Cores Revision >= 3 ==
 * - Mapped GPIO CSRs into the PCI config space. Refer to
 *   BHND_PCI_GPIO_*.
 * 
 * == PCI/PCIE Cores Revision >= 14 ==
 * - Mapped the clock CSR into the PCI config space. Refer to
 *   BHND_PCI_CLK_CTL_ST
 */

/* Common PCI/PCIE Config Registers */
#define	BHNDB_PCI_SPROM_CONTROL		0x88	/* sprom property control */
#define	BHNDB_PCI_BAR1_CONTROL		0x8c	/* BAR1 region prefetch/burst control */
#define	BHNDB_PCI_INT_STATUS		0x90	/* PCI and other cores interrupts */
#define	BHNDB_PCI_INT_MASK		0x94	/* mask of PCI and other cores interrupts */
#define	BHNDB_PCI_TO_SB_MB		0x98	/* signal backplane interrupts */
#define	BHNDB_PCI_BACKPLANE_ADDR	0xa0	/* address an arbitrary location on the system backplane */
#define	BHNDB_PCI_BACKPLANE_DATA	0xa4	/* data at the location specified by above address */

/* PCI (non-PCIe) GPIO/Clock Config Registers */
#define	BHNDB_PCI_CLK_CTL		0xa8	/* clock control/status (pci >=rev14) */
#define	BHNDB_PCI_GPIO_IN		0xb0	/* gpio input (pci >=rev3) */
#define	BHNDB_PCI_GPIO_OUT		0xb4	/* gpio output (pci >=rev3) */
#define	BHNDB_PCI_GPIO_OUTEN		0xb8	/* gpio output enable (pci >=rev3) */

/* Hardware revisions used to determine PCI revision */
#define	BHNDB_PCI_V0_MAX_PCI_HWREV	12
#define	BHNDB_PCI_V1_MIN_PCI_HWREV	13
#define	BHNDB_PCI_V1_MAX_CHIPC_HWREV	31
#define	BHNDB_PCI_V2_MIN_CHIPC_HWREV	32

/**
 * Number of times to retry writing to a PCI window address register.
 * 
 * On siba(4) devices, it's possible that writing a PCI window register may
 * not succeed; it's necessary to immediately read the configuration register
 * and retry if not set to the desired value.
 */
#define	BHNDB_PCI_BARCTRL_WRITE_RETRY	50	

/* PCI_V0  */
#define	BHNDB_PCI_V0_BAR0_WIN0_CONTROL	0x80	/* backplane address space accessed by BAR0/WIN0 */
#define	BHNDB_PCI_V0_BAR1_WIN0_CONTROL	0x84	/* backplane address space accessed by BAR1/WIN0. */

#define	BHNDB_PCI_V0_BAR0_SIZE		0x2000	/* 8KB BAR0 */
#define	BHNDB_PCI_V0_BAR0_WIN0_OFFSET	0x0	/* bar0 + 0x0 accesses configurable 4K region of backplane address space */
#define	BHNDB_PCI_V0_BAR0_WIN0_SIZE	0x1000
#define	BHNDB_PCI_V0_BAR0_SPROM_OFFSET	0x1000	/* bar0 + 4K accesses sprom shadow (in pci core) */
#define BHNDB_PCI_V0_BAR0_SPROM_SIZE	0x0800
#define	BHNDB_PCI_V0_BAR0_PCIREG_OFFSET	0x1800	/* bar0 + 6K accesses pci core registers (not including SSB CFG registers) */
#define	BHNDB_PCI_V0_BAR0_PCIREG_SIZE	0x0E00
#define	BHNDB_PCI_V0_BAR0_PCISB_OFFSET	0x1E00	/* bar0 + 7.5K accesses pci core's SSB CFG register blocks */
#define	BHNDB_PCI_V0_BAR0_PCISB_SIZE	0x0200
#define	BHNDB_PCI_V0_BAR0_PCISB_COREOFF	0xE00	/* mapped offset relative to the core base address */

/* PCI_V1 */
#define	BHNDB_PCI_V1_BAR0_WIN0_CONTROL	0x80	/* backplane address space accessed by BAR0/WIN0 */
#define	BHNDB_PCI_V1_BAR1_WIN0_CONTROL	0x84	/* backplane address space accessed by BAR1/WIN0. */

#define	BHNDB_PCI_V1_BAR0_SIZE		0x4000	/* 16KB BAR0 */
#define	BHNDB_PCI_V1_BAR0_WIN0_OFFSET	0x0	/* bar0 + 0x0 accesses configurable 4K region of backplane address space */
#define	BHNDB_PCI_V1_BAR0_WIN0_SIZE	0x1000
#define	BHNDB_PCI_V1_BAR0_SPROM_OFFSET	0x1000	/* bar0 + 4K accesses sprom shadow (in pci core) */
#define BHNDB_PCI_V1_BAR0_SPROM_SIZE	0x1000
#define	BHNDB_PCI_V1_BAR0_PCIREG_OFFSET	0x2000	/* bar0 + 8K accesses pci/pcie core registers */
#define	BHNDB_PCI_V1_BAR0_PCIREG_SIZE	0x1000
#define	BHNDB_PCI_V1_BAR0_CCREGS_OFFSET	0x3000	/* bar0 + 12K accesses chipc core registers */
#define	BHNDB_PCI_V1_BAR0_CCREGS_SIZE	0x1000

/* PCI_V2 */
#define	BHNDB_PCI_V2_BAR0_WIN0_CONTROL	0x80	/* backplane address space accessed by BAR0/WIN0 */
#define	BHNDB_PCI_V2_BAR1_WIN0_CONTROL	0x84	/* backplane address space accessed by BAR1/WIN0. */
#define	BHNDB_PCI_V2_BAR0_WIN1_CONTROL	0xAC	/* backplane address space accessed by BAR0/WIN1 */

#define	BHNDB_PCI_V2_BAR0_SIZE		0x4000	/* 16KB BAR0 */
#define	BHNDB_PCI_V2_BAR0_WIN0_OFFSET	0x0	/* bar0 + 0x0 accesses configurable 4K region of backplane address space */
#define	BHNDB_PCI_V2_BAR0_WIN0_SIZE	0x1000
#define	BHNDB_PCI_V2_BAR0_WIN1_OFFSET	0x1000	/* bar0 + 4K accesses second 4K window */
#define BHNDB_PCI_V2_BAR0_WIN1_SIZE	0x1000
#define	BHNDB_PCI_V2_BAR0_PCIREG_OFFSET	0x2000	/* bar0 + 8K accesses pci/pcie core registers */
#define	BHNDB_PCI_V2_BAR0_PCIREG_SIZE	0x1000
#define	BHNDB_PCI_V2_BAR0_CCREGS_OFFSET	0x3000	/* bar0 + 12K accesses chipc core registers */
#define	BHNDB_PCI_V2_BAR0_CCREGS_SIZE	0x1000

/* PCI_V3 (PCIe-G2) */
#define	BHNDB_PCI_V3_BAR0_WIN0_CONTROL	0x80	/* backplane address space accessed by BAR0/WIN0 */
#define BHNDB_PCI_V3_BAR0_WIN1_CONTROL	0x70	/* backplane address space accessed by BAR0/WIN1 */

#define	BHNDB_PCI_V3_BAR0_SIZE		0x8000	/* 32KB BAR0 */
#define	BHNDB_PCI_V3_BAR0_WIN0_OFFSET	0x0	/* bar0 + 0x0 accesses configurable 4K region of backplane address space */
#define	BHNDB_PCI_V3_BAR0_WIN0_SIZE	0x1000
#define	BHNDB_PCI_V3_BAR0_WIN1_OFFSET	0x1000	/* bar0 + 4K accesses second 4K window */
#define BHNDB_PCI_V3_BAR0_WIN1_SIZE	0x1000
#define	BHNDB_PCI_V3_BAR0_PCIREG_OFFSET	0x2000	/* bar0 + 8K accesses pci/pcie core registers */
#define	BHNDB_PCI_V3_BAR0_PCIREG_SIZE	0x1000
#define	BHNDB_PCI_V3_BAR0_CCREGS_OFFSET	0x3000	/* bar0 + 12K accesses chipc core registers */
#define	BHNDB_PCI_V3_BAR0_CCREGS_SIZE	0x1000

/* BHNDB_PCI_INT_STATUS */
#define	BHNDB_PCI_SBIM_STATUS_SERR	0x4	/* backplane SBErr interrupt status */

/* BHNDB_PCI_INT_MASK */
#define	BHNDB_PCI_SBIM_SHIFT		8	/* backplane core interrupt mask bits offset */
#define	BHNDB_PCI_SBIM_COREIDX_MAX	15	/**< maximum representible core index (in 16 bit field) */
#define	BHNDB_PCI_SBIM_MASK		0xff00	/* backplane core interrupt mask */
#define	BHNDB_PCI_SBIM_MASK_SERR	0x4	/* backplane SBErr interrupt mask */

/* BHNDB_PCI_SPROM_CONTROL */
#define	BHNDB_PCI_SPROM_SZ_MASK		0x03	/**< sprom size mask */
#define	BHNDB_PCI_SPROM_SZ_1KB		0x00	/**< 1KB sprom size */
#define	BHNDB_PCI_SPROM_SZ_4KB		0x01	/**< 4KB sprom size */
#define	BHNDB_PCI_SPROM_SZ_16KB		0x02	/**< 16KB sprom size */
#define	BHNDB_PCI_SPROM_SZ_RESERVED	0x03	/**< unsupported sprom size */
#define	BHNDB_PCI_SPROM_LOCKED		0x08	/**< sprom locked */
#define	BHNDB_PCI_SPROM_BLANK		0x04	/**< sprom blank */
#define	BHNDB_PCI_SPROM_WRITEEN		0x10	/**< sprom write enable */
#define	BHNDB_PCI_SPROM_BOOTROM_WE	0x20	/**< external bootrom write enable */
#define	BHNDB_PCI_SPROM_BACKPLANE_EN	0x40	/**< enable indirect backplane access (BHNDB_PCI_BACKPLANE_*) */
#define	BHNDB_PCI_SPROM_OTPIN_USE	0x80	/**< device OTP in use */


/* PCI (non-PCIe) BHNDB_PCI_GPIO_OUTEN  */
#define	BHNDB_PCI_GPIO_SCS		0x10	/* PCI config space bit 4 for 4306c0 slow clock source */
#define	BHNDB_PCI_GPIO_HWRAD_OFF		0x20	/* PCI config space GPIO 13 for hw radio disable */
#define	BHNDB_PCI_GPIO_XTAL_ON		0x40	/* PCI config space GPIO 14 for Xtal power-up */
#define	BHNDB_PCI_GPIO_PLL_OFF		0x80	/* PCI config space GPIO 15 for PLL power-down */

#endif /* _BHND_BHNDB_PCIREG_H_ */
