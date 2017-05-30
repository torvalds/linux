#ifndef LINUX_PCI_ATS_H
#define LINUX_PCI_ATS_H

#include <linux/pci.h>

#ifdef CONFIG_PCI_PRI

int pci_enable_pri(struct pci_dev *pdev, u32 reqs);
void pci_disable_pri(struct pci_dev *pdev);
void pci_restore_pri_state(struct pci_dev *pdev);
int pci_reset_pri(struct pci_dev *pdev);

#else /* CONFIG_PCI_PRI */

static inline int pci_enable_pri(struct pci_dev *pdev, u32 reqs)
{
	return -ENODEV;
}

static inline void pci_disable_pri(struct pci_dev *pdev)
{
}

static inline void pci_restore_pri_state(struct pci_dev *pdev)
{
}

static inline int pci_reset_pri(struct pci_dev *pdev)
{
	return -ENODEV;
}

#endif /* CONFIG_PCI_PRI */

#ifdef CONFIG_PCI_PASID

int pci_enable_pasid(struct pci_dev *pdev, int features);
void pci_disable_pasid(struct pci_dev *pdev);
void pci_restore_pasid_state(struct pci_dev *pdev);
int pci_pasid_features(struct pci_dev *pdev);
int pci_max_pasids(struct pci_dev *pdev);

#else  /* CONFIG_PCI_PASID */

static inline int pci_enable_pasid(struct pci_dev *pdev, int features)
{
	return -EINVAL;
}

static inline void pci_disable_pasid(struct pci_dev *pdev)
{
}

static inline void pci_restore_pasid_state(struct pci_dev *pdev)
{
}

static inline int pci_pasid_features(struct pci_dev *pdev)
{
	return -EINVAL;
}

static inline int pci_max_pasids(struct pci_dev *pdev)
{
	return -EINVAL;
}

#endif /* CONFIG_PCI_PASID */


#endif /* LINUX_PCI_ATS_H*/
