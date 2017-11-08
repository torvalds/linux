/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *	pci_regs.h
 *
 *	PCI standard defines
 *	Copyright 1994, Drew Eckhardt
 *	Copyright 1997--1999 Martin Mares <mj@ucw.cz>
 *
 *	For more information, please consult the following manuals (look at
 *	http://www.pcisig.com/ for how to get them):
 *
 *	PCI BIOS Specification
 *	PCI Local Bus Specification
 *	PCI to PCI Bridge Specification
 *	PCI System Design Guide
 *
 *	For HyperTransport information, please consult the following manuals
 *	from http://www.hypertransport.org
 *
 *	The HyperTransport I/O Link Specification
 */

#ifndef LINUX_PCI_REGS_H
#define LINUX_PCI_REGS_H

/*
 * Conventional PCI and PCI-X Mode 1 devices have 256 bytes of
 * configuration space.  PCI-X Mode 2 and PCIe devices have 4096 bytes of
 * configuration space.
 */
#define PCI_CFG_SPACE_SIZE	256
#define PCI_CFG_SPACE_EXP_SIZE	4096

/*
 * Under PCI, each device has 256 bytes of configuration address space,
 * of which the first 64 bytes are standardized as follows:
 */
#define PCI_STD_HEADER_SIZEOF	64
#define PCI_VENDOR_ID		0x00	/* 16 bits */
#define PCI_DEVICE_ID		0x02	/* 16 bits */
#define PCI_COMMAND		0x04	/* 16 bits */
#define  PCI_COMMAND_IO		0x1	/* Enable response in I/O space */
#define  PCI_COMMAND_MEMORY	0x2	/* Enable response in Memory space */
#define  PCI_COMMAND_MASTER	0x4	/* Enable bus mastering */
#define  PCI_COMMAND_SPECIAL	0x8	/* Enable response to special cycles */
#define  PCI_COMMAND_INVALIDATE	0x10	/* Use memory write and invalidate */
#define  PCI_COMMAND_VGA_PALETTE 0x20	/* Enable palette snooping */
#define  PCI_COMMAND_PARITY	0x40	/* Enable parity checking */
#define  PCI_COMMAND_WAIT	0x80	/* Enable address/data stepping */
#define  PCI_COMMAND_SERR	0x100	/* Enable SERR */
#define  PCI_COMMAND_FAST_BACK	0x200	/* Enable back-to-back writes */
#define  PCI_COMMAND_INTX_DISABLE 0x400 /* INTx Emulation Disable */

#define PCI_STATUS		0x06	/* 16 bits */
#define  PCI_STATUS_INTERRUPT	0x08	/* Interrupt status */
#define  PCI_STATUS_CAP_LIST	0x10	/* Support Capability List */
#define  PCI_STATUS_66MHZ	0x20	/* Support 66 MHz PCI 2.1 bus */
#define  PCI_STATUS_UDF		0x40	/* Support User Definable Features [obsolete] */
#define  PCI_STATUS_FAST_BACK	0x80	/* Accept fast-back to back */
#define  PCI_STATUS_PARITY	0x100	/* Detected parity error */
#define  PCI_STATUS_DEVSEL_MASK	0x600	/* DEVSEL timing */
#define  PCI_STATUS_DEVSEL_FAST		0x000
#define  PCI_STATUS_DEVSEL_MEDIUM	0x200
#define  PCI_STATUS_DEVSEL_SLOW		0x400
#define  PCI_STATUS_SIG_TARGET_ABORT	0x800 /* Set on target abort */
#define  PCI_STATUS_REC_TARGET_ABORT	0x1000 /* Master ack of " */
#define  PCI_STATUS_REC_MASTER_ABORT	0x2000 /* Set on master abort */
#define  PCI_STATUS_SIG_SYSTEM_ERROR	0x4000 /* Set when we drive SERR */
#define  PCI_STATUS_DETECTED_PARITY	0x8000 /* Set on parity error */

#define PCI_CLASS_REVISION	0x08	/* High 24 bits are class, low 8 revision */
#define PCI_REVISION_ID		0x08	/* Revision ID */
#define PCI_CLASS_PROG		0x09	/* Reg. Level Programming Interface */
#define PCI_CLASS_DEVICE	0x0a	/* Device class */

#define PCI_CACHE_LINE_SIZE	0x0c	/* 8 bits */
#define PCI_LATENCY_TIMER	0x0d	/* 8 bits */
#define PCI_HEADER_TYPE		0x0e	/* 8 bits */
#define  PCI_HEADER_TYPE_NORMAL		0
#define  PCI_HEADER_TYPE_BRIDGE		1
#define  PCI_HEADER_TYPE_CARDBUS	2

#define PCI_BIST		0x0f	/* 8 bits */
#define  PCI_BIST_CODE_MASK	0x0f	/* Return result */
#define  PCI_BIST_START		0x40	/* 1 to start BIST, 2 secs or less */
#define  PCI_BIST_CAPABLE	0x80	/* 1 if BIST capable */

/*
 * Base addresses specify locations in memory or I/O space.
 * Decoded size can be determined by writing a value of
 * 0xffffffff to the register, and reading it back.  Only
 * 1 bits are decoded.
 */
#define PCI_BASE_ADDRESS_0	0x10	/* 32 bits */
#define PCI_BASE_ADDRESS_1	0x14	/* 32 bits [htype 0,1 only] */
#define PCI_BASE_ADDRESS_2	0x18	/* 32 bits [htype 0 only] */
#define PCI_BASE_ADDRESS_3	0x1c	/* 32 bits */
#define PCI_BASE_ADDRESS_4	0x20	/* 32 bits */
#define PCI_BASE_ADDRESS_5	0x24	/* 32 bits */
#define  PCI_BASE_ADDRESS_SPACE		0x01	/* 0 = memory, 1 = I/O */
#define  PCI_BASE_ADDRESS_SPACE_IO	0x01
#define  PCI_BASE_ADDRESS_SPACE_MEMORY	0x00
#define  PCI_BASE_ADDRESS_MEM_TYPE_MASK	0x06
#define  PCI_BASE_ADDRESS_MEM_TYPE_32	0x00	/* 32 bit address */
#define  PCI_BASE_ADDRESS_MEM_TYPE_1M	0x02	/* Below 1M [obsolete] */
#define  PCI_BASE_ADDRESS_MEM_TYPE_64	0x04	/* 64 bit address */
#define  PCI_BASE_ADDRESS_MEM_PREFETCH	0x08	/* prefetchable? */
#define  PCI_BASE_ADDRESS_MEM_MASK	(~0x0fUL)
#define  PCI_BASE_ADDRESS_IO_MASK	(~0x03UL)
/* bit 1 is reserved if address_space = 1 */

/* Header type 0 (normal devices) */
#define PCI_CARDBUS_CIS		0x28
#define PCI_SUBSYSTEM_VENDOR_ID	0x2c
#define PCI_SUBSYSTEM_ID	0x2e
#define PCI_ROM_ADDRESS		0x30	/* Bits 31..11 are address, 10..1 reserved */
#define  PCI_ROM_ADDRESS_ENABLE	0x01
#define PCI_ROM_ADDRESS_MASK	(~0x7ffU)

#define PCI_CAPABILITY_LIST	0x34	/* Offset of first capability list entry */

/* 0x35-0x3b are reserved */
#define PCI_INTERRUPT_LINE	0x3c	/* 8 bits */
#define PCI_INTERRUPT_PIN	0x3d	/* 8 bits */
#define PCI_MIN_GNT		0x3e	/* 8 bits */
#define PCI_MAX_LAT		0x3f	/* 8 bits */

/* Header type 1 (PCI-to-PCI bridges) */
#define PCI_PRIMARY_BUS		0x18	/* Primary bus number */
#define PCI_SECONDARY_BUS	0x19	/* Secondary bus number */
#define PCI_SUBORDINATE_BUS	0x1a	/* Highest bus number behind the bridge */
#define PCI_SEC_LATENCY_TIMER	0x1b	/* Latency timer for secondary interface */
#define PCI_IO_BASE		0x1c	/* I/O range behind the bridge */
#define PCI_IO_LIMIT		0x1d
#define  PCI_IO_RANGE_TYPE_MASK	0x0fUL	/* I/O bridging type */
#define  PCI_IO_RANGE_TYPE_16	0x00
#define  PCI_IO_RANGE_TYPE_32	0x01
#define  PCI_IO_RANGE_MASK	(~0x0fUL) /* Standard 4K I/O windows */
#define  PCI_IO_1K_RANGE_MASK	(~0x03UL) /* Intel 1K I/O windows */
#define PCI_SEC_STATUS		0x1e	/* Secondary status register, only bit 14 used */
#define PCI_MEMORY_BASE		0x20	/* Memory range behind */
#define PCI_MEMORY_LIMIT	0x22
#define  PCI_MEMORY_RANGE_TYPE_MASK 0x0fUL
#define  PCI_MEMORY_RANGE_MASK	(~0x0fUL)
#define PCI_PREF_MEMORY_BASE	0x24	/* Prefetchable memory range behind */
#define PCI_PREF_MEMORY_LIMIT	0x26
#define  PCI_PREF_RANGE_TYPE_MASK 0x0fUL
#define  PCI_PREF_RANGE_TYPE_32	0x00
#define  PCI_PREF_RANGE_TYPE_64	0x01
#define  PCI_PREF_RANGE_MASK	(~0x0fUL)
#define PCI_PREF_BASE_UPPER32	0x28	/* Upper half of prefetchable memory range */
#define PCI_PREF_LIMIT_UPPER32	0x2c
#define PCI_IO_BASE_UPPER16	0x30	/* Upper half of I/O addresses */
#define PCI_IO_LIMIT_UPPER16	0x32
/* 0x34 same as for htype 0 */
/* 0x35-0x3b is reserved */
#define PCI_ROM_ADDRESS1	0x38	/* Same as PCI_ROM_ADDRESS, but for htype 1 */
/* 0x3c-0x3d are same as for htype 0 */
#define PCI_BRIDGE_CONTROL	0x3e
#define  PCI_BRIDGE_CTL_PARITY	0x01	/* Enable parity detection on secondary interface */
#define  PCI_BRIDGE_CTL_SERR	0x02	/* The same for SERR forwarding */
#define  PCI_BRIDGE_CTL_ISA	0x04	/* Enable ISA mode */
#define  PCI_BRIDGE_CTL_VGA	0x08	/* Forward VGA addresses */
#define  PCI_BRIDGE_CTL_MASTER_ABORT	0x20  /* Report master aborts */
#define  PCI_BRIDGE_CTL_BUS_RESET	0x40	/* Secondary bus reset */
#define  PCI_BRIDGE_CTL_FAST_BACK	0x80	/* Fast Back2Back enabled on secondary interface */

/* Header type 2 (CardBus bridges) */
#define PCI_CB_CAPABILITY_LIST	0x14
/* 0x15 reserved */
#define PCI_CB_SEC_STATUS	0x16	/* Secondary status */
#define PCI_CB_PRIMARY_BUS	0x18	/* PCI bus number */
#define PCI_CB_CARD_BUS		0x19	/* CardBus bus number */
#define PCI_CB_SUBORDINATE_BUS	0x1a	/* Subordinate bus number */
#define PCI_CB_LATENCY_TIMER	0x1b	/* CardBus latency timer */
#define PCI_CB_MEMORY_BASE_0	0x1c
#define PCI_CB_MEMORY_LIMIT_0	0x20
#define PCI_CB_MEMORY_BASE_1	0x24
#define PCI_CB_MEMORY_LIMIT_1	0x28
#define PCI_CB_IO_BASE_0	0x2c
#define PCI_CB_IO_BASE_0_HI	0x2e
#define PCI_CB_IO_LIMIT_0	0x30
#define PCI_CB_IO_LIMIT_0_HI	0x32
#define PCI_CB_IO_BASE_1	0x34
#define PCI_CB_IO_BASE_1_HI	0x36
#define PCI_CB_IO_LIMIT_1	0x38
#define PCI_CB_IO_LIMIT_1_HI	0x3a
#define  PCI_CB_IO_RANGE_MASK	(~0x03UL)
/* 0x3c-0x3d are same as for htype 0 */
#define PCI_CB_BRIDGE_CONTROL	0x3e
#define  PCI_CB_BRIDGE_CTL_PARITY	0x01	/* Similar to standard bridge control register */
#define  PCI_CB_BRIDGE_CTL_SERR		0x02
#define  PCI_CB_BRIDGE_CTL_ISA		0x04
#define  PCI_CB_BRIDGE_CTL_VGA		0x08
#define  PCI_CB_BRIDGE_CTL_MASTER_ABORT	0x20
#define  PCI_CB_BRIDGE_CTL_CB_RESET	0x40	/* CardBus reset */
#define  PCI_CB_BRIDGE_CTL_16BIT_INT	0x80	/* Enable interrupt for 16-bit cards */
#define  PCI_CB_BRIDGE_CTL_PREFETCH_MEM0 0x100	/* Prefetch enable for both memory regions */
#define  PCI_CB_BRIDGE_CTL_PREFETCH_MEM1 0x200
#define  PCI_CB_BRIDGE_CTL_POST_WRITES	0x400
#define PCI_CB_SUBSYSTEM_VENDOR_ID	0x40
#define PCI_CB_SUBSYSTEM_ID		0x42
#define PCI_CB_LEGACY_MODE_BASE		0x44	/* 16-bit PC Card legacy mode base address (ExCa) */
/* 0x48-0x7f reserved */

/* Capability lists */

#define PCI_CAP_LIST_ID		0	/* Capability ID */
#define  PCI_CAP_ID_PM		0x01	/* Power Management */
#define  PCI_CAP_ID_AGP		0x02	/* Accelerated Graphics Port */
#define  PCI_CAP_ID_VPD		0x03	/* Vital Product Data */
#define  PCI_CAP_ID_SLOTID	0x04	/* Slot Identification */
#define  PCI_CAP_ID_MSI		0x05	/* Message Signalled Interrupts */
#define  PCI_CAP_ID_CHSWP	0x06	/* CompactPCI HotSwap */
#define  PCI_CAP_ID_PCIX	0x07	/* PCI-X */
#define  PCI_CAP_ID_HT		0x08	/* HyperTransport */
#define  PCI_CAP_ID_VNDR	0x09	/* Vendor-Specific */
#define  PCI_CAP_ID_DBG		0x0A	/* Debug port */
#define  PCI_CAP_ID_CCRC	0x0B	/* CompactPCI Central Resource Control */
#define  PCI_CAP_ID_SHPC	0x0C	/* PCI Standard Hot-Plug Controller */
#define  PCI_CAP_ID_SSVID	0x0D	/* Bridge subsystem vendor/device ID */
#define  PCI_CAP_ID_AGP3	0x0E	/* AGP Target PCI-PCI bridge */
#define  PCI_CAP_ID_SECDEV	0x0F	/* Secure Device */
#define  PCI_CAP_ID_EXP		0x10	/* PCI Express */
#define  PCI_CAP_ID_MSIX	0x11	/* MSI-X */
#define  PCI_CAP_ID_SATA	0x12	/* SATA Data/Index Conf. */
#define  PCI_CAP_ID_AF		0x13	/* PCI Advanced Features */
#define  PCI_CAP_ID_EA		0x14	/* PCI Enhanced Allocation */
#define  PCI_CAP_ID_MAX		PCI_CAP_ID_EA
#define PCI_CAP_LIST_NEXT	1	/* Next capability in the list */
#define PCI_CAP_FLAGS		2	/* Capability defined flags (16 bits) */
#define PCI_CAP_SIZEOF		4

/* Power Management Registers */

#define PCI_PM_PMC		2	/* PM Capabilities Register */
#define  PCI_PM_CAP_VER_MASK	0x0007	/* Version */
#define  PCI_PM_CAP_PME_CLOCK	0x0008	/* PME clock required */
#define  PCI_PM_CAP_RESERVED    0x0010  /* Reserved field */
#define  PCI_PM_CAP_DSI		0x0020	/* Device specific initialization */
#define  PCI_PM_CAP_AUX_POWER	0x01C0	/* Auxiliary power support mask */
#define  PCI_PM_CAP_D1		0x0200	/* D1 power state support */
#define  PCI_PM_CAP_D2		0x0400	/* D2 power state support */
#define  PCI_PM_CAP_PME		0x0800	/* PME pin supported */
#define  PCI_PM_CAP_PME_MASK	0xF800	/* PME Mask of all supported states */
#define  PCI_PM_CAP_PME_D0	0x0800	/* PME# from D0 */
#define  PCI_PM_CAP_PME_D1	0x1000	/* PME# from D1 */
#define  PCI_PM_CAP_PME_D2	0x2000	/* PME# from D2 */
#define  PCI_PM_CAP_PME_D3	0x4000	/* PME# from D3 (hot) */
#define  PCI_PM_CAP_PME_D3cold	0x8000	/* PME# from D3 (cold) */
#define  PCI_PM_CAP_PME_SHIFT	11	/* Start of the PME Mask in PMC */
#define PCI_PM_CTRL		4	/* PM control and status register */
#define  PCI_PM_CTRL_STATE_MASK	0x0003	/* Current power state (D0 to D3) */
#define  PCI_PM_CTRL_NO_SOFT_RESET	0x0008	/* No reset for D3hot->D0 */
#define  PCI_PM_CTRL_PME_ENABLE	0x0100	/* PME pin enable */
#define  PCI_PM_CTRL_DATA_SEL_MASK	0x1e00	/* Data select (??) */
#define  PCI_PM_CTRL_DATA_SCALE_MASK	0x6000	/* Data scale (??) */
#define  PCI_PM_CTRL_PME_STATUS	0x8000	/* PME pin status */
#define PCI_PM_PPB_EXTENSIONS	6	/* PPB support extensions (??) */
#define  PCI_PM_PPB_B2_B3	0x40	/* Stop clock when in D3hot (??) */
#define  PCI_PM_BPCC_ENABLE	0x80	/* Bus power/clock control enable (??) */
#define PCI_PM_DATA_REGISTER	7	/* (??) */
#define PCI_PM_SIZEOF		8

/* AGP registers */

#define PCI_AGP_VERSION		2	/* BCD version number */
#define PCI_AGP_RFU		3	/* Rest of capability flags */
#define PCI_AGP_STATUS		4	/* Status register */
#define  PCI_AGP_STATUS_RQ_MASK	0xff000000	/* Maximum number of requests - 1 */
#define  PCI_AGP_STATUS_SBA	0x0200	/* Sideband addressing supported */
#define  PCI_AGP_STATUS_64BIT	0x0020	/* 64-bit addressing supported */
#define  PCI_AGP_STATUS_FW	0x0010	/* FW transfers supported */
#define  PCI_AGP_STATUS_RATE4	0x0004	/* 4x transfer rate supported */
#define  PCI_AGP_STATUS_RATE2	0x0002	/* 2x transfer rate supported */
#define  PCI_AGP_STATUS_RATE1	0x0001	/* 1x transfer rate supported */
#define PCI_AGP_COMMAND		8	/* Control register */
#define  PCI_AGP_COMMAND_RQ_MASK 0xff000000  /* Master: Maximum number of requests */
#define  PCI_AGP_COMMAND_SBA	0x0200	/* Sideband addressing enabled */
#define  PCI_AGP_COMMAND_AGP	0x0100	/* Allow processing of AGP transactions */
#define  PCI_AGP_COMMAND_64BIT	0x0020	/* Allow processing of 64-bit addresses */
#define  PCI_AGP_COMMAND_FW	0x0010	/* Force FW transfers */
#define  PCI_AGP_COMMAND_RATE4	0x0004	/* Use 4x rate */
#define  PCI_AGP_COMMAND_RATE2	0x0002	/* Use 2x rate */
#define  PCI_AGP_COMMAND_RATE1	0x0001	/* Use 1x rate */
#define PCI_AGP_SIZEOF		12

/* Vital Product Data */

#define PCI_VPD_ADDR		2	/* Address to access (15 bits!) */
#define  PCI_VPD_ADDR_MASK	0x7fff	/* Address mask */
#define  PCI_VPD_ADDR_F		0x8000	/* Write 0, 1 indicates completion */
#define PCI_VPD_DATA		4	/* 32-bits of data returned here */
#define PCI_CAP_VPD_SIZEOF	8

/* Slot Identification */

#define PCI_SID_ESR		2	/* Expansion Slot Register */
#define  PCI_SID_ESR_NSLOTS	0x1f	/* Number of expansion slots available */
#define  PCI_SID_ESR_FIC	0x20	/* First In Chassis Flag */
#define PCI_SID_CHASSIS_NR	3	/* Chassis Number */

/* Message Signalled Interrupts registers */

#define PCI_MSI_FLAGS		2	/* Message Control */
#define  PCI_MSI_FLAGS_ENABLE	0x0001	/* MSI feature enabled */
#define  PCI_MSI_FLAGS_QMASK	0x000e	/* Maximum queue size available */
#define  PCI_MSI_FLAGS_QSIZE	0x0070	/* Message queue size configured */
#define  PCI_MSI_FLAGS_64BIT	0x0080	/* 64-bit addresses allowed */
#define  PCI_MSI_FLAGS_MASKBIT	0x0100	/* Per-vector masking capable */
#define PCI_MSI_RFU		3	/* Rest of capability flags */
#define PCI_MSI_ADDRESS_LO	4	/* Lower 32 bits */
#define PCI_MSI_ADDRESS_HI	8	/* Upper 32 bits (if PCI_MSI_FLAGS_64BIT set) */
#define PCI_MSI_DATA_32		8	/* 16 bits of data for 32-bit devices */
#define PCI_MSI_MASK_32		12	/* Mask bits register for 32-bit devices */
#define PCI_MSI_PENDING_32	16	/* Pending intrs for 32-bit devices */
#define PCI_MSI_DATA_64		12	/* 16 bits of data for 64-bit devices */
#define PCI_MSI_MASK_64		16	/* Mask bits register for 64-bit devices */
#define PCI_MSI_PENDING_64	20	/* Pending intrs for 64-bit devices */

/* MSI-X registers */
#define PCI_MSIX_FLAGS		2	/* Message Control */
#define  PCI_MSIX_FLAGS_QSIZE	0x07FF	/* Table size */
#define  PCI_MSIX_FLAGS_MASKALL	0x4000	/* Mask all vectors for this function */
#define  PCI_MSIX_FLAGS_ENABLE	0x8000	/* MSI-X enable */
#define PCI_MSIX_TABLE		4	/* Table offset */
#define  PCI_MSIX_TABLE_BIR	0x00000007 /* BAR index */
#define  PCI_MSIX_TABLE_OFFSET	0xfffffff8 /* Offset into specified BAR */
#define PCI_MSIX_PBA		8	/* Pending Bit Array offset */
#define  PCI_MSIX_PBA_BIR	0x00000007 /* BAR index */
#define  PCI_MSIX_PBA_OFFSET	0xfffffff8 /* Offset into specified BAR */
#define PCI_MSIX_FLAGS_BIRMASK	PCI_MSIX_PBA_BIR /* deprecated */
#define PCI_CAP_MSIX_SIZEOF	12	/* size of MSIX registers */

/* MSI-X Table entry format */
#define PCI_MSIX_ENTRY_SIZE		16
#define  PCI_MSIX_ENTRY_LOWER_ADDR	0
#define  PCI_MSIX_ENTRY_UPPER_ADDR	4
#define  PCI_MSIX_ENTRY_DATA		8
#define  PCI_MSIX_ENTRY_VECTOR_CTRL	12
#define   PCI_MSIX_ENTRY_CTRL_MASKBIT	1

/* CompactPCI Hotswap Register */

#define PCI_CHSWP_CSR		2	/* Control and Status Register */
#define  PCI_CHSWP_DHA		0x01	/* Device Hiding Arm */
#define  PCI_CHSWP_EIM		0x02	/* ENUM# Signal Mask */
#define  PCI_CHSWP_PIE		0x04	/* Pending Insert or Extract */
#define  PCI_CHSWP_LOO		0x08	/* LED On / Off */
#define  PCI_CHSWP_PI		0x30	/* Programming Interface */
#define  PCI_CHSWP_EXT		0x40	/* ENUM# status - extraction */
#define  PCI_CHSWP_INS		0x80	/* ENUM# status - insertion */

/* PCI Advanced Feature registers */

#define PCI_AF_LENGTH		2
#define PCI_AF_CAP		3
#define  PCI_AF_CAP_TP		0x01
#define  PCI_AF_CAP_FLR		0x02
#define PCI_AF_CTRL		4
#define  PCI_AF_CTRL_FLR	0x01
#define PCI_AF_STATUS		5
#define  PCI_AF_STATUS_TP	0x01
#define PCI_CAP_AF_SIZEOF	6	/* size of AF registers */

/* PCI Enhanced Allocation registers */

#define PCI_EA_NUM_ENT		2	/* Number of Capability Entries */
#define  PCI_EA_NUM_ENT_MASK	0x3f	/* Num Entries Mask */
#define PCI_EA_FIRST_ENT	4	/* First EA Entry in List */
#define PCI_EA_FIRST_ENT_BRIDGE	8	/* First EA Entry for Bridges */
#define  PCI_EA_ES		0x00000007 /* Entry Size */
#define  PCI_EA_BEI		0x000000f0 /* BAR Equivalent Indicator */
/* 0-5 map to BARs 0-5 respectively */
#define   PCI_EA_BEI_BAR0		0
#define   PCI_EA_BEI_BAR5		5
#define   PCI_EA_BEI_BRIDGE		6	/* Resource behind bridge */
#define   PCI_EA_BEI_ENI		7	/* Equivalent Not Indicated */
#define   PCI_EA_BEI_ROM		8	/* Expansion ROM */
/* 9-14 map to VF BARs 0-5 respectively */
#define   PCI_EA_BEI_VF_BAR0		9
#define   PCI_EA_BEI_VF_BAR5		14
#define   PCI_EA_BEI_RESERVED		15	/* Reserved - Treat like ENI */
#define  PCI_EA_PP		0x0000ff00	/* Primary Properties */
#define  PCI_EA_SP		0x00ff0000	/* Secondary Properties */
#define   PCI_EA_P_MEM			0x00	/* Non-Prefetch Memory */
#define   PCI_EA_P_MEM_PREFETCH		0x01	/* Prefetchable Memory */
#define   PCI_EA_P_IO			0x02	/* I/O Space */
#define   PCI_EA_P_VF_MEM_PREFETCH	0x03	/* VF Prefetchable Memory */
#define   PCI_EA_P_VF_MEM		0x04	/* VF Non-Prefetch Memory */
#define   PCI_EA_P_BRIDGE_MEM		0x05	/* Bridge Non-Prefetch Memory */
#define   PCI_EA_P_BRIDGE_MEM_PREFETCH	0x06	/* Bridge Prefetchable Memory */
#define   PCI_EA_P_BRIDGE_IO		0x07	/* Bridge I/O Space */
/* 0x08-0xfc reserved */
#define   PCI_EA_P_MEM_RESERVED		0xfd	/* Reserved Memory */
#define   PCI_EA_P_IO_RESERVED		0xfe	/* Reserved I/O Space */
#define   PCI_EA_P_UNAVAILABLE		0xff	/* Entry Unavailable */
#define  PCI_EA_WRITABLE	0x40000000	/* Writable: 1 = RW, 0 = HwInit */
#define  PCI_EA_ENABLE		0x80000000	/* Enable for this entry */
#define PCI_EA_BASE		4		/* Base Address Offset */
#define PCI_EA_MAX_OFFSET	8		/* MaxOffset (resource length) */
/* bit 0 is reserved */
#define  PCI_EA_IS_64		0x00000002	/* 64-bit field flag */
#define  PCI_EA_FIELD_MASK	0xfffffffc	/* For Base & Max Offset */

/* PCI-X registers (Type 0 (non-bridge) devices) */

#define PCI_X_CMD		2	/* Modes & Features */
#define  PCI_X_CMD_DPERR_E	0x0001	/* Data Parity Error Recovery Enable */
#define  PCI_X_CMD_ERO		0x0002	/* Enable Relaxed Ordering */
#define  PCI_X_CMD_READ_512	0x0000	/* 512 byte maximum read byte count */
#define  PCI_X_CMD_READ_1K	0x0004	/* 1Kbyte maximum read byte count */
#define  PCI_X_CMD_READ_2K	0x0008	/* 2Kbyte maximum read byte count */
#define  PCI_X_CMD_READ_4K	0x000c	/* 4Kbyte maximum read byte count */
#define  PCI_X_CMD_MAX_READ	0x000c	/* Max Memory Read Byte Count */
				/* Max # of outstanding split transactions */
#define  PCI_X_CMD_SPLIT_1	0x0000	/* Max 1 */
#define  PCI_X_CMD_SPLIT_2	0x0010	/* Max 2 */
#define  PCI_X_CMD_SPLIT_3	0x0020	/* Max 3 */
#define  PCI_X_CMD_SPLIT_4	0x0030	/* Max 4 */
#define  PCI_X_CMD_SPLIT_8	0x0040	/* Max 8 */
#define  PCI_X_CMD_SPLIT_12	0x0050	/* Max 12 */
#define  PCI_X_CMD_SPLIT_16	0x0060	/* Max 16 */
#define  PCI_X_CMD_SPLIT_32	0x0070	/* Max 32 */
#define  PCI_X_CMD_MAX_SPLIT	0x0070	/* Max Outstanding Split Transactions */
#define  PCI_X_CMD_VERSION(x)	(((x) >> 12) & 3) /* Version */
#define PCI_X_STATUS		4	/* PCI-X capabilities */
#define  PCI_X_STATUS_DEVFN	0x000000ff	/* A copy of devfn */
#define  PCI_X_STATUS_BUS	0x0000ff00	/* A copy of bus nr */
#define  PCI_X_STATUS_64BIT	0x00010000	/* 64-bit device */
#define  PCI_X_STATUS_133MHZ	0x00020000	/* 133 MHz capable */
#define  PCI_X_STATUS_SPL_DISC	0x00040000	/* Split Completion Discarded */
#define  PCI_X_STATUS_UNX_SPL	0x00080000	/* Unexpected Split Completion */
#define  PCI_X_STATUS_COMPLEX	0x00100000	/* Device Complexity */
#define  PCI_X_STATUS_MAX_READ	0x00600000	/* Designed Max Memory Read Count */
#define  PCI_X_STATUS_MAX_SPLIT	0x03800000	/* Designed Max Outstanding Split Transactions */
#define  PCI_X_STATUS_MAX_CUM	0x1c000000	/* Designed Max Cumulative Read Size */
#define  PCI_X_STATUS_SPL_ERR	0x20000000	/* Rcvd Split Completion Error Msg */
#define  PCI_X_STATUS_266MHZ	0x40000000	/* 266 MHz capable */
#define  PCI_X_STATUS_533MHZ	0x80000000	/* 533 MHz capable */
#define PCI_X_ECC_CSR		8	/* ECC control and status */
#define PCI_CAP_PCIX_SIZEOF_V0	8	/* size of registers for Version 0 */
#define PCI_CAP_PCIX_SIZEOF_V1	24	/* size for Version 1 */
#define PCI_CAP_PCIX_SIZEOF_V2	PCI_CAP_PCIX_SIZEOF_V1	/* Same for v2 */

/* PCI-X registers (Type 1 (bridge) devices) */

#define PCI_X_BRIDGE_SSTATUS	2	/* Secondary Status */
#define  PCI_X_SSTATUS_64BIT	0x0001	/* Secondary AD interface is 64 bits */
#define  PCI_X_SSTATUS_133MHZ	0x0002	/* 133 MHz capable */
#define  PCI_X_SSTATUS_FREQ	0x03c0	/* Secondary Bus Mode and Frequency */
#define  PCI_X_SSTATUS_VERS	0x3000	/* PCI-X Capability Version */
#define  PCI_X_SSTATUS_V1	0x1000	/* Mode 2, not Mode 1 */
#define  PCI_X_SSTATUS_V2	0x2000	/* Mode 1 or Modes 1 and 2 */
#define  PCI_X_SSTATUS_266MHZ	0x4000	/* 266 MHz capable */
#define  PCI_X_SSTATUS_533MHZ	0x8000	/* 533 MHz capable */
#define PCI_X_BRIDGE_STATUS	4	/* Bridge Status */

/* PCI Bridge Subsystem ID registers */

#define PCI_SSVID_VENDOR_ID     4	/* PCI Bridge subsystem vendor ID */
#define PCI_SSVID_DEVICE_ID     6	/* PCI Bridge subsystem device ID */

/* PCI Express capability registers */

#define PCI_EXP_FLAGS		2	/* Capabilities register */
#define PCI_EXP_FLAGS_VERS	0x000f	/* Capability version */
#define PCI_EXP_FLAGS_TYPE	0x00f0	/* Device/Port type */
#define  PCI_EXP_TYPE_ENDPOINT	0x0	/* Express Endpoint */
#define  PCI_EXP_TYPE_LEG_END	0x1	/* Legacy Endpoint */
#define  PCI_EXP_TYPE_ROOT_PORT 0x4	/* Root Port */
#define  PCI_EXP_TYPE_UPSTREAM	0x5	/* Upstream Port */
#define  PCI_EXP_TYPE_DOWNSTREAM 0x6	/* Downstream Port */
#define  PCI_EXP_TYPE_PCI_BRIDGE 0x7	/* PCIe to PCI/PCI-X Bridge */
#define  PCI_EXP_TYPE_PCIE_BRIDGE 0x8	/* PCI/PCI-X to PCIe Bridge */
#define  PCI_EXP_TYPE_RC_END	0x9	/* Root Complex Integrated Endpoint */
#define  PCI_EXP_TYPE_RC_EC	0xa	/* Root Complex Event Collector */
#define PCI_EXP_FLAGS_SLOT	0x0100	/* Slot implemented */
#define PCI_EXP_FLAGS_IRQ	0x3e00	/* Interrupt message number */
#define PCI_EXP_DEVCAP		4	/* Device capabilities */
#define  PCI_EXP_DEVCAP_PAYLOAD	0x00000007 /* Max_Payload_Size */
#define  PCI_EXP_DEVCAP_PHANTOM	0x00000018 /* Phantom functions */
#define  PCI_EXP_DEVCAP_EXT_TAG	0x00000020 /* Extended tags */
#define  PCI_EXP_DEVCAP_L0S	0x000001c0 /* L0s Acceptable Latency */
#define  PCI_EXP_DEVCAP_L1	0x00000e00 /* L1 Acceptable Latency */
#define  PCI_EXP_DEVCAP_ATN_BUT	0x00001000 /* Attention Button Present */
#define  PCI_EXP_DEVCAP_ATN_IND	0x00002000 /* Attention Indicator Present */
#define  PCI_EXP_DEVCAP_PWR_IND	0x00004000 /* Power Indicator Present */
#define  PCI_EXP_DEVCAP_RBER	0x00008000 /* Role-Based Error Reporting */
#define  PCI_EXP_DEVCAP_PWR_VAL	0x03fc0000 /* Slot Power Limit Value */
#define  PCI_EXP_DEVCAP_PWR_SCL	0x0c000000 /* Slot Power Limit Scale */
#define  PCI_EXP_DEVCAP_FLR     0x10000000 /* Function Level Reset */
#define PCI_EXP_DEVCTL		8	/* Device Control */
#define  PCI_EXP_DEVCTL_CERE	0x0001	/* Correctable Error Reporting En. */
#define  PCI_EXP_DEVCTL_NFERE	0x0002	/* Non-Fatal Error Reporting Enable */
#define  PCI_EXP_DEVCTL_FERE	0x0004	/* Fatal Error Reporting Enable */
#define  PCI_EXP_DEVCTL_URRE	0x0008	/* Unsupported Request Reporting En. */
#define  PCI_EXP_DEVCTL_RELAX_EN 0x0010 /* Enable relaxed ordering */
#define  PCI_EXP_DEVCTL_PAYLOAD	0x00e0	/* Max_Payload_Size */
#define  PCI_EXP_DEVCTL_EXT_TAG	0x0100	/* Extended Tag Field Enable */
#define  PCI_EXP_DEVCTL_PHANTOM	0x0200	/* Phantom Functions Enable */
#define  PCI_EXP_DEVCTL_AUX_PME	0x0400	/* Auxiliary Power PM Enable */
#define  PCI_EXP_DEVCTL_NOSNOOP_EN 0x0800  /* Enable No Snoop */
#define  PCI_EXP_DEVCTL_READRQ	0x7000	/* Max_Read_Request_Size */
#define  PCI_EXP_DEVCTL_READRQ_128B  0x0000 /* 128 Bytes */
#define  PCI_EXP_DEVCTL_READRQ_256B  0x1000 /* 256 Bytes */
#define  PCI_EXP_DEVCTL_READRQ_512B  0x2000 /* 512 Bytes */
#define  PCI_EXP_DEVCTL_READRQ_1024B 0x3000 /* 1024 Bytes */
#define  PCI_EXP_DEVCTL_BCR_FLR 0x8000  /* Bridge Configuration Retry / FLR */
#define PCI_EXP_DEVSTA		10	/* Device Status */
#define  PCI_EXP_DEVSTA_CED	0x0001	/* Correctable Error Detected */
#define  PCI_EXP_DEVSTA_NFED	0x0002	/* Non-Fatal Error Detected */
#define  PCI_EXP_DEVSTA_FED	0x0004	/* Fatal Error Detected */
#define  PCI_EXP_DEVSTA_URD	0x0008	/* Unsupported Request Detected */
#define  PCI_EXP_DEVSTA_AUXPD	0x0010	/* AUX Power Detected */
#define  PCI_EXP_DEVSTA_TRPND	0x0020	/* Transactions Pending */
#define PCI_CAP_EXP_RC_ENDPOINT_SIZEOF_V1	12	/* v1 endpoints without link end here */
#define PCI_EXP_LNKCAP		12	/* Link Capabilities */
#define  PCI_EXP_LNKCAP_SLS	0x0000000f /* Supported Link Speeds */
#define  PCI_EXP_LNKCAP_SLS_2_5GB 0x00000001 /* LNKCAP2 SLS Vector bit 0 */
#define  PCI_EXP_LNKCAP_SLS_5_0GB 0x00000002 /* LNKCAP2 SLS Vector bit 1 */
#define  PCI_EXP_LNKCAP_SLS_8_0GB 0x00000003 /* LNKCAP2 SLS Vector bit 2 */
#define  PCI_EXP_LNKCAP_MLW	0x000003f0 /* Maximum Link Width */
#define  PCI_EXP_LNKCAP_ASPMS	0x00000c00 /* ASPM Support */
#define  PCI_EXP_LNKCAP_L0SEL	0x00007000 /* L0s Exit Latency */
#define  PCI_EXP_LNKCAP_L1EL	0x00038000 /* L1 Exit Latency */
#define  PCI_EXP_LNKCAP_CLKPM	0x00040000 /* Clock Power Management */
#define  PCI_EXP_LNKCAP_SDERC	0x00080000 /* Surprise Down Error Reporting Capable */
#define  PCI_EXP_LNKCAP_DLLLARC	0x00100000 /* Data Link Layer Link Active Reporting Capable */
#define  PCI_EXP_LNKCAP_LBNC	0x00200000 /* Link Bandwidth Notification Capability */
#define  PCI_EXP_LNKCAP_PN	0xff000000 /* Port Number */
#define PCI_EXP_LNKCTL		16	/* Link Control */
#define  PCI_EXP_LNKCTL_ASPMC	0x0003	/* ASPM Control */
#define  PCI_EXP_LNKCTL_ASPM_L0S 0x0001	/* L0s Enable */
#define  PCI_EXP_LNKCTL_ASPM_L1  0x0002	/* L1 Enable */
#define  PCI_EXP_LNKCTL_RCB	0x0008	/* Read Completion Boundary */
#define  PCI_EXP_LNKCTL_LD	0x0010	/* Link Disable */
#define  PCI_EXP_LNKCTL_RL	0x0020	/* Retrain Link */
#define  PCI_EXP_LNKCTL_CCC	0x0040	/* Common Clock Configuration */
#define  PCI_EXP_LNKCTL_ES	0x0080	/* Extended Synch */
#define  PCI_EXP_LNKCTL_CLKREQ_EN 0x0100 /* Enable clkreq */
#define  PCI_EXP_LNKCTL_HAWD	0x0200	/* Hardware Autonomous Width Disable */
#define  PCI_EXP_LNKCTL_LBMIE	0x0400	/* Link Bandwidth Management Interrupt Enable */
#define  PCI_EXP_LNKCTL_LABIE	0x0800	/* Link Autonomous Bandwidth Interrupt Enable */
#define PCI_EXP_LNKSTA		18	/* Link Status */
#define  PCI_EXP_LNKSTA_CLS	0x000f	/* Current Link Speed */
#define  PCI_EXP_LNKSTA_CLS_2_5GB 0x0001 /* Current Link Speed 2.5GT/s */
#define  PCI_EXP_LNKSTA_CLS_5_0GB 0x0002 /* Current Link Speed 5.0GT/s */
#define  PCI_EXP_LNKSTA_CLS_8_0GB 0x0003 /* Current Link Speed 8.0GT/s */
#define  PCI_EXP_LNKSTA_NLW	0x03f0	/* Negotiated Link Width */
#define  PCI_EXP_LNKSTA_NLW_X1	0x0010	/* Current Link Width x1 */
#define  PCI_EXP_LNKSTA_NLW_X2	0x0020	/* Current Link Width x2 */
#define  PCI_EXP_LNKSTA_NLW_X4	0x0040	/* Current Link Width x4 */
#define  PCI_EXP_LNKSTA_NLW_X8	0x0080	/* Current Link Width x8 */
#define  PCI_EXP_LNKSTA_NLW_SHIFT 4	/* start of NLW mask in link status */
#define  PCI_EXP_LNKSTA_LT	0x0800	/* Link Training */
#define  PCI_EXP_LNKSTA_SLC	0x1000	/* Slot Clock Configuration */
#define  PCI_EXP_LNKSTA_DLLLA	0x2000	/* Data Link Layer Link Active */
#define  PCI_EXP_LNKSTA_LBMS	0x4000	/* Link Bandwidth Management Status */
#define  PCI_EXP_LNKSTA_LABS	0x8000	/* Link Autonomous Bandwidth Status */
#define PCI_CAP_EXP_ENDPOINT_SIZEOF_V1	20	/* v1 endpoints with link end here */
#define PCI_EXP_SLTCAP		20	/* Slot Capabilities */
#define  PCI_EXP_SLTCAP_ABP	0x00000001 /* Attention Button Present */
#define  PCI_EXP_SLTCAP_PCP	0x00000002 /* Power Controller Present */
#define  PCI_EXP_SLTCAP_MRLSP	0x00000004 /* MRL Sensor Present */
#define  PCI_EXP_SLTCAP_AIP	0x00000008 /* Attention Indicator Present */
#define  PCI_EXP_SLTCAP_PIP	0x00000010 /* Power Indicator Present */
#define  PCI_EXP_SLTCAP_HPS	0x00000020 /* Hot-Plug Surprise */
#define  PCI_EXP_SLTCAP_HPC	0x00000040 /* Hot-Plug Capable */
#define  PCI_EXP_SLTCAP_SPLV	0x00007f80 /* Slot Power Limit Value */
#define  PCI_EXP_SLTCAP_SPLS	0x00018000 /* Slot Power Limit Scale */
#define  PCI_EXP_SLTCAP_EIP	0x00020000 /* Electromechanical Interlock Present */
#define  PCI_EXP_SLTCAP_NCCS	0x00040000 /* No Command Completed Support */
#define  PCI_EXP_SLTCAP_PSN	0xfff80000 /* Physical Slot Number */
#define PCI_EXP_SLTCTL		24	/* Slot Control */
#define  PCI_EXP_SLTCTL_ABPE	0x0001	/* Attention Button Pressed Enable */
#define  PCI_EXP_SLTCTL_PFDE	0x0002	/* Power Fault Detected Enable */
#define  PCI_EXP_SLTCTL_MRLSCE	0x0004	/* MRL Sensor Changed Enable */
#define  PCI_EXP_SLTCTL_PDCE	0x0008	/* Presence Detect Changed Enable */
#define  PCI_EXP_SLTCTL_CCIE	0x0010	/* Command Completed Interrupt Enable */
#define  PCI_EXP_SLTCTL_HPIE	0x0020	/* Hot-Plug Interrupt Enable */
#define  PCI_EXP_SLTCTL_AIC	0x00c0	/* Attention Indicator Control */
#define  PCI_EXP_SLTCTL_ATTN_IND_ON    0x0040 /* Attention Indicator on */
#define  PCI_EXP_SLTCTL_ATTN_IND_BLINK 0x0080 /* Attention Indicator blinking */
#define  PCI_EXP_SLTCTL_ATTN_IND_OFF   0x00c0 /* Attention Indicator off */
#define  PCI_EXP_SLTCTL_PIC	0x0300	/* Power Indicator Control */
#define  PCI_EXP_SLTCTL_PWR_IND_ON     0x0100 /* Power Indicator on */
#define  PCI_EXP_SLTCTL_PWR_IND_BLINK  0x0200 /* Power Indicator blinking */
#define  PCI_EXP_SLTCTL_PWR_IND_OFF    0x0300 /* Power Indicator off */
#define  PCI_EXP_SLTCTL_PCC	0x0400	/* Power Controller Control */
#define  PCI_EXP_SLTCTL_PWR_ON         0x0000 /* Power On */
#define  PCI_EXP_SLTCTL_PWR_OFF        0x0400 /* Power Off */
#define  PCI_EXP_SLTCTL_EIC	0x0800	/* Electromechanical Interlock Control */
#define  PCI_EXP_SLTCTL_DLLSCE	0x1000	/* Data Link Layer State Changed Enable */
#define PCI_EXP_SLTSTA		26	/* Slot Status */
#define  PCI_EXP_SLTSTA_ABP	0x0001	/* Attention Button Pressed */
#define  PCI_EXP_SLTSTA_PFD	0x0002	/* Power Fault Detected */
#define  PCI_EXP_SLTSTA_MRLSC	0x0004	/* MRL Sensor Changed */
#define  PCI_EXP_SLTSTA_PDC	0x0008	/* Presence Detect Changed */
#define  PCI_EXP_SLTSTA_CC	0x0010	/* Command Completed */
#define  PCI_EXP_SLTSTA_MRLSS	0x0020	/* MRL Sensor State */
#define  PCI_EXP_SLTSTA_PDS	0x0040	/* Presence Detect State */
#define  PCI_EXP_SLTSTA_EIS	0x0080	/* Electromechanical Interlock Status */
#define  PCI_EXP_SLTSTA_DLLSC	0x0100	/* Data Link Layer State Changed */
#define PCI_EXP_RTCTL		28	/* Root Control */
#define  PCI_EXP_RTCTL_SECEE	0x0001	/* System Error on Correctable Error */
#define  PCI_EXP_RTCTL_SENFEE	0x0002	/* System Error on Non-Fatal Error */
#define  PCI_EXP_RTCTL_SEFEE	0x0004	/* System Error on Fatal Error */
#define  PCI_EXP_RTCTL_PMEIE	0x0008	/* PME Interrupt Enable */
#define  PCI_EXP_RTCTL_CRSSVE	0x0010	/* CRS Software Visibility Enable */
#define PCI_EXP_RTCAP		30	/* Root Capabilities */
#define  PCI_EXP_RTCAP_CRSVIS	0x0001	/* CRS Software Visibility capability */
#define PCI_EXP_RTSTA		32	/* Root Status */
#define PCI_EXP_RTSTA_PME	0x00010000 /* PME status */
#define PCI_EXP_RTSTA_PENDING	0x00020000 /* PME pending */
/*
 * The Device Capabilities 2, Device Status 2, Device Control 2,
 * Link Capabilities 2, Link Status 2, Link Control 2,
 * Slot Capabilities 2, Slot Status 2, and Slot Control 2 registers
 * are only present on devices with PCIe Capability version 2.
 * Use pcie_capability_read_word() and similar interfaces to use them
 * safely.
 */
#define PCI_EXP_DEVCAP2		36	/* Device Capabilities 2 */
#define  PCI_EXP_DEVCAP2_ARI		0x00000020 /* Alternative Routing-ID */
#define  PCI_EXP_DEVCAP2_ATOMIC_ROUTE	0x00000040 /* Atomic Op routing */
#define PCI_EXP_DEVCAP2_ATOMIC_COMP64	0x00000100 /* Atomic 64-bit compare */
#define  PCI_EXP_DEVCAP2_LTR		0x00000800 /* Latency tolerance reporting */
#define  PCI_EXP_DEVCAP2_OBFF_MASK	0x000c0000 /* OBFF support mechanism */
#define  PCI_EXP_DEVCAP2_OBFF_MSG	0x00040000 /* New message signaling */
#define  PCI_EXP_DEVCAP2_OBFF_WAKE	0x00080000 /* Re-use WAKE# for OBFF */
#define PCI_EXP_DEVCTL2		40	/* Device Control 2 */
#define  PCI_EXP_DEVCTL2_COMP_TIMEOUT	0x000f	/* Completion Timeout Value */
#define  PCI_EXP_DEVCTL2_ARI		0x0020	/* Alternative Routing-ID */
#define PCI_EXP_DEVCTL2_ATOMIC_REQ	0x0040	/* Set Atomic requests */
#define PCI_EXP_DEVCTL2_ATOMIC_EGRESS_BLOCK 0x0080 /* Block atomic egress */
#define  PCI_EXP_DEVCTL2_IDO_REQ_EN	0x0100	/* Allow IDO for requests */
#define  PCI_EXP_DEVCTL2_IDO_CMP_EN	0x0200	/* Allow IDO for completions */
#define  PCI_EXP_DEVCTL2_LTR_EN		0x0400	/* Enable LTR mechanism */
#define  PCI_EXP_DEVCTL2_OBFF_MSGA_EN	0x2000	/* Enable OBFF Message type A */
#define  PCI_EXP_DEVCTL2_OBFF_MSGB_EN	0x4000	/* Enable OBFF Message type B */
#define  PCI_EXP_DEVCTL2_OBFF_WAKE_EN	0x6000	/* OBFF using WAKE# signaling */
#define PCI_EXP_DEVSTA2		42	/* Device Status 2 */
#define PCI_CAP_EXP_RC_ENDPOINT_SIZEOF_V2	44	/* v2 endpoints without link end here */
#define PCI_EXP_LNKCAP2		44	/* Link Capabilities 2 */
#define  PCI_EXP_LNKCAP2_SLS_2_5GB	0x00000002 /* Supported Speed 2.5GT/s */
#define  PCI_EXP_LNKCAP2_SLS_5_0GB	0x00000004 /* Supported Speed 5.0GT/s */
#define  PCI_EXP_LNKCAP2_SLS_8_0GB	0x00000008 /* Supported Speed 8.0GT/s */
#define  PCI_EXP_LNKCAP2_CROSSLINK	0x00000100 /* Crosslink supported */
#define PCI_EXP_LNKCTL2		48	/* Link Control 2 */
#define PCI_EXP_LNKSTA2		50	/* Link Status 2 */
#define PCI_CAP_EXP_ENDPOINT_SIZEOF_V2	52	/* v2 endpoints with link end here */
#define PCI_EXP_SLTCAP2		52	/* Slot Capabilities 2 */
#define PCI_EXP_SLTCTL2		56	/* Slot Control 2 */
#define PCI_EXP_SLTSTA2		58	/* Slot Status 2 */

/* Extended Capabilities (PCI-X 2.0 and Express) */
#define PCI_EXT_CAP_ID(header)		(header & 0x0000ffff)
#define PCI_EXT_CAP_VER(header)		((header >> 16) & 0xf)
#define PCI_EXT_CAP_NEXT(header)	((header >> 20) & 0xffc)

#define PCI_EXT_CAP_ID_ERR	0x01	/* Advanced Error Reporting */
#define PCI_EXT_CAP_ID_VC	0x02	/* Virtual Channel Capability */
#define PCI_EXT_CAP_ID_DSN	0x03	/* Device Serial Number */
#define PCI_EXT_CAP_ID_PWR	0x04	/* Power Budgeting */
#define PCI_EXT_CAP_ID_RCLD	0x05	/* Root Complex Link Declaration */
#define PCI_EXT_CAP_ID_RCILC	0x06	/* Root Complex Internal Link Control */
#define PCI_EXT_CAP_ID_RCEC	0x07	/* Root Complex Event Collector */
#define PCI_EXT_CAP_ID_MFVC	0x08	/* Multi-Function VC Capability */
#define PCI_EXT_CAP_ID_VC9	0x09	/* same as _VC */
#define PCI_EXT_CAP_ID_RCRB	0x0A	/* Root Complex RB? */
#define PCI_EXT_CAP_ID_VNDR	0x0B	/* Vendor-Specific */
#define PCI_EXT_CAP_ID_CAC	0x0C	/* Config Access - obsolete */
#define PCI_EXT_CAP_ID_ACS	0x0D	/* Access Control Services */
#define PCI_EXT_CAP_ID_ARI	0x0E	/* Alternate Routing ID */
#define PCI_EXT_CAP_ID_ATS	0x0F	/* Address Translation Services */
#define PCI_EXT_CAP_ID_SRIOV	0x10	/* Single Root I/O Virtualization */
#define PCI_EXT_CAP_ID_MRIOV	0x11	/* Multi Root I/O Virtualization */
#define PCI_EXT_CAP_ID_MCAST	0x12	/* Multicast */
#define PCI_EXT_CAP_ID_PRI	0x13	/* Page Request Interface */
#define PCI_EXT_CAP_ID_AMD_XXX	0x14	/* Reserved for AMD */
#define PCI_EXT_CAP_ID_REBAR	0x15	/* Resizable BAR */
#define PCI_EXT_CAP_ID_DPA	0x16	/* Dynamic Power Allocation */
#define PCI_EXT_CAP_ID_TPH	0x17	/* TPH Requester */
#define PCI_EXT_CAP_ID_LTR	0x18	/* Latency Tolerance Reporting */
#define PCI_EXT_CAP_ID_SECPCI	0x19	/* Secondary PCIe Capability */
#define PCI_EXT_CAP_ID_PMUX	0x1A	/* Protocol Multiplexing */
#define PCI_EXT_CAP_ID_PASID	0x1B	/* Process Address Space ID */
#define PCI_EXT_CAP_ID_DPC	0x1D	/* Downstream Port Containment */
#define PCI_EXT_CAP_ID_L1SS	0x1E	/* L1 PM Substates */
#define PCI_EXT_CAP_ID_PTM	0x1F	/* Precision Time Measurement */
#define PCI_EXT_CAP_ID_MAX	PCI_EXT_CAP_ID_PTM

#define PCI_EXT_CAP_DSN_SIZEOF	12
#define PCI_EXT_CAP_MCAST_ENDPOINT_SIZEOF 40

/* Advanced Error Reporting */
#define PCI_ERR_UNCOR_STATUS	4	/* Uncorrectable Error Status */
#define  PCI_ERR_UNC_UND	0x00000001	/* Undefined */
#define  PCI_ERR_UNC_DLP	0x00000010	/* Data Link Protocol */
#define  PCI_ERR_UNC_SURPDN	0x00000020	/* Surprise Down */
#define  PCI_ERR_UNC_POISON_TLP	0x00001000	/* Poisoned TLP */
#define  PCI_ERR_UNC_FCP	0x00002000	/* Flow Control Protocol */
#define  PCI_ERR_UNC_COMP_TIME	0x00004000	/* Completion Timeout */
#define  PCI_ERR_UNC_COMP_ABORT	0x00008000	/* Completer Abort */
#define  PCI_ERR_UNC_UNX_COMP	0x00010000	/* Unexpected Completion */
#define  PCI_ERR_UNC_RX_OVER	0x00020000	/* Receiver Overflow */
#define  PCI_ERR_UNC_MALF_TLP	0x00040000	/* Malformed TLP */
#define  PCI_ERR_UNC_ECRC	0x00080000	/* ECRC Error Status */
#define  PCI_ERR_UNC_UNSUP	0x00100000	/* Unsupported Request */
#define  PCI_ERR_UNC_ACSV	0x00200000	/* ACS Violation */
#define  PCI_ERR_UNC_INTN	0x00400000	/* internal error */
#define  PCI_ERR_UNC_MCBTLP	0x00800000	/* MC blocked TLP */
#define  PCI_ERR_UNC_ATOMEG	0x01000000	/* Atomic egress blocked */
#define  PCI_ERR_UNC_TLPPRE	0x02000000	/* TLP prefix blocked */
#define PCI_ERR_UNCOR_MASK	8	/* Uncorrectable Error Mask */
	/* Same bits as above */
#define PCI_ERR_UNCOR_SEVER	12	/* Uncorrectable Error Severity */
	/* Same bits as above */
#define PCI_ERR_COR_STATUS	16	/* Correctable Error Status */
#define  PCI_ERR_COR_RCVR	0x00000001	/* Receiver Error Status */
#define  PCI_ERR_COR_BAD_TLP	0x00000040	/* Bad TLP Status */
#define  PCI_ERR_COR_BAD_DLLP	0x00000080	/* Bad DLLP Status */
#define  PCI_ERR_COR_REP_ROLL	0x00000100	/* REPLAY_NUM Rollover */
#define  PCI_ERR_COR_REP_TIMER	0x00001000	/* Replay Timer Timeout */
#define  PCI_ERR_COR_ADV_NFAT	0x00002000	/* Advisory Non-Fatal */
#define  PCI_ERR_COR_INTERNAL	0x00004000	/* Corrected Internal */
#define  PCI_ERR_COR_LOG_OVER	0x00008000	/* Header Log Overflow */
#define PCI_ERR_COR_MASK	20	/* Correctable Error Mask */
	/* Same bits as above */
#define PCI_ERR_CAP		24	/* Advanced Error Capabilities */
#define  PCI_ERR_CAP_FEP(x)	((x) & 31)	/* First Error Pointer */
#define  PCI_ERR_CAP_ECRC_GENC	0x00000020	/* ECRC Generation Capable */
#define  PCI_ERR_CAP_ECRC_GENE	0x00000040	/* ECRC Generation Enable */
#define  PCI_ERR_CAP_ECRC_CHKC	0x00000080	/* ECRC Check Capable */
#define  PCI_ERR_CAP_ECRC_CHKE	0x00000100	/* ECRC Check Enable */
#define PCI_ERR_HEADER_LOG	28	/* Header Log Register (16 bytes) */
#define PCI_ERR_ROOT_COMMAND	44	/* Root Error Command */
#define PCI_ERR_ROOT_CMD_COR_EN		0x00000001 /* Correctable Err Reporting Enable */
#define PCI_ERR_ROOT_CMD_NONFATAL_EN	0x00000002 /* Non-Fatal Err Reporting Enable */
#define PCI_ERR_ROOT_CMD_FATAL_EN	0x00000004 /* Fatal Err Reporting Enable */
#define PCI_ERR_ROOT_STATUS	48
#define PCI_ERR_ROOT_COR_RCV		0x00000001 /* ERR_COR Received */
#define PCI_ERR_ROOT_MULTI_COR_RCV	0x00000002 /* Multiple ERR_COR */
#define PCI_ERR_ROOT_UNCOR_RCV		0x00000004 /* ERR_FATAL/NONFATAL */
#define PCI_ERR_ROOT_MULTI_UNCOR_RCV	0x00000008 /* Multiple FATAL/NONFATAL */
#define PCI_ERR_ROOT_FIRST_FATAL	0x00000010 /* First UNC is Fatal */
#define PCI_ERR_ROOT_NONFATAL_RCV	0x00000020 /* Non-Fatal Received */
#define PCI_ERR_ROOT_FATAL_RCV		0x00000040 /* Fatal Received */
#define PCI_ERR_ROOT_ERR_SRC	52	/* Error Source Identification */

/* Virtual Channel */
#define PCI_VC_PORT_CAP1	4
#define  PCI_VC_CAP1_EVCC	0x00000007	/* extended VC count */
#define  PCI_VC_CAP1_LPEVCC	0x00000070	/* low prio extended VC count */
#define  PCI_VC_CAP1_ARB_SIZE	0x00000c00
#define PCI_VC_PORT_CAP2	8
#define  PCI_VC_CAP2_32_PHASE		0x00000002
#define  PCI_VC_CAP2_64_PHASE		0x00000004
#define  PCI_VC_CAP2_128_PHASE		0x00000008
#define  PCI_VC_CAP2_ARB_OFF		0xff000000
#define PCI_VC_PORT_CTRL	12
#define  PCI_VC_PORT_CTRL_LOAD_TABLE	0x00000001
#define PCI_VC_PORT_STATUS	14
#define  PCI_VC_PORT_STATUS_TABLE	0x00000001
#define PCI_VC_RES_CAP		16
#define  PCI_VC_RES_CAP_32_PHASE	0x00000002
#define  PCI_VC_RES_CAP_64_PHASE	0x00000004
#define  PCI_VC_RES_CAP_128_PHASE	0x00000008
#define  PCI_VC_RES_CAP_128_PHASE_TB	0x00000010
#define  PCI_VC_RES_CAP_256_PHASE	0x00000020
#define  PCI_VC_RES_CAP_ARB_OFF		0xff000000
#define PCI_VC_RES_CTRL		20
#define  PCI_VC_RES_CTRL_LOAD_TABLE	0x00010000
#define  PCI_VC_RES_CTRL_ARB_SELECT	0x000e0000
#define  PCI_VC_RES_CTRL_ID		0x07000000
#define  PCI_VC_RES_CTRL_ENABLE		0x80000000
#define PCI_VC_RES_STATUS	26
#define  PCI_VC_RES_STATUS_TABLE	0x00000001
#define  PCI_VC_RES_STATUS_NEGO		0x00000002
#define PCI_CAP_VC_BASE_SIZEOF		0x10
#define PCI_CAP_VC_PER_VC_SIZEOF	0x0C

/* Power Budgeting */
#define PCI_PWR_DSR		4	/* Data Select Register */
#define PCI_PWR_DATA		8	/* Data Register */
#define  PCI_PWR_DATA_BASE(x)	((x) & 0xff)	    /* Base Power */
#define  PCI_PWR_DATA_SCALE(x)	(((x) >> 8) & 3)    /* Data Scale */
#define  PCI_PWR_DATA_PM_SUB(x)	(((x) >> 10) & 7)   /* PM Sub State */
#define  PCI_PWR_DATA_PM_STATE(x) (((x) >> 13) & 3) /* PM State */
#define  PCI_PWR_DATA_TYPE(x)	(((x) >> 15) & 7)   /* Type */
#define  PCI_PWR_DATA_RAIL(x)	(((x) >> 18) & 7)   /* Power Rail */
#define PCI_PWR_CAP		12	/* Capability */
#define  PCI_PWR_CAP_BUDGET(x)	((x) & 1)	/* Included in system budget */
#define PCI_EXT_CAP_PWR_SIZEOF	16

/* Vendor-Specific (VSEC, PCI_EXT_CAP_ID_VNDR) */
#define PCI_VNDR_HEADER		4	/* Vendor-Specific Header */
#define  PCI_VNDR_HEADER_ID(x)	((x) & 0xffff)
#define  PCI_VNDR_HEADER_REV(x)	(((x) >> 16) & 0xf)
#define  PCI_VNDR_HEADER_LEN(x)	(((x) >> 20) & 0xfff)

/*
 * HyperTransport sub capability types
 *
 * Unfortunately there are both 3 bit and 5 bit capability types defined
 * in the HT spec, catering for that is a little messy. You probably don't
 * want to use these directly, just use pci_find_ht_capability() and it
 * will do the right thing for you.
 */
#define HT_3BIT_CAP_MASK	0xE0
#define HT_CAPTYPE_SLAVE	0x00	/* Slave/Primary link configuration */
#define HT_CAPTYPE_HOST		0x20	/* Host/Secondary link configuration */

#define HT_5BIT_CAP_MASK	0xF8
#define HT_CAPTYPE_IRQ		0x80	/* IRQ Configuration */
#define HT_CAPTYPE_REMAPPING_40	0xA0	/* 40 bit address remapping */
#define HT_CAPTYPE_REMAPPING_64 0xA2	/* 64 bit address remapping */
#define HT_CAPTYPE_UNITID_CLUMP	0x90	/* Unit ID clumping */
#define HT_CAPTYPE_EXTCONF	0x98	/* Extended Configuration Space Access */
#define HT_CAPTYPE_MSI_MAPPING	0xA8	/* MSI Mapping Capability */
#define  HT_MSI_FLAGS		0x02		/* Offset to flags */
#define  HT_MSI_FLAGS_ENABLE	0x1		/* Mapping enable */
#define  HT_MSI_FLAGS_FIXED	0x2		/* Fixed mapping only */
#define  HT_MSI_FIXED_ADDR	0x00000000FEE00000ULL	/* Fixed addr */
#define  HT_MSI_ADDR_LO		0x04		/* Offset to low addr bits */
#define  HT_MSI_ADDR_LO_MASK	0xFFF00000	/* Low address bit mask */
#define  HT_MSI_ADDR_HI		0x08		/* Offset to high addr bits */
#define HT_CAPTYPE_DIRECT_ROUTE	0xB0	/* Direct routing configuration */
#define HT_CAPTYPE_VCSET	0xB8	/* Virtual Channel configuration */
#define HT_CAPTYPE_ERROR_RETRY	0xC0	/* Retry on error configuration */
#define HT_CAPTYPE_GEN3		0xD0	/* Generation 3 HyperTransport configuration */
#define HT_CAPTYPE_PM		0xE0	/* HyperTransport power management configuration */
#define HT_CAP_SIZEOF_LONG	28	/* slave & primary */
#define HT_CAP_SIZEOF_SHORT	24	/* host & secondary */

/* Alternative Routing-ID Interpretation */
#define PCI_ARI_CAP		0x04	/* ARI Capability Register */
#define  PCI_ARI_CAP_MFVC	0x0001	/* MFVC Function Groups Capability */
#define  PCI_ARI_CAP_ACS	0x0002	/* ACS Function Groups Capability */
#define  PCI_ARI_CAP_NFN(x)	(((x) >> 8) & 0xff) /* Next Function Number */
#define PCI_ARI_CTRL		0x06	/* ARI Control Register */
#define  PCI_ARI_CTRL_MFVC	0x0001	/* MFVC Function Groups Enable */
#define  PCI_ARI_CTRL_ACS	0x0002	/* ACS Function Groups Enable */
#define  PCI_ARI_CTRL_FG(x)	(((x) >> 4) & 7) /* Function Group */
#define PCI_EXT_CAP_ARI_SIZEOF	8

/* Address Translation Service */
#define PCI_ATS_CAP		0x04	/* ATS Capability Register */
#define  PCI_ATS_CAP_QDEP(x)	((x) & 0x1f)	/* Invalidate Queue Depth */
#define  PCI_ATS_MAX_QDEP	32	/* Max Invalidate Queue Depth */
#define PCI_ATS_CTRL		0x06	/* ATS Control Register */
#define  PCI_ATS_CTRL_ENABLE	0x8000	/* ATS Enable */
#define  PCI_ATS_CTRL_STU(x)	((x) & 0x1f)	/* Smallest Translation Unit */
#define  PCI_ATS_MIN_STU	12	/* shift of minimum STU block */
#define PCI_EXT_CAP_ATS_SIZEOF	8

/* Page Request Interface */
#define PCI_PRI_CTRL		0x04	/* PRI control register */
#define  PCI_PRI_CTRL_ENABLE	0x01	/* Enable */
#define  PCI_PRI_CTRL_RESET	0x02	/* Reset */
#define PCI_PRI_STATUS		0x06	/* PRI status register */
#define  PCI_PRI_STATUS_RF	0x001	/* Response Failure */
#define  PCI_PRI_STATUS_UPRGI	0x002	/* Unexpected PRG index */
#define  PCI_PRI_STATUS_STOPPED	0x100	/* PRI Stopped */
#define PCI_PRI_MAX_REQ		0x08	/* PRI max reqs supported */
#define PCI_PRI_ALLOC_REQ	0x0c	/* PRI max reqs allowed */
#define PCI_EXT_CAP_PRI_SIZEOF	16

/* Process Address Space ID */
#define PCI_PASID_CAP		0x04    /* PASID feature register */
#define  PCI_PASID_CAP_EXEC	0x02	/* Exec permissions Supported */
#define  PCI_PASID_CAP_PRIV	0x04	/* Privilege Mode Supported */
#define PCI_PASID_CTRL		0x06    /* PASID control register */
#define  PCI_PASID_CTRL_ENABLE	0x01	/* Enable bit */
#define  PCI_PASID_CTRL_EXEC	0x02	/* Exec permissions Enable */
#define  PCI_PASID_CTRL_PRIV	0x04	/* Privilege Mode Enable */
#define PCI_EXT_CAP_PASID_SIZEOF	8

/* Single Root I/O Virtualization */
#define PCI_SRIOV_CAP		0x04	/* SR-IOV Capabilities */
#define  PCI_SRIOV_CAP_VFM	0x01	/* VF Migration Capable */
#define  PCI_SRIOV_CAP_INTR(x)	((x) >> 21) /* Interrupt Message Number */
#define PCI_SRIOV_CTRL		0x08	/* SR-IOV Control */
#define  PCI_SRIOV_CTRL_VFE	0x01	/* VF Enable */
#define  PCI_SRIOV_CTRL_VFM	0x02	/* VF Migration Enable */
#define  PCI_SRIOV_CTRL_INTR	0x04	/* VF Migration Interrupt Enable */
#define  PCI_SRIOV_CTRL_MSE	0x08	/* VF Memory Space Enable */
#define  PCI_SRIOV_CTRL_ARI	0x10	/* ARI Capable Hierarchy */
#define PCI_SRIOV_STATUS	0x0a	/* SR-IOV Status */
#define  PCI_SRIOV_STATUS_VFM	0x01	/* VF Migration Status */
#define PCI_SRIOV_INITIAL_VF	0x0c	/* Initial VFs */
#define PCI_SRIOV_TOTAL_VF	0x0e	/* Total VFs */
#define PCI_SRIOV_NUM_VF	0x10	/* Number of VFs */
#define PCI_SRIOV_FUNC_LINK	0x12	/* Function Dependency Link */
#define PCI_SRIOV_VF_OFFSET	0x14	/* First VF Offset */
#define PCI_SRIOV_VF_STRIDE	0x16	/* Following VF Stride */
#define PCI_SRIOV_VF_DID	0x1a	/* VF Device ID */
#define PCI_SRIOV_SUP_PGSIZE	0x1c	/* Supported Page Sizes */
#define PCI_SRIOV_SYS_PGSIZE	0x20	/* System Page Size */
#define PCI_SRIOV_BAR		0x24	/* VF BAR0 */
#define  PCI_SRIOV_NUM_BARS	6	/* Number of VF BARs */
#define PCI_SRIOV_VFM		0x3c	/* VF Migration State Array Offset*/
#define  PCI_SRIOV_VFM_BIR(x)	((x) & 7)	/* State BIR */
#define  PCI_SRIOV_VFM_OFFSET(x) ((x) & ~7)	/* State Offset */
#define  PCI_SRIOV_VFM_UA	0x0	/* Inactive.Unavailable */
#define  PCI_SRIOV_VFM_MI	0x1	/* Dormant.MigrateIn */
#define  PCI_SRIOV_VFM_MO	0x2	/* Active.MigrateOut */
#define  PCI_SRIOV_VFM_AV	0x3	/* Active.Available */
#define PCI_EXT_CAP_SRIOV_SIZEOF 64

#define PCI_LTR_MAX_SNOOP_LAT	0x4
#define PCI_LTR_MAX_NOSNOOP_LAT	0x6
#define  PCI_LTR_VALUE_MASK	0x000003ff
#define  PCI_LTR_SCALE_MASK	0x00001c00
#define  PCI_LTR_SCALE_SHIFT	10
#define PCI_EXT_CAP_LTR_SIZEOF	8

/* Access Control Service */
#define PCI_ACS_CAP		0x04	/* ACS Capability Register */
#define  PCI_ACS_SV		0x01	/* Source Validation */
#define  PCI_ACS_TB		0x02	/* Translation Blocking */
#define  PCI_ACS_RR		0x04	/* P2P Request Redirect */
#define  PCI_ACS_CR		0x08	/* P2P Completion Redirect */
#define  PCI_ACS_UF		0x10	/* Upstream Forwarding */
#define  PCI_ACS_EC		0x20	/* P2P Egress Control */
#define  PCI_ACS_DT		0x40	/* Direct Translated P2P */
#define PCI_ACS_EGRESS_BITS	0x05	/* ACS Egress Control Vector Size */
#define PCI_ACS_CTRL		0x06	/* ACS Control Register */
#define PCI_ACS_EGRESS_CTL_V	0x08	/* ACS Egress Control Vector */

#define PCI_VSEC_HDR		4	/* extended cap - vendor-specific */
#define  PCI_VSEC_HDR_LEN_SHIFT	20	/* shift for length field */

/* SATA capability */
#define PCI_SATA_REGS		4	/* SATA REGs specifier */
#define  PCI_SATA_REGS_MASK	0xF	/* location - BAR#/inline */
#define  PCI_SATA_REGS_INLINE	0xF	/* REGS in config space */
#define PCI_SATA_SIZEOF_SHORT	8
#define PCI_SATA_SIZEOF_LONG	16

/* Resizable BARs */
#define PCI_REBAR_CTRL		8	/* control register */
#define  PCI_REBAR_CTRL_NBAR_MASK	(7 << 5)	/* mask for # bars */
#define  PCI_REBAR_CTRL_NBAR_SHIFT	5	/* shift for # bars */

/* Dynamic Power Allocation */
#define PCI_DPA_CAP		4	/* capability register */
#define  PCI_DPA_CAP_SUBSTATE_MASK	0x1F	/* # substates - 1 */
#define PCI_DPA_BASE_SIZEOF	16	/* size with 0 substates */

/* TPH Requester */
#define PCI_TPH_CAP		4	/* capability register */
#define  PCI_TPH_CAP_LOC_MASK	0x600	/* location mask */
#define   PCI_TPH_LOC_NONE	0x000	/* no location */
#define   PCI_TPH_LOC_CAP	0x200	/* in capability */
#define   PCI_TPH_LOC_MSIX	0x400	/* in MSI-X */
#define PCI_TPH_CAP_ST_MASK	0x07FF0000	/* st table mask */
#define PCI_TPH_CAP_ST_SHIFT	16	/* st table shift */
#define PCI_TPH_BASE_SIZEOF	12	/* size with no st table */

/* Downstream Port Containment */
#define PCI_EXP_DPC_CAP			4	/* DPC Capability */
#define  PCI_EXP_DPC_CAP_RP_EXT		0x20	/* Root Port Extensions for DPC */
#define  PCI_EXP_DPC_CAP_POISONED_TLP	0x40	/* Poisoned TLP Egress Blocking Supported */
#define  PCI_EXP_DPC_CAP_SW_TRIGGER	0x80	/* Software Triggering Supported */
#define  PCI_EXP_DPC_RP_PIO_LOG_SIZE	0xF00	/* RP PIO log size */
#define  PCI_EXP_DPC_CAP_DL_ACTIVE	0x1000	/* ERR_COR signal on DL_Active supported */

#define PCI_EXP_DPC_CTL			6	/* DPC control */
#define  PCI_EXP_DPC_CTL_EN_NONFATAL 	0x02	/* Enable trigger on ERR_NONFATAL message */
#define  PCI_EXP_DPC_CTL_INT_EN 	0x08	/* DPC Interrupt Enable */

#define PCI_EXP_DPC_STATUS		8	/* DPC Status */
#define  PCI_EXP_DPC_STATUS_TRIGGER	0x01	/* Trigger Status */
#define  PCI_EXP_DPC_STATUS_INTERRUPT	0x08	/* Interrupt Status */
#define  PCI_EXP_DPC_RP_BUSY		0x10	/* Root Port Busy */

#define PCI_EXP_DPC_SOURCE_ID		10	/* DPC Source Identifier */

#define PCI_EXP_DPC_RP_PIO_STATUS	 0x0C	/* RP PIO Status */
#define PCI_EXP_DPC_RP_PIO_MASK		 0x10	/* RP PIO MASK */
#define PCI_EXP_DPC_RP_PIO_SEVERITY	 0x14	/* RP PIO Severity */
#define PCI_EXP_DPC_RP_PIO_SYSERROR	 0x18	/* RP PIO SysError */
#define PCI_EXP_DPC_RP_PIO_EXCEPTION	 0x1C	/* RP PIO Exception */
#define PCI_EXP_DPC_RP_PIO_HEADER_LOG	 0x20	/* RP PIO Header Log */
#define PCI_EXP_DPC_RP_PIO_IMPSPEC_LOG	 0x30	/* RP PIO ImpSpec Log */
#define PCI_EXP_DPC_RP_PIO_TLPPREFIX_LOG 0x34	/* RP PIO TLP Prefix Log */

/* Precision Time Measurement */
#define PCI_PTM_CAP			0x04	    /* PTM Capability */
#define  PCI_PTM_CAP_REQ		0x00000001  /* Requester capable */
#define  PCI_PTM_CAP_ROOT		0x00000004  /* Root capable */
#define  PCI_PTM_GRANULARITY_MASK	0x0000FF00  /* Clock granularity */
#define PCI_PTM_CTRL			0x08	    /* PTM Control */
#define  PCI_PTM_CTRL_ENABLE		0x00000001  /* PTM enable */
#define  PCI_PTM_CTRL_ROOT		0x00000002  /* Root select */

/* L1 PM Substates */
#define PCI_L1SS_CAP		    4	/* capability register */
#define  PCI_L1SS_CAP_PCIPM_L1_2	 1	/* PCI PM L1.2 Support */
#define  PCI_L1SS_CAP_PCIPM_L1_1	 2	/* PCI PM L1.1 Support */
#define  PCI_L1SS_CAP_ASPM_L1_2		 4	/* ASPM L1.2 Support */
#define  PCI_L1SS_CAP_ASPM_L1_1		 8	/* ASPM L1.1 Support */
#define  PCI_L1SS_CAP_L1_PM_SS		16	/* L1 PM Substates Support */
#define PCI_L1SS_CTL1		    8	/* Control Register 1 */
#define  PCI_L1SS_CTL1_PCIPM_L1_2	1	/* PCI PM L1.2 Enable */
#define  PCI_L1SS_CTL1_PCIPM_L1_1	2	/* PCI PM L1.1 Support */
#define  PCI_L1SS_CTL1_ASPM_L1_2	4	/* ASPM L1.2 Support */
#define  PCI_L1SS_CTL1_ASPM_L1_1	8	/* ASPM L1.1 Support */
#define  PCI_L1SS_CTL1_L1SS_MASK	0x0000000F
#define PCI_L1SS_CTL2		    0xC	/* Control Register 2 */

#endif /* LINUX_PCI_REGS_H */
