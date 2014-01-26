/*
 * linux/sound/soc/codecs/tlv320aic3111.c
 *
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
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
 * Rev 0.1   ASoC driver support    Mistral         14-04-2010
 * 
 * Rev 0.2   Updated based Review Comments Mistral      29-06-2010 
 *
 * Rev 0.3   Updated for Codec Family Compatibility     12-07-2010
 */

/*
 ***************************************************************************** 
 * Include Files
 ***************************************************************************** 
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/of_gpio.h>
#include <linux/seq_file.h>
#include <linux/spi/spi.h>
#include <linux/clk.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

//#include <mach/gpio.h>
#include "tlv320aic3111.h"

#if 0
#define	AIC_DBG(x...)	printk(KERN_INFO x)
#else
#define	AIC_DBG(x...)	do { } while (0)
#endif

#define GPIO_HIGH 1
#define GPIO_LOW 0
#define INVALID_GPIO -1

/* codec status */
#define AIC3110_IS_SHUTDOWN	0
#define AIC3110_IS_CAPTURE_ON	1
#define AIC3110_IS_PLAYBACK_ON	2
#define AIC3110_IS_INITPOWER_ON	4

/* work type */
#define AIC3110_POWERDOWN_NULL	0
#define AIC3110_POWERDOWN_PLAYBACK	1
#define AIC3110_POWERDOWN_CAPTURE	2
#define AIC3110_POWERDOWN_PLAYBACK_CAPTURE	3
#define JACK_DET_ADLOOP         msecs_to_jiffies(200)

#define SPK 1
#define HP 0

int aic3111_spk_ctl_gpio = INVALID_GPIO;
int aic3111_hp_det_gpio = INVALID_GPIO;

static int aic3111_power_speaker (bool on);
struct speaker_data {
     struct timer_list timer;
     struct semaphore sem;
};
enum 
{
	POWER_STATE_OFF = 0,
	POWER_STATE_ON,

	POWER_STATE_SW_HP = 0,
	POWER_STATE_SW_SPK,
}; 

static void aic3111_work(struct work_struct *work);

static struct workqueue_struct *aic3111_workq;
static DECLARE_DELAYED_WORK(delayed_work, aic3111_work);
static int aic3111_current_status = AIC3110_IS_SHUTDOWN, aic3111_work_type = AIC3110_POWERDOWN_NULL;
static bool isHSin = true, isSetHW = false;
int old_status = SPK;
/*
 ***************************************************************************** 
 * Global Variables
 ***************************************************************************** 
 */
/* Used to maintain the Register Access control*/
static u8 aic3111_reg_ctl;

static struct snd_soc_codec *aic3111_codec;
struct aic3111_priv *aic3111_privdata;
struct i2c_client *aic3111_i2c;

  /* add a timer for checkout HP or SPK*/
static struct timer_list aic3111_timer;

/*Used to delay work hpdet switch irq handle*/
struct delayed_work aic3111_hpdet_work;

#ifdef CONFIG_MINI_DSP
extern int aic3111_minidsp_program (struct snd_soc_codec *codec);
extern void aic3111_add_minidsp_controls (struct snd_soc_codec *codec);
#endif

/*
 *	AIC3111 register cache
 *	We are caching the registers here.
 *	NOTE: In AIC3111, there are 61 pages of 128 registers supported.
 * 	The following table contains the page0, page1 and page2 registers values.
 */

#ifdef AIC3111_CODEC_SUPPORT
static const u8 aic31xx_reg[AIC31xx_CACHEREGNUM] = {
/* Page 0 Registers */
/* 0  */ 0x00, 0x00, 0x12, 0x00, 0x00, 0x11, 0x04, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x01, 0x00, 0x80, 0x80,
/* 10 */ 0x08, 0x00, 0x01, 0x01, 0x80, 0x80, 0x04, 0x00, 0x01, 0x00, 0x00,
  0x00, 0x01, 0x00, 0x00, 0x00,
/* 20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0x55, 0x55, 0x00, 0x00,
  0x00, 0x01, 0x01, 0x00, 0x14,
/* 40 */ 0x0c, 0x00, 0x00, 0x00, 0x6f, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xee, 0x10, 0xd8, 0x7e, 0xe3,
/* 50 */ 0x00, 0x00, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 60 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 70 */ 0x00, 0x00, 0x10, 0x32, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x12, 0x02,
/* Page 1 Registers */
/* 0 */ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x80, 0x80, 0x00, 0x00, 0x00,
/* 40 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 50 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 60 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 70 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
};

#elif defined(AIC3110_CODEC_SUPPORT)
/**************** AIC3110 REG CACHE ******************/
static const u8 aic31xx_reg[AIC31xx_CACHEREGNUM] = {
/* Page 0 Registers */
0x00, 0x00, 0x01, 0x56, 0x00, 0x11, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x80, 0x80, 
0x08, 0x00, 0x01, 0x01, 0x80, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x02, 0x32, 0x12, 0x03, 0x02, 0x02, 0x11, 0x10, 0x00, 0x01, 0x04, 0x00, 0x14, 
0x0c, 0x00, 0x00, 0x00, 0x0f, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0xee, 0x10, 0xd8, 0x7e, 0xe3, 
0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
/* Page 1 Registers */
0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 
0x06, 0x3e, 0x00, 0x00, 0x7f, 0x7f, 0x7f, 0x7f, 0x02, 0x02, 0x00, 0x00, 0x20, 0x86, 0x00, 0x80, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
}; /**************************** End of AIC3110 REG CAHE ******************/

#elif defined(AIC3100_CODEC_SUPPORT)
/******************************* AIC3100 REG CACHE ***********************/
static const u8 aic31xx_reg[AIC31xx_CACHEREGNUM] = {
/* Page 0 Registers */
/* 0  */ 0x00, 0x00, 0x12, 0x00, 0x00, 0x11, 0x04, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x01, 0x00, 0x80, 0x00,
/* 10 */ 0x00, 0x00, 0x01, 0x01, 0x80, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
  0x00, 0x01, 0x00, 0x00, 0x00,
/* 20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0x55, 0x55, 0x00, 0x00,
  0x00, 0x01, 0x01, 0x00, 0x14,
/* 40 */ 0x0c, 0x00, 0x00, 0x00, 0x6f, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xee, 0x10, 0xd8, 0x7e, 0xe3,
/* 50 */ 0x00, 0x00, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 60 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 70 */ 0x00, 0x00, 0x10, 0x32, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x12, 0x02,
/* Page 1 Registers */
/* 0 */ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x80, 0x80, 0x00, 0x00, 0x00,
/* 40 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 50 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 60 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 70 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
}; /**************************** End of AIC3100 REG CACHE ******************/

#else /*#ifdef AIC3120_CODEC_SUPPORT */
static const u8 aic31xx_reg[AIC31xx_CACHEREGNUM] = {
/* Page 0 Registers */
/* 0  */ 0x00, 0x00, 0x12, 0x00, 0x00, 0x11, 0x04, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x01, 0x00, 0x80, 0x80,
/* 10 */ 0x08, 0x00, 0x01, 0x01, 0x80, 0x80, 0x04, 0x00, 0x01, 0x00, 0x00,
  0x00, 0x01, 0x00, 0x00, 0x00,
/* 20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0x55, 0x55, 0x00, 0x00,
  0x00, 0x01, 0x01, 0x00, 0x14,
/* 40 */ 0x0c, 0x00, 0x00, 0x00, 0x6f, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xee, 0x10, 0xd8, 0x7e, 0xe3,
/* 50 */ 0x00, 0x00, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 60 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 70 */ 0x00, 0x00, 0x10, 0x32, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x12, 0x02,
/* Page 1 Registers */
/* 0 */ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x80, 0x80, 0x00, 0x00, 0x00,
/* 40 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 50 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 60 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
/* 70 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
};

#endif

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_change_page
 * Purpose  : This function is to switch between page 0 and page 1.
 *            
 *----------------------------------------------------------------------------
 */
static int aic3111_change_page (struct snd_soc_codec *codec, u8 new_page)
{
	struct aic3111_priv *aic3111 = aic3111_privdata;
	u8 data[2];

	if (new_page == 2 || new_page > 8) {
		printk("ERROR::codec do not have page %d !!!!!!\n", new_page);
		return -1;
	}

	data[0] = 0;
	data[1] = new_page;
	aic3111->page_no = new_page;

	if (codec->hw_write (codec->control_data, data, 2) != 2)
	{
		printk ("Error in changing page to %d \n", new_page);
		return -1;
	}

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_write_reg_cache
 * Purpose  : This function is to write aic3111 register cache
 *            
 *----------------------------------------------------------------------------
 */
static inline void aic3111_write_reg_cache (struct snd_soc_codec *codec, u16 reg, u8 value)
{
	u8 *cache = codec->reg_cache;

	if (reg >= AIC31xx_CACHEREGNUM)
	{
		return;
	}

	cache[reg] = value;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_read
 * Purpose  : This function is to read the aic3111 register space.
 *            
 *----------------------------------------------------------------------------
 */
static unsigned int aic3111_read (struct snd_soc_codec *codec, unsigned int reg)
{
	struct aic3111_priv *aic3111 = aic3111_privdata;
	u8 value;
	u8 page = reg / 128;

	if (page == 2 || page > 8) {
		printk("aic3111_read::Error page, there's not page %d in codec tlv320aic3111 !!!\n", page);
		return -1;
	}

	reg = reg % 128;

	if (aic3111->page_no != page)
	{
		aic3111_change_page (codec, page);
	}

	i2c_master_send (codec->control_data, (char *) &reg, 1);
	i2c_master_recv (codec->control_data, &value, 1);

	return value;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_write
 * Purpose  : This function is to write to the aic3111 register space.
 *            
 *----------------------------------------------------------------------------
 */
static int aic3111_write (struct snd_soc_codec *codec, unsigned int reg, unsigned int value)
{
	struct aic3111_priv *aic3111 = aic3111_privdata;
	u8 data[2];
	u8 page;
        //printk("enter %s!!!!!!\n",__FUNCTION__);
	//printk("aic3111_hp_det_gpio =%d!!!!!!\n",gpio_get_value(aic3111_hp_det_gpio));
	page = reg / 128;
 	data[AIC3111_REG_OFFSET_INDEX] = reg % 128;

	if (page == 2 || page > 9) {
		printk("aic3111_write::Error page, there's not page %d in codec tlv320aic3111 !!!\n", page);
		return -1;
	}

	if (aic3111->page_no != page)
	{
		aic3111_change_page (codec, page);
	}

	/* data is
	*   D15..D8 aic3111 register offset
	*   D7...D0 register data
	*/
	data[AIC3111_REG_DATA_INDEX] = value & AIC3111_8BITS_MASK;
#if defined(EN_REG_CACHE)
	if ((page == 0) || (page == 1))
	{
		aic3111_write_reg_cache (codec, reg, value);
	}
#endif
	if (codec->hw_write (codec->control_data, data, 2) != 2)
	{
		printk ("Error in i2c write\n");
		return -EIO;
	}

	return 0;
}

static int aic3111_print_register_cache (struct platform_device *pdev)
{
	struct snd_soc_codec *codec = aic3111_codec;
	u8 *cache = codec->reg_cache;
	int reg;

	printk ("\n========3110 reg========\n");
	for (reg = 0; reg < codec->reg_size; reg++) 
	{
		if (reg == 0) printk ("Page 0\n");
		if (reg == 128) printk ("\nPage 1\n");
		if (reg%16 == 0 && reg != 0 && reg != 128) printk ("\n");
		printk("0x%02x, ",aic3111_read(codec,reg));
	}
	printk ("\n========3110 cache========\n");
	for (reg = 0; reg < codec->reg_size; reg++) 
	{
		if (reg == 0) printk ("Page 0\n");
		if (reg == 128) printk ("\nPage 1\n");
		if (reg%16 == 0 && reg != 0 && reg != 128) printk ("\n");
		printk ("0x%02x, ",cache[reg]);
	}
	printk ("\n==========================\n");
	return 0;
}

static void aic3111_soft_reset (void)
{
	struct snd_soc_codec *codec = aic3111_codec;

	AIC_DBG("CODEC::%s\n",__FUNCTION__);

	//aic3111_write (codec, 1, 0x01);
	aic3111_write (codec, 63, 0x00);
	gpio_set_value(aic3111_spk_ctl_gpio, GPIO_LOW);
	msleep(10);
	aic3111_write (aic3111_codec, (68), 0x01); //disable DRC
	aic3111_write (aic3111_codec, (128 + 31), 0xc4);
	aic3111_write (aic3111_codec, (128 + 36), 0x28); //Left Analog Vol to HPL
	aic3111_write (aic3111_codec, (128 + 37), 0x28); //Right Analog Vol to HPL
	aic3111_write (aic3111_codec, (128 + 40), 0x4f); //HPL driver PGA
	aic3111_write (aic3111_codec, (128 + 41), 0x4f); //HPR driver PGA
	aic3111_power_speaker(POWER_STATE_OFF);	
	mdelay (20);
	aic3111_write (codec, 1, 0x00);

	memcpy(codec->reg_cache, aic31xx_reg,
	       sizeof(aic31xx_reg));

	isSetHW = false;

	return;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_set_bias_level
 * Purpose  : This function is to get triggered when dapm events occurs.
 *            
 *----------------------------------------------------------------------------
 */
static int aic3111_set_bias_level(struct snd_soc_codec *codec,
			enum snd_soc_bias_level level)
{
	struct aic3111_priv *aic3111 = aic3111_privdata;
	u8 value;

	if (isSetHW)
		return 0;

	AIC_DBG ("CODEC::%s>>>>>>level:%d>>>>master:%d\n", __FUNCTION__, level, aic3111->master);

  	switch (level) {
    	 /* full On */
	case SND_SOC_BIAS_ON:
      	/* all power is driven by DAPM system */
      		if (aic3111->master)
		{
	 		 /* Switch on PLL */
	 		value = aic3111_read(codec, CLK_REG_2);
	 		aic3111_write(codec, CLK_REG_2, (value | ENABLE_PLL));

	  		/* Switch on NDAC Divider */
	  		value = aic3111_read(codec, NDAC_CLK_REG);
	  		aic3111_write(codec, NDAC_CLK_REG, value | ENABLE_NDAC);

	  		/* Switch on MDAC Divider */
	  		value = aic3111_read(codec, MDAC_CLK_REG);
	 		aic3111_write(codec, MDAC_CLK_REG, value | ENABLE_MDAC);

	  		/* Switch on NADC Divider */
	  		value = aic3111_read(codec, NADC_CLK_REG);
	  		aic3111_write(codec, NADC_CLK_REG, value | ENABLE_MDAC);

	 		/* Switch on MADC Divider */
	 		value = aic3111_read(codec, MADC_CLK_REG);
	  		aic3111_write(codec, MADC_CLK_REG, value | ENABLE_MDAC);

	 		/* Switch on BCLK_N Divider */
	 		value = aic3111_read(codec, BCLK_N_VAL);
	  		aic3111_write(codec, BCLK_N_VAL, value | ENABLE_BCLK);
		} else {
	  		/* Switch on PLL */
	  		value = aic3111_read(codec, CLK_REG_2);
	 		aic3111_write(codec, CLK_REG_2, (value | ENABLE_PLL));

	  		/* Switch on NDAC Divider */
	  		value = aic3111_read(codec, NDAC_CLK_REG);
	  		aic3111_write(codec, NDAC_CLK_REG, value | ENABLE_NDAC);

	  		/* Switch on MDAC Divider */
	  		value = aic3111_read(codec, MDAC_CLK_REG);
	  		aic3111_write(codec, MDAC_CLK_REG, value | ENABLE_MDAC);

	  		/* Switch on NADC Divider */
	  		value = aic3111_read(codec, NADC_CLK_REG);
	  		aic3111_write(codec, NADC_CLK_REG, value | ENABLE_MDAC);

	  		/* Switch on MADC Divider */
	  		value = aic3111_read(codec, MADC_CLK_REG);
	  		aic3111_write(codec, MADC_CLK_REG, value | ENABLE_MDAC);

	  		/* Switch on BCLK_N Divider */
	  		value = aic3111_read(codec, BCLK_N_VAL);
	  		aic3111_write(codec, BCLK_N_VAL, value | ENABLE_BCLK);
		}
		break;

	/* partial On */
	case SND_SOC_BIAS_PREPARE:
		break;

	/* Off, with power */
	case SND_SOC_BIAS_STANDBY:
		/*
		 * all power is driven by DAPM system,
		 * so output power is safe if bypass was set
		 */
		if (aic3111->master)
		{
			/* Switch off PLL */
			value = aic3111_read(codec, CLK_REG_2);
			aic3111_write(codec, CLK_REG_2, (value & ~ENABLE_PLL));

			/* Switch off NDAC Divider */
			value = aic3111_read(codec, NDAC_CLK_REG);
			aic3111_write(codec, NDAC_CLK_REG, value & ~ENABLE_NDAC);

			/* Switch off MDAC Divider */
			value = aic3111_read(codec, MDAC_CLK_REG);
			aic3111_write(codec, MDAC_CLK_REG, value & ~ENABLE_MDAC);

			/* Switch off NADC Divider */
			value = aic3111_read(codec, NADC_CLK_REG);
			aic3111_write(codec, NADC_CLK_REG, value & ~ENABLE_NDAC);

			/* Switch off MADC Divider */
			value = aic3111_read(codec, MADC_CLK_REG);
			aic3111_write(codec, MADC_CLK_REG, value & ~ENABLE_MDAC);
			value = aic3111_read(codec, BCLK_N_VAL);

			/* Switch off BCLK_N Divider */
			aic3111_write(codec, BCLK_N_VAL, value & ~ENABLE_BCLK);
		}
		break;

	/* Off, without power */
	case SND_SOC_BIAS_OFF:
		/* force all power off */
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

/* the structure contains the different values for mclk */
static const struct aic3111_rate_divs aic3111_divs[] = {
/* 
 * mclk, rate, p_val, pll_j, pll_d, dosr, ndac, mdac, aosr, nadc, madc, blck_N, 
 * codec_speficic_initializations 
 */
  /* 8k rate */
  {12000000, 8000, 1, 7, 6800, 768,  5, 3, 128, 5, 18, 24},
  //{12288000, 8000, 1, 7, 8643, 768,  5, 3, 128, 5, 18, 24},
  {24000000, 8000, 2, 7, 6800, 768, 15, 1,  64, 45, 4, 24},
  /* 11.025k rate */
  {12000000, 11025, 1, 7, 5264, 512,  8, 2, 128,  8, 8, 16},
  {24000000, 11025, 2, 7, 5264, 512, 16, 1,  64, 32, 4, 16},
  /* 16k rate */
  {12000000, 16000, 1, 7, 6800, 384,  5, 3, 128,  5, 9, 12},
  {24000000, 16000, 2, 7, 6800, 384, 15, 1,  64, 18, 5, 12},
  /* 22.05k rate */
  {12000000, 22050, 1, 7, 5264, 256,  4, 4, 128,  4, 8, 8},
  {24000000, 22050, 2, 7, 5264, 256, 16, 1,  64, 16, 4, 8},
  /* 32k rate */
  {12000000, 32000, 1, 7, 1680, 192, 2, 7, 64, 2, 21, 6},
  {24000000, 32000, 2, 7, 1680, 192, 7, 2, 64, 7,  6, 6},
  /* 44.1k rate */
  {12000000, 44100, 1, 7, 5264, 128, 2, 8, 128, 2, 8, 4},
  {11289600, 44100, 1, 8,    0, 128, 4, 4, 128, 4, 4, 4},
  {24000000, 44100, 2, 7, 5264, 128, 8, 2,  64, 8, 4, 4},
  /* 48k rate */
  {12000000, 48000, 1, 8, 1920, 128, 2, 8, 128, 2, 8, 4},
  {24000000, 48000, 2, 8, 1920, 128, 8, 2,  64, 8, 4, 4},
  /*96k rate */
  {12000000, 96000, 1, 8, 1920, 64, 2, 8, 64, 2, 8, 2},
  {24000000, 96000, 2, 8, 1920, 64, 4, 4, 64, 8, 2, 2},
  /*192k */
  {12000000, 192000, 1, 8, 1920, 32, 2, 8, 32, 2, 8, 1},
  {24000000, 192000, 2, 8, 1920, 32, 4, 4, 32, 4, 4, 1},
};

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_get_divs
 * Purpose  : This function is to get required divisor from the "aic3111_divs"
 *            table.
 *            
 *----------------------------------------------------------------------------
 */
static inline int aic3111_get_divs (int mclk, int rate)
{
	int i;

	AIC_DBG("Enter::%s\n",__FUNCTION__);

	for (i = 0; i < ARRAY_SIZE (aic3111_divs); i++)
	{
		if ((aic3111_divs[i].rate == rate) && (aic3111_divs[i].mclk == mclk))
		{
			return i;
		}
	}

	printk ("Master clock and sample rate is not supported\n");
	return -EINVAL;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_hw_params
 * Purpose  : This function is to set the hardware parameters for AIC3111.
 *            The functions set the sample rate and audio serial data word 
 *            length.
 *            
 *----------------------------------------------------------------------------
 */
static int aic3111_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params,
                       struct snd_soc_dai *codec_dai)
{
	struct snd_soc_codec *codec = aic3111_codec;
	struct aic3111_priv *aic3111 = aic3111_privdata;
	int i;
	u8 data;

	if (isSetHW)
		return 0;

	AIC_DBG("CODEC::%s\n", __FUNCTION__);

	aic3111_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	i = aic3111_get_divs(aic3111->sysclk, params_rate (params));

	if (i < 0) {
		printk ("sampling rate not supported\n");
		return i;
	}

	/* We will fix R value to 1 and will make P & J=K.D as varialble */

	/* Setting P & R values */
	aic3111_write(codec, CLK_REG_2, ((aic3111_divs[i].p_val << 4) | 0x01));

	/* J value */
	aic3111_write(codec, CLK_REG_3, aic3111_divs[i].pll_j);

	/* MSB & LSB for D value */
	aic3111_write(codec, CLK_REG_4, (aic3111_divs[i].pll_d >> 8));
	aic3111_write(codec, CLK_REG_5, (aic3111_divs[i].pll_d & AIC3111_8BITS_MASK));

	/* NDAC divider value */
	aic3111_write(codec, NDAC_CLK_REG, aic3111_divs[i].ndac);

	/* MDAC divider value */
	aic3111_write(codec, MDAC_CLK_REG, aic3111_divs[i].mdac);

	/* DOSR MSB & LSB values */
	aic3111_write(codec, DAC_OSR_MSB, aic3111_divs[i].dosr >> 8);
	aic3111_write(codec, DAC_OSR_LSB, aic3111_divs[i].dosr & AIC3111_8BITS_MASK);

	/* NADC divider value */
	aic3111_write(codec, NADC_CLK_REG, aic3111_divs[i].nadc);

	/* MADC divider value */
	aic3111_write(codec, MADC_CLK_REG, aic3111_divs[i].madc);

	/* AOSR value */
	aic3111_write(codec, ADC_OSR_REG, aic3111_divs[i].aosr);

	/* BCLK N divider */
	aic3111_write(codec, BCLK_N_VAL, aic3111_divs[i].blck_N);

	aic3111_set_bias_level(codec, SND_SOC_BIAS_ON);

	data = aic3111_read(codec, INTERFACE_SET_REG_1);

	data = data & ~(3 << 4);

	switch (params_format (params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		data |= (AIC3111_WORD_LEN_20BITS << DATA_LEN_SHIFT);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data |= (AIC3111_WORD_LEN_24BITS << DATA_LEN_SHIFT);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		data |= (AIC3111_WORD_LEN_32BITS << DATA_LEN_SHIFT);
		break;
	}

	aic3111_write(codec, INTERFACE_SET_REG_1, data);

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_mute
 * Purpose  : This function is to mute or unmute the left and right DAC
 *            
 *----------------------------------------------------------------------------
 */
static int aic3111_mute (struct snd_soc_dai *codec_dai, int mute)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u8 dac_reg;

	AIC_DBG ("CODEC::%s>>>>mute:%d\n", __FUNCTION__, mute);

	dac_reg = aic3111_read (codec, DAC_MUTE_CTRL_REG) & ~MUTE_ON;
	if (mute)
		;//aic3111_write (codec, DAC_MUTE_CTRL_REG, dac_reg | MUTE_ON);
	else {
		//aic3111_write (codec, DAC_MUTE_CTRL_REG, dac_reg);
		isSetHW = true;
	}

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_set_dai_sysclk
 * Purpose  : This function is to set the DAI system clock
 *            
 *----------------------------------------------------------------------------
 */
static int aic3111_set_dai_sysclk (struct snd_soc_dai *codec_dai,
			int clk_id, unsigned int freq, int dir)
{
	struct aic3111_priv *aic3111 = aic3111_privdata;

	if (isSetHW)
		return 0;

	AIC_DBG("Enter %s and line %d\n",__FUNCTION__,__LINE__);

	switch (freq) {
	case AIC3111_FREQ_11289600:
	case AIC3111_FREQ_12000000:
	case AIC3111_FREQ_24000000:
		aic3111->sysclk = freq;
		return 0;
	}

	printk ("Invalid frequency to set DAI system clock\n");
	return -EINVAL;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_set_dai_fmt
 * Purpose  : This function is to set the DAI format
 *            
 *----------------------------------------------------------------------------
 */
static int aic3111_set_dai_fmt (struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct aic3111_priv *aic3111 = aic3111_privdata;
	u8 iface_reg;

	if (isSetHW)
		return 0;

	AIC_DBG("Enter %s and line %d\n",__FUNCTION__,__LINE__);

	iface_reg = aic3111_read (codec, INTERFACE_SET_REG_1);
	iface_reg = iface_reg & ~(3 << 6 | 3 << 2);   //set I2S mode BCLK and WCLK is input

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		aic3111->master = 1;
		iface_reg |= BIT_CLK_MASTER | WORD_CLK_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		aic3111->master = 0;
		iface_reg &= ~(BIT_CLK_MASTER | WORD_CLK_MASTER);
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		aic3111->master = 0;
		iface_reg |= BIT_CLK_MASTER;
		iface_reg &= ~(WORD_CLK_MASTER);
		break;
	default:
		printk ("Invalid DAI master/slave interface\n");
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface_reg |= (AIC3111_DSP_MODE << AUDIO_MODE_SHIFT);
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		iface_reg |= (AIC3111_RIGHT_JUSTIFIED_MODE << AUDIO_MODE_SHIFT);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface_reg |= (AIC3111_LEFT_JUSTIFIED_MODE << AUDIO_MODE_SHIFT);
		break;
	default:
		printk ("Invalid DAI interface format\n");
		return -EINVAL;
	}

	aic3111_write (codec, INTERFACE_SET_REG_1, iface_reg);

	return 0;
}


/*
 *----------------------------------------------------------------------------
 * Function : aic3111_power_headphone
 * Purpose  : 
 * parameter: on = 1: power up;	
 *            on = 0: power dn;	
 * xjq@rock-chips.com     
 *----------------------------------------------------------------------------
 */
static int aic3111_power_headphone (bool on)
{
	struct snd_soc_codec *codec = aic3111_codec;

	AIC_DBG("Enter %s and line %d\n",__FUNCTION__,__LINE__);

	if (on == POWER_STATE_ON) {
		aic3111_write (codec, (63), 0xd4);
//		aic3111_write(codec, (128 + 35), 0x88);
		aic3111_write (codec, (68), 0x01); //disable DRC
		aic3111_write (codec, (128 + 31), 0xc4);
		aic3111_write (codec, (128 + 44), 0x00);
		aic3111_write (codec, (128 + 36), 0x28); //Left Analog Vol to HPL
		aic3111_write (codec, (128 + 37), 0x28); //Right Analog Vol to HPL
//		aic3111_write (codec, (128 + 40), 0x06); //HPL driver PGA
//		aic3111_write (codec, (128 + 41), 0x06); //HPR driver PGA
		aic3111_write (codec, (128 + 40), 0x4f); //HPL driver PGA
		aic3111_write (codec, (128 + 41), 0x4f); //HPR driver PGA

	} else if (on == POWER_STATE_OFF) {

		aic3111_write (codec, (128 + 31), 0x00);
		aic3111_write (codec, (128 + 44), 0x00);
		aic3111_write (codec, (128 + 36), 0xff);
		aic3111_write (codec, (128 + 37), 0xff);
		aic3111_write (codec, (128 + 40), 0x02);
		aic3111_write (codec, (128 + 41), 0x02);
	}

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_power_speaker
 * Purpose  : 
 * parameter: on = 1: power up;	
 *            on = 0: power dn;	
 * xjq@rock-chips.com     
 *----------------------------------------------------------------------------
 */
static int aic3111_power_speaker (bool on)
{
	struct snd_soc_codec *codec = aic3111_codec;

	AIC_DBG("Enter %s and line %d\n",__FUNCTION__,__LINE__);

	if (on == POWER_STATE_ON) {
#if 0
//		aic3111_write(codec, (128 + 32), 0x86);
		aic3111_write(codec, (128 + 32), 0xc6);
		aic3111_write(codec, (128 + 30), 0x00);  
//		aic3111_write(codec, (128 + 38), 0x08); //set left speaker analog gain to -4.88db
		aic3111_write(codec, (128 + 38), 0x16); //set left speaker analog gain to -4.88db
		aic3111_write(codec, (128 + 39), 0x16); //Right Analog Vol to SPR
//		aic3111_write(codec, (128 + 38), 0x7f); //set left speaker analog gain to -4.88db
//		aic3111_write(codec, (128 + 39), 0x7f); //Right Analog Vol to SPR
		aic3111_write(codec, (128 + 42), 0x1d); //set left speaker driver gain to 12db
		aic3111_write(codec, (128 + 43), 0x1d); //bit3-4 output stage gain
//		aic3111_write(codec, (128 + 43), 0x00); //bit3-4 output stage gain
		aic3111_write(codec, (37), 0x98);

#if 1		/* DRC */
		aic3111_write(codec, (60), 0x02); //select PRB_P2
		aic3111_write(codec, (68), 0x61); //enable left and right DRC, set DRC threshold to -3db, set DRC hystersis to 1db
		aic3111_write(codec, (69), 0x00); //set hold time disable
		aic3111_write(codec, (70), 0x5D); //set attack time to 0.125db per sample period and decay time to 0.000488db per sample
#endif
#endif
#if 1
		aic3111_write(codec, (63), 0xfc);
		aic3111_write(codec, (128 + 32), 0xc6);
		aic3111_write(codec, (128 + 30), 0x00);
		aic3111_write(codec, (128 + 39), 0x08); //set left speaker analog gain to -4.88db
		aic3111_write(codec, (128 + 38), 0x08); //Right Analog Vol to SPR
		aic3111_write(codec, (128 + 43), 0x0D); //set left speaker driver gain to 12db
		aic3111_write(codec, (128 + 42), 0x0D); //bit3-4 output stage gain
		aic3111_write(codec, (37), 0x99);

#if 1		/* DRC */
		aic3111_write(codec, (60), 0x02); //select PRB_P2
		aic3111_write(codec, (68), 0x61); //enable left and right DRC, set DRC threshold to -3db, set DRC hystersis to 1db
		aic3111_write(codec, (69), 0x00); //set hold time disable
		aic3111_write(codec, (70), 0x5D); //set attack time to 0.125db per sample period and decay time to 0.000488db per sample
#endif
#endif
	} else if (on == POWER_STATE_OFF) {

		aic3111_write(codec, (68), 0x01); //disable DRC
		aic3111_write(codec, (128 + 32), 0x06);
		aic3111_write(codec, (128 + 30), 0x00);
		aic3111_write(codec, (128 + 38), 0xff);
		aic3111_write(codec, (128 + 39), 0xff);
		aic3111_write(codec, (128 + 42), 0x00);
		aic3111_write(codec, (128 + 43), 0x00);
		aic3111_write(codec, (37), 0x00);
	}

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_HS_switch
 * Purpose  : This function is to initialise the AIC3111 driver
 *            In PLAYBACK, switch between HP and SPK app.	
 * parameter: on = 1: SPK power up & HP  power dn;	
 *            on = 0: HP  power up & SPK power dn;		
 * xjq@rock-chips.com     
 *----------------------------------------------------------------------------
 */
static int aic3111_HS_switch (bool on)
{
	AIC_DBG("enter %s and line %d\n",__FUNCTION__,__LINE__);

	if (POWER_STATE_SW_SPK == on) {

		//aic3111_power_headphone (POWER_STATE_OFF);
		aic3111_power_speaker (POWER_STATE_ON);
	} else if (POWER_STATE_SW_HP == on) {

		aic3111_power_speaker (POWER_STATE_OFF);
		//aic3111_power_headphone (POWER_STATE_ON);

		//aic3111_power_speaker (POWER_STATE_ON);
		//aic3111_power_headphone (POWER_STATE_OFF);
	}

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_SPK_HS_powerdown
 * Purpose  : This function is to power down HP and SPK.			
 * xjq@rock-chips.com
 *----------------------------------------------------------------------------
 */
static int aic3111_SPK_HS_powerdown (void)
{
	AIC_DBG("enter %s and line %d\n",__FUNCTION__,__LINE__);

	//aic3111_power_headphone (POWER_STATE_OFF);
	aic3111_power_speaker (POWER_STATE_OFF);
//	aic3111_power_speaker (POWER_STATE_ON);

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_power_init
 * Purpose  : pll clock setting
 * parameter: on = 1: power up;	
 *            on = 0: power dn;	
 * xjq@rock-chips.com     
 *----------------------------------------------------------------------------
 */
static void aic3111_power_init (void)
{
	struct snd_soc_codec *codec = aic3111_codec;

	AIC_DBG("enter %s and line %d\n",__FUNCTION__,__LINE__);

	if (!(aic3111_current_status & AIC3110_IS_INITPOWER_ON)) {

		AIC_DBG ("CODEC::%s\n", __FUNCTION__);

		aic3111_write(codec, (128 + 46), 0x0b);
		aic3111_write(codec, (128 + 35), 0x44);
		aic3111_write(codec,  (4), 0x03);
		aic3111_write(codec, (29), 0x01);
		aic3111_write(codec, (48), 0xC0);
		aic3111_write(codec, (51), 0x14);
		aic3111_write(codec, (67), 0x82);

		aic3111_current_status |= AIC3110_IS_INITPOWER_ON;
	}

	return;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_power_playback
 * Purpose  : 
 * parameter: on = 1: power up;	
 *            on = 0: power dn;	
 * xjq@rock-chips.com     
 *----------------------------------------------------------------------------
 */
static int aic3111_power_playback (bool on)
{
	struct snd_soc_codec *codec = aic3111_codec;

	AIC_DBG ("CODEC::%s>>>>>>%d\n", __FUNCTION__, on);
	gpio_set_value(aic3111_spk_ctl_gpio, GPIO_LOW);
	aic3111_power_init();

	if ((on == POWER_STATE_ON) &&
	    !(aic3111_current_status & AIC3110_IS_PLAYBACK_ON)) {
//	if(1){
		//gpio_set_value(aic3111_spk_ctl_gpio, GPIO_HIGH);

		/****open HPL and HPR*******/
		//aic3111_write(codec, (63), 0xfc);
		msleep(10);
		aic3111_write(codec, (65), 0x00); //LDAC VOL
		aic3111_write(codec, (66), 0x00); //RDAC VOL
		aic3111_write (aic3111_codec, (63), 0xd4);
//		aic3111_write(codec, (128 + 35), 0x88);

		//aic3111_write (aic3111_codec, (68), 0x01); //disable DRC
		//aic3111_write (aic3111_codec, (128 + 31), 0xc4);
		aic3111_write (aic3111_codec, (128 + 44), 0x00);
		//aic3111_write (aic3111_codec, (128 + 36), 0x28); //Left Analog Vol to HPL
		//aic3111_write (aic3111_codec, (128 + 37), 0x28); //Right Analog Vol to HPL
		aic3111_write (codec, (128 + 40), 0x06); //HPL driver PGA
		aic3111_write (codec, (128 + 41), 0x06); //HPR driver PGA
		//aic3111_write (aic3111_codec, (128 + 40), 0x4f); //HPL driver PGA
		//aic3111_write (aic3111_codec, (128 + 41), 0x4f); //HPR driver PGA
		//printk("HP INIT~~~~~~~~~~~~~~~~~~~~~~~~~`\n");
		/***************************/		
		
		aic3111_HS_switch(isHSin);

		aic3111_write(codec, (65), 0x10); //LDAC VOL to +8db
		aic3111_write(codec, (66), 0x10); //RDAC VOL to +8db
		msleep(10);
		aic3111_write(codec, (64), 0x00);
		
		aic3111_current_status |= AIC3110_IS_PLAYBACK_ON;

	} else if ((on == POWER_STATE_OFF) &&
	           (aic3111_current_status & AIC3110_IS_PLAYBACK_ON)) {

		aic3111_write(codec, (68), 0x01); //disable DRC
		aic3111_write(codec, (64), 0x0c);
		aic3111_write(codec, (63), 0x00);
		aic3111_write(codec, (65), 0x00); //LDAC VOL
		aic3111_write(codec, (66), 0x00); //RDAC VOL

		aic3111_SPK_HS_powerdown();

		aic3111_current_status &= ~AIC3110_IS_PLAYBACK_ON;
	}
	//mdelay(800);
	gpio_set_value(aic3111_spk_ctl_gpio, GPIO_HIGH);

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_power_capture
 * Purpose  : 
 * parameter: on = 1: power up;	
 *            on = 0: power dn;	
 * xjq@rock-chips.com     
 *----------------------------------------------------------------------------
 */
static int aic3111_power_capture (bool on)
{
	struct snd_soc_codec *codec = aic3111_codec;

	AIC_DBG ("CODEC::%s>>>>>>%d\n", __FUNCTION__, on);

	aic3111_power_init();

	if ((on == POWER_STATE_ON) &&
	    !(aic3111_current_status & AIC3110_IS_CAPTURE_ON)) {
		aic3111_write(codec, (64), 0x0c);
		msleep(10);

		aic3111_write(codec, (61), 0x0b);
		aic3111_write(codec, (128 + 47), 0x00); //MIC PGA 0x80:0dB  0x14:10dB  0x28:20dB  0x3c:30dB 0x77:59dB
		aic3111_write(codec, (128 + 48), 0x80); //MIC1LP\MIC1LM RIN = 10.
		aic3111_write(codec, (128 + 49), 0x20);
		aic3111_write(codec, (82), 0x00); //D7=0:0: ADC channel not muted
		aic3111_write(codec, (83), 0x1A); //ADC Digital Volume 0 dB
		aic3111_write(codec, (81), 0x80); //D7=1:ADC channel is powered up.

#if 1
		/*configure register to creat a filter 20~3.5kHz*/

		aic3111_write(codec, (128*4 + 14), 0x7f);
		aic3111_write(codec, (128*4 + 15), 0x00);
		aic3111_write(codec, (128*4 + 16), 0xc0);
		aic3111_write(codec, (128*4 + 17), 0x18);
		aic3111_write(codec, (128*4 + 18), 0x00);

		aic3111_write(codec, (128*4 + 19), 0x00);
		aic3111_write(codec, (128*4 + 20), 0x3f);
		aic3111_write(codec, (128*4 + 21), 0x00);
		aic3111_write(codec, (128*4 + 22), 0x00);
		aic3111_write(codec, (128*4 + 23), 0x00);

		aic3111_write(codec, (128*4 + 24), 0x05);
		aic3111_write(codec, (128*4 + 25), 0xd2);
		aic3111_write(codec, (128*4 + 26), 0x05);
		aic3111_write(codec, (128*4 + 27), 0xd2);
		aic3111_write(codec, (128*4 + 28), 0x05);

		aic3111_write(codec, (128*4 + 29), 0xd2);
		aic3111_write(codec, (128*4 + 30), 0x53);
		aic3111_write(codec, (128*4 + 31), 0xff);
		aic3111_write(codec, (128*4 + 32), 0xc0);
		aic3111_write(codec, (128*4 + 33), 0xb5);
#endif
		msleep(10);
		aic3111_write(codec, (64), 0x00);
		aic3111_current_status |= AIC3110_IS_CAPTURE_ON;

	} else if ((on == POWER_STATE_OFF) &&
	           (aic3111_current_status & AIC3110_IS_CAPTURE_ON)) {

		aic3111_write(codec, (61), 0x00);
		aic3111_write(codec, (128 + 47), 0x00); //MIC PGA AOL
		aic3111_write(codec, (128 + 48), 0x00);
		aic3111_write(codec, (128 + 50), 0x00);
		aic3111_write(codec, (81), 0x00);
		aic3111_write(codec, (82), 0x80);
		aic3111_write(codec, (83), 0x00); //ADC VOL
		aic3111_write(codec, (86), 0x00);

		aic3111_current_status &= ~AIC3110_IS_CAPTURE_ON;
	}

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_powerdown
 * Purpose  : This function is to power down codec.
 *            
 *----------------------------------------------------------------------------
 */
static void aic3111_powerdown (void)
{
	AIC_DBG ("CODEC::%s\n", __FUNCTION__);

	if (aic3111_current_status != AIC3110_IS_SHUTDOWN) {
		aic3111_soft_reset();//sai
		aic3111_current_status = AIC3110_IS_SHUTDOWN;
	}
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_work
 * Purpose  : This function is to respond to HPDET handle.
 *            
 *----------------------------------------------------------------------------
 */
static void aic3111_work (struct work_struct *work)
{
	AIC_DBG("Enter %s and line %d\n",__FUNCTION__,__LINE__);

	switch (aic3111_work_type) {
	case AIC3110_POWERDOWN_NULL:
		break;
	case AIC3110_POWERDOWN_PLAYBACK:
		aic3111_power_playback(POWER_STATE_OFF);
		break;
	case AIC3110_POWERDOWN_CAPTURE:
		aic3111_power_capture(POWER_STATE_OFF);
		break;
	case AIC3110_POWERDOWN_PLAYBACK_CAPTURE:
		aic3111_powerdown();//sai
		break;
	default:
		break;
	}

	aic3111_work_type = AIC3110_POWERDOWN_NULL;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_startup
 * Purpose  : This function is to start up codec.
 *            
 *----------------------------------------------------------------------------
 */
static int aic3111_startup (struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
/*
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = aic3111_codec;
*/

	AIC_DBG ("CODEC::%s----substream->stream:%s \n", __FUNCTION__,
		   substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? "PLAYBACK":"CAPTURE");

	cancel_delayed_work_sync(&delayed_work);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {

		aic3111_power_playback(POWER_STATE_ON);

	} else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {

		aic3111_power_capture(POWER_STATE_ON);
	}

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_startup
 * Purpose  : This function is to shut down codec.
 *            
 *----------------------------------------------------------------------------
 */
static void aic3111_shutdown (struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_dai *codec_dai = dai;

	AIC_DBG ("CODEC::%s----substream->stream:%s \n", __FUNCTION__,
		   substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? "PLAYBACK":"CAPTURE");

	if (!codec_dai->capture_active && !codec_dai->playback_active) {

		cancel_delayed_work_sync(&delayed_work);

		/* If codec is already shutdown, return */
		if (aic3111_current_status == AIC3110_IS_SHUTDOWN)
			return;

		AIC_DBG ("CODEC::Is going to power down aic3111\n");

		aic3111_work_type = AIC3110_POWERDOWN_PLAYBACK_CAPTURE;

		/* If codec is useless, queue work to close it */
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			queue_delayed_work(aic3111_workq, &delayed_work,
				msecs_to_jiffies(1000));
		}
		else {
			queue_delayed_work(aic3111_workq, &delayed_work,
				msecs_to_jiffies(3000));
		}
	} 
	else if (codec_dai->capture_active && !codec_dai->playback_active) {

		cancel_delayed_work_sync(&delayed_work);

		aic3111_work_type = AIC3110_POWERDOWN_PLAYBACK;

		/* Turn off playback and keep record on */
		queue_delayed_work(aic3111_workq, &delayed_work,
			msecs_to_jiffies(1000));
	} 
	else if (!codec_dai->capture_active && codec_dai->playback_active) {

		cancel_delayed_work_sync(&delayed_work);

		aic3111_work_type = AIC3110_POWERDOWN_CAPTURE;

		/* Turn off record and keep playback on */
		queue_delayed_work(aic3111_workq, &delayed_work,
			msecs_to_jiffies(3000));
	}
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_trigger
 * Purpose  : This function is to respond to playback trigger.
 *            
 *----------------------------------------------------------------------------
 */
static int aic3111_trigger(struct snd_pcm_substream *substream,
			  int status,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_dai *codec_dai = dai;

	if(status == 0)
	{
		gpio_set_value(aic3111_spk_ctl_gpio, GPIO_LOW);
		mdelay(10);
	}

	AIC_DBG ("CODEC::%s----status = %d substream->stream:%s \n", __FUNCTION__, status,
		   substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? "PLAYBACK":"CAPTURE");	

	if (status == 1 || status == 0) {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			codec_dai->playback_active = status;
		} else {
			codec_dai->capture_active = status;
		}
	}

	return 0;
}

static struct snd_soc_dai_ops aic3111_dai_ops = {
	.hw_params = aic3111_hw_params,
	.digital_mute = aic3111_mute,
	.set_sysclk = aic3111_set_dai_sysclk,
	.set_fmt = aic3111_set_dai_fmt,
	.startup	= aic3111_startup,
	.shutdown	= aic3111_shutdown,
	.trigger	= aic3111_trigger,
};

static struct snd_soc_dai_driver aic3111_dai[] = {
	{
		.name = "AIC3111 HiFi",
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AIC3111_RATES,
			.formats = AIC3111_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AIC3111_RATES,
			.formats = AIC3111_FORMATS,
	 	},
		.ops = &aic3111_dai_ops,
	},
};

struct delayed_work aic3111_speaker_delayed_work;
int speakeronoff;

static void  aic3111_speaker_delayed_work_func(struct work_struct  *work)
{
	struct snd_soc_codec *codec = aic3111_codec;

	if (aic3111_current_status & AIC3110_IS_PLAYBACK_ON){
		if(speakeronoff) {

			 //aic3111_write(codec, (128 + 32), 0xc6);
			//printk("reg 128+32 = %x\n"aic3111_read(codec, (128 + 32)));
			isHSin = 0;
			//gpio_set_value(aic3111_spk_ctl_gpio, GPIO_LOW);
			aic3111_power_speaker(POWER_STATE_OFF);
			gpio_set_value(aic3111_spk_ctl_gpio, GPIO_HIGH);
			//aic3111_power_headphone(POWER_STATE_ON);
			//aic3111_write(codec, (128 + 35), 0x88);
			printk("now hp sound\n");
		} else {

			//aic3111_power_speaker(POWER_STATE_ON);

			isHSin = 1;
			//aic3111_power_headphone(POWER_STATE_OFF);
			gpio_set_value(aic3111_spk_ctl_gpio, GPIO_LOW);
			aic3111_power_speaker(POWER_STATE_ON);
			aic3111_write(codec, (128 + 35), 0x44);
			aic3111_write(codec, (63), 0xfc);
			printk("now spk sound\n");

		}
	}    
        //printk("----------------------------mma7660_work_func------------------------\n");
    
}

/**for check hp or spk****/
static int speaker_timer(unsigned long _data)
{
	struct speaker_data *spk = (struct speaker_data *)_data;
	int new_status;

	if (gpio_get_value(aic3111_hp_det_gpio) == 0) {
		new_status = HP;
		isHSin = 0;
		//printk("hp now\n");
		if(old_status != new_status)
		{
			old_status = new_status;
			// printk("new_status = %d,old_status = %d\n",new_status,old_status);
			old_status = new_status;

			schedule_delayed_work(&aic3111_speaker_delayed_work,msecs_to_jiffies(30));
			speakeronoff=1;
			//printk("HS RUN!!!!!!!!!!\n");
		}
	}

	if (gpio_get_value(aic3111_hp_det_gpio) == 1) {
		new_status = SPK;
		isHSin = 1;
		//printk("speak now\n");
		if(old_status != new_status)
		{
			old_status = new_status;
			printk("new_status = %d,old_status = %d\n",new_status,old_status);
			old_status = new_status;

			schedule_delayed_work(&aic3111_speaker_delayed_work,msecs_to_jiffies(30));
			speakeronoff=0;
			//printk("HS RUN!!!!!!!!!!\n");
		}
	}

	mod_timer(&spk->timer, jiffies + msecs_to_jiffies(200));

	return 0;
}
/*
 *----------------------------------------------------------------------------
 * Function : aic3111_probe
 * Purpose  : This is first driver function called by the SoC core driver.
 *            
 *----------------------------------------------------------------------------
 */
static int aic3111_probe (struct snd_soc_codec *codec)
{
	int ret = 0;//, flags, hp_det_irq;

	codec->hw_write = (hw_write_t) i2c_master_send;
	codec->control_data = aic3111_i2c;
	aic3111_codec = codec;

#if 1
	gpio_set_value(aic3111_hp_det_gpio,1);
	struct speaker_data *spk;

	spk = kzalloc(sizeof(struct speaker_data), GFP_KERNEL);
	if (spk == NULL) {
		printk("Allocate Memory Failed!\n");
		ret = -ENOMEM;
		//goto exit_gpio_free;
	}

	setup_timer(&spk->timer, speaker_timer, (unsigned long)spk);
	mod_timer(&spk->timer, jiffies + JACK_DET_ADLOOP);
	INIT_DELAYED_WORK(&aic3111_speaker_delayed_work, aic3111_speaker_delayed_work_func);

/*********************/
	//pio_set_value(aic3111_spk_ctl_gpio, GPIO_LOW);
	//aic3111_power_speaker(POWER_STATE_OFF);
	//aic3111_power_headphone(POWER_STATE_ON);
#endif

	aic3111_workq = create_freezable_workqueue("aic3111");
	if (aic3111_workq == NULL) {
		return -ENOMEM;
	}

/*	INIT_DELAYED_WORK (&aic3111_hpdet_work, aic3111_hpdet_work_handle);
	if (gpio_request (HP_DET_PIN, "hp_det")) {      
		gpio_free (HP_DET_PIN);   
		printk ("CODEC::tlv3110 hp det  pin request error\n");       
	}	
	else {
		gpio_direction_input (HP_DET_PIN);
		gpio_pull_updown (HP_DET_PIN, PullDisable);
		hp_det_irq = gpio_to_irq (HP_DET_PIN);
		isHSin = gpio_get_value (HP_DET_PIN);
		
		flags = isHSin ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;
		ret = request_irq (hp_det_irq, aic3111_hpdet_isr, flags, "hpdet", codec);
		if (ret < 0) {
			printk ("CODEC::request hp_det_irq error\n");
		}
	}
*/
	/* Just Reset codec */
	aic3111_soft_reset();
	gpio_set_value(aic3111_spk_ctl_gpio, GPIO_LOW);
	msleep(10);
	aic3111_write (aic3111_codec, (68), 0x01); //disable DRC
	aic3111_write (aic3111_codec, (128 + 31), 0xc4);
	aic3111_write (aic3111_codec, (128 + 36), 0x28); //Left Analog Vol to HPL
	aic3111_write (aic3111_codec, (128 + 37), 0x28); //Right Analog Vol to HPL
	aic3111_write (aic3111_codec, (128 + 40), 0x4f); //HPL driver PGA
	aic3111_write (aic3111_codec, (128 + 41), 0x4f); //HPR driver PGA

	aic3111_set_bias_level (codec, SND_SOC_BIAS_STANDBY);

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_remove
 * Purpose  : to remove aic3111 soc device 
 *            
 *----------------------------------------------------------------------------
 */
static int aic3111_remove (struct snd_soc_codec *codec)
{
	AIC_DBG ("CODEC::%s\n", __FUNCTION__);

	/* Disable HPDET irq */
	//disable_irq_nosync (HP_DET_PIN);

	/* power down chip */
	aic3111_set_bias_level (codec, SND_SOC_BIAS_OFF);

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_suspend
 * Purpose  : This function is to suspend the AIC3111 driver.
 *            
 *----------------------------------------------------------------------------
 */
static int aic3111_suspend (struct snd_soc_codec *codec)
{

	AIC_DBG ("CODEC::%s\n", __FUNCTION__);

	aic3111_set_bias_level (codec, SND_SOC_BIAS_STANDBY);

	aic3111_soft_reset();//sai

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3111_resume
 * Purpose  : This function is to resume the AIC3111 driver
 *            
 *----------------------------------------------------------------------------
 */
static int aic3111_resume (struct snd_soc_codec *codec)
{
	//isHSin = gpio_get_value(HP_DET_PIN);
	aic3111_set_bias_level (codec, SND_SOC_BIAS_STANDBY);
	//aic3111_set_bias_level(codec, codec->suspend_bias_level);

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * @struct  snd_soc_codec_device |
 *          This structure is soc audio codec device sturecute which pointer
 *          to basic functions aic3111_probe(), aic3111_remove(),  
 *          aic3111_suspend() and aic3111_resume()
 *----------------------------------------------------------------------------
 */
static struct snd_soc_codec_driver soc_codec_dev_aic3111 = {
	.probe = aic3111_probe,
	.remove = aic3111_remove,
	.suspend = aic3111_suspend,
	.resume = aic3111_resume,
	.set_bias_level = aic3111_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(aic31xx_reg),
	.reg_word_size = sizeof(u16),
	.reg_cache_default = aic31xx_reg,
	.reg_cache_step = 1,
};

static const struct i2c_device_id tlv320aic3111_i2c_id[] = {
	{ "aic3111", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tlv320aic3111_i2c_id);

/*
dts:
	codec@1a {
		compatible = "aic3111";
		reg = <0x1a>;
		spk-ctl-gpio = <&gpio6 GPIO_B5 GPIO_ACTIVE_HIGH>;
		hp-det-pio = <&gpio6 GPIO_B6 GPIO_ACTIVE_HIGH>;
	};
*/
static int tlv320aic3111_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	struct aic3111_priv *aic3111;
	int ret;

	aic3111 = kzalloc(sizeof(struct aic3111_priv), GFP_KERNEL);
	if (NULL == aic3111)
		return -ENOMEM;

	aic3111_i2c = i2c;

#ifdef CONFIG_OF
	aic3111_spk_ctl_gpio= of_get_named_gpio_flags(i2c->dev.of_node, "spk-ctl-gpio", 0, NULL);
	if (aic3111_spk_ctl_gpio < 0) {
		printk("%s() Can not read property spk-ctl-gpio\n", __FUNCTION__);
		aic3111_spk_ctl_gpio = INVALID_GPIO;
	}

	aic3111_hp_det_gpio = of_get_named_gpio_flags(i2c->dev.of_node, "hp-det-pio", 0, NULL);
	if (aic3111_hp_det_gpio < 0) {
		printk("%s() Can not read property hp-det-pio\n", __FUNCTION__);
		aic3111_hp_det_gpio = INVALID_GPIO;
	}
#endif //#ifdef CONFIG_OF

	i2c_set_clientdata(i2c, aic3111);

	aic3111_privdata = aic3111;

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_aic3111,
			aic3111_dai, ARRAY_SIZE(aic3111_dai));
	if (ret < 0)
		kfree(aic3111);

	return ret;
}

static int tlv320aic3111_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

struct i2c_driver tlv320aic3111_i2c_driver = {
	.driver = {
		.name = "AIC3111",
		.owner = THIS_MODULE,
	},
	.probe = tlv320aic3111_i2c_probe,
	.remove   = tlv320aic3111_i2c_remove,
	.id_table = tlv320aic3111_i2c_id,
};

static int __init tlv320aic3111_init (void)
{
	return i2c_add_driver(&tlv320aic3111_i2c_driver);
}

static void __exit tlv320aic3111_exit (void)
{
	i2c_del_driver(&tlv320aic3111_i2c_driver);
}

module_init (tlv320aic3111_init);
module_exit (tlv320aic3111_exit);

MODULE_DESCRIPTION (" ASoC TLV320AIC3111 codec driver ");
MODULE_AUTHOR (" Jaz B John <jazbjohn@mistralsolutions.com> ");
MODULE_LICENSE ("GPL");

