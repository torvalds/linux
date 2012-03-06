/*
 * rt5625.c  --  RT5625 ALSA SoC audio codec driver
 *
 * Copyright 2011 Realtek Semiconductor Corp.
 * Author: Johnny Hsu <johnnyhsu@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rt5625.h"

#define RT5625_PROC
#ifdef RT5625_PROC
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
char debug_write_read = 0;
#endif

#if 1
#define DBG(x...) printk(KERN_INFO x)
#else
#define DBG(x...) do { } while (0)
#endif

static struct snd_soc_codec *rt5625_codec;

#define RT5625_REG_RW 1 /* for debug */
//#define RT5625_DEMO 1 	/* only for demo; please remove it */

#define RT5625_F_SMT_PHO
#define RT5625_PLY_BIT 0
#define RT5625_PLY_MASK (0x1)
#define RT5625_REC_BIT 1
#define RT5625_REC_MASK (0x1 << RT5625_REC_BIT)
#define RT5625_3G_BIT 2
#define RT5625_3G_MASK (0x1 << RT5625_3G_BIT)
#define RT5625_BT_BIT 3
#define RT5625_BT_MASK (0x1 << RT5625_BT_BIT)
#define RT5625_VOIP_BIT 4
#define RT5625_VOIP_MASK (0x1 << RT5625_VOIP_BIT)

struct rt5625_priv {
	unsigned int stereo_sysclk;
	unsigned int voice_sysclk;

	int vodsp_fun;
#ifdef RT5625_F_SMT_PHO
	int app_bmp;/* bit{0, 1, 2, 3, 4} = {play, rec, 3g, bt, voip} */
	int pll_sel;
	int pll2_sel;
	int dac_active;
	int adc_active;
	int headset;
	int vodsp_fun_bak;
#endif
};

#ifdef RT5625_F_SMT_PHO
static u16 rt5625_voip_back[][2] = {
	{RT5625_VODSP_PDM_CTL, 0x0000},
	{RT5625_F_DAC_ADC_VDAC, 0x0000},
};
#define RT5625_VOIP_BK_NUM \
	(sizeof(rt5625_voip_back) / sizeof(rt5625_voip_back[0]))
#endif

#ifdef RT5625_DEMO
struct rt5625_init_reg {
	u8 reg;
	u16 val;
};

static struct rt5625_init_reg init_list[] = {
	{RT5625_HP_OUT_VOL		, 0x8888},	//default is -12db
	{RT5625_SPK_OUT_VOL 		, 0x8080},	//default is 0db
	{RT5625_PHONEIN_VOL 	, 0xe800},	//phone differential
	{RT5625_DAC_MIC_CTRL	, 0xee01},	//DAC to hpmixer & spkmixer
	{RT5625_OUTMIX_CTRL		, 0x2bc8},	//spk from spkmixer; hp from hpmixer; aux from monomixer; classAB
	{RT5625_ADC_REC_MIXER	, 0x1f1f},	//record source from mic1 & mic2
	{RT5625_GEN_CTRL1		, 0x0c08},	//speaker vdd ratio is 1; 1.25VDD ratio
};
#define RT5625_INIT_REG_LEN ARRAY_SIZE(init_list)

static int rt5625_reg_init(struct snd_soc_codec *codec)
{
	int i;
	for (i = 0; i < RT5625_INIT_REG_LEN; i++)
		snd_soc_write(codec, init_list[i].reg, init_list[i].val);
	return 0;
}
#endif

static const u16 rt5625_reg[0x80] = {
	[RT5625_RESET] = 0x59b4,
	[RT5625_SPK_OUT_VOL] = 0x8080,
	[RT5625_HP_OUT_VOL] = 0x8080,
	[RT5625_AUX_OUT_VOL] = 0x8080,
	[RT5625_PHONEIN_VOL] = 0xc800,
	[RT5625_LINE_IN_VOL] = 0xe808,
	[RT5625_DAC_VOL] = 0x1010,
	[RT5625_MIC_VOL] = 0x0808,
	[RT5625_DAC_MIC_CTRL] = 0xee0f,
	[RT5625_ADC_REC_GAIN] = 0xcbcb,
	[RT5625_ADC_REC_MIXER] = 0x7f7f,
	[RT5625_VDAC_OUT_VOL] = 0xe010,
	[RT5625_OUTMIX_CTRL] = 0x8008,
	[RT5625_VODSP_CTL] = 0x2007,
	[RT5625_DMIC_CTRL] = 0x00c0,
	[RT5625_PD_CTRL] = 0xef00,
	[RT5625_GEN_CTRL1] = 0x0c0a,
	[RT5625_LDO_CTRL] = 0x0029,
	[RT5625_GPIO_CONFIG] = 0xbe3e,
	[RT5625_GPIO_POLAR] = 0x3e3e,
	[RT5625_GPIO_STATUS] = 0x803a,
	[RT5625_SOFT_VOL_CTRL] = 0x0009,
	[RT5625_DAC_CLK_CTRL1] = 0x3075,
	[RT5625_DAC_CLK_CTRL2] = 0x1010,
	[RT5625_VDAC_CLK_CTRL1] = 0x3110,
	[RT5625_PS_CTRL] = 0x0553,
	[RT5625_VENDOR_ID1] = 0x10ec,
	[RT5625_VENDOR_ID2] = 0x5c02,
};

rt5625_dsp_reg rt5625_dsp_init[] = {
	{0x232C, 0x0025},
	{0x230B, 0x0001},
	{0x2308, 0x007F},
	{0x23F8, 0x4003},
	{0x2301, 0x0002},
	{0x2328, 0x0001},
	{0x2304, 0x00FA},
	{0x2305, 0x0500},
	{0x2306, 0x4000},
	{0x230D, 0x0300},
	{0x230E, 0x0280},
	{0x2312, 0x00B1},
	{0x2314, 0xC000},
	{0x2316, 0x0041},
	{0x2317, 0x2800},
	{0x2318, 0x0800},
	{0x231D, 0x0050},
	{0x231F, 0x4000},
	{0x2330, 0x0008},
	{0x2335, 0x000A},
	{0x2336, 0x0004},
	{0x2337, 0x5000},
	{0x233A, 0x0300},
	{0x233B, 0x0030},
	{0x2341, 0x0008},
	{0x2343, 0x0800},	
	{0x2352, 0x7FFF},
	{0x237F, 0x0400},
	{0x23A7, 0x2800},
	{0x22CE, 0x0400},
	{0x22D3, 0x1500},
	{0x22D4, 0x2800},
	{0x22D5, 0x3000},
	{0x2399, 0x2800},
	{0x230C, 0x0000},	
};
#define RT5625_DSP_INIT_NUM ARRAY_SIZE(rt5625_dsp_init)

static int rt5625_volatile_register(
	struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case RT5625_RESET:
	case RT5625_PD_CTRL:
	case RT5625_GPIO_STATUS:
	case RT5625_OTC_STATUS:
	case RT5625_PRIV_DATA:
	case RT5625_EQ_CTRL:
	case RT5625_DSP_DATA:
	case RT5625_DSP_CMD:
	case RT5625_VENDOR_ID1:
	case RT5625_VENDOR_ID2:
		return 1;
	default:
		return 0;
	}
}

static int rt5625_readable_register(
	struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case RT5625_RESET:
	case RT5625_SPK_OUT_VOL:
	case RT5625_HP_OUT_VOL:
	case RT5625_AUX_OUT_VOL:
	case RT5625_PHONEIN_VOL:
	case RT5625_LINE_IN_VOL:
	case RT5625_DAC_VOL:
	case RT5625_MIC_VOL:
	case RT5625_DAC_MIC_CTRL:
	case RT5625_ADC_REC_GAIN:
	case RT5625_ADC_REC_MIXER:
	case RT5625_VDAC_OUT_VOL:
	case RT5625_VODSP_PDM_CTL:
	case RT5625_OUTMIX_CTRL:
	case RT5625_VODSP_CTL:
	case RT5625_MIC_CTRL:
	case RT5625_DMIC_CTRL:
	case RT5625_PD_CTRL:
	case RT5625_F_DAC_ADC_VDAC:
	case RT5625_SDP_CTRL:
	case RT5625_EXT_SDP_CTRL:
	case RT5625_PWR_ADD1:
	case RT5625_PWR_ADD2:
	case RT5625_PWR_ADD3:
	case RT5625_GEN_CTRL1:
	case RT5625_GEN_CTRL2:
	case RT5625_PLL_CTRL:
	case RT5625_PLL2_CTRL:
	case RT5625_LDO_CTRL:
	case RT5625_GPIO_CONFIG:
	case RT5625_GPIO_POLAR:
	case RT5625_GPIO_STICKY:
	case RT5625_GPIO_WAKEUP:
	case RT5625_GPIO_STATUS:
	case RT5625_GPIO_SHARING:
	case RT5625_OTC_STATUS:
	case RT5625_SOFT_VOL_CTRL:
	case RT5625_GPIO_OUT_CTRL:
	case RT5625_MISC_CTRL:
	case RT5625_DAC_CLK_CTRL1:
	case RT5625_DAC_CLK_CTRL2:
	case RT5625_VDAC_CLK_CTRL1:
	case RT5625_PS_CTRL:
	case RT5625_PRIV_INDEX:
	case RT5625_PRIV_DATA:
	case RT5625_EQ_CTRL:
	case RT5625_DSP_ADDR:
	case RT5625_DSP_DATA:
	case RT5625_DSP_CMD:
	case RT5625_VENDOR_ID1:
	case RT5625_VENDOR_ID2:
		return 1;
	default:
		return 0;
	}
}

static unsigned int rt5625_read(struct snd_soc_codec *codec,
				unsigned int reg)
{
	unsigned int val;

	val = codec->hw_read(codec, reg);
	return val;
}

static int rt5625_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	unsigned int val;
	u8 data[3];

	data[0] = reg;
	data[1] = (value >> 8) & 0xff;
	data[2] = value & 0xff;
	
	val = codec->hw_write(codec->control_data, data, 3);
	return val;
}

static int rt5625_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, RT5625_RESET, 0);
}

/**
 * rt5625_index_write - Write private register.
 * @codec: SoC audio codec device.
 * @reg: Private register index.
 * @value: Private register Data.
 *
 * Modify private register for advanced setting. It can be written through
 * private index (0x6a) and data (0x6c) register.
 *
 * Returns 0 for success or negative error code.
 */
static int rt5625_index_write(struct snd_soc_codec *codec,
		unsigned int reg, unsigned int value)
{
	int ret;

	ret = snd_soc_write(codec, RT5625_PRIV_INDEX, reg);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set private addr: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT5625_PRIV_DATA, value);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set private value: %d\n", ret);
		goto err;
	}
	return 0;

err:
	return ret;
}

/**
 * rt5625_index_read - Read private register.
 * @codec: SoC audio codec device.
 * @reg: Private register index.
 *
 * Read advanced setting from private register. It can be read through
 * private index (0x6a) and data (0x6c) register.
 *
 * Returns private register value or negative error code.
 */
static unsigned int rt5625_index_read(
	struct snd_soc_codec *codec, unsigned int reg)
{
	int ret;

	ret = snd_soc_write(codec, RT5625_PRIV_INDEX, reg);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set private addr: %d\n", ret);
		return ret;
	}
	return snd_soc_read(codec, RT5625_PRIV_DATA);
}

/**
 * rt5625_index_update_bits - update private register bits
 * @codec: Audio codec
 * @reg: Private register index.
 * @mask: Register mask
 * @value: New value
 *
 * Writes new register value.
 *
 * Returns 1 for change, 0 for no change, or negative error code.
 */
static int rt5625_index_update_bits(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int mask, unsigned int value)
{
	unsigned int old, new;
	int change, ret;

	ret = rt5625_index_read(codec, reg);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read private reg: %d\n", ret);
		goto err;
	}

	old = ret;
	new = (old & ~mask) | (value & mask);
	change = old != new;
	if (change) {
		ret = rt5625_index_write(codec, reg, new);
		if (ret < 0) {
			dev_err(codec->dev,
				"Failed to write private reg: %d\n", ret);
			goto err;
		}
	}
	return change;

err:
	return ret;
}

/**
 * rt5625_dsp_done - Wait until DSP is ready.
 * @codec: SoC Audio Codec device.
 *
 * To check voice DSP status and confirm it's ready for next work.
 *
 * Returns 0 for success or negative error code.
 */
static int rt5625_dsp_done(struct snd_soc_codec *codec)
{
	unsigned int count = 0, dsp_val;

	dsp_val = snd_soc_read(codec, RT5625_DSP_CMD);
	while(dsp_val & RT5625_DSP_BUSY_MASK) {
		if(count > 10)
			return -EBUSY;
		dsp_val = snd_soc_read(codec, RT5625_DSP_CMD);
		count ++;		
	}

	return 0;
}

/**
 * rt5625_dsp_write - Write DSP register.
 * @codec: SoC audio codec device.
 * @reg: DSP register index.
 * @value: DSP register Data.
 *
 * Modify voice DSP register for sound effect. The DSP can be controlled
 * through DSP addr (0x70), data (0x72) and cmd (0x74) register. It has
 * to wait until the DSP is ready.
 *
 * Returns 0 for success or negative error code.
 */
static int rt5625_dsp_write(struct snd_soc_codec *codec,
		unsigned int reg, unsigned int value)
{
	int ret;

	ret = rt5625_dsp_done(codec);
	if (ret < 0) {
		dev_err(codec->dev, "DSP is busy: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT5625_DSP_ADDR, reg);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP addr reg: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT5625_DSP_DATA, value);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP data reg: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT5625_DSP_CMD,
		RT5625_DSP_W_EN | RT5625_DSP_CMD_MW);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP cmd reg: %d\n", ret);
		goto err;
	}
	mdelay(10);
	return 0;
	
err:
	return ret;
}

/**
 * rt5625_dsp_read - Read DSP register.
 * @codec: SoC audio codec device.
 * @reg: DSP register index.
 *
 * Read DSP setting value from voice DSP. The DSP can be controlled
 * through DSP addr (0x70), data (0x72) and cmd (0x74) register. Each
 * command has to wait until the DSP is ready.
 *
 * Returns DSP register value or negative error code.
 */
static unsigned int rt5625_dsp_read(
	struct snd_soc_codec *codec, unsigned int reg)
{
	unsigned int val_h, val_l;
	int ret = 0;

	ret = rt5625_dsp_done(codec);
	if (ret < 0) {
		dev_err(codec->dev, "DSP is busy: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT5625_DSP_ADDR, reg);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP addr reg: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT5625_DSP_CMD,
		RT5625_DSP_R_EN | RT5625_DSP_CMD_MR);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP cmd reg: %d\n", ret);
		goto err;
	}

	/* Read DSP high byte data */
	ret = rt5625_dsp_done(codec);
	if (ret < 0) {
		dev_err(codec->dev, "DSP is busy: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT5625_DSP_ADDR, 0x26);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP addr reg: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT5625_DSP_CMD,
		RT5625_DSP_R_EN | RT5625_DSP_CMD_RR);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP cmd reg: %d\n", ret);
		goto err;
	}
	ret = rt5625_dsp_done(codec);
	if (ret < 0) {
		dev_err(codec->dev, "DSP is busy: %d\n", ret);
		goto err;
	}
	ret = snd_soc_read(codec, RT5625_DSP_DATA);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read DSP data reg: %d\n", ret);
		goto err;
	}
	val_h = ret;

	/* Read DSP low byte data */
	ret = snd_soc_write(codec, RT5625_DSP_ADDR, 0x25);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP addr reg: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT5625_DSP_CMD,
		RT5625_DSP_R_EN | RT5625_DSP_CMD_RR);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP cmd reg: %d\n", ret);
		goto err;
	}
	ret = rt5625_dsp_done(codec);
	if (ret < 0) {
		dev_err(codec->dev, "DSP is busy: %d\n", ret);
		goto err;
	}
	ret = snd_soc_read(codec, RT5625_DSP_DATA);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read DSP data reg: %d\n", ret);
		goto err;
	}
	val_l = ret;

	return ((val_h & 0xff) << 8) |(val_l & 0xff);

err:
	return ret;
}

/* ADCR function select */
static const char *adcr_fun_sel[] = {
	"Stereo ADC", "Voice ADC", "VoDSP", "PDM Slave"};

static const struct soc_enum adcr_fun_sel_enum =
	SOC_ENUM_SINGLE(RT5625_F_DAC_ADC_VDAC, RT5625_ADCR_F_SFT,
				ARRAY_SIZE(adcr_fun_sel), adcr_fun_sel);

/* ADCL function select */
static const char *adcl_fun_sel[] = {"Stereo ADC", "VoDSP"};

static const struct soc_enum adcl_fun_sel_enum =
	SOC_ENUM_SINGLE(RT5625_F_DAC_ADC_VDAC, RT5625_ADCL_F_SFT,
			ARRAY_SIZE(adcl_fun_sel), adcl_fun_sel);

/* Voice DSP */
static const char *rt5625_aec_fun[] = {"Disable", "Enable"};

static const SOC_ENUM_SINGLE_DECL(rt5625_aec_fun_enum, 0, 0, rt5625_aec_fun);

static const char *rt5625_dsp_lrck[] = {"8KHz", "16KHz"};

static const SOC_ENUM_SINGLE_DECL(rt5625_dsp_lrck_enum,
	RT5625_VODSP_CTL, RT5625_DSP_LRCK_SFT, rt5625_dsp_lrck);

static const char *rt5625_bp_ctrl[] = {"Bypass", "Normal"};

static const SOC_ENUM_SINGLE_DECL(rt5625_bp_ctrl_enum,
	RT5625_VODSP_CTL, RT5625_DSP_BP_SFT, rt5625_bp_ctrl);

static const char *rt5625_pd_ctrl[] = {"Power down", "Normal"};

static const SOC_ENUM_SINGLE_DECL(rt5625_pd_ctrl_enum,
	RT5625_VODSP_CTL, RT5625_DSP_PD_SFT, rt5625_pd_ctrl);

static const char *rt5625_rst_ctrl[] = {"Reset", "Normal"};

static const SOC_ENUM_SINGLE_DECL(rt5625_rst_ctrl_enum,
	RT5625_VODSP_CTL, RT5625_DSP_RST_SFT, rt5625_rst_ctrl);
/* Speaker */
static const char *rt5625_spk_out[] = {"Class AB", "Class D"};

static const SOC_ENUM_SINGLE_DECL(rt5625_spk_out_enum,
	RT5625_OUTMIX_CTRL, RT5625_SPK_T_SFT, rt5625_spk_out);

static const char *rt5625_spkl_src[] = {"LPRN", "LPRP", "LPLN", "MM"};

static const SOC_ENUM_SINGLE_DECL(rt5625_spkl_src_enum,
	RT5625_OUTMIX_CTRL, RT5625_SPKN_S_SFT, rt5625_spkl_src);

static const char *rt5625_spkamp_ratio[] = {"2.25 Vdd", "2.00 Vdd",
		"1.75 Vdd", "1.50 Vdd", "1.25 Vdd", "1.00 Vdd"};

static const SOC_ENUM_SINGLE_DECL(rt5625_spkamp_ratio_enum,
	RT5625_GEN_CTRL1, RT5625_SPK_R_SFT, rt5625_spkamp_ratio);

/* Output/Input Mode */
//static const char *rt5625_auxout_mode[] = {"Differential", "Single ended"};

//static const SOC_ENUM_SINGLE_DECL(rt5625_auxout_mode_enum,
//	RT5625_OUTMIX_CTRL, RT5625_AUXOUT_MODE_SFT, rt5625_auxout_mode);

//static const char *rt5625_input_mode[] = {"Single ended", "Differential"};

//static const SOC_ENUM_SINGLE_DECL(rt5625_phone_mode_enum,
//	RT5625_PHONEIN_VOL, RT5625_PHO_DIFF_SFT, rt5625_input_mode);

//static const SOC_ENUM_SINGLE_DECL(rt5625_mic1_mode_enum,
//	RT5625_MIC_VOL, RT5625_MIC1_DIFF_SFT, rt5625_input_mode);

//static const SOC_ENUM_SINGLE_DECL(rt5625_mic2_mode_enum,
//	RT5625_MIC_VOL, RT5625_MIC2_DIFF_SFT, rt5625_input_mode);

static int rt5625_adcr_fun_sel_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	unsigned int val, mask, bitmask;

	for (bitmask = 1; bitmask < e->max; bitmask <<= 1)
		;
	if (ucontrol->value.enumerated.item[0] > e->max - 1)
		return -EINVAL;
	val = ucontrol->value.enumerated.item[0] << e->shift_l;
	mask = (bitmask - 1) << e->shift_l;

	snd_soc_update_bits(codec, RT5625_PD_CTRL,
		RT5625_PWR_PR0, RT5625_PWR_PR0);
	if ((rt5625->app_bmp & RT5625_3G_MASK) &&
		rt5625->vodsp_fun == RT5625_AEC_EN) {
		snd_soc_update_bits(codec, e->reg, mask, RT5625_ADCR_F_PDM);
	} else if (rt5625->app_bmp & RT5625_VOIP_MASK &&
		rt5625->vodsp_fun == RT5625_AEC_EN) {
		snd_soc_update_bits(codec, e->reg, mask, RT5625_ADCR_F_PDM);
	} else if (rt5625->app_bmp & RT5625_BT_MASK) {
		snd_soc_update_bits(codec, e->reg, mask, RT5625_ADCR_F_VADC);
	} else {
		snd_soc_update_bits(codec, e->reg, mask, val);
	}
	snd_soc_update_bits(codec, RT5625_PD_CTRL, RT5625_PWR_PR0, 0);

	return 0;
}

static int rt5625_adcl_fun_sel_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	unsigned int val, mask, bitmask;

	for (bitmask = 1; bitmask < e->max; bitmask <<= 1)
		;
	if (ucontrol->value.enumerated.item[0] > e->max - 1)
		return -EINVAL;
	val = ucontrol->value.enumerated.item[0] << e->shift_l;
	mask = (bitmask - 1) << e->shift_l;

	snd_soc_update_bits(codec, RT5625_PD_CTRL,
		RT5625_PWR_PR0, RT5625_PWR_PR0);
	if ((rt5625->app_bmp & RT5625_3G_MASK) &&
		rt5625->vodsp_fun == RT5625_AEC_EN) {
		snd_soc_update_bits(codec, e->reg, mask, RT5625_ADCL_F_DSP);
	} else {
		snd_soc_update_bits(codec, e->reg, mask, val);
	}
	snd_soc_update_bits(codec, RT5625_PD_CTRL, RT5625_PWR_PR0, 0);

	return 0;
}

static int rt5625_init_vodsp_aec(struct snd_soc_codec *codec)
{
	int i, ret = 0;

	/*disable LDO power*/
	snd_soc_update_bits(codec, RT5625_LDO_CTRL,
		RT5625_LDO_MASK, RT5625_LDO_DIS);
	mdelay(20);	
	snd_soc_update_bits(codec, RT5625_VODSP_CTL,
		RT5625_DSP_PD_MASK, RT5625_DSP_PD_NOR);
	/*enable LDO power and set output voltage to 1.2V*/
	snd_soc_update_bits(codec, RT5625_LDO_CTRL,
		RT5625_LDO_MASK | RT5625_LDO_VC_MASK,
		RT5625_LDO_EN | RT5625_LDO_VC_1_20V);
	mdelay(20);
	/*enable power of VODSP I2C interface*/ 
	snd_soc_update_bits(codec, RT5625_PWR_ADD3, RT5625_P_DSP_IF |
		RT5625_P_DSP_I2C, RT5625_P_DSP_IF | RT5625_P_DSP_I2C);
	mdelay(1);
	/*Reset VODSP*/
	snd_soc_update_bits(codec, RT5625_VODSP_CTL,
		RT5625_DSP_RST_MASK, RT5625_DSP_RST_EN);
	mdelay(1);
	/*set VODSP to non-reset status*/
	snd_soc_update_bits(codec, RT5625_VODSP_CTL,
		RT5625_DSP_RST_MASK, RT5625_DSP_RST_NOR);
	mdelay(20);

	/*initize AEC paramter*/
	for(i = 0; i < RT5625_DSP_INIT_NUM; i++) {
		ret = rt5625_dsp_write(codec, rt5625_dsp_init[i].index,
					rt5625_dsp_init[i].value);
		if(ret)
			return -EIO;
	}		
	mdelay(10);	
	//printk("[DSP poweron] 0x%04x: 0x%04x\n", 0x230C, rt5625_dsp_read(codec, 0x230C));

	return 0;
}

static int rt5625_aec_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = rt5625->vodsp_fun;
	return 0;
}


static int rt5625_aec_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{ 
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	if(ucontrol->value.integer.value[0] == rt5625->vodsp_fun)
		return 0;
	rt5625->vodsp_fun = ucontrol->value.integer.value[0];

	switch(rt5625->vodsp_fun) {
	case RT5625_AEC_EN:
		break;
	case RT5625_AEC_DIS:
		if (!(rt5625->app_bmp & RT5625_3G_MASK) ||
			((rt5625->app_bmp & RT5625_3G_MASK) && rt5625->headset)) {
			snd_soc_update_bits(codec, RT5625_VODSP_CTL,
				RT5625_DSP_PD_MASK, RT5625_DSP_PD_EN);
			snd_soc_update_bits(codec, RT5625_PWR_ADD3,
				RT5625_P_DSP_IF | RT5625_P_DSP_I2C, 0);
			snd_soc_update_bits(codec, RT5625_LDO_CTRL,
				RT5625_LDO_MASK, RT5625_LDO_DIS);
		}
		break;
	default:
		break;
	}

	return 0;
}

#ifdef RT5625_F_SMT_PHO
static int rt5625_app_get(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	pr_info("App status: %x\n", rt5625->app_bmp);

	return 0;
}

static int rt5625_cap_voip_chk_put(struct snd_kcontrol *kcontrol, 
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	int i, upd;

	/* VoIP start up if record & playback all turn on, or AEC	*
	 * is disabled. otherwise, cheat dapm.			*/
	if ((rt5625->app_bmp & RT5625_REC_MASK) == 0 ||
		(rt5625->app_bmp & RT5625_PLY_MASK) == 0) {
		/* backup registers for voip routing */
		for(i = 0; i < RT5625_VOIP_BK_NUM; i++)
			rt5625_voip_back[i][1] =
				snd_soc_read(codec, rt5625_voip_back[i][0]);
		/* cheat dapm */
		snd_soc_update_bits(codec, RT5625_VODSP_PDM_CTL,
			RT5625_REC_IIS_S_MASK, RT5625_REC_IIS_S_SRC2);
		return 0;
	}

	if (rt5625->headset) {
		/* backup registers for voip routing */
		for(i = 0; i < RT5625_VOIP_BK_NUM; i++)
			rt5625_voip_back[i][1] =
				snd_soc_read(codec, rt5625_voip_back[i][0]);
		/* cheat dapm */
		snd_soc_update_bits(codec, RT5625_VODSP_PDM_CTL,
			RT5625_REC_IIS_S_MASK, RT5625_REC_IIS_S_SRC2);
	} else
		rt5625->vodsp_fun = RT5625_AEC_EN;

	upd = (rt5625->app_bmp & ~RT5625_VOIP_MASK) |
		(ucontrol->value.integer.value[0] << RT5625_VOIP_BIT);
	if (rt5625->app_bmp != upd) {
		rt5625->app_bmp = upd;
		rt5625->app_bmp &= ~(RT5625_3G_MASK | RT5625_BT_MASK);
	}

	return 0;
}

static int rt5625_cap_voip_put(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	int i;

	if (rt5625->app_bmp & RT5625_VOIP_MASK) {
		if (rt5625->headset) {
			snd_soc_update_bits(codec, RT5625_ADC_REC_MIXER,
				RT5625_M_RM_L_MIC1 | RT5625_M_RM_R_MIC1 | RT5625_M_RM_L_PHO,
				RT5625_M_RM_L_MIC1 | RT5625_M_RM_R_MIC1 | RT5625_M_RM_L_PHO);
			/* recover all changes by voip */
			for(i = 0; i < RT5625_VOIP_BK_NUM; i++)
				snd_soc_write(codec, rt5625_voip_back[i][0],
						rt5625_voip_back[i][1]);
			snd_soc_update_bits(codec, RT5625_VODSP_PDM_CTL,
				RT5625_SRC1_PWR | RT5625_SRC2_PWR, 0);
		} else {
			snd_soc_update_bits(codec, RT5625_ADC_REC_MIXER,
				RT5625_M_RM_L_MIC1 | RT5625_M_RM_L_MIC2 | RT5625_M_RM_R_MIC2 | RT5625_M_RM_L_PHO,
				RT5625_M_RM_L_MIC1 | RT5625_M_RM_L_MIC2 | RT5625_M_RM_R_MIC2 | RT5625_M_RM_L_PHO);
			rt5625->vodsp_fun = RT5625_AEC_EN;
			/* Mic1 & Mic2 boost 0db */
			snd_soc_update_bits(codec, RT5625_MIC_CTRL,
				RT5625_MIC1_BST_MASK | RT5625_MIC2_BST_MASK,
				RT5625_MIC1_BST_BYPASS | RT5625_MIC2_BST_BYPASS);
			/* Capture volume gain 9db */
			snd_soc_update_bits(codec, RT5625_ADC_REC_GAIN,
				RT5625_G_ADCL_MASK | RT5625_G_ADCR_MASK, (0x11 << 8 ) | 0x11);
		}
	} else {
		/* recover all changes by voip */
		for(i = 0; i < RT5625_VOIP_BK_NUM; i++)
			snd_soc_write(codec, rt5625_voip_back[i][0],
					rt5625_voip_back[i][1]);
		snd_soc_update_bits(codec, RT5625_VODSP_PDM_CTL,
			RT5625_SRC1_PWR | RT5625_SRC2_PWR, 0);
		if (rt5625->app_bmp & RT5625_3G_MASK &&
			rt5625->vodsp_fun ==  RT5625_AEC_EN) {
			/* Mic1 boost 0db */
			snd_soc_update_bits(codec, RT5625_MIC_CTRL,
				RT5625_MIC1_BST_MASK, RT5625_MIC1_BST_BYPASS);
			/* Capture volume gain 9db */
			snd_soc_update_bits(codec, RT5625_ADC_REC_GAIN,
				RT5625_G_ADCR_MASK, 0x11);
		}
	}

	return 0;
}

static int rt5625_hs_voip_chk_put(struct snd_kcontrol *kcontrol, 
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	int i, upd;

	rt5625->headset = true;
	if ((rt5625->app_bmp & RT5625_REC_MASK) == 0 ||
		(rt5625->app_bmp & RT5625_PLY_MASK) == 0) {
		/* backup registers for voip routing */
		for(i = 0; i < RT5625_VOIP_BK_NUM; i++)
			rt5625_voip_back[i][1] =
				snd_soc_read(codec, rt5625_voip_back[i][0]);
		rt5625->vodsp_fun_bak = rt5625->vodsp_fun;
		/* cheat dapm */
		snd_soc_update_bits(codec, RT5625_VODSP_PDM_CTL,
			RT5625_REC_IIS_S_MASK | RT5625_RXDP_PWR, RT5625_REC_IIS_S_ADC);
		return 0;
	}

	upd = (rt5625->app_bmp & ~RT5625_VOIP_MASK) |
		(ucontrol->value.integer.value[0] << RT5625_VOIP_BIT);
	if (rt5625->app_bmp != upd) {
		rt5625->app_bmp = upd;
		rt5625->app_bmp &= ~(RT5625_3G_MASK | RT5625_BT_MASK);
	}

	return 0;
}

static int rt5625_hs_voip_put(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	int i;

	if (rt5625->app_bmp & RT5625_VOIP_MASK) {
		snd_soc_update_bits(codec, RT5625_ADC_REC_MIXER,
			RT5625_M_RM_R_MIC1 | RT5625_M_RM_L_PHO,
			RT5625_M_RM_R_MIC1 | RT5625_M_RM_L_PHO);
		/* Mic1 & Mic2 boost 0db */
		snd_soc_update_bits(codec, RT5625_MIC_CTRL,
			RT5625_MIC1_BST_MASK | RT5625_MIC2_BST_MASK,
			RT5625_MIC1_BST_BYPASS | RT5625_MIC2_BST_BYPASS);
		/* Capture volume gain 9db */
		snd_soc_update_bits(codec, RT5625_ADC_REC_GAIN,
			RT5625_G_ADCL_MASK | RT5625_G_ADCR_MASK, (0x11 << 8 ) | 0x11);
	} else {
		/* recover all changes by voip */
		for(i = 0; i < RT5625_VOIP_BK_NUM; i++)
			snd_soc_write(codec, rt5625_voip_back[i][0],
					rt5625_voip_back[i][1]);
		rt5625->vodsp_fun = rt5625->vodsp_fun_bak;
		if ((rt5625->app_bmp & RT5625_3G_MASK) && rt5625->vodsp_fun == RT5625_AEC_EN) {
			snd_soc_update_bits(codec, RT5625_F_DAC_ADC_VDAC,
				RT5625_ADCR_F_MASK, RT5625_ADCR_F_PDM);
		} else if (rt5625->app_bmp & RT5625_BT_MASK) {
			snd_soc_update_bits(codec, RT5625_F_DAC_ADC_VDAC,
				RT5625_ADCR_F_MASK, RT5625_ADCR_F_VADC);
		} else {
			snd_soc_update_bits(codec, RT5625_F_DAC_ADC_VDAC,
				RT5625_ADCR_F_MASK, RT5625_ADCR_F_ADC);
		}
	}

	return 0;
}

static int rt5625_voip_chk_put(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	int i, upd;

	rt5625->headset = false;
	/* voip start-up if record is on-going; otherwise, cheat dapm */
	if ((rt5625->app_bmp & RT5625_REC_MASK) == 0 ||
		(rt5625->app_bmp & RT5625_PLY_MASK) == 0) {
		/* backup registers for voip routing */
		for(i = 0; i < RT5625_VOIP_BK_NUM; i++)
			rt5625_voip_back[i][1] =
				snd_soc_read(codec, rt5625_voip_back[i][0]);
		rt5625->vodsp_fun_bak = rt5625->vodsp_fun;
		/* cheat dapm */
		snd_soc_update_bits(codec, RT5625_VODSP_PDM_CTL,
			RT5625_RXDP_S_MASK | RT5625_REC_IIS_S_MASK | RT5625_RXDP_PWR,
			RT5625_RXDP_S_SRC1 | RT5625_REC_IIS_S_SRC2);
		return 0;
	}

	upd = (rt5625->app_bmp & ~RT5625_VOIP_MASK) |
		(ucontrol->value.integer.value[0] << RT5625_VOIP_BIT);
	if (rt5625->app_bmp != upd) {
		rt5625->app_bmp = upd;
		rt5625->app_bmp &= ~(RT5625_3G_MASK | RT5625_BT_MASK);
	}

	return 0;
}

static int rt5625_voip_put(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	int i;

	if (rt5625->app_bmp & RT5625_VOIP_MASK) {
		snd_soc_update_bits(codec, RT5625_ADC_REC_MIXER,
			RT5625_M_RM_R_MIC2 | RT5625_M_RM_L_PHO,
			RT5625_M_RM_R_MIC2 | RT5625_M_RM_L_PHO);
		/* Mic1 & Mic2 boost 0db */
		snd_soc_update_bits(codec, RT5625_MIC_CTRL,
			RT5625_MIC1_BST_MASK | RT5625_MIC2_BST_MASK,
			RT5625_MIC1_BST_BYPASS | RT5625_MIC2_BST_BYPASS);
		/* Capture volume gain 9db */
		snd_soc_update_bits(codec, RT5625_ADC_REC_GAIN,
			RT5625_G_ADCL_MASK | RT5625_G_ADCR_MASK, (0x11 << 8 ) | 0x11);
	} else {
		/* recover all changes by voip */
		for(i = 0; i < RT5625_VOIP_BK_NUM; i++)
			snd_soc_write(codec, rt5625_voip_back[i][0],
					rt5625_voip_back[i][1]);
		rt5625->vodsp_fun = rt5625->vodsp_fun_bak;
		snd_soc_update_bits(codec, RT5625_VODSP_PDM_CTL,
			RT5625_SRC1_PWR | RT5625_SRC2_PWR, 0);
		if ((rt5625->app_bmp & RT5625_3G_MASK) && rt5625->vodsp_fun == RT5625_AEC_EN) {
			snd_soc_update_bits(codec, RT5625_F_DAC_ADC_VDAC,
				RT5625_ADCR_F_MASK, RT5625_ADCR_F_PDM);
		} else if (rt5625->app_bmp & RT5625_BT_MASK) {
			snd_soc_update_bits(codec, RT5625_F_DAC_ADC_VDAC,
				RT5625_ADCR_F_MASK, RT5625_ADCR_F_VADC);
		} else {
			snd_soc_update_bits(codec, RT5625_F_DAC_ADC_VDAC,
				RT5625_ADCR_F_MASK, RT5625_ADCR_F_ADC);
		}
	}

	return 0;
}

static int rt5625_voip_get(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] =
		(rt5625->app_bmp & RT5625_VOIP_MASK) >> RT5625_VOIP_BIT;

	return 0;
}

static int rt5625_play_get(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] =
		(rt5625->app_bmp & RT5625_PLY_MASK) >> RT5625_PLY_BIT;

	return 0;
}

static int rt5625_play_put(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	int upd;

	upd = (rt5625->app_bmp & ~RT5625_PLY_MASK) |
		(ucontrol->value.integer.value[0] << RT5625_PLY_BIT);
	if (rt5625->app_bmp != upd)
		rt5625->app_bmp = upd;

	if (!(rt5625->app_bmp & RT5625_3G_MASK) ||
		((rt5625->app_bmp & RT5625_3G_MASK) && rt5625->headset)) {
		snd_soc_update_bits(codec, RT5625_ADC_REC_MIXER,
			RT5625_M_RM_L_PHO, RT5625_M_RM_L_PHO);
	}

	return 0;
}

static int rt5625_rec_get(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] =
		(rt5625->app_bmp & RT5625_REC_MASK) >> RT5625_REC_BIT;

	return 0;
}

static int rt5625_rec_put(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	int upd;

	upd = (rt5625->app_bmp & ~RT5625_REC_MASK) |
		(ucontrol->value.integer.value[0] << RT5625_REC_BIT);
	if (rt5625->app_bmp != upd)
		rt5625->app_bmp = upd;

	if (!(rt5625->app_bmp & RT5625_3G_MASK) ||
		((rt5625->app_bmp & RT5625_3G_MASK) && rt5625->headset)) {
		snd_soc_update_bits(codec, RT5625_ADC_REC_MIXER,
			RT5625_M_RM_L_PHO, RT5625_M_RM_L_PHO);
	}


	return 0;
}

static int rt5625_bt_get(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] =
		(rt5625->app_bmp & RT5625_BT_MASK) >> RT5625_BT_BIT;

	return 0;
}

static int rt5625_bt_put(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	int upd;

	if (!(rt5625->app_bmp & RT5625_REC_MASK)) {
		snd_soc_update_bits(codec, RT5625_ADC_REC_MIXER,
			RT5625_M_RM_L_MIC1 | RT5625_M_RM_L_MIC2 |
			RT5625_M_RM_R_MIC1 | RT5625_M_RM_R_MIC2,
			RT5625_M_RM_L_MIC1 | RT5625_M_RM_L_MIC2 |
			RT5625_M_RM_R_MIC1 | RT5625_M_RM_R_MIC2);
	}

	upd = (rt5625->app_bmp & ~RT5625_BT_MASK) |
		(ucontrol->value.integer.value[0] << RT5625_BT_BIT);
	if (rt5625->app_bmp != upd) {
		rt5625->app_bmp = upd;
		rt5625->app_bmp &= ~(RT5625_3G_MASK | RT5625_VOIP_MASK);
	}

	return 0;
}

static int rt5625_3g_get(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	
	ucontrol->value.integer.value[0] =
		(rt5625->app_bmp & RT5625_3G_MASK) >> RT5625_3G_BIT;

	return 0;
}

static int rt5625_3g_put(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	int upd;

	rt5625->headset = false;
	if (!(rt5625->app_bmp & RT5625_REC_MASK)) {
		snd_soc_update_bits(codec, RT5625_ADC_REC_MIXER,
			RT5625_M_RM_L_MIC1 | RT5625_M_RM_L_MIC2 | RT5625_M_RM_R_MIC2,
			RT5625_M_RM_L_MIC1 | RT5625_M_RM_L_MIC2 | RT5625_M_RM_R_MIC2);
	}

	upd = (rt5625->app_bmp & ~RT5625_3G_MASK) |
		(ucontrol->value.integer.value[0] << RT5625_3G_BIT);
	if (rt5625->app_bmp != upd) {
		rt5625->app_bmp = upd;
		rt5625->app_bmp &= ~(RT5625_BT_MASK | RT5625_VOIP_MASK);
	}

	return 0;
}

static int rt5625_hs_3g_put(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	int upd;

	rt5625->headset = true;
	if (!(rt5625->app_bmp & RT5625_REC_MASK)) {
		snd_soc_update_bits(codec, RT5625_ADC_REC_MIXER,
			RT5625_M_RM_L_MIC1 | RT5625_M_RM_L_MIC2 |
			RT5625_M_RM_R_MIC1 | RT5625_M_RM_R_MIC2,
			RT5625_M_RM_L_MIC1 | RT5625_M_RM_L_MIC2 |
			RT5625_M_RM_R_MIC1 | RT5625_M_RM_R_MIC2);
	}
	upd = (rt5625->app_bmp & ~RT5625_3G_MASK) |
		(ucontrol->value.integer.value[0] << RT5625_3G_BIT);
	if (rt5625->app_bmp != upd) {
		rt5625->app_bmp = upd;
		rt5625->app_bmp &= ~(RT5625_BT_MASK | RT5625_VOIP_MASK);
	}

	return 0;
}

static int rt5625_dump_dsp_get(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int i;
	u16 val;

	pr_info("\n[ RT5625 DSP Register ]\n");
	for (i = 0; i < RT5625_DSP_INIT_NUM; i++) {
		val = rt5625_dsp_read(codec, rt5625_dsp_init[i].index);
		if (val) pr_info("    0x%x: 0x%x\n",
			rt5625_dsp_init[i].index, val);
	}
	return 0;
}

static int rt5625_dac_active_get(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = rt5625->dac_active;
	return 0;
}

static int rt5625_dac_active_put(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	struct snd_soc_dapm_widget *w;
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	if(ucontrol->value.integer.value[0] == rt5625->dac_active)
		return 0;
	rt5625->dac_active = ucontrol->value.integer.value[0];

	/* playback is on-going; do nothing when turn off BT */
	if (rt5625->dac_active == 0 && rt5625->app_bmp & RT5625_PLY_MASK)
		return 0;

	list_for_each_entry(w, &dapm->card->widgets, list)
	{
		if (!w->sname || w->dapm != dapm)
			continue;
		if (strstr(w->sname, "Playback")) {
			pr_info("widget %s %s %s\n", w->name, w->sname,
				rt5625->dac_active ? "active" : "inactive");
			w->active = rt5625->dac_active;
		}
	}

	if (!(rt5625->dac_active))
		snd_soc_dapm_sync(dapm);

	return 0;
}

static int rt5625_adc_active_get(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = rt5625->adc_active;
	return 0;
}

static int rt5625_adc_active_put(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	struct snd_soc_dapm_widget *w;
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	if(ucontrol->value.integer.value[0] == rt5625->adc_active)
		return 0;
	rt5625->adc_active = ucontrol->value.integer.value[0];

	/* record is on-going; do nothing when turn off BT */
	if (rt5625->adc_active == 0 && rt5625->app_bmp & RT5625_REC_MASK)
		return 0;
	
	list_for_each_entry(w, &dapm->card->widgets, list)
	{
		if (!w->sname || w->dapm != dapm)
			continue;
		if (strstr(w->sname, "Capture")) {
			pr_info("widget %s %s %s\n", w->name, w->sname,
				rt5625->adc_active ? "active" : "inactive");
			w->active = rt5625->adc_active;
		}
	}

	if (!(rt5625->adc_active))
		snd_soc_dapm_sync(dapm);

	return 0;
}

static int rt5625_pll_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = rt5625->pll_sel;
	return 0;
}


static int rt5625_pll_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{ 
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	if(ucontrol->value.integer.value[0] == rt5625->pll_sel)
		return 0;
	rt5625->pll_sel = ucontrol->value.integer.value[0];

	switch(rt5625->pll_sel) {
	case RT5625_PLL_DIS:
		pr_info("%s(): Disable\n", __func__);
		snd_soc_update_bits(codec, RT5625_GEN_CTRL1,
					RT5625_SCLK_PLL1, 0);
		snd_soc_write(codec, RT5625_DAC_CLK_CTRL2, 0);
		break;

	case RT5625_PLL_112896_225792:
		pr_info("%s(): 11.2896>22.5792\n", __func__);
		snd_soc_write(codec, RT5625_GEN_CTRL2, 0x0000);
		snd_soc_write(codec, RT5625_PLL_CTRL, 0x06a0);
		snd_soc_update_bits(codec, RT5625_GEN_CTRL1,
			RT5625_SCLK_PLL1, RT5625_SCLK_PLL1);
		snd_soc_write(codec, RT5625_DAC_CLK_CTRL2, 0x0210);
		break;

	case RT5625_PLL_112896_24576:
		pr_info("%s(): 11.2896->24.576\n", __func__);
		snd_soc_write(codec, RT5625_GEN_CTRL2, 0x0000);
		snd_soc_write(codec, RT5625_PLL_CTRL, 0x922f);
		snd_soc_update_bits(codec, RT5625_GEN_CTRL1,
			RT5625_SCLK_PLL1, RT5625_SCLK_PLL1);
		snd_soc_write(codec, RT5625_DAC_CLK_CTRL2, 0x0210);
		break;

	default:
		break;
	}

	return 0;
}

static int rt5625_pll2_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = rt5625->pll2_sel;
	return 0;
}

static int rt5625_pll2_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	if(ucontrol->value.integer.value[0] == rt5625->pll2_sel)
		return 0;
	rt5625->pll2_sel = ucontrol->value.integer.value[0];

	if(rt5625->pll2_sel != RT5625_PLL_DIS) {
		snd_soc_update_bits(codec, RT5625_GEN_CTRL1,
			RT5625_VSCLK_MASK, RT5625_VSCLK_PLL2);
		snd_soc_write(codec, RT5625_PLL2_CTRL, RT5625_PLL2_EN);
		snd_soc_write(codec, RT5625_VDAC_CLK_CTRL1,
				RT5625_VBCLK_DIV1_4);
		snd_soc_update_bits(codec, RT5625_EXT_SDP_CTRL,
			RT5625_PCM_CS_MASK, RT5625_PCM_CS_VSCLK);
	}

	return 0;
}

static const char *rt5625_pll_sel[] = {"Disable", "11.2896->22.5792", "11.2896->24.576"};

static const SOC_ENUM_SINGLE_DECL(rt5625_pll_sel_enum, 0, 0, rt5625_pll_sel);

static const char *rt5625_pll2_sel[] = {"Disable", "Enable"};

static const SOC_ENUM_SINGLE_DECL(rt5625_pll2_sel_enum, 0, 0, rt5625_pll2_sel);
#endif

static const char *rt5625_AUXOUT_mode[] = {"Differential mode", "Single-ended mode"}; 
static const char *rt5625_Differential_Input_Control[] = {"Disable", "Enable"};  

static const struct soc_enum rt5625_differential_enum[] = {
SOC_ENUM_SINGLE(RT5625_OUTMIX_CTRL, 4, 2, rt5625_AUXOUT_mode),         /*0*/
SOC_ENUM_SINGLE(RT5625_PHONEIN_VOL, 13, 2, rt5625_Differential_Input_Control),/*1*/
SOC_ENUM_SINGLE(RT5625_MIC_VOL, 15, 2, rt5625_Differential_Input_Control),        /*2*/
SOC_ENUM_SINGLE(RT5625_MIC_VOL, 7, 2, rt5625_Differential_Input_Control),         /*3*/
};

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -4650, 150, 0);
static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -3525, 75, 0);
static const DECLARE_TLV_DB_SCALE(in_vol_tlv, -3450, 150, 0);
static const DECLARE_TLV_DB_SCALE(adc_vol_tlv, -1650, 150, 0);
static const DECLARE_TLV_DB_SCALE(dmic_bst_tlv, 0, 600, 0);
/* {0, +20, +30, +40} dB */
static unsigned int mic_bst_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 3, TLV_DB_SCALE_ITEM(2000, 1000, 0),
};

#ifdef RT5625_REG_RW
#define REGVAL_MAX 0xffff
static unsigned int regctl_addr = 0x3e;
static int rt5625_regctl_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = REGVAL_MAX;
	return 0;
}

static int rt5625_regctl_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = regctl_addr;
	ucontrol->value.integer.value[1] = snd_soc_read(codec, regctl_addr);
	return 0;
}

static int rt5625_regctl_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	regctl_addr = ucontrol->value.integer.value[0];
	if(ucontrol->value.integer.value[1] <= REGVAL_MAX)
		snd_soc_write(codec, regctl_addr, ucontrol->value.integer.value[1]);
	return 0;
}
#endif

static const struct snd_kcontrol_new rt5625_snd_controls[] = {
	SOC_DOUBLE_TLV("SPKOUT Playback Volume", RT5625_SPK_OUT_VOL,
		RT5625_L_VOL_SFT, RT5625_R_VOL_SFT, 31, 1, out_vol_tlv),
	SOC_DOUBLE("SPKOUT Playback Switch", RT5625_SPK_OUT_VOL,
			RT5625_L_MUTE_SFT, RT5625_R_MUTE_SFT, 1, 1),
	SOC_ENUM("SPK Amp Type", rt5625_spk_out_enum),
	SOC_ENUM("Left SPK Source", rt5625_spkl_src_enum),
	SOC_ENUM("SPK Amp Ratio", rt5625_spkamp_ratio_enum),
	//SOC_DOUBLE_TLV("Headphone Playback Volume", RT5625_HP_OUT_VOL,
	//	RT5625_L_VOL_SFT, RT5625_R_VOL_SFT, 31, 1, out_vol_tlv),
	//SOC_DOUBLE("Headphone Playback Switch", RT5625_HP_OUT_VOL,
	//		RT5625_L_MUTE_SFT, RT5625_R_MUTE_SFT, 1, 1),
	//SOC_ENUM("AUXOUT Mode Control", rt5625_auxout_mode_enum),
	SOC_DOUBLE_TLV("AUXOUT Playback Volume", RT5625_AUX_OUT_VOL,
		RT5625_L_VOL_SFT, RT5625_R_VOL_SFT, 31, 1, out_vol_tlv),
	SOC_DOUBLE("AUXOUT Playback Switch", RT5625_AUX_OUT_VOL,
			RT5625_L_MUTE_SFT, RT5625_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("PCM Playback Volume", RT5625_DAC_VOL,
		RT5625_L_VOL_SFT, RT5625_R_VOL_SFT, 63, 1, dmic_bst_tlv),
	//SOC_ENUM("Phone Mode Control", rt5625_phone_mode_enum),
	SOC_SINGLE_TLV("Phone Playback Volume", RT5625_PHONEIN_VOL,
		RT5625_L_VOL_SFT, 31, 1, in_vol_tlv),
	//SOC_ENUM("MIC1 Mode Control", rt5625_mic1_mode_enum),
	SOC_SINGLE_TLV("MIC1 Boost", RT5625_MIC_CTRL,
		RT5625_MIC1_BST_SFT, 3, 0, mic_bst_tlv),
	SOC_SINGLE_TLV("Mic1 Playback Volume", RT5625_MIC_VOL,
		RT5625_L_VOL_SFT, 31, 1, in_vol_tlv),
	//SOC_ENUM("MIC2 Mode Control", rt5625_mic2_mode_enum),
	SOC_SINGLE_TLV("MIC2 Boost", RT5625_MIC_CTRL,
		RT5625_MIC2_BST_SFT, 3, 0, mic_bst_tlv),
	SOC_SINGLE_TLV("Mic2 Playback Volume", RT5625_MIC_VOL,
		RT5625_R_VOL_SFT, 31, 1, in_vol_tlv),
	SOC_SINGLE_TLV("Dmic Boost", RT5625_DMIC_CTRL,
		RT5625_DIG_BST_SFT, 7, 0, dmic_bst_tlv),
	SOC_DOUBLE_TLV("LineIn Playback Volume", RT5625_LINE_IN_VOL,
		RT5625_L_VOL_SFT, RT5625_R_VOL_SFT, 31, 1, in_vol_tlv),
	SOC_DOUBLE_TLV("PCM Capture Volume", RT5625_ADC_REC_GAIN,
		RT5625_L_VOL_SFT, RT5625_R_VOL_SFT, 31, 0, adc_vol_tlv),
	//SOC_DOUBLE_TLV("ADC Record Gain", RT5625_ADC_REC_GAIN,
	//	RT5625_L_VOL_SFT, RT5625_R_VOL_SFT, 31, 0, adc_vol_tlv),
	/* This item does'nt affect path connected; only for clock choosen */
	SOC_ENUM_EXT("ADCR fun select Control", adcr_fun_sel_enum,
		snd_soc_get_enum_double, rt5625_adcr_fun_sel_put),
	SOC_ENUM_EXT("ADCL fun select Control", adcl_fun_sel_enum,
		snd_soc_get_enum_double, rt5625_adcl_fun_sel_put),

	/* Voice DSP */
	SOC_ENUM_EXT("VoDSP AEC", rt5625_aec_fun_enum,
		rt5625_aec_get, rt5625_aec_put),
	SOC_ENUM("VoDSP LRCK Control",  rt5625_dsp_lrck_enum),
	SOC_ENUM("VoDSP BP Pin Control",  rt5625_bp_ctrl_enum),
	SOC_ENUM("VoDSP Power Down Pin Control",  rt5625_pd_ctrl_enum),
	SOC_ENUM("VoDSP Reset Pin Control",  rt5625_rst_ctrl_enum),

#ifdef RT5625_F_SMT_PHO
	SOC_SINGLE_EXT("VoDSP Dump", 0, 0, 1, 0, rt5625_dump_dsp_get, NULL),
	SOC_SINGLE_EXT("DAC Switch", 0, 0, 1, 0, rt5625_dac_active_get, rt5625_dac_active_put),
	SOC_SINGLE_EXT("ADC Switch", 0, 0, 1, 0, rt5625_adc_active_get, rt5625_adc_active_put),
	SOC_ENUM_EXT("PLL Switch", rt5625_pll_sel_enum, rt5625_pll_get, rt5625_pll_put),
	SOC_ENUM_EXT("PLL2 Switch", rt5625_pll2_sel_enum, rt5625_pll2_get, rt5625_pll2_put),
	SOC_SINGLE_EXT("VoIP Check", 0, 0, 1, 0, rt5625_voip_get, rt5625_voip_chk_put),
	SOC_SINGLE_EXT("VoIP Switch", 0, 0, 1, 0, rt5625_voip_get, rt5625_voip_put),
	SOC_SINGLE_EXT("Capture VoIP Check", 0, 0, 1, 0, rt5625_voip_get, rt5625_cap_voip_chk_put),
	SOC_SINGLE_EXT("Capture VoIP Switch", 0, 0, 1, 0, rt5625_voip_get, rt5625_cap_voip_put),
	SOC_SINGLE_EXT("Headset VoIP Check", 0, 0, 1, 0, rt5625_voip_get, rt5625_hs_voip_chk_put),
	SOC_SINGLE_EXT("Headset VoIP Switch", 0, 0, 1, 0, rt5625_voip_get, rt5625_hs_voip_put),
	SOC_SINGLE_EXT("Playback Switch", 0, 0, 1, 0, rt5625_play_get, rt5625_play_put),
	SOC_SINGLE_EXT("Record Switch", 0, 0, 1, 0, rt5625_rec_get, rt5625_rec_put),
	SOC_SINGLE_EXT("BT Switch", 0, 0, 1, 0, rt5625_bt_get, rt5625_bt_put),
	SOC_SINGLE_EXT("3G Switch", 0, 0, 1, 0, rt5625_3g_get, rt5625_3g_put),
	SOC_SINGLE_EXT("Headset 3G Switch", 0, 0, 1, 0, rt5625_3g_get, rt5625_hs_3g_put),
	SOC_SINGLE_EXT("APP disp", 0, 0, 1, 0, rt5625_app_get, NULL),
#endif
	//SOC_DOUBLE_TLV("SPKOUT Playback Volume", RT5625_SPK_OUT_VOL,
	//	RT5625_L_VOL_SFT, RT5625_R_VOL_SFT, 31, 1, out_vol_tlv),
	//SOC_DOUBLE("SPKOUT Playback Switch", RT5625_SPK_OUT_VOL,
	//		RT5625_L_MUTE_SFT, RT5625_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("HPOUT Playback Volume", RT5625_HP_OUT_VOL,
		RT5625_L_VOL_SFT, RT5625_R_VOL_SFT, 31, 1, out_vol_tlv),
	SOC_DOUBLE("HPOUT Playback Switch", RT5625_HP_OUT_VOL,
			RT5625_L_MUTE_SFT, RT5625_R_MUTE_SFT, 1, 1),
	SOC_ENUM("AUXOUT mode switch", rt5625_differential_enum[0]),
	SOC_ENUM("Phone Differential Input Control", rt5625_differential_enum[1]),
	SOC_ENUM("MIC1 Differential Input Control", rt5625_differential_enum[2]),
	SOC_ENUM("MIC2 Differential Input Control", rt5625_differential_enum[3]),

#ifdef RT5625_REG_RW
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Register Control",
		.info = rt5625_regctl_info,
		.get = rt5625_regctl_get,
		.put = rt5625_regctl_put,
	},
#endif
};


 /*Left ADC Rec mixer*/
static const struct snd_kcontrol_new rt5625_adcl_rec_mixer[] = {
	SOC_DAPM_SINGLE("Mic1 Capture Switch", RT5625_ADC_REC_MIXER,
				RT5625_M_RM_L_MIC1_SFT, 1, 1),
	SOC_DAPM_SINGLE("Mic2 Capture Switch", RT5625_ADC_REC_MIXER,
				RT5625_M_RM_L_MIC2_SFT, 1, 1),
	SOC_DAPM_SINGLE("LineIn Capture Switch", RT5625_ADC_REC_MIXER,
				RT5625_M_RM_L_LINE_SFT, 1, 1),
	SOC_DAPM_SINGLE("Phone Capture Switch", RT5625_ADC_REC_MIXER,
				RT5625_M_RM_L_PHO_SFT, 1, 1),
	SOC_DAPM_SINGLE("HP Mixer Capture Switch", RT5625_ADC_REC_MIXER,
				RT5625_M_RM_L_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("SPK Mixer Capture Switch", RT5625_ADC_REC_MIXER,
				RT5625_M_RM_L_SM_SFT, 1, 1),
	SOC_DAPM_SINGLE("MoNo Mixer Capture Switch", RT5625_ADC_REC_MIXER,
				RT5625_M_RM_L_MM_SFT, 1, 1),
};

/*Right ADC Rec mixer*/
static const struct snd_kcontrol_new rt5625_adcr_rec_mixer[] = {
	SOC_DAPM_SINGLE("Mic1 Capture Switch", RT5625_ADC_REC_MIXER,
				RT5625_M_RM_R_MIC1_SFT, 1, 1),
	SOC_DAPM_SINGLE("Mic2 Capture Switch", RT5625_ADC_REC_MIXER,
				RT5625_M_RM_R_MIC2_SFT, 1, 1),
	SOC_DAPM_SINGLE("LineIn Capture Switch", RT5625_ADC_REC_MIXER,
				RT5625_M_RM_R_LINE_SFT, 1, 1),
	SOC_DAPM_SINGLE("Phone Capture Switch", RT5625_ADC_REC_MIXER,
				RT5625_M_RM_R_PHO_SFT, 1, 1),
	SOC_DAPM_SINGLE("HP Mixer Capture Switch", RT5625_ADC_REC_MIXER,
				RT5625_M_RM_R_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("SPK Mixer Capture Switch", RT5625_ADC_REC_MIXER,
				RT5625_M_RM_R_SM_SFT, 1, 1),
	SOC_DAPM_SINGLE("MoNo Mixer Capture Switch", RT5625_ADC_REC_MIXER,
				RT5625_M_RM_R_MM_SFT, 1, 1),
};

/* HP Mixer for mono input */
static const struct snd_kcontrol_new rt5625_hp_mixer[] = {
	SOC_DAPM_SINGLE("LineIn Playback Switch", RT5625_LINE_IN_VOL,
				RT5625_M_LI_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("Phone Playback Switch", RT5625_PHONEIN_VOL,
				RT5625_M_PHO_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("Mic1 Playback Switch", RT5625_DAC_MIC_CTRL,
				RT5625_M_MIC1_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("Mic2 Playback Switch", RT5625_DAC_MIC_CTRL,
				RT5625_M_MIC2_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("Voice DAC Playback Switch", RT5625_VDAC_OUT_VOL,
				RT5625_M_VDAC_HM_SFT, 1, 1),
};

/* Left HP Mixer */
static const struct snd_kcontrol_new rt5625_hpl_mixer[] = {
	SOC_DAPM_SINGLE("ADC Playback Switch", RT5625_ADC_REC_GAIN,
				RT5625_M_ADCL_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC Playback Switch", RT5625_DAC_MIC_CTRL,
				RT5625_M_DACL_HM_SFT, 1, 1),
};

/* Right HP Mixer */
static const struct snd_kcontrol_new rt5625_hpr_mixer[] = {
	SOC_DAPM_SINGLE("ADC Playback Switch", RT5625_ADC_REC_GAIN,
				RT5625_M_ADCR_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC Playback Switch", RT5625_DAC_MIC_CTRL,
				RT5625_M_DACR_HM_SFT, 1, 1),
};

/* Mono Mixer */
static const struct snd_kcontrol_new rt5625_mono_mixer[] = {
	SOC_DAPM_SINGLE("ADCL Playback Switch", RT5625_ADC_REC_GAIN,
				RT5625_M_ADCL_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADCR Playback Switch", RT5625_ADC_REC_GAIN,
				RT5625_M_ADCR_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("Line Mixer Playback Switch", RT5625_LINE_IN_VOL,
				RT5625_M_LI_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("Mic1 Playback Switch", RT5625_DAC_MIC_CTRL,
				RT5625_M_MIC1_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("Mic2 Playback Switch", RT5625_DAC_MIC_CTRL,
				RT5625_M_MIC2_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC Mixer Playback Switch", RT5625_DAC_MIC_CTRL,
				RT5625_M_DAC_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("Voice DAC Playback Switch", RT5625_VDAC_OUT_VOL,
				RT5625_M_VDAC_MM_SFT, 1, 1),
};

/* Speaker Mixer */
static const struct snd_kcontrol_new rt5625_spk_mixer[] = {
	SOC_DAPM_SINGLE("Line Mixer Playback Switch", RT5625_LINE_IN_VOL,
				RT5625_M_LI_SM_SFT, 1, 1),
	SOC_DAPM_SINGLE("Phone Playback Switch", RT5625_PHONEIN_VOL,
				RT5625_M_PHO_SM_SFT, 1, 1),
	SOC_DAPM_SINGLE("Mic1 Playback Switch", RT5625_DAC_MIC_CTRL,
				RT5625_M_MIC1_SM_SFT, 1, 1),
	SOC_DAPM_SINGLE("Mic2 Playback Switch", RT5625_DAC_MIC_CTRL,
				RT5625_M_MIC2_SM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC Mixer Playback Switch", RT5625_DAC_MIC_CTRL,
				RT5625_M_DAC_SM_SFT, 1, 1),
	SOC_DAPM_SINGLE("Voice DAC Playback Switch", RT5625_VDAC_OUT_VOL,
				RT5625_M_VDAC_SM_SFT, 1, 1),
};

static int rt5625_dac_func_event(struct snd_soc_dapm_widget *w, struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_PRE_REG:
		snd_soc_update_bits(codec, RT5625_PD_CTRL,
			RT5625_PWR_PR1, RT5625_PWR_PR1);
		break;

	case SND_SOC_DAPM_POST_REG:
		snd_soc_update_bits(codec, RT5625_PD_CTRL,
			RT5625_PWR_PR1, 0);
		break;

	default:
		return 0;
	}	

	return 0;
}

static int rt5625_hpmix_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5625_PWR_ADD2,
			RT5625_P_HM_L | RT5625_P_HM_R, 0);
		break;

	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5625_PWR_ADD2,
			RT5625_P_HM_L | RT5625_P_HM_R,
			RT5625_P_HM_L | RT5625_P_HM_R);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5625_vodsp_event(struct snd_soc_dapm_widget *w, struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMD:
		//pr_info("%s(): PMD\n", __func__);
		snd_soc_update_bits(codec, RT5625_VODSP_CTL,
			RT5625_DSP_PD_MASK, RT5625_DSP_PD_EN);
		snd_soc_update_bits(codec, RT5625_PWR_ADD3,
			RT5625_P_DSP_IF | RT5625_P_DSP_I2C, 0);
		snd_soc_update_bits(codec, RT5625_LDO_CTRL,
			RT5625_LDO_MASK, RT5625_LDO_DIS);
		break;

	case SND_SOC_DAPM_POST_PMU:
		//pr_info("%s(): PMU\n", __func__);
		if(rt5625->vodsp_fun == RT5625_AEC_EN)
			rt5625_init_vodsp_aec(codec);
		//pr_info("[DSP poweron] 0x%04x: 0x%04x\n", 0x230C, rt5625_dsp_read(codec, 0x230C));
		break;

	default:
		return 0;
	}	

	return 0;
}


static void hp_depop_mode2(struct snd_soc_codec *codec)
{
	snd_soc_update_bits(codec, RT5625_PWR_ADD1,
		RT5625_P_SG_EN, RT5625_P_SG_EN);
	snd_soc_update_bits(codec, RT5625_PWR_ADD3,
		RT5625_P_HPL_VOL | RT5625_P_HPR_VOL,
		RT5625_P_HPL_VOL | RT5625_P_HPR_VOL);
	snd_soc_write(codec, RT5625_MISC_CTRL, RT5625_HP_DEPOP_M2);
	schedule_timeout_uninterruptible(msecs_to_jiffies(500));
	snd_soc_update_bits(codec, RT5625_PWR_ADD1,
		RT5625_P_HPO_AMP | RT5625_P_HPO_ENH,
		RT5625_P_HPO_AMP | RT5625_P_HPO_ENH);
}

/* enable depop function for mute/unmute */
static void hp_mute_unmute_depop(struct snd_soc_codec *codec,int mute)
{
 	if(mute) {
		snd_soc_update_bits(codec, RT5625_PWR_ADD1,
			RT5625_P_SG_EN, RT5625_P_SG_EN);
		snd_soc_write(codec, RT5625_MISC_CTRL, RT5625_MUM_DEPOP |
			RT5625_HPR_MUM_DEPOP | RT5625_HPL_MUM_DEPOP);
		snd_soc_update_bits(codec, RT5625_HP_OUT_VOL,
			RT5625_L_MUTE | RT5625_R_MUTE,
			RT5625_L_MUTE | RT5625_R_MUTE);
		mdelay(50);
		snd_soc_update_bits(codec, RT5625_PWR_ADD1, RT5625_P_SG_EN, 0);
	} else {
		snd_soc_update_bits(codec, RT5625_PWR_ADD1,
			RT5625_P_SG_EN, RT5625_P_SG_EN);
		snd_soc_write(codec, RT5625_MISC_CTRL, RT5625_MUM_DEPOP |
			RT5625_HPR_MUM_DEPOP | RT5625_HPL_MUM_DEPOP);
		snd_soc_update_bits(codec,RT5625_HP_OUT_VOL,
			RT5625_L_MUTE | RT5625_R_MUTE, 0);
		mdelay(50);
	}
}

static int rt5625_hp_event(struct snd_soc_dapm_widget *w, struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMD:
		hp_mute_unmute_depop(codec,1);
		snd_soc_update_bits(codec, RT5625_PWR_ADD1,
			RT5625_P_HPO_AMP | RT5625_P_HPO_ENH, 0);
		snd_soc_update_bits(codec, RT5625_PWR_ADD3,
			RT5625_P_HPL_VOL | RT5625_P_HPR_VOL, 0);
		break;

	case SND_SOC_DAPM_POST_PMU:
		hp_depop_mode2(codec);
		hp_mute_unmute_depop(codec,0);
		break;

	default:
		return 0;
	}	

	return 0;
}

/* DAC function select MUX */
static const char *dac_fun_sel[] = {
	"Stereo DAC", "SRC2 Out", "TxDP", "TxDC"};

static const struct soc_enum dac_fun_sel_enum =
	SOC_ENUM_SINGLE(RT5625_F_DAC_ADC_VDAC, RT5625_DAC_F_SFT,
				ARRAY_SIZE(dac_fun_sel), dac_fun_sel);

static const struct snd_kcontrol_new dac_fun_sel_mux =
	SOC_DAPM_ENUM("DAC Function Select Mux", dac_fun_sel_enum);

/* Voice DAC source select MUX */
static const char *vdac_src_sel[] = {
	"Voice PCM", "SRC2 Out", "TxDP", "TxDC"};

static const struct soc_enum vdac_src_sel_enum =
	SOC_ENUM_SINGLE(RT5625_F_DAC_ADC_VDAC, RT5625_VDAC_S_SFT,
				ARRAY_SIZE(vdac_src_sel), vdac_src_sel);

static const struct snd_kcontrol_new vdac_src_sel_mux =
	SOC_DAPM_ENUM("Voice DAC Source Mux", vdac_src_sel_enum);

/* SRC1 power switch */
static const struct snd_kcontrol_new src1_pwr_sw_control =
	SOC_DAPM_SINGLE("Switch", RT5625_VODSP_PDM_CTL,
				RT5625_SRC1_PWR_SFT, 1, 0);

/* SRC2 power switch */
static const struct snd_kcontrol_new src2_pwr_sw_control =
	SOC_DAPM_SINGLE("Switch", RT5625_VODSP_PDM_CTL,
				RT5625_SRC2_PWR_SFT, 1, 0);

/* SRC2 source select MUX */
static const char *src2_src_sel[] = {"TxDP", "TxDC"};

static const struct soc_enum src2_src_sel_enum =
	SOC_ENUM_SINGLE(RT5625_VODSP_PDM_CTL, RT5625_SRC2_S_SFT,
			ARRAY_SIZE(src2_src_sel), src2_src_sel);

static const struct snd_kcontrol_new src2_src_sel_mux =
	SOC_DAPM_ENUM("SRC2 Source Mux", src2_src_sel_enum);

/* VoDSP RxDP power switch */
static const struct snd_kcontrol_new rxdp_pwr_sw_control =
	SOC_DAPM_SINGLE("Switch", RT5625_VODSP_PDM_CTL,
				RT5625_RXDP_PWR_SFT, 1, 0);
/* VoDSP RxDC power switch */
static const struct snd_kcontrol_new rxdc_pwr_sw_control =
	SOC_DAPM_SINGLE("Switch", RT5625_VODSP_PDM_CTL,
				RT5625_RXDC_PWR_SFT, 1, 0);

/* VoDSP RxDP source select MUX */
static const char *rxdp_src_sel[] = {"SRC1 Output", "ADCL to VoDSP",
			"Voice to Stereo", "ADCR to VoDSP"};

static const struct soc_enum rxdp_src_sel_enum =
	SOC_ENUM_SINGLE(RT5625_VODSP_PDM_CTL, RT5625_RXDP_S_SFT,
			ARRAY_SIZE(rxdp_src_sel), rxdp_src_sel);

static const struct snd_kcontrol_new rxdp_src_sel_mux =
	SOC_DAPM_ENUM("RxDP Source Mux", rxdp_src_sel_enum);

/* PCM source select MUX */
static const char *pcm_src_sel[] = {"ADCR", "TxDP"};

static const struct soc_enum pcm_src_sel_enum =
	SOC_ENUM_SINGLE(RT5625_VODSP_PDM_CTL, RT5625_PCM_S_SFT,
			ARRAY_SIZE(pcm_src_sel), pcm_src_sel);

static const struct snd_kcontrol_new pcm_src_sel_mux =
	SOC_DAPM_ENUM("PCM Source Mux", pcm_src_sel_enum);

/* Main stereo record I2S source select MUX */
static const char *rec_iis_src_sel[] = {"ADC", "Voice to Stereo", "SRC2 Output"};

static const struct soc_enum rec_iis_src_enum =
	SOC_ENUM_SINGLE(RT5625_VODSP_PDM_CTL, RT5625_REC_IIS_S_SFT,
			ARRAY_SIZE(rec_iis_src_sel), rec_iis_src_sel);

static const struct snd_kcontrol_new rec_iis_src_mux =
	SOC_DAPM_ENUM("REC I2S Source Mux", rec_iis_src_enum);

/* SPK volume input select MUX */
static const char *spkvol_input_sel[] = {"VMID", "HP Mixer", "SPK Mixer", "Mono Mixer"};

static const struct soc_enum spkvol_input_enum =
	SOC_ENUM_SINGLE(RT5625_OUTMIX_CTRL, RT5625_SPKVOL_S_SFT,
			ARRAY_SIZE(spkvol_input_sel), spkvol_input_sel);

static const struct snd_kcontrol_new spkvol_input_mux =
	SOC_DAPM_ENUM("SPK Vol Input Mux", spkvol_input_enum);

/* HP volume input select MUX */
static const char *hpvol_input_sel[] = {"VMID", "HP Mixer"};

static const struct soc_enum hplvol_input_enum =
	SOC_ENUM_SINGLE(RT5625_OUTMIX_CTRL, RT5625_HPVOL_L_S_SFT,
			ARRAY_SIZE(hpvol_input_sel), hpvol_input_sel);

static const struct snd_kcontrol_new hplvol_input_mux =
	SOC_DAPM_ENUM("HPL Vol Input Mux", hplvol_input_enum);

static const struct soc_enum hprvol_input_enum =
	SOC_ENUM_SINGLE(RT5625_OUTMIX_CTRL, RT5625_HPVOL_R_S_SFT,
			ARRAY_SIZE(hpvol_input_sel), hpvol_input_sel);

static const struct snd_kcontrol_new hprvol_input_mux =
	SOC_DAPM_ENUM("HPR Vol Input Mux", hprvol_input_enum);

/* AUX volume input select MUX */
static const struct soc_enum auxvol_input_enum =
	SOC_ENUM_SINGLE(RT5625_OUTMIX_CTRL, RT5625_AUXVOL_S_SFT,
			ARRAY_SIZE(spkvol_input_sel), spkvol_input_sel);

static const struct snd_kcontrol_new auxvol_input_mux =
	SOC_DAPM_ENUM("AUX Vol Input Mux", auxvol_input_enum);

static const struct snd_soc_dapm_widget rt5625_dapm_widgets[] = {
	/* supply */
	SND_SOC_DAPM_SUPPLY("IIS Interface", RT5625_PWR_ADD1,
				RT5625_P_I2S_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL1", RT5625_PWR_ADD2,
				RT5625_P_PLL1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL2", RT5625_PWR_ADD2,
				RT5625_P_PLL2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_VMID("VMID"),
	SND_SOC_DAPM_SUPPLY("DAC Ref", RT5625_PWR_ADD1,
				RT5625_P_DAC_REF_BIT, 0, NULL, 0),
	/* microphone bias */
	SND_SOC_DAPM_MICBIAS("Mic1 Bias", RT5625_PWR_ADD1,
					RT5625_P_MB1_BIT, 0),
	SND_SOC_DAPM_MICBIAS("Mic2 Bias", RT5625_PWR_ADD1,
					RT5625_P_MB2_BIT, 0),

	/* Input */
	SND_SOC_DAPM_INPUT("Left LineIn"),
	SND_SOC_DAPM_INPUT("Right LineIn"),
	SND_SOC_DAPM_INPUT("Phone"),
	SND_SOC_DAPM_INPUT("Mic1"),
	SND_SOC_DAPM_INPUT("Mic2"),

	SND_SOC_DAPM_PGA("Mic1 Boost", RT5625_PWR_ADD3,
			RT5625_P_MIC1_BST_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Mic2 Boost", RT5625_PWR_ADD3,
			RT5625_P_MIC2_BST_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Phone Rec Mixer", RT5625_PWR_ADD3,
			RT5625_P_PH_ADMIX_BIT, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("Left Rec Mixer", RT5625_PWR_ADD2,
		RT5625_P_ADCL_RM_BIT, 0, rt5625_adcl_rec_mixer,
		ARRAY_SIZE(rt5625_adcl_rec_mixer)),
	SND_SOC_DAPM_MIXER("Right Rec Mixer", RT5625_PWR_ADD2,
		RT5625_P_ADCR_RM_BIT, 0, rt5625_adcr_rec_mixer,
		ARRAY_SIZE(rt5625_adcr_rec_mixer)),

	SND_SOC_DAPM_ADC("Left ADC", NULL, RT5625_PWR_ADD2,
					RT5625_P_ADCL_BIT, 0),
	SND_SOC_DAPM_ADC("Right ADC", NULL, RT5625_PWR_ADD2,
					RT5625_P_ADCR_BIT, 0),
	SND_SOC_DAPM_MUX("PCM src select Mux", SND_SOC_NOPM, 0, 0,
				&pcm_src_sel_mux),
	SND_SOC_DAPM_MUX("IIS src select Mux", SND_SOC_NOPM, 0, 0,
				&rec_iis_src_mux),

	/* Input Stream Audio Interface */
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 HiFi Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "AIF2 Voice Capture", 0, SND_SOC_NOPM, 0, 0),

	/* Voice DSP */
	SND_SOC_DAPM_MUX("SRC2 src select Mux", SND_SOC_NOPM, 0, 0,
				&src2_src_sel_mux),
	SND_SOC_DAPM_SWITCH("SRC1 Enable", SND_SOC_NOPM, 0, 0,
				&src1_pwr_sw_control),
	SND_SOC_DAPM_SWITCH("SRC2 Enable", SND_SOC_NOPM, 0, 0,
				&src2_pwr_sw_control),
	SND_SOC_DAPM_SWITCH("RxDP Enable", SND_SOC_NOPM, 0, 0,
				&rxdp_pwr_sw_control),
	SND_SOC_DAPM_SWITCH("RxDC Enable", SND_SOC_NOPM, 0, 0,
				&rxdc_pwr_sw_control),

	SND_SOC_DAPM_MUX("RxDP src select Mux", SND_SOC_NOPM, 0, 0,
				&rxdp_src_sel_mux),

	SND_SOC_DAPM_PGA("TxDP", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("TxDC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PDM", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("RxDP", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("RxDC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA_E("Voice DSP", SND_SOC_NOPM,
		0, 0, NULL, 0, rt5625_vodsp_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),

	/* Output */
	/* Output Stream Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 HiFi Playback",
				0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2RX", "AIF2 Voice Playback",
				0, SND_SOC_NOPM, 0, 0),

	/* DAC function select Mux */
	SND_SOC_DAPM_MUX_E("DAC fun Mux", SND_SOC_NOPM, 0, 0,
		&dac_fun_sel_mux, rt5625_dac_func_event,
		SND_SOC_DAPM_PRE_REG | SND_SOC_DAPM_POST_REG),
	/* VDAC source select Mux */
	SND_SOC_DAPM_MUX_E("VDAC src Mux", SND_SOC_NOPM, 0, 0,
		&vdac_src_sel_mux, rt5625_dac_func_event,
		SND_SOC_DAPM_PRE_REG | SND_SOC_DAPM_POST_REG),

	SND_SOC_DAPM_DAC("Left DAC", NULL, RT5625_PWR_ADD2,
					RT5625_P_DACL_BIT, 0),
	SND_SOC_DAPM_DAC("Right DAC", NULL, RT5625_PWR_ADD2,
					RT5625_P_DACR_BIT, 0),
	SND_SOC_DAPM_DAC("Voice DAC", NULL, RT5625_PWR_ADD2,
					RT5625_P_VDAC_BIT, 0),

	SND_SOC_DAPM_PGA("Mic1 Volume", RT5625_PWR_ADD3,
				RT5625_P_MIC1_VOL_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Mic2 Volume", RT5625_PWR_ADD3,
				RT5625_P_MIC2_VOL_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left LineIn Volume", RT5625_PWR_ADD3,
				RT5625_P_LV_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right LineIn Volume", RT5625_PWR_ADD3,
				RT5625_P_LV_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Phone Volume", RT5625_PWR_ADD3,
				RT5625_P_PH_VOL_BIT, 0, NULL, 0),

	SND_SOC_DAPM_PGA("Left DAC To Mixer", RT5625_PWR_ADD1,
				RT5625_P_DACL_MIX_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right DAC To Mixer", RT5625_PWR_ADD1,
				RT5625_P_DACR_MIX_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Voice DAC To Mixer", RT5625_PWR_ADD1,
				RT5625_P_VDAC_MIX_BIT, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("SPK Mixer", RT5625_PWR_ADD2,
		RT5625_P_SM_BIT, 0, rt5625_spk_mixer,
		ARRAY_SIZE(rt5625_spk_mixer)),	
	SND_SOC_DAPM_MIXER("Mono HP Mixer", SND_SOC_NOPM, 0, 0,
		rt5625_hp_mixer, ARRAY_SIZE(rt5625_hp_mixer)),
	SND_SOC_DAPM_MIXER("Left HP Mixer", SND_SOC_NOPM, 0, 0,
		rt5625_hpl_mixer, ARRAY_SIZE(rt5625_hpl_mixer)),
	SND_SOC_DAPM_MIXER("Right HP Mixer", SND_SOC_NOPM, 0, 0,
		rt5625_hpr_mixer, ARRAY_SIZE(rt5625_hpr_mixer)),
	SND_SOC_DAPM_MIXER("Mono Mixer", RT5625_PWR_ADD2,
		RT5625_P_MM_BIT, 0, rt5625_mono_mixer,
		ARRAY_SIZE(rt5625_mono_mixer)),

	SND_SOC_DAPM_MIXER_E("HP Mixer", SND_SOC_NOPM, 0, 0, NULL, 0,
		rt5625_hpmix_event, SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_MIXER("DAC Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Line Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("SPK Vol Input Mux", SND_SOC_NOPM,
			0, 0, &spkvol_input_mux),
	SND_SOC_DAPM_SUPPLY("SPKL Vol", RT5625_PWR_ADD3,
			RT5625_P_SPKL_VOL_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SPKR Vol", RT5625_PWR_ADD3,
			RT5625_P_SPKR_VOL_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MUX("HPL Vol Input Mux", RT5625_PWR_ADD3,
			RT5625_P_HPL_VOL_BIT, 0, &hplvol_input_mux),
	SND_SOC_DAPM_MUX("HPR Vol Input Mux", RT5625_PWR_ADD3,
			RT5625_P_HPR_VOL_BIT, 0, &hprvol_input_mux),
	SND_SOC_DAPM_MUX("AUX Vol Input Mux", RT5625_PWR_ADD3,
			RT5625_P_AUX_VOL_BIT, 0, &auxvol_input_mux),

	SND_SOC_DAPM_SUPPLY("SPK Amp", RT5625_PWR_ADD1,
				RT5625_P_SPK_AMP_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_E("HP Amp", SND_SOC_NOPM, 0, 0, NULL, 0,
		rt5625_hp_event, SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
				
	SND_SOC_DAPM_OUTPUT("SPKL"),
	SND_SOC_DAPM_OUTPUT("SPKR"),
	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),
	SND_SOC_DAPM_OUTPUT("AUX"),
};

static const struct snd_soc_dapm_route rt5625_dapm_routes[] = {
	{"DAC Ref", NULL, "IIS Interface"},
	{"DAC Ref", NULL, "PLL1"},
	{"DAC Ref", NULL, "PLL2"},

	/* Input */
	{"Phone Rec Mixer", NULL, "Phone"},
	{"Mic1 Boost", NULL, "Mic1"},
	{"Mic2 Boost", NULL, "Mic2"},

	{"Left Rec Mixer", "LineIn Capture Switch", "Left LineIn"},
	{"Left Rec Mixer", "Phone Capture Switch", "Phone Rec Mixer"},
	{"Left Rec Mixer", "Mic1 Capture Switch", "Mic1 Boost"},
	{"Left Rec Mixer", "Mic2 Capture Switch", "Mic2 Boost"},
	{"Left Rec Mixer", "HP Mixer Capture Switch", "Left HP Mixer"},
	{"Left Rec Mixer", "SPK Mixer Capture Switch", "SPK Mixer"},
	{"Left Rec Mixer", "MoNo Mixer Capture Switch", "Mono Mixer"},

	{"Right Rec Mixer", "LineIn Capture Switch", "Right LineIn"},
	{"Right Rec Mixer", "Phone Capture Switch", "Phone Rec Mixer"},
	{"Right Rec Mixer", "Mic1 Capture Switch", "Mic1 Boost"},
	{"Right Rec Mixer", "Mic2 Capture Switch", "Mic2 Boost"},
	{"Right Rec Mixer", "HP Mixer Capture Switch", "Right HP Mixer"},
	{"Right Rec Mixer", "SPK Mixer Capture Switch", "SPK Mixer"},
	{"Right Rec Mixer", "MoNo Mixer Capture Switch", "Mono Mixer"},

	{"Left ADC", NULL, "DAC Ref"},
	{"Left ADC", NULL, "Left Rec Mixer"},
	{"Right ADC", NULL, "DAC Ref"},
	{"Right ADC", NULL, "Right Rec Mixer"},

	{"PCM src select Mux", "TxDP", "TxDP"},
	{"PCM src select Mux", "ADCR", "Right ADC"},

	{"IIS src select Mux", "ADC", "Left ADC"},
	{"IIS src select Mux", "ADC", "Right ADC"},
	{"IIS src select Mux", "Voice to Stereo", "AIF2RX"},
	{"IIS src select Mux", "SRC2 Output", "SRC2 Enable"},

	{"AIF2TX", NULL, "IIS Interface"},
	{"AIF2TX", NULL, "PCM src select Mux"},
	{"AIF1TX", NULL, "IIS Interface"},
	{"AIF1TX", NULL, "IIS src select Mux"},

	/* Output */
	{"AIF1RX", NULL, "IIS Interface"},
	{"AIF2RX", NULL, "IIS Interface"},

	{"DAC fun Mux", "SRC2 Out", "SRC2 Enable"},
	{"DAC fun Mux", "TxDP", "TxDP"},
	{"DAC fun Mux", "TxDC", "TxDC"},
	{"DAC fun Mux", "Stereo DAC", "AIF1RX"},

	{"VDAC src Mux", "SRC2 Out", "SRC2 Enable"},
	{"VDAC src Mux", "TxDP", "TxDP"},
	{"VDAC src Mux", "TxDC", "TxDC"},
	{"VDAC src Mux", "Voice PCM", "AIF2RX"},

	{"Left DAC", NULL, "DAC Ref"},
	{"Left DAC", NULL, "DAC fun Mux"},
	{"Right DAC", NULL, "DAC Ref"},
	{"Right DAC", NULL, "DAC fun Mux"},
	{"Voice DAC", NULL, "DAC Ref"},
	{"Voice DAC", NULL, "VDAC src Mux"},

	{"Left LineIn Volume", NULL, "Left LineIn"},
	{"Right LineIn Volume", NULL, "Right LineIn"},
	{"Phone Volume", NULL, "Phone"},
	{"Mic1 Volume", NULL, "Mic1 Boost"},
	{"Mic2 Volume", NULL, "Mic2 Boost"},

	{"Left DAC To Mixer", NULL, "Left DAC"},
	{"Right DAC To Mixer", NULL, "Right DAC"},
	{"Voice DAC To Mixer", NULL, "Voice DAC"},

	{"DAC Mixer", NULL, "Left DAC To Mixer"},
	{"DAC Mixer", NULL, "Right DAC To Mixer"},
	{"Line Mixer", NULL, "Left LineIn Volume"},
	{"Line Mixer", NULL, "Right LineIn Volume"},

	{"Mono HP Mixer", "LineIn Playback Switch", "Line Mixer"},
	{"Mono HP Mixer", "Phone Playback Switch", "Phone Volume"},
	{"Mono HP Mixer", "Mic1 Playback Switch", "Mic1 Volume"},
	{"Mono HP Mixer", "Mic2 Playback Switch", "Mic2 Volume"},
	{"Mono HP Mixer", "Voice DAC Playback Switch", "Voice DAC To Mixer"},
	{"Left HP Mixer", "ADC Playback Switch", "Left Rec Mixer"},
	{"Left HP Mixer", "DAC Playback Switch", "Left DAC To Mixer"},
	{"Right HP Mixer", "ADC Playback Switch", "Right Rec Mixer"},
	{"Right HP Mixer", "DAC Playback Switch", "Right DAC To Mixer"},

	{"SPK Mixer", "Line Mixer Playback Switch", "Line Mixer"},
	{"SPK Mixer", "Phone Playback Switch", "Phone Volume"},
	{"SPK Mixer", "Mic1 Playback Switch", "Mic1 Volume"},
	{"SPK Mixer", "Mic2 Playback Switch", "Mic2 Volume"},
	{"SPK Mixer", "DAC Mixer Playback Switch", "DAC Mixer"},
	{"SPK Mixer", "Voice DAC Playback Switch", "Voice DAC To Mixer"},

	{"Mono Mixer", "Line Mixer Playback Switch", "Line Mixer"},
	{"Mono Mixer", "ADCL Playback Switch","Left Rec Mixer"},
	{"Mono Mixer", "ADCR Playback Switch","Right Rec Mixer"},
	{"Mono Mixer", "Mic1 Playback Switch", "Mic1 Volume"},
	{"Mono Mixer", "Mic2 Playback Switch", "Mic2 Volume"},
	{"Mono Mixer", "DAC Mixer Playback Switch", "DAC Mixer"},
	{"Mono Mixer", "Voice DAC Playback Switch", "Voice DAC To Mixer"},

	{"HP Mixer", NULL, "Mono HP Mixer"},
	{"HP Mixer", NULL, "Left HP Mixer"},
	{"HP Mixer", NULL, "Right HP Mixer"},

	{"SPK Vol Input Mux", "VMID", "VMID"},
	{"SPK Vol Input Mux", "HP Mixer", "HP Mixer"},
	{"SPK Vol Input Mux", "SPK Mixer", "SPK Mixer"},
	{"SPK Vol Input Mux", "Mono Mixer", "Mono Mixer"},
	{"SPK Vol Input Mux", NULL, "SPKL Vol"},
	{"SPK Vol Input Mux", NULL, "SPKR Vol"},
	
	{"HPL Vol Input Mux", "HP Mixer", "HP Mixer"},
	{"HPL Vol Input Mux", "VMID", "VMID"},
	{"HP Amp", NULL, "HPL Vol Input Mux"},
	{"HPR Vol Input Mux", "HP Mixer", "HP Mixer"},
	{"HPR Vol Input Mux", "VMID", "VMID"},
	{"HP Amp", NULL, "HPR Vol Input Mux"},

	{"AUX Vol Input Mux", "VMID", "VMID"},
	{"AUX Vol Input Mux", "HP Mixer", "HP Mixer"},
	{"AUX Vol Input Mux", "SPK Mixer", "SPK Mixer"},
	{"AUX Vol Input Mux", "Mono Mixer", "Mono Mixer"},

	{"SPKL", NULL, "SPK Amp"},
	{"SPKL", NULL, "SPK Vol Input Mux"},
	{"SPKR", NULL, "SPK Amp"},
	{"SPKR", NULL, "SPK Vol Input Mux"},
	{"HPL", NULL, "HP Amp"},
	{"HPR", NULL, "HP Amp"},
	{"AUX", NULL, "AUX Vol Input Mux"},

	/* Voice DSP */
	{"SRC1 Enable", "Switch", "AIF1RX"},

	{"RxDP src select Mux", "Voice to Stereo", "AIF2RX"},
	{"RxDP src select Mux", "ADCL to VoDSP", "Left ADC"},
	{"RxDP src select Mux", "SRC1 Output", "SRC1 Enable"},
	{"RxDP src select Mux", "ADCR to VoDSP", "Right ADC"},

	{"RxDP Enable", "Switch", "RxDP src select Mux"},
	{"RxDC Enable", "Switch", "Left ADC"},

	{"RxDP", NULL, "RxDP Enable"},
	{"RxDC", NULL, "RxDC Enable"},
	{"PDM", NULL, "Right ADC"},

	{"Voice DSP", NULL, "RxDP"},
	{"Voice DSP", NULL, "RxDC"},
	{"Voice DSP", NULL, "PDM"},

	{"TxDP", NULL, "Voice DSP"},
	{"TxDC", NULL, "Voice DSP"},

	{"SRC2 src select Mux", "TxDP", "TxDP"},
	{"SRC2 src select Mux", "TxDC", "TxDC"},
	{"SRC2 Enable", "Switch", "SRC2 src select Mux"},
};

struct _pll_div{
	u32 pll_in;
	u32 pll_out;
	u16 regvalue;
};

/**************************************************************
  *	watch out!
  *	our codec support you to select different source as pll input, but if you 
  *	use both of the I2S audio interface and pcm interface instantially. 
  *	The two DAI must have the same pll setting params, so you have to offer
  *	the same pll input, and set our codec's sysclk the same one, we suggest 
  *	24576000.
  **************************************************************/
static const struct _pll_div codec_master_pll1_div[] = {
	{  2048000,   8192000,  0x0ea0},
	{  3686400,   8192000,  0x4e27},
	{ 12000000,   8192000,  0x456b},
	{ 13000000,   8192000,  0x495f},
	{ 13100000,   8192000,  0x0320},
	{  2048000,  11289600,  0xf637},
	{  3686400,  11289600,  0x2f22},
	{ 12000000,  11289600,  0x3e2f},
	{ 13000000,  11289600,  0x4d5b},
	{ 13100000,  11289600,  0x363b},
	{  2048000,  16384000,  0x1ea0},
	{  3686400,  16384000,  0x9e27},
	{ 12000000,  16384000,  0x452b},
	{ 13000000,  16384000,  0x542f},
	{ 13100000,  16384000,  0x03a0},
	{  2048000,  16934400,  0xe625},
	{  3686400,  16934400,  0x9126},
	{ 12000000,  16934400,  0x4d2c},
	{ 13000000,  16934400,  0x742f},
	{ 13100000,  16934400,  0x3c27},
	{  2048000,  22579200,  0x2aa0},
	{  3686400,  22579200,  0x2f20},
	{ 12000000,  22579200,  0x7e2f},
	{ 13000000,  22579200,  0x742f},
	{ 13100000,  22579200,  0x3c27},
	{  2048000,  24576000,  0x2ea0},
	{  3686400,  24576000,  0xee27},
	{ 11289600,  24576000,  0x950F},
	{ 12000000,  24576000,  0x2915},
	{ 12288000,  24576000,  0x0600},
	{ 13000000,  24576000,  0x772e},
	{ 13100000,  24576000,  0x0d20},
	{ 26000000,  24576000,  0x2027},
	{ 26000000,  22579200,  0x392f},
	{ 24576000,  22579200,  0x0921},
	{ 24576000,  24576000,  0x02a0},
};

static const struct _pll_div codec_bclk_pll1_div[] = {
	{  256000,   4096000,  0x3ea0},
	{  352800,   5644800,  0x3ea0},
	{  512000,   8192000,  0x3ea0},
	{  705600,  11289600,  0x3ea0},
	{ 1024000,  16384000,  0x3ea0},	
	{ 1411200,  22579200,  0x3ea0},
	{ 1536000,  24576000,  0x3ea0},	
	{ 2048000,  16384000,  0x1ea0},	
	{ 2822400,  22579200,  0x1ea0},
	{ 3072000,  24576000,  0x1ea0},
	{  705600,  11289600,  0x3ea0},
	{  705600,   8467200,  0x3ab0},
	{ 2822400,  11289600,  0x1ee0},
	{ 3072000,  12288000,  0x1ee0},			
};

static const struct _pll_div codec_vbclk_pll1_div[] = {
	{  256000,   4096000,  0x3ea0},
	{  352800,   5644800,  0x3ea0},
	{  512000,   8192000,  0x3ea0},
	{  705600,  11289600,  0x3ea0},
	{ 1024000,  16384000,  0x3ea0},	
	{ 1411200,  22579200,  0x3ea0},
	{ 1536000,  24576000,  0x3ea0},	
	{ 2048000,  16384000,  0x1ea0},	
	{ 2822400,  22579200,  0x1ea0},
	{ 3072000,  24576000,  0x1ea0},
	{  705600,  11289600,  0x3ea0},
	{  705600,   8467200,  0x3ab0},
};

struct _coeff_div_stereo {
	unsigned int mclk;
	unsigned int rate;
	unsigned int reg60;
	unsigned int reg62;
};

struct _coeff_div_voice {
	unsigned int mclk;
	unsigned int rate;
	unsigned int reg64;
};

/* bclk is config to 32fs, if codec is choose to be slave mode,
input bclk should be 32*fs */
static const struct _coeff_div_stereo coeff_div_stereo[] = {
	{24576000, 48000, 0x3174, 0x1010}, 
	{12288000, 48000, 0x1174, 0x0000},
	{18432000, 48000, 0x2174, 0x1111},
	{36864000, 48000, 0x2274, 0x2020},
	{49152000, 48000, 0xf074, 0x3030},
	{24576000, 48000, 0x3172, 0x1010},
	{24576000,  8000, 0xB274, 0x2424},
	{24576000, 16000, 0xB174, 0x2222},
	{24576000, 32000, 0xB074, 0x2121},
	{22579200, 11025, 0X3374, 0x1414},
	{22579200, 22050, 0X3274, 0x1212},
	{22579200, 44100, 0X3174, 0x1010},
	{12288000,  8000, 0xB174, 0x2222},
	{11289600, 44100, 0X3072, 0x0000},
};

/* bclk is config to 32fs, if codec is choose to be slave mode,
input bclk should be 32*fs */
static const struct _coeff_div_voice coeff_div_voice[] = {
	{24576000, 16000, 0x2622}, 
	{24576000, 8000, 0x2824},
	{2048000,8000,0x3000},
};

static int get_coeff(unsigned int mclk, unsigned int rate, int mode)
{
	int i;

	pr_info("mclk = %d, rate = %d, mode = %d\n", mclk, rate, mode);

	if (!mode)
		for (i = 0; i < ARRAY_SIZE(coeff_div_stereo); i++) {
			if ((coeff_div_stereo[i].rate == rate) &&
				(coeff_div_stereo[i].mclk == mclk))
				return i;
		}
	else
		for (i = 0; i< ARRAY_SIZE(coeff_div_voice); i++) {
			if ((coeff_div_voice[i].rate == rate) &&
				(coeff_div_voice[i].mclk == mclk))
				return i;
		}

	pr_err("can't find a matched mclk and rate in %s\n",
	       (mode ? "coeff_div_voice[]" : "coeff_div_audio[]"));

	return -EINVAL;
}


static int rt5625_set_dai_pll(struct snd_soc_dai *dai, int pll_id, int source,
			unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = dai->codec;
	int i, ret = -EINVAL;

	if (pll_id < RT5625_PLL_MCLK || pll_id > RT5625_PLL_VBCLK)
		return -EINVAL;

	if (!freq_in || !freq_out) {
		dev_info(dai->dev, "PLL is closed\n");
		snd_soc_update_bits(codec, RT5625_GEN_CTRL1,
			RT5625_SCLK_MASK, RT5625_SCLK_MCLK);
		return 0;
	}

	if (RT5625_PLL_MCLK == pll_id) {
		for (i = 0; i < ARRAY_SIZE(codec_master_pll1_div); i ++)
			if ((freq_in == codec_master_pll1_div[i].pll_in) &&
				(freq_out == codec_master_pll1_div[i].pll_out)) {
				snd_soc_write(codec, RT5625_GEN_CTRL2,
					RT5625_PLL1_S_MCLK);
				snd_soc_write(codec, RT5625_PLL_CTRL,
					codec_master_pll1_div[i].regvalue);
				snd_soc_update_bits(codec, RT5625_GEN_CTRL1,
					RT5625_SCLK_MASK, RT5625_SCLK_PLL1);
				ret = 0;
				break;
			}
	} else if (RT5625_PLL_MCLK_TO_VSYSCLK == pll_id) {
		for (i = 0; i < ARRAY_SIZE(codec_master_pll1_div); i ++)
		{
			if ((freq_in == codec_master_pll1_div[i].pll_in) && (freq_out == codec_master_pll1_div[i].pll_out))
			{
				snd_soc_write(codec, RT5625_GEN_CTRL2, 0x0000);  /*PLL source from MCLK*/
				snd_soc_write(codec, RT5625_PLL_CTRL, codec_master_pll1_div[i].regvalue);  /*set pll code*/
				snd_soc_update_bits(codec, RT5625_GEN_CTRL1, 0x0030, 0x0030);  /* Voice SYSCLK source from FLL1 */
				ret = 0;
			}
		}
	}else if (RT5625_PLL_BCLK == pll_id) {
		for (i = 0; i < ARRAY_SIZE(codec_bclk_pll1_div); i ++)
			if ((freq_in == codec_bclk_pll1_div[i].pll_in) &&
				(freq_out == codec_bclk_pll1_div[i].pll_out)) {
				snd_soc_write(codec, RT5625_GEN_CTRL2,
					RT5625_PLL1_S_BCLK);
				snd_soc_write(codec, RT5625_PLL_CTRL,
					codec_bclk_pll1_div[i].regvalue);
				snd_soc_update_bits(codec, RT5625_GEN_CTRL1,
					RT5625_SCLK_MASK, RT5625_SCLK_PLL1);
				ret = 0;
				break;
			}
	} else if (RT5625_PLL_VBCLK == pll_id) {
		for (i = 0; i < ARRAY_SIZE(codec_vbclk_pll1_div); i ++)
			if ((freq_in == codec_vbclk_pll1_div[i].pll_in) &&
				(freq_out == codec_vbclk_pll1_div[i].pll_out)) {
				snd_soc_write(codec, RT5625_GEN_CTRL2,
					RT5625_PLL1_S_VBCLK);
				snd_soc_write(codec, RT5625_PLL_CTRL,
					codec_vbclk_pll1_div[i].regvalue);
				snd_soc_update_bits(codec, RT5625_GEN_CTRL1,
					RT5625_SCLK_MASK, RT5625_SCLK_PLL1);
				ret = 0;
				break;
			}
	}

	return ret;
}


static int rt5625_set_dai_sysclk(struct snd_soc_dai *dai, 
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	unsigned int val = 0;

	switch (dai->id) {
	case RT5625_AIF1:
		if (freq == rt5625->stereo_sysclk)
			return 0;
		rt5625->stereo_sysclk = freq;
		break;

	case RT5625_AIF2:
		if (freq == rt5625->voice_sysclk)
			return 0;
		rt5625->voice_sysclk = freq;
		break;

	default:
		return -EINVAL;
	}

	switch (clk_id) {
	case RT5625_SCLK_S_MCLK:
		break;

	case RT5625_SCLK_S_PLL:
		val |= RT5625_SCLK_PLL1;
		break;

	default:
		dev_err(codec->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}
	snd_soc_update_bits(codec, RT5625_GEN_CTRL1,
			RT5625_SCLK_MASK, val);

	dev_dbg(dai->dev, "Sysclk is %dHz and clock id is %d\n", freq, clk_id);
	
	return 0;
}

static int rt5625_hw_params(struct snd_pcm_substream *substream, 
			struct snd_pcm_hw_params *params,
			struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	unsigned int iface = 0, rate = params_rate(params), coeff;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;

	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= RT5625_I2S_DL_20;
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= RT5625_I2S_DL_24;
		break;

	case SNDRV_PCM_FORMAT_S8:
		iface |= RT5625_I2S_DL_8;
		break;

	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5625_AIF1:
		coeff = get_coeff(rt5625->stereo_sysclk, rate, 0);
		if (coeff < 0) {
			dev_err(codec->dev, "Unsupported clock setting\n");
			return -EINVAL;
		}
		snd_soc_write(codec, RT5625_DAC_CLK_CTRL1,
				coeff_div_stereo[coeff].reg60);
		snd_soc_write(codec, RT5625_DAC_CLK_CTRL2,
				coeff_div_stereo[coeff].reg62);
		snd_soc_update_bits(codec, RT5625_SDP_CTRL,
				RT5625_I2S_DL_MASK, iface);
		break;

	case RT5625_AIF2:
		rate = 8000;
		coeff = get_coeff(rt5625->voice_sysclk, rate, 1);
		if (coeff < 0) {
			dev_err(codec->dev, "Unsupported clock setting\n");
			return -EINVAL;
		}
		snd_soc_write(codec, RT5625_VDAC_CLK_CTRL1,
				coeff_div_voice[coeff].reg64);
		iface |= 0x0100; //Voice SYSCLK source from FLL1
		snd_soc_update_bits(codec, RT5625_EXT_SDP_CTRL,
				RT5625_I2S_DL_MASK, iface);
		break;

	default:
		return -EINVAL;
	}

#ifdef RT5625_F_SMT_PHO
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		rt5625->app_bmp |= RT5625_PLY_MASK;
	else
		rt5625->app_bmp |= RT5625_REC_MASK;
#endif

	return 0;
}

static int rt5625_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int iface = 0;
	bool slave;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		slave = false;
		break;

	case SND_SOC_DAIFMT_CBS_CFS:
		slave = true;
		break;

	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;

	case SND_SOC_DAIFMT_LEFT_J:
		iface |= RT5625_I2S_DF_LEFT;
		break;

	case SND_SOC_DAIFMT_DSP_A:
		iface |= RT5625_I2S_DF_PCM_A;
		break;

	case SND_SOC_DAIFMT_DSP_B:
		iface |= RT5625_I2S_DF_PCM_B;
		break;

	default:
		return -EINVAL;			
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;

	case SND_SOC_DAIFMT_IB_NF:
		iface |= RT5625_I2S_BP_INV;
		break;

	default:
		return -EINVAL;
	}
	iface |= 0x8000;      /*enable vopcm*/
	
	switch (dai->id) {
	case RT5625_AIF1:
		if (slave)
			iface |= RT5625_I2S_M_SLV;
		snd_soc_update_bits(codec, RT5625_SDP_CTRL,
			RT5625_I2S_M_MASK | RT5625_I2S_BP_MASK |
			RT5625_I2S_DF_MASK, iface);
		break;

	case RT5625_AIF2:
		if (slave)
			iface |= RT5625_PCM_M_SLV;
		snd_soc_update_bits(codec, RT5625_EXT_SDP_CTRL,
			RT5625_PCM_M_MASK | RT5625_I2S_BP_MASK |
			RT5625_I2S_DF_MASK, iface);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

#ifdef RT5625_F_SMT_PHO
static int rt5625_trigger(struct snd_pcm_substream *substream,
	int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct rt5625_priv *rt5625 = snd_soc_codec_get_drvdata(codec);
	int capture = (substream->stream == SNDRV_PCM_STREAM_CAPTURE);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (capture)
			rt5625->app_bmp |= RT5625_REC_MASK;
		else
			rt5625->app_bmp |= RT5625_PLY_MASK;
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (capture)
			rt5625->app_bmp &= ~RT5625_REC_MASK;
		else
			rt5625->app_bmp &= ~RT5625_PLY_MASK;
		rt5625->app_bmp &= ~RT5625_VOIP_MASK;
		break;

	default:
		break;
	}

	return 0;
}
#endif

static int rt5625_set_bias_level(struct snd_soc_codec *codec, 
			enum snd_soc_bias_level level)
{
	switch(level) {
	case SND_SOC_BIAS_ON:
#ifdef RT5625_DEMO
		snd_soc_update_bits(codec, RT5625_HP_OUT_VOL,
				RT5625_L_MUTE | RT5625_R_MUTE, 0);
		snd_soc_update_bits(codec, RT5625_SPK_OUT_VOL,
				RT5625_L_MUTE | RT5625_R_MUTE, 0);
#endif
		break;

	case SND_SOC_BIAS_PREPARE:
		snd_soc_write(codec, RT5625_PD_CTRL, 0x0000);
		snd_soc_update_bits(codec, RT5625_PWR_ADD1,
				RT5625_P_MB1 | RT5625_P_MB2,
				RT5625_P_MB1 | RT5625_P_MB2);
		break;

	case SND_SOC_BIAS_STANDBY:
#ifdef RT5625_DEMO
		snd_soc_update_bits(codec, RT5625_HP_OUT_VOL,
			RT5625_L_MUTE | RT5625_R_MUTE,
			RT5625_L_MUTE | RT5625_R_MUTE);
		snd_soc_update_bits(codec, RT5625_SPK_OUT_VOL,
			RT5625_L_MUTE | RT5625_R_MUTE,
			RT5625_L_MUTE | RT5625_R_MUTE);
		snd_soc_update_bits(codec, RT5625_PWR_ADD1,
				RT5625_P_MB1 | RT5625_P_MB2, 0);
#endif
		if (SND_SOC_BIAS_OFF == codec->dapm.bias_level) {
			snd_soc_write(codec, RT5625_PD_CTRL, 0);
			snd_soc_write(codec, RT5625_PWR_ADD1,
					RT5625_P_MAIN_BIAS);
			snd_soc_write(codec, RT5625_PWR_ADD2, RT5625_P_VREF);
			codec->cache_only = false;
			snd_soc_cache_sync(codec);
		}
		break;

	case SND_SOC_BIAS_OFF:
#ifdef RT5625_DEMO
		snd_soc_update_bits(codec, RT5625_HP_OUT_VOL,
			RT5625_L_MUTE | RT5625_R_MUTE,
			RT5625_L_MUTE | RT5625_R_MUTE);
		snd_soc_update_bits(codec, RT5625_SPK_OUT_VOL,
			RT5625_L_MUTE | RT5625_R_MUTE,
			RT5625_L_MUTE | RT5625_R_MUTE);
#endif
		snd_soc_write(codec, RT5625_PWR_ADD1, 0x0000);
		snd_soc_write(codec, RT5625_PWR_ADD2, 0x0000);
		snd_soc_write(codec, RT5625_PWR_ADD3, 0x0000);
		
		break;
	}
	codec->dapm.bias_level = level;

	return 0;
}

#ifdef RT5625_PROC	
static int rt5625_proc_init(void);
#endif

static int rt5625_probe(struct snd_soc_codec *codec)
{
	int ret;

	ret = snd_soc_codec_set_cache_io(codec, 8, 16, SND_SOC_I2C);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}
	
	#ifdef RT5625_PROC	
	rt5625_proc_init();
	#endif
	
	rt5625_reset(codec);
	snd_soc_write(codec, RT5625_PD_CTRL, 0);
	snd_soc_write(codec, RT5625_PWR_ADD1, RT5625_P_MAIN_BIAS);
	snd_soc_write(codec, RT5625_PWR_ADD2, RT5625_P_VREF);
#ifdef RT5625_DEMO
	rt5625_reg_init(codec);
#endif
 	codec->dapm.bias_level = SND_SOC_BIAS_STANDBY;
	rt5625_codec = codec;
	return 0;
}

static int rt5625_remove(struct snd_soc_codec *codec)
{
	rt5625_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

#ifdef CONFIG_PM
static int rt5625_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	rt5625_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int rt5625_resume(struct snd_soc_codec *codec)
{
	rt5625_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}
#else
#define rt5625_suspend NULL
#define rt5625_resume NULL
#endif

#define RT5625_STEREO_RATES SNDRV_PCM_RATE_8000_48000
#define RT5625_VOICE_RATES SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_8000
#define RT5625_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S8)

struct snd_soc_dai_ops rt5625_aif_dai_ops = {
#ifdef RT5625_F_SMT_PHO
	.trigger = rt5625_trigger,
#endif
	.hw_params = rt5625_hw_params,
	.set_fmt = rt5625_set_dai_fmt,
	.set_sysclk = rt5625_set_dai_sysclk,
	.set_pll = rt5625_set_dai_pll,
};

struct snd_soc_dai_driver rt5625_dai[] = {
	{
		.name = "rt5625-aif1",
		.id = RT5625_AIF1,
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5625_STEREO_RATES,
			.formats = RT5625_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5625_STEREO_RATES,
			.formats = RT5625_FORMATS,
		},
		.ops = &rt5625_aif_dai_ops,
	},
	{
		.name = "rt5625-aif2",
		.id = RT5625_AIF2,
		.playback = {
			.stream_name = "Voice Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5625_VOICE_RATES,
			.formats = RT5625_FORMATS,
		},
		.capture = {
			.stream_name = "Voice Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5625_VOICE_RATES,
			.formats = RT5625_FORMATS,
		},
		.ops = &rt5625_aif_dai_ops,
	},
};

static struct snd_soc_codec_driver soc_codec_dev_rt5625 = {
	.probe = rt5625_probe,
	.remove = rt5625_remove,
	.suspend = rt5625_suspend,
	.resume = rt5625_resume,
	.set_bias_level = rt5625_set_bias_level,
	.reg_cache_size = 0x80,
	.reg_word_size = sizeof(u16),
	.reg_cache_default = rt5625_reg,
	.volatile_register = rt5625_volatile_register,
	.readable_register = rt5625_readable_register,
	.reg_cache_step = 1,
	.controls = rt5625_snd_controls,
	.num_controls = ARRAY_SIZE(rt5625_snd_controls),
	.dapm_widgets = rt5625_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt5625_dapm_widgets),
	.dapm_routes = rt5625_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt5625_dapm_routes),
};

static const struct i2c_device_id rt5625_i2c_id[] = {
	{ "rt5625", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5625_i2c_id);

static int rt5625_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	struct rt5625_priv *rt5625;
	int ret;

	rt5625 = kzalloc(sizeof(struct rt5625_priv), GFP_KERNEL);
	if (NULL == rt5625)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5625);

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_rt5625,
			rt5625_dai, ARRAY_SIZE(rt5625_dai));
	if (ret < 0)
		kfree(rt5625);

	return ret;
}

static __devexit int rt5625_i2c_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_codec(&i2c->dev);
	kfree(i2c_get_clientdata(i2c));
	return 0;
}

struct i2c_driver rt5625_i2c_driver = {
	.driver = {
		.name = "rt5625",
		.owner = THIS_MODULE,
	},
	.probe = rt5625_i2c_probe,
	.remove   = __devexit_p(rt5625_i2c_remove),
	.id_table = rt5625_i2c_id,
};

static int __init rt5625_modinit(void)
{
	return i2c_add_driver(&rt5625_i2c_driver);
}
module_init(rt5625_modinit);

static void __exit rt5625_modexit(void)
{
	i2c_del_driver(&rt5625_i2c_driver);
}
module_exit(rt5625_modexit);

MODULE_DESCRIPTION("ASoC RT5625 driver");
MODULE_AUTHOR("Johnny Hsu <johnnyhsu@realtek.com>");
MODULE_LICENSE("GPL");


#ifdef RT5625_PROC

static ssize_t rt5625_proc_write(struct file *file, const char __user *buffer,
			   unsigned long len, void *data)
{
	char *cookie_pot; 
	char *p;
	int reg;
	int value;
	
	cookie_pot = (char *)vmalloc( len );
	if (!cookie_pot) 
	{
		return -ENOMEM;
	} 
	else 
	{
		if (copy_from_user( cookie_pot, buffer, len )) 
			return -EFAULT;
	}

	switch(cookie_pot[0])
	{
	case 'd':
	case 'D':
		debug_write_read ++;
		debug_write_read %= 2;
		if(debug_write_read != 0)
			printk("Debug read and write reg on\n");
		else	
			printk("Debug read and write reg off\n");	
		break;	
	case 'r':
	case 'R':
		printk("Read reg debug\n");		
		if(cookie_pot[1] ==':')
		{
			debug_write_read = 1;
			strsep(&cookie_pot,":");
			while((p=strsep(&cookie_pot,",")))
			{
				reg = simple_strtol(p,NULL,16);
				value = rt5625_read(rt5625_codec,reg);
				printk("rt5625_read:0x%04x = 0x%04x\n",reg,value);
			}
			debug_write_read = 0;
			printk("\n");
		}
		else
		{
			printk("Error Read reg debug.\n");
			printk("For example: echo r:22,23,24,25>rt5625_ts\n");
		}
		break;
	case 'w':
	case 'W':
		printk("Write reg debug\n");		
		if(cookie_pot[1] ==':')
		{
			debug_write_read = 1;
			strsep(&cookie_pot,":");
			while((p=strsep(&cookie_pot,"=")))
			{
				reg = simple_strtol(p,NULL,16);
				p=strsep(&cookie_pot,",");
				value = simple_strtol(p,NULL,16);
				rt5625_write(rt5625_codec,reg,value);
				printk("rt5625_write:0x%04x = 0x%04x\n",reg,value);
			}
			debug_write_read = 0;
			printk("\n");
		}
		else
		{
			printk("Error Write reg debug.\n");
			printk("For example: w:22=0,23=0,24=0,25=0>rt5625_ts\n");
		}
		break;
	case 'a':
		printk("Dump reg \n");		

		for(reg = 0; reg < 0x6e; reg+=2)
		{
			value = rt5625_read(rt5625_codec,reg);
			printk("rt5625_read:0x%04x = 0x%04x\n",reg,value);
		}

		break;		
	default:
		printk("Help for rt5625_ts .\n-->The Cmd list: \n");
		printk("-->'d&&D' Open or Off the debug\n");
		printk("-->'r&&R' Read reg debug,Example: echo 'r:22,23,24,25'>rt5625_ts\n");
		printk("-->'w&&W' Write reg debug,Example: echo 'w:22=0,23=0,24=0,25=0'>rt5625_ts\n");
		break;
	}

	return len;
}

static const struct file_operations rt5625_proc_fops = {
	.owner		= THIS_MODULE,
};

static int rt5625_proc_init(void)
{
	struct proc_dir_entry *rt5625_proc_entry;
	rt5625_proc_entry = create_proc_entry("driver/rt5625_ts", 0777, NULL);
	if(rt5625_proc_entry != NULL)
	{
		rt5625_proc_entry->write_proc = rt5625_proc_write;
		return 0;
	}
	else
	{
		printk("create proc error !\n");
		return -1;
	}
}
#endif

