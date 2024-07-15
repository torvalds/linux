// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
//

#include <sound/pcm_params.h>
#include "ipc3-priv.h"
#include "ops.h"
#include "sof-priv.h"
#include "sof-audio.h"

static int sof_ipc3_pcm_hw_free(struct snd_soc_component *component,
				struct snd_pcm_substream *substream)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sof_ipc_stream stream;
	struct snd_sof_pcm *spcm;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	if (!spcm->prepared[substream->stream])
		return 0;

	stream.hdr.size = sizeof(stream);
	stream.hdr.cmd = SOF_IPC_GLB_STREAM_MSG | SOF_IPC_STREAM_PCM_FREE;
	stream.comp_id = spcm->stream[substream->stream].comp_id;

	/* send IPC to the DSP */
	return sof_ipc_tx_message_no_reply(sdev->ipc, &stream, sizeof(stream));
}

static int sof_ipc3_pcm_hw_params(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_sof_platform_stream_params *platform_params)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sof_ipc_fw_version *v = &sdev->fw_ready.version;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sof_ipc_pcm_params_reply ipc_params_reply;
	struct sof_ipc_pcm_params pcm;
	struct snd_sof_pcm *spcm;
	int ret;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	memset(&pcm, 0, sizeof(pcm));

	/* number of pages should be rounded up */
	pcm.params.buffer.pages = PFN_UP(runtime->dma_bytes);

	/* set IPC PCM parameters */
	pcm.hdr.size = sizeof(pcm);
	pcm.hdr.cmd = SOF_IPC_GLB_STREAM_MSG | SOF_IPC_STREAM_PCM_PARAMS;
	pcm.comp_id = spcm->stream[substream->stream].comp_id;
	pcm.params.hdr.size = sizeof(pcm.params);
	pcm.params.buffer.phy_addr = spcm->stream[substream->stream].page_table.addr;
	pcm.params.buffer.size = runtime->dma_bytes;
	pcm.params.direction = substream->stream;
	pcm.params.sample_valid_bytes = params_width(params) >> 3;
	pcm.params.buffer_fmt = SOF_IPC_BUFFER_INTERLEAVED;
	pcm.params.rate = params_rate(params);
	pcm.params.channels = params_channels(params);
	pcm.params.host_period_bytes = params_period_bytes(params);

	/* container size */
	ret = snd_pcm_format_physical_width(params_format(params));
	if (ret < 0)
		return ret;
	pcm.params.sample_container_bytes = ret >> 3;

	/* format */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16:
		pcm.params.frame_fmt = SOF_IPC_FRAME_S16_LE;
		break;
	case SNDRV_PCM_FORMAT_S24:
		pcm.params.frame_fmt = SOF_IPC_FRAME_S24_4LE;
		break;
	case SNDRV_PCM_FORMAT_S32:
		pcm.params.frame_fmt = SOF_IPC_FRAME_S32_LE;
		break;
	case SNDRV_PCM_FORMAT_FLOAT:
		pcm.params.frame_fmt = SOF_IPC_FRAME_FLOAT;
		break;
	default:
		return -EINVAL;
	}

	/* Update the IPC message with information from the platform */
	pcm.params.stream_tag = platform_params->stream_tag;

	if (platform_params->use_phy_address)
		pcm.params.buffer.phy_addr = platform_params->phy_addr;

	if (platform_params->no_ipc_position) {
		/* For older ABIs set host_period_bytes to zero to inform
		 * FW we don't want position updates. Newer versions use
		 * no_stream_position for this purpose.
		 */
		if (v->abi_version < SOF_ABI_VER(3, 10, 0))
			pcm.params.host_period_bytes = 0;
		else
			pcm.params.no_stream_position = 1;
	}

	if (platform_params->cont_update_posn)
		pcm.params.cont_update_posn = 1;

	dev_dbg(component->dev, "stream_tag %d", pcm.params.stream_tag);

	/* send hw_params IPC to the DSP */
	ret = sof_ipc_tx_message(sdev->ipc, &pcm, sizeof(pcm),
				 &ipc_params_reply, sizeof(ipc_params_reply));
	if (ret < 0) {
		dev_err(component->dev, "HW params ipc failed for stream %d\n",
			pcm.params.stream_tag);
		return ret;
	}

	ret = snd_sof_set_stream_data_offset(sdev, &spcm->stream[substream->stream],
					     ipc_params_reply.posn_offset);
	if (ret < 0) {
		dev_err(component->dev, "%s: invalid stream data offset for PCM %d\n",
			__func__, spcm->pcm.pcm_id);
		return ret;
	}

	return ret;
}

static int sof_ipc3_pcm_trigger(struct snd_soc_component *component,
				struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct sof_ipc_stream stream;
	struct snd_sof_pcm *spcm;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	stream.hdr.size = sizeof(stream);
	stream.hdr.cmd = SOF_IPC_GLB_STREAM_MSG;
	stream.comp_id = spcm->stream[substream->stream].comp_id;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_PAUSE;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_RELEASE;
		break;
	case SNDRV_PCM_TRIGGER_START:
		stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_START;
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		fallthrough;
	case SNDRV_PCM_TRIGGER_STOP:
		stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_STOP;
		break;
	default:
		dev_err(component->dev, "Unhandled trigger cmd %d\n", cmd);
		return -EINVAL;
	}

	/* send IPC to the DSP */
	return sof_ipc_tx_message_no_reply(sdev->ipc, &stream, sizeof(stream));
}

static void ssp_dai_config_pcm_params_match(struct snd_sof_dev *sdev, const char *link_name,
					    struct snd_pcm_hw_params *params)
{
	struct sof_ipc_dai_config *config;
	struct snd_sof_dai *dai;
	int i;

	/*
	 * Search for all matching DAIs as we can have both playback and capture DAI
	 * associated with the same link.
	 */
	list_for_each_entry(dai, &sdev->dai_list, list) {
		if (!dai->name || strcmp(link_name, dai->name))
			continue;
		for (i = 0; i < dai->number_configs; i++) {
			struct sof_dai_private_data *private = dai->private;

			config = &private->dai_config[i];
			if (config->ssp.fsync_rate == params_rate(params)) {
				dev_dbg(sdev->dev, "DAI config %d matches pcm hw params\n", i);
				dai->current_config = i;
				break;
			}
		}
	}
}

static int sof_ipc3_pcm_dai_link_fixup(struct snd_soc_pcm_runtime *rtd,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(rtd, SOF_AUDIO_PCM_DRV_NAME);
	struct snd_interval *channels = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_sof_dai *dai = snd_sof_find_dai(component, (char *)rtd->dai_link->name);
	struct snd_interval *rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct sof_dai_private_data *private;
	struct snd_soc_dpcm *dpcm;

	if (!dai) {
		dev_err(component->dev, "%s: No DAI found with name %s\n", __func__,
			rtd->dai_link->name);
		return -EINVAL;
	}

	private = dai->private;
	if (!private) {
		dev_err(component->dev, "%s: No private data found for DAI %s\n", __func__,
			rtd->dai_link->name);
		return -EINVAL;
	}

	/* read format from topology */
	snd_mask_none(fmt);

	switch (private->comp_dai->config.frame_fmt) {
	case SOF_IPC_FRAME_S16_LE:
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S16_LE);
		break;
	case SOF_IPC_FRAME_S24_4LE:
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S24_LE);
		break;
	case SOF_IPC_FRAME_S32_LE:
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S32_LE);
		break;
	default:
		dev_err(component->dev, "No available DAI format!\n");
		return -EINVAL;
	}

	/* read rate and channels from topology */
	switch (private->dai_config->type) {
	case SOF_DAI_INTEL_SSP:
		/* search for config to pcm params match, if not found use default */
		ssp_dai_config_pcm_params_match(sdev, (char *)rtd->dai_link->name, params);

		rate->min = private->dai_config[dai->current_config].ssp.fsync_rate;
		rate->max = private->dai_config[dai->current_config].ssp.fsync_rate;
		channels->min = private->dai_config[dai->current_config].ssp.tdm_slots;
		channels->max = private->dai_config[dai->current_config].ssp.tdm_slots;

		dev_dbg(component->dev, "rate_min: %d rate_max: %d\n", rate->min, rate->max);
		dev_dbg(component->dev, "channels_min: %d channels_max: %d\n",
			channels->min, channels->max);

		break;
	case SOF_DAI_INTEL_DMIC:
		/* DMIC only supports 16 or 32 bit formats */
		if (private->comp_dai->config.frame_fmt == SOF_IPC_FRAME_S24_4LE) {
			dev_err(component->dev, "Invalid fmt %d for DAI type %d\n",
				private->comp_dai->config.frame_fmt,
				private->dai_config->type);
		}
		break;
	case SOF_DAI_INTEL_HDA:
		/*
		 * HDAudio does not follow the default trigger
		 * sequence due to firmware implementation
		 */
		for_each_dpcm_fe(rtd, SNDRV_PCM_STREAM_PLAYBACK, dpcm) {
			struct snd_soc_pcm_runtime *fe = dpcm->fe;

			fe->dai_link->trigger[SNDRV_PCM_STREAM_PLAYBACK] =
				SND_SOC_DPCM_TRIGGER_POST;
		}
		break;
	case SOF_DAI_INTEL_ALH:
		/*
		 * Dai could run with different channel count compared with
		 * front end, so get dai channel count from topology
		 */
		channels->min = private->dai_config->alh.channels;
		channels->max = private->dai_config->alh.channels;
		break;
	case SOF_DAI_IMX_ESAI:
		rate->min = private->dai_config->esai.fsync_rate;
		rate->max = private->dai_config->esai.fsync_rate;
		channels->min = private->dai_config->esai.tdm_slots;
		channels->max = private->dai_config->esai.tdm_slots;

		dev_dbg(component->dev, "rate_min: %d rate_max: %d\n", rate->min, rate->max);
		dev_dbg(component->dev, "channels_min: %d channels_max: %d\n",
			channels->min, channels->max);
		break;
	case SOF_DAI_MEDIATEK_AFE:
		rate->min = private->dai_config->afe.rate;
		rate->max = private->dai_config->afe.rate;
		channels->min = private->dai_config->afe.channels;
		channels->max = private->dai_config->afe.channels;

		snd_mask_none(fmt);

		switch (private->dai_config->afe.format) {
		case SOF_IPC_FRAME_S16_LE:
			snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S16_LE);
			break;
		case SOF_IPC_FRAME_S24_4LE:
			snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S24_LE);
			break;
		case SOF_IPC_FRAME_S32_LE:
			snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S32_LE);
			break;
		default:
			dev_err(component->dev, "Not available format!\n");
			return -EINVAL;
		}

		dev_dbg(component->dev, "rate_min: %d rate_max: %d\n", rate->min, rate->max);
		dev_dbg(component->dev, "channels_min: %d channels_max: %d\n",
			channels->min, channels->max);
		break;
	case SOF_DAI_IMX_SAI:
		rate->min = private->dai_config->sai.fsync_rate;
		rate->max = private->dai_config->sai.fsync_rate;
		channels->min = private->dai_config->sai.tdm_slots;
		channels->max = private->dai_config->sai.tdm_slots;

		dev_dbg(component->dev, "rate_min: %d rate_max: %d\n", rate->min, rate->max);
		dev_dbg(component->dev, "channels_min: %d channels_max: %d\n",
			channels->min, channels->max);
		break;
	case SOF_DAI_AMD_BT:
		rate->min = private->dai_config->acpbt.fsync_rate;
		rate->max = private->dai_config->acpbt.fsync_rate;
		channels->min = private->dai_config->acpbt.tdm_slots;
		channels->max = private->dai_config->acpbt.tdm_slots;

		dev_dbg(component->dev,
			"AMD_BT rate_min: %d rate_max: %d\n", rate->min, rate->max);
		dev_dbg(component->dev, "AMD_BT channels_min: %d channels_max: %d\n",
			channels->min, channels->max);
		break;
	case SOF_DAI_AMD_SP:
	case SOF_DAI_AMD_SP_VIRTUAL:
		rate->min = private->dai_config->acpsp.fsync_rate;
		rate->max = private->dai_config->acpsp.fsync_rate;
		channels->min = private->dai_config->acpsp.tdm_slots;
		channels->max = private->dai_config->acpsp.tdm_slots;

		dev_dbg(component->dev,
			"AMD_SP rate_min: %d rate_max: %d\n", rate->min, rate->max);
		dev_dbg(component->dev, "AMD_SP channels_min: %d channels_max: %d\n",
			channels->min, channels->max);
		break;
	case SOF_DAI_AMD_HS:
	case SOF_DAI_AMD_HS_VIRTUAL:
		rate->min = private->dai_config->acphs.fsync_rate;
		rate->max = private->dai_config->acphs.fsync_rate;
		channels->min = private->dai_config->acphs.tdm_slots;
		channels->max = private->dai_config->acphs.tdm_slots;

		dev_dbg(component->dev,
			"AMD_HS channel_max: %d rate_max: %d\n", channels->max, rate->max);
		break;
	case SOF_DAI_AMD_DMIC:
		rate->min = private->dai_config->acpdmic.pdm_rate;
		rate->max = private->dai_config->acpdmic.pdm_rate;
		channels->min = private->dai_config->acpdmic.pdm_ch;
		channels->max = private->dai_config->acpdmic.pdm_ch;

		dev_dbg(component->dev,
			"AMD_DMIC rate_min: %d rate_max: %d\n", rate->min, rate->max);
		dev_dbg(component->dev, "AMD_DMIC channels_min: %d channels_max: %d\n",
			channels->min, channels->max);
		break;
	case SOF_DAI_IMX_MICFIL:
		rate->min = private->dai_config->micfil.pdm_rate;
		rate->max = private->dai_config->micfil.pdm_rate;
		channels->min = private->dai_config->micfil.pdm_ch;
		channels->max = private->dai_config->micfil.pdm_ch;

		dev_dbg(component->dev,
			"MICFIL PDM rate_min: %d rate_max: %d\n", rate->min, rate->max);
		dev_dbg(component->dev, "MICFIL PDM channels_min: %d channels_max: %d\n",
			channels->min, channels->max);
		break;
	case SOF_DAI_AMD_SDW:
		/* change the default trigger sequence as per HW implementation */
		for_each_dpcm_fe(rtd, SNDRV_PCM_STREAM_PLAYBACK, dpcm) {
			struct snd_soc_pcm_runtime *fe = dpcm->fe;

			fe->dai_link->trigger[SNDRV_PCM_STREAM_PLAYBACK] =
					SND_SOC_DPCM_TRIGGER_POST;
		}

		for_each_dpcm_fe(rtd, SNDRV_PCM_STREAM_CAPTURE, dpcm) {
			struct snd_soc_pcm_runtime *fe = dpcm->fe;

			fe->dai_link->trigger[SNDRV_PCM_STREAM_CAPTURE] =
					SND_SOC_DPCM_TRIGGER_POST;
		}
		rate->min = private->dai_config->acp_sdw.rate;
		rate->max = private->dai_config->acp_sdw.rate;
		channels->min = private->dai_config->acp_sdw.channels;
		channels->max = private->dai_config->acp_sdw.channels;

		dev_dbg(component->dev,
			"AMD_SDW rate_min: %d rate_max: %d\n", rate->min, rate->max);
		dev_dbg(component->dev, "AMD_SDW channels_min: %d channels_max: %d\n",
			channels->min, channels->max);
		break;
	default:
		dev_err(component->dev, "Invalid DAI type %d\n", private->dai_config->type);
		break;
	}

	return 0;
}

const struct sof_ipc_pcm_ops ipc3_pcm_ops = {
	.hw_params = sof_ipc3_pcm_hw_params,
	.hw_free = sof_ipc3_pcm_hw_free,
	.trigger = sof_ipc3_pcm_trigger,
	.dai_link_fixup = sof_ipc3_pcm_dai_link_fixup,
	.reset_hw_params_during_stop = true,
	.d0i3_supported_in_s0ix = true,
};
