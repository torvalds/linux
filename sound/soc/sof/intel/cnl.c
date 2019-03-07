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
 * Hardware interface for audio DSP on Cannonlake.
 */

#include "../ops.h"
#include "hda.h"

static const struct snd_sof_debugfs_map cnl_dsp_debugfs[] = {
	{"hda", HDA_DSP_HDA_BAR, 0, 0x4000, SOF_DEBUGFS_ACCESS_ALWAYS},
	{"pp", HDA_DSP_PP_BAR,  0, 0x1000, SOF_DEBUGFS_ACCESS_ALWAYS},
	{"dsp", HDA_DSP_BAR,  0, 0x10000, SOF_DEBUGFS_ACCESS_ALWAYS},
};

static int cnl_ipc_cmd_done(struct snd_sof_dev *sdev, int dir);

static irqreturn_t cnl_ipc_irq_thread(int irq, void *context)
{
	struct snd_sof_dev *sdev = (struct snd_sof_dev *)context;
	u32 hipci;
	u32 hipcctl;
	u32 hipcida;
	u32 hipctdr;
	u32 hipctdd;
	u32 msg;
	u32 msg_ext;
	irqreturn_t ret = IRQ_NONE;

	/* here we handle IPC interrupts only */
	if (!(sdev->irq_status & HDA_DSP_ADSPIS_IPC))
		return ret;

	hipcida = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDA);
	hipcctl = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCCTL);

	/* reply message from DSP */
	if (hipcida & CNL_DSP_REG_HIPCIDA_DONE &&
	    hipcctl & CNL_DSP_REG_HIPCCTL_DONE) {
		hipci = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
					 CNL_DSP_REG_HIPCIDR);
		msg_ext = hipci & CNL_DSP_REG_HIPCIDR_MSG_MASK;
		msg = hipcida & CNL_DSP_REG_HIPCIDA_MSG_MASK;

		dev_vdbg(sdev->dev,
			 "ipc: firmware response, msg:0x%x, msg_ext:0x%x\n",
			 msg, msg_ext);

		/* mask Done interrupt */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
					CNL_DSP_REG_HIPCCTL,
					CNL_DSP_REG_HIPCCTL_DONE, 0);

		/*
		 * handle immediate reply from DSP core. If the msg is
		 * found, set done bit in cmd_done which is called at the
		 * end of message processing function, else set it here
		 * because the done bit can't be set in cmd_done function
		 * which is triggered by msg
		 */
		if (snd_sof_ipc_reply(sdev, msg))
			cnl_ipc_cmd_done(sdev, SOF_IPC_DSP_REPLY);

		ret = IRQ_HANDLED;
	}

	hipctdr = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCTDR);

	/* new message from DSP */
	if (hipctdr & CNL_DSP_REG_HIPCTDR_BUSY) {
		hipctdd = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
					   CNL_DSP_REG_HIPCTDD);
		msg = hipctdr & CNL_DSP_REG_HIPCTDR_MSG_MASK;
		msg_ext = hipctdd & CNL_DSP_REG_HIPCTDD_MSG_MASK;

		dev_vdbg(sdev->dev,
			 "ipc: firmware initiated, msg:0x%x, msg_ext:0x%x\n",
			 msg, msg_ext);

		/* handle messages from DSP */
		if ((hipctdr & SOF_IPC_PANIC_MAGIC_MASK) ==
		   SOF_IPC_PANIC_MAGIC) {
			snd_sof_dsp_panic(sdev, HDA_DSP_PANIC_OFFSET(msg_ext));
		} else {
			snd_sof_ipc_msgs_rx(sdev);
		}

		/*
		 * clear busy interrupt to tell dsp controller this
		 * interrupt has been accepted, not trigger it again
		 */
		snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR,
					       CNL_DSP_REG_HIPCTDR,
					       CNL_DSP_REG_HIPCTDR_BUSY,
					       CNL_DSP_REG_HIPCTDR_BUSY);

		ret = IRQ_HANDLED;
	}

	if (ret == IRQ_HANDLED) {
		/* reenable IPC interrupt */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPIC,
					HDA_DSP_ADSPIC_IPC, HDA_DSP_ADSPIC_IPC);
	}

	if (sdev->code_loading)	{
		sdev->code_loading = 0;
		wake_up(&sdev->waitq);
	}

	return ret;
}

static int cnl_ipc_cmd_done(struct snd_sof_dev *sdev, int dir)
{
	if (dir == SOF_IPC_HOST_REPLY) {
		/*
		 * set done bit to ack dsp the msg has been
		 * processed and send reply msg to dsp
		 */
		snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR,
					       CNL_DSP_REG_HIPCTDA,
					       CNL_DSP_REG_HIPCTDA_DONE,
					       CNL_DSP_REG_HIPCTDA_DONE);
	} else {
		/*
		 * set DONE bit - tell DSP we have received the reply msg
		 * from DSP, and processed it, don't send more reply to host
		 */
		snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR,
					       CNL_DSP_REG_HIPCIDA,
					       CNL_DSP_REG_HIPCIDA_DONE,
					       CNL_DSP_REG_HIPCIDA_DONE);

		/* unmask Done interrupt */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
					CNL_DSP_REG_HIPCCTL,
					CNL_DSP_REG_HIPCCTL_DONE,
					CNL_DSP_REG_HIPCCTL_DONE);
	}

	return 0;
}

static int cnl_ipc_send_msg(struct snd_sof_dev *sdev,
			    struct snd_sof_ipc_msg *msg)
{
	u32 cmd = msg->header;

	/* send the message */
	sof_mailbox_write(sdev, sdev->host_box.offset, msg->msg_data,
			  msg->msg_size);
	snd_sof_dsp_write(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDR,
			  cmd | CNL_DSP_REG_HIPCIDR_BUSY);

	return 0;
}

/* cannonlake ops */
const struct snd_sof_dsp_ops sof_cnl_ops = {
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
	.irq_thread	= cnl_ipc_irq_thread,

	/* mailbox */
	.mailbox_read	= sof_mailbox_read,
	.mailbox_write	= sof_mailbox_write,

	/* ipc */
	.send_msg	= cnl_ipc_send_msg,
	.get_reply	= hda_dsp_ipc_get_reply,
	.fw_ready	= hda_dsp_ipc_fw_ready,
	.cmd_done	= cnl_ipc_cmd_done,

	/* debug */
	.debug_map	= cnl_dsp_debugfs,
	.debug_map_count	= ARRAY_SIZE(cnl_dsp_debugfs),
	.dbg_dump	= hda_dsp_dump,

	/* stream callbacks */
	.pcm_open	= hda_dsp_pcm_open,
	.pcm_close	= hda_dsp_pcm_close,
	.pcm_hw_params	= hda_dsp_pcm_hw_params,
	.pcm_trigger	= hda_dsp_pcm_trigger,

	/* firmware loading */
	.load_firmware = snd_sof_load_firmware_raw,

	/* pre/post fw run */
	.pre_fw_run = hda_dsp_pre_fw_run,
	.post_fw_run = hda_dsp_post_fw_run,

	/* dsp core power up/down */
	.core_power_up = hda_dsp_enable_core,
	.core_power_down = hda_dsp_core_reset_power_down,

	/* firmware run */
	.run = hda_dsp_cl_boot_firmware,

	/* trace callback */
	.trace_init = hda_dsp_trace_init,
	.trace_release = hda_dsp_trace_release,
	.trace_trigger = hda_dsp_trace_trigger,

	/* DAI drivers */
	.drv		= skl_dai,
	.num_drv	= SOF_SKL_NUM_DAIS,

	/* PM */
	.suspend		= hda_dsp_suspend,
	.resume			= hda_dsp_resume,
	.runtime_suspend	= hda_dsp_runtime_suspend,
	.runtime_resume		= hda_dsp_runtime_resume,
};
EXPORT_SYMBOL(sof_cnl_ops);

const struct sof_intel_dsp_desc cnl_chip_info = {
	/* Cannonlake */
	.cores_num = 4,
	.init_core_mask = 1,
	.cores_mask = HDA_DSP_CORE_MASK(0) |
				HDA_DSP_CORE_MASK(1) |
				HDA_DSP_CORE_MASK(2) |
				HDA_DSP_CORE_MASK(3),
	.ipc_req = CNL_DSP_REG_HIPCIDR,
	.ipc_req_mask = CNL_DSP_REG_HIPCIDR_BUSY,
	.ipc_ack = CNL_DSP_REG_HIPCIDA,
	.ipc_ack_mask = CNL_DSP_REG_HIPCIDA_DONE,
	.ipc_ctl = CNL_DSP_REG_HIPCCTL,
	.rom_init_timeout	= 300,
};
EXPORT_SYMBOL(cnl_chip_info);
