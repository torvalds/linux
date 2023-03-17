// SPDX-License-Identifier: GPL-2.0
/*
 * mt8188-afe-clk.c  --  MediaTek 8188 afe clock ctrl
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Bicycle Tsai <bicycle.tsai@mediatek.com>
 *         Trevor Wu <trevor.wu@mediatek.com>
 *         Chun-Chia Chiu <chun-chia.chiu@mediatek.com>
 */

#include <linux/clk.h>

#include "mt8188-afe-common.h"
#include "mt8188-afe-clk.h"
#include "mt8188-audsys-clk.h"
#include "mt8188-reg.h"

static const char *aud_clks[MT8188_CLK_NUM] = {
	/* xtal */
	[MT8188_CLK_XTAL_26M] = "clk26m",

	/* pll */
	[MT8188_CLK_APMIXED_APLL1] = "apll1",
	[MT8188_CLK_APMIXED_APLL2] = "apll2",

	/* divider */
	[MT8188_CLK_TOP_APLL12_DIV0] = "apll12_div0",
	[MT8188_CLK_TOP_APLL12_DIV1] = "apll12_div1",
	[MT8188_CLK_TOP_APLL12_DIV2] = "apll12_div2",
	[MT8188_CLK_TOP_APLL12_DIV3] = "apll12_div3",
	[MT8188_CLK_TOP_APLL12_DIV9] = "apll12_div9",

	/* mux */
	[MT8188_CLK_TOP_A1SYS_HP_SEL] = "top_a1sys_hp",
	[MT8188_CLK_TOP_AUD_INTBUS_SEL] = "top_aud_intbus",
	[MT8188_CLK_TOP_AUDIO_H_SEL] = "top_audio_h",
	[MT8188_CLK_TOP_AUDIO_LOCAL_BUS_SEL] = "top_audio_local_bus",
	[MT8188_CLK_TOP_DPTX_M_SEL] = "top_dptx",
	[MT8188_CLK_TOP_I2SO1_M_SEL] = "top_i2so1",
	[MT8188_CLK_TOP_I2SO2_M_SEL] = "top_i2so2",
	[MT8188_CLK_TOP_I2SI1_M_SEL] = "top_i2si1",
	[MT8188_CLK_TOP_I2SI2_M_SEL] = "top_i2si2",

	/* clock gate */
	[MT8188_CLK_ADSP_AUDIO_26M] = "adsp_audio_26m",
	/* afe clock gate */
	[MT8188_CLK_AUD_AFE] = "aud_afe",
	[MT8188_CLK_AUD_APLL1_TUNER] = "aud_apll1_tuner",
	[MT8188_CLK_AUD_APLL2_TUNER] = "aud_apll2_tuner",
	[MT8188_CLK_AUD_APLL] = "aud_apll",
	[MT8188_CLK_AUD_APLL2] = "aud_apll2",
	[MT8188_CLK_AUD_DAC] = "aud_dac",
	[MT8188_CLK_AUD_ADC] = "aud_adc",
	[MT8188_CLK_AUD_DAC_HIRES] = "aud_dac_hires",
	[MT8188_CLK_AUD_A1SYS_HP] = "aud_a1sys_hp",
	[MT8188_CLK_AUD_ADC_HIRES] = "aud_adc_hires",
	[MT8188_CLK_AUD_I2SIN] = "aud_i2sin",
	[MT8188_CLK_AUD_TDM_IN] = "aud_tdm_in",
	[MT8188_CLK_AUD_I2S_OUT] = "aud_i2s_out",
	[MT8188_CLK_AUD_TDM_OUT] = "aud_tdm_out",
	[MT8188_CLK_AUD_HDMI_OUT] = "aud_hdmi_out",
	[MT8188_CLK_AUD_ASRC11] = "aud_asrc11",
	[MT8188_CLK_AUD_ASRC12] = "aud_asrc12",
	[MT8188_CLK_AUD_A1SYS] = "aud_a1sys",
	[MT8188_CLK_AUD_A2SYS] = "aud_a2sys",
	[MT8188_CLK_AUD_PCMIF] = "aud_pcmif",
	[MT8188_CLK_AUD_MEMIF_UL1] = "aud_memif_ul1",
	[MT8188_CLK_AUD_MEMIF_UL2] = "aud_memif_ul2",
	[MT8188_CLK_AUD_MEMIF_UL3] = "aud_memif_ul3",
	[MT8188_CLK_AUD_MEMIF_UL4] = "aud_memif_ul4",
	[MT8188_CLK_AUD_MEMIF_UL5] = "aud_memif_ul5",
	[MT8188_CLK_AUD_MEMIF_UL6] = "aud_memif_ul6",
	[MT8188_CLK_AUD_MEMIF_UL8] = "aud_memif_ul8",
	[MT8188_CLK_AUD_MEMIF_UL9] = "aud_memif_ul9",
	[MT8188_CLK_AUD_MEMIF_UL10] = "aud_memif_ul10",
	[MT8188_CLK_AUD_MEMIF_DL2] = "aud_memif_dl2",
	[MT8188_CLK_AUD_MEMIF_DL3] = "aud_memif_dl3",
	[MT8188_CLK_AUD_MEMIF_DL6] = "aud_memif_dl6",
	[MT8188_CLK_AUD_MEMIF_DL7] = "aud_memif_dl7",
	[MT8188_CLK_AUD_MEMIF_DL8] = "aud_memif_dl8",
	[MT8188_CLK_AUD_MEMIF_DL10] = "aud_memif_dl10",
	[MT8188_CLK_AUD_MEMIF_DL11] = "aud_memif_dl11",
};

struct mt8188_afe_tuner_cfg {
	unsigned int id;
	int apll_div_reg;
	unsigned int apll_div_shift;
	unsigned int apll_div_maskbit;
	unsigned int apll_div_default;
	int ref_ck_sel_reg;
	unsigned int ref_ck_sel_shift;
	unsigned int ref_ck_sel_maskbit;
	unsigned int ref_ck_sel_default;
	int tuner_en_reg;
	unsigned int tuner_en_shift;
	unsigned int tuner_en_maskbit;
	int upper_bound_reg;
	unsigned int upper_bound_shift;
	unsigned int upper_bound_maskbit;
	unsigned int upper_bound_default;
	spinlock_t ctrl_lock; /* lock for apll tuner ctrl*/
	int ref_cnt;
};

static struct mt8188_afe_tuner_cfg
	mt8188_afe_tuner_cfgs[MT8188_AUD_PLL_NUM] = {
	[MT8188_AUD_PLL1] = {
		.id = MT8188_AUD_PLL1,
		.apll_div_reg = AFE_APLL_TUNER_CFG,
		.apll_div_shift = 4,
		.apll_div_maskbit = 0xf,
		.apll_div_default = 0x7,
		.ref_ck_sel_reg = AFE_APLL_TUNER_CFG,
		.ref_ck_sel_shift = 1,
		.ref_ck_sel_maskbit = 0x3,
		.ref_ck_sel_default = 0x2,
		.tuner_en_reg = AFE_APLL_TUNER_CFG,
		.tuner_en_shift = 0,
		.tuner_en_maskbit = 0x1,
		.upper_bound_reg = AFE_APLL_TUNER_CFG,
		.upper_bound_shift = 8,
		.upper_bound_maskbit = 0xff,
		.upper_bound_default = 0x3,
	},
	[MT8188_AUD_PLL2] = {
		.id = MT8188_AUD_PLL2,
		.apll_div_reg = AFE_APLL_TUNER_CFG1,
		.apll_div_shift = 4,
		.apll_div_maskbit = 0xf,
		.apll_div_default = 0x7,
		.ref_ck_sel_reg = AFE_APLL_TUNER_CFG1,
		.ref_ck_sel_shift = 1,
		.ref_ck_sel_maskbit = 0x3,
		.ref_ck_sel_default = 0x1,
		.tuner_en_reg = AFE_APLL_TUNER_CFG1,
		.tuner_en_shift = 0,
		.tuner_en_maskbit = 0x1,
		.upper_bound_reg = AFE_APLL_TUNER_CFG1,
		.upper_bound_shift = 8,
		.upper_bound_maskbit = 0xff,
		.upper_bound_default = 0x3,
	},
	[MT8188_AUD_PLL3] = {
		.id = MT8188_AUD_PLL3,
		.apll_div_reg = AFE_EARC_APLL_TUNER_CFG,
		.apll_div_shift = 4,
		.apll_div_maskbit = 0x3f,
		.apll_div_default = 0x3,
		.ref_ck_sel_reg = AFE_EARC_APLL_TUNER_CFG,
		.ref_ck_sel_shift = 24,
		.ref_ck_sel_maskbit = 0x3,
		.ref_ck_sel_default = 0x0,
		.tuner_en_reg = AFE_EARC_APLL_TUNER_CFG,
		.tuner_en_shift = 0,
		.tuner_en_maskbit = 0x1,
		.upper_bound_reg = AFE_EARC_APLL_TUNER_CFG,
		.upper_bound_shift = 12,
		.upper_bound_maskbit = 0xff,
		.upper_bound_default = 0x4,
	},
	[MT8188_AUD_PLL4] = {
		.id = MT8188_AUD_PLL4,
		.apll_div_reg = AFE_SPDIFIN_APLL_TUNER_CFG,
		.apll_div_shift = 4,
		.apll_div_maskbit = 0x3f,
		.apll_div_default = 0x7,
		.ref_ck_sel_reg = AFE_SPDIFIN_APLL_TUNER_CFG1,
		.ref_ck_sel_shift = 8,
		.ref_ck_sel_maskbit = 0x1,
		.ref_ck_sel_default = 0,
		.tuner_en_reg = AFE_SPDIFIN_APLL_TUNER_CFG,
		.tuner_en_shift = 0,
		.tuner_en_maskbit = 0x1,
		.upper_bound_reg = AFE_SPDIFIN_APLL_TUNER_CFG,
		.upper_bound_shift = 12,
		.upper_bound_maskbit = 0xff,
		.upper_bound_default = 0x4,
	},
	[MT8188_AUD_PLL5] = {
		.id = MT8188_AUD_PLL5,
		.apll_div_reg = AFE_LINEIN_APLL_TUNER_CFG,
		.apll_div_shift = 4,
		.apll_div_maskbit = 0x3f,
		.apll_div_default = 0x3,
		.ref_ck_sel_reg = AFE_LINEIN_APLL_TUNER_CFG,
		.ref_ck_sel_shift = 24,
		.ref_ck_sel_maskbit = 0x1,
		.ref_ck_sel_default = 0,
		.tuner_en_reg = AFE_LINEIN_APLL_TUNER_CFG,
		.tuner_en_shift = 0,
		.tuner_en_maskbit = 0x1,
		.upper_bound_reg = AFE_LINEIN_APLL_TUNER_CFG,
		.upper_bound_shift = 12,
		.upper_bound_maskbit = 0xff,
		.upper_bound_default = 0x4,
	},
};

static struct mt8188_afe_tuner_cfg *mt8188_afe_found_apll_tuner(unsigned int id)
{
	if (id >= MT8188_AUD_PLL_NUM)
		return NULL;

	return &mt8188_afe_tuner_cfgs[id];
}

static int mt8188_afe_init_apll_tuner(unsigned int id)
{
	struct mt8188_afe_tuner_cfg *cfg = mt8188_afe_found_apll_tuner(id);

	if (!cfg)
		return -EINVAL;

	cfg->ref_cnt = 0;
	spin_lock_init(&cfg->ctrl_lock);

	return 0;
}

static int mt8188_afe_setup_apll_tuner(struct mtk_base_afe *afe, unsigned int id)
{
	const struct mt8188_afe_tuner_cfg *cfg = mt8188_afe_found_apll_tuner(id);

	if (!cfg)
		return -EINVAL;

	regmap_update_bits(afe->regmap,
			   cfg->apll_div_reg,
			   cfg->apll_div_maskbit << cfg->apll_div_shift,
			   cfg->apll_div_default << cfg->apll_div_shift);

	regmap_update_bits(afe->regmap,
			   cfg->ref_ck_sel_reg,
			   cfg->ref_ck_sel_maskbit << cfg->ref_ck_sel_shift,
			   cfg->ref_ck_sel_default << cfg->ref_ck_sel_shift);

	regmap_update_bits(afe->regmap,
			   cfg->upper_bound_reg,
			   cfg->upper_bound_maskbit << cfg->upper_bound_shift,
			   cfg->upper_bound_default << cfg->upper_bound_shift);

	return 0;
}

static int mt8188_afe_enable_tuner_clk(struct mtk_base_afe *afe,
				       unsigned int id)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;

	switch (id) {
	case MT8188_AUD_PLL1:
		mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_APLL]);
		mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_APLL1_TUNER]);
		break;
	case MT8188_AUD_PLL2:
		mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_APLL2]);
		mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_APLL2_TUNER]);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mt8188_afe_disable_tuner_clk(struct mtk_base_afe *afe,
					unsigned int id)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;

	switch (id) {
	case MT8188_AUD_PLL1:
		mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_APLL1_TUNER]);
		mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_APLL]);
		break;
	case MT8188_AUD_PLL2:
		mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_APLL2_TUNER]);
		mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_APLL2]);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mt8188_afe_enable_apll_tuner(struct mtk_base_afe *afe, unsigned int id)
{
	struct mt8188_afe_tuner_cfg *cfg = mt8188_afe_found_apll_tuner(id);
	unsigned long flags;
	int ret;

	if (!cfg)
		return -EINVAL;

	ret = mt8188_afe_setup_apll_tuner(afe, id);
	if (ret)
		return ret;

	ret = mt8188_afe_enable_tuner_clk(afe, id);
	if (ret)
		return ret;

	spin_lock_irqsave(&cfg->ctrl_lock, flags);

	cfg->ref_cnt++;
	if (cfg->ref_cnt == 1)
		regmap_update_bits(afe->regmap,
				   cfg->tuner_en_reg,
				   cfg->tuner_en_maskbit << cfg->tuner_en_shift,
				   BIT(cfg->tuner_en_shift));

	spin_unlock_irqrestore(&cfg->ctrl_lock, flags);

	return 0;
}

static int mt8188_afe_disable_apll_tuner(struct mtk_base_afe *afe, unsigned int id)
{
	struct mt8188_afe_tuner_cfg *cfg = mt8188_afe_found_apll_tuner(id);
	unsigned long flags;
	int ret;

	if (!cfg)
		return -EINVAL;

	spin_lock_irqsave(&cfg->ctrl_lock, flags);

	cfg->ref_cnt--;
	if (cfg->ref_cnt == 0)
		regmap_update_bits(afe->regmap,
				   cfg->tuner_en_reg,
				   cfg->tuner_en_maskbit << cfg->tuner_en_shift,
				   0 << cfg->tuner_en_shift);
	else if (cfg->ref_cnt < 0)
		cfg->ref_cnt = 0;

	spin_unlock_irqrestore(&cfg->ctrl_lock, flags);

	ret = mt8188_afe_disable_tuner_clk(afe, id);
	if (ret)
		return ret;

	return 0;
}

int mt8188_afe_get_mclk_source_clk_id(int sel)
{
	switch (sel) {
	case MT8188_MCK_SEL_26M:
		return MT8188_CLK_XTAL_26M;
	case MT8188_MCK_SEL_APLL1:
		return MT8188_CLK_APMIXED_APLL1;
	case MT8188_MCK_SEL_APLL2:
		return MT8188_CLK_APMIXED_APLL2;
	default:
		return -EINVAL;
	}
}

int mt8188_afe_get_mclk_source_rate(struct mtk_base_afe *afe, int apll)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	int clk_id = mt8188_afe_get_mclk_source_clk_id(apll);

	if (clk_id < 0) {
		dev_dbg(afe->dev, "invalid clk id\n");
		return 0;
	}

	return clk_get_rate(afe_priv->clk[clk_id]);
}

int mt8188_afe_get_default_mclk_source_by_rate(int rate)
{
	return ((rate % 8000) == 0) ?
		MT8188_MCK_SEL_APLL1 : MT8188_MCK_SEL_APLL2;
}

int mt8188_afe_init_clock(struct mtk_base_afe *afe)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	int i, ret;

	ret = mt8188_audsys_clk_register(afe);
	if (ret) {
		dev_err(afe->dev, "register audsys clk fail %d\n", ret);
		return ret;
	}

	afe_priv->clk =
		devm_kcalloc(afe->dev, MT8188_CLK_NUM, sizeof(*afe_priv->clk),
			     GFP_KERNEL);
	if (!afe_priv->clk)
		return -ENOMEM;

	for (i = 0; i < MT8188_CLK_NUM; i++) {
		afe_priv->clk[i] = devm_clk_get(afe->dev, aud_clks[i]);
		if (IS_ERR(afe_priv->clk[i])) {
			dev_err(afe->dev, "%s(), devm_clk_get %s fail, ret %ld\n",
				__func__, aud_clks[i],
				PTR_ERR(afe_priv->clk[i]));
			return PTR_ERR(afe_priv->clk[i]);
		}
	}

	/* initial tuner */
	for (i = 0; i < MT8188_AUD_PLL_NUM; i++) {
		ret = mt8188_afe_init_apll_tuner(i);
		if (ret) {
			dev_info(afe->dev, "%s(), init apll_tuner%d failed",
				 __func__, (i + 1));
			return -EINVAL;
		}
	}

	return 0;
}

void mt8188_afe_deinit_clock(void *priv)
{
	struct mtk_base_afe *afe = priv;

	mt8188_audsys_clk_unregister(afe);
}

int mt8188_afe_enable_clk(struct mtk_base_afe *afe, struct clk *clk)
{
	int ret;

	if (clk) {
		ret = clk_prepare_enable(clk);
		if (ret) {
			dev_dbg(afe->dev, "%s(), failed to enable clk\n",
				__func__);
			return ret;
		}
	} else {
		dev_dbg(afe->dev, "NULL clk\n");
	}
	return 0;
}
EXPORT_SYMBOL_GPL(mt8188_afe_enable_clk);

void mt8188_afe_disable_clk(struct mtk_base_afe *afe, struct clk *clk)
{
	if (clk)
		clk_disable_unprepare(clk);
	else
		dev_dbg(afe->dev, "NULL clk\n");
}
EXPORT_SYMBOL_GPL(mt8188_afe_disable_clk);

int mt8188_afe_set_clk_rate(struct mtk_base_afe *afe, struct clk *clk,
			    unsigned int rate)
{
	int ret;

	if (clk) {
		ret = clk_set_rate(clk, rate);
		if (ret) {
			dev_dbg(afe->dev, "%s(), failed to set clk rate\n",
				__func__);
			return ret;
		}
	}

	return 0;
}

int mt8188_afe_set_clk_parent(struct mtk_base_afe *afe, struct clk *clk,
			      struct clk *parent)
{
	int ret;

	if (clk && parent) {
		ret = clk_set_parent(clk, parent);
		if (ret) {
			dev_dbg(afe->dev, "%s(), failed to set clk parent\n",
				__func__);
			return ret;
		}
	}

	return 0;
}

static unsigned int get_top_cg_reg(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8188_TOP_CG_A1SYS_TIMING:
	case MT8188_TOP_CG_A2SYS_TIMING:
	case MT8188_TOP_CG_26M_TIMING:
		return ASYS_TOP_CON;
	default:
		return 0;
	}
}

static unsigned int get_top_cg_mask(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8188_TOP_CG_A1SYS_TIMING:
		return ASYS_TOP_CON_A1SYS_TIMING_ON;
	case MT8188_TOP_CG_A2SYS_TIMING:
		return ASYS_TOP_CON_A2SYS_TIMING_ON;
	case MT8188_TOP_CG_26M_TIMING:
		return ASYS_TOP_CON_26M_TIMING_ON;
	default:
		return 0;
	}
}

static unsigned int get_top_cg_on_val(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8188_TOP_CG_A1SYS_TIMING:
	case MT8188_TOP_CG_A2SYS_TIMING:
	case MT8188_TOP_CG_26M_TIMING:
		return get_top_cg_mask(cg_type);
	default:
		return 0;
	}
}

static unsigned int get_top_cg_off_val(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8188_TOP_CG_A1SYS_TIMING:
	case MT8188_TOP_CG_A2SYS_TIMING:
	case MT8188_TOP_CG_26M_TIMING:
		return 0;
	default:
		return get_top_cg_mask(cg_type);
	}
}

static int mt8188_afe_enable_top_cg(struct mtk_base_afe *afe, unsigned int cg_type)
{
	unsigned int reg = get_top_cg_reg(cg_type);
	unsigned int mask = get_top_cg_mask(cg_type);
	unsigned int val = get_top_cg_on_val(cg_type);

	regmap_update_bits(afe->regmap, reg, mask, val);

	return 0;
}

static int mt8188_afe_disable_top_cg(struct mtk_base_afe *afe, unsigned int cg_type)
{
	unsigned int reg = get_top_cg_reg(cg_type);
	unsigned int mask = get_top_cg_mask(cg_type);
	unsigned int val = get_top_cg_off_val(cg_type);

	regmap_update_bits(afe->regmap, reg, mask, val);

	return 0;
}

int mt8188_afe_enable_reg_rw_clk(struct mtk_base_afe *afe)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;

	/* bus clock for AFE external access, like DRAM */
	mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_TOP_AUDIO_LOCAL_BUS_SEL]);

	/* bus clock for AFE internal access, like AFE SRAM */
	mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_TOP_AUD_INTBUS_SEL]);

	/* audio 26m clock source */
	mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_ADSP_AUDIO_26M]);

	/* AFE hw clock */
	mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_AFE]);
	mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_A1SYS_HP]);
	mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_A1SYS]);

	return 0;
}

int mt8188_afe_disable_reg_rw_clk(struct mtk_base_afe *afe)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;

	mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_A1SYS]);
	mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_A1SYS_HP]);
	mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_AFE]);
	mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_ADSP_AUDIO_26M]);
	mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_TOP_AUD_INTBUS_SEL]);
	mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_TOP_AUDIO_LOCAL_BUS_SEL]);

	return 0;
}

static int mt8188_afe_enable_afe_on(struct mtk_base_afe *afe)
{
	regmap_update_bits(afe->regmap, AFE_DAC_CON0, 0x1, 0x1);
	return 0;
}

static int mt8188_afe_disable_afe_on(struct mtk_base_afe *afe)
{
	regmap_update_bits(afe->regmap, AFE_DAC_CON0, 0x1, 0x0);
	return 0;
}

static int mt8188_afe_enable_timing_sys(struct mtk_base_afe *afe)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;

	mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_A1SYS]);
	mt8188_afe_enable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_A2SYS]);

	mt8188_afe_enable_top_cg(afe, MT8188_TOP_CG_A1SYS_TIMING);
	mt8188_afe_enable_top_cg(afe, MT8188_TOP_CG_A2SYS_TIMING);
	mt8188_afe_enable_top_cg(afe, MT8188_TOP_CG_26M_TIMING);

	return 0;
}

static int mt8188_afe_disable_timing_sys(struct mtk_base_afe *afe)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;

	mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_A1SYS]);
	mt8188_afe_disable_clk(afe, afe_priv->clk[MT8188_CLK_AUD_A2SYS]);

	mt8188_afe_disable_top_cg(afe, MT8188_TOP_CG_26M_TIMING);
	mt8188_afe_disable_top_cg(afe, MT8188_TOP_CG_A2SYS_TIMING);
	mt8188_afe_disable_top_cg(afe, MT8188_TOP_CG_A1SYS_TIMING);

	return 0;
}

int mt8188_afe_enable_main_clock(struct mtk_base_afe *afe)
{
	mt8188_afe_enable_timing_sys(afe);

	mt8188_afe_enable_afe_on(afe);

	mt8188_afe_enable_apll_tuner(afe, MT8188_AUD_PLL1);
	mt8188_afe_enable_apll_tuner(afe, MT8188_AUD_PLL2);

	return 0;
}

int mt8188_afe_disable_main_clock(struct mtk_base_afe *afe)
{
	mt8188_afe_disable_apll_tuner(afe, MT8188_AUD_PLL2);
	mt8188_afe_disable_apll_tuner(afe, MT8188_AUD_PLL1);

	mt8188_afe_disable_afe_on(afe);

	mt8188_afe_disable_timing_sys(afe);

	return 0;
}
