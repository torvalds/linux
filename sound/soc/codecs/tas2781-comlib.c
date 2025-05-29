// SPDX-License-Identifier: GPL-2.0
//
// TAS2563/TAS2781 Common functions for HDA and ASoC Audio drivers
//
// Copyright 2023 - 2025 Texas Instruments, Inc.
//
// Author: Shenghao Ding <shenghao-ding@ti.com>

#include <linux/crc8.h>
#include <linux/dev_printk.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/tas2781.h>

int tasdevice_dev_read(struct tasdevice_priv *tas_priv,
	unsigned short chn, unsigned int reg, unsigned int *val)
{
	int ret = 0;

	if (chn < tas_priv->ndev) {
		struct regmap *map = tas_priv->regmap;

		ret = tas_priv->change_chn_book(tas_priv, chn,
			TASDEVICE_BOOK_ID(reg));
		if (ret < 0)
			goto out;

		ret = regmap_read(map, TASDEVICE_PGRG(reg), val);
		if (ret < 0)
			dev_err(tas_priv->dev, "%s, E=%d\n", __func__, ret);
	} else {
		ret = -EINVAL;
		dev_err(tas_priv->dev, "%s, no such channel(%d)\n", __func__,
			chn);
	}

out:
	return ret;
}
EXPORT_SYMBOL_GPL(tasdevice_dev_read);

int tasdevice_dev_bulk_read(struct tasdevice_priv *tas_priv,
	unsigned short chn, unsigned int reg, unsigned char *data,
	unsigned int len)
{
	int ret = 0;

	if (chn < tas_priv->ndev) {
		struct regmap *map = tas_priv->regmap;

		ret = tas_priv->change_chn_book(tas_priv, chn,
			TASDEVICE_BOOK_ID(reg));
		if (ret < 0)
			goto out;

		ret = regmap_bulk_read(map, TASDEVICE_PGRG(reg), data, len);
		if (ret < 0)
			dev_err(tas_priv->dev, "%s, E=%d\n", __func__, ret);
	} else
		dev_err(tas_priv->dev, "%s, no such channel(%d)\n", __func__,
			chn);

out:
	return ret;
}
EXPORT_SYMBOL_GPL(tasdevice_dev_bulk_read);

int tasdevice_dev_write(struct tasdevice_priv *tas_priv,
	unsigned short chn, unsigned int reg, unsigned int value)
{
	int ret = 0;

	if (chn < tas_priv->ndev) {
		struct regmap *map = tas_priv->regmap;

		ret = tas_priv->change_chn_book(tas_priv, chn,
			TASDEVICE_BOOK_ID(reg));
		if (ret < 0)
			goto out;

		ret = regmap_write(map, TASDEVICE_PGRG(reg),
			value);
		if (ret < 0)
			dev_err(tas_priv->dev, "%s, E=%d\n", __func__, ret);
	} else {
		ret = -EINVAL;
		dev_err(tas_priv->dev, "%s, no such channel(%d)\n", __func__,
			chn);
	}

out:
	return ret;
}
EXPORT_SYMBOL_GPL(tasdevice_dev_write);

int tasdevice_dev_bulk_write(
	struct tasdevice_priv *tas_priv, unsigned short chn,
	unsigned int reg, unsigned char *data,
	unsigned int len)
{
	int ret = 0;

	if (chn < tas_priv->ndev) {
		struct regmap *map = tas_priv->regmap;

		ret = tas_priv->change_chn_book(tas_priv, chn,
			TASDEVICE_BOOK_ID(reg));
		if (ret < 0)
			goto out;

		ret = regmap_bulk_write(map, TASDEVICE_PGRG(reg),
			data, len);
		if (ret < 0)
			dev_err(tas_priv->dev, "%s, E=%d\n", __func__, ret);
	} else {
		ret = -EINVAL;
		dev_err(tas_priv->dev, "%s, no such channel(%d)\n", __func__,
			chn);
	}

out:
	return ret;
}
EXPORT_SYMBOL_GPL(tasdevice_dev_bulk_write);

static void tasdev_dsp_prog_blk_remove(struct tasdevice_prog *prog)
{
	struct tasdevice_data *tas_dt;
	struct tasdev_blk *blk;
	unsigned int i;

	if (!prog)
		return;

	tas_dt = &(prog->dev_data);

	if (!tas_dt->dev_blks)
		return;

	for (i = 0; i < tas_dt->nr_blk; i++) {
		blk = &(tas_dt->dev_blks[i]);
		kfree(blk->data);
	}
	kfree(tas_dt->dev_blks);
}

static void tasdev_dsp_prog_remove(struct tasdevice_prog *prog,
	unsigned short nr)
{
	int i;

	for (i = 0; i < nr; i++)
		tasdev_dsp_prog_blk_remove(&prog[i]);
	kfree(prog);
}

static void tasdev_dsp_cfg_blk_remove(struct tasdevice_config *cfg)
{
	struct tasdevice_data *tas_dt;
	struct tasdev_blk *blk;
	unsigned int i;

	if (cfg) {
		tas_dt = &(cfg->dev_data);

		if (!tas_dt->dev_blks)
			return;

		for (i = 0; i < tas_dt->nr_blk; i++) {
			blk = &(tas_dt->dev_blks[i]);
			kfree(blk->data);
		}
		kfree(tas_dt->dev_blks);
	}
}

static void tasdev_dsp_cfg_remove(struct tasdevice_config *config,
	unsigned short nr)
{
	int i;

	for (i = 0; i < nr; i++)
		tasdev_dsp_cfg_blk_remove(&config[i]);
	kfree(config);
}

void tasdevice_dsp_remove(void *context)
{
	struct tasdevice_priv *tas_dev = (struct tasdevice_priv *) context;
	struct tasdevice_fw *tas_fmw = tas_dev->fmw;

	if (!tas_dev->fmw)
		return;

	if (tas_fmw->programs)
		tasdev_dsp_prog_remove(tas_fmw->programs,
			tas_fmw->nr_programs);
	if (tas_fmw->configs)
		tasdev_dsp_cfg_remove(tas_fmw->configs,
			tas_fmw->nr_configurations);
	kfree(tas_fmw);
	tas_dev->fmw = NULL;
}
EXPORT_SYMBOL_GPL(tasdevice_dsp_remove);

void tasdevice_remove(struct tasdevice_priv *tas_priv)
{
	mutex_destroy(&tas_priv->codec_lock);
}
EXPORT_SYMBOL_GPL(tasdevice_remove);

MODULE_DESCRIPTION("TAS2781 common library");
MODULE_AUTHOR("Shenghao Ding, TI, <shenghao-ding@ti.com>");
MODULE_LICENSE("GPL");
