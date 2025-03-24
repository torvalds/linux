// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation
//

/*
 * common ops for SKL+ HDAudio platforms
 */

#include "../sof-priv.h"
#include "hda.h"
#include "../sof-audio.h"

const struct snd_sof_dsp_ops sof_hda_common_ops = {
	/* probe/remove/shutdown */
	.probe_early	= hda_dsp_probe_early,
	.probe		= hda_dsp_probe,
	.remove		= hda_dsp_remove,
	.remove_late	= hda_dsp_remove_late,

	/* Register IO uses direct mmio */

	/* Block IO */
	.block_read	= sof_block_read,
	.block_write	= sof_block_write,

	/* Mailbox IO */
	.mailbox_read	= sof_mailbox_read,
	.mailbox_write	= sof_mailbox_write,

	/* ipc */
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
	.dbg_dump	= hda_dsp_dump,
	.debugfs_add_region_item = snd_sof_debugfs_add_region_item_iomem,

	/* stream callbacks */
	.pcm_open	= hda_dsp_pcm_open,
	.pcm_close	= hda_dsp_pcm_close,
	.pcm_hw_params	= hda_dsp_pcm_hw_params,
	.pcm_hw_free	= hda_dsp_stream_hw_free,
	.pcm_trigger	= hda_dsp_pcm_trigger,
	.pcm_pointer	= hda_dsp_pcm_pointer,
	.pcm_ack	= hda_dsp_pcm_ack,

	.get_dai_frame_counter = hda_dsp_get_stream_llp,
	.get_host_byte_counter = hda_dsp_get_stream_ldp,

	/* firmware loading */
	.load_firmware = snd_sof_load_firmware_raw,

	/* pre/post fw run */
	.pre_fw_run = hda_dsp_pre_fw_run,

	/* firmware run */
	.run = hda_dsp_cl_boot_firmware,

	/* parse platform specific extended manifest */
	.parse_platform_ext_manifest = hda_dsp_ext_man_get_cavs_config_data,

	/* dsp core get/put */

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
	.is_chain_dma_supported	= hda_is_chain_dma_supported,

	/* PM */
	.suspend		= hda_dsp_suspend,
	.resume			= hda_dsp_resume,
	.runtime_suspend	= hda_dsp_runtime_suspend,
	.runtime_resume		= hda_dsp_runtime_resume,
	.runtime_idle		= hda_dsp_runtime_idle,
	.set_hw_params_upon_resume = hda_dsp_set_hw_params_upon_resume,

	/* ALSA HW info flags */
	.hw_info =	SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_PAUSE |
			SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,

	.dsp_arch_ops = &sof_xtensa_arch_ops,
};
EXPORT_SYMBOL_NS(sof_hda_common_ops, "SND_SOC_SOF_INTEL_HDA_GENERIC");
