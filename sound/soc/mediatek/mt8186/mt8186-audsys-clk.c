// SPDX-License-Identifier: GPL-2.0
//
// mt8186-audsys-clk.h  --  Mediatek 8186 audsys clock control
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Jiaxin Yu <jiaxin.yu@mediatek.com>

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include "mt8186-afe-common.h"
#include "mt8186-audsys-clk.h"
#include "mt8186-audsys-clkid.h"
#include "mt8186-reg.h"

struct afe_gate {
	int id;
	const char *name;
	const char *parent_name;
	int reg;
	u8 bit;
	const struct clk_ops *ops;
	unsigned long flags;
	u8 cg_flags;
};

#define GATE_AFE_FLAGS(_id, _name, _parent, _reg, _bit, _flags, _cgflags) {\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.reg = _reg,					\
		.bit = _bit,					\
		.flags = _flags,				\
		.cg_flags = _cgflags,				\
	}

#define GATE_AFE(_id, _name, _parent, _reg, _bit)		\
	GATE_AFE_FLAGS(_id, _name, _parent, _reg, _bit,		\
		       CLK_SET_RATE_PARENT, CLK_GATE_SET_TO_DISABLE)

#define GATE_AUD0(_id, _name, _parent, _bit)			\
	GATE_AFE(_id, _name, _parent, AUDIO_TOP_CON0, _bit)

#define GATE_AUD1(_id, _name, _parent, _bit)			\
	GATE_AFE(_id, _name, _parent, AUDIO_TOP_CON1, _bit)

#define GATE_AUD2(_id, _name, _parent, _bit)			\
	GATE_AFE(_id, _name, _parent, AUDIO_TOP_CON2, _bit)

static const struct afe_gate aud_clks[CLK_AUD_NR_CLK] = {
	/* AUD0 */
	GATE_AUD0(CLK_AUD_AFE, "aud_afe_clk", "top_audio", 2),
	GATE_AUD0(CLK_AUD_22M, "aud_apll22m_clk", "top_aud_engen1", 8),
	GATE_AUD0(CLK_AUD_24M, "aud_apll24m_clk", "top_aud_engen2", 9),
	GATE_AUD0(CLK_AUD_APLL2_TUNER, "aud_apll2_tuner_clk", "top_aud_engen2", 18),
	GATE_AUD0(CLK_AUD_APLL_TUNER, "aud_apll_tuner_clk", "top_aud_engen1", 19),
	GATE_AUD0(CLK_AUD_TDM, "aud_tdm_clk", "top_aud_1", 20),
	GATE_AUD0(CLK_AUD_ADC, "aud_adc_clk", "top_audio", 24),
	GATE_AUD0(CLK_AUD_DAC, "aud_dac_clk", "top_audio", 25),
	GATE_AUD0(CLK_AUD_DAC_PREDIS, "aud_dac_predis_clk", "top_audio", 26),
	GATE_AUD0(CLK_AUD_TML, "aud_tml_clk", "top_audio", 27),
	GATE_AUD0(CLK_AUD_NLE, "aud_nle_clk", "top_audio", 28),

	/* AUD1 */
	GATE_AUD1(CLK_AUD_I2S1_BCLK, "aud_i2s1_bclk", "top_audio", 4),
	GATE_AUD1(CLK_AUD_I2S2_BCLK, "aud_i2s2_bclk", "top_audio", 5),
	GATE_AUD1(CLK_AUD_I2S3_BCLK, "aud_i2s3_bclk", "top_audio", 6),
	GATE_AUD1(CLK_AUD_I2S4_BCLK, "aud_i2s4_bclk", "top_audio", 7),
	GATE_AUD1(CLK_AUD_CONNSYS_I2S_ASRC, "aud_connsys_i2s_asrc", "top_audio", 12),
	GATE_AUD1(CLK_AUD_GENERAL1_ASRC, "aud_general1_asrc", "top_audio", 13),
	GATE_AUD1(CLK_AUD_GENERAL2_ASRC, "aud_general2_asrc", "top_audio", 14),
	GATE_AUD1(CLK_AUD_DAC_HIRES, "aud_dac_hires_clk", "top_audio_h", 15),
	GATE_AUD1(CLK_AUD_ADC_HIRES, "aud_adc_hires_clk", "top_audio_h", 16),
	GATE_AUD1(CLK_AUD_ADC_HIRES_TML, "aud_adc_hires_tml", "top_audio_h", 17),
	GATE_AUD1(CLK_AUD_ADDA6_ADC, "aud_adda6_adc", "top_audio", 20),
	GATE_AUD1(CLK_AUD_ADDA6_ADC_HIRES, "aud_adda6_adc_hires", "top_audio_h", 21),
	GATE_AUD1(CLK_AUD_3RD_DAC, "aud_3rd_dac", "top_audio", 28),
	GATE_AUD1(CLK_AUD_3RD_DAC_PREDIS, "aud_3rd_dac_predis", "top_audio", 29),
	GATE_AUD1(CLK_AUD_3RD_DAC_TML, "aud_3rd_dac_tml", "top_audio", 30),
	GATE_AUD1(CLK_AUD_3RD_DAC_HIRES, "aud_3rd_dac_hires", "top_audio_h", 31),

	/* AUD2 */
	GATE_AUD2(CLK_AUD_ETDM_IN1_BCLK, "aud_etdm_in1_bclk", "top_audio", 23),
	GATE_AUD2(CLK_AUD_ETDM_OUT1_BCLK, "aud_etdm_out1_bclk", "top_audio", 24),
};

static void mt8186_audsys_clk_unregister(void *data)
{
	struct mtk_base_afe *afe = data;
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct clk *clk;
	struct clk_lookup *cl;
	int i;

	if (!afe_priv)
		return;

	for (i = 0; i < CLK_AUD_NR_CLK; i++) {
		cl = afe_priv->lookup[i];
		if (!cl)
			continue;

		clk = cl->clk;
		clk_unregister_gate(clk);

		clkdev_drop(cl);
	}
}

int mt8186_audsys_clk_register(struct mtk_base_afe *afe)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct clk *clk;
	struct clk_lookup *cl;
	int i;

	afe_priv->lookup = devm_kcalloc(afe->dev, CLK_AUD_NR_CLK,
					sizeof(*afe_priv->lookup),
					GFP_KERNEL);

	if (!afe_priv->lookup)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(aud_clks); i++) {
		const struct afe_gate *gate = &aud_clks[i];

		clk = clk_register_gate(afe->dev, gate->name, gate->parent_name,
					gate->flags, afe->base_addr + gate->reg,
					gate->bit, gate->cg_flags, NULL);

		if (IS_ERR(clk)) {
			dev_err(afe->dev, "Failed to register clk %s: %ld\n",
				gate->name, PTR_ERR(clk));
			continue;
		}

		/* add clk_lookup for devm_clk_get(SND_SOC_DAPM_CLOCK_SUPPLY) */
		cl = kzalloc(sizeof(*cl), GFP_KERNEL);
		if (!cl)
			return -ENOMEM;

		cl->clk = clk;
		cl->con_id = gate->name;
		cl->dev_id = dev_name(afe->dev);
		clkdev_add(cl);

		afe_priv->lookup[i] = cl;
	}

	return devm_add_action_or_reset(afe->dev, mt8186_audsys_clk_unregister, afe);
}

