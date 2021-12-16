/**
  ******************************************************************************
  * @file  sf_pdm.c
  * @author  StarFive Technology
  * @version  V1.0
  * @date  05/27/2021
  * @brief
  ******************************************************************************
  * @copy
  *
  * THE PRESENT SOFTWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
  * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
  * TIME. AS A RESULT, STARFIVE SHALL NOT BE HELD LIABLE FOR ANY
  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
  * FROM THE CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * <h2><center>&copy; COPYRIGHT 20120 Shanghai StarFive Technology Co., Ltd. </center></h2>
  */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include "starfive_pdm.h"

#define AUDIOC_CLK 	(12288000)
#define PDM_MUL 	(128)

struct sf_pdm {
	struct regmap *pdm_map;
	struct regmap *clk_map;
	struct clk *clk;
};

static const DECLARE_TLV_DB_SCALE(volume_tlv, -9450, 150, 0);

static const struct snd_kcontrol_new sf_pdm_snd_controls[] = {
	SOC_SINGLE("DC compensation Control", PDM_DMIC_CTRL0, 30, 1, 0),
	SOC_SINGLE("High Pass Filter Control", PDM_DMIC_CTRL0, 28, 1, 0),
	SOC_SINGLE("Left Channel Volume Control", PDM_DMIC_CTRL0, 23, 1, 0),
	SOC_SINGLE("Right Channel Volume Control", PDM_DMIC_CTRL0, 22, 1, 0),
	SOC_SINGLE_TLV("Volume", PDM_DMIC_CTRL0, 16, 0x3F, 1, volume_tlv),
	SOC_SINGLE("Data MSB Shift", PDM_DMIC_CTRL0, 1, 7, 0),
	SOC_SINGLE("SCALE", PDM_DC_SCALE0, 0, 0x3F, 0),
	SOC_SINGLE("DC offset", PDM_DC_SCALE0, 8, 0xFFFFF, 0),
};

static int sf_pdm_set_mclk(struct regmap *map, 
	unsigned int clk, unsigned int weight)
{
	int mclk_div,bclk_div,lrclk_div;
	u32	pdm_div;

	/*
	audio source clk:12288000, mclk_div:4, mclk:3M
	support 8K/16K/32K/48K sample reate
	suapport 16/24/32 bit weight
	bit weight 32
	mclk bclk  lrclk
	3M   1.5M  48K
	3M   1M    32K
	3M   0.5M  16K
	3M   0.25M  8K
	
	bit weight 24,set lrclk_div as 32
	mclk bclk  lrclk
	3M   1.5M  48K
	3M   1M    32K
	3M   0.5M  16K
	3M   0.25M  8K
	
	bit weight 16
	mclk bclk   lrclk
	3M   0.75M  48K
	3M   0.5M   32K
	3M   0.25M  16K
	3M   0.125M 8K
	*/

	switch (clk) {
	case 8000:		
	case 16000:		
	case 32000:		
	case 48000:		
		break;
	default:
		printk(KERN_ERR "sample rate:%d\n", clk);
		return -EINVAL;
	}

	switch (weight) {
	case 16:		
	case 24:		
	case 32:		
		break;
	default:
		printk(KERN_ERR "bit weight:%d\n", weight);
		return -EINVAL;
	}

	if (24 == weight) {
		weight = 32;
	}

	mclk_div = 4;
	bclk_div = AUDIOC_CLK/mclk_div/(clk*weight);
	lrclk_div = weight;

	/* PDM MCLK = 128*LRCLK */
	pdm_div = AUDIOC_CLK/(PDM_MUL*clk);

	regmap_update_bits(map, AUDIO_CLK_ADC_MCLK, 0x0F, mclk_div);
	regmap_update_bits(map, AUDIO_CLK_I2SADC_BCLK, 0x1F, bclk_div);
	regmap_update_bits(map, AUDIO_CLK_ADC_LRCLK, 0x3F, lrclk_div);
	regmap_update_bits(map, AUDIO_CLK_PDM_CLK, 0x0F, pdm_div);

	return 0;
}

static void sf_pdm_enable(struct regmap *map)
{
	/* Enable PDM */
	regmap_update_bits(map, PDM_DMIC_CTRL0, 0x01<<PDM_DMIC_RVOL_OFFSET, 0);
	regmap_update_bits(map, PDM_DMIC_CTRL0, 0x01<<PDM_DMIC_LVOL_OFFSET, 0);
}

static void sf_pdm_disable(struct regmap *map)
{
	regmap_update_bits(map, PDM_DMIC_CTRL0, 
		0x01<<PDM_DMIC_RVOL_OFFSET, 0x01<<PDM_DMIC_RVOL_OFFSET);
	regmap_update_bits(map, PDM_DMIC_CTRL0, 
		0x01<<PDM_DMIC_LVOL_OFFSET, 0x01<<PDM_DMIC_LVOL_OFFSET);
}

static int sf_pdm_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct sf_pdm *priv = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		sf_pdm_enable(priv->pdm_map);
		return 0;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		sf_pdm_disable(priv->pdm_map);
		return 0;

	default:
		return -EINVAL;
	}
}

static int sf_pdm_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct sf_pdm *priv = snd_soc_dai_get_drvdata(dai);
	unsigned int rate = params_rate(params);
	unsigned int width;
	int ret;
	
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return 0;
	
	width = params_width(params);
	switch (width) {
	case 16:
	case 24:
	case 32:
		break;
	default:
		dev_err(dai->dev, "unsupported sample width\n");
		return -EINVAL;
	}

	ret = sf_pdm_set_mclk(priv->clk_map, rate, width);
	if (ret < 0) {
		dev_err(dai->dev, "unsupported sample rate\n");
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops sf_pdm_dai_ops = {
	.trigger	= sf_pdm_trigger,
	.hw_params	= sf_pdm_hw_params,
};

static int sf_pdm_dai_probe(struct snd_soc_dai *dai)
{
	struct sf_pdm *priv = snd_soc_dai_get_drvdata(dai);

	/* Reset */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0, 
		0x01<<PDM_DMIC_SW_RSTN_OFFSET, 0x00);
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0, 
		0x01<<PDM_DMIC_SW_RSTN_OFFSET, 0x01<<PDM_DMIC_SW_RSTN_OFFSET);

	/* Make sure the device is initially disabled */
	sf_pdm_disable(priv->pdm_map);

	/* MUTE */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0, 
		0x3F<<PDM_DMIC_VOL_OFFSET, 0x3F<<PDM_DMIC_VOL_OFFSET);

	/* UNMUTE */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0, 
		0x3F<<PDM_DMIC_VOL_OFFSET, 0);

	/* enable high pass filter */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0, 
		0x01<<PDM_DMIC_ENHPF_OFFSET, 0x01<<PDM_DMIC_ENHPF_OFFSET);

	/* i2s slaver mode */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0, 
		0x01<<PDM_DMIC_I2SMODE_OFFSET, 0x01<<PDM_DMIC_I2SMODE_OFFSET);

	/* disable fast mode */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0, 
		0x01<<PDM_DMIC_FASTMODE_OFFSET, 0);

	/* enable dc bypass mode */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0, 
		0x01<<PDM_DMIC_DCBPS_OFFSET, 0);

	/* dmic msb shift 0 */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0, 
		0x07<<PDM_DMIC_MSB_SHIFT_OFFSET, 0);

	/* scale:0 */
	regmap_update_bits(priv->pdm_map, PDM_DC_SCALE0, 0x3F, 0x08);
	
	/* DC offset:0 */
	regmap_update_bits(priv->pdm_map, PDM_DC_SCALE0, 
		0xFFFFF<<PDM_DMIC_DCOFF1_OFFSET, 0xC0005<<PDM_DMIC_DCOFF1_OFFSET);

	return 0;
}

static int sf_pdm_dai_remove(struct snd_soc_dai *dai)
{
	struct sf_pdm *priv = snd_soc_dai_get_drvdata(dai);
	
	/* MUTE */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0, 
		0x3F<<PDM_DMIC_VOL_OFFSET, 0x3F<<PDM_DMIC_VOL_OFFSET);

	return 0;
}

#define SF_PCM_RATE (SNDRV_PCM_RATE_8000|SNDRV_PCM_RATE_16000|\
					SNDRV_PCM_RATE_32000|SNDRV_PCM_RATE_48000)

static struct snd_soc_dai_driver sf_pdm_dai_drv = {
	.name = "PDM",
	.id = 0,
	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= 	SF_PCM_RATE,
		.formats	= 	SNDRV_PCM_FMTBIT_S16_LE|\
						SNDRV_PCM_FMTBIT_S24_LE|\
						SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops		= &sf_pdm_dai_ops,
	.probe		= sf_pdm_dai_probe,
	.remove		= sf_pdm_dai_remove,	
	.symmetric_rate = 1,
};
		
static int pdm_probe(struct snd_soc_component *component)
{
	struct sf_pdm *priv = snd_soc_component_get_drvdata(component);

	snd_soc_component_init_regmap(component, priv->pdm_map);
	snd_soc_add_component_controls(component, sf_pdm_snd_controls,
				     ARRAY_SIZE(sf_pdm_snd_controls));

	return 0;
}

static const struct snd_soc_component_driver sf_pdm_component_drv = {
	.name = "sf-pdm",
	.probe = pdm_probe,
};

static const struct regmap_config sf_pdm_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x20,
};

static const struct regmap_config sf_audio_clk_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x100,
};
 
static int sf_pdm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sf_pdm *priv;
	struct resource *res;
	void __iomem *regs;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);
	
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pdm");
	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	priv->pdm_map = devm_regmap_init_mmio(dev, regs, &sf_pdm_regmap_cfg);
	if (IS_ERR(priv->pdm_map)) {
		dev_err(dev, "failed to init regmap: %ld\n",
			PTR_ERR(priv->pdm_map));
		return PTR_ERR(priv->pdm_map);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "audio-clk");
	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	priv->clk_map = devm_regmap_init_mmio(dev, regs, &sf_audio_clk_regmap_cfg);
	if (IS_ERR(priv->clk_map)) {
		dev_err(dev, "failed to init regmap: %ld\n",
			PTR_ERR(priv->clk_map));
		return PTR_ERR(priv->clk_map);
	}

	return devm_snd_soc_register_component(dev, &sf_pdm_component_drv,
					       &sf_pdm_dai_drv, 1);
}

static int sf_pdm_dev_remove(struct platform_device *pdev)
{
	return 0;
}
static const struct of_device_id sf_pdm_of_match[] = {
	{.compatible = "starfive,sf-pdm",}, 
	{}
};
MODULE_DEVICE_TABLE(of, sf_pdm_of_match);

static struct platform_driver sf_pdm_driver = {

	.driver = {
		.name = "sf-pdm",
		.of_match_table = sf_pdm_of_match,
	},
	.probe = sf_pdm_probe,
	.remove = sf_pdm_dev_remove,
};
module_platform_driver(sf_pdm_driver);

MODULE_AUTHOR("michael.yan <michael.yan@starfivetech.com>");
MODULE_DESCRIPTION("starfive PDM Controller Driver");
MODULE_LICENSE("GPL v2");
