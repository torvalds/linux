/*
* ALSA SoC CX20709 Channel codec driver
*
* Copyright:   (C) 2009/2010 Conexant Systems
*
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* 
*      
*************************************************************************
*  Modified Date:  12/01/11
*  File Version:   2.26.35.11
*************************************************************************
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/module.h>	/* Specifically, a module */
#include <linux/fs.h>
#include <asm/uaccess.h>	/* for get_user and put_user */
//#include <sound/core.h>
//#include <sound/pcm.h>
//#include <sound/pcm_params.h>
#include <sound/soc.h>
//#include <sound/soc-dapm.h>
#include <linux/gpio.h>
#include <linux/slab.h>

//#define DEBUG 1

#include "cxdebug.h"

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
#include <linux/i2c.h>
#endif 

#if defined(CONFIG_SPI_MASTER)
#include <linux/spi/spi.h>
#endif 


/*
* 
 * Is the device open right now? Used to prevent
 * concurent access into the same device 
 */
static int Device_Open = 0;
extern int CX_AUDDRV_VERSION;
struct snd_soc_codec *g_codec = NULL;

/* 
 * This is called whenever a process attempts to open the device file 
 */
static int cxdbg_dev_open(struct inode *inode, struct file *file)
{
#ifdef DEBUG
    printk(KERN_INFO "cxdbg_dev: device_open(%p)\n", file);
#endif

    /* 
    * We don't want to talk to two processes at the same time 
    */
    if (Device_Open)
        return -EBUSY;

    Device_Open++;
    /*
    * Initialize the message 
    */

   // try_module_get(THIS_MODULE);
    return 0;
}

/* 
 * This is called whenever a process attempts to open the device file 
 */
static int cxdbg_dev_release(struct inode *inode, struct file *file)
{
#ifdef DEBUG
    printk(KERN_INFO "cxdbg_dev: device_release(%p)\n", file);
#endif


    Device_Open--;
    /*
    * Initialize the message 
    */

    return 0;
}
static int codec_reg_write(struct CXDBG_IODATA  *reg)
{
    int errno = 0;

    BUG_ON(!g_codec);
    BUG_ON(!g_codec->hw_write);

    if(g_codec&& g_codec->hw_write)
    {
        if( g_codec->hw_write(g_codec,(char*)reg->data,reg->len) 
            !=reg->len)
        {
            errno =-EIO;
            printk(KERN_ERR "cxdbg_dev: codec_reg_write failed\n");
        }
    }
    else
    {
        errno = -EBUSY;
        printk(KERN_ERR "cxdbg_dev: codec_reg_write failed, device is not ready.\n");
    }
    return errno;
}

static unsigned int codec_reg_read(struct CXDBG_IODATA  *reg)
{
    int errno = 0;
    unsigned int regaddr;
    unsigned int data;


    BUG_ON(!g_codec);
    BUG_ON(!g_codec->hw_read);

    if (reg-> len == 2)
    {
        regaddr = (((unsigned int)reg->data[0])<<8) + reg->data[1];
    }
    else if (reg->len == 1)
    {
        regaddr = (unsigned int)reg->data[0];
    }
    else 
    {
       printk(KERN_ERR "cxdbg_dev: codec_reg_read failed, invalid parameter.\n");
       return -EINVAL;
    }
    memset(reg,0,sizeof(*reg));
    if(g_codec && g_codec->hw_read)
    {
        data = g_codec->hw_read(g_codec,regaddr);
        reg->data[0] = data & 0xFF;
        reg->len     = 1;
    }
    else
    {
        errno = -EBUSY;
        printk(KERN_ERR "cxdbg_dev: codec_reg_read failed, device is not ready.\n");
    }
    return errno;
    return 0;
}

long cxdbg_dev_ioctl(struct file * file, unsigned int cmd, unsigned long arg)
{
    struct CXDBG_IODATA  *reg=NULL ;
    int __user *ip = (int __user*) arg;
    long err = -1;
#ifdef DEBUG
    printk(KERN_INFO "cxdbg_dev: ioctl, cmd=0x%02x, arg=0x%02lx\n", cmd, arg);
#endif
    
    /* 
    * Switch according to the ioctl called 
    */
    switch (cmd) {
    case CXDBG_IOCTL_REG_SET:
	reg = (struct CXDBG_IODATA*) kmalloc(sizeof(*reg),GFP_KERNEL);
        err = copy_from_user((char*) reg, (char*)arg,sizeof(*reg));
        if(err==0)
        {
            codec_reg_write(reg);
        }
        break;

    case CXDBG_IOCTL_REG_GET:
	reg = (struct CXDBG_IODATA*) kmalloc(sizeof(*reg),GFP_KERNEL);
        err =copy_from_user((char*) reg, (char*)arg,sizeof(*reg));
        if( err == 0)
        {
           codec_reg_read(reg);
           err = copy_to_user((char*) arg, (char*)reg,sizeof(*reg));
        }
        break;
    case CXDBG_IOCTL_PDRIVER_VERSION:
	err = put_user(CX_AUDDRV_VERSION,ip);
	break;	
    default:
        err =  -EINVAL;
    }

    if(reg)
    {
	kfree(reg);
    }

    return err;
}


#if defined(_MSC_VER)
static const struct file_operations cxdbg_dev_fops =
{
    /*.owner =	*/THIS_MODULE,
    /*.llseek*/NULL,
    /*.read =		*/NULL,
    /*.write*/ NULL,
    /*.aio_read*/ NULL,
    /*.aio_write*/NULL,
    /*readdir*/NULL,
    /*.poll*/NULL,
    /*ioctl*/ NULL /*i2cdev_ioctl*/,
    /*.unlocked_ioctl*/cxdbg_dev_ioctl,
    /*.compat_ioctl*/NULL,
    /*.mmap*/NULL,
    /*.open*/cxdbg_dev_open,
    /*.flush*/NULL,
    /*.release*/NULL,
    /*.fsync*/NULL,
    /*.aio_fsync*/NULL,
    /*.fasync*/NULL,
    /*.lock*/NULL,
    /*.sendpage*/NULL,
};
#else
static const struct file_operations cxdbg_dev_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = cxdbg_dev_ioctl,
    .open           = cxdbg_dev_open,
    .release        = cxdbg_dev_release,
};
#endif


/* 
* Initialize the module - Register the character device 
*/
int cxdbg_dev_init(struct snd_soc_codec *socdev)
{
    int err;
    printk(KERN_INFO "cxdbg_dev: entries driver\n");

    g_codec = socdev;

    err = register_chrdev(CXDBG_MAJOR, CXDBG_DEVICE_NAME, &cxdbg_dev_fops);
    if (err)
    {
        printk(KERN_ERR "cxdbg_dev: Driver Initialisation failed\n");
    }
    return err;
}

void cxdbg_dev_exit(void)
{
    unregister_chrdev(CXDBG_MAJOR,CXDBG_DEVICE_NAME);
}


MODULE_AUTHOR("Simon Ho<simon.ho@conexant.com");
MODULE_DESCRIPTION("debug driver");
MODULE_LICENSE("GPL");

