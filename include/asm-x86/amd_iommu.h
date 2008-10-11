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

#ifndef ASM_X86__AMD_IOMMU_H
#define ASM_X86__AMD_IOMMU_H

#include <linux/irqreturn.h>

#ifdef CONFIG_AMD_IOMMU
extern int amd_iommu_init(void);
extern int amd_iommu_init_dma_ops(void);
extern void amd_iommu_detect(void);
extern irqreturn_t amd_iommu_int_handler(int irq, void *data);
#else
static inline int amd_iommu_init(void) { return -ENODEV; }
static inline void amd_iommu_detect(void) { }
#endif

#endif /* ASM_X86__AMD_IOMMU_H */
