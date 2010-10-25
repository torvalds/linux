/*
 * FSI - HDMI sound support
 *
 * Copyright (C) 2010 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/platform_device.h>
#include <sound/sh_fsi.h>

static struct snd_soc_dai_link fsi_dai_link = {
	.name		= "HDMI",
	.stream_name	= "HDMI",
	.cpu_dai_name	= "fsib-dai", /* fsi B */
	.codec_dai_name	= "sh_mobile_hdmi-hifi",
	.platform_name	= "sh_fsi2",
	.codec_name	= "sh-mobile-hdmi",
};

static struct snd_soc_card fsi_soc_card  = {
	.name		= "FSI (SH MOBILE HDMI)",
	.dai_link	= &fsi_dai_link,
	.num_links	= 1,
};

static struct platform_device *fsi_snd_device;

static int __init fsi_hdmi_init(void)
{
	int ret = -ENOMEM;

	fsi_snd_device = platform_device_alloc("soc-audio", FSI_PORT_B);
	if (!fsi_snd_device)
		goto out;

	platform_set_drvdata(fsi_snd_device, &fsi_soc_card);
	ret = platform_device_add(fsi_snd_device);

	if (ret)
		platform_device_put(fsi_snd_device);

out:
	return ret;
}

static void __exit fsi_hdmi_exit(void)
{
	platform_device_unregister(fsi_snd_device);
}

module_init(fsi_hdmi_init);
module_exit(fsi_hdmi_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic SH4 FSI-HDMI sound card");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
