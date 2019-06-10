// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SiRF audio codec driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "sirf-audio-codec.h"

struct sirf_audio_codec {
	struct clk *clk;
	struct regmap *regmap;
	u32 reg_ctrl0, reg_ctrl1;
};

static const char * const input_mode_mux[] = {"Single-ended",
	"Differential"};

static const struct soc_enum input_mode_mux_enum =
	SOC_ENUM_SINGLE(AUDIO_IC_CODEC_CTRL1, 4, 2, input_mode_mux);

static const struct snd_kcontrol_new sirf_audio_codec_input_mode_control =
	SOC_DAPM_ENUM("Route", input_mode_mux_enum);

static const DECLARE_TLV_DB_SCALE(playback_vol_tlv, -12400, 100, 0);
static const DECLARE_TLV_DB_SCALE(capture_vol_tlv_prima2, 500, 100, 0);
static const DECLARE_TLV_DB_RANGE(capture_vol_tlv_atlas6,
	0, 7, TLV_DB_SCALE_ITEM(-100, 100, 0),
	0x22, 0x3F, TLV_DB_SCALE_ITEM(700, 100, 0),
);

static struct snd_kcontrol_new volume_controls_atlas6[] = {
	SOC_DOUBLE_TLV("Playback Volume", AUDIO_IC_CODEC_CTRL0, 21, 14,
			0x7F, 0, playback_vol_tlv),
	SOC_DOUBLE_TLV("Capture Volume", AUDIO_IC_CODEC_CTRL1, 16, 10,
			0x3F, 0, capture_vol_tlv_atlas6),
};

static struct snd_kcontrol_new volume_controls_prima2[] = {
	SOC_DOUBLE_TLV("Speaker Volume", AUDIO_IC_CODEC_CTRL0, 21, 14,
			0x7F, 0, playback_vol_tlv),
	SOC_DOUBLE_TLV("Capture Volume", AUDIO_IC_CODEC_CTRL1, 15, 10,
			0x1F, 0, capture_vol_tlv_prima2),
};

static struct snd_kcontrol_new left_input_path_controls[] = {
	SOC_DAPM_SINGLE("Line Left Switch", AUDIO_IC_CODEC_CTRL1, 6, 1, 0),
	SOC_DAPM_SINGLE("Mic Left Switch", AUDIO_IC_CODEC_CTRL1, 3, 1, 0),
};

static struct snd_kcontrol_new right_input_path_controls[] = {
	SOC_DAPM_SINGLE("Line Right Switch", AUDIO_IC_CODEC_CTRL1, 5, 1, 0),
	SOC_DAPM_SINGLE("Mic Right Switch", AUDIO_IC_CODEC_CTRL1, 2, 1, 0),
};

static struct snd_kcontrol_new left_dac_to_hp_left_amp_switch_control =
	SOC_DAPM_SINGLE("Switch", AUDIO_IC_CODEC_CTRL0, 9, 1, 0);

static struct snd_kcontrol_new left_dac_to_hp_right_amp_switch_control =
	SOC_DAPM_SINGLE("Switch", AUDIO_IC_CODEC_CTRL0, 8, 1, 0);

static struct snd_kcontrol_new right_dac_to_hp_left_amp_switch_control =
	SOC_DAPM_SINGLE("Switch", AUDIO_IC_CODEC_CTRL0, 7, 1, 0);

static struct snd_kcontrol_new right_dac_to_hp_right_amp_switch_control =
	SOC_DAPM_SINGLE("Switch", AUDIO_IC_CODEC_CTRL0, 6, 1, 0);

static struct snd_kcontrol_new left_dac_to_speaker_lineout_switch_control =
	SOC_DAPM_SINGLE("Switch", AUDIO_IC_CODEC_CTRL0, 11, 1, 0);

static struct snd_kcontrol_new right_dac_to_speaker_lineout_switch_control =
	SOC_DAPM_SINGLE("Switch", AUDIO_IC_CODEC_CTRL0, 10, 1, 0);

/* After enable adc, Delay 200ms to avoid pop noise */
static int adc_enable_delay_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		msleep(200);
		break;
	default:
		break;
	}

	return 0;
}

static void enable_and_reset_codec(struct regmap *regmap,
		u32 codec_enable_bits, u32 codec_reset_bits)
{
	regmap_update_bits(regmap, AUDIO_IC_CODEC_CTRL1,
			codec_enable_bits | codec_reset_bits,
			codec_enable_bits);
	msleep(20);
	regmap_update_bits(regmap, AUDIO_IC_CODEC_CTRL1,
			codec_reset_bits, codec_reset_bits);
}

static int atlas6_codec_enable_and_reset_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
#define ATLAS6_CODEC_ENABLE_BITS (1 << 29)
#define ATLAS6_CODEC_RESET_BITS (1 << 28)
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct sirf_audio_codec *sirf_audio_codec = snd_soc_component_get_drvdata(component);
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		enable_and_reset_codec(sirf_audio_codec->regmap,
			ATLAS6_CODEC_ENABLE_BITS, ATLAS6_CODEC_RESET_BITS);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(sirf_audio_codec->regmap,
			AUDIO_IC_CODEC_CTRL1, ATLAS6_CODEC_ENABLE_BITS, 0);
		break;
	default:
		break;
	}

	return 0;
}

static int prima2_codec_enable_and_reset_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
#define PRIMA2_CODEC_ENABLE_BITS (1 << 27)
#define PRIMA2_CODEC_RESET_BITS (1 << 26)
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct sirf_audio_codec *sirf_audio_codec = snd_soc_component_get_drvdata(component);
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		enable_and_reset_codec(sirf_audio_codec->regmap,
			PRIMA2_CODEC_ENABLE_BITS, PRIMA2_CODEC_RESET_BITS);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(sirf_audio_codec->regmap,
			AUDIO_IC_CODEC_CTRL1, PRIMA2_CODEC_ENABLE_BITS, 0);
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget atlas6_output_driver_dapm_widgets[] = {
	SND_SOC_DAPM_OUT_DRV("HP Left Driver", AUDIO_IC_CODEC_CTRL1,
			25, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("HP Right Driver", AUDIO_IC_CODEC_CTRL1,
			26, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("Speaker Driver", AUDIO_IC_CODEC_CTRL1,
			27, 0, NULL, 0),
};

static const struct snd_soc_dapm_widget prima2_output_driver_dapm_widgets[] = {
	SND_SOC_DAPM_OUT_DRV("HP Left Driver", AUDIO_IC_CODEC_CTRL1,
			23, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("HP Right Driver", AUDIO_IC_CODEC_CTRL1,
			24, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("Speaker Driver", AUDIO_IC_CODEC_CTRL1,
			25, 0, NULL, 0),
};

static const struct snd_soc_dapm_widget atlas6_codec_clock_dapm_widget =
	SND_SOC_DAPM_SUPPLY("codecclk", SND_SOC_NOPM, 0, 0,
			atlas6_codec_enable_and_reset_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD);

static const struct snd_soc_dapm_widget prima2_codec_clock_dapm_widget =
	SND_SOC_DAPM_SUPPLY("codecclk", SND_SOC_NOPM, 0, 0,
			prima2_codec_enable_and_reset_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD);

static const struct snd_soc_dapm_widget sirf_audio_codec_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC left", NULL, AUDIO_IC_CODEC_CTRL0, 1, 0),
	SND_SOC_DAPM_DAC("DAC right", NULL, AUDIO_IC_CODEC_CTRL0, 0, 0),
	SND_SOC_DAPM_SWITCH("Left dac to hp left amp", SND_SOC_NOPM, 0, 0,
			&left_dac_to_hp_left_amp_switch_control),
	SND_SOC_DAPM_SWITCH("Left dac to hp right amp", SND_SOC_NOPM, 0, 0,
			&left_dac_to_hp_right_amp_switch_control),
	SND_SOC_DAPM_SWITCH("Right dac to hp left amp", SND_SOC_NOPM, 0, 0,
			&right_dac_to_hp_left_amp_switch_control),
	SND_SOC_DAPM_SWITCH("Right dac to hp right amp", SND_SOC_NOPM, 0, 0,
			&right_dac_to_hp_right_amp_switch_control),
	SND_SOC_DAPM_OUT_DRV("HP amp left driver", AUDIO_IC_CODEC_CTRL0, 3, 0,
			NULL, 0),
	SND_SOC_DAPM_OUT_DRV("HP amp right driver", AUDIO_IC_CODEC_CTRL0, 3, 0,
			NULL, 0),

	SND_SOC_DAPM_SWITCH("Left dac to speaker lineout", SND_SOC_NOPM, 0, 0,
			&left_dac_to_speaker_lineout_switch_control),
	SND_SOC_DAPM_SWITCH("Right dac to speaker lineout", SND_SOC_NOPM, 0, 0,
			&right_dac_to_speaker_lineout_switch_control),
	SND_SOC_DAPM_OUT_DRV("Speaker amp driver", AUDIO_IC_CODEC_CTRL0, 4, 0,
			NULL, 0),

	SND_SOC_DAPM_OUTPUT("HPOUTL"),
	SND_SOC_DAPM_OUTPUT("HPOUTR"),
	SND_SOC_DAPM_OUTPUT("SPKOUT"),

	SND_SOC_DAPM_ADC_E("ADC left", NULL, AUDIO_IC_CODEC_CTRL1, 8, 0,
			adc_enable_delay_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_ADC_E("ADC right", NULL, AUDIO_IC_CODEC_CTRL1, 7, 0,
			adc_enable_delay_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MIXER("Left PGA mixer", AUDIO_IC_CODEC_CTRL1, 1, 0,
		&left_input_path_controls[0],
		ARRAY_SIZE(left_input_path_controls)),
	SND_SOC_DAPM_MIXER("Right PGA mixer", AUDIO_IC_CODEC_CTRL1, 0, 0,
		&right_input_path_controls[0],
		ARRAY_SIZE(right_input_path_controls)),

	SND_SOC_DAPM_MUX("Mic input mode mux", SND_SOC_NOPM, 0, 0,
			&sirf_audio_codec_input_mode_control),
	SND_SOC_DAPM_MICBIAS("Mic Bias", AUDIO_IC_CODEC_PWR, 3, 0),
	SND_SOC_DAPM_INPUT("MICIN1"),
	SND_SOC_DAPM_INPUT("MICIN2"),
	SND_SOC_DAPM_INPUT("LINEIN1"),
	SND_SOC_DAPM_INPUT("LINEIN2"),

	SND_SOC_DAPM_SUPPLY("HSL Phase Opposite", AUDIO_IC_CODEC_CTRL0,
			30, 0, NULL, 0),
};

static const struct snd_soc_dapm_route sirf_audio_codec_map[] = {
	{"SPKOUT", NULL, "Speaker Driver"},
	{"Speaker Driver", NULL, "Speaker amp driver"},
	{"Speaker amp driver", NULL, "Left dac to speaker lineout"},
	{"Speaker amp driver", NULL, "Right dac to speaker lineout"},
	{"Left dac to speaker lineout", "Switch", "DAC left"},
	{"Right dac to speaker lineout", "Switch", "DAC right"},
	{"HPOUTL", NULL, "HP Left Driver"},
	{"HPOUTR", NULL, "HP Right Driver"},
	{"HP Left Driver", NULL, "HP amp left driver"},
	{"HP Right Driver", NULL, "HP amp right driver"},
	{"HP amp left driver", NULL, "Right dac to hp left amp"},
	{"HP amp right driver", NULL , "Right dac to hp right amp"},
	{"HP amp left driver", NULL, "Left dac to hp left amp"},
	{"HP amp right driver", NULL , "Right dac to hp right amp"},
	{"Right dac to hp left amp", "Switch", "DAC left"},
	{"Right dac to hp right amp", "Switch", "DAC right"},
	{"Left dac to hp left amp", "Switch", "DAC left"},
	{"Left dac to hp right amp", "Switch", "DAC right"},
	{"DAC left", NULL, "codecclk"},
	{"DAC right", NULL, "codecclk"},
	{"DAC left", NULL, "Playback"},
	{"DAC right", NULL, "Playback"},
	{"DAC left", NULL, "HSL Phase Opposite"},
	{"DAC right", NULL, "HSL Phase Opposite"},

	{"Capture", NULL, "ADC left"},
	{"Capture", NULL, "ADC right"},
	{"ADC left", NULL, "codecclk"},
	{"ADC right", NULL, "codecclk"},
	{"ADC left", NULL, "Left PGA mixer"},
	{"ADC right", NULL, "Right PGA mixer"},
	{"Left PGA mixer", "Line Left Switch", "LINEIN2"},
	{"Right PGA mixer", "Line Right Switch", "LINEIN1"},
	{"Left PGA mixer", "Mic Left Switch", "MICIN2"},
	{"Right PGA mixer", "Mic Right Switch", "Mic input mode mux"},
	{"Mic input mode mux", "Single-ended", "MICIN1"},
	{"Mic input mode mux", "Differential", "MICIN1"},
};

static void sirf_audio_codec_tx_enable(struct sirf_audio_codec *sirf_audio_codec)
{
	regmap_update_bits(sirf_audio_codec->regmap, AUDIO_PORT_IC_TXFIFO_OP,
		AUDIO_FIFO_RESET, AUDIO_FIFO_RESET);
	regmap_update_bits(sirf_audio_codec->regmap, AUDIO_PORT_IC_TXFIFO_OP,
		AUDIO_FIFO_RESET, ~AUDIO_FIFO_RESET);
	regmap_write(sirf_audio_codec->regmap, AUDIO_PORT_IC_TXFIFO_INT_MSK, 0);
	regmap_write(sirf_audio_codec->regmap, AUDIO_PORT_IC_TXFIFO_OP, 0);
	regmap_update_bits(sirf_audio_codec->regmap, AUDIO_PORT_IC_TXFIFO_OP,
		AUDIO_FIFO_START, AUDIO_FIFO_START);
	regmap_update_bits(sirf_audio_codec->regmap,
		AUDIO_PORT_IC_CODEC_TX_CTRL, IC_TX_ENABLE, IC_TX_ENABLE);
}

static void sirf_audio_codec_tx_disable(struct sirf_audio_codec *sirf_audio_codec)
{
	regmap_write(sirf_audio_codec->regmap, AUDIO_PORT_IC_TXFIFO_OP, 0);
	regmap_update_bits(sirf_audio_codec->regmap,
		AUDIO_PORT_IC_CODEC_TX_CTRL, IC_TX_ENABLE, ~IC_TX_ENABLE);
}

static void sirf_audio_codec_rx_enable(struct sirf_audio_codec *sirf_audio_codec,
	int channels)
{
	regmap_update_bits(sirf_audio_codec->regmap, AUDIO_PORT_IC_RXFIFO_OP,
		AUDIO_FIFO_RESET, AUDIO_FIFO_RESET);
	regmap_update_bits(sirf_audio_codec->regmap, AUDIO_PORT_IC_RXFIFO_OP,
		AUDIO_FIFO_RESET, ~AUDIO_FIFO_RESET);
	regmap_write(sirf_audio_codec->regmap,
		AUDIO_PORT_IC_RXFIFO_INT_MSK, 0);
	regmap_write(sirf_audio_codec->regmap, AUDIO_PORT_IC_RXFIFO_OP, 0);
	regmap_update_bits(sirf_audio_codec->regmap, AUDIO_PORT_IC_RXFIFO_OP,
		AUDIO_FIFO_START, AUDIO_FIFO_START);
	if (channels == 1)
		regmap_update_bits(sirf_audio_codec->regmap,
			AUDIO_PORT_IC_CODEC_RX_CTRL,
			IC_RX_ENABLE_MONO, IC_RX_ENABLE_MONO);
	else
		regmap_update_bits(sirf_audio_codec->regmap,
			AUDIO_PORT_IC_CODEC_RX_CTRL,
			IC_RX_ENABLE_STEREO, IC_RX_ENABLE_STEREO);
}

static void sirf_audio_codec_rx_disable(struct sirf_audio_codec *sirf_audio_codec)
{
	regmap_update_bits(sirf_audio_codec->regmap,
			AUDIO_PORT_IC_CODEC_RX_CTRL,
			IC_RX_ENABLE_STEREO, ~IC_RX_ENABLE_STEREO);
}

static int sirf_audio_codec_trigger(struct snd_pcm_substream *substream,
		int cmd,
		struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct sirf_audio_codec *sirf_audio_codec = snd_soc_component_get_drvdata(component);
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	/*
	 * This is a workaround, When stop playback,
	 * need disable HP amp, avoid the current noise.
	 */
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (playback) {
			snd_soc_component_update_bits(component, AUDIO_IC_CODEC_CTRL0,
				IC_HSLEN | IC_HSREN, 0);
			sirf_audio_codec_tx_disable(sirf_audio_codec);
		} else
			sirf_audio_codec_rx_disable(sirf_audio_codec);
		break;
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (playback) {
			sirf_audio_codec_tx_enable(sirf_audio_codec);
			snd_soc_component_update_bits(component, AUDIO_IC_CODEC_CTRL0,
				IC_HSLEN | IC_HSREN, IC_HSLEN | IC_HSREN);
		} else
			sirf_audio_codec_rx_enable(sirf_audio_codec,
				substream->runtime->channels);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops sirf_audio_codec_dai_ops = {
	.trigger = sirf_audio_codec_trigger,
};

static struct snd_soc_dai_driver sirf_audio_codec_dai = {
	.name = "sirf-audio-codec",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &sirf_audio_codec_dai_ops,
};

static int sirf_audio_codec_probe(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);

	pm_runtime_enable(component->dev);

	if (of_device_is_compatible(component->dev->of_node, "sirf,prima2-audio-codec")) {
		snd_soc_dapm_new_controls(dapm,
			prima2_output_driver_dapm_widgets,
			ARRAY_SIZE(prima2_output_driver_dapm_widgets));
		snd_soc_dapm_new_controls(dapm,
			&prima2_codec_clock_dapm_widget, 1);
		return snd_soc_add_component_controls(component,
			volume_controls_prima2,
			ARRAY_SIZE(volume_controls_prima2));
	}
	if (of_device_is_compatible(component->dev->of_node, "sirf,atlas6-audio-codec")) {
		snd_soc_dapm_new_controls(dapm,
			atlas6_output_driver_dapm_widgets,
			ARRAY_SIZE(atlas6_output_driver_dapm_widgets));
		snd_soc_dapm_new_controls(dapm,
			&atlas6_codec_clock_dapm_widget, 1);
		return snd_soc_add_component_controls(component,
			volume_controls_atlas6,
			ARRAY_SIZE(volume_controls_atlas6));
	}

	return -EINVAL;
}

static void sirf_audio_codec_remove(struct snd_soc_component *component)
{
	pm_runtime_disable(component->dev);
}

static const struct snd_soc_component_driver soc_codec_device_sirf_audio_codec = {
	.probe			= sirf_audio_codec_probe,
	.remove			= sirf_audio_codec_remove,
	.dapm_widgets		= sirf_audio_codec_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sirf_audio_codec_dapm_widgets),
	.dapm_routes		= sirf_audio_codec_map,
	.num_dapm_routes	= ARRAY_SIZE(sirf_audio_codec_map),
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct of_device_id sirf_audio_codec_of_match[] = {
	{ .compatible = "sirf,prima2-audio-codec" },
	{ .compatible = "sirf,atlas6-audio-codec" },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_audio_codec_of_match);

static const struct regmap_config sirf_audio_codec_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = AUDIO_PORT_IC_RXFIFO_INT_MSK,
	.cache_type = REGCACHE_NONE,
};

static int sirf_audio_codec_driver_probe(struct platform_device *pdev)
{
	int ret;
	struct sirf_audio_codec *sirf_audio_codec;
	void __iomem *base;
	struct resource *mem_res;

	sirf_audio_codec = devm_kzalloc(&pdev->dev,
		sizeof(struct sirf_audio_codec), GFP_KERNEL);
	if (!sirf_audio_codec)
		return -ENOMEM;

	platform_set_drvdata(pdev, sirf_audio_codec);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	sirf_audio_codec->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					    &sirf_audio_codec_regmap_config);
	if (IS_ERR(sirf_audio_codec->regmap))
		return PTR_ERR(sirf_audio_codec->regmap);

	sirf_audio_codec->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(sirf_audio_codec->clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		return PTR_ERR(sirf_audio_codec->clk);
	}

	ret = clk_prepare_enable(sirf_audio_codec->clk);
	if (ret) {
		dev_err(&pdev->dev, "Enable clock failed.\n");
		return ret;
	}

	ret = devm_snd_soc_register_component(&(pdev->dev),
			&soc_codec_device_sirf_audio_codec,
			&sirf_audio_codec_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Register Audio Codec dai failed.\n");
		goto err_clk_put;
	}

	/*
	 * Always open charge pump, if not, when the charge pump closed the
	 * adc will not stable
	 */
	regmap_update_bits(sirf_audio_codec->regmap, AUDIO_IC_CODEC_CTRL0,
		IC_CPFREQ, IC_CPFREQ);

	if (of_device_is_compatible(pdev->dev.of_node, "sirf,atlas6-audio-codec"))
		regmap_update_bits(sirf_audio_codec->regmap,
				AUDIO_IC_CODEC_CTRL0, IC_CPEN, IC_CPEN);
	return 0;

err_clk_put:
	clk_disable_unprepare(sirf_audio_codec->clk);
	return ret;
}

static int sirf_audio_codec_driver_remove(struct platform_device *pdev)
{
	struct sirf_audio_codec *sirf_audio_codec = platform_get_drvdata(pdev);

	clk_disable_unprepare(sirf_audio_codec->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sirf_audio_codec_suspend(struct device *dev)
{
	struct sirf_audio_codec *sirf_audio_codec = dev_get_drvdata(dev);

	regmap_read(sirf_audio_codec->regmap, AUDIO_IC_CODEC_CTRL0,
		&sirf_audio_codec->reg_ctrl0);
	regmap_read(sirf_audio_codec->regmap, AUDIO_IC_CODEC_CTRL1,
		&sirf_audio_codec->reg_ctrl1);
	clk_disable_unprepare(sirf_audio_codec->clk);

	return 0;
}

static int sirf_audio_codec_resume(struct device *dev)
{
	struct sirf_audio_codec *sirf_audio_codec = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(sirf_audio_codec->clk);
	if (ret)
		return ret;

	regmap_write(sirf_audio_codec->regmap, AUDIO_IC_CODEC_CTRL0,
		sirf_audio_codec->reg_ctrl0);
	regmap_write(sirf_audio_codec->regmap, AUDIO_IC_CODEC_CTRL1,
		sirf_audio_codec->reg_ctrl1);

	return 0;
}
#endif

static const struct dev_pm_ops sirf_audio_codec_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sirf_audio_codec_suspend, sirf_audio_codec_resume)
};

static struct platform_driver sirf_audio_codec_driver = {
	.driver = {
		.name = "sirf-audio-codec",
		.of_match_table = sirf_audio_codec_of_match,
		.pm = &sirf_audio_codec_pm_ops,
	},
	.probe = sirf_audio_codec_driver_probe,
	.remove = sirf_audio_codec_driver_remove,
};

module_platform_driver(sirf_audio_codec_driver);

MODULE_DESCRIPTION("SiRF audio codec driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
