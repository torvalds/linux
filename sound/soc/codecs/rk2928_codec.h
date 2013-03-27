/*
 * rk2928.h ALSA SoC RK2928 codec driver
 *
 * Copyright 2012 Rockchip
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RK2928_CODEC_H__
#define __RK2928_CODEC_H__

#define CODEC_REG_ADC_DIGITAL_GIAN_L 0x08
#define CODEC_REG_ADC_DIGITAL_GIAN_H 0x09

#define CODEC_REG_ADC_PGA_GAIN	0x0b
	#define m_MIC_GAIN_CHANNEL_L		(0x0F << 4)
	#define m_MIC_GAIN_CHANNEL_R		(0x0F)
	#define v_MIC_GAIN_CHANNEL_L(n)		((n) << 4)
	#define v_MIC_GAIN_CHANNEL_R(n)		(n)
	
#define CODEC_REG_POWER			0x0c
	#define m_PD_CODEC			(0x01)
	#define m_PD_MIC_BIAS		(0x01 << 1)
	#define m_PD_ADC_R			(0x01 << 2)
	#define m_PD_ADC_L			(0x01 << 3)
	#define m_PD_ADC			(0x03 << 2)
	#define m_PD_DAC			(0x03 << 4)
	#define v_PD_CODEC(n)		(n)
	#define v_PD_MIC_BIAS(n)	(n << 1)
	#define v_PD_ADC_R(n)		(n << 2)
	#define v_PD_ADC_L(n)		(n << 3)
	#define v_PD_DAC_R(n)		(n << 4)
	#define v_PD_DAC_L(n)		(n << 5)
	#define v_PD_ADC(n)			(v_PD_ADC_L(n) | v_PD_ADC_R(n))
	#define v_PD_DAC(n)			(v_PD_DAC_L(n) | v_PD_DAC_R(n))
	#define v_PWR_OFF			v_PD_DAC_L(1) | v_PD_DAC_R(1) | v_PD_ADC_L(1) | v_PD_ADC_R(1) | v_PD_MIC_BIAS(0) | v_PD_CODEC(1) //²»¹Ø±Õmic_bias for phone_pad
	
#define CODEC_REG_VCM_BIAS		0x0d
	#define v_MIC_BIAS(n)		(n)
	enum {
		VCM_RESISTOR_100K = 0,
		VCM_RESISTOR_25K
	};
	#define v_VCM_25K_100K(n)	(n << 2)
	
#define CODEC_REG_DAC_MUTE		0x0e
	#define v_MUTE_DAC_L(n)		(n << 1)
	#define v_MUTE_DAC_R(n)		(n)
	#define v_MUTE_DAC(n)	v_MUTE_DAC_L(n) | v_MUTE_DAC_R(n)
	
#define CODEC_REG_ADC_SOURCE	0x0f
	enum {
		ADC_SRC_MIC = 0,
		ADC_SRC_LINE_IN
	};
	#define v_SRC_ADC_L(n)		(n << 1)
	#define v_SRC_ADC_R(n)		(n)
	
#define CODEC_REG_DAC_GAIN		0x10
	#define m_GAIN_DAC_L		(0x03 << 2)
	#define m_GAIN_DAC_R		(0x03)
	enum {
		DAC_GAIN_0DB = 0,
		DAC_GAIN_3DB_P = 0x2,	//3db
		DAC_GAIN_3DB_N			//-3db
	};
	#define v_GAIN_DAC_L(n)		(n << 2)
	#define v_GAIN_DAC_R(n)		(n)
	#define v_GAIN_DAC(n)		(v_GAIN_DAC_L(n) | v_GAIN_DAC_R(n))
	
//#ifndef DEBUG
//#define DEBUG
//#endif
	
#ifdef DEBUG
#define DBG(format, ...) \
		printk(KERN_INFO "RK2928 CODEC: " format "\n", ## __VA_ARGS__)
#else
#define DBG(format, ...)
#endif

struct rk2928_codec_pdata {
	int	hpctl;
	int (*hpctl_io_init)(void);	
};

#endif /* __RK2928_CODEC_H__ */
