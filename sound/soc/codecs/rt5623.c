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

#define RT5623_PROC
#ifdef RT5623_PROC
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#endif

#define MODEM_ON 1
#define MODEM_OFF 0

static struct i2c_client *i2c_client;
static int status;

static int codec_write(struct i2c_client *client, unsigned int reg,
			      unsigned int value)
{
	u8 data[3];

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
	xfer[0].scl_rate = 100 * 1000;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 2;
	xfer[1].buf = (u8 *)&data;
	xfer[1].scl_rate = 100 * 1000;

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
	{RT5623_PWR_MANAG_ADD3			, 0x8000},
	{RT5623_PWR_MANAG_ADD2			, 0x2000},
	{RT5623_LINE_IN_VOL			, 0xa808},
	{RT5623_STEREO_DAC_VOL			, 0x6808},
	{RT5623_OUTPUT_MIXER_CTRL		, 0x1400},
	{RT5623_ADC_REC_GAIN			, 0xf58b},
	{RT5623_ADC_REC_MIXER			, 0x7d7d},
	{RT5623_AUDIO_INTERFACE			, 0x8083},
	{RT5623_STEREO_AD_DA_CLK_CTRL		, 0x0a2d},
	{RT5623_PWR_MANAG_ADD1			, 0x8000},
	{RT5623_PWR_MANAG_ADD2			, 0xb7f3},
	{RT5623_PWR_MANAG_ADD3			, 0x90c0},
	{RT5623_SPK_OUT_VOL			, 0x0000},
	{RT5623_PLL_CTRL			, 0x481f},
	{RT5623_GLOBAL_CLK_CTRL_REG		, 0x8000},
	{RT5623_STEREO_AD_DA_CLK_CTRL		, 0x3a2d},
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
	if(status == MODEM_OFF)	
	{
		printk("enter %s\n",__func__);
		rt5623_reset(i2c_client);
		rt5623_reg_init(i2c_client);
		status = MODEM_ON;
	}
}
EXPORT_SYMBOL(rt5623_on);

void rt5623_off(void)
{
	if(status == MODEM_ON)	
	{
		printk("enter %s\n",__func__);
		codec_write(i2c_client, RT5623_SPK_OUT_VOL, 0x8080);
		rt5623_reset(i2c_client);
		codec_write(i2c_client, RT5623_PWR_MANAG_ADD3, 0x0000);
		codec_write(i2c_client, RT5623_PWR_MANAG_ADD2, 0x0000);
		codec_write(i2c_client, RT5623_PWR_MANAG_ADD1, 0x0000);
		status = MODEM_OFF;
	}
}
EXPORT_SYMBOL(rt5623_off);

static const struct i2c_device_id rt5623_i2c_id[] = {
	{ "rt5623", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5623_i2c_id);

static int rt5623_proc_init(void);

static int __devinit rt5623_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	pr_info("%s(%d)\n", __func__, __LINE__);

	#ifdef RT5623_PROC	
	rt5623_proc_init();
	#endif

	i2c_client = i2c;
	rt5623_reset(i2c);
	status = MODEM_ON;
	rt5623_off( );
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
late_initcall(rt5623_modinit);

static void __exit rt5623_modexit(void)
{
	i2c_del_driver(&rt5623_i2c_driver);
}
module_exit(rt5623_modexit);

MODULE_DESCRIPTION("ASoC RT5623 driver");
MODULE_AUTHOR("Johnny Hsu <johnnyhsu@realtek.com>");
MODULE_LICENSE("GPL");


#ifdef RT5623_PROC

static ssize_t rt5623_proc_write(struct file *file, const char __user *buffer,
		unsigned long len, void *data)
{
	char *cookie_pot; 
	char *p;
	int reg;
	int value;

	cookie_pot = (char *)vmalloc( len );
	if (!cookie_pot) 
	{
		return -ENOMEM;
	} 
	else 
	{
		if (copy_from_user( cookie_pot, buffer, len )) 
			return -EFAULT;
	}

	switch(cookie_pot[0])
	{
		case 'r':
		case 'R':
			printk("Read reg debug\n");		
			if(cookie_pot[1] ==':')
			{
				strsep(&cookie_pot,":");
				while((p=strsep(&cookie_pot,",")))
				{
					reg = simple_strtol(p,NULL,16);
					value = codec_read(i2c_client,reg);
					printk("codec_read:0x%04x = 0x%04x\n",reg,value);
				}
				printk("\n");
			}
			else
			{
				printk("Error Read reg debug.\n");
				printk("For example: echo r:22,23,24,25>rt5623_ts\n");
			}
			break;
		case 'w':
		case 'W':
			printk("Write reg debug\n");		
			if(cookie_pot[1] ==':')
			{
				strsep(&cookie_pot,":");
				while((p=strsep(&cookie_pot,"=")))
				{
					reg = simple_strtol(p,NULL,16);
					p=strsep(&cookie_pot,",");
					value = simple_strtol(p,NULL,16);
					codec_write(i2c_client,reg,value);
					printk("codec_write:0x%04x = 0x%04x\n",reg,value);
				}
				printk("\n");
			}
			else
			{
				printk("Error Write reg debug.\n");
				printk("For example: w:22=0,23=0,24=0,25=0>rt5623_ts\n");
			}
			break;
		case 'a':
			printk("Dump reg \n");		

			for(reg = 0; reg < 0x6e; reg+=2)
			{
				value = codec_read(i2c_client,reg);
				printk("codec_read:0x%04x = 0x%04x\n",reg,value);
			}

			break;		
		default:
			printk("Help for rt5623_ts .\n-->The Cmd list: \n");
			printk("-->'d&&D' Open or Off the debug\n");
			printk("-->'r&&R' Read reg debug,Example: echo 'r:22,23,24,25'>rt5623_ts\n");
			printk("-->'w&&W' Write reg debug,Example: echo 'w:22=0,23=0,24=0,25=0'>rt5623_ts\n");
			break;
	}

	return len;
}

static const struct file_operations rt5623_proc_fops = {
	.owner		= THIS_MODULE,
};

static int rt5623_proc_init(void)
{
	struct proc_dir_entry *rt5623_proc_entry;
	rt5623_proc_entry = create_proc_entry("driver/rt5623_ts", 0777, NULL);
	if(rt5623_proc_entry != NULL)
	{
		rt5623_proc_entry->write_proc = rt5623_proc_write;
		return 0;
	}
	else
	{
		printk("create proc error !\n");
		return -1;
	}
}
#endif





