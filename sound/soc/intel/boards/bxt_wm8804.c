// SPDX-License-Identifier: GPL-2.0
/*
 * bxt-wm8804.c - ASoC machine driver for Up and Up2 board
 * based on WM8804/Hifiberry Digi+
 * Copyright (c) 2018, Intel Corporation.
 */

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <asm/platform_sst_audio.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../../codecs/wm8804.h"
#include "../atom/sst-atom-controls.h"
#include <linux/gpio/consumer.h>

#include "../../codecs/wm8804.h"

static short int auto_shutdown_output;
module_param(auto_shutdown_output, short, 0660);
MODULE_PARM_DESC(auto_shutdown_output, "Shutdown SP/DIF output if playback is stopped");

#define CLK_44EN_RATE 22579200UL
#define CLK_48EN_RATE 24576000UL

static bool snd_rpi_hifiberry_is_digipro;
static struct gpio_desc *snd_rpi_hifiberry_clk44gpio;
static struct gpio_desc *snd_rpi_hifiberry_clk48gpio;

static int samplerate = 44100;

static uint32_t snd_rpi_hifiberry_digi_enable_clock(int sample_rate)
{
	switch (sample_rate) {
	case 11025:
	case 22050:
	case 44100:
	case 88200:
	case 176400:
		gpiod_set_value_cansleep(snd_rpi_hifiberry_clk44gpio, 1);
		gpiod_set_value_cansleep(snd_rpi_hifiberry_clk48gpio, 0);
		return CLK_44EN_RATE;
	default:
		gpiod_set_value_cansleep(snd_rpi_hifiberry_clk48gpio, 1);
		gpiod_set_value_cansleep(snd_rpi_hifiberry_clk44gpio, 0);
		return CLK_48EN_RATE;
	}
}

static int snd_rpi_hifiberry_digi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *codec =  rtd->codec_dai->component;

	/* enable TX output */
	snd_soc_component_update_bits(codec, WM8804_PWRDN, 0x4, 0x0);

	/* Initialize Digi+ Pro hardware */
	if (snd_rpi_hifiberry_is_digipro) {
		struct snd_soc_dai_link *dai = rtd->dai_link;

		dai->name = "HiFiBerry Digi+ Pro";
		dai->stream_name = "HiFiBerry Digi+ Pro HiFi";
	}

	return 0;
}

static int snd_rpi_hifiberry_digi_startup(struct snd_pcm_substream *substream)
{
	/* turn on digital output */
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *codec = rtd->codec_dai->component;

	snd_soc_component_update_bits(codec, WM8804_PWRDN, 0x3c, 0x00);
	return 0;
}

static void snd_rpi_hifiberry_digi_shutdown(struct snd_pcm_substream *substream)
{
	/* turn off output */
	if (auto_shutdown_output) {
		/* turn off output */
		struct snd_soc_pcm_runtime *rtd = substream->private_data;
		struct snd_soc_component *codec = rtd->codec_dai->component;

		snd_soc_component_update_bits(codec, WM8804_PWRDN, 0x3c, 0x3c);
	}
}

static int snd_rpi_hifiberry_digi_hw_params(struct snd_pcm_substream *substream,
					    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_component *codec = codec_dai->component;

	int sysclk = 27000000; /* This is fixed on this board */

	long mclk_freq = 0;
	int mclk_div = 1;
	int sampling_freq = 1;

	int ret;

	samplerate = params_rate(params);

	if (samplerate <= 96000) {
		mclk_freq = samplerate * 256;
		mclk_div = WM8804_MCLKDIV_256FS;
	} else {
		mclk_freq = samplerate * 128;
		mclk_div = WM8804_MCLKDIV_128FS;
	}

	if (snd_rpi_hifiberry_is_digipro)
		sysclk = snd_rpi_hifiberry_digi_enable_clock(samplerate);

	switch (samplerate) {
	case 32000:
		sampling_freq = 0x03;
		break;
	case 44100:
		sampling_freq = 0x00;
		break;
	case 48000:
		sampling_freq = 0x02;
		break;
	case 88200:
		sampling_freq = 0x08;
		break;
	case 96000:
		sampling_freq = 0x0a;
		break;
	case 176400:
		sampling_freq = 0x0c;
		break;
	case 192000:
		sampling_freq = 0x0e;
		break;
	default:
		dev_err(codec->dev,
			"Failed to set WM8804 SYSCLK, unsupported samplerate %d\n",
			samplerate);
	}

	snd_soc_dai_set_clkdiv(codec_dai, WM8804_MCLK_DIV, mclk_div);
	snd_soc_dai_set_pll(codec_dai, 0, 0, sysclk, mclk_freq);

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8804_TX_CLKSRC_PLL,
				     sysclk, SND_SOC_CLOCK_OUT);

	if (ret < 0) {
		dev_err(codec->dev,
			"Failed to set WM8804 SYSCLK: %d\n", ret);
		return ret;
	}

	/* Enable TX output */
	snd_soc_component_update_bits(codec, WM8804_PWRDN, 0x4, 0x0);

	/* Power on */
	snd_soc_component_update_bits(codec, WM8804_PWRDN, 0x9, 0);

	/* set sampling frequency status bits */
	snd_soc_component_update_bits(codec, WM8804_SPDTX4, 0x0f,
				      sampling_freq);

	return 0;
}

/* machine stream operations */
static struct snd_soc_ops snd_rpi_hifiberry_digi_ops = {
	.hw_params = snd_rpi_hifiberry_digi_hw_params,
	.startup = snd_rpi_hifiberry_digi_startup,
	.shutdown = snd_rpi_hifiberry_digi_shutdown,
};

static struct snd_soc_dai_link dailink[] = {
	/* CODEC<->CODEC link */
	/* back ends */
	{
		.name = "SSP5-Codec",
		.id = 0,
		.cpu_dai_name = "SSP5 Pin",
		.platform_name = "sof-audio",
		.no_pcm = 1,
		.codec_dai_name = "wm8804-spdif",
		.codec_name = "i2c-1AEC8804:00",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBM_CFM,
		.nonatomic = true,
		.dpcm_playback = 1,
		.ops		= &snd_rpi_hifiberry_digi_ops,
		.init		= snd_rpi_hifiberry_digi_init,
	},
};

/* SoC card */
static struct snd_soc_card bxt_wm8804_card = {
	.name = "bxt-wm8804",
	.owner = THIS_MODULE,
	.dai_link = dailink,
	.num_links = ARRAY_SIZE(dailink),
};

 /* i2c-<HID>:00 with HID being 8 chars */
static char codec_name[SND_ACPI_I2C_ID_LEN];

static int bxt_wm8804_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct snd_soc_acpi_mach *mach;
	const char *i2c_name = NULL;
	int dai_index = 0;
	int ret_val = 0;
	int i;

	mach = (&pdev->dev)->platform_data;
	card = &bxt_wm8804_card;
	card->dev = &pdev->dev;

	/* fix index of codec dai */
	for (i = 0; i < ARRAY_SIZE(dailink); i++) {
		if (!strcmp(dailink[i].codec_name, "i2c-1AEC8804:00")) {
			dai_index = i;
			break;
		}
	}

	/* fixup codec name based on HID */
	i2c_name = acpi_dev_get_first_match_name(mach->id, NULL, -1);
	if (i2c_name) {
		snprintf(codec_name, sizeof(codec_name),
			 "%s%s", "i2c-", i2c_name);
		dailink[dai_index].codec_name = codec_name;
	}

	ret_val = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret_val) {
		dev_err(&pdev->dev,
			"snd_soc_register_card failed %d\n", ret_val);
		return ret_val;
	}
	platform_set_drvdata(pdev, card);
	return ret_val;
}

static struct platform_driver bxt_wm8804_driver = {
	.driver = {
		.name = "bxt-wm8804",
	},
	.probe = bxt_wm8804_probe,
};
module_platform_driver(bxt_wm8804_driver);

MODULE_DESCRIPTION("ASoC Intel(R) Broxton + WM8804 Machine driver");
MODULE_AUTHOR("Pierre-Louis Bossart");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bxt-wm8804");
