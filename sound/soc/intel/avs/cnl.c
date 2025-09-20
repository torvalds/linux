// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2024 Intel Corporation
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <sound/hdaudio_ext.h>
#include "avs.h"
#include "debug.h"
#include "messages.h"
#include "registers.h"

static void avs_cnl_ipc_interrupt(struct avs_dev *adev)
{
	const struct avs_spec *spec = adev->spec;
	u32 hipc_ack, hipc_rsp;

	snd_hdac_adsp_updatel(adev, spec->hipc->ctl_offset,
			      AVS_ADSP_HIPCCTL_DONE | AVS_ADSP_HIPCCTL_BUSY, 0);

	hipc_ack = snd_hdac_adsp_readl(adev, spec->hipc->ack_offset);
	hipc_rsp = snd_hdac_adsp_readl(adev, spec->hipc->rsp_offset);

	/* DSP acked host's request. */
	if (hipc_ack & spec->hipc->ack_done_mask) {
		complete(&adev->ipc->done_completion);

		/* Tell DSP it has our attention. */
		snd_hdac_adsp_updatel(adev, spec->hipc->ack_offset, spec->hipc->ack_done_mask,
				      spec->hipc->ack_done_mask);
	}

	/* DSP sent new response to process. */
	if (hipc_rsp & spec->hipc->rsp_busy_mask) {
		union avs_reply_msg msg;
		u32 hipctda;

		msg.primary = snd_hdac_adsp_readl(adev, CNL_ADSP_REG_HIPCTDR);
		msg.ext.val = snd_hdac_adsp_readl(adev, CNL_ADSP_REG_HIPCTDD);

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
	}

	snd_hdac_adsp_updatel(adev, spec->hipc->ctl_offset,
			      AVS_ADSP_HIPCCTL_DONE | AVS_ADSP_HIPCCTL_BUSY,
			      AVS_ADSP_HIPCCTL_DONE | AVS_ADSP_HIPCCTL_BUSY);
}

irqreturn_t avs_cnl_dsp_interrupt(struct avs_dev *adev)
{
	u32 adspis = snd_hdac_adsp_readl(adev, AVS_ADSP_REG_ADSPIS);
	irqreturn_t ret = IRQ_NONE;

	if (adspis == UINT_MAX)
		return ret;

	if (adspis & AVS_ADSP_ADSPIS_IPC) {
		avs_cnl_ipc_interrupt(adev);
		ret = IRQ_HANDLED;
	}

	return ret;
}

const struct avs_dsp_ops avs_cnl_dsp_ops = {
	.power = avs_dsp_core_power,
	.reset = avs_dsp_core_reset,
	.stall = avs_dsp_core_stall,
	.dsp_interrupt = avs_cnl_dsp_interrupt,
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
