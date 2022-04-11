/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/* Copyright(c) 2015-17 Intel Corporation. */

#ifndef __SDW_INTEL_H
#define __SDW_INTEL_H

/**
 * struct sdw_intel_ops: Intel audio driver callback ops
 *
 * @config_stream: configure the stream with the hw_params
 * the first argument containing the context is mandatory
 */
struct sdw_intel_ops {
	int (*config_stream)(void *arg, void *substream,
			     void *dai, void *hw_params, int stream_num);
};

/**
 * struct sdw_intel_res - Soundwire Intel resource structure
 * @mmio_base: mmio base of SoundWire registers
 * @irq: interrupt number
 * @handle: ACPI parent handle
 * @parent: parent device
 * @ops: callback ops
 * @arg: callback arg
 */
struct sdw_intel_res {
	void __iomem *mmio_base;
	int irq;
	acpi_handle handle;
	struct device *parent;
	const struct sdw_intel_ops *ops;
	void *arg;
};

void *sdw_intel_init(acpi_handle *parent_handle, struct sdw_intel_res *res);
void sdw_intel_exit(void *arg);

#endif
