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

enum aic31xx_type {
	AIC3100	= 0,
	AIC3110 = AIC31XX_STEREO_CLASS_D_BIT,
	AIC3120 = AIC31XX_MINIDSP_BIT,
	AIC3111 = (AIC31XX_STEREO_CLASS_D_BIT | AIC31XX_MINIDSP_BIT),
};

struct aic31xx_pdata {
	enum aic31xx_type codec_type;
	unsigned int gpio_reset;
	int micbias_vg;
};

/* Page Control Register */
#define AIC31XX_PAGECTL				0x00

/* Page 0 Registers */
/* Software reset register */
#define AIC31XX_RESET				0x01
/* OT FLAG register */
#define AIC31XX_OT_FLAG				0x03
/* Clock clock Gen muxing, Multiplexers*/
#define AIC31XX_CLKMUX				0x04
/* PLL P and R-VAL register */
#define AIC31XX_PLLPR				0x05
/* PLL J-VAL register */
#define AIC31XX_PLLJ				0x06
/* PLL D-VAL MSB register */
#define AIC31XX_PLLDMSB				0x07
/* PLL D-VAL LSB register */
#define AIC31XX_PLLDLSB				0x08
/* DAC NDAC_VAL register*/
#define AIC31XX_NDAC				0x0B
/* DAC MDAC_VAL register */
#define AIC31XX_MDAC				0x0C
/* DAC OSR setting register 1, MSB value */
#define AIC31XX_DOSRMSB				0x0D
/* DAC OSR setting register 2, LSB value */
#define AIC31XX_DOSRLSB				0x0E
#define AIC31XX_MINI_DSP_INPOL			0x10
/* Clock setting register 8, PLL */
#define AIC31XX_NADC				0x12
/* Clock setting register 9, PLL */
#define AIC31XX_MADC				0x13
/* ADC Oversampling (AOSR) Register */
#define AIC31XX_AOSR				0x14
/* Clock setting register 9, Multiplexers */
#define AIC31XX_CLKOUTMUX			0x19
/* Clock setting register 10, CLOCKOUT M divider value */
#define AIC31XX_CLKOUTMVAL			0x1A
/* Audio Interface Setting Register 1 */
#define AIC31XX_IFACE1				0x1B
/* Audio Data Slot Offset Programming */
#define AIC31XX_DATA_OFFSET			0x1C
/* Audio Interface Setting Register 2 */
#define AIC31XX_IFACE2				0x1D
/* Clock setting register 11, BCLK N Divider */
#define AIC31XX_BCLKN				0x1E
/* Audio Interface Setting Register 3, Secondary Audio Interface */
#define AIC31XX_IFACESEC1			0x1F
/* Audio Interface Setting Register 4 */
#define AIC31XX_IFACESEC2			0x20
/* Audio Interface Setting Register 5 */
#define AIC31XX_IFACESEC3			0x21
/* I2C Bus Condition */
#define AIC31XX_I2C				0x22
/* ADC FLAG */
#define AIC31XX_ADCFLAG				0x24
/* DAC Flag Registers */
#define AIC31XX_DACFLAG1			0x25
#define AIC31XX_DACFLAG2			0x26
/* Sticky Interrupt flag (overflow) */
#define AIC31XX_OFFLAG				0x27
/* Sticy DAC Interrupt flags */
#define AIC31XX_INTRDACFLAG			0x2C
/* Sticy ADC Interrupt flags */
#define AIC31XX_INTRADCFLAG			0x2D
/* DAC Interrupt flags 2 */
#define AIC31XX_INTRDACFLAG2			0x2E
/* ADC Interrupt flags 2 */
#define AIC31XX_INTRADCFLAG2			0x2F
/* INT1 interrupt control */
#define AIC31XX_INT1CTRL			0x30
/* INT2 interrupt control */
#define AIC31XX_INT2CTRL			0x31
/* GPIO1 control */
#define AIC31XX_GPIO1				0x33

#define AIC31XX_DACPRB				0x3C
/* ADC Instruction Set Register */
#define AIC31XX_ADCPRB				0x3D
/* DAC channel setup register */
#define AIC31XX_DACSETUP			0x3F
/* DAC Mute and volume control register */
#define AIC31XX_DACMUTE				0x40
/* Left DAC channel digital volume control */
#define AIC31XX_LDACVOL				0x41
/* Right DAC channel digital volume control */
#define AIC31XX_RDACVOL				0x42
/* Headset detection */
#define AIC31XX_HSDETECT			0x43
/* ADC Digital Mic */
#define AIC31XX_ADCSETUP			0x51
/* ADC Digital Volume Control Fine Adjust */
#define AIC31XX_ADCFGA				0x52
/* ADC Digital Volume Control Coarse Adjust */
#define AIC31XX_ADCVOL				0x53


/* Page 1 Registers */
/* Headphone drivers */
#define AIC31XX_HPDRIVER			0x9F
/* Class-D Speakear Amplifier */
#define AIC31XX_SPKAMP				0xA0
/* HP Output Drivers POP Removal Settings */
#define AIC31XX_HPPOP				0xA1
/* Output Driver PGA Ramp-Down Period Control */
#define AIC31XX_SPPGARAMP			0xA2
/* DAC_L and DAC_R Output Mixer Routing */
#define AIC31XX_DACMIXERROUTE			0xA3
/* Left Analog Vol to HPL */
#define AIC31XX_LANALOGHPL			0xA4
/* Right Analog Vol to HPR */
#define AIC31XX_RANALOGHPR			0xA5
/* Left Analog Vol to SPL */
#define AIC31XX_LANALOGSPL			0xA6
/* Right Analog Vol to SPR */
#define AIC31XX_RANALOGSPR			0xA7
/* HPL Driver */
#define AIC31XX_HPLGAIN				0xA8
/* HPR Driver */
#define AIC31XX_HPRGAIN				0xA9
/* SPL Driver */
#define AIC31XX_SPLGAIN				0xAA
/* SPR Driver */
#define AIC31XX_SPRGAIN				0xAB
/* HP Driver Control */
#define AIC31XX_HPCONTROL			0xAC
/* MIC Bias Control */
#define AIC31XX_MICBIAS				0xAE
/* MIC PGA*/
#define AIC31XX_MICPGA				0xAF
/* Delta-Sigma Mono ADC Channel Fine-Gain Input Selection for P-Terminal */
#define AIC31XX_MICPGAPI			0xB0
/* ADC Input Selection for M-Terminal */
#define AIC31XX_MICPGAMI			0xB1
/* Input CM Settings */
#define AIC31XX_MICPGACM			0xB2

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
