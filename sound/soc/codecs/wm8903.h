/*
 * wm8903.h - WM8903 audio codec interface
 *
 * Copyright 2008 Wolfson Microelectronics PLC.
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#ifndef _WM8903_H
#define _WM8903_H

#include <linux/i2c.h>

extern struct snd_soc_dai wm8903_dai;
extern struct snd_soc_codec_device soc_codec_dev_wm8903;

#define WM8903_MCLK_DIV_2 1
#define WM8903_CLK_SYS    2
#define WM8903_BCLK       3
#define WM8903_LRCLK      4

/*
 * Register values.
 */
#define WM8903_SW_RESET_AND_ID                  0x00
#define WM8903_REVISION_NUMBER                  0x01
#define WM8903_BIAS_CONTROL_0                   0x04
#define WM8903_VMID_CONTROL_0                   0x05
#define WM8903_MIC_BIAS_CONTROL_0               0x06
#define WM8903_ANALOGUE_DAC_0                   0x08
#define WM8903_ANALOGUE_ADC_0                   0x0A
#define WM8903_POWER_MANAGEMENT_0               0x0C
#define WM8903_POWER_MANAGEMENT_1               0x0D
#define WM8903_POWER_MANAGEMENT_2               0x0E
#define WM8903_POWER_MANAGEMENT_3               0x0F
#define WM8903_POWER_MANAGEMENT_4               0x10
#define WM8903_POWER_MANAGEMENT_5               0x11
#define WM8903_POWER_MANAGEMENT_6               0x12
#define WM8903_CLOCK_RATES_0                    0x14
#define WM8903_CLOCK_RATES_1                    0x15
#define WM8903_CLOCK_RATES_2                    0x16
#define WM8903_AUDIO_INTERFACE_0                0x18
#define WM8903_AUDIO_INTERFACE_1                0x19
#define WM8903_AUDIO_INTERFACE_2                0x1A
#define WM8903_AUDIO_INTERFACE_3                0x1B
#define WM8903_DAC_DIGITAL_VOLUME_LEFT          0x1E
#define WM8903_DAC_DIGITAL_VOLUME_RIGHT         0x1F
#define WM8903_DAC_DIGITAL_0                    0x20
#define WM8903_DAC_DIGITAL_1                    0x21
#define WM8903_ADC_DIGITAL_VOLUME_LEFT          0x24
#define WM8903_ADC_DIGITAL_VOLUME_RIGHT         0x25
#define WM8903_ADC_DIGITAL_0                    0x26
#define WM8903_DIGITAL_MICROPHONE_0             0x27
#define WM8903_DRC_0                            0x28
#define WM8903_DRC_1                            0x29
#define WM8903_DRC_2                            0x2A
#define WM8903_DRC_3                            0x2B
#define WM8903_ANALOGUE_LEFT_INPUT_0            0x2C
#define WM8903_ANALOGUE_RIGHT_INPUT_0           0x2D
#define WM8903_ANALOGUE_LEFT_INPUT_1            0x2E
#define WM8903_ANALOGUE_RIGHT_INPUT_1           0x2F
#define WM8903_ANALOGUE_LEFT_MIX_0              0x32
#define WM8903_ANALOGUE_RIGHT_MIX_0             0x33
#define WM8903_ANALOGUE_SPK_MIX_LEFT_0          0x34
#define WM8903_ANALOGUE_SPK_MIX_LEFT_1          0x35
#define WM8903_ANALOGUE_SPK_MIX_RIGHT_0         0x36
#define WM8903_ANALOGUE_SPK_MIX_RIGHT_1         0x37
#define WM8903_ANALOGUE_OUT1_LEFT               0x39
#define WM8903_ANALOGUE_OUT1_RIGHT              0x3A
#define WM8903_ANALOGUE_OUT2_LEFT               0x3B
#define WM8903_ANALOGUE_OUT2_RIGHT              0x3C
#define WM8903_ANALOGUE_OUT3_LEFT               0x3E
#define WM8903_ANALOGUE_OUT3_RIGHT              0x3F
#define WM8903_ANALOGUE_SPK_OUTPUT_CONTROL_0    0x41
#define WM8903_DC_SERVO_0                       0x43
#define WM8903_DC_SERVO_2                       0x45
#define WM8903_ANALOGUE_HP_0                    0x5A
#define WM8903_ANALOGUE_LINEOUT_0               0x5E
#define WM8903_CHARGE_PUMP_0                    0x62
#define WM8903_CLASS_W_0                        0x68
#define WM8903_WRITE_SEQUENCER_0                0x6C
#define WM8903_WRITE_SEQUENCER_1                0x6D
#define WM8903_WRITE_SEQUENCER_2                0x6E
#define WM8903_WRITE_SEQUENCER_3                0x6F
#define WM8903_WRITE_SEQUENCER_4                0x70
#define WM8903_CONTROL_INTERFACE                0x72
#define WM8903_GPIO_CONTROL_1                   0x74
#define WM8903_GPIO_CONTROL_2                   0x75
#define WM8903_GPIO_CONTROL_3                   0x76
#define WM8903_GPIO_CONTROL_4                   0x77
#define WM8903_GPIO_CONTROL_5                   0x78
#define WM8903_INTERRUPT_STATUS_1               0x79
#define WM8903_INTERRUPT_STATUS_1_MASK          0x7A
#define WM8903_INTERRUPT_POLARITY_1             0x7B
#define WM8903_INTERRUPT_CONTROL                0x7E
#define WM8903_CONTROL_INTERFACE_TEST_1         0x81
#define WM8903_CHARGE_PUMP_TEST_1               0x95
#define WM8903_CLOCK_RATE_TEST_4                0xA4
#define WM8903_ANALOGUE_OUTPUT_BIAS_0           0xAC

#define WM8903_REGISTER_COUNT                   75
#define WM8903_MAX_REGISTER                     0xAC

/*
 * Field Definitions.
 */

/*
 * R0 (0x00) - SW Reset and ID
 */
#define WM8903_SW_RESET_DEV_ID1_MASK            0xFFFF  /* SW_RESET_DEV_ID1 - [15:0] */
#define WM8903_SW_RESET_DEV_ID1_SHIFT                0  /* SW_RESET_DEV_ID1 - [15:0] */
#define WM8903_SW_RESET_DEV_ID1_WIDTH               16  /* SW_RESET_DEV_ID1 - [15:0] */

/*
 * R1 (0x01) - Revision Number
 */
#define WM8903_CHIP_REV_MASK                    0x000F  /* CHIP_REV - [3:0] */
#define WM8903_CHIP_REV_SHIFT                        0  /* CHIP_REV - [3:0] */
#define WM8903_CHIP_REV_WIDTH                        4  /* CHIP_REV - [3:0] */

/*
 * R4 (0x04) - Bias Control 0
 */
#define WM8903_POBCTRL                          0x0010  /* POBCTRL */
#define WM8903_POBCTRL_MASK                     0x0010  /* POBCTRL */
#define WM8903_POBCTRL_SHIFT                         4  /* POBCTRL */
#define WM8903_POBCTRL_WIDTH                         1  /* POBCTRL */
#define WM8903_ISEL_MASK                        0x000C  /* ISEL - [3:2] */
#define WM8903_ISEL_SHIFT                            2  /* ISEL - [3:2] */
#define WM8903_ISEL_WIDTH                            2  /* ISEL - [3:2] */
#define WM8903_STARTUP_BIAS_ENA                 0x0002  /* STARTUP_BIAS_ENA */
#define WM8903_STARTUP_BIAS_ENA_MASK            0x0002  /* STARTUP_BIAS_ENA */
#define WM8903_STARTUP_BIAS_ENA_SHIFT                1  /* STARTUP_BIAS_ENA */
#define WM8903_STARTUP_BIAS_ENA_WIDTH                1  /* STARTUP_BIAS_ENA */
#define WM8903_BIAS_ENA                         0x0001  /* BIAS_ENA */
#define WM8903_BIAS_ENA_MASK                    0x0001  /* BIAS_ENA */
#define WM8903_BIAS_ENA_SHIFT                        0  /* BIAS_ENA */
#define WM8903_BIAS_ENA_WIDTH                        1  /* BIAS_ENA */

/*
 * R5 (0x05) - VMID Control 0
 */
#define WM8903_VMID_TIE_ENA                     0x0080  /* VMID_TIE_ENA */
#define WM8903_VMID_TIE_ENA_MASK                0x0080  /* VMID_TIE_ENA */
#define WM8903_VMID_TIE_ENA_SHIFT                    7  /* VMID_TIE_ENA */
#define WM8903_VMID_TIE_ENA_WIDTH                    1  /* VMID_TIE_ENA */
#define WM8903_BUFIO_ENA                        0x0040  /* BUFIO_ENA */
#define WM8903_BUFIO_ENA_MASK                   0x0040  /* BUFIO_ENA */
#define WM8903_BUFIO_ENA_SHIFT                       6  /* BUFIO_ENA */
#define WM8903_BUFIO_ENA_WIDTH                       1  /* BUFIO_ENA */
#define WM8903_VMID_IO_ENA                      0x0020  /* VMID_IO_ENA */
#define WM8903_VMID_IO_ENA_MASK                 0x0020  /* VMID_IO_ENA */
#define WM8903_VMID_IO_ENA_SHIFT                     5  /* VMID_IO_ENA */
#define WM8903_VMID_IO_ENA_WIDTH                     1  /* VMID_IO_ENA */
#define WM8903_VMID_SOFT_MASK                   0x0018  /* VMID_SOFT - [4:3] */
#define WM8903_VMID_SOFT_SHIFT                       3  /* VMID_SOFT - [4:3] */
#define WM8903_VMID_SOFT_WIDTH                       2  /* VMID_SOFT - [4:3] */
#define WM8903_VMID_RES_MASK                    0x0006  /* VMID_RES - [2:1] */
#define WM8903_VMID_RES_SHIFT                        1  /* VMID_RES - [2:1] */
#define WM8903_VMID_RES_WIDTH                        2  /* VMID_RES - [2:1] */
#define WM8903_VMID_BUF_ENA                     0x0001  /* VMID_BUF_ENA */
#define WM8903_VMID_BUF_ENA_MASK                0x0001  /* VMID_BUF_ENA */
#define WM8903_VMID_BUF_ENA_SHIFT                    0  /* VMID_BUF_ENA */
#define WM8903_VMID_BUF_ENA_WIDTH                    1  /* VMID_BUF_ENA */

#define WM8903_VMID_RES_50K                          2
#define WM8903_VMID_RES_250K                         3
#define WM8903_VMID_RES_5K                           4

/*
 * R6 (0x06) - Mic Bias Control 0
 */
#define WM8903_MICDET_HYST_ENA                  0x0080  /* MICDET_HYST_ENA */
#define WM8903_MICDET_HYST_ENA_MASK             0x0080  /* MICDET_HYST_ENA */
#define WM8903_MICDET_HYST_ENA_SHIFT                 7  /* MICDET_HYST_ENA */
#define WM8903_MICDET_HYST_ENA_WIDTH                 1  /* MICDET_HYST_ENA */
#define WM8903_MICDET_THR_MASK                  0x0070  /* MICDET_THR - [6:4] */
#define WM8903_MICDET_THR_SHIFT                      4  /* MICDET_THR - [6:4] */
#define WM8903_MICDET_THR_WIDTH                      3  /* MICDET_THR - [6:4] */
#define WM8903_MICSHORT_THR_MASK                0x000C  /* MICSHORT_THR - [3:2] */
#define WM8903_MICSHORT_THR_SHIFT                    2  /* MICSHORT_THR - [3:2] */
#define WM8903_MICSHORT_THR_WIDTH                    2  /* MICSHORT_THR - [3:2] */
#define WM8903_MICDET_ENA                       0x0002  /* MICDET_ENA */
#define WM8903_MICDET_ENA_MASK                  0x0002  /* MICDET_ENA */
#define WM8903_MICDET_ENA_SHIFT                      1  /* MICDET_ENA */
#define WM8903_MICDET_ENA_WIDTH                      1  /* MICDET_ENA */
#define WM8903_MICBIAS_ENA                      0x0001  /* MICBIAS_ENA */
#define WM8903_MICBIAS_ENA_MASK                 0x0001  /* MICBIAS_ENA */
#define WM8903_MICBIAS_ENA_SHIFT                     0  /* MICBIAS_ENA */
#define WM8903_MICBIAS_ENA_WIDTH                     1  /* MICBIAS_ENA */

/*
 * R8 (0x08) - Analogue DAC 0
 */
#define WM8903_DACBIAS_SEL_MASK                 0x0018  /* DACBIAS_SEL - [4:3] */
#define WM8903_DACBIAS_SEL_SHIFT                     3  /* DACBIAS_SEL - [4:3] */
#define WM8903_DACBIAS_SEL_WIDTH                     2  /* DACBIAS_SEL - [4:3] */
#define WM8903_DACVMID_BIAS_SEL_MASK            0x0006  /* DACVMID_BIAS_SEL - [2:1] */
#define WM8903_DACVMID_BIAS_SEL_SHIFT                1  /* DACVMID_BIAS_SEL - [2:1] */
#define WM8903_DACVMID_BIAS_SEL_WIDTH                2  /* DACVMID_BIAS_SEL - [2:1] */

/*
 * R10 (0x0A) - Analogue ADC 0
 */
#define WM8903_ADC_OSR128                       0x0001  /* ADC_OSR128 */
#define WM8903_ADC_OSR128_MASK                  0x0001  /* ADC_OSR128 */
#define WM8903_ADC_OSR128_SHIFT                      0  /* ADC_OSR128 */
#define WM8903_ADC_OSR128_WIDTH                      1  /* ADC_OSR128 */

/*
 * R12 (0x0C) - Power Management 0
 */
#define WM8903_INL_ENA                          0x0002  /* INL_ENA */
#define WM8903_INL_ENA_MASK                     0x0002  /* INL_ENA */
#define WM8903_INL_ENA_SHIFT                         1  /* INL_ENA */
#define WM8903_INL_ENA_WIDTH                         1  /* INL_ENA */
#define WM8903_INR_ENA                          0x0001  /* INR_ENA */
#define WM8903_INR_ENA_MASK                     0x0001  /* INR_ENA */
#define WM8903_INR_ENA_SHIFT                         0  /* INR_ENA */
#define WM8903_INR_ENA_WIDTH                         1  /* INR_ENA */

/*
 * R13 (0x0D) - Power Management 1
 */
#define WM8903_MIXOUTL_ENA                      0x0002  /* MIXOUTL_ENA */
#define WM8903_MIXOUTL_ENA_MASK                 0x0002  /* MIXOUTL_ENA */
#define WM8903_MIXOUTL_ENA_SHIFT                     1  /* MIXOUTL_ENA */
#define WM8903_MIXOUTL_ENA_WIDTH                     1  /* MIXOUTL_ENA */
#define WM8903_MIXOUTR_ENA                      0x0001  /* MIXOUTR_ENA */
#define WM8903_MIXOUTR_ENA_MASK                 0x0001  /* MIXOUTR_ENA */
#define WM8903_MIXOUTR_ENA_SHIFT                     0  /* MIXOUTR_ENA */
#define WM8903_MIXOUTR_ENA_WIDTH                     1  /* MIXOUTR_ENA */

/*
 * R14 (0x0E) - Power Management 2
 */
#define WM8903_HPL_PGA_ENA                      0x0002  /* HPL_PGA_ENA */
#define WM8903_HPL_PGA_ENA_MASK                 0x0002  /* HPL_PGA_ENA */
#define WM8903_HPL_PGA_ENA_SHIFT                     1  /* HPL_PGA_ENA */
#define WM8903_HPL_PGA_ENA_WIDTH                     1  /* HPL_PGA_ENA */
#define WM8903_HPR_PGA_ENA                      0x0001  /* HPR_PGA_ENA */
#define WM8903_HPR_PGA_ENA_MASK                 0x0001  /* HPR_PGA_ENA */
#define WM8903_HPR_PGA_ENA_SHIFT                     0  /* HPR_PGA_ENA */
#define WM8903_HPR_PGA_ENA_WIDTH                     1  /* HPR_PGA_ENA */

/*
 * R15 (0x0F) - Power Management 3
 */
#define WM8903_LINEOUTL_PGA_ENA                 0x0002  /* LINEOUTL_PGA_ENA */
#define WM8903_LINEOUTL_PGA_ENA_MASK            0x0002  /* LINEOUTL_PGA_ENA */
#define WM8903_LINEOUTL_PGA_ENA_SHIFT                1  /* LINEOUTL_PGA_ENA */
#define WM8903_LINEOUTL_PGA_ENA_WIDTH                1  /* LINEOUTL_PGA_ENA */
#define WM8903_LINEOUTR_PGA_ENA                 0x0001  /* LINEOUTR_PGA_ENA */
#define WM8903_LINEOUTR_PGA_ENA_MASK            0x0001  /* LINEOUTR_PGA_ENA */
#define WM8903_LINEOUTR_PGA_ENA_SHIFT                0  /* LINEOUTR_PGA_ENA */
#define WM8903_LINEOUTR_PGA_ENA_WIDTH                1  /* LINEOUTR_PGA_ENA */

/*
 * R16 (0x10) - Power Management 4
 */
#define WM8903_MIXSPKL_ENA                      0x0002  /* MIXSPKL_ENA */
#define WM8903_MIXSPKL_ENA_MASK                 0x0002  /* MIXSPKL_ENA */
#define WM8903_MIXSPKL_ENA_SHIFT                     1  /* MIXSPKL_ENA */
#define WM8903_MIXSPKL_ENA_WIDTH                     1  /* MIXSPKL_ENA */
#define WM8903_MIXSPKR_ENA                      0x0001  /* MIXSPKR_ENA */
#define WM8903_MIXSPKR_ENA_MASK                 0x0001  /* MIXSPKR_ENA */
#define WM8903_MIXSPKR_ENA_SHIFT                     0  /* MIXSPKR_ENA */
#define WM8903_MIXSPKR_ENA_WIDTH                     1  /* MIXSPKR_ENA */

/*
 * R17 (0x11) - Power Management 5
 */
#define WM8903_SPKL_ENA                         0x0002  /* SPKL_ENA */
#define WM8903_SPKL_ENA_MASK                    0x0002  /* SPKL_ENA */
#define WM8903_SPKL_ENA_SHIFT                        1  /* SPKL_ENA */
#define WM8903_SPKL_ENA_WIDTH                        1  /* SPKL_ENA */
#define WM8903_SPKR_ENA                         0x0001  /* SPKR_ENA */
#define WM8903_SPKR_ENA_MASK                    0x0001  /* SPKR_ENA */
#define WM8903_SPKR_ENA_SHIFT                        0  /* SPKR_ENA */
#define WM8903_SPKR_ENA_WIDTH                        1  /* SPKR_ENA */

/*
 * R18 (0x12) - Power Management 6
 */
#define WM8903_DACL_ENA                         0x0008  /* DACL_ENA */
#define WM8903_DACL_ENA_MASK                    0x0008  /* DACL_ENA */
#define WM8903_DACL_ENA_SHIFT                        3  /* DACL_ENA */
#define WM8903_DACL_ENA_WIDTH                        1  /* DACL_ENA */
#define WM8903_DACR_ENA                         0x0004  /* DACR_ENA */
#define WM8903_DACR_ENA_MASK                    0x0004  /* DACR_ENA */
#define WM8903_DACR_ENA_SHIFT                        2  /* DACR_ENA */
#define WM8903_DACR_ENA_WIDTH                        1  /* DACR_ENA */
#define WM8903_ADCL_ENA                         0x0002  /* ADCL_ENA */
#define WM8903_ADCL_ENA_MASK                    0x0002  /* ADCL_ENA */
#define WM8903_ADCL_ENA_SHIFT                        1  /* ADCL_ENA */
#define WM8903_ADCL_ENA_WIDTH                        1  /* ADCL_ENA */
#define WM8903_ADCR_ENA                         0x0001  /* ADCR_ENA */
#define WM8903_ADCR_ENA_MASK                    0x0001  /* ADCR_ENA */
#define WM8903_ADCR_ENA_SHIFT                        0  /* ADCR_ENA */
#define WM8903_ADCR_ENA_WIDTH                        1  /* ADCR_ENA */

/*
 * R20 (0x14) - Clock Rates 0
 */
#define WM8903_MCLKDIV2                         0x0001  /* MCLKDIV2 */
#define WM8903_MCLKDIV2_MASK                    0x0001  /* MCLKDIV2 */
#define WM8903_MCLKDIV2_SHIFT                        0  /* MCLKDIV2 */
#define WM8903_MCLKDIV2_WIDTH                        1  /* MCLKDIV2 */

/*
 * R21 (0x15) - Clock Rates 1
 */
#define WM8903_CLK_SYS_RATE_MASK                0x3C00  /* CLK_SYS_RATE - [13:10] */
#define WM8903_CLK_SYS_RATE_SHIFT                   10  /* CLK_SYS_RATE - [13:10] */
#define WM8903_CLK_SYS_RATE_WIDTH                    4  /* CLK_SYS_RATE - [13:10] */
#define WM8903_CLK_SYS_MODE_MASK                0x0300  /* CLK_SYS_MODE - [9:8] */
#define WM8903_CLK_SYS_MODE_SHIFT                    8  /* CLK_SYS_MODE - [9:8] */
#define WM8903_CLK_SYS_MODE_WIDTH                    2  /* CLK_SYS_MODE - [9:8] */
#define WM8903_SAMPLE_RATE_MASK                 0x000F  /* SAMPLE_RATE - [3:0] */
#define WM8903_SAMPLE_RATE_SHIFT                     0  /* SAMPLE_RATE - [3:0] */
#define WM8903_SAMPLE_RATE_WIDTH                     4  /* SAMPLE_RATE - [3:0] */

/*
 * R22 (0x16) - Clock Rates 2
 */
#define WM8903_CLK_SYS_ENA                      0x0004  /* CLK_SYS_ENA */
#define WM8903_CLK_SYS_ENA_MASK                 0x0004  /* CLK_SYS_ENA */
#define WM8903_CLK_SYS_ENA_SHIFT                     2  /* CLK_SYS_ENA */
#define WM8903_CLK_SYS_ENA_WIDTH                     1  /* CLK_SYS_ENA */
#define WM8903_CLK_DSP_ENA                      0x0002  /* CLK_DSP_ENA */
#define WM8903_CLK_DSP_ENA_MASK                 0x0002  /* CLK_DSP_ENA */
#define WM8903_CLK_DSP_ENA_SHIFT                     1  /* CLK_DSP_ENA */
#define WM8903_CLK_DSP_ENA_WIDTH                     1  /* CLK_DSP_ENA */
#define WM8903_TO_ENA                           0x0001  /* TO_ENA */
#define WM8903_TO_ENA_MASK                      0x0001  /* TO_ENA */
#define WM8903_TO_ENA_SHIFT                          0  /* TO_ENA */
#define WM8903_TO_ENA_WIDTH                          1  /* TO_ENA */

/*
 * R24 (0x18) - Audio Interface 0
 */
#define WM8903_DACL_DATINV                      0x1000  /* DACL_DATINV */
#define WM8903_DACL_DATINV_MASK                 0x1000  /* DACL_DATINV */
#define WM8903_DACL_DATINV_SHIFT                    12  /* DACL_DATINV */
#define WM8903_DACL_DATINV_WIDTH                     1  /* DACL_DATINV */
#define WM8903_DACR_DATINV                      0x0800  /* DACR_DATINV */
#define WM8903_DACR_DATINV_MASK                 0x0800  /* DACR_DATINV */
#define WM8903_DACR_DATINV_SHIFT                    11  /* DACR_DATINV */
#define WM8903_DACR_DATINV_WIDTH                     1  /* DACR_DATINV */
#define WM8903_DAC_BOOST_MASK                   0x0600  /* DAC_BOOST - [10:9] */
#define WM8903_DAC_BOOST_SHIFT                       9  /* DAC_BOOST - [10:9] */
#define WM8903_DAC_BOOST_WIDTH                       2  /* DAC_BOOST - [10:9] */
#define WM8903_LOOPBACK                         0x0100  /* LOOPBACK */
#define WM8903_LOOPBACK_MASK                    0x0100  /* LOOPBACK */
#define WM8903_LOOPBACK_SHIFT                        8  /* LOOPBACK */
#define WM8903_LOOPBACK_WIDTH                        1  /* LOOPBACK */
#define WM8903_AIFADCL_SRC                      0x0080  /* AIFADCL_SRC */
#define WM8903_AIFADCL_SRC_MASK                 0x0080  /* AIFADCL_SRC */
#define WM8903_AIFADCL_SRC_SHIFT                     7  /* AIFADCL_SRC */
#define WM8903_AIFADCL_SRC_WIDTH                     1  /* AIFADCL_SRC */
#define WM8903_AIFADCR_SRC                      0x0040  /* AIFADCR_SRC */
#define WM8903_AIFADCR_SRC_MASK                 0x0040  /* AIFADCR_SRC */
#define WM8903_AIFADCR_SRC_SHIFT                     6  /* AIFADCR_SRC */
#define WM8903_AIFADCR_SRC_WIDTH                     1  /* AIFADCR_SRC */
#define WM8903_AIFDACL_SRC                      0x0020  /* AIFDACL_SRC */
#define WM8903_AIFDACL_SRC_MASK                 0x0020  /* AIFDACL_SRC */
#define WM8903_AIFDACL_SRC_SHIFT                     5  /* AIFDACL_SRC */
#define WM8903_AIFDACL_SRC_WIDTH                     1  /* AIFDACL_SRC */
#define WM8903_AIFDACR_SRC                      0x0010  /* AIFDACR_SRC */
#define WM8903_AIFDACR_SRC_MASK                 0x0010  /* AIFDACR_SRC */
#define WM8903_AIFDACR_SRC_SHIFT                     4  /* AIFDACR_SRC */
#define WM8903_AIFDACR_SRC_WIDTH                     1  /* AIFDACR_SRC */
#define WM8903_ADC_COMP                         0x0008  /* ADC_COMP */
#define WM8903_ADC_COMP_MASK                    0x0008  /* ADC_COMP */
#define WM8903_ADC_COMP_SHIFT                        3  /* ADC_COMP */
#define WM8903_ADC_COMP_WIDTH                        1  /* ADC_COMP */
#define WM8903_ADC_COMPMODE                     0x0004  /* ADC_COMPMODE */
#define WM8903_ADC_COMPMODE_MASK                0x0004  /* ADC_COMPMODE */
#define WM8903_ADC_COMPMODE_SHIFT                    2  /* ADC_COMPMODE */
#define WM8903_ADC_COMPMODE_WIDTH                    1  /* ADC_COMPMODE */
#define WM8903_DAC_COMP                         0x0002  /* DAC_COMP */
#define WM8903_DAC_COMP_MASK                    0x0002  /* DAC_COMP */
#define WM8903_DAC_COMP_SHIFT                        1  /* DAC_COMP */
#define WM8903_DAC_COMP_WIDTH                        1  /* DAC_COMP */
#define WM8903_DAC_COMPMODE                     0x0001  /* DAC_COMPMODE */
#define WM8903_DAC_COMPMODE_MASK                0x0001  /* DAC_COMPMODE */
#define WM8903_DAC_COMPMODE_SHIFT                    0  /* DAC_COMPMODE */
#define WM8903_DAC_COMPMODE_WIDTH                    1  /* DAC_COMPMODE */

/*
 * R25 (0x19) - Audio Interface 1
 */
#define WM8903_AIFDAC_TDM                       0x2000  /* AIFDAC_TDM */
#define WM8903_AIFDAC_TDM_MASK                  0x2000  /* AIFDAC_TDM */
#define WM8903_AIFDAC_TDM_SHIFT                     13  /* AIFDAC_TDM */
#define WM8903_AIFDAC_TDM_WIDTH                      1  /* AIFDAC_TDM */
#define WM8903_AIFDAC_TDM_CHAN                  0x1000  /* AIFDAC_TDM_CHAN */
#define WM8903_AIFDAC_TDM_CHAN_MASK             0x1000  /* AIFDAC_TDM_CHAN */
#define WM8903_AIFDAC_TDM_CHAN_SHIFT                12  /* AIFDAC_TDM_CHAN */
#define WM8903_AIFDAC_TDM_CHAN_WIDTH                 1  /* AIFDAC_TDM_CHAN */
#define WM8903_AIFADC_TDM                       0x0800  /* AIFADC_TDM */
#define WM8903_AIFADC_TDM_MASK                  0x0800  /* AIFADC_TDM */
#define WM8903_AIFADC_TDM_SHIFT                     11  /* AIFADC_TDM */
#define WM8903_AIFADC_TDM_WIDTH                      1  /* AIFADC_TDM */
#define WM8903_AIFADC_TDM_CHAN                  0x0400  /* AIFADC_TDM_CHAN */
#define WM8903_AIFADC_TDM_CHAN_MASK             0x0400  /* AIFADC_TDM_CHAN */
#define WM8903_AIFADC_TDM_CHAN_SHIFT                10  /* AIFADC_TDM_CHAN */
#define WM8903_AIFADC_TDM_CHAN_WIDTH                 1  /* AIFADC_TDM_CHAN */
#define WM8903_LRCLK_DIR                        0x0200  /* LRCLK_DIR */
#define WM8903_LRCLK_DIR_MASK                   0x0200  /* LRCLK_DIR */
#define WM8903_LRCLK_DIR_SHIFT                       9  /* LRCLK_DIR */
#define WM8903_LRCLK_DIR_WIDTH                       1  /* LRCLK_DIR */
#define WM8903_AIF_BCLK_INV                     0x0080  /* AIF_BCLK_INV */
#define WM8903_AIF_BCLK_INV_MASK                0x0080  /* AIF_BCLK_INV */
#define WM8903_AIF_BCLK_INV_SHIFT                    7  /* AIF_BCLK_INV */
#define WM8903_AIF_BCLK_INV_WIDTH                    1  /* AIF_BCLK_INV */
#define WM8903_BCLK_DIR                         0x0040  /* BCLK_DIR */
#define WM8903_BCLK_DIR_MASK                    0x0040  /* BCLK_DIR */
#define WM8903_BCLK_DIR_SHIFT                        6  /* BCLK_DIR */
#define WM8903_BCLK_DIR_WIDTH                        1  /* BCLK_DIR */
#define WM8903_AIF_LRCLK_INV                    0x0010  /* AIF_LRCLK_INV */
#define WM8903_AIF_LRCLK_INV_MASK               0x0010  /* AIF_LRCLK_INV */
#define WM8903_AIF_LRCLK_INV_SHIFT                   4  /* AIF_LRCLK_INV */
#define WM8903_AIF_LRCLK_INV_WIDTH                   1  /* AIF_LRCLK_INV */
#define WM8903_AIF_WL_MASK                      0x000C  /* AIF_WL - [3:2] */
#define WM8903_AIF_WL_SHIFT                          2  /* AIF_WL - [3:2] */
#define WM8903_AIF_WL_WIDTH                          2  /* AIF_WL - [3:2] */
#define WM8903_AIF_FMT_MASK                     0x0003  /* AIF_FMT - [1:0] */
#define WM8903_AIF_FMT_SHIFT                         0  /* AIF_FMT - [1:0] */
#define WM8903_AIF_FMT_WIDTH                         2  /* AIF_FMT - [1:0] */

/*
 * R26 (0x1A) - Audio Interface 2
 */
#define WM8903_BCLK_DIV_MASK                    0x001F  /* BCLK_DIV - [4:0] */
#define WM8903_BCLK_DIV_SHIFT                        0  /* BCLK_DIV - [4:0] */
#define WM8903_BCLK_DIV_WIDTH                        5  /* BCLK_DIV - [4:0] */

/*
 * R27 (0x1B) - Audio Interface 3
 */
#define WM8903_LRCLK_RATE_MASK                  0x07FF  /* LRCLK_RATE - [10:0] */
#define WM8903_LRCLK_RATE_SHIFT                      0  /* LRCLK_RATE - [10:0] */
#define WM8903_LRCLK_RATE_WIDTH                     11  /* LRCLK_RATE - [10:0] */

/*
 * R30 (0x1E) - DAC Digital Volume Left
 */
#define WM8903_DACVU                            0x0100  /* DACVU */
#define WM8903_DACVU_MASK                       0x0100  /* DACVU */
#define WM8903_DACVU_SHIFT                           8  /* DACVU */
#define WM8903_DACVU_WIDTH                           1  /* DACVU */
#define WM8903_DACL_VOL_MASK                    0x00FF  /* DACL_VOL - [7:0] */
#define WM8903_DACL_VOL_SHIFT                        0  /* DACL_VOL - [7:0] */
#define WM8903_DACL_VOL_WIDTH                        8  /* DACL_VOL - [7:0] */

/*
 * R31 (0x1F) - DAC Digital Volume Right
 */
#define WM8903_DACVU                            0x0100  /* DACVU */
#define WM8903_DACVU_MASK                       0x0100  /* DACVU */
#define WM8903_DACVU_SHIFT                           8  /* DACVU */
#define WM8903_DACVU_WIDTH                           1  /* DACVU */
#define WM8903_DACR_VOL_MASK                    0x00FF  /* DACR_VOL - [7:0] */
#define WM8903_DACR_VOL_SHIFT                        0  /* DACR_VOL - [7:0] */
#define WM8903_DACR_VOL_WIDTH                        8  /* DACR_VOL - [7:0] */

/*
 * R32 (0x20) - DAC Digital 0
 */
#define WM8903_ADCL_DAC_SVOL_MASK               0x0F00  /* ADCL_DAC_SVOL - [11:8] */
#define WM8903_ADCL_DAC_SVOL_SHIFT                   8  /* ADCL_DAC_SVOL - [11:8] */
#define WM8903_ADCL_DAC_SVOL_WIDTH                   4  /* ADCL_DAC_SVOL - [11:8] */
#define WM8903_ADCR_DAC_SVOL_MASK               0x00F0  /* ADCR_DAC_SVOL - [7:4] */
#define WM8903_ADCR_DAC_SVOL_SHIFT                   4  /* ADCR_DAC_SVOL - [7:4] */
#define WM8903_ADCR_DAC_SVOL_WIDTH                   4  /* ADCR_DAC_SVOL - [7:4] */
#define WM8903_ADC_TO_DACL_MASK                 0x000C  /* ADC_TO_DACL - [3:2] */
#define WM8903_ADC_TO_DACL_SHIFT                     2  /* ADC_TO_DACL - [3:2] */
#define WM8903_ADC_TO_DACL_WIDTH                     2  /* ADC_TO_DACL - [3:2] */
#define WM8903_ADC_TO_DACR_MASK                 0x0003  /* ADC_TO_DACR - [1:0] */
#define WM8903_ADC_TO_DACR_SHIFT                     0  /* ADC_TO_DACR - [1:0] */
#define WM8903_ADC_TO_DACR_WIDTH                     2  /* ADC_TO_DACR - [1:0] */

/*
 * R33 (0x21) - DAC Digital 1
 */
#define WM8903_DAC_MONO                         0x1000  /* DAC_MONO */
#define WM8903_DAC_MONO_MASK                    0x1000  /* DAC_MONO */
#define WM8903_DAC_MONO_SHIFT                       12  /* DAC_MONO */
#define WM8903_DAC_MONO_WIDTH                        1  /* DAC_MONO */
#define WM8903_DAC_SB_FILT                      0x0800  /* DAC_SB_FILT */
#define WM8903_DAC_SB_FILT_MASK                 0x0800  /* DAC_SB_FILT */
#define WM8903_DAC_SB_FILT_SHIFT                    11  /* DAC_SB_FILT */
#define WM8903_DAC_SB_FILT_WIDTH                     1  /* DAC_SB_FILT */
#define WM8903_DAC_MUTERATE                     0x0400  /* DAC_MUTERATE */
#define WM8903_DAC_MUTERATE_MASK                0x0400  /* DAC_MUTERATE */
#define WM8903_DAC_MUTERATE_SHIFT                   10  /* DAC_MUTERATE */
#define WM8903_DAC_MUTERATE_WIDTH                    1  /* DAC_MUTERATE */
#define WM8903_DAC_MUTEMODE                     0x0200  /* DAC_MUTEMODE */
#define WM8903_DAC_MUTEMODE_MASK                0x0200  /* DAC_MUTEMODE */
#define WM8903_DAC_MUTEMODE_SHIFT                    9  /* DAC_MUTEMODE */
#define WM8903_DAC_MUTEMODE_WIDTH                    1  /* DAC_MUTEMODE */
#define WM8903_DAC_MUTE                         0x0008  /* DAC_MUTE */
#define WM8903_DAC_MUTE_MASK                    0x0008  /* DAC_MUTE */
#define WM8903_DAC_MUTE_SHIFT                        3  /* DAC_MUTE */
#define WM8903_DAC_MUTE_WIDTH                        1  /* DAC_MUTE */
#define WM8903_DEEMPH_MASK                      0x0006  /* DEEMPH - [2:1] */
#define WM8903_DEEMPH_SHIFT                          1  /* DEEMPH - [2:1] */
#define WM8903_DEEMPH_WIDTH                          2  /* DEEMPH - [2:1] */

/*
 * R36 (0x24) - ADC Digital Volume Left
 */
#define WM8903_ADCVU                            0x0100  /* ADCVU */
#define WM8903_ADCVU_MASK                       0x0100  /* ADCVU */
#define WM8903_ADCVU_SHIFT                           8  /* ADCVU */
#define WM8903_ADCVU_WIDTH                           1  /* ADCVU */
#define WM8903_ADCL_VOL_MASK                    0x00FF  /* ADCL_VOL - [7:0] */
#define WM8903_ADCL_VOL_SHIFT                        0  /* ADCL_VOL - [7:0] */
#define WM8903_ADCL_VOL_WIDTH                        8  /* ADCL_VOL - [7:0] */

/*
 * R37 (0x25) - ADC Digital Volume Right
 */
#define WM8903_ADCVU                            0x0100  /* ADCVU */
#define WM8903_ADCVU_MASK                       0x0100  /* ADCVU */
#define WM8903_ADCVU_SHIFT                           8  /* ADCVU */
#define WM8903_ADCVU_WIDTH                           1  /* ADCVU */
#define WM8903_ADCR_VOL_MASK                    0x00FF  /* ADCR_VOL - [7:0] */
#define WM8903_ADCR_VOL_SHIFT                        0  /* ADCR_VOL - [7:0] */
#define WM8903_ADCR_VOL_WIDTH                        8  /* ADCR_VOL - [7:0] */

/*
 * R38 (0x26) - ADC Digital 0
 */
#define WM8903_ADC_HPF_CUT_MASK                 0x0060  /* ADC_HPF_CUT - [6:5] */
#define WM8903_ADC_HPF_CUT_SHIFT                     5  /* ADC_HPF_CUT - [6:5] */
#define WM8903_ADC_HPF_CUT_WIDTH                     2  /* ADC_HPF_CUT - [6:5] */
#define WM8903_ADC_HPF_ENA                      0x0010  /* ADC_HPF_ENA */
#define WM8903_ADC_HPF_ENA_MASK                 0x0010  /* ADC_HPF_ENA */
#define WM8903_ADC_HPF_ENA_SHIFT                     4  /* ADC_HPF_ENA */
#define WM8903_ADC_HPF_ENA_WIDTH                     1  /* ADC_HPF_ENA */
#define WM8903_ADCL_DATINV                      0x0002  /* ADCL_DATINV */
#define WM8903_ADCL_DATINV_MASK                 0x0002  /* ADCL_DATINV */
#define WM8903_ADCL_DATINV_SHIFT                     1  /* ADCL_DATINV */
#define WM8903_ADCL_DATINV_WIDTH                     1  /* ADCL_DATINV */
#define WM8903_ADCR_DATINV                      0x0001  /* ADCR_DATINV */
#define WM8903_ADCR_DATINV_MASK                 0x0001  /* ADCR_DATINV */
#define WM8903_ADCR_DATINV_SHIFT                     0  /* ADCR_DATINV */
#define WM8903_ADCR_DATINV_WIDTH                     1  /* ADCR_DATINV */

/*
 * R39 (0x27) - Digital Microphone 0
 */
#define WM8903_DIGMIC_MODE_SEL                  0x0100  /* DIGMIC_MODE_SEL */
#define WM8903_DIGMIC_MODE_SEL_MASK             0x0100  /* DIGMIC_MODE_SEL */
#define WM8903_DIGMIC_MODE_SEL_SHIFT                 8  /* DIGMIC_MODE_SEL */
#define WM8903_DIGMIC_MODE_SEL_WIDTH                 1  /* DIGMIC_MODE_SEL */
#define WM8903_DIGMIC_CLK_SEL_L_MASK            0x00C0  /* DIGMIC_CLK_SEL_L - [7:6] */
#define WM8903_DIGMIC_CLK_SEL_L_SHIFT                6  /* DIGMIC_CLK_SEL_L - [7:6] */
#define WM8903_DIGMIC_CLK_SEL_L_WIDTH                2  /* DIGMIC_CLK_SEL_L - [7:6] */
#define WM8903_DIGMIC_CLK_SEL_R_MASK            0x0030  /* DIGMIC_CLK_SEL_R - [5:4] */
#define WM8903_DIGMIC_CLK_SEL_R_SHIFT                4  /* DIGMIC_CLK_SEL_R - [5:4] */
#define WM8903_DIGMIC_CLK_SEL_R_WIDTH                2  /* DIGMIC_CLK_SEL_R - [5:4] */
#define WM8903_DIGMIC_CLK_SEL_RT_MASK           0x000C  /* DIGMIC_CLK_SEL_RT - [3:2] */
#define WM8903_DIGMIC_CLK_SEL_RT_SHIFT               2  /* DIGMIC_CLK_SEL_RT - [3:2] */
#define WM8903_DIGMIC_CLK_SEL_RT_WIDTH               2  /* DIGMIC_CLK_SEL_RT - [3:2] */
#define WM8903_DIGMIC_CLK_SEL_MASK              0x0003  /* DIGMIC_CLK_SEL - [1:0] */
#define WM8903_DIGMIC_CLK_SEL_SHIFT                  0  /* DIGMIC_CLK_SEL - [1:0] */
#define WM8903_DIGMIC_CLK_SEL_WIDTH                  2  /* DIGMIC_CLK_SEL - [1:0] */

/*
 * R40 (0x28) - DRC 0
 */
#define WM8903_DRC_ENA                          0x8000  /* DRC_ENA */
#define WM8903_DRC_ENA_MASK                     0x8000  /* DRC_ENA */
#define WM8903_DRC_ENA_SHIFT                        15  /* DRC_ENA */
#define WM8903_DRC_ENA_WIDTH                         1  /* DRC_ENA */
#define WM8903_DRC_THRESH_HYST_MASK             0x1800  /* DRC_THRESH_HYST - [12:11] */
#define WM8903_DRC_THRESH_HYST_SHIFT                11  /* DRC_THRESH_HYST - [12:11] */
#define WM8903_DRC_THRESH_HYST_WIDTH                 2  /* DRC_THRESH_HYST - [12:11] */
#define WM8903_DRC_STARTUP_GAIN_MASK            0x07C0  /* DRC_STARTUP_GAIN - [10:6] */
#define WM8903_DRC_STARTUP_GAIN_SHIFT                6  /* DRC_STARTUP_GAIN - [10:6] */
#define WM8903_DRC_STARTUP_GAIN_WIDTH                5  /* DRC_STARTUP_GAIN - [10:6] */
#define WM8903_DRC_FF_DELAY                     0x0020  /* DRC_FF_DELAY */
#define WM8903_DRC_FF_DELAY_MASK                0x0020  /* DRC_FF_DELAY */
#define WM8903_DRC_FF_DELAY_SHIFT                    5  /* DRC_FF_DELAY */
#define WM8903_DRC_FF_DELAY_WIDTH                    1  /* DRC_FF_DELAY */
#define WM8903_DRC_SMOOTH_ENA                   0x0008  /* DRC_SMOOTH_ENA */
#define WM8903_DRC_SMOOTH_ENA_MASK              0x0008  /* DRC_SMOOTH_ENA */
#define WM8903_DRC_SMOOTH_ENA_SHIFT                  3  /* DRC_SMOOTH_ENA */
#define WM8903_DRC_SMOOTH_ENA_WIDTH                  1  /* DRC_SMOOTH_ENA */
#define WM8903_DRC_QR_ENA                       0x0004  /* DRC_QR_ENA */
#define WM8903_DRC_QR_ENA_MASK                  0x0004  /* DRC_QR_ENA */
#define WM8903_DRC_QR_ENA_SHIFT                      2  /* DRC_QR_ENA */
#define WM8903_DRC_QR_ENA_WIDTH                      1  /* DRC_QR_ENA */
#define WM8903_DRC_ANTICLIP_ENA                 0x0002  /* DRC_ANTICLIP_ENA */
#define WM8903_DRC_ANTICLIP_ENA_MASK            0x0002  /* DRC_ANTICLIP_ENA */
#define WM8903_DRC_ANTICLIP_ENA_SHIFT                1  /* DRC_ANTICLIP_ENA */
#define WM8903_DRC_ANTICLIP_ENA_WIDTH                1  /* DRC_ANTICLIP_ENA */
#define WM8903_DRC_HYST_ENA                     0x0001  /* DRC_HYST_ENA */
#define WM8903_DRC_HYST_ENA_MASK                0x0001  /* DRC_HYST_ENA */
#define WM8903_DRC_HYST_ENA_SHIFT                    0  /* DRC_HYST_ENA */
#define WM8903_DRC_HYST_ENA_WIDTH                    1  /* DRC_HYST_ENA */

/*
 * R41 (0x29) - DRC 1
 */
#define WM8903_DRC_ATTACK_RATE_MASK             0xF000  /* DRC_ATTACK_RATE - [15:12] */
#define WM8903_DRC_ATTACK_RATE_SHIFT                12  /* DRC_ATTACK_RATE - [15:12] */
#define WM8903_DRC_ATTACK_RATE_WIDTH                 4  /* DRC_ATTACK_RATE - [15:12] */
#define WM8903_DRC_DECAY_RATE_MASK              0x0F00  /* DRC_DECAY_RATE - [11:8] */
#define WM8903_DRC_DECAY_RATE_SHIFT                  8  /* DRC_DECAY_RATE - [11:8] */
#define WM8903_DRC_DECAY_RATE_WIDTH                  4  /* DRC_DECAY_RATE - [11:8] */
#define WM8903_DRC_THRESH_QR_MASK               0x00C0  /* DRC_THRESH_QR - [7:6] */
#define WM8903_DRC_THRESH_QR_SHIFT                   6  /* DRC_THRESH_QR - [7:6] */
#define WM8903_DRC_THRESH_QR_WIDTH                   2  /* DRC_THRESH_QR - [7:6] */
#define WM8903_DRC_RATE_QR_MASK                 0x0030  /* DRC_RATE_QR - [5:4] */
#define WM8903_DRC_RATE_QR_SHIFT                     4  /* DRC_RATE_QR - [5:4] */
#define WM8903_DRC_RATE_QR_WIDTH                     2  /* DRC_RATE_QR - [5:4] */
#define WM8903_DRC_MINGAIN_MASK                 0x000C  /* DRC_MINGAIN - [3:2] */
#define WM8903_DRC_MINGAIN_SHIFT                     2  /* DRC_MINGAIN - [3:2] */
#define WM8903_DRC_MINGAIN_WIDTH                     2  /* DRC_MINGAIN - [3:2] */
#define WM8903_DRC_MAXGAIN_MASK                 0x0003  /* DRC_MAXGAIN - [1:0] */
#define WM8903_DRC_MAXGAIN_SHIFT                     0  /* DRC_MAXGAIN - [1:0] */
#define WM8903_DRC_MAXGAIN_WIDTH                     2  /* DRC_MAXGAIN - [1:0] */

/*
 * R42 (0x2A) - DRC 2
 */
#define WM8903_DRC_R0_SLOPE_COMP_MASK           0x0038  /* DRC_R0_SLOPE_COMP - [5:3] */
#define WM8903_DRC_R0_SLOPE_COMP_SHIFT               3  /* DRC_R0_SLOPE_COMP - [5:3] */
#define WM8903_DRC_R0_SLOPE_COMP_WIDTH               3  /* DRC_R0_SLOPE_COMP - [5:3] */
#define WM8903_DRC_R1_SLOPE_COMP_MASK           0x0007  /* DRC_R1_SLOPE_COMP - [2:0] */
#define WM8903_DRC_R1_SLOPE_COMP_SHIFT               0  /* DRC_R1_SLOPE_COMP - [2:0] */
#define WM8903_DRC_R1_SLOPE_COMP_WIDTH               3  /* DRC_R1_SLOPE_COMP - [2:0] */

/*
 * R43 (0x2B) - DRC 3
 */
#define WM8903_DRC_THRESH_COMP_MASK             0x07E0  /* DRC_THRESH_COMP - [10:5] */
#define WM8903_DRC_THRESH_COMP_SHIFT                 5  /* DRC_THRESH_COMP - [10:5] */
#define WM8903_DRC_THRESH_COMP_WIDTH                 6  /* DRC_THRESH_COMP - [10:5] */
#define WM8903_DRC_AMP_COMP_MASK                0x001F  /* DRC_AMP_COMP - [4:0] */
#define WM8903_DRC_AMP_COMP_SHIFT                    0  /* DRC_AMP_COMP - [4:0] */
#define WM8903_DRC_AMP_COMP_WIDTH                    5  /* DRC_AMP_COMP - [4:0] */

/*
 * R44 (0x2C) - Analogue Left Input 0
 */
#define WM8903_LINMUTE                          0x0080  /* LINMUTE */
#define WM8903_LINMUTE_MASK                     0x0080  /* LINMUTE */
#define WM8903_LINMUTE_SHIFT                         7  /* LINMUTE */
#define WM8903_LINMUTE_WIDTH                         1  /* LINMUTE */
#define WM8903_LIN_VOL_MASK                     0x001F  /* LIN_VOL - [4:0] */
#define WM8903_LIN_VOL_SHIFT                         0  /* LIN_VOL - [4:0] */
#define WM8903_LIN_VOL_WIDTH                         5  /* LIN_VOL - [4:0] */

/*
 * R45 (0x2D) - Analogue Right Input 0
 */
#define WM8903_RINMUTE                          0x0080  /* RINMUTE */
#define WM8903_RINMUTE_MASK                     0x0080  /* RINMUTE */
#define WM8903_RINMUTE_SHIFT                         7  /* RINMUTE */
#define WM8903_RINMUTE_WIDTH                         1  /* RINMUTE */
#define WM8903_RIN_VOL_MASK                     0x001F  /* RIN_VOL - [4:0] */
#define WM8903_RIN_VOL_SHIFT                         0  /* RIN_VOL - [4:0] */
#define WM8903_RIN_VOL_WIDTH                         5  /* RIN_VOL - [4:0] */

/*
 * R46 (0x2E) - Analogue Left Input 1
 */
#define WM8903_INL_CM_ENA                       0x0040  /* INL_CM_ENA */
#define WM8903_INL_CM_ENA_MASK                  0x0040  /* INL_CM_ENA */
#define WM8903_INL_CM_ENA_SHIFT                      6  /* INL_CM_ENA */
#define WM8903_INL_CM_ENA_WIDTH                      1  /* INL_CM_ENA */
#define WM8903_L_IP_SEL_N_MASK                  0x0030  /* L_IP_SEL_N - [5:4] */
#define WM8903_L_IP_SEL_N_SHIFT                      4  /* L_IP_SEL_N - [5:4] */
#define WM8903_L_IP_SEL_N_WIDTH                      2  /* L_IP_SEL_N - [5:4] */
#define WM8903_L_IP_SEL_P_MASK                  0x000C  /* L_IP_SEL_P - [3:2] */
#define WM8903_L_IP_SEL_P_SHIFT                      2  /* L_IP_SEL_P - [3:2] */
#define WM8903_L_IP_SEL_P_WIDTH                      2  /* L_IP_SEL_P - [3:2] */
#define WM8903_L_MODE_MASK                      0x0003  /* L_MODE - [1:0] */
#define WM8903_L_MODE_SHIFT                          0  /* L_MODE - [1:0] */
#define WM8903_L_MODE_WIDTH                          2  /* L_MODE - [1:0] */

/*
 * R47 (0x2F) - Analogue Right Input 1
 */
#define WM8903_INR_CM_ENA                       0x0040  /* INR_CM_ENA */
#define WM8903_INR_CM_ENA_MASK                  0x0040  /* INR_CM_ENA */
#define WM8903_INR_CM_ENA_SHIFT                      6  /* INR_CM_ENA */
#define WM8903_INR_CM_ENA_WIDTH                      1  /* INR_CM_ENA */
#define WM8903_R_IP_SEL_N_MASK                  0x0030  /* R_IP_SEL_N - [5:4] */
#define WM8903_R_IP_SEL_N_SHIFT                      4  /* R_IP_SEL_N - [5:4] */
#define WM8903_R_IP_SEL_N_WIDTH                      2  /* R_IP_SEL_N - [5:4] */
#define WM8903_R_IP_SEL_P_MASK                  0x000C  /* R_IP_SEL_P - [3:2] */
#define WM8903_R_IP_SEL_P_SHIFT                      2  /* R_IP_SEL_P - [3:2] */
#define WM8903_R_IP_SEL_P_WIDTH                      2  /* R_IP_SEL_P - [3:2] */
#define WM8903_R_MODE_MASK                      0x0003  /* R_MODE - [1:0] */
#define WM8903_R_MODE_SHIFT                          0  /* R_MODE - [1:0] */
#define WM8903_R_MODE_WIDTH                          2  /* R_MODE - [1:0] */

/*
 * R50 (0x32) - Analogue Left Mix 0
 */
#define WM8903_DACL_TO_MIXOUTL                  0x0008  /* DACL_TO_MIXOUTL */
#define WM8903_DACL_TO_MIXOUTL_MASK             0x0008  /* DACL_TO_MIXOUTL */
#define WM8903_DACL_TO_MIXOUTL_SHIFT                 3  /* DACL_TO_MIXOUTL */
#define WM8903_DACL_TO_MIXOUTL_WIDTH                 1  /* DACL_TO_MIXOUTL */
#define WM8903_DACR_TO_MIXOUTL                  0x0004  /* DACR_TO_MIXOUTL */
#define WM8903_DACR_TO_MIXOUTL_MASK             0x0004  /* DACR_TO_MIXOUTL */
#define WM8903_DACR_TO_MIXOUTL_SHIFT                 2  /* DACR_TO_MIXOUTL */
#define WM8903_DACR_TO_MIXOUTL_WIDTH                 1  /* DACR_TO_MIXOUTL */
#define WM8903_BYPASSL_TO_MIXOUTL               0x0002  /* BYPASSL_TO_MIXOUTL */
#define WM8903_BYPASSL_TO_MIXOUTL_MASK          0x0002  /* BYPASSL_TO_MIXOUTL */
#define WM8903_BYPASSL_TO_MIXOUTL_SHIFT              1  /* BYPASSL_TO_MIXOUTL */
#define WM8903_BYPASSL_TO_MIXOUTL_WIDTH              1  /* BYPASSL_TO_MIXOUTL */
#define WM8903_BYPASSR_TO_MIXOUTL               0x0001  /* BYPASSR_TO_MIXOUTL */
#define WM8903_BYPASSR_TO_MIXOUTL_MASK          0x0001  /* BYPASSR_TO_MIXOUTL */
#define WM8903_BYPASSR_TO_MIXOUTL_SHIFT              0  /* BYPASSR_TO_MIXOUTL */
#define WM8903_BYPASSR_TO_MIXOUTL_WIDTH              1  /* BYPASSR_TO_MIXOUTL */

/*
 * R51 (0x33) - Analogue Right Mix 0
 */
#define WM8903_DACL_TO_MIXOUTR                  0x0008  /* DACL_TO_MIXOUTR */
#define WM8903_DACL_TO_MIXOUTR_MASK             0x0008  /* DACL_TO_MIXOUTR */
#define WM8903_DACL_TO_MIXOUTR_SHIFT                 3  /* DACL_TO_MIXOUTR */
#define WM8903_DACL_TO_MIXOUTR_WIDTH                 1  /* DACL_TO_MIXOUTR */
#define WM8903_DACR_TO_MIXOUTR                  0x0004  /* DACR_TO_MIXOUTR */
#define WM8903_DACR_TO_MIXOUTR_MASK             0x0004  /* DACR_TO_MIXOUTR */
#define WM8903_DACR_TO_MIXOUTR_SHIFT                 2  /* DACR_TO_MIXOUTR */
#define WM8903_DACR_TO_MIXOUTR_WIDTH                 1  /* DACR_TO_MIXOUTR */
#define WM8903_BYPASSL_TO_MIXOUTR               0x0002  /* BYPASSL_TO_MIXOUTR */
#define WM8903_BYPASSL_TO_MIXOUTR_MASK          0x0002  /* BYPASSL_TO_MIXOUTR */
#define WM8903_BYPASSL_TO_MIXOUTR_SHIFT              1  /* BYPASSL_TO_MIXOUTR */
#define WM8903_BYPASSL_TO_MIXOUTR_WIDTH              1  /* BYPASSL_TO_MIXOUTR */
#define WM8903_BYPASSR_TO_MIXOUTR               0x0001  /* BYPASSR_TO_MIXOUTR */
#define WM8903_BYPASSR_TO_MIXOUTR_MASK          0x0001  /* BYPASSR_TO_MIXOUTR */
#define WM8903_BYPASSR_TO_MIXOUTR_SHIFT              0  /* BYPASSR_TO_MIXOUTR */
#define WM8903_BYPASSR_TO_MIXOUTR_WIDTH              1  /* BYPASSR_TO_MIXOUTR */

/*
 * R52 (0x34) - Analogue Spk Mix Left 0
 */
#define WM8903_DACL_TO_MIXSPKL                  0x0008  /* DACL_TO_MIXSPKL */
#define WM8903_DACL_TO_MIXSPKL_MASK             0x0008  /* DACL_TO_MIXSPKL */
#define WM8903_DACL_TO_MIXSPKL_SHIFT                 3  /* DACL_TO_MIXSPKL */
#define WM8903_DACL_TO_MIXSPKL_WIDTH                 1  /* DACL_TO_MIXSPKL */
#define WM8903_DACR_TO_MIXSPKL                  0x0004  /* DACR_TO_MIXSPKL */
#define WM8903_DACR_TO_MIXSPKL_MASK             0x0004  /* DACR_TO_MIXSPKL */
#define WM8903_DACR_TO_MIXSPKL_SHIFT                 2  /* DACR_TO_MIXSPKL */
#define WM8903_DACR_TO_MIXSPKL_WIDTH                 1  /* DACR_TO_MIXSPKL */
#define WM8903_BYPASSL_TO_MIXSPKL               0x0002  /* BYPASSL_TO_MIXSPKL */
#define WM8903_BYPASSL_TO_MIXSPKL_MASK          0x0002  /* BYPASSL_TO_MIXSPKL */
#define WM8903_BYPASSL_TO_MIXSPKL_SHIFT              1  /* BYPASSL_TO_MIXSPKL */
#define WM8903_BYPASSL_TO_MIXSPKL_WIDTH              1  /* BYPASSL_TO_MIXSPKL */
#define WM8903_BYPASSR_TO_MIXSPKL               0x0001  /* BYPASSR_TO_MIXSPKL */
#define WM8903_BYPASSR_TO_MIXSPKL_MASK          0x0001  /* BYPASSR_TO_MIXSPKL */
#define WM8903_BYPASSR_TO_MIXSPKL_SHIFT              0  /* BYPASSR_TO_MIXSPKL */
#define WM8903_BYPASSR_TO_MIXSPKL_WIDTH              1  /* BYPASSR_TO_MIXSPKL */

/*
 * R53 (0x35) - Analogue Spk Mix Left 1
 */
#define WM8903_DACL_MIXSPKL_VOL                 0x0008  /* DACL_MIXSPKL_VOL */
#define WM8903_DACL_MIXSPKL_VOL_MASK            0x0008  /* DACL_MIXSPKL_VOL */
#define WM8903_DACL_MIXSPKL_VOL_SHIFT                3  /* DACL_MIXSPKL_VOL */
#define WM8903_DACL_MIXSPKL_VOL_WIDTH                1  /* DACL_MIXSPKL_VOL */
#define WM8903_DACR_MIXSPKL_VOL                 0x0004  /* DACR_MIXSPKL_VOL */
#define WM8903_DACR_MIXSPKL_VOL_MASK            0x0004  /* DACR_MIXSPKL_VOL */
#define WM8903_DACR_MIXSPKL_VOL_SHIFT                2  /* DACR_MIXSPKL_VOL */
#define WM8903_DACR_MIXSPKL_VOL_WIDTH                1  /* DACR_MIXSPKL_VOL */
#define WM8903_BYPASSL_MIXSPKL_VOL              0x0002  /* BYPASSL_MIXSPKL_VOL */
#define WM8903_BYPASSL_MIXSPKL_VOL_MASK         0x0002  /* BYPASSL_MIXSPKL_VOL */
#define WM8903_BYPASSL_MIXSPKL_VOL_SHIFT             1  /* BYPASSL_MIXSPKL_VOL */
#define WM8903_BYPASSL_MIXSPKL_VOL_WIDTH             1  /* BYPASSL_MIXSPKL_VOL */
#define WM8903_BYPASSR_MIXSPKL_VOL              0x0001  /* BYPASSR_MIXSPKL_VOL */
#define WM8903_BYPASSR_MIXSPKL_VOL_MASK         0x0001  /* BYPASSR_MIXSPKL_VOL */
#define WM8903_BYPASSR_MIXSPKL_VOL_SHIFT             0  /* BYPASSR_MIXSPKL_VOL */
#define WM8903_BYPASSR_MIXSPKL_VOL_WIDTH             1  /* BYPASSR_MIXSPKL_VOL */

/*
 * R54 (0x36) - Analogue Spk Mix Right 0
 */
#define WM8903_DACL_TO_MIXSPKR                  0x0008  /* DACL_TO_MIXSPKR */
#define WM8903_DACL_TO_MIXSPKR_MASK             0x0008  /* DACL_TO_MIXSPKR */
#define WM8903_DACL_TO_MIXSPKR_SHIFT                 3  /* DACL_TO_MIXSPKR */
#define WM8903_DACL_TO_MIXSPKR_WIDTH                 1  /* DACL_TO_MIXSPKR */
#define WM8903_DACR_TO_MIXSPKR                  0x0004  /* DACR_TO_MIXSPKR */
#define WM8903_DACR_TO_MIXSPKR_MASK             0x0004  /* DACR_TO_MIXSPKR */
#define WM8903_DACR_TO_MIXSPKR_SHIFT                 2  /* DACR_TO_MIXSPKR */
#define WM8903_DACR_TO_MIXSPKR_WIDTH                 1  /* DACR_TO_MIXSPKR */
#define WM8903_BYPASSL_TO_MIXSPKR               0x0002  /* BYPASSL_TO_MIXSPKR */
#define WM8903_BYPASSL_TO_MIXSPKR_MASK          0x0002  /* BYPASSL_TO_MIXSPKR */
#define WM8903_BYPASSL_TO_MIXSPKR_SHIFT              1  /* BYPASSL_TO_MIXSPKR */
#define WM8903_BYPASSL_TO_MIXSPKR_WIDTH              1  /* BYPASSL_TO_MIXSPKR */
#define WM8903_BYPASSR_TO_MIXSPKR               0x0001  /* BYPASSR_TO_MIXSPKR */
#define WM8903_BYPASSR_TO_MIXSPKR_MASK          0x0001  /* BYPASSR_TO_MIXSPKR */
#define WM8903_BYPASSR_TO_MIXSPKR_SHIFT              0  /* BYPASSR_TO_MIXSPKR */
#define WM8903_BYPASSR_TO_MIXSPKR_WIDTH              1  /* BYPASSR_TO_MIXSPKR */

/*
 * R55 (0x37) - Analogue Spk Mix Right 1
 */
#define WM8903_DACL_MIXSPKR_VOL                 0x0008  /* DACL_MIXSPKR_VOL */
#define WM8903_DACL_MIXSPKR_VOL_MASK            0x0008  /* DACL_MIXSPKR_VOL */
#define WM8903_DACL_MIXSPKR_VOL_SHIFT                3  /* DACL_MIXSPKR_VOL */
#define WM8903_DACL_MIXSPKR_VOL_WIDTH                1  /* DACL_MIXSPKR_VOL */
#define WM8903_DACR_MIXSPKR_VOL                 0x0004  /* DACR_MIXSPKR_VOL */
#define WM8903_DACR_MIXSPKR_VOL_MASK            0x0004  /* DACR_MIXSPKR_VOL */
#define WM8903_DACR_MIXSPKR_VOL_SHIFT                2  /* DACR_MIXSPKR_VOL */
#define WM8903_DACR_MIXSPKR_VOL_WIDTH                1  /* DACR_MIXSPKR_VOL */
#define WM8903_BYPASSL_MIXSPKR_VOL              0x0002  /* BYPASSL_MIXSPKR_VOL */
#define WM8903_BYPASSL_MIXSPKR_VOL_MASK         0x0002  /* BYPASSL_MIXSPKR_VOL */
#define WM8903_BYPASSL_MIXSPKR_VOL_SHIFT             1  /* BYPASSL_MIXSPKR_VOL */
#define WM8903_BYPASSL_MIXSPKR_VOL_WIDTH             1  /* BYPASSL_MIXSPKR_VOL */
#define WM8903_BYPASSR_MIXSPKR_VOL              0x0001  /* BYPASSR_MIXSPKR_VOL */
#define WM8903_BYPASSR_MIXSPKR_VOL_MASK         0x0001  /* BYPASSR_MIXSPKR_VOL */
#define WM8903_BYPASSR_MIXSPKR_VOL_SHIFT             0  /* BYPASSR_MIXSPKR_VOL */
#define WM8903_BYPASSR_MIXSPKR_VOL_WIDTH             1  /* BYPASSR_MIXSPKR_VOL */

/*
 * R57 (0x39) - Analogue OUT1 Left
 */
#define WM8903_HPL_MUTE                         0x0100  /* HPL_MUTE */
#define WM8903_HPL_MUTE_MASK                    0x0100  /* HPL_MUTE */
#define WM8903_HPL_MUTE_SHIFT                        8  /* HPL_MUTE */
#define WM8903_HPL_MUTE_WIDTH                        1  /* HPL_MUTE */
#define WM8903_HPOUTVU                          0x0080  /* HPOUTVU */
#define WM8903_HPOUTVU_MASK                     0x0080  /* HPOUTVU */
#define WM8903_HPOUTVU_SHIFT                         7  /* HPOUTVU */
#define WM8903_HPOUTVU_WIDTH                         1  /* HPOUTVU */
#define WM8903_HPOUTLZC                         0x0040  /* HPOUTLZC */
#define WM8903_HPOUTLZC_MASK                    0x0040  /* HPOUTLZC */
#define WM8903_HPOUTLZC_SHIFT                        6  /* HPOUTLZC */
#define WM8903_HPOUTLZC_WIDTH                        1  /* HPOUTLZC */
#define WM8903_HPOUTL_VOL_MASK                  0x003F  /* HPOUTL_VOL - [5:0] */
#define WM8903_HPOUTL_VOL_SHIFT                      0  /* HPOUTL_VOL - [5:0] */
#define WM8903_HPOUTL_VOL_WIDTH                      6  /* HPOUTL_VOL - [5:0] */

/*
 * R58 (0x3A) - Analogue OUT1 Right
 */
#define WM8903_HPR_MUTE                         0x0100  /* HPR_MUTE */
#define WM8903_HPR_MUTE_MASK                    0x0100  /* HPR_MUTE */
#define WM8903_HPR_MUTE_SHIFT                        8  /* HPR_MUTE */
#define WM8903_HPR_MUTE_WIDTH                        1  /* HPR_MUTE */
#define WM8903_HPOUTVU                          0x0080  /* HPOUTVU */
#define WM8903_HPOUTVU_MASK                     0x0080  /* HPOUTVU */
#define WM8903_HPOUTVU_SHIFT                         7  /* HPOUTVU */
#define WM8903_HPOUTVU_WIDTH                         1  /* HPOUTVU */
#define WM8903_HPOUTRZC                         0x0040  /* HPOUTRZC */
#define WM8903_HPOUTRZC_MASK                    0x0040  /* HPOUTRZC */
#define WM8903_HPOUTRZC_SHIFT                        6  /* HPOUTRZC */
#define WM8903_HPOUTRZC_WIDTH                        1  /* HPOUTRZC */
#define WM8903_HPOUTR_VOL_MASK                  0x003F  /* HPOUTR_VOL - [5:0] */
#define WM8903_HPOUTR_VOL_SHIFT                      0  /* HPOUTR_VOL - [5:0] */
#define WM8903_HPOUTR_VOL_WIDTH                      6  /* HPOUTR_VOL - [5:0] */

/*
 * R59 (0x3B) - Analogue OUT2 Left
 */
#define WM8903_LINEOUTL_MUTE                    0x0100  /* LINEOUTL_MUTE */
#define WM8903_LINEOUTL_MUTE_MASK               0x0100  /* LINEOUTL_MUTE */
#define WM8903_LINEOUTL_MUTE_SHIFT                   8  /* LINEOUTL_MUTE */
#define WM8903_LINEOUTL_MUTE_WIDTH                   1  /* LINEOUTL_MUTE */
#define WM8903_LINEOUTVU                        0x0080  /* LINEOUTVU */
#define WM8903_LINEOUTVU_MASK                   0x0080  /* LINEOUTVU */
#define WM8903_LINEOUTVU_SHIFT                       7  /* LINEOUTVU */
#define WM8903_LINEOUTVU_WIDTH                       1  /* LINEOUTVU */
#define WM8903_LINEOUTLZC                       0x0040  /* LINEOUTLZC */
#define WM8903_LINEOUTLZC_MASK                  0x0040  /* LINEOUTLZC */
#define WM8903_LINEOUTLZC_SHIFT                      6  /* LINEOUTLZC */
#define WM8903_LINEOUTLZC_WIDTH                      1  /* LINEOUTLZC */
#define WM8903_LINEOUTL_VOL_MASK                0x003F  /* LINEOUTL_VOL - [5:0] */
#define WM8903_LINEOUTL_VOL_SHIFT                    0  /* LINEOUTL_VOL - [5:0] */
#define WM8903_LINEOUTL_VOL_WIDTH                    6  /* LINEOUTL_VOL - [5:0] */

/*
 * R60 (0x3C) - Analogue OUT2 Right
 */
#define WM8903_LINEOUTR_MUTE                    0x0100  /* LINEOUTR_MUTE */
#define WM8903_LINEOUTR_MUTE_MASK               0x0100  /* LINEOUTR_MUTE */
#define WM8903_LINEOUTR_MUTE_SHIFT                   8  /* LINEOUTR_MUTE */
#define WM8903_LINEOUTR_MUTE_WIDTH                   1  /* LINEOUTR_MUTE */
#define WM8903_LINEOUTVU                        0x0080  /* LINEOUTVU */
#define WM8903_LINEOUTVU_MASK                   0x0080  /* LINEOUTVU */
#define WM8903_LINEOUTVU_SHIFT                       7  /* LINEOUTVU */
#define WM8903_LINEOUTVU_WIDTH                       1  /* LINEOUTVU */
#define WM8903_LINEOUTRZC                       0x0040  /* LINEOUTRZC */
#define WM8903_LINEOUTRZC_MASK                  0x0040  /* LINEOUTRZC */
#define WM8903_LINEOUTRZC_SHIFT                      6  /* LINEOUTRZC */
#define WM8903_LINEOUTRZC_WIDTH                      1  /* LINEOUTRZC */
#define WM8903_LINEOUTR_VOL_MASK                0x003F  /* LINEOUTR_VOL - [5:0] */
#define WM8903_LINEOUTR_VOL_SHIFT                    0  /* LINEOUTR_VOL - [5:0] */
#define WM8903_LINEOUTR_VOL_WIDTH                    6  /* LINEOUTR_VOL - [5:0] */

/*
 * R62 (0x3E) - Analogue OUT3 Left
 */
#define WM8903_SPKL_MUTE                        0x0100  /* SPKL_MUTE */
#define WM8903_SPKL_MUTE_MASK                   0x0100  /* SPKL_MUTE */
#define WM8903_SPKL_MUTE_SHIFT                       8  /* SPKL_MUTE */
#define WM8903_SPKL_MUTE_WIDTH                       1  /* SPKL_MUTE */
#define WM8903_SPKVU                            0x0080  /* SPKVU */
#define WM8903_SPKVU_MASK                       0x0080  /* SPKVU */
#define WM8903_SPKVU_SHIFT                           7  /* SPKVU */
#define WM8903_SPKVU_WIDTH                           1  /* SPKVU */
#define WM8903_SPKLZC                           0x0040  /* SPKLZC */
#define WM8903_SPKLZC_MASK                      0x0040  /* SPKLZC */
#define WM8903_SPKLZC_SHIFT                          6  /* SPKLZC */
#define WM8903_SPKLZC_WIDTH                          1  /* SPKLZC */
#define WM8903_SPKL_VOL_MASK                    0x003F  /* SPKL_VOL - [5:0] */
#define WM8903_SPKL_VOL_SHIFT                        0  /* SPKL_VOL - [5:0] */
#define WM8903_SPKL_VOL_WIDTH                        6  /* SPKL_VOL - [5:0] */

/*
 * R63 (0x3F) - Analogue OUT3 Right
 */
#define WM8903_SPKR_MUTE                        0x0100  /* SPKR_MUTE */
#define WM8903_SPKR_MUTE_MASK                   0x0100  /* SPKR_MUTE */
#define WM8903_SPKR_MUTE_SHIFT                       8  /* SPKR_MUTE */
#define WM8903_SPKR_MUTE_WIDTH                       1  /* SPKR_MUTE */
#define WM8903_SPKVU                            0x0080  /* SPKVU */
#define WM8903_SPKVU_MASK                       0x0080  /* SPKVU */
#define WM8903_SPKVU_SHIFT                           7  /* SPKVU */
#define WM8903_SPKVU_WIDTH                           1  /* SPKVU */
#define WM8903_SPKRZC                           0x0040  /* SPKRZC */
#define WM8903_SPKRZC_MASK                      0x0040  /* SPKRZC */
#define WM8903_SPKRZC_SHIFT                          6  /* SPKRZC */
#define WM8903_SPKRZC_WIDTH                          1  /* SPKRZC */
#define WM8903_SPKR_VOL_MASK                    0x003F  /* SPKR_VOL - [5:0] */
#define WM8903_SPKR_VOL_SHIFT                        0  /* SPKR_VOL - [5:0] */
#define WM8903_SPKR_VOL_WIDTH                        6  /* SPKR_VOL - [5:0] */

/*
 * R65 (0x41) - Analogue SPK Output Control 0
 */
#define WM8903_SPK_DISCHARGE                    0x0002  /* SPK_DISCHARGE */
#define WM8903_SPK_DISCHARGE_MASK               0x0002  /* SPK_DISCHARGE */
#define WM8903_SPK_DISCHARGE_SHIFT                   1  /* SPK_DISCHARGE */
#define WM8903_SPK_DISCHARGE_WIDTH                   1  /* SPK_DISCHARGE */
#define WM8903_VROI                             0x0001  /* VROI */
#define WM8903_VROI_MASK                        0x0001  /* VROI */
#define WM8903_VROI_SHIFT                            0  /* VROI */
#define WM8903_VROI_WIDTH                            1  /* VROI */

/*
 * R67 (0x43) - DC Servo 0
 */
#define WM8903_DCS_MASTER_ENA                   0x0010  /* DCS_MASTER_ENA */
#define WM8903_DCS_MASTER_ENA_MASK              0x0010  /* DCS_MASTER_ENA */
#define WM8903_DCS_MASTER_ENA_SHIFT                  4  /* DCS_MASTER_ENA */
#define WM8903_DCS_MASTER_ENA_WIDTH                  1  /* DCS_MASTER_ENA */
#define WM8903_DCS_ENA_MASK                     0x000F  /* DCS_ENA - [3:0] */
#define WM8903_DCS_ENA_SHIFT                         0  /* DCS_ENA - [3:0] */
#define WM8903_DCS_ENA_WIDTH                         4  /* DCS_ENA - [3:0] */

/*
 * R69 (0x45) - DC Servo 2
 */
#define WM8903_DCS_MODE_MASK                    0x0003  /* DCS_MODE - [1:0] */
#define WM8903_DCS_MODE_SHIFT                        0  /* DCS_MODE - [1:0] */
#define WM8903_DCS_MODE_WIDTH                        2  /* DCS_MODE - [1:0] */

/*
 * R90 (0x5A) - Analogue HP 0
 */
#define WM8903_HPL_RMV_SHORT                    0x0080  /* HPL_RMV_SHORT */
#define WM8903_HPL_RMV_SHORT_MASK               0x0080  /* HPL_RMV_SHORT */
#define WM8903_HPL_RMV_SHORT_SHIFT                   7  /* HPL_RMV_SHORT */
#define WM8903_HPL_RMV_SHORT_WIDTH                   1  /* HPL_RMV_SHORT */
#define WM8903_HPL_ENA_OUTP                     0x0040  /* HPL_ENA_OUTP */
#define WM8903_HPL_ENA_OUTP_MASK                0x0040  /* HPL_ENA_OUTP */
#define WM8903_HPL_ENA_OUTP_SHIFT                    6  /* HPL_ENA_OUTP */
#define WM8903_HPL_ENA_OUTP_WIDTH                    1  /* HPL_ENA_OUTP */
#define WM8903_HPL_ENA_DLY                      0x0020  /* HPL_ENA_DLY */
#define WM8903_HPL_ENA_DLY_MASK                 0x0020  /* HPL_ENA_DLY */
#define WM8903_HPL_ENA_DLY_SHIFT                     5  /* HPL_ENA_DLY */
#define WM8903_HPL_ENA_DLY_WIDTH                     1  /* HPL_ENA_DLY */
#define WM8903_HPL_ENA                          0x0010  /* HPL_ENA */
#define WM8903_HPL_ENA_MASK                     0x0010  /* HPL_ENA */
#define WM8903_HPL_ENA_SHIFT                         4  /* HPL_ENA */
#define WM8903_HPL_ENA_WIDTH                         1  /* HPL_ENA */
#define WM8903_HPR_RMV_SHORT                    0x0008  /* HPR_RMV_SHORT */
#define WM8903_HPR_RMV_SHORT_MASK               0x0008  /* HPR_RMV_SHORT */
#define WM8903_HPR_RMV_SHORT_SHIFT                   3  /* HPR_RMV_SHORT */
#define WM8903_HPR_RMV_SHORT_WIDTH                   1  /* HPR_RMV_SHORT */
#define WM8903_HPR_ENA_OUTP                     0x0004  /* HPR_ENA_OUTP */
#define WM8903_HPR_ENA_OUTP_MASK                0x0004  /* HPR_ENA_OUTP */
#define WM8903_HPR_ENA_OUTP_SHIFT                    2  /* HPR_ENA_OUTP */
#define WM8903_HPR_ENA_OUTP_WIDTH                    1  /* HPR_ENA_OUTP */
#define WM8903_HPR_ENA_DLY                      0x0002  /* HPR_ENA_DLY */
#define WM8903_HPR_ENA_DLY_MASK                 0x0002  /* HPR_ENA_DLY */
#define WM8903_HPR_ENA_DLY_SHIFT                     1  /* HPR_ENA_DLY */
#define WM8903_HPR_ENA_DLY_WIDTH                     1  /* HPR_ENA_DLY */
#define WM8903_HPR_ENA                          0x0001  /* HPR_ENA */
#define WM8903_HPR_ENA_MASK                     0x0001  /* HPR_ENA */
#define WM8903_HPR_ENA_SHIFT                         0  /* HPR_ENA */
#define WM8903_HPR_ENA_WIDTH                         1  /* HPR_ENA */

/*
 * R94 (0x5E) - Analogue Lineout 0
 */
#define WM8903_LINEOUTL_RMV_SHORT               0x0080  /* LINEOUTL_RMV_SHORT */
#define WM8903_LINEOUTL_RMV_SHORT_MASK          0x0080  /* LINEOUTL_RMV_SHORT */
#define WM8903_LINEOUTL_RMV_SHORT_SHIFT              7  /* LINEOUTL_RMV_SHORT */
#define WM8903_LINEOUTL_RMV_SHORT_WIDTH              1  /* LINEOUTL_RMV_SHORT */
#define WM8903_LINEOUTL_ENA_OUTP                0x0040  /* LINEOUTL_ENA_OUTP */
#define WM8903_LINEOUTL_ENA_OUTP_MASK           0x0040  /* LINEOUTL_ENA_OUTP */
#define WM8903_LINEOUTL_ENA_OUTP_SHIFT               6  /* LINEOUTL_ENA_OUTP */
#define WM8903_LINEOUTL_ENA_OUTP_WIDTH               1  /* LINEOUTL_ENA_OUTP */
#define WM8903_LINEOUTL_ENA_DLY                 0x0020  /* LINEOUTL_ENA_DLY */
#define WM8903_LINEOUTL_ENA_DLY_MASK            0x0020  /* LINEOUTL_ENA_DLY */
#define WM8903_LINEOUTL_ENA_DLY_SHIFT                5  /* LINEOUTL_ENA_DLY */
#define WM8903_LINEOUTL_ENA_DLY_WIDTH                1  /* LINEOUTL_ENA_DLY */
#define WM8903_LINEOUTL_ENA                     0x0010  /* LINEOUTL_ENA */
#define WM8903_LINEOUTL_ENA_MASK                0x0010  /* LINEOUTL_ENA */
#define WM8903_LINEOUTL_ENA_SHIFT                    4  /* LINEOUTL_ENA */
#define WM8903_LINEOUTL_ENA_WIDTH                    1  /* LINEOUTL_ENA */
#define WM8903_LINEOUTR_RMV_SHORT               0x0008  /* LINEOUTR_RMV_SHORT */
#define WM8903_LINEOUTR_RMV_SHORT_MASK          0x0008  /* LINEOUTR_RMV_SHORT */
#define WM8903_LINEOUTR_RMV_SHORT_SHIFT              3  /* LINEOUTR_RMV_SHORT */
#define WM8903_LINEOUTR_RMV_SHORT_WIDTH              1  /* LINEOUTR_RMV_SHORT */
#define WM8903_LINEOUTR_ENA_OUTP                0x0004  /* LINEOUTR_ENA_OUTP */
#define WM8903_LINEOUTR_ENA_OUTP_MASK           0x0004  /* LINEOUTR_ENA_OUTP */
#define WM8903_LINEOUTR_ENA_OUTP_SHIFT               2  /* LINEOUTR_ENA_OUTP */
#define WM8903_LINEOUTR_ENA_OUTP_WIDTH               1  /* LINEOUTR_ENA_OUTP */
#define WM8903_LINEOUTR_ENA_DLY                 0x0002  /* LINEOUTR_ENA_DLY */
#define WM8903_LINEOUTR_ENA_DLY_MASK            0x0002  /* LINEOUTR_ENA_DLY */
#define WM8903_LINEOUTR_ENA_DLY_SHIFT                1  /* LINEOUTR_ENA_DLY */
#define WM8903_LINEOUTR_ENA_DLY_WIDTH                1  /* LINEOUTR_ENA_DLY */
#define WM8903_LINEOUTR_ENA                     0x0001  /* LINEOUTR_ENA */
#define WM8903_LINEOUTR_ENA_MASK                0x0001  /* LINEOUTR_ENA */
#define WM8903_LINEOUTR_ENA_SHIFT                    0  /* LINEOUTR_ENA */
#define WM8903_LINEOUTR_ENA_WIDTH                    1  /* LINEOUTR_ENA */

/*
 * R98 (0x62) - Charge Pump 0
 */
#define WM8903_CP_ENA                           0x0001  /* CP_ENA */
#define WM8903_CP_ENA_MASK                      0x0001  /* CP_ENA */
#define WM8903_CP_ENA_SHIFT                          0  /* CP_ENA */
#define WM8903_CP_ENA_WIDTH                          1  /* CP_ENA */

/*
 * R104 (0x68) - Class W 0
 */
#define WM8903_CP_DYN_FREQ                      0x0002  /* CP_DYN_FREQ */
#define WM8903_CP_DYN_FREQ_MASK                 0x0002  /* CP_DYN_FREQ */
#define WM8903_CP_DYN_FREQ_SHIFT                     1  /* CP_DYN_FREQ */
#define WM8903_CP_DYN_FREQ_WIDTH                     1  /* CP_DYN_FREQ */
#define WM8903_CP_DYN_V                         0x0001  /* CP_DYN_V */
#define WM8903_CP_DYN_V_MASK                    0x0001  /* CP_DYN_V */
#define WM8903_CP_DYN_V_SHIFT                        0  /* CP_DYN_V */
#define WM8903_CP_DYN_V_WIDTH                        1  /* CP_DYN_V */

/*
 * R108 (0x6C) - Write Sequencer 0
 */
#define WM8903_WSEQ_ENA                         0x0100  /* WSEQ_ENA */
#define WM8903_WSEQ_ENA_MASK                    0x0100  /* WSEQ_ENA */
#define WM8903_WSEQ_ENA_SHIFT                        8  /* WSEQ_ENA */
#define WM8903_WSEQ_ENA_WIDTH                        1  /* WSEQ_ENA */
#define WM8903_WSEQ_WRITE_INDEX_MASK            0x001F  /* WSEQ_WRITE_INDEX - [4:0] */
#define WM8903_WSEQ_WRITE_INDEX_SHIFT                0  /* WSEQ_WRITE_INDEX - [4:0] */
#define WM8903_WSEQ_WRITE_INDEX_WIDTH                5  /* WSEQ_WRITE_INDEX - [4:0] */

/*
 * R109 (0x6D) - Write Sequencer 1
 */
#define WM8903_WSEQ_DATA_WIDTH_MASK             0x7000  /* WSEQ_DATA_WIDTH - [14:12] */
#define WM8903_WSEQ_DATA_WIDTH_SHIFT                12  /* WSEQ_DATA_WIDTH - [14:12] */
#define WM8903_WSEQ_DATA_WIDTH_WIDTH                 3  /* WSEQ_DATA_WIDTH - [14:12] */
#define WM8903_WSEQ_DATA_START_MASK             0x0F00  /* WSEQ_DATA_START - [11:8] */
#define WM8903_WSEQ_DATA_START_SHIFT                 8  /* WSEQ_DATA_START - [11:8] */
#define WM8903_WSEQ_DATA_START_WIDTH                 4  /* WSEQ_DATA_START - [11:8] */
#define WM8903_WSEQ_ADDR_MASK                   0x00FF  /* WSEQ_ADDR - [7:0] */
#define WM8903_WSEQ_ADDR_SHIFT                       0  /* WSEQ_ADDR - [7:0] */
#define WM8903_WSEQ_ADDR_WIDTH                       8  /* WSEQ_ADDR - [7:0] */

/*
 * R110 (0x6E) - Write Sequencer 2
 */
#define WM8903_WSEQ_EOS                         0x4000  /* WSEQ_EOS */
#define WM8903_WSEQ_EOS_MASK                    0x4000  /* WSEQ_EOS */
#define WM8903_WSEQ_EOS_SHIFT                       14  /* WSEQ_EOS */
#define WM8903_WSEQ_EOS_WIDTH                        1  /* WSEQ_EOS */
#define WM8903_WSEQ_DELAY_MASK                  0x0F00  /* WSEQ_DELAY - [11:8] */
#define WM8903_WSEQ_DELAY_SHIFT                      8  /* WSEQ_DELAY - [11:8] */
#define WM8903_WSEQ_DELAY_WIDTH                      4  /* WSEQ_DELAY - [11:8] */
#define WM8903_WSEQ_DATA_MASK                   0x00FF  /* WSEQ_DATA - [7:0] */
#define WM8903_WSEQ_DATA_SHIFT                       0  /* WSEQ_DATA - [7:0] */
#define WM8903_WSEQ_DATA_WIDTH                       8  /* WSEQ_DATA - [7:0] */

/*
 * R111 (0x6F) - Write Sequencer 3
 */
#define WM8903_WSEQ_ABORT                       0x0200  /* WSEQ_ABORT */
#define WM8903_WSEQ_ABORT_MASK                  0x0200  /* WSEQ_ABORT */
#define WM8903_WSEQ_ABORT_SHIFT                      9  /* WSEQ_ABORT */
#define WM8903_WSEQ_ABORT_WIDTH                      1  /* WSEQ_ABORT */
#define WM8903_WSEQ_START                       0x0100  /* WSEQ_START */
#define WM8903_WSEQ_START_MASK                  0x0100  /* WSEQ_START */
#define WM8903_WSEQ_START_SHIFT                      8  /* WSEQ_START */
#define WM8903_WSEQ_START_WIDTH                      1  /* WSEQ_START */
#define WM8903_WSEQ_START_INDEX_MASK            0x003F  /* WSEQ_START_INDEX - [5:0] */
#define WM8903_WSEQ_START_INDEX_SHIFT                0  /* WSEQ_START_INDEX - [5:0] */
#define WM8903_WSEQ_START_INDEX_WIDTH                6  /* WSEQ_START_INDEX - [5:0] */

/*
 * R112 (0x70) - Write Sequencer 4
 */
#define WM8903_WSEQ_CURRENT_INDEX_MASK          0x03F0  /* WSEQ_CURRENT_INDEX - [9:4] */
#define WM8903_WSEQ_CURRENT_INDEX_SHIFT              4  /* WSEQ_CURRENT_INDEX - [9:4] */
#define WM8903_WSEQ_CURRENT_INDEX_WIDTH              6  /* WSEQ_CURRENT_INDEX - [9:4] */
#define WM8903_WSEQ_BUSY                        0x0001  /* WSEQ_BUSY */
#define WM8903_WSEQ_BUSY_MASK                   0x0001  /* WSEQ_BUSY */
#define WM8903_WSEQ_BUSY_SHIFT                       0  /* WSEQ_BUSY */
#define WM8903_WSEQ_BUSY_WIDTH                       1  /* WSEQ_BUSY */

/*
 * R114 (0x72) - Control Interface
 */
#define WM8903_MASK_WRITE_ENA                   0x0001  /* MASK_WRITE_ENA */
#define WM8903_MASK_WRITE_ENA_MASK              0x0001  /* MASK_WRITE_ENA */
#define WM8903_MASK_WRITE_ENA_SHIFT                  0  /* MASK_WRITE_ENA */
#define WM8903_MASK_WRITE_ENA_WIDTH                  1  /* MASK_WRITE_ENA */

/*
 * R116 (0x74) - GPIO Control 1
 */
#define WM8903_GP1_FN_MASK                      0x1F00  /* GP1_FN - [12:8] */
#define WM8903_GP1_FN_SHIFT                          8  /* GP1_FN - [12:8] */
#define WM8903_GP1_FN_WIDTH                          5  /* GP1_FN - [12:8] */
#define WM8903_GP1_DIR                          0x0080  /* GP1_DIR */
#define WM8903_GP1_DIR_MASK                     0x0080  /* GP1_DIR */
#define WM8903_GP1_DIR_SHIFT                         7  /* GP1_DIR */
#define WM8903_GP1_DIR_WIDTH                         1  /* GP1_DIR */
#define WM8903_GP1_OP_CFG                       0x0040  /* GP1_OP_CFG */
#define WM8903_GP1_OP_CFG_MASK                  0x0040  /* GP1_OP_CFG */
#define WM8903_GP1_OP_CFG_SHIFT                      6  /* GP1_OP_CFG */
#define WM8903_GP1_OP_CFG_WIDTH                      1  /* GP1_OP_CFG */
#define WM8903_GP1_IP_CFG                       0x0020  /* GP1_IP_CFG */
#define WM8903_GP1_IP_CFG_MASK                  0x0020  /* GP1_IP_CFG */
#define WM8903_GP1_IP_CFG_SHIFT                      5  /* GP1_IP_CFG */
#define WM8903_GP1_IP_CFG_WIDTH                      1  /* GP1_IP_CFG */
#define WM8903_GP1_LVL                          0x0010  /* GP1_LVL */
#define WM8903_GP1_LVL_MASK                     0x0010  /* GP1_LVL */
#define WM8903_GP1_LVL_SHIFT                         4  /* GP1_LVL */
#define WM8903_GP1_LVL_WIDTH                         1  /* GP1_LVL */
#define WM8903_GP1_PD                           0x0008  /* GP1_PD */
#define WM8903_GP1_PD_MASK                      0x0008  /* GP1_PD */
#define WM8903_GP1_PD_SHIFT                          3  /* GP1_PD */
#define WM8903_GP1_PD_WIDTH                          1  /* GP1_PD */
#define WM8903_GP1_PU                           0x0004  /* GP1_PU */
#define WM8903_GP1_PU_MASK                      0x0004  /* GP1_PU */
#define WM8903_GP1_PU_SHIFT                          2  /* GP1_PU */
#define WM8903_GP1_PU_WIDTH                          1  /* GP1_PU */
#define WM8903_GP1_INTMODE                      0x0002  /* GP1_INTMODE */
#define WM8903_GP1_INTMODE_MASK                 0x0002  /* GP1_INTMODE */
#define WM8903_GP1_INTMODE_SHIFT                     1  /* GP1_INTMODE */
#define WM8903_GP1_INTMODE_WIDTH                     1  /* GP1_INTMODE */
#define WM8903_GP1_DB                           0x0001  /* GP1_DB */
#define WM8903_GP1_DB_MASK                      0x0001  /* GP1_DB */
#define WM8903_GP1_DB_SHIFT                          0  /* GP1_DB */
#define WM8903_GP1_DB_WIDTH                          1  /* GP1_DB */

/*
 * R117 (0x75) - GPIO Control 2
 */
#define WM8903_GP2_FN_MASK                      0x1F00  /* GP2_FN - [12:8] */
#define WM8903_GP2_FN_SHIFT                          8  /* GP2_FN - [12:8] */
#define WM8903_GP2_FN_WIDTH                          5  /* GP2_FN - [12:8] */
#define WM8903_GP2_DIR                          0x0080  /* GP2_DIR */
#define WM8903_GP2_DIR_MASK                     0x0080  /* GP2_DIR */
#define WM8903_GP2_DIR_SHIFT                         7  /* GP2_DIR */
#define WM8903_GP2_DIR_WIDTH                         1  /* GP2_DIR */
#define WM8903_GP2_OP_CFG                       0x0040  /* GP2_OP_CFG */
#define WM8903_GP2_OP_CFG_MASK                  0x0040  /* GP2_OP_CFG */
#define WM8903_GP2_OP_CFG_SHIFT                      6  /* GP2_OP_CFG */
#define WM8903_GP2_OP_CFG_WIDTH                      1  /* GP2_OP_CFG */
#define WM8903_GP2_IP_CFG                       0x0020  /* GP2_IP_CFG */
#define WM8903_GP2_IP_CFG_MASK                  0x0020  /* GP2_IP_CFG */
#define WM8903_GP2_IP_CFG_SHIFT                      5  /* GP2_IP_CFG */
#define WM8903_GP2_IP_CFG_WIDTH                      1  /* GP2_IP_CFG */
#define WM8903_GP2_LVL                          0x0010  /* GP2_LVL */
#define WM8903_GP2_LVL_MASK                     0x0010  /* GP2_LVL */
#define WM8903_GP2_LVL_SHIFT                         4  /* GP2_LVL */
#define WM8903_GP2_LVL_WIDTH                         1  /* GP2_LVL */
#define WM8903_GP2_PD                           0x0008  /* GP2_PD */
#define WM8903_GP2_PD_MASK                      0x0008  /* GP2_PD */
#define WM8903_GP2_PD_SHIFT                          3  /* GP2_PD */
#define WM8903_GP2_PD_WIDTH                          1  /* GP2_PD */
#define WM8903_GP2_PU                           0x0004  /* GP2_PU */
#define WM8903_GP2_PU_MASK                      0x0004  /* GP2_PU */
#define WM8903_GP2_PU_SHIFT                          2  /* GP2_PU */
#define WM8903_GP2_PU_WIDTH                          1  /* GP2_PU */
#define WM8903_GP2_INTMODE                      0x0002  /* GP2_INTMODE */
#define WM8903_GP2_INTMODE_MASK                 0x0002  /* GP2_INTMODE */
#define WM8903_GP2_INTMODE_SHIFT                     1  /* GP2_INTMODE */
#define WM8903_GP2_INTMODE_WIDTH                     1  /* GP2_INTMODE */
#define WM8903_GP2_DB                           0x0001  /* GP2_DB */
#define WM8903_GP2_DB_MASK                      0x0001  /* GP2_DB */
#define WM8903_GP2_DB_SHIFT                          0  /* GP2_DB */
#define WM8903_GP2_DB_WIDTH                          1  /* GP2_DB */

/*
 * R118 (0x76) - GPIO Control 3
 */
#define WM8903_GP3_FN_MASK                      0x1F00  /* GP3_FN - [12:8] */
#define WM8903_GP3_FN_SHIFT                          8  /* GP3_FN - [12:8] */
#define WM8903_GP3_FN_WIDTH                          5  /* GP3_FN - [12:8] */
#define WM8903_GP3_DIR                          0x0080  /* GP3_DIR */
#define WM8903_GP3_DIR_MASK                     0x0080  /* GP3_DIR */
#define WM8903_GP3_DIR_SHIFT                         7  /* GP3_DIR */
#define WM8903_GP3_DIR_WIDTH                         1  /* GP3_DIR */
#define WM8903_GP3_OP_CFG                       0x0040  /* GP3_OP_CFG */
#define WM8903_GP3_OP_CFG_MASK                  0x0040  /* GP3_OP_CFG */
#define WM8903_GP3_OP_CFG_SHIFT                      6  /* GP3_OP_CFG */
#define WM8903_GP3_OP_CFG_WIDTH                      1  /* GP3_OP_CFG */
#define WM8903_GP3_IP_CFG                       0x0020  /* GP3_IP_CFG */
#define WM8903_GP3_IP_CFG_MASK                  0x0020  /* GP3_IP_CFG */
#define WM8903_GP3_IP_CFG_SHIFT                      5  /* GP3_IP_CFG */
#define WM8903_GP3_IP_CFG_WIDTH                      1  /* GP3_IP_CFG */
#define WM8903_GP3_LVL                          0x0010  /* GP3_LVL */
#define WM8903_GP3_LVL_MASK                     0x0010  /* GP3_LVL */
#define WM8903_GP3_LVL_SHIFT                         4  /* GP3_LVL */
#define WM8903_GP3_LVL_WIDTH                         1  /* GP3_LVL */
#define WM8903_GP3_PD                           0x0008  /* GP3_PD */
#define WM8903_GP3_PD_MASK                      0x0008  /* GP3_PD */
#define WM8903_GP3_PD_SHIFT                          3  /* GP3_PD */
#define WM8903_GP3_PD_WIDTH                          1  /* GP3_PD */
#define WM8903_GP3_PU                           0x0004  /* GP3_PU */
#define WM8903_GP3_PU_MASK                      0x0004  /* GP3_PU */
#define WM8903_GP3_PU_SHIFT                          2  /* GP3_PU */
#define WM8903_GP3_PU_WIDTH                          1  /* GP3_PU */
#define WM8903_GP3_INTMODE                      0x0002  /* GP3_INTMODE */
#define WM8903_GP3_INTMODE_MASK                 0x0002  /* GP3_INTMODE */
#define WM8903_GP3_INTMODE_SHIFT                     1  /* GP3_INTMODE */
#define WM8903_GP3_INTMODE_WIDTH                     1  /* GP3_INTMODE */
#define WM8903_GP3_DB                           0x0001  /* GP3_DB */
#define WM8903_GP3_DB_MASK                      0x0001  /* GP3_DB */
#define WM8903_GP3_DB_SHIFT                          0  /* GP3_DB */
#define WM8903_GP3_DB_WIDTH                          1  /* GP3_DB */

/*
 * R119 (0x77) - GPIO Control 4
 */
#define WM8903_GP4_FN_MASK                      0x1F00  /* GP4_FN - [12:8] */
#define WM8903_GP4_FN_SHIFT                          8  /* GP4_FN - [12:8] */
#define WM8903_GP4_FN_WIDTH                          5  /* GP4_FN - [12:8] */
#define WM8903_GP4_DIR                          0x0080  /* GP4_DIR */
#define WM8903_GP4_DIR_MASK                     0x0080  /* GP4_DIR */
#define WM8903_GP4_DIR_SHIFT                         7  /* GP4_DIR */
#define WM8903_GP4_DIR_WIDTH                         1  /* GP4_DIR */
#define WM8903_GP4_OP_CFG                       0x0040  /* GP4_OP_CFG */
#define WM8903_GP4_OP_CFG_MASK                  0x0040  /* GP4_OP_CFG */
#define WM8903_GP4_OP_CFG_SHIFT                      6  /* GP4_OP_CFG */
#define WM8903_GP4_OP_CFG_WIDTH                      1  /* GP4_OP_CFG */
#define WM8903_GP4_IP_CFG                       0x0020  /* GP4_IP_CFG */
#define WM8903_GP4_IP_CFG_MASK                  0x0020  /* GP4_IP_CFG */
#define WM8903_GP4_IP_CFG_SHIFT                      5  /* GP4_IP_CFG */
#define WM8903_GP4_IP_CFG_WIDTH                      1  /* GP4_IP_CFG */
#define WM8903_GP4_LVL                          0x0010  /* GP4_LVL */
#define WM8903_GP4_LVL_MASK                     0x0010  /* GP4_LVL */
#define WM8903_GP4_LVL_SHIFT                         4  /* GP4_LVL */
#define WM8903_GP4_LVL_WIDTH                         1  /* GP4_LVL */
#define WM8903_GP4_PD                           0x0008  /* GP4_PD */
#define WM8903_GP4_PD_MASK                      0x0008  /* GP4_PD */
#define WM8903_GP4_PD_SHIFT                          3  /* GP4_PD */
#define WM8903_GP4_PD_WIDTH                          1  /* GP4_PD */
#define WM8903_GP4_PU                           0x0004  /* GP4_PU */
#define WM8903_GP4_PU_MASK                      0x0004  /* GP4_PU */
#define WM8903_GP4_PU_SHIFT                          2  /* GP4_PU */
#define WM8903_GP4_PU_WIDTH                          1  /* GP4_PU */
#define WM8903_GP4_INTMODE                      0x0002  /* GP4_INTMODE */
#define WM8903_GP4_INTMODE_MASK                 0x0002  /* GP4_INTMODE */
#define WM8903_GP4_INTMODE_SHIFT                     1  /* GP4_INTMODE */
#define WM8903_GP4_INTMODE_WIDTH                     1  /* GP4_INTMODE */
#define WM8903_GP4_DB                           0x0001  /* GP4_DB */
#define WM8903_GP4_DB_MASK                      0x0001  /* GP4_DB */
#define WM8903_GP4_DB_SHIFT                          0  /* GP4_DB */
#define WM8903_GP4_DB_WIDTH                          1  /* GP4_DB */

/*
 * R120 (0x78) - GPIO Control 5
 */
#define WM8903_GP5_FN_MASK                      0x1F00  /* GP5_FN - [12:8] */
#define WM8903_GP5_FN_SHIFT                          8  /* GP5_FN - [12:8] */
#define WM8903_GP5_FN_WIDTH                          5  /* GP5_FN - [12:8] */
#define WM8903_GP5_DIR                          0x0080  /* GP5_DIR */
#define WM8903_GP5_DIR_MASK                     0x0080  /* GP5_DIR */
#define WM8903_GP5_DIR_SHIFT                         7  /* GP5_DIR */
#define WM8903_GP5_DIR_WIDTH                         1  /* GP5_DIR */
#define WM8903_GP5_OP_CFG                       0x0040  /* GP5_OP_CFG */
#define WM8903_GP5_OP_CFG_MASK                  0x0040  /* GP5_OP_CFG */
#define WM8903_GP5_OP_CFG_SHIFT                      6  /* GP5_OP_CFG */
#define WM8903_GP5_OP_CFG_WIDTH                      1  /* GP5_OP_CFG */
#define WM8903_GP5_IP_CFG                       0x0020  /* GP5_IP_CFG */
#define WM8903_GP5_IP_CFG_MASK                  0x0020  /* GP5_IP_CFG */
#define WM8903_GP5_IP_CFG_SHIFT                      5  /* GP5_IP_CFG */
#define WM8903_GP5_IP_CFG_WIDTH                      1  /* GP5_IP_CFG */
#define WM8903_GP5_LVL                          0x0010  /* GP5_LVL */
#define WM8903_GP5_LVL_MASK                     0x0010  /* GP5_LVL */
#define WM8903_GP5_LVL_SHIFT                         4  /* GP5_LVL */
#define WM8903_GP5_LVL_WIDTH                         1  /* GP5_LVL */
#define WM8903_GP5_PD                           0x0008  /* GP5_PD */
#define WM8903_GP5_PD_MASK                      0x0008  /* GP5_PD */
#define WM8903_GP5_PD_SHIFT                          3  /* GP5_PD */
#define WM8903_GP5_PD_WIDTH                          1  /* GP5_PD */
#define WM8903_GP5_PU                           0x0004  /* GP5_PU */
#define WM8903_GP5_PU_MASK                      0x0004  /* GP5_PU */
#define WM8903_GP5_PU_SHIFT                          2  /* GP5_PU */
#define WM8903_GP5_PU_WIDTH                          1  /* GP5_PU */
#define WM8903_GP5_INTMODE                      0x0002  /* GP5_INTMODE */
#define WM8903_GP5_INTMODE_MASK                 0x0002  /* GP5_INTMODE */
#define WM8903_GP5_INTMODE_SHIFT                     1  /* GP5_INTMODE */
#define WM8903_GP5_INTMODE_WIDTH                     1  /* GP5_INTMODE */
#define WM8903_GP5_DB                           0x0001  /* GP5_DB */
#define WM8903_GP5_DB_MASK                      0x0001  /* GP5_DB */
#define WM8903_GP5_DB_SHIFT                          0  /* GP5_DB */
#define WM8903_GP5_DB_WIDTH                          1  /* GP5_DB */

/*
 * R121 (0x79) - Interrupt Status 1
 */
#define WM8903_MICSHRT_EINT                     0x8000  /* MICSHRT_EINT */
#define WM8903_MICSHRT_EINT_MASK                0x8000  /* MICSHRT_EINT */
#define WM8903_MICSHRT_EINT_SHIFT                   15  /* MICSHRT_EINT */
#define WM8903_MICSHRT_EINT_WIDTH                    1  /* MICSHRT_EINT */
#define WM8903_MICDET_EINT                      0x4000  /* MICDET_EINT */
#define WM8903_MICDET_EINT_MASK                 0x4000  /* MICDET_EINT */
#define WM8903_MICDET_EINT_SHIFT                    14  /* MICDET_EINT */
#define WM8903_MICDET_EINT_WIDTH                     1  /* MICDET_EINT */
#define WM8903_WSEQ_BUSY_EINT                   0x2000  /* WSEQ_BUSY_EINT */
#define WM8903_WSEQ_BUSY_EINT_MASK              0x2000  /* WSEQ_BUSY_EINT */
#define WM8903_WSEQ_BUSY_EINT_SHIFT                 13  /* WSEQ_BUSY_EINT */
#define WM8903_WSEQ_BUSY_EINT_WIDTH                  1  /* WSEQ_BUSY_EINT */
#define WM8903_GP5_EINT                         0x0010  /* GP5_EINT */
#define WM8903_GP5_EINT_MASK                    0x0010  /* GP5_EINT */
#define WM8903_GP5_EINT_SHIFT                        4  /* GP5_EINT */
#define WM8903_GP5_EINT_WIDTH                        1  /* GP5_EINT */
#define WM8903_GP4_EINT                         0x0008  /* GP4_EINT */
#define WM8903_GP4_EINT_MASK                    0x0008  /* GP4_EINT */
#define WM8903_GP4_EINT_SHIFT                        3  /* GP4_EINT */
#define WM8903_GP4_EINT_WIDTH                        1  /* GP4_EINT */
#define WM8903_GP3_EINT                         0x0004  /* GP3_EINT */
#define WM8903_GP3_EINT_MASK                    0x0004  /* GP3_EINT */
#define WM8903_GP3_EINT_SHIFT                        2  /* GP3_EINT */
#define WM8903_GP3_EINT_WIDTH                        1  /* GP3_EINT */
#define WM8903_GP2_EINT                         0x0002  /* GP2_EINT */
#define WM8903_GP2_EINT_MASK                    0x0002  /* GP2_EINT */
#define WM8903_GP2_EINT_SHIFT                        1  /* GP2_EINT */
#define WM8903_GP2_EINT_WIDTH                        1  /* GP2_EINT */
#define WM8903_GP1_EINT                         0x0001  /* GP1_EINT */
#define WM8903_GP1_EINT_MASK                    0x0001  /* GP1_EINT */
#define WM8903_GP1_EINT_SHIFT                        0  /* GP1_EINT */
#define WM8903_GP1_EINT_WIDTH                        1  /* GP1_EINT */

/*
 * R122 (0x7A) - Interrupt Status 1 Mask
 */
#define WM8903_IM_MICSHRT_EINT                  0x8000  /* IM_MICSHRT_EINT */
#define WM8903_IM_MICSHRT_EINT_MASK             0x8000  /* IM_MICSHRT_EINT */
#define WM8903_IM_MICSHRT_EINT_SHIFT                15  /* IM_MICSHRT_EINT */
#define WM8903_IM_MICSHRT_EINT_WIDTH                 1  /* IM_MICSHRT_EINT */
#define WM8903_IM_MICDET_EINT                   0x4000  /* IM_MICDET_EINT */
#define WM8903_IM_MICDET_EINT_MASK              0x4000  /* IM_MICDET_EINT */
#define WM8903_IM_MICDET_EINT_SHIFT                 14  /* IM_MICDET_EINT */
#define WM8903_IM_MICDET_EINT_WIDTH                  1  /* IM_MICDET_EINT */
#define WM8903_IM_WSEQ_BUSY_EINT                0x2000  /* IM_WSEQ_BUSY_EINT */
#define WM8903_IM_WSEQ_BUSY_EINT_MASK           0x2000  /* IM_WSEQ_BUSY_EINT */
#define WM8903_IM_WSEQ_BUSY_EINT_SHIFT              13  /* IM_WSEQ_BUSY_EINT */
#define WM8903_IM_WSEQ_BUSY_EINT_WIDTH               1  /* IM_WSEQ_BUSY_EINT */
#define WM8903_IM_GP5_EINT                      0x0010  /* IM_GP5_EINT */
#define WM8903_IM_GP5_EINT_MASK                 0x0010  /* IM_GP5_EINT */
#define WM8903_IM_GP5_EINT_SHIFT                     4  /* IM_GP5_EINT */
#define WM8903_IM_GP5_EINT_WIDTH                     1  /* IM_GP5_EINT */
#define WM8903_IM_GP4_EINT                      0x0008  /* IM_GP4_EINT */
#define WM8903_IM_GP4_EINT_MASK                 0x0008  /* IM_GP4_EINT */
#define WM8903_IM_GP4_EINT_SHIFT                     3  /* IM_GP4_EINT */
#define WM8903_IM_GP4_EINT_WIDTH                     1  /* IM_GP4_EINT */
#define WM8903_IM_GP3_EINT                      0x0004  /* IM_GP3_EINT */
#define WM8903_IM_GP3_EINT_MASK                 0x0004  /* IM_GP3_EINT */
#define WM8903_IM_GP3_EINT_SHIFT                     2  /* IM_GP3_EINT */
#define WM8903_IM_GP3_EINT_WIDTH                     1  /* IM_GP3_EINT */
#define WM8903_IM_GP2_EINT                      0x0002  /* IM_GP2_EINT */
#define WM8903_IM_GP2_EINT_MASK                 0x0002  /* IM_GP2_EINT */
#define WM8903_IM_GP2_EINT_SHIFT                     1  /* IM_GP2_EINT */
#define WM8903_IM_GP2_EINT_WIDTH                     1  /* IM_GP2_EINT */
#define WM8903_IM_GP1_EINT                      0x0001  /* IM_GP1_EINT */
#define WM8903_IM_GP1_EINT_MASK                 0x0001  /* IM_GP1_EINT */
#define WM8903_IM_GP1_EINT_SHIFT                     0  /* IM_GP1_EINT */
#define WM8903_IM_GP1_EINT_WIDTH                     1  /* IM_GP1_EINT */

/*
 * R123 (0x7B) - Interrupt Polarity 1
 */
#define WM8903_MICSHRT_INV                      0x8000  /* MICSHRT_INV */
#define WM8903_MICSHRT_INV_MASK                 0x8000  /* MICSHRT_INV */
#define WM8903_MICSHRT_INV_SHIFT                    15  /* MICSHRT_INV */
#define WM8903_MICSHRT_INV_WIDTH                     1  /* MICSHRT_INV */
#define WM8903_MICDET_INV                       0x4000  /* MICDET_INV */
#define WM8903_MICDET_INV_MASK                  0x4000  /* MICDET_INV */
#define WM8903_MICDET_INV_SHIFT                     14  /* MICDET_INV */
#define WM8903_MICDET_INV_WIDTH                      1  /* MICDET_INV */

/*
 * R126 (0x7E) - Interrupt Control
 */
#define WM8903_IRQ_POL                          0x0001  /* IRQ_POL */
#define WM8903_IRQ_POL_MASK                     0x0001  /* IRQ_POL */
#define WM8903_IRQ_POL_SHIFT                         0  /* IRQ_POL */
#define WM8903_IRQ_POL_WIDTH                         1  /* IRQ_POL */

/*
 * R129 (0x81) - Control Interface Test 1
 */
#define WM8903_USER_KEY                         0x0002  /* USER_KEY */
#define WM8903_USER_KEY_MASK                    0x0002  /* USER_KEY */
#define WM8903_USER_KEY_SHIFT                        1  /* USER_KEY */
#define WM8903_USER_KEY_WIDTH                        1  /* USER_KEY */
#define WM8903_TEST_KEY                         0x0001  /* TEST_KEY */
#define WM8903_TEST_KEY_MASK                    0x0001  /* TEST_KEY */
#define WM8903_TEST_KEY_SHIFT                        0  /* TEST_KEY */
#define WM8903_TEST_KEY_WIDTH                        1  /* TEST_KEY */

/*
 * R149 (0x95) - Charge Pump Test 1
 */
#define WM8903_CP_SW_KELVIN_MODE_MASK           0x0006  /* CP_SW_KELVIN_MODE - [2:1] */
#define WM8903_CP_SW_KELVIN_MODE_SHIFT               1  /* CP_SW_KELVIN_MODE - [2:1] */
#define WM8903_CP_SW_KELVIN_MODE_WIDTH               2  /* CP_SW_KELVIN_MODE - [2:1] */

/*
 * R164 (0xA4) - Clock Rate Test 4
 */
#define WM8903_ADC_DIG_MIC                      0x0200  /* ADC_DIG_MIC */
#define WM8903_ADC_DIG_MIC_MASK                 0x0200  /* ADC_DIG_MIC */
#define WM8903_ADC_DIG_MIC_SHIFT                     9  /* ADC_DIG_MIC */
#define WM8903_ADC_DIG_MIC_WIDTH                     1  /* ADC_DIG_MIC */

/*
 * R172 (0xAC) - Analogue Output Bias 0
 */
#define WM8903_PGA_BIAS_MASK                    0x0070  /* PGA_BIAS - [6:4] */
#define WM8903_PGA_BIAS_SHIFT                        4  /* PGA_BIAS - [6:4] */
#define WM8903_PGA_BIAS_WIDTH                        3  /* PGA_BIAS - [6:4] */

#endif
