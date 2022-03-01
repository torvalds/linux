// SPDX-License-Identifier: GPL-2.0-only
/*
 * patch_cs8409-tables.c  --  HD audio interface patch for Cirrus Logic CS8409 HDA bridge chip
 *
 * Copyright (C) 2021 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 *
 * Author: Lucas Tanure <tanureal@opensource.cirrus.com>
 */

#include "patch_cs8409.h"

/******************************************************************************
 *                          CS42L42 Specific Data
 *
 ******************************************************************************/

static const DECLARE_TLV_DB_SCALE(cs42l42_dac_db_scale, CS42L42_HP_VOL_REAL_MIN * 100, 100, 1);

static const DECLARE_TLV_DB_SCALE(cs42l42_adc_db_scale, CS42L42_AMIC_VOL_REAL_MIN * 100, 100, 1);

const struct snd_kcontrol_new cs42l42_dac_volume_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.index = 0,
	.subdevice = (HDA_SUBDEV_AMP_FLAG | HDA_SUBDEV_NID_FLAG),
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.info = cs42l42_volume_info,
	.get = cs42l42_volume_get,
	.put = cs42l42_volume_put,
	.tlv = { .p = cs42l42_dac_db_scale },
	.private_value = HDA_COMPOSE_AMP_VAL_OFS(CS8409_PIN_ASP1_TRANSMITTER_A, 3, CS8409_CODEC0,
			 HDA_OUTPUT, CS42L42_VOL_DAC) | HDA_AMP_VAL_MIN_MUTE
};

const struct snd_kcontrol_new cs42l42_adc_volume_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.index = 0,
	.subdevice = (HDA_SUBDEV_AMP_FLAG | HDA_SUBDEV_NID_FLAG),
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.info = cs42l42_volume_info,
	.get = cs42l42_volume_get,
	.put = cs42l42_volume_put,
	.tlv = { .p = cs42l42_adc_db_scale },
	.private_value = HDA_COMPOSE_AMP_VAL_OFS(CS8409_PIN_ASP1_RECEIVER_A, 1, CS8409_CODEC0,
			 HDA_INPUT, CS42L42_VOL_ADC) | HDA_AMP_VAL_MIN_MUTE
};

const struct hda_pcm_stream cs42l42_48k_pcm_analog_playback = {
	.rates = SNDRV_PCM_RATE_48000, /* fixed rate */
};

const struct hda_pcm_stream cs42l42_48k_pcm_analog_capture = {
	.rates = SNDRV_PCM_RATE_48000, /* fixed rate */
};

/******************************************************************************
 *                   BULLSEYE / WARLOCK / CYBORG Specific Arrays
 *                               CS8409/CS42L42
 ******************************************************************************/

const struct hda_verb cs8409_cs42l42_init_verbs[] = {
	{ CS8409_PIN_AFG, AC_VERB_SET_GPIO_WAKE_MASK, 0x0018 },		/* WAKE from GPIO 3,4 */
	{ CS8409_PIN_VENDOR_WIDGET, AC_VERB_SET_PROC_STATE, 0x0001 },	/* Enable VPW processing */
	{ CS8409_PIN_VENDOR_WIDGET, AC_VERB_SET_COEF_INDEX, 0x0002 },	/* Configure GPIO 6,7 */
	{ CS8409_PIN_VENDOR_WIDGET, AC_VERB_SET_PROC_COEF,  0x0080 },	/* I2C mode */
	{ CS8409_PIN_VENDOR_WIDGET, AC_VERB_SET_COEF_INDEX, 0x005b },	/* Set I2C bus speed */
	{ CS8409_PIN_VENDOR_WIDGET, AC_VERB_SET_PROC_COEF,  0x0200 },	/* 100kHz I2C_STO = 2 */
	{} /* terminator */
};

const struct hda_pintbl cs8409_cs42l42_pincfgs[] = {
	{ CS8409_PIN_ASP1_TRANSMITTER_A, 0x042120f0 },	/* ASP-1-TX */
	{ CS8409_PIN_ASP1_RECEIVER_A, 0x04a12050 },	/* ASP-1-RX */
	{ CS8409_PIN_ASP2_TRANSMITTER_A, 0x901000f0 },	/* ASP-2-TX */
	{ CS8409_PIN_DMIC1_IN, 0x90a00090 },		/* DMIC-1 */
	{} /* terminator */
};

/* Vendor specific HW configuration for CS42L42 */
static const struct cs8409_i2c_param cs42l42_init_reg_seq[] = {
	{ 0x1010, 0xB0 },
	{ 0x1D01, 0x00 },
	{ 0x1D02, 0x06 },
	{ 0x1D03, 0x9F },
	{ 0x1107, 0x01 },
	{ 0x1009, 0x02 },
	{ 0x1007, 0x03 },
	{ 0x1201, 0x00 },
	{ 0x1208, 0x13 },
	{ 0x1205, 0xFF },
	{ 0x1206, 0x00 },
	{ 0x1207, 0x20 },
	{ 0x1202, 0x0D },
	{ 0x2A02, 0x02 },
	{ 0x2A03, 0x00 },
	{ 0x2A04, 0x00 },
	{ 0x2A05, 0x02 },
	{ 0x2A06, 0x00 },
	{ 0x2A07, 0x20 },
	{ 0x2A08, 0x02 },
	{ 0x2A09, 0x00 },
	{ 0x2A0A, 0x80 },
	{ 0x2A0B, 0x02 },
	{ 0x2A0C, 0x00 },
	{ 0x2A0D, 0xA0 },
	{ 0x2A01, 0x0C },
	{ 0x2902, 0x01 },
	{ 0x2903, 0x02 },
	{ 0x2904, 0x00 },
	{ 0x2905, 0x00 },
	{ 0x2901, 0x01 },
	{ 0x1101, 0x0A },
	{ 0x1102, 0x84 },
	{ 0x2301, 0x3F },
	{ 0x2303, 0x3F },
	{ 0x2302, 0x3f },
	{ 0x2001, 0x03 },
	{ 0x1B75, 0xB6 },
	{ 0x1B73, 0xC2 },
	{ 0x1129, 0x01 },
	{ 0x1121, 0xF3 },
	{ 0x1103, 0x20 },
	{ 0x1105, 0x00 },
	{ 0x1112, 0x00 },
	{ 0x1113, 0x80 },
	{ 0x1C03, 0xC0 },
	{ 0x1101, 0x02 },
	{ 0x1316, 0xff },
	{ 0x1317, 0xff },
	{ 0x1318, 0xff },
	{ 0x1319, 0xff },
	{ 0x131a, 0xff },
	{ 0x131b, 0xff },
	{ 0x131c, 0xff },
	{ 0x131e, 0xff },
	{ 0x131f, 0xff },
	{ 0x1320, 0xff },
	{ 0x1b79, 0xff },
	{ 0x1b7a, 0xff },
};

/* Vendor specific hw configuration for CS8409 */
const struct cs8409_cir_param cs8409_cs42l42_hw_cfg[] = {
	/* +PLL1/2_EN, +I2C_EN */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_DEV_CFG1, 0xb008 },
	/* ASP1/2_EN=0, ASP1_STP=1 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_DEV_CFG2, 0x0002 },
	/* ASP1/2_BUS_IDLE=10, +GPIO_I2C */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_DEV_CFG3, 0x0a80 },
	/* ASP1.A: TX.LAP=0, TX.LSZ=24 bits, TX.LCS=0 */
	{ CS8409_PIN_VENDOR_WIDGET, ASP1_A_TX_CTRL1, 0x0800 },
	/* ASP1.A: TX.RAP=0, TX.RSZ=24 bits, TX.RCS=32 */
	{ CS8409_PIN_VENDOR_WIDGET, ASP1_A_TX_CTRL2, 0x0820 },
	/* ASP2.A: TX.LAP=0, TX.LSZ=24 bits, TX.LCS=0 */
	{ CS8409_PIN_VENDOR_WIDGET, ASP2_A_TX_CTRL1, 0x0800 },
	/* ASP2.A: TX.RAP=1, TX.RSZ=24 bits, TX.RCS=0 */
	{ CS8409_PIN_VENDOR_WIDGET, ASP2_A_TX_CTRL2, 0x2800 },
	/* ASP1.A: RX.LAP=0, RX.LSZ=24 bits, RX.LCS=0 */
	{ CS8409_PIN_VENDOR_WIDGET, ASP1_A_RX_CTRL1, 0x0800 },
	/* ASP1.A: RX.RAP=0, RX.RSZ=24 bits, RX.RCS=0 */
	{ CS8409_PIN_VENDOR_WIDGET, ASP1_A_RX_CTRL2, 0x0800 },
	/* ASP1: LCHI = 00h */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_ASP1_CLK_CTRL1, 0x8000 },
	/* ASP1: MC/SC_SRCSEL=PLL1, LCPR=FFh */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_ASP1_CLK_CTRL2, 0x28ff },
	/* ASP1: MCEN=0, FSD=011, SCPOL_IN/OUT=0, SCDIV=1:4 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_ASP1_CLK_CTRL3, 0x0062 },
	/* ASP2: LCHI=1Fh */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_ASP2_CLK_CTRL1, 0x801f },
	/* ASP2: MC/SC_SRCSEL=PLL1, LCPR=3Fh */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_ASP2_CLK_CTRL2, 0x283f },
	/* ASP2: 5050=1, MCEN=0, FSD=010, SCPOL_IN/OUT=1, SCDIV=1:16 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_ASP2_CLK_CTRL3, 0x805c },
	/* DMIC1_MO=10b, DMIC1/2_SR=1 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_DMIC_CFG, 0x0023 },
	/* ASP1/2_BEEP=0 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_BEEP_CFG, 0x0000 },
	/* ASP1/2_EN=1, ASP1_STP=1 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_DEV_CFG2, 0x0062 },
	/* -PLL2_EN */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_DEV_CFG1, 0x9008 },
	/* TX2.A: pre-scale att.=0 dB */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PRE_SCALE_ATTN2, 0x0000 },
	/* ASP1/2_xxx_EN=1, ASP1/2_MCLK_EN=0, DMIC1_SCL_EN=1 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PAD_CFG_SLW_RATE_CTRL, 0xfc03 },
	/* test mode on */
	{ CS8409_PIN_VENDOR_WIDGET, 0xc0, 0x9999 },
	/* GPIO hysteresis = 30 us */
	{ CS8409_PIN_VENDOR_WIDGET, 0xc5, 0x0000 },
	/* test mode off */
	{ CS8409_PIN_VENDOR_WIDGET, 0xc0, 0x0000 },
	{} /* Terminator */
};

const struct cs8409_cir_param cs8409_cs42l42_bullseye_atn[] = {
	/* EQ_SEL=1, EQ1/2_EN=0 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_CTRL1, 0x4000 },
	/* +EQ_ACC */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W2, 0x4000 },
	/* +EQ2_EN */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_CTRL1, 0x4010 },
	/* EQ_DATA_HI=0x0647 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W1, 0x0647 },
	/* +EQ_WRT, +EQ_ACC, EQ_ADR=0, EQ_DATA_LO=0x67 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W2, 0xc0c7 },
	/* EQ_DATA_HI=0x0647 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W1, 0x0647 },
	/* +EQ_WRT, +EQ_ACC, EQ_ADR=1, EQ_DATA_LO=0x67 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W2, 0xc1c7 },
	/* EQ_DATA_HI=0xf370 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W1, 0xf370 },
	/* +EQ_WRT, +EQ_ACC, EQ_ADR=2, EQ_DATA_LO=0x71 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W2, 0xc271 },
	/* EQ_DATA_HI=0x1ef8 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W1, 0x1ef8 },
	/* +EQ_WRT, +EQ_ACC, EQ_ADR=3, EQ_DATA_LO=0x48 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W2, 0xc348 },
	/* EQ_DATA_HI=0xc110 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W1, 0xc110 },
	/* +EQ_WRT, +EQ_ACC, EQ_ADR=4, EQ_DATA_LO=0x5a */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W2, 0xc45a },
	/* EQ_DATA_HI=0x1f29 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W1, 0x1f29 },
	/* +EQ_WRT, +EQ_ACC, EQ_ADR=5, EQ_DATA_LO=0x74 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W2, 0xc574 },
	/* EQ_DATA_HI=0x1d7a */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W1, 0x1d7a },
	/* +EQ_WRT, +EQ_ACC, EQ_ADR=6, EQ_DATA_LO=0x53 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W2, 0xc653 },
	/* EQ_DATA_HI=0xc38c */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W1, 0xc38c },
	/* +EQ_WRT, +EQ_ACC, EQ_ADR=7, EQ_DATA_LO=0x14 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W2, 0xc714 },
	/* EQ_DATA_HI=0x1ca3 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W1, 0x1ca3 },
	/* +EQ_WRT, +EQ_ACC, EQ_ADR=8, EQ_DATA_LO=0xc7 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W2, 0xc8c7 },
	/* EQ_DATA_HI=0xc38c */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W1, 0xc38c },
	/* +EQ_WRT, +EQ_ACC, EQ_ADR=9, EQ_DATA_LO=0x14 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W2, 0xc914 },
	/* -EQ_ACC, -EQ_WRT */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PFE_COEF_W2, 0x0000 },
	{} /* Terminator */
};

struct sub_codec cs8409_cs42l42_codec = {
	.addr = CS42L42_I2C_ADDR,
	.reset_gpio = CS8409_CS42L42_RESET,
	.irq_mask = CS8409_CS42L42_INT,
	.init_seq = cs42l42_init_reg_seq,
	.init_seq_num = ARRAY_SIZE(cs42l42_init_reg_seq),
	.hp_jack_in = 0,
	.mic_jack_in = 0,
	.force_status_change = 1,
	.paged = 1,
	.suspended = 1,
	.no_type_dect = 0,
};

/******************************************************************************
 *                          Dolphin Specific Arrays
 *                            CS8409/ 2 X CS42L42
 ******************************************************************************/

const struct hda_verb dolphin_init_verbs[] = {
	{ 0x01, AC_VERB_SET_GPIO_WAKE_MASK, DOLPHIN_WAKE }, /* WAKE from GPIO 0,4 */
	{ CS8409_PIN_VENDOR_WIDGET, AC_VERB_SET_PROC_STATE, 0x0001 }, /* Enable VPW processing  */
	{ CS8409_PIN_VENDOR_WIDGET, AC_VERB_SET_COEF_INDEX, 0x0002 }, /* Configure GPIO 6,7 */
	{ CS8409_PIN_VENDOR_WIDGET, AC_VERB_SET_PROC_COEF,  0x0080 }, /* I2C mode */
	{ CS8409_PIN_VENDOR_WIDGET, AC_VERB_SET_COEF_INDEX, 0x005b }, /* Set I2C bus speed */
	{ CS8409_PIN_VENDOR_WIDGET, AC_VERB_SET_PROC_COEF,  0x0200 }, /* 100kHz I2C_STO = 2 */
	{} /* terminator */
};

const struct hda_pintbl dolphin_pincfgs[] = {
	{ 0x24, 0x022210f0 }, /* ASP-1-TX-A */
	{ 0x25, 0x010240f0 }, /* ASP-1-TX-B */
	{ 0x34, 0x02a21050 }, /* ASP-1-RX */
	{} /* terminator */
};

/* Vendor specific HW configuration for CS42L42 */
static const struct cs8409_i2c_param dolphin_c0_init_reg_seq[] = {
	{ 0x1010, 0xB0 },
	{ 0x1D01, 0x00 },
	{ 0x1D02, 0x06 },
	{ 0x1D03, 0x9F },
	{ 0x1107, 0x01 },
	{ 0x1009, 0x02 },
	{ 0x1007, 0x03 },
	{ 0x1201, 0x00 },
	{ 0x1208, 0x13 },
	{ 0x1205, 0xFF },
	{ 0x1206, 0x00 },
	{ 0x1207, 0x20 },
	{ 0x1202, 0x0D },
	{ 0x2A02, 0x02 },
	{ 0x2A03, 0x00 },
	{ 0x2A04, 0x00 },
	{ 0x2A05, 0x02 },
	{ 0x2A06, 0x00 },
	{ 0x2A07, 0x20 },
	{ 0x2A01, 0x0C },
	{ 0x2902, 0x01 },
	{ 0x2903, 0x02 },
	{ 0x2904, 0x00 },
	{ 0x2905, 0x00 },
	{ 0x2901, 0x01 },
	{ 0x1101, 0x0A },
	{ 0x1102, 0x84 },
	{ 0x2001, 0x03 },
	{ 0x2301, 0x3F },
	{ 0x2303, 0x3F },
	{ 0x2302, 0x3f },
	{ 0x1B75, 0xB6 },
	{ 0x1B73, 0xC2 },
	{ 0x1129, 0x01 },
	{ 0x1121, 0xF3 },
	{ 0x1103, 0x20 },
	{ 0x1105, 0x00 },
	{ 0x1112, 0x00 },
	{ 0x1113, 0x80 },
	{ 0x1C03, 0xC0 },
	{ 0x1101, 0x02 },
	{ 0x1316, 0xff },
	{ 0x1317, 0xff },
	{ 0x1318, 0xff },
	{ 0x1319, 0xff },
	{ 0x131a, 0xff },
	{ 0x131b, 0xff },
	{ 0x131c, 0xff },
	{ 0x131e, 0xff },
	{ 0x131f, 0xff },
	{ 0x1320, 0xff },
	{ 0x1b79, 0xff },
	{ 0x1b7a, 0xff }
};

static const struct cs8409_i2c_param dolphin_c1_init_reg_seq[] = {
	{ 0x1010, 0xB0 },
	{ 0x1D01, 0x00 },
	{ 0x1D02, 0x06 },
	{ 0x1D03, 0x9F },
	{ 0x1107, 0x01 },
	{ 0x1009, 0x02 },
	{ 0x1007, 0x03 },
	{ 0x1201, 0x00 },
	{ 0x1208, 0x13 },
	{ 0x1205, 0xFF },
	{ 0x1206, 0x00 },
	{ 0x1207, 0x20 },
	{ 0x1202, 0x0D },
	{ 0x2A02, 0x02 },
	{ 0x2A03, 0x00 },
	{ 0x2A04, 0x80 },
	{ 0x2A05, 0x02 },
	{ 0x2A06, 0x00 },
	{ 0x2A07, 0xA0 },
	{ 0x2A01, 0x0C },
	{ 0x2902, 0x00 },
	{ 0x2903, 0x02 },
	{ 0x2904, 0x00 },
	{ 0x2905, 0x00 },
	{ 0x2901, 0x00 },
	{ 0x1101, 0x0E },
	{ 0x1102, 0x84 },
	{ 0x2001, 0x01 },
	{ 0x2301, 0x3F },
	{ 0x2303, 0x3F },
	{ 0x2302, 0x3f },
	{ 0x1B75, 0xB6 },
	{ 0x1B73, 0xC2 },
	{ 0x1129, 0x01 },
	{ 0x1121, 0xF3 },
	{ 0x1103, 0x20 },
	{ 0x1105, 0x00 },
	{ 0x1112, 0x00 },
	{ 0x1113, 0x80 },
	{ 0x1C03, 0xC0 },
	{ 0x1101, 0x06 },
	{ 0x1316, 0xff },
	{ 0x1317, 0xff },
	{ 0x1318, 0xff },
	{ 0x1319, 0xff },
	{ 0x131a, 0xff },
	{ 0x131b, 0xff },
	{ 0x131c, 0xff },
	{ 0x131e, 0xff },
	{ 0x131f, 0xff },
	{ 0x1320, 0xff },
	{ 0x1b79, 0xff },
	{ 0x1b7a, 0xff }
};

/* Vendor specific hw configuration for CS8409 */
const struct cs8409_cir_param dolphin_hw_cfg[] = {
	/* +PLL1/2_EN, +I2C_EN */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_DEV_CFG1, 0xb008 },
	/* ASP1_EN=0, ASP1_STP=1 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_DEV_CFG2, 0x0002 },
	/* ASP1/2_BUS_IDLE=10, +GPIO_I2C */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_DEV_CFG3, 0x0a80 },
	/* ASP1.A: TX.LAP=0, TX.LSZ=24 bits, TX.LCS=0 */
	{ CS8409_PIN_VENDOR_WIDGET, ASP1_A_TX_CTRL1, 0x0800 },
	/* ASP1.A: TX.RAP=0, TX.RSZ=24 bits, TX.RCS=32 */
	{ CS8409_PIN_VENDOR_WIDGET, ASP1_A_TX_CTRL2, 0x0820 },
	/* ASP1.B: TX.LAP=0, TX.LSZ=24 bits, TX.LCS=128 */
	{ CS8409_PIN_VENDOR_WIDGET, ASP1_B_TX_CTRL1, 0x0880 },
	/* ASP1.B: TX.RAP=0, TX.RSZ=24 bits, TX.RCS=160 */
	{ CS8409_PIN_VENDOR_WIDGET, ASP1_B_TX_CTRL2, 0x08a0 },
	/* ASP1.A: RX.LAP=0, RX.LSZ=24 bits, RX.LCS=0 */
	{ CS8409_PIN_VENDOR_WIDGET, ASP1_A_RX_CTRL1, 0x0800 },
	/* ASP1.A: RX.RAP=0, RX.RSZ=24 bits, RX.RCS=0 */
	{ CS8409_PIN_VENDOR_WIDGET, ASP1_A_RX_CTRL2, 0x0800 },
	/* ASP1: LCHI = 00h */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_ASP1_CLK_CTRL1, 0x8000 },
	/* ASP1: MC/SC_SRCSEL=PLL1, LCPR=FFh */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_ASP1_CLK_CTRL2, 0x28ff },
	/* ASP1: MCEN=0, FSD=011, SCPOL_IN/OUT=0, SCDIV=1:4 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_ASP1_CLK_CTRL3, 0x0062 },
	/* ASP1/2_BEEP=0 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_BEEP_CFG, 0x0000 },
	/* ASP1_EN=1, ASP1_STP=1 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_DEV_CFG2, 0x0022 },
	/* -PLL2_EN */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_DEV_CFG1, 0x9008 },
	/* ASP1_xxx_EN=1, ASP1_MCLK_EN=0 */
	{ CS8409_PIN_VENDOR_WIDGET, CS8409_PAD_CFG_SLW_RATE_CTRL, 0x5400 },
	/* test mode on */
	{ CS8409_PIN_VENDOR_WIDGET, 0xc0, 0x9999 },
	/* GPIO hysteresis = 30 us */
	{ CS8409_PIN_VENDOR_WIDGET, 0xc5, 0x0000 },
	/* test mode off */
	{ CS8409_PIN_VENDOR_WIDGET, 0xc0, 0x0000 },
	{} /* Terminator */
};

struct sub_codec dolphin_cs42l42_0 = {
	.addr = DOLPHIN_C0_I2C_ADDR,
	.reset_gpio = DOLPHIN_C0_RESET,
	.irq_mask = DOLPHIN_C0_INT,
	.init_seq = dolphin_c0_init_reg_seq,
	.init_seq_num = ARRAY_SIZE(dolphin_c0_init_reg_seq),
	.hp_jack_in = 0,
	.mic_jack_in = 0,
	.force_status_change = 1,
	.paged = 1,
	.suspended = 1,
	.no_type_dect = 0,
};

struct sub_codec dolphin_cs42l42_1 = {
	.addr = DOLPHIN_C1_I2C_ADDR,
	.reset_gpio = DOLPHIN_C1_RESET,
	.irq_mask = DOLPHIN_C1_INT,
	.init_seq = dolphin_c1_init_reg_seq,
	.init_seq_num = ARRAY_SIZE(dolphin_c1_init_reg_seq),
	.hp_jack_in = 0,
	.mic_jack_in = 0,
	.force_status_change = 1,
	.paged = 1,
	.suspended = 1,
	.no_type_dect = 1,
};

/******************************************************************************
 *                         CS8409 Patch Driver Structs
 *                    Arrays Used for all projects using CS8409
 ******************************************************************************/

const struct snd_pci_quirk cs8409_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1028, 0x0A11, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0A12, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0A23, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0A24, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0A25, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0A29, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0A2A, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0A2B, "Bullseye", CS8409_BULLSEYE),
	SND_PCI_QUIRK(0x1028, 0x0AB0, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AB2, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AB1, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AB3, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AB4, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AB5, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AD9, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0ADA, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0ADB, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0ADC, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AF4, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0AF5, "Warlock", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0BB5, "Warlock N3 15 TGL-U Nuvoton EC", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0BB6, "Warlock V3 15 TGL-U Nuvoton EC", CS8409_WARLOCK),
	SND_PCI_QUIRK(0x1028, 0x0A77, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0A78, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0A79, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0A7A, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0A7D, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0A7E, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0A7F, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0A80, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0ADF, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AE0, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AE1, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AE2, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AE9, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AEA, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AEB, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AEC, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AED, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AEE, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AEF, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AF0, "Cyborg", CS8409_CYBORG),
	SND_PCI_QUIRK(0x1028, 0x0AD0, "Dolphin", CS8409_DOLPHIN),
	SND_PCI_QUIRK(0x1028, 0x0AD1, "Dolphin", CS8409_DOLPHIN),
	SND_PCI_QUIRK(0x1028, 0x0AD2, "Dolphin", CS8409_DOLPHIN),
	SND_PCI_QUIRK(0x1028, 0x0AD3, "Dolphin", CS8409_DOLPHIN),
	SND_PCI_QUIRK(0x1028, 0x0ACF, "Dolphin", CS8409_DOLPHIN),
	{} /* terminator */
};

/* Dell Inspiron models with cs8409/cs42l42 */
const struct hda_model_fixup cs8409_models[] = {
	{ .id = CS8409_BULLSEYE, .name = "bullseye" },
	{ .id = CS8409_WARLOCK, .name = "warlock" },
	{ .id = CS8409_CYBORG, .name = "cyborg" },
	{ .id = CS8409_DOLPHIN, .name = "dolphin" },
	{}
};

const struct hda_fixup cs8409_fixups[] = {
	[CS8409_BULLSEYE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = cs8409_cs42l42_pincfgs,
		.chained = true,
		.chain_id = CS8409_FIXUPS,
	},
	[CS8409_WARLOCK] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = cs8409_cs42l42_pincfgs,
		.chained = true,
		.chain_id = CS8409_FIXUPS,
	},
	[CS8409_CYBORG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = cs8409_cs42l42_pincfgs,
		.chained = true,
		.chain_id = CS8409_FIXUPS,
	},
	[CS8409_FIXUPS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs8409_cs42l42_fixups,
	},
	[CS8409_DOLPHIN] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dolphin_pincfgs,
		.chained = true,
		.chain_id = CS8409_DOLPHIN_FIXUPS,
	},
	[CS8409_DOLPHIN_FIXUPS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = dolphin_fixups,
	},
};
