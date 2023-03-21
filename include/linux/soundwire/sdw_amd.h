/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2023 Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef __SDW_AMD_H
#define __SDW_AMD_H

#include <linux/soundwire/sdw.h>

#define ACP_SDW0	0
#define ACP_SDW1	1

struct acp_sdw_pdata {
	u16 instance;
	/* mutex to protect acp common register access */
	struct mutex *acp_sdw_lock;
};

struct sdw_manager_reg_mask {
	u32 sw_pad_enable_mask;
	u32 sw_pad_pulldown_mask;
	u32 acp_sdw_intr_mask;
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
 * @reg_mask: register mask structure per manager instance
 * @amd_sdw_irq_thread: SoundWire manager irq workqueue
 * @amd_sdw_work: peripheral status work queue
 * @probe_work: SoundWire manager probe workqueue
 * @acp_sdw_lock: mutex to protect acp share register access
 * @status: peripheral devices status array
 * @num_din_ports: number of input ports
 * @num_dout_ports: number of output ports
 * @cols_index: Column index in frame shape
 * @rows_index: Rows index in frame shape
 * @instance: SoundWire manager instance
 * @quirks: SoundWire manager quirks
 * @wake_en_mask: wake enable mask per SoundWire manager
 * @power_mode_mask: flag interprets amd SoundWire manager power mode
 * @dai_runtime_array: dai runtime array
 */
struct amd_sdw_manager {
	struct sdw_bus bus;
	struct device *dev;

	void __iomem *mmio;
	void __iomem *acp_mmio;

	struct sdw_manager_reg_mask *reg_mask;
	struct work_struct amd_sdw_irq_thread;
	struct work_struct amd_sdw_work;
	struct work_struct probe_work;
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

	struct sdw_amd_dai_runtime **dai_runtime_array;
};
#endif
