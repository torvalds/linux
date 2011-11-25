/*
 * rt5621.h  --  RT5621 ALSA SoC audio driver
 *
 * Copyright 2011 Realtek Microelectronics
 * Author: Johnny Hsu <johnnyhsu@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RT5621_H__
#define __RT5621_H__

#define RT5621_RESET						0X00			//RESET CODEC TO DEFAULT
#define RT5621_SPK_OUT_VOL					0X02			//SPEAKER OUT VOLUME
#define RT5621_HP_OUT_VOL					0X04			//HEADPHONE OUTPUT VOLUME
#define RT5621_MONO_AUX_OUT_VOL				0X06			//MONO OUTPUT/AUXOUT VOLUME
#define RT5621_AUXIN_VOL					0X08			//AUXIN VOLUME
#define RT5621_LINE_IN_VOL					0X0A			//LINE IN VOLUME
#define RT5621_STEREO_DAC_VOL				0X0C			//STEREO DAC VOLUME
#define RT5621_MIC_VOL						0X0E			//MICROPHONE VOLUME
#define RT5621_MIC_ROUTING_CTRL				0X10			//MIC ROUTING CONTROL
#define RT5621_ADC_REC_GAIN					0X12			//ADC RECORD GAIN
#define RT5621_ADC_REC_MIXER				0X14			//ADC RECORD MIXER CONTROL
#define RT5621_SOFT_VOL_CTRL_TIME			0X16			//SOFT VOLUME CONTROL TIME
#define RT5621_OUTPUT_MIXER_CTRL			0X1C			//OUTPUT MIXER CONTROL
#define RT5621_MIC_CTRL						0X22			//MICROPHONE CONTROL
#define	RT5621_AUDIO_INTERFACE				0X34			//AUDIO INTERFACE
#define RT5621_STEREO_AD_DA_CLK_CTRL		0X36			//STEREO AD/DA CLOCK CONTROL
#define	RT5621_COMPANDING_CTRL				0X38			//COMPANDING CONTROL
#define	RT5621_PWR_MANAG_ADD1				0X3A			//POWER MANAGMENT ADDITION 1
#define RT5621_PWR_MANAG_ADD2				0X3C			//POWER MANAGMENT ADDITION 2
#define RT5621_PWR_MANAG_ADD3				0X3E			//POWER MANAGMENT ADDITION 3
#define RT5621_ADD_CTRL_REG					0X40			//ADDITIONAL CONTROL REGISTER
#define	RT5621_GLOBAL_CLK_CTRL_REG			0X42			//GLOBAL CLOCK CONTROL REGISTER
#define RT5621_PLL_CTRL						0X44			//PLL CONTROL
#define RT5621_GPIO_OUTPUT_PIN_CTRL			0X4A			//GPIO OUTPUT PIN CONTROL
#define RT5621_GPIO_PIN_CONFIG				0X4C			//GPIO PIN CONFIGURATION
#define RT5621_GPIO_PIN_POLARITY			0X4E			//GPIO PIN POLARITY/TYPE	
#define RT5621_GPIO_PIN_STICKY				0X50			//GPIO PIN STICKY	
#define RT5621_GPIO_PIN_WAKEUP				0X52			//GPIO PIN WAKE UP
#define RT5621_GPIO_PIN_STATUS				0X54			//GPIO PIN STATUS
#define RT5621_GPIO_PIN_SHARING				0X56			//GPIO PIN SHARING
#define	RT5621_OVER_TEMP_CURR_STATUS		0X58			//OVER TEMPERATURE AND CURRENT STATUS
#define RT5621_JACK_DET_CTRL				0X5A			//JACK DETECT CONTROL REGISTER
#define RT5621_MISC_CTRL					0X5E			//MISC CONTROL
#define	RT5621_PSEDUEO_SPATIAL_CTRL			0X60			//PSEDUEO STEREO & SPATIAL EFFECT BLOCK CONTROL
#define RT5621_EQ_CTRL						0X62			//EQ CONTROL
#define RT5621_EQ_MODE_ENABLE				0X66			//EQ MODE CHANGE ENABLE
#define RT5621_AVC_CTRL						0X68			//AVC CONTROL
#define RT5621_HID_CTRL_INDEX				0X6A			//HIDDEN CONTROL INDEX PORT
#define RT5621_HID_CTRL_DATA				0X6C			//HIDDEN CONTROL DATA PORT
#define RT5621_VENDOR_ID1	  		    	0x7C			//VENDOR ID1
#define RT5621_VENDOR_ID2	  		    	0x7E			//VENDOR ID2


//global definition
#define RT_L_MUTE						(0x1<<15)		//MUTE LEFT CONTROL BIT
#define RT_L_ZC							(0x1<<14)		//LEFT ZERO CROSS CONTROL BIT
#define RT_L_SM							(0x1<<13)		//LEFT SOFTMUTE CONTROL BIT
#define RT_R_MUTE						(0x1<<7)		//MUTE RIGHT CONTROL BIT
#define RT_R_ZC							(0x1<<6)		//RIGHT ZERO CROSS CONTROL BIT
#define RT_R_SM							(0x1<<5)		//RIGHT SOFTMUTE CONTROL BIT
#define RT_M_HP_MIXER					(0x1<<15)		//Mute source to HP Mixer
#define RT_M_SPK_MIXER					(0x1<<14)		//Mute source to Speaker Mixer
#define RT_M_MONO_MIXER					(0x1<<13)		//Mute source to Mono Mixer
#define SPK_CLASS_AB						0
#define SPK_CLASS_D							1

//Mic Routing Control(0x10)
#define M_MIC1_TO_HP_MIXER				(0x1<<15)		//Mute MIC1 to HP mixer
#define M_MIC1_TO_SPK_MIXER				(0x1<<14)		//Mute MiC1 to SPK mixer
#define M_MIC1_TO_MONO_MIXER			(0x1<<13)		//Mute MIC1 to MONO mixer
#define MIC1_DIFF_INPUT_CTRL			(0x1<<12)		//MIC1 different input control
#define M_MIC2_TO_HP_MIXER				(0x1<<7)		//Mute MIC2 to HP mixer
#define M_MIC2_TO_SPK_MIXER				(0x1<<6)		//Mute MiC2 to SPK mixer
#define M_MIC2_TO_MONO_MIXER			(0x1<<5)		//Mute MIC2 to MONO mixer
#define MIC2_DIFF_INPUT_CTRL			(0x1<<4)		//MIC2 different input control

//ADC Record Gain(0x12)
#define M_ADC_L_TO_HP_MIXER				(0x1<<15)		//Mute left of ADC to HP Mixer
#define M_ADC_R_TO_HP_MIXER				(0x1<<14)		//Mute right of ADC to HP Mixer
#define M_ADC_L_TO_MONO_MIXER			(0x1<<13)		//Mute left of ADC to MONO Mixer
#define M_ADC_R_TO_MONO_MIXER			(0x1<<12)		//Mute right of ADC to MONO Mixer
#define ADC_L_GAIN_MASK					(0x1f<<7)		//ADC Record Gain Left channel Mask
#define ADC_L_ZC_DET					(0x1<<6)		//ADC Zero-Cross Detector Control
#define ADC_R_ZC_DET					(0x1<<5)		//ADC Zero-Cross Detector Control
#define ADC_R_GAIN_MASK					(0x1f<<0)		//ADC Record Gain Right channel Mask

//ADC Input Mixer Control(0x14)
#define M_MIC1_TO_ADC_L_MIXER				(0x1<<14)		//Mute mic1 to left channel of ADC mixer
#define M_MIC2_TO_ADC_L_MIXER				(0x1<<13)		//Mute mic2 to left channel of ADC mixer
#define M_LINEIN_L_TO_ADC_L_MIXER			(0x1<<12)		//Mute line In left channel to left channel of ADC mixer
#define M_AUXIN_L_TO_ADC_L_MIXER			(0x1<<11)		//Mute aux In left channel to left channel of ADC mixer
#define M_HPMIXER_L_TO_ADC_L_MIXER			(0x1<<10)		//Mute HP mixer left channel to left channel of ADC mixer
#define M_SPKMIXER_L_TO_ADC_L_MIXER			(0x1<<9)		//Mute SPK mixer left channel to left channel of ADC mixer
#define M_MONOMIXER_L_TO_ADC_L_MIXER		(0x1<<8)		//Mute MONO mixer left channel to left channel of ADC mixer
#define M_MIC1_TO_ADC_R_MIXER				(0x1<<6)		//Mute mic1 to right channel of ADC mixer
#define M_MIC2_TO_ADC_R_MIXER				(0x1<<5)		//Mute mic2 to right channel of ADC mixer
#define M_LINEIN_R_TO_ADC_R_MIXER			(0x1<<4)		//Mute lineIn right channel to right channel of ADC mixer
#define M_AUXIN_R_TO_ADC_R_MIXER			(0x1<<3)		//Mute aux In right channel to right channel of ADC mixer
#define M_HPMIXER_R_TO_ADC_R_MIXER			(0x1<<2)		//Mute HP mixer right channel to right channel of ADC mixer
#define M_SPKMIXER_R_TO_ADC_R_MIXER			(0x1<<1)		//Mute SPK mixer right channel to right channel of ADC mixer
#define M_MONOMIXER_R_TO_ADC_R_MIXER		(0x1<<0)		//Mute MONO mixer right channel to right channel of ADC mixer

//Output Mixer Control(0x1C)
#define	SPKOUT_N_SOUR_MASK					(0x3<<14)	
#define	SPKOUT_N_SOUR_LN					(0x2<<14)
#define	SPKOUT_N_SOUR_RP					(0x1<<14)
#define	SPKOUT_N_SOUR_RN					(0x0<<14)
#define SPK_OUTPUT_CLASS_AB					(0x0<<13)
#define SPK_OUTPUT_CLASS_D					(0x1<<13)
#define SPK_CLASS_AB_S_AMP					(0x0<<12)
#define SPK_CALSS_AB_W_AMP					(0x1<<12)
#define SPKOUT_INPUT_SEL_MASK				(0x3<<10)
#define SPKOUT_INPUT_SEL_MONOMIXER			(0x3<<10)
#define SPKOUT_INPUT_SEL_SPKMIXER			(0x2<<10)
#define SPKOUT_INPUT_SEL_HPMIXER			(0x1<<10)
#define SPKOUT_INPUT_SEL_VMID				(0x0<<10)
#define HPL_INPUT_SEL_HPLMIXER				(0x1<<9)
#define HPR_INPUT_SEL_HPRMIXER				(0x1<<8)	
#define MONO_AUX_INPUT_SEL_MASK				(0x3<<6)
#define MONO_AUX_INPUT_SEL_MONO				(0x3<<6)
#define MONO_AUX_INPUT_SEL_SPK				(0x2<<6)
#define MONO_AUX_INPUT_SEL_HP				(0x1<<6)
#define MONO_AUX_INPUT_SEL_VMID				(0x0<<6)

//Micphone Control define(0x22)
#define MIC1		1
#define MIC2		2
#define MIC_BIAS_90_PRECNET_AVDD	1
#define	MIC_BIAS_75_PRECNET_AVDD	2

#define MIC1_BOOST_CTRL_MASK		(0x3<<10)
#define MIC1_BOOST_CTRL_BYPASS		(0x0<<10)
#define MIC1_BOOST_CTRL_20DB		(0x1<<10)
#define MIC1_BOOST_CTRL_30DB		(0x2<<10)
#define MIC1_BOOST_CTRL_40DB		(0x3<<10)

#define MIC2_BOOST_CTRL_MASK		(0x3<<8)
#define MIC2_BOOST_CTRL_BYPASS		(0x0<<8)
#define MIC2_BOOST_CTRL_20DB		(0x1<<8)
#define MIC2_BOOST_CTRL_30DB		(0x2<<8)
#define MIC2_BOOST_CTRL_40DB		(0x3<<8)

#define MICBIAS_VOLT_CTRL_MASK		(0x1<<5)
#define MICBIAS_VOLT_CTRL_90P		(0x0<<5)
#define MICBIAS_VOLT_CTRL_75P		(0x1<<5)

#define MICBIAS_SHORT_CURR_DET_MASK		(0x3)
#define MICBIAS_SHORT_CURR_DET_600UA	(0x0)
#define MICBIAS_SHORT_CURR_DET_1200UA	(0x1)
#define MICBIAS_SHORT_CURR_DET_1800UA	(0x2)

//Audio Interface(0x34)			
#define SDP_MASTER_MODE				(0x0<<15)		//Main I2S interface select Master mode
#define SDP_SLAVE_MODE				(0x1<<15)		//Main I2S interface select Slave mode
#define I2S_PCM_MODE				(0x1<<14)		//PCM    	0:mode A				,1:mode B 												 	
#define MAIN_I2S_BCLK_POL_CTRL		(0x1<<7)		//0:Normal 1:Invert
#define ADC_DATA_L_R_SWAP			(0x1<<5)		//0:ADC data appear at left phase of LRCK
													//1:ADC data appear at right phase of LRCK
#define DAC_DATA_L_R_SWAP			(0x1<<4)		//0:DAC data appear at left phase of LRCK
													//1:DAC data appear at right phase of LRCK	
//Data Length Slection
#define I2S_DL_MASK				(0x3<<2)		//main i2s Data Length mask	
#define I2S_DL_16				(0x0<<2)		//16 bits
#define I2S_DL_20				(0x1<<2)		//20 bits
#define	I2S_DL_24				(0x2<<2)		//24 bits
#define I2S_DL_32				(0x3<<2)		//32 bits
													
//PCM Data Format Selection
#define I2S_DF_MASK				(0x3)			//main i2s Data Format mask
#define I2S_DF_I2S				(0x0)			//I2S FORMAT 
#define I2S_DF_RIGHT			(0x1)			//RIGHT JUSTIFIED format
#define	I2S_DF_LEFT				(0x2)			//LEFT JUSTIFIED  format
#define I2S_DF_PCM				(0x3)			//PCM format

//Stereo AD/DA Clock Control(0x36h)
#define I2S_PRE_DIV_MASK		(0x7<<12)			
#define I2S_PRE_DIV_1			(0x0<<12)			//DIV 1
#define I2S_PRE_DIV_2			(0x1<<12)			//DIV 2
#define I2S_PRE_DIV_4			(0x2<<12)			//DIV 4
#define I2S_PRE_DIV_8			(0x3<<12)			//DIV 8
#define I2S_PRE_DIV_16			(0x4<<12)			//DIV 16
#define I2S_PRE_DIV_32			(0x5<<12)			//DIV 32

#define I2S_SCLK_DIV_MASK		(0x7<<9)			
#define I2S_SCLK_DIV_1			(0x0<<9)			//DIV 1
#define I2S_SCLK_DIV_2			(0x1<<9)			//DIV 2
#define I2S_SCLK_DIV_3			(0x2<<9)			//DIV 3
#define I2S_SCLK_DIV_4			(0x3<<9)			//DIV 4
#define I2S_SCLK_DIV_6			(0x4<<9)			//DIV 6
#define I2S_SCLK_DIV_8			(0x5<<9)			//DIV 8
#define I2S_SCLK_DIV_12			(0x6<<9)			//DIV 12
#define I2S_SCLK_DIV_16			(0x7<<9)			//DIV 16

#define I2S_WCLK_DIV_PRE_MASK		(0xF<<5)			
#define I2S_WCLK_PRE_DIV_1			(0x0<<5)			//DIV 1
#define I2S_WCLK_PRE_DIV_2			(0x1<<5)			//DIV 2
#define I2S_WCLK_PRE_DIV_3			(0x2<<5)			//DIV 3
#define I2S_WCLK_PRE_DIV_4			(0x3<<5)			//DIV 4
#define I2S_WCLK_PRE_DIV_5			(0x4<<5)			//DIV 5
#define I2S_WCLK_PRE_DIV_6			(0x5<<5)			//DIV 6
#define I2S_WCLK_PRE_DIV_7			(0x6<<5)			//DIV 7
#define I2S_WCLK_PRE_DIV_8			(0x7<<5)			//DIV 8
//........................

#define I2S_WCLK_DIV_MASK		(0x7<<2)			
#define I2S_WCLK_DIV_2			(0x0<<2)			//DIV 2
#define I2S_WCLK_DIV_4			(0x1<<2)			//DIV 4
#define I2S_WCLK_DIV_8			(0x2<<2)			//DIV 8
#define I2S_WCLK_DIV_16			(0x3<<2)			//DIV 16
#define I2S_WCLK_DIV_32			(0x4<<2)			//DIV 32

#define ADDA_FILTER_CLK_SEL_256FS	(0<<1)			//256FS
#define ADDA_FILTER_CLK_SEL_384FS	(1<<1)			//384FS

#define ADDA_OSR_SEL_64FS	(0)						//64FS
#define ADDA_OSR_SEL_128FS	(1)						//128FS

//Power managment addition 1 (0x3A),0:Disable,1:Enable
#define PWR_MAIN_I2S_EN				(0x1<<15)
#define PWR_ZC_DET_PD_EN			(0x1<<14)	
#define PWR_MIC1_BIAS_EN			(0x1<<11)
#define PWR_SHORT_CURR_DET_EN		(0x1<<10)
#define PWR_SOFTGEN_EN				(0x1<<8)
#define	PWR_DEPOP_BUF_HP			(0x1<<6)
#define	PWR_HP_OUT_AMP				(0x1<<5)
#define	PWR_HP_OUT_ENH_AMP			(0x1<<4)
#define PWR_DEPOP_BUF_AUX			(0x1<<2)
#define PWR_AUX_OUT_AMP				(0x1<<1)
#define PWR_AUX_OUT_ENH_AMP			(0x1)


//Power managment addition 2(0x3C),0:Disable,1:Enable
#define PWR_CLASS_AB				(0x1<<15)
#define PWR_CLASS_D					(0x1<<14)
#define PWR_VREF					(0x1<<13)
#define PWR_PLL						(0x1<<12)
#define PWR_DAC_REF_CIR				(0x1<<10)
#define PWR_L_DAC_CLK				(0x1<<9)
#define PWR_R_DAC_CLK				(0x1<<8)
#define PWR_L_ADC_CLK_GAIN			(0x1<<7)
#define PWR_R_ADC_CLK_GAIN			(0x1<<6)
#define PWR_L_HP_MIXER				(0x1<<5)
#define PWR_R_HP_MIXER				(0x1<<4)
#define PWR_SPK_MIXER				(0x1<<3)
#define PWR_MONO_MIXER				(0x1<<2)
#define PWR_L_ADC_REC_MIXER			(0x1<<1)
#define PWR_R_ADC_REC_MIXER			(0x1)

//Power managment addition 3(0x3E),0:Disable,1:Enable
#define PWR_MAIN_BIAS				(0x1<<15)
#define PWR_AUXOUT_L_VOL_AMP		(0x1<<14)
#define PWR_AUXOUT_R_VOL_AMP		(0x1<<13)
#define PWR_SPK_OUT					(0x1<<12)
#define PWR_HP_L_OUT_VOL			(0x1<<10)
#define PWR_HP_R_OUT_VOL			(0x1<<9)
#define PWR_LINEIN_L_VOL			(0x1<<7)
#define PWR_LINEIN_R_VOL			(0x1<<6)
#define PWR_AUXIN_L_VOL				(0x1<<5)
#define PWR_AUXIN_R_VOL				(0x1<<4)
#define PWR_MIC1_FUN_CTRL			(0x1<<3)
#define PWR_MIC2_FUN_CTRL			(0x1<<2)
#define PWR_MIC1_BOOST_MIXER		(0x1<<1)
#define PWR_MIC2_BOOST_MIXER		(0x1)


//Additional Control Register(0x40)
#define AUXOUT_SEL_DIFF					(0x1<<15)	//Differential Mode
#define AUXOUT_SEL_SE					(0x1<<15)	//Single-End Mode

#define SPK_AB_AMP_CTRL_MASK			(0x7<<12)
#define SPK_AB_AMP_CTRL_RATIO_225		(0x0<<12)		//2.25 Vdd
#define SPK_AB_AMP_CTRL_RATIO_200		(0x1<<12)		//2.00 Vdd
#define SPK_AB_AMP_CTRL_RATIO_175		(0x2<<12)		//1.75 Vdd
#define SPK_AB_AMP_CTRL_RATIO_150		(0x3<<12)		//1.50 Vdd
#define SPK_AB_AMP_CTRL_RATIO_125		(0x4<<12)		//1.25 Vdd	
#define SPK_AB_AMP_CTRL_RATIO_100		(0x5<<12)		//1.00 Vdd

#define SPK_D_AMP_CTRL_MASK				(0x3<<10)
#define SPK_D_AMP_CTRL_RATIO_175		(0x0<<10)		//1.75 Vdd
#define SPK_D_AMP_CTRL_RATIO_150		(0x1<<10)		//1.50 Vdd	
#define SPK_D_AMP_CTRL_RATIO_125		(0x2<<10)		//1.25 Vdd
#define SPK_D_AMP_CTRL_RATIO_100		(0x3<<10)		//1.00 Vdd

#define STEREO_DAC_HI_PASS_FILTER_EN	(0x1<<9)		//Stereo DAC high pass filter enable
#define STEREO_ADC_HI_PASS_FILTER_EN	(0x1<<8)		//Stereo ADC high pass filter enable

#define DIG_VOL_BOOST_MASK				(0x3<<4)		//Digital volume Boost mask
#define DIG_VOL_BOOST_0DB				(0x0<<4)		//Digital volume Boost 0DB
#define DIG_VOL_BOOST_6DB				(0x1<<4)		//Digital volume Boost 6DB
#define DIG_VOL_BOOST_12DB				(0x2<<4)		//Digital volume Boost 12DB
#define DIG_VOL_BOOST_18DB				(0x3<<4)		//Digital volume Boost 18DB


//Global Clock Control Register(0x42)
#define SYSCLK_SOUR_SEL_MASK			(0x1<<15)
#define SYSCLK_SOUR_SEL_MCLK			(0x0<<15)		//system Clock source from MCLK
#define SYSCLK_SOUR_SEL_PLL				(0x1<<15)		//system Clock source from PLL
#define PLLCLK_SOUR_SEL_MCLK			(0x0<<14)		//PLL clock source from MCLK
#define PLLCLK_SOUR_SEL_BITCLK			(0x1<<14)		//PLL clock source from BITCLK

#define PLLCLK_DIV_RATIO_MASK			(0x3<<1)		
#define PLLCLK_DIV_RATIO_DIV1			(0x0<<1)		//DIV 1
#define PLLCLK_DIV_RATIO_DIV2			(0x1<<1)		//DIV 2
#define PLLCLK_DIV_RATIO_DIV4			(0x2<<1)		//DIV 4
#define PLLCLK_DIV_RATIO_DIV8			(0x3<<1)		//DIV 8

#define PLLCLK_PRE_DIV1					(0x0)			//DIV 1
#define PLLCLK_PRE_DIV2					(0x1)			//DIV 2

//PLL Control(0x44)

#define PLL_CTRL_M_VAL(m)		((m)&0xf)
#define PLL_CTRL_K_VAL(k)		(((k)&0x7)<<4)
#define PLL_CTRL_N_VAL(n)		(((n)&0xff)<<8)

//GPIO Pin Configuration(0x4C)
#define GPIO_PIN_MASK				(0x1<<1)
#define GPIO_PIN_SET_INPUT			(0x1<<1)
#define GPIO_PIN_SET_OUTPUT			(0x0<<1)

//Pin Sharing(0x56)
#define LINEIN_L_PIN_SHARING		(0x1<<15)
#define LINEIN_L_PIN_AS_LINEIN_L	(0x0<<15)
#define LINEIN_L_PIN_AS_JD1			(0x1<<15)

#define LINEIN_R_PIN_SHARING		(0x1<<14)
#define LINEIN_R_PIN_AS_LINEIN_R	(0x0<<14)
#define LINEIN_R_PIN_AS_JD2			(0x1<<14)

#define GPIO_PIN_SHARING			(0x3)
#define GPIO_PIN_AS_GPIO			(0x0)
#define GPIO_PIN_AS_IRQOUT			(0x1)
#define GPIO_PIN_AS_PLLOUT			(0x3)

//Jack Detect Control Register(0x5A)
#define JACK_DETECT_MASK			(0x3<<14)
#define JACK_DETECT_USE_JD2			(0x3<<14)
#define JACK_DETECT_USE_JD1			(0x2<<14)
#define JACK_DETECT_USE_GPIO		(0x1<<14)
#define JACK_DETECT_OFF				(0x0<<14)

#define	SPK_EN_IN_HI				(0x1<<11)
#define AUX_R_EN_IN_HI				(0x1<<10)
#define AUX_L_EN_IN_HI				(0x1<<9)
#define HP_EN_IN_HI					(0x1<<8)
#define	SPK_EN_IN_LO				(0x1<<7)
#define AUX_R_EN_IN_LO				(0x1<<6)
#define AUX_L_EN_IN_LO				(0x1<<5)
#define HP_EN_IN_LO					(0x1<<4)

////MISC CONTROL(0x5E)
#define DISABLE_FAST_VREG			(0x1<<15)
#define SPK_CLASS_AB_OC_PD			(0x1<<13)
#define SPK_CLASS_AB_OC_DET			(0x1<<12)
#define HP_DEPOP_MODE3_EN			(0x1<<10)
#define HP_DEPOP_MODE2_EN			(0x1<<9)
#define HP_DEPOP_MODE1_EN			(0x1<<8)
#define AUXOUT_DEPOP_MODE3_EN		(0x1<<6)
#define AUXOUT_DEPOP_MODE2_EN		(0x1<<5)
#define AUXOUT_DEPOP_MODE1_EN		(0x1<<4)
#define M_DAC_L_INPUT				(0x1<<3)
#define M_DAC_R_INPUT				(0x1<<2)
#define IRQOUT_INV_CTRL				(0x1<<0)

//Psedueo Stereo & Spatial Effect Block Control(0x60)
#define SPATIAL_CTRL_EN				(0x1<<15)
#define ALL_PASS_FILTER_EN			(0x1<<14)
#define PSEUDO_STEREO_EN			(0x1<<13)
#define STEREO_EXPENSION_EN			(0x1<<12)

#define GAIN_3D_PARA_L_MASK			(0x7<<9)
#define GAIN_3D_PARA_L_1_00			(0x0<<9)
#define GAIN_3D_PARA_L_1_25			(0x1<<9)
#define GAIN_3D_PARA_L_1_50			(0x2<<9)
#define GAIN_3D_PARA_L_1_75			(0x3<<9)
#define GAIN_3D_PARA_L_2_00			(0x4<<9)

#define GAIN_3D_PARA_R_MASK			(0x7<<6)
#define GAIN_3D_PARA_R_1_00			(0x0<<6)
#define GAIN_3D_PARA_R_1_25			(0x1<<6)
#define GAIN_3D_PARA_R_1_50			(0x2<<6)
#define GAIN_3D_PARA_R_1_75			(0x3<<6)
#define GAIN_3D_PARA_R_2_00			(0x4<<6)

#define RATIO_3D_L_MASK				(0x3<<4)
#define RATIO_3D_L_0_0				(0x0<<4)
#define RATIO_3D_L_0_66				(0x1<<4)
#define RATIO_3D_L_1_0				(0x2<<4)

#define RATIO_3D_R_MASK				(0x3<<2)
#define RATIO_3D_R_0_0				(0x0<<2)
#define RATIO_3D_R_0_66				(0x1<<2)
#define RATIO_3D_R_1_0				(0x2<<2)

#define APF_MASK					(0x3)
#define APF_FOR_48K					(0x3)
#define APF_FOR_44_1K				(0x2)
#define APF_FOR_32K					(0x1)

//EQ CONTROL(0x62)

#define EN_HW_EQ_BLK			(0x1<<15)		//HW EQ block control
#define EN_HW_EQ_HPF_MODE		(0x1<<14)		//High Frequency shelving filter mode
#define EN_HW_EQ_SOUR			(0x1<<11)		//0:DAC PATH,1:ADC PATH
#define EN_HW_EQ_HPF			(0x1<<4)		//EQ High Pass Filter Control
#define EN_HW_EQ_BP3			(0x1<<3)		//EQ Band-3 Control
#define EN_HW_EQ_BP2			(0x1<<2)		//EQ Band-2 Control
#define EN_HW_EQ_BP1			(0x1<<1)		//EQ Band-1 Control
#define EN_HW_EQ_LPF			(0x1<<0)		//EQ Low Pass Filter Control

//EQ Mode Change Enable(0x66)
#define EQ_HPF_CHANGE_EN		(0x1<<4)		//EQ High Pass Filter Mode Change Enable
#define EQ_BP3_CHANGE_EN		(0x1<<3)		//EQ Band-3 Pass Filter Mode Change Enable
#define EQ_BP2_CHANGE_EN		(0x1<<2)		//EQ Band-2 Pass Filter Mode Change Enable
#define EQ_BP1_CHANGE_EN		(0x1<<1)		//EQ Band-1 Pass Filter Mode Change Enable
#define EQ_LPF_CHANGE_EN		(0x1<<0)		//EQ Low Pass Filter Mode Change Enable


//AVC Control(0x68)
#define AVC_ENABLE				(0x1<<15)
#define AVC_TARTGET_SEL_MASK	(0x1<<14)
#define AVC_TARTGET_SEL_R 		(0x1<<14)
#define AVC_TARTGET_SEL_L		(0x0<<14)


#define RT5621_PLL_FR_MCLK			0
#define RT5621_PLL_FR_BCLK			1


#define REALTEK_HWDEP 0

//WaveOut channel for realtek codec
enum 
{
	RT_WAVOUT_SPK  				=(0x1<<0),
	RT_WAVOUT_SPK_R				=(0x1<<1),
	RT_WAVOUT_SPK_L				=(0x1<<2),
	RT_WAVOUT_HP				=(0x1<<3),
	RT_WAVOUT_HP_R				=(0x1<<4),
	RT_WAVOUT_HP_L				=(0x1<<5),	
	RT_WAVOUT_MONO				=(0x1<<6),
	RT_WAVOUT_AUXOUT			=(0x1<<7),
	RT_WAVOUT_AUXOUT_R			=(0x1<<8),
	RT_WAVOUT_AUXOUT_L			=(0x1<<9),
	RT_WAVOUT_LINEOUT			=(0x1<<10),
	RT_WAVOUT_LINEOUT_R			=(0x1<<11),
	RT_WAVOUT_LINEOUT_L			=(0x1<<12),	
	RT_WAVOUT_DAC				=(0x1<<13),		
	RT_WAVOUT_ALL_ON			=(0x1<<14),
};

//WaveIn channel for realtek codec
enum
{
	RT_WAVIN_R_MONO_MIXER		=(0x1<<0),
	RT_WAVIN_R_SPK_MIXER		=(0x1<<1),
	RT_WAVIN_R_HP_MIXER			=(0x1<<2),
	RT_WAVIN_R_PHONE			=(0x1<<3),
	RT_WAVIN_R_AUXIN			=(0x1<<3),	
	RT_WAVIN_R_LINE_IN			=(0x1<<4),
	RT_WAVIN_R_MIC2				=(0x1<<5),
	RT_WAVIN_R_MIC1				=(0x1<<6),

	RT_WAVIN_L_MONO_MIXER		=(0x1<<8),
	RT_WAVIN_L_SPK_MIXER		=(0x1<<9),
	RT_WAVIN_L_HP_MIXER			=(0x1<<10),
	RT_WAVIN_L_PHONE			=(0x1<<11),
	RT_WAVIN_L_AUXIN			=(0x1<<11),
	RT_WAVIN_L_LINE_IN			=(0x1<<12),
	RT_WAVIN_L_MIC2				=(0x1<<13),
	RT_WAVIN_L_MIC1				=(0x1<<14),
};

enum 
{
	POWER_STATE_D0=0,
	POWER_STATE_D1,
	POWER_STATE_D1_PLAYBACK,
	POWER_STATE_D1_RECORD,
	POWER_STATE_D2,
	POWER_STATE_D2_PLAYBACK,
	POWER_STATE_D2_RECORD,
	POWER_STATE_D3,
	POWER_STATE_D4

}; 

#if REALTEK_HWDEP

struct rt56xx_reg_state
{
	unsigned int reg_index;
	unsigned int reg_value;
};

struct rt56xx_cmd
{
	size_t number;
	struct rt56xx_reg_state __user *buf;		
};

enum 
{
	RT_READ_CODEC_REG_IOCTL = _IOR('R', 0x01, struct rt56xx_cmd),
	RT_READ_ALL_CODEC_REG_IOCTL = _IOR('R', 0x02, struct rt56xx_cmd),
	RT_WRITE_CODEC_REG_IOCTL = _IOW('R', 0x03, struct rt56xx_cmd),
};

#endif

#endif /* __RT5621_H__ */
