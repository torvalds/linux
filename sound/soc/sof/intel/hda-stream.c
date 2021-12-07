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
 * Hardware interface for generic Intel audio DSP HDA IP
 */

#include <linux/pm_runtime.h>
#include <sound/hdaudio_ext.h>
#include <sound/hda_register.h>
#include <sound/sof.h>
#include "../ops.h"
#include "../sof-audio.h"
#include "hda.h"

#define HDA_LTRP_GB_VALUE_US	95

static inline const char *hda_hstream_direction_str(struct hdac_stream *hstream)
{
	if (hstream->direction == SNDRV_PCM_STREAM_PLAYBACK)
		return "Playback";
	else
		return "Capture";
}

static char *hda_hstream_dbg_get_stream_info_str(struct hdac_stream *hstream)
{
	struct snd_soc_pcm_runtime *rtd;

	if (hstream->substream)
		rtd = asoc_substream_to_rtd(hstream->substream);
	else if (hstream->cstream)
		rtd = hstream->cstream->private_data;
	else
		/* Non audio DMA user, like dma-trace */
		return kasprintf(GFP_KERNEL, "-- (%s, stream_tag: %u)",
				 hda_hstream_direction_str(hstream),
				 hstream->stream_tag);

	return kasprintf(GFP_KERNEL, "dai_link \"%s\" (%s, stream_tag: %u)",
			 rtd->dai_link->name, hda_hstream_direction_str(hstream),
			 hstream->stream_tag);
}

/*
 * set up one of BDL entries for a stream
 */
static int hda_setup_bdle(struct snd_sof_dev *sdev,
			  struct snd_dma_buffer *dmab,
			  struct hdac_stream *stream,
			  struct sof_intel_dsp_bdl **bdlp,
			  int offset, int size, int ioc)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct sof_intel_dsp_bdl *bdl = *bdlp;

	while (size > 0) {
		dma_addr_t addr;
		int chunk;

		if (stream->frags >= HDA_DSP_MAX_BDL_ENTRIES) {
			dev_err(sdev->dev, "error: stream frags exceeded\n");
			return -EINVAL;
		}

		addr = snd_sgbuf_get_addr(dmab, offset);
		/* program BDL addr */
		bdl->addr_l = cpu_to_le32(lower_32_bits(addr));
		bdl->addr_h = cpu_to_le32(upper_32_bits(addr));
		/* program BDL size */
		chunk = snd_sgbuf_get_chunk_size(dmab, offset, size);
		/* one BDLE should not cross 4K boundary */
		if (bus->align_bdle_4k) {
			u32 remain = 0x1000 - (offset & 0xfff);

			if (chunk > remain)
				chunk = remain;
		}
		bdl->size = cpu_to_le32(chunk);
		/* only program IOC when the whole segment is processed */
		size -= chunk;
		bdl->ioc = (size || !ioc) ? 0 : cpu_to_le32(0x01);
		bdl++;
		stream->frags++;
		offset += chunk;

		dev_vdbg(sdev->dev, "bdl, frags:%d, chunk size:0x%x;\n",
			 stream->frags, chunk);
	}

	*bdlp = bdl;
	return offset;
}

/*
 * set up Buffer Descriptor List (BDL) for host memory transfer
 * BDL describes the location of the individual buffers and is little endian.
 */
int hda_dsp_stream_setup_bdl(struct snd_sof_dev *sdev,
			     struct snd_dma_buffer *dmab,
			     struct hdac_stream *stream)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	struct sof_intel_dsp_bdl *bdl;
	int i, offset, period_bytes, periods;
	int remain, ioc;

	period_bytes = stream->period_bytes;
	dev_dbg(sdev->dev, "%s: period_bytes:0x%x\n", __func__, period_bytes);
	if (!period_bytes)
		period_bytes = stream->bufsize;

	periods = stream->bufsize / period_bytes;

	dev_dbg(sdev->dev, "%s: periods:%d\n", __func__, periods);

	remain = stream->bufsize % period_bytes;
	if (remain)
		periods++;

	/* program the initial BDL entries */
	bdl = (struct sof_intel_dsp_bdl *)stream->bdl.area;
	offset = 0;
	stream->frags = 0;

	/*
	 * set IOC if don't use position IPC
	 * and period_wakeup needed.
	 */
	ioc = hda->no_ipc_position ?
	      !stream->no_period_wakeup : 0;

	for (i = 0; i < periods; i++) {
		if (i == (periods - 1) && remain)
			/* set the last small entry */
			offset = hda_setup_bdle(sdev, dmab,
						stream, &bdl, offset,
						remain, 0);
		else
			offset = hda_setup_bdle(sdev, dmab,
						stream, &bdl, offset,
						period_bytes, ioc);
	}

	return offset;
}

int hda_dsp_stream_spib_config(struct snd_sof_dev *sdev,
			       struct hdac_ext_stream *stream,
			       int enable, u32 size)
{
	struct hdac_stream *hstream = &stream->hstream;
	u32 mask;

	if (!sdev->bar[HDA_DSP_SPIB_BAR]) {
		dev_err(sdev->dev, "error: address of spib capability is NULL\n");
		return -EINVAL;
	}

	mask = (1 << hstream->index);

	/* enable/disable SPIB for the stream */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_SPIB_BAR,
				SOF_HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL, mask,
				enable << hstream->index);

	/* set the SPIB value */
	sof_io_write(sdev, stream->spib_addr, size);

	return 0;
}

/* get next unused stream */
struct hdac_ext_stream *
hda_dsp_stream_get(struct snd_sof_dev *sdev, int direction, u32 flags)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct sof_intel_hda_stream *hda_stream;
	struct hdac_ext_stream *stream = NULL;
	struct hdac_stream *s;

	spin_lock_irq(&bus->reg_lock);

	/* get an unused stream */
	list_for_each_entry(s, &bus->stream_list, list) {
		if (s->direction == direction && !s->opened) {
			stream = stream_to_hdac_ext_stream(s);
			hda_stream = container_of(stream,
						  struct sof_intel_hda_stream,
						  hda_stream);
			/* check if the host DMA channel is reserved */
			if (hda_stream->host_reserved)
				continue;

			s->opened = true;
			break;
		}
	}

	spin_unlock_irq(&bus->reg_lock);

	/* stream found ? */
	if (!stream) {
		dev_err(sdev->dev, "error: no free %s streams\n",
			direction == SNDRV_PCM_STREAM_PLAYBACK ?
			"playback" : "capture");
		return stream;
	}

	hda_stream->flags = flags;

	/*
	 * Prevent DMI Link L1 entry for streams that don't support it.
	 * Workaround to address a known issue with host DMA that results
	 * in xruns during pause/release in capture scenarios.
	 */
	if (!(flags & SOF_HDA_STREAM_DMI_L1_COMPATIBLE))
		snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
					HDA_VS_INTEL_EM2,
					HDA_VS_INTEL_EM2_L1SEN, 0);

	return stream;
}

/* free a stream */
int hda_dsp_stream_put(struct snd_sof_dev *sdev, int direction, int stream_tag)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct sof_intel_hda_stream *hda_stream;
	struct hdac_ext_stream *stream;
	struct hdac_stream *s;
	bool dmi_l1_enable = true;
	bool found = false;

	spin_lock_irq(&bus->reg_lock);

	/*
	 * close stream matching the stream tag and check if there are any open streams
	 * that are DMI L1 incompatible.
	 */
	list_for_each_entry(s, &bus->stream_list, list) {
		stream = stream_to_hdac_ext_stream(s);
		hda_stream = container_of(stream, struct sof_intel_hda_stream, hda_stream);

		if (!s->opened)
			continue;

		if (s->direction == direction && s->stream_tag == stream_tag) {
			s->opened = false;
			found = true;
		} else if (!(hda_stream->flags & SOF_HDA_STREAM_DMI_L1_COMPATIBLE)) {
			dmi_l1_enable = false;
		}
	}

	spin_unlock_irq(&bus->reg_lock);

	/* Enable DMI L1 if permitted */
	if (dmi_l1_enable)
		snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, HDA_VS_INTEL_EM2,
					HDA_VS_INTEL_EM2_L1SEN, HDA_VS_INTEL_EM2_L1SEN);

	if (!found) {
		dev_dbg(sdev->dev, "%s: stream_tag %d not opened!\n",
			__func__, stream_tag);
		return -ENODEV;
	}

	return 0;
}

static int hda_dsp_stream_reset(struct snd_sof_dev *sdev, struct hdac_stream *hstream)
{
	int sd_offset = SOF_STREAM_SD_OFFSET(hstream);
	int timeout = HDA_DSP_STREAM_RESET_TIMEOUT;
	u32 val;

	/* enter stream reset */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, sd_offset, SOF_STREAM_SD_OFFSET_CRST,
				SOF_STREAM_SD_OFFSET_CRST);
	do {
		val = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, sd_offset);
		if (val & SOF_STREAM_SD_OFFSET_CRST)
			break;
	} while (--timeout);
	if (timeout == 0) {
		dev_err(sdev->dev, "timeout waiting for stream reset\n");
		return -ETIMEDOUT;
	}

	timeout = HDA_DSP_STREAM_RESET_TIMEOUT;

	/* exit stream reset and wait to read a zero before reading any other register */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, sd_offset, SOF_STREAM_SD_OFFSET_CRST, 0x0);

	/* wait for hardware to report that stream is out of reset */
	udelay(3);
	do {
		val = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, sd_offset);
		if ((val & SOF_STREAM_SD_OFFSET_CRST) == 0)
			break;
	} while (--timeout);
	if (timeout == 0) {
		dev_err(sdev->dev, "timeout waiting for stream to exit reset\n");
		return -ETIMEDOUT;
	}

	return 0;
}

int hda_dsp_stream_trigger(struct snd_sof_dev *sdev,
			   struct hdac_ext_stream *stream, int cmd)
{
	struct hdac_stream *hstream = &stream->hstream;
	int sd_offset = SOF_STREAM_SD_OFFSET(hstream);
	u32 dma_start = SOF_HDA_SD_CTL_DMA_START;
	int ret = 0;
	u32 run;

	/* cmd must be for audio stream */
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
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

		ret = snd_sof_dsp_read_poll_timeout(sdev,
					HDA_DSP_HDA_BAR,
					sd_offset, run,
					((run &	dma_start) == dma_start),
					HDA_DSP_REG_POLL_INTERVAL_US,
					HDA_DSP_STREAM_RUN_TIMEOUT);

		if (ret >= 0)
			hstream->running = true;

		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
					sd_offset,
					SOF_HDA_SD_CTL_DMA_START |
					SOF_HDA_CL_DMA_SD_INT_MASK, 0x0);

		ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_HDA_BAR,
						sd_offset, run,
						!(run &	dma_start),
						HDA_DSP_REG_POLL_INTERVAL_US,
						HDA_DSP_STREAM_RUN_TIMEOUT);

		if (ret >= 0) {
			snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
					  sd_offset + SOF_HDA_ADSP_REG_CL_SD_STS,
					  SOF_HDA_CL_DMA_SD_INT_MASK);

			hstream->running = false;
			snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
						SOF_HDA_INTCTL,
						1 << hstream->index, 0x0);
		}
		break;
	default:
		dev_err(sdev->dev, "error: unknown command: %d\n", cmd);
		return -EINVAL;
	}

	if (ret < 0) {
		char *stream_name = hda_hstream_dbg_get_stream_info_str(hstream);

		dev_err(sdev->dev,
			"%s: cmd %d on %s: timeout on STREAM_SD_OFFSET read\n",
			__func__, cmd, stream_name ? stream_name : "unknown stream");
		kfree(stream_name);
	}

	return ret;
}

/* minimal recommended programming for ICCMAX stream */
int hda_dsp_iccmax_stream_hw_params(struct snd_sof_dev *sdev, struct hdac_ext_stream *stream,
				    struct snd_dma_buffer *dmab,
				    struct snd_pcm_hw_params *params)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct hdac_stream *hstream = &stream->hstream;
	int sd_offset = SOF_STREAM_SD_OFFSET(hstream);
	int ret;
	u32 mask = 0x1 << hstream->index;

	if (!stream) {
		dev_err(sdev->dev, "error: no stream available\n");
		return -ENODEV;
	}

	if (hstream->posbuf)
		*hstream->posbuf = 0;

	/* reset BDL address */
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPL,
			  0x0);
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPU,
			  0x0);

	hstream->frags = 0;

	ret = hda_dsp_stream_setup_bdl(sdev, dmab, hstream);
	if (ret < 0) {
		dev_err(sdev->dev, "error: set up of BDL failed\n");
		return ret;
	}

	/* program BDL address */
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPL,
			  (u32)hstream->bdl.addr);
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPU,
			  upper_32_bits(hstream->bdl.addr));

	/* program cyclic buffer length */
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_CBL,
			  hstream->bufsize);

	/* program last valid index */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
				sd_offset + SOF_HDA_ADSP_REG_CL_SD_LVI,
				0xffff, (hstream->frags - 1));

	/* decouple host and link DMA, enable DSP features */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_PP_BAR, SOF_HDA_REG_PP_PPCTL,
				mask, mask);

	/* Follow HW recommendation to set the guardband value to 95us during FW boot */
	snd_hdac_chip_updateb(bus, VS_LTRP, HDA_VS_INTEL_LTRP_GB_MASK, HDA_LTRP_GB_VALUE_US);

	/* start DMA */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, sd_offset,
				SOF_HDA_SD_CTL_DMA_START, SOF_HDA_SD_CTL_DMA_START);

	return 0;
}

/*
 * prepare for common hdac registers settings, for both code loader
 * and normal stream.
 */
int hda_dsp_stream_hw_params(struct snd_sof_dev *sdev,
			     struct hdac_ext_stream *stream,
			     struct snd_dma_buffer *dmab,
			     struct snd_pcm_hw_params *params)
{
	const struct sof_intel_dsp_desc *chip = get_chip_info(sdev->pdata);
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct hdac_stream *hstream = &stream->hstream;
	int sd_offset = SOF_STREAM_SD_OFFSET(hstream);
	int ret;
	u32 dma_start = SOF_HDA_SD_CTL_DMA_START;
	u32 mask;
	u32 run;

	if (!stream) {
		dev_err(sdev->dev, "error: no stream available\n");
		return -ENODEV;
	}

	/* decouple host and link DMA */
	mask = 0x1 << hstream->index;
	snd_sof_dsp_update_bits(sdev, HDA_DSP_PP_BAR, SOF_HDA_REG_PP_PPCTL,
				mask, mask);

	if (!dmab) {
		dev_err(sdev->dev, "error: no dma buffer allocated!\n");
		return -ENODEV;
	}

	/* clear stream status */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, sd_offset,
				SOF_HDA_CL_DMA_SD_INT_MASK |
				SOF_HDA_SD_CTL_DMA_START, 0);

	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_HDA_BAR,
					    sd_offset, run,
					    !(run & dma_start),
					    HDA_DSP_REG_POLL_INTERVAL_US,
					    HDA_DSP_STREAM_RUN_TIMEOUT);

	if (ret < 0) {
		char *stream_name = hda_hstream_dbg_get_stream_info_str(hstream);

		dev_err(sdev->dev,
			"%s: on %s: timeout on STREAM_SD_OFFSET read1\n",
			__func__, stream_name ? stream_name : "unknown stream");
		kfree(stream_name);
		return ret;
	}

	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
				sd_offset + SOF_HDA_ADSP_REG_CL_SD_STS,
				SOF_HDA_CL_DMA_SD_INT_MASK,
				SOF_HDA_CL_DMA_SD_INT_MASK);

	/* stream reset */
	ret = hda_dsp_stream_reset(sdev, hstream);
	if (ret < 0)
		return ret;

	if (hstream->posbuf)
		*hstream->posbuf = 0;

	/* reset BDL address */
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPL,
			  0x0);
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPU,
			  0x0);

	/* clear stream status */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, sd_offset,
				SOF_HDA_CL_DMA_SD_INT_MASK |
				SOF_HDA_SD_CTL_DMA_START, 0);

	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_HDA_BAR,
					    sd_offset, run,
					    !(run & dma_start),
					    HDA_DSP_REG_POLL_INTERVAL_US,
					    HDA_DSP_STREAM_RUN_TIMEOUT);

	if (ret < 0) {
		char *stream_name = hda_hstream_dbg_get_stream_info_str(hstream);

		dev_err(sdev->dev,
			"%s: on %s: timeout on STREAM_SD_OFFSET read1\n",
			__func__, stream_name ? stream_name : "unknown stream");
		kfree(stream_name);
		return ret;
	}

	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
				sd_offset + SOF_HDA_ADSP_REG_CL_SD_STS,
				SOF_HDA_CL_DMA_SD_INT_MASK,
				SOF_HDA_CL_DMA_SD_INT_MASK);

	hstream->frags = 0;

	ret = hda_dsp_stream_setup_bdl(sdev, dmab, hstream);
	if (ret < 0) {
		dev_err(sdev->dev, "error: set up of BDL failed\n");
		return ret;
	}

	/* program stream tag to set up stream descriptor for DMA */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, sd_offset,
				SOF_HDA_CL_SD_CTL_STREAM_TAG_MASK,
				hstream->stream_tag <<
				SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT);

	/* program cyclic buffer length */
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_CBL,
			  hstream->bufsize);

	/*
	 * Recommended hardware programming sequence for HDAudio DMA format
	 * on earlier platforms - this is not needed on newer platforms
	 *
	 * 1. Put DMA into coupled mode by clearing PPCTL.PROCEN bit
	 *    for corresponding stream index before the time of writing
	 *    format to SDxFMT register.
	 * 2. Write SDxFMT
	 * 3. Set PPCTL.PROCEN bit for corresponding stream index to
	 *    enable decoupled mode
	 */

	if (chip->quirks & SOF_INTEL_PROCEN_FMT_QUIRK) {
		/* couple host and link DMA, disable DSP features */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_PP_BAR, SOF_HDA_REG_PP_PPCTL,
					mask, 0);
	}

	/* program stream format */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
				sd_offset +
				SOF_HDA_ADSP_REG_CL_SD_FORMAT,
				0xffff, hstream->format_val);

	if (chip->quirks & SOF_INTEL_PROCEN_FMT_QUIRK) {
		/* decouple host and link DMA, enable DSP features */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_PP_BAR, SOF_HDA_REG_PP_PPCTL,
					mask, mask);
	}

	/* program last valid index */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
				sd_offset + SOF_HDA_ADSP_REG_CL_SD_LVI,
				0xffff, (hstream->frags - 1));

	/* program BDL address */
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPL,
			  (u32)hstream->bdl.addr);
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPU,
			  upper_32_bits(hstream->bdl.addr));

	/* enable position buffer */
	if (!(snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, SOF_HDA_ADSP_DPLBASE)
				& SOF_HDA_ADSP_DPLBASE_ENABLE)) {
		snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR, SOF_HDA_ADSP_DPUBASE,
				  upper_32_bits(bus->posbuf.addr));
		snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR, SOF_HDA_ADSP_DPLBASE,
				  (u32)bus->posbuf.addr |
				  SOF_HDA_ADSP_DPLBASE_ENABLE);
	}

	/* set interrupt enable bits */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, sd_offset,
				SOF_HDA_CL_DMA_SD_INT_MASK,
				SOF_HDA_CL_DMA_SD_INT_MASK);

	/* read FIFO size */
	if (hstream->direction == SNDRV_PCM_STREAM_PLAYBACK) {
		hstream->fifo_size =
			snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR,
					 sd_offset +
					 SOF_HDA_ADSP_REG_CL_SD_FIFOSIZE);
		hstream->fifo_size &= 0xffff;
		hstream->fifo_size += 1;
	} else {
		hstream->fifo_size = 0;
	}

	return ret;
}

int hda_dsp_stream_hw_free(struct snd_sof_dev *sdev,
			   struct snd_pcm_substream *substream)
{
	struct hdac_stream *stream = substream->runtime->private_data;
	struct hdac_ext_stream *link_dev = container_of(stream,
							struct hdac_ext_stream,
							hstream);
	struct hdac_bus *bus = sof_to_bus(sdev);
	u32 mask = 0x1 << stream->index;
	int ret;

	ret = hda_dsp_stream_reset(sdev, stream);
	if (ret < 0)
		return ret;

	spin_lock_irq(&bus->reg_lock);
	/* couple host and link DMA if link DMA channel is idle */
	if (!link_dev->link_locked)
		snd_sof_dsp_update_bits(sdev, HDA_DSP_PP_BAR,
					SOF_HDA_REG_PP_PPCTL, mask, 0);
	spin_unlock_irq(&bus->reg_lock);

	hda_dsp_stream_spib_config(sdev, link_dev, HDA_DSP_SPIB_DISABLE, 0);

	stream->substream = NULL;

	return 0;
}

bool hda_dsp_check_stream_irq(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	bool ret = false;
	u32 status;

	/* The function can be called at irq thread, so use spin_lock_irq */
	spin_lock_irq(&bus->reg_lock);

	status = snd_hdac_chip_readl(bus, INTSTS);
	dev_vdbg(bus->dev, "stream irq, INTSTS status: 0x%x\n", status);

	/* if Register inaccessible, ignore it.*/
	if (status != 0xffffffff)
		ret = true;

	spin_unlock_irq(&bus->reg_lock);

	return ret;
}

static void
hda_dsp_set_bytes_transferred(struct hdac_stream *hstream, u64 buffer_size)
{
	u64 prev_pos, pos, num_bytes;

	div64_u64_rem(hstream->curr_pos, buffer_size, &prev_pos);
	pos = snd_hdac_stream_get_pos_posbuf(hstream);

	if (pos < prev_pos)
		num_bytes = (buffer_size - prev_pos) +  pos;
	else
		num_bytes = pos - prev_pos;

	hstream->curr_pos += num_bytes;
}

static bool hda_dsp_stream_check(struct hdac_bus *bus, u32 status)
{
	struct sof_intel_hda_dev *sof_hda = bus_to_sof_hda(bus);
	struct hdac_stream *s;
	bool active = false;
	u32 sd_status;

	list_for_each_entry(s, &bus->stream_list, list) {
		if (status & BIT(s->index) && s->opened) {
			sd_status = snd_hdac_stream_readb(s, SD_STS);

			dev_vdbg(bus->dev, "stream %d status 0x%x\n",
				 s->index, sd_status);

			snd_hdac_stream_writeb(s, SD_STS, sd_status);

			active = true;
			if ((!s->substream && !s->cstream) ||
			    !s->running ||
			    (sd_status & SOF_HDA_CL_DMA_SD_INT_COMPLETE) == 0)
				continue;

			/* Inform ALSA only in case not do that with IPC */
			if (s->substream && sof_hda->no_ipc_position) {
				snd_sof_pcm_period_elapsed(s->substream);
			} else if (s->cstream) {
				hda_dsp_set_bytes_transferred(s,
					s->cstream->runtime->buffer_size);
				snd_compr_fragment_elapsed(s->cstream);
			}
		}
	}

	return active;
}

irqreturn_t hda_dsp_stream_threaded_handler(int irq, void *context)
{
	struct snd_sof_dev *sdev = context;
	struct hdac_bus *bus = sof_to_bus(sdev);
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	u32 rirb_status;
#endif
	bool active;
	u32 status;
	int i;

	/*
	 * Loop 10 times to handle missed interrupts caused by
	 * unsolicited responses from the codec
	 */
	for (i = 0, active = true; i < 10 && active; i++) {
		spin_lock_irq(&bus->reg_lock);

		status = snd_hdac_chip_readl(bus, INTSTS);

		/* check streams */
		active = hda_dsp_stream_check(bus, status);

		/* check and clear RIRB interrupt */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
		if (status & AZX_INT_CTRL_EN) {
			rirb_status = snd_hdac_chip_readb(bus, RIRBSTS);
			if (rirb_status & RIRB_INT_MASK) {
				/*
				 * Clearing the interrupt status here ensures
				 * that no interrupt gets masked after the RIRB
				 * wp is read in snd_hdac_bus_update_rirb.
				 */
				snd_hdac_chip_writeb(bus, RIRBSTS,
						     RIRB_INT_MASK);
				active = true;
				if (rirb_status & RIRB_INT_RESPONSE)
					snd_hdac_bus_update_rirb(bus);
			}
		}
#endif
		spin_unlock_irq(&bus->reg_lock);
	}

	return IRQ_HANDLED;
}

int hda_dsp_stream_init(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct hdac_ext_stream *stream;
	struct hdac_stream *hstream;
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	struct sof_intel_hda_dev *sof_hda = bus_to_sof_hda(bus);
	int sd_offset;
	int i, num_playback, num_capture, num_total, ret;
	u32 gcap;

	gcap = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, SOF_HDA_GCAP);
	dev_dbg(sdev->dev, "hda global caps = 0x%x\n", gcap);

	/* get stream count from GCAP */
	num_capture = (gcap >> 8) & 0x0f;
	num_playback = (gcap >> 12) & 0x0f;
	num_total = num_playback + num_capture;

	dev_dbg(sdev->dev, "detected %d playback and %d capture streams\n",
		num_playback, num_capture);

	if (num_playback >= SOF_HDA_PLAYBACK_STREAMS) {
		dev_err(sdev->dev, "error: too many playback streams %d\n",
			num_playback);
		return -EINVAL;
	}

	if (num_capture >= SOF_HDA_CAPTURE_STREAMS) {
		dev_err(sdev->dev, "error: too many capture streams %d\n",
			num_playback);
		return -EINVAL;
	}

	/*
	 * mem alloc for the position buffer
	 * TODO: check position buffer update
	 */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, &pci->dev,
				  SOF_HDA_DPIB_ENTRY_SIZE * num_total,
				  &bus->posbuf);
	if (ret < 0) {
		dev_err(sdev->dev, "error: posbuffer dma alloc failed\n");
		return -ENOMEM;
	}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* mem alloc for the CORB/RIRB ringbuffers */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, &pci->dev,
				  PAGE_SIZE, &bus->rb);
	if (ret < 0) {
		dev_err(sdev->dev, "error: RB alloc failed\n");
		return -ENOMEM;
	}
#endif

	/* create capture streams */
	for (i = 0; i < num_capture; i++) {
		struct sof_intel_hda_stream *hda_stream;

		hda_stream = devm_kzalloc(sdev->dev, sizeof(*hda_stream),
					  GFP_KERNEL);
		if (!hda_stream)
			return -ENOMEM;

		hda_stream->sdev = sdev;

		stream = &hda_stream->hda_stream;

		stream->pphc_addr = sdev->bar[HDA_DSP_PP_BAR] +
			SOF_HDA_PPHC_BASE + SOF_HDA_PPHC_INTERVAL * i;

		stream->pplc_addr = sdev->bar[HDA_DSP_PP_BAR] +
			SOF_HDA_PPLC_BASE + SOF_HDA_PPLC_MULTI * num_total +
			SOF_HDA_PPLC_INTERVAL * i;

		/* do we support SPIB */
		if (sdev->bar[HDA_DSP_SPIB_BAR]) {
			stream->spib_addr = sdev->bar[HDA_DSP_SPIB_BAR] +
				SOF_HDA_SPIB_BASE + SOF_HDA_SPIB_INTERVAL * i +
				SOF_HDA_SPIB_SPIB;

			stream->fifo_addr = sdev->bar[HDA_DSP_SPIB_BAR] +
				SOF_HDA_SPIB_BASE + SOF_HDA_SPIB_INTERVAL * i +
				SOF_HDA_SPIB_MAXFIFO;
		}

		hstream = &stream->hstream;
		hstream->bus = bus;
		hstream->sd_int_sta_mask = 1 << i;
		hstream->index = i;
		sd_offset = SOF_STREAM_SD_OFFSET(hstream);
		hstream->sd_addr = sdev->bar[HDA_DSP_HDA_BAR] + sd_offset;
		hstream->stream_tag = i + 1;
		hstream->opened = false;
		hstream->running = false;
		hstream->direction = SNDRV_PCM_STREAM_CAPTURE;

		/* memory alloc for stream BDL */
		ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, &pci->dev,
					  HDA_DSP_BDL_SIZE, &hstream->bdl);
		if (ret < 0) {
			dev_err(sdev->dev, "error: stream bdl dma alloc failed\n");
			return -ENOMEM;
		}
		hstream->posbuf = (__le32 *)(bus->posbuf.area +
			(hstream->index) * 8);

		list_add_tail(&hstream->list, &bus->stream_list);
	}

	/* create playback streams */
	for (i = num_capture; i < num_total; i++) {
		struct sof_intel_hda_stream *hda_stream;

		hda_stream = devm_kzalloc(sdev->dev, sizeof(*hda_stream),
					  GFP_KERNEL);
		if (!hda_stream)
			return -ENOMEM;

		hda_stream->sdev = sdev;

		stream = &hda_stream->hda_stream;

		/* we always have DSP support */
		stream->pphc_addr = sdev->bar[HDA_DSP_PP_BAR] +
			SOF_HDA_PPHC_BASE + SOF_HDA_PPHC_INTERVAL * i;

		stream->pplc_addr = sdev->bar[HDA_DSP_PP_BAR] +
			SOF_HDA_PPLC_BASE + SOF_HDA_PPLC_MULTI * num_total +
			SOF_HDA_PPLC_INTERVAL * i;

		/* do we support SPIB */
		if (sdev->bar[HDA_DSP_SPIB_BAR]) {
			stream->spib_addr = sdev->bar[HDA_DSP_SPIB_BAR] +
				SOF_HDA_SPIB_BASE + SOF_HDA_SPIB_INTERVAL * i +
				SOF_HDA_SPIB_SPIB;

			stream->fifo_addr = sdev->bar[HDA_DSP_SPIB_BAR] +
				SOF_HDA_SPIB_BASE + SOF_HDA_SPIB_INTERVAL * i +
				SOF_HDA_SPIB_MAXFIFO;
		}

		hstream = &stream->hstream;
		hstream->bus = bus;
		hstream->sd_int_sta_mask = 1 << i;
		hstream->index = i;
		sd_offset = SOF_STREAM_SD_OFFSET(hstream);
		hstream->sd_addr = sdev->bar[HDA_DSP_HDA_BAR] + sd_offset;
		hstream->stream_tag = i - num_capture + 1;
		hstream->opened = false;
		hstream->running = false;
		hstream->direction = SNDRV_PCM_STREAM_PLAYBACK;

		/* mem alloc for stream BDL */
		ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, &pci->dev,
					  HDA_DSP_BDL_SIZE, &hstream->bdl);
		if (ret < 0) {
			dev_err(sdev->dev, "error: stream bdl dma alloc failed\n");
			return -ENOMEM;
		}

		hstream->posbuf = (__le32 *)(bus->posbuf.area +
			(hstream->index) * 8);

		list_add_tail(&hstream->list, &bus->stream_list);
	}

	/* store total stream count (playback + capture) from GCAP */
	sof_hda->stream_max = num_total;

	return 0;
}

void hda_dsp_stream_free(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct hdac_stream *s, *_s;
	struct hdac_ext_stream *stream;
	struct sof_intel_hda_stream *hda_stream;

	/* free position buffer */
	if (bus->posbuf.area)
		snd_dma_free_pages(&bus->posbuf);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* free position buffer */
	if (bus->rb.area)
		snd_dma_free_pages(&bus->rb);
#endif

	list_for_each_entry_safe(s, _s, &bus->stream_list, list) {
		/* TODO: decouple */

		/* free bdl buffer */
		if (s->bdl.area)
			snd_dma_free_pages(&s->bdl);
		list_del(&s->list);
		stream = stream_to_hdac_ext_stream(s);
		hda_stream = container_of(stream, struct sof_intel_hda_stream,
					  hda_stream);
		devm_kfree(sdev->dev, hda_stream);
	}
}
