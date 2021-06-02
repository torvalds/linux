/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt711-sdca.h -- RT711 SDCA ALSA SoC audio driver header
 *
 * Copyright(c) 2021 Realtek Semiconductor Corp.
 */

#ifndef __RT711_SDCA_H__
#define __RT711_SDCA_H__

#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/soc.h>
#include <linux/workqueue.h>

struct  rt711_sdca_priv {
	struct regmap *regmap, *mbq_regmap;
	struct snd_soc_component *component;
	struct sdw_slave *slave;
	enum sdw_slave_status status;
	struct sdw_bus_params params;
	bool hw_init;
	bool first_hw_init;
	struct snd_soc_jack *hs_jack;
	struct delayed_work jack_detect_work;
	struct delayed_work jack_btn_check_work;
	struct mutex calibrate_mutex; /* for headset calibration */
	int jack_type, jd_src;
	unsigned int scp_sdca_stat1, scp_sdca_stat2;
	int hw_ver;
	bool fu0f_dapm_mute, fu0f_mixer_l_mute, fu0f_mixer_r_mute;
	bool fu1e_dapm_mute, fu1e_mixer_l_mute, fu1e_mixer_r_mute;
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
#define RT711_VENDOR_IMS_DRE				0x5b
#define RT711_VENDOR_VAD				0x5e
#define RT711_VENDOR_ANALOG_CTL				0x5f
#define RT711_VENDOR_HDA_CTL				0x61

/* Index (NID:20h) */
#define RT711_JD_PRODUCT_NUM			0x00
#define RT711_DMIC_CTL1					0x06
#define RT711_JD_CTL1					0x08
#define RT711_JD_CTL2					0x09
#define RT711_CC_DET1					0x11
#define RT711_PARA_VERB_CTL				0x1a
#define RT711_COMBO_JACK_AUTO_CTL1			0x45
#define RT711_COMBO_JACK_AUTO_CTL2			0x46
#define RT711_COMBO_JACK_AUTO_CTL3			0x47
#define RT711_INLINE_CMD_CTL				0x48
#define RT711_DIGITAL_MISC_CTRL4			0x4a
#define RT711_JD_CTRL6			0x6a
#define RT711_VREFOUT_CTL				0x6b
#define RT711_GPIO_TEST_MODE_CTL2			0x6d
#define RT711_FSM_CTL					0x6f
#define RT711_IRQ_FLAG_TABLE1				0x80
#define RT711_IRQ_FLAG_TABLE2				0x81
#define RT711_IRQ_FLAG_TABLE3				0x82
#define RT711_HP_FSM_CTL				0x83
#define RT711_TX_RX_MUX_CTL				0x91
#define RT711_FILTER_SRC_SEL				0xb0
#define RT711_ADC27_VOL_SET				0xb7

/* Index (NID:58h) */
#define RT711_DAC_DC_CALI_CTL1				0x00
#define RT711_DAC_DC_CALI_CTL2				0x01

/* Index (NID:5bh) */
#define RT711_IMS_DIGITAL_CTL1				0x00
#define RT711_HP_IMS_RESULT_L				0x20
#define RT711_HP_IMS_RESULT_R				0x21

/* Index (NID:5eh) */
#define RT711_VAD_SRAM_CTL1				0x10

/* Index (NID:5fh) */
#define RT711_MISC_POWER_CTL0				0x01
#define RT711_MISC_POWER_CTL4				0x05

/* Index (NID:61h) */
#define RT711_HDA_LEGACY_MUX_CTL1			0x00
#define RT711_HDA_LEGACY_UNSOLICITED_CTL	0x03
#define RT711_HDA_LEGACY_CONFIG_CTL			0x06
#define RT711_HDA_LEGACY_RESET_CTL			0x08
#define RT711_HDA_LEGACY_GPIO_CTL			0x0a
#define RT711_ADC08_09_PDE_CTL				0x24
#define RT711_GE_MODE_RELATED_CTL			0x35
#define RT711_PUSH_BTN_INT_CTL0				0x36
#define RT711_PUSH_BTN_INT_CTL1				0x37
#define RT711_PUSH_BTN_INT_CTL2				0x38
#define RT711_PUSH_BTN_INT_CTL6				0x3c
#define RT711_PUSH_BTN_INT_CTL7				0x3d
#define RT711_PUSH_BTN_INT_CTL9				0x3f

/* DAC DC offset calibration control-1 (0x00)(NID:20h) */
#define RT711_DAC_DC_CALI_TRIGGER (0x1 << 15)
#define RT711_DAC_DC_CALI_CLK_EN (0x1 << 14)
#define RT711_DAC_DC_FORCE_CALI_RST (0x1 << 3)

/* jack detect control 1 (0x08)(NID:20h) */
#define RT711_JD2_DIGITAL_MODE_SEL (0x1 << 1)

/* jack detect control 2 (0x09)(NID:20h) */
#define RT711_JD2_2PORT_200K_DECODE_HP (0x1 << 13)
#define RT711_HP_JD_SEL_JD1 (0x0 << 1)
#define RT711_HP_JD_SEL_JD2 (0x1 << 1)

/* CC DET1 (0x11)(NID:20h) */
#define RT711_HP_JD_FINAL_RESULT_CTL_JD12 (0x1 << 10)
#define RT711_HP_JD_FINAL_RESULT_CTL_CCDET (0x0 << 10)

/* Parameter & Verb control (0x1a)(NID:20h) */
#define RT711_HIDDEN_REG_SW_RESET (0x1 << 14)

/* combo jack auto switch control 2 (0x46)(NID:20h) */
#define RT711_COMBOJACK_AUTO_DET_STATUS			(0x1 << 11)
#define RT711_COMBOJACK_AUTO_DET_TRS			(0x1 << 10)
#define RT711_COMBOJACK_AUTO_DET_CTIA			(0x1 << 9)
#define RT711_COMBOJACK_AUTO_DET_OMTP			(0x1 << 8)

/* FSM control (0x6f)(NID:20h) */
#define RT711_CALI_CTL			(0x0 << 0)
#define RT711_COMBOJACK_CTL		(0x1 << 0)
#define RT711_IMS_CTL			(0x2 << 0)
#define RT711_DEPOP_CTL			(0x3 << 0)
#define RT711_FSM_IMP_EN		(0x1 << 6)

/* Impedance Sense Digital Control 1 (0x00)(NID:5bh) */
#define RT711_TRIGGER_IMS		(0x1 << 15)
#define RT711_IMS_EN			(0x1 << 6)

#define RT711_EAPD_HIGH				0x2
#define RT711_EAPD_LOW				0x0
#define RT711_MUTE_SFT				7
/* set input/output mapping to payload[14][15] separately */
#define RT711_DIR_IN_SFT			6
#define RT711_DIR_OUT_SFT			7

/* RC Calibration register */
#define RT711_RC_CAL_STATUS			0x320c

/* Buffer address for HID */
#define RT711_BUF_ADDR_HID1			0x44030000
#define RT711_BUF_ADDR_HID2			0x44030020

/* RT711 SDCA Control - function number */
#define FUNC_NUM_JACK_CODEC 0x01
#define FUNC_NUM_MIC_ARRAY 0x02
#define FUNC_NUM_HID 0x03

/* RT711 SDCA entity */
#define RT711_SDCA_ENT_HID01 0x01
#define RT711_SDCA_ENT_GE49 0x49
#define RT711_SDCA_ENT_USER_FU05 0x05
#define RT711_SDCA_ENT_USER_FU0F 0x0f
#define RT711_SDCA_ENT_USER_FU1E 0x1e
#define RT711_SDCA_ENT_PLATFORM_FU15 0x15
#define RT711_SDCA_ENT_PLATFORM_FU44 0x44
#define RT711_SDCA_ENT_PDE28 0x28
#define RT711_SDCA_ENT_PDE29 0x29
#define RT711_SDCA_ENT_PDE2A 0x2a
#define RT711_SDCA_ENT_CS01 0x01
#define RT711_SDCA_ENT_CS11 0x11
#define RT711_SDCA_ENT_CS1F 0x1f
#define RT711_SDCA_ENT_OT1 0x06
#define RT711_SDCA_ENT_LINE1 0x09
#define RT711_SDCA_ENT_LINE2 0x31
#define RT711_SDCA_ENT_PDELINE2 0x36
#define RT711_SDCA_ENT_USER_FU9 0x41

/* RT711 SDCA control */
#define RT711_SDCA_CTL_SAMPLE_FREQ_INDEX 0x10
#define RT711_SDCA_CTL_FU_CH_GAIN 0x0b
#define RT711_SDCA_CTL_FU_MUTE 0x01
#define RT711_SDCA_CTL_FU_VOLUME 0x02
#define RT711_SDCA_CTL_HIDTX_CURRENT_OWNER 0x10
#define RT711_SDCA_CTL_HIDTX_SET_OWNER_TO_DEVICE 0x11
#define RT711_SDCA_CTL_HIDTX_MESSAGE_OFFSET 0x12
#define RT711_SDCA_CTL_HIDTX_MESSAGE_LENGTH 0x13
#define RT711_SDCA_CTL_SELECTED_MODE 0x01
#define RT711_SDCA_CTL_DETECTED_MODE 0x02
#define RT711_SDCA_CTL_REQ_POWER_STATE 0x01
#define RT711_SDCA_CTL_VENDOR_DEF 0x30

/* RT711 SDCA channel */
#define CH_L 0x01
#define CH_R 0x02

/* sample frequency index */
#define RT711_SDCA_RATE_44100HZ		0x08
#define RT711_SDCA_RATE_48000HZ		0x09
#define RT711_SDCA_RATE_96000HZ		0x0b
#define RT711_SDCA_RATE_192000HZ	0x0d

enum {
	RT711_AIF1,
	RT711_AIF2,
	RT711_AIFS,
};

enum rt711_sdca_jd_src {
	RT711_JD_NULL,
	RT711_JD1,
	RT711_JD2
};

enum rt711_sdca_ver {
	RT711_VER_VD0,
	RT711_VER_VD1
};

int rt711_sdca_io_init(struct device *dev, struct sdw_slave *slave);
int rt711_sdca_init(struct device *dev, struct regmap *regmap,
	       struct regmap *mbq_regmap, struct sdw_slave *slave);

int rt711_sdca_jack_detect(struct rt711_sdca_priv *rt711, bool *hp, bool *mic);
#endif /* __RT711_SDCA_H__ */
