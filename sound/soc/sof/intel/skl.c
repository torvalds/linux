// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Authors: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//	    Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//	    Rander Wang <rander.wang@intel.com>
//          Keyon Jie <yang.jie@linux.intel.com>
//

/*
 * Hardware interface for audio DSP on Skylake and Kabylake.
 */

#include "../sof-priv.h"
#include "hda.h"

static const struct snd_sof_debugfs_map skl_dsp_debugfs[] = {
	{"hda", HDA_DSP_HDA_BAR, 0, 0x4000},
	{"pp", HDA_DSP_PP_BAR,  0, 0x1000},
	{"dsp", HDA_DSP_BAR,  0, 0x10000},
};

/* skylake ops */
const struct snd_sof_dsp_ops sof_skl_ops = {
	/* probe and remove */
	.probe		= hda_dsp_probe,
	.remove		= hda_dsp_remove,

	/* Register IO */
	.write		= sof_io_write,
	.read		= sof_io_read,
	.write64	= sof_io_write64,
	.read64		= sof_io_read64,

	/* Block IO */
	.block_read	= sof_block_read,
	.block_write	= sof_block_write,

	/* doorbell */
	.irq_handler	= hda_dsp_ipc_irq_handler,
	.irq_thread	= hda_dsp_ipc_irq_thread,

	/* mailbox */
	.mailbox_read	= sof_mailbox_read,
	.mailbox_write	= sof_mailbox_write,

	/* ipc */
	.send_msg	= hda_dsp_ipc_send_msg,
	.get_reply	= hda_dsp_ipc_get_reply,
	.fw_ready	= hda_dsp_ipc_fw_ready,
	.is_ready	= hda_dsp_ipc_is_ready,
	.cmd_done	= hda_dsp_ipc_cmd_done,

	/* debug */
	.debug_map	= skl_dsp_debugfs,
	.debug_map_count	= ARRAY_SIZE(skl_dsp_debugfs),
	.dbg_dump	= hda_dsp_dump_skl,

	/* stream callbacks */
	.pcm_open	= hda_dsp_pcm_open,
	.pcm_close	= hda_dsp_pcm_close,
	.pcm_hw_params	= hda_dsp_pcm_hw_params,
	.pcm_trigger	= hda_dsp_pcm_trigger,

	/* firmware loading */
	.load_firmware = hda_dsp_cl_load_fw,

	/* pre/post fw run */
	.pre_fw_run = hda_dsp_pre_fw_run,
	.post_fw_run = hda_dsp_post_fw_run,

	/* dsp core power up/down */
	.core_power_up = hda_dsp_enable_core,
	.core_power_down = hda_dsp_core_reset_power_down,

	/* firmware run */
	.run = hda_dsp_cl_boot_firmware_skl,

	/* trace callback */
	.trace_init = hda_dsp_trace_init,
	.trace_release = hda_dsp_trace_release,
	.trace_trigger = hda_dsp_trace_trigger,

	/* DAI drivers */
	.drv		= skl_dai,
	.num_drv	= SOF_SKL_NUM_DAIS,
};
EXPORT_SYMBOL(sof_skl_ops);

const struct sof_intel_dsp_desc skl_chip_info = {
	/* Apollolake */
	.cores_num = 2,
	.cores_mask = HDA_DSP_CORE_MASK(0) | HDA_DSP_CORE_MASK(1),
	.ipc_req = HDA_DSP_REG_HIPCI,
	.ipc_req_mask = HDA_DSP_REG_HIPCI_BUSY,
	.ipc_ack = HDA_DSP_REG_HIPCIE,
	.ipc_ack_mask = HDA_DSP_REG_HIPCIE_DONE,
	.ipc_ctl = HDA_DSP_REG_HIPCCTL,
};
EXPORT_SYMBOL(skl_chip_info);
