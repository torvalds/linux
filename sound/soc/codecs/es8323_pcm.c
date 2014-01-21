/*
 * es8323.c -- es8323 ALSA SoC audio driver
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

#include "es8323.h"

//#define ES8323_PROC
#ifdef ES8323_PROC
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#endif

#if 1
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif

enum {
	OFF,
	RCV,
	SPK_PATH,
	HP_PATH,
	HP_NO_MIC,
	BT,
};

static struct i2c_client *i2c_client;
int es8323_codec_state = OFF;

static int codec_write(struct i2c_client *client, unsigned int reg,
			      unsigned int value)
{
	u8 data[2];

	data[0] = reg;
	data[1] = value & 0x00ff;

	//printk("%s: reg=0x%x value=0x%x\n",__func__,reg,value);
	if (i2c_master_send(client, data, 2) == 2)
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

static int es8323_reg_init(struct i2c_client *client, bool main_mic)
{
	if (es8323_codec_state != OFF) {
		if (main_mic) {
			codec_write(client, 0x0b,0x82);  //ADC INPUT=LIN2/RIN2 //82
		} else {
			codec_write(client, 0x0b,0x02);  //ADC INPUT=LIN1/RIN1 //02
		}

		DBG("es8323_reg_init() change to %s\n",
			main_mic ? "main mic" : "headset mic");
		return 0;
	}
	codec_write(client,  0x35, 0xa0);
	codec_write(client,  0x36, 0xc8); //for 1.8V VDD
	codec_write(client,  0x08, 0x20); //slave 0x00, master 0x80, bclk invert(bit5)
	codec_write(client,  0x02, 0xf3);
	codec_write(client,  0x2b, 0x80); //use ADC LRCK, slave 0x80, master 0xc0
	codec_write(client,  0x00, 0x36); //DACMCLK is the chip master clock source
	codec_write(client,  0x01, 0x72); //all normal
	codec_write(client,  0x03, 0x00); //all normal
	codec_write(client,  0x04, 0x3c); //L/R DAC power up, L/R out1 enable
	codec_write(client,  0x05, 0x00); //normal
	codec_write(client,  0x06, 0x00); //normal
	codec_write(client,  0x07, 0x7c);
	codec_write(client,  0x09, 0x88); //MIC GAIN=24dB
	codec_write(client,  0x0a, 0xf0); //L-R diff
	if (main_mic) {
		codec_write(client, 0x0b,0x82);  //ADC INPUT=LIN2/RIN2 //82
	} else {
		codec_write(client, 0x0b,0x02);  //ADC INPUT=LIN1/RIN1 //02
	}
	codec_write(client,  0x0c, 0x23); //ADC PCM(bit0-1), 18bit(bit2-4), 2nd(bit5)
	codec_write(client,  0x0d, 0x02);
	codec_write(client,  0x0f, 0xf0); //unmute ADC
	codec_write(client,  0x10, 0x00);
	codec_write(client,  0x11, 0x00);
	codec_write(client,  0x12, 0x2a); //ALC off
	codec_write(client,  0x13, 0xC0); //ALC
	codec_write(client,  0x14, 0x05); //ALC
	codec_write(client,  0x15, 0x06); //ALC
	codec_write(client,  0x16, 0x50); //ALC
	codec_write(client,  0x17, 0x06); //DAC PCM(bit1-2), 16bit(bit3-5), 2nd(bit6), lr swap(bit 7)
	codec_write(client,  0x18, 0x02); // MCLK/256
	codec_write(client,  0x19, 0x22);
	codec_write(client,  0x1a, 0x00); //lout digital
	codec_write(client,  0x1b, 0x00); //rout digital
	codec_write(client,  0x26, 0x00);
	codec_write(client,  0x27, 0xb8); //LD2LO to left mixer
	codec_write(client,  0x28, 0x38);
	codec_write(client,  0x29, 0x38);
	codec_write(client,  0x2a, 0xb8); //RD2RO to right mixer
	codec_write(client,  0x30, 0x1e);
	codec_write(client,  0x31, 0x1e);
	codec_write(client,  0x02, 0x00);

	DBG("es8323_reg_init() set codec route with %s\n",
		main_mic ? "main mic" : "headset mic");

	return 0;
}

static int es8323_reset(struct i2c_client *client)
{
	es8323_codec_state = OFF;

	codec_write(client, ES8323_CONTROL1, 0x80);
	return codec_write(client, ES8323_CONTROL1, 0x00);
}

int set_es8323(int cmd)
{
	DBG("%s : set voice_call_path = %d\n", __func__,
		cmd);

	if (i2c_client == NULL) {
		printk("%s : i2c_client is NULL!\n", __func__);
		return -EINVAL;
	}

	switch (cmd) {
	case OFF:
		es8323_reset(i2c_client);
		break;
	case HP_PATH:
		es8323_reg_init(i2c_client, 0);
		break;
	case RCV:
	case SPK_PATH:
	case HP_NO_MIC:
		es8323_reg_init(i2c_client, 1);
		break;
	case BT:
		break;
	default:
		return -EINVAL;
	}

	es8323_codec_state = cmd;

	return 0;
}

EXPORT_SYMBOL(set_es8323);

static const struct i2c_device_id es8323_i2c_id[] = {
	{ "es8323-pcm", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, es8323_i2c_id);

#ifdef ES8323_PROC
static int es8323_proc_init(void);
#endif

static int es8323_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	DBG("%s\n", __func__);

	#ifdef ES8323_PROC	
	es8323_proc_init();
	#endif

	i2c_client = i2c;
	es8323_reset(i2c);

	return 0;
}

static int es8323_i2c_remove(struct i2c_client *i2c)
{
	return 0;
}

struct i2c_driver es8323_i2c_driver = {
	.driver = {
		.name = "es8323-pcm",
		.owner = THIS_MODULE,
	},
	.probe = es8323_i2c_probe,
	.remove   = es8323_i2c_remove,
	.id_table = es8323_i2c_id,
};

static int __init es8323_modinit(void)
{
	return i2c_add_driver(&es8323_i2c_driver);
}
late_initcall(es8323_modinit);

static void __exit es8323_modexit(void)
{
	i2c_del_driver(&es8323_i2c_driver);
}
module_exit(es8323_modexit);

MODULE_DESCRIPTION("ASoC ES8323 driver");
MODULE_AUTHOR("Jear");
MODULE_LICENSE("GPL");


#ifdef ES8323_PROC

static ssize_t es8323_proc_write(struct file *file, const char __user *buffer,
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
				printk("For example: echo r:22,23,24,25>es8323_ts\n");
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
				printk("For example: w:22=0,23=0,24=0,25=0>es8323_ts\n");
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
			printk("Help for es8323_ts .\n-->The Cmd list: \n");
			printk("-->'d&&D' Open or Off the debug\n");
			printk("-->'r&&R' Read reg debug,Example: echo 'r:22,23,24,25'>es8323_ts\n");
			printk("-->'w&&W' Write reg debug,Example: echo 'w:22=0,23=0,24=0,25=0'>es8323_ts\n");
			break;
	}

	return len;
}

static const struct file_operations es8323_proc_fops = {
	.owner		= THIS_MODULE,
};

static int es8323_proc_init(void)
{
	struct proc_dir_entry *es8323_proc_entry;
	es8323_proc_entry = create_proc_entry("driver/es8323_pcm_ts", 0777, NULL);
	if(es8323_proc_entry != NULL)
	{
		es8323_proc_entry->write_proc = es8323_proc_write;
		return 0;
	}
	else
	{
		printk("create proc error !\n");
		return -1;
	}
}
#endif
