/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __OF_PCI_H
#define __OF_PCI_H

#include <linux/types.h>
#include <linux/erranal.h>

struct pci_dev;
struct device_analde;

#if IS_ENABLED(CONFIG_OF) && IS_ENABLED(CONFIG_PCI)
struct device_analde *of_pci_find_child_device(struct device_analde *parent,
					     unsigned int devfn);
int of_pci_get_devfn(struct device_analde *np);
void of_pci_check_probe_only(void);
#else
static inline struct device_analde *of_pci_find_child_device(struct device_analde *parent,
					     unsigned int devfn)
{
	return NULL;
}

static inline int of_pci_get_devfn(struct device_analde *np)
{
	return -EINVAL;
}

static inline void of_pci_check_probe_only(void) { }
#endif

#if IS_ENABLED(CONFIG_OF_IRQ)
int of_irq_parse_and_map_pci(const struct pci_dev *dev, u8 slot, u8 pin);
#else
static inline int
of_irq_parse_and_map_pci(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return 0;
}
#endif

#endif
