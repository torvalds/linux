// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.
//
// Authors: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//

/*
 * Hardware interface for audio DSP on Meteorlake.
 */

#include <linux/firmware.h>
#include <sound/sof/ipc4/header.h>
#include <trace/events/sof_intel.h>
#include "../ipc4-priv.h"
#include "../ops.h"
#include "hda.h"
#include "hda-ipc.h"
#include "../sof-audio.h"
#include "mtl.h"

static const struct snd_sof_debugfs_map mtl_dsp_debugfs[] = {
	{"hda", HDA_DSP_HDA_BAR, 0, 0x4000, SOF_DEBUGFS_ACCESS_ALWAYS},
	{"pp", HDA_DSP_PP_BAR,  0, 0x1000, SOF_DEBUGFS_ACCESS_ALWAYS},
	{"dsp", HDA_DSP_BAR,  0, 0x10000, SOF_DEBUGFS_ACCESS_ALWAYS},
};

static void mtl_ipc_host_done(struct snd_sof_dev *sdev)
{
	/*
	 * clear busy interrupt to tell dsp controller this interrupt has been accepted,
	 * not trigger it again
	 */
	snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXTDR,
				       MTL_DSP_REG_HFIPCXTDR_BUSY, MTL_DSP_REG_HFIPCXTDR_BUSY);
	/*
	 * clear busy bit to ack dsp the msg has been processed and send reply msg to dsp
	 */
	snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXTDA,
				       MTL_DSP_REG_HFIPCXTDA_BUSY, 0);
}

static void mtl_ipc_dsp_done(struct snd_sof_dev *sdev)
{
	/*
	 * set DONE bit - tell DSP we have received the reply msg from DSP, and processed it,
	 * don't send more reply to host
	 */
	snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXIDA,
				       MTL_DSP_REG_HFIPCXIDA_DONE, MTL_DSP_REG_HFIPCXIDA_DONE);

	/* unmask Done interrupt */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXCTL,
				MTL_DSP_REG_HFIPCXCTL_DONE, MTL_DSP_REG_HFIPCXCTL_DONE);
}

/* Check if an IPC IRQ occurred */
static bool mtl_dsp_check_ipc_irq(struct snd_sof_dev *sdev)
{
	u32 irq_status;
	u32 hfintipptr;

	/* read Interrupt IP Pointer */
	hfintipptr = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_HFINTIPPTR) & MTL_HFINTIPPTR_PTR_MASK;
	irq_status = snd_sof_dsp_read(sdev, HDA_DSP_BAR, hfintipptr + MTL_DSP_IRQSTS);

	trace_sof_intel_hda_irq_ipc_check(sdev, irq_status);

	if (irq_status != U32_MAX && (irq_status & MTL_DSP_IRQSTS_IPC))
		return true;

	return false;
}

/* Check if an SDW IRQ occurred */
static bool mtl_dsp_check_sdw_irq(struct snd_sof_dev *sdev)
{
	u32 irq_status;
	u32 hfintipptr;

	/* read Interrupt IP Pointer */
	hfintipptr = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_HFINTIPPTR) & MTL_HFINTIPPTR_PTR_MASK;
	irq_status = snd_sof_dsp_read(sdev, HDA_DSP_BAR, hfintipptr + MTL_DSP_IRQSTS);

	if (irq_status != U32_MAX && (irq_status & MTL_DSP_IRQSTS_SDW))
		return true;

	return false;
}

static int mtl_ipc_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
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

	snd_sof_dsp_write(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXIDDY,
			  msg_data->extension);
	snd_sof_dsp_write(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXIDR,
			  msg_data->primary | MTL_DSP_REG_HFIPCXIDR_BUSY);

	hda_dsp_ipc4_schedule_d0i3_work(hdev, msg);

	return 0;
}

static void mtl_enable_ipc_interrupts(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;

	/* enable IPC DONE and BUSY interrupts */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, chip->ipc_ctl,
				MTL_DSP_REG_HFIPCXCTL_BUSY | MTL_DSP_REG_HFIPCXCTL_DONE,
				MTL_DSP_REG_HFIPCXCTL_BUSY | MTL_DSP_REG_HFIPCXCTL_DONE);
}

static void mtl_disable_ipc_interrupts(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;

	/* disable IPC DONE and BUSY interrupts */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, chip->ipc_ctl,
				MTL_DSP_REG_HFIPCXCTL_BUSY | MTL_DSP_REG_HFIPCXCTL_DONE, 0);
}

static void mtl_enable_sdw_irq(struct snd_sof_dev *sdev, bool enable)
{
	u32 hipcie;
	u32 mask;
	u32 val;
	int ret;

	/* Enable/Disable SoundWire interrupt */
	mask = MTL_DSP_REG_HfSNDWIE_IE_MASK;
	if (enable)
		val = mask;
	else
		val = 0;

	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, MTL_DSP_REG_HfSNDWIE, mask, val);

	/* check if operation was successful */
	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR, MTL_DSP_REG_HfSNDWIE, hipcie,
					    (hipcie & mask) == val,
					    HDA_DSP_REG_POLL_INTERVAL_US, HDA_DSP_RESET_TIMEOUT_US);
	if (ret < 0)
		dev_err(sdev->dev, "failed to set SoundWire IPC interrupt %s\n",
			enable ? "enable" : "disable");
}

static int mtl_enable_interrupts(struct snd_sof_dev *sdev, bool enable)
{
	u32 hfintipptr;
	u32 irqinten;
	u32 hipcie;
	u32 mask;
	u32 val;
	int ret;

	/* read Interrupt IP Pointer */
	hfintipptr = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_HFINTIPPTR) & MTL_HFINTIPPTR_PTR_MASK;

	/* Enable/Disable Host IPC and SOUNDWIRE */
	mask = MTL_IRQ_INTEN_L_HOST_IPC_MASK | MTL_IRQ_INTEN_L_SOUNDWIRE_MASK;
	if (enable)
		val = mask;
	else
		val = 0;

	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, hfintipptr, mask, val);

	/* check if operation was successful */
	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR, hfintipptr, irqinten,
					    (irqinten & mask) == val,
					    HDA_DSP_REG_POLL_INTERVAL_US, HDA_DSP_RESET_TIMEOUT_US);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to %s Host IPC and/or SOUNDWIRE\n",
			enable ? "enable" : "disable");
		return ret;
	}

	/* Enable/Disable Host IPC interrupt*/
	mask = MTL_DSP_REG_HfHIPCIE_IE_MASK;
	if (enable)
		val = mask;
	else
		val = 0;

	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, MTL_DSP_REG_HfHIPCIE, mask, val);

	/* check if operation was successful */
	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR, MTL_DSP_REG_HfHIPCIE, hipcie,
					    (hipcie & mask) == val,
					    HDA_DSP_REG_POLL_INTERVAL_US, HDA_DSP_RESET_TIMEOUT_US);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to set Host IPC interrupt %s\n",
			enable ? "enable" : "disable");
		return ret;
	}

	return ret;
}

/* pre fw run operations */
static int mtl_dsp_pre_fw_run(struct snd_sof_dev *sdev)
{
	u32 dsphfpwrsts;
	u32 dsphfdsscs;
	u32 cpa;
	u32 pgs;
	int ret;

	/* Set the DSP subsystem power on */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, MTL_HFDSSCS,
				MTL_HFDSSCS_SPA_MASK, MTL_HFDSSCS_SPA_MASK);

	/* Wait for unstable CPA read (1 then 0 then 1) just after setting SPA bit */
	usleep_range(1000, 1010);

	/* poll with timeout to check if operation successful */
	cpa = MTL_HFDSSCS_CPA_MASK;
	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR, MTL_HFDSSCS, dsphfdsscs,
					    (dsphfdsscs & cpa) == cpa, HDA_DSP_REG_POLL_INTERVAL_US,
					    HDA_DSP_RESET_TIMEOUT_US);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to enable DSP subsystem\n");
		return ret;
	}

	/* Power up gated-DSP-0 domain in order to access the DSP shim register block. */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, MTL_HFPWRCTL,
				MTL_HFPWRCTL_WPDSPHPXPG, MTL_HFPWRCTL_WPDSPHPXPG);

	usleep_range(1000, 1010);

	/* poll with timeout to check if operation successful */
	pgs = MTL_HFPWRSTS_DSPHPXPGS_MASK;
	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR, MTL_HFPWRSTS, dsphfpwrsts,
					    (dsphfpwrsts & pgs) == pgs,
					    HDA_DSP_REG_POLL_INTERVAL_US,
					    HDA_DSP_RESET_TIMEOUT_US);
	if (ret < 0)
		dev_err(sdev->dev, "failed to power up gated DSP domain\n");

	/* make sure SoundWire is not power-gated */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, MTL_HFPWRCTL,
				MTL_HfPWRCTL_WPIOXPG(1), MTL_HfPWRCTL_WPIOXPG(1));
	return ret;
}

static int mtl_dsp_post_fw_run(struct snd_sof_dev *sdev)
{
	int ret;

	if (sdev->first_boot) {
		struct sof_intel_hda_dev *hdev = sdev->pdata->hw_pdata;

		ret = hda_sdw_startup(sdev);
		if (ret < 0) {
			dev_err(sdev->dev, "could not startup SoundWire links\n");
			return ret;
		}

		/* Check if IMR boot is usable */
		if (!sof_debug_check_flag(SOF_DBG_IGNORE_D3_PERSISTENT))
			hdev->imrboot_supported = true;
	}

	hda_sdw_int_enable(sdev, true);
	return 0;
}

static void mtl_dsp_dump(struct snd_sof_dev *sdev, u32 flags)
{
	char *level = (flags & SOF_DBG_DUMP_OPTIONAL) ? KERN_DEBUG : KERN_ERR;
	u32 romdbgsts;
	u32 romdbgerr;
	u32 fwsts;
	u32 fwlec;

	fwsts = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP_ROM_STS);
	fwlec = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP_ROM_ERROR);
	romdbgsts = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFFLGPXQWY);
	romdbgerr = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFFLGPXQWY_ERROR);

	dev_err(sdev->dev, "ROM status: %#x, ROM error: %#x\n", fwsts, fwlec);
	dev_err(sdev->dev, "ROM debug status: %#x, ROM debug error: %#x\n", romdbgsts,
		romdbgerr);
	romdbgsts = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFFLGPXQWY + 0x8 * 3);
	dev_printk(level, sdev->dev, "ROM feature bit%s enabled\n",
		   romdbgsts & BIT(24) ? "" : " not");
}

static bool mtl_dsp_primary_core_is_enabled(struct snd_sof_dev *sdev)
{
	int val;

	val = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP2CXCTL_PRIMARY_CORE);
	if (val != U32_MAX && val & MTL_DSP2CXCTL_PRIMARY_CORE_CPA_MASK)
		return true;

	return false;
}

static int mtl_dsp_core_power_up(struct snd_sof_dev *sdev, int core)
{
	unsigned int cpa;
	u32 dspcxctl;
	int ret;

	/* Only the primary core can be powered up by the host */
	if (core != SOF_DSP_PRIMARY_CORE || mtl_dsp_primary_core_is_enabled(sdev))
		return 0;

	/* Program the owner of the IP & shim registers (10: Host CPU) */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, MTL_DSP2CXCTL_PRIMARY_CORE,
				MTL_DSP2CXCTL_PRIMARY_CORE_OSEL,
				0x2 << MTL_DSP2CXCTL_PRIMARY_CORE_OSEL_SHIFT);

	/* enable SPA bit */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, MTL_DSP2CXCTL_PRIMARY_CORE,
				MTL_DSP2CXCTL_PRIMARY_CORE_SPA_MASK,
				MTL_DSP2CXCTL_PRIMARY_CORE_SPA_MASK);

	/* Wait for unstable CPA read (1 then 0 then 1) just after setting SPA bit */
	usleep_range(1000, 1010);

	/* poll with timeout to check if operation successful */
	cpa = MTL_DSP2CXCTL_PRIMARY_CORE_CPA_MASK;
	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR, MTL_DSP2CXCTL_PRIMARY_CORE, dspcxctl,
					    (dspcxctl & cpa) == cpa, HDA_DSP_REG_POLL_INTERVAL_US,
					    HDA_DSP_RESET_TIMEOUT_US);
	if (ret < 0)
		dev_err(sdev->dev, "%s: timeout on MTL_DSP2CXCTL_PRIMARY_CORE read\n",
			__func__);

	return ret;
}

static int mtl_dsp_core_power_down(struct snd_sof_dev *sdev, int core)
{
	u32 dspcxctl;
	int ret;

	/* Only the primary core can be powered down by the host */
	if (core != SOF_DSP_PRIMARY_CORE || !mtl_dsp_primary_core_is_enabled(sdev))
		return 0;

	/* disable SPA bit */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, MTL_DSP2CXCTL_PRIMARY_CORE,
				MTL_DSP2CXCTL_PRIMARY_CORE_SPA_MASK, 0);

	/* Wait for unstable CPA read (0 then 1 then 0) just after setting SPA bit */
	usleep_range(1000, 1010);

	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR, MTL_DSP2CXCTL_PRIMARY_CORE, dspcxctl,
					    !(dspcxctl & MTL_DSP2CXCTL_PRIMARY_CORE_CPA_MASK),
					    HDA_DSP_REG_POLL_INTERVAL_US,
					    HDA_DSP_PD_TIMEOUT * USEC_PER_MSEC);
	if (ret < 0)
		dev_err(sdev->dev, "failed to power down primary core\n");

	return ret;
}

static int mtl_power_down_dsp(struct snd_sof_dev *sdev)
{
	u32 dsphfdsscs, cpa;
	int ret;

	/* first power down core */
	ret = mtl_dsp_core_power_down(sdev, SOF_DSP_PRIMARY_CORE);
	if (ret) {
		dev_err(sdev->dev, "mtl dsp power down error, %d\n", ret);
		return ret;
	}

	/* Set the DSP subsystem power down */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, MTL_HFDSSCS,
				MTL_HFDSSCS_SPA_MASK, 0);

	/* Wait for unstable CPA read (0 then 1 then 0) just after setting SPA bit */
	usleep_range(1000, 1010);

	/* poll with timeout to check if operation successful */
	cpa = MTL_HFDSSCS_CPA_MASK;
	dsphfdsscs = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_HFDSSCS);
	return snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR, MTL_HFDSSCS, dsphfdsscs,
					     (dsphfdsscs & cpa) == 0, HDA_DSP_REG_POLL_INTERVAL_US,
					     HDA_DSP_RESET_TIMEOUT_US);
}

static int mtl_dsp_cl_init(struct snd_sof_dev *sdev, int stream_tag, bool imr_boot)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;
	unsigned int status;
	u32 ipc_hdr;
	int ret;

	/* step 1: purge FW request */
	ipc_hdr = chip->ipc_req_mask | HDA_DSP_ROM_IPC_CONTROL;
	if (!imr_boot)
		ipc_hdr |= HDA_DSP_ROM_IPC_PURGE_FW | ((stream_tag - 1) << 9);

	snd_sof_dsp_write(sdev, HDA_DSP_BAR, chip->ipc_req, ipc_hdr);

	/* step 2: power up primary core */
	ret = mtl_dsp_core_power_up(sdev, SOF_DSP_PRIMARY_CORE);
	if (ret < 0) {
		if (hda->boot_iteration == HDA_FW_BOOT_ATTEMPTS)
			dev_err(sdev->dev, "dsp core 0/1 power up failed\n");
		goto err;
	}

	dev_dbg(sdev->dev, "Primary core power up successful\n");

	/* step 3: wait for IPC DONE bit from ROM */
	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR, chip->ipc_ack, status,
					    ((status & chip->ipc_ack_mask) == chip->ipc_ack_mask),
					    HDA_DSP_REG_POLL_INTERVAL_US, MTL_DSP_PURGE_TIMEOUT_US);
	if (ret < 0) {
		if (hda->boot_iteration == HDA_FW_BOOT_ATTEMPTS)
			dev_err(sdev->dev, "timeout waiting for purge IPC done\n");
		goto err;
	}

	/* set DONE bit to clear the reply IPC message */
	snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR, chip->ipc_ack, chip->ipc_ack_mask,
				       chip->ipc_ack_mask);

	/* step 4: enable interrupts */
	ret = mtl_enable_interrupts(sdev, true);
	if (ret < 0) {
		if (hda->boot_iteration == HDA_FW_BOOT_ATTEMPTS)
			dev_err(sdev->dev, "%s: failed to enable interrupts\n", __func__);
		goto err;
	}

	mtl_enable_ipc_interrupts(sdev);

	/*
	 * ACE workaround: don't wait for ROM INIT.
	 * The platform cannot catch ROM_INIT_DONE because of a very short
	 * timing window. Follow the recommendations and skip this part.
	 */

	return 0;

err:
	snd_sof_dsp_dbg_dump(sdev, "MTL DSP init fail", 0);
	mtl_dsp_core_power_down(sdev, SOF_DSP_PRIMARY_CORE);
	return ret;
}

static irqreturn_t mtl_ipc_irq_thread(int irq, void *context)
{
	struct sof_ipc4_msg notification_data = {{ 0 }};
	struct snd_sof_dev *sdev = context;
	bool ack_received = false;
	bool ipc_irq = false;
	u32 hipcida;
	u32 hipctdr;

	hipcida = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXIDA);
	hipctdr = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXTDR);

	/* reply message from DSP */
	if (hipcida & MTL_DSP_REG_HFIPCXIDA_DONE) {
		/* DSP received the message */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXCTL,
					MTL_DSP_REG_HFIPCXCTL_DONE, 0);

		mtl_ipc_dsp_done(sdev);

		ipc_irq = true;
		ack_received = true;
	}

	if (hipctdr & MTL_DSP_REG_HFIPCXTDR_BUSY) {
		/* Message from DSP (reply or notification) */
		u32 extension = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXTDDY);
		u32 primary = hipctdr & MTL_DSP_REG_HFIPCXTDR_MSG_MASK;

		/*
		 * ACE fw sends a new fw ipc message to host to
		 * notify the status of the last host ipc message
		 */
		if (primary & SOF_IPC4_MSG_DIR_MASK) {
			/* Reply received */
			if (likely(sdev->fw_state == SOF_FW_BOOT_COMPLETE)) {
				struct sof_ipc4_msg *data = sdev->ipc->msg.reply_data;

				data->primary = primary;
				data->extension = extension;

				spin_lock_irq(&sdev->ipc_lock);

				snd_sof_ipc_get_reply(sdev);
				mtl_ipc_host_done(sdev);
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

			mtl_ipc_host_done(sdev);
		}

		ipc_irq = true;
	}

	if (!ipc_irq) {
		/* This interrupt is not shared so no need to return IRQ_NONE. */
		dev_dbg_ratelimited(sdev->dev, "nothing to do in IPC IRQ thread\n");
	}

	if (ack_received) {
		struct sof_intel_hda_dev *hdev = sdev->pdata->hw_pdata;

		if (hdev->delayed_ipc_tx_msg)
			mtl_ipc_send_msg(sdev, hdev->delayed_ipc_tx_msg);
	}

	return IRQ_HANDLED;
}

static int mtl_dsp_ipc_get_mailbox_offset(struct snd_sof_dev *sdev)
{
	return MTL_DSP_MBOX_UPLINK_OFFSET;
}

static int mtl_dsp_ipc_get_window_offset(struct snd_sof_dev *sdev, u32 id)
{
	return MTL_SRAM_WINDOW_OFFSET(id);
}

static void mtl_ipc_dump(struct snd_sof_dev *sdev)
{
	u32 hipcidr, hipcidd, hipcida, hipctdr, hipctdd, hipctda, hipcctl;

	hipcidr = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXIDR);
	hipcidd = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXIDDY);
	hipcida = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXIDA);
	hipctdr = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXTDR);
	hipctdd = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXTDDY);
	hipctda = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXTDA);
	hipcctl = snd_sof_dsp_read(sdev, HDA_DSP_BAR, MTL_DSP_REG_HFIPCXCTL);

	dev_err(sdev->dev,
		"Host IPC initiator: %#x|%#x|%#x, target: %#x|%#x|%#x, ctl: %#x\n",
		hipcidr, hipcidd, hipcida, hipctdr, hipctdd, hipctda, hipcctl);
}

static int mtl_dsp_disable_interrupts(struct snd_sof_dev *sdev)
{
	mtl_enable_sdw_irq(sdev, false);
	mtl_disable_ipc_interrupts(sdev);
	return mtl_enable_interrupts(sdev, false);
}

static u64 mtl_dsp_get_stream_hda_link_position(struct snd_sof_dev *sdev,
						struct snd_soc_component *component,
						struct snd_pcm_substream *substream)
{
	struct hdac_stream *hstream = substream->runtime->private_data;
	u32 llp_l, llp_u;

	llp_l = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, MTL_PPLCLLPL(hstream->index));
	llp_u = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, MTL_PPLCLLPU(hstream->index));
	return ((u64)llp_u << 32) | llp_l;
}

/* Meteorlake ops */
struct snd_sof_dsp_ops sof_mtl_ops;
EXPORT_SYMBOL_NS(sof_mtl_ops, SND_SOC_SOF_INTEL_HDA_COMMON);

int sof_mtl_ops_init(struct snd_sof_dev *sdev)
{
	struct sof_ipc4_fw_data *ipc4_data;

	/* common defaults */
	memcpy(&sof_mtl_ops, &sof_hda_common_ops, sizeof(struct snd_sof_dsp_ops));

	/* shutdown */
	sof_mtl_ops.shutdown = hda_dsp_shutdown;

	/* doorbell */
	sof_mtl_ops.irq_thread = mtl_ipc_irq_thread;

	/* ipc */
	sof_mtl_ops.send_msg = mtl_ipc_send_msg;
	sof_mtl_ops.get_mailbox_offset = mtl_dsp_ipc_get_mailbox_offset;
	sof_mtl_ops.get_window_offset = mtl_dsp_ipc_get_window_offset;

	/* debug */
	sof_mtl_ops.debug_map = mtl_dsp_debugfs;
	sof_mtl_ops.debug_map_count = ARRAY_SIZE(mtl_dsp_debugfs);
	sof_mtl_ops.dbg_dump = mtl_dsp_dump;
	sof_mtl_ops.ipc_dump = mtl_ipc_dump;

	/* pre/post fw run */
	sof_mtl_ops.pre_fw_run = mtl_dsp_pre_fw_run;
	sof_mtl_ops.post_fw_run = mtl_dsp_post_fw_run;

	/* parse platform specific extended manifest */
	sof_mtl_ops.parse_platform_ext_manifest = NULL;

	/* dsp core get/put */
	/* TODO: add core_get and core_put */

	sof_mtl_ops.get_stream_position = mtl_dsp_get_stream_hda_link_position;

	sdev->private = devm_kzalloc(sdev->dev, sizeof(struct sof_ipc4_fw_data), GFP_KERNEL);
	if (!sdev->private)
		return -ENOMEM;

	ipc4_data = sdev->private;
	ipc4_data->manifest_fw_hdr_offset = SOF_MAN4_FW_HDR_OFFSET;

	ipc4_data->mtrace_type = SOF_IPC4_MTRACE_INTEL_CAVS_2;

	/* External library loading support */
	ipc4_data->load_library = hda_dsp_ipc4_load_library;

	/* set DAI ops */
	hda_set_dai_drv_ops(sdev, &sof_mtl_ops);

	return 0;
};
EXPORT_SYMBOL_NS(sof_mtl_ops_init, SND_SOC_SOF_INTEL_HDA_COMMON);

const struct sof_intel_dsp_desc mtl_chip_info = {
	.cores_num = 3,
	.init_core_mask = BIT(0),
	.host_managed_cores_mask = BIT(0),
	.ipc_req = MTL_DSP_REG_HFIPCXIDR,
	.ipc_req_mask = MTL_DSP_REG_HFIPCXIDR_BUSY,
	.ipc_ack = MTL_DSP_REG_HFIPCXIDA,
	.ipc_ack_mask = MTL_DSP_REG_HFIPCXIDA_DONE,
	.ipc_ctl = MTL_DSP_REG_HFIPCXCTL,
	.rom_status_reg = MTL_DSP_ROM_STS,
	.rom_init_timeout	= 300,
	.ssp_count = MTL_SSP_COUNT,
	.ssp_base_offset = CNL_SSP_BASE_OFFSET,
	.sdw_shim_base = SDW_SHIM_BASE_ACE,
	.sdw_alh_base = SDW_ALH_BASE_ACE,
	.d0i3_offset = MTL_HDA_VS_D0I3C,
	.read_sdw_lcount =  hda_sdw_check_lcount_common,
	.enable_sdw_irq = mtl_enable_sdw_irq,
	.check_sdw_irq = mtl_dsp_check_sdw_irq,
	.check_ipc_irq = mtl_dsp_check_ipc_irq,
	.cl_init = mtl_dsp_cl_init,
	.power_down_dsp = mtl_power_down_dsp,
	.disable_interrupts = mtl_dsp_disable_interrupts,
	.hw_ip_version = SOF_INTEL_ACE_1_0,
};
EXPORT_SYMBOL_NS(mtl_chip_info, SND_SOC_SOF_INTEL_HDA_COMMON);
