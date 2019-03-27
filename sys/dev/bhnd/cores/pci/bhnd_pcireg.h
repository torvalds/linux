/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2015 Landon Fuller <landon@landonf.org>
 * Copyright (c) 2010 Broadcom Corporation
 * All rights reserved.
 *
 * This file is derived from the hndsoc.h, pci_core.h, and pcie_core.h headers
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

#ifndef _BHND_CORES_PCI_BHND_PCIREG_H_
#define _BHND_CORES_PCI_BHND_PCIREG_H_

/*
 * PCI/PCIe-Gen1 DMA Constants
 */

#define	BHND_PCI_DMA32_TRANSLATION	0x40000000			/**< PCI DMA32 address translation (sbtopci2) */
#define	BHND_PCI_DMA32_MASK		BHND_PCI_SBTOPCI2_MASK		/**< PCI DMA32 translation mask */

#define	BHND_PCIE_DMA32_TRANSLATION	0x80000000			/**< PCIe-Gen1 DMA32 address translation (sb2pcitranslation2) */
#define	BHND_PCIE_DMA32_MASK		BHND_PCIE_SBTOPCI2_MASK		/**< PCIe-Gen1 DMA32 translation mask */

#define	BHND_PCIE_DMA64_TRANSLATION	_BHND_PCIE_DMA64(TRANSLATION)	/**< PCIe-Gen1 DMA64 address translation (sb2pcitranslation2) */
#define	BHND_PCIE_DMA64_MASK		_BHND_PCIE_DMA64(MASK)		/**< PCIe-Gen1 DMA64 translation mask */
#define	_BHND_PCIE_DMA64(_x)		((uint64_t)BHND_PCIE_DMA32_ ## _x << 32)
/*
 * PCI Core Registers
 */

#define	BHND_PCI_CTL			0x000	/**< PCI core control*/
#define	BHND_PCI_ARB_CTL		0x010	/**< PCI arbiter control */
#define	BHND_PCI_CLKRUN_CTL		0x014	/**< PCI clckrun control (>= rev11) */
#define	BHND_PCI_INTR_STATUS		0x020	/**< Interrupt status */
#define	BHND_PCI_INTR_MASK		0x024	/**< Interrupt mask */
#define	BHND_PCI_SBTOPCI_MBOX		0x028	/**< Sonics to PCI mailbox */
#define	BHND_PCI_BCAST_ADDR		0x050	/**< Sonics broadcast address (pci) */
#define	BHND_PCI_BCAST_DATA		0x054	/**< Sonics broadcast data (pci) */
#define	BHND_PCI_GPIO_IN		0x060	/**< GPIO input (>= rev2) */
#define	BHND_PCI_GPIO_OUT		0x064	/**< GPIO output (>= rev2) */
#define	BHND_PCI_GPIO_EN		0x068	/**< GPIO output enable (>= rev2) */
#define	BHND_PCI_GPIO_CTL		0x06C	/**< GPIO control (>= rev2) */
#define	BHND_PCI_SBTOPCI0		0x100	/**< Sonics to PCI translation 0 */
#define	BHND_PCI_SBTOPCI1		0x104	/**< Sonics to PCI translation 1 */
#define	BHND_PCI_SBTOPCI2		0x108	/**< Sonics to PCI translation 2 */
#define	BHND_PCI_FUNC0_CFG		0x400	/**< PCI function 0 cfg space (>= rev8) */
#define	BHND_PCI_FUNC1_CFG		0x500	/**< PCI function 1 cfg space (>= rev8) */
#define	BHND_PCI_FUNC2_CFG		0x600	/**< PCI function 2 cfg space (>= rev8) */
#define	BHND_PCI_FUNC3_CFG		0x700	/**< PCI function 3 cfg space (>= rev8) */
#define	BHND_PCI_SPROM_SHADOW		0x800	/**< PCI SPROM shadow */

/* BHND_PCI_CTL */
#define	BHND_PCI_CTL_RST_OE		0x01	/* When set, drives PCI_RESET out to pin */
#define	BHND_PCI_CTL_RST		0x02	/* Value driven out to pin */
#define	BHND_PCI_CTL_CLK_OE		0x04	/* When set, drives clock as gated by PCI_CLK out to pin */
#define	BHND_PCI_CTL_CLK		0x08	/* Gate for clock driven out to pin */

/* BHND_PCI_ARB_CTL */
#define	BHND_PCI_ARB_INT		0x01	/* When set, use an internal arbiter */
#define	BHND_PCI_ARB_EXT		0x02	/* When set, use an external arbiter */

/* BHND_PCI_ARB_CTL - ParkID (>= rev8) */
#define	BHND_PCI_ARB_PARKID_MASK	0x1c	/* Selects which agent is parked on an idle bus */
#define	BHND_PCI_ARB_PARKID_SHIFT	2
#define	BHND_PCI_ARB_PARKID_EXT0	0	/* External master 0 */
#define	BHND_PCI_ARB_PARKID_EXT1	1	/* External master 1 */
#define	BHND_PCI_ARB_PARKID_EXT2	2	/* External master 2 */
#define	BHND_PCI_ARB_PARKID_EXT3	3	/* External master 3 (rev >= 11) */
#define	BHND_PCI_ARB_PARKID_INT_r10	3	/* Internal master (rev < 11) */
#define	BHND_PCI_ARB_PARKID_INT_r11	4	/* Internal master (rev >= 11) */
#define	BHND_PCI_ARB_PARKID_LAST_r10	4	/* Last active master (rev < 11) */
#define	BHND_PCI_ARB_PARKID_LAST_r11	5	/* Last active master (rev >= 11) */

/* BHND_PCI_CLKRUN_CTL */
#define	BHND_PCI_CLKRUN_DSBL		0x8000	/* Bit 15 forceClkrun */

/* BHND_PCI_INTR_STATUS / BHND_PCI_INTR_MASK */
#define	BHND_PCI_INTR_A			0x01	/* PCI INTA# is asserted */
#define	BHND_PCI_INTR_B			0x02	/* PCI INTB# is asserted */
#define	BHND_PCI_INTR_SERR		0x04	/* PCI SERR# has been asserted (write one to clear) */
#define	BHND_PCI_INTR_PERR		0x08	/* PCI PERR# has been asserted (write one to clear) */

/* BHND_PCI_SBTOPCI_MBOX
 * (General) PCI/SB mailbox interrupts, two bits per pci function */
#define	BHND_PCI_SBTOPCI_MBOX_F0_0	0x100	/* function 0, int 0 */
#define	BHND_PCI_SBTOPCI_MBOX_F0_1	0x200	/* function 0, int 1 */
#define	BHND_PCI_SBTOPCI_MBOX_F1_0	0x400	/* function 1, int 0 */
#define	BHND_PCI_SBTOPCI_MBOX_F1_1	0x800	/* function 1, int 1 */
#define	BHND_PCI_SBTOPCI_MBOX_F2_0	0x1000	/* function 2, int 0 */
#define	BHND_PCI_SBTOPCI_MBOX_F2_1	0x2000	/* function 2, int 1 */
#define	BHND_PCI_SBTOPCI_MBOX_F3_0	0x4000	/* function 3, int 0 */
#define	BHND_PCI_SBTOPCI_MBOX_F3_1	0x8000	/* function 3, int 1 */

/* BHND_PCI_BCAST_ADDR */
#define	BHNC_PCI_BCAST_ADDR_MASK	0xFF	/* Broadcast register address */

/* Sonics to PCI translation types */
#define BHND_PCI_SBTOPCI0_MASK	0xfc000000
#define BHND_PCI_SBTOPCI1_MASK	0xfc000000
#define BHND_PCI_SBTOPCI2_MASK	0xc0000000

/* Access type bits (0:1) */
#define	BHND_PCI_SBTOPCI_MEM		0
#define	BHND_PCI_SBTOPCI_IO		1
#define	BHND_PCI_SBTOPCI_CFG0		2
#define	BHND_PCI_SBTOPCI_CFG1		3

#define	BHND_PCI_SBTOPCI_PREF		0x4	/* prefetch enable */
#define	BHND_PCI_SBTOPCI_BURST		0x8	/* burst enable */

#define	BHND_PCI_SBTOPCI_RC_MASK	0x30	/* read command (>= rev11) */
#define	BHND_PCI_SBTOPCI_RC_READ	0x00	/* memory read */
#define	BHND_PCI_SBTOPCI_RC_READLINE	0x10	/* memory read line */
#define	BHND_PCI_SBTOPCI_RC_READMULTI	0x20	/* memory read multiple */

/* PCI base address bits in SPROM shadow area */
#define	BHND_PCI_SRSH_PI_OFFSET		0	/* first word */
#define	BHND_PCI_SRSH_PI_MASK		0xf000	/* bit 15:12 */
#define	BHND_PCI_SRSH_PI_SHIFT		12	/* bit 15:12 */
#define	BHND_PCI_SRSH_PI_ADDR_MASK	0x0000F000
#define	BHND_PCI_SRSH_PI_ADDR_SHIFT	12

/*
 * PCIe-Gen1 Core Registers
 */

#define	BHND_PCIE_CTL		BHND_PCI_CTL		/**< PCI core control*/
#define	BHND_PCIE_BIST_STATUS	0x00C			/**< BIST status */
#define	BHND_PCIE_GPIO_SEL	0x010			/**< GPIO select */
#define	BHND_PCIE_GPIO_OUT_EN	0x014			/**< GPIO output enable */
#define	BHND_PCIE_INTR_STATUS	BHND_PCI_INTR_STATUS	/**< Interrupt status */
#define	BHND_PCIE_INTR_MASK	BHND_PCI_INTR_MASK	/**< Interrupt mask */
#define	BHND_PCIE_SBTOPCI_MBOX	BHND_PCI_SBTOPCI_MBOX	/**< Sonics to PCI mailbox */
#define	BHND_PCIE_SBTOPCI0	BHND_PCI_SBTOPCI0	/**< Sonics to PCI translation 0 */
#define	BHND_PCIE_SBTOPCI1	BHND_PCI_SBTOPCI1	/**< Sonics to PCI translation 1 */
#define	BHND_PCIE_SBTOPCI2	BHND_PCI_SBTOPCI2	/**< Sonics to PCI translation 2 */

/* indirect pci config space access */
#define	BHND_PCIE_CFG_ADDR	0x120			/**< pcie config space address */
#define	BHND_PCIE_CFG_DATA	0x124			/**< pcie config space data */

/* mdio register access */
#define	BHND_PCIE_MDIO_CTL	0x128			/**< mdio control */
#define	BHND_PCIE_MDIO_DATA	0x12C			/**< mdio data */

/* indirect protocol phy/dllp/tlp register access */
#define	BHND_PCIE_IND_ADDR	0x130			/**< internal protocol register address */
#define	BHND_PCIE_IND_DATA	0x134			/**< internal protocol register data */

#define	BHND_PCIE_CLKREQEN_CTL	0x138			/**< clkreq rdma control */
#define	BHND_PCIE_FUNC0_CFG	BHND_PCI_FUNC0_CFG	/**< PCI function 0 cfg space */
#define	BHND_PCIE_FUNC1_CFG	BHND_PCI_FUNC1_CFG	/**< PCI function 1 cfg space */
#define	BHND_PCIE_FUNC2_CFG	BHND_PCI_FUNC2_CFG	/**< PCI function 2 cfg space */
#define	BHND_PCIE_FUNC3_CFG	BHND_PCI_FUNC3_CFG	/**< PCI function 3 cfg space */
#define	BHND_PCIE_SPROM_SHADOW	BHND_PCI_SPROM_SHADOW	/**< PCI SPROM shadow */

/* BHND_PCIE_CTL */
#define	BHND_PCIE_CTL_RST_OE	BHND_PCI_CTL_RST_OE	/* When set, drives PCI_RESET out to pin */
#define	BHND_PCIE_CTL_RST	BHND_PCI_CTL_RST_OE	/* Value driven out to pin */

/* BHND_PCI_INTR_STATUS / BHND_PCI_INTR_MASK */
#define	BHND_PCIE_INTR_A	BHND_PCI_INTR_A		/* PCIE INTA message is received */
#define	BHND_PCIE_INTR_B	BHND_PCI_INTR_B		/* PCIE INTB message is received */
#define	BHND_PCIE_INTR_FATAL	0x04			/* PCIE INTFATAL message is received */
#define	BHND_PCIE_INTR_NFATAL	0x08			/* PCIE INTNONFATAL message is received */
#define	BHND_PCIE_INTR_CORR	0x10			/* PCIE INTCORR message is received */
#define	BHND_PCIE_INTR_PME	0x20			/* PCIE INTPME message is received */

/* SB to PCIE translation masks */
#define	BHND_PCIE_SBTOPCI0_MASK	BHND_PCI_SBTOPCI0_MASK
#define	BHND_PCIE_SBTOPCI1_MASK	BHND_PCI_SBTOPCI1_MASK
#define	BHND_PCIE_SBTOPCI2_MASK	BHND_PCI_SBTOPCI2_MASK

/* Access type bits (0:1) */
#define	BHND_PCIE_SBTOPCI_MEM	BHND_PCI_SBTOPCI_MEM
#define	BHND_PCIE_SBTOPCI_IO	BHND_PCI_SBTOPCI_IO
#define	BHND_PCIE_SBTOPCI_CFG0	BHND_PCI_SBTOPCI_CFG0
#define	BHND_PCIE_SBTOPCI_CFG1	BHND_PCI_SBTOPCI_CFG1

#define	BHND_PCIE_SBTOPCI_PREF	BHND_PCI_SBTOPCI_PREF	/* prefetch enable */
#define	BHND_PCIE_SBTOPCI_BURST	BHND_PCI_SBTOPCI_BURST	/* burst enable */

/* BHND_PCIE_CFG_ADDR / BHND_PCIE_CFG_DATA */
#define	BHND_PCIE_CFG_ADDR_FUNC_MASK	0x7000
#define	BHND_PCIE_CFG_ADDR_FUNC_SHIFT	12
#define	BHND_PCIE_CFG_ADDR_REG_MASK	0x0FFF
#define	BHND_PCIE_CFG_ADDR_REG_SHIFT	0

#define	BHND_PCIE_CFG_OFFSET(f, r)	\
	((((f) & BHND_PCIE_CFG_ADDR_FUNC_MASK) << BHND_PCIE_CFG_ADDR_FUNC_SHIFT) | \
	(((r) & BHND_PCIE_CFG_ADDR_FUNC_SHIFT) << BHND_PCIE_CFG_ADDR_REG_SHIFT))
	
/* BHND_PCIE_MDIO_CTL control */
#define	BHND_PCIE_MDIOCTL_DIVISOR_MASK		0x7f	/* clock divisor mask */
#define	BHND_PCIE_MDIOCTL_DIVISOR_VAL		0x2	/* default clock divisor */
#define	BHND_PCIE_MDIOCTL_PREAM_EN		0x80	/* enable preamble mode */
#define	BHND_PCIE_MDIOCTL_DONE			0x100	/* tranaction completed */

/* PCIe BHND_PCIE_MDIO_DATA Data */
#define	BHND_PCIE_MDIODATA_PHYADDR_MASK		0x0f800000	/* phy addr */
#define	BHND_PCIE_MDIODATA_PHYADDR_SHIFT	23
#define	BHND_PCIE_MDIODATA_REGADDR_MASK		0x007c0000	/* reg/dev addr */
#define	BHND_PCIE_MDIODATA_REGADDR_SHIFT	18
#define	BHND_PCIE_MDIODATA_DATA_MASK		0x0000ffff	/* data  */

#define	BHND_PCIE_MDIODATA_TA			0x00020000	/* slave turnaround time */
#define	BHND_PCIE_MDIODATA_START		0x40000000	/* start of transaction */
#define	BHND_PCIE_MDIODATA_CMD_WRITE		0x10000000	/* write command */
#define	BHND_PCIE_MDIODATA_CMD_READ		0x20000000	/* read command */

#define	BHND_PCIE_MDIODATA_ADDR(_phyaddr, _regaddr)	(		\
	(((_phyaddr) << BHND_PCIE_MDIODATA_PHYADDR_SHIFT) &		\
	    BHND_PCIE_MDIODATA_PHYADDR_MASK) |				\
	(((_regaddr) << BHND_PCIE_MDIODATA_REGADDR_SHIFT) &		\
	    BHND_PCIE_MDIODATA_REGADDR_MASK)				\
)

/* PCIE protocol PHY diagnostic registers */
#define	BHND_PCIE_PLP_MODEREG			0x200	/* Mode */
#define	BHND_PCIE_PLP_STATUSREG			0x204	/* Status */
#define	BHND_PCIE_PLP_LTSSMCTRLREG		0x208	/* LTSSM control */
#define	BHND_PCIE_PLP_LTLINKNUMREG		0x20c	/* Link Training Link number */
#define	BHND_PCIE_PLP_LTLANENUMREG		0x210	/* Link Training Lane number */
#define	BHND_PCIE_PLP_LTNFTSREG			0x214	/* Link Training N_FTS */
#define	BHND_PCIE_PLP_ATTNREG			0x218	/* Attention */
#define	BHND_PCIE_PLP_ATTNMASKREG		0x21C	/* Attention Mask */
#define	BHND_PCIE_PLP_RXERRCTR			0x220	/* Rx Error */
#define	BHND_PCIE_PLP_RXFRMERRCTR		0x224	/* Rx Framing Error */
#define	BHND_PCIE_PLP_RXERRTHRESHREG		0x228	/* Rx Error threshold */
#define	BHND_PCIE_PLP_TESTCTRLREG		0x22C	/* Test Control reg */
#define	BHND_PCIE_PLP_SERDESCTRLOVRDREG		0x230	/* SERDES Control Override */
#define	BHND_PCIE_PLP_TIMINGOVRDREG		0x234	/* Timing param override */
#define	BHND_PCIE_PLP_RXTXSMDIAGREG		0x238	/* RXTX State Machine Diag */
#define	BHND_PCIE_PLP_LTSSMDIAGREG		0x23C	/* LTSSM State Machine Diag */

/* PCIE protocol DLLP diagnostic registers */
#define	BHND_PCIE_DLLP_LCREG			0x100	/* Link Control */
#define	  BHND_PCIE_DLLP_LCREG_PCIPM_EN		0x40	/* Enable PCI-PM power management */
#define	BHND_PCIE_DLLP_LSREG			0x104	/* Link Status */
#define	BHND_PCIE_DLLP_LAREG			0x108	/* Link Attention */
#define	BHND_PCIE_DLLP_LAMASKREG		0x10C	/* Link Attention Mask */
#define	BHND_PCIE_DLLP_NEXTTXSEQNUMREG		0x110	/* Next Tx Seq Num */
#define	BHND_PCIE_DLLP_ACKEDTXSEQNUMREG		0x114	/* Acked Tx Seq Num */
#define	BHND_PCIE_DLLP_PURGEDTXSEQNUMREG	0x118	/* Purged Tx Seq Num */
#define	BHND_PCIE_DLLP_RXSEQNUMREG		0x11C	/* Rx Sequence Number */
#define	BHND_PCIE_DLLP_LRREG			0x120	/* Link Replay */
#define	BHND_PCIE_DLLP_LACKTOREG		0x124	/* Link Ack Timeout */
#define	BHND_PCIE_DLLP_PMTHRESHREG		0x128	/* Power Management Threshold */
#define	  BHND_PCIE_L0THRESHOLDTIME_MASK	0xFF00	/* bits 0 - 7 */
#define	  BHND_PCIE_L1THRESHOLDTIME_MASK	0xFF00	/* bits 8 - 15 */
#define	  BHND_PCIE_L1THRESHOLDTIME_SHIFT	8	/* PCIE_L1THRESHOLDTIME_SHIFT */
#define	  BHND_PCIE_L1THRESHOLD_WARVAL		0x72	/* WAR value */
#define	  BHND_PCIE_ASPMTIMER_EXTEND		0x1000000 /* > rev7: enable extend ASPM timer */
#define	BHND_PCIE_DLLP_RTRYWPREG		0x12C	/* Retry buffer write ptr */
#define	BHND_PCIE_DLLP_RTRYRPREG		0x130	/* Retry buffer Read ptr */
#define	BHND_PCIE_DLLP_RTRYPPREG		0x134	/* Retry buffer Purged ptr */
#define	BHND_PCIE_DLLP_RTRRWREG			0x138	/* Retry buffer Read/Write */
#define	BHND_PCIE_DLLP_ECTHRESHREG		0x13C	/* Error Count Threshold */
#define	BHND_PCIE_DLLP_TLPERRCTRREG		0x140	/* TLP Error Counter */
#define	BHND_PCIE_DLLP_ERRCTRREG		0x144	/* Error Counter */
#define	BHND_PCIE_DLLP_NAKRXCTRREG		0x148	/* NAK Received Counter */
#define	BHND_PCIE_DLLP_TESTREG			0x14C	/* Test */
#define	BHND_PCIE_DLLP_PKTBIST			0x150	/* Packet BIST */
#define	BHND_PCIE_DLLP_PCIE11			0x154	/* DLLP PCIE 1.1 reg */

#define	BHND_PCIE_DLLP_LSREG_LINKUP		(1 << 16)

/* PCIE protocol TLP diagnostic registers */
#define	BHND_PCIE_TLP_CONFIGREG			0x000	/* Configuration */
#define	BHND_PCIE_TLP_WORKAROUNDSREG		0x004	/* TLP Workarounds */
#define	  BHND_PCIE_TLP_WORKAROUND_URBIT	0x8	/* If enabled, UR status bit is set 
							 * on memory access of an unmatched
							 * address */

#define	BHND_PCIE_TLP_WRDMAUPPER		0x010	/* Write DMA Upper Address */
#define	BHND_PCIE_TLP_WRDMALOWER		0x014	/* Write DMA Lower Address */
#define	BHND_PCIE_TLP_WRDMAREQ_LBEREG		0x018	/* Write DMA Len/ByteEn Req */
#define	BHND_PCIE_TLP_RDDMAUPPER		0x01C	/* Read DMA Upper Address */
#define	BHND_PCIE_TLP_RDDMALOWER		0x020	/* Read DMA Lower Address */
#define	BHND_PCIE_TLP_RDDMALENREG		0x024	/* Read DMA Len Req */
#define	BHND_PCIE_TLP_MSIDMAUPPER		0x028	/* MSI DMA Upper Address */
#define	BHND_PCIE_TLP_MSIDMALOWER		0x02C	/* MSI DMA Lower Address */
#define	BHND_PCIE_TLP_MSIDMALENREG		0x030	/* MSI DMA Len Req */
#define	BHND_PCIE_TLP_SLVREQLENREG		0x034	/* Slave Request Len */
#define	BHND_PCIE_TLP_FCINPUTSREQ		0x038	/* Flow Control Inputs */
#define	BHND_PCIE_TLP_TXSMGRSREQ		0x03C	/* Tx StateMachine and Gated Req */
#define	BHND_PCIE_TLP_ADRACKCNTARBLEN		0x040	/* Address Ack XferCnt and ARB Len */
#define	BHND_PCIE_TLP_DMACPLHDR0		0x044	/* DMA Completion Hdr 0 */
#define	BHND_PCIE_TLP_DMACPLHDR1		0x048	/* DMA Completion Hdr 1 */
#define	BHND_PCIE_TLP_DMACPLHDR2		0x04C	/* DMA Completion Hdr 2 */
#define	BHND_PCIE_TLP_DMACPLMISC0		0x050	/* DMA Completion Misc0 */
#define	BHND_PCIE_TLP_DMACPLMISC1		0x054	/* DMA Completion Misc1 */
#define	BHND_PCIE_TLP_DMACPLMISC2		0x058	/* DMA Completion Misc2 */
#define	BHND_PCIE_TLP_SPTCTRLLEN		0x05C	/* Split Controller Req len */
#define	BHND_PCIE_TLP_SPTCTRLMSIC0		0x060	/* Split Controller Misc 0 */
#define	BHND_PCIE_TLP_SPTCTRLMSIC1		0x064	/* Split Controller Misc 1 */
#define	BHND_PCIE_TLP_BUSDEVFUNC		0x068	/* Bus/Device/Func */
#define	BHND_PCIE_TLP_RESETCTR			0x06C	/* Reset Counter */
#define	BHND_PCIE_TLP_RTRYBUF			0x070	/* Retry Buffer value */
#define	BHND_PCIE_TLP_TGTDEBUG1			0x074	/* Target Debug Reg1 */
#define	BHND_PCIE_TLP_TGTDEBUG2			0x078	/* Target Debug Reg2 */
#define	BHND_PCIE_TLP_TGTDEBUG3			0x07C	/* Target Debug Reg3 */
#define	BHND_PCIE_TLP_TGTDEBUG4			0x080	/* Target Debug Reg4 */


/*
 * PCIe-G1 SerDes MDIO Registers (>= rev10)
 */
#define BHND_PCIE_PHYADDR_SD		0x0	/* serdes PHY address */

#define	BHND_PCIE_SD_ADDREXT		0x1F	/* serdes address extension register */

#define	BHND_PCIE_SD_REGS_IEEE0		0x0000	/* IEEE0 AN CTRL block */
#define	BHND_PCIE_SD_REGS_IEEE1		0x0010	/* IEEE1 AN ADV block */
#define	BHND_PCIE_SD_REGS_BLK0		0x8000	/* ??? */
#define	BHND_PCIE_SD_REGS_BLK1		0x8010	/* ??? */
#define	BHND_PCIE_SD_REGS_BLK2		0x8020	/* ??? */
#define	BHND_PCIE_SD_REGS_BLK3		0x8030	/* ??? */
#define	BHND_PCIE_SD_REGS_BLK4		0x8040	/* ??? */
#define	BHND_PCIE_SD_REGS_PLL		0x8080	/* (?) PLL register block */
#define	BHND_PCIE_SD_REGS_TX0		0x8200	/* (?) Transmit 0 block */
#define	BHND_PCIE_SD_REGS_SERDESID	0x8310	/* ??? */
#define	BHND_PCIE_SD_REGS_RX0		0x8400	/* (?) Receive 0 register block */

/* The interpretation of these registers and values are just guesses based on
 * the limited available documentation from other (likely similar) Broadcom
 * SerDes IP. */
#define	BHND_PCIE_SD_TX_DRIVER			0x17	/* TX transmit driver register */
#define	  BHND_PCIE_SD_TX_DRIVER_IFIR_MASK	0x000E	/* unconfirmed */
#define	  BHND_PCIE_SD_TX_DRIVER_IFIR_SHIFT	1	/* unconfirmed */
#define	  BHND_PCIE_SD_TX_DRIVER_IPRE_MASK	0x00F0	/* unconfirmed */
#define	  BHND_PCIE_SD_TX_DRIVER_IPRE_SHIFT	4	/* unconfirmed */
#define	  BHND_PCIE_SD_TX_DRIVER_IDRIVER_MASK	0x0F00	/* unconfirmed */
#define	  BHND_PCIE_SD_TX_DRIVER_IDRIVER_SHIFT	8	/* unconfirmed */
#define	  BHND_PCIE_SD_TX_DRIVER_P2_COEFF_SHIFT	12	/* unconfirmed */
#define	  BHND_PCIE_SD_TX_DRIVER_P2_COEFF_MASK	0xF000	/* unconfirmed */

/* Constants used with host bridge quirk handling */
#define	BHND_PCIE_APPLE_TX_P2_COEFF_MAX		0x7	/* 9.6dB pre-emphassis coeff (???) */ 
#define	BHND_PCIE_APPLE_TX_IDRIVER_MAX		0xF	/* 1400mV voltage range (???) */

#define	BHND_PCIE_APPLE_TX_P2_COEFF_700MV	0x7	/* 2.3dB pre-emphassis coeff (???) */ 
#define	BHND_PCIE_APPLE_TX_IDRIVER_700MV	0x0	/* 670mV voltage range (???) */

/*
 * PCIe-G1 SerDes-R9 MDIO Registers (<= rev9)
 * 
 * These register definitions appear to match those provided in the
 * "PCI Express SerDes Registers" section of the BCM5761 Ethernet Controller 
 * Programmer's Reference Guide.
 */
#define	BHND_PCIE_PHY_SDR9_PLL       		0x1C	/* SerDes PLL PHY Address*/
#define	  BHND_PCIE_SDR9_PLL_CTRL		0x17	/* PLL control reg */
#define	    BHND_PCIE_SDR9_PLL_CTRL_FREQDET_EN	0x4000	/* bit 14 is FREQDET on */
#define	BHND_PCIE_PHY_SDR9_TXRX       	 	0x0F	/* SerDes RX/TX PHY Address */

#define	BHND_PCIE_SDR9_RX_CTRL			0x11	/* RX ctrl register */
#define	    BHND_PCIE_SDR9_RX_CTRL_FORCE	0x80	/* rxpolarity_force */
#define	    BHND_PCIE_SDR9_RX_CTRL_POLARITY_INV	0x40	/* rxpolarity_value (if set, inverse polarity) */

#define	BHND_PCIE_SDR9_RX_CDR			0x16	/* RX CDR ctrl register */
#define	  BHND_PCIE_SDR9_RX_CDR_FREQ_OVR_EN	0x0100	/* freq_override_en flag */
#define	  BHND_PCIE_SDR9_RX_CDR_FREQ_OVR_MASK	0x00FF	/* freq_override_val */
#define	  BHND_PCIE_SDR9_RX_CDR_FREQ_OVR_SHIFT	0

#define	BHND_PCIE_SDR9_RX_CDRBW			0x17	/* RX CDR bandwidth (PLL tuning) */
#define	  BHND_PCIE_SDR9_RX_CDRBW_INTGTRK_MASK	0x7000	/* integral loop bandwidth (phase tracking mode) */
#define	  BHND_PCIE_SDR9_RX_CDRBW_INTGTRK_SHIFT	11
#define	  BHND_PCIE_SDR9_RX_CDRBW_INTGACQ_MASK	0x0700	/* integral loop bandwidth (phase acquisition mode) */
#define	  BHND_PCIE_SDR9_RX_CDRBW_INTGACQ_SHIFT	8
#define	  BHND_PCIE_SDR9_RX_CDRBW_PROPTRK_MASK	0x0070	/* proportional loop bandwidth (phase tracking mode) */
#define	  BHND_PCIE_SDR9_RX_CDRBW_PROPTRK_SHIFT	4
#define	  BHND_PCIE_SDR9_RX_CDRBW_PROPACQ_MASK	0x0007	/* proportional loop bandwidth (phase acquisition mode) */
#define	  BHND_PCIE_SDR9_RX_CDRBW_PROPACQ_SHIFT	0

#define	BHND_PCIE_SDR9_RX_TIMER1		0x12	/* timer1 register */
#define	  BHND_PCIE_SDR9_RX_TIMER1_LKTRK_MASK	0xFF00	/* phase tracking delay before asserting RX seq completion (in 16ns units) */
#define	  BHND_PCIE_SDR9_RX_TIMER1_LKTRK_SHIFT	8
#define	  BHND_PCIE_SDR9_RX_TIMER1_LKACQ_MASK	0x00FF	/* phase acquisition mode time (in 1024ns units) */
#define	  BHND_PCIE_SDR9_RX_TIMER1_LKACQ_SHIFT	0


/* SPROM offsets */
#define	BHND_PCIE_SRSH_PI_OFFSET		BHND_PCI_SRSH_PI_OFFSET	/**< PCI base address bits in SPROM shadow area */
#define	BHND_PCIE_SRSH_PI_MASK			BHND_PCI_SRSH_PI_MASK	/**< bits 15:12 of the PCI core address */
#define	BHND_PCIE_SRSH_PI_SHIFT			BHND_PCI_SRSH_PI_SHIFT
#define	BHND_PCIE_SRSH_PI_ADDR_MASK		BHND_PCI_SRSH_PI_ADDR_MASK
#define	BHND_PCIE_SRSH_PI_ADDR_SHIFT		BHND_PCI_SRSH_PI_ADDR_SHIFT

#define	BHND_PCIE_SRSH_ASPM_OFFSET		8	/* word 4 */
#define	BHND_PCIE_SRSH_ASPM_ENB			0x18	/* bit 3, 4 */
#define	BHND_PCIE_SRSH_ASPM_L1_ENB		0x10	/* bit 4 */
#define	BHND_PCIE_SRSH_ASPM_L0s_ENB		0x8	/* bit 3 */
#define	BHND_PCIE_SRSH_PCIE_MISC_CONFIG		10	/* word 5 */
#define	BHND_PCIE_SRSH_L23READY_EXIT_NOPRST	0x8000	/* bit 15 */
#define	BHND_PCIE_SRSH_CLKREQ_OFFSET_R5		40	/* word 20 for srom rev <= 5 */
#define	BHND_PCIE_SRSH_CLKREQ_OFFSET_R8		104	/* word 52 for srom rev 8 */
#define	BHND_PCIE_SRSH_CLKREQ_ENB		0x0800	/* bit 11 */
#define	BHND_PCIE_SRSH_BD_OFFSET		12	/* word 6 */
#define	BHND_PCIE_SRSH_AUTOINIT_OFFSET		36	/* auto initialization enable */

/* Status reg PCIE_PLP_STATUSREG */
#define	BHND_PCIE_PLP_POLARITY_INV		0x10	/* lane polarity is inverted */

#endif /* _BHND_CORES_PCI_BHND_PCIREG_H_ */
