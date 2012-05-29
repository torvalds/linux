/*
 * linux/sound/soc/codecs/AIC3262_tiload.c
 *
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 *
 *
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * History:
 *
 * Rev 0.1 	 Tiload support    		TI         16-09-2010
 *
 *          The Tiload programming support is added to AIC3262.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/control.h>
#include <linux/switch.h>
#include <sound/jack.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <linux/mfd/tlv320aic3262-core.h>
#include "tlv320aic326x.h"
#include "aic326x_tiload.h"

/* enable debug prints in the driver */
//#define DEBUG
#undef DEBUG

#ifdef DEBUG
#define dprintk(x...) 	printk(x)
#else
#define dprintk(x...)
#endif

#ifdef AIC3262_TiLoad

/* Function prototypes */
#ifdef REG_DUMP_aic3262
static void aic3262_dump_page(struct i2c_client *i2c, u8 page);
#endif

/* externs */
/*extern int aic3262_change_page(struct snd_soc_codec *codec, u8 new_page);
extern int aic3262_change_book(struct snd_soc_codec *codec, u8 new_book);*/
extern int aic3262_write(struct snd_soc_codec *codec, unsigned int reg,
			 unsigned int value);

int aic3262_driver_init(struct snd_soc_codec *codec);
/************** Dynamic aic3262 driver, TI LOAD support  ***************/

static struct cdev *aic3262_cdev;
static aic326x_reg_union aic_reg;
static int aic3262_major = 0;	/* Dynamic allocation of Mjr No. */
static int aic3262_opened = 0;	/* Dynamic allocation of Mjr No. */
static struct snd_soc_codec *aic3262_codec;
struct class *tiload_class;
static unsigned int magic_num = 0xE0;

/******************************** Debug section *****************************/

#ifdef REG_DUMP_aic3262
/*
 *----------------------------------------------------------------------------
 * Function : aic3262_dump_page
 * Purpose  : Read and display one codec register page, for debugging purpose
 *----------------------------------------------------------------------------
 */
static void aic3262_dump_page(struct i2c_client *i2c, u8 page)
{
	int i;
	u8 data;
	u8 test_page_array[8];

	dprintk("TiLoad DRIVER : %s\n", __FUNCTION__);
//	aic3262_change_page(codec, page);

	data = 0x0;

	i2c_master_send(i2c, data, 1);
	i2c_master_recv(i2c, test_page_array, 8);

	dprintk("\n------- aic3262 PAGE %d DUMP --------\n", page);
	for (i = 0; i < 8; i++) {
		printk(" [ %d ] = 0x%x\n", i, test_page_array[i]);
	}
}
#endif

/*
 *----------------------------------------------------------------------------
 * Function : tiload_open
 *
 * Purpose  : open method for aic3262-tiload programming interface
 *----------------------------------------------------------------------------
 */
static int tiload_open(struct inode *in, struct file *filp)
{
	dprintk("TiLoad DRIVER : %s\n", __FUNCTION__);
	if (aic3262_opened) {
		dprintk("%s device is already opened\n", "aic3262");
		dprintk("%s: only one instance of driver is allowed\n",
		       "aic3262");
		return -1;
	}
	aic3262_opened++;
	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : tiload_release
 *
 * Purpose  : close method for aic3262_tilaod programming interface
 *----------------------------------------------------------------------------
 */
static int tiload_release(struct inode *in, struct file *filp)
{
	dprintk("TiLoad DRIVER : %s\n", __FUNCTION__);
	aic3262_opened--;
	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : tiload_read
 *
 * Purpose  : read method for mini dsp programming interface
 *----------------------------------------------------------------------------
 */
static ssize_t tiload_read(struct file *file, char __user * buf,
			   size_t count, loff_t * offset)
{
	static char rd_data[8];
	char reg_addr;
	size_t size;
	#ifdef DEBUG
	int i;
	#endif
	struct aic3262 *control = aic3262_codec->control_data;

	dprintk("TiLoad DRIVER : %s\n", __FUNCTION__);
	if (count > 128) {
		printk("Max 128 bytes can be read\n");
		count = 128;
	}

	/* copy register address from user space  */
	size = copy_from_user(&reg_addr, buf, 1);
	if (size != 0) {
		printk("read: copy_from_user failure\n");
		return -1;
	}
	/* Send the address to device thats is to be read */
	
	aic_reg.aic326x_register.offset = reg_addr;
	size = aic3262_bulk_read(control, aic_reg.aic326x_register_int, count, rd_data);
/*
	if (i2c_master_send(i2c, &reg_addr, 1) != 1) {
		dprintk("Can not write register address\n");
		return -1;
	}
	size = i2c_master_recv(i2c, rd_data, count);
*/
#ifdef DEBUG
	printk(KERN_ERR "read size = %d, reg_addr= %x , count = %d\n",
	       (int)size, reg_addr, (int)count);
	for (i = 0; i < (int)size; i++) {
		dprintk(KERN_ERR "rd_data[%d]=%x\n", i, rd_data[i]);
	}
#endif
	if (size != count) {
		dprintk("read %d registers from the codec\n", size);
	}

	if (copy_to_user(buf, rd_data, size) != 0) {
		dprintk("copy_to_user failed\n");
		return -1;
	}

	return size;
}

/*
 *----------------------------------------------------------------------------
 * Function : tiload_write
 *
 * Purpose  : write method for aic3262_tiload programming interface
 *----------------------------------------------------------------------------
 */
static ssize_t tiload_write(struct file *file, const char __user * buf,
			    size_t count, loff_t * offset)
{
	static char wr_data[8];
	u8 pg_no;
	unsigned int reg;
	#ifdef DEBUG
	int i;
	#endif
	struct aic3262 *control = aic3262_codec->control_data;

	dprintk("TiLoad DRIVER : %s\n", __FUNCTION__);
	/* copy buffer from user space  */
	if (copy_from_user(wr_data, buf, count)) {
		printk("copy_from_user failure\n");
		return -1;
	}
#ifdef DEBUG
	dprintk(KERN_ERR "write size = %d\n", (int)count);
	for (i = 0; i < (int)count; i++) {
		printk(KERN_INFO "\nwr_data[%d]=%x\n", i, wr_data[i]);
	}
#endif
	if(wr_data[0] == 0)
	{
		//change of page seen, but will only be registered		
		aic_reg.aic326x_register.page = wr_data[1];
		return count;// trick
		
	}	
	else
	if(wr_data[0] == 127 /* && aic_reg.aic326x_register.page == 0*/) 
	{	
		//change of book seen, but will not be sent for I2C write
		aic_reg.aic326x_register.book = wr_data[1];
		return count; //trick
		 
	}
	else
	{ 
		aic_reg.aic326x_register.offset = wr_data[0];	
		aic3262_bulk_write(control, aic_reg.aic326x_register_int, count - 1,&wr_data[1]); 
		return count;
	}
/*	if (wr_data[0] == 0) {
		aic3262_change_page(aic3262_codec, wr_data[1]);
		return count;
	}
	pg_no = aic3262_private->page_no;

	if ((wr_data[0] == 127) && (pg_no == 0)) {
		aic3262_change_book(aic3262_codec, wr_data[1]);
		return count;
	}
	return i2c_master_send(i2c, wr_data, count);*/
	
}

static long tiload_ioctl(struct file *filp,
			unsigned int cmd, unsigned long arg)
{
	long num = 0;
	void __user *argp = (void __user *)arg;
	if (_IOC_TYPE(cmd) != aic3262_IOC_MAGIC)
		return -ENOTTY;

	dprintk("TiLoad DRIVER : %s\n", __FUNCTION__);
	switch (cmd) {
	case aic3262_IOMAGICNUM_GET:
		num = copy_to_user(argp, &magic_num, sizeof(int));
		break;
	case aic3262_IOMAGICNUM_SET:
		num = copy_from_user(&magic_num, argp, sizeof(int));
		break;
	}
	return num;
}

/*********** File operations structure for aic3262-tiload programming *************/
static struct file_operations aic3262_fops = {
	.owner = THIS_MODULE,
	.open = tiload_open,
	.release = tiload_release,
	.read = tiload_read,
	.write = tiload_write,
	.unlocked_ioctl = tiload_ioctl,
};

/*
 *----------------------------------------------------------------------------
 * Function : aic3262_driver_init
 *
 * Purpose  : Register a char driver for dynamic aic3262-tiload programming
 *----------------------------------------------------------------------------
 */
int aic3262_driver_init(struct snd_soc_codec *codec)
{
	int result;

	dev_t dev = MKDEV(aic3262_major, 0);
	dprintk("TiLoad DRIVER : %s\n", __FUNCTION__);
	aic3262_codec = codec;

	dprintk("allocating dynamic major number\n");

	result = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
	if (result < 0) {
		dprintk("cannot allocate major number %d\n", aic3262_major);
		return result;
	}

	tiload_class = class_create(THIS_MODULE, DEVICE_NAME);
	aic3262_major = MAJOR(dev);
	dprintk("allocated Major Number: %d\n", aic3262_major);

	aic3262_cdev = cdev_alloc();
	cdev_init(aic3262_cdev, &aic3262_fops);
	aic3262_cdev->owner = THIS_MODULE;
	aic3262_cdev->ops = &aic3262_fops;

	aic_reg.aic326x_register.page = 0;
	aic_reg.aic326x_register.book = 0;

	if (cdev_add(aic3262_cdev, dev, 1) < 0) {
		dprintk("aic3262_driver: cdev_add failed \n");
		unregister_chrdev_region(dev, 1);
		aic3262_cdev = NULL;
		return 1;
	}
	dprintk("Registered aic3262 TiLoad driver, Major number: %d \n",
	       aic3262_major);
	//class_device_create(tiload_class, NULL, dev, NULL, DEVICE_NAME, 0);
	return 0;
}

#endif
