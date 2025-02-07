/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Analog Devices Inc.
 * Copyright (C) 2024 BayLibre, SAS
 */

#ifndef __LINUX_SPI_OFFLOAD_PROVIDER_H
#define __LINUX_SPI_OFFLOAD_PROVIDER_H

#include <linux/module.h>
#include <linux/types.h>

MODULE_IMPORT_NS("SPI_OFFLOAD");

struct device;

struct spi_offload *devm_spi_offload_alloc(struct device *dev, size_t priv_size);

#endif /* __LINUX_SPI_OFFLOAD_PROVIDER_H */
