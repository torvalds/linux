// SPDX-License-Identifier: GPL-2.0
/*
 * TDM driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/reset.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>
#include <linux/dma/starfive-dma.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#define TDM_PCMGBCR			0x00
	#define PCMGBCR_ENABLE		BIT(0)
	#define CLKPOL_BIT		5
	#define ELM_BIT			3
	#define SYNCM_BIT		2
	#define MS_BIT			1
#define TDM_PCMTXCR			0x04
	#define PCMTXCR_TXEN		BIT(0)
	#define IFL_BIT			11
	#define WL_BIT			8
	#define SSCALE_BIT		4
	#define SL_BIT			2
	#define LRJ_BIT			1
#define TDM_PCMRXCR			0x08
	#define PCMRXCR_RXEN		BIT(0)
#define TDM_PCMDIV			0x0c

enum TDM_MASTER_SLAVE_MODE {
	TDM_AS_MASTER = 0,
	TDM_AS_SLAVE,
};

enum TDM_CLKPOL {
	/* tx raising and rx falling */
	TDM_TX_RASING_RX_FALLING = 0,
	/* tx falling and rx raising */
	TDM_TX_FALLING_RX_RASING,
};

enum TDM_ELM {
	/* only work while SYNCM=0 */
	TDM_ELM_LATE = 0,
	TDM_ELM_EARLY,
};

enum TDM_SYNCM {
	/* short frame sync */
	TDM_SYNCM_SHORT = 0,
	/* long frame sync */
	TDM_SYNCM_LONG,
};

enum TDM_IFL {
	/* FIFO to send or received : half-1/2, Quarter-1/4 */
	TDM_FIFO_HALF = 0,
	TDM_FIFO_QUARTER,
};

enum TDM_WL {
	/* send or received word length */
	TDM_8BIT_WORD_LEN = 0,
	TDM_16BIT_WORD_LEN,
	TDM_20BIT_WORD_LEN,
	TDM_24BIT_WORD_LEN,
	TDM_32BIT_WORD_LEN,
};

enum TDM_SL {
	/* send or received slot length */
	TDM_8BIT_SLOT_LEN = 0,
	TDM_16BIT_SLOT_LEN,
	TDM_32BIT_SLOT_LEN,
};

enum TDM_LRJ {
	/* left-justify or right-justify */
	TDM_RIGHT_JUSTIFY = 0,
	TDM_LEFT_JUSTIFT,
};

struct tdm_chan_cfg {
	enum TDM_IFL ifl;
	enum TDM_WL  wl;
	unsigned char sscale;
	enum TDM_SL  sl;
	enum TDM_LRJ lrj;
	unsigned char enable;
};

struct jh7110_tdm_dev {
	void __iomem *tdm_base;
	struct device *dev;
	struct clk_bulk_data clks[6];
	struct reset_control *resets;

	enum TDM_CLKPOL clkpolity;
	enum TDM_ELM	elm;
	enum TDM_SYNCM	syncm;
	enum TDM_MASTER_SLAVE_MODE ms_mode;

	struct tdm_chan_cfg tx;
	struct tdm_chan_cfg rx;

	u16 syncdiv;
	u32 samplerate;
	u32 pcmclk;

	u32 saved_pcmgbcr;
	u32 saved_pcmtxcr;
	u32 saved_pcmrxcr;
	u32 saved_pcmdiv;
};

static inline u32 jh7110_tdm_readl(struct jh7110_tdm_dev *dev, u16 reg)
{
	return readl_relaxed(dev->tdm_base + reg);
}

static inline void jh7110_tdm_writel(struct jh7110_tdm_dev *dev, u16 reg, u32 val)
{
	writel_relaxed(val, dev->tdm_base + reg);
}

static void jh7110_tdm_clk_disable(struct jh7110_tdm_dev *priv)
{
	clk_bulk_disable_unprepare(ARRAY_SIZE(priv->clks), priv->clks);
}

static int jh7110_tdm_clk_enable(struct jh7110_tdm_dev *priv)
{
	int ret;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(priv->clks), priv->clks);
	if (ret) {
		dev_err(priv->dev, "Failed to enable tdm clocks\n");
		return ret;
	}

	ret = reset_control_deassert(priv->resets);
	if (ret) {
		dev_err(priv->dev, "%s: failed to deassert tdm resets\n", __func__);
		goto dis_tdm_clk;
	}

	/* select tdm_ext clock as the clock source for tdm */
	ret = clk_set_parent(priv->clks[5].clk, priv->clks[4].clk);
	if (ret) {
		dev_err(priv->dev, "Can't set extern clock source for clk_tdm\n");
		goto dis_tdm_clk;
	}

	return 0;

dis_tdm_clk:
	clk_bulk_disable_unprepare(ARRAY_SIZE(priv->clks), priv->clks);

	return ret;
}

#ifdef CONFIG_PM
static int jh7110_tdm_runtime_suspend(struct device *dev)
{
	struct jh7110_tdm_dev *priv = dev_get_drvdata(dev);

	jh7110_tdm_clk_disable(priv);
	return 0;
}

static int jh7110_tdm_runtime_resume(struct device *dev)
{
	struct jh7110_tdm_dev *priv = dev_get_drvdata(dev);

	return jh7110_tdm_clk_enable(priv);
}
#endif

/*
 * To stop dma first, we must implement this function, because it is
 * called before stopping the stream.
 */

static const struct snd_soc_component_driver jh7110_tdm_component = {
	.name		= "jh7110-tdm",
};

static int jh7110_tdm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct jh7110_tdm_dev *dev = snd_soc_dai_get_drvdata(dai);
	int chan_wl, chan_sl, chan_nr;
	unsigned int data_width;
	unsigned int mclk_rate;
	unsigned int dma_bus_width;
	int channels;
	int ret;
	struct snd_dmaengine_dai_dma_data *dma_data = NULL;

	dev_dbg(dev->dev, "tdm: jh7110_tdm_hw_params");

	channels = params_channels(params);
	data_width = params_width(params);

	dev->samplerate = params_rate(params);
	switch (dev->samplerate) {
	/*  There are some limitation when using 8k sample rate  */
	case 8000:
		mclk_rate = 12288000;
		if ((data_width == 16) || (channels == 1)) {
			pr_err("TDM: not support 16bit or 1-channel when using 8k sample rate\n");
			return -EINVAL;
		}
		break;
	case 11025:
		/* sysclk */
		mclk_rate = 11289600;
		break;
	case 16000:
		mclk_rate = 12288000;
		break;
	case 22050:
		mclk_rate = 11289600;
		break;
	case 32000:
		mclk_rate = 12288000;
		break;
	case 44100:
		mclk_rate = 11289600;
		break;
	case 48000:
		mclk_rate = 12288000;
		break;
	default:
		pr_err("TDM: not support sample rate:%d\n", dev->samplerate);
		return -EINVAL;
	}

	dev->pcmclk = channels * dev->samplerate * data_width;

	ret = clk_set_rate(dev->clks[0].clk, mclk_rate);
	if (ret) {
		dev_err(dev->dev, "Can't set clk_mclk: %d\n", ret);
		return ret;
	}

	ret = clk_set_rate(dev->clks[3].clk, dev->pcmclk);
	if (ret) {
		dev_err(dev->dev, "Can't set clk_tdm_internal: %d\n", ret);
		return ret;
	}

	ret = clk_set_parent(dev->clks[5].clk, dev->clks[4].clk);
	if (ret) {
		dev_err(dev->dev, "Can't set clock source for clk_tdm: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(dev->clks[1].clk);
	if (ret) {
		dev_err(dev->dev, "Failed to prepare enable clk_tdm_ahb\n");
		return ret;
	}

	ret = clk_prepare_enable(dev->clks[2].clk);
	if (ret) {
		dev_err(dev->dev, "Failed to prepare enable clk_tdm_apb\n");
		return ret;
	}

	return 0;
}

static const struct snd_soc_dai_ops jh7110_tdm_dai_ops = {
	.hw_params	= jh7110_tdm_hw_params
};

static int jh7110_tdm_dai_probe(struct snd_soc_dai *dai)
{
	struct jh7110_tdm_dev *dev = snd_soc_dai_get_drvdata(dai);

	// snd_soc_dai_init_dma_data(dai, &dev->play_dma_data, &dev->capture_dma_data);
	snd_soc_dai_set_drvdata(dai, dev);
	return 0;
}

#define JH7110_TDM_RATES SNDRV_PCM_RATE_8000_48000

#define JH7110_TDM_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver jh7110_tdm_dai = {
	.name = "ssp0",
	.id = 0,
	.playback = {
		.channels_min   = 1,
		.channels_max   = 8,
		.rates          = JH7110_TDM_RATES,
		.formats        = JH7110_TDM_FORMATS,
	},
	.capture = {
		.channels_min   = 1,
		.channels_max   = 8,
		.rates          = JH7110_TDM_RATES,
		.formats        = JH7110_TDM_FORMATS,
	},
	.ops = &jh7110_tdm_dai_ops,
	.probe = jh7110_tdm_dai_probe,
	.symmetric_rate = 1,
};

static int jh7110_tdm_clk_reset_init(struct platform_device *pdev, struct jh7110_tdm_dev *dev)
{
	int ret;

	dev->clks[0].id = "mclk_inner";
	dev->clks[1].id = "clk_tdm_ahb";
	dev->clks[2].id = "clk_tdm_apb";
	dev->clks[3].id = "clk_tdm_internal";
	dev->clks[4].id = "clk_tdm_ext";
	dev->clks[5].id = "clk_tdm";

	ret = devm_clk_bulk_get(&pdev->dev, ARRAY_SIZE(dev->clks), dev->clks);
	if (ret) {
		dev_err(&pdev->dev, "failed to get tdm clocks\n");
		goto exit;
	}

	dev->resets = devm_reset_control_array_get_exclusive(&pdev->dev);
	if (IS_ERR(dev->resets)) {
		ret = PTR_ERR(dev->resets);
		dev_err(&pdev->dev, "Failed to get tdm resets");
		goto exit;
	}

	ret = jh7110_tdm_clk_enable(dev);

exit:
	return ret;
}

static int jh7110_tdm_probe(struct platform_device *pdev)
{
	struct jh7110_tdm_dev *dev;
	struct resource *res;
	int ret;

	dev_dbg(&pdev->dev, "jh7110_tdm_probe\n");

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev->tdm_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->tdm_base))
		return PTR_ERR(dev->tdm_base);

	dev->dev = &pdev->dev;

	ret = jh7110_tdm_clk_reset_init(pdev, dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable audio-tdm clock\n");
		return ret;
	}

	dev_set_drvdata(&pdev->dev, dev);
	ret = devm_snd_soc_register_component(&pdev->dev, &jh7110_tdm_component,
					 &jh7110_tdm_dai, 1);
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to register dai\n");
		return ret;
	}

	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int jh7110_tdm_dev_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	return 0;
}
static const struct of_device_id jh7110_tdm_of_match[] = {
	{.compatible = "starfive,jh7110-sof-dai",},
	{}
};
MODULE_DEVICE_TABLE(of, jh7110_tdm_of_match);

static const struct dev_pm_ops jh7110_tdm_pm_ops = {
	SET_RUNTIME_PM_OPS(jh7110_tdm_runtime_suspend,
			   jh7110_tdm_runtime_resume, NULL)
};

static struct platform_driver jh7110_tdm_driver = {

	.driver = {
		.name = "jh7110-sof-tdm",
		.of_match_table = jh7110_tdm_of_match,
		.pm = &jh7110_tdm_pm_ops,
	},
	.probe = jh7110_tdm_probe,
	.remove = jh7110_tdm_dev_remove,
};
module_platform_driver(jh7110_tdm_driver);

MODULE_AUTHOR("Walker Chen <walker.chen@starfivetech.com>");
MODULE_DESCRIPTION("Starfive TDM Controller Driver");
MODULE_LICENSE("GPL v2");
