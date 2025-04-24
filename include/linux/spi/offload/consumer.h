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

struct spi_offload_trigger
*devm_spi_offload_trigger_get(struct device *dev,
			      struct spi_offload *offload,
			      enum spi_offload_trigger_type type);
int spi_offload_trigger_validate(struct spi_offload_trigger *trigger,
				 struct spi_offload_trigger_config *config);
int spi_offload_trigger_enable(struct spi_offload *offload,
			       struct spi_offload_trigger *trigger,
			       struct spi_offload_trigger_config *config);
void spi_offload_trigger_disable(struct spi_offload *offload,
				 struct spi_offload_trigger *trigger);

struct dma_chan *devm_spi_offload_tx_stream_request_dma_chan(struct device *dev,
							     struct spi_offload *offload);
struct dma_chan *devm_spi_offload_rx_stream_request_dma_chan(struct device *dev,
							     struct spi_offload *offload);

#endif /* __LINUX_SPI_OFFLOAD_CONSUMER_H */
