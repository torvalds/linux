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
extern void pcie_aspm_init_link_state(struct pci_dev *pdev);
extern void pcie_aspm_exit_link_state(struct pci_dev *pdev);
extern void pcie_aspm_pm_state_change(struct pci_dev *pdev);
extern void pci_disable_link_state(struct pci_dev *pdev, int state);
#else
#define pcie_aspm_init_link_state(pdev)		do {} while (0)
#define pcie_aspm_exit_link_state(pdev)		do {} while (0)
#define pcie_aspm_pm_state_change(pdev)		do {} while (0)
#define pci_disable_link_state(pdev, state)	do {} while (0)
#endif

#ifdef CONFIG_PCIEASPM_DEBUG /* this depends on CONFIG_PCIEASPM */
extern void pcie_aspm_create_sysfs_dev_files(struct pci_dev *pdev);
extern void pcie_aspm_remove_sysfs_dev_files(struct pci_dev *pdev);
#else
#define pcie_aspm_create_sysfs_dev_files(pdev)	do {} while (0)
#define pcie_aspm_remove_sysfs_dev_files(pdev)	do {} while (0)
#endif
#endif /* LINUX_ASPM_H */
