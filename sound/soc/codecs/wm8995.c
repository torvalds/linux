// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm8995.c  --  WM8995 ALSA SoC Audio driver
 *
 * Copyright 2010 Wolfson Microelectronics plc
 *
 * Author: Dimitris Papastamos <dp@opensource.wolfsonmicro.com>
 *
 * Based on wm8994.c and wm_hubs.c by Mark Brown
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8995.h"

#define WM8995_NUM_SUPPLIES 8
static const char *wm8995_supply_names[WM8995_NUM_SUPPLIES] = {
	"DCVDD",
	"DBVDD1",
	"DBVDD2",
	"DBVDD3",
	"AVDD1",
	"AVDD2",
	"CPVDD",
	"MICVDD"
};

static const struct reg_default wm8995_reg_defaults[] = {
	{ 0, 0x8995 },
	{ 5, 0x0100 },
	{ 16, 0x000b },
	{ 17, 0x000b },
	{ 24, 0x02c0 },
	{ 25, 0x02c0 },
	{ 26, 0x02c0 },
	{ 27, 0x02c0 },
	{ 28, 0x000f },
	{ 32, 0x0005 },
	{ 33, 0x0005 },
	{ 40, 0x0003 },
	{ 41, 0x0013 },
	{ 48, 0x0004 },
	{ 56, 0x09f8 },
	{ 64, 0x1f25 },
	{ 69, 0x0004 },
	{ 82, 0xaaaa },
	{ 84, 0x2a2a },
	{ 146, 0x0060 },
	{ 256, 0x0002 },
	{ 257, 0x8004 },
	{ 520, 0x0010 },
	{ 528, 0x0083 },
	{ 529, 0x0083 },
	{ 548, 0x0c80 },
	{ 580, 0x0c80 },
	{ 768, 0x4050 },
	{ 769, 0x4000 },
	{ 771, 0x0040 },
	{ 772, 0x0040 },
	{ 773, 0x0040 },
	{ 774, 0x0004 },
	{ 775, 0x0100 },
	{ 784, 0x4050 },
	{ 785, 0x4000 },
	{ 787, 0x0040 },
	{ 788, 0x0040 },
	{ 789, 0x0040 },
	{ 1024, 0x00c0 },
	{ 1025, 0x00c0 },
	{ 1026, 0x00c0 },
	{ 1027, 0x00c0 },
	{ 1028, 0x00c0 },
	{ 1029, 0x00c0 },
	{ 1030, 0x00c0 },
	{ 1031, 0x00c0 },
	{ 1056, 0x0200 },
	{ 1057, 0x0010 },
	{ 1058, 0x0200 },
	{ 1059, 0x0010 },
	{ 1088, 0x0098 },
	{ 1089, 0x0845 },
	{ 1104, 0x0098 },
	{ 1105, 0x0845 },
	{ 1152, 0x6318 },
	{ 1153, 0x6300 },
	{ 1154, 0x0fca },
	{ 1155, 0x0400 },
	{ 1156, 0x00d8 },
	{ 1157, 0x1eb5 },
	{ 1158, 0xf145 },
	{ 1159, 0x0b75 },
	{ 1160, 0x01c5 },
	{ 1161, 0x1c58 },
	{ 1162, 0xf373 },
	{ 1163, 0x0a54 },
	{ 1164, 0x0558 },
	{ 1165, 0x168e },
	{ 1166, 0xf829 },
	{ 1167, 0x07ad },
	{ 1168, 0x1103 },
	{ 1169, 0x0564 },
	{ 1170, 0x0559 },
	{ 1171, 0x4000 },
	{ 1184, 0x6318 },
	{ 1185, 0x6300 },
	{ 1186, 0x0fca },
	{ 1187, 0x0400 },
	{ 1188, 0x00d8 },
	{ 1189, 0x1eb5 },
	{ 1190, 0xf145 },
	{ 1191, 0x0b75 },
	{ 1192, 0x01c5 },
	{ 1193, 0x1c58 },
	{ 1194, 0xf373 },
	{ 1195, 0x0a54 },
	{ 1196, 0x0558 },
	{ 1197, 0x168e },
	{ 1198, 0xf829 },
	{ 1199, 0x07ad },
	{ 1200, 0x1103 },
	{ 1201, 0x0564 },
	{ 1202, 0x0559 },
	{ 1203, 0x4000 },
	{ 1280, 0x00c0 },
	{ 1281, 0x00c0 },
	{ 1282, 0x00c0 },
	{ 1283, 0x00c0 },
	{ 1312, 0x0200 },
	{ 1313, 0x0010 },
	{ 1344, 0x0098 },
	{ 1345, 0x0845 },
	{ 1408, 0x6318 },
	{ 1409, 0x6300 },
	{ 1410, 0x0fca },
	{ 1411, 0x0400 },
	{ 1412, 0x00d8 },
	{ 1413, 0x1eb5 },
	{ 1414, 0xf145 },
	{ 1415, 0x0b75 },
	{ 1416, 0x01c5 },
	{ 1417, 0x1c58 },
	{ 1418, 0xf373 },
	{ 1419, 0x0a54 },
	{ 1420, 0x0558 },
	{ 1421, 0x168e },
	{ 1422, 0xf829 },
	{ 1423, 0x07ad },
	{ 1424, 0x1103 },
	{ 1425, 0x0564 },
	{ 1426, 0x0559 },
	{ 1427, 0x4000 },
	{ 1568, 0x0002 },
	{ 1792, 0xa100 },
	{ 1793, 0xa101 },
	{ 1794, 0xa101 },
	{ 1795, 0xa101 },
	{ 1796, 0xa101 },
	{ 1797, 0xa101 },
	{ 1798, 0xa101 },
	{ 1799, 0xa101 },
	{ 1800, 0xa101 },
	{ 1801, 0xa101 },
	{ 1802, 0xa101 },
	{ 1803, 0xa101 },
	{ 1804, 0xa101 },
	{ 1805, 0xa101 },
	{ 1825, 0x0055 },
	{ 1848, 0x3fff },
	{ 1849, 0x1fff },
	{ 2049, 0x0001 },
	{ 2050, 0x0069 },
	{ 2056, 0x0002 },
	{ 2057, 0x0003 },
	{ 2058, 0x0069 },
	{ 12288, 0x0001 },
	{ 12289, 0x0001 },
	{ 12291, 0x0006 },
	{ 12292, 0x0040 },
	{ 12293, 0x0001 },
	{ 12294, 0x000f },
	{ 12295, 0x0006 },
	{ 12296, 0x0001 },
	{ 12297, 0x0003 },
	{ 12298, 0x0104 },
	{ 12300, 0x0060 },
	{ 12301, 0x0011 },
	{ 12302, 0x0401 },
	{ 12304, 0x0050 },
	{ 12305, 0x0003 },
	{ 12306, 0x0100 },
	{ 12308, 0x0051 },
	{ 12309, 0x0003 },
	{ 12310, 0x0104 },
	{ 12311, 0x000a },
	{ 12312, 0x0060 },
	{ 12313, 0x003b },
	{ 12314, 0x0502 },
	{ 12315, 0x0100 },
	{ 12316, 0x2fff },
	{ 12320, 0x2fff },
	{ 12324, 0x2fff },
	{ 12328, 0x2fff },
	{ 12332, 0x2fff },
	{ 12336, 0x2fff },
	{ 12340, 0x2fff },
	{ 12344, 0x2fff },
	{ 12348, 0x2fff },
	{ 12352, 0x0001 },
	{ 12353, 0x0001 },
	{ 12355, 0x0006 },
	{ 12356, 0x0040 },
	{ 12357, 0x0001 },
	{ 12358, 0x000f },
	{ 12359, 0x0006 },
	{ 12360, 0x0001 },
	{ 12361, 0x0003 },
	{ 12362, 0x0104 },
	{ 12364, 0x0060 },
	{ 12365, 0x0011 },
	{ 12366, 0x0401 },
	{ 12368, 0x0050 },
	{ 12369, 0x0003 },
	{ 12370, 0x0100 },
	{ 12372, 0x0060 },
	{ 12373, 0x003b },
	{ 12374, 0x0502 },
	{ 12375, 0x0100 },
	{ 12376, 0x2fff },
	{ 12380, 0x2fff },
	{ 12384, 0x2fff },
	{ 12388, 0x2fff },
	{ 12392, 0x2fff },
	{ 12396, 0x2fff },
	{ 12400, 0x2fff },
	{ 12404, 0x2fff },
	{ 12408, 0x2fff },
	{ 12412, 0x2fff },
	{ 12416, 0x0001 },
	{ 12417, 0x0001 },
	{ 12419, 0x0006 },
	{ 12420, 0x0040 },
	{ 12421, 0x0001 },
	{ 12422, 0x000f },
	{ 12423, 0x0006 },
	{ 12424, 0x0001 },
	{ 12425, 0x0003 },
	{ 12426, 0x0106 },
	{ 12428, 0x0061 },
	{ 12429, 0x0011 },
	{ 12430, 0x0401 },
	{ 12432, 0x0050 },
	{ 12433, 0x0003 },
	{ 12434, 0x0102 },
	{ 12436, 0x0051 },
	{ 12437, 0x0003 },
	{ 12438, 0x0106 },
	{ 12439, 0x000a },
	{ 12440, 0x0061 },
	{ 12441, 0x003b },
	{ 12442, 0x0502 },
	{ 12443, 0x0100 },
	{ 12444, 0x2fff },
	{ 12448, 0x2fff },
	{ 12452, 0x2fff },
	{ 12456, 0x2fff },
	{ 12460, 0x2fff },
	{ 12464, 0x2fff },
	{ 12468, 0x2fff },
	{ 12472, 0x2fff },
	{ 12476, 0x2fff },
	{ 12480, 0x0001 },
	{ 12481, 0x0001 },
	{ 12483, 0x0006 },
	{ 12484, 0x0040 },
	{ 12485, 0x0001 },
	{ 12486, 0x000f },
	{ 12487, 0x0006 },
	{ 12488, 0x0001 },
	{ 12489, 0x0003 },
	{ 12490, 0x0106 },
	{ 12492, 0x0061 },
	{ 12493, 0x0011 },
	{ 12494, 0x0401 },
	{ 12496, 0x0050 },
	{ 12497, 0x0003 },
	{ 12498, 0x0102 },
	{ 12500, 0x0061 },
	{ 12501, 0x003b },
	{ 12502, 0x0502 },
	{ 12503, 0x0100 },
	{ 12504, 0x2fff },
	{ 12508, 0x2fff },
	{ 12512, 0x2fff },
	{ 12516, 0x2fff },
	{ 12520, 0x2fff },
	{ 12524, 0x2fff },
	{ 12528, 0x2fff },
	{ 12532, 0x2fff },
	{ 12536, 0x2fff },
	{ 12540, 0x2fff },
	{ 12544, 0x0060 },
	{ 12546, 0x0601 },
	{ 12548, 0x0050 },
	{ 12550, 0x0100 },
	{ 12552, 0x0001 },
	{ 12554, 0x0104 },
	{ 12555, 0x0100 },
	{ 12556, 0x2fff },
	{ 12560, 0x2fff },
	{ 12564, 0x2fff },
	{ 12568, 0x2fff },
	{ 12572, 0x2fff },
	{ 12576, 0x2fff },
	{ 12580, 0x2fff },
	{ 12584, 0x2fff },
	{ 12588, 0x2fff },
	{ 12592, 0x2fff },
	{ 12596, 0x2fff },
	{ 12600, 0x2fff },
	{ 12604, 0x2fff },
	{ 12608, 0x0061 },
	{ 12610, 0x0601 },
	{ 12612, 0x0050 },
	{ 12614, 0x0102 },
	{ 12616, 0x0001 },
	{ 12618, 0x0106 },
	{ 12619, 0x0100 },
	{ 12620, 0x2fff },
	{ 12624, 0x2fff },
	{ 12628, 0x2fff },
	{ 12632, 0x2fff },
	{ 12636, 0x2fff },
	{ 12640, 0x2fff },
	{ 12644, 0x2fff },
	{ 12648, 0x2fff },
	{ 12652, 0x2fff },
	{ 12656, 0x2fff },
	{ 12660, 0x2fff },
	{ 12664, 0x2fff },
	{ 12668, 0x2fff },
	{ 12672, 0x0060 },
	{ 12674, 0x0601 },
	{ 12676, 0x0061 },
	{ 12678, 0x0601 },
	{ 12680, 0x0050 },
	{ 12682, 0x0300 },
	{ 12684, 0x0001 },
	{ 12686, 0x0304 },
	{ 12688, 0x0040 },
	{ 12690, 0x000f },
	{ 12692, 0x0001 },
	{ 12695, 0x0100 },
};

struct fll_config {
	int src;
	int in;
	int out;
};

struct wm8995_priv {
	struct regmap *regmap;
	int sysclk[2];
	int mclk[2];
	int aifclk[2];
	struct fll_config fll[2], fll_suspend[2];
	struct regulator_bulk_data supplies[WM8995_NUM_SUPPLIES];
	struct notifier_block disable_nb[WM8995_NUM_SUPPLIES];
	struct snd_soc_component *component;
};

/*
 * We can't use the same notifier block for more than one supply and
 * there's no way I can see to get from a callback to the caller
 * except container_of().
 */
#define WM8995_REGULATOR_EVENT(n) \
static int wm8995_regulator_event_##n(struct notifier_block *nb, \
				      unsigned long event, void *data)    \
{ \
	struct wm8995_priv *wm8995 = container_of(nb, struct wm8995_priv, \
				     disable_nb[n]); \
	if (event & REGULATOR_EVENT_DISABLE) { \
		regcache_mark_dirty(wm8995->regmap);	\
	} \
	return 0; \
}

WM8995_REGULATOR_EVENT(0)
WM8995_REGULATOR_EVENT(1)
WM8995_REGULATOR_EVENT(2)
WM8995_REGULATOR_EVENT(3)
WM8995_REGULATOR_EVENT(4)
WM8995_REGULATOR_EVENT(5)
WM8995_REGULATOR_EVENT(6)
WM8995_REGULATOR_EVENT(7)

static const DECLARE_TLV_DB_SCALE(digital_tlv, -7200, 75, 1);
static const DECLARE_TLV_DB_SCALE(in1lr_pga_tlv, -1650, 150, 0);
static const DECLARE_TLV_DB_SCALE(in1l_boost_tlv, 0, 600, 0);
static const DECLARE_TLV_DB_SCALE(sidetone_tlv, -3600, 150, 0);

static const char *in1l_text[] = {
	"Differential", "Single-ended IN1LN", "Single-ended IN1LP"
};

static SOC_ENUM_SINGLE_DECL(in1l_enum, WM8995_LEFT_LINE_INPUT_CONTROL,
			    2, in1l_text);

static const char *in1r_text[] = {
	"Differential", "Single-ended IN1RN", "Single-ended IN1RP"
};

static SOC_ENUM_SINGLE_DECL(in1r_enum, WM8995_LEFT_LINE_INPUT_CONTROL,
			    0, in1r_text);

static const char *dmic_src_text[] = {
	"DMICDAT1", "DMICDAT2", "DMICDAT3"
};

static SOC_ENUM_SINGLE_DECL(dmic_src1_enum, WM8995_POWER_MANAGEMENT_5,
			    8, dmic_src_text);
static SOC_ENUM_SINGLE_DECL(dmic_src2_enum, WM8995_POWER_MANAGEMENT_5,
			    6, dmic_src_text);

static const struct snd_kcontrol_new wm8995_snd_controls[] = {
	SOC_DOUBLE_R_TLV("DAC1 Volume", WM8995_DAC1_LEFT_VOLUME,
		WM8995_DAC1_RIGHT_VOLUME, 0, 96, 0, digital_tlv),
	SOC_DOUBLE_R("DAC1 Switch", WM8995_DAC1_LEFT_VOLUME,
		WM8995_DAC1_RIGHT_VOLUME, 9, 1, 1),

	SOC_DOUBLE_R_TLV("DAC2 Volume", WM8995_DAC2_LEFT_VOLUME,
		WM8995_DAC2_RIGHT_VOLUME, 0, 96, 0, digital_tlv),
	SOC_DOUBLE_R("DAC2 Switch", WM8995_DAC2_LEFT_VOLUME,
		WM8995_DAC2_RIGHT_VOLUME, 9, 1, 1),

	SOC_DOUBLE_R_TLV("AIF1DAC1 Volume", WM8995_AIF1_DAC1_LEFT_VOLUME,
		WM8995_AIF1_DAC1_RIGHT_VOLUME, 0, 96, 0, digital_tlv),
	SOC_DOUBLE_R_TLV("AIF1DAC2 Volume", WM8995_AIF1_DAC2_LEFT_VOLUME,
		WM8995_AIF1_DAC2_RIGHT_VOLUME, 0, 96, 0, digital_tlv),
	SOC_DOUBLE_R_TLV("AIF2DAC Volume", WM8995_AIF2_DAC_LEFT_VOLUME,
		WM8995_AIF2_DAC_RIGHT_VOLUME, 0, 96, 0, digital_tlv),

	SOC_DOUBLE_R_TLV("IN1LR Volume", WM8995_LEFT_LINE_INPUT_1_VOLUME,
		WM8995_RIGHT_LINE_INPUT_1_VOLUME, 0, 31, 0, in1lr_pga_tlv),

	SOC_SINGLE_TLV("IN1L Boost", WM8995_LEFT_LINE_INPUT_CONTROL,
		4, 3, 0, in1l_boost_tlv),

	SOC_ENUM("IN1L Mode", in1l_enum),
	SOC_ENUM("IN1R Mode", in1r_enum),

	SOC_ENUM("DMIC1 SRC", dmic_src1_enum),
	SOC_ENUM("DMIC2 SRC", dmic_src2_enum),

	SOC_DOUBLE_TLV("DAC1 Sidetone Volume", WM8995_DAC1_MIXER_VOLUMES, 0, 5,
		24, 0, sidetone_tlv),
	SOC_DOUBLE_TLV("DAC2 Sidetone Volume", WM8995_DAC2_MIXER_VOLUMES, 0, 5,
		24, 0, sidetone_tlv),

	SOC_DOUBLE_R_TLV("AIF1ADC1 Volume", WM8995_AIF1_ADC1_LEFT_VOLUME,
		WM8995_AIF1_ADC1_RIGHT_VOLUME, 0, 96, 0, digital_tlv),
	SOC_DOUBLE_R_TLV("AIF1ADC2 Volume", WM8995_AIF1_ADC2_LEFT_VOLUME,
		WM8995_AIF1_ADC2_RIGHT_VOLUME, 0, 96, 0, digital_tlv),
	SOC_DOUBLE_R_TLV("AIF2ADC Volume", WM8995_AIF2_ADC_LEFT_VOLUME,
		WM8995_AIF2_ADC_RIGHT_VOLUME, 0, 96, 0, digital_tlv)
};

static void wm8995_update_class_w(struct snd_soc_component *component)
{
	int enable = 1;
	int source = 0;  /* GCC flow analysis can't track enable */
	int reg, reg_r;

	/* We also need the same setting for L/R and only one path */
	reg = snd_soc_component_read(component, WM8995_DAC1_LEFT_MIXER_ROUTING);
	switch (reg) {
	case WM8995_AIF2DACL_TO_DAC1L:
		dev_dbg(component->dev, "Class W source AIF2DAC\n");
		source = 2 << WM8995_CP_DYN_SRC_SEL_SHIFT;
		break;
	case WM8995_AIF1DAC2L_TO_DAC1L:
		dev_dbg(component->dev, "Class W source AIF1DAC2\n");
		source = 1 << WM8995_CP_DYN_SRC_SEL_SHIFT;
		break;
	case WM8995_AIF1DAC1L_TO_DAC1L:
		dev_dbg(component->dev, "Class W source AIF1DAC1\n");
		source = 0 << WM8995_CP_DYN_SRC_SEL_SHIFT;
		break;
	default:
		dev_dbg(component->dev, "DAC mixer setting: %x\n", reg);
		enable = 0;
		break;
	}

	reg_r = snd_soc_component_read(component, WM8995_DAC1_RIGHT_MIXER_ROUTING);
	if (reg_r != reg) {
		dev_dbg(component->dev, "Left and right DAC mixers different\n");
		enable = 0;
	}

	if (enable) {
		dev_dbg(component->dev, "Class W enabled\n");
		snd_soc_component_update_bits(component, WM8995_CLASS_W_1,
				    WM8995_CP_DYN_PWR_MASK |
				    WM8995_CP_DYN_SRC_SEL_MASK,
				    source | WM8995_CP_DYN_PWR);
	} else {
		dev_dbg(component->dev, "Class W disabled\n");
		snd_soc_component_update_bits(component, WM8995_CLASS_W_1,
				    WM8995_CP_DYN_PWR_MASK, 0);
	}
}

static int check_clk_sys(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(source->dapm);
	unsigned int reg;
	const char *clk;

	reg = snd_soc_component_read(component, WM8995_CLOCKING_1);
	/* Check what we're currently using for CLK_SYS */
	if (reg & WM8995_SYSCLK_SRC)
		clk = "AIF2CLK";
	else
		clk = "AIF1CLK";
	return !strcmp(source->name, clk);
}

static int wm8995_put_class_w(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_dapm_kcontrol_component(kcontrol);
	int ret;

	ret = snd_soc_dapm_put_volsw(kcontrol, ucontrol);
	wm8995_update_class_w(component);
	return ret;
}

static int hp_supply_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Enable the headphone amp */
		snd_soc_component_update_bits(component, WM8995_POWER_MANAGEMENT_1,
				    WM8995_HPOUT1L_ENA_MASK |
				    WM8995_HPOUT1R_ENA_MASK,
				    WM8995_HPOUT1L_ENA |
				    WM8995_HPOUT1R_ENA);

		/* Enable the second stage */
		snd_soc_component_update_bits(component, WM8995_ANALOGUE_HP_1,
				    WM8995_HPOUT1L_DLY_MASK |
				    WM8995_HPOUT1R_DLY_MASK,
				    WM8995_HPOUT1L_DLY |
				    WM8995_HPOUT1R_DLY);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, WM8995_CHARGE_PUMP_1,
				    WM8995_CP_ENA_MASK, 0);
		break;
	}

	return 0;
}

static void dc_servo_cmd(struct snd_soc_component *component,
			 unsigned int reg, unsigned int val, unsigned int mask)
{
	int timeout = 10;

	dev_dbg(component->dev, "%s: reg = %#x, val = %#x, mask = %#x\n",
		__func__, reg, val, mask);

	snd_soc_component_write(component, reg, val);
	while (timeout--) {
		msleep(10);
		val = snd_soc_component_read(component, WM8995_DC_SERVO_READBACK_0);
		if ((val & mask) == mask)
			return;
	}

	dev_err(component->dev, "Timed out waiting for DC Servo\n");
}

static int hp_event(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	unsigned int reg;

	reg = snd_soc_component_read(component, WM8995_ANALOGUE_HP_1);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, WM8995_CHARGE_PUMP_1,
				    WM8995_CP_ENA_MASK, WM8995_CP_ENA);

		msleep(5);

		snd_soc_component_update_bits(component, WM8995_POWER_MANAGEMENT_1,
				    WM8995_HPOUT1L_ENA_MASK |
				    WM8995_HPOUT1R_ENA_MASK,
				    WM8995_HPOUT1L_ENA | WM8995_HPOUT1R_ENA);

		udelay(20);

		reg |= WM8995_HPOUT1L_DLY | WM8995_HPOUT1R_DLY;
		snd_soc_component_write(component, WM8995_ANALOGUE_HP_1, reg);

		snd_soc_component_write(component, WM8995_DC_SERVO_1, WM8995_DCS_ENA_CHAN_0 |
			      WM8995_DCS_ENA_CHAN_1);

		dc_servo_cmd(component, WM8995_DC_SERVO_2,
			     WM8995_DCS_TRIG_STARTUP_0 |
			     WM8995_DCS_TRIG_STARTUP_1,
			     WM8995_DCS_TRIG_DAC_WR_0 |
			     WM8995_DCS_TRIG_DAC_WR_1);

		reg |= WM8995_HPOUT1R_OUTP | WM8995_HPOUT1R_RMV_SHORT |
		       WM8995_HPOUT1L_OUTP | WM8995_HPOUT1L_RMV_SHORT;
		snd_soc_component_write(component, WM8995_ANALOGUE_HP_1, reg);

		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, WM8995_ANALOGUE_HP_1,
				    WM8995_HPOUT1L_OUTP_MASK |
				    WM8995_HPOUT1R_OUTP_MASK |
				    WM8995_HPOUT1L_RMV_SHORT_MASK |
				    WM8995_HPOUT1R_RMV_SHORT_MASK, 0);

		snd_soc_component_update_bits(component, WM8995_ANALOGUE_HP_1,
				    WM8995_HPOUT1L_DLY_MASK |
				    WM8995_HPOUT1R_DLY_MASK, 0);

		snd_soc_component_write(component, WM8995_DC_SERVO_1, 0);

		snd_soc_component_update_bits(component, WM8995_POWER_MANAGEMENT_1,
				    WM8995_HPOUT1L_ENA_MASK |
				    WM8995_HPOUT1R_ENA_MASK,
				    0);
		break;
	}

	return 0;
}

static int configure_aif_clock(struct snd_soc_component *component, int aif)
{
	struct wm8995_priv *wm8995;
	int rate;
	int reg1 = 0;
	int offset;

	wm8995 = snd_soc_component_get_drvdata(component);

	if (aif)
		offset = 4;
	else
		offset = 0;

	switch (wm8995->sysclk[aif]) {
	case WM8995_SYSCLK_MCLK1:
		rate = wm8995->mclk[0];
		break;
	case WM8995_SYSCLK_MCLK2:
		reg1 |= 0x8;
		rate = wm8995->mclk[1];
		break;
	case WM8995_SYSCLK_FLL1:
		reg1 |= 0x10;
		rate = wm8995->fll[0].out;
		break;
	case WM8995_SYSCLK_FLL2:
		reg1 |= 0x18;
		rate = wm8995->fll[1].out;
		break;
	default:
		return -EINVAL;
	}

	if (rate >= 13500000) {
		rate /= 2;
		reg1 |= WM8995_AIF1CLK_DIV;

		dev_dbg(component->dev, "Dividing AIF%d clock to %dHz\n",
			aif + 1, rate);
	}

	wm8995->aifclk[aif] = rate;

	snd_soc_component_update_bits(component, WM8995_AIF1_CLOCKING_1 + offset,
			    WM8995_AIF1CLK_SRC_MASK | WM8995_AIF1CLK_DIV_MASK,
			    reg1);
	return 0;
}

static int configure_clock(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct wm8995_priv *wm8995;
	int change, new;

	wm8995 = snd_soc_component_get_drvdata(component);

	/* Bring up the AIF clocks first */
	configure_aif_clock(component, 0);
	configure_aif_clock(component, 1);

	/*
	 * Then switch CLK_SYS over to the higher of them; a change
	 * can only happen as a result of a clocking change which can
	 * only be made outside of DAPM so we can safely redo the
	 * clocking.
	 */

	/* If they're equal it doesn't matter which is used */
	if (wm8995->aifclk[0] == wm8995->aifclk[1])
		return 0;

	if (wm8995->aifclk[0] < wm8995->aifclk[1])
		new = WM8995_SYSCLK_SRC;
	else
		new = 0;

	change = snd_soc_component_update_bits(component, WM8995_CLOCKING_1,
				     WM8995_SYSCLK_SRC_MASK, new);
	if (!change)
		return 0;

	snd_soc_dapm_sync(dapm);

	return 0;
}

static int clk_sys_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		return configure_clock(component);

	case SND_SOC_DAPM_POST_PMD:
		configure_clock(component);
		break;
	}

	return 0;
}

static const char *sidetone_text[] = {
	"ADC/DMIC1", "DMIC2",
};

static SOC_ENUM_SINGLE_DECL(sidetone1_enum, WM8995_SIDETONE, 0, sidetone_text);

static const struct snd_kcontrol_new sidetone1_mux =
	SOC_DAPM_ENUM("Left Sidetone Mux", sidetone1_enum);

static SOC_ENUM_SINGLE_DECL(sidetone2_enum, WM8995_SIDETONE, 1, sidetone_text);

static const struct snd_kcontrol_new sidetone2_mux =
	SOC_DAPM_ENUM("Right Sidetone Mux", sidetone2_enum);

static const struct snd_kcontrol_new aif1adc1l_mix[] = {
	SOC_DAPM_SINGLE("ADC/DMIC Switch", WM8995_AIF1_ADC1_LEFT_MIXER_ROUTING,
		1, 1, 0),
	SOC_DAPM_SINGLE("AIF2 Switch", WM8995_AIF1_ADC1_LEFT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new aif1adc1r_mix[] = {
	SOC_DAPM_SINGLE("ADC/DMIC Switch", WM8995_AIF1_ADC1_RIGHT_MIXER_ROUTING,
		1, 1, 0),
	SOC_DAPM_SINGLE("AIF2 Switch", WM8995_AIF1_ADC1_RIGHT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new aif1adc2l_mix[] = {
	SOC_DAPM_SINGLE("DMIC Switch", WM8995_AIF1_ADC2_LEFT_MIXER_ROUTING,
		1, 1, 0),
	SOC_DAPM_SINGLE("AIF2 Switch", WM8995_AIF1_ADC2_LEFT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new aif1adc2r_mix[] = {
	SOC_DAPM_SINGLE("DMIC Switch", WM8995_AIF1_ADC2_RIGHT_MIXER_ROUTING,
		1, 1, 0),
	SOC_DAPM_SINGLE("AIF2 Switch", WM8995_AIF1_ADC2_RIGHT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new dac1l_mix[] = {
	WM8995_CLASS_W_SWITCH("Right Sidetone Switch", WM8995_DAC1_LEFT_MIXER_ROUTING,
		5, 1, 0),
	WM8995_CLASS_W_SWITCH("Left Sidetone Switch", WM8995_DAC1_LEFT_MIXER_ROUTING,
		4, 1, 0),
	WM8995_CLASS_W_SWITCH("AIF2 Switch", WM8995_DAC1_LEFT_MIXER_ROUTING,
		2, 1, 0),
	WM8995_CLASS_W_SWITCH("AIF1.2 Switch", WM8995_DAC1_LEFT_MIXER_ROUTING,
		1, 1, 0),
	WM8995_CLASS_W_SWITCH("AIF1.1 Switch", WM8995_DAC1_LEFT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new dac1r_mix[] = {
	WM8995_CLASS_W_SWITCH("Right Sidetone Switch", WM8995_DAC1_RIGHT_MIXER_ROUTING,
		5, 1, 0),
	WM8995_CLASS_W_SWITCH("Left Sidetone Switch", WM8995_DAC1_RIGHT_MIXER_ROUTING,
		4, 1, 0),
	WM8995_CLASS_W_SWITCH("AIF2 Switch", WM8995_DAC1_RIGHT_MIXER_ROUTING,
		2, 1, 0),
	WM8995_CLASS_W_SWITCH("AIF1.2 Switch", WM8995_DAC1_RIGHT_MIXER_ROUTING,
		1, 1, 0),
	WM8995_CLASS_W_SWITCH("AIF1.1 Switch", WM8995_DAC1_RIGHT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new aif2dac2l_mix[] = {
	SOC_DAPM_SINGLE("Right Sidetone Switch", WM8995_DAC2_LEFT_MIXER_ROUTING,
		5, 1, 0),
	SOC_DAPM_SINGLE("Left Sidetone Switch", WM8995_DAC2_LEFT_MIXER_ROUTING,
		4, 1, 0),
	SOC_DAPM_SINGLE("AIF2 Switch", WM8995_DAC2_LEFT_MIXER_ROUTING,
		2, 1, 0),
	SOC_DAPM_SINGLE("AIF1.2 Switch", WM8995_DAC2_LEFT_MIXER_ROUTING,
		1, 1, 0),
	SOC_DAPM_SINGLE("AIF1.1 Switch", WM8995_DAC2_LEFT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new aif2dac2r_mix[] = {
	SOC_DAPM_SINGLE("Right Sidetone Switch", WM8995_DAC2_RIGHT_MIXER_ROUTING,
		5, 1, 0),
	SOC_DAPM_SINGLE("Left Sidetone Switch", WM8995_DAC2_RIGHT_MIXER_ROUTING,
		4, 1, 0),
	SOC_DAPM_SINGLE("AIF2 Switch", WM8995_DAC2_RIGHT_MIXER_ROUTING,
		2, 1, 0),
	SOC_DAPM_SINGLE("AIF1.2 Switch", WM8995_DAC2_RIGHT_MIXER_ROUTING,
		1, 1, 0),
	SOC_DAPM_SINGLE("AIF1.1 Switch", WM8995_DAC2_RIGHT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new in1l_pga =
	SOC_DAPM_SINGLE("IN1L Switch", WM8995_POWER_MANAGEMENT_2, 5, 1, 0);

static const struct snd_kcontrol_new in1r_pga =
	SOC_DAPM_SINGLE("IN1R Switch", WM8995_POWER_MANAGEMENT_2, 4, 1, 0);

static const char *adc_mux_text[] = {
	"ADC",
	"DMIC",
};

static SOC_ENUM_SINGLE_VIRT_DECL(adc_enum, adc_mux_text);

static const struct snd_kcontrol_new adcl_mux =
	SOC_DAPM_ENUM("ADCL Mux", adc_enum);

static const struct snd_kcontrol_new adcr_mux =
	SOC_DAPM_ENUM("ADCR Mux", adc_enum);

static const char *spk_src_text[] = {
	"DAC1L", "DAC1R", "DAC2L", "DAC2R"
};

static SOC_ENUM_SINGLE_DECL(spk1l_src_enum, WM8995_LEFT_PDM_SPEAKER_1,
			    0, spk_src_text);
static SOC_ENUM_SINGLE_DECL(spk1r_src_enum, WM8995_RIGHT_PDM_SPEAKER_1,
			    0, spk_src_text);
static SOC_ENUM_SINGLE_DECL(spk2l_src_enum, WM8995_LEFT_PDM_SPEAKER_2,
			    0, spk_src_text);
static SOC_ENUM_SINGLE_DECL(spk2r_src_enum, WM8995_RIGHT_PDM_SPEAKER_2,
			    0, spk_src_text);

static const struct snd_kcontrol_new spk1l_mux =
	SOC_DAPM_ENUM("SPK1L SRC", spk1l_src_enum);
static const struct snd_kcontrol_new spk1r_mux =
	SOC_DAPM_ENUM("SPK1R SRC", spk1r_src_enum);
static const struct snd_kcontrol_new spk2l_mux =
	SOC_DAPM_ENUM("SPK2L SRC", spk2l_src_enum);
static const struct snd_kcontrol_new spk2r_mux =
	SOC_DAPM_ENUM("SPK2R SRC", spk2r_src_enum);

static const struct snd_soc_dapm_widget wm8995_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("DMIC1DAT"),
	SND_SOC_DAPM_INPUT("DMIC2DAT"),

	SND_SOC_DAPM_INPUT("IN1L"),
	SND_SOC_DAPM_INPUT("IN1R"),

	SND_SOC_DAPM_MIXER("IN1L PGA", SND_SOC_NOPM, 0, 0,
		&in1l_pga, 1),
	SND_SOC_DAPM_MIXER("IN1R PGA", SND_SOC_NOPM, 0, 0,
		&in1r_pga, 1),

	SND_SOC_DAPM_SUPPLY("MICBIAS1", WM8995_POWER_MANAGEMENT_1, 8, 0,
			    NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS2", WM8995_POWER_MANAGEMENT_1, 9, 0,
			    NULL, 0),

	SND_SOC_DAPM_SUPPLY("AIF1CLK", WM8995_AIF1_CLOCKING_1, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("AIF2CLK", WM8995_AIF2_CLOCKING_1, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DSP1CLK", WM8995_CLOCKING_1, 3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DSP2CLK", WM8995_CLOCKING_1, 2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SYSDSPCLK", WM8995_CLOCKING_1, 1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLK_SYS", SND_SOC_NOPM, 0, 0, clk_sys_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_AIF_OUT("AIF1ADC1L", "AIF1 Capture", 0,
		WM8995_POWER_MANAGEMENT_3, 9, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1ADC1R", "AIF1 Capture", 0,
		WM8995_POWER_MANAGEMENT_3, 8, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1ADCDAT", "AIF1 Capture", 0,
	SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1ADC2L", "AIF1 Capture",
		0, WM8995_POWER_MANAGEMENT_3, 11, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1ADC2R", "AIF1 Capture",
		0, WM8995_POWER_MANAGEMENT_3, 10, 0),

	SND_SOC_DAPM_MUX("ADCL Mux", SND_SOC_NOPM, 1, 0, &adcl_mux),
	SND_SOC_DAPM_MUX("ADCR Mux", SND_SOC_NOPM, 0, 0, &adcr_mux),

	SND_SOC_DAPM_ADC("DMIC2L", NULL, WM8995_POWER_MANAGEMENT_3, 5, 0),
	SND_SOC_DAPM_ADC("DMIC2R", NULL, WM8995_POWER_MANAGEMENT_3, 4, 0),
	SND_SOC_DAPM_ADC("DMIC1L", NULL, WM8995_POWER_MANAGEMENT_3, 3, 0),
	SND_SOC_DAPM_ADC("DMIC1R", NULL, WM8995_POWER_MANAGEMENT_3, 2, 0),

	SND_SOC_DAPM_ADC("ADCL", NULL, WM8995_POWER_MANAGEMENT_3, 1, 0),
	SND_SOC_DAPM_ADC("ADCR", NULL, WM8995_POWER_MANAGEMENT_3, 0, 0),

	SND_SOC_DAPM_MIXER("AIF1ADC1L Mixer", SND_SOC_NOPM, 0, 0,
		aif1adc1l_mix, ARRAY_SIZE(aif1adc1l_mix)),
	SND_SOC_DAPM_MIXER("AIF1ADC1R Mixer", SND_SOC_NOPM, 0, 0,
		aif1adc1r_mix, ARRAY_SIZE(aif1adc1r_mix)),
	SND_SOC_DAPM_MIXER("AIF1ADC2L Mixer", SND_SOC_NOPM, 0, 0,
		aif1adc2l_mix, ARRAY_SIZE(aif1adc2l_mix)),
	SND_SOC_DAPM_MIXER("AIF1ADC2R Mixer", SND_SOC_NOPM, 0, 0,
		aif1adc2r_mix, ARRAY_SIZE(aif1adc2r_mix)),

	SND_SOC_DAPM_AIF_IN("AIF1DAC1L", NULL, 0, WM8995_POWER_MANAGEMENT_4,
		9, 0),
	SND_SOC_DAPM_AIF_IN("AIF1DAC1R", NULL, 0, WM8995_POWER_MANAGEMENT_4,
		8, 0),
	SND_SOC_DAPM_AIF_IN("AIF1DACDAT", "AIF1 Playback", 0, SND_SOC_NOPM,
		0, 0),

	SND_SOC_DAPM_AIF_IN("AIF1DAC2L", NULL, 0, WM8995_POWER_MANAGEMENT_4,
		11, 0),
	SND_SOC_DAPM_AIF_IN("AIF1DAC2R", NULL, 0, WM8995_POWER_MANAGEMENT_4,
		10, 0),

	SND_SOC_DAPM_MIXER("AIF2DAC2L Mixer", SND_SOC_NOPM, 0, 0,
		aif2dac2l_mix, ARRAY_SIZE(aif2dac2l_mix)),
	SND_SOC_DAPM_MIXER("AIF2DAC2R Mixer", SND_SOC_NOPM, 0, 0,
		aif2dac2r_mix, ARRAY_SIZE(aif2dac2r_mix)),

	SND_SOC_DAPM_DAC("DAC2L", NULL, WM8995_POWER_MANAGEMENT_4, 3, 0),
	SND_SOC_DAPM_DAC("DAC2R", NULL, WM8995_POWER_MANAGEMENT_4, 2, 0),
	SND_SOC_DAPM_DAC("DAC1L", NULL, WM8995_POWER_MANAGEMENT_4, 1, 0),
	SND_SOC_DAPM_DAC("DAC1R", NULL, WM8995_POWER_MANAGEMENT_4, 0, 0),

	SND_SOC_DAPM_MIXER("DAC1L Mixer", SND_SOC_NOPM, 0, 0, dac1l_mix,
		ARRAY_SIZE(dac1l_mix)),
	SND_SOC_DAPM_MIXER("DAC1R Mixer", SND_SOC_NOPM, 0, 0, dac1r_mix,
		ARRAY_SIZE(dac1r_mix)),

	SND_SOC_DAPM_MUX("Left Sidetone", SND_SOC_NOPM, 0, 0, &sidetone1_mux),
	SND_SOC_DAPM_MUX("Right Sidetone", SND_SOC_NOPM, 0, 0, &sidetone2_mux),

	SND_SOC_DAPM_PGA_E("Headphone PGA", SND_SOC_NOPM, 0, 0, NULL, 0,
		hp_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_SUPPLY("Headphone Supply", SND_SOC_NOPM, 0, 0,
		hp_supply_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_MUX("SPK1L Driver", WM8995_LEFT_PDM_SPEAKER_1,
		4, 0, &spk1l_mux),
	SND_SOC_DAPM_MUX("SPK1R Driver", WM8995_RIGHT_PDM_SPEAKER_1,
		4, 0, &spk1r_mux),
	SND_SOC_DAPM_MUX("SPK2L Driver", WM8995_LEFT_PDM_SPEAKER_2,
		4, 0, &spk2l_mux),
	SND_SOC_DAPM_MUX("SPK2R Driver", WM8995_RIGHT_PDM_SPEAKER_2,
		4, 0, &spk2r_mux),

	SND_SOC_DAPM_SUPPLY("LDO2", WM8995_POWER_MANAGEMENT_2, 1, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("HP1L"),
	SND_SOC_DAPM_OUTPUT("HP1R"),
	SND_SOC_DAPM_OUTPUT("SPK1L"),
	SND_SOC_DAPM_OUTPUT("SPK1R"),
	SND_SOC_DAPM_OUTPUT("SPK2L"),
	SND_SOC_DAPM_OUTPUT("SPK2R")
};

static const struct snd_soc_dapm_route wm8995_intercon[] = {
	{ "CLK_SYS", NULL, "AIF1CLK", check_clk_sys },
	{ "CLK_SYS", NULL, "AIF2CLK", check_clk_sys },

	{ "DSP1CLK", NULL, "CLK_SYS" },
	{ "DSP2CLK", NULL, "CLK_SYS" },
	{ "SYSDSPCLK", NULL, "CLK_SYS" },

	{ "AIF1ADC1L", NULL, "AIF1CLK" },
	{ "AIF1ADC1L", NULL, "DSP1CLK" },
	{ "AIF1ADC1R", NULL, "AIF1CLK" },
	{ "AIF1ADC1R", NULL, "DSP1CLK" },
	{ "AIF1ADC1R", NULL, "SYSDSPCLK" },

	{ "AIF1ADC2L", NULL, "AIF1CLK" },
	{ "AIF1ADC2L", NULL, "DSP1CLK" },
	{ "AIF1ADC2R", NULL, "AIF1CLK" },
	{ "AIF1ADC2R", NULL, "DSP1CLK" },
	{ "AIF1ADC2R", NULL, "SYSDSPCLK" },

	{ "DMIC1L", NULL, "DMIC1DAT" },
	{ "DMIC1L", NULL, "CLK_SYS" },
	{ "DMIC1R", NULL, "DMIC1DAT" },
	{ "DMIC1R", NULL, "CLK_SYS" },
	{ "DMIC2L", NULL, "DMIC2DAT" },
	{ "DMIC2L", NULL, "CLK_SYS" },
	{ "DMIC2R", NULL, "DMIC2DAT" },
	{ "DMIC2R", NULL, "CLK_SYS" },

	{ "ADCL", NULL, "AIF1CLK" },
	{ "ADCL", NULL, "DSP1CLK" },
	{ "ADCL", NULL, "SYSDSPCLK" },

	{ "ADCR", NULL, "AIF1CLK" },
	{ "ADCR", NULL, "DSP1CLK" },
	{ "ADCR", NULL, "SYSDSPCLK" },

	{ "IN1L PGA", "IN1L Switch", "IN1L" },
	{ "IN1R PGA", "IN1R Switch", "IN1R" },
	{ "IN1L PGA", NULL, "LDO2" },
	{ "IN1R PGA", NULL, "LDO2" },

	{ "ADCL", NULL, "IN1L PGA" },
	{ "ADCR", NULL, "IN1R PGA" },

	{ "ADCL Mux", "ADC", "ADCL" },
	{ "ADCL Mux", "DMIC", "DMIC1L" },
	{ "ADCR Mux", "ADC", "ADCR" },
	{ "ADCR Mux", "DMIC", "DMIC1R" },

	/* AIF1 outputs */
	{ "AIF1ADC1L", NULL, "AIF1ADC1L Mixer" },
	{ "AIF1ADC1L Mixer", "ADC/DMIC Switch", "ADCL Mux" },

	{ "AIF1ADC1R", NULL, "AIF1ADC1R Mixer" },
	{ "AIF1ADC1R Mixer", "ADC/DMIC Switch", "ADCR Mux" },

	{ "AIF1ADC2L", NULL, "AIF1ADC2L Mixer" },
	{ "AIF1ADC2L Mixer", "DMIC Switch", "DMIC2L" },

	{ "AIF1ADC2R", NULL, "AIF1ADC2R Mixer" },
	{ "AIF1ADC2R Mixer", "DMIC Switch", "DMIC2R" },

	/* Sidetone */
	{ "Left Sidetone", "ADC/DMIC1", "AIF1ADC1L" },
	{ "Left Sidetone", "DMIC2", "AIF1ADC2L" },
	{ "Right Sidetone", "ADC/DMIC1", "AIF1ADC1R" },
	{ "Right Sidetone", "DMIC2", "AIF1ADC2R" },

	{ "AIF1DAC1L", NULL, "AIF1CLK" },
	{ "AIF1DAC1L", NULL, "DSP1CLK" },
	{ "AIF1DAC1R", NULL, "AIF1CLK" },
	{ "AIF1DAC1R", NULL, "DSP1CLK" },
	{ "AIF1DAC1R", NULL, "SYSDSPCLK" },

	{ "AIF1DAC2L", NULL, "AIF1CLK" },
	{ "AIF1DAC2L", NULL, "DSP1CLK" },
	{ "AIF1DAC2R", NULL, "AIF1CLK" },
	{ "AIF1DAC2R", NULL, "DSP1CLK" },
	{ "AIF1DAC2R", NULL, "SYSDSPCLK" },

	{ "DAC1L", NULL, "AIF1CLK" },
	{ "DAC1L", NULL, "DSP1CLK" },
	{ "DAC1L", NULL, "SYSDSPCLK" },

	{ "DAC1R", NULL, "AIF1CLK" },
	{ "DAC1R", NULL, "DSP1CLK" },
	{ "DAC1R", NULL, "SYSDSPCLK" },

	{ "AIF1DAC1L", NULL, "AIF1DACDAT" },
	{ "AIF1DAC1R", NULL, "AIF1DACDAT" },
	{ "AIF1DAC2L", NULL, "AIF1DACDAT" },
	{ "AIF1DAC2R", NULL, "AIF1DACDAT" },

	/* DAC1 inputs */
	{ "DAC1L", NULL, "DAC1L Mixer" },
	{ "DAC1L Mixer", "AIF1.1 Switch", "AIF1DAC1L" },
	{ "DAC1L Mixer", "AIF1.2 Switch", "AIF1DAC2L" },
	{ "DAC1L Mixer", "Left Sidetone Switch", "Left Sidetone" },
	{ "DAC1L Mixer", "Right Sidetone Switch", "Right Sidetone" },

	{ "DAC1R", NULL, "DAC1R Mixer" },
	{ "DAC1R Mixer", "AIF1.1 Switch", "AIF1DAC1R" },
	{ "DAC1R Mixer", "AIF1.2 Switch", "AIF1DAC2R" },
	{ "DAC1R Mixer", "Left Sidetone Switch", "Left Sidetone" },
	{ "DAC1R Mixer", "Right Sidetone Switch", "Right Sidetone" },

	/* DAC2/AIF2 outputs */
	{ "DAC2L", NULL, "AIF2DAC2L Mixer" },
	{ "AIF2DAC2L Mixer", "AIF1.2 Switch", "AIF1DAC2L" },
	{ "AIF2DAC2L Mixer", "AIF1.1 Switch", "AIF1DAC1L" },

	{ "DAC2R", NULL, "AIF2DAC2R Mixer" },
	{ "AIF2DAC2R Mixer", "AIF1.2 Switch", "AIF1DAC2R" },
	{ "AIF2DAC2R Mixer", "AIF1.1 Switch", "AIF1DAC1R" },

	/* Output stages */
	{ "Headphone PGA", NULL, "DAC1L" },
	{ "Headphone PGA", NULL, "DAC1R" },

	{ "Headphone PGA", NULL, "DAC2L" },
	{ "Headphone PGA", NULL, "DAC2R" },

	{ "Headphone PGA", NULL, "Headphone Supply" },
	{ "Headphone PGA", NULL, "CLK_SYS" },
	{ "Headphone PGA", NULL, "LDO2" },

	{ "HP1L", NULL, "Headphone PGA" },
	{ "HP1R", NULL, "Headphone PGA" },

	{ "SPK1L Driver", "DAC1L", "DAC1L" },
	{ "SPK1L Driver", "DAC1R", "DAC1R" },
	{ "SPK1L Driver", "DAC2L", "DAC2L" },
	{ "SPK1L Driver", "DAC2R", "DAC2R" },
	{ "SPK1L Driver", NULL, "CLK_SYS" },

	{ "SPK1R Driver", "DAC1L", "DAC1L" },
	{ "SPK1R Driver", "DAC1R", "DAC1R" },
	{ "SPK1R Driver", "DAC2L", "DAC2L" },
	{ "SPK1R Driver", "DAC2R", "DAC2R" },
	{ "SPK1R Driver", NULL, "CLK_SYS" },

	{ "SPK2L Driver", "DAC1L", "DAC1L" },
	{ "SPK2L Driver", "DAC1R", "DAC1R" },
	{ "SPK2L Driver", "DAC2L", "DAC2L" },
	{ "SPK2L Driver", "DAC2R", "DAC2R" },
	{ "SPK2L Driver", NULL, "CLK_SYS" },

	{ "SPK2R Driver", "DAC1L", "DAC1L" },
	{ "SPK2R Driver", "DAC1R", "DAC1R" },
	{ "SPK2R Driver", "DAC2L", "DAC2L" },
	{ "SPK2R Driver", "DAC2R", "DAC2R" },
	{ "SPK2R Driver", NULL, "CLK_SYS" },

	{ "SPK1L", NULL, "SPK1L Driver" },
	{ "SPK1R", NULL, "SPK1R Driver" },
	{ "SPK2L", NULL, "SPK2L Driver" },
	{ "SPK2R", NULL, "SPK2R Driver" }
};

static bool wm8995_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8995_SOFTWARE_RESET:
	case WM8995_POWER_MANAGEMENT_1:
	case WM8995_POWER_MANAGEMENT_2:
	case WM8995_POWER_MANAGEMENT_3:
	case WM8995_POWER_MANAGEMENT_4:
	case WM8995_POWER_MANAGEMENT_5:
	case WM8995_LEFT_LINE_INPUT_1_VOLUME:
	case WM8995_RIGHT_LINE_INPUT_1_VOLUME:
	case WM8995_LEFT_LINE_INPUT_CONTROL:
	case WM8995_DAC1_LEFT_VOLUME:
	case WM8995_DAC1_RIGHT_VOLUME:
	case WM8995_DAC2_LEFT_VOLUME:
	case WM8995_DAC2_RIGHT_VOLUME:
	case WM8995_OUTPUT_VOLUME_ZC_1:
	case WM8995_MICBIAS_1:
	case WM8995_MICBIAS_2:
	case WM8995_LDO_1:
	case WM8995_LDO_2:
	case WM8995_ACCESSORY_DETECT_MODE1:
	case WM8995_ACCESSORY_DETECT_MODE2:
	case WM8995_HEADPHONE_DETECT1:
	case WM8995_HEADPHONE_DETECT2:
	case WM8995_MIC_DETECT_1:
	case WM8995_MIC_DETECT_2:
	case WM8995_CHARGE_PUMP_1:
	case WM8995_CLASS_W_1:
	case WM8995_DC_SERVO_1:
	case WM8995_DC_SERVO_2:
	case WM8995_DC_SERVO_3:
	case WM8995_DC_SERVO_5:
	case WM8995_DC_SERVO_6:
	case WM8995_DC_SERVO_7:
	case WM8995_DC_SERVO_READBACK_0:
	case WM8995_ANALOGUE_HP_1:
	case WM8995_ANALOGUE_HP_2:
	case WM8995_CHIP_REVISION:
	case WM8995_CONTROL_INTERFACE_1:
	case WM8995_CONTROL_INTERFACE_2:
	case WM8995_WRITE_SEQUENCER_CTRL_1:
	case WM8995_WRITE_SEQUENCER_CTRL_2:
	case WM8995_AIF1_CLOCKING_1:
	case WM8995_AIF1_CLOCKING_2:
	case WM8995_AIF2_CLOCKING_1:
	case WM8995_AIF2_CLOCKING_2:
	case WM8995_CLOCKING_1:
	case WM8995_CLOCKING_2:
	case WM8995_AIF1_RATE:
	case WM8995_AIF2_RATE:
	case WM8995_RATE_STATUS:
	case WM8995_FLL1_CONTROL_1:
	case WM8995_FLL1_CONTROL_2:
	case WM8995_FLL1_CONTROL_3:
	case WM8995_FLL1_CONTROL_4:
	case WM8995_FLL1_CONTROL_5:
	case WM8995_FLL2_CONTROL_1:
	case WM8995_FLL2_CONTROL_2:
	case WM8995_FLL2_CONTROL_3:
	case WM8995_FLL2_CONTROL_4:
	case WM8995_FLL2_CONTROL_5:
	case WM8995_AIF1_CONTROL_1:
	case WM8995_AIF1_CONTROL_2:
	case WM8995_AIF1_MASTER_SLAVE:
	case WM8995_AIF1_BCLK:
	case WM8995_AIF1ADC_LRCLK:
	case WM8995_AIF1DAC_LRCLK:
	case WM8995_AIF1DAC_DATA:
	case WM8995_AIF1ADC_DATA:
	case WM8995_AIF2_CONTROL_1:
	case WM8995_AIF2_CONTROL_2:
	case WM8995_AIF2_MASTER_SLAVE:
	case WM8995_AIF2_BCLK:
	case WM8995_AIF2ADC_LRCLK:
	case WM8995_AIF2DAC_LRCLK:
	case WM8995_AIF2DAC_DATA:
	case WM8995_AIF2ADC_DATA:
	case WM8995_AIF1_ADC1_LEFT_VOLUME:
	case WM8995_AIF1_ADC1_RIGHT_VOLUME:
	case WM8995_AIF1_DAC1_LEFT_VOLUME:
	case WM8995_AIF1_DAC1_RIGHT_VOLUME:
	case WM8995_AIF1_ADC2_LEFT_VOLUME:
	case WM8995_AIF1_ADC2_RIGHT_VOLUME:
	case WM8995_AIF1_DAC2_LEFT_VOLUME:
	case WM8995_AIF1_DAC2_RIGHT_VOLUME:
	case WM8995_AIF1_ADC1_FILTERS:
	case WM8995_AIF1_ADC2_FILTERS:
	case WM8995_AIF1_DAC1_FILTERS_1:
	case WM8995_AIF1_DAC1_FILTERS_2:
	case WM8995_AIF1_DAC2_FILTERS_1:
	case WM8995_AIF1_DAC2_FILTERS_2:
	case WM8995_AIF1_DRC1_1:
	case WM8995_AIF1_DRC1_2:
	case WM8995_AIF1_DRC1_3:
	case WM8995_AIF1_DRC1_4:
	case WM8995_AIF1_DRC1_5:
	case WM8995_AIF1_DRC2_1:
	case WM8995_AIF1_DRC2_2:
	case WM8995_AIF1_DRC2_3:
	case WM8995_AIF1_DRC2_4:
	case WM8995_AIF1_DRC2_5:
	case WM8995_AIF1_DAC1_EQ_GAINS_1:
	case WM8995_AIF1_DAC1_EQ_GAINS_2:
	case WM8995_AIF1_DAC1_EQ_BAND_1_A:
	case WM8995_AIF1_DAC1_EQ_BAND_1_B:
	case WM8995_AIF1_DAC1_EQ_BAND_1_PG:
	case WM8995_AIF1_DAC1_EQ_BAND_2_A:
	case WM8995_AIF1_DAC1_EQ_BAND_2_B:
	case WM8995_AIF1_DAC1_EQ_BAND_2_C:
	case WM8995_AIF1_DAC1_EQ_BAND_2_PG:
	case WM8995_AIF1_DAC1_EQ_BAND_3_A:
	case WM8995_AIF1_DAC1_EQ_BAND_3_B:
	case WM8995_AIF1_DAC1_EQ_BAND_3_C:
	case WM8995_AIF1_DAC1_EQ_BAND_3_PG:
	case WM8995_AIF1_DAC1_EQ_BAND_4_A:
	case WM8995_AIF1_DAC1_EQ_BAND_4_B:
	case WM8995_AIF1_DAC1_EQ_BAND_4_C:
	case WM8995_AIF1_DAC1_EQ_BAND_4_PG:
	case WM8995_AIF1_DAC1_EQ_BAND_5_A:
	case WM8995_AIF1_DAC1_EQ_BAND_5_B:
	case WM8995_AIF1_DAC1_EQ_BAND_5_PG:
	case WM8995_AIF1_DAC2_EQ_GAINS_1:
	case WM8995_AIF1_DAC2_EQ_GAINS_2:
	case WM8995_AIF1_DAC2_EQ_BAND_1_A:
	case WM8995_AIF1_DAC2_EQ_BAND_1_B:
	case WM8995_AIF1_DAC2_EQ_BAND_1_PG:
	case WM8995_AIF1_DAC2_EQ_BAND_2_A:
	case WM8995_AIF1_DAC2_EQ_BAND_2_B:
	case WM8995_AIF1_DAC2_EQ_BAND_2_C:
	case WM8995_AIF1_DAC2_EQ_BAND_2_PG:
	case WM8995_AIF1_DAC2_EQ_BAND_3_A:
	case WM8995_AIF1_DAC2_EQ_BAND_3_B:
	case WM8995_AIF1_DAC2_EQ_BAND_3_C:
	case WM8995_AIF1_DAC2_EQ_BAND_3_PG:
	case WM8995_AIF1_DAC2_EQ_BAND_4_A:
	case WM8995_AIF1_DAC2_EQ_BAND_4_B:
	case WM8995_AIF1_DAC2_EQ_BAND_4_C:
	case WM8995_AIF1_DAC2_EQ_BAND_4_PG:
	case WM8995_AIF1_DAC2_EQ_BAND_5_A:
	case WM8995_AIF1_DAC2_EQ_BAND_5_B:
	case WM8995_AIF1_DAC2_EQ_BAND_5_PG:
	case WM8995_AIF2_ADC_LEFT_VOLUME:
	case WM8995_AIF2_ADC_RIGHT_VOLUME:
	case WM8995_AIF2_DAC_LEFT_VOLUME:
	case WM8995_AIF2_DAC_RIGHT_VOLUME:
	case WM8995_AIF2_ADC_FILTERS:
	case WM8995_AIF2_DAC_FILTERS_1:
	case WM8995_AIF2_DAC_FILTERS_2:
	case WM8995_AIF2_DRC_1:
	case WM8995_AIF2_DRC_2:
	case WM8995_AIF2_DRC_3:
	case WM8995_AIF2_DRC_4:
	case WM8995_AIF2_DRC_5:
	case WM8995_AIF2_EQ_GAINS_1:
	case WM8995_AIF2_EQ_GAINS_2:
	case WM8995_AIF2_EQ_BAND_1_A:
	case WM8995_AIF2_EQ_BAND_1_B:
	case WM8995_AIF2_EQ_BAND_1_PG:
	case WM8995_AIF2_EQ_BAND_2_A:
	case WM8995_AIF2_EQ_BAND_2_B:
	case WM8995_AIF2_EQ_BAND_2_C:
	case WM8995_AIF2_EQ_BAND_2_PG:
	case WM8995_AIF2_EQ_BAND_3_A:
	case WM8995_AIF2_EQ_BAND_3_B:
	case WM8995_AIF2_EQ_BAND_3_C:
	case WM8995_AIF2_EQ_BAND_3_PG:
	case WM8995_AIF2_EQ_BAND_4_A:
	case WM8995_AIF2_EQ_BAND_4_B:
	case WM8995_AIF2_EQ_BAND_4_C:
	case WM8995_AIF2_EQ_BAND_4_PG:
	case WM8995_AIF2_EQ_BAND_5_A:
	case WM8995_AIF2_EQ_BAND_5_B:
	case WM8995_AIF2_EQ_BAND_5_PG:
	case WM8995_DAC1_MIXER_VOLUMES:
	case WM8995_DAC1_LEFT_MIXER_ROUTING:
	case WM8995_DAC1_RIGHT_MIXER_ROUTING:
	case WM8995_DAC2_MIXER_VOLUMES:
	case WM8995_DAC2_LEFT_MIXER_ROUTING:
	case WM8995_DAC2_RIGHT_MIXER_ROUTING:
	case WM8995_AIF1_ADC1_LEFT_MIXER_ROUTING:
	case WM8995_AIF1_ADC1_RIGHT_MIXER_ROUTING:
	case WM8995_AIF1_ADC2_LEFT_MIXER_ROUTING:
	case WM8995_AIF1_ADC2_RIGHT_MIXER_ROUTING:
	case WM8995_DAC_SOFTMUTE:
	case WM8995_OVERSAMPLING:
	case WM8995_SIDETONE:
	case WM8995_GPIO_1:
	case WM8995_GPIO_2:
	case WM8995_GPIO_3:
	case WM8995_GPIO_4:
	case WM8995_GPIO_5:
	case WM8995_GPIO_6:
	case WM8995_GPIO_7:
	case WM8995_GPIO_8:
	case WM8995_GPIO_9:
	case WM8995_GPIO_10:
	case WM8995_GPIO_11:
	case WM8995_GPIO_12:
	case WM8995_GPIO_13:
	case WM8995_GPIO_14:
	case WM8995_PULL_CONTROL_1:
	case WM8995_PULL_CONTROL_2:
	case WM8995_INTERRUPT_STATUS_1:
	case WM8995_INTERRUPT_STATUS_2:
	case WM8995_INTERRUPT_RAW_STATUS_2:
	case WM8995_INTERRUPT_STATUS_1_MASK:
	case WM8995_INTERRUPT_STATUS_2_MASK:
	case WM8995_INTERRUPT_CONTROL:
	case WM8995_LEFT_PDM_SPEAKER_1:
	case WM8995_RIGHT_PDM_SPEAKER_1:
	case WM8995_PDM_SPEAKER_1_MUTE_SEQUENCE:
	case WM8995_LEFT_PDM_SPEAKER_2:
	case WM8995_RIGHT_PDM_SPEAKER_2:
	case WM8995_PDM_SPEAKER_2_MUTE_SEQUENCE:
		return true;
	default:
		return false;
	}
}

static bool wm8995_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8995_SOFTWARE_RESET:
	case WM8995_DC_SERVO_READBACK_0:
	case WM8995_INTERRUPT_STATUS_1:
	case WM8995_INTERRUPT_STATUS_2:
	case WM8995_INTERRUPT_CONTROL:
	case WM8995_ACCESSORY_DETECT_MODE1:
	case WM8995_ACCESSORY_DETECT_MODE2:
	case WM8995_HEADPHONE_DETECT1:
	case WM8995_HEADPHONE_DETECT2:
	case WM8995_RATE_STATUS:
		return true;
	default:
		return false;
	}
}

static int wm8995_aif_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	int mute_reg;

	switch (dai->id) {
	case 0:
		mute_reg = WM8995_AIF1_DAC1_FILTERS_1;
		break;
	case 1:
		mute_reg = WM8995_AIF2_DAC_FILTERS_1;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, mute_reg, WM8995_AIF1DAC1_MUTE_MASK,
			    !!mute << WM8995_AIF1DAC1_MUTE_SHIFT);
	return 0;
}

static int wm8995_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component;
	int master;
	int aif;

	component = dai->component;

	master = 0;
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		master = WM8995_AIF1_MSTR;
		break;
	default:
		dev_err(dai->dev, "Unknown master/slave configuration\n");
		return -EINVAL;
	}

	aif = 0;
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_B:
		aif |= WM8995_AIF1_LRCLK_INV;
		fallthrough;
	case SND_SOC_DAIFMT_DSP_A:
		aif |= (0x3 << WM8995_AIF1_FMT_SHIFT);
		break;
	case SND_SOC_DAIFMT_I2S:
		aif |= (0x2 << WM8995_AIF1_FMT_SHIFT);
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aif |= (0x1 << WM8995_AIF1_FMT_SHIFT);
		break;
	default:
		dev_err(dai->dev, "Unknown dai format\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		/* frame inversion not valid for DSP modes */
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif |= WM8995_AIF1_BCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;

	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			aif |= WM8995_AIF1_BCLK_INV | WM8995_AIF1_LRCLK_INV;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif |= WM8995_AIF1_BCLK_INV;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			aif |= WM8995_AIF1_LRCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, WM8995_AIF1_CONTROL_1,
			    WM8995_AIF1_BCLK_INV_MASK |
			    WM8995_AIF1_LRCLK_INV_MASK |
			    WM8995_AIF1_FMT_MASK, aif);
	snd_soc_component_update_bits(component, WM8995_AIF1_MASTER_SLAVE,
			    WM8995_AIF1_MSTR_MASK, master);
	return 0;
}

static const int srs[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100,
	48000, 88200, 96000
};

static const int fs_ratios[] = {
	-1 /* reserved */,
	128, 192, 256, 384, 512, 768, 1024, 1408, 1536
};

static const int bclk_divs[] = {
	10, 15, 20, 30, 40, 55, 60, 80, 110, 120, 160, 220, 240, 320, 440, 480
};

static int wm8995_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component;
	struct wm8995_priv *wm8995;
	int aif1_reg;
	int bclk_reg;
	int lrclk_reg;
	int rate_reg;
	int bclk_rate;
	int aif1;
	int lrclk, bclk;
	int i, rate_val, best, best_val, cur_val;

	component = dai->component;
	wm8995 = snd_soc_component_get_drvdata(component);

	switch (dai->id) {
	case 0:
		aif1_reg = WM8995_AIF1_CONTROL_1;
		bclk_reg = WM8995_AIF1_BCLK;
		rate_reg = WM8995_AIF1_RATE;
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK /* ||
			wm8995->lrclk_shared[0] */) {
			lrclk_reg = WM8995_AIF1DAC_LRCLK;
		} else {
			lrclk_reg = WM8995_AIF1ADC_LRCLK;
			dev_dbg(component->dev, "AIF1 using split LRCLK\n");
		}
		break;
	case 1:
		aif1_reg = WM8995_AIF2_CONTROL_1;
		bclk_reg = WM8995_AIF2_BCLK;
		rate_reg = WM8995_AIF2_RATE;
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK /* ||
		    wm8995->lrclk_shared[1] */) {
			lrclk_reg = WM8995_AIF2DAC_LRCLK;
		} else {
			lrclk_reg = WM8995_AIF2ADC_LRCLK;
			dev_dbg(component->dev, "AIF2 using split LRCLK\n");
		}
		break;
	default:
		return -EINVAL;
	}

	bclk_rate = snd_soc_params_to_bclk(params);
	if (bclk_rate < 0)
		return bclk_rate;

	aif1 = 0;
	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		aif1 |= (0x1 << WM8995_AIF1_WL_SHIFT);
		break;
	case 24:
		aif1 |= (0x2 << WM8995_AIF1_WL_SHIFT);
		break;
	case 32:
		aif1 |= (0x3 << WM8995_AIF1_WL_SHIFT);
		break;
	default:
		dev_err(dai->dev, "Unsupported word length %u\n",
			params_width(params));
		return -EINVAL;
	}

	/* try to find a suitable sample rate */
	for (i = 0; i < ARRAY_SIZE(srs); ++i)
		if (srs[i] == params_rate(params))
			break;
	if (i == ARRAY_SIZE(srs)) {
		dev_err(dai->dev, "Sample rate %d is not supported\n",
			params_rate(params));
		return -EINVAL;
	}
	rate_val = i << WM8995_AIF1_SR_SHIFT;

	dev_dbg(dai->dev, "Sample rate is %dHz\n", srs[i]);
	dev_dbg(dai->dev, "AIF%dCLK is %dHz, target BCLK %dHz\n",
		dai->id + 1, wm8995->aifclk[dai->id], bclk_rate);

	/* AIFCLK/fs ratio; look for a close match in either direction */
	best = 1;
	best_val = abs((fs_ratios[1] * params_rate(params))
		       - wm8995->aifclk[dai->id]);
	for (i = 2; i < ARRAY_SIZE(fs_ratios); i++) {
		cur_val = abs((fs_ratios[i] * params_rate(params))
			      - wm8995->aifclk[dai->id]);
		if (cur_val >= best_val)
			continue;
		best = i;
		best_val = cur_val;
	}
	rate_val |= best;

	dev_dbg(dai->dev, "Selected AIF%dCLK/fs = %d\n",
		dai->id + 1, fs_ratios[best]);

	/*
	 * We may not get quite the right frequency if using
	 * approximate clocks so look for the closest match that is
	 * higher than the target (we need to ensure that there enough
	 * BCLKs to clock out the samples).
	 */
	best = 0;
	bclk = 0;
	for (i = 0; i < ARRAY_SIZE(bclk_divs); i++) {
		cur_val = (wm8995->aifclk[dai->id] * 10 / bclk_divs[i]) - bclk_rate;
		if (cur_val < 0) /* BCLK table is sorted */
			break;
		best = i;
	}
	bclk |= best << WM8995_AIF1_BCLK_DIV_SHIFT;

	bclk_rate = wm8995->aifclk[dai->id] * 10 / bclk_divs[best];
	dev_dbg(dai->dev, "Using BCLK_DIV %d for actual BCLK %dHz\n",
		bclk_divs[best], bclk_rate);

	lrclk = bclk_rate / params_rate(params);
	dev_dbg(dai->dev, "Using LRCLK rate %d for actual LRCLK %dHz\n",
		lrclk, bclk_rate / lrclk);

	snd_soc_component_update_bits(component, aif1_reg,
			    WM8995_AIF1_WL_MASK, aif1);
	snd_soc_component_update_bits(component, bclk_reg,
			    WM8995_AIF1_BCLK_DIV_MASK, bclk);
	snd_soc_component_update_bits(component, lrclk_reg,
			    WM8995_AIF1DAC_RATE_MASK, lrclk);
	snd_soc_component_update_bits(component, rate_reg,
			    WM8995_AIF1_SR_MASK |
			    WM8995_AIF1CLK_RATE_MASK, rate_val);
	return 0;
}

static int wm8995_set_tristate(struct snd_soc_dai *codec_dai, int tristate)
{
	struct snd_soc_component *component = codec_dai->component;
	int reg, val, mask;

	switch (codec_dai->id) {
	case 0:
		reg = WM8995_AIF1_MASTER_SLAVE;
		mask = WM8995_AIF1_TRI;
		break;
	case 1:
		reg = WM8995_AIF2_MASTER_SLAVE;
		mask = WM8995_AIF2_TRI;
		break;
	case 2:
		reg = WM8995_POWER_MANAGEMENT_5;
		mask = WM8995_AIF3_TRI;
		break;
	default:
		return -EINVAL;
	}

	if (tristate)
		val = mask;
	else
		val = 0;

	return snd_soc_component_update_bits(component, reg, mask, val);
}

/* The size in bits of the FLL divide multiplied by 10
 * to allow rounding later */
#define FIXED_FLL_SIZE ((1 << 16) * 10)

struct fll_div {
	u16 outdiv;
	u16 n;
	u16 k;
	u16 clk_ref_div;
	u16 fll_fratio;
};

static int wm8995_get_fll_config(struct fll_div *fll,
				 int freq_in, int freq_out)
{
	u64 Kpart;
	unsigned int K, Ndiv, Nmod;

	pr_debug("FLL input=%dHz, output=%dHz\n", freq_in, freq_out);

	/* Scale the input frequency down to <= 13.5MHz */
	fll->clk_ref_div = 0;
	while (freq_in > 13500000) {
		fll->clk_ref_div++;
		freq_in /= 2;

		if (fll->clk_ref_div > 3)
			return -EINVAL;
	}
	pr_debug("CLK_REF_DIV=%d, Fref=%dHz\n", fll->clk_ref_div, freq_in);

	/* Scale the output to give 90MHz<=Fvco<=100MHz */
	fll->outdiv = 3;
	while (freq_out * (fll->outdiv + 1) < 90000000) {
		fll->outdiv++;
		if (fll->outdiv > 63)
			return -EINVAL;
	}
	freq_out *= fll->outdiv + 1;
	pr_debug("OUTDIV=%d, Fvco=%dHz\n", fll->outdiv, freq_out);

	if (freq_in > 1000000) {
		fll->fll_fratio = 0;
	} else if (freq_in > 256000) {
		fll->fll_fratio = 1;
		freq_in *= 2;
	} else if (freq_in > 128000) {
		fll->fll_fratio = 2;
		freq_in *= 4;
	} else if (freq_in > 64000) {
		fll->fll_fratio = 3;
		freq_in *= 8;
	} else {
		fll->fll_fratio = 4;
		freq_in *= 16;
	}
	pr_debug("FLL_FRATIO=%d, Fref=%dHz\n", fll->fll_fratio, freq_in);

	/* Now, calculate N.K */
	Ndiv = freq_out / freq_in;

	fll->n = Ndiv;
	Nmod = freq_out % freq_in;
	pr_debug("Nmod=%d\n", Nmod);

	/* Calculate fractional part - scale up so we can round. */
	Kpart = FIXED_FLL_SIZE * (long long)Nmod;

	do_div(Kpart, freq_in);

	K = Kpart & 0xFFFFFFFF;

	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	fll->k = K / 10;

	pr_debug("N=%x K=%x\n", fll->n, fll->k);

	return 0;
}

static int wm8995_set_fll(struct snd_soc_dai *dai, int id,
			  int src, unsigned int freq_in,
			  unsigned int freq_out)
{
	struct snd_soc_component *component;
	struct wm8995_priv *wm8995;
	int reg_offset, ret;
	struct fll_div fll;
	u16 reg, aif1, aif2;

	component = dai->component;
	wm8995 = snd_soc_component_get_drvdata(component);

	aif1 = snd_soc_component_read(component, WM8995_AIF1_CLOCKING_1)
	       & WM8995_AIF1CLK_ENA;

	aif2 = snd_soc_component_read(component, WM8995_AIF2_CLOCKING_1)
	       & WM8995_AIF2CLK_ENA;

	switch (id) {
	case WM8995_FLL1:
		reg_offset = 0;
		id = 0;
		break;
	case WM8995_FLL2:
		reg_offset = 0x20;
		id = 1;
		break;
	default:
		return -EINVAL;
	}

	switch (src) {
	case 0:
		/* Allow no source specification when stopping */
		if (freq_out)
			return -EINVAL;
		break;
	case WM8995_FLL_SRC_MCLK1:
	case WM8995_FLL_SRC_MCLK2:
	case WM8995_FLL_SRC_LRCLK:
	case WM8995_FLL_SRC_BCLK:
		break;
	default:
		return -EINVAL;
	}

	/* Are we changing anything? */
	if (wm8995->fll[id].src == src &&
	    wm8995->fll[id].in == freq_in && wm8995->fll[id].out == freq_out)
		return 0;

	/* If we're stopping the FLL redo the old config - no
	 * registers will actually be written but we avoid GCC flow
	 * analysis bugs spewing warnings.
	 */
	if (freq_out)
		ret = wm8995_get_fll_config(&fll, freq_in, freq_out);
	else
		ret = wm8995_get_fll_config(&fll, wm8995->fll[id].in,
					    wm8995->fll[id].out);
	if (ret < 0)
		return ret;

	/* Gate the AIF clocks while we reclock */
	snd_soc_component_update_bits(component, WM8995_AIF1_CLOCKING_1,
			    WM8995_AIF1CLK_ENA_MASK, 0);
	snd_soc_component_update_bits(component, WM8995_AIF2_CLOCKING_1,
			    WM8995_AIF2CLK_ENA_MASK, 0);

	/* We always need to disable the FLL while reconfiguring */
	snd_soc_component_update_bits(component, WM8995_FLL1_CONTROL_1 + reg_offset,
			    WM8995_FLL1_ENA_MASK, 0);

	reg = (fll.outdiv << WM8995_FLL1_OUTDIV_SHIFT) |
	      (fll.fll_fratio << WM8995_FLL1_FRATIO_SHIFT);
	snd_soc_component_update_bits(component, WM8995_FLL1_CONTROL_2 + reg_offset,
			    WM8995_FLL1_OUTDIV_MASK |
			    WM8995_FLL1_FRATIO_MASK, reg);

	snd_soc_component_write(component, WM8995_FLL1_CONTROL_3 + reg_offset, fll.k);

	snd_soc_component_update_bits(component, WM8995_FLL1_CONTROL_4 + reg_offset,
			    WM8995_FLL1_N_MASK,
			    fll.n << WM8995_FLL1_N_SHIFT);

	snd_soc_component_update_bits(component, WM8995_FLL1_CONTROL_5 + reg_offset,
			    WM8995_FLL1_REFCLK_DIV_MASK |
			    WM8995_FLL1_REFCLK_SRC_MASK,
			    (fll.clk_ref_div << WM8995_FLL1_REFCLK_DIV_SHIFT) |
			    (src - 1));

	if (freq_out)
		snd_soc_component_update_bits(component, WM8995_FLL1_CONTROL_1 + reg_offset,
				    WM8995_FLL1_ENA_MASK, WM8995_FLL1_ENA);

	wm8995->fll[id].in = freq_in;
	wm8995->fll[id].out = freq_out;
	wm8995->fll[id].src = src;

	/* Enable any gated AIF clocks */
	snd_soc_component_update_bits(component, WM8995_AIF1_CLOCKING_1,
			    WM8995_AIF1CLK_ENA_MASK, aif1);
	snd_soc_component_update_bits(component, WM8995_AIF2_CLOCKING_1,
			    WM8995_AIF2CLK_ENA_MASK, aif2);

	configure_clock(component);

	return 0;
}

static int wm8995_set_dai_sysclk(struct snd_soc_dai *dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component;
	struct wm8995_priv *wm8995;

	component = dai->component;
	wm8995 = snd_soc_component_get_drvdata(component);

	switch (dai->id) {
	case 0:
	case 1:
		break;
	default:
		/* AIF3 shares clocking with AIF1/2 */
		return -EINVAL;
	}

	switch (clk_id) {
	case WM8995_SYSCLK_MCLK1:
		wm8995->sysclk[dai->id] = WM8995_SYSCLK_MCLK1;
		wm8995->mclk[0] = freq;
		dev_dbg(dai->dev, "AIF%d using MCLK1 at %uHz\n",
			dai->id + 1, freq);
		break;
	case WM8995_SYSCLK_MCLK2:
		wm8995->sysclk[dai->id] = WM8995_SYSCLK_MCLK2;
		wm8995->mclk[1] = freq;
		dev_dbg(dai->dev, "AIF%d using MCLK2 at %uHz\n",
			dai->id + 1, freq);
		break;
	case WM8995_SYSCLK_FLL1:
		wm8995->sysclk[dai->id] = WM8995_SYSCLK_FLL1;
		dev_dbg(dai->dev, "AIF%d using FLL1\n", dai->id + 1);
		break;
	case WM8995_SYSCLK_FLL2:
		wm8995->sysclk[dai->id] = WM8995_SYSCLK_FLL2;
		dev_dbg(dai->dev, "AIF%d using FLL2\n", dai->id + 1);
		break;
	case WM8995_SYSCLK_OPCLK:
	default:
		dev_err(dai->dev, "Unknown clock source %d\n", clk_id);
		return -EINVAL;
	}

	configure_clock(component);

	return 0;
}

static int wm8995_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct wm8995_priv *wm8995;
	int ret;

	wm8995 = snd_soc_component_get_drvdata(component);
	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(wm8995->supplies),
						    wm8995->supplies);
			if (ret)
				return ret;

			ret = regcache_sync(wm8995->regmap);
			if (ret) {
				dev_err(component->dev,
					"Failed to sync cache: %d\n", ret);
				return ret;
			}

			snd_soc_component_update_bits(component, WM8995_POWER_MANAGEMENT_1,
					    WM8995_BG_ENA_MASK, WM8995_BG_ENA);
		}
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_component_update_bits(component, WM8995_POWER_MANAGEMENT_1,
				    WM8995_BG_ENA_MASK, 0);
		regulator_bulk_disable(ARRAY_SIZE(wm8995->supplies),
				       wm8995->supplies);
		break;
	}

	return 0;
}

static int wm8995_probe(struct snd_soc_component *component)
{
	struct wm8995_priv *wm8995;
	int i;
	int ret;

	wm8995 = snd_soc_component_get_drvdata(component);
	wm8995->component = component;

	for (i = 0; i < ARRAY_SIZE(wm8995->supplies); i++)
		wm8995->supplies[i].supply = wm8995_supply_names[i];

	ret = devm_regulator_bulk_get(component->dev,
				      ARRAY_SIZE(wm8995->supplies),
				      wm8995->supplies);
	if (ret) {
		dev_err(component->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	wm8995->disable_nb[0].notifier_call = wm8995_regulator_event_0;
	wm8995->disable_nb[1].notifier_call = wm8995_regulator_event_1;
	wm8995->disable_nb[2].notifier_call = wm8995_regulator_event_2;
	wm8995->disable_nb[3].notifier_call = wm8995_regulator_event_3;
	wm8995->disable_nb[4].notifier_call = wm8995_regulator_event_4;
	wm8995->disable_nb[5].notifier_call = wm8995_regulator_event_5;
	wm8995->disable_nb[6].notifier_call = wm8995_regulator_event_6;
	wm8995->disable_nb[7].notifier_call = wm8995_regulator_event_7;

	/* This should really be moved into the regulator core */
	for (i = 0; i < ARRAY_SIZE(wm8995->supplies); i++) {
		ret = devm_regulator_register_notifier(
						wm8995->supplies[i].consumer,
						&wm8995->disable_nb[i]);
		if (ret) {
			dev_err(component->dev,
				"Failed to register regulator notifier: %d\n",
				ret);
		}
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8995->supplies),
				    wm8995->supplies);
	if (ret) {
		dev_err(component->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	ret = snd_soc_component_read(component, WM8995_SOFTWARE_RESET);
	if (ret < 0) {
		dev_err(component->dev, "Failed to read device ID: %d\n", ret);
		goto err_reg_enable;
	}

	if (ret != 0x8995) {
		dev_err(component->dev, "Invalid device ID: %#x\n", ret);
		ret = -EINVAL;
		goto err_reg_enable;
	}

	ret = snd_soc_component_write(component, WM8995_SOFTWARE_RESET, 0);
	if (ret < 0) {
		dev_err(component->dev, "Failed to issue reset: %d\n", ret);
		goto err_reg_enable;
	}

	/* Latch volume updates (right only; we always do left then right). */
	snd_soc_component_update_bits(component, WM8995_AIF1_DAC1_RIGHT_VOLUME,
			    WM8995_AIF1DAC1_VU_MASK, WM8995_AIF1DAC1_VU);
	snd_soc_component_update_bits(component, WM8995_AIF1_DAC2_RIGHT_VOLUME,
			    WM8995_AIF1DAC2_VU_MASK, WM8995_AIF1DAC2_VU);
	snd_soc_component_update_bits(component, WM8995_AIF2_DAC_RIGHT_VOLUME,
			    WM8995_AIF2DAC_VU_MASK, WM8995_AIF2DAC_VU);
	snd_soc_component_update_bits(component, WM8995_AIF1_ADC1_RIGHT_VOLUME,
			    WM8995_AIF1ADC1_VU_MASK, WM8995_AIF1ADC1_VU);
	snd_soc_component_update_bits(component, WM8995_AIF1_ADC2_RIGHT_VOLUME,
			    WM8995_AIF1ADC2_VU_MASK, WM8995_AIF1ADC2_VU);
	snd_soc_component_update_bits(component, WM8995_AIF2_ADC_RIGHT_VOLUME,
			    WM8995_AIF2ADC_VU_MASK, WM8995_AIF1ADC2_VU);
	snd_soc_component_update_bits(component, WM8995_DAC1_RIGHT_VOLUME,
			    WM8995_DAC1_VU_MASK, WM8995_DAC1_VU);
	snd_soc_component_update_bits(component, WM8995_DAC2_RIGHT_VOLUME,
			    WM8995_DAC2_VU_MASK, WM8995_DAC2_VU);
	snd_soc_component_update_bits(component, WM8995_RIGHT_LINE_INPUT_1_VOLUME,
			    WM8995_IN1_VU_MASK, WM8995_IN1_VU);

	wm8995_update_class_w(component);

	return 0;

err_reg_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8995->supplies), wm8995->supplies);
	return ret;
}

#define WM8995_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops wm8995_aif1_dai_ops = {
	.set_sysclk = wm8995_set_dai_sysclk,
	.set_fmt = wm8995_set_dai_fmt,
	.hw_params = wm8995_hw_params,
	.mute_stream = wm8995_aif_mute,
	.set_pll = wm8995_set_fll,
	.set_tristate = wm8995_set_tristate,
	.no_capture_mute = 1,
};

static const struct snd_soc_dai_ops wm8995_aif2_dai_ops = {
	.set_sysclk = wm8995_set_dai_sysclk,
	.set_fmt = wm8995_set_dai_fmt,
	.hw_params = wm8995_hw_params,
	.mute_stream = wm8995_aif_mute,
	.set_pll = wm8995_set_fll,
	.set_tristate = wm8995_set_tristate,
	.no_capture_mute = 1,
};

static const struct snd_soc_dai_ops wm8995_aif3_dai_ops = {
	.set_tristate = wm8995_set_tristate,
};

static struct snd_soc_dai_driver wm8995_dai[] = {
	{
		.name = "wm8995-aif1",
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = WM8995_FORMATS
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = WM8995_FORMATS
		},
		.ops = &wm8995_aif1_dai_ops
	},
	{
		.name = "wm8995-aif2",
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = WM8995_FORMATS
		},
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = WM8995_FORMATS
		},
		.ops = &wm8995_aif2_dai_ops
	},
	{
		.name = "wm8995-aif3",
		.playback = {
			.stream_name = "AIF3 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = WM8995_FORMATS
		},
		.capture = {
			.stream_name = "AIF3 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = WM8995_FORMATS
		},
		.ops = &wm8995_aif3_dai_ops
	}
};

static const struct snd_soc_component_driver soc_component_dev_wm8995 = {
	.probe			= wm8995_probe,
	.set_bias_level		= wm8995_set_bias_level,
	.controls		= wm8995_snd_controls,
	.num_controls		= ARRAY_SIZE(wm8995_snd_controls),
	.dapm_widgets		= wm8995_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm8995_dapm_widgets),
	.dapm_routes		= wm8995_intercon,
	.num_dapm_routes	= ARRAY_SIZE(wm8995_intercon),
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config wm8995_regmap = {
	.reg_bits = 16,
	.val_bits = 16,

	.max_register = WM8995_MAX_REGISTER,
	.reg_defaults = wm8995_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8995_reg_defaults),
	.volatile_reg = wm8995_volatile,
	.readable_reg = wm8995_readable,
	.cache_type = REGCACHE_RBTREE,
};

#if defined(CONFIG_SPI_MASTER)
static int wm8995_spi_probe(struct spi_device *spi)
{
	struct wm8995_priv *wm8995;
	int ret;

	wm8995 = devm_kzalloc(&spi->dev, sizeof(*wm8995), GFP_KERNEL);
	if (!wm8995)
		return -ENOMEM;

	spi_set_drvdata(spi, wm8995);

	wm8995->regmap = devm_regmap_init_spi(spi, &wm8995_regmap);
	if (IS_ERR(wm8995->regmap)) {
		ret = PTR_ERR(wm8995->regmap);
		dev_err(&spi->dev, "Failed to register regmap: %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_component(&spi->dev,
				     &soc_component_dev_wm8995, wm8995_dai,
				     ARRAY_SIZE(wm8995_dai));
	return ret;
}

static struct spi_driver wm8995_spi_driver = {
	.driver = {
		.name = "wm8995",
	},
	.probe = wm8995_spi_probe,
};
#endif

#if IS_ENABLED(CONFIG_I2C)
static int wm8995_i2c_probe(struct i2c_client *i2c)
{
	struct wm8995_priv *wm8995;
	int ret;

	wm8995 = devm_kzalloc(&i2c->dev, sizeof(*wm8995), GFP_KERNEL);
	if (!wm8995)
		return -ENOMEM;

	i2c_set_clientdata(i2c, wm8995);

	wm8995->regmap = devm_regmap_init_i2c(i2c, &wm8995_regmap);
	if (IS_ERR(wm8995->regmap)) {
		ret = PTR_ERR(wm8995->regmap);
		dev_err(&i2c->dev, "Failed to register regmap: %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_component(&i2c->dev,
				     &soc_component_dev_wm8995, wm8995_dai,
				     ARRAY_SIZE(wm8995_dai));
	if (ret < 0)
		dev_err(&i2c->dev, "Failed to register CODEC: %d\n", ret);

	return ret;
}

static const struct i2c_device_id wm8995_i2c_id[] = {
	{"wm8995", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, wm8995_i2c_id);

static struct i2c_driver wm8995_i2c_driver = {
	.driver = {
		.name = "wm8995",
	},
	.probe_new = wm8995_i2c_probe,
	.id_table = wm8995_i2c_id
};
#endif

static int __init wm8995_modinit(void)
{
	int ret = 0;

#if IS_ENABLED(CONFIG_I2C)
	ret = i2c_add_driver(&wm8995_i2c_driver);
	if (ret) {
		printk(KERN_ERR "Failed to register wm8995 I2C driver: %d\n",
		       ret);
	}
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8995_spi_driver);
	if (ret) {
		printk(KERN_ERR "Failed to register wm8995 SPI driver: %d\n",
		       ret);
	}
#endif
	return ret;
}

module_init(wm8995_modinit);

static void __exit wm8995_exit(void)
{
#if IS_ENABLED(CONFIG_I2C)
	i2c_del_driver(&wm8995_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8995_spi_driver);
#endif
}

module_exit(wm8995_exit);

MODULE_DESCRIPTION("ASoC WM8995 driver");
MODULE_AUTHOR("Dimitris Papastamos <dp@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
