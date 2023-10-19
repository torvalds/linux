// SPDX-License-Identifier: GPL-2.0-only
/*
 * tegra20_das.c - Tegra20 DAS driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2010 - NVIDIA, Inc.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/soc.h>

#define DRV_NAME "tegra20-das"

/* Register TEGRA20_DAS_DAP_CTRL_SEL */
#define TEGRA20_DAS_DAP_CTRL_SEL			0x00
#define TEGRA20_DAS_DAP_CTRL_SEL_COUNT			5
#define TEGRA20_DAS_DAP_CTRL_SEL_STRIDE			4
#define TEGRA20_DAS_DAP_CTRL_SEL_DAP_MS_SEL_P		31
#define TEGRA20_DAS_DAP_CTRL_SEL_DAP_MS_SEL_S		1
#define TEGRA20_DAS_DAP_CTRL_SEL_DAP_SDATA1_TX_RX_P	30
#define TEGRA20_DAS_DAP_CTRL_SEL_DAP_SDATA1_TX_RX_S	1
#define TEGRA20_DAS_DAP_CTRL_SEL_DAP_SDATA2_TX_RX_P	29
#define TEGRA20_DAS_DAP_CTRL_SEL_DAP_SDATA2_TX_RX_S	1
#define TEGRA20_DAS_DAP_CTRL_SEL_DAP_CTRL_SEL_P		0
#define TEGRA20_DAS_DAP_CTRL_SEL_DAP_CTRL_SEL_S		5

/* Values for field TEGRA20_DAS_DAP_CTRL_SEL_DAP_CTRL_SEL */
#define TEGRA20_DAS_DAP_SEL_DAC1	0
#define TEGRA20_DAS_DAP_SEL_DAC2	1
#define TEGRA20_DAS_DAP_SEL_DAC3	2
#define TEGRA20_DAS_DAP_SEL_DAP1	16
#define TEGRA20_DAS_DAP_SEL_DAP2	17
#define TEGRA20_DAS_DAP_SEL_DAP3	18
#define TEGRA20_DAS_DAP_SEL_DAP4	19
#define TEGRA20_DAS_DAP_SEL_DAP5	20

/* Register TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL */
#define TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL			0x40
#define TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL_COUNT		3
#define TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL_STRIDE		4
#define TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL_DAC_SDATA2_SEL_P	28
#define TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL_DAC_SDATA2_SEL_S	4
#define TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL_DAC_SDATA1_SEL_P	24
#define TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL_DAC_SDATA1_SEL_S	4
#define TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL_DAC_CLK_SEL_P	0
#define TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL_DAC_CLK_SEL_S	4

/*
 * Values for:
 * TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL_DAC_SDATA2_SEL
 * TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL_DAC_SDATA1_SEL
 * TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL_DAC_CLK_SEL
 */
#define TEGRA20_DAS_DAC_SEL_DAP1	0
#define TEGRA20_DAS_DAC_SEL_DAP2	1
#define TEGRA20_DAS_DAC_SEL_DAP3	2
#define TEGRA20_DAS_DAC_SEL_DAP4	3
#define TEGRA20_DAS_DAC_SEL_DAP5	4

/*
 * Names/IDs of the DACs/DAPs.
 */

#define TEGRA20_DAS_DAP_ID_1 0
#define TEGRA20_DAS_DAP_ID_2 1
#define TEGRA20_DAS_DAP_ID_3 2
#define TEGRA20_DAS_DAP_ID_4 3
#define TEGRA20_DAS_DAP_ID_5 4

#define TEGRA20_DAS_DAC_ID_1 0
#define TEGRA20_DAS_DAC_ID_2 1
#define TEGRA20_DAS_DAC_ID_3 2

struct tegra20_das {
	struct regmap *regmap;
};

/*
 * Terminology:
 * DAS: Digital audio switch (HW module controlled by this driver)
 * DAP: Digital audio port (port/pins on Tegra device)
 * DAC: Digital audio controller (e.g. I2S or AC97 controller elsewhere)
 *
 * The Tegra DAS is a mux/cross-bar which can connect each DAP to a specific
 * DAC, or another DAP. When DAPs are connected, one must be the master and
 * one the slave. Each DAC allows selection of a specific DAP for input, to
 * cater for the case where N DAPs are connected to 1 DAC for broadcast
 * output.
 *
 * This driver is dumb; no attempt is made to ensure that a valid routing
 * configuration is programmed.
 */

static inline void tegra20_das_write(struct tegra20_das *das, u32 reg, u32 val)
{
	regmap_write(das->regmap, reg, val);
}

static void tegra20_das_connect_dap_to_dac(struct tegra20_das *das, int dap, int dac)
{
	u32 addr;
	u32 reg;

	addr = TEGRA20_DAS_DAP_CTRL_SEL +
		(dap * TEGRA20_DAS_DAP_CTRL_SEL_STRIDE);
	reg = dac << TEGRA20_DAS_DAP_CTRL_SEL_DAP_CTRL_SEL_P;

	tegra20_das_write(das, addr, reg);
}

static void tegra20_das_connect_dac_to_dap(struct tegra20_das *das, int dac, int dap)
{
	u32 addr;
	u32 reg;

	addr = TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL +
		(dac * TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL_STRIDE);
	reg = dap << TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL_DAC_CLK_SEL_P |
		dap << TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL_DAC_SDATA1_SEL_P |
		dap << TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL_DAC_SDATA2_SEL_P;

	tegra20_das_write(das, addr, reg);
}

#define LAST_REG(name) \
	(TEGRA20_DAS_##name + \
	 (TEGRA20_DAS_##name##_STRIDE * (TEGRA20_DAS_##name##_COUNT - 1)))

static bool tegra20_das_wr_rd_reg(struct device *dev, unsigned int reg)
{
	if (reg <= LAST_REG(DAP_CTRL_SEL))
		return true;
	if ((reg >= TEGRA20_DAS_DAC_INPUT_DATA_CLK_SEL) &&
	    (reg <= LAST_REG(DAC_INPUT_DATA_CLK_SEL)))
		return true;

	return false;
}

static const struct regmap_config tegra20_das_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = LAST_REG(DAC_INPUT_DATA_CLK_SEL),
	.writeable_reg = tegra20_das_wr_rd_reg,
	.readable_reg = tegra20_das_wr_rd_reg,
	.cache_type = REGCACHE_FLAT,
};

static int tegra20_das_probe(struct platform_device *pdev)
{
	void __iomem *regs;
	struct tegra20_das *das;

	das = devm_kzalloc(&pdev->dev, sizeof(struct tegra20_das), GFP_KERNEL);
	if (!das)
		return -ENOMEM;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	das->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					    &tegra20_das_regmap_config);
	if (IS_ERR(das->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		return PTR_ERR(das->regmap);
	}

	tegra20_das_connect_dap_to_dac(das, TEGRA20_DAS_DAP_ID_1,
				       TEGRA20_DAS_DAP_SEL_DAC1);
	tegra20_das_connect_dac_to_dap(das, TEGRA20_DAS_DAC_ID_1,
				       TEGRA20_DAS_DAC_SEL_DAP1);
	tegra20_das_connect_dap_to_dac(das, TEGRA20_DAS_DAP_ID_3,
				       TEGRA20_DAS_DAP_SEL_DAC3);
	tegra20_das_connect_dac_to_dap(das, TEGRA20_DAS_DAC_ID_3,
				       TEGRA20_DAS_DAC_SEL_DAP3);

	return 0;
}

static const struct of_device_id tegra20_das_of_match[] = {
	{ .compatible = "nvidia,tegra20-das", },
	{},
};

static struct platform_driver tegra20_das_driver = {
	.probe = tegra20_das_probe,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = tegra20_das_of_match,
	},
};
module_platform_driver(tegra20_das_driver);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Tegra20 DAS driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, tegra20_das_of_match);
