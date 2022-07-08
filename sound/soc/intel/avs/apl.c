// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/devcoredump.h>
#include <linux/slab.h>
#include "avs.h"
#include "messages.h"
#include "path.h"
#include "topology.h"

static int apl_enable_logs(struct avs_dev *adev, enum avs_log_enable enable, u32 aging_period,
			   u32 fifo_full_period, unsigned long resource_mask, u32 *priorities)
{
	struct apl_log_state_info *info;
	u32 size, num_cores = adev->hw_cfg.dsp_cores;
	int ret, i;

	if (fls_long(resource_mask) > num_cores)
		return -EINVAL;
	size = struct_size(info, logs_core, num_cores);
	info = kzalloc(size, GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->aging_timer_period = aging_period;
	info->fifo_full_timer_period = fifo_full_period;
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

static int apl_log_buffer_status(struct avs_dev *adev, union avs_notify_msg *msg)
{
	struct apl_log_buffer_layout layout;
	unsigned long flags;
	void __iomem *addr, *buf;

	addr = avs_log_buffer_addr(adev, msg->log.core);
	if (!addr)
		return -ENXIO;

	memcpy_fromio(&layout, addr, sizeof(layout));

	spin_lock_irqsave(&adev->dbg.trace_lock, flags);
	if (!kfifo_initialized(&adev->dbg.trace_fifo))
		/* consume the logs regardless of consumer presence */
		goto update_read_ptr;

	buf = apl_log_payload_addr(addr);

	if (layout.read_ptr > layout.write_ptr) {
		__kfifo_fromio_locked(&adev->dbg.trace_fifo, buf + layout.read_ptr,
				      apl_log_payload_size(adev) - layout.read_ptr,
				      &adev->dbg.fifo_lock);
		layout.read_ptr = 0;
	}
	__kfifo_fromio_locked(&adev->dbg.trace_fifo, buf + layout.read_ptr,
			      layout.write_ptr - layout.read_ptr, &adev->dbg.fifo_lock);

	wake_up(&adev->dbg.trace_waitq);

update_read_ptr:
	spin_unlock_irqrestore(&adev->dbg.trace_lock, flags);
	writel(layout.write_ptr, addr);
	return 0;
}

static int apl_wait_log_entry(struct avs_dev *adev, u32 core, struct apl_log_buffer_layout *layout)
{
	unsigned long timeout;
	void __iomem *addr;

	addr = avs_log_buffer_addr(adev, core);
	if (!addr)
		return -ENXIO;

	timeout = jiffies + msecs_to_jiffies(10);

	do {
		memcpy_fromio(layout, addr, sizeof(*layout));
		if (layout->read_ptr != layout->write_ptr)
			return 0;
		usleep_range(500, 1000);
	} while (!time_after(jiffies, timeout));

	return -ETIMEDOUT;
}

/* reads log header and tests its type */
#define apl_is_entry_stackdump(addr) ((readl(addr) >> 30) & 0x1)

static int apl_coredump(struct avs_dev *adev, union avs_notify_msg *msg)
{
	struct apl_log_buffer_layout layout;
	void __iomem *addr, *buf;
	size_t dump_size;
	u16 offset = 0;
	u8 *dump, *pos;

	dump_size = AVS_FW_REGS_SIZE + msg->ext.coredump.stack_dump_size;
	dump = vzalloc(dump_size);
	if (!dump)
		return -ENOMEM;

	memcpy_fromio(dump, avs_sram_addr(adev, AVS_FW_REGS_WINDOW), AVS_FW_REGS_SIZE);

	if (!msg->ext.coredump.stack_dump_size)
		goto exit;

	/* Dump the registers even if an external error prevents gathering the stack. */
	addr = avs_log_buffer_addr(adev, msg->ext.coredump.core_id);
	if (!addr)
		goto exit;

	buf = apl_log_payload_addr(addr);
	memcpy_fromio(&layout, addr, sizeof(layout));
	if (!apl_is_entry_stackdump(buf + layout.read_ptr)) {
		/*
		 * DSP awaits the remaining logs to be
		 * gathered before dumping stack
		 */
		msg->log.core = msg->ext.coredump.core_id;
		avs_dsp_op(adev, log_buffer_status, msg);
	}

	pos = dump + AVS_FW_REGS_SIZE;
	/* gather the stack */
	do {
		u32 count;

		if (apl_wait_log_entry(adev, msg->ext.coredump.core_id, &layout))
			break;

		if (layout.read_ptr > layout.write_ptr) {
			count = apl_log_payload_size(adev) - layout.read_ptr;
			memcpy_fromio(pos + offset, buf + layout.read_ptr, count);
			layout.read_ptr = 0;
			offset += count;
		}
		count = layout.write_ptr - layout.read_ptr;
		memcpy_fromio(pos + offset, buf + layout.read_ptr, count);
		offset += count;

		/* update read pointer */
		writel(layout.write_ptr, addr);
	} while (offset < msg->ext.coredump.stack_dump_size);

exit:
	dev_coredumpv(adev->dev, dump, dump_size, GFP_KERNEL);

	return 0;
}

static bool apl_lp_streaming(struct avs_dev *adev)
{
	struct avs_path *path;

	/* Any gateway without buffer allocated in LP area disqualifies D0IX. */
	list_for_each_entry(path, &adev->path_list, node) {
		struct avs_path_pipeline *ppl;

		list_for_each_entry(ppl, &path->ppl_list, node) {
			struct avs_path_module *mod;

			list_for_each_entry(mod, &ppl->mod_list, node) {
				struct avs_tplg_modcfg_ext *cfg;

				cfg = mod->template->cfg_ext;

				/* only copiers have gateway attributes */
				if (!guid_equal(&cfg->type, &AVS_COPIER_MOD_UUID))
					continue;
				/* non-gateway copiers do not prevent PG */
				if (cfg->copier.dma_type == INVALID_OBJECT_ID)
					continue;

				if (!mod->gtw_attrs.lp_buffer_alloc)
					return false;
			}
		}
	}

	return true;
}

static bool apl_d0ix_toggle(struct avs_dev *adev, struct avs_ipc_msg *tx, bool wake)
{
	/* wake in all cases */
	if (wake)
		return true;

	/*
	 * If no pipelines are running, allow for d0ix schedule.
	 * If all gateways have lp=1, allow for d0ix schedule.
	 * If any gateway with lp=0 is allocated, abort scheduling d0ix.
	 *
	 * Note: for cAVS 1.5+ and 1.8, D0IX is LP-firmware transition,
	 * not the power-gating mechanism known from cAVS 2.0.
	 */
	return apl_lp_streaming(adev);
}

static int apl_set_d0ix(struct avs_dev *adev, bool enable)
{
	bool streaming = false;
	int ret;

	if (enable)
		/* Either idle or all gateways with lp=1. */
		streaming = !list_empty(&adev->path_list);

	ret = avs_ipc_set_d0ix(adev, enable, streaming);
	return AVS_IPC_RET(ret);
}

const struct avs_dsp_ops apl_dsp_ops = {
	.power = avs_dsp_core_power,
	.reset = avs_dsp_core_reset,
	.stall = avs_dsp_core_stall,
	.irq_handler = avs_dsp_irq_handler,
	.irq_thread = avs_dsp_irq_thread,
	.int_control = avs_dsp_interrupt_control,
	.load_basefw = avs_hda_load_basefw,
	.load_lib = avs_hda_load_library,
	.transfer_mods = avs_hda_transfer_modules,
	.enable_logs = apl_enable_logs,
	.log_buffer_offset = skl_log_buffer_offset,
	.log_buffer_status = apl_log_buffer_status,
	.coredump = apl_coredump,
	.d0ix_toggle = apl_d0ix_toggle,
	.set_d0ix = apl_set_d0ix,
};
