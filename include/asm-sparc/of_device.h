#ifndef _ASM_SPARC_OF_DEVICE_H
#define _ASM_SPARC_OF_DEVICE_H
#ifdef __KERNEL__

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <asm/openprom.h>
#include <asm/prom.h>

extern struct bus_type ebus_bus_type;
extern struct bus_type sbus_bus_type;
extern struct bus_type of_bus_type;

/*
 * The of_device is a kind of "base class" that is a superset of
 * struct device for use by devices attached to an OF node and
 * probed using OF properties.
 */
struct of_device
{
	struct device_node		*node;
	struct device			dev;
	struct resource			resource[PROMREG_MAX];
	unsigned int			irqs[PROMINTR_MAX];
	int				num_irqs;

	void				*sysdata;

	int				slot;
	int				portid;
	int				clock_freq;
};
#define	to_of_device(d) container_of(d, struct of_device, dev)

extern void __iomem *of_ioremap(struct resource *res, unsigned long offset, unsigned long size, char *name);
extern void of_iounmap(struct resource *res, void __iomem *base, unsigned long size);

extern struct of_device *of_find_device_by_node(struct device_node *);

extern const struct of_device_id *of_match_device(
	const struct of_device_id *matches, const struct of_device *dev);

extern struct of_device *of_dev_get(struct of_device *dev);
extern void of_dev_put(struct of_device *dev);

/*
 * An of_platform_driver driver is attached to a basic of_device on
 * the ISA, EBUS, and SBUS busses on sparc64.
 */
struct of_platform_driver
{
	char			*name;
	struct of_device_id	*match_table;
	struct module		*owner;

	int	(*probe)(struct of_device* dev, const struct of_device_id *match);
	int	(*remove)(struct of_device* dev);

	int	(*suspend)(struct of_device* dev, pm_message_t state);
	int	(*resume)(struct of_device* dev);
	int	(*shutdown)(struct of_device* dev);

	struct device_driver	driver;
};
#define	to_of_platform_driver(drv) container_of(drv,struct of_platform_driver, driver)

extern int of_register_driver(struct of_platform_driver *drv,
			      struct bus_type *bus);
extern void of_unregister_driver(struct of_platform_driver *drv);
extern int of_device_register(struct of_device *ofdev);
extern void of_device_unregister(struct of_device *ofdev);
extern struct of_device *of_platform_device_create(struct device_node *np,
						   const char *bus_id,
						   struct device *parent,
						   struct bus_type *bus);
extern void of_release_dev(struct device *dev);

#endif /* __KERNEL__ */
#endif /* _ASM_SPARC_OF_DEVICE_H */
