// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022, Analog Devices Inc.

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <sound/pcm_params.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>
#include <linux/gpio.h>
#include <sound/tlv.h>
#include "max98396.h"

static const char * const max98396_core_supplies[MAX98396_NUM_CORE_SUPPLIES] = {
	"avdd",
	"dvdd",
	"dvddio",
};

static struct reg_default max98396_reg[] = {
	{MAX98396_R2000_SW_RESET, 0x00},
	{MAX98396_R2001_INT_RAW1, 0x00},
	{MAX98396_R2002_INT_RAW2, 0x00},
	{MAX98396_R2003_INT_RAW3, 0x00},
	{MAX98396_R2004_INT_RAW4, 0x00},
	{MAX98396_R2006_INT_STATE1, 0x00},
	{MAX98396_R2007_INT_STATE2, 0x00},
	{MAX98396_R2008_INT_STATE3, 0x00},
	{MAX98396_R2009_INT_STATE4, 0x00},
	{MAX98396_R200B_INT_FLAG1, 0x00},
	{MAX98396_R200C_INT_FLAG2, 0x00},
	{MAX98396_R200D_INT_FLAG3, 0x00},
	{MAX98396_R200E_INT_FLAG4, 0x00},
	{MAX98396_R2010_INT_EN1, 0x02},
	{MAX98396_R2011_INT_EN2, 0x00},
	{MAX98396_R2012_INT_EN3, 0x00},
	{MAX98396_R2013_INT_EN4, 0x00},
	{MAX98396_R2015_INT_FLAG_CLR1, 0x00},
	{MAX98396_R2016_INT_FLAG_CLR2, 0x00},
	{MAX98396_R2017_INT_FLAG_CLR3, 0x00},
	{MAX98396_R2018_INT_FLAG_CLR4, 0x00},
	{MAX98396_R201F_IRQ_CTRL, 0x00},
	{MAX98396_R2020_THERM_WARN_THRESH, 0x46},
	{MAX98396_R2021_THERM_WARN_THRESH2, 0x46},
	{MAX98396_R2022_THERM_SHDN_THRESH, 0x64},
	{MAX98396_R2023_THERM_HYSTERESIS, 0x02},
	{MAX98396_R2024_THERM_FOLDBACK_SET, 0xC5},
	{MAX98396_R2027_THERM_FOLDBACK_EN, 0x01},
	{MAX98396_R2030_NOISEGATE_MODE_CTRL, 0x32},
	{MAX98396_R2033_NOISEGATE_MODE_EN, 0x00},
	{MAX98396_R2038_CLK_MON_CTRL, 0x00},
	{MAX98396_R2039_DATA_MON_CTRL, 0x00},
	{MAX98396_R203F_ENABLE_CTRLS, 0x0F},
	{MAX98396_R2040_PIN_CFG, 0x55},
	{MAX98396_R2041_PCM_MODE_CFG, 0xC0},
	{MAX98396_R2042_PCM_CLK_SETUP, 0x04},
	{MAX98396_R2043_PCM_SR_SETUP, 0x88},
	{MAX98396_R2044_PCM_TX_CTRL_1, 0x00},
	{MAX98396_R2045_PCM_TX_CTRL_2, 0x00},
	{MAX98396_R2046_PCM_TX_CTRL_3, 0x00},
	{MAX98396_R2047_PCM_TX_CTRL_4, 0x00},
	{MAX98396_R2048_PCM_TX_CTRL_5, 0x00},
	{MAX98396_R2049_PCM_TX_CTRL_6, 0x00},
	{MAX98396_R204A_PCM_TX_CTRL_7, 0x00},
	{MAX98396_R204B_PCM_TX_CTRL_8, 0x00},
	{MAX98396_R204C_PCM_TX_HIZ_CTRL_1, 0xFF},
	{MAX98396_R204D_PCM_TX_HIZ_CTRL_2, 0xFF},
	{MAX98396_R204E_PCM_TX_HIZ_CTRL_3, 0xFF},
	{MAX98396_R204F_PCM_TX_HIZ_CTRL_4, 0xFF},
	{MAX98396_R2050_PCM_TX_HIZ_CTRL_5, 0xFF},
	{MAX98396_R2051_PCM_TX_HIZ_CTRL_6, 0xFF},
	{MAX98396_R2052_PCM_TX_HIZ_CTRL_7, 0xFF},
	{MAX98396_R2053_PCM_TX_HIZ_CTRL_8, 0xFF},
	{MAX98396_R2055_PCM_RX_SRC1, 0x00},
	{MAX98396_R2056_PCM_RX_SRC2, 0x00},
	{MAX98396_R2058_PCM_BYPASS_SRC, 0x00},
	{MAX98396_R205D_PCM_TX_SRC_EN, 0x00},
	{MAX98396_R205E_PCM_RX_EN, 0x00},
	{MAX98396_R205F_PCM_TX_EN, 0x00},
	{MAX98396_R2070_ICC_RX_EN_A, 0x00},
	{MAX98396_R2071_ICC_RX_EN_B, 0x00},
	{MAX98396_R2072_ICC_TX_CTRL, 0x00},
	{MAX98396_R207F_ICC_EN, 0x00},
	{MAX98396_R2083_TONE_GEN_DC_CFG, 0x04},
	{MAX98396_R2084_TONE_GEN_DC_LVL1, 0x00},
	{MAX98396_R2085_TONE_GEN_DC_LVL2, 0x00},
	{MAX98396_R2086_TONE_GEN_DC_LVL3, 0x00},
	{MAX98396_R208F_TONE_GEN_EN, 0x00},
	{MAX98396_R2090_AMP_VOL_CTRL, 0x00},
	{MAX98396_R2091_AMP_PATH_GAIN, 0x0B},
	{MAX98396_R2092_AMP_DSP_CFG, 0x23},
	{MAX98396_R2093_SSM_CFG, 0x0D},
	{MAX98396_R2094_SPK_CLS_DG_THRESH, 0x12},
	{MAX98396_R2095_SPK_CLS_DG_HDR, 0x17},
	{MAX98396_R2096_SPK_CLS_DG_HOLD_TIME, 0x17},
	{MAX98396_R2097_SPK_CLS_DG_DELAY, 0x00},
	{MAX98396_R2098_SPK_CLS_DG_MODE, 0x00},
	{MAX98396_R2099_SPK_CLS_DG_VBAT_LVL, 0x03},
	{MAX98396_R209A_SPK_EDGE_CTRL, 0x00},
	{MAX98396_R209C_SPK_EDGE_CTRL1, 0x0A},
	{MAX98396_R209D_SPK_EDGE_CTRL2, 0xAA},
	{MAX98396_R209E_AMP_CLIP_GAIN, 0x00},
	{MAX98396_R209F_BYPASS_PATH_CFG, 0x00},
	{MAX98396_R20A0_AMP_SUPPLY_CTL, 0x00},
	{MAX98396_R20AF_AMP_EN, 0x00},
	{MAX98396_R20B0_ADC_SR, 0x30},
	{MAX98396_R20B1_ADC_PVDD_CFG, 0x00},
	{MAX98396_R20B2_ADC_VBAT_CFG, 0x00},
	{MAX98396_R20B3_ADC_THERMAL_CFG, 0x00},
	{MAX98396_R20B4_ADC_READBACK_CTRL1, 0x00},
	{MAX98396_R20B5_ADC_READBACK_CTRL2, 0x00},
	{MAX98396_R20B6_ADC_PVDD_READBACK_MSB, 0x00},
	{MAX98396_R20B7_ADC_PVDD_READBACK_LSB, 0x00},
	{MAX98396_R20B8_ADC_VBAT_READBACK_MSB, 0x00},
	{MAX98396_R20B9_ADC_VBAT_READBACK_LSB, 0x00},
	{MAX98396_R20BA_ADC_TEMP_READBACK_MSB, 0x00},
	{MAX98396_R20BB_ADC_TEMP_READBACK_LSB, 0x00},
	{MAX98396_R20BC_ADC_LO_PVDD_READBACK_MSB, 0x00},
	{MAX98396_R20BD_ADC_LO_PVDD_READBACK_LSB, 0x00},
	{MAX98396_R20BE_ADC_LO_VBAT_READBACK_MSB, 0x00},
	{MAX98396_R20BF_ADC_LO_VBAT_READBACK_LSB, 0x00},
	{MAX98396_R20C7_ADC_CFG, 0x00},
	{MAX98396_R20D0_DHT_CFG1, 0x00},
	{MAX98396_R20D1_LIMITER_CFG1, 0x08},
	{MAX98396_R20D2_LIMITER_CFG2, 0x00},
	{MAX98396_R20D3_DHT_CFG2, 0x14},
	{MAX98396_R20D4_DHT_CFG3, 0x02},
	{MAX98396_R20D5_DHT_CFG4, 0x04},
	{MAX98396_R20D6_DHT_HYSTERESIS_CFG, 0x07},
	{MAX98396_R20DF_DHT_EN, 0x00},
	{MAX98396_R20E0_IV_SENSE_PATH_CFG, 0x04},
	{MAX98396_R20E4_IV_SENSE_PATH_EN, 0x00},
	{MAX98396_R20E5_BPE_STATE, 0x00},
	{MAX98396_R20E6_BPE_L3_THRESH_MSB, 0x00},
	{MAX98396_R20E7_BPE_L3_THRESH_LSB, 0x00},
	{MAX98396_R20E8_BPE_L2_THRESH_MSB, 0x00},
	{MAX98396_R20E9_BPE_L2_THRESH_LSB, 0x00},
	{MAX98396_R20EA_BPE_L1_THRESH_MSB, 0x00},
	{MAX98396_R20EB_BPE_L1_THRESH_LSB, 0x00},
	{MAX98396_R20EC_BPE_L0_THRESH_MSB, 0x00},
	{MAX98396_R20ED_BPE_L0_THRESH_LSB, 0x00},
	{MAX98396_R20EE_BPE_L3_DWELL_HOLD_TIME, 0x00},
	{MAX98396_R20EF_BPE_L2_DWELL_HOLD_TIME, 0x00},
	{MAX98396_R20F0_BPE_L1_DWELL_HOLD_TIME, 0x00},
	{MAX98396_R20F1_BPE_L0_HOLD_TIME, 0x00},
	{MAX98396_R20F2_BPE_L3_ATTACK_REL_STEP, 0x00},
	{MAX98396_R20F3_BPE_L2_ATTACK_REL_STEP, 0x00},
	{MAX98396_R20F4_BPE_L1_ATTACK_REL_STEP, 0x00},
	{MAX98396_R20F5_BPE_L0_ATTACK_REL_STEP, 0x00},
	{MAX98396_R20F6_BPE_L3_MAX_GAIN_ATTN, 0x00},
	{MAX98396_R20F7_BPE_L2_MAX_GAIN_ATTN, 0x00},
	{MAX98396_R20F8_BPE_L1_MAX_GAIN_ATTN, 0x00},
	{MAX98396_R20F9_BPE_L0_MAX_GAIN_ATTN, 0x00},
	{MAX98396_R20FA_BPE_L3_ATT_REL_RATE, 0x00},
	{MAX98396_R20FB_BPE_L2_ATT_REL_RATE, 0x00},
	{MAX98396_R20FC_BPE_L1_ATT_REL_RATE, 0x00},
	{MAX98396_R20FD_BPE_L0_ATT_REL_RATE, 0x00},
	{MAX98396_R20FE_BPE_L3_LIMITER_CFG, 0x00},
	{MAX98396_R20FF_BPE_L2_LIMITER_CFG, 0x00},
	{MAX98396_R2100_BPE_L1_LIMITER_CFG, 0x00},
	{MAX98396_R2101_BPE_L0_LIMITER_CFG, 0x00},
	{MAX98396_R2102_BPE_L3_LIM_ATT_REL_RATE, 0x00},
	{MAX98396_R2103_BPE_L2_LIM_ATT_REL_RATE, 0x00},
	{MAX98396_R2104_BPE_L1_LIM_ATT_REL_RATE, 0x00},
	{MAX98396_R2105_BPE_L0_LIM_ATT_REL_RATE, 0x00},
	{MAX98396_R2106_BPE_THRESH_HYSTERESIS, 0x00},
	{MAX98396_R2107_BPE_INFINITE_HOLD_CLR, 0x00},
	{MAX98396_R2108_BPE_SUPPLY_SRC, 0x00},
	{MAX98396_R2109_BPE_LOW_STATE, 0x00},
	{MAX98396_R210A_BPE_LOW_GAIN, 0x00},
	{MAX98396_R210B_BPE_LOW_LIMITER, 0x00},
	{MAX98396_R210D_BPE_EN, 0x00},
	{MAX98396_R210E_AUTO_RESTART, 0x00},
	{MAX98396_R210F_GLOBAL_EN, 0x00},
	{MAX98396_R21FF_REVISION_ID, 0x00},
};

static struct reg_default max98397_reg[] = {
	{MAX98396_R2000_SW_RESET, 0x00},
	{MAX98396_R2001_INT_RAW1, 0x00},
	{MAX98396_R2002_INT_RAW2, 0x00},
	{MAX98396_R2003_INT_RAW3, 0x00},
	{MAX98396_R2004_INT_RAW4, 0x00},
	{MAX98396_R2006_INT_STATE1, 0x00},
	{MAX98396_R2007_INT_STATE2, 0x00},
	{MAX98396_R2008_INT_STATE3, 0x00},
	{MAX98396_R2009_INT_STATE4, 0x00},
	{MAX98396_R200B_INT_FLAG1, 0x00},
	{MAX98396_R200C_INT_FLAG2, 0x00},
	{MAX98396_R200D_INT_FLAG3, 0x00},
	{MAX98396_R200E_INT_FLAG4, 0x00},
	{MAX98396_R2010_INT_EN1, 0x02},
	{MAX98396_R2011_INT_EN2, 0x00},
	{MAX98396_R2012_INT_EN3, 0x00},
	{MAX98396_R2013_INT_EN4, 0x00},
	{MAX98396_R2015_INT_FLAG_CLR1, 0x00},
	{MAX98396_R2016_INT_FLAG_CLR2, 0x00},
	{MAX98396_R2017_INT_FLAG_CLR3, 0x00},
	{MAX98396_R2018_INT_FLAG_CLR4, 0x00},
	{MAX98396_R201F_IRQ_CTRL, 0x00},
	{MAX98396_R2020_THERM_WARN_THRESH, 0x46},
	{MAX98396_R2021_THERM_WARN_THRESH2, 0x46},
	{MAX98396_R2022_THERM_SHDN_THRESH, 0x64},
	{MAX98396_R2023_THERM_HYSTERESIS, 0x02},
	{MAX98396_R2024_THERM_FOLDBACK_SET, 0xC5},
	{MAX98396_R2027_THERM_FOLDBACK_EN, 0x01},
	{MAX98396_R2030_NOISEGATE_MODE_CTRL, 0x32},
	{MAX98396_R2033_NOISEGATE_MODE_EN, 0x00},
	{MAX98396_R2038_CLK_MON_CTRL, 0x00},
	{MAX98396_R2039_DATA_MON_CTRL, 0x00},
	{MAX98397_R203A_SPK_MON_THRESH, 0x03},
	{MAX98396_R203F_ENABLE_CTRLS, 0x0F},
	{MAX98396_R2040_PIN_CFG, 0x55},
	{MAX98396_R2041_PCM_MODE_CFG, 0xC0},
	{MAX98396_R2042_PCM_CLK_SETUP, 0x04},
	{MAX98396_R2043_PCM_SR_SETUP, 0x88},
	{MAX98396_R2044_PCM_TX_CTRL_1, 0x00},
	{MAX98396_R2045_PCM_TX_CTRL_2, 0x00},
	{MAX98396_R2046_PCM_TX_CTRL_3, 0x00},
	{MAX98396_R2047_PCM_TX_CTRL_4, 0x00},
	{MAX98396_R2048_PCM_TX_CTRL_5, 0x00},
	{MAX98396_R2049_PCM_TX_CTRL_6, 0x00},
	{MAX98396_R204A_PCM_TX_CTRL_7, 0x00},
	{MAX98396_R204B_PCM_TX_CTRL_8, 0x00},
	{MAX98397_R204C_PCM_TX_CTRL_9, 0x00},
	{MAX98397_R204D_PCM_TX_HIZ_CTRL_1, 0xFF},
	{MAX98397_R204E_PCM_TX_HIZ_CTRL_2, 0xFF},
	{MAX98397_R204F_PCM_TX_HIZ_CTRL_3, 0xFF},
	{MAX98397_R2050_PCM_TX_HIZ_CTRL_4, 0xFF},
	{MAX98397_R2051_PCM_TX_HIZ_CTRL_5, 0xFF},
	{MAX98397_R2052_PCM_TX_HIZ_CTRL_6, 0xFF},
	{MAX98397_R2053_PCM_TX_HIZ_CTRL_7, 0xFF},
	{MAX98397_R2054_PCM_TX_HIZ_CTRL_8, 0xFF},
	{MAX98397_R2056_PCM_RX_SRC1, 0x00},
	{MAX98397_R2057_PCM_RX_SRC2, 0x00},
	{MAX98396_R2058_PCM_BYPASS_SRC, 0x00},
	{MAX98396_R205D_PCM_TX_SRC_EN, 0x00},
	{MAX98396_R205E_PCM_RX_EN, 0x00},
	{MAX98396_R205F_PCM_TX_EN, 0x00},
	{MAX98397_R2060_PCM_TX_SUPPLY_SEL, 0x00},
	{MAX98396_R2070_ICC_RX_EN_A, 0x00},
	{MAX98396_R2071_ICC_RX_EN_B, 0x00},
	{MAX98396_R2072_ICC_TX_CTRL, 0x00},
	{MAX98396_R207F_ICC_EN, 0x00},
	{MAX98396_R2083_TONE_GEN_DC_CFG, 0x04},
	{MAX98396_R2084_TONE_GEN_DC_LVL1, 0x00},
	{MAX98396_R2085_TONE_GEN_DC_LVL2, 0x00},
	{MAX98396_R2086_TONE_GEN_DC_LVL3, 0x00},
	{MAX98396_R208F_TONE_GEN_EN, 0x00},
	{MAX98396_R2090_AMP_VOL_CTRL, 0x00},
	{MAX98396_R2091_AMP_PATH_GAIN, 0x12},
	{MAX98396_R2092_AMP_DSP_CFG, 0x22},
	{MAX98396_R2093_SSM_CFG, 0x08},
	{MAX98396_R2094_SPK_CLS_DG_THRESH, 0x12},
	{MAX98396_R2095_SPK_CLS_DG_HDR, 0x17},
	{MAX98396_R2096_SPK_CLS_DG_HOLD_TIME, 0x17},
	{MAX98396_R2097_SPK_CLS_DG_DELAY, 0x00},
	{MAX98396_R2098_SPK_CLS_DG_MODE, 0x00},
	{MAX98396_R2099_SPK_CLS_DG_VBAT_LVL, 0x03},
	{MAX98396_R209A_SPK_EDGE_CTRL, 0x00},
	{MAX98397_R209B_SPK_PATH_WB_ONLY, 0x00},
	{MAX98396_R209C_SPK_EDGE_CTRL1, 0x03},
	{MAX98396_R209D_SPK_EDGE_CTRL2, 0xFC},
	{MAX98396_R209E_AMP_CLIP_GAIN, 0x00},
	{MAX98396_R209F_BYPASS_PATH_CFG, 0x00},
	{MAX98396_R20AF_AMP_EN, 0x00},
	{MAX98396_R20B0_ADC_SR, 0x30},
	{MAX98396_R20B1_ADC_PVDD_CFG, 0x00},
	{MAX98396_R20B2_ADC_VBAT_CFG, 0x00},
	{MAX98396_R20B3_ADC_THERMAL_CFG, 0x00},
	{MAX98397_R20B4_ADC_VDDH_CFG, 0x00},
	{MAX98397_R20B5_ADC_READBACK_CTRL1, 0x00},
	{MAX98397_R20B6_ADC_READBACK_CTRL2, 0x00},
	{MAX98397_R20B7_ADC_PVDD_READBACK_MSB, 0x00},
	{MAX98397_R20B8_ADC_PVDD_READBACK_LSB, 0x00},
	{MAX98397_R20B9_ADC_VBAT_READBACK_MSB, 0x00},
	{MAX98397_R20BA_ADC_VBAT_READBACK_LSB, 0x00},
	{MAX98397_R20BB_ADC_TEMP_READBACK_MSB, 0x00},
	{MAX98397_R20BC_ADC_TEMP_READBACK_LSB, 0x00},
	{MAX98397_R20BD_ADC_VDDH__READBACK_MSB, 0x00},
	{MAX98397_R20BE_ADC_VDDH_READBACK_LSB, 0x00},
	{MAX98396_R20BF_ADC_LO_VBAT_READBACK_LSB, 0x00},
	{MAX98397_R20C3_ADC_LO_VDDH_READBACK_MSB, 0x00},
	{MAX98397_R20C4_ADC_LO_VDDH_READBACK_LSB, 0x00},
	{MAX98397_R20C5_MEAS_ADC_OPTIMAL_MODE, 0x04},
	{MAX98396_R20C7_ADC_CFG, 0x00},
	{MAX98396_R20D0_DHT_CFG1, 0x00},
	{MAX98396_R20D1_LIMITER_CFG1, 0x08},
	{MAX98396_R20D2_LIMITER_CFG2, 0x00},
	{MAX98396_R20D3_DHT_CFG2, 0x14},
	{MAX98396_R20D4_DHT_CFG3, 0x02},
	{MAX98396_R20D5_DHT_CFG4, 0x04},
	{MAX98396_R20D6_DHT_HYSTERESIS_CFG, 0x07},
	{MAX98396_R20DF_DHT_EN, 0x00},
	{MAX98396_R20E0_IV_SENSE_PATH_CFG, 0x04},
	{MAX98396_R20E4_IV_SENSE_PATH_EN, 0x00},
	{MAX98396_R20E5_BPE_STATE, 0x00},
	{MAX98396_R20E6_BPE_L3_THRESH_MSB, 0x00},
	{MAX98396_R20E7_BPE_L3_THRESH_LSB, 0x00},
	{MAX98396_R20E8_BPE_L2_THRESH_MSB, 0x00},
	{MAX98396_R20E9_BPE_L2_THRESH_LSB, 0x00},
	{MAX98396_R20EA_BPE_L1_THRESH_MSB, 0x00},
	{MAX98396_R20EB_BPE_L1_THRESH_LSB, 0x00},
	{MAX98396_R20EC_BPE_L0_THRESH_MSB, 0x00},
	{MAX98396_R20ED_BPE_L0_THRESH_LSB, 0x00},
	{MAX98396_R20EE_BPE_L3_DWELL_HOLD_TIME, 0x00},
	{MAX98396_R20EF_BPE_L2_DWELL_HOLD_TIME, 0x00},
	{MAX98396_R20F0_BPE_L1_DWELL_HOLD_TIME, 0x00},
	{MAX98396_R20F1_BPE_L0_HOLD_TIME, 0x00},
	{MAX98396_R20F2_BPE_L3_ATTACK_REL_STEP, 0x00},
	{MAX98396_R20F3_BPE_L2_ATTACK_REL_STEP, 0x00},
	{MAX98396_R20F4_BPE_L1_ATTACK_REL_STEP, 0x00},
	{MAX98396_R20F5_BPE_L0_ATTACK_REL_STEP, 0x00},
	{MAX98396_R20F6_BPE_L3_MAX_GAIN_ATTN, 0x00},
	{MAX98396_R20F7_BPE_L2_MAX_GAIN_ATTN, 0x00},
	{MAX98396_R20F8_BPE_L1_MAX_GAIN_ATTN, 0x00},
	{MAX98396_R20F9_BPE_L0_MAX_GAIN_ATTN, 0x00},
	{MAX98396_R20FA_BPE_L3_ATT_REL_RATE, 0x00},
	{MAX98396_R20FB_BPE_L2_ATT_REL_RATE, 0x00},
	{MAX98396_R20FC_BPE_L1_ATT_REL_RATE, 0x00},
	{MAX98396_R20FD_BPE_L0_ATT_REL_RATE, 0x00},
	{MAX98396_R20FE_BPE_L3_LIMITER_CFG, 0x00},
	{MAX98396_R20FF_BPE_L2_LIMITER_CFG, 0x00},
	{MAX98396_R2100_BPE_L1_LIMITER_CFG, 0x00},
	{MAX98396_R2101_BPE_L0_LIMITER_CFG, 0x00},
	{MAX98396_R2102_BPE_L3_LIM_ATT_REL_RATE, 0x00},
	{MAX98396_R2103_BPE_L2_LIM_ATT_REL_RATE, 0x00},
	{MAX98396_R2104_BPE_L1_LIM_ATT_REL_RATE, 0x00},
	{MAX98396_R2105_BPE_L0_LIM_ATT_REL_RATE, 0x00},
	{MAX98396_R2106_BPE_THRESH_HYSTERESIS, 0x00},
	{MAX98396_R2107_BPE_INFINITE_HOLD_CLR, 0x00},
	{MAX98396_R2108_BPE_SUPPLY_SRC, 0x00},
	{MAX98396_R2109_BPE_LOW_STATE, 0x00},
	{MAX98396_R210A_BPE_LOW_GAIN, 0x00},
	{MAX98396_R210B_BPE_LOW_LIMITER, 0x00},
	{MAX98396_R210D_BPE_EN, 0x00},
	{MAX98396_R210E_AUTO_RESTART, 0x00},
	{MAX98396_R210F_GLOBAL_EN, 0x00},
	{MAX98397_R22FF_REVISION_ID, 0x00},
};

static void max98396_global_enable_onoff(struct regmap *regmap, bool onoff)
{
	regmap_write(regmap, MAX98396_R210F_GLOBAL_EN, onoff ? 1 : 0);
	usleep_range(11000, 12000);
}

static int max98396_dai_set_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct max98396_priv *max98396 = snd_soc_component_get_drvdata(component);
	unsigned int format_mask, format = 0;
	unsigned int bclk_pol = 0;
	int ret, status;
	int reg;
	bool update = false;

	format_mask = MAX98396_PCM_MODE_CFG_FORMAT_MASK |
		      MAX98396_PCM_MODE_CFG_LRCLKEDGE;

	dev_dbg(component->dev, "%s: fmt 0x%08X\n", __func__, fmt);

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_NB_IF:
		format = MAX98396_PCM_MODE_CFG_LRCLKEDGE;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		bclk_pol = MAX98396_PCM_MODE_CFG_BCLKEDGE;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		bclk_pol = MAX98396_PCM_MODE_CFG_BCLKEDGE;
		format = MAX98396_PCM_MODE_CFG_LRCLKEDGE;
		break;

	default:
		dev_err(component->dev, "DAI invert mode %d unsupported\n",
			fmt & SND_SOC_DAIFMT_INV_MASK);
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format |= MAX98396_PCM_FORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format |= MAX98396_PCM_FORMAT_LJ;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		format |= MAX98396_PCM_FORMAT_TDM_MODE1;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		format |= MAX98396_PCM_FORMAT_TDM_MODE0;
		break;
	default:
		dev_err(component->dev, "DAI format %d unsupported\n",
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	ret = regmap_read(max98396->regmap, MAX98396_R210F_GLOBAL_EN, &status);
	if (ret < 0)
		return -EINVAL;

	if (status) {
		ret = regmap_read(max98396->regmap, MAX98396_R2041_PCM_MODE_CFG, &reg);
		if (ret < 0)
			return -EINVAL;
		if (format != (reg & format_mask)) {
			update = true;
		} else {
			ret = regmap_read(max98396->regmap,
					  MAX98396_R2042_PCM_CLK_SETUP, &reg);
			if (ret < 0)
				return -EINVAL;
			if (bclk_pol != (reg & MAX98396_PCM_MODE_CFG_BCLKEDGE))
				update = true;
		}
		/* GLOBAL_EN OFF prior to pcm mode, clock configuration change */
		if (update)
			max98396_global_enable_onoff(max98396->regmap, false);
	}

	regmap_update_bits(max98396->regmap,
			   MAX98396_R2041_PCM_MODE_CFG,
			   format_mask, format);

	regmap_update_bits(max98396->regmap,
			   MAX98396_R2042_PCM_CLK_SETUP,
			   MAX98396_PCM_MODE_CFG_BCLKEDGE,
			   bclk_pol);

	if (status && update)
		max98396_global_enable_onoff(max98396->regmap, true);

	return 0;
}

#define MAX98396_BSEL_32	0x2
#define MAX98396_BSEL_48	0x3
#define MAX98396_BSEL_64	0x4
#define MAX98396_BSEL_96	0x5
#define MAX98396_BSEL_128	0x6
#define MAX98396_BSEL_192	0x7
#define MAX98396_BSEL_256	0x8
#define MAX98396_BSEL_384	0x9
#define MAX98396_BSEL_512	0xa
#define MAX98396_BSEL_320	0xb
#define MAX98396_BSEL_250	0xc
#define MAX98396_BSEL_125	0xd

/* Refer to table 5 in the datasheet */
static const struct max98396_pcm_config {
	int in, out, width, bsel, max_sr;
} max98396_pcm_configs[] = {
	{ .in = 2,  .out = 4,  .width = 16, .bsel = MAX98396_BSEL_32,  .max_sr = 192000 },
	{ .in = 2,  .out = 6,  .width = 24, .bsel = MAX98396_BSEL_48,  .max_sr = 192000 },
	{ .in = 2,  .out = 8,  .width = 32, .bsel = MAX98396_BSEL_64,  .max_sr = 192000 },
	{ .in = 3,  .out = 15, .width = 32, .bsel = MAX98396_BSEL_125, .max_sr = 192000 },
	{ .in = 4,  .out = 8,  .width = 16, .bsel = MAX98396_BSEL_64,  .max_sr = 192000 },
	{ .in = 4,  .out = 12, .width = 24, .bsel = MAX98396_BSEL_96,  .max_sr = 192000 },
	{ .in = 4,  .out = 16, .width = 32, .bsel = MAX98396_BSEL_128, .max_sr = 192000 },
	{ .in = 5,  .out = 15, .width = 24, .bsel = MAX98396_BSEL_125, .max_sr = 192000 },
	{ .in = 7,  .out = 15, .width = 16, .bsel = MAX98396_BSEL_125, .max_sr = 192000 },
	{ .in = 2,  .out = 4,  .width = 16, .bsel = MAX98396_BSEL_32,  .max_sr = 96000  },
	{ .in = 2,  .out = 6,  .width = 24, .bsel = MAX98396_BSEL_48,  .max_sr = 96000  },
	{ .in = 2,  .out = 8,  .width = 32, .bsel = MAX98396_BSEL_64,  .max_sr = 96000  },
	{ .in = 3,  .out = 15, .width = 32, .bsel = MAX98396_BSEL_125, .max_sr = 96000  },
	{ .in = 4,  .out = 8,  .width = 16, .bsel = MAX98396_BSEL_64,  .max_sr = 96000  },
	{ .in = 4,  .out = 12, .width = 24, .bsel = MAX98396_BSEL_96,  .max_sr = 96000  },
	{ .in = 4,  .out = 16, .width = 32, .bsel = MAX98396_BSEL_128, .max_sr = 96000  },
	{ .in = 5,  .out = 15, .width = 24, .bsel = MAX98396_BSEL_125, .max_sr = 96000  },
	{ .in = 7,  .out = 15, .width = 16, .bsel = MAX98396_BSEL_125, .max_sr = 96000  },
	{ .in = 7,  .out = 31, .width = 32, .bsel = MAX98396_BSEL_250, .max_sr = 96000  },
	{ .in = 8,  .out = 16, .width = 16, .bsel = MAX98396_BSEL_128, .max_sr = 96000  },
	{ .in = 8,  .out = 24, .width = 24, .bsel = MAX98396_BSEL_192, .max_sr = 96000  },
	{ .in = 8,  .out = 32, .width = 32, .bsel = MAX98396_BSEL_256, .max_sr = 96000  },
	{ .in = 10, .out = 31, .width = 24, .bsel = MAX98396_BSEL_250, .max_sr = 96000  },
	{ .in = 15, .out = 31, .width = 16, .bsel = MAX98396_BSEL_250, .max_sr = 96000  },
	{ .in = 16, .out = 32, .width = 16, .bsel = MAX98396_BSEL_256, .max_sr = 96000  },
	{ .in = 7,  .out = 31, .width = 32, .bsel = MAX98396_BSEL_250, .max_sr = 48000  },
	{ .in = 10, .out = 31, .width = 24, .bsel = MAX98396_BSEL_250, .max_sr = 48000  },
	{ .in = 10, .out = 40, .width = 32, .bsel = MAX98396_BSEL_320, .max_sr = 48000  },
	{ .in = 15, .out = 31, .width = 16, .bsel = MAX98396_BSEL_250, .max_sr = 48000  },
	{ .in = 16, .out = 48, .width = 24, .bsel = MAX98396_BSEL_384, .max_sr = 48000  },
	{ .in = 16, .out = 64, .width = 32, .bsel = MAX98396_BSEL_512, .max_sr = 48000  },
};

static int max98396_pcm_config_index(int in_slots, int out_slots, int width)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(max98396_pcm_configs); i++) {
		const struct max98396_pcm_config *c = &max98396_pcm_configs[i];

		if (in_slots == c->in && out_slots <= c->out && width == c->width)
			return i;
	}

	return -1;
}

static int max98396_dai_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct max98396_priv *max98396 = snd_soc_component_get_drvdata(component);
	unsigned int sampling_rate = 0;
	unsigned int chan_sz = 0;
	int ret, reg, status, bsel = 0;
	bool update = false;

	/* pcm mode configuration */
	switch (snd_pcm_format_width(params_format(params))) {
	case 16:
		chan_sz = MAX98396_PCM_MODE_CFG_CHANSZ_16;
		break;
	case 24:
		chan_sz = MAX98396_PCM_MODE_CFG_CHANSZ_24;
		break;
	case 32:
		chan_sz = MAX98396_PCM_MODE_CFG_CHANSZ_32;
		break;
	default:
		dev_err(component->dev, "format unsupported %d\n",
			params_format(params));
		goto err;
	}

	dev_dbg(component->dev, "format supported %d",
		params_format(params));

	/* sampling rate configuration */
	switch (params_rate(params)) {
	case 8000:
		sampling_rate = MAX98396_PCM_SR_8000;
		break;
	case 11025:
		sampling_rate = MAX98396_PCM_SR_11025;
		break;
	case 12000:
		sampling_rate = MAX98396_PCM_SR_12000;
		break;
	case 16000:
		sampling_rate = MAX98396_PCM_SR_16000;
		break;
	case 22050:
		sampling_rate = MAX98396_PCM_SR_22050;
		break;
	case 24000:
		sampling_rate = MAX98396_PCM_SR_24000;
		break;
	case 32000:
		sampling_rate = MAX98396_PCM_SR_32000;
		break;
	case 44100:
		sampling_rate = MAX98396_PCM_SR_44100;
		break;
	case 48000:
		sampling_rate = MAX98396_PCM_SR_48000;
		break;
	case 88200:
		sampling_rate = MAX98396_PCM_SR_88200;
		break;
	case 96000:
		sampling_rate = MAX98396_PCM_SR_96000;
		break;
	case 192000:
		sampling_rate = MAX98396_PCM_SR_192000;
		break;
	default:
		dev_err(component->dev, "rate %d not supported\n",
			params_rate(params));
		goto err;
	}

	if (max98396->tdm_mode) {
		if (params_rate(params) > max98396->tdm_max_samplerate) {
			dev_err(component->dev, "TDM sample rate %d too high",
				params_rate(params));
			goto err;
		}
	} else {
		/* BCLK configuration */
		ret = max98396_pcm_config_index(params_channels(params),
						params_channels(params),
						snd_pcm_format_width(params_format(params)));
		if (ret < 0) {
			dev_err(component->dev,
				"no PCM config for %d channels, format %d\n",
				params_channels(params), params_format(params));
			goto err;
		}

		bsel = max98396_pcm_configs[ret].bsel;

		if (params_rate(params) > max98396_pcm_configs[ret].max_sr) {
			dev_err(component->dev, "sample rate %d too high",
				params_rate(params));
			goto err;
		}
	}

	ret = regmap_read(max98396->regmap, MAX98396_R210F_GLOBAL_EN, &status);
	if (ret < 0)
		goto err;

	if (status) {
		ret = regmap_read(max98396->regmap, MAX98396_R2041_PCM_MODE_CFG, &reg);
		if (ret < 0)
			goto err;
		if (chan_sz != (reg & MAX98396_PCM_MODE_CFG_CHANSZ_MASK)) {
			update = true;
		} else {
			ret = regmap_read(max98396->regmap, MAX98396_R2043_PCM_SR_SETUP, &reg);
			if (ret < 0)
				goto err;
			if (sampling_rate != (reg & MAX98396_PCM_SR_MASK))
				update = true;
		}

		/* GLOBAL_EN OFF prior to channel size and sampling rate change */
		if (update)
			max98396_global_enable_onoff(max98396->regmap, false);
	}

	/* set channel size */
	regmap_update_bits(max98396->regmap, MAX98396_R2041_PCM_MODE_CFG,
			   MAX98396_PCM_MODE_CFG_CHANSZ_MASK, chan_sz);

	/* set DAI_SR to correct LRCLK frequency */
	regmap_update_bits(max98396->regmap, MAX98396_R2043_PCM_SR_SETUP,
			   MAX98396_PCM_SR_MASK, sampling_rate);

	/* set sampling rate of IV */
	if (max98396->interleave_mode &&
	    sampling_rate > MAX98396_PCM_SR_16000)
		regmap_update_bits(max98396->regmap,
				   MAX98396_R2043_PCM_SR_SETUP,
				   MAX98396_IVADC_SR_MASK,
				   (sampling_rate - 3)
				   << MAX98396_IVADC_SR_SHIFT);
	else
		regmap_update_bits(max98396->regmap,
				   MAX98396_R2043_PCM_SR_SETUP,
				   MAX98396_IVADC_SR_MASK,
				   sampling_rate << MAX98396_IVADC_SR_SHIFT);

	if (bsel)
		regmap_update_bits(max98396->regmap,
				MAX98396_R2042_PCM_CLK_SETUP,
				MAX98396_PCM_CLK_SETUP_BSEL_MASK,
				bsel);

	if (status && update)
		max98396_global_enable_onoff(max98396->regmap, true);

	return 0;

err:
	return -EINVAL;
}

static int max98396_dai_tdm_slot(struct snd_soc_dai *dai,
				 unsigned int tx_mask, unsigned int rx_mask,
				 int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct max98396_priv *max98396 =
		snd_soc_component_get_drvdata(component);
	int bsel;
	unsigned int chan_sz = 0;
	int ret, status;
	int reg;
	bool update = false;

	if (!tx_mask && !rx_mask && !slots && !slot_width)
		max98396->tdm_mode = false;
	else
		max98396->tdm_mode = true;

	/* BCLK configuration */
	ret = max98396_pcm_config_index(slots, slots, slot_width);
	if (ret < 0) {
		dev_err(component->dev, "no TDM config for %d slots %d bits\n",
			slots, slot_width);
		return -EINVAL;
	}

	bsel = max98396_pcm_configs[ret].bsel;
	max98396->tdm_max_samplerate = max98396_pcm_configs[ret].max_sr;

	/* Channel size configuration */
	switch (slot_width) {
	case 16:
		chan_sz = MAX98396_PCM_MODE_CFG_CHANSZ_16;
		break;
	case 24:
		chan_sz = MAX98396_PCM_MODE_CFG_CHANSZ_24;
		break;
	case 32:
		chan_sz = MAX98396_PCM_MODE_CFG_CHANSZ_32;
		break;
	default:
		dev_err(component->dev, "slot width %d unsupported\n",
			slot_width);
		return -EINVAL;
	}

	ret = regmap_read(max98396->regmap, MAX98396_R210F_GLOBAL_EN, &status);
	if (ret < 0)
		return -EINVAL;

	if (status) {
		ret = regmap_read(max98396->regmap, MAX98396_R2042_PCM_CLK_SETUP, &reg);
		if (ret < 0)
			return -EINVAL;
		if (bsel != (reg & MAX98396_PCM_CLK_SETUP_BSEL_MASK)) {
			update = true;
		} else {
			ret = regmap_read(max98396->regmap, MAX98396_R2041_PCM_MODE_CFG, &reg);
			if (ret < 0)
				return -EINVAL;
			if (chan_sz != (reg & MAX98396_PCM_MODE_CFG_CHANSZ_MASK))
				update = true;
		}

		/* GLOBAL_EN OFF prior to channel size and BCLK per LRCLK change */
		if (update)
			max98396_global_enable_onoff(max98396->regmap, false);
	}

	regmap_update_bits(max98396->regmap,
			   MAX98396_R2042_PCM_CLK_SETUP,
			   MAX98396_PCM_CLK_SETUP_BSEL_MASK,
			   bsel);

	regmap_update_bits(max98396->regmap,
			   MAX98396_R2041_PCM_MODE_CFG,
			   MAX98396_PCM_MODE_CFG_CHANSZ_MASK, chan_sz);

	/* Rx slot configuration */
	if (max98396->device_id == CODEC_TYPE_MAX98396) {
		regmap_update_bits(max98396->regmap,
				   MAX98396_R2056_PCM_RX_SRC2,
				   MAX98396_PCM_DMIX_CH0_SRC_MASK,
				   rx_mask);
		regmap_update_bits(max98396->regmap,
				   MAX98396_R2056_PCM_RX_SRC2,
				   MAX98396_PCM_DMIX_CH1_SRC_MASK,
				   rx_mask << MAX98396_PCM_DMIX_CH1_SHIFT);
	} else {
		regmap_update_bits(max98396->regmap,
				   MAX98397_R2057_PCM_RX_SRC2,
				   MAX98396_PCM_DMIX_CH0_SRC_MASK,
				   rx_mask);
		regmap_update_bits(max98396->regmap,
				   MAX98397_R2057_PCM_RX_SRC2,
				   MAX98396_PCM_DMIX_CH1_SRC_MASK,
				   rx_mask << MAX98396_PCM_DMIX_CH1_SHIFT);
	}

	/* Tx slot Hi-Z configuration */
	if (max98396->device_id == CODEC_TYPE_MAX98396) {
		regmap_write(max98396->regmap,
			     MAX98396_R2053_PCM_TX_HIZ_CTRL_8,
			     ~tx_mask & 0xFF);
		regmap_write(max98396->regmap,
			     MAX98396_R2052_PCM_TX_HIZ_CTRL_7,
			     (~tx_mask & 0xFF00) >> 8);
	} else {
		regmap_write(max98396->regmap,
			     MAX98397_R2054_PCM_TX_HIZ_CTRL_8,
			     ~tx_mask & 0xFF);
		regmap_write(max98396->regmap,
			     MAX98397_R2053_PCM_TX_HIZ_CTRL_7,
			     (~tx_mask & 0xFF00) >> 8);
	}

	if (status && update)
		max98396_global_enable_onoff(max98396->regmap, true);

	return 0;
}

#define MAX98396_RATES SNDRV_PCM_RATE_8000_192000

#define MAX98396_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops max98396_dai_ops = {
	.set_fmt = max98396_dai_set_fmt,
	.hw_params = max98396_dai_hw_params,
	.set_tdm_slot = max98396_dai_tdm_slot,
};

static int max98396_dac_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct max98396_priv *max98396 =
		snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		max98396_global_enable_onoff(max98396->regmap, true);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		max98396_global_enable_onoff(max98396->regmap, false);

		max98396->tdm_mode = false;
		break;
	default:
		return 0;
	}
	return 0;
}

static bool max98396_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98396_R2001_INT_RAW1 ... MAX98396_R2004_INT_RAW4:
	case MAX98396_R2006_INT_STATE1 ... MAX98396_R2009_INT_STATE4:
	case MAX98396_R200B_INT_FLAG1 ... MAX98396_R200E_INT_FLAG4:
	case MAX98396_R2010_INT_EN1 ... MAX98396_R2013_INT_EN4:
	case MAX98396_R2015_INT_FLAG_CLR1 ... MAX98396_R2018_INT_FLAG_CLR4:
	case MAX98396_R201F_IRQ_CTRL ... MAX98396_R2024_THERM_FOLDBACK_SET:
	case MAX98396_R2027_THERM_FOLDBACK_EN:
	case MAX98396_R2030_NOISEGATE_MODE_CTRL:
	case MAX98396_R2033_NOISEGATE_MODE_EN:
	case MAX98396_R2038_CLK_MON_CTRL ... MAX98396_R2039_DATA_MON_CTRL:
	case MAX98396_R203F_ENABLE_CTRLS ... MAX98396_R2053_PCM_TX_HIZ_CTRL_8:
	case MAX98396_R2055_PCM_RX_SRC1 ... MAX98396_R2056_PCM_RX_SRC2:
	case MAX98396_R2058_PCM_BYPASS_SRC:
	case MAX98396_R205D_PCM_TX_SRC_EN ... MAX98396_R205F_PCM_TX_EN:
	case MAX98396_R2070_ICC_RX_EN_A... MAX98396_R2072_ICC_TX_CTRL:
	case MAX98396_R207F_ICC_EN:
	case MAX98396_R2083_TONE_GEN_DC_CFG ... MAX98396_R2086_TONE_GEN_DC_LVL3:
	case MAX98396_R208F_TONE_GEN_EN ... MAX98396_R209A_SPK_EDGE_CTRL:
	case MAX98396_R209C_SPK_EDGE_CTRL1 ... MAX98396_R20A0_AMP_SUPPLY_CTL:
	case MAX98396_R20AF_AMP_EN ... MAX98396_R20BF_ADC_LO_VBAT_READBACK_LSB:
	case MAX98396_R20C7_ADC_CFG:
	case MAX98396_R20D0_DHT_CFG1 ... MAX98396_R20D6_DHT_HYSTERESIS_CFG:
	case MAX98396_R20DF_DHT_EN:
	case MAX98396_R20E0_IV_SENSE_PATH_CFG:
	case MAX98396_R20E4_IV_SENSE_PATH_EN
		... MAX98396_R2106_BPE_THRESH_HYSTERESIS:
	case MAX98396_R2108_BPE_SUPPLY_SRC ... MAX98396_R210B_BPE_LOW_LIMITER:
	case MAX98396_R210D_BPE_EN ... MAX98396_R210F_GLOBAL_EN:
	case MAX98396_R21FF_REVISION_ID:
		return true;
	default:
		return false;
	}
};

static bool max98396_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98396_R2000_SW_RESET:
	case MAX98396_R2001_INT_RAW1 ... MAX98396_R200E_INT_FLAG4:
	case MAX98396_R2041_PCM_MODE_CFG:
	case MAX98396_R20B6_ADC_PVDD_READBACK_MSB
		... MAX98396_R20BF_ADC_LO_VBAT_READBACK_LSB:
	case MAX98396_R20E5_BPE_STATE:
	case MAX98396_R2109_BPE_LOW_STATE
		... MAX98396_R210B_BPE_LOW_LIMITER:
	case MAX98396_R210F_GLOBAL_EN:
	case MAX98396_R21FF_REVISION_ID:
		return true;
	default:
		return false;
	}
}

static bool max98397_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98396_R2001_INT_RAW1 ... MAX98396_R2004_INT_RAW4:
	case MAX98396_R2006_INT_STATE1 ... MAX98396_R2009_INT_STATE4:
	case MAX98396_R200B_INT_FLAG1 ... MAX98396_R200E_INT_FLAG4:
	case MAX98396_R2010_INT_EN1 ... MAX98396_R2013_INT_EN4:
	case MAX98396_R2015_INT_FLAG_CLR1 ... MAX98396_R2018_INT_FLAG_CLR4:
	case MAX98396_R201F_IRQ_CTRL ... MAX98396_R2024_THERM_FOLDBACK_SET:
	case MAX98396_R2027_THERM_FOLDBACK_EN:
	case MAX98396_R2030_NOISEGATE_MODE_CTRL:
	case MAX98396_R2033_NOISEGATE_MODE_EN:
	case MAX98396_R2038_CLK_MON_CTRL ... MAX98397_R203A_SPK_MON_THRESH:
	case MAX98396_R203F_ENABLE_CTRLS ... MAX98397_R2054_PCM_TX_HIZ_CTRL_8:
	case MAX98397_R2056_PCM_RX_SRC1... MAX98396_R2058_PCM_BYPASS_SRC:
	case MAX98396_R205D_PCM_TX_SRC_EN ... MAX98397_R2060_PCM_TX_SUPPLY_SEL:
	case MAX98396_R2070_ICC_RX_EN_A... MAX98396_R2072_ICC_TX_CTRL:
	case MAX98396_R207F_ICC_EN:
	case MAX98396_R2083_TONE_GEN_DC_CFG ... MAX98396_R2086_TONE_GEN_DC_LVL3:
	case MAX98396_R208F_TONE_GEN_EN ... MAX98396_R209F_BYPASS_PATH_CFG:
	case MAX98396_R20AF_AMP_EN ... MAX98397_R20C5_MEAS_ADC_OPTIMAL_MODE:
	case MAX98396_R20C7_ADC_CFG:
	case MAX98396_R20D0_DHT_CFG1 ... MAX98396_R20D6_DHT_HYSTERESIS_CFG:
	case MAX98396_R20DF_DHT_EN:
	case MAX98396_R20E0_IV_SENSE_PATH_CFG:
	case MAX98396_R20E4_IV_SENSE_PATH_EN
		... MAX98396_R2106_BPE_THRESH_HYSTERESIS:
	case MAX98396_R2108_BPE_SUPPLY_SRC ... MAX98396_R210B_BPE_LOW_LIMITER:
	case MAX98396_R210D_BPE_EN ... MAX98396_R210F_GLOBAL_EN:
	case MAX98397_R22FF_REVISION_ID:
		return true;
	default:
		return false;
	}
};

static bool max98397_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98396_R2001_INT_RAW1 ... MAX98396_R200E_INT_FLAG4:
	case MAX98396_R2041_PCM_MODE_CFG:
	case MAX98397_R20B7_ADC_PVDD_READBACK_MSB
		... MAX98397_R20C4_ADC_LO_VDDH_READBACK_LSB:
	case MAX98396_R20E5_BPE_STATE:
	case MAX98396_R2109_BPE_LOW_STATE
		... MAX98396_R210B_BPE_LOW_LIMITER:
	case MAX98396_R210F_GLOBAL_EN:
	case MAX98397_R22FF_REVISION_ID:
		return true;
	default:
		return false;
	}
}

static const char * const max98396_op_mod_text[] = {
	"DG", "PVDD", "VBAT",
};

static SOC_ENUM_SINGLE_DECL(max98396_op_mod_enum,
			    MAX98396_R2098_SPK_CLS_DG_MODE,
			    0, max98396_op_mod_text);

static DECLARE_TLV_DB_SCALE(max98396_digital_tlv, -6350, 50, 1);
static const DECLARE_TLV_DB_RANGE(max98396_spk_tlv,
	0, 0x11, TLV_DB_SCALE_ITEM(400, 100, 0),
);
static DECLARE_TLV_DB_RANGE(max98397_digital_tlv,
	0, 0x4A, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 1),
	0x4B, 0xFF, TLV_DB_SCALE_ITEM(-9000, 50, 0),
);
static const DECLARE_TLV_DB_RANGE(max98397_spk_tlv,
	0, 0x15, TLV_DB_SCALE_ITEM(600, 100, 0),
);

static int max98396_mux_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct max98396_priv *max98396 = snd_soc_component_get_drvdata(component);
	int reg, val;

	if (max98396->device_id == CODEC_TYPE_MAX98396)
		reg = MAX98396_R2055_PCM_RX_SRC1;
	else
		reg = MAX98397_R2056_PCM_RX_SRC1;

	regmap_read(max98396->regmap, reg, &val);

	ucontrol->value.enumerated.item[0] = val;

	return 0;
}

static int max98396_mux_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct max98396_priv *max98396 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	int reg, val;
	int change;

	if (item[0] >= e->items)
		return -EINVAL;

	val = snd_soc_enum_item_to_val(e, item[0]) << e->shift_l;

	if (max98396->device_id == CODEC_TYPE_MAX98396)
		reg = MAX98396_R2055_PCM_RX_SRC1;
	else
		reg = MAX98397_R2056_PCM_RX_SRC1;

	change = snd_soc_component_test_bits(component, reg,
					     MAX98396_PCM_RX_MASK, val);

	if (change)
		regmap_update_bits(max98396->regmap, reg,
				   MAX98396_PCM_RX_MASK, val);

	snd_soc_dapm_mux_update_power(dapm, kcontrol, item[0], e, NULL);

	return change;
}

static const char * const max98396_switch_text[] = {
	"Left", "Right", "LeftRight"};

static SOC_ENUM_SINGLE_DECL(dai_sel_enum, SND_SOC_NOPM, 0,
			    max98396_switch_text);

static const struct snd_kcontrol_new max98396_dai_mux =
	SOC_DAPM_ENUM_EXT("DAI Sel Mux", dai_sel_enum,
			  max98396_mux_get, max98396_mux_put);

static const struct snd_kcontrol_new max98396_vi_control =
	SOC_DAPM_SINGLE("Switch", MAX98396_R205F_PCM_TX_EN, 0, 1, 0);

static const struct snd_soc_dapm_widget max98396_dapm_widgets[] = {
	SND_SOC_DAPM_DAC_E("Amp Enable", "HiFi Playback",
			   MAX98396_R20AF_AMP_EN, 0, 0, max98396_dac_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MUX("DAI Sel Mux", SND_SOC_NOPM, 0, 0,
			 &max98396_dai_mux),
	SND_SOC_DAPM_OUTPUT("BE_OUT"),
	SND_SOC_DAPM_AIF_OUT("Voltage Sense", "HiFi Capture", 0,
			     MAX98396_R20E4_IV_SENSE_PATH_EN, 0, 0),
	SND_SOC_DAPM_AIF_OUT("Current Sense", "HiFi Capture", 0,
			     MAX98396_R20E4_IV_SENSE_PATH_EN, 1, 0),
	SND_SOC_DAPM_SWITCH("VI Sense", SND_SOC_NOPM, 0, 0,
			    &max98396_vi_control),
	SND_SOC_DAPM_SIGGEN("VMON"),
	SND_SOC_DAPM_SIGGEN("IMON"),
	SND_SOC_DAPM_SIGGEN("FBMON"),
};

static const char * const max98396_thermal_thresh_text[] = {
	"50C", "51C", "52C", "53C", "54C", "55C", "56C", "57C",
	"58C", "59C", "60C", "61C", "62C", "63C", "64C", "65C",
	"66C", "67C", "68C", "69C", "70C", "71C", "72C", "73C",
	"74C", "75C", "76C", "77C", "78C", "79C", "80C", "81C",
	"82C", "83C", "84C", "85C", "86C", "87C", "88C", "89C",
	"90C", "91C", "92C", "93C", "94C", "95C", "96C", "97C",
	"98C", "99C", "100C", "101C", "102C", "103C", "104C", "105C",
	"106C", "107C", "108C", "109C", "110C", "111C", "112C", "113C",
	"114C", "115C", "116C", "117C", "118C", "119C", "120C", "121C",
	"122C", "123C", "124C", "125C", "126C", "127C", "128C", "129C",
	"130C", "131C", "132C", "133C", "134C", "135C", "136C", "137C",
	"138C", "139C", "140C", "141C", "142C", "143C", "144C", "145C",
	"146C", "147C", "148C", "149C", "150C"
};

static SOC_ENUM_SINGLE_DECL(max98396_thermal_warn_thresh1_enum,
			    MAX98396_R2020_THERM_WARN_THRESH, 0,
			    max98396_thermal_thresh_text);

static SOC_ENUM_SINGLE_DECL(max98396_thermal_warn_thresh2_enum,
			    MAX98396_R2021_THERM_WARN_THRESH2, 0,
			    max98396_thermal_thresh_text);

static SOC_ENUM_SINGLE_DECL(max98396_thermal_shdn_thresh_enum,
			    MAX98396_R2022_THERM_SHDN_THRESH, 0,
			    max98396_thermal_thresh_text);

static const char * const max98396_thermal_hyteresis_text[] = {
	"2C", "5C", "7C", "10C"
};

static SOC_ENUM_SINGLE_DECL(max98396_thermal_hysteresis_enum,
			    MAX98396_R2023_THERM_HYSTERESIS, 0,
			    max98396_thermal_hyteresis_text);

static const char * const max98396_foldback_slope_text[] = {
	"0.25", "0.5", "1.0", "2.0"
};

static SOC_ENUM_SINGLE_DECL(max98396_thermal_fb_slope1_enum,
			    MAX98396_R2024_THERM_FOLDBACK_SET,
			    MAX98396_THERM_FB_SLOPE1_SHIFT,
			    max98396_foldback_slope_text);

static SOC_ENUM_SINGLE_DECL(max98396_thermal_fb_slope2_enum,
			    MAX98396_R2024_THERM_FOLDBACK_SET,
			    MAX98396_THERM_FB_SLOPE2_SHIFT,
			    max98396_foldback_slope_text);

static const char * const max98396_foldback_reltime_text[] = {
	"3ms", "10ms", "100ms", "300ms"
};

static SOC_ENUM_SINGLE_DECL(max98396_thermal_fb_reltime_enum,
			    MAX98396_R2024_THERM_FOLDBACK_SET,
			    MAX98396_THERM_FB_REL_SHIFT,
			    max98396_foldback_reltime_text);

static const char * const max98396_foldback_holdtime_text[] = {
	"0ms", "20ms", "40ms", "80ms"
};

static SOC_ENUM_SINGLE_DECL(max98396_thermal_fb_holdtime_enum,
			    MAX98396_R2024_THERM_FOLDBACK_SET,
			    MAX98396_THERM_FB_HOLD_SHIFT,
			    max98396_foldback_holdtime_text);

static int max98396_adc_value_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct max98396_priv *max98396 = snd_soc_component_get_drvdata(component);
	int ret;
	u8 val[2];
	int reg = mc->reg;

	/* ADC value is not available if the device is powered down */
	if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF)
		goto exit;

	if (max98396->device_id == CODEC_TYPE_MAX98397) {
		switch (mc->reg) {
		case MAX98396_R20B6_ADC_PVDD_READBACK_MSB:
			reg = MAX98397_R20B7_ADC_PVDD_READBACK_MSB;
			break;
		case MAX98396_R20B8_ADC_VBAT_READBACK_MSB:
			reg = MAX98397_R20B9_ADC_VBAT_READBACK_MSB;
			break;
		case MAX98396_R20BA_ADC_TEMP_READBACK_MSB:
			reg = MAX98397_R20BB_ADC_TEMP_READBACK_MSB;
			break;
		default:
			goto exit;
		}
	}

	ret = regmap_raw_read(max98396->regmap, reg, &val, 2);
	if (ret)
		goto exit;

	/* ADC readback bits[8:0] rearrangement */
	ucontrol->value.integer.value[0] = (val[0] << 1) | (val[1] & 1);
	return 0;

exit:
	ucontrol->value.integer.value[0] = 0;
	return 0;
}

static const struct snd_kcontrol_new max98396_snd_controls[] = {
	/* Volume */
	SOC_SINGLE_TLV("Digital Volume", MAX98396_R2090_AMP_VOL_CTRL,
		       0, 0x7F, 1, max98396_digital_tlv),
	SOC_SINGLE_TLV("Speaker Volume", MAX98396_R2091_AMP_PATH_GAIN,
		       0, 0x11, 0, max98396_spk_tlv),
	/* Volume Ramp Up/Down Enable*/
	SOC_SINGLE("Ramp Up Switch", MAX98396_R2092_AMP_DSP_CFG,
		   MAX98396_DSP_SPK_VOL_RMPUP_SHIFT, 1, 0),
	SOC_SINGLE("Ramp Down Switch", MAX98396_R2092_AMP_DSP_CFG,
		   MAX98396_DSP_SPK_VOL_RMPDN_SHIFT, 1, 0),
	/* Clock Monitor Enable */
	SOC_SINGLE("CLK Monitor Switch", MAX98396_R203F_ENABLE_CTRLS,
		   MAX98396_CTRL_CMON_EN_SHIFT, 1, 0),
	/* Dither Enable */
	SOC_SINGLE("Dither Switch", MAX98396_R2092_AMP_DSP_CFG,
		   MAX98396_DSP_SPK_DITH_EN_SHIFT, 1, 0),
	SOC_SINGLE("IV Dither Switch", MAX98396_R20E0_IV_SENSE_PATH_CFG,
		   MAX98396_IV_SENSE_DITH_EN_SHIFT, 1, 0),
	/* DC Blocker Enable */
	SOC_SINGLE("DC Blocker Switch", MAX98396_R2092_AMP_DSP_CFG,
		   MAX98396_DSP_SPK_DCBLK_EN_SHIFT, 1, 0),
	SOC_SINGLE("IV DC Blocker Switch", MAX98396_R20E0_IV_SENSE_PATH_CFG,
		   MAX98396_IV_SENSE_DCBLK_EN_SHIFT, 3, 0),
	/* Speaker Safe Mode Enable */
	SOC_SINGLE("Safe Mode Switch", MAX98396_R2092_AMP_DSP_CFG,
		   MAX98396_DSP_SPK_SAFE_EN_SHIFT, 1, 0),
	/* Wideband Filter Enable */
	SOC_SINGLE("WB Filter Switch", MAX98396_R2092_AMP_DSP_CFG,
		   MAX98396_DSP_SPK_WB_FLT_EN_SHIFT, 1, 0),
	SOC_SINGLE("IV WB Filter Switch", MAX98396_R20E0_IV_SENSE_PATH_CFG,
		   MAX98396_IV_SENSE_WB_FLT_EN_SHIFT, 1, 0),
	/* Dynamic Headroom Tracking */
	SOC_SINGLE("DHT Switch", MAX98396_R20DF_DHT_EN, 0, 1, 0),
	/* Brownout Protection Engine */
	SOC_SINGLE("BPE Switch", MAX98396_R210D_BPE_EN, 0, 1, 0),
	SOC_SINGLE("BPE Limiter Switch", MAX98396_R210D_BPE_EN, 1, 1, 0),
	/* Bypass Path Enable */
	SOC_SINGLE("Bypass Path Switch",
		   MAX98396_R205E_PCM_RX_EN, 1, 1, 0),
	/* Speaker Operation Mode */
	SOC_ENUM("OP Mode", max98396_op_mod_enum),
	/* Auto Restart functions */
	SOC_SINGLE("CMON Auto Restart Switch", MAX98396_R2038_CLK_MON_CTRL,
		   MAX98396_CLK_MON_AUTO_RESTART_SHIFT, 1, 0),
	SOC_SINGLE("PVDD Auto Restart Switch", MAX98396_R210E_AUTO_RESTART,
		   MAX98396_PVDD_UVLO_RESTART_SHFT, 1, 0),
	SOC_SINGLE("VBAT Auto Restart Switch", MAX98396_R210E_AUTO_RESTART,
		   MAX98396_VBAT_UVLO_RESTART_SHFT, 1, 0),
	SOC_SINGLE("THERM Auto Restart Switch", MAX98396_R210E_AUTO_RESTART,
		   MAX98396_THEM_SHDN_RESTART_SHFT, 1, 0),
	SOC_SINGLE("OVC Auto Restart Switch", MAX98396_R210E_AUTO_RESTART,
		   MAX98396_OVC_RESTART_SHFT, 1, 0),
	/* Thermal Threshold */
	SOC_ENUM("THERM Thresh1", max98396_thermal_warn_thresh1_enum),
	SOC_ENUM("THERM Thresh2", max98396_thermal_warn_thresh2_enum),
	SOC_ENUM("THERM SHDN Thresh", max98396_thermal_shdn_thresh_enum),
	SOC_ENUM("THERM Hysteresis", max98396_thermal_hysteresis_enum),
	SOC_SINGLE("THERM Foldback Switch",
		   MAX98396_R2027_THERM_FOLDBACK_EN, 0, 1, 0),
	SOC_ENUM("THERM Slope1", max98396_thermal_fb_slope1_enum),
	SOC_ENUM("THERM Slope2", max98396_thermal_fb_slope2_enum),
	SOC_ENUM("THERM Release", max98396_thermal_fb_reltime_enum),
	SOC_ENUM("THERM Hold", max98396_thermal_fb_holdtime_enum),
	/* ADC */
	SOC_SINGLE_EXT("ADC PVDD", MAX98396_R20B6_ADC_PVDD_READBACK_MSB, 0, 0x1FF, 0,
		       max98396_adc_value_get, NULL),
	SOC_SINGLE_EXT("ADC VBAT", MAX98396_R20B8_ADC_VBAT_READBACK_MSB, 0, 0x1FF, 0,
		       max98396_adc_value_get, NULL),
	SOC_SINGLE_EXT("ADC TEMP", MAX98396_R20BA_ADC_TEMP_READBACK_MSB, 0, 0x1FF, 0,
		       max98396_adc_value_get, NULL),
};

static const struct snd_kcontrol_new max98397_snd_controls[] = {
	/* Volume */
	SOC_SINGLE_TLV("Digital Volume", MAX98396_R2090_AMP_VOL_CTRL,
		       0, 0xFF, 1, max98397_digital_tlv),
	SOC_SINGLE_TLV("Speaker Volume", MAX98396_R2091_AMP_PATH_GAIN,
		       0, 0x15, 0, max98397_spk_tlv),
	/* Volume Ramp Up/Down Enable*/
	SOC_SINGLE("Ramp Up Switch", MAX98396_R2092_AMP_DSP_CFG,
		   MAX98396_DSP_SPK_VOL_RMPUP_SHIFT, 1, 0),
	SOC_SINGLE("Ramp Down Switch", MAX98396_R2092_AMP_DSP_CFG,
		   MAX98396_DSP_SPK_VOL_RMPDN_SHIFT, 1, 0),
	/* Clock Monitor Enable */
	SOC_SINGLE("CLK Monitor Switch", MAX98396_R203F_ENABLE_CTRLS,
		   MAX98396_CTRL_CMON_EN_SHIFT, 1, 0),
	/* Dither Enable */
	SOC_SINGLE("Dither Switch", MAX98396_R2092_AMP_DSP_CFG,
		   MAX98396_DSP_SPK_DITH_EN_SHIFT, 1, 0),
	SOC_SINGLE("IV Dither Switch", MAX98396_R20E0_IV_SENSE_PATH_CFG,
		   MAX98396_IV_SENSE_DITH_EN_SHIFT, 1, 0),
	/* DC Blocker Enable */
	SOC_SINGLE("DC Blocker Switch", MAX98396_R2092_AMP_DSP_CFG,
		   MAX98396_DSP_SPK_DCBLK_EN_SHIFT, 1, 0),
	SOC_SINGLE("IV DC Blocker Switch", MAX98396_R20E0_IV_SENSE_PATH_CFG,
		   MAX98396_IV_SENSE_DCBLK_EN_SHIFT, 3, 0),
	/* Speaker Safe Mode Enable */
	SOC_SINGLE("Safe Mode Switch", MAX98396_R2092_AMP_DSP_CFG,
		   MAX98396_DSP_SPK_SAFE_EN_SHIFT, 1, 0),
	/* Wideband Filter Enable */
	SOC_SINGLE("WB Filter Switch", MAX98396_R2092_AMP_DSP_CFG,
		   MAX98396_DSP_SPK_WB_FLT_EN_SHIFT, 1, 0),
	SOC_SINGLE("IV WB Filter Switch", MAX98396_R20E0_IV_SENSE_PATH_CFG,
		   MAX98396_IV_SENSE_WB_FLT_EN_SHIFT, 1, 0),
	/* Dynamic Headroom Tracking */
	SOC_SINGLE("DHT Switch", MAX98396_R20DF_DHT_EN, 0, 1, 0),
	/* Brownout Protection Engine */
	SOC_SINGLE("BPE Switch", MAX98396_R210D_BPE_EN, 0, 1, 0),
	SOC_SINGLE("BPE Limiter Switch", MAX98396_R210D_BPE_EN, 1, 1, 0),
	/* Bypass Path Enable */
	SOC_SINGLE("Bypass Path Switch",
		   MAX98396_R205E_PCM_RX_EN, 1, 1, 0),
	/* Speaker Operation Mode */
	SOC_ENUM("OP Mode", max98396_op_mod_enum),
	/* Auto Restart functions */
	SOC_SINGLE("CMON Auto Restart Switch", MAX98396_R2038_CLK_MON_CTRL,
		   MAX98396_CLK_MON_AUTO_RESTART_SHIFT, 1, 0),
	SOC_SINGLE("PVDD Auto Restart Switch", MAX98396_R210E_AUTO_RESTART,
		   MAX98396_PVDD_UVLO_RESTART_SHFT, 1, 0),
	SOC_SINGLE("VBAT Auto Restart Switch", MAX98396_R210E_AUTO_RESTART,
		   MAX98396_VBAT_UVLO_RESTART_SHFT, 1, 0),
	SOC_SINGLE("THERM Auto Restart Switch", MAX98396_R210E_AUTO_RESTART,
		   MAX98396_THEM_SHDN_RESTART_SHFT, 1, 0),
	SOC_SINGLE("OVC Auto Restart Switch", MAX98396_R210E_AUTO_RESTART,
		   MAX98396_OVC_RESTART_SHFT, 1, 0),
	/* Thermal Threshold */
	SOC_ENUM("THERM Thresh1", max98396_thermal_warn_thresh1_enum),
	SOC_ENUM("THERM Thresh2", max98396_thermal_warn_thresh2_enum),
	SOC_ENUM("THERM SHDN Thresh", max98396_thermal_shdn_thresh_enum),
	SOC_ENUM("THERM Hysteresis", max98396_thermal_hysteresis_enum),
	SOC_SINGLE("THERM Foldback Switch",
		   MAX98396_R2027_THERM_FOLDBACK_EN, 0, 1, 0),
	SOC_ENUM("THERM Slope1", max98396_thermal_fb_slope1_enum),
	SOC_ENUM("THERM Slope2", max98396_thermal_fb_slope2_enum),
	SOC_ENUM("THERM Release", max98396_thermal_fb_reltime_enum),
	SOC_ENUM("THERM Hold", max98396_thermal_fb_holdtime_enum),
	/* ADC */
	SOC_SINGLE_EXT("ADC PVDD", MAX98396_R20B6_ADC_PVDD_READBACK_MSB, 0, 0x1FF, 0,
		       max98396_adc_value_get, NULL),
	SOC_SINGLE_EXT("ADC VBAT", MAX98396_R20B8_ADC_VBAT_READBACK_MSB, 0, 0x1FF, 0,
		       max98396_adc_value_get, NULL),
	SOC_SINGLE_EXT("ADC TEMP", MAX98396_R20BA_ADC_TEMP_READBACK_MSB, 0, 0x1FF, 0,
		       max98396_adc_value_get, NULL),
};

static const struct snd_soc_dapm_route max98396_audio_map[] = {
	/* Plabyack */
	{"DAI Sel Mux", "Left", "Amp Enable"},
	{"DAI Sel Mux", "Right", "Amp Enable"},
	{"DAI Sel Mux", "LeftRight", "Amp Enable"},
	{"BE_OUT", NULL, "DAI Sel Mux"},
	/* Capture */
	{ "VI Sense", "Switch", "VMON" },
	{ "VI Sense", "Switch", "IMON" },
	{ "Voltage Sense", NULL, "VI Sense" },
	{ "Current Sense", NULL, "VI Sense" },
};

static struct snd_soc_dai_driver max98396_dai[] = {
	{
		.name = "max98396-aif1",
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MAX98396_RATES,
			.formats = MAX98396_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MAX98396_RATES,
			.formats = MAX98396_FORMATS,
		},
		.ops = &max98396_dai_ops,
	}
};

static struct snd_soc_dai_driver max98397_dai[] = {
	{
		.name = "max98397-aif1",
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MAX98396_RATES,
			.formats = MAX98396_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MAX98396_RATES,
			.formats = MAX98396_FORMATS,
		},
		.ops = &max98396_dai_ops,
	}
};

static void max98396_reset(struct max98396_priv *max98396, struct device *dev)
{
	int ret, reg, count;

	/* Software Reset */
	ret = regmap_write(max98396->regmap,
			   MAX98396_R2000_SW_RESET, 1);
	if (ret)
		dev_err(dev, "Reset command failed. (ret:%d)\n", ret);

	count = 0;
	while (count < 3) {
		usleep_range(5000, 6000);
		/* Software Reset Verification */
		ret = regmap_read(max98396->regmap,
				  GET_REG_ADDR_REV_ID(max98396->device_id), &reg);
		if (!ret) {
			dev_info(dev, "Reset completed (retry:%d)\n", count);
			return;
		}
		count++;
	}
	dev_err(dev, "Reset failed. (ret:%d)\n", ret);
}

static int max98396_probe(struct snd_soc_component *component)
{
	struct max98396_priv *max98396 =
		snd_soc_component_get_drvdata(component);

	/* Software Reset */
	max98396_reset(max98396, component->dev);

	/* L/R mix configuration */
	if (max98396->device_id == CODEC_TYPE_MAX98396) {
		regmap_write(max98396->regmap,
			     MAX98396_R2055_PCM_RX_SRC1, 0x02);
		regmap_write(max98396->regmap,
			     MAX98396_R2056_PCM_RX_SRC2, 0x10);
	} else {
		regmap_write(max98396->regmap,
			     MAX98397_R2056_PCM_RX_SRC1, 0x02);
		regmap_write(max98396->regmap,
			     MAX98397_R2057_PCM_RX_SRC2, 0x10);
	}
	/* Supply control */
	regmap_update_bits(max98396->regmap,
			   MAX98396_R20A0_AMP_SUPPLY_CTL,
			   MAX98396_AMP_SUPPLY_NOVBAT,
			   (max98396->vbat == NULL) ?
				MAX98396_AMP_SUPPLY_NOVBAT : 0);
	/* Enable DC blocker */
	regmap_update_bits(max98396->regmap,
			   MAX98396_R2092_AMP_DSP_CFG, 1, 1);
	/* Enable IV Monitor DC blocker */
	regmap_update_bits(max98396->regmap,
			   MAX98396_R20E0_IV_SENSE_PATH_CFG,
			   MAX98396_IV_SENSE_DCBLK_EN_MASK,
			   MAX98396_IV_SENSE_DCBLK_EN_MASK);
	/* Configure default data output sources */
	regmap_write(max98396->regmap,
		     MAX98396_R205D_PCM_TX_SRC_EN, 3);
	/* Enable Wideband Filter */
	regmap_update_bits(max98396->regmap,
			   MAX98396_R2092_AMP_DSP_CFG, 0x40, 0x40);
	/* Enable IV Wideband Filter */
	regmap_update_bits(max98396->regmap,
			   MAX98396_R20E0_IV_SENSE_PATH_CFG, 8, 8);

	/* Enable Bypass Source */
	regmap_write(max98396->regmap,
		     MAX98396_R2058_PCM_BYPASS_SRC,
		     max98396->bypass_slot);
	/* Voltage, current slot configuration */
	regmap_write(max98396->regmap,
		     MAX98396_R2044_PCM_TX_CTRL_1,
		     max98396->v_slot);
	regmap_write(max98396->regmap,
		     MAX98396_R2045_PCM_TX_CTRL_2,
		     max98396->i_slot);
	regmap_write(max98396->regmap,
		     MAX98396_R204A_PCM_TX_CTRL_7,
		     max98396->spkfb_slot);

	if (max98396->v_slot < 8)
		if (max98396->device_id == CODEC_TYPE_MAX98396)
			regmap_update_bits(max98396->regmap,
					   MAX98396_R2053_PCM_TX_HIZ_CTRL_8,
					   1 << max98396->v_slot, 0);
		else
			regmap_update_bits(max98396->regmap,
					   MAX98397_R2054_PCM_TX_HIZ_CTRL_8,
					   1 << max98396->v_slot, 0);
	else
		if (max98396->device_id == CODEC_TYPE_MAX98396)
			regmap_update_bits(max98396->regmap,
					   MAX98396_R2052_PCM_TX_HIZ_CTRL_7,
					   1 << (max98396->v_slot - 8), 0);
		else
			regmap_update_bits(max98396->regmap,
					   MAX98397_R2053_PCM_TX_HIZ_CTRL_7,
					   1 << (max98396->v_slot - 8), 0);

	if (max98396->i_slot < 8)
		if (max98396->device_id == CODEC_TYPE_MAX98396)
			regmap_update_bits(max98396->regmap,
					   MAX98396_R2053_PCM_TX_HIZ_CTRL_8,
					   1 << max98396->i_slot, 0);
		else
			regmap_update_bits(max98396->regmap,
					   MAX98397_R2054_PCM_TX_HIZ_CTRL_8,
					   1 << max98396->i_slot, 0);
	else
		if (max98396->device_id == CODEC_TYPE_MAX98396)
			regmap_update_bits(max98396->regmap,
					   MAX98396_R2052_PCM_TX_HIZ_CTRL_7,
					   1 << (max98396->i_slot - 8), 0);
		else
			regmap_update_bits(max98396->regmap,
					   MAX98397_R2053_PCM_TX_HIZ_CTRL_7,
					   1 << (max98396->i_slot - 8), 0);

	/* Set interleave mode */
	if (max98396->interleave_mode)
		regmap_update_bits(max98396->regmap,
				   MAX98396_R2041_PCM_MODE_CFG,
				   MAX98396_PCM_TX_CH_INTERLEAVE_MASK,
				   MAX98396_PCM_TX_CH_INTERLEAVE_MASK);

	regmap_update_bits(max98396->regmap,
			   MAX98396_R2038_CLK_MON_CTRL,
			   MAX98396_CLK_MON_AUTO_RESTART_MASK,
			   MAX98396_CLK_MON_AUTO_RESTART_MASK);

	regmap_update_bits(max98396->regmap,
			   MAX98396_R203F_ENABLE_CTRLS,
			   MAX98396_CTRL_DMON_STUCK_EN_MASK,
			   max98396->dmon_stuck_enable ?
				MAX98396_CTRL_DMON_STUCK_EN_MASK : 0);

	regmap_update_bits(max98396->regmap,
			   MAX98396_R203F_ENABLE_CTRLS,
			   MAX98396_CTRL_DMON_MAG_EN_MASK,
			   max98396->dmon_mag_enable ?
				MAX98396_CTRL_DMON_MAG_EN_MASK : 0);

	switch (max98396->dmon_duration) {
	case 64:
		regmap_update_bits(max98396->regmap,
				   MAX98396_R2039_DATA_MON_CTRL,
				   MAX98396_DMON_DURATION_MASK, 0);
		break;
	case 256:
		regmap_update_bits(max98396->regmap,
				   MAX98396_R2039_DATA_MON_CTRL,
				   MAX98396_DMON_DURATION_MASK, 1);
		break;
	case 1024:
		regmap_update_bits(max98396->regmap,
				   MAX98396_R2039_DATA_MON_CTRL,
				   MAX98396_DMON_DURATION_MASK, 2);
		break;
	case 4096:
		regmap_update_bits(max98396->regmap,
				   MAX98396_R2039_DATA_MON_CTRL,
				   MAX98396_DMON_DURATION_MASK, 3);
		break;
	default:
		dev_err(component->dev, "Invalid DMON duration %d\n",
			max98396->dmon_duration);
	}

	switch (max98396->dmon_stuck_threshold) {
	case 15:
		regmap_update_bits(max98396->regmap,
				   MAX98396_R2039_DATA_MON_CTRL,
				   MAX98396_DMON_STUCK_THRESH_MASK,
				   0 << MAX98396_DMON_STUCK_THRESH_SHIFT);
		break;
	case 13:
		regmap_update_bits(max98396->regmap,
				   MAX98396_R2039_DATA_MON_CTRL,
				   MAX98396_DMON_STUCK_THRESH_MASK,
				   1 << MAX98396_DMON_STUCK_THRESH_SHIFT);
		break;
	case 22:
		regmap_update_bits(max98396->regmap,
				   MAX98396_R2039_DATA_MON_CTRL,
				   MAX98396_DMON_STUCK_THRESH_MASK,
				   2 << MAX98396_DMON_STUCK_THRESH_SHIFT);
		break;
	case 9:
		regmap_update_bits(max98396->regmap,
				   MAX98396_R2039_DATA_MON_CTRL,
				   MAX98396_DMON_STUCK_THRESH_MASK,
				   3 << MAX98396_DMON_STUCK_THRESH_SHIFT);
		break;
	default:
		dev_err(component->dev, "Invalid DMON stuck threshold %d\n",
			max98396->dmon_stuck_threshold);
	}

	switch (max98396->dmon_mag_threshold) {
	case 2 ... 5:
		regmap_update_bits(max98396->regmap,
				   MAX98396_R2039_DATA_MON_CTRL,
				   MAX98396_DMON_STUCK_THRESH_MASK,
				   (5 - max98396->dmon_mag_threshold)
					<< MAX98396_DMON_MAG_THRESH_SHIFT);
		break;
	default:
		dev_err(component->dev, "Invalid DMON magnitude threshold %d\n",
			max98396->dmon_mag_threshold);
	}

	/* Speaker Amplifier PCM RX Enable by default */
	regmap_update_bits(max98396->regmap,
			   MAX98396_R205E_PCM_RX_EN,
			   MAX98396_PCM_RX_EN_MASK, 1);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max98396_suspend(struct device *dev)
{
	struct max98396_priv *max98396 = dev_get_drvdata(dev);

	regcache_cache_only(max98396->regmap, true);
	regcache_mark_dirty(max98396->regmap);
	regulator_bulk_disable(MAX98396_NUM_CORE_SUPPLIES,
			       max98396->core_supplies);
	if (max98396->pvdd)
		regulator_disable(max98396->pvdd);

	if (max98396->vbat)
		regulator_disable(max98396->vbat);

	return 0;
}

static int max98396_resume(struct device *dev)
{
	struct max98396_priv *max98396 = dev_get_drvdata(dev);
	int ret;

	ret = regulator_bulk_enable(MAX98396_NUM_CORE_SUPPLIES,
				    max98396->core_supplies);
	if (ret < 0)
		return ret;

	if (max98396->pvdd) {
		ret = regulator_enable(max98396->pvdd);
		if (ret < 0)
			return ret;
	}

	if (max98396->vbat) {
		ret = regulator_enable(max98396->vbat);
		if (ret < 0)
			return ret;
	}

	regcache_cache_only(max98396->regmap, false);
	max98396_reset(max98396, dev);
	regcache_sync(max98396->regmap);
	return 0;
}
#endif

static const struct dev_pm_ops max98396_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(max98396_suspend, max98396_resume)
};

static const struct snd_soc_component_driver soc_codec_dev_max98396 = {
	.probe			= max98396_probe,
	.controls		= max98396_snd_controls,
	.num_controls		= ARRAY_SIZE(max98396_snd_controls),
	.dapm_widgets		= max98396_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(max98396_dapm_widgets),
	.dapm_routes		= max98396_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(max98396_audio_map),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct snd_soc_component_driver soc_codec_dev_max98397 = {
	.probe			= max98396_probe,
	.controls		= max98397_snd_controls,
	.num_controls		= ARRAY_SIZE(max98397_snd_controls),
	.dapm_widgets		= max98396_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(max98396_dapm_widgets),
	.dapm_routes		= max98396_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(max98396_audio_map),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config max98396_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = MAX98396_R21FF_REVISION_ID,
	.reg_defaults  = max98396_reg,
	.num_reg_defaults = ARRAY_SIZE(max98396_reg),
	.readable_reg = max98396_readable_register,
	.volatile_reg = max98396_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

static const struct regmap_config max98397_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = MAX98397_R22FF_REVISION_ID,
	.reg_defaults  = max98397_reg,
	.num_reg_defaults = ARRAY_SIZE(max98397_reg),
	.readable_reg = max98397_readable_register,
	.volatile_reg = max98397_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

static void max98396_read_device_property(struct device *dev,
					  struct max98396_priv *max98396)
{
	int value;

	if (!device_property_read_u32(dev, "adi,vmon-slot-no", &value))
		max98396->v_slot = value & 0xF;
	else
		max98396->v_slot = 0;

	if (!device_property_read_u32(dev, "adi,imon-slot-no", &value))
		max98396->i_slot = value & 0xF;
	else
		max98396->i_slot = 1;

	if (!device_property_read_u32(dev, "adi,spkfb-slot-no", &value))
		max98396->spkfb_slot = value & 0xF;
	else
		max98396->spkfb_slot = 2;

	if (!device_property_read_u32(dev, "adi,bypass-slot-no", &value))
		max98396->bypass_slot = value & 0xF;
	else
		max98396->bypass_slot = 0;

	max98396->dmon_stuck_enable =
		device_property_read_bool(dev, "adi,dmon-stuck-enable");

	if (!device_property_read_u32(dev, "adi,dmon-stuck-threshold-bits", &value))
		max98396->dmon_stuck_threshold = value;
	else
		max98396->dmon_stuck_threshold = 15;

	max98396->dmon_mag_enable =
		device_property_read_bool(dev, "adi,dmon-magnitude-enable");

	if (!device_property_read_u32(dev, "adi,dmon-magnitude-threshold-bits", &value))
		max98396->dmon_mag_threshold = value;
	else
		max98396->dmon_mag_threshold = 5;

	if (!device_property_read_u32(dev, "adi,dmon-duration-ms", &value))
		max98396->dmon_duration = value;
	else
		max98396->dmon_duration = 64;
}

static void max98396_core_supplies_disable(void *priv)
{
	struct max98396_priv *max98396 = priv;

	regulator_bulk_disable(MAX98396_NUM_CORE_SUPPLIES,
			       max98396->core_supplies);
}

static void max98396_supply_disable(void *r)
{
	regulator_disable((struct regulator *) r);
}

static int max98396_i2c_probe(struct i2c_client *i2c)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(i2c);
	struct max98396_priv *max98396 = NULL;
	int i, ret, reg;

	max98396 = devm_kzalloc(&i2c->dev, sizeof(*max98396), GFP_KERNEL);

	if (!max98396) {
		ret = -ENOMEM;
		return ret;
	}
	i2c_set_clientdata(i2c, max98396);

	max98396->device_id =  id->driver_data;

	/* regmap initialization */
	if (max98396->device_id == CODEC_TYPE_MAX98396)
		max98396->regmap = devm_regmap_init_i2c(i2c, &max98396_regmap);

	else
		max98396->regmap = devm_regmap_init_i2c(i2c, &max98397_regmap);

	if (IS_ERR(max98396->regmap)) {
		ret = PTR_ERR(max98396->regmap);
		dev_err(&i2c->dev,
			"Failed to allocate regmap: %d\n", ret);
		return ret;
	}

	/* Obtain regulator supplies */
	for (i = 0; i < MAX98396_NUM_CORE_SUPPLIES; i++)
		max98396->core_supplies[i].supply = max98396_core_supplies[i];

	ret = devm_regulator_bulk_get(&i2c->dev, MAX98396_NUM_CORE_SUPPLIES,
				      max98396->core_supplies);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to request core supplies: %d\n", ret);
		return ret;
	}

	max98396->vbat = devm_regulator_get_optional(&i2c->dev, "vbat");
	if (IS_ERR(max98396->vbat)) {
		if (PTR_ERR(max98396->vbat) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		max98396->vbat = NULL;
	}

	max98396->pvdd = devm_regulator_get_optional(&i2c->dev, "pvdd");
	if (IS_ERR(max98396->pvdd)) {
		if (PTR_ERR(max98396->pvdd) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		max98396->pvdd = NULL;
	}

	ret = regulator_bulk_enable(MAX98396_NUM_CORE_SUPPLIES,
				    max98396->core_supplies);
	if (ret < 0) {
		dev_err(&i2c->dev, "Unable to enable core supplies: %d", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(&i2c->dev, max98396_core_supplies_disable,
				       max98396);
	if (ret < 0)
		return ret;

	if (max98396->pvdd) {
		ret = regulator_enable(max98396->pvdd);
		if (ret < 0)
			return ret;

		ret = devm_add_action_or_reset(&i2c->dev,
					       max98396_supply_disable,
					       max98396->pvdd);
		if (ret < 0)
			return ret;
	}

	if (max98396->vbat) {
		ret = regulator_enable(max98396->vbat);
		if (ret < 0)
			return ret;

		ret = devm_add_action_or_reset(&i2c->dev,
					       max98396_supply_disable,
					       max98396->vbat);
		if (ret < 0)
			return ret;
	}

	/* update interleave mode info */
	if (device_property_read_bool(&i2c->dev, "adi,interleave_mode"))
		max98396->interleave_mode = true;
	else
		max98396->interleave_mode = false;

	/* voltage/current slot & gpio configuration */
	max98396_read_device_property(&i2c->dev, max98396);

	/* Reset the Device */
	max98396->reset_gpio = devm_gpiod_get_optional(&i2c->dev,
						       "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(max98396->reset_gpio)) {
		ret = PTR_ERR(max98396->reset_gpio);
		dev_err(&i2c->dev, "Unable to request GPIO pin: %d.\n", ret);
		return ret;
	}

	if (max98396->reset_gpio) {
		usleep_range(5000, 6000);
		gpiod_set_value_cansleep(max98396->reset_gpio, 0);
		/* Wait for the hw reset done */
		usleep_range(5000, 6000);
	}

	ret = regmap_read(max98396->regmap,
			  GET_REG_ADDR_REV_ID(max98396->device_id), &reg);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s: failed to read revision of the device.\n",  id->name);
		return ret;
	}
	dev_info(&i2c->dev, "%s revision ID: 0x%02X\n", id->name, reg);

	/* codec registration */
	if (max98396->device_id == CODEC_TYPE_MAX98396)
		ret = devm_snd_soc_register_component(&i2c->dev,
						      &soc_codec_dev_max98396,
						      max98396_dai,
						      ARRAY_SIZE(max98396_dai));
	else
		ret = devm_snd_soc_register_component(&i2c->dev,
						      &soc_codec_dev_max98397,
						      max98397_dai,
						      ARRAY_SIZE(max98397_dai));
	if (ret < 0)
		dev_err(&i2c->dev, "Failed to register codec: %d\n", ret);

	return ret;
}

static const struct i2c_device_id max98396_i2c_id[] = {
	{ "max98396", CODEC_TYPE_MAX98396},
	{ "max98397", CODEC_TYPE_MAX98397},
	{ },
};

MODULE_DEVICE_TABLE(i2c, max98396_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id max98396_of_match[] = {
	{ .compatible = "adi,max98396", },
	{ .compatible = "adi,max98397", },
	{ }
};
MODULE_DEVICE_TABLE(of, max98396_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id max98396_acpi_match[] = {
	{ "ADS8396", 0 },
	{ "ADS8397", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, max98396_acpi_match);
#endif

static struct i2c_driver max98396_i2c_driver = {
	.driver = {
		.name = "max98396",
		.of_match_table = of_match_ptr(max98396_of_match),
		.acpi_match_table = ACPI_PTR(max98396_acpi_match),
		.pm = &max98396_pm,
	},
	.probe_new = max98396_i2c_probe,
	.id_table = max98396_i2c_id,
};

module_i2c_driver(max98396_i2c_driver)

MODULE_DESCRIPTION("ALSA SoC MAX98396 driver");
MODULE_AUTHOR("Ryan Lee <ryans.lee@analog.com>");
MODULE_LICENSE("GPL");
