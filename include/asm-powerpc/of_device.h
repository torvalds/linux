#ifndef _ASM_POWERPC_OF_DEVICE_H
#define _ASM_POWERPC_OF_DEVICE_H
#ifdef __KERNEL__

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <asm/prom.h>


/*
 * The of_device is a kind of "base class" that is a superset of
 * struct device for use by devices attached to an OF node and
 * probed using OF properties
 */
struct of_device
{
	struct device_node	*node;		/* to be obsoleted */
	u64			dma_mask;	/* DMA mask */
	struct device		dev;		/* Generic device interface */
};
#define	to_of_device(d) container_of(d, struct of_device, dev)

extern const struct of_device_id *of_match_node(
	const struct of_device_id *matches, const struct device_node *node);
extern const struct of_device_id *of_match_device(
	const struct of_device_id *matches, const struct of_device *dev);

extern struct of_device *of_dev_get(struct of_device *dev);
extern void of_dev_put(struct of_device *dev);

extern int of_device_register(struct of_device *ofdev);
extern void of_device_unregister(struct of_device *ofdev);
extern void of_release_dev(struct device *dev);

extern int of_device_uevent(struct device *dev,
	char **envp, int num_envp, char *buffer, int buffer_size);

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_OF_DEVICE_H */
