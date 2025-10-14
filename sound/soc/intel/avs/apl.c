// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/devcoredump.h>
#include <linux/slab.h>
#include <sound/hdaudio_ext.h>
#include "avs.h"
#include "debug.h"
#include "messages.h"
#include "path.h"
#include "registers.h"
#include "topology.h"

static irqreturn_t avs_apl_dsp_interrupt(struct avs_dev *adev)
{
	u32 adspis = snd_hdac_adsp_readl(adev, AVS_ADSP_REG_ADSPIS);
	irqreturn_t ret = IRQ_NONE;

	if (adspis == UINT_MAX)
		return ret;

	if (adspis & AVS_ADSP_ADSPIS_IPC) {
		avs_skl_ipc_interrupt(adev);
		ret = IRQ_HANDLED;
	}

	return ret;
}

#ifdef CONFIG_DEBUG_FS
int avs_apl_enable_logs(struct avs_dev *adev, enum avs_log_enable enable, u32 aging_period,
			u32 fifo_full_period, unsigned long resource_mask, u32 *priorities)
{
	struct avs_apl_log_state_info *info;
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
#endif

int avs_apl_log_buffer_status(struct avs_dev *adev, union avs_notify_msg *msg)
{
	struct avs_apl_log_buffer_layout layout;
	void __iomem *addr, *buf;

	addr = avs_log_buffer_addr(adev, msg->log.core);
	if (!addr)
		return -ENXIO;

	memcpy_fromio(&layout, addr, sizeof(layout));

	if (!avs_logging_fw(adev))
		/* consume the logs regardless of consumer presence */
		goto update_read_ptr;

	buf = avs_apl_log_payload_addr(addr);

	if (layout.read_ptr > layout.write_ptr) {
		avs_dump_fw_log(adev, buf + layout.read_ptr,
				avs_apl_log_payload_size(adev) - layout.read_ptr);
		layout.read_ptr = 0;
	}
	avs_dump_fw_log_wakeup(adev, buf + layout.read_ptr, layout.write_ptr - layout.read_ptr);

update_read_ptr:
	writel(layout.write_ptr, addr);
	return 0;
}

static int avs_apl_wait_log_entry(struct avs_dev *adev, u32 core,
				  struct avs_apl_log_buffer_layout *layout)
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
#define avs_apl_is_entry_stackdump(addr) ((readl(addr) >> 30) & 0x1)

int avs_apl_coredump(struct avs_dev *adev, union avs_notify_msg *msg)
{
	struct avs_apl_log_buffer_layout layout;
	void __iomem *addr, *buf;
	size_t dump_size;
	u32 offset = 0;
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

	buf = avs_apl_log_payload_addr(addr);
	memcpy_fromio(&layout, addr, sizeof(layout));
	if (!avs_apl_is_entry_stackdump(buf + layout.read_ptr)) {
		union avs_notify_msg lbs_msg = AVS_NOTIFICATION(LOG_BUFFER_STATUS);

		/*
		 * DSP awaits the remaining logs to be
		 * gathered before dumping stack
		 */
		lbs_msg.log.core = msg->ext.coredump.core_id;
		avs_log_buffer_status_locked(adev, &lbs_msg);
	}

	pos = dump + AVS_FW_REGS_SIZE;
	/* gather the stack */
	do {
		u32 count;

		if (avs_apl_wait_log_entry(adev, msg->ext.coredump.core_id, &layout))
			break;

		if (layout.read_ptr > layout.write_ptr) {
			count = avs_apl_log_payload_size(adev) - layout.read_ptr;
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

static bool avs_apl_lp_streaming(struct avs_dev *adev)
{
	struct avs_path *path;

	spin_lock(&adev->path_list_lock);
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

				if (!mod->gtw_attrs.lp_buffer_alloc) {
					spin_unlock(&adev->path_list_lock);
					return false;
				}
			}
		}
	}
	spin_unlock(&adev->path_list_lock);

	return true;
}

bool avs_apl_d0ix_toggle(struct avs_dev *adev, struct avs_ipc_msg *tx, bool wake)
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
	return avs_apl_lp_streaming(adev);
}

int avs_apl_set_d0ix(struct avs_dev *adev, bool enable)
{
	bool streaming = false;
	int ret;

	if (enable)
		/* Either idle or all gateways with lp=1. */
		streaming = !list_empty(&adev->path_list);

	ret = avs_ipc_set_d0ix(adev, enable, streaming);
	return AVS_IPC_RET(ret);
}

const struct avs_dsp_ops avs_apl_dsp_ops = {
	.power = avs_dsp_core_power,
	.reset = avs_dsp_core_reset,
	.stall = avs_dsp_core_stall,
	.dsp_interrupt = avs_apl_dsp_interrupt,
	.int_control = avs_dsp_interrupt_control,
	.load_basefw = avs_hda_load_basefw,
	.load_lib = avs_hda_load_library,
	.transfer_mods = avs_hda_transfer_modules,
	.log_buffer_offset = avs_skl_log_buffer_offset,
	.log_buffer_status = avs_apl_log_buffer_status,
	.coredump = avs_apl_coredump,
	.d0ix_toggle = avs_apl_d0ix_toggle,
	.set_d0ix = avs_apl_set_d0ix,
	AVS_SET_ENABLE_LOGS_OP(apl)
};
