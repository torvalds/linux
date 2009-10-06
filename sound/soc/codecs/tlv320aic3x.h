/*
 * ALSA SoC TLV320AIC3X codec driver
 *
 * Author:      Vladimir Barinov, <vbarinov@embeddedalley.com>
 * Copyright:   (C) 2007 MontaVista Software, Inc., <source@mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _AIC3X_H
#define _AIC3X_H

/* AIC3X register space */
#define AIC3X_CACHEREGNUM		103

/* Page select register */
#define AIC3X_PAGE_SELECT		0
/* Software reset register */
#define AIC3X_RESET			1
/* Codec Sample rate select register */
#define AIC3X_SAMPLE_RATE_SEL_REG	2
/* PLL progrramming register A */
#define AIC3X_PLL_PROGA_REG		3
/* PLL progrramming register B */
#define AIC3X_PLL_PROGB_REG		4
/* PLL progrramming register C */
#define AIC3X_PLL_PROGC_REG		5
/* PLL progrramming register D */
#define AIC3X_PLL_PROGD_REG		6
/* Codec datapath setup register */
#define AIC3X_CODEC_DATAPATH_REG	7
/* Audio serial data interface control register A */
#define AIC3X_ASD_INTF_CTRLA		8
/* Audio serial data interface control register B */
#define AIC3X_ASD_INTF_CTRLB		9
/* Audio serial data interface control register C */
#define AIC3X_ASD_INTF_CTRLC		10
/* Audio overflow status and PLL R value programming register */
#define AIC3X_OVRF_STATUS_AND_PLLR_REG	11
/* Audio codec digital filter control register */
#define AIC3X_CODEC_DFILT_CTRL		12
/* Headset/button press detection register */
#define AIC3X_HEADSET_DETECT_CTRL_A	13
#define AIC3X_HEADSET_DETECT_CTRL_B	14
/* ADC PGA Gain control registers */
#define LADC_VOL			15
#define RADC_VOL			16
/* MIC3 control registers */
#define MIC3LR_2_LADC_CTRL		17
#define MIC3LR_2_RADC_CTRL		18
/* Line1 Input control registers */
#define LINE1L_2_LADC_CTRL		19
#define LINE1R_2_LADC_CTRL		21
#define LINE1R_2_RADC_CTRL		22
#define LINE1L_2_RADC_CTRL		24
/* Line2 Input control registers */
#define LINE2L_2_LADC_CTRL		20
#define LINE2R_2_RADC_CTRL		23
/* MICBIAS Control Register */
#define MICBIAS_CTRL			25

/* AGC Control Registers A, B, C */
#define LAGC_CTRL_A			26
#define LAGC_CTRL_B			27
#define LAGC_CTRL_C			28
#define RAGC_CTRL_A			29
#define RAGC_CTRL_B			30
#define RAGC_CTRL_C			31

/* DAC Power and Left High Power Output control registers */
#define DAC_PWR				37
#define HPLCOM_CFG			37
/* Right High Power Output control registers */
#define HPRCOM_CFG			38
/* DAC Output Switching control registers */
#define DAC_LINE_MUX			41
/* High Power Output Driver Pop Reduction registers */
#define HPOUT_POP_REDUCTION		42
/* DAC Digital control registers */
#define LDAC_VOL			43
#define RDAC_VOL			44
/* High Power Output control registers */
#define LINE2L_2_HPLOUT_VOL		45
#define LINE2R_2_HPROUT_VOL		62
#define PGAL_2_HPLOUT_VOL		46
#define PGAL_2_HPROUT_VOL		60
#define PGAR_2_HPLOUT_VOL		49
#define PGAR_2_HPROUT_VOL		63
#define DACL1_2_HPLOUT_VOL		47
#define DACR1_2_HPROUT_VOL		64
#define HPLOUT_CTRL			51
#define HPROUT_CTRL			65
/* High Power COM control registers */
#define LINE2L_2_HPLCOM_VOL		52
#define LINE2R_2_HPRCOM_VOL		69
#define PGAL_2_HPLCOM_VOL		53
#define PGAR_2_HPLCOM_VOL		56
#define PGAL_2_HPRCOM_VOL		67
#define PGAR_2_HPRCOM_VOL		70
#define DACL1_2_HPLCOM_VOL		54
#define DACR1_2_HPRCOM_VOL		71
#define HPLCOM_CTRL			58
#define HPRCOM_CTRL			72
/* Mono Line Output Plus/Minus control registers */
#define LINE2L_2_MONOLOPM_VOL		73
#define LINE2R_2_MONOLOPM_VOL		76
#define PGAL_2_MONOLOPM_VOL		74
#define PGAR_2_MONOLOPM_VOL		77
#define DACL1_2_MONOLOPM_VOL		75
#define DACR1_2_MONOLOPM_VOL		78
#define MONOLOPM_CTRL			79
/* Line Output Plus/Minus control registers */
#define LINE2L_2_LLOPM_VOL		80
#define LINE2L_2_RLOPM_VOL		87
#define LINE2R_2_LLOPM_VOL		83
#define LINE2R_2_RLOPM_VOL		90
#define PGAL_2_LLOPM_VOL		81
#define PGAL_2_RLOPM_VOL		88
#define PGAR_2_LLOPM_VOL		84
#define PGAR_2_RLOPM_VOL		91
#define DACL1_2_LLOPM_VOL		82
#define DACL1_2_RLOPM_VOL		89
#define DACR1_2_RLOPM_VOL		92
#define DACR1_2_LLOPM_VOL		85
#define LLOPM_CTRL			86
#define RLOPM_CTRL			93
/* GPIO/IRQ registers */
#define AIC3X_STICKY_IRQ_FLAGS_REG	96
#define AIC3X_RT_IRQ_FLAGS_REG		97
#define AIC3X_GPIO1_REG			98
#define AIC3X_GPIO2_REG			99
#define AIC3X_GPIOA_REG			100
#define AIC3X_GPIOB_REG			101
/* Clock generation control register */
#define AIC3X_CLKGEN_CTRL_REG		102

/* Page select register bits */
#define PAGE0_SELECT		0
#define PAGE1_SELECT		1

/* Audio serial data interface control register A bits */
#define BIT_CLK_MASTER          0x80
#define WORD_CLK_MASTER         0x40

/* Codec Datapath setup register 7 */
#define FSREF_44100		(1 << 7)
#define FSREF_48000		(0 << 7)
#define DUAL_RATE_MODE		((1 << 5) | (1 << 6))
#define LDAC2LCH		(0x1 << 3)
#define RDAC2RCH		(0x1 << 1)

/* PLL registers bitfields */
#define PLLP_SHIFT		0
#define PLLQ_SHIFT		3
#define PLLR_SHIFT		0
#define PLLJ_SHIFT		2
#define PLLD_MSB_SHIFT		0
#define PLLD_LSB_SHIFT		2

/* Clock generation register bits */
#define CODEC_CLKIN_PLLDIV	0
#define CODEC_CLKIN_CLKDIV	1
#define PLL_CLKIN_SHIFT		4
#define MCLK_SOURCE		0x0
#define PLL_CLKDIV_SHIFT	0

/* Software reset register bits */
#define SOFT_RESET		0x80

/* PLL progrramming register A bits */
#define PLL_ENABLE		0x80

/* Route bits */
#define ROUTE_ON		0x80

/* Mute bits */
#define UNMUTE			0x08
#define MUTE_ON			0x80

/* Power bits */
#define LADC_PWR_ON		0x04
#define RADC_PWR_ON		0x04
#define LDAC_PWR_ON		0x80
#define RDAC_PWR_ON		0x40
#define HPLOUT_PWR_ON		0x01
#define HPROUT_PWR_ON		0x01
#define HPLCOM_PWR_ON		0x01
#define HPRCOM_PWR_ON		0x01
#define MONOLOPM_PWR_ON		0x01
#define LLOPM_PWR_ON		0x01
#define RLOPM_PWR_ON	0x01

#define INVERT_VOL(val)   (0x7f - val)

/* Default output volume (inverted) */
#define DEFAULT_VOL     INVERT_VOL(0x50)
/* Default input volume */
#define DEFAULT_GAIN    0x20

/* GPIO API */
enum {
	AIC3X_GPIO1_FUNC_DISABLED		= 0,
	AIC3X_GPIO1_FUNC_AUDIO_WORDCLK_ADC	= 1,
	AIC3X_GPIO1_FUNC_CLOCK_MUX		= 2,
	AIC3X_GPIO1_FUNC_CLOCK_MUX_DIV2		= 3,
	AIC3X_GPIO1_FUNC_CLOCK_MUX_DIV4		= 4,
	AIC3X_GPIO1_FUNC_CLOCK_MUX_DIV8		= 5,
	AIC3X_GPIO1_FUNC_SHORT_CIRCUIT_IRQ	= 6,
	AIC3X_GPIO1_FUNC_AGC_NOISE_IRQ		= 7,
	AIC3X_GPIO1_FUNC_INPUT			= 8,
	AIC3X_GPIO1_FUNC_OUTPUT			= 9,
	AIC3X_GPIO1_FUNC_DIGITAL_MIC_MODCLK	= 10,
	AIC3X_GPIO1_FUNC_AUDIO_WORDCLK		= 11,
	AIC3X_GPIO1_FUNC_BUTTON_IRQ		= 12,
	AIC3X_GPIO1_FUNC_HEADSET_DETECT_IRQ	= 13,
	AIC3X_GPIO1_FUNC_HEADSET_DETECT_OR_BUTTON_IRQ	= 14,
	AIC3X_GPIO1_FUNC_ALL_IRQ		= 16
};

enum {
	AIC3X_GPIO2_FUNC_DISABLED		= 0,
	AIC3X_GPIO2_FUNC_HEADSET_DETECT_IRQ	= 2,
	AIC3X_GPIO2_FUNC_INPUT			= 3,
	AIC3X_GPIO2_FUNC_OUTPUT			= 4,
	AIC3X_GPIO2_FUNC_DIGITAL_MIC_INPUT	= 5,
	AIC3X_GPIO2_FUNC_AUDIO_BITCLK		= 8,
	AIC3X_GPIO2_FUNC_HEADSET_DETECT_OR_BUTTON_IRQ = 9,
	AIC3X_GPIO2_FUNC_ALL_IRQ		= 10,
	AIC3X_GPIO2_FUNC_SHORT_CIRCUIT_OR_AGC_IRQ = 11,
	AIC3X_GPIO2_FUNC_HEADSET_OR_BUTTON_PRESS_OR_SHORT_CIRCUIT_IRQ = 12,
	AIC3X_GPIO2_FUNC_SHORT_CIRCUIT_IRQ	= 13,
	AIC3X_GPIO2_FUNC_AGC_NOISE_IRQ		= 14,
	AIC3X_GPIO2_FUNC_BUTTON_PRESS_IRQ	= 15
};

void aic3x_set_gpio(struct snd_soc_codec *codec, int gpio, int state);
int aic3x_get_gpio(struct snd_soc_codec *codec, int gpio);

/* headset detection / button API */

/* The AIC3x supports detection of stereo headsets (GND + left + right signal)
 * and cellular headsets (GND + speaker output + microphone input).
 * It is recommended to enable MIC bias for this function to work properly.
 * For more information, please refer to the datasheet. */
enum {
	AIC3X_HEADSET_DETECT_OFF	= 0,
	AIC3X_HEADSET_DETECT_STEREO	= 1,
	AIC3X_HEADSET_DETECT_CELLULAR   = 2,
	AIC3X_HEADSET_DETECT_BOTH	= 3
};

enum {
	AIC3X_HEADSET_DEBOUNCE_16MS	= 0,
	AIC3X_HEADSET_DEBOUNCE_32MS	= 1,
	AIC3X_HEADSET_DEBOUNCE_64MS	= 2,
	AIC3X_HEADSET_DEBOUNCE_128MS	= 3,
	AIC3X_HEADSET_DEBOUNCE_256MS	= 4,
	AIC3X_HEADSET_DEBOUNCE_512MS	= 5
};

enum {
	AIC3X_BUTTON_DEBOUNCE_0MS	= 0,
	AIC3X_BUTTON_DEBOUNCE_8MS	= 1,
	AIC3X_BUTTON_DEBOUNCE_16MS	= 2,
	AIC3X_BUTTON_DEBOUNCE_32MS	= 3
};

#define AIC3X_HEADSET_DETECT_ENABLED	0x80
#define AIC3X_HEADSET_DETECT_SHIFT	5
#define AIC3X_HEADSET_DETECT_MASK	3
#define AIC3X_HEADSET_DEBOUNCE_SHIFT	2
#define AIC3X_HEADSET_DEBOUNCE_MASK	7
#define AIC3X_BUTTON_DEBOUNCE_SHIFT 	0
#define AIC3X_BUTTON_DEBOUNCE_MASK	3

/* see the enums above for valid parameters to this function */
void aic3x_set_headset_detection(struct snd_soc_codec *codec, int detect,
				 int headset_debounce, int button_debounce);
int aic3x_headset_detected(struct snd_soc_codec *codec);
int aic3x_button_pressed(struct snd_soc_codec *codec);

struct aic3x_setup_data {
	unsigned int gpio_func[2];
};

extern struct snd_soc_dai aic3x_dai;
extern struct snd_soc_codec_device soc_codec_dev_aic3x;

#endif /* _AIC3X_H */
