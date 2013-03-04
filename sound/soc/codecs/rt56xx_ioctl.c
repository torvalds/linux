/*
 * rt56xx_ioctl.h  --  RT56XX ALSA SoC audio driver IO control
 *
 * Copyright 2012 Realtek Microelectronics
 * Author: Bard <bardliao@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/spi/spi.h>
#include <sound/soc.h>
#include "rt56xx_ioctl.h"

static struct rt56xx_ops rt56xx_ioctl_ops;

#if defined(CONFIG_SND_HWDEP) || defined(CONFIG_SND_HWDEP_MODULE)
#define RT_CE_CODEC_HWDEP_NAME "rt56xx hwdep "
static int rt56xx_hwdep_open(struct snd_hwdep *hw, struct file *file)
{
	struct snd_soc_codec *codec = hw->private_data;
	dev_dbg(codec->dev, "%s()\n", __func__);
	return 0;
}

static int rt56xx_hwdep_release(struct snd_hwdep *hw, struct file *file)
{
	struct snd_soc_codec *codec = hw->private_data;
	dev_dbg(codec->dev, "%s()\n", __func__);
	return 0;
}

static int rt56xx_hwdep_ioctl_common(struct snd_hwdep *hw,
		struct file *file, unsigned int cmd, unsigned long arg)
{
	struct snd_soc_codec *codec = hw->private_data;
	struct rt56xx_cmd __user *_rt56xx = (struct rt56xx_cmd *)arg;
	struct rt56xx_cmd rt56xx;
	int *buf, *p;

	if (copy_from_user(&rt56xx, _rt56xx, sizeof(rt56xx))) {
		dev_err(codec->dev,"copy_from_user faild\n");
		return -EFAULT;
	}
	dev_dbg(codec->dev, "%s(): rt56xx.number=%d, cmd=%d\n",
			__func__, rt56xx.number, cmd);
	buf = kmalloc(sizeof(*buf) * rt56xx.number, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;
	if (copy_from_user(buf, rt56xx.buf, sizeof(*buf) * rt56xx.number)) {
		goto err;
	}
	
	switch (cmd) {
	case RT_READ_CODEC_REG_IOCTL:
		for (p = buf; p < buf + rt56xx.number / 2; p++) {
			*(p + rt56xx.number / 2) = snd_soc_read(codec, *p);
		}
		if (copy_to_user(rt56xx.buf, buf, sizeof(*buf) * rt56xx.number))
			goto err;
		break;		

	case RT_WRITE_CODEC_REG_IOCTL:
		for (p = buf; p < buf + rt56xx.number / 2; p++)
			snd_soc_write(codec, *p, *(p + rt56xx.number / 2));
		break;

	case RT_READ_CODEC_INDEX_IOCTL:
		if (NULL == rt56xx_ioctl_ops.index_read)
			goto err;

		for (p = buf; p < buf + rt56xx.number / 2; p++)
			*(p+rt56xx.number/2) = rt56xx_ioctl_ops.index_read(
							codec, *p);
		if (copy_to_user(rt56xx.buf, buf,
			sizeof(*buf) * rt56xx.number))
			goto err;
		break;

	case RT_WRITE_CODEC_INDEX_IOCTL:
		if (NULL == rt56xx_ioctl_ops.index_write)
			goto err;

		for (p = buf; p < buf + rt56xx.number / 2; p++)
			rt56xx_ioctl_ops.index_write(codec, *p,
				*(p+rt56xx.number/2));
		break;		

	default:
		if (NULL == rt56xx_ioctl_ops.ioctl_common)
			goto err;

		rt56xx_ioctl_ops.ioctl_common(hw, file, cmd, arg);
		break;
	}

	kfree(buf);
	return 0;

err:
	kfree(buf);
	return -EFAULT;
}

static int rt56xx_codec_dump_reg(struct snd_hwdep *hw,
		struct file *file, unsigned long arg)
{
	struct snd_soc_codec *codec = hw->private_data;
	struct rt56xx_cmd __user *_rt56xx =(struct rt56xx_cmd *)arg;
	struct rt56xx_cmd rt56xx;
	int i, *buf, number = codec->driver->reg_cache_size;

	dev_dbg(codec->dev, "enter %s, number = %d\n", __func__, number);
	if (copy_from_user(&rt56xx, _rt56xx, sizeof(rt56xx)))
		return -EFAULT;
	
	buf = kmalloc(sizeof(*buf) * number, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	for (i = 0; i < number/2; i++) {
		buf[i] = i << 1;
		buf[i + number / 2] = codec->read(codec, buf[i]);
	}
	if (copy_to_user(rt56xx.buf, buf, sizeof(*buf) * i))
		goto err;
	rt56xx.number = number;
	if (copy_to_user(_rt56xx, &rt56xx, sizeof(rt56xx)))
		goto err;
	kfree(buf);
	return 0;

err:
	kfree(buf);
	return -EFAULT;
}

static int rt56xx_hwdep_ioctl(struct snd_hwdep *hw, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case RT_READ_ALL_CODEC_REG_IOCTL:
		return rt56xx_codec_dump_reg(hw, file, arg);

	default:
		return rt56xx_hwdep_ioctl_common(hw, file, cmd, arg);
	}

	return 0;
}

int realtek_ce_init_hwdep(struct snd_soc_codec *codec)
{
	struct snd_hwdep *hw;
	struct snd_card *card = codec->card->snd_card;
	int err;

	dev_dbg(codec->dev, "enter %s\n", __func__);

	if ((err = snd_hwdep_new(card, RT_CE_CODEC_HWDEP_NAME, 0, &hw)) < 0)
		return err;
	
	strcpy(hw->name, RT_CE_CODEC_HWDEP_NAME);
	hw->private_data = codec;
	hw->ops.open = rt56xx_hwdep_open;
	hw->ops.release = rt56xx_hwdep_release;
	hw->ops.ioctl = rt56xx_hwdep_ioctl;

	return 0;
}
EXPORT_SYMBOL_GPL(realtek_ce_init_hwdep);
#endif

struct rt56xx_ops *rt56xx_get_ioctl_ops(void)
{
	return &rt56xx_ioctl_ops;
}
EXPORT_SYMBOL_GPL(rt56xx_get_ioctl_ops);
