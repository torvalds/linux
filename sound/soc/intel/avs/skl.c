// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/devcoredump.h>
#include <linux/slab.h>
#include <sound/hdaudio_ext.h>
#include "avs.h"
#include "messages.h"

static int __maybe_unused
skl_enable_logs(struct avs_dev *adev, enum avs_log_enable enable, u32 aging_period,
		u32 fifo_full_period, unsigned long resource_mask, u32 *priorities)
{
	struct skl_log_state_info *info;
	u32 size, num_cores = adev->hw_cfg.dsp_cores;
	int ret, i;

	if (fls_long(resource_mask) > num_cores)
		return -EINVAL;
	size = struct_size(info, logs_core, num_cores);
	info = kzalloc(size, GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->core_mask = resource_mask;
	if (enable)
		for_each_set_bit(i, &resource_mask, num_cores) {
			info->logs_core[i].enable = enable;
			info->logs_core[i].min_priority = *priorities++;
		}
	else
		for_each_set_bit(i, &resource_mask, num_cores)
			info->logs_core[i].enable = enable;

	ret = avs_ipc_set_enable_logs(adev, (u8 *)info, size);
	kfree(info);
	if (ret)
		return AVS_IPC_RET(ret);

	return 0;
}

int skl_log_buffer_offset(struct avs_dev *adev, u32 core)
{
	return core * avs_log_buffer_size(adev);
}

/* fw DbgLogWp registers */
#define FW_REGS_DBG_LOG_WP(core) (0x30 + 0x4 * core)

static int
skl_log_buffer_status(struct avs_dev *adev, union avs_notify_msg *msg)
{
	void __iomem *buf;
	u16 size, write, offset;

	if (!avs_logging_fw(adev))
		return 0;

	size = avs_log_buffer_size(adev) / 2;
	write = readl(avs_sram_addr(adev, AVS_FW_REGS_WINDOW) + FW_REGS_DBG_LOG_WP(msg->log.core));
	/* determine buffer half */
	offset = (write < size) ? size : 0;

	/* Address is guaranteed to exist in SRAM2. */
	buf = avs_log_buffer_addr(adev, msg->log.core) + offset;
	avs_dump_fw_log_wakeup(adev, buf, size);

	return 0;
}

static int skl_coredump(struct avs_dev *adev, union avs_notify_msg *msg)
{
	u8 *dump;

	dump = vzalloc(AVS_FW_REGS_SIZE);
	if (!dump)
		return -ENOMEM;

	memcpy_fromio(dump, avs_sram_addr(adev, AVS_FW_REGS_WINDOW), AVS_FW_REGS_SIZE);
	dev_coredumpv(adev->dev, dump, AVS_FW_REGS_SIZE, GFP_KERNEL);

	return 0;
}

static bool
skl_d0ix_toggle(struct avs_dev *adev, struct avs_ipc_msg *tx, bool wake)
{
	/* unsupported on cAVS 1.5 hw */
	return false;
}

static int skl_set_d0ix(struct avs_dev *adev, bool enable)
{
	/* unsupported on cAVS 1.5 hw */
	return 0;
}

const struct avs_dsp_ops skl_dsp_ops = {
	.power = avs_dsp_core_power,
	.reset = avs_dsp_core_reset,
	.stall = avs_dsp_core_stall,
	.irq_handler = avs_dsp_irq_handler,
	.irq_thread = avs_dsp_irq_thread,
	.int_control = avs_dsp_interrupt_control,
	.load_basefw = avs_cldma_load_basefw,
	.load_lib = avs_cldma_load_library,
	.transfer_mods = avs_cldma_transfer_modules,
	.log_buffer_offset = skl_log_buffer_offset,
	.log_buffer_status = skl_log_buffer_status,
	.coredump = skl_coredump,
	.d0ix_toggle = skl_d0ix_toggle,
	.set_d0ix = skl_set_d0ix,
	AVS_SET_ENABLE_LOGS_OP(skl)
};
