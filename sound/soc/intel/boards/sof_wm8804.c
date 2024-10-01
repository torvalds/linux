// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2018-2020, Intel Corporation
//
// sof-wm8804.c - ASoC machine driver for Up and Up2 board
// based on WM8804/Hifiberry Digi+


#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../../codecs/wm8804.h"

struct sof_card_private {
	struct gpio_desc *gpio_44;
	struct gpio_desc *gpio_48;
	int sample_rate;
};

#define SOF_WM8804_UP2_QUIRK			BIT(0)

static unsigned long sof_wm8804_quirk;

static int sof_wm8804_quirk_cb(const struct dmi_system_id *id)
{
	sof_wm8804_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id sof_wm8804_quirk_table[] = {
	{
		.callback = sof_wm8804_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "AAEON"),
			DMI_MATCH(DMI_PRODUCT_NAME, "UP-APL01"),
		},
		.driver_data = (void *)SOF_WM8804_UP2_QUIRK,
	},
	{}
};

static int sof_wm8804_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *codec = codec_dai->component;
	const int sysclk = 27000000; /* This is fixed on this board */
	int samplerate;
	long mclk_freq;
	int mclk_div;
	int sampling_freq;
	bool clk_44;
	int ret;

	samplerate = params_rate(params);
	if (samplerate == ctx->sample_rate)
		return 0;

	ctx->sample_rate = 0;

	if (samplerate <= 96000) {
		mclk_freq = samplerate * 256;
		mclk_div = WM8804_MCLKDIV_256FS;
	} else {
		mclk_freq = samplerate * 128;
		mclk_div = WM8804_MCLKDIV_128FS;
	}

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
		dev_err(rtd->card->dev,
			"unsupported samplerate %d\n", samplerate);
		return -EINVAL;
	}

	if (samplerate % 16000)
		clk_44 = true; /* use 44.1 kHz root frequency */
	else
		clk_44 = false;

	if (!(IS_ERR_OR_NULL(ctx->gpio_44) ||
	      IS_ERR_OR_NULL(ctx->gpio_48))) {
		/*
		 * ensure both GPIOs are LOW first, then drive the
		 * relevant one to HIGH
		 */
		if (clk_44) {
			gpiod_set_value_cansleep(ctx->gpio_48, !clk_44);
			gpiod_set_value_cansleep(ctx->gpio_44, clk_44);
		} else {
			gpiod_set_value_cansleep(ctx->gpio_44, clk_44);
			gpiod_set_value_cansleep(ctx->gpio_48, !clk_44);
		}
	}

	snd_soc_dai_set_clkdiv(codec_dai, WM8804_MCLK_DIV, mclk_div);
	ret = snd_soc_dai_set_pll(codec_dai, 0, 0, sysclk, mclk_freq);
	if (ret < 0) {
		dev_err(rtd->card->dev, "Failed to set WM8804 PLL\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8804_TX_CLKSRC_PLL,
				     sysclk, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(rtd->card->dev,
			"Failed to set WM8804 SYSCLK: %d\n", ret);
		return ret;
	}

	/* set sampling frequency status bits */
	snd_soc_component_update_bits(codec, WM8804_SPDTX4, 0x0f,
				      sampling_freq);

	ctx->sample_rate = samplerate;

	return 0;
}

/* machine stream operations */
static struct snd_soc_ops sof_wm8804_ops = {
	.hw_params = sof_wm8804_hw_params,
};

SND_SOC_DAILINK_DEF(ssp5_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP5 Pin")));

SND_SOC_DAILINK_DEF(ssp5_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-1AEC8804:00", "wm8804-spdif")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("0000:00:0e.0")));

static struct snd_soc_dai_link dailink[] = {
	/* back ends */
	{
		.name = "SSP5-Codec",
		.id = 0,
		.no_pcm = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &sof_wm8804_ops,
		SND_SOC_DAILINK_REG(ssp5_pin, ssp5_codec, platform),
	},
};

/* SoC card */
static struct snd_soc_card sof_wm8804_card = {
	.name = "wm8804", /* sof- prefix added automatically */
	.owner = THIS_MODULE,
	.dai_link = dailink,
	.num_links = ARRAY_SIZE(dailink),
};

 /* i2c-<HID>:00 with HID being 8 chars */
static char codec_name[SND_ACPI_I2C_ID_LEN];

/*
 * to control the HifiBerry Digi+ PRO, it's required to toggle GPIO to
 * select the clock source. On the Up2 board, this means
 * Pin29/BCM5/Linux GPIO 430 and Pin 31/BCM6/ Linux GPIO 404.
 *
 * Using the ACPI device name is not very nice, but since we only use
 * the value for the Up2 board there is no risk of conflict with other
 * platforms.
 */

static struct gpiod_lookup_table up2_gpios_table = {
	/* .dev_id is set during probe */
	.table = {
		GPIO_LOOKUP("INT3452:01", 73, "BCM-GPIO5", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT3452:01", 74, "BCM-GPIO6", GPIO_ACTIVE_HIGH),
		{ },
	},
};

static int sof_wm8804_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct snd_soc_acpi_mach *mach;
	struct sof_card_private *ctx;
	struct acpi_device *adev;
	int dai_index = 0;
	int ret;
	int i;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mach = pdev->dev.platform_data;
	card = &sof_wm8804_card;
	card->dev = &pdev->dev;

	dmi_check_system(sof_wm8804_quirk_table);

	if (sof_wm8804_quirk & SOF_WM8804_UP2_QUIRK) {
		up2_gpios_table.dev_id = dev_name(&pdev->dev);
		gpiod_add_lookup_table(&up2_gpios_table);

		/*
		 * The gpios are required for specific boards with
		 * local oscillators, and optional in other cases.
		 * Since we can't identify when they are needed, use
		 * the GPIO as non-optional
		 */

		ctx->gpio_44 = devm_gpiod_get(&pdev->dev, "BCM-GPIO5",
					      GPIOD_OUT_LOW);
		if (IS_ERR(ctx->gpio_44)) {
			ret = PTR_ERR(ctx->gpio_44);
			dev_err(&pdev->dev,
				"could not get BCM-GPIO5: %d\n",
				ret);
			return ret;
		}

		ctx->gpio_48 = devm_gpiod_get(&pdev->dev, "BCM-GPIO6",
					      GPIOD_OUT_LOW);
		if (IS_ERR(ctx->gpio_48)) {
			ret = PTR_ERR(ctx->gpio_48);
			dev_err(&pdev->dev,
				"could not get BCM-GPIO6: %d\n",
				ret);
			return ret;
		}
	}

	/* fix index of codec dai */
	for (i = 0; i < ARRAY_SIZE(dailink); i++) {
		if (!strcmp(dailink[i].codecs->name, "i2c-1AEC8804:00")) {
			dai_index = i;
			break;
		}
	}

	/* fixup codec name based on HID */
	adev = acpi_dev_get_first_match_dev(mach->id, NULL, -1);
	if (adev) {
		snprintf(codec_name, sizeof(codec_name),
			 "%s%s", "i2c-", acpi_dev_name(adev));
		put_device(&adev->dev);
		dailink[dai_index].codecs->name = codec_name;
	}

	snd_soc_card_set_drvdata(card, ctx);

	return devm_snd_soc_register_card(&pdev->dev, card);
}

static int sof_wm8804_remove(struct platform_device *pdev)
{
	if (sof_wm8804_quirk & SOF_WM8804_UP2_QUIRK)
		gpiod_remove_lookup_table(&up2_gpios_table);
	return 0;
}

static struct platform_driver sof_wm8804_driver = {
	.driver = {
		.name = "sof-wm8804",
		.pm = &snd_soc_pm_ops,
	},
	.probe = sof_wm8804_probe,
	.remove = sof_wm8804_remove,
};
module_platform_driver(sof_wm8804_driver);

MODULE_DESCRIPTION("ASoC Intel(R) SOF + WM8804 Machine driver");
MODULE_AUTHOR("Pierre-Louis Bossart");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sof-wm8804");
