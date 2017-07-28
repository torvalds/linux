/*
 * ALSA SoC TLV320AIC31XX codec driver
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
#ifndef _TLV320AIC31XX_H
#define _TLV320AIC31XX_H

#define AIC31XX_RATES	SNDRV_PCM_RATE_8000_192000

#define AIC31XX_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE \
			 | SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_LE \
			 | SNDRV_PCM_FMTBIT_S32_LE)


#define AIC31XX_STEREO_CLASS_D_BIT	0x1
#define AIC31XX_MINIDSP_BIT		0x2
#define DAC31XX_BIT			0x4

enum aic31xx_type {
	AIC3100	= 0,
	AIC3110 = AIC31XX_STEREO_CLASS_D_BIT,
	AIC3120 = AIC31XX_MINIDSP_BIT,
	AIC3111 = (AIC31XX_STEREO_CLASS_D_BIT | AIC31XX_MINIDSP_BIT),
	DAC3100 = DAC31XX_BIT,
	DAC3101 = DAC31XX_BIT | AIC31XX_STEREO_CLASS_D_BIT,
};

struct aic31xx_pdata {
	enum aic31xx_type codec_type;
	unsigned int gpio_reset;
	int micbias_vg;
};

#define AIC31XX_REG(page, reg)	((page * 128) + reg)

/* Page Control Register */
#define AIC31XX_PAGECTL		AIC31XX_REG(0, 0)

/* Page 0 Registers */
/* Software reset register */
#define AIC31XX_RESET		AIC31XX_REG(0, 1)
/* OT FLAG register */
#define AIC31XX_OT_FLAG		AIC31XX_REG(0, 3)
/* Clock clock Gen muxing, Multiplexers*/
#define AIC31XX_CLKMUX		AIC31XX_REG(0, 4)
/* PLL P and R-VAL register */
#define AIC31XX_PLLPR		AIC31XX_REG(0, 5)
/* PLL J-VAL register */
#define AIC31XX_PLLJ		AIC31XX_REG(0, 6)
/* PLL D-VAL MSB register */
#define AIC31XX_PLLDMSB		AIC31XX_REG(0, 7)
/* PLL D-VAL LSB register */
#define AIC31XX_PLLDLSB		AIC31XX_REG(0, 8)
/* DAC NDAC_VAL register*/
#define AIC31XX_NDAC		AIC31XX_REG(0, 11)
/* DAC MDAC_VAL register */
#define AIC31XX_MDAC		AIC31XX_REG(0, 12)
/* DAC OSR setting register 1, MSB value */
#define AIC31XX_DOSRMSB		AIC31XX_REG(0, 13)
/* DAC OSR setting register 2, LSB value */
#define AIC31XX_DOSRLSB		AIC31XX_REG(0, 14)
#define AIC31XX_MINI_DSP_INPOL	AIC31XX_REG(0, 16)
/* Clock setting register 8, PLL */
#define AIC31XX_NADC		AIC31XX_REG(0, 18)
/* Clock setting register 9, PLL */
#define AIC31XX_MADC		AIC31XX_REG(0, 19)
/* ADC Oversampling (AOSR) Register */
#define AIC31XX_AOSR		AIC31XX_REG(0, 20)
/* Clock setting register 9, Multiplexers */
#define AIC31XX_CLKOUTMUX	AIC31XX_REG(0, 25)
/* Clock setting register 10, CLOCKOUT M divider value */
#define AIC31XX_CLKOUTMVAL	AIC31XX_REG(0, 26)
/* Audio Interface Setting Register 1 */
#define AIC31XX_IFACE1		AIC31XX_REG(0, 27)
/* Audio Data Slot Offset Programming */
#define AIC31XX_DATA_OFFSET	AIC31XX_REG(0, 28)
/* Audio Interface Setting Register 2 */
#define AIC31XX_IFACE2		AIC31XX_REG(0, 29)
/* Clock setting register 11, BCLK N Divider */
#define AIC31XX_BCLKN		AIC31XX_REG(0, 30)
/* Audio Interface Setting Register 3, Secondary Audio Interface */
#define AIC31XX_IFACESEC1	AIC31XX_REG(0, 31)
/* Audio Interface Setting Register 4 */
#define AIC31XX_IFACESEC2	AIC31XX_REG(0, 32)
/* Audio Interface Setting Register 5 */
#define AIC31XX_IFACESEC3	AIC31XX_REG(0, 33)
/* I2C Bus Condition */
#define AIC31XX_I2C		AIC31XX_REG(0, 34)
/* ADC FLAG */
#define AIC31XX_ADCFLAG		AIC31XX_REG(0, 36)
/* DAC Flag Registers */
#define AIC31XX_DACFLAG1	AIC31XX_REG(0, 37)
#define AIC31XX_DACFLAG2	AIC31XX_REG(0, 38)
/* Sticky Interrupt flag (overflow) */
#define AIC31XX_OFFLAG		AIC31XX_REG(0, 39)
/* Sticy DAC Interrupt flags */
#define AIC31XX_INTRDACFLAG	AIC31XX_REG(0, 44)
/* Sticy ADC Interrupt flags */
#define AIC31XX_INTRADCFLAG	AIC31XX_REG(0, 45)
/* DAC Interrupt flags 2 */
#define AIC31XX_INTRDACFLAG2	AIC31XX_REG(0, 46)
/* ADC Interrupt flags 2 */
#define AIC31XX_INTRADCFLAG2	AIC31XX_REG(0, 47)
/* INT1 interrupt control */
#define AIC31XX_INT1CTRL	AIC31XX_REG(0, 48)
/* INT2 interrupt control */
#define AIC31XX_INT2CTRL	AIC31XX_REG(0, 49)
/* GPIO1 control */
#define AIC31XX_GPIO1		AIC31XX_REG(0, 50)

#define AIC31XX_DACPRB		AIC31XX_REG(0, 60)
/* ADC Instruction Set Register */
#define AIC31XX_ADCPRB		AIC31XX_REG(0, 61)
/* DAC channel setup register */
#define AIC31XX_DACSETUP	AIC31XX_REG(0, 63)
/* DAC Mute and volume control register */
#define AIC31XX_DACMUTE		AIC31XX_REG(0, 64)
/* Left DAC channel digital volume control */
#define AIC31XX_LDACVOL		AIC31XX_REG(0, 65)
/* Right DAC channel digital volume control */
#define AIC31XX_RDACVOL		AIC31XX_REG(0, 66)
/* Headset detection */
#define AIC31XX_HSDETECT	AIC31XX_REG(0, 67)
/* ADC Digital Mic */
#define AIC31XX_ADCSETUP	AIC31XX_REG(0, 81)
/* ADC Digital Volume Control Fine Adjust */
#define AIC31XX_ADCFGA		AIC31XX_REG(0, 82)
/* ADC Digital Volume Control Coarse Adjust */
#define AIC31XX_ADCVOL		AIC31XX_REG(0, 83)


/* Page 1 Registers */
/* Headphone drivers */
#define AIC31XX_HPDRIVER	AIC31XX_REG(1, 31)
/* Class-D Speakear Amplifier */
#define AIC31XX_SPKAMP		AIC31XX_REG(1, 32)
/* HP Output Drivers POP Removal Settings */
#define AIC31XX_HPPOP		AIC31XX_REG(1, 33)
/* Output Driver PGA Ramp-Down Period Control */
#define AIC31XX_SPPGARAMP	AIC31XX_REG(1, 34)
/* DAC_L and DAC_R Output Mixer Routing */
#define AIC31XX_DACMIXERROUTE	AIC31XX_REG(1, 35)
/* Left Analog Vol to HPL */
#define AIC31XX_LANALOGHPL	AIC31XX_REG(1, 36)
/* Right Analog Vol to HPR */
#define AIC31XX_RANALOGHPR	AIC31XX_REG(1, 37)
/* Left Analog Vol to SPL */
#define AIC31XX_LANALOGSPL	AIC31XX_REG(1, 38)
/* Right Analog Vol to SPR */
#define AIC31XX_RANALOGSPR	AIC31XX_REG(1, 39)
/* HPL Driver */
#define AIC31XX_HPLGAIN		AIC31XX_REG(1, 40)
/* HPR Driver */
#define AIC31XX_HPRGAIN		AIC31XX_REG(1, 41)
/* SPL Driver */
#define AIC31XX_SPLGAIN		AIC31XX_REG(1, 42)
/* SPR Driver */
#define AIC31XX_SPRGAIN		AIC31XX_REG(1, 43)
/* HP Driver Control */
#define AIC31XX_HPCONTROL	AIC31XX_REG(1, 44)
/* MIC Bias Control */
#define AIC31XX_MICBIAS		AIC31XX_REG(1, 46)
/* MIC PGA*/
#define AIC31XX_MICPGA		AIC31XX_REG(1, 47)
/* Delta-Sigma Mono ADC Channel Fine-Gain Input Selection for P-Terminal */
#define AIC31XX_MICPGAPI	AIC31XX_REG(1, 48)
/* ADC Input Selection for M-Terminal */
#define AIC31XX_MICPGAMI	AIC31XX_REG(1, 49)
/* Input CM Settings */
#define AIC31XX_MICPGACM	AIC31XX_REG(1, 50)

/* Bits, masks and shifts */

/* AIC31XX_CLKMUX */
#define AIC31XX_PLL_CLKIN_MASK			0x0c
#define AIC31XX_PLL_CLKIN_SHIFT			2
#define AIC31XX_PLL_CLKIN_MCLK			0
#define AIC31XX_CODEC_CLKIN_MASK		0x03
#define AIC31XX_CODEC_CLKIN_SHIFT		0
#define AIC31XX_CODEC_CLKIN_PLL			3
#define AIC31XX_CODEC_CLKIN_BCLK		1

/* AIC31XX_PLLPR, AIC31XX_NDAC, AIC31XX_MDAC, AIC31XX_NADC, AIC31XX_MADC,
   AIC31XX_BCLKN */
#define AIC31XX_PLL_MASK		0x7f
#define AIC31XX_PM_MASK			0x80

/* AIC31XX_IFACE1 */
#define AIC31XX_WORD_LEN_16BITS		0x00
#define AIC31XX_WORD_LEN_20BITS		0x01
#define AIC31XX_WORD_LEN_24BITS		0x02
#define AIC31XX_WORD_LEN_32BITS		0x03
#define AIC31XX_IFACE1_DATALEN_MASK	0x30
#define AIC31XX_IFACE1_DATALEN_SHIFT	(4)
#define AIC31XX_IFACE1_DATATYPE_MASK	0xC0
#define AIC31XX_IFACE1_DATATYPE_SHIFT	(6)
#define AIC31XX_I2S_MODE		0x00
#define AIC31XX_DSP_MODE		0x01
#define AIC31XX_RIGHT_JUSTIFIED_MODE	0x02
#define AIC31XX_LEFT_JUSTIFIED_MODE	0x03
#define AIC31XX_IFACE1_MASTER_MASK	0x0C
#define AIC31XX_BCLK_MASTER		0x08
#define AIC31XX_WCLK_MASTER		0x04

/* AIC31XX_DATA_OFFSET */
#define AIC31XX_DATA_OFFSET_MASK	0xFF

/* AIC31XX_IFACE2 */
#define AIC31XX_BCLKINV_MASK		0x08
#define AIC31XX_BDIVCLK_MASK		0x03
#define AIC31XX_DAC2BCLK		0x00
#define AIC31XX_DACMOD2BCLK		0x01
#define AIC31XX_ADC2BCLK		0x02
#define AIC31XX_ADCMOD2BCLK		0x03

/* AIC31XX_ADCFLAG */
#define AIC31XX_ADCPWRSTATUS_MASK		0x40

/* AIC31XX_DACFLAG1 */
#define AIC31XX_LDACPWRSTATUS_MASK		0x80
#define AIC31XX_RDACPWRSTATUS_MASK		0x08
#define AIC31XX_HPLDRVPWRSTATUS_MASK		0x20
#define AIC31XX_HPRDRVPWRSTATUS_MASK		0x02
#define AIC31XX_SPLDRVPWRSTATUS_MASK		0x10
#define AIC31XX_SPRDRVPWRSTATUS_MASK		0x01

/* AIC31XX_INTRDACFLAG */
#define AIC31XX_HPSCDETECT_MASK			0x80
#define AIC31XX_BUTTONPRESS_MASK		0x20
#define AIC31XX_HSPLUG_MASK			0x10
#define AIC31XX_LDRCTHRES_MASK			0x08
#define AIC31XX_RDRCTHRES_MASK			0x04
#define AIC31XX_DACSINT_MASK			0x02
#define AIC31XX_DACAINT_MASK			0x01

/* AIC31XX_INT1CTRL */
#define AIC31XX_HSPLUGDET_MASK			0x80
#define AIC31XX_BUTTONPRESSDET_MASK		0x40
#define AIC31XX_DRCTHRES_MASK			0x20
#define AIC31XX_AGCNOISE_MASK			0x10
#define AIC31XX_OC_MASK				0x08
#define AIC31XX_ENGINE_MASK			0x04

/* AIC31XX_DACSETUP */
#define AIC31XX_SOFTSTEP_MASK			0x03

/* AIC31XX_DACMUTE */
#define AIC31XX_DACMUTE_MASK			0x0C

/* AIC31XX_MICBIAS */
#define AIC31XX_MICBIAS_MASK			0x03
#define AIC31XX_MICBIAS_SHIFT			0

#endif	/* _TLV320AIC31XX_H */
