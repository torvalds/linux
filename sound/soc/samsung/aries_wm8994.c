// SPDX-License-Identifier: GPL-2.0+
#include <linux/extcon.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/consumer.h>
#include <linux/input-event-codes.h>
#include <linux/mfd/wm8994/registers.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "i2s.h"
#include "../codecs/wm8994.h"

#define ARIES_MCLK1_FREQ 24000000

struct aries_wm8994_variant {
	unsigned int modem_dai_fmt;
	bool has_fm_radio;
};

struct aries_wm8994_data {
	struct extcon_dev *usb_extcon;
	struct regulator *reg_main_micbias;
	struct regulator *reg_headset_micbias;
	struct gpio_desc *gpio_headset_detect;
	struct gpio_desc *gpio_headset_key;
	struct gpio_desc *gpio_earpath_sel;
	struct iio_channel *adc;
	const struct aries_wm8994_variant *variant;
};

/* USB dock */
static struct snd_soc_jack aries_dock;

static struct snd_soc_jack_pin dock_pins[] = {
	{
		.pin = "LINE",
		.mask = SND_JACK_LINEOUT,
	},
};

static int aries_extcon_notifier(struct notifier_block *this,
				 unsigned long connected, void *_cmd)
{
	if (connected)
		snd_soc_jack_report(&aries_dock, SND_JACK_LINEOUT,
				SND_JACK_LINEOUT);
	else
		snd_soc_jack_report(&aries_dock, 0, SND_JACK_LINEOUT);

	return NOTIFY_DONE;
}

static struct notifier_block aries_extcon_notifier_block = {
	.notifier_call = aries_extcon_notifier,
};

/* Headset jack */
static struct snd_soc_jack aries_headset;

static struct snd_soc_jack_pin jack_pins[] = {
	{
		.pin = "HP",
		.mask = SND_JACK_HEADPHONE,
	}, {
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
};

static struct snd_soc_jack_zone headset_zones[] = {
	{
		.min_mv = 0,
		.max_mv = 241,
		.jack_type = SND_JACK_HEADPHONE,
	}, {
		.min_mv = 242,
		.max_mv = 2980,
		.jack_type = SND_JACK_HEADSET,
	}, {
		.min_mv = 2981,
		.max_mv = UINT_MAX,
		.jack_type = SND_JACK_HEADPHONE,
	},
};

static irqreturn_t headset_det_irq_thread(int irq, void *data)
{
	struct aries_wm8994_data *priv = (struct aries_wm8994_data *) data;
	int ret = 0;
	int time_left_ms = 300;
	int adc;

	while (time_left_ms > 0) {
		if (!gpiod_get_value(priv->gpio_headset_detect)) {
			snd_soc_jack_report(&aries_headset, 0,
					SND_JACK_HEADSET);
			gpiod_set_value(priv->gpio_earpath_sel, 0);
			return IRQ_HANDLED;
		}
		msleep(20);
		time_left_ms -= 20;
	}

	/* Temporarily enable micbias and earpath selector */
	ret = regulator_enable(priv->reg_headset_micbias);
	if (ret)
		pr_err("%s failed to enable micbias: %d", __func__, ret);

	gpiod_set_value(priv->gpio_earpath_sel, 1);

	ret = iio_read_channel_processed(priv->adc, &adc);
	if (ret < 0) {
		/* failed to read ADC, so assume headphone */
		pr_err("%s failed to read ADC, assuming headphones", __func__);
		snd_soc_jack_report(&aries_headset, SND_JACK_HEADPHONE,
				SND_JACK_HEADSET);
	} else {
		snd_soc_jack_report(&aries_headset,
				snd_soc_jack_get_type(&aries_headset, adc),
				SND_JACK_HEADSET);
	}

	ret = regulator_disable(priv->reg_headset_micbias);
	if (ret)
		pr_err("%s failed disable micbias: %d", __func__, ret);

	/* Disable earpath selector when no mic connected */
	if (!(aries_headset.status & SND_JACK_MICROPHONE))
		gpiod_set_value(priv->gpio_earpath_sel, 0);

	return IRQ_HANDLED;
}

static int headset_button_check(void *data)
{
	struct aries_wm8994_data *priv = (struct aries_wm8994_data *) data;

	/* Filter out keypresses when 4 pole jack not detected */
	if (gpiod_get_value_cansleep(priv->gpio_headset_key) &&
			aries_headset.status & SND_JACK_MICROPHONE)
		return SND_JACK_BTN_0;

	return 0;
}

static struct snd_soc_jack_gpio headset_button_gpio[] = {
	{
		.name = "Media Button",
		.report = SND_JACK_BTN_0,
		.debounce_time  = 30,
		.jack_status_check = headset_button_check,
	},
};

static int aries_spk_cfg(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_card *card = w->dapm->card;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_component *component;
	int ret = 0;

	rtd = snd_soc_get_pcm_runtime(card, &card->dai_link[0]);
	component = snd_soc_rtd_to_codec(rtd, 0)->component;

	/**
	 * We have an odd setup - the SPKMODE pin is pulled up so
	 * we only have access to the left side SPK configs,
	 * but SPKOUTR isn't bridged so when playing back in
	 * stereo, we only get the left hand channel.  The only
	 * option we're left with is to force the AIF into mono
	 * mode.
	 */
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		ret = snd_soc_component_update_bits(component,
				WM8994_AIF1_DAC1_FILTERS_1,
				WM8994_AIF1DAC1_MONO, WM8994_AIF1DAC1_MONO);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		ret = snd_soc_component_update_bits(component,
				WM8994_AIF1_DAC1_FILTERS_1,
				WM8994_AIF1DAC1_MONO, 0);
		break;
	}

	return ret;
}

static int aries_main_bias(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_card *card = w->dapm->card;
	struct aries_wm8994_data *priv = snd_soc_card_get_drvdata(card);
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = regulator_enable(priv->reg_main_micbias);
		break;
	case SND_SOC_DAPM_POST_PMD:
		ret = regulator_disable(priv->reg_main_micbias);
		break;
	}

	return ret;
}

static int aries_headset_bias(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_card *card = w->dapm->card;
	struct aries_wm8994_data *priv = snd_soc_card_get_drvdata(card);
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = regulator_enable(priv->reg_headset_micbias);
		break;
	case SND_SOC_DAPM_POST_PMD:
		ret = regulator_disable(priv->reg_headset_micbias);
		break;
	}

	return ret;
}

static const struct snd_kcontrol_new aries_controls[] = {
	SOC_DAPM_PIN_SWITCH("Modem In"),
	SOC_DAPM_PIN_SWITCH("Modem Out"),
};

static const struct snd_soc_dapm_widget aries_dapm_widgets[] = {
	SND_SOC_DAPM_HP("HP", NULL),

	SND_SOC_DAPM_SPK("SPK", aries_spk_cfg),
	SND_SOC_DAPM_SPK("RCV", NULL),

	SND_SOC_DAPM_LINE("LINE", NULL),

	SND_SOC_DAPM_MIC("Main Mic", aries_main_bias),
	SND_SOC_DAPM_MIC("Headset Mic", aries_headset_bias),

	SND_SOC_DAPM_MIC("Bluetooth Mic", NULL),
	SND_SOC_DAPM_SPK("Bluetooth SPK", NULL),

	SND_SOC_DAPM_LINE("Modem In", NULL),
	SND_SOC_DAPM_LINE("Modem Out", NULL),

	/* This must be last as it is conditionally not used */
	SND_SOC_DAPM_LINE("FM In", NULL),
};

static int aries_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int pll_out;
	int ret;

	/* AIF1CLK should be >=3MHz for optimal performance */
	if (params_width(params) == 24)
		pll_out = params_rate(params) * 384;
	else if (params_rate(params) == 8000 || params_rate(params) == 11025)
		pll_out = params_rate(params) * 512;
	else
		pll_out = params_rate(params) * 256;

	ret = snd_soc_dai_set_pll(codec_dai, WM8994_FLL1, WM8994_FLL_SRC_MCLK1,
				ARIES_MCLK1_FREQ, pll_out);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_FLL1,
				pll_out, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

static int aries_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int ret;

	/* Switch sysclk to MCLK1 */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_MCLK1,
				ARIES_MCLK1_FREQ, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* Stop PLL */
	ret = snd_soc_dai_set_pll(codec_dai, WM8994_FLL1, WM8994_FLL_SRC_MCLK1,
				ARIES_MCLK1_FREQ, 0);
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * Main DAI operations
 */
static const struct snd_soc_ops aries_ops = {
	.hw_params = aries_hw_params,
	.hw_free = aries_hw_free,
};

static int aries_baseband_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int pll_out;
	int ret;

	pll_out = 8000 * 512;

	/* Set the codec FLL */
	ret = snd_soc_dai_set_pll(codec_dai, WM8994_FLL2, WM8994_FLL_SRC_MCLK1,
			ARIES_MCLK1_FREQ, pll_out);
	if (ret < 0)
		return ret;

	/* Set the codec system clock */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_FLL2,
			pll_out, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

static int aries_late_probe(struct snd_soc_card *card)
{
	struct aries_wm8994_data *priv = snd_soc_card_get_drvdata(card);
	int ret, irq;

	ret = snd_soc_card_jack_new_pins(card, "Dock", SND_JACK_LINEOUT,
			&aries_dock, dock_pins, ARRAY_SIZE(dock_pins));
	if (ret)
		return ret;

	ret = devm_extcon_register_notifier(card->dev,
			priv->usb_extcon, EXTCON_JACK_LINE_OUT,
			&aries_extcon_notifier_block);
	if (ret)
		return ret;

	if (extcon_get_state(priv->usb_extcon,
			EXTCON_JACK_LINE_OUT) > 0)
		snd_soc_jack_report(&aries_dock, SND_JACK_LINEOUT,
				SND_JACK_LINEOUT);
	else
		snd_soc_jack_report(&aries_dock, 0, SND_JACK_LINEOUT);

	ret = snd_soc_card_jack_new_pins(card, "Headset",
			SND_JACK_HEADSET | SND_JACK_BTN_0,
			&aries_headset,
			jack_pins, ARRAY_SIZE(jack_pins));
	if (ret)
		return ret;

	ret = snd_soc_jack_add_zones(&aries_headset, ARRAY_SIZE(headset_zones),
			headset_zones);
	if (ret)
		return ret;

	irq = gpiod_to_irq(priv->gpio_headset_detect);
	if (irq < 0) {
		dev_err(card->dev, "Failed to map headset detect gpio to irq");
		return -EINVAL;
	}

	ret = devm_request_threaded_irq(card->dev, irq, NULL,
			headset_det_irq_thread,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
			IRQF_ONESHOT, "headset_detect", priv);
	if (ret) {
		dev_err(card->dev, "Failed to request headset detect irq");
		return ret;
	}

	headset_button_gpio[0].data = priv;
	headset_button_gpio[0].desc = priv->gpio_headset_key;

	snd_jack_set_key(aries_headset.jack, SND_JACK_BTN_0, KEY_MEDIA);

	return snd_soc_jack_add_gpios(&aries_headset,
			ARRAY_SIZE(headset_button_gpio), headset_button_gpio);
}

static const struct snd_soc_pcm_stream baseband_params = {
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rate_min = 8000,
	.rate_max = 8000,
	.channels_min = 1,
	.channels_max = 1,
};

static const struct snd_soc_pcm_stream bluetooth_params = {
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rate_min = 8000,
	.rate_max = 8000,
	.channels_min = 1,
	.channels_max = 2,
};

static const struct snd_soc_dapm_widget aries_modem_widgets[] = {
	SND_SOC_DAPM_INPUT("Modem RX"),
	SND_SOC_DAPM_OUTPUT("Modem TX"),
};

static const struct snd_soc_dapm_route aries_modem_routes[] = {
	{ "Modem Capture", NULL, "Modem RX" },
	{ "Modem TX", NULL, "Modem Playback" },
};

static const struct snd_soc_component_driver aries_component = {
	.name			= "aries-audio",
	.dapm_widgets		= aries_modem_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(aries_modem_widgets),
	.dapm_routes		= aries_modem_routes,
	.num_dapm_routes	= ARRAY_SIZE(aries_modem_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static struct snd_soc_dai_driver aries_ext_dai[] = {
	{
		.name = "Voice call",
		.playback = {
			.stream_name = "Modem Playback",
			.channels_min = 1,
			.channels_max = 1,
			.rate_min = 8000,
			.rate_max = 8000,
			.rates = SNDRV_PCM_RATE_8000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.stream_name = "Modem Capture",
			.channels_min = 1,
			.channels_max = 1,
			.rate_min = 8000,
			.rate_max = 8000,
			.rates = SNDRV_PCM_RATE_8000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
	},
};

SND_SOC_DAILINK_DEFS(aif1,
	DAILINK_COMP_ARRAY(COMP_CPU(SAMSUNG_I2S_DAI)),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "wm8994-aif1")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(baseband,
	DAILINK_COMP_ARRAY(COMP_CPU("Voice call")),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "wm8994-aif2")));

SND_SOC_DAILINK_DEFS(bluetooth,
	DAILINK_COMP_ARRAY(COMP_CPU("bt-sco-pcm")),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "wm8994-aif3")));

static struct snd_soc_dai_link aries_dai[] = {
	{
		.name = "WM8994 AIF1",
		.stream_name = "HiFi",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBP_CFP,
		.ops = &aries_ops,
		SND_SOC_DAILINK_REG(aif1),
	},
	{
		.name = "WM8994 AIF2",
		.stream_name = "Baseband",
		.init = &aries_baseband_init,
		.c2c_params = &baseband_params,
		.num_c2c_params = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(baseband),
	},
	{
		.name = "WM8994 AIF3",
		.stream_name = "Bluetooth",
		.c2c_params = &bluetooth_params,
		.num_c2c_params = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(bluetooth),
	},
};

static struct snd_soc_card aries_card = {
	.name = "ARIES",
	.owner = THIS_MODULE,
	.dai_link = aries_dai,
	.num_links = ARRAY_SIZE(aries_dai),
	.controls = aries_controls,
	.num_controls = ARRAY_SIZE(aries_controls),
	.dapm_widgets = aries_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(aries_dapm_widgets),
	.late_probe = aries_late_probe,
};

static const struct aries_wm8994_variant fascinate4g_variant = {
	.modem_dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBC_CFC
		| SND_SOC_DAIFMT_IB_NF,
	.has_fm_radio = false,
};

static const struct aries_wm8994_variant aries_variant = {
	.modem_dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBP_CFP
		| SND_SOC_DAIFMT_IB_NF,
	.has_fm_radio = true,
};

static const struct of_device_id samsung_wm8994_of_match[] = {
	{
		.compatible = "samsung,fascinate4g-wm8994",
		.data = &fascinate4g_variant,
	},
	{
		.compatible = "samsung,aries-wm8994",
		.data = &aries_variant,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, samsung_wm8994_of_match);

static int aries_audio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *cpu, *codec, *extcon_np;
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card = &aries_card;
	struct aries_wm8994_data *priv;
	struct snd_soc_dai_link *dai_link;
	const struct of_device_id *match;
	enum iio_chan_type channel_type;
	int ret, i;

	if (!np)
		return -EINVAL;

	card->dev = dev;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	snd_soc_card_set_drvdata(card, priv);

	match = of_match_node(samsung_wm8994_of_match, np);
	priv->variant = match->data;

	/* Remove FM widget if not present */
	if (!priv->variant->has_fm_radio)
		card->num_dapm_widgets--;

	priv->reg_main_micbias = devm_regulator_get(dev, "main-micbias");
	if (IS_ERR(priv->reg_main_micbias)) {
		dev_err(dev, "Failed to get main micbias regulator\n");
		return PTR_ERR(priv->reg_main_micbias);
	}

	priv->reg_headset_micbias = devm_regulator_get(dev, "headset-micbias");
	if (IS_ERR(priv->reg_headset_micbias)) {
		dev_err(dev, "Failed to get headset micbias regulator\n");
		return PTR_ERR(priv->reg_headset_micbias);
	}

	priv->gpio_earpath_sel = devm_gpiod_get(dev, "earpath-sel",
			GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpio_earpath_sel)) {
		dev_err(dev, "Failed to get earpath selector gpio");
		return PTR_ERR(priv->gpio_earpath_sel);
	}

	extcon_np = of_parse_phandle(np, "extcon", 0);
	priv->usb_extcon = extcon_find_edev_by_node(extcon_np);
	of_node_put(extcon_np);
	if (IS_ERR(priv->usb_extcon))
		return dev_err_probe(dev, PTR_ERR(priv->usb_extcon),
				     "Failed to get extcon device");

	priv->adc = devm_iio_channel_get(dev, "headset-detect");
	if (IS_ERR(priv->adc))
		return dev_err_probe(dev, PTR_ERR(priv->adc),
				     "Failed to get ADC channel");

	ret = iio_get_channel_type(priv->adc, &channel_type);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to get ADC channel type");
	if (channel_type != IIO_VOLTAGE)
		return -EINVAL;

	priv->gpio_headset_key = devm_gpiod_get(dev, "headset-key",
			GPIOD_IN);
	if (IS_ERR(priv->gpio_headset_key)) {
		dev_err(dev, "Failed to get headset key gpio");
		return PTR_ERR(priv->gpio_headset_key);
	}

	priv->gpio_headset_detect = devm_gpiod_get(dev,
			"headset-detect", GPIOD_IN);
	if (IS_ERR(priv->gpio_headset_detect)) {
		dev_err(dev, "Failed to get headset detect gpio");
		return PTR_ERR(priv->gpio_headset_detect);
	}

	/* Update card-name if provided through DT, else use default name */
	snd_soc_of_parse_card_name(card, "model");

	ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
	if (ret < 0) {
		/* Backwards compatible way */
		ret = snd_soc_of_parse_audio_routing(card, "samsung,audio-routing");
		if (ret < 0) {
			dev_err(dev, "Audio routing invalid/unspecified\n");
			return ret;
		}
	}

	aries_dai[1].dai_fmt = priv->variant->modem_dai_fmt;

	cpu = of_get_child_by_name(dev->of_node, "cpu");
	if (!cpu)
		return -EINVAL;

	codec = of_get_child_by_name(dev->of_node, "codec");
	if (!codec) {
		ret = -EINVAL;
		goto out;
	}

	for_each_card_prelinks(card, i, dai_link) {
		dai_link->codecs->of_node = of_parse_phandle(codec,
				"sound-dai", 0);
		if (!dai_link->codecs->of_node) {
			ret = -EINVAL;
			goto out;
		}
	}

	/* Set CPU and platform of_node for main DAI */
	aries_dai[0].cpus->of_node = of_parse_phandle(cpu,
			"sound-dai", 0);
	if (!aries_dai[0].cpus->of_node) {
		ret = -EINVAL;
		goto out;
	}

	aries_dai[0].platforms->of_node = aries_dai[0].cpus->of_node;

	/* Set CPU of_node for BT DAI */
	aries_dai[2].cpus->of_node = of_parse_phandle(cpu,
			"sound-dai", 1);
	if (!aries_dai[2].cpus->of_node) {
		ret = -EINVAL;
		goto out;
	}

	ret = devm_snd_soc_register_component(dev, &aries_component,
				aries_ext_dai, ARRAY_SIZE(aries_ext_dai));
	if (ret < 0) {
		dev_err(dev, "Failed to register component: %d\n", ret);
		goto out;
	}

	ret = devm_snd_soc_register_card(dev, card);
	if (ret)
		dev_err(dev, "snd_soc_register_card() failed:%d\n", ret);

out:
	of_node_put(cpu);
	of_node_put(codec);

	return ret;
}

static struct platform_driver aries_audio_driver = {
	.driver		= {
		.name	= "aries-audio-wm8994",
		.of_match_table = of_match_ptr(samsung_wm8994_of_match),
		.pm	= &snd_soc_pm_ops,
	},
	.probe		= aries_audio_probe,
};

module_platform_driver(aries_audio_driver);

MODULE_DESCRIPTION("ALSA SoC ARIES WM8994");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:aries-audio-wm8994");
