/*
 * Copyright (C) 2007-2008 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 *         Leo Duran <leo.duran@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef __AMD_IOMMU_TYPES_H__
#define __AMD_IOMMU_TYPES_H__

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>

/*
 * some size calculation constants
 */
#define DEV_TABLE_ENTRY_SIZE		32
#define ALIAS_TABLE_ENTRY_SIZE		2
#define RLOOKUP_TABLE_ENTRY_SIZE	(sizeof(void *))

/* Length of the MMIO region for the AMD IOMMU */
#define MMIO_REGION_LENGTH       0x4000

/* Capability offsets used by the driver */
#define MMIO_CAP_HDR_OFFSET	0x00
#define MMIO_RANGE_OFFSET	0x0c

/* Masks, shifts and macros to parse the device range capability */
#define MMIO_RANGE_LD_MASK	0xff000000
#define MMIO_RANGE_FD_MASK	0x00ff0000
#define MMIO_RANGE_BUS_MASK	0x0000ff00
#define MMIO_RANGE_LD_SHIFT	24
#define MMIO_RANGE_FD_SHIFT	16
#define MMIO_RANGE_BUS_SHIFT	8
#define MMIO_GET_LD(x)  (((x) & MMIO_RANGE_LD_MASK) >> MMIO_RANGE_LD_SHIFT)
#define MMIO_GET_FD(x)  (((x) & MMIO_RANGE_FD_MASK) >> MMIO_RANGE_FD_SHIFT)
#define MMIO_GET_BUS(x) (((x) & MMIO_RANGE_BUS_MASK) >> MMIO_RANGE_BUS_SHIFT)

/* Flag masks for the AMD IOMMU exclusion range */
#define MMIO_EXCL_ENABLE_MASK 0x01ULL
#define MMIO_EXCL_ALLOW_MASK  0x02ULL

/* Used offsets into the MMIO space */
#define MMIO_DEV_TABLE_OFFSET   0x0000
#define MMIO_CMD_BUF_OFFSET     0x0008
#define MMIO_EVT_BUF_OFFSET     0x0010
#define MMIO_CONTROL_OFFSET     0x0018
#define MMIO_EXCL_BASE_OFFSET   0x0020
#define MMIO_EXCL_LIMIT_OFFSET  0x0028
#define MMIO_CMD_HEAD_OFFSET	0x2000
#define MMIO_CMD_TAIL_OFFSET	0x2008
#define MMIO_EVT_HEAD_OFFSET	0x2010
#define MMIO_EVT_TAIL_OFFSET	0x2018
#define MMIO_STATUS_OFFSET	0x2020

/* MMIO status bits */
#define MMIO_STATUS_COM_WAIT_INT_MASK	0x04

/* feature control bits */
#define CONTROL_IOMMU_EN        0x00ULL
#define CONTROL_HT_TUN_EN       0x01ULL
#define CONTROL_EVT_LOG_EN      0x02ULL
#define CONTROL_EVT_INT_EN      0x03ULL
#define CONTROL_COMWAIT_EN      0x04ULL
#define CONTROL_PASSPW_EN       0x08ULL
#define CONTROL_RESPASSPW_EN    0x09ULL
#define CONTROL_COHERENT_EN     0x0aULL
#define CONTROL_ISOC_EN         0x0bULL
#define CONTROL_CMDBUF_EN       0x0cULL
#define CONTROL_PPFLOG_EN       0x0dULL
#define CONTROL_PPFINT_EN       0x0eULL

/* command specific defines */
#define CMD_COMPL_WAIT          0x01
#define CMD_INV_DEV_ENTRY       0x02
#define CMD_INV_IOMMU_PAGES     0x03

#define CMD_COMPL_WAIT_STORE_MASK	0x01
#define CMD_COMPL_WAIT_INT_MASK		0x02
#define CMD_INV_IOMMU_PAGES_SIZE_MASK	0x01
#define CMD_INV_IOMMU_PAGES_PDE_MASK	0x02

#define CMD_INV_IOMMU_ALL_PAGES_ADDRESS	0x7fffffffffffffffULL

/* macros and definitions for device table entries */
#define DEV_ENTRY_VALID         0x00
#define DEV_ENTRY_TRANSLATION   0x01
#define DEV_ENTRY_IR            0x3d
#define DEV_ENTRY_IW            0x3e
#define DEV_ENTRY_NO_PAGE_FAULT	0x62
#define DEV_ENTRY_EX            0x67
#define DEV_ENTRY_SYSMGT1       0x68
#define DEV_ENTRY_SYSMGT2       0x69
#define DEV_ENTRY_INIT_PASS     0xb8
#define DEV_ENTRY_EINT_PASS     0xb9
#define DEV_ENTRY_NMI_PASS      0xba
#define DEV_ENTRY_LINT0_PASS    0xbe
#define DEV_ENTRY_LINT1_PASS    0xbf

/* constants to configure the command buffer */
#define CMD_BUFFER_SIZE    8192
#define CMD_BUFFER_ENTRIES 512
#define MMIO_CMD_SIZE_SHIFT 56
#define MMIO_CMD_SIZE_512 (0x9ULL << MMIO_CMD_SIZE_SHIFT)

#define PAGE_MODE_1_LEVEL 0x01
#define PAGE_MODE_2_LEVEL 0x02
#define PAGE_MODE_3_LEVEL 0x03

#define IOMMU_PDE_NL_0   0x000ULL
#define IOMMU_PDE_NL_1   0x200ULL
#define IOMMU_PDE_NL_2   0x400ULL
#define IOMMU_PDE_NL_3   0x600ULL

#define IOMMU_PTE_L2_INDEX(address) (((address) >> 30) & 0x1ffULL)
#define IOMMU_PTE_L1_INDEX(address) (((address) >> 21) & 0x1ffULL)
#define IOMMU_PTE_L0_INDEX(address) (((address) >> 12) & 0x1ffULL)

#define IOMMU_MAP_SIZE_L1 (1ULL << 21)
#define IOMMU_MAP_SIZE_L2 (1ULL << 30)
#define IOMMU_MAP_SIZE_L3 (1ULL << 39)

#define IOMMU_PTE_P  (1ULL << 0)
#define IOMMU_PTE_U  (1ULL << 59)
#define IOMMU_PTE_FC (1ULL << 60)
#define IOMMU_PTE_IR (1ULL << 61)
#define IOMMU_PTE_IW (1ULL << 62)

#define IOMMU_L1_PDE(address) \
	((address) | IOMMU_PDE_NL_1 | IOMMU_PTE_P | IOMMU_PTE_IR | IOMMU_PTE_IW)
#define IOMMU_L2_PDE(address) \
	((address) | IOMMU_PDE_NL_2 | IOMMU_PTE_P | IOMMU_PTE_IR | IOMMU_PTE_IW)

#define IOMMU_PAGE_MASK (((1ULL << 52) - 1) & ~0xfffULL)
#define IOMMU_PTE_PRESENT(pte) ((pte) & IOMMU_PTE_P)
#define IOMMU_PTE_PAGE(pte) (phys_to_virt((pte) & IOMMU_PAGE_MASK))
#define IOMMU_PTE_MODE(pte) (((pte) >> 9) & 0x07)

#define IOMMU_PROT_MASK 0x03
#define IOMMU_PROT_IR 0x01
#define IOMMU_PROT_IW 0x02

/* IOMMU capabilities */
#define IOMMU_CAP_IOTLB   24
#define IOMMU_CAP_NPCACHE 26

#define MAX_DOMAIN_ID 65536

/*
 * This structure contains generic data for  IOMMU protection domains
 * independent of their use.
 */
struct protection_domain {
	spinlock_t lock; /* mostly used to lock the page table*/
	u16 id;		 /* the domain id written to the device table */
	int mode;	 /* paging mode (0-6 levels) */
	u64 *pt_root;	 /* page table root pointer */
	void *priv;	 /* private data */
};

/*
 * Data container for a dma_ops specific protection domain
 */
struct dma_ops_domain {
	struct list_head list;

	/* generic protection domain information */
	struct protection_domain domain;

	/* size of the aperture for the mappings */
	unsigned long aperture_size;

	/* address we start to search for free addresses */
	unsigned long next_bit;

	/* address allocation bitmap */
	unsigned long *bitmap;

	/*
	 * Array of PTE pages for the aperture. In this array we save all the
	 * leaf pages of the domain page table used for the aperture. This way
	 * we don't need to walk the page table to find a specific PTE. We can
	 * just calculate its address in constant time.
	 */
	u64 **pte_pages;
};

/*
 * Structure where we save information about one hardware AMD IOMMU in the
 * system.
 */
struct amd_iommu {
	struct list_head list;

	/* locks the accesses to the hardware */
	spinlock_t lock;

	/* device id of this IOMMU */
	u16 devid;
	/*
	 * Capability pointer. There could be more than one IOMMU per PCI
	 * device function if there are more than one AMD IOMMU capability
	 * pointers.
	 */
	u16 cap_ptr;

	/* physical address of MMIO space */
	u64 mmio_phys;
	/* virtual address of MMIO space */
	u8 *mmio_base;

	/* capabilities of that IOMMU read from ACPI */
	u32 cap;

	/* first device this IOMMU handles. read from PCI */
	u16 first_device;
	/* last device this IOMMU handles. read from PCI */
	u16 last_device;

	/* start of exclusion range of that IOMMU */
	u64 exclusion_start;
	/* length of exclusion range of that IOMMU */
	u64 exclusion_length;

	/* command buffer virtual address */
	u8 *cmd_buf;
	/* size of command buffer */
	u32 cmd_buf_size;

	/* if one, we need to send a completion wait command */
	int need_sync;

	/* default dma_ops domain for that IOMMU */
	struct dma_ops_domain *default_dom;
};

/*
 * List with all IOMMUs in the system. This list is not locked because it is
 * only written and read at driver initialization or suspend time
 */
extern struct list_head amd_iommu_list;

/*
 * Structure defining one entry in the device table
 */
struct dev_table_entry {
	u32 data[8];
};

/*
 * One entry for unity mappings parsed out of the ACPI table.
 */
struct unity_map_entry {
	struct list_head list;

	/* starting device id this entry is used for (including) */
	u16 devid_start;
	/* end device id this entry is used for (including) */
	u16 devid_end;

	/* start address to unity map (including) */
	u64 address_start;
	/* end address to unity map (including) */
	u64 address_end;

	/* required protection */
	int prot;
};

/*
 * List of all unity mappings. It is not locked because as runtime it is only
 * read. It is created at ACPI table parsing time.
 */
extern struct list_head amd_iommu_unity_map;

/*
 * Data structures for device handling
 */

/*
 * Device table used by hardware. Read and write accesses by software are
 * locked with the amd_iommu_pd_table lock.
 */
extern struct dev_table_entry *amd_iommu_dev_table;

/*
 * Alias table to find requestor ids to device ids. Not locked because only
 * read on runtime.
 */
extern u16 *amd_iommu_alias_table;

/*
 * Reverse lookup table to find the IOMMU which translates a specific device.
 */
extern struct amd_iommu **amd_iommu_rlookup_table;

/* size of the dma_ops aperture as power of 2 */
extern unsigned amd_iommu_aperture_order;

/* largest PCI device id we expect translation requests for */
extern u16 amd_iommu_last_bdf;

/* data structures for protection domain handling */
extern struct protection_domain **amd_iommu_pd_table;

/* allocation bitmap for domain ids */
extern unsigned long *amd_iommu_pd_alloc_bitmap;

/* will be 1 if device isolation is enabled */
extern int amd_iommu_isolate;

/* takes a PCI device id and prints it out in a readable form */
static inline void print_devid(u16 devid, int nl)
{
	int bus = devid >> 8;
	int dev = devid >> 3 & 0x1f;
	int fn  = devid & 0x07;

	printk("%02x:%02x.%x", bus, dev, fn);
	if (nl)
		printk("\n");
}

/* takes bus and device/function and returns the device id
 * FIXME: should that be in generic PCI code? */
static inline u16 calc_devid(u8 bus, u8 devfn)
{
	return (((u16)bus) << 8) | devfn;
}

#endif
