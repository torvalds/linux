// SPDX-License-Identifier: GPL-2.0
//
// rt1015.c  --  RT1015 ALSA SoC audio amplifier driver
//
// Copyright 2019 Realtek Semiconductor Corp.
//
// Author: Jack Yu <jack.yu@realtek.com>
//
//

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/rt1015.h>
#include <sound/soc-dapm.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "rl6231.h"
#include "rt1015.h"

static const struct rt1015_platform_data i2s_default_platform_data = {
	.power_up_delay_ms = 50,
};

static const struct reg_default rt1015_reg[] = {
	{ 0x0000, 0x0000 },
	{ 0x0004, 0xa000 },
	{ 0x0006, 0x0003 },
	{ 0x000a, 0x081e },
	{ 0x000c, 0x0006 },
	{ 0x000e, 0x0000 },
	{ 0x0010, 0x0000 },
	{ 0x0012, 0x0000 },
	{ 0x0014, 0x0000 },
	{ 0x0016, 0x0000 },
	{ 0x0018, 0x0000 },
	{ 0x0020, 0x8000 },
	{ 0x0022, 0x8043 },
	{ 0x0076, 0x0000 },
	{ 0x0078, 0x0000 },
	{ 0x007a, 0x0002 },
	{ 0x007c, 0x10ec },
	{ 0x007d, 0x1015 },
	{ 0x00f0, 0x5000 },
	{ 0x00f2, 0x004c },
	{ 0x00f3, 0xecfe },
	{ 0x00f4, 0x0000 },
	{ 0x00f6, 0x0400 },
	{ 0x0100, 0x0028 },
	{ 0x0102, 0xff02 },
	{ 0x0104, 0xa213 },
	{ 0x0106, 0x200c },
	{ 0x010c, 0x0000 },
	{ 0x010e, 0x0058 },
	{ 0x0111, 0x0200 },
	{ 0x0112, 0x0400 },
	{ 0x0114, 0x0022 },
	{ 0x0116, 0x0000 },
	{ 0x0118, 0x0000 },
	{ 0x011a, 0x0123 },
	{ 0x011c, 0x4567 },
	{ 0x0300, 0x203d },
	{ 0x0302, 0x001e },
	{ 0x0311, 0x0000 },
	{ 0x0313, 0x6014 },
	{ 0x0314, 0x00a2 },
	{ 0x031a, 0x00a0 },
	{ 0x031c, 0x001f },
	{ 0x031d, 0xffff },
	{ 0x031e, 0x0000 },
	{ 0x031f, 0x0000 },
	{ 0x0320, 0x0000 },
	{ 0x0321, 0x0000 },
	{ 0x0322, 0xd7df },
	{ 0x0328, 0x10b2 },
	{ 0x0329, 0x0175 },
	{ 0x032a, 0x36ad },
	{ 0x032b, 0x7e55 },
	{ 0x032c, 0x0520 },
	{ 0x032d, 0xaa00 },
	{ 0x032e, 0x570e },
	{ 0x0330, 0xe180 },
	{ 0x0332, 0x0034 },
	{ 0x0334, 0x0001 },
	{ 0x0336, 0x0010 },
	{ 0x0338, 0x0000 },
	{ 0x04fa, 0x0030 },
	{ 0x04fc, 0x35c8 },
	{ 0x04fe, 0x0800 },
	{ 0x0500, 0x0400 },
	{ 0x0502, 0x1000 },
	{ 0x0504, 0x0000 },
	{ 0x0506, 0x04ff },
	{ 0x0508, 0x0010 },
	{ 0x050a, 0x001a },
	{ 0x0519, 0x1c68 },
	{ 0x051a, 0x0ccc },
	{ 0x051b, 0x0666 },
	{ 0x051d, 0x0000 },
	{ 0x051f, 0x0000 },
	{ 0x0536, 0x061c },
	{ 0x0538, 0x0000 },
	{ 0x053a, 0x0000 },
	{ 0x053c, 0x0000 },
	{ 0x053d, 0x0000 },
	{ 0x053e, 0x0000 },
	{ 0x053f, 0x0000 },
	{ 0x0540, 0x0000 },
	{ 0x0541, 0x0000 },
	{ 0x0542, 0x0000 },
	{ 0x0543, 0x0000 },
	{ 0x0544, 0x0000 },
	{ 0x0568, 0x0000 },
	{ 0x056a, 0x0000 },
	{ 0x1000, 0x0040 },
	{ 0x1002, 0x5405 },
	{ 0x1006, 0x5515 },
	{ 0x1007, 0x05f7 },
	{ 0x1009, 0x0b0a },
	{ 0x100a, 0x00ef },
	{ 0x100d, 0x0003 },
	{ 0x1010, 0xa433 },
	{ 0x1020, 0x0000 },
	{ 0x1200, 0x5a01 },
	{ 0x1202, 0x6524 },
	{ 0x1204, 0x1f00 },
	{ 0x1206, 0x0000 },
	{ 0x1208, 0x0000 },
	{ 0x120a, 0x0000 },
	{ 0x120c, 0x0000 },
	{ 0x120e, 0x0000 },
	{ 0x1210, 0x0000 },
	{ 0x1212, 0x0000 },
	{ 0x1300, 0x10a1 },
	{ 0x1302, 0x12ff },
	{ 0x1304, 0x0400 },
	{ 0x1305, 0x0844 },
	{ 0x1306, 0x4611 },
	{ 0x1308, 0x555e },
	{ 0x130a, 0x0000 },
	{ 0x130c, 0x2000 },
	{ 0x130e, 0x0100 },
	{ 0x130f, 0x0001 },
	{ 0x1310, 0x0000 },
	{ 0x1312, 0x0000 },
	{ 0x1314, 0x0000 },
	{ 0x1316, 0x0000 },
	{ 0x1318, 0x0000 },
	{ 0x131a, 0x0000 },
	{ 0x1322, 0x0029 },
	{ 0x1323, 0x4a52 },
	{ 0x1324, 0x002c },
	{ 0x1325, 0x0b02 },
	{ 0x1326, 0x002d },
	{ 0x1327, 0x6b5a },
	{ 0x1328, 0x002e },
	{ 0x1329, 0xcbb2 },
	{ 0x132a, 0x0030 },
	{ 0x132b, 0x2c0b },
	{ 0x1330, 0x0031 },
	{ 0x1331, 0x8c63 },
	{ 0x1332, 0x0032 },
	{ 0x1333, 0xecbb },
	{ 0x1334, 0x0034 },
	{ 0x1335, 0x4d13 },
	{ 0x1336, 0x0037 },
	{ 0x1337, 0x0dc3 },
	{ 0x1338, 0x003d },
	{ 0x1339, 0xef7b },
	{ 0x133a, 0x0044 },
	{ 0x133b, 0xd134 },
	{ 0x133c, 0x0047 },
	{ 0x133d, 0x91e4 },
	{ 0x133e, 0x004d },
	{ 0x133f, 0xc370 },
	{ 0x1340, 0x0053 },
	{ 0x1341, 0xf4fd },
	{ 0x1342, 0x0060 },
	{ 0x1343, 0x5816 },
	{ 0x1344, 0x006c },
	{ 0x1345, 0xbb2e },
	{ 0x1346, 0x0072 },
	{ 0x1347, 0xecbb },
	{ 0x1348, 0x0076 },
	{ 0x1349, 0x5d97 },
};

static bool rt1015_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT1015_RESET:
	case RT1015_CLK_DET:
	case RT1015_SIL_DET:
	case RT1015_VER_ID:
	case RT1015_VENDOR_ID:
	case RT1015_DEVICE_ID:
	case RT1015_PRO_ALT:
	case RT1015_MAN_I2C:
	case RT1015_DAC3:
	case RT1015_VBAT_TEST_OUT1:
	case RT1015_VBAT_TEST_OUT2:
	case RT1015_VBAT_PROT_ATT:
	case RT1015_VBAT_DET_CODE:
	case RT1015_SMART_BST_CTRL1:
	case RT1015_SPK_DC_DETECT1:
	case RT1015_SPK_DC_DETECT4:
	case RT1015_SPK_DC_DETECT5:
	case RT1015_DC_CALIB_CLSD1:
	case RT1015_DC_CALIB_CLSD5:
	case RT1015_DC_CALIB_CLSD6:
	case RT1015_DC_CALIB_CLSD7:
	case RT1015_DC_CALIB_CLSD8:
	case RT1015_S_BST_TIMING_INTER1:
	case RT1015_OSCK_STA:
	case RT1015_MONO_DYNA_CTRL1:
	case RT1015_MONO_DYNA_CTRL5:
		return true;

	default:
		return false;
	}
}

static bool rt1015_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT1015_RESET:
	case RT1015_CLK2:
	case RT1015_CLK3:
	case RT1015_PLL1:
	case RT1015_PLL2:
	case RT1015_DUM_RW1:
	case RT1015_DUM_RW2:
	case RT1015_DUM_RW3:
	case RT1015_DUM_RW4:
	case RT1015_DUM_RW5:
	case RT1015_DUM_RW6:
	case RT1015_CLK_DET:
	case RT1015_SIL_DET:
	case RT1015_CUSTOMER_ID:
	case RT1015_PCODE_FWVER:
	case RT1015_VER_ID:
	case RT1015_VENDOR_ID:
	case RT1015_DEVICE_ID:
	case RT1015_PAD_DRV1:
	case RT1015_PAD_DRV2:
	case RT1015_GAT_BOOST:
	case RT1015_PRO_ALT:
	case RT1015_OSCK_STA:
	case RT1015_MAN_I2C:
	case RT1015_DAC1:
	case RT1015_DAC2:
	case RT1015_DAC3:
	case RT1015_ADC1:
	case RT1015_ADC2:
	case RT1015_TDM_MASTER:
	case RT1015_TDM_TCON:
	case RT1015_TDM1_1:
	case RT1015_TDM1_2:
	case RT1015_TDM1_3:
	case RT1015_TDM1_4:
	case RT1015_TDM1_5:
	case RT1015_MIXER1:
	case RT1015_MIXER2:
	case RT1015_ANA_PROTECT1:
	case RT1015_ANA_CTRL_SEQ1:
	case RT1015_ANA_CTRL_SEQ2:
	case RT1015_VBAT_DET_DEB:
	case RT1015_VBAT_VOLT_DET1:
	case RT1015_VBAT_VOLT_DET2:
	case RT1015_VBAT_TEST_OUT1:
	case RT1015_VBAT_TEST_OUT2:
	case RT1015_VBAT_PROT_ATT:
	case RT1015_VBAT_DET_CODE:
	case RT1015_PWR1:
	case RT1015_PWR4:
	case RT1015_PWR5:
	case RT1015_PWR6:
	case RT1015_PWR7:
	case RT1015_PWR8:
	case RT1015_PWR9:
	case RT1015_CLASSD_SEQ:
	case RT1015_SMART_BST_CTRL1:
	case RT1015_SMART_BST_CTRL2:
	case RT1015_ANA_CTRL1:
	case RT1015_ANA_CTRL2:
	case RT1015_PWR_STATE_CTRL:
	case RT1015_MONO_DYNA_CTRL:
	case RT1015_MONO_DYNA_CTRL1:
	case RT1015_MONO_DYNA_CTRL2:
	case RT1015_MONO_DYNA_CTRL3:
	case RT1015_MONO_DYNA_CTRL4:
	case RT1015_MONO_DYNA_CTRL5:
	case RT1015_SPK_VOL:
	case RT1015_SHORT_DETTOP1:
	case RT1015_SHORT_DETTOP2:
	case RT1015_SPK_DC_DETECT1:
	case RT1015_SPK_DC_DETECT2:
	case RT1015_SPK_DC_DETECT3:
	case RT1015_SPK_DC_DETECT4:
	case RT1015_SPK_DC_DETECT5:
	case RT1015_BAT_RPO_STEP1:
	case RT1015_BAT_RPO_STEP2:
	case RT1015_BAT_RPO_STEP3:
	case RT1015_BAT_RPO_STEP4:
	case RT1015_BAT_RPO_STEP5:
	case RT1015_BAT_RPO_STEP6:
	case RT1015_BAT_RPO_STEP7:
	case RT1015_BAT_RPO_STEP8:
	case RT1015_BAT_RPO_STEP9:
	case RT1015_BAT_RPO_STEP10:
	case RT1015_BAT_RPO_STEP11:
	case RT1015_BAT_RPO_STEP12:
	case RT1015_SPREAD_SPEC1:
	case RT1015_SPREAD_SPEC2:
	case RT1015_PAD_STATUS:
	case RT1015_PADS_PULLING_CTRL1:
	case RT1015_PADS_DRIVING:
	case RT1015_SYS_RST1:
	case RT1015_SYS_RST2:
	case RT1015_SYS_GATING1:
	case RT1015_TEST_MODE1:
	case RT1015_TEST_MODE2:
	case RT1015_TIMING_CTRL1:
	case RT1015_PLL_INT:
	case RT1015_TEST_OUT1:
	case RT1015_DC_CALIB_CLSD1:
	case RT1015_DC_CALIB_CLSD2:
	case RT1015_DC_CALIB_CLSD3:
	case RT1015_DC_CALIB_CLSD4:
	case RT1015_DC_CALIB_CLSD5:
	case RT1015_DC_CALIB_CLSD6:
	case RT1015_DC_CALIB_CLSD7:
	case RT1015_DC_CALIB_CLSD8:
	case RT1015_DC_CALIB_CLSD9:
	case RT1015_DC_CALIB_CLSD10:
	case RT1015_CLSD_INTERNAL1:
	case RT1015_CLSD_INTERNAL2:
	case RT1015_CLSD_INTERNAL3:
	case RT1015_CLSD_INTERNAL4:
	case RT1015_CLSD_INTERNAL5:
	case RT1015_CLSD_INTERNAL6:
	case RT1015_CLSD_INTERNAL7:
	case RT1015_CLSD_INTERNAL8:
	case RT1015_CLSD_INTERNAL9:
	case RT1015_CLSD_OCP_CTRL:
	case RT1015_VREF_LV:
	case RT1015_MBIAS1:
	case RT1015_MBIAS2:
	case RT1015_MBIAS3:
	case RT1015_MBIAS4:
	case RT1015_VREF_LV1:
	case RT1015_S_BST_TIMING_INTER1:
	case RT1015_S_BST_TIMING_INTER2:
	case RT1015_S_BST_TIMING_INTER3:
	case RT1015_S_BST_TIMING_INTER4:
	case RT1015_S_BST_TIMING_INTER5:
	case RT1015_S_BST_TIMING_INTER6:
	case RT1015_S_BST_TIMING_INTER7:
	case RT1015_S_BST_TIMING_INTER8:
	case RT1015_S_BST_TIMING_INTER9:
	case RT1015_S_BST_TIMING_INTER10:
	case RT1015_S_BST_TIMING_INTER11:
	case RT1015_S_BST_TIMING_INTER12:
	case RT1015_S_BST_TIMING_INTER13:
	case RT1015_S_BST_TIMING_INTER14:
	case RT1015_S_BST_TIMING_INTER15:
	case RT1015_S_BST_TIMING_INTER16:
	case RT1015_S_BST_TIMING_INTER17:
	case RT1015_S_BST_TIMING_INTER18:
	case RT1015_S_BST_TIMING_INTER19:
	case RT1015_S_BST_TIMING_INTER20:
	case RT1015_S_BST_TIMING_INTER21:
	case RT1015_S_BST_TIMING_INTER22:
	case RT1015_S_BST_TIMING_INTER23:
	case RT1015_S_BST_TIMING_INTER24:
	case RT1015_S_BST_TIMING_INTER25:
	case RT1015_S_BST_TIMING_INTER26:
	case RT1015_S_BST_TIMING_INTER27:
	case RT1015_S_BST_TIMING_INTER28:
	case RT1015_S_BST_TIMING_INTER29:
	case RT1015_S_BST_TIMING_INTER30:
	case RT1015_S_BST_TIMING_INTER31:
	case RT1015_S_BST_TIMING_INTER32:
	case RT1015_S_BST_TIMING_INTER33:
	case RT1015_S_BST_TIMING_INTER34:
	case RT1015_S_BST_TIMING_INTER35:
	case RT1015_S_BST_TIMING_INTER36:
		return true;

	default:
		return false;
	}
}

static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -9525, 75, 0);

static const char * const rt1015_din_source_select[] = {
	"Left",
	"Right",
	"Left + Right average",
};

static SOC_ENUM_SINGLE_DECL(rt1015_mono_lr_sel, RT1015_PAD_DRV2, 4,
	rt1015_din_source_select);

static const char * const rt1015_boost_mode[] = {
	"Bypass", "Adaptive", "Fixed Adaptive"
};

static SOC_ENUM_SINGLE_DECL(rt1015_boost_mode_enum, 0, 0,
	rt1015_boost_mode);

static int rt1015_boost_mode_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct rt1015_priv *rt1015 =
		snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rt1015->boost_mode;

	return 0;
}

static int rt1015_boost_mode_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct rt1015_priv *rt1015 =
		snd_soc_component_get_drvdata(component);
	int boost_mode = ucontrol->value.integer.value[0];

	switch (boost_mode) {
	case BYPASS:
		snd_soc_component_update_bits(component,
			RT1015_SMART_BST_CTRL1, RT1015_ABST_AUTO_EN_MASK |
			RT1015_ABST_FIX_TGT_MASK | RT1015_BYPASS_SWR_REG_MASK,
			RT1015_ABST_REG_MODE | RT1015_ABST_FIX_TGT_DIS |
			RT1015_BYPASS_SWRREG_BYPASS);
		break;
	case ADAPTIVE:
		snd_soc_component_update_bits(component,
			RT1015_SMART_BST_CTRL1, RT1015_ABST_AUTO_EN_MASK |
			RT1015_ABST_FIX_TGT_MASK | RT1015_BYPASS_SWR_REG_MASK,
			RT1015_ABST_AUTO_MODE | RT1015_ABST_FIX_TGT_DIS |
			RT1015_BYPASS_SWRREG_PASS);
		break;
	case FIXED_ADAPTIVE:
		snd_soc_component_update_bits(component,
			RT1015_SMART_BST_CTRL1, RT1015_ABST_AUTO_EN_MASK |
			RT1015_ABST_FIX_TGT_MASK | RT1015_BYPASS_SWR_REG_MASK,
			RT1015_ABST_AUTO_MODE | RT1015_ABST_FIX_TGT_EN |
			RT1015_BYPASS_SWRREG_PASS);
		break;
	default:
		dev_err(component->dev, "Unknown boost control.\n");
		return -EINVAL;
	}

	rt1015->boost_mode = boost_mode;

	return 0;
}

static int rt1015_bypass_boost_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct rt1015_priv *rt1015 =
		snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rt1015->bypass_boost;

	return 0;
}

static void rt1015_calibrate(struct rt1015_priv *rt1015)
{
	struct snd_soc_component *component = rt1015->component;
	struct regmap *regmap = rt1015->regmap;

	snd_soc_dapm_mutex_lock(&component->dapm);
	regcache_cache_bypass(regmap, true);

	regmap_write(regmap, RT1015_CLK_DET, 0x0000);
	regmap_write(regmap, RT1015_PWR4, 0x00B2);
	regmap_write(regmap, RT1015_PWR_STATE_CTRL, 0x0009);
	msleep(100);
	regmap_write(regmap, RT1015_PWR_STATE_CTRL, 0x000A);
	msleep(100);
	regmap_write(regmap, RT1015_PWR_STATE_CTRL, 0x000C);
	msleep(100);
	regmap_write(regmap, RT1015_CLSD_INTERNAL8, 0x2028);
	regmap_write(regmap, RT1015_CLSD_INTERNAL9, 0x0140);
	regmap_write(regmap, RT1015_PWR_STATE_CTRL, 0x000D);
	msleep(300);
	regmap_write(regmap, RT1015_PWR_STATE_CTRL, 0x0008);
	regmap_write(regmap, RT1015_SYS_RST1, 0x05F5);
	regmap_write(regmap, RT1015_CLK_DET, 0x8000);

	regcache_cache_bypass(regmap, false);
	regcache_mark_dirty(regmap);
	regcache_sync(regmap);
	snd_soc_dapm_mutex_unlock(&component->dapm);
}

static int rt1015_bypass_boost_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct rt1015_priv *rt1015 =
		snd_soc_component_get_drvdata(component);

	if (rt1015->dac_is_used) {
		dev_err(component->dev, "DAC is being used!\n");
		return -EBUSY;
	}

	rt1015->bypass_boost = ucontrol->value.integer.value[0];
	if (rt1015->bypass_boost == RT1015_Bypass_Boost &&
			!rt1015->cali_done) {
		rt1015_calibrate(rt1015);
		rt1015->cali_done = 1;

		regmap_write(rt1015->regmap, RT1015_MONO_DYNA_CTRL, 0x0010);
	}

	return 0;
}

static const struct snd_kcontrol_new rt1015_snd_controls[] = {
	SOC_SINGLE_TLV("DAC Playback Volume", RT1015_DAC1, RT1015_DAC_VOL_SFT,
		127, 0, dac_vol_tlv),
	SOC_DOUBLE("DAC Playback Switch", RT1015_DAC3,
		RT1015_DA_MUTE_SFT, RT1015_DVOL_MUTE_FLAG_SFT, 1, 1),
	SOC_ENUM_EXT("Boost Mode", rt1015_boost_mode_enum,
		rt1015_boost_mode_get, rt1015_boost_mode_put),
	SOC_ENUM("Mono LR Select", rt1015_mono_lr_sel),
	SOC_SINGLE_EXT("Bypass Boost", SND_SOC_NOPM, 0, 1, 0,
		rt1015_bypass_boost_get, rt1015_bypass_boost_put),
};

static int rt1015_is_sys_clk_from_pll(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(source->dapm);
	struct rt1015_priv *rt1015 = snd_soc_component_get_drvdata(component);

	if (rt1015->sysclk_src == RT1015_SCLK_S_PLL)
		return 1;
	else
		return 0;
}

static int r1015_dac_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt1015_priv *rt1015 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		rt1015->dac_is_used = 1;
		if (rt1015->bypass_boost == RT1015_Enable_Boost) {
			snd_soc_component_write(component,
				RT1015_SYS_RST1, 0x05f7);
			snd_soc_component_write(component,
				RT1015_SYS_RST2, 0x0b0a);
			snd_soc_component_write(component,
				RT1015_GAT_BOOST, 0xacfe);
			snd_soc_component_write(component,
				RT1015_PWR9, 0xaa00);
			snd_soc_component_write(component,
				RT1015_GAT_BOOST, 0xecfe);
		} else {
			snd_soc_component_write(component,
				0x032d, 0xaa60);
			snd_soc_component_write(component,
				RT1015_SYS_RST1, 0x05f7);
			snd_soc_component_write(component,
				RT1015_SYS_RST2, 0x0b0a);
			snd_soc_component_write(component,
				RT1015_PWR_STATE_CTRL, 0x008e);
		}
		break;

	case SND_SOC_DAPM_POST_PMD:
		if (rt1015->bypass_boost == RT1015_Enable_Boost) {
			snd_soc_component_write(component,
				RT1015_PWR9, 0xa800);
			snd_soc_component_write(component,
				RT1015_SYS_RST1, 0x05f5);
			snd_soc_component_write(component,
				RT1015_SYS_RST2, 0x0b9a);
		} else {
			snd_soc_component_write(component,
				0x032d, 0xaa60);
			snd_soc_component_write(component,
				RT1015_PWR_STATE_CTRL, 0x0088);
			snd_soc_component_write(component,
				RT1015_SYS_RST1, 0x05f5);
			snd_soc_component_write(component,
				RT1015_SYS_RST2, 0x0b9a);
		}
		rt1015->dac_is_used = 0;
		break;

	default:
		break;
	}
	return 0;
}

static int rt1015_amp_drv_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt1015_priv *rt1015 = snd_soc_component_get_drvdata(component);
	unsigned int ret, ret2;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = snd_soc_component_read(component, RT1015_CLK_DET);
		ret2 = snd_soc_component_read(component, RT1015_SPK_DC_DETECT1);
		if (!((ret >> 15) & 0x1)) {
			snd_soc_component_update_bits(component, RT1015_CLK_DET,
				RT1015_EN_BCLK_DET_MASK, RT1015_EN_BCLK_DET);
			dev_dbg(component->dev, "BCLK Detection Enabled.\n");
		}
		if (!((ret2 >> 12) & 0x1)) {
			snd_soc_component_update_bits(component, RT1015_SPK_DC_DETECT1,
				RT1015_EN_CLA_D_DC_DET_MASK, RT1015_EN_CLA_D_DC_DET);
			dev_dbg(component->dev, "Class-D DC Detection Enabled.\n");
		}
		break;
	case SND_SOC_DAPM_POST_PMU:
		msleep(rt1015->pdata.power_up_delay_ms);
		break;
	default:
		break;
	}
	return 0;
}

static const struct snd_soc_dapm_widget rt1015_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("PLL", RT1015_PWR1, RT1015_PWR_PLL_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_AIF_IN("AIFRX", "AIF Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0,
		r1015_dac_event, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUT_DRV_E("Amp Drv", SND_SOC_NOPM, 0, 0, NULL, 0,
			rt1015_amp_drv_event, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_OUTPUT("SPO"),
};

static const struct snd_soc_dapm_route rt1015_dapm_routes[] = {
	{ "DAC", NULL, "AIFRX" },
	{ "DAC", NULL, "PLL", rt1015_is_sys_clk_from_pll},
	{ "Amp Drv", NULL, "DAC" },
	{ "SPO", NULL, "Amp Drv" },
};

static int rt1015_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt1015_priv *rt1015 = snd_soc_component_get_drvdata(component);
	int pre_div, frame_size, lrck;
	unsigned int val_len = 0;

	lrck = params_rate(params);
	pre_div = rl6231_get_clk_info(rt1015->sysclk, lrck);
	if (pre_div < 0) {
		dev_err(component->dev, "Unsupported clock rate\n");
		return -EINVAL;
	}

	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(component->dev, "Unsupported frame size: %d\n",
			frame_size);
		return -EINVAL;
	}

	dev_dbg(component->dev, "pre_div is %d for iis %d\n", pre_div, dai->id);

	dev_dbg(component->dev, "lrck is %dHz and pre_div is %d for iis %d\n",
				lrck, pre_div, dai->id);

	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		val_len = RT1015_I2S_DL_20;
		break;
	case 24:
		val_len = RT1015_I2S_DL_24;
		break;
	case 8:
		val_len = RT1015_I2S_DL_8;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RT1015_TDM_MASTER,
		RT1015_I2S_DL_MASK, val_len);
	snd_soc_component_update_bits(component, RT1015_CLK2,
		RT1015_FS_PD_MASK, pre_div << RT1015_FS_PD_SFT);

	return 0;
}

static int rt1015_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	unsigned int reg_val = 0, reg_val2 = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		reg_val |= RT1015_TCON_TDM_MS_M;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		reg_val |= RT1015_TCON_TDM_MS_S;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_val2 |= RT1015_TDM_INV_BCLK;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;

	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT1015_I2S_M_DF_LEFT;
		break;

	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT1015_I2S_M_DF_PCM_A;
		break;

	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT1015_I2S_M_DF_PCM_B;
		break;

	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RT1015_TDM_MASTER,
			RT1015_TCON_TDM_MS_MASK | RT1015_I2S_M_DF_MASK,
			reg_val);
	snd_soc_component_update_bits(component, RT1015_TDM1_1,
			RT1015_TDM_INV_BCLK_MASK, reg_val2);

	return 0;
}

static int rt1015_set_component_sysclk(struct snd_soc_component *component,
		int clk_id, int source, unsigned int freq, int dir)
{
	struct rt1015_priv *rt1015 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0;

	if (freq == rt1015->sysclk && clk_id == rt1015->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT1015_SCLK_S_MCLK:
		reg_val |= RT1015_CLK_SYS_PRE_SEL_MCLK;
		break;

	case RT1015_SCLK_S_PLL:
		reg_val |= RT1015_CLK_SYS_PRE_SEL_PLL;
		break;

	default:
		dev_err(component->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}

	rt1015->sysclk = freq;
	rt1015->sysclk_src = clk_id;

	dev_dbg(component->dev, "Sysclk is %dHz and clock id is %d\n",
		freq, clk_id);

	snd_soc_component_update_bits(component, RT1015_CLK2,
			RT1015_CLK_SYS_PRE_SEL_MASK, reg_val);

	return 0;
}

static int rt1015_set_component_pll(struct snd_soc_component *component,
		int pll_id, int source, unsigned int freq_in,
		unsigned int freq_out)
{
	struct rt1015_priv *rt1015 = snd_soc_component_get_drvdata(component);
	struct rl6231_pll_code pll_code;
	int ret;

	if (!freq_in || !freq_out) {
		dev_dbg(component->dev, "PLL disabled\n");

		rt1015->pll_in = 0;
		rt1015->pll_out = 0;

		return 0;
	}

	if (source == rt1015->pll_src && freq_in == rt1015->pll_in &&
		freq_out == rt1015->pll_out)
		return 0;

	switch (source) {
	case RT1015_PLL_S_MCLK:
		snd_soc_component_update_bits(component, RT1015_CLK2,
			RT1015_PLL_SEL_MASK, RT1015_PLL_SEL_PLL_SRC2);
		break;

	case RT1015_PLL_S_BCLK:
		snd_soc_component_update_bits(component, RT1015_CLK2,
			RT1015_PLL_SEL_MASK, RT1015_PLL_SEL_BCLK);
		break;

	default:
		dev_err(component->dev, "Unknown PLL Source %d\n", source);
		return -EINVAL;
	}

	ret = rl6231_pll_calc(freq_in, freq_out, &pll_code);
	if (ret < 0) {
		dev_err(component->dev, "Unsupport input clock %d\n", freq_in);
		return ret;
	}

	dev_dbg(component->dev, "bypass=%d m=%d n=%d k=%d\n",
		pll_code.m_bp, (pll_code.m_bp ? 0 : pll_code.m_code),
		pll_code.n_code, pll_code.k_code);

	snd_soc_component_write(component, RT1015_PLL1,
		((pll_code.m_bp ? 0 : pll_code.m_code) << RT1015_PLL_M_SFT) |
		(pll_code.m_bp << RT1015_PLL_M_BP_SFT) |
		pll_code.n_code);
	snd_soc_component_write(component, RT1015_PLL2,
		pll_code.k_code);

	rt1015->pll_in = freq_in;
	rt1015->pll_out = freq_out;
	rt1015->pll_src = source;

	return 0;
}

static int rt1015_set_tdm_slot(struct snd_soc_dai *dai,
	unsigned int tx_mask, unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	unsigned int val = 0, rx_slotnum, tx_slotnum;
	int ret = 0, first_bit;

	switch (slots) {
	case 2:
		val |= RT1015_I2S_TX_2CH;
		break;
	case 4:
		val |= RT1015_I2S_TX_4CH;
		break;
	case 6:
		val |= RT1015_I2S_TX_6CH;
		break;
	case 8:
		val |= RT1015_I2S_TX_8CH;
		break;
	default:
		ret = -EINVAL;
		goto _set_tdm_err_;
	}

	switch (slot_width) {
	case 16:
		val |= RT1015_I2S_CH_TX_LEN_16B;
		break;
	case 20:
		val |= RT1015_I2S_CH_TX_LEN_20B;
		break;
	case 24:
		val |= RT1015_I2S_CH_TX_LEN_24B;
		break;
	case 32:
		val |= RT1015_I2S_CH_TX_LEN_32B;
		break;
	default:
		ret = -EINVAL;
		goto _set_tdm_err_;
	}

	/* Rx slot configuration */
	rx_slotnum = hweight_long(rx_mask);
	if (rx_slotnum != 1) {
		ret = -EINVAL;
		dev_err(component->dev, "too many rx slots or zero slot\n");
		goto _set_tdm_err_;
	}

	/* This is an assumption that the system sends stereo audio to the amplifier typically.
	 * And the stereo audio is placed in slot 0/2/4/6 as the starting slot.
	 * The users could select the channel from L/R/L+R by "Mono LR Select" control.
	 */
	first_bit = __ffs(rx_mask);
	switch (first_bit) {
	case 0:
	case 2:
	case 4:
	case 6:
		snd_soc_component_update_bits(component,
			RT1015_TDM1_4,
			RT1015_TDM_I2S_TX_L_DAC1_1_MASK |
			RT1015_TDM_I2S_TX_R_DAC1_1_MASK,
			(first_bit << RT1015_TDM_I2S_TX_L_DAC1_1_SFT) |
			((first_bit+1) << RT1015_TDM_I2S_TX_R_DAC1_1_SFT));
		break;
	case 1:
	case 3:
	case 5:
	case 7:
		snd_soc_component_update_bits(component,
			RT1015_TDM1_4,
			RT1015_TDM_I2S_TX_L_DAC1_1_MASK |
			RT1015_TDM_I2S_TX_R_DAC1_1_MASK,
			((first_bit-1) << RT1015_TDM_I2S_TX_L_DAC1_1_SFT) |
			(first_bit << RT1015_TDM_I2S_TX_R_DAC1_1_SFT));
		break;
	default:
		ret = -EINVAL;
		goto _set_tdm_err_;
	}

	/* Tx slot configuration */
	tx_slotnum = hweight_long(tx_mask);
	if (tx_slotnum) {
		ret = -EINVAL;
		dev_err(component->dev, "doesn't need to support tx slots\n");
		goto _set_tdm_err_;
	}

	snd_soc_component_update_bits(component, RT1015_TDM1_1,
		RT1015_I2S_CH_TX_MASK | RT1015_I2S_CH_RX_MASK |
		RT1015_I2S_CH_TX_LEN_MASK | RT1015_I2S_CH_RX_LEN_MASK, val);

_set_tdm_err_:
	return ret;
}

static int rt1015_probe(struct snd_soc_component *component)
{
	struct rt1015_priv *rt1015 =
		snd_soc_component_get_drvdata(component);

	rt1015->component = component;

	return 0;
}

static void rt1015_remove(struct snd_soc_component *component)
{
	struct rt1015_priv *rt1015 = snd_soc_component_get_drvdata(component);

	regmap_write(rt1015->regmap, RT1015_RESET, 0);
}

#define RT1015_STEREO_RATES SNDRV_PCM_RATE_8000_192000
#define RT1015_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static const struct snd_soc_dai_ops rt1015_aif_dai_ops = {
	.hw_params = rt1015_hw_params,
	.set_fmt = rt1015_set_dai_fmt,
	.set_tdm_slot = rt1015_set_tdm_slot,
};

static struct snd_soc_dai_driver rt1015_dai[] = {
	{
		.name = "rt1015-aif",
		.id = 0,
		.playback = {
			.stream_name = "AIF Playback",
			.channels_min = 1,
			.channels_max = 4,
			.rates = RT1015_STEREO_RATES,
			.formats = RT1015_FORMATS,
		},
		.ops = &rt1015_aif_dai_ops,
	}
};

#ifdef CONFIG_PM
static int rt1015_suspend(struct snd_soc_component *component)
{
	struct rt1015_priv *rt1015 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt1015->regmap, true);
	regcache_mark_dirty(rt1015->regmap);

	return 0;
}

static int rt1015_resume(struct snd_soc_component *component)
{
	struct rt1015_priv *rt1015 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt1015->regmap, false);
	regcache_sync(rt1015->regmap);

	if (rt1015->cali_done)
		rt1015_calibrate(rt1015);

	return 0;
}
#else
#define rt1015_suspend NULL
#define rt1015_resume NULL
#endif

static const struct snd_soc_component_driver soc_component_dev_rt1015 = {
	.probe = rt1015_probe,
	.remove = rt1015_remove,
	.suspend = rt1015_suspend,
	.resume = rt1015_resume,
	.controls = rt1015_snd_controls,
	.num_controls = ARRAY_SIZE(rt1015_snd_controls),
	.dapm_widgets = rt1015_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt1015_dapm_widgets),
	.dapm_routes = rt1015_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt1015_dapm_routes),
	.set_sysclk = rt1015_set_component_sysclk,
	.set_pll = rt1015_set_component_pll,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config rt1015_regmap = {
	.reg_bits = 16,
	.val_bits = 16,
	.max_register = RT1015_S_BST_TIMING_INTER36,
	.volatile_reg = rt1015_volatile_register,
	.readable_reg = rt1015_readable_register,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt1015_reg,
	.num_reg_defaults = ARRAY_SIZE(rt1015_reg),
};

static const struct i2c_device_id rt1015_i2c_id[] = {
	{ "rt1015", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt1015_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id rt1015_of_match[] = {
	{ .compatible = "realtek,rt1015", },
	{},
};
MODULE_DEVICE_TABLE(of, rt1015_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id rt1015_acpi_match[] = {
	{"10EC1015", 0,},
	{},
};
MODULE_DEVICE_TABLE(acpi, rt1015_acpi_match);
#endif

static void rt1015_parse_dt(struct rt1015_priv *rt1015, struct device *dev)
{
	device_property_read_u32(dev, "realtek,power-up-delay-ms",
		&rt1015->pdata.power_up_delay_ms);
}

static int rt1015_i2c_probe(struct i2c_client *i2c,
	const struct i2c_device_id *id)
{
	struct rt1015_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct rt1015_priv *rt1015;
	int ret;
	unsigned int val;

	rt1015 = devm_kzalloc(&i2c->dev, sizeof(*rt1015), GFP_KERNEL);
	if (!rt1015)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt1015);

	rt1015->pdata = i2s_default_platform_data;

	if (pdata)
		rt1015->pdata = *pdata;
	else
		rt1015_parse_dt(rt1015, &i2c->dev);

	rt1015->regmap = devm_regmap_init_i2c(i2c, &rt1015_regmap);
	if (IS_ERR(rt1015->regmap)) {
		ret = PTR_ERR(rt1015->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = regmap_read(rt1015->regmap, RT1015_DEVICE_ID, &val);
	if (ret) {
		dev_err(&i2c->dev,
			"Failed to read device register: %d\n", ret);
		return ret;
	} else if ((val != RT1015_DEVICE_ID_VAL) &&
			(val != RT1015_DEVICE_ID_VAL2)) {
		dev_err(&i2c->dev,
			"Device with ID register %x is not rt1015\n", val);
		return -ENODEV;
	}

	return devm_snd_soc_register_component(&i2c->dev,
		&soc_component_dev_rt1015,
		rt1015_dai, ARRAY_SIZE(rt1015_dai));
}

static void rt1015_i2c_shutdown(struct i2c_client *client)
{
	struct rt1015_priv *rt1015 = i2c_get_clientdata(client);

	regmap_write(rt1015->regmap, RT1015_RESET, 0);
}

static struct i2c_driver rt1015_i2c_driver = {
	.driver = {
		.name = "rt1015",
		.of_match_table = of_match_ptr(rt1015_of_match),
		.acpi_match_table = ACPI_PTR(rt1015_acpi_match),
	},
	.probe = rt1015_i2c_probe,
	.shutdown = rt1015_i2c_shutdown,
	.id_table = rt1015_i2c_id,
};
module_i2c_driver(rt1015_i2c_driver);

MODULE_DESCRIPTION("ASoC RT1015 driver");
MODULE_AUTHOR("Jack Yu <jack.yu@realtek.com>");
MODULE_LICENSE("GPL v2");
