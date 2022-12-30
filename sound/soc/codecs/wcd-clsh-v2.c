// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
// Copyright (c) 2017-2018, Linaro Limited

#include <linux/slab.h>
#include <sound/soc.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include "wcd9335.h"
#include "wcd-clsh-v2.h"

struct wcd_clsh_ctrl {
	int state;
	int mode;
	int flyback_users;
	int buck_users;
	int clsh_users;
	int codec_version;
	struct snd_soc_component *comp;
};

/* Class-H registers for codecs from and above WCD9335 */
#define WCD9XXX_A_CDC_RX0_RX_PATH_CFG0			WCD9335_REG(0xB, 0x42)
#define WCD9XXX_A_CDC_RX_PATH_CLSH_EN_MASK		BIT(6)
#define WCD9XXX_A_CDC_RX_PATH_CLSH_ENABLE		BIT(6)
#define WCD9XXX_A_CDC_RX_PATH_CLSH_DISABLE		0
#define WCD9XXX_A_CDC_RX1_RX_PATH_CFG0			WCD9335_REG(0xB, 0x56)
#define WCD9XXX_A_CDC_RX2_RX_PATH_CFG0			WCD9335_REG(0xB, 0x6A)
#define WCD9XXX_A_CDC_CLSH_K1_MSB			WCD9335_REG(0xC, 0x08)
#define WCD9XXX_A_CDC_CLSH_K1_MSB_COEF_MASK		GENMASK(3, 0)
#define WCD9XXX_A_CDC_CLSH_K1_LSB			WCD9335_REG(0xC, 0x09)
#define WCD9XXX_A_CDC_CLSH_K1_LSB_COEF_MASK		GENMASK(7, 0)
#define WCD9XXX_A_ANA_RX_SUPPLIES			WCD9335_REG(0x6, 0x08)
#define WCD9XXX_A_ANA_RX_REGULATOR_MODE_MASK		BIT(1)
#define WCD9XXX_A_ANA_RX_REGULATOR_MODE_CLS_H		0
#define WCD9XXX_A_ANA_RX_REGULATOR_MODE_CLS_AB		BIT(1)
#define WCD9XXX_A_ANA_RX_VNEG_PWR_LVL_MASK		BIT(2)
#define WCD9XXX_A_ANA_RX_VNEG_PWR_LVL_UHQA		BIT(2)
#define WCD9XXX_A_ANA_RX_VNEG_PWR_LVL_DEFAULT		0
#define WCD9XXX_A_ANA_RX_VPOS_PWR_LVL_MASK		BIT(3)
#define WCD9XXX_A_ANA_RX_VPOS_PWR_LVL_UHQA		BIT(3)
#define WCD9XXX_A_ANA_RX_VPOS_PWR_LVL_DEFAULT		0
#define WCD9XXX_A_ANA_RX_VNEG_EN_MASK			BIT(6)
#define WCD9XXX_A_ANA_RX_VNEG_EN_SHIFT			6
#define WCD9XXX_A_ANA_RX_VNEG_ENABLE			BIT(6)
#define WCD9XXX_A_ANA_RX_VNEG_DISABLE			0
#define WCD9XXX_A_ANA_RX_VPOS_EN_MASK			BIT(7)
#define WCD9XXX_A_ANA_RX_VPOS_EN_SHIFT			7
#define WCD9XXX_A_ANA_RX_VPOS_ENABLE			BIT(7)
#define WCD9XXX_A_ANA_RX_VPOS_DISABLE			0
#define WCD9XXX_A_ANA_HPH				WCD9335_REG(0x6, 0x09)
#define WCD9XXX_A_ANA_HPH_PWR_LEVEL_MASK		GENMASK(3, 2)
#define WCD9XXX_A_ANA_HPH_PWR_LEVEL_UHQA		0x08
#define WCD9XXX_A_ANA_HPH_PWR_LEVEL_LP			0x04
#define WCD9XXX_A_ANA_HPH_PWR_LEVEL_NORMAL		0x0
#define WCD9XXX_A_CDC_CLSH_CRC				WCD9335_REG(0xC, 0x01)
#define WCD9XXX_A_CDC_CLSH_CRC_CLK_EN_MASK		BIT(0)
#define WCD9XXX_A_CDC_CLSH_CRC_CLK_ENABLE		BIT(0)
#define WCD9XXX_A_CDC_CLSH_CRC_CLK_DISABLE		0
#define WCD9XXX_FLYBACK_EN				WCD9335_REG(0x6, 0xA4)
#define WCD9XXX_FLYBACK_EN_DELAY_SEL_MASK		GENMASK(6, 5)
#define WCD9XXX_FLYBACK_EN_DELAY_26P25_US		0x40
#define WCD9XXX_FLYBACK_EN_RESET_BY_EXT_MASK		BIT(4)
#define WCD9XXX_FLYBACK_EN_PWDN_WITHOUT_DELAY		BIT(4)
#define WCD9XXX_FLYBACK_EN_PWDN_WITH_DELAY			0
#define WCD9XXX_RX_BIAS_FLYB_BUFF			WCD9335_REG(0x6, 0xC7)
#define WCD9XXX_RX_BIAS_FLYB_VNEG_5_UA_MASK		GENMASK(7, 4)
#define WCD9XXX_RX_BIAS_FLYB_VPOS_5_UA_MASK		GENMASK(3, 0)
#define WCD9XXX_HPH_L_EN				WCD9335_REG(0x6, 0xD3)
#define WCD9XXX_HPH_CONST_SEL_L_MASK			GENMASK(7, 3)
#define WCD9XXX_HPH_CONST_SEL_BYPASS			0
#define WCD9XXX_HPH_CONST_SEL_LP_PATH			0x40
#define WCD9XXX_HPH_CONST_SEL_HQ_PATH			0x80
#define WCD9XXX_HPH_R_EN				WCD9335_REG(0x6, 0xD6)
#define WCD9XXX_HPH_REFBUFF_UHQA_CTL			WCD9335_REG(0x6, 0xDD)
#define WCD9XXX_HPH_REFBUFF_UHQA_GAIN_MASK		GENMASK(2, 0)
#define WCD9XXX_CLASSH_CTRL_VCL_2                       WCD9335_REG(0x6, 0x9B)
#define WCD9XXX_CLASSH_CTRL_VCL_2_VREF_FILT_1_MASK	GENMASK(5, 4)
#define WCD9XXX_CLASSH_CTRL_VCL_VREF_FILT_R_50KOHM	0x20
#define WCD9XXX_CLASSH_CTRL_VCL_VREF_FILT_R_0KOHM	0x0
#define WCD9XXX_CDC_RX1_RX_PATH_CTL			WCD9335_REG(0xB, 0x55)
#define WCD9XXX_CDC_RX2_RX_PATH_CTL			WCD9335_REG(0xB, 0x69)
#define WCD9XXX_CDC_CLK_RST_CTRL_MCLK_CONTROL		WCD9335_REG(0xD, 0x41)
#define WCD9XXX_CDC_CLK_RST_CTRL_MCLK_EN_MASK		BIT(0)
#define WCD9XXX_CDC_CLK_RST_CTRL_MCLK_11P3_EN_MASK	BIT(1)
#define WCD9XXX_CLASSH_CTRL_CCL_1                       WCD9335_REG(0x6, 0x9C)
#define WCD9XXX_CLASSH_CTRL_CCL_1_DELTA_IPEAK_MASK	GENMASK(7, 4)
#define WCD9XXX_CLASSH_CTRL_CCL_1_DELTA_IPEAK_50MA	0x50
#define WCD9XXX_CLASSH_CTRL_CCL_1_DELTA_IPEAK_30MA	0x30

#define WCD9XXX_BASE_ADDRESS				0x3000
#define WCD9XXX_ANA_RX_SUPPLIES				(WCD9XXX_BASE_ADDRESS+0x008)
#define WCD9XXX_ANA_HPH					(WCD9XXX_BASE_ADDRESS+0x009)
#define WCD9XXX_CLASSH_MODE_2				(WCD9XXX_BASE_ADDRESS+0x098)
#define WCD9XXX_CLASSH_MODE_3				(WCD9XXX_BASE_ADDRESS+0x099)
#define WCD9XXX_FLYBACK_VNEG_CTRL_1			(WCD9XXX_BASE_ADDRESS+0x0A5)
#define WCD9XXX_FLYBACK_VNEG_CTRL_4			(WCD9XXX_BASE_ADDRESS+0x0A8)
#define WCD9XXX_FLYBACK_VNEGDAC_CTRL_2			(WCD9XXX_BASE_ADDRESS+0x0AF)
#define WCD9XXX_RX_BIAS_HPH_LOWPOWER			(WCD9XXX_BASE_ADDRESS+0x0BF)
#define WCD9XXX_V3_RX_BIAS_FLYB_BUFF			(WCD9XXX_BASE_ADDRESS+0x0C7)
#define WCD9XXX_HPH_PA_CTL1				(WCD9XXX_BASE_ADDRESS+0x0D1)
#define WCD9XXX_HPH_NEW_INT_PA_MISC2			(WCD9XXX_BASE_ADDRESS+0x138)

#define CLSH_REQ_ENABLE		true
#define CLSH_REQ_DISABLE	false
#define WCD_USLEEP_RANGE	50

enum {
	DAC_GAIN_0DB = 0,
	DAC_GAIN_0P2DB,
	DAC_GAIN_0P4DB,
	DAC_GAIN_0P6DB,
	DAC_GAIN_0P8DB,
	DAC_GAIN_M0P2DB,
	DAC_GAIN_M0P4DB,
	DAC_GAIN_M0P6DB,
};

static inline void wcd_enable_clsh_block(struct wcd_clsh_ctrl *ctrl,
					 bool enable)
{
	struct snd_soc_component *comp = ctrl->comp;

	if ((enable && ++ctrl->clsh_users == 1) ||
	    (!enable && --ctrl->clsh_users == 0))
		snd_soc_component_update_bits(comp, WCD9XXX_A_CDC_CLSH_CRC,
				      WCD9XXX_A_CDC_CLSH_CRC_CLK_EN_MASK,
				      enable);
	if (ctrl->clsh_users < 0)
		ctrl->clsh_users = 0;
}

static inline void wcd_clsh_set_buck_mode(struct snd_soc_component *comp,
					  int mode)
{
	/* set to HIFI */
	if (mode == CLS_H_HIFI)
		snd_soc_component_update_bits(comp, WCD9XXX_A_ANA_RX_SUPPLIES,
					WCD9XXX_A_ANA_RX_VPOS_PWR_LVL_MASK,
					WCD9XXX_A_ANA_RX_VPOS_PWR_LVL_UHQA);
	else
		snd_soc_component_update_bits(comp, WCD9XXX_A_ANA_RX_SUPPLIES,
					WCD9XXX_A_ANA_RX_VPOS_PWR_LVL_MASK,
					WCD9XXX_A_ANA_RX_VPOS_PWR_LVL_DEFAULT);
}

static void wcd_clsh_v3_set_buck_mode(struct snd_soc_component *component,
					  int mode)
{
	if (mode == CLS_H_HIFI || mode == CLS_H_LOHIFI ||
	    mode == CLS_AB_HIFI || mode == CLS_AB_LOHIFI)
		snd_soc_component_update_bits(component,
				WCD9XXX_ANA_RX_SUPPLIES,
				0x08, 0x08); /* set to HIFI */
	else
		snd_soc_component_update_bits(component,
				WCD9XXX_ANA_RX_SUPPLIES,
				0x08, 0x00); /* set to default */
}

static inline void wcd_clsh_set_flyback_mode(struct snd_soc_component *comp,
					     int mode)
{
	/* set to HIFI */
	if (mode == CLS_H_HIFI)
		snd_soc_component_update_bits(comp, WCD9XXX_A_ANA_RX_SUPPLIES,
					WCD9XXX_A_ANA_RX_VNEG_PWR_LVL_MASK,
					WCD9XXX_A_ANA_RX_VNEG_PWR_LVL_UHQA);
	else
		snd_soc_component_update_bits(comp, WCD9XXX_A_ANA_RX_SUPPLIES,
					WCD9XXX_A_ANA_RX_VNEG_PWR_LVL_MASK,
					WCD9XXX_A_ANA_RX_VNEG_PWR_LVL_DEFAULT);
}

static void wcd_clsh_buck_ctrl(struct wcd_clsh_ctrl *ctrl,
			       int mode,
			       bool enable)
{
	struct snd_soc_component *comp = ctrl->comp;

	/* enable/disable buck */
	if ((enable && (++ctrl->buck_users == 1)) ||
	   (!enable && (--ctrl->buck_users == 0)))
		snd_soc_component_update_bits(comp, WCD9XXX_A_ANA_RX_SUPPLIES,
				WCD9XXX_A_ANA_RX_VPOS_EN_MASK,
				enable << WCD9XXX_A_ANA_RX_VPOS_EN_SHIFT);
	/*
	 * 500us sleep is required after buck enable/disable
	 * as per HW requirement
	 */
	usleep_range(500, 500 + WCD_USLEEP_RANGE);
}

static void wcd_clsh_v3_buck_ctrl(struct snd_soc_component *component,
			       struct wcd_clsh_ctrl *ctrl,
			       int mode,
			       bool enable)
{
	/* enable/disable buck */
	if ((enable && (++ctrl->buck_users == 1)) ||
	   (!enable && (--ctrl->buck_users == 0))) {
		snd_soc_component_update_bits(component,
				WCD9XXX_ANA_RX_SUPPLIES,
				(1 << 7), (enable << 7));
		/*
		 * 500us sleep is required after buck enable/disable
		 * as per HW requirement
		 */
		usleep_range(500, 510);
		if (mode == CLS_H_LOHIFI || mode == CLS_H_ULP ||
			mode == CLS_H_HIFI || mode == CLS_H_LP)
			snd_soc_component_update_bits(component,
					WCD9XXX_CLASSH_MODE_3,
					0x02, 0x00);

		snd_soc_component_update_bits(component,
					WCD9XXX_CLASSH_MODE_2,
					0xFF, 0x3A);
		/* 500usec delay is needed as per HW requirement */
		usleep_range(500, 500 + WCD_USLEEP_RANGE);
	}
}

static void wcd_clsh_flyback_ctrl(struct wcd_clsh_ctrl *ctrl,
				  int mode,
				  bool enable)
{
	struct snd_soc_component *comp = ctrl->comp;

	/* enable/disable flyback */
	if ((enable && (++ctrl->flyback_users == 1)) ||
	   (!enable && (--ctrl->flyback_users == 0))) {
		snd_soc_component_update_bits(comp, WCD9XXX_A_ANA_RX_SUPPLIES,
				WCD9XXX_A_ANA_RX_VNEG_EN_MASK,
				enable << WCD9XXX_A_ANA_RX_VNEG_EN_SHIFT);
		/* 100usec delay is needed as per HW requirement */
		usleep_range(100, 110);
	}
	/*
	 * 500us sleep is required after flyback enable/disable
	 * as per HW requirement
	 */
	usleep_range(500, 500 + WCD_USLEEP_RANGE);
}

static void wcd_clsh_set_gain_path(struct wcd_clsh_ctrl *ctrl, int mode)
{
	struct snd_soc_component *comp = ctrl->comp;
	int val = 0;

	switch (mode) {
	case CLS_H_NORMAL:
	case CLS_AB:
		val = WCD9XXX_HPH_CONST_SEL_BYPASS;
		break;
	case CLS_H_HIFI:
		val = WCD9XXX_HPH_CONST_SEL_HQ_PATH;
		break;
	case CLS_H_LP:
		val = WCD9XXX_HPH_CONST_SEL_LP_PATH;
		break;
	}

	snd_soc_component_update_bits(comp, WCD9XXX_HPH_L_EN,
					WCD9XXX_HPH_CONST_SEL_L_MASK,
					val);

	snd_soc_component_update_bits(comp, WCD9XXX_HPH_R_EN,
					WCD9XXX_HPH_CONST_SEL_L_MASK,
					val);
}

static void wcd_clsh_v2_set_hph_mode(struct snd_soc_component *comp, int mode)
{
	int val = 0, gain = 0, res_val;
	int ipeak = WCD9XXX_CLASSH_CTRL_CCL_1_DELTA_IPEAK_50MA;

	res_val = WCD9XXX_CLASSH_CTRL_VCL_VREF_FILT_R_0KOHM;
	switch (mode) {
	case CLS_H_NORMAL:
		res_val = WCD9XXX_CLASSH_CTRL_VCL_VREF_FILT_R_50KOHM;
		val = WCD9XXX_A_ANA_HPH_PWR_LEVEL_NORMAL;
		gain = DAC_GAIN_0DB;
		ipeak = WCD9XXX_CLASSH_CTRL_CCL_1_DELTA_IPEAK_50MA;
		break;
	case CLS_AB:
		val = WCD9XXX_A_ANA_HPH_PWR_LEVEL_NORMAL;
		gain = DAC_GAIN_0DB;
		ipeak = WCD9XXX_CLASSH_CTRL_CCL_1_DELTA_IPEAK_50MA;
		break;
	case CLS_H_HIFI:
		val = WCD9XXX_A_ANA_HPH_PWR_LEVEL_UHQA;
		gain = DAC_GAIN_M0P2DB;
		ipeak = WCD9XXX_CLASSH_CTRL_CCL_1_DELTA_IPEAK_50MA;
		break;
	case CLS_H_LP:
		val = WCD9XXX_A_ANA_HPH_PWR_LEVEL_LP;
		ipeak = WCD9XXX_CLASSH_CTRL_CCL_1_DELTA_IPEAK_30MA;
		break;
	}

	snd_soc_component_update_bits(comp, WCD9XXX_A_ANA_HPH,
					WCD9XXX_A_ANA_HPH_PWR_LEVEL_MASK, val);
	snd_soc_component_update_bits(comp, WCD9XXX_CLASSH_CTRL_VCL_2,
				WCD9XXX_CLASSH_CTRL_VCL_2_VREF_FILT_1_MASK,
				res_val);
	if (mode != CLS_H_LP)
		snd_soc_component_update_bits(comp,
					WCD9XXX_HPH_REFBUFF_UHQA_CTL,
					WCD9XXX_HPH_REFBUFF_UHQA_GAIN_MASK,
					gain);
	snd_soc_component_update_bits(comp, WCD9XXX_CLASSH_CTRL_CCL_1,
				WCD9XXX_CLASSH_CTRL_CCL_1_DELTA_IPEAK_MASK,
				ipeak);
}

static void wcd_clsh_v3_set_hph_mode(struct snd_soc_component *component,
				  int mode)
{
	u8 val;

	switch (mode) {
	case CLS_H_NORMAL:
		val = 0x00;
		break;
	case CLS_AB:
	case CLS_H_ULP:
		val = 0x0C;
		break;
	case CLS_AB_HIFI:
	case CLS_H_HIFI:
		val = 0x08;
		break;
	case CLS_H_LP:
	case CLS_H_LOHIFI:
	case CLS_AB_LP:
	case CLS_AB_LOHIFI:
		val = 0x04;
		break;
	default:
		dev_err(component->dev, "%s:Invalid mode %d\n", __func__, mode);
		return;
	}

	snd_soc_component_update_bits(component, WCD9XXX_ANA_HPH, 0x0C, val);
}

void wcd_clsh_set_hph_mode(struct wcd_clsh_ctrl *ctrl, int mode)
{
	struct snd_soc_component *comp = ctrl->comp;

	if (ctrl->codec_version >= WCD937X)
		wcd_clsh_v3_set_hph_mode(comp, mode);
	else
		wcd_clsh_v2_set_hph_mode(comp, mode);

}

static void wcd_clsh_set_flyback_current(struct snd_soc_component *comp,
					 int mode)
{

	snd_soc_component_update_bits(comp, WCD9XXX_RX_BIAS_FLYB_BUFF,
				WCD9XXX_RX_BIAS_FLYB_VPOS_5_UA_MASK, 0x0A);
	snd_soc_component_update_bits(comp, WCD9XXX_RX_BIAS_FLYB_BUFF,
				WCD9XXX_RX_BIAS_FLYB_VNEG_5_UA_MASK, 0x0A);
	/* Sleep needed to avoid click and pop as per HW requirement */
	usleep_range(100, 110);
}

static void wcd_clsh_set_buck_regulator_mode(struct snd_soc_component *comp,
					     int mode)
{
	if (mode == CLS_AB)
		snd_soc_component_update_bits(comp, WCD9XXX_A_ANA_RX_SUPPLIES,
					WCD9XXX_A_ANA_RX_REGULATOR_MODE_MASK,
					WCD9XXX_A_ANA_RX_REGULATOR_MODE_CLS_AB);
	else
		snd_soc_component_update_bits(comp, WCD9XXX_A_ANA_RX_SUPPLIES,
					WCD9XXX_A_ANA_RX_REGULATOR_MODE_MASK,
					WCD9XXX_A_ANA_RX_REGULATOR_MODE_CLS_H);
}

static void wcd_clsh_v3_set_buck_regulator_mode(struct snd_soc_component *component,
						int mode)
{
	snd_soc_component_update_bits(component, WCD9XXX_ANA_RX_SUPPLIES,
			    0x02, 0x00);
}

static void wcd_clsh_v3_set_flyback_mode(struct snd_soc_component *component,
						int mode)
{
	if (mode == CLS_H_HIFI || mode == CLS_H_LOHIFI ||
	    mode == CLS_AB_HIFI || mode == CLS_AB_LOHIFI) {
		snd_soc_component_update_bits(component,
				WCD9XXX_ANA_RX_SUPPLIES,
				0x04, 0x04);
		snd_soc_component_update_bits(component,
				WCD9XXX_FLYBACK_VNEG_CTRL_4,
				0xF0, 0x80);
	} else {
		snd_soc_component_update_bits(component,
				WCD9XXX_ANA_RX_SUPPLIES,
				0x04, 0x00); /* set to Default */
		snd_soc_component_update_bits(component,
				WCD9XXX_FLYBACK_VNEG_CTRL_4,
				0xF0, 0x70);
	}
}

static void wcd_clsh_v3_force_iq_ctl(struct snd_soc_component *component,
					 int mode, bool enable)
{
	if (enable) {
		snd_soc_component_update_bits(component,
				WCD9XXX_FLYBACK_VNEGDAC_CTRL_2,
				0xE0, 0xA0);
		/* 100usec delay is needed as per HW requirement */
		usleep_range(100, 110);
		snd_soc_component_update_bits(component,
				WCD9XXX_CLASSH_MODE_3,
				0x02, 0x02);
		snd_soc_component_update_bits(component,
				WCD9XXX_CLASSH_MODE_2,
				0xFF, 0x1C);
		if (mode == CLS_H_LOHIFI || mode == CLS_AB_LOHIFI) {
			snd_soc_component_update_bits(component,
					WCD9XXX_HPH_NEW_INT_PA_MISC2,
					0x20, 0x20);
			snd_soc_component_update_bits(component,
					WCD9XXX_RX_BIAS_HPH_LOWPOWER,
					0xF0, 0xC0);
			snd_soc_component_update_bits(component,
					WCD9XXX_HPH_PA_CTL1,
					0x0E, 0x02);
		}
	} else {
		snd_soc_component_update_bits(component,
				WCD9XXX_HPH_NEW_INT_PA_MISC2,
				0x20, 0x00);
		snd_soc_component_update_bits(component,
				WCD9XXX_RX_BIAS_HPH_LOWPOWER,
				0xF0, 0x80);
		snd_soc_component_update_bits(component,
				WCD9XXX_HPH_PA_CTL1,
				0x0E, 0x06);
	}
}

static void wcd_clsh_v3_flyback_ctrl(struct snd_soc_component *component,
				  struct wcd_clsh_ctrl *ctrl,
				  int mode,
				  bool enable)
{
	/* enable/disable flyback */
	if ((enable && (++ctrl->flyback_users == 1)) ||
	   (!enable && (--ctrl->flyback_users == 0))) {
		snd_soc_component_update_bits(component,
				WCD9XXX_FLYBACK_VNEG_CTRL_1,
				0xE0, 0xE0);
		snd_soc_component_update_bits(component,
				WCD9XXX_ANA_RX_SUPPLIES,
				(1 << 6), (enable << 6));
		/*
		 * 100us sleep is required after flyback enable/disable
		 * as per HW requirement
		 */
		usleep_range(100, 110);
		snd_soc_component_update_bits(component,
				WCD9XXX_FLYBACK_VNEGDAC_CTRL_2,
				0xE0, 0xE0);
		/* 500usec delay is needed as per HW requirement */
		usleep_range(500, 500 + WCD_USLEEP_RANGE);
	}
}

static void wcd_clsh_v3_set_flyback_current(struct snd_soc_component *component,
				int mode)
{
	snd_soc_component_update_bits(component, WCD9XXX_V3_RX_BIAS_FLYB_BUFF,
				0x0F, 0x0A);
	snd_soc_component_update_bits(component, WCD9XXX_V3_RX_BIAS_FLYB_BUFF,
				0xF0, 0xA0);
	/* Sleep needed to avoid click and pop as per HW requirement */
	usleep_range(100, 110);
}

static void wcd_clsh_v3_state_aux(struct wcd_clsh_ctrl *ctrl, int req_state,
			      bool is_enable, int mode)
{
	struct snd_soc_component *component = ctrl->comp;

	if (is_enable) {
		wcd_clsh_v3_set_buck_mode(component, mode);
		wcd_clsh_v3_set_flyback_mode(component, mode);
		wcd_clsh_v3_flyback_ctrl(component, ctrl, mode, true);
		wcd_clsh_v3_set_flyback_current(component, mode);
		wcd_clsh_v3_buck_ctrl(component, ctrl, mode, true);
	} else {
		wcd_clsh_v3_buck_ctrl(component, ctrl, mode, false);
		wcd_clsh_v3_flyback_ctrl(component, ctrl, mode, false);
		wcd_clsh_v3_set_flyback_mode(component, CLS_H_NORMAL);
		wcd_clsh_v3_set_buck_mode(component, CLS_H_NORMAL);
	}
}

static void wcd_clsh_state_lo(struct wcd_clsh_ctrl *ctrl, int req_state,
			      bool is_enable, int mode)
{
	struct snd_soc_component *comp = ctrl->comp;

	if (mode != CLS_AB) {
		dev_err(comp->dev, "%s: LO cannot be in this mode: %d\n",
			__func__, mode);
		return;
	}

	if (is_enable) {
		wcd_clsh_set_buck_regulator_mode(comp, mode);
		wcd_clsh_set_buck_mode(comp, mode);
		wcd_clsh_set_flyback_mode(comp, mode);
		wcd_clsh_flyback_ctrl(ctrl, mode, true);
		wcd_clsh_set_flyback_current(comp, mode);
		wcd_clsh_buck_ctrl(ctrl, mode, true);
	} else {
		wcd_clsh_buck_ctrl(ctrl, mode, false);
		wcd_clsh_flyback_ctrl(ctrl, mode, false);
		wcd_clsh_set_flyback_mode(comp, CLS_H_NORMAL);
		wcd_clsh_set_buck_mode(comp, CLS_H_NORMAL);
		wcd_clsh_set_buck_regulator_mode(comp, CLS_H_NORMAL);
	}
}

static void wcd_clsh_v3_state_hph_r(struct wcd_clsh_ctrl *ctrl, int req_state,
				 bool is_enable, int mode)
{
	struct snd_soc_component *component = ctrl->comp;

	if (mode == CLS_H_NORMAL) {
		dev_dbg(component->dev, "%s: Normal mode not applicable for hph_r\n",
			__func__);
		return;
	}

	if (is_enable) {
		wcd_clsh_v3_set_buck_regulator_mode(component, mode);
		wcd_clsh_v3_set_flyback_mode(component, mode);
		wcd_clsh_v3_force_iq_ctl(component, mode, true);
		wcd_clsh_v3_flyback_ctrl(component, ctrl, mode, true);
		wcd_clsh_v3_set_flyback_current(component, mode);
		wcd_clsh_v3_set_buck_mode(component, mode);
		wcd_clsh_v3_buck_ctrl(component, ctrl, mode, true);
		wcd_clsh_v3_set_hph_mode(component, mode);
	} else {
		wcd_clsh_v3_set_hph_mode(component, CLS_H_NORMAL);

		/* buck and flyback set to default mode and disable */
		wcd_clsh_v3_flyback_ctrl(component, ctrl, CLS_H_NORMAL, false);
		wcd_clsh_v3_buck_ctrl(component, ctrl, CLS_H_NORMAL, false);
		wcd_clsh_v3_force_iq_ctl(component, CLS_H_NORMAL, false);
		wcd_clsh_v3_set_flyback_mode(component, CLS_H_NORMAL);
		wcd_clsh_v3_set_buck_mode(component, CLS_H_NORMAL);
	}
}

static void wcd_clsh_state_hph_r(struct wcd_clsh_ctrl *ctrl, int req_state,
				 bool is_enable, int mode)
{
	struct snd_soc_component *comp = ctrl->comp;

	if (mode == CLS_H_NORMAL) {
		dev_err(comp->dev, "%s: Normal mode not applicable for hph_r\n",
			__func__);
		return;
	}

	if (is_enable) {
		if (mode != CLS_AB) {
			wcd_enable_clsh_block(ctrl, true);
			/*
			 * These K1 values depend on the Headphone Impedance
			 * For now it is assumed to be 16 ohm
			 */
			snd_soc_component_update_bits(comp,
					WCD9XXX_A_CDC_CLSH_K1_MSB,
					WCD9XXX_A_CDC_CLSH_K1_MSB_COEF_MASK,
					0x00);
			snd_soc_component_update_bits(comp,
					WCD9XXX_A_CDC_CLSH_K1_LSB,
					WCD9XXX_A_CDC_CLSH_K1_LSB_COEF_MASK,
					0xC0);
			snd_soc_component_update_bits(comp,
					    WCD9XXX_A_CDC_RX2_RX_PATH_CFG0,
					    WCD9XXX_A_CDC_RX_PATH_CLSH_EN_MASK,
					    WCD9XXX_A_CDC_RX_PATH_CLSH_ENABLE);
		}
		wcd_clsh_set_buck_regulator_mode(comp, mode);
		wcd_clsh_set_flyback_mode(comp, mode);
		wcd_clsh_flyback_ctrl(ctrl, mode, true);
		wcd_clsh_set_flyback_current(comp, mode);
		wcd_clsh_set_buck_mode(comp, mode);
		wcd_clsh_buck_ctrl(ctrl, mode, true);
		wcd_clsh_v2_set_hph_mode(comp, mode);
		wcd_clsh_set_gain_path(ctrl, mode);
	} else {
		wcd_clsh_v2_set_hph_mode(comp, CLS_H_NORMAL);

		if (mode != CLS_AB) {
			snd_soc_component_update_bits(comp,
					    WCD9XXX_A_CDC_RX2_RX_PATH_CFG0,
					    WCD9XXX_A_CDC_RX_PATH_CLSH_EN_MASK,
					    WCD9XXX_A_CDC_RX_PATH_CLSH_DISABLE);
			wcd_enable_clsh_block(ctrl, false);
		}
		/* buck and flyback set to default mode and disable */
		wcd_clsh_buck_ctrl(ctrl, CLS_H_NORMAL, false);
		wcd_clsh_flyback_ctrl(ctrl, CLS_H_NORMAL, false);
		wcd_clsh_set_flyback_mode(comp, CLS_H_NORMAL);
		wcd_clsh_set_buck_mode(comp, CLS_H_NORMAL);
		wcd_clsh_set_buck_regulator_mode(comp, CLS_H_NORMAL);
	}
}

static void wcd_clsh_v3_state_hph_l(struct wcd_clsh_ctrl *ctrl, int req_state,
				 bool is_enable, int mode)
{
	struct snd_soc_component *component = ctrl->comp;

	if (mode == CLS_H_NORMAL) {
		dev_dbg(component->dev, "%s: Normal mode not applicable for hph_l\n",
			__func__);
		return;
	}

	if (is_enable) {
		wcd_clsh_v3_set_buck_regulator_mode(component, mode);
		wcd_clsh_v3_set_flyback_mode(component, mode);
		wcd_clsh_v3_force_iq_ctl(component, mode, true);
		wcd_clsh_v3_flyback_ctrl(component, ctrl, mode, true);
		wcd_clsh_v3_set_flyback_current(component, mode);
		wcd_clsh_v3_set_buck_mode(component, mode);
		wcd_clsh_v3_buck_ctrl(component, ctrl, mode, true);
		wcd_clsh_v3_set_hph_mode(component, mode);
	} else {
		wcd_clsh_v3_set_hph_mode(component, CLS_H_NORMAL);

		/* set buck and flyback to Default Mode */
		wcd_clsh_v3_flyback_ctrl(component, ctrl, CLS_H_NORMAL, false);
		wcd_clsh_v3_buck_ctrl(component, ctrl, CLS_H_NORMAL, false);
		wcd_clsh_v3_force_iq_ctl(component, CLS_H_NORMAL, false);
		wcd_clsh_v3_set_flyback_mode(component, CLS_H_NORMAL);
		wcd_clsh_v3_set_buck_mode(component, CLS_H_NORMAL);
	}
}

static void wcd_clsh_state_hph_l(struct wcd_clsh_ctrl *ctrl, int req_state,
				 bool is_enable, int mode)
{
	struct snd_soc_component *comp = ctrl->comp;

	if (mode == CLS_H_NORMAL) {
		dev_err(comp->dev, "%s: Normal mode not applicable for hph_l\n",
			__func__);
		return;
	}

	if (is_enable) {
		if (mode != CLS_AB) {
			wcd_enable_clsh_block(ctrl, true);
			/*
			 * These K1 values depend on the Headphone Impedance
			 * For now it is assumed to be 16 ohm
			 */
			snd_soc_component_update_bits(comp,
					WCD9XXX_A_CDC_CLSH_K1_MSB,
					WCD9XXX_A_CDC_CLSH_K1_MSB_COEF_MASK,
					0x00);
			snd_soc_component_update_bits(comp,
					WCD9XXX_A_CDC_CLSH_K1_LSB,
					WCD9XXX_A_CDC_CLSH_K1_LSB_COEF_MASK,
					0xC0);
			snd_soc_component_update_bits(comp,
					    WCD9XXX_A_CDC_RX1_RX_PATH_CFG0,
					    WCD9XXX_A_CDC_RX_PATH_CLSH_EN_MASK,
					    WCD9XXX_A_CDC_RX_PATH_CLSH_ENABLE);
		}
		wcd_clsh_set_buck_regulator_mode(comp, mode);
		wcd_clsh_set_flyback_mode(comp, mode);
		wcd_clsh_flyback_ctrl(ctrl, mode, true);
		wcd_clsh_set_flyback_current(comp, mode);
		wcd_clsh_set_buck_mode(comp, mode);
		wcd_clsh_buck_ctrl(ctrl, mode, true);
		wcd_clsh_v2_set_hph_mode(comp, mode);
		wcd_clsh_set_gain_path(ctrl, mode);
	} else {
		wcd_clsh_v2_set_hph_mode(comp, CLS_H_NORMAL);

		if (mode != CLS_AB) {
			snd_soc_component_update_bits(comp,
					    WCD9XXX_A_CDC_RX1_RX_PATH_CFG0,
					    WCD9XXX_A_CDC_RX_PATH_CLSH_EN_MASK,
					    WCD9XXX_A_CDC_RX_PATH_CLSH_DISABLE);
			wcd_enable_clsh_block(ctrl, false);
		}
		/* set buck and flyback to Default Mode */
		wcd_clsh_buck_ctrl(ctrl, CLS_H_NORMAL, false);
		wcd_clsh_flyback_ctrl(ctrl, CLS_H_NORMAL, false);
		wcd_clsh_set_flyback_mode(comp, CLS_H_NORMAL);
		wcd_clsh_set_buck_mode(comp, CLS_H_NORMAL);
		wcd_clsh_set_buck_regulator_mode(comp, CLS_H_NORMAL);
	}
}

static void wcd_clsh_v3_state_ear(struct wcd_clsh_ctrl *ctrl, int req_state,
			       bool is_enable, int mode)
{
	struct snd_soc_component *component = ctrl->comp;

	if (is_enable) {
		wcd_clsh_v3_set_buck_regulator_mode(component, mode);
		wcd_clsh_v3_set_flyback_mode(component, mode);
		wcd_clsh_v3_force_iq_ctl(component, mode, true);
		wcd_clsh_v3_flyback_ctrl(component, ctrl, mode, true);
		wcd_clsh_v3_set_flyback_current(component, mode);
		wcd_clsh_v3_set_buck_mode(component, mode);
		wcd_clsh_v3_buck_ctrl(component, ctrl, mode, true);
		wcd_clsh_v3_set_hph_mode(component, mode);
	} else {
		wcd_clsh_v3_set_hph_mode(component, CLS_H_NORMAL);

		/* set buck and flyback to Default Mode */
		wcd_clsh_v3_flyback_ctrl(component, ctrl, CLS_H_NORMAL, false);
		wcd_clsh_v3_buck_ctrl(component, ctrl, CLS_H_NORMAL, false);
		wcd_clsh_v3_force_iq_ctl(component, CLS_H_NORMAL, false);
		wcd_clsh_v3_set_flyback_mode(component, CLS_H_NORMAL);
		wcd_clsh_v3_set_buck_mode(component, CLS_H_NORMAL);
	}
}

static void wcd_clsh_state_ear(struct wcd_clsh_ctrl *ctrl, int req_state,
			       bool is_enable, int mode)
{
	struct snd_soc_component *comp = ctrl->comp;

	if (mode != CLS_H_NORMAL) {
		dev_err(comp->dev, "%s: mode: %d cannot be used for EAR\n",
			__func__, mode);
		return;
	}

	if (is_enable) {
		wcd_enable_clsh_block(ctrl, true);
		snd_soc_component_update_bits(comp,
					WCD9XXX_A_CDC_RX0_RX_PATH_CFG0,
					WCD9XXX_A_CDC_RX_PATH_CLSH_EN_MASK,
					WCD9XXX_A_CDC_RX_PATH_CLSH_ENABLE);
		wcd_clsh_set_buck_mode(comp, mode);
		wcd_clsh_set_flyback_mode(comp, mode);
		wcd_clsh_flyback_ctrl(ctrl, mode, true);
		wcd_clsh_set_flyback_current(comp, mode);
		wcd_clsh_buck_ctrl(ctrl, mode, true);
	} else {
		snd_soc_component_update_bits(comp,
					WCD9XXX_A_CDC_RX0_RX_PATH_CFG0,
					WCD9XXX_A_CDC_RX_PATH_CLSH_EN_MASK,
					WCD9XXX_A_CDC_RX_PATH_CLSH_DISABLE);
		wcd_enable_clsh_block(ctrl, false);
		wcd_clsh_buck_ctrl(ctrl, mode, false);
		wcd_clsh_flyback_ctrl(ctrl, mode, false);
		wcd_clsh_set_flyback_mode(comp, CLS_H_NORMAL);
		wcd_clsh_set_buck_mode(comp, CLS_H_NORMAL);
	}
}

static int _wcd_clsh_ctrl_set_state(struct wcd_clsh_ctrl *ctrl, int req_state,
				    bool is_enable, int mode)
{
	switch (req_state) {
	case WCD_CLSH_STATE_EAR:
		if (ctrl->codec_version >= WCD937X)
			wcd_clsh_v3_state_ear(ctrl, req_state, is_enable, mode);
		else
			wcd_clsh_state_ear(ctrl, req_state, is_enable, mode);
		break;
	case WCD_CLSH_STATE_HPHL:
		if (ctrl->codec_version >= WCD937X)
			wcd_clsh_v3_state_hph_l(ctrl, req_state, is_enable, mode);
		else
			wcd_clsh_state_hph_l(ctrl, req_state, is_enable, mode);
		break;
	case WCD_CLSH_STATE_HPHR:
		if (ctrl->codec_version >= WCD937X)
			wcd_clsh_v3_state_hph_r(ctrl, req_state, is_enable, mode);
		else
			wcd_clsh_state_hph_r(ctrl, req_state, is_enable, mode);
		break;
	case WCD_CLSH_STATE_LO:
		if (ctrl->codec_version < WCD937X)
			wcd_clsh_state_lo(ctrl, req_state, is_enable, mode);
		break;
	case WCD_CLSH_STATE_AUX:
		if (ctrl->codec_version >= WCD937X)
			wcd_clsh_v3_state_aux(ctrl, req_state, is_enable, mode);
		break;
	default:
		break;
	}

	return 0;
}

/*
 * Function: wcd_clsh_is_state_valid
 * Params: state
 * Description:
 * Provides information on valid states of Class H configuration
 */
static bool wcd_clsh_is_state_valid(int state)
{
	switch (state) {
	case WCD_CLSH_STATE_IDLE:
	case WCD_CLSH_STATE_EAR:
	case WCD_CLSH_STATE_HPHL:
	case WCD_CLSH_STATE_HPHR:
	case WCD_CLSH_STATE_LO:
	case WCD_CLSH_STATE_AUX:
		return true;
	default:
		return false;
	};
}

/*
 * Function: wcd_clsh_fsm
 * Params: ctrl, req_state, req_type, clsh_event
 * Description:
 * This function handles PRE DAC and POST DAC conditions of different devices
 * and updates class H configuration of different combination of devices
 * based on validity of their states. ctrl will contain current
 * class h state information
 */
int wcd_clsh_ctrl_set_state(struct wcd_clsh_ctrl *ctrl,
			    enum wcd_clsh_event clsh_event,
			    int nstate,
			    enum wcd_clsh_mode mode)
{
	struct snd_soc_component *comp = ctrl->comp;

	if (nstate == ctrl->state)
		return 0;

	if (!wcd_clsh_is_state_valid(nstate)) {
		dev_err(comp->dev, "Class-H not a valid new state:\n");
		return -EINVAL;
	}

	switch (clsh_event) {
	case WCD_CLSH_EVENT_PRE_DAC:
		_wcd_clsh_ctrl_set_state(ctrl, nstate, CLSH_REQ_ENABLE, mode);
		break;
	case WCD_CLSH_EVENT_POST_PA:
		_wcd_clsh_ctrl_set_state(ctrl, nstate, CLSH_REQ_DISABLE, mode);
		break;
	}

	ctrl->state = nstate;
	ctrl->mode = mode;

	return 0;
}

int wcd_clsh_ctrl_get_state(struct wcd_clsh_ctrl *ctrl)
{
	return ctrl->state;
}

struct wcd_clsh_ctrl *wcd_clsh_ctrl_alloc(struct snd_soc_component *comp,
					  int version)
{
	struct wcd_clsh_ctrl *ctrl;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return ERR_PTR(-ENOMEM);

	ctrl->state = WCD_CLSH_STATE_IDLE;
	ctrl->comp = comp;
	ctrl->codec_version = version;

	return ctrl;
}

void wcd_clsh_ctrl_free(struct wcd_clsh_ctrl *ctrl)
{
	kfree(ctrl);
}
