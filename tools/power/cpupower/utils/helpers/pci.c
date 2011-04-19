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
 * vendor_id : the pci vendor id matching the pci device to access
 * dev_ids :   device ids matching the pci device to access
 *
 * Returns :
 * struct pci_dev which can be used with pci_{read,write}_* functions
 *                to access the PCI config space of matching pci devices
 */
struct pci_dev *pci_acc_init(struct pci_access **pacc, int vendor_id,
				    int *dev_ids)
{
	struct pci_filter filter_nb_link = { -1, -1, -1, -1, vendor_id, 0};
	struct pci_dev *device;
	unsigned int i;

	*pacc = pci_alloc();
	if (*pacc == NULL)
		return NULL;

	pci_init(*pacc);
	pci_scan_bus(*pacc);

	for (i = 0; dev_ids[i] != 0; i++) {
		filter_nb_link.device = dev_ids[i];
		for (device = (*pacc)->devices; device; device = device->next) {
			if (pci_filter_match(&filter_nb_link, device))
				return device;
		}
	}
	pci_cleanup(*pacc);
	return NULL;
}
#endif /* defined(__i386__) || defined(__x86_64__) */
