/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Anish Gupta (anish@freebsd.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _AMDVI_PRIV_H_
#define _AMDVI_PRIV_H_

#include <contrib/dev/acpica/include/acpi.h>

#define	BIT(n)			(1ULL << (n))
/* Return value of bits[n:m] where n and (n >= ) m are bit positions. */
#define REG_BITS(x, n, m)	(((x) >> (m)) & 		\
				((1 << (((n) - (m)) + 1)) - 1))

/*
 * IOMMU PCI capability.
 */
#define AMDVI_PCI_CAP_IOTLB	BIT(0)	/* IOTLB is supported. */
#define AMDVI_PCI_CAP_HT	BIT(1)	/* HyperTransport tunnel support. */
#define AMDVI_PCI_CAP_NPCACHE	BIT(2)	/* Not present page cached. */
#define AMDVI_PCI_CAP_EFR	BIT(3)	/* Extended features. */
#define AMDVI_PCI_CAP_EXT	BIT(4)	/* Miscellaneous information reg. */

/*
 * IOMMU extended features.
 */
#define AMDVI_EX_FEA_PREFSUP	BIT(0)	/* Prefetch command support. */
#define AMDVI_EX_FEA_PPRSUP	BIT(1)	/* PPR support */
#define AMDVI_EX_FEA_XTSUP	BIT(2)	/* Reserved */
#define AMDVI_EX_FEA_NXSUP	BIT(3)	/* No-execute. */
#define AMDVI_EX_FEA_GTSUP	BIT(4)	/* Guest translation support. */
#define AMDVI_EX_FEA_EFRW	BIT(5)	/* Reserved */
#define AMDVI_EX_FEA_IASUP	BIT(6)	/* Invalidate all command supp. */
#define AMDVI_EX_FEA_GASUP	BIT(7)	/* Guest APIC or AVIC support. */
#define AMDVI_EX_FEA_HESUP	BIT(8)	/* Hardware Error. */
#define AMDVI_EX_FEA_PCSUP	BIT(9)	/* Performance counters support. */
/* XXX: add more EFER bits. */

/*
 * Device table entry or DTE
 * NOTE: Must be 256-bits/32 bytes aligned.
 */
struct amdvi_dte {
	uint32_t dt_valid:1;		/* Device Table valid. */
	uint32_t pt_valid:1;		/* Page translation valid. */
	uint16_t :7;			/* Reserved[8:2] */
	uint8_t	 pt_level:3;		/* Paging level, 0 to disable. */
	uint64_t pt_base:40;		/* Page table root pointer. */
	uint8_t  :3;			/* Reserved[54:52] */
	uint8_t	 gv_valid:1;		/* Revision 2, GVA to SPA. */
	uint8_t	 gv_level:2;		/* Revision 2, GLX level. */
	uint8_t	 gv_cr3_lsb:3;		/* Revision 2, GCR3[14:12] */
	uint8_t	 read_allow:1;		/* I/O read enabled. */
	uint8_t	 write_allow:1;		/* I/O write enabled. */
	uint8_t  :1;			/* Reserved[63] */
	uint16_t domain_id:16;		/* Domain ID */
	uint16_t gv_cr3_lsb2:16;	/* Revision 2, GCR3[30:15] */
	uint8_t	 iotlb_enable:1;	/* Device support IOTLB */
	uint8_t	 sup_second_io_fault:1;	/* Suppress subsequent I/O faults. */
	uint8_t	 sup_all_io_fault:1;	/* Suppress all I/O page faults. */
	uint8_t	 IOctl:2;		/* Port I/O control. */
	uint8_t	 iotlb_cache_disable:1;	/* IOTLB cache hints. */
	uint8_t	 snoop_disable:1;	/* Snoop disable. */
	uint8_t	 allow_ex:1;		/* Allow exclusion. */
	uint8_t	 sysmgmt:2;		/* System management message.*/
	uint8_t  :1;			/* Reserved[106] */
	uint32_t gv_cr3_msb:21;		/* Revision 2, GCR3[51:31] */
	uint8_t	 intmap_valid:1;	/* Interrupt map valid. */
	uint8_t	 intmap_len:4;		/* Interrupt map table length. */
	uint8_t	 intmap_ign:1;		/* Ignore unmapped interrupts. */
	uint64_t intmap_base:46;	/* IntMap base. */
	uint8_t  :4;			/* Reserved[183:180] */
	uint8_t	 init_pass:1;		/* INIT pass through or PT */
	uint8_t	 extintr_pass:1;	/* External Interrupt PT */
	uint8_t	 nmi_pass:1;		/* NMI PT */
	uint8_t  :1;			/* Reserved[187] */
	uint8_t	 intr_ctrl:2;		/* Interrupt control */
	uint8_t	 lint0_pass:1;		/* LINT0 PT */
	uint8_t	 lint1_pass:1;		/* LINT1 PT */
	uint64_t :64;			/* Reserved[255:192] */
} __attribute__((__packed__));
CTASSERT(sizeof(struct amdvi_dte) == 32);

/*
 * IOMMU command entry.
 */
struct amdvi_cmd {
	uint32_t 	word0;
	uint32_t 	word1:28;
	uint8_t		opcode:4;
	uint64_t 	addr;
} __attribute__((__packed__));

/* Command opcodes. */
#define AMDVI_CMP_WAIT_OPCODE	0x1	/* Completion wait. */
#define AMDVI_INVD_DTE_OPCODE	0x2	/* Invalidate device table entry. */
#define AMDVI_INVD_PAGE_OPCODE	0x3	/* Invalidate pages. */
#define AMDVI_INVD_IOTLB_OPCODE	0x4	/* Invalidate IOTLB pages. */
#define AMDVI_INVD_INTR_OPCODE	0x5	/* Invalidate Interrupt table. */
#define AMDVI_PREFETCH_PAGES_OPCODE	0x6	/* Prefetch IOMMU pages. */
#define AMDVI_COMP_PPR_OPCODE	0x7	/* Complete PPR request. */
#define AMDVI_INV_ALL_OPCODE	0x8	/* Invalidate all. */

/* Completion wait attributes. */
#define AMDVI_CMP_WAIT_STORE	BIT(0)	/* Write back data. */
#define AMDVI_CMP_WAIT_INTR	BIT(1)	/* Completion wait interrupt. */
#define AMDVI_CMP_WAIT_FLUSH	BIT(2)	/* Flush queue. */

/* Invalidate page. */
#define AMDVI_INVD_PAGE_S	BIT(0)	/* Invalidation size. */
#define AMDVI_INVD_PAGE_PDE	BIT(1)	/* Invalidate PDE. */
#define AMDVI_INVD_PAGE_GN_GVA	BIT(2)	/* GPA or GVA. */

#define AMDVI_INVD_PAGE_ALL_ADDR	(0x7FFFFFFFFFFFFULL << 12)

/* Invalidate IOTLB. */
#define AMDVI_INVD_IOTLB_S	BIT(0)	/* Invalidation size 4k or addr */
#define AMDVI_INVD_IOTLB_GN_GVA	BIT(2)	/* GPA or GVA. */

#define AMDVI_INVD_IOTLB_ALL_ADDR	(0x7FFFFFFFFFFFFULL << 12)
/* XXX: add more command entries. */

/*
 * IOMMU event entry.
 */
struct amdvi_event {
	uint16_t 	devid;
	uint16_t 	pasid_hi;
	uint16_t 	pasid_domid;	/* PASID low or DomainID */
	uint16_t 	flag:12;
	uint8_t		opcode:4;
	uint64_t 	addr;
} __attribute__((__packed__));
CTASSERT(sizeof(struct amdvi_event) == 16);

/* Various event types. */
#define AMDVI_EVENT_INVALID_DTE		0x1
#define AMDVI_EVENT_PFAULT		0x2
#define AMDVI_EVENT_DTE_HW_ERROR	0x3
#define AMDVI_EVENT_PAGE_HW_ERROR	0x4
#define AMDVI_EVENT_ILLEGAL_CMD		0x5
#define AMDVI_EVENT_CMD_HW_ERROR	0x6
#define AMDVI_EVENT_IOTLB_TIMEOUT	0x7
#define AMDVI_EVENT_INVALID_DTE_REQ	0x8
#define AMDVI_EVENT_INVALID_PPR_REQ	0x9
#define AMDVI_EVENT_COUNTER_ZERO	0xA

#define AMDVI_EVENT_FLAG_MASK           0x1FF	/* Mask for event flags. */
#define AMDVI_EVENT_FLAG_TYPE(x)        (((x) >> 9) & 0x3)

/*
 * IOMMU control block.
 */
struct amdvi_ctrl {
	struct {
		uint16_t size:9;
		uint16_t :3;
		uint64_t base:40;	/* Devtable register base. */
		uint16_t :12;
	} dte;
	struct {
		uint16_t :12;
		uint64_t base:40;
		uint8_t  :4;
		uint8_t	 len:4;
		uint8_t  :4;
	} cmd;
	struct {
		uint16_t :12;
		uint64_t base:40;
		uint8_t  :4;
		uint8_t	 len:4;
		uint8_t  :4;
	} event;
	uint16_t control :13;
	uint64_t	 :51;
	struct {
		uint8_t	 enable:1;
		uint8_t	 allow:1;
		uint16_t :10;
		uint64_t base:40;
		uint16_t :12;
		uint16_t :12;
		uint64_t limit:40;
		uint16_t :12;
	} excl;
	/* 
	 * Revision 2 only. 
	 */
	uint64_t ex_feature;
	struct {
		uint16_t :12;
		uint64_t base:40;
		uint8_t  :4;
		uint8_t	 len:4;
		uint8_t  :4;
	} ppr;
	uint64_t first_event;
	uint64_t second_event;
	uint64_t event_status;
	/* Revision 2 only, end. */
	uint8_t	 pad1[0x1FA8];		/* Padding. */
	uint32_t cmd_head:19;
	uint64_t :45;
	uint32_t cmd_tail:19;
	uint64_t :45;
	uint32_t evt_head:19;
	uint64_t :45;
	uint32_t evt_tail:19;
	uint64_t :45;
	uint32_t status:19;
	uint64_t :45;
	uint64_t pad2;
	uint8_t  :4;
	uint16_t ppr_head:15;
	uint64_t :45;
	uint8_t  :4;
	uint16_t ppr_tail:15;
	uint64_t :45;
	uint8_t	 pad3[0x1FC0];		/* Padding. */

	/* XXX: More for rev2. */
} __attribute__((__packed__));
CTASSERT(offsetof(struct amdvi_ctrl, pad1)== 0x58);
CTASSERT(offsetof(struct amdvi_ctrl, pad2)== 0x2028);
CTASSERT(offsetof(struct amdvi_ctrl, pad3)== 0x2040);

#define AMDVI_MMIO_V1_SIZE	(4 * PAGE_SIZE)	/* v1 size */
/* 
 * AMF IOMMU v2 size including event counters 
 */
#define AMDVI_MMIO_V2_SIZE	(8 * PAGE_SIZE)

CTASSERT(sizeof(struct amdvi_ctrl) == 0x4000);
CTASSERT(sizeof(struct amdvi_ctrl) == AMDVI_MMIO_V1_SIZE);

/* IVHD flag */
#define IVHD_FLAG_HTT		BIT(0)	/* Hypertransport Tunnel. */
#define IVHD_FLAG_PPW		BIT(1)	/* Pass posted write. */
#define IVHD_FLAG_RPPW		BIT(2)	/* Response pass posted write. */
#define IVHD_FLAG_ISOC		BIT(3)	/* Isoc support. */
#define IVHD_FLAG_IOTLB		BIT(4)	/* IOTLB support. */
#define IVHD_FLAG_COH		BIT(5)	/* Coherent control, default 1 */
#define IVHD_FLAG_PFS		BIT(6)	/* Prefetch IOMMU pages. */
#define IVHD_FLAG_PPRS		BIT(7)	/* Peripheral page support. */

/* IVHD device entry data setting. */
#define IVHD_DEV_LINT0_PASS	BIT(6)	/* LINT0 interrupts. */
#define IVHD_DEV_LINT1_PASS	BIT(7)	/* LINT1 interrupts. */

/* Bit[5:4] for System Mgmt. Bit3 is reserved. */
#define IVHD_DEV_INIT_PASS	BIT(0)	/* INIT */
#define IVHD_DEV_EXTINTR_PASS	BIT(1)	/* ExtInt */
#define IVHD_DEV_NMI_PASS	BIT(2)	/* NMI */

/* IVHD 8-byte extended data settings. */
#define IVHD_DEV_EXT_ATS_DISABLE	BIT(31)	/* Disable ATS */

/* IOMMU control register. */
#define AMDVI_CTRL_EN		BIT(0)	/* IOMMU enable. */
#define AMDVI_CTRL_HTT		BIT(1)	/* Hypertransport tunnel enable. */
#define AMDVI_CTRL_ELOG		BIT(2)	/* Event log enable. */
#define AMDVI_CTRL_ELOGINT	BIT(3)	/* Event log interrupt. */
#define AMDVI_CTRL_COMINT	BIT(4)	/* Completion wait interrupt. */
#define AMDVI_CTRL_PPW		BIT(8)
#define AMDVI_CTRL_RPPW		BIT(9)
#define AMDVI_CTRL_COH		BIT(10)
#define AMDVI_CTRL_ISOC		BIT(11)
#define AMDVI_CTRL_CMD		BIT(12)	/* Command buffer enable. */
#define AMDVI_CTRL_PPRLOG	BIT(13)
#define AMDVI_CTRL_PPRINT	BIT(14)
#define AMDVI_CTRL_PPREN	BIT(15)
#define AMDVI_CTRL_GTE		BIT(16)	/* Guest translation enable. */
#define AMDVI_CTRL_GAE		BIT(17)	/* Guest APIC enable. */

/* Invalidation timeout. */
#define AMDVI_CTRL_INV_NO_TO	0	/* No timeout. */
#define AMDVI_CTRL_INV_TO_1ms	1	/* 1 ms */
#define AMDVI_CTRL_INV_TO_10ms	2	/* 10 ms */
#define AMDVI_CTRL_INV_TO_100ms	3	/* 100 ms */
#define AMDVI_CTRL_INV_TO_1S	4	/* 1 second */
#define AMDVI_CTRL_INV_TO_10S	5	/* 10 second */
#define AMDVI_CTRL_INV_TO_100S	6	/* 100 second */

/*
 * Max number of PCI devices.
 * 256 bus x 32 slot/devices x 8 functions.
 */
#define PCI_NUM_DEV_MAX		0x10000

/* Maximum number of domains supported by IOMMU. */
#define AMDVI_MAX_DOMAIN	(BIT(16) - 1)

/*
 * IOMMU Page Table attributes.
 */
#define AMDVI_PT_PRESENT	BIT(0)
#define AMDVI_PT_COHERENT	BIT(60)
#define AMDVI_PT_READ		BIT(61)
#define AMDVI_PT_WRITE		BIT(62)

#define AMDVI_PT_RW		(AMDVI_PT_READ | AMDVI_PT_WRITE)
#define AMDVI_PT_MASK		0xFFFFFFFFFF000UL /* Only [51:12] for PA */

#define AMDVI_PD_LEVEL_SHIFT	9
#define AMDVI_PD_SUPER(x)	(((x) >> AMDVI_PD_LEVEL_SHIFT) == 7)
/*
 * IOMMU Status, offset 0x2020
 */
#define AMDVI_STATUS_EV_OF		BIT(0)	/* Event overflow. */
#define AMDVI_STATUS_EV_INTR		BIT(1)	/* Event interrupt. */
/* Completion wait command completed. */
#define AMDVI_STATUS_CMP		BIT(2)

#define	IVRS_CTRL_RID			1	/* MMIO RID */

/* ACPI IVHD */
struct ivhd_dev_cfg {
	uint32_t start_id;
	uint32_t end_id;
	uint8_t	 data;			/* Device configuration. */
	bool	 enable_ats;		/* ATS enabled for the device. */
	int	 ats_qlen;		/* ATS invalidation queue depth. */
};

struct amdvi_domain {
	uint64_t *ptp;			/* Highest level page table */
	int	ptp_level;		/* Level of page tables */
	u_int	id;			/* Domain id */
	SLIST_ENTRY (amdvi_domain) next;
};

/*
 * I/O Virtualization Hardware Definition Block (IVHD) type 0x10 (legacy)
 * uses ACPI_IVRS_HARDWARE define in contrib/dev/acpica/include/actbl2.h
 * New IVHD types 0x11 and 0x40 as defined in AMD IOMMU spec[48882] are missing in
 * ACPI code. These new types add extra field EFR(Extended Feature Register).
 * XXX : Use definition from ACPI when it is available.
 */
typedef struct acpi_ivrs_hardware_efr_sup
{
	ACPI_IVRS_HEADER Header;
	UINT16 CapabilityOffset;   /* Offset for IOMMU control fields */
	UINT64 BaseAddress;        /* IOMMU control registers */
	UINT16 PciSegmentGroup;
	UINT16 Info;               /* MSI number and unit ID */
	UINT32 Attr;               /* IOMMU Feature */
	UINT64 ExtFR;              /* IOMMU Extended Feature */
	UINT64 Reserved;           /* v1 feature or v2 attribute */
} __attribute__ ((__packed__)) ACPI_IVRS_HARDWARE_EFRSUP;
CTASSERT(sizeof(ACPI_IVRS_HARDWARE_EFRSUP) == 40);

/*
 * Different type of IVHD.
 * XXX: Use AcpiIvrsType once new IVHD types are available.
*/
enum IvrsType
{
	IVRS_TYPE_HARDWARE_LEGACY = 0x10, /* Legacy without EFRi support. */
	IVRS_TYPE_HARDWARE_EFR 	  = 0x11, /* With EFR support. */
	IVRS_TYPE_HARDWARE_MIXED  = 0x40, /* Mixed with EFR support. */
};

/*
 * AMD IOMMU softc.
 */
struct amdvi_softc {
	struct amdvi_ctrl *ctrl;	/* Control area. */
	device_t 	dev;		/* IOMMU device. */
	enum IvrsType   ivhd_type;	/* IOMMU IVHD type. */
	bool		iotlb;		/* IOTLB supported by IOMMU */
	struct amdvi_cmd *cmd;		/* Command descriptor area. */
	int 		cmd_max;	/* Max number of commands. */
	uint64_t	cmp_data;	/* Command completion write back. */
	struct amdvi_event *event;	/* Event descriptor area. */
	struct resource *event_res;	/* Event interrupt resource. */
	void   		*event_tag;	/* Event interrupt tag. */
	int		event_max;	/* Max number of events. */
	int		event_irq;
	int		event_rid;
	/* ACPI various flags. */
	uint32_t 	ivhd_flag;	/* ACPI IVHD flag. */
	uint32_t 	ivhd_feature;	/* ACPI v1 Reserved or v2 attribute. */
	uint64_t 	ext_feature;	/* IVHD EFR */
	/* PCI related. */
	uint16_t 	cap_off;	/* PCI Capability offset. */
	uint8_t		pci_cap;	/* PCI capability. */
	uint16_t 	pci_seg;	/* IOMMU PCI domain/segment. */
	uint16_t 	pci_rid;	/* PCI BDF of IOMMU */
	/* Device range under this IOMMU. */
	uint16_t 	start_dev_rid;	/* First device under this IOMMU. */
	uint16_t 	end_dev_rid;	/* Last device under this IOMMU. */

	/* BIOS provided device configuration for end points. */
	struct 		ivhd_dev_cfg dev_cfg[10];
	int		dev_cfg_cnt;

	/* Software statistics. */
	uint64_t 	event_intr_cnt;	/* Total event INTR count. */
	uint64_t 	total_cmd;	/* Total number of commands. */
};

int	amdvi_setup_hw(struct amdvi_softc *softc);
int	amdvi_teardown_hw(struct amdvi_softc *softc);
#endif /* _AMDVI_PRIV_H_ */
