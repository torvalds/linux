/*
 *  mfld_machine.c - ASoc Machine driver for Intel Medfield MID platform
 *
 *  Copyright (C) 2010 Intel Corp
 *  Author: Vinod Koul <vinod.koul@intel.com>
 *  Author: Harsha Priya <priya.harsha@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include "../codecs/sn95031.h"

#define MID_MONO 1
#define MID_STEREO 2
#define MID_MAX_CAP 5
#define MFLD_JACK_INSERT 0x04

enum soc_mic_bias_zones {
	MFLD_MV_START = 0,
	/* mic bias volutage range for Headphones*/
	MFLD_MV_HP = 400,
	/* mic bias volutage range for American Headset*/
	MFLD_MV_AM_HS = 650,
	/* mic bias volutage range for Headset*/
	MFLD_MV_HS = 2000,
	MFLD_MV_UNDEFINED,
};

static unsigned int	hs_switch;
static unsigned int	lo_dac;

struct mfld_mc_private {
	void __iomem *int_base;
	u8 interrupt_status;
};

struct snd_soc_jack mfld_jack;

/*Headset jack detection DAPM pins */
static struct snd_soc_jack_pin mfld_jack_pins[] = {
	{
		.pin = "Headphones",
		.mask = SND_JACK_HEADPHONE,
	},
	{
		.pin = "AMIC1",
		.mask = SND_JACK_MICROPHONE,
	},
};

/* jack detection voltage zones */
static struct snd_soc_jack_zone mfld_zones[] = {
	{MFLD_MV_START, MFLD_MV_AM_HS, SND_JACK_HEADPHONE},
	{MFLD_MV_AM_HS, MFLD_MV_HS, SND_JACK_HEADSET},
};

/* sound card controls */
static const char *headset_switch_text[] = {"Earpiece", "Headset"};

static const char *lo_text[] = {"Vibra", "Headset", "IHF", "None"};

static const struct soc_enum headset_enum =
	SOC_ENUM_SINGLE_EXT(2, headset_switch_text);

static const struct soc_enum lo_enum =
	SOC_ENUM_SINGLE_EXT(4, lo_text);

static int headset_get_switch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = hs_switch;
	return 0;
}

static int headset_set_switch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (ucontrol->value.integer.value[0] == hs_switch)
		return 0;

	if (ucontrol->value.integer.value[0]) {
		pr_debug("hs_set HS path\n");
		snd_soc_dapm_enable_pin(&codec->dapm, "Headphones");
		snd_soc_dapm_disable_pin(&codec->dapm, "EPOUT");
	} else {
		pr_debug("hs_set EP path\n");
		snd_soc_dapm_disable_pin(&codec->dapm, "Headphones");
		snd_soc_dapm_enable_pin(&codec->dapm, "EPOUT");
	}
	snd_soc_dapm_sync(&codec->dapm);
	hs_switch = ucontrol->value.integer.value[0];

	return 0;
}

static void lo_enable_out_pins(struct snd_soc_codec *codec)
{
	snd_soc_dapm_enable_pin(&codec->dapm, "IHFOUTL");
	snd_soc_dapm_enable_pin(&codec->dapm, "IHFOUTR");
	snd_soc_dapm_enable_pin(&codec->dapm, "LINEOUTL");
	snd_soc_dapm_enable_pin(&codec->dapm, "LINEOUTR");
	snd_soc_dapm_enable_pin(&codec->dapm, "VIB1OUT");
	snd_soc_dapm_enable_pin(&codec->dapm, "VIB2OUT");
	if (hs_switch) {
		snd_soc_dapm_enable_pin(&codec->dapm, "Headphones");
		snd_soc_dapm_disable_pin(&codec->dapm, "EPOUT");
	} else {
		snd_soc_dapm_disable_pin(&codec->dapm, "Headphones");
		snd_soc_dapm_enable_pin(&codec->dapm, "EPOUT");
	}
}

static int lo_get_switch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = lo_dac;
	return 0;
}

static int lo_set_switch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (ucontrol->value.integer.value[0] == lo_dac)
		return 0;

	/* we dont want to work with last state of lineout so just enable all
	 * pins and then disable pins not required
	 */
	lo_enable_out_pins(codec);
	switch (ucontrol->value.integer.value[0]) {
	case 0:
		pr_debug("set vibra path\n");
		snd_soc_dapm_disable_pin(&codec->dapm, "VIB1OUT");
		snd_soc_dapm_disable_pin(&codec->dapm, "VIB2OUT");
		snd_soc_update_bits(codec, SN95031_LOCTL, 0x66, 0);
		break;

	case 1:
		pr_debug("set hs  path\n");
		snd_soc_dapm_disable_pin(&codec->dapm, "Headphones");
		snd_soc_dapm_disable_pin(&codec->dapm, "EPOUT");
		snd_soc_update_bits(codec, SN95031_LOCTL, 0x66, 0x22);
		break;

	case 2:
		pr_debug("set spkr path\n");
		snd_soc_dapm_disable_pin(&codec->dapm, "IHFOUTL");
		snd_soc_dapm_disable_pin(&codec->dapm, "IHFOUTR");
		snd_soc_update_bits(codec, SN95031_LOCTL, 0x66, 0x44);
		break;

	case 3:
		pr_debug("set null path\n");
		snd_soc_dapm_disable_pin(&codec->dapm, "LINEOUTL");
		snd_soc_dapm_disable_pin(&codec->dapm, "LINEOUTR");
		snd_soc_update_bits(codec, SN95031_LOCTL, 0x66, 0x66);
		break;
	}
	snd_soc_dapm_sync(&codec->dapm);
	lo_dac = ucontrol->value.integer.value[0];
	return 0;
}

static const struct snd_kcontrol_new mfld_snd_controls[] = {
	SOC_ENUM_EXT("Playback Switch", headset_enum,
			headset_get_switch, headset_set_switch),
	SOC_ENUM_EXT("Lineout Mux", lo_enum,
			lo_get_switch, lo_set_switch),
};

static const struct snd_soc_dapm_widget mfld_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_MIC("Mic", NULL),
};

static const struct snd_soc_dapm_route mfld_map[] = {
	{"Headphones", NULL, "HPOUTR"},
	{"Headphones", NULL, "HPOUTL"},
	{"Mic", NULL, "AMIC1"},
};

static void mfld_jack_check(unsigned int intr_status)
{
	struct mfld_jack_data jack_data;

	jack_data.mfld_jack = &mfld_jack;
	jack_data.intr_id = intr_status;

	sn95031_jack_detection(&jack_data);
	/* TODO: add american headset detection post gpiolib support */
}

static int mfld_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_codec *codec = runtime->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret_val;

	/* Add jack sense widgets */
	snd_soc_dapm_new_controls(dapm, mfld_widgets, ARRAY_SIZE(mfld_widgets));

	/* Set up the map */
	snd_soc_dapm_add_routes(dapm, mfld_map, ARRAY_SIZE(mfld_map));

	/* always connected */
	snd_soc_dapm_enable_pin(dapm, "Headphones");
	snd_soc_dapm_enable_pin(dapm, "Mic");

	ret_val = snd_soc_add_codec_controls(codec, mfld_snd_controls,
				ARRAY_SIZE(mfld_snd_controls));
	if (ret_val) {
		pr_err("soc_add_controls failed %d", ret_val);
		return ret_val;
	}
	/* default is earpiece pin, userspace sets it explcitly */
	snd_soc_dapm_disable_pin(dapm, "Headphones");
	/* default is lineout NC, userspace sets it explcitly */
	snd_soc_dapm_disable_pin(dapm, "LINEOUTL");
	snd_soc_dapm_disable_pin(dapm, "LINEOUTR");
	lo_dac = 3;
	hs_switch = 0;
	/* we dont use linein in this so set to NC */
	snd_soc_dapm_disable_pin(dapm, "LINEINL");
	snd_soc_dapm_disable_pin(dapm, "LINEINR");

	/* Headset and button jack detection */
	ret_val = snd_soc_jack_new(codec, "Intel(R) MID Audio Jack",
			SND_JACK_HEADSET | SND_JACK_BTN_0 |
			SND_JACK_BTN_1, &mfld_jack);
	if (ret_val) {
		pr_err("jack creation failed\n");
		return ret_val;
	}

	ret_val = snd_soc_jack_add_pins(&mfld_jack,
			ARRAY_SIZE(mfld_jack_pins), mfld_jack_pins);
	if (ret_val) {
		pr_err("adding jack pins failed\n");
		return ret_val;
	}
	ret_val = snd_soc_jack_add_zones(&mfld_jack,
			ARRAY_SIZE(mfld_zones), mfld_zones);
	if (ret_val) {
		pr_err("adding jack zones failed\n");
		return ret_val;
	}

	/* we want to check if anything is inserted at boot,
	 * so send a fake event to codec and it will read adc
	 * to find if anything is there or not */
	mfld_jack_check(MFLD_JACK_INSERT);
	return ret_val;
}

static struct snd_soc_dai_link mfld_msic_dailink[] = {
	{
		.name = "Medfield Headset",
		.stream_name = "Headset",
		.cpu_dai_name = "Headset-cpu-dai",
		.codec_dai_name = "SN95031 Headset",
		.codec_name = "sn95031",
		.platform_name = "sst-platform",
		.init = mfld_init,
	},
	{
		.name = "Medfield Speaker",
		.stream_name = "Speaker",
		.cpu_dai_name = "Speaker-cpu-dai",
		.codec_dai_name = "SN95031 Speaker",
		.codec_name = "sn95031",
		.platform_name = "sst-platform",
		.init = NULL,
	},
	{
		.name = "Medfield Vibra",
		.stream_name = "Vibra1",
		.cpu_dai_name = "Vibra1-cpu-dai",
		.codec_dai_name = "SN95031 Vibra1",
		.codec_name = "sn95031",
		.platform_name = "sst-platform",
		.init = NULL,
	},
	{
		.name = "Medfield Haptics",
		.stream_name = "Vibra2",
		.cpu_dai_name = "Vibra2-cpu-dai",
		.codec_dai_name = "SN95031 Vibra2",
		.codec_name = "sn95031",
		.platform_name = "sst-platform",
		.init = NULL,
	},
	{
		.name = "Medfield Compress",
		.stream_name = "Speaker",
		.cpu_dai_name = "Compress-cpu-dai",
		.codec_dai_name = "SN95031 Speaker",
		.codec_name = "sn95031",
		.platform_name = "sst-platform",
		.init = NULL,
	},
};

/* SoC card */
static struct snd_soc_card snd_soc_card_mfld = {
	.name = "medfield_audio",
	.owner = THIS_MODULE,
	.dai_link = mfld_msic_dailink,
	.num_links = ARRAY_SIZE(mfld_msic_dailink),
};

static irqreturn_t snd_mfld_jack_intr_handler(int irq, void *dev)
{
	struct mfld_mc_private *mc_private = (struct mfld_mc_private *) dev;

	memcpy_fromio(&mc_private->interrupt_status,
			((void *)(mc_private->int_base)),
			sizeof(u8));
	return IRQ_WAKE_THREAD;
}

static irqreturn_t snd_mfld_jack_detection(int irq, void *data)
{
	struct mfld_mc_private *mc_drv_ctx = (struct mfld_mc_private *) data;

	if (mfld_jack.codec == NULL)
		return IRQ_HANDLED;
	mfld_jack_check(mc_drv_ctx->interrupt_status);

	return IRQ_HANDLED;
}

static int snd_mfld_mc_probe(struct platform_device *pdev)
{
	int ret_val = 0, irq;
	struct mfld_mc_private *mc_drv_ctx;
	struct resource *irq_mem;

	pr_debug("snd_mfld_mc_probe called\n");

	/* retrive the irq number */
	irq = platform_get_irq(pdev, 0);

	/* audio interrupt base of SRAM location where
	 * interrupts are stored by System FW */
	mc_drv_ctx = kzalloc(sizeof(*mc_drv_ctx), GFP_ATOMIC);
	if (!mc_drv_ctx) {
		pr_err("allocation failed\n");
		return -ENOMEM;
	}

	irq_mem = platform_get_resource_byname(
				pdev, IORESOURCE_MEM, "IRQ_BASE");
	if (!irq_mem) {
		pr_err("no mem resource given\n");
		ret_val = -ENODEV;
		goto unalloc;
	}
	mc_drv_ctx->int_base = ioremap_nocache(irq_mem->start,
					resource_size(irq_mem));
	if (!mc_drv_ctx->int_base) {
		pr_err("Mapping of cache failed\n");
		ret_val = -ENOMEM;
		goto unalloc;
	}
	/* register for interrupt */
	ret_val = request_threaded_irq(irq, snd_mfld_jack_intr_handler,
			snd_mfld_jack_detection,
			IRQF_SHARED, pdev->dev.driver->name, mc_drv_ctx);
	if (ret_val) {
		pr_err("cannot register IRQ\n");
		goto unalloc;
	}
	/* register the soc card */
	snd_soc_card_mfld.dev = &pdev->dev;
	ret_val = snd_soc_register_card(&snd_soc_card_mfld);
	if (ret_val) {
		pr_debug("snd_soc_register_card failed %d\n", ret_val);
		goto freeirq;
	}
	platform_set_drvdata(pdev, mc_drv_ctx);
	pr_debug("successfully exited probe\n");
	return ret_val;

freeirq:
	free_irq(irq, mc_drv_ctx);
unalloc:
	kfree(mc_drv_ctx);
	return ret_val;
}

static int snd_mfld_mc_remove(struct platform_device *pdev)
{
	struct mfld_mc_private *mc_drv_ctx = platform_get_drvdata(pdev);

	pr_debug("snd_mfld_mc_remove called\n");
	free_irq(platform_get_irq(pdev, 0), mc_drv_ctx);
	snd_soc_unregister_card(&snd_soc_card_mfld);
	kfree(mc_drv_ctx);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_driver snd_mfld_mc_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "msic_audio",
	},
	.probe = snd_mfld_mc_probe,
	.remove = snd_mfld_mc_remove,
};

module_platform_driver(snd_mfld_mc_driver);

MODULE_DESCRIPTION("ASoC Intel(R) MID Machine driver");
MODULE_AUTHOR("Vinod Koul <vinod.koul@intel.com>");
MODULE_AUTHOR("Harsha Priya <priya.harsha@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:msic-audio");
