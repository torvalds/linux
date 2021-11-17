/*
 * ALSA SoC ES7202 pdm adc driver
 *
 * Author:      David Yang, <yangxiaohua@everest-semi.com>
 * Copyright:   (C) 2020 Everest Semiconductor Co Ltd.,
 *
 * Based on sound/soc/codecs/es7210.c by David Yang
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Notes:
 *  ES7202 is 2-ch ADC with PDM interface
 *
 */
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <linux/proc_fs.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/regmap.h>
#include "es7202.h"

//static struct snd_soc_codec *es7202_codec;
struct i2c_client *i2c_ctl[ADC_DEV_MAXNUM];
struct snd_soc_component *tron_component1[ADC_DEV_MAXNUM];
static int es7202_adc_num = 0;

/* codec private data */
struct es7202_priv {
	struct regmap *regmap;
	struct i2c_client *i2c;

	unsigned int pwr_vdd_voltage;
	struct regulator *vdd;
	int reset_gpio;
	bool reset_active_level;
};

static const struct reg_default es7202_reg_defaults[] = {
	{0x00, 0x10}, {0x01, 0x00}, {0x02, 0x04}, {0x03, 0x00},
	{0x04, 0x01}, {0x05, 0x18}, {0x06, 0x00}, {0x07, 0x30},
	{0x08, 0x02}, {0x10, 0xff}, {0x11, 0x0c}, {0x12, 0x55},
	{0x13, 0x55}, {0x14, 0x8c}, {0x15, 0x33}, {0x16, 0x33},
	{0x17, 0x33}, {0x18, 0x44}, {0x19, 0x00}, {0x1a, 0x00},
	{0x1b, 0x00}, {0x1c, 0xf8}, {0x1d, 0x18}, {0x1e, 0x18},
};

static int es7202_read(u8 reg, u8 * rt_value, struct i2c_client *client)
{
	int ret;
	u8 read_cmd[3] = { 0 };
	u8 cmd_len = 0;

	read_cmd[0] = reg;
	cmd_len = 1;

	if (client->adapter == NULL)
		printk("es7202_read client->adapter==NULL\n");

	ret = i2c_master_send(client, read_cmd, cmd_len);
	if (ret != cmd_len) {
		printk("es7202_read error1\n");
		return -1;
	}

	ret = i2c_master_recv(client, rt_value, 1);
	if (ret != 1) {
		printk("es7202_read error2, ret = %d.\n", ret);
		return -1;
	}

	return 0;
}

static int es7202_write(u8 reg, unsigned char value, struct i2c_client *client)
{
	int ret = 0;
	u8 write_cmd[2] = { 0 };

	write_cmd[0] = reg;
	write_cmd[1] = value;

	ret = i2c_master_send(client, write_cmd, 2);
	if (ret != 2) {
		printk("es7202_write error->[REG-0x%02x,val-0x%02x]\n",
		       reg, value);
		return -1;
	}

	return 0;
}

static int es7202_update_bits(u8 reg, u8 mask, u8 value,
			      struct i2c_client *client)
{
	u8 val_old = 0, val_new = 0;

	es7202_read(reg, &val_old, client);
	val_new = (val_old & ~mask) | (value & mask);
	if (val_new != val_old) {
		es7202_write(reg, val_new, client);
	}

	return 0;
}
#if 0
static int es7202_multi_chips_write(u8 reg, unsigned char value)
{
	u8 i;

	for (i = 0; i < ADC_DEV_MAXNUM; i++) {
		es7202_write(reg, value, i2c_ctl[i]);
	}

	return 0;
}
#endif
static int es7202_multi_chips_update_bits(u8 reg, u8 mask, u8 value)
{
	u8 i;

	for (i = 0; i < ADC_DEV_MAXNUM; i++) {
		es7202_update_bits(reg, mask, value, i2c_ctl[i]);
	}

	return 0;
}



static const DECLARE_TLV_DB_SCALE(mic_boost_tlv, 0, 300, 0);

#if ES7202_CHANNELS_MAX > 0
static int es7202_micboost1_setting_set(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1d, 0x0F,
		ucontrol->value.integer.value[0] & 0x0f, 
		i2c_ctl[0]);
	return 0;
}

static int es7202_micboost1_setting_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7202_read(0x1d, &val, i2c_ctl[0]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int es7202_micboost2_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1e,
			   0x0F,
			   ucontrol->value.integer.value[0] & 0x0f, i2c_ctl[0]);
	return 0;
}

static int es7202_micboost2_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7202_read(0x1e, &val, i2c_ctl[0]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}
#endif
#if ES7202_CHANNELS_MAX > 2
static int es7202_micboost3_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1d,
			   0x0F,
			   ucontrol->value.integer.value[0] & 0x0f, i2c_ctl[1]);
	return 0;
}

static int es7202_micboost3_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7202_read(0x1d, &val, i2c_ctl[1]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int es7202_micboost4_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1e,
			   0x0F,
			   ucontrol->value.integer.value[0] & 0x0f, i2c_ctl[1]);
	return 0;
}

static int es7202_micboost4_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7202_read(0x1e, &val, i2c_ctl[1]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}
#endif
#if ES7202_CHANNELS_MAX > 4
static int es7202_micboost5_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1d,
			   0x0F,
			   ucontrol->value.integer.value[0] & 0x0f, i2c_ctl[2]);
	return 0;
}

static int es7202_micboost5_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7202_read(0x1d, &val, i2c_ctl[2]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int es7202_micboost6_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1e,
			   0x0F,
			   ucontrol->value.integer.value[0] & 0x0f, i2c_ctl[2]);
	return 0;
}

static int es7202_micboost6_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7202_read(0x1e, &val, i2c_ctl[2]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}
#endif

#if ES7202_CHANNELS_MAX > 6
static int es7202_micboost7_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1d,
			   0x0F,
			   ucontrol->value.integer.value[0] & 0x0f, i2c_ctl[3]);
	return 0;
}

static int es7202_micboost7_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7202_read(0x1d, &val, i2c_ctl[3]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int es7202_micboost8_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1e,
			   0x0F,
			   ucontrol->value.integer.value[0] & 0x0f, i2c_ctl[3]);
	return 0;
}

static int es7202_micboost8_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7202_read(0x1e, &val, i2c_ctl[3]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}
#endif

#if ES7202_CHANNELS_MAX > 8
static int es7202_micboost9_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1d,
			   0x0F,
			   ucontrol->value.integer.value[0] & 0x0f, i2c_ctl[4]);
	return 0;
}

static int es7202_micboost9_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7202_read(0x1d, &val, i2c_ctl[4]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int es7202_micboost10_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1e,
			   0x0F,
			   ucontrol->value.integer.value[0] & 0x0f, i2c_ctl[4]);
	return 0;
}

static int es7202_micboost10_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7202_read(0x1e, &val, i2c_ctl[4]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}
#endif

#if ES7202_CHANNELS_MAX > 10
static int es7202_micboost11_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1d,
			   0x0F,
			   ucontrol->value.integer.value[0] & 0x0f, i2c_ctl[5]);
	return 0;
}

static int es7202_micboost11_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7202_read(0x1d, &val, i2c_ctl[5]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int es7202_micboost12_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1e,
			   0x0F,
			   ucontrol->value.integer.value[0] & 0x0f, i2c_ctl[5]);
	return 0;
}

static int es7202_micboost12_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7202_read(0x1e, &val, i2c_ctl[5]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}
#endif

#if ES7202_CHANNELS_MAX > 12
static int es7202_micboost13_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1d,
			   0x0F,
			   ucontrol->value.integer.value[0] & 0x0f, i2c_ctl[6]);
	return 0;
}

static int es7202_micboost13_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7202_read(0x1d, &val, i2c_ctl[6]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int es7202_micboost14_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1e,
			   0x0F,
			   ucontrol->value.integer.value[0] & 0x0f, i2c_ctl[6]);
	return 0;
}

static int es7202_micboost14_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7202_read(0x1e, &val, i2c_ctl[6]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}
#endif

#if ES7202_CHANNELS_MAX > 14
static int es7202_micboost15_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1d,
			   0x0F,
			   ucontrol->value.integer.value[0] & 0x0f, i2c_ctl[7]);
	return 0;
}

static int es7202_micboost15_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7202_read(0x1d, &val, i2c_ctl[7]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int es7202_micboost16_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7202_update_bits(0x1e,
			   0x0F,
			   ucontrol->value.integer.value[0] & 0x0f, i2c_ctl[7]);
	return 0;
}

static int es7202_micboost16_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7202_read(0x1e, &val, i2c_ctl[7]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}
#endif

static const struct snd_kcontrol_new es7202_snd_controls[] = {
#if ES7202_CHANNELS_MAX > 0
	SOC_SINGLE_EXT_TLV("PGA1_setting", 0x1D, 0, 0x0C, 0,
			   es7202_micboost1_setting_get,
			   es7202_micboost1_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA2_setting", 0x1E, 0, 0x0C, 0,
			   es7202_micboost2_setting_get,
			   es7202_micboost2_setting_set,
			   mic_boost_tlv),
#endif
#if ES7202_CHANNELS_MAX > 2
	SOC_SINGLE_EXT_TLV("PGA3_setting", 0x1D, 0, 0x0C, 0,
			   es7202_micboost3_setting_get,
			   es7202_micboost3_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA4_setting", 0x1E, 0, 0x0C, 0,
			   es7202_micboost4_setting_get,
			   es7202_micboost4_setting_set,
			   mic_boost_tlv),
#endif
#if ES7202_CHANNELS_MAX > 4
	SOC_SINGLE_EXT_TLV("PGA5_setting", 0x1D, 0, 0x0C, 0,
			   es7202_micboost5_setting_get,
			   es7202_micboost5_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA6_setting", 0x1E, 0, 0x0C, 0,
			   es7202_micboost6_setting_get,
			   es7202_micboost6_setting_set,
			   mic_boost_tlv),
#endif
#if ES7202_CHANNELS_MAX > 6
	SOC_SINGLE_EXT_TLV("PGA7_setting", 0x1D, 0, 0x0C, 0,
			   es7202_micboost7_setting_get,
			   es7202_micboost7_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA8_setting", 0x1E, 0, 0x0C, 0,
			   es7202_micboost8_setting_get,
			   es7202_micboost8_setting_set,
			   mic_boost_tlv),
#endif
#if ES7202_CHANNELS_MAX > 8
	SOC_SINGLE_EXT_TLV("PGA9_setting", 0x1D, 0, 0x0C, 0,
			   es7202_micboost9_setting_get,
			   es7202_micboost9_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA10_setting", 0x1E, 0, 0x0C, 0,
			   es7202_micboost10_setting_get,
			   es7202_micboost10_setting_set,
			   mic_boost_tlv),
#endif
#if ES7202_CHANNELS_MAX > 10
	SOC_SINGLE_EXT_TLV("PGA11_setting", 0x1D, 0, 0x0C, 0,
			   es7202_micboost11_setting_get,
			   es7202_micboost11_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA12_setting", 0x1E, 0, 0x0C, 0,
			   es7202_micboost12_setting_get,
			   es7202_micboost12_setting_set,
			   mic_boost_tlv),
#endif
#if ES7202_CHANNELS_MAX > 12
	SOC_SINGLE_EXT_TLV("PGA13_setting", 0x1D, 0, 0x0C, 0,
			   es7202_micboost13_setting_get,
			   es7202_micboost13_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA14_setting", 0x1E, 0, 0x0C, 0,
			   es7202_micboost14_setting_get,
			   es7202_micboost14_setting_set,
			   mic_boost_tlv),
#endif
#if ES7202_CHANNELS_MAX > 14
	SOC_SINGLE_EXT_TLV("PGA15_setting", 0x1D, 0, 0x0C, 0,
			   es7202_micboost15_setting_get,
			   es7202_micboost15_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA16_setting", 0x1E, 0, 0x0C, 0,
			   es7202_micboost16_setting_get,
			   es7202_micboost16_setting_set,
			   mic_boost_tlv),
#endif
};

static int es7202_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	if (mute) {
		es7202_multi_chips_update_bits(ES7202_PDM_INF_CTL_REG07, 0x03,0x03);
	} else {
		es7202_multi_chips_update_bits(ES7202_PDM_INF_CTL_REG07, 0x03,0x00);
	}

	return 0;
}

#define es7202_RATES SNDRV_PCM_RATE_8000_96000

static struct snd_soc_dai_ops es7202_ops = {
	.mute_stream = es7202_mute,
};
#if ES7202_CHANNELS_MAX > 0
static struct snd_soc_dai_driver es7202_dai0 = {
	.name = "es7202 pdm 0",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = es7202_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &es7202_ops,
	.symmetric_rates = 1,
};
#endif
#if ES7202_CHANNELS_MAX > 2
static struct snd_soc_dai_driver es7202_dai1 = {
	.name = "es7202 pdm 1",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = es7202_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &es7202_ops,
	.symmetric_rates = 1,
};
#endif
#if ES7202_CHANNELS_MAX > 4
static struct snd_soc_dai_driver es7202_dai2 = {
	.name = "es7202 pdm 2",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = es7202_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &es7202_ops,
	.symmetric_rates = 1,
};
#endif
#if ES7202_CHANNELS_MAX > 6
static struct snd_soc_dai_driver es7202_dai3 = {
	.name = "es7202 pdm 3",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = es7202_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &es7202_ops,
	.symmetric_rates = 1,
};
#endif
#if ES7202_CHANNELS_MAX > 8
static struct snd_soc_dai_driver es7202_dai4 = {
	.name = "es7202 pdm 4",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 10,
		.rates = es7202_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &es7202_ops,
	.symmetric_rates = 1,
};
#endif
#if ES7202_CHANNELS_MAX > 10
static struct snd_soc_dai_driver es7202_dai5 = {
	.name = "es7202 pdm 5",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 12,
		.rates = es7202_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &es7202_ops,
	.symmetric_rates = 1,
};
#endif
#if ES7202_CHANNELS_MAX > 12
static struct snd_soc_dai_driver es7202_dai6 = {
	.name = "es7202 pdm 6",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 14,
		.rates = es7202_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &es7202_ops,
	.symmetric_rates = 1,
};
#endif
#if ES7202_CHANNELS_MAX > 14
static struct snd_soc_dai_driver es7202_dai7 = {
	.name = "es7202 pdm 7",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 16,
		.rates = es7202_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &es7202_ops,
	.symmetric_rates = 1,
};
#endif

static struct snd_soc_dai_driver *es7202_dai[] = {
#if ES7202_CHANNELS_MAX > 0
        &es7202_dai0,
#endif
#if ES7202_CHANNELS_MAX > 2
        &es7202_dai1,
#endif
#if ES7202_CHANNELS_MAX > 4
        &es7202_dai2,
#endif
#if ES7202_CHANNELS_MAX > 6
        &es7202_dai3,
#endif
#if ES7202_CHANNELS_MAX > 8
        &es7202_dai4,
#endif
#if ES7202_CHANNELS_MAX > 10
        &es7202_dai5,
#endif
#if ES7202_CHANNELS_MAX > 12
        &es7202_dai6,
#endif
#if ES7202_CHANNELS_MAX > 14
        &es7202_dai7,
#endif
};

static int es7202_suspend(struct snd_soc_component *component)
{
	es7202_multi_chips_update_bits(ES7202_PDM_INF_CTL_REG07, 0x03,0x03);
	msleep(50);
	return 0;
}

static int es7202_resume(struct snd_soc_component *component)
{	
	msleep(50);
	es7202_multi_chips_update_bits(ES7202_PDM_INF_CTL_REG07, 0x03,0x00);
	return 0;
}

static int es7202_probe(struct snd_soc_component *component)
{
	struct es7202_priv *es7202 = snd_soc_component_get_drvdata(component);
	int cnt;
	int ret = 0;
	printk("enter into %s()\n", __func__);
	tron_component1[es7202_adc_num++] = component;

	for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
		es7202_write(ES7202_SOFT_MODE_REG01, 0x01, i2c_ctl[cnt]);
		switch(es7202->pwr_vdd_voltage) {
		case VDD_3V3:
			es7202_write(ES7202_ANALOG_MISC1_REG1B, 0x50, i2c_ctl[cnt]);
			es7202_write(ES7202_PGA1_REG1D, 0x1b, i2c_ctl[cnt]);
			es7202_write(ES7202_PGA2_REG1E, 0x1b, i2c_ctl[cnt]);
			es7202_write(ES7202_ANALOG_EN_REG10, 0x7F, i2c_ctl[cnt]);
			es7202_write(ES7202_BIAS_VMID_REG11, 0x2F, i2c_ctl[cnt]);
			es7202_write(ES7202_ANALOG_EN_REG10, 0x0F, i2c_ctl[cnt]);
			es7202_write(ES7202_ANALOG_EN_REG10, 0x00, i2c_ctl[cnt]);
			break;
		default:
		case VDD_1V8:
			es7202_write(ES7202_ANALOG_MISC1_REG1B, 0x40, i2c_ctl[cnt]);
			es7202_write(ES7202_PGA1_REG1D, 0x1b, i2c_ctl[cnt]);
			es7202_write(ES7202_PGA2_REG1E, 0x1b, i2c_ctl[cnt]);
			es7202_write(ES7202_ANALOG_EN_REG10, 0x7F, i2c_ctl[cnt]);
			es7202_write(ES7202_BIAS_VMID_REG11, 0x2F, i2c_ctl[cnt]);
			es7202_write(ES7202_ANALOG_EN_REG10, 0x3F, i2c_ctl[cnt]);
			es7202_write(ES7202_ANALOG_EN_REG10, 0x00, i2c_ctl[cnt]);
			break;	
		}
		es7202_write(ES7202_MOD1_BIAS_REG14, 0x58, i2c_ctl[cnt]);
		es7202_write(ES7202_CLK_DIV_REG02, 0x01, i2c_ctl[cnt]);
		es7202_write(ES7202_T2_VMID_REG05, 0x01, i2c_ctl[cnt]);
		es7202_write(ES7202_MISC_CTL_REG08, 0x02, i2c_ctl[cnt]);
		es7202_write(ES7202_RESET_REG00, 0x01, i2c_ctl[cnt]);
		es7202_write(ES7202_CLK_EN_REG03, 0x03, i2c_ctl[cnt]);
		es7202_write(ES7202_BIAS_VMID_REG11, 0x2E, i2c_ctl[cnt]);

		es7202_multi_chips_update_bits(ES7202_PDM_INF_CTL_REG07, 0x03, 0x00);
	}
	return ret;
}

static void es7202_remove(struct snd_soc_component *component)
{
	es7202_multi_chips_update_bits(ES7202_PDM_INF_CTL_REG07, 0x03,0x03);
	msleep(50);
}

const struct regmap_config es7202_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= 0xff,
	.cache_type	= REGCACHE_RBTREE,
	.reg_defaults = es7202_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(es7202_reg_defaults),
};

static struct snd_soc_component_driver soc_codec_dev_es7202 = {
	.probe =	es7202_probe,
	.remove =	es7202_remove,
	.suspend =	es7202_suspend,
	.resume =	es7202_resume,
	
	
	.controls = es7202_snd_controls,
	.num_controls = ARRAY_SIZE(es7202_snd_controls),
	.idle_bias_on = 1,
	.use_pmdown_time = 1,
	.endianness = 1,
	.non_legacy_dai_naming = 1,
};

static ssize_t es7202_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val=0, flag=0;
	u8 i=0, reg, num, value_w, value_r;
	
	struct es7202_priv *es7202 = dev_get_drvdata(dev);
	val = simple_strtol(buf, NULL, 16);
	flag = (val >> 16) & 0xFF;
	
	if (flag) {
		reg = (val >> 8) & 0xFF;
		value_w = val & 0xFF;
		printk("\nWrite: start REG:0x%02x,val:0x%02x,count:0x%02x\n", reg, value_w, flag);
		while(flag--) {
			es7202_write(reg, value_w,  es7202->i2c);
			printk("Write 0x%02x to REG:0x%02x\n", value_w, reg);
			reg++;
		}
	} else {
		reg = (val >> 8) & 0xFF;
		num = val & 0xff;
		printk("\nRead: start REG:0x%02x,count:0x%02x\n", reg, num);
		do {
			value_r = 0;
			es7202_read(reg, &value_r, es7202->i2c);
			printk("REG[0x%02x]: 0x%02x;  ", reg, value_r);
			reg++;
			i++;
			if ((i==num) || (i%4==0))	printk("\n");
		} while (i<num);
	}
	
	return count;
}

static ssize_t es7202_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk("echo flag|reg|val > es7202\n");
	printk("eg read star addres=0x06,count 0x10:echo 0610 >es7202\n");
	printk("eg write star addres=0x90,value=0x3c,count=4:echo 4903c >es7202\n");
	return 0;
}

static DEVICE_ATTR(es7202, 0644, es7202_show, es7202_store);

static struct attribute *es7202_debug_attrs[] = {
	&dev_attr_es7202.attr,
	NULL,
};

static struct attribute_group es7202_debug_attr_group = {
	.name   = "es7202_debug",
	.attrs  = es7202_debug_attrs,
};

static int es7202_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct es7202_priv *es7202;
	int uV;
	int ret = -1;

	dev_info(&i2c->dev, "probe\n");
	es7202 = devm_kzalloc(&i2c->dev, sizeof(*es7202), GFP_KERNEL);
	if (!es7202)
		return -ENOMEM;
	es7202->i2c = i2c;
	es7202->vdd = devm_regulator_get_optional(&i2c->dev, "power");
	if (IS_ERR(es7202->vdd)) {
		if (PTR_ERR(es7202->vdd) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_warn(&i2c->dev, "power-supply get fail, use 3v3 as default\n");
		es7202->pwr_vdd_voltage = VDD_3V3;
	} else {
		uV = regulator_get_voltage(es7202->vdd);
		dev_info(&i2c->dev, "probe power-supply %duV\n", uV);
		if (uV <= MAX_VOLTAGE_1_8)
			es7202->pwr_vdd_voltage = VDD_1V8;
		else
			es7202->pwr_vdd_voltage = VDD_3V3;
	}
	dev_set_drvdata(&i2c->dev, es7202);
	if (id->driver_data < ADC_DEV_MAXNUM) {
		i2c_ctl[id->driver_data] = i2c;
		dev_info(&i2c->dev, "probe reigister es7202 dai(%s) component\n",
			 es7202_dai[id->driver_data]->name);
		ret = devm_snd_soc_register_component(&i2c->dev, &soc_codec_dev_es7202,
						      es7202_dai[id->driver_data], 1);
		if (ret < 0) {
			return ret;
		}
	}
	ret = sysfs_create_group(&i2c->dev.kobj, &es7202_debug_attr_group);
	if (ret) {
		dev_err(&i2c->dev, "failed to create attr group\n");
	}
	return ret;
}

static  int es7202_i2c_remove(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &es7202_debug_attr_group);

	return 0;
}

static void es7202_i2c_shutdown(struct i2c_client *client)
{
	es7202_multi_chips_update_bits(ES7202_PDM_INF_CTL_REG07, 0x03,0x03);
	msleep(50);
}

#if !ES7202_MATCH_DTS_EN
static int es7202_i2c_detect(struct i2c_client *client,
			     struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;

	if (adapter->nr == ES7202_I2C_BUS_NUM) {
		if (client->addr == 0x30) {
			strlcpy(info->type, "ES7202_PDM_ADC_1", I2C_NAME_SIZE);
			return 0;
		} else if (client->addr == 0x31) {
			strlcpy(info->type, "ES7202_PDM_ADC_2", I2C_NAME_SIZE);
			return 0;
		} else if (client->addr == 0x32) {
			strlcpy(info->type, "ES7202_PDM_ADC_3", I2C_NAME_SIZE);
			return 0;
		} else if (client->addr == 0x33) {
			strlcpy(info->type, "ES7202_PDM_ADC_4", I2C_NAME_SIZE);
			return 0;
		}else if (client->addr == 0x34) {
			strlcpy(info->type, "ES7202_PDM_ADC_5", I2C_NAME_SIZE);
			return 0;
		}else if (client->addr == 0x35) {
			strlcpy(info->type, "ES7202_PDM_ADC_6", I2C_NAME_SIZE);
			return 0;
		}else if (client->addr == 0x36) {
			strlcpy(info->type, "ES7202_PDM_ADC_7", I2C_NAME_SIZE);
			return 0;
		}else if (client->addr == 0x37) {
			strlcpy(info->type, "ES7202_PDM_ADC_8", I2C_NAME_SIZE);
			return 0;
		}
	}

	return -ENODEV;
}

static const unsigned short es7202_i2c_addr[] = {
#if ES7202_CHANNELS_MAX > 0
	0x30,
#endif

#if ES7202_CHANNELS_MAX > 2
	0x31,
#endif

#if ES7202_CHANNELS_MAX > 4
	0x32,
#endif

#if ES7202_CHANNELS_MAX > 6
	0x33,
#endif

#if ES7202_CHANNELS_MAX > 8
	0x34,
#endif

#if ES7202_CHANNELS_MAX > 10
	0x35,
#endif

#if ES7202_CHANNELS_MAX > 12
	0x36,
#endif

#if ES7202_CHANNELS_MAX > 14
	0x37,
#endif
	I2C_CLIENT_END,
};
#endif

#if ES7202_MATCH_DTS_EN
/*
* device tree source or i2c_board_info both use to 
* transfer hardware information to linux kernel, 
* use one of them wil be OK
*/
#if 0
static struct i2c_board_info es7202_i2c_board_info[] = {
#if ES7202_CHANNELS_MAX > 0
	{I2C_BOARD_INFO("ES7202_PDM_ADC_1", 0x30),},
#endif

#if ES7202_CHANNELS_MAX > 2
	{I2C_BOARD_INFO("ES7202_PDM_ADC_2", 0x31),},
#endif

#if ES7202_CHANNELS_MAX > 4
	{I2C_BOARD_INFO("ES7202_PDM_ADC_3", 0x32),},
#endif

#if ES7202_CHANNELS_MAX > 6
	{I2C_BOARD_INFO("ES7202_PDM_ADC_4", 0x33),},
#endif

#if ES7202_CHANNELS_MAX > 8
	{I2C_BOARD_INFO("ES7202_PDM_ADC_5", 0x34),},
#endif

#if ES7202_CHANNELS_MAX > 10
	{I2C_BOARD_INFO("ES7202_PDM_ADC_6", 0x35),},
#endif

#if ES7202_CHANNELS_MAX > 12
	{I2C_BOARD_INFO("ES7202_PDM_ADC_7", 0x36),},
#endif

#if ES7202_CHANNELS_MAX > 14
	{I2C_BOARD_INFO("ES7202_PDM_ADC_8", 0x37),},
#endif
};
#endif
static const struct of_device_id es7202_dt_ids[] = {
#if ES7202_CHANNELS_MAX > 0
	{.compatible = "ES7202_PDM_ADC_1",},
#endif

#if ES7202_CHANNELS_MAX > 2
	{.compatible = "ES7202_PDM_ADC_2",},
#endif

#if ES7202_CHANNELS_MAX > 4
	{.compatible = "ES7202_PDM_ADC_3",},
#endif

#if ES7202_CHANNELS_MAX > 6
	{.compatible = "ES7202_PDM_ADC_4",},
#endif

#if ES7202_CHANNELS_MAX > 8
	{.compatible = "ES7202_PDM_ADC_5",},
#endif

#if ES7202_CHANNELS_MAX > 10
	{.compatible = "ES7202_PDM_ADC_6",},
#endif

#if ES7202_CHANNELS_MAX > 12
	{.compatible = "ES7202_PDM_ADC_7",},
#endif

#if ES7202_CHANNELS_MAX > 14
	{.compatible = "ES7202_PDM_ADC_8",},
#endif
	{}
};
#endif

static const struct i2c_device_id es7202_i2c_id[] = {
#if ES7202_CHANNELS_MAX > 0
	{"ES7202_PDM_ADC_1", 0},
#endif

#if ES7202_CHANNELS_MAX > 2
	{"ES7202_PDM_ADC_2", 1},
#endif

#if ES7202_CHANNELS_MAX > 4
	{"ES7202_PDM_ADC_3", 2},
#endif

#if ES7202_CHANNELS_MAX > 6
	{"ES7202_PDM_ADC_4", 3},
#endif

#if ES7202_CHANNELS_MAX > 8
	{"ES7202_PDM_ADC_5", 4},
#endif

#if ES7202_CHANNELS_MAX > 10
	{"ES7202_PDM_ADC_6", 5},
#endif

#if ES7202_CHANNELS_MAX > 12
	{"ES7202_PDM_ADC_7", 6},
#endif

#if ES7202_CHANNELS_MAX > 14
	{"ES7202_PDM_ADC_8", 7},
#endif
	{}
};

static struct i2c_driver es7202_i2c_driver = {
	.driver = {
		.name		= "es7202",
#if ES7202_MATCH_DTS_EN
		   .of_match_table = es7202_dt_ids,
#endif
	},
	.probe    = es7202_i2c_probe,
	.remove   = es7202_i2c_remove,
	.shutdown = es7202_i2c_shutdown,
	.class = I2C_CLASS_HWMON,
	.id_table = es7202_i2c_id,
#if !ES7202_MATCH_DTS_EN
	.address_list = es7202_i2c_addr,
	.detect = es7202_i2c_detect,
#endif	
};

static int __init es7202_modinit(void)
{
	int ret;
//#if ES7202_MATCH_DTS_EN
#if 0
	int i;
	struct i2c_adapter *adapter;
	struct i2c_client *client;
#endif

//#if ES7202_MATCH_DTS_EN
#if 0
/*
* Notes:
* if the device has been declared in DTS tree,
* here don't need to create new i2c device with i2c_board_info.
*/
	adapter = i2c_get_adapter(ES7202_I2C_BUS_NUM);
	if (!adapter) {
		printk("i2c_get_adapter() fail!\n");
		return -ENODEV;
	}
	printk("%s() begin0000", __func__);

	for (i = 0; i < ADC_DEV_MAXNUM; i++) {
		client = i2c_new_device(adapter, &es7202_i2c_board_info[i]);
		printk("%s() i2c_new_device\n", __func__);
		if (!client)
			return -ENODEV;
	}
	i2c_put_adapter(adapter);
#endif
	ret = i2c_add_driver(&es7202_i2c_driver);
	if (ret != 0)
		printk("Failed to register es7202 i2c driver : %d \n", ret);
	return ret;
}

late_initcall(es7202_modinit);
//module_init(es7202_modinit);
static void __exit es7202_exit(void)
{
	i2c_del_driver(&es7202_i2c_driver);
}

module_exit(es7202_exit);

MODULE_DESCRIPTION("ASoC es7202 pdm adc driver");
MODULE_AUTHOR(" David Yang, <yangxiaohua@everest-semi.com>>");
MODULE_LICENSE("GPL v2");
