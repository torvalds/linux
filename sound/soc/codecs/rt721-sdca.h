/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt721-sdca.h -- RT721 SDCA ALSA SoC audio driver header
 *
 * Copyright(c) 2024 Realtek Semiconductor Corp.
 */

#ifndef __RT721_H__
#define __RT721_H__

#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/soc.h>
#include <linux/workqueue.h>

struct  rt721_sdca_priv {
	struct regmap *regmap;
	struct regmap *mbq_regmap;
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
};

struct rt721_sdca_dmic_kctrl_priv {
	unsigned int reg_base;
	unsigned int count;
	unsigned int max;
	unsigned int invert;
};

/* NID */
#define RT721_ANA_POW_PART			0x01
#define RT721_DAC_CTRL				0x04
#define RT721_JD_CTRL				0x09
#define RT721_CBJ_CTRL				0x0a
#define RT721_CAP_PORT_CTRL			0x0c
#define RT721_CLASD_AMP_CTRL			0x0d
#define RT721_BOOST_CTRL			0x0f
#define RT721_VENDOR_REG			0x20
#define RT721_RC_CALIB_CTRL			0x40
#define RT721_VENDOR_EQ_L			0x53
#define RT721_VENDOR_EQ_R			0x54
#define RT721_VENDOR_HP_CALI			0x56
#define RT721_VENDOR_CHARGE_PUMP		0x57
#define RT721_VENDOR_CLASD_CALI			0x58
#define RT721_VENDOR_IMS_DRE			0x5b
#define RT721_VENDOR_SPK_EFUSE			0x5c
#define RT721_VENDOR_LEVEL_CTRL			0x5d
#define RT721_VENDOR_ANA_CTL			0x5f
#define RT721_HDA_SDCA_FLOAT			0x61

/* Index (NID:01h) */
#define RT721_MBIAS_LV_CTRL2			0x07
#define RT721_VREF1_HV_CTRL1			0x0a
#define RT721_VREF2_LV_CTRL1			0x0b

/* Index (NID:04h) */
#define RT721_DAC_2CH_CTRL3			0x02
#define RT721_DAC_2CH_CTRL4			0x03

/* Index (NID:09h) */
#define RT721_JD_1PIN_GAT_CTRL2			0x07

/* Index (NID:0ah) */
#define RT721_CBJ_A0_GAT_CTRL1			0x04
#define RT721_CBJ_A0_GAT_CTRL2			0x05

/* Index (NID:0Ch) */
#define RT721_HP_AMP_2CH_CAL1			0x05
#define RT721_HP_AMP_2CH_CAL4			0x08
#define RT721_HP_AMP_2CH_CAL18			0x1b

/* Index (NID:0dh) */
#define RT721_CLASD_AMP_2CH_CAL			0x14

/* Index (NID:0fh) */
#define RT721_BST_4CH_TOP_GATING_CTRL1		0x05

/* Index (NID:20h) */
#define RT721_JD_PRODUCT_NUM			0x00
#define RT721_ANALOG_BIAS_CTL3			0x04
#define RT721_JD_CTRL1				0x09
#define RT721_LDO2_3_CTL1			0x0e
#define RT721_GPIO_PAD_CTRL5			0x13
#define RT721_LDO1_CTL				0x1a
#define RT721_HP_JD_CTRL			0x24
#define RT721_VD_HIDDEN_CTRL			0x26
#define RT721_CLSD_CTRL6			0x3c
#define RT721_COMBO_JACK_AUTO_CTL1		0x45
#define RT721_COMBO_JACK_AUTO_CTL2		0x46
#define RT721_COMBO_JACK_AUTO_CTL3		0x47
#define RT721_DIGITAL_MISC_CTRL4		0x4a
#define RT721_VREFO_GAT				0x63
#define RT721_FSM_CTL				0x67
#define RT721_SDCA_INTR_REC			0x82
#define RT721_SW_CONFIG1			0x8a
#define RT721_SW_CONFIG2			0x8b

/* Index (NID:40h) */
#define RT721_RC_CALIB_CTRL0			0x00

/* Index (NID:58h) */
#define RT721_DAC_DC_CALI_CTL1			0x01
#define RT721_DAC_DC_CALI_CTL2			0x02
#define RT721_DAC_DC_CALI_CTL3			0x03

/* Index (NID:5fh) */
#define RT721_MISC_POWER_CTL0			0x00
#define RT721_MISC_POWER_CTL31			0x31
#define RT721_UAJ_TOP_TCON13			0x44
#define RT721_UAJ_TOP_TCON14			0x45
#define RT721_UAJ_TOP_TCON17			0x48

/* Index (NID:61h) */
#define RT721_HDA_LEGACY_MUX_CTL0		0x00
#define RT721_HDA_LEGACY_UAJ_CTL		0x02
#define RT721_HDA_LEGACY_CTL1			0x05
#define RT721_HDA_LEGACY_RESET_CTL		0x06
#define RT721_MISC_CTL				0x07
#define RT721_XU_REL_CTRL			0x0c
#define RT721_GE_REL_CTRL1			0x0d
#define RT721_HDA_LEGACY_GPIO_WAKE_EN_CTL	0x0e
#define RT721_GE_SDCA_RST_CTRL			0x10
#define RT721_INT_RST_EN_CTRL			0x11
#define RT721_XU_EVENT_EN			0x13
#define RT721_INLINE_CTL2			0x17
#define RT721_UMP_HID_CTRL1			0x18
#define RT721_UMP_HID_CTRL2			0x19
#define RT721_UMP_HID_CTRL3			0x1a
#define RT721_UMP_HID_CTRL4			0x1b
#define RT721_UMP_HID_CTRL5			0x1c
#define RT721_FUNC_FLOAT_CTL0			0x22
#define RT721_FUNC_FLOAT_CTL1			0x23
#define RT721_FUNC_FLOAT_CTL2			0x24
#define RT721_FUNC_FLOAT_CTL3			0x25
#define RT721_ENT_FLOAT_CTL0			0x29
#define RT721_ENT_FLOAT_CTL1			0x2c
#define RT721_ENT_FLOAT_CTL2			0x2d
#define RT721_ENT_FLOAT_CTL3			0x2e
#define RT721_ENT_FLOAT_CTL4			0x2f
#define RT721_CH_FLOAT_CTL1			0x45
#define RT721_CH_FLOAT_CTL2			0x46
#define RT721_ENT_FLOAT_CTL5			0x53
#define RT721_ENT_FLOAT_CTL6			0x54
#define RT721_ENT_FLOAT_CTL7			0x55
#define RT721_ENT_FLOAT_CTL8			0x57
#define RT721_ENT_FLOAT_CTL9			0x5a
#define RT721_ENT_FLOAT_CTL10			0x5b
#define RT721_CH_FLOAT_CTL3			0x6a
#define RT721_CH_FLOAT_CTL4			0x6d
#define RT721_CH_FLOAT_CTL5			0x70
#define RT721_CH_FLOAT_CTL6			0x92

/* Parameter & Verb control 01 (0x26)(NID:20h) */
#define RT721_HIDDEN_REG_SW_RESET (0x1 << 14)

/* Buffer address for HID */
#define RT721_BUF_ADDR_HID1			0x44030000
#define RT721_BUF_ADDR_HID2			0x44030020

/* RT721 SDCA Control - function number */
#define FUNC_NUM_JACK_CODEC			0x01
#define FUNC_NUM_MIC_ARRAY			0x02
#define FUNC_NUM_HID				0x03
#define FUNC_NUM_AMP				0x04

/* RT721 SDCA entity */
#define RT721_SDCA_ENT_HID01			0x01
#define RT721_SDCA_ENT_XUV			0x03
#define RT721_SDCA_ENT_GE49			0x49
#define RT721_SDCA_ENT_USER_FU05		0x05
#define RT721_SDCA_ENT_USER_FU06		0x06
#define RT721_SDCA_ENT_USER_FU0F		0x0f
#define RT721_SDCA_ENT_USER_FU10		0x19
#define RT721_SDCA_ENT_USER_FU1E		0x1e
#define RT721_SDCA_ENT_FU15			0x15
#define RT721_SDCA_ENT_PDE23			0x23
#define RT721_SDCA_ENT_PDE40			0x40
#define RT721_SDCA_ENT_PDE41			0x41
#define RT721_SDCA_ENT_PDE11			0x11
#define RT721_SDCA_ENT_PDE12			0x12
#define RT721_SDCA_ENT_PDE2A			0x2a
#define RT721_SDCA_ENT_CS01			0x01
#define RT721_SDCA_ENT_CS11			0x11
#define RT721_SDCA_ENT_CS1F			0x1f
#define RT721_SDCA_ENT_CS1C			0x1c
#define RT721_SDCA_ENT_CS31			0x31
#define RT721_SDCA_ENT_OT23			0x42
#define RT721_SDCA_ENT_IT26			0x26
#define RT721_SDCA_ENT_IT09			0x09
#define RT721_SDCA_ENT_PLATFORM_FU15		0x15
#define RT721_SDCA_ENT_PLATFORM_FU44		0x44
#define RT721_SDCA_ENT_XU03			0x03
#define RT721_SDCA_ENT_XU0D			0x0d
#define RT721_SDCA_ENT_FU55			0x55

/* RT721 SDCA control */
#define RT721_SDCA_CTL_SAMPLE_FREQ_INDEX		0x10
#define RT721_SDCA_CTL_FU_MUTE				0x01
#define RT721_SDCA_CTL_FU_VOLUME			0x02
#define RT721_SDCA_CTL_HIDTX_CURRENT_OWNER		0x10
#define RT721_SDCA_CTL_HIDTX_SET_OWNER_TO_DEVICE	0x11
#define RT721_SDCA_CTL_HIDTX_MESSAGE_OFFSET		0x12
#define RT721_SDCA_CTL_HIDTX_MESSAGE_LENGTH		0x13
#define RT721_SDCA_CTL_SELECTED_MODE			0x01
#define RT721_SDCA_CTL_DETECTED_MODE			0x02
#define RT721_SDCA_CTL_REQ_POWER_STATE			0x01
#define RT721_SDCA_CTL_VENDOR_DEF			0x30
#define RT721_SDCA_CTL_XUV				0x34
#define RT721_SDCA_CTL_FU_CH_GAIN			0x0b

/* RT721 SDCA channel */
#define CH_L	0x01
#define CH_R	0x02
#define CH_01	0x01
#define CH_02	0x02
#define CH_03	0x03
#define CH_04	0x04
#define CH_08	0x08
#define CH_09	0x09
#define CH_0A	0x0a

/* sample frequency index */
#define RT721_SDCA_RATE_8000HZ		0x01
#define RT721_SDCA_RATE_11025HZ		0x02
#define RT721_SDCA_RATE_12000HZ		0x03
#define RT721_SDCA_RATE_16000HZ		0x04
#define RT721_SDCA_RATE_22050HZ		0x05
#define RT721_SDCA_RATE_24000HZ		0x06
#define RT721_SDCA_RATE_32000HZ		0x07
#define RT721_SDCA_RATE_44100HZ		0x08
#define RT721_SDCA_RATE_48000HZ		0x09
#define RT721_SDCA_RATE_88200HZ		0x0a
#define RT721_SDCA_RATE_96000HZ		0x0b
#define RT721_SDCA_RATE_176400HZ	0x0c
#define RT721_SDCA_RATE_192000HZ	0x0d
#define RT721_SDCA_RATE_384000HZ	0x0e
#define RT721_SDCA_RATE_768000HZ	0x0f

/* RT721 HID ID */
#define RT721_SDCA_HID_ID		0x11

enum {
	RT721_AIF1, /* For headset mic and headphone */
	RT721_AIF2, /* For speaker */
	RT721_AIF3, /* For dmic */
	RT721_AIFS,
};

int rt721_sdca_io_init(struct device *dev, struct sdw_slave *slave);
int rt721_sdca_init(struct device *dev, struct regmap *regmap,
			struct regmap *mbq_regmap, struct sdw_slave *slave);
#endif /* __RT721_H__ */
