#ifndef _ASM_POWERPC_ISERIES_IOMMU_H
#define _ASM_POWERPC_ISERIES_IOMMU_H

/*
 * Copyright (C) 2005  Stephen Rothwell, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 * Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330,
 * Boston, MA  02111-1307  USA
 */

struct pci_dev;
struct device_node;
struct iommu_table;

/* Creates table for an individual device node */
extern void iommu_devnode_init_iSeries(struct pci_dev *pdev,
				       struct device_node *dn);

/* Get table parameters from HV */
extern void iommu_table_getparms_iSeries(unsigned long busno,
		unsigned char slotno, unsigned char virtbus,
		struct iommu_table *tbl);

#endif /* _ASM_POWERPC_ISERIES_IOMMU_H */
