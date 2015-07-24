#ifndef __LINUX_ULPI_INTERFACE_H
#define __LINUX_ULPI_INTERFACE_H

#include <linux/types.h>

struct ulpi;

/**
 * struct ulpi_ops - ULPI register access
 * @dev: the interface provider
 * @read: read operation for ULPI register access
 * @write: write operation for ULPI register access
 */
struct ulpi_ops {
	struct device *dev;
	int (*read)(struct ulpi_ops *ops, u8 addr);
	int (*write)(struct ulpi_ops *ops, u8 addr, u8 val);
};

struct ulpi *ulpi_register_interface(struct device *, struct ulpi_ops *);
void ulpi_unregister_interface(struct ulpi *);

#endif /* __LINUX_ULPI_INTERFACE_H */
