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
 * Hardware interface for generic Intel audio DSP HDA IP
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

/*
 * set up Buffer Descriptor List (BDL) for host memory transfer
 * BDL describes the location of the individual buffers and is little endian.
 */
int hda_dsp_stream_setup_bdl(struct snd_sof_dev *sdev,
			     struct snd_dma_buffer *dmab,
			     struct sof_intel_hda_stream *stream,
			     struct sof_intel_dsp_bdl *bdl, int size,
			     struct snd_pcm_hw_params *params)
{
	int offset = 0;
	int chunk = PAGE_SIZE, entry_size;
	dma_addr_t addr;

	if (stream->substream && params) {
		chunk = params_period_bytes(params);
		dev_dbg(sdev->dev, "period_bytes:0x%x\n", chunk);
	}

	while (size > 0) {
		if (stream->frags >= HDA_DSP_MAX_BDL_ENTRIES) {
			dev_err(sdev->dev, "error: stream frags exceeded\n");
			return -EINVAL;
		}

		addr = snd_sgbuf_get_addr(dmab, offset);

		/* program BDL addr */
		bdl->addr_l = lower_32_bits(addr);
		bdl->addr_h = upper_32_bits(addr);

		entry_size = size > chunk ? chunk : size;

		/* program BDL size */
		bdl->size = snd_sgbuf_get_chunk_size(dmab, offset, entry_size);

		/* program the IOC to enable interrupt
		 * when the whole fragment is processed
		 */
		size -= entry_size;
		if (size)
			bdl->ioc = 0;
		else
			bdl->ioc = 1;

		stream->frags++;
		offset += bdl->size;

		dev_vdbg(sdev->dev, "bdl, frags:%d, entry size:0x%x;\n",
			 stream->frags, entry_size);

		bdl++;
	}

	return offset;
}

int hda_dsp_stream_spib_config(struct snd_sof_dev *sdev,
			       struct sof_intel_hda_stream *stream,
			       int enable, u32 size)
{
	u32 mask = 0;

	if (!sdev->bar[HDA_DSP_SPIB_BAR]) {
		dev_err(sdev->dev, "error: address of spib capability is NULL\n");
		return -EINVAL;
	}

	mask |= (1 << stream->index);

	/* enable/disable SPIB for the stream */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_SPIB_BAR,
				SOF_HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL, mask,
				enable << stream->index);

	/* set the SPIB value */
	hda_dsp_write(sdev, stream->spib_addr, size);

	return 0;
}

/* get next unused playback stream */
struct sof_intel_hda_stream *
hda_dsp_stream_get_pstream(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hdev = sdev->hda;
	struct sof_intel_hda_stream *stream = NULL;
	int i;

	/* get an unused playback stream */
	for (i = 0; i < hdev->num_playback; i++) {
		if (!hdev->pstream[i].open) {
			hdev->pstream[i].open = true;
			stream = &hdev->pstream[i];
			break;
		}
	}

	/* stream found ? */
	if (!stream)
		dev_err(sdev->dev, "error: no free playback streams\n");

	return stream;
}

/* get next unused capture stream */
struct sof_intel_hda_stream *
hda_dsp_stream_get_cstream(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hdev = sdev->hda;
	struct sof_intel_hda_stream *stream = NULL;
	int i;

	/* get an unused capture stream */
	for (i = 0; i < hdev->num_capture; i++) {
		if (!hdev->cstream[i].open) {
			hdev->cstream[i].open = true;
			stream = &hdev->cstream[i];
			break;
		}
	}

	/* stream found ? */
	if (!stream)
		dev_err(sdev->dev, "error: no free capture streams\n");

	return stream;
}

/* free playback stream */
int hda_dsp_stream_put_pstream(struct snd_sof_dev *sdev, int tag)
{
	struct sof_intel_hda_dev *hdev = sdev->hda;
	int i;

	/* find used playback stream */
	for (i = 0; i < hdev->num_playback; i++) {
		if (hdev->pstream[i].open &&
		    hdev->pstream[i].tag == tag) {
			hdev->pstream[i].open = false;
			return 0;
		}
	}

	dev_dbg(sdev->dev, "tag %d not opened!\n", tag);
	return -ENODEV;
}

/* free capture stream */
int hda_dsp_stream_put_cstream(struct snd_sof_dev *sdev, int tag)
{
	struct sof_intel_hda_dev *hdev = sdev->hda;
	int i;

	/* find used capture stream */
	for (i = 0; i < hdev->num_capture; i++) {
		if (hdev->cstream[i].open &&
		    hdev->cstream[i].tag == tag) {
			hdev->cstream[i].open = false;
			return 0;
		}
	}

	dev_dbg(sdev->dev, "tag %d not opened!\n", tag);
	return -ENODEV;
}

int hda_dsp_stream_trigger(struct snd_sof_dev *sdev,
			   struct sof_intel_hda_stream *stream, int cmd)
{
	/* cmd must be for audio stream */
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_START:
		snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, SOF_HDA_INTCTL,
					1 << stream->index,
					1 << stream->index);

		snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
					stream->sd_offset,
					SOF_HDA_SD_CTL_DMA_START |
					SOF_HDA_CL_DMA_SD_INT_MASK,
					SOF_HDA_SD_CTL_DMA_START |
					SOF_HDA_CL_DMA_SD_INT_MASK);

		stream->running = true;
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
					stream->sd_offset,
					SOF_HDA_SD_CTL_DMA_START |
					SOF_HDA_CL_DMA_SD_INT_MASK, 0x0);

		snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR, stream->sd_offset +
				  SOF_HDA_ADSP_REG_CL_SD_STS,
				  SOF_HDA_CL_DMA_SD_INT_MASK);

		stream->running = false;
		snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, SOF_HDA_INTCTL,
					1 << stream->index, 0x0);
		break;
	default:
		dev_err(sdev->dev, "error: unknown command: %d\n", cmd);
		return -EINVAL;
	}

	return 0;
}

/*
 * prepare for common hdac registers settings, for both code loader
 * and normal stream.
 */
int hda_dsp_stream_hw_params(struct snd_sof_dev *sdev,
			     struct sof_intel_hda_stream *stream,
			     struct snd_dma_buffer *dmab,
			     struct snd_pcm_hw_params *params)
{
	struct sof_intel_hda_dev *hdev = sdev->hda;
	struct sof_intel_dsp_bdl *bdl;
	int ret, timeout = HDA_DSP_STREAM_RESET_TIMEOUT;
	u32 val, mask;

	if (!stream) {
		dev_err(sdev->dev, "error: no stream available\n");
		return -ENODEV;
	}

	/* decouple host and link DMA */
	mask = 0x1 << stream->index;
	snd_sof_dsp_update_bits(sdev, HDA_DSP_PP_BAR, SOF_HDA_REG_PP_PPCTL,
				mask, mask);

	if (!dmab) {
		dev_err(sdev->dev, "error: no dma buffer allocated!\n");
		return -ENODEV;
	}

	/* clear stream status */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, stream->sd_offset,
				SOF_HDA_CL_DMA_SD_INT_MASK |
				SOF_HDA_SD_CTL_DMA_START, 0);
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
				stream->sd_offset + SOF_HDA_ADSP_REG_CL_SD_STS,
				SOF_HDA_CL_DMA_SD_INT_MASK,
				SOF_HDA_CL_DMA_SD_INT_MASK);

	/* stream reset */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, stream->sd_offset, 0x1,
				0x1);
	udelay(3);
	do {
		val = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR,
				       stream->sd_offset);
		if (val & 0x1)
			break;
	} while (--timeout);
	if (timeout == 0) {
		dev_err(sdev->dev, "error: stream reset failed\n");
		return -ETIMEDOUT;
	}

	timeout = HDA_DSP_STREAM_RESET_TIMEOUT;
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, stream->sd_offset, 0x1,
				0x0);

	/* wait for hardware to report that stream is out of reset */
	udelay(3);
	do {
		val = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR,
				       stream->sd_offset);
		if ((val & 0x1) == 0)
			break;
	} while (--timeout);
	if (timeout == 0) {
		dev_err(sdev->dev, "error: timeout waiting for stream reset\n");
		return -ETIMEDOUT;
	}

	if (stream->posbuf)
		*stream->posbuf = 0;

	/* reset BDL address */
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  stream->sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPL,
			  0x0);
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  stream->sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPU,
			  0x0);

	/* clear stream status */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, stream->sd_offset,
				SOF_HDA_CL_DMA_SD_INT_MASK |
				SOF_HDA_SD_CTL_DMA_START, 0);
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
				stream->sd_offset + SOF_HDA_ADSP_REG_CL_SD_STS,
				SOF_HDA_CL_DMA_SD_INT_MASK,
				SOF_HDA_CL_DMA_SD_INT_MASK);

	stream->frags = 0;

	bdl = (struct sof_intel_dsp_bdl *)stream->bdl.area;
	ret = hda_dsp_stream_setup_bdl(sdev, dmab, stream, bdl,
				       stream->bufsize, params);
	if (ret < 0) {
		dev_err(sdev->dev, "error: set up of BDL failed\n");
		return ret;
	}

	/* set up stream descriptor for DMA */
	/* program stream tag */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, stream->sd_offset,
				SOF_HDA_CL_SD_CTL_STREAM_TAG_MASK,
				stream->tag <<
				SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT);

	/* program cyclic buffer length */
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  stream->sd_offset + SOF_HDA_ADSP_REG_CL_SD_CBL,
			  stream->bufsize);

	/* program stream format */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
				stream->sd_offset +
				SOF_HDA_ADSP_REG_CL_SD_FORMAT,
				0xffff, stream->config);

	/* program last valid index */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
				stream->sd_offset + SOF_HDA_ADSP_REG_CL_SD_LVI,
				0xffff, (stream->frags - 1));

	/* program BDL address */
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  stream->sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPL,
			  (u32)stream->bdl.addr);
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
			  stream->sd_offset + SOF_HDA_ADSP_REG_CL_SD_BDLPU,
			  upper_32_bits(stream->bdl.addr));

	/* enable position buffer */
	if (!(snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, SOF_HDA_ADSP_DPLBASE)
				& SOF_HDA_ADSP_DPLBASE_ENABLE))
		snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR, SOF_HDA_ADSP_DPLBASE,
				  (u32)hdev->posbuffer.addr |
				  SOF_HDA_ADSP_DPLBASE_ENABLE);

	/* set interrupt enable bits */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, stream->sd_offset,
				SOF_HDA_CL_DMA_SD_INT_MASK,
				SOF_HDA_CL_DMA_SD_INT_MASK);

	/* read FIFO size */
	if (stream->direction == SNDRV_PCM_STREAM_PLAYBACK) {
		stream->fifo_size =
			snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR,
					 stream->sd_offset +
					 SOF_HDA_ADSP_REG_CL_SD_FIFOSIZE);
		stream->fifo_size &= 0xffff;
		stream->fifo_size += 1;
	} else {
		stream->fifo_size = 0;
	}

	return ret;
}

irqreturn_t hda_dsp_stream_interrupt(int irq, void *context)
{
	struct snd_sof_dev *sdev = (struct snd_sof_dev *)context;
	u32 status;

	if (!pm_runtime_active(sdev->dev))
		return IRQ_NONE;

	status = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, SOF_HDA_INTSTS);

	if (status == 0 || status == 0xffffffff)
		return IRQ_NONE;

	return status ? IRQ_WAKE_THREAD : IRQ_HANDLED;
}

irqreturn_t hda_dsp_stream_threaded_handler(int irq, void *context)
{
	struct snd_sof_dev *sdev = (struct snd_sof_dev *)context;
	struct sof_intel_hda_dev *hdev = sdev->hda;
	u32 status = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, SOF_HDA_INTSTS);
	u32 sd_status;
	int i;

	/* check playback streams */
	for (i = 0; i < hdev->num_playback; i++) {
		/* is IRQ for this stream ? */
		if (status & (1 << hdev->pstream[i].index)) {
			sd_status =
				snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR,
						 hdev->pstream[i].sd_offset +
						 SOF_HDA_ADSP_REG_CL_SD_STS) &
						 0xff;

			dev_dbg(sdev->dev, "pstream %d status 0x%x\n",
				i, sd_status);

			snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
						hdev->pstream[i].sd_offset +
						SOF_HDA_ADSP_REG_CL_SD_STS,
						SOF_HDA_CL_DMA_SD_INT_MASK,
						SOF_HDA_CL_DMA_SD_INT_MASK);

			if (!hdev->pstream[i].substream ||
			    !hdev->pstream[i].running ||
			    (sd_status & SOF_HDA_CL_DMA_SD_INT_MASK) == 0)
				continue;
		}
	}

	/* check capture streams */
	for (i = 0; i < hdev->num_capture; i++) {
		/* is IRQ for this stream ? */
		if (status & (1 << hdev->cstream[i].index)) {
			sd_status =
				snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR,
						 hdev->cstream[i].sd_offset +
						 SOF_HDA_ADSP_REG_CL_SD_STS) &
						 0xff;

			dev_dbg(sdev->dev, "cstream %d status 0x%x\n",
				i, sd_status);

			snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
						hdev->cstream[i].sd_offset +
						SOF_HDA_ADSP_REG_CL_SD_STS,
						SOF_HDA_CL_DMA_SD_INT_MASK,
						SOF_HDA_CL_DMA_SD_INT_MASK);

			if (!hdev->cstream[i].substream ||
			    !hdev->cstream[i].running ||
			    (sd_status & SOF_HDA_CL_DMA_SD_INT_MASK) == 0)
				continue;
		}
	}

	return IRQ_HANDLED;
}

int hda_dsp_stream_init(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hdev = sdev->hda;
	struct sof_intel_hda_stream *stream;
	struct pci_dev *pci = sdev->pci;
	int i, num_playback, num_capture, num_total, ret;
	u32 gcap;

	gcap = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, SOF_HDA_GCAP);
	dev_dbg(sdev->dev, "hda global caps = 0x%x\n", gcap);

	/* get stream count from GCAP */
	num_capture = (gcap >> 8) & 0x0f;
	num_playback = (gcap >> 12) & 0x0f;
	num_total = num_playback + num_capture;

	hdev->num_capture = num_capture;
	hdev->num_playback = num_playback;

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

	/* mem alloc for the position buffer */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, &pci->dev, 8,
				  &hdev->posbuffer);
	if (ret < 0) {
		dev_err(sdev->dev, "error: posbuffer dma alloc failed\n");
		return -ENOMEM;
	}

	/* create capture streams */
	for (i = 0; i < num_capture; i++) {
		stream = &hdev->cstream[i];

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

		/* do we support DRSM */
		if (sdev->bar[HDA_DSP_DRSM_BAR])
			stream->drsm_addr = sdev->bar[HDA_DSP_DRSM_BAR] +
				SOF_HDA_DRSM_BASE + SOF_HDA_DRSM_INTERVAL * i;

		stream->sd_offset = 0x20 * i + SOF_HDA_ADSP_LOADER_BASE;
		stream->sd_addr = sdev->bar[HDA_DSP_HDA_BAR] +
					stream->sd_offset;

		stream->tag = i + 1;
		stream->open = false;
		stream->running = false;
		stream->direction = SNDRV_PCM_STREAM_CAPTURE;
		stream->index = i;

		/* memory alloc for stream BDL */
		ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, &pci->dev,
					  HDA_DSP_BDL_SIZE, &stream->bdl);
		if (ret < 0) {
			dev_err(sdev->dev, "error: stream bdl dma alloc failed\n");
			return -ENOMEM;
		}
		stream->posbuf = (__le32 *)(hdev->posbuffer.area +
			(stream->index) * 8);
	}

	/* create playback streams */
	for (i = num_capture; i < num_total; i++) {
		stream = &hdev->pstream[i - num_capture];

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

		/* do we support DRSM */
		if (sdev->bar[HDA_DSP_DRSM_BAR])
			stream->drsm_addr = sdev->bar[HDA_DSP_DRSM_BAR] +
				SOF_HDA_DRSM_BASE + SOF_HDA_DRSM_INTERVAL * i;

		stream->sd_offset = 0x20 * i + SOF_HDA_ADSP_LOADER_BASE;
		stream->sd_addr = sdev->bar[HDA_DSP_HDA_BAR] +
					stream->sd_offset;
		stream->tag = i - num_capture + 1;
		stream->open = false;
		stream->running = false;
		stream->direction = SNDRV_PCM_STREAM_PLAYBACK;
		stream->index = i;

		/* mem alloc for stream BDL */
		ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, &pci->dev,
					  HDA_DSP_BDL_SIZE, &stream->bdl);
		if (ret < 0) {
			dev_err(sdev->dev, "error: stream bdl dma alloc failed\n");
			return -ENOMEM;
		}

		stream->posbuf = (__le32 *)(hdev->posbuffer.area +
			(stream->index) * 8);
	}

	return 0;
}

void hda_dsp_stream_free(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hdev = sdev->hda;
	struct sof_intel_hda_stream *stream;
	int i;

	/* free position buffer */
	if (hdev->posbuffer.area)
		snd_dma_free_pages(&hdev->posbuffer);

	/* free capture streams */
	for (i = 0; i < hdev->num_capture; i++) {
		stream = &hdev->cstream[i];

		/* free bdl buffer */
		if (stream->bdl.area)
			snd_dma_free_pages(&stream->bdl);
	}

	/* free playback streams */
	for (i = 0; i < hdev->num_playback; i++) {
		stream = &hdev->pstream[i];

		/* free bdl buffer */
		if (stream->bdl.area)
			snd_dma_free_pages(&stream->bdl);
	}
}

