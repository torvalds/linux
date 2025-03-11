/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCIe bandwidth controller
 *
 * Copyright (C) 2023-2024 Intel Corporation
 */

#ifndef LINUX_PCI_BWCTRL_H
#define LINUX_PCI_BWCTRL_H

#include <linux/pci.h>

struct thermal_cooling_device;

#ifdef CONFIG_PCIE_THERMAL
struct thermal_cooling_device *pcie_cooling_device_register(struct pci_dev *port);
void pcie_cooling_device_unregister(struct thermal_cooling_device *cdev);
#else
static inline struct thermal_cooling_device *pcie_cooling_device_register(struct pci_dev *port)
{
	return NULL;
}
static inline void pcie_cooling_device_unregister(struct thermal_cooling_device *cdev)
{
}
#endif

#endif
