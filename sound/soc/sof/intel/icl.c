// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Author: Fred Oh <fred.oh@linux.intel.com>
//

/*
 * Hardware interface for audio DSP on IceLake.
 */

#include <linux/kernel.h>
#include <linux/kconfig.h>
#include <linux/export.h>
#include <linux/bits.h>
#include "../ops.h"
#include "hda.h"
#include "hda-ipc.h"
#include "../sof-audio.h"

#define ICL_DSP_HPRO_CORE_ID 3

static const struct snd_sof_debugfs_map icl_dsp_debugfs[] = {
	{"hda", HDA_DSP_HDA_BAR, 0, 0x4000, SOF_DEBUGFS_ACCESS_ALWAYS},
	{"pp", HDA_DSP_PP_BAR,  0, 0x1000, SOF_DEBUGFS_ACCESS_ALWAYS},
	{"dsp", HDA_DSP_BAR,  0, 0x10000, SOF_DEBUGFS_ACCESS_ALWAYS},
};

static int icl_dsp_core_stall(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;

	/* make sure core_mask in host managed cores */
	core_mask &= chip->host_managed_cores_mask;
	if (!core_mask) {
		dev_err(sdev->dev, "error: core_mask is not in host managed cores\n");
		return -EINVAL;
	}

	/* stall core */
	snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPCS,
					 HDA_DSP_ADSPCS_CSTALL_MASK(core_mask),
					 HDA_DSP_ADSPCS_CSTALL_MASK(core_mask));

	return 0;
}

/*
 * post fw run operation for ICL.
 * Core 3 will be powered up and in stall when HPRO is enabled
 */
static int icl_dsp_post_fw_run(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	int ret;

	if (sdev->first_boot) {
		ret = hda_sdw_startup(sdev);
		if (ret < 0) {
			dev_err(sdev->dev, "error: could not startup SoundWire links\n");
			return ret;
		}
	}

	hda_sdw_int_enable(sdev, true);

	/*
	 * The recommended HW programming sequence for ICL is to
	 * power up core 3 and keep it in stall if HPRO is enabled.
	 */
	if (!hda->clk_config_lpro) {
		ret = hda_dsp_enable_core(sdev, BIT(ICL_DSP_HPRO_CORE_ID));
		if (ret < 0) {
			dev_err(sdev->dev, "error: dsp core power up failed on core %d\n",
				ICL_DSP_HPRO_CORE_ID);
			return ret;
		}

		sdev->enabled_cores_mask |= BIT(ICL_DSP_HPRO_CORE_ID);
		sdev->dsp_core_ref_count[ICL_DSP_HPRO_CORE_ID]++;

		snd_sof_dsp_stall(sdev, BIT(ICL_DSP_HPRO_CORE_ID));
	}

	/* re-enable clock gating and power gating */
	return hda_dsp_ctrl_clock_power_gating(sdev, true);
}

/* Icelake ops */
const struct snd_sof_dsp_ops sof_icl_ops = {
	/* probe/remove/shutdown */
	.probe		= hda_dsp_probe,
	.remove		= hda_dsp_remove,
	.shutdown	= hda_dsp_shutdown,

	/* Register IO */
	.write		= sof_io_write,
	.read		= sof_io_read,
	.write64	= sof_io_write64,
	.read64		= sof_io_read64,

	/* Block IO */
	.block_read	= sof_block_read,
	.block_write	= sof_block_write,

	/* Mailbox IO */
	.mailbox_read	= sof_mailbox_read,
	.mailbox_write	= sof_mailbox_write,

	/* doorbell */
	.irq_thread	= cnl_ipc_irq_thread,

	/* ipc */
	.send_msg	= cnl_ipc_send_msg,
	.fw_ready	= sof_fw_ready,
	.get_mailbox_offset = hda_dsp_ipc_get_mailbox_offset,
	.get_window_offset = hda_dsp_ipc_get_window_offset,

	.ipc_msg_data	= hda_ipc_msg_data,
	.set_stream_data_offset = hda_set_stream_data_offset,

	/* machine driver */
	.machine_select = hda_machine_select,
	.machine_register = sof_machine_register,
	.machine_unregister = sof_machine_unregister,
	.set_mach_params = hda_set_mach_params,

	/* debug */
	.debug_map	= icl_dsp_debugfs,
	.debug_map_count	= ARRAY_SIZE(icl_dsp_debugfs),
	.dbg_dump	= hda_dsp_dump,
	.ipc_dump	= cnl_ipc_dump,
	.debugfs_add_region_item = snd_sof_debugfs_add_region_item_iomem,

	/* stream callbacks */
	.pcm_open	= hda_dsp_pcm_open,
	.pcm_close	= hda_dsp_pcm_close,
	.pcm_hw_params	= hda_dsp_pcm_hw_params,
	.pcm_hw_free	= hda_dsp_stream_hw_free,
	.pcm_trigger	= hda_dsp_pcm_trigger,
	.pcm_pointer	= hda_dsp_pcm_pointer,
	.pcm_ack	= hda_dsp_pcm_ack,

	/* firmware loading */
	.load_firmware = snd_sof_load_firmware_raw,

	/* pre/post fw run */
	.pre_fw_run = hda_dsp_pre_fw_run,
	.post_fw_run = icl_dsp_post_fw_run,

	/* parse platform specific extended manifest */
	.parse_platform_ext_manifest = hda_dsp_ext_man_get_cavs_config_data,

	/* dsp core get/put */
	.core_get = hda_dsp_core_get,

	/* firmware run */
	.run = hda_dsp_cl_boot_firmware_iccmax,
	.stall = icl_dsp_core_stall,

	/* trace callback */
	.trace_init = hda_dsp_trace_init,
	.trace_release = hda_dsp_trace_release,
	.trace_trigger = hda_dsp_trace_trigger,

	/* client ops */
	.register_ipc_clients = hda_register_clients,
	.unregister_ipc_clients = hda_unregister_clients,

	/* DAI drivers */
	.drv		= skl_dai,
	.num_drv	= SOF_SKL_NUM_DAIS,

	/* PM */
	.suspend		= hda_dsp_suspend,
	.resume			= hda_dsp_resume,
	.runtime_suspend	= hda_dsp_runtime_suspend,
	.runtime_resume		= hda_dsp_runtime_resume,
	.runtime_idle		= hda_dsp_runtime_idle,
	.set_hw_params_upon_resume = hda_dsp_set_hw_params_upon_resume,
	.set_power_state	= hda_dsp_set_power_state,

	/* ALSA HW info flags */
	.hw_info =	SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_PAUSE |
			SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,

	.dsp_arch_ops = &sof_xtensa_arch_ops,
};
EXPORT_SYMBOL_NS(sof_icl_ops, SND_SOC_SOF_INTEL_HDA_COMMON);

const struct sof_intel_dsp_desc icl_chip_info = {
	/* Icelake */
	.cores_num = 4,
	.init_core_mask = 1,
	.host_managed_cores_mask = GENMASK(3, 0),
	.ipc_req = CNL_DSP_REG_HIPCIDR,
	.ipc_req_mask = CNL_DSP_REG_HIPCIDR_BUSY,
	.ipc_ack = CNL_DSP_REG_HIPCIDA,
	.ipc_ack_mask = CNL_DSP_REG_HIPCIDA_DONE,
	.ipc_ctl = CNL_DSP_REG_HIPCCTL,
	.rom_init_timeout	= 300,
	.ssp_count = ICL_SSP_COUNT,
	.ssp_base_offset = CNL_SSP_BASE_OFFSET,
	.sdw_shim_base = SDW_SHIM_BASE,
	.sdw_alh_base = SDW_ALH_BASE,
	.check_sdw_irq	= hda_common_check_sdw_irq,
};
EXPORT_SYMBOL_NS(icl_chip_info, SND_SOC_SOF_INTEL_HDA_COMMON);
