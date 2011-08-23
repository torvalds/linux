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
#include <linux/wakelock.h>
#include <linux/earlysuspend.h>

#include <mach/iomux.h>
#include <mach/gpio.h>

#include "wm8994.h"
#include <linux/miscdevice.h>
#include <linux/circ_buf.h>
#include <linux/mfd/wm8994/core.h>
#include <linux/mfd/wm8994/registers.h>
#include <linux/mfd/wm8994/pdata.h>
#include <linux/mfd/wm8994/gpio.h>

#define WM8994_PROC
#ifdef WM8994_PROC
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
char debug_write_read = 0;
#endif


/* If digital BB is used,open this define. 
 Define what kind of digital BB is used. */
//#define PCM_BB
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

static struct snd_soc_codec *wm8994_codec;

enum wm8994_codec_mode
{
  wm8994_record_only,
  wm8994_record_add,
  wm8994_AP_to_speakers_and_headset,
  wm8994_AP_to_headset,
  wm8994_AP_to_speakers,
  wm8994_handsetMIC_to_baseband_to_headset,
  wm8994_mainMIC_to_baseband_to_headset,
  wm8994_mainMIC_to_baseband_to_earpiece,
  wm8994_mainMIC_to_baseband_to_speakers,
  wm8994_BT_baseband,
  null
};
/* wm8994_current_mode:save current wm8994 mode */
unsigned char wm8994_current_mode=null;
enum stream_type_wm8994
{
	VOICE_CALL	=0,
	BLUETOOTH_SCO	=6,
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

//5:0 000000 0x3F
unsigned short headset_vol_table[6]	={0x012D,0x0133,0x0136,0x0139,0x013B,0x013D};
unsigned short speakers_vol_table[6]	={0x012D,0x0133,0x0136,0x0139,0x013B,0x013D};
unsigned short earpiece_vol_table[6]	={0x0127,0x012D,0x0130,0x0135,0x0139,0x013D};//normal
unsigned short BT_vol_table[16]		={0x01DB,0x01DC,0x01DD,0x01DE,0x01DF,0x01E0,
										0x01E1,0x01E2,0x01E3,0x01E4,0x01E5,0x01E6,
										0x01E7,0x01E8,0x01E9,0x01EA};


/* codec private data */
struct wm8994_priv {
	struct mutex io_lock;
	struct mutex route_lock;
	int route_status;//Because the time callback cannot use mutex
	int sysclk;
	int mclk;
	int fmt;//master or salve
	int rate;//Sampling rate
	struct snd_soc_codec codec;
	struct snd_kcontrol kcontrol;//The current working path
	char RW_status;				//ERROR = -1, TRUE = 0;
	struct wm8994_pdata *pdata;
	
	struct delayed_work wm8994_delayed_work;
	int work_type;

	unsigned int playback_active:1;
	unsigned int capture_active:1;
	/* call_vol:  save all kinds of system volume value. */
	unsigned char call_vol;
	unsigned char BT_call_vol;	

	struct wake_lock wm8994_on_wake;
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
	msg.read_type = 0;
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
	msgs[0].read_type = 2;

	msgs[1].addr = client->addr;
	msgs[1].buf = (char *)buf;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = 2;
	msgs[1].scl_rate = scl_rate;
	msgs[1].read_type = 2;

	ret = i2c_transfer(adap, msgs, 2);

	return ret;
}

int wm8994_set_status(void)
{
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;	
	int ret = 1;
	
	if(wm8994->route_status != IDLE)
		ret = -BUSY;
		
	return ret;
}
EXPORT_SYMBOL_GPL(wm8994_set_status);

static int wm8994_read(unsigned short reg,unsigned short *value)
{
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;
	
	unsigned short regs=((reg>>8)&0x00FF)|((reg<<8)&0xFF00),values;
	char i = 2;
	mutex_lock(&wm8994->io_lock);
	if(wm8994->RW_status == ERROR) goto out;

	while(i > 0)
	{
		i--;
		if (reg_recv_data(wm8994_codec->control_data,&regs,&values,400000) > 0)
		{
			*value=((values>>8)& 0x00FF)|((values<<8)&0xFF00);
		#ifdef WM8994_PROC	
			if(debug_write_read != 0)
				DBG("%s:0x%04x = 0x%04x",__FUNCTION__,reg,*value);
		#endif
			mutex_unlock(&wm8994->io_lock);
			return 0;
		}
	}
	
	wm8994->RW_status = ERROR;	
	printk("%s---line->%d:Codec read error! reg = 0x%x , value = 0x%x\n",__FUNCTION__,__LINE__,reg,*value);
out:	
	mutex_unlock(&wm8994->io_lock);
	return -EIO;
}
	
static int wm8994_write(unsigned short reg,unsigned short value)
{
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;

	unsigned short regs=((reg>>8)&0x00FF)|((reg<<8)&0xFF00),values=((value>>8)&0x00FF)|((value<<8)&0xFF00);
	char i = 2;
	
	mutex_lock(&wm8994->io_lock);

	if(wm8994->RW_status == ERROR) goto out;

#ifdef WM8994_PROC	
	if(debug_write_read != 0)
		DBG("%s:0x%04x = 0x%04x\n",__FUNCTION__,reg,value);
#endif		
	while(i > 0)
	{
		i--;
		if (reg_send_data(wm8994_codec->control_data,&regs,&values,400000) > 0) 
		{
			mutex_unlock(&wm8994->io_lock);
			return 0;
		}	
	}
	
	wm8994->RW_status = ERROR;
	printk("%s---line->%d:Codec write error! reg = 0x%x , value = 0x%x\n",__FUNCTION__,__LINE__,reg,value);

out:	
	mutex_unlock(&wm8994->io_lock);
	return -EIO;
}

static int wm8994_set_bit(unsigned short reg,unsigned short val)
{
	int ret;
	u16 r;

	ret = wm8994_read(reg, &r);
	if (ret < 0)
		goto out;

	r |= val;

	ret = wm8994_write(reg, r);

out:
	return ret;
}

static void wm8994_set_volume(unsigned char wm8994_mode,unsigned char volume,unsigned char max_volume)
{
	unsigned short lvol=0,rvol=0;
//	DBG("%s::volume = %d \n",__FUNCTION__,volume);

	if(volume>max_volume)
		volume=max_volume;
	
	switch(wm8994_mode)
	{
		case wm8994_handsetMIC_to_baseband_to_headset:
		case wm8994_mainMIC_to_baseband_to_headset:
			wm8994_read(0x001C, &lvol);
			wm8994_read(0x001D, &rvol);
			//HPOUT1L_VOL bit 0~5 /-57dB to +6dB in 1dB steps
			wm8994_write(0x001C, (lvol&~0x003f)|headset_vol_table[volume]); 
			//HPOUT1R_VOL bit 0~5 /-57dB to +6dB in 1dB steps
			wm8994_write(0x001D, (rvol&~0x003f)|headset_vol_table[volume]); 
			break;
		case wm8994_mainMIC_to_baseband_to_speakers:
			wm8994_read(0x0026, &lvol);
			wm8994_read(0x0027, &rvol);
			//SPKOUTL_VOL bit 0~5 /-57dB to +6dB in 1dB steps
			wm8994_write(0x0026, (lvol&~0x003f)|speakers_vol_table[volume]);
			//SPKOUTR_VOL bit 0~5 /-57dB to +6dB in 1dB steps
			wm8994_write(0x0027, (rvol&~0x003f)|speakers_vol_table[volume]);
			break;
		case wm8994_mainMIC_to_baseband_to_earpiece:
			wm8994_read(0x0020, &lvol);
			wm8994_read(0x0021, &rvol);
			//MIXOUTL_VOL bit 0~5 /-57dB to +6dB in 1dB steps
			wm8994_write(0x0020, (lvol&~0x003f)|earpiece_vol_table[volume]);
			//MIXOUTR_VOL bit 0~5 /-57dB to +6dB in 1dB steps
			wm8994_write(0x0021, (rvol&~0x003f)|earpiece_vol_table[volume]);
			break;
		case wm8994_BT_baseband:
			//bit 0~4 /-16.5dB to +30dB in 1.5dB steps
			DBG("BT_vol_table[volume] = 0x%x\n",BT_vol_table[volume]);
			wm8994_write(0x0500, BT_vol_table[volume]);
			wm8994_write(0x0501, 0x0100);		
			break;
		default:
		//	DBG("Set all volume\n");
			wm8994_read(0x001C, &lvol);
			wm8994_read(0x001D, &rvol);
			wm8994_write(0x001C, (lvol&~0x003f)|headset_vol_table[volume]); 
			wm8994_write(0x001D, (rvol&~0x003f)|headset_vol_table[volume]);	
			wm8994_read(0x0026, &lvol);
			wm8994_read(0x0027, &rvol);
			wm8994_write(0x0026, (lvol&~0x003f)|speakers_vol_table[volume]);
			wm8994_write(0x0027, (rvol&~0x003f)|speakers_vol_table[volume]);	
			wm8994_read(0x0020, &lvol);
			wm8994_read(0x0021, &rvol);
			wm8994_write(0x0020, (lvol&~0x003f)|earpiece_vol_table[volume]);
			wm8994_write(0x0021, (rvol&~0x003f)|earpiece_vol_table[volume]);
			break;
	}
}

static void wm8994_set_all_mute(void)
{
	int i;
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;

	if(wm8994->call_vol < 0)
		return;

	for (i = wm8994->call_vol; i >= 0; i--)        
		wm8994_set_volume(null,i,call_maxvol);

}

static void wm8994_set_level_volume(void)
{
	int i;
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;
	
	for (i = 0; i <= wm8994->call_vol; i++)
		wm8994_set_volume(wm8994_current_mode,i,call_maxvol);
	
}

static void PA_ctrl(unsigned char ctrl)
{
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;
	struct wm8994_pdata *pdata = wm8994->pdata;
	
	if(pdata->PA_control_pin > 0)
	{
		if(ctrl == GPIO_HIGH)
		{
			DBG("enable PA_control\n");
			gpio_request(pdata->PA_control_pin, NULL);		//AUDIO_PA_ON	 
			gpio_direction_output(pdata->PA_control_pin,GPIO_HIGH); 		
			gpio_free(pdata->PA_control_pin);
		}
		else
		{
			DBG("disable PA_control\n");
			gpio_request(pdata->PA_control_pin, NULL);		//AUDIO_PA_ON	 
			gpio_direction_output(pdata->PA_control_pin,GPIO_LOW); 		
			gpio_free(pdata->PA_control_pin);			
		}
	}
}

/* The size in bits of the FLL divide multiplied by 10
 * to allow rounding later */
#define FIXED_FLL_SIZE ((1 << 16) * 10)

struct fll_div {
	u16 outdiv;
	u16 n;
	u16 k;
	u16 clk_ref_div;
	u16 fll_fratio;
};

static int wm8994_get_fll_config(struct fll_div *fll,
				 int freq_in, int freq_out)
{
	u64 Kpart;
	unsigned int K, Ndiv, Nmod;

//	DBG("FLL input=%dHz, output=%dHz\n", freq_in, freq_out);

	/* Scale the input frequency down to <= 13.5MHz */
	fll->clk_ref_div = 0;
	while (freq_in > 13500000) {
		fll->clk_ref_div++;
		freq_in /= 2;

		if (fll->clk_ref_div > 3)
			return -EINVAL;
	}
//	DBG("CLK_REF_DIV=%d, Fref=%dHz\n", fll->clk_ref_div, freq_in);//0 12m

	/* Scale the output to give 90MHz<=Fvco<=100MHz */
	fll->outdiv = 3;
	while (freq_out * (fll->outdiv + 1) < 90000000) {
		fll->outdiv++;
		if (fll->outdiv > 63)
			return -EINVAL;
	}
	freq_out *= fll->outdiv + 1;
//	DBG("OUTDIV=%d, Fvco=%dHz\n", fll->outdiv, freq_out);//8 98.304MHz

	if (freq_in > 1000000) {
		fll->fll_fratio = 0;
	} else {
		fll->fll_fratio = 3;
		freq_in *= 8;
	}
//	DBG("FLL_FRATIO=%d, Fref=%dHz\n", fll->fll_fratio, freq_in);//0 12M

	/* Now, calculate N.K */
	Ndiv = freq_out / freq_in;

	fll->n = Ndiv;
	Nmod = freq_out % freq_in;
//	DBG("Nmod=%d\n", Nmod);

	/* Calculate fractional part - scale up so we can round. */
	Kpart = FIXED_FLL_SIZE * (long long)Nmod;

	do_div(Kpart, freq_in);

	K = Kpart & 0xFFFFFFFF;

	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	fll->k = K / 10;

//	DBG("N=%x K=%x\n", fll->n, fll->k);//8 3127

	return 0;
}

static int wm8994_set_fll(unsigned int freq_in, unsigned int freq_out) 			  
{
	int ret;
	struct fll_div fll;
	u16 reg=0;
//	DBG("Enter %s::%s---%d\n",__FILE__,__FUNCTION__,__LINE__);
	wm8994_write(0x220, 0x0000); 
	/* If we're stopping the FLL redo the old config - no
	 * registers will actually be written but we avoid GCC flow
	 * analysis bugs spewing warnings.
	 */
	ret = wm8994_get_fll_config(&fll, freq_in, freq_out);
	if (ret < 0)
		return ret;

	reg = (fll.outdiv << WM8994_FLL1_OUTDIV_SHIFT) |(fll.fll_fratio << WM8994_FLL1_FRATIO_SHIFT);
	wm8994_write(0x221, reg);//0x221  DIV
	wm8994_write(0x222, fll.k);//0x222	K
	wm8994_write(0x223,	fll.n << WM8994_FLL1_N_SHIFT);//0x223		N
	wm8994_write(0x224, fll.clk_ref_div << WM8994_FLL1_REFCLK_DIV_SHIFT);//0x224	

	wm8994_write(0x220, 0x0004); 
	msleep(10);
	wm8994_write(0x220, 0x0005); 			
	msleep(5);
	wm8994_write(0x200, 0x0010); // sysclk = MCLK1	
	return 0;
}

static int wm8994_sysclk_config(void)
{
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;
	unsigned int freq_in,freq_out;
	
	wm8994_write(0x200, 0x0000); 
	freq_in = 	wm8994->mclk;
	switch(wm8994->mclk)
	{
	case 12288000:
	case 11289600:
		freq_out = wm8994->mclk;
		break;
	case 3072000:
	case 2822400:	
		freq_out = wm8994->mclk * 4;
		break;
	default:
		printk("wm8994->mclk error = %d\n",wm8994->mclk);
		return -1;
	}
	
	switch(wm8994->sysclk)
	{
	case WM8994_SYSCLK_FLL1: 
		wm8994_set_fll(freq_in,freq_out);
		break;
	case WM8994_SYSCLK_FLL2:
		break;
	case WM8994_SYSCLK_MCLK2:
		wm8994_write(0x701, 0x0000);//MCLK2
	case WM8994_SYSCLK_MCLK1:
		if(freq_out == freq_in)
			break;
	default:
		printk("wm8994->sysclk error = %d\n",wm8994->sysclk);
		return -1;
	}
	
	wm8994_write(0x208, 0x0008); //DSP_FS1CLK_ENA=1, DSP_FSINTCLK_ENA=1
	wm8994_write(0x208, 0x000A); //DSP_FS1CLK_ENA=1, DSP_FSINTCLK_ENA=1	
	
	switch(wm8994->rate)
	{
	case 8000:
		printk("wm8994->rate = %d!!!!\n",wm8994->rate);
		break;
	case 44100:
		wm8994_write(0x210, 0x0073); // SR=48KHz
		break;
	case 48000:
		wm8994_write(0x210, 0x0083); // SR=48KHz
		break;
	case 11025:
	case 16000:
	case 22050:
	case 32000:		
	default:
		printk("wm8994->rate error = %d\n",wm8994->rate);
		return -1;
	}	
	
	switch(wm8994->fmt)
	{
	case SND_SOC_DAIFMT_CBS_CFS:
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	default:
		printk("wm8994->fmt error = %d\n",wm8994->fmt);
		return -1;
	}
	
	wm8994_write(0x200, wm8994->sysclk << 3|0x01); 
	return 0;
}

static void wm8994_set_AIF1DAC_EQ(void)
{
	//100HZ. 300HZ.  875HZ  2400HZ    6900HZ
//	int bank_vol[6] = {0,0,-3,3,-6,3};//-12DB ~ 12DB   default 0DB
	int bank_vol[6] = {6,2,0,0,0,0};//-12DB ~ 12DB   default 0DB	
	wm8994_write(0x0480, 0x0001|((bank_vol[1]+12)<<11)|
		((bank_vol[2]+12)<<6)|((bank_vol[3]+12)<<1));
	wm8994_write(0x0481, 0x0000|((bank_vol[4]+12)<<11)|
		((bank_vol[5]+12)<<6));
}

static int wm8994_reset_ldo(void)
{
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;	
	struct wm8994_pdata *pdata = wm8994->pdata;
	unsigned short value;
	
	if(wm8994->RW_status == TRUE)
		return 0;
		
	gpio_request(pdata->Power_EN_Pin, NULL);
	gpio_direction_output(pdata->Power_EN_Pin,GPIO_LOW);
	gpio_free(pdata->Power_EN_Pin);	
	msleep(50);
	gpio_request(pdata->Power_EN_Pin, NULL);
	gpio_direction_output(pdata->Power_EN_Pin,GPIO_HIGH);
	gpio_free(pdata->Power_EN_Pin);		
	msleep(50);
	
	wm8994->RW_status = TRUE;
	wm8994_read(0x00,  &value);

	if(value == 0x8994)
		DBG("wm8994_reset_ldo Read ID = 0x%x\n",value);
	else
	{
		wm8994->RW_status = ERROR;
		printk("wm8994_reset_ldo Read ID error value = 0x%x\n",value);
		return -1;
	}	
		
	return 0;	
}
//Set the volume of each channel (including recording)
static void wm8994_set_channel_vol(void)
{
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;
	struct wm8994_pdata *pdata = wm8994->pdata;	
	int vol;

	switch(wm8994_current_mode){
	case wm8994_AP_to_speakers_and_headset:
		MAX_MIN(-57,pdata->speaker_normal_vol,6);
		MAX_MIN(-57,pdata->headset_normal_vol,6);
		DBG("headset_normal_vol = %ddB \n",pdata->headset_normal_vol);
		DBG("speaker_normal_vol = %ddB \n",pdata->speaker_normal_vol);
		
		vol = pdata->speaker_normal_vol;
		wm8994_write(0x26,  320+vol+57);  //-57dB~6dB
		wm8994_write(0x27,  320+vol+57);  //-57dB~6dB

		vol = pdata->headset_normal_vol-4;
		//for turn off headset volume when ringtone
		if(vol >= -48)
			vol -= 14;
		else
			vol = -57;

		wm8994_write(0x1C,  320+vol+57);  //-57dB~6dB
		wm8994_write(0x1D,  320+vol+57);  //-57dB~6dB

		wm8994_set_AIF1DAC_EQ();
		break;

	case wm8994_AP_to_headset:
		MAX_MIN(-57,pdata->headset_normal_vol,6);
		DBG("headset_normal_vol = %ddB \n",pdata->headset_normal_vol);
		vol = pdata->headset_normal_vol;
		wm8994_write(0x1C,  320+vol+57);  //-57dB~6dB
		wm8994_write(0x1D,  320+vol+57);  //-57dB~6dB
		break;

	case wm8994_AP_to_speakers:
		MAX_MIN(-57,pdata->speaker_normal_vol,6);
		DBG("speaker_normal_vol = %ddB \n",pdata->speaker_normal_vol);
		vol = pdata->speaker_normal_vol;
		wm8994_write(0x26,  320+vol+57);  //-57dB~6dB
		wm8994_write(0x27,  320+vol+57);  //-57dB~6dB
		break;
		
	case wm8994_handsetMIC_to_baseband_to_headset:
		MAX_MIN(-12,pdata->headset_incall_vol,6);
		MAX_MIN(-22,pdata->headset_incall_mic_vol,30);
		DBG("headset_incall_mic_vol = %ddB \n",pdata->headset_incall_mic_vol);
		DBG("headset_incall_vol = %ddB \n",pdata->headset_incall_vol);

		vol = pdata->headset_incall_mic_vol;
		if(vol<-16)
		{
			wm8994_write(0x1E,  0x0016);  //mic vol
			wm8994_write(0x18,  320+(vol+22)*10/15);  //mic vol	
		}
		else
		{
			wm8994_write(0x1E,  0x0006);  //mic vol
			wm8994_write(0x18,  320+(vol+16)*10/15);  //mic vol
		}
		break;
	case wm8994_mainMIC_to_baseband_to_headset:
		MAX_MIN(-12,pdata->headset_incall_vol,6);
		MAX_MIN(-22,pdata->speaker_incall_mic_vol,30);
		DBG("speaker_incall_mic_vol = %ddB \n",pdata->speaker_incall_mic_vol);
		DBG("headset_incall_vol = %ddB \n",pdata->headset_incall_vol);

		vol=pdata->speaker_incall_mic_vol;
		if(vol<-16)
		{
			wm8994_write(0x1E,  0x0016);  //mic vol
			wm8994_write(0x1A,  320+(vol+22)*10/15);  //mic vol	
		}
		else
		{
			wm8994_write(0x1E,  0x0006);  //mic vol
			wm8994_write(0x1A,  320+(vol+16)*10/15);  //mic vol
		}
		break;

	case wm8994_mainMIC_to_baseband_to_earpiece:
		MAX_MIN(-22,pdata->speaker_incall_mic_vol,30);
		MAX_MIN(-21,pdata->earpiece_incall_vol,6);
		DBG("earpiece_incall_vol = %ddB \n",pdata->earpiece_incall_vol);
		DBG("speaker_incall_mic_vol = %ddB \n",pdata->speaker_incall_mic_vol);

		vol = pdata->earpiece_incall_vol;
		if(vol>=0)
		{
			wm8994_write(0x33,  0x0018);  //6dB
			wm8994_write(0x31,  (((6-vol)/3)<<3)+(6-vol)/3);  //-21dB
		}
		else
		{
			wm8994_write(0x33,  0x0010);
			wm8994_write(0x31,  (((-vol)/3)<<3)+(-vol)/3);  //-21dB
		}
		vol = pdata->speaker_incall_mic_vol;
		if(vol<-16)
		{
			wm8994_write(0x1E,  0x0016);
			wm8994_write(0x1A,  320+(vol+22)*10/15);	
		}
		else
		{
			wm8994_write(0x1E,  0x0006);
			wm8994_write(0x1A,  320+(vol+16)*10/15);
		}
		break;

	case wm8994_mainMIC_to_baseband_to_speakers:
		MAX_MIN(-22,pdata->speaker_incall_mic_vol,30);
		MAX_MIN(-21,pdata->speaker_incall_vol,12);
		DBG("speaker_incall_vol = %ddB \n",pdata->speaker_incall_vol);
		DBG("speaker_incall_mic_vol = %ddB \n",pdata->speaker_incall_mic_vol);

		vol = pdata->speaker_incall_mic_vol;
		if(vol<-16)
		{
			wm8994_write(0x1E,  0x0016);
			wm8994_write(0x1A,  320+(vol+22)*10/15);	
		}
		else
		{
			wm8994_write(0x1E,  0x0006);
			wm8994_write(0x1A,  320+(vol+16)*10/15);
		}
		vol = pdata->speaker_incall_vol;
		if(vol<=0)
		{
			wm8994_write(0x31,  (((-vol)/3)<<3)+(-vol)/3);
		}
		else if(vol <= 9)
		{
			wm8994_write(0x25,  ((vol*10/15)<<3)+vol*10/15);
		}
		else
		{
			wm8994_write(0x25,  0x003F);
		}
		break;

	case wm8994_BT_baseband:
		MAX_MIN(-16,pdata->BT_incall_vol,30);
		MAX_MIN(-57,pdata->BT_incall_mic_vol,6);
		DBG("BT_incall_mic_vol = %ddB \n",pdata->BT_incall_mic_vol);
		DBG("BT_incall_vol = %ddB \n",pdata->BT_incall_vol);
		vol = pdata->BT_incall_mic_vol;
		wm8994_write(0x20,  320+vol+57);
		vol = pdata->BT_incall_vol;
		wm8994_write(0x19, 0x0500+(vol+16)*10/15);
		break;
	default:
		printk("route error !\n");
	}

}

#define wm8994_reset()	wm8994_set_all_mute();\
						wm8994_write(WM8994_RESET, 0)					

void record_only(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);
	if(wm8994_current_mode==wm8994_record_only)return;
	wm8994_current_mode=wm8994_record_only;		
	wm8994_write(0,0);
	msleep(WM8994_DELAY);

	wm8994_write(0x01,  0x0003);
	msleep(WM8994_DELAY);
//clk
	wm8994_sysclk_config();
	wm8994_write(0x300, 0xC010); //AIF1ADCL_SRC=1, AIF1ADCR_SRC=1, AIF1_WL=00, AIF1_FMT=10
//	wm8994_write(0x300, 0xC050); // AIF1ADCL_SRC=1, AIF1ADCR_SRC=1, AIF1_WL=10, AIF1_FMT=10
//path
	wm8994_write(0x28,  0x0003); // IN1RP_TO_IN1R=1, IN1RN_TO_IN1R=1
	wm8994_write(0x2A,  0x0030); //IN1R_TO_MIXINR   IN1R_MIXINR_VOL
	wm8994_write(0x606, 0x0002); // ADC1L_TO_AIF1ADC1L=1
	wm8994_write(0x607, 0x0002); // ADC1R_TO_AIF1ADC1R=1
	wm8994_write(0x620, 0x0000); //ADC_OSR128=0, DAC_OSR128=0	
//DRC
	wm8994_write(0x440, 0x01BF);
	wm8994_write(0x450, 0x01BF);	
//valume
//	wm8994_write(0x1A,  0x014B);//IN1_VU=1, IN1R_MUTE=0, IN1R_ZC=1, IN1R_VOL=0_1011
	wm8994_write(0x402, 0x01FF); // AIF1ADC1L_VOL [7:0]
	wm8994_write(0x403, 0x01FF); // AIF1ADC1R_VOL [7:0]
	
//power
	wm8994_write(0x02,  0x6110); // TSHUT_ENA=1, TSHUT_OPDIS=1, MIXINR_ENA=1,IN1R_ENA=1
	wm8994_write(0x04,  0x0303); // AIF1ADC1L_ENA=1, AIF1ADC1R_ENA=1, ADCL_ENA=1, ADCR_ENA=1	
	wm8994_write(0x01,  0x3033);
}

void recorder_add(void)
{
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;
	struct wm8994_pdata *pdata = wm8994->pdata;	
	if(wm8994_current_mode == null)
	{
		record_only();
		goto out;
	}
	DBG("%s::%d\n",__FUNCTION__,__LINE__);
	if(wm8994_current_mode==wm8994_record_add)return;
	wm8994_current_mode=wm8994_record_add;	
//path
	wm8994_set_bit(0x28,  0x0003); // IN1RP_TO_IN1R=1, IN1RN_TO_IN1R=1
	wm8994_set_bit(0x2A,  0x0030); //IN1R_TO_MIXINR   IN1R_MIXINR_VOL
	wm8994_set_bit(0x606, 0x0002); // ADC1L_TO_AIF1ADC1L=1
	wm8994_set_bit(0x607, 0x0002); // ADC1R_TO_AIF1ADC1R=1
//DRC
	wm8994_set_bit(0x440, 0x01BF);
	wm8994_set_bit(0x450, 0x01BF);	
//valume
//	wm8994_set_bit(0x1A,  0x014B);//IN1_VU=1, IN1R_MUTE=0, IN1R_ZC=1, IN1R_VOL=0_1011
	wm8994_set_bit(0x402, 0x01FF); // AIF1ADC1L_VOL [7:0]
	wm8994_set_bit(0x403, 0x01FF); // AIF1ADC1R_VOL [7:0]	
//power
	wm8994_set_bit(0x02,  0x6110); // TSHUT_ENA=1, TSHUT_OPDIS=1, MIXINR_ENA=1,IN1R_ENA=1
	wm8994_set_bit(0x04,  0x0303); // AIF1ADC1L_ENA=1, AIF1ADC1R_ENA=1, ADCL_ENA=1, ADCR_ENA=1	
	wm8994_set_bit(0x01,  0x0033);	
	
out:
	MAX_MIN(-16,pdata->recorder_vol,60);
	DBG("recorder_vol = %ddB \n",pdata->recorder_vol);
	wm8994_write(0x1A,  320+(pdata->recorder_vol+16)*10/15);  //mic vol	
}

void AP_to_speakers_and_headset(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);
	if(wm8994_current_mode==wm8994_AP_to_speakers_and_headset)return;
	wm8994_current_mode=wm8994_AP_to_speakers_and_headset;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x39,  0x006C);
	wm8994_write(0x01,  0x0023);
	msleep(WM8994_DELAY);
//clk
	wm8994_sysclk_config();
	wm8994_write(0x300, 0xC010); // i2s 16 bits	
//path
	wm8994_write(0x2D,  0x0100);
	wm8994_write(0x2E,  0x0100);
	wm8994_write(0x60,  0x0022);
	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x420, 0x0000); 	
	wm8994_write(0x601, 0x0001);
	wm8994_write(0x602, 0x0001);
	wm8994_write(0x36,  0x0003);
//	wm8994_write(0x24,  0x0011);	
//other
	wm8994_write(0x4C,  0x9F25);
//volume
	wm8994_write(0x22,  0x0000);
	wm8994_write(0x23,  0x0100);
	wm8994_write(0x610, 0x01C0);  //DAC1 Left Volume bit0~7	
	wm8994_write(0x611, 0x01C0);  //DAC1 Right Volume bit0~7
//	wm8994_write(0x25,  0x003F);	
	wm8994_set_channel_vol();
//power
	wm8994_write(0x04,  0x0303); // AIF1ADC1L_ENA=1, AIF1ADC1R_ENA=1, ADCL_ENA=1, ADCR_ENA=1
	wm8994_write(0x05,  0x0303);  
	wm8994_write(0x03,  0x3330);	
	wm8994_write(0x01,  0x3303);
	msleep(50); 
	wm8994_write(0x01,  0x3333);	
}

void AP_to_headset(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_AP_to_headset)return;
	wm8994_current_mode=wm8994_AP_to_headset;
	wm8994_reset();
	msleep(WM8994_DELAY);
	
	wm8994_write(0x39, 0x006C);

	wm8994_write(0x01, 0x0003);
	msleep(35);	
	wm8994_write(0xFF, 0x0000);
	msleep(5);
	wm8994_write(0x4C, 0x9F25);
	msleep(5);
	wm8994_write(0x01, 0x0303);
	wm8994_write(0x60, 0x0022);
	msleep(5);	
	wm8994_write(0x54, 0x0033);//
	
	wm8994_write(0x200, 0x0000);
	msleep(WM8994_DELAY);
//clk
	wm8994_sysclk_config();
	wm8994_write(0x300, 0xC010); // i2s 16 bits	
//path	
	wm8994_write(0x2D,  0x0100); // DAC1L_TO_HPOUT1L=1   
	wm8994_write(0x2E,  0x0100); // DAC1R_TO_HPOUT1R=1   
	wm8994_write(0x60,  0x0022);
	wm8994_write(0x60,  0x00FF);
	wm8994_write(0x420, 0x0000); 
	wm8994_write(0x601, 0x0001); // AIF1DAC1L_TO_DAC1L=1
	wm8994_write(0x602, 0x0001); // AIF1DAC1R_TO_DAC1R=1
//volume	
	wm8994_write(0x610, 0x01FF); // DAC1_VU=1, DAC1L_VOL=1100_0000
	wm8994_write(0x611, 0x01FF); // DAC1_VU=1, DAC1R_VOL=1100_0000
	wm8994_set_channel_vol();	
//other
	wm8994_write(0x620, 0x0001); 		
//power
	wm8994_write(0x03,  0x3030);
	wm8994_write(0x05,  0x0303); // AIF1DAC1L_ENA=1, AIF1DAC1R_ENA=1, DAC1L_ENA=1, DAC1R_ENA=1
	
	wm8994_write(0x01,  0x0333);
}

void AP_to_speakers(void)
{
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_AP_to_speakers)return;
	wm8994_current_mode=wm8994_AP_to_speakers;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x39,  0x006C);
	wm8994_write(0x01,  0x0023);
	msleep(WM8994_DELAY);
//clk
	wm8994_sysclk_config();
	wm8994_write(0x300, 0xC010); // i2s 16 bits	
//path
	wm8994_write(0x2D,  0x0001); // DAC1L_TO_MIXOUTL=1
	wm8994_write(0x2E,  0x0001); // DAC1R_TO_MIXOUTR=1
	wm8994_write(0x36,  0x000C); // MIXOUTL_TO_SPKMIXL=1, MIXOUTR_TO_SPKMIXR=1
	wm8994_write(0x601, 0x0001); // AIF1DAC1L_TO_DAC1L=1
	wm8994_write(0x602, 0x0001); // AIF1DAC1R_TO_DAC1R=1
	wm8994_write(0x420, 0x0000);
//	wm8994_write(0x24,  0x001f);	
//volume
	wm8994_write(0x22,  0x0000);
	wm8994_write(0x23,  0x0100); // SPKOUT_CLASSAB=1	
	wm8994_write(0x610, 0x01C0); // DAC1_VU=1, DAC1L_VOL=1100_0000
	wm8994_write(0x611, 0x01C0); // DAC1_VU=1, DAC1R_VOL=1100_0000	
	wm8994_write(0x25,  0x003F);
	wm8994_set_channel_vol();	
//other	
	wm8994_write(0x4C,  0x9F25);	
//power
	wm8994_write(0x03,  0x0330); // SPKRVOL_ENA=1, SPKLVOL_ENA=1, MIXOUTL_ENA=1, MIXOUTR_ENA=1  
	wm8994_write(0x05,  0x0303); // AIF1DAC1L_ENA=1, AIF1DAC1R_ENA=1, DAC1L_ENA=1, DAC1R_ENA=1	
	wm8994_write(0x01,  0x3003);
	msleep(50);
	wm8994_write(0x01,  0x3033);
}

#ifndef PCM_BB
void handsetMIC_to_baseband_to_headset(void)
{//
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;
	struct wm8994_pdata *pdata = wm8994->pdata;
	
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode == wm8994_handsetMIC_to_baseband_to_headset)return;
	wm8994_current_mode = wm8994_handsetMIC_to_baseband_to_headset;
	wm8994_reset();
	msleep(WM8994_DELAY);
	
	wm8994_write(0x01,  0x0023); 
	wm8994_write(0x200, 0x0000);
	msleep(WM8994_DELAY);
//clk
	wm8994_sysclk_config();
	wm8994_write(0x300, 0xC010); // i2s 16 bits	
//path
	wm8994_write(0x28,  0x0030); //IN1LN_TO_IN1L IN1LP_TO_IN1L
	wm8994_write(0x34,  0x0002); //IN1L_TO_LINEOUT1P	
	if(pdata->BB_input_diff == 1)
	{
		wm8994_write(0x2B,  0x0005);
		wm8994_write(0x2D,  0x0041);    
		wm8994_write(0x2E,  0x0081);  		
	}
	else
	{
		wm8994_write(0x2D,  0x0003);  //bit 1 IN2LP_TO_MIXOUTL bit 0 DAC1L_TO_MIXOUTL   
		wm8994_write(0x2E,  0x0003);  //bit 1 IN2RP_TO_MIXOUTR bit 0 DAC1R_TO_MIXOUTL
	}
	wm8994_write(0x60,  0x0022);
	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x420, 0x0000);
	wm8994_write(0x601, 0x0001); //AIF1DAC1L_TO_DAC1L
	wm8994_write(0x602, 0x0001); //AIF1DAC1R_TO_DAC1R
//volume
	wm8994_write(0x610, 0x01A0);  //DAC1 Left Volume bit0~7  		
	wm8994_write(0x611, 0x01A0);  //DAC1 Right Volume bit0~7
	wm8994_set_channel_vol();	
//other
	wm8994_write(0x4C,  0x9F25);	
//power
	wm8994_write(0x03,  0x3030);
	wm8994_write(0x05,  0x0303);
	wm8994_write(0x02,  0x6240); 
	wm8994_write(0x01,  0x0303);
	msleep(50);
	wm8994_write(0x01,  0x0333);
	
	wm8994_set_level_volume();
}

void mainMIC_to_baseband_to_headset(void)
{//
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;
	struct wm8994_pdata *pdata = wm8994->pdata;
	
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode == wm8994_mainMIC_to_baseband_to_headset)return;
	wm8994_current_mode = wm8994_mainMIC_to_baseband_to_headset;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01,  0x0023);
	wm8994_write(0x200, 0x0000);
	mdelay(WM8994_DELAY);
//clk
	wm8994_sysclk_config();
	wm8994_write(0x300, 0xC010); // i2s 16 bits	
//path
	wm8994_write(0x28,  0x0003);  //IN1RN_TO_IN1R IN1RP_TO_IN1R
	wm8994_write(0x34,  0x0004);  //IN1R_TO_LINEOUT1P	
	if(pdata->BB_input_diff == 1)
	{
		wm8994_write(0x2B,  0x0005);
		wm8994_write(0x2D,  0x0041);    
		wm8994_write(0x2E,  0x0081);  		
	}
	else
	{
		wm8994_write(0x2D,  0x0003);  //bit 1 IN2LP_TO_MIXOUTL bit 0 DAC1L_TO_MIXOUTL   
		wm8994_write(0x2E,  0x0003);  //bit 1 IN2RP_TO_MIXOUTR bit 0 DAC1R_TO_MIXOUTL
	}
	wm8994_write(0x36,  0x0003);
	wm8994_write(0x60,  0x0022);
	wm8994_write(0x60,  0x00EE);
	wm8994_write(0x420, 0x0000);
	wm8994_write(0x601, 0x0001);
	wm8994_write(0x602, 0x0001);
//volume
	wm8994_write(0x610, 0x01A0);  //DAC1 Left Volume bit0~7  		
	wm8994_write(0x611, 0x01A0);  //DAC1 Right Volume bit0~7
	wm8994_set_channel_vol();	
//other
	wm8994_write(0x4C,  0x9F25);	
//power
	wm8994_write(0x03,  0x3030);
	wm8994_write(0x05,  0x0303);
	wm8994_write(0x02,  0x6250);
	wm8994_write(0x01,  0x0303);
	msleep(50);
	wm8994_write(0x01,  0x0333);	
	
	wm8994_set_level_volume();
}

void mainMIC_to_baseband_to_earpiece(void)
{//
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode == wm8994_mainMIC_to_baseband_to_earpiece)return;
	wm8994_current_mode = wm8994_mainMIC_to_baseband_to_earpiece;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01,  0x0023);
	wm8994_write(0x200, 0x0000);
	msleep(WM8994_DELAY);
//clk
	wm8994_sysclk_config();
	wm8994_write(0x300, 0xC010); // i2s 16 bits	
//path
	wm8994_write(0x28,  0x0003); //IN1RP_TO_IN1R IN1RN_TO_IN1R
	wm8994_write(0x34,  0x0004); //IN1R_TO_LINEOUT1P
	wm8994_write(0x2D,  0x0003);  //bit 1 IN2LP_TO_MIXOUTL bit 0 DAC1L_TO_MIXOUTL   
	wm8994_write(0x2E,  0x0003);  //bit 1 IN2RP_TO_MIXOUTR bit 0 DAC1R_TO_MIXOUTL
	wm8994_write(0x601, 0x0001); //AIF1DAC1L_TO_DAC1L=1
	wm8994_write(0x602, 0x0001); //AIF1DAC1R_TO_DAC1R=1
	wm8994_write(0x420, 0x0000);
//volume
	wm8994_write(0x610, 0x01C0); //DAC1_VU=1, DAC1L_VOL=1100_0000
	wm8994_write(0x611, 0x01C0); //DAC1_VU=1, DAC1R_VOL=1100_0000
	wm8994_write(0x1F,  0x0000);//HPOUT2
	wm8994_set_channel_vol();	
//other
	wm8994_write(0x4C,  0x9F25);		
//power
	wm8994_write(0x01,  0x0833); //HPOUT2_ENA=1, VMID_SEL=01, BIAS_ENA=1
	wm8994_write(0x02,  0x6210); //bit4 IN1R_ENV bit6 IN1L_ENV 
	wm8994_write(0x03,  0x30F0);
	wm8994_write(0x05,  0x0303);
	
	wm8994_set_level_volume();	
}

void mainMIC_to_baseband_to_speakers(void)
{//
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_mainMIC_to_baseband_to_speakers)return;

	wm8994_current_mode=wm8994_mainMIC_to_baseband_to_speakers;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x39,  0x006C);
	wm8994_write(0x01,  0x0023);
	wm8994_write(0x200, 0x0000);
	msleep(WM8994_DELAY);
//clk
	wm8994_sysclk_config();
	wm8994_write(0x300, 0xC010); // i2s 16 bits	
//path
	wm8994_write(0x22,  0x0000);
	wm8994_write(0x23,  0x0100);
	wm8994_write(0x24,  0x0011);
	//Say
	wm8994_write(0x28,  0x0003);  //IN1RP_TO_IN1R IN1RN_TO_IN1R
	wm8994_write(0x34,  0x0004);  //IN1R_TO_LINEOUT1P
	//Listen
	wm8994_write(0x2D,  0x0003);  //bit 1 IN2LP_TO_MIXOUTL bit 0 DAC1L_TO_MIXOUTL   
	wm8994_write(0x2E,  0x0003);  //bit 1 IN2RP_TO_MIXOUTR bit 0 DAC1R_TO_MIXOUTL
	wm8994_write(0x36,  0x000C);  //MIXOUTL_TO_SPKMIXL	 MIXOUTR_TO_SPKMIXR
	wm8994_write(0x420, 0x0000);  //AIF1DAC1_MUTE = unMUTE
	wm8994_write(0x601, 0x0001);  //AIF1DAC1L_TO_DAC1L=1
	wm8994_write(0x602, 0x0001);  //AIF1DAC1R_TO_DAC1R=1
//volume
//	wm8994_write(0x25,  0x003F);
	wm8994_write(0x610, 0x01C0);  //DAC1 Left Volume bit0~7	
	wm8994_write(0x611, 0x01C0);  //DAC1 Right Volume bit0~7
	wm8994_set_channel_vol();	
//other
	wm8994_write(0x4C,  0x9F25);		
//power
	wm8994_write(0x02,  0x6210);
	wm8994_write(0x03,  0x1330);
	wm8994_write(0x05,  0x0303);	
	wm8994_write(0x01,  0x3003);
	msleep(50);
	wm8994_write(0x01,  0x3033);	
	
	wm8994_set_level_volume();		
}

void BT_baseband(void)
{//
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;
	DBG("%s::%d\n",__FUNCTION__,__LINE__);

	if(wm8994_current_mode==wm8994_BT_baseband)return;

	wm8994_current_mode=wm8994_BT_baseband;
	wm8994_reset();
	msleep(WM8994_DELAY);

	wm8994_write(0x01,  0x0023);
	wm8994_write(0x200, 0x0000);
	msleep(WM8994_DELAY);
//CLK	
	//AIF1CLK
	wm8994_sysclk_config();
	wm8994_write(0x300, 0xC010); // i2s 16 bits	
	//AIF2CLK use FLL2
	wm8994_write(0x204, 0x0000);
	msleep(WM8994_DELAY);
	wm8994_write(0x240, 0x0000);
	switch(wm8994->mclk)
	{
	case 12288000:
		wm8994_write(0x241, 0x2F00);//48
		wm8994_write(0x242, 0);
		wm8994_write(0x243, 0x0100);		
		break;
	case 11289600:
		wm8994_write(0x241, 0x2b00);
		wm8994_write(0x242, 0xfb5b);
		wm8994_write(0x243, 0x00e0);	
		break;
	case 3072000:
		wm8994_write(0x241, 0x2F00);//48
		wm8994_write(0x242, 0);
		wm8994_write(0x243, 0x0400);
		break;
	case 2822400:
		wm8994_write(0x241, 0x2b00);
		wm8994_write(0x242, 0xed6d);
		wm8994_write(0x243, 0x03e0);
		break;
	default:
		printk("wm8994->mclk error = %d\n",wm8994->mclk);
		return;
	}	
	
	wm8994_write(0x240, 0x0004);
	msleep(10);
	wm8994_write(0x240, 0x0005);
	msleep(5);
	wm8994_write(0x204, 0x0018);    // SMbus_16inx_16dat     Write  0x34      * AIF2 Clocking (1)(204H): 0011  AIF2CLK_SRC=10, AIF2CLK_INV=0, AIF2CLK_DIV=0, AIF2CLK_ENA=1
	wm8994_write(0x208, 0x000E);
	wm8994_write(0x211, 0x0003); 
	
	wm8994_write(0x312, 0x3000);    // SMbus_16inx_16dat     Write  0x34      * AIF2 Master/Slave(312H): 7000  AIF2_TRI=0, AIF2_MSTR=1, AIF2_CLK_FRC=0, AIF2_LRCLK_FRC=0
	msleep(30);
	wm8994_write(0x312, 0x7000);
	wm8994_write(0x313, 0x0020);    // SMbus_16inx_16dat     Write  0x34      * AIF2 BCLK DIV--------AIF1CLK/2
	wm8994_write(0x314, 0x0080);    // SMbus_16inx_16dat     Write  0x34      * AIF2 ADCLRCK DIV-----BCLK/128
	wm8994_write(0x315, 0x0080);
	wm8994_write(0x310, 0x0118);    //DSP/PCM; 16bits; ADC L channel = R channel;MODE A
	
	wm8994_write(0x204, 0x0019);    // SMbus_16inx_16dat     Write  0x34      * AIF2 Clocking (1)(204H): 0011  AIF2CLK_SRC=10, AIF2CLK_INV=0, AIF2CLK_DIV=0, AIF2CLK_ENA=1
/*	
	wm8994_write(0x310, 0x0118); 
	wm8994_write(0x204, 0x0001); 	
	wm8994_write(0x208, 0x000F); 	
	wm8994_write(0x211, 0x0009); 	
	wm8994_write(0x312, 0x7000); 	
	wm8994_write(0x313, 0x00F0); 
*/	
//GPIO
	wm8994_write(0x702, 0x2100);
	wm8994_write(0x703, 0x2100);
	wm8994_write(0x704, 0xA100);
	wm8994_write(0x707, 0xA100);
	wm8994_write(0x708, 0x2100);
	wm8994_write(0x709, 0x2100);
	wm8994_write(0x70A, 0x2100);
	wm8994_write(0x06,   0x000A);
//path
	wm8994_write(0x29,   0x0100);
	wm8994_write(0x2A,   0x0100);
	wm8994_write(0x28,   0x00C0);	
	wm8994_write(0x24,   0x0009);
	wm8994_write(0x29,   0x0130);
	wm8994_write(0x2A,   0x0130);
	wm8994_write(0x2D,   0x0001);
	wm8994_write(0x2E,   0x0001);
	wm8994_write(0x34,   0x0001);
	wm8994_write(0x36,   0x0004);
	wm8994_write(0x601, 0x0004);
	wm8994_write(0x602, 0x0001);
	wm8994_write(0x603, 0x000C);
	wm8994_write(0x604, 0x0010);
	wm8994_write(0x605, 0x0010);
	wm8994_write(0x620, 0x0000);
	wm8994_write(0x420, 0x0000);
//DRC&&EQ
	wm8994_write(0x440, 0x0018);
	wm8994_write(0x450, 0x0018);
	wm8994_write(0x540, 0x01BF); //open nosie gate
	wm8994_write(0x550, 0x01BF); //open nosie gate
	wm8994_write(0x480, 0x0000);
	wm8994_write(0x481, 0x0000);
	wm8994_write(0x4A0, 0x0000);
	wm8994_write(0x4A1, 0x0000);
	wm8994_write(0x520, 0x0000);
	wm8994_write(0x540, 0x0018);
	wm8994_write(0x580, 0x0000);
	wm8994_write(0x581, 0x0000);
//volume
	wm8994_set_volume(wm8994_current_mode,wm8994->call_vol,call_maxvol);
	wm8994_write(0x1E,   0x0006);
	wm8994_set_channel_vol();
	wm8994_write(0x22,   0x0000);
	wm8994_write(0x23,   0x0100);	
	wm8994_write(0x610, 0x01C0);
	wm8994_write(0x611, 0x01C0);
	wm8994_write(0x612, 0x01C0);
	wm8994_write(0x613, 0x01C0);	
//other
	wm8994_write(0x4C,   0x9F25);
	wm8994_write(0x60,   0x00EE);
	msleep(5);
//power	
	wm8994_write(0x01,   0x3033);
	wm8994_write(0x02,   0x63A0);
	wm8994_write(0x03,   0x33F0);
	wm8994_write(0x04,   0x3303);
	wm8994_write(0x05,   0x3303);	
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
	wm8994_write(0x1F,  0x0000); 
	wm8994_write(0x28,  0x0030);  ///0x0003);
	wm8994_write(0x29,  0x0020);
	wm8994_write(0x2D,  0x0001);
	wm8994_write(0x2E,  0x0001);
	wm8994_write(0x33,  0x0018);
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
	//wm8994_write(0x25,  0x0152);
	wm8994_write(0x28,  0x0003);
	wm8994_write(0x2A,  0x0020);
	wm8994_write(0x2D,  0x0001);
	wm8994_write(0x2E,  0x0001);
	wm8994_write(0x36,  0x000C);  //MIXOUTL_TO_SPKMIXL  MIXOUTR_TO_SPKMIXR
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
	wm8994_write(0x313, 0x00F0);  //AIF2BCLK
	wm8994_write(0x314, 0x0020);  //AIF2ADCLRCK
	wm8994_write(0x315, 0x0020);  //AIF2DACLRCLK

	wm8994_write(0x603, 0x018C);  //Rev.D ADCL SideTone
	wm8994_write(0x604, 0x0020);  ///0x0010);  //ADC2_TO_DAC2L
	wm8994_write(0x605, 0x0020);  //0x0010);  //ADC2_TO_DAC2R
	wm8994_write(0x621, 0x0000);  ///0x0001);
	wm8994_write(0x317, 0x0003);
	wm8994_write(0x312, 0x0000);  //AIF2 SET AS MASTER


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

	wm8994_write(0x2A ,0x0005);

	wm8994_write(0x313 ,0x00F0);
	wm8994_write(0x314 ,0x0020);
	wm8994_write(0x315 ,0x0020);

	wm8994_write(0x2E ,0x0001);
	wm8994_write(0x420 ,0x0000);
	wm8994_write(0x520 ,0x0000);
	wm8994_write(0x601 ,0x0001);
	wm8994_write(0x602 ,0x0001);
	wm8994_write(0x604 ,0x0001);
	wm8994_write(0x605 ,0x0001);
	wm8994_write(0x607 ,0x0002);
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

}
#endif //PCM_BB

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
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;
	struct wm8994_pdata *pdata = wm8994->pdata;
	char route = kcontrol->private_value & 0xff;
	char last_route = wm8994->kcontrol.private_value & 0xff;
	wake_lock(&wm8994->wm8994_on_wake);
	mutex_lock(&wm8994->route_lock);
	wm8994->kcontrol.private_value = route;//save rount
	wm8994->route_status = BUSY;
	//before set the route -- disable PA
	switch(route)
	{
		case MIC_CAPTURE:
			break;
		default:
			PA_ctrl(GPIO_LOW);
			break;
	}
	//set rount
	switch(route)
	{
		case SPEAKER_NORMAL: 						//AP-> 8994Codec -> Speaker
		case SPEAKER_RINGTONE:			
		case EARPIECE_RINGTONE:	
			if(pdata->phone_pad == 1)
				AP_to_headset();
			else
				AP_to_speakers();
			break;
		case SPEAKER_INCALL: 						//BB-> 8994Codec -> Speaker
			if(pdata->phone_pad == 1)
				mainMIC_to_baseband_to_headset();
			else
				mainMIC_to_baseband_to_speakers();
			break;					
		case HEADSET_NORMAL:						//AP-> 8994Codec -> Headset
			AP_to_headset();
			break;
		case HEADSET_INCALL:						//AP-> 8994Codec -> Headset
#ifdef CONFIG_RK_HEADSET_DET		
			if(Headset_isMic())
				handsetMIC_to_baseband_to_headset();
			else
#endif				
				mainMIC_to_baseband_to_headset();
			break;	    
		case EARPIECE_INCALL:						//BB-> 8994Codec -> EARPIECE
			if(pdata->phone_pad == 1)
				mainMIC_to_baseband_to_headset();
			else
				mainMIC_to_baseband_to_earpiece();
			break;
		case EARPIECE_NORMAL:						//BB-> 8994Codec -> EARPIECE
			switch(wm8994_current_mode)
			{
				case wm8994_handsetMIC_to_baseband_to_headset:
				case wm8994_mainMIC_to_baseband_to_headset:
					AP_to_headset();
					break;
				default:
					if(pdata->phone_pad == 1)
						AP_to_headset();
					else
						AP_to_speakers();	
					break;
			}
			break;   	
		case BLUETOOTH_SCO_INCALL:					//BB-> 8994Codec -> BLUETOOTH_SCO 
			BT_baseband();
			break;   
		case MIC_CAPTURE:
			recorder_add();	
			break;
		case HEADSET_RINGTONE:
			AP_to_speakers_and_headset();
			break;
		case BLUETOOTH_A2DP_NORMAL:					//AP-> 8994Codec -> BLUETOOTH_A2DP
		case BLUETOOTH_A2DP_INCALL:
		case BLUETOOTH_SCO_NORMAL:
			printk("this route not use\n");
			break;		 			
		default:
			printk("wm8994 error route!!!\n");
			goto out;
	}
	
	switch(last_route)
	{
	case MIC_CAPTURE:
		recorder_add();	
		break;		
	default:
		break;
	}
	
	if(wm8994->RW_status == ERROR)
	{//Failure to read or write, will reset wm8994
		cancel_delayed_work_sync(&wm8994->wm8994_delayed_work);
		wm8994->work_type = SNDRV_PCM_TRIGGER_PAUSE_PUSH;
		schedule_delayed_work(&wm8994->wm8994_delayed_work, msecs_to_jiffies(10));
		goto out;
	}
	//after set the route -- enable PA
	switch(route)
	{
		case EARPIECE_NORMAL:
			if(wm8994_current_mode == wm8994_handsetMIC_to_baseband_to_headset||
				wm8994_current_mode == wm8994_mainMIC_to_baseband_to_headset)
				break;
		case SPEAKER_NORMAL:	
		case SPEAKER_RINGTONE:
		case SPEAKER_INCALL:			
		case EARPIECE_RINGTONE:	
		case HEADSET_RINGTONE:		
			msleep(50);
			PA_ctrl(GPIO_HIGH);				
			break;
		case EARPIECE_INCALL:
			if(pdata->phone_pad == 1)
			{
				msleep(50);
				PA_ctrl(GPIO_HIGH);				
			}
			break;
		default: 		
			break;
	}
out:	
	wm8994->route_status = IDLE;
	mutex_unlock(&wm8994->route_lock);	
	wake_unlock(&wm8994->wm8994_on_wake);	
	return 0;
}

/*
 * WM8994 Controls
 */
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

static void wm8994_codec_set_volume(unsigned char system_type,unsigned char volume)
{
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;
//	DBG("%s:: system_type = %d volume = %d \n",__FUNCTION__,system_type,volume);

	if(system_type == VOICE_CALL)
	{
		if(volume <= call_maxvol)
			wm8994->call_vol=volume;
		else
		{
			printk("%s----%d::max value is 5\n",__FUNCTION__,__LINE__);
			wm8994->call_vol=call_maxvol;
		}
		if(wm8994_current_mode == wm8994_handsetMIC_to_baseband_to_headset ||
			wm8994_current_mode == wm8994_mainMIC_to_baseband_to_headset ||
			wm8994_current_mode == wm8994_mainMIC_to_baseband_to_earpiece||
			wm8994_current_mode == wm8994_mainMIC_to_baseband_to_speakers)
			wm8994_set_volume(wm8994_current_mode,wm8994->call_vol,call_maxvol);
	}
	else if(system_type == BLUETOOTH_SCO)
	{
		if(volume <= BT_call_maxvol)
			wm8994->BT_call_vol = volume;
		else
		{
			printk("%s----%d::max value is 15\n",__FUNCTION__,__LINE__);
			wm8994->BT_call_vol = BT_call_maxvol;
		}
		if(wm8994_current_mode<null&&
			wm8994_current_mode>=wm8994_BT_baseband)
			wm8994_set_volume(wm8994_current_mode,wm8994->BT_call_vol,BT_call_maxvol);
	}
	else
	{
		printk("%s----%d::system type error!\n",__FUNCTION__,__LINE__);
		return;
	}
}

/*
 * Note that this should be called from init rather than from hw_params.
 */
static int wm8994_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8994_priv *wm8994 = codec->private_data;
	
//	DBG("%s----%d\n",__FUNCTION__,__LINE__);

	switch (clk_id) {
	case WM8994_SYSCLK_MCLK1:
		wm8994->sysclk = WM8994_SYSCLK_MCLK1;
		wm8994->mclk = freq;
	//	DBG("AIF1 using MCLK1 at %uHz\n", freq);
		break;

	case WM8994_SYSCLK_MCLK2:
		//TODO: Set GPIO AF 
		wm8994->sysclk = WM8994_SYSCLK_MCLK2;
		wm8994->mclk = freq;
	//	DBG("AIF1 using MCLK2 at %uHz\n",freq);
		break;

	case WM8994_SYSCLK_FLL1:
		wm8994->sysclk = WM8994_SYSCLK_FLL1;
		wm8994->mclk = freq;
	//	DBG("AIF1 using FLL1 at %uHz\n",freq);
		break;

	case WM8994_SYSCLK_FLL2:
		wm8994->sysclk = WM8994_SYSCLK_FLL2;
		wm8994->mclk = freq;
	//	DBG("AIF1 using FLL2 at %uHz\n",freq);
		break;

	default:
		DBG("ERROR:AIF3 shares clocking with AIF1/2. \n");
		return -EINVAL;
	}

	return 0;
}

static int wm8994_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8994_priv *wm8994 = codec->private_data;
//	DBG("Enter %s---%d\n",__FUNCTION__,__LINE__);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		wm8994->fmt = SND_SOC_DAIFMT_CBS_CFS;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		wm8994->fmt = SND_SOC_DAIFMT_CBM_CFM;
		break;
	default:
		return -EINVAL;
	}


	return 0;	
}

static int wm8994_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8994_priv *wm8994 = codec->private_data;

//	DBG("Enter %s::%s---%d\n",__FILE__,__FUNCTION__,__LINE__);	

	wm8994->rate = params_rate(params);
//	DBG("Sample rate is %dHz\n",wm8994->rate);

	return 0;
}

static int wm8994_mute(struct snd_soc_dai *dai, int mute)
{
	return 0;
}

static int wm8994_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	return 0;
}

static int wm8994_trigger(struct snd_pcm_substream *substream,
			  int cmd,
			  struct snd_soc_dai *dai)
{
//	struct snd_soc_pcm_runtime *rtd = substream->private_data;
//	struct snd_soc_dai_link *machine = rtd->dai;
//	struct snd_soc_dai *codec_dai = machine->codec_dai;
	struct snd_soc_codec *codec = dai->codec;
	struct wm8994_priv *wm8994 = codec->private_data;
	
	if(wm8994_current_mode >= wm8994_handsetMIC_to_baseband_to_headset && wm8994_current_mode != null)
		return 0;
//	DBG("%s::%d status = %d substream->stream '%s'\n",__FUNCTION__,__LINE__,
//	    cmd, substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? "PLAYBACK":"CAPTURE");
	
	switch(cmd)
	{
		case SNDRV_PCM_TRIGGER_STOP:
		case SNDRV_PCM_TRIGGER_START:
			if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			{
				if(wm8994->playback_active == cmd)
					return 0;
				wm8994->playback_active = cmd;
			}	
			else
			{
				if(wm8994->capture_active == cmd)
					return 0;
				wm8994->capture_active = cmd;	
			}	
			break;
		default:
			return 0;
	}

	if (!wm8994->playback_active && 
		!wm8994->capture_active )
	{//suspend
		DBG("It's going to power down wm8994\n");
		cancel_delayed_work_sync(&wm8994->wm8994_delayed_work);		
		wm8994->work_type = SNDRV_PCM_TRIGGER_STOP;
		schedule_delayed_work(&wm8994->wm8994_delayed_work, msecs_to_jiffies(2000));
	} 
	else if (wm8994->playback_active 
			|| wm8994->capture_active) 
	{//resume
		DBG("Power up wm8994\n");	
		if(wm8994->work_type == SNDRV_PCM_TRIGGER_STOP)
			cancel_delayed_work_sync(&wm8994->wm8994_delayed_work);
		wm8994->work_type = SNDRV_PCM_TRIGGER_START;
		schedule_delayed_work(&wm8994->wm8994_delayed_work, msecs_to_jiffies(0));		
	}

	return 0;
}

static void wm8994_work_fun(struct work_struct *work)
{	
	struct snd_soc_codec *codec = wm8994_codec;
	struct wm8994_priv *wm8994 = codec->private_data;
	struct wm8994_pdata *pdata = wm8994->pdata;
	int error_count = 5;
//	DBG("Enter %s---%d = %d\n",__FUNCTION__,__LINE__,wm8994_current_mode);

	switch(wm8994->work_type)
	{
	case SNDRV_PCM_TRIGGER_STOP:
		if(wm8994_current_mode > wm8994_AP_to_speakers)
			return;	
	//	DBG("wm8994 shutdown\n");
		mutex_lock(&wm8994->route_lock);
		wm8994->route_status = BUSY;
		PA_ctrl(GPIO_LOW);
		msleep(50);
		wm8994_write(0,0);
		msleep(50);
		wm8994_write(0x01, 0x0033);	
		wm8994_current_mode = null;//Automatically re-set the wake-up time	
		wm8994->route_status = IDLE;
		mutex_unlock(&wm8994->route_lock);	
		break;
	case SNDRV_PCM_TRIGGER_START:
		break;
	case SNDRV_PCM_TRIGGER_RESUME:	
		msleep(100);
		gpio_request(pdata->Power_EN_Pin, NULL);
		gpio_direction_output(pdata->Power_EN_Pin,GPIO_HIGH);
		gpio_free(pdata->Power_EN_Pin);		
		msleep(100);
		wm8994_current_mode = null;
		snd_soc_put_route(&wm8994->kcontrol,NULL);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:	
		while(error_count)
		{
			if( wm8994_reset_ldo() ==  0)
			{
				wm8994_current_mode = null;
				snd_soc_put_route(&wm8994->kcontrol,NULL);
				break;
			}
			error_count --;
		}
		if(error_count == 0)
		{
			PA_ctrl(GPIO_LOW);
			printk("wm8994 Major problems, give me log,tks, -- qjb\n");
		}	
		break;

	default:
		break;
	}
}

#define WM8994_RATES SNDRV_PCM_RATE_8000_48000
#define WM8994_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops wm8994_ops = {
	.hw_params = wm8994_pcm_hw_params,
	.set_fmt = wm8994_set_dai_fmt,
	.set_sysclk = wm8994_set_dai_sysclk,
	.digital_mute = wm8994_mute,
	.trigger	= wm8994_trigger,
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
	struct wm8994_priv *wm8994 = codec->private_data;
	struct wm8994_pdata *pdata = wm8994->pdata;
	
	cancel_delayed_work_sync(&wm8994->wm8994_delayed_work);	
	
	if(wm8994_current_mode>wm8994_AP_to_speakers &&
		wm8994_current_mode< null )//incall status,wm8994 not suspend		
		return 0;		
	DBG("%s----%d\n",__FUNCTION__,__LINE__);
	
	wm8994->route_status = SUSPEND;
	PA_ctrl(GPIO_LOW);
	wm8994_write(0x00, 0x00);
	
	gpio_request(pdata->Power_EN_Pin, NULL);
	gpio_direction_output(pdata->Power_EN_Pin,GPIO_LOW);
	gpio_free(pdata->Power_EN_Pin);	
	msleep(50);

	return 0;
}

static int wm8994_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	struct wm8994_priv *wm8994 = codec->private_data;
//	struct wm8994_pdata *pdata = wm8994->pdata;
	if(wm8994_current_mode>wm8994_AP_to_speakers &&
		wm8994_current_mode< null )//incall status,wm8994 not suspend
		return 0;		
	DBG("%s----%d\n",__FUNCTION__,__LINE__);

	cancel_delayed_work_sync(&wm8994->wm8994_delayed_work);	
	wm8994->work_type = SNDRV_PCM_TRIGGER_RESUME;
	schedule_delayed_work(&wm8994->wm8994_delayed_work, msecs_to_jiffies(0));	

	return 0;
}

#ifdef WM8994_PROC
static ssize_t wm8994_proc_write(struct file *file, const char __user *buffer,
			   unsigned long len, void *data)
{
	char *cookie_pot; 
	char *p;
	int reg;
	int value;
	struct snd_kcontrol kcontrol;
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;
	struct wm8994_pdata *pdata = wm8994->pdata;
	
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
	case 'd':
	case 'D':
		debug_write_read ++;
		debug_write_read %= 2;
		if(debug_write_read != 0)
			DBG("Debug read and write reg on\n");
		else	
			DBG("Debug read and write reg off\n");	
		break;	
	case 'r':
	case 'R':
		DBG("Read reg debug\n");		
		if(cookie_pot[1] ==':')
		{
			debug_write_read = 1;
			strsep(&cookie_pot,":");
			while((p=strsep(&cookie_pot,",")))
			{
				wm8994_read(simple_strtol(p,NULL,16),(unsigned short *)&value);
			}
			debug_write_read = 0;;
			DBG("\n");		
		}
		else
		{
			DBG("Error Read reg debug.\n");
			DBG("For example: echo 'r:22,23,24,25'>wm8994_ts\n");
		}
		break;
	case 'w':
	case 'W':
		DBG("Write reg debug\n");		
		if(cookie_pot[1] ==':')
		{
			debug_write_read = 1;
			strsep(&cookie_pot,":");
			while((p=strsep(&cookie_pot,"=")))
			{
				reg = simple_strtol(p,NULL,16);
				p=strsep(&cookie_pot,",");
				value = simple_strtol(p,NULL,16);
				wm8994_write(reg,value);
			}
			debug_write_read = 0;;
			DBG("\n");
		}
		else
		{
			DBG("Error Write reg debug.\n");
			DBG("For example: w:22=0,23=0,24=0,25=0\n");
		}
		break;	
	case 'p':
	case 'P':
		if(cookie_pot[1] =='-')
		{
			kcontrol.private_value = simple_strtol(&cookie_pot[2],NULL,10);
			printk("kcontrol.private_value = %ld\n",kcontrol.private_value);
			if(kcontrol.private_value<SPEAKER_INCALL || kcontrol.private_value>HEADSET_RINGTONE)
			{
				printk("route error\n");
				goto help;
			}	
			snd_soc_put_route(&kcontrol,NULL);
			break;
		}	
		else if(cookie_pot[1] ==':')
		{
			strsep(&cookie_pot,":");
			while((p=strsep(&cookie_pot,",")))
			{
				kcontrol.private_value = simple_strtol(p,NULL,10);
				printk("kcontrol.private_value = %ld\n",kcontrol.private_value);
				if(kcontrol.private_value<SPEAKER_INCALL || kcontrol.private_value>HEADSET_RINGTONE)
				{
					printk("route error\n");
					goto help;
				}	
				snd_soc_put_route(&kcontrol,NULL);
			}
			break;
		}		
		else
		{
			goto help;
		}
		help:
			printk("snd_soc_put_route list\n");
			printk("SPEAKER_INCALL--\"p-0\",\nSPEAKER_NORMAL--\"p-1\",\nHEADSET_INCALL--\"p-2\",\
			\nHEADSET_NORMAL--\"p-3\",\nEARPIECE_INCALL--\"p-4\",\nEARPIECE_NORMAL--\"p-5\",\
			\nBLUETOOTH_SCO_INCALL--\"p-6\",\nMIC_CAPTURE--\"p-10\",\nEARPIECE_RINGTONE--\"p-11\",\
			\nSPEAKER_RINGTONE--\"p-12\",\nHEADSET_RINGTONE--\"p-13\"\n");			
		break;
	case 'S':
	case 's':
		printk("Debug : Set volume begin\n");
		switch(cookie_pot[1])
		{
			case '+':
				if(cookie_pot[2] == '\n')
				{
				
				}
				else
				{
					value = simple_strtol(&cookie_pot[2],NULL,10);
					printk("value = %d\n",value);

				}
				break;
			case '-':
				if(cookie_pot[2] == '\n')
				{
					
				}
				else
				{
					value = simple_strtol(&cookie_pot[2],NULL,10);
					printk("value = %d\n",value);
				}
				break;
			default:
				if(cookie_pot[1] == '=')
				{
					value = simple_strtol(&cookie_pot[2],NULL,10);
					printk("value = %d\n",value);
				}	
				else
					printk("Help the set volume,Example: echo s+**>wm8994_ts,s=**>wm8994_ts,s-**>wm8994_ts\n");

				break;				
		}		
		break;	
	case '1':
		gpio_request(pdata->Power_EN_Pin, NULL);
		gpio_direction_output(pdata->Power_EN_Pin,GPIO_LOW);
		gpio_free(pdata->Power_EN_Pin);	
		break;
	case '2':	
		gpio_request(pdata->Power_EN_Pin, NULL);
		gpio_direction_output(pdata->Power_EN_Pin,GPIO_HIGH);
		gpio_free(pdata->Power_EN_Pin);			
		break;
	default:
		DBG("Help for wm8994_ts .\n-->The Cmd list: \n");
		DBG("-->'d&&D' Open or close the debug\n");
		DBG("-->'r&&R' Read reg debug,Example: echo 'r:22,23,24,25'>wm8994_ts\n");
		DBG("-->'w&&W' Write reg debug,Example: echo 'w:22=0,23=0,24=0,25=0'>wm8994_ts\n");
		DBG("-->'ph&&Ph' cat snd_soc_put_route list\n");
		break;
	}

	return len;
}

static const struct file_operations wm8994_proc_fops = {
	.owner		= THIS_MODULE,
	//.open		= snd_mem_proc_open,
	//.read		= seq_read,
//#ifdef CONFIG_PCI
	.write		= wm8994_proc_write,
//#endif
	//.llseek	= seq_lseek,
	//.release	= single_release,
};

static int wm8994_proc_init(void){

	struct proc_dir_entry *wm8994_proc_entry;

	wm8994_proc_entry = create_proc_entry("driver/wm8994_ts", 0777, NULL);

	if(wm8994_proc_entry != NULL){

		wm8994_proc_entry->write_proc = wm8994_proc_write;

		return -1;
	}else{
		printk("create proc error !\n");
	}

	return 0;
}

#endif

static int wm8994_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	struct wm8994_priv *wm8994;
	struct wm8994_pdata *pdata;
	int ret = 0;
	

#ifdef WM8994_PROC
	wm8994_proc_init();
#endif

	if (wm8994_codec == NULL) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = wm8994_codec;
	codec = wm8994_codec;
	wm8994 = codec->private_data;
	pdata = wm8994->pdata;
	//disable power_EN
	gpio_request(pdata->Power_EN_Pin, NULL);			 
	gpio_direction_output(pdata->Power_EN_Pin,GPIO_LOW); 		
	gpio_free(pdata->Power_EN_Pin);	
	
	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms: %d\n", ret);
		goto pcm_err;
	}

	snd_soc_add_controls(codec,wm8994_snd_controls,
				ARRAY_SIZE(wm8994_snd_controls));

	ret = snd_soc_init_card(socdev);
	if (ret < 0) {
		dev_err(codec->dev, "failed to register card: %d\n", ret);
		goto card_err;
	}

	PA_ctrl(GPIO_LOW);
	//enable power_EN
	msleep(50);
	gpio_request(pdata->Power_EN_Pin, NULL);			 
	gpio_direction_output(pdata->Power_EN_Pin,GPIO_HIGH); 		
	gpio_free(pdata->Power_EN_Pin);	

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
//	codec->reg_cache_size = ARRAY_SIZE(wm8994->reg_cache);
//	codec->reg_cache = &wm8994->reg_cache;
	codec->bias_level = SND_SOC_BIAS_OFF;
	codec->set_bias_level = wm8994_set_bias_level;

//	memcpy(codec->reg_cache, wm8994_reg,
//	       sizeof(wm8994_reg));

	ret = snd_soc_codec_set_cache_io(codec,7, 9, control);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		goto err;
	}

	ret = 0;
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

	wm8994 = kzalloc(sizeof(struct wm8994_priv), GFP_KERNEL);
	if (wm8994 == NULL)
		return -ENOMEM;

	codec = &wm8994->codec;

	i2c_set_clientdata(i2c, wm8994);
	codec->control_data = i2c;

	codec->dev = &i2c->dev;
	wm8994->pdata = i2c->dev.platform_data;//add
	wm8994->RW_status = TRUE;//add
	wm8994->capture_active = 0;
	wm8994->playback_active = 0;
	wm8994->call_vol = call_maxvol;
	wm8994->BT_call_vol = BT_call_maxvol;
	wm8994->route_status = POWER_ON;
	INIT_DELAYED_WORK(&wm8994->wm8994_delayed_work, wm8994_work_fun);
	mutex_init(&wm8994->io_lock);	
	mutex_init(&wm8994->route_lock);
	wake_lock_init(&wm8994->wm8994_on_wake, WAKE_LOCK_SUSPEND, "wm8994_on_wake");
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

static void wm8994_i2c_shutdown(struct i2c_client *client)
{
	struct wm8994_priv *wm8994 = wm8994_codec->private_data;
	struct wm8994_pdata *pdata = wm8994->pdata;
	DBG("%s----%d\n",__FUNCTION__,__LINE__);
	//disable PA
	PA_ctrl(GPIO_LOW);	
	gpio_request(pdata->Power_EN_Pin, NULL);
	gpio_direction_output(pdata->Power_EN_Pin,GPIO_LOW);
	gpio_free(pdata->Power_EN_Pin);	
}

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
	.shutdown = wm8994_i2c_shutdown,
};

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
