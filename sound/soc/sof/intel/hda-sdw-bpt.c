// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2025 Intel Corporation.
//

/*
 * Hardware interface for SoundWire BPT support with HDA DMA
 */

#include <sound/hdaudio_ext.h>
#include <sound/hda-mlink.h>
#include <sound/hda-sdw-bpt.h>
#include <sound/sof.h>
#include <sound/sof/ipc4/header.h>
#include "../ops.h"
#include "../sof-priv.h"
#include "../ipc4-priv.h"
#include "hda.h"

#define BPT_FREQUENCY		192000 /* The max rate defined in rate_bits[] hdac_device.c */
#define BPT_MULTIPLIER		((BPT_FREQUENCY / 48000) - 1)
#define BPT_CHAIN_DMA_FIFO_MS	10
/*
 * This routine is directly inspired by sof_ipc4_chain_dma_trigger(),
 * with major simplifications since there are no pipelines defined
 * and no dependency on ALSA hw_params
 */
static int chain_dma_trigger(struct snd_sof_dev *sdev, unsigned int stream_tag,
			     int direction, int state)
{
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	bool allocate, enable, set_fifo_size;
	struct sof_ipc4_msg msg = {{ 0 }};
	int dma_id;

	if (sdev->pdata->ipc_type != SOF_IPC_TYPE_4)
		return -EOPNOTSUPP;

	switch (state) {
	case SOF_IPC4_PIPE_RUNNING: /* Allocate and start the chain */
		allocate = true;
		enable = true;
		set_fifo_size = true;
		break;
	case SOF_IPC4_PIPE_PAUSED: /* Stop the chain */
		allocate = true;
		enable = false;
		set_fifo_size = false;
		break;
	case SOF_IPC4_PIPE_RESET: /* Deallocate chain resources and remove the chain */
		allocate = false;
		enable = false;
		set_fifo_size = false;
		break;
	default:
		dev_err(sdev->dev, "Unexpected state %d", state);
		return -EINVAL;
	}

	msg.primary = SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_GLB_CHAIN_DMA);
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_FW_GEN_MSG);

	/* for BPT/BRA we can use the same stream tag for host and link */
	dma_id = stream_tag - 1;
	if (direction == SNDRV_PCM_STREAM_CAPTURE)
		dma_id += ipc4_data->num_playback_streams;

	msg.primary |=  SOF_IPC4_GLB_CHAIN_DMA_HOST_ID(dma_id);
	msg.primary |=  SOF_IPC4_GLB_CHAIN_DMA_LINK_ID(dma_id);

	/* For BPT/BRA we use 32 bits so SCS is not set */

	/* CHAIN DMA needs at least 2ms */
	if (set_fifo_size)
		msg.extension |=  SOF_IPC4_GLB_EXT_CHAIN_DMA_FIFO_SIZE(BPT_FREQUENCY / 1000 *
								       BPT_CHAIN_DMA_FIFO_MS *
								       sizeof(u32));

	if (allocate)
		msg.primary |= SOF_IPC4_GLB_CHAIN_DMA_ALLOCATE_MASK;

	if (enable)
		msg.primary |= SOF_IPC4_GLB_CHAIN_DMA_ENABLE_MASK;

	return sof_ipc_tx_message_no_reply(sdev->ipc, &msg, 0);
}

static int hda_sdw_bpt_dma_prepare(struct device *dev, struct hdac_ext_stream **sdw_bpt_stream,
				   struct snd_dma_buffer *dmab_bdl, u32 bpt_num_bytes,
				   unsigned int num_channels, int direction)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	struct hdac_ext_stream *bpt_stream;
	unsigned int format = HDA_CL_STREAM_FORMAT;

	/*
	 * the baseline format needs to be adjusted to
	 * bandwidth requirements
	 */
	format |= (num_channels - 1);
	format |= BPT_MULTIPLIER << AC_FMT_MULT_SHIFT;

	dev_dbg(dev, "direction %d format_val %#x\n", direction, format);

	bpt_stream = hda_cl_prepare(dev, format, bpt_num_bytes, dmab_bdl, false, direction, false);
	if (IS_ERR(bpt_stream)) {
		dev_err(sdev->dev, "%s: SDW BPT DMA prepare failed: dir %d\n",
			__func__, direction);
		return PTR_ERR(bpt_stream);
	}
	*sdw_bpt_stream = bpt_stream;

	if (!sdev->dspless_mode_selected) {
		struct hdac_stream *hstream;
		u32 mask;

		/* decouple host and link DMA if the DSP is used */
		hstream = &bpt_stream->hstream;
		mask = BIT(hstream->index);

		snd_sof_dsp_update_bits(sdev, HDA_DSP_PP_BAR, SOF_HDA_REG_PP_PPCTL, mask, mask);

		snd_hdac_ext_stream_reset(bpt_stream);

		snd_hdac_ext_stream_setup(bpt_stream, format);
	}

	if (hdac_stream(bpt_stream)->direction == SNDRV_PCM_STREAM_PLAYBACK) {
		struct hdac_bus *bus = sof_to_bus(sdev);
		struct hdac_ext_link *hlink;
		int stream_tag;

		stream_tag = hdac_stream(bpt_stream)->stream_tag;
		hlink = hdac_bus_eml_sdw_get_hlink(bus);

		snd_hdac_ext_bus_link_set_stream_id(hlink, stream_tag);
	}
	return 0;
}

static int hda_sdw_bpt_dma_deprepare(struct device *dev, struct hdac_ext_stream *sdw_bpt_stream,
				     struct snd_dma_buffer *dmab_bdl)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	struct hdac_stream *hstream;
	u32 mask;
	int ret;

	ret = hda_cl_cleanup(sdev->dev, dmab_bdl, false, sdw_bpt_stream);
	if (ret < 0) {
		dev_err(sdev->dev, "%s: SDW BPT DMA cleanup failed\n",
			__func__);
		return ret;
	}

	if (hdac_stream(sdw_bpt_stream)->direction == SNDRV_PCM_STREAM_PLAYBACK) {
		struct hdac_bus *bus = sof_to_bus(sdev);
		struct hdac_ext_link *hlink;
		int stream_tag;

		stream_tag = hdac_stream(sdw_bpt_stream)->stream_tag;
		hlink = hdac_bus_eml_sdw_get_hlink(bus);

		snd_hdac_ext_bus_link_clear_stream_id(hlink, stream_tag);
	}

	if (!sdev->dspless_mode_selected) {
		/* Release CHAIN_DMA resources */
		ret = chain_dma_trigger(sdev, hdac_stream(sdw_bpt_stream)->stream_tag,
					hdac_stream(sdw_bpt_stream)->direction,
					SOF_IPC4_PIPE_RESET);
		if (ret < 0)
			dev_err(sdev->dev, "%s: chain_dma_trigger PIPE_RESET failed: %d\n",
				__func__, ret);

		/* couple host and link DMA */
		hstream = &sdw_bpt_stream->hstream;
		mask = BIT(hstream->index);

		snd_sof_dsp_update_bits(sdev, HDA_DSP_PP_BAR, SOF_HDA_REG_PP_PPCTL, mask, 0);
	}

	return 0;
}

static int hda_sdw_bpt_dma_enable(struct device *dev, struct hdac_ext_stream *sdw_bpt_stream)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	int ret;

	ret = hda_cl_trigger(sdev->dev, sdw_bpt_stream, SNDRV_PCM_TRIGGER_START);
	if (ret < 0)
		dev_err(sdev->dev, "%s: SDW BPT DMA trigger start failed\n", __func__);

	if (!sdev->dspless_mode_selected) {
		/* the chain DMA needs to be programmed before the DMAs */
		ret = chain_dma_trigger(sdev, hdac_stream(sdw_bpt_stream)->stream_tag,
					hdac_stream(sdw_bpt_stream)->direction,
					SOF_IPC4_PIPE_RUNNING);
		if (ret < 0) {
			dev_err(sdev->dev, "%s: chain_dma_trigger failed: %d\n",
				__func__, ret);
			hda_cl_trigger(sdev->dev, sdw_bpt_stream, SNDRV_PCM_TRIGGER_STOP);
			return ret;
		}
		snd_hdac_ext_stream_start(sdw_bpt_stream);
	}

	return ret;
}

static int hda_sdw_bpt_dma_disable(struct device *dev, struct hdac_ext_stream *sdw_bpt_stream)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	int ret;

	if (!sdev->dspless_mode_selected) {
		snd_hdac_ext_stream_clear(sdw_bpt_stream);

		ret = chain_dma_trigger(sdev, hdac_stream(sdw_bpt_stream)->stream_tag,
					hdac_stream(sdw_bpt_stream)->direction,
					SOF_IPC4_PIPE_PAUSED);
		if (ret < 0)
			dev_err(sdev->dev, "%s: chain_dma_trigger PIPE_PAUSED failed: %d\n",
				__func__, ret);
	}

	ret = hda_cl_trigger(sdev->dev, sdw_bpt_stream, SNDRV_PCM_TRIGGER_STOP);
	if (ret < 0)
		dev_err(sdev->dev, "%s: SDW BPT DMA trigger stop failed\n", __func__);

	return ret;
}

int hda_sdw_bpt_open(struct device *dev, int link_id, struct hdac_ext_stream **bpt_tx_stream,
		     struct snd_dma_buffer *dmab_tx_bdl, u32 bpt_tx_num_bytes,
		     u32 tx_dma_bandwidth, struct hdac_ext_stream **bpt_rx_stream,
		     struct snd_dma_buffer *dmab_rx_bdl, u32 bpt_rx_num_bytes,
		     u32 rx_dma_bandwidth)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	unsigned int num_channels_tx;
	unsigned int num_channels_rx;
	int ret1;
	int ret;

	num_channels_tx = DIV_ROUND_UP(tx_dma_bandwidth, BPT_FREQUENCY * 32);

	ret = hda_sdw_bpt_dma_prepare(dev, bpt_tx_stream, dmab_tx_bdl, bpt_tx_num_bytes,
				      num_channels_tx, SNDRV_PCM_STREAM_PLAYBACK);
	if (ret < 0) {
		dev_err(dev, "%s: hda_sdw_bpt_dma_prepare failed for TX: %d\n",
			__func__, ret);
		return ret;
	}

	num_channels_rx = DIV_ROUND_UP(rx_dma_bandwidth, BPT_FREQUENCY * 32);

	ret = hda_sdw_bpt_dma_prepare(dev, bpt_rx_stream, dmab_rx_bdl, bpt_rx_num_bytes,
				      num_channels_rx, SNDRV_PCM_STREAM_CAPTURE);
	if (ret < 0) {
		dev_err(dev, "%s: hda_sdw_bpt_dma_prepare failed for RX: %d\n",
			__func__, ret);

		ret1 = hda_sdw_bpt_dma_deprepare(dev, *bpt_tx_stream, dmab_tx_bdl);
		if (ret1 < 0)
			dev_err(dev, "%s: hda_sdw_bpt_dma_deprepare failed for TX: %d\n",
				__func__, ret1);
		return ret;
	}

	/* we need to map the channels in PCMSyCM registers */
	ret = hdac_bus_eml_sdw_map_stream_ch(sof_to_bus(sdev), link_id,
					     0, /* cpu_dai->id -> PDI0 */
					     GENMASK(num_channels_tx - 1, 0),
					     hdac_stream(*bpt_tx_stream)->stream_tag,
					     SNDRV_PCM_STREAM_PLAYBACK);
	if (ret < 0) {
		dev_err(dev, "%s: hdac_bus_eml_sdw_map_stream_ch failed for TX: %d\n",
			__func__, ret);
		goto close;
	}

	ret = hdac_bus_eml_sdw_map_stream_ch(sof_to_bus(sdev), link_id,
					     1, /* cpu_dai->id -> PDI1 */
					     GENMASK(num_channels_rx - 1, 0),
					     hdac_stream(*bpt_rx_stream)->stream_tag,
					     SNDRV_PCM_STREAM_CAPTURE);
	if (!ret)
		return 0;

	dev_err(dev, "%s: hdac_bus_eml_sdw_map_stream_ch failed for RX: %d\n",
		__func__, ret);

close:
	ret1 = hda_sdw_bpt_close(dev, *bpt_tx_stream, dmab_tx_bdl, *bpt_rx_stream, dmab_rx_bdl);
	if (ret1 < 0)
		dev_err(dev, "%s: hda_sdw_bpt_close failed: %d\n",
			__func__, ret1);

	return ret;
}
EXPORT_SYMBOL_NS(hda_sdw_bpt_open, "SND_SOC_SOF_INTEL_HDA_SDW_BPT");

int hda_sdw_bpt_send_async(struct device *dev, struct hdac_ext_stream *bpt_tx_stream,
			   struct hdac_ext_stream *bpt_rx_stream)
{
	int ret1;
	int ret;

	ret = hda_sdw_bpt_dma_enable(dev, bpt_tx_stream);
	if (ret < 0) {
		dev_err(dev, "%s: hda_sdw_bpt_dma_enable failed for TX: %d\n",
			__func__, ret);
		return ret;
	}

	ret = hda_sdw_bpt_dma_enable(dev, bpt_rx_stream);
	if (ret < 0) {
		dev_err(dev, "%s: hda_sdw_bpt_dma_enable failed for RX: %d\n",
			__func__, ret);

		ret1 = hda_sdw_bpt_dma_disable(dev, bpt_tx_stream);
		if (ret1 < 0)
			dev_err(dev, "%s: hda_sdw_bpt_dma_disable failed for TX: %d\n",
				__func__, ret1);
	}

	return ret;
}
EXPORT_SYMBOL_NS(hda_sdw_bpt_send_async, "SND_SOC_SOF_INTEL_HDA_SDW_BPT");

/*
 * 3s is several orders of magnitude larger than what is needed for a
 * typical firmware download.
 */
#define HDA_BPT_IOC_TIMEOUT_MS 3000

int hda_sdw_bpt_wait(struct device *dev, struct hdac_ext_stream *bpt_tx_stream,
		     struct hdac_ext_stream *bpt_rx_stream)
{
	struct sof_intel_hda_stream *hda_tx_stream;
	struct sof_intel_hda_stream *hda_rx_stream;
	snd_pcm_uframes_t tx_position;
	snd_pcm_uframes_t rx_position;
	unsigned long time_tx_left;
	unsigned long time_rx_left;
	int ret = 0;
	int ret1;
	int i;

	hda_tx_stream = container_of(bpt_tx_stream, struct sof_intel_hda_stream, hext_stream);
	hda_rx_stream = container_of(bpt_rx_stream, struct sof_intel_hda_stream, hext_stream);

	time_tx_left = wait_for_completion_timeout(&hda_tx_stream->ioc,
						   msecs_to_jiffies(HDA_BPT_IOC_TIMEOUT_MS));
	if (!time_tx_left) {
		tx_position = hda_dsp_stream_get_position(hdac_stream(bpt_tx_stream),
							  SNDRV_PCM_STREAM_PLAYBACK, false);
		dev_err(dev, "%s: SDW BPT TX DMA did not complete: %ld\n",
			__func__, tx_position);
		ret = -ETIMEDOUT;
		goto dma_disable;
	}

	/* Make sure the DMA is flushed */
	i = 0;
	do {
		tx_position = hda_dsp_stream_get_position(hdac_stream(bpt_tx_stream),
							  SNDRV_PCM_STREAM_PLAYBACK, false);
		usleep_range(1000, 1010);
		i++;
	} while (tx_position && i < HDA_BPT_IOC_TIMEOUT_MS);
	if (tx_position) {
		dev_err(dev, "%s: SDW BPT TX DMA position %ld was not cleared\n",
			__func__, tx_position);
		ret = -ETIMEDOUT;
		goto dma_disable;
	}

	/* the wait should be minimal here */
	time_rx_left = wait_for_completion_timeout(&hda_rx_stream->ioc,
						   msecs_to_jiffies(HDA_BPT_IOC_TIMEOUT_MS));
	if (!time_rx_left) {
		rx_position = hda_dsp_stream_get_position(hdac_stream(bpt_rx_stream),
							  SNDRV_PCM_STREAM_CAPTURE, false);
		dev_err(dev, "%s: SDW BPT RX DMA did not complete: %ld\n",
			__func__, rx_position);
		ret = -ETIMEDOUT;
		goto dma_disable;
	}

	/* Make sure the DMA is flushed */
	i = 0;
	do {
		rx_position = hda_dsp_stream_get_position(hdac_stream(bpt_rx_stream),
							  SNDRV_PCM_STREAM_CAPTURE, false);
		usleep_range(1000, 1010);
		i++;
	} while (rx_position && i < HDA_BPT_IOC_TIMEOUT_MS);
	if (rx_position) {
		dev_err(dev, "%s: SDW BPT RX DMA position %ld was not cleared\n",
			__func__, rx_position);
		ret = -ETIMEDOUT;
		goto dma_disable;
	}

dma_disable:
	ret1 = hda_sdw_bpt_dma_disable(dev, bpt_rx_stream);
	if (!ret)
		ret = ret1;

	ret1 = hda_sdw_bpt_dma_disable(dev, bpt_tx_stream);
	if (!ret)
		ret = ret1;

	return ret;
}
EXPORT_SYMBOL_NS(hda_sdw_bpt_wait, "SND_SOC_SOF_INTEL_HDA_SDW_BPT");

int hda_sdw_bpt_close(struct device *dev, struct hdac_ext_stream *bpt_tx_stream,
		      struct snd_dma_buffer *dmab_tx_bdl, struct hdac_ext_stream *bpt_rx_stream,
		      struct snd_dma_buffer *dmab_rx_bdl)
{
	int ret;
	int ret1;

	ret = hda_sdw_bpt_dma_deprepare(dev, bpt_rx_stream, dmab_rx_bdl);

	ret1 = hda_sdw_bpt_dma_deprepare(dev, bpt_tx_stream, dmab_tx_bdl);
	if (!ret)
		ret = ret1;

	return ret;
}
EXPORT_SYMBOL_NS(hda_sdw_bpt_close, "SND_SOC_SOF_INTEL_HDA_SDW_BPT");

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("SOF helpers for HDaudio SoundWire BPT");
MODULE_IMPORT_NS("SND_SOC_SOF_INTEL_HDA_COMMON");
MODULE_IMPORT_NS("SND_SOC_SOF_HDA_MLINK");
