// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

#ifndef __SDW_INTEL_H
#define __SDW_INTEL_H

/**
 * struct sdw_intel_res - Soundwire Intel resource structure
 * @mmio_base: mmio base of SoundWire registers
 * @irq: interrupt number
 * @handle: ACPI parent handle
 * @parent: parent device
 */
struct sdw_intel_res {
	void __iomem *mmio_base;
	int irq;
	acpi_handle handle;
	struct device *parent;
};

void *sdw_intel_init(acpi_handle *parent_handle, struct sdw_intel_res *res);
void sdw_intel_exit(void *arg);

#endif
