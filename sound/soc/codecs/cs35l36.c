// SPDX-License-Identifier: GPL-2.0
//
// cs35l36.c -- CS35L36 ALSA SoC audio driver
//
// Copyright 2018 Cirrus Logic, Inc.
//
// Author: James Schulman <james.schulman@cirrus.com>

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/gpio.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/cs35l36.h>
#include <linux/of_irq.h>
#include <linux/completion.h>

#include "cs35l36.h"

/*
 * Some fields take zero as a valid value so use a high bit flag that won't
 * get written to the device to mark those.
 */
#define CS35L36_VALID_PDATA 0x80000000

static const char * const cs35l36_supplies[] = {
	"VA",
	"VP",
};

struct  cs35l36_private {
	struct device *dev;
	struct cs35l36_platform_data pdata;
	struct regmap *regmap;
	struct regulator_bulk_data supplies[2];
	int num_supplies;
	int clksrc;
	int chip_version;
	int rev_id;
	int ldm_mode_sel;
	struct gpio_desc *reset_gpio;
};

struct cs35l36_pll_config {
	int freq;
	int clk_cfg;
	int fll_igain;
};

static const struct cs35l36_pll_config cs35l36_pll_sysclk[] = {
	{32768,		0x00, 0x05},
	{8000,		0x01, 0x03},
	{11025,		0x02, 0x03},
	{12000,		0x03, 0x03},
	{16000,		0x04, 0x04},
	{22050,		0x05, 0x04},
	{24000,		0x06, 0x04},
	{32000,		0x07, 0x05},
	{44100,		0x08, 0x05},
	{48000,		0x09, 0x05},
	{88200,		0x0A, 0x06},
	{96000,		0x0B, 0x06},
	{128000,	0x0C, 0x07},
	{176400,	0x0D, 0x07},
	{192000,	0x0E, 0x07},
	{256000,	0x0F, 0x08},
	{352800,	0x10, 0x08},
	{384000,	0x11, 0x08},
	{512000,	0x12, 0x09},
	{705600,	0x13, 0x09},
	{750000,	0x14, 0x09},
	{768000,	0x15, 0x09},
	{1000000,	0x16, 0x0A},
	{1024000,	0x17, 0x0A},
	{1200000,	0x18, 0x0A},
	{1411200,	0x19, 0x0A},
	{1500000,	0x1A, 0x0A},
	{1536000,	0x1B, 0x0A},
	{2000000,	0x1C, 0x0A},
	{2048000,	0x1D, 0x0A},
	{2400000,	0x1E, 0x0A},
	{2822400,	0x1F, 0x0A},
	{3000000,	0x20, 0x0A},
	{3072000,	0x21, 0x0A},
	{3200000,	0x22, 0x0A},
	{4000000,	0x23, 0x0A},
	{4096000,	0x24, 0x0A},
	{4800000,	0x25, 0x0A},
	{5644800,	0x26, 0x0A},
	{6000000,	0x27, 0x0A},
	{6144000,	0x28, 0x0A},
	{6250000,	0x29, 0x08},
	{6400000,	0x2A, 0x0A},
	{6500000,	0x2B, 0x08},
	{6750000,	0x2C, 0x09},
	{7526400,	0x2D, 0x0A},
	{8000000,	0x2E, 0x0A},
	{8192000,	0x2F, 0x0A},
	{9600000,	0x30, 0x0A},
	{11289600,	0x31, 0x0A},
	{12000000,	0x32, 0x0A},
	{12288000,	0x33, 0x0A},
	{12500000,	0x34, 0x08},
	{12800000,	0x35, 0x0A},
	{13000000,	0x36, 0x0A},
	{13500000,	0x37, 0x0A},
	{19200000,	0x38, 0x0A},
	{22579200,	0x39, 0x0A},
	{24000000,	0x3A, 0x0A},
	{24576000,	0x3B, 0x0A},
	{25000000,	0x3C, 0x0A},
	{25600000,	0x3D, 0x0A},
	{26000000,	0x3E, 0x0A},
	{27000000,	0x3F, 0x0A},
};

static struct reg_default cs35l36_reg[] = {
	{CS35L36_TESTKEY_CTRL,			0x00000000},
	{CS35L36_USERKEY_CTL,			0x00000000},
	{CS35L36_OTP_CTRL1,			0x00002460},
	{CS35L36_OTP_CTRL2,			0x00000000},
	{CS35L36_OTP_CTRL3,			0x00000000},
	{CS35L36_OTP_CTRL4,			0x00000000},
	{CS35L36_OTP_CTRL5,			0x00000000},
	{CS35L36_PAC_CTL1,			0x00000004},
	{CS35L36_PAC_CTL2,			0x00000000},
	{CS35L36_PAC_CTL3,			0x00000000},
	{CS35L36_PWR_CTRL1,			0x00000000},
	{CS35L36_PWR_CTRL2,			0x00003321},
	{CS35L36_PWR_CTRL3,			0x01000010},
	{CS35L36_CTRL_OVRRIDE,			0x00000002},
	{CS35L36_AMP_OUT_MUTE,			0x00000000},
	{CS35L36_OTP_TRIM_STATUS,		0x00000000},
	{CS35L36_DISCH_FILT,			0x00000000},
	{CS35L36_PROTECT_REL_ERR,		0x00000000},
	{CS35L36_PAD_INTERFACE,			0x00000038},
	{CS35L36_PLL_CLK_CTRL,			0x00000010},
	{CS35L36_GLOBAL_CLK_CTRL,		0x00000003},
	{CS35L36_ADC_CLK_CTRL,			0x00000000},
	{CS35L36_SWIRE_CLK_CTRL,		0x00000000},
	{CS35L36_SP_SCLK_CLK_CTRL,		0x00000000},
	{CS35L36_MDSYNC_EN,			0x00000000},
	{CS35L36_MDSYNC_TX_ID,			0x00000000},
	{CS35L36_MDSYNC_PWR_CTRL,		0x00000000},
	{CS35L36_MDSYNC_DATA_TX,		0x00000000},
	{CS35L36_MDSYNC_TX_STATUS,		0x00000002},
	{CS35L36_MDSYNC_RX_STATUS,		0x00000000},
	{CS35L36_MDSYNC_ERR_STATUS,		0x00000000},
	{CS35L36_BSTCVRT_VCTRL1,		0x00000000},
	{CS35L36_BSTCVRT_VCTRL2,		0x00000001},
	{CS35L36_BSTCVRT_PEAK_CUR,		0x0000004A},
	{CS35L36_BSTCVRT_SFT_RAMP,		0x00000003},
	{CS35L36_BSTCVRT_COEFF,			0x00002424},
	{CS35L36_BSTCVRT_SLOPE_LBST,		0x00005800},
	{CS35L36_BSTCVRT_SW_FREQ,		0x00010000},
	{CS35L36_BSTCVRT_DCM_CTRL,		0x00002001},
	{CS35L36_BSTCVRT_DCM_MODE_FORCE,	0x00000000},
	{CS35L36_BSTCVRT_OVERVOLT_CTRL,		0x00000130},
	{CS35L36_VPI_LIMIT_MODE,		0x00000000},
	{CS35L36_VPI_LIMIT_MINMAX,		0x00003000},
	{CS35L36_VPI_VP_THLD,			0x00101010},
	{CS35L36_VPI_TRACK_CTRL,		0x00000000},
	{CS35L36_VPI_TRIG_MODE_CTRL,		0x00000000},
	{CS35L36_VPI_TRIG_STEPS,		0x00000000},
	{CS35L36_VI_SPKMON_FILT,		0x00000003},
	{CS35L36_VI_SPKMON_GAIN,		0x00000909},
	{CS35L36_VI_SPKMON_IP_SEL,		0x00000000},
	{CS35L36_DTEMP_WARN_THLD,		0x00000002},
	{CS35L36_DTEMP_STATUS,			0x00000000},
	{CS35L36_VPVBST_FS_SEL,			0x00000001},
	{CS35L36_VPVBST_VP_CTRL,		0x000001C0},
	{CS35L36_VPVBST_VBST_CTRL,		0x000001C0},
	{CS35L36_ASP_TX_PIN_CTRL,		0x00000028},
	{CS35L36_ASP_RATE_CTRL,			0x00090000},
	{CS35L36_ASP_FORMAT,			0x00000002},
	{CS35L36_ASP_FRAME_CTRL,		0x00180018},
	{CS35L36_ASP_TX1_TX2_SLOT,		0x00010000},
	{CS35L36_ASP_TX3_TX4_SLOT,		0x00030002},
	{CS35L36_ASP_TX5_TX6_SLOT,		0x00050004},
	{CS35L36_ASP_TX7_TX8_SLOT,		0x00070006},
	{CS35L36_ASP_RX1_SLOT,			0x00000000},
	{CS35L36_ASP_RX_TX_EN,			0x00000000},
	{CS35L36_ASP_RX1_SEL,			0x00000008},
	{CS35L36_ASP_TX1_SEL,			0x00000018},
	{CS35L36_ASP_TX2_SEL,			0x00000019},
	{CS35L36_ASP_TX3_SEL,			0x00000028},
	{CS35L36_ASP_TX4_SEL,			0x00000029},
	{CS35L36_ASP_TX5_SEL,			0x00000020},
	{CS35L36_ASP_TX6_SEL,			0x00000000},
	{CS35L36_SWIRE_P1_TX1_SEL,		0x00000018},
	{CS35L36_SWIRE_P1_TX2_SEL,		0x00000019},
	{CS35L36_SWIRE_P2_TX1_SEL,		0x00000028},
	{CS35L36_SWIRE_P2_TX2_SEL,		0x00000029},
	{CS35L36_SWIRE_P2_TX3_SEL,		0x00000020},
	{CS35L36_SWIRE_DP1_FIFO_CFG,		0x0000001B},
	{CS35L36_SWIRE_DP2_FIFO_CFG,		0x0000001B},
	{CS35L36_SWIRE_DP3_FIFO_CFG,		0x0000001B},
	{CS35L36_SWIRE_PCM_RX_DATA,		0x00000000},
	{CS35L36_SWIRE_FS_SEL,			0x00000001},
	{CS35L36_AMP_DIG_VOL_CTRL,		0x00008000},
	{CS35L36_VPBR_CFG,			0x02AA1905},
	{CS35L36_VBBR_CFG,			0x02AA1905},
	{CS35L36_VPBR_STATUS,			0x00000000},
	{CS35L36_VBBR_STATUS,			0x00000000},
	{CS35L36_OVERTEMP_CFG,			0x00000001},
	{CS35L36_AMP_ERR_VOL,			0x00000000},
	{CS35L36_CLASSH_CFG,			0x000B0405},
	{CS35L36_CLASSH_FET_DRV_CFG,		0x00000111},
	{CS35L36_NG_CFG,			0x00000033},
	{CS35L36_AMP_GAIN_CTRL,			0x00000273},
	{CS35L36_PWM_MOD_IO_CTRL,		0x00000000},
	{CS35L36_PWM_MOD_STATUS,		0x00000000},
	{CS35L36_DAC_MSM_CFG,			0x00000000},
	{CS35L36_AMP_SLOPE_CTRL,		0x00000B00},
	{CS35L36_AMP_PDM_VOLUME,		0x00000000},
	{CS35L36_AMP_PDM_RATE_CTRL,		0x00000000},
	{CS35L36_PDM_CH_SEL,			0x00000000},
	{CS35L36_AMP_NG_CTRL,			0x0000212F},
	{CS35L36_PDM_HIGHFILT_CTRL,		0x00000000},
	{CS35L36_PAC_INT0_CTRL,			0x00000001},
	{CS35L36_PAC_INT1_CTRL,			0x00000001},
	{CS35L36_PAC_INT2_CTRL,			0x00000001},
	{CS35L36_PAC_INT3_CTRL,			0x00000001},
	{CS35L36_PAC_INT4_CTRL,			0x00000001},
	{CS35L36_PAC_INT5_CTRL,			0x00000001},
	{CS35L36_PAC_INT6_CTRL,			0x00000001},
	{CS35L36_PAC_INT7_CTRL,			0x00000001},
};

static bool cs35l36_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L36_SW_RESET:
	case CS35L36_SW_REV:
	case CS35L36_HW_REV:
	case CS35L36_TESTKEY_CTRL:
	case CS35L36_USERKEY_CTL:
	case CS35L36_OTP_MEM30:
	case CS35L36_OTP_CTRL1:
	case CS35L36_OTP_CTRL2:
	case CS35L36_OTP_CTRL3:
	case CS35L36_OTP_CTRL4:
	case CS35L36_OTP_CTRL5:
	case CS35L36_PAC_CTL1:
	case CS35L36_PAC_CTL2:
	case CS35L36_PAC_CTL3:
	case CS35L36_DEVICE_ID:
	case CS35L36_FAB_ID:
	case CS35L36_REV_ID:
	case CS35L36_PWR_CTRL1:
	case CS35L36_PWR_CTRL2:
	case CS35L36_PWR_CTRL3:
	case CS35L36_CTRL_OVRRIDE:
	case CS35L36_AMP_OUT_MUTE:
	case CS35L36_OTP_TRIM_STATUS:
	case CS35L36_DISCH_FILT:
	case CS35L36_PROTECT_REL_ERR:
	case CS35L36_PAD_INTERFACE:
	case CS35L36_PLL_CLK_CTRL:
	case CS35L36_GLOBAL_CLK_CTRL:
	case CS35L36_ADC_CLK_CTRL:
	case CS35L36_SWIRE_CLK_CTRL:
	case CS35L36_SP_SCLK_CLK_CTRL:
	case CS35L36_TST_FS_MON0:
	case CS35L36_MDSYNC_EN:
	case CS35L36_MDSYNC_TX_ID:
	case CS35L36_MDSYNC_PWR_CTRL:
	case CS35L36_MDSYNC_DATA_TX:
	case CS35L36_MDSYNC_TX_STATUS:
	case CS35L36_MDSYNC_RX_STATUS:
	case CS35L36_MDSYNC_ERR_STATUS:
	case CS35L36_BSTCVRT_VCTRL1:
	case CS35L36_BSTCVRT_VCTRL2:
	case CS35L36_BSTCVRT_PEAK_CUR:
	case CS35L36_BSTCVRT_SFT_RAMP:
	case CS35L36_BSTCVRT_COEFF:
	case CS35L36_BSTCVRT_SLOPE_LBST:
	case CS35L36_BSTCVRT_SW_FREQ:
	case CS35L36_BSTCVRT_DCM_CTRL:
	case CS35L36_BSTCVRT_DCM_MODE_FORCE:
	case CS35L36_BSTCVRT_OVERVOLT_CTRL:
	case CS35L36_BST_TST_MANUAL:
	case CS35L36_BST_ANA2_TEST:
	case CS35L36_VPI_LIMIT_MODE:
	case CS35L36_VPI_LIMIT_MINMAX:
	case CS35L36_VPI_VP_THLD:
	case CS35L36_VPI_TRACK_CTRL:
	case CS35L36_VPI_TRIG_MODE_CTRL:
	case CS35L36_VPI_TRIG_STEPS:
	case CS35L36_VI_SPKMON_FILT:
	case CS35L36_VI_SPKMON_GAIN:
	case CS35L36_VI_SPKMON_IP_SEL:
	case CS35L36_DTEMP_WARN_THLD:
	case CS35L36_DTEMP_STATUS:
	case CS35L36_VPVBST_FS_SEL:
	case CS35L36_VPVBST_VP_CTRL:
	case CS35L36_VPVBST_VBST_CTRL:
	case CS35L36_ASP_TX_PIN_CTRL:
	case CS35L36_ASP_RATE_CTRL:
	case CS35L36_ASP_FORMAT:
	case CS35L36_ASP_FRAME_CTRL:
	case CS35L36_ASP_TX1_TX2_SLOT:
	case CS35L36_ASP_TX3_TX4_SLOT:
	case CS35L36_ASP_TX5_TX6_SLOT:
	case CS35L36_ASP_TX7_TX8_SLOT:
	case CS35L36_ASP_RX1_SLOT:
	case CS35L36_ASP_RX_TX_EN:
	case CS35L36_ASP_RX1_SEL:
	case CS35L36_ASP_TX1_SEL:
	case CS35L36_ASP_TX2_SEL:
	case CS35L36_ASP_TX3_SEL:
	case CS35L36_ASP_TX4_SEL:
	case CS35L36_ASP_TX5_SEL:
	case CS35L36_ASP_TX6_SEL:
	case CS35L36_SWIRE_P1_TX1_SEL:
	case CS35L36_SWIRE_P1_TX2_SEL:
	case CS35L36_SWIRE_P2_TX1_SEL:
	case CS35L36_SWIRE_P2_TX2_SEL:
	case CS35L36_SWIRE_P2_TX3_SEL:
	case CS35L36_SWIRE_DP1_FIFO_CFG:
	case CS35L36_SWIRE_DP2_FIFO_CFG:
	case CS35L36_SWIRE_DP3_FIFO_CFG:
	case CS35L36_SWIRE_PCM_RX_DATA:
	case CS35L36_SWIRE_FS_SEL:
	case CS35L36_AMP_DIG_VOL_CTRL:
	case CS35L36_VPBR_CFG:
	case CS35L36_VBBR_CFG:
	case CS35L36_VPBR_STATUS:
	case CS35L36_VBBR_STATUS:
	case CS35L36_OVERTEMP_CFG:
	case CS35L36_AMP_ERR_VOL:
	case CS35L36_CLASSH_CFG:
	case CS35L36_CLASSH_FET_DRV_CFG:
	case CS35L36_NG_CFG:
	case CS35L36_AMP_GAIN_CTRL:
	case CS35L36_PWM_MOD_IO_CTRL:
	case CS35L36_PWM_MOD_STATUS:
	case CS35L36_DAC_MSM_CFG:
	case CS35L36_AMP_SLOPE_CTRL:
	case CS35L36_AMP_PDM_VOLUME:
	case CS35L36_AMP_PDM_RATE_CTRL:
	case CS35L36_PDM_CH_SEL:
	case CS35L36_AMP_NG_CTRL:
	case CS35L36_PDM_HIGHFILT_CTRL:
	case CS35L36_INT1_STATUS:
	case CS35L36_INT2_STATUS:
	case CS35L36_INT3_STATUS:
	case CS35L36_INT4_STATUS:
	case CS35L36_INT1_RAW_STATUS:
	case CS35L36_INT2_RAW_STATUS:
	case CS35L36_INT3_RAW_STATUS:
	case CS35L36_INT4_RAW_STATUS:
	case CS35L36_INT1_MASK:
	case CS35L36_INT2_MASK:
	case CS35L36_INT3_MASK:
	case CS35L36_INT4_MASK:
	case CS35L36_INT1_EDGE_LVL_CTRL:
	case CS35L36_INT3_EDGE_LVL_CTRL:
	case CS35L36_PAC_INT_STATUS:
	case CS35L36_PAC_INT_RAW_STATUS:
	case CS35L36_PAC_INT_FLUSH_CTRL:
	case CS35L36_PAC_INT0_CTRL:
	case CS35L36_PAC_INT1_CTRL:
	case CS35L36_PAC_INT2_CTRL:
	case CS35L36_PAC_INT3_CTRL:
	case CS35L36_PAC_INT4_CTRL:
	case CS35L36_PAC_INT5_CTRL:
	case CS35L36_PAC_INT6_CTRL:
	case CS35L36_PAC_INT7_CTRL:
		return true;
	default:
		if (reg >= CS35L36_PAC_PMEM_WORD0 &&
			reg <= CS35L36_PAC_PMEM_WORD1023)
			return true;
		else
			return false;
	}
}

static bool cs35l36_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L36_TESTKEY_CTRL:
	case CS35L36_USERKEY_CTL:
	case CS35L36_TST_FS_MON0:
		return true;
	default:
		return false;
	}
}

static bool cs35l36_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L36_SW_RESET:
	case CS35L36_SW_REV:
	case CS35L36_HW_REV:
	case CS35L36_TESTKEY_CTRL:
	case CS35L36_USERKEY_CTL:
	case CS35L36_DEVICE_ID:
	case CS35L36_FAB_ID:
	case CS35L36_REV_ID:
	case CS35L36_INT1_STATUS:
	case CS35L36_INT2_STATUS:
	case CS35L36_INT3_STATUS:
	case CS35L36_INT4_STATUS:
	case CS35L36_INT1_RAW_STATUS:
	case CS35L36_INT2_RAW_STATUS:
	case CS35L36_INT3_RAW_STATUS:
	case CS35L36_INT4_RAW_STATUS:
	case CS35L36_INT1_MASK:
	case CS35L36_INT2_MASK:
	case CS35L36_INT3_MASK:
	case CS35L36_INT4_MASK:
	case CS35L36_INT1_EDGE_LVL_CTRL:
	case CS35L36_INT3_EDGE_LVL_CTRL:
	case CS35L36_PAC_INT_STATUS:
	case CS35L36_PAC_INT_RAW_STATUS:
	case CS35L36_PAC_INT_FLUSH_CTRL:
		return true;
	default:
		if (reg >= CS35L36_PAC_PMEM_WORD0 &&
			reg <= CS35L36_PAC_PMEM_WORD1023)
			return true;
		else
			return false;
	}
}

static const DECLARE_TLV_DB_RANGE(dig_vol_tlv, 0, 912,
				  TLV_DB_MINMAX_ITEM(-10200, 1200));
static DECLARE_TLV_DB_SCALE(amp_gain_tlv, 0, 1, 1);

static const char * const cs35l36_pcm_sftramp_text[] =  {
	"Off", ".5ms", "1ms", "2ms", "4ms", "8ms", "15ms", "30ms"};

static SOC_ENUM_SINGLE_DECL(pcm_sft_ramp, CS35L36_AMP_DIG_VOL_CTRL, 0,
			    cs35l36_pcm_sftramp_text);

static int cs35l36_ldm_sel_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct cs35l36_private *cs35l36 =
			snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = cs35l36->ldm_mode_sel;

	return 0;
}

static int cs35l36_ldm_sel_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct cs35l36_private *cs35l36 =
			snd_soc_component_get_drvdata(component);
	int val = (ucontrol->value.integer.value[0]) ? CS35L36_NG_AMP_EN_MASK :
						       0;

	cs35l36->ldm_mode_sel = val;

	regmap_update_bits(cs35l36->regmap, CS35L36_NG_CFG,
			   CS35L36_NG_AMP_EN_MASK, val);

	return 0;
}

static const struct snd_kcontrol_new cs35l36_aud_controls[] = {
	SOC_SINGLE_SX_TLV("Digital PCM Volume", CS35L36_AMP_DIG_VOL_CTRL,
		3, 0x4D0, 0x390, dig_vol_tlv),
	SOC_SINGLE_TLV("Analog PCM Volume", CS35L36_AMP_GAIN_CTRL, 5, 0x13, 0,
		amp_gain_tlv),
	SOC_ENUM("PCM Soft Ramp", pcm_sft_ramp),
	SOC_SINGLE("Amp Gain Zero-Cross Switch", CS35L36_AMP_GAIN_CTRL,
		CS35L36_AMP_ZC_SHIFT, 1, 0),
	SOC_SINGLE("PDM LDM Enter Ramp Switch", CS35L36_DAC_MSM_CFG,
		CS35L36_PDM_LDM_ENTER_SHIFT, 1, 0),
	SOC_SINGLE("PDM LDM Exit Ramp Switch", CS35L36_DAC_MSM_CFG,
		CS35L36_PDM_LDM_EXIT_SHIFT, 1, 0),
	SOC_SINGLE_BOOL_EXT("LDM Select Switch", 0, cs35l36_ldm_sel_get,
		cs35l36_ldm_sel_put),
};

static int cs35l36_main_amp_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
			snd_soc_dapm_to_component(w->dapm);
	struct cs35l36_private *cs35l36 =
			snd_soc_component_get_drvdata(component);
	u32 reg;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(cs35l36->regmap, CS35L36_PWR_CTRL1,
				   CS35L36_GLOBAL_EN_MASK,
				   1 << CS35L36_GLOBAL_EN_SHIFT);

		usleep_range(2000, 2100);

		regmap_read(cs35l36->regmap, CS35L36_INT4_RAW_STATUS, &reg);

		if (WARN_ON_ONCE(reg & CS35L36_PLL_UNLOCK_MASK))
			dev_crit(cs35l36->dev, "PLL Unlocked\n");

		regmap_update_bits(cs35l36->regmap, CS35L36_ASP_RX1_SEL,
				   CS35L36_PCM_RX_SEL_MASK,
				   CS35L36_PCM_RX_SEL_PCM);
		regmap_update_bits(cs35l36->regmap, CS35L36_AMP_OUT_MUTE,
				   CS35L36_AMP_MUTE_MASK,
				   0 << CS35L36_AMP_MUTE_SHIFT);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_update_bits(cs35l36->regmap, CS35L36_ASP_RX1_SEL,
				   CS35L36_PCM_RX_SEL_MASK,
				   CS35L36_PCM_RX_SEL_ZERO);
		regmap_update_bits(cs35l36->regmap, CS35L36_AMP_OUT_MUTE,
				   CS35L36_AMP_MUTE_MASK,
				   1 << CS35L36_AMP_MUTE_SHIFT);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(cs35l36->regmap, CS35L36_PWR_CTRL1,
				   CS35L36_GLOBAL_EN_MASK,
				   0 << CS35L36_GLOBAL_EN_SHIFT);

		usleep_range(2000, 2100);
		break;
	default:
		dev_dbg(component->dev, "Invalid event = 0x%x\n", event);
		return -EINVAL;
	}

	return 0;
}

static int cs35l36_boost_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
			snd_soc_dapm_to_component(w->dapm);
	struct cs35l36_private *cs35l36 =
			snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (!cs35l36->pdata.extern_boost)
			regmap_update_bits(cs35l36->regmap, CS35L36_PWR_CTRL2,
					   CS35L36_BST_EN_MASK,
					   CS35L36_BST_EN <<
					   CS35L36_BST_EN_SHIFT);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (!cs35l36->pdata.extern_boost)
			regmap_update_bits(cs35l36->regmap, CS35L36_PWR_CTRL2,
					   CS35L36_BST_EN_MASK,
					   CS35L36_BST_DIS_VP <<
					   CS35L36_BST_EN_SHIFT);
		break;
	default:
		dev_dbg(component->dev, "Invalid event = 0x%x\n", event);
		return -EINVAL;
	}

	return 0;
}

static const char * const cs35l36_chan_text[] = {
	"RX1",
	"RX2",
};

static SOC_ENUM_SINGLE_DECL(chansel_enum, CS35L36_ASP_RX1_SLOT, 0,
			    cs35l36_chan_text);

static const struct snd_kcontrol_new cs35l36_chan_mux =
		SOC_DAPM_ENUM("Input Mux", chansel_enum);

static const struct snd_kcontrol_new amp_enable_ctrl =
		SOC_DAPM_SINGLE_AUTODISABLE("Switch", CS35L36_AMP_OUT_MUTE,
					    CS35L36_AMP_MUTE_SHIFT, 1, 1);

static const struct snd_kcontrol_new boost_ctrl =
		SOC_DAPM_SINGLE_VIRT("Switch", 1);

static const char * const asp_tx_src_text[] = {
	"Zero Fill", "ASPRX1", "VMON", "IMON", "ERRVOL", "VPMON", "VBSTMON"
};

static const unsigned int asp_tx_src_values[] = {
	0x00, 0x08, 0x18, 0x19, 0x20, 0x28, 0x29
};

static SOC_VALUE_ENUM_SINGLE_DECL(asp_tx1_src_enum, CS35L36_ASP_TX1_SEL, 0,
				  CS35L36_APS_TX_SEL_MASK, asp_tx_src_text,
				  asp_tx_src_values);

static const struct snd_kcontrol_new asp_tx1_src =
		SOC_DAPM_ENUM("ASPTX1SRC", asp_tx1_src_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(asp_tx2_src_enum, CS35L36_ASP_TX2_SEL, 0,
				  CS35L36_APS_TX_SEL_MASK, asp_tx_src_text,
				  asp_tx_src_values);

static const struct snd_kcontrol_new asp_tx2_src =
		SOC_DAPM_ENUM("ASPTX2SRC", asp_tx2_src_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(asp_tx3_src_enum, CS35L36_ASP_TX3_SEL, 0,
				  CS35L36_APS_TX_SEL_MASK, asp_tx_src_text,
				  asp_tx_src_values);

static const struct snd_kcontrol_new asp_tx3_src =
		SOC_DAPM_ENUM("ASPTX3SRC", asp_tx3_src_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(asp_tx4_src_enum, CS35L36_ASP_TX4_SEL, 0,
				  CS35L36_APS_TX_SEL_MASK, asp_tx_src_text,
				  asp_tx_src_values);

static const struct snd_kcontrol_new asp_tx4_src =
		SOC_DAPM_ENUM("ASPTX4SRC", asp_tx4_src_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(asp_tx5_src_enum, CS35L36_ASP_TX5_SEL, 0,
				  CS35L36_APS_TX_SEL_MASK, asp_tx_src_text,
				  asp_tx_src_values);

static const struct snd_kcontrol_new asp_tx5_src =
		SOC_DAPM_ENUM("ASPTX5SRC", asp_tx5_src_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(asp_tx6_src_enum, CS35L36_ASP_TX6_SEL, 0,
				  CS35L36_APS_TX_SEL_MASK, asp_tx_src_text,
				  asp_tx_src_values);

static const struct snd_kcontrol_new asp_tx6_src =
		SOC_DAPM_ENUM("ASPTX6SRC", asp_tx6_src_enum);

static const struct snd_soc_dapm_widget cs35l36_dapm_widgets[] = {
	SND_SOC_DAPM_MUX("Channel Mux", SND_SOC_NOPM, 0, 0, &cs35l36_chan_mux),
	SND_SOC_DAPM_AIF_IN("SDIN", NULL, 0, CS35L36_ASP_RX_TX_EN, 16, 0),

	SND_SOC_DAPM_OUT_DRV_E("Main AMP", CS35L36_PWR_CTRL2, 0, 0, NULL, 0,
			       cs35l36_main_amp_event, SND_SOC_DAPM_POST_PMD |
			       SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_OUTPUT("SPK"),
	SND_SOC_DAPM_SWITCH("AMP Enable", SND_SOC_NOPM, 0, 1, &amp_enable_ctrl),
	SND_SOC_DAPM_MIXER("CLASS H", CS35L36_PWR_CTRL3, 4, 0, NULL, 0),
	SND_SOC_DAPM_SWITCH_E("BOOST Enable", SND_SOC_NOPM, 0, 0, &boost_ctrl,
			      cs35l36_boost_event, SND_SOC_DAPM_POST_PMD |
			      SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_AIF_OUT("ASPTX1", NULL, 0, CS35L36_ASP_RX_TX_EN, 0, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX2", NULL, 1, CS35L36_ASP_RX_TX_EN, 1, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX3", NULL, 2, CS35L36_ASP_RX_TX_EN, 2, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX4", NULL, 3, CS35L36_ASP_RX_TX_EN, 3, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX5", NULL, 4, CS35L36_ASP_RX_TX_EN, 4, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX6", NULL, 5, CS35L36_ASP_RX_TX_EN, 5, 0),

	SND_SOC_DAPM_MUX("ASPTX1SRC", SND_SOC_NOPM, 0, 0, &asp_tx1_src),
	SND_SOC_DAPM_MUX("ASPTX2SRC", SND_SOC_NOPM, 0, 0, &asp_tx2_src),
	SND_SOC_DAPM_MUX("ASPTX3SRC", SND_SOC_NOPM, 0, 0, &asp_tx3_src),
	SND_SOC_DAPM_MUX("ASPTX4SRC", SND_SOC_NOPM, 0, 0, &asp_tx4_src),
	SND_SOC_DAPM_MUX("ASPTX5SRC", SND_SOC_NOPM, 0, 0, &asp_tx5_src),
	SND_SOC_DAPM_MUX("ASPTX6SRC", SND_SOC_NOPM, 0, 0, &asp_tx6_src),

	SND_SOC_DAPM_ADC("VMON ADC", NULL, CS35L36_PWR_CTRL2, 12, 0),
	SND_SOC_DAPM_ADC("IMON ADC", NULL, CS35L36_PWR_CTRL2, 13, 0),
	SND_SOC_DAPM_ADC("VPMON ADC", NULL, CS35L36_PWR_CTRL2, 8, 0),
	SND_SOC_DAPM_ADC("VBSTMON ADC", NULL, CS35L36_PWR_CTRL2, 9, 0),

	SND_SOC_DAPM_INPUT("VP"),
	SND_SOC_DAPM_INPUT("VBST"),
	SND_SOC_DAPM_INPUT("VSENSE"),
};

static const struct snd_soc_dapm_route cs35l36_audio_map[] = {
	{"VPMON ADC", NULL, "VP"},
	{"VBSTMON ADC", NULL, "VBST"},
	{"IMON ADC", NULL, "VSENSE"},
	{"VMON ADC", NULL, "VSENSE"},

	{"ASPTX1SRC", "IMON", "IMON ADC"},
	{"ASPTX1SRC", "VMON", "VMON ADC"},
	{"ASPTX1SRC", "VBSTMON", "VBSTMON ADC"},
	{"ASPTX1SRC", "VPMON", "VPMON ADC"},

	{"ASPTX2SRC", "IMON", "IMON ADC"},
	{"ASPTX2SRC", "VMON", "VMON ADC"},
	{"ASPTX2SRC", "VBSTMON", "VBSTMON ADC"},
	{"ASPTX2SRC", "VPMON", "VPMON ADC"},

	{"ASPTX3SRC", "IMON", "IMON ADC"},
	{"ASPTX3SRC", "VMON", "VMON ADC"},
	{"ASPTX3SRC", "VBSTMON", "VBSTMON ADC"},
	{"ASPTX3SRC", "VPMON", "VPMON ADC"},

	{"ASPTX4SRC", "IMON", "IMON ADC"},
	{"ASPTX4SRC", "VMON", "VMON ADC"},
	{"ASPTX4SRC", "VBSTMON", "VBSTMON ADC"},
	{"ASPTX4SRC", "VPMON", "VPMON ADC"},

	{"ASPTX5SRC", "IMON", "IMON ADC"},
	{"ASPTX5SRC", "VMON", "VMON ADC"},
	{"ASPTX5SRC", "VBSTMON", "VBSTMON ADC"},
	{"ASPTX5SRC", "VPMON", "VPMON ADC"},

	{"ASPTX6SRC", "IMON", "IMON ADC"},
	{"ASPTX6SRC", "VMON", "VMON ADC"},
	{"ASPTX6SRC", "VBSTMON", "VBSTMON ADC"},
	{"ASPTX6SRC", "VPMON", "VPMON ADC"},

	{"ASPTX1", NULL, "ASPTX1SRC"},
	{"ASPTX2", NULL, "ASPTX2SRC"},
	{"ASPTX3", NULL, "ASPTX3SRC"},
	{"ASPTX4", NULL, "ASPTX4SRC"},
	{"ASPTX5", NULL, "ASPTX5SRC"},
	{"ASPTX6", NULL, "ASPTX6SRC"},

	{"AMP Capture", NULL, "ASPTX1"},
	{"AMP Capture", NULL, "ASPTX2"},
	{"AMP Capture", NULL, "ASPTX3"},
	{"AMP Capture", NULL, "ASPTX4"},
	{"AMP Capture", NULL, "ASPTX5"},
	{"AMP Capture", NULL, "ASPTX6"},

	{"AMP Enable", "Switch", "AMP Playback"},
	{"SDIN", NULL, "AMP Enable"},
	{"Channel Mux", "RX1", "SDIN"},
	{"Channel Mux", "RX2", "SDIN"},
	{"BOOST Enable", "Switch", "Channel Mux"},
	{"CLASS H", NULL, "BOOST Enable"},
	{"Main AMP", NULL, "Channel Mux"},
	{"Main AMP", NULL, "CLASS H"},
	{"SPK", NULL, "Main AMP"},
};

static int cs35l36_set_dai_fmt(struct snd_soc_dai *component_dai,
			       unsigned int fmt)
{
	struct cs35l36_private *cs35l36 =
			snd_soc_component_get_drvdata(component_dai->component);
	unsigned int asp_fmt, lrclk_fmt, sclk_fmt, clock_provider, clk_frc;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		clock_provider = 1;
		break;
	case SND_SOC_DAIFMT_CBC_CFC:
		clock_provider = 0;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(cs35l36->regmap, CS35L36_ASP_TX_PIN_CTRL,
				CS35L36_SCLK_MSTR_MASK,
				clock_provider << CS35L36_SCLK_MSTR_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_ASP_RATE_CTRL,
				CS35L36_LRCLK_MSTR_MASK,
				clock_provider << CS35L36_LRCLK_MSTR_SHIFT);

	switch (fmt & SND_SOC_DAIFMT_CLOCK_MASK) {
	case SND_SOC_DAIFMT_CONT:
		clk_frc = 1;
		break;
	case SND_SOC_DAIFMT_GATED:
		clk_frc = 0;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(cs35l36->regmap, CS35L36_ASP_TX_PIN_CTRL,
			   CS35L36_SCLK_FRC_MASK, clk_frc <<
			   CS35L36_SCLK_FRC_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_ASP_RATE_CTRL,
			   CS35L36_LRCLK_FRC_MASK, clk_frc <<
			   CS35L36_LRCLK_FRC_SHIFT);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		asp_fmt = 0;
		break;
	case SND_SOC_DAIFMT_I2S:
		asp_fmt = 2;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
		lrclk_fmt = 1;
		sclk_fmt = 0;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		lrclk_fmt = 0;
		sclk_fmt = 1;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		lrclk_fmt = 1;
		sclk_fmt = 1;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		lrclk_fmt = 0;
		sclk_fmt = 0;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(cs35l36->regmap, CS35L36_ASP_RATE_CTRL,
			   CS35L36_LRCLK_INV_MASK,
			   lrclk_fmt << CS35L36_LRCLK_INV_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_ASP_TX_PIN_CTRL,
			   CS35L36_SCLK_INV_MASK,
			   sclk_fmt << CS35L36_SCLK_INV_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_ASP_FORMAT,
			   CS35L36_ASP_FMT_MASK, asp_fmt);

	return 0;
}

struct cs35l36_global_fs_config {
	int rate;
	int fs_cfg;
};

static const struct cs35l36_global_fs_config cs35l36_fs_rates[] = {
	{12000, 0x01},
	{24000, 0x02},
	{48000, 0x03},
	{96000, 0x04},
	{192000, 0x05},
	{384000, 0x06},
	{11025, 0x09},
	{22050, 0x0A},
	{44100, 0x0B},
	{88200, 0x0C},
	{176400, 0x0D},
	{8000, 0x11},
	{16000, 0x12},
	{32000, 0x13},
};

static int cs35l36_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct cs35l36_private *cs35l36 =
			snd_soc_component_get_drvdata(dai->component);
	unsigned int asp_width, global_fs = params_rate(params);
	int i;

	for (i = 0; i < ARRAY_SIZE(cs35l36_fs_rates); i++) {
		if (global_fs == cs35l36_fs_rates[i].rate)
			regmap_update_bits(cs35l36->regmap,
					   CS35L36_GLOBAL_CLK_CTRL,
					   CS35L36_GLOBAL_FS_MASK,
					   cs35l36_fs_rates[i].fs_cfg <<
					   CS35L36_GLOBAL_FS_SHIFT);
	}

	switch (params_width(params)) {
	case 16:
		asp_width = CS35L36_ASP_WIDTH_16;
		break;
	case 24:
		asp_width = CS35L36_ASP_WIDTH_24;
		break;
	case 32:
		asp_width = CS35L36_ASP_WIDTH_32;
		break;
	default:
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_update_bits(cs35l36->regmap, CS35L36_ASP_FRAME_CTRL,
				   CS35L36_ASP_RX_WIDTH_MASK,
				   asp_width << CS35L36_ASP_RX_WIDTH_SHIFT);
	} else {
		regmap_update_bits(cs35l36->regmap, CS35L36_ASP_FRAME_CTRL,
				   CS35L36_ASP_TX_WIDTH_MASK,
				   asp_width << CS35L36_ASP_TX_WIDTH_SHIFT);
	}

	return 0;
}

static int cs35l36_dai_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				  unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct cs35l36_private *cs35l36 =
			snd_soc_component_get_drvdata(component);
	int fs1, fs2;

	if (freq > CS35L36_FS_NOM_6MHZ) {
		fs1 = CS35L36_FS1_DEFAULT_VAL;
		fs2 = CS35L36_FS2_DEFAULT_VAL;
	} else {
		fs1 = 3 * ((CS35L36_FS_NOM_6MHZ * 4 + freq - 1) / freq) + 4;
		fs2 = 5 * ((CS35L36_FS_NOM_6MHZ * 4 + freq - 1) / freq) + 4;
	}

	regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
			CS35L36_TEST_UNLOCK1);
	regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
			CS35L36_TEST_UNLOCK2);

	regmap_update_bits(cs35l36->regmap, CS35L36_TST_FS_MON0,
			   CS35L36_FS1_WINDOW_MASK | CS35L36_FS2_WINDOW_MASK,
			   fs1 | (fs2 << CS35L36_FS2_WINDOW_SHIFT));

	regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
			CS35L36_TEST_LOCK1);
	regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
			CS35L36_TEST_LOCK2);
	return 0;
}

static const struct cs35l36_pll_config *cs35l36_get_clk_config(
		struct cs35l36_private *cs35l36, int freq)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs35l36_pll_sysclk); i++) {
		if (cs35l36_pll_sysclk[i].freq == freq)
			return &cs35l36_pll_sysclk[i];
	}

	return NULL;
}

static const unsigned int cs35l36_src_rates[] = {
	8000, 12000, 11025, 16000, 22050, 24000, 32000,
	44100, 48000, 88200, 96000, 176400, 192000, 384000
};

static const struct snd_pcm_hw_constraint_list cs35l36_constraints = {
	.count  = ARRAY_SIZE(cs35l36_src_rates),
	.list   = cs35l36_src_rates,
};

static int cs35l36_pcm_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	snd_pcm_hw_constraint_list(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE, &cs35l36_constraints);

	return 0;
}

static const struct snd_soc_dai_ops cs35l36_ops = {
	.startup = cs35l36_pcm_startup,
	.set_fmt = cs35l36_set_dai_fmt,
	.hw_params = cs35l36_pcm_hw_params,
	.set_sysclk = cs35l36_dai_set_sysclk,
};

static struct snd_soc_dai_driver cs35l36_dai[] = {
	{
		.name = "cs35l36-pcm",
		.id = 0,
		.playback = {
			.stream_name = "AMP Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = CS35L36_RX_FORMATS,
		},
		.capture = {
			.stream_name = "AMP Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = CS35L36_TX_FORMATS,
		},
		.ops = &cs35l36_ops,
		.symmetric_rate = 1,
	},
};

static int cs35l36_component_set_sysclk(struct snd_soc_component *component,
				int clk_id, int source, unsigned int freq,
				int dir)
{
	struct cs35l36_private *cs35l36 =
			snd_soc_component_get_drvdata(component);
	const struct cs35l36_pll_config *clk_cfg;
	int prev_clksrc;
	bool pdm_switch;

	prev_clksrc = cs35l36->clksrc;

	switch (clk_id) {
	case 0:
		cs35l36->clksrc = CS35L36_PLLSRC_SCLK;
		break;
	case 1:
		cs35l36->clksrc = CS35L36_PLLSRC_LRCLK;
		break;
	case 2:
		cs35l36->clksrc = CS35L36_PLLSRC_PDMCLK;
		break;
	case 3:
		cs35l36->clksrc = CS35L36_PLLSRC_SELF;
		break;
	case 4:
		cs35l36->clksrc = CS35L36_PLLSRC_MCLK;
		break;
	default:
		return -EINVAL;
	}

	clk_cfg = cs35l36_get_clk_config(cs35l36, freq);
	if (clk_cfg == NULL) {
		dev_err(component->dev, "Invalid CLK Config Freq: %d\n", freq);
		return -EINVAL;
	}

	regmap_update_bits(cs35l36->regmap, CS35L36_PLL_CLK_CTRL,
			   CS35L36_PLL_OPENLOOP_MASK,
			   1 << CS35L36_PLL_OPENLOOP_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_PLL_CLK_CTRL,
			   CS35L36_REFCLK_FREQ_MASK,
			   clk_cfg->clk_cfg << CS35L36_REFCLK_FREQ_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_PLL_CLK_CTRL,
			   CS35L36_PLL_REFCLK_EN_MASK,
			   0 << CS35L36_PLL_REFCLK_EN_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_PLL_CLK_CTRL,
			   CS35L36_PLL_CLK_SEL_MASK,
			   cs35l36->clksrc);
	regmap_update_bits(cs35l36->regmap, CS35L36_PLL_CLK_CTRL,
			   CS35L36_PLL_OPENLOOP_MASK,
			   0 << CS35L36_PLL_OPENLOOP_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_PLL_CLK_CTRL,
			   CS35L36_PLL_REFCLK_EN_MASK,
			   1 << CS35L36_PLL_REFCLK_EN_SHIFT);

	if (cs35l36->rev_id == CS35L36_REV_A0) {
		regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
			     CS35L36_TEST_UNLOCK1);
		regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
			     CS35L36_TEST_UNLOCK2);

		regmap_write(cs35l36->regmap, CS35L36_DCO_CTRL, 0x00036DA8);
		regmap_write(cs35l36->regmap, CS35L36_MISC_CTRL, 0x0100EE0E);

		regmap_update_bits(cs35l36->regmap, CS35L36_PLL_LOOP_PARAMS,
				   CS35L36_PLL_IGAIN_MASK,
				   CS35L36_PLL_IGAIN <<
				   CS35L36_PLL_IGAIN_SHIFT);
		regmap_update_bits(cs35l36->regmap, CS35L36_PLL_LOOP_PARAMS,
				   CS35L36_PLL_FFL_IGAIN_MASK,
				   clk_cfg->fll_igain);

		regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
			     CS35L36_TEST_LOCK1);
		regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
			     CS35L36_TEST_LOCK2);
	}

	if (cs35l36->clksrc == CS35L36_PLLSRC_PDMCLK) {
		pdm_switch = cs35l36->ldm_mode_sel &&
			     (prev_clksrc != CS35L36_PLLSRC_PDMCLK);

		if (pdm_switch)
			regmap_update_bits(cs35l36->regmap, CS35L36_NG_CFG,
					   CS35L36_NG_DELAY_MASK,
					   0 << CS35L36_NG_DELAY_SHIFT);

		regmap_update_bits(cs35l36->regmap, CS35L36_DAC_MSM_CFG,
				   CS35L36_PDM_MODE_MASK,
				   1 << CS35L36_PDM_MODE_SHIFT);

		if (pdm_switch)
			regmap_update_bits(cs35l36->regmap, CS35L36_NG_CFG,
					   CS35L36_NG_DELAY_MASK,
					   3 << CS35L36_NG_DELAY_SHIFT);
	} else {
		pdm_switch = cs35l36->ldm_mode_sel &&
			     (prev_clksrc == CS35L36_PLLSRC_PDMCLK);

		if (pdm_switch)
			regmap_update_bits(cs35l36->regmap, CS35L36_NG_CFG,
					   CS35L36_NG_DELAY_MASK,
					   0 << CS35L36_NG_DELAY_SHIFT);

		regmap_update_bits(cs35l36->regmap, CS35L36_DAC_MSM_CFG,
				   CS35L36_PDM_MODE_MASK,
				   0 << CS35L36_PDM_MODE_SHIFT);

		if (pdm_switch)
			regmap_update_bits(cs35l36->regmap, CS35L36_NG_CFG,
					   CS35L36_NG_DELAY_MASK,
					   3 << CS35L36_NG_DELAY_SHIFT);
	}

	return 0;
}

static int cs35l36_boost_inductor(struct cs35l36_private *cs35l36, int inductor)
{
	regmap_update_bits(cs35l36->regmap, CS35L36_BSTCVRT_COEFF,
			   CS35L36_BSTCVRT_K1_MASK, 0x3C);
	regmap_update_bits(cs35l36->regmap, CS35L36_BSTCVRT_COEFF,
			   CS35L36_BSTCVRT_K2_MASK,
			   0x3C << CS35L36_BSTCVRT_K2_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_BSTCVRT_SW_FREQ,
			   CS35L36_BSTCVRT_CCMFREQ_MASK, 0x00);

	switch (inductor) {
	case 1000: /* 1 uH */
		regmap_update_bits(cs35l36->regmap, CS35L36_BSTCVRT_SLOPE_LBST,
				   CS35L36_BSTCVRT_SLOPE_MASK,
				   0x75 << CS35L36_BSTCVRT_SLOPE_SHIFT);
		regmap_update_bits(cs35l36->regmap, CS35L36_BSTCVRT_SLOPE_LBST,
				   CS35L36_BSTCVRT_LBSTVAL_MASK, 0x00);
		break;
	case 1200: /* 1.2 uH */
		regmap_update_bits(cs35l36->regmap, CS35L36_BSTCVRT_SLOPE_LBST,
				   CS35L36_BSTCVRT_SLOPE_MASK,
				   0x6B << CS35L36_BSTCVRT_SLOPE_SHIFT);
		regmap_update_bits(cs35l36->regmap, CS35L36_BSTCVRT_SLOPE_LBST,
				   CS35L36_BSTCVRT_LBSTVAL_MASK, 0x01);
		break;
	default:
		dev_err(cs35l36->dev, "%s Invalid Inductor Value %d uH\n",
			__func__, inductor);
		return -EINVAL;
	}

	return 0;
}

static int cs35l36_component_probe(struct snd_soc_component *component)
{
	struct cs35l36_private *cs35l36 =
			snd_soc_component_get_drvdata(component);
	int ret;

	if ((cs35l36->rev_id == CS35L36_REV_A0) && cs35l36->pdata.dcm_mode) {
		regmap_update_bits(cs35l36->regmap, CS35L36_BSTCVRT_DCM_CTRL,
				   CS35L36_DCM_AUTO_MASK,
				   CS35L36_DCM_AUTO_MASK);

		regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
			     CS35L36_TEST_UNLOCK1);
		regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
			     CS35L36_TEST_UNLOCK2);

		regmap_update_bits(cs35l36->regmap, CS35L36_BST_TST_MANUAL,
				   CS35L36_BST_MAN_IPKCOMP_MASK,
				   0 << CS35L36_BST_MAN_IPKCOMP_SHIFT);
		regmap_update_bits(cs35l36->regmap, CS35L36_BST_TST_MANUAL,
				   CS35L36_BST_MAN_IPKCOMP_EN_MASK,
				   CS35L36_BST_MAN_IPKCOMP_EN_MASK);

		regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
				CS35L36_TEST_LOCK1);
		regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
				CS35L36_TEST_LOCK2);
	}

	if (cs35l36->pdata.amp_pcm_inv)
		regmap_update_bits(cs35l36->regmap, CS35L36_AMP_DIG_VOL_CTRL,
				   CS35L36_AMP_PCM_INV_MASK,
				   CS35L36_AMP_PCM_INV_MASK);

	if (cs35l36->pdata.multi_amp_mode)
		regmap_update_bits(cs35l36->regmap, CS35L36_ASP_TX_PIN_CTRL,
				   CS35L36_ASP_TX_HIZ_MASK,
				   CS35L36_ASP_TX_HIZ_MASK);

	if (cs35l36->pdata.imon_pol_inv)
		regmap_update_bits(cs35l36->regmap, CS35L36_VI_SPKMON_FILT,
				   CS35L36_IMON_POL_MASK, 0);

	if (cs35l36->pdata.vmon_pol_inv)
		regmap_update_bits(cs35l36->regmap, CS35L36_VI_SPKMON_FILT,
				   CS35L36_VMON_POL_MASK, 0);

	if (cs35l36->pdata.bst_vctl)
		regmap_update_bits(cs35l36->regmap, CS35L36_BSTCVRT_VCTRL1,
				   CS35L35_BSTCVRT_CTL_MASK,
				   cs35l36->pdata.bst_vctl);

	if (cs35l36->pdata.bst_vctl_sel)
		regmap_update_bits(cs35l36->regmap, CS35L36_BSTCVRT_VCTRL2,
				   CS35L35_BSTCVRT_CTL_SEL_MASK,
				   cs35l36->pdata.bst_vctl_sel);

	if (cs35l36->pdata.bst_ipk)
		regmap_update_bits(cs35l36->regmap, CS35L36_BSTCVRT_PEAK_CUR,
				   CS35L36_BST_IPK_MASK,
				   cs35l36->pdata.bst_ipk);

	if (cs35l36->pdata.boost_ind) {
		ret = cs35l36_boost_inductor(cs35l36, cs35l36->pdata.boost_ind);
		if (ret < 0) {
			dev_err(cs35l36->dev,
				"Boost inductor config failed(%d)\n", ret);
			return ret;
		}
	}

	if (cs35l36->pdata.temp_warn_thld)
		regmap_update_bits(cs35l36->regmap, CS35L36_DTEMP_WARN_THLD,
				   CS35L36_TEMP_THLD_MASK,
				   cs35l36->pdata.temp_warn_thld);

	if (cs35l36->pdata.irq_drv_sel)
		regmap_update_bits(cs35l36->regmap, CS35L36_PAD_INTERFACE,
				   CS35L36_INT_DRV_SEL_MASK,
				   cs35l36->pdata.irq_drv_sel <<
				   CS35L36_INT_DRV_SEL_SHIFT);

	if (cs35l36->pdata.irq_gpio_sel)
		regmap_update_bits(cs35l36->regmap, CS35L36_PAD_INTERFACE,
				   CS35L36_INT_GPIO_SEL_MASK,
				   cs35l36->pdata.irq_gpio_sel <<
				   CS35L36_INT_GPIO_SEL_SHIFT);

	/*
	 * Rev B0 has 2 versions
	 * L36 is 10V
	 * L37 is 12V
	 * If L36 we need to clamp some values for safety
	 * after probe has setup dt values. We want to make
	 * sure we dont miss any values set in probe
	 */
	if (cs35l36->chip_version == CS35L36_10V_L36) {
		regmap_update_bits(cs35l36->regmap,
				   CS35L36_BSTCVRT_OVERVOLT_CTRL,
				   CS35L36_BST_OVP_THLD_MASK,
				   CS35L36_BST_OVP_THLD_11V);

		regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
			     CS35L36_TEST_UNLOCK1);
		regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
			     CS35L36_TEST_UNLOCK2);

		regmap_update_bits(cs35l36->regmap, CS35L36_BST_ANA2_TEST,
				   CS35L36_BST_OVP_TRIM_MASK,
				   CS35L36_BST_OVP_TRIM_11V <<
				   CS35L36_BST_OVP_TRIM_SHIFT);
		regmap_update_bits(cs35l36->regmap, CS35L36_BSTCVRT_VCTRL2,
				   CS35L36_BST_CTRL_LIM_MASK,
				   1 << CS35L36_BST_CTRL_LIM_SHIFT);
		regmap_update_bits(cs35l36->regmap, CS35L36_BSTCVRT_VCTRL1,
				   CS35L35_BSTCVRT_CTL_MASK,
				   CS35L36_BST_CTRL_10V_CLAMP);
		regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
			     CS35L36_TEST_LOCK1);
		regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
			     CS35L36_TEST_LOCK2);
	}

	/*
	 * RevA and B require the disabling of
	 * SYNC_GLOBAL_OVR when GLOBAL_EN = 0.
	 * Just turn it off from default
	 */
	regmap_update_bits(cs35l36->regmap, CS35L36_CTRL_OVRRIDE,
			   CS35L36_SYNC_GLOBAL_OVR_MASK,
			   0 << CS35L36_SYNC_GLOBAL_OVR_SHIFT);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_cs35l36 = {
	.probe			= &cs35l36_component_probe,
	.set_sysclk		= cs35l36_component_set_sysclk,
	.dapm_widgets		= cs35l36_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs35l36_dapm_widgets),
	.dapm_routes		= cs35l36_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(cs35l36_audio_map),
	.controls		= cs35l36_aud_controls,
	.num_controls		= ARRAY_SIZE(cs35l36_aud_controls),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static struct regmap_config cs35l36_regmap = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = CS35L36_PAC_PMEM_WORD1023,
	.reg_defaults = cs35l36_reg,
	.num_reg_defaults = ARRAY_SIZE(cs35l36_reg),
	.precious_reg = cs35l36_precious_reg,
	.volatile_reg = cs35l36_volatile_reg,
	.readable_reg = cs35l36_readable_reg,
	.cache_type = REGCACHE_RBTREE,
};

static irqreturn_t cs35l36_irq(int irq, void *data)
{
	struct cs35l36_private *cs35l36 = data;
	unsigned int status[4];
	unsigned int masks[4];
	int ret = IRQ_NONE;

	/* ack the irq by reading all status registers */
	regmap_bulk_read(cs35l36->regmap, CS35L36_INT1_STATUS, status,
			 ARRAY_SIZE(status));

	regmap_bulk_read(cs35l36->regmap, CS35L36_INT1_MASK, masks,
			 ARRAY_SIZE(masks));

	/* Check to see if unmasked bits are active */
	if (!(status[0] & ~masks[0]) && !(status[1] & ~masks[1]) &&
		!(status[2] & ~masks[2]) && !(status[3] & ~masks[3])) {
		return IRQ_NONE;
	}

	/*
	 * The following interrupts require a
	 * protection release cycle to get the
	 * speaker out of Safe-Mode.
	 */
	if (status[2] & CS35L36_AMP_SHORT_ERR) {
		dev_crit(cs35l36->dev, "Amp short error\n");
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_AMP_SHORT_ERR_RLS, 0);
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_AMP_SHORT_ERR_RLS,
				   CS35L36_AMP_SHORT_ERR_RLS);
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_AMP_SHORT_ERR_RLS, 0);
		regmap_update_bits(cs35l36->regmap, CS35L36_INT3_STATUS,
				   CS35L36_AMP_SHORT_ERR,
				   CS35L36_AMP_SHORT_ERR);
		ret = IRQ_HANDLED;
	}

	if (status[0] & CS35L36_TEMP_WARN) {
		dev_crit(cs35l36->dev, "Over temperature warning\n");
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_TEMP_WARN_ERR_RLS, 0);
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_TEMP_WARN_ERR_RLS,
				   CS35L36_TEMP_WARN_ERR_RLS);
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_TEMP_WARN_ERR_RLS, 0);
		regmap_update_bits(cs35l36->regmap, CS35L36_INT1_STATUS,
				   CS35L36_TEMP_WARN, CS35L36_TEMP_WARN);
		ret = IRQ_HANDLED;
	}

	if (status[0] & CS35L36_TEMP_ERR) {
		dev_crit(cs35l36->dev, "Over temperature error\n");
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_TEMP_ERR_RLS, 0);
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_TEMP_ERR_RLS, CS35L36_TEMP_ERR_RLS);
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_TEMP_ERR_RLS, 0);
		regmap_update_bits(cs35l36->regmap, CS35L36_INT1_STATUS,
				   CS35L36_TEMP_ERR, CS35L36_TEMP_ERR);
		ret = IRQ_HANDLED;
	}

	if (status[0] & CS35L36_BST_OVP_ERR) {
		dev_crit(cs35l36->dev, "VBST Over Voltage error\n");
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_TEMP_ERR_RLS, 0);
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_TEMP_ERR_RLS, CS35L36_TEMP_ERR_RLS);
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_TEMP_ERR_RLS, 0);
		regmap_update_bits(cs35l36->regmap, CS35L36_INT1_STATUS,
				   CS35L36_BST_OVP_ERR, CS35L36_BST_OVP_ERR);
		ret = IRQ_HANDLED;
	}

	if (status[0] & CS35L36_BST_DCM_UVP_ERR) {
		dev_crit(cs35l36->dev, "DCM VBST Under Voltage Error\n");
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_BST_UVP_ERR_RLS, 0);
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_BST_UVP_ERR_RLS,
				   CS35L36_BST_UVP_ERR_RLS);
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_BST_UVP_ERR_RLS, 0);
		regmap_update_bits(cs35l36->regmap, CS35L36_INT1_STATUS,
				   CS35L36_BST_DCM_UVP_ERR,
				   CS35L36_BST_DCM_UVP_ERR);
		ret = IRQ_HANDLED;
	}

	if (status[0] & CS35L36_BST_SHORT_ERR) {
		dev_crit(cs35l36->dev, "LBST SHORT error!\n");
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_BST_SHORT_ERR_RLS, 0);
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_BST_SHORT_ERR_RLS,
				   CS35L36_BST_SHORT_ERR_RLS);
		regmap_update_bits(cs35l36->regmap, CS35L36_PROTECT_REL_ERR,
				   CS35L36_BST_SHORT_ERR_RLS, 0);
		regmap_update_bits(cs35l36->regmap, CS35L36_INT1_STATUS,
				   CS35L36_BST_SHORT_ERR,
				   CS35L36_BST_SHORT_ERR);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static int cs35l36_handle_of_data(struct i2c_client *i2c_client,
				struct cs35l36_platform_data *pdata)
{
	struct device_node *np = i2c_client->dev.of_node;
	struct cs35l36_vpbr_cfg *vpbr_config = &pdata->vpbr_config;
	struct device_node *vpbr_node;
	unsigned int val;
	int ret;

	if (!np)
		return 0;

	ret = of_property_read_u32(np, "cirrus,boost-ctl-millivolt", &val);
	if (!ret) {
		if (val < 2550 || val > 12000) {
			dev_err(&i2c_client->dev,
				"Invalid Boost Voltage %d mV\n", val);
			return -EINVAL;
		}
		pdata->bst_vctl = (((val - 2550) / 100) + 1) << 1;
	} else {
		dev_err(&i2c_client->dev,
			"Unable to find required parameter 'cirrus,boost-ctl-millivolt'");
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "cirrus,boost-ctl-select", &val);
	if (!ret)
		pdata->bst_vctl_sel = val | CS35L36_VALID_PDATA;

	ret = of_property_read_u32(np, "cirrus,boost-peak-milliamp", &val);
	if (!ret) {
		if (val < 1600 || val > 4500) {
			dev_err(&i2c_client->dev,
				"Invalid Boost Peak Current %u mA\n", val);
			return -EINVAL;
		}

		pdata->bst_ipk = (val - 1600) / 50;
	} else {
		dev_err(&i2c_client->dev,
			"Unable to find required parameter 'cirrus,boost-peak-milliamp'");
		return -EINVAL;
	}

	pdata->multi_amp_mode = of_property_read_bool(np,
					"cirrus,multi-amp-mode");

	pdata->dcm_mode = of_property_read_bool(np,
					"cirrus,dcm-mode-enable");

	pdata->amp_pcm_inv = of_property_read_bool(np,
					"cirrus,amp-pcm-inv");

	pdata->imon_pol_inv = of_property_read_bool(np,
					"cirrus,imon-pol-inv");

	pdata->vmon_pol_inv = of_property_read_bool(np,
					"cirrus,vmon-pol-inv");

	if (of_property_read_u32(np, "cirrus,temp-warn-threshold", &val) >= 0)
		pdata->temp_warn_thld = val | CS35L36_VALID_PDATA;

	if (of_property_read_u32(np, "cirrus,boost-ind-nanohenry", &val) >= 0) {
		pdata->boost_ind = val;
	} else {
		dev_err(&i2c_client->dev, "Inductor not specified.\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "cirrus,irq-drive-select", &val) >= 0)
		pdata->irq_drv_sel = val | CS35L36_VALID_PDATA;

	if (of_property_read_u32(np, "cirrus,irq-gpio-select", &val) >= 0)
		pdata->irq_gpio_sel = val | CS35L36_VALID_PDATA;

	/* VPBR Config */
	vpbr_node = of_get_child_by_name(np, "cirrus,vpbr-config");
	vpbr_config->is_present = vpbr_node ? true : false;
	if (vpbr_config->is_present) {
		if (of_property_read_u32(vpbr_node, "cirrus,vpbr-en",
					 &val) >= 0)
			vpbr_config->vpbr_en = val;
		if (of_property_read_u32(vpbr_node, "cirrus,vpbr-thld",
					 &val) >= 0)
			vpbr_config->vpbr_thld = val;
		if (of_property_read_u32(vpbr_node, "cirrus,vpbr-atk-rate",
					 &val) >= 0)
			vpbr_config->vpbr_atk_rate = val;
		if (of_property_read_u32(vpbr_node, "cirrus,vpbr-atk-vol",
					 &val) >= 0)
			vpbr_config->vpbr_atk_vol = val;
		if (of_property_read_u32(vpbr_node, "cirrus,vpbr-max-attn",
					 &val) >= 0)
			vpbr_config->vpbr_max_attn = val;
		if (of_property_read_u32(vpbr_node, "cirrus,vpbr-wait",
					 &val) >= 0)
			vpbr_config->vpbr_wait = val;
		if (of_property_read_u32(vpbr_node, "cirrus,vpbr-rel-rate",
					 &val) >= 0)
			vpbr_config->vpbr_rel_rate = val;
		if (of_property_read_u32(vpbr_node, "cirrus,vpbr-mute-en",
					 &val) >= 0)
			vpbr_config->vpbr_mute_en = val;
	}
	of_node_put(vpbr_node);

	return 0;
}

static int cs35l36_pac(struct cs35l36_private *cs35l36)
{
	int ret, count;
	unsigned int val;

	if (cs35l36->rev_id != CS35L36_REV_B0)
		return 0;

	/*
	 * Magic code for internal PAC
	 */
	regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
		     CS35L36_TEST_UNLOCK1);
	regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
		     CS35L36_TEST_UNLOCK2);

	usleep_range(9500, 10500);

	regmap_write(cs35l36->regmap, CS35L36_PAC_CTL1,
		     CS35L36_PAC_RESET);
	regmap_write(cs35l36->regmap, CS35L36_PAC_CTL3,
		     CS35L36_PAC_MEM_ACCESS);
	regmap_write(cs35l36->regmap, CS35L36_PAC_PMEM_WORD0,
		     CS35L36_B0_PAC_PATCH);

	regmap_write(cs35l36->regmap, CS35L36_PAC_CTL3,
		     CS35L36_PAC_MEM_ACCESS_CLR);
	regmap_write(cs35l36->regmap, CS35L36_PAC_CTL1,
		     CS35L36_PAC_ENABLE_MASK);

	usleep_range(9500, 10500);

	ret = regmap_read(cs35l36->regmap, CS35L36_INT4_STATUS, &val);
	if (ret < 0) {
		dev_err(cs35l36->dev, "Failed to read int4_status %d\n", ret);
		return ret;
	}

	count = 0;
	while (!(val & CS35L36_MCU_CONFIG_CLR)) {
		usleep_range(100, 200);
		count++;

		ret = regmap_read(cs35l36->regmap, CS35L36_INT4_STATUS,
				  &val);
		if (ret < 0) {
			dev_err(cs35l36->dev, "Failed to read int4_status %d\n",
				ret);
			return ret;
		}

		if (count >= 100)
			return -EINVAL;
	}

	regmap_write(cs35l36->regmap, CS35L36_INT4_STATUS,
		     CS35L36_MCU_CONFIG_CLR);
	regmap_update_bits(cs35l36->regmap, CS35L36_PAC_CTL1,
			   CS35L36_PAC_ENABLE_MASK, 0);

	regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
		     CS35L36_TEST_LOCK1);
	regmap_write(cs35l36->regmap, CS35L36_TESTKEY_CTRL,
		     CS35L36_TEST_LOCK2);

	return 0;
}

static void cs35l36_apply_vpbr_config(struct cs35l36_private *cs35l36)
{
	struct cs35l36_platform_data *pdata = &cs35l36->pdata;
	struct cs35l36_vpbr_cfg *vpbr_config = &pdata->vpbr_config;

	regmap_update_bits(cs35l36->regmap, CS35L36_PWR_CTRL3,
			   CS35L36_VPBR_EN_MASK,
			   vpbr_config->vpbr_en <<
			   CS35L36_VPBR_EN_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_VPBR_CFG,
			   CS35L36_VPBR_THLD_MASK,
			   vpbr_config->vpbr_thld <<
			   CS35L36_VPBR_THLD_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_VPBR_CFG,
			   CS35L36_VPBR_MAX_ATTN_MASK,
			   vpbr_config->vpbr_max_attn <<
			   CS35L36_VPBR_MAX_ATTN_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_VPBR_CFG,
			   CS35L36_VPBR_ATK_VOL_MASK,
			   vpbr_config->vpbr_atk_vol <<
			   CS35L36_VPBR_ATK_VOL_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_VPBR_CFG,
			   CS35L36_VPBR_ATK_RATE_MASK,
			   vpbr_config->vpbr_atk_rate <<
			   CS35L36_VPBR_ATK_RATE_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_VPBR_CFG,
			   CS35L36_VPBR_WAIT_MASK,
			   vpbr_config->vpbr_wait <<
			   CS35L36_VPBR_WAIT_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_VPBR_CFG,
			   CS35L36_VPBR_REL_RATE_MASK,
			   vpbr_config->vpbr_rel_rate <<
			   CS35L36_VPBR_REL_RATE_SHIFT);
	regmap_update_bits(cs35l36->regmap, CS35L36_VPBR_CFG,
			   CS35L36_VPBR_MUTE_EN_MASK,
			   vpbr_config->vpbr_mute_en <<
			   CS35L36_VPBR_MUTE_EN_SHIFT);
}

static const struct reg_sequence cs35l36_reva0_errata_patch[] = {
	{ CS35L36_TESTKEY_CTRL,		CS35L36_TEST_UNLOCK1 },
	{ CS35L36_TESTKEY_CTRL,		CS35L36_TEST_UNLOCK2 },
	/* Errata Writes */
	{ CS35L36_OTP_CTRL1,		0x00002060 },
	{ CS35L36_OTP_CTRL2,		0x00000001 },
	{ CS35L36_OTP_CTRL1,		0x00002460 },
	{ CS35L36_OTP_CTRL2,		0x00000001 },
	{ 0x00002088,			0x012A1838 },
	{ 0x00003014,			0x0100EE0E },
	{ 0x00003008,			0x0008184A },
	{ 0x00007418,			0x509001C8 },
	{ 0x00007064,			0x0929A800 },
	{ 0x00002D10,			0x0002C01C },
	{ 0x0000410C,			0x00000A11 },
	{ 0x00006E08,			0x8B19140C },
	{ 0x00006454,			0x0300000A },
	{ CS35L36_AMP_NG_CTRL,		0x000020EF },
	{ 0x00007E34,			0x0000000E },
	{ 0x0000410C,			0x00000A11 },
	{ 0x00007410,			0x20514B00 },
	/* PAC Config */
	{ CS35L36_CTRL_OVRRIDE,		0x00000000 },
	{ CS35L36_PAC_INT0_CTRL,	0x00860001 },
	{ CS35L36_PAC_INT1_CTRL,	0x00860001 },
	{ CS35L36_PAC_INT2_CTRL,	0x00860001 },
	{ CS35L36_PAC_INT3_CTRL,	0x00860001 },
	{ CS35L36_PAC_INT4_CTRL,	0x00860001 },
	{ CS35L36_PAC_INT5_CTRL,	0x00860001 },
	{ CS35L36_PAC_INT6_CTRL,	0x00860001 },
	{ CS35L36_PAC_INT7_CTRL,	0x00860001 },
	{ CS35L36_PAC_INT_FLUSH_CTRL,	0x000000FF },
	{ CS35L36_TESTKEY_CTRL,		CS35L36_TEST_LOCK1 },
	{ CS35L36_TESTKEY_CTRL,		CS35L36_TEST_LOCK2 },
};

static const struct reg_sequence cs35l36_revb0_errata_patch[] = {
	{ CS35L36_TESTKEY_CTRL,	CS35L36_TEST_UNLOCK1 },
	{ CS35L36_TESTKEY_CTRL, CS35L36_TEST_UNLOCK2 },
	{ 0x00007064,		0x0929A800 },
	{ 0x00007850,		0x00002FA9 },
	{ 0x00007854,		0x0003F1D5 },
	{ 0x00007858,		0x0003F5E3 },
	{ 0x0000785C,		0x00001137 },
	{ 0x00007860,		0x0001A7A5 },
	{ 0x00007864,		0x0002F16A },
	{ 0x00007868,		0x00003E21 },
	{ 0x00007848,		0x00000001 },
	{ 0x00003854,		0x05180240 },
	{ 0x00007418,		0x509001C8 },
	{ 0x0000394C,		0x028764BD },
	{ CS35L36_TESTKEY_CTRL,	CS35L36_TEST_LOCK1 },
	{ CS35L36_TESTKEY_CTRL, CS35L36_TEST_LOCK2 },
};

static int cs35l36_i2c_probe(struct i2c_client *i2c_client)
{
	struct cs35l36_private *cs35l36;
	struct device *dev = &i2c_client->dev;
	struct cs35l36_platform_data *pdata = dev_get_platdata(dev);
	struct irq_data *irq_d;
	int ret, irq_pol, chip_irq_pol, i;
	u32 reg_id, reg_revid, l37_id_reg;

	cs35l36 = devm_kzalloc(dev, sizeof(struct cs35l36_private), GFP_KERNEL);
	if (!cs35l36)
		return -ENOMEM;

	cs35l36->dev = dev;

	i2c_set_clientdata(i2c_client, cs35l36);
	cs35l36->regmap = devm_regmap_init_i2c(i2c_client, &cs35l36_regmap);
	if (IS_ERR(cs35l36->regmap)) {
		ret = PTR_ERR(cs35l36->regmap);
		dev_err(dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}

	cs35l36->num_supplies = ARRAY_SIZE(cs35l36_supplies);
	for (i = 0; i < ARRAY_SIZE(cs35l36_supplies); i++)
		cs35l36->supplies[i].supply = cs35l36_supplies[i];

	ret = devm_regulator_bulk_get(dev, cs35l36->num_supplies,
				      cs35l36->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to request core supplies: %d\n", ret);
		return ret;
	}

	if (pdata) {
		cs35l36->pdata = *pdata;
	} else {
		pdata = devm_kzalloc(dev, sizeof(struct cs35l36_platform_data),
				     GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		if (i2c_client->dev.of_node) {
			ret = cs35l36_handle_of_data(i2c_client, pdata);
			if (ret != 0)
				return ret;

		}

		cs35l36->pdata = *pdata;
	}

	ret = regulator_bulk_enable(cs35l36->num_supplies, cs35l36->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable core supplies: %d\n", ret);
		return ret;
	}

	/* returning NULL can be an option if in stereo mode */
	cs35l36->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						      GPIOD_OUT_LOW);
	if (IS_ERR(cs35l36->reset_gpio)) {
		ret = PTR_ERR(cs35l36->reset_gpio);
		cs35l36->reset_gpio = NULL;
		if (ret == -EBUSY) {
			dev_info(dev, "Reset line busy, assuming shared reset\n");
		} else {
			dev_err(dev, "Failed to get reset GPIO: %d\n", ret);
			goto err_disable_regs;
		}
	}

	if (cs35l36->reset_gpio)
		gpiod_set_value_cansleep(cs35l36->reset_gpio, 1);

	usleep_range(2000, 2100);

	/* initialize amplifier */
	ret = regmap_read(cs35l36->regmap, CS35L36_SW_RESET, &reg_id);
	if (ret < 0) {
		dev_err(dev, "Get Device ID failed %d\n", ret);
		goto err;
	}

	if (reg_id != CS35L36_CHIP_ID) {
		dev_err(dev, "Device ID (%X). Expected ID %X\n", reg_id,
			CS35L36_CHIP_ID);
		ret = -ENODEV;
		goto err;
	}

	ret = regmap_read(cs35l36->regmap, CS35L36_REV_ID, &reg_revid);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "Get Revision ID failed %d\n", ret);
		goto err;
	}

	cs35l36->rev_id = reg_revid >> 8;

	ret = regmap_read(cs35l36->regmap, CS35L36_OTP_MEM30, &l37_id_reg);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "Failed to read otp_id Register %d\n",
			ret);
		goto err;
	}

	if ((l37_id_reg & CS35L36_OTP_REV_MASK) == CS35L36_OTP_REV_L37)
		cs35l36->chip_version = CS35L36_12V_L37;
	else
		cs35l36->chip_version = CS35L36_10V_L36;

	switch (cs35l36->rev_id) {
	case CS35L36_REV_A0:
		ret = regmap_register_patch(cs35l36->regmap,
				cs35l36_reva0_errata_patch,
				ARRAY_SIZE(cs35l36_reva0_errata_patch));
		if (ret < 0) {
			dev_err(dev, "Failed to apply A0 errata patch %d\n",
				ret);
			goto err;
		}
		break;
	case CS35L36_REV_B0:
		ret = cs35l36_pac(cs35l36);
		if (ret < 0) {
			dev_err(dev, "Failed to Trim OTP %d\n", ret);
			goto err;
		}

		ret = regmap_register_patch(cs35l36->regmap,
				cs35l36_revb0_errata_patch,
				ARRAY_SIZE(cs35l36_revb0_errata_patch));
		if (ret < 0) {
			dev_err(dev, "Failed to apply B0 errata patch %d\n",
				ret);
			goto err;
		}
		break;
	}

	if (pdata->vpbr_config.is_present)
		cs35l36_apply_vpbr_config(cs35l36);

	irq_d = irq_get_irq_data(i2c_client->irq);
	if (!irq_d) {
		dev_err(&i2c_client->dev, "Invalid IRQ: %d\n", i2c_client->irq);
		ret = -ENODEV;
		goto err;
	}

	irq_pol = irqd_get_trigger_type(irq_d);

	switch (irq_pol) {
	case IRQF_TRIGGER_FALLING:
	case IRQF_TRIGGER_LOW:
		chip_irq_pol = 0;
		break;
	case IRQF_TRIGGER_RISING:
	case IRQF_TRIGGER_HIGH:
		chip_irq_pol = 1;
		break;
	default:
		dev_err(cs35l36->dev, "Invalid IRQ polarity: %d\n", irq_pol);
		ret = -EINVAL;
		goto err;
	}

	regmap_update_bits(cs35l36->regmap, CS35L36_PAD_INTERFACE,
			   CS35L36_INT_POL_SEL_MASK,
			   chip_irq_pol << CS35L36_INT_POL_SEL_SHIFT);

	ret = devm_request_threaded_irq(dev, i2c_client->irq, NULL, cs35l36_irq,
					IRQF_ONESHOT | irq_pol, "cs35l36",
					cs35l36);
	if (ret != 0) {
		dev_err(dev, "Failed to request IRQ: %d\n", ret);
		goto err;
	}

	regmap_update_bits(cs35l36->regmap, CS35L36_PAD_INTERFACE,
			   CS35L36_INT_OUTPUT_EN_MASK, 1);

	/* Set interrupt masks for critical errors */
	regmap_write(cs35l36->regmap, CS35L36_INT1_MASK,
		     CS35L36_INT1_MASK_DEFAULT);
	regmap_write(cs35l36->regmap, CS35L36_INT3_MASK,
		     CS35L36_INT3_MASK_DEFAULT);

	dev_info(&i2c_client->dev, "Cirrus Logic CS35L%d, Revision: %02X\n",
		 cs35l36->chip_version, reg_revid >> 8);

	ret =  devm_snd_soc_register_component(dev, &soc_component_dev_cs35l36,
					       cs35l36_dai,
					       ARRAY_SIZE(cs35l36_dai));
	if (ret < 0) {
		dev_err(dev, "%s: Register component failed %d\n", __func__,
			ret);
		goto err;
	}

	return 0;

err:
	gpiod_set_value_cansleep(cs35l36->reset_gpio, 0);

err_disable_regs:
	regulator_bulk_disable(cs35l36->num_supplies, cs35l36->supplies);
	return ret;
}

static int cs35l36_i2c_remove(struct i2c_client *client)
{
	struct cs35l36_private *cs35l36 = i2c_get_clientdata(client);

	/* Reset interrupt masks for device removal */
	regmap_write(cs35l36->regmap, CS35L36_INT1_MASK,
		     CS35L36_INT1_MASK_RESET);
	regmap_write(cs35l36->regmap, CS35L36_INT3_MASK,
		     CS35L36_INT3_MASK_RESET);

	if (cs35l36->reset_gpio)
		gpiod_set_value_cansleep(cs35l36->reset_gpio, 0);

	regulator_bulk_disable(cs35l36->num_supplies, cs35l36->supplies);

	return 0;
}
static const struct of_device_id cs35l36_of_match[] = {
	{.compatible = "cirrus,cs35l36"},
	{},
};
MODULE_DEVICE_TABLE(of, cs35l36_of_match);

static const struct i2c_device_id cs35l36_id[] = {
	{"cs35l36", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, cs35l36_id);

static struct i2c_driver cs35l36_i2c_driver = {
	.driver = {
		.name = "cs35l36",
		.of_match_table = cs35l36_of_match,
	},
	.id_table = cs35l36_id,
	.probe_new = cs35l36_i2c_probe,
	.remove = cs35l36_i2c_remove,
};
module_i2c_driver(cs35l36_i2c_driver);

MODULE_DESCRIPTION("ASoC CS35L36 driver");
MODULE_AUTHOR("James Schulman, Cirrus Logic Inc, <james.schulman@cirrus.com>");
MODULE_LICENSE("GPL");
