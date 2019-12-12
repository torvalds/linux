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
 * struct sdw_intel_acpi_info - Soundwire Intel information found in ACPI tables
 * @handle: ACPI controller handle
 * @count: link count found with "sdw-master-count" property
 * @link_mask: bit-wise mask listing links enabled by BIOS menu
 *
 * this structure could be expanded to e.g. provide all the _ADR
 * information in case the link_mask is not sufficient to identify
 * platform capabilities.
 */
struct sdw_intel_acpi_info {
	acpi_handle handle;
	int count;
	u32 link_mask;
};

struct sdw_intel_link_res;

/**
 * struct sdw_intel_ctx - context allocated by the controller
 * driver probe
 * @count: link count
 * @mmio_base: mmio base of SoundWire registers, only used to check
 * hardware capabilities after all power dependencies are settled.
 * @link_mask: bit-wise mask listing SoundWire links reported by the
 * Controller
 * @handle: ACPI parent handle
 * @links: information for each link (controller-specific and kept
 * opaque here)
 */
struct sdw_intel_ctx {
	int count;
	void __iomem *mmio_base;
	u32 link_mask;
	acpi_handle handle;
	struct sdw_intel_link_res *links;
};

/**
 * struct sdw_intel_res - Soundwire Intel global resource structure,
 * typically populated by the DSP driver
 *
 * @count: link count
 * @mmio_base: mmio base of SoundWire registers
 * @irq: interrupt number
 * @handle: ACPI parent handle
 * @parent: parent device
 * @ops: callback ops
 * @dev: device implementing hwparams and free callbacks
 * @link_mask: bit-wise mask listing links selected by the DSP driver
 * This mask may be a subset of the one reported by the controller since
 * machine-specific quirks are handled in the DSP driver.
 */
struct sdw_intel_res {
	int count;
	void __iomem *mmio_base;
	int irq;
	acpi_handle handle;
	struct device *parent;
	const struct sdw_intel_ops *ops;
	struct device *dev;
	u32 link_mask;
};

/*
 * On Intel platforms, the SoundWire IP has dependencies on power
 * rails shared with the DSP, and the initialization steps are split
 * in three. First an ACPI scan to check what the firmware describes
 * in DSDT tables, then an allocation step (with no hardware
 * configuration but with all the relevant devices created) and last
 * the actual hardware configuration. The final stage is a global
 * interrupt enable which is controlled by the DSP driver. Splitting
 * these phases helps simplify the boot flow and make early decisions
 * on e.g. which machine driver to select (I2S mode, HDaudio or
 * SoundWire).
 */
int sdw_intel_acpi_scan(acpi_handle *parent_handle,
			struct sdw_intel_acpi_info *info);

struct sdw_intel_ctx *
sdw_intel_probe(struct sdw_intel_res *res);

int sdw_intel_startup(struct sdw_intel_ctx *ctx);

void sdw_intel_exit(struct sdw_intel_ctx *ctx);

void sdw_intel_enable_irq(void __iomem *mmio_base, bool enable);

#endif
