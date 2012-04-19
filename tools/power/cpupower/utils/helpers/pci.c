#if defined(__i386__) || defined(__x86_64__)

#include <helpers/helpers.h>

/*
 * pci_acc_init
 *
 * PCI access helper function depending on libpci
 *
 * **pacc : if a valid pci_dev is returned
 *         *pacc must be passed to pci_acc_cleanup to free it
 *
 * domain: domain
 * bus:    bus
 * slot:   slot
 * func:   func
 * vendor: vendor
 * device: device
 * Pass -1 for one of the six above to match any
 *
 * Returns :
 * struct pci_dev which can be used with pci_{read,write}_* functions
 *                to access the PCI config space of matching pci devices
 */
struct pci_dev *pci_acc_init(struct pci_access **pacc, int domain, int bus,
			     int slot, int func, int vendor, int dev)
{
	struct pci_filter filter_nb_link = { domain, bus, slot, func,
					     vendor, dev };
	struct pci_dev *device;

	*pacc = pci_alloc();
	if (*pacc == NULL)
		return NULL;

	pci_init(*pacc);
	pci_scan_bus(*pacc);

	for (device = (*pacc)->devices; device; device = device->next) {
		if (pci_filter_match(&filter_nb_link, device))
			return device;
	}
	pci_cleanup(*pacc);
	return NULL;
}

/* Typically one wants to get a specific slot(device)/func of the root domain
   and bus */
struct pci_dev *pci_slot_func_init(struct pci_access **pacc, int slot,
				       int func)
{
	return pci_acc_init(pacc, 0, 0, slot, func, -1, -1);
}

#endif /* defined(__i386__) || defined(__x86_64__) */
