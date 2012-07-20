/*
 * rt5623.c  --  RT5623 ALSA SoC audio codec driver
 *
 * Copyright 2011 Realtek Semiconductor Corp.
 * Author: Johnny Hsu <johnnyhsu@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rt5623.h"

static struct i2c_client *i2c_client;

static int codec_write(struct i2c_client *client, unsigned int reg,
			      unsigned int value)
{
	u8 data[3];
	int ret;

	data[0] = reg;
	data[1] = (value >> 8) & 0xff;
	data[2] = value & 0xff;

	//printk("%s: reg=0x%x value=0x%x\n",__func__,reg,value);
	if (i2c_master_send(client, data, 3) == 3)
		return 0;
	else
		return -EIO;
}

static unsigned int codec_read(struct i2c_client *client,
					  unsigned int r)
{
	struct i2c_msg xfer[2];
	u8 reg = r;
	u16 data;
	int ret;

	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = 1;
	xfer[0].buf = &reg;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 2;
	xfer[1].buf = (u8 *)&data;

	ret = i2c_transfer(client->adapter, xfer, 2);
	if (ret != 2) {
		dev_err(&client->dev, "i2c_transfer() returned %d\n", ret);
		return 0;
	}
	//printk("%s: reg=0x%x value=0x%x\n",__func__,reg,(data >> 8) | ((data & 0xff) << 8));

	return (data >> 8) | ((data & 0xff) << 8);
}

struct rt5623_reg {
	u8 reg_index;
	u16 reg_value;
};

static struct rt5623_reg init_data[] = {
	{RT5623_PWR_MANAG_ADD3		, 0x8000},
	{RT5623_PWR_MANAG_ADD2		, 0x2000},
	{RT5623_LINE_IN_VOL		, 0xe808},
	{RT5623_STEREO_DAC_VOL		, 0x6808},
	{RT5623_OUTPUT_MIXER_CTRL	, 0x1400},
	{RT5623_ADC_REC_GAIN		, 0xf58b},
	{RT5623_ADC_REC_MIXER		, 0x6f6f},
	{RT5623_AUDIO_INTERFACE		, 0x8000},
	{RT5623_STEREO_AD_DA_CLK_CTRL	, 0x0a2d},
	{RT5623_PWR_MANAG_ADD1		, 0x8000},
	{RT5623_PWR_MANAG_ADD2		, 0x27f3},
	{RT5623_PWR_MANAG_ADD3		, 0x9c00},
	{RT5623_SPK_OUT_VOL		, 0x0000},
};
#define RT5623_INIT_REG_NUM ARRAY_SIZE(init_data)

static int rt5623_reg_init(struct i2c_client *client)
{
	int i;

	for (i = 0; i < RT5623_INIT_REG_NUM; i++)
		codec_write(client, init_data[i].reg_index,
				init_data[i].reg_value);

	return 0;
}

static int rt5623_reset(struct i2c_client *client)
{
	return codec_write(client, RT5623_RESET, 0);
}

void rt5623_on(void)
{
	printk("enter %s\n",__func__);
	rt5623_reset(i2c_client);
	rt5623_reg_init(i2c_client);
}
EXPORT_SYMBOL(rt5623_on);

void rt5623_off(void)
{
	printk("enter %s\n",__func__);
	codec_write(i2c_client, RT5623_SPK_OUT_VOL, 0x8080);
	rt5623_reset(i2c_client);
	codec_write(i2c_client, RT5623_PWR_MANAG_ADD3, 0x0000);
	codec_write(i2c_client, RT5623_PWR_MANAG_ADD2, 0x0000);
	codec_write(i2c_client, RT5623_PWR_MANAG_ADD1, 0x0000);
}
EXPORT_SYMBOL(rt5623_off);

static const struct i2c_device_id rt5623_i2c_id[] = {
	{ "rt5623", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5623_i2c_id);

static int __devinit rt5623_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	pr_info("%s(%d)\n", __func__, __LINE__);

	i2c_client = i2c;
	rt5623_reset(i2c);

	rt5623_on( );

	return 0;
}

static int __devexit rt5623_i2c_remove(struct i2c_client *i2c)
{
	return 0;
}

struct i2c_driver rt5623_i2c_driver = {
	.driver = {
		.name = "rt5623",
		.owner = THIS_MODULE,
	},
	.probe = rt5623_i2c_probe,
	.remove   = __devexit_p(rt5623_i2c_remove),
	.id_table = rt5623_i2c_id,
};

static int __init rt5623_modinit(void)
{
	return i2c_add_driver(&rt5623_i2c_driver);
}
module_init(rt5623_modinit);

static void __exit rt5623_modexit(void)
{
	i2c_del_driver(&rt5623_i2c_driver);
}
module_exit(rt5623_modexit);

MODULE_DESCRIPTION("ASoC RT5623 driver");
MODULE_AUTHOR("Johnny Hsu <johnnyhsu@realtek.com>");
MODULE_LICENSE("GPL");
