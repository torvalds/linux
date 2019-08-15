/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rt711.h -- RT711 ALSA SoC audio driver header
 *
 * Copyright(c) 2019 Realtek Semiconductor Corp.
 */

#ifndef __RT711_H__
#define __RT711_H__

#include <linux/regulator/consumer.h>

extern const struct dev_pm_ops rt711_runtime_pm;

struct  rt711_priv {
	struct regmap *regmap;
	struct regmap *sdw_regmap;
	struct snd_soc_component *component;
	struct sdw_slave *slave;
	enum sdw_slave_status status;
	struct sdw_bus_params params;
	bool hw_init;
	bool first_init;
	struct snd_soc_jack *hs_jack;
	struct delayed_work jack_detect_work;
	struct delayed_work jack_btn_check_work;
	struct work_struct calibration_work;
	struct mutex calibrate_mutex; /* for headset calibration */
	int jack_type;
};

struct sdw_stream_data {
	struct sdw_stream_runtime *sdw_stream;
};

/* NID */
#define RT711_AUDIO_FUNCTION_GROUP			0x01
#define RT711_DAC_OUT2					0x03
#define RT711_ADC_IN1					0x09
#define RT711_ADC_IN2					0x08
#define RT711_DMIC1					0x12
#define RT711_DMIC2					0x13
#define RT711_MIC2					0x19
#define RT711_LINE1					0x1a
#define RT711_LINE2					0x1b
#define RT711_BEEP					0x1d
#define RT711_VENDOR_REG				0x20
#define RT711_HP_OUT					0x21
#define RT711_MIXER_IN1					0x22
#define RT711_MIXER_IN2					0x23
#define RT711_INLINE_CMD				0x55
#define RT711_VENDOR_CALI				0x58
#define RT711_VENDOR_IMS_DRE			0x5b

/* Index (NID:20h) */
#define RT711_DAC_DC_CALI_CTL1				0x00
#define RT711_PARA_VERB_CTL				0x1a
#define RT711_COMBO_JACK_AUTO_CTL1				0x45
#define RT711_COMBO_JACK_AUTO_CTL2				0x46
#define RT711_INLINE_CMD_CTL				0x48
#define RT711_DIGITAL_MISC_CTRL4			0x4a
#define RT711_VREFOUT_CTL				0x6b
#define RT711_FSM_CTL				0x6f
#define RT711_IRQ_FLAG_TABLE1				0x80
#define RT711_IRQ_FLAG_TABLE2				0x81
#define RT711_IRQ_FLAG_TABLE3				0x82
#define RT711_TX_RX_MUX_CTL				0x91

/* Index (NID:5bh) */
#define RT711_IMS_DIGITAL_CTL1				0x00
#define RT711_HP_IMS_RESULT_L				0x20
#define RT711_HP_IMS_RESULT_R				0x21

/* Verb */
#define RT711_VERB_SET_CONNECT_SEL			0x3100
#define RT711_VERB_SET_EAPD_BTLENABLE			0x3c00
#define RT711_VERB_GET_CONNECT_SEL			0xb100
#define RT711_VERB_SET_POWER_STATE			0x3500
#define RT711_VERB_SET_CHANNEL_STREAMID			0x3600
#define RT711_VERB_SET_PIN_WIDGET_CONTROL		0x3700
#define RT711_VERB_SET_UNSOLICITED_ENABLE		0x3800
#define RT711_SET_AMP_GAIN_MUTE_H			0x7300
#define RT711_SET_AMP_GAIN_MUTE_L			0x8380
#define RT711_VERB_GET_POWER_STATE			0xb500
#define RT711_VERB_GET_CHANNEL_STREAMID			0xb600
#define RT711_VERB_GET_PIN_SENSE			0xb900
#define RT711_FUNC_RESET			0xff01

#define RT711_READ_HDA_3				0x2012
#define RT711_READ_HDA_2				0x2013
#define RT711_READ_HDA_1				0x2014
#define RT711_READ_HDA_0				0x2015
#define RT711_PRIV_INDEX_W_H				0x7500
#define RT711_PRIV_INDEX_W_L				0x8580
#define RT711_PRIV_DATA_W_H				0x7400
#define RT711_PRIV_DATA_W_L				0x8480
#define RT711_PRIV_INDEX_R_H				0x9d00
#define RT711_PRIV_INDEX_R_L				0xad80
#define RT711_PRIV_DATA_R_H				0x9c00
#define RT711_PRIV_DATA_R_L				0xac80
#define RT711_DAC_FORMAT_H				0x7203
#define RT711_DAC_FORMAT_L				0x8283
#define RT711_ADC1_FORMAT_H				0x7209
#define RT711_ADC1_FORMAT_L				0x8289
#define RT711_ADC2_FORMAT_H				0x7208
#define RT711_ADC2_FORMAT_L				0x8288

#define RT711_SET_AUDIO_POWER_STATE\
	(RT711_VERB_SET_POWER_STATE | RT711_AUDIO_FUNCTION_GROUP)
#define RT711_GET_AUDIO_POWER_STATE\
		(RT711_VERB_GET_POWER_STATE | RT711_AUDIO_FUNCTION_GROUP)
#define RT711_SET_PIN_DMIC1\
	(RT711_VERB_SET_PIN_WIDGET_CONTROL | RT711_DMIC1)
#define RT711_SET_PIN_DMIC2\
	(RT711_VERB_SET_PIN_WIDGET_CONTROL | RT711_DMIC2)
#define RT711_SET_PIN_HP\
	(RT711_VERB_SET_PIN_WIDGET_CONTROL | RT711_HP_OUT)
#define RT711_SET_PIN_MIC2\
	(RT711_VERB_SET_PIN_WIDGET_CONTROL | RT711_MIC2)
#define RT711_SET_PIN_LINE1\
	(RT711_VERB_SET_PIN_WIDGET_CONTROL | RT711_LINE1)
#define RT711_SET_PIN_LINE2\
	(RT711_VERB_SET_PIN_WIDGET_CONTROL | RT711_LINE2)
#define RT711_SET_MIC2_UNSOLICITED_ENABLE\
	(RT711_VERB_SET_UNSOLICITED_ENABLE | RT711_MIC2)
#define RT711_SET_HP_UNSOLICITED_ENABLE\
	(RT711_VERB_SET_UNSOLICITED_ENABLE | RT711_HP_OUT)
#define RT711_SET_INLINE_UNSOLICITED_ENABLE\
	(RT711_VERB_SET_UNSOLICITED_ENABLE | RT711_INLINE_CMD)
#define RT711_SET_STREAMID_DAC2\
	(RT711_VERB_SET_CHANNEL_STREAMID | RT711_DAC_OUT2)
#define RT711_SET_STREAMID_ADC1\
	(RT711_VERB_SET_CHANNEL_STREAMID | RT711_ADC_IN1)
#define RT711_SET_STREAMID_ADC2\
	(RT711_VERB_SET_CHANNEL_STREAMID | RT711_ADC_IN2)
#define RT711_GET_STREAMID_DAC2\
	(RT711_VERB_GET_CHANNEL_STREAMID | RT711_DAC_OUT2)
#define RT711_GET_STREAMID_ADC1\
	(RT711_VERB_GET_CHANNEL_STREAMID | RT711_ADC_IN1)
#define RT711_GET_STREAMID_ADC2\
	(RT711_VERB_GET_CHANNEL_STREAMID | RT711_ADC_IN2)
#define RT711_SET_GAIN_DAC2_L\
	(RT711_SET_AMP_GAIN_MUTE_L | RT711_DAC_OUT2)
#define RT711_SET_GAIN_DAC2_H\
	(RT711_SET_AMP_GAIN_MUTE_H | RT711_DAC_OUT2)
#define RT711_SET_GAIN_ADC1_L\
	(RT711_SET_AMP_GAIN_MUTE_L | RT711_ADC_IN1)
#define RT711_SET_GAIN_ADC1_H\
	(RT711_SET_AMP_GAIN_MUTE_H | RT711_ADC_IN1)
#define RT711_SET_GAIN_ADC2_L\
	(RT711_SET_AMP_GAIN_MUTE_L | RT711_ADC_IN2)
#define RT711_SET_GAIN_ADC2_H\
	(RT711_SET_AMP_GAIN_MUTE_H | RT711_ADC_IN2)
#define RT711_SET_GAIN_AMIC_L\
	(RT711_SET_AMP_GAIN_MUTE_L | RT711_MIC2)
#define RT711_SET_GAIN_AMIC_H\
	(RT711_SET_AMP_GAIN_MUTE_H | RT711_MIC2)
#define RT711_SET_GAIN_DMIC1_L\
	(RT711_SET_AMP_GAIN_MUTE_L | RT711_DMIC1)
#define RT711_SET_GAIN_DMIC1_H\
	(RT711_SET_AMP_GAIN_MUTE_H | RT711_DMIC1)
#define RT711_SET_GAIN_DMIC2_L\
	(RT711_SET_AMP_GAIN_MUTE_L | RT711_DMIC2)
#define RT711_SET_GAIN_DMIC2_H\
	(RT711_SET_AMP_GAIN_MUTE_H | RT711_DMIC2)
#define RT711_SET_GAIN_HP_L\
	(RT711_SET_AMP_GAIN_MUTE_L | RT711_HP_OUT)
#define RT711_SET_GAIN_HP_H\
	(RT711_SET_AMP_GAIN_MUTE_H | RT711_HP_OUT)

/* DAC DC offset calibration control-1 (0x00)(NID:20h) */
#define RT711_DAC_DC_CALI_TRIGGER (0x1 << 15)

/* Parameter & Verb control (0x1a)(NID:20h) */
#define RT711_HIDDEN_REG_SW_RESET (0x1 << 14)

/* combo jack auto switch control 2 (0x46)(NID:20h) */
#define RT711_COMBOJACK_AUTO_DET_STATUS			(0x1 << 11)
#define RT711_COMBOJACK_AUTO_DET_TRS			(0x1 << 10)
#define RT711_COMBOJACK_AUTO_DET_CTIA			(0x1 << 9)
#define RT711_COMBOJACK_AUTO_DET_OMTP			(0x1 << 8)

/* FSM control (0x6f)(NID:20h) */
#define RT711_CALI_CTL			(0x0 << 0)
#define RT711_COMBOJACK_CTL			(0x1 << 0)
#define RT711_IMS_CTL			(0x2 << 0)
#define RT711_DEPOP_CTL			(0x3 << 0)

/* Impedance Sense Digital Control 1 (0x00)(NID:5bh) */
#define RT711_TRIGGER_IMS			(0x1 << 15)
#define RT711_IMS_EN			(0x1 << 6)

#define RT711_EAPD_HIGH					0x2
#define RT711_EAPD_LOW					0x0
#define RT711_MUTE_SFT					7
/* set input/output mapping to payload[14][15] separately */
#define RT711_DIR_IN_SFT				6
#define RT711_DIR_OUT_SFT				7

enum {
	RT711_AIF1,
	RT711_AIF2,
	RT711_AIFS,
};

int rt711_io_init(struct device *dev, struct sdw_slave *slave);
int rt711_init(struct device *dev, struct regmap *sdw_regmap,
	       struct regmap *regmap, struct sdw_slave *slave);

int rt711_jack_detect(struct rt711_priv *rt711, bool *hp, bool *mic);
int rt711_clock_config(struct device *dev);
#endif /* __RT711_H__ */
