/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright (C) 2023-24 Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef __SDW_AMD_H
#define __SDW_AMD_H

#include <linux/acpi.h>
#include <linux/soundwire/sdw.h>

/* AMD pm_runtime quirk definitions */

/*
 * Force the clock to stop(ClockStopMode0) when suspend callback
 * is invoked.
 */
#define AMD_SDW_CLK_STOP_MODE		1

/*
 * Stop the bus when runtime suspend/system level suspend callback
 * is invoked. If set, a complete bus reset and re-enumeration will
 * be performed when the bus restarts. In-band wake interrupts are
 * not supported in this mode.
 */
#define AMD_SDW_POWER_OFF_MODE		2
#define ACP_SDW0	0
#define ACP_SDW1	1
#define AMD_SDW_MAX_MANAGER_COUNT	2

struct acp_sdw_pdata {
	u16 instance;
	/* mutex to protect acp common register access */
	struct mutex *acp_sdw_lock;
};

/**
 * struct sdw_amd_dai_runtime: AMD sdw dai runtime  data
 *
 * @name: SoundWire stream name
 * @stream: stream runtime
 * @bus: Bus handle
 * @stream_type: Stream type
 */
struct sdw_amd_dai_runtime {
	char *name;
	struct sdw_stream_runtime *stream;
	struct sdw_bus *bus;
	enum sdw_stream_type stream_type;
};

/**
 * struct amd_sdw_manager - amd manager driver context
 * @bus: bus handle
 * @dev: linux device
 * @mmio: SoundWire registers mmio base
 * @acp_mmio: acp registers mmio base
 * @amd_sdw_irq_thread: SoundWire manager irq workqueue
 * @amd_sdw_work: peripheral status work queue
 * @acp_sdw_lock: mutex to protect acp share register access
 * @status: peripheral devices status array
 * @num_din_ports: number of input ports
 * @num_dout_ports: number of output ports
 * @cols_index: Column index in frame shape
 * @rows_index: Rows index in frame shape
 * @instance: SoundWire manager instance
 * @quirks: SoundWire manager quirks
 * @wake_en_mask: wake enable mask per SoundWire manager
 * @clk_stopped: flag set to true when clock is stopped
 * @power_mode_mask: flag interprets amd SoundWire manager power mode
 * @dai_runtime_array: dai runtime array
 */
struct amd_sdw_manager {
	struct sdw_bus bus;
	struct device *dev;

	void __iomem *mmio;
	void __iomem *acp_mmio;

	struct work_struct amd_sdw_irq_thread;
	struct work_struct amd_sdw_work;
	/* mutex to protect acp common register access */
	struct mutex *acp_sdw_lock;

	enum sdw_slave_status status[SDW_MAX_DEVICES + 1];

	int num_din_ports;
	int num_dout_ports;

	int cols_index;
	int rows_index;

	u32 instance;
	u32 quirks;
	u32 wake_en_mask;
	u32 power_mode_mask;
	bool clk_stopped;

	struct sdw_amd_dai_runtime **dai_runtime_array;
};

/**
 * struct sdw_amd_acpi_info - Soundwire AMD information found in ACPI tables
 * @handle: ACPI controller handle
 * @count: maximum no of soundwire manager links supported on AMD platform.
 * @link_mask: bit-wise mask listing links enabled by BIOS menu
 */
struct sdw_amd_acpi_info {
	acpi_handle handle;
	int count;
	u32 link_mask;
};

/**
 * struct sdw_amd_ctx - context allocated by the controller driver probe
 *
 * @count: link count
 * @num_slaves: total number of devices exposed across all enabled links
 * @link_mask: bit-wise mask listing SoundWire links reported by the
 * Controller
 * @ids: array of slave_id, representing Slaves exposed across all enabled
 * links
 * @pdev: platform device structure
 */
struct sdw_amd_ctx {
	int count;
	int num_slaves;
	u32 link_mask;
	struct sdw_extended_slave_id *ids;
	struct platform_device *pdev[AMD_SDW_MAX_MANAGER_COUNT];
};

/**
 * struct sdw_amd_res - Soundwire AMD global resource structure,
 * typically populated by the DSP driver/Legacy driver
 *
 * @addr: acp pci device resource start address
 * @reg_range: ACP register range
 * @link_mask: bit-wise mask listing links selected by the DSP driver/
 * legacy driver
 * @count: link count
 * @mmio_base: mmio base of SoundWire registers
 * @handle: ACPI parent handle
 * @parent: parent device
 * @dev: device implementing hwparams and free callbacks
 * @acp_lock: mutex protecting acp common registers access
 */
struct sdw_amd_res {
	u32 addr;
	u32 reg_range;
	u32 link_mask;
	int count;
	void __iomem *mmio_base;
	acpi_handle handle;
	struct device *parent;
	struct device *dev;
	/* use to protect acp common registers access */
	struct mutex *acp_lock;
};

int sdw_amd_probe(struct sdw_amd_res *res, struct sdw_amd_ctx **ctx);

void sdw_amd_exit(struct sdw_amd_ctx *ctx);

int sdw_amd_get_slave_info(struct sdw_amd_ctx *ctx);

int amd_sdw_scan_controller(struct sdw_amd_acpi_info *info);
#endif
