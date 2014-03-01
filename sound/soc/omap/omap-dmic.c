/*
 * omap-dmic.c  --  OMAP ASoC DMIC DAI driver
 *
 * Copyright (C) 2010 - 2011 Texas Instruments
 *
 * Author: David Lambert <dlambert@ti.com>
 *	   Misael Lopez Cruz <misael.lopez@ti.com>
 *	   Liam Girdwood <lrg@ti.com>
 *	   Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/of_device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "omap-dmic.h"

struct omap_dmic {
	struct device *dev;
	void __iomem *io_base;
	struct clk *fclk;
	int fclk_freq;
	int out_freq;
	int clk_div;
	int sysclk;
	int threshold;
	u32 ch_enabled;
	bool active;
	struct mutex mutex;

	struct snd_dmaengine_dai_dma_data dma_data;
};

static inline void omap_dmic_write(struct omap_dmic *dmic, u16 reg, u32 val)
{
	writel_relaxed(val, dmic->io_base + reg);
}

static inline int omap_dmic_read(struct omap_dmic *dmic, u16 reg)
{
	return readl_relaxed(dmic->io_base + reg);
}

static inline void omap_dmic_start(struct omap_dmic *dmic)
{
	u32 ctrl = omap_dmic_read(dmic, OMAP_DMIC_CTRL_REG);

	/* Configure DMA controller */
	omap_dmic_write(dmic, OMAP_DMIC_DMAENABLE_SET_REG,
			OMAP_DMIC_DMA_ENABLE);

	omap_dmic_write(dmic, OMAP_DMIC_CTRL_REG, ctrl | dmic->ch_enabled);
}

static inline void omap_dmic_stop(struct omap_dmic *dmic)
{
	u32 ctrl = omap_dmic_read(dmic, OMAP_DMIC_CTRL_REG);
	omap_dmic_write(dmic, OMAP_DMIC_CTRL_REG,
			ctrl & ~OMAP_DMIC_UP_ENABLE_MASK);

	/* Disable DMA request generation */
	omap_dmic_write(dmic, OMAP_DMIC_DMAENABLE_CLR_REG,
			OMAP_DMIC_DMA_ENABLE);

}

static inline int dmic_is_enabled(struct omap_dmic *dmic)
{
	return omap_dmic_read(dmic, OMAP_DMIC_CTRL_REG) &
						OMAP_DMIC_UP_ENABLE_MASK;
}

static int omap_dmic_dai_startup(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct omap_dmic *dmic = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	mutex_lock(&dmic->mutex);

	if (!dai->active)
		dmic->active = 1;
	else
		ret = -EBUSY;

	mutex_unlock(&dmic->mutex);

	snd_soc_dai_set_dma_data(dai, substream, &dmic->dma_data);
	return ret;
}

static void omap_dmic_dai_shutdown(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	struct omap_dmic *dmic = snd_soc_dai_get_drvdata(dai);

	mutex_lock(&dmic->mutex);

	if (!dai->active)
		dmic->active = 0;

	mutex_unlock(&dmic->mutex);
}

static int omap_dmic_select_divider(struct omap_dmic *dmic, int sample_rate)
{
	int divider = -EINVAL;

	/*
	 * 192KHz rate is only supported with 19.2MHz/3.84MHz clock
	 * configuration.
	 */
	if (sample_rate == 192000) {
		if (dmic->fclk_freq == 19200000 && dmic->out_freq == 3840000)
			divider = 0x6; /* Divider: 5 (192KHz sampling rate) */
		else
			dev_err(dmic->dev,
				"invalid clock configuration for 192KHz\n");

		return divider;
	}

	switch (dmic->out_freq) {
	case 1536000:
		if (dmic->fclk_freq != 24576000)
			goto div_err;
		divider = 0x4; /* Divider: 16 */
		break;
	case 2400000:
		switch (dmic->fclk_freq) {
		case 12000000:
			divider = 0x5; /* Divider: 5 */
			break;
		case 19200000:
			divider = 0x0; /* Divider: 8 */
			break;
		case 24000000:
			divider = 0x2; /* Divider: 10 */
			break;
		default:
			goto div_err;
		}
		break;
	case 3072000:
		if (dmic->fclk_freq != 24576000)
			goto div_err;
		divider = 0x3; /* Divider: 8 */
		break;
	case 3840000:
		if (dmic->fclk_freq != 19200000)
			goto div_err;
		divider = 0x1; /* Divider: 5 (96KHz sampling rate) */
		break;
	default:
		dev_err(dmic->dev, "invalid out frequency: %dHz\n",
			dmic->out_freq);
		break;
	}

	return divider;

div_err:
	dev_err(dmic->dev, "invalid out frequency %dHz for %dHz input\n",
		dmic->out_freq, dmic->fclk_freq);
	return -EINVAL;
}

static int omap_dmic_dai_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	struct omap_dmic *dmic = snd_soc_dai_get_drvdata(dai);
	struct snd_dmaengine_dai_dma_data *dma_data;
	int channels;

	dmic->clk_div = omap_dmic_select_divider(dmic, params_rate(params));
	if (dmic->clk_div < 0) {
		dev_err(dmic->dev, "no valid divider for %dHz from %dHz\n",
			dmic->out_freq, dmic->fclk_freq);
		return -EINVAL;
	}

	dmic->ch_enabled = 0;
	channels = params_channels(params);
	switch (channels) {
	case 6:
		dmic->ch_enabled |= OMAP_DMIC_UP3_ENABLE;
	case 4:
		dmic->ch_enabled |= OMAP_DMIC_UP2_ENABLE;
	case 2:
		dmic->ch_enabled |= OMAP_DMIC_UP1_ENABLE;
		break;
	default:
		dev_err(dmic->dev, "invalid number of legacy channels\n");
		return -EINVAL;
	}

	/* packet size is threshold * channels */
	dma_data = snd_soc_dai_get_dma_data(dai, substream);
	dma_data->maxburst = dmic->threshold * channels;

	return 0;
}

static int omap_dmic_dai_prepare(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct omap_dmic *dmic = snd_soc_dai_get_drvdata(dai);
	u32 ctrl;

	/* Configure uplink threshold */
	omap_dmic_write(dmic, OMAP_DMIC_FIFO_CTRL_REG, dmic->threshold);

	ctrl = omap_dmic_read(dmic, OMAP_DMIC_CTRL_REG);

	/* Set dmic out format */
	ctrl &= ~(OMAP_DMIC_FORMAT | OMAP_DMIC_POLAR_MASK);
	ctrl |= (OMAP_DMICOUTFORMAT_LJUST | OMAP_DMIC_POLAR1 |
		 OMAP_DMIC_POLAR2 | OMAP_DMIC_POLAR3);

	/* Configure dmic clock divider */
	ctrl &= ~OMAP_DMIC_CLK_DIV_MASK;
	ctrl |= OMAP_DMIC_CLK_DIV(dmic->clk_div);

	omap_dmic_write(dmic, OMAP_DMIC_CTRL_REG, ctrl);

	omap_dmic_write(dmic, OMAP_DMIC_CTRL_REG,
			ctrl | OMAP_DMICOUTFORMAT_LJUST | OMAP_DMIC_POLAR1 |
			OMAP_DMIC_POLAR2 | OMAP_DMIC_POLAR3);

	return 0;
}

static int omap_dmic_dai_trigger(struct snd_pcm_substream *substream,
				  int cmd, struct snd_soc_dai *dai)
{
	struct omap_dmic *dmic = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		omap_dmic_start(dmic);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		omap_dmic_stop(dmic);
		break;
	default:
		break;
	}

	return 0;
}

static int omap_dmic_select_fclk(struct omap_dmic *dmic, int clk_id,
				 unsigned int freq)
{
	struct clk *parent_clk;
	char *parent_clk_name;
	int ret = 0;

	switch (freq) {
	case 12000000:
	case 19200000:
	case 24000000:
	case 24576000:
		break;
	default:
		dev_err(dmic->dev, "invalid input frequency: %dHz\n", freq);
		dmic->fclk_freq = 0;
		return -EINVAL;
	}

	if (dmic->sysclk == clk_id) {
		dmic->fclk_freq = freq;
		return 0;
	}

	/* re-parent not allowed if a stream is ongoing */
	if (dmic->active && dmic_is_enabled(dmic)) {
		dev_err(dmic->dev, "can't re-parent when DMIC active\n");
		return -EBUSY;
	}

	switch (clk_id) {
	case OMAP_DMIC_SYSCLK_PAD_CLKS:
		parent_clk_name = "pad_clks_ck";
		break;
	case OMAP_DMIC_SYSCLK_SLIMBLUS_CLKS:
		parent_clk_name = "slimbus_clk";
		break;
	case OMAP_DMIC_SYSCLK_SYNC_MUX_CLKS:
		parent_clk_name = "dmic_sync_mux_ck";
		break;
	default:
		dev_err(dmic->dev, "fclk clk_id (%d) not supported\n", clk_id);
		return -EINVAL;
	}

	parent_clk = clk_get(dmic->dev, parent_clk_name);
	if (IS_ERR(parent_clk)) {
		dev_err(dmic->dev, "can't get %s\n", parent_clk_name);
		return -ENODEV;
	}

	mutex_lock(&dmic->mutex);
	if (dmic->active) {
		/* disable clock while reparenting */
		pm_runtime_put_sync(dmic->dev);
		ret = clk_set_parent(dmic->fclk, parent_clk);
		pm_runtime_get_sync(dmic->dev);
	} else {
		ret = clk_set_parent(dmic->fclk, parent_clk);
	}
	mutex_unlock(&dmic->mutex);

	if (ret < 0) {
		dev_err(dmic->dev, "re-parent failed\n");
		goto err_busy;
	}

	dmic->sysclk = clk_id;
	dmic->fclk_freq = freq;

err_busy:
	clk_put(parent_clk);

	return ret;
}

static int omap_dmic_select_outclk(struct omap_dmic *dmic, int clk_id,
				    unsigned int freq)
{
	int ret = 0;

	if (clk_id != OMAP_DMIC_ABE_DMIC_CLK) {
		dev_err(dmic->dev, "output clk_id (%d) not supported\n",
			clk_id);
		return -EINVAL;
	}

	switch (freq) {
	case 1536000:
	case 2400000:
	case 3072000:
	case 3840000:
		dmic->out_freq = freq;
		break;
	default:
		dev_err(dmic->dev, "invalid out frequency: %dHz\n", freq);
		dmic->out_freq = 0;
		ret = -EINVAL;
	}

	return ret;
}

static int omap_dmic_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				    unsigned int freq, int dir)
{
	struct omap_dmic *dmic = snd_soc_dai_get_drvdata(dai);

	if (dir == SND_SOC_CLOCK_IN)
		return omap_dmic_select_fclk(dmic, clk_id, freq);
	else if (dir == SND_SOC_CLOCK_OUT)
		return omap_dmic_select_outclk(dmic, clk_id, freq);

	dev_err(dmic->dev, "invalid clock direction (%d)\n", dir);
	return -EINVAL;
}

static const struct snd_soc_dai_ops omap_dmic_dai_ops = {
	.startup	= omap_dmic_dai_startup,
	.shutdown	= omap_dmic_dai_shutdown,
	.hw_params	= omap_dmic_dai_hw_params,
	.prepare	= omap_dmic_dai_prepare,
	.trigger	= omap_dmic_dai_trigger,
	.set_sysclk	= omap_dmic_set_dai_sysclk,
};

static int omap_dmic_probe(struct snd_soc_dai *dai)
{
	struct omap_dmic *dmic = snd_soc_dai_get_drvdata(dai);

	pm_runtime_enable(dmic->dev);

	/* Disable lines while request is ongoing */
	pm_runtime_get_sync(dmic->dev);
	omap_dmic_write(dmic, OMAP_DMIC_CTRL_REG, 0x00);
	pm_runtime_put_sync(dmic->dev);

	/* Configure DMIC threshold value */
	dmic->threshold = OMAP_DMIC_THRES_MAX - 3;
	return 0;
}

static int omap_dmic_remove(struct snd_soc_dai *dai)
{
	struct omap_dmic *dmic = snd_soc_dai_get_drvdata(dai);

	pm_runtime_disable(dmic->dev);

	return 0;
}

static struct snd_soc_dai_driver omap_dmic_dai = {
	.name = "omap-dmic",
	.probe = omap_dmic_probe,
	.remove = omap_dmic_remove,
	.capture = {
		.channels_min = 2,
		.channels_max = 6,
		.rates = SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_S32_LE,
		.sig_bits = 24,
	},
	.ops = &omap_dmic_dai_ops,
};

static const struct snd_soc_component_driver omap_dmic_component = {
	.name		= "omap-dmic",
};

static int asoc_dmic_probe(struct platform_device *pdev)
{
	struct omap_dmic *dmic;
	struct resource *res;
	int ret;

	dmic = devm_kzalloc(&pdev->dev, sizeof(struct omap_dmic), GFP_KERNEL);
	if (!dmic)
		return -ENOMEM;

	platform_set_drvdata(pdev, dmic);
	dmic->dev = &pdev->dev;
	dmic->sysclk = OMAP_DMIC_SYSCLK_SYNC_MUX_CLKS;

	mutex_init(&dmic->mutex);

	dmic->fclk = clk_get(dmic->dev, "fck");
	if (IS_ERR(dmic->fclk)) {
		dev_err(dmic->dev, "cant get fck\n");
		return -ENODEV;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dma");
	if (!res) {
		dev_err(dmic->dev, "invalid dma memory resource\n");
		ret = -ENODEV;
		goto err_put_clk;
	}
	dmic->dma_data.addr = res->start + OMAP_DMIC_DATA_REG;

	dmic->dma_data.filter_data = "up_link";

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mpu");
	dmic->io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dmic->io_base)) {
		ret = PTR_ERR(dmic->io_base);
		goto err_put_clk;
	}


	ret = snd_soc_register_component(&pdev->dev, &omap_dmic_component,
					 &omap_dmic_dai, 1);
	if (ret)
		goto err_put_clk;

	return 0;

err_put_clk:
	clk_put(dmic->fclk);
	return ret;
}

static int asoc_dmic_remove(struct platform_device *pdev)
{
	struct omap_dmic *dmic = platform_get_drvdata(pdev);

	snd_soc_unregister_component(&pdev->dev);
	clk_put(dmic->fclk);

	return 0;
}

static const struct of_device_id omap_dmic_of_match[] = {
	{ .compatible = "ti,omap4-dmic", },
	{ }
};
MODULE_DEVICE_TABLE(of, omap_dmic_of_match);

static struct platform_driver asoc_dmic_driver = {
	.driver = {
		.name = "omap-dmic",
		.owner = THIS_MODULE,
		.of_match_table = omap_dmic_of_match,
	},
	.probe = asoc_dmic_probe,
	.remove = asoc_dmic_remove,
};

module_platform_driver(asoc_dmic_driver);

MODULE_ALIAS("platform:omap-dmic");
MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@ti.com>");
MODULE_DESCRIPTION("OMAP DMIC ASoC Interface");
MODULE_LICENSE("GPL");
