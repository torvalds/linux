// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020, Linaro Limited

#include <linux/err.h>
#include <linux/init.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include "q6afe.h"

#define Q6AFE_CLK(id) {					\
		.clk_id	= id,				\
		.afe_clk_id	= Q6AFE_##id,		\
		.name = #id,				\
		.rate = 19200000,			\
	}

#define Q6AFE_VOTE_CLK(id, blkid, n) {			\
		.clk_id	= id,				\
		.afe_clk_id = blkid,			\
		.name = n,				\
	}

struct q6afe_clk_init {
	int clk_id;
	int afe_clk_id;
	char *name;
	int rate;
};

struct q6afe_clk {
	struct device *dev;
	int afe_clk_id;
	int attributes;
	int rate;
	uint32_t handle;
	struct clk_hw hw;
};

#define to_q6afe_clk(_hw) container_of(_hw, struct q6afe_clk, hw)

struct q6afe_cc {
	struct device *dev;
	struct q6afe_clk *clks[Q6AFE_MAX_CLK_ID];
};

static int clk_q6afe_prepare(struct clk_hw *hw)
{
	struct q6afe_clk *clk = to_q6afe_clk(hw);

	return q6afe_set_lpass_clock(clk->dev, clk->afe_clk_id, clk->attributes,
				     Q6AFE_LPASS_CLK_ROOT_DEFAULT, clk->rate);
}

static void clk_q6afe_unprepare(struct clk_hw *hw)
{
	struct q6afe_clk *clk = to_q6afe_clk(hw);

	q6afe_set_lpass_clock(clk->dev, clk->afe_clk_id, clk->attributes,
			      Q6AFE_LPASS_CLK_ROOT_DEFAULT, 0);
}

static int clk_q6afe_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct q6afe_clk *clk = to_q6afe_clk(hw);

	clk->rate = rate;

	return 0;
}

static unsigned long clk_q6afe_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct q6afe_clk *clk = to_q6afe_clk(hw);

	return clk->rate;
}

static long clk_q6afe_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *parent_rate)
{
	return rate;
}

static const struct clk_ops clk_q6afe_ops = {
	.prepare	= clk_q6afe_prepare,
	.unprepare	= clk_q6afe_unprepare,
	.set_rate	= clk_q6afe_set_rate,
	.round_rate	= clk_q6afe_round_rate,
	.recalc_rate	= clk_q6afe_recalc_rate,
};

static int clk_vote_q6afe_block(struct clk_hw *hw)
{
	struct q6afe_clk *clk = to_q6afe_clk(hw);

	return q6afe_vote_lpass_core_hw(clk->dev, clk->afe_clk_id,
					clk_hw_get_name(&clk->hw), &clk->handle);
}

static void clk_unvote_q6afe_block(struct clk_hw *hw)
{
	struct q6afe_clk *clk = to_q6afe_clk(hw);

	q6afe_unvote_lpass_core_hw(clk->dev, clk->afe_clk_id, clk->handle);
}

static const struct clk_ops clk_vote_q6afe_ops = {
	.prepare	= clk_vote_q6afe_block,
	.unprepare	= clk_unvote_q6afe_block,
};

static const struct q6afe_clk_init q6afe_clks[] = {
	Q6AFE_CLK(LPASS_CLK_ID_PRI_MI2S_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_PRI_MI2S_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_SEC_MI2S_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_SEC_MI2S_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_TER_MI2S_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_TER_MI2S_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_QUAD_MI2S_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_QUAD_MI2S_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_SPEAKER_I2S_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_SPEAKER_I2S_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_SPEAKER_I2S_OSR),
	Q6AFE_CLK(LPASS_CLK_ID_QUI_MI2S_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_QUI_MI2S_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_SEN_MI2S_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_SEN_MI2S_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_INT0_MI2S_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_INT1_MI2S_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_INT2_MI2S_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_INT3_MI2S_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_INT4_MI2S_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_INT5_MI2S_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_INT6_MI2S_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_QUI_MI2S_OSR),
	Q6AFE_CLK(LPASS_CLK_ID_PRI_PCM_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_PRI_PCM_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_SEC_PCM_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_SEC_PCM_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_TER_PCM_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_TER_PCM_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_QUAD_PCM_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_QUAD_PCM_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_QUIN_PCM_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_QUIN_PCM_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_QUI_PCM_OSR),
	Q6AFE_CLK(LPASS_CLK_ID_PRI_TDM_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_PRI_TDM_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_SEC_TDM_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_SEC_TDM_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_TER_TDM_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_TER_TDM_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_QUAD_TDM_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_QUAD_TDM_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_QUIN_TDM_IBIT),
	Q6AFE_CLK(LPASS_CLK_ID_QUIN_TDM_EBIT),
	Q6AFE_CLK(LPASS_CLK_ID_QUIN_TDM_OSR),
	Q6AFE_CLK(LPASS_CLK_ID_MCLK_1),
	Q6AFE_CLK(LPASS_CLK_ID_MCLK_2),
	Q6AFE_CLK(LPASS_CLK_ID_MCLK_3),
	Q6AFE_CLK(LPASS_CLK_ID_MCLK_4),
	Q6AFE_CLK(LPASS_CLK_ID_INTERNAL_DIGITAL_CODEC_CORE),
	Q6AFE_CLK(LPASS_CLK_ID_INT_MCLK_0),
	Q6AFE_CLK(LPASS_CLK_ID_INT_MCLK_1),
	Q6AFE_CLK(LPASS_CLK_ID_WSA_CORE_MCLK),
	Q6AFE_CLK(LPASS_CLK_ID_WSA_CORE_NPL_MCLK),
	Q6AFE_CLK(LPASS_CLK_ID_VA_CORE_MCLK),
	Q6AFE_CLK(LPASS_CLK_ID_TX_CORE_MCLK),
	Q6AFE_CLK(LPASS_CLK_ID_TX_CORE_NPL_MCLK),
	Q6AFE_CLK(LPASS_CLK_ID_RX_CORE_MCLK),
	Q6AFE_CLK(LPASS_CLK_ID_RX_CORE_NPL_MCLK),
	Q6AFE_CLK(LPASS_CLK_ID_VA_CORE_2X_MCLK),
	Q6AFE_VOTE_CLK(LPASS_HW_AVTIMER_VOTE,
		       Q6AFE_LPASS_CORE_AVTIMER_BLOCK,
		       "LPASS_AVTIMER_MACRO"),
	Q6AFE_VOTE_CLK(LPASS_HW_MACRO_VOTE,
		       Q6AFE_LPASS_CORE_HW_MACRO_BLOCK,
		       "LPASS_HW_MACRO"),
	Q6AFE_VOTE_CLK(LPASS_HW_DCODEC_VOTE,
		       Q6AFE_LPASS_CORE_HW_DCODEC_BLOCK,
		       "LPASS_HW_DCODEC"),
};

static struct clk_hw *q6afe_of_clk_hw_get(struct of_phandle_args *clkspec,
					  void *data)
{
	struct q6afe_cc *cc = data;
	unsigned int idx = clkspec->args[0];
	unsigned int attr = clkspec->args[1];

	if (idx >= Q6AFE_MAX_CLK_ID || attr > LPASS_CLK_ATTRIBUTE_COUPLE_DIVISOR) {
		dev_err(cc->dev, "Invalid clk specifier (%d, %d)\n", idx, attr);
		return ERR_PTR(-EINVAL);
	}

	if (cc->clks[idx]) {
		cc->clks[idx]->attributes = attr;
		return &cc->clks[idx]->hw;
	}

	return ERR_PTR(-ENOENT);
}

static int q6afe_clock_dev_probe(struct platform_device *pdev)
{
	struct q6afe_cc *cc;
	struct device *dev = &pdev->dev;
	int i, ret;

	cc = devm_kzalloc(dev, sizeof(*cc), GFP_KERNEL);
	if (!cc)
		return -ENOMEM;

	cc->dev = dev;
	for (i = 0; i < ARRAY_SIZE(q6afe_clks); i++) {
		unsigned int id = q6afe_clks[i].clk_id;
		struct clk_init_data init = {
			.name =  q6afe_clks[i].name,
		};
		struct q6afe_clk *clk;

		clk = devm_kzalloc(dev, sizeof(*clk), GFP_KERNEL);
		if (!clk)
			return -ENOMEM;

		clk->dev = dev;
		clk->afe_clk_id = q6afe_clks[i].afe_clk_id;
		clk->rate = q6afe_clks[i].rate;
		clk->hw.init = &init;

		if (clk->rate)
			init.ops = &clk_q6afe_ops;
		else
			init.ops = &clk_vote_q6afe_ops;

		cc->clks[id] = clk;

		ret = devm_clk_hw_register(dev, &clk->hw);
		if (ret)
			return ret;
	}

	ret = devm_of_clk_add_hw_provider(dev, q6afe_of_clk_hw_get, cc);
	if (ret)
		return ret;

	dev_set_drvdata(dev, cc);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id q6afe_clock_device_id[] = {
	{ .compatible = "qcom,q6afe-clocks" },
	{},
};
MODULE_DEVICE_TABLE(of, q6afe_clock_device_id);
#endif

static struct platform_driver q6afe_clock_platform_driver = {
	.driver = {
		.name = "q6afe-clock",
		.of_match_table = of_match_ptr(q6afe_clock_device_id),
	},
	.probe = q6afe_clock_dev_probe,
};
module_platform_driver(q6afe_clock_platform_driver);

MODULE_DESCRIPTION("Q6 Audio Frontend clock driver");
MODULE_LICENSE("GPL v2");
