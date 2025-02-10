/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Analog Devices Inc.
 * Copyright (C) 2024 BayLibre, SAS
 */

#ifndef __LINUX_SPI_OFFLOAD_TYPES_H
#define __LINUX_SPI_OFFLOAD_TYPES_H

#include <linux/bits.h>
#include <linux/types.h>

struct device;

/* This is write xfer but TX uses external data stream rather than tx_buf. */
#define SPI_OFFLOAD_XFER_TX_STREAM	BIT(0)
/* This is read xfer but RX uses external data stream rather than rx_buf. */
#define SPI_OFFLOAD_XFER_RX_STREAM	BIT(1)

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
	/** @ops: callbacks for offload support */
	const struct spi_offload_ops *ops;
	/** @xfer_flags: %SPI_OFFLOAD_XFER_* flags supported by provider */
	u32 xfer_flags;
};

enum spi_offload_trigger_type {
	/* Indication from SPI peripheral that data is read to read. */
	SPI_OFFLOAD_TRIGGER_DATA_READY,
	/* Trigger comes from a periodic source such as a clock. */
	SPI_OFFLOAD_TRIGGER_PERIODIC,
};

struct spi_offload_trigger_periodic {
	u64 frequency_hz;
};

struct spi_offload_trigger_config {
	/** @type: type discriminator for union */
	enum spi_offload_trigger_type type;
	union {
		struct spi_offload_trigger_periodic periodic;
	};
};

/**
 * struct spi_offload_ops - callbacks implemented by offload providers
 */
struct spi_offload_ops {
	/**
	 * @trigger_enable: Optional callback to enable the trigger for the
	 * given offload instance.
	 */
	int (*trigger_enable)(struct spi_offload *offload);
	/**
	 * @trigger_disable: Optional callback to disable the trigger for the
	 * given offload instance.
	 */
	void (*trigger_disable)(struct spi_offload *offload);
	/**
	 * @tx_stream_request_dma_chan: Optional callback for controllers that
	 * have an offload where the TX data stream is connected directly to a
	 * DMA channel.
	 */
	struct dma_chan *(*tx_stream_request_dma_chan)(struct spi_offload *offload);
	/**
	 * @rx_stream_request_dma_chan: Optional callback for controllers that
	 * have an offload where the RX data stream is connected directly to a
	 * DMA channel.
	 */
	struct dma_chan *(*rx_stream_request_dma_chan)(struct spi_offload *offload);
};

#endif /* __LINUX_SPI_OFFLOAD_TYPES_H */
