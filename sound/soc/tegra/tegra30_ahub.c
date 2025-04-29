// SPDX-License-Identifier: GPL-2.0-only
/*
 * tegra30_ahub.c - Tegra30 AHUB driver
 *
 * Copyright (c) 2011,2012, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>
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

	clk_bulk_disable_unprepare(ahub->nclocks, ahub->clocks);

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

	ret = reset_control_bulk_assert(ahub->nresets, ahub->resets);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(ahub->nclocks, ahub->clocks);
	if (ret)
		return ret;

	usleep_range(10, 100);

	ret = reset_control_bulk_deassert(ahub->nresets, ahub->resets);
	if (ret)
		goto disable_clocks;

	regcache_cache_only(ahub->regmap_apbif, false);
	regcache_cache_only(ahub->regmap_ahub, false);
	regcache_mark_dirty(ahub->regmap_apbif);
	regcache_mark_dirty(ahub->regmap_ahub);

	ret = regcache_sync(ahub->regmap_apbif);
	if (ret)
		goto disable_clocks;

	ret = regcache_sync(ahub->regmap_ahub);
	if (ret)
		goto disable_clocks;

	return 0;

disable_clocks:
	clk_bulk_disable_unprepare(ahub->nclocks, ahub->clocks);

	return ret;
}

int tegra30_ahub_allocate_rx_fifo(enum tegra30_ahub_rxcif *rxcif,
				  char *dmachan, int dmachan_len,
				  dma_addr_t *fiforeg)
{
	int channel;
	u32 reg, val;
	struct tegra30_ahub_cif_conf cif_conf;

	channel = find_first_zero_bit(ahub->rx_usage,
				      TEGRA30_AHUB_CHANNEL_CTRL_COUNT);
	if (channel >= TEGRA30_AHUB_CHANNEL_CTRL_COUNT)
		return -EBUSY;

	__set_bit(channel, ahub->rx_usage);

	*rxcif = TEGRA30_AHUB_RXCIF_APBIF_RX0 + channel;
	snprintf(dmachan, dmachan_len, "rx%d", channel);
	*fiforeg = ahub->apbif_addr + TEGRA30_AHUB_CHANNEL_RXFIFO +
		   (channel * TEGRA30_AHUB_CHANNEL_RXFIFO_STRIDE);

	pm_runtime_get_sync(ahub->dev);

	reg = TEGRA30_AHUB_CHANNEL_CTRL +
	      (channel * TEGRA30_AHUB_CHANNEL_CTRL_STRIDE);
	val = tegra30_apbif_read(reg);
	val &= ~(TEGRA30_AHUB_CHANNEL_CTRL_RX_THRESHOLD_MASK |
		 TEGRA30_AHUB_CHANNEL_CTRL_RX_PACK_MASK);
	val |= (7 << TEGRA30_AHUB_CHANNEL_CTRL_RX_THRESHOLD_SHIFT) |
	       TEGRA30_AHUB_CHANNEL_CTRL_RX_PACK_EN |
	       TEGRA30_AHUB_CHANNEL_CTRL_RX_PACK_16;
	tegra30_apbif_write(reg, val);

	cif_conf.threshold = 0;
	cif_conf.audio_channels = 2;
	cif_conf.client_channels = 2;
	cif_conf.audio_bits = TEGRA30_AUDIOCIF_BITS_16;
	cif_conf.client_bits = TEGRA30_AUDIOCIF_BITS_16;
	cif_conf.expand = 0;
	cif_conf.stereo_conv = 0;
	cif_conf.replicate = 0;
	cif_conf.direction = TEGRA30_AUDIOCIF_DIRECTION_RX;
	cif_conf.truncate = 0;
	cif_conf.mono_conv = 0;

	reg = TEGRA30_AHUB_CIF_RX_CTRL +
	      (channel * TEGRA30_AHUB_CIF_RX_CTRL_STRIDE);
	ahub->soc_data->set_audio_cif(ahub->regmap_apbif, reg, &cif_conf);

	pm_runtime_put(ahub->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_allocate_rx_fifo);

int tegra30_ahub_enable_rx_fifo(enum tegra30_ahub_rxcif rxcif)
{
	int channel = rxcif - TEGRA30_AHUB_RXCIF_APBIF_RX0;
	int reg, val;

	pm_runtime_get_sync(ahub->dev);

	reg = TEGRA30_AHUB_CHANNEL_CTRL +
	      (channel * TEGRA30_AHUB_CHANNEL_CTRL_STRIDE);
	val = tegra30_apbif_read(reg);
	val |= TEGRA30_AHUB_CHANNEL_CTRL_RX_EN;
	tegra30_apbif_write(reg, val);

	pm_runtime_put(ahub->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_enable_rx_fifo);

int tegra30_ahub_disable_rx_fifo(enum tegra30_ahub_rxcif rxcif)
{
	int channel = rxcif - TEGRA30_AHUB_RXCIF_APBIF_RX0;
	int reg, val;

	pm_runtime_get_sync(ahub->dev);

	reg = TEGRA30_AHUB_CHANNEL_CTRL +
	      (channel * TEGRA30_AHUB_CHANNEL_CTRL_STRIDE);
	val = tegra30_apbif_read(reg);
	val &= ~TEGRA30_AHUB_CHANNEL_CTRL_RX_EN;
	tegra30_apbif_write(reg, val);

	pm_runtime_put(ahub->dev);

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
				  char *dmachan, int dmachan_len,
				  dma_addr_t *fiforeg)
{
	int channel;
	u32 reg, val;
	struct tegra30_ahub_cif_conf cif_conf;

	channel = find_first_zero_bit(ahub->tx_usage,
				      TEGRA30_AHUB_CHANNEL_CTRL_COUNT);
	if (channel >= TEGRA30_AHUB_CHANNEL_CTRL_COUNT)
		return -EBUSY;

	__set_bit(channel, ahub->tx_usage);

	*txcif = TEGRA30_AHUB_TXCIF_APBIF_TX0 + channel;
	snprintf(dmachan, dmachan_len, "tx%d", channel);
	*fiforeg = ahub->apbif_addr + TEGRA30_AHUB_CHANNEL_TXFIFO +
		   (channel * TEGRA30_AHUB_CHANNEL_TXFIFO_STRIDE);

	pm_runtime_get_sync(ahub->dev);

	reg = TEGRA30_AHUB_CHANNEL_CTRL +
	      (channel * TEGRA30_AHUB_CHANNEL_CTRL_STRIDE);
	val = tegra30_apbif_read(reg);
	val &= ~(TEGRA30_AHUB_CHANNEL_CTRL_TX_THRESHOLD_MASK |
		 TEGRA30_AHUB_CHANNEL_CTRL_TX_PACK_MASK);
	val |= (7 << TEGRA30_AHUB_CHANNEL_CTRL_TX_THRESHOLD_SHIFT) |
	       TEGRA30_AHUB_CHANNEL_CTRL_TX_PACK_EN |
	       TEGRA30_AHUB_CHANNEL_CTRL_TX_PACK_16;
	tegra30_apbif_write(reg, val);

	cif_conf.threshold = 0;
	cif_conf.audio_channels = 2;
	cif_conf.client_channels = 2;
	cif_conf.audio_bits = TEGRA30_AUDIOCIF_BITS_16;
	cif_conf.client_bits = TEGRA30_AUDIOCIF_BITS_16;
	cif_conf.expand = 0;
	cif_conf.stereo_conv = 0;
	cif_conf.replicate = 0;
	cif_conf.direction = TEGRA30_AUDIOCIF_DIRECTION_TX;
	cif_conf.truncate = 0;
	cif_conf.mono_conv = 0;

	reg = TEGRA30_AHUB_CIF_TX_CTRL +
	      (channel * TEGRA30_AHUB_CIF_TX_CTRL_STRIDE);
	ahub->soc_data->set_audio_cif(ahub->regmap_apbif, reg, &cif_conf);

	pm_runtime_put(ahub->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_allocate_tx_fifo);

int tegra30_ahub_enable_tx_fifo(enum tegra30_ahub_txcif txcif)
{
	int channel = txcif - TEGRA30_AHUB_TXCIF_APBIF_TX0;
	int reg, val;

	pm_runtime_get_sync(ahub->dev);

	reg = TEGRA30_AHUB_CHANNEL_CTRL +
	      (channel * TEGRA30_AHUB_CHANNEL_CTRL_STRIDE);
	val = tegra30_apbif_read(reg);
	val |= TEGRA30_AHUB_CHANNEL_CTRL_TX_EN;
	tegra30_apbif_write(reg, val);

	pm_runtime_put(ahub->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_enable_tx_fifo);

int tegra30_ahub_disable_tx_fifo(enum tegra30_ahub_txcif txcif)
{
	int channel = txcif - TEGRA30_AHUB_TXCIF_APBIF_TX0;
	int reg, val;

	pm_runtime_get_sync(ahub->dev);

	reg = TEGRA30_AHUB_CHANNEL_CTRL +
	      (channel * TEGRA30_AHUB_CHANNEL_CTRL_STRIDE);
	val = tegra30_apbif_read(reg);
	val &= ~TEGRA30_AHUB_CHANNEL_CTRL_TX_EN;
	tegra30_apbif_write(reg, val);

	pm_runtime_put(ahub->dev);

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

	pm_runtime_get_sync(ahub->dev);

	reg = TEGRA30_AHUB_AUDIO_RX +
	      (channel * TEGRA30_AHUB_AUDIO_RX_STRIDE);
	tegra30_audio_write(reg, 1 << txcif);

	pm_runtime_put(ahub->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_set_rx_cif_source);

int tegra30_ahub_unset_rx_cif_source(enum tegra30_ahub_rxcif rxcif)
{
	int channel = rxcif - TEGRA30_AHUB_RXCIF_APBIF_RX0;
	int reg;

	pm_runtime_get_sync(ahub->dev);

	reg = TEGRA30_AHUB_AUDIO_RX +
	      (channel * TEGRA30_AHUB_AUDIO_RX_STRIDE);
	tegra30_audio_write(reg, 0);

	pm_runtime_put(ahub->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra30_ahub_unset_rx_cif_source);

static const struct reset_control_bulk_data tegra30_ahub_resets_data[] = {
	{ "d_audio" },
	{ "apbif" },
	{ "i2s0" },
	{ "i2s1" },
	{ "i2s2" },
	{ "i2s3" },
	{ "i2s4" },
	{ "dam0" },
	{ "dam1" },
	{ "dam2" },
	{ "spdif" },
	{ "amx" }, /* Tegra114+ */
	{ "adx" }, /* Tegra114+ */
	{ "amx1" }, /* Tegra124 */
	{ "adx1" }, /* Tegra124 */
	{ "afc0" }, /* Tegra124 */
	{ "afc1" }, /* Tegra124 */
	{ "afc2" }, /* Tegra124 */
	{ "afc3" }, /* Tegra124 */
	{ "afc4" }, /* Tegra124 */
	{ "afc5" }, /* Tegra124 */
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
	}

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
	}

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
	.cache_type = REGCACHE_FLAT,
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
	.cache_type = REGCACHE_FLAT,
};

static struct tegra30_ahub_soc_data soc_data_tegra30 = {
	.num_resets = 11,
	.set_audio_cif = tegra30_ahub_set_cif,
};

static struct tegra30_ahub_soc_data soc_data_tegra114 = {
	.num_resets = 13,
	.set_audio_cif = tegra30_ahub_set_cif,
};

static struct tegra30_ahub_soc_data soc_data_tegra124 = {
	.num_resets = 21,
	.set_audio_cif = tegra124_ahub_set_cif,
};

static const struct of_device_id tegra30_ahub_of_match[] = {
	{ .compatible = "nvidia,tegra124-ahub", .data = &soc_data_tegra124 },
	{ .compatible = "nvidia,tegra114-ahub", .data = &soc_data_tegra114 },
	{ .compatible = "nvidia,tegra30-ahub",  .data = &soc_data_tegra30 },
	{},
};

static int tegra30_ahub_probe(struct platform_device *pdev)
{
	const struct tegra30_ahub_soc_data *soc_data;
	struct resource *res0;
	void __iomem *regs_apbif, *regs_ahub;
	int ret = 0;

	soc_data = of_device_get_match_data(&pdev->dev);
	if (!soc_data)
		return -EINVAL;

	ahub = devm_kzalloc(&pdev->dev, sizeof(struct tegra30_ahub),
			    GFP_KERNEL);
	if (!ahub)
		return -ENOMEM;
	dev_set_drvdata(&pdev->dev, ahub);

	BUILD_BUG_ON(sizeof(ahub->resets) != sizeof(tegra30_ahub_resets_data));
	memcpy(ahub->resets, tegra30_ahub_resets_data, sizeof(ahub->resets));

	ahub->nresets = soc_data->num_resets;
	ahub->soc_data = soc_data;
	ahub->dev = &pdev->dev;

	ahub->clocks[ahub->nclocks++].id = "apbif";
	ahub->clocks[ahub->nclocks++].id = "d_audio";

	ret = devm_clk_bulk_get(&pdev->dev, ahub->nclocks, ahub->clocks);
	if (ret)
		goto err_unset_ahub;

	ret = devm_reset_control_bulk_get_exclusive(&pdev->dev, ahub->nresets,
						    ahub->resets);
	if (ret) {
		dev_err(&pdev->dev, "Can't get resets: %d\n", ret);
		goto err_unset_ahub;
	}

	regs_apbif = devm_platform_get_and_ioremap_resource(pdev, 0, &res0);
	if (IS_ERR(regs_apbif)) {
		ret = PTR_ERR(regs_apbif);
		goto err_unset_ahub;
	}

	ahub->apbif_addr = res0->start;

	ahub->regmap_apbif = devm_regmap_init_mmio(&pdev->dev, regs_apbif,
					&tegra30_ahub_apbif_regmap_config);
	if (IS_ERR(ahub->regmap_apbif)) {
		dev_err(&pdev->dev, "apbif regmap init failed\n");
		ret = PTR_ERR(ahub->regmap_apbif);
		goto err_unset_ahub;
	}
	regcache_cache_only(ahub->regmap_apbif, true);

	regs_ahub = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(regs_ahub)) {
		ret = PTR_ERR(regs_ahub);
		goto err_unset_ahub;
	}

	ahub->regmap_ahub = devm_regmap_init_mmio(&pdev->dev, regs_ahub,
					&tegra30_ahub_ahub_regmap_config);
	if (IS_ERR(ahub->regmap_ahub)) {
		dev_err(&pdev->dev, "ahub regmap init failed\n");
		ret = PTR_ERR(ahub->regmap_ahub);
		goto err_unset_ahub;
	}
	regcache_cache_only(ahub->regmap_ahub, true);

	pm_runtime_enable(&pdev->dev);

	of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);

	return 0;

err_unset_ahub:
	ahub = NULL;

	return ret;
}

static void tegra30_ahub_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	ahub = NULL;
}

static const struct dev_pm_ops tegra30_ahub_pm_ops = {
	RUNTIME_PM_OPS(tegra30_ahub_runtime_suspend,
		       tegra30_ahub_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
};

static struct platform_driver tegra30_ahub_driver = {
	.probe = tegra30_ahub_probe,
	.remove = tegra30_ahub_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = tegra30_ahub_of_match,
		.pm = pm_ptr(&tegra30_ahub_pm_ops),
	},
};
module_platform_driver(tegra30_ahub_driver);

void tegra30_ahub_set_cif(struct regmap *regmap, unsigned int reg,
			  struct tegra30_ahub_cif_conf *conf)
{
	unsigned int value;

	value = (conf->threshold <<
			TEGRA30_AUDIOCIF_CTRL_FIFO_THRESHOLD_SHIFT) |
		((conf->audio_channels - 1) <<
			TEGRA30_AUDIOCIF_CTRL_AUDIO_CHANNELS_SHIFT) |
		((conf->client_channels - 1) <<
			TEGRA30_AUDIOCIF_CTRL_CLIENT_CHANNELS_SHIFT) |
		(conf->audio_bits <<
			TEGRA30_AUDIOCIF_CTRL_AUDIO_BITS_SHIFT) |
		(conf->client_bits <<
			TEGRA30_AUDIOCIF_CTRL_CLIENT_BITS_SHIFT) |
		(conf->expand <<
			TEGRA30_AUDIOCIF_CTRL_EXPAND_SHIFT) |
		(conf->stereo_conv <<
			TEGRA30_AUDIOCIF_CTRL_STEREO_CONV_SHIFT) |
		(conf->replicate <<
			TEGRA30_AUDIOCIF_CTRL_REPLICATE_SHIFT) |
		(conf->direction <<
			TEGRA30_AUDIOCIF_CTRL_DIRECTION_SHIFT) |
		(conf->truncate <<
			TEGRA30_AUDIOCIF_CTRL_TRUNCATE_SHIFT) |
		(conf->mono_conv <<
			TEGRA30_AUDIOCIF_CTRL_MONO_CONV_SHIFT);

	regmap_write(regmap, reg, value);
}
EXPORT_SYMBOL_GPL(tegra30_ahub_set_cif);

void tegra124_ahub_set_cif(struct regmap *regmap, unsigned int reg,
			   struct tegra30_ahub_cif_conf *conf)
{
	unsigned int value;

	value = (conf->threshold <<
			TEGRA124_AUDIOCIF_CTRL_FIFO_THRESHOLD_SHIFT) |
		((conf->audio_channels - 1) <<
			TEGRA124_AUDIOCIF_CTRL_AUDIO_CHANNELS_SHIFT) |
		((conf->client_channels - 1) <<
			TEGRA124_AUDIOCIF_CTRL_CLIENT_CHANNELS_SHIFT) |
		(conf->audio_bits <<
			TEGRA30_AUDIOCIF_CTRL_AUDIO_BITS_SHIFT) |
		(conf->client_bits <<
			TEGRA30_AUDIOCIF_CTRL_CLIENT_BITS_SHIFT) |
		(conf->expand <<
			TEGRA30_AUDIOCIF_CTRL_EXPAND_SHIFT) |
		(conf->stereo_conv <<
			TEGRA30_AUDIOCIF_CTRL_STEREO_CONV_SHIFT) |
		(conf->replicate <<
			TEGRA30_AUDIOCIF_CTRL_REPLICATE_SHIFT) |
		(conf->direction <<
			TEGRA30_AUDIOCIF_CTRL_DIRECTION_SHIFT) |
		(conf->truncate <<
			TEGRA30_AUDIOCIF_CTRL_TRUNCATE_SHIFT) |
		(conf->mono_conv <<
			TEGRA30_AUDIOCIF_CTRL_MONO_CONV_SHIFT);

	regmap_write(regmap, reg, value);
}
EXPORT_SYMBOL_GPL(tegra124_ahub_set_cif);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Tegra30 AHUB driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, tegra30_ahub_of_match);
