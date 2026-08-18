// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

/* WSA885X codec driver */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

/* Control Registers - Audio Processing */
#define WSA885X_SMP_AMP_CTRL_STEREO_STEREO_SMP_AMP_CTRL_I2S    0x0000
#define WSA885X_SMP_AMP_CTRL_STEREO_CMT_GRP_MASK       0x0004
#define WSA885X_SMP_AMP_CTRL_STEREO_IT21_CLUSERINDEX   0x0140
#define WSA885X_SMP_AMP_CTRL_STEREO_CS21_CLOCK_VALID   0x0208
#define WSA885X_SMP_AMP_CTRL_STEREO_CS21_SAMPLERATEINDEX       0x0240
#define WSA885X_SMP_AMP_CTRL_STEREO_PPU21_POSTURENUMBER        0x0340
#define WSA885X_SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X0    0x4405
#define WSA885X_SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X1    0x4406
#define WSA885X_SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_LSB      0x4409
#define WSA885X_SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_MSB      0x6409
#define WSA885X_SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_LSB      0x440a
#define WSA885X_SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_MSB      0x640a
#define WSA885X_SMP_AMP_CTRL_STEREO_PDE23_REQ_PS       0x0a04
#define WSA885X_SMP_AMP_CTRL_STEREO_PDE23_ACT_PS       0x0a40
#define WSA885X_SMP_AMP_CTRL_STEREO_OT23_USAGE         0x0b10
#define WSA885X_SMP_AMP_CTRL_STEREO_CS24_SAMPLERATEINDEX       0x0e40

/* Analog Top Registers - Power and Clock Control */
#define WSA885X_ANA_TOP_PON_CKSK_CTL_0 0x800d
#define WSA885X_ANA_TOP_BG_TVP_UVLO1_PROG      0x8024
#define WSA885X_ANA_TOP_BG_TVP_UVLO2_PROG      0x8025
#define WSA885X_ANA_TOP_BG_TVP_OVRD_CTL        0x8034

/* Analog PLL Registers */
#define WSA885X_ANA_PLL_DIV_CTL_0      0x8090
#define WSA885X_ANA_PLL_DIV_CTL_1      0x8091
#define WSA885X_ANA_TOP_PLL_VCO_CTL    0x8092
#define WSA885X_ANA_TOP_PLL_LOOPFILT_0         0x8093
#define WSA885X_ANA_TOP_PLL_OVRD_CTL   0x8098
#define WSA885X_ANA_TOP_PLL_STATUS_0   0x809a
#define WSA885X_ANA_TOP_PLL_STATUS_1   0x809b

/* Analog Boost Control Registers */
#define WSA885X_ANA_TOP_BOOST_STB_CTRL2        0x805b
#define WSA885X_ANA_TOP_BOOST_STB_CTRL3        0x805c
#define WSA885X_ANA_TOP_BOOST_BYP_CTRL2        0x805e
#define WSA885X_ANA_TOP_BOOST_BYP_CTRL3        0x805f
#define WSA885X_ANA_TOP_BOOST_MISC     0x8063
#define WSA885X_ANA_TOP_BOOST_PWRSTAGE_CTRL2   0x8065
#define WSA885X_ANA_TOP_BOOST_PWRSTAGE_CTRL4   0x8067

/* Analog IV Sense ADC Registers */
#define WSA885X_ANA_TOP_IVSENSE_ADC_MODE_CTL2  0x80ca
#define WSA885X_ANA_TOP_IVSENSE_ADC_MODE_CTL3  0x80cb
#define WSA885X_ANA_TOP_IVSENSE_ADC_REF_CTL    0x80cc
#define WSA885X_ANA_TOP_IVSENSE_ADC_CDAC_CAL_CTL2      0x80d0

/* Analog Speaker Power Stage Registers */
#define WSA885X_ANA_TOP_SPK_TOP_PWRSTG_CH1_CTRL3       0x8108
#define WSA885X_ANA_TOP_SPK_TOP_PWRSTG_CH1_TUNE3       0x810b
#define WSA885X_ANA_TOP_SPK_TOP_PWRSTG_CH2_CTRL3       0x810e
#define WSA885X_ANA_TOP_SPK_TOP_PWRSTG_CH2_TUNE3       0x8111
#define WSA885X_ANA_TOP_SPK_TOP_SPARE3       0x813c
#define WSA885X_SPK_TOP_LF_CH1_CTRL11       0x811c
#define WSA885X_SPK_TOP_LF_CH1_TUNE1       0x811d
#define WSA885X_SPK_TOP_LF_CH2_TUNE1       0x8129
#define WSA885X_SPK_TOP_LF_CH1_CTRL9       0x811a
#define WSA885X_SPK_TOP_LF_CH2_CTRL9       0x8126
#define WSA885X_SPK_TOP_LF_CH2_CTRL11       0x8128
#define WSA885X_SPK_TOP_COMMON_CTRL2        0x8102
#define WSA885X_SPK_TOP_COMMON_TUNE1       0x8103
#define WSA885X_IVSENSE_VSNS_ISNS_CTL_CH1       0x80ba
#define WSA885X_DIG_CTRL0_TOP_CLK_CFG  0x8418
#define WSA885X_DIG_CTRL0_SDCA_COMMIT          0x8419
#define WSA885X_DIG_CTRL0_CLK_SOURCE_ENABLE    0x841a
#define WSA885X_DIG_CTRL0_SYS_CLK_SEL          0x841b
#define WSA885X_DIG_CTRL0_CDC_CLK_CTL          0x841c
#define WSA885X_DIG_CTRL0_PA_FSM_CTL   0x8420
#define WSA885X_DIG_CTRL0_POWER_FSM_CTL0       0x8423
#define WSA885X_DIG_CTRL0_POWER_FSM_CTL1       0x8424
#define WSA885X_DIG_CTRL0_PA0_FSM_CTL1         0x842b
#define WSA885X_DIG_CTRL0_PA1_FSM_CTL1         0x8435
#define WSA885X_DIG_CTRL0_VBAT_THRM_FLT_CTL    0x8458
#define WSA885X_DIG_CTRL0_CDC_RXTX_FSCNT_CTL   0x8470
#define WSA885X_DIG_CTRL0_GAIN_RAMP0_CTL1      0x84b4
#define WSA885X_DIG_CTRL0_GAIN_RAMP1_CTL1      0x84b7
#define WSA885X_DIG_CTRL0_PCM_DATA_WD0_CTL1    0x84A0
#define WSA885X_DIG_CTRL0_PCM_DATA_WD1_CTL1    0x84A4

/* Digital Control 1 Registers - I2S/TDM Interface */
#define WSA885X_DIG_CTRL1_I2S_CTL0     0x85A0
#define WSA885X_DIG_CTRL1_I2S_CFG0_TDM_TX      0x85A2
#define WSA885X_DIG_CTRL1_I2S_CFG1_TDM_TX      0x85A3
#define WSA885X_DIG_CTRL1_I2S_TDM_CTL0 0x85A7
#define WSA885X_DIG_CTRL1_I2S_TDM_CTL1 0x85A9
#define WSA885X_DIG_CTRL1_I2S_TDM_CH_RX        0x85AA
#define WSA885X_DIG_CTRL1_I2S_TDM_CH_TX        0x85AB
#define WSA885X_DIG_CTRL1_I2S_RESET_CTL        0x85AE

/* CDC RX Path Registers - Audio Data Path */
#define WSA885X_CDC_RX0_RX_PATH_CFG0   0x8601
#define WSA885X_CDC_RX0_RX_PATH_CFG1   0x8602
#define WSA885X_CDC_RX0_RX_PATH_CTL    0x8606
#define WSA885X_RX0_RX_PATH_DSMDEM_CTL 0x8613
#define WSA885X_CDC_RX1_RX_PATH_CFG0   0x8621
#define WSA885X_CDC_RX1_RX_PATH_CFG1   0x8622
#define WSA885X_CDC_RX1_RX_PATH_CTL    0x8626
#define WSA885X_RX1_RX_PATH_DSMDEM_CTL 0x8633

/* CDC Compander Registers - Dynamic Range Control */
#define WSA885X_CDC_COMPANDER0_CTL0    0x8640
#define WSA885X_CDC_COMPANDER0_CTL7    0x8647
#define WSA885X_CDC_COMPANDER1_CTL0    0x8660
#define WSA885X_CDC_COMPANDER1_CTL7    0x8667

/* CDC Speaker Protection Registers - IV Sense */
#define WSA885X_CDC_VSENSE0_SPKR_PROT_PATH_CTL 0x86A1
#define WSA885X_CDC_VSENSE1_SPKR_PROT_PATH_CTL 0x86B1
#define WSA885X_CDC_ISENSE0_SPKR_PROT_PATH_CTL 0x86A9
#define WSA885X_CDC_ISENSE1_SPKR_PROT_PATH_CTL 0x86B9

/* CDC Class-H Registers - Headroom Control */
#define WSA885X_CDC_CLSH_V1P8_BP_CTL1  0x86CD
#define WSA885X_CDC_CLSH_V1P8_BP_CTL0  0x86CC
#define WSA885X_CDC_CLSH_CLSH_SIG_DP_CTL0      0x86C7
#define WSA885X_CDC_CLSH_CLSH_V_HD_PA  0x86C3
#define WSA885X_CDC_CLSH_V1P8_BP_CTL2  0x86CE

/* Driver Constants */
#define WSA885X_CLK_RATE_FIXED 73728000
#define WSA885X_NUM_REGS       0x03

/* Interrupt Registers */
#define WSA885X_INTR_STATUS0   0x8584
#define WSA885X_INTR_MASK0     0x8581
#define WSA885X_INTR_CLEAR0    0x8587

/* Power and PA FSM Control Registers */
#define WSA885X_PA0_FSM_CTL0   0x842A
#define WSA885X_PA1_FSM_CTL0   0x8434

/* Digital Control GPIO and Interrupt Registers */
#define WSA885X_DIG_CTRL1_PIN_CT       0x8510
#define WSA885X_DIG_CTRL1_SPMI_PAD_GPIO2_CTL   0x8518
#define WSA885X_DIG_CTRL1_INTR_MODE    0x8580

#define WSA885X_I2S_CTL0_PCM_RATE_MASK         GENMASK(4, 1)
#define WSA885X_I2S_CTL0_ENABLE_MASK           BIT(0)
#define WSA885X_I2S_CTL0_PCM_RATE(v)           \
	FIELD_PREP(WSA885X_I2S_CTL0_PCM_RATE_MASK, (v))
#define WSA885X_I2S_CTL0_PCM_RATE_8KHZ         0x0
#define WSA885X_I2S_CTL0_PCM_RATE_16KHZ        0x1
#define WSA885X_I2S_CTL0_PCM_RATE_32KHZ        0x2
#define WSA885X_I2S_CTL0_PCM_RATE_48_OR_44KHZ  0x3
#define WSA885X_I2S_CTL0_PCM_RATE_96_OR_88KHZ  0x4
#define WSA885X_I2S_CTL0_PCM_RATE_192_OR_176KHZ 0x5
#define WSA885X_I2S_CTL0_PCM_RATE_384_OR_352KHZ 0x6
#define WSA885X_I2S_CFG0_TDM_TX_SLOT0_MASK     GENMASK(2, 0)
#define WSA885X_I2S_CFG0_TDM_TX_SLOT1_MASK     GENMASK(6, 4)
#define WSA885X_I2S_CFG0_TDM_TX_SLOT0(v)       \
	FIELD_PREP_CONST(WSA885X_I2S_CFG0_TDM_TX_SLOT0_MASK, (v))
#define WSA885X_I2S_CFG0_TDM_TX_SLOT1(v)       \
	FIELD_PREP_CONST(WSA885X_I2S_CFG0_TDM_TX_SLOT1_MASK, (v))
#define WSA885X_I2S_CFG1_TDM_TX_SLOT2_MASK     GENMASK(2, 0)
#define WSA885X_I2S_CFG1_TDM_TX_SLOT3_MASK     GENMASK(6, 4)
#define WSA885X_I2S_CFG1_TDM_TX_SLOT2(v)       \
	FIELD_PREP_CONST(WSA885X_I2S_CFG1_TDM_TX_SLOT2_MASK, (v))
#define WSA885X_I2S_CFG1_TDM_TX_SLOT3(v)       \
	FIELD_PREP_CONST(WSA885X_I2S_CFG1_TDM_TX_SLOT3_MASK, (v))
#define WSA885X_I2S_TDM_CTL0_I2S_TDM_EN_MASK   BIT(0)
#define WSA885X_I2S_TDM_CTL0_NUM_CHANNELS_MASK GENMASK(3, 2)
#define WSA885X_I2S_TDM_CTL0_NUM_CHANNELS_2    \
	FIELD_PREP_CONST(WSA885X_I2S_TDM_CTL0_NUM_CHANNELS_MASK, 0)
#define WSA885X_I2S_TDM_CTL0_NUM_CHANNELS_4    \
	FIELD_PREP_CONST(WSA885X_I2S_TDM_CTL0_NUM_CHANNELS_MASK, 1)
#define WSA885X_I2S_TDM_CTL0_NUM_CHANNELS_8    \
	FIELD_PREP_CONST(WSA885X_I2S_TDM_CTL0_NUM_CHANNELS_MASK, 3)
#define WSA885X_I2S_TDM_CH_TX_CH0_EN           BIT(0)
#define WSA885X_I2S_TDM_CH_TX_CH1_EN           BIT(1)
#define WSA885X_I2S_TDM_CH_TX_CH2_EN           BIT(2)
#define WSA885X_I2S_TDM_CH_TX_CH3_EN           BIT(3)
#define WSA885X_I2S_TDM_CH_RX_CH0_EN           BIT(0)
#define WSA885X_I2S_TDM_CH_RX_CH3_EN           BIT(3)
#define WSA885X_I2S_RESET_CTL_RESET_MASK       BIT(0)
#define WSA885X_PCM_DATA_WD_CTL1_PCM_DATA_WD_EN_MASK BIT(2)
#define WSA885X_POWER_FSM_CTL0_CLEAR_ERROR_MASK BIT(3)
#define WSA885X_PA_FSM_CTL0_CLEAR_ERROR_MASK   BIT(2)

#define WSA885X_I2S_TX_SLOT_ISENSE0            0x1
#define WSA885X_I2S_TX_SLOT_ISENSE1            0x2
#define WSA885X_I2S_TX_SLOT_CUR_SENSE0         0x5
#define WSA885X_I2S_TX_SLOT_CUR_SENSE1         0x6

/* RX Sample Rate Index Values - Audio Playback Path */
#define WSA885X_RX_RATE_8000HZ          0x00
#define WSA885X_RX_RATE_16000HZ         0x01
#define WSA885X_RX_RATE_32000HZ         0x02
#define WSA885X_RX_RATE_44100HZ         0x03
#define WSA885X_RX_RATE_48000HZ         0x04
#define WSA885X_RX_RATE_96000HZ         0x05
#define WSA885X_RX_RATE_192000HZ        0x06
#define WSA885X_RX_RATE_384000HZ        0x07

/* VI Sample Rate Index Values - Voltage/Current Sensing Path */
#define WSA885X_VI_RATE_8000HZ          0x00
#define WSA885X_VI_RATE_16000HZ         0x01
#define WSA885X_VI_RATE_44100HZ         0x02
#define WSA885X_VI_RATE_48000HZ         0x03
#define WSA885X_VI_RATE_96000HZ         0x04
#define WSA885X_VI_RATE_22050HZ         0x05
#define WSA885X_VI_RATE_24000HZ         0x06
#define WSA885X_VI_RATE_192000HZ        0x07
#define WSA885X_VI_RATE_384000HZ        0x08

/* Channel Configuration Masks */
#define WSA885X_CHANNEL_STEREO          0x03
#define WSA885X_CHANNEL_MONO_LEFT       0x01
#define WSA885X_CHANNEL_MONO_RIGHT      0x02

#define WSA885X_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 | \
			       SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 | \
			       SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)

#define WSA885X_PLL_LOCK_BIT            BIT(0)

#define WSA885X_FU21_VOL_STEPS 124
#define WSA885X_USAGE_MODE_MAX 8
static const DECLARE_TLV_DB_SCALE(wsa885x_fu21_digital_gain, -8400, 100, 0);

static bool wsa885x_is_valid_rx_slot_mask(u32 mask)
{
	return mask == WSA885X_CHANNEL_MONO_LEFT ||
	       mask == WSA885X_CHANNEL_MONO_RIGHT ||
	       mask == WSA885X_CHANNEL_STEREO;
}

static const char *const wsa885x_supply_name[] = {
	"vdd-1p8",
	"vdd-io",
};

enum {
	WSA885X_BATT_1S = 1,
	WSA885X_BATT_2S,
};

enum {
	WSA885X_IRQ_INT_SAF2WAR = 0,
	WSA885X_IRQ_INT_WAR2SAF,
	WSA885X_IRQ_INT_DISABLE,
	WSA885X_IRQ_INT_PA0_OCP,
	WSA885X_IRQ_INT_PA1_OCP,
	WSA885X_IRQ_INT_CLIP0,
	WSA885X_IRQ_INT_CLIP1,
	WSA885X_IRQ_INT_CLK_WD,
	WSA885X_IRQ_INT_INTR_GPIO1_PIN,
	WSA885X_IRQ_INT_INTR_GPIO2_PIN,
	WSA885X_IRQ_INT_UVLO,
	WSA885X_IRQ_INT_BOP,
	WSA885X_IRQ_INT_PA0_FSM_ERR,
	WSA885X_IRQ_INT_PA1_FSM_ERR,
	WSA885X_IRQ_INT_MAIN_FSM_ERR,
	WSA885X_IRQ_INT_PCM_DATA0_WD,
	WSA885X_IRQ_INT_PCM_DATA1_WD,
	WSA885X_IRQ_INT_PCM_DATA0_DC,
	WSA885X_IRQ_INT_PCM_DATA1_DC,
	WSA885X_IRQ_INT_PLL_UNLOCKED,
	WSA885X_IRQ_INT_PROT_MODE_CHANGE,
	WSA885X_IRQ_INT_PB_CLOCK_VALID,
	WSA885X_IRQ_INT_SENSE_CLOCK_VALID,
	WSA885X_IRQ_MAX,
};

struct wsa885x_priv {
	struct i2c_client *client;
	struct regmap *regmap;
	struct device *dev;
	struct snd_soc_component *component;
	struct gpio_desc *sd_n;
	struct reset_control *sd_reset;
	u32 usage_mode;
	u32 rx_slot_mask;
	u32 batt_conf;
	int stereo_vol_db;
	struct mutex state_lock; /* protects mutable control state */
};

struct wsa885x_reg_update {
	unsigned int reg;
	unsigned int mask;
	unsigned int val;
};

static const struct regmap_range_cfg wsa885x_regmap_ranges[] = {
	{
		.range_min = 0,
		.range_max = 0x88ff,
		.selector_reg = 0x0,
		.selector_mask = 0xFF,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 0x100,
	},
};

static const struct reg_default wsa885x_codec_reg_defaults[] = {
	{WSA885X_SMP_AMP_CTRL_STEREO_STEREO_SMP_AMP_CTRL_I2S, 0x00},
	{WSA885X_SMP_AMP_CTRL_STEREO_IT21_CLUSERINDEX, 0x01},
	{WSA885X_SMP_AMP_CTRL_STEREO_CMT_GRP_MASK, 0x00},
	{WSA885X_SMP_AMP_CTRL_STEREO_OT23_USAGE, 0x00},
	{WSA885X_SMP_AMP_CTRL_STEREO_CS21_CLOCK_VALID, 0x00},
	{WSA885X_SMP_AMP_CTRL_STEREO_CS21_SAMPLERATEINDEX, 0x04},
	{WSA885X_SMP_AMP_CTRL_STEREO_PPU21_POSTURENUMBER, 0x01},
	{WSA885X_SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X0, 0x01},
	{WSA885X_SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X1, 0x01},
	{WSA885X_SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_MSB, 0xac},
	{WSA885X_SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_LSB, 0x00},
	{WSA885X_SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_MSB, 0xac},
	{WSA885X_SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_LSB, 0x00},
	{WSA885X_SMP_AMP_CTRL_STEREO_PDE23_REQ_PS, 0x03},
	{WSA885X_SMP_AMP_CTRL_STEREO_PDE23_ACT_PS, 0x03},
	{WSA885X_SMP_AMP_CTRL_STEREO_CS24_SAMPLERATEINDEX, 0x03},
	{WSA885X_ANA_TOP_PON_CKSK_CTL_0, 0x00},
	{WSA885X_ANA_TOP_BG_TVP_UVLO1_PROG, 0x19},
	{WSA885X_ANA_TOP_BG_TVP_UVLO2_PROG, 0x22},
	{WSA885X_ANA_PLL_DIV_CTL_0, 0x0c},
	{WSA885X_ANA_PLL_DIV_CTL_1, 0x50},
	{WSA885X_ANA_TOP_PLL_VCO_CTL, 0x00},
	{WSA885X_ANA_TOP_PLL_LOOPFILT_0, 0xb4},
	{WSA885X_ANA_TOP_PLL_OVRD_CTL, 0x00},
	{WSA885X_ANA_TOP_BG_TVP_OVRD_CTL, 0x00},
	{WSA885X_ANA_TOP_BOOST_STB_CTRL2, 0x03},
	{WSA885X_ANA_TOP_BOOST_STB_CTRL3, 0x3c},
	{WSA885X_ANA_TOP_BOOST_BYP_CTRL2, 0xc5},
	{WSA885X_ANA_TOP_BOOST_BYP_CTRL3, 0x13},
	{WSA885X_ANA_TOP_BOOST_MISC, 0x79},
	{WSA885X_ANA_TOP_SPK_TOP_SPARE3, 0x00},
	{WSA885X_SPK_TOP_COMMON_CTRL2, 0x08},
	{WSA885X_SPK_TOP_LF_CH1_CTRL11, 0x09},
	{WSA885X_SPK_TOP_LF_CH1_TUNE1, 0x00},
	{WSA885X_SPK_TOP_LF_CH2_TUNE1, 0x00},
	{WSA885X_SPK_TOP_LF_CH1_CTRL9, 0x00},
	{WSA885X_SPK_TOP_LF_CH2_CTRL9, 0x00},
	{WSA885X_SPK_TOP_LF_CH2_CTRL11, 0x09},
	{WSA885X_SPK_TOP_COMMON_TUNE1, 0x03},
	{WSA885X_IVSENSE_VSNS_ISNS_CTL_CH1, 0x00},
	{WSA885X_DIG_CTRL0_CDC_CLK_CTL, 0x0e},
	{WSA885X_ANA_TOP_BOOST_PWRSTAGE_CTRL2, 0x40},
	{WSA885X_ANA_TOP_BOOST_PWRSTAGE_CTRL4, 0xff},
	{WSA885X_ANA_TOP_PLL_STATUS_0, 0x00},
	{WSA885X_ANA_TOP_PLL_STATUS_1, 0x00},
	{WSA885X_ANA_TOP_IVSENSE_ADC_MODE_CTL2, 0x84},
	{WSA885X_ANA_TOP_IVSENSE_ADC_MODE_CTL3, 0x02},
	{WSA885X_ANA_TOP_IVSENSE_ADC_REF_CTL, 0x00},
	{WSA885X_ANA_TOP_IVSENSE_ADC_CDAC_CAL_CTL2, 0xe0},
	{WSA885X_ANA_TOP_SPK_TOP_PWRSTG_CH1_CTRL3, 0xa4},
	{WSA885X_ANA_TOP_SPK_TOP_PWRSTG_CH1_TUNE3, 0xc9},
	{WSA885X_ANA_TOP_SPK_TOP_PWRSTG_CH2_CTRL3, 0xa4},
	{WSA885X_ANA_TOP_SPK_TOP_PWRSTG_CH2_TUNE3, 0xc9},
	{WSA885X_DIG_CTRL0_TOP_CLK_CFG, 0x00},
	{WSA885X_DIG_CTRL0_SDCA_COMMIT, 0x00},
	{WSA885X_DIG_CTRL0_CLK_SOURCE_ENABLE, 0x00},
	{WSA885X_DIG_CTRL0_SYS_CLK_SEL, 0x00},
	{WSA885X_DIG_CTRL0_PA_FSM_CTL, 0x00},
	{WSA885X_DIG_CTRL0_POWER_FSM_CTL0, 0x05},
	{WSA885X_DIG_CTRL0_POWER_FSM_CTL1, 0x00},
	{WSA885X_DIG_CTRL0_PA0_FSM_CTL1, 0x45},
	{WSA885X_DIG_CTRL0_PA1_FSM_CTL1, 0x45},
	{WSA885X_DIG_CTRL0_VBAT_THRM_FLT_CTL, 0x7f},
	{WSA885X_DIG_CTRL0_CDC_RXTX_FSCNT_CTL, 0x00},
	{WSA885X_DIG_CTRL0_GAIN_RAMP0_CTL1, 0x01},
	{WSA885X_DIG_CTRL0_GAIN_RAMP1_CTL1, 0x01},
	{WSA885X_DIG_CTRL1_I2S_CTL0, 0x06},
	{WSA885X_DIG_CTRL1_I2S_CFG0_TDM_TX, 0x00},
	{WSA885X_DIG_CTRL1_I2S_CFG1_TDM_TX, 0x00},
	{WSA885X_DIG_CTRL1_I2S_TDM_CTL0, 0x00},
	{WSA885X_DIG_CTRL1_I2S_TDM_CTL1, 0x05},
	{WSA885X_DIG_CTRL1_I2S_TDM_CH_TX, 0x00},
	{WSA885X_DIG_CTRL1_I2S_RESET_CTL, 0x00},
	{WSA885X_DIG_CTRL1_I2S_TDM_CH_RX, WSA885X_I2S_TDM_CH_RX_CH3_EN},
	{WSA885X_CDC_RX0_RX_PATH_CFG0, 0x89},
	{WSA885X_CDC_RX0_RX_PATH_CFG1, 0x64},
	{WSA885X_CDC_RX0_RX_PATH_CTL, 0x24},
	{WSA885X_RX0_RX_PATH_DSMDEM_CTL, 0x01},
	{WSA885X_CDC_RX1_RX_PATH_CFG0, 0x89},
	{WSA885X_CDC_RX1_RX_PATH_CFG1, 0x64},
	{WSA885X_CDC_RX1_RX_PATH_CTL, 0x04},
	{WSA885X_RX1_RX_PATH_DSMDEM_CTL, 0x01},
	{WSA885X_CDC_COMPANDER0_CTL0, 0x01},
	{WSA885X_CDC_COMPANDER0_CTL7, 0x2a},
	{WSA885X_CDC_COMPANDER1_CTL0, 0x01},
	{WSA885X_CDC_COMPANDER1_CTL7, 0x2a},
	{WSA885X_CDC_VSENSE0_SPKR_PROT_PATH_CTL, 0x14},
	{WSA885X_CDC_VSENSE1_SPKR_PROT_PATH_CTL, 0x14},
	{WSA885X_CDC_ISENSE0_SPKR_PROT_PATH_CTL, 0x14},
	{WSA885X_CDC_ISENSE1_SPKR_PROT_PATH_CTL, 0x14},
	{WSA885X_CDC_CLSH_V1P8_BP_CTL1, 0x50},
	{WSA885X_CDC_CLSH_V1P8_BP_CTL0, 0x6c},
	{WSA885X_CDC_CLSH_CLSH_SIG_DP_CTL0, 0x0d},
	{WSA885X_CDC_CLSH_CLSH_V_HD_PA, 0x03},
	{WSA885X_CDC_CLSH_V1P8_BP_CTL2, 0x05},
};

static void wsa885x_multi_update_bits(struct regmap *regmap,
				      const struct wsa885x_reg_update *updates,
				      size_t num_updates)
{
	size_t i;

	for (i = 0; i < num_updates; i++)
		regmap_update_bits(regmap, updates[i].reg,
				   updates[i].mask, updates[i].val);
}

static void wsa885x_toggle_irq_bit(struct wsa885x_priv *wsa885x,
				   unsigned int reg, unsigned int mask)
{
	regmap_update_bits(wsa885x->regmap, reg, mask, 0);
	regmap_update_bits(wsa885x->regmap, reg, mask, mask);
}

static void wsa885x_pulse_irq_bit(struct wsa885x_priv *wsa885x,
				  unsigned int reg, unsigned int mask)
{
	regmap_update_bits(wsa885x->regmap, reg, mask, 0);
	regmap_update_bits(wsa885x->regmap, reg, mask, mask);
	regmap_update_bits(wsa885x->regmap, reg, mask, 0);
}

static int wsa885x_tdm_ctl0_slot_num_val(int slots, unsigned int *slot_num_val)
{
	if (!slot_num_val)
		return -EINVAL;

	switch (slots) {
	case 2:
		*slot_num_val = WSA885X_I2S_TDM_CTL0_NUM_CHANNELS_2;
		return 0;
	case 4:
		*slot_num_val = WSA885X_I2S_TDM_CTL0_NUM_CHANNELS_4;
		return 0;
	case 8:
		*slot_num_val = WSA885X_I2S_TDM_CTL0_NUM_CHANNELS_8;
		return 0;
	default:
		return -EINVAL;
	}
}

static int wsa885x_reg_update_sequence(struct regmap *regmap, int slots)
{
	static const struct reg_sequence regs[] = {
		{ WSA885X_DIG_CTRL1_I2S_TDM_CTL1, 0x15 },
		{ WSA885X_DIG_CTRL1_I2S_TDM_CTL1, 0x11 },
	};
	unsigned int slot_num_val;
	int ret;

	if (!regmap)
		return -EINVAL;

	ret = wsa885x_tdm_ctl0_slot_num_val(slots, &slot_num_val);
	if (ret)
		return ret;

	regmap_multi_reg_write(regmap, regs, ARRAY_SIZE(regs));

	regmap_update_bits(regmap, WSA885X_DIG_CTRL1_I2S_TDM_CTL0,
			   WSA885X_I2S_TDM_CTL0_NUM_CHANNELS_MASK,
			   slot_num_val);
	regmap_update_bits(regmap, WSA885X_DIG_CTRL1_I2S_TDM_CTL0,
			   WSA885X_I2S_TDM_CTL0_I2S_TDM_EN_MASK,
			   WSA885X_I2S_TDM_CTL0_I2S_TDM_EN_MASK);
	regmap_write(regmap, WSA885X_DIG_CTRL1_I2S_TDM_CH_TX,
		     WSA885X_I2S_TDM_CH_TX_CH0_EN);
	regmap_update_bits(regmap, WSA885X_DIG_CTRL1_I2S_TDM_CH_TX,
			   WSA885X_I2S_TDM_CH_TX_CH1_EN,
			   WSA885X_I2S_TDM_CH_TX_CH1_EN);

	return 0;
}

static int wsa885x_wait_for_pll_lock(struct wsa885x_priv *wsa885x)
{
	unsigned int status = 0;
	int cnt = 0, ret = 0;

	do {
		usleep_range(1000, 1100);
		ret = regmap_read(wsa885x->regmap, WSA885X_ANA_TOP_PLL_STATUS_0, &status);
		if (ret) {
			dev_err(wsa885x->dev, "PLL status read failed: %d\n", ret);
			return ret;
		}

		if (status & WSA885X_PLL_LOCK_BIT)
			return 0;
	} while (++cnt < 20);

	dev_warn(wsa885x->dev, "PLL lock timeout after 20ms, status=0x%x\n", status);
	return -ETIMEDOUT;
}

static int wsa885x_2s_conf(struct wsa885x_priv *wsa885x)
{
	static const struct reg_sequence regs[] = {
		{ WSA885X_SPK_TOP_COMMON_TUNE1, 0x26 },
		{ WSA885X_SPK_TOP_LF_CH1_CTRL11, 0x0d },
		{ WSA885X_SPK_TOP_LF_CH2_CTRL11, 0x0d },
		{ WSA885X_CDC_CLSH_V1P8_BP_CTL1, 0x71 },
		{ WSA885X_CDC_CLSH_V1P8_BP_CTL0, 0xAA },
	};

	return regmap_multi_reg_write(wsa885x->regmap, regs, ARRAY_SIZE(regs));
}

static const struct reg_sequence wsa885x_reg_init[] = {
	{ WSA885X_CDC_RX0_RX_PATH_CTL, 0x24 },
	{ WSA885X_CDC_RX1_RX_PATH_CTL, 0x24 },
	{ WSA885X_RX0_RX_PATH_DSMDEM_CTL, 0x01 },
	{ WSA885X_RX1_RX_PATH_DSMDEM_CTL, 0x01 },
	{ WSA885X_CDC_COMPANDER0_CTL0, 0x01 },
	{ WSA885X_CDC_COMPANDER1_CTL0, 0x01 },
	{ WSA885X_CDC_VSENSE0_SPKR_PROT_PATH_CTL, 0x14 },
	{ WSA885X_CDC_VSENSE1_SPKR_PROT_PATH_CTL, 0x14 },
	{ WSA885X_CDC_ISENSE0_SPKR_PROT_PATH_CTL, 0x14 },
	{ WSA885X_CDC_ISENSE1_SPKR_PROT_PATH_CTL, 0x14 },
	{ WSA885X_DIG_CTRL0_CDC_CLK_CTL, 0x0f },
	{ WSA885X_DIG_CTRL0_CDC_CLK_CTL, 0x4f },
	{ WSA885X_DIG_CTRL0_CDC_RXTX_FSCNT_CTL, 0x02 },
	{ WSA885X_DIG_CTRL0_CDC_RXTX_FSCNT_CTL, 0x00 },
	{ WSA885X_DIG_CTRL0_CDC_RXTX_FSCNT_CTL, 0x01 },
	{ WSA885X_SMP_AMP_CTRL_STEREO_CMT_GRP_MASK, 0x01 },
	{ WSA885X_CDC_RX0_RX_PATH_CFG1, 0x60 },
	{ WSA885X_CDC_RX1_RX_PATH_CFG1, 0x60 },
	{ WSA885X_ANA_TOP_SPK_TOP_PWRSTG_CH1_CTRL3, 0xa5 },
	{ WSA885X_ANA_TOP_SPK_TOP_PWRSTG_CH2_CTRL3, 0xa5 },
	{ WSA885X_ANA_TOP_IVSENSE_ADC_MODE_CTL2, 0x85 },
	{ WSA885X_ANA_TOP_IVSENSE_ADC_MODE_CTL3, 0x0c },
	{ WSA885X_ANA_TOP_IVSENSE_ADC_MODE_CTL3, 0x0e },
	{ WSA885X_ANA_TOP_IVSENSE_ADC_REF_CTL, 0x0c },
	{ WSA885X_DIG_CTRL0_GAIN_RAMP0_CTL1, 0x01 },
	{ WSA885X_DIG_CTRL0_GAIN_RAMP1_CTL1, 0x01 },
	{ WSA885X_CDC_RX0_RX_PATH_CFG0, 0x88 },
	{ WSA885X_CDC_RX0_RX_PATH_CFG0, 0x89 },
	{ WSA885X_CDC_RX1_RX_PATH_CFG0, 0x88 },
	{ WSA885X_CDC_RX1_RX_PATH_CFG0, 0x89 },
	{ WSA885X_ANA_TOP_BOOST_STB_CTRL2, 0x82 },
	{ WSA885X_ANA_TOP_BOOST_STB_CTRL3, 0x34 },
	{ WSA885X_ANA_TOP_BOOST_PWRSTAGE_CTRL2, 0x41 },
	{ WSA885X_ANA_TOP_BOOST_PWRSTAGE_CTRL4, 0x7f },
	{ WSA885X_CDC_CLSH_V1P8_BP_CTL1, 0x50 },
	{ WSA885X_CDC_CLSH_V1P8_BP_CTL0, 0x6c },
	{ WSA885X_CDC_CLSH_CLSH_SIG_DP_CTL0, 0x0d },
	{ WSA885X_CDC_CLSH_CLSH_V_HD_PA, 0x03 },
	{ WSA885X_DIG_CTRL0_POWER_FSM_CTL0, 0x05 },
	{ WSA885X_ANA_TOP_PON_CKSK_CTL_0, 0x20 },
	{ WSA885X_ANA_TOP_SPK_TOP_PWRSTG_CH1_TUNE3, 0x45 },
	{ WSA885X_ANA_TOP_SPK_TOP_PWRSTG_CH2_TUNE3, 0x45 },
	{ WSA885X_CDC_CLSH_V1P8_BP_CTL2, 0x05 },
	{ WSA885X_ANA_TOP_BG_TVP_UVLO1_PROG, 0x35 },
	{ WSA885X_ANA_TOP_BG_TVP_UVLO2_PROG, 0x21 },
	{ WSA885X_ANA_TOP_BOOST_BYP_CTRL2, 0xc7 },
	{ WSA885X_ANA_TOP_BOOST_BYP_CTRL3, 0x11 },
	{ WSA885X_ANA_TOP_IVSENSE_ADC_CDAC_CAL_CTL2, 0x80 },
	{ WSA885X_ANA_TOP_SPK_TOP_SPARE3, 0x08 },
	{ WSA885X_DIG_CTRL0_PA0_FSM_CTL1, 0x47 },
	{ WSA885X_DIG_CTRL0_PA1_FSM_CTL1, 0x47 },
	{ WSA885X_CDC_COMPANDER0_CTL7, 0x34 },
	{ WSA885X_CDC_COMPANDER1_CTL7, 0x34 },
	{ WSA885X_DIG_CTRL0_VBAT_THRM_FLT_CTL, 0x79 },
};

static int wsa885x_hw_init(struct wsa885x_priv *wsa885x)
{
	static const struct reg_sequence regs[] = {
		{ WSA885X_DIG_CTRL1_SPMI_PAD_GPIO2_CTL, 0x2e },
		{ WSA885X_DIG_CTRL1_INTR_MODE, 0x01 },
		{ WSA885X_DIG_CTRL1_PIN_CT, 0x04 },
	};
	int ret;

	ret = regmap_multi_reg_write(wsa885x->regmap, wsa885x_reg_init,
				     ARRAY_SIZE(wsa885x_reg_init));
	if (ret)
		return ret;

	if (wsa885x->batt_conf == WSA885X_BATT_2S) {
		ret = wsa885x_2s_conf(wsa885x);
		if (ret)
			return ret;
	}

	return regmap_multi_reg_write(wsa885x->regmap, regs, ARRAY_SIZE(regs));
}

static int wsa885x_unmask_interrupts(struct wsa885x_priv *wsa885x)
{
	static const struct reg_sequence regs[] = {
		{ WSA885X_INTR_MASK0, 0x00 },
		{ WSA885X_INTR_MASK0 + 1, 0x00 },
		{ WSA885X_INTR_MASK0 + 2, 0xf8 },
	};

	return regmap_multi_reg_write(wsa885x->regmap, regs, ARRAY_SIZE(regs));
}

static int wsa885x_wait_for_pde_state(struct wsa885x_priv *wsa885x, int ps)
{
	unsigned int act_ps = 0, clock_valid = 0;
	int rc = 0, cnt = 0;

	if (ps < 0 || ps > 3)
		return -EINVAL;

	do {
		usleep_range(1000, 1500);
		rc = regmap_read(wsa885x->regmap,
				 WSA885X_SMP_AMP_CTRL_STEREO_PDE23_ACT_PS,
				 &act_ps);
		if (rc) {
			dev_err(wsa885x->dev, "PDE state read failed: %d\n", rc);
			return rc;
		}
		if (act_ps == ps)
			return 0;
	} while (++cnt < 5);

	if (regmap_read(wsa885x->regmap,
			WSA885X_SMP_AMP_CTRL_STEREO_CS21_CLOCK_VALID,
			&clock_valid))
		dev_err(wsa885x->dev,
			"PDE power state %d request failed, actual_ps %d, clock_valid read failed\n",
			ps, act_ps);
	else
		dev_err(wsa885x->dev,
			"PDE power state %d request failed, actual_ps %d, clock_valid:%d\n",
			ps, act_ps, clock_valid);

	return -ETIMEDOUT;
}

static void wsa885x_program_stereo_volume(struct wsa885x_priv *wsa885x,
					  int stereo_vol_db, bool commit)
{
	regmap_write(wsa885x->regmap,
		     WSA885X_SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_MSB,
		     (u8)(s8)stereo_vol_db);
	regmap_write(wsa885x->regmap,
		     WSA885X_SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_LSB, 0x00);
	regmap_write(wsa885x->regmap,
		     WSA885X_SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_MSB,
		     (u8)(s8)stereo_vol_db);
	regmap_write(wsa885x->regmap,
		     WSA885X_SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_LSB, 0x00);

	if (commit)
		regmap_write(wsa885x->regmap, WSA885X_DIG_CTRL0_SDCA_COMMIT, 0x01);
}

static int wsa885x_codec_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct wsa885x_priv *wsa885x;
	u8 pcm_rate, cs21_sample_rate_idx, cs24_sample_rate_idx;

	wsa885x = snd_soc_component_get_drvdata(dai->component);

	switch (params_rate(params)) {
	case 8000:
		pcm_rate = WSA885X_I2S_CTL0_PCM_RATE_8KHZ;
		cs21_sample_rate_idx = WSA885X_RX_RATE_8000HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_8000HZ;
		break;
	case 16000:
		pcm_rate = WSA885X_I2S_CTL0_PCM_RATE_16KHZ;
		cs21_sample_rate_idx = WSA885X_RX_RATE_16000HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_16000HZ;
		break;
	case 32000:
		pcm_rate = WSA885X_I2S_CTL0_PCM_RATE_32KHZ;
		cs21_sample_rate_idx = WSA885X_RX_RATE_32000HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_48000HZ;
		break;
	case 44100:
		pcm_rate = WSA885X_I2S_CTL0_PCM_RATE_48_OR_44KHZ;
		cs21_sample_rate_idx = WSA885X_RX_RATE_44100HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_44100HZ;
		break;
	case 48000:
		pcm_rate = WSA885X_I2S_CTL0_PCM_RATE_48_OR_44KHZ;
		cs21_sample_rate_idx = WSA885X_RX_RATE_48000HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_48000HZ;
		break;
	case 88200:
	case 96000:
		pcm_rate = WSA885X_I2S_CTL0_PCM_RATE_96_OR_88KHZ;
		cs21_sample_rate_idx = WSA885X_RX_RATE_96000HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_96000HZ;
		break;
	case 176400:
	case 192000:
		pcm_rate = WSA885X_I2S_CTL0_PCM_RATE_192_OR_176KHZ;
		cs21_sample_rate_idx = WSA885X_RX_RATE_192000HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_192000HZ;
		break;
	case 352800:
	case 384000:
		pcm_rate = WSA885X_I2S_CTL0_PCM_RATE_384_OR_352KHZ;
		cs21_sample_rate_idx = WSA885X_RX_RATE_384000HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_384000HZ;
		break;
	default:
		dev_err(wsa885x->dev, "sampling rate %d is not supported\n", params_rate(params));
		return -EINVAL;
	}

	regmap_update_bits(wsa885x->regmap, WSA885X_DIG_CTRL1_I2S_CTL0,
			   WSA885X_I2S_CTL0_PCM_RATE_MASK |
			   WSA885X_I2S_CTL0_ENABLE_MASK,
			   WSA885X_I2S_CTL0_PCM_RATE(pcm_rate) |
			   WSA885X_I2S_CTL0_ENABLE_MASK);
	regmap_write(wsa885x->regmap, WSA885X_DIG_CTRL1_I2S_RESET_CTL, 0x00);
	regmap_write(wsa885x->regmap, WSA885X_SMP_AMP_CTRL_STEREO_CS21_SAMPLERATEINDEX,
		     cs21_sample_rate_idx);
	regmap_write(wsa885x->regmap, WSA885X_SMP_AMP_CTRL_STEREO_CS24_SAMPLERATEINDEX,
		     cs24_sample_rate_idx);

	mutex_lock(&wsa885x->state_lock);
	wsa885x_program_stereo_volume(wsa885x, wsa885x->stereo_vol_db, false);
	mutex_unlock(&wsa885x->state_lock);

	regmap_write(wsa885x->regmap, WSA885X_DIG_CTRL0_SDCA_COMMIT, 0x01);

	return 0;
}

static int wsa885x_codec_set_tdm_slot(struct snd_soc_dai *dai,
				      unsigned int tx_slot_mask,
				      unsigned int rx_slot_mask, int slots,
				      int slot_width)
{
	static const struct wsa885x_reg_update stereo_updates[] = {
		{ WSA885X_DIG_CTRL1_I2S_CFG0_TDM_TX, WSA885X_I2S_CFG0_TDM_TX_SLOT0_MASK,
		  WSA885X_I2S_CFG0_TDM_TX_SLOT0(WSA885X_I2S_TX_SLOT_ISENSE0) },
		{ WSA885X_DIG_CTRL1_I2S_CFG0_TDM_TX, WSA885X_I2S_CFG0_TDM_TX_SLOT1_MASK,
		  WSA885X_I2S_CFG0_TDM_TX_SLOT1(WSA885X_I2S_TX_SLOT_ISENSE1) },
		{ WSA885X_DIG_CTRL1_I2S_CFG1_TDM_TX, WSA885X_I2S_CFG1_TDM_TX_SLOT2_MASK,
		  WSA885X_I2S_CFG1_TDM_TX_SLOT2(WSA885X_I2S_TX_SLOT_CUR_SENSE0) },
		{ WSA885X_DIG_CTRL1_I2S_CFG1_TDM_TX, WSA885X_I2S_CFG1_TDM_TX_SLOT3_MASK,
		  WSA885X_I2S_CFG1_TDM_TX_SLOT3(WSA885X_I2S_TX_SLOT_CUR_SENSE1) },
	};
	static const struct wsa885x_reg_update mono_left_updates[] = {
		{ WSA885X_DIG_CTRL1_I2S_CFG0_TDM_TX, WSA885X_I2S_CFG0_TDM_TX_SLOT0_MASK,
		  WSA885X_I2S_CFG0_TDM_TX_SLOT0(WSA885X_I2S_TX_SLOT_ISENSE0) },
		{ WSA885X_DIG_CTRL1_I2S_CFG0_TDM_TX, WSA885X_I2S_CFG0_TDM_TX_SLOT1_MASK,
		  WSA885X_I2S_CFG0_TDM_TX_SLOT1(WSA885X_I2S_TX_SLOT_CUR_SENSE0) },
	};
	static const struct wsa885x_reg_update mono_right_updates[] = {
		{ WSA885X_DIG_CTRL1_I2S_CFG0_TDM_TX, WSA885X_I2S_CFG0_TDM_TX_SLOT0_MASK,
		  WSA885X_I2S_CFG0_TDM_TX_SLOT0(WSA885X_I2S_TX_SLOT_ISENSE1) },
		{ WSA885X_DIG_CTRL1_I2S_CFG0_TDM_TX, WSA885X_I2S_CFG0_TDM_TX_SLOT1_MASK,
		  WSA885X_I2S_CFG0_TDM_TX_SLOT1(WSA885X_I2S_TX_SLOT_CUR_SENSE1) },
	};
	struct wsa885x_priv *wsa885x;
	unsigned int slot_num_val;
	u32 mask;
	int ret;

	wsa885x = snd_soc_component_get_drvdata(dai->component);

	ret = wsa885x_tdm_ctl0_slot_num_val(slots, &slot_num_val);
	if (ret) {
		dev_err(wsa885x->dev, "%s: unsupported slot count %d\n",
			__func__, slots);
		return ret;
	}

	if (rx_slot_mask && !wsa885x_is_valid_rx_slot_mask(rx_slot_mask)) {
		dev_err(wsa885x->dev,
			"%s: unsupported rx_slot_mask 0x%x\n",
			__func__, rx_slot_mask);
		return -EINVAL;
	}

	mutex_lock(&wsa885x->state_lock);
	if (rx_slot_mask)
		wsa885x->rx_slot_mask = rx_slot_mask;
	else if (!wsa885x_is_valid_rx_slot_mask(wsa885x->rx_slot_mask))
		wsa885x->rx_slot_mask = WSA885X_CHANNEL_STEREO;
	mask = wsa885x->rx_slot_mask;

	regmap_update_bits(wsa885x->regmap, WSA885X_DIG_CTRL1_I2S_RESET_CTL,
			   WSA885X_I2S_RESET_CTL_RESET_MASK,
			   WSA885X_I2S_RESET_CTL_RESET_MASK);

	if (mask == WSA885X_CHANNEL_STEREO) {
		wsa885x_multi_update_bits(wsa885x->regmap, stereo_updates,
					  ARRAY_SIZE(stereo_updates));
		ret = wsa885x_reg_update_sequence(wsa885x->regmap, slots);
		if (ret)
			goto exit_unlock;
		regmap_update_bits(wsa885x->regmap, WSA885X_DIG_CTRL1_I2S_TDM_CH_TX,
				   WSA885X_I2S_TDM_CH_TX_CH2_EN,
				   WSA885X_I2S_TDM_CH_TX_CH2_EN);
		regmap_update_bits(wsa885x->regmap, WSA885X_DIG_CTRL1_I2S_TDM_CH_TX,
				   WSA885X_I2S_TDM_CH_TX_CH3_EN,
				   WSA885X_I2S_TDM_CH_TX_CH3_EN);
	} else if (mask == WSA885X_CHANNEL_MONO_LEFT) {
		wsa885x_multi_update_bits(wsa885x->regmap, mono_left_updates,
					  ARRAY_SIZE(mono_left_updates));
		ret = wsa885x_reg_update_sequence(wsa885x->regmap, slots);
		if (ret)
			goto exit_unlock;
	} else if (mask == WSA885X_CHANNEL_MONO_RIGHT) {
		wsa885x_multi_update_bits(wsa885x->regmap, mono_right_updates,
					  ARRAY_SIZE(mono_right_updates));
		ret = wsa885x_reg_update_sequence(wsa885x->regmap, slots);
		if (ret)
			goto exit_unlock;
	}

	regmap_update_bits(wsa885x->regmap, WSA885X_DIG_CTRL1_I2S_CTL0,
			   WSA885X_I2S_CTL0_ENABLE_MASK,
			   WSA885X_I2S_CTL0_ENABLE_MASK);
	regmap_update_bits(wsa885x->regmap, WSA885X_DIG_CTRL1_I2S_RESET_CTL,
			   WSA885X_I2S_RESET_CTL_RESET_MASK, 0);

	ret = 0;

exit_unlock:
	mutex_unlock(&wsa885x->state_lock);

	return ret;
}

static int wsa885x_codec_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				    unsigned int freq, int dir)
{
	static const struct reg_sequence pll_prep[] = {
		{ WSA885X_ANA_TOP_BG_TVP_OVRD_CTL, 0x03 },
		{ WSA885X_DIG_CTRL0_SYS_CLK_SEL, 0x04 },
		{ WSA885X_ANA_TOP_PLL_LOOPFILT_0, 0xB4 },
		{ WSA885X_ANA_TOP_PLL_VCO_CTL, 0x00 },
		{ WSA885X_ANA_TOP_PLL_OVRD_CTL, 0x00 },
	};
	static const struct reg_sequence pll_cleanup[] = {
		{ WSA885X_DIG_CTRL0_CLK_SOURCE_ENABLE, 0x00 },
		{ WSA885X_DIG_CTRL0_SYS_CLK_SEL, 0x00 },
		{ WSA885X_ANA_TOP_BG_TVP_OVRD_CTL, 0x00 },
	};
	struct wsa885x_priv *wsa885x;
	u32 pll_div;
	int ret = 0;

	wsa885x = snd_soc_component_get_drvdata(dai->component);

	if (!freq)
		return -EINVAL;
	if (WSA885X_CLK_RATE_FIXED % freq)
		return -EINVAL;
	pll_div = WSA885X_CLK_RATE_FIXED / freq;
	if (pll_div > 0xff)
		return -EINVAL;

	regmap_multi_reg_write(wsa885x->regmap, pll_prep, ARRAY_SIZE(pll_prep));
	regmap_write(wsa885x->regmap, WSA885X_ANA_PLL_DIV_CTL_0, pll_div);
	regmap_write(wsa885x->regmap, WSA885X_DIG_CTRL0_CLK_SOURCE_ENABLE, 0x02);

	ret = wsa885x_wait_for_pll_lock(wsa885x);
	if (ret) {
		dev_err(wsa885x->dev, "PLL lock failed, aborting sysclk configuration\n");
		regmap_multi_reg_write(wsa885x->regmap, pll_cleanup,
				       ARRAY_SIZE(pll_cleanup));
		return ret;
	}

	regmap_write(wsa885x->regmap, WSA885X_DIG_CTRL0_SYS_CLK_SEL, 0x00);
	regmap_write(wsa885x->regmap, WSA885X_DIG_CTRL0_POWER_FSM_CTL1, 0x01);
	regmap_write(wsa885x->regmap, WSA885X_ANA_TOP_BG_TVP_OVRD_CTL, 0x00);

	return 0;
}

static int wsa885x_codec_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	static const struct reg_sequence mute_regs[] = {
		{ WSA885X_DIG_CTRL0_PA_FSM_CTL, 0x00 },
		{ WSA885X_SMP_AMP_CTRL_STEREO_PDE23_REQ_PS, 0x03 },
	};
	static const struct reg_sequence mute_commit_regs[] = {
		{ WSA885X_SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X0, 0x01 },
		{ WSA885X_SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X1, 0x01 },
		{ WSA885X_DIG_CTRL0_SDCA_COMMIT, 0x01 },
	};
	static const struct reg_sequence unmute_prep_head_regs[] = {
		{ WSA885X_DIG_CTRL0_PA_FSM_CTL, 0x00 },
	};
	static const struct reg_sequence unmute_prep_tail_regs[] = {
		{ WSA885X_SMP_AMP_CTRL_STEREO_IT21_CLUSERINDEX, 0x01 },
		{ WSA885X_SMP_AMP_CTRL_STEREO_PPU21_POSTURENUMBER, 0x01 },
	};
	static const struct reg_sequence unmute_volume_regs[] = {
		{ WSA885X_SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_LSB, 0x00 },
		{ WSA885X_SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_LSB, 0x00 },
	};
	static const struct reg_sequence unmute_commit_regs[] = {
		{ WSA885X_DIG_CTRL0_SDCA_COMMIT, 0x01 },
		{ WSA885X_SMP_AMP_CTRL_STEREO_PDE23_REQ_PS, 0x00 },
	};
	static const struct reg_sequence unmute_finish_regs[] = {
		{ WSA885X_SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X0, 0x00 },
		{ WSA885X_SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X1, 0x00 },
		{ WSA885X_DIG_CTRL0_SDCA_COMMIT, 0x01 },
	};
	struct wsa885x_priv *wsa885x;
	int ret = 0, ps0 = 0, ps3 = 3;

	wsa885x = snd_soc_component_get_drvdata(dai->component);

	if (stream != SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	mutex_lock(&wsa885x->state_lock);

	if (wsa885x->usage_mode > WSA885X_USAGE_MODE_MAX) {
		ret = -EINVAL;
		goto exit_unlock;
	}

	if (!wsa885x_is_valid_rx_slot_mask(wsa885x->rx_slot_mask))
		wsa885x->rx_slot_mask = WSA885X_CHANNEL_STEREO;

	if (mute) {
		regmap_multi_reg_write(wsa885x->regmap, mute_regs,
				       ARRAY_SIZE(mute_regs));
		ret = wsa885x_wait_for_pde_state(wsa885x, ps3);
		if (ret) {
			dev_err(wsa885x->dev,
				"PS3 transition failed: %d\n", ret);
		} else {
			regmap_multi_reg_write(wsa885x->regmap, mute_commit_regs,
					       ARRAY_SIZE(mute_commit_regs));
		}
	} else {
		regmap_multi_reg_write(wsa885x->regmap, unmute_prep_head_regs,
				       ARRAY_SIZE(unmute_prep_head_regs));
		regmap_write(wsa885x->regmap, WSA885X_SMP_AMP_CTRL_STEREO_OT23_USAGE,
			     wsa885x->usage_mode);
		regmap_multi_reg_write(wsa885x->regmap, unmute_prep_tail_regs,
				       ARRAY_SIZE(unmute_prep_tail_regs));
		wsa885x_program_stereo_volume(wsa885x, wsa885x->stereo_vol_db, false);
		regmap_multi_reg_write(wsa885x->regmap, unmute_volume_regs,
				       ARRAY_SIZE(unmute_volume_regs));
		regmap_multi_reg_write(wsa885x->regmap, unmute_commit_regs,
				       ARRAY_SIZE(unmute_commit_regs));
		ret = wsa885x_wait_for_pde_state(wsa885x, ps0);
		if (ret)
			goto exit_unlock;

		if (wsa885x->rx_slot_mask == WSA885X_CHANNEL_STEREO) {
			regmap_write(wsa885x->regmap, WSA885X_DIG_CTRL1_I2S_TDM_CH_RX,
				     WSA885X_I2S_TDM_CH_RX_CH0_EN |
				     WSA885X_I2S_TDM_CH_RX_CH3_EN);
			regmap_write(wsa885x->regmap, WSA885X_DIG_CTRL0_PA_FSM_CTL, 0x03);
		} else if (wsa885x->rx_slot_mask == WSA885X_CHANNEL_MONO_LEFT) {
			regmap_write(wsa885x->regmap, WSA885X_DIG_CTRL1_I2S_TDM_CH_RX,
				     WSA885X_I2S_TDM_CH_RX_CH3_EN);
			regmap_write(wsa885x->regmap, WSA885X_DIG_CTRL0_PA_FSM_CTL, 0x01);
		} else if (wsa885x->rx_slot_mask == WSA885X_CHANNEL_MONO_RIGHT) {
			regmap_write(wsa885x->regmap, WSA885X_DIG_CTRL1_I2S_TDM_CH_RX,
				     WSA885X_I2S_TDM_CH_RX_CH0_EN);
			regmap_write(wsa885x->regmap, WSA885X_DIG_CTRL0_PA_FSM_CTL, 0x02);
		}

		regmap_multi_reg_write(wsa885x->regmap, unmute_finish_regs,
				       ARRAY_SIZE(unmute_finish_regs));
	}

exit_unlock:
	mutex_unlock(&wsa885x->state_lock);

	return ret;
}

static int wsa885x_codec_hw_free(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	static const struct reg_sequence regs[] = {
		{ WSA885X_DIG_CTRL0_PA_FSM_CTL, 0x00 },
	};
	struct wsa885x_priv *wsa885x;

	wsa885x = snd_soc_component_get_drvdata(dai->component);

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	mutex_lock(&wsa885x->state_lock);
	regmap_multi_reg_write(wsa885x->regmap, regs, ARRAY_SIZE(regs));
	mutex_unlock(&wsa885x->state_lock);

	return 0;
}

static const struct snd_soc_dai_ops wsa885x_dai_ops = {
	.hw_params = wsa885x_codec_hw_params,
	.set_tdm_slot = wsa885x_codec_set_tdm_slot,
	.set_sysclk = wsa885x_codec_set_sysclk,
	.mute_stream = wsa885x_codec_mute_stream,
	.hw_free = wsa885x_codec_hw_free,
};

static struct snd_soc_dai_driver wsa885x_dai[] = {
	{
		.name = "wsa885x_dai_drv",
		.playback = {
			.stream_name = "WSA885X TDM Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = WSA885X_RATES,
			.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
					   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &wsa885x_dai_ops,
	},
};

static void wsa885x_reset_assert(void *data)
{
	struct wsa885x_priv *wsa885x = data;

	if (wsa885x->sd_reset)
		reset_control_assert(wsa885x->sd_reset);
	else
		gpiod_direction_output(wsa885x->sd_n, 1);
}

static void wsa885x_reset_deassert(struct wsa885x_priv *wsa885x)
{
	if (wsa885x->sd_reset)
		reset_control_deassert(wsa885x->sd_reset);
	else
		gpiod_direction_output(wsa885x->sd_n, 0);
}

static int wsa885x_get_reset(struct device *dev, struct wsa885x_priv *wsa885x)
{
	wsa885x->sd_reset = devm_reset_control_get_optional_shared(dev, NULL);
	if (IS_ERR(wsa885x->sd_reset))
		return dev_err_probe(dev, PTR_ERR(wsa885x->sd_reset),
				     "Failed to get reset\n");
	else if (wsa885x->sd_reset)
		return 0;

	wsa885x->sd_n = devm_gpiod_get_optional(dev, "powerdown", GPIOD_OUT_HIGH);
	if (IS_ERR(wsa885x->sd_n))
		return dev_err_probe(dev, PTR_ERR(wsa885x->sd_n),
				     "Shutdown Control GPIO not found\n");

	return 0;
}

static bool wsa885x_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WSA885X_ANA_TOP_PLL_STATUS_0:
	case WSA885X_ANA_TOP_PLL_STATUS_1:
	case WSA885X_DIG_CTRL0_SDCA_COMMIT:
	case WSA885X_SMP_AMP_CTRL_STEREO_PDE23_ACT_PS:
	case WSA885X_SMP_AMP_CTRL_STEREO_CS21_CLOCK_VALID:
	case WSA885X_INTR_STATUS0:
	case WSA885X_INTR_STATUS0 + 1:
	case WSA885X_INTR_STATUS0 + 2:
	case WSA885X_INTR_CLEAR0:
	case WSA885X_INTR_CLEAR0 + 1:
	case WSA885X_INTR_CLEAR0 + 2:
		return true;
	default:
		return false;
	}
}

static bool wsa885x_readable_register(struct device *dev, unsigned int reg)
{
	if (reg == WSA885X_INTR_CLEAR0 ||
	    reg == WSA885X_INTR_CLEAR0 + 1 ||
	    reg == WSA885X_INTR_CLEAR0 + 2)
		return false;
	return reg <= 0x88ff;
}

static bool wsa885x_writeable_register(struct device *dev, unsigned int reg)
{
	if (reg > 0x88ff)
		return false;

	switch (reg) {
	case WSA885X_ANA_TOP_PLL_STATUS_0:
	case WSA885X_ANA_TOP_PLL_STATUS_1:
	case WSA885X_INTR_STATUS0:
	case WSA885X_INTR_STATUS0 + 1:
	case WSA885X_INTR_STATUS0 + 2:
	case WSA885X_SMP_AMP_CTRL_STEREO_PDE23_ACT_PS:
	case WSA885X_SMP_AMP_CTRL_STEREO_CS21_CLOCK_VALID:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config wsa885x_regmap_cfg = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x88FF,
	.ranges = wsa885x_regmap_ranges,
	.num_ranges = ARRAY_SIZE(wsa885x_regmap_ranges),
	.reg_defaults = wsa885x_codec_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wsa885x_codec_reg_defaults),
	.volatile_reg = wsa885x_volatile_register,
	.writeable_reg = wsa885x_writeable_register,
	.readable_reg = wsa885x_readable_register,
	.cache_type = REGCACHE_MAPLE,
	.use_single_read = true,
	.use_single_write = true,
};

static int wsa885x_component_probe(struct snd_soc_component *component)
{
	struct wsa885x_priv *wsa885x =
		snd_soc_component_get_drvdata(component);
	int ret;

	wsa885x->component = component;
	snd_soc_component_init_regmap(component, wsa885x->regmap);

	ret = wsa885x_hw_init(wsa885x);
	if (ret)
		return ret;

	return wsa885x_unmask_interrupts(wsa885x);
}

static int wsa885x_stereo_gain_offset_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct wsa885x_priv *wsa885x = snd_soc_component_get_drvdata(component);
	int val;

	mutex_lock(&wsa885x->state_lock);
	val = wsa885x->stereo_vol_db + 84;
	mutex_unlock(&wsa885x->state_lock);
	if (val < 0 || val > WSA885X_FU21_VOL_STEPS)
		return -ERANGE;

	ucontrol->value.integer.value[0] = val;
	return 0;
}

static int wsa885x_stereo_gain_offset_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct wsa885x_priv *wsa885x = snd_soc_component_get_drvdata(component);
	long val;
	int stereo_vol_db;

	val = ucontrol->value.integer.value[0];

	if (val < 0 || val > WSA885X_FU21_VOL_STEPS) {
		dev_err(component->dev, "%s: Invalid range, Val: %ld\n", __func__, val);
		return -EINVAL;
	}

	stereo_vol_db = (int)val - 84;

	mutex_lock(&wsa885x->state_lock);
	if (wsa885x->stereo_vol_db == stereo_vol_db) {
		mutex_unlock(&wsa885x->state_lock);
		return 0;
	}

	wsa885x_program_stereo_volume(wsa885x, stereo_vol_db, true);
	wsa885x->stereo_vol_db = stereo_vol_db;
	mutex_unlock(&wsa885x->state_lock);

	return 1;
}

static int wsa885x_usage_modes_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct wsa885x_priv *wsa885x = snd_soc_component_get_drvdata(component);

	mutex_lock(&wsa885x->state_lock);
	if (wsa885x->usage_mode > WSA885X_USAGE_MODE_MAX) {
		mutex_unlock(&wsa885x->state_lock);
		return -ERANGE;
	}

	ucontrol->value.integer.value[0] = wsa885x->usage_mode;
	mutex_unlock(&wsa885x->state_lock);

	return 0;
}

static int wsa885x_usage_modes_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct wsa885x_priv *wsa885x = snd_soc_component_get_drvdata(component);
	u32 val = ucontrol->value.integer.value[0];

	if (val > WSA885X_USAGE_MODE_MAX)
		return -EINVAL;

	mutex_lock(&wsa885x->state_lock);
	if (wsa885x->usage_mode == val) {
		mutex_unlock(&wsa885x->state_lock);
		return 0;
	}

	wsa885x->usage_mode = val;
	mutex_unlock(&wsa885x->state_lock);

	return 1;
}

static int wsa885x_rx_slot_mask_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct wsa885x_priv *wsa885x = snd_soc_component_get_drvdata(component);
	u32 mask;

	mutex_lock(&wsa885x->state_lock);
	mask = wsa885x->rx_slot_mask;
	mutex_unlock(&wsa885x->state_lock);
	if (!wsa885x_is_valid_rx_slot_mask(mask))
		return -ERANGE;

	ucontrol->value.integer.value[0] = mask;

	return 0;
}

static int wsa885x_rx_slot_mask_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct wsa885x_priv *wsa885x = snd_soc_component_get_drvdata(component);
	u32 mask = ucontrol->value.integer.value[0];

	if (!wsa885x_is_valid_rx_slot_mask(mask))
		return -EINVAL;

	mutex_lock(&wsa885x->state_lock);
	if (wsa885x->rx_slot_mask == mask) {
		mutex_unlock(&wsa885x->state_lock);
		return 0;
	}

	wsa885x->rx_slot_mask = mask;
	mutex_unlock(&wsa885x->state_lock);

	return 1;
}

static const struct snd_kcontrol_new wsa885x_snd_controls[] = {
	SOC_SINGLE_EXT("Usage Mode", SND_SOC_NOPM, 0, WSA885X_USAGE_MODE_MAX, 0,
		       wsa885x_usage_modes_get,
		       wsa885x_usage_modes_put),

	SOC_SINGLE_EXT_TLV("Speaker Volume", SND_SOC_NOPM,
			   0, WSA885X_FU21_VOL_STEPS, 0,
			   wsa885x_stereo_gain_offset_get,
			   wsa885x_stereo_gain_offset_put,
			   wsa885x_fu21_digital_gain),

	SOC_SINGLE_EXT("Rx Slot Mask", SND_SOC_NOPM, 0, 3, 0,
		       wsa885x_rx_slot_mask_get,
		       wsa885x_rx_slot_mask_put),
};

static const struct snd_soc_component_driver wsa885x_component = {
	.name = "wsa885x",
	.probe = wsa885x_component_probe,
	.controls = wsa885x_snd_controls,
	.num_controls = ARRAY_SIZE(wsa885x_snd_controls),
};

static irqreturn_t wsa885x_handle_irq(int irq_idx, void *data)
{
	struct wsa885x_priv *wsa885x = data;

	if (irq_idx < 0 || irq_idx >= WSA885X_IRQ_MAX)
		return IRQ_NONE;

	switch (irq_idx) {
	case WSA885X_IRQ_INT_SAF2WAR:
	case WSA885X_IRQ_INT_WAR2SAF:
	case WSA885X_IRQ_INT_DISABLE:
	case WSA885X_IRQ_INT_INTR_GPIO1_PIN:
	case WSA885X_IRQ_INT_INTR_GPIO2_PIN:
	case WSA885X_IRQ_INT_PA0_OCP:
	case WSA885X_IRQ_INT_PA1_OCP:
	case WSA885X_IRQ_INT_CLIP0:
	case WSA885X_IRQ_INT_CLIP1:
	case WSA885X_IRQ_INT_CLK_WD:
	case WSA885X_IRQ_INT_BOP:
	case WSA885X_IRQ_INT_UVLO:
	case WSA885X_IRQ_INT_PCM_DATA0_DC:
	case WSA885X_IRQ_INT_PCM_DATA1_DC:
	case WSA885X_IRQ_INT_PLL_UNLOCKED:
	case WSA885X_IRQ_INT_PROT_MODE_CHANGE:
	case WSA885X_IRQ_INT_PB_CLOCK_VALID:
	case WSA885X_IRQ_INT_SENSE_CLOCK_VALID:
		break;
	case WSA885X_IRQ_INT_PCM_DATA0_WD:
	case WSA885X_IRQ_INT_PCM_DATA1_WD:
		if (irq_idx == WSA885X_IRQ_INT_PCM_DATA0_WD)
			wsa885x_toggle_irq_bit(wsa885x, WSA885X_DIG_CTRL0_PCM_DATA_WD0_CTL1,
					       WSA885X_PCM_DATA_WD_CTL1_PCM_DATA_WD_EN_MASK);
		else
			wsa885x_toggle_irq_bit(wsa885x, WSA885X_DIG_CTRL0_PCM_DATA_WD1_CTL1,
					       WSA885X_PCM_DATA_WD_CTL1_PCM_DATA_WD_EN_MASK);
		break;
	case WSA885X_IRQ_INT_PA0_FSM_ERR:
	case WSA885X_IRQ_INT_PA1_FSM_ERR:
	case WSA885X_IRQ_INT_MAIN_FSM_ERR:
		if (irq_idx == WSA885X_IRQ_INT_MAIN_FSM_ERR) {
			wsa885x_pulse_irq_bit(wsa885x, WSA885X_DIG_CTRL0_POWER_FSM_CTL0,
					      WSA885X_POWER_FSM_CTL0_CLEAR_ERROR_MASK);
		} else if (irq_idx == WSA885X_IRQ_INT_PA0_FSM_ERR) {
			wsa885x_pulse_irq_bit(wsa885x, WSA885X_PA0_FSM_CTL0,
					      WSA885X_PA_FSM_CTL0_CLEAR_ERROR_MASK);
		} else if (irq_idx == WSA885X_IRQ_INT_PA1_FSM_ERR) {
			wsa885x_pulse_irq_bit(wsa885x, WSA885X_PA1_FSM_CTL0,
					      WSA885X_PA_FSM_CTL0_CLEAR_ERROR_MASK);
		}
		break;
	default:
		break;
	}

	return IRQ_HANDLED;
}

static irqreturn_t wsa885x_interrupt_handler(int irq, void *data)
{
	static const unsigned int status_reg[WSA885X_NUM_REGS] = {
		WSA885X_INTR_STATUS0,
		WSA885X_INTR_STATUS0 + 1,
		WSA885X_INTR_STATUS0 + 2,
	};
	static const unsigned int clear_reg[WSA885X_NUM_REGS] = {
		WSA885X_INTR_CLEAR0,
		WSA885X_INTR_CLEAR0 + 1,
		WSA885X_INTR_CLEAR0 + 2,
	};
	unsigned int status[WSA885X_NUM_REGS] = { 0 };
	struct wsa885x_priv *wsa885x = data;
	irqreturn_t handled = IRQ_NONE;
	irqreturn_t irq_ret;
	int i, bit, ret, irq_num;

	for (i = 0; i < WSA885X_NUM_REGS; i++) {
		ret = regmap_read(wsa885x->regmap, status_reg[i], &status[i]);
		if (ret) {
			dev_err(wsa885x->dev,
				"Failed to read status_reg[%d] (0x%x): %d\n",
				i, status_reg[i], ret);
			status[i] = 0;
			continue;
		}
	}

	for (i = 0; i < WSA885X_NUM_REGS; i++) {
		for (bit = 0; bit < 8; bit++) {
			if (status[i] & BIT(bit)) {
				irq_num = i * 8 + bit;
				regmap_write(wsa885x->regmap, clear_reg[i], BIT(bit));
				regmap_write(wsa885x->regmap, clear_reg[i], 0);
				if (irq_num >= WSA885X_IRQ_MAX) {
					dev_warn_ratelimited(wsa885x->dev,
							     "Unexpected IRQ bit %d (reg %d)\n",
							     bit, i);
					handled = IRQ_HANDLED;
					continue;
				}
				irq_ret = wsa885x_handle_irq(irq_num, wsa885x);
				if (irq_ret == IRQ_HANDLED)
					handled = IRQ_HANDLED;
			}
		}
	}
	return handled;
}

static int wsa885x_register_irq(struct wsa885x_priv *wsa885x)
{
	if (!wsa885x->client->irq)
		return dev_err_probe(wsa885x->dev, -EINVAL,
				     "IRQ is not configured\n");

	return devm_request_threaded_irq(wsa885x->dev, wsa885x->client->irq, NULL,
					wsa885x_interrupt_handler,
					IRQF_ONESHOT,
					dev_name(wsa885x->dev), wsa885x);
}

static int wsa885x_probe(struct i2c_client *client)
{
	struct wsa885x_priv *wsa885x;
	const struct snd_soc_component_driver *component_driver = &wsa885x_component;
	const char *battery_config;
	unsigned int i;
	int ret;
	struct device *dev = &client->dev;

	wsa885x = devm_kzalloc(dev, sizeof(*wsa885x), GFP_KERNEL);
	if (!wsa885x)
		return -ENOMEM;

	wsa885x->client = client;
	wsa885x->dev = dev;
	wsa885x->stereo_vol_db = -84;
	wsa885x->rx_slot_mask = WSA885X_CHANNEL_STEREO;
	mutex_init(&wsa885x->state_lock);
	wsa885x->regmap = devm_regmap_init_i2c(client, &wsa885x_regmap_cfg);

	if (IS_ERR(wsa885x->regmap))
		return PTR_ERR(wsa885x->regmap);

	ret = device_property_read_string(dev, "qcom,battery-config",
					  &battery_config);
	if (ret) {
		wsa885x->batt_conf = WSA885X_BATT_1S;
	} else if (!strcmp(battery_config, "1s")) {
		wsa885x->batt_conf = WSA885X_BATT_1S;
	} else if (!strcmp(battery_config, "2s")) {
		wsa885x->batt_conf = WSA885X_BATT_2S;
	} else {
		return dev_err_probe(dev, -EINVAL,
				     "Invalid battery config %s (expected 1s or 2s)\n",
				     battery_config);
	}

	for (i = 0; i < ARRAY_SIZE(wsa885x_supply_name); i++) {
		ret = devm_regulator_get_enable(dev, wsa885x_supply_name[i]);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to enable regulator %s\n",
					     wsa885x_supply_name[i]);
	}

	ret = wsa885x_get_reset(dev, wsa885x);
	if (ret)
		return ret;

	wsa885x_reset_deassert(wsa885x);
	usleep_range(5000, 5500);

	ret = devm_add_action_or_reset(dev, wsa885x_reset_assert, wsa885x);
	if (ret)
		return dev_err_probe(dev, ret, "devm_add_action_or_reset failed\n");

	i2c_set_clientdata(client, wsa885x);

	ret = wsa885x_register_irq(wsa885x);
	if (ret)
		return dev_err_probe(dev, ret, "wsa885x irq registration failed\n");

	ret = devm_snd_soc_register_component(dev, component_driver,
					      wsa885x_dai,
					      ARRAY_SIZE(wsa885x_dai));
	if (ret)
		return dev_err_probe(dev, ret, "Codec component registration failed\n");

	return 0;
}

static const struct of_device_id wsa885x_dt_match[] = {
	{
		.compatible = "qcom,wsa8855",
	},
	{}
};
MODULE_DEVICE_TABLE(of, wsa885x_dt_match);

static const struct i2c_device_id wsa885x_id[] = {
	{
		.name = "wsa885x",
		.driver_data = 0,
	},
	{}
};
MODULE_DEVICE_TABLE(i2c, wsa885x_id);

static struct i2c_driver wsa885x_driver = {
	.driver = {
		.name = "wsa885x",
		.of_match_table = wsa885x_dt_match,
	},
	.probe = wsa885x_probe,
	.id_table = wsa885x_id,
};

module_i2c_driver(wsa885x_driver);

MODULE_DESCRIPTION("ASoC WSA885X Stereo Smart PA Codec Driver");
MODULE_LICENSE("GPL");
