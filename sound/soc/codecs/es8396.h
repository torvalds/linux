/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ES8396.h  --  ES8396 ALSA SoC Audio Codec
 *
 * Based on alc5632.h by Arnaud Patard
 */

#ifndef _ES8396_H
#define _ES8396_H

/* THE REGISTER DEFINITION FORMAT            */
/* ES8396_REGISTER NAME_REG_REGISTER ADDRESS */

/* write 0x01 to Register 0x00 will reset all registers of codec.
 * Register 0x00 must be cleared before normal
*/
#define ES8396_RESET_REG00			0x00

/* Clock Scheme Register definition */
/* Register 0x01 for MCLK source selection */
#define ES8396_CLK_SRC_SEL_REG01		0x01
/* Register 0x02 for PLL power down/up, reset, divider and divider dither */
#define ES8396_PLL_CTRL_1_REG02			0x02
/* Register 0x03 for PLL low power mode and PLL power supply selection */
#define ES8396_PLL_CTRL_2_REG03			0x03
/* Register 0x04 for PLL N cofficient, must be in 5 to 13 range*/
#define ES8396_PLL_N_REG04			0x04
/* Register 0x05-0x07 for PLL k cofficient*/
#define ES8396_PLL_K2_REG05			0x05
#define ES8396_PLL_K1_REG06			0x06
#define ES8396_PLL_K0_REG07			0x07
/* Register 0x08 for ADC,DAC CHARGE PUMP and CLASS D clock switch*/
#define ES8396_CLK_CTRL_REG08			0x08
/* Register 0x09 for ADC MCLK divider*/
#define ES8396_ADC_CLK_DIV_REG09		0x09
 /* Register 0x0A for DAC MCLK divider*/
#define ES8396_DAC_CLK_DIV_REG0A		0x0A
 /* Register 0x0B for CHARGE PUMP CLOCK divider*/
#define ES8396_CP_CLK_DIV_REG0B			0x0B
/* Register 0x0C for CLASS D Amplifier Clock divider*/
#define ES8396_DAMP_CLK_DIV_REG0C		0x0C
/* Register 0x0D for DLL control and DAC MCLK SELECTION*/
#define ES8396_DLL_CTRL_REG0D			0x0D
/* Register 0x0E for BCLK divider1 in I2S BUS Master mode*/
#define ES8396_BCLK_DIV_M1_REG0E		0x0E
/* Register 0x0F for BCLK divider2 in I2S BUS Master mode*/
#define ES8396_BCLK_DIV_M2_REG0F		0x0F
/* Register 0x10 for LRCK divider3 in I2S BUS Master mode*/
#define ES8396_LRCK_DIV_M3_REG10		0x10
/* Register 0x11 for LRCK divider4 in I2S BUS Master mode*/
#define ES8396_LRCK_DIV_M4_REG11		0x11

/* PAD MUX REGISTER DEFINITION */
/* Register 0x12 for SDP1 Master or slave mode*/
#define ES8396_SDP_1_MS_REG12			0x12
/* Register 0x13 for SDP2 Master or slave mode*/
#define ES8396_SDP_2_MS_REG13			0x13
/* Register 0x14 for SDP1 Master or slave mode*/
#define ES8396_SDP_3_MS_REG14			0x14
/* Register 0x15 for ADLRCK or GPIO control*/
#define ES8396_ALRCK_GPIO_SEL_REG15		0x15

/* GPIO REGISTER DEFINITION */
/* Register 0x16 for GPIO interrupt*/
#define ES8396_GPIO_IRQ_REG16			0x16
/* Register 0x17 for GPIO STATUS*/
#define ES8396_GPIO_STA_REG17			0x17

/*  Digital Mixer Register Definition  */
/* Register 0x18 for Digital Mixer Source*/
#define ES8396_DMIX_SRC_1_REG18		        0x18
/* Register 0x19 for Digital Mixer Source*/
#define ES8396_DMIX_SRC_2_REG19		        0x19
/* Register 0x1A for DAC Digital Source and SDP1 Digital Output Source*/
#define ES8396_DAC_SRC_SDP1O_SRC_REG1A	0x1A
/* Register 0x1B for SDP2 and SDP3 Digital Output Source*/
#define ES8396_SDP2O_SDP3O_SRC_REG1B		0x1B
/* Register 0x1C for EQ CLOCK and OSR Selection*/
#define ES8396_EQ_CLK_OSR_SEL_REG1C		0x1C
/* Register 0x1D for Address of shared register map*/
#define ES8396_SHARED_ADDR_REG1D		0x1D
/* Register 0x1E for DATA of shared register map*/
#define ES8396_SHARED_DATA_REG1E		0x1E

/*  Serial AUDIO Interface Register Definition  */
/* Register 0x1F for SDP1 INPUT FORMAT*/
#define ES8396_SDP1_IN_FMT_REG1F		0x1F
/* Register 0x20 for SDP1 OUTPUT FORMAT*/
#define ES8396_SDP1_OUT_FMT_REG20		0x20
/* Register 0x21 for SDP1 Digital GAIN AND TDM MODE*/
#define ES8396_SDP1_DGAIN_TDM_REG21		0x21
/* Register 0x22 for SDP2 INPUT FORMAT*/
#define ES8396_SDP2_IN_FMT_REG22		0x22
/* Register 0x23 for SDP2 OUTPUT FORMAT*/
#define ES8396_SDP2_OUT_FMT_REG23		0x23
/* Register 0x24 for SDP3 INPUT FORMAT*/
#define ES8396_SDP3_IN_FMT_REG24		0x24
/* Register 0x25 for SDP3 OUTPUT FORMAT*/
#define ES8396_SDP3_OUT_FMT_REG25		0x25

/* SPEAKER MIXER Register Definition */
/* Register 0x26 for SPK MIXER*/
#define ES8396_SPK_MIXER_REG26		        0x26
/* Register 0x27 for SPK MIXER BOOST GAIN*/
#define ES8396_SPK_MIXER_BOOST_REG27		0x27
/* Register 0x28 for SPK MIXER VOLUME*/
#define ES8396_SPK_MIXER_VOL_REG28		0x28
/* Register 0x29 for SPK MIXER REFERENCE AND LOW POWER MODE*/
#define ES8396_SPK_MIXER_REF_LP_REG29		0x29

/*  HP MIXER Register Definition  */
/* Register 0x2A for HP MIXER*/
#define ES8396_HP_MIXER_REG2A		        0x2A
/* Register 0x2B for HP MIXER BOOST GAIN*/
#define ES8396_HP_MIXER_BOOST_REG2B		0x2B
/* Register 0x2C for HP MIXER VOLUME*/
#define ES8396_HP_MIXER_VOL_REG2C		0x2C
/* Register 0x2D for HP MIXER REFERENCE AND LOW POWER MODE*/
#define ES8396_HP_MIXER_REF_LP_REG2D		0x2D

/*  AX MIXER Register Definition  */
/* Register 0x2E for AX MIXER*/
#define ES8396_AX_MIXER_REG2E		        0x2E
/* Register 0x2F for AX MIXER BOOST GAIN*/
#define ES8396_AX_MIXER_BOOST_REG2F		0x2F
/* Register 0x30 for AX MIXER VOLUME*/
#define ES8396_AX_MIXER_VOL_REG30		0x30
/* Register 0x31 for AX MIXER REFERENCE AND LOW POWER MODE*/
#define ES8396_AX_MIXER_REF_LP_REG31		0x31

/*  LN MIXER Register Definition  */
/* Register 0x32 for LN MIXER*/
#define ES8396_LN_MIXER_REG32		        0x32
/* Register 0x33 for LN MIXER BOOST GAIN*/
#define ES8396_LN_MIXER_BOOST_REG33		0x33
/* Register 0x34 for LN MIXER VOLUME*/
#define ES8396_LN_MIXER_VOL_REG34		0x34
/* Register 0x35 for LN MIXER REFERENCE AND LOW POWER MODE*/
#define ES8396_LN_MIXER_REF_LP_REG35		0x35

/*  MN MIXER Register Definition  */
/* Register 0x36 for MN MIXER*/
#define ES8396_MN_MIXER_REG36		        0x36
/* Register 0x37 for MN MIXER BOOST GAIN*/
#define ES8396_MN_MIXER_BOOST_REG37		0x37
/* Register 0x38 for MN MIXER VOLUME*/
#define ES8396_MN_MIXER_VOL_REG38		0x38
/* Register 0x39 for MN MIXER REFERENCE AND LOW POWER MODE*/
#define ES8396_MN_MIXER_REF_LP_REG39		0x39

/*  SPKD Register Definition  */
/* Register 0x3A for CLASS D control and SOURCE SELECTION*/
#define ES8396_SPK_CTRL_SRC_REG3A		0x3A
/* Register 0x3B for CLASS D Enabled and Volume Control*/
#define ES8396_SPK_EN_VOL_REG3B		        0x3B
/* Register 0x3C for CLASS D CONTROL */
#define ES8396_SPK_CTRL_1_REG3C		        0x3C
/* Register 0x3D for CLASS D CONTROL*/
#define ES8396_SPK_CTRL_2_REG3D		        0x3D

/*  CPHP Register Definition  */
/* Register 0x3E for CPHP HPL ICAL VALUE*/
#define ES8396_CPHP_HPL_ICAL_REG3E		0x3E
/* Register 0x3F for CPHP HPR ICAL VALUE*/
#define ES8396_CPHP_HPR_ICAL_REG3F		0x3F
/* Register 0x40 for CPHP ENABLE */
#define ES8396_CPHP_ENABLE_REG40		0x40
/* Register 0x41 for CPHP VOLUME AND ICAL ENABLE*/
#define ES8396_CPHP_ICAL_VOL_REG41		0x41
/* Register 0x42 for CPHP CONTROL */
#define ES8396_CPHP_CTRL_1_REG42		0x42
/* Register 0x43 for CPHP CONTROL*/
#define ES8396_CPHP_CTRL_2_REG43		0x43
/* Register 0x44 for CPHP CONTROL*/
#define ES8396_CPHP_CTRL_3_REG44		0x44

/*  MONOHP Register Definition  */
/* Register 0x45 for MONOHP REFERENCE AND LOW POWER MODE*/
#define ES8396_MONOHP_REF_LP_REG45		0x45
/* Register 0x46 for MONOHP N MIXER*/
#define ES8396_MONOHP_N_MIXER_REG46		0x46
/* Register 0x47 for MONOHP P MIXER */
#define ES8396_MONOHP_P_MIXER_REG47		0x47
/* Register 0x48 for MONOHP P BOOST AND MUTE CONTROL*/
#define ES8396_MONOHP_P_BOOST_MUTE_REG48	0x48
/* Register 0x49 for MONOHP N BOOST AND MUTE CONTROL */
#define ES8396_MONOHP_N_BOOST_MUTE_REG49	0x49

/*  LNOUT Register Definition  */
/* Register 0x4A for LNOUT LOUT1 ENABLE AND MIXER*/
#define ES8396_LNOUT_LO1EN_LO1MIX_REG4A		0x4A
/* Register 0x4B for LNOUT ROUT1 ENABLE AND MIXER*/
#define ES8396_LNOUT_RO1EN_RO1MIX_REG4B		0x4B
/* Register 0x4C for LNOUT LOUT2 ENABLE AND MIXER*/
#define ES8396_LNOUT_LO2EN_LO2MIX_REG4C		0x4C
/* Register 0x4D for LNOUT ROUT2 ENABLE AND MIXER*/
#define ES8396_LNOUT_RO2EN_RO2MIX_REG4D		0x4D
/* Register 0x4E for LNOUT LOUT1 GAIN CONTROL */
#define ES8396_LNOUT_LO1_GAIN_CTRL_REG4E	0x4E
/* Register 0x4F for LNOUT ROUT1 GAIN CONTROL */
#define ES8396_LNOUT_RO1_GAIN_CTRL_REG4F	0x4F
/* Register 0x50 for LNOUT LOUT2 GAIN CONTROL */
#define ES8396_LNOUT_LO2_GAIN_CTRL_REG50	0x50
/* Register 0x51 for LNOUT ROUT2 GAIN CONTROL */
#define ES8396_LNOUT_RO2_GAIN_CTRL_REG51	0x51
/* Register 0x52 for LNOUT REFERENCE */
#define ES8396_LNOUT_REFERENCE_REG52	        0x52

/*  ADC Register Definition  */
/* Register 0x53 for ADC CHIP STATE MACHINE and Digital Control*/
#define ES8396_ADC_CSM_REG53			0x53
/* Register 0x54 for ADC DMIC and Ramp Rate*/
#define ES8396_ADC_DMIC_RAMPRATE_REG54		0x54
/* Register 0x55 for ADC HIGH PASS FILTER,U-LAW/A-LAW COMPMODE,DATA SELECTION*/
#define ES8396_ADC_HPF_COMP_DASEL_REG55         0x55
/* Register 0x56 for ADC LEFT ADC VOLUME*/
#define ES8396_ADC_LADC_VOL_REG56		0x56
/* Register 0x57 for ADC RIGHT ADC VOLUME */
#define ES8396_ADC_RADC_VOL_REG57		0x57
/* Register 0x58 for ADC ALC CONTROL 1*/
#define ES8396_ADC_ALC_CTRL_1_REG58	        0x58
/* Register 0x59 for ADC ALC CONTROL 2 */
#define ES8396_ADC_ALC_CTRL_2_REG59		0x59
/* Register 0x5A for ADC ALC CONTROL 3 */
#define ES8396_ADC_ALC_CTRL_3_REG5A	        0x5A
/* Register 0x5B for ADC ALC CONTROL 4 */
#define ES8396_ADC_ALC_CTRL_4_REG5B	        0x5B
/* Register 0x5C for ADC ALC CONTROL 5*/
#define ES8396_ADC_ALC_CTRL_5_REG5C		0x5C
/* Register 0x5D for ADC ALC CONTROL 6*/
#define ES8396_ADC_ALC_CTRL_6_REG5D		0x5D
/* Register 0x5E for ADC ANALOG CONTROL*/
#define ES8396_ADC_ANALOG_CTRL_REG5E		0x5E
/* Register 0x5F for ADC LOW POWER MODE AND REFERENCE*/
#define ES8396_ADC_LP_REFERENCE_REG5F		0x5F
/* Register 0x60 for ADC MIC BOOST */
#define ES8396_ADC_MICBOOST_REG60		0x60
/* Register 0x61 for ADC L/R PGA GAIN */
#define ES8396_ADC_PGA_GAIN_REG61		0x61
/* Register 0x62 for ADC LPGA MIXER */
#define ES8396_ADC_LPGA_MIXER_REG62		0x62
/* Register 0x63 for ADC RPGA MIXER */
#define ES8396_ADC_RPGA_MIXER_REG63	        0x63
/* Register 0x64 for ADC LNMUX */
#define ES8396_ADC_LN_MUX_REG64			0x64
/* Register 0x65 for ADC AXMUX */
#define ES8396_ADC_AX_MUX_REG65			0x65

/*  DAC Register Definition  */
/* Register 0x66 for DAC CHIP STATE MACHINE AND MUTE CONTROL*/
#define ES8396_DAC_CSM_REG66			0x66
/* Register 0x67 for DAC RAMPE RATE AND MONO/ZERO CONTROL*/
#define ES8396_DAC_RAMP_RATE_REG67	        0x67
/* Register 0x68 for DAC STEREO ENHANCEMENT */
#define ES8396_DAC_STEREO_ENHANCE_REG68		0x68
/* Register 0x69 for DAC JACK DETECTION AND U/A-LAW COMPRESS */
#define ES8396_DAC_JACK_DET_COMP_REG69		0x69
/* Register 0x6A for DAC LEFT DAC VOLUME */
#define ES8396_DAC_LDAC_VOL_REG6A	        0x6A
/* Register 0x6B for DAC RIGHT DAC VOLUME */
#define ES8396_DAC_RDAC_VOL_REG6B	        0x6B
/* Register 0x6C for DAC LIMITER CONTROL 1 */
#define ES8396_DAC_DPL_CTRL_1_REG6C	        0x6C
/* Register 0x6D for DAC LIMITER CONTROL 2 */
#define ES8396_DAC_DPL_CTRL_2_REG6D	        0x6D
/* Register 0x6E for DAC REFERENCE AND POWER CONTROL */
#define ES8396_DAC_REF_PWR_CTRL_REG6E		0x6E
/* Register 0x6F for DAC DC OFFSET CALIBRATION */
#define ES8396_DAC_OFFSET_CALI_REG6F		0x6F

/* SYSTEM Register Definition  */
/* Register 0x70 for CHIP ANALOG CONTROL,
 * SUCH AS ANALOG POWER CONTROL, AVDDLDO POWER CONTROL */
#define ES8396_SYS_CHIP_ANA_CTL_REG70		0x70
/* Register 0x71 for VMID SELECTION AND REFERENCE */
#define ES8396_SYS_VMID_REF_REG71	        0x71
/* Register 0x72 for VSEL 1 */
#define ES8396_SYS_VSEL_1_REG72			0x72
/* Register 0x73 for VSEL 2 */
#define ES8396_SYS_VSEL_2_REG73			0x73
/* Register 0x74 for MICBIAS CONTROL */
#define ES8396_SYS_MICBIAS_CTRL_REG74		0x74
/* Register 0x75 for MIC ENABLE AND IBIASGEN SELECTION*/
#define ES8396_SYS_MIC_IBIAS_EN_REG75		0x75

/* undocumented */
/* Write 0XA0 TO REG0X76 to ENABLE TEST MODE*/
#define ES8396_TEST_MODE_REG76			0x76
#define ES8396_ADC_FORCE_REG77			0x77
#define ES8396_NGTH_REG7A			0X7A
#define ES8396_MAX_REGISTER			0x7F

#define NO_EVENT      0
#define JD_EVENT      1
#define BOT_EVENT     2

#define DET_HEADPHONE 1
#define DET_HEADSET   2

#define BOT_NULL      0
#define BOT_DWN       1

#define MICBIAS_3V    7
#define MICBIAS_2_8V  6
#define MICBIAS_2_5V  1
#define MICBIAS_2_3V  2
#define MICBIAS_2V    4
#define MICBIAS_1_5V  0

#define MIC_AMIC      0
#define MIC_DMIC      1

#define ANA_LDO_3V    3
#define ANA_LDO_2_9V  2
#define ANA_LDO_2_8V  1
#define ANA_LDO_2_7V  0
#define ANA_LDO_2_4V  7
#define ANA_LDO_2_3V  6
#define ANA_LDO_2_2V  5
#define ANA_LDO_2_1V  4

#define SPK_LDO_3_3V  3
#define SPK_LDO_3_2V  2
#define SPK_LDO_3V    1
#define SPK_LDO_2_9V  0
#define SPK_LDO_2_8V  7
#define SPK_LDO_2_6V  6
#define SPK_LDO_2_5V  5
#define SPK_LDO_2_4V  4

#define ES8396_AIF_MUTE			0x40

#define ES8396_SDP1			0
#define ES8396_SDP2			1
#define ES8396_SDP3			2
/*
* es8396 System clock derived from MCLK or BCLK
*/
#define ES8396_CLKID_MCLK		0
#define ES8396_CLKID_BCLK		1
#define ES8396_CLKID_PLLO		2
/*
* PLL clock source
*/
#define ES8396_PLL			0

#define ES8396_PLL_NO_SRC_0		0
#define ES8396_PLL_SRC_FRM_MCLK		1
#define ES8396_PLL_NO_SRC_1		2
#define ES8396_PLL_SRC_FRM_BCLK		3

#define MS_MASTER			(0x24)

#endif
