// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2012-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2020, Stephan Gerhold

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <dt-bindings/sound/qcom,q6afe.h>
#include <dt-bindings/sound/qcom,q6voice.h>
#include "q6voice.h"

#define DRV_NAME	"q6voice-dai"

static int q6voice_dai_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct q6voice *v = snd_soc_dai_get_drvdata(dai);

	return q6voice_start(v, Q6VOICE_PATH_VOICE, substream->stream);
}

static void q6voice_dai_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct q6voice *v = snd_soc_dai_get_drvdata(dai);

	q6voice_stop(v, Q6VOICE_PATH_VOICE, substream->stream);
}

static struct snd_soc_dai_ops q6voice_dai_ops = {
	.startup = q6voice_dai_startup,
	.shutdown = q6voice_dai_shutdown,
};

static struct snd_soc_dai_driver q6voice_dais[] = {
	{
		.id = CS_VOICE,
		.name = "CS-VOICE",
		/* The constraints here are not really meaningful... */
		.playback = {
			.stream_name =	"CS-VOICE Playback",
			.formats =	SNDRV_PCM_FMTBIT_S16_LE,
			.rates =	SNDRV_PCM_RATE_8000,
			.rate_min =	8000,
			.rate_max =	8000,
			.channels_min =	1,
			.channels_max =	1,
		},
		.capture = {
			.stream_name =	"CS-VOICE Capture",
			.formats =	SNDRV_PCM_FMTBIT_S16_LE,
			.rates =	SNDRV_PCM_RATE_8000,
			.rate_min =	8000,
			.rate_max =	8000,
			.channels_min =	1,
			.channels_max =	1,
		},
		.ops = &q6voice_dai_ops,
	},
};

/* FIXME: Use codec2codec instead */
static struct snd_pcm_hardware q6voice_dai_hardware = {
	.info =			SNDRV_PCM_INFO_INTERLEAVED,
	.buffer_bytes_max =	4096 * 2,
	.period_bytes_min =	2048,
	.period_bytes_max =	4096,
	.periods_min =		2,
	.periods_max =		4,
	.fifo_size =		0,
};

static int q6voice_dai_open(struct snd_soc_component *component,
			    struct snd_pcm_substream *substream)
{
	substream->runtime->hw = q6voice_dai_hardware;
	return 0;
}

static int q6voice_get_mixer(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_component *c = snd_soc_dapm_to_component(dapm);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct q6voice *v = snd_soc_component_get_drvdata(c);
	bool capture = !!mc->shift;

	ucontrol->value.integer.value[0] =
		q6voice_get_port(v, Q6VOICE_PATH_VOICE, capture) == mc->reg;
	return 0;
}

static int q6voice_put_mixer(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_component *c = snd_soc_dapm_to_component(dapm);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct q6voice *v = snd_soc_component_get_drvdata(c);
	bool val = !!ucontrol->value.integer.value[0];
	bool capture = !!mc->shift;

	if (val)
		q6voice_set_port(v, Q6VOICE_PATH_VOICE, capture, mc->reg);
	else if (q6voice_get_port(v, Q6VOICE_PATH_VOICE, capture) == mc->reg)
		q6voice_set_port(v, Q6VOICE_PATH_VOICE, capture, 0);

	snd_soc_dapm_mixer_update_power(dapm, kcontrol, val, NULL);
	return 1;
}

static const struct snd_kcontrol_new voice_tx_mixer_controls[] = {
	SOC_SINGLE_EXT("PRI_MI2S_TX", PRIMARY_MI2S_TX, 1, 1, 0,
		       q6voice_get_mixer, q6voice_put_mixer),
	SOC_SINGLE_EXT("SEC_MI2S_TX", SECONDARY_MI2S_TX, 1, 1, 0,
		       q6voice_get_mixer, q6voice_put_mixer),
	SOC_SINGLE_EXT("TERT_MI2S_TX", TERTIARY_MI2S_TX, 1, 1, 0,
		       q6voice_get_mixer, q6voice_put_mixer),
	SOC_SINGLE_EXT("QUAT_MI2S_TX", QUATERNARY_MI2S_TX, 1, 1, 0,
		       q6voice_get_mixer, q6voice_put_mixer),
	SOC_SINGLE_EXT("QUIN_MI2S_TX", QUINARY_MI2S_TX, 1, 1, 0,
		       q6voice_get_mixer, q6voice_put_mixer),
};

static const struct snd_kcontrol_new primary_mi2s_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("CS-Voice", PRIMARY_MI2S_RX, 0, 1, 0,
		       q6voice_get_mixer, q6voice_put_mixer)
};

static const struct snd_kcontrol_new secondary_mi2s_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("CS-Voice", SECONDARY_MI2S_RX, 0, 1, 0,
		       q6voice_get_mixer, q6voice_put_mixer)
};

static const struct snd_kcontrol_new tertiary_mi2s_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("CS-Voice", TERTIARY_MI2S_RX, 0, 1, 0,
		       q6voice_get_mixer, q6voice_put_mixer)
};

static const struct snd_kcontrol_new quaternary_mi2s_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("CS-Voice", QUATERNARY_MI2S_RX, 0, 1, 0,
		       q6voice_get_mixer, q6voice_put_mixer)
};

static const struct snd_kcontrol_new quinary_mi2s_rx_mixer_controls[] = {
	SOC_SINGLE_EXT("CS-Voice", QUINARY_MI2S_RX, 0, 1, 0,
		       q6voice_get_mixer, q6voice_put_mixer)
};

static const struct snd_soc_dapm_widget q6voice_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("CS-VOICE_DL1", "CS-VOICE Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("CS-VOICE_UL1", "CS-VOICE Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_MIXER("CS-Voice Capture Mixer", SND_SOC_NOPM, 0, 0,
			   voice_tx_mixer_controls,
			   ARRAY_SIZE(voice_tx_mixer_controls)),
	SND_SOC_DAPM_MIXER("PRI_MI2S_RX Voice Mixer", SND_SOC_NOPM, 0, 0,
			   primary_mi2s_rx_mixer_controls,
			   ARRAY_SIZE(primary_mi2s_rx_mixer_controls)),
	SND_SOC_DAPM_MIXER("SEC_MI2S_RX Voice Mixer", SND_SOC_NOPM, 0, 0,
			   secondary_mi2s_rx_mixer_controls,
			   ARRAY_SIZE(secondary_mi2s_rx_mixer_controls)),
	SND_SOC_DAPM_MIXER("TERT_MI2S_RX Voice Mixer", SND_SOC_NOPM, 0, 0,
			   tertiary_mi2s_rx_mixer_controls,
			   ARRAY_SIZE(tertiary_mi2s_rx_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUAT_MI2S_RX Voice Mixer", SND_SOC_NOPM, 0, 0,
			   quaternary_mi2s_rx_mixer_controls,
			   ARRAY_SIZE(quaternary_mi2s_rx_mixer_controls)),
	SND_SOC_DAPM_MIXER("QUIN_MI2S_RX Voice Mixer", SND_SOC_NOPM, 0, 0,
			   quinary_mi2s_rx_mixer_controls,
			   ARRAY_SIZE(quinary_mi2s_rx_mixer_controls)),
};

static const struct snd_soc_dapm_route q6voice_dapm_routes[] = {
	{ "CS-Voice Capture Mixer",	"PRI_MI2S_TX",	"PRI_MI2S_TX" },
	{ "CS-Voice Capture Mixer",	"SEC_MI2S_TX",	"SEC_MI2S_TX" },
	{ "CS-Voice Capture Mixer",	"TERT_MI2S_TX",	"TERT_MI2S_TX" },
	{ "CS-Voice Capture Mixer",	"QUAT_MI2S_TX",	"QUAT_MI2S_TX" },
	{ "CS-Voice Capture Mixer",	"QUIN_MI2S_TX",	"QUIN_MI2S_TX" },
	{ "CS-VOICE_UL1",		NULL,		"CS-Voice Capture Mixer" },

	{ "PRI_MI2S_RX Voice Mixer",	"CS-Voice",	"CS-VOICE_DL1" },
	{ "SEC_MI2S_RX Voice Mixer",	"CS-Voice",	"CS-VOICE_DL1" },
	{ "TERT_MI2S_RX Voice Mixer",	"CS-Voice",	"CS-VOICE_DL1" },
	{ "QUAT_MI2S_RX Voice Mixer",	"CS-Voice",	"CS-VOICE_DL1" },
	{ "QUIN_MI2S_RX Voice Mixer",	"CS-Voice",	"CS-VOICE_DL1" },
	{ "PRI_MI2S_RX",		NULL,		"PRI_MI2S_RX Voice Mixer" },
	{ "SEC_MI2S_RX",		NULL,		"SEC_MI2S_RX Voice Mixer" },
	{ "TERT_MI2S_RX",		NULL,		"TERT_MI2S_RX Voice Mixer" },
	{ "QUAT_MI2S_RX",		NULL,		"QUAT_MI2S_RX Voice Mixer" },
	{ "QUIN_MI2S_RX",		NULL,		"QUIN_MI2S_RX Voice Mixer" },
};

static unsigned int q6voice_reg_read(struct snd_soc_component *component,
				     unsigned int reg)
{
	/* default value */
	return 0;
}

static int q6voice_reg_write(struct snd_soc_component *component,
			     unsigned int reg, unsigned int val)
{
	/* dummy */
	return 0;
}

static const struct snd_soc_component_driver q6voice_dai_component = {
	.name = DRV_NAME,
	.open = q6voice_dai_open,

	.dapm_widgets = q6voice_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(q6voice_dapm_widgets),
	.dapm_routes = q6voice_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(q6voice_dapm_routes),
	.read = q6voice_reg_read,
	.write = q6voice_reg_write,

	/* Needs to probe after q6afe */
	.probe_order = SND_SOC_COMP_ORDER_LATE,
};

static int q6voice_dai_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct q6voice *v;

	v = q6voice_create(dev);
	if (IS_ERR(v))
		return PTR_ERR(v);

	dev_set_drvdata(dev, v);

	return devm_snd_soc_register_component(dev, &q6voice_dai_component,
					       q6voice_dais,
					       ARRAY_SIZE(q6voice_dais));
}

static const struct of_device_id q6voice_dai_device_id[] = {
	{ .compatible = "qcom,q6voice-dais" },
	{},
};
MODULE_DEVICE_TABLE(of, q6voice_dai_device_id);

static struct platform_driver q6voice_dai_platform_driver = {
	.driver = {
		.name = "q6voice-dai",
		.of_match_table = of_match_ptr(q6voice_dai_device_id),
	},
	.probe = q6voice_dai_probe,
};
module_platform_driver(q6voice_dai_platform_driver);

MODULE_AUTHOR("Stephan Gerhold <stephan@gerhold.net>");
MODULE_DESCRIPTION("Q6Voice DAI driver");
MODULE_LICENSE("GPL v2");
