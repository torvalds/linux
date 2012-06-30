/*
 * Handles the Mitac mioa701 SoC system
 *
 * Copyright (C) 2008 Robert Jarzmik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation in version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This is a little schema of the sound interconnections :
 *
 *    Sagem X200                 Wolfson WM9713
 *    +--------+             +-------------------+      Rear Speaker
 *    |        |             |                   |           /-+
 *    |        +--->----->---+MONOIN         SPKL+--->----+-+  |
 *    |  GSM   |             |                   |        | |  |
 *    |        +--->----->---+PCBEEP         SPKR+--->----+-+  |
 *    |  CHIP  |             |                   |           \-+
 *    |        +---<-----<---+MONO               |
 *    |        |             |                   |      Front Speaker
 *    +--------+             |                   |           /-+
 *                           |                HPL+--->----+-+  |
 *                           |                   |        | |  |
 *                           |               OUT3+--->----+-+  |
 *                           |                   |           \-+
 *                           |                   |
 *                           |                   |     Front Micro
 *                           |                   |         +
 *                           |               MIC1+-----<--+o+
 *                           |                   |         +
 *                           +-------------------+        ---
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>
#include <mach/audio.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/ac97_codec.h>

#include "pxa2xx-ac97.h"
#include "../codecs/wm9713.h"

#define ARRAY_AND_SIZE(x)	(x), ARRAY_SIZE(x)

#define AC97_GPIO_PULL		0x58

/* Use GPIO8 for rear speaker amplifier */
static int rear_amp_power(struct snd_soc_codec *codec, int power)
{
	unsigned short reg;

	if (power) {
		reg = snd_soc_read(codec, AC97_GPIO_CFG);
		snd_soc_write(codec, AC97_GPIO_CFG, reg | 0x0100);
		reg = snd_soc_read(codec, AC97_GPIO_PULL);
		snd_soc_write(codec, AC97_GPIO_PULL, reg | (1<<15));
	} else {
		reg = snd_soc_read(codec, AC97_GPIO_CFG);
		snd_soc_write(codec, AC97_GPIO_CFG, reg & ~0x0100);
		reg = snd_soc_read(codec, AC97_GPIO_PULL);
		snd_soc_write(codec, AC97_GPIO_PULL, reg & ~(1<<15));
	}

	return 0;
}

static int rear_amp_event(struct snd_soc_dapm_widget *widget,
			  struct snd_kcontrol *kctl, int event)
{
	struct snd_soc_codec *codec = widget->codec;

	return rear_amp_power(codec, SND_SOC_DAPM_EVENT_ON(event));
}

/* mioa701 machine dapm widgets */
static const struct snd_soc_dapm_widget mioa701_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Front Speaker", NULL),
	SND_SOC_DAPM_SPK("Rear Speaker", rear_amp_event),
	SND_SOC_DAPM_MIC("Headset", NULL),
	SND_SOC_DAPM_LINE("GSM Line Out", NULL),
	SND_SOC_DAPM_LINE("GSM Line In", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Front Mic", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* Call Mic */
	{"Mic Bias", NULL, "Front Mic"},
	{"MIC1", NULL, "Mic Bias"},

	/* Headset Mic */
	{"LINEL", NULL, "Headset Mic"},
	{"LINER", NULL, "Headset Mic"},

	/* GSM Module */
	{"MONOIN", NULL, "GSM Line Out"},
	{"PCBEEP", NULL, "GSM Line Out"},
	{"GSM Line In", NULL, "MONO"},

	/* headphone connected to HPL, HPR */
	{"Headset", NULL, "HPL"},
	{"Headset", NULL, "HPR"},

	/* front speaker connected to HPL, OUT3 */
	{"Front Speaker", NULL, "HPL"},
	{"Front Speaker", NULL, "OUT3"},

	/* rear speaker connected to SPKL, SPKR */
	{"Rear Speaker", NULL, "SPKL"},
	{"Rear Speaker", NULL, "SPKR"},
};

static int mioa701_wm9713_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	unsigned short reg;

	/* Add mioa701 specific widgets */
	snd_soc_dapm_new_controls(dapm, ARRAY_AND_SIZE(mioa701_dapm_widgets));

	/* Set up mioa701 specific audio path audio_mapnects */
	snd_soc_dapm_add_routes(dapm, ARRAY_AND_SIZE(audio_map));

	/* Prepare GPIO8 for rear speaker amplifier */
	reg = codec->driver->read(codec, AC97_GPIO_CFG);
	codec->driver->write(codec, AC97_GPIO_CFG, reg | 0x0100);

	/* Prepare MIC input */
	reg = codec->driver->read(codec, AC97_3D_CONTROL);
	codec->driver->write(codec, AC97_3D_CONTROL, reg | 0xc000);

	snd_soc_dapm_enable_pin(dapm, "Front Speaker");
	snd_soc_dapm_enable_pin(dapm, "Rear Speaker");
	snd_soc_dapm_enable_pin(dapm, "Front Mic");
	snd_soc_dapm_enable_pin(dapm, "GSM Line In");
	snd_soc_dapm_enable_pin(dapm, "GSM Line Out");

	return 0;
}

static struct snd_soc_ops mioa701_ops;

static struct snd_soc_dai_link mioa701_dai[] = {
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		.cpu_dai_name = "pxa2xx-ac97",
		.codec_dai_name = "wm9713-hifi",
		.codec_name = "wm9713-codec",
		.init = mioa701_wm9713_init,
		.platform_name = "pxa-pcm-audio",
		.ops = &mioa701_ops,
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		.cpu_dai_name = "pxa2xx-ac97-aux",
		.codec_dai_name ="wm9713-aux",
		.codec_name = "wm9713-codec",
		.platform_name = "pxa-pcm-audio",
		.ops = &mioa701_ops,
	},
};

static struct snd_soc_card mioa701 = {
	.name = "MioA701",
	.owner = THIS_MODULE,
	.dai_link = mioa701_dai,
	.num_links = ARRAY_SIZE(mioa701_dai),
};

static int __devinit mioa701_wm9713_probe(struct platform_device *pdev)
{
	int rc;

	if (!machine_is_mioa701())
		return -ENODEV;

	mioa701.dev = &pdev->dev;
	rc =  snd_soc_register_card(&mioa701);
	if (!rc)
		dev_warn(&pdev->dev, "Be warned that incorrect mixers/muxes setup will"
			 "lead to overheating and possible destruction of your device."
			 " Do not use without a good knowledge of mio's board design!\n");
	return rc;
}

static int __devexit mioa701_wm9713_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	return 0;
}

static struct platform_driver mioa701_wm9713_driver = {
	.probe		= mioa701_wm9713_probe,
	.remove		= __devexit_p(mioa701_wm9713_remove),
	.driver		= {
		.name		= "mioa701-wm9713",
		.owner		= THIS_MODULE,
	},
};

module_platform_driver(mioa701_wm9713_driver);

/* Module information */
MODULE_AUTHOR("Robert Jarzmik (rjarzmik@free.fr)");
MODULE_DESCRIPTION("ALSA SoC WM9713 MIO A701");
MODULE_LICENSE("GPL");
