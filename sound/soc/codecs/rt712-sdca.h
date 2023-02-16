/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt712-sdca.h -- RT712 SDCA ALSA SoC audio driver header
 *
 * Copyright(c) 2023 Realtek Semiconductor Corp.
 */

#ifndef __RT712_H__
#define __RT712_H__

#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/soc.h>
#include <linux/workqueue.h>

struct  rt712_sdca_priv {
	struct regmap *regmap;
	struct regmap *mbq_regmap;
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
	struct mutex disable_irq_lock; /* SDCA irq lock protection */
	bool disable_irq;
	int jack_type;
	int jd_src;
	unsigned int scp_sdca_stat1;
	unsigned int scp_sdca_stat2;
	unsigned int hw_id;
	bool fu0f_dapm_mute;
	bool fu0f_mixer_l_mute;
	bool fu0f_mixer_r_mute;
};

struct sdw_stream_data {
	struct sdw_stream_runtime *sdw_stream;
};

/* NID */
#define RT712_VENDOR_REG			0x20
#define RT712_VENDOR_CALI			0x58
#define RT712_ULTRA_SOUND_DET			0x59
#define RT712_VENDOR_IMS_DRE			0x5b
#define RT712_VENDOR_ANALOG_CTL			0x5f
#define RT712_VENDOR_HDA_CTL			0x61

/* Index (NID:20h) */
#define RT712_JD_PRODUCT_NUM			0x00
#define RT712_ANALOG_BIAS_CTL3			0x04
#define RT712_LDO2_3_CTL1			0x0e
#define RT712_PARA_VERB_CTL			0x1a
#define RT712_CC_DET1				0x24
#define RT712_COMBO_JACK_AUTO_CTL1		0x45
#define RT712_COMBO_JACK_AUTO_CTL2		0x46
#define RT712_COMBO_JACK_AUTO_CTL3		0x47
#define RT712_DIGITAL_MISC_CTRL4		0x4a
#define RT712_FSM_CTL				0x67
#define RT712_SW_CONFIG1			0x8a
#define RT712_SW_CONFIG2			0x8b

/* Index (NID:58h) */
#define RT712_DAC_DC_CALI_CTL1			0x00
#define RT712_DAC_DC_CALI_CTL2			0x01

/* Index (NID:59h) */
#define RT712_ULTRA_SOUND_DETECTOR6		0x1e

/* Index (NID:5bh) */
#define RT712_IMS_DIGITAL_CTL1			0x00
#define RT712_IMS_DIGITAL_CTL5			0x05
#define RT712_HP_DETECT_RLDET_CTL1		0x29
#define RT712_HP_DETECT_RLDET_CTL2		0x2a

/* Index (NID:5fh) */
#define RT712_MISC_POWER_CTL0			0x00
#define RT712_MISC_POWER_CTL7			0x08

/* Index (NID:61h) */
#define RT712_HDA_LEGACY_MUX_CTL0			0x00
#define RT712_HDA_LEGACY_CONFIG_CTL0			0x06
#define RT712_HDA_LEGACY_RESET_CTL			0x08
#define RT712_HDA_LEGACY_GPIO_WAKE_EN_CTL		0x0e
#define RT712_DMIC_ENT_FLOAT_CTL			0x10
#define RT712_DMIC_GAIN_ENT_FLOAT_CTL0			0x11
#define RT712_DMIC_GAIN_ENT_FLOAT_CTL2			0x13
#define RT712_ADC_ENT_FLOAT_CTL				0x15
#define RT712_ADC_VOL_CH_FLOAT_CTL2			0x18
#define RT712_DAC03_HP_PDE_FLOAT_CTL			0x22
#define RT712_MIC2_LINE2_PDE_FLOAT_CTL			0x23
#define RT712_ADC0A_08_PDE_FLOAT_CTL			0x26
#define RT712_ADC0B_11_PDE_FLOAT_CTL			0x27
#define RT712_DMIC1_2_PDE_FLOAT_CTL			0x2b
#define RT712_AMP_PDE_FLOAT_CTL				0x2c
#define RT712_I2S_IN_OUT_PDE_FLOAT_CTL			0x2f
#define RT712_GE_RELATED_CTL1				0x45
#define RT712_GE_RELATED_CTL2				0x46
#define RT712_MIXER_CTL0				0x52
#define RT712_MIXER_CTL1				0x53
#define RT712_EAPD_CTL					0x55
#define RT712_UMP_HID_CTL0				0x60
#define RT712_UMP_HID_CTL1				0x61
#define RT712_UMP_HID_CTL2				0x62
#define RT712_UMP_HID_CTL3				0x63
#define RT712_UMP_HID_CTL4				0x64
#define RT712_UMP_HID_CTL5				0x65
#define RT712_UMP_HID_CTL6				0x66
#define RT712_UMP_HID_CTL7				0x67
#define RT712_UMP_HID_CTL8				0x68

/* Parameter & Verb control 01 (0x1a)(NID:20h) */
#define RT712_HIDDEN_REG_SW_RESET (0x1 << 14)

/* combo jack auto switch control 2 (0x46)(NID:20h) */
#define RT712_COMBOJACK_AUTO_DET_STATUS			(0x1 << 11)
#define RT712_COMBOJACK_AUTO_DET_TRS			(0x1 << 10)
#define RT712_COMBOJACK_AUTO_DET_CTIA			(0x1 << 9)
#define RT712_COMBOJACK_AUTO_DET_OMTP			(0x1 << 8)

/* DAC DC offset calibration control-1 (0x00)(NID:58h) */
#define RT712_DAC_DC_CALI_TRIGGER (0x1 << 15)

#define RT712_EAPD_HIGH				0x2
#define RT712_EAPD_LOW				0x0

/* RC Calibration register */
#define RT712_RC_CAL			0x3201

/* Buffer address for HID */
#define RT712_BUF_ADDR_HID1			0x44030000
#define RT712_BUF_ADDR_HID2			0x44030020

/* RT712 SDCA Control - function number */
#define FUNC_NUM_JACK_CODEC 0x01
#define FUNC_NUM_MIC_ARRAY 0x02
#define FUNC_NUM_HID 0x03
#define FUNC_NUM_AMP 0x04

/* RT712 SDCA entity */
#define RT712_SDCA_ENT_HID01 0x01
#define RT712_SDCA_ENT_GE49 0x49
#define RT712_SDCA_ENT_USER_FU05 0x05
#define RT712_SDCA_ENT_USER_FU06 0x06
#define RT712_SDCA_ENT_USER_FU0F 0x0f
#define RT712_SDCA_ENT_USER_FU10 0x19
#define RT712_SDCA_ENT_USER_FU1E 0x1e
#define RT712_SDCA_ENT_FU15 0x15
#define RT712_SDCA_ENT_PDE23 0x23
#define RT712_SDCA_ENT_PDE40 0x40
#define RT712_SDCA_ENT_PDE11 0x11
#define RT712_SDCA_ENT_PDE12 0x12
#define RT712_SDCA_ENT_CS01 0x01
#define RT712_SDCA_ENT_CS11 0x11
#define RT712_SDCA_ENT_CS1F 0x1f
#define RT712_SDCA_ENT_CS1C 0x1c
#define RT712_SDCA_ENT_CS31 0x31
#define RT712_SDCA_ENT_OT23 0x42
#define RT712_SDCA_ENT_IT26 0x26
#define RT712_SDCA_ENT_IT09 0x09
#define RT712_SDCA_ENT_PLATFORM_FU15 0x15
#define RT712_SDCA_ENT_PLATFORM_FU44 0x44

/* RT712 SDCA control */
#define RT712_SDCA_CTL_SAMPLE_FREQ_INDEX 0x10
#define RT712_SDCA_CTL_FU_MUTE 0x01
#define RT712_SDCA_CTL_FU_VOLUME 0x02
#define RT712_SDCA_CTL_HIDTX_CURRENT_OWNER 0x10
#define RT712_SDCA_CTL_HIDTX_SET_OWNER_TO_DEVICE 0x11
#define RT712_SDCA_CTL_HIDTX_MESSAGE_OFFSET 0x12
#define RT712_SDCA_CTL_HIDTX_MESSAGE_LENGTH 0x13
#define RT712_SDCA_CTL_SELECTED_MODE 0x01
#define RT712_SDCA_CTL_DETECTED_MODE 0x02
#define RT712_SDCA_CTL_REQ_POWER_STATE 0x01
#define RT712_SDCA_CTL_VENDOR_DEF 0x30
#define RT712_SDCA_CTL_FU_CH_GAIN 0x0b

/* RT712 SDCA channel */
#define CH_L 0x01
#define CH_R 0x02

/* sample frequency index */
#define RT712_SDCA_RATE_16000HZ		0x04
#define RT712_SDCA_RATE_32000HZ		0x07
#define RT712_SDCA_RATE_44100HZ		0x08
#define RT712_SDCA_RATE_48000HZ		0x09
#define RT712_SDCA_RATE_96000HZ		0x0b
#define RT712_SDCA_RATE_192000HZ	0x0d

enum {
	RT712_AIF1,
	RT712_AIF2,
};

enum rt712_sdca_jd_src {
	RT712_JD_NULL,
	RT712_JD1,
};

enum rt712_sdca_hw_id {
	RT712_DEV_ID_712 = 0x7,
	RT712_DEV_ID_713 = 0x6,
	RT712_DEV_ID_716 = 0x5,
	RT712_DEV_ID_717 = 0x4,
};

#define RT712_PART_ID_713 0x713

int rt712_sdca_io_init(struct device *dev, struct sdw_slave *slave);
int rt712_sdca_init(struct device *dev, struct regmap *regmap,
			struct regmap *mbq_regmap, struct sdw_slave *slave);

int rt712_sdca_jack_detect(struct rt712_sdca_priv *rt712, bool *hp, bool *mic);
#endif /* __RT712_H__ */
