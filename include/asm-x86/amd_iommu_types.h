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
#define DEV_TABLE_ENTRY_SIZE		256
#define ALIAS_TABLE_ENTRY_SIZE		2
#define RLOOKUP_TABLE_ENTRY_SIZE	(sizeof(void *))

/* helper macros */
#define LOW_U32(x) ((x) & ((1ULL << 32)-1))
#define HIGH_U32(x) (LOW_U32((x) >> 32))

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
#define CMD_INV_IOMMU_PAGES_SIZE_MASK	0x01
#define CMD_INV_IOMMU_PAGES_PDE_MASK	0x02

#define CMD_INV_IOMMU_ALL_PAGES_ADDRESS	0x7fffffffffffffffULL

/* macros and definitions for device table entries */
#define DEV_ENTRY_VALID         0x00
#define DEV_ENTRY_TRANSLATION   0x01
#define DEV_ENTRY_IR            0x3d
#define DEV_ENTRY_IW            0x3e
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

struct protection_domain {
	spinlock_t lock;
	u16 id;
	int mode;
	u64 *pt_root;
	void *priv;
};

struct dma_ops_domain {
	struct list_head list;
	struct protection_domain domain;
	unsigned long aperture_size;
	unsigned long next_bit;
	unsigned long *bitmap;
	u64 **pte_pages;
};

struct amd_iommu {
	struct list_head list;
	spinlock_t lock;

	u16 devid;
	u16 cap_ptr;

	u64 mmio_phys;
	u8 *mmio_base;
	u32 cap;
	u16 first_device;
	u16 last_device;
	u64 exclusion_start;
	u64 exclusion_length;

	u8 *cmd_buf;
	u32 cmd_buf_size;

	int need_sync;

	struct dma_ops_domain *default_dom;
};

extern struct list_head amd_iommu_list;

struct dev_table_entry {
	u32 data[8];
};

struct unity_map_entry {
	struct list_head list;
	u16 devid_start;
	u16 devid_end;
	u64 address_start;
	u64 address_end;
	int prot;
};

extern struct list_head amd_iommu_unity_map;

/* data structures for device handling */
extern struct dev_table_entry *amd_iommu_dev_table;
extern u16 *amd_iommu_alias_table;
extern struct amd_iommu **amd_iommu_rlookup_table;

extern unsigned amd_iommu_aperture_order;

extern u16 amd_iommu_last_bdf;

/* data structures for protection domain handling */
extern struct protection_domain **amd_iommu_pd_table;
extern unsigned long *amd_iommu_pd_alloc_bitmap;

extern int amd_iommu_isolate;

static inline void print_devid(u16 devid, int nl)
{
	int bus = devid >> 8;
	int dev = devid >> 3 & 0x1f;
	int fn  = devid & 0x07;

	printk("%02x:%02x.%x", bus, dev, fn);
	if (nl)
		printk("\n");
}

#endif
