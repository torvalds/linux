/**
  ******************************************************************************
  * @file  sf_spdif.c
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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/dmaengine_pcm.h>
#include "starfive_spdif.h"

static irqreturn_t spdif_irq_handler(int irq, void *dev_id)
{
	struct sf_spdif_dev *dev = dev_id;
	bool irq_valid = false;
	unsigned int intr;
	unsigned int stat;

	regmap_read(dev->regmap, SPDIF_INT_REG, &intr);
	regmap_read(dev->regmap, SPDIF_STAT_REG, &stat);
	regmap_update_bits(dev->regmap, SPDIF_CTRL,
		SPDIF_MASK_ENABLE, 0);
	regmap_update_bits(dev->regmap, SPDIF_INT_REG, 
		SPDIF_INT_REG_BIT, 0);

	if ((stat & SPDIF_EMPTY_FLAG) || (stat & SPDIF_AEMPTY_FLAG)) {
		sf_spdif_pcm_push_tx(dev);
		irq_valid = true;
	} 
	
	if ((stat & SPDIF_FULL_FLAG) || (stat & SPDIF_AFULL_FLAG)) {
		sf_spdif_pcm_pop_rx(dev);
		irq_valid = true;
	} 

	if (stat & SPDIF_PARITY_FLAG) {
		irq_valid = true;
	} 
	
	if (stat & SPDIF_UNDERR_FLAG) {
		irq_valid = true;
	}
	
	if (stat & SPDIF_OVRERR_FLAG) {
		irq_valid = true;
	}
	
	if (stat & SPDIF_SYNCERR_FLAG) {
		irq_valid = true;
	}
	
	if (stat & SPDIF_LOCK_FLAG) {
		irq_valid = true;
	}
	
	if (stat & SPDIF_BEGIN_FLAG) {
		irq_valid = true;
	}

	if (stat & SPDIF_RIGHT_LEFT) {
		irq_valid = true;
	}

	regmap_update_bits(dev->regmap, SPDIF_CTRL,
		SPDIF_MASK_ENABLE, SPDIF_MASK_ENABLE);

	if (irq_valid)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static int sf_spdif_trigger(struct snd_pcm_substream *substream, int cmd,
	struct snd_soc_dai *dai)
{
	struct sf_spdif_dev *spdif = snd_soc_dai_get_drvdata(dai);
	bool tx;

	tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	if (tx) {
		/* tx mode */
		regmap_update_bits(spdif->regmap, SPDIF_CTRL,
			SPDIF_TR_MODE, SPDIF_TR_MODE);
	
		regmap_update_bits(spdif->regmap, SPDIF_CTRL,
			SPDIF_MASK_FIFO, SPDIF_EMPTY_MASK | SPDIF_AEMPTY_MASK);
	} else {
		/* rx mode */
		regmap_update_bits(spdif->regmap, SPDIF_CTRL,
			SPDIF_TR_MODE, 0);
		
		regmap_update_bits(spdif->regmap, SPDIF_CTRL,
			SPDIF_MASK_FIFO, SPDIF_FULL_MASK | SPDIF_AFULL_MASK);
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		/* clock recovery form the SPDIF data stream  0:clk_enable */
		regmap_update_bits(spdif->regmap, SPDIF_CTRL,
			SPDIF_CLK_ENABLE, 0);
	
		regmap_update_bits(spdif->regmap, SPDIF_CTRL,
			SPDIF_ENABLE, SPDIF_ENABLE);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		/* clock recovery form the SPDIF data stream  1:power save mode */
		regmap_update_bits(spdif->regmap, SPDIF_CTRL,
			SPDIF_CLK_ENABLE, SPDIF_CLK_ENABLE);
	
		regmap_update_bits(spdif->regmap, SPDIF_CTRL,
			SPDIF_ENABLE, 0);
		break;
	default:
		printk(KERN_ERR "%s L.%d cmd:%d\n", __func__, __LINE__, cmd);
		return -EINVAL;
	}

	return 0;
}

static int sf_spdif_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct sf_spdif_dev *spdif = snd_soc_dai_get_drvdata(dai);
	unsigned int channels;
	unsigned int rate;
	unsigned int format;
	unsigned int tsamplerate;

	channels = params_channels(params);
	rate = params_rate(params);
	format = params_format(params);
	
	switch (channels) {
	case 2:
		break;
	default:
		dev_err(dai->dev, "invalid channels number\n");
		return -EINVAL;
	}

	switch (format) {
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		break;
	default:
		dev_err(spdif->dev, "invalid format\n");
		return -EINVAL;
	}

	switch (rate) {
	case 8000:
	case 11025:
	case 16000:
	case 22050:
		break;
	default:
		printk(KERN_ERR "channel:%d sample rate:%d\n", channels, rate);
		return -EINVAL;
	}

	/* 4096000/128=32000 */
	tsamplerate = (32000 + rate/2)/rate - 1;
	
	if (rate < 3) {
		return -EINVAL;
	}
	
	/* transmission sample rate */
	regmap_update_bits(spdif->regmap, SPDIF_CTRL, 0xFF, tsamplerate);

	return 0;
}

static int sf_spdif_clks_get(struct platform_device *pdev,
				struct sf_spdif_dev *spdif)
{

	static struct clk_bulk_data clks[] = {
			{ .id = "spdif-apb" },		//clock-names in dts file
			{ .id = "spdif-core" },
			{ .id = "audioclk" },
	};
	int ret = devm_clk_bulk_get(&pdev->dev, ARRAY_SIZE(clks), clks);
	spdif->spdif_apb = clks[0].clk;
	spdif->spdif_core = clks[1].clk;
	spdif->audioclk = clks[2].clk;
	return ret;
}

static int sf_spdif_resets_get(struct platform_device *pdev,
				struct sf_spdif_dev *spdif)
{
	struct reset_control_bulk_data resets[] = {
			{ .id = "rst_apb" },
	};
	int ret = devm_reset_control_bulk_get_exclusive(&pdev->dev, ARRAY_SIZE(resets), resets);
	if (ret)
		return ret;

	spdif->rst_apb = resets[0].rstc;

	return 0;
}

static int sf_spdif_clk_init(struct platform_device *pdev,
				struct sf_spdif_dev *spdif)
{
	int ret = 0;

	ret = clk_prepare_enable(spdif->spdif_apb);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable spdif_apb\n");
			goto err_clk_spdif;
	}

	ret = clk_prepare_enable(spdif->spdif_core);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable spdif_core\n");
		goto err_clk_spdif;
	}

	ret = clk_prepare_enable(spdif->audioclk);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable audioclk\n");
		goto err_clk_spdif;
	}

	ret = reset_control_deassert(spdif->rst_apb);
	if (ret) {
		printk(KERN_INFO "failed to deassert apb\n");
		goto err_clk_spdif;
	}

	printk(KERN_INFO "Initialize spdif...success\n");

err_clk_spdif:
		return ret;
}

static int sf_spdif_dai_probe(struct snd_soc_dai *dai)
{
	struct sf_spdif_dev *spdif = snd_soc_dai_get_drvdata(dai);

	#if 0
	spdif->play_dma_data.addr = (dma_addr_t)spdif->spdif_base + SPDIF_FIFO_ADDR;
	spdif->play_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	spdif->play_dma_data.fifo_size = 16;
	spdif->play_dma_data.maxburst = 16;
	spdif->capture_dma_data.addr = (dma_addr_t)spdif->spdif_base + SPDIF_FIFO_ADDR;
	spdif->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	spdif->capture_dma_data.fifo_size = 16;
	spdif->capture_dma_data.maxburst = 16;
	snd_soc_dai_init_dma_data(dai, &spdif->play_dma_data, &spdif->capture_dma_data);
	snd_soc_dai_set_drvdata(dai, spdif);
	#endif

	/* reset */
	regmap_update_bits(spdif->regmap, SPDIF_CTRL,
		SPDIF_ENABLE | SPDIF_SFR_ENABLE | SPDIF_FIFO_ENABLE, 0);

	/* clear irq */
	regmap_update_bits(spdif->regmap, SPDIF_INT_REG,
		SPDIF_INT_REG_BIT, 0);	

	/* power save mode */
	regmap_update_bits(spdif->regmap, SPDIF_CTRL,
		SPDIF_CLK_ENABLE, SPDIF_CLK_ENABLE);

	/* power save mode */
	regmap_update_bits(spdif->regmap, SPDIF_CTRL,
		SPDIF_CLK_ENABLE, SPDIF_CLK_ENABLE);

	regmap_update_bits(spdif->regmap, SPDIF_CTRL,
		SPDIF_PARITCHECK|SPDIF_VALIDITYCHECK|SPDIF_DUPLICATE,
		SPDIF_PARITCHECK|SPDIF_VALIDITYCHECK|SPDIF_DUPLICATE);

	regmap_update_bits(spdif->regmap, SPDIF_CTRL,
		SPDIF_SETPREAMBB, SPDIF_SETPREAMBB);

	regmap_update_bits(spdif->regmap, SPDIF_INT_REG,
		0x1FFF<<SPDIF_PREAMBLEDEL, 0x3<<SPDIF_PREAMBLEDEL);

	regmap_update_bits(spdif->regmap, SPDIF_FIFO_CTRL,
		0xFFFFFFFF, 0x20|(0x20<<SPDIF_AFULL_THRESHOLD));

	regmap_update_bits(spdif->regmap, SPDIF_CTRL,
		SPDIF_PARITYGEN, SPDIF_PARITYGEN);

	regmap_update_bits(spdif->regmap, SPDIF_CTRL,
		SPDIF_MASK_ENABLE, SPDIF_MASK_ENABLE);

	/* APB access to FIFO enable, disable if use DMA/FIFO */
	regmap_update_bits(spdif->regmap, SPDIF_CTRL,
		SPDIF_USE_FIFO_IF, 0);

	/* two channel */
	regmap_update_bits(spdif->regmap, SPDIF_CTRL,
		SPDIF_CHANNEL_MODE, 0);

	return 0;
}

static const struct snd_soc_dai_ops sf_spdif_dai_ops = {
	.trigger = sf_spdif_trigger,
	.hw_params = sf_spdif_hw_params,
};

#define SF_PCM_RATE_44100_192000  (SNDRV_PCM_RATE_44100 | \
									SNDRV_PCM_RATE_48000 | \
									SNDRV_PCM_RATE_96000 | \
									SNDRV_PCM_RATE_192000)
									
#define SF_PCM_RATE_8000_22050  (SNDRV_PCM_RATE_8000 | \
									SNDRV_PCM_RATE_11025 | \
									SNDRV_PCM_RATE_16000 | \
									SNDRV_PCM_RATE_22050)									

static struct snd_soc_dai_driver sf_spdif_dai = {
	.name = "spdif",
	.id = 0,
	.probe = sf_spdif_dai_probe,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SF_PCM_RATE_8000_22050,
		.formats = SNDRV_PCM_FMTBIT_S16_LE \
					|SNDRV_PCM_FMTBIT_S24_LE \
					|SNDRV_PCM_FMTBIT_S32_LE,
	},
	.capture =  {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SF_PCM_RATE_8000_22050,
		.formats = SNDRV_PCM_FMTBIT_S16_LE \
					|SNDRV_PCM_FMTBIT_S24_LE \
					|SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &sf_spdif_dai_ops,
	.symmetric_rate = 1,
};

static const struct snd_soc_component_driver sf_spdif_component = {
	.name = "sf-spdif",
};

static const struct regmap_config sf_spdif_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x200,
};

static int sf_spdif_probe(struct platform_device *pdev)
{
	struct sf_spdif_dev *spdif;
	struct resource *res;
	void __iomem *base;
	int ret;
	int irq;

	spdif = devm_kzalloc(&pdev->dev, sizeof(*spdif), GFP_KERNEL);
	if (!spdif)
		return -ENOMEM;

	platform_set_drvdata(pdev, spdif);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	spdif->spdif_base = base;
	spdif->regmap = devm_regmap_init_mmio(&pdev->dev, spdif->spdif_base,
					    &sf_spdif_regmap_config);
	if (IS_ERR(spdif->regmap))
		return PTR_ERR(spdif->regmap);

	ret = sf_spdif_clks_get(pdev, spdif);
	if (ret) {
			dev_err(&pdev->dev, "failed to get audio clock\n");
			return ret;
	}

	ret = sf_spdif_resets_get(pdev, spdif);
	if (ret) {
			dev_err(&pdev->dev, "failed to get audio reset controls\n");
			return ret;
	}

	ret = sf_spdif_clk_init(pdev, spdif);
	if (ret) {
			dev_err(&pdev->dev, "failed to enable audio clock\n");
			return ret;
	}
	
	spdif->dev = &pdev->dev;
	spdif->fifo_th = 16;
	
	irq = platform_get_irq(pdev, 0);
	if (irq >= 0) {
		ret = devm_request_irq(&pdev->dev, irq, spdif_irq_handler, 0,
				pdev->name, spdif);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to request irq\n");
			return ret;
		}
	}

	ret = devm_snd_soc_register_component(&pdev->dev, &sf_spdif_component,
					 &sf_spdif_dai, 1);
	if (ret)
		goto err_clk_disable;
	
	if (irq >= 0) {
		ret = sf_spdif_pcm_register(pdev);
		spdif->use_pio = true;
	} else {
		ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL,
					0);
		spdif->use_pio = false;
	}

	if (ret)
		goto err_clk_disable;

	return 0;

err_clk_disable:
	return ret;
}

static const struct of_device_id sf_spdif_of_match[] = {
	{ .compatible = "starfive,sf-spdif", },
	{},
};
MODULE_DEVICE_TABLE(of, sf_spdif_of_match);

static struct platform_driver sf_spdif_driver = {
	.driver = {
		.name = "sf-spdif",
		.of_match_table = sf_spdif_of_match,
	},
	.probe = sf_spdif_probe,
};
module_platform_driver(sf_spdif_driver);

MODULE_AUTHOR("curry.zhang <michael.yan@starfive.com>");
MODULE_DESCRIPTION("starfive SPDIF driver");
MODULE_LICENSE("GPL v2");
