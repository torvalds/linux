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
extern void pcie_aspm_powersave_config_link(struct pci_dev *pdev);
extern void pci_disable_link_state(struct pci_dev *pdev, int state);
extern void pcie_clear_aspm(void);
extern void pcie_no_aspm(void);
#else
static inline void pcie_aspm_init_link_state(struct pci_dev *pdev)
{
}
static inline void pcie_aspm_exit_link_state(struct pci_dev *pdev)
{
}
static inline void pcie_aspm_pm_state_change(struct pci_dev *pdev)
{
}
static inline void pcie_aspm_powersave_config_link(struct pci_dev *pdev)
{
}
static inline void pci_disable_link_state(struct pci_dev *pdev, int state)
{
}
static inline void pcie_clear_aspm(void)
{
}
static inline void pcie_no_aspm(void)
{
}
#endif

#ifdef CONFIG_PCIEASPM_DEBUG /* this depends on CONFIG_PCIEASPM */
extern void pcie_aspm_create_sysfs_dev_files(struct pci_dev *pdev);
extern void pcie_aspm_remove_sysfs_dev_files(struct pci_dev *pdev);
#else
static inline void pcie_aspm_create_sysfs_dev_files(struct pci_dev *pdev)
{
}
static inline void pcie_aspm_remove_sysfs_dev_files(struct pci_dev *pdev)
{
}
#endif
#endif /* LINUX_ASPM_H */
