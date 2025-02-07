/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Analog Devices Inc.
 * Copyright (C) 2024 BayLibre, SAS
 */

#ifndef __LINUX_SPI_OFFLOAD_TYPES_H
#define __LINUX_SPI_OFFLOAD_TYPES_H

#include <linux/types.h>

struct device;

/* Offload can be triggered by external hardware event. */
#define SPI_OFFLOAD_CAP_TRIGGER			BIT(0)
/* Offload can record and then play back TX data when triggered. */
#define SPI_OFFLOAD_CAP_TX_STATIC_DATA		BIT(1)
/* Offload can get TX data from an external stream source. */
#define SPI_OFFLOAD_CAP_TX_STREAM_DMA		BIT(2)
/* Offload can send RX data to an external stream sink. */
#define SPI_OFFLOAD_CAP_RX_STREAM_DMA		BIT(3)

/**
 * struct spi_offload_config - offload configuration
 *
 * This is used to request an offload with specific configuration.
 */
struct spi_offload_config {
	/** @capability_flags: required capabilities. See %SPI_OFFLOAD_CAP_* */
	u32 capability_flags;
};

/**
 * struct spi_offload - offload instance
 */
struct spi_offload {
	/** @provider_dev: for get/put reference counting */
	struct device *provider_dev;
	/** @priv: provider driver private data */
	void *priv;
};

#endif /* __LINUX_SPI_OFFLOAD_TYPES_H */
