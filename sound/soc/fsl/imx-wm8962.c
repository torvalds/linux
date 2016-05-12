/*
 * Copyright (C) 2013-2016 Freescale Semiconductor, Inc.
 *
 * Based on imx-sgtl5000.c
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 * Copyright (C) 2012 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/control.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <linux/pinctrl/consumer.h>

#include "../codecs/wm8962.h"
#include "imx-audmux.h"

#define DAI_NAME_SIZE	32

struct imx_wm8962_data {
	struct snd_soc_dai_link dai[3];
	struct snd_soc_card card;
	char codec_dai_name[DAI_NAME_SIZE];
	char platform_name[DAI_NAME_SIZE];
	unsigned int clk_frequency;
	bool is_codec_master;
};

struct imx_priv {
	int hp_gpio;
	int hp_active_low;
	int mic_gpio;
	int mic_active_low;
	bool amic_mono;
	bool dmic_mono;
	struct snd_soc_codec *codec;
	struct platform_device *pdev;
	struct snd_pcm_substream *first_stream;
	struct snd_pcm_substream *second_stream;
	struct snd_kcontrol *headphone_kctl;
	struct snd_card *snd_card;
	struct platform_device *asrc_pdev;
	u32 asrc_rate;
	u32 asrc_format;
};
static struct imx_priv card_priv;

#ifdef CONFIG_SND_SOC_IMX_WM8962_ANDROID
static int sample_rate = 44100;
static snd_pcm_format_t sample_format = SNDRV_PCM_FORMAT_S16_LE;
#endif

static struct snd_soc_jack imx_hp_jack;
static struct snd_soc_jack_pin imx_hp_jack_pins[] = {
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
	},
};
static struct snd_soc_jack_gpio imx_hp_jack_gpio = {
	.name = "headphone detect",
	.report = SND_JACK_HEADPHONE,
	.debounce_time = 250,
	.invert = 0,
};

static struct snd_soc_jack imx_mic_jack;
static struct snd_soc_jack_pin imx_mic_jack_pins[] = {
	{
		.pin = "AMIC",
		.mask = SND_JACK_MICROPHONE,
	},
};
static struct snd_soc_jack_gpio imx_mic_jack_gpio = {
	.name = "microphone detect",
	.report = SND_JACK_MICROPHONE,
	.debounce_time = 250,
	.invert = 0,
};

static int hpjack_status_check(void *data)
{
	struct imx_priv *priv = &card_priv;
	struct platform_device *pdev = priv->pdev;
	char *envp[3], *buf;
	int hp_status, ret;

	if (!gpio_is_valid(priv->hp_gpio))
		return 0;

	hp_status = gpio_get_value(priv->hp_gpio) ? 1 : 0;

	buf = kmalloc(32, GFP_ATOMIC);
	if (!buf) {
		dev_err(&pdev->dev, "%s kmalloc failed\n", __func__);
		return -ENOMEM;
	}

	if (hp_status != priv->hp_active_low) {
		snprintf(buf, 32, "STATE=%d", 2);
		snd_soc_dapm_disable_pin(&priv->codec->dapm, "Ext Spk");
		ret = imx_hp_jack_gpio.report;
		snd_kctl_jack_report(priv->snd_card, priv->headphone_kctl, 1);
	} else {
		snprintf(buf, 32, "STATE=%d", 0);
		snd_soc_dapm_enable_pin(&priv->codec->dapm, "Ext Spk");
		ret = 0;
		snd_kctl_jack_report(priv->snd_card, priv->headphone_kctl, 0);
	}

	envp[0] = "NAME=headphone";
	envp[1] = buf;
	envp[2] = NULL;
	kobject_uevent_env(&pdev->dev.kobj, KOBJ_CHANGE, envp);
	kfree(buf);

	return ret;
}

static int micjack_status_check(void *data)
{
	struct imx_priv *priv = &card_priv;
	struct platform_device *pdev = priv->pdev;
	char *envp[3], *buf;
	int mic_status, ret;

	if (!gpio_is_valid(priv->mic_gpio))
		return 0;

	mic_status = gpio_get_value(priv->mic_gpio) ? 1 : 0;

	if ((mic_status != priv->mic_active_low && priv->amic_mono)
		|| (mic_status == priv->mic_active_low && priv->dmic_mono))
		snd_soc_update_bits(priv->codec, WM8962_THREED1,
				WM8962_ADC_MONOMIX_MASK, WM8962_ADC_MONOMIX);
	else
		snd_soc_update_bits(priv->codec, WM8962_THREED1,
				WM8962_ADC_MONOMIX_MASK, 0);

	buf = kmalloc(32, GFP_ATOMIC);
	if (!buf) {
		dev_err(&pdev->dev, "%s kmalloc failed\n", __func__);
		return -ENOMEM;
	}

	if (mic_status != priv->mic_active_low) {
		snprintf(buf, 32, "STATE=%d", 2);
		snd_soc_dapm_disable_pin(&priv->codec->dapm, "DMIC");
		ret = imx_mic_jack_gpio.report;
	} else {
		snprintf(buf, 32, "STATE=%d", 0);
		snd_soc_dapm_enable_pin(&priv->codec->dapm, "DMIC");
		ret = 0;
	}

	envp[0] = "NAME=microphone";
	envp[1] = buf;
	envp[2] = NULL;
	kobject_uevent_env(&pdev->dev.kobj, KOBJ_CHANGE, envp);
	kfree(buf);

	return ret;
}


static const struct snd_soc_dapm_widget imx_wm8962_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_MIC("AMIC", NULL),
	SND_SOC_DAPM_MIC("DMIC", NULL),
};

static u32 imx_wm8962_rates[] = {32000, 48000, 96000};
static struct snd_pcm_hw_constraint_list imx_wm8962_rate_constraints = {
	.count = ARRAY_SIZE(imx_wm8962_rates),
	.list = imx_wm8962_rates,
};

static int imx_hifi_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct imx_wm8962_data *data = snd_soc_card_get_drvdata(card);
	int ret;

	if (!data->is_codec_master) {
		ret = snd_pcm_hw_constraint_list(runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE,
				&imx_wm8962_rate_constraints);
		if (ret)
			return ret;
	}

	return 0;
}

#ifdef CONFIG_SND_SOC_IMX_WM8962_ANDROID
static int imx_hifi_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct imx_priv *priv = &card_priv;
	struct device *dev = &priv->pdev->dev;
	u32 dai_format;
	int ret = 0;

	sample_rate = params_rate(params);
	sample_format = params_format(params);

	if (data->is_codec_master)
		dai_format = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBM_CFM;
	else
		dai_format = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, dai_format);
	if (ret) {
		dev_err(dev, "failed to set codec dai fmt: %d\n", ret);
		return ret;
	}

	return 0;
}

static struct snd_soc_ops imx_hifi_ops = {
	.startup = imx_hifi_startup,
	.hw_params = imx_hifi_hw_params,
};

static int imx_wm8962_set_bias_level(struct snd_soc_card *card,
					struct snd_soc_dapm_context *dapm,
					enum snd_soc_bias_level level)
{
	struct snd_soc_dai *codec_dai = card->rtd[0].codec_dai;
	struct imx_priv *priv = &card_priv;
	struct imx_wm8962_data *data = snd_soc_card_get_drvdata(card);
	struct device *dev = &priv->pdev->dev;
	unsigned int pll_out;
	int ret;

	if (dapm->dev != codec_dai->dev)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (dapm->bias_level == SND_SOC_BIAS_STANDBY) {
			if (sample_format == SNDRV_PCM_FORMAT_S24_LE)
				pll_out = sample_rate * 384;
			else
				pll_out = sample_rate * 256;

			ret = snd_soc_dai_set_pll(codec_dai, WM8962_FLL,
					WM8962_FLL_MCLK, data->clk_frequency,
					pll_out);
			if (ret < 0) {
				dev_err(dev, "failed to start FLL: %d\n", ret);
				return ret;
			}

			ret = snd_soc_dai_set_sysclk(codec_dai,
					WM8962_SYSCLK_FLL, pll_out,
					SND_SOC_CLOCK_IN);
			if (ret < 0) {
				dev_err(dev, "failed to set SYSCLK: %d\n", ret);
				return ret;
			}
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		if (dapm->bias_level == SND_SOC_BIAS_PREPARE) {
			ret = snd_soc_dai_set_sysclk(codec_dai,
					WM8962_SYSCLK_MCLK, data->clk_frequency,
					SND_SOC_CLOCK_IN);
			if (ret < 0) {
				dev_err(dev,
					"failed to switch away from FLL: %d\n",
					ret);
				return ret;
			}

			ret = snd_soc_dai_set_pll(codec_dai, WM8962_FLL,
					0, 0, 0);
			if (ret < 0) {
				dev_err(dev, "failed to stop FLL: %d\n", ret);
				return ret;
			}
		}
		break;

	default:
		break;
	}

	return 0;
}

#else

static int imx_hifi_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct imx_priv *priv = &card_priv;
	struct device *dev = &priv->pdev->dev;
	struct snd_soc_card *card = platform_get_drvdata(priv->pdev);
	struct imx_wm8962_data *data = snd_soc_card_get_drvdata(card);
	unsigned int sample_rate = params_rate(params);
	snd_pcm_format_t sample_format = params_format(params);
	u32 dai_format, pll_out;
	int ret = 0;

	if (!priv->first_stream) {
		priv->first_stream = substream;
	} else {
		priv->second_stream = substream;

		/* We suppose the two substream are using same params */
		return 0;
	}

	if (data->is_codec_master)
		dai_format = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBM_CFM;
	else
		dai_format = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, dai_format);
	if (ret) {
		dev_err(dev, "failed to set codec dai fmt: %d\n", ret);
		return ret;
	}

	if (sample_format == SNDRV_PCM_FORMAT_S24_LE)
		pll_out = sample_rate * 384;
	else
		pll_out = sample_rate * 256;

	ret = snd_soc_dai_set_pll(codec_dai, WM8962_FLL, WM8962_FLL_MCLK,
			data->clk_frequency, pll_out);
	if (ret) {
		dev_err(dev, "failed to start FLL: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8962_SYSCLK_FLL,
			pll_out, SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(dev, "failed to set SYSCLK: %d\n", ret);
		return ret;
	}

	return 0;
}

static int imx_hifi_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct imx_priv *priv = &card_priv;
	struct device *dev = &priv->pdev->dev;
	int ret;

	/* We don't need to handle anything if there's no substream running */
	if (!priv->first_stream)
		return 0;

	if (priv->first_stream == substream)
		priv->first_stream = priv->second_stream;
	priv->second_stream = NULL;

	if (!priv->first_stream) {
		/*
		 * Continuously setting FLL would cause playback distortion.
		 * We can fix it just by mute codec after playback.
		 */
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			snd_soc_dai_digital_mute(codec_dai, 1, substream->stream);

		/*
		 * WM8962 doesn't allow us to continuously setting FLL,
		 * So we set MCLK as sysclk once, which'd remove the limitation.
		 */
		ret = snd_soc_dai_set_sysclk(codec_dai, WM8962_SYSCLK_MCLK,
				0, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(dev, "failed to switch away from FLL: %d\n", ret);
			return ret;
		}

		/* Disable FLL and let codec do pm_runtime_put() */
		ret = snd_soc_dai_set_pll(codec_dai, WM8962_FLL,
				WM8962_FLL_MCLK, 0, 0);
		if (ret < 0) {
			dev_err(dev, "failed to stop FLL: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static struct snd_soc_ops imx_hifi_ops = {
	.startup = imx_hifi_startup,
	.hw_params = imx_hifi_hw_params,
	.hw_free = imx_hifi_hw_free,
};
#endif /* CONFIG_SND_SOC_IMX_WM8962_ANDROID */

static int imx_wm8962_gpio_init(struct snd_soc_card *card)
{
	struct snd_soc_dai *codec_dai = card->rtd[0].codec_dai;
	struct snd_soc_codec *codec = codec_dai->codec;
	struct imx_priv *priv = &card_priv;

	priv->codec = codec;

	if (gpio_is_valid(priv->hp_gpio)) {
		imx_hp_jack_gpio.gpio = priv->hp_gpio;
		imx_hp_jack_gpio.jack_status_check = hpjack_status_check;
	
		snd_soc_card_jack_new(card, "Headphone Jack",
				SND_JACK_HEADPHONE, &imx_hp_jack,
				imx_hp_jack_pins, ARRAY_SIZE(imx_hp_jack_pins));

		snd_soc_jack_add_gpios(&imx_hp_jack, 1, &imx_hp_jack_gpio);
	}

	if (gpio_is_valid(priv->mic_gpio)) {
		imx_mic_jack_gpio.gpio = priv->mic_gpio;
		imx_mic_jack_gpio.jack_status_check = micjack_status_check;

		snd_soc_card_jack_new(card, "AMIC",
				SND_JACK_MICROPHONE, &imx_mic_jack,
				imx_mic_jack_pins, ARRAY_SIZE(imx_mic_jack_pins)); 

		snd_soc_jack_add_gpios(&imx_mic_jack, 1, &imx_mic_jack_gpio);
	} else if (priv->amic_mono || priv->dmic_mono) {
		/*
		 * Permanent set monomix bit if only one microphone
		 * is present on the board while it needs monomix.
		 */
		snd_soc_update_bits(priv->codec, WM8962_THREED1,
				WM8962_ADC_MONOMIX_MASK, WM8962_ADC_MONOMIX);
	}

	return 0;
}

static ssize_t show_headphone(struct device_driver *dev, char *buf)
{
	struct imx_priv *priv = &card_priv;
	int hp_status;

	if (!gpio_is_valid(priv->hp_gpio)) {
		strcpy(buf, "no detect gpio connected\n");
		return strlen(buf);
	}

	/* Check if headphone is plugged in */
	hp_status = gpio_get_value(priv->hp_gpio) ? 1 : 0;

	if (hp_status != priv->hp_active_low)
		strcpy(buf, "headphone\n");
	else
		strcpy(buf, "speaker\n");

	return strlen(buf);
}

static DRIVER_ATTR(headphone, S_IRUGO | S_IWUSR, show_headphone, NULL);

static ssize_t show_mic(struct device_driver *dev, char *buf)
{
	struct imx_priv *priv = &card_priv;
	int mic_status;

	if (!gpio_is_valid(priv->mic_gpio)) {
		strcpy(buf, "no detect gpio connected\n");
		return strlen(buf);
	}

	/* Check if analog microphone is plugged in */
	mic_status = gpio_get_value(priv->mic_gpio) ? 1 : 0;

	if (mic_status != priv->mic_active_low)
		strcpy(buf, "amic\n");
	else
		strcpy(buf, "dmic\n");

	return strlen(buf);
}

static DRIVER_ATTR(microphone, S_IRUGO | S_IWUSR, show_mic, NULL);

static int imx_wm8962_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_dai *codec_dai = card->rtd[0].codec_dai;
	struct imx_priv *priv = &card_priv;
	struct imx_wm8962_data *data = snd_soc_card_get_drvdata(card);
	struct device *dev = &priv->pdev->dev;
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8962_SYSCLK_MCLK,
			data->clk_frequency, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(dev, "failed to set sysclk in %s\n", __func__);

	return ret;
}

static int be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				struct snd_pcm_hw_params *params) {
	struct imx_priv *priv = &card_priv;
	struct snd_interval *rate;
	struct snd_mask *mask;

	if (!priv->asrc_pdev)
		return -EINVAL;

	rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	rate->max = rate->min = priv->asrc_rate;

	mask = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	snd_mask_none(mask);
	snd_mask_set(mask, priv->asrc_format);

	return 0;
}

static int imx_wm8962_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *cpu_np, *codec_np = NULL;
	struct platform_device *cpu_pdev;
	struct imx_priv *priv = &card_priv;
	struct i2c_client *codec_dev;
	struct imx_wm8962_data *data;
	struct clk *codec_clk = NULL;
	int int_port, ext_port, tmp_port;
	int ret;
	struct platform_device *asrc_pdev = NULL;
	struct device_node *asrc_np;
	u32 width;

	priv->pdev = pdev;
	priv->asrc_pdev = NULL;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto fail;
	}

	if (of_property_read_bool(pdev->dev.of_node, "codec-master"))
		data->is_codec_master = true;

	cpu_np = of_parse_phandle(pdev->dev.of_node, "cpu-dai", 0);
	if (!cpu_np) {
		dev_err(&pdev->dev, "cpu dai phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	if (!strstr(cpu_np->name, "ssi"))
		goto audmux_bypass;

	ret = of_property_read_u32(np, "mux-int-port", &int_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-int-port missing or invalid\n");
		goto fail;
	}
	ret = of_property_read_u32(np, "mux-ext-port", &ext_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-ext-port missing or invalid\n");
		goto fail;
	}

	/*
	 * The port numbering in the hardware manual starts at 1, while
	 * the audmux API expects it starts at 0.
	 */
	int_port--;
	ext_port--;
	if (data->is_codec_master) {
		tmp_port = int_port;
		int_port = ext_port;
		ext_port = tmp_port;
	}

	ret = imx_audmux_v2_configure_port(ext_port,
			IMX_AUDMUX_V2_PTCR_SYN |
			IMX_AUDMUX_V2_PTCR_TFSEL(int_port) |
			IMX_AUDMUX_V2_PTCR_TCSEL(int_port) |
			IMX_AUDMUX_V2_PTCR_TFSDIR |
			IMX_AUDMUX_V2_PTCR_TCLKDIR,
			IMX_AUDMUX_V2_PDCR_RXDSEL(int_port));
	if (ret) {
		dev_err(&pdev->dev, "audmux internal port setup failed\n");
		goto fail;
	}
	ret = imx_audmux_v2_configure_port(int_port,
			IMX_AUDMUX_V2_PTCR_SYN,
			IMX_AUDMUX_V2_PDCR_RXDSEL(ext_port));
	if (ret) {
		dev_err(&pdev->dev, "audmux external port setup failed\n");
		goto fail;
	}

audmux_bypass:
	codec_np = of_parse_phandle(pdev->dev.of_node, "audio-codec", 0);
	if (!codec_np) {
		dev_err(&pdev->dev, "phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	cpu_pdev = of_find_device_by_node(cpu_np);
	if (!cpu_pdev) {
		dev_err(&pdev->dev, "failed to find SSI platform device\n");
		ret = -EINVAL;
		goto fail;
	}
	codec_dev = of_find_i2c_device_by_node(codec_np);
	if (!codec_dev || !codec_dev->dev.driver) {
		dev_err(&pdev->dev, "failed to find codec platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	asrc_np = of_parse_phandle(pdev->dev.of_node, "asrc-controller", 0);
	if (asrc_np) {
		asrc_pdev = of_find_device_by_node(asrc_np);
		priv->asrc_pdev = asrc_pdev;
	}

	priv->first_stream = NULL;
	priv->second_stream = NULL;

	codec_clk = devm_clk_get(&codec_dev->dev, NULL);
	if (IS_ERR(codec_clk)) {
		ret = PTR_ERR(codec_clk);
		dev_err(&codec_dev->dev, "failed to get codec clk: %d\n", ret);
		goto fail;
	}

	data->clk_frequency = clk_get_rate(codec_clk);

	priv->amic_mono = of_property_read_bool(codec_np, "amic-mono");
	priv->dmic_mono = of_property_read_bool(codec_np, "dmic-mono");

	priv->hp_gpio = of_get_named_gpio_flags(np, "hp-det-gpios", 0,
				(enum of_gpio_flags *)&priv->hp_active_low);
	priv->mic_gpio = of_get_named_gpio_flags(np, "mic-det-gpios", 0,
				(enum of_gpio_flags *)&priv->mic_active_low);

	data->dai[0].name = "HiFi";
	data->dai[0].stream_name = "HiFi";
	data->dai[0].codec_dai_name = "wm8962";
	data->dai[0].codec_of_node = codec_np;
	data->dai[0].cpu_dai_name = dev_name(&cpu_pdev->dev);
	data->dai[0].platform_of_node = cpu_np;
	data->dai[0].ops = &imx_hifi_ops;
	data->dai[0].dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF;
	if (data->is_codec_master)
		data->dai[0].dai_fmt |= SND_SOC_DAIFMT_CBM_CFM;
	else
		data->dai[0].dai_fmt |= SND_SOC_DAIFMT_CBS_CFS;

	data->card.num_links = 1;

	if (asrc_pdev) {
		data->dai[0].ignore_pmdown_time = 1;
		data->dai[1].name = "HiFi-ASRC-FE";
		data->dai[1].stream_name = "HiFi-ASRC-FE";
		data->dai[1].codec_name = "snd-soc-dummy";
		data->dai[1].codec_dai_name = "snd-soc-dummy-dai";
		data->dai[1].cpu_of_node = asrc_np;
		data->dai[1].platform_of_node = asrc_np;
		data->dai[1].dynamic = 1;
		data->dai[1].ignore_pmdown_time = 1;
		data->dai[1].dpcm_playback = 1;
		data->dai[1].dpcm_capture = 1;

		data->dai[2].name = "HiFi-ASRC-BE";
		data->dai[2].stream_name = "HiFi-ASRC-BE";
		data->dai[2].codec_dai_name = "wm8962";
		data->dai[2].codec_of_node = codec_np;
		data->dai[2].cpu_dai_name = dev_name(&cpu_pdev->dev);
		data->dai[2].platform_name = "snd-soc-dummy";
		data->dai[2].ops = &imx_hifi_ops;
		data->dai[2].be_hw_params_fixup = be_hw_params_fixup;
		data->dai[2].no_pcm = 1;
		data->dai[2].ignore_pmdown_time = 1;
		data->dai[2].dpcm_playback = 1;
		data->dai[2].dpcm_capture = 1;
		data->card.num_links = 3;

		ret = of_property_read_u32(asrc_np, "fsl,asrc-rate",
						&priv->asrc_rate);
		if (ret) {
			dev_err(&pdev->dev, "failed to get output rate\n");
			ret = -EINVAL;
			goto fail;
		}

		ret = of_property_read_u32(asrc_np, "fsl,asrc-width", &width);
		if (ret) {
			dev_err(&pdev->dev, "failed to get output rate\n");
			ret = -EINVAL;
			goto fail;
		}

		if (width == 24)
			priv->asrc_format = SNDRV_PCM_FORMAT_S24_LE;
		else
			priv->asrc_format = SNDRV_PCM_FORMAT_S16_LE;
	}

	data->card.dev = &pdev->dev;
	ret = snd_soc_of_parse_card_name(&data->card, "model");
	if (ret)
		goto fail;
	ret = snd_soc_of_parse_audio_routing(&data->card, "audio-routing");
	if (ret)
		goto fail;
	data->card.owner = THIS_MODULE;
	data->card.dai_link = data->dai;
	data->card.dapm_widgets = imx_wm8962_dapm_widgets;
	data->card.num_dapm_widgets = ARRAY_SIZE(imx_wm8962_dapm_widgets);

	data->card.late_probe = imx_wm8962_late_probe;

#ifdef CONFIG_SND_SOC_IMX_WM8962_ANDROID
	data->card.set_bias_level = imx_wm8962_set_bias_level;
#endif
	platform_set_drvdata(pdev, &data->card);
	snd_soc_card_set_drvdata(&data->card, data);

	ret = devm_snd_soc_register_card(&pdev->dev, &data->card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		goto fail;
	}

	priv->snd_card = data->card.snd_card;
	priv->headphone_kctl = snd_kctl_jack_new("Headphone", 0, NULL);
	ret = snd_ctl_add(data->card.snd_card, priv->headphone_kctl);
	if (ret)
		goto fail;

	imx_wm8962_gpio_init(&data->card);

	if (gpio_is_valid(priv->hp_gpio)) {
		ret = driver_create_file(pdev->dev.driver, &driver_attr_headphone);
		if (ret) {
			dev_err(&pdev->dev, "create hp attr failed (%d)\n", ret);
			goto fail_hp;
		}
	}

	if (gpio_is_valid(priv->mic_gpio)) {
		ret = driver_create_file(pdev->dev.driver, &driver_attr_microphone);
		if (ret) {
			dev_err(&pdev->dev, "create mic attr failed (%d)\n", ret);
			goto fail_mic;
		}
	}

	goto fail;

fail_mic:
	driver_remove_file(pdev->dev.driver, &driver_attr_headphone);
fail_hp:
fail:
	of_node_put(cpu_np);
	of_node_put(codec_np);

	return ret;
}

static int imx_wm8962_remove(struct platform_device *pdev)
{
	driver_remove_file(pdev->dev.driver, &driver_attr_microphone);
	driver_remove_file(pdev->dev.driver, &driver_attr_headphone);

	return 0;
}

static const struct of_device_id imx_wm8962_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-wm8962", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_wm8962_dt_ids);

static struct platform_driver imx_wm8962_driver = {
	.driver = {
		.name = "imx-wm8962",
		.pm = &snd_soc_pm_ops,
		.of_match_table = imx_wm8962_dt_ids,
	},
	.probe = imx_wm8962_probe,
	.remove = imx_wm8962_remove,
};
module_platform_driver(imx_wm8962_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("Freescale i.MX WM8962 ASoC machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-wm8962");
