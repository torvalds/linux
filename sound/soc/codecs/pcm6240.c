// SPDX-License-Identifier: GPL-2.0
//
// ALSA SoC Texas Instruments PCM6240 Family Audio ADC/DAC Device
//
// Copyright (C) 2022 - 2024 Texas Instruments Incorporated
// https://www.ti.com
//
// The PCM6240 driver implements a flexible and configurable
// algo coefficient setting for one, two, or even multiple
// PCM6240 Family chips.
//
// Author: Shenghao Ding <shenghao-ding@ti.com>
//

#include <linux/unaligned.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "pcm6240.h"

static const struct i2c_device_id pcmdevice_i2c_id[] = {
	{ "adc3120",  ADC3120  },
	{ "adc5120",  ADC5120  },
	{ "adc6120",  ADC6120  },
	{ "dix4192",  DIX4192  },
	{ "pcm1690",  PCM1690  },
	{ "pcm3120",  PCM3120  },
	{ "pcm3140",  PCM3140  },
	{ "pcm5120",  PCM5120  },
	{ "pcm5140",  PCM5140  },
	{ "pcm6120",  PCM6120  },
	{ "pcm6140",  PCM6140  },
	{ "pcm6240",  PCM6240  },
	{ "pcm6260",  PCM6260  },
	{ "pcm9211",  PCM9211  },
	{ "pcmd3140", PCMD3140 },
	{ "pcmd3180", PCMD3180 },
	{ "pcmd512x", PCMD512X },
	{ "taa5212",  TAA5212  },
	{ "taa5412",  TAA5412  },
	{ "tad5212",  TAD5212  },
	{ "tad5412",  TAD5412  },
	{}
};
MODULE_DEVICE_TABLE(i2c, pcmdevice_i2c_id);

static const char *const pcmdev_ctrl_name[] = {
	"%s i2c%d Dev%d Ch%d Ana Volume",
	"%s i2c%d Dev%d Ch%d Digi Volume",
	"%s i2c%d Dev%d Ch%d Fine Volume",
};

static const struct pcmdevice_mixer_control adc5120_analog_gain_ctl[] = {
	{
		.shift = 1,
		.reg = ADC5120_REG_CH1_ANALOG_GAIN,
		.max = 0x54,
		.invert = 0,
	},
	{
		.shift = 1,
		.reg = ADC5120_REG_CH2_ANALOG_GAIN,
		.max = 0x54,
		.invert = 0,
	}
};

static const struct pcmdevice_mixer_control adc5120_digi_gain_ctl[] = {
	{
		.shift = 0,
		.reg = ADC5120_REG_CH1_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = ADC5120_REG_CH2_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	}
};

static const struct pcmdevice_mixer_control pcm1690_digi_gain_ctl[] = {
	{
		.shift = 0,
		.reg = PCM1690_REG_CH1_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM1690_REG_CH2_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM1690_REG_CH3_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM1690_REG_CH4_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM1690_REG_CH5_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM1690_REG_CH6_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM1690_REG_CH7_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM1690_REG_CH8_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	}
};

static const struct pcmdevice_mixer_control pcm6240_analog_gain_ctl[] = {
	{
		.shift = 2,
		.reg = PCM6240_REG_CH1_ANALOG_GAIN,
		.max = 0x42,
		.invert = 0,
	},
	{
		.shift = 2,
		.reg = PCM6240_REG_CH2_ANALOG_GAIN,
		.max = 0x42,
		.invert = 0,
	},
	{
		.shift = 2,
		.reg = PCM6240_REG_CH3_ANALOG_GAIN,
		.max = 0x42,
		.invert = 0,
	},
	{
		.shift = 2,
		.reg = PCM6240_REG_CH4_ANALOG_GAIN,
		.max = 0x42,
		.invert = 0,
	}
};

static const struct pcmdevice_mixer_control pcm6240_digi_gain_ctl[] = {
	{
		.shift = 0,
		.reg = PCM6240_REG_CH1_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM6240_REG_CH2_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM6240_REG_CH3_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM6240_REG_CH4_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	}
};

static const struct pcmdevice_mixer_control pcm6260_analog_gain_ctl[] = {
	{
		.shift = 2,
		.reg = PCM6260_REG_CH1_ANALOG_GAIN,
		.max = 0x42,
		.invert = 0,
	},
	{
		.shift = 2,
		.reg = PCM6260_REG_CH2_ANALOG_GAIN,
		.max = 0x42,
		.invert = 0,
	},
	{
		.shift = 2,
		.reg = PCM6260_REG_CH3_ANALOG_GAIN,
		.max = 0x42,
		.invert = 0,
	},
	{
		.shift = 2,
		.reg = PCM6260_REG_CH4_ANALOG_GAIN,
		.max = 0x42,
		.invert = 0,
	},
	{
		.shift = 2,
		.reg = PCM6260_REG_CH5_ANALOG_GAIN,
		.max = 0x42,
		.invert = 0,
	},
	{
		.shift = 2,
		.reg = PCM6260_REG_CH6_ANALOG_GAIN,
		.max = 0x42,
		.invert = 0,
	}
};

static const struct pcmdevice_mixer_control pcm6260_digi_gain_ctl[] = {
	{
		.shift = 0,
		.reg = PCM6260_REG_CH1_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM6260_REG_CH2_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM6260_REG_CH3_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM6260_REG_CH4_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM6260_REG_CH5_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM6260_REG_CH6_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	}
};

static const struct pcmdevice_mixer_control pcm9211_digi_gain_ctl[] = {
	{
		.shift = 0,
		.reg = PCM9211_REG_CH1_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCM9211_REG_CH2_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	}
};

static const struct pcmdevice_mixer_control pcmd3140_digi_gain_ctl[] = {
	{
		.shift = 0,
		.reg = PCMD3140_REG_CH1_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCMD3140_REG_CH2_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCMD3140_REG_CH3_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCMD3140_REG_CH4_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	}
};

static const struct pcmdevice_mixer_control pcmd3140_fine_gain_ctl[] = {
	{
		.shift = 4,
		.reg = PCMD3140_REG_CH1_FINE_GAIN,
		.max = 0xf,
		.invert = 0,
	},
	{
		.shift = 4,
		.reg = PCMD3140_REG_CH2_FINE_GAIN,
		.max = 0xf,
		.invert = 0,
	},
	{
		.shift = 4,
		.reg = PCMD3140_REG_CH3_FINE_GAIN,
		.max = 0xf,
		.invert = 0,
	},
	{
		.shift = 4,
		.reg = PCMD3140_REG_CH4_FINE_GAIN,
		.max = 0xf,
		.invert = 0,
	}
};

static const struct pcmdevice_mixer_control pcmd3180_digi_gain_ctl[] = {
	{
		.shift = 0,
		.reg = PCMD3180_REG_CH1_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCMD3180_REG_CH2_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCMD3180_REG_CH3_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCMD3180_REG_CH4_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCMD3180_REG_CH5_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCMD3180_REG_CH6_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCMD3180_REG_CH7_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = PCMD3180_REG_CH8_DIGITAL_GAIN,
		.max = 0xff,
		.invert = 0,
	}
};

static const struct pcmdevice_mixer_control pcmd3180_fine_gain_ctl[] = {
	{
		.shift = 4,
		.reg = PCMD3180_REG_CH1_FINE_GAIN,
		.max = 0xf,
		.invert = 0,
	},
	{
		.shift = 4,
		.reg = PCMD3180_REG_CH2_FINE_GAIN,
		.max = 0xf,
		.invert = 0,
	},
	{
		.shift = 4,
		.reg = PCMD3180_REG_CH3_FINE_GAIN,
		.max = 0xf,
		.invert = 0,
	},
	{
		.shift = 4,
		.reg = PCMD3180_REG_CH4_FINE_GAIN,
		.max = 0xf,
		.invert = 0,
	},
	{
		.shift = 4,
		.reg = PCMD3180_REG_CH5_FINE_GAIN,
		.max = 0xf,
		.invert = 0,
	},
	{
		.shift = 4,
		.reg = PCMD3180_REG_CH6_FINE_GAIN,
		.max = 0xf,
		.invert = 0,
	},
	{
		.shift = 4,
		.reg = PCMD3180_REG_CH7_FINE_GAIN,
		.max = 0xf,
		.invert = 0,
	},
	{
		.shift = 4,
		.reg = PCMD3180_REG_CH8_FINE_GAIN,
		.max = 0xf,
		.invert = 0,
	}
};

static const struct pcmdevice_mixer_control taa5412_digi_vol_ctl[] = {
	{
		.shift = 0,
		.reg = TAA5412_REG_CH1_DIGITAL_VOLUME,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = TAA5412_REG_CH2_DIGITAL_VOLUME,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = TAA5412_REG_CH3_DIGITAL_VOLUME,
		.max = 0xff,
		.invert = 0,
	},
	{
		.shift = 0,
		.reg = TAA5412_REG_CH4_DIGITAL_VOLUME,
		.max = 0xff,
		.invert = 0,
	}
};

static const struct pcmdevice_mixer_control taa5412_fine_gain_ctl[] = {
	{
		.shift = 4,
		.reg = TAA5412_REG_CH1_FINE_GAIN,
		.max = 0xf,
		.invert = 0,
	},
	{
		.shift = 4,
		.reg = TAA5412_REG_CH2_FINE_GAIN,
		.max = 0xf,
		.invert = 0,
	},
	{
		.shift = 4,
		.reg = TAA5412_REG_CH3_FINE_GAIN,
		.max = 0xf,
		.invert = 4,
	},
	{
		.shift = 0,
		.reg = TAA5412_REG_CH4_FINE_GAIN,
		.max = 0xf,
		.invert = 4,
	}
};

static const DECLARE_TLV_DB_MINMAX_MUTE(pcmd3140_dig_gain_tlv,
	-10000, 2700);
static const DECLARE_TLV_DB_MINMAX_MUTE(pcm1690_fine_dig_gain_tlv,
	-12750, 0);
static const DECLARE_TLV_DB_MINMAX_MUTE(pcm1690_dig_gain_tlv,
	-25500, 0);
static const DECLARE_TLV_DB_MINMAX_MUTE(pcm9211_dig_gain_tlv,
	-11450, 2000);
static const DECLARE_TLV_DB_MINMAX_MUTE(adc5120_fgain_tlv,
	-10050, 2700);
static const DECLARE_TLV_DB_LINEAR(adc5120_chgain_tlv, 0, 4200);
static const DECLARE_TLV_DB_MINMAX_MUTE(pcm6260_fgain_tlv,
	-10000, 2700);
static const DECLARE_TLV_DB_LINEAR(pcm6260_chgain_tlv, 0, 4200);
static const DECLARE_TLV_DB_MINMAX_MUTE(taa5412_dig_vol_tlv,
	-8050, 4700);
static const DECLARE_TLV_DB_LINEAR(taa5412_fine_gain_tlv,
	-80, 70);

static int pcmdev_change_dev(struct pcmdevice_priv *pcm_priv,
	unsigned short dev_no)
{
	struct i2c_client *client = (struct i2c_client *)pcm_priv->client;
	struct regmap *map = pcm_priv->regmap;
	int ret;

	if (client->addr == pcm_priv->addr[dev_no])
		return 0;

	client->addr = pcm_priv->addr[dev_no];
	/* All pcmdevices share the same regmap, clear the page
	 * inside regmap once switching to another pcmdevice.
	 * Register 0 at any pages inside pcmdevice is the same
	 * one for page-switching.
	 */
	ret = regmap_write(map, PCMDEVICE_PAGE_SELECT, 0);
	if (ret < 0)
		dev_err(pcm_priv->dev, "%s: err = %d\n", __func__, ret);

	return ret;
}

static int pcmdev_dev_read(struct pcmdevice_priv *pcm_dev,
	unsigned int dev_no, unsigned int reg, unsigned int *val)
{
	struct regmap *map = pcm_dev->regmap;
	int ret;

	if (dev_no >= pcm_dev->ndev) {
		dev_err(pcm_dev->dev, "%s: no such channel(%d)\n", __func__,
			dev_no);
		return -EINVAL;
	}

	ret = pcmdev_change_dev(pcm_dev, dev_no);
	if (ret < 0) {
		dev_err(pcm_dev->dev, "%s: chg dev err = %d\n", __func__, ret);
		return ret;
	}

	ret = regmap_read(map, reg, val);
	if (ret < 0)
		dev_err(pcm_dev->dev, "%s: err = %d\n", __func__, ret);

	return ret;
}

static int pcmdev_dev_update_bits(struct pcmdevice_priv *pcm_dev,
	unsigned int dev_no, unsigned int reg, unsigned int mask,
	unsigned int value)
{
	struct regmap *map = pcm_dev->regmap;
	int ret;

	if (dev_no >= pcm_dev->ndev) {
		dev_err(pcm_dev->dev, "%s: no such channel(%d)\n", __func__,
			dev_no);
		return -EINVAL;
	}

	ret = pcmdev_change_dev(pcm_dev, dev_no);
	if (ret < 0) {
		dev_err(pcm_dev->dev, "%s: chg dev err = %d\n", __func__, ret);
		return ret;
	}

	ret = regmap_update_bits(map, reg, mask, value);
	if (ret < 0)
		dev_err(pcm_dev->dev, "%s: update_bits err=%d\n",
			__func__, ret);

	return ret;
}

static int pcmdev_get_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol, int vol_ctrl_type)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct pcmdevice_priv *pcm_dev =
		snd_soc_component_get_drvdata(component);
	struct pcmdevice_mixer_control *mc =
		(struct pcmdevice_mixer_control *)kcontrol->private_value;
	int max = mc->max, ret;
	unsigned int mask = BIT(fls(max)) - 1;
	unsigned int dev_no = mc->dev_no;
	unsigned int shift = mc->shift;
	unsigned int reg = mc->reg;
	unsigned int val;

	mutex_lock(&pcm_dev->codec_lock);

	if (pcm_dev->chip_id == PCM1690) {
		ret = pcmdev_dev_read(pcm_dev, dev_no, PCM1690_REG_MODE_CTRL,
			&val);
		if (ret) {
			dev_err(pcm_dev->dev, "%s: read mode err=%d\n",
				__func__, ret);
			goto out;
		}
		val &= PCM1690_REG_MODE_CTRL_DAMS_MSK;
		/* Set to wide-range mode, before using vol ctrl. */
		if (!val && vol_ctrl_type == PCMDEV_PCM1690_VOL_CTRL) {
			ucontrol->value.integer.value[0] = -25500;
			goto out;
		}
		/* Set to fine mode, before using fine vol ctrl. */
		if (val && vol_ctrl_type == PCMDEV_PCM1690_FINE_VOL_CTRL) {
			ucontrol->value.integer.value[0] = -12750;
			goto out;
		}
	}

	ret = pcmdev_dev_read(pcm_dev, dev_no, reg, &val);
	if (ret) {
		dev_err(pcm_dev->dev, "%s: read err=%d\n",
			__func__, ret);
		goto out;
	}

	val = (val >> shift) & mask;
	val = (val > max) ? max : val;
	val = mc->invert ? max - val : val;
	ucontrol->value.integer.value[0] = val;
out:
	mutex_unlock(&pcm_dev->codec_lock);
	return ret;
}

static int pcmdevice_get_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return pcmdev_get_volsw(kcontrol, ucontrol, PCMDEV_GENERIC_VOL_CTRL);
}

static int pcm1690_get_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return pcmdev_get_volsw(kcontrol, ucontrol, PCMDEV_PCM1690_VOL_CTRL);
}

static int pcm1690_get_finevolsw(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	return pcmdev_get_volsw(kcontrol, ucontrol,
		PCMDEV_PCM1690_FINE_VOL_CTRL);
}

static int pcmdev_put_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol, int vol_ctrl_type)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct pcmdevice_priv *pcm_dev =
		snd_soc_component_get_drvdata(component);
	struct pcmdevice_mixer_control *mc =
		(struct pcmdevice_mixer_control *)kcontrol->private_value;
	int max = mc->max, rc;
	unsigned int mask = BIT(fls(max)) - 1;
	unsigned int dev_no = mc->dev_no;
	unsigned int shift = mc->shift;
	unsigned int val, val_mask;
	unsigned int reg = mc->reg;

	mutex_lock(&pcm_dev->codec_lock);
	val = ucontrol->value.integer.value[0] & mask;
	val = (val > max) ? max : val;
	val = mc->invert ? max - val : val;
	val_mask = mask << shift;
	val = val << shift;

	switch (vol_ctrl_type) {
	case PCMDEV_PCM1690_VOL_CTRL:
		val_mask |= PCM1690_REG_MODE_CTRL_DAMS_MSK;
		val |= PCM1690_REG_MODE_CTRL_DAMS_WIDE_RANGE;
		break;
	case PCMDEV_PCM1690_FINE_VOL_CTRL:
		val_mask |= PCM1690_REG_MODE_CTRL_DAMS_MSK;
		val |= PCM1690_REG_MODE_CTRL_DAMS_FINE_STEP;
		break;
	}

	rc = pcmdev_dev_update_bits(pcm_dev, dev_no, reg, val_mask, val);
	if (rc < 0)
		dev_err(pcm_dev->dev, "%s: update_bits err = %d\n",
			__func__, rc);
	else
		rc = 1;
	mutex_unlock(&pcm_dev->codec_lock);
	return rc;
}

static int pcmdevice_put_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return pcmdev_put_volsw(kcontrol, ucontrol, PCMDEV_GENERIC_VOL_CTRL);
}

static int pcm1690_put_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return pcmdev_put_volsw(kcontrol, ucontrol, PCMDEV_PCM1690_VOL_CTRL);
}

static int pcm1690_put_finevolsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return pcmdev_put_volsw(kcontrol, ucontrol,
		PCMDEV_PCM1690_FINE_VOL_CTRL);
}

static const struct pcmdev_ctrl_info pcmdev_gain_ctl_info[][2] = {
	// ADC3120
	{
		{
			.gain = adc5120_chgain_tlv,
			.pcmdev_ctrl = adc5120_analog_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(adc5120_analog_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 0,
		},
		{
			.gain = adc5120_fgain_tlv,
			.pcmdev_ctrl = adc5120_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(adc5120_digi_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
	},
	// ADC5120
	{
		{
			.gain = adc5120_chgain_tlv,
			.pcmdev_ctrl = adc5120_analog_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(adc5120_analog_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 0,
		},
		{
			.gain = adc5120_fgain_tlv,
			.pcmdev_ctrl = adc5120_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(adc5120_digi_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
	},
	// ADC6120
	{
		{
			.gain = adc5120_chgain_tlv,
			.pcmdev_ctrl = adc5120_analog_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(adc5120_analog_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 0,
		},
		{
			.gain = adc5120_fgain_tlv,
			.pcmdev_ctrl = adc5120_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(adc5120_digi_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
	},
	// DIX4192
	{
		{
			.ctrl_array_size = 0,
		},
		{
			.ctrl_array_size = 0,
		},
	},
	// PCM1690
	{
		{
			.gain = pcm1690_fine_dig_gain_tlv,
			.pcmdev_ctrl = pcm1690_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcm1690_digi_gain_ctl),
			.get = pcm1690_get_volsw,
			.put = pcm1690_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
		{
			.gain = pcm1690_dig_gain_tlv,
			.pcmdev_ctrl = pcm1690_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcm1690_digi_gain_ctl),
			.get = pcm1690_get_finevolsw,
			.put = pcm1690_put_finevolsw,
			.pcmdev_ctrl_name_id = 2,
		},
	},
	// PCM3120
	{
		{
			.gain = adc5120_chgain_tlv,
			.pcmdev_ctrl = adc5120_analog_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(adc5120_analog_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 0,
		},
		{
			.gain = adc5120_fgain_tlv,
			.pcmdev_ctrl = adc5120_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(adc5120_digi_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
	},
	// PCM3140
	{
		{
			.gain = pcm6260_chgain_tlv,
			.pcmdev_ctrl = pcm6240_analog_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcm6240_analog_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 0,
		},
		{
			.gain = pcm6260_fgain_tlv,
			.pcmdev_ctrl = pcm6240_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcm6240_digi_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
	},
	// PCM5120
	{
		{
			.gain = adc5120_chgain_tlv,
			.pcmdev_ctrl = adc5120_analog_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(adc5120_analog_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 0,
		},
		{
			.gain = adc5120_fgain_tlv,
			.pcmdev_ctrl = adc5120_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(adc5120_digi_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
	},
	// PCM5140
	{
		{
			.gain = pcm6260_chgain_tlv,
			.pcmdev_ctrl = pcm6240_analog_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcm6240_analog_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 0,
		},
		{
			.gain = pcm6260_fgain_tlv,
			.pcmdev_ctrl = pcm6240_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcm6240_digi_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
	},
	// PCM6120
	{
		{
			.gain = adc5120_chgain_tlv,
			.pcmdev_ctrl = adc5120_analog_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(adc5120_analog_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 0,
		},
		{
			.gain = adc5120_fgain_tlv,
			.pcmdev_ctrl = adc5120_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(adc5120_digi_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
	},
	// PCM6140
	{
		{
			.gain = pcm6260_chgain_tlv,
			.pcmdev_ctrl = pcm6240_analog_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcm6240_analog_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 0,
		},
		{
			.gain = pcm6260_fgain_tlv,
			.pcmdev_ctrl = pcm6240_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcm6240_digi_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
	},
	// PCM6240
	{
		{
			.gain = pcm6260_chgain_tlv,
			.pcmdev_ctrl = pcm6240_analog_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcm6240_analog_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 0,
		},
		{
			.gain = pcm6260_fgain_tlv,
			.pcmdev_ctrl = pcm6240_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcm6240_digi_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
	},
	// PCM6260
	{
		{
			.gain = pcm6260_chgain_tlv,
			.pcmdev_ctrl = pcm6260_analog_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcm6260_analog_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 0,
		},
		{
			.gain = pcm6260_fgain_tlv,
			.pcmdev_ctrl = pcm6260_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcm6260_digi_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
	},
	// PCM9211
	{
		{
			.ctrl_array_size = 0,
		},
		{
			.gain = pcm9211_dig_gain_tlv,
			.pcmdev_ctrl = pcm9211_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcm9211_digi_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},

	},
	// PCMD3140
	{
		{
			.gain = taa5412_fine_gain_tlv,
			.pcmdev_ctrl = pcmd3140_fine_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcmd3140_fine_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 2,
		},
		{
			.gain = pcmd3140_dig_gain_tlv,
			.pcmdev_ctrl = pcmd3140_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcmd3140_digi_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
	},
	// PCMD3180
	{
		{
			.gain = taa5412_fine_gain_tlv,
			.pcmdev_ctrl = pcmd3180_fine_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcmd3180_fine_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 2,
		},
		{
			.gain = pcmd3140_dig_gain_tlv,
			.pcmdev_ctrl = pcmd3180_digi_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(pcmd3180_digi_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
	},
	// PCMD512X
	{
		{
			.ctrl_array_size = 0,
		},
		{
			.ctrl_array_size = 0,
		},
	},
	// TAA5212
	{
		{
			.gain = taa5412_fine_gain_tlv,
			.pcmdev_ctrl = taa5412_fine_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(taa5412_fine_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 2,
		},
		{
			.gain = taa5412_dig_vol_tlv,
			.pcmdev_ctrl = taa5412_digi_vol_ctl,
			.ctrl_array_size = ARRAY_SIZE(taa5412_digi_vol_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
	},
	// TAA5412
	{
		{
			.gain = taa5412_fine_gain_tlv,
			.pcmdev_ctrl = taa5412_fine_gain_ctl,
			.ctrl_array_size = ARRAY_SIZE(taa5412_fine_gain_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 2,
		},
		{
			.gain = taa5412_dig_vol_tlv,
			.pcmdev_ctrl = taa5412_digi_vol_ctl,
			.ctrl_array_size = ARRAY_SIZE(taa5412_digi_vol_ctl),
			.get = pcmdevice_get_volsw,
			.put = pcmdevice_put_volsw,
			.pcmdev_ctrl_name_id = 1,
		},
	},
	// TAD5212
	{
		{
			.ctrl_array_size = 0,
		},
		{
			.ctrl_array_size = 0,
		},
	},
	// TAD5412
	{
		{
			.ctrl_array_size = 0,
		},
		{
			.ctrl_array_size = 0,
		},
	},
};

static int pcmdev_dev_bulk_write(struct pcmdevice_priv *pcm_dev,
	unsigned int dev_no, unsigned int reg, unsigned char *data,
	unsigned int len)
{
	struct regmap *map = pcm_dev->regmap;
	int ret;

	if (dev_no >= pcm_dev->ndev) {
		dev_err(pcm_dev->dev, "%s: no such channel(%d)\n", __func__,
			dev_no);
		return -EINVAL;
	}

	ret = pcmdev_change_dev(pcm_dev, dev_no);
	if (ret < 0) {
		dev_err(pcm_dev->dev, "%s: chg dev err = %d\n", __func__, ret);
		return ret;
	}

	ret = regmap_bulk_write(map, reg, data, len);
	if (ret < 0)
		dev_err(pcm_dev->dev, "%s: bulk_write err = %d\n", __func__,
			ret);

	return ret;
}

static int pcmdev_dev_write(struct pcmdevice_priv *pcm_dev,
	unsigned int dev_no, unsigned int reg, unsigned int value)
{
	struct regmap *map = pcm_dev->regmap;
	int ret;

	if (dev_no >= pcm_dev->ndev) {
		dev_err(pcm_dev->dev, "%s: no such channel(%d)\n", __func__,
			dev_no);
		return -EINVAL;
	}

	ret = pcmdev_change_dev(pcm_dev, dev_no);
	if (ret < 0) {
		dev_err(pcm_dev->dev, "%s: chg dev err = %d\n", __func__, ret);
		return ret;
	}

	ret = regmap_write(map, reg, value);
	if (ret < 0)
		dev_err(pcm_dev->dev, "%s: err = %d\n", __func__, ret);

	return ret;
}

static int pcmdevice_info_profile(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct snd_soc_component *codec
		= snd_soc_kcontrol_component(kcontrol);
	struct pcmdevice_priv *pcm_dev =
		snd_soc_component_get_drvdata(codec);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = max(0, pcm_dev->regbin.ncfgs - 1);

	return 0;
}

static int pcmdevice_get_profile_id(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec
		= snd_soc_kcontrol_component(kcontrol);
	struct pcmdevice_priv *pcm_dev =
		snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = pcm_dev->cur_conf;

	return 0;
}

static int pcmdevice_set_profile_id(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec
		= snd_soc_kcontrol_component(kcontrol);
	struct pcmdevice_priv *pcm_dev =
		snd_soc_component_get_drvdata(codec);
	int nr_profile = ucontrol->value.integer.value[0];
	int max = pcm_dev->regbin.ncfgs - 1;
	int ret = 0;

	nr_profile = clamp(nr_profile, 0, max);

	if (pcm_dev->cur_conf != nr_profile) {
		pcm_dev->cur_conf = nr_profile;
		ret = 1;
	}

	return ret;
}

static int pcmdevice_info_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct pcmdevice_mixer_control *mc =
		(struct pcmdevice_mixer_control *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mc->max;
	return 0;
}

static void pcm9211_sw_rst(struct pcmdevice_priv *pcm_dev)
{
	int ret, i;

	for (i = 0; i < pcm_dev->ndev; i++) {
		ret = pcmdev_dev_update_bits(pcm_dev, i,
			PCM9211_REG_SW_CTRL, PCM9211_REG_SW_CTRL_MRST_MSK,
			PCM9211_REG_SW_CTRL_MRST);
		if (ret < 0)
			dev_err(pcm_dev->dev, "%s: dev %d swreset fail %d\n",
				__func__, i, ret);
	}
}

static void pcmdevice_sw_rst(struct pcmdevice_priv *pcm_dev)
{
	int ret, i;

	for (i = 0; i < pcm_dev->ndev; i++) {
		ret = pcmdev_dev_write(pcm_dev, i, PCMDEVICE_REG_SWRESET,
			PCMDEVICE_REG_SWRESET_RESET);
		if (ret < 0)
			dev_err(pcm_dev->dev, "%s: dev %d swreset fail %d\n",
				__func__, i, ret);
	}
}

static struct pcmdevice_config_info *pcmdevice_add_config(void *ctxt,
	const unsigned char *config_data, unsigned int config_size,
	int *status)
{
	struct pcmdevice_priv *pcm_dev = (struct pcmdevice_priv *)ctxt;
	struct pcmdevice_config_info *cfg_info;
	struct pcmdevice_block_data **bk_da;
	unsigned int config_offset = 0, i;

	cfg_info = kzalloc(sizeof(struct pcmdevice_config_info), GFP_KERNEL);
	if (!cfg_info) {
		*status = -ENOMEM;
		goto out;
	}

	if (pcm_dev->regbin.fw_hdr.binary_version_num >= 0x105) {
		if (config_offset + 64 > (int)config_size) {
			*status = -EINVAL;
			dev_err(pcm_dev->dev,
				"%s: cfg_name out of boundary\n", __func__);
			goto out;
		}
		memcpy(cfg_info->cfg_name, &config_data[config_offset], 64);
		config_offset += 64;
	}

	if (config_offset + 4 > config_size) {
		*status = -EINVAL;
		dev_err(pcm_dev->dev, "%s: nblocks out of boundary\n",
			__func__);
		goto out;
	}
	cfg_info->nblocks =
		get_unaligned_be32(&config_data[config_offset]);
	config_offset += 4;

	bk_da = cfg_info->blk_data = kcalloc(cfg_info->nblocks,
		sizeof(struct pcmdevice_block_data *), GFP_KERNEL);
	if (!bk_da) {
		*status = -ENOMEM;
		goto out;
	}
	cfg_info->real_nblocks = 0;
	for (i = 0; i < cfg_info->nblocks; i++) {
		if (config_offset + 12 > config_size) {
			*status = -EINVAL;
			dev_err(pcm_dev->dev,
				"%s: out of boundary i = %d nblocks = %u\n",
				__func__, i, cfg_info->nblocks);
			break;
		}
		bk_da[i] = kzalloc(sizeof(struct pcmdevice_block_data),
			GFP_KERNEL);
		if (!bk_da[i]) {
			*status = -ENOMEM;
			break;
		}
		bk_da[i]->dev_idx = config_data[config_offset];
		config_offset++;

		bk_da[i]->block_type = config_data[config_offset];
		config_offset++;

		if (bk_da[i]->block_type == PCMDEVICE_BIN_BLK_PRE_POWER_UP) {
			if (bk_da[i]->dev_idx == 0)
				cfg_info->active_dev =
					(1 << pcm_dev->ndev) - 1;
			else
				cfg_info->active_dev =
					1 << (bk_da[i]->dev_idx - 1);
		}

		bk_da[i]->yram_checksum =
			get_unaligned_be16(&config_data[config_offset]);
		config_offset += 2;
		bk_da[i]->block_size =
			get_unaligned_be32(&config_data[config_offset]);
		config_offset += 4;

		bk_da[i]->n_subblks =
			get_unaligned_be32(&config_data[config_offset]);

		config_offset += 4;

		if (config_offset + bk_da[i]->block_size > config_size) {
			*status = -EINVAL;
			dev_err(pcm_dev->dev,
				"%s: out of boundary: i = %d blks = %u\n",
				__func__, i, cfg_info->nblocks);
			break;
		}

		bk_da[i]->regdata = kmemdup(&config_data[config_offset],
			bk_da[i]->block_size, GFP_KERNEL);
		if (!bk_da[i]->regdata) {
			*status = -ENOMEM;
			goto out;
		}
		config_offset += bk_da[i]->block_size;
		cfg_info->real_nblocks += 1;
	}
out:
	return cfg_info;
}

static int pcmdev_gain_ctrl_add(struct pcmdevice_priv *pcm_dev,
	int dev_no, int ctl_id)
{
	struct i2c_adapter *adap = pcm_dev->client->adapter;
	struct snd_soc_component *comp = pcm_dev->component;
	struct pcmdevice_mixer_control *pcmdev_ctrl;
	struct snd_kcontrol_new *pcmdev_controls;
	int ret, mix_index = 0, name_id, chn;
	unsigned int id = pcm_dev->chip_id;
	const int nr_chn =
		pcmdev_gain_ctl_info[id][ctl_id].ctrl_array_size;
	const char *ctrl_name;
	char *name;

	if (!nr_chn) {
		dev_dbg(pcm_dev->dev, "%s: no gain ctrl for %s\n", __func__,
			pcm_dev->dev_name);
		return 0;
	}

	pcmdev_controls = devm_kzalloc(pcm_dev->dev,
		nr_chn * sizeof(struct snd_kcontrol_new), GFP_KERNEL);
	if (!pcmdev_controls)
		return -ENOMEM;

	name_id = pcmdev_gain_ctl_info[id][ctl_id].pcmdev_ctrl_name_id;

	ctrl_name = pcmdev_ctrl_name[name_id];

	for (chn = 1; chn <= nr_chn; chn++) {
		name = devm_kzalloc(pcm_dev->dev,
			SNDRV_CTL_ELEM_ID_NAME_MAXLEN, GFP_KERNEL);
		if (!name) {
			ret = -ENOMEM;
			goto out;
		}
		scnprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
			ctrl_name, pcm_dev->upper_dev_name, adap->nr,
			dev_no, chn);
		pcmdev_controls[mix_index].tlv.p =
			pcmdev_gain_ctl_info[id][ctl_id].gain;
		pcmdev_ctrl = devm_kmemdup(pcm_dev->dev,
			&pcmdev_gain_ctl_info[id][ctl_id].pcmdev_ctrl[chn - 1],
			sizeof(*pcmdev_ctrl), GFP_KERNEL);
		if (!pcmdev_ctrl) {
			ret = -ENOMEM;
			goto out;
		}
		pcmdev_ctrl->dev_no = dev_no;
		pcmdev_controls[mix_index].private_value =
			(unsigned long)pcmdev_ctrl;
		pcmdev_controls[mix_index].name = name;
		pcmdev_controls[mix_index].access =
			SNDRV_CTL_ELEM_ACCESS_TLV_READ |
			SNDRV_CTL_ELEM_ACCESS_READWRITE;
		pcmdev_controls[mix_index].iface =
			SNDRV_CTL_ELEM_IFACE_MIXER;
		pcmdev_controls[mix_index].info = pcmdevice_info_volsw;
		pcmdev_controls[mix_index].get =
			pcmdev_gain_ctl_info[id][ctl_id].get;
		pcmdev_controls[mix_index].put =
			pcmdev_gain_ctl_info[id][ctl_id].put;
		mix_index++;
	}

	ret = snd_soc_add_component_controls(comp, pcmdev_controls, mix_index);
	if (ret)
		dev_err(pcm_dev->dev, "%s: add_controls err = %d\n",
			__func__, ret);
out:
	return ret;
}

static int pcmdev_profile_ctrl_add(struct pcmdevice_priv *pcm_dev)
{
	struct snd_soc_component *comp = pcm_dev->component;
	struct i2c_adapter *adap = pcm_dev->client->adapter;
	struct snd_kcontrol_new *pcmdev_ctrl;
	char *name;
	int ret;

	pcmdev_ctrl = devm_kzalloc(pcm_dev->dev,
		sizeof(struct snd_kcontrol_new), GFP_KERNEL);
	if (!pcmdev_ctrl)
		return -ENOMEM;

	/* Create a mixer item for selecting the active profile */
	name = devm_kzalloc(pcm_dev->dev, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	scnprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		"%s i2c%d Profile id", pcm_dev->upper_dev_name, adap->nr);
	pcmdev_ctrl->name = name;
	pcmdev_ctrl->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	pcmdev_ctrl->info = pcmdevice_info_profile;
	pcmdev_ctrl->get = pcmdevice_get_profile_id;
	pcmdev_ctrl->put = pcmdevice_set_profile_id;

	ret = snd_soc_add_component_controls(comp, pcmdev_ctrl, 1);
	if (ret)
		dev_err(pcm_dev->dev, "%s: add_controls err = %d\n",
			__func__, ret);

	return ret;
}

static void pcmdevice_config_info_remove(void *ctxt)
{
	struct pcmdevice_priv *pcm_dev = (struct pcmdevice_priv *) ctxt;
	struct pcmdevice_regbin *regbin = &(pcm_dev->regbin);
	struct pcmdevice_config_info **cfg_info = regbin->cfg_info;
	int i, j;

	if (!cfg_info)
		return;
	for (i = 0; i < regbin->ncfgs; i++) {
		if (!cfg_info[i])
			continue;
		if (cfg_info[i]->blk_data) {
			for (j = 0; j < (int)cfg_info[i]->real_nblocks; j++) {
				if (!cfg_info[i]->blk_data[j])
					continue;
				kfree(cfg_info[i]->blk_data[j]->regdata);
				kfree(cfg_info[i]->blk_data[j]);
			}
			kfree(cfg_info[i]->blk_data);
		}
		kfree(cfg_info[i]);
	}
	kfree(cfg_info);
}

static int pcmdev_regbin_ready(const struct firmware *fmw, void *ctxt)
{
	struct pcmdevice_config_info **cfg_info;
	struct pcmdevice_priv *pcm_dev = ctxt;
	struct pcmdevice_regbin_hdr *fw_hdr;
	struct pcmdevice_regbin *regbin;
	unsigned int total_config_sz = 0;
	int offset = 0, ret = 0, i;
	unsigned char *buf;

	regbin = &(pcm_dev->regbin);
	fw_hdr = &(regbin->fw_hdr);
	if (!fmw || !fmw->data) {
		dev_err(pcm_dev->dev, "%s: failed to read %s\n",
			__func__, pcm_dev->bin_name);
		pcm_dev->fw_state = PCMDEVICE_FW_LOAD_FAILED;
		ret = -EINVAL;
		goto out;
	}
	buf = (unsigned char *)fmw->data;

	fw_hdr->img_sz = get_unaligned_be32(&buf[offset]);
	offset += 4;
	if (fw_hdr->img_sz != fmw->size) {
		dev_err(pcm_dev->dev, "%s: file size(%d) not match %u",
			__func__, (int)fmw->size, fw_hdr->img_sz);
		pcm_dev->fw_state = PCMDEVICE_FW_LOAD_FAILED;
		ret = -EINVAL;
		goto out;
	}

	fw_hdr->checksum = get_unaligned_be32(&buf[offset]);
	offset += 4;
	fw_hdr->binary_version_num = get_unaligned_be32(&buf[offset]);
	if (fw_hdr->binary_version_num < 0x103) {
		dev_err(pcm_dev->dev, "%s: bin version 0x%04x is out of date",
			__func__, fw_hdr->binary_version_num);
		pcm_dev->fw_state = PCMDEVICE_FW_LOAD_FAILED;
		ret = -EINVAL;
		goto out;
	}
	offset += 4;
	fw_hdr->drv_fw_version = get_unaligned_be32(&buf[offset]);
	offset += 8;
	fw_hdr->plat_type = buf[offset];
	offset += 1;
	fw_hdr->dev_family = buf[offset];
	offset += 1;
	fw_hdr->reserve = buf[offset];
	offset += 1;
	fw_hdr->ndev = buf[offset];
	offset += 1;
	if (fw_hdr->ndev != pcm_dev->ndev) {
		dev_err(pcm_dev->dev, "%s: invalid ndev(%u)\n", __func__,
			fw_hdr->ndev);
		pcm_dev->fw_state = PCMDEVICE_FW_LOAD_FAILED;
		ret = -EINVAL;
		goto out;
	}

	if (offset + PCMDEVICE_MAX_REGBIN_DEVICES > fw_hdr->img_sz) {
		dev_err(pcm_dev->dev, "%s: devs out of boundary!\n", __func__);
		pcm_dev->fw_state = PCMDEVICE_FW_LOAD_FAILED;
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < PCMDEVICE_MAX_REGBIN_DEVICES; i++, offset++)
		fw_hdr->devs[i] = buf[offset];

	fw_hdr->nconfig = get_unaligned_be32(&buf[offset]);
	offset += 4;

	for (i = 0; i < PCMDEVICE_CONFIG_SUM; i++) {
		fw_hdr->config_size[i] = get_unaligned_be32(&buf[offset]);
		offset += 4;
		total_config_sz += fw_hdr->config_size[i];
	}

	if (fw_hdr->img_sz - total_config_sz != (unsigned int)offset) {
		dev_err(pcm_dev->dev, "%s: bin file error!\n", __func__);
		pcm_dev->fw_state = PCMDEVICE_FW_LOAD_FAILED;
		ret = -EINVAL;
		goto out;
	}
	cfg_info = kcalloc(fw_hdr->nconfig, sizeof(*cfg_info), GFP_KERNEL);
	if (!cfg_info) {
		pcm_dev->fw_state = PCMDEVICE_FW_LOAD_FAILED;
		ret = -ENOMEM;
		goto out;
	}
	regbin->cfg_info = cfg_info;
	regbin->ncfgs = 0;
	for (i = 0; i < (int)fw_hdr->nconfig; i++) {
		cfg_info[i] = pcmdevice_add_config(ctxt, &buf[offset],
				fw_hdr->config_size[i], &ret);
		if (ret) {
			/* In case the bin file is partially destroyed. */
			if (regbin->ncfgs == 0)
				pcm_dev->fw_state = PCMDEVICE_FW_LOAD_FAILED;
			break;
		}
		offset += (int)fw_hdr->config_size[i];
		regbin->ncfgs += 1;
	}

out:
	if (pcm_dev->fw_state == PCMDEVICE_FW_LOAD_FAILED) {
		dev_err(pcm_dev->dev,
			"%s: remove config due to fw load error!\n", __func__);
		pcmdevice_config_info_remove(pcm_dev);
	}

	return ret;
}

static int pcmdevice_comp_probe(struct snd_soc_component *comp)
{
	struct pcmdevice_priv *pcm_dev = snd_soc_component_get_drvdata(comp);
	struct i2c_adapter *adap = pcm_dev->client->adapter;
	const struct firmware *fw_entry = NULL;
	int ret, i, j;

	mutex_lock(&pcm_dev->codec_lock);

	pcm_dev->component = comp;

	for (i = 0; i < pcm_dev->ndev; i++) {
		for (j = 0; j < 2; j++) {
			ret = pcmdev_gain_ctrl_add(pcm_dev, i, j);
			if (ret < 0)
				goto out;
		}
	}

	if (comp->name_prefix) {
		/* There's name_prefix defined in DTS. Bin file name will be
		 * name_prefix.bin stores the firmware including register
		 * setting and params for different filters inside chips, it
		 * must be copied into firmware folder. The same types of
		 * pcmdevices sitting on the same i2c bus will be aggregated as
		 * one single codec, all of them share the same bin file.
		 */
		scnprintf(pcm_dev->bin_name, PCMDEVICE_BIN_FILENAME_LEN,
			"%s.bin", comp->name_prefix);
	} else {
		/* There's NO name_prefix defined in DTS. Bin file name will be
		 * device-name[defined in pcmdevice_i2c_id]-i2c-bus_id
		 * [0,1,...,N]-sum[1,...,4]dev.bin stores the firmware
		 * including register setting and params for different filters
		 * inside chips, it must be copied into firmware folder. The
		 * same types of pcmdevices sitting on the same i2c bus will be
		 * aggregated as one single codec, all of them share the same
		 * bin file.
		 */
		scnprintf(pcm_dev->bin_name, PCMDEVICE_BIN_FILENAME_LEN,
			"%s-i2c-%d-%udev.bin", pcm_dev->dev_name, adap->nr,
			pcm_dev->ndev);
	}

	ret = request_firmware(&fw_entry, pcm_dev->bin_name, pcm_dev->dev);
	if (ret) {
		dev_err(pcm_dev->dev, "%s: request %s err = %d\n", __func__,
			pcm_dev->bin_name, ret);
		goto out;
	}

	ret = pcmdev_regbin_ready(fw_entry, pcm_dev);
	if (ret) {
		dev_err(pcm_dev->dev, "%s: %s parse err = %d\n", __func__,
			pcm_dev->bin_name, ret);
		goto out;
	}
	ret = pcmdev_profile_ctrl_add(pcm_dev);
out:
	if (fw_entry)
		release_firmware(fw_entry);

	mutex_unlock(&pcm_dev->codec_lock);
	return ret;
}


static void pcmdevice_comp_remove(struct snd_soc_component *codec)
{
	struct pcmdevice_priv *pcm_dev = snd_soc_component_get_drvdata(codec);

	if (!pcm_dev)
		return;
	mutex_lock(&pcm_dev->codec_lock);
	pcmdevice_config_info_remove(pcm_dev);
	mutex_unlock(&pcm_dev->codec_lock);
}

static const struct snd_soc_dapm_widget pcmdevice_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("ASI", "ASI Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("ASI1 OUT", "ASI1 Capture",
		0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("OUT"),
	SND_SOC_DAPM_INPUT("MIC"),
};

static const struct snd_soc_dapm_route pcmdevice_audio_map[] = {
	{"OUT", NULL, "ASI"},
	{"ASI1 OUT", NULL, "MIC"},
};

static const struct snd_soc_component_driver
	soc_codec_driver_pcmdevice = {
	.probe			= pcmdevice_comp_probe,
	.remove			= pcmdevice_comp_remove,
	.dapm_widgets		= pcmdevice_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(pcmdevice_dapm_widgets),
	.dapm_routes		= pcmdevice_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(pcmdevice_audio_map),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 0,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int pcmdev_single_byte_wr(struct pcmdevice_priv *pcm_dev,
	unsigned char *data, int devn, int sublocksize)
{
	unsigned short len = get_unaligned_be16(&data[2]);
	int offset = 2;
	int i, ret;

	offset += 2;
	if (offset + 4 * len > sublocksize) {
		dev_err(pcm_dev->dev, "%s: dev-%d byt wr out of boundary\n",
			__func__, devn);
		return -EINVAL;
	}

	for (i = 0; i < len; i++) {
		ret = pcmdev_dev_write(pcm_dev, devn,
			PCMDEVICE_REG(data[offset + 1], data[offset + 2]),
			data[offset + 3]);
		/* skip this error for next operation or next devices */
		if (ret < 0)
			dev_err(pcm_dev->dev, "%s: dev-%d single write err\n",
				__func__, devn);

		offset += 4;
	}

	return offset;
}

static int pcmdev_burst_wr(struct pcmdevice_priv *pcm_dev,
	unsigned char *data, int devn, int sublocksize)
{
	unsigned short len = get_unaligned_be16(&data[2]);
	int offset = 2;
	int ret;

	offset += 2;
	if (offset + 4 + len > sublocksize) {
		dev_err(pcm_dev->dev, "%s: dev-%d burst Out of boundary\n",
			__func__, devn);
		return -EINVAL;
	}
	if (len % 4) {
		dev_err(pcm_dev->dev, "%s: dev-%d bst-len(%u) not div by 4\n",
			__func__, devn, len);
		return -EINVAL;
	}
	ret = pcmdev_dev_bulk_write(pcm_dev, devn,
		PCMDEVICE_REG(data[offset + 1], data[offset + 2]),
		&(data[offset + 4]), len);
	/* skip this error for next devices */
	if (ret < 0)
		dev_err(pcm_dev->dev, "%s: dev-%d bulk_write err = %d\n",
			__func__, devn, ret);

	offset += (len + 4);

	return offset;
}

static int pcmdev_delay(struct pcmdevice_priv *pcm_dev,
	unsigned char *data, int devn, int sublocksize)
{
	unsigned int delay_time = 0;
	int offset = 2;

	if (offset + 2 > sublocksize) {
		dev_err(pcm_dev->dev, "%s: dev-%d delay out of boundary\n",
			__func__, devn);
		return -EINVAL;
	}
	delay_time = get_unaligned_be16(&data[2]) * 1000;
	usleep_range(delay_time, delay_time + 50);
	offset += 2;

	return offset;
}

static int pcmdev_bits_wr(struct pcmdevice_priv *pcm_dev,
	unsigned char *data, int devn, int sublocksize)
{
	int offset = 2;
	int ret;

	if (offset + 6 > sublocksize) {
		dev_err(pcm_dev->dev, "%s: dev-%d bit write out of memory\n",
			__func__, devn);
		return -EINVAL;
	}
	ret = pcmdev_dev_update_bits(pcm_dev, devn,
		PCMDEVICE_REG(data[offset + 3], data[offset + 4]),
		data[offset + 1], data[offset + 5]);
	/* skip this error for next devices */
	if (ret < 0)
		dev_err(pcm_dev->dev, "%s: dev-%d update_bits err = %d\n",
			__func__, devn, ret);

	offset += 6;

	return offset;
}

static int pcmdevice_process_block(void *ctxt, unsigned char *data,
	unsigned char dev_idx, int sublocksize)
{
	struct pcmdevice_priv *pcm_dev = (struct pcmdevice_priv *)ctxt;
	int devn, dev_end, ret = 0;
	unsigned char subblk_typ = data[1];

	if (dev_idx) {
		devn = dev_idx - 1;
		dev_end = dev_idx;
	} else {
		devn = 0;
		dev_end = pcm_dev->ndev;
	}

	/* loop in case of several devices sharing the same sub-block */
	for (; devn < dev_end; devn++) {
		switch (subblk_typ) {
		case PCMDEVICE_CMD_SING_W:
		ret = pcmdev_single_byte_wr(pcm_dev, data, devn, sublocksize);
			break;
		case PCMDEVICE_CMD_BURST:
		ret = pcmdev_burst_wr(pcm_dev, data, devn, sublocksize);
			break;
		case PCMDEVICE_CMD_DELAY:
		ret = pcmdev_delay(pcm_dev, data, devn, sublocksize);
			break;
		case PCMDEVICE_CMD_FIELD_W:
		ret = pcmdev_bits_wr(pcm_dev, data, devn, sublocksize);
			break;
		default:
			break;
		}
		/*
		 * In case of sub-block error, break the loop for the rest of
		 * devices.
		 */
		if (ret < 0)
			break;
	}

	return ret;
}

static void pcmdevice_select_cfg_blk(void *ctxt, int conf_no,
	unsigned char block_type)
{
	struct pcmdevice_priv *pcm_dev = (struct pcmdevice_priv *)ctxt;
	struct pcmdevice_regbin *regbin = &(pcm_dev->regbin);
	struct pcmdevice_config_info **cfg_info = regbin->cfg_info;
	struct pcmdevice_block_data **blk_data;
	int j, k;

	if (conf_no >= regbin->ncfgs || conf_no < 0 || NULL == cfg_info) {
		dev_err(pcm_dev->dev, "%s: conf_no should be less than %u\n",
			__func__, regbin->ncfgs);
		goto out;
	}
	blk_data = cfg_info[conf_no]->blk_data;

	for (j = 0; j < (int)cfg_info[conf_no]->real_nblocks; j++) {
		unsigned int length = 0, ret;

		if (block_type > 5 || block_type < 2) {
			dev_err(pcm_dev->dev,
				"%s: block_type should be out of range\n",
				__func__);
			goto out;
		}
		if (block_type != blk_data[j]->block_type)
			continue;

		for (k = 0; k < (int)blk_data[j]->n_subblks; k++) {
			ret = pcmdevice_process_block(pcm_dev,
				blk_data[j]->regdata + length,
				blk_data[j]->dev_idx,
				blk_data[j]->block_size - length);
			length += ret;
			if (blk_data[j]->block_size < length) {
				dev_err(pcm_dev->dev,
					"%s: %u %u out of boundary\n",
					__func__, length,
					blk_data[j]->block_size);
				break;
			}
		}
		if (length != blk_data[j]->block_size)
			dev_err(pcm_dev->dev, "%s: %u %u size is not same\n",
				__func__, length, blk_data[j]->block_size);
	}

out:
	return;
}

static int pcmdevice_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *codec = dai->component;
	struct pcmdevice_priv *pcm_dev = snd_soc_component_get_drvdata(codec);
	unsigned char block_type;

	if (pcm_dev->fw_state == PCMDEVICE_FW_LOAD_FAILED) {
		dev_err(pcm_dev->dev, "%s: bin file not loaded\n", __func__);
		return -EINVAL;
	}

	if (mute)
		block_type = PCMDEVICE_BIN_BLK_PRE_SHUTDOWN;
	else
		block_type = PCMDEVICE_BIN_BLK_PRE_POWER_UP;

	mutex_lock(&pcm_dev->codec_lock);
	pcmdevice_select_cfg_blk(pcm_dev, pcm_dev->cur_conf, block_type);
	mutex_unlock(&pcm_dev->codec_lock);
	return 0;
}

static int pcmdevice_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct pcmdevice_priv *pcm_dev = snd_soc_dai_get_drvdata(dai);
	unsigned int fsrate;
	unsigned int slot_width;
	int bclk_rate;
	int ret = 0;

	fsrate = params_rate(params);
	switch (fsrate) {
	case 48000:
		break;
	case 44100:
		break;
	default:
		dev_err(pcm_dev->dev, "%s: incorrect sample rate = %u\n",
			__func__, fsrate);
		ret = -EINVAL;
		goto out;
	}

	slot_width = params_width(params);
	switch (slot_width) {
	case 16:
		break;
	case 20:
		break;
	case 24:
		break;
	case 32:
		break;
	default:
		dev_err(pcm_dev->dev, "%s: incorrect slot width = %u\n",
			__func__, slot_width);
		ret = -EINVAL;
		goto out;
	}

	bclk_rate = snd_soc_params_to_bclk(params);
	if (bclk_rate < 0) {
		dev_err(pcm_dev->dev, "%s: incorrect bclk rate = %d\n",
			__func__, bclk_rate);
		ret = bclk_rate;
	}

out:
	return ret;
}

static const struct snd_soc_dai_ops pcmdevice_dai_ops = {
	.mute_stream = pcmdevice_mute,
	.hw_params = pcmdevice_hw_params,
};

static struct snd_soc_dai_driver pcmdevice_dai_driver[] = {
	{
		.name = "pcmdevice-codec",
		.capture = {
			.stream_name	 = "Capture",
			.channels_min	 = 2,
			.channels_max	 = PCMDEVICE_MAX_CHANNELS,
			.rates		 = PCMDEVICE_RATES,
			.formats	 = PCMDEVICE_FORMATS,
		},
		.playback = {
			.stream_name	 = "Playback",
			.channels_min	 = 2,
			.channels_max	 = PCMDEVICE_MAX_CHANNELS,
			.rates		 = PCMDEVICE_RATES,
			.formats	 = PCMDEVICE_FORMATS,
		},
		.ops = &pcmdevice_dai_ops,
		.symmetric_rate = 1,
	}
};

#ifdef CONFIG_OF
static const struct of_device_id pcmdevice_of_match[] = {
	{ .compatible = "ti,adc3120"  },
	{ .compatible = "ti,adc5120"  },
	{ .compatible = "ti,adc6120"  },
	{ .compatible = "ti,dix4192"  },
	{ .compatible = "ti,pcm1690"  },
	{ .compatible = "ti,pcm3120"  },
	{ .compatible = "ti,pcm3140"  },
	{ .compatible = "ti,pcm5120"  },
	{ .compatible = "ti,pcm5140"  },
	{ .compatible = "ti,pcm6120"  },
	{ .compatible = "ti,pcm6140"  },
	{ .compatible = "ti,pcm6240"  },
	{ .compatible = "ti,pcm6260"  },
	{ .compatible = "ti,pcm9211"  },
	{ .compatible = "ti,pcmd3140" },
	{ .compatible = "ti,pcmd3180" },
	{ .compatible = "ti,pcmd512x" },
	{ .compatible = "ti,taa5212"  },
	{ .compatible = "ti,taa5412"  },
	{ .compatible = "ti,tad5212"  },
	{ .compatible = "ti,tad5412"  },
	{},
};
MODULE_DEVICE_TABLE(of, pcmdevice_of_match);
#endif

static const struct regmap_range_cfg pcmdevice_ranges[] = {
	{
		.range_min = 0,
		.range_max = 256 * 128,
		.selector_reg = PCMDEVICE_PAGE_SELECT,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 128,
	},
};

static const struct regmap_config pcmdevice_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_MAPLE,
	.ranges = pcmdevice_ranges,
	.num_ranges = ARRAY_SIZE(pcmdevice_ranges),
	.max_register = 256 * 128,
};

static void pcmdevice_remove(struct pcmdevice_priv *pcm_dev)
{
	if (pcm_dev->irq)
		free_irq(pcm_dev->irq, pcm_dev);
	mutex_destroy(&pcm_dev->codec_lock);
}

static char *str_to_upper(char *str)
{
	char *orig = str;

	if (!str)
		return NULL;

	while (*str) {
		*str = toupper(*str);
		str++;
	}

	return orig;
}

static int pcmdevice_i2c_probe(struct i2c_client *i2c)
{
	struct pcmdevice_priv *pcm_dev;
	struct device_node *np;
	unsigned int dev_addrs[PCMDEVICE_MAX_I2C_DEVICES];
	int ret = 0, i = 0, ndev = 0;

	pcm_dev = devm_kzalloc(&i2c->dev, sizeof(*pcm_dev), GFP_KERNEL);
	if (!pcm_dev)
		return -ENOMEM;

	pcm_dev->chip_id = (uintptr_t)i2c_get_match_data(i2c);

	pcm_dev->dev = &i2c->dev;
	pcm_dev->client = i2c;

	if (pcm_dev->chip_id >= MAX_DEVICE)
		pcm_dev->chip_id = 0;

	strscpy(pcm_dev->dev_name, pcmdevice_i2c_id[pcm_dev->chip_id].name,
		sizeof(pcm_dev->dev_name));

	strscpy(pcm_dev->upper_dev_name,
		pcmdevice_i2c_id[pcm_dev->chip_id].name,
		sizeof(pcm_dev->upper_dev_name));

	str_to_upper(pcm_dev->upper_dev_name);

	pcm_dev->regmap = devm_regmap_init_i2c(i2c, &pcmdevice_i2c_regmap);
	if (IS_ERR(pcm_dev->regmap)) {
		ret = PTR_ERR(pcm_dev->regmap);
		dev_err(&i2c->dev, "%s: failed to allocate register map: %d\n",
			__func__, ret);
		goto out;
	}

	i2c_set_clientdata(i2c, pcm_dev);
	mutex_init(&pcm_dev->codec_lock);
	np = pcm_dev->dev->of_node;

	if (IS_ENABLED(CONFIG_OF)) {
		u64 addr;

		for (i = 0; i < PCMDEVICE_MAX_I2C_DEVICES; i++) {
			if (of_property_read_reg(np, i, &addr, NULL))
				break;
			dev_addrs[ndev++] = addr;
		}
	} else {
		ndev = 1;
		dev_addrs[0] = i2c->addr;
	}
	pcm_dev->irq = of_irq_get(np, 0);

	for (i = 0; i < ndev; i++)
		pcm_dev->addr[i] = dev_addrs[i];

	pcm_dev->ndev = ndev;

	pcm_dev->hw_rst = devm_gpiod_get_optional(&i2c->dev,
			"reset-gpios", GPIOD_OUT_HIGH);
	/* No reset GPIO, no side-effect */
	if (IS_ERR(pcm_dev->hw_rst)) {
		if (pcm_dev->chip_id == PCM9211 || pcm_dev->chip_id == PCM1690)
			pcm9211_sw_rst(pcm_dev);
		else
			pcmdevice_sw_rst(pcm_dev);
	} else {
		gpiod_set_value_cansleep(pcm_dev->hw_rst, 0);
		usleep_range(500, 1000);
		gpiod_set_value_cansleep(pcm_dev->hw_rst, 1);
	}

	if (pcm_dev->chip_id == PCM1690)
		goto skip_interrupt;
	if (pcm_dev->irq) {
		dev_dbg(pcm_dev->dev, "irq = %d", pcm_dev->irq);
	} else
		dev_err(pcm_dev->dev, "No irq provided\n");

skip_interrupt:
	ret = devm_snd_soc_register_component(&i2c->dev,
		&soc_codec_driver_pcmdevice, pcmdevice_dai_driver,
		ARRAY_SIZE(pcmdevice_dai_driver));
	if (ret < 0)
		dev_err(&i2c->dev, "probe register comp failed %d\n", ret);

out:
	if (ret < 0)
		pcmdevice_remove(pcm_dev);
	return ret;
}

static void pcmdevice_i2c_remove(struct i2c_client *i2c)
{
	struct pcmdevice_priv *pcm_dev = i2c_get_clientdata(i2c);

	pcmdevice_remove(pcm_dev);
}

static struct i2c_driver pcmdevice_i2c_driver = {
	.driver = {
		.name = "pcmdevice-codec",
		.of_match_table = of_match_ptr(pcmdevice_of_match),
	},
	.probe	= pcmdevice_i2c_probe,
	.remove = pcmdevice_i2c_remove,
	.id_table = pcmdevice_i2c_id,
};
module_i2c_driver(pcmdevice_i2c_driver);

MODULE_AUTHOR("Shenghao Ding <shenghao-ding@ti.com>");
MODULE_DESCRIPTION("ASoC PCM6240 Family Audio ADC/DAC Driver");
MODULE_LICENSE("GPL");
