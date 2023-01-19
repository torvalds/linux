// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
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

#include <sound/sof/ext_manifest4.h>
#include <sound/sof/ipc4/header.h>
#include <trace/events/sof_intel.h>
#include "../ipc4-priv.h"
#include "../ops.h"
#include "hda.h"
#include "hda-ipc.h"
#include "../sof-audio.h"

static const struct snd_sof_debugfs_map cnl_dsp_debugfs[] = {
	{"hda", HDA_DSP_HDA_BAR, 0, 0x4000, SOF_DEBUGFS_ACCESS_ALWAYS},
	{"pp", HDA_DSP_PP_BAR,  0, 0x1000, SOF_DEBUGFS_ACCESS_ALWAYS},
	{"dsp", HDA_DSP_BAR,  0, 0x10000, SOF_DEBUGFS_ACCESS_ALWAYS},
};

static void cnl_ipc_host_done(struct snd_sof_dev *sdev);
static void cnl_ipc_dsp_done(struct snd_sof_dev *sdev);

irqreturn_t cnl_ipc4_irq_thread(int irq, void *context)
{
	struct sof_ipc4_msg notification_data = {{ 0 }};
	struct snd_sof_dev *sdev = context;
	bool ack_received = false;
	bool ipc_irq = false;
	u32 hipcida, hipctdr;

	hipcida = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDA);
	hipctdr = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCTDR);
	if (hipcida & CNL_DSP_REG_HIPCIDA_DONE) {
		/* DSP received the message */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
					CNL_DSP_REG_HIPCCTL,
					CNL_DSP_REG_HIPCCTL_DONE, 0);
		cnl_ipc_dsp_done(sdev);

		ipc_irq = true;
		ack_received = true;
	}

	if (hipctdr & CNL_DSP_REG_HIPCTDR_BUSY) {
		/* Message from DSP (reply or notification) */
		u32 hipctdd = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
					       CNL_DSP_REG_HIPCTDD);
		u32 primary = hipctdr & CNL_DSP_REG_HIPCTDR_MSG_MASK;
		u32 extension = hipctdd & CNL_DSP_REG_HIPCTDD_MSG_MASK;

		if (primary & SOF_IPC4_MSG_DIR_MASK) {
			/* Reply received */
			if (likely(sdev->fw_state == SOF_FW_BOOT_COMPLETE)) {
				struct sof_ipc4_msg *data = sdev->ipc->msg.reply_data;

				data->primary = primary;
				data->extension = extension;

				spin_lock_irq(&sdev->ipc_lock);

				snd_sof_ipc_get_reply(sdev);
				cnl_ipc_host_done(sdev);
				snd_sof_ipc_reply(sdev, data->primary);

				spin_unlock_irq(&sdev->ipc_lock);
			} else {
				dev_dbg_ratelimited(sdev->dev,
						    "IPC reply before FW_READY: %#x|%#x\n",
						    primary, extension);
			}
		} else {
			/* Notification received */
			notification_data.primary = primary;
			notification_data.extension = extension;

			sdev->ipc->msg.rx_data = &notification_data;
			snd_sof_ipc_msgs_rx(sdev);
			sdev->ipc->msg.rx_data = NULL;

			/* Let DSP know that we have finished processing the message */
			cnl_ipc_host_done(sdev);
		}

		ipc_irq = true;
	}

	if (!ipc_irq)
		/* This interrupt is not shared so no need to return IRQ_NONE. */
		dev_dbg_ratelimited(sdev->dev, "nothing to do in IPC IRQ thread\n");

	if (ack_received) {
		struct sof_intel_hda_dev *hdev = sdev->pdata->hw_pdata;

		if (hdev->delayed_ipc_tx_msg)
			cnl_ipc4_send_msg(sdev, hdev->delayed_ipc_tx_msg);
	}

	return IRQ_HANDLED;
}

irqreturn_t cnl_ipc_irq_thread(int irq, void *context)
{
	struct snd_sof_dev *sdev = context;
	u32 hipci;
	u32 hipcida;
	u32 hipctdr;
	u32 hipctdd;
	u32 msg;
	u32 msg_ext;
	bool ipc_irq = false;

	hipcida = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDA);
	hipctdr = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCTDR);
	hipctdd = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCTDD);
	hipci = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDR);

	/* reply message from DSP */
	if (hipcida & CNL_DSP_REG_HIPCIDA_DONE) {
		msg_ext = hipci & CNL_DSP_REG_HIPCIDR_MSG_MASK;
		msg = hipcida & CNL_DSP_REG_HIPCIDA_MSG_MASK;

		trace_sof_intel_ipc_firmware_response(sdev, msg, msg_ext);

		/* mask Done interrupt */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
					CNL_DSP_REG_HIPCCTL,
					CNL_DSP_REG_HIPCCTL_DONE, 0);

		if (likely(sdev->fw_state == SOF_FW_BOOT_COMPLETE)) {
			spin_lock_irq(&sdev->ipc_lock);

			/* handle immediate reply from DSP core */
			hda_dsp_ipc_get_reply(sdev);
			snd_sof_ipc_reply(sdev, msg);

			cnl_ipc_dsp_done(sdev);

			spin_unlock_irq(&sdev->ipc_lock);
		} else {
			dev_dbg_ratelimited(sdev->dev, "IPC reply before FW_READY: %#x\n",
					    msg);
		}

		ipc_irq = true;
	}

	/* new message from DSP */
	if (hipctdr & CNL_DSP_REG_HIPCTDR_BUSY) {
		msg = hipctdr & CNL_DSP_REG_HIPCTDR_MSG_MASK;
		msg_ext = hipctdd & CNL_DSP_REG_HIPCTDD_MSG_MASK;

		trace_sof_intel_ipc_firmware_initiated(sdev, msg, msg_ext);

		/* handle messages from DSP */
		if ((hipctdr & SOF_IPC_PANIC_MAGIC_MASK) == SOF_IPC_PANIC_MAGIC) {
			struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
			bool non_recoverable = true;

			/*
			 * This is a PANIC message!
			 *
			 * If it is arriving during firmware boot and it is not
			 * the last boot attempt then change the non_recoverable
			 * to false as the DSP might be able to boot in the next
			 * iteration(s)
			 */
			if (sdev->fw_state == SOF_FW_BOOT_IN_PROGRESS &&
			    hda->boot_iteration < HDA_FW_BOOT_ATTEMPTS)
				non_recoverable = false;

			snd_sof_dsp_panic(sdev, HDA_DSP_PANIC_OFFSET(msg_ext),
					  non_recoverable);
		} else {
			snd_sof_ipc_msgs_rx(sdev);
		}

		cnl_ipc_host_done(sdev);

		ipc_irq = true;
	}

	if (!ipc_irq) {
		/*
		 * This interrupt is not shared so no need to return IRQ_NONE.
		 */
		dev_dbg_ratelimited(sdev->dev,
				    "nothing to do in IPC IRQ thread\n");
	}

	return IRQ_HANDLED;
}

static void cnl_ipc_host_done(struct snd_sof_dev *sdev)
{
	/*
	 * clear busy interrupt to tell dsp controller this
	 * interrupt has been accepted, not trigger it again
	 */
	snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR,
				       CNL_DSP_REG_HIPCTDR,
				       CNL_DSP_REG_HIPCTDR_BUSY,
				       CNL_DSP_REG_HIPCTDR_BUSY);
	/*
	 * set done bit to ack dsp the msg has been
	 * processed and send reply msg to dsp
	 */
	snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR,
				       CNL_DSP_REG_HIPCTDA,
				       CNL_DSP_REG_HIPCTDA_DONE,
				       CNL_DSP_REG_HIPCTDA_DONE);
}

static void cnl_ipc_dsp_done(struct snd_sof_dev *sdev)
{
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

static bool cnl_compact_ipc_compress(struct snd_sof_ipc_msg *msg,
				     u32 *dr, u32 *dd)
{
	struct sof_ipc_pm_gate *pm_gate = msg->msg_data;

	if (pm_gate->hdr.cmd == (SOF_IPC_GLB_PM_MSG | SOF_IPC_PM_GATE)) {
		/* send the compact message via the primary register */
		*dr = HDA_IPC_MSG_COMPACT | HDA_IPC_PM_GATE;

		/* send payload via the extended data register */
		*dd = pm_gate->flags;

		return true;
	}

	return false;
}

int cnl_ipc4_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	struct sof_intel_hda_dev *hdev = sdev->pdata->hw_pdata;
	struct sof_ipc4_msg *msg_data = msg->msg_data;

	if (hda_ipc4_tx_is_busy(sdev)) {
		hdev->delayed_ipc_tx_msg = msg;
		return 0;
	}

	hdev->delayed_ipc_tx_msg = NULL;

	/* send the message via mailbox */
	if (msg_data->data_size)
		sof_mailbox_write(sdev, sdev->host_box.offset, msg_data->data_ptr,
				  msg_data->data_size);

	snd_sof_dsp_write(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDD, msg_data->extension);
	snd_sof_dsp_write(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDR,
			  msg_data->primary | CNL_DSP_REG_HIPCIDR_BUSY);

	return 0;
}

int cnl_ipc_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	struct sof_intel_hda_dev *hdev = sdev->pdata->hw_pdata;
	struct sof_ipc_cmd_hdr *hdr;
	u32 dr = 0;
	u32 dd = 0;

	/*
	 * Currently the only compact IPC supported is the PM_GATE
	 * IPC which is used for transitioning the DSP between the
	 * D0I0 and D0I3 states. And these are sent only during the
	 * set_power_state() op. Therefore, there will never be a case
	 * that a compact IPC results in the DSP exiting D0I3 without
	 * the host and FW being in sync.
	 */
	if (cnl_compact_ipc_compress(msg, &dr, &dd)) {
		/* send the message via IPC registers */
		snd_sof_dsp_write(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDD,
				  dd);
		snd_sof_dsp_write(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDR,
				  CNL_DSP_REG_HIPCIDR_BUSY | dr);
		return 0;
	}

	/* send the message via mailbox */
	sof_mailbox_write(sdev, sdev->host_box.offset, msg->msg_data,
			  msg->msg_size);
	snd_sof_dsp_write(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDR,
			  CNL_DSP_REG_HIPCIDR_BUSY);

	hdr = msg->msg_data;

	/*
	 * Use mod_delayed_work() to schedule the delayed work
	 * to avoid scheduling multiple workqueue items when
	 * IPCs are sent at a high-rate. mod_delayed_work()
	 * modifies the timer if the work is pending.
	 * Also, a new delayed work should not be queued after the
	 * CTX_SAVE IPC, which is sent before the DSP enters D3.
	 */
	if (hdr->cmd != (SOF_IPC_GLB_PM_MSG | SOF_IPC_PM_CTX_SAVE))
		mod_delayed_work(system_wq, &hdev->d0i3_work,
				 msecs_to_jiffies(SOF_HDA_D0I3_WORK_DELAY_MS));

	return 0;
}

void cnl_ipc_dump(struct snd_sof_dev *sdev)
{
	u32 hipcctl;
	u32 hipcida;
	u32 hipctdr;

	hda_ipc_irq_dump(sdev);

	/* read IPC status */
	hipcida = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDA);
	hipcctl = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCCTL);
	hipctdr = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCTDR);

	/* dump the IPC regs */
	/* TODO: parse the raw msg */
	dev_err(sdev->dev,
		"error: host status 0x%8.8x dsp status 0x%8.8x mask 0x%8.8x\n",
		hipcida, hipctdr, hipcctl);
}

void cnl_ipc4_dump(struct snd_sof_dev *sdev)
{
	u32 hipcidr, hipcidd, hipcida, hipctdr, hipctdd, hipctda, hipcctl;

	hda_ipc_irq_dump(sdev);

	hipcidr = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDR);
	hipcidd = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDD);
	hipcida = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDA);
	hipctdr = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCTDR);
	hipctdd = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCTDD);
	hipctda = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCTDA);
	hipcctl = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCCTL);

	/* dump the IPC regs */
	/* TODO: parse the raw msg */
	dev_err(sdev->dev,
		"Host IPC initiator: %#x|%#x|%#x, target: %#x|%#x|%#x, ctl: %#x\n",
		hipcidr, hipcidd, hipcida, hipctdr, hipctdd, hipctda, hipcctl);
}

/* cannonlake ops */
struct snd_sof_dsp_ops sof_cnl_ops;
EXPORT_SYMBOL_NS(sof_cnl_ops, SND_SOC_SOF_INTEL_HDA_COMMON);

int sof_cnl_ops_init(struct snd_sof_dev *sdev)
{
	/* common defaults */
	memcpy(&sof_cnl_ops, &sof_hda_common_ops, sizeof(struct snd_sof_dsp_ops));

	/* probe/remove/shutdown */
	sof_cnl_ops.shutdown	= hda_dsp_shutdown;

	/* ipc */
	if (sdev->pdata->ipc_type == SOF_IPC) {
		/* doorbell */
		sof_cnl_ops.irq_thread	= cnl_ipc_irq_thread;

		/* ipc */
		sof_cnl_ops.send_msg	= cnl_ipc_send_msg;

		/* debug */
		sof_cnl_ops.ipc_dump	= cnl_ipc_dump;
	}

	if (sdev->pdata->ipc_type == SOF_INTEL_IPC4) {
		struct sof_ipc4_fw_data *ipc4_data;

		sdev->private = devm_kzalloc(sdev->dev, sizeof(*ipc4_data), GFP_KERNEL);
		if (!sdev->private)
			return -ENOMEM;

		ipc4_data = sdev->private;
		ipc4_data->manifest_fw_hdr_offset = SOF_MAN4_FW_HDR_OFFSET;

		ipc4_data->mtrace_type = SOF_IPC4_MTRACE_INTEL_CAVS_1_8;

		/* External library loading support */
		ipc4_data->load_library = hda_dsp_ipc4_load_library;

		/* doorbell */
		sof_cnl_ops.irq_thread	= cnl_ipc4_irq_thread;

		/* ipc */
		sof_cnl_ops.send_msg	= cnl_ipc4_send_msg;

		/* debug */
		sof_cnl_ops.ipc_dump	= cnl_ipc4_dump;
	}

	/* set DAI driver ops */
	hda_set_dai_drv_ops(sdev, &sof_cnl_ops);

	/* debug */
	sof_cnl_ops.debug_map	= cnl_dsp_debugfs;
	sof_cnl_ops.debug_map_count	= ARRAY_SIZE(cnl_dsp_debugfs);

	/* pre/post fw run */
	sof_cnl_ops.post_fw_run = hda_dsp_post_fw_run;

	/* firmware run */
	sof_cnl_ops.run = hda_dsp_cl_boot_firmware;

	/* dsp core get/put */
	sof_cnl_ops.core_get = hda_dsp_core_get;

	return 0;
};
EXPORT_SYMBOL_NS(sof_cnl_ops_init, SND_SOC_SOF_INTEL_HDA_COMMON);

const struct sof_intel_dsp_desc cnl_chip_info = {
	/* Cannonlake */
	.cores_num = 4,
	.init_core_mask = 1,
	.host_managed_cores_mask = GENMASK(3, 0),
	.ipc_req = CNL_DSP_REG_HIPCIDR,
	.ipc_req_mask = CNL_DSP_REG_HIPCIDR_BUSY,
	.ipc_ack = CNL_DSP_REG_HIPCIDA,
	.ipc_ack_mask = CNL_DSP_REG_HIPCIDA_DONE,
	.ipc_ctl = CNL_DSP_REG_HIPCCTL,
	.rom_status_reg = HDA_DSP_SRAM_REG_ROM_STATUS,
	.rom_init_timeout	= 300,
	.ssp_count = CNL_SSP_COUNT,
	.ssp_base_offset = CNL_SSP_BASE_OFFSET,
	.sdw_shim_base = SDW_SHIM_BASE,
	.sdw_alh_base = SDW_ALH_BASE,
	.d0i3_offset = SOF_HDA_VS_D0I3C,
	.read_sdw_lcount =  hda_sdw_check_lcount_common,
	.enable_sdw_irq	= hda_common_enable_sdw_irq,
	.check_sdw_irq	= hda_common_check_sdw_irq,
	.check_ipc_irq	= hda_dsp_check_ipc_irq,
	.cl_init = cl_dsp_init,
	.power_down_dsp = hda_power_down_dsp,
	.disable_interrupts = hda_dsp_disable_interrupts,
	.hw_ip_version = SOF_INTEL_CAVS_1_8,
};
EXPORT_SYMBOL_NS(cnl_chip_info, SND_SOC_SOF_INTEL_HDA_COMMON);

/*
 * JasperLake is technically derived from IceLake, and should be in
 * described in icl.c. However since JasperLake was designed with
 * two cores, it cannot support the IceLake-specific power-up sequences
 * which rely on core3. To simplify, JasperLake uses the CannonLake ops and
 * is described in cnl.c
 */
const struct sof_intel_dsp_desc jsl_chip_info = {
	/* Jasperlake */
	.cores_num = 2,
	.init_core_mask = 1,
	.host_managed_cores_mask = GENMASK(1, 0),
	.ipc_req = CNL_DSP_REG_HIPCIDR,
	.ipc_req_mask = CNL_DSP_REG_HIPCIDR_BUSY,
	.ipc_ack = CNL_DSP_REG_HIPCIDA,
	.ipc_ack_mask = CNL_DSP_REG_HIPCIDA_DONE,
	.ipc_ctl = CNL_DSP_REG_HIPCCTL,
	.rom_status_reg = HDA_DSP_SRAM_REG_ROM_STATUS,
	.rom_init_timeout	= 300,
	.ssp_count = ICL_SSP_COUNT,
	.ssp_base_offset = CNL_SSP_BASE_OFFSET,
	.sdw_shim_base = SDW_SHIM_BASE,
	.sdw_alh_base = SDW_ALH_BASE,
	.d0i3_offset = SOF_HDA_VS_D0I3C,
	.read_sdw_lcount =  hda_sdw_check_lcount_common,
	.enable_sdw_irq	= hda_common_enable_sdw_irq,
	.check_sdw_irq	= hda_common_check_sdw_irq,
	.check_ipc_irq	= hda_dsp_check_ipc_irq,
	.cl_init = cl_dsp_init,
	.power_down_dsp = hda_power_down_dsp,
	.disable_interrupts = hda_dsp_disable_interrupts,
	.hw_ip_version = SOF_INTEL_CAVS_2_0,
};
EXPORT_SYMBOL_NS(jsl_chip_info, SND_SOC_SOF_INTEL_HDA_COMMON);
