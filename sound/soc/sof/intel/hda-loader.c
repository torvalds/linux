// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation
//
// Authors: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//	    Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//	    Rander Wang <rander.wang@intel.com>
//          Keyon Jie <yang.jie@linux.intel.com>
//

/*
 * Hardware interface for HDA DSP code loader
 */

#include <linux/firmware.h>
#include <sound/hdaudio_ext.h>
#include <sound/hda_register.h>
#include <sound/sof.h>
#include <sound/sof/ipc4/header.h>
#include "ext_manifest.h"
#include "../ipc4-priv.h"
#include "../ops.h"
#include "../sof-priv.h"
#include "hda.h"

static bool persistent_cl_buffer = true;
module_param(persistent_cl_buffer, bool, 0444);
MODULE_PARM_DESC(persistent_cl_buffer, "Persistent Code Loader DMA buffer "
		 "(default = Y, use N to force buffer re-allocation)");

static void hda_ssp_set_cbp_cfp(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;
	int i;

	/* DSP is powered up, set all SSPs to clock consumer/codec provider mode */
	for (i = 0; i < chip->ssp_count; i++) {
		snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR,
						 chip->ssp_base_offset
						 + i * SSP_DEV_MEM_SIZE
						 + SSP_SSC1_OFFSET,
						 SSP_SET_CBP_CFP,
						 SSP_SET_CBP_CFP);
	}
}

struct hdac_ext_stream*
hda_cl_prepare(struct device *dev, unsigned int format, unsigned int size,
	       struct snd_dma_buffer *dmab, bool persistent_buffer, int direction,
	       bool is_iccmax)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	struct hdac_ext_stream *hext_stream;
	struct hdac_stream *hstream;
	int ret;

	hext_stream = hda_dsp_stream_get(sdev, direction, 0);

	if (!hext_stream) {
		dev_err(sdev->dev, "error: no stream available\n");
		return ERR_PTR(-ENODEV);
	}
	hstream = &hext_stream->hstream;
	hstream->substream = NULL;

	/*
	 * Allocate DMA buffer if it is temporary or if the buffer is intended
	 * to be persistent but not yet allocated.
	 * We cannot rely solely on !dmab->area as caller might use a struct on
	 * stack (when it is temporary) without clearing it to 0.
	 */
	if (!persistent_buffer || !dmab->area) {
		ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV_SG, dev, size, dmab);
		if (ret < 0) {
			dev_err(sdev->dev, "%s: memory alloc failed: %d\n",
				__func__, ret);
			goto out_put;
		}
	}

	hstream->period_bytes = 0;/* initialize period_bytes */
	hstream->format_val = format;
	hstream->bufsize = size;

	if (is_iccmax) {
		ret = hda_dsp_iccmax_stream_hw_params(sdev, hext_stream, dmab, NULL);
		if (ret < 0) {
			dev_err(sdev->dev, "error: iccmax stream prepare failed: %d\n", ret);
			goto out_free;
		}
	} else {
		ret = hda_dsp_stream_hw_params(sdev, hext_stream, dmab, NULL);
		if (ret < 0) {
			dev_err(sdev->dev, "error: hdac prepare failed: %d\n", ret);
			goto out_free;
		}
		hda_dsp_stream_spib_config(sdev, hext_stream, HDA_DSP_SPIB_ENABLE, size);
	}

	return hext_stream;

out_free:
	snd_dma_free_pages(dmab);
	dmab->area = NULL;
	dmab->bytes = 0;
	hstream->bufsize = 0;
	hstream->format_val = 0;
out_put:
	hda_dsp_stream_put(sdev, direction, hstream->stream_tag);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_NS(hda_cl_prepare, "SND_SOC_SOF_INTEL_HDA_COMMON");

/*
 * first boot sequence has some extra steps.
 * power on all host managed cores and only unstall/run the boot core to boot the
 * DSP then turn off all non boot cores (if any) is powered on.
 */
int cl_dsp_init(struct snd_sof_dev *sdev, int stream_tag, bool imr_boot)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;
	unsigned int status, target_status;
	u32 flags, ipc_hdr, j;
	unsigned long mask;
	char *dump_msg;
	int ret;

	/* step 1: power up corex */
	ret = hda_dsp_core_power_up(sdev, chip->host_managed_cores_mask);
	if (ret < 0) {
		if (hda->boot_iteration == HDA_FW_BOOT_ATTEMPTS)
			dev_err(sdev->dev, "error: dsp core 0/1 power up failed\n");
		goto err;
	}

	hda_ssp_set_cbp_cfp(sdev);

	/* step 2: Send ROM_CONTROL command (stream_tag is ignored for IMR boot) */
	ipc_hdr = chip->ipc_req_mask | HDA_DSP_ROM_IPC_CONTROL;
	if (!imr_boot)
		ipc_hdr |= HDA_DSP_ROM_IPC_PURGE_FW | ((stream_tag - 1) << 9);

	snd_sof_dsp_write(sdev, HDA_DSP_BAR, chip->ipc_req, ipc_hdr);

	/* step 3: unset core 0 reset state & unstall/run core 0 */
	ret = hda_dsp_core_run(sdev, chip->init_core_mask);
	if (ret < 0) {
		if (hda->boot_iteration == HDA_FW_BOOT_ATTEMPTS)
			dev_err(sdev->dev,
				"error: dsp core start failed %d\n", ret);
		ret = -EIO;
		goto err;
	}

	/* step 4: wait for IPC DONE bit from ROM */
	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR,
					    chip->ipc_ack, status,
					    ((status & chip->ipc_ack_mask)
						    == chip->ipc_ack_mask),
					    HDA_DSP_REG_POLL_INTERVAL_US,
					    HDA_DSP_INIT_TIMEOUT_US);

	if (ret < 0) {
		if (hda->boot_iteration == HDA_FW_BOOT_ATTEMPTS)
			dev_err(sdev->dev,
				"error: %s: timeout for HIPCIE done\n",
				__func__);
		goto err;
	}

	/* set DONE bit to clear the reply IPC message */
	snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR,
				       chip->ipc_ack,
				       chip->ipc_ack_mask,
				       chip->ipc_ack_mask);

	/* step 5: power down cores that are no longer needed */
	ret = hda_dsp_core_reset_power_down(sdev, chip->host_managed_cores_mask &
					   ~(chip->init_core_mask));
	if (ret < 0) {
		if (hda->boot_iteration == HDA_FW_BOOT_ATTEMPTS)
			dev_err(sdev->dev,
				"error: dsp core x power down failed\n");
		goto err;
	}

	/* step 6: enable IPC interrupts */
	hda_dsp_ipc_int_enable(sdev);

	/*
	 * step 7:
	 * - Cold/Full boot: wait for ROM init to proceed to download the firmware
	 * - IMR boot: wait for ROM firmware entered (firmware booted up from IMR)
	 */
	if (imr_boot)
		target_status = FSR_STATE_FW_ENTERED;
	else
		target_status = FSR_STATE_INIT_DONE;

	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR,
					chip->rom_status_reg, status,
					(FSR_TO_STATE_CODE(status) == target_status),
					HDA_DSP_REG_POLL_INTERVAL_US,
					chip->rom_init_timeout *
					USEC_PER_MSEC);
	if (!ret) {
		/* set enabled cores mask and increment ref count for cores in init_core_mask */
		sdev->enabled_cores_mask |= chip->init_core_mask;
		mask = sdev->enabled_cores_mask;
		for_each_set_bit(j, &mask, SOF_MAX_DSP_NUM_CORES)
			sdev->dsp_core_ref_count[j]++;
		return 0;
	}

	if (hda->boot_iteration == HDA_FW_BOOT_ATTEMPTS)
		dev_err(sdev->dev,
			"%s: timeout with rom_status_reg (%#x) read\n",
			__func__, chip->rom_status_reg);

err:
	flags = SOF_DBG_DUMP_PCI | SOF_DBG_DUMP_MBOX | SOF_DBG_DUMP_OPTIONAL;

	/* after max boot attempts make sure that the dump is printed */
	if (hda->boot_iteration == HDA_FW_BOOT_ATTEMPTS)
		flags &= ~SOF_DBG_DUMP_OPTIONAL;

	dump_msg = kasprintf(GFP_KERNEL, "Boot iteration failed: %d/%d",
			     hda->boot_iteration, HDA_FW_BOOT_ATTEMPTS);
	snd_sof_dsp_dbg_dump(sdev, dump_msg, flags);
	hda_dsp_core_reset_power_down(sdev, chip->host_managed_cores_mask);

	kfree(dump_msg);
	return ret;
}
EXPORT_SYMBOL_NS(cl_dsp_init, "SND_SOC_SOF_INTEL_HDA_COMMON");

int hda_cl_trigger(struct device *dev, struct hdac_ext_stream *hext_stream, int cmd)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	struct hdac_stream *hstream = &hext_stream->hstream;
	int sd_offset = SOF_STREAM_SD_OFFSET(hstream);
	struct sof_intel_hda_stream *hda_stream;

	/* code loader is special case that reuses stream ops */
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		hda_stream = container_of(hext_stream, struct sof_intel_hda_stream,
					  hext_stream);
		reinit_completion(&hda_stream->ioc);

		snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, SOF_HDA_INTCTL,
					1 << hstream->index,
					1 << hstream->index);

		snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
					sd_offset,
					SOF_HDA_SD_CTL_DMA_START |
					SOF_HDA_CL_DMA_SD_INT_MASK,
					SOF_HDA_SD_CTL_DMA_START |
					SOF_HDA_CL_DMA_SD_INT_MASK);

		hstream->running = true;
		return 0;
	default:
		return hda_dsp_stream_trigger(sdev, hext_stream, cmd);
	}
}
EXPORT_SYMBOL_NS(hda_cl_trigger, "SND_SOC_SOF_INTEL_HDA_COMMON");

int hda_cl_cleanup(struct device *dev, struct snd_dma_buffer *dmab,
			  bool persistent_buffer, struct hdac_ext_stream *hext_stream)
{
	struct snd_sof_dev *sdev =  dev_get_drvdata(dev);
	struct hdac_stream *hstream = &hext_stream->hstream;
	int sd_offset = SOF_STREAM_SD_OFFSET(hstream);
	int ret = 0;

	if (hstream->direction == SNDRV_PCM_STREAM_PLAYBACK)
		ret = hda_dsp_stream_spib_config(sdev, hext_stream, HDA_DSP_SPIB_DISABLE, 0);
	else
		snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, sd_offset,
					SOF_HDA_SD_CTL_DMA_START, 0);

	hda_dsp_stream_put(sdev, hstream->direction, hstream->stream_tag);
	hstream->running = 0;
	hstream->substream = NULL;

	/* reset BDL address */
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_SD_BDLPL, 0);
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_SD_BDLPU, 0);

	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR, sd_offset, 0);

	if (!persistent_buffer) {
		snd_dma_free_pages(dmab);
		dmab->area = NULL;
		dmab->bytes = 0;
		hstream->bufsize = 0;
		hstream->format_val = 0;
	}

	return ret;
}
EXPORT_SYMBOL_NS(hda_cl_cleanup, "SND_SOC_SOF_INTEL_HDA_COMMON");

#define HDA_CL_DMA_IOC_TIMEOUT_MS 500

int hda_cl_copy_fw(struct snd_sof_dev *sdev, struct hdac_ext_stream *hext_stream)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;
	unsigned int reg;
	int ret, status;

	dev_dbg(sdev->dev, "Code loader DMA starting\n");

	ret = hda_cl_trigger(sdev->dev, hext_stream, SNDRV_PCM_TRIGGER_START);
	if (ret < 0) {
		dev_err(sdev->dev, "error: DMA trigger start failed\n");
		return ret;
	}

	dev_dbg(sdev->dev, "waiting for FW_ENTERED status\n");

	status = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR,
					chip->rom_status_reg, reg,
					(FSR_TO_STATE_CODE(reg) == FSR_STATE_FW_ENTERED),
					HDA_DSP_REG_POLL_INTERVAL_US,
					HDA_DSP_BASEFW_TIMEOUT_US);

	/*
	 * even in case of errors we still need to stop the DMAs,
	 * but we return the initial error should the DMA stop also fail
	 */

	if (status < 0) {
		dev_err(sdev->dev,
			"%s: timeout with rom_status_reg (%#x) read\n",
			__func__, chip->rom_status_reg);
	} else {
		dev_dbg(sdev->dev, "Code loader FW_ENTERED status\n");
	}

	ret = hda_cl_trigger(sdev->dev, hext_stream, SNDRV_PCM_TRIGGER_STOP);
	if (ret < 0) {
		dev_err(sdev->dev, "error: DMA trigger stop failed\n");
		if (!status)
			status = ret;
	} else {
		dev_dbg(sdev->dev, "Code loader DMA stopped\n");
	}

	return status;
}

int hda_dsp_cl_boot_firmware_iccmax(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	struct hdac_ext_stream *iccmax_stream;
	int ret, ret1;
	u8 original_gb;

	/* save the original LTRP guardband value */
	original_gb = snd_sof_dsp_read8(sdev, HDA_DSP_HDA_BAR, HDA_VS_INTEL_LTRP) &
		HDA_VS_INTEL_LTRP_GB_MASK;

	/*
	 * Prepare capture stream for ICCMAX. We do not need to store
	 * the data, so use a buffer of PAGE_SIZE for receiving.
	 */
	iccmax_stream = hda_cl_prepare(sdev->dev, HDA_CL_STREAM_FORMAT, PAGE_SIZE,
				       &hda->iccmax_dmab, persistent_cl_buffer,
				       SNDRV_PCM_STREAM_CAPTURE, true);
	if (IS_ERR(iccmax_stream)) {
		dev_err(sdev->dev, "error: dma prepare for ICCMAX stream failed\n");
		return PTR_ERR(iccmax_stream);
	}

	ret = hda_dsp_cl_boot_firmware(sdev);

	/*
	 * Perform iccmax stream cleanup. This should be done even if firmware loading fails.
	 * If the cleanup also fails, we return the initial error
	 */
	ret1 = hda_cl_cleanup(sdev->dev, &hda->iccmax_dmab,
			      persistent_cl_buffer, iccmax_stream);
	if (ret1 < 0) {
		dev_err(sdev->dev, "error: ICCMAX stream cleanup failed\n");

		/* set return value to indicate cleanup failure */
		if (!ret)
			ret = ret1;
	}

	/* restore the original guardband value after FW boot */
	snd_sof_dsp_update8(sdev, HDA_DSP_HDA_BAR, HDA_VS_INTEL_LTRP,
			    HDA_VS_INTEL_LTRP_GB_MASK, original_gb);

	return ret;
}
EXPORT_SYMBOL_NS(hda_dsp_cl_boot_firmware_iccmax, "SND_SOC_SOF_INTEL_CNL");

static int hda_dsp_boot_imr(struct snd_sof_dev *sdev)
{
	const struct sof_intel_dsp_desc *chip_info;
	int ret;

	chip_info = get_chip_info(sdev->pdata);
	if (chip_info->cl_init)
		ret = chip_info->cl_init(sdev, 0, true);
	else
		ret = -EINVAL;

	if (!ret)
		hda_sdw_process_wakeen(sdev);

	return ret;
}

int hda_dsp_cl_boot_firmware(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	struct snd_sof_pdata *plat_data = sdev->pdata;
	const struct sof_dev_desc *desc = plat_data->desc;
	const struct sof_intel_dsp_desc *chip_info;
	struct hdac_ext_stream *hext_stream;
	struct firmware stripped_firmware;
	int ret, ret1, i;

	if (hda->imrboot_supported && !sdev->first_boot && !hda->skip_imr_boot) {
		dev_dbg(sdev->dev, "IMR restore supported, booting from IMR directly\n");
		hda->boot_iteration = 0;
		ret = hda_dsp_boot_imr(sdev);
		if (!ret) {
			hda->booted_from_imr = true;
			return 0;
		}

		dev_warn(sdev->dev, "IMR restore failed, trying to cold boot\n");
	}

	hda->booted_from_imr = false;

	chip_info = desc->chip_info;

	if (sdev->basefw.fw->size <= sdev->basefw.payload_offset) {
		dev_err(sdev->dev, "error: firmware size must be greater than firmware offset\n");
		return -EINVAL;
	}

	/* init for booting wait */
	init_waitqueue_head(&sdev->boot_wait);

	/* prepare DMA for code loader stream */
	stripped_firmware.size = sdev->basefw.fw->size - sdev->basefw.payload_offset;
	hext_stream = hda_cl_prepare(sdev->dev, HDA_CL_STREAM_FORMAT,
				     stripped_firmware.size,
				     &hda->cl_dmab, persistent_cl_buffer,
				     SNDRV_PCM_STREAM_PLAYBACK, false);
	if (IS_ERR(hext_stream)) {
		dev_err(sdev->dev, "error: dma prepare for fw loading failed\n");
		return PTR_ERR(hext_stream);
	}

	/*
	 * Copy the payload to the DMA buffer if it is temporary or if the
	 * buffer is  persistent but it does not have the basefw payload either
	 * because this is the first boot and the buffer needs to be initialized,
	 * or a library got loaded and it replaced the basefw.
	 */
	if (!persistent_cl_buffer || !hda->cl_dmab_contains_basefw) {
		stripped_firmware.data = sdev->basefw.fw->data + sdev->basefw.payload_offset;
		memcpy(hda->cl_dmab.area, stripped_firmware.data, stripped_firmware.size);
		hda->cl_dmab_contains_basefw = true;
	}

	/* try ROM init a few times before giving up */
	for (i = 0; i < HDA_FW_BOOT_ATTEMPTS; i++) {
		dev_dbg(sdev->dev,
			"Attempting iteration %d of Core En/ROM load...\n", i);

		hda->boot_iteration = i + 1;
		if (chip_info->cl_init)
			ret = chip_info->cl_init(sdev, hext_stream->hstream.stream_tag, false);
		else
			ret = -EINVAL;

		/* don't retry anymore if successful */
		if (!ret)
			break;
	}

	if (i == HDA_FW_BOOT_ATTEMPTS) {
		dev_err(sdev->dev, "error: dsp init failed after %d attempts with err: %d\n",
			i, ret);
		goto cleanup;
	}

	/*
	 * When a SoundWire link is in clock stop state, a Slave
	 * device may trigger in-band wakes for events such as jack
	 * insertion or acoustic event detection. This event will lead
	 * to a WAKEEN interrupt, handled by the PCI device and routed
	 * to PME if the PCI device is in D3. The resume function in
	 * audio PCI driver will be invoked by ACPI for PME event and
	 * initialize the device and process WAKEEN interrupt.
	 *
	 * The WAKEEN interrupt should be processed ASAP to prevent an
	 * interrupt flood, otherwise other interrupts, such IPC,
	 * cannot work normally.  The WAKEEN is handled after the ROM
	 * is initialized successfully, which ensures power rails are
	 * enabled before accessing the SoundWire SHIM registers
	 */
	if (!sdev->first_boot)
		hda_sdw_process_wakeen(sdev);

	/*
	 * Set the boot_iteration to the last attempt, indicating that the
	 * DSP ROM has been initialized and from this point there will be no
	 * retry done to boot.
	 *
	 * Continue with code loading and firmware boot
	 */
	hda->boot_iteration = HDA_FW_BOOT_ATTEMPTS;
	ret = hda_cl_copy_fw(sdev, hext_stream);
	if (!ret) {
		dev_dbg(sdev->dev, "Firmware download successful, booting...\n");
		hda->skip_imr_boot = false;
	} else {
		snd_sof_dsp_dbg_dump(sdev, "Firmware download failed",
				     SOF_DBG_DUMP_PCI | SOF_DBG_DUMP_MBOX);
		hda->skip_imr_boot = true;
	}

cleanup:
	/*
	 * Perform codeloader stream cleanup.
	 * This should be done even if firmware loading fails.
	 * If the cleanup also fails, we return the initial error
	 */
	ret1 = hda_cl_cleanup(sdev->dev, &hda->cl_dmab,
			      persistent_cl_buffer, hext_stream);
	if (ret1 < 0) {
		dev_err(sdev->dev, "error: Code loader DSP cleanup failed\n");

		/* set return value to indicate cleanup failure */
		if (!ret)
			ret = ret1;
	}

	/*
	 * return primary core id if both fw copy
	 * and stream clean up are successful
	 */
	if (!ret)
		return chip_info->init_core_mask;

	/* disable DSP */
	hda_dsp_ctrl_ppcap_enable(sdev, false);

	return ret;
}
EXPORT_SYMBOL_NS(hda_dsp_cl_boot_firmware, "SND_SOC_SOF_INTEL_HDA_COMMON");

int hda_dsp_ipc4_load_library(struct snd_sof_dev *sdev,
			      struct sof_ipc4_fw_library *fw_lib, bool reload)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct hdac_ext_stream *hext_stream;
	struct firmware stripped_firmware;
	struct sof_ipc4_msg msg = {};
	int ret, ret1;

	/* if IMR booting is enabled and fw context is saved for D3 state, skip the loading */
	if (reload && hda->booted_from_imr && ipc4_data->fw_context_save)
		return 0;

	/* the fw_lib has been verified during loading, we can trust the validity here */
	stripped_firmware.data = fw_lib->sof_fw.fw->data + fw_lib->sof_fw.payload_offset;
	stripped_firmware.size = fw_lib->sof_fw.fw->size - fw_lib->sof_fw.payload_offset;

	/*
	 * force re-allocation of the cl_dmab if the preserved DMA buffer is
	 * smaller than what is needed for the library
	 */
	if (persistent_cl_buffer && stripped_firmware.size > hda->cl_dmab.bytes) {
		snd_dma_free_pages(&hda->cl_dmab);
		hda->cl_dmab.area = NULL;
		hda->cl_dmab.bytes = 0;
	}

	/* prepare DMA for code loader stream */
	hext_stream = hda_cl_prepare(sdev->dev, HDA_CL_STREAM_FORMAT,
				     stripped_firmware.size,
				     &hda->cl_dmab, persistent_cl_buffer,
				     SNDRV_PCM_STREAM_PLAYBACK, false);
	if (IS_ERR(hext_stream)) {
		dev_err(sdev->dev, "%s: DMA prepare failed\n", __func__);
		return PTR_ERR(hext_stream);
	}

	memcpy(hda->cl_dmab.area, stripped_firmware.data, stripped_firmware.size);
	hda->cl_dmab_contains_basefw = false;

	/*
	 * 1st stage: SOF_IPC4_GLB_LOAD_LIBRARY_PREPARE
	 * Message includes the dma_id to be prepared for the library loading.
	 * If the firmware does not have support for the message, we will
	 * receive -EOPNOTSUPP. In this case we will use single step library
	 * loading and proceed to send the LOAD_LIBRARY message.
	 */
	msg.primary = hext_stream->hstream.stream_tag - 1;
	msg.primary |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_GLB_LOAD_LIBRARY_PREPARE);
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_FW_GEN_MSG);
	ret = sof_ipc_tx_message_no_reply(sdev->ipc, &msg, 0);
	if (!ret) {
		int sd_offset = SOF_STREAM_SD_OFFSET(&hext_stream->hstream);
		unsigned int status;

		/*
		 * Make sure that the FIFOS value is not 0 in SDxFIFOS register
		 * which indicates that the firmware set the GEN bit and we can
		 * continue to start the DMA
		 */
		ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_HDA_BAR,
					sd_offset + SOF_HDA_ADSP_REG_SD_FIFOSIZE,
					status,
					status & SOF_HDA_SD_FIFOSIZE_FIFOS_MASK,
					HDA_DSP_REG_POLL_INTERVAL_US,
					HDA_DSP_BASEFW_TIMEOUT_US);

		if (ret < 0)
			dev_warn(sdev->dev,
				 "%s: timeout waiting for FIFOS\n", __func__);
	} else if (ret != -EOPNOTSUPP) {
		goto cleanup;
	}

	ret = hda_cl_trigger(sdev->dev, hext_stream, SNDRV_PCM_TRIGGER_START);
	if (ret < 0) {
		dev_err(sdev->dev, "%s: DMA trigger start failed\n", __func__);
		goto cleanup;
	}

	/*
	 * 2nd stage: LOAD_LIBRARY
	 * Message includes the dma_id and the lib_id, the dma_id must be
	 * identical to the one sent via LOAD_LIBRARY_PREPARE
	 */
	msg.primary &= ~SOF_IPC4_MSG_TYPE_MASK;
	msg.primary |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_GLB_LOAD_LIBRARY);
	msg.primary |= SOF_IPC4_GLB_LOAD_LIBRARY_LIB_ID(fw_lib->id);
	ret = sof_ipc_tx_message_no_reply(sdev->ipc, &msg, 0);

	/* Stop the DMA channel */
	ret1 = hda_cl_trigger(sdev->dev, hext_stream, SNDRV_PCM_TRIGGER_STOP);
	if (ret1 < 0) {
		dev_err(sdev->dev, "%s: DMA trigger stop failed\n", __func__);
		if (!ret)
			ret = ret1;
	}

cleanup:
	/* clean up even in case of error and return the first error */
	ret1 = hda_cl_cleanup(sdev->dev, &hda->cl_dmab, persistent_cl_buffer,
			      hext_stream);
	if (ret1 < 0) {
		dev_err(sdev->dev, "%s: Code loader DSP cleanup failed\n", __func__);

		/* set return value to indicate cleanup failure */
		if (!ret)
			ret = ret1;
	}

	return ret;
}
EXPORT_SYMBOL_NS(hda_dsp_ipc4_load_library, "SND_SOC_SOF_INTEL_HDA_COMMON");

int hda_dsp_ext_man_get_cavs_config_data(struct snd_sof_dev *sdev,
					 const struct sof_ext_man_elem_header *hdr)
{
	const struct sof_ext_man_cavs_config_data *config_data =
		container_of(hdr, struct sof_ext_man_cavs_config_data, hdr);
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	int i, elem_num;

	/* calculate total number of config data elements */
	elem_num = (hdr->size - sizeof(struct sof_ext_man_elem_header))
		   / sizeof(struct sof_config_elem);
	if (elem_num <= 0) {
		dev_err(sdev->dev, "cavs config data is inconsistent: %d\n", elem_num);
		return -EINVAL;
	}

	for (i = 0; i < elem_num; i++)
		switch (config_data->elems[i].token) {
		case SOF_EXT_MAN_CAVS_CONFIG_EMPTY:
			/* skip empty token */
			break;
		case SOF_EXT_MAN_CAVS_CONFIG_CAVS_LPRO:
			hda->clk_config_lpro = config_data->elems[i].value;
			dev_dbg(sdev->dev, "FW clock config: %s\n",
				hda->clk_config_lpro ? "LPRO" : "HPRO");
			break;
		case SOF_EXT_MAN_CAVS_CONFIG_OUTBOX_SIZE:
		case SOF_EXT_MAN_CAVS_CONFIG_INBOX_SIZE:
			/* These elements are defined but not being used yet. No warn is required */
			break;
		default:
			dev_info(sdev->dev, "unsupported token type: %d\n",
				 config_data->elems[i].token);
		}

	return 0;
}
EXPORT_SYMBOL_NS(hda_dsp_ext_man_get_cavs_config_data, "SND_SOC_SOF_INTEL_HDA_COMMON");
