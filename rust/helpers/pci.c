// SPDX-License-Identifier: GPL-2.0

#include <linux/pci.h>

resource_size_t rust_helper_pci_resource_len(struct pci_dev *pdev, int bar)
{
	return pci_resource_len(pdev, bar);
}

bool rust_helper_dev_is_pci(const struct device *dev)
{
	return dev_is_pci(dev);
}
