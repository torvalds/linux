#ifndef __OF_PCI_H
#define __OF_PCI_H

#include <linux/pci.h>
#include <linux/msi.h>

struct pci_dev;
struct of_phandle_args;
struct device_node;

#ifdef CONFIG_OF_PCI
int of_irq_parse_pci(const struct pci_dev *pdev, struct of_phandle_args *out_irq);
struct device_node *of_pci_find_child_device(struct device_node *parent,
					     unsigned int devfn);
int of_pci_get_devfn(struct device_node *np);
int of_irq_parse_and_map_pci(const struct pci_dev *dev, u8 slot, u8 pin);
int of_pci_parse_bus_range(struct device_node *node, struct resource *res);
int of_get_pci_domain_nr(struct device_node *node);
int of_pci_get_max_link_speed(struct device_node *node);
void of_pci_check_probe_only(void);
int of_pci_map_rid(struct device_node *np, u32 rid,
		   const char *map_name, const char *map_mask_name,
		   struct device_node **target, u32 *id_out);
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

static inline int
of_get_pci_domain_nr(struct device_node *node)
{
	return -1;
}

static inline int of_pci_map_rid(struct device_node *np, u32 rid,
			const char *map_name, const char *map_mask_name,
			struct device_node **target, u32 *id_out)
{
	return -EINVAL;
}

static inline int
of_pci_get_max_link_speed(struct device_node *node)
{
	return -EINVAL;
}

static inline void of_pci_check_probe_only(void) { }
#endif

#if defined(CONFIG_OF_ADDRESS)
int of_pci_get_host_bridge_resources(struct device_node *dev,
			unsigned char busno, unsigned char bus_max,
			struct list_head *resources, resource_size_t *io_base);
#else
static inline int of_pci_get_host_bridge_resources(struct device_node *dev,
			unsigned char busno, unsigned char bus_max,
			struct list_head *resources, resource_size_t *io_base)
{
	return -EINVAL;
}
#endif

#endif
