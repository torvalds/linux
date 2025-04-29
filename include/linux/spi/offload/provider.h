/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Analog Devices Inc.
 * Copyright (C) 2024 BayLibre, SAS
 */

#ifndef __LINUX_SPI_OFFLOAD_PROVIDER_H
#define __LINUX_SPI_OFFLOAD_PROVIDER_H

#include <linux/module.h>
#include <linux/spi/offload/types.h>
#include <linux/types.h>

MODULE_IMPORT_NS("SPI_OFFLOAD");

struct device;
struct spi_offload_trigger;

struct spi_offload *devm_spi_offload_alloc(struct device *dev, size_t priv_size);

struct spi_offload_trigger_ops {
	bool (*match)(struct spi_offload_trigger *trigger,
		      enum spi_offload_trigger_type type, u64 *args, u32 nargs);
	int (*request)(struct spi_offload_trigger *trigger,
		       enum spi_offload_trigger_type type, u64 *args, u32 nargs);
	void (*release)(struct spi_offload_trigger *trigger);
	int (*validate)(struct spi_offload_trigger *trigger,
			struct spi_offload_trigger_config *config);
	int (*enable)(struct spi_offload_trigger *trigger,
		      struct spi_offload_trigger_config *config);
	void (*disable)(struct spi_offload_trigger *trigger);
};

struct spi_offload_trigger_info {
	/** @fwnode: Provider fwnode, used to match to consumer. */
	struct fwnode_handle *fwnode;
	/** @ops: Provider-specific callbacks. */
	const struct spi_offload_trigger_ops *ops;
	/** Provider-specific state to be used in callbacks. */
	void *priv;
};

int devm_spi_offload_trigger_register(struct device *dev,
				      struct spi_offload_trigger_info *info);
void *spi_offload_trigger_get_priv(struct spi_offload_trigger *trigger);

#endif /* __LINUX_SPI_OFFLOAD_PROVIDER_H */
