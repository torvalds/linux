/*
 * Copyright (C) 2007-2010 Advanced Micro Devices, Inc.
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

#ifndef _ASM_X86_AMD_IOMMU_H
#define _ASM_X86_AMD_IOMMU_H

#include <linux/irqreturn.h>

#ifdef CONFIG_AMD_IOMMU

extern int amd_iommu_detect(void);


/**
 * amd_iommu_enable_device_erratum() - Enable erratum workaround for device
 *				       in the IOMMUv2 driver
 * @pdev: The PCI device the workaround is necessary for
 * @erratum: The erratum workaround to enable
 *
 * Possible values for the erratum number are for now:
 * - AMD_PRI_DEV_ERRATUM_ENABLE_RESET - Reset PRI capability when PRI
 *					is enabled
 * - AMD_PRI_DEV_ERRATUM_LIMIT_REQ_ONE - Limit number of outstanding PRI
 *					 requests to one
 */
#define AMD_PRI_DEV_ERRATUM_ENABLE_RESET		0
#define AMD_PRI_DEV_ERRATUM_LIMIT_REQ_ONE		1

extern void amd_iommu_enable_device_erratum(struct pci_dev *pdev, u32 erratum);

#else

static inline int amd_iommu_detect(void) { return -ENODEV; }

#endif

#endif /* _ASM_X86_AMD_IOMMU_H */
