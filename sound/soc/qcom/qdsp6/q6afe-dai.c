// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include <dt-bindings/sound/qcom,q6afe.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include "q6dsp-lpass-ports.h"
#include "q6dsp-common.h"
#include "q6afe.h"


struct q6afe_dai_priv_data {
	uint32_t sd_line_mask;
	uint32_t sync_mode;
	uint32_t sync_src;
	uint32_t data_out_enable;
	uint32_t invert_sync;
	uint32_t data_delay;
	uint32_t data_align;
};

struct q6afe_dai_data {
	struct q6afe_port *port[AFE_PORT_MAX];
	struct q6afe_port_config port_config[AFE_PORT_MAX];
	bool is_port_started[AFE_PORT_MAX];
	struct q6afe_dai_priv_data priv[AFE_PORT_MAX];
};

static int q6slim_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{

	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct q6afe_slim_cfg *slim = &dai_data->port_config[dai->id].slim;

	slim->sample_rate = params_rate(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_SPECIAL:
		slim->bit_width = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		slim->bit_width = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		slim->bit_width = 32;
		break;
	default:
		pr_err("%s: format %d\n",
			__func__, params_format(params));
		return -EINVAL;
	}

	return 0;
}

static int q6hdmi_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	int channels = params_channels(params);
	struct q6afe_hdmi_cfg *hdmi = &dai_data->port_config[dai->id].hdmi;
	int ret;

	hdmi->sample_rate = params_rate(params);
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		hdmi->bit_width = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		hdmi->bit_width = 24;
		break;
	}

	ret = q6dsp_get_channel_allocation(channels);
	if (ret < 0)
		return ret;

	hdmi->channel_allocation = (u16) ret;

	return 0;
}

static int q6afe_usb_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	int channels = params_channels(params);
	int rate = params_rate(params);
	struct q6afe_usb_cfg *usb = &dai_data->port_config[dai->id].usb_audio;

	usb->sample_rate = rate;
	usb->num_channels = channels;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_S16_LE:
		usb->bit_width = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
		usb->bit_width = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		usb->bit_width = 32;
		break;
	default:
		dev_err(dai->dev, "%s: invalid format %d\n",
			__func__, params_format(params));
		return -EINVAL;
	}

	return 0;
}

static int q6i2s_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct snd_soc_dai *dai)
{
	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct q6afe_i2s_cfg *i2s = &dai_data->port_config[dai->id].i2s_cfg;

	i2s->sample_rate = params_rate(params);
	i2s->bit_width = params_width(params);
	i2s->num_channels = params_channels(params);
	i2s->sd_line_mask = dai_data->priv[dai->id].sd_line_mask;

	return 0;
}

static int q6i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct q6afe_i2s_cfg *i2s = &dai_data->port_config[dai->id].i2s_cfg;

	i2s->fmt = fmt;

	return 0;
}

static int q6tdm_set_tdm_slot(struct snd_soc_dai *dai,
				unsigned int tx_mask,
				unsigned int rx_mask,
				int slots, int slot_width)
{

	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct q6afe_tdm_cfg *tdm = &dai_data->port_config[dai->id].tdm;
	unsigned int cap_mask;
	int rc = 0;

	/* HW only supports 16 and 32 bit slot width configuration */
	if ((slot_width != 16) && (slot_width != 32)) {
		dev_err(dai->dev, "%s: invalid slot_width %d\n",
			__func__, slot_width);
		return -EINVAL;
	}

	/* HW supports 1-32 slots configuration. Typical: 1, 2, 4, 8, 16, 32 */
	switch (slots) {
	case 2:
		cap_mask = 0x03;
		break;
	case 4:
		cap_mask = 0x0F;
		break;
	case 8:
		cap_mask = 0xFF;
		break;
	case 16:
		cap_mask = 0xFFFF;
		break;
	default:
		dev_err(dai->dev, "%s: invalid slots %d\n",
			__func__, slots);
		return -EINVAL;
	}

	switch (dai->id) {
	case PRIMARY_TDM_RX_0 ... QUINARY_TDM_TX_7:
		tdm->nslots_per_frame = slots;
		tdm->slot_width = slot_width;
		/* TDM RX dais ids are even and tx are odd */
		tdm->slot_mask = ((dai->id & 0x1) ? tx_mask : rx_mask) & cap_mask;
		break;
	default:
		dev_err(dai->dev, "%s: invalid dai id 0x%x\n",
			__func__, dai->id);
		return -EINVAL;
	}

	return rc;
}

static int q6tdm_set_channel_map(struct snd_soc_dai *dai,
				unsigned int tx_num, const unsigned int *tx_slot,
				unsigned int rx_num, const unsigned int *rx_slot)
{

	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct q6afe_tdm_cfg *tdm = &dai_data->port_config[dai->id].tdm;
	int rc = 0;
	int i = 0;

	switch (dai->id) {
	case PRIMARY_TDM_RX_0 ... QUINARY_TDM_TX_7:
		if (dai->id & 0x1) {
			if (!tx_slot) {
				dev_err(dai->dev, "tx slot not found\n");
				return -EINVAL;
			}
			if (tx_num > AFE_PORT_MAX_AUDIO_CHAN_CNT) {
				dev_err(dai->dev, "invalid tx num %d\n",
					tx_num);
				return -EINVAL;
			}

			for (i = 0; i < tx_num; i++)
				tdm->ch_mapping[i] = tx_slot[i];

			for (i = tx_num; i < AFE_PORT_MAX_AUDIO_CHAN_CNT; i++)
				tdm->ch_mapping[i] = Q6AFE_CMAP_INVALID;

			tdm->num_channels = tx_num;
		} else {
			/* rx */
			if (!rx_slot) {
				dev_err(dai->dev, "rx slot not found\n");
				return -EINVAL;
			}
			if (rx_num > AFE_PORT_MAX_AUDIO_CHAN_CNT) {
				dev_err(dai->dev, "invalid rx num %d\n",
					rx_num);
				return -EINVAL;
			}

			for (i = 0; i < rx_num; i++)
				tdm->ch_mapping[i] = rx_slot[i];

			for (i = rx_num; i < AFE_PORT_MAX_AUDIO_CHAN_CNT; i++)
				tdm->ch_mapping[i] = Q6AFE_CMAP_INVALID;

			tdm->num_channels = rx_num;
		}

		break;
	default:
		dev_err(dai->dev, "%s: invalid dai id 0x%x\n",
			__func__, dai->id);
		return -EINVAL;
	}

	return rc;
}

static int q6tdm_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct snd_soc_dai *dai)
{
	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct q6afe_tdm_cfg *tdm = &dai_data->port_config[dai->id].tdm;

	tdm->bit_width = params_width(params);
	tdm->sample_rate = params_rate(params);
	tdm->num_channels = params_channels(params);
	tdm->data_align_type = dai_data->priv[dai->id].data_align;
	tdm->sync_src = dai_data->priv[dai->id].sync_src;
	tdm->sync_mode = dai_data->priv[dai->id].sync_mode;

	return 0;
}

static int q6dma_set_channel_map(struct snd_soc_dai *dai,
				 unsigned int tx_num,
				 const unsigned int *tx_ch_mask,
				 unsigned int rx_num,
				 const unsigned int *rx_ch_mask)
{

	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct q6afe_cdc_dma_cfg *cfg = &dai_data->port_config[dai->id].dma_cfg;
	int ch_mask;
	int rc = 0;

	switch (dai->id) {
	case WSA_CODEC_DMA_TX_0:
	case WSA_CODEC_DMA_TX_1:
	case WSA_CODEC_DMA_TX_2:
	case VA_CODEC_DMA_TX_0:
	case VA_CODEC_DMA_TX_1:
	case VA_CODEC_DMA_TX_2:
	case TX_CODEC_DMA_TX_0:
	case TX_CODEC_DMA_TX_1:
	case TX_CODEC_DMA_TX_2:
	case TX_CODEC_DMA_TX_3:
	case TX_CODEC_DMA_TX_4:
	case TX_CODEC_DMA_TX_5:
		if (!tx_ch_mask) {
			dev_err(dai->dev, "tx slot not found\n");
			return -EINVAL;
		}

		if (tx_num > AFE_PORT_MAX_AUDIO_CHAN_CNT) {
			dev_err(dai->dev, "invalid tx num %d\n",
				tx_num);
			return -EINVAL;
		}
		ch_mask = *tx_ch_mask;

		break;
	case WSA_CODEC_DMA_RX_0:
	case WSA_CODEC_DMA_RX_1:
	case RX_CODEC_DMA_RX_0:
	case RX_CODEC_DMA_RX_1:
	case RX_CODEC_DMA_RX_2:
	case RX_CODEC_DMA_RX_3:
	case RX_CODEC_DMA_RX_4:
	case RX_CODEC_DMA_RX_5:
	case RX_CODEC_DMA_RX_6:
	case RX_CODEC_DMA_RX_7:
		/* rx */
		if (!rx_ch_mask) {
			dev_err(dai->dev, "rx slot not found\n");
			return -EINVAL;
		}
		if (rx_num > AFE_PORT_MAX_AUDIO_CHAN_CNT) {
			dev_err(dai->dev, "invalid rx num %d\n",
				rx_num);
			return -EINVAL;
		}
		ch_mask = *rx_ch_mask;

		break;
	default:
		dev_err(dai->dev, "%s: invalid dai id 0x%x\n",
			__func__, dai->id);
		return -EINVAL;
	}

	cfg->active_channels_mask = ch_mask;

	return rc;
}

static int q6dma_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct snd_soc_dai *dai)
{
	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct q6afe_cdc_dma_cfg *cfg = &dai_data->port_config[dai->id].dma_cfg;

	cfg->bit_width = params_width(params);
	cfg->sample_rate = params_rate(params);
	cfg->num_channels = params_channels(params);

	return 0;
}
static void q6afe_dai_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	int rc;

	if (!dai_data->is_port_started[dai->id])
		return;

	rc = q6afe_port_stop(dai_data->port[dai->id]);
	if (rc < 0)
		dev_err(dai->dev, "fail to close AFE port (%d)\n", rc);

	dai_data->is_port_started[dai->id] = false;

}

static int q6afe_dai_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	int rc;

	if (dai_data->is_port_started[dai->id]) {
		/* stop the port and restart with new port config */
		rc = q6afe_port_stop(dai_data->port[dai->id]);
		if (rc < 0) {
			dev_err(dai->dev, "fail to close AFE port (%d)\n", rc);
			return rc;
		}
	}

	switch (dai->id) {
	case HDMI_RX:
	case DISPLAY_PORT_RX:
		q6afe_hdmi_port_prepare(dai_data->port[dai->id],
					&dai_data->port_config[dai->id].hdmi);
		break;
	case SLIMBUS_0_RX ... SLIMBUS_6_TX:
		q6afe_slim_port_prepare(dai_data->port[dai->id],
					&dai_data->port_config[dai->id].slim);
		break;
	case QUINARY_MI2S_RX ... QUINARY_MI2S_TX:
	case PRIMARY_MI2S_RX ... QUATERNARY_MI2S_TX:
		rc = q6afe_i2s_port_prepare(dai_data->port[dai->id],
			       &dai_data->port_config[dai->id].i2s_cfg);
		if (rc < 0) {
			dev_err(dai->dev, "fail to prepare AFE port %x\n",
				dai->id);
			return rc;
		}
		break;
	case PRIMARY_TDM_RX_0 ... QUINARY_TDM_TX_7:
		q6afe_tdm_port_prepare(dai_data->port[dai->id],
					&dai_data->port_config[dai->id].tdm);
		break;
	case WSA_CODEC_DMA_RX_0 ... RX_CODEC_DMA_RX_7:
		q6afe_cdc_dma_port_prepare(dai_data->port[dai->id],
					   &dai_data->port_config[dai->id].dma_cfg);
		break;
	case USB_RX:
		q6afe_usb_port_prepare(dai_data->port[dai->id],
				       &dai_data->port_config[dai->id].usb_audio);
		break;
	default:
		return -EINVAL;
	}

	rc = q6afe_port_start(dai_data->port[dai->id]);
	if (rc < 0) {
		dev_err(dai->dev, "fail to start AFE port %x\n", dai->id);
		return rc;
	}
	dai_data->is_port_started[dai->id] = true;

	return 0;
}

static int q6slim_set_channel_map(struct snd_soc_dai *dai,
				  unsigned int tx_num,
				  const unsigned int *tx_slot,
				  unsigned int rx_num,
				  const unsigned int *rx_slot)
{
	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct q6afe_port_config *pcfg = &dai_data->port_config[dai->id];
	int i;

	if (dai->id & 0x1) {
		/* TX */
		if (!tx_slot) {
			pr_err("%s: tx slot not found\n", __func__);
			return -EINVAL;
		}

		for (i = 0; i < tx_num; i++)
			pcfg->slim.ch_mapping[i] = tx_slot[i];

		pcfg->slim.num_channels = tx_num;


	} else {
		if (!rx_slot) {
			pr_err("%s: rx slot not found\n", __func__);
			return -EINVAL;
		}

		for (i = 0; i < rx_num; i++)
			pcfg->slim.ch_mapping[i] =   rx_slot[i];

		pcfg->slim.num_channels = rx_num;

	}

	return 0;
}

static int q6afe_mi2s_set_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct q6afe_port *port = dai_data->port[dai->id];

	switch (clk_id) {
	case LPAIF_DIG_CLK:
		return q6afe_port_set_sysclk(port, clk_id, 0, 5, freq, dir);
	case LPAIF_BIT_CLK:
	case LPAIF_OSR_CLK:
		return q6afe_port_set_sysclk(port, clk_id,
					     Q6AFE_LPASS_CLK_SRC_INTERNAL,
					     Q6AFE_LPASS_CLK_ROOT_DEFAULT,
					     freq, dir);
	case Q6AFE_LPASS_CLK_ID_PRI_MI2S_IBIT ... Q6AFE_LPASS_CLK_ID_QUI_MI2S_OSR:
	case Q6AFE_LPASS_CLK_ID_MCLK_1 ... Q6AFE_LPASS_CLK_ID_INT_MCLK_1:
	case Q6AFE_LPASS_CLK_ID_WSA_CORE_MCLK ... Q6AFE_LPASS_CLK_ID_VA_CORE_2X_MCLK:
		return q6afe_port_set_sysclk(port, clk_id,
					     Q6AFE_LPASS_CLK_ATTRIBUTE_COUPLE_NO,
					     Q6AFE_LPASS_CLK_ROOT_DEFAULT,
					     freq, dir);
	case Q6AFE_LPASS_CLK_ID_PRI_TDM_IBIT ... Q6AFE_LPASS_CLK_ID_QUIN_TDM_EBIT:
		return q6afe_port_set_sysclk(port, clk_id,
					     Q6AFE_LPASS_CLK_ATTRIBUTE_INVERT_COUPLE_NO,
					     Q6AFE_LPASS_CLK_ROOT_DEFAULT,
					     freq, dir);
	}

	return 0;
}

static const struct snd_soc_dapm_route q6afe_dapm_routes[] = {
	{"HDMI Playback", NULL, "HDMI_RX"},
	{"DISPLAY_PORT_RX_0 Playback", NULL, "DISPLAY_PORT_RX"},
	{"Slimbus Playback", NULL, "SLIMBUS_0_RX"},
	{"Slimbus1 Playback", NULL, "SLIMBUS_1_RX"},
	{"Slimbus2 Playback", NULL, "SLIMBUS_2_RX"},
	{"Slimbus3 Playback", NULL, "SLIMBUS_3_RX"},
	{"Slimbus4 Playback", NULL, "SLIMBUS_4_RX"},
	{"Slimbus5 Playback", NULL, "SLIMBUS_5_RX"},
	{"Slimbus6 Playback", NULL, "SLIMBUS_6_RX"},

	{"SLIMBUS_0_TX", NULL, "Slimbus Capture"},
	{"SLIMBUS_1_TX", NULL, "Slimbus1 Capture"},
	{"SLIMBUS_2_TX", NULL, "Slimbus2 Capture"},
	{"SLIMBUS_3_TX", NULL, "Slimbus3 Capture"},
	{"SLIMBUS_4_TX", NULL, "Slimbus4 Capture"},
	{"SLIMBUS_5_TX", NULL, "Slimbus5 Capture"},
	{"SLIMBUS_6_TX", NULL, "Slimbus6 Capture"},

	{"Primary MI2S Playback", NULL, "PRI_MI2S_RX"},
	{"Secondary MI2S Playback", NULL, "SEC_MI2S_RX"},
	{"Tertiary MI2S Playback", NULL, "TERT_MI2S_RX"},
	{"Quaternary MI2S Playback", NULL, "QUAT_MI2S_RX"},
	{"Quinary MI2S Playback", NULL, "QUIN_MI2S_RX"},

	{"Primary TDM0 Playback", NULL, "PRIMARY_TDM_RX_0"},
	{"Primary TDM1 Playback", NULL, "PRIMARY_TDM_RX_1"},
	{"Primary TDM2 Playback", NULL, "PRIMARY_TDM_RX_2"},
	{"Primary TDM3 Playback", NULL, "PRIMARY_TDM_RX_3"},
	{"Primary TDM4 Playback", NULL, "PRIMARY_TDM_RX_4"},
	{"Primary TDM5 Playback", NULL, "PRIMARY_TDM_RX_5"},
	{"Primary TDM6 Playback", NULL, "PRIMARY_TDM_RX_6"},
	{"Primary TDM7 Playback", NULL, "PRIMARY_TDM_RX_7"},

	{"Secondary TDM0 Playback", NULL, "SEC_TDM_RX_0"},
	{"Secondary TDM1 Playback", NULL, "SEC_TDM_RX_1"},
	{"Secondary TDM2 Playback", NULL, "SEC_TDM_RX_2"},
	{"Secondary TDM3 Playback", NULL, "SEC_TDM_RX_3"},
	{"Secondary TDM4 Playback", NULL, "SEC_TDM_RX_4"},
	{"Secondary TDM5 Playback", NULL, "SEC_TDM_RX_5"},
	{"Secondary TDM6 Playback", NULL, "SEC_TDM_RX_6"},
	{"Secondary TDM7 Playback", NULL, "SEC_TDM_RX_7"},

	{"Tertiary TDM0 Playback", NULL, "TERT_TDM_RX_0"},
	{"Tertiary TDM1 Playback", NULL, "TERT_TDM_RX_1"},
	{"Tertiary TDM2 Playback", NULL, "TERT_TDM_RX_2"},
	{"Tertiary TDM3 Playback", NULL, "TERT_TDM_RX_3"},
	{"Tertiary TDM4 Playback", NULL, "TERT_TDM_RX_4"},
	{"Tertiary TDM5 Playback", NULL, "TERT_TDM_RX_5"},
	{"Tertiary TDM6 Playback", NULL, "TERT_TDM_RX_6"},
	{"Tertiary TDM7 Playback", NULL, "TERT_TDM_RX_7"},

	{"Quaternary TDM0 Playback", NULL, "QUAT_TDM_RX_0"},
	{"Quaternary TDM1 Playback", NULL, "QUAT_TDM_RX_1"},
	{"Quaternary TDM2 Playback", NULL, "QUAT_TDM_RX_2"},
	{"Quaternary TDM3 Playback", NULL, "QUAT_TDM_RX_3"},
	{"Quaternary TDM4 Playback", NULL, "QUAT_TDM_RX_4"},
	{"Quaternary TDM5 Playback", NULL, "QUAT_TDM_RX_5"},
	{"Quaternary TDM6 Playback", NULL, "QUAT_TDM_RX_6"},
	{"Quaternary TDM7 Playback", NULL, "QUAT_TDM_RX_7"},

	{"Quinary TDM0 Playback", NULL, "QUIN_TDM_RX_0"},
	{"Quinary TDM1 Playback", NULL, "QUIN_TDM_RX_1"},
	{"Quinary TDM2 Playback", NULL, "QUIN_TDM_RX_2"},
	{"Quinary TDM3 Playback", NULL, "QUIN_TDM_RX_3"},
	{"Quinary TDM4 Playback", NULL, "QUIN_TDM_RX_4"},
	{"Quinary TDM5 Playback", NULL, "QUIN_TDM_RX_5"},
	{"Quinary TDM6 Playback", NULL, "QUIN_TDM_RX_6"},
	{"Quinary TDM7 Playback", NULL, "QUIN_TDM_RX_7"},

	{"PRIMARY_TDM_TX_0", NULL, "Primary TDM0 Capture"},
	{"PRIMARY_TDM_TX_1", NULL, "Primary TDM1 Capture"},
	{"PRIMARY_TDM_TX_2", NULL, "Primary TDM2 Capture"},
	{"PRIMARY_TDM_TX_3", NULL, "Primary TDM3 Capture"},
	{"PRIMARY_TDM_TX_4", NULL, "Primary TDM4 Capture"},
	{"PRIMARY_TDM_TX_5", NULL, "Primary TDM5 Capture"},
	{"PRIMARY_TDM_TX_6", NULL, "Primary TDM6 Capture"},
	{"PRIMARY_TDM_TX_7", NULL, "Primary TDM7 Capture"},

	{"SEC_TDM_TX_0", NULL, "Secondary TDM0 Capture"},
	{"SEC_TDM_TX_1", NULL, "Secondary TDM1 Capture"},
	{"SEC_TDM_TX_2", NULL, "Secondary TDM2 Capture"},
	{"SEC_TDM_TX_3", NULL, "Secondary TDM3 Capture"},
	{"SEC_TDM_TX_4", NULL, "Secondary TDM4 Capture"},
	{"SEC_TDM_TX_5", NULL, "Secondary TDM5 Capture"},
	{"SEC_TDM_TX_6", NULL, "Secondary TDM6 Capture"},
	{"SEC_TDM_TX_7", NULL, "Secondary TDM7 Capture"},

	{"TERT_TDM_TX_0", NULL, "Tertiary TDM0 Capture"},
	{"TERT_TDM_TX_1", NULL, "Tertiary TDM1 Capture"},
	{"TERT_TDM_TX_2", NULL, "Tertiary TDM2 Capture"},
	{"TERT_TDM_TX_3", NULL, "Tertiary TDM3 Capture"},
	{"TERT_TDM_TX_4", NULL, "Tertiary TDM4 Capture"},
	{"TERT_TDM_TX_5", NULL, "Tertiary TDM5 Capture"},
	{"TERT_TDM_TX_6", NULL, "Tertiary TDM6 Capture"},
	{"TERT_TDM_TX_7", NULL, "Tertiary TDM7 Capture"},

	{"QUAT_TDM_TX_0", NULL, "Quaternary TDM0 Capture"},
	{"QUAT_TDM_TX_1", NULL, "Quaternary TDM1 Capture"},
	{"QUAT_TDM_TX_2", NULL, "Quaternary TDM2 Capture"},
	{"QUAT_TDM_TX_3", NULL, "Quaternary TDM3 Capture"},
	{"QUAT_TDM_TX_4", NULL, "Quaternary TDM4 Capture"},
	{"QUAT_TDM_TX_5", NULL, "Quaternary TDM5 Capture"},
	{"QUAT_TDM_TX_6", NULL, "Quaternary TDM6 Capture"},
	{"QUAT_TDM_TX_7", NULL, "Quaternary TDM7 Capture"},

	{"QUIN_TDM_TX_0", NULL, "Quinary TDM0 Capture"},
	{"QUIN_TDM_TX_1", NULL, "Quinary TDM1 Capture"},
	{"QUIN_TDM_TX_2", NULL, "Quinary TDM2 Capture"},
	{"QUIN_TDM_TX_3", NULL, "Quinary TDM3 Capture"},
	{"QUIN_TDM_TX_4", NULL, "Quinary TDM4 Capture"},
	{"QUIN_TDM_TX_5", NULL, "Quinary TDM5 Capture"},
	{"QUIN_TDM_TX_6", NULL, "Quinary TDM6 Capture"},
	{"QUIN_TDM_TX_7", NULL, "Quinary TDM7 Capture"},

	{"TERT_MI2S_TX", NULL, "Tertiary MI2S Capture"},
	{"PRI_MI2S_TX", NULL, "Primary MI2S Capture"},
	{"SEC_MI2S_TX", NULL, "Secondary MI2S Capture"},
	{"QUAT_MI2S_TX", NULL, "Quaternary MI2S Capture"},
	{"QUIN_MI2S_TX", NULL, "Quinary MI2S Capture"},

	{"WSA_CODEC_DMA_RX_0 Playback", NULL, "WSA_CODEC_DMA_RX_0"},
	{"WSA_CODEC_DMA_TX_0", NULL, "WSA_CODEC_DMA_TX_0 Capture"},
	{"WSA_CODEC_DMA_RX_1 Playback", NULL, "WSA_CODEC_DMA_RX_1"},
	{"WSA_CODEC_DMA_TX_1", NULL, "WSA_CODEC_DMA_TX_1 Capture"},
	{"WSA_CODEC_DMA_TX_2", NULL, "WSA_CODEC_DMA_TX_2 Capture"},
	{"VA_CODEC_DMA_TX_0", NULL, "VA_CODEC_DMA_TX_0 Capture"},
	{"VA_CODEC_DMA_TX_1", NULL, "VA_CODEC_DMA_TX_1 Capture"},
	{"VA_CODEC_DMA_TX_2", NULL, "VA_CODEC_DMA_TX_2 Capture"},
	{"RX_CODEC_DMA_RX_0 Playback", NULL, "RX_CODEC_DMA_RX_0"},
	{"TX_CODEC_DMA_TX_0", NULL, "TX_CODEC_DMA_TX_0 Capture"},
	{"RX_CODEC_DMA_RX_1 Playback", NULL, "RX_CODEC_DMA_RX_1"},
	{"TX_CODEC_DMA_TX_1", NULL, "TX_CODEC_DMA_TX_1 Capture"},
	{"RX_CODEC_DMA_RX_2 Playback", NULL, "RX_CODEC_DMA_RX_2"},
	{"TX_CODEC_DMA_TX_2", NULL, "TX_CODEC_DMA_TX_2 Capture"},
	{"RX_CODEC_DMA_RX_3 Playback", NULL, "RX_CODEC_DMA_RX_3"},
	{"TX_CODEC_DMA_TX_3", NULL, "TX_CODEC_DMA_TX_3 Capture"},
	{"RX_CODEC_DMA_RX_4 Playback", NULL, "RX_CODEC_DMA_RX_4"},
	{"TX_CODEC_DMA_TX_4", NULL, "TX_CODEC_DMA_TX_4 Capture"},
	{"RX_CODEC_DMA_RX_5 Playback", NULL, "RX_CODEC_DMA_RX_5"},
	{"TX_CODEC_DMA_TX_5", NULL, "TX_CODEC_DMA_TX_5 Capture"},
	{"RX_CODEC_DMA_RX_6 Playback", NULL, "RX_CODEC_DMA_RX_6"},
	{"RX_CODEC_DMA_RX_7 Playback", NULL, "RX_CODEC_DMA_RX_7"},

	/* USB playback AFE port receives data for playback, hence use the RX port */
	{"USB Playback", NULL, "USB_RX"},
};

static int msm_dai_q6_dai_probe(struct snd_soc_dai *dai)
{
	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct q6afe_port *port;

	port = q6afe_port_get_from_id(dai->dev, dai->id);
	if (IS_ERR(port)) {
		dev_err(dai->dev, "Unable to get afe port\n");
		return -EINVAL;
	}
	dai_data->port[dai->id] = port;

	return 0;
}

static int msm_dai_q6_dai_remove(struct snd_soc_dai *dai)
{
	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);

	q6afe_port_put(dai_data->port[dai->id]);
	dai_data->port[dai->id] = NULL;

	return 0;
}

static const struct snd_soc_dai_ops q6afe_usb_ops = {
	.probe		= msm_dai_q6_dai_probe,
	.prepare	= q6afe_dai_prepare,
	.hw_params	= q6afe_usb_hw_params,
	/*
	 * Shutdown callback required to stop the USB AFE port, which is enabled
	 * by the prepare() stage.  This stops the audio traffic on the USB AFE
	 * port on the Q6DSP.
	 */
	.shutdown	= q6afe_dai_shutdown,
	/*
	 * Startup callback not needed, as AFE port start command passes the PCM
	 * parameters within the AFE command, which is provided by the PCM core
	 * during the prepare() stage.
	 */
};

static const struct snd_soc_dai_ops q6hdmi_ops = {
	.probe			= msm_dai_q6_dai_probe,
	.remove			= msm_dai_q6_dai_remove,
	.prepare		= q6afe_dai_prepare,
	.hw_params		= q6hdmi_hw_params,
	.shutdown		= q6afe_dai_shutdown,
};

static const struct snd_soc_dai_ops q6i2s_ops = {
	.probe			= msm_dai_q6_dai_probe,
	.remove			= msm_dai_q6_dai_remove,
	.prepare		= q6afe_dai_prepare,
	.hw_params		= q6i2s_hw_params,
	.set_fmt		= q6i2s_set_fmt,
	.shutdown		= q6afe_dai_shutdown,
	.set_sysclk		= q6afe_mi2s_set_sysclk,
};

static const struct snd_soc_dai_ops q6slim_ops = {
	.probe			= msm_dai_q6_dai_probe,
	.remove			= msm_dai_q6_dai_remove,
	.prepare		= q6afe_dai_prepare,
	.hw_params		= q6slim_hw_params,
	.shutdown		= q6afe_dai_shutdown,
	.set_channel_map	= q6slim_set_channel_map,
};

static const struct snd_soc_dai_ops q6tdm_ops = {
	.probe			= msm_dai_q6_dai_probe,
	.remove			= msm_dai_q6_dai_remove,
	.prepare		= q6afe_dai_prepare,
	.shutdown		= q6afe_dai_shutdown,
	.set_sysclk		= q6afe_mi2s_set_sysclk,
	.set_tdm_slot		= q6tdm_set_tdm_slot,
	.set_channel_map	= q6tdm_set_channel_map,
	.hw_params		= q6tdm_hw_params,
};

static const struct snd_soc_dai_ops q6dma_ops = {
	.probe			= msm_dai_q6_dai_probe,
	.remove			= msm_dai_q6_dai_remove,
	.prepare		= q6afe_dai_prepare,
	.shutdown		= q6afe_dai_shutdown,
	.set_sysclk		= q6afe_mi2s_set_sysclk,
	.set_channel_map	= q6dma_set_channel_map,
	.hw_params		= q6dma_hw_params,
};

static const struct snd_soc_dapm_widget q6afe_dai_widgets[] = {
	SND_SOC_DAPM_AIF_IN("HDMI_RX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SLIMBUS_0_RX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SLIMBUS_1_RX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SLIMBUS_2_RX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SLIMBUS_3_RX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SLIMBUS_4_RX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SLIMBUS_5_RX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SLIMBUS_6_RX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_0_TX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_1_TX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_2_TX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_3_TX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_4_TX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_5_TX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_6_TX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUIN_MI2S_RX", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_MI2S_TX", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_MI2S_RX", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_MI2S_TX", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_MI2S_RX", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_MI2S_TX", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_MI2S_RX", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_MI2S_TX", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_MI2S_RX_SD1",
			"Secondary MI2S Playback SD1",
			0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRI_MI2S_RX", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRI_MI2S_TX", NULL,
						0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_0", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_1", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_2", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_3", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_4", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_5", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_6", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_7", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_0", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_1", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_2", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_3", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_4", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_5", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_6", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_7", NULL,
						0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_0", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_1", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_2", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_3", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_4", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_5", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_6", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_7", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_0", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_1", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_2", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_3", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_4", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_5", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_6", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_7", NULL,
						0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_0", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_1", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_2", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_3", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_4", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_5", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_6", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_7", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_0", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_1", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_2", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_3", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_4", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_5", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_6", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_7", NULL,
						0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_0", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_1", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_2", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_3", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_4", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_5", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_6", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_7", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_0", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_1", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_2", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_3", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_4", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_5", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_6", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_7", NULL,
						0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_0", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_1", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_2", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_3", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_4", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_5", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_6", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_7", NULL,
			     0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_0", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_1", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_2", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_3", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_4", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_5", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_6", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_7", NULL,
						0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DISPLAY_PORT_RX", "NULL", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_IN("WSA_CODEC_DMA_RX_0", "NULL",
		0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("WSA_CODEC_DMA_TX_0", "NULL",
		 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("WSA_CODEC_DMA_RX_1", "NULL",
		0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("WSA_CODEC_DMA_TX_1", "NULL",
		 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("WSA_CODEC_DMA_TX_2", "NULL",
		 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("VA_CODEC_DMA_TX_0", "NULL",
		 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("VA_CODEC_DMA_TX_1", "NULL",
		 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("VA_CODEC_DMA_TX_2", "NULL",
		 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX_CODEC_DMA_RX_0", "NULL",
		0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TX_CODEC_DMA_TX_0", "NULL",
		 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX_CODEC_DMA_RX_1", "NULL",
		0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TX_CODEC_DMA_TX_1", "NULL",
		 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX_CODEC_DMA_RX_2", "NULL",
		0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TX_CODEC_DMA_TX_2", "NULL",
		 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX_CODEC_DMA_RX_3", "NULL",
		0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TX_CODEC_DMA_TX_3", "NULL",
		 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX_CODEC_DMA_RX_4", "NULL",
		0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TX_CODEC_DMA_TX_4", "NULL",
		 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX_CODEC_DMA_RX_5", "NULL",
		0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TX_CODEC_DMA_TX_5", "NULL",
		 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX_CODEC_DMA_RX_6", "NULL",
		0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX_CODEC_DMA_RX_7", "NULL",
		0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_IN("USB_RX", NULL, 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_component_driver q6afe_dai_component = {
	.name		= "q6afe-dai-component",
	.dapm_widgets = q6afe_dai_widgets,
	.num_dapm_widgets = ARRAY_SIZE(q6afe_dai_widgets),
	.dapm_routes = q6afe_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(q6afe_dapm_routes),
	.of_xlate_dai_name = q6dsp_audio_ports_of_xlate_dai_name,

};

static void of_q6afe_parse_dai_data(struct device *dev,
				    struct q6afe_dai_data *data)
{
	struct device_node *node;
	int ret;

	for_each_child_of_node(dev->of_node, node) {
		unsigned int lines[Q6AFE_MAX_MI2S_LINES];
		struct q6afe_dai_priv_data *priv;
		int id, i, num_lines;

		ret = of_property_read_u32(node, "reg", &id);
		if (ret || id < 0 || id >= AFE_PORT_MAX) {
			dev_err(dev, "valid dai id not found:%d\n", ret);
			continue;
		}

		switch (id) {
		/* MI2S specific properties */
		case QUINARY_MI2S_RX ... QUINARY_MI2S_TX:
		case PRIMARY_MI2S_RX ... QUATERNARY_MI2S_TX:
			priv = &data->priv[id];
			ret = of_property_read_variable_u32_array(node,
							"qcom,sd-lines",
							lines, 0,
							Q6AFE_MAX_MI2S_LINES);
			if (ret < 0)
				num_lines = 0;
			else
				num_lines = ret;

			priv->sd_line_mask = 0;

			for (i = 0; i < num_lines; i++)
				priv->sd_line_mask |= BIT(lines[i]);

			break;
		case PRIMARY_TDM_RX_0 ... QUINARY_TDM_TX_7:
			priv = &data->priv[id];
			ret = of_property_read_u32(node, "qcom,tdm-sync-mode",
						   &priv->sync_mode);
			if (ret) {
				dev_err(dev, "No Sync mode from DT\n");
				break;
			}
			ret = of_property_read_u32(node, "qcom,tdm-sync-src",
						   &priv->sync_src);
			if (ret) {
				dev_err(dev, "No Sync Src from DT\n");
				break;
			}
			ret = of_property_read_u32(node, "qcom,tdm-data-out",
						   &priv->data_out_enable);
			if (ret) {
				dev_err(dev, "No Data out enable from DT\n");
				break;
			}
			ret = of_property_read_u32(node, "qcom,tdm-invert-sync",
						   &priv->invert_sync);
			if (ret) {
				dev_err(dev, "No Invert sync from DT\n");
				break;
			}
			ret = of_property_read_u32(node, "qcom,tdm-data-delay",
						   &priv->data_delay);
			if (ret) {
				dev_err(dev, "No Data Delay from DT\n");
				break;
			}
			ret = of_property_read_u32(node, "qcom,tdm-data-align",
						   &priv->data_align);
			if (ret) {
				dev_err(dev, "No Data align from DT\n");
				break;
			}
			break;
		default:
			break;
		}
	}
}

static int q6afe_dai_dev_probe(struct platform_device *pdev)
{
	struct q6dsp_audio_port_dai_driver_config cfg;
	struct snd_soc_dai_driver *dais;
	struct q6afe_dai_data *dai_data;
	struct device *dev = &pdev->dev;
	int num_dais;

	dai_data = devm_kzalloc(dev, sizeof(*dai_data), GFP_KERNEL);
	if (!dai_data)
		return -ENOMEM;

	dev_set_drvdata(dev, dai_data);
	of_q6afe_parse_dai_data(dev, dai_data);

	cfg.q6hdmi_ops = &q6hdmi_ops;
	cfg.q6slim_ops = &q6slim_ops;
	cfg.q6i2s_ops = &q6i2s_ops;
	cfg.q6tdm_ops = &q6tdm_ops;
	cfg.q6dma_ops = &q6dma_ops;
	cfg.q6usb_ops = &q6afe_usb_ops;
	dais = q6dsp_audio_ports_set_config(dev, &cfg, &num_dais);

	return devm_snd_soc_register_component(dev, &q6afe_dai_component, dais, num_dais);
}

#ifdef CONFIG_OF
static const struct of_device_id q6afe_dai_device_id[] = {
	{ .compatible = "qcom,q6afe-dais" },
	{},
};
MODULE_DEVICE_TABLE(of, q6afe_dai_device_id);
#endif

static struct platform_driver q6afe_dai_platform_driver = {
	.driver = {
		.name = "q6afe-dai",
		.of_match_table = of_match_ptr(q6afe_dai_device_id),
	},
	.probe = q6afe_dai_dev_probe,
};
module_platform_driver(q6afe_dai_platform_driver);

MODULE_DESCRIPTION("Q6 Audio Frontend dai driver");
MODULE_LICENSE("GPL v2");
