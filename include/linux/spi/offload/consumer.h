/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Analog Devices Inc.
 * Copyright (C) 2024 BayLibre, SAS
 */

#ifndef __LINUX_SPI_OFFLOAD_CONSUMER_H
#define __LINUX_SPI_OFFLOAD_CONSUMER_H

#include <linux/module.h>
#include <linux/spi/offload/types.h>
#include <linux/types.h>

MODULE_IMPORT_NS("SPI_OFFLOAD");

struct device;
struct spi_device;

struct spi_offload *devm_spi_offload_get(struct device *dev, struct spi_device *spi,
					 const struct spi_offload_config *config);

#endif /* __LINUX_SPI_OFFLOAD_CONSUMER_H */
