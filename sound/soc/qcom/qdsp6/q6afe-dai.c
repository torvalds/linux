// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include "q6afe.h"

#define Q6AFE_TDM_PB_DAI(pre, num, did) {				\
		.playback = {						\
			.stream_name = pre" TDM"#num" Playback",	\
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
				SNDRV_PCM_RATE_176400,			\
			.formats = SNDRV_PCM_FMTBIT_S16_LE |		\
				   SNDRV_PCM_FMTBIT_S24_LE |		\
				   SNDRV_PCM_FMTBIT_S32_LE,		\
			.channels_min = 1,				\
			.channels_max = 8,				\
			.rate_min = 8000,				\
			.rate_max = 176400,				\
		},							\
		.name = #did,						\
		.ops = &q6tdm_ops,					\
		.id = did,						\
		.probe = msm_dai_q6_dai_probe,				\
		.remove = msm_dai_q6_dai_remove,			\
	}

#define Q6AFE_TDM_CAP_DAI(pre, num, did) {				\
		.capture = {						\
			.stream_name = pre" TDM"#num" Capture",		\
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
				SNDRV_PCM_RATE_176400,			\
			.formats = SNDRV_PCM_FMTBIT_S16_LE |		\
				   SNDRV_PCM_FMTBIT_S24_LE |		\
				   SNDRV_PCM_FMTBIT_S32_LE,		\
			.channels_min = 1,				\
			.channels_max = 8,				\
			.rate_min = 8000,				\
			.rate_max = 176400,				\
		},							\
		.name = #did,						\
		.ops = &q6tdm_ops,					\
		.id = did,						\
		.probe = msm_dai_q6_dai_probe,				\
		.remove = msm_dai_q6_dai_remove,			\
	}

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

	hdmi->sample_rate = params_rate(params);
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		hdmi->bit_width = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		hdmi->bit_width = 24;
		break;
	}

	/* HDMI spec CEA-861-E: Table 28 Audio InfoFrame Data Byte 4 */
	switch (channels) {
	case 2:
		hdmi->channel_allocation = 0;
		break;
	case 3:
		hdmi->channel_allocation = 0x02;
		break;
	case 4:
		hdmi->channel_allocation = 0x06;
		break;
	case 5:
		hdmi->channel_allocation = 0x0A;
		break;
	case 6:
		hdmi->channel_allocation = 0x0B;
		break;
	case 7:
		hdmi->channel_allocation = 0x12;
		break;
	case 8:
		hdmi->channel_allocation = 0x13;
		break;
	default:
		dev_err(dai->dev, "invalid Channels = %u\n", channels);
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
		tdm->slot_mask = (dai->id & 0x1 ? tx_mask : rx_mask) & cap_mask;
		break;
	default:
		dev_err(dai->dev, "%s: invalid dai id 0x%x\n",
			__func__, dai->id);
		return -EINVAL;
	}

	return rc;
}

static int q6tdm_set_channel_map(struct snd_soc_dai *dai,
				unsigned int tx_num, unsigned int *tx_slot,
				unsigned int rx_num, unsigned int *rx_slot)
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
		q6afe_hdmi_port_prepare(dai_data->port[dai->id],
					&dai_data->port_config[dai->id].hdmi);
		break;
	case SLIMBUS_0_RX ... SLIMBUS_6_TX:
		q6afe_slim_port_prepare(dai_data->port[dai->id],
					&dai_data->port_config[dai->id].slim);
		break;
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
				unsigned int tx_num, unsigned int *tx_slot,
				unsigned int rx_num, unsigned int *rx_slot)
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
};

static const struct snd_soc_dai_ops q6hdmi_ops = {
	.prepare	= q6afe_dai_prepare,
	.hw_params	= q6hdmi_hw_params,
	.shutdown	= q6afe_dai_shutdown,
};

static const struct snd_soc_dai_ops q6i2s_ops = {
	.prepare	= q6afe_dai_prepare,
	.hw_params	= q6i2s_hw_params,
	.set_fmt	= q6i2s_set_fmt,
	.shutdown	= q6afe_dai_shutdown,
	.set_sysclk	= q6afe_mi2s_set_sysclk,
};

static const struct snd_soc_dai_ops q6slim_ops = {
	.prepare	= q6afe_dai_prepare,
	.hw_params	= q6slim_hw_params,
	.shutdown	= q6afe_dai_shutdown,
	.set_channel_map = q6slim_set_channel_map,
};

static const struct snd_soc_dai_ops q6tdm_ops = {
	.prepare	= q6afe_dai_prepare,
	.shutdown	= q6afe_dai_shutdown,
	.set_sysclk	= q6afe_mi2s_set_sysclk,
	.set_tdm_slot     = q6tdm_set_tdm_slot,
	.set_channel_map  = q6tdm_set_channel_map,
	.hw_params        = q6tdm_hw_params,
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

static struct snd_soc_dai_driver q6afe_dais[] = {
	{
		.playback = {
			.stream_name = "HDMI Playback",
			.rates = SNDRV_PCM_RATE_48000 |
				 SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 2,
			.channels_max = 8,
			.rate_max =     192000,
			.rate_min =	48000,
		},
		.ops = &q6hdmi_ops,
		.id = HDMI_RX,
		.name = "HDMI",
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	}, {
		.name = "SLIMBUS_0_RX",
		.ops = &q6slim_ops,
		.id = SLIMBUS_0_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
		.playback = {
			.stream_name = "Slimbus Playback",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.name = "SLIMBUS_0_TX",
		.ops = &q6slim_ops,
		.id = SLIMBUS_0_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
		.capture = {
			.stream_name = "Slimbus Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.playback = {
			.stream_name = "Slimbus1 Playback",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.name = "SLIMBUS_1_RX",
		.ops = &q6slim_ops,
		.id = SLIMBUS_1_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	}, {
		.name = "SLIMBUS_1_TX",
		.ops = &q6slim_ops,
		.id = SLIMBUS_1_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
		.capture = {
			.stream_name = "Slimbus1 Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.playback = {
			.stream_name = "Slimbus2 Playback",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.name = "SLIMBUS_2_RX",
		.ops = &q6slim_ops,
		.id = SLIMBUS_2_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,

	}, {
		.name = "SLIMBUS_2_TX",
		.ops = &q6slim_ops,
		.id = SLIMBUS_2_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
		.capture = {
			.stream_name = "Slimbus2 Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.playback = {
			.stream_name = "Slimbus3 Playback",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.name = "SLIMBUS_3_RX",
		.ops = &q6slim_ops,
		.id = SLIMBUS_3_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,

	}, {
		.name = "SLIMBUS_3_TX",
		.ops = &q6slim_ops,
		.id = SLIMBUS_3_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
		.capture = {
			.stream_name = "Slimbus3 Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.playback = {
			.stream_name = "Slimbus4 Playback",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.name = "SLIMBUS_4_RX",
		.ops = &q6slim_ops,
		.id = SLIMBUS_4_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,

	}, {
		.name = "SLIMBUS_4_TX",
		.ops = &q6slim_ops,
		.id = SLIMBUS_4_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
		.capture = {
			.stream_name = "Slimbus4 Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.playback = {
			.stream_name = "Slimbus5 Playback",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.name = "SLIMBUS_5_RX",
		.ops = &q6slim_ops,
		.id = SLIMBUS_5_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,

	}, {
		.name = "SLIMBUS_5_TX",
		.ops = &q6slim_ops,
		.id = SLIMBUS_5_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
		.capture = {
			.stream_name = "Slimbus5 Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.playback = {
			.stream_name = "Slimbus6 Playback",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.ops = &q6slim_ops,
		.name = "SLIMBUS_6_RX",
		.id = SLIMBUS_6_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,

	}, {
		.name = "SLIMBUS_6_TX",
		.ops = &q6slim_ops,
		.id = SLIMBUS_6_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
		.capture = {
			.stream_name = "Slimbus6 Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.playback = {
			.stream_name = "Primary MI2S Playback",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.id = PRIMARY_MI2S_RX,
		.name = "PRI_MI2S_RX",
		.ops = &q6i2s_ops,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	}, {
		.capture = {
			.stream_name = "Primary MI2S Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.id = PRIMARY_MI2S_TX,
		.name = "PRI_MI2S_TX",
		.ops = &q6i2s_ops,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	}, {
		.playback = {
			.stream_name = "Secondary MI2S Playback",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.name = "SEC_MI2S_RX",
		.id = SECONDARY_MI2S_RX,
		.ops = &q6i2s_ops,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	}, {
		.capture = {
			.stream_name = "Secondary MI2S Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.id = SECONDARY_MI2S_TX,
		.name = "SEC_MI2S_TX",
		.ops = &q6i2s_ops,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	}, {
		.playback = {
			.stream_name = "Tertiary MI2S Playback",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.name = "TERT_MI2S_RX",
		.id = TERTIARY_MI2S_RX,
		.ops = &q6i2s_ops,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	}, {
		.capture = {
			.stream_name = "Tertiary MI2S Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.id = TERTIARY_MI2S_TX,
		.name = "TERT_MI2S_TX",
		.ops = &q6i2s_ops,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	}, {
		.playback = {
			.stream_name = "Quaternary MI2S Playback",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.name = "QUAT_MI2S_RX",
		.id = QUATERNARY_MI2S_RX,
		.ops = &q6i2s_ops,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	}, {
		.capture = {
			.stream_name = "Quaternary MI2S Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.id = QUATERNARY_MI2S_TX,
		.name = "QUAT_MI2S_TX",
		.ops = &q6i2s_ops,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	Q6AFE_TDM_PB_DAI("Primary", 0, PRIMARY_TDM_RX_0),
	Q6AFE_TDM_PB_DAI("Primary", 1, PRIMARY_TDM_RX_1),
	Q6AFE_TDM_PB_DAI("Primary", 2, PRIMARY_TDM_RX_2),
	Q6AFE_TDM_PB_DAI("Primary", 3, PRIMARY_TDM_RX_3),
	Q6AFE_TDM_PB_DAI("Primary", 4, PRIMARY_TDM_RX_4),
	Q6AFE_TDM_PB_DAI("Primary", 5, PRIMARY_TDM_RX_5),
	Q6AFE_TDM_PB_DAI("Primary", 6, PRIMARY_TDM_RX_6),
	Q6AFE_TDM_PB_DAI("Primary", 7, PRIMARY_TDM_RX_7),
	Q6AFE_TDM_CAP_DAI("Primary", 0, PRIMARY_TDM_TX_0),
	Q6AFE_TDM_CAP_DAI("Primary", 1, PRIMARY_TDM_TX_1),
	Q6AFE_TDM_CAP_DAI("Primary", 2, PRIMARY_TDM_TX_2),
	Q6AFE_TDM_CAP_DAI("Primary", 3, PRIMARY_TDM_TX_3),
	Q6AFE_TDM_CAP_DAI("Primary", 4, PRIMARY_TDM_TX_4),
	Q6AFE_TDM_CAP_DAI("Primary", 5, PRIMARY_TDM_TX_5),
	Q6AFE_TDM_CAP_DAI("Primary", 6, PRIMARY_TDM_TX_6),
	Q6AFE_TDM_CAP_DAI("Primary", 7, PRIMARY_TDM_TX_7),
	Q6AFE_TDM_PB_DAI("Secondary", 0, SECONDARY_TDM_RX_0),
	Q6AFE_TDM_PB_DAI("Secondary", 1, SECONDARY_TDM_RX_1),
	Q6AFE_TDM_PB_DAI("Secondary", 2, SECONDARY_TDM_RX_2),
	Q6AFE_TDM_PB_DAI("Secondary", 3, SECONDARY_TDM_RX_3),
	Q6AFE_TDM_PB_DAI("Secondary", 4, SECONDARY_TDM_RX_4),
	Q6AFE_TDM_PB_DAI("Secondary", 5, SECONDARY_TDM_RX_5),
	Q6AFE_TDM_PB_DAI("Secondary", 6, SECONDARY_TDM_RX_6),
	Q6AFE_TDM_PB_DAI("Secondary", 7, SECONDARY_TDM_RX_7),
	Q6AFE_TDM_CAP_DAI("Secondary", 0, SECONDARY_TDM_TX_0),
	Q6AFE_TDM_CAP_DAI("Secondary", 1, SECONDARY_TDM_TX_1),
	Q6AFE_TDM_CAP_DAI("Secondary", 2, SECONDARY_TDM_TX_2),
	Q6AFE_TDM_CAP_DAI("Secondary", 3, SECONDARY_TDM_TX_3),
	Q6AFE_TDM_CAP_DAI("Secondary", 4, SECONDARY_TDM_TX_4),
	Q6AFE_TDM_CAP_DAI("Secondary", 5, SECONDARY_TDM_TX_5),
	Q6AFE_TDM_CAP_DAI("Secondary", 6, SECONDARY_TDM_TX_6),
	Q6AFE_TDM_CAP_DAI("Secondary", 7, SECONDARY_TDM_TX_7),
	Q6AFE_TDM_PB_DAI("Tertiary", 0, TERTIARY_TDM_RX_0),
	Q6AFE_TDM_PB_DAI("Tertiary", 1, TERTIARY_TDM_RX_1),
	Q6AFE_TDM_PB_DAI("Tertiary", 2, TERTIARY_TDM_RX_2),
	Q6AFE_TDM_PB_DAI("Tertiary", 3, TERTIARY_TDM_RX_3),
	Q6AFE_TDM_PB_DAI("Tertiary", 4, TERTIARY_TDM_RX_4),
	Q6AFE_TDM_PB_DAI("Tertiary", 5, TERTIARY_TDM_RX_5),
	Q6AFE_TDM_PB_DAI("Tertiary", 6, TERTIARY_TDM_RX_6),
	Q6AFE_TDM_PB_DAI("Tertiary", 7, TERTIARY_TDM_RX_7),
	Q6AFE_TDM_CAP_DAI("Tertiary", 0, TERTIARY_TDM_TX_0),
	Q6AFE_TDM_CAP_DAI("Tertiary", 1, TERTIARY_TDM_TX_1),
	Q6AFE_TDM_CAP_DAI("Tertiary", 2, TERTIARY_TDM_TX_2),
	Q6AFE_TDM_CAP_DAI("Tertiary", 3, TERTIARY_TDM_TX_3),
	Q6AFE_TDM_CAP_DAI("Tertiary", 4, TERTIARY_TDM_TX_4),
	Q6AFE_TDM_CAP_DAI("Tertiary", 5, TERTIARY_TDM_TX_5),
	Q6AFE_TDM_CAP_DAI("Tertiary", 6, TERTIARY_TDM_TX_6),
	Q6AFE_TDM_CAP_DAI("Tertiary", 7, TERTIARY_TDM_TX_7),
	Q6AFE_TDM_PB_DAI("Quaternary", 0, QUATERNARY_TDM_RX_0),
	Q6AFE_TDM_PB_DAI("Quaternary", 1, QUATERNARY_TDM_RX_1),
	Q6AFE_TDM_PB_DAI("Quaternary", 2, QUATERNARY_TDM_RX_2),
	Q6AFE_TDM_PB_DAI("Quaternary", 3, QUATERNARY_TDM_RX_3),
	Q6AFE_TDM_PB_DAI("Quaternary", 4, QUATERNARY_TDM_RX_4),
	Q6AFE_TDM_PB_DAI("Quaternary", 5, QUATERNARY_TDM_RX_5),
	Q6AFE_TDM_PB_DAI("Quaternary", 6, QUATERNARY_TDM_RX_6),
	Q6AFE_TDM_PB_DAI("Quaternary", 7, QUATERNARY_TDM_RX_7),
	Q6AFE_TDM_CAP_DAI("Quaternary", 0, QUATERNARY_TDM_TX_0),
	Q6AFE_TDM_CAP_DAI("Quaternary", 1, QUATERNARY_TDM_TX_1),
	Q6AFE_TDM_CAP_DAI("Quaternary", 2, QUATERNARY_TDM_TX_2),
	Q6AFE_TDM_CAP_DAI("Quaternary", 3, QUATERNARY_TDM_TX_3),
	Q6AFE_TDM_CAP_DAI("Quaternary", 4, QUATERNARY_TDM_TX_4),
	Q6AFE_TDM_CAP_DAI("Quaternary", 5, QUATERNARY_TDM_TX_5),
	Q6AFE_TDM_CAP_DAI("Quaternary", 6, QUATERNARY_TDM_TX_6),
	Q6AFE_TDM_CAP_DAI("Quaternary", 7, QUATERNARY_TDM_TX_7),
	Q6AFE_TDM_PB_DAI("Quinary", 0, QUINARY_TDM_RX_0),
	Q6AFE_TDM_PB_DAI("Quinary", 1, QUINARY_TDM_RX_1),
	Q6AFE_TDM_PB_DAI("Quinary", 2, QUINARY_TDM_RX_2),
	Q6AFE_TDM_PB_DAI("Quinary", 3, QUINARY_TDM_RX_3),
	Q6AFE_TDM_PB_DAI("Quinary", 4, QUINARY_TDM_RX_4),
	Q6AFE_TDM_PB_DAI("Quinary", 5, QUINARY_TDM_RX_5),
	Q6AFE_TDM_PB_DAI("Quinary", 6, QUINARY_TDM_RX_6),
	Q6AFE_TDM_PB_DAI("Quinary", 7, QUINARY_TDM_RX_7),
	Q6AFE_TDM_CAP_DAI("Quinary", 0, QUINARY_TDM_TX_0),
	Q6AFE_TDM_CAP_DAI("Quinary", 1, QUINARY_TDM_TX_1),
	Q6AFE_TDM_CAP_DAI("Quinary", 2, QUINARY_TDM_TX_2),
	Q6AFE_TDM_CAP_DAI("Quinary", 3, QUINARY_TDM_TX_3),
	Q6AFE_TDM_CAP_DAI("Quinary", 4, QUINARY_TDM_TX_4),
	Q6AFE_TDM_CAP_DAI("Quinary", 5, QUINARY_TDM_TX_5),
	Q6AFE_TDM_CAP_DAI("Quinary", 6, QUINARY_TDM_TX_6),
	Q6AFE_TDM_CAP_DAI("Quinary", 7, QUINARY_TDM_TX_7),
};

static int q6afe_of_xlate_dai_name(struct snd_soc_component *component,
				   struct of_phandle_args *args,
				   const char **dai_name)
{
	int id = args->args[0];
	int ret = -EINVAL;
	int i;

	for (i = 0; i  < ARRAY_SIZE(q6afe_dais); i++) {
		if (q6afe_dais[i].id == id) {
			*dai_name = q6afe_dais[i].name;
			ret = 0;
			break;
		}
	}

	return ret;
}

static const struct snd_soc_dapm_widget q6afe_dai_widgets[] = {
	SND_SOC_DAPM_AIF_IN("HDMI_RX", NULL, 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SLIMBUS_0_RX", NULL, 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SLIMBUS_1_RX", NULL, 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SLIMBUS_2_RX", NULL, 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SLIMBUS_3_RX", NULL, 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SLIMBUS_4_RX", NULL, 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SLIMBUS_5_RX", NULL, 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SLIMBUS_6_RX", NULL, 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_0_TX", NULL, 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_1_TX", NULL, 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_2_TX", NULL, 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_3_TX", NULL, 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_4_TX", NULL, 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_5_TX", NULL, 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_6_TX", NULL, 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_MI2S_RX", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_MI2S_TX", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_MI2S_RX", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_MI2S_TX", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_MI2S_RX", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_MI2S_TX", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_MI2S_RX_SD1",
			"Secondary MI2S Playback SD1",
			0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRI_MI2S_RX", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRI_MI2S_TX", NULL,
						0, 0, 0, 0),

	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_0", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_1", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_2", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_3", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_4", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_5", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_6", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRIMARY_TDM_RX_7", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_0", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_1", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_2", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_3", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_4", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_5", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_6", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRIMARY_TDM_TX_7", NULL,
						0, 0, 0, 0),

	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_0", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_1", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_2", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_3", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_4", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_5", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_6", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_TDM_RX_7", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_0", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_1", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_2", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_3", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_4", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_5", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_6", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_TDM_TX_7", NULL,
						0, 0, 0, 0),

	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_0", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_1", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_2", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_3", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_4", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_5", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_6", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_TDM_RX_7", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_0", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_1", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_2", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_3", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_4", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_5", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_6", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_TDM_TX_7", NULL,
						0, 0, 0, 0),

	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_0", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_1", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_2", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_3", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_4", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_5", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_6", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_TDM_RX_7", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_0", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_1", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_2", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_3", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_4", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_5", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_6", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_TDM_TX_7", NULL,
						0, 0, 0, 0),

	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_0", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_1", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_2", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_3", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_4", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_5", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_6", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUIN_TDM_RX_7", NULL,
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_0", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_1", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_2", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_3", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_4", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_5", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_6", NULL,
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUIN_TDM_TX_7", NULL,
						0, 0, 0, 0),
};

static const struct snd_soc_component_driver q6afe_dai_component = {
	.name		= "q6afe-dai-component",
	.dapm_widgets = q6afe_dai_widgets,
	.num_dapm_widgets = ARRAY_SIZE(q6afe_dai_widgets),
	.dapm_routes = q6afe_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(q6afe_dapm_routes),
	.of_xlate_dai_name = q6afe_of_xlate_dai_name,

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
	struct q6afe_dai_data *dai_data;
	struct device *dev = &pdev->dev;

	dai_data = devm_kzalloc(dev, sizeof(*dai_data), GFP_KERNEL);
	if (!dai_data)
		return -ENOMEM;

	dev_set_drvdata(dev, dai_data);

	of_q6afe_parse_dai_data(dev, dai_data);

	return devm_snd_soc_register_component(dev, &q6afe_dai_component,
					  q6afe_dais, ARRAY_SIZE(q6afe_dais));
}

static const struct of_device_id q6afe_dai_device_id[] = {
	{ .compatible = "qcom,q6afe-dais" },
	{},
};
MODULE_DEVICE_TABLE(of, q6afe_dai_device_id);

static struct platform_driver q6afe_dai_platform_driver = {
	.driver = {
		.name = "q6afe-dai",
		.of_match_table = of_match_ptr(q6afe_dai_device_id),
	},
	.probe = q6afe_dai_dev_probe,
};
module_platform_driver(q6afe_dai_platform_driver);

MODULE_DESCRIPTION("Q6 Audio Fronend dai driver");
MODULE_LICENSE("GPL v2");
