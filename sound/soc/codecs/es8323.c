/*
 * es8323.c -- es8323 ALSA SoC audio driver
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
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include <mach/iomux.h>
#include <mach/board.h>
#include <mach/gpio.h>

//#include <linux/tchip_sysinf.h>

#include "es8323.h"

#include <linux/proc_fs.h>
#include <linux/gpio.h>


#include <linux/interrupt.h>
#include <linux/irq.h>

#if 0
#define DBG(x...) printk(KERN_INFO x)
#else
#define DBG(x...) do { } while (0)
#endif
#define alsa_dbg DBG

static int set_spk = 1;                     // add by xhc when insert hdmi 0, no insert hdmi 1
#define RT5633_SPK_TIMER	0	//if enable this, MUST enable RT5633_EQ_FUNC_ENA and RT5633_EQ_FUNC_SEL==RT5633_EQ_FOR_MANUAL first!
#if (RT5633_SPK_TIMER == 1)
static struct timer_list spk_timer;
struct work_struct  spk_work;
static bool last_is_spk = false;
//#define ES8323_HP_PIN  RK30_PIN0_PC7	// need modify.
#endif
//#define SPK_CTL 		RK29_PIN6_PB7
//#define SPK_CON 		RK29_PIN6_PB6
    #undef SPK_CTL
//    #undef SPK_CON
#ifdef CONFIG_MACH_RK_FAC 
int es8323_hdmi_ctrl=0;
#endif
#define SPK_CON 		RK30_PIN2_PD7 //RK30_PIN4_PC5
#define HP_DET          RK30_PIN0_PB5
static int HP_IRQ=0;
static int hp_irq_flag = 0;
//#define SPK_CTL             RK29_PIN6_PB6
//#define EAR_CON_PIN             RK29_PIN6_PB5
#undef EAR_CON_PIN

#ifndef es8323_DEF_VOL
#define es8323_DEF_VOL			0x1e
#endif

static int es8323_set_bias_level(struct snd_soc_codec *codec,enum snd_soc_bias_level level);
extern int es8323_dapm_pre_event(struct snd_soc_dapm_widget* widget, struct snd_kcontrol * null, int event);
extern int es8323_dapm_post_event(struct snd_soc_dapm_widget* widget, struct snd_kcontrol * null, int event);                                
/*
 * es8323 register cache
 * We can't read the es8323 register space when we
 * are using 2 wire for device control, so we cache them instead.
 */
static u16 es8323_reg[] = {
	0x06, 0x1C, 0xC3, 0xFC,  /*  0 *////0x0100 0x0180
	0xC0, 0x00, 0x00, 0x7C,  /*  4 */
	0x80, 0x00, 0x00, 0x06,  /*  8 */
	0x00, 0x06, 0x30, 0x30,  /* 12 */
	0xC0, 0xC0, 0x38, 0xB0,  /* 16 */
	0x32, 0x06, 0x00, 0x00,  /* 20 */
	0x06, 0x30, 0xC0, 0xC0,  /* 24 */
	0x08, 0x06, 0x1F, 0xF7,  /* 28 */
	0xFD, 0xFF, 0x1F, 0xF7,  /* 32 */
	0xFD, 0xFF, 0x00, 0x38,  /* 36 */
	0x38, 0x38, 0x38, 0x38,  /* 40 */
	0x38, 0x00, 0x00, 0x00,  /* 44 */
	0x00, 0x00, 0x00, 0x00,  /* 48 */
	0x00, 0x00, 0x00, 0x00,  /* 52 */
};

/* codec private data */
struct es8323_priv {
	unsigned int sysclk;
	enum snd_soc_control_type control_type;
	struct snd_pcm_hw_constraint_list *sysclk_constraints;
	int is_startup;		// gModify.Add
	int is_biason;
};

static void hp_detect_do_switch(struct work_struct *work)
{
	int ret;
	int irq = gpio_to_irq(HP_DET);
	unsigned int type;

	//rk28_send_wakeup_key();
	printk("hjc:%s,irq=%d\n",__func__,irq);
	type = gpio_get_value(HP_DET) ? IRQ_TYPE_EDGE_FALLING : IRQ_TYPE_EDGE_RISING;
	ret = irq_set_irq_type(irq, type);
	if (ret < 0) {
		pr_err("%s: irq_set_irq_type(%d, %d) failed\n", __func__, irq, type);
	}

	hp_irq_flag = 1;

	if(0 == gpio_get_value(HP_DET)){
		printk("hp_det = 0,insert hp\n");
		gpio_set_value(SPK_CON,0);
	}else if(1 == gpio_get_value(HP_DET)){
		printk("hp_det = 1,deinsert hp\n");
		gpio_set_value(SPK_CON,1);
	}	
	enable_irq(irq);
}


static DECLARE_DELAYED_WORK(wakeup_work, hp_detect_do_switch);


static irqreturn_t hp_det_irq_handler(int irq, void *dev_id)
{
#if 0
    printk("%s=%d,%d\n",__FUNCTION__,HP_IRQ,HP_DET);
    //disable_irq_nosync(ts->client->irq);	
	//queue_work(gt801_wq, &ts->work);
	if(0 == gpio_get_value(HP_DET)){
		printk("hp_det = 0,insert hp\n");
		gpio_set_value(SPK_CON,0);
	}else if(1 == gpio_get_value(HP_DET)){
		printk("hp_det = 1,insert hp\n");
		gpio_set_value(SPK_CON,1);
	}	
    return IRQ_HANDLED;
#endif
	printk("hjc:%s>>>>\n",__func__);
	disable_irq_nosync(irq); // for irq debounce
	//wake_lock_timeout(&usb_wakelock, WAKE_LOCK_TIMEOUT);
	schedule_delayed_work(&wakeup_work, HZ / 10);
	return IRQ_HANDLED;

}



static unsigned int es8323_read_reg_cache(struct snd_soc_codec *codec,
				     unsigned int reg)
{
	//u16 *cache = codec->reg_cache;
	if (reg >= ARRAY_SIZE(es8323_reg))
		return -1;
	return es8323_reg[reg];
}

static int es8323_write(struct snd_soc_codec *codec, unsigned int reg,
			     unsigned int value)
{
	//u16 *cache = codec->reg_cache;
	u8 data[2];
	int ret;

	BUG_ON(codec->volatile_register);

	data[0] = reg;
	data[1] = value & 0x00ff;

	if (reg < ARRAY_SIZE(es8323_reg))
		es8323_reg[reg] = value;
	ret = codec->hw_write(codec->control_data, data, 2);
	if (ret == 2)
		return 0;
	if (ret < 0)
		return ret;
	else
		return -EIO;
}

//#define es8323_reset(c)	snd_soc_write(c, es8323_RESET, 0)
 static int es8323_reset(struct snd_soc_codec *codec)
 {
 	snd_soc_write(codec, ES8323_CONTROL1, 0x80);
  return snd_soc_write(codec, ES8323_CONTROL1, 0x00);
 }

static const char *es8323_line_texts[] = {
	"Line 1", "Line 2", "PGA"};

static const unsigned int es8323_line_values[] = {
	0, 1, 3};
static const char *es8323_pga_sel[] = {"Line 1", "Line 2", "Differential"};
static const char *stereo_3d_txt[] = {"No 3D  ", "Level 1","Level 2","Level 3","Level 4","Level 5","Level 6","Level 7"};
static const char *alc_func_txt[] = {"Off", "Right", "Left", "Stereo"};
static const char *ng_type_txt[] = {"Constant PGA Gain","Mute ADC Output"};
static const char *deemph_txt[] = {"None", "32Khz", "44.1Khz", "48Khz"};
static const char *adcpol_txt[] = {"Normal", "L Invert", "R Invert","L + R Invert"};
static const char *es8323_mono_mux[] = {"Stereo", "Mono (Left)","Mono (Right)"};
static const char *es8323_diff_sel[] = {"Line 1", "Line 2"};
				   
static const struct soc_enum es8323_enum[]={	
	SOC_VALUE_ENUM_SINGLE(ES8323_DACCONTROL16, 3, 7, ARRAY_SIZE(es8323_line_texts), es8323_line_texts, es8323_line_values),/* LLINE */
	SOC_VALUE_ENUM_SINGLE(ES8323_DACCONTROL16, 0, 7, ARRAY_SIZE(es8323_line_texts), es8323_line_texts, es8323_line_values),/* rline	*/
	SOC_VALUE_ENUM_SINGLE(ES8323_ADCCONTROL2, 6, 3, ARRAY_SIZE(es8323_pga_sel), es8323_line_texts, es8323_line_values),/* Left PGA Mux */
	SOC_VALUE_ENUM_SINGLE(ES8323_ADCCONTROL2, 4, 3, ARRAY_SIZE(es8323_pga_sel), es8323_line_texts, es8323_line_values),/* Right PGA Mux */
	SOC_ENUM_SINGLE(ES8323_DACCONTROL7, 2, 8, stereo_3d_txt),/* stereo-3d */
	SOC_ENUM_SINGLE(ES8323_ADCCONTROL10, 6, 4, alc_func_txt),/*alc func*/
	SOC_ENUM_SINGLE(ES8323_ADCCONTROL14, 1, 2, ng_type_txt),/*noise gate type*/
	SOC_ENUM_SINGLE(ES8323_DACCONTROL6, 6, 4, deemph_txt),/*Playback De-emphasis*/
	SOC_ENUM_SINGLE(ES8323_ADCCONTROL6, 6, 4, adcpol_txt),
	SOC_ENUM_SINGLE(ES8323_ADCCONTROL3, 3, 3, es8323_mono_mux),
	SOC_ENUM_SINGLE(ES8323_ADCCONTROL3, 7, 2, es8323_diff_sel),
	};
		

static const DECLARE_TLV_DB_SCALE(pga_tlv, 0, 300, 0);
static const DECLARE_TLV_DB_SCALE(adc_tlv, -9600, 50, 1);
static const DECLARE_TLV_DB_SCALE(dac_tlv, -9600, 50, 1);
static const DECLARE_TLV_DB_SCALE(out_tlv, -4500, 150, 0);
static const DECLARE_TLV_DB_SCALE(bypass_tlv, -1500, 300, 0);

static const struct snd_kcontrol_new es8323_snd_controls[] = {
SOC_ENUM("3D Mode", es8323_enum[4]),
SOC_SINGLE("ALC Capture Target Volume", ES8323_ADCCONTROL11, 4, 15, 0),
SOC_SINGLE("ALC Capture Max PGA", ES8323_ADCCONTROL10, 3, 7, 0),
SOC_SINGLE("ALC Capture Min PGA", ES8323_ADCCONTROL10, 0, 7, 0),
SOC_ENUM("ALC Capture Function", es8323_enum[5]),
SOC_SINGLE("ALC Capture ZC Switch", ES8323_ADCCONTROL13, 6, 1, 0),
SOC_SINGLE("ALC Capture Hold Time", ES8323_ADCCONTROL11, 0, 15, 0),
SOC_SINGLE("ALC Capture Decay Time", ES8323_ADCCONTROL12, 4, 15, 0),
SOC_SINGLE("ALC Capture Attack Time", ES8323_ADCCONTROL12, 0, 15, 0),
SOC_SINGLE("ALC Capture NG Threshold", ES8323_ADCCONTROL14, 3, 31, 0),
SOC_ENUM("ALC Capture NG Type",es8323_enum[6]),
SOC_SINGLE("ALC Capture NG Switch", ES8323_ADCCONTROL14, 0, 1, 0),
SOC_SINGLE("ZC Timeout Switch", ES8323_ADCCONTROL13, 6, 1, 0),
SOC_DOUBLE_R_TLV("Capture Digital Volume", ES8323_ADCCONTROL8, ES8323_ADCCONTROL9,0, 255, 1, adc_tlv),		 
SOC_SINGLE("Capture Mute", ES8323_ADCCONTROL7, 2, 1, 0),		
SOC_SINGLE_TLV("Left Channel Capture Volume",	ES8323_ADCCONTROL1, 4, 15, 0, bypass_tlv),
SOC_SINGLE_TLV("Right Channel Capture Volume",	ES8323_ADCCONTROL1, 0, 15, 0, bypass_tlv),
SOC_ENUM("Playback De-emphasis", es8323_enum[7]),
SOC_ENUM("Capture Polarity", es8323_enum[8]),
SOC_DOUBLE_R_TLV("PCM Volume", ES8323_DACCONTROL4, ES8323_DACCONTROL5, 0, 255, 1, dac_tlv),
SOC_SINGLE_TLV("Left Mixer Left Bypass Volume", ES8323_DACCONTROL17, 3, 7, 1, bypass_tlv),
SOC_SINGLE_TLV("Right Mixer Right Bypass Volume", ES8323_DACCONTROL20, 3, 7, 1, bypass_tlv),
SOC_DOUBLE_R_TLV("Output 1 Playback Volume", ES8323_DACCONTROL24, ES8323_DACCONTROL25, 0, 64, 0, out_tlv),
SOC_DOUBLE_R_TLV("Output 2 Playback Volume", ES8323_DACCONTROL26, ES8323_DACCONTROL27, 0, 64, 0, out_tlv),
};


static const struct snd_kcontrol_new es8323_left_line_controls =
	SOC_DAPM_VALUE_ENUM("Route", es8323_enum[0]);

static const struct snd_kcontrol_new es8323_right_line_controls =
	SOC_DAPM_VALUE_ENUM("Route", es8323_enum[1]);

/* Left PGA Mux */
static const struct snd_kcontrol_new es8323_left_pga_controls =
	SOC_DAPM_VALUE_ENUM("Route", es8323_enum[2]);
/* Right PGA Mux */
static const struct snd_kcontrol_new es8323_right_pga_controls =
	SOC_DAPM_VALUE_ENUM("Route", es8323_enum[3]);

/* Left Mixer */
static const struct snd_kcontrol_new es8323_left_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left Playback Switch", ES8323_DACCONTROL17, 7, 1, 0),
	SOC_DAPM_SINGLE("Left Bypass Switch", ES8323_DACCONTROL17, 6, 1, 0),	
};

/* Right Mixer */
static const struct snd_kcontrol_new es8323_right_mixer_controls[] = {
	SOC_DAPM_SINGLE("Right Playback Switch", ES8323_DACCONTROL20, 7, 1, 0),
	SOC_DAPM_SINGLE("Right Bypass Switch", ES8323_DACCONTROL20, 6, 1, 0),
};

/* Differential Mux */
//static const char *es8323_diff_sel[] = {"Line 1", "Line 2"};
static const struct snd_kcontrol_new es8323_diffmux_controls =
	SOC_DAPM_ENUM("Route", es8323_enum[10]);

/* Mono ADC Mux */
static const struct snd_kcontrol_new es8323_monomux_controls =
	SOC_DAPM_ENUM("Route", es8323_enum[9]);

static const struct snd_soc_dapm_widget es8323_dapm_widgets[] = {
#if 1
	SND_SOC_DAPM_INPUT("LINPUT1"),
	SND_SOC_DAPM_INPUT("LINPUT2"),
	SND_SOC_DAPM_INPUT("RINPUT1"),
	SND_SOC_DAPM_INPUT("RINPUT2"),
	
	SND_SOC_DAPM_MICBIAS("Mic Bias", ES8323_ADCPOWER, 3, 1),

	SND_SOC_DAPM_MUX("Differential Mux", SND_SOC_NOPM, 0, 0,
		&es8323_diffmux_controls),
		
	SND_SOC_DAPM_MUX("Left ADC Mux", SND_SOC_NOPM, 0, 0,
		&es8323_monomux_controls),
	SND_SOC_DAPM_MUX("Right ADC Mux", SND_SOC_NOPM, 0, 0,
		&es8323_monomux_controls),
 
	SND_SOC_DAPM_MUX("Left PGA Mux", ES8323_ADCPOWER, 7, 1,
		&es8323_left_pga_controls),
	SND_SOC_DAPM_MUX("Right PGA Mux", ES8323_ADCPOWER, 6, 1,
		&es8323_right_pga_controls),

	SND_SOC_DAPM_MUX("Left Line Mux", SND_SOC_NOPM, 0, 0,
		&es8323_left_line_controls),
	SND_SOC_DAPM_MUX("Right Line Mux", SND_SOC_NOPM, 0, 0,
		&es8323_right_line_controls),

	SND_SOC_DAPM_ADC("Right ADC", "Right Capture", ES8323_ADCPOWER, 4, 1),
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", ES8323_ADCPOWER, 5, 1),

	/* gModify.Cmmt Implement when suspend/startup */
	SND_SOC_DAPM_DAC("Right DAC", "Right Playback", ES8323_DACPOWER, 7, 0),
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback", ES8323_DACPOWER, 8, 0),

	SND_SOC_DAPM_MIXER("Left Mixer", SND_SOC_NOPM, 0, 0,
		&es8323_left_mixer_controls[0],
		ARRAY_SIZE(es8323_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right Mixer", SND_SOC_NOPM, 0, 0,
		&es8323_right_mixer_controls[0],
		ARRAY_SIZE(es8323_right_mixer_controls)),

	SND_SOC_DAPM_PGA("Right Out 2", ES8323_DACPOWER, 2, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 2", ES8323_DACPOWER, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Out 1", ES8323_DACPOWER, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 1", ES8323_DACPOWER, 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA("LAMP", ES8323_ADCCONTROL1, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA("RAMP", ES8323_ADCCONTROL1, 0, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("LOUT1"),
	SND_SOC_DAPM_OUTPUT("ROUT1"),
	SND_SOC_DAPM_OUTPUT("LOUT2"),
	SND_SOC_DAPM_OUTPUT("ROUT2"),
	SND_SOC_DAPM_OUTPUT("VREF"),

	SND_SOC_DAPM_PRE("PRE", es8323_dapm_pre_event),	
  SND_SOC_DAPM_POST("POST", es8323_dapm_post_event),
#endif
};

static const struct snd_soc_dapm_route audio_map[] = {

	{ "Left Line Mux", "NULL", "LINPUT1" },
	{ "Left Line Mux", "NULL", "LINPUT2" },
	{ "Left Line Mux", "NULL", "Left PGA Mux" },
	
	{ "Right Line Mux", "NULL", "RINPUT1" },
	{ "Right Line Mux", "NULL", "RINPUT2" },
	{ "Right Line Mux", "NULL", "Right PGA Mux" },	

	{ "Left PGA Mux", "LAMP", "LINPUT1" },
	{ "Left PGA Mux", "LAMP", "LINPUT2" },
	{ "Left PGA Mux", "LAMP", "Differential Mux" },

	{ "Right PGA Mux", "RAMP", "RINPUT1" },
	{ "Right PGA Mux", "RAMP", "RINPUT2" },
	{ "Right PGA Mux", "RAMP", "Differential Mux" },

	{ "Differential Mux", "LAMP", "LINPUT1" },
	{ "Differential Mux", "RAMP", "RINPUT1" },
	{ "Differential Mux", "LAMP", "LINPUT2" },
	{ "Differential Mux", "RAMP", "RINPUT2" },

	{ "Left ADC Mux", "Stereo", "Left PGA Mux" },
	{ "Left ADC Mux", "Mono (Left)", "Left PGA Mux" },
	//{ "Left ADC Mux", "Digital Mono", "Left PGA Mux" },

	{ "Right ADC Mux", "Stereo", "Right PGA Mux" },
	{ "Right ADC Mux", "Mono (Right)", "Right PGA Mux" },
	//{ "Right ADC Mux", "Digital Mono", "Right PGA Mux" },

	{ "Left ADC", NULL, "Left ADC Mux" },
	{ "Right ADC", NULL, "Right ADC Mux" },

	{ "Left Line Mux", "LAMP", "LINPUT1" },
	{ "Left Line Mux", "LAMP", "LINPUT2" },
	{ "Left Line Mux", "LAMP", "Left PGA Mux" },

	{ "Right Line Mux", "RAMP", "RINPUT1" },
	{ "Right Line Mux", "RAMP", "RINPUT2" },
	{ "Right Line Mux", "RAMP", "Right PGA Mux" },	

	{ "Left Mixer", "Left Playback Switch", "Left DAC" },
	{ "Left Mixer", "Left Bypass Switch", "Left Line Mux" },

	{ "Right Mixer", "Right Playback Switch", "Right DAC" },
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

int es8323_dapm_pre_event(struct snd_soc_dapm_widget* widget, struct snd_kcontrol * null, int event)
{
//	printk("fun:%s, event:%d\r\n", __FUNCTION__, event);
	if (event==1)
	{ 
		widget->dapm->dev_power = 1;
		es8323_set_bias_level(widget->codec, SND_SOC_BIAS_PREPARE);
		}		
	return 0;
}
int es8323_dapm_post_event(struct snd_soc_dapm_widget* widget, struct snd_kcontrol * null, int event)
{
//	printk("fun:%s, event:%d\r\n", __FUNCTION__, event);
	if (event==8)
	{
		widget->dapm->dev_power = 0;
		es8323_set_bias_level(widget->codec, SND_SOC_BIAS_STANDBY);
	}
	return 0;
}

struct _coeff_div {
	u32 mclk;
	u32 rate;
	u16 fs;
	u8 sr:4;
	u8 usb:1;
};

/* codec hifi mclk clock divider coefficients */
static const struct _coeff_div coeff_div[] = {
	/* 8k */
	{12288000, 8000, 1536, 0xa, 0x0},
	{11289600, 8000, 1408, 0x9, 0x0},
	{18432000, 8000, 2304, 0xc, 0x0},
	{16934400, 8000, 2112, 0xb, 0x0},
	{12000000, 8000, 1500, 0xb, 0x1},

	/* 11.025k */
	{11289600, 11025, 1024, 0x7, 0x0},
	{16934400, 11025, 1536, 0xa, 0x0},
	{12000000, 11025, 1088, 0x9, 0x1},

	/* 16k */
	{12288000, 16000, 768, 0x6, 0x0},
	{18432000, 16000, 1152, 0x8, 0x0},
	{12000000, 16000, 750, 0x7, 0x1},

	/* 22.05k */
	{11289600, 22050, 512, 0x4, 0x0},
	{16934400, 22050, 768, 0x6, 0x0},
	{12000000, 22050, 544, 0x6, 0x1},

	/* 32k */
	{12288000, 32000, 384, 0x3, 0x0},
	{18432000, 32000, 576, 0x5, 0x0},
	{12000000, 32000, 375, 0x4, 0x1},

	/* 44.1k */
	{11289600, 44100, 256, 0x2, 0x0},
	{16934400, 44100, 384, 0x3, 0x0},
	{12000000, 44100, 272, 0x3, 0x1},

	/* 48k */
	{12288000, 48000, 256, 0x2, 0x0},
	{18432000, 48000, 384, 0x3, 0x0},
	{12000000, 48000, 250, 0x2, 0x1},

	/* 88.2k */
	{11289600, 88200, 128, 0x0, 0x0},
	{16934400, 88200, 192, 0x1, 0x0},
	{12000000, 88200, 136, 0x1, 0x1},

	/* 96k */
	{12288000, 96000, 128, 0x0, 0x0},
	{18432000, 96000, 192, 0x1, 0x0},
	{12000000, 96000, 125, 0x0, 0x1},
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
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000,
	48000, 88235, 96000,
};

static struct snd_pcm_hw_constraint_list constraints_12 = {
	.count	= ARRAY_SIZE(rates_12),
	.list	= rates_12,
};

static void on_off_ext_amp(int i)
{
    // struct snd_soc_codec *codec;
    if (set_spk == 0) {
            return;
    }
   if(hp_irq_flag == 0)	
  	 gpio_set_value(SPK_CON, i);  //delete by hjc

    DBG("*** %s() SPEAKER set SPK_CON %d\n", __FUNCTION__, i);
    mdelay(50);
    #ifdef SPK_CTL
    //gpio_direction_output(SPK_CTL, GPIO_LOW);
    gpio_set_value(SPK_CTL, i);
    DBG("*** %s() SPEAKER set as %d\n", __FUNCTION__, i);
    #endif
    #ifdef EAR_CON_PIN
    //gpio_direction_output(EAR_CON_PIN, GPIO_LOW);
    gpio_set_value(EAR_CON_PIN, i);
    DBG("*** %s() HEADPHONE set as %d\n", __FUNCTION__, i);
    mdelay(50);
    #endif
}

#if 0
static void es8323_codec_set_spk(bool on)
{
	on_off_ext_amp(on);
}
#endif



/*
 * Note that this should be called from init rather than from hw_params.
 */
static int es8323_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct es8323_priv *es8323 = snd_soc_codec_get_drvdata(codec);

    DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
		
	switch (freq) {
	case 11289600:
	case 18432000:
	case 22579200:
	case 36864000:
		es8323->sysclk_constraints = &constraints_112896;
		es8323->sysclk = freq;
		return 0;

	case 12288000:
	case 16934400:
	case 24576000:
	case 33868800:
		es8323->sysclk_constraints = &constraints_12288;
		es8323->sysclk = freq;
		return 0;

	case 12000000:
	case 24000000:
		es8323->sysclk_constraints = &constraints_12;
		es8323->sysclk = freq;
		return 0;
	}
	return -EINVAL;
}

static int es8323_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
  struct snd_soc_codec *codec = codec_dai->codec;
    u8 iface = 0;
    u8 adciface = 0;
    u8 daciface = 0;
    alsa_dbg("%s----%d, fmt[%02x]\n",__FUNCTION__,__LINE__,fmt);

    iface    = snd_soc_read(codec, ES8323_IFACE);
    adciface = snd_soc_read(codec, ES8323_ADC_IFACE);
    daciface = snd_soc_read(codec, ES8323_DAC_IFACE);

    /* set master/slave audio interface */
    switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
        case SND_SOC_DAIFMT_CBM_CFM:    // MASTER MODE
        	  alsa_dbg("es8323 in master mode");
            iface |= 0x80;
            break;
        case SND_SOC_DAIFMT_CBS_CFS:    // SLAVE MODE
        	  alsa_dbg("es8323 in slave mode");
            iface &= 0x7F;
            break;
        default:
            return -EINVAL;
    }


    /* interface format */
    switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
        case SND_SOC_DAIFMT_I2S:
            adciface &= 0xFC;
            //daciface &= 0xF9;  //updated by david-everest,5-25           
            daciface &= 0xF9;
            break;
        case SND_SOC_DAIFMT_RIGHT_J:
            break;
        case SND_SOC_DAIFMT_LEFT_J:
            break;
        case SND_SOC_DAIFMT_DSP_A:
            break;
        case SND_SOC_DAIFMT_DSP_B:
            break;
        default:
            return -EINVAL;
    }

    /* clock inversion */
    switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
        case SND_SOC_DAIFMT_NB_NF:
            iface    &= 0xDF;
            adciface &= 0xDF;
            //daciface &= 0xDF;    //UPDATED BY david-everest,5-25        
            daciface &= 0xBF;
            break;
        case SND_SOC_DAIFMT_IB_IF:
            iface    |= 0x20;
            //adciface &= 0xDF;    //UPDATED BY david-everest,5-25
            adciface |= 0x20;
            //daciface &= 0xDF;   //UPDATED BY david-everest,5-25
            daciface |= 0x40;
            break;
        case SND_SOC_DAIFMT_IB_NF:
            iface    |= 0x20;
           // adciface |= 0x40;  //UPDATED BY david-everest,5-25
            adciface &= 0xDF;
            //daciface |= 0x40;  //UPDATED BY david-everest,5-25
            daciface &= 0xBF;
            break;
        case SND_SOC_DAIFMT_NB_IF:
            iface    &= 0xDF;
            adciface |= 0x20;
            //daciface |= 0x20;  //UPDATED BY david-everest,5-25
            daciface |= 0x40;
            break;
        default:
            return -EINVAL;
    }

    snd_soc_write(codec, ES8323_IFACE    , iface);
    snd_soc_write(codec, ES8323_ADC_IFACE, adciface);
    snd_soc_write(codec, ES8323_DAC_IFACE, daciface);

    return 0;
}

static int es8323_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct es8323_priv *es8323 = snd_soc_codec_get_drvdata(codec);
        // u16 i;
	if (!es8323->is_startup) {
		es8323->is_startup = 1;
		//on_off_ext_amp(0);
    /*
		snd_soc_write(codec, ES8323_CONTROL1, 0x06);
		snd_soc_write(codec, ES8323_CONTROL2, 0x72);
		snd_soc_write(codec, ES8323_DACPOWER, 0x00);
		mdelay(30);
	  //snd_soc_write(codec, ES8323_CHIPPOWER, 0xf3);
		snd_soc_write(codec, ES8323_DACCONTROL21, 0x80);
		*/
		snd_soc_write(codec, ES8323_ADCPOWER, 0x59);
	 	snd_soc_write(codec, ES8323_DACPOWER, 0x3c);
		snd_soc_write(codec, ES8323_CHIPPOWER, 0x00);
		//on_off_ext_amp(1);
	}

	DBG("Enter::%s----%d  es8323->sysclk=%d\n",__FUNCTION__,__LINE__,es8323->sysclk);

	/* The set of sample rates that can be supported depends on the
	 * MCLK supplied to the CODEC - enforce this.
	 */
	if (!es8323->sysclk) {
		dev_err(codec->dev,
			"No MCLK configured, call set_sysclk() on init\n");
		return -EINVAL;
	}

	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   es8323->sysclk_constraints);

	return 0;
}

static int es8323_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{ 

        static int codecfirstuse=0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct es8323_priv *es8323 = snd_soc_codec_get_drvdata(codec);
	//u16 iface = snd_soc_read(codec, es8323_IFACE) & 0x1f3;
	//u16 srate = snd_soc_read(codec, es8323_SRATE) & 0x180;
	
	u16 srate    = snd_soc_read(codec, ES8323_IFACE) & 0x80;
	u16 adciface = snd_soc_read(codec, ES8323_ADC_IFACE) & 0xE3;
	u16 daciface = snd_soc_read(codec, ES8323_DAC_IFACE) & 0xC7;
	
	int coeff;

	coeff = get_coeff(es8323->sysclk, params_rate(params));
	if (coeff < 0) {
		coeff = get_coeff(es8323->sysclk / 2, params_rate(params));
		srate |= 0x40;
	}
	if (coeff < 0) {
		dev_err(codec->dev,
			"Unable to configure sample rate %dHz with %dHz MCLK\n",
			params_rate(params), es8323->sysclk);
		return coeff;
	}

	/* bit size */
 switch (params_format(params)) {
  case SNDRV_PCM_FORMAT_S16_LE:
      adciface |= 0x000C;
      daciface |= 0x0018;
      break;
  case SNDRV_PCM_FORMAT_S20_3LE:
      adciface |= 0x0004;
      daciface |= 0x0008;
      break;
  case SNDRV_PCM_FORMAT_S24_LE:
      break;
  case SNDRV_PCM_FORMAT_S32_LE:
      adciface |= 0x0010;
      daciface |= 0x0020;
      break;
  }

  /* set iface & srate*/
  snd_soc_write(codec, ES8323_DAC_IFACE, daciface); //dac bits length
  snd_soc_write(codec, ES8323_ADC_IFACE, adciface); //adc bits length

	if (coeff >= 0)
		{
		 snd_soc_write(codec, ES8323_IFACE, srate);  //bclk div,mclkdiv2
		 snd_soc_write(codec, ES8323_ADCCONTROL5, coeff_div[coeff].sr | (coeff_div[coeff].usb) << 4);
		 snd_soc_write(codec, ES8323_DACCONTROL2, coeff_div[coeff].sr | (coeff_div[coeff].usb) << 4);
		}
	if (codecfirstuse == 0)
		{
			snd_soc_write(codec, ES8323_LOUT2_VOL, es8323_DEF_VOL);//0x1c);   // 
		  snd_soc_write(codec, ES8323_ROUT2_VOL, es8323_DEF_VOL);//0x1c);   // 
		  codecfirstuse=1;
			}

	return 0;
}

static int es8323_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	// u16 mute_reg = snd_soc_read(codec, ES8323_DACCONTROL3) & 0xfb;

	DBG("Enter::%s----%d--mute=%d\n",__FUNCTION__,__LINE__,mute);

	if (mute)
		//snd_soc_write(codec, ES8323_DACCONTROL3, mute_reg | 0x4);
	 {
  snd_soc_write(codec, ES8323_DACCONTROL3, 0x06);//0xe6);

	 }
	else
	{

   	snd_soc_write(codec, ES8323_DACCONTROL3, 0x02);//0xe2);
		snd_soc_write(codec, 0x30,0x1e);
		snd_soc_write(codec, 0x31,0x1e);
	 

	}
    on_off_ext_amp(!mute);
    
	return 0;
}

/////////////////////////////////////////////////////////////////
static int es8323_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct es8323_priv *es8323 = snd_soc_codec_get_drvdata(codec);
	// u16 OUT_VOL = snd_soc_read(codec, ES8323_LOUT1_VOL);
        // u16 i;
        
	DBG("Enter::%s----%d level =%d\n",__FUNCTION__,__LINE__,level);
	switch (level) {
	case SND_SOC_BIAS_ON:
		es8323->is_biason = 1;
		break;
	case SND_SOC_BIAS_PREPARE:
  	snd_soc_write(codec, ES8323_ANAVOLMANAG, 0x7C);
  	snd_soc_write(codec, ES8323_CHIPLOPOW1, 0x00);
  	snd_soc_write(codec, ES8323_CHIPLOPOW2, 0xFF);							
		snd_soc_write(codec, ES8323_CHIPPOWER, 0x00);	
		snd_soc_write(codec, ES8323_ADCPOWER, 0x59);
		break;
	case SND_SOC_BIAS_STANDBY:		
  	snd_soc_write(codec, ES8323_ANAVOLMANAG, 0x7C);
  	snd_soc_write(codec, ES8323_CHIPLOPOW1, 0x00);
  	snd_soc_write(codec, ES8323_CHIPLOPOW2, 0xFF);							
		snd_soc_write(codec, ES8323_CHIPPOWER, 0x00);	
		snd_soc_write(codec, ES8323_ADCPOWER, 0x59);
		break;
	case SND_SOC_BIAS_OFF:	
		snd_soc_write(codec, ES8323_ANAVOLMANAG, 0x7B);
  	snd_soc_write(codec, ES8323_CHIPLOPOW1, 0xFF);
  	snd_soc_write(codec, ES8323_CHIPLOPOW2, 0xFF);		
		snd_soc_write(codec, ES8323_ADCPOWER, 0xFF);					
  	snd_soc_write(codec, ES8323_CHIPPOWER, 0xAA);

  	//snd_soc_write(codec, 0x2b, 0x90);
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}



#define es8323_RATES SNDRV_PCM_RATE_8000_96000

#define es8323_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops es8323_ops = {
	.startup = es8323_pcm_startup,
	.hw_params = es8323_pcm_hw_params,
	.set_fmt = es8323_set_dai_fmt,
	.set_sysclk = es8323_set_dai_sysclk,
	.digital_mute = es8323_mute,
};

static struct snd_soc_dai_driver es8323_dai = {
	.name = "ES8323 HiFi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = es8323_RATES,
		.formats = es8323_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = es8323_RATES,
		.formats = es8323_FORMATS,
	 },
	.ops = &es8323_ops,
	.symmetric_rates = 1,
};

static int es8323_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	// u16 i;
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

#if 0
        snd_soc_write(codec, 0x19, 0x06);
        snd_soc_write(codec, 0x07, 0x7B);
        snd_soc_write(codec, 0x06, 0xFF);
        snd_soc_write(codec, 0x05, 0xFF);
#endif

        snd_soc_write(codec, 0x19, 0x06);
        snd_soc_write(codec, 0x30, 0x00);
        snd_soc_write(codec, 0x31, 0x00);
				snd_soc_write(codec, ES8323_ADCPOWER, 0xFF);					
				snd_soc_write(codec, ES8323_DACPOWER, 0xc0);  	
				snd_soc_write(codec, ES8323_CHIPPOWER, 0xF3);
				snd_soc_write(codec, 0x00, 0x00);
				snd_soc_write(codec, 0x01, 0x58);
				snd_soc_write(codec, 0x2b, 0x9c);	
				msleep(50);
				gpio_set_value(SPK_CON, 0);
				return 0;
}

static int es8323_resume(struct snd_soc_codec *codec)
{
	// u16 i;
	// u8 data[2];
	// u16 *cache = codec->reg_cache;	
	snd_soc_write(codec, 0x2b, 0x80);	
  snd_soc_write(codec, 0x01, 0x50);
  snd_soc_write(codec, 0x00, 0x32);
	snd_soc_write(codec, ES8323_CHIPPOWER, 0x00);	
	snd_soc_write(codec, ES8323_DACPOWER, 0x0c);	
	snd_soc_write(codec, ES8323_ADCPOWER, 0x59);
  snd_soc_write(codec, 0x31, es8323_DEF_VOL);
  snd_soc_write(codec, 0x30, es8323_DEF_VOL);
	snd_soc_write(codec, 0x19, 0x02);			
	gpio_set_value(SPK_CON, 1);
	return 0;
}

static u32 cur_reg=0;
static struct snd_soc_codec *es8323_codec;
static int entry_read(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int len;

	snd_soc_write(es8323_codec, ES8323_ADCPOWER, 0xff);
	snd_soc_write(es8323_codec, ES8323_DACPOWER, 0xf0);
	snd_soc_write(es8323_codec, ES8323_DACPOWER, 0xc0);
	snd_soc_write(es8323_codec, ES8323_CHIPPOWER, 0xf3);

	len = sprintf(page, "es8323 suspend...\n");

	return len ;
}

#if (RT5633_SPK_TIMER == 1)
static void spk_work_handler(struct work_struct *work)
{
	//if(!gpio_get_value(ES8323_HP_PIN)){
		//gpio_direction_output(SPK_CON,0);
 //               gpio_set_value(SPK_CON, 0);
//	}else{
		//gpio_direction_output(SPK_CON,1);	
 //               gpio_set_value(SPK_CON, 1);
//	}
}
void spk_timer_callback(unsigned long data )
{	
	int ret = 0;
	schedule_work(&spk_work);
  ret = mod_timer(&spk_timer, jiffies + msecs_to_jiffies(1000));
  if (ret) printk("Error in mod_timer\n");
}
#endif

static int es8323_probe(struct snd_soc_codec *codec)
{
	// struct es8323_priv *es8323 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret = 0;
	unsigned long flags=0;
	// u16 reg,i;

    printk("%s\n", __func__);
#if 0
    ret = gpio_request(RK30_PIN0_PC7, NULL);
    if (ret != 0) {
        printk("%s request RK30_PIN0_PC7 error", __func__);
        return ret;
    }
    gpio_direction_input(RK30_PIN0_PC7);
#endif
    ret = gpio_request(SPK_CON, NULL);
    if (ret != 0) {
            printk("%s request SPK_CON error", __func__);
            return ret;
    }
    //gpio_set_value(SPK_CON, 1);
    gpio_direction_output(SPK_CON,0);


		ret = gpio_request(HP_DET, NULL);
		if (ret != 0) {
				printk("%s request HP_DET error", __func__);
				return ret;
		}
		gpio_direction_input(HP_DET);
		
		flags = gpio_get_value(HP_DET) ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;
		
		HP_IRQ = gpio_to_irq(HP_DET);
    if (HP_IRQ){
        ret = request_irq(HP_IRQ, hp_det_irq_handler, flags, "ES8323", NULL);
        if(ret == 0){
            printk("%s:register ISR (irq=%d)\n", __FUNCTION__,HP_IRQ);
        }
        else 
			printk("request_irq HP_IRQ failed\n");
    }
    
	if (codec == NULL) {
		dev_err(codec->dev, "Codec device not registered\n");
		return -ENODEV;
	}
    codec->read  = es8323_read_reg_cache;
    codec->write = es8323_write;
    codec->hw_write = (hw_write_t)i2c_master_send;
	codec->control_data = container_of(codec->dev, struct i2c_client, dev);

	es8323_codec = codec;
	ret = es8323_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		return ret;
	}
	#if (RT5633_SPK_TIMER == 1)
	setup_timer( &spk_timer, spk_timer_callback, 0 );
	ret = mod_timer( &spk_timer, jiffies + msecs_to_jiffies(5000) );
	if (ret)
		printk("Error in mod_timer\n");
	INIT_WORK(&spk_work, spk_work_handler);
	es8323_ANVOL=1;
#endif
	
  //es8323_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
  
#if 1	
    //snd_soc_write(codec, 0x35  , 0xa0); 
    //snd_soc_write(codec, 0x36  , 0x08); //for 1.8V VDD
snd_soc_write(codec, 0x02,0xf3);
snd_soc_write(codec, 0x2B,0x80);
snd_soc_write(codec, 0x08,0x00);   //ES8388 salve  
snd_soc_write(codec, 0x00,0x32);   //
snd_soc_write(codec, 0x01,0x72);   //PLAYBACK & RECORD Mode,EnRefr=1
snd_soc_write(codec, 0x03,0x59);   //pdn_ana=0,ibiasgen_pdn=0
snd_soc_write(codec, 0x05,0x00);   //pdn_ana=0,ibiasgen_pdn=0
snd_soc_write(codec, 0x06,0xc3);   //pdn_ana=0,ibiasgen_pdn=0 
snd_soc_write(codec, 0x09,0x88);  //ADC L/R PGA =  +24dB
//----------------------------------------------------------------------------------------------------------------
snd_soc_write(codec, 0x0a,0xf0);  //ADC INPUT=LIN2/RIN2
snd_soc_write(codec, 0x0b,0x82);  //ADC INPUT=LIN2/RIN2 //82
//-----------------------------------------------------------------------------------------------------------------
snd_soc_write(codec, 0x0C,0x4c);  //I2S-24BIT
snd_soc_write(codec, 0x0d,0x02);  //MCLK/LRCK=256 
snd_soc_write(codec, 0x10,0x00);  //ADC Left Volume=0db
snd_soc_write(codec, 0x11,0x00);  //ADC Right Volume=0db
snd_soc_write(codec, 0x12,0xea); // ALC stereo MAXGAIN: 35.5dB,  MINGAIN: +6dB (Record Volume increased!)
snd_soc_write(codec, 0x13,0xc0);
snd_soc_write(codec, 0x14,0x05);
snd_soc_write(codec, 0x15,0x06);
snd_soc_write(codec, 0x16,0x53);  
snd_soc_write(codec, 0x17,0x18);  //I2S-16BIT
snd_soc_write(codec, 0x18,0x02);
snd_soc_write(codec, 0x1A,0x00);  //DAC VOLUME=0DB
snd_soc_write(codec, 0x1B,0x00);
                /*
                snd_soc_write(codec, 0x1E,0x01);    //for 47uF capacitors ,15db Bass@90Hz,Fs=44100
                snd_soc_write(codec, 0x1F,0x84);
                snd_soc_write(codec, 0x20,0xED);
                snd_soc_write(codec, 0x21,0xAF);
                snd_soc_write(codec, 0x22,0x20);
                snd_soc_write(codec, 0x23,0x6C);
                snd_soc_write(codec, 0x24,0xE9);
                snd_soc_write(codec, 0x25,0xBE);
                */
snd_soc_write(codec, 0x26,0x12);  //Left DAC TO Left IXER
snd_soc_write(codec, 0x27,0xb8);  //Left DAC TO Left MIXER
snd_soc_write(codec, 0x28,0x38);
snd_soc_write(codec, 0x29,0x38);
snd_soc_write(codec, 0x2A,0xb8);
snd_soc_write(codec, 0x02,0x00); //aa //START DLL and state-machine,START DSM 
snd_soc_write(codec, 0x19,0x02);  //SOFT RAMP RATE=32LRCKS/STEP,Enable ZERO-CROSS CHECK,DAC MUTE
snd_soc_write(codec, 0x04,0x0c);   //pdn_ana=0,ibiasgen_pdn=0  
msleep(100);
snd_soc_write(codec, 0x2e,0x00); 
snd_soc_write(codec, 0x2f,0x00);
snd_soc_write(codec, 0x30,0x08); 
snd_soc_write(codec, 0x31,0x08);
msleep(200);
snd_soc_write(codec, 0x30,0x0f); 
snd_soc_write(codec, 0x31,0x0f);
msleep(200);
snd_soc_write(codec, 0x30,0x18); 
snd_soc_write(codec, 0x31,0x18);
msleep(100);
snd_soc_write(codec, 0x04,0x2c);   //pdn_ana=0,ibiasgen_pdn=0 
#endif	
	
  //s8323_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
  //codec->dapm.bias_level = SND_SOC_BIAS_STANDBY;
      
	snd_soc_add_controls(codec, es8323_snd_controls,
				ARRAY_SIZE(es8323_snd_controls));
	snd_soc_dapm_new_controls(dapm, es8323_dapm_widgets,
				  ARRAY_SIZE(es8323_dapm_widgets));
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));
	  
	create_proc_read_entry("es8323_suspend", 0644, NULL, entry_read, NULL);

	return 0;
}

static int es8323_remove(struct snd_soc_codec *codec)
{
	es8323_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

#ifdef CONFIG_MACH_RK_FAC
void es8323_codec_set_spk(bool on)
{
	if(es8323_hdmi_ctrl)
	{
 set_spk = on;
  gpio_set_value(SPK_CON, on);
	}
}
#else
void codec_set_spk(bool on)
{
	DBG("Enter::%s----%d--, on = %d\n",__FUNCTION__,__LINE__, on);

        set_spk = on;
        gpio_set_value(SPK_CON, on);
	//return;
}
#endif 

static struct snd_soc_codec_driver soc_codec_dev_es8323 = {
	.probe =	es8323_probe,
	.remove =	es8323_remove,
	.suspend =	es8323_suspend,
	.resume =	es8323_resume,
	.set_bias_level = es8323_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(es8323_reg),
	.reg_word_size = sizeof(u16),
	.reg_cache_default = es8323_reg,
	//------------------------------------------
	//.volatile_register = es8323_volatile_register,
	//.readable_register = es8323_readable_register,
	.reg_cache_step = 1,
#if 0
	.controls = es8323_snd_controls,
	.num_controls = ARRAY_SIZE(es8323_snd_controls),	
  .dapm_routes = audio_map,  
  .num_dapm_routes = ARRAY_SIZE(audio_map), 
  .dapm_widgets = es8323_dapm_widgets,  
  .num_dapm_widgets = ARRAY_SIZE(es8323_dapm_widgets),   
  	
	//--------------------------------------------------	
	.read	= es8323_read_reg_cache,
	.write = es8323_write,	
#endif
};

#if defined(CONFIG_SPI_MASTER)
static int __devinit es8323_spi_probe(struct spi_device *spi)
{
	struct es8323_priv *es8323;
	int ret;

	es8323 = kzalloc(sizeof(struct es8323_priv), GFP_KERNEL);
	if (es8323 == NULL)
		return -ENOMEM;

	es8323->control_type = SND_SOC_SPI;
	spi_set_drvdata(spi, es8323);

	ret = snd_soc_register_codec(&spi->dev,
			&soc_codec_dev_es8323, &es8323_dai, 1);
	if (ret < 0)
		kfree(es8323);
	return ret;
}

static int __devexit es8323_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	kfree(spi_get_drvdata(spi));
	return 0;
}

static struct spi_driver es8323_spi_driver = {
	.driver = {
		.name	= "ES8323",
		.owner	= THIS_MODULE,
	},
	.probe		= es8323_spi_probe,
	.remove		= __devexit_p(es8323_spi_remove),
};
#endif /* CONFIG_SPI_MASTER */

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static ssize_t es8323_show(struct device *dev, struct device_attribute *attr, char *_buf)
{
	return sprintf(_buf, "%s(): get 0x%04x=0x%04x\n", __FUNCTION__, cur_reg, 
		snd_soc_read(es8323_codec, cur_reg));
}

static u32 strtol(const char *nptr, int base)
{
	u32 ret;
	if(!nptr || (base!=16 && base!=10 && base!=8))
	{

		printk("%s(): NULL pointer input\n", __FUNCTION__);
		return -1;
	}
	for(ret=0; *nptr; nptr++)
	{


		if((base==16 && *nptr>='A' && *nptr<='F') || 
			(base==16 && *nptr>='a' && *nptr<='f') || 
			(base>=10 && *nptr>='0' && *nptr<='9') ||
			(base>=8 && *nptr>='0' && *nptr<='7') )
		{
			ret *= base;
			if(base==16 && *nptr>='A' && *nptr<='F')
				ret += *nptr-'A'+10;
			else if(base==16 && *nptr>='a' && *nptr<='f')
				ret += *nptr-'a'+10;
			else if(base>=10 && *nptr>='0' && *nptr<='9')
				ret += *nptr-'0';
			else if(base>=8 && *nptr>='0' && *nptr<='7')
				ret += *nptr-'0';
		}
		else
			return ret;
	}
	return ret;
}

static ssize_t es8323_store(struct device *dev,
					struct device_attribute *attr,
					const char *_buf, size_t _count)
{
	const char * p=_buf;
	u32 reg, val;
	
	if(!strncmp(_buf, "get", strlen("get")))
	{
		p+=strlen("get");
		cur_reg=(u32)strtol(p, 16);
		val=snd_soc_read(es8323_codec, cur_reg);
		printk("%s(): get 0x%04x=0x%04x\n", __FUNCTION__, cur_reg, val);
	}
	else if(!strncmp(_buf, "put", strlen("put")))
	{
		p+=strlen("put");
		reg=strtol(p, 16);
		p=strchr(_buf, '=');
		if(p)
		{
			++ p;
			val=strtol(p, 16);
			snd_soc_write(es8323_codec, reg, val);
			printk("%s(): set 0x%04x=0x%04x\n", __FUNCTION__, reg, val);
		}
		else
			printk("%s(): Bad string format input!\n", __FUNCTION__);
	}
	else
		printk("%s(): Bad string format input!\n", __FUNCTION__);
	
	return _count;
} 

static struct device *es8323_dev = NULL;
static struct class *es8323_class = NULL;
static DEVICE_ATTR(es8323, 0664, es8323_show, es8323_store);
static __devinit int es8323_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	
	struct es8323_priv *es8323;
	int ret = -1;
	struct i2c_adapter *adapter = to_i2c_adapter(i2c->dev.parent);
	char reg;
	char tmp;

	 if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
        dev_warn(&adapter->dev,
        	 "I2C-Adapter doesn't support I2C_FUNC_I2C\n");
        return -EIO;
    }

	es8323 = kzalloc(sizeof(struct es8323_priv), GFP_KERNEL);
	if (es8323 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, es8323);
	es8323->control_type = SND_SOC_I2C;
	
	reg = ES8323_DACCONTROL18;
	ret = i2c_master_reg8_recv(i2c, reg, &tmp, 1 ,200 * 1000);
	//ret =i2c_master_reg8_recv(client, 0x00, buf, 2, 200*1000);//i2c_write_bytes(client, &test_data, 1);	//Test I2C connection.
	if (ret < 0){
				printk("es8323 probe error\n");
				kfree(es8323);
				return ret;
	}
	
	printk("es8323 probe i2c recv ok\n");

	ret =  snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_es8323, &es8323_dai, 1);
	if (ret < 0) {
		kfree(es8323);
		return ret;
	}
	es8323_class = class_create(THIS_MODULE, "es8323");
	if (IS_ERR(es8323_class))
	{
		printk("Create class audio_es8323.\n");
		return -ENOMEM;
	}
	es8323_dev = device_create(es8323_class, NULL, MKDEV(0, 1), NULL, "dev");
	ret = device_create_file(es8323_dev, &dev_attr_es8323);
        if (ret < 0)
                printk("failed to add dev_attr_es8323 file\n");
  #ifdef CONFIG_MACH_RK_FAC              
  	es8323_hdmi_ctrl=1;
  #endif 

	return ret;
}

static __devexit int es8323_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id es8323_i2c_id[] = {
	{ "es8323", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, es8323_i2c_id);

void es8323_i2c_shutdown(struct i2c_client *client)
{
        printk("Chenzy-------hkw-------%s\n", __func__);
        gpio_direction_output(SPK_CON,0);

        snd_soc_write(es8323_codec, ES8323_CONTROL2, 0x58);	
	snd_soc_write(es8323_codec, ES8323_CONTROL1, 0x32);					
  	snd_soc_write(es8323_codec, ES8323_CHIPPOWER, 0xf3);
  	snd_soc_write(es8323_codec, ES8323_DACPOWER, 0xc0);

  	snd_soc_write(es8323_codec, ES8323_DACCONTROL26, 0x00);
  	snd_soc_write(es8323_codec, ES8323_DACCONTROL27, 0x00);

	snd_soc_write(es8323_codec, ES8323_CONTROL1, 0x30);					
	snd_soc_write(es8323_codec, ES8323_CONTROL1, 0x34);					

        mdelay(100);
}
#define  I2C_CLK_NAME  GPIO0B0_I2S8CHCLK_NAME
#define  I2C_CLK_GPIO_MODE  GPIO0B_GPIO0B0
#define  I2C_GPIO_OUTPUT  GPIO_LOW
#define  I2C_CLK_CLK_MODE   GPIO0B_I2S_8CH_CLK
#define  I2C_CLK_GPIO   RK30_PIN0_PB0

#define  I2C_MCLK_NAME  GPIO0B1_I2S8CHSCLK_NAME
#define  I2C_MCLK_GPIO_MODE  GPIO0B_GPIO0B1
#define  I2C_MGPIO_OUTPUT  GPIO_LOW
#define  I2C_MCLK_CLK_MODE   GPIO0B_I2S_8CH_SCLK
#define  I2C_MCLK_GPIO   RK30_PIN0_PB1
static int   es8323_i2c_suspend (struct i2c_client *client, pm_message_t mesg)
{
#if 0
	rk30_mux_api_set(I2C_CLK_NAME,I2C_CLK_GPIO_MODE);
	if (gpio_request(I2C_CLK_GPIO, NULL)) {
		printk("func %s, line %d: request gpio fail\n", __FUNCTION__, __LINE__);
		return -1;
	}

	gpio_direction_output(I2C_CLK_GPIO,I2C_GPIO_OUTPUT);

	rk30_mux_api_set(I2C_MCLK_NAME,I2C_MCLK_GPIO_MODE);
	if (gpio_request(I2C_MCLK_GPIO, NULL)) {
		printk("func %s, line %d: request gpio fail\n", __FUNCTION__, __LINE__);
		return -1;
	}

	gpio_direction_output(I2C_MCLK_GPIO,I2C_MGPIO_OUTPUT);
#endif
        iomux_set(GPIO1_C1);
        if (gpio_request(RK30_PIN1_PC1, NULL)) {
		printk("func %s, line %d: request gpio fail\n", __FUNCTION__, __LINE__);
		return -1;
	}
        gpio_direction_input(RK30_PIN1_PC1);
        gpio_pull_updown(RK30_PIN1_PC1, PullDisable);

#if 0
        iomux_set(GPIO1_C2);
        gpio_direction_input(RK30_PIN1_PC2);
        gpio_pull_updown(RK30_PIN1_PC2, PullDisable);

        iomux_set(GPIO1_C3);
        gpio_direction_input(RK30_PIN1_PC3);
        gpio_pull_updown(RK30_PIN1_PC3, PullDisable);

        iomux_set(GPIO1_C4);
        gpio_direction_input(RK30_PIN1_PC4);
        gpio_pull_updown(RK30_PIN1_PC4, PullDisable);

        iomux_set(GPIO1_C5);
        gpio_direction_input(RK30_PIN1_PC5);
        gpio_pull_updown(RK30_PIN1_PC5, PullDisable);
#endif

	return 0;
}

static int   es8323_i2c_resume(struct i2c_client *client)
{
#if 0
	gpio_free(I2C_MCLK_GPIO);
	gpio_free(I2C_CLK_GPIO);

	rk30_mux_api_set(I2C_MCLK_NAME,I2C_MCLK_CLK_MODE);
	rk30_mux_api_set(I2C_CLK_NAME,I2C_CLK_CLK_MODE);
#endif

        gpio_free(RK30_PIN1_PC1);
        iomux_set(I2S0_SCLK);

	return 0;
}

static struct i2c_driver es8323_i2c_driver = {
	.driver = {
		.name = "ES8323",
		.owner = THIS_MODULE,
	},
	.probe =    es8323_i2c_probe,
	.remove =   __devexit_p(es8323_i2c_remove),
	.shutdown = es8323_i2c_shutdown,
	.suspend  = es8323_i2c_suspend,
	.resume = es8323_i2c_resume,
	.id_table = es8323_i2c_id,
};
#endif

static int __init es8323_modinit(void)
{
	return i2c_add_driver(&es8323_i2c_driver);
}
module_init(es8323_modinit);

static void __exit es8323_exit(void)
{

//	if(0 == tcsi_get_value(TCSI_CODEC_ES8323))
//		return;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&es8323_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&es8323_spi_driver);
#endif
}
module_exit(es8323_exit);


MODULE_DESCRIPTION("ASoC es8323 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");

