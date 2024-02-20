// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2024 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/slab.h>
#include "avs.h"
#include "messages.h"

#ifdef CONFIG_DEBUG_FS
int avs_icl_enable_logs(struct avs_dev *adev, enum avs_log_enable enable, u32 aging_period,
			u32 fifo_full_period, unsigned long resource_mask, u32 *priorities)
{
	struct avs_icl_log_state_info *info;
	u32 size, num_libs = adev->fw_cfg.max_libs_count;
	int i, ret;

	if (fls_long(resource_mask) > num_libs)
		return -EINVAL;
	size = struct_size(info, logs_priorities_mask, num_libs);
	info = kzalloc(size, GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->aging_timer_period = aging_period;
	info->fifo_full_timer_period = fifo_full_period;
	info->enable = enable;
	if (enable)
		for_each_set_bit(i, &resource_mask, num_libs)
			info->logs_priorities_mask[i] = *priorities++;

	ret = avs_ipc_set_enable_logs(adev, (u8 *)info, size);
	kfree(info);
	if (ret)
		return AVS_IPC_RET(ret);

	return 0;
}
#endif

union avs_icl_memwnd2_slot_type {
	u32 val;
	struct {
		u32 resource_id:8;
		u32 type:24;
	};
} __packed;

struct avs_icl_memwnd2_desc {
	u32 resource_id;
	union avs_icl_memwnd2_slot_type slot_id;
	u32 vma;
} __packed;

#define AVS_ICL_MEMWND2_SLOTS_COUNT	15

struct avs_icl_memwnd2 {
	union {
		struct avs_icl_memwnd2_desc slot_desc[AVS_ICL_MEMWND2_SLOTS_COUNT];
		u8 rsvd[PAGE_SIZE];
	};
	u8 slot_array[AVS_ICL_MEMWND2_SLOTS_COUNT][PAGE_SIZE];
} __packed;

#define AVS_ICL_SLOT_UNUSED \
	((union avs_icl_memwnd2_slot_type) { 0x00000000U })
#define AVS_ICL_SLOT_CRITICAL_LOG \
	((union avs_icl_memwnd2_slot_type) { 0x54524300U })
#define AVS_ICL_SLOT_DEBUG_LOG \
	((union avs_icl_memwnd2_slot_type) { 0x474f4c00U })
#define AVS_ICL_SLOT_GDB_STUB \
	((union avs_icl_memwnd2_slot_type) { 0x42444700U })
#define AVS_ICL_SLOT_BROKEN \
	((union avs_icl_memwnd2_slot_type) { 0x44414544U })

static int avs_icl_slot_offset(struct avs_dev *adev, union avs_icl_memwnd2_slot_type slot_type)
{
	struct avs_icl_memwnd2_desc desc[AVS_ICL_MEMWND2_SLOTS_COUNT];
	int i;

	memcpy_fromio(&desc, avs_sram_addr(adev, AVS_DEBUG_WINDOW), sizeof(desc));

	for (i = 0; i < AVS_ICL_MEMWND2_SLOTS_COUNT; i++)
		if (desc[i].slot_id.val == slot_type.val)
			return offsetof(struct avs_icl_memwnd2, slot_array) +
			       avs_skl_log_buffer_offset(adev, i);
	return -ENXIO;
}

int avs_icl_log_buffer_offset(struct avs_dev *adev, u32 core)
{
	union avs_icl_memwnd2_slot_type slot_type = AVS_ICL_SLOT_DEBUG_LOG;
	int ret;

	slot_type.resource_id = core;
	ret = avs_icl_slot_offset(adev, slot_type);
	if (ret < 0)
		dev_dbg(adev->dev, "No slot offset found for: %x\n",
			slot_type.val);

	return ret;
}

bool avs_icl_d0ix_toggle(struct avs_dev *adev, struct avs_ipc_msg *tx, bool wake)
{
	/* Payload-less IPCs do not take part in d0ix toggling. */
	return tx->size;
}

int avs_icl_set_d0ix(struct avs_dev *adev, bool enable)
{
	int ret;

	ret = avs_ipc_set_d0ix(adev, enable, false);
	return AVS_IPC_RET(ret);
}

const struct avs_dsp_ops avs_icl_dsp_ops = {
	.power = avs_dsp_core_power,
	.reset = avs_dsp_core_reset,
	.stall = avs_dsp_core_stall,
	.irq_handler = avs_irq_handler,
	.irq_thread = avs_cnl_irq_thread,
	.int_control = avs_dsp_interrupt_control,
	.load_basefw = avs_hda_load_basefw,
	.load_lib = avs_hda_load_library,
	.transfer_mods = avs_hda_transfer_modules,
	.log_buffer_offset = avs_icl_log_buffer_offset,
	.log_buffer_status = avs_apl_log_buffer_status,
	.coredump = avs_apl_coredump,
	.d0ix_toggle = avs_icl_d0ix_toggle,
	.set_d0ix = avs_icl_set_d0ix,
	AVS_SET_ENABLE_LOGS_OP(icl)
};
