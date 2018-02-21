// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA SoC TLV320AIC31xx CODEC Driver Definitions
 *
 * Copyright (C) 2014-2017 Texas Instruments Incorporated - http://www.ti.com/
 */

#ifndef _TLV320AIC31XX_H
#define _TLV320AIC31XX_H

#define AIC31XX_RATES	SNDRV_PCM_RATE_8000_192000

#define AIC31XX_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
			 SNDRV_PCM_FMTBIT_S20_3LE | \
			 SNDRV_PCM_FMTBIT_S24_3LE | \
			 SNDRV_PCM_FMTBIT_S24_LE | \
			 SNDRV_PCM_FMTBIT_S32_LE)

#define AIC31XX_STEREO_CLASS_D_BIT	BIT(1)
#define AIC31XX_MINIDSP_BIT		BIT(2)
#define DAC31XX_BIT			BIT(3)

enum aic31xx_type {
	AIC3100	= 0,
	AIC3110 = AIC31XX_STEREO_CLASS_D_BIT,
	AIC3120 = AIC31XX_MINIDSP_BIT,
	AIC3111 = AIC31XX_STEREO_CLASS_D_BIT | AIC31XX_MINIDSP_BIT,
	DAC3100 = DAC31XX_BIT,
	DAC3101 = DAC31XX_BIT | AIC31XX_STEREO_CLASS_D_BIT,
};

struct aic31xx_pdata {
	enum aic31xx_type codec_type;
	unsigned int gpio_reset;
	int micbias_vg;
};

#define AIC31XX_REG(page, reg)	((page * 128) + reg)

#define AIC31XX_PAGECTL		AIC31XX_REG(0, 0) /* Page Control Register */

/* Page 0 Registers */
#define AIC31XX_RESET		AIC31XX_REG(0, 1) /* Software reset register */
#define AIC31XX_OT_FLAG		AIC31XX_REG(0, 3) /* OT FLAG register */
#define AIC31XX_CLKMUX		AIC31XX_REG(0, 4) /* Clock clock Gen muxing, Multiplexers*/
#define AIC31XX_PLLPR		AIC31XX_REG(0, 5) /* PLL P and R-VAL register */
#define AIC31XX_PLLJ		AIC31XX_REG(0, 6) /* PLL J-VAL register */
#define AIC31XX_PLLDMSB		AIC31XX_REG(0, 7) /* PLL D-VAL MSB register */
#define AIC31XX_PLLDLSB		AIC31XX_REG(0, 8) /* PLL D-VAL LSB register */
#define AIC31XX_NDAC		AIC31XX_REG(0, 11) /* DAC NDAC_VAL register*/
#define AIC31XX_MDAC		AIC31XX_REG(0, 12) /* DAC MDAC_VAL register */
#define AIC31XX_DOSRMSB		AIC31XX_REG(0, 13) /* DAC OSR setting register 1, MSB value */
#define AIC31XX_DOSRLSB		AIC31XX_REG(0, 14) /* DAC OSR setting register 2, LSB value */
#define AIC31XX_MINI_DSP_INPOL	AIC31XX_REG(0, 16)
#define AIC31XX_NADC		AIC31XX_REG(0, 18) /* Clock setting register 8, PLL */
#define AIC31XX_MADC		AIC31XX_REG(0, 19) /* Clock setting register 9, PLL */
#define AIC31XX_AOSR		AIC31XX_REG(0, 20) /* ADC Oversampling (AOSR) Register */
#define AIC31XX_CLKOUTMUX	AIC31XX_REG(0, 25) /* Clock setting register 9, Multiplexers */
#define AIC31XX_CLKOUTMVAL	AIC31XX_REG(0, 26) /* Clock setting register 10, CLOCKOUT M divider value */
#define AIC31XX_IFACE1		AIC31XX_REG(0, 27) /* Audio Interface Setting Register 1 */
#define AIC31XX_DATA_OFFSET	AIC31XX_REG(0, 28) /* Audio Data Slot Offset Programming */
#define AIC31XX_IFACE2		AIC31XX_REG(0, 29) /* Audio Interface Setting Register 2 */
#define AIC31XX_BCLKN		AIC31XX_REG(0, 30) /* Clock setting register 11, BCLK N Divider */
#define AIC31XX_IFACESEC1	AIC31XX_REG(0, 31) /* Audio Interface Setting Register 3, Secondary Audio Interface */
#define AIC31XX_IFACESEC2	AIC31XX_REG(0, 32) /* Audio Interface Setting Register 4 */
#define AIC31XX_IFACESEC3	AIC31XX_REG(0, 33) /* Audio Interface Setting Register 5 */
#define AIC31XX_I2C		AIC31XX_REG(0, 34) /* I2C Bus Condition */
#define AIC31XX_ADCFLAG		AIC31XX_REG(0, 36) /* ADC FLAG */
#define AIC31XX_DACFLAG1	AIC31XX_REG(0, 37) /* DAC Flag Registers */
#define AIC31XX_DACFLAG2	AIC31XX_REG(0, 38)
#define AIC31XX_OFFLAG		AIC31XX_REG(0, 39) /* Sticky Interrupt flag (overflow) */
#define AIC31XX_INTRDACFLAG	AIC31XX_REG(0, 44) /* Sticy DAC Interrupt flags */
#define AIC31XX_INTRADCFLAG	AIC31XX_REG(0, 45) /* Sticy ADC Interrupt flags */
#define AIC31XX_INTRDACFLAG2	AIC31XX_REG(0, 46) /* DAC Interrupt flags 2 */
#define AIC31XX_INTRADCFLAG2	AIC31XX_REG(0, 47) /* ADC Interrupt flags 2 */
#define AIC31XX_INT1CTRL	AIC31XX_REG(0, 48) /* INT1 interrupt control */
#define AIC31XX_INT2CTRL	AIC31XX_REG(0, 49) /* INT2 interrupt control */
#define AIC31XX_GPIO1		AIC31XX_REG(0, 51) /* GPIO1 control */
#define AIC31XX_DACPRB		AIC31XX_REG(0, 60)
#define AIC31XX_ADCPRB		AIC31XX_REG(0, 61) /* ADC Instruction Set Register */
#define AIC31XX_DACSETUP	AIC31XX_REG(0, 63) /* DAC channel setup register */
#define AIC31XX_DACMUTE		AIC31XX_REG(0, 64) /* DAC Mute and volume control register */
#define AIC31XX_LDACVOL		AIC31XX_REG(0, 65) /* Left DAC channel digital volume control */
#define AIC31XX_RDACVOL		AIC31XX_REG(0, 66) /* Right DAC channel digital volume control */
#define AIC31XX_HSDETECT	AIC31XX_REG(0, 67) /* Headset detection */
#define AIC31XX_ADCSETUP	AIC31XX_REG(0, 81) /* ADC Digital Mic */
#define AIC31XX_ADCFGA		AIC31XX_REG(0, 82) /* ADC Digital Volume Control Fine Adjust */
#define AIC31XX_ADCVOL		AIC31XX_REG(0, 83) /* ADC Digital Volume Control Coarse Adjust */

/* Page 1 Registers */
#define AIC31XX_HPDRIVER	AIC31XX_REG(1, 31) /* Headphone drivers */
#define AIC31XX_SPKAMP		AIC31XX_REG(1, 32) /* Class-D Speakear Amplifier */
#define AIC31XX_HPPOP		AIC31XX_REG(1, 33) /* HP Output Drivers POP Removal Settings */
#define AIC31XX_SPPGARAMP	AIC31XX_REG(1, 34) /* Output Driver PGA Ramp-Down Period Control */
#define AIC31XX_DACMIXERROUTE	AIC31XX_REG(1, 35) /* DAC_L and DAC_R Output Mixer Routing */
#define AIC31XX_LANALOGHPL	AIC31XX_REG(1, 36) /* Left Analog Vol to HPL */
#define AIC31XX_RANALOGHPR	AIC31XX_REG(1, 37) /* Right Analog Vol to HPR */
#define AIC31XX_LANALOGSPL	AIC31XX_REG(1, 38) /* Left Analog Vol to SPL */
#define AIC31XX_RANALOGSPR	AIC31XX_REG(1, 39) /* Right Analog Vol to SPR */
#define AIC31XX_HPLGAIN		AIC31XX_REG(1, 40) /* HPL Driver */
#define AIC31XX_HPRGAIN		AIC31XX_REG(1, 41) /* HPR Driver */
#define AIC31XX_SPLGAIN		AIC31XX_REG(1, 42) /* SPL Driver */
#define AIC31XX_SPRGAIN		AIC31XX_REG(1, 43) /* SPR Driver */
#define AIC31XX_HPCONTROL	AIC31XX_REG(1, 44) /* HP Driver Control */
#define AIC31XX_MICBIAS		AIC31XX_REG(1, 46) /* MIC Bias Control */
#define AIC31XX_MICPGA		AIC31XX_REG(1, 47) /* MIC PGA*/
#define AIC31XX_MICPGAPI	AIC31XX_REG(1, 48) /* Delta-Sigma Mono ADC Channel Fine-Gain Input Selection for P-Terminal */
#define AIC31XX_MICPGAMI	AIC31XX_REG(1, 49) /* ADC Input Selection for M-Terminal */
#define AIC31XX_MICPGACM	AIC31XX_REG(1, 50) /* Input CM Settings */

/* Bits, masks, and shifts */

/* AIC31XX_CLKMUX */
#define AIC31XX_PLL_CLKIN_MASK		GENMASK(3, 2)
#define AIC31XX_PLL_CLKIN_SHIFT		(2)
#define AIC31XX_PLL_CLKIN_MCLK		0x00
#define AIC31XX_PLL_CLKIN_BCKL		0x01
#define AIC31XX_PLL_CLKIN_GPIO1		0x02
#define AIC31XX_PLL_CLKIN_DIN		0x03
#define AIC31XX_CODEC_CLKIN_MASK	GENMASK(1, 0)
#define AIC31XX_CODEC_CLKIN_SHIFT	(0)
#define AIC31XX_CODEC_CLKIN_MCLK	0x00
#define AIC31XX_CODEC_CLKIN_BCLK	0x01
#define AIC31XX_CODEC_CLKIN_GPIO1	0x02
#define AIC31XX_CODEC_CLKIN_PLL		0x03

/* AIC31XX_PLLPR */
/* AIC31XX_NDAC */
/* AIC31XX_MDAC */
/* AIC31XX_NADC */
/* AIC31XX_MADC */
/* AIC31XX_BCLKN */
#define AIC31XX_PLL_MASK		GENMASK(6, 0)
#define AIC31XX_PM_MASK			BIT(7)

/* AIC31XX_IFACE1 */
#define AIC31XX_IFACE1_DATATYPE_MASK	GENMASK(7, 6)
#define AIC31XX_IFACE1_DATATYPE_SHIFT	(6)
#define AIC31XX_I2S_MODE		0x00
#define AIC31XX_DSP_MODE		0x01
#define AIC31XX_RIGHT_JUSTIFIED_MODE	0x02
#define AIC31XX_LEFT_JUSTIFIED_MODE	0x03
#define AIC31XX_IFACE1_DATALEN_MASK	GENMASK(5, 4)
#define AIC31XX_IFACE1_DATALEN_SHIFT	(4)
#define AIC31XX_WORD_LEN_16BITS		0x00
#define AIC31XX_WORD_LEN_20BITS		0x01
#define AIC31XX_WORD_LEN_24BITS		0x02
#define AIC31XX_WORD_LEN_32BITS		0x03
#define AIC31XX_IFACE1_MASTER_MASK	GENMASK(3, 2)
#define AIC31XX_BCLK_MASTER		BIT(2)
#define AIC31XX_WCLK_MASTER		BIT(3)

/* AIC31XX_DATA_OFFSET */
#define AIC31XX_DATA_OFFSET_MASK	GENMASK(7, 0)

/* AIC31XX_IFACE2 */
#define AIC31XX_BCLKINV_MASK		BIT(3)
#define AIC31XX_BDIVCLK_MASK		GENMASK(1, 0)
#define AIC31XX_DAC2BCLK		0x00
#define AIC31XX_DACMOD2BCLK		0x01
#define AIC31XX_ADC2BCLK		0x02
#define AIC31XX_ADCMOD2BCLK		0x03

/* AIC31XX_ADCFLAG */
#define AIC31XX_ADCPWRSTATUS_MASK	BIT(6)

/* AIC31XX_DACFLAG1 */
#define AIC31XX_LDACPWRSTATUS_MASK	BIT(7)
#define AIC31XX_HPLDRVPWRSTATUS_MASK	BIT(5)
#define AIC31XX_SPLDRVPWRSTATUS_MASK	BIT(4)
#define AIC31XX_RDACPWRSTATUS_MASK	BIT(3)
#define AIC31XX_HPRDRVPWRSTATUS_MASK	BIT(1)
#define AIC31XX_SPRDRVPWRSTATUS_MASK	BIT(0)

/* AIC31XX_INTRDACFLAG */
#define AIC31XX_HPLSCDETECT		BIT(7)
#define AIC31XX_HPRSCDETECT		BIT(6)
#define AIC31XX_BUTTONPRESS		BIT(5)
#define AIC31XX_HSPLUG			BIT(4)
#define AIC31XX_LDRCTHRES		BIT(3)
#define AIC31XX_RDRCTHRES		BIT(2)
#define AIC31XX_DACSINT			BIT(1)
#define AIC31XX_DACAINT			BIT(0)

/* AIC31XX_INT1CTRL */
#define AIC31XX_HSPLUGDET		BIT(7)
#define AIC31XX_BUTTONPRESSDET		BIT(6)
#define AIC31XX_DRCTHRES		BIT(5)
#define AIC31XX_AGCNOISE		BIT(4)
#define AIC31XX_SC			BIT(3)
#define AIC31XX_ENGINE			BIT(2)

/* AIC31XX_DACSETUP */
#define AIC31XX_SOFTSTEP_MASK		GENMASK(1, 0)

/* AIC31XX_DACMUTE */
#define AIC31XX_DACMUTE_MASK		GENMASK(3, 2)

/* AIC31XX_MICBIAS */
#define AIC31XX_MICBIAS_MASK		GENMASK(1, 0)
#define AIC31XX_MICBIAS_SHIFT		0

#endif	/* _TLV320AIC31XX_H */
