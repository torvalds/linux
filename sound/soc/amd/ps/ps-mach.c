// SPDX-License-Identifier: GPL-2.0-only
/*
 * Machine driver for AMD Pink Sardine platform using DMIC
 *
 * Copyright 2022 Advanced Micro Devices, Inc.
 */

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/io.h>
#include <linux/dmi.h>

#include "acp63.h"

#define DRV_NAME "acp_ps_mach"

SND_SOC_DAILINK_DEF(acp63_pdm,
		    DAILINK_COMP_ARRAY(COMP_CPU("acp_ps_pdm_dma.0")));

SND_SOC_DAILINK_DEF(dmic_codec,
		    DAILINK_COMP_ARRAY(COMP_CODEC("dmic-codec.0",
						  "dmic-hifi")));

SND_SOC_DAILINK_DEF(pdm_platform,
		    DAILINK_COMP_ARRAY(COMP_PLATFORM("acp_ps_pdm_dma.0")));

static struct snd_soc_dai_link acp63_dai_pdm[] = {
	{
		.name = "acp63-dmic-capture",
		.stream_name = "DMIC capture",
		.capture_only = 1,
		SND_SOC_DAILINK_REG(acp63_pdm, dmic_codec, pdm_platform),
	},
};

static struct snd_soc_card acp63_card = {
	.name = "acp63",
	.owner = THIS_MODULE,
	.dai_link = acp63_dai_pdm,
	.num_links = 1,
};

static int acp63_probe(struct platform_device *pdev)
{
	struct acp63_pdm *machine = NULL;
	struct snd_soc_card *card;
	int ret;

	platform_set_drvdata(pdev, &acp63_card);
	card = platform_get_drvdata(pdev);
	acp63_card.dev = &pdev->dev;

	snd_soc_card_set_drvdata(card, machine);
	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret,
				"snd_soc_register_card(%s) failed\n",
				card->name);
	}

	return 0;
}

static struct platform_driver acp63_mach_driver = {
	.driver = {
		.name = "acp_ps_mach",
		.pm = &snd_soc_pm_ops,
	},
	.probe = acp63_probe,
};

module_platform_driver(acp63_mach_driver);

MODULE_AUTHOR("Syed.SabaKareem@amd.com");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
