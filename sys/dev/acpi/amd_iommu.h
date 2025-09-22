/*
 * Copyright (c) 2019 Jordan Hargrave <jordan_hargrave@hotmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __amd_iommu_h__
#define __amd_iommu_h__

#define DEV_TAB_BASE_REG	0x0000
#define CMD_BASE_REG		0x0008
#define EVT_BASE_REG		0x0010

#define EXCL_BASE_REG		0x0020
#define EXCL_LIMIT_REG		0x0028

/* Extended Feature Register */
#define EXTFEAT_REG		0x0030
#define  EFR_PREFSUP		(1L << 0)
#define  EFR_PPRSUP		(1L << 1)
#define  EFR_NXSUP		(1L << 3)
#define  EFR_GTSUP		(1L << 4)
#define  EFR_IASUP		(1L << 6)
#define  EFR_GASUP		(1L << 7)
#define  EFR_HESUP		(1L << 8)
#define  EFR_PCSUP		(1L << 9)
#define  EFR_HATS_SHIFT		10
#define  EFR_HATS_MASK		0x3
#define  EFR_GATS_SHIFT		12
#define  EFR_GATS_MASK		0x3
#define  EFR_GLXSUP_SHIFT	14
#define  EFR_GLXSUP_MASK	0x3
#define  EFR_SMIFSUP_SHIFT	16
#define  EFR_SMIFSUP_MASK	0x3
#define  EFR_SMIFRC_SHIFT	18
#define  EFR_SMIFRC_MASK	0x7
#define  EFR_GAMSUP_SHIFT	21
#define  EFR_GAMSUP_MASK	0x7

#define CMD_HEAD_REG		0x2000
#define CMD_TAIL_REG		0x2008
#define EVT_HEAD_REG		0x2010
#define EVT_TAIL_REG		0x2018

#define IOMMUSTS_REG		0x2020

#define DEV_TAB_MASK		0x000FFFFFFFFFF000LL
#define DEV_TAB_LEN		0x1FF

/* IOMMU Control */
#define IOMMUCTL_REG		0x0018
#define  CTL_IOMMUEN		(1L << 0)
#define  CTL_HTTUNEN		(1L << 1)
#define  CTL_EVENTLOGEN		(1L << 2)
#define  CTL_EVENTINTEN		(1L << 3)
#define  CTL_COMWAITINTEN	(1L << 4)
#define  CTL_INVTIMEOUT_SHIFT	5
#define  CTL_INVTIMEOUT_MASK	0x7
#define  CTL_INVTIMEOUT_NONE	0
#define  CTL_INVTIMEOUT_1MS     1
#define  CTL_INVTIMEOUT_10MS    2
#define  CTL_INVTIMEOUT_100MS   3
#define  CTL_INVTIMEOUT_1S      4
#define  CTL_INVTIMEOUT_10S     5
#define  CTL_INVTIMEOUT_100S    6
#define  CTL_PASSPW		(1L << 8)
#define  CTL_RESPASSPW		(1L << 9)
#define  CTL_COHERENT		(1L << 10)
#define  CTL_ISOC		(1L << 11)
#define  CTL_CMDBUFEN		(1L << 12)
#define  CTL_PPRLOGEN		(1L << 13)
#define  CTL_PPRINTEN		(1L << 14)
#define  CTL_PPREN		(1L << 15)
#define  CTL_GTEN		(1L << 16)
#define  CTL_GAEN		(1L << 17)
#define  CTL_CRW_SHIFT		18
#define  CTL_CRW_MASK		0xF
#define  CTL_SMIFEN		(1L << 22)
#define  CTL_SLFWBDIS		(1L << 23)
#define  CTL_SMIFLOGEN		(1L << 24)
#define  CTL_GAMEN_SHIFT	25
#define  CTL_GAMEN_MASK		0x7
#define  CTL_GALOGEN		(1L << 28)
#define  CTL_GAINTEN		(1L << 29)
#define  CTL_DUALPPRLOGEN_SHIFT	30
#define  CTL_DUALPPRLOGEN_MASK	0x3
#define  CTL_DUALEVTLOGEN_SHIFT	32
#define  CTL_DUALEVTLOGEN_MASK	0x3
#define  CTL_DEVTBLSEGEN_SHIFT	34
#define  CTL_DEVTBLSEGEN_MASK	0x7
#define  CTL_PRIVABRTEN_SHIFT	37
#define  CTL_PRIVABRTEN_MASK	0x3
#define  CTL_PPRAUTORSPEN	(1LL << 39)
#define  CTL_MARCEN		(1LL << 40)
#define  CTL_BLKSTOPMRKEN	(1LL << 41)
#define  CTL_PPRAUTOSPAON	(1LL << 42)
#define  CTL_DOMAINIDPNE	(1LL << 43)

#define CMD_BASE_MASK		0x000FFFFFFFFFF000LL
#define CMD_TBL_SIZE		4096
#define CMD_TBL_LEN_4K		(8LL << 56)
#define CMD_TBL_LEN_8K		(9lL << 56)

#define EVT_BASE_MASK		0x000FFFFFFFFFF000LL
#define EVT_TBL_SIZE		4096
#define EVT_TBL_LEN_4K		(8LL << 56)
#define EVT_TBL_LEN_8K		(9LL << 56)

/*========================
 * DEVICE TABLE ENTRY
 * Contains mapping of bus-device-function
 *
 *  0       Valid (V)
 *  1       Translation Valid (TV)
 *  7:8     Host Address Dirty (HAD)
 *  9:11    Page Table Depth (usually 4)
 *  12:51   Page Table Physical Address
 *  52      PPR Enable
 *  53      GPRP
 *  54      Guest I/O Protection Valid (GIoV)
 *  55      Guest Translation Valid (GV)
 *  56:57   Guest Levels translated (GLX)
 *  58:60   Guest CR3 bits 12:14 (GCR3TRP)
 *  61      I/O Read Permission (IR)
 *  62      I/O Write Permission (IW)
 *  64:79   Domain ID
 *  80:95   Guest CR3 bits 15:30 (GCR3TRP)
 *  96      IOTLB Enable (I)
 *  97      Suppress multiple I/O page faults (I)
 *  98      Suppress all I/O page faults (SA)
 *  99:100  Port I/O Control (IoCTL)
 *  101     Cache IOTLB Hint
 *  102     Snoop Disable (SD)
 *  103     Allow Exclusion (EX)
 *  104:105 System Management Message (SysMgt)
 *  107:127 Guest CR3 bits 31:51 (GCR3TRP)
 *  128     Interrupt Map Valid (IV)
 *  129:132 Interrupt Table Length (IntTabLen)
 *========================*/
struct ivhd_dte {
	uint32_t dw0;
	uint32_t dw1;
	uint32_t dw2;
	uint32_t dw3;
	uint32_t dw4;
	uint32_t dw5;
	uint32_t dw6;
	uint32_t dw7;
} __packed;

#define HWDTE_SIZE (65536 * sizeof(struct ivhd_dte))

#define DTE_V			(1L << 0)			/* dw0 */
#define DTE_TV			(1L << 1)			/* dw0 */
#define DTE_LEVEL_SHIFT		9				/* dw0 */
#define DTE_LEVEL_MASK		0x7				/* dw0 */
#define DTE_HPTRP_MASK		0x000FFFFFFFFFF000LL		/* dw0,1 */

#define DTE_PPR			(1L << 20)			/* dw1 */
#define DTE_GPRP		(1L << 21)			/* dw1 */
#define DTE_GIOV		(1L << 22)			/* dw1 */
#define DTE_GV			(1L << 23)			/* dw1 */
#define DTE_IR			(1L << 29)			/* dw1 */
#define DTE_IW			(1L << 30)			/* dw1 */

#define DTE_DID_MASK		0xFFFF				/* dw2 */

#define DTE_IV			(1L << 0)			/* dw3 */
#define DTE_SE			(1L << 1)
#define DTE_SA			(1L << 2)
#define DTE_INTTABLEN_SHIFT	1
#define DTE_INTTABLEN_MASK	0xF
#define DTE_IRTP_MASK		0x000FFFFFFFFFFFC0LL

#define PTE_LVL5                48
#define PTE_LVL4                39
#define PTE_LVL3                30
#define PTE_LVL2                21
#define PTE_LVL1                12

#define PTE_NXTLVL(x)           (((x) & 0x7) << 9)
#define PTE_PADDR_MASK		0x000FFFFFFFFFF000LL
#define PTE_IR                  (1LL << 61)
#define PTE_IW                  (1LL << 62)

#define DTE_GCR312_MASK		0x3
#define DTE_GCR312_SHIFT	24

#define DTE_GCR315_MASK		0xFFFF
#define DTE_GCR315_SHIFT	16

#define DTE_GCR331_MASK		0xFFFFF
#define DTE_GCR331_SHIFT	12

#define _get64(x)   *(uint64_t *)(x)
#define _put64(x,v) *(uint64_t *)(x) = (v)

/* Set Guest CR3 address */
static inline void
dte_set_guest_cr3(struct ivhd_dte *dte, paddr_t paddr)
{
	iommu_rmw32(&dte->dw1, DTE_GCR312_MASK, DTE_GCR312_SHIFT, paddr >> 12);
	iommu_rmw32(&dte->dw2, DTE_GCR315_MASK, DTE_GCR315_SHIFT, paddr >> 15);
	iommu_rmw32(&dte->dw3, DTE_GCR331_MASK, DTE_GCR331_SHIFT, paddr >> 31);
}

/* Set Interrupt Remapping Root Pointer */
static inline void
dte_set_interrupt_table_root_ptr(struct ivhd_dte *dte, paddr_t paddr)
{
	uint64_t ov = _get64(&dte->dw4);
	_put64(&dte->dw4, (ov & ~DTE_IRTP_MASK) | (paddr & DTE_IRTP_MASK));
}

/* Set Interrupt Remapping Table length */
static inline void
dte_set_interrupt_table_length(struct ivhd_dte *dte, int nEnt)
{
	iommu_rmw32(&dte->dw4, DTE_INTTABLEN_MASK, DTE_INTTABLEN_SHIFT, nEnt);
}

/* Set Interrupt Remapping Valid */
static inline void
dte_set_interrupt_valid(struct ivhd_dte *dte)
{
	dte->dw4 |= DTE_IV;
}

/* Set Domain ID in Device Table Entry */
static inline void
dte_set_domain(struct ivhd_dte *dte, uint16_t did)
{
	dte->dw2 = (dte->dw2 & ~DTE_DID_MASK) | (did & DTE_DID_MASK);
}

/* Set Page Table Pointer for device */
static inline void
dte_set_host_page_table_root_ptr(struct ivhd_dte *dte, paddr_t paddr)
{
	uint64_t ov;

	ov = _get64(&dte->dw0) & ~DTE_HPTRP_MASK;
	ov |= (paddr & DTE_HPTRP_MASK) | PTE_IW | PTE_IR;

	_put64(&dte->dw0, ov);
}

/* Set Page Table Levels Mask */
static inline void
dte_set_mode(struct ivhd_dte *dte, int mode)
{
	iommu_rmw32(&dte->dw0, DTE_LEVEL_MASK, DTE_LEVEL_SHIFT, mode);
}

static inline void
dte_set_tv(struct ivhd_dte *dte)
{
	dte->dw0 |= DTE_TV;
}

/* Set Device Table Entry valid.
 * Domain/Level/Mode/PageTable should already be set
 */
static inline void
dte_set_valid(struct ivhd_dte *dte)
{
	dte->dw0 |= DTE_V;
}

/* Check if Device Table Entry is valid */
static inline int
dte_is_valid(struct ivhd_dte *dte)
{
	return (dte->dw0 & DTE_V);
}

/*=========================================
 * COMMAND
 *=========================================*/
struct ivhd_command {
	uint32_t dw0;
	uint32_t dw1;
	uint32_t dw2;
	uint32_t dw3;
} __packed;

#define CMD_SHIFT 28

enum {
	COMPLETION_WAIT			= 0x01,
	INVALIDATE_DEVTAB_ENTRY		= 0x02,
	INVALIDATE_IOMMU_PAGES		= 0x03,
	INVALIDATE_IOTLB_PAGES		= 0x04,
	INVALIDATE_INTERRUPT_TABLE	= 0x05,
	PREFETCH_IOMMU_PAGES		= 0x06,
	COMPLETE_PPR_REQUEST		= 0x07,
	INVALIDATE_IOMMU_ALL		= 0x08,
};

/*=========================================
 * EVENT
 *=========================================*/
struct ivhd_event {
	uint32_t dw0;
	uint32_t dw1;
	uint32_t dw2;
	uint32_t dw3;
} __packed;

#define EVT_TYPE_SHIFT		28
#define EVT_TYPE_MASK		0xF
#define EVT_SID_SHIFT		0
#define EVT_SID_MASK		0xFFFF
#define EVT_DID_SHIFT		0
#define EVT_DID_MASK		0xFFFF
#define EVT_FLAG_SHIFT		16
#define EVT_FLAG_MASK		0xFFF

/* IOMMU Fault reasons */
enum {
	ILLEGAL_DEV_TABLE_ENTRY		= 0x1,
	IO_PAGE_FAULT			= 0x2,
	DEV_TAB_HARDWARE_ERROR		= 0x3,
	PAGE_TAB_HARDWARE_ERROR		= 0x4,
	ILLEGAL_COMMAND_ERROR		= 0x5,
	COMMAND_HARDWARE_ERROR		= 0x6,
	IOTLB_INV_TIMEOUT		= 0x7,
	INVALID_DEVICE_REQUEST		= 0x8,
};

#define EVT_GN			(1L << 16)
#define EVT_NX			(1L << 17)
#define EVT_US			(1L << 18)
#define EVT_I			(1L << 19)
#define EVT_PR			(1L << 20)
#define EVT_RW			(1L << 21)
#define EVT_PE			(1L << 22)
#define EVT_RZ			(1L << 23)
#define EVT_TR			(1L << 24)

struct iommu_softc;

int	ivhd_flush_devtab(struct iommu_softc *, int);
int	ivhd_invalidate_iommu_all(struct iommu_softc *);
int	ivhd_invalidate_interrupt_table(struct iommu_softc *, int);
int	ivhd_issue_command(struct iommu_softc *, const struct ivhd_command *, int);
int	ivhd_invalidate_domain(struct iommu_softc *, int);

void	_dumppte(struct pte_entry *, int, vaddr_t);

#endif
