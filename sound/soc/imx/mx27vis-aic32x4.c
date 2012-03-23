/*
 * mx27vis-aic32x4.c
 *
 * Copyright 2011 Vista Silicon S.L.
 *
 * Author: Javier Martin <javier.martin@vista-silicon.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <asm/mach-types.h>
#include <mach/audmux.h>

#include "../codecs/tlv320aic32x4.h"
#include "imx-ssi.h"

static int mx27vis_aic32x4_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;
	u32 dai_format;

	dai_format = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM;

	/* set codec DAI configuration */
	snd_soc_dai_set_fmt(codec_dai, dai_format);

	/* set cpu DAI configuration */
	snd_soc_dai_set_fmt(cpu_dai, dai_format);

	ret = snd_soc_dai_set_sysclk(codec_dai, 0,
				     25000000, SND_SOC_CLOCK_OUT);
	if (ret) {
		pr_err("%s: failed setting codec sysclk\n", __func__);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, IMX_SSP_SYS_CLK, 0,
				SND_SOC_CLOCK_IN);
	if (ret) {
		pr_err("can't set CPU system clock IMX_SSP_SYS_CLK\n");
		return ret;
	}

	return 0;
}

static struct snd_soc_ops mx27vis_aic32x4_snd_ops = {
	.hw_params	= mx27vis_aic32x4_hw_params,
};

static struct snd_soc_dai_link mx27vis_aic32x4_dai = {
	.name		= "tlv320aic32x4",
	.stream_name	= "TLV320AIC32X4",
	.codec_dai_name	= "tlv320aic32x4-hifi",
	.platform_name	= "imx-pcm-audio.0",
	.codec_name	= "tlv320aic32x4.0-0018",
	.cpu_dai_name	= "imx-ssi.0",
	.ops		= &mx27vis_aic32x4_snd_ops,
};

static struct snd_soc_card mx27vis_aic32x4 = {
	.name		= "visstrim_m10-audio",
	.owner		= THIS_MODULE,
	.dai_link	= &mx27vis_aic32x4_dai,
	.num_links	= 1,
};

static struct platform_device *mx27vis_aic32x4_snd_device;

static int __init mx27vis_aic32x4_init(void)
{
	int ret;

	mx27vis_aic32x4_snd_device = platform_device_alloc("soc-audio", -1);
	if (!mx27vis_aic32x4_snd_device)
		return -ENOMEM;

	platform_set_drvdata(mx27vis_aic32x4_snd_device, &mx27vis_aic32x4);
	ret = platform_device_add(mx27vis_aic32x4_snd_device);

	if (ret) {
		printk(KERN_ERR "ASoC: Platform device allocation failed\n");
		platform_device_put(mx27vis_aic32x4_snd_device);
	}

	/* Connect SSI0 as clock slave to SSI1 external pins */
	mxc_audmux_v1_configure_port(MX27_AUDMUX_HPCR1_SSI0,
			MXC_AUDMUX_V1_PCR_SYN |
			MXC_AUDMUX_V1_PCR_TFSDIR |
			MXC_AUDMUX_V1_PCR_TCLKDIR |
			MXC_AUDMUX_V1_PCR_TFCSEL(MX27_AUDMUX_PPCR1_SSI_PINS_1) |
			MXC_AUDMUX_V1_PCR_RXDSEL(MX27_AUDMUX_PPCR1_SSI_PINS_1)
	);
	mxc_audmux_v1_configure_port(MX27_AUDMUX_PPCR1_SSI_PINS_1,
			MXC_AUDMUX_V1_PCR_SYN |
			MXC_AUDMUX_V1_PCR_RXDSEL(MX27_AUDMUX_HPCR1_SSI0)
	);

	return ret;
}

static void __exit mx27vis_aic32x4_exit(void)
{
	platform_device_unregister(mx27vis_aic32x4_snd_device);
}

module_init(mx27vis_aic32x4_init);
module_exit(mx27vis_aic32x4_exit);

MODULE_AUTHOR("Javier Martin <javier.martin@vista-silicon.com>");
MODULE_DESCRIPTION("ALSA SoC AIC32X4 mx27 visstrim");
MODULE_LICENSE("GPL");
