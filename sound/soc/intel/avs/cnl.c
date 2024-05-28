// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2024 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <sound/hdaudio_ext.h>
#include "avs.h"
#include "messages.h"

irqreturn_t avs_cnl_irq_thread(struct avs_dev *adev)
{
	union avs_reply_msg msg;
	u32 hipctdr, hipctdd, hipctda;

	hipctdr = snd_hdac_adsp_readl(adev, CNL_ADSP_REG_HIPCTDR);
	hipctdd = snd_hdac_adsp_readl(adev, CNL_ADSP_REG_HIPCTDD);

	/* Ensure DSP sent new response to process. */
	if (!(hipctdr & CNL_ADSP_HIPCTDR_BUSY))
		return IRQ_NONE;

	msg.primary = hipctdr;
	msg.ext.val = hipctdd;
	avs_dsp_process_response(adev, msg.val);

	/* Tell DSP we accepted its message. */
	snd_hdac_adsp_updatel(adev, CNL_ADSP_REG_HIPCTDR,
			      CNL_ADSP_HIPCTDR_BUSY, CNL_ADSP_HIPCTDR_BUSY);
	/* Ack this response. */
	snd_hdac_adsp_updatel(adev, CNL_ADSP_REG_HIPCTDA,
			      CNL_ADSP_HIPCTDA_DONE, CNL_ADSP_HIPCTDA_DONE);
	/* HW might have been clock gated, give some time for change to propagate. */
	snd_hdac_adsp_readl_poll(adev, CNL_ADSP_REG_HIPCTDA, hipctda,
				 !(hipctda & CNL_ADSP_HIPCTDA_DONE), 10, 1000);
	/* Unmask busy interrupt. */
	snd_hdac_adsp_updatel(adev, CNL_ADSP_REG_HIPCCTL,
			      AVS_ADSP_HIPCCTL_BUSY, AVS_ADSP_HIPCCTL_BUSY);

	return IRQ_HANDLED;
}

const struct avs_dsp_ops avs_cnl_dsp_ops = {
	.power = avs_dsp_core_power,
	.reset = avs_dsp_core_reset,
	.stall = avs_dsp_core_stall,
	.irq_handler = avs_irq_handler,
	.irq_thread = avs_cnl_irq_thread,
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
