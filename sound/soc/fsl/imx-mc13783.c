// SPDX-License-Identifier: GPL-2.0+
//
// imx-mc13783.c  --  SoC audio for imx based boards with mc13783 codec
//
// Copyright 2012 Philippe Retornaz, <philippe.retornaz@epfl.ch>
//
// Heavly based on phycore-mc13783:
// Copyright 2009 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <asm/mach-types.h>

#include "../codecs/mc13783.h"
#include "imx-ssi.h"
#include "imx-audmux.h"

#define FMT_SSI (SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_NB_NF | \
		SND_SOC_DAIFMT_CBM_CFM)

static int imx_mc13783_hifi_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	int ret;

	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x3, 0x3, 4, 16);
	if (ret)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, MC13783_CLK_CLIA, 26000000, 0);
	if (ret)
		return ret;

	return snd_soc_dai_set_tdm_slot(cpu_dai, 0x3, 0x3, 2, 16);
}

static const struct snd_soc_ops imx_mc13783_hifi_ops = {
	.hw_params = imx_mc13783_hifi_hw_params,
};

SND_SOC_DAILINK_DEFS(hifi,
	DAILINK_COMP_ARRAY(COMP_CPU("imx-ssi.0")),
	DAILINK_COMP_ARRAY(COMP_CODEC("mc13783-codec", "mc13783-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("imx-ssi.0")));

static struct snd_soc_dai_link imx_mc13783_dai_mc13783[] = {
	{
		.name = "MC13783",
		.stream_name	 = "Sound",
		.ops		 = &imx_mc13783_hifi_ops,
		.symmetric_rates = 1,
		.dai_fmt 	 = FMT_SSI,
		SND_SOC_DAILINK_REG(hifi),
	},
};

static const struct snd_soc_dapm_widget imx_mc13783_widget[] = {
	SND_SOC_DAPM_MIC("Mic", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static const struct snd_soc_dapm_route imx_mc13783_routes[] = {
	{"Speaker", NULL, "LSP"},
	{"Headphone", NULL, "HSL"},
	{"Headphone", NULL, "HSR"},

	{"MC1LIN", NULL, "MC1 Bias"},
	{"MC2IN", NULL, "MC2 Bias"},
	{"MC1 Bias", NULL, "Mic"},
	{"MC2 Bias", NULL, "Mic"},
};

static struct snd_soc_card imx_mc13783 = {
	.name		= "imx_mc13783",
	.owner		= THIS_MODULE,
	.dai_link	= imx_mc13783_dai_mc13783,
	.num_links	= ARRAY_SIZE(imx_mc13783_dai_mc13783),
	.dapm_widgets	= imx_mc13783_widget,
	.num_dapm_widgets = ARRAY_SIZE(imx_mc13783_widget),
	.dapm_routes	= imx_mc13783_routes,
	.num_dapm_routes = ARRAY_SIZE(imx_mc13783_routes),
};

static int imx_mc13783_probe(struct platform_device *pdev)
{
	int ret;

	imx_mc13783.dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, &imx_mc13783);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		return ret;
	}

	if (machine_is_mx31_3ds() || machine_is_mx31moboard()) {
		imx_audmux_v2_configure_port(MX31_AUDMUX_PORT4_SSI_PINS_4,
			IMX_AUDMUX_V2_PTCR_SYN,
			IMX_AUDMUX_V2_PDCR_RXDSEL(MX31_AUDMUX_PORT1_SSI0) |
			IMX_AUDMUX_V2_PDCR_MODE(1) |
			IMX_AUDMUX_V2_PDCR_INMMASK(0xfc));
		imx_audmux_v2_configure_port(MX31_AUDMUX_PORT1_SSI0,
			IMX_AUDMUX_V2_PTCR_SYN |
			IMX_AUDMUX_V2_PTCR_TFSDIR |
			IMX_AUDMUX_V2_PTCR_TFSEL(MX31_AUDMUX_PORT4_SSI_PINS_4) |
			IMX_AUDMUX_V2_PTCR_TCLKDIR |
			IMX_AUDMUX_V2_PTCR_TCSEL(MX31_AUDMUX_PORT4_SSI_PINS_4) |
			IMX_AUDMUX_V2_PTCR_RFSDIR |
			IMX_AUDMUX_V2_PTCR_RFSEL(MX31_AUDMUX_PORT4_SSI_PINS_4) |
			IMX_AUDMUX_V2_PTCR_RCLKDIR |
			IMX_AUDMUX_V2_PTCR_RCSEL(MX31_AUDMUX_PORT4_SSI_PINS_4),
			IMX_AUDMUX_V2_PDCR_RXDSEL(MX31_AUDMUX_PORT4_SSI_PINS_4));
	} else if (machine_is_mx27_3ds()) {
		imx_audmux_v1_configure_port(MX27_AUDMUX_HPCR1_SSI0,
			IMX_AUDMUX_V1_PCR_SYN |
			IMX_AUDMUX_V1_PCR_TFSDIR |
			IMX_AUDMUX_V1_PCR_TCLKDIR |
			IMX_AUDMUX_V1_PCR_RFSDIR |
			IMX_AUDMUX_V1_PCR_RCLKDIR |
			IMX_AUDMUX_V1_PCR_TFCSEL(MX27_AUDMUX_HPCR3_SSI_PINS_4) |
			IMX_AUDMUX_V1_PCR_RFCSEL(MX27_AUDMUX_HPCR3_SSI_PINS_4) |
			IMX_AUDMUX_V1_PCR_RXDSEL(MX27_AUDMUX_HPCR3_SSI_PINS_4)
		);
		imx_audmux_v1_configure_port(MX27_AUDMUX_HPCR3_SSI_PINS_4,
			IMX_AUDMUX_V1_PCR_SYN |
			IMX_AUDMUX_V1_PCR_RXDSEL(MX27_AUDMUX_HPCR1_SSI0)
		);
	}

	return ret;
}

static struct platform_driver imx_mc13783_audio_driver = {
	.driver = {
		.name = "imx_mc13783",
	},
	.probe = imx_mc13783_probe,
};

module_platform_driver(imx_mc13783_audio_driver);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_AUTHOR("Philippe Retornaz <philippe.retornaz@epfl.ch");
MODULE_DESCRIPTION("imx with mc13783 codec ALSA SoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx_mc13783");
