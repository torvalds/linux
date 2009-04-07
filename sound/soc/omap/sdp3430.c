/*
 * sdp3430.c  --  SoC audio for TI OMAP3430 SDP
 *
 * Author: Misael Lopez Cruz <x0052729@ti.com>
 *
 * Based on:
 * Author: Steve Sakoman <steve@sakoman.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/mcbsp.h>

#include "omap-mcbsp.h"
#include "omap-pcm.h"
#include "../codecs/twl4030.h"

static struct snd_soc_card snd_soc_sdp3430;

static int sdp3430_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	int ret;

	/* Set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai,
				  SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0) {
		printk(KERN_ERR "can't set codec DAI configuration\n");
		return ret;
	}

	/* Set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai,
				  SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0) {
		printk(KERN_ERR "can't set cpu DAI configuration\n");
		return ret;
	}

	/* Set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, 26000000,
					    SND_SOC_CLOCK_IN);
	if (ret < 0) {
		printk(KERN_ERR "can't set codec system clock\n");
		return ret;
	}

	return 0;
}

static struct snd_soc_ops sdp3430_ops = {
	.hw_params = sdp3430_hw_params,
};

/* Headset jack */
static struct snd_soc_jack hs_jack;

/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin hs_jack_pins[] = {
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Headset Stereophone",
		.mask = SND_JACK_HEADPHONE,
	},
};

/* Headset jack detection gpios */
static struct snd_soc_jack_gpio hs_jack_gpios[] = {
	{
		.gpio = (OMAP_MAX_GPIO_LINES + 2),
		.name = "hsdet-gpio",
		.report = SND_JACK_HEADSET,
		.debounce_time = 200,
	},
};

/* SDP3430 machine DAPM */
static const struct snd_soc_dapm_widget sdp3430_twl4030_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Ext Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_HP("Headset Stereophone", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* External Mics: MAINMIC, SUBMIC with bias*/
	{"MAINMIC", NULL, "Mic Bias 1"},
	{"SUBMIC", NULL, "Mic Bias 2"},
	{"Mic Bias 1", NULL, "Ext Mic"},
	{"Mic Bias 2", NULL, "Ext Mic"},

	/* External Speakers: HFL, HFR */
	{"Ext Spk", NULL, "HFL"},
	{"Ext Spk", NULL, "HFR"},

	/* Headset Mic: HSMIC with bias */
	{"HSMIC", NULL, "Headset Mic Bias"},
	{"Headset Mic Bias", NULL, "Headset Mic"},

	/* Headset Stereophone (Headphone): HSOL, HSOR */
	{"Headset Stereophone", NULL, "HSOL"},
	{"Headset Stereophone", NULL, "HSOR"},
};

static int sdp3430_twl4030_init(struct snd_soc_codec *codec)
{
	int ret;

	/* Add SDP3430 specific widgets */
	ret = snd_soc_dapm_new_controls(codec, sdp3430_twl4030_dapm_widgets,
				ARRAY_SIZE(sdp3430_twl4030_dapm_widgets));
	if (ret)
		return ret;

	/* Set up SDP3430 specific audio path audio_map */
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	/* SDP3430 connected pins */
	snd_soc_dapm_enable_pin(codec, "Ext Mic");
	snd_soc_dapm_enable_pin(codec, "Ext Spk");
	snd_soc_dapm_disable_pin(codec, "Headset Mic");
	snd_soc_dapm_disable_pin(codec, "Headset Stereophone");

	/* TWL4030 not connected pins */
	snd_soc_dapm_nc_pin(codec, "AUXL");
	snd_soc_dapm_nc_pin(codec, "AUXR");
	snd_soc_dapm_nc_pin(codec, "CARKITMIC");
	snd_soc_dapm_nc_pin(codec, "DIGIMIC0");
	snd_soc_dapm_nc_pin(codec, "DIGIMIC1");

	snd_soc_dapm_nc_pin(codec, "OUTL");
	snd_soc_dapm_nc_pin(codec, "OUTR");
	snd_soc_dapm_nc_pin(codec, "EARPIECE");
	snd_soc_dapm_nc_pin(codec, "PREDRIVEL");
	snd_soc_dapm_nc_pin(codec, "PREDRIVER");
	snd_soc_dapm_nc_pin(codec, "CARKITL");
	snd_soc_dapm_nc_pin(codec, "CARKITR");

	ret = snd_soc_dapm_sync(codec);
	if (ret)
		return ret;

	/* Headset jack detection */
	ret = snd_soc_jack_new(&snd_soc_sdp3430, "Headset Jack",
				SND_JACK_HEADSET, &hs_jack);
	if (ret)
		return ret;

	ret = snd_soc_jack_add_pins(&hs_jack, ARRAY_SIZE(hs_jack_pins),
				hs_jack_pins);
	if (ret)
		return ret;

	ret = snd_soc_jack_add_gpios(&hs_jack, ARRAY_SIZE(hs_jack_gpios),
				hs_jack_gpios);

	return ret;
}

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link sdp3430_dai = {
	.name = "TWL4030",
	.stream_name = "TWL4030",
	.cpu_dai = &omap_mcbsp_dai[0],
	.codec_dai = &twl4030_dai,
	.init = sdp3430_twl4030_init,
	.ops = &sdp3430_ops,
};

/* Audio machine driver */
static struct snd_soc_card snd_soc_sdp3430 = {
	.name = "SDP3430",
	.platform = &omap_soc_platform,
	.dai_link = &sdp3430_dai,
	.num_links = 1,
};

/* Audio subsystem */
static struct snd_soc_device sdp3430_snd_devdata = {
	.card = &snd_soc_sdp3430,
	.codec_dev = &soc_codec_dev_twl4030,
};

static struct platform_device *sdp3430_snd_device;

static int __init sdp3430_soc_init(void)
{
	int ret;

	if (!machine_is_omap_3430sdp()) {
		pr_debug("Not SDP3430!\n");
		return -ENODEV;
	}
	printk(KERN_INFO "SDP3430 SoC init\n");

	sdp3430_snd_device = platform_device_alloc("soc-audio", -1);
	if (!sdp3430_snd_device) {
		printk(KERN_ERR "Platform device allocation failed\n");
		return -ENOMEM;
	}

	platform_set_drvdata(sdp3430_snd_device, &sdp3430_snd_devdata);
	sdp3430_snd_devdata.dev = &sdp3430_snd_device->dev;
	*(unsigned int *)sdp3430_dai.cpu_dai->private_data = 1; /* McBSP2 */

	ret = platform_device_add(sdp3430_snd_device);
	if (ret)
		goto err1;

	return 0;

err1:
	printk(KERN_ERR "Unable to add platform device\n");
	platform_device_put(sdp3430_snd_device);

	return ret;
}
module_init(sdp3430_soc_init);

static void __exit sdp3430_soc_exit(void)
{
	snd_soc_jack_free_gpios(&hs_jack, ARRAY_SIZE(hs_jack_gpios),
				hs_jack_gpios);

	platform_device_unregister(sdp3430_snd_device);
}
module_exit(sdp3430_soc_exit);

MODULE_AUTHOR("Misael Lopez Cruz <x0052729@ti.com>");
MODULE_DESCRIPTION("ALSA SoC SDP3430");
MODULE_LICENSE("GPL");

