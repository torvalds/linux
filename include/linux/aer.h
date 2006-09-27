/*
 * Copyright (C) 2006 Intel Corp.
 *     Tom Long Nguyen (tom.l.nguyen@intel.com)
 *     Zhang Yanmin (yanmin.zhang@intel.com)
 */

#ifndef _AER_H_
#define _AER_H_

#if defined(CONFIG_PCIEAER)
/* pci-e port driver needs this function to enable aer */
extern int pci_enable_pcie_error_reporting(struct pci_dev *dev);
extern int pci_find_aer_capability(struct pci_dev *dev);
extern int pci_disable_pcie_error_reporting(struct pci_dev *dev);
extern int pci_cleanup_aer_uncorrect_error_status(struct pci_dev *dev);
#else
#define pci_enable_pcie_error_reporting(dev)		do { } while (0)
#define pci_find_aer_capability(dev)			do { } while (0)
#define pci_disable_pcie_error_reporting(dev)		do { } while (0)
#define pci_cleanup_aer_uncorrect_error_status(dev)	do { } while (0)
#endif

#endif //_AER_H_

