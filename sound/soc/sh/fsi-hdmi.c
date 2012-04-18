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
#include <linux/module.h>
#include <sound/sh_fsi.h>

struct fsi_hdmi_data {
	const char *cpu_dai;
	const char *card;
	int id;
};

static int fsi_hdmi_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu = rtd->cpu_dai;
	int ret;

	ret = snd_soc_dai_set_fmt(cpu, SND_SOC_DAIFMT_CBM_CFM);

	return ret;
}

static struct snd_soc_dai_link fsi_dai_link = {
	.name		= "HDMI",
	.stream_name	= "HDMI",
	.codec_dai_name	= "sh_mobile_hdmi-hifi",
	.platform_name	= "sh_fsi2",
	.codec_name	= "sh-mobile-hdmi",
	.init		= fsi_hdmi_dai_init,
};

static struct snd_soc_card fsi_soc_card  = {
	.owner		= THIS_MODULE,
	.dai_link	= &fsi_dai_link,
	.num_links	= 1,
};

static struct platform_device *fsi_snd_device;

static int fsi_hdmi_probe(struct platform_device *pdev)
{
	int ret = -ENOMEM;
	const struct platform_device_id	*id_entry;
	struct fsi_hdmi_data *pdata;

	id_entry = pdev->id_entry;
	if (!id_entry) {
		dev_err(&pdev->dev, "unknown fsi hdmi\n");
		return -ENODEV;
	}

	pdata = (struct fsi_hdmi_data *)id_entry->driver_data;

	fsi_snd_device = platform_device_alloc("soc-audio", pdata->id);
	if (!fsi_snd_device)
		goto out;

	fsi_dai_link.cpu_dai_name	= pdata->cpu_dai;
	fsi_soc_card.name		= pdata->card;

	platform_set_drvdata(fsi_snd_device, &fsi_soc_card);
	ret = platform_device_add(fsi_snd_device);

	if (ret)
		platform_device_put(fsi_snd_device);

out:
	return ret;
}

static int fsi_hdmi_remove(struct platform_device *pdev)
{
	platform_device_unregister(fsi_snd_device);
	return 0;
}

static struct fsi_hdmi_data fsi2_a_hdmi = {
	.cpu_dai	= "fsia-dai",
	.card		= "FSI2A-HDMI",
	.id		= FSI_PORT_A,
};

static struct fsi_hdmi_data fsi2_b_hdmi = {
	.cpu_dai	= "fsib-dai",
	.card		= "FSI2B-HDMI",
	.id		= FSI_PORT_B,
};

static struct platform_device_id fsi_id_table[] = {
	/* FSI 2 */
	{ "sh_fsi2_a_hdmi",	(kernel_ulong_t)&fsi2_a_hdmi },
	{ "sh_fsi2_b_hdmi",	(kernel_ulong_t)&fsi2_b_hdmi },
	{},
};

static struct platform_driver fsi_hdmi = {
	.driver = {
		.name	= "fsi-hdmi-audio",
	},
	.probe		= fsi_hdmi_probe,
	.remove		= fsi_hdmi_remove,
	.id_table	= fsi_id_table,
};

module_platform_driver(fsi_hdmi);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic SH4 FSI-HDMI sound card");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
