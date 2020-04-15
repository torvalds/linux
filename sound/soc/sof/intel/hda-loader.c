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
 * Hardware interface for HDA DSP code loader
 */

#include <linux/firmware.h>
#include <sound/hdaudio_ext.h>
#include <sound/sof.h>
#include "../ops.h"
#include "hda.h"

#define HDA_FW_BOOT_ATTEMPTS	3

static int cl_stream_prepare(struct snd_sof_dev *sdev, unsigned int format,
			     unsigned int size, struct snd_dma_buffer *dmab,
			     int direction)
{
	struct hdac_ext_stream *dsp_stream;
	struct hdac_stream *hstream;
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	int ret;

	if (direction != SNDRV_PCM_STREAM_PLAYBACK) {
		dev_err(sdev->dev, "error: code loading DMA is playback only\n");
		return -EINVAL;
	}

	dsp_stream = hda_dsp_stream_get(sdev, direction);

	if (!dsp_stream) {
		dev_err(sdev->dev, "error: no stream available\n");
		return -ENODEV;
	}
	hstream = &dsp_stream->hstream;
	hstream->substream = NULL;

	/* allocate DMA buffer */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV_SG, &pci->dev, size, dmab);
	if (ret < 0) {
		dev_err(sdev->dev, "error: memory alloc failed: %x\n", ret);
		goto error;
	}

	hstream->period_bytes = 0;/* initialize period_bytes */
	hstream->format_val = format;
	hstream->bufsize = size;

	ret = hda_dsp_stream_hw_params(sdev, dsp_stream, dmab, NULL);
	if (ret < 0) {
		dev_err(sdev->dev, "error: hdac prepare failed: %x\n", ret);
		goto error;
	}

	hda_dsp_stream_spib_config(sdev, dsp_stream, HDA_DSP_SPIB_ENABLE, size);

	return hstream->stream_tag;

error:
	hda_dsp_stream_put(sdev, direction, hstream->stream_tag);
	snd_dma_free_pages(dmab);
	return ret;
}

/*
 * first boot sequence has some extra steps. core 0 waits for power
 * status on core 1, so power up core 1 also momentarily, keep it in
 * reset/stall and then turn it off
 */
static int cl_dsp_init(struct snd_sof_dev *sdev, const void *fwdata,
		       u32 fwsize, int stream_tag)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;
	unsigned int status;
	int ret;
	int i;

	/* step 1: power up corex */
	ret = hda_dsp_core_power_up(sdev, chip->cores_mask);
	if (ret < 0) {
		dev_err(sdev->dev, "error: dsp core 0/1 power up failed\n");
		goto err;
	}

	/* DSP is powered up, set all SSPs to slave mode */
	for (i = 0; i < chip->ssp_count; i++) {
		snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR,
						 chip->ssp_base_offset
						 + i * SSP_DEV_MEM_SIZE
						 + SSP_SSC1_OFFSET,
						 SSP_SET_SLAVE,
						 SSP_SET_SLAVE);
	}

	/* step 2: purge FW request */
	snd_sof_dsp_write(sdev, HDA_DSP_BAR, chip->ipc_req,
			  chip->ipc_req_mask | (HDA_DSP_IPC_PURGE_FW |
			  ((stream_tag - 1) << 9)));

	/* step 3: unset core 0 reset state & unstall/run core 0 */
	ret = hda_dsp_core_run(sdev, HDA_DSP_CORE_MASK(0));
	if (ret < 0) {
		dev_err(sdev->dev, "error: dsp core start failed %d\n", ret);
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
		dev_err(sdev->dev, "error: %s: timeout for HIPCIE done\n",
			__func__);
		goto err;
	}

	/* set DONE bit to clear the reply IPC message */
	snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR,
				       chip->ipc_ack,
				       chip->ipc_ack_mask,
				       chip->ipc_ack_mask);

	/* step 5: power down corex */
	ret = hda_dsp_core_power_down(sdev,
				  chip->cores_mask & ~(HDA_DSP_CORE_MASK(0)));
	if (ret < 0) {
		dev_err(sdev->dev, "error: dsp core x power down failed\n");
		goto err;
	}

	/* step 6: enable IPC interrupts */
	hda_dsp_ipc_int_enable(sdev);

	/* step 7: wait for ROM init */
	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR,
					HDA_DSP_SRAM_REG_ROM_STATUS, status,
					((status & HDA_DSP_ROM_STS_MASK)
						== HDA_DSP_ROM_INIT),
					HDA_DSP_REG_POLL_INTERVAL_US,
					chip->rom_init_timeout *
					USEC_PER_MSEC);
	if (!ret)
		return 0;

	dev_err(sdev->dev,
		"error: %s: timeout HDA_DSP_SRAM_REG_ROM_STATUS read\n",
		__func__);

err:
	hda_dsp_dump(sdev, SOF_DBG_REGS | SOF_DBG_PCI | SOF_DBG_MBOX);
	hda_dsp_core_reset_power_down(sdev, chip->cores_mask);

	return ret;
}

static int cl_trigger(struct snd_sof_dev *sdev,
		      struct hdac_ext_stream *stream, int cmd)
{
	struct hdac_stream *hstream = &stream->hstream;
	int sd_offset = SOF_STREAM_SD_OFFSET(hstream);

	/* code loader is special case that reuses stream ops */
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
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
		return hda_dsp_stream_trigger(sdev, stream, cmd);
	}
}

static struct hdac_ext_stream *get_stream_with_tag(struct snd_sof_dev *sdev,
						   int tag)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct hdac_stream *s;

	/* get stream with tag */
	list_for_each_entry(s, &bus->stream_list, list) {
		if (s->direction == SNDRV_PCM_STREAM_PLAYBACK &&
		    s->stream_tag == tag) {
			return stream_to_hdac_ext_stream(s);
		}
	}

	return NULL;
}

static int cl_cleanup(struct snd_sof_dev *sdev, struct snd_dma_buffer *dmab,
		      struct hdac_ext_stream *stream)
{
	struct hdac_stream *hstream = &stream->hstream;
	int sd_offset = SOF_STREAM_SD_OFFSET(hstream);
	int ret;

	ret = hda_dsp_stream_spib_config(sdev, stream, HDA_DSP_SPIB_DISABLE, 0);

	hda_dsp_stream_put(sdev, SNDRV_PCM_STREAM_PLAYBACK,
			   hstream->stream_tag);
	hstream->running = 0;
	hstream->substream = NULL;

	/* reset BDL address */
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPL, 0);
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPU, 0);

	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR, sd_offset, 0);
	snd_dma_free_pages(dmab);
	dmab->area = NULL;
	hstream->bufsize = 0;
	hstream->format_val = 0;

	return ret;
}

static int cl_copy_fw(struct snd_sof_dev *sdev, struct hdac_ext_stream *stream)
{
	unsigned int reg;
	int ret, status;

	ret = cl_trigger(sdev, stream, SNDRV_PCM_TRIGGER_START);
	if (ret < 0) {
		dev_err(sdev->dev, "error: DMA trigger start failed\n");
		return ret;
	}

	status = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR,
					HDA_DSP_SRAM_REG_ROM_STATUS, reg,
					((reg & HDA_DSP_ROM_STS_MASK)
						== HDA_DSP_ROM_FW_ENTERED),
					HDA_DSP_REG_POLL_INTERVAL_US,
					HDA_DSP_BASEFW_TIMEOUT_US);

	/*
	 * even in case of errors we still need to stop the DMAs,
	 * but we return the initial error should the DMA stop also fail
	 */

	if (status < 0) {
		dev_err(sdev->dev,
			"error: %s: timeout HDA_DSP_SRAM_REG_ROM_STATUS read\n",
			__func__);
	}

	ret = cl_trigger(sdev, stream, SNDRV_PCM_TRIGGER_STOP);
	if (ret < 0) {
		dev_err(sdev->dev, "error: DMA trigger stop failed\n");
		if (!status)
			status = ret;
	}

	return status;
}

int hda_dsp_cl_boot_firmware(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *plat_data = sdev->pdata;
	const struct sof_dev_desc *desc = plat_data->desc;
	const struct sof_intel_dsp_desc *chip_info;
	struct hdac_ext_stream *stream;
	struct firmware stripped_firmware;
	int ret, ret1, tag, i;

	chip_info = desc->chip_info;

	if (plat_data->fw->size < plat_data->fw_offset) {
		dev_err(sdev->dev, "error: firmware size must be greater than firmware offset\n");
		return -EINVAL;
	}

	stripped_firmware.data = plat_data->fw->data + plat_data->fw_offset;
	stripped_firmware.size = plat_data->fw->size - plat_data->fw_offset;

	/* init for booting wait */
	init_waitqueue_head(&sdev->boot_wait);

	/* prepare DMA for code loader stream */
	tag = cl_stream_prepare(sdev, 0x40, stripped_firmware.size,
				&sdev->dmab, SNDRV_PCM_STREAM_PLAYBACK);

	if (tag < 0) {
		dev_err(sdev->dev, "error: dma prepare for fw loading err: %x\n",
			tag);
		return tag;
	}

	/* get stream with tag */
	stream = get_stream_with_tag(sdev, tag);
	if (!stream) {
		dev_err(sdev->dev,
			"error: could not get stream with stream tag %d\n",
			tag);
		ret = -ENODEV;
		goto err;
	}

	memcpy(sdev->dmab.area, stripped_firmware.data,
	       stripped_firmware.size);

	/* try ROM init a few times before giving up */
	for (i = 0; i < HDA_FW_BOOT_ATTEMPTS; i++) {
		ret = cl_dsp_init(sdev, stripped_firmware.data,
				  stripped_firmware.size, tag);

		/* don't retry anymore if successful */
		if (!ret)
			break;

		dev_dbg(sdev->dev, "iteration %d of Core En/ROM load failed: %d\n",
			i, ret);
		dev_dbg(sdev->dev, "Error code=0x%x: FW status=0x%x\n",
			snd_sof_dsp_read(sdev, HDA_DSP_BAR,
					 HDA_DSP_SRAM_REG_ROM_ERROR),
			snd_sof_dsp_read(sdev, HDA_DSP_BAR,
					 HDA_DSP_SRAM_REG_ROM_STATUS));
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
	 * at this point DSP ROM has been initialized and
	 * should be ready for code loading and firmware boot
	 */
	ret = cl_copy_fw(sdev, stream);
	if (!ret)
		dev_dbg(sdev->dev, "Firmware download successful, booting...\n");
	else
		dev_err(sdev->dev, "error: load fw failed ret: %d\n", ret);

cleanup:
	/*
	 * Perform codeloader stream cleanup.
	 * This should be done even if firmware loading fails.
	 * If the cleanup also fails, we return the initial error
	 */
	ret1 = cl_cleanup(sdev, &sdev->dmab, stream);
	if (ret1 < 0) {
		dev_err(sdev->dev, "error: Code loader DSP cleanup failed\n");

		/* set return value to indicate cleanup failure */
		if (!ret)
			ret = ret1;
	}

	/*
	 * return master core id if both fw copy
	 * and stream clean up are successful
	 */
	if (!ret)
		return chip_info->init_core_mask;

	/* dump dsp registers and disable DSP upon error */
err:
	hda_dsp_dump(sdev, SOF_DBG_REGS | SOF_DBG_PCI | SOF_DBG_MBOX);

	/* disable DSP */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_PP_BAR,
				SOF_HDA_REG_PP_PPCTL,
				SOF_HDA_PPCTL_GPROCEN, 0);
	return ret;
}

/* pre fw run operations */
int hda_dsp_pre_fw_run(struct snd_sof_dev *sdev)
{
	/* disable clock gating and power gating */
	return hda_dsp_ctrl_clock_power_gating(sdev, false);
}

/* post fw run operations */
int hda_dsp_post_fw_run(struct snd_sof_dev *sdev)
{
	int ret;

	if (sdev->first_boot) {
		ret = hda_sdw_startup(sdev);
		if (ret < 0) {
			dev_err(sdev->dev,
				"error: could not startup SoundWire links\n");
			return ret;
		}
	}

	hda_sdw_int_enable(sdev, true);

	/* re-enable clock gating and power gating */
	return hda_dsp_ctrl_clock_power_gating(sdev, true);
}
