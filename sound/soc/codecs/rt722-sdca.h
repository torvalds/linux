/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt722-sdca.h -- RT722 SDCA ALSA SoC audio driver header
 *
 * Copyright(c) 2023 Realtek Semiconductor Corp.
 */

#ifndef __RT722_H__
#define __RT722_H__

#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/soc.h>
#include <linux/workqueue.h>

struct  rt722_sdca_priv {
	struct regmap *regmap;
	struct snd_soc_component *component;
	struct sdw_slave *slave;
	struct sdw_bus_params params;
	bool hw_init;
	bool first_hw_init;
	struct mutex calibrate_mutex;
	struct mutex disable_irq_lock;
	bool disable_irq;
	/* For Headset jack & Headphone */
	unsigned int scp_sdca_stat1;
	unsigned int scp_sdca_stat2;
	struct snd_soc_jack *hs_jack;
	struct delayed_work jack_detect_work;
	struct delayed_work jack_btn_check_work;
	int jack_type;
	int jd_src;
	bool fu0f_dapm_mute;
	bool fu0f_mixer_l_mute;
	bool fu0f_mixer_r_mute;
	/* For DMIC */
	bool fu1e_dapm_mute;
	bool fu1e_mixer_mute[4];
	int hw_vid;
};

struct rt722_sdca_dmic_kctrl_priv {
	unsigned int reg_base;
	unsigned int count;
	unsigned int max;
	unsigned int invert;
};

/* NID */
#define RT722_VENDOR_REG			0x20
#define RT722_VENDOR_CALI			0x58
#define RT722_VENDOR_SPK_EFUSE			0x5c
#define RT722_VENDOR_IMS_DRE			0x5b
#define RT722_VENDOR_ANALOG_CTL			0x5f
#define RT722_VENDOR_HDA_CTL			0x61

/* Index (NID:20h) */
#define RT722_JD_PRODUCT_NUM			0x00
#define RT722_ANALOG_BIAS_CTL3			0x04
#define RT722_JD_CTRL1				0x09
#define RT722_LDO2_3_CTL1			0x0e
#define RT722_LDO1_CTL				0x1a
#define RT722_HP_JD_CTRL			0x24
#define RT722_CLSD_CTRL6			0x3c
#define RT722_COMBO_JACK_AUTO_CTL1		0x45
#define RT722_COMBO_JACK_AUTO_CTL2		0x46
#define RT722_COMBO_JACK_AUTO_CTL3		0x47
#define RT722_DIGITAL_MISC_CTRL4		0x4a
#define RT722_VREFO_GAT				0x63
#define RT722_FSM_CTL				0x67
#define RT722_SDCA_INTR_REC			0x82
#define RT722_SW_CONFIG1			0x8a
#define RT722_SW_CONFIG2			0x8b

/* Index (NID:58h) */
#define RT722_DAC_DC_CALI_CTL0			0x00
#define RT722_DAC_DC_CALI_CTL1			0x01
#define RT722_DAC_DC_CALI_CTL2			0x02
#define RT722_DAC_DC_CALI_CTL3			0x03

/* Index (NID:59h) */
#define RT722_ULTRA_SOUND_DETECTOR6		0x1e

/* Index (NID:5bh) */
#define RT722_IMS_DIGITAL_CTL1			0x00
#define RT722_IMS_DIGITAL_CTL5			0x05
#define RT722_HP_DETECT_RLDET_CTL1		0x29
#define RT722_HP_DETECT_RLDET_CTL2		0x2a

/* Index (NID:5fh) */
#define RT722_MISC_POWER_CTL0			0x00
#define RT722_MISC_POWER_CTL7			0x08

/* Index (NID:61h) */
#define RT722_HDA_LEGACY_MUX_CTL0		0x00
#define RT722_HDA_LEGACY_UNSOL_CTL		0x03
#define RT722_HDA_LEGACY_CONFIG_CTL0		0x06
#define RT722_HDA_LEGACY_RESET_CTL		0x08
#define RT722_HDA_LEGACY_GPIO_WAKE_EN_CTL	0x0e
#define RT722_DMIC_ENT_FLOAT_CTL		0x10
#define RT722_DMIC_GAIN_ENT_FLOAT_CTL0		0x11
#define RT722_DMIC_GAIN_ENT_FLOAT_CTL2		0x13
#define RT722_ADC_ENT_FLOAT_CTL			0x15
#define RT722_ADC_VOL_CH_FLOAT_CTL		0x17
#define RT722_ADC_SAMPLE_RATE_FLOAT		0x18
#define RT722_DAC03_HP_PDE_FLOAT_CTL		0x22
#define RT722_MIC2_LINE2_PDE_FLOAT_CTL		0x23
#define RT722_ET41_LINE2_PDE_FLOAT_CTL		0x24
#define RT722_ADC0A_08_PDE_FLOAT_CTL		0x25
#define RT722_ADC10_PDE_FLOAT_CTL		0x26
#define RT722_DMIC1_2_PDE_FLOAT_CTL		0x28
#define RT722_AMP_PDE_FLOAT_CTL			0x29
#define RT722_I2S_IN_OUT_PDE_FLOAT_CTL		0x2f
#define RT722_GE_RELATED_CTL1			0x45
#define RT722_GE_RELATED_CTL2			0x46
#define RT722_MIXER_CTL0			0x52
#define RT722_MIXER_CTL1			0x53
#define RT722_EAPD_CTL				0x55
#define RT722_UMP_HID_CTL0			0x60
#define RT722_UMP_HID_CTL1			0x61
#define RT722_UMP_HID_CTL2			0x62
#define RT722_UMP_HID_CTL3			0x63
#define RT722_UMP_HID_CTL4			0x64
#define RT722_UMP_HID_CTL5			0x65
#define RT722_UMP_HID_CTL6			0x66
#define RT722_UMP_HID_CTL7			0x67
#define RT722_UMP_HID_CTL8			0x68
#define RT722_FLOAT_CTRL_1			0x70
#define RT722_ENT_FLOAT_CTRL_1		0x76

/* Parameter & Verb control 01 (0x1a)(NID:20h) */
#define RT722_HIDDEN_REG_SW_RESET (0x1 << 14)

/* combo jack auto switch control 2 (0x46)(NID:20h) */
#define RT722_COMBOJACK_AUTO_DET_STATUS		(0x1 << 11)
#define RT722_COMBOJACK_AUTO_DET_TRS		(0x1 << 10)
#define RT722_COMBOJACK_AUTO_DET_CTIA		(0x1 << 9)
#define RT722_COMBOJACK_AUTO_DET_OMTP		(0x1 << 8)

/* DAC calibration control (0x00)(NID:58h) */
#define RT722_DC_CALIB_CTRL (0x1 << 16)
/* DAC DC offset calibration control-1 (0x01)(NID:58h) */
#define RT722_PDM_DC_CALIB_STATUS (0x1 << 15)

#define RT722_EAPD_HIGH				0x2
#define RT722_EAPD_LOW				0x0

/* Buffer address for HID */
#define RT722_BUF_ADDR_HID1			0x44030000
#define RT722_BUF_ADDR_HID2			0x44030020

/* RT722 SDCA Control - function number */
#define FUNC_NUM_JACK_CODEC			0x01
#define FUNC_NUM_MIC_ARRAY			0x02
#define FUNC_NUM_HID				0x03
#define FUNC_NUM_AMP				0x04

/* RT722 SDCA entity */
#define RT722_SDCA_ENT_HID01			0x01
#define RT722_SDCA_ENT_GE49			0x49
#define RT722_SDCA_ENT_USER_FU05		0x05
#define RT722_SDCA_ENT_USER_FU06		0x06
#define RT722_SDCA_ENT_USER_FU0F		0x0f
#define RT722_SDCA_ENT_USER_FU10		0x19
#define RT722_SDCA_ENT_USER_FU1E		0x1e
#define RT722_SDCA_ENT_FU15			0x15
#define RT722_SDCA_ENT_PDE23			0x23
#define RT722_SDCA_ENT_PDE40			0x40
#define RT722_SDCA_ENT_PDE11			0x11
#define RT722_SDCA_ENT_PDE12			0x12
#define RT722_SDCA_ENT_PDE2A			0x2a
#define RT722_SDCA_ENT_CS01			0x01
#define RT722_SDCA_ENT_CS11			0x11
#define RT722_SDCA_ENT_CS1F			0x1f
#define RT722_SDCA_ENT_CS1C			0x1c
#define RT722_SDCA_ENT_CS31			0x31
#define RT722_SDCA_ENT_OT23			0x42
#define RT722_SDCA_ENT_IT26			0x26
#define RT722_SDCA_ENT_IT09			0x09
#define RT722_SDCA_ENT_PLATFORM_FU15		0x15
#define RT722_SDCA_ENT_PLATFORM_FU44		0x44
#define RT722_SDCA_ENT_XU03			0x03
#define RT722_SDCA_ENT_XU0D			0x0d
#define RT722_SDCA_ENT0 0x00

/* RT722 SDCA control */
#define RT722_SDCA_CTL_SAMPLE_FREQ_INDEX		0x10
#define RT722_SDCA_CTL_FU_MUTE				0x01
#define RT722_SDCA_CTL_FU_VOLUME			0x02
#define RT722_SDCA_CTL_HIDTX_CURRENT_OWNER		0x10
#define RT722_SDCA_CTL_HIDTX_SET_OWNER_TO_DEVICE	0x11
#define RT722_SDCA_CTL_HIDTX_MESSAGE_OFFSET		0x12
#define RT722_SDCA_CTL_HIDTX_MESSAGE_LENGTH		0x13
#define RT722_SDCA_CTL_SELECTED_MODE			0x01
#define RT722_SDCA_CTL_DETECTED_MODE			0x02
#define RT722_SDCA_CTL_REQ_POWER_STATE			0x01
#define RT722_SDCA_CTL_VENDOR_DEF			0x30
#define RT722_SDCA_CTL_FU_CH_GAIN			0x0b
#define RT722_SDCA_CTL_FUNC_STATUS			0x10
#define RT722_SDCA_CTL_ACTUAL_POWER_STATE		0x10

/* RT722 SDCA channel */
#define CH_L	0x01
#define CH_R	0x02
#define CH_01	0x01
#define CH_02	0x02
#define CH_03	0x03
#define CH_04	0x04
#define CH_08	0x08

/* sample frequency index */
#define RT722_SDCA_RATE_16000HZ		0x04
#define RT722_SDCA_RATE_32000HZ		0x07
#define RT722_SDCA_RATE_44100HZ		0x08
#define RT722_SDCA_RATE_48000HZ		0x09
#define RT722_SDCA_RATE_96000HZ		0x0b
#define RT722_SDCA_RATE_192000HZ	0x0d

/* Function_Status */
#define FUNCTION_NEEDS_INITIALIZATION		BIT(5)

enum {
	RT722_AIF1, /* For headset mic and headphone */
	RT722_AIF2, /* For speaker */
	RT722_AIF3, /* For dmic */
	RT722_AIFS,
};

enum rt722_sdca_jd_src {
	RT722_JD_NULL,
	RT722_JD1,
};

enum rt722_sdca_version {
	RT722_VA,
	RT722_VB,
};

int rt722_sdca_io_init(struct device *dev, struct sdw_slave *slave);
int rt722_sdca_init(struct device *dev, struct regmap *regmap, struct sdw_slave *slave);
int rt722_sdca_index_write(struct rt722_sdca_priv *rt722,
		unsigned int nid, unsigned int reg, unsigned int value);
int rt722_sdca_index_read(struct rt722_sdca_priv *rt722,
		unsigned int nid, unsigned int reg, unsigned int *value);

int rt722_sdca_jack_detect(struct rt722_sdca_priv *rt722, bool *hp, bool *mic);
#endif /* __RT722_H__ */
