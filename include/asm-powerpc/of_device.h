#ifndef _ASM_POWERPC_OF_DEVICE_H
#define _ASM_POWERPC_OF_DEVICE_H
#ifdef __KERNEL__

#include <linux/device.h>
#include <linux/of.h>

/*
 * The of_device is a kind of "base class" that is a superset of
 * struct device for use by devices attached to an OF node and
 * probed using OF properties.
 */
struct of_device
{
	struct device_node	*node;		/* to be obsoleted */
	u64			dma_mask;	/* DMA mask */
	struct device		dev;		/* Generic device interface */
};

extern ssize_t of_device_get_modalias(struct of_device *ofdev,
					char *str, ssize_t len);
extern int of_device_uevent(struct device *dev,
	char **envp, int num_envp, char *buffer, int buffer_size);

/* This is just here during the transition */
#include <linux/of_device.h>

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_OF_DEVICE_H */
