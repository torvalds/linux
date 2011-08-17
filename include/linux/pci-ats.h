#ifndef LINUX_PCI_ATS_H
#define LINUX_PCI_ATS_H

/* Address Translation Service */
struct pci_ats {
	int pos;        /* capability position */
	int stu;        /* Smallest Translation Unit */
	int qdep;       /* Invalidate Queue Depth */
	int ref_cnt;    /* Physical Function reference count */
	unsigned int is_enabled:1;      /* Enable bit is set */
};

#ifdef CONFIG_PCI_IOV

extern int pci_enable_ats(struct pci_dev *dev, int ps);
extern void pci_disable_ats(struct pci_dev *dev);
extern int pci_ats_queue_depth(struct pci_dev *dev);
/**
 * pci_ats_enabled - query the ATS status
 * @dev: the PCI device
 *
 * Returns 1 if ATS capability is enabled, or 0 if not.
 */
static inline int pci_ats_enabled(struct pci_dev *dev)
{
	return dev->ats && dev->ats->is_enabled;
}

#else /* CONFIG_PCI_IOV */

static inline int pci_enable_ats(struct pci_dev *dev, int ps)
{
	return -ENODEV;
}

static inline void pci_disable_ats(struct pci_dev *dev)
{
}

static inline int pci_ats_queue_depth(struct pci_dev *dev)
{
	return -ENODEV;
}

static inline int pci_ats_enabled(struct pci_dev *dev)
{
	return 0;
}

#endif /* CONFIG_PCI_IOV */

#endif /* LINUX_PCI_ATS_H*/
