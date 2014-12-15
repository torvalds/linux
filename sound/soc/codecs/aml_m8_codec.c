/*
 * aml_m8_codec.c  --  AMLM8 ALSA SoC Audio driver
 *
 * Copyright 2013 AMLOGIC
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
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <mach/am_regs.h>

#include "aml_m8_codec.h"

#include <linux/of.h>

unsigned int acodec_regbank[252] = {0x00, 0x05, 0x00, 0x01, 0x7d, 0x02, 0x7d, 0x02, 0x01, 0x7d, // Reg   0 -   9
                                    0x02, 0x7d, 0x02, 0x01, 0x7d, 0x02, 0x7d, 0x02, 0x00, 0x00, // Reg  10 -  19
                                    0xce, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reg  20 -  29
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbf, 0xbf, 0x12, 0x12, // Reg  30 -  39
                                    0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reg  40 -  49
                                    0x00, 0x00, 0xe7, 0xe7, 0x00, 0x00, 0x0d, 0x0d, 0x00, 0x00, // Reg  50 -  59
                                    0x0d, 0x0d, 0x00, 0x00, 0x0d, 0x0d, 0x0d, 0x0d, 0x00, 0x00, // Reg  60 -  69
                                    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, // Reg  70 -  79
                                    0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01, // Reg  80 -  89
                                    0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, // Reg  90 -  99
                                    0x01, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reg 100 - 109
                                    0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reg 110 - 119
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reg 120 - 129
                                    0x3f, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x3f, 0x00, 0x00, // Reg 130 - 139
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reg 140 - 149
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reg 150 - 159
                                    0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reg 160 - 169
                                    0x00, 0x74, 0xff, 0x29, 0xff, 0x74, 0xff, 0x29, 0xff, 0x74, // Reg 170 - 179
                                    0xff, 0x29, 0xff, 0x74, 0xff, 0x29, 0xff, 0x00, 0x00, 0x00, // Reg 180 - 189
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01, // Reg 190 - 199
                                    0x00, 0x16, 0x00, 0x02, 0x02, 0xff, 0x00, 0x00, 0x00, 0x00, // Reg 200 - 209
                                    0x00, 0x04, 0x05, 0x01, 0x00, 0x55, 0x00, 0x00, 0x00, 0x00, // Reg 210 - 219
                                    0x28, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x40, 0x01, 0x00, 0x00, // Reg 220 - 229
                                    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reg 230 - 239
                                    0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf1, 0x00, // Reg 240 - 249
                                    0x00, 0x00                                                  // Reg 250 - 251
                                   };

extern unsigned audio_aiu_pg_enable(unsigned char enable);
struct snd_soc_codec *m8_codec = NULL;
void adac_wr_reg (unsigned long addr, unsigned long data)
{
    // Write high byte for 16-bit register
    if ((addr == 36) || (addr == 38) || (addr == 52) || (addr == 56) || (addr == 71) || (addr == 89) || (addr == 93) ||
        (addr == 128) || (addr == 130) || (addr == 136) || (addr == 138)) {
		WRITE_APB_REG((APB_BASE+(addr<<2)), data & 0xff);
        acodec_regbank[addr] = (data & 0xff);
        WRITE_APB_REG((APB_BASE+(addr<<2)),  ((data>>8) & 0xff) | (1<<31));  // Latch=1 for low byte
        acodec_regbank[addr+1] = ((data>>8) & 0xff);
    } else {
        WRITE_APB_REG((APB_BASE+(addr<<2)), (data & 0xff) | (1<<31));       // Latch=1 for single byte
        acodec_regbank[addr] = (data & 0xff);
    }
} /* adac_wr_reg */

unsigned  long adac_rd_reg (unsigned long addr)
{
    return (unsigned long )READ_APB_REG((APB_BASE +(addr<<2)));
} /* adac_rd_reg */

static bool aml_m8_is_16bit_reg(unsigned int reg)
{
	return 	(reg == 0x24) || (reg == 0x26) || //(reg == 0x34) ||
			/*(reg == 0x38) ||*/ (reg == 0x47) || //(reg == 0x59) ||
			(reg == 0x5d) || (reg == 0x80) || (reg == 0x82) ||
			(reg == 0x88) || (reg == 0x8a);
}
static int aml_m8_write(struct snd_soc_codec *codec, unsigned int reg,
							unsigned int value)
{
	if (reg > AMLM8_MAX_REG_NUM)
		return -EINVAL;
	
	if (aml_m8_is_16bit_reg(reg)){
		// Latch=0 for low byte
		WRITE_APB_REG((APB_BASE+(reg<<2)), value & 0xff);
		// Latch=1 for high byte
		WRITE_APB_REG((APB_BASE+(reg<<2)), ((value>>8) & 0xff) | (1<<31));
	}else{
		// Latch=1 for single byte
		WRITE_APB_REG((APB_BASE+(reg<<2)), (value & 0xff) | (1<<31));
	}

	return 0;
}

static unsigned int aml_m8_read(struct snd_soc_codec *codec,
							unsigned int reg)
{
	//printk("read reg %x,vaule  %x \n",reg,(unsigned)READ_APB_REG((APB_BASE +(reg<<2))));
	if (reg > AMLM8_MAX_REG_NUM)
		return -EINVAL;
	
	return READ_APB_REG((APB_BASE +(reg<<2)));
}
static int aml_m8_codec_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = 0;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface |= 0x08;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x01;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		iface |= 0x00;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x02;
		break;
	default:
		return -EINVAL;
	}

	/* set iface */
	snd_soc_write(codec, AMLM8_I2S1_CONFIG_0, iface);
	return 0;
}

static int aml_m8_codec_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	u16 iface = snd_soc_read(codec, AMLM8_I2S1_CONFIG_0) & 0xcf;

    /* bit size */
    switch (params_format(params)) {
    case SNDRV_PCM_FORMAT_S16_LE:
		iface |= 0x30;
        break;
    case SNDRV_PCM_FORMAT_S20_3LE:
        iface |= 0x10;
        break;
    case SNDRV_PCM_FORMAT_S24_LE:
        break;
	case SNDRV_PCM_FORMAT_S18_3LE:
        iface |= 0x20;
        break;
    }
	
	/* set iface */
	snd_soc_write(codec, AMLM8_I2S1_CONFIG_0, iface);

	return 0;
}

static int aml_m8_codec_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
    struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = snd_soc_read(codec, AMLM8_MUTE_2) & 0xfc;
    u16 mic_mute_reg = snd_soc_read(codec, AMLM8_MUTE_0) & 0xfc;
    printk(KERN_DEBUG "enter:%s, mute=%d, stream=%d \n",__func__,mute,stream);

    if(stream == SNDRV_PCM_STREAM_PLAYBACK){
    	if (mute){
    		//snd_soc_write(codec, AMLM8_MUTE_2, mute_reg | 0x3);
    	}else{
    		snd_soc_write(codec, AMLM8_MUTE_2, mute_reg);
    	}
    }
    if(stream == SNDRV_PCM_STREAM_CAPTURE){
        if (mute){
            snd_soc_write(codec, AMLM8_MUTE_0,mic_mute_reg | 0x3);
        }else{
            msleep(200);
            snd_soc_write(codec, AMLM8_MUTE_0,mic_mute_reg);
        }
            
    }
	return 0;
}
static int aml_m8_codec_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = snd_soc_read(codec, AMLM8_MUTE_2) & 0xfc;

	if (mute)
		snd_soc_write(codec, AMLM8_MUTE_2, mute_reg | 0x3);
	else
		snd_soc_write(codec, AMLM8_MUTE_2, mute_reg);
	return 0;
}
static int aml_m8_codec_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 reg = snd_soc_read(codec, AMLM8_CLK_EXT_SELECT) & 0xf0;
	switch (freq) {
	case 11289600:
	case 12288000:
		reg |= 0x05;
		break;
	case 22579200:
	case 24576000:
		reg |= 0x07;
		break;
	default:
		break;
	}
	
	snd_soc_write(codec, AMLM8_CLK_EXT_SELECT, reg);

	return 0;
}

static int aml_m8_codec_set_dai_pll(struct snd_soc_dai *dai, int pll_id, int source,
            unsigned int freq_in, unsigned int freq_out)
{
    return 0;
}

static int aml_m8_codec_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
		int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;

	switch (div_id) {
	case AML_M8_PLAY_LRCLK_DIV:
		snd_soc_write(codec, AMLM8_I2S1_CONFIG_1, div);
		break;
	case AML_M8_PLAY_SCLK_DIV:
		snd_soc_write(codec, AMLM8_I2S1_CONFIG_2, div);
		break;
	case AML_M8_REC_LRCLK_DIV:
		snd_soc_write(codec, AMLM8_I2S1_CONFIG_3, div);
		break;
	case AML_M8_REC_SCLK_DIV:
		snd_soc_write(codec, AMLM8_I2S1_CONFIG_4, div);
		break;
	default:
		return -EINVAL;
	}
	snd_soc_write(codec, AMLM8_RESET, 0);
	snd_soc_write(codec, AMLM8_RESET, 0x3);

	return 0;
}

static struct snd_soc_dai_ops aml_m8_codec_dai_ops = {
 	.hw_params	= aml_m8_codec_hw_params,
	.digital_mute = aml_m8_codec_mute,
	.mute_stream = aml_m8_codec_mute_stream,
	.set_sysclk	= aml_m8_codec_set_dai_sysclk,
	.set_clkdiv = aml_m8_codec_set_dai_clkdiv,
    .set_pll = aml_m8_codec_set_dai_pll,
	.set_fmt	= aml_m8_codec_set_dai_fmt,
};
#define AML_RATES SNDRV_PCM_RATE_8000_96000

#define AML_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
	SNDRV_PCM_FMTBIT_S24_LE|SNDRV_PCM_FMTBIT_S32_LE)

struct snd_soc_dai_driver aml_m8_codec_dai = {
	.name = "AML-M8",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 8,
		.rates = AML_RATES,
		.formats = AML_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = AML_RATES,
		.formats = AML_FORMATS,},
	.ops = &aml_m8_codec_dai_ops,
	.symmetric_rates = 1,
};
static int aml_m8_set_bias_level(struct snd_soc_codec *codec,
                 enum snd_soc_bias_level level)
{
    switch (level) {
    case SND_SOC_BIAS_ON:
        break;
    case SND_SOC_BIAS_PREPARE:
        switch (codec->dapm.bias_level) {
        case SND_SOC_BIAS_STANDBY:
	    // WRITE_MPEG_REG_BITS( HHI_MPLL_CNTL9, 1,14, 1);			
            adac_wr_reg(89,0x0101);
            snd_soc_write(codec,AMLM8_PD_3,snd_soc_read(codec,AMLM8_PD_3) | 0xf3);         
            //adac_wr_reg(24,adac_rd_reg(24) | 0x3); // power on the DAC
            break;

        default:
            break;
        }

        break;
    case SND_SOC_BIAS_STANDBY:
        switch (codec->dapm.bias_level) {
        case SND_SOC_BIAS_PREPARE:
	   //  WRITE_MPEG_REG_BITS( HHI_MPLL_CNTL9, 1,14, 1);
            adac_wr_reg(89,0x0000); 
           // adac_wr_reg(24,adac_rd_reg(24) & 0xfc); //power down the DAC
	    // WRITE_MPEG_REG_BITS( HHI_MPLL_CNTL9, 0,14, 1);
            break;

        default:
	    // WRITE_MPEG_REG_BITS( HHI_MPLL_CNTL9, 0,14, 1);			
            break;
        }

        break;
        
    case SND_SOC_BIAS_OFF:
	// WRITE_MPEG_REG_BITS( HHI_MPLL_CNTL9, 0,14, 1);
        break;
    default:
        break;
    }
    codec->dapm.bias_level = level;
    return 0;
}
static void acodec_delay_us (int us)
{
	msleep(us/1000);
} /* acodec_delay_us */

void acodec_normal_startup (struct snd_soc_codec *codec)
{
	unsigned int data32;
    // Apply reset for at least 3 clk_ext cycles
    WRITE_MPEG_REG_BITS(AIU_AUDAC_CTRL0, 1, 15, 1); 
    acodec_delay_us(20);
    WRITE_MPEG_REG_BITS(AIU_AUDAC_CTRL0, 0, 15, 1); 	
    
    acodec_delay_us(2);
    
    snd_soc_write(codec, 1, 5); // fs_clk_ext
    snd_soc_write(codec, 4, 64);
    snd_soc_write(codec, 5, 4);
    snd_soc_write(codec, 6, 64);
    snd_soc_write(codec, 7, 4);
    
	snd_soc_write(codec, 3, 1);//i2s1_mode
    data32  = 0;
    data32 |= 0 << 1;   // [    1] rstadcdpz
    data32 |= 0 << 0;   // [    0] rstdacdpz
    snd_soc_write(codec, 0, data32);

	acodec_delay_us(1);

    data32  = 0;
    data32 |= 1 << 1;   // [    1] rstadcdpz
    data32 |= 1 << 0;   // [    0] rstdacdpz
    snd_soc_write(codec, 0, data32);

	data32  = 0;
    data32 |= 1 << 4;   // [    4] pd_micb2z
    data32 |= 1 << 3;   // [    3] pd_micb1z
    data32 |= 1 << 2;   // [    2] pd_pgbuf2z
    data32 |= 1 << 1;   // [    1] pd_pgbuf1z
    data32 |= 1 << 0;   // [    0] pdz
    snd_soc_write(codec, 21, data32);
    
	//set the nominal current
	snd_soc_write(codec, 0x12, 0x0);

    // Disable soft-ramping
    data32  = 0;
    data32 |= 0 << 7;   // [    7] disable_sr
    data32 |= 0 << 5;   // [ 6: 5] dither_lvl
    data32 |= 0 << 4;   // [    4] noise_shape_en
    data32 |= 3 << 2;   // [ 3: 2] dem_cfg
    data32 |= 0 << 0;   // [ 1: 0] cfg_adc_dither
    snd_soc_write(codec, 0xF3, data32);
	snd_soc_write(codec, 24, 0xf3);

}

void acodec_config(unsigned int clk_ext_sel,           // [3:0]: 0=1.958M~2.65M; 1=2.6M~3.5M; 2=3.9M~5.3M; 3=5.2M~7.06M; 4=10.2M~13.8M;
                                                        //        5=15.3M~20.7M; 6=20.4M~26.6M; 7=25.5M~34.5M; 8=30.6M~41.4M; 9=40.8M~55.2M.
                   unsigned int i2s1_play_sclk_div,    // [7:0] For I2S master mode: ratio of mclk/sclk for playback
                   unsigned int i2s1_play_lrclk_div,   // [7:0] For I2S master mode: ratio of sclk/lrclk for playback
                   unsigned int i2s1_rec_sclk_div,     // [7:0] For I2S master mode: ratio of mclk/sclk for recording
                   unsigned int i2s1_rec_lrclk_div,    // [7:0] For I2S master mode: ratio of sclk/lrclk for recording
                   unsigned int en_i2s1_ext_clk,       // For I2S master mode:0=sclk rate depend on sclk/lrclk_div; 1= sclk=ext_clk.
                   unsigned int i2s1_word_sel,         // [1:0] 0=24-bit; 1=20bit; 2=18-bit; 3=16-bit.
                   unsigned int i2s1_ms_mode,          // 0=slave mode; 1=master mode.
                   unsigned int i2s1_mode,             // [2:0]: 0=Right justify, 1=I2S, 2=Left justify, 3=Burst1, 4=Burst2, 5=Mono burst1, 6=Mono burst2, 7=Rsrv.
                   unsigned int pga1_mute,             // [1:0]: [0] Input PGA left channel mute; [1] Input PGA right channel mute. 0=un-mute; 1=mute.
                   unsigned int rec_mute,              // [1:0]: [0] Recording left channel digital mute; [1] Recording right channel digital mute. 0=un-mute; 1=mute.
                   unsigned int hs1_mute,              // [1:0]: [0] Headset left channel analog mute; [1] Headset right channel analog mute. 0=un-mute; 1=mute.
                   unsigned int lm_mute,               // [1:0]: [0] Playback left channel digital mute; [1] Playback right channel digital mute. 0=un-mute; 1=mute.
                   unsigned int ld1_out_mute,          // [1:0]: [0] Playback left channel analog mute; [1] Playback right channel analog mute. 0=un-mute; 1=mute.
                   unsigned int ld2_out_mute,          // [1:0]: [0] Playback left channel analog mute; [1] Playback right channel analog mute. 0=un-mute; 1=mute.
                   unsigned int rec_vol,               // [15:0]: Recording digital master volume control. [7:0] Left; [15:8] Right. 0xbf=0dB.
                   unsigned int pga1_vol,              // [15:0]: Input PGA volume control. [7:0] Left; [15:8] Right. 0x12=0dB.
                   unsigned int lm_vol,                // [15:0]: Digital playback master volume control. [7:0] Left; [15:8] Right. 0xe7=0dB.
                   unsigned int hs1_vol,               // [15:0]: Headset analog volume control. [7:0] Left; [15:8] Right. 0x0d=0dB.
                   unsigned int pga1_sel,              // [15:0]: PGA input selection. [7:0] Left; [15:8] Right. 0=ain1p/n, 1=ain1p, 4=ain2p/n, 5=ain2p, 3=ain3.
                   unsigned int ldr1_sel,              // [15:0]: Playback analog mixer input selection.
                                                        //      [10] Signal from left recording path PGA.  0=Disabled; 1=Enabled;
                                                        //      [ 9] Signal from right recording path PGA. 0=Disabled; 1=Enabled;
                                                        //      [ 8] Signal from right channel DAC output. 0=Disabled; 1=Enabled;
                                                        //      [ 2] Signal from right recording path PGA. 0=Disabled; 1=Enabled;
                                                        //      [ 1] Signal from left recording path PGA.  0=Disabled; 1=Enabled;
                                                        //      [ 0] Signal from left channel DAC output.  0=Disabled; 1=Enabled;
                   unsigned int ld2_sel,               // [15:0]: Playback analog mixer input selection.
                                                        //      [ 2] Signal from right channel DAC output. 0=Disabled; 1=Enabled;
                                                        //      [ 1] Signal from left recording path PGA.  0=Disabled; 1=Enabled;
                                                        //      [ 0] Signal from left channel DAC output.  0=Disabled; 1=Enabled;
                   unsigned int ctr,                   // [2:0]: test mode sel. 0=Normal, 1=Digital filter loopback, 2=Digital filter bypass, 3=Digital audio I/F loopback, 4=Shaping filters loop-back.
                   unsigned int enhp)                  // Record channel high pass filter enable.
{
    adac_wr_reg(1, clk_ext_sel);
    adac_wr_reg(5, i2s1_play_sclk_div);
    adac_wr_reg(4, i2s1_play_lrclk_div);
    adac_wr_reg(7, i2s1_rec_sclk_div);
    adac_wr_reg(6, i2s1_rec_lrclk_div);
    adac_wr_reg(3, (en_i2s1_ext_clk<<6) | (i2s1_word_sel<<4) | (i2s1_ms_mode<<3) | (i2s1_mode<<0));
    adac_wr_reg(29, (pga1_mute<<2) | (rec_mute<<0));
    adac_wr_reg(31, (hs1_mute<<4) | lm_mute);
    adac_wr_reg(33, (ld2_out_mute<<2) | (ld1_out_mute<<0));
    adac_wr_reg(36, rec_vol);
    adac_wr_reg(38, pga1_vol);
    adac_wr_reg(52, lm_vol);
    adac_wr_reg(56, hs1_vol);
    adac_wr_reg(71, pga1_sel);
    adac_wr_reg(89, ldr1_sel);
    adac_wr_reg(93, ld2_sel);
    adac_wr_reg(210, ctr);
    adac_wr_reg(211, (enhp<<2));
}

void acodec_reserved_reg_set (struct snd_soc_codec *codec)
{
	unsigned int data32;

	data32  = 0;
    data32 |= 0   << 5; // [ 6: 5] cfg_adc_vcmi
    data32 |= 0   << 3; // [ 4: 3] cfg_dac_vcmi
    data32 |= 0   << 2; // [    2] sel_in_vcm_buf
    data32 |= 0   << 0; // [ 1: 0] cfg_vcm_buf
    snd_soc_write(codec, 220, data32);

	data32  = 0;
	data32 |= 0 << 7;    //[7]      tstenadcch
	data32 |= 1 << 6;    //[6]      enmux
	data32 |= 1 << 5;    //[5]      enpnadc
	data32 |= 1 << 4;    //[4]      cfgpn
	data32 |= 0 << 2;    //[3:2]   enff
	data32 |= 1 << 0;    //[1:0]   cfgadclib
	snd_soc_write(codec, 248, data32);

	data32  = 0;
	data32 |= 0 << 7;    //[7]      seladc1b
	data32 |= 0 << 0;    //[6:0]
	snd_soc_write(codec, 249, data32);

	data32  = 0;
	data32 |= 0 << 7;    //[7]      enpddac
	data32 |= 2 << 5;    //[6:5]   cfgdac
	data32 |= 0 << 4;    //[4]      enpulldown
	data32 |= 0 << 5;    //[3:0]   config_ana_2
	snd_soc_write(codec, 250, data32);

	data32  = 0;
	data32 |= 0 << 4;    //[7:4]   
	data32 |= 1 << 3;    //[3]      enRefResSeq
	data32 |= 0 << 2;    //[2]      enDischRefRes
	data32 |= 0 << 1;    //[1]      enForceRefRes
	data32 |= 1 << 0;    //[0]      enRefRes
	snd_soc_write(codec, 251, data32);
}
static void set_acodec_source (unsigned int src)
{
    unsigned int data32;
    unsigned int i;
    
    // Disable acodec clock input and its DAC input
    data32  = 0;
    data32 |= 0     << 4;   // [5:4]    acodec_data_sel: 00=disable acodec_sdin; 01=Select pcm data; 10=Select AIU I2S data; 11=Not allowed.
    data32 |= 0     << 0;   // [1:0]    acodec_clk_sel: 00=Disable acodec_sclk; 01=Select pcm clock; 10=Select AIU aoclk; 11=Not allowed.
    aml_write_reg32(P_AIU_CODEC_CLK_DATA_CTRL, data32);

    // Enable acodec clock from the selected source
    data32  = 0;
    data32 |= 0      << 4;  // [5:4]    acodec_data_sel: 00=disable acodec_sdin; 01=Select pcm data; 10=Select AIU I2S data; 11=Not allowed.
    data32 |= src   << 0;   // [1:0]    acodec_clk_sel: 00=Disable acodec_sclk; 01=Select pcm clock; 10=Select AIU aoclk; 11=Not allowed.
    aml_write_reg32(P_AIU_CODEC_CLK_DATA_CTRL, data32);
    
    // Wait until clock change is settled
    i = 0;
    while ( (((aml_read_reg32(P_AIU_CODEC_CLK_DATA_CTRL)) >> 8) & 0x3) != src ) {
        if (i > 255) {
            printk("[TEST.C] Error: set_acodec_source timeout!\n");
        }
        i++;
    }

    // Enable acodec DAC input from the selected source
    data32  = 0;
    data32 |= src   << 4;   // [5:4]    acodec_data_sel: 00=disable acodec_sdin; 01=Select pcm data; 10=Select AIU I2S data; 11=Not allowed.
    data32 |= src   << 0;   // [1:0]    acodec_clk_sel: 00=Disable acodec_sclk; 01=Select pcm clock; 10=Select AIU aoclk; 11=Not allowed.
    aml_write_reg32(P_AIU_CODEC_CLK_DATA_CTRL, data32);

    // Wait until data change is settled
    while ( (((aml_read_reg32(P_AIU_CODEC_CLK_DATA_CTRL)) >> 12) & 0x3) != src) {}
} /* set_acodec_source */

static void start_codec(struct snd_soc_codec *codec)
{
    // --------------------------------------------------------
    // Configure audio DAC control interface
    // --------------------------------------------------------
    unsigned data32 = 0;
    data32  = 0;
    data32 |= 0     << 15;  // [15]     audac_soft_reset_n
    data32 |= 1     << 14;  // [14]     audac_reset_ctrl: 0=use audac_reset_n pulse from reset module; 1=use audac_soft_reset_n.
    data32 |= 0     << 8;   // [8]      audac_reg_clk_inv
    data32 |= 0x55  << 1;   // [7:1]    audac_i2caddr
    data32 |= 0     << 0;   // [0]      audac_intfsel: 0=use host bus; 1=use I2C.
    aml_write_reg32(P_AIU_AUDAC_CTRL0, data32);

	acodec_normal_startup(codec);

    acodec_config(  5,      // clk_ext_sel[3:0]: 0=2.304M+/-15%; 1=3.072M+/-15%; 2=4.608M+/-15%; 3=6.144M+/-15%; 4=9.216M+/-15%;
                            //                   5=12.288M+/-15%; 6=18.432M+/-15%; 7=24.576M+/-15%; 8=30.720M+/-15%; 9=36.864M+/-15%; 10=49.152M+/-15%.
                    4,      // i2s1_play_sclk_div
                    64,     // i2s1_play_lrclk_div
                    4,      // i2s1_rec_sclk_div 
                    64,     // i2s1_rec_lrclk_div
                    0,      // en_i2s1_ext_clk
                    3,      // i2s1_word_sel: 0=24-bit; 1=20bit; 2=18-bit; 3=16-bit.
                    0,      // i2s1_ms_mode: 0=slave mode; 1=master mode.
                    1,      // i2s1_mode[2:0]: 0=Right justify, 1=I2S, 2=Left justify, 3=Burst1, 4=Burst2, 5=Mono burst1, 6=Mono burst2, 7=Rsrv.
                    0,      // pga1_mute[1:0]: [0] Input PGA left channel mute; [1] Input PGA right channel mute. 0=un-mute; 1=mute.
                    0,      // rec_mute[1:0]: [0] Recording left channel digital mute; [1] Recording right channel digital mute. 0=un-mute; 1=mute.
                    0,      // hs1_mute[1:0]: [0] Headset left channel analog mute; [1] Headset right channel analog mute. 0=un-mute; 1=mute.
                    0,      // lm_mute[1:0]: [0] Playback left channel digital mute; [1] Playback right channel digital mute. 0=un-mute; 1=mute.
                    0,      // ld1_out_mute[1:0]: [0] Playback left channel analog mute; [1] Playback right channel analog mute. 0=un-mute; 1=mute.
                    0,      // ld2_out_mute[1:0]: [0] Playback left channel analog mute; [1] Playback right channel analog mute. 0=un-mute; 1=mute.
                    0xbfbf,//0xbfbf, // rec_vol[15:0]: Recording digital master volume control. [7:0] Left; [15:8] Right. 0xbf=0dB.
                    0x2a2a, // 0x0606,pga1_vol[15:0]: Input PGA volume control. [7:0] Left; [15:8] Right. 0x12=0dB.
                    0xe7e7, // lm_vol[15:0]: Digital playback master volume control. [7:0] Left; [15:8] Right. 0xe7=0dB.
                    0x0d0d, // hs1_vol[15:0]: Headset analog volume control. [7:0] Left; [15:8] Right. 0x0d=0dB.
                    0x0101, // pga1_sel[15:0]: PGA input selection. [7:0] Left; [15:8] Right. 0=ain1p/n, 1=ain1p, 4=ain2p/n, 5=ain2p, 3=ain3.
                    0x0101, // ldr1_sel[15:0]: Playback analog mixer input selection.
                            // [10] Signal from left recording path PGA.  0=Disabled; 1=Enabled;
                            // [ 9] Signal from right recording path PGA. 0=Disabled; 1=Enabled;
                            // [ 8] Signal from right channel DAC output. 0=Disabled; 1=Enabled;
                            // [ 2] Signal from right recording path PGA. 0=Disabled; 1=Enabled;
                            // [ 1] Signal from left recording path PGA.  0=Disabled; 1=Enabled;
                            // [ 0] Signal from left channel DAC output.  0=Disabled; 1=Enabled;
                    0x0101, // ld2_sel[15:0]: Playback analog mixer input selection.
                            // [ 2] Signal from right channel DAC output. 0=Disabled; 1=Enabled;
                            // [ 1] Signal from left recording path PGA.  0=Disabled; 1=Enabled;
                            // [ 0] Signal from left channel DAC output.  0=Disabled; 1=Enabled;
                    0,      // ctr[2:0]: test mode sel. 0=Normal, 1=Digital filter loopback, 2=Digital filter bypass, 3=Digital audio I/F loopback, 4=Shaping filters loop-back.
                    1);     // enhp: Record channel high pass filter enable.


	acodec_reserved_reg_set(codec);

}
extern void audio_util_set_dac_i2s_format(unsigned format);

void aml_m8_codec_reset(struct snd_soc_codec* codec)
{
    aml_write_reg32(P_AUDIN_SOURCE_SEL,    1 << 0);    // Select audio codec output as I2S source
    aml_write_reg32(P_AUDIN_I2SIN_CTRL,    3 << I2SIN_SIZE             | 
                            			1 << I2SIN_CHAN_EN          |
                            			0 << I2SIN_POS_SYNC         | 
                            			1 << I2SIN_LRCLK_SKEW       | // delay LRCLK one aoclk cycle.
                            			0 << I2SIN_EN);               // Enable in IRQ

	set_acodec_source(2);
	audio_util_set_dac_i2s_format(0);

	start_codec(codec);
    snd_soc_write(codec,0x7b,0x03);  // record left frame output the playback left and right channels. 
}
static int pd3_env(struct snd_soc_dapm_widget *w,
    struct snd_kcontrol *kcontrol, int event)
{
    struct snd_soc_codec *codec = w->codec;
    unsigned int mask = 1<<w->shift;
    printk(KERN_INFO "Amlogic <> %s: name %s , event(%d)\n",__func__, w->name, event);
    snd_soc_update_bits(codec, AMLM8_PD_3, mask, mask);
    return 0;
}
static const char *left_linein_texts[] = {
	"Left Input 1 Differential", "Left Input 1 Single Ended",
	"Left Input 2 Differential", "Left Input 2 Single Ended",
	"Left Input 3 Single Ended"
};

static const char *right_linein_texts[] = {
	"Right Input 1 Differential", "Right Input 1 Single Ended",
	"Right Input 2 Differential", "Right Input 2 Single Ended",
	"Right Input 3 Single Ended"
};

static const unsigned int linein_values[] = {
    0, 1, 4, 5, 3
};
static const SOC_VALUE_ENUM_SINGLE_DECL(left_linein_select, AMLM8_PGA_SEL,
	0, 0x7, left_linein_texts, linein_values);
static const SOC_VALUE_ENUM_SINGLE_DECL(right_linein_select, AMLM8_PGA_SEL,
	8, 0x7, right_linein_texts, linein_values);

static const DECLARE_TLV_DB_SCALE(dac_tlv, -11550, 50, 0);
static const DECLARE_TLV_DB_SCALE(hs_tlv, -3900, 300, 0);
static const DECLARE_TLV_DB_SCALE(adc_tlv, -9550, 50, 0);
static const DECLARE_TLV_DB_SCALE(pga_tlv, -1800, 100, 0);
static const char *noise_gate[] = {
	"-70dB", "-60dB", "-50dB", "-40dB"
};
static const char *wf_freq[] = {
	"85 HZ", "130 HZ", "170 HZ", "240 HZ",
	"340 HZ", "480 HZ", "700 HZ", "1000 HZ"
};
static const char *digi_mixer[] = {
	"Stereo Mode", "Mixed Mode", "Switch Mode", 
	"Left Mono Mode", "Right Mono Mode"
};

static const struct soc_enum amlm8_enum[] = {
	SOC_ENUM_SINGLE(AMLM8_WIND_FILTER, 0, 8, wf_freq),	
	SOC_ENUM_SINGLE(AMLM8_NOISE_GATE_0, 1, 4, noise_gate),
	SOC_ENUM_SINGLE(AMLM8_REC_DMIX, 0, 5, digi_mixer),
	SOC_ENUM_SINGLE(AMLM8_PB_DMIX, 0, 5, digi_mixer),
	SOC_ENUM_SINGLE(AMLM8_I2S1_DMIX, 0, 5, digi_mixer)
};

static const struct snd_kcontrol_new amlm8_snd_controls[] = {
SOC_DOUBLE_R_TLV("Master Playback Volume", AMLM8_LM_LEFT_VOL, AMLM8_LM_RIGHT_VOL, 0, 0xFF, 0, dac_tlv),
SOC_DOUBLE_R_TLV("Headphone Volume", AMLM8_HS_LEFT_VOL, AMLM8_HS_RIGHT_VOL, 0, 0x0F, 0, hs_tlv),
SOC_DOUBLE_R_TLV("Record Volume", AMLM8_REC_LEFT_VOL, AMLM8_REC_RIGHT_VOL, 0, 0xFF, 0, adc_tlv),
SOC_DOUBLE_R_TLV("MIC PGA Volume", AMLM8_PGA_LEFT_VOL, AMLM8_PGA_RIGHT_VOL, 0, 0x35, 0, pga_tlv),

SOC_DOUBLE("Headphone Switch", AMLM8_MUTE_2, 4, 5, 1, 1),
SOC_DOUBLE("Capture Switch", AMLM8_MUTE_0, 0, 1, 1, 1),
SOC_DOUBLE("MIC PGA Switch", AMLM8_MUTE_0, 2, 3, 1, 1),
SOC_SINGLE("High Pass Filter Switch", AMLM8_HP_0, 2, 1, 0),

SOC_ENUM("Wind Filter Frequency", amlm8_enum[0]),
SOC_SINGLE("Wind Filter Switch", AMLM8_WIND_FILTER, 3, 1, 0),

SOC_ENUM("Noise Gate Threshhold", amlm8_enum[1]),
SOC_SINGLE("Noise Gate Switch", AMLM8_NOISE_GATE_0, 0, 1, 0),

SOC_ENUM("REC digital mixer", amlm8_enum[2]),
SOC_ENUM("Playback digital mixer", amlm8_enum[3]),
SOC_ENUM("i2s1 digital mixer", amlm8_enum[4]),

SOC_VALUE_ENUM("Left LINEIN Select", left_linein_select),
SOC_VALUE_ENUM("Right LINEIN Select", right_linein_select),

};
static const struct snd_kcontrol_new amlm8_left_ld1_mixer[] = {
SOC_DAPM_SINGLE("LEFT DAC Switch", AMLM8_LDR1_LEFT_SEL, 0, 1, 0),
SOC_DAPM_SINGLE("LEFT REC PGA Switch", AMLM8_LDR1_LEFT_SEL, 1, 1, 0),
SOC_DAPM_SINGLE("RIGHT REC PGA Switch", AMLM8_LDR1_LEFT_SEL, 2, 1, 0),
};
static const struct snd_kcontrol_new amlm8_right_ld1_mixer[] = {
SOC_DAPM_SINGLE("RIGHT DAC Switch", AMLM8_LDR1_RIGHT_SEL, 0, 1, 0),
SOC_DAPM_SINGLE("RIGHT REC PGA Switch", AMLM8_LDR1_RIGHT_SEL, 1, 1, 0),
SOC_DAPM_SINGLE("LEFT REC PGA Switch", AMLM8_LDR1_RIGHT_SEL, 2, 1, 0),
};
static const struct snd_kcontrol_new amlm8_mono_ld2_mixer[] = {
SOC_DAPM_SINGLE("LEFT DAC Switch", AMLM8_LDR2_SEL, 0, 1, 0),
SOC_DAPM_SINGLE("LEFT REC PGA Switch", AMLM8_LDR2_SEL, 1, 1, 0),
SOC_DAPM_SINGLE("RIGHT DAC Switch", AMLM8_LDR2_SEL, 2, 1, 0),
};

static const struct snd_soc_dapm_widget aml_m8_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("LINEOUTL"),
	SND_SOC_DAPM_OUTPUT("LINEOUTR"),
	SND_SOC_DAPM_OUTPUT("LINEOUTMONO"),
	SND_SOC_DAPM_OUTPUT("HP_L"),
	SND_SOC_DAPM_OUTPUT("HP_R"),
	
	SND_SOC_DAPM_INPUT("LINPUT1"),
	SND_SOC_DAPM_INPUT("LINPUT2"),
	SND_SOC_DAPM_INPUT("LINPUT3"),
	SND_SOC_DAPM_INPUT("RINPUT1"),
	SND_SOC_DAPM_INPUT("RINPUT2"),
	SND_SOC_DAPM_INPUT("RINPUT3"),

	SND_SOC_DAPM_SUPPLY("PG VCM", AMLM8_PD_0, 1, 0, NULL, 0),
	SND_SOC_DAPM_MICBIAS("MICBIAS", AMLM8_PD_0, 3, 0),

	SND_SOC_DAPM_ADC("Left ADC", "Capture", AMLM8_PD_1, 0, 0),
	SND_SOC_DAPM_ADC("Right ADC", "Capture", AMLM8_PD_1, 1, 0),
	SND_SOC_DAPM_PGA("Left IN PGA", AMLM8_PD_1, 2, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right IN PGA", AMLM8_PD_1, 3, 0, NULL, 0),

	//SND_SOC_DAPM_DAC("Left DAC", "Playback", AMLM8_PD_3, 0, 0),
	//SND_SOC_DAPM_DAC("Right DAC", "Playback", AMLM8_PD_3, 1, 0),
	//SND_SOC_DAPM_PGA("Left HP OUT PGA", AMLM8_PD_3, 4, 0, NULL, 0),
	//SND_SOC_DAPM_PGA("Right HP OUT PGA", AMLM8_PD_3, 5, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Left Output Mixer", SND_SOC_NOPM, 6, 0,
		&amlm8_left_ld1_mixer[0],
		ARRAY_SIZE(amlm8_left_ld1_mixer)),
	SND_SOC_DAPM_MIXER("Right Output Mixer", SND_SOC_NOPM, 7, 0,
		&amlm8_right_ld1_mixer[0],
		ARRAY_SIZE(amlm8_right_ld1_mixer)),
    SND_SOC_DAPM_DAC_E("Left DAC", "Playback", SND_SOC_NOPM, 0, 0, pd3_env,
        SND_SOC_DAPM_PRE_PMD|SND_SOC_DAPM_PRE_PMU|SND_SOC_DAPM_POST_PMD),
         
    SND_SOC_DAPM_DAC_E("Right DAC", "Playback", SND_SOC_NOPM, 1, 0, pd3_env,
        SND_SOC_DAPM_PRE_PMD|SND_SOC_DAPM_PRE_PMU|SND_SOC_DAPM_POST_PMD),

    SND_SOC_DAPM_PGA_E("Left HP OUT PGA", SND_SOC_NOPM, 4, 0, NULL, 0, pd3_env, 
        SND_SOC_DAPM_PRE_PMD|SND_SOC_DAPM_PRE_PMU),
        
    SND_SOC_DAPM_PGA_E("Right HP OUT PGA", SND_SOC_NOPM, 5, 0, NULL, 0, pd3_env, 
        SND_SOC_DAPM_PRE_PMD|SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_MIXER("Mono Output Mixer", AMLM8_PD_4, 2, 0,
		&amlm8_mono_ld2_mixer[0],
		ARRAY_SIZE(amlm8_mono_ld2_mixer)),

};

static const struct snd_soc_dapm_route aml_m8_audio_map[] = {
	{ "Left IN PGA", NULL, "LINPUT1" },
	{ "Left IN PGA", NULL, "LINPUT2" },
	{ "Left IN PGA", NULL, "LINPUT3" },

	{ "Right IN PGA", NULL, "RINPUT1" },
	{ "Right IN PGA", NULL, "RINPUT2" },
	{ "Right IN PGA", NULL, "RINPUT3" },
	
	{ "Left ADC", NULL, "Left IN PGA" },
	{ "Right ADC", NULL, "Right IN PGA" },

	{ "Left Output Mixer", "LEFT DAC Switch", "Left DAC" },
	{ "Left Output Mixer", "LEFT REC PGA Switch", "Left IN PGA" },
	{ "Left Output Mixer", "RIGHT REC PGA Switch", "Right IN PGA" },

	{ "Right Output Mixer", "RIGHT DAC Switch", "Right DAC" },
	{ "Right Output Mixer", "RIGHT REC PGA Switch", "Right IN PGA" },
	{ "Right Output Mixer", "LEFT REC PGA Switch", "Left IN PGA" },

	{ "Mono Output Mixer", "LEFT DAC Switch", "Left DAC" },
	{ "Mono Output Mixer", "LEFT REC PGA Switch", "Left IN PGA" },
	{ "Mono Output Mixer", "RIGHT DAC Switch", "Right DAC" },

	{ "LINEOUTL", NULL, "Left Output Mixer" },
	{ "LINEOUTR", NULL, "Right Output Mixer" },
	
	{ "LINEOUTMONO", NULL, "Mono Output Mixer" },

	{ "Left HP OUT PGA", NULL, "Left Output Mixer" },
	{ "Right HP OUT PGA", NULL, "Right Output Mixer" },
	{ "HP_L", NULL, "Left HP OUT PGA" },
	{ "HP_R", NULL, "Right HP OUT PGA" },
};

static int aml_m8_soc_probe(struct snd_soc_codec *codec){
	struct snd_soc_dapm_context *dapm = &codec->dapm;
    m8_codec = codec;
	audio_aiu_pg_enable(1);
	aml_m8_codec_reset(codec);
	audio_aiu_pg_enable(0);
   	codec->dapm.bias_level = SND_SOC_BIAS_STANDBY;

	snd_soc_dapm_force_enable_pin(dapm, "MICBIAS");

    return 0;
}
static int aml_m8_soc_remove(struct snd_soc_codec *codec){
	aml_m8_set_bias_level(codec, SND_SOC_BIAS_OFF);	
	return 0;
}
static int aml_m8_soc_suspend(struct snd_soc_codec *codec){
	printk("aml_m8_codec_suspend\n");
	aml_m8_set_bias_level(codec, SND_SOC_BIAS_OFF);	
    return 0;
}

static int aml_m8_soc_resume(struct snd_soc_codec *codec){
	printk("aml_m8_codec resume\n");

	aml_m8_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	//WRITE_MPEG_REG_BITS( HHI_MPLL_CNTL9, 1,14, 1);	
    return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_amlm8 = {
	.probe = 	aml_m8_soc_probe,
	.remove = 	aml_m8_soc_remove,
	.suspend =	aml_m8_soc_suspend,
	.resume = 	aml_m8_soc_resume,
	.read = aml_m8_read,
	.write = aml_m8_write,
	.set_bias_level = aml_m8_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(acodec_regbank),
	.reg_word_size = sizeof(u16),
	.reg_cache_step = 1,
	.reg_cache_default = acodec_regbank,
	.controls = amlm8_snd_controls,
	.num_controls = ARRAY_SIZE(amlm8_snd_controls),
	.dapm_widgets = aml_m8_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(aml_m8_dapm_widgets),
	.dapm_routes = aml_m8_audio_map,
	.num_dapm_routes = ARRAY_SIZE(aml_m8_audio_map),
};

static int aml_m8_codec_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, 
		&soc_codec_dev_amlm8, &aml_m8_codec_dai, 1);
}

static int aml_m8_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static void aml_m8_codec_shutdown(struct platform_device *pdev)
{
    struct snd_soc_codec *codec = m8_codec;
    printk(KERN_DEBUG "aml_m8_platform_shutdown\n");
    if(codec){
        u16 mute_reg = snd_soc_read(codec, AMLM8_MUTE_2) & 0xfc;

        snd_soc_write(codec, AMLM8_MUTE_2, mute_reg | 0x3);
    }
}

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_audio_codec_dt_match[]={
    { .compatible = "amlogic,m8_audio_codec", },
    {},
};
#else
#define amlogic_audio_dt_match NULL
#endif
static struct platform_driver aml_m8_codec_platform_driver = {
	.driver = {
		.name = "aml_m8_codec",
		.owner = THIS_MODULE,
        .of_match_table = amlogic_audio_codec_dt_match,
	},
	.probe = aml_m8_codec_probe,
	.remove = aml_m8_codec_remove,
	.shutdown = aml_m8_codec_shutdown,
};

static int __init aml_m8_codec_modinit(void)
{
	int ret = 0;

	ret = platform_driver_register(&aml_m8_codec_platform_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register AMLM8 platform driver: %d\n",
		       ret);
	}

	return ret;
}
module_init(aml_m8_codec_modinit);

static void __exit aml_m8_codec_exit(void)
{
	platform_driver_unregister(&aml_m8_codec_platform_driver);
}
module_exit(aml_m8_codec_exit);

MODULE_DESCRIPTION("ASoC AMLM8 CODEC driver");
MODULE_AUTHOR("AMLOGIC INC.");
MODULE_LICENSE("GPL");
