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

struct fsi_ak4642_data {
	const char *name;
	const char *card;
	const char *cpu_dai;
	const char *codec;
	const char *platform;
};

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
	.codec_dai_name	= "ak4642-hifi",
	.init		= fsi_ak4642_dai_init,
};

static struct snd_soc_card fsi_soc_card  = {
	.dai_link	= &fsi_dai_link,
	.num_links	= 1,
};

static struct platform_device *fsi_snd_device;

static int fsi_ak4642_probe(struct platform_device *pdev)
{
	int ret = -ENOMEM;
	const struct platform_device_id	*id_entry;
	struct fsi_ak4642_data *pdata;

	id_entry = pdev->id_entry;
	if (!id_entry) {
		dev_err(&pdev->dev, "unknown fsi ak4642\n");
		return -ENODEV;
	}

	pdata = (struct fsi_ak4642_data *)id_entry->driver_data;

	fsi_snd_device = platform_device_alloc("soc-audio", FSI_PORT_A);
	if (!fsi_snd_device)
		goto out;

	fsi_dai_link.name		= pdata->name;
	fsi_dai_link.stream_name	= pdata->name;
	fsi_dai_link.cpu_dai_name	= pdata->cpu_dai;
	fsi_dai_link.platform_name	= pdata->platform;
	fsi_dai_link.codec_name		= pdata->codec;
	fsi_soc_card.name		= pdata->card;

	platform_set_drvdata(fsi_snd_device, &fsi_soc_card);
	ret = platform_device_add(fsi_snd_device);

	if (ret)
		platform_device_put(fsi_snd_device);

out:
	return ret;
}

static int fsi_ak4642_remove(struct platform_device *pdev)
{
	platform_device_unregister(fsi_snd_device);
	return 0;
}

static struct fsi_ak4642_data fsi_a_ak4642 = {
	.name		= "AK4642",
	.card		= "FSIA (AK4642)",
	.cpu_dai	= "fsia-dai",
	.codec		= "ak4642-codec.0-0012",
	.platform	= "sh_fsi.0",
};

static struct fsi_ak4642_data fsi_b_ak4642 = {
	.name		= "AK4642",
	.card		= "FSIB (AK4642)",
	.cpu_dai	= "fsib-dai",
	.codec		= "ak4642-codec.0-0012",
	.platform	= "sh_fsi.0",
};

static struct fsi_ak4642_data fsi_a_ak4643 = {
	.name		= "AK4643",
	.card		= "FSIA (AK4643)",
	.cpu_dai	= "fsia-dai",
	.codec		= "ak4642-codec.0-0013",
	.platform	= "sh_fsi.0",
};

static struct fsi_ak4642_data fsi_b_ak4643 = {
	.name		= "AK4643",
	.card		= "FSIB (AK4643)",
	.cpu_dai	= "fsib-dai",
	.codec		= "ak4642-codec.0-0013",
	.platform	= "sh_fsi.0",
};

static struct fsi_ak4642_data fsi2_a_ak4642 = {
	.name		= "AK4642",
	.card		= "FSI2A (AK4642)",
	.cpu_dai	= "fsia-dai",
	.codec		= "ak4642-codec.0-0012",
	.platform	= "sh_fsi2",
};

static struct fsi_ak4642_data fsi2_b_ak4642 = {
	.name		= "AK4642",
	.card		= "FSI2B (AK4642)",
	.cpu_dai	= "fsib-dai",
	.codec		= "ak4642-codec.0-0012",
	.platform	= "sh_fsi2",
};

static struct fsi_ak4642_data fsi2_a_ak4643 = {
	.name		= "AK4643",
	.card		= "FSI2A (AK4643)",
	.cpu_dai	= "fsia-dai",
	.codec		= "ak4642-codec.0-0013",
	.platform	= "sh_fsi2",
};

static struct fsi_ak4642_data fsi2_b_ak4643 = {
	.name		= "AK4643",
	.card		= "FSI2B (AK4643)",
	.cpu_dai	= "fsib-dai",
	.codec		= "ak4642-codec.0-0013",
	.platform	= "sh_fsi2",
};

static struct platform_device_id fsi_id_table[] = {
	/* FSI */
	{ "sh_fsi_a_ak4642",	(kernel_ulong_t)&fsi_a_ak4642 },
	{ "sh_fsi_b_ak4642",	(kernel_ulong_t)&fsi_b_ak4642 },
	{ "sh_fsi_a_ak4643",	(kernel_ulong_t)&fsi_a_ak4643 },
	{ "sh_fsi_b_ak4643",	(kernel_ulong_t)&fsi_b_ak4643 },

	/* FSI 2 */
	{ "sh_fsi2_a_ak4642",	(kernel_ulong_t)&fsi2_a_ak4642 },
	{ "sh_fsi2_b_ak4642",	(kernel_ulong_t)&fsi2_b_ak4642 },
	{ "sh_fsi2_a_ak4643",	(kernel_ulong_t)&fsi2_a_ak4643 },
	{ "sh_fsi2_b_ak4643",	(kernel_ulong_t)&fsi2_b_ak4643 },
	{},
};

static struct platform_driver fsi_ak4642 = {
	.driver = {
		.name	= "fsi-ak4642-audio",
	},
	.probe		= fsi_ak4642_probe,
	.remove		= fsi_ak4642_remove,
	.id_table	= fsi_id_table,
};

static int __init fsi_ak4642_init(void)
{
	return platform_driver_register(&fsi_ak4642);
}

static void __exit fsi_ak4642_exit(void)
{
	platform_driver_unregister(&fsi_ak4642);
}

module_init(fsi_ak4642_init);
module_exit(fsi_ak4642_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic SH4 FSI-AK4642 sound card");
MODULE_AUTHOR("Kuninori Morimoto <morimoto.kuninori@renesas.com>");
