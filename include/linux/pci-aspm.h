/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	aspm.h
 *
 *	PCI Express ASPM defines and function prototypes
 *
 *	Copyright (C) 2007 Intel Corp.
 *		Zhang Yanmin (yanmin.zhang@intel.com)
 *		Shaohua Li (shaohua.li@intel.com)
 *
 *	For more information, please consult the following manuals (look at
 *	http://www.pcisig.com/ for how to get them):
 *
 *	PCI Express Specification
 */

#ifndef LINUX_ASPM_H
#define LINUX_ASPM_H

#include <linux/pci.h>

#define PCIE_LINK_STATE_L0S	1
#define PCIE_LINK_STATE_L1	2
#define PCIE_LINK_STATE_CLKPM	4

#ifdef CONFIG_PCIEASPM
void pci_disable_link_state(struct pci_dev *pdev, int state);
void pci_disable_link_state_locked(struct pci_dev *pdev, int state);
void pcie_no_aspm(void);
#else
static inline void pci_disable_link_state(struct pci_dev *pdev, int state) { }
static inline void pcie_no_aspm(void) { }
#endif

#endif /* LINUX_ASPM_H */
