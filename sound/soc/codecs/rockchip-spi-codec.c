// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip Audio CODEC Driver for remote dsp
 *
 * Copyright (C) 2023 Rockchip Electronics Co.,Ltd
 *
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "rockchip-spi-codec.h"

#define TDM_CH_MAX		(32)
#define TDM_CH_ID(n)	(n)
#define VOL_MAX	(0x27)
#define VOL_MIN	(0x00)

struct spi_codec_private {
	struct device *dev;
	struct regmap *regmap;
	struct spi_device *spi;
	struct snd_soc_component *component;
	struct spi_codec_protocol_packet *packet;
	struct mutex lock;
	struct gpio_desc *reset_gpio;
	int tdm_volume[TDM_CH_MAX];
	int tdm_mute[TDM_CH_MAX];
};

static const DECLARE_TLV_DB_SCALE(playback_tlv, -1000, 100, 0);

#define SPICODEC_CH_VOLUME(name, ch)				\
	SOC_SINGLE_EXT_TLV(name, ch, VOL_MIN, VOL_MAX, 0X00,	\
			   spi_codec_ext_ch_volume_get,		\
			   spi_codec_ext_ch_volume_put, playback_tlv)

#define SPICODEC_CH_MUTE(name, ch)				\
	SOC_SINGLE_BOOL_EXT(name, ch,			\
			    spi_codec_ext_ch_mute_get,	\
			    spi_codec_ext_ch_mute_put)

static inline struct spi_device *soc_component_to_spi(struct snd_soc_component *c)
{
	return container_of(c->dev, struct spi_device, dev);
}

static int spi_codec_cmd_request_unlock(struct spi_codec_private *spi_priv,
					unsigned int cmd_type,
					unsigned int *payload,
					unsigned int payload_len,
					unsigned int address)
{
	struct spi_device *spi = spi_priv->spi;
	unsigned int packet_size, num, loop;

	if (!spi)
		return -ENODEV;

	packet_size = sizeof(struct spi_codec_protocol_packet);

	spi_priv->packet->cmd_begin = SS_CMD_BEGIN;
	spi_priv->packet->cmd_type  = cmd_type;

	loop = roundup(payload_len, PAYLOAD_MAX) / PAYLOAD_MAX;

	spi_priv->packet->crc = 0;
	spi_priv->packet->address = address;

	for (num = 0; num < loop; num++) {
		if (num == loop - 1) {
			spi_priv->packet->payload_len = payload_len % PAYLOAD_MAX;
			spi_priv->packet->cmd_end = SS_CMD_END;
		} else {
			spi_priv->packet->payload_len = PAYLOAD_MAX;
			spi_priv->packet->cmd_end = SS_CMD_PARTIAL_END;
		}

		memcpy(spi_priv->packet->payload, payload + num * PAYLOAD_MAX,
		       spi_priv->packet->payload_len);

		if (spi_write(spi, spi_priv->packet, packet_size)) {
			dev_err(&spi->dev, "ERR:spi write failed\n");
			return -EINVAL;
		}
	}
	return 0;
}

static int spi_codec_set_parameter(struct spi_codec_private *spi_priv,
				   unsigned int cmd_type,
				   unsigned int *payload,
				   unsigned int payload_len,
				   unsigned int address)
{
	int ret;

	mutex_lock(&spi_priv->lock);
	ret = spi_codec_cmd_request_unlock(spi_priv, cmd_type, payload,
					   payload_len, address);
	mutex_unlock(&spi_priv->lock);

	return ret;
}

static int spi_codec_ext_ch_volume_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct spi_codec_private *priv = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	unsigned int ch = mc->reg;

	ucontrol->value.integer.value[0] = priv->tdm_volume[ch];

	return 0;
}

static int spi_codec_ext_ch_volume_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct spi_codec_private *priv = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	unsigned int ch = mc->reg;

	priv->tdm_volume[ch] = ucontrol->value.integer.value[0];

	spi_codec_set_parameter(priv, SS_CMD_BLOCK_PARAMETER_SAFE,
				&priv->tdm_volume[ch], sizeof(priv->tdm_volume[ch]),
				TDM_CH_ID(ch));

	return 0;
}

static int spi_codec_ext_ch_mute_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct spi_codec_private *priv = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	unsigned int ch = mc->reg;

	ucontrol->value.integer.value[0] = priv->tdm_mute[ch];

	return 0;
}

static int spi_codec_ext_ch_mute_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct spi_codec_private *priv = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	unsigned int ch = mc->reg;

	priv->tdm_mute[ch] = ucontrol->value.integer.value[0];

	spi_codec_set_parameter(priv, SS_CMD_BLOCK_PARAMETER_SAFE,
				&priv->tdm_mute[ch], sizeof(priv->tdm_mute[ch]),
				TDM_CH_ID(TDM_CH_MAX + ch));

	return 0;
}

static int spi_codec_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	return 0;
}

static void spi_codec_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
}

static int spi_codec_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	return 0;
}

static int spi_codec_hw_free(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	return 0;
}

static int spi_codec_set_sysclk(struct snd_soc_dai *cpu_dai, int stream,
				unsigned int freq, int dir)
{
	return 0;
}

static int spi_codec_set_fmt(struct snd_soc_dai *cpu_dai,
			     unsigned int fmt)
{
	return 0;
}

static int spi_codec_trigger(struct snd_pcm_substream *substream,
			     int cmd, struct snd_soc_dai *dai)
{
	return 0;
}

static const struct snd_soc_dai_ops spi_codec_dai_ops = {
	.startup	= spi_codec_startup,
	.shutdown	= spi_codec_shutdown,
	.hw_params	= spi_codec_hw_params,
	.hw_free	= spi_codec_hw_free,
	.set_sysclk	= spi_codec_set_sysclk,
	.set_fmt	= spi_codec_set_fmt,
	.trigger	= spi_codec_trigger,
};

static struct snd_soc_dai_driver spi_codec_dai = {
	.name = "spi_codec",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 384,
		.rates = SNDRV_PCM_RATE_8000_384000,
		.formats = (SNDRV_PCM_FMTBIT_S8 |
			    SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE |
			    SNDRV_PCM_FMTBIT_S32_LE),
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 384,
		.rates = SNDRV_PCM_RATE_8000_384000,
		.formats = (SNDRV_PCM_FMTBIT_S8 |
			    SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE |
			    SNDRV_PCM_FMTBIT_S32_LE),
	},
	.ops = &spi_codec_dai_ops,
};

static int spi_codec_comp_probe(struct snd_soc_component *component)
{
	struct spi_device *spi = container_of(component->dev, struct spi_device, dev);
	struct spi_codec_private *spi_priv = dev_get_drvdata(&spi->dev);

	snd_soc_component_set_drvdata(component, spi_priv);

	return 0;
}

static void spi_codec_comp_remove(struct snd_soc_component *component)
{
}

static const struct snd_soc_dapm_widget spi_codec_dapm_widgets[] = {
};

static const struct snd_soc_dapm_route spi_codec_dapm_routes[] = {
};

static const struct snd_kcontrol_new spi_codec_snd_controls[] = {
	SPICODEC_CH_VOLUME("CHN0 Playback Vol", 0),
	SPICODEC_CH_VOLUME("CHN1 Playback Vol", 1),
	SPICODEC_CH_VOLUME("CHN2 Playback Vol", 2),
	SPICODEC_CH_VOLUME("CHN3 Playback Vol", 3),
	SPICODEC_CH_VOLUME("CHN4 Playback Vol", 4),
	SPICODEC_CH_VOLUME("CHN5 Playback Vol", 5),
	SPICODEC_CH_VOLUME("CHN6 Playback Vol", 6),
	SPICODEC_CH_VOLUME("CHN7 Playback Vol", 7),
	SPICODEC_CH_MUTE("CHN0 Playback Mute", 0),
	SPICODEC_CH_MUTE("CHN1 Playback Mute", 1),
	SPICODEC_CH_MUTE("CHN2 Playback Mute", 2),
	SPICODEC_CH_MUTE("CHN3 Playback Mute", 3),
	SPICODEC_CH_MUTE("CHN4 Playback Mute", 4),
	SPICODEC_CH_MUTE("CHN5 Playback Mute", 5),
	SPICODEC_CH_MUTE("CHN6 Playback Mute", 6),
	SPICODEC_CH_MUTE("CHN7 Playback Mute", 7),
};

static const struct snd_soc_component_driver spi_codec_component = {
	.probe			= spi_codec_comp_probe,
	.remove			= spi_codec_comp_remove,
	.dapm_widgets		= spi_codec_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(spi_codec_dapm_widgets),
	.dapm_routes		= spi_codec_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(spi_codec_dapm_routes),
	.controls		= spi_codec_snd_controls,
	.num_controls		= ARRAY_SIZE(spi_codec_snd_controls),
};

static int spi_codec_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct spi_codec_private *spi_priv;
	int ret = 0;

	spi_priv = devm_kzalloc(dev, sizeof(*spi_priv), GFP_KERNEL);
	if (!spi_priv)
		return -ENOMEM;

	spi_priv->packet = devm_kzalloc(dev, sizeof(*spi_priv->packet), GFP_KERNEL);
	if (!spi_priv->packet)
		return -ENOMEM;

	spi_priv->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_IN);
	if (!IS_ERR_OR_NULL(spi_priv->reset_gpio)) {
		if (!gpiod_get_value_cansleep(spi_priv->reset_gpio)) {
			dev_err(dev, "the remote dsp should be powered!\n");
			return -EINVAL;
		}
	}

	mutex_init(&spi_priv->lock);

	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(dev, "ERR:fail to setup spi\n");
		return -EINVAL;
	}

	spi_priv->spi = spi;
	spi_priv->dev = dev;
	dev_set_drvdata(dev, spi_priv);

	ret = devm_snd_soc_register_component(dev, &spi_codec_component,
					      &spi_codec_dai, 1);

	return ret;
}

static int spi_codec_remove(struct spi_device *spi)
{
	return 0;
}

static const struct of_device_id spi_codec_device_id[] = {
	{ .compatible = "rockchip,spi-codec", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, spi_codec_device_id);

static struct spi_driver spi_codec_driver = {
	.driver		= {
		.name	= "spi_codec",
		.owner = THIS_MODULE,
			.of_match_table = of_match_ptr(spi_codec_device_id),
	},
	.probe		= spi_codec_probe,
	.remove		= spi_codec_remove,
};
module_spi_driver(spi_codec_driver);

MODULE_AUTHOR("Jun Zeng <jun.zeng@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip SPI Codec Driver");
MODULE_LICENSE("GPL");
