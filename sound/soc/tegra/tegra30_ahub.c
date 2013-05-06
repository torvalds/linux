/*
 * tegra30_ahub.c - Tegra30 AHUB driver
 *
 * Copyright (c) 2011,2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/clk/tegra.h>
#include <sound/soc.h>
#include "tegra30_ahub.h"

#define DRV_NAME "tegra30-ahub"

static struct tegra30_ahub *ahub;

static inline void tegra30_apbif_write(u32 reg, u32 val)
{
	regmap_write(ahub->regmap_apbif, reg, val);
}

static inline u32 tegra30_apbif_read(u32 reg)
{
	u32 val;
	regmap_read(ahub->regmap_apbif, reg, &val);
	return val;
}

static inline void tegra30_audio_write(u32 reg, u32 val)
{
	regmap_write(ahub->regmap_ahub, reg, val);
}

static int tegra30_ahub_runtime_suspend(struct device *dev)
{
	regcache_cache_only(ahub->regmap_apbif, true);
	regcache_cache_only(ahub->regmap_ahub, true);

	clk_disable_unprepare(ahub->clk_apbif);
	clk_disable_unprepare(ahub->clk_d_audio);

	return 0;
}

/*
 * clk_apbif isn't required for an I2S<->I2S configuration where no PCM data
 * is read from or sent to memory. However, that's not something the rest of
 * the driver supports right now, so we'll just treat the two clocks as one
 * for now.
 *
 * These functions should not be a plain ref-count. Instead, each active stream
 * contributes some requirement to the minimum clock rate, so starting or
 * stopping streams should dynamically adjust the clock as required.  However,
 * this is not yet implemented.
 */
static int tegra30_ahub_runtime_resume(struct device *dev)
{
	int ret;

	ret = clk_prepare_enable(ahub->clk_d_audio);
	if (ret) {
		dev_err(dev, "clk_enable d_audio failed: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(ahub->clk_apbif);
	if (ret) {
		dev_err(dev, "clk_enable apbif failed: %d\n", ret);
		clk_disable(ahub->clk_d_audio);
		return ret;
	}

	regcache_cache_only(ahub->regmap_apbif, false);
	regcache_cache_only(ahub->regmap_ahub, false);

	return 0;
}

int tegra30_ahub_allocate_rx_fifo(enum tegra30_ahub_rxcif *rxcif,
				  dma_addr_t *fiforeg,
				  unsigned int *reqsel)
{
	int channel;
	u32 reg, val;

	channel = find_first_zero_bit(ahub->rx_usage,
				      TEGRA30_AHUB_CHANNEL_CTRL_COUNT);
	if (channel >= TEGRA30_AHUB_CHANNEL_CTRL_COUNT)
		return -EBUSY;

	__set_bit(channel, ahub->rx_usage);

	*rxcif = TEGRA30_AHUB_RXCIF_APBIF_RX0 + channel;
	*fiforeg = ahub->apbif_addr + TEGRA30_AHUB_CHANNEL_RXFIFO +
		   (channel * TEGRA30_AHUB_CHANNEL_RXFIFO_STRIDE);
	*reqsel = ahub->dma_sel + channel;

	reg = TEGRA30_AHUB_CHANNEL_CTRL +
	      (channel * TEGRA30_AHUB_CHANNEL_CTRL_STRIDE);
	val = tegra30_apbif_read(reg);
	val &= ~(TEGRA30_AHUB_CHANNEL_CTRL_RX_THRESHOLD_MASK |
		 TEGRA30_AHUB_CHANNEL_CTRL_RX_PACK_MASK);
	val |= (7 << TEGRA30_AHUB_CHANNEL_CTRL_RX_THRESHOLD_SHIFT) |
	       TEGRA30_AHUB_CHANNEL_CTRL_RX_PACK_EN |
	       TEGRA30_AHUB_CHANNEL_CTRL_RX_PACK_16;
	tegra30_apbif_write(reg, val);

	reg = TEGRA30_AHUB_CIF_RX_CTRL +
	      (channel * TEGRA30_AHUB_CIF_RX_CTRL_STRIDE);
	val = (0 << TEGRA30_AUDIOCIF_CTRL_FIFO_THRESHOLD_SHIFT) |
	      (1 << TEGRA30_AUDIOCIF_CTRL_AUDIO_CHANNELS_SHIFT) |
	      (1 << TEGRA30_AUDIOCIF_CTRL_CLIENT_CHANNELS_SHIFT) |
	      TEGRA30_AUDIOCIF_CTRL_AUDIO_BITS_16 |
	      TEGRA30_AUDIOCIF_CTRL_CLIENT_BITS_16 |
	      TEGRA30_AUDIOCIF_CTRL_DIRECTION_RX;
	tegra30_apbif_write(reg, val);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_allocate_rx_fifo);

int tegra30_ahub_enable_rx_fifo(enum tegra30_ahub_rxcif rxcif)
{
	int channel = rxcif - TEGRA30_AHUB_RXCIF_APBIF_RX0;
	int reg, val;

	reg = TEGRA30_AHUB_CHANNEL_CTRL +
	      (channel * TEGRA30_AHUB_CHANNEL_CTRL_STRIDE);
	val = tegra30_apbif_read(reg);
	val |= TEGRA30_AHUB_CHANNEL_CTRL_RX_EN;
	tegra30_apbif_write(reg, val);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_enable_rx_fifo);

int tegra30_ahub_disable_rx_fifo(enum tegra30_ahub_rxcif rxcif)
{
	int channel = rxcif - TEGRA30_AHUB_RXCIF_APBIF_RX0;
	int reg, val;

	reg = TEGRA30_AHUB_CHANNEL_CTRL +
	      (channel * TEGRA30_AHUB_CHANNEL_CTRL_STRIDE);
	val = tegra30_apbif_read(reg);
	val &= ~TEGRA30_AHUB_CHANNEL_CTRL_RX_EN;
	tegra30_apbif_write(reg, val);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_disable_rx_fifo);

int tegra30_ahub_free_rx_fifo(enum tegra30_ahub_rxcif rxcif)
{
	int channel = rxcif - TEGRA30_AHUB_RXCIF_APBIF_RX0;

	__clear_bit(channel, ahub->rx_usage);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_free_rx_fifo);

int tegra30_ahub_allocate_tx_fifo(enum tegra30_ahub_txcif *txcif,
				  dma_addr_t *fiforeg,
				  unsigned int *reqsel)
{
	int channel;
	u32 reg, val;

	channel = find_first_zero_bit(ahub->tx_usage,
				      TEGRA30_AHUB_CHANNEL_CTRL_COUNT);
	if (channel >= TEGRA30_AHUB_CHANNEL_CTRL_COUNT)
		return -EBUSY;

	__set_bit(channel, ahub->tx_usage);

	*txcif = TEGRA30_AHUB_TXCIF_APBIF_TX0 + channel;
	*fiforeg = ahub->apbif_addr + TEGRA30_AHUB_CHANNEL_TXFIFO +
		   (channel * TEGRA30_AHUB_CHANNEL_TXFIFO_STRIDE);
	*reqsel = ahub->dma_sel + channel;

	reg = TEGRA30_AHUB_CHANNEL_CTRL +
	      (channel * TEGRA30_AHUB_CHANNEL_CTRL_STRIDE);
	val = tegra30_apbif_read(reg);
	val &= ~(TEGRA30_AHUB_CHANNEL_CTRL_TX_THRESHOLD_MASK |
		 TEGRA30_AHUB_CHANNEL_CTRL_TX_PACK_MASK);
	val |= (7 << TEGRA30_AHUB_CHANNEL_CTRL_TX_THRESHOLD_SHIFT) |
	       TEGRA30_AHUB_CHANNEL_CTRL_TX_PACK_EN |
	       TEGRA30_AHUB_CHANNEL_CTRL_TX_PACK_16;
	tegra30_apbif_write(reg, val);

	reg = TEGRA30_AHUB_CIF_TX_CTRL +
	      (channel * TEGRA30_AHUB_CIF_TX_CTRL_STRIDE);
	val = (0 << TEGRA30_AUDIOCIF_CTRL_FIFO_THRESHOLD_SHIFT) |
	      (1 << TEGRA30_AUDIOCIF_CTRL_AUDIO_CHANNELS_SHIFT) |
	      (1 << TEGRA30_AUDIOCIF_CTRL_CLIENT_CHANNELS_SHIFT) |
	      TEGRA30_AUDIOCIF_CTRL_AUDIO_BITS_16 |
	      TEGRA30_AUDIOCIF_CTRL_CLIENT_BITS_16 |
	      TEGRA30_AUDIOCIF_CTRL_DIRECTION_TX;
	tegra30_apbif_write(reg, val);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_allocate_tx_fifo);

int tegra30_ahub_enable_tx_fifo(enum tegra30_ahub_txcif txcif)
{
	int channel = txcif - TEGRA30_AHUB_TXCIF_APBIF_TX0;
	int reg, val;

	reg = TEGRA30_AHUB_CHANNEL_CTRL +
	      (channel * TEGRA30_AHUB_CHANNEL_CTRL_STRIDE);
	val = tegra30_apbif_read(reg);
	val |= TEGRA30_AHUB_CHANNEL_CTRL_TX_EN;
	tegra30_apbif_write(reg, val);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_enable_tx_fifo);

int tegra30_ahub_disable_tx_fifo(enum tegra30_ahub_txcif txcif)
{
	int channel = txcif - TEGRA30_AHUB_TXCIF_APBIF_TX0;
	int reg, val;

	reg = TEGRA30_AHUB_CHANNEL_CTRL +
	      (channel * TEGRA30_AHUB_CHANNEL_CTRL_STRIDE);
	val = tegra30_apbif_read(reg);
	val &= ~TEGRA30_AHUB_CHANNEL_CTRL_TX_EN;
	tegra30_apbif_write(reg, val);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_disable_tx_fifo);

int tegra30_ahub_free_tx_fifo(enum tegra30_ahub_txcif txcif)
{
	int channel = txcif - TEGRA30_AHUB_TXCIF_APBIF_TX0;

	__clear_bit(channel, ahub->tx_usage);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_free_tx_fifo);

int tegra30_ahub_set_rx_cif_source(enum tegra30_ahub_rxcif rxcif,
				   enum tegra30_ahub_txcif txcif)
{
	int channel = rxcif - TEGRA30_AHUB_RXCIF_APBIF_RX0;
	int reg;

	reg = TEGRA30_AHUB_AUDIO_RX +
	      (channel * TEGRA30_AHUB_AUDIO_RX_STRIDE);
	tegra30_audio_write(reg, 1 << txcif);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_set_rx_cif_source);

int tegra30_ahub_unset_rx_cif_source(enum tegra30_ahub_rxcif rxcif)
{
	int channel = rxcif - TEGRA30_AHUB_RXCIF_APBIF_RX0;
	int reg;

	reg = TEGRA30_AHUB_AUDIO_RX +
	      (channel * TEGRA30_AHUB_AUDIO_RX_STRIDE);
	tegra30_audio_write(reg, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_unset_rx_cif_source);

#define CLK_LIST_MASK_TEGRA30	BIT(0)
#define CLK_LIST_MASK_TEGRA114	BIT(1)

#define CLK_LIST_MASK_TEGRA30_OR_LATER \
		(CLK_LIST_MASK_TEGRA30 | CLK_LIST_MASK_TEGRA114)

static const struct {
	const char *clk_name;
	u32 clk_list_mask;
} configlink_clocks[] = {
	{ "i2s0", CLK_LIST_MASK_TEGRA30_OR_LATER },
	{ "i2s1", CLK_LIST_MASK_TEGRA30_OR_LATER },
	{ "i2s2", CLK_LIST_MASK_TEGRA30_OR_LATER },
	{ "i2s3", CLK_LIST_MASK_TEGRA30_OR_LATER },
	{ "i2s4", CLK_LIST_MASK_TEGRA30_OR_LATER },
	{ "dam0", CLK_LIST_MASK_TEGRA30_OR_LATER },
	{ "dam1", CLK_LIST_MASK_TEGRA30_OR_LATER },
	{ "dam2", CLK_LIST_MASK_TEGRA30_OR_LATER },
	{ "spdif_in", CLK_LIST_MASK_TEGRA30_OR_LATER },
	{ "amx", CLK_LIST_MASK_TEGRA114 },
	{ "adx", CLK_LIST_MASK_TEGRA114 },
};

#define LAST_REG(name) \
	(TEGRA30_AHUB_##name + \
	 (TEGRA30_AHUB_##name##_STRIDE * TEGRA30_AHUB_##name##_COUNT) - 4)

#define REG_IN_ARRAY(reg, name) \
	((reg >= TEGRA30_AHUB_##name) && \
	 (reg <= LAST_REG(name) && \
	 (!((reg - TEGRA30_AHUB_##name) % TEGRA30_AHUB_##name##_STRIDE))))

static bool tegra30_ahub_apbif_wr_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA30_AHUB_CONFIG_LINK_CTRL:
	case TEGRA30_AHUB_MISC_CTRL:
	case TEGRA30_AHUB_APBDMA_LIVE_STATUS:
	case TEGRA30_AHUB_I2S_LIVE_STATUS:
	case TEGRA30_AHUB_SPDIF_LIVE_STATUS:
	case TEGRA30_AHUB_I2S_INT_MASK:
	case TEGRA30_AHUB_DAM_INT_MASK:
	case TEGRA30_AHUB_SPDIF_INT_MASK:
	case TEGRA30_AHUB_APBIF_INT_MASK:
	case TEGRA30_AHUB_I2S_INT_STATUS:
	case TEGRA30_AHUB_DAM_INT_STATUS:
	case TEGRA30_AHUB_SPDIF_INT_STATUS:
	case TEGRA30_AHUB_APBIF_INT_STATUS:
	case TEGRA30_AHUB_I2S_INT_SOURCE:
	case TEGRA30_AHUB_DAM_INT_SOURCE:
	case TEGRA30_AHUB_SPDIF_INT_SOURCE:
	case TEGRA30_AHUB_APBIF_INT_SOURCE:
	case TEGRA30_AHUB_I2S_INT_SET:
	case TEGRA30_AHUB_DAM_INT_SET:
	case TEGRA30_AHUB_SPDIF_INT_SET:
	case TEGRA30_AHUB_APBIF_INT_SET:
		return true;
	default:
		break;
	};

	if (REG_IN_ARRAY(reg, CHANNEL_CTRL) ||
	    REG_IN_ARRAY(reg, CHANNEL_CLEAR) ||
	    REG_IN_ARRAY(reg, CHANNEL_STATUS) ||
	    REG_IN_ARRAY(reg, CHANNEL_TXFIFO) ||
	    REG_IN_ARRAY(reg, CHANNEL_RXFIFO) ||
	    REG_IN_ARRAY(reg, CIF_TX_CTRL) ||
	    REG_IN_ARRAY(reg, CIF_RX_CTRL) ||
	    REG_IN_ARRAY(reg, DAM_LIVE_STATUS))
		return true;

	return false;
}

static bool tegra30_ahub_apbif_volatile_reg(struct device *dev,
					    unsigned int reg)
{
	switch (reg) {
	case TEGRA30_AHUB_CONFIG_LINK_CTRL:
	case TEGRA30_AHUB_MISC_CTRL:
	case TEGRA30_AHUB_APBDMA_LIVE_STATUS:
	case TEGRA30_AHUB_I2S_LIVE_STATUS:
	case TEGRA30_AHUB_SPDIF_LIVE_STATUS:
	case TEGRA30_AHUB_I2S_INT_STATUS:
	case TEGRA30_AHUB_DAM_INT_STATUS:
	case TEGRA30_AHUB_SPDIF_INT_STATUS:
	case TEGRA30_AHUB_APBIF_INT_STATUS:
	case TEGRA30_AHUB_I2S_INT_SET:
	case TEGRA30_AHUB_DAM_INT_SET:
	case TEGRA30_AHUB_SPDIF_INT_SET:
	case TEGRA30_AHUB_APBIF_INT_SET:
		return true;
	default:
		break;
	};

	if (REG_IN_ARRAY(reg, CHANNEL_CLEAR) ||
	    REG_IN_ARRAY(reg, CHANNEL_STATUS) ||
	    REG_IN_ARRAY(reg, CHANNEL_TXFIFO) ||
	    REG_IN_ARRAY(reg, CHANNEL_RXFIFO) ||
	    REG_IN_ARRAY(reg, DAM_LIVE_STATUS))
		return true;

	return false;
}

static bool tegra30_ahub_apbif_precious_reg(struct device *dev,
					    unsigned int reg)
{
	if (REG_IN_ARRAY(reg, CHANNEL_TXFIFO) ||
	    REG_IN_ARRAY(reg, CHANNEL_RXFIFO))
		return true;

	return false;
}

static const struct regmap_config tegra30_ahub_apbif_regmap_config = {
	.name = "apbif",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = TEGRA30_AHUB_APBIF_INT_SET,
	.writeable_reg = tegra30_ahub_apbif_wr_rd_reg,
	.readable_reg = tegra30_ahub_apbif_wr_rd_reg,
	.volatile_reg = tegra30_ahub_apbif_volatile_reg,
	.precious_reg = tegra30_ahub_apbif_precious_reg,
	.cache_type = REGCACHE_RBTREE,
};

static bool tegra30_ahub_ahub_wr_rd_reg(struct device *dev, unsigned int reg)
{
	if (REG_IN_ARRAY(reg, AUDIO_RX))
		return true;

	return false;
}

static const struct regmap_config tegra30_ahub_ahub_regmap_config = {
	.name = "ahub",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = LAST_REG(AUDIO_RX),
	.writeable_reg = tegra30_ahub_ahub_wr_rd_reg,
	.readable_reg = tegra30_ahub_ahub_wr_rd_reg,
	.cache_type = REGCACHE_RBTREE,
};

static struct tegra30_ahub_soc_data soc_data_tegra30 = {
	.clk_list_mask = CLK_LIST_MASK_TEGRA30,
};

static struct tegra30_ahub_soc_data soc_data_tegra114 = {
	.clk_list_mask = CLK_LIST_MASK_TEGRA114,
};

static const struct of_device_id tegra30_ahub_of_match[] = {
	{ .compatible = "nvidia,tegra114-ahub", .data = &soc_data_tegra114 },
	{ .compatible = "nvidia,tegra30-ahub",  .data = &soc_data_tegra30 },
	{},
};

static int tegra30_ahub_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct tegra30_ahub_soc_data *soc_data;
	struct clk *clk;
	int i;
	struct resource *res0, *res1, *region;
	u32 of_dma[2];
	void __iomem *regs_apbif, *regs_ahub;
	int ret = 0;

	if (ahub)
		return -ENODEV;

	match = of_match_device(tegra30_ahub_of_match, &pdev->dev);
	if (!match)
		return -EINVAL;
	soc_data = match->data;

	/*
	 * The AHUB hosts a register bus: the "configlink". For this to
	 * operate correctly, all devices on this bus must be out of reset.
	 * Ensure that here.
	 */
	for (i = 0; i < ARRAY_SIZE(configlink_clocks); i++) {
		if (!(configlink_clocks[i].clk_list_mask &
					soc_data->clk_list_mask))
			continue;
		clk = clk_get(&pdev->dev, configlink_clocks[i].clk_name);
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "Can't get clock %s\n",
				configlink_clocks[i].clk_name);
			ret = PTR_ERR(clk);
			goto err;
		}
		tegra_periph_reset_deassert(clk);
		clk_put(clk);
	}

	ahub = devm_kzalloc(&pdev->dev, sizeof(struct tegra30_ahub),
			    GFP_KERNEL);
	if (!ahub) {
		dev_err(&pdev->dev, "Can't allocate tegra30_ahub\n");
		ret = -ENOMEM;
		goto err;
	}
	dev_set_drvdata(&pdev->dev, ahub);

	ahub->dev = &pdev->dev;

	ahub->clk_d_audio = clk_get(&pdev->dev, "d_audio");
	if (IS_ERR(ahub->clk_d_audio)) {
		dev_err(&pdev->dev, "Can't retrieve ahub d_audio clock\n");
		ret = PTR_ERR(ahub->clk_d_audio);
		goto err;
	}

	ahub->clk_apbif = clk_get(&pdev->dev, "apbif");
	if (IS_ERR(ahub->clk_apbif)) {
		dev_err(&pdev->dev, "Can't retrieve ahub apbif clock\n");
		ret = PTR_ERR(ahub->clk_apbif);
		goto err_clk_put_d_audio;
	}

	if (of_property_read_u32_array(pdev->dev.of_node,
				"nvidia,dma-request-selector",
				of_dma, 2) < 0) {
		dev_err(&pdev->dev,
			"Missing property nvidia,dma-request-selector\n");
		ret = -ENODEV;
		goto err_clk_put_d_audio;
	}
	ahub->dma_sel = of_dma[1];

	res0 = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res0) {
		dev_err(&pdev->dev, "No apbif memory resource\n");
		ret = -ENODEV;
		goto err_clk_put_apbif;
	}

	region = devm_request_mem_region(&pdev->dev, res0->start,
					 resource_size(res0), DRV_NAME);
	if (!region) {
		dev_err(&pdev->dev, "request region apbif failed\n");
		ret = -EBUSY;
		goto err_clk_put_apbif;
	}
	ahub->apbif_addr = res0->start;

	regs_apbif = devm_ioremap(&pdev->dev, res0->start,
				  resource_size(res0));
	if (!regs_apbif) {
		dev_err(&pdev->dev, "ioremap apbif failed\n");
		ret = -ENOMEM;
		goto err_clk_put_apbif;
	}

	ahub->regmap_apbif = devm_regmap_init_mmio(&pdev->dev, regs_apbif,
					&tegra30_ahub_apbif_regmap_config);
	if (IS_ERR(ahub->regmap_apbif)) {
		dev_err(&pdev->dev, "apbif regmap init failed\n");
		ret = PTR_ERR(ahub->regmap_apbif);
		goto err_clk_put_apbif;
	}
	regcache_cache_only(ahub->regmap_apbif, true);

	res1 = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res1) {
		dev_err(&pdev->dev, "No ahub memory resource\n");
		ret = -ENODEV;
		goto err_clk_put_apbif;
	}

	region = devm_request_mem_region(&pdev->dev, res1->start,
					 resource_size(res1), DRV_NAME);
	if (!region) {
		dev_err(&pdev->dev, "request region ahub failed\n");
		ret = -EBUSY;
		goto err_clk_put_apbif;
	}

	regs_ahub = devm_ioremap(&pdev->dev, res1->start,
				 resource_size(res1));
	if (!regs_ahub) {
		dev_err(&pdev->dev, "ioremap ahub failed\n");
		ret = -ENOMEM;
		goto err_clk_put_apbif;
	}

	ahub->regmap_ahub = devm_regmap_init_mmio(&pdev->dev, regs_ahub,
					&tegra30_ahub_ahub_regmap_config);
	if (IS_ERR(ahub->regmap_ahub)) {
		dev_err(&pdev->dev, "ahub regmap init failed\n");
		ret = PTR_ERR(ahub->regmap_ahub);
		goto err_clk_put_apbif;
	}
	regcache_cache_only(ahub->regmap_ahub, true);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = tegra30_ahub_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);

	return 0;

err_pm_disable:
	pm_runtime_disable(&pdev->dev);
err_clk_put_apbif:
	clk_put(ahub->clk_apbif);
err_clk_put_d_audio:
	clk_put(ahub->clk_d_audio);
	ahub = NULL;
err:
	return ret;
}

static int tegra30_ahub_remove(struct platform_device *pdev)
{
	if (!ahub)
		return -ENODEV;

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra30_ahub_runtime_suspend(&pdev->dev);

	clk_put(ahub->clk_apbif);
	clk_put(ahub->clk_d_audio);

	ahub = NULL;

	return 0;
}

static const struct dev_pm_ops tegra30_ahub_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra30_ahub_runtime_suspend,
			   tegra30_ahub_runtime_resume, NULL)
};

static struct platform_driver tegra30_ahub_driver = {
	.probe = tegra30_ahub_probe,
	.remove = tegra30_ahub_remove,
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = tegra30_ahub_of_match,
		.pm = &tegra30_ahub_pm_ops,
	},
};
module_platform_driver(tegra30_ahub_driver);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Tegra30 AHUB driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, tegra30_ahub_of_match);
