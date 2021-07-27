/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rt700.h -- RT700 ALSA SoC audio driver header
 *
 * Copyright(c) 2019 Realtek Semiconductor Corp.
 */

#ifndef __RT700_H__
#define __RT700_H__

extern const struct dev_pm_ops rt700_runtime_pm;

struct  rt700_priv {
	struct snd_soc_component *component;
	struct regmap *regmap;
	struct regmap *sdw_regmap;
	struct sdw_slave *slave;
	enum sdw_slave_status status;
	struct sdw_bus_params params;
	bool hw_init;
	bool first_hw_init;
	struct snd_soc_jack *hs_jack;
	struct delayed_work jack_detect_work;
	struct delayed_work jack_btn_check_work;
	int jack_type;
	struct mutex disable_irq_lock; /* imp-def irq lock protection */
	bool disable_irq;
};

struct sdw_stream_data {
	struct sdw_stream_runtime *sdw_stream;
};

/* NID */
#define RT700_AUDIO_FUNCTION_GROUP			0x01
#define RT700_DAC_OUT1					0x02
#define RT700_DAC_OUT2					0x03
#define RT700_ADC_IN1					0x09
#define RT700_ADC_IN2					0x08
#define RT700_DMIC1					0x12
#define RT700_DMIC2					0x13
#define RT700_SPK_OUT					0x14
#define RT700_MIC2					0x19
#define RT700_LINE1					0x1a
#define RT700_LINE2					0x1b
#define RT700_BEEP					0x1d
#define RT700_SPDIF					0x1e
#define RT700_VENDOR_REGISTERS				0x20
#define RT700_HP_OUT					0x21
#define RT700_MIXER_IN1					0x22
#define RT700_MIXER_IN2					0x23
#define RT700_INLINE_CMD				0x55

/* Index (NID:20h) */
#define RT700_DAC_DC_CALI_CTL1				0x00
#define RT700_PARA_VERB_CTL				0x1a
#define RT700_COMBO_JACK_AUTO_CTL1				0x45
#define RT700_COMBO_JACK_AUTO_CTL2				0x46
#define RT700_INLINE_CMD_CTL				0x48
#define RT700_DIGITAL_MISC_CTRL4			0x4a
#define RT700_VREFOUT_CTL				0x6b
#define RT700_FSM_CTL				0x6f
#define RT700_IRQ_FLAG_TABLE1				0x80
#define RT700_IRQ_FLAG_TABLE2				0x81
#define RT700_IRQ_FLAG_TABLE3				0x82

/* Verb */
#define RT700_VERB_SET_CONNECT_SEL			0x3100
#define RT700_VERB_SET_EAPD_BTLENABLE			0x3c00
#define RT700_VERB_GET_CONNECT_SEL			0xb100
#define RT700_VERB_SET_POWER_STATE			0x3500
#define RT700_VERB_SET_CHANNEL_STREAMID			0x3600
#define RT700_VERB_SET_PIN_WIDGET_CONTROL		0x3700
#define RT700_VERB_SET_UNSOLICITED_ENABLE		0x3800
#define RT700_SET_AMP_GAIN_MUTE_H			0x7300
#define RT700_SET_AMP_GAIN_MUTE_L			0x8380
#define RT700_VERB_GET_PIN_SENSE			0xb900

#define RT700_READ_HDA_3				0x2012
#define RT700_READ_HDA_2				0x2013
#define RT700_READ_HDA_1				0x2014
#define RT700_READ_HDA_0				0x2015
#define RT700_PRIV_INDEX_W_H				0x7520
#define RT700_PRIV_INDEX_W_L				0x85a0
#define RT700_PRIV_DATA_W_H				0x7420
#define RT700_PRIV_DATA_W_L				0x84a0
#define RT700_PRIV_INDEX_R_H				0x9d20
#define RT700_PRIV_INDEX_R_L				0xada0
#define RT700_PRIV_DATA_R_H				0x9c20
#define RT700_PRIV_DATA_R_L				0xaca0
#define RT700_DAC_FORMAT_H				0x7203
#define RT700_DAC_FORMAT_L				0x8283
#define RT700_ADC_FORMAT_H				0x7209
#define RT700_ADC_FORMAT_L				0x8289
#define RT700_SET_AUDIO_POWER_STATE\
	(RT700_VERB_SET_POWER_STATE | RT700_AUDIO_FUNCTION_GROUP)
#define RT700_SET_PIN_DMIC1\
	(RT700_VERB_SET_PIN_WIDGET_CONTROL | RT700_DMIC1)
#define RT700_SET_PIN_DMIC2\
	(RT700_VERB_SET_PIN_WIDGET_CONTROL | RT700_DMIC2)
#define RT700_SET_PIN_SPK\
	(RT700_VERB_SET_PIN_WIDGET_CONTROL | RT700_SPK_OUT)
#define RT700_SET_PIN_HP\
	(RT700_VERB_SET_PIN_WIDGET_CONTROL | RT700_HP_OUT)
#define RT700_SET_PIN_MIC2\
	(RT700_VERB_SET_PIN_WIDGET_CONTROL | RT700_MIC2)
#define RT700_SET_PIN_LINE1\
	(RT700_VERB_SET_PIN_WIDGET_CONTROL | RT700_LINE1)
#define RT700_SET_PIN_LINE2\
	(RT700_VERB_SET_PIN_WIDGET_CONTROL | RT700_LINE2)
#define RT700_SET_MIC2_UNSOLICITED_ENABLE\
	(RT700_VERB_SET_UNSOLICITED_ENABLE | RT700_MIC2)
#define RT700_SET_HP_UNSOLICITED_ENABLE\
	(RT700_VERB_SET_UNSOLICITED_ENABLE | RT700_HP_OUT)
#define RT700_SET_INLINE_UNSOLICITED_ENABLE\
	(RT700_VERB_SET_UNSOLICITED_ENABLE | RT700_INLINE_CMD)
#define RT700_SET_STREAMID_DAC1\
	(RT700_VERB_SET_CHANNEL_STREAMID | RT700_DAC_OUT1)
#define RT700_SET_STREAMID_DAC2\
	(RT700_VERB_SET_CHANNEL_STREAMID | RT700_DAC_OUT2)
#define RT700_SET_STREAMID_ADC1\
	(RT700_VERB_SET_CHANNEL_STREAMID | RT700_ADC_IN1)
#define RT700_SET_STREAMID_ADC2\
	(RT700_VERB_SET_CHANNEL_STREAMID | RT700_ADC_IN2)
#define RT700_SET_GAIN_DAC1_L\
	(RT700_SET_AMP_GAIN_MUTE_L | RT700_DAC_OUT1)
#define RT700_SET_GAIN_DAC1_H\
	(RT700_SET_AMP_GAIN_MUTE_H | RT700_DAC_OUT1)
#define RT700_SET_GAIN_ADC1_L\
	(RT700_SET_AMP_GAIN_MUTE_L | RT700_ADC_IN1)
#define RT700_SET_GAIN_ADC1_H\
	(RT700_SET_AMP_GAIN_MUTE_H | RT700_ADC_IN1)
#define RT700_SET_GAIN_ADC2_L\
	(RT700_SET_AMP_GAIN_MUTE_L | RT700_ADC_IN2)
#define RT700_SET_GAIN_ADC2_H\
	(RT700_SET_AMP_GAIN_MUTE_H | RT700_ADC_IN2)
#define RT700_SET_GAIN_AMIC_L\
	(RT700_SET_AMP_GAIN_MUTE_L | RT700_MIC2)
#define RT700_SET_GAIN_AMIC_H\
	(RT700_SET_AMP_GAIN_MUTE_H | RT700_MIC2)
#define RT700_SET_GAIN_HP_L\
	(RT700_SET_AMP_GAIN_MUTE_L | RT700_HP_OUT)
#define RT700_SET_GAIN_HP_H\
	(RT700_SET_AMP_GAIN_MUTE_H | RT700_HP_OUT)
#define RT700_SET_GAIN_SPK_L\
	(RT700_SET_AMP_GAIN_MUTE_L | RT700_SPK_OUT)
#define RT700_SET_GAIN_SPK_H\
	(RT700_SET_AMP_GAIN_MUTE_H | RT700_SPK_OUT)
#define RT700_SET_EAPD_SPK\
	(RT700_VERB_SET_EAPD_BTLENABLE | RT700_SPK_OUT)

/* combo jack auto switch control 2 (0x46)(NID:20h) */
#define RT700_COMBOJACK_AUTO_DET_STATUS			(0x1 << 11)
#define RT700_COMBOJACK_AUTO_DET_TRS			(0x1 << 10)
#define RT700_COMBOJACK_AUTO_DET_CTIA			(0x1 << 9)
#define RT700_COMBOJACK_AUTO_DET_OMTP			(0x1 << 8)

#define RT700_EAPD_HIGH					0x2
#define RT700_EAPD_LOW					0x0
#define RT700_MUTE_SFT					7
#define RT700_DIR_IN_SFT				6
#define RT700_DIR_OUT_SFT				7

enum {
	RT700_AIF1,
	RT700_AIF2,
	RT700_AIFS,
};

int rt700_io_init(struct device *dev, struct sdw_slave *slave);
int rt700_init(struct device *dev, struct regmap *sdw_regmap,
	       struct regmap *regmap, struct sdw_slave *slave);

int rt700_jack_detect(struct rt700_priv *rt700, bool *hp, bool *mic);
int rt700_clock_config(struct device *dev);
#endif /* __RT700_H__ */
