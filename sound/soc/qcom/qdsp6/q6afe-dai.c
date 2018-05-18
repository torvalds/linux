// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include <linux/err.h>
#include <linux/init.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include "q6afe.h"

struct q6afe_dai_priv_data {
	uint32_t sd_line_mask;
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

	slim->num_channels = params_channels(params);
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

static void q6afe_dai_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct q6afe_dai_data *dai_data = dev_get_drvdata(dai->dev);
	int rc;

	rc = q6afe_port_stop(dai_data->port[dai->id]);
	if (rc < 0)
		dev_err(dai->dev, "fail to close AFE port (%d)\n", rc);

	dai_data->is_port_started[dai->id] = false;

}

static int q6afe_mi2s_prepare(struct snd_pcm_substream *substream,
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

	rc = q6afe_i2s_port_prepare(dai_data->port[dai->id],
			       &dai_data->port_config[dai->id].i2s_cfg);
	if (rc < 0) {
		dev_err(dai->dev, "fail to prepare AFE port %x\n", dai->id);
		return rc;
	}

	rc = q6afe_port_start(dai_data->port[dai->id]);
	if (rc < 0) {
		dev_err(dai->dev, "fail to start AFE port %x\n", dai->id);
		return rc;
	}
	dai_data->is_port_started[dai->id] = true;

	return 0;
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

	if (dai->id == HDMI_RX)
		q6afe_hdmi_port_prepare(dai_data->port[dai->id],
					&dai_data->port_config[dai->id].hdmi);
	else if (dai->id >= SLIMBUS_0_RX && dai->id <= SLIMBUS_6_TX)
		q6afe_slim_port_prepare(dai_data->port[dai->id],
					&dai_data->port_config[dai->id].slim);

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

	if (!rx_slot) {
		pr_err("%s: rx slot not found\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < rx_num; i++) {
		pcfg->slim.ch_mapping[i] =   rx_slot[i];
		pr_debug("%s: find number of channels[%d] ch[%d]\n",
		       __func__, i, rx_slot[i]);
	}

	pcfg->slim.num_channels = rx_num;

	pr_debug("%s: SLIMBUS_%d_RX cnt[%d] ch[%d %d]\n", __func__,
		(dai->id - SLIMBUS_0_RX) / 2, rx_num,
		pcfg->slim.ch_mapping[0],
		pcfg->slim.ch_mapping[1]);

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
	case Q6AFE_LPASS_CLK_ID_PRI_MI2S_IBIT ... Q6AFE_LPASS_CLK_ID_INT_MCLK_1:
		return q6afe_port_set_sysclk(port, clk_id,
					     Q6AFE_LPASS_CLK_ATTRIBUTE_COUPLE_NO,
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

	{"Primary MI2S Playback", NULL, "PRI_MI2S_RX"},
	{"Secondary MI2S Playback", NULL, "SEC_MI2S_RX"},
	{"Tertiary MI2S Playback", NULL, "TERT_MI2S_RX"},
	{"Quaternary MI2S Playback", NULL, "QUAT_MI2S_RX"},

	{"TERT_MI2S_TX", NULL, "Tertiary MI2S Capture"},
	{"PRI_MI2S_TX", NULL, "Primary MI2S Capture"},
	{"SEC_MI2S_TX", NULL, "Secondary MI2S Capture"},
	{"QUAT_MI2S_TX", NULL, "Quaternary MI2S Capture"},
};

static struct snd_soc_dai_ops q6hdmi_ops = {
	.prepare	= q6afe_dai_prepare,
	.hw_params	= q6hdmi_hw_params,
	.shutdown	= q6afe_dai_shutdown,
};

static struct snd_soc_dai_ops q6i2s_ops = {
	.prepare	= q6afe_mi2s_prepare,
	.hw_params	= q6i2s_hw_params,
	.set_fmt	= q6i2s_set_fmt,
	.shutdown	= q6afe_dai_shutdown,
	.set_sysclk	= q6afe_mi2s_set_sysclk,
};

static struct snd_soc_dai_ops q6slim_ops = {
	.prepare	= q6afe_dai_prepare,
	.hw_params	= q6slim_hw_params,
	.shutdown	= q6afe_dai_shutdown,
	.set_channel_map = q6slim_set_channel_map,
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
	SND_SOC_DAPM_AIF_OUT("HDMI_RX", "HDMI Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_0_RX", "Slimbus Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_1_RX", "Slimbus1 Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_2_RX", "Slimbus2 Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_3_RX", "Slimbus3 Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_4_RX", "Slimbus4 Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_5_RX", "Slimbus5 Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLIMBUS_6_RX", "Slimbus6 Playback", 0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("QUAT_MI2S_RX", "Quaternary MI2S Playback",
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("QUAT_MI2S_TX", "Quaternary MI2S Capture",
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TERT_MI2S_RX", "Tertiary MI2S Playback",
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("TERT_MI2S_TX", "Tertiary MI2S Capture",
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_MI2S_RX", "Secondary MI2S Playback",
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("SEC_MI2S_TX", "Secondary MI2S Capture",
						0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SEC_MI2S_RX_SD1",
			"Secondary MI2S Playback SD1",
			0, 0, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PRI_MI2S_RX", "Primary MI2S Playback",
			     0, 0, 0, 0),
	SND_SOC_DAPM_AIF_IN("PRI_MI2S_TX", "Primary MI2S Capture",
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
		if (ret || id > AFE_PORT_MAX) {
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
		default:
			break;
		}
	}
}

static int q6afe_dai_bind(struct device *dev, struct device *master, void *data)
{
	struct q6afe_dai_data *dai_data;

	dai_data = kzalloc(sizeof(*dai_data), GFP_KERNEL);
	if (!dai_data)
		return -ENOMEM;

	dev_set_drvdata(dev, dai_data);

	of_q6afe_parse_dai_data(dev, dai_data);

	return snd_soc_register_component(dev, &q6afe_dai_component,
					  q6afe_dais, ARRAY_SIZE(q6afe_dais));
}

static void q6afe_dai_unbind(struct device *dev, struct device *master,
			     void *data)
{
	struct q6afe_dai_data *dai_data = dev_get_drvdata(dev);

	snd_soc_unregister_component(dev);
	kfree(dai_data);
}

static const struct component_ops q6afe_dai_comp_ops = {
	.bind   = q6afe_dai_bind,
	.unbind = q6afe_dai_unbind,
};

static int q6afe_dai_dev_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &q6afe_dai_comp_ops);
}

static int q6afe_dai_dev_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &q6afe_dai_comp_ops);
	return 0;
}

static struct platform_driver q6afe_dai_platform_driver = {
	.driver = {
		.name = "q6afe-dai",
	},
	.probe = q6afe_dai_dev_probe,
	.remove = q6afe_dai_dev_remove,
};
module_platform_driver(q6afe_dai_platform_driver);

MODULE_DESCRIPTION("Q6 Audio Fronend dai driver");
MODULE_LICENSE("GPL v2");
