// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, Linaro Limited

#include <dt-bindings/sound/qcom,q6dsp-lpass-ports.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include "q6dsp-lpass-clocks.h"
#include "q6prm.h"

#define Q6PRM_CLK(id) {					\
		.clk_id	= id,				\
		.q6dsp_clk_id	= Q6PRM_##id,		\
		.name = #id,				\
		.rate = 19200000,			\
	}

static const struct q6dsp_clk_init q6prm_clks[] = {
	Q6PRM_CLK(LPASS_CLK_ID_PRI_MI2S_IBIT),
	Q6PRM_CLK(LPASS_CLK_ID_PRI_MI2S_EBIT),
	Q6PRM_CLK(LPASS_CLK_ID_SEC_MI2S_IBIT),
	Q6PRM_CLK(LPASS_CLK_ID_SEC_MI2S_EBIT),
	Q6PRM_CLK(LPASS_CLK_ID_TER_MI2S_IBIT),
	Q6PRM_CLK(LPASS_CLK_ID_TER_MI2S_EBIT),
	Q6PRM_CLK(LPASS_CLK_ID_QUAD_MI2S_IBIT),
	Q6PRM_CLK(LPASS_CLK_ID_QUAD_MI2S_EBIT),
	Q6PRM_CLK(LPASS_CLK_ID_SPEAKER_I2S_IBIT),
	Q6PRM_CLK(LPASS_CLK_ID_SPEAKER_I2S_EBIT),
	Q6PRM_CLK(LPASS_CLK_ID_SPEAKER_I2S_OSR),
	Q6PRM_CLK(LPASS_CLK_ID_QUI_MI2S_IBIT),
	Q6PRM_CLK(LPASS_CLK_ID_QUI_MI2S_EBIT),
	Q6PRM_CLK(LPASS_CLK_ID_SEN_MI2S_IBIT),
	Q6PRM_CLK(LPASS_CLK_ID_SEN_MI2S_EBIT),
	Q6PRM_CLK(LPASS_CLK_ID_INT0_MI2S_IBIT),
	Q6PRM_CLK(LPASS_CLK_ID_INT1_MI2S_IBIT),
	Q6PRM_CLK(LPASS_CLK_ID_INT2_MI2S_IBIT),
	Q6PRM_CLK(LPASS_CLK_ID_INT3_MI2S_IBIT),
	Q6PRM_CLK(LPASS_CLK_ID_INT4_MI2S_IBIT),
	Q6PRM_CLK(LPASS_CLK_ID_INT5_MI2S_IBIT),
	Q6PRM_CLK(LPASS_CLK_ID_INT6_MI2S_IBIT),
	Q6PRM_CLK(LPASS_CLK_ID_QUI_MI2S_OSR),
	Q6PRM_CLK(LPASS_CLK_ID_WSA_CORE_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_WSA_CORE_NPL_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_VA_CORE_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_TX_CORE_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_TX_CORE_NPL_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_RX_CORE_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_RX_CORE_NPL_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_VA_CORE_2X_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_WSA2_CORE_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_WSA2_CORE_2X_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_RX_CORE_TX_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_RX_CORE_TX_2X_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_WSA_CORE_TX_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_WSA_CORE_TX_2X_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_WSA2_CORE_TX_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_WSA2_CORE_TX_2X_MCLK),
	Q6PRM_CLK(LPASS_CLK_ID_RX_CORE_MCLK2_2X_MCLK),
	Q6DSP_VOTE_CLK(LPASS_HW_MACRO_VOTE, Q6PRM_HW_CORE_ID_LPASS,
		       "LPASS_HW_MACRO"),
	Q6DSP_VOTE_CLK(LPASS_HW_DCODEC_VOTE, Q6PRM_HW_CORE_ID_DCODEC,
		       "LPASS_HW_DCODEC"),
};

static const struct q6dsp_clk_desc q6dsp_clk_q6prm __maybe_unused = {
	.clks = q6prm_clks,
	.num_clks = ARRAY_SIZE(q6prm_clks),
	.lpass_set_clk = q6prm_set_lpass_clock,
	.lpass_vote_clk = q6prm_vote_lpass_core_hw,
	.lpass_unvote_clk = q6prm_unvote_lpass_core_hw,
};

#ifdef CONFIG_OF
static const struct of_device_id q6prm_clock_device_id[] = {
	{ .compatible = "qcom,q6prm-lpass-clocks", .data = &q6dsp_clk_q6prm },
	{},
};
MODULE_DEVICE_TABLE(of, q6prm_clock_device_id);
#endif

static struct platform_driver q6prm_clock_platform_driver = {
	.driver = {
		.name = "q6prm-lpass-clock",
		.of_match_table = of_match_ptr(q6prm_clock_device_id),
	},
	.probe = q6dsp_clock_dev_probe,
};
module_platform_driver(q6prm_clock_platform_driver);

MODULE_DESCRIPTION("Q6 Proxy Resource Manager LPASS clock driver");
MODULE_LICENSE("GPL");
