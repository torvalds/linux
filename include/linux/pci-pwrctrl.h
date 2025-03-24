/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Linaro Ltd.
 */

#ifndef __PCI_PWRCTRL_H__
#define __PCI_PWRCTRL_H__

#include <linux/notifier.h>
#include <linux/workqueue.h>

struct device;
struct device_link;

/*
 * This is a simple framework for solving the issue of PCI devices that require
 * certain resources (regulators, GPIOs, clocks) to be enabled before the
 * device can actually be detected on the PCI bus.
 *
 * The idea is to reuse the platform bus to populate OF nodes describing the
 * PCI device and its resources, let these platform devices probe and enable
 * relevant resources and then trigger a rescan of the PCI bus allowing for the
 * same device (with a second associated struct device) to be registered with
 * the PCI subsystem.
 *
 * To preserve a correct hierarchy for PCI power management and device reset,
 * we create a device link between the power control platform device (parent)
 * and the supplied PCI device (child).
 */

/**
 * struct pci_pwrctrl - PCI device power control context.
 * @dev: Address of the power controlling device.
 *
 * An object of this type must be allocated by the PCI power control device and
 * passed to the pwrctrl subsystem to trigger a bus rescan and setup a device
 * link with the device once it's up.
 */
struct pci_pwrctrl {
	struct device *dev;

	/* Private: don't use. */
	struct notifier_block nb;
	struct device_link *link;
	struct work_struct work;
};

void pci_pwrctrl_init(struct pci_pwrctrl *pwrctrl, struct device *dev);
int pci_pwrctrl_device_set_ready(struct pci_pwrctrl *pwrctrl);
void pci_pwrctrl_device_unset_ready(struct pci_pwrctrl *pwrctrl);
int devm_pci_pwrctrl_device_set_ready(struct device *dev,
				     struct pci_pwrctrl *pwrctrl);

#endif /* __PCI_PWRCTRL_H__ */
