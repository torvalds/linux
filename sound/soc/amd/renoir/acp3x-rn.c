// SPDX-License-Identifier: GPL-2.0+
//
// Machine driver for AMD Renoir platform using DMIC
//
//Copyright 2020 Advanced Micro Devices, Inc.

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/io.h>

#include "rn_acp3x.h"

#define DRV_NAME "acp_pdm_mach"

SND_SOC_DAILINK_DEF(acp_pdm,
		    DAILINK_COMP_ARRAY(COMP_CPU("acp_rn_pdm_dma.0")));

SND_SOC_DAILINK_DEF(dmic_codec,
		    DAILINK_COMP_ARRAY(COMP_CODEC("dmic-codec.0",
						  "dmic-hifi")));

SND_SOC_DAILINK_DEF(platform,
		    DAILINK_COMP_ARRAY(COMP_PLATFORM("acp_rn_pdm_dma.0")));

static struct snd_soc_dai_link acp_dai_pdm[] = {
	{
		.name = "acp3x-dmic-capture",
		.stream_name = "DMIC capture",
		.capture_only = 1,
		SND_SOC_DAILINK_REG(acp_pdm, dmic_codec, platform),
	},
};

static struct snd_soc_card acp_card = {
	.name = "acp",
	.owner = THIS_MODULE,
	.dai_link = acp_dai_pdm,
	.num_links = 1,
};

static int acp_probe(struct platform_device *pdev)
{
	int ret;
	struct acp_pdm *machine = NULL;
	struct snd_soc_card *card;

	card = &acp_card;
	acp_card.dev = &pdev->dev;

	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, machine);
	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret,
				"snd_soc_register_card(%s) failed\n",
				card->name);
	}
	return 0;
}

static struct platform_driver acp_mach_driver = {
	.driver = {
		.name = "acp_pdm_mach",
		.pm = &snd_soc_pm_ops,
	},
	.probe = acp_probe,
};

module_platform_driver(acp_mach_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD Renoir support for DMIC");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
