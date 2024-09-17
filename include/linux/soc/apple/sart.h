/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Apple SART device driver
 * Copyright (C) The Asahi Linux Contributors
 *
 * Apple SART is a simple address filter for DMA transactions.
 * Regions of physical memory must be added to the SART's allow
 * list before any DMA can target these. Unlike a proper
 * IOMMU no remapping can be done.
 */

#ifndef _LINUX_SOC_APPLE_SART_H_
#define _LINUX_SOC_APPLE_SART_H_

#include <linux/device.h>
#include <linux/err.h>
#include <linux/types.h>

struct apple_sart;

/*
 * Get a reference to the SART attached to dev.
 *
 * Looks for the phandle reference in apple,sart and returns a pointer
 * to the corresponding apple_sart struct to be used with
 * apple_sart_add_allowed_region and apple_sart_remove_allowed_region.
 */
struct apple_sart *devm_apple_sart_get(struct device *dev);

/*
 * Adds the region [paddr, paddr+size] to the DMA allow list.
 *
 * @sart: SART reference
 * @paddr: Start address of the region to be used for DMA
 * @size: Size of the region to be used for DMA.
 */
int apple_sart_add_allowed_region(struct apple_sart *sart, phys_addr_t paddr,
				  size_t size);

/*
 * Removes the region [paddr, paddr+size] from the DMA allow list.
 *
 * Note that exact same paddr and size used for apple_sart_add_allowed_region
 * have to be passed.
 *
 * @sart: SART reference
 * @paddr: Start address of the region no longer used for DMA
 * @size: Size of the region no longer used for DMA.
 */
int apple_sart_remove_allowed_region(struct apple_sart *sart, phys_addr_t paddr,
				     size_t size);

#endif /* _LINUX_SOC_APPLE_SART_H_ */
