/*
 * Copyright (c) 2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Copyright (C) Ashok Raj <ashok.raj@intel.com>
 * Copyright (C) Shaohua Li <shaohua.li@intel.com>
 */

#ifndef __DMAR_H__
#define __DMAR_H__

#include <linux/acpi.h>
#include <linux/types.h>


extern int dmar_table_init(void);
extern int early_dmar_detect(void);

extern struct list_head dmar_drhd_units;
extern struct list_head dmar_rmrr_units;

struct dmar_drhd_unit {
	struct list_head list;		/* list of drhd units	*/
	u64	reg_base_addr;		/* register base address*/
	struct	pci_dev **devices; 	/* target device array	*/
	int	devices_cnt;		/* target device count	*/
	u8	ignored:1; 		/* ignore drhd		*/
	u8	include_all:1;
	struct intel_iommu *iommu;
};

struct dmar_rmrr_unit {
	struct list_head list;		/* list of rmrr units	*/
	u64	base_address;		/* reserved base address*/
	u64	end_address;		/* reserved end address */
	struct pci_dev **devices;	/* target devices */
	int	devices_cnt;		/* target device count */
};

#endif /* __DMAR_H__ */
