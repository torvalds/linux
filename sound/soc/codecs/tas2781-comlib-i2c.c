// SPDX-License-Identifier: GPL-2.0
//
// TAS2563/TAS2781 Common functions for HDA and ASoC Audio drivers based on I2C
//
// Copyright 2025 Texas Instruments, Inc.
//
// Author: Shenghao Ding <shenghao-ding@ti.com>

#include <linux/crc8.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tas2781.h>
#include <sound/tas2781-comlib-i2c.h>

static const struct regmap_range_cfg tasdevice_ranges[] = {
	{
		.range_min = 0,
		.range_max = 256 * 128,
		.selector_reg = TASDEVICE_PAGE_SELECT,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 128,
	},
};

static const struct regmap_config tasdevice_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_NONE,
	.ranges = tasdevice_ranges,
	.num_ranges = ARRAY_SIZE(tasdevice_ranges),
	.max_register = 256 * 128,
};

static int tasdevice_change_chn_book(struct tasdevice_priv *tas_priv,
	unsigned short chn, int book)
{
	struct i2c_client *client = (struct i2c_client *)tas_priv->client;
	int ret = 0;

	if (chn < tas_priv->ndev) {
		struct tasdevice *tasdev = &tas_priv->tasdevice[chn];
		struct regmap *map = tas_priv->regmap;

		if (client->addr != tasdev->dev_addr) {
			client->addr = tasdev->dev_addr;
			/* All tas2781s share the same regmap, clear the page
			 * inside regmap once switching to another tas2781.
			 * Register 0 at any pages and any books inside tas2781
			 * is the same one for page-switching.
			 */
			ret = regmap_write(map, TASDEVICE_PAGE_SELECT, 0);
			if (ret < 0) {
				dev_err(tas_priv->dev, "%s, E=%d channel:%d\n",
					__func__, ret, chn);
				goto out;
			}
		}

		if (tasdev->cur_book != book) {
			ret = regmap_write(map, TASDEVICE_BOOKCTL_REG, book);
			if (ret < 0) {
				dev_err(tas_priv->dev, "%s, E=%d\n",
					__func__, ret);
				goto out;
			}
			tasdev->cur_book = book;
		}
	} else {
		ret = -EINVAL;
		dev_err(tas_priv->dev, "%s, no such channel(%d)\n", __func__,
			chn);
	}

out:
	return ret;
}

int tasdev_chn_switch(struct tasdevice_priv *tas_priv,
	unsigned short chn)
{
	struct i2c_client *client = (struct i2c_client *)tas_priv->client;
	struct tasdevice *tasdev = &tas_priv->tasdevice[chn];
	struct regmap *map = tas_priv->regmap;
	int ret;

	if (client->addr != tasdev->dev_addr) {
		client->addr = tasdev->dev_addr;
		/* All devices share the same regmap, clear the page
		 * inside regmap once switching to another device.
		 * Register 0 at any pages and any books inside tas2781
		 * is the same one for page-switching.
		 */
		ret = regmap_write(map, TASDEVICE_PAGE_SELECT, 0);
		if (ret < 0) {
			dev_err(tas_priv->dev, "%s, E=%d\n", __func__, ret);
			return ret;
		}
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(tasdev_chn_switch);

int tasdevice_dev_update_bits(
	struct tasdevice_priv *tas_priv, unsigned short chn,
	unsigned int reg, unsigned int mask, unsigned int value)
{
	int ret = 0;

	if (chn < tas_priv->ndev) {
		struct regmap *map = tas_priv->regmap;

		ret = tas_priv->change_chn_book(tas_priv, chn,
			TASDEVICE_BOOK_ID(reg));
		if (ret < 0)
			goto out;

		ret = regmap_update_bits(map, TASDEVICE_PGRG(reg),
			mask, value);
		if (ret < 0)
			dev_err(tas_priv->dev, "%s, E=%d\n", __func__, ret);
	} else {
		dev_err(tas_priv->dev, "%s, no such channel(%d)\n", __func__,
			chn);
		ret = -EINVAL;
	}

out:
	return ret;
}
EXPORT_SYMBOL_GPL(tasdevice_dev_update_bits);

struct tasdevice_priv *tasdevice_kzalloc(struct i2c_client *i2c)
{
	struct tasdevice_priv *tas_priv;

	tas_priv = devm_kzalloc(&i2c->dev, sizeof(*tas_priv), GFP_KERNEL);
	if (!tas_priv)
		return NULL;
	tas_priv->dev = &i2c->dev;
	tas_priv->client = (void *)i2c;

	return tas_priv;
}
EXPORT_SYMBOL_GPL(tasdevice_kzalloc);

int tasdevice_init(struct tasdevice_priv *tas_priv)
{
	int ret = 0;
	int i;

	tas_priv->regmap = devm_regmap_init_i2c(tas_priv->client,
		&tasdevice_regmap);
	if (IS_ERR(tas_priv->regmap)) {
		ret = PTR_ERR(tas_priv->regmap);
		dev_err(tas_priv->dev, "Failed to allocate register map: %d\n",
			ret);
		goto out;
	}

	tas_priv->cur_prog = -1;
	tas_priv->cur_conf = -1;
	tas_priv->isspi = false;

	for (i = 0; i < tas_priv->ndev; i++) {
		tas_priv->tasdevice[i].cur_book = -1;
		tas_priv->tasdevice[i].cur_prog = -1;
		tas_priv->tasdevice[i].cur_conf = -1;
	}

	tas_priv->update_bits = tasdevice_dev_update_bits;
	tas_priv->change_chn_book = tasdevice_change_chn_book;
	tas_priv->dev_read = tasdevice_dev_read;
	tas_priv->dev_bulk_read = tasdevice_dev_bulk_read;

	mutex_init(&tas_priv->codec_lock);

out:
	return ret;
}
EXPORT_SYMBOL_GPL(tasdevice_init);

static int tasdevice_clamp(int val, int max, unsigned int invert)
{
	if (val > max)
		val = max;
	if (invert)
		val = max - val;
	if (val < 0)
		val = 0;
	return val;
}

int tasdevice_amp_putvol(struct tasdevice_priv *tas_priv,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc)
{
	unsigned int invert = mc->invert;
	unsigned char mask;
	int max = mc->max;
	int err_cnt = 0;
	int val, i, ret;

	mask = (1 << fls(max)) - 1;
	mask <<= mc->shift;
	val = tasdevice_clamp(ucontrol->value.integer.value[0], max, invert);
	for (i = 0; i < tas_priv->ndev; i++) {
		ret = tasdevice_dev_update_bits(tas_priv, i,
			mc->reg, mask, (unsigned int)(val << mc->shift));
		if (!ret)
			continue;
		err_cnt++;
		dev_err(tas_priv->dev, "set AMP vol error in dev %d\n", i);
	}

	/* All the devices set error, return 0 */
	return (err_cnt == tas_priv->ndev) ? 0 : 1;
}
EXPORT_SYMBOL_GPL(tasdevice_amp_putvol);

int tasdevice_amp_getvol(struct tasdevice_priv *tas_priv,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc)
{
	unsigned int invert = mc->invert;
	unsigned char mask = 0;
	int max = mc->max;
	int ret = 0;
	int val;

	/* Read the primary device */
	ret = tasdevice_dev_read(tas_priv, 0, mc->reg, &val);
	if (ret) {
		dev_err(tas_priv->dev, "%s, get AMP vol error\n", __func__);
		goto out;
	}

	mask = (1 << fls(max)) - 1;
	mask <<= mc->shift;
	val = (val & mask) >> mc->shift;
	val = tasdevice_clamp(val, max, invert);
	ucontrol->value.integer.value[0] = val;

out:
	return ret;

}
EXPORT_SYMBOL_GPL(tasdevice_amp_getvol);

int tasdevice_digital_getvol(struct tasdevice_priv *tas_priv,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc)
{
	unsigned int invert = mc->invert;
	int max = mc->max;
	int ret, val;

	/* Read the primary device as the whole */
	ret = tasdevice_dev_read(tas_priv, 0, mc->reg, &val);
	if (ret) {
		dev_err(tas_priv->dev, "%s, get digital vol error\n",
			__func__);
		goto out;
	}

	val = tasdevice_clamp(val, max, invert);
	ucontrol->value.integer.value[0] = val;

out:
	return ret;

}
EXPORT_SYMBOL_GPL(tasdevice_digital_getvol);

int tasdevice_digital_putvol(struct tasdevice_priv *tas_priv,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc)
{
	unsigned int invert = mc->invert;
	int max = mc->max;
	int err_cnt = 0;
	int ret;
	int val, i;

	val = tasdevice_clamp(ucontrol->value.integer.value[0], max, invert);

	for (i = 0; i < tas_priv->ndev; i++) {
		ret = tasdevice_dev_write(tas_priv, i, mc->reg,
			(unsigned int)val);
		if (!ret)
			continue;
		err_cnt++;
		dev_err(tas_priv->dev,
			"set digital vol err in dev %d\n", i);
	}

	/* All the devices set error, return 0 */
	return (err_cnt == tas_priv->ndev) ? 0 : 1;

}
EXPORT_SYMBOL_GPL(tasdevice_digital_putvol);

void tasdevice_reset(struct tasdevice_priv *tas_dev)
{
	int ret, i;

	if (tas_dev->reset) {
		gpiod_set_value_cansleep(tas_dev->reset, 0);
		usleep_range(500, 1000);
		gpiod_set_value_cansleep(tas_dev->reset, 1);
	} else {
		for (i = 0; i < tas_dev->ndev; i++) {
			ret = tasdevice_dev_write(tas_dev, i,
				TASDEVICE_REG_SWRESET,
				tas_dev->chip_id >= TAS5825 ?
				TAS5825_REG_SWRESET_RESET :
				TASDEVICE_REG_SWRESET_RESET);
			if (ret < 0)
				dev_err(tas_dev->dev,
					"dev %d swreset fail, %d\n",
					i, ret);
		}
	}
	usleep_range(1000, 1050);
}
EXPORT_SYMBOL_GPL(tasdevice_reset);

int tascodec_init(struct tasdevice_priv *tas_priv, void *codec,
	struct module *module,
	void (*cont)(const struct firmware *fw, void *context))
{
	int ret = 0;

	/* Codec Lock Hold to ensure that codec_probe and firmware parsing and
	 * loading do not simultaneously execute.
	 */
	mutex_lock(&tas_priv->codec_lock);

	if (tas_priv->name_prefix)
		scnprintf(tas_priv->rca_binaryname, 64, "%s-%sRCA%d.bin",
			tas_priv->name_prefix, tas_priv->dev_name,
			tas_priv->ndev);
	else
		scnprintf(tas_priv->rca_binaryname, 64, "%sRCA%d.bin",
			tas_priv->dev_name, tas_priv->ndev);
	crc8_populate_msb(tas_priv->crc8_lkp_tbl, TASDEVICE_CRC8_POLYNOMIAL);
	tas_priv->codec = codec;
	ret = request_firmware_nowait(module, FW_ACTION_UEVENT,
		tas_priv->rca_binaryname, tas_priv->dev, GFP_KERNEL, tas_priv,
		cont);
	if (ret)
		dev_err(tas_priv->dev, "request_firmware_nowait err:0x%08x\n",
			ret);

	/* Codec Lock Release*/
	mutex_unlock(&tas_priv->codec_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(tascodec_init);

MODULE_DESCRIPTION("TAS2781 common library for I2C");
MODULE_AUTHOR("Shenghao Ding, TI, <shenghao-ding@ti.com>");
MODULE_LICENSE("GPL");
