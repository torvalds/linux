// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * Authors: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *	    Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
 *	    Jeeja KP <jeeja.kp@intel.com>
 *	    Rander Wang <rander.wang@intel.com>
 *          Keyon Jie <yang.jie@linux.intel.com>
 */

/*
 * Hardware interface for audio DSP on Cannonlake.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/pci.h>
#include <sound/hdaudio_ext.h>
#include <sound/sof.h>
#include <sound/pcm_params.h>
#include <linux/pm_runtime.h>

#include "../sof-priv.h"
#include "../ops.h"
#include "hda.h"

static const struct snd_sof_debugfs_map cnl_dsp_debugfs[] = {
	{"hda", HDA_DSP_HDA_BAR, 0, 0x4000},
	{"pp", HDA_DSP_PP_BAR,  0, 0x1000},
	{"dsp", HDA_DSP_BAR,  0, 0x10000},
};

static int cnl_ipc_cmd_done(struct snd_sof_dev *sdev, int dir);

static irqreturn_t cnl_ipc_irq_thread(int irq, void *context)
{
	struct snd_sof_dev *sdev = (struct snd_sof_dev *)context;
	u32 hipci, hipcida, hipctdr, hipctdd, msg = 0, msg_ext = 0;
	irqreturn_t ret = IRQ_NONE;

	/* here we handle IPC interrupts only */
	if (!(sdev->irq_status & HDA_DSP_ADSPIS_IPC))
		return ret;

	hipcida = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDA);

	/* reply message from DSP */
	if (hipcida & CNL_DSP_REG_HIPCIDA_DONE) {
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

static int cnl_ipc_is_ready(struct snd_sof_dev *sdev)
{
	u64 busy, done;

	busy = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDR);
	done = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDA);
	if ((busy & CNL_DSP_REG_HIPCIDR_BUSY) ||
	    (done & CNL_DSP_REG_HIPCIDA_DONE))
		return 0;

	return 1;
}

static int cnl_ipc_send_msg(struct snd_sof_dev *sdev,
			    struct snd_sof_ipc_msg *msg)
{
	u32 cmd = msg->header;

	/* send the message */
	hda_dsp_mailbox_write(sdev, sdev->host_box.offset, msg->msg_data,
			      msg->msg_size);
	snd_sof_dsp_write(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDR,
			  cmd | CNL_DSP_REG_HIPCIDR_BUSY);

	return 0;
}

/* cannonlake ops */
struct snd_sof_dsp_ops sof_cnl_ops = {
	/* probe and remove */
	.probe		= hda_dsp_probe,
	.remove		= hda_dsp_remove,

	/* Register IO */
	.write		= hda_dsp_write,
	.read		= hda_dsp_read,
	.write64	= hda_dsp_write64,
	.read64		= hda_dsp_read64,

	/* Block IO */
	.block_read	= hda_dsp_block_read,
	.block_write	= hda_dsp_block_write,

	/* doorbell */
	.irq_handler	= hda_dsp_ipc_irq_handler,
	.irq_thread	= cnl_ipc_irq_thread,

	/* mailbox */
	.mailbox_read	= hda_dsp_mailbox_read,
	.mailbox_write	= hda_dsp_mailbox_write,

	/* ipc */
	.send_msg	= cnl_ipc_send_msg,
	.get_reply	= hda_dsp_ipc_get_reply,
	.fw_ready	= hda_dsp_ipc_fw_ready,
	.is_ready	= cnl_ipc_is_ready,
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
	.load_firmware = hda_dsp_cl_load_fw,

	/* firmware run */
	.run = hda_dsp_cl_boot_firmware,

	/* trace callback */
	.trace_init = hda_dsp_trace_init,
	.trace_release = hda_dsp_trace_release,
	.trace_trigger = hda_dsp_trace_trigger,

	/* DAI drivers */
	.drv		= skl_dai,
	.num_drv	= SOF_SKL_NUM_DAIS,
};
EXPORT_SYMBOL(sof_cnl_ops);
