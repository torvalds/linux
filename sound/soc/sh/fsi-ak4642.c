/*
 * FSI-AK464x sound support for ms7724se
 *
 * Copyright (C) 2009 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/platform_device.h>
#include <sound/sh_fsi.h>

static int fsi_ak4642_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_fmt(dai, SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(dai, 0, 11289600, 0);

	return ret;
}

static struct snd_soc_dai_link fsi_dai_link = {
	.name		= "AK4642",
	.stream_name	= "AK4642",
	.cpu_dai_name	= "fsia-dai", /* fsi A */
	.codec_dai_name	= "ak4642-hifi",
#ifdef CONFIG_MACH_AP4EVB
	.platform_name	= "sh_fsi2",
	.codec_name	= "ak4642-codec.0-0013",
#else
	.platform_name	= "sh_fsi.0",
	.codec_name	= "ak4642-codec.0-0012",
#endif
	.init		= fsi_ak4642_dai_init,
	.ops		= NULL,
};

static struct snd_soc_card fsi_soc_card  = {
	.name		= "FSI (AK4642)",
	.dai_link	= &fsi_dai_link,
	.num_links	= 1,
};

static struct platform_device *fsi_snd_device;

static int __init fsi_ak4642_init(void)
{
	int ret = -ENOMEM;

	fsi_snd_device = platform_device_alloc("soc-audio", FSI_PORT_A);
	if (!fsi_snd_device)
		goto out;

	platform_set_drvdata(fsi_snd_device, &fsi_soc_card);
	ret = platform_device_add(fsi_snd_device);

	if (ret)
		platform_device_put(fsi_snd_device);

out:
	return ret;
}

static void __exit fsi_ak4642_exit(void)
{
	platform_device_unregister(fsi_snd_device);
}

module_init(fsi_ak4642_init);
module_exit(fsi_ak4642_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic SH4 FSI-AK4642 sound card");
MODULE_AUTHOR("Kuninori Morimoto <morimoto.kuninori@renesas.com>");
