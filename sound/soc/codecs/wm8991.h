/*
 * wm8991.h  --  audio driver for WM8991
 *
 * Copyright 2007 Wolfson Microelectronics PLC.
 * Author: Graeme Gregory
 *         graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#ifndef _WM8991_H
#define _WM8991_H

/*
 * Register values.
 */
#define WM8991_RESET                            0x00
#define WM8991_POWER_MANAGEMENT_1               0x01
#define WM8991_POWER_MANAGEMENT_2               0x02
#define WM8991_POWER_MANAGEMENT_3               0x03
#define WM8991_AUDIO_INTERFACE_1                0x04
#define WM8991_AUDIO_INTERFACE_2                0x05
#define WM8991_CLOCKING_1                       0x06
#define WM8991_CLOCKING_2                       0x07
#define WM8991_AUDIO_INTERFACE_3                0x08
#define WM8991_AUDIO_INTERFACE_4                0x09
#define WM8991_DAC_CTRL                         0x0A
#define WM8991_LEFT_DAC_DIGITAL_VOLUME          0x0B
#define WM8991_RIGHT_DAC_DIGITAL_VOLUME         0x0C
#define WM8991_DIGITAL_SIDE_TONE                0x0D
#define WM8991_ADC_CTRL                         0x0E
#define WM8991_LEFT_ADC_DIGITAL_VOLUME          0x0F
#define WM8991_RIGHT_ADC_DIGITAL_VOLUME         0x10
#define WM8991_GPIO_CTRL_1                      0x12
#define WM8991_GPIO1_GPIO2                      0x13
#define WM8991_GPIO3_GPIO4                      0x14
#define WM8991_GPIO5_GPIO6                      0x15
#define WM8991_GPIOCTRL_2                       0x16
#define WM8991_GPIO_POL                         0x17
#define WM8991_LEFT_LINE_INPUT_1_2_VOLUME       0x18
#define WM8991_LEFT_LINE_INPUT_3_4_VOLUME       0x19
#define WM8991_RIGHT_LINE_INPUT_1_2_VOLUME      0x1A
#define WM8991_RIGHT_LINE_INPUT_3_4_VOLUME      0x1B
#define WM8991_LEFT_OUTPUT_VOLUME               0x1C
#define WM8991_RIGHT_OUTPUT_VOLUME              0x1D
#define WM8991_LINE_OUTPUTS_VOLUME              0x1E
#define WM8991_OUT3_4_VOLUME                    0x1F
#define WM8991_LEFT_OPGA_VOLUME                 0x20
#define WM8991_RIGHT_OPGA_VOLUME                0x21
#define WM8991_SPEAKER_VOLUME                   0x22
#define WM8991_CLASSD1                          0x23
#define WM8991_CLASSD3                          0x25
#define WM8991_INPUT_MIXER1                     0x27
#define WM8991_INPUT_MIXER2                     0x28
#define WM8991_INPUT_MIXER3                     0x29
#define WM8991_INPUT_MIXER4                     0x2A
#define WM8991_INPUT_MIXER5                     0x2B
#define WM8991_INPUT_MIXER6                     0x2C
#define WM8991_OUTPUT_MIXER1                    0x2D
#define WM8991_OUTPUT_MIXER2                    0x2E
#define WM8991_OUTPUT_MIXER3                    0x2F
#define WM8991_OUTPUT_MIXER4                    0x30
#define WM8991_OUTPUT_MIXER5                    0x31
#define WM8991_OUTPUT_MIXER6                    0x32
#define WM8991_OUT3_4_MIXER                     0x33
#define WM8991_LINE_MIXER1                      0x34
#define WM8991_LINE_MIXER2                      0x35
#define WM8991_SPEAKER_MIXER                    0x36
#define WM8991_ADDITIONAL_CONTROL               0x37
#define WM8991_ANTIPOP1                         0x38
#define WM8991_ANTIPOP2                         0x39
#define WM8991_MICBIAS                          0x3A
#define WM8991_PLL1                             0x3C
#define WM8991_PLL2                             0x3D
#define WM8991_PLL3                             0x3E
#define WM8991_INTDRIVBITS			0x3F

#define WM8991_REGISTER_COUNT                   60
#define WM8991_MAX_REGISTER                     0x3F

/*
 * Field Definitions.
 */

/*
 * R0 (0x00) - Reset
 */
#define WM8991_SW_RESET_CHIP_ID_MASK            0xFFFF  /* SW_RESET_CHIP_ID - [15:0] */

/*
 * R1 (0x01) - Power Management (1)
 */
#define WM8991_SPK_ENA                          0x1000  /* SPK_ENA */
#define WM8991_SPK_ENA_BIT			12
#define WM8991_OUT3_ENA                         0x0800  /* OUT3_ENA */
#define WM8991_OUT3_ENA_BIT			11
#define WM8991_OUT4_ENA                         0x0400  /* OUT4_ENA */
#define WM8991_OUT4_ENA_BIT			10
#define WM8991_LOUT_ENA                         0x0200  /* LOUT_ENA */
#define WM8991_LOUT_ENA_BIT			9
#define WM8991_ROUT_ENA                         0x0100  /* ROUT_ENA */
#define WM8991_ROUT_ENA_BIT			8
#define WM8991_MICBIAS_ENA                      0x0010  /* MICBIAS_ENA */
#define WM8991_MICBIAS_ENA_BIT			4
#define WM8991_VMID_MODE_MASK                   0x0006  /* VMID_MODE - [2:1] */
#define WM8991_VREF_ENA                         0x0001  /* VREF_ENA */
#define WM8991_VREF_ENA_BIT			0

/*
 * R2 (0x02) - Power Management (2)
 */
#define WM8991_PLL_ENA                          0x8000  /* PLL_ENA */
#define WM8991_PLL_ENA_BIT			15
#define WM8991_TSHUT_ENA                        0x4000  /* TSHUT_ENA */
#define WM8991_TSHUT_ENA_BIT			14
#define WM8991_TSHUT_OPDIS                      0x2000  /* TSHUT_OPDIS */
#define WM8991_TSHUT_OPDIS_BIT			13
#define WM8991_OPCLK_ENA                        0x0800  /* OPCLK_ENA */
#define WM8991_OPCLK_ENA_BIT			11
#define WM8991_AINL_ENA                         0x0200  /* AINL_ENA */
#define WM8991_AINL_ENA_BIT			9
#define WM8991_AINR_ENA                         0x0100  /* AINR_ENA */
#define WM8991_AINR_ENA_BIT			8
#define WM8991_LIN34_ENA                        0x0080  /* LIN34_ENA */
#define WM8991_LIN34_ENA_BIT			7
#define WM8991_LIN12_ENA                        0x0040  /* LIN12_ENA */
#define WM8991_LIN12_ENA_BIT			6
#define WM8991_RIN34_ENA                        0x0020  /* RIN34_ENA */
#define WM8991_RIN34_ENA_BIT			5
#define WM8991_RIN12_ENA                        0x0010  /* RIN12_ENA */
#define WM8991_RIN12_ENA_BIT			4
#define WM8991_ADCL_ENA                         0x0002  /* ADCL_ENA */
#define WM8991_ADCL_ENA_BIT			1
#define WM8991_ADCR_ENA                         0x0001  /* ADCR_ENA */
#define WM8991_ADCR_ENA_BIT			0

/*
 * R3 (0x03) - Power Management (3)
 */
#define WM8991_LON_ENA                          0x2000  /* LON_ENA */
#define WM8991_LON_ENA_BIT			13
#define WM8991_LOP_ENA                          0x1000  /* LOP_ENA */
#define WM8991_LOP_ENA_BIT			12
#define WM8991_RON_ENA                          0x0800  /* RON_ENA */
#define WM8991_RON_ENA_BIT			11
#define WM8991_ROP_ENA                          0x0400  /* ROP_ENA */
#define WM8991_ROP_ENA_BIT			10
#define WM8991_LOPGA_ENA                        0x0080  /* LOPGA_ENA */
#define WM8991_LOPGA_ENA_BIT			7
#define WM8991_ROPGA_ENA                        0x0040  /* ROPGA_ENA */
#define WM8991_ROPGA_ENA_BIT			6
#define WM8991_LOMIX_ENA                        0x0020  /* LOMIX_ENA */
#define WM8991_LOMIX_ENA_BIT			5
#define WM8991_ROMIX_ENA                        0x0010  /* ROMIX_ENA */
#define WM8991_ROMIX_ENA_BIT			4
#define WM8991_DACL_ENA                         0x0002  /* DACL_ENA */
#define WM8991_DACL_ENA_BIT			1
#define WM8991_DACR_ENA                         0x0001  /* DACR_ENA */
#define WM8991_DACR_ENA_BIT			0

/*
 * R4 (0x04) - Audio Interface (1)
 */
#define WM8991_AIFADCL_SRC                      0x8000  /* AIFADCL_SRC */
#define WM8991_AIFADCR_SRC                      0x4000  /* AIFADCR_SRC */
#define WM8991_AIFADC_TDM                       0x2000  /* AIFADC_TDM */
#define WM8991_AIFADC_TDM_CHAN                  0x1000  /* AIFADC_TDM_CHAN */
#define WM8991_AIF_BCLK_INV                     0x0100  /* AIF_BCLK_INV */
#define WM8991_AIF_LRCLK_INV                    0x0080  /* AIF_LRCLK_INV */
#define WM8991_AIF_WL_MASK                      0x0060  /* AIF_WL - [6:5] */
#define WM8991_AIF_WL_16BITS			(0 << 5)
#define WM8991_AIF_WL_20BITS			(1 << 5)
#define WM8991_AIF_WL_24BITS			(2 << 5)
#define WM8991_AIF_WL_32BITS			(3 << 5)
#define WM8991_AIF_FMT_MASK                     0x0018  /* AIF_FMT - [4:3] */
#define WM8991_AIF_TMF_RIGHTJ			(0 << 3)
#define WM8991_AIF_TMF_LEFTJ			(1 << 3)
#define WM8991_AIF_TMF_I2S			(2 << 3)
#define WM8991_AIF_TMF_DSP			(3 << 3)

/*
 * R5 (0x05) - Audio Interface (2)
 */
#define WM8991_DACL_SRC                         0x8000  /* DACL_SRC */
#define WM8991_DACR_SRC                         0x4000  /* DACR_SRC */
#define WM8991_AIFDAC_TDM                       0x2000  /* AIFDAC_TDM */
#define WM8991_AIFDAC_TDM_CHAN                  0x1000  /* AIFDAC_TDM_CHAN */
#define WM8991_DAC_BOOST_MASK                   0x0C00  /* DAC_BOOST - [11:10] */
#define WM8991_DAC_COMP                         0x0010  /* DAC_COMP */
#define WM8991_DAC_COMPMODE                     0x0008  /* DAC_COMPMODE */
#define WM8991_ADC_COMP                         0x0004  /* ADC_COMP */
#define WM8991_ADC_COMPMODE                     0x0002  /* ADC_COMPMODE */
#define WM8991_LOOPBACK                         0x0001  /* LOOPBACK */

/*
 * R6 (0x06) - Clocking (1)
 */
#define WM8991_TOCLK_RATE                       0x8000  /* TOCLK_RATE */
#define WM8991_TOCLK_ENA                        0x4000  /* TOCLK_ENA */
#define WM8991_OPCLKDIV_MASK                    0x1E00  /* OPCLKDIV - [12:9] */
#define WM8991_DCLKDIV_MASK                     0x01C0  /* DCLKDIV - [8:6] */
#define WM8991_BCLK_DIV_MASK                    0x001E  /* BCLK_DIV - [4:1] */
#define WM8991_BCLK_DIV_1			(0x0 << 1)
#define WM8991_BCLK_DIV_1_5			(0x1 << 1)
#define WM8991_BCLK_DIV_2			(0x2 << 1)
#define WM8991_BCLK_DIV_3			(0x3 << 1)
#define WM8991_BCLK_DIV_4			(0x4 << 1)
#define WM8991_BCLK_DIV_5_5			(0x5 << 1)
#define WM8991_BCLK_DIV_6			(0x6 << 1)
#define WM8991_BCLK_DIV_8			(0x7 << 1)
#define WM8991_BCLK_DIV_11			(0x8 << 1)
#define WM8991_BCLK_DIV_12			(0x9 << 1)
#define WM8991_BCLK_DIV_16			(0xA << 1)
#define WM8991_BCLK_DIV_22			(0xB << 1)
#define WM8991_BCLK_DIV_24			(0xC << 1)
#define WM8991_BCLK_DIV_32			(0xD << 1)
#define WM8991_BCLK_DIV_44			(0xE << 1)
#define WM8991_BCLK_DIV_48			(0xF << 1)

/*
 * R7 (0x07) - Clocking (2)
 */
#define WM8991_MCLK_SRC                         0x8000  /* MCLK_SRC */
#define WM8991_SYSCLK_SRC                       0x4000  /* SYSCLK_SRC */
#define WM8991_CLK_FORCE                        0x2000  /* CLK_FORCE */
#define WM8991_MCLK_DIV_MASK                    0x1800  /* MCLK_DIV - [12:11] */
#define WM8991_MCLK_DIV_1			(0 << 11)
#define WM8991_MCLK_DIV_2			( 2 << 11)
#define WM8991_MCLK_INV                         0x0400  /* MCLK_INV */
#define WM8991_ADC_CLKDIV_MASK                  0x00E0  /* ADC_CLKDIV - [7:5] */
#define WM8991_ADC_CLKDIV_1			(0 << 5)
#define WM8991_ADC_CLKDIV_1_5			(1 << 5)
#define WM8991_ADC_CLKDIV_2			(2 << 5)
#define WM8991_ADC_CLKDIV_3			(3 << 5)
#define WM8991_ADC_CLKDIV_4			(4 << 5)
#define WM8991_ADC_CLKDIV_5_5			(5 << 5)
#define WM8991_ADC_CLKDIV_6			(6 << 5)
#define WM8991_DAC_CLKDIV_MASK                  0x001C  /* DAC_CLKDIV - [4:2] */
#define WM8991_DAC_CLKDIV_1			(0 << 2)
#define WM8991_DAC_CLKDIV_1_5			(1 << 2)
#define WM8991_DAC_CLKDIV_2			(2 << 2)
#define WM8991_DAC_CLKDIV_3			(3 << 2)
#define WM8991_DAC_CLKDIV_4			(4 << 2)
#define WM8991_DAC_CLKDIV_5_5			(5 << 2)
#define WM8991_DAC_CLKDIV_6			(6 << 2)

/*
 * R8 (0x08) - Audio Interface (3)
 */
#define WM8991_AIF_MSTR1                        0x8000  /* AIF_MSTR1 */
#define WM8991_AIF_MSTR2                        0x4000  /* AIF_MSTR2 */
#define WM8991_AIF_SEL                          0x2000  /* AIF_SEL */
#define WM8991_ADCLRC_DIR                       0x0800  /* ADCLRC_DIR */
#define WM8991_ADCLRC_RATE_MASK                 0x07FF  /* ADCLRC_RATE - [10:0] */

/*
 * R9 (0x09) - Audio Interface (4)
 */
#define WM8991_ALRCGPIO1                        0x8000  /* ALRCGPIO1 */
#define WM8991_ALRCBGPIO6                       0x4000  /* ALRCBGPIO6 */
#define WM8991_AIF_TRIS                         0x2000  /* AIF_TRIS */
#define WM8991_DACLRC_DIR                       0x0800  /* DACLRC_DIR */
#define WM8991_DACLRC_RATE_MASK                 0x07FF  /* DACLRC_RATE - [10:0] */

/*
 * R10 (0x0A) - DAC CTRL
 */
#define WM8991_AIF_LRCLKRATE                    0x0400  /* AIF_LRCLKRATE */
#define WM8991_DAC_MONO                         0x0200  /* DAC_MONO */
#define WM8991_DAC_SB_FILT                      0x0100  /* DAC_SB_FILT */
#define WM8991_DAC_MUTERATE                     0x0080  /* DAC_MUTERATE */
#define WM8991_DAC_MUTEMODE                     0x0040  /* DAC_MUTEMODE */
#define WM8991_DEEMP_MASK                       0x0030  /* DEEMP - [5:4] */
#define WM8991_DAC_MUTE                         0x0004  /* DAC_MUTE */
#define WM8991_DACL_DATINV                      0x0002  /* DACL_DATINV */
#define WM8991_DACR_DATINV                      0x0001  /* DACR_DATINV */

/*
 * R11 (0x0B) - Left DAC Digital Volume
 */
#define WM8991_DAC_VU                           0x0100  /* DAC_VU */
#define WM8991_DACL_VOL_MASK                    0x00FF  /* DACL_VOL - [7:0] */
#define WM8991_DACL_VOL_SHIFT			0
/*
 * R12 (0x0C) - Right DAC Digital Volume
 */
#define WM8991_DAC_VU                           0x0100  /* DAC_VU */
#define WM8991_DACR_VOL_MASK                    0x00FF  /* DACR_VOL - [7:0] */
#define WM8991_DACR_VOL_SHIFT			0
/*
 * R13 (0x0D) - Digital Side Tone
 */
#define WM8991_ADCL_DAC_SVOL_MASK               0x0F  /* ADCL_DAC_SVOL - [12:9] */
#define WM8991_ADCL_DAC_SVOL_SHIFT		9
#define WM8991_ADCR_DAC_SVOL_MASK               0x0F  /* ADCR_DAC_SVOL - [8:5] */
#define WM8991_ADCR_DAC_SVOL_SHIFT		5
#define WM8991_ADC_TO_DACL_MASK                 0x03  /* ADC_TO_DACL - [3:2] */
#define WM8991_ADC_TO_DACL_SHIFT		2
#define WM8991_ADC_TO_DACR_MASK                 0x03  /* ADC_TO_DACR - [1:0] */
#define WM8991_ADC_TO_DACR_SHIFT		0

/*
 * R14 (0x0E) - ADC CTRL
 */
#define WM8991_ADC_HPF_ENA                      0x0100  /* ADC_HPF_ENA */
#define WM8991_ADC_HPF_ENA_BIT			8
#define WM8991_ADC_HPF_CUT_MASK                 0x03  /* ADC_HPF_CUT - [6:5] */
#define WM8991_ADC_HPF_CUT_SHIFT		5
#define WM8991_ADCL_DATINV                      0x0002  /* ADCL_DATINV */
#define WM8991_ADCL_DATINV_BIT			1
#define WM8991_ADCR_DATINV                      0x0001  /* ADCR_DATINV */
#define WM8991_ADCR_DATINV_BIT			0

/*
 * R15 (0x0F) - Left ADC Digital Volume
 */
#define WM8991_ADC_VU                           0x0100  /* ADC_VU */
#define WM8991_ADCL_VOL_MASK                    0x00FF  /* ADCL_VOL - [7:0] */
#define WM8991_ADCL_VOL_SHIFT			0

/*
 * R16 (0x10) - Right ADC Digital Volume
 */
#define WM8991_ADC_VU                           0x0100  /* ADC_VU */
#define WM8991_ADCR_VOL_MASK                    0x00FF  /* ADCR_VOL - [7:0] */
#define WM8991_ADCR_VOL_SHIFT			0

/*
 * R18 (0x12) - GPIO CTRL 1
 */
#define WM8991_IRQ                              0x1000  /* IRQ */
#define WM8991_TEMPOK                           0x0800  /* TEMPOK */
#define WM8991_MICSHRT                          0x0400  /* MICSHRT */
#define WM8991_MICDET                           0x0200  /* MICDET */
#define WM8991_PLL_LCK                          0x0100  /* PLL_LCK */
#define WM8991_GPI8_STATUS                      0x0080  /* GPI8_STATUS */
#define WM8991_GPI7_STATUS                      0x0040  /* GPI7_STATUS */
#define WM8991_GPIO6_STATUS                     0x0020  /* GPIO6_STATUS */
#define WM8991_GPIO5_STATUS                     0x0010  /* GPIO5_STATUS */
#define WM8991_GPIO4_STATUS                     0x0008  /* GPIO4_STATUS */
#define WM8991_GPIO3_STATUS                     0x0004  /* GPIO3_STATUS */
#define WM8991_GPIO2_STATUS                     0x0002  /* GPIO2_STATUS */
#define WM8991_GPIO1_STATUS                     0x0001  /* GPIO1_STATUS */

/*
 * R19 (0x13) - GPIO1 & GPIO2
 */
#define WM8991_GPIO2_DEB_ENA                    0x8000  /* GPIO2_DEB_ENA */
#define WM8991_GPIO2_IRQ_ENA                    0x4000  /* GPIO2_IRQ_ENA */
#define WM8991_GPIO2_PU                         0x2000  /* GPIO2_PU */
#define WM8991_GPIO2_PD                         0x1000  /* GPIO2_PD */
#define WM8991_GPIO2_SEL_MASK                   0x0F00  /* GPIO2_SEL - [11:8] */
#define WM8991_GPIO1_DEB_ENA                    0x0080  /* GPIO1_DEB_ENA */
#define WM8991_GPIO1_IRQ_ENA                    0x0040  /* GPIO1_IRQ_ENA */
#define WM8991_GPIO1_PU                         0x0020  /* GPIO1_PU */
#define WM8991_GPIO1_PD                         0x0010  /* GPIO1_PD */
#define WM8991_GPIO1_SEL_MASK                   0x000F  /* GPIO1_SEL - [3:0] */

/*
 * R20 (0x14) - GPIO3 & GPIO4
 */
#define WM8991_GPIO4_DEB_ENA                    0x8000  /* GPIO4_DEB_ENA */
#define WM8991_GPIO4_IRQ_ENA                    0x4000  /* GPIO4_IRQ_ENA */
#define WM8991_GPIO4_PU                         0x2000  /* GPIO4_PU */
#define WM8991_GPIO4_PD                         0x1000  /* GPIO4_PD */
#define WM8991_GPIO4_SEL_MASK                   0x0F00  /* GPIO4_SEL - [11:8] */
#define WM8991_GPIO3_DEB_ENA                    0x0080  /* GPIO3_DEB_ENA */
#define WM8991_GPIO3_IRQ_ENA                    0x0040  /* GPIO3_IRQ_ENA */
#define WM8991_GPIO3_PU                         0x0020  /* GPIO3_PU */
#define WM8991_GPIO3_PD                         0x0010  /* GPIO3_PD */
#define WM8991_GPIO3_SEL_MASK                   0x000F  /* GPIO3_SEL - [3:0] */

/*
 * R21 (0x15) - GPIO5 & GPIO6
 */
#define WM8991_GPIO6_DEB_ENA                    0x8000  /* GPIO6_DEB_ENA */
#define WM8991_GPIO6_IRQ_ENA                    0x4000  /* GPIO6_IRQ_ENA */
#define WM8991_GPIO6_PU                         0x2000  /* GPIO6_PU */
#define WM8991_GPIO6_PD                         0x1000  /* GPIO6_PD */
#define WM8991_GPIO6_SEL_MASK                   0x0F00  /* GPIO6_SEL - [11:8] */
#define WM8991_GPIO5_DEB_ENA                    0x0080  /* GPIO5_DEB_ENA */
#define WM8991_GPIO5_IRQ_ENA                    0x0040  /* GPIO5_IRQ_ENA */
#define WM8991_GPIO5_PU                         0x0020  /* GPIO5_PU */
#define WM8991_GPIO5_PD                         0x0010  /* GPIO5_PD */
#define WM8991_GPIO5_SEL_MASK                   0x000F  /* GPIO5_SEL - [3:0] */

/*
 * R22 (0x16) - GPIOCTRL 2
 */
#define WM8991_RD_3W_ENA                        0x8000  /* RD_3W_ENA */
#define WM8991_MODE_3W4W                        0x4000  /* MODE_3W4W */
#define WM8991_TEMPOK_IRQ_ENA                   0x0800  /* TEMPOK_IRQ_ENA */
#define WM8991_MICSHRT_IRQ_ENA                  0x0400  /* MICSHRT_IRQ_ENA */
#define WM8991_MICDET_IRQ_ENA                   0x0200  /* MICDET_IRQ_ENA */
#define WM8991_PLL_LCK_IRQ_ENA                  0x0100  /* PLL_LCK_IRQ_ENA */
#define WM8991_GPI8_DEB_ENA                     0x0080  /* GPI8_DEB_ENA */
#define WM8991_GPI8_IRQ_ENA                     0x0040  /* GPI8_IRQ_ENA */
#define WM8991_GPI8_ENA                         0x0010  /* GPI8_ENA */
#define WM8991_GPI7_DEB_ENA                     0x0008  /* GPI7_DEB_ENA */
#define WM8991_GPI7_IRQ_ENA                     0x0004  /* GPI7_IRQ_ENA */
#define WM8991_GPI7_ENA                         0x0001  /* GPI7_ENA */

/*
 * R23 (0x17) - GPIO_POL
 */
#define WM8991_IRQ_INV                          0x1000  /* IRQ_INV */
#define WM8991_TEMPOK_POL                       0x0800  /* TEMPOK_POL */
#define WM8991_MICSHRT_POL                      0x0400  /* MICSHRT_POL */
#define WM8991_MICDET_POL                       0x0200  /* MICDET_POL */
#define WM8991_PLL_LCK_POL                      0x0100  /* PLL_LCK_POL */
#define WM8991_GPI8_POL                         0x0080  /* GPI8_POL */
#define WM8991_GPI7_POL                         0x0040  /* GPI7_POL */
#define WM8991_GPIO6_POL                        0x0020  /* GPIO6_POL */
#define WM8991_GPIO5_POL                        0x0010  /* GPIO5_POL */
#define WM8991_GPIO4_POL                        0x0008  /* GPIO4_POL */
#define WM8991_GPIO3_POL                        0x0004  /* GPIO3_POL */
#define WM8991_GPIO2_POL                        0x0002  /* GPIO2_POL */
#define WM8991_GPIO1_POL                        0x0001  /* GPIO1_POL */

/*
 * R24 (0x18) - Left Line Input 1&2 Volume
 */
#define WM8991_IPVU                             0x0100  /* IPVU */
#define WM8991_LI12MUTE                         0x0080  /* LI12MUTE */
#define WM8991_LI12MUTE_BIT			7
#define WM8991_LI12ZC                           0x0040  /* LI12ZC */
#define WM8991_LI12ZC_BIT			6
#define WM8991_LIN12VOL_MASK                    0x001F  /* LIN12VOL - [4:0] */
#define WM8991_LIN12VOL_SHIFT			0
/*
 * R25 (0x19) - Left Line Input 3&4 Volume
 */
#define WM8991_IPVU                             0x0100  /* IPVU */
#define WM8991_LI34MUTE                         0x0080  /* LI34MUTE */
#define WM8991_LI34MUTE_BIT			7
#define WM8991_LI34ZC                           0x0040  /* LI34ZC */
#define WM8991_LI34ZC_BIT			6
#define WM8991_LIN34VOL_MASK                    0x001F  /* LIN34VOL - [4:0] */
#define WM8991_LIN34VOL_SHIFT			0

/*
 * R26 (0x1A) - Right Line Input 1&2 Volume
 */
#define WM8991_IPVU                             0x0100  /* IPVU */
#define WM8991_RI12MUTE                         0x0080  /* RI12MUTE */
#define WM8991_RI12MUTE_BIT			7
#define WM8991_RI12ZC                           0x0040  /* RI12ZC */
#define WM8991_RI12ZC_BIT			6
#define WM8991_RIN12VOL_MASK                    0x001F  /* RIN12VOL - [4:0] */
#define WM8991_RIN12VOL_SHIFT			0

/*
 * R27 (0x1B) - Right Line Input 3&4 Volume
 */
#define WM8991_IPVU                             0x0100  /* IPVU */
#define WM8991_RI34MUTE                         0x0080  /* RI34MUTE */
#define WM8991_RI34MUTE_BIT			7
#define WM8991_RI34ZC                           0x0040  /* RI34ZC */
#define WM8991_RI34ZC_BIT			6
#define WM8991_RIN34VOL_MASK                    0x001F  /* RIN34VOL - [4:0] */
#define WM8991_RIN34VOL_SHIFT			0

/*
 * R28 (0x1C) - Left Output Volume
 */
#define WM8991_OPVU                             0x0100  /* OPVU */
#define WM8991_LOZC                             0x0080  /* LOZC */
#define WM8991_LOZC_BIT				7
#define WM8991_LOUTVOL_MASK                     0x007F  /* LOUTVOL - [6:0] */
#define WM8991_LOUTVOL_SHIFT			0
/*
 * R29 (0x1D) - Right Output Volume
 */
#define WM8991_OPVU                             0x0100  /* OPVU */
#define WM8991_ROZC                             0x0080  /* ROZC */
#define WM8991_ROZC_BIT				7
#define WM8991_ROUTVOL_MASK                     0x007F  /* ROUTVOL - [6:0] */
#define WM8991_ROUTVOL_SHIFT			0
/*
 * R30 (0x1E) - Line Outputs Volume
 */
#define WM8991_LONMUTE                          0x0040  /* LONMUTE */
#define WM8991_LONMUTE_BIT			6
#define WM8991_LOPMUTE                          0x0020  /* LOPMUTE */
#define WM8991_LOPMUTE_BIT			5
#define WM8991_LOATTN                           0x0010  /* LOATTN */
#define WM8991_LOATTN_BIT			4
#define WM8991_RONMUTE                          0x0004  /* RONMUTE */
#define WM8991_RONMUTE_BIT			2
#define WM8991_ROPMUTE                          0x0002  /* ROPMUTE */
#define WM8991_ROPMUTE_BIT			1
#define WM8991_ROATTN                           0x0001  /* ROATTN */
#define WM8991_ROATTN_BIT			0

/*
 * R31 (0x1F) - Out3/4 Volume
 */
#define WM8991_OUT3MUTE                         0x0020  /* OUT3MUTE */
#define WM8991_OUT3MUTE_BIT			5
#define WM8991_OUT3ATTN                         0x0010  /* OUT3ATTN */
#define WM8991_OUT3ATTN_BIT			4
#define WM8991_OUT4MUTE                         0x0002  /* OUT4MUTE */
#define WM8991_OUT4MUTE_BIT			1
#define WM8991_OUT4ATTN                         0x0001  /* OUT4ATTN */
#define WM8991_OUT4ATTN_BIT			0

/*
 * R32 (0x20) - Left OPGA Volume
 */
#define WM8991_OPVU                             0x0100  /* OPVU */
#define WM8991_LOPGAZC                          0x0080  /* LOPGAZC */
#define WM8991_LOPGAZC_BIT			7
#define WM8991_LOPGAVOL_MASK                    0x007F  /* LOPGAVOL - [6:0] */
#define WM8991_LOPGAVOL_SHIFT			0

/*
 * R33 (0x21) - Right OPGA Volume
 */
#define WM8991_OPVU                             0x0100  /* OPVU */
#define WM8991_ROPGAZC                          0x0080  /* ROPGAZC */
#define WM8991_ROPGAZC_BIT			7
#define WM8991_ROPGAVOL_MASK                    0x007F  /* ROPGAVOL - [6:0] */
#define WM8991_ROPGAVOL_SHIFT			0
/*
 * R34 (0x22) - Speaker Volume
 */
#define WM8991_SPKVOL_MASK                      0x0003  /* SPKVOL - [1:0] */
#define WM8991_SPKVOL_SHIFT			0

/*
 * R35 (0x23) - ClassD1
 */
#define WM8991_CDMODE                           0x0100  /* CDMODE */
#define WM8991_CDMODE_BIT			8

/*
 * R37 (0x25) - ClassD3
 */
#define WM8991_DCGAIN_MASK                      0x0007  /* DCGAIN - [5:3] */
#define WM8991_DCGAIN_SHIFT			3
#define WM8991_ACGAIN_MASK                      0x0007  /* ACGAIN - [2:0] */
#define WM8991_ACGAIN_SHIFT			0
/*
 * R39 (0x27) - Input Mixer1
 */
#define WM8991_AINLMODE_MASK                    0x000C  /* AINLMODE - [3:2] */
#define WM8991_AINLMODE_SHIFT			2
#define WM8991_AINRMODE_MASK                    0x0003  /* AINRMODE - [1:0] */
#define WM8991_AINRMODE_SHIFT			0

/*
 * R40 (0x28) - Input Mixer2
 */
#define WM8991_LMP4								0x0080	/* LMP4 */
#define WM8991_LMP4_BIT                         7		/* LMP4 */
#define WM8991_LMN3                             0x0040  /* LMN3 */
#define WM8991_LMN3_BIT                         6       /* LMN3 */
#define WM8991_LMP2                             0x0020  /* LMP2 */
#define WM8991_LMP2_BIT                         5       /* LMP2 */
#define WM8991_LMN1                             0x0010  /* LMN1 */
#define WM8991_LMN1_BIT                         4       /* LMN1 */
#define WM8991_RMP4                             0x0008  /* RMP4 */
#define WM8991_RMP4_BIT                         3       /* RMP4 */
#define WM8991_RMN3                             0x0004  /* RMN3 */
#define WM8991_RMN3_BIT                         2       /* RMN3 */
#define WM8991_RMP2                             0x0002  /* RMP2 */
#define WM8991_RMP2_BIT                         1       /* RMP2 */
#define WM8991_RMN1                             0x0001  /* RMN1 */
#define WM8991_RMN1_BIT                         0       /* RMN1 */

/*
 * R41 (0x29) - Input Mixer3
 */
#define WM8991_L34MNB                           0x0100  /* L34MNB */
#define WM8991_L34MNB_BIT			8
#define WM8991_L34MNBST                         0x0080  /* L34MNBST */
#define WM8991_L34MNBST_BIT			7
#define WM8991_L12MNB                           0x0020  /* L12MNB */
#define WM8991_L12MNB_BIT			5
#define WM8991_L12MNBST                         0x0010  /* L12MNBST */
#define WM8991_L12MNBST_BIT			4
#define WM8991_LDBVOL_MASK                      0x0007  /* LDBVOL - [2:0] */
#define WM8991_LDBVOL_SHIFT			0

/*
 * R42 (0x2A) - Input Mixer4
 */
#define WM8991_R34MNB                           0x0100  /* R34MNB */
#define WM8991_R34MNB_BIT			8
#define WM8991_R34MNBST                         0x0080  /* R34MNBST */
#define WM8991_R34MNBST_BIT			7
#define WM8991_R12MNB                           0x0020  /* R12MNB */
#define WM8991_R12MNB_BIT			5
#define WM8991_R12MNBST                         0x0010  /* R12MNBST */
#define WM8991_R12MNBST_BIT			4
#define WM8991_RDBVOL_MASK                      0x0007  /* RDBVOL - [2:0] */
#define WM8991_RDBVOL_SHIFT			0

/*
 * R43 (0x2B) - Input Mixer5
 */
#define WM8991_LI2BVOL_MASK                     0x07  /* LI2BVOL - [8:6] */
#define WM8991_LI2BVOL_SHIFT			6
#define WM8991_LR4BVOL_MASK                     0x07  /* LR4BVOL - [5:3] */
#define WM8991_LR4BVOL_SHIFT			3
#define WM8991_LL4BVOL_MASK                     0x07  /* LL4BVOL - [2:0] */
#define WM8991_LL4BVOL_SHIFT			0

/*
 * R44 (0x2C) - Input Mixer6
 */
#define WM8991_RI2BVOL_MASK                     0x07  /* RI2BVOL - [8:6] */
#define WM8991_RI2BVOL_SHIFT			6
#define WM8991_RL4BVOL_MASK                     0x07  /* RL4BVOL - [5:3] */
#define WM8991_RL4BVOL_SHIFT			3
#define WM8991_RR4BVOL_MASK                     0x07  /* RR4BVOL - [2:0] */
#define WM8991_RR4BVOL_SHIFT			0

/*
 * R45 (0x2D) - Output Mixer1
 */
#define WM8991_LRBLO                            0x0080  /* LRBLO */
#define WM8991_LRBLO_BIT			7
#define WM8991_LLBLO                            0x0040  /* LLBLO */
#define WM8991_LLBLO_BIT			6
#define WM8991_LRI3LO                           0x0020  /* LRI3LO */
#define WM8991_LRI3LO_BIT			5
#define WM8991_LLI3LO                           0x0010  /* LLI3LO */
#define WM8991_LLI3LO_BIT			4
#define WM8991_LR12LO                           0x0008  /* LR12LO */
#define WM8991_LR12LO_BIT			3
#define WM8991_LL12LO                           0x0004  /* LL12LO */
#define WM8991_LL12LO_BIT			2
#define WM8991_LDLO                             0x0001  /* LDLO */
#define WM8991_LDLO_BIT				0

/*
 * R46 (0x2E) - Output Mixer2
 */
#define WM8991_RLBRO                            0x0080  /* RLBRO */
#define WM8991_RLBRO_BIT			7
#define WM8991_RRBRO                            0x0040  /* RRBRO */
#define WM8991_RRBRO_BIT			6
#define WM8991_RLI3RO                           0x0020  /* RLI3RO */
#define WM8991_RLI3RO_BIT			5
#define WM8991_RRI3RO                           0x0010  /* RRI3RO */
#define WM8991_RRI3RO_BIT			4
#define WM8991_RL12RO                           0x0008  /* RL12RO */
#define WM8991_RL12RO_BIT			3
#define WM8991_RR12RO                           0x0004  /* RR12RO */
#define WM8991_RR12RO_BIT			2
#define WM8991_RDRO                             0x0001  /* RDRO */
#define WM8991_RDRO_BIT				0

/*
 * R47 (0x2F) - Output Mixer3
 */
#define WM8991_LLI3LOVOL_MASK                   0x07  /* LLI3LOVOL - [8:6] */
#define WM8991_LLI3LOVOL_SHIFT			6
#define WM8991_LR12LOVOL_MASK                   0x07  /* LR12LOVOL - [5:3] */
#define WM8991_LR12LOVOL_SHIFT			3
#define WM8991_LL12LOVOL_MASK                   0x07  /* LL12LOVOL - [2:0] */
#define WM8991_LL12LOVOL_SHIFT			0

/*
 * R48 (0x30) - Output Mixer4
 */
#define WM8991_RRI3ROVOL_MASK                   0x07  /* RRI3ROVOL - [8:6] */
#define WM8991_RRI3ROVOL_SHIFT			6
#define WM8991_RL12ROVOL_MASK                   0x07  /* RL12ROVOL - [5:3] */
#define WM8991_RL12ROVOL_SHIFT			3
#define WM8991_RR12ROVOL_MASK                   0x07  /* RR12ROVOL - [2:0] */
#define WM8991_RR12ROVOL_SHIFT			0

/*
 * R49 (0x31) - Output Mixer5
 */
#define WM8991_LRI3LOVOL_MASK                   0x07  /* LRI3LOVOL - [8:6] */
#define WM8991_LRI3LOVOL_SHIFT			6
#define WM8991_LRBLOVOL_MASK                    0x07  /* LRBLOVOL - [5:3] */
#define WM8991_LRBLOVOL_SHIFT			3
#define WM8991_LLBLOVOL_MASK                    0x07  /* LLBLOVOL - [2:0] */
#define WM8991_LLBLOVOL_SHIFT			0

/*
 * R50 (0x32) - Output Mixer6
 */
#define WM8991_RLI3ROVOL_MASK                   0x07  /* RLI3ROVOL - [8:6] */
#define WM8991_RLI3ROVOL_SHIFT			6
#define WM8991_RLBROVOL_MASK                    0x07  /* RLBROVOL - [5:3] */
#define WM8991_RLBROVOL_SHIFT			3
#define WM8991_RRBROVOL_MASK                    0x07  /* RRBROVOL - [2:0] */
#define WM8991_RRBROVOL_SHIFT			0

/*
 * R51 (0x33) - Out3/4 Mixer
 */
#define WM8991_VSEL_MASK                        0x0180  /* VSEL - [8:7] */
#define WM8991_LI4O3                            0x0020  /* LI4O3 */
#define WM8991_LI4O3_BIT			5
#define WM8991_LPGAO3                           0x0010  /* LPGAO3 */
#define WM8991_LPGAO3_BIT			4
#define WM8991_RI4O4                            0x0002  /* RI4O4 */
#define WM8991_RI4O4_BIT			1
#define WM8991_RPGAO4                           0x0001  /* RPGAO4 */
#define WM8991_RPGAO4_BIT			0
/*
 * R52 (0x34) - Line Mixer1
 */
#define WM8991_LLOPGALON                        0x0040  /* LLOPGALON */
#define WM8991_LLOPGALON_BIT			6
#define WM8991_LROPGALON                        0x0020  /* LROPGALON */
#define WM8991_LROPGALON_BIT			5
#define WM8991_LOPLON                           0x0010  /* LOPLON */
#define WM8991_LOPLON_BIT			4
#define WM8991_LR12LOP                          0x0004  /* LR12LOP */
#define WM8991_LR12LOP_BIT			2
#define WM8991_LL12LOP                          0x0002  /* LL12LOP */
#define WM8991_LL12LOP_BIT			1
#define WM8991_LLOPGALOP                        0x0001  /* LLOPGALOP */
#define WM8991_LLOPGALOP_BIT			0
/*
 * R53 (0x35) - Line Mixer2
 */
#define WM8991_RROPGARON                        0x0040  /* RROPGARON */
#define WM8991_RROPGARON_BIT			6
#define WM8991_RLOPGARON                        0x0020  /* RLOPGARON */
#define WM8991_RLOPGARON_BIT			5
#define WM8991_ROPRON                           0x0010  /* ROPRON */
#define WM8991_ROPRON_BIT			4
#define WM8991_RL12ROP                          0x0004  /* RL12ROP */
#define WM8991_RL12ROP_BIT			2
#define WM8991_RR12ROP                          0x0002  /* RR12ROP */
#define WM8991_RR12ROP_BIT			1
#define WM8991_RROPGAROP                        0x0001  /* RROPGAROP */
#define WM8991_RROPGAROP_BIT			0

/*
 * R54 (0x36) - Speaker Mixer
 */
#define WM8991_LB2SPK                           0x0080  /* LB2SPK */
#define WM8991_LB2SPK_BIT			7
#define WM8991_RB2SPK                           0x0040  /* RB2SPK */
#define WM8991_RB2SPK_BIT			6
#define WM8991_LI2SPK                           0x0020  /* LI2SPK */
#define WM8991_LI2SPK_BIT			5
#define WM8991_RI2SPK                           0x0010  /* RI2SPK */
#define WM8991_RI2SPK_BIT			4
#define WM8991_LOPGASPK                         0x0008  /* LOPGASPK */
#define WM8991_LOPGASPK_BIT			3
#define WM8991_ROPGASPK                         0x0004  /* ROPGASPK */
#define WM8991_ROPGASPK_BIT			2
#define WM8991_LDSPK                            0x0002  /* LDSPK */
#define WM8991_LDSPK_BIT			1
#define WM8991_RDSPK                            0x0001  /* RDSPK */
#define WM8991_RDSPK_BIT			0

/*
 * R55 (0x37) - Additional Control
 */
#define WM8991_VROI                             0x0001  /* VROI */

/*
 * R56 (0x38) - AntiPOP1
 */
#define WM8991_DIS_LLINE                        0x0020  /* DIS_LLINE */
#define WM8991_DIS_RLINE                        0x0010  /* DIS_RLINE */
#define WM8991_DIS_OUT3                         0x0008  /* DIS_OUT3 */
#define WM8991_DIS_OUT4                         0x0004  /* DIS_OUT4 */
#define WM8991_DIS_LOUT                         0x0002  /* DIS_LOUT */
#define WM8991_DIS_ROUT                         0x0001  /* DIS_ROUT */

/*
 * R57 (0x39) - AntiPOP2
 */
#define WM8991_SOFTST                           0x0040  /* SOFTST */
#define WM8991_BUFIOEN                          0x0008  /* BUFIOEN */
#define WM8991_BUFDCOPEN                        0x0004  /* BUFDCOPEN */
#define WM8991_POBCTRL                          0x0002  /* POBCTRL */
#define WM8991_VMIDTOG                          0x0001  /* VMIDTOG */

/*
 * R58 (0x3A) - MICBIAS
 */
#define WM8991_MCDSCTH_MASK                     0x00C0  /* MCDSCTH - [7:6] */
#define WM8991_MCDTHR_MASK                      0x0038  /* MCDTHR - [5:3] */
#define WM8991_MCD                              0x0004  /* MCD */
#define WM8991_MBSEL                            0x0001  /* MBSEL */

/*
 * R60 (0x3C) - PLL1
 */
#define WM8991_SDM                              0x0080  /* SDM */
#define WM8991_PRESCALE                         0x0040  /* PRESCALE */
#define WM8991_PLLN_MASK                        0x000F  /* PLLN - [3:0] */

/*
 * R61 (0x3D) - PLL2
 */
#define WM8991_PLLK1_MASK                       0x00FF  /* PLLK1 - [7:0] */

/*
 * R62 (0x3E) - PLL3
 */
#define WM8991_PLLK2_MASK                       0x00FF  /* PLLK2 - [7:0] */

/*
 * R63 (0x3F) - Internal Driver Bits
 */
#define WM8991_INMIXL_PWR_BIT			0
#define WM8991_AINLMUX_PWR_BIT			1
#define WM8991_INMIXR_PWR_BIT			2
#define WM8991_AINRMUX_PWR_BIT			3

#define WM8991_MCLK_DIV 0
#define WM8991_DACCLK_DIV 1
#define WM8991_ADCCLK_DIV 2
#define WM8991_BCLK_DIV 3

#define SOC_WM899X_OUTPGA_SINGLE_R_TLV(xname, reg, shift, max, invert,\
					 tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, \
	.get = snd_soc_get_volsw, .put = wm899x_outpga_put_volsw_vu, \
	.private_value = SOC_SINGLE_VALUE(reg, shift, max, invert) }

#endif /* _WM8991_H */
