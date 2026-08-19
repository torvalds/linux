/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AMD Promontory 21 xHCI auxiliary device platform data.
 *
 * Copyright (C) 2026 Jihong Min <hurryman2212@gmail.com>
 */

#ifndef _LINUX_PLATFORM_DATA_USB_XHCI_PROM21_H
#define _LINUX_PLATFORM_DATA_USB_XHCI_PROM21_H

#include <linux/compiler_types.h>
#include <linux/types.h>

struct pci_dev;

struct prom21_xhci_pdata {
	struct pci_dev *pdev;
	void __iomem *regs;
	resource_size_t rsrc_len;
};

#endif
