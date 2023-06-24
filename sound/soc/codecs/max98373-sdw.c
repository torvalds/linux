// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020, Maxim Integrated

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/of.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/soundwire/sdw_registers.h>
#include "max98373.h"
#include "max98373-sdw.h"

struct sdw_stream_data {
	struct sdw_stream_runtime *sdw_stream;
};

static const u32 max98373_sdw_cache_reg[] = {
	MAX98373_R2054_MEAS_ADC_PVDD_CH_READBACK,
	MAX98373_R2055_MEAS_ADC_THERM_CH_READBACK,
	MAX98373_R20B6_BDE_CUR_STATE_READBACK,
};

static struct reg_default max98373_reg[] = {
	{MAX98373_R0040_SCP_INIT_STAT_1, 0x00},
	{MAX98373_R0041_SCP_INIT_MASK_1, 0x00},
	{MAX98373_R0042_SCP_INIT_STAT_2, 0x00},
	{MAX98373_R0044_SCP_CTRL, 0x00},
	{MAX98373_R0045_SCP_SYSTEM_CTRL, 0x00},
	{MAX98373_R0046_SCP_DEV_NUMBER, 0x00},
	{MAX98373_R0050_SCP_DEV_ID_0, 0x21},
	{MAX98373_R0051_SCP_DEV_ID_1, 0x01},
	{MAX98373_R0052_SCP_DEV_ID_2, 0x9F},
	{MAX98373_R0053_SCP_DEV_ID_3, 0x87},
	{MAX98373_R0054_SCP_DEV_ID_4, 0x08},
	{MAX98373_R0055_SCP_DEV_ID_5, 0x00},
	{MAX98373_R0060_SCP_FRAME_CTLR, 0x00},
	{MAX98373_R0070_SCP_FRAME_CTLR, 0x00},
	{MAX98373_R0100_DP1_INIT_STAT, 0x00},
	{MAX98373_R0101_DP1_INIT_MASK, 0x00},
	{MAX98373_R0102_DP1_PORT_CTRL, 0x00},
	{MAX98373_R0103_DP1_BLOCK_CTRL_1, 0x00},
	{MAX98373_R0104_DP1_PREPARE_STATUS, 0x00},
	{MAX98373_R0105_DP1_PREPARE_CTRL, 0x00},
	{MAX98373_R0120_DP1_CHANNEL_EN, 0x00},
	{MAX98373_R0122_DP1_SAMPLE_CTRL1, 0x00},
	{MAX98373_R0123_DP1_SAMPLE_CTRL2, 0x00},
	{MAX98373_R0124_DP1_OFFSET_CTRL1, 0x00},
	{MAX98373_R0125_DP1_OFFSET_CTRL2, 0x00},
	{MAX98373_R0126_DP1_HCTRL, 0x00},
	{MAX98373_R0127_DP1_BLOCK_CTRL3, 0x00},
	{MAX98373_R0130_DP1_CHANNEL_EN, 0x00},
	{MAX98373_R0132_DP1_SAMPLE_CTRL1, 0x00},
	{MAX98373_R0133_DP1_SAMPLE_CTRL2, 0x00},
	{MAX98373_R0134_DP1_OFFSET_CTRL1, 0x00},
	{MAX98373_R0135_DP1_OFFSET_CTRL2, 0x00},
	{MAX98373_R0136_DP1_HCTRL, 0x0136},
	{MAX98373_R0137_DP1_BLOCK_CTRL3, 0x00},
	{MAX98373_R0300_DP3_INIT_STAT, 0x00},
	{MAX98373_R0301_DP3_INIT_MASK, 0x00},
	{MAX98373_R0302_DP3_PORT_CTRL, 0x00},
	{MAX98373_R0303_DP3_BLOCK_CTRL_1, 0x00},
	{MAX98373_R0304_DP3_PREPARE_STATUS, 0x00},
	{MAX98373_R0305_DP3_PREPARE_CTRL, 0x00},
	{MAX98373_R0320_DP3_CHANNEL_EN, 0x00},
	{MAX98373_R0322_DP3_SAMPLE_CTRL1, 0x00},
	{MAX98373_R0323_DP3_SAMPLE_CTRL2, 0x00},
	{MAX98373_R0324_DP3_OFFSET_CTRL1, 0x00},
	{MAX98373_R0325_DP3_OFFSET_CTRL2, 0x00},
	{MAX98373_R0326_DP3_HCTRL, 0x00},
	{MAX98373_R0327_DP3_BLOCK_CTRL3, 0x00},
	{MAX98373_R0330_DP3_CHANNEL_EN, 0x00},
	{MAX98373_R0332_DP3_SAMPLE_CTRL1, 0x00},
	{MAX98373_R0333_DP3_SAMPLE_CTRL2, 0x00},
	{MAX98373_R0334_DP3_OFFSET_CTRL1, 0x00},
	{MAX98373_R0335_DP3_OFFSET_CTRL2, 0x00},
	{MAX98373_R0336_DP3_HCTRL, 0x00},
	{MAX98373_R0337_DP3_BLOCK_CTRL3, 0x00},
	{MAX98373_R2000_SW_RESET, 0x00},
	{MAX98373_R2001_INT_RAW1, 0x00},
	{MAX98373_R2002_INT_RAW2, 0x00},
	{MAX98373_R2003_INT_RAW3, 0x00},
	{MAX98373_R2004_INT_STATE1, 0x00},
	{MAX98373_R2005_INT_STATE2, 0x00},
	{MAX98373_R2006_INT_STATE3, 0x00},
	{MAX98373_R2007_INT_FLAG1, 0x00},
	{MAX98373_R2008_INT_FLAG2, 0x00},
	{MAX98373_R2009_INT_FLAG3, 0x00},
	{MAX98373_R200A_INT_EN1, 0x00},
	{MAX98373_R200B_INT_EN2, 0x00},
	{MAX98373_R200C_INT_EN3, 0x00},
	{MAX98373_R200D_INT_FLAG_CLR1, 0x00},
	{MAX98373_R200E_INT_FLAG_CLR2, 0x00},
	{MAX98373_R200F_INT_FLAG_CLR3, 0x00},
	{MAX98373_R2010_IRQ_CTRL, 0x00},
	{MAX98373_R2014_THERM_WARN_THRESH, 0x10},
	{MAX98373_R2015_THERM_SHDN_THRESH, 0x27},
	{MAX98373_R2016_THERM_HYSTERESIS, 0x01},
	{MAX98373_R2017_THERM_FOLDBACK_SET, 0xC0},
	{MAX98373_R2018_THERM_FOLDBACK_EN, 0x00},
	{MAX98373_R201E_PIN_DRIVE_STRENGTH, 0x55},
	{MAX98373_R2020_PCM_TX_HIZ_EN_1, 0xFE},
	{MAX98373_R2021_PCM_TX_HIZ_EN_2, 0xFF},
	{MAX98373_R2022_PCM_TX_SRC_1, 0x00},
	{MAX98373_R2023_PCM_TX_SRC_2, 0x00},
	{MAX98373_R2024_PCM_DATA_FMT_CFG, 0xC0},
	{MAX98373_R2025_AUDIO_IF_MODE, 0x00},
	{MAX98373_R2026_PCM_CLOCK_RATIO, 0x04},
	{MAX98373_R2027_PCM_SR_SETUP_1, 0x08},
	{MAX98373_R2028_PCM_SR_SETUP_2, 0x88},
	{MAX98373_R2029_PCM_TO_SPK_MONO_MIX_1, 0x00},
	{MAX98373_R202A_PCM_TO_SPK_MONO_MIX_2, 0x00},
	{MAX98373_R202B_PCM_RX_EN, 0x00},
	{MAX98373_R202C_PCM_TX_EN, 0x00},
	{MAX98373_R202E_ICC_RX_CH_EN_1, 0x00},
	{MAX98373_R202F_ICC_RX_CH_EN_2, 0x00},
	{MAX98373_R2030_ICC_TX_HIZ_EN_1, 0xFF},
	{MAX98373_R2031_ICC_TX_HIZ_EN_2, 0xFF},
	{MAX98373_R2032_ICC_LINK_EN_CFG, 0x30},
	{MAX98373_R2034_ICC_TX_CNTL, 0x00},
	{MAX98373_R2035_ICC_TX_EN, 0x00},
	{MAX98373_R2036_SOUNDWIRE_CTRL, 0x05},
	{MAX98373_R203D_AMP_DIG_VOL_CTRL, 0x00},
	{MAX98373_R203E_AMP_PATH_GAIN, 0x08},
	{MAX98373_R203F_AMP_DSP_CFG, 0x02},
	{MAX98373_R2040_TONE_GEN_CFG, 0x00},
	{MAX98373_R2041_AMP_CFG, 0x03},
	{MAX98373_R2042_AMP_EDGE_RATE_CFG, 0x00},
	{MAX98373_R2043_AMP_EN, 0x00},
	{MAX98373_R2046_IV_SENSE_ADC_DSP_CFG, 0x04},
	{MAX98373_R2047_IV_SENSE_ADC_EN, 0x00},
	{MAX98373_R2051_MEAS_ADC_SAMPLING_RATE, 0x00},
	{MAX98373_R2052_MEAS_ADC_PVDD_FLT_CFG, 0x00},
	{MAX98373_R2053_MEAS_ADC_THERM_FLT_CFG, 0x00},
	{MAX98373_R2054_MEAS_ADC_PVDD_CH_READBACK, 0x00},
	{MAX98373_R2055_MEAS_ADC_THERM_CH_READBACK, 0x00},
	{MAX98373_R2056_MEAS_ADC_PVDD_CH_EN, 0x00},
	{MAX98373_R2090_BDE_LVL_HOLD, 0x00},
	{MAX98373_R2091_BDE_GAIN_ATK_REL_RATE, 0x00},
	{MAX98373_R2092_BDE_CLIPPER_MODE, 0x00},
	{MAX98373_R2097_BDE_L1_THRESH, 0x00},
	{MAX98373_R2098_BDE_L2_THRESH, 0x00},
	{MAX98373_R2099_BDE_L3_THRESH, 0x00},
	{MAX98373_R209A_BDE_L4_THRESH, 0x00},
	{MAX98373_R209B_BDE_THRESH_HYST, 0x00},
	{MAX98373_R20A8_BDE_L1_CFG_1, 0x00},
	{MAX98373_R20A9_BDE_L1_CFG_2, 0x00},
	{MAX98373_R20AA_BDE_L1_CFG_3, 0x00},
	{MAX98373_R20AB_BDE_L2_CFG_1, 0x00},
	{MAX98373_R20AC_BDE_L2_CFG_2, 0x00},
	{MAX98373_R20AD_BDE_L2_CFG_3, 0x00},
	{MAX98373_R20AE_BDE_L3_CFG_1, 0x00},
	{MAX98373_R20AF_BDE_L3_CFG_2, 0x00},
	{MAX98373_R20B0_BDE_L3_CFG_3, 0x00},
	{MAX98373_R20B1_BDE_L4_CFG_1, 0x00},
	{MAX98373_R20B2_BDE_L4_CFG_2, 0x00},
	{MAX98373_R20B3_BDE_L4_CFG_3, 0x00},
	{MAX98373_R20B4_BDE_INFINITE_HOLD_RELEASE, 0x00},
	{MAX98373_R20B5_BDE_EN, 0x00},
	{MAX98373_R20B6_BDE_CUR_STATE_READBACK, 0x00},
	{MAX98373_R20D1_DHT_CFG, 0x01},
	{MAX98373_R20D2_DHT_ATTACK_CFG, 0x02},
	{MAX98373_R20D3_DHT_RELEASE_CFG, 0x03},
	{MAX98373_R20D4_DHT_EN, 0x00},
	{MAX98373_R20E0_LIMITER_THRESH_CFG, 0x00},
	{MAX98373_R20E1_LIMITER_ATK_REL_RATES, 0x00},
	{MAX98373_R20E2_LIMITER_EN, 0x00},
	{MAX98373_R20FE_DEVICE_AUTO_RESTART_CFG, 0x00},
	{MAX98373_R20FF_GLOBAL_SHDN, 0x00},
	{MAX98373_R21FF_REV_ID, 0x42},
};

static bool max98373_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98373_R21FF_REV_ID:
	case MAX98373_R2010_IRQ_CTRL:
	/* SoundWire Control Port Registers */
	case MAX98373_R0040_SCP_INIT_STAT_1 ... MAX98373_R0070_SCP_FRAME_CTLR:
	/* Soundwire Data Port 1 Registers */
	case MAX98373_R0100_DP1_INIT_STAT ... MAX98373_R0137_DP1_BLOCK_CTRL3:
	/* Soundwire Data Port 3 Registers */
	case MAX98373_R0300_DP3_INIT_STAT ... MAX98373_R0337_DP3_BLOCK_CTRL3:
	case MAX98373_R2000_SW_RESET ... MAX98373_R200C_INT_EN3:
	case MAX98373_R2014_THERM_WARN_THRESH
		... MAX98373_R2018_THERM_FOLDBACK_EN:
	case MAX98373_R201E_PIN_DRIVE_STRENGTH
		... MAX98373_R2036_SOUNDWIRE_CTRL:
	case MAX98373_R203D_AMP_DIG_VOL_CTRL ... MAX98373_R2043_AMP_EN:
	case MAX98373_R2046_IV_SENSE_ADC_DSP_CFG
		... MAX98373_R2047_IV_SENSE_ADC_EN:
	case MAX98373_R2051_MEAS_ADC_SAMPLING_RATE
		... MAX98373_R2056_MEAS_ADC_PVDD_CH_EN:
	case MAX98373_R2090_BDE_LVL_HOLD ... MAX98373_R2092_BDE_CLIPPER_MODE:
	case MAX98373_R2097_BDE_L1_THRESH
		... MAX98373_R209B_BDE_THRESH_HYST:
	case MAX98373_R20A8_BDE_L1_CFG_1 ... MAX98373_R20B3_BDE_L4_CFG_3:
	case MAX98373_R20B5_BDE_EN ... MAX98373_R20B6_BDE_CUR_STATE_READBACK:
	case MAX98373_R20D1_DHT_CFG ... MAX98373_R20D4_DHT_EN:
	case MAX98373_R20E0_LIMITER_THRESH_CFG ... MAX98373_R20E2_LIMITER_EN:
	case MAX98373_R20FE_DEVICE_AUTO_RESTART_CFG
		... MAX98373_R20FF_GLOBAL_SHDN:
		return true;
	default:
		return false;
	}
};

static bool max98373_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98373_R2054_MEAS_ADC_PVDD_CH_READBACK:
	case MAX98373_R2055_MEAS_ADC_THERM_CH_READBACK:
	case MAX98373_R20B6_BDE_CUR_STATE_READBACK:
	case MAX98373_R20FF_GLOBAL_SHDN:
	case MAX98373_R21FF_REV_ID:
	/* SoundWire Control Port Registers */
	case MAX98373_R0040_SCP_INIT_STAT_1 ... MAX98373_R0070_SCP_FRAME_CTLR:
	/* Soundwire Data Port 1 Registers */
	case MAX98373_R0100_DP1_INIT_STAT ... MAX98373_R0137_DP1_BLOCK_CTRL3:
	/* Soundwire Data Port 3 Registers */
	case MAX98373_R0300_DP3_INIT_STAT ... MAX98373_R0337_DP3_BLOCK_CTRL3:
	case MAX98373_R2000_SW_RESET ... MAX98373_R2009_INT_FLAG3:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config max98373_sdw_regmap = {
	.reg_bits = 32,
	.val_bits = 8,
	.max_register = MAX98373_R21FF_REV_ID,
	.reg_defaults  = max98373_reg,
	.num_reg_defaults = ARRAY_SIZE(max98373_reg),
	.readable_reg = max98373_readable_register,
	.volatile_reg = max98373_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};

/* Power management functions and structure */
static __maybe_unused int max98373_suspend(struct device *dev)
{
	struct max98373_priv *max98373 = dev_get_drvdata(dev);
	int i;

	/* cache feedback register values before suspend */
	for (i = 0; i < max98373->cache_num; i++)
		regmap_read(max98373->regmap, max98373->cache[i].reg, &max98373->cache[i].val);

	regcache_cache_only(max98373->regmap, true);

	return 0;
}

#define MAX98373_PROBE_TIMEOUT 5000

static __maybe_unused int max98373_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct max98373_priv *max98373 = dev_get_drvdata(dev);
	unsigned long time;

	if (!max98373->first_hw_init)
		return 0;

	if (!slave->unattach_request)
		goto regmap_sync;

	time = wait_for_completion_timeout(&slave->initialization_complete,
					   msecs_to_jiffies(MAX98373_PROBE_TIMEOUT));
	if (!time) {
		dev_err(dev, "Initialization not complete, timed out\n");
		sdw_show_ping_status(slave->bus, true);

		return -ETIMEDOUT;
	}

regmap_sync:
	slave->unattach_request = 0;
	regcache_cache_only(max98373->regmap, false);
	regcache_sync(max98373->regmap);

	return 0;
}

static const struct dev_pm_ops max98373_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(max98373_suspend, max98373_resume)
	SET_RUNTIME_PM_OPS(max98373_suspend, max98373_resume, NULL)
};

static int max98373_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int nval, i;
	u32 bit;
	unsigned long addr;
	struct sdw_dpn_prop *dpn;

	prop->scp_int1_mask = SDW_SCP_INT1_BUS_CLASH | SDW_SCP_INT1_PARITY;

	/* BITMAP: 00001000  Dataport 3 is active */
	prop->source_ports = BIT(3);
	/* BITMAP: 00000010  Dataport 1 is active */
	prop->sink_ports = BIT(1);
	prop->paging_support = true;
	prop->clk_stop_timeout = 20;

	nval = hweight32(prop->source_ports);
	prop->src_dpn_prop = devm_kcalloc(&slave->dev, nval,
					  sizeof(*prop->src_dpn_prop),
					  GFP_KERNEL);
	if (!prop->src_dpn_prop)
		return -ENOMEM;

	i = 0;
	dpn = prop->src_dpn_prop;
	addr = prop->source_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[i].num = bit;
		dpn[i].type = SDW_DPN_FULL;
		dpn[i].simple_ch_prep_sm = true;
		dpn[i].ch_prep_timeout = 10;
		i++;
	}

	/* do this again for sink now */
	nval = hweight32(prop->sink_ports);
	prop->sink_dpn_prop = devm_kcalloc(&slave->dev, nval,
					   sizeof(*prop->sink_dpn_prop),
					   GFP_KERNEL);
	if (!prop->sink_dpn_prop)
		return -ENOMEM;

	i = 0;
	dpn = prop->sink_dpn_prop;
	addr = prop->sink_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[i].num = bit;
		dpn[i].type = SDW_DPN_FULL;
		dpn[i].simple_ch_prep_sm = true;
		dpn[i].ch_prep_timeout = 10;
		i++;
	}

	/* set the timeout values */
	prop->clk_stop_timeout = 20;

	return 0;
}

static int max98373_io_init(struct sdw_slave *slave)
{
	struct device *dev = &slave->dev;
	struct max98373_priv *max98373 = dev_get_drvdata(dev);

	if (max98373->first_hw_init) {
		regcache_cache_only(max98373->regmap, false);
		regcache_cache_bypass(max98373->regmap, true);
	}

	/*
	 * PM runtime is only enabled when a Slave reports as Attached
	 */
	if (!max98373->first_hw_init) {
		/* set autosuspend parameters */
		pm_runtime_set_autosuspend_delay(dev, 3000);
		pm_runtime_use_autosuspend(dev);

		/* update count of parent 'active' children */
		pm_runtime_set_active(dev);

		/* make sure the device does not suspend immediately */
		pm_runtime_mark_last_busy(dev);

		pm_runtime_enable(dev);
	}

	pm_runtime_get_noresume(dev);

	/* Software Reset */
	max98373_reset(max98373, dev);

	/* Set soundwire mode */
	regmap_write(max98373->regmap, MAX98373_R2025_AUDIO_IF_MODE, 3);
	/* Enable ADC */
	regmap_write(max98373->regmap, MAX98373_R2047_IV_SENSE_ADC_EN, 3);
	/* Set default Soundwire clock */
	regmap_write(max98373->regmap, MAX98373_R2036_SOUNDWIRE_CTRL, 5);
	/* Set default sampling rate for speaker and IVDAC */
	regmap_write(max98373->regmap, MAX98373_R2028_PCM_SR_SETUP_2, 0x88);
	/* IV default slot configuration */
	regmap_write(max98373->regmap,
		     MAX98373_R2020_PCM_TX_HIZ_EN_1,
		     0xFF);
	regmap_write(max98373->regmap,
		     MAX98373_R2021_PCM_TX_HIZ_EN_2,
		     0xFF);
	/* L/R mix configuration */
	regmap_write(max98373->regmap,
		     MAX98373_R2029_PCM_TO_SPK_MONO_MIX_1,
		     0x80);
	regmap_write(max98373->regmap,
		     MAX98373_R202A_PCM_TO_SPK_MONO_MIX_2,
		     0x1);
	/* Enable DC blocker */
	regmap_write(max98373->regmap,
		     MAX98373_R203F_AMP_DSP_CFG,
		     0x3);
	/* Enable IMON VMON DC blocker */
	regmap_write(max98373->regmap,
		     MAX98373_R2046_IV_SENSE_ADC_DSP_CFG,
		     0x7);
	/* voltage, current slot configuration */
	regmap_write(max98373->regmap,
		     MAX98373_R2022_PCM_TX_SRC_1,
		     (max98373->i_slot << MAX98373_PCM_TX_CH_SRC_A_I_SHIFT |
		     max98373->v_slot) & 0xFF);
	if (max98373->v_slot < 8)
		regmap_update_bits(max98373->regmap,
				   MAX98373_R2020_PCM_TX_HIZ_EN_1,
				   1 << max98373->v_slot, 0);
	else
		regmap_update_bits(max98373->regmap,
				   MAX98373_R2021_PCM_TX_HIZ_EN_2,
				   1 << (max98373->v_slot - 8), 0);

	if (max98373->i_slot < 8)
		regmap_update_bits(max98373->regmap,
				   MAX98373_R2020_PCM_TX_HIZ_EN_1,
				   1 << max98373->i_slot, 0);
	else
		regmap_update_bits(max98373->regmap,
				   MAX98373_R2021_PCM_TX_HIZ_EN_2,
				   1 << (max98373->i_slot - 8), 0);

	/* speaker feedback slot configuration */
	regmap_write(max98373->regmap,
		     MAX98373_R2023_PCM_TX_SRC_2,
		     max98373->spkfb_slot & 0xFF);

	/* Set interleave mode */
	if (max98373->interleave_mode)
		regmap_update_bits(max98373->regmap,
				   MAX98373_R2024_PCM_DATA_FMT_CFG,
				   MAX98373_PCM_TX_CH_INTERLEAVE_MASK,
				   MAX98373_PCM_TX_CH_INTERLEAVE_MASK);

	/* Speaker enable */
	regmap_update_bits(max98373->regmap,
			   MAX98373_R2043_AMP_EN,
			   MAX98373_SPK_EN_MASK, 1);

	regmap_write(max98373->regmap, MAX98373_R20B5_BDE_EN, 1);
	regmap_write(max98373->regmap, MAX98373_R20E2_LIMITER_EN, 1);

	if (max98373->first_hw_init) {
		regcache_cache_bypass(max98373->regmap, false);
		regcache_mark_dirty(max98373->regmap);
	}

	max98373->first_hw_init = true;
	max98373->hw_init = true;

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;
}

static int max98373_clock_calculate(struct sdw_slave *slave,
				    unsigned int clk_freq)
{
	int x, y;
	static const int max98373_clk_family[] = {
		7680000, 8400000, 9600000, 11289600,
		12000000, 12288000, 13000000
	};

	for (x = 0; x < 4; x++)
		for (y = 0; y < ARRAY_SIZE(max98373_clk_family); y++)
			if (clk_freq == (max98373_clk_family[y] >> x))
				return (x << 3) + y;

	/* Set default clock (12.288 Mhz) if the value is not in the list */
	dev_err(&slave->dev, "Requested clock not found. (clk_freq = %d)\n",
		clk_freq);
	return 0x5;
}

static int max98373_clock_config(struct sdw_slave *slave,
				 struct sdw_bus_params *params)
{
	struct device *dev = &slave->dev;
	struct max98373_priv *max98373 = dev_get_drvdata(dev);
	unsigned int clk_freq, value;

	clk_freq = (params->curr_dr_freq >> 1);

	/*
	 *	Select the proper value for the register based on the
	 *	requested clock. If the value is not in the list,
	 *	use reasonable default - 12.288 Mhz
	 */
	value = max98373_clock_calculate(slave, clk_freq);

	/* SWCLK */
	regmap_write(max98373->regmap, MAX98373_R2036_SOUNDWIRE_CTRL, value);

	/* The default Sampling Rate value for IV is 48KHz*/
	regmap_write(max98373->regmap, MAX98373_R2028_PCM_SR_SETUP_2, 0x88);

	return 0;
}

#define MAX98373_RATES SNDRV_PCM_RATE_8000_96000
#define MAX98373_FORMATS (SNDRV_PCM_FMTBIT_S32_LE)

static int max98373_sdw_dai_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct max98373_priv *max98373 =
		snd_soc_component_get_drvdata(component);

	struct sdw_stream_config stream_config;
	struct sdw_port_config port_config;
	enum sdw_data_direction direction;
	struct sdw_stream_data *stream;
	int ret, chan_sz, sampling_rate;

	stream = snd_soc_dai_get_dma_data(dai, substream);

	if (!stream)
		return -EINVAL;

	if (!max98373->slave)
		return -EINVAL;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		direction = SDW_DATA_DIR_RX;
		port_config.num = 1;
	} else {
		direction = SDW_DATA_DIR_TX;
		port_config.num = 3;
	}

	stream_config.frame_rate = params_rate(params);
	stream_config.bps = snd_pcm_format_width(params_format(params));
	stream_config.direction = direction;

	if (max98373->slot && direction == SDW_DATA_DIR_RX) {
		stream_config.ch_count = max98373->slot;
		port_config.ch_mask = max98373->rx_mask;
	} else {
		/* only IV are supported by capture */
		if (direction == SDW_DATA_DIR_TX)
			stream_config.ch_count = 2;
		else
			stream_config.ch_count = params_channels(params);

		port_config.ch_mask = GENMASK((int)stream_config.ch_count - 1, 0);
	}

	ret = sdw_stream_add_slave(max98373->slave, &stream_config,
				   &port_config, 1, stream->sdw_stream);
	if (ret) {
		dev_err(dai->dev, "Unable to configure port\n");
		return ret;
	}

	if (params_channels(params) > 16) {
		dev_err(component->dev, "Unsupported channels %d\n",
			params_channels(params));
		return -EINVAL;
	}

	/* Channel size configuration */
	switch (snd_pcm_format_width(params_format(params))) {
	case 16:
		chan_sz = MAX98373_PCM_MODE_CFG_CHANSZ_16;
		break;
	case 24:
		chan_sz = MAX98373_PCM_MODE_CFG_CHANSZ_24;
		break;
	case 32:
		chan_sz = MAX98373_PCM_MODE_CFG_CHANSZ_32;
		break;
	default:
		dev_err(component->dev, "Channel size unsupported %d\n",
			params_format(params));
		return -EINVAL;
	}

	max98373->ch_size = snd_pcm_format_width(params_format(params));

	regmap_update_bits(max98373->regmap,
			   MAX98373_R2024_PCM_DATA_FMT_CFG,
			   MAX98373_PCM_MODE_CFG_CHANSZ_MASK, chan_sz);

	dev_dbg(component->dev, "Format supported %d", params_format(params));

	/* Sampling rate configuration */
	switch (params_rate(params)) {
	case 8000:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_8000;
		break;
	case 11025:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_11025;
		break;
	case 12000:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_12000;
		break;
	case 16000:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_16000;
		break;
	case 22050:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_22050;
		break;
	case 24000:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_24000;
		break;
	case 32000:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_32000;
		break;
	case 44100:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_44100;
		break;
	case 48000:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_48000;
		break;
	case 88200:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_88200;
		break;
	case 96000:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_96000;
		break;
	default:
		dev_err(component->dev, "Rate %d is not supported\n",
			params_rate(params));
		return -EINVAL;
	}

	/* set correct sampling frequency */
	regmap_update_bits(max98373->regmap,
			   MAX98373_R2028_PCM_SR_SETUP_2,
			   MAX98373_PCM_SR_SET2_SR_MASK,
			   sampling_rate << MAX98373_PCM_SR_SET2_SR_SHIFT);

	/* set sampling rate of IV */
	regmap_update_bits(max98373->regmap,
			   MAX98373_R2028_PCM_SR_SETUP_2,
			   MAX98373_PCM_SR_SET2_IVADC_SR_MASK,
			   sampling_rate);

	return 0;
}

static int max98373_pcm_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct max98373_priv *max98373 =
		snd_soc_component_get_drvdata(component);
	struct sdw_stream_data *stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!max98373->slave)
		return -EINVAL;

	sdw_stream_remove_slave(max98373->slave, stream->sdw_stream);
	return 0;
}

static int max98373_set_sdw_stream(struct snd_soc_dai *dai,
				   void *sdw_stream, int direction)
{
	struct sdw_stream_data *stream;

	if (!sdw_stream)
		return 0;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return -ENOMEM;

	stream->sdw_stream = sdw_stream;

	/* Use tx_mask or rx_mask to configure stream tag and set dma_data */
	if (direction == SNDRV_PCM_STREAM_PLAYBACK)
		dai->playback_dma_data = stream;
	else
		dai->capture_dma_data = stream;

	return 0;
}

static void max98373_shutdown(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct sdw_stream_data *stream;

	stream = snd_soc_dai_get_dma_data(dai, substream);
	snd_soc_dai_set_dma_data(dai, substream, NULL);
	kfree(stream);
}

static int max98373_sdw_set_tdm_slot(struct snd_soc_dai *dai,
				     unsigned int tx_mask,
				     unsigned int rx_mask,
				     int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct max98373_priv *max98373 =
		snd_soc_component_get_drvdata(component);

	/* tx_mask is unused since it's irrelevant for I/V feedback */
	if (tx_mask)
		return -EINVAL;

	if (!rx_mask && !slots && !slot_width)
		max98373->tdm_mode = false;
	else
		max98373->tdm_mode = true;

	max98373->rx_mask = rx_mask;
	max98373->slot = slots;

	return 0;
}

static const struct snd_soc_dai_ops max98373_dai_sdw_ops = {
	.hw_params = max98373_sdw_dai_hw_params,
	.hw_free = max98373_pcm_hw_free,
	.set_stream = max98373_set_sdw_stream,
	.shutdown = max98373_shutdown,
	.set_tdm_slot = max98373_sdw_set_tdm_slot,
};

static struct snd_soc_dai_driver max98373_sdw_dai[] = {
	{
		.name = "max98373-aif1",
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MAX98373_RATES,
			.formats = MAX98373_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MAX98373_RATES,
			.formats = MAX98373_FORMATS,
		},
		.ops = &max98373_dai_sdw_ops,
	}
};

static int max98373_init(struct sdw_slave *slave, struct regmap *regmap)
{
	struct max98373_priv *max98373;
	int ret;
	int i;
	struct device *dev = &slave->dev;

	/*  Allocate and assign private driver data structure  */
	max98373 = devm_kzalloc(dev, sizeof(*max98373), GFP_KERNEL);
	if (!max98373)
		return -ENOMEM;

	dev_set_drvdata(dev, max98373);
	max98373->regmap = regmap;
	max98373->slave = slave;

	max98373->cache_num = ARRAY_SIZE(max98373_sdw_cache_reg);
	max98373->cache = devm_kcalloc(dev, max98373->cache_num,
				       sizeof(*max98373->cache),
				       GFP_KERNEL);
	if (!max98373->cache)
		return -ENOMEM;

	for (i = 0; i < max98373->cache_num; i++)
		max98373->cache[i].reg = max98373_sdw_cache_reg[i];

	/* Read voltage and slot configuration */
	max98373_slot_config(dev, max98373);

	max98373->hw_init = false;
	max98373->first_hw_init = false;

	/* codec registration  */
	ret = devm_snd_soc_register_component(dev, &soc_codec_dev_max98373_sdw,
					      max98373_sdw_dai,
					      ARRAY_SIZE(max98373_sdw_dai));
	if (ret < 0)
		dev_err(dev, "Failed to register codec: %d\n", ret);

	return ret;
}

static int max98373_update_status(struct sdw_slave *slave,
				  enum sdw_slave_status status)
{
	struct max98373_priv *max98373 = dev_get_drvdata(&slave->dev);

	if (status == SDW_SLAVE_UNATTACHED)
		max98373->hw_init = false;

	/*
	 * Perform initialization only if slave status is SDW_SLAVE_ATTACHED
	 */
	if (max98373->hw_init || status != SDW_SLAVE_ATTACHED)
		return 0;

	/* perform I/O transfers required for Slave initialization */
	return max98373_io_init(slave);
}

static int max98373_bus_config(struct sdw_slave *slave,
			       struct sdw_bus_params *params)
{
	int ret;

	ret = max98373_clock_config(slave, params);
	if (ret < 0)
		dev_err(&slave->dev, "Invalid clk config");

	return ret;
}

/*
 * slave_ops: callbacks for get_clock_stop_mode, clock_stop and
 * port_prep are not defined for now
 */
static struct sdw_slave_ops max98373_slave_ops = {
	.read_prop = max98373_read_prop,
	.update_status = max98373_update_status,
	.bus_config = max98373_bus_config,
};

static int max98373_sdw_probe(struct sdw_slave *slave,
			      const struct sdw_device_id *id)
{
	struct regmap *regmap;

	/* Regmap Initialization */
	regmap = devm_regmap_init_sdw(slave, &max98373_sdw_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return max98373_init(slave, regmap);
}

static int max98373_sdw_remove(struct sdw_slave *slave)
{
	struct max98373_priv *max98373 = dev_get_drvdata(&slave->dev);

	if (max98373->first_hw_init)
		pm_runtime_disable(&slave->dev);

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id max98373_of_match[] = {
	{ .compatible = "maxim,max98373", },
	{},
};
MODULE_DEVICE_TABLE(of, max98373_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id max98373_acpi_match[] = {
	{ "MX98373", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, max98373_acpi_match);
#endif

static const struct sdw_device_id max98373_id[] = {
	SDW_SLAVE_ENTRY(0x019F, 0x8373, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, max98373_id);

static struct sdw_driver max98373_sdw_driver = {
	.driver = {
		.name = "max98373",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(max98373_of_match),
		.acpi_match_table = ACPI_PTR(max98373_acpi_match),
		.pm = &max98373_pm,
	},
	.probe = max98373_sdw_probe,
	.remove = max98373_sdw_remove,
	.ops = &max98373_slave_ops,
	.id_table = max98373_id,
};

module_sdw_driver(max98373_sdw_driver);

MODULE_DESCRIPTION("ASoC MAX98373 driver SDW");
MODULE_AUTHOR("Oleg Sherbakov <oleg.sherbakov@maximintegrated.com>");
MODULE_LICENSE("GPL v2");
