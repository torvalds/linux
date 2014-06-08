#ifndef __OF_PCI_H
#define __OF_PCI_H

#include <linux/pci.h>
#include <linux/msi.h>

struct pci_dev;
struct of_phandle_args;
struct device_node;

#ifdef CONFIG_OF
int of_irq_parse_pci(const struct pci_dev *pdev, struct of_phandle_args *out_irq);
struct device_node *of_pci_find_child_device(struct device_node *parent,
					     unsigned int devfn);
int of_pci_get_devfn(struct device_node *np);
int of_irq_parse_and_map_pci(const struct pci_dev *dev, u8 slot, u8 pin);
int of_pci_parse_bus_range(struct device_node *node, struct resource *res);
#else
static inline int of_irq_parse_pci(const struct pci_dev *pdev, struct of_phandle_args *out_irq)
{
	return 0;
}

static inline struct device_node *of_pci_find_child_device(struct device_node *parent,
					     unsigned int devfn)
{
	return NULL;
}

static inline int of_pci_get_devfn(struct device_node *np)
{
	return -EINVAL;
}

static inline int
of_irq_parse_and_map_pci(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return 0;
}

static inline int
of_pci_parse_bus_range(struct device_node *node, struct resource *res)
{
	return -EINVAL;
}
#endif

#if defined(CONFIG_OF) && defined(CONFIG_PCI_MSI)
int of_pci_msi_chip_add(struct msi_chip *chip);
void of_pci_msi_chip_remove(struct msi_chip *chip);
struct msi_chip *of_pci_find_msi_chip_by_node(struct device_node *of_node);
#else
static inline int of_pci_msi_chip_add(struct msi_chip *chip) { return -EINVAL; }
static inline void of_pci_msi_chip_remove(struct msi_chip *chip) { }
static inline struct msi_chip *
of_pci_find_msi_chip_by_node(struct device_node *of_node) { return NULL; }
#endif

#endif
