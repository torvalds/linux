/*
 * ACPI helpers for DMA request / controller
 *
 * Based on of_dma.h
 *
 * Copyright (C) 2013, Intel Corporation
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_ACPI_DMA_H
#define __LINUX_ACPI_DMA_H

#include <linux/list.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/dmaengine.h>

/**
 * struct acpi_dma_spec - slave device DMA resources
 * @chan_id:	channel unique id
 * @slave_id:	request line unique id
 * @dev:	struct device of the DMA controller to be used in the filter
 *		function
 */
struct acpi_dma_spec {
	int		chan_id;
	int		slave_id;
	struct device	*dev;
};

/**
 * struct acpi_dma - representation of the registered DMAC
 * @dma_controllers:	linked list node
 * @dev:		struct device of this controller
 * @acpi_dma_xlate:	callback function to find a suitable channel
 * @data:		private data used by a callback function
 * @base_request_line:	first supported request line (CSRT)
 * @end_request_line:	last supported request line (CSRT)
 */
struct acpi_dma {
	struct list_head	dma_controllers;
	struct device		*dev;
	struct dma_chan		*(*acpi_dma_xlate)
				(struct acpi_dma_spec *, struct acpi_dma *);
	void			*data;
	unsigned short		base_request_line;
	unsigned short		end_request_line;
};

/* Used with acpi_dma_simple_xlate() */
struct acpi_dma_filter_info {
	dma_cap_mask_t	dma_cap;
	dma_filter_fn	filter_fn;
};

#ifdef CONFIG_DMA_ACPI

int acpi_dma_controller_register(struct device *dev,
		struct dma_chan *(*acpi_dma_xlate)
		(struct acpi_dma_spec *, struct acpi_dma *),
		void *data);
int acpi_dma_controller_free(struct device *dev);
int devm_acpi_dma_controller_register(struct device *dev,
		struct dma_chan *(*acpi_dma_xlate)
		(struct acpi_dma_spec *, struct acpi_dma *),
		void *data);
void devm_acpi_dma_controller_free(struct device *dev);

struct dma_chan *acpi_dma_request_slave_chan_by_index(struct device *dev,
						      size_t index);
struct dma_chan *acpi_dma_request_slave_chan_by_name(struct device *dev,
						     const char *name);

struct dma_chan *acpi_dma_simple_xlate(struct acpi_dma_spec *dma_spec,
				       struct acpi_dma *adma);
#else

static inline int acpi_dma_controller_register(struct device *dev,
		struct dma_chan *(*acpi_dma_xlate)
		(struct acpi_dma_spec *, struct acpi_dma *),
		void *data)
{
	return -ENODEV;
}
static inline int acpi_dma_controller_free(struct device *dev)
{
	return -ENODEV;
}
static inline int devm_acpi_dma_controller_register(struct device *dev,
		struct dma_chan *(*acpi_dma_xlate)
		(struct acpi_dma_spec *, struct acpi_dma *),
		void *data)
{
	return -ENODEV;
}
static inline void devm_acpi_dma_controller_free(struct device *dev)
{
}

static inline struct dma_chan *acpi_dma_request_slave_chan_by_index(
		struct device *dev, size_t index)
{
	return ERR_PTR(-ENODEV);
}
static inline struct dma_chan *acpi_dma_request_slave_chan_by_name(
		struct device *dev, const char *name)
{
	return ERR_PTR(-ENODEV);
}

#define acpi_dma_simple_xlate	NULL

#endif

#define acpi_dma_request_slave_channel	acpi_dma_request_slave_chan_by_index

#endif /* __LINUX_ACPI_DMA_H */
