// SPDX-License-Identifier: GPL-2.0
/*
 * jh7110_pwmdac.c -- StarFive JH7110 PWM-DAC driver
 *
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 *
 * Authors: Jenny Zhang
 *	    Curry Zhang
 *	    Xingyu Wu <xingyu.wu@starfivetech.com>
 *	    Hal Feng <hal.feng@starfivetech.com>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#define JH7110_PWMDAC_WDATA				0x00
#define JH7110_PWMDAC_CTRL				0x04
	#define JH7110_PWMDAC_ENABLE			BIT(0)
	#define JH7110_PWMDAC_SHIFT			BIT(1)
	#define JH7110_PWMDAC_DUTY_CYCLE_SHIFT		2
	#define JH7110_PWMDAC_DUTY_CYCLE_MASK		GENMASK(3, 2)
	#define JH7110_PWMDAC_CNT_N_SHIFT		4
	#define JH7110_PWMDAC_CNT_N_MASK		GENMASK(12, 4)
	#define JH7110_PWMDAC_DATA_CHANGE		BIT(13)
	#define JH7110_PWMDAC_DATA_MODE			BIT(14)
	#define JH7110_PWMDAC_DATA_SHIFT_SHIFT		15
	#define JH7110_PWMDAC_DATA_SHIFT_MASK		GENMASK(17, 15)

enum JH7110_PWMDAC_SHIFT_VAL {
	PWMDAC_SHIFT_8 = 0,
	PWMDAC_SHIFT_10,
};

enum JH7110_PWMDAC_DUTY_CYCLE_VAL {
	PWMDAC_CYCLE_LEFT = 0,
	PWMDAC_CYCLE_RIGHT,
	PWMDAC_CYCLE_CENTER,
};

enum JH7110_PWMDAC_CNT_N_VAL {
	PWMDAC_SAMPLE_CNT_1 = 1,
	PWMDAC_SAMPLE_CNT_2,
	PWMDAC_SAMPLE_CNT_3,
	PWMDAC_SAMPLE_CNT_512 = 512, /* max */
};

enum JH7110_PWMDAC_DATA_CHANGE_VAL {
	NO_CHANGE = 0,
	CHANGE,
};

enum JH7110_PWMDAC_DATA_MODE_VAL {
	UNSIGNED_DATA = 0,
	INVERTER_DATA_MSB,
};

enum JH7110_PWMDAC_DATA_SHIFT_VAL {
	PWMDAC_DATA_LEFT_SHIFT_BIT_0 = 0,
	PWMDAC_DATA_LEFT_SHIFT_BIT_1,
	PWMDAC_DATA_LEFT_SHIFT_BIT_2,
	PWMDAC_DATA_LEFT_SHIFT_BIT_3,
	PWMDAC_DATA_LEFT_SHIFT_BIT_4,
	PWMDAC_DATA_LEFT_SHIFT_BIT_5,
	PWMDAC_DATA_LEFT_SHIFT_BIT_6,
	PWMDAC_DATA_LEFT_SHIFT_BIT_7,
};

struct jh7110_pwmdac_cfg {
	enum JH7110_PWMDAC_SHIFT_VAL shift;
	enum JH7110_PWMDAC_DUTY_CYCLE_VAL duty_cycle;
	u16 cnt_n;
	enum JH7110_PWMDAC_DATA_CHANGE_VAL data_change;
	enum JH7110_PWMDAC_DATA_MODE_VAL data_mode;
	enum JH7110_PWMDAC_DATA_SHIFT_VAL data_shift;
};

struct jh7110_pwmdac_dev {
	void __iomem *base;
	resource_size_t	mapbase;
	struct jh7110_pwmdac_cfg cfg;

	struct clk_bulk_data clks[2];
	struct reset_control *rst_apb;
	struct device *dev;
	struct snd_dmaengine_dai_dma_data play_dma_data;
	u32 saved_ctrl;
};

static inline void jh7110_pwmdac_write_reg(void __iomem *io_base, int reg, u32 val)
{
	writel(val, io_base + reg);
}

static inline u32 jh7110_pwmdac_read_reg(void __iomem *io_base, int reg)
{
	return readl(io_base + reg);
}

static void jh7110_pwmdac_set_enable(struct jh7110_pwmdac_dev *dev, bool enable)
{
	u32 value;

	value = jh7110_pwmdac_read_reg(dev->base, JH7110_PWMDAC_CTRL);
	if (enable)
		value |= JH7110_PWMDAC_ENABLE;
	else
		value &= ~JH7110_PWMDAC_ENABLE;

	jh7110_pwmdac_write_reg(dev->base, JH7110_PWMDAC_CTRL, value);
}

static void jh7110_pwmdac_set_shift(struct jh7110_pwmdac_dev *dev)
{
	u32 value;

	value = jh7110_pwmdac_read_reg(dev->base, JH7110_PWMDAC_CTRL);
	if (dev->cfg.shift == PWMDAC_SHIFT_8)
		value &= ~JH7110_PWMDAC_SHIFT;
	else if (dev->cfg.shift == PWMDAC_SHIFT_10)
		value |= JH7110_PWMDAC_SHIFT;

	jh7110_pwmdac_write_reg(dev->base, JH7110_PWMDAC_CTRL, value);
}

static void jh7110_pwmdac_set_duty_cycle(struct jh7110_pwmdac_dev *dev)
{
	u32 value;

	value = jh7110_pwmdac_read_reg(dev->base, JH7110_PWMDAC_CTRL);
	value &= ~JH7110_PWMDAC_DUTY_CYCLE_MASK;
	value |= (dev->cfg.duty_cycle & 0x3) << JH7110_PWMDAC_DUTY_CYCLE_SHIFT;

	jh7110_pwmdac_write_reg(dev->base, JH7110_PWMDAC_CTRL, value);
}

static void jh7110_pwmdac_set_cnt_n(struct jh7110_pwmdac_dev *dev)
{
	u32 value;

	value = jh7110_pwmdac_read_reg(dev->base, JH7110_PWMDAC_CTRL);
	value &= ~JH7110_PWMDAC_CNT_N_MASK;
	value |= ((dev->cfg.cnt_n - 1) & 0x1ff) << JH7110_PWMDAC_CNT_N_SHIFT;

	jh7110_pwmdac_write_reg(dev->base, JH7110_PWMDAC_CTRL, value);
}

static void jh7110_pwmdac_set_data_change(struct jh7110_pwmdac_dev *dev)
{
	u32 value;

	value = jh7110_pwmdac_read_reg(dev->base, JH7110_PWMDAC_CTRL);
	if (dev->cfg.data_change == NO_CHANGE)
		value &= ~JH7110_PWMDAC_DATA_CHANGE;
	else if (dev->cfg.data_change == CHANGE)
		value |= JH7110_PWMDAC_DATA_CHANGE;

	jh7110_pwmdac_write_reg(dev->base, JH7110_PWMDAC_CTRL, value);
}

static void jh7110_pwmdac_set_data_mode(struct jh7110_pwmdac_dev *dev)
{
	u32 value;

	value = jh7110_pwmdac_read_reg(dev->base, JH7110_PWMDAC_CTRL);
	if (dev->cfg.data_mode == UNSIGNED_DATA)
		value &= ~JH7110_PWMDAC_DATA_MODE;
	else if (dev->cfg.data_mode == INVERTER_DATA_MSB)
		value |= JH7110_PWMDAC_DATA_MODE;

	jh7110_pwmdac_write_reg(dev->base, JH7110_PWMDAC_CTRL, value);
}

static void jh7110_pwmdac_set_data_shift(struct jh7110_pwmdac_dev *dev)
{
	u32 value;

	value = jh7110_pwmdac_read_reg(dev->base, JH7110_PWMDAC_CTRL);
	value &= ~JH7110_PWMDAC_DATA_SHIFT_MASK;
	value |= (dev->cfg.data_shift & 0x7) << JH7110_PWMDAC_DATA_SHIFT_SHIFT;

	jh7110_pwmdac_write_reg(dev->base, JH7110_PWMDAC_CTRL, value);
}

static void jh7110_pwmdac_set(struct jh7110_pwmdac_dev *dev)
{
	jh7110_pwmdac_set_shift(dev);
	jh7110_pwmdac_set_duty_cycle(dev);
	jh7110_pwmdac_set_cnt_n(dev);
	jh7110_pwmdac_set_enable(dev, true);

	jh7110_pwmdac_set_data_change(dev);
	jh7110_pwmdac_set_data_mode(dev);
	jh7110_pwmdac_set_data_shift(dev);
}

static void jh7110_pwmdac_stop(struct jh7110_pwmdac_dev *dev)
{
	jh7110_pwmdac_set_enable(dev, false);
}

static int jh7110_pwmdac_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai_link *dai_link = rtd->dai_link;

	dai_link->trigger_stop = SND_SOC_TRIGGER_ORDER_LDC;

	return 0;
}

static int jh7110_pwmdac_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct jh7110_pwmdac_dev *dev = dev_get_drvdata(dai->dev);
	unsigned long core_clk_rate;
	int ret;

	switch (params_rate(params)) {
	case 8000:
		dev->cfg.cnt_n = PWMDAC_SAMPLE_CNT_3;
		core_clk_rate = 6144000;
		break;
	case 11025:
		dev->cfg.cnt_n = PWMDAC_SAMPLE_CNT_2;
		core_clk_rate = 5644800;
		break;
	case 16000:
		dev->cfg.cnt_n = PWMDAC_SAMPLE_CNT_3;
		core_clk_rate = 12288000;
		break;
	case 22050:
		dev->cfg.cnt_n = PWMDAC_SAMPLE_CNT_1;
		core_clk_rate = 5644800;
		break;
	case 32000:
		dev->cfg.cnt_n = PWMDAC_SAMPLE_CNT_1;
		core_clk_rate = 8192000;
		break;
	case 44100:
		dev->cfg.cnt_n = PWMDAC_SAMPLE_CNT_1;
		core_clk_rate = 11289600;
		break;
	case 48000:
		dev->cfg.cnt_n = PWMDAC_SAMPLE_CNT_1;
		core_clk_rate = 12288000;
		break;
	default:
		dev_err(dai->dev, "%d rate not supported\n",
			params_rate(params));
		return -EINVAL;
	}

	switch (params_channels(params)) {
	case 1:
		dev->play_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;
	case 2:
		dev->play_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	default:
		dev_err(dai->dev, "%d channels not supported\n",
			params_channels(params));
		return -EINVAL;
	}

	/*
	 * The clock rate always rounds down when using clk_set_rate()
	 * so increase the rate a bit
	 */
	core_clk_rate += 64;
	jh7110_pwmdac_set(dev);

	ret = clk_set_rate(dev->clks[1].clk, core_clk_rate);
	if (ret)
		return dev_err_probe(dai->dev, ret,
				     "failed to set rate %lu for core clock\n",
				     core_clk_rate);

	return 0;
}

static int jh7110_pwmdac_trigger(struct snd_pcm_substream *substream, int cmd,
				 struct snd_soc_dai *dai)
{
	struct jh7110_pwmdac_dev *dev = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		jh7110_pwmdac_set(dev);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		jh7110_pwmdac_stop(dev);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int jh7110_pwmdac_crg_enable(struct jh7110_pwmdac_dev *dev, bool enable)
{
	int ret;

	if (enable) {
		ret = clk_bulk_prepare_enable(ARRAY_SIZE(dev->clks), dev->clks);
		if (ret)
			return dev_err_probe(dev->dev, ret,
					     "failed to enable pwmdac clocks\n");

		ret = reset_control_deassert(dev->rst_apb);
		if (ret) {
			dev_err(dev->dev, "failed to deassert pwmdac apb reset\n");
			goto err_rst_apb;
		}
	} else {
		clk_bulk_disable_unprepare(ARRAY_SIZE(dev->clks), dev->clks);
	}

	return 0;

err_rst_apb:
	clk_bulk_disable_unprepare(ARRAY_SIZE(dev->clks), dev->clks);

	return ret;
}

static int jh7110_pwmdac_dai_probe(struct snd_soc_dai *dai)
{
	struct jh7110_pwmdac_dev *dev = dev_get_drvdata(dai->dev);

	snd_soc_dai_init_dma_data(dai, &dev->play_dma_data, NULL);
	snd_soc_dai_set_drvdata(dai, dev);

	return 0;
}

static const struct snd_soc_dai_ops jh7110_pwmdac_dai_ops = {
	.probe		= jh7110_pwmdac_dai_probe,
	.startup	= jh7110_pwmdac_startup,
	.hw_params	= jh7110_pwmdac_hw_params,
	.trigger	= jh7110_pwmdac_trigger,
};

static const struct snd_soc_component_driver jh7110_pwmdac_component = {
	.name		= "jh7110-pwmdac",
};

static struct snd_soc_dai_driver jh7110_pwmdac_dai = {
	.name		= "jh7110-pwmdac",
	.id		= 0,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &jh7110_pwmdac_dai_ops,
};

static int jh7110_pwmdac_runtime_suspend(struct device *dev)
{
	struct jh7110_pwmdac_dev *pwmdac = dev_get_drvdata(dev);

	return jh7110_pwmdac_crg_enable(pwmdac, false);
}

static int jh7110_pwmdac_runtime_resume(struct device *dev)
{
	struct jh7110_pwmdac_dev *pwmdac = dev_get_drvdata(dev);

	return jh7110_pwmdac_crg_enable(pwmdac, true);
}

static int jh7110_pwmdac_system_suspend(struct device *dev)
{
	struct jh7110_pwmdac_dev *pwmdac = dev_get_drvdata(dev);

	/* save the CTRL register value */
	pwmdac->saved_ctrl = jh7110_pwmdac_read_reg(pwmdac->base,
						    JH7110_PWMDAC_CTRL);
	return pm_runtime_force_suspend(dev);
}

static int jh7110_pwmdac_system_resume(struct device *dev)
{
	struct jh7110_pwmdac_dev *pwmdac = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret)
		return ret;

	/* restore the CTRL register value */
	jh7110_pwmdac_write_reg(pwmdac->base, JH7110_PWMDAC_CTRL,
				pwmdac->saved_ctrl);
	return 0;
}

static const struct dev_pm_ops jh7110_pwmdac_pm_ops = {
	RUNTIME_PM_OPS(jh7110_pwmdac_runtime_suspend,
		       jh7110_pwmdac_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(jh7110_pwmdac_system_suspend,
			    jh7110_pwmdac_system_resume)
};

static void jh7110_pwmdac_init_params(struct jh7110_pwmdac_dev *dev)
{
	dev->cfg.shift = PWMDAC_SHIFT_8;
	dev->cfg.duty_cycle = PWMDAC_CYCLE_CENTER;
	dev->cfg.cnt_n = PWMDAC_SAMPLE_CNT_1;
	dev->cfg.data_change = NO_CHANGE;
	dev->cfg.data_mode = INVERTER_DATA_MSB;
	dev->cfg.data_shift = PWMDAC_DATA_LEFT_SHIFT_BIT_0;

	dev->play_dma_data.addr = dev->mapbase + JH7110_PWMDAC_WDATA;
	dev->play_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dev->play_dma_data.fifo_size = 1;
	dev->play_dma_data.maxburst = 16;
}

static int jh7110_pwmdac_probe(struct platform_device *pdev)
{
	struct jh7110_pwmdac_dev *dev;
	struct resource *res;
	int ret;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(dev->base))
		return PTR_ERR(dev->base);

	dev->mapbase = res->start;

	dev->clks[0].id = "apb";
	dev->clks[1].id = "core";

	ret = devm_clk_bulk_get(&pdev->dev, ARRAY_SIZE(dev->clks), dev->clks);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to get pwmdac clocks\n");

	dev->rst_apb = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(dev->rst_apb))
		return dev_err_probe(&pdev->dev, PTR_ERR(dev->rst_apb),
				     "failed to get pwmdac apb reset\n");

	jh7110_pwmdac_init_params(dev);

	dev->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, dev);
	ret = devm_snd_soc_register_component(&pdev->dev,
					      &jh7110_pwmdac_component,
					      &jh7110_pwmdac_dai, 1);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to register dai\n");

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to register pcm\n");

	pm_runtime_enable(dev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = jh7110_pwmdac_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	return 0;

err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static void jh7110_pwmdac_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id jh7110_pwmdac_of_match[] = {
	{ .compatible = "starfive,jh7110-pwmdac" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jh7110_pwmdac_of_match);

static struct platform_driver jh7110_pwmdac_driver = {
	.driver		= {
		.name	= "jh7110-pwmdac",
		.of_match_table = jh7110_pwmdac_of_match,
		.pm = pm_ptr(&jh7110_pwmdac_pm_ops),
	},
	.probe		= jh7110_pwmdac_probe,
	.remove		= jh7110_pwmdac_remove,
};
module_platform_driver(jh7110_pwmdac_driver);

MODULE_AUTHOR("Jenny Zhang");
MODULE_AUTHOR("Curry Zhang");
MODULE_AUTHOR("Xingyu Wu <xingyu.wu@starfivetech.com>");
MODULE_AUTHOR("Hal Feng <hal.feng@starfivetech.com>");
MODULE_DESCRIPTION("StarFive JH7110 PWM-DAC driver");
MODULE_LICENSE("GPL");
