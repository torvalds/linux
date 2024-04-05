// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2024 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/slab.h>
#include <sound/hdaudio.h>
#include <sound/hdaudio_ext.h>
#include "avs.h"
#include "messages.h"

#define ICL_VS_LTRP_GB_ICCMAX	95

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
		u8 rsvd[SZ_4K];
	};
	u8 slot_array[AVS_ICL_MEMWND2_SLOTS_COUNT][SZ_4K];
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
			return offsetof(struct avs_icl_memwnd2, slot_array) + i * SZ_4K;
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

int avs_icl_load_basefw(struct avs_dev *adev, struct firmware *fw)
{
	struct hdac_bus *bus = &adev->base.core;
	struct hdac_ext_stream *host_stream;
	struct snd_pcm_substream substream;
	struct snd_dma_buffer dmab;
	unsigned int sd_fmt;
	u8 ltrp_gb;
	int ret;

	/*
	 * ICCMAX:
	 *
	 * For ICL+ platforms, as per HW recommendation LTRP_GB is set to 95us
	 * during FW load. Its original value shall be restored once load completes.
	 *
	 * To avoid DMI/OPIO L1 entry during the load procedure, additional CAPTURE
	 * stream is allocated and set to run.
	 */

	memset(&substream, 0, sizeof(substream));
	substream.stream = SNDRV_PCM_STREAM_CAPTURE;

	host_stream = snd_hdac_ext_stream_assign(bus, &substream, HDAC_EXT_STREAM_TYPE_HOST);
	if (!host_stream)
		return -EBUSY;

	ltrp_gb = snd_hdac_chip_readb(bus, VS_LTRP) & AZX_REG_VS_LTRP_GB_MASK;
	/* Carries no real data, use default format. */
	sd_fmt = snd_hdac_stream_format(1, 32, 48000);

	ret = snd_hdac_dsp_prepare(hdac_stream(host_stream), sd_fmt, fw->size, &dmab);
	if (ret < 0)
		goto release_stream;

	snd_hdac_chip_updateb(bus, VS_LTRP, AZX_REG_VS_LTRP_GB_MASK, ICL_VS_LTRP_GB_ICCMAX);

	spin_lock(&bus->reg_lock);
	snd_hdac_stream_start(hdac_stream(host_stream));
	spin_unlock(&bus->reg_lock);

	ret = avs_hda_load_basefw(adev, fw);

	spin_lock(&bus->reg_lock);
	snd_hdac_stream_stop(hdac_stream(host_stream));
	spin_unlock(&bus->reg_lock);

	snd_hdac_dsp_cleanup(hdac_stream(host_stream), &dmab);

release_stream:
	snd_hdac_ext_stream_release(host_stream, HDAC_EXT_STREAM_TYPE_HOST);
	snd_hdac_chip_updateb(bus, VS_LTRP, AZX_REG_VS_LTRP_GB_MASK, ltrp_gb);

	return ret;
}

const struct avs_dsp_ops avs_icl_dsp_ops = {
	.power = avs_dsp_core_power,
	.reset = avs_dsp_core_reset,
	.stall = avs_dsp_core_stall,
	.irq_handler = avs_irq_handler,
	.irq_thread = avs_cnl_irq_thread,
	.int_control = avs_dsp_interrupt_control,
	.load_basefw = avs_icl_load_basefw,
	.load_lib = avs_hda_load_library,
	.transfer_mods = avs_hda_transfer_modules,
	.log_buffer_offset = avs_icl_log_buffer_offset,
	.log_buffer_status = avs_apl_log_buffer_status,
	.coredump = avs_apl_coredump,
	.d0ix_toggle = avs_icl_d0ix_toggle,
	.set_d0ix = avs_icl_set_d0ix,
	AVS_SET_ENABLE_LOGS_OP(icl)
};
