/*
 * wm8978.h		--  codec driver for WM8978
 *
 * Copyright 2009 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __WM8978_H__
#define __WM8978_H__

/*
 * Register values.
 */
#define WM8978_RESET				0x00
#define WM8978_POWER_MANAGEMENT_1		0x01
#define WM8978_POWER_MANAGEMENT_2		0x02
#define WM8978_POWER_MANAGEMENT_3		0x03
#define WM8978_AUDIO_INTERFACE			0x04
#define WM8978_COMPANDING_CONTROL		0x05
#define WM8978_CLOCKING				0x06
#define WM8978_ADDITIONAL_CONTROL		0x07
#define WM8978_GPIO_CONTROL			0x08
#define WM8978_JACK_DETECT_CONTROL_1		0x09
#define WM8978_DAC_CONTROL			0x0A
#define WM8978_LEFT_DAC_DIGITAL_VOLUME		0x0B
#define WM8978_RIGHT_DAC_DIGITAL_VOLUME		0x0C
#define WM8978_JACK_DETECT_CONTROL_2		0x0D
#define WM8978_ADC_CONTROL			0x0E
#define WM8978_LEFT_ADC_DIGITAL_VOLUME		0x0F
#define WM8978_RIGHT_ADC_DIGITAL_VOLUME		0x10
#define WM8978_EQ1				0x12
#define WM8978_EQ2				0x13
#define WM8978_EQ3				0x14
#define WM8978_EQ4				0x15
#define WM8978_EQ5				0x16
#define WM8978_DAC_LIMITER_1			0x18
#define WM8978_DAC_LIMITER_2			0x19
#define WM8978_NOTCH_FILTER_1			0x1b
#define WM8978_NOTCH_FILTER_2			0x1c
#define WM8978_NOTCH_FILTER_3			0x1d
#define WM8978_NOTCH_FILTER_4			0x1e
#define WM8978_ALC_CONTROL_1			0x20
#define WM8978_ALC_CONTROL_2			0x21
#define WM8978_ALC_CONTROL_3			0x22
#define WM8978_NOISE_GATE			0x23
#define WM8978_PLL_N				0x24
#define WM8978_PLL_K1				0x25
#define WM8978_PLL_K2				0x26
#define WM8978_PLL_K3				0x27
#define WM8978_3D_CONTROL			0x29
#define WM8978_BEEP_CONTROL			0x2b
#define WM8978_INPUT_CONTROL			0x2c
#define WM8978_LEFT_INP_PGA_CONTROL		0x2d
#define WM8978_RIGHT_INP_PGA_CONTROL		0x2e
#define WM8978_LEFT_ADC_BOOST_CONTROL		0x2f
#define WM8978_RIGHT_ADC_BOOST_CONTROL		0x30
#define WM8978_OUTPUT_CONTROL			0x31
#define WM8978_LEFT_MIXER_CONTROL		0x32
#define WM8978_RIGHT_MIXER_CONTROL		0x33
#define WM8978_LOUT1_HP_CONTROL			0x34
#define WM8978_ROUT1_HP_CONTROL			0x35
#define WM8978_LOUT2_SPK_CONTROL		0x36
#define WM8978_ROUT2_SPK_CONTROL		0x37
#define WM8978_OUT3_MIXER_CONTROL		0x38
#define WM8978_OUT4_MIXER_CONTROL		0x39

#define WM8978_CACHEREGNUM			58

/* Clock divider Id's */
enum wm8978_clk_id {
	WM8978_OPCLKRATE,
	WM8978_BCLKDIV,
};

enum wm8978_sysclk_src {
	WM8978_PLL,
	WM8978_MCLK
};

#endif	/* __WM8978_H__ */
