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
#include <linux/miscdevice.h>
#include <linux/circ_buf.h>
#include <mach/spi_fpga.h>

/* If digital BB is used,open this define. */
//#define PCM_BB

/* Open wm8994 with diferent ways. */
//#define chinaone
#define rk28SDK

/* Define what kind of digital BB is used. */
#ifdef PCM_BB
#define TD688_MODE  
//#define MU301_MODE
//#define CHONGY_MODE
//#define THINKWILL_M800_MODE
#endif //PCM_BB

#if 1
#define DBG(x...) printk(KERN_INFO x)
#else
#define DBG(x...) do { } while (0)
#endif
#define wm8994_mic_VCC 0x0010
#define WM8994_DELAY 50

struct i2c_client *wm8994_client;
//struct wm8994_board wm8994_codec_board_data;
int reg_send_data(struct i2c_client *client, unsigned short *reg, unsigned short *data, u32 scl_rate);
int reg_recv_data(struct i2c_client *client, unsigned short *reg, unsigned short *buf, u32 scl_rate);

enum wm8994_codec_mode
{
  wm8994_AP_to_headset,
  wm8994_AP_to_speakers,
  wm8994_recorder_and_AP_to_headset,
  wm8994_recorder_and_AP_to_speakers,
  wm8994_FM_to_headset,
  wm8994_FM_to_headset_and_record,
  wm8994_FM_to_speakers,
  wm8994_FM_to_speakers_and_record,
  wm8994_handsetMIC_to_baseband_to_headset,
  wm8994_handsetMIC_to_baseband_to_headset_and_record,
  wm8994_mainMIC_to_baseband_to_earpiece,
  wm8994_mainMIC_to_baseband_to_earpiece_and_record,
  wm8994_mainMIC_to_baseband_to_speakers,
  wm8994_mainMIC_to_baseband_with_AP_to_speakers,
  wm8994_mainMIC_to_baseband_to_speakers_and_record,
  wm8994_BT_baseband,
  wm8994_BT_baseband_and_record,
  null
};

/* wm8994_current_mode:save current wm8994 mode */
unsigned char wm8994_current_mode=null;//,wm8994_mic_VCC=0x0000;

void wm8994_set_volume(unsigned char wm8994_mode,unsigned char volume,unsigned char max_volume);

enum stream_type_wm8994
{
	VOICE_CALL	=0,
	BLUETOOTH_SCO,
};

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

	EARPIECE_RINGTONE,
	SPEAKER_RINGTONE,
	HEADSET_RINGTONE,
	
	ALL_OPEN,
	ALL_CLOSED
};


#define call_maxvol 5

/* call_vol:  save all kinds of system volume value. */
unsigned char call_vol=3;
int vol;
unsigned short headset_vol_table[6]	={0x0100,0x011d,0x012d,0x0135,0x013b,0x013f};
unsigned short speakers_vol_table[6]	={0x0100,0x011d,0x012d,0x0135,0x013b,0x013f};
unsigned short earpiece_vol_table[6]	={0x0100,0x011d,0x012d,0x0135,0x013b,0x013f};
unsigned short BT_vol_table[6]		={0x0100,0x011d,0x012d,0x0135,0x013b,0x013f};

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

	if (reg_recv_data(wm8994_client,&regs,&values,400000) > 0)
	{
		*value=((values>>8)& 0x00FF)|((values<<8)&0xFF00);
		return 0;
	}

	printk("%s---line->%d:Codec read error! reg = 0x%x , value = 0x%x\n",__FUNCTION__,__LINE__,reg,*value);

	return -EIO;
}
	

static int wm8994_write(unsigned short reg,unsigned short value)
{
	unsigned short regs=((reg>>8)&0x00FF)|((reg<<8)&0xFF00),values=((value>>8)&0x00FF)|((value<<8)&0xFF00);

	if (reg_send_data(wm8994_client,&regs,&values,400000) > 0)
		return 0;

	printk("%s---line->%d:Codec write error! reg = 0x%x , value = 0x%x\n",__FUNCTION__,__LINE__,reg,value);

	return -EIO;
}

#define wm8994_reset()	wm8994_write(WM8994_RESET, 0)
void AP_to_headset(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_AP_to_headset)return;
	wm8994_current_mode=wm8994_AP_to_headset;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01,  0x0003);
	msleep(WM8994_DELAY);

	wm8994_write(0x200, 0x0001);
	wm8994_write(0x220, 0x0000);
	wm8994_write(0x221, 0x0700);
	wm8994_write(0x222, 0x3126);
	wm8994_write(0x223, 0x0100);

	wm8994_write(0x210, 0x0083); // SR=48KHz
	wm8994_write(0x220, 0x0004);  
	msleep(WM8994_DELAY);
	wm8994_write(0x220, 0x0005);
	wm8994_write(0x200, 0x0011);  // sysclk = fll (bit4 =1)   0x0011
	wm8994_write(0x300, 0x4010);  // i2s 16 bits
  
	wm8994_write(0x04,  0x0303); // AIF1ADC1L_ENA=1, AIF1ADC1R_ENA=1, ADCL_ENA=1, ADCR_ENA=1/ q
	wm8994_write(0x05,  0x0303);   
	wm8994_write(0x2D,  0x0100);
	wm8994_write(0x2E,  0x0100);
	
	wm8994_write(0x4C,  0x9F25);
	msleep(5);
	wm8994_write(0x01,  0x0303);
	msleep(50);
	wm8994_write(0x60,  0x0022);
	wm8994_write(0x60,  0x00FF);
	
	wm8994_write(0x208, 0x000A);
	wm8994_write(0x420, 0x0000);
	wm8994_write(0x601, 0x0001);
	wm8994_write(0x602, 0x0001);
    
	wm8994_write(0x610, 0x01A0);  //DAC1 Left Volume bit0~7  		
	wm8994_write(0x611, 0x01A0);  //DAC1 Right Volume bit0~7	
	wm8994_write(0x03,  0x3030);
	wm8994_write(0x22,  0x0000);
	wm8994_write(0x23,  0x0100);
	wm8994_write(0x36,  0x0003);
	wm8994_write(0x1C,  0x017F);  //HPOUT1L Volume
	wm8994_write(0x1D,  0x017F);  //HPOUT1R Volume

#ifdef  CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x303, 0x0040); // AIF1 BCLK DIV--------AIF1CLK/4
	wm8994_write(0x304, 0x0040); // AIF1 ADCLRCK DIV-----BCLK/64
	wm8994_write(0x305, 0x0040); // AIF1 DACLRCK DIV-----BCLK/64
	wm8994_write(0x302, 0x4000); // AIF1_MSTR=1
#endif
}

void AP_to_speakers(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_AP_to_speakers)return;
	wm8994_current_mode=wm8994_AP_to_speakers;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01,  0x0003);
	msleep(WM8994_DELAY);

	wm8994_write(0x200, 0x0001);
	wm8994_write(0x220, 0x0000);
	wm8994_write(0x221, 0x0700);
	wm8994_write(0x222, 0x3126);
	wm8994_write(0x223, 0x0100);

	wm8994_write(0x210, 0x0083); // SR=48KHz
	wm8994_write(0x220, 0x0004);  
	msleep(WM8994_DELAY);
	wm8994_write(0x220, 0x0005);
	wm8994_write(0x200, 0x0011);  // sysclk = fll (bit4 =1)   0x0011
	wm8994_write(0x300, 0xC010);  // i2s 16 bits
  
	wm8994_write(0x01,  0x3003); 
	wm8994_write(0x04,  0x0303); // AIF1ADC1L_ENA=1, AIF1ADC1R_ENA=1, ADCL_ENA=1, ADCR_ENA=1
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
	wm8994_write(0x03,  0x0330);
	wm8994_write(0x22,  0x0000);
	wm8994_write(0x23,  0x0100);
	wm8994_write(0x36,  0x0003);
	wm8994_write(0x26,  0x017F);  //Speaker Left Output Volume
	wm8994_write(0x27,  0x017F);  //Speaker Right Output Volume

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x303, 0x0040); // AIF1 BCLK DIV--------AIF1CLK/4
	wm8994_write(0x304, 0x0040); // AIF1 ADCLRCK DIV-----BCLK/64
	wm8994_write(0x305, 0x0040); // AIF1 DACLRCK DIV-----BCLK/64
	wm8994_write(0x302, 0x4000); // AIF1_MSTR=1
#endif
}

void recorder_and_AP_to_headset(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_recorder_and_AP_to_headset)return;
	wm8994_current_mode=wm8994_recorder_and_AP_to_headset;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01,  0x0003);
	msleep(WM8994_DELAY);

//MCLK=12MHz
//48KHz, BCLK=48KHz*64=3.072MHz, Fout=12.288MHz

	wm8994_write(0x200, 0x0001); // AIF1CLK_ENA=1
	wm8994_write(0x220, 0x0000);
	wm8994_write(0x221, 0x0700);
	wm8994_write(0x222, 0x3126);
	wm8994_write(0x223, 0x0100);
	wm8994_write(0x210, 0x0083); // SR=48KHz
	wm8994_write(0x220, 0x0004); 
	msleep(WM8994_DELAY);
	wm8994_write(0x220, 0x0005); // FLL1_FRACN_ENA=1, FLL1_ENA=1
	wm8994_write(0x200, 0x0011); // AIF1CLK_SRC=10, AIF1CLK_ENA=1

	vol=CONFIG_WM8994_RECORDER_VOL;
	if(vol>60)vol=60;
	if(vol<-16)vol=-16;
	if(vol<30){
		wm8994_write(0x1A,  320+(vol+16)*10/15);  //mic vol
	}else{
		wm8994_write(0x2A,  0x0030);
		wm8994_write(0x1A,  320+(vol-14)*10/15);  //mic vol
	}
	vol=CONFIG_WM8994_HEADSET_NORMAL_VOL;
	if(vol>6)vol=6;
	if(vol<-57)vol=-57;
	wm8994_write(0x1C,  320+vol+57);  //-57dB~6dB
	wm8994_write(0x1D,  320+vol+57);  //-57dB~6dB

	wm8994_write(0x28,  0x0003); // IN1RP_TO_IN1R=1, IN1RN_TO_IN1R=1
	wm8994_write(0x200, 0x0011); // AIF1CLK_ENA=1
	wm8994_write(0x208, 0x000A); // DSP_FS1CLK_ENA=1, DSP_FSINTCLK_ENA=1
	wm8994_write(0x300, 0xC050); // AIF1ADCL_SRC=1, AIF1ADCR_SRC=1, AIF1_WL=10, AIF1_FMT=10
	wm8994_write(0x606, 0x0002); // ADC1L_TO_AIF1ADC1L=1
	wm8994_write(0x607, 0x0002); // ADC1R_TO_AIF1ADC1R=1
	wm8994_write(0x620, 0x0000); 
	wm8994_write(0x700, 0xA101); 

	wm8994_write(0x402, 0x01FF); // AIF1ADC1L_VOL [7:0]
	wm8994_write(0x403, 0x01FF); // AIF1ADC1R_VOL [7:0]
	wm8994_write(0x2D,  0x0100); // DAC1L_TO_HPOUT1L=1
	wm8994_write(0x2E,  0x0100); // DAC1R_TO_HPOUT1R=1

	wm8994_write(0x4C,  0x9F25);
	mdelay(5);
	wm8994_write(0x01,  0x0313);
	mdelay(50);
	wm8994_write(0x60,  0x0022);
	wm8994_write(0x60,  0x00EE);

	wm8994_write(0x601, 0x0001); // AIF1DAC1L_TO_DAC1L=1
	wm8994_write(0x602, 0x0001); // AIF1DAC1R_TO_DAC1R=1
	wm8994_write(0x610, 0x01A0); // DAC1_VU=1, DAC1L_VOL=1100_0000
	wm8994_write(0x611, 0x01A0); // DAC1_VU=1, DAC1R_VOL=1100_0000
	wm8994_write(0x02,  0x6110); // TSHUT_ENA=1, TSHUT_OPDIS=1, MIXINR_ENA=1,IN1R_ENA=1
	wm8994_write(0x03,  0x3030);
	wm8994_write(0x04,  0x0303); // AIF1ADC1L_ENA=1, AIF1ADC1R_ENA=1, ADCL_ENA=1, ADCR_ENA=1
	wm8994_write(0x05,  0x0303); // AIF1DAC1L_ENA=1, AIF1DAC1R_ENA=1, DAC1L_ENA=1, DAC1R_ENA=1
	wm8994_write(0x420, 0x0000); 
	wm8994_write(0x700, 0xA101); 
#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x303, 0x0040); // AIF1 BCLK DIV--------AIF1CLK/4
	wm8994_write(0x304, 0x0040); // AIF1 ADCLRCK DIV-----BCLK/64
	wm8994_write(0x305, 0x0040); // AIF1 DACLRCK DIV-----BCLK/64
	wm8994_write(0x302, 0x3000); // AIF1_MSTR=1
	msleep(50);
	wm8994_write(0x302, 0x7000); // AIF1_MSTR=1
	msleep(50);
#endif
}

void recorder_and_AP_to_speakers(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_recorder_and_AP_to_speakers)return;
	wm8994_current_mode=wm8994_recorder_and_AP_to_speakers;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01,  0x0003);
	msleep(WM8994_DELAY);

//MCLK=12MHz
//48KHz, BCLK=48KHz*64=3.072MHz, Fout=12.288MHz

	wm8994_write(0x200, 0x0001); // AIF1CLK_ENA=1
	wm8994_write(0x220, 0x0000);
	wm8994_write(0x221, 0x0700);
	wm8994_write(0x222, 0x3126);
	wm8994_write(0x223, 0x0100);
	wm8994_write(0x210, 0x0083); // SR=48KHz

	wm8994_write(0x220, 0x0004); 
	msleep(WM8994_DELAY);
	wm8994_write(0x220, 0x0005); // FLL1_FRACN_ENA=1, FLL1_ENA=1
	wm8994_write(0x200, 0x0011); // AIF1CLK_SRC=10, AIF1CLK_ENA=1

	wm8994_write(0x02,  0x6110); // TSHUT_ENA=1, TSHUT_OPDIS=1, MIXINR_ENA=1,IN1R_ENA=1
	wm8994_write(0x04,  0x0303); // AIF1ADC1L_ENA=1, AIF1ADC1R_ENA=1, ADCL_ENA=1, ADCR_ENA=1
	wm8994_write(0x28,  0x0003); // IN1RP_TO_IN1R=1, IN1RN_TO_IN1R=1
	vol=CONFIG_WM8994_RECORDER_VOL;
	if(vol>60)vol=60;
	if(vol<-16)vol=-16;
	if(vol<30){
		wm8994_write(0x1A,  320+(vol+16)*10/15);  //mic vol
	}else{
		wm8994_write(0x2A,  0x0030);
		wm8994_write(0x1A,  320+(vol-14)*10/15);  //mic vol
	}

	vol=CONFIG_WM8994_SPEAKER_NORMAL_VOL;
	if(vol>18)vol=18;
	if(vol<-57)vol=-57;
	if(vol<=6){
		wm8994_write(0x26,  320+vol+57);  //-57dB~6dB
		wm8994_write(0x27,  320+vol+57);  //-57dB~6dB
	}else{
		wm8994_write(0x25,  0x003F);  //0~12dB
		wm8994_write(0x26,  320+vol+39);  //-57dB~6dB
		wm8994_write(0x27,  320+vol+39);  //-57dB~6dB
	}

	wm8994_write(0x200, 0x0011); // AIF1CLK_ENA=1
	wm8994_write(0x208, 0x000A); // DSP_FS1CLK_ENA=1, DSP_FSINTCLK_ENA=1
	wm8994_write(0x300, 0xC050); // AIF1ADCL_SRC=1, AIF1ADCR_SRC=1, AIF1_WL=10, AIF1_FMT=10
	wm8994_write(0x606, 0x0002); // ADC1L_TO_AIF1ADC1L=1
	wm8994_write(0x607, 0x0002); // ADC1R_TO_AIF1ADC1R=1
	wm8994_write(0x620, 0x0000); 

	wm8994_write(0x402, 0x01FF); // AIF1ADC1L_VOL [7:0]
	wm8994_write(0x403, 0x01FF); // AIF1ADC1R_VOL [7:0]

	wm8994_write(0x700, 0xA101); 

	wm8994_write(0x01,  0x3013);
	wm8994_write(0x03,  0x0330); // SPKRVOL_ENA=1, SPKLVOL_ENA=1, MIXOUTL_ENA=1, MIXOUTR_ENA=1  
	wm8994_write(0x05,  0x0303); // AIF1DAC1L_ENA=1, AIF1DAC1R_ENA=1, DAC1L_ENA=1, DAC1R_ENA=1
	wm8994_write(0x22,  0x0000);
	wm8994_write(0x23,  0x0100); // SPKOUT_CLASSAB=1

	wm8994_write(0x2D,  0x0001); // DAC1L_TO_MIXOUTL=1
	wm8994_write(0x2E,  0x0001); // DAC1R_TO_MIXOUTR=1
	wm8994_write(0x4C,  0x9F25);
	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x36,  0x000C); // MIXOUTL_TO_SPKMIXL=1, MIXOUTR_TO_SPKMIXR=1
	wm8994_write(0x601, 0x0001); // AIF1DAC1L_TO_DAC1L=1
	wm8994_write(0x602, 0x0001); // AIF1DAC1R_TO_DAC1R=1
	wm8994_write(0x610, 0x01C0); // DAC1_VU=1, DAC1L_VOL=1100_0000
	wm8994_write(0x611, 0x01C0); // DAC1_VU=1, DAC1R_VOL=1100_0000
	wm8994_write(0x420, 0x0000); 
#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x303, 0x0040); // AIF1 BCLK DIV--------AIF1CLK/4
	wm8994_write(0x304, 0x0040); // AIF1 ADCLRCK DIV-----BCLK/64
	wm8994_write(0x305, 0x0040); // AIF1 DACLRCK DIV-----BCLK/64
	wm8994_write(0x302, 0x3000); // AIF1_MSTR=1
	msleep(50);
	wm8994_write(0x302, 0x7000); // AIF1_MSTR=1
	msleep(50);
#endif
}

void FM_to_headset(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_FM_to_headset)return;
	wm8994_current_mode=wm8994_FM_to_headset;
	wm8994_reset();
	msleep(WM8994_DELAY);

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

void FM_to_headset_and_record(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_FM_to_headset_and_record)return;
	wm8994_current_mode=wm8994_FM_to_headset_and_record;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01,   0x0003);
	msleep(WM8994_DELAY);
	wm8994_write(0x221,  0x1900);  //8~13BIT div

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302,  0x4000);  // master = 0x4000 // slave= 0x0000
	wm8994_write(0x303,  0x0040);  // master  0x0050 lrck 7.94kHz bclk 510KHz
#endif
	
	wm8994_write(0x220,  0x0004);
	msleep(WM8994_DELAY);
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

void FM_to_speakers(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_FM_to_speakers)return;
	wm8994_current_mode=wm8994_FM_to_speakers;
	wm8994_reset();
	msleep(WM8994_DELAY);

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

	wm8994_write(0x220,  0x0003);
	wm8994_write(0x221,  0x0700);
	wm8994_write(0x224,  0x0CC0);

	wm8994_write(0x200,  0x0011);
	wm8994_write(0x20,   0x01F9);
	wm8994_write(0x21,   0x01F9);
}

void FM_to_speakers_and_record(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_FM_to_speakers_and_record)return;
	wm8994_current_mode=wm8994_FM_to_speakers_and_record;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01,   0x0003);  
	msleep(WM8994_DELAY);

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302,  0x4000);  // master = 0x4000 // slave= 0x0000
	wm8994_write(0x303,  0x0090);  //
#endif
	
	wm8994_write(0x220,  0x0006);
	msleep(WM8994_DELAY);

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
#ifndef PCM_BB
void handsetMIC_to_baseband_to_headset(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_handsetMIC_to_baseband_to_headset)return;
	wm8994_current_mode=wm8994_handsetMIC_to_baseband_to_headset;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01,  0x0003);
	msleep(50);

	wm8994_write(0x200, 0x0001);
	wm8994_write(0x220, 0x0000);
	wm8994_write(0x221, 0x0700);
	wm8994_write(0x222, 0x3126);
	wm8994_write(0x223, 0x0100);

	wm8994_write(0x210, 0x0083); // SR=48KHz
	wm8994_write(0x220, 0x0004);  
	msleep(WM8994_DELAY);
	wm8994_write(0x220, 0x0005);
	wm8994_write(0x200, 0x0011);  // sysclk = fll (bit4 =1)   0x0011
	wm8994_write(0x300, 0xC010);  // i2s 16 bits

	vol=CONFIG_WM8994_HEADSET_INCALL_MIC_VOL;
	if(vol>30)vol=30;
	if(vol<-22)vol=-22;
	if(vol<-16){
		wm8994_write(0x1E,  0x0016);  //mic vol
		wm8994_write(0x18,  320+(vol+22)*10/15);  //mic vol	
	}else{
		wm8994_write(0x1E,  0x0006);  //mic vol
		wm8994_write(0x18,  320+(vol+16)*10/15);  //mic vol
	}
	vol=CONFIG_WM8994_HEADSET_INCALL_VOL;
	if(vol>0)vol=0;
	if(vol<-21)vol=-21;
	wm8994_write(0x31,  (((-vol)/3)<<3)+(-vol)/3);  //-21dB

	wm8994_write(0x22,  0x0000);
	wm8994_write(0x23,  0x0100);
	wm8994_write(0x28,  0x0030);  //IN1LN_TO_IN1L IN1LP_TO_IN1L
	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);

#ifdef CONFIG_SND_BB_NORMAL_INPUT
	wm8994_write(0x2D,  0x0003);  //bit 1 IN2LP_TO_MIXOUTL bit 12 DAC1L_TO_HPOUT1L  0x0102 
	wm8994_write(0x2E,  0x0003);  //bit 1 IN2RP_TO_MIXOUTR bit 12 DAC1R_TO_HPOUT1R  0x0102
#endif
#ifdef CONFIG_SND_BB_DIFFERENTIAL_INPUT
	wm8994_write(0x02,  0x6240);
	wm8994_write(0x2B,  0x0005);  //VRX_MIXINL_VOL
	wm8994_write(0x2D,  0x0041);  //bit 1 MIXINL_TO_MIXOUTL bit 12 DAC1L_TO_HPOUT1L  0x0102 
	wm8994_write(0x2E,  0x0081);  //bit 1 MIXINL_TO_MIXOUTR bit 12 DAC1R_TO_HPOUT1R  0x0102
#endif

	wm8994_write(0x34,  0x0002);  //IN1L_TO_LINEOUT1P
	wm8994_write(0x36,  0x0003);

	wm8994_write(0x4C,  0x9F25);
	msleep(5);
	wm8994_write(0x01,  0x0323);
	msleep(50);
	wm8994_write(0x60,  0x0022);
	wm8994_write(0x60,  0x00EE);

	wm8994_write(0x02,  0x6040);
	wm8994_write(0x03,  0x3030);
	wm8994_write(0x04,  0x0300); // AIF1ADC1L_ENA=1, AIF1ADC1R_ENA=1
	wm8994_write(0x05,  0x0303);
	wm8994_write(0x208, 0x000A);
	wm8994_write(0x224, 0x0CC0);
	wm8994_write(0x420, 0x0000);
	wm8994_write(0x601, 0x0001);
	wm8994_write(0x602, 0x0001);

	wm8994_write(0x610, 0x01A0);  //DAC1 Left Volume bit0~7  		
	wm8994_write(0x611, 0x01A0);  //DAC1 Right Volume bit0~7
	wm8994_write(0x700, 0xA101);
#ifdef CONFIG_SND_CODEC_SOC_MASTER 
	wm8994_write(0x303, 0x0040); // AIF1 BCLK DIV--------AIF1CLK/4
	wm8994_write(0x304, 0x0040); // AIF1 ADCLRCK DIV-----BCLK/64
	wm8994_write(0x305, 0x0040); // AIF1 DACLRCK DIV-----BCLK/64
	wm8994_write(0x302, 0x3000); // AIF1_MSTR=1
	msleep(50);
	wm8994_write(0x302, 0x7000); // AIF1_MSTR=1
	msleep(50);
#endif
}

void handsetMIC_to_baseband_to_headset_and_record(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_handsetMIC_to_baseband_to_headset_and_record)return;
	wm8994_current_mode=wm8994_handsetMIC_to_baseband_to_headset_and_record;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01,  0x0303|wm8994_mic_VCC); 
	wm8994_write(0x02,  0x62C0); 
	wm8994_write(0x03,  0x3030); 
	wm8994_write(0x04,  0x0303); 
	wm8994_write(0x18,  0x014B);  //volume
	wm8994_write(0x19,  0x014B);  //volume
	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);
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
}

void mainMIC_to_baseband_to_earpiece(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_mainMIC_to_baseband_to_earpiece)return;
	wm8994_current_mode=wm8994_mainMIC_to_baseband_to_earpiece;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01,  0x0003);
	msleep(WM8994_DELAY);

	wm8994_write(0x200, 0x0001);
	wm8994_write(0x220, 0x0000);
	wm8994_write(0x221, 0x0700);
	wm8994_write(0x222, 0x3126);
	wm8994_write(0x223, 0x0100);

	wm8994_write(0x210, 0x0083); // SR=48KHz
	wm8994_write(0x220, 0x0004);
	msleep(WM8994_DELAY);
	wm8994_write(0x220, 0x0005);
	wm8994_write(0x200, 0x0011); // sysclk = fll (bit4 =1)   0x0011
	wm8994_write(0x300, 0x4010); // i2s 16 bits

	wm8994_write(0x01,  0x0833); //HPOUT2_ENA=1, VMID_SEL=01, BIAS_ENA=1
	wm8994_write(0x02,  0x6250); //bit4 IN1R_ENV bit6 IN1L_ENV 
	wm8994_write(0x03,  0x30F0);
	wm8994_write(0x04,  0x0303); // AIF1ADC1L_ENA=1, AIF1ADC1R_ENA=1, ADCL_ENA=1, ADCR_ENA=1
	wm8994_write(0x05,  0x0303);
	wm8994_write(0x1F,  0x0000);
#if defined(CONFIG_SND_INSIDE_EARPIECE)||defined(CONFIG_SND_OUTSIDE_EARPIECE)
	vol=CONFIG_WM8994_EARPIECE_INCALL_VOL;
	if(vol>30)vol=30;
	if(vol<-27)vol=-27;
	if(vol>9){
		wm8994_write(0x2E,  0x0081);  //30dB
		wm8994_write(0x33,  0x0018);  //30dB
		wm8994_write(0x31,  (((30-vol)/3)<<3)+(30-vol)/3);  //-21dB
	}else if(vol>3){
		wm8994_write(0x2E,  0x0081);  //30dB
		wm8994_write(0x33,  0x0018);  //30dB
		wm8994_write(0x31,  (((24-vol)/3)<<3)+(24-vol)/3);  //-21dB
		wm8994_write(0x1F,  0x0010);
	}else if(vol>=0){	
	}else if(vol>-21){
		wm8994_write(0x31,  (((-vol)/3)<<3)+(-vol)/3);  //-21dB
	}else{
		wm8994_write(0x1F,  0x0010);
		wm8994_write(0x31,  (((-vol-6)/3)<<3)+(-vol-6)/3);  //-21dB
	}
#ifdef CONFIG_SND_INSIDE_EARPIECE
	wm8994_write(0x28,  0x0003); //IN1RP_TO_IN1R IN1RN_TO_IN1R
	wm8994_write(0x34,  0x0004); //IN1R_TO_LINEOUT1P
	vol=CONFIG_WM8994_SPEAKER_INCALL_MIC_VOL;
	if(vol>30)vol=30;
	if(vol<-22)vol=-22;
	if(vol<-16){
		wm8994_write(0x1E,  0x0016);
		wm8994_write(0x1A,  320+(vol+22)*10/15);	
	}else{
		wm8994_write(0x1E,  0x0006);
		wm8994_write(0x1A,  320+(vol+16)*10/15);
	}
#endif
#ifdef CONFIG_SND_OUTSIDE_EARPIECE
	wm8994_write(0x28,  0x0030); //IN1LP_TO_IN1L IN1LN_TO_IN1L
	wm8994_write(0x34,  0x0002); //IN1L_TO_LINEOUT1P
	vol=CONFIG_WM8994_HEADSET_INCALL_MIC_VOL;
	if(vol>30)vol=30;
	if(vol<-22)vol=-22;
	if(vol<-16){
		wm8994_write(0x1E,  0x0016);  //mic vol
		wm8994_write(0x18,  320+(vol+22)*10/15);  //mic vol	
	}else{
		wm8994_write(0x1E,  0x0006);  //mic vol
		wm8994_write(0x18,  320+(vol+16)*10/15);  //mic vol
	}
#endif
#endif
#ifdef CONFIG_SND_BB_NORMAL_INPUT
	wm8994_write(0x2D,  0x0003);  //bit 1 IN2LP_TO_MIXOUTL bit 12 DAC1L_TO_HPOUT1L  0x0102 
	wm8994_write(0x2E,  0x0003);  //bit 1 IN2RP_TO_MIXOUTR bit 12 DAC1R_TO_HPOUT1R  0x0102
#endif
#ifdef CONFIG_SND_BB_DIFFERENTIAL_INPUT
	wm8994_write(0x2B,  0x0005);  //VRX_MIXINL_VOL
	wm8994_write(0x2D,  0x0041);  //bit 1 MIXINL_TO_MIXOUTL bit 12 DAC1L_TO_HPOUT1L  0x0102 
	wm8994_write(0x2E,  0x0081);  //bit 1 MIXINL_TO_MIXOUTR bit 12 DAC1R_TO_HPOUT1R  0x0102
#endif
	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);
	wm8994_write(0x33,  0x0010);

	wm8994_write(0x208, 0x000A); //DSP_FS1CLK_ENA=1, DSP_FSINTCLK_ENA=1
	wm8994_write(0x601, 0x0001); //AIF1DAC1L_TO_DAC1L=1
	wm8994_write(0x602, 0x0001); //AIF1DAC1R_TO_DAC1R=1
	wm8994_write(0x610, 0x01C0); //DAC1_VU=1, DAC1L_VOL=1100_0000
	wm8994_write(0x611, 0x01C0); //DAC1_VU=1, DAC1R_VOL=1100_0000

	wm8994_write(0x420, 0x0000);
	wm8994_write(0x700, 0xA101); 
#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x303, 0x0040); // AIF1 BCLK DIV--------AIF1CLK/4
	wm8994_write(0x304, 0x0040); // AIF1 ADCLRCK DIV-----BCLK/64
	wm8994_write(0x305, 0x0040); // AIF1 DACLRCK DIV-----BCLK/64
	wm8994_write(0x302, 0x3000); // AIF1_MSTR=1
	msleep(50);
	wm8994_write(0x302, 0x7000); // AIF1_MSTR=1
	msleep(50);
#endif
}

void mainMIC_to_baseband_to_earpiece_I2S(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_mainMIC_to_baseband_to_earpiece)return;
	wm8994_current_mode=wm8994_mainMIC_to_baseband_to_earpiece;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01,  0x0003);
	msleep(WM8994_DELAY);

	wm8994_write(0x200, 0x0001);
	wm8994_write(0x220, 0x0000);
	wm8994_write(0x221, 0x0700);
	wm8994_write(0x222, 0x3126);
	wm8994_write(0x223, 0x0100);

	wm8994_write(0x210, 0x0083); // SR=48KHz
	wm8994_write(0x220, 0x0004);
	msleep(WM8994_DELAY);
	wm8994_write(0x220, 0x0005);
	wm8994_write(0x200, 0x0011);  // sysclk = fll (bit4 =1)   0x0011

	wm8994_write(0x02,  0x6240);
	wm8994_write(0x04,  0x0303);  // AIF1ADC1L_ENA=1, AIF1ADC1R_ENA=1, ADCL_ENA=1, ADCR_ENA=1
	wm8994_write(0x18,  0x015B);  //IN1_VU=1, IN1L_MUTE=0, IN1L_ZC=1, IN1L_VOL=0_1011
	wm8994_write(0x1E,  0x0006);
	wm8994_write(0x1F,  0x0000);
	wm8994_write(0x28,  0x0030);
	wm8994_write(0x29,  0x0020); //IN1L_TO_MIXINL=1, IN1L_MIXINL_VOL=0, MIXOUTL_MIXINL_VOL=000
	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);

	wm8994_write(0x200, 0x0011);  // sysclk = fll (bit4 =1)   0x0011 //cjq

	wm8994_write(0x208, 0x000A); //DSP_FS1CLK_ENA=1, DSP_FSINTCLK_ENA=1
	wm8994_write(0x300, 0x0050); //AIF1ADCL_SRC=1, AIF1ADCR_SRC=1
	wm8994_write(0x606, 0x0002); //ADC1L_TO_AIF1ADC1L=1
	wm8994_write(0x607, 0x0002); //ADC1R_TO_AIF1ADC1R=1

	wm8994_write(0x620, 0x0000); //cjq
	wm8994_write(0x700, 0xA101); //cjq

	wm8994_write(0x01,  0x0833); //HPOUT2_ENA=1, VMID_SEL=01, BIAS_ENA=1
	wm8994_write(0x03,  0x30F0);
	wm8994_write(0x05,  0x0303);
	wm8994_write(0x2D,  0x0021); //DAC1L_TO_MIXOUTL=1
	wm8994_write(0x2E,  0x0001); //DAC1R_TO_MIXOUTR=1

	wm8994_write(0x4C,  0x9F25); //cjq
	wm8994_write(0x60,  0x00EE); //cjq

	wm8994_write(0x33,  0x0010);
	wm8994_write(0x34,  0x0002);

	wm8994_write(0x601, 0x0001); //AIF1DAC1L_TO_DAC1L=1
	wm8994_write(0x602, 0x0001); //AIF1DAC1R_TO_DAC1R=1
	wm8994_write(0x610, 0x01FF); //DAC1_VU=1, DAC1L_VOL=1100_0000
	wm8994_write(0x611, 0x01FF); //DAC1_VU=1, DAC1R_VOL=1100_0000

	wm8994_write(0x420, 0x0000);
	wm8994_write(0x700, 0xA101); 
#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x303, 0x0040); // AIF1 BCLK DIV--------AIF1CLK/4
	wm8994_write(0x304, 0x0040); // AIF1 ADCLRCK DIV-----BCLK/64
	wm8994_write(0x305, 0x0040); // AIF1 DACLRCK DIV-----BCLK/64
	wm8994_write(0x302, 0x3000); // AIF1_MSTR=1
	msleep(50);
	wm8994_write(0x302, 0x7000); // AIF1_MSTR=1
	msleep(50);
#endif
}

void mainMIC_to_baseband_to_earpiece_and_record(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_mainMIC_to_baseband_to_earpiece_and_record)return;
	wm8994_current_mode=wm8994_mainMIC_to_baseband_to_earpiece_and_record;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01  ,0x0803|wm8994_mic_VCC);
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
	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302, 0x4000);  // master = 0x4000 // slave= 0x0000
	wm8994_write(0x303, 0x0090);  // master lrck 16k
#endif

	wm8994_write(0x606 ,0x0002);
	wm8994_write(0x607 ,0x0002);
	wm8994_write(0x620 ,0x0000);
}

void mainMIC_to_baseband_to_speakers(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_mainMIC_to_baseband_to_speakers)return;
	wm8994_current_mode=wm8994_mainMIC_to_baseband_to_speakers;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01,  0x0003);
	msleep(WM8994_DELAY);

	wm8994_write(0x200, 0x0001);
	wm8994_write(0x220, 0x0000);
	wm8994_write(0x221, 0x0700);
	wm8994_write(0x222, 0x3126);
	wm8994_write(0x223, 0x0100);

	wm8994_write(0x210, 0x0083); // SR=48KHz
	msleep(WM8994_DELAY);
	wm8994_write(0x220, 0x0005);
	wm8994_write(0x200, 0x0011);  // sysclk = fll (bit4 =1)   0x0011
  	wm8994_write(0x300, 0xC010);  // i2s 16 bits
	
	wm8994_write(0x01,  0x3013); 
	wm8994_write(0x02,  0x6210);
	wm8994_write(0x03,  0x33F0);
	wm8994_write(0x04,  0x0303); // AIF1ADC1L_ENA=1, AIF1ADC1R_ENA=1, ADCL_ENA=1, ADCR_ENA=1
	wm8994_write(0x05,  0x0303);
	wm8994_write(0x22,  0x0000);
	wm8994_write(0x23,  0x0100);
	vol=CONFIG_WM8994_SPEAKER_INCALL_MIC_VOL;
	if(vol>30)vol=30;
	if(vol<-22)vol=-22;
	if(vol<-16){
		wm8994_write(0x1E,  0x0016);
		wm8994_write(0x1A,  320+(vol+22)*10/15);	
	}else{
		wm8994_write(0x1E,  0x0006);
		wm8994_write(0x1A,  320+(vol+16)*10/15);
	}
	vol=CONFIG_WM8994_SPEAKER_INCALL_VOL;
	if(vol>12)vol=12;
	if(vol<-21)vol=-21;
	if(vol<0){
		wm8994_write(0x31,  (((-vol)/3)<<3)+(-vol)/3);
	}else{
		wm8994_write(0x25,  ((vol*10/15)<<3)+vol*10/15);
	}
	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);
	wm8994_write(0x28,  0x0003);  //IN1RP_TO_IN1R  IN1RN_TO_IN1R
#ifdef CONFIG_SND_BB_NORMAL_INPUT
	wm8994_write(0x2D,  0x0003);  //bit 1 IN2LP_TO_MIXOUTL bit 12 DAC1L_TO_HPOUT1L  0x0102 
	wm8994_write(0x2E,  0x0003);  //bit 1 IN2RP_TO_MIXOUTR bit 12 DAC1R_TO_HPOUT1R  0x0102
#endif
#ifdef CONFIG_SND_BB_DIFFERENTIAL_INPUT
	wm8994_write(0x2B,  0x0005);  //VRX_MIXINL_VOL
	wm8994_write(0x2D,  0x0041);  //bit 1 MIXINL_TO_MIXOUTL bit 12 DAC1L_TO_HPOUT1L  0x0102 
	wm8994_write(0x2E,  0x0081);  //bit 1 MIXINL_TO_MIXOUTR bit 12 DAC1R_TO_HPOUT1R  0x0102
#endif
	wm8994_write(0x4C,  0x9F25);
	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x34,  0x0004);
	wm8994_write(0x36,  0x000C);  //MIXOUTL_TO_SPKMIXL  MIXOUTR_TO_SPKMIXR

	wm8994_write(0x208, 0x000A);
	wm8994_write(0x420, 0x0000); 
	
	wm8994_write(0x601, 0x0001);
	wm8994_write(0x602, 0x0001);
    
	wm8994_write(0x610, 0x01c0);  //DAC1 Left Volume bit0~7	
	wm8994_write(0x611, 0x01c0);  //DAC1 Right Volume bit0~7
	wm8994_write(0x700, 0xA101); 
#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x303, 0x0040); // AIF1 BCLK DIV--------AIF1CLK/4
	wm8994_write(0x304, 0x0040); // AIF1 ADCLRCK DIV-----BCLK/64
	wm8994_write(0x305, 0x0040); // AIF1 DACLRCK DIV-----BCLK/64
	wm8994_write(0x302, 0x3000); // AIF1_MSTR=1
	msleep(50);
	wm8994_write(0x302, 0x7000); // AIF1_MSTR=1
	msleep(50);
#endif
}

void mainMIC_to_baseband_to_speakers_and_record(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_mainMIC_to_baseband_to_speakers_and_record)return;
	wm8994_current_mode=wm8994_mainMIC_to_baseband_to_speakers_and_record;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01, 0x3003|wm8994_mic_VCC);
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
	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302, 0x4000);  // master = 0x4000 // slave= 0x0000
	wm8994_write(0x303, 0x0090);  // master lrck 16k
#endif

 	wm8994_write(0x606, 0x0002);
 	wm8994_write(0x607, 0x0002);
 	wm8994_write(0x620, 0x0000);
}

void BT_baseband(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_BT_baseband)return;
	wm8994_current_mode=wm8994_BT_baseband;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01, 0x0003);
	wm8994_write(0x02, 0x63A0);
	wm8994_write(0x03, 0x30A0);
	wm8994_write(0x04, 0x3303);
	wm8994_write(0x05, 0x3002);
	wm8994_write(0x06, 0x000A);
	wm8994_write(0x1E, 0x0006);
#ifdef CONFIG_SND_BB_NORMAL_INPUT
	wm8994_write(0x28, 0x00C0);
#endif
#ifdef CONFIG_SND_BB_DIFFERENTIAL_INPUT
	wm8994_write(0x28, 0x00CC);
#endif
	wm8994_write(0x29, 0x0100);
	vol=CONFIG_WM8994_BT_INCALL_MIC_VOL;
	if(vol>6)vol=6;
	if(vol<-57)vol=-57;
	wm8994_write(0x20,  320+vol+57);

	vol=CONFIG_WM8994_BT_INCALL_VOL;
	if(vol>30)vol=30;
	if(vol<0)vol=0;
	if(vol>30)wm8994_write(0x29, 0x0130);
	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);
	wm8994_write(0x2A, 0x0100);
	wm8994_write(0x2D, 0x0001);
	wm8994_write(0x34, 0x0001);
	wm8994_write(0x200, 0x0001);

	//roger_chen@20100524
	//8KHz, BCLK=8KHz*128=1024KHz, Fout=2.048MHz
	wm8994_write(0x204, 0x0001);    // SMbus_16inx_16dat     Write  0x34      * AIF2 Clocking (1)(204H): 0011  AIF2CLK_SRC=00, AIF2CLK_INV=0, AIF2CLK_DIV=0, AIF2CLK_ENA=1
	wm8994_write(0x208, 0x000F);
	wm8994_write(0x220, 0x0000);    // SMbus_16inx_16dat     Write  0x34      * FLL1 Control (1)(220H):  0005  FLL1_FRACN_ENA=0, FLL1_OSC_ENA=0, FLL1_ENA=0
	wm8994_write(0x221, 0x2F00);    // SMbus_16inx_16dat     Write  0x34      * FLL1 Control (2)(221H):  0700  FLL1_OUTDIV=2Fh, FLL1_CTRL_RATE=000, FLL1_FRATIO=000
	wm8994_write(0x222, 0x3126);    // SMbus_16inx_16dat     Write  0x34      * FLL1 Control (3)(222H):  8FD5  FLL1_K=3126h
	wm8994_write(0x223, 0x0100);    // SMbus_16inx_16dat     Write  0x34      * FLL1 Control (4)(223H):  00E0  FLL1_N=8h, FLL1_GAIN=0000
	wm8994_write(0x303, 0x0090);
	wm8994_write(0x310, 0xC118);  //DSP/PCM; 16bits; ADC L channel = R channel;MODE A
	wm8994_write(0x313, 0x0020);    // SMbus_16inx_16dat     Write  0x34      * AIF2 BCLK DIV--------AIF1CLK/2
	wm8994_write(0x314, 0x0080);    // SMbus_16inx_16dat     Write  0x34      * AIF2 ADCLRCK DIV-----BCLK/128
	wm8994_write(0x315, 0x0080);    // SMbus_16inx_16dat     Write  0x34      * AIF2 DACLRCK DIV-----BCLK/128
	wm8994_write(0x210, 0x0003);    // SMbus_16inx_16dat     Write  0x34      * SR=8KHz
	wm8994_write(0x220, 0x0004);    // SMbus_16inx_16dat     Write  0x34      * FLL1 Control (1)(220H):  0005  FLL1_FRACN_ENA=1, FLL1_OSC_ENA=0, FLL1_ENA=0
	msleep(50);
	wm8994_write(0x220, 0x0005);    // SMbus_16inx_16dat     Write  0x34      * FLL1 Control (1)(220H):  0005  FLL1_FRACN_ENA=1, FLL1_OSC_ENA=0, FLL1_ENA=1
	wm8994_write(0x200, 0x0011);
	wm8994_write(0x204, 0x0011);    // SMbus_16inx_16dat     Write  0x34      * AIF2 Clocking (1)(204H): 0011  AIF2CLK_SRC=10, AIF2CLK_INV=0, AIF2CLK_DIV=0, AIF2CLK_ENA=1

	wm8994_write(0x520, 0x0000);

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

	//roger_chen@20100519
	//enable AIF2 BCLK,LRCK
	//Rev.B and Rev.D is different
	wm8994_write(0x700, 0xA101);
	wm8994_write(0x704, 0xA100);
	wm8994_write(0x707, 0xA100);
	wm8994_write(0x708, 0x2100);
	wm8994_write(0x709, 0x2100);
	wm8994_write(0x70A, 0x2100);
	msleep(10);
	wm8994_write(0x702, 0xA100);
	wm8994_write(0x703, 0xA100); 

	wm8994_write(0x603, 0x018C);
	wm8994_write(0x605, 0x0020);

#ifdef CONFIG_SND_CODEC_SOC_MASTER
	wm8994_write(0x302, 0x3000); // AIF1_MSTR=1
	msleep(10);
	wm8994_write(0x302, 0x7000); // AIF1_MSTR=1
	msleep(10);
	wm8994_write(0x312, 0x3000); // AIF1_MSTR=1
	msleep(10);
	wm8994_write(0x312, 0x7000); // AIF1_MSTR=1
	msleep(10);
#endif
}

void BT_baseband_and_record(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_BT_baseband_and_record)return;
	wm8994_current_mode=wm8994_BT_baseband_and_record;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01, 0x0003);
	wm8994_write(0x02, 0x63A0);
	wm8994_write(0x03, 0x30A0);
	wm8994_write(0x04, 0x3303);
	wm8994_write(0x05, 0x3002);
	wm8994_write(0x06, 0x000A);
	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);
	wm8994_write(0x1E, 0x0006);
	wm8994_write(0x28, 0x00CC);
	wm8994_write(0x29, 0x0100);
	wm8994_write(0x2A, 0x0100);
	wm8994_write(0x2D, 0x0001);
	wm8994_write(0x34, 0x0001);
	wm8994_write(0x200, 0x0001);

	//roger_chen@20100524
	//8KHz, BCLK=8KHz*128=1024KHz, Fout=2.048MHz
	wm8994_write(0x204, 0x0001);    // SMbus_16inx_16dat     Write  0x34      * AIF2 Clocking (1)(204H): 0011  AIF2CLK_SRC=00, AIF2CLK_INV=0, AIF2CLK_DIV=0, AIF2CLK_ENA=1
	wm8994_write(0x208, 0x000F);
	wm8994_write(0x220, 0x0000);    // SMbus_16inx_16dat     Write  0x34      * FLL1 Control (1)(220H):  0005  FLL1_FRACN_ENA=0, FLL1_OSC_ENA=0, FLL1_ENA=0
	wm8994_write(0x221, 0x2F00);    // SMbus_16inx_16dat     Write  0x34      * FLL1 Control (2)(221H):  0700  FLL1_OUTDIV=2Fh, FLL1_CTRL_RATE=000, FLL1_FRATIO=000
	wm8994_write(0x222, 0x3126);    // SMbus_16inx_16dat     Write  0x34      * FLL1 Control (3)(222H):  8FD5  FLL1_K=3126h
	wm8994_write(0x223, 0x0100);    // SMbus_16inx_16dat     Write  0x34      * FLL1 Control (4)(223H):  00E0  FLL1_N=8h, FLL1_GAIN=0000
	wm8994_write(0x302, 0x4000);
	wm8994_write(0x303, 0x0090);    
	wm8994_write(0x310, 0xC118);  //DSP/PCM; 16bits; ADC L channel = R channel;MODE A
	wm8994_write(0x312, 0x4000);    // SMbus_16inx_16dat     Write  0x34      * AIF2 Master/Slave(312H): 7000  AIF2_TRI=0, AIF2_MSTR=1, AIF2_CLK_FRC=0, AIF2_LRCLK_FRC=0
	wm8994_write(0x313, 0x0020);    // SMbus_16inx_16dat     Write  0x34      * AIF2 BCLK DIV--------AIF1CLK/2
	wm8994_write(0x314, 0x0080);    // SMbus_16inx_16dat     Write  0x34      * AIF2 ADCLRCK DIV-----BCLK/128
	wm8994_write(0x315, 0x0080);    // SMbus_16inx_16dat     Write  0x34      * AIF2 DACLRCK DIV-----BCLK/128
	wm8994_write(0x210, 0x0003);    // SMbus_16inx_16dat     Write  0x34      * SR=8KHz
	wm8994_write(0x220, 0x0004);    // SMbus_16inx_16dat     Write  0x34      * FLL1 Control (1)(220H):  0005  FLL1_FRACN_ENA=1, FLL1_OSC_ENA=0, FLL1_ENA=0
	msleep(WM8994_DELAY);
	wm8994_write(0x220, 0x0005);    // SMbus_16inx_16dat     Write  0x34      * FLL1 Control (1)(220H):  0005  FLL1_FRACN_ENA=1, FLL1_OSC_ENA=0, FLL1_ENA=1
	wm8994_write(0x204, 0x0011);    // SMbus_16inx_16dat     Write  0x34      * AIF2 Clocking (1)(204H): 0011  AIF2CLK_SRC=10, AIF2CLK_INV=0, AIF2CLK_DIV=0, AIF2CLK_ENA=1

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

	//roger_chen@20100519
	//enable AIF2 BCLK,LRCK
	//Rev.B and Rev.D is different
	wm8994_write(0x702, 0xA100);    
	wm8994_write(0x703, 0xA100);

	wm8994_write(0x704, 0xA100);
	wm8994_write(0x707, 0xA100);
	wm8994_write(0x708, 0x2100);
	wm8994_write(0x709, 0x2100);
	wm8994_write(0x70A, 0x2100);
}

#else //PCM_BB

/******************PCM BB BEGIN*****************/

void handsetMIC_to_baseband_to_headset(void) //pcmbaseband
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_handsetMIC_to_baseband_to_headset)return;
	wm8994_current_mode=wm8994_handsetMIC_to_baseband_to_headset;
	wm8994_reset();
	msleep(50);
	
	wm8994_write(0x01,  0x0003|wm8994_mic_VCC);  
	msleep(50);
	wm8994_write(0x221, 0x0700);  
	wm8994_write(0x222, 0x3127);	
	wm8994_write(0x223, 0x0100);	
	wm8994_write(0x220, 0x0004);
	msleep(50);
	wm8994_write(0x220, 0x0005);  

	wm8994_write(0x01,  0x0303|wm8994_mic_VCC);  ///0x0303);	 // sysclk = fll (bit4 =1)   0x0011 
	wm8994_write(0x02,  0x0240);
	wm8994_write(0x03,  0x0030);
	wm8994_write(0x04,  0x3003);
	wm8994_write(0x05,  0x3003);  // i2s 16 bits
	wm8994_write(0x18,  0x010B);
	wm8994_write(0x28,  0x0030);
	wm8994_write(0x29,  0x0020);
	wm8994_write(0x2D,  0x0100);  //0x0100);DAC1L_TO_HPOUT1L    ;;;bit 8 
	wm8994_write(0x2E,  0x0100);  //0x0100);DAC1R_TO_HPOUT1R    ;;;bit 8 
	wm8994_write(0x4C,  0x9F25);
	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x200, 0x0001);	
	wm8994_write(0x204, 0x0001);
	wm8994_write(0x208, 0x0007);	
	wm8994_write(0x520, 0x0000);	
	wm8994_write(0x601, 0x0004);  //AIF2DACL_TO_DAC1L
	wm8994_write(0x602, 0x0004);  //AIF2DACR_TO_DAC1R

	wm8994_write(0x610, 0x01C0);  //DAC1 Left Volume bit0~7
	wm8994_write(0x611, 0x01C0);  //DAC1 Right Volume bit0~7
	wm8994_write(0x612, 0x01C0);  //DAC2 Left Volume bit0~7	
	wm8994_write(0x613, 0x01C0);  //DAC2 Right Volume bit0~7

	wm8994_write(0x702, 0xC100);
	wm8994_write(0x703, 0xC100);
	wm8994_write(0x704, 0xC100);
	wm8994_write(0x706, 0x4100);
	wm8994_write(0x204, 0x0011);
	wm8994_write(0x211, 0x0009);
	#ifdef TD688_MODE
	wm8994_write(0x310, 0x4108); ///0x4118);  ///interface dsp mode 16bit
	#endif
	#ifdef CHONGY_MODE
	wm8994_write(0x310, 0x4118); ///0x4118);  ///interface dsp mode 16bit
	#endif	
	#ifdef MU301_MODE
	wm8994_write(0x310, 0x4118); ///0x4118);  ///interface dsp mode 16bit
	wm8994_write(0x241, 0x2f04);
	wm8994_write(0x242, 0x0000);
	wm8994_write(0x243, 0x0300);
	wm8994_write(0x240, 0x0004);
	msleep(40);
	wm8994_write(0x240, 0x0005);
	wm8994_write(0x204, 0x0019); 
	wm8994_write(0x211, 0x0003);
	wm8994_write(0x244, 0x0c83);
	wm8994_write(0x620, 0x0000);
	#endif
	#ifdef THINKWILL_M800_MODE
	wm8994_write(0x310, 0x4118); ///0x4118);  ///interface dsp mode 16bit
	#endif
	wm8994_write(0x313, 0x00F0);
	wm8994_write(0x314, 0x0020);
	wm8994_write(0x315, 0x0020);
	wm8994_write(0x603, 0x018c);  ///0x000C);  //Rev.D ADCL SideTone
	wm8994_write(0x604, 0x0010); //XX
	wm8994_write(0x605, 0x0010); //XX
	wm8994_write(0x621, 0x0000);  //0x0001);   ///0x0000);
	wm8994_write(0x317, 0x0003);
	wm8994_write(0x312, 0x0000); /// as slave  ///0x4000);  //AIF2 SET AS MASTER
	
	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);
}

void handsetMIC_to_baseband_to_headset_and_record(void) //pcmbaseband
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_handsetMIC_to_baseband_to_headset_and_record)return;
	wm8994_current_mode=wm8994_handsetMIC_to_baseband_to_headset_and_record;
	wm8994_reset();
	msleep(50);

	wm8994_write(0x01,  0x0003|wm8994_mic_VCC);  
	msleep(50);
	wm8994_write(0x221, 0x0700);  //MCLK=12MHz
	wm8994_write(0x222, 0x3127);	
	wm8994_write(0x223, 0x0100);	
	wm8994_write(0x220, 0x0004);
	msleep(50);
	wm8994_write(0x220, 0x0005);  

	wm8994_write(0x01,  0x0303|wm8994_mic_VCC);	 
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

	wm8994_write(0x610, 0x01C0);  //DAC1 Left Volume bit0~7
	wm8994_write(0x611, 0x01C0);  //DAC1 Right Volume bit0~7
	wm8994_write(0x612, 0x01C0);  //DAC2 Left Volume bit0~7	
	wm8994_write(0x613, 0x01C0);  //DAC2 Right Volume bit0~7

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

	wm8994_write(0x603, 0x018c);  ///0x000C);  //Rev.D ADCL SideTone
	wm8994_write(0x604, 0x0010);
	wm8994_write(0x605, 0x0010);
	wm8994_write(0x621, 0x0000);
	//wm8994_write(0x317, 0x0003);
	//wm8994_write(0x312, 0x4000);  //AIF2 SET AS MASTER
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
	wm8994_write(0x302,  0x4000);
	wm8994_write(0x303,  0x00F0);
	wm8994_write(0x304,  0x0020);
	wm8994_write(0x305,  0x0020);

////AIF1 DAC1 HP
	wm8994_write(0x05,   0x3303);
	wm8994_write(0x420,  0x0000);
	wm8994_write(0x601,  0x0001);
	wm8994_write(0x602,  0x0001);
	wm8994_write(0x700,  0x8140);//SYNC issue, AIF1 ADCLRC1 from FLL after AIF1 MASTER!!!
	
	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);
}

void mainMIC_to_baseband_to_earpiece(void) //pcmbaseband
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_mainMIC_to_baseband_to_earpiece)return;
	wm8994_current_mode=wm8994_mainMIC_to_baseband_to_earpiece;
	wm8994_reset();
	msleep(50);

	wm8994_write(0x01,  0x0003|wm8994_mic_VCC);  
	msleep(50);
	wm8994_write(0x221, 0x0700);  //MCLK=12MHz
	wm8994_write(0x222, 0x3127);	
	wm8994_write(0x223, 0x0100);	
	wm8994_write(0x220, 0x0004);
	msleep(50);
	wm8994_write(0x220, 0x0005);  

	wm8994_write(0x01,  0x0803|wm8994_mic_VCC);   ///0x0813);	 
	wm8994_write(0x02,  0x0240);   ///0x0110);
	wm8994_write(0x03,  0x00F0);
	wm8994_write(0x04,  0x3003);
	wm8994_write(0x05,  0x3003); 
	wm8994_write(0x18,  0x011F); 
	//wm8994_write(0x1A,  0x010B); 
	wm8994_write(0x1F,  0x0000); 
	wm8994_write(0x28,  0x0030);  ///0x0003);
	wm8994_write(0x29,  0x0020);
	//wm8994_write(0x2A,  0x0020);
	wm8994_write(0x2D,  0x0001);
	wm8994_write(0x2E,  0x0001);
	wm8994_write(0x33,  0x0018);
	//wm8994_write(0x4C,  0x9F25);
	//wm8994_write(0x60,  0x00EE);
	wm8994_write(0x200, 0x0001);
	wm8994_write(0x204, 0x0001);
	wm8994_write(0x208, 0x0007);
	wm8994_write(0x520, 0x0000);
	wm8994_write(0x601, 0x0004);
	wm8994_write(0x602, 0x0004);

	wm8994_write(0x610, 0x01C0);  //DAC1 Left Volume bit0~7
	wm8994_write(0x611, 0x01C0);  //DAC1 Right Volume bit0~7
	wm8994_write(0x612, 0x01C0);  //DAC2 Left Volume bit0~7	
	wm8994_write(0x613, 0x01C0);  //DAC2 Right Volume bit0~7

	//wm8994_write(0x700, 0x8141);
	wm8994_write(0x702, 0xC100);
	wm8994_write(0x703, 0xC100);
	wm8994_write(0x704, 0xC100);
	wm8994_write(0x706, 0x4100);
	wm8994_write(0x204, 0x0011);  //AIF2 MCLK=FLL1
	wm8994_write(0x211, 0x0009);  //LRCK=8KHz, Rate=MCLK/1536
	///wm8994_write(0x310, 0x4108); /// 0x4118);  ///0x4118);  //DSP/PCM 16bits
	#ifdef TD688_MODE
	wm8994_write(0x310, 0x4108); ///0x4118);  ///interface dsp mode 16bit
	#endif
	#ifdef CHONGY_MODE
	wm8994_write(0x310, 0x4118); ///0x4118);  ///interface dsp mode 16bit
	#endif
	#ifdef MU301_MODE
	wm8994_write(0x310, 0x4118); ///0x4118);  ///interface dsp mode 16bit
	wm8994_write(0x241, 0x2f04);
	wm8994_write(0x242, 0x0000);
	wm8994_write(0x243, 0x0300);
	wm8994_write(0x240, 0x0004);
	msleep(40);
	wm8994_write(0x240, 0x0005);
	wm8994_write(0x204, 0x0019); 
	wm8994_write(0x211, 0x0003);
	wm8994_write(0x244, 0x0c83);
	wm8994_write(0x620, 0x0000);
	#endif
	#ifdef THINKWILL_M800_MODE
	wm8994_write(0x310, 0x4118); ///0x4118);  ///interface dsp mode 16bit
	#endif
	wm8994_write(0x313, 0x00F0);
	wm8994_write(0x314, 0x0020);
	wm8994_write(0x315, 0x0020);

	wm8994_write(0x603, 0x018C);  //Rev.D ADCL SideTone
	wm8994_write(0x604, 0x0010);
	wm8994_write(0x605, 0x0010);
	wm8994_write(0x621, 0x0000);  ///0x0001);
	wm8994_write(0x317, 0x0003);
	wm8994_write(0x312, 0x0000);  //AIF2 SET AS MASTER
	
	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);
}

void mainMIC_to_baseband_to_earpiece_and_record(void) //pcmbaseband
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_mainMIC_to_baseband_to_earpiece_and_record)return;
	wm8994_current_mode=wm8994_mainMIC_to_baseband_to_earpiece_and_record;
	wm8994_reset();
	msleep(50);

	wm8994_write(0x01,  0x0003|wm8994_mic_VCC);  
	msleep(50);
	wm8994_write(0x221, 0x0700);  //MCLK=12MHz
	wm8994_write(0x222, 0x3127);
	wm8994_write(0x223, 0x0100);
	wm8994_write(0x220, 0x0004);
	msleep(50);
	wm8994_write(0x220, 0x0005);  

	wm8994_write(0x01,  0x0803|wm8994_mic_VCC);
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
	//wm8994_write(0x4C,  0x9F25);
	//wm8994_write(0x60,  0x00EE);
	wm8994_write(0x200, 0x0001);	
	wm8994_write(0x204, 0x0001);
	wm8994_write(0x208, 0x0007);	
	wm8994_write(0x520, 0x0000);	
	wm8994_write(0x601, 0x0004);
	wm8994_write(0x602, 0x0004);

	wm8994_write(0x610, 0x01C0);  //DAC1 Left Volume bit0~7
	wm8994_write(0x611, 0x01C0);  //DAC1 Right Volume bit0~7
	wm8994_write(0x612, 0x01C0);  //DAC2 Left Volume bit0~7	
	wm8994_write(0x613, 0x01C0);  //DAC2 Right Volume bit0~7

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
	//wm8994_write(0x317, 0x0003);
	//wm8994_write(0x312, 0x4000);  //AIF2 SET AS MASTER

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
	wm8994_write(0x302,  0x4000);
	wm8994_write(0x303,  0x00F0);
	wm8994_write(0x304,  0x0020);
	wm8994_write(0x305,  0x0020);

////AIF1 DAC1 HP
	wm8994_write(0x05,   0x3303);
	wm8994_write(0x420,  0x0000);
	wm8994_write(0x601,  0x0001);
	wm8994_write(0x602,  0x0001);
	wm8994_write(0x700,  0x8140);//SYNC issue, AIF1 ADCLRC1 from FLL after AIF1 MASTER!!!
	
	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);
}

void mainMIC_to_baseband_to_speakers(void) //pcmbaseband
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_mainMIC_to_baseband_to_speakers)return;
	wm8994_current_mode=wm8994_mainMIC_to_baseband_to_speakers;
	wm8994_reset();
	msleep(50);

	wm8994_write(0x01,  0x0003|wm8994_mic_VCC);  //0x0013);  
	msleep(50);
	wm8994_write(0x221, 0x0700);  //MCLK=12MHz   //FLL1 CONTRLO(2)
	wm8994_write(0x222, 0x3127);  //FLL1 CONTRLO(3)	
	wm8994_write(0x223, 0x0100);  //FLL1 CONTRLO(4)	
	wm8994_write(0x220, 0x0004);  //FLL1 CONTRLO(1)
	msleep(50);
	wm8994_write(0x220, 0x0005);  //FLL1 CONTRLO(1)

	wm8994_write(0x01,  0x3003|wm8994_mic_VCC);	 
	wm8994_write(0x02,  0x0110);
	wm8994_write(0x03,  0x0030);  ///0x0330);
	wm8994_write(0x04,  0x3003);
	wm8994_write(0x05,  0x3003); 
	wm8994_write(0x1A,  0x011F);
	wm8994_write(0x22,  0x0000);
	wm8994_write(0x23,  0x0100);  ///0x0000);
	wm8994_write(0x25,  0x0152);
	wm8994_write(0x28,  0x0003);
	wm8994_write(0x2A,  0x0020);
	wm8994_write(0x2D,  0x0001);
	wm8994_write(0x2E,  0x0001);
	wm8994_write(0x36,  0x000C);  //MIXOUTL_TO_SPKMIXL  MIXOUTR_TO_SPKMIXR
	//wm8994_write(0x4C,  0x9F25);
	//wm8994_write(0x60,  0x00EE);
	wm8994_write(0x200, 0x0001);  //AIF1 CLOCKING(1)
	wm8994_write(0x204, 0x0001);  //AIF2 CLOCKING(1)
	wm8994_write(0x208, 0x0007);  //CLOCKING(1)
	wm8994_write(0x520, 0x0000);  //AIF2 DAC FILTERS(1)
	wm8994_write(0x601, 0x0004);  //AIF2DACL_DAC1L
	wm8994_write(0x602, 0x0004);  //AIF2DACR_DAC1R

	wm8994_write(0x610, 0x01C0);  //DAC1 Left Volume bit0~7
	wm8994_write(0x611, 0x01C0);  //DAC1 Right Volume bit0~7
	wm8994_write(0x612, 0x01C0);  //DAC2 Left Volume bit0~7	
	wm8994_write(0x613, 0x01C0);  //DAC2 Right Volume bit0~7

	wm8994_write(0x702, 0xC100);  //GPIO3
	wm8994_write(0x703, 0xC100);  //GPIO4
	wm8994_write(0x704, 0xC100);  //GPIO5
	wm8994_write(0x706, 0x4100);  //GPIO7
	wm8994_write(0x204, 0x0011);  //AIF2 MCLK=FLL1
	wm8994_write(0x211, 0x0009);  //LRCK=8KHz, Rate=MCLK/1536
	#ifdef TD688_MODE
	wm8994_write(0x310, 0xc108); ///0x4118);  ///interface dsp mode 16bit
	#endif
	#ifdef CHONGY_MODE
	wm8994_write(0x310, 0xc018); ///0x4118);  ///interface dsp mode 16bit
	#endif
	#ifdef MU301_MODE
	wm8994_write(0x310, 0xc118); ///0x4118);  ///interface dsp mode 16bit
	wm8994_write(0x241, 0x2f04);
	wm8994_write(0x242, 0x0000);
	wm8994_write(0x243, 0x0300);
	wm8994_write(0x240, 0x0004);
	msleep(40);
	wm8994_write(0x240, 0x0005);
	wm8994_write(0x204, 0x0019);
	wm8994_write(0x211, 0x0003);
	wm8994_write(0x244, 0x0c83);
	wm8994_write(0x620, 0x0000);
	#endif
	#ifdef THINKWILL_M800_MODE
	wm8994_write(0x310, 0xc118); ///0x4118);  ///interface dsp mode 16bit
	#endif
	//wm8994_write(0x310, 0xc008);  //0xC018);//  //4118);  //DSP/PCM 16bits
	wm8994_write(0x313, 0x00F0);  //AIF2BCLK
	wm8994_write(0x314, 0x0020);  //AIF2ADCLRCK
	wm8994_write(0x315, 0x0020);  //AIF2DACLRCLK

	wm8994_write(0x603, 0x018C);  //Rev.D ADCL SideTone
	wm8994_write(0x604, 0x0020);  ///0x0010);  //ADC2_TO_DAC2L
	wm8994_write(0x605, 0x0020);  //0x0010);  //ADC2_TO_DAC2R
	wm8994_write(0x621, 0x0000);  ///0x0001);
	wm8994_write(0x317, 0x0003);
	wm8994_write(0x312, 0x0000);  //AIF2 SET AS MASTER

	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);
}

void mainMIC_to_baseband_to_speakers_and_record(void) //pcmbaseband
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_mainMIC_to_baseband_to_speakers_and_record)return;
	wm8994_current_mode=wm8994_mainMIC_to_baseband_to_speakers_and_record;
	wm8994_reset();
	msleep(50);

	wm8994_write(0x01,  0x0003|wm8994_mic_VCC);  
	msleep(50);
	wm8994_write(0x221, 0x0700);  //MCLK=12MHz
	wm8994_write(0x222, 0x3127);	
	wm8994_write(0x223, 0x0100);	
	wm8994_write(0x220, 0x0004);
	msleep(50);
	wm8994_write(0x220, 0x0005);  

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
	//wm8994_write(0x4C,  0x9F25);
	//wm8994_write(0x60,  0x00EE);
	wm8994_write(0x200, 0x0001);	
	wm8994_write(0x204, 0x0001);
	wm8994_write(0x208, 0x0007);	
	wm8994_write(0x520, 0x0000);	
	wm8994_write(0x601, 0x0004);
	wm8994_write(0x602, 0x0004);

	wm8994_write(0x610, 0x01C0);  //DAC1 Left Volume bit0~7
	wm8994_write(0x611, 0x01C0);  //DAC1 Right Volume bit0~7
	wm8994_write(0x612, 0x01C0);  //DAC2 Left Volume bit0~7	
	wm8994_write(0x613, 0x01C0);  //DAC2 Right Volume bit0~7

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
	//wm8994_write(0x317, 0x0003);
	//wm8994_write(0x312, 0x4000);  //AIF2 SET AS MASTER

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
	wm8994_write(0x302,  0x4000);
	wm8994_write(0x303,  0x00F0);
	wm8994_write(0x304,  0x0020);
	wm8994_write(0x305,  0x0020);

////AIF1 DAC1 HP
	wm8994_write(0x05,   0x3303);
	wm8994_write(0x420,  0x0000);
	wm8994_write(0x601,  0x0001);
	wm8994_write(0x602,  0x0001);
	wm8994_write(0x700,  0x8140);//SYNC issue, AIF1 ADCLRC1 from FLL after AIF1 MASTER!!!
	
	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);
}

void BT_baseband(void) //pcmbaseband
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_BT_baseband)return;
	wm8994_current_mode=wm8994_BT_baseband;
	wm8994_reset();
	msleep(50);

	wm8994_write(0x01 ,0x0003);
	msleep (50);

	wm8994_write(0x200 ,0x0001);
	wm8994_write(0x221 ,0x0700);//MCLK=12MHz
	wm8994_write(0x222 ,0x3127);
	wm8994_write(0x223 ,0x0100);
	wm8994_write(0x220 ,0x0004);
	msleep (50);
	wm8994_write(0x220 ,0x0005); 

	wm8994_write(0x02 ,0x0000); 
	wm8994_write(0x200 ,0x0011);// AIF1 MCLK=FLL1
	wm8994_write(0x210 ,0x0009);// LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x300 ,0x4018);// DSP/PCM 16bits

	wm8994_write(0x204 ,0x0011);// AIF2 MCLK=FLL1
	wm8994_write(0x211 ,0x0009);// LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x310 ,0x4118);// DSP/PCM 16bits
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
	wm8994_write(0x04 ,0x3301);//ADCL off
	wm8994_write(0x05 ,0x3301);//DACL off

//	wm8994_write(0x29 ,0x0005);  
	wm8994_write(0x2A ,0x0005);

	wm8994_write(0x313 ,0x00F0);
	wm8994_write(0x314 ,0x0020);
	wm8994_write(0x315 ,0x0020);

//	wm8994_write(0x2D ,0x0001);
	wm8994_write(0x2E ,0x0001);
	wm8994_write(0x420 ,0x0000);
	wm8994_write(0x520 ,0x0000);
	wm8994_write(0x601 ,0x0001);
	wm8994_write(0x602 ,0x0001);
	wm8994_write(0x604 ,0x0001);
	wm8994_write(0x605 ,0x0001);
//	wm8994_write(0x606 ,0x0002);
	wm8994_write(0x607 ,0x0002);
//	wm8994_write(0x610, 0x01C0);  //DAC1 Left Volume bit0~7
	wm8994_write(0x611, 0x01C0);  //DAC1 Right Volume bit0~7
	wm8994_write(0x612, 0x01C0);  //DAC2 Left Volume bit0~7	
	wm8994_write(0x613, 0x01C0);  //DAC2 Right Volume bit0~7


	wm8994_write(0x312 ,0x4000);

	wm8994_write(0x606 ,0x0001);
	wm8994_write(0x607 ,0x0003);//R channel for data mix/CPU record data


////////////HP output test
	wm8994_write(0x01 ,0x0303);
	wm8994_write(0x4C ,0x9F25);
	wm8994_write(0x60 ,0x00EE);
///////////end HP test

	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);
}

void BT_baseband_and_record(void) //pcmbaseband
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_BT_baseband_and_record)return;
	wm8994_current_mode=wm8994_BT_baseband_and_record;
	wm8994_reset();
	msleep(50);

	wm8994_write(0x01  ,0x0003);
	msleep (50);

	wm8994_write(0x200 ,0x0001);
	wm8994_write(0x221 ,0x0700);//MCLK=12MHz
	wm8994_write(0x222 ,0x3127);
	wm8994_write(0x223 ,0x0100);
	wm8994_write(0x220 ,0x0004);
	msleep (50);
	wm8994_write(0x220 ,0x0005); 

	wm8994_write(0x02 ,0x0000); 
	wm8994_write(0x200 ,0x0011);// AIF1 MCLK=FLL1
	wm8994_write(0x210 ,0x0009);// LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x300 ,0x4018);// DSP/PCM 16bits

	wm8994_write(0x204 ,0x0011);// AIF2 MCLK=FLL1
	wm8994_write(0x211 ,0x0009);// LRCK=8KHz, Rate=MCLK/1536
	wm8994_write(0x310 ,0x4118);// DSP/PCM 16bits
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
	wm8994_write(0x04 ,0x3301);//ADCL off
	wm8994_write(0x05 ,0x3301);//DACL off

//   wm8994_write(0x29 ,0x0005);  
	wm8994_write(0x2A ,0x0005);

	wm8994_write(0x313 ,0x00F0);
	wm8994_write(0x314 ,0x0020);
	wm8994_write(0x315 ,0x0020);

//   wm8994_write(0x2D ,0x0001);
	wm8994_write(0x2E ,0x0001);
	wm8994_write(0x420 ,0x0000);
	wm8994_write(0x520 ,0x0000);
//  wm8994_write(0x601 ,0x0001);
	wm8994_write(0x602 ,0x0001);
	wm8994_write(0x604 ,0x0001);
	wm8994_write(0x605 ,0x0001);
//  wm8994_write(0x606 ,0x0002);
	wm8994_write(0x607 ,0x0002);
//	wm8994_write(0x610, 0x01C0);  //DAC1 Left Volume bit0~7
	wm8994_write(0x611, 0x01C0);  //DAC1 Right Volume bit0~7
	wm8994_write(0x612, 0x01C0);  //DAC2 Left Volume bit0~7	
	wm8994_write(0x613, 0x01C0);  //DAC2 Right Volume bit0~7


	wm8994_write(0x312 ,0x4000);

	wm8994_write(0x606 ,0x0001);
	wm8994_write(0x607 ,0x0003);//R channel for data mix/CPU record data


////////////HP output test
	wm8994_write(0x01 ,0x0303);
	wm8994_write(0x4C ,0x9F25); 
	wm8994_write(0x60 ,0x00EE); 
///////////end HP test

	wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);
}
#endif //PCM_BB


typedef void (wm8994_codec_fnc_t) (void);

wm8994_codec_fnc_t *wm8994_codec_sequence[] = {
	AP_to_headset,
	AP_to_speakers,
	recorder_and_AP_to_headset,
	recorder_and_AP_to_speakers,
	FM_to_headset,
	FM_to_headset_and_record,
	FM_to_speakers,
	FM_to_speakers_and_record,
	handsetMIC_to_baseband_to_headset,
	handsetMIC_to_baseband_to_headset_and_record,
	mainMIC_to_baseband_to_earpiece,
	mainMIC_to_baseband_to_earpiece_and_record,
	mainMIC_to_baseband_to_speakers,
	mainMIC_to_baseband_to_speakers_and_record,
	BT_baseband,
	BT_baseband_and_record,
};

/********************set wm8994 volume*****volume=0\1\2\3\4\5\6\7*******************/

void wm8994_codec_set_volume(unsigned char system_type,unsigned char volume)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(system_type == VOICE_CALL||system_type == BLUETOOTH_SCO )
	{
		if(volume<=call_maxvol)
			call_vol=volume;
		else{
			printk("%s----%d::call volume more than max value 7\n",__FUNCTION__,__LINE__);
			call_vol=call_maxvol;
		}
		if(wm8994_current_mode<null&&wm8994_current_mode>=wm8994_handsetMIC_to_baseband_to_headset)
			wm8994_set_volume(wm8994_current_mode,call_vol,call_maxvol);
	}
	else
		printk("%s----%d::system type error!\n",__FUNCTION__,__LINE__);
}

void wm8994_set_volume(unsigned char wm8994_mode,unsigned char volume,unsigned char max_volume)
{
	unsigned short lvol=0,rvol=0;

	if(volume>max_volume)volume=max_volume;
	
	if(wm8994_mode==wm8994_handsetMIC_to_baseband_to_headset_and_record||
	wm8994_mode==wm8994_handsetMIC_to_baseband_to_headset)
	{
		wm8994_read(0x001C, &lvol);
		wm8994_read(0x001D, &rvol);
		//HPOUT1L_VOL bit 0~5 /-57dB to +6dB in 1dB steps
		wm8994_write(0x001C, (lvol&~0x003f)|headset_vol_table[volume]); 
		//HPOUT1R_VOL bit 0~5 /-57dB to +6dB in 1dB steps
		wm8994_write(0x001D, (lvol&~0x003f)|headset_vol_table[volume]); 
	}
	else if(wm8994_mode==wm8994_mainMIC_to_baseband_to_speakers_and_record||
	wm8994_mode==wm8994_mainMIC_to_baseband_to_speakers||
	wm8994_mode==wm8994_mainMIC_to_baseband_with_AP_to_speakers)
	{
		wm8994_read(0x0026, &lvol);
		wm8994_read(0x0027, &rvol);
		//SPKOUTL_VOL bit 0~5 /-57dB to +6dB in 1dB steps
		wm8994_write(0x0026, (lvol&~0x003f)|speakers_vol_table[volume]);
		//SPKOUTR_VOL bit 0~5 /-57dB to +6dB in 1dB steps
		wm8994_write(0x0027, (lvol&~0x003f)|speakers_vol_table[volume]);
	}
	else if(wm8994_mode==wm8994_mainMIC_to_baseband_to_earpiece||
	wm8994_mode==wm8994_mainMIC_to_baseband_to_earpiece_and_record)
	{
		wm8994_read(0x0020, &lvol);
		wm8994_read(0x0021, &rvol);

		//MIXOUTL_VOL bit 0~5 /-57dB to +6dB in 1dB steps
		wm8994_write(0x0020, (lvol&~0x003f)|earpiece_vol_table[volume]);
		//MIXOUTR_VOL bit 0~5 /-57dB to +6dB in 1dB steps
		wm8994_write(0x0021, (lvol&~0x003f)|earpiece_vol_table[volume]);
	}
	else if(wm8994_mode==wm8994_BT_baseband||wm8994_mode==wm8994_BT_baseband_and_record)
	{
		wm8994_read(0x0019, &lvol);
		wm8994_read(0x001b, &rvol);
		//bit 0~4 /-16.5dB to +30dB in 1.5dB steps
		wm8994_write(0x0019, (lvol&~0x000f)|BT_vol_table[volume]);
		//bit 0~4 /-16.5dB to +30dB in 1.5dB steps
		wm8994_write(0x001b, (lvol&~0x000f)|BT_vol_table[volume]);
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
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0;
	return 0;
}

int snd_soc_get_route(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

int snd_soc_put_route(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int route = kcontrol->private_value & 0xff;

	switch(route)
	{
		/* Speaker*/
		case SPEAKER_NORMAL: //AP-> 8994Codec -> Speaker
			recorder_and_AP_to_speakers();
			break;
		case SPEAKER_INCALL: //BB-> 8994Codec -> Speaker
			mainMIC_to_baseband_to_speakers();
			break;		
			
		/* Headset */	
		case HEADSET_NORMAL:	//AP-> 8994Codec -> Headset
			recorder_and_AP_to_headset();
			break;
		case HEADSET_INCALL:	//AP-> 8994Codec -> Headset
			handsetMIC_to_baseband_to_headset();
			break;		    

		/* Earpiece*/			    
		case EARPIECE_INCALL:	//:BB-> 8994Codec -> EARPIECE
#ifdef CONFIG_SND_NO_EARPIECE
			mainMIC_to_baseband_to_speakers();
#else
			mainMIC_to_baseband_to_earpiece();
#endif
			break;

		case EARPIECE_NORMAL:	//:BB-> 8994Codec -> EARPIECE
			if(wm8994_current_mode==wm8994_handsetMIC_to_baseband_to_headset)
				recorder_and_AP_to_headset();
			else if(wm8994_current_mode==wm8994_mainMIC_to_baseband_to_speakers||
				wm8994_current_mode==wm8994_mainMIC_to_baseband_to_earpiece)
				recorder_and_AP_to_speakers();
			else if(wm8994_current_mode==wm8994_recorder_and_AP_to_speakers||
				wm8994_current_mode==wm8994_recorder_and_AP_to_speakers)
				break;
			else{
				recorder_and_AP_to_speakers();
				printk("%s--%d--: wm8994 with null mode\n",__FUNCTION__,__LINE__);
			}	
			break;


		/* BLUETOOTH_SCO*/		    	
		case BLUETOOTH_SCO_INCALL:	//BB-> 8994Codec -> BLUETOOTH_SCO  
			BT_baseband();
			break;

		/* BLUETOOTH_A2DP*/			    
		case BLUETOOTH_A2DP_NORMAL:	//AP-> 8994Codec -> BLUETOOTH_A2DP
			break;
		    
		case MIC_CAPTURE:
			if(wm8994_current_mode==wm8994_AP_to_headset)
				recorder_and_AP_to_headset();
			else if(wm8994_current_mode==wm8994_AP_to_speakers)
				recorder_and_AP_to_speakers();
			else if(wm8994_current_mode==wm8994_recorder_and_AP_to_speakers||
				wm8994_current_mode==wm8994_recorder_and_AP_to_headset)
				break;
			else{
				recorder_and_AP_to_speakers();
				printk("%s--%d--: wm8994 with null mode\n",__FUNCTION__,__LINE__);
			}
			break;

		case EARPIECE_RINGTONE:
			AP_to_speakers();
			break;

		case HEADSET_RINGTONE:
			AP_to_headset();
			break;

		case SPEAKER_RINGTONE:
			AP_to_speakers();
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

SOC_DOUBLE_SWITCH_WM8994CODEC("Speaker incall Switch", SPEAKER_INCALL),	
SOC_DOUBLE_SWITCH_WM8994CODEC("Speaker normal Switch", SPEAKER_NORMAL),

SOC_DOUBLE_SWITCH_WM8994CODEC("Earpiece incall Switch", EARPIECE_INCALL),	
SOC_DOUBLE_SWITCH_WM8994CODEC("Earpiece normal Switch", EARPIECE_NORMAL),

SOC_DOUBLE_SWITCH_WM8994CODEC("Headset incall Switch", HEADSET_INCALL),	
SOC_DOUBLE_SWITCH_WM8994CODEC("Headset normal Switch", HEADSET_NORMAL),

SOC_DOUBLE_SWITCH_WM8994CODEC("Bluetooth incall Switch", BLUETOOTH_SCO_INCALL),	
SOC_DOUBLE_SWITCH_WM8994CODEC("Bluetooth normal Switch", BLUETOOTH_SCO_NORMAL),

SOC_DOUBLE_SWITCH_WM8994CODEC("Bluetooth-A2DP incall Switch", BLUETOOTH_A2DP_INCALL),	
SOC_DOUBLE_SWITCH_WM8994CODEC("Bluetooth-A2DP normal Switch", BLUETOOTH_A2DP_NORMAL),

SOC_DOUBLE_SWITCH_WM8994CODEC("Capture Switch", MIC_CAPTURE),

SOC_DOUBLE_SWITCH_WM8994CODEC("Earpiece ringtone Switch",EARPIECE_RINGTONE),
SOC_DOUBLE_SWITCH_WM8994CODEC("Speaker ringtone Switch",SPEAKER_RINGTONE),
SOC_DOUBLE_SWITCH_WM8994CODEC("Headset ringtone Switch",HEADSET_RINGTONE),
};

/*
 * DAPM Controls
 */

static int wm8994_lrc_control(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	return 0;
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
	
	DBG("%s----%d\n",__FUNCTION__,__LINE__);
		
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
	int coeff;
	
	coeff = get_coeff(wm8994->sysclk, params_rate(params));
	if (coeff < 0) {
		coeff = get_coeff(wm8994->sysclk / 2, params_rate(params));
	}
	if (coeff < 0) {
		dev_err(codec->dev,
			"Unable to configure sample rate %dHz with %dHz MCLK\n",
			params_rate(params), wm8994->sysclk);
		return coeff;
	}
	params_format(params);

	return 0;
}

static int wm8994_mute(struct snd_soc_dai *dai, int mute)
{
	return 0;
}

static int wm8994_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{

	codec->bias_level = level;
	return 0;
}

#define WM8994_RATES SNDRV_PCM_RATE_48000

#define WM8994_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops wm8994_ops = {
	.startup = wm8994_pcm_startup,
	.hw_params = wm8994_pcm_hw_params,
	.set_fmt = wm8994_set_dai_fmt,
	.set_sysclk = wm8994_set_dai_sysclk,
	.digital_mute = wm8994_mute,
	/*add by qiuen for volume*/
	.set_volume = wm8994_codec_set_volume,
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

	wm8994_set_bias_level(codec,SND_SOC_BIAS_OFF);
	wm8994_reset();
	msleep(WM8994_DELAY);
	return 0;
}

static int wm8994_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	wm8994_codec_fnc_t **wm8994_fnc_ptr=wm8994_codec_sequence;
	unsigned char wm8994_resume_mode=wm8994_current_mode;
	wm8994_current_mode=null;

	wm8994_set_bias_level(codec,SND_SOC_BIAS_STANDBY);
	if(wm8994_resume_mode<=wm8994_recorder_and_AP_to_speakers)
	{
		wm8994_fnc_ptr+=wm8994_resume_mode;
		(*wm8994_fnc_ptr)() ;
	}
	else if(wm8994_resume_mode>wm8994_BT_baseband_and_record)
	{
		printk("%s--%d--: Wm8994 resume with null mode\n",__FUNCTION__,__LINE__);
	}
	else
	{
		wm8994_fnc_ptr+=wm8994_resume_mode;
		(*wm8994_fnc_ptr)();
		printk("%s--%d--: Wm8994 resume with error mode\n",__FUNCTION__,__LINE__);
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

	snd_soc_add_controls(codec,wm8994_snd_controls,
				ARRAY_SIZE(wm8994_snd_controls));
	snd_soc_dapm_new_controls(codec,wm8994_dapm_widgets,
				  ARRAY_SIZE(wm8994_dapm_widgets));
	snd_soc_dapm_add_routes(codec,audio_map, ARRAY_SIZE(audio_map));
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

	ret = snd_soc_codec_set_cache_io(codec,7, 9, control);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		goto err;
	}

	ret = 0;//wm8994_reset(); cjq
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		goto err;
	}

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
	msg.read_type = I2C_NORMAL;
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
	msgs[0].read_type = I2C_NO_STOP;

	msgs[1].addr = client->addr;
	msgs[1].buf = (char *)buf;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = 2;
	msgs[1].scl_rate = scl_rate;
	msgs[1].read_type = I2C_NO_STOP;

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
