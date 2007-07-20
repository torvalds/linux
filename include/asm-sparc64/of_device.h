#ifndef _ASM_SPARC64_OF_DEVICE_H
#define _ASM_SPARC64_OF_DEVICE_H
#ifdef __KERNEL__

#include <linux/device.h>
#include <linux/of.h>
#include <linux/mod_devicetable.h>
#include <asm/openprom.h>

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

extern void __iomem *of_ioremap(struct resource *res, unsigned long offset, unsigned long size, char *name);
extern void of_iounmap(struct resource *res, void __iomem *base, unsigned long size);

/* These are just here during the transition */
#include <linux/of_device.h>
#include <linux/of_platform.h>

#endif /* __KERNEL__ */
#endif /* _ASM_SPARC64_OF_DEVICE_H */
