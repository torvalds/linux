// SPDX-License-Identifier: GPL-2.0
/*
 * mt8188-audsys-clk.c  --  MediaTek 8188 audsys clock control
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Chun-Chia Chiu <chun-chia.chiu@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include "mt8188-afe-common.h"
#include "mt8188-audsys-clk.h"
#include "mt8188-audsys-clkid.h"
#include "mt8188-reg.h"

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

#define GATE_AUD3(_id, _name, _parent, _bit)			\
	GATE_AFE(_id, _name, _parent, AUDIO_TOP_CON3, _bit)

#define GATE_AUD4(_id, _name, _parent, _bit)			\
	GATE_AFE(_id, _name, _parent, AUDIO_TOP_CON4, _bit)

#define GATE_AUD5(_id, _name, _parent, _bit)			\
	GATE_AFE(_id, _name, _parent, AUDIO_TOP_CON5, _bit)

#define GATE_AUD6(_id, _name, _parent, _bit)			\
	GATE_AFE(_id, _name, _parent, AUDIO_TOP_CON6, _bit)

static const struct afe_gate aud_clks[CLK_AUD_NR_CLK] = {
	/* AUD0 */
	GATE_AUD0(CLK_AUD_AFE, "aud_afe", "top_a1sys_hp", 2),
	GATE_AUD0(CLK_AUD_LRCK_CNT, "aud_lrck_cnt", "top_a1sys_hp", 4),
	GATE_AUD0(CLK_AUD_SPDIFIN_TUNER_APLL, "aud_spdifin_tuner_apll", "top_apll4", 10),
	GATE_AUD0(CLK_AUD_SPDIFIN_TUNER_DBG, "aud_spdifin_tuner_dbg", "top_apll4", 11),
	GATE_AUD0(CLK_AUD_UL_TML, "aud_ul_tml", "top_a1sys_hp", 18),
	GATE_AUD0(CLK_AUD_APLL1_TUNER, "aud_apll1_tuner", "top_apll1", 19),
	GATE_AUD0(CLK_AUD_APLL2_TUNER, "aud_apll2_tuner", "top_apll2", 20),
	GATE_AUD0(CLK_AUD_TOP0_SPDF, "aud_top0_spdf", "top_aud_iec_clk", 21),
	GATE_AUD0(CLK_AUD_APLL, "aud_apll", "top_apll1", 23),
	GATE_AUD0(CLK_AUD_APLL2, "aud_apll2", "top_apll2", 24),
	GATE_AUD0(CLK_AUD_DAC, "aud_dac", "top_a1sys_hp", 25),
	GATE_AUD0(CLK_AUD_DAC_PREDIS, "aud_dac_predis", "top_a1sys_hp", 26),
	GATE_AUD0(CLK_AUD_TML, "aud_tml", "top_a1sys_hp", 27),
	GATE_AUD0(CLK_AUD_ADC, "aud_adc", "top_a1sys_hp", 28),
	GATE_AUD0(CLK_AUD_DAC_HIRES, "aud_dac_hires", "top_audio_h", 31),

	/* AUD1 */
	GATE_AUD1(CLK_AUD_A1SYS_HP, "aud_a1sys_hp", "top_a1sys_hp", 2),
	GATE_AUD1(CLK_AUD_AFE_DMIC1, "aud_afe_dmic1", "top_a1sys_hp", 10),
	GATE_AUD1(CLK_AUD_AFE_DMIC2, "aud_afe_dmic2", "top_a1sys_hp", 11),
	GATE_AUD1(CLK_AUD_AFE_DMIC3, "aud_afe_dmic3", "top_a1sys_hp", 12),
	GATE_AUD1(CLK_AUD_AFE_DMIC4, "aud_afe_dmic4", "top_a1sys_hp", 13),
	GATE_AUD1(CLK_AUD_AFE_26M_DMIC_TM, "aud_afe_26m_dmic_tm", "top_a1sys_hp", 14),
	GATE_AUD1(CLK_AUD_UL_TML_HIRES, "aud_ul_tml_hires", "top_audio_h", 16),
	GATE_AUD1(CLK_AUD_ADC_HIRES, "aud_adc_hires", "top_audio_h", 17),

	/* AUD3 */
	GATE_AUD3(CLK_AUD_LINEIN_TUNER, "aud_linein_tuner", "top_apll5", 5),
	GATE_AUD3(CLK_AUD_EARC_TUNER, "aud_earc_tuner", "top_apll3", 7),

	/* AUD4 */
	GATE_AUD4(CLK_AUD_I2SIN, "aud_i2sin", "top_a1sys_hp", 0),
	GATE_AUD4(CLK_AUD_TDM_IN, "aud_tdm_in", "top_a1sys_hp", 1),
	GATE_AUD4(CLK_AUD_I2S_OUT, "aud_i2s_out", "top_a1sys_hp", 6),
	GATE_AUD4(CLK_AUD_TDM_OUT, "aud_tdm_out", "top_a1sys_hp", 7),
	GATE_AUD4(CLK_AUD_HDMI_OUT, "aud_hdmi_out", "top_a1sys_hp", 8),
	GATE_AUD4(CLK_AUD_ASRC11, "aud_asrc11", "top_a1sys_hp", 16),
	GATE_AUD4(CLK_AUD_ASRC12, "aud_asrc12", "top_a1sys_hp", 17),
	GATE_AUD4(CLK_AUD_MULTI_IN, "aud_multi_in", "mphone_slave_b", 19),
	GATE_AUD4(CLK_AUD_INTDIR, "aud_intdir", "top_intdir", 20),
	GATE_AUD4(CLK_AUD_A1SYS, "aud_a1sys", "top_a1sys_hp", 21),
	GATE_AUD4(CLK_AUD_A2SYS, "aud_a2sys", "top_a2sys", 22),
	GATE_AUD4(CLK_AUD_PCMIF, "aud_pcmif", "top_a1sys_hp", 24),
	GATE_AUD4(CLK_AUD_A3SYS, "aud_a3sys", "top_a3sys", 30),
	GATE_AUD4(CLK_AUD_A4SYS, "aud_a4sys", "top_a4sys", 31),

	/* AUD5 */
	GATE_AUD5(CLK_AUD_MEMIF_UL1, "aud_memif_ul1", "top_a1sys_hp", 0),
	GATE_AUD5(CLK_AUD_MEMIF_UL2, "aud_memif_ul2", "top_a1sys_hp", 1),
	GATE_AUD5(CLK_AUD_MEMIF_UL3, "aud_memif_ul3", "top_a1sys_hp", 2),
	GATE_AUD5(CLK_AUD_MEMIF_UL4, "aud_memif_ul4", "top_a1sys_hp", 3),
	GATE_AUD5(CLK_AUD_MEMIF_UL5, "aud_memif_ul5", "top_a1sys_hp", 4),
	GATE_AUD5(CLK_AUD_MEMIF_UL6, "aud_memif_ul6", "top_a1sys_hp", 5),
	GATE_AUD5(CLK_AUD_MEMIF_UL8, "aud_memif_ul8", "top_a1sys_hp", 7),
	GATE_AUD5(CLK_AUD_MEMIF_UL9, "aud_memif_ul9", "top_a1sys_hp", 8),
	GATE_AUD5(CLK_AUD_MEMIF_UL10, "aud_memif_ul10", "top_a1sys_hp", 9),
	GATE_AUD5(CLK_AUD_MEMIF_DL2, "aud_memif_dl2", "top_a1sys_hp", 18),
	GATE_AUD5(CLK_AUD_MEMIF_DL3, "aud_memif_dl3", "top_a1sys_hp", 19),
	GATE_AUD5(CLK_AUD_MEMIF_DL6, "aud_memif_dl6", "top_a1sys_hp", 22),
	GATE_AUD5(CLK_AUD_MEMIF_DL7, "aud_memif_dl7", "top_a1sys_hp", 23),
	GATE_AUD5(CLK_AUD_MEMIF_DL8, "aud_memif_dl8", "top_a1sys_hp", 24),
	GATE_AUD5(CLK_AUD_MEMIF_DL10, "aud_memif_dl10", "top_a1sys_hp", 26),
	GATE_AUD5(CLK_AUD_MEMIF_DL11, "aud_memif_dl11", "top_a1sys_hp", 27),

	/* AUD6 */
	GATE_AUD6(CLK_AUD_GASRC0, "aud_gasrc0", "top_asm_h", 0),
	GATE_AUD6(CLK_AUD_GASRC1, "aud_gasrc1", "top_asm_h", 1),
	GATE_AUD6(CLK_AUD_GASRC2, "aud_gasrc2", "top_asm_h", 2),
	GATE_AUD6(CLK_AUD_GASRC3, "aud_gasrc3", "top_asm_h", 3),
	GATE_AUD6(CLK_AUD_GASRC4, "aud_gasrc4", "top_asm_h", 4),
	GATE_AUD6(CLK_AUD_GASRC5, "aud_gasrc5", "top_asm_h", 5),
	GATE_AUD6(CLK_AUD_GASRC6, "aud_gasrc6", "top_asm_h", 6),
	GATE_AUD6(CLK_AUD_GASRC7, "aud_gasrc7", "top_asm_h", 7),
	GATE_AUD6(CLK_AUD_GASRC8, "aud_gasrc8", "top_asm_h", 8),
	GATE_AUD6(CLK_AUD_GASRC9, "aud_gasrc9", "top_asm_h", 9),
	GATE_AUD6(CLK_AUD_GASRC10, "aud_gasrc10", "top_asm_h", 10),
	GATE_AUD6(CLK_AUD_GASRC11, "aud_gasrc11", "top_asm_h", 11),
};

static void mt8188_audsys_clk_unregister(void *data)
{
	struct mtk_base_afe *afe = data;
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
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

int mt8188_audsys_clk_register(struct mtk_base_afe *afe)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
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
		cl->clk_hw = NULL;
		clkdev_add(cl);

		afe_priv->lookup[i] = cl;
	}

	return devm_add_action_or_reset(afe->dev, mt8188_audsys_clk_unregister, afe);
}
