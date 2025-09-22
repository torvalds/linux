/*
 * Copyright (c) 2015 Jordan Hargrave <jordan_hargrave@hotmail.com>
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

#ifndef _DEV_ACPI_DMARREG_H_
#define _DEV_ACPI_DMARREG_H_

/*#define IOMMU_DEBUG*/

#define VTD_STRIDE_MASK 0x1FF
#define VTD_STRIDE_SIZE 9
#define VTD_PAGE_SIZE   4096
#define VTD_PAGE_MASK   0xFFF
#define VTD_PTE_MASK    0x0000FFFFFFFFF000LL

#define VTD_LEVEL0	12
#define VTD_LEVEL1	21
#define VTD_LEVEL2	30 /* Minimum level supported */
#define VTD_LEVEL3	39 /* Also supported */
#define VTD_LEVEL4	48
#define VTD_LEVEL5	57

#define _xbit(x,y) (((x)>> (y)) & 1)
#define _xfld(x,y) (uint32_t)(((x)>> y##_SHIFT) & y##_MASK)

#define VTD_AWTOLEVEL(x)    (((x) - 30) / VTD_STRIDE_SIZE)
#define VTD_LEVELTOAW(x)    (((x) * VTD_STRIDE_SIZE) + 30)

#define DMAR_VER_REG		0x00    /* 32:Arch version supported by this IOMMU */
#define DMAR_RTADDR_REG		0x20    /* 64:Root entry table */
#define DMAR_FEDATA_REG		0x3c    /* 32:Fault event interrupt data register */
#define DMAR_FEADDR_REG		0x40    /* 32:Fault event interrupt addr register */
#define DMAR_FEUADDR_REG	0x44    /* 32:Upper address register */
#define DMAR_AFLOG_REG		0x58    /* 64:Advanced Fault control */
#define DMAR_PMEN_REG		0x64    /* 32:Enable Protected Memory Region */
#define DMAR_PLMBASE_REG	0x68    /* 32:PMRR Low addr */
#define DMAR_PLMLIMIT_REG	0x6c    /* 32:PMRR low limit */
#define DMAR_PHMBASE_REG	0x70    /* 64:pmrr high base addr */
#define DMAR_PHMLIMIT_REG	0x78    /* 64:pmrr high limit */
#define DMAR_ICS_REG		0x9C    /* 32:Invalidation complete status register */
#define DMAR_IECTL_REG		0xa0    /* 32:Invalidation event control register */
#define DMAR_IEDATA_REG		0xa4    /* 32:Invalidation event data register */
#define DMAR_IEADDR_REG		0xa8    /* 32:Invalidation event address register */
#define DMAR_IEUADDR_REG	0xac    /* 32:Invalidation event upper address register */
#define DMAR_IRTA_REG		0xb8    /* 64:Interrupt remapping table addr register */
#define DMAR_CAP_REG		0x08    /* 64:Hardware supported capabilities */
#define   CAP_PI		(1LL << 59)
#define   CAP_FL1GP		(1LL << 56)
#define   CAP_DRD		(1LL << 55)
#define   CAP_DWD		(1LL << 54)
#define   CAP_MAMV_MASK		0x3F
#define   CAP_MAMV_SHIFT	48LL
#define   cap_mamv(x)		_xfld(x,CAP_MAMV)
#define   CAP_NFR_MASK		0xFF
#define   CAP_NFR_SHIFT		40LL
#define   cap_nfr(x)		(_xfld(x,CAP_NFR) + 1)
#define   CAP_PSI		(1LL << 39)
#define   CAP_SLLPS_MASK	0xF
#define   CAP_SLLPS_SHIFT	34LL
#define   cap_sllps(x)		_xfld(x,CAP_SLLPS)
#define   CAP_FRO_MASK		0x3FF
#define   CAP_FRO_SHIFT		24LL
#define   cap_fro(x)		(_xfld(x,CAP_FRO) * 16)
#define   CAP_ZLR		(1LL << 22)
#define   CAP_MGAW_MASK		0x3F
#define   CAP_MGAW_SHIFT	16LL
#define   cap_mgaw(x)		(_xfld(x,CAP_MGAW) + 1)
#define   CAP_SAGAW_MASK	0x1F
#define   CAP_SAGAW_SHIFT	8LL
#define   cap_sagaw(x)		_xfld(x,CAP_SAGAW)
#define   CAP_CM		(1LL << 7)
#define   CAP_PHMR		(1LL << 6)
#define   CAP_PLMR		(1LL << 5)
#define   CAP_RWBF		(1LL << 4)
#define   CAP_AFL		(1LL << 3)
#define   CAP_ND_MASK		0x7
#define   CAP_ND_SHIFT		0x00
#define   cap_nd(x)		(16 << (((x) & CAP_ND_MASK) << 1))

#define DMAR_ECAP_REG		0x10	/* 64:Extended capabilities supported */
#define   ECAP_PSS_MASK		0x1F
#define   ECAP_PSS_SHIFT	35
#define   ECAP_EAFS		(1LL << 34)
#define   ECAP_NWFS		(1LL << 33)
#define   ECAP_SRS		(1LL << 31)
#define   ECAP_ERS		(1LL << 30)
#define   ECAP_PRS		(1LL << 29)
#define   ECAP_PASID		(1LL << 28)
#define   ECAP_DIS		(1LL << 27)
#define   ECAP_NEST		(1LL << 26)
#define   ECAP_MTS		(1LL << 25)
#define   ECAP_ECS		(1LL << 24)
#define   ECAP_MHMV_MASK	0xF
#define   ECAP_MHMV_SHIFT	0x20
#define   ecap_mhmv(x)		_xfld(x,ECAP_MHMV)
#define   ECAP_IRO_MASK		0x3FF	/* IOTLB Register */
#define   ECAP_IRO_SHIFT	0x8
#define   ecap_iro(x)		(_xfld(x,ECAP_IRO) * 16)
#define   ECAP_SC		(1LL << 7)	/* Snoop Control */
#define   ECAP_PT		(1LL << 6)	/* HW Passthru */
#define   ECAP_EIM		(1LL << 4)
#define   ECAP_IR		(1LL << 3)	/* Interrupt remap */
#define   ECAP_DT		(1LL << 2)	/* Device IOTLB */
#define   ECAP_QI		(1LL << 1)	/* Queued Invalidation */
#define   ECAP_C		(1LL << 0)	/* Coherent cache */

#define DMAR_GCMD_REG		0x18		/* 32:Global command register */
#define   GCMD_TE		(1LL << 31)
#define   GCMD_SRTP		(1LL << 30)
#define   GCMD_SFL		(1LL << 29)
#define   GCMD_EAFL		(1LL << 28)
#define   GCMD_WBF		(1LL << 27)
#define   GCMD_QIE		(1LL << 26)
#define   GCMD_IRE		(1LL << 25)
#define   GCMD_SIRTP		(1LL << 24)
#define   GCMD_CFI		(1LL << 23)

#define DMAR_GSTS_REG		0x1c		/* 32:Global status register */
#define   GSTS_TES		(1LL << 31)
#define   GSTS_RTPS		(1LL << 30)
#define   GSTS_FLS		(1LL << 29)
#define   GSTS_AFLS		(1LL << 28)
#define   GSTS_WBFS		(1LL << 27)
#define   GSTS_QIES		(1LL << 26)
#define   GSTS_IRES		(1LL << 25)
#define   GSTS_IRTPS		(1LL << 24)
#define   GSTS_CFIS		(1LL << 23)

#define DMAR_CCMD_REG		0x28		/* 64:Context command reg */
#define   CCMD_ICC		(1LL << 63)
#define   CCMD_CIRG_MASK	0x3
#define   CCMD_CIRG_SHIFT	61
#define   CCMD_CIRG(x)		((uint64_t)(x) << CCMD_CIRG_SHIFT)
#define   CCMD_CAIG_MASK	0x3
#define   CCMD_CAIG_SHIFT	59
#define   CCMD_FM_MASK		0x3
#define   CCMD_FM_SHIFT		32
#define   CCMD_FM(x)		(((uint64_t)(x) << CCMD_FM_SHIFT))
#define   CCMD_SID_MASK		0xFFFF
#define   CCMD_SID_SHIFT	8
#define   CCMD_SID(x)		(((x) << CCMD_SID_SHIFT))
#define   CCMD_DID_MASK		0xFFFF
#define   CCMD_DID_SHIFT	0
#define   CCMD_DID(x)		(((x) << CCMD_DID_SHIFT))

#define CIG_GLOBAL		CCMD_CIRG(CTX_GLOBAL)
#define CIG_DOMAIN		CCMD_CIRG(CTX_DOMAIN)
#define CIG_DEVICE		CCMD_CIRG(CTX_DEVICE)


#define DMAR_FSTS_REG		0x34	/* 32:Fault Status register */
#define   FSTS_FRI_MASK		0xFF
#define   FSTS_FRI_SHIFT	8
#define   FSTS_PRO		(1LL << 7)
#define   FSTS_ITE		(1LL << 6)
#define   FSTS_ICE		(1LL << 5)
#define   FSTS_IQE		(1LL << 4)
#define   FSTS_APF		(1LL << 3)
#define   FSTS_APO		(1LL << 2)
#define   FSTS_PPF		(1LL << 1)
#define   FSTS_PFO		(1LL << 0)

#define DMAR_FECTL_REG		0x38	/* 32:Fault control register */
#define   FECTL_IM		(1LL << 31)
#define   FECTL_IP		(1LL << 30)

#define FRCD_HI_F		(1LL << (127-64))
#define FRCD_HI_T		(1LL << (126-64))
#define FRCD_HI_AT_MASK		0x3
#define FRCD_HI_AT_SHIFT	(124-64)
#define FRCD_HI_PV_MASK		0xFFFFF
#define FRCD_HI_PV_SHIFT	(104-64)
#define FRCD_HI_FR_MASK		0xFF
#define FRCD_HI_FR_SHIFT	(96-64)
#define FRCD_HI_PP		(1LL << (95-64))

#define FRCD_HI_SID_MASK	0xFF
#define FRCD_HI_SID_SHIFT	0
#define FRCD_HI_BUS_SHIFT	8
#define FRCD_HI_BUS_MASK	0xFF
#define FRCD_HI_DEV_SHIFT	3
#define FRCD_HI_DEV_MASK	0x1F
#define FRCD_HI_FUN_SHIFT	0
#define FRCD_HI_FUN_MASK	0x7

#define DMAR_IOTLB_REG(x)	(ecap_iro((x)->ecap) + 8)
#define DMAR_IVA_REG(x)		(ecap_iro((x)->ecap) + 0)

#define DMAR_FRIH_REG(x,i)	(cap_fro((x)->cap) + 16*(i) + 8)
#define DMAR_FRIL_REG(x,i)	(cap_fro((x)->cap) + 16*(i) + 0)

#define IOTLB_IVT		(1LL << 63)
#define IOTLB_IIRG_MASK		0x3
#define IOTLB_IIRG_SHIFT	60
#define IOTLB_IIRG(x)		((uint64_t)(x) << IOTLB_IIRG_SHIFT)
#define IOTLB_IAIG_MASK		0x3
#define IOTLB_IAIG_SHIFT	57
#define IOTLB_DR		(1LL << 49)
#define IOTLB_DW		(1LL << 48)
#define IOTLB_DID_MASK		0xFFFF
#define IOTLB_DID_SHIFT		32
#define IOTLB_DID(x)		((uint64_t)(x) << IOTLB_DID_SHIFT)

#define IIG_GLOBAL	IOTLB_IIRG(IOTLB_GLOBAL)
#define IIG_DOMAIN	IOTLB_IIRG(IOTLB_DOMAIN)
#define IIG_PAGE	IOTLB_IIRG(IOTLB_PAGE)

#define DMAR_IQH_REG	0x80	/* 64:Invalidation queue head register */
#define DMAR_IQT_REG	0x88	/* 64:Invalidation queue tail register */
#define DMAR_IQA_REG	0x90	/* 64:Invalidation queue addr register */
#define IQA_QS_256	0	/* 256 entries */
#define IQA_QS_512	1	/* 512 */
#define IQA_QS_1K	2	/* 1024 */
#define IQA_QS_2K	3	/* 2048 */
#define IQA_QS_4K	4	/* 4096 */
#define IQA_QS_8K	5	/* 8192 */
#define IQA_QS_16K	6	/* 16384 */
#define IQA_QS_32K	7	/* 32768 */

/* Read-Modify-Write helpers */
static inline void
iommu_rmw32(void *ov, uint32_t mask, uint32_t shift, uint32_t nv)
{
	*(uint32_t *)ov &= ~(mask << shift);
	*(uint32_t *)ov |= (nv & mask) << shift;
}

static inline void
iommu_rmw64(void *ov, uint32_t mask, uint32_t shift, uint64_t nv)
{
	*(uint64_t *)ov &= ~(mask << shift);
	*(uint64_t *)ov |= (nv & mask) << shift;
}

/*
 * Root Entry: one per bus (256 x 128 bit = 4k)
 *   0        = Present
 *   1:11     = Reserved
 *   12:HAW-1 = Context Table Pointer
 *   HAW:63   = Reserved
 *   64:127   = Reserved
 */
#define ROOT_P	(1L << 0)
struct root_entry {
	uint64_t		lo;
	uint64_t		hi;
};

/* Check if root entry is valid */
static inline bool
root_entry_is_valid(struct root_entry *re)
{
	return (re->lo & ROOT_P);
}

/*
 * Context Entry: one per devfn (256 x 128 bit = 4k)
 *   0      = Present
 *   1      = Fault Processing Disable
 *   2:3    = Translation Type
 *   4:11   = Reserved
 *   12:63  = Second Level Page Translation
 *   64:66  = Address Width (# PTE levels)
 *   67:70  = Ignore
 *   71     = Reserved
 *   72:87  = Domain ID
 *   88:127 = Reserved
 */
#define CTX_P		(1L << 0)
#define CTX_FPD		(1L << 1)
#define CTX_T_MASK	0x3
#define CTX_T_SHIFT	2
enum {
	CTX_T_MULTI,
	CTX_T_IOTLB,
	CTX_T_PASSTHRU
};

#define CTX_H_AW_MASK	0x7
#define CTX_H_AW_SHIFT	0
#define CTX_H_USER_MASK 0xF
#define CTX_H_USER_SHIFT 3
#define CTX_H_DID_MASK	0xFFFF
#define CTX_H_DID_SHIFT	8

struct context_entry {
	uint64_t		lo;
	uint64_t		hi;
};

/* Set fault processing enable/disable */
static inline void
context_set_fpd(struct context_entry *ce, int enable)
{
	ce->lo &= ~CTX_FPD;
	if (enable)
		ce->lo |= CTX_FPD;
}

/* Set context entry present */
static inline void
context_set_present(struct context_entry *ce)
{
	ce->lo |= CTX_P;
}

/* Set Second Level Page Table Entry PA */
static inline void
context_set_slpte(struct context_entry *ce, paddr_t slpte)
{
	ce->lo &= VTD_PAGE_MASK;
	ce->lo |= (slpte & ~VTD_PAGE_MASK);
}

/* Set translation type */
static inline void
context_set_translation_type(struct context_entry *ce, int tt)
{
	ce->lo &= ~(CTX_T_MASK << CTX_T_SHIFT);
	ce->lo |= ((tt & CTX_T_MASK) << CTX_T_SHIFT);
}

/* Set Address Width (# of Page Table levels) */
static inline void
context_set_address_width(struct context_entry *ce, int lvl)
{
	ce->hi &= ~(CTX_H_AW_MASK << CTX_H_AW_SHIFT);
	ce->hi |= ((lvl & CTX_H_AW_MASK) << CTX_H_AW_SHIFT);
}

/* Set domain ID */
static inline void
context_set_domain_id(struct context_entry *ce, int did)
{
	ce->hi &= ~(CTX_H_DID_MASK << CTX_H_DID_SHIFT);
	ce->hi |= ((did & CTX_H_DID_MASK) << CTX_H_DID_SHIFT);
}

/* Get Second Level Page Table PA */
static inline uint64_t
context_pte(struct context_entry *ce)
{
	return (ce->lo & ~VTD_PAGE_MASK);
}

/* Get translation type */
static inline int
context_translation_type(struct context_entry *ce)
{
	return (ce->lo >> CTX_T_SHIFT) & CTX_T_MASK;
}

/* Get domain ID */
static inline int
context_domain_id(struct context_entry *ce)
{
	return (ce->hi >> CTX_H_DID_SHIFT) & CTX_H_DID_MASK;
}

/* Get Address Width */
static inline int
context_address_width(struct context_entry *ce)
{
	return VTD_LEVELTOAW((ce->hi >> CTX_H_AW_SHIFT) & CTX_H_AW_MASK);
}

/* Check if context entry is valid */
static inline bool
context_entry_is_valid(struct context_entry *ce)
{
	return (ce->lo & CTX_P);
}

/* User-available bits in context entry */
static inline int
context_user(struct context_entry *ce)
{
	return (ce->hi >> CTX_H_USER_SHIFT) & CTX_H_USER_MASK;
}

static inline void
context_set_user(struct context_entry *ce, int v)
{
	ce->hi &= ~(CTX_H_USER_MASK << CTX_H_USER_SHIFT);
	ce->hi |=  ((v & CTX_H_USER_MASK) << CTX_H_USER_SHIFT);
}

/*
 * Fault entry
 *   0..HAW-1 = Fault address
 *   HAW:63   = Reserved
 *   64:71    = Source ID
 *   96:103   = Fault Reason
 *   104:123  = PV
 *   124:125  = Address Translation type
 *   126      = Type (0 = Read, 1 = Write)
 *   127      = Fault bit
 */
struct fault_entry {
	uint64_t	lo;
	uint64_t	hi;
};

/* PTE Entry: 512 x 64-bit = 4k */
#define PTE_P	(1L << 0)
#define PTE_R	0x00
#define PTE_W	(1L << 1)
#define PTE_US  (1L << 2)
#define PTE_PWT (1L << 3)
#define PTE_PCD (1L << 4)
#define PTE_A   (1L << 5)
#define PTE_D   (1L << 6)
#define PTE_PAT (1L << 7)
#define PTE_G   (1L << 8)
#define PTE_EA  (1L << 10)
#define PTE_XD  (1LL << 63)

/* PDE Level entry */
#define PTE_PS  (1L << 7)

/* PDPE Level entry */

/* ----------------------------------------------------------------
 * 5555555444444444333333333222222222111111111000000000------------
 * [PML4 ->] PDPE.1GB
 * [PML4 ->] PDPE.PDE -> PDE.2MB
 * [PML4 ->] PDPE.PDE -> PDE -> PTE
 * GAW0 = (12.20) (PTE)
 * GAW1 = (21.29) (PDE)
 * GAW2 = (30.38) (PDPE)
 * GAW3 = (39.47) (PML4)
 * GAW4 = (48.57) (n/a)
 * GAW5 = (58.63) (n/a)
 */
struct pte_entry {
	uint64_t	val;
};

/*
 * Queued Invalidation entry
 *  0:3   = 01h
 *  4:5   = Granularity
 *  6:15  = Reserved
 *  16:31 = Domain ID
 *  32:47 = Source ID
 *  48:49 = FM
 */

/* Invalidate Context Entry */
#define QI_CTX_DID_MASK		0xFFFF
#define QI_CTX_DID_SHIFT	16
#define QI_CTX_SID_MASK		0xFFFF
#define QI_CTX_SID_SHIFT	32
#define QI_CTX_FM_MASK		0x3
#define QI_CTX_FM_SHIFT		48
#define QI_CTX_IG_MASK		0x3
#define QI_CTX_IG_SHIFT		4
#define QI_CTX_DID(x)		(((uint64_t)(x) << QI_CTX_DID_SHIFT))
#define QI_CTX_SID(x)		(((uint64_t)(x) << QI_CTX_SID_SHIFT))
#define QI_CTX_FM(x)		(((uint64_t)(x) << QI_CTX_FM_SHIFT))

#define QI_CTX_IG_GLOBAL	(CTX_GLOBAL << QI_CTX_IG_SHIFT)
#define QI_CTX_IG_DOMAIN	(CTX_DOMAIN << QI_CTX_IG_SHIFT)
#define QI_CTX_IG_DEVICE	(CTX_DEVICE << QI_CTX_IG_SHIFT)

/* Invalidate IOTLB Entry */
#define QI_IOTLB_DID_MASK	0xFFFF
#define QI_IOTLB_DID_SHIFT	16
#define QI_IOTLB_IG_MASK	0x3
#define QI_IOTLB_IG_SHIFT	4
#define QI_IOTLB_DR		(1LL << 6)
#define QI_IOTLB_DW		(1LL << 5)
#define QI_IOTLB_DID(x)		(((uint64_t)(x) << QI_IOTLB_DID_SHIFT))

#define QI_IOTLB_IG_GLOBAL	(1 << QI_IOTLB_IG_SHIFT)
#define QI_IOTLB_IG_DOMAIN	(2 << QI_IOTLB_IG_SHIFT)
#define QI_IOTLB_IG_PAGE	(3 << QI_IOTLB_IG_SHIFT)

/* QI Commands */
#define QI_CTX		0x1
#define QI_IOTLB	0x2
#define QI_DEVTLB	0x3
#define QI_INTR		0x4
#define QI_WAIT		0x5
#define QI_EXTTLB	0x6
#define QI_PAS		0x7
#define QI_EXTDEV	0x8

struct qi_entry {
	uint64_t	lo;
	uint64_t	hi;
};

enum {
	CTX_GLOBAL = 1,
	CTX_DOMAIN,
	CTX_DEVICE,

	IOTLB_GLOBAL = 1,
	IOTLB_DOMAIN,
	IOTLB_PAGE,
};

enum {
	VTD_FAULT_ROOT_P = 0x1,         /* P field in root entry is 0 */
	VTD_FAULT_CTX_P = 0x2,          /* P field in context entry is 0 */
	VTD_FAULT_CTX_INVAL = 0x3,      /* context AW/TT/SLPPTR invalid */
	VTD_FAULT_LIMIT = 0x4,          /* Address is outside of MGAW */
	VTD_FAULT_WRITE = 0x5,          /* Address-translation fault, non-writable */
	VTD_FAULT_READ = 0x6,           /* Address-translation fault, non-readable */
	VTD_FAULT_PTE_INVAL = 0x7,      /* page table hw access error */
	VTD_FAULT_ROOT_INVAL = 0x8,     /* root table hw access error */
	VTD_FAULT_CTX_TBL_INVAL = 0x9,  /* context entry hw access error */
	VTD_FAULT_ROOT_RESERVED = 0xa,  /* non-zero reserved field in root entry */
	VTD_FAULT_CTX_RESERVED = 0xb,   /* non-zero reserved field in context entry */
	VTD_FAULT_PTE_RESERVED = 0xc,   /* non-zero reserved field in paging entry */
	VTD_FAULT_CTX_TT = 0xd,         /* invalid translation type */
};

#endif

void	acpidmar_pci_hook(pci_chipset_tag_t, struct pci_attach_args *);
void	dmar_ptmap(bus_dma_tag_t, bus_addr_t);

#define __EXTRACT(v,m) (((v) >> m##_SHIFT) & m##_MASK)
