/*
 * wm8994.c -- WM8994 ALSA SoC audio driver
 *
 * Copyright 2009 Wolfson Microelectronics plc
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
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
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include <mach/iomux.h>
#include <mach/gpio.h>

#include "wm8994.h"


#if 1
#define DBG(x...) printk(KERN_INFO x)
#else
#define DBG(x...) do { } while (0)
#endif

#define WM8994_TEST

struct i2c_client *wm8994_client;
int reg_send_data(struct i2c_client *client, unsigned short *reg, unsigned short *data, u32 scl_rate);
int reg_recv_data(struct i2c_client *client, unsigned short *reg, unsigned short *buf, u32 scl_rate);

enum wm8994_codec_mode
{
  wm8994_AP_to_Headset,
  wm8994_AP_to_Speakers,
  wm8994_Recorder,
  wm8994_FM_to_Headset,
  wm8994_FM_to_Headset_and_Record,
  wm8994_FM_to_Speakers,
  wm8994_FM_to_Speakers_and_Record,
  wm8994_HandsetMIC_to_Baseband_to_Headset,
  wm8994_HandsetMIC_to_Baseband_to_Headset_and_Record,
  wm8994_MainMIC_to_Baseband_to_Earpiece,
  wm8994_MainMIC_to_Baseband_to_Earpiece_and_Record,
  wm8994_MainMIC_to_Baseband_to_Speakers,
  wm8994_MainMIC_to_Baseband_to_Speakers_and_Record,
  wm8994_BT_Baseband,
  wm8994_BT_Baseband_and_record,
///PCM BB BEGIN
  wm8994_HandsetMIC_to_PCMBaseband_to_Headset,
  wm8994_HandsetMIC_to_PCMBaseband_to_Headset_and_Record,
  wm8994_MainMIC_to_PCMBaseband_to_Earpiece,
  wm8994_MainMIC_to_PCMBaseband_to_Earpiece_and_Record,
  wm8994_MainMIC_to_PCMBaseband_to_Speakers,
  wm8994_MainMIC_to_PCMBaseband_to_Speakers_and_Record,
  wm8994_BT_PCMBaseband,
  wm8994_BT_PCMBaseband_and_record,
///PCM BB END
  null
};

unsigned char wm8994_mode=null;

/* For voice device route set, add by phc  */
enum VoiceDeviceSwitch
{
	SPEAKER_INCALL,
	SPEAKER_NORMAL,
	
	HEADSET_INCALL,
	HEADSET_NORMAL,

	EARPIECE_INCALL,
	EARPIECE_NORMAL,
	
	BLUETOOTH_SCO_INCALL,
	BLUETOOTH_SCO_NORMAL,

	BLUETOOTH_A2DP_INCALL,
	BLUETOOTH_A2DP_NORMAL,
	
	MIC_CAPTURE,
	
	ALL_OPEN,
	ALL_CLOSED
};
/*
 * wm8994 register cache
 * We can't read the WM8994 register space when we
 * are using 2 wire for device control, so we cache them instead.
 */
static const u16 wm8994_reg[] = {
	0x0097, 0x0097, 0x0079, 0x0079,  /*  0 */
	0x0000, 0x0008, 0x0000, 0x000a,  /*  4 */
	0x0000, 0x0000, 0x00ff, 0x00ff,  /*  8 */
	0x000f, 0x000f, 0x0000, 0x0000,  /* 12 */
	0x0000, 0x007b, 0x0000, 0x0032,  /* 16 */
	0x0000, 0x00c3, 0x00c3, 0x00c0,  /* 20 */
	0x0000, 0x0000, 0x0000, 0x0000,  /* 24 */
	0x0000, 0x0000, 0x0000, 0x0000,  /* 28 */
	0x0000, 0x0000, 0x0050, 0x0050,  /* 32 */
	0x0050, 0x0050, 0x0050, 0x0050,  /* 36 */
	0x0079, 0x0079, 0x0079,          /* 40 */
};

/* codec private data */
struct wm8994_priv {
	unsigned int sysclk;
	struct snd_soc_codec codec;
	struct snd_pcm_hw_constraint_list *sysclk_constraints;
	u16 reg_cache[WM8994_NUM_REG];
};

static int wm8994_read(unsigned short reg,unsigned short *value)
{
	unsigned short regs=((reg>>8)&0x00FF)|((reg<<8)&0xFF00),values;

	if (reg_recv_data(wm8994_client,&regs,&values,400000) >= 0)
	{
		*value=((values>>8)& 0x00FF)|((values<<8)&0xFF00);
		DBG("Enter::%s----line->%d--reg=0x%x value=%x\n",__FUNCTION__,__LINE__,reg,*value);
		return 0;
	}
	printk("%s---line->%d:Codec read error!\n",__FUNCTION__,__LINE__);
	return -EIO;
}
	

static int wm8994_write(unsigned short reg,unsigned short value)
{
	unsigned short regs=((reg>>8)&0x00FF)|((reg<<8)&0xFF00),values=((value>>8)&0x00FF)|((value<<8)&0xFF00);

	DBG("Enter::%s----line->%d-- reg=%x--value=%x -- regs=%x--values=%x\n",__FUNCTION__,__LINE__,reg,value,regs,values);

	if (reg_send_data(wm8994_client,&regs,&values,400000)>=0)
		return 0;

	printk("%s---line->%d:Codec write error!\n",__FUNCTION__,__LINE__);
	return -EIO;
}

#define wm8994_reset()	wm8994_write(WM8994_RESET, 0)

void  AP_to_Headset(void)
{
	DBG("Enter::%s----line->%d-- AP_to_Headset\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_AP_to_Headset;
	wm8994_reset();
	mdelay(50);

#if 1
	wm8994_write(0x01,  0x0003);
	mdelay(50);
	wm8994_write(0x221, 0x0700);  
	wm8994_write(0x222, 0x90C2);  //86C2	
	wm8994_write(0x223, 0x00E0);

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302, 0x4000);  // master = 0x4000 // slave= 0x0000
	wm8994_write(0x303, 0x0050);  // master  0x0050 lrck 40.98kHz bclk 2.15MHz
	wm8994_write(0x305, 0x0035);  // master  0x0035 lrck 40.98kHz bclk 2.15MHz
#endif

	wm8994_write(0x220, 0x0005);  
	mdelay(50);
	wm8994_write(0x200, 0x0001);  // sysclk = fll (bit4 =1)   0x0011
  	wm8994_write(0x300, 0x4010);  // i2s 16 bits
  
	wm8994_write(0x01,  0x0303); 
	wm8994_write(0x05,  0x0303);   
	wm8994_write(0x2D,  0x0100);
	wm8994_write(0x2E,  0x0100);
	wm8994_write(0x4C,  0x9F25);
	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x208, 0x000A);	    
	wm8994_write(0x420, 0x0000); 
	wm8994_write(0x601, 0x0001);
	wm8994_write(0x602, 0x0001);
    
	wm8994_write(0x610, 0x0190);  //DAC1 Left Volume bit0~7  		
	wm8994_write(0x611, 0x0190);  //DAC1 Right Volume bit0~7	
	wm8994_write(0x03,  0x0300);
	wm8994_write(0x22,  0x0000);	
	wm8994_write(0x23,  0x0100);
	wm8994_write(0x36,  0x0003);

#endif
#if 0
	wm8994_write(0x01,  0x0003);  
	mdelay(50);
	wm8994_write(0x221, 0x0700);  
	wm8994_write(0x222, 0x3127);	
	wm8994_write(0x223, 0x0100);	
	wm8994_write(0x220, 0x0004);
	mdelay(50);
	wm8994_write(0x220, 0x0005);  

	wm8994_write(0x01,  0x0303);  // sysclk = fll (bit4 =1)   0x0011 
	wm8994_write(0x05,  0x0303);  // i2s 16 bits
  
	wm8994_write(0x2D,  0x0100);
	wm8994_write(0x2E,  0x0100);
	wm8994_write(0x4C,  0x9F25);
	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x200, 0x0011);	
	wm8994_write(0x208, 0x000A);	
	wm8994_write(0x601, 0x0001);
	wm8994_write(0x602, 0x0001);

	wm8994_write(0x610, 0x01C0);		
	wm8994_write(0x611, 0x01C0);		
	wm8994_write(0x420, 0x0000);
#endif

}

void  AP_to_Speakers(void)
{
	DBG("Enter::%s----line->%d-- AP_to_Speakers\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_AP_to_Speakers;
	wm8994_reset();
	mdelay(50);

#if 1  // codec slave
	wm8994_write(0x01,  0x0003);
	mdelay(50);
	wm8994_write(0x221, 0x0700);
	wm8994_write(0x222, 0x90C2);
	wm8994_write(0x223, 0x00E0);

#ifdef CONFIG_SND_CODEC_SOC_MASTER
  	wm8994_write(0x302, 0x4000);  // master = 0x4000  slave= 0x0000
	wm8994_write(0x303, 0x0050);  // master  0x0050 lrck 40.98kHz bclk 2.15MHz
	wm8994_write(0x305, 0x0035);  // master  0x0035 lrck 40.98kHz bclk 2.15MHz
#endif

	wm8994_write(0x220, 0x0005);  
	mdelay(50);
	wm8994_write(0x200, 0x0001);  // sysclk = fll (bit4 =1)   0x0011
  	wm8994_write(0x300, 0x4010);  // i2s 16 bits
  
	wm8994_write(0x01,  0x3003); 
	wm8994_write(0x05,  0x0303);   
	wm8994_write(0x2D,  0x0100);
	wm8994_write(0x2E,  0x0100);
	wm8994_write(0x4C,  0x9F25);
	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x208, 0x000A);
	wm8994_write(0x420, 0x0000); 
	
	wm8994_write(0x601, 0x0001);
	wm8994_write(0x602, 0x0001);
    
	wm8994_write(0x610, 0x01c0);  //DAC1 Left Volume bit0~7	
	wm8994_write(0x611, 0x01c0);  //DAC1 Right Volume bit0~7	
	wm8994_write(0x03,  0x0300);
	wm8994_write(0x22,  0x0000);	
	wm8994_write(0x23,  0x0100);
	wm8994_write(0x36,  0x0003);	
	mdelay(50);
#endif
#if 0
	wm8994_write(0x01,  0x0003);  
	mdelay(50);
	wm8994_write(0x221, 0x0700);  
	wm8994_write(0x222, 0x3127);	
	wm8994_write(0x223, 0x0100);	
	wm8994_write(0x220, 0x0004);
	mdelay(50);
	wm8994_write(0x220, 0x0005);  

	wm8994_write(0x01,  0x3003);  // sysclk = fll (bit4 =1)   0x0011 
	wm8994_write(0x03,  0x0330);
	wm8994_write(0x05,  0x0303);  // i2s 16 bits

	wm8994_write(0x22,  0x0000);
	wm8994_write(0x23,  0x0100);
	wm8994_write(0x2D,  0x0001);
	wm8994_write(0x2E,  0x0001);
//	wm8994_write(0x4C,  0x9F25);
//	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x200, 0x0011);	
	wm8994_write(0x208, 0x000A);	
	wm8994_write(0x601, 0x0001);
	wm8994_write(0x602, 0x0001);

	wm8994_write(0x610, 0x01C0);		
	wm8994_write(0x611, 0x01C0);
	wm8994_write(0x620, 0x0000);
	wm8994_write(0x420, 0x0000);
#endif
}

void  Recorder(void)
{
	DBG("Enter::%s----line->%d-- Recorder\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_Recorder;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01,   0x0013);
	mdelay(50);
	wm8994_write(0x221,  0x0D00);
	wm8994_write(0x222,  0x3300);
	wm8994_write(0x223,  0x00E0);

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302,  0x4000);  //master = 0x4000  slave= 0x0000
	wm8994_write(0x303,  0x0090);  //master  0x0090 lrck1 8kHz bclk1 515KHz
	wm8994_write(0x305,  0x00F0);  //master  0x00F0 lrck1 8kHz bclk1 515KHz
#endif

	wm8994_write(0x220,  0x0004);
	mdelay(50);
	wm8994_write(0x220,  0x0005);

	wm8994_write(0x02,   0x6110);
	wm8994_write(0x04,   0x0303);
	wm8994_write(0x1A,   0x015F);  //volume
  
	wm8994_write(0x28,   0x0003);
	wm8994_write(0x2A,   0x0020);
	wm8994_write(0x200,  0x0011);
	wm8994_write(0x208,  0x000A);
	wm8994_write(0x300,  0xC050);
	wm8994_write(0x606,  0x0002);
	wm8994_write(0x607,  0x0002);
	wm8994_write(0x620,  0x0000);

}

void  FM_to_Headset(void)
{
	DBG("Enter::%s----line->%d-- FM_to_Headset\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_FM_to_Headset;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01,  0x0323); 
	wm8994_write(0x02,  0x03A0);  
	wm8994_write(0x03,  0x0030);	
	wm8994_write(0x19,  0x010B);  //LEFT LINE INPUT 3&4 VOLUME	
	wm8994_write(0x1B,  0x010B);  //RIGHT LINE INPUT 3&4 VOLUME

	wm8994_write(0x28,  0x0044);  
	wm8994_write(0x29,  0x0100);	 
	wm8994_write(0x2A,  0x0100);
	wm8994_write(0x2D,  0x0040); 
	wm8994_write(0x2E,  0x0040);
	wm8994_write(0x4C,  0x9F25);
	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x220, 0x0003);
	wm8994_write(0x221, 0x0700);
	wm8994_write(0x224, 0x0CC0);
	wm8994_write(0x200, 0x0011);	
	wm8994_write(0x1C,  0x01F9);  //LEFT OUTPUT VOLUME	
	wm8994_write(0x1D,  0x01F9);  //RIGHT OUTPUT VOLUME
}

void  FM_to_Headset_and_Record(void)
{
	DBG("Enter::%s----line->%d-- FM_to_Headset_and_Record\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_FM_to_Headset_and_Record;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01,   0x0003);  
	mdelay(50);
	wm8994_write(0x221,  0x1900);  //8~13BIT div

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302,  0x4000);  // master = 0x4000 // slave= 0x0000
	wm8994_write(0x303,  0x0040);  // master  0x0050 lrck 7.94kHz bclk 510KHz
#endif
	
	wm8994_write(0x220,  0x0004);
	mdelay(50);
	wm8994_write(0x220,  0x0005);  

	wm8994_write(0x01,   0x0323);
	wm8994_write(0x02,   0x03A0);
	wm8994_write(0x03,   0x0030);
	wm8994_write(0x19,   0x010B);  //LEFT LINE INPUT 3&4 VOLUME	
	wm8994_write(0x1B,   0x010B);  //RIGHT LINE INPUT 3&4 VOLUME
  
	wm8994_write(0x28,   0x0044);
	wm8994_write(0x29,   0x0100);
	wm8994_write(0x2A,   0x0100);
	wm8994_write(0x2D,   0x0040);
	wm8994_write(0x2E,   0x0040);
	wm8994_write(0x4C,   0x9F25);
	wm8994_write(0x60,   0x00EE);
	wm8994_write(0x200,  0x0011);
	wm8994_write(0x1C,   0x01F9);  //LEFT OUTPUT VOLUME
	wm8994_write(0x1D,   0x01F9);  //RIGHT OUTPUT VOLUME
	wm8994_write(0x04,   0x0303);
	wm8994_write(0x208,  0x000A);
	wm8994_write(0x300,  0x4050);
	wm8994_write(0x606,  0x0002);
	wm8994_write(0x607,  0x0002);
	wm8994_write(0x620,  0x0000);
}

void  FM_to_Speakers(void)
{
	DBG("Enter::%s----line->%d-- FM_to_Speakers\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_FM_to_Speakers;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01,   0x3023);
	wm8994_write(0x02,   0x03A0);
	wm8994_write(0x03,   0x0330);
	wm8994_write(0x19,   0x010B);  //LEFT LINE INPUT 3&4 VOLUME
	wm8994_write(0x1B,   0x010B);  //RIGHT LINE INPUT 3&4 VOLUME
  
	wm8994_write(0x22,   0x0000);
	wm8994_write(0x23,   0x0000);
	wm8994_write(0x36,   0x000C);

	wm8994_write(0x28,   0x0044);
	wm8994_write(0x29,   0x0100);
	wm8994_write(0x2A,   0x0100);
	wm8994_write(0x2D,   0x0040);
	wm8994_write(0x2E,   0x0040);
//	wm8994_write(0x4C,   0x9F25);
//	wm8994_write(0x60,   0x00EE);

	wm8994_write(0x220,  0x0003);
	wm8994_write(0x221,  0x0700);
	wm8994_write(0x224,  0x0CC0);

	wm8994_write(0x200,  0x0011);
	wm8994_write(0x20,   0x01F9);
	wm8994_write(0x21,   0x01F9);
}

void  FM_to_Speakers_and_Record(void)
{
	DBG("Enter::%s----line->%d-- FM_to_Speakers_and_Record\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_FM_to_Speakers_and_Record;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01,   0x0003);  
	mdelay(50);

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302,  0x4000);  // master = 0x4000 // slave= 0x0000
	wm8994_write(0x303,  0x0090);  //
#endif
	
	wm8994_write(0x220,  0x0006);
	mdelay(50);

	wm8994_write(0x01,   0x3023);
	wm8994_write(0x02,   0x03A0);
	wm8994_write(0x03,   0x0330);
	wm8994_write(0x19,   0x010B);  //LEFT LINE INPUT 3&4 VOLUME
	wm8994_write(0x1B,   0x010B);  //RIGHT LINE INPUT 3&4 VOLUME
  
	wm8994_write(0x22,   0x0000);
	wm8994_write(0x23,   0x0000);
	wm8994_write(0x36,   0x000C);

	wm8994_write(0x28,   0x0044);
	wm8994_write(0x29,   0x0100);
	wm8994_write(0x2A,   0x0100);
	wm8994_write(0x2D,   0x0040);
	wm8994_write(0x2E,   0x0040);
//	wm8994_write(0x4C,   0x9F25);
//	wm8994_write(0x60,   0x00EE);

	wm8994_write(0x220,  0x0003);
	wm8994_write(0x221,  0x0700);
	wm8994_write(0x224,  0x0CC0);

	wm8994_write(0x200,  0x0011);
	wm8994_write(0x20,   0x01F9);
	wm8994_write(0x21,   0x01F9);
	wm8994_write(0x04,   0x0303);
	wm8994_write(0x208,  0x000A);	
	wm8994_write(0x300,  0x4050);
	wm8994_write(0x606,  0x0002);	
	wm8994_write(0x607,  0x0002);
	wm8994_write(0x620,  0x0000);
}

void  HandsetMIC_to_Baseband_to_Headset(void)
{
	DBG("Enter::%s----line->%d-- HandsetMIC_to_Baseband_to_Headset\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_HandsetMIC_to_Baseband_to_Headset;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01,  0x0323); 
	wm8994_write(0x02,  0x6040); 
	wm8994_write(0x03,  0x3030); 
	wm8994_write(0x18,  0x014B);  //mic volume
	wm8994_write(0x1E,  0x0006); 
	wm8994_write(0x28,  0x0030); 
	wm8994_write(0x2D,  0x0002);  //bit 1 IN2LP_TO_MIXOUTL
	wm8994_write(0x2E,  0x0002);  //bit 1 IN2RP_TO_MIXOUTR
	wm8994_write(0x34,  0x0002); 
	wm8994_write(0x4C,  0x9F25); 
	wm8994_write(0x60,  0x00EE); 
	wm8994_write(0x220, 0x0003); 
	wm8994_write(0x221, 0x0700); 
	wm8994_write(0x224, 0x0CC0); 

//Note: free-running start first, then open AIF1 clock setting
	wm8994_write(0x200, 0x0011);
//Note: 0x1C/0x1D=0x01FF-->bypass volume no gain/attenuation
	wm8994_write(0x1C,  0x01FF);  //LEFT OUTPUT VOLUME
	wm8994_write(0x1D,  0x01F9);  //RIGHT OUTPUT VOLUME
}

void  HandsetMIC_to_Baseband_to_Headset_and_Record(void)
{
	DBG("Enter::%s----line->%d-- HandsetMIC_to_Baseband_to_Headset_and_Record\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_HandsetMIC_to_Baseband_to_Headset_and_Record;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01,  0x0323); 
	wm8994_write(0x02,  0x62C0); 
	wm8994_write(0x03,  0x3030); 
	wm8994_write(0x04,  0x0303); 
	wm8994_write(0x18,  0x014B);  //volume
	wm8994_write(0x19,  0x014B);  //volume
	wm8994_write(0x1C,  0x01FF);  //LEFT OUTPUT VOLUME
	wm8994_write(0x1D,  0x01F9);  //RIGHT OUTPUT VOLUME
	wm8994_write(0x1E,  0x0006); 
	wm8994_write(0x28,  0x00B0);  //IN2LP_TO_IN2L
	wm8994_write(0x29,  0x0120); 
	wm8994_write(0x2D,  0x0002);  //bit 1 IN2LP_TO_MIXOUTL
	wm8994_write(0x2E,  0x0002);  //bit 1 IN2RP_TO_MIXOUTR
	wm8994_write(0x34,  0x0002); 
	wm8994_write(0x4C,  0x9F25); 
	wm8994_write(0x60,  0x00EE); 
	wm8994_write(0x200, 0x0001); 
	wm8994_write(0x208, 0x000A); 
	wm8994_write(0x300, 0x0050); 

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302, 0x4000);  // master = 0x4000 // slave= 0x0000
	wm8994_write(0x303, 0x0090);  // master lrck 16k
#endif

	wm8994_write(0x606, 0x0002); 
	wm8994_write(0x607, 0x0002); 
	wm8994_write(0x620, 0x0000); 

	wm8994_write(0x1C, 0x01F9); 
	wm8994_write(0x1D, 0x01F9); 
}

void  MainMIC_to_Baseband_to_Earpiece(void)
{
	DBG("Enter::%s----line->%d-- MainMIC_to_Baseband_to_Earpiece\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_MainMIC_to_Baseband_to_Earpiece;
	wm8994_reset();
	mdelay(50);

  	wm8994_write(0x01, 0x0813);
	wm8994_write(0x02, 0x6210);
	wm8994_write(0x03, 0x30A0);
	wm8994_write(0x1A, 0x015F);  //main mic volume
	wm8994_write(0x1E, 0x0006);
	wm8994_write(0x1F, 0x0000);
	wm8994_write(0x28, 0x0003);
	wm8994_write(0x2B, 0x0005);  //VRX_MIXINL_VOL
	wm8994_write(0x2D, 0x0040);
   	wm8994_write(0x33, 0x0010);
	wm8994_write(0x34, 0x0004);
}

void  MainMIC_to_Baseband_to_Earpiece_and_Record(void)
{
	DBG("Enter::%s----line->%d-- MainMIC_to_Baseband_to_Earpiece_and_Record\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_MainMIC_to_Baseband_to_Earpiece_and_Record;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01  ,0x0813);
	wm8994_write(0x02  ,0x6310);
	wm8994_write(0x03  ,0x30A0);
	wm8994_write(0x04  ,0x0303);
	wm8994_write(0x1A  ,0x014F);
	wm8994_write(0x1E  ,0x0006);
	wm8994_write(0x1F  ,0x0000);
	wm8994_write(0x28  ,0x0003);  //MAINMIC_TO_IN1R  //
	wm8994_write(0x2A  ,0x0020);  //IN1R_TO_MIXINR   //
	wm8994_write(0x2B  ,0x0005);  //VRX_MIXINL_VOL bit 0~2
	wm8994_write(0x2C  ,0x0005);  //VRX_MIXINR_VOL
	wm8994_write(0x2D  ,0x0040);  //MIXINL_TO_MIXOUTL
   	wm8994_write(0x33  ,0x0010);  //MIXOUTLVOL_TO_HPOUT2
	wm8994_write(0x34  ,0x0004);  //IN1R_TO_LINEOUT1 //
	wm8994_write(0x200 ,0x0001);
	wm8994_write(0x208 ,0x000A);
	wm8994_write(0x300 ,0xC050);

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302, 0x4000);  // master = 0x4000 // slave= 0x0000
	wm8994_write(0x303, 0x0090);  // master lrck 16k
#endif

	wm8994_write(0x606 ,0x0002);
	wm8994_write(0x607 ,0x0002);
	wm8994_write(0x620 ,0x0000);
}

void  MainMIC_to_Baseband_to_Speakers(void)
{
	DBG("Enter::%s----line->%d-- MainMIC_to_Baseband_to_Speakers\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_MainMIC_to_Baseband_to_Speakers;
	wm8994_reset();
	mdelay(50);

#if 1
	wm8994_write(0x01 ,0x3013);
	wm8994_write(0x02 ,0x6210);
	wm8994_write(0x03 ,0x3330);
	wm8994_write(0x1A ,0x015F);
	wm8994_write(0x1E ,0x0006);
	wm8994_write(0x22 ,0x0000);
	wm8994_write(0x23 ,0x0100);
	wm8994_write(0x26 ,0x017F);  //Speaker Volume Left bit 0~5
	wm8994_write(0x27 ,0x017F);  //Speaker Volume Right bit 0~5
	wm8994_write(0x28 ,0x0003);
	wm8994_write(0x2D ,0x0002);  //bit 1 IN2LP_TO_MIXOUTL
	wm8994_write(0x2E ,0x0002);  //bit 1 IN2RP_TO_MIXOUTR
   	wm8994_write(0x34 ,0x0004);   
	wm8994_write(0x36 ,0x000C);
#endif
}

void  MainMIC_to_Baseband_to_Speakers_and_Record(void)
{
	DBG("Enter::%s----line->%d-- MainMIC_to_Baseband_to_Speakers_and_Record\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_MainMIC_to_Baseband_to_Speakers_and_Record;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01, 0x3013);
	wm8994_write(0x02, 0x6330);
	wm8994_write(0x03, 0x3330);
	wm8994_write(0x04, 0x0303);
	wm8994_write(0x1A, 0x014B);
	wm8994_write(0x1B, 0x014B);
	wm8994_write(0x1E, 0x0006);
	wm8994_write(0x22, 0x0000);
	wm8994_write(0x23, 0x0100);
 	wm8994_write(0x28, 0x0007);
	wm8994_write(0x2A, 0x0120);
	wm8994_write(0x2D, 0x0002);  //bit 1 IN2LP_TO_MIXOUTL
	wm8994_write(0x2E, 0x0002);  //bit 1 IN2RP_TO_MIXOUTR
	wm8994_write(0x34, 0x0004);
	wm8994_write(0x36, 0x000C);
	wm8994_write(0x200, 0x0001);
	wm8994_write(0x208, 0x000A);
 	wm8994_write(0x300, 0xC050);

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302, 0x4000);  // master = 0x4000 // slave= 0x0000
	wm8994_write(0x303, 0x0090);  // master lrck 16k
#endif

 	wm8994_write(0x606, 0x0002);
 	wm8994_write(0x607, 0x0002);
 	wm8994_write(0x620, 0x0000);

}

void  BT_Baseband(void)
{
	DBG("Enter::%s----line->%d-- BT_Baseband\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_BT_Baseband;
	wm8994_reset();
	mdelay(50);
#if 1
	wm8994_write(0x01,  0x0003);  
	mdelay(50);
	wm8994_write(0x221, 0x0700);  
	wm8994_write(0x222, 0x3127);	
	wm8994_write(0x223, 0x0100);	
   	wm8994_write(0x220, 0x0004);
   	mdelay(50);
	wm8994_write(0x220, 0x0005);

	wm8994_write(0x01,  0x0003);
	wm8994_write(0x03,  0x30F0);
	wm8994_write(0x05,  0x3003);
	wm8994_write(0x2D,  0x0001);
	wm8994_write(0x2E,  0x0001);

	wm8994_write(0x200, 0x0001); 
	wm8994_write(0x204, 0x0001);
	wm8994_write(0x208, 0x0007);
	wm8994_write(0x520, 0x0000);
	wm8994_write(0x601, 0x0004);
	wm8994_write(0x602, 0x0004);
	wm8994_write(0x610, 0x01C0);
	wm8994_write(0x611, 0x01C0);
	wm8994_write(0x613, 0x01C0);
   
	wm8994_write(0x702, 0xC100); 
	wm8994_write(0x703, 0xC100);
	wm8994_write(0x704, 0xC100);
	wm8994_write(0x706, 0x4100);
	
	wm8994_write(0x204, 0x0011);  // AIF2 MCLK=FLL                            //MASTER
	wm8994_write(0x211, 0x0039);  //LRCK=8KHZ,Rate=MCLK/1536	                 //MASTER
	wm8994_write(0x310, 0xC118);  //DSP/PCM; 16bits; ADC L channel = R channel;MODE A

	wm8994_write(0x313, 0x00F0);
	wm8994_write(0x314, 0x0020);    
	wm8994_write(0x315, 0x0020);		
	wm8994_write(0x2B,  0x0005);    
	wm8994_write(0x2C,  0x0005);
	wm8994_write(0x02,  0x6300);
	wm8994_write(0x04,  0x3003);

	wm8994_write(0x1E,  0x0006);  //LINEOUT1N_MUTE(001Eh);
	wm8994_write(0x34,  0x0001);  //LINEOUT1_MODE=1;LINEOUT_VMID_BUF_ENA=1;

	wm8994_write(0x603, 0x018C);	 
	wm8994_write(0x604, 0x0010);
	wm8994_write(0x605, 0x0010);
	wm8994_write(0x621, 0x0001);
	wm8994_write(0x317, 0x0003);

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x312, 0x4000);  //set 0x312 PCM2 as Master
	wm8994_write(0x313, 0x0090);  //master   0x0090 lrck2 8kHz bclk2 1MH
	wm8994_write(0x315, 0x007D);  //master   0x007D lrck2 8kHz bclk2 1MH
#endif

#endif
#if 0
	wm8994_write(0x01 ,0x0003);
	wm8994_write(0x02 ,0x63A0); 
	wm8994_write(0x03 ,0x30A0); 
	wm8994_write(0x04 ,0x0303); 
	wm8994_write(0x05 ,0x0202); 
	wm8994_write(0x06 ,0x0001); 
	wm8994_write(0x19 ,0x014B); 
	wm8994_write(0x1B ,0x014B); 
	wm8994_write(0x1E ,0x0006); 
	wm8994_write(0x28 ,0x00CC); 
	wm8994_write(0x29 ,0x0100); 
	wm8994_write(0x2A ,0x0100); 
	wm8994_write(0x2D ,0x0001); 
	wm8994_write(0x34 ,0x0001); 
	wm8994_write(0x200 ,0x0001);
	wm8994_write(0x208 ,0x000A);
#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302 ,0x7000);
#endif
	wm8994_write(0x303, 0x0090);  //master   0x0090 lrck1 8kHz bclk1 1MH
	wm8994_write(0x305, 0x007D);  //master   0x007D lrck1 8kHz bclk1 1MH

	wm8994_write(0x420 ,0x0000);
	wm8994_write(0x601 ,0x0001);
	wm8994_write(0x602 ,0x0001);
	wm8994_write(0x606 ,0x0002);
	wm8994_write(0x607 ,0x0002);
	wm8994_write(0x610 ,0x01C0);
	wm8994_write(0x611 ,0x01C0);
	wm8994_write(0x620 ,0x0000);
	wm8994_write(0x707 ,0xA100);
	wm8994_write(0x708 ,0x2100);
	wm8994_write(0x709 ,0x2100);
	wm8994_write(0x70A ,0x2100);
#endif
}

void  BT_Baseband_and_record(void)
{
	DBG("Enter::%s----line->%d-- BT_Baseband_and_record\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_BT_Baseband_and_record;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01, 0x0003);
	wm8994_write(0x02, 0x63A0);
	wm8994_write(0x03, 0x30A0);
	wm8994_write(0x04, 0x3303);
	wm8994_write(0x05, 0x3002);
	wm8994_write(0x06, 0x000A);
	wm8994_write(0x19, 0x014B);
	wm8994_write(0x1B, 0x014B);
	wm8994_write(0x1E, 0x0006);
	wm8994_write(0x28, 0x00CC);
	wm8994_write(0x29, 0x0100);
	wm8994_write(0x2A, 0x0100);
	wm8994_write(0x2D, 0x0001);
	wm8994_write(0x34, 0x0001);
	wm8994_write(0x200, 0x0001);
	wm8994_write(0x204, 0x0001);
	wm8994_write(0x208, 0x000F);

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x312, 0x7000);
	wm8994_write(0x313, 0x0090);  //master   0x0090 lrck2 8kHz bclk2 1MH
	wm8994_write(0x315, 0x007D);  //master   0x007D lrck2 8kHz bclk2 1MH

	wm8994_write(0x302, 0x4000);
	wm8994_write(0x303, 0x0090);  //master  
#endif	

	wm8994_write(0x440, 0x0018);
	wm8994_write(0x450, 0x0018);
	wm8994_write(0x480, 0x0000);
	wm8994_write(0x481, 0x0000);
	wm8994_write(0x4A0, 0x0000);
	wm8994_write(0x4A1, 0x0000);
	wm8994_write(0x520, 0x0000);
	wm8994_write(0x540, 0x0018);
	wm8994_write(0x580, 0x0000);
	wm8994_write(0x581, 0x0000);
	wm8994_write(0x601, 0x0004);
	wm8994_write(0x603, 0x000C);
	wm8994_write(0x604, 0x0010);
	wm8994_write(0x605, 0x0010);
	wm8994_write(0x606, 0x0003);
	wm8994_write(0x607, 0x0003);
	wm8994_write(0x610, 0x01C0);
	wm8994_write(0x612, 0x01C0);
	wm8994_write(0x613, 0x01C0);
	wm8994_write(0x620, 0x0000);
	wm8994_write(0x704, 0xA100);
 	wm8994_write(0x707, 0xA100);
	wm8994_write(0x708, 0x2100);
	wm8994_write(0x709, 0x2100);
	wm8994_write(0x70A, 0x2100);
}


///PCM BB BEGIN////////////////////////////////////

void  HandsetMIC_to_PCMBaseband_to_Headset(void)
{
	DBG("Enter::%s----line->%d-- HandsetMIC_to_PCMBaseband_to_Headset\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_HandsetMIC_to_PCMBaseband_to_Headset;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01,  0x0003);  
	mdelay(50);
	wm8994_write(0x221, 0x0700);  
	wm8994_write(0x222, 0x3127);	
	wm8994_write(0x223, 0x0100);	
	wm8994_write(0x220, 0x0004);
	mdelay(50);
	wm8994_write(0x220, 0x0005);  

	wm8994_write(0x01,  0x0303);  // sysclk = fll (bit4 =1)   0x0011 
	wm8994_write(0x02,  0x0240);
	wm8994_write(0x03,  0x0030);
	wm8994_write(0x04,  0x3003);
	wm8994_write(0x05,  0x3003);  // i2s 16 bits
	wm8994_write(0x18,  0x010B);
	wm8994_write(0x28,  0x0030);
	wm8994_write(0x29,  0x0020);
	wm8994_write(0x2D,  0x0100);
	wm8994_write(0x2E,  0x0100);
	wm8994_write(0x4C,  0x9F25);
	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x200, 0x0001);
	wm8994_write(0x204, 0x0001);
	wm8994_write(0x208, 0x0007);
	wm8994_write(0x520, 0x0000);
	wm8994_write(0x601, 0x0004);
	wm8994_write(0x602, 0x0004);

	wm8994_write(0x610, 0x01C0);
	wm8994_write(0x611, 0x01C0);
	wm8994_write(0x612, 0x01C0);
	wm8994_write(0x613, 0x01C0);

	wm8994_write(0x702, 0xC100);
	wm8994_write(0x703, 0xC100);
	wm8994_write(0x704, 0xC100);
	wm8994_write(0x706, 0x4100);
	wm8994_write(0x204, 0x0011);
	wm8994_write(0x211, 0x0009);
	wm8994_write(0x310, 0x4118);
	wm8994_write(0x313, 0x00F0);
	wm8994_write(0x314, 0x0020);
	wm8994_write(0x315, 0x0020);

	wm8994_write(0x603, 0x018C);  //Rev.D ADCL SideTone
	wm8994_write(0x604, 0x0010);
	wm8994_write(0x605, 0x0010);
	wm8994_write(0x621, 0x0001);
//	wm8994_write(0x317, 0x0003);
#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x312, 0x4000);  //AIF2 SET AS MASTER
#endif
}

void  HandsetMIC_to_PCMBaseband_to_Headset_and_Record(void)
{
	DBG("Enter::%s----line->%d-- HandsetMIC_to_PCMBaseband_to_Headset_and_Record\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_HandsetMIC_to_PCMBaseband_to_Headset_and_Record;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01,  0x0003);  
	mdelay(50);
	wm8994_write(0x221, 0x0700);  //MCLK=12MHz
	wm8994_write(0x222, 0x3127);	
	wm8994_write(0x223, 0x0100);	
	wm8994_write(0x220, 0x0004);
	mdelay(50);
	wm8994_write(0x220, 0x0005);  

	wm8994_write(0x01,  0x0303);	 
	wm8994_write(0x02,  0x0240);
	wm8994_write(0x03,  0x0030);
	wm8994_write(0x04,  0x3003);
	wm8994_write(0x05,  0x3003); 
	wm8994_write(0x18,  0x010B);  // 0x011F=+30dB for MIC
	wm8994_write(0x28,  0x0030);
	wm8994_write(0x29,  0x0020);
	wm8994_write(0x2D,  0x0100);
	wm8994_write(0x2E,  0x0100);
	wm8994_write(0x4C,  0x9F25);
	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x200, 0x0001);	
	wm8994_write(0x204, 0x0001);
	wm8994_write(0x208, 0x0007);	
	wm8994_write(0x520, 0x0000);	
	wm8994_write(0x601, 0x0004);
	wm8994_write(0x602, 0x0004);

	wm8994_write(0x610, 0x01C0);		
	wm8994_write(0x611, 0x01C0);
	wm8994_write(0x612, 0x01C0);		
	wm8994_write(0x613, 0x01C0);

	wm8994_write(0x700, 0x8141);  //SYNC issue, AIF1 ADCLRC1 from LRCK1
	wm8994_write(0x702, 0xC100);
	wm8994_write(0x703, 0xC100);
	wm8994_write(0x704, 0xC100);
	wm8994_write(0x706, 0x4100);
	wm8994_write(0x204, 0x0011);  //AIF2 MCLK=FLL1
	wm8994_write(0x211, 0x0009);  //LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x310, 0x4118);  //DSP/PCM 16bits
	wm8994_write(0x313, 0x00F0);
	wm8994_write(0x314, 0x0020);
	wm8994_write(0x315, 0x0020);

	wm8994_write(0x603, 0x000C);  //Rev.D ADCL SideTone
	wm8994_write(0x604, 0x0010);
	wm8994_write(0x605, 0x0010);
	wm8994_write(0x621, 0x0000);
//	wm8994_write(0x317, 0x0003);
#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x312, 0x4000);  //AIF2 SET AS MASTER
#endif
////AIF1
	wm8994_write(0x04,   0x3303);
	wm8994_write(0x200,  0x0001);
	wm8994_write(0x208,  0x000F);
	wm8994_write(0x210,  0x0009);  //LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x300,  0x0118);  //DSP/PCM 16bits, R ADC = L ADC 
	wm8994_write(0x606,  0x0003);	
	wm8994_write(0x607,  0x0003);

////AIF1 Master Clock(SR=8KHz)
	wm8994_write(0x200,  0x0011);
#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302,  0x4000);
#endif
	wm8994_write(0x303,  0x00F0);
	wm8994_write(0x304,  0x0020);
	wm8994_write(0x305,  0x0020);

////AIF1 DAC1 HP
	wm8994_write(0x05,   0x3303);
	wm8994_write(0x420,  0x0000);
	wm8994_write(0x601,  0x0001);
	wm8994_write(0x602,  0x0001);
	wm8994_write(0x700,  0x8140);  //SYNC issue, AIF1 ADCLRC1 from FLL after AIF1 MASTER!!!
}

void  MainMIC_to_PCMBaseband_to_Earpiece(void)
{
	DBG("Enter::%s----line->%d-- MainMIC_to_PCMBaseband_to_Earpiece\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_MainMIC_to_PCMBaseband_to_Earpiece;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01,  0x0013);  
	mdelay(50);
	wm8994_write(0x221, 0x0700);  //MCLK=12MHz
	wm8994_write(0x222, 0x3127);	
	wm8994_write(0x223, 0x0100);	
	wm8994_write(0x220, 0x0004);
	mdelay(50);
	wm8994_write(0x220, 0x0005);  

	wm8994_write(0x01,  0x0813);	 
	wm8994_write(0x02,  0x0110);
	wm8994_write(0x03,  0x00F0);
	wm8994_write(0x04,  0x3003);
	wm8994_write(0x05,  0x3003); 
	wm8994_write(0x1A,  0x010B); 
	wm8994_write(0x1F,  0x0000); 
	wm8994_write(0x28,  0x0003);
	wm8994_write(0x2A,  0x0020);
	wm8994_write(0x2D,  0x0001);
	wm8994_write(0x2E,  0x0001);
	wm8994_write(0x33,  0x0018);
//	wm8994_write(0x4C,  0x9F25);
//	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x200, 0x0001);
	wm8994_write(0x204, 0x0001);
	wm8994_write(0x208, 0x0007);
	wm8994_write(0x520, 0x0000);
	wm8994_write(0x601, 0x0004);
	wm8994_write(0x602, 0x0004);

	wm8994_write(0x610, 0x01C0);
	wm8994_write(0x611, 0x01C0);
	wm8994_write(0x612, 0x01C0);
	wm8994_write(0x613, 0x01C0);

	wm8994_write(0x700, 0x8141);
	wm8994_write(0x702, 0xC100);
	wm8994_write(0x703, 0xC100);
	wm8994_write(0x704, 0xC100);
	wm8994_write(0x706, 0x4100);
	wm8994_write(0x204, 0x0011);  //AIF2 MCLK=FLL1
	wm8994_write(0x211, 0x0009);  //LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x310, 0x4118);  //DSP/PCM 16bits
	wm8994_write(0x313, 0x00F0);
	wm8994_write(0x314, 0x0020);
	wm8994_write(0x315, 0x0020);

	wm8994_write(0x603, 0x018C);  //Rev.D ADCL SideTone
	wm8994_write(0x604, 0x0010);
	wm8994_write(0x605, 0x0010);
	wm8994_write(0x621, 0x0001);
//	wm8994_write(0x317, 0x0003);
#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x312, 0x4000);  //AIF2 SET AS MASTER
#endif
}

void  MainMIC_to_PCMBaseband_to_Earpiece_and_Record(void)
{
	DBG("Enter::%s----line->%d-- MainMIC_to_PCMBaseband_to_Earpiece_and_Record\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_MainMIC_to_PCMBaseband_to_Earpiece_and_Record;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01,  0x0013);  
	mdelay(50);
	wm8994_write(0x221, 0x0700);  //MCLK=12MHz
	wm8994_write(0x222, 0x3127);	
	wm8994_write(0x223, 0x0100);	
	wm8994_write(0x220, 0x0004);
	mdelay(50);
	wm8994_write(0x220, 0x0005);  

	wm8994_write(0x01,  0x0813);	 
	wm8994_write(0x02,  0x0110);
	wm8994_write(0x03,  0x00F0);
	wm8994_write(0x04,  0x3003);
	wm8994_write(0x05,  0x3003); 
	wm8994_write(0x1A,  0x010B); 
	wm8994_write(0x1F,  0x0000); 
	wm8994_write(0x28,  0x0003);
	wm8994_write(0x2A,  0x0020);
	wm8994_write(0x2D,  0x0001);
	wm8994_write(0x2E,  0x0001);
	wm8994_write(0x33,  0x0018);
//	wm8994_write(0x4C,  0x9F25);
//	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x200, 0x0001);	
	wm8994_write(0x204, 0x0001);
	wm8994_write(0x208, 0x0007);	
	wm8994_write(0x520, 0x0000);	
	wm8994_write(0x601, 0x0004);
	wm8994_write(0x602, 0x0004);

	wm8994_write(0x610, 0x01C0);		
	wm8994_write(0x611, 0x01C0);
	wm8994_write(0x612, 0x01C0);		
	wm8994_write(0x613, 0x01C0);

	wm8994_write(0x702, 0xC100);
	wm8994_write(0x703, 0xC100);
	wm8994_write(0x704, 0xC100);
	wm8994_write(0x706, 0x4100);
	wm8994_write(0x204, 0x0011);  //AIF2 MCLK=FLL1
	wm8994_write(0x211, 0x0009);  //LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x310, 0x4118);  //DSP/PCM 16bits
	wm8994_write(0x313, 0x00F0);
	wm8994_write(0x314, 0x0020);
	wm8994_write(0x315, 0x0020);

	wm8994_write(0x603, 0x018C);  //Rev.D ADCL SideTone
	wm8994_write(0x604, 0x0010);
	wm8994_write(0x605, 0x0010);
	wm8994_write(0x621, 0x0001);
//	wm8994_write(0x317, 0x0003);
#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x312, 0x4000);  //AIF2 SET AS MASTER
#endif

////AIF1
	wm8994_write(0x04,   0x3303);
	wm8994_write(0x200,  0x0001);
	wm8994_write(0x208,  0x000F);
	wm8994_write(0x210,  0x0009);  //LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x300,  0xC118);  //DSP/PCM 16bits, R ADC = L ADC 
	wm8994_write(0x606,  0x0003);	
	wm8994_write(0x607,  0x0003);

////AIF1 Master Clock(SR=8KHz)
	wm8994_write(0x200,  0x0011);
#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302,  0x4000);
#endif
	wm8994_write(0x303,  0x00F0);
	wm8994_write(0x304,  0x0020);
	wm8994_write(0x305,  0x0020);

////AIF1 DAC1 HP
	wm8994_write(0x05,   0x3303);
	wm8994_write(0x420,  0x0000);
	wm8994_write(0x601,  0x0001);
	wm8994_write(0x602,  0x0001);
	wm8994_write(0x700,  0x8140);//SYNC issue, AIF1 ADCLRC1 from FLL after AIF1 MASTER!!!
}

void  MainMIC_to_PCMBaseband_to_Speakers(void)
{
	DBG("Enter::%s----line->%d-- MainMIC_to_PCMBaseband_to_Speakers\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_MainMIC_to_PCMBaseband_to_Speakers;
	wm8994_reset();
	mdelay(50);
#if 1
	wm8994_write(0x01,  0x0013);  
	mdelay(50);
	wm8994_write(0x221, 0x0700);  //MCLK=12MHz   //FLL1 CONTRLO(2)
	wm8994_write(0x222, 0x3127);  //FLL1 CONTRLO(3)	
	wm8994_write(0x223, 0x0100);  //FLL1 CONTRLO(4)	
	wm8994_write(0x220, 0x0004);  //FLL1 CONTRLO(1)
	mdelay(50);
	wm8994_write(0x220, 0x0005);  //FLL1 CONTRLO(1)

	wm8994_write(0x01,  0x3013);	 
	wm8994_write(0x02,  0x0110);
	wm8994_write(0x03,  0x0330);
	wm8994_write(0x04,  0x3003);
	wm8994_write(0x05,  0x3003); 
	wm8994_write(0x1A,  0x011F);
	wm8994_write(0x22,  0x0000);
	wm8994_write(0x23,  0x0000);
	wm8994_write(0x28,  0x0003);
	wm8994_write(0x2A,  0x0020);
	wm8994_write(0x2D,  0x0001);
	wm8994_write(0x2E,  0x0001);
	wm8994_write(0x36,  0x000C);  //MIXOUTL_TO_SPKMIXL  MIXOUTR_TO_SPKMIXR
//	wm8994_write(0x4C,  0x9F25);
//	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x200, 0x0001);  //AIF1 CLOCKING(1)
	wm8994_write(0x204, 0x0001);  //AIF2 CLOCKING(1)
	wm8994_write(0x208, 0x0007);  //CLOCKING(1)
	wm8994_write(0x520, 0x0000);  //AIF2 DAC FILTERS(1)
	wm8994_write(0x601, 0x0004);  //AIF2DACL_DAC1L
	wm8994_write(0x602, 0x0004);  //AIF2DACR_DAC1R

	wm8994_write(0x610, 0x01C0);  //DAC1L_VOLUME
	wm8994_write(0x611, 0x01C0);  //DAC1R_VOLUME
	wm8994_write(0x612, 0x01C0);  //DAC2L_VOLUME
	wm8994_write(0x613, 0x01C0);  //DAC2R_VOLUME

	wm8994_write(0x702, 0xC100);  //GPIO3
	wm8994_write(0x703, 0xC100);  //GPIO4
	wm8994_write(0x704, 0xC100);  //GPIO5
	wm8994_write(0x706, 0x4100);  //GPIO7
	wm8994_write(0x204, 0x0011);  //AIF2 MCLK=FLL1
	wm8994_write(0x211, 0x0009);  //LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x310, 0x4118);  //DSP/PCM 16bits
	wm8994_write(0x313, 0x00F0);  //AIF2BCLK
	wm8994_write(0x314, 0x0020);  //AIF2ADCLRCK
	wm8994_write(0x315, 0x0020);  //AIF2DACLRCLK

	wm8994_write(0x603, 0x018C);  //Rev.D ADCL SideTone
	wm8994_write(0x604, 0x0010);  //ADC2_TO_DAC2L
	wm8994_write(0x605, 0x0010);  //ADC2_TO_DAC2R
	wm8994_write(0x621, 0x0001);
//	wm8994_write(0x317, 0x0003);
#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x312, 0x4000);  //AIF2 SET AS MASTER
#endif
#endif
}

void  MainMIC_to_PCMBaseband_to_Speakers_and_Record(void)
{
	DBG("Enter::%s----line->%d-- MainMIC_to_PCMBaseband_to_Speakers_and_Record\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_MainMIC_to_PCMBaseband_to_Speakers_and_Record;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01,  0x0013);  
	mdelay(50);
	wm8994_write(0x221, 0x0700);  //MCLK=12MHz
	wm8994_write(0x222, 0x3127);	
	wm8994_write(0x223, 0x0100);	
	wm8994_write(0x220, 0x0004);
	mdelay(50);
	wm8994_write(0x220, 0x0005);  

	wm8994_write(0x01,  0x3013);	 
	wm8994_write(0x02,  0x0110);
	wm8994_write(0x03,  0x0330);
	wm8994_write(0x04,  0x3003);
	wm8994_write(0x05,  0x3003); 
	wm8994_write(0x1A,  0x010B); 
	wm8994_write(0x22,  0x0000);
	wm8994_write(0x23,  0x0000);
	wm8994_write(0x28,  0x0003);
	wm8994_write(0x2A,  0x0020);
	wm8994_write(0x2D,  0x0001);
	wm8994_write(0x2E,  0x0001);
	wm8994_write(0x36,  0x000C);
//	wm8994_write(0x4C,  0x9F25);
//	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x200, 0x0001);	
	wm8994_write(0x204, 0x0001);
	wm8994_write(0x208, 0x0007);	
	wm8994_write(0x520, 0x0000);	
	wm8994_write(0x601, 0x0004);
	wm8994_write(0x602, 0x0004);

	wm8994_write(0x610, 0x01C0);		
	wm8994_write(0x611, 0x01C0);
	wm8994_write(0x612, 0x01C0);		
	wm8994_write(0x613, 0x01C0);

	wm8994_write(0x700, 0x8141);
	wm8994_write(0x702, 0xC100);
	wm8994_write(0x703, 0xC100);
	wm8994_write(0x704, 0xC100);
	wm8994_write(0x706, 0x4100);
	wm8994_write(0x204, 0x0011);  //AIF2 MCLK=FLL1
	wm8994_write(0x211, 0x0009);  //LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x310, 0x4118);  //DSP/PCM 16bits
	wm8994_write(0x313, 0x00F0);
	wm8994_write(0x314, 0x0020);
	wm8994_write(0x315, 0x0020);

	wm8994_write(0x603, 0x018C);  //Rev.D ADCL SideTone
	wm8994_write(0x604, 0x0010);
	wm8994_write(0x605, 0x0010);
	wm8994_write(0x621, 0x0001);
//	wm8994_write(0x317, 0x0003);
#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x312, 0x4000);  //AIF2 SET AS MASTER
#endif

////AIF1
	wm8994_write(0x04,   0x3303);
	wm8994_write(0x200,  0x0001);
	wm8994_write(0x208,  0x000F);
	wm8994_write(0x210,  0x0009);  //LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x300,  0xC118);  //DSP/PCM 16bits, R ADC = L ADC 
	wm8994_write(0x606,  0x0003);	
	wm8994_write(0x607,  0x0003);

////AIF1 Master Clock(SR=8KHz)
	wm8994_write(0x200,  0x0011);
#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302,  0x4000);
#endif
	wm8994_write(0x303,  0x00F0);
	wm8994_write(0x304,  0x0020);
	wm8994_write(0x305,  0x0020);

////AIF1 DAC1 HP
	wm8994_write(0x05,   0x3303);
	wm8994_write(0x420,  0x0000);
	wm8994_write(0x601,  0x0001);
	wm8994_write(0x602,  0x0001);
	wm8994_write(0x700,  0x8140);  //SYNC issue, AIF1 ADCLRC1 from FLL after AIF1 MASTER!!!
}

void  BT_PCMBaseband(void)
{
	DBG("Enter::%s----line->%d-- BT_PCMBaseband\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_BT_PCMBaseband;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01  ,0x0003);
	mdelay (50);

	wm8994_write(0x200 ,0x0001);
	wm8994_write(0x221 ,0x0700);  //MCLK=12MHz
	wm8994_write(0x222 ,0x3127);
	wm8994_write(0x223 ,0x0100);
	wm8994_write(0x220 ,0x0004);
	mdelay (50);
	wm8994_write(0x220 ,0x0005); 

	wm8994_write(0x02  ,0x0000); 
	wm8994_write(0x200 ,0x0011);  // AIF1 MCLK=FLL1
	wm8994_write(0x210 ,0x0009);  // LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x300 ,0x4018);  // DSP/PCM 16bits

	wm8994_write(0x204 ,0x0011);  // AIF2 MCLK=FLL1
	wm8994_write(0x211 ,0x0009);  // LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x310 ,0x4118);  // DSP/PCM 16bits
	wm8994_write(0x208 ,0x000F); 

/////AIF1
	wm8994_write(0x700 ,0x8101);
/////AIF2
	wm8994_write(0x702 ,0xC100);
	wm8994_write(0x703 ,0xC100);
	wm8994_write(0x704 ,0xC100);
	wm8994_write(0x706 ,0x4100);
/////AIF3
	wm8994_write(0x707 ,0xA100); 
	wm8994_write(0x708 ,0xA100);
	wm8994_write(0x709 ,0xA100); 
	wm8994_write(0x70A ,0xA100);

	wm8994_write(0x06 ,0x0001);

	wm8994_write(0x02 ,0x0300);
	wm8994_write(0x03 ,0x0030);
	wm8994_write(0x04 ,0x3301);  //ADCL off
	wm8994_write(0x05 ,0x3301);  //DACL off

//	wm8994_write(0x29 ,0x0005);  
	wm8994_write(0x2A ,0x0005);

	wm8994_write(0x313 ,0x00F0);
	wm8994_write(0x314 ,0x0020);
	wm8994_write(0x315 ,0x0020);

//	wm8994_write(0x2D  ,0x0001);
	wm8994_write(0x2E  ,0x0001);
	wm8994_write(0x420 ,0x0000);
	wm8994_write(0x520 ,0x0000);
//	wm8994_write(0x601 ,0x0001);
	wm8994_write(0x602 ,0x0001);
	wm8994_write(0x604 ,0x0001);
	wm8994_write(0x605 ,0x0001);
//	wm8994_write(0x606 ,0x0002);
	wm8994_write(0x607 ,0x0002);
//	wm8994_write(0x610 ,0x01C0);
	wm8994_write(0x611 ,0x01C0);
	wm8994_write(0x612 ,0x01C0);
	wm8994_write(0x613 ,0x01C0);

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x312 ,0x4000);
#endif

	wm8994_write(0x606 ,0x0001);
	wm8994_write(0x607 ,0x0003);  //R channel for data mix/CPU record data

////////////HP output test
	wm8994_write(0x01  ,0x0303);
	wm8994_write(0x4C  ,0x9F25);
	wm8994_write(0x60  ,0x00EE);
///////////end HP test
}

void  BT_PCMBaseband_and_record(void)
{
	DBG("Enter::%s----line->%d-- BT_PCMBaseband_and_record\n",__FUNCTION__,__LINE__);
	wm8994_mode=wm8994_BT_PCMBaseband_and_record;
	wm8994_reset();
	mdelay(50);

	wm8994_write(0x01  ,0x0003);
	mdelay (50);

	wm8994_write(0x200 ,0x0001);
	wm8994_write(0x221 ,0x0700);  //MCLK=12MHz
	wm8994_write(0x222 ,0x3127);
	wm8994_write(0x223 ,0x0100);
	wm8994_write(0x220 ,0x0004);
	mdelay (50);
	wm8994_write(0x220 ,0x0005); 

	wm8994_write(0x02  ,0x0000); 
	wm8994_write(0x200 ,0x0011);  // AIF1 MCLK=FLL1
	wm8994_write(0x210 ,0x0009);  // LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x300 ,0x4018);  // DSP/PCM 16bits

	wm8994_write(0x204 ,0x0011);  // AIF2 MCLK=FLL1
	wm8994_write(0x211 ,0x0009);  // LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x310 ,0x4118);  // DSP/PCM 16bits
	wm8994_write(0x208 ,0x000F); 

/////AIF1
	wm8994_write(0x700 ,0x8101);
/////AIF2
	wm8994_write(0x702 ,0xC100);
	wm8994_write(0x703 ,0xC100);
	wm8994_write(0x704 ,0xC100);
	wm8994_write(0x706 ,0x4100);
/////AIF3
	wm8994_write(0x707 ,0xA100); 
	wm8994_write(0x708 ,0xA100);
	wm8994_write(0x709 ,0xA100); 
	wm8994_write(0x70A ,0xA100);

	wm8994_write(0x06  ,0x0001);

	wm8994_write(0x02  ,0x0300);
	wm8994_write(0x03  ,0x0030);
	wm8994_write(0x04  ,0x3301);  //ADCL off
	wm8994_write(0x05  ,0x3301);  //DACL off

//	wm8994_write(0x29  ,0x0005);  
	wm8994_write(0x2A  ,0x0005);

	wm8994_write(0x313 ,0x00F0);
	wm8994_write(0x314 ,0x0020);
	wm8994_write(0x315 ,0x0020);

//	wm8994_write(0x2D  ,0x0001);
	wm8994_write(0x2E  ,0x0001);
	wm8994_write(0x420 ,0x0000);
	wm8994_write(0x520 ,0x0000);
//	wm8994_write(0x601 ,0x0001);
	wm8994_write(0x602 ,0x0001);
	wm8994_write(0x604 ,0x0001);
	wm8994_write(0x605 ,0x0001);
//	wm8994_write(0x606 ,0x0002);
	wm8994_write(0x607 ,0x0002);
//	wm8994_write(0x610 ,0x01C0);
	wm8994_write(0x611 ,0x01C0);
	wm8994_write(0x612 ,0x01C0);
	wm8994_write(0x613 ,0x01C0);

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x312 ,0x4000);
#endif

	wm8994_write(0x606 ,0x0001);
	wm8994_write(0x607 ,0x0003);  //R channel for data mix/CPU record data

////////////HP output test
	wm8994_write(0x01  ,0x0303);
	wm8994_write(0x4C  ,0x9F25); 
	wm8994_write(0x60  ,0x00EE); 
///////////end HP test
}


typedef void (wm8994_codec_fnc_t) (void);

wm8994_codec_fnc_t *wm8994_codec_sequence[] = {
	AP_to_Headset,
	AP_to_Speakers,
	Recorder,
	FM_to_Headset,
	FM_to_Headset_and_Record,
	FM_to_Speakers,
	FM_to_Speakers_and_Record,
	HandsetMIC_to_Baseband_to_Headset,
	HandsetMIC_to_Baseband_to_Headset_and_Record,
	MainMIC_to_Baseband_to_Earpiece,
	MainMIC_to_Baseband_to_Earpiece_and_Record,
	MainMIC_to_Baseband_to_Speakers,
	MainMIC_to_Baseband_to_Speakers_and_Record,
	BT_Baseband,
	BT_Baseband_and_record,
///PCM BB BEGIN
	HandsetMIC_to_PCMBaseband_to_Headset,
	HandsetMIC_to_PCMBaseband_to_Headset_and_Record, 
	MainMIC_to_PCMBaseband_to_Earpiece,
	MainMIC_to_PCMBaseband_to_Earpiece_and_Record,
	MainMIC_to_PCMBaseband_to_Speakers,
	MainMIC_to_PCMBaseband_to_Speakers_and_Record,
	BT_PCMBaseband,
	BT_PCMBaseband_and_record
///PCM BB END
};

/********************set wm8994 volume*****volume=0\1\2\3\4\5\6\7*******************/

unsigned char Handset_maxvol=0x3f,VRX_maxvol=0x07,Speaker_maxvol=0x3f,AP_maxvol=0xff,Recorder_maxvol=0x1f,FM_maxvol=0x1f;

void wm8994_codec_set_volume(unsigned char mode,unsigned char volume)
{
	unsigned short lvol=0,rvol=0;

	if(wm8994_mode==wm8994_HandsetMIC_to_Baseband_to_Headset_and_Record||
	wm8994_mode==wm8994_HandsetMIC_to_Baseband_to_Headset)
	{
		wm8994_read(0x001c, &lvol);
		wm8994_read(0x001d, &rvol);
		wm8994_write(0x001c ,(0x0100|(lvol&0xffc0))|(0x003f&(Handset_maxvol*volume/7)));//bit 0~5  -57dB~6dB
		wm8994_write(0x001d ,(0x0100|(rvol&0xffc0))|(0x003f&(Handset_maxvol*volume/7)));//bit 0~5 / -57dB~6dB
	}
	else if(wm8994_mode==wm8994_BT_Baseband_and_record||wm8994_mode==wm8994_BT_Baseband||
	wm8994_mode==wm8994_MainMIC_to_Baseband_to_Earpiece_and_Record||
	wm8994_mode==wm8994_MainMIC_to_Baseband_to_Earpiece)
	{
		wm8994_read(0x002b, &lvol);
		wm8994_write(0x002b ,(0x0100|(lvol&0xfff8))|(0x0007&(VRX_maxvol*volume/7))); //bit 0~2 / -12dB~6dB
	}
	else if(wm8994_mode==wm8994_MainMIC_to_Baseband_to_Speakers_and_Record||
	wm8994_mode==wm8994_MainMIC_to_Baseband_to_Speakers)
	{
		wm8994_read(0x0026, &lvol);
		wm8994_write(0x0026 ,(0x0100|(lvol&0xffc0))|(0x003f&(Speaker_maxvol*volume/7))); //bit0~5 / -57dB~6dB
	}
	else if(wm8994_mode==wm8994_AP_to_Headset||wm8994_mode==wm8994_AP_to_Speakers)
	{
		wm8994_read(0x0610, &lvol);
		wm8994_read(0x0611, &rvol);
		wm8994_write(0x0610 ,(0x0100|(lvol&0xff00))|(0x00ff&(AP_maxvol*volume/7))); //bit 0~7 / -71.625dB~0dB
		wm8994_write(0x0611 ,(0x0100|(rvol&0xff00))|(0x00ff&(AP_maxvol*volume/7))); //bit 0~7 / -71.625dB~0dB
	}
	else if(wm8994_mode==wm8994_Recorder)
	{
		wm8994_read(0x001a, &lvol);
		wm8994_write(0x001a ,(0x0100|(lvol&0xffe0))|(0x001f&(Recorder_maxvol*volume/7))); //bit 0~4 / -16.5dB~30dB
	}
	else if(wm8994_mode==wm8994_FM_to_Headset||wm8994_mode==wm8994_FM_to_Headset_and_Record||
	wm8994_mode==wm8994_FM_to_Speakers||wm8994_mode==wm8994_FM_to_Speakers_and_Record)
	{
		wm8994_read(0x0019, &lvol);
		wm8994_read(0x001b, &rvol);
		wm8994_write(0x0019 ,(0x0100|(lvol&0xffe0))|(0x001f&(FM_maxvol*volume/7))); //bit 0~4 / -16.5dB~30dB
		wm8994_write(0x001b ,(0x0100|(rvol&0xffe0))|(0x001f&(FM_maxvol*volume/7))); //bit 0~4 / -16.5dB~30dB
	}
}


#define SOC_DOUBLE_SWITCH_WM8994CODEC(xname, route) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.info = snd_soc_info_route, \
	.get = snd_soc_get_route, .put = snd_soc_put_route, \
	.private_value = route }

int snd_soc_info_route(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	
	//uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0;
	return 0;
}

int snd_soc_get_route(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	DBG("@@@Enter::%s----line->%d\n",__FUNCTION__,__LINE__);
	return 0;
}

int snd_soc_put_route(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{	
	
	DBG("@@@Enter::%s----line->%d\n",__FUNCTION__,__LINE__);
	int route = kcontrol->private_value & 0xff;

	switch(route)
	{
		/* Speaker*/
		case SPEAKER_NORMAL: //AP-> 8994Codec -> Speaker
#ifndef WM8994_TEST
		    AP_to_Speakers();
#endif
		    break;
		case SPEAKER_INCALL: //BB-> 8994Codec -> Speaker
		    MainMIC_to_Baseband_to_Speakers();
		    break;		
			
		/* Headset */	
		case HEADSET_NORMAL:	//AP-> 8994Codec -> Headset
		    AP_to_Headset();
		    break;
		case HEADSET_INCALL:	//AP-> 8994Codec -> Headset
		    HandsetMIC_to_Baseband_to_Headset();
		    break;		    

		/* Earpiece*/			    
		case EARPIECE_INCALL:	//:BB-> 8994Codec -> EARPIECE
		    MainMIC_to_Baseband_to_Earpiece();
		    break;		

		/* BLUETOOTH_SCO*/		    	
		case BLUETOOTH_SCO_INCALL:	//BB-> 8994Codec -> BLUETOOTH_SCO  
		    BT_Baseband();
		    break;

		/* BLUETOOTH_A2DP*/			    
		case BLUETOOTH_A2DP_NORMAL:	//AP-> 8994Codec -> BLUETOOTH_A2DP
		    break;
		    
		case MIC_CAPTURE:  //
		    Recorder();
		    break;
		default:
		    //codec_daout_route();
		    break;
	}
	return 0;
}

/*
 * WM8994 Controls
 */

static const char *bass_boost_txt[] = {"Linear Control", "Adaptive Boost"};
static const struct soc_enum bass_boost =
	SOC_ENUM_SINGLE(WM8994_BASS, 7, 2, bass_boost_txt);

static const char *bass_filter_txt[] = { "130Hz @ 48kHz", "200Hz @ 48kHz" };
static const struct soc_enum bass_filter =
	SOC_ENUM_SINGLE(WM8994_BASS, 6, 2, bass_filter_txt);

static const char *treble_txt[] = {"8kHz", "4kHz"};
static const struct soc_enum treble =
	SOC_ENUM_SINGLE(WM8994_TREBLE, 6, 2, treble_txt);

static const char *stereo_3d_lc_txt[] = {"200Hz", "500Hz"};
static const struct soc_enum stereo_3d_lc =
	SOC_ENUM_SINGLE(WM8994_3D, 5, 2, stereo_3d_lc_txt);

static const char *stereo_3d_uc_txt[] = {"2.2kHz", "1.5kHz"};
static const struct soc_enum stereo_3d_uc =
	SOC_ENUM_SINGLE(WM8994_3D, 6, 2, stereo_3d_uc_txt);

static const char *stereo_3d_func_txt[] = {"Capture", "Playback"};
static const struct soc_enum stereo_3d_func =
	SOC_ENUM_SINGLE(WM8994_3D, 7, 2, stereo_3d_func_txt);

static const char *alc_func_txt[] = {"Off", "Right", "Left", "Stereo"};
static const struct soc_enum alc_func =
	SOC_ENUM_SINGLE(WM8994_ALC1, 7, 4, alc_func_txt);

static const char *ng_type_txt[] = {"Constant PGA Gain",
				    "Mute ADC Output"};
static const struct soc_enum ng_type =
	SOC_ENUM_SINGLE(WM8994_NGATE, 1, 2, ng_type_txt);

static const char *deemph_txt[] = {"None", "32Khz", "44.1Khz", "48Khz"};
static const struct soc_enum deemph =
	SOC_ENUM_SINGLE(WM8994_ADCDAC, 1, 4, deemph_txt);

static const char *adcpol_txt[] = {"Normal", "L Invert", "R Invert",
				   "L + R Invert"};
static const struct soc_enum adcpol =
	SOC_ENUM_SINGLE(WM8994_ADCDAC, 5, 4, adcpol_txt);

static const DECLARE_TLV_DB_SCALE(pga_tlv, -1725, 75, 0);
static const DECLARE_TLV_DB_SCALE(adc_tlv, -9750, 50, 1);
static const DECLARE_TLV_DB_SCALE(dac_tlv, -12750, 50, 1);
static const DECLARE_TLV_DB_SCALE(out_tlv, -12100, 100, 1);
static const DECLARE_TLV_DB_SCALE(bypass_tlv, -1500, 300, 0);

static const struct snd_kcontrol_new wm8994_snd_controls[] = {

/* 鍠囧彮 */
SOC_DOUBLE_SWITCH_WM8994CODEC("Speaker incall Switch", SPEAKER_INCALL),	
SOC_DOUBLE_SWITCH_WM8994CODEC("Speaker normal Switch", SPEAKER_NORMAL),
/* 鍚瓛 */
SOC_DOUBLE_SWITCH_WM8994CODEC("Earpiece incall Switch", EARPIECE_INCALL),	
SOC_DOUBLE_SWITCH_WM8994CODEC("Earpiece normal Switch", EARPIECE_NORMAL),
/* 鑰虫満 */
SOC_DOUBLE_SWITCH_WM8994CODEC("Headset incall Switch", HEADSET_INCALL),	
SOC_DOUBLE_SWITCH_WM8994CODEC("Headset normal Switch", HEADSET_NORMAL),
/* 钃濈墮SCO */
SOC_DOUBLE_SWITCH_WM8994CODEC("Bluetooth incall Switch", BLUETOOTH_SCO_INCALL),	
SOC_DOUBLE_SWITCH_WM8994CODEC("Bluetooth normal Switch", BLUETOOTH_SCO_NORMAL),
/* 钃濈墮A2DP */
SOC_DOUBLE_SWITCH_WM8994CODEC("Bluetooth-A2DP incall Switch", BLUETOOTH_A2DP_INCALL),	
SOC_DOUBLE_SWITCH_WM8994CODEC("Bluetooth-A2DP normal Switch", BLUETOOTH_A2DP_NORMAL),
/* 鑰抽害 */
SOC_DOUBLE_SWITCH_WM8994CODEC("Capture Switch", MIC_CAPTURE),

};

/*
 * DAPM Controls
 */

static int wm8994_lrc_control(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	u16 adctl2 = snd_soc_read(codec, WM8994_ADCTL2);

	/* Use the DAC to gate LRC if active, otherwise use ADC */
	if (snd_soc_read(codec, WM8994_PWR2) & 0x180)
		adctl2 &= ~0x4;
	else
		adctl2 |= 0x4;

	DBG("Enter::%s----line->%d, adctl2 = %x\n",__FUNCTION__,__LINE__,adctl2);
	
	return snd_soc_write(codec, WM8994_ADCTL2, adctl2);
}

static const char *wm8994_line_texts[] = {
	"Line 1", "Line 2", "PGA", "Differential"};

static const unsigned int wm8994_line_values[] = {
	0, 1, 3, 4};

static const struct soc_enum wm8994_lline_enum =
	SOC_VALUE_ENUM_SINGLE(WM8994_LOUTM1, 0, 7,
			      ARRAY_SIZE(wm8994_line_texts),
			      wm8994_line_texts,
			      wm8994_line_values);
static const struct snd_kcontrol_new wm8994_left_line_controls =
	SOC_DAPM_VALUE_ENUM("Route", wm8994_lline_enum);

static const struct soc_enum wm8994_rline_enum =
	SOC_VALUE_ENUM_SINGLE(WM8994_ROUTM1, 0, 7,
			      ARRAY_SIZE(wm8994_line_texts),
			      wm8994_line_texts,
			      wm8994_line_values);
static const struct snd_kcontrol_new wm8994_right_line_controls =
	SOC_DAPM_VALUE_ENUM("Route", wm8994_lline_enum);

/* Left Mixer */
static const struct snd_kcontrol_new wm8994_left_mixer_controls[] = {
	SOC_DAPM_SINGLE("Playback Switch", WM8994_LOUTM1, 8, 1, 0),
	SOC_DAPM_SINGLE("Left Bypass Switch", WM8994_LOUTM1, 7, 1, 0),
	SOC_DAPM_SINGLE("Right Playback Switch", WM8994_LOUTM2, 8, 1, 0),
	SOC_DAPM_SINGLE("Right Bypass Switch", WM8994_LOUTM2, 7, 1, 0),
};

/* Right Mixer */
static const struct snd_kcontrol_new wm8994_right_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left Playback Switch", WM8994_ROUTM1, 8, 1, 0),
	SOC_DAPM_SINGLE("Left Bypass Switch", WM8994_ROUTM1, 7, 1, 0),
	SOC_DAPM_SINGLE("Playback Switch", WM8994_ROUTM2, 8, 1, 0),
	SOC_DAPM_SINGLE("Right Bypass Switch", WM8994_ROUTM2, 7, 1, 0),
};

static const char *wm8994_pga_sel[] = {"Line 1", "Line 2", "Differential"};
static const unsigned int wm8994_pga_val[] = { 0, 1, 3 };

/* Left PGA Mux */
static const struct soc_enum wm8994_lpga_enum =
	SOC_VALUE_ENUM_SINGLE(WM8994_LADCIN, 6, 3,
			      ARRAY_SIZE(wm8994_pga_sel),
			      wm8994_pga_sel,
			      wm8994_pga_val);
static const struct snd_kcontrol_new wm8994_left_pga_controls =
	SOC_DAPM_VALUE_ENUM("Route", wm8994_lpga_enum);

/* Right PGA Mux */
static const struct soc_enum wm8994_rpga_enum =
	SOC_VALUE_ENUM_SINGLE(WM8994_RADCIN, 6, 3,
			      ARRAY_SIZE(wm8994_pga_sel),
			      wm8994_pga_sel,
			      wm8994_pga_val);
static const struct snd_kcontrol_new wm8994_right_pga_controls =
	SOC_DAPM_VALUE_ENUM("Route", wm8994_rpga_enum);

/* Differential Mux */
static const char *wm8994_diff_sel[] = {"Line 1", "Line 2"};
static const struct soc_enum diffmux =
	SOC_ENUM_SINGLE(WM8994_ADCIN, 8, 2, wm8994_diff_sel);
static const struct snd_kcontrol_new wm8994_diffmux_controls =
	SOC_DAPM_ENUM("Route", diffmux);

/* Mono ADC Mux */
static const char *wm8994_mono_mux[] = {"Stereo", "Mono (Left)",
	"Mono (Right)", "Digital Mono"};
static const struct soc_enum monomux =
	SOC_ENUM_SINGLE(WM8994_ADCIN, 6, 4, wm8994_mono_mux);
static const struct snd_kcontrol_new wm8994_monomux_controls =
	SOC_DAPM_ENUM("Route", monomux);

static const struct snd_soc_dapm_widget wm8994_dapm_widgets[] = {
	SND_SOC_DAPM_MICBIAS("Mic Bias", WM8994_PWR1, 1, 0),

	SND_SOC_DAPM_MUX("Differential Mux", SND_SOC_NOPM, 0, 0,
		&wm8994_diffmux_controls),
	SND_SOC_DAPM_MUX("Left ADC Mux", SND_SOC_NOPM, 0, 0,
		&wm8994_monomux_controls),
	SND_SOC_DAPM_MUX("Right ADC Mux", SND_SOC_NOPM, 0, 0,
		&wm8994_monomux_controls),

	SND_SOC_DAPM_MUX("Left PGA Mux", WM8994_PWR1, 5, 0,
		&wm8994_left_pga_controls),
	SND_SOC_DAPM_MUX("Right PGA Mux", WM8994_PWR1, 4, 0,
		&wm8994_right_pga_controls),

	SND_SOC_DAPM_MUX("Left Line Mux", SND_SOC_NOPM, 0, 0,
		&wm8994_left_line_controls),
	SND_SOC_DAPM_MUX("Right Line Mux", SND_SOC_NOPM, 0, 0,
		&wm8994_right_line_controls),

	SND_SOC_DAPM_ADC("Right ADC", "Right Capture", WM8994_PWR1, 2, 0),
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", WM8994_PWR1, 3, 0),

	SND_SOC_DAPM_DAC("Right DAC", "Right Playback", WM8994_PWR2, 7, 0),
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback", WM8994_PWR2, 8, 0),

	SND_SOC_DAPM_MIXER("Left Mixer", SND_SOC_NOPM, 0, 0,
		&wm8994_left_mixer_controls[0],
		ARRAY_SIZE(wm8994_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right Mixer", SND_SOC_NOPM, 0, 0,
		&wm8994_right_mixer_controls[0],
		ARRAY_SIZE(wm8994_right_mixer_controls)),

	SND_SOC_DAPM_PGA("Right Out 2", WM8994_PWR2, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 2", WM8994_PWR2, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Out 1", WM8994_PWR2, 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 1", WM8994_PWR2, 6, 0, NULL, 0),

	SND_SOC_DAPM_POST("LRC control", wm8994_lrc_control),

	SND_SOC_DAPM_OUTPUT("LOUT1"),
	SND_SOC_DAPM_OUTPUT("ROUT1"),
	SND_SOC_DAPM_OUTPUT("LOUT2"),
	SND_SOC_DAPM_OUTPUT("ROUT2"),
	SND_SOC_DAPM_OUTPUT("VREF"),

	SND_SOC_DAPM_INPUT("LINPUT1"),
	SND_SOC_DAPM_INPUT("LINPUT2"),
	SND_SOC_DAPM_INPUT("RINPUT1"),
	SND_SOC_DAPM_INPUT("RINPUT2"),
};

static const struct snd_soc_dapm_route audio_map[] = {

	{ "Left Line Mux", "Line 1", "LINPUT1" },
	{ "Left Line Mux", "Line 2", "LINPUT2" },
	{ "Left Line Mux", "PGA", "Left PGA Mux" },
	{ "Left Line Mux", "Differential", "Differential Mux" },

	{ "Right Line Mux", "Line 1", "RINPUT1" },
	{ "Right Line Mux", "Line 2", "RINPUT2" },
	{ "Right Line Mux", "PGA", "Right PGA Mux" },
	{ "Right Line Mux", "Differential", "Differential Mux" },

	{ "Left PGA Mux", "Line 1", "LINPUT1" },
	{ "Left PGA Mux", "Line 2", "LINPUT2" },
	{ "Left PGA Mux", "Differential", "Differential Mux" },

	{ "Right PGA Mux", "Line 1", "RINPUT1" },
	{ "Right PGA Mux", "Line 2", "RINPUT2" },
	{ "Right PGA Mux", "Differential", "Differential Mux" },

	{ "Differential Mux", "Line 1", "LINPUT1" },
	{ "Differential Mux", "Line 1", "RINPUT1" },
	{ "Differential Mux", "Line 2", "LINPUT2" },
	{ "Differential Mux", "Line 2", "RINPUT2" },

	{ "Left ADC Mux", "Stereo", "Left PGA Mux" },
	{ "Left ADC Mux", "Mono (Left)", "Left PGA Mux" },
	{ "Left ADC Mux", "Digital Mono", "Left PGA Mux" },

	{ "Right ADC Mux", "Stereo", "Right PGA Mux" },
	{ "Right ADC Mux", "Mono (Right)", "Right PGA Mux" },
	{ "Right ADC Mux", "Digital Mono", "Right PGA Mux" },

	{ "Left ADC", NULL, "Left ADC Mux" },
	{ "Right ADC", NULL, "Right ADC Mux" },

	{ "Left Line Mux", "Line 1", "LINPUT1" },
	{ "Left Line Mux", "Line 2", "LINPUT2" },
	{ "Left Line Mux", "PGA", "Left PGA Mux" },
	{ "Left Line Mux", "Differential", "Differential Mux" },

	{ "Right Line Mux", "Line 1", "RINPUT1" },
	{ "Right Line Mux", "Line 2", "RINPUT2" },
	{ "Right Line Mux", "PGA", "Right PGA Mux" },
	{ "Right Line Mux", "Differential", "Differential Mux" },

	{ "Left Mixer", "Playback Switch", "Left DAC" },
	{ "Left Mixer", "Left Bypass Switch", "Left Line Mux" },
	{ "Left Mixer", "Right Playback Switch", "Right DAC" },
	{ "Left Mixer", "Right Bypass Switch", "Right Line Mux" },

	{ "Right Mixer", "Left Playback Switch", "Left DAC" },
	{ "Right Mixer", "Left Bypass Switch", "Left Line Mux" },
	{ "Right Mixer", "Playback Switch", "Right DAC" },
	{ "Right Mixer", "Right Bypass Switch", "Right Line Mux" },

	{ "Left Out 1", NULL, "Left Mixer" },
	{ "LOUT1", NULL, "Left Out 1" },
	{ "Right Out 1", NULL, "Right Mixer" },
	{ "ROUT1", NULL, "Right Out 1" },

	{ "Left Out 2", NULL, "Left Mixer" },
	{ "LOUT2", NULL, "Left Out 2" },
	{ "Right Out 2", NULL, "Right Mixer" },
	{ "ROUT2", NULL, "Right Out 2" },
};

struct _coeff_div {
	u32 mclk;
	u32 rate;
	u16 fs;
	u8 sr:5;
	u8 usb:1;
};

/* codec hifi mclk clock divider coefficients */
static const struct _coeff_div coeff_div[] = {
	/* 8k */
	{12288000, 8000, 1536, 0x6, 0x0},
	{11289600, 8000, 1408, 0x16, 0x0},
	{18432000, 8000, 2304, 0x7, 0x0},
	{16934400, 8000, 2112, 0x17, 0x0},
	{12000000, 8000, 1500, 0x6, 0x1},

	/* 11.025k */
	{11289600, 11025, 1024, 0x18, 0x0},
	{16934400, 11025, 1536, 0x19, 0x0},
	{12000000, 11025, 1088, 0x19, 0x1},

	/* 16k */
	{12288000, 16000, 768, 0xa, 0x0},
	{18432000, 16000, 1152, 0xb, 0x0},
	{12000000, 16000, 750, 0xa, 0x1},

	/* 22.05k */
	{11289600, 22050, 512, 0x1a, 0x0},
	{16934400, 22050, 768, 0x1b, 0x0},
	{12000000, 22050, 544, 0x1b, 0x1},

	/* 32k */
	{12288000, 32000, 384, 0xc, 0x0},
	{18432000, 32000, 576, 0xd, 0x0},
	{12000000, 32000, 375, 0xa, 0x1},

	/* 44.1k */
	{11289600, 44100, 256, 0x10, 0x0},
	{16934400, 44100, 384, 0x11, 0x0},
	{12000000, 44100, 272, 0x11, 0x1},

	/* 48k */
	{12288000, 48000, 256, 0x0, 0x0},
	{18432000, 48000, 384, 0x1, 0x0},
	{12000000, 48000, 250, 0x0, 0x1},

	/* 88.2k */
	{11289600, 88200, 128, 0x1e, 0x0},
	{16934400, 88200, 192, 0x1f, 0x0},
	{12000000, 88200, 136, 0x1f, 0x1},

	/* 96k */
	{12288000, 96000, 128, 0xe, 0x0},
	{18432000, 96000, 192, 0xf, 0x0},
	{12000000, 96000, 125, 0xe, 0x1},
};


static inline int get_coeff(int mclk, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
			return i;
	}

	return -EINVAL;
}

/* The set of rates we can generate from the above for each SYSCLK */

static unsigned int rates_12288[] = {
	8000, 12000, 16000, 24000, 24000, 32000, 48000, 96000,
};

static struct snd_pcm_hw_constraint_list constraints_12288 = {
	.count	= ARRAY_SIZE(rates_12288),
	.list	= rates_12288,
};

static unsigned int rates_112896[] = {
	8000, 11025, 22050, 44100,
};

static struct snd_pcm_hw_constraint_list constraints_112896 = {
	.count	= ARRAY_SIZE(rates_112896),
	.list	= rates_112896,
};

static unsigned int rates_12[] = {
	8000, 11025, 12000, 16000, 22050, 2400, 32000, 41100, 48000,
	48000, 88235, 96000,
};

static struct snd_pcm_hw_constraint_list constraints_12 = {
	.count	= ARRAY_SIZE(rates_12),
	.list	= rates_12,
};

/*
 * Note that this should be called from init rather than from hw_params.
 */
static int wm8994_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8994_priv *wm8994 = codec->private_data;
	
	DBG("Enter::%s----line->%d\n",__FUNCTION__,__LINE__);
		
	switch (freq) {
	case 11289600:
	case 18432000:
	case 22579200:
	case 36864000:
		wm8994->sysclk_constraints = &constraints_112896;
		wm8994->sysclk = freq;
		return 0;

	case 12288000:
	case 16934400:
	case 24576000:
	case 33868800:
		wm8994->sysclk_constraints = &constraints_12288;
		wm8994->sysclk = freq;
		return 0;

	case 12000000:
	case 24000000:
		wm8994->sysclk_constraints = &constraints_12;
		wm8994->sysclk = freq;
		return 0;
	}
	return -EINVAL;
}

static int wm8994_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = 0;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface = 0x0040;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0002;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0001;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x0003;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface |= 0x0013;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x0090;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x0080;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x0010;
		break;
	default:
		return -EINVAL;
	}

	DBG("Enter::%s----line->%d  iface=%x\n",__FUNCTION__,__LINE__,iface);
	snd_soc_write(codec, WM8994_IFACE, iface);
	return 0;
}

static int wm8994_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8994_priv *wm8994 = codec->private_data;
	
	/* The set of sample rates that can be supported depends on the
	 * MCLK supplied to the CODEC - enforce this.
	 */
	DBG("Enter::%s----line->%d  wm8994->sysclk=%d\n",__FUNCTION__,__LINE__,wm8994->sysclk); 
	if (!wm8994->sysclk) {
		dev_err(codec->dev,
			"No MCLK configured, call set_sysclk() on init\n");
		return -EINVAL;
	}

	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   wm8994->sysclk_constraints);

	return 0;
}

static int wm8994_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct wm8994_priv *wm8994 = codec->private_data;
	u16 iface = snd_soc_read(codec, WM8994_IFACE) & 0x1f3;
	u16 srate = snd_soc_read(codec, WM8994_SRATE) & 0x180;
	int coeff;
	
	coeff = get_coeff(wm8994->sysclk, params_rate(params));
	if (coeff < 0) {
		coeff = get_coeff(wm8994->sysclk / 2, params_rate(params));
		srate |= 0x40;
	}
	if (coeff < 0) {
		dev_err(codec->dev,
			"Unable to configure sample rate %dHz with %dHz MCLK\n",
			params_rate(params), wm8994->sysclk);
		return coeff;
	}

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= 0x0004;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= 0x0008;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		iface |= 0x000c;
		break;
	}
	DBG("Enter::%s----line->%d  iface=%x srate =%x rate=%d\n",__FUNCTION__,__LINE__,iface,srate,params_rate(params));

	/* set iface & srate */
	snd_soc_write(codec, WM8994_IFACE, iface);
	if (coeff >= 0)
		snd_soc_write(codec, WM8994_SRATE, srate |
			(coeff_div[coeff].sr << 1) | coeff_div[coeff].usb);

	return 0;
}

static int wm8994_mute(struct snd_soc_dai *dai, int mute)
{
	return 0;
}

static int wm8994_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	u16 pwr_reg = snd_soc_read(codec, WM8994_PWR1) & ~0x1c1;
	DBG("Enter::%s----line->%d level =%d\n",__FUNCTION__,__LINE__,level);
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* VREF, VMID=2x50k, digital enabled */
		snd_soc_write(codec, WM8994_PWR1, pwr_reg | 0x00c0);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->bias_level == SND_SOC_BIAS_OFF) {
			/* VREF, VMID=2x5k */
			snd_soc_write(codec, WM8994_PWR1, pwr_reg | 0x1c1);

			/* Charge caps */
			msleep(100);
		}

		/* VREF, VMID=2*500k, digital stopped */
		snd_soc_write(codec, WM8994_PWR1, pwr_reg | 0x0141);
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_write(codec, WM8994_PWR1, 0x0000);
		break;
	}
	codec->bias_level = level;
	return 0;
}

//#define WM8994_RATES SNDRV_PCM_RATE_8000_96000//cjq

#define WM8994_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)

#define WM8994_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops wm8994_ops = {
	.startup = wm8994_pcm_startup,
	.hw_params = wm8994_pcm_hw_params,
	.set_fmt = wm8994_set_dai_fmt,
	.set_sysclk = wm8994_set_dai_sysclk,
	.digital_mute = wm8994_mute,
};

struct snd_soc_dai wm8994_dai = {
	.name = "WM8994",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8994_RATES,
		.formats = WM8994_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8994_RATES,
		.formats = WM8994_FORMATS,
	 },
	.ops = &wm8994_ops,
	.symmetric_rates = 1,
};
EXPORT_SYMBOL_GPL(wm8994_dai);

static int wm8994_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	DBG("Enter::%s----line->%d\n",__FUNCTION__,__LINE__);
	wm8994_set_bias_level(codec, SND_SOC_BIAS_OFF);
	wm8994_reset();
	mdelay(50);
	return 0;
}

static int wm8994_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	int i;
	u8 data[2];
	u16 *cache = codec->reg_cache;
	wm8994_codec_fnc_t **wm8994_fnc_ptr=wm8994_codec_sequence;
	DBG("Enter::%s----line->%d\n",__FUNCTION__,__LINE__);
	/* Sync reg_cache with the hardware */
	for (i = 0; i < WM8994_NUM_REG; i++) {
		if (i == WM8994_RESET)
			continue;
		data[0] = (i << 1) | ((cache[i] >> 8) & 0x0001);
		data[1] = cache[i] & 0x00ff;
		codec->hw_write(codec->control_data, data, 2);
	}

	wm8994_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	if(wm8994_mode<=wm8994_AP_to_Speakers)
	{
		wm8994_fnc_ptr+=wm8994_mode;
		(*wm8994_fnc_ptr)() ;
	}
	else
	{
		wm8994_fnc_ptr+=wm8994_mode;
		(*wm8994_fnc_ptr)() ;
		printk("%s----line->%d--: Wm8994 resume with error mode\n",__FUNCTION__,__LINE__);
	}

	return 0;
}

static struct snd_soc_codec *wm8994_codec;

static int wm8994_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret = 0;

	if (wm8994_codec == NULL) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = wm8994_codec;
	codec = wm8994_codec;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms: %d\n", ret);
		goto pcm_err;
	}

	snd_soc_add_controls(codec, wm8994_snd_controls,
				ARRAY_SIZE(wm8994_snd_controls));
	snd_soc_dapm_new_controls(codec, wm8994_dapm_widgets,
				  ARRAY_SIZE(wm8994_dapm_widgets));
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));
	snd_soc_dapm_new_widgets(codec);

	ret = snd_soc_init_card(socdev);
	if (ret < 0) {
		dev_err(codec->dev, "failed to register card: %d\n", ret);
		goto card_err;
	}

	return ret;

card_err:
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
pcm_err:
	return ret;
}

static int wm8994_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_wm8994 = {
	.probe = 	wm8994_probe,
	.remove = 	wm8994_remove,
	.suspend = 	wm8994_suspend,
	.resume =	wm8994_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_wm8994);

static int wm8994_register(struct wm8994_priv *wm8994,
			   enum snd_soc_control_type control)
{
	struct snd_soc_codec *codec = &wm8994->codec;
	int ret;
	u16 reg;

	/*************text----------cjq**************/
	DBG("\n\n\nEnter::%s----line->%d-- WM8994 test begin\n",__FUNCTION__,__LINE__);
	AP_to_Headset();
	//HandsetMIC_to_Baseband_to_Headset();
	DBG("Enter::%s----line->%d-- WM8994 test end\n\n\n\n",__FUNCTION__,__LINE__);

	if (wm8994_codec) {
		dev_err(codec->dev, "Another WM8994 is registered\n");
		ret = -EINVAL;
		goto err;
	}

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	codec->private_data = wm8994;
	codec->name = "WM8994";
	codec->owner = THIS_MODULE;
	codec->dai = &wm8994_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = ARRAY_SIZE(wm8994->reg_cache);
	codec->reg_cache = &wm8994->reg_cache;
	codec->bias_level = SND_SOC_BIAS_OFF;
	codec->set_bias_level = wm8994_set_bias_level;

	memcpy(codec->reg_cache, wm8994_reg,
	       sizeof(wm8994_reg));

	ret = snd_soc_codec_set_cache_io(codec, 7, 9, control);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		goto err;
	}

	ret = 0;//wm8994_reset(); cjq
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		goto err;
	}
#if 1
		/*disable speaker */
		gpio_request(RK2818_PIN_PF7, "WM8994");	
		rk2818_mux_api_set(GPIOE_SPI1_FLASH_SEL_NAME, IOMUXA_GPIO1_A3B7);
		gpio_direction_output(RK2818_PIN_PF7,GPIO_HIGH);
		
#endif
	/* set the update bits (we always update left then right) */
	reg = snd_soc_read(codec, WM8994_RADC);
	snd_soc_write(codec, WM8994_RADC, reg | 0x100);
	reg = snd_soc_read(codec, WM8994_RDAC);
	snd_soc_write(codec, WM8994_RDAC, reg | 0x0100);
	reg = snd_soc_read(codec, WM8994_ROUT1V);
	snd_soc_write(codec, WM8994_ROUT1V, reg | 0x0100);
	reg = snd_soc_read(codec, WM8994_ROUT2V);
	snd_soc_write(codec, WM8994_ROUT2V, reg | 0x0100);
	reg = snd_soc_read(codec, WM8994_RINVOL);
	snd_soc_write(codec, WM8994_RINVOL, reg | 0x0100); 
	
	snd_soc_write(codec, WM8994_LOUTM1, 0x120); 
	snd_soc_write(codec, WM8994_ROUTM2, 0x120);  
	snd_soc_write(codec, WM8994_LOUTM2, 0x0070);
	snd_soc_write(codec, WM8994_ROUTM1, 0x0070);
	
	snd_soc_write(codec, WM8994_LOUT1V, 0x017f); 
	snd_soc_write(codec, WM8994_ROUT1V, 0x017f);
	snd_soc_write(codec, WM8994_LDAC, 0xff);  
	snd_soc_write(codec, WM8994_RDAC, 0x1ff);//vol set 
	
	snd_soc_write(codec, WM8994_SRATE,0x100);  ///SET MCLK/8
	snd_soc_write(codec, WM8994_PWR1, 0x1cc);  ///(0x80|0x40|0x20|0x08|0x04|0x10|0x02));
 	snd_soc_write(codec, WM8994_PWR2, 0x1e0);  //power r l out1


	wm8994_set_bias_level(&wm8994->codec, SND_SOC_BIAS_STANDBY);

	wm8994_dai.dev = codec->dev;

	wm8994_codec = codec;

	ret = snd_soc_register_codec(codec);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register codec: %d\n", ret);
		goto err;
	}

	ret = snd_soc_register_dai(&wm8994_dai);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register DAI: %d\n", ret);
		snd_soc_unregister_codec(codec);
		goto err_codec;
	}
	return 0;

err_codec:
	snd_soc_unregister_codec(codec);
err:
	kfree(wm8994);
	return ret;
}

static void wm8994_unregister(struct wm8994_priv *wm8994)
{
	wm8994_set_bias_level(&wm8994->codec, SND_SOC_BIAS_OFF);
	snd_soc_unregister_dai(&wm8994_dai);
	snd_soc_unregister_codec(&wm8994->codec);
	kfree(wm8994);
	wm8994_codec = NULL;
}

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static int wm8994_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8994_priv *wm8994;
	struct snd_soc_codec *codec;
	wm8994_client=i2c;
	wm8994 = kzalloc(sizeof(struct wm8994_priv), GFP_KERNEL);
	if (wm8994 == NULL)
		return -ENOMEM;

	codec = &wm8994->codec;

	i2c_set_clientdata(i2c, wm8994);
	codec->control_data = i2c;

	codec->dev = &i2c->dev;

	return wm8994_register(wm8994, SND_SOC_I2C);
}

static int wm8994_i2c_remove(struct i2c_client *client)
{
	struct wm8994_priv *wm8994 = i2c_get_clientdata(client);
	wm8994_unregister(wm8994);
	return 0;
}

#ifdef CONFIG_PM
static int wm8994_i2c_suspend(struct i2c_client *client, pm_message_t msg)
{
	return snd_soc_suspend_device(&client->dev);
}

static int wm8994_i2c_resume(struct i2c_client *client)
{
	return snd_soc_resume_device(&client->dev);
}
#else
#define wm8994_i2c_suspend NULL
#define wm8994_i2c_resume NULL
#endif

static const struct i2c_device_id wm8994_i2c_id[] = {
	{ "wm8994", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8994_i2c_id);

static struct i2c_driver wm8994_i2c_driver = {
	.driver = {
		.name = "WM8994",
		.owner = THIS_MODULE,
	},
	.probe = wm8994_i2c_probe,
	.remove = wm8994_i2c_remove,
	.suspend = wm8994_i2c_suspend,
	.resume = wm8994_i2c_resume,
	.id_table = wm8994_i2c_id,
};

int reg_send_data(struct i2c_client *client, unsigned short *reg, unsigned short *data, u32 scl_rate)
{
	int ret;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	char tx_buf[4];

	memcpy(tx_buf, reg, 2);
	memcpy(tx_buf+2, data, 2);
	msg.addr = client->addr;
	msg.buf = tx_buf;
	msg.len = 4;
	msg.flags = client->flags;
	msg.scl_rate = scl_rate;
    
	ret = i2c_transfer(adap, &msg, 1);

	return ret;
}

int reg_recv_data(struct i2c_client *client, unsigned short *reg, unsigned short *buf, u32 scl_rate)
{
	int ret;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msgs[2];

	msgs[0].addr = client->addr;
	msgs[0].buf = (char *)reg;
	msgs[0].flags = client->flags;
	msgs[0].len = 2;
	msgs[0].scl_rate = scl_rate;

	msgs[1].addr = client->addr;
	msgs[1].buf = (char *)buf;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = 2;
	msgs[1].scl_rate = scl_rate;

	ret = i2c_transfer(adap, msgs, 2);

	return ret;
}

#endif

#if defined(CONFIG_SPI_MASTER)
static int __devinit wm8994_spi_probe(struct spi_device *spi)
{
	struct wm8994_priv *wm8994;
	struct snd_soc_codec *codec;

	wm8994 = kzalloc(sizeof(struct wm8994_priv), GFP_KERNEL);
	if (wm8994 == NULL)
		return -ENOMEM;

	codec = &wm8994->codec;
	codec->control_data = spi;
	codec->dev = &spi->dev;

	dev_set_drvdata(&spi->dev, wm8994);

	return wm8994_register(wm8994, SND_SOC_SPI);
}

static int __devexit wm8994_spi_remove(struct spi_device *spi)
{
	struct wm8994_priv *wm8994 = dev_get_drvdata(&spi->dev);

	wm8994_unregister(wm8994);

	return 0;
}

#ifdef CONFIG_PM
static int wm8994_spi_suspend(struct spi_device *spi, pm_message_t msg)
{
	return snd_soc_suspend_device(&spi->dev);
}

static int wm8994_spi_resume(struct spi_device *spi)
{
	return snd_soc_resume_device(&spi->dev);
}
#else
#define wm8994_spi_suspend NULL
#define wm8994_spi_resume NULL
#endif

static struct spi_driver wm8994_spi_driver = {
	.driver = {
		.name	= "wm8994",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm8994_spi_probe,
	.remove		= __devexit_p(wm8994_spi_remove),
	.suspend	= wm8994_spi_suspend,
	.resume		= wm8994_spi_resume,
};
#endif

static int __init wm8994_modinit(void)
{
	int ret;

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8994_i2c_driver);
	if (ret != 0)
		pr_err("WM8994: Unable to register I2C driver: %d\n", ret);
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8994_spi_driver);
	if (ret != 0)
		pr_err("WM8994: Unable to register SPI driver: %d\n", ret);
#endif
	return ret;
}
module_init(wm8994_modinit);

static void __exit wm8994_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8994_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8994_spi_driver);
#endif
}
module_exit(wm8994_exit);


MODULE_DESCRIPTION("ASoC WM8994 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");

