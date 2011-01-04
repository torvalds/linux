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
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "../codecs/sn95031.h"

#define MID_MONO 1
#define MID_STEREO 2
#define MID_MAX_CAP 5

static unsigned int	hs_switch;
static unsigned int	lo_dac;

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
		snd_soc_dapm_enable_pin(&codec->dapm, "HPOUTL");
		snd_soc_dapm_enable_pin(&codec->dapm, "HPOUTR");
		snd_soc_dapm_disable_pin(&codec->dapm, "EPOUT");
	} else {
		pr_debug("hs_set EP path\n");
		snd_soc_dapm_disable_pin(&codec->dapm, "HPOUTL");
		snd_soc_dapm_disable_pin(&codec->dapm, "HPOUTR");
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
		snd_soc_dapm_enable_pin(&codec->dapm, "HPOUTL");
		snd_soc_dapm_enable_pin(&codec->dapm, "HPOUTR");
		snd_soc_dapm_disable_pin(&codec->dapm, "EPOUT");
	} else {
		snd_soc_dapm_disable_pin(&codec->dapm, "HPOUTL");
		snd_soc_dapm_disable_pin(&codec->dapm, "HPOUTR");
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
		snd_soc_dapm_disable_pin(&codec->dapm, "HPOUTL");
		snd_soc_dapm_disable_pin(&codec->dapm, "HPOUTR");
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

static int mfld_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_codec *codec = runtime->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret_val;

	ret_val = snd_soc_add_controls(codec, mfld_snd_controls,
				ARRAY_SIZE(mfld_snd_controls));
	if (ret_val) {
		pr_err("soc_add_controls failed %d", ret_val);
		return ret_val;
	}
	/* default is earpiece pin, userspace sets it explcitly */
	snd_soc_dapm_disable_pin(dapm, "HPOUTL");
	snd_soc_dapm_disable_pin(dapm, "HPOUTR");
	/* default is lineout NC, userspace sets it explcitly */
	snd_soc_dapm_disable_pin(dapm, "LINEOUTL");
	snd_soc_dapm_disable_pin(dapm, "LINEOUTR");
	lo_dac = 3;
	hs_switch = 0;
	return snd_soc_dapm_sync(dapm);
}

struct snd_soc_dai_link mfld_msic_dailink[] = {
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
};

/* SoC card */
static struct snd_soc_card snd_soc_card_mfld = {
	.name = "medfield_audio",
	.dai_link = mfld_msic_dailink,
	.num_links = ARRAY_SIZE(mfld_msic_dailink),
};

static int __devinit snd_mfld_mc_probe(struct platform_device *pdev)
{
	struct platform_device *socdev;
	int ret_val = 0;

	pr_debug("snd_mfld_mc_probe called\n");

	socdev =  platform_device_alloc("soc-audio", -1);
	if (!socdev) {
		pr_err("soc-audio device allocation failed\n");
		return -ENOMEM;
	}
	platform_set_drvdata(socdev, &snd_soc_card_mfld);
	ret_val = platform_device_add(socdev);
	if (ret_val) {
		pr_err("Unable to add soc-audio device, err %d\n", ret_val);
		platform_device_put(socdev);
	}

	platform_set_drvdata(pdev, socdev);

	pr_debug("successfully exited probe\n");
	return ret_val;
}

static int __devexit snd_mfld_mc_remove(struct platform_device *pdev)
{
	struct platform_device *socdev =  platform_get_drvdata(pdev);
	pr_debug("snd_mfld_mc_remove called\n");

	platform_device_unregister(socdev);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_driver snd_mfld_mc_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "msic_audio",
	},
	.probe = snd_mfld_mc_probe,
	.remove = __devexit_p(snd_mfld_mc_remove),
};

static int __init snd_mfld_driver_init(void)
{
	pr_debug("snd_mfld_driver_init called\n");
	return platform_driver_register(&snd_mfld_mc_driver);
}
module_init(snd_mfld_driver_init);

static void __exit snd_mfld_driver_exit(void)
{
	pr_debug("snd_mfld_driver_exit called\n");
	platform_driver_unregister(&snd_mfld_mc_driver);
}
module_exit(snd_mfld_driver_exit);

MODULE_DESCRIPTION("ASoC Intel(R) MID Machine driver");
MODULE_AUTHOR("Vinod Koul <vinod.koul@intel.com>");
MODULE_AUTHOR("Harsha Priya <priya.harsha@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:msic-audio");
