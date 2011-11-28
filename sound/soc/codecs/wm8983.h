/*
 * wm8983.h  --  WM8983 ALSA SoC Audio driver
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 * Author: Dimitris Papastamos <dp@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _WM8983_H
#define _WM8983_H

/*
 * Register values.
 */
#define WM8983_SOFTWARE_RESET                   0x00
#define WM8983_POWER_MANAGEMENT_1               0x01
#define WM8983_POWER_MANAGEMENT_2               0x02
#define WM8983_POWER_MANAGEMENT_3               0x03
#define WM8983_AUDIO_INTERFACE                  0x04
#define WM8983_COMPANDING_CONTROL               0x05
#define WM8983_CLOCK_GEN_CONTROL                0x06
#define WM8983_ADDITIONAL_CONTROL               0x07
#define WM8983_GPIO_CONTROL                     0x08
#define WM8983_JACK_DETECT_CONTROL_1            0x09
#define WM8983_DAC_CONTROL                      0x0A
#define WM8983_LEFT_DAC_DIGITAL_VOL             0x0B
#define WM8983_RIGHT_DAC_DIGITAL_VOL            0x0C
#define WM8983_JACK_DETECT_CONTROL_2            0x0D
#define WM8983_ADC_CONTROL                      0x0E
#define WM8983_LEFT_ADC_DIGITAL_VOL             0x0F
#define WM8983_RIGHT_ADC_DIGITAL_VOL            0x10
#define WM8983_EQ1_LOW_SHELF                    0x12
#define WM8983_EQ2_PEAK_1                       0x13
#define WM8983_EQ3_PEAK_2                       0x14
#define WM8983_EQ4_PEAK_3                       0x15
#define WM8983_EQ5_HIGH_SHELF                   0x16
#define WM8983_DAC_LIMITER_1                    0x18
#define WM8983_DAC_LIMITER_2                    0x19
#define WM8983_NOTCH_FILTER_1                   0x1B
#define WM8983_NOTCH_FILTER_2                   0x1C
#define WM8983_NOTCH_FILTER_3                   0x1D
#define WM8983_NOTCH_FILTER_4                   0x1E
#define WM8983_ALC_CONTROL_1                    0x20
#define WM8983_ALC_CONTROL_2                    0x21
#define WM8983_ALC_CONTROL_3                    0x22
#define WM8983_NOISE_GATE                       0x23
#define WM8983_PLL_N                            0x24
#define WM8983_PLL_K_1                          0x25
#define WM8983_PLL_K_2                          0x26
#define WM8983_PLL_K_3                          0x27
#define WM8983_3D_CONTROL                       0x29
#define WM8983_OUT4_TO_ADC                      0x2A
#define WM8983_BEEP_CONTROL                     0x2B
#define WM8983_INPUT_CTRL                       0x2C
#define WM8983_LEFT_INP_PGA_GAIN_CTRL           0x2D
#define WM8983_RIGHT_INP_PGA_GAIN_CTRL          0x2E
#define WM8983_LEFT_ADC_BOOST_CTRL              0x2F
#define WM8983_RIGHT_ADC_BOOST_CTRL             0x30
#define WM8983_OUTPUT_CTRL                      0x31
#define WM8983_LEFT_MIXER_CTRL                  0x32
#define WM8983_RIGHT_MIXER_CTRL                 0x33
#define WM8983_LOUT1_HP_VOLUME_CTRL             0x34
#define WM8983_ROUT1_HP_VOLUME_CTRL             0x35
#define WM8983_LOUT2_SPK_VOLUME_CTRL            0x36
#define WM8983_ROUT2_SPK_VOLUME_CTRL            0x37
#define WM8983_OUT3_MIXER_CTRL                  0x38
#define WM8983_OUT4_MONO_MIX_CTRL               0x39
#define WM8983_BIAS_CTRL                        0x3D

#define WM8983_REGISTER_COUNT                   59
#define WM8983_MAX_REGISTER                     0x3F

/*
 * Field Definitions.
 */

/*
 * R0 (0x00) - Software Reset
 */
#define WM8983_SOFTWARE_RESET_MASK              0x01FF  /* SOFTWARE_RESET - [8:0] */
#define WM8983_SOFTWARE_RESET_SHIFT                  0  /* SOFTWARE_RESET - [8:0] */
#define WM8983_SOFTWARE_RESET_WIDTH                  9  /* SOFTWARE_RESET - [8:0] */

/*
 * R1 (0x01) - Power management 1
 */
#define WM8983_BUFDCOPEN                        0x0100  /* BUFDCOPEN */
#define WM8983_BUFDCOPEN_MASK                   0x0100  /* BUFDCOPEN */
#define WM8983_BUFDCOPEN_SHIFT                       8  /* BUFDCOPEN */
#define WM8983_BUFDCOPEN_WIDTH                       1  /* BUFDCOPEN */
#define WM8983_OUT4MIXEN                        0x0080  /* OUT4MIXEN */
#define WM8983_OUT4MIXEN_MASK                   0x0080  /* OUT4MIXEN */
#define WM8983_OUT4MIXEN_SHIFT                       7  /* OUT4MIXEN */
#define WM8983_OUT4MIXEN_WIDTH                       1  /* OUT4MIXEN */
#define WM8983_OUT3MIXEN                        0x0040  /* OUT3MIXEN */
#define WM8983_OUT3MIXEN_MASK                   0x0040  /* OUT3MIXEN */
#define WM8983_OUT3MIXEN_SHIFT                       6  /* OUT3MIXEN */
#define WM8983_OUT3MIXEN_WIDTH                       1  /* OUT3MIXEN */
#define WM8983_PLLEN                            0x0020  /* PLLEN */
#define WM8983_PLLEN_MASK                       0x0020  /* PLLEN */
#define WM8983_PLLEN_SHIFT                           5  /* PLLEN */
#define WM8983_PLLEN_WIDTH                           1  /* PLLEN */
#define WM8983_MICBEN                           0x0010  /* MICBEN */
#define WM8983_MICBEN_MASK                      0x0010  /* MICBEN */
#define WM8983_MICBEN_SHIFT                          4  /* MICBEN */
#define WM8983_MICBEN_WIDTH                          1  /* MICBEN */
#define WM8983_BIASEN                           0x0008  /* BIASEN */
#define WM8983_BIASEN_MASK                      0x0008  /* BIASEN */
#define WM8983_BIASEN_SHIFT                          3  /* BIASEN */
#define WM8983_BIASEN_WIDTH                          1  /* BIASEN */
#define WM8983_BUFIOEN                          0x0004  /* BUFIOEN */
#define WM8983_BUFIOEN_MASK                     0x0004  /* BUFIOEN */
#define WM8983_BUFIOEN_SHIFT                         2  /* BUFIOEN */
#define WM8983_BUFIOEN_WIDTH                         1  /* BUFIOEN */
#define WM8983_VMIDSEL_MASK                     0x0003  /* VMIDSEL - [1:0] */
#define WM8983_VMIDSEL_SHIFT                         0  /* VMIDSEL - [1:0] */
#define WM8983_VMIDSEL_WIDTH                         2  /* VMIDSEL - [1:0] */

/*
 * R2 (0x02) - Power management 2
 */
#define WM8983_ROUT1EN                          0x0100  /* ROUT1EN */
#define WM8983_ROUT1EN_MASK                     0x0100  /* ROUT1EN */
#define WM8983_ROUT1EN_SHIFT                         8  /* ROUT1EN */
#define WM8983_ROUT1EN_WIDTH                         1  /* ROUT1EN */
#define WM8983_LOUT1EN                          0x0080  /* LOUT1EN */
#define WM8983_LOUT1EN_MASK                     0x0080  /* LOUT1EN */
#define WM8983_LOUT1EN_SHIFT                         7  /* LOUT1EN */
#define WM8983_LOUT1EN_WIDTH                         1  /* LOUT1EN */
#define WM8983_SLEEP                            0x0040  /* SLEEP */
#define WM8983_SLEEP_MASK                       0x0040  /* SLEEP */
#define WM8983_SLEEP_SHIFT                           6  /* SLEEP */
#define WM8983_SLEEP_WIDTH                           1  /* SLEEP */
#define WM8983_BOOSTENR                         0x0020  /* BOOSTENR */
#define WM8983_BOOSTENR_MASK                    0x0020  /* BOOSTENR */
#define WM8983_BOOSTENR_SHIFT                        5  /* BOOSTENR */
#define WM8983_BOOSTENR_WIDTH                        1  /* BOOSTENR */
#define WM8983_BOOSTENL                         0x0010  /* BOOSTENL */
#define WM8983_BOOSTENL_MASK                    0x0010  /* BOOSTENL */
#define WM8983_BOOSTENL_SHIFT                        4  /* BOOSTENL */
#define WM8983_BOOSTENL_WIDTH                        1  /* BOOSTENL */
#define WM8983_INPGAENR                         0x0008  /* INPGAENR */
#define WM8983_INPGAENR_MASK                    0x0008  /* INPGAENR */
#define WM8983_INPGAENR_SHIFT                        3  /* INPGAENR */
#define WM8983_INPGAENR_WIDTH                        1  /* INPGAENR */
#define WM8983_INPPGAENL                        0x0004  /* INPPGAENL */
#define WM8983_INPPGAENL_MASK                   0x0004  /* INPPGAENL */
#define WM8983_INPPGAENL_SHIFT                       2  /* INPPGAENL */
#define WM8983_INPPGAENL_WIDTH                       1  /* INPPGAENL */
#define WM8983_ADCENR                           0x0002  /* ADCENR */
#define WM8983_ADCENR_MASK                      0x0002  /* ADCENR */
#define WM8983_ADCENR_SHIFT                          1  /* ADCENR */
#define WM8983_ADCENR_WIDTH                          1  /* ADCENR */
#define WM8983_ADCENL                           0x0001  /* ADCENL */
#define WM8983_ADCENL_MASK                      0x0001  /* ADCENL */
#define WM8983_ADCENL_SHIFT                          0  /* ADCENL */
#define WM8983_ADCENL_WIDTH                          1  /* ADCENL */

/*
 * R3 (0x03) - Power management 3
 */
#define WM8983_OUT4EN                           0x0100  /* OUT4EN */
#define WM8983_OUT4EN_MASK                      0x0100  /* OUT4EN */
#define WM8983_OUT4EN_SHIFT                          8  /* OUT4EN */
#define WM8983_OUT4EN_WIDTH                          1  /* OUT4EN */
#define WM8983_OUT3EN                           0x0080  /* OUT3EN */
#define WM8983_OUT3EN_MASK                      0x0080  /* OUT3EN */
#define WM8983_OUT3EN_SHIFT                          7  /* OUT3EN */
#define WM8983_OUT3EN_WIDTH                          1  /* OUT3EN */
#define WM8983_LOUT2EN                          0x0040  /* LOUT2EN */
#define WM8983_LOUT2EN_MASK                     0x0040  /* LOUT2EN */
#define WM8983_LOUT2EN_SHIFT                         6  /* LOUT2EN */
#define WM8983_LOUT2EN_WIDTH                         1  /* LOUT2EN */
#define WM8983_ROUT2EN                          0x0020  /* ROUT2EN */
#define WM8983_ROUT2EN_MASK                     0x0020  /* ROUT2EN */
#define WM8983_ROUT2EN_SHIFT                         5  /* ROUT2EN */
#define WM8983_ROUT2EN_WIDTH                         1  /* ROUT2EN */
#define WM8983_RMIXEN                           0x0008  /* RMIXEN */
#define WM8983_RMIXEN_MASK                      0x0008  /* RMIXEN */
#define WM8983_RMIXEN_SHIFT                          3  /* RMIXEN */
#define WM8983_RMIXEN_WIDTH                          1  /* RMIXEN */
#define WM8983_LMIXEN                           0x0004  /* LMIXEN */
#define WM8983_LMIXEN_MASK                      0x0004  /* LMIXEN */
#define WM8983_LMIXEN_SHIFT                          2  /* LMIXEN */
#define WM8983_LMIXEN_WIDTH                          1  /* LMIXEN */
#define WM8983_DACENR                           0x0002  /* DACENR */
#define WM8983_DACENR_MASK                      0x0002  /* DACENR */
#define WM8983_DACENR_SHIFT                          1  /* DACENR */
#define WM8983_DACENR_WIDTH                          1  /* DACENR */
#define WM8983_DACENL                           0x0001  /* DACENL */
#define WM8983_DACENL_MASK                      0x0001  /* DACENL */
#define WM8983_DACENL_SHIFT                          0  /* DACENL */
#define WM8983_DACENL_WIDTH                          1  /* DACENL */

/*
 * R4 (0x04) - Audio Interface
 */
#define WM8983_BCP                              0x0100  /* BCP */
#define WM8983_BCP_MASK                         0x0100  /* BCP */
#define WM8983_BCP_SHIFT                             8  /* BCP */
#define WM8983_BCP_WIDTH                             1  /* BCP */
#define WM8983_LRCP                             0x0080  /* LRCP */
#define WM8983_LRCP_MASK                        0x0080  /* LRCP */
#define WM8983_LRCP_SHIFT                            7  /* LRCP */
#define WM8983_LRCP_WIDTH                            1  /* LRCP */
#define WM8983_WL_MASK                          0x0060  /* WL - [6:5] */
#define WM8983_WL_SHIFT                              5  /* WL - [6:5] */
#define WM8983_WL_WIDTH                              2  /* WL - [6:5] */
#define WM8983_FMT_MASK                         0x0018  /* FMT - [4:3] */
#define WM8983_FMT_SHIFT                             3  /* FMT - [4:3] */
#define WM8983_FMT_WIDTH                             2  /* FMT - [4:3] */
#define WM8983_DLRSWAP                          0x0004  /* DLRSWAP */
#define WM8983_DLRSWAP_MASK                     0x0004  /* DLRSWAP */
#define WM8983_DLRSWAP_SHIFT                         2  /* DLRSWAP */
#define WM8983_DLRSWAP_WIDTH                         1  /* DLRSWAP */
#define WM8983_ALRSWAP                          0x0002  /* ALRSWAP */
#define WM8983_ALRSWAP_MASK                     0x0002  /* ALRSWAP */
#define WM8983_ALRSWAP_SHIFT                         1  /* ALRSWAP */
#define WM8983_ALRSWAP_WIDTH                         1  /* ALRSWAP */
#define WM8983_MONO                             0x0001  /* MONO */
#define WM8983_MONO_MASK                        0x0001  /* MONO */
#define WM8983_MONO_SHIFT                            0  /* MONO */
#define WM8983_MONO_WIDTH                            1  /* MONO */

/*
 * R5 (0x05) - Companding control
 */
#define WM8983_WL8                              0x0020  /* WL8 */
#define WM8983_WL8_MASK                         0x0020  /* WL8 */
#define WM8983_WL8_SHIFT                             5  /* WL8 */
#define WM8983_WL8_WIDTH                             1  /* WL8 */
#define WM8983_DAC_COMP_MASK                    0x0018  /* DAC_COMP - [4:3] */
#define WM8983_DAC_COMP_SHIFT                        3  /* DAC_COMP - [4:3] */
#define WM8983_DAC_COMP_WIDTH                        2  /* DAC_COMP - [4:3] */
#define WM8983_ADC_COMP_MASK                    0x0006  /* ADC_COMP - [2:1] */
#define WM8983_ADC_COMP_SHIFT                        1  /* ADC_COMP - [2:1] */
#define WM8983_ADC_COMP_WIDTH                        2  /* ADC_COMP - [2:1] */
#define WM8983_LOOPBACK                         0x0001  /* LOOPBACK */
#define WM8983_LOOPBACK_MASK                    0x0001  /* LOOPBACK */
#define WM8983_LOOPBACK_SHIFT                        0  /* LOOPBACK */
#define WM8983_LOOPBACK_WIDTH                        1  /* LOOPBACK */

/*
 * R6 (0x06) - Clock Gen control
 */
#define WM8983_CLKSEL                           0x0100  /* CLKSEL */
#define WM8983_CLKSEL_MASK                      0x0100  /* CLKSEL */
#define WM8983_CLKSEL_SHIFT                          8  /* CLKSEL */
#define WM8983_CLKSEL_WIDTH                          1  /* CLKSEL */
#define WM8983_MCLKDIV_MASK                     0x00E0  /* MCLKDIV - [7:5] */
#define WM8983_MCLKDIV_SHIFT                         5  /* MCLKDIV - [7:5] */
#define WM8983_MCLKDIV_WIDTH                         3  /* MCLKDIV - [7:5] */
#define WM8983_BCLKDIV_MASK                     0x001C  /* BCLKDIV - [4:2] */
#define WM8983_BCLKDIV_SHIFT                         2  /* BCLKDIV - [4:2] */
#define WM8983_BCLKDIV_WIDTH                         3  /* BCLKDIV - [4:2] */
#define WM8983_MS                               0x0001  /* MS */
#define WM8983_MS_MASK                          0x0001  /* MS */
#define WM8983_MS_SHIFT                              0  /* MS */
#define WM8983_MS_WIDTH                              1  /* MS */

/*
 * R7 (0x07) - Additional control
 */
#define WM8983_SR_MASK                          0x000E  /* SR - [3:1] */
#define WM8983_SR_SHIFT                              1  /* SR - [3:1] */
#define WM8983_SR_WIDTH                              3  /* SR - [3:1] */
#define WM8983_SLOWCLKEN                        0x0001  /* SLOWCLKEN */
#define WM8983_SLOWCLKEN_MASK                   0x0001  /* SLOWCLKEN */
#define WM8983_SLOWCLKEN_SHIFT                       0  /* SLOWCLKEN */
#define WM8983_SLOWCLKEN_WIDTH                       1  /* SLOWCLKEN */

/*
 * R8 (0x08) - GPIO Control
 */
#define WM8983_OPCLKDIV_MASK                    0x0030  /* OPCLKDIV - [5:4] */
#define WM8983_OPCLKDIV_SHIFT                        4  /* OPCLKDIV - [5:4] */
#define WM8983_OPCLKDIV_WIDTH                        2  /* OPCLKDIV - [5:4] */
#define WM8983_GPIO1POL                         0x0008  /* GPIO1POL */
#define WM8983_GPIO1POL_MASK                    0x0008  /* GPIO1POL */
#define WM8983_GPIO1POL_SHIFT                        3  /* GPIO1POL */
#define WM8983_GPIO1POL_WIDTH                        1  /* GPIO1POL */
#define WM8983_GPIO1SEL_MASK                    0x0007  /* GPIO1SEL - [2:0] */
#define WM8983_GPIO1SEL_SHIFT                        0  /* GPIO1SEL - [2:0] */
#define WM8983_GPIO1SEL_WIDTH                        3  /* GPIO1SEL - [2:0] */

/*
 * R9 (0x09) - Jack Detect Control 1
 */
#define WM8983_JD_VMID1                         0x0100  /* JD_VMID1 */
#define WM8983_JD_VMID1_MASK                    0x0100  /* JD_VMID1 */
#define WM8983_JD_VMID1_SHIFT                        8  /* JD_VMID1 */
#define WM8983_JD_VMID1_WIDTH                        1  /* JD_VMID1 */
#define WM8983_JD_VMID0                         0x0080  /* JD_VMID0 */
#define WM8983_JD_VMID0_MASK                    0x0080  /* JD_VMID0 */
#define WM8983_JD_VMID0_SHIFT                        7  /* JD_VMID0 */
#define WM8983_JD_VMID0_WIDTH                        1  /* JD_VMID0 */
#define WM8983_JD_EN                            0x0040  /* JD_EN */
#define WM8983_JD_EN_MASK                       0x0040  /* JD_EN */
#define WM8983_JD_EN_SHIFT                           6  /* JD_EN */
#define WM8983_JD_EN_WIDTH                           1  /* JD_EN */
#define WM8983_JD_SEL_MASK                      0x0030  /* JD_SEL - [5:4] */
#define WM8983_JD_SEL_SHIFT                          4  /* JD_SEL - [5:4] */
#define WM8983_JD_SEL_WIDTH                          2  /* JD_SEL - [5:4] */

/*
 * R10 (0x0A) - DAC Control
 */
#define WM8983_SOFTMUTE                         0x0040  /* SOFTMUTE */
#define WM8983_SOFTMUTE_MASK                    0x0040  /* SOFTMUTE */
#define WM8983_SOFTMUTE_SHIFT                        6  /* SOFTMUTE */
#define WM8983_SOFTMUTE_WIDTH                        1  /* SOFTMUTE */
#define WM8983_DACOSR128                        0x0008  /* DACOSR128 */
#define WM8983_DACOSR128_MASK                   0x0008  /* DACOSR128 */
#define WM8983_DACOSR128_SHIFT                       3  /* DACOSR128 */
#define WM8983_DACOSR128_WIDTH                       1  /* DACOSR128 */
#define WM8983_AMUTE                            0x0004  /* AMUTE */
#define WM8983_AMUTE_MASK                       0x0004  /* AMUTE */
#define WM8983_AMUTE_SHIFT                           2  /* AMUTE */
#define WM8983_AMUTE_WIDTH                           1  /* AMUTE */
#define WM8983_DACRPOL                          0x0002  /* DACRPOL */
#define WM8983_DACRPOL_MASK                     0x0002  /* DACRPOL */
#define WM8983_DACRPOL_SHIFT                         1  /* DACRPOL */
#define WM8983_DACRPOL_WIDTH                         1  /* DACRPOL */
#define WM8983_DACLPOL                          0x0001  /* DACLPOL */
#define WM8983_DACLPOL_MASK                     0x0001  /* DACLPOL */
#define WM8983_DACLPOL_SHIFT                         0  /* DACLPOL */
#define WM8983_DACLPOL_WIDTH                         1  /* DACLPOL */

/*
 * R11 (0x0B) - Left DAC digital Vol
 */
#define WM8983_DACVU                            0x0100  /* DACVU */
#define WM8983_DACVU_MASK                       0x0100  /* DACVU */
#define WM8983_DACVU_SHIFT                           8  /* DACVU */
#define WM8983_DACVU_WIDTH                           1  /* DACVU */
#define WM8983_DACLVOL_MASK                     0x00FF  /* DACLVOL - [7:0] */
#define WM8983_DACLVOL_SHIFT                         0  /* DACLVOL - [7:0] */
#define WM8983_DACLVOL_WIDTH                         8  /* DACLVOL - [7:0] */

/*
 * R12 (0x0C) - Right DAC digital vol
 */
#define WM8983_DACVU                            0x0100  /* DACVU */
#define WM8983_DACVU_MASK                       0x0100  /* DACVU */
#define WM8983_DACVU_SHIFT                           8  /* DACVU */
#define WM8983_DACVU_WIDTH                           1  /* DACVU */
#define WM8983_DACRVOL_MASK                     0x00FF  /* DACRVOL - [7:0] */
#define WM8983_DACRVOL_SHIFT                         0  /* DACRVOL - [7:0] */
#define WM8983_DACRVOL_WIDTH                         8  /* DACRVOL - [7:0] */

/*
 * R13 (0x0D) - Jack Detect Control 2
 */
#define WM8983_JD_EN1_MASK                      0x00F0  /* JD_EN1 - [7:4] */
#define WM8983_JD_EN1_SHIFT                          4  /* JD_EN1 - [7:4] */
#define WM8983_JD_EN1_WIDTH                          4  /* JD_EN1 - [7:4] */
#define WM8983_JD_EN0_MASK                      0x000F  /* JD_EN0 - [3:0] */
#define WM8983_JD_EN0_SHIFT                          0  /* JD_EN0 - [3:0] */
#define WM8983_JD_EN0_WIDTH                          4  /* JD_EN0 - [3:0] */

/*
 * R14 (0x0E) - ADC Control
 */
#define WM8983_HPFEN                            0x0100  /* HPFEN */
#define WM8983_HPFEN_MASK                       0x0100  /* HPFEN */
#define WM8983_HPFEN_SHIFT                           8  /* HPFEN */
#define WM8983_HPFEN_WIDTH                           1  /* HPFEN */
#define WM8983_HPFAPP                           0x0080  /* HPFAPP */
#define WM8983_HPFAPP_MASK                      0x0080  /* HPFAPP */
#define WM8983_HPFAPP_SHIFT                          7  /* HPFAPP */
#define WM8983_HPFAPP_WIDTH                          1  /* HPFAPP */
#define WM8983_HPFCUT_MASK                      0x0070  /* HPFCUT - [6:4] */
#define WM8983_HPFCUT_SHIFT                          4  /* HPFCUT - [6:4] */
#define WM8983_HPFCUT_WIDTH                          3  /* HPFCUT - [6:4] */
#define WM8983_ADCOSR128                        0x0008  /* ADCOSR128 */
#define WM8983_ADCOSR128_MASK                   0x0008  /* ADCOSR128 */
#define WM8983_ADCOSR128_SHIFT                       3  /* ADCOSR128 */
#define WM8983_ADCOSR128_WIDTH                       1  /* ADCOSR128 */
#define WM8983_ADCRPOL                          0x0002  /* ADCRPOL */
#define WM8983_ADCRPOL_MASK                     0x0002  /* ADCRPOL */
#define WM8983_ADCRPOL_SHIFT                         1  /* ADCRPOL */
#define WM8983_ADCRPOL_WIDTH                         1  /* ADCRPOL */
#define WM8983_ADCLPOL                          0x0001  /* ADCLPOL */
#define WM8983_ADCLPOL_MASK                     0x0001  /* ADCLPOL */
#define WM8983_ADCLPOL_SHIFT                         0  /* ADCLPOL */
#define WM8983_ADCLPOL_WIDTH                         1  /* ADCLPOL */

/*
 * R15 (0x0F) - Left ADC Digital Vol
 */
#define WM8983_ADCVU                            0x0100  /* ADCVU */
#define WM8983_ADCVU_MASK                       0x0100  /* ADCVU */
#define WM8983_ADCVU_SHIFT                           8  /* ADCVU */
#define WM8983_ADCVU_WIDTH                           1  /* ADCVU */
#define WM8983_ADCLVOL_MASK                     0x00FF  /* ADCLVOL - [7:0] */
#define WM8983_ADCLVOL_SHIFT                         0  /* ADCLVOL - [7:0] */
#define WM8983_ADCLVOL_WIDTH                         8  /* ADCLVOL - [7:0] */

/*
 * R16 (0x10) - Right ADC Digital Vol
 */
#define WM8983_ADCVU                            0x0100  /* ADCVU */
#define WM8983_ADCVU_MASK                       0x0100  /* ADCVU */
#define WM8983_ADCVU_SHIFT                           8  /* ADCVU */
#define WM8983_ADCVU_WIDTH                           1  /* ADCVU */
#define WM8983_ADCRVOL_MASK                     0x00FF  /* ADCRVOL - [7:0] */
#define WM8983_ADCRVOL_SHIFT                         0  /* ADCRVOL - [7:0] */
#define WM8983_ADCRVOL_WIDTH                         8  /* ADCRVOL - [7:0] */

/*
 * R18 (0x12) - EQ1 - low shelf
 */
#define WM8983_EQ3DMODE                         0x0100  /* EQ3DMODE */
#define WM8983_EQ3DMODE_MASK                    0x0100  /* EQ3DMODE */
#define WM8983_EQ3DMODE_SHIFT                        8  /* EQ3DMODE */
#define WM8983_EQ3DMODE_WIDTH                        1  /* EQ3DMODE */
#define WM8983_EQ1C_MASK                        0x0060  /* EQ1C - [6:5] */
#define WM8983_EQ1C_SHIFT                            5  /* EQ1C - [6:5] */
#define WM8983_EQ1C_WIDTH                            2  /* EQ1C - [6:5] */
#define WM8983_EQ1G_MASK                        0x001F  /* EQ1G - [4:0] */
#define WM8983_EQ1G_SHIFT                            0  /* EQ1G - [4:0] */
#define WM8983_EQ1G_WIDTH                            5  /* EQ1G - [4:0] */

/*
 * R19 (0x13) - EQ2 - peak 1
 */
#define WM8983_EQ2BW                            0x0100  /* EQ2BW */
#define WM8983_EQ2BW_MASK                       0x0100  /* EQ2BW */
#define WM8983_EQ2BW_SHIFT                           8  /* EQ2BW */
#define WM8983_EQ2BW_WIDTH                           1  /* EQ2BW */
#define WM8983_EQ2C_MASK                        0x0060  /* EQ2C - [6:5] */
#define WM8983_EQ2C_SHIFT                            5  /* EQ2C - [6:5] */
#define WM8983_EQ2C_WIDTH                            2  /* EQ2C - [6:5] */
#define WM8983_EQ2G_MASK                        0x001F  /* EQ2G - [4:0] */
#define WM8983_EQ2G_SHIFT                            0  /* EQ2G - [4:0] */
#define WM8983_EQ2G_WIDTH                            5  /* EQ2G - [4:0] */

/*
 * R20 (0x14) - EQ3 - peak 2
 */
#define WM8983_EQ3BW                            0x0100  /* EQ3BW */
#define WM8983_EQ3BW_MASK                       0x0100  /* EQ3BW */
#define WM8983_EQ3BW_SHIFT                           8  /* EQ3BW */
#define WM8983_EQ3BW_WIDTH                           1  /* EQ3BW */
#define WM8983_EQ3C_MASK                        0x0060  /* EQ3C - [6:5] */
#define WM8983_EQ3C_SHIFT                            5  /* EQ3C - [6:5] */
#define WM8983_EQ3C_WIDTH                            2  /* EQ3C - [6:5] */
#define WM8983_EQ3G_MASK                        0x001F  /* EQ3G - [4:0] */
#define WM8983_EQ3G_SHIFT                            0  /* EQ3G - [4:0] */
#define WM8983_EQ3G_WIDTH                            5  /* EQ3G - [4:0] */

/*
 * R21 (0x15) - EQ4 - peak 3
 */
#define WM8983_EQ4BW                            0x0100  /* EQ4BW */
#define WM8983_EQ4BW_MASK                       0x0100  /* EQ4BW */
#define WM8983_EQ4BW_SHIFT                           8  /* EQ4BW */
#define WM8983_EQ4BW_WIDTH                           1  /* EQ4BW */
#define WM8983_EQ4C_MASK                        0x0060  /* EQ4C - [6:5] */
#define WM8983_EQ4C_SHIFT                            5  /* EQ4C - [6:5] */
#define WM8983_EQ4C_WIDTH                            2  /* EQ4C - [6:5] */
#define WM8983_EQ4G_MASK                        0x001F  /* EQ4G - [4:0] */
#define WM8983_EQ4G_SHIFT                            0  /* EQ4G - [4:0] */
#define WM8983_EQ4G_WIDTH                            5  /* EQ4G - [4:0] */

/*
 * R22 (0x16) - EQ5 - high shelf
 */
#define WM8983_EQ5C_MASK                        0x0060  /* EQ5C - [6:5] */
#define WM8983_EQ5C_SHIFT                            5  /* EQ5C - [6:5] */
#define WM8983_EQ5C_WIDTH                            2  /* EQ5C - [6:5] */
#define WM8983_EQ5G_MASK                        0x001F  /* EQ5G - [4:0] */
#define WM8983_EQ5G_SHIFT                            0  /* EQ5G - [4:0] */
#define WM8983_EQ5G_WIDTH                            5  /* EQ5G - [4:0] */

/*
 * R24 (0x18) - DAC Limiter 1
 */
#define WM8983_LIMEN                            0x0100  /* LIMEN */
#define WM8983_LIMEN_MASK                       0x0100  /* LIMEN */
#define WM8983_LIMEN_SHIFT                           8  /* LIMEN */
#define WM8983_LIMEN_WIDTH                           1  /* LIMEN */
#define WM8983_LIMDCY_MASK                      0x00F0  /* LIMDCY - [7:4] */
#define WM8983_LIMDCY_SHIFT                          4  /* LIMDCY - [7:4] */
#define WM8983_LIMDCY_WIDTH                          4  /* LIMDCY - [7:4] */
#define WM8983_LIMATK_MASK                      0x000F  /* LIMATK - [3:0] */
#define WM8983_LIMATK_SHIFT                          0  /* LIMATK - [3:0] */
#define WM8983_LIMATK_WIDTH                          4  /* LIMATK - [3:0] */

/*
 * R25 (0x19) - DAC Limiter 2
 */
#define WM8983_LIMLVL_MASK                      0x0070  /* LIMLVL - [6:4] */
#define WM8983_LIMLVL_SHIFT                          4  /* LIMLVL - [6:4] */
#define WM8983_LIMLVL_WIDTH                          3  /* LIMLVL - [6:4] */
#define WM8983_LIMBOOST_MASK                    0x000F  /* LIMBOOST - [3:0] */
#define WM8983_LIMBOOST_SHIFT                        0  /* LIMBOOST - [3:0] */
#define WM8983_LIMBOOST_WIDTH                        4  /* LIMBOOST - [3:0] */

/*
 * R27 (0x1B) - Notch Filter 1
 */
#define WM8983_NFU                              0x0100  /* NFU */
#define WM8983_NFU_MASK                         0x0100  /* NFU */
#define WM8983_NFU_SHIFT                             8  /* NFU */
#define WM8983_NFU_WIDTH                             1  /* NFU */
#define WM8983_NFEN                             0x0080  /* NFEN */
#define WM8983_NFEN_MASK                        0x0080  /* NFEN */
#define WM8983_NFEN_SHIFT                            7  /* NFEN */
#define WM8983_NFEN_WIDTH                            1  /* NFEN */
#define WM8983_NFA0_13_7_MASK                   0x007F  /* NFA0(13:7) - [6:0] */
#define WM8983_NFA0_13_7_SHIFT                       0  /* NFA0(13:7) - [6:0] */
#define WM8983_NFA0_13_7_WIDTH                       7  /* NFA0(13:7) - [6:0] */

/*
 * R28 (0x1C) - Notch Filter 2
 */
#define WM8983_NFU                              0x0100  /* NFU */
#define WM8983_NFU_MASK                         0x0100  /* NFU */
#define WM8983_NFU_SHIFT                             8  /* NFU */
#define WM8983_NFU_WIDTH                             1  /* NFU */
#define WM8983_NFA0_6_0_MASK                    0x007F  /* NFA0(6:0) - [6:0] */
#define WM8983_NFA0_6_0_SHIFT                        0  /* NFA0(6:0) - [6:0] */
#define WM8983_NFA0_6_0_WIDTH                        7  /* NFA0(6:0) - [6:0] */

/*
 * R29 (0x1D) - Notch Filter 3
 */
#define WM8983_NFU                              0x0100  /* NFU */
#define WM8983_NFU_MASK                         0x0100  /* NFU */
#define WM8983_NFU_SHIFT                             8  /* NFU */
#define WM8983_NFU_WIDTH                             1  /* NFU */
#define WM8983_NFA1_13_7_MASK                   0x007F  /* NFA1(13:7) - [6:0] */
#define WM8983_NFA1_13_7_SHIFT                       0  /* NFA1(13:7) - [6:0] */
#define WM8983_NFA1_13_7_WIDTH                       7  /* NFA1(13:7) - [6:0] */

/*
 * R30 (0x1E) - Notch Filter 4
 */
#define WM8983_NFU                              0x0100  /* NFU */
#define WM8983_NFU_MASK                         0x0100  /* NFU */
#define WM8983_NFU_SHIFT                             8  /* NFU */
#define WM8983_NFU_WIDTH                             1  /* NFU */
#define WM8983_NFA1_6_0_MASK                    0x007F  /* NFA1(6:0) - [6:0] */
#define WM8983_NFA1_6_0_SHIFT                        0  /* NFA1(6:0) - [6:0] */
#define WM8983_NFA1_6_0_WIDTH                        7  /* NFA1(6:0) - [6:0] */

/*
 * R32 (0x20) - ALC control 1
 */
#define WM8983_ALCSEL_MASK                      0x0180  /* ALCSEL - [8:7] */
#define WM8983_ALCSEL_SHIFT                          7  /* ALCSEL - [8:7] */
#define WM8983_ALCSEL_WIDTH                          2  /* ALCSEL - [8:7] */
#define WM8983_ALCMAX_MASK                      0x0038  /* ALCMAX - [5:3] */
#define WM8983_ALCMAX_SHIFT                          3  /* ALCMAX - [5:3] */
#define WM8983_ALCMAX_WIDTH                          3  /* ALCMAX - [5:3] */
#define WM8983_ALCMIN_MASK                      0x0007  /* ALCMIN - [2:0] */
#define WM8983_ALCMIN_SHIFT                          0  /* ALCMIN - [2:0] */
#define WM8983_ALCMIN_WIDTH                          3  /* ALCMIN - [2:0] */

/*
 * R33 (0x21) - ALC control 2
 */
#define WM8983_ALCHLD_MASK                      0x00F0  /* ALCHLD - [7:4] */
#define WM8983_ALCHLD_SHIFT                          4  /* ALCHLD - [7:4] */
#define WM8983_ALCHLD_WIDTH                          4  /* ALCHLD - [7:4] */
#define WM8983_ALCLVL_MASK                      0x000F  /* ALCLVL - [3:0] */
#define WM8983_ALCLVL_SHIFT                          0  /* ALCLVL - [3:0] */
#define WM8983_ALCLVL_WIDTH                          4  /* ALCLVL - [3:0] */

/*
 * R34 (0x22) - ALC control 3
 */
#define WM8983_ALCMODE                          0x0100  /* ALCMODE */
#define WM8983_ALCMODE_MASK                     0x0100  /* ALCMODE */
#define WM8983_ALCMODE_SHIFT                         8  /* ALCMODE */
#define WM8983_ALCMODE_WIDTH                         1  /* ALCMODE */
#define WM8983_ALCDCY_MASK                      0x00F0  /* ALCDCY - [7:4] */
#define WM8983_ALCDCY_SHIFT                          4  /* ALCDCY - [7:4] */
#define WM8983_ALCDCY_WIDTH                          4  /* ALCDCY - [7:4] */
#define WM8983_ALCATK_MASK                      0x000F  /* ALCATK - [3:0] */
#define WM8983_ALCATK_SHIFT                          0  /* ALCATK - [3:0] */
#define WM8983_ALCATK_WIDTH                          4  /* ALCATK - [3:0] */

/*
 * R35 (0x23) - Noise Gate
 */
#define WM8983_NGEN                             0x0008  /* NGEN */
#define WM8983_NGEN_MASK                        0x0008  /* NGEN */
#define WM8983_NGEN_SHIFT                            3  /* NGEN */
#define WM8983_NGEN_WIDTH                            1  /* NGEN */
#define WM8983_NGTH_MASK                        0x0007  /* NGTH - [2:0] */
#define WM8983_NGTH_SHIFT                            0  /* NGTH - [2:0] */
#define WM8983_NGTH_WIDTH                            3  /* NGTH - [2:0] */

/*
 * R36 (0x24) - PLL N
 */
#define WM8983_PLL_PRESCALE                     0x0010  /* PLL_PRESCALE */
#define WM8983_PLL_PRESCALE_MASK                0x0010  /* PLL_PRESCALE */
#define WM8983_PLL_PRESCALE_SHIFT                    4  /* PLL_PRESCALE */
#define WM8983_PLL_PRESCALE_WIDTH                    1  /* PLL_PRESCALE */
#define WM8983_PLLN_MASK                        0x000F  /* PLLN - [3:0] */
#define WM8983_PLLN_SHIFT                            0  /* PLLN - [3:0] */
#define WM8983_PLLN_WIDTH                            4  /* PLLN - [3:0] */

/*
 * R37 (0x25) - PLL K 1
 */
#define WM8983_PLLK_23_18_MASK                  0x003F  /* PLLK(23:18) - [5:0] */
#define WM8983_PLLK_23_18_SHIFT                      0  /* PLLK(23:18) - [5:0] */
#define WM8983_PLLK_23_18_WIDTH                      6  /* PLLK(23:18) - [5:0] */

/*
 * R38 (0x26) - PLL K 2
 */
#define WM8983_PLLK_17_9_MASK                   0x01FF  /* PLLK(17:9) - [8:0] */
#define WM8983_PLLK_17_9_SHIFT                       0  /* PLLK(17:9) - [8:0] */
#define WM8983_PLLK_17_9_WIDTH                       9  /* PLLK(17:9) - [8:0] */

/*
 * R39 (0x27) - PLL K 3
 */
#define WM8983_PLLK_8_0_MASK                    0x01FF  /* PLLK(8:0) - [8:0] */
#define WM8983_PLLK_8_0_SHIFT                        0  /* PLLK(8:0) - [8:0] */
#define WM8983_PLLK_8_0_WIDTH                        9  /* PLLK(8:0) - [8:0] */

/*
 * R41 (0x29) - 3D control
 */
#define WM8983_DEPTH3D_MASK                     0x000F  /* DEPTH3D - [3:0] */
#define WM8983_DEPTH3D_SHIFT                         0  /* DEPTH3D - [3:0] */
#define WM8983_DEPTH3D_WIDTH                         4  /* DEPTH3D - [3:0] */

/*
 * R42 (0x2A) - OUT4 to ADC
 */
#define WM8983_OUT4_2ADCVOL_MASK                0x01C0  /* OUT4_2ADCVOL - [8:6] */
#define WM8983_OUT4_2ADCVOL_SHIFT                    6  /* OUT4_2ADCVOL - [8:6] */
#define WM8983_OUT4_2ADCVOL_WIDTH                    3  /* OUT4_2ADCVOL - [8:6] */
#define WM8983_OUT4_2LNR                        0x0020  /* OUT4_2LNR */
#define WM8983_OUT4_2LNR_MASK                   0x0020  /* OUT4_2LNR */
#define WM8983_OUT4_2LNR_SHIFT                       5  /* OUT4_2LNR */
#define WM8983_OUT4_2LNR_WIDTH                       1  /* OUT4_2LNR */
#define WM8983_POBCTRL                          0x0004  /* POBCTRL */
#define WM8983_POBCTRL_MASK                     0x0004  /* POBCTRL */
#define WM8983_POBCTRL_SHIFT                         2  /* POBCTRL */
#define WM8983_POBCTRL_WIDTH                         1  /* POBCTRL */
#define WM8983_DELEN                            0x0002  /* DELEN */
#define WM8983_DELEN_MASK                       0x0002  /* DELEN */
#define WM8983_DELEN_SHIFT                           1  /* DELEN */
#define WM8983_DELEN_WIDTH                           1  /* DELEN */
#define WM8983_OUT1DEL                          0x0001  /* OUT1DEL */
#define WM8983_OUT1DEL_MASK                     0x0001  /* OUT1DEL */
#define WM8983_OUT1DEL_SHIFT                         0  /* OUT1DEL */
#define WM8983_OUT1DEL_WIDTH                         1  /* OUT1DEL */

/*
 * R43 (0x2B) - Beep control
 */
#define WM8983_BYPL2RMIX                        0x0100  /* BYPL2RMIX */
#define WM8983_BYPL2RMIX_MASK                   0x0100  /* BYPL2RMIX */
#define WM8983_BYPL2RMIX_SHIFT                       8  /* BYPL2RMIX */
#define WM8983_BYPL2RMIX_WIDTH                       1  /* BYPL2RMIX */
#define WM8983_BYPR2LMIX                        0x0080  /* BYPR2LMIX */
#define WM8983_BYPR2LMIX_MASK                   0x0080  /* BYPR2LMIX */
#define WM8983_BYPR2LMIX_SHIFT                       7  /* BYPR2LMIX */
#define WM8983_BYPR2LMIX_WIDTH                       1  /* BYPR2LMIX */
#define WM8983_MUTERPGA2INV                     0x0020  /* MUTERPGA2INV */
#define WM8983_MUTERPGA2INV_MASK                0x0020  /* MUTERPGA2INV */
#define WM8983_MUTERPGA2INV_SHIFT                    5  /* MUTERPGA2INV */
#define WM8983_MUTERPGA2INV_WIDTH                    1  /* MUTERPGA2INV */
#define WM8983_INVROUT2                         0x0010  /* INVROUT2 */
#define WM8983_INVROUT2_MASK                    0x0010  /* INVROUT2 */
#define WM8983_INVROUT2_SHIFT                        4  /* INVROUT2 */
#define WM8983_INVROUT2_WIDTH                        1  /* INVROUT2 */
#define WM8983_BEEPVOL_MASK                     0x000E  /* BEEPVOL - [3:1] */
#define WM8983_BEEPVOL_SHIFT                         1  /* BEEPVOL - [3:1] */
#define WM8983_BEEPVOL_WIDTH                         3  /* BEEPVOL - [3:1] */
#define WM8983_BEEPEN                           0x0001  /* BEEPEN */
#define WM8983_BEEPEN_MASK                      0x0001  /* BEEPEN */
#define WM8983_BEEPEN_SHIFT                          0  /* BEEPEN */
#define WM8983_BEEPEN_WIDTH                          1  /* BEEPEN */

/*
 * R44 (0x2C) - Input ctrl
 */
#define WM8983_MBVSEL                           0x0100  /* MBVSEL */
#define WM8983_MBVSEL_MASK                      0x0100  /* MBVSEL */
#define WM8983_MBVSEL_SHIFT                          8  /* MBVSEL */
#define WM8983_MBVSEL_WIDTH                          1  /* MBVSEL */
#define WM8983_R2_2INPPGA                       0x0040  /* R2_2INPPGA */
#define WM8983_R2_2INPPGA_MASK                  0x0040  /* R2_2INPPGA */
#define WM8983_R2_2INPPGA_SHIFT                      6  /* R2_2INPPGA */
#define WM8983_R2_2INPPGA_WIDTH                      1  /* R2_2INPPGA */
#define WM8983_RIN2INPPGA                       0x0020  /* RIN2INPPGA */
#define WM8983_RIN2INPPGA_MASK                  0x0020  /* RIN2INPPGA */
#define WM8983_RIN2INPPGA_SHIFT                      5  /* RIN2INPPGA */
#define WM8983_RIN2INPPGA_WIDTH                      1  /* RIN2INPPGA */
#define WM8983_RIP2INPPGA                       0x0010  /* RIP2INPPGA */
#define WM8983_RIP2INPPGA_MASK                  0x0010  /* RIP2INPPGA */
#define WM8983_RIP2INPPGA_SHIFT                      4  /* RIP2INPPGA */
#define WM8983_RIP2INPPGA_WIDTH                      1  /* RIP2INPPGA */
#define WM8983_L2_2INPPGA                       0x0004  /* L2_2INPPGA */
#define WM8983_L2_2INPPGA_MASK                  0x0004  /* L2_2INPPGA */
#define WM8983_L2_2INPPGA_SHIFT                      2  /* L2_2INPPGA */
#define WM8983_L2_2INPPGA_WIDTH                      1  /* L2_2INPPGA */
#define WM8983_LIN2INPPGA                       0x0002  /* LIN2INPPGA */
#define WM8983_LIN2INPPGA_MASK                  0x0002  /* LIN2INPPGA */
#define WM8983_LIN2INPPGA_SHIFT                      1  /* LIN2INPPGA */
#define WM8983_LIN2INPPGA_WIDTH                      1  /* LIN2INPPGA */
#define WM8983_LIP2INPPGA                       0x0001  /* LIP2INPPGA */
#define WM8983_LIP2INPPGA_MASK                  0x0001  /* LIP2INPPGA */
#define WM8983_LIP2INPPGA_SHIFT                      0  /* LIP2INPPGA */
#define WM8983_LIP2INPPGA_WIDTH                      1  /* LIP2INPPGA */

/*
 * R45 (0x2D) - Left INP PGA gain ctrl
 */
#define WM8983_INPGAVU                          0x0100  /* INPGAVU */
#define WM8983_INPGAVU_MASK                     0x0100  /* INPGAVU */
#define WM8983_INPGAVU_SHIFT                         8  /* INPGAVU */
#define WM8983_INPGAVU_WIDTH                         1  /* INPGAVU */
#define WM8983_INPPGAZCL                        0x0080  /* INPPGAZCL */
#define WM8983_INPPGAZCL_MASK                   0x0080  /* INPPGAZCL */
#define WM8983_INPPGAZCL_SHIFT                       7  /* INPPGAZCL */
#define WM8983_INPPGAZCL_WIDTH                       1  /* INPPGAZCL */
#define WM8983_INPPGAMUTEL                      0x0040  /* INPPGAMUTEL */
#define WM8983_INPPGAMUTEL_MASK                 0x0040  /* INPPGAMUTEL */
#define WM8983_INPPGAMUTEL_SHIFT                     6  /* INPPGAMUTEL */
#define WM8983_INPPGAMUTEL_WIDTH                     1  /* INPPGAMUTEL */
#define WM8983_INPPGAVOLL_MASK                  0x003F  /* INPPGAVOLL - [5:0] */
#define WM8983_INPPGAVOLL_SHIFT                      0  /* INPPGAVOLL - [5:0] */
#define WM8983_INPPGAVOLL_WIDTH                      6  /* INPPGAVOLL - [5:0] */

/*
 * R46 (0x2E) - Right INP PGA gain ctrl
 */
#define WM8983_INPGAVU                          0x0100  /* INPGAVU */
#define WM8983_INPGAVU_MASK                     0x0100  /* INPGAVU */
#define WM8983_INPGAVU_SHIFT                         8  /* INPGAVU */
#define WM8983_INPGAVU_WIDTH                         1  /* INPGAVU */
#define WM8983_INPPGAZCR                        0x0080  /* INPPGAZCR */
#define WM8983_INPPGAZCR_MASK                   0x0080  /* INPPGAZCR */
#define WM8983_INPPGAZCR_SHIFT                       7  /* INPPGAZCR */
#define WM8983_INPPGAZCR_WIDTH                       1  /* INPPGAZCR */
#define WM8983_INPPGAMUTER                      0x0040  /* INPPGAMUTER */
#define WM8983_INPPGAMUTER_MASK                 0x0040  /* INPPGAMUTER */
#define WM8983_INPPGAMUTER_SHIFT                     6  /* INPPGAMUTER */
#define WM8983_INPPGAMUTER_WIDTH                     1  /* INPPGAMUTER */
#define WM8983_INPPGAVOLR_MASK                  0x003F  /* INPPGAVOLR - [5:0] */
#define WM8983_INPPGAVOLR_SHIFT                      0  /* INPPGAVOLR - [5:0] */
#define WM8983_INPPGAVOLR_WIDTH                      6  /* INPPGAVOLR - [5:0] */

/*
 * R47 (0x2F) - Left ADC BOOST ctrl
 */
#define WM8983_PGABOOSTL                        0x0100  /* PGABOOSTL */
#define WM8983_PGABOOSTL_MASK                   0x0100  /* PGABOOSTL */
#define WM8983_PGABOOSTL_SHIFT                       8  /* PGABOOSTL */
#define WM8983_PGABOOSTL_WIDTH                       1  /* PGABOOSTL */
#define WM8983_L2_2BOOSTVOL_MASK                0x0070  /* L2_2BOOSTVOL - [6:4] */
#define WM8983_L2_2BOOSTVOL_SHIFT                    4  /* L2_2BOOSTVOL - [6:4] */
#define WM8983_L2_2BOOSTVOL_WIDTH                    3  /* L2_2BOOSTVOL - [6:4] */
#define WM8983_AUXL2BOOSTVOL_MASK               0x0007  /* AUXL2BOOSTVOL - [2:0] */
#define WM8983_AUXL2BOOSTVOL_SHIFT                   0  /* AUXL2BOOSTVOL - [2:0] */
#define WM8983_AUXL2BOOSTVOL_WIDTH                   3  /* AUXL2BOOSTVOL - [2:0] */

/*
 * R48 (0x30) - Right ADC BOOST ctrl
 */
#define WM8983_PGABOOSTR                        0x0100  /* PGABOOSTR */
#define WM8983_PGABOOSTR_MASK                   0x0100  /* PGABOOSTR */
#define WM8983_PGABOOSTR_SHIFT                       8  /* PGABOOSTR */
#define WM8983_PGABOOSTR_WIDTH                       1  /* PGABOOSTR */
#define WM8983_R2_2BOOSTVOL_MASK                0x0070  /* R2_2BOOSTVOL - [6:4] */
#define WM8983_R2_2BOOSTVOL_SHIFT                    4  /* R2_2BOOSTVOL - [6:4] */
#define WM8983_R2_2BOOSTVOL_WIDTH                    3  /* R2_2BOOSTVOL - [6:4] */
#define WM8983_AUXR2BOOSTVOL_MASK               0x0007  /* AUXR2BOOSTVOL - [2:0] */
#define WM8983_AUXR2BOOSTVOL_SHIFT                   0  /* AUXR2BOOSTVOL - [2:0] */
#define WM8983_AUXR2BOOSTVOL_WIDTH                   3  /* AUXR2BOOSTVOL - [2:0] */

/*
 * R49 (0x31) - Output ctrl
 */
#define WM8983_DACL2RMIX                        0x0040  /* DACL2RMIX */
#define WM8983_DACL2RMIX_MASK                   0x0040  /* DACL2RMIX */
#define WM8983_DACL2RMIX_SHIFT                       6  /* DACL2RMIX */
#define WM8983_DACL2RMIX_WIDTH                       1  /* DACL2RMIX */
#define WM8983_DACR2LMIX                        0x0020  /* DACR2LMIX */
#define WM8983_DACR2LMIX_MASK                   0x0020  /* DACR2LMIX */
#define WM8983_DACR2LMIX_SHIFT                       5  /* DACR2LMIX */
#define WM8983_DACR2LMIX_WIDTH                       1  /* DACR2LMIX */
#define WM8983_OUT4BOOST                        0x0010  /* OUT4BOOST */
#define WM8983_OUT4BOOST_MASK                   0x0010  /* OUT4BOOST */
#define WM8983_OUT4BOOST_SHIFT                       4  /* OUT4BOOST */
#define WM8983_OUT4BOOST_WIDTH                       1  /* OUT4BOOST */
#define WM8983_OUT3BOOST                        0x0008  /* OUT3BOOST */
#define WM8983_OUT3BOOST_MASK                   0x0008  /* OUT3BOOST */
#define WM8983_OUT3BOOST_SHIFT                       3  /* OUT3BOOST */
#define WM8983_OUT3BOOST_WIDTH                       1  /* OUT3BOOST */
#define WM8983_SPKBOOST                         0x0004  /* SPKBOOST */
#define WM8983_SPKBOOST_MASK                    0x0004  /* SPKBOOST */
#define WM8983_SPKBOOST_SHIFT                        2  /* SPKBOOST */
#define WM8983_SPKBOOST_WIDTH                        1  /* SPKBOOST */
#define WM8983_TSDEN                            0x0002  /* TSDEN */
#define WM8983_TSDEN_MASK                       0x0002  /* TSDEN */
#define WM8983_TSDEN_SHIFT                           1  /* TSDEN */
#define WM8983_TSDEN_WIDTH                           1  /* TSDEN */
#define WM8983_VROI                             0x0001  /* VROI */
#define WM8983_VROI_MASK                        0x0001  /* VROI */
#define WM8983_VROI_SHIFT                            0  /* VROI */
#define WM8983_VROI_WIDTH                            1  /* VROI */

/*
 * R50 (0x32) - Left mixer ctrl
 */
#define WM8983_AUXLMIXVOL_MASK                  0x01C0  /* AUXLMIXVOL - [8:6] */
#define WM8983_AUXLMIXVOL_SHIFT                      6  /* AUXLMIXVOL - [8:6] */
#define WM8983_AUXLMIXVOL_WIDTH                      3  /* AUXLMIXVOL - [8:6] */
#define WM8983_AUXL2LMIX                        0x0020  /* AUXL2LMIX */
#define WM8983_AUXL2LMIX_MASK                   0x0020  /* AUXL2LMIX */
#define WM8983_AUXL2LMIX_SHIFT                       5  /* AUXL2LMIX */
#define WM8983_AUXL2LMIX_WIDTH                       1  /* AUXL2LMIX */
#define WM8983_BYPLMIXVOL_MASK                  0x001C  /* BYPLMIXVOL - [4:2] */
#define WM8983_BYPLMIXVOL_SHIFT                      2  /* BYPLMIXVOL - [4:2] */
#define WM8983_BYPLMIXVOL_WIDTH                      3  /* BYPLMIXVOL - [4:2] */
#define WM8983_BYPL2LMIX                        0x0002  /* BYPL2LMIX */
#define WM8983_BYPL2LMIX_MASK                   0x0002  /* BYPL2LMIX */
#define WM8983_BYPL2LMIX_SHIFT                       1  /* BYPL2LMIX */
#define WM8983_BYPL2LMIX_WIDTH                       1  /* BYPL2LMIX */
#define WM8983_DACL2LMIX                        0x0001  /* DACL2LMIX */
#define WM8983_DACL2LMIX_MASK                   0x0001  /* DACL2LMIX */
#define WM8983_DACL2LMIX_SHIFT                       0  /* DACL2LMIX */
#define WM8983_DACL2LMIX_WIDTH                       1  /* DACL2LMIX */

/*
 * R51 (0x33) - Right mixer ctrl
 */
#define WM8983_AUXRMIXVOL_MASK                  0x01C0  /* AUXRMIXVOL - [8:6] */
#define WM8983_AUXRMIXVOL_SHIFT                      6  /* AUXRMIXVOL - [8:6] */
#define WM8983_AUXRMIXVOL_WIDTH                      3  /* AUXRMIXVOL - [8:6] */
#define WM8983_AUXR2RMIX                        0x0020  /* AUXR2RMIX */
#define WM8983_AUXR2RMIX_MASK                   0x0020  /* AUXR2RMIX */
#define WM8983_AUXR2RMIX_SHIFT                       5  /* AUXR2RMIX */
#define WM8983_AUXR2RMIX_WIDTH                       1  /* AUXR2RMIX */
#define WM8983_BYPRMIXVOL_MASK                  0x001C  /* BYPRMIXVOL - [4:2] */
#define WM8983_BYPRMIXVOL_SHIFT                      2  /* BYPRMIXVOL - [4:2] */
#define WM8983_BYPRMIXVOL_WIDTH                      3  /* BYPRMIXVOL - [4:2] */
#define WM8983_BYPR2RMIX                        0x0002  /* BYPR2RMIX */
#define WM8983_BYPR2RMIX_MASK                   0x0002  /* BYPR2RMIX */
#define WM8983_BYPR2RMIX_SHIFT                       1  /* BYPR2RMIX */
#define WM8983_BYPR2RMIX_WIDTH                       1  /* BYPR2RMIX */
#define WM8983_DACR2RMIX                        0x0001  /* DACR2RMIX */
#define WM8983_DACR2RMIX_MASK                   0x0001  /* DACR2RMIX */
#define WM8983_DACR2RMIX_SHIFT                       0  /* DACR2RMIX */
#define WM8983_DACR2RMIX_WIDTH                       1  /* DACR2RMIX */

/*
 * R52 (0x34) - LOUT1 (HP) volume ctrl
 */
#define WM8983_OUT1VU                           0x0100  /* OUT1VU */
#define WM8983_OUT1VU_MASK                      0x0100  /* OUT1VU */
#define WM8983_OUT1VU_SHIFT                          8  /* OUT1VU */
#define WM8983_OUT1VU_WIDTH                          1  /* OUT1VU */
#define WM8983_LOUT1ZC                          0x0080  /* LOUT1ZC */
#define WM8983_LOUT1ZC_MASK                     0x0080  /* LOUT1ZC */
#define WM8983_LOUT1ZC_SHIFT                         7  /* LOUT1ZC */
#define WM8983_LOUT1ZC_WIDTH                         1  /* LOUT1ZC */
#define WM8983_LOUT1MUTE                        0x0040  /* LOUT1MUTE */
#define WM8983_LOUT1MUTE_MASK                   0x0040  /* LOUT1MUTE */
#define WM8983_LOUT1MUTE_SHIFT                       6  /* LOUT1MUTE */
#define WM8983_LOUT1MUTE_WIDTH                       1  /* LOUT1MUTE */
#define WM8983_LOUT1VOL_MASK                    0x003F  /* LOUT1VOL - [5:0] */
#define WM8983_LOUT1VOL_SHIFT                        0  /* LOUT1VOL - [5:0] */
#define WM8983_LOUT1VOL_WIDTH                        6  /* LOUT1VOL - [5:0] */

/*
 * R53 (0x35) - ROUT1 (HP) volume ctrl
 */
#define WM8983_OUT1VU                           0x0100  /* OUT1VU */
#define WM8983_OUT1VU_MASK                      0x0100  /* OUT1VU */
#define WM8983_OUT1VU_SHIFT                          8  /* OUT1VU */
#define WM8983_OUT1VU_WIDTH                          1  /* OUT1VU */
#define WM8983_ROUT1ZC                          0x0080  /* ROUT1ZC */
#define WM8983_ROUT1ZC_MASK                     0x0080  /* ROUT1ZC */
#define WM8983_ROUT1ZC_SHIFT                         7  /* ROUT1ZC */
#define WM8983_ROUT1ZC_WIDTH                         1  /* ROUT1ZC */
#define WM8983_ROUT1MUTE                        0x0040  /* ROUT1MUTE */
#define WM8983_ROUT1MUTE_MASK                   0x0040  /* ROUT1MUTE */
#define WM8983_ROUT1MUTE_SHIFT                       6  /* ROUT1MUTE */
#define WM8983_ROUT1MUTE_WIDTH                       1  /* ROUT1MUTE */
#define WM8983_ROUT1VOL_MASK                    0x003F  /* ROUT1VOL - [5:0] */
#define WM8983_ROUT1VOL_SHIFT                        0  /* ROUT1VOL - [5:0] */
#define WM8983_ROUT1VOL_WIDTH                        6  /* ROUT1VOL - [5:0] */

/*
 * R54 (0x36) - LOUT2 (SPK) volume ctrl
 */
#define WM8983_OUT2VU                           0x0100  /* OUT2VU */
#define WM8983_OUT2VU_MASK                      0x0100  /* OUT2VU */
#define WM8983_OUT2VU_SHIFT                          8  /* OUT2VU */
#define WM8983_OUT2VU_WIDTH                          1  /* OUT2VU */
#define WM8983_LOUT2ZC                          0x0080  /* LOUT2ZC */
#define WM8983_LOUT2ZC_MASK                     0x0080  /* LOUT2ZC */
#define WM8983_LOUT2ZC_SHIFT                         7  /* LOUT2ZC */
#define WM8983_LOUT2ZC_WIDTH                         1  /* LOUT2ZC */
#define WM8983_LOUT2MUTE                        0x0040  /* LOUT2MUTE */
#define WM8983_LOUT2MUTE_MASK                   0x0040  /* LOUT2MUTE */
#define WM8983_LOUT2MUTE_SHIFT                       6  /* LOUT2MUTE */
#define WM8983_LOUT2MUTE_WIDTH                       1  /* LOUT2MUTE */
#define WM8983_LOUT2VOL_MASK                    0x003F  /* LOUT2VOL - [5:0] */
#define WM8983_LOUT2VOL_SHIFT                        0  /* LOUT2VOL - [5:0] */
#define WM8983_LOUT2VOL_WIDTH                        6  /* LOUT2VOL - [5:0] */

/*
 * R55 (0x37) - ROUT2 (SPK) volume ctrl
 */
#define WM8983_OUT2VU                           0x0100  /* OUT2VU */
#define WM8983_OUT2VU_MASK                      0x0100  /* OUT2VU */
#define WM8983_OUT2VU_SHIFT                          8  /* OUT2VU */
#define WM8983_OUT2VU_WIDTH                          1  /* OUT2VU */
#define WM8983_ROUT2ZC                          0x0080  /* ROUT2ZC */
#define WM8983_ROUT2ZC_MASK                     0x0080  /* ROUT2ZC */
#define WM8983_ROUT2ZC_SHIFT                         7  /* ROUT2ZC */
#define WM8983_ROUT2ZC_WIDTH                         1  /* ROUT2ZC */
#define WM8983_ROUT2MUTE                        0x0040  /* ROUT2MUTE */
#define WM8983_ROUT2MUTE_MASK                   0x0040  /* ROUT2MUTE */
#define WM8983_ROUT2MUTE_SHIFT                       6  /* ROUT2MUTE */
#define WM8983_ROUT2MUTE_WIDTH                       1  /* ROUT2MUTE */
#define WM8983_ROUT2VOL_MASK                    0x003F  /* ROUT2VOL - [5:0] */
#define WM8983_ROUT2VOL_SHIFT                        0  /* ROUT2VOL - [5:0] */
#define WM8983_ROUT2VOL_WIDTH                        6  /* ROUT2VOL - [5:0] */

/*
 * R56 (0x38) - OUT3 mixer ctrl
 */
#define WM8983_OUT3MUTE                         0x0040  /* OUT3MUTE */
#define WM8983_OUT3MUTE_MASK                    0x0040  /* OUT3MUTE */
#define WM8983_OUT3MUTE_SHIFT                        6  /* OUT3MUTE */
#define WM8983_OUT3MUTE_WIDTH                        1  /* OUT3MUTE */
#define WM8983_OUT4_2OUT3                       0x0008  /* OUT4_2OUT3 */
#define WM8983_OUT4_2OUT3_MASK                  0x0008  /* OUT4_2OUT3 */
#define WM8983_OUT4_2OUT3_SHIFT                      3  /* OUT4_2OUT3 */
#define WM8983_OUT4_2OUT3_WIDTH                      1  /* OUT4_2OUT3 */
#define WM8983_BYPL2OUT3                        0x0004  /* BYPL2OUT3 */
#define WM8983_BYPL2OUT3_MASK                   0x0004  /* BYPL2OUT3 */
#define WM8983_BYPL2OUT3_SHIFT                       2  /* BYPL2OUT3 */
#define WM8983_BYPL2OUT3_WIDTH                       1  /* BYPL2OUT3 */
#define WM8983_LMIX2OUT3                        0x0002  /* LMIX2OUT3 */
#define WM8983_LMIX2OUT3_MASK                   0x0002  /* LMIX2OUT3 */
#define WM8983_LMIX2OUT3_SHIFT                       1  /* LMIX2OUT3 */
#define WM8983_LMIX2OUT3_WIDTH                       1  /* LMIX2OUT3 */
#define WM8983_LDAC2OUT3                        0x0001  /* LDAC2OUT3 */
#define WM8983_LDAC2OUT3_MASK                   0x0001  /* LDAC2OUT3 */
#define WM8983_LDAC2OUT3_SHIFT                       0  /* LDAC2OUT3 */
#define WM8983_LDAC2OUT3_WIDTH                       1  /* LDAC2OUT3 */

/*
 * R57 (0x39) - OUT4 (MONO) mix ctrl
 */
#define WM8983_OUT3_2OUT4                       0x0080  /* OUT3_2OUT4 */
#define WM8983_OUT3_2OUT4_MASK                  0x0080  /* OUT3_2OUT4 */
#define WM8983_OUT3_2OUT4_SHIFT                      7  /* OUT3_2OUT4 */
#define WM8983_OUT3_2OUT4_WIDTH                      1  /* OUT3_2OUT4 */
#define WM8983_OUT4MUTE                         0x0040  /* OUT4MUTE */
#define WM8983_OUT4MUTE_MASK                    0x0040  /* OUT4MUTE */
#define WM8983_OUT4MUTE_SHIFT                        6  /* OUT4MUTE */
#define WM8983_OUT4MUTE_WIDTH                        1  /* OUT4MUTE */
#define WM8983_OUT4ATTN                         0x0020  /* OUT4ATTN */
#define WM8983_OUT4ATTN_MASK                    0x0020  /* OUT4ATTN */
#define WM8983_OUT4ATTN_SHIFT                        5  /* OUT4ATTN */
#define WM8983_OUT4ATTN_WIDTH                        1  /* OUT4ATTN */
#define WM8983_LMIX2OUT4                        0x0010  /* LMIX2OUT4 */
#define WM8983_LMIX2OUT4_MASK                   0x0010  /* LMIX2OUT4 */
#define WM8983_LMIX2OUT4_SHIFT                       4  /* LMIX2OUT4 */
#define WM8983_LMIX2OUT4_WIDTH                       1  /* LMIX2OUT4 */
#define WM8983_LDAC2OUT4                        0x0008  /* LDAC2OUT4 */
#define WM8983_LDAC2OUT4_MASK                   0x0008  /* LDAC2OUT4 */
#define WM8983_LDAC2OUT4_SHIFT                       3  /* LDAC2OUT4 */
#define WM8983_LDAC2OUT4_WIDTH                       1  /* LDAC2OUT4 */
#define WM8983_BYPR2OUT4                        0x0004  /* BYPR2OUT4 */
#define WM8983_BYPR2OUT4_MASK                   0x0004  /* BYPR2OUT4 */
#define WM8983_BYPR2OUT4_SHIFT                       2  /* BYPR2OUT4 */
#define WM8983_BYPR2OUT4_WIDTH                       1  /* BYPR2OUT4 */
#define WM8983_RMIX2OUT4                        0x0002  /* RMIX2OUT4 */
#define WM8983_RMIX2OUT4_MASK                   0x0002  /* RMIX2OUT4 */
#define WM8983_RMIX2OUT4_SHIFT                       1  /* RMIX2OUT4 */
#define WM8983_RMIX2OUT4_WIDTH                       1  /* RMIX2OUT4 */
#define WM8983_RDAC2OUT4                        0x0001  /* RDAC2OUT4 */
#define WM8983_RDAC2OUT4_MASK                   0x0001  /* RDAC2OUT4 */
#define WM8983_RDAC2OUT4_SHIFT                       0  /* RDAC2OUT4 */
#define WM8983_RDAC2OUT4_WIDTH                       1  /* RDAC2OUT4 */

/*
 * R61 (0x3D) - BIAS CTRL
 */
#define WM8983_BIASCUT                          0x0100  /* BIASCUT */
#define WM8983_BIASCUT_MASK                     0x0100  /* BIASCUT */
#define WM8983_BIASCUT_SHIFT                         8  /* BIASCUT */
#define WM8983_BIASCUT_WIDTH                         1  /* BIASCUT */
#define WM8983_HALFIPBIAS                       0x0080  /* HALFIPBIAS */
#define WM8983_HALFIPBIAS_MASK                  0x0080  /* HALFIPBIAS */
#define WM8983_HALFIPBIAS_SHIFT                      7  /* HALFIPBIAS */
#define WM8983_HALFIPBIAS_WIDTH                      1  /* HALFIPBIAS */
#define WM8983_VBBIASTST_MASK                   0x0060  /* VBBIASTST - [6:5] */
#define WM8983_VBBIASTST_SHIFT                       5  /* VBBIASTST - [6:5] */
#define WM8983_VBBIASTST_WIDTH                       2  /* VBBIASTST - [6:5] */
#define WM8983_BUFBIAS_MASK                     0x0018  /* BUFBIAS - [4:3] */
#define WM8983_BUFBIAS_SHIFT                         3  /* BUFBIAS - [4:3] */
#define WM8983_BUFBIAS_WIDTH                         2  /* BUFBIAS - [4:3] */
#define WM8983_ADCBIAS_MASK                     0x0006  /* ADCBIAS - [2:1] */
#define WM8983_ADCBIAS_SHIFT                         1  /* ADCBIAS - [2:1] */
#define WM8983_ADCBIAS_WIDTH                         2  /* ADCBIAS - [2:1] */
#define WM8983_HALFOPBIAS                       0x0001  /* HALFOPBIAS */
#define WM8983_HALFOPBIAS_MASK                  0x0001  /* HALFOPBIAS */
#define WM8983_HALFOPBIAS_SHIFT                      0  /* HALFOPBIAS */
#define WM8983_HALFOPBIAS_WIDTH                      1  /* HALFOPBIAS */

enum clk_src {
	WM8983_CLKSRC_MCLK,
	WM8983_CLKSRC_PLL
};

#endif /* _WM8983_H */
