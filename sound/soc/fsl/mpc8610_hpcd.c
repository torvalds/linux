// SPDX-License-Identifier: GPL-2.0
//
// Freescale MPC8610HPCD ALSA SoC Machine driver
//
// Author: Timur Tabi <timur@freescale.com>
//
// Copyright 2007-2010 Freescale Semiconductor, Inc.

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/fsl/guts.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <sound/soc.h>

#include "fsl_dma.h"
#include "fsl_ssi.h"
#include "fsl_utils.h"

/* There's only one global utilities register */
static phys_addr_t guts_phys;

/**
 * mpc8610_hpcd_data: machine-specific ASoC device data
 *
 * This structure contains data for a single sound platform device on an
 * MPC8610 HPCD.  Some of the data is taken from the device tree.
 */
struct mpc8610_hpcd_data {
	struct snd_soc_dai_link dai[2];
	struct snd_soc_card card;
	unsigned int dai_format;
	unsigned int codec_clk_direction;
	unsigned int cpu_clk_direction;
	unsigned int clk_frequency;
	unsigned int ssi_id;		/* 0 = SSI1, 1 = SSI2, etc */
	unsigned int dma_id[2];		/* 0 = DMA1, 1 = DMA2, etc */
	unsigned int dma_channel_id[2]; /* 0 = ch 0, 1 = ch 1, etc*/
	char codec_dai_name[DAI_NAME_SIZE];
	char platform_name[2][DAI_NAME_SIZE]; /* One for each DMA channel */
};

/**
 * mpc8610_hpcd_machine_probe: initialize the board
 *
 * This function is used to initialize the board-specific hardware.
 *
 * Here we program the DMACR and PMUXCR registers.
 */
static int mpc8610_hpcd_machine_probe(struct snd_soc_card *card)
{
	struct mpc8610_hpcd_data *machine_data =
		container_of(card, struct mpc8610_hpcd_data, card);
	struct ccsr_guts __iomem *guts;

	guts = ioremap(guts_phys, sizeof(struct ccsr_guts));
	if (!guts) {
		dev_err(card->dev, "could not map global utilities\n");
		return -ENOMEM;
	}

	/* Program the signal routing between the SSI and the DMA */
	guts_set_dmacr(guts, machine_data->dma_id[0],
		       machine_data->dma_channel_id[0],
		       CCSR_GUTS_DMACR_DEV_SSI);
	guts_set_dmacr(guts, machine_data->dma_id[1],
		       machine_data->dma_channel_id[1],
		       CCSR_GUTS_DMACR_DEV_SSI);

	guts_set_pmuxcr_dma(guts, machine_data->dma_id[0],
			    machine_data->dma_channel_id[0], 0);
	guts_set_pmuxcr_dma(guts, machine_data->dma_id[1],
			    machine_data->dma_channel_id[1], 0);

	switch (machine_data->ssi_id) {
	case 0:
		clrsetbits_be32(&guts->pmuxcr,
			CCSR_GUTS_PMUXCR_SSI1_MASK, CCSR_GUTS_PMUXCR_SSI1_SSI);
		break;
	case 1:
		clrsetbits_be32(&guts->pmuxcr,
			CCSR_GUTS_PMUXCR_SSI2_MASK, CCSR_GUTS_PMUXCR_SSI2_SSI);
		break;
	}

	iounmap(guts);

	return 0;
}

/**
 * mpc8610_hpcd_startup: program the board with various hardware parameters
 *
 * This function takes board-specific information, like clock frequencies
 * and serial data formats, and passes that information to the codec and
 * transport drivers.
 */
static int mpc8610_hpcd_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct mpc8610_hpcd_data *machine_data =
		container_of(rtd->card, struct mpc8610_hpcd_data, card);
	struct device *dev = rtd->card->dev;
	int ret = 0;

	/* Tell the codec driver what the serial protocol is. */
	ret = snd_soc_dai_set_fmt(asoc_rtd_to_codec(rtd, 0), machine_data->dai_format);
	if (ret < 0) {
		dev_err(dev, "could not set codec driver audio format\n");
		return ret;
	}

	/*
	 * Tell the codec driver what the MCLK frequency is, and whether it's
	 * a slave or master.
	 */
	ret = snd_soc_dai_set_sysclk(asoc_rtd_to_codec(rtd, 0), 0,
				     machine_data->clk_frequency,
				     machine_data->codec_clk_direction);
	if (ret < 0) {
		dev_err(dev, "could not set codec driver clock params\n");
		return ret;
	}

	return 0;
}

/**
 * mpc8610_hpcd_machine_remove: Remove the sound device
 *
 * This function is called to remove the sound device for one SSI.  We
 * de-program the DMACR and PMUXCR register.
 */
static int mpc8610_hpcd_machine_remove(struct snd_soc_card *card)
{
	struct mpc8610_hpcd_data *machine_data =
		container_of(card, struct mpc8610_hpcd_data, card);
	struct ccsr_guts __iomem *guts;

	guts = ioremap(guts_phys, sizeof(struct ccsr_guts));
	if (!guts) {
		dev_err(card->dev, "could not map global utilities\n");
		return -ENOMEM;
	}

	/* Restore the signal routing */

	guts_set_dmacr(guts, machine_data->dma_id[0],
		       machine_data->dma_channel_id[0], 0);
	guts_set_dmacr(guts, machine_data->dma_id[1],
		       machine_data->dma_channel_id[1], 0);

	switch (machine_data->ssi_id) {
	case 0:
		clrsetbits_be32(&guts->pmuxcr,
			CCSR_GUTS_PMUXCR_SSI1_MASK, CCSR_GUTS_PMUXCR_SSI1_LA);
		break;
	case 1:
		clrsetbits_be32(&guts->pmuxcr,
			CCSR_GUTS_PMUXCR_SSI2_MASK, CCSR_GUTS_PMUXCR_SSI2_LA);
		break;
	}

	iounmap(guts);

	return 0;
}

/**
 * mpc8610_hpcd_ops: ASoC machine driver operations
 */
static const struct snd_soc_ops mpc8610_hpcd_ops = {
	.startup = mpc8610_hpcd_startup,
};

/**
 * mpc8610_hpcd_probe: platform probe function for the machine driver
 *
 * Although this is a machine driver, the SSI node is the "master" node with
 * respect to audio hardware connections.  Therefore, we create a new ASoC
 * device for each new SSI node that has a codec attached.
 */
static int mpc8610_hpcd_probe(struct platform_device *pdev)
{
	struct device *dev = pdev->dev.parent;
	/* ssi_pdev is the platform device for the SSI node that probed us */
	struct platform_device *ssi_pdev = to_platform_device(dev);
	struct device_node *np = ssi_pdev->dev.of_node;
	struct device_node *codec_np = NULL;
	struct mpc8610_hpcd_data *machine_data;
	struct snd_soc_dai_link_component *comp;
	int ret;
	const char *sprop;
	const u32 *iprop;

	/* Find the codec node for this SSI. */
	codec_np = of_parse_phandle(np, "codec-handle", 0);
	if (!codec_np) {
		dev_err(dev, "invalid codec node\n");
		return -EINVAL;
	}

	machine_data = kzalloc(sizeof(struct mpc8610_hpcd_data), GFP_KERNEL);
	if (!machine_data) {
		ret = -ENOMEM;
		goto error_alloc;
	}

	comp = devm_kzalloc(&pdev->dev, 6 * sizeof(*comp), GFP_KERNEL);
	if (!comp) {
		ret = -ENOMEM;
		goto error_alloc;
	}

	machine_data->dai[0].cpus	= &comp[0];
	machine_data->dai[0].codecs	= &comp[1];
	machine_data->dai[0].platforms	= &comp[2];

	machine_data->dai[0].num_cpus		= 1;
	machine_data->dai[0].num_codecs		= 1;
	machine_data->dai[0].num_platforms	= 1;

	machine_data->dai[1].cpus	= &comp[3];
	machine_data->dai[1].codecs	= &comp[4];
	machine_data->dai[1].platforms	= &comp[5];

	machine_data->dai[1].num_cpus		= 1;
	machine_data->dai[1].num_codecs		= 1;
	machine_data->dai[1].num_platforms	= 1;

	machine_data->dai[0].cpus->dai_name = dev_name(&ssi_pdev->dev);
	machine_data->dai[0].ops = &mpc8610_hpcd_ops;

	/* ASoC core can match codec with device node */
	machine_data->dai[0].codecs->of_node = codec_np;

	/* The DAI name from the codec (snd_soc_dai_driver.name) */
	machine_data->dai[0].codecs->dai_name = "cs4270-hifi";

	/* We register two DAIs per SSI, one for playback and the other for
	 * capture.  Currently, we only support codecs that have one DAI for
	 * both playback and capture.
	 */
	memcpy(&machine_data->dai[1], &machine_data->dai[0],
	       sizeof(struct snd_soc_dai_link));

	/* Get the device ID */
	iprop = of_get_property(np, "cell-index", NULL);
	if (!iprop) {
		dev_err(&pdev->dev, "cell-index property not found\n");
		ret = -EINVAL;
		goto error;
	}
	machine_data->ssi_id = be32_to_cpup(iprop);

	/* Get the serial format and clock direction. */
	sprop = of_get_property(np, "fsl,mode", NULL);
	if (!sprop) {
		dev_err(&pdev->dev, "fsl,mode property not found\n");
		ret = -EINVAL;
		goto error;
	}

	if (strcasecmp(sprop, "i2s-slave") == 0) {
		machine_data->dai_format =
			SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBP_CFP;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_OUT;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_IN;

		/* In i2s-slave mode, the codec has its own clock source, so we
		 * need to get the frequency from the device tree and pass it to
		 * the codec driver.
		 */
		iprop = of_get_property(codec_np, "clock-frequency", NULL);
		if (!iprop || !*iprop) {
			dev_err(&pdev->dev, "codec bus-frequency "
				"property is missing or invalid\n");
			ret = -EINVAL;
			goto error;
		}
		machine_data->clk_frequency = be32_to_cpup(iprop);
	} else if (strcasecmp(sprop, "i2s-master") == 0) {
		machine_data->dai_format =
			SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBC_CFC;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_IN;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_OUT;
	} else if (strcasecmp(sprop, "lj-slave") == 0) {
		machine_data->dai_format =
			SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_CBP_CFP;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_OUT;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_IN;
	} else if (strcasecmp(sprop, "lj-master") == 0) {
		machine_data->dai_format =
			SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_CBC_CFC;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_IN;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_OUT;
	} else if (strcasecmp(sprop, "rj-slave") == 0) {
		machine_data->dai_format =
			SND_SOC_DAIFMT_RIGHT_J | SND_SOC_DAIFMT_CBP_CFP;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_OUT;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_IN;
	} else if (strcasecmp(sprop, "rj-master") == 0) {
		machine_data->dai_format =
			SND_SOC_DAIFMT_RIGHT_J | SND_SOC_DAIFMT_CBC_CFC;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_IN;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_OUT;
	} else if (strcasecmp(sprop, "ac97-slave") == 0) {
		machine_data->dai_format =
			SND_SOC_DAIFMT_AC97 | SND_SOC_DAIFMT_CBP_CFP;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_OUT;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_IN;
	} else if (strcasecmp(sprop, "ac97-master") == 0) {
		machine_data->dai_format =
			SND_SOC_DAIFMT_AC97 | SND_SOC_DAIFMT_CBC_CFC;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_IN;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_OUT;
	} else {
		dev_err(&pdev->dev,
			"unrecognized fsl,mode property '%s'\n", sprop);
		ret = -EINVAL;
		goto error;
	}

	if (!machine_data->clk_frequency) {
		dev_err(&pdev->dev, "unknown clock frequency\n");
		ret = -EINVAL;
		goto error;
	}

	/* Find the playback DMA channel to use. */
	machine_data->dai[0].platforms->name = machine_data->platform_name[0];
	ret = fsl_asoc_get_dma_channel(np, "fsl,playback-dma",
				       &machine_data->dai[0],
				       &machine_data->dma_channel_id[0],
				       &machine_data->dma_id[0]);
	if (ret) {
		dev_err(&pdev->dev, "missing/invalid playback DMA phandle\n");
		goto error;
	}

	/* Find the capture DMA channel to use. */
	machine_data->dai[1].platforms->name = machine_data->platform_name[1];
	ret = fsl_asoc_get_dma_channel(np, "fsl,capture-dma",
				       &machine_data->dai[1],
				       &machine_data->dma_channel_id[1],
				       &machine_data->dma_id[1]);
	if (ret) {
		dev_err(&pdev->dev, "missing/invalid capture DMA phandle\n");
		goto error;
	}

	/* Initialize our DAI data structure.  */
	machine_data->dai[0].stream_name = "playback";
	machine_data->dai[1].stream_name = "capture";
	machine_data->dai[0].name = machine_data->dai[0].stream_name;
	machine_data->dai[1].name = machine_data->dai[1].stream_name;

	machine_data->card.probe = mpc8610_hpcd_machine_probe;
	machine_data->card.remove = mpc8610_hpcd_machine_remove;
	machine_data->card.name = pdev->name; /* The platform driver name */
	machine_data->card.owner = THIS_MODULE;
	machine_data->card.dev = &pdev->dev;
	machine_data->card.num_links = 2;
	machine_data->card.dai_link = machine_data->dai;

	/* Register with ASoC */
	ret = snd_soc_register_card(&machine_data->card);
	if (ret) {
		dev_err(&pdev->dev, "could not register card\n");
		goto error;
	}

	of_node_put(codec_np);

	return 0;

error:
	kfree(machine_data);
error_alloc:
	of_node_put(codec_np);
	return ret;
}

/**
 * mpc8610_hpcd_remove: remove the platform device
 *
 * This function is called when the platform device is removed.
 */
static int mpc8610_hpcd_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct mpc8610_hpcd_data *machine_data =
		container_of(card, struct mpc8610_hpcd_data, card);

	snd_soc_unregister_card(card);
	kfree(machine_data);

	return 0;
}

static struct platform_driver mpc8610_hpcd_driver = {
	.probe = mpc8610_hpcd_probe,
	.remove = mpc8610_hpcd_remove,
	.driver = {
		/* The name must match 'compatible' property in the device tree,
		 * in lowercase letters.
		 */
		.name = "snd-soc-mpc8610hpcd",
	},
};

/**
 * mpc8610_hpcd_init: machine driver initialization.
 *
 * This function is called when this module is loaded.
 */
static int __init mpc8610_hpcd_init(void)
{
	struct device_node *guts_np;
	struct resource res;

	pr_info("Freescale MPC8610 HPCD ALSA SoC machine driver\n");

	/* Get the physical address of the global utilities registers */
	guts_np = of_find_compatible_node(NULL, NULL, "fsl,mpc8610-guts");
	if (of_address_to_resource(guts_np, 0, &res)) {
		pr_err("mpc8610-hpcd: missing/invalid global utilities node\n");
		of_node_put(guts_np);
		return -EINVAL;
	}
	guts_phys = res.start;
	of_node_put(guts_np);

	return platform_driver_register(&mpc8610_hpcd_driver);
}

/**
 * mpc8610_hpcd_exit: machine driver exit
 *
 * This function is called when this driver is unloaded.
 */
static void __exit mpc8610_hpcd_exit(void)
{
	platform_driver_unregister(&mpc8610_hpcd_driver);
}

module_init(mpc8610_hpcd_init);
module_exit(mpc8610_hpcd_exit);

MODULE_AUTHOR("Timur Tabi <timur@freescale.com>");
MODULE_DESCRIPTION("Freescale MPC8610 HPCD ALSA SoC machine driver");
MODULE_LICENSE("GPL v2");
