/*
 * rk610.c -- RK610 ALSA SoC audio driver
 *
 * Copyright (C) 2009 rockchip lhh
 *
 *
 * Based on RK610.c
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
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <mach/gpio.h>
#include <mach/iomux.h>

#include "rk610_codec.h"

#define RK610_SPK_CTRL_PIN  RK29_PIN6_PB6


/*
 * Debug
 */
#if 0
#define	DBG(x...)	printk(KERN_ERR x)
#else
#define	DBG(x...)
#endif

#define err(format, arg...) \
	printk(KERN_ERR AUDIO_NAME ": " format "\n" , ## arg)
#define info(format, arg...) \
	printk(KERN_INFO AUDIO_NAME ": " format "\n" , ## arg)
	
#define OUT_CAPLESS  (0)   //是否为无电容输出，1:无电容输出，0:有电容输出	

static u32 gVolReg = 0x00;  ///0x0f; //用于记录音量寄存器
//static u32 gCodecVol = 0x0f;
static u8 gR0AReg = 0;  //用于记录R0A寄存器的值，用于改变采样率前通过R0A停止clk
static u8 gR0BReg = 0;  //用于记录R0B寄存器的值，用于改变采样率前通过R0B停止interplate和decimation
//static u8 gR1314Reg = 0;  //用于记录R13,R14寄存器的值，用于FM音量为0时

/*
 * rk610 register cache
 * We can't read the RK610 register space when we
 * are using 2 wire for device control, so we cache them instead.
 */
static const u16 rk610_codec_reg[] = {
	0x0005, 0x0004, 0x00fd, 0x00f3,  /*  0 */
	0x0003, 0x0000, 0x0000, 0x0000,  /*  4 */
	0x0000, 0x0005, 0x0000, 0x0000,  /*  8 */
	0x0097, 0x0097, 0x0097, 0x0097,  /* 12 */
	0x0097, 0x0097, 0x00cc, 0x0000,  /* 16 */
	0x0000, 0x00f1, 0x0090, 0x00ff,  /* 20 */
	0x00ff, 0x00ff, 0x009c, 0x0000,  /* 24 */
	0x0000, 0x00ff, 0x00ff, 0x00ff,  /* 28 */
};

static struct snd_soc_codec *rk610_codec_codec;
/* codec private data */
struct rk610_codec_priv {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	enum snd_soc_control_type control_type;
#endif
	unsigned int sysclk;
	struct snd_soc_codec codec;
	struct snd_pcm_hw_constraint_list *sysclk_constraints;
	u16 reg_cache[RK610_CODEC_NUM_REG];
};

extern int rk610_control_init_codec(void);
extern int rk610_codec_pll_set(unsigned int rate);

/*
 * read rk610 register cache
 */
static inline unsigned int rk610_codec_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg > RK610_CACHE_REGNUM)
		return -1;
	return cache[reg];
}

static unsigned int rk610_codec_read(struct snd_soc_codec *codec, unsigned int r)
{	
	struct i2c_msg xfer[1];
	u8 reg = r;
	int ret;
	struct i2c_client *client = codec->control_data;

	/* Read register */
	xfer[0].addr = (client->addr& 0x60)|(reg);
	xfer[0].flags = I2C_M_RD;
	xfer[0].len = 1;
	xfer[0].buf = &reg;
	xfer[0].scl_rate = 100000;
	ret = i2c_transfer(client->adapter, xfer, 1);
	if (ret != 1) {
		dev_err(&client->dev, "i2c_transfer() returned %d\n", ret);
		return 0;
	}

	return reg;
}
/*
 * write rk610 register cache
 */
static inline void rk610_codec_write_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;
	if (reg > RK610_CACHE_REGNUM)
		return;
	cache[reg] = value;
}

static int rk610_codec_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	u8 data[2];
	struct i2c_client *i2c;
	DBG("Enter::%s, %d, reg=0x%02X, value=0x%02X\n",__FUNCTION__,__LINE__, reg, value);
	data[0] = value & 0x00ff;
	rk610_codec_write_reg_cache (codec, reg, value);
	i2c = (struct i2c_client *)codec->control_data;
	i2c->addr = (i2c->addr & 0x60)|reg;
	
	if (codec->hw_write(codec->control_data, data, 1) == 1){
//		DBG("================%s %d Run OK================\n",__FUNCTION__,__LINE__);
		return 0;
	}else{
		DBG("================%s %d Run EIO================\n",__FUNCTION__,__LINE__);
		return -EIO;
	}
}

void rk610_codec_reg_read(void)
{
    struct snd_soc_codec *codec = rk610_codec_codec;
    int i;
    unsigned int data;

    for (i=0; i<=0x1f; i++){
        data = rk610_codec_read(codec, i);
        DBG("reg[0x%x]=0x%x\n",i,data);
    }
}

static const struct snd_kcontrol_new rk610_codec_snd_controls[] = {
SOC_DOUBLE_R("Capture Volume", ACCELCODEC_R0C, ACCELCODEC_R0D, 0, 15, 0),
SOC_DOUBLE_R("Capture Switch", ACCELCODEC_R0C, ACCELCODEC_R0D, 7, 1, 1),
SOC_DOUBLE_R("PCM Volume", ACCELCODEC_R0D, ACCELCODEC_R0E, 0, 7, 0),
//SOC_SINGLE("Left ADC Capture Volume", ACCELCODEC_R17, 0, 63, 0),
//SOC_SINGLE("Right ADC Capture Volume", ACCELCODEC_R18, 0, 63, 0),
};

/* Left Mixer */
static const struct snd_kcontrol_new rk610_codec_left_mixer_controls[] = {
SOC_DAPM_SINGLE("Left Playback Switch", ACCELCODEC_R15, 6, 1, 0),
SOC_DAPM_SINGLE("Left Bypass Switch", ACCELCODEC_R15, 2, 1, 0),
};

/* Right Mixer */
static const struct snd_kcontrol_new rk610_codec_right_mixer_controls[] = {
SOC_DAPM_SINGLE("Right Playback Switch", ACCELCODEC_R15, 7, 1, 0),
SOC_DAPM_SINGLE("Right Bypass Switch", ACCELCODEC_R15, 3, 1, 0),
};

static const struct snd_soc_dapm_widget rk610_codec_dapm_widgets[] = {
	SND_SOC_DAPM_MIXER("Left Mixer", SND_SOC_NOPM, 0, 0,
		&rk610_codec_left_mixer_controls[0],
		ARRAY_SIZE(rk610_codec_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right Mixer", SND_SOC_NOPM, 0, 0,
		&rk610_codec_right_mixer_controls[0],
		ARRAY_SIZE(rk610_codec_right_mixer_controls)),
    
	//SND_SOC_DAPM_PGA("Right Out 1", ACCELCODEC_R1E, 0, 0, NULL, 0),
	//SND_SOC_DAPM_PGA("Left Out 1", ACCELCODEC_R1E, 1, 0, NULL, 0),
	//SND_SOC_DAPM_DAC("Right DAC", "Right Playback", ACCELCODEC_R1F, 1, 0),
	//SND_SOC_DAPM_DAC("Left DAC", "Left Playback", ACCELCODEC_R1F, 2, 0),
    
	SND_SOC_DAPM_ADC("ADC", "Capture", ACCELCODEC_R1D, 6, 1),
	SND_SOC_DAPM_ADC("ADC BUFF", "Capture BUFF", ACCELCODEC_R1D, 2, 0),
    
     
	SND_SOC_DAPM_OUTPUT("LOUT1"),
	SND_SOC_DAPM_OUTPUT("ROUT1"),
    
	SND_SOC_DAPM_INPUT("LINPUT1"),
	SND_SOC_DAPM_INPUT("RINPUT1"),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* left mixer */
	{"Left Mixer", "Left Playback Switch", "Left DAC"},
	{"Left Mixer", "Left Bypass Switch", "Left Line Mux"},
	{"Right Mixer", "Right Playback Switch", "Right DAC"},
	{"Right Mixer", "Right Bypass Switch", "Right Line Mux"},
    
	/* left out 1 */
	{"Left Out 1", NULL, "Left Mixer"},
	{"LOUT1", NULL, "Left Out 1"},
    
    
	/* right out 1 */
	{"Right Out 1", NULL, "Right Mixer"},
	{"ROUT1", NULL, "Right Out 1"},
    
	/* Left Line Mux */
	{"Left Line Mux", "Line 1", "LINPUT1"},
	{"Left Line Mux", "PGA", "Left PGA Mux"},
	{"Left Line Mux", "Differential", "Differential Mux"},
    
	/* Right Line Mux */
	{"Right Line Mux", "Line 1", "RINPUT1"},
	{"Right Line Mux", "PGA", "Right PGA Mux"},
	{"Right Line Mux", "Differential", "Differential Mux"},
    
	/* Left PGA Mux */
	{"Left PGA Mux", "Line 1", "LINPUT1"},
	{"Left PGA Mux", "Line 2", "LINPUT2"},
	{"Left PGA Mux", "Line 3", "LINPUT3"},
	{"Left PGA Mux", "Differential", "Differential Mux"},
    
	/* Right PGA Mux */
	{"Right PGA Mux", "Line 1", "RINPUT1"},
	{"Right PGA Mux", "Differential", "Differential Mux"},
    
	/* Differential Mux */
	{"Differential Mux", "Line 1", "LINPUT1"},
	{"Differential Mux", "Line 1", "RINPUT1"},
    
	/* Left ADC Mux */
	{"Left ADC Mux", "Stereo", "Left PGA Mux"},
	{"Left ADC Mux", "Mono (Left)", "Left PGA Mux"},
	{"Left ADC Mux", "Digital Mono", "Left PGA Mux"},
    
	/* Right ADC Mux */
	{"Right ADC Mux", "Stereo", "Right PGA Mux"},
	{"Right ADC Mux", "Mono (Right)", "Right PGA Mux"},
	{"Right ADC Mux", "Digital Mono", "Right PGA Mux"},
    
	/* ADC */
	{"Left ADC", NULL, "Left ADC Mux"},
	{"Right ADC", NULL, "Right ADC Mux"},
    
	/* terminator */
//	{NULL, NULL, NULL},
};

struct _coeff_div {
	u32 mclk;
	u32 rate;
	u16 fs;
	u8 sr:5;
	u8 usb:1;
	u8 bclk;
};

/* codec hifi mclk clock divider coefficients */
static const struct _coeff_div coeff_div[] = {
	/* 8k */
	{12288000, 8000, 1536, 0x6, 0x0,ASC_BCLKDIV_16},
	{11289600, 8000, 1408, 0x16, 0x0,ASC_BCLKDIV_16},
	{18432000, 8000, 2304, 0x7, 0x0,ASC_BCLKDIV_16},
	{16934400, 8000, 2112, 0x17, 0x0,ASC_BCLKDIV_16},
	{8192000, 8000, 1024, 0x0, 0x0,ASC_BCLKDIV_16},
	{12000000, 8000, 1500, 0x6, 0x1,ASC_BCLKDIV_16},
    
	/* 11.025k */
	{11289600, 11025, 1024, 0x18, 0x0,ASC_BCLKDIV_16},
	{16934400, 11025, 1536, 0x19, 0x0,ASC_BCLKDIV_16},
	{12000000, 11025, 1088, 0x19, 0x1,ASC_BCLKDIV_16},
    
    /* 12k */
	{12288000, 12000, 1024, 0x8, 0x0,ASC_BCLKDIV_16},
	{18432000, 12000, 1536, 0x9, 0x0,ASC_BCLKDIV_16},
	{12000000, 12000, 1000, 0x8, 0x1,ASC_BCLKDIV_16},
    
	/* 16k */
	{12288000, 16000, 768, 0xa, 0x0,ASC_BCLKDIV_8},
	{18432000, 16000, 1152, 0xb, 0x0,ASC_BCLKDIV_8},
	{12000000, 16000, 750, 0xa, 0x1,ASC_BCLKDIV_8},
    
	/* 22.05k */
	{11289600, 22050, 512, 0x1a, 0x0,ASC_BCLKDIV_8},
	{16934400, 22050, 768, 0x1b, 0x0,ASC_BCLKDIV_8},
	{12000000, 22050, 544, 0x1b, 0x1,ASC_BCLKDIV_8},
    
    /* 24k */
	{12288000, 24000, 512, 0x1c, 0x0,ASC_BCLKDIV_8},
	{18432000, 24000, 768, 0x1d, 0x0,ASC_BCLKDIV_8},
	{12000000, 24000, 500, 0x1c, 0x1,ASC_BCLKDIV_8},
	
	/* 32k */
	{12288000, 32000, 384, 0xc, 0x0,ASC_BCLKDIV_8},
	{18432000, 32000, 576, 0xd, 0x0,ASC_BCLKDIV_8},
	{12000000, 32000, 375, 0xa, 0x1,ASC_BCLKDIV_8},
    
	/* 44.1k */
	{11289600, 44100, 256, 0x10, 0x0,ASC_BCLKDIV_4},
	{16934400, 44100, 384, 0x11, 0x0,ASC_BCLKDIV_8},
	{12000000, 44100, 272, 0x11, 0x1,ASC_BCLKDIV_8},
    
	/* 48k */
	{12288000, 48000, 256, 0x0, 0x0,ASC_BCLKDIV_4},
	{18432000, 48000, 384, 0x1, 0x0,ASC_BCLKDIV_4},
	{12000000, 48000, 250, 0x0, 0x1,ASC_BCLKDIV_4},
    
	/* 88.2k */
	{11289600, 88200, 128, 0x1e, 0x0,ASC_BCLKDIV_4},
	{16934400, 88200, 192, 0x1f, 0x0,ASC_BCLKDIV_4},
	{12000000, 88200, 136, 0x1f, 0x1,ASC_BCLKDIV_4},
    
	/* 96k */
	{12288000, 96000, 128, 0xe, 0x0,ASC_BCLKDIV_4},
	{18432000, 96000, 192, 0xf, 0x0,ASC_BCLKDIV_4},
	{12000000, 96000, 125, 0xe, 0x1,ASC_BCLKDIV_4},
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
static int rk610_codec_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	struct rk610_codec_priv *rk610_codec =snd_soc_codec_get_drvdata(codec);
#else
	struct rk610_codec_priv *rk610_codec = codec->private_data;
#endif	
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	
//	if(rk610_codec_pll_set(freq))
//		return -EINVAL;
		
	switch (freq) {
	case 11289600:
	case 18432000:
	case 22579200:
	case 36864000:
		rk610_codec->sysclk_constraints = &constraints_112896;
		rk610_codec->sysclk = freq;
		break;

	case 12288000:
	case 16934400:
	case 24576000:
	case 33868800:
		rk610_codec->sysclk_constraints = &constraints_12288;
		rk610_codec->sysclk = freq;
		break;

	case 12000000:
	case 24000000:
		rk610_codec->sysclk_constraints = &constraints_12;
		rk610_codec->sysclk = freq;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int rk610_codec_set_dai_fmt(struct snd_soc_dai *codec_dai,
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
		iface = 0x0000;
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

	DBG("Enter::%s----%d  iface=%x\n",__FUNCTION__,__LINE__,iface);
	rk610_codec_write(codec, ACCELCODEC_R09, iface);
	return 0;
}

static int rk610_codec_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	struct snd_soc_codec *codec = rtd->codec;
	struct rk610_codec_priv *rk610_codec =snd_soc_codec_get_drvdata(codec);
#else
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct rk610_codec_priv *rk610_codec = codec->private_data;
#endif
	u16 iface = rk610_codec_read_reg_cache(codec, ACCELCODEC_R09) & 0x1f3;
	u16 srate = rk610_codec_read_reg_cache(codec, ACCELCODEC_R00) & 0x180;
	int coeff;
	
	/*by Vincent Hsiung for EQ Vol Change*/
	#define HW_PARAMS_FLAG_EQVOL_ON 0x21
	#define HW_PARAMS_FLAG_EQVOL_OFF 0x22
	if (params->flags == HW_PARAMS_FLAG_EQVOL_ON)
	{
		u16 r17 = rk610_codec_read_reg_cache(codec, ACCELCODEC_R17);
		u16 r18 = rk610_codec_read_reg_cache(codec, ACCELCODEC_R18);
		
		r17 &= (~0x3f); //6db
		r18 &= (~0x3f); //6db
		
		rk610_codec_write(codec, ACCELCODEC_R17, r17);
		rk610_codec_write(codec, ACCELCODEC_R18, r18);
		
		return 0;
	}
	else if (params->flags == HW_PARAMS_FLAG_EQVOL_OFF)
	{
		u16 r17 = rk610_codec_read_reg_cache(codec, ACCELCODEC_R17);
		u16 r18 = rk610_codec_read_reg_cache(codec, ACCELCODEC_R18);
		
		r17 &= (~0x3f); 
		r17 |= 0x0f; //0db
		
		r18 &= (~0x3f); 
		r18 |= 0x0f; //0db
		
		rk610_codec_write(codec, ACCELCODEC_R17, r17);
		rk610_codec_write(codec, ACCELCODEC_R18, r18);
		return 0;
	} 
	
	coeff = get_coeff(rk610_codec->sysclk, params_rate(params));
	DBG("Enter::%s----%d  rk610_codec->sysclk=%d coeff = %d\n",__FUNCTION__,__LINE__,rk610_codec->sysclk, coeff); 
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
	DBG("Enter::%s----%d  iface=%x srate =%x rate=%d\n",__FUNCTION__,__LINE__,iface,srate,params_rate(params));
	
//	rk610_codec_write(codec,ACCELCODEC_R0C, 0x17);  
	rk610_codec_write(codec,ACCELCODEC_R04, ASC_INT_MUTE_L|ASC_INT_MUTE_R|ASC_SIDETONE_L_OFF|ASC_SIDETONE_R_OFF);   //soft mute
	//必须先将clk和EN_INT都disable掉，否则切换bclk分频值可能导致codec内部时序混乱掉，
	//表现出来的现象是，以后的音乐都变成了噪音，而且就算把输入codec的I2S_DATAOUT断开也一样出噪音
	rk610_codec_write(codec,ACCELCODEC_R0B, ASC_DEC_DISABLE|ASC_INT_DISABLE);  //0x00
	
	/* set iface & srate */
	#ifdef CONFIG_SND_RK29_CODEC_SOC_MASTER
	iface |= ASC_INVERT_BCLK;//翻转BCLK  master状态送出的少了半个时钟，导致未到最大音量的时候破音、
	#endif
	rk610_codec_write(codec, ACCELCODEC_R09, iface);
	if (coeff >= 0){
	    rk610_codec_write(codec, ACCELCODEC_R00, srate|coeff_div[coeff].bclk);
		rk610_codec_write(codec, ACCELCODEC_R0A, (coeff_div[coeff].sr << 1) | coeff_div[coeff].usb|ASC_CLKNODIV|ASC_CLK_ENABLE);
	}
	rk610_codec_write(codec,ACCELCODEC_R0B, gR0BReg);

	return 0;
}

void PhaseOut(struct snd_soc_codec *codec,u32 nStep, u32 us)
{
    DBG("%s[%d]\n",__FUNCTION__,__LINE__); 
    rk610_codec_write(codec,ACCELCODEC_R17, gVolReg|ASC_OUTPUT_ACTIVE|ASC_CROSSZERO_EN);  //AOL
    rk610_codec_write(codec,ACCELCODEC_R18, gVolReg|ASC_OUTPUT_ACTIVE|ASC_CROSSZERO_EN);  //AOR
    udelay(us);
}

void PhaseIn(struct snd_soc_codec *codec,u32 nStep, u32 us)
{
    DBG("%s[%d]\n",__FUNCTION__,__LINE__); 
    rk610_codec_write(codec,ACCELCODEC_R17, gVolReg|ASC_OUTPUT_ACTIVE|ASC_CROSSZERO_EN);  //AOL gVolReg|ASC_OUTPUT_ACTIVE|ASC_CROSSZERO_EN);  //AOL
    rk610_codec_write(codec,ACCELCODEC_R18, gVolReg|ASC_OUTPUT_ACTIVE|ASC_CROSSZERO_EN); //gVolReg|ASC_OUTPUT_ACTIVE|ASC_CROSSZERO_EN);  //AOR
    udelay(us);
}

static int rk610_codec_mute(struct snd_soc_dai *dai, int mute)
{
    struct snd_soc_codec *codec = dai->codec;
	
    DBG("Enter::%s----%d--mute=%d\n",__FUNCTION__,__LINE__,mute);

    if (mute)
	{
        PhaseOut(codec,1, 5000);
        rk610_codec_write(codec,ACCELCODEC_R19, 0xFF);  //AOM
        rk610_codec_write(codec,ACCELCODEC_R04, ASC_INT_MUTE_L|ASC_INT_MUTE_R|ASC_SIDETONE_L_OFF|ASC_SIDETONE_R_OFF);  //soft mute  
	//add for standby
		if(!dai->capture_active)
		{
			rk610_codec_write(codec, ACCELCODEC_R1D, 0xFE);
			rk610_codec_write(codec, ACCELCODEC_R1E, 0xFF);
			rk610_codec_write(codec, ACCELCODEC_R1F, 0xFF);	
		}
    }
	else
	{		
        rk610_codec_write(codec,ACCELCODEC_R1D, 0x2a);  //setup Vmid and Vref, other module power down
        rk610_codec_write(codec,ACCELCODEC_R1E, 0x40);  ///|ASC_PDASDML_ENABLE);
        
        #if OUT_CAPLESS
    	rk610_codec_write(codec,ACCELCODEC_R1F, 0x09|ASC_PDMIXM_ENABLE);
    	#else
    	rk610_codec_write(codec,ACCELCODEC_R1F, 0x09|ASC_PDMIXM_ENABLE|ASC_PDPAM_ENABLE);
		#endif

        PhaseIn(codec,1, 5000);
        //if(gCodecVol != 0)
		//{
        rk610_codec_write(codec,ACCELCODEC_R04, ASC_INT_ACTIVE_L|ASC_INT_ACTIVE_R|ASC_SIDETONE_L_OFF|ASC_SIDETONE_R_OFF);
        //}
       rk610_codec_write(codec,ACCELCODEC_R19, 0x7F);  //AOM
#if 0
        /*disable speaker */
        gpio_set_value(RK29_PIN6_PB6, GPIO_LOW);
#endif
	rk610_codec_reg_read();
    }
	
    return 0;
}

static int rk610_codec_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	DBG("Enter::%s----%d level =%d\n",__FUNCTION__,__LINE__,level);
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		/* VREF, VMID=2x50k, digital enabled */
	//	rk610_codec_write(codec, ACCELCODEC_R1D, pwr_reg | 0x0080);
		break;

	case SND_SOC_BIAS_STANDBY:
		printk("rk610 standby\n");
		rk610_codec_write(codec, ACCELCODEC_R1D, 0xFE);
		rk610_codec_write(codec, ACCELCODEC_R1E, 0xFF);
		rk610_codec_write(codec, ACCELCODEC_R1F, 0xFF);		
		break;

	case SND_SOC_BIAS_OFF:
		printk("rk610 power off\n");	
		rk610_codec_write(codec, ACCELCODEC_R1D, 0xFF);
		rk610_codec_write(codec, ACCELCODEC_R1E, 0xFF);
		rk610_codec_write(codec, ACCELCODEC_R1F, 0xFF);
		break;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	codec->dapm.bias_level = level;
#else
	codec->bias_level = level;
#endif
	return 0;
}

#define RK610_CODEC_RATES SNDRV_PCM_RATE_8000_96000

#define RK610_CODEC_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops rk610_codec_ops = {
	.hw_params = rk610_codec_pcm_hw_params,
	.set_fmt = rk610_codec_set_dai_fmt,
	.set_sysclk = rk610_codec_set_dai_sysclk,
	.digital_mute = rk610_codec_mute,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static struct snd_soc_dai_driver rk610_codec_dai = 
#else
struct snd_soc_dai rk610_codec_dai = 
#endif
{
	.name = "rk610_codec_xx",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = RK610_CODEC_RATES,
		.formats = RK610_CODEC_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = RK610_CODEC_RATES,
		.formats = RK610_CODEC_FORMATS,
	 },
	.ops = &rk610_codec_ops,
	.symmetric_rates = 1,
};
EXPORT_SYMBOL_GPL(rk610_codec_dai);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static int rk610_codec_suspend(struct snd_soc_codec *codec, pm_message_t state)
#else
static int rk610_codec_suspend(struct platform_device *pdev, pm_message_t state)
#endif
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))	
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
#endif
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	rk610_codec_set_bias_level(codec, SND_SOC_BIAS_OFF);
	rk610_codec_reg_read();

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static int rk610_codec_resume(struct snd_soc_codec *codec)
#else
static int rk610_codec_resume(struct platform_device *pdev)
#endif
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
#endif
	int i;
	u8 data[2];
	struct i2c_client *i2c;
	u16 *cache = codec->reg_cache;
	
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	/* Sync reg_cache with the hardware */
	for (i = 0; i < RK610_CODEC_NUM_REG; i++) {
		data[0] = cache[i] & 0x00ff;
		i2c = (struct i2c_client *)codec->control_data;
		i2c->addr = (i2c->addr & 0x60)|i;
		codec->hw_write(codec->control_data, data, 1);
	}

	rk610_codec_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}

#if 1
#define USE_MIC_IN
#define USE_LPF		1
void rk610_codec_reg_set(void)
{
    struct snd_soc_codec *codec = rk610_codec_codec;
    unsigned int digital_gain;
	
	rk610_codec_write(codec,ACCELCODEC_R1D, 0xFF);
    rk610_codec_write(codec,ACCELCODEC_R1E, 0xFF);
    rk610_codec_write(codec,ACCELCODEC_R1F, 0xFF);

#if USE_LPF
	// Route R-LPF->R-Mixer, L-LPF->L-Mixer
	rk610_codec_write(codec,ACCELCODEC_R15, 0xC1);
#else
	// Route RDAC->R-Mixer, LDAC->L->Mixer
	rk610_codec_write(codec,ACCELCODEC_R15, 0x0C);
#endif
	// With Cap Output, VMID ramp up slow
	rk610_codec_write(codec,ACCELCODEC_R1A, 0x14);
    mdelay(10);

	rk610_codec_write(codec,ACCELCODEC_R0C, 0x10|ASC_INPUT_VOL_0DB);   //LIL
    rk610_codec_write(codec,ACCELCODEC_R0D, 0x10|ASC_INPUT_VOL_0DB);   //LIR
    rk610_codec_write(codec,ACCELCODEC_R0E, 0x10|ASC_INPUT_VOL_0DB);   //MIC
#ifdef USE_MIC_IN
    rk610_codec_write(codec,ACCELCODEC_R12, 0x4c|ASC_MIC_INPUT|ASC_MIC_BOOST_20DB);   //Select MIC input
    rk610_codec_write(codec,ACCELCODEC_R1C, ASC_DEM_ENABLE);  //0x00);  //use default value
#else
    rk610_codec_write(codec,ACCELCODEC_R12, 0x4c);   //Select Line input
#endif
	// Diable route PGA->R/L Mixer, PGA gain 0db.
    rk610_codec_write(codec,ACCELCODEC_R13, 0x05 | 0 << 3);
    rk610_codec_write(codec,ACCELCODEC_R14, 0x05 | 0 << 3);

    //2soft mute
    rk610_codec_write(codec,ACCELCODEC_R04, ASC_INT_MUTE_L|ASC_INT_MUTE_R|ASC_SIDETONE_L_OFF|ASC_SIDETONE_R_OFF);   //soft mute
    
    //2set default SR and clk
    rk610_codec_write(codec,ACCELCODEC_R0A, ASC_NORMAL_MODE|(0x10 << 1)|ASC_CLKNODIV|ASC_CLK_DISABLE);
    gR0AReg = ASC_NORMAL_MODE|(0x10 << 1)|ASC_CLKNODIV|ASC_CLK_DISABLE;
    //2Config audio  interface
    rk610_codec_write(codec,ACCELCODEC_R09, ASC_I2S_MODE|ASC_16BIT_MODE|ASC_NORMAL_LRCLK|ASC_LRSWAP_DISABLE|ASC_MASTER_MODE|ASC_NORMAL_BCLK);
    rk610_codec_write(codec,ACCELCODEC_R00, ASC_HPF_ENABLE|ASC_DSM_MODE_DISABLE|ASC_SCRAMBLE_DISABLE|ASC_DITHER_ENABLE|ASC_BCLKDIV_4);
    //2volume,input,output
    digital_gain = 0xE42;
    rk610_codec_write(codec,ACCELCODEC_R05, (digital_gain >> 8) & 0xFF);
    rk610_codec_write(codec,ACCELCODEC_R06, digital_gain & 0xFF);
    rk610_codec_write(codec,ACCELCODEC_R07, (digital_gain >> 8) & 0xFF);
    rk610_codec_write(codec,ACCELCODEC_R08, digital_gain & 0xFF);
//    rk610_codec_write(codec,ACCELCODEC_R05, 0x0e);
//    rk610_codec_write(codec,ACCELCODEC_R06, 0x42);
//    rk610_codec_write(codec,ACCELCODEC_R07, 0x0e);
//    rk610_codec_write(codec,ACCELCODEC_R08, 0x42);
    
    rk610_codec_write(codec,ACCELCODEC_R0B, ASC_DEC_ENABLE|ASC_INT_ENABLE);
    gR0BReg = ASC_DEC_ENABLE|ASC_INT_ENABLE;  //ASC_DEC_DISABLE|ASC_INT_ENABLE;
    
    rk610_codec_write(codec,ACCELCODEC_R1D, 0x30);
    rk610_codec_write(codec,ACCELCODEC_R1E, 0x40);
    #if OUT_CAPLESS
    rk610_codec_write(codec,ACCELCODEC_R1F, 0x09|ASC_PDMIXM_ENABLE);
    #else
    rk610_codec_write(codec,ACCELCODEC_R1F, 0x09|ASC_PDMIXM_ENABLE|ASC_PDPAM_ENABLE);
	#endif
}
#else
void rk610_codec_reg_set(void)
{
    struct snd_soc_codec *codec = rk610_codec_codec;
    int reg;
    int i;
    unsigned int data;

    rk610_codec_write(codec,ACCELCODEC_R1D, 0x00);
    rk610_codec_write(codec,ACCELCODEC_R17, 0xFF);  //AOL
    rk610_codec_write(codec,ACCELCODEC_R18, 0xFF);  //AOR
    rk610_codec_write(codec,ACCELCODEC_R19, 0xFF);  //AOM

    rk610_codec_write(codec,ACCELCODEC_R1F, 0xDF);
    mdelay(10);
    rk610_codec_write(codec,ACCELCODEC_R1F, 0x5F);
    rk610_codec_write(codec,ACCELCODEC_R19, 0x7F);  //AOM
    rk610_codec_write(codec,ACCELCODEC_R15, 0xC1);//rk610_codec_write(codec,ACCELCODEC_R15, 0xCD);//by Vincent Hsiung
    rk610_codec_write(codec,ACCELCODEC_R1A, 0x1C);
    mdelay(100);
    rk610_codec_write(codec,ACCELCODEC_R1F, 0x09);
    rk610_codec_write(codec,ACCELCODEC_R1E, 0x00);
    mdelay(10);
    rk610_codec_write(codec,ACCELCODEC_R1A, 0x14);
    rk610_codec_write(codec,ACCELCODEC_R1D, 0xFE);
    rk610_codec_write(codec,ACCELCODEC_R17, 0xBF);  //AOL
    rk610_codec_write(codec,ACCELCODEC_R18, 0xBF);  //AOR
    rk610_codec_write(codec,ACCELCODEC_R19, 0x7F);  //AOM
    rk610_codec_write(codec,ACCELCODEC_R1F, 0xDF);

    //2soft mute
    rk610_codec_write(codec,ACCELCODEC_R04, ASC_INT_MUTE_L|ASC_INT_MUTE_R|ASC_SIDETONE_L_OFF|ASC_SIDETONE_R_OFF);   //soft mute
    
    //2set default SR and clk
    rk610_codec_write(codec,ACCELCODEC_R0A, ASC_USB_MODE|FREQ48kHz|ASC_CLKNODIV|ASC_CLK_DISABLE);
    gR0AReg = ASC_USB_MODE|FREQ48kHz|ASC_CLKNODIV|ASC_CLK_DISABLE;
    //2Config audio  interface
    rk610_codec_write(codec,ACCELCODEC_R09, ASC_I2S_MODE|ASC_16BIT_MODE|ASC_NORMAL_LRCLK|ASC_LRSWAP_DISABLE|ASC_MASTER_MODE|ASC_NORMAL_BCLK);
    rk610_codec_write(codec,ACCELCODEC_R00, ASC_HPF_ENABLE|ASC_DSM_MODE_DISABLE|ASC_SCRAMBLE_ENABLE|ASC_DITHER_ENABLE|ASC_BCLKDIV_8);  //BCLK div 8
    //2volume,input,outpu
    rk610_codec_write(codec,ACCELCODEC_R05, 0x0e);
    rk610_codec_write(codec,ACCELCODEC_R06, 0x42);
    rk610_codec_write(codec,ACCELCODEC_R07, 0x0e);
    rk610_codec_write(codec,ACCELCODEC_R08, 0x42);
    
    rk610_codec_write(codec,ACCELCODEC_R0C, 0x10|ASC_INPUT_VOL_0DB|ASC_INPUT_MUTE);   //LIL
    rk610_codec_write(codec,ACCELCODEC_R0D, 0x10|ASC_INPUT_VOL_0DB);   //LIR
    rk610_codec_write(codec,ACCELCODEC_R0E, 0x10|ASC_INPUT_VOL_0DB);   //MIC
    rk610_codec_write(codec,ACCELCODEC_R12, 0x4c|ASC_MIC_INPUT|ASC_MIC_BOOST_20DB);  //mic input and boost 20dB
    rk610_codec_write(codec,ACCELCODEC_R13, ASC_LPGAMX_DISABLE|ASC_ALMX_DISABLE|((LINE_2_MIXER_GAIN & 0x7) << 4)|0x0);
    rk610_codec_write(codec,ACCELCODEC_R14, ASC_RPGAMX_DISABLE|ASC_ARMX_DISABLE|((LINE_2_MIXER_GAIN & 0x7) << 4)|0x0);
    gR1314Reg = ASC_RPGAMX_DISABLE|ASC_ARMX_DISABLE|((LINE_2_MIXER_GAIN & 0x7) << 4)|0x0;

    //2other
    rk610_codec_write(codec,ACCELCODEC_R0B, ASC_DEC_DISABLE|ASC_INT_DISABLE);  //0x00
    gR0BReg = ASC_DEC_DISABLE|ASC_INT_DISABLE;
    rk610_codec_write(codec,ACCELCODEC_R15, \
                    0x01|ASC_RLPFMX_DISABLE|ASC_LLPFMX_DISABLE|ASC_LDAMX_DISABLE|ASC_RDAMX_DISABLE|ASC_LSCF_ACTIVE|ASC_RSCF_ACTIVE);  //0x3c
    rk610_codec_write(codec,ACCELCODEC_R1B, 0x32);
    rk610_codec_write(codec,ACCELCODEC_R1C, ASC_DEM_ENABLE);  ///0x00);  //use default value
    
    ///dac mode
    rk610_codec_write(codec,ACCELCODEC_R17, 0xBF);  //AOL  音量最低
    rk610_codec_write(codec,ACCELCODEC_R18, 0xBF);  //AOR
        
    //2power down useless module
    rk610_codec_write(codec,ACCELCODEC_R1D, 0x2a|ASC_PDSDL_ENABLE|ASC_PDBSTL_ENABLE|ASC_PDPGAL_ENABLE);  //setup Vmid and Vref, other module power down
    rk610_codec_write(codec,ACCELCODEC_R1E, 0x40|ASC_PDASDML_ENABLE);
    #if OUT_CAPLESS
    rk610_codec_write(codec,ACCELCODEC_R1F, 0x09|ASC_PDMICB_ENABLE|ASC_PDMIXM_ENABLE);
    #else
    rk610_codec_write(codec,ACCELCODEC_R1F, 0x09|ASC_PDMICB_ENABLE|ASC_PDMIXM_ENABLE|ASC_PDPAM_ENABLE);
    #endif

    //2other
    rk610_codec_write(codec,ACCELCODEC_R0B, ASC_DEC_DISABLE|ASC_INT_DISABLE);
    gR0BReg = ASC_DEC_ENABLE|ASC_INT_ENABLE;  //ASC_DEC_DISABLE|ASC_INT_ENABLE;
    rk610_codec_write(codec,ACCELCODEC_R15, 0xC1);//rk610_codec_write(codec,ACCELCODEC_R15, 0xCD);//by Vincent Hsiung
    rk610_codec_write(codec,ACCELCODEC_R0C, 0x10|ASC_INPUT_VOL_0DB);   //LIL
    rk610_codec_write(codec,ACCELCODEC_R0D, 0x10|ASC_INPUT_VOL_0DB);   //LIR
    rk610_codec_write(codec,ACCELCODEC_R0E, 0x10|ASC_INPUT_VOL_0DB);   //MIC
//    rk610_codec_write(codec,ACCELCODEC_R12, 0x4c|ASC_MIC_INPUT|ASC_MIC_BOOST_20DB);  //mic input and boost 20dB
    rk610_codec_write(codec,ACCELCODEC_R12, 0x4c);  //line input and boost 20dB
    rk610_codec_write(codec,ACCELCODEC_R13, 0x00);
    rk610_codec_write(codec,ACCELCODEC_R14, 0x00);
    gR1314Reg = 0x00;
    rk610_codec_write(codec,ACCELCODEC_R1C, ASC_DEM_ENABLE);  //0x00);  //use default value
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static int rk610_codec_probe(struct snd_soc_codec *codec)
{
	struct rk610_codec_priv *rk610_codec = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret;
	
	rk610_codec_codec = codec;
	printk(KERN_ERR "[%s] start\n", __FUNCTION__);
	ret = snd_soc_codec_set_cache_io(codec, 8, 16, rk610_codec->control_type);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}
	//For RK610, i2c write&read method is special, do not use system default method.
	codec->write = rk610_codec_write;
	codec->read = rk610_codec_read;
	codec->hw_write = (hw_write_t)i2c_master_send;
#else
static int rk610_codec_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret = 0;
#endif
	if (rk610_codec_codec == NULL) {
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
		dev_err(codec->dev, "Codec device not registered\n");
		#else
		dev_err(&pdev->dev, "Codec device not registered\n");
		#endif
		return -ENODEV;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
	socdev->card->codec = rk610_codec_codec;
	codec = rk610_codec_codec;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms: %d\n", ret);
		goto pcm_err;
	}
#endif

	snd_soc_add_controls(codec, rk610_codec_snd_controls,
				ARRAY_SIZE(rk610_codec_snd_controls));
				
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
	snd_soc_dapm_new_controls(codec, rk610_codec_dapm_widgets,
				  ARRAY_SIZE(rk610_codec_dapm_widgets));
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));
	snd_soc_dapm_new_widgets(codec);

	ret = snd_soc_init_card(socdev);
	if (ret < 0) {
		dev_err(codec->dev, "failed to register card: %d\n", ret);
		goto card_err;
	}
#else
	snd_soc_dapm_new_controls(dapm, rk610_codec_dapm_widgets,
				  ARRAY_SIZE(rk610_codec_dapm_widgets));
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));

#endif

#if defined(RK610_SPK_CTRL_PIN)
	ret = gpio_request(RK610_SPK_CTRL_PIN, "rk610 spk_ctrl");
    if (ret){   
        printk("rk610_control request gpio fail\n");
        //goto err1;
    }
    gpio_set_value(RK610_SPK_CTRL_PIN, GPIO_HIGH);
    gpio_direction_output(RK610_SPK_CTRL_PIN, GPIO_HIGH);
#endif

    rk610_control_init_codec();
    
    rk610_codec_reg_set();
	rk610_codec_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	
	return ret;
	
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
card_err:
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
pcm_err:
	return ret;
#endif
}

/* power down chip */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static int rk610_codec_remove(struct snd_soc_codec *codec)
{
	rk610_codec_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}
#else
static int rk610_codec_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
    printk("rk610_codec_remove\n");
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
	return 0;
}
#endif

static struct snd_soc_codec_driver soc_codec_dev_rk610_codec = {
	.probe =	rk610_codec_probe,
	.remove =	rk610_codec_remove,
	.suspend =	rk610_codec_suspend,
	.resume =	rk610_codec_resume,
	.set_bias_level = rk610_codec_set_bias_level,
//	.volatile_register = wm8900_volatile_register,
	.reg_cache_size = ARRAY_SIZE(rk610_codec_reg),
	.reg_word_size = sizeof(u16),
	.reg_cache_default = rk610_codec_reg,
	.dapm_widgets = rk610_codec_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rk610_codec_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

EXPORT_SYMBOL_GPL(soc_codec_dev_rk610_codec);


#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static int rk610_codec_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct rk610_codec_priv *rk610_codec;
	int ret;
	printk("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$ %s start $$$$$$$$$$$$$$$$$$$$$$$$$$$\n", __FUNCTION__);
	rk610_codec = kzalloc(sizeof(struct rk610_codec_priv), GFP_KERNEL);
	if (rk610_codec == NULL)
		return -ENOMEM;
		
	i2c_set_clientdata(i2c, rk610_codec);
	rk610_codec->control_type = SND_SOC_I2C;
	
	ret =  snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_rk610_codec, &rk610_codec_dai, 1);
	if (ret < 0) {
		printk("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ %s: snd_soc_register_codec error!!!!\n", __FUNCTION__);
		dev_err(&i2c->dev, "Failed to register codec: %d\n", ret);
		kfree(rk610_codec);
	}
	return ret;
}


static int rk610_codec_i2c_remove(struct i2c_client *client)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
#else
	struct rk610_codec_priv *rk610_codec = i2c_get_clientdata(client);
	rk610_codec_unregister(rk610_codec);
#endif
	return 0;
}

#ifdef CONFIG_PM
static int rk610_codec_i2c_suspend(struct i2c_client *client, pm_message_t msg)
{
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	return 0;
	#else
	return snd_soc_suspend_device(&client->dev);
	#endif
}

static int rk610_codec_i2c_resume(struct i2c_client *client)
{
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	return 0;
	#else
	return snd_soc_resume_device(&client->dev);
	#endif
}
#else
#define rk610_codec_i2c_suspend NULL
#define rk610_codec_i2c_resume NULL
#endif

static const struct i2c_device_id rk610_codec_i2c_id[] = {
	{ "rk610_i2c_codec", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rk610_codec_i2c_id);

/* corgi i2c codec control layer */
static struct i2c_driver rk610_codec_i2c_driver = {
	.driver = {
		.name = "RK610_CODEC",
		.owner = THIS_MODULE,
	},
	.probe = rk610_codec_i2c_probe,
	.remove = rk610_codec_i2c_remove,
	//.suspend = rk610_codec_i2c_suspend,
	//.resume = rk610_codec_i2c_resume,
	.id_table = rk610_codec_i2c_id,
};
#endif

static int __init rk610_codec_modinit(void)
{
	int ret;
//	DBG("[%s] start\n", __FUNCTION__);
	ret = i2c_add_driver(&rk610_codec_i2c_driver);
	if (ret != 0)
		pr_err("rk610 codec: Unable to register I2C driver: %d\n", ret);
	return ret;
}
module_init(rk610_codec_modinit);

static void __exit rk610_codec_exit(void)
{
	i2c_del_driver(&rk610_codec_i2c_driver);
}
module_exit(rk610_codec_exit);

MODULE_DESCRIPTION("ASoC RK610 CODEC driver");
MODULE_AUTHOR("rk@rock-chips.com");
MODULE_LICENSE("GPL");
