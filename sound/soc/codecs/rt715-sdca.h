/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt715-sdca.h -- RT715 ALSA SoC audio driver header
 *
 * Copyright(c) 2020 Realtek Semiconductor Corp.
 */

#ifndef __RT715_SDCA_H__
#define __RT715_SDCA_H__

#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/soc.h>
#include <linux/workqueue.h>
#include <linux/device.h>

struct rt715_sdca_priv {
	struct regmap *regmap;
	struct regmap *mbq_regmap;
	struct snd_soc_codec *codec;
	struct sdw_slave *slave;
	struct delayed_work adc_mute_work;
	int dbg_nid;
	int dbg_vid;
	int dbg_payload;
	enum sdw_slave_status status;
	struct sdw_bus_params params;
	bool hw_init;
	bool first_init;
	int l_is_unmute;
	int r_is_unmute;
	int hw_sdw_ver;
	int kctl_switch_orig[4];
	int kctl_2ch_orig[2];
	int kctl_4ch_orig[4];
	int kctl_8ch_orig[8];
};

struct rt715_sdw_stream_data {
	struct sdw_stream_runtime *sdw_stream;
};

struct rt715_sdca_kcontrol_private {
	unsigned int reg_base;
	unsigned int count;
	unsigned int max;
	unsigned int shift;
	unsigned int invert;
};

/* MIPI Register */
#define RT715_INT_CTRL					0x005a
#define RT715_INT_MASK					0x005e

/* NID */
#define RT715_AUDIO_FUNCTION_GROUP			0x01
#define RT715_MIC_ADC					0x07
#define RT715_LINE_ADC					0x08
#define RT715_MIX_ADC					0x09
#define RT715_DMIC1					0x12
#define RT715_DMIC2					0x13
#define RT715_MIC1					0x18
#define RT715_MIC2					0x19
#define RT715_LINE1					0x1a
#define RT715_LINE2					0x1b
#define RT715_DMIC3					0x1d
#define RT715_DMIC4					0x29
#define RT715_VENDOR_REG				0x20
#define RT715_MUX_IN1					0x22
#define RT715_MUX_IN2					0x23
#define RT715_MUX_IN3					0x24
#define RT715_MUX_IN4					0x25
#define RT715_MIX_ADC2					0x27
#define RT715_INLINE_CMD				0x55
#define RT715_VENDOR_HDA_CTL				0x61

/* Index (NID:20h) */
#define RT715_PRODUCT_NUM				0x0
#define RT715_IRQ_CTRL					0x2b
#define RT715_AD_FUNC_EN				0x36
#define RT715_REV_1					0x37
#define RT715_SDW_INPUT_SEL				0x39
#define RT715_EXT_DMIC_CLK_CTRL2			0x54

/* Index (NID:61h) */
#define RT715_HDA_LEGACY_MUX_CTL1			0x00

/* SDCA (Function) */
#define FUN_JACK_CODEC				0x01
#define FUN_MIC_ARRAY				0x02
#define FUN_HID						0x03
/* SDCA (Entity) */
#define RT715_SDCA_ST_EN							0x00
#define RT715_SDCA_CS_FREQ_IND_EN					0x01
#define RT715_SDCA_FU_ADC8_9_VOL					0x02
#define RT715_SDCA_SMPU_TRIG_ST_EN					0x05
#define RT715_SDCA_FU_ADC10_11_VOL					0x06
#define RT715_SDCA_FU_ADC7_27_VOL					0x0a
#define RT715_SDCA_FU_AMIC_GAIN_EN					0x0c
#define RT715_SDCA_FU_DMIC_GAIN_EN					0x0e
#define RT715_SDCA_CX_CLK_SEL_EN					0x10
#define RT715_SDCA_CREQ_POW_EN						0x18
/* SDCA (Control) */
#define RT715_SDCA_ST_CTRL							0x00
#define RT715_SDCA_CX_CLK_SEL_CTRL					0x01
#define RT715_SDCA_REQ_POW_CTRL					0x01
#define RT715_SDCA_FU_MUTE_CTRL					0x01
#define RT715_SDCA_FU_VOL_CTRL						0x02
#define RT715_SDCA_FU_DMIC_GAIN_CTRL				0x0b
#define RT715_SDCA_FREQ_IND_CTRL					0x10
#define RT715_SDCA_SMPU_TRIG_EN_CTRL				0x10
#define RT715_SDCA_SMPU_TRIG_ST_CTRL				0x11
/* SDCA (Channel) */
#define CH_00						0x00
#define CH_01						0x01
#define CH_02						0x02
#define CH_03						0x03
#define CH_04						0x04
#define CH_05						0x05
#define CH_06						0x06
#define CH_07						0x07
#define CH_08						0x08

#define RT715_SDCA_DB_STEP			375

enum {
	RT715_AIF1,
	RT715_AIF2,
};

int rt715_sdca_io_init(struct device *dev, struct sdw_slave *slave);
int rt715_sdca_init(struct device *dev, struct regmap *mbq_regmap,
	struct regmap *regmap, struct sdw_slave *slave);

#endif /* __RT715_SDCA_H__ */
