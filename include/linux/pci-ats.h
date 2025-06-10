/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_PCI_ATS_H
#define LINUX_PCI_ATS_H

#include <linux/pci.h>

#ifdef CONFIG_PCI_ATS
/* Address Translation Service */
bool pci_ats_supported(struct pci_dev *dev);
int pci_enable_ats(struct pci_dev *dev, int ps);
int pci_prepare_ats(struct pci_dev *dev, int ps);
void pci_disable_ats(struct pci_dev *dev);
int pci_ats_queue_depth(struct pci_dev *dev);
int pci_ats_page_aligned(struct pci_dev *dev);
#else /* CONFIG_PCI_ATS */
static inline bool pci_ats_supported(struct pci_dev *d)
{ return false; }
static inline int pci_enable_ats(struct pci_dev *d, int ps)
{ return -ENODEV; }
static inline int pci_prepare_ats(struct pci_dev *dev, int ps)
{ return -ENODEV; }
static inline void pci_disable_ats(struct pci_dev *d) { }
static inline int pci_ats_queue_depth(struct pci_dev *d)
{ return -ENODEV; }
static inline int pci_ats_page_aligned(struct pci_dev *dev)
{ return 0; }
#endif /* CONFIG_PCI_ATS */

#ifdef CONFIG_PCI_PRI
int pci_enable_pri(struct pci_dev *pdev, u32 reqs);
void pci_disable_pri(struct pci_dev *pdev);
int pci_reset_pri(struct pci_dev *pdev);
int pci_prg_resp_pasid_required(struct pci_dev *pdev);
bool pci_pri_supported(struct pci_dev *pdev);
#else
static inline bool pci_pri_supported(struct pci_dev *pdev)
{ return false; }
#endif /* CONFIG_PCI_PRI */

#ifdef CONFIG_PCI_PASID
int pci_enable_pasid(struct pci_dev *pdev, int features);
void pci_disable_pasid(struct pci_dev *pdev);
int pci_pasid_features(struct pci_dev *pdev);
int pci_max_pasids(struct pci_dev *pdev);
int pci_pasid_status(struct pci_dev *pdev);
#else /* CONFIG_PCI_PASID */
static inline int pci_enable_pasid(struct pci_dev *pdev, int features)
{ return -EINVAL; }
static inline void pci_disable_pasid(struct pci_dev *pdev) { }
static inline int pci_pasid_features(struct pci_dev *pdev)
{ return -EINVAL; }
static inline int pci_max_pasids(struct pci_dev *pdev)
{ return -EINVAL; }
static inline int pci_pasid_status(struct pci_dev *pdev)
{ return -EINVAL; }
#endif /* CONFIG_PCI_PASID */

#endif /* LINUX_PCI_ATS_H */
