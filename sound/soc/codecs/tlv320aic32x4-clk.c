/* SPDX-License-Identifier: GPL-2.0
 *
 * Clock Tree for the Texas Instruments TLV320AIC32x4
 *
 * Copyright 2019 Annaliese McDermond
 *
 * Author: Annaliese McDermond <nh6z@nh6z.net>
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/regmap.h>
#include <linux/device.h>

#include "tlv320aic32x4.h"

#define to_clk_aic32x4(_hw) container_of(_hw, struct clk_aic32x4, hw)
struct clk_aic32x4 {
	struct clk_hw hw;
	struct device *dev;
	struct regmap *regmap;
	unsigned int reg;
};

/*
 * struct clk_aic32x4_pll_muldiv - Multiplier/divider settings
 * @p:		Divider
 * @r:		first multiplier
 * @j:		integer part of second multiplier
 * @d:		decimal part of second multiplier
 */
struct clk_aic32x4_pll_muldiv {
	u8 p;
	u16 r;
	u8 j;
	u16 d;
};

struct aic32x4_clkdesc {
	const char *name;
	const char * const *parent_names;
	unsigned int num_parents;
	const struct clk_ops *ops;
	unsigned int reg;
};

static int clk_aic32x4_pll_prepare(struct clk_hw *hw)
{
	struct clk_aic32x4 *pll = to_clk_aic32x4(hw);

	return regmap_update_bits(pll->regmap, AIC32X4_PLLPR,
				AIC32X4_PLLEN, AIC32X4_PLLEN);
}

static void clk_aic32x4_pll_unprepare(struct clk_hw *hw)
{
	struct clk_aic32x4 *pll = to_clk_aic32x4(hw);

	regmap_update_bits(pll->regmap, AIC32X4_PLLPR,
				AIC32X4_PLLEN, 0);
}

static int clk_aic32x4_pll_is_prepared(struct clk_hw *hw)
{
	struct clk_aic32x4 *pll = to_clk_aic32x4(hw);

	unsigned int val;
	int ret;

	ret = regmap_read(pll->regmap, AIC32X4_PLLPR, &val);
	if (ret < 0)
		return ret;

	return !!(val & AIC32X4_PLLEN);
}

static int clk_aic32x4_pll_get_muldiv(struct clk_aic32x4 *pll,
			struct clk_aic32x4_pll_muldiv *settings)
{
	/*	Change to use regmap_bulk_read? */
	unsigned int val;
	int ret;

	ret = regmap_read(pll->regmap, AIC32X4_PLLPR, &val);
	if (ret)
		return ret;
	settings->r = val & AIC32X4_PLL_R_MASK;
	settings->p = (val & AIC32X4_PLL_P_MASK) >> AIC32X4_PLL_P_SHIFT;

	ret = regmap_read(pll->regmap, AIC32X4_PLLJ, &val);
	if (ret < 0)
		return ret;
	settings->j = val;

	ret = regmap_read(pll->regmap, AIC32X4_PLLDMSB, &val);
	if (ret < 0)
		return ret;
	settings->d = val << 8;

	ret = regmap_read(pll->regmap, AIC32X4_PLLDLSB,	 &val);
	if (ret < 0)
		return ret;
	settings->d |= val;

	return 0;
}

static int clk_aic32x4_pll_set_muldiv(struct clk_aic32x4 *pll,
			struct clk_aic32x4_pll_muldiv *settings)
{
	int ret;
	/*	Change to use regmap_bulk_write for some if not all? */

	ret = regmap_update_bits(pll->regmap, AIC32X4_PLLPR,
				AIC32X4_PLL_R_MASK, settings->r);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(pll->regmap, AIC32X4_PLLPR,
				AIC32X4_PLL_P_MASK,
				settings->p << AIC32X4_PLL_P_SHIFT);
	if (ret < 0)
		return ret;

	ret = regmap_write(pll->regmap, AIC32X4_PLLJ, settings->j);
	if (ret < 0)
		return ret;

	ret = regmap_write(pll->regmap, AIC32X4_PLLDMSB, (settings->d >> 8));
	if (ret < 0)
		return ret;
	ret = regmap_write(pll->regmap, AIC32X4_PLLDLSB, (settings->d & 0xff));
	if (ret < 0)
		return ret;

	return 0;
}

static unsigned long clk_aic32x4_pll_calc_rate(
			struct clk_aic32x4_pll_muldiv *settings,
			unsigned long parent_rate)
{
	u64 rate;
	/*
	 * We scale j by 10000 to account for the decimal part of P and divide
	 * it back out later.
	 */
	rate = (u64) parent_rate * settings->r *
				((settings->j * 10000) + settings->d);

	return (unsigned long) DIV_ROUND_UP_ULL(rate, settings->p * 10000);
}

static int clk_aic32x4_pll_calc_muldiv(struct clk_aic32x4_pll_muldiv *settings,
			unsigned long rate, unsigned long parent_rate)
{
	u64 multiplier;

	settings->p = parent_rate / AIC32X4_MAX_PLL_CLKIN + 1;
	if (settings->p > 8)
		return -1;

	/*
	 * We scale this figure by 10000 so that we can get the decimal part
	 * of the multiplier.	This is because we can't do floating point
	 * math in the kernel.
	 */
	 multiplier = (u64) rate * settings->p * 10000;
	 do_div(multiplier, parent_rate);

	/*
	 * J can't be over 64, so R can scale this.
	 * R can't be greater than 4.
	 */
	settings->r = ((u32) multiplier / 640000) + 1;
	if (settings->r > 4)
		return -1;
	do_div(multiplier, settings->r);

	/*
	 * J can't be < 1.
	 */
	if (multiplier < 10000)
		return -1;

	/* Figure out the integer part, J, and the fractional part, D. */
	settings->j = (u32) multiplier / 10000;
	settings->d = (u32) multiplier % 10000;

	return 0;
}

static unsigned long clk_aic32x4_pll_recalc_rate(struct clk_hw *hw,
			unsigned long parent_rate)
{
	struct clk_aic32x4 *pll = to_clk_aic32x4(hw);
	struct clk_aic32x4_pll_muldiv settings;
	int ret;

	ret =  clk_aic32x4_pll_get_muldiv(pll, &settings);
	if (ret < 0)
		return 0;

	return clk_aic32x4_pll_calc_rate(&settings, parent_rate);
}

static long clk_aic32x4_pll_round_rate(struct clk_hw *hw,
			unsigned long rate,
			unsigned long *parent_rate)
{
	struct clk_aic32x4_pll_muldiv settings;
	int ret;

	ret = clk_aic32x4_pll_calc_muldiv(&settings, rate, *parent_rate);
	if (ret < 0)
		return 0;

	return clk_aic32x4_pll_calc_rate(&settings, *parent_rate);
}

static int clk_aic32x4_pll_set_rate(struct clk_hw *hw,
			unsigned long rate,
			unsigned long parent_rate)
{
	struct clk_aic32x4 *pll = to_clk_aic32x4(hw);
	struct clk_aic32x4_pll_muldiv settings;
	int ret;

	ret = clk_aic32x4_pll_calc_muldiv(&settings, rate, parent_rate);
	if (ret < 0)
		return -EINVAL;

	return clk_aic32x4_pll_set_muldiv(pll, &settings);
}

static int clk_aic32x4_pll_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_aic32x4 *pll = to_clk_aic32x4(hw);

	return regmap_update_bits(pll->regmap,
				AIC32X4_CLKMUX,
				AIC32X4_PLL_CLKIN_MASK,
				index << AIC32X4_PLL_CLKIN_SHIFT);
}

static u8 clk_aic32x4_pll_get_parent(struct clk_hw *hw)
{
	struct clk_aic32x4 *pll = to_clk_aic32x4(hw);
	unsigned int val;

	regmap_read(pll->regmap, AIC32X4_PLLPR, &val);

	return (val & AIC32X4_PLL_CLKIN_MASK) >> AIC32X4_PLL_CLKIN_SHIFT;
}


static const struct clk_ops aic32x4_pll_ops = {
	.prepare = clk_aic32x4_pll_prepare,
	.unprepare = clk_aic32x4_pll_unprepare,
	.is_prepared = clk_aic32x4_pll_is_prepared,
	.recalc_rate = clk_aic32x4_pll_recalc_rate,
	.round_rate = clk_aic32x4_pll_round_rate,
	.set_rate = clk_aic32x4_pll_set_rate,
	.set_parent = clk_aic32x4_pll_set_parent,
	.get_parent = clk_aic32x4_pll_get_parent,
};

static struct aic32x4_clkdesc aic32x4_clkdesc_array[] = {
	{
		.name = "pll",
		.parent_names =
			(const char* []) { "mclk", "bclk", "gpio", "din" },
		.num_parents = 4,
		.ops = &aic32x4_pll_ops,
		.reg = 0,
	},
};

static struct clk *aic32x4_register_clk(struct device *dev,
			struct aic32x4_clkdesc *desc)
{
	struct clk_init_data init;
	struct clk_aic32x4 *priv;
	const char *devname = dev_name(dev);

	init.ops = desc->ops;
	init.name = desc->name;
	init.parent_names = desc->parent_names;
	init.num_parents = desc->num_parents;
	init.flags = 0;

	priv = devm_kzalloc(dev, sizeof(struct clk_aic32x4), GFP_KERNEL);
	if (priv == NULL)
		return (struct clk *) -ENOMEM;

	priv->dev = dev;
	priv->hw.init = &init;
	priv->regmap = dev_get_regmap(dev, NULL);
	priv->reg = desc->reg;

	clk_hw_register_clkdev(&priv->hw, desc->name, devname);
	return devm_clk_register(dev, &priv->hw);
}

int aic32x4_register_clocks(struct device *dev, const char *mclk_name)
{
	int i;

	/*
	 * These lines are here to preserve the current functionality of
	 * the driver with regard to the DT.  These should eventually be set
	 * by DT nodes so that the connections can be set up in configuration
	 * rather than code.
	 */
	aic32x4_clkdesc_array[0].parent_names =
			(const char* []) { mclk_name, "bclk", "gpio", "din" };

	for (i = 0; i < ARRAY_SIZE(aic32x4_clkdesc_array); ++i)
		aic32x4_register_clk(dev, &aic32x4_clkdesc_array[i]);

	return 0;
}
EXPORT_SYMBOL_GPL(aic32x4_register_clocks);
