/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * wm8985.h  --  WM8985 ASoC driver
 *
 * Copyright 2010 Wolfson Microelectronics plc
 *
 * Author: Dimitris Papastamos <dp@opensource.wolfsonmicro.com>
 */

#ifndef _WM8985_H
#define _WM8985_H

#define WM8985_SOFTWARE_RESET                   0x00
#define WM8985_POWER_MANAGEMENT_1               0x01
#define WM8985_POWER_MANAGEMENT_2               0x02
#define WM8985_POWER_MANAGEMENT_3               0x03
#define WM8985_AUDIO_INTERFACE                  0x04
#define WM8985_COMPANDING_CONTROL               0x05
#define WM8985_CLOCK_GEN_CONTROL                0x06
#define WM8985_ADDITIONAL_CONTROL               0x07
#define WM8985_GPIO_CONTROL                     0x08
#define WM8985_JACK_DETECT_CONTROL_1            0x09
#define WM8985_DAC_CONTROL                      0x0A
#define WM8985_LEFT_DAC_DIGITAL_VOL             0x0B
#define WM8985_RIGHT_DAC_DIGITAL_VOL            0x0C
#define WM8985_JACK_DETECT_CONTROL_2            0x0D
#define WM8985_ADC_CONTROL                      0x0E
#define WM8985_LEFT_ADC_DIGITAL_VOL             0x0F
#define WM8985_RIGHT_ADC_DIGITAL_VOL            0x10
#define WM8985_EQ1_LOW_SHELF                    0x12
#define WM8985_EQ2_PEAK_1                       0x13
#define WM8985_EQ3_PEAK_2                       0x14
#define WM8985_EQ4_PEAK_3                       0x15
#define WM8985_EQ5_HIGH_SHELF                   0x16
#define WM8985_DAC_LIMITER_1                    0x18
#define WM8985_DAC_LIMITER_2                    0x19
#define WM8985_NOTCH_FILTER_1                   0x1B
#define WM8985_NOTCH_FILTER_2                   0x1C
#define WM8985_NOTCH_FILTER_3                   0x1D
#define WM8985_NOTCH_FILTER_4                   0x1E
#define WM8985_ALC_CONTROL_1                    0x20
#define WM8985_ALC_CONTROL_2                    0x21
#define WM8985_ALC_CONTROL_3                    0x22
#define WM8985_NOISE_GATE                       0x23
#define WM8985_PLL_N                            0x24
#define WM8985_PLL_K_1                          0x25
#define WM8985_PLL_K_2                          0x26
#define WM8985_PLL_K_3                          0x27
#define WM8985_3D_CONTROL                       0x29
#define WM8985_OUT4_TO_ADC                      0x2A
#define WM8985_BEEP_CONTROL                     0x2B
#define WM8985_INPUT_CTRL                       0x2C
#define WM8985_LEFT_INP_PGA_GAIN_CTRL           0x2D
#define WM8985_RIGHT_INP_PGA_GAIN_CTRL          0x2E
#define WM8985_LEFT_ADC_BOOST_CTRL              0x2F
#define WM8985_RIGHT_ADC_BOOST_CTRL             0x30
#define WM8985_OUTPUT_CTRL0                     0x31
#define WM8985_LEFT_MIXER_CTRL                  0x32
#define WM8985_RIGHT_MIXER_CTRL                 0x33
#define WM8985_LOUT1_HP_VOLUME_CTRL             0x34
#define WM8985_ROUT1_HP_VOLUME_CTRL             0x35
#define WM8985_LOUT2_SPK_VOLUME_CTRL            0x36
#define WM8985_ROUT2_SPK_VOLUME_CTRL            0x37
#define WM8985_OUT3_MIXER_CTRL                  0x38
#define WM8985_OUT4_MONO_MIX_CTRL               0x39
#define WM8985_OUTPUT_CTRL1                     0x3C
#define WM8985_BIAS_CTRL                        0x3D

#define WM8985_REGISTER_COUNT                   59
#define WM8985_MAX_REGISTER                     0x3F

/*
 * Field Definitions.
 */

/*
 * R0 (0x00) - Software Reset
 */
#define WM8985_SOFTWARE_RESET_MASK              0x01FF  /* SOFTWARE_RESET - [8:0] */
#define WM8985_SOFTWARE_RESET_SHIFT                  0  /* SOFTWARE_RESET - [8:0] */
#define WM8985_SOFTWARE_RESET_WIDTH                  9  /* SOFTWARE_RESET - [8:0] */

/*
 * R1 (0x01) - Power management 1
 */
#define WM8985_OUT4MIXEN                        0x0080  /* OUT4MIXEN */
#define WM8985_OUT4MIXEN_MASK                   0x0080  /* OUT4MIXEN */
#define WM8985_OUT4MIXEN_SHIFT                       7  /* OUT4MIXEN */
#define WM8985_OUT4MIXEN_WIDTH                       1  /* OUT4MIXEN */
#define WM8985_OUT3MIXEN                        0x0040  /* OUT3MIXEN */
#define WM8985_OUT3MIXEN_MASK                   0x0040  /* OUT3MIXEN */
#define WM8985_OUT3MIXEN_SHIFT                       6  /* OUT3MIXEN */
#define WM8985_OUT3MIXEN_WIDTH                       1  /* OUT3MIXEN */
#define WM8985_PLLEN                            0x0020  /* PLLEN */
#define WM8985_PLLEN_MASK                       0x0020  /* PLLEN */
#define WM8985_PLLEN_SHIFT                           5  /* PLLEN */
#define WM8985_PLLEN_WIDTH                           1  /* PLLEN */
#define WM8985_MICBEN                           0x0010  /* MICBEN */
#define WM8985_MICBEN_MASK                      0x0010  /* MICBEN */
#define WM8985_MICBEN_SHIFT                          4  /* MICBEN */
#define WM8985_MICBEN_WIDTH                          1  /* MICBEN */
#define WM8985_BIASEN                           0x0008  /* BIASEN */
#define WM8985_BIASEN_MASK                      0x0008  /* BIASEN */
#define WM8985_BIASEN_SHIFT                          3  /* BIASEN */
#define WM8985_BIASEN_WIDTH                          1  /* BIASEN */
#define WM8985_BUFIOEN                          0x0004  /* BUFIOEN */
#define WM8985_BUFIOEN_MASK                     0x0004  /* BUFIOEN */
#define WM8985_BUFIOEN_SHIFT                         2  /* BUFIOEN */
#define WM8985_BUFIOEN_WIDTH                         1  /* BUFIOEN */
#define WM8985_VMIDSEL                          0x0003  /* VMIDSEL */
#define WM8985_VMIDSEL_MASK                     0x0003  /* VMIDSEL - [1:0] */
#define WM8985_VMIDSEL_SHIFT                         0  /* VMIDSEL - [1:0] */
#define WM8985_VMIDSEL_WIDTH                         2  /* VMIDSEL - [1:0] */

/*
 * R2 (0x02) - Power management 2
 */
#define WM8985_ROUT1EN                          0x0100  /* ROUT1EN */
#define WM8985_ROUT1EN_MASK                     0x0100  /* ROUT1EN */
#define WM8985_ROUT1EN_SHIFT                         8  /* ROUT1EN */
#define WM8985_ROUT1EN_WIDTH                         1  /* ROUT1EN */
#define WM8985_LOUT1EN                          0x0080  /* LOUT1EN */
#define WM8985_LOUT1EN_MASK                     0x0080  /* LOUT1EN */
#define WM8985_LOUT1EN_SHIFT                         7  /* LOUT1EN */
#define WM8985_LOUT1EN_WIDTH                         1  /* LOUT1EN */
#define WM8985_SLEEP                            0x0040  /* SLEEP */
#define WM8985_SLEEP_MASK                       0x0040  /* SLEEP */
#define WM8985_SLEEP_SHIFT                           6  /* SLEEP */
#define WM8985_SLEEP_WIDTH                           1  /* SLEEP */
#define WM8985_BOOSTENR                         0x0020  /* BOOSTENR */
#define WM8985_BOOSTENR_MASK                    0x0020  /* BOOSTENR */
#define WM8985_BOOSTENR_SHIFT                        5  /* BOOSTENR */
#define WM8985_BOOSTENR_WIDTH                        1  /* BOOSTENR */
#define WM8985_BOOSTENL                         0x0010  /* BOOSTENL */
#define WM8985_BOOSTENL_MASK                    0x0010  /* BOOSTENL */
#define WM8985_BOOSTENL_SHIFT                        4  /* BOOSTENL */
#define WM8985_BOOSTENL_WIDTH                        1  /* BOOSTENL */
#define WM8985_INPGAENR                         0x0008  /* INPGAENR */
#define WM8985_INPGAENR_MASK                    0x0008  /* INPGAENR */
#define WM8985_INPGAENR_SHIFT                        3  /* INPGAENR */
#define WM8985_INPGAENR_WIDTH                        1  /* INPGAENR */
#define WM8985_INPPGAENL                        0x0004  /* INPPGAENL */
#define WM8985_INPPGAENL_MASK                   0x0004  /* INPPGAENL */
#define WM8985_INPPGAENL_SHIFT                       2  /* INPPGAENL */
#define WM8985_INPPGAENL_WIDTH                       1  /* INPPGAENL */
#define WM8985_ADCENR                           0x0002  /* ADCENR */
#define WM8985_ADCENR_MASK                      0x0002  /* ADCENR */
#define WM8985_ADCENR_SHIFT                          1  /* ADCENR */
#define WM8985_ADCENR_WIDTH                          1  /* ADCENR */
#define WM8985_ADCENL                           0x0001  /* ADCENL */
#define WM8985_ADCENL_MASK                      0x0001  /* ADCENL */
#define WM8985_ADCENL_SHIFT                          0  /* ADCENL */
#define WM8985_ADCENL_WIDTH                          1  /* ADCENL */

/*
 * R3 (0x03) - Power management 3
 */
#define WM8985_OUT4EN                           0x0100  /* OUT4EN */
#define WM8985_OUT4EN_MASK                      0x0100  /* OUT4EN */
#define WM8985_OUT4EN_SHIFT                          8  /* OUT4EN */
#define WM8985_OUT4EN_WIDTH                          1  /* OUT4EN */
#define WM8985_OUT3EN                           0x0080  /* OUT3EN */
#define WM8985_OUT3EN_MASK                      0x0080  /* OUT3EN */
#define WM8985_OUT3EN_SHIFT                          7  /* OUT3EN */
#define WM8985_OUT3EN_WIDTH                          1  /* OUT3EN */
#define WM8985_ROUT2EN                          0x0040  /* ROUT2EN */
#define WM8985_ROUT2EN_MASK                     0x0040  /* ROUT2EN */
#define WM8985_ROUT2EN_SHIFT                         6  /* ROUT2EN */
#define WM8985_ROUT2EN_WIDTH                         1  /* ROUT2EN */
#define WM8985_LOUT2EN                          0x0020  /* LOUT2EN */
#define WM8985_LOUT2EN_MASK                     0x0020  /* LOUT2EN */
#define WM8985_LOUT2EN_SHIFT                         5  /* LOUT2EN */
#define WM8985_LOUT2EN_WIDTH                         1  /* LOUT2EN */
#define WM8985_RMIXEN                           0x0008  /* RMIXEN */
#define WM8985_RMIXEN_MASK                      0x0008  /* RMIXEN */
#define WM8985_RMIXEN_SHIFT                          3  /* RMIXEN */
#define WM8985_RMIXEN_WIDTH                          1  /* RMIXEN */
#define WM8985_LMIXEN                           0x0004  /* LMIXEN */
#define WM8985_LMIXEN_MASK                      0x0004  /* LMIXEN */
#define WM8985_LMIXEN_SHIFT                          2  /* LMIXEN */
#define WM8985_LMIXEN_WIDTH                          1  /* LMIXEN */
#define WM8985_DACENR                           0x0002  /* DACENR */
#define WM8985_DACENR_MASK                      0x0002  /* DACENR */
#define WM8985_DACENR_SHIFT                          1  /* DACENR */
#define WM8985_DACENR_WIDTH                          1  /* DACENR */
#define WM8985_DACENL                           0x0001  /* DACENL */
#define WM8985_DACENL_MASK                      0x0001  /* DACENL */
#define WM8985_DACENL_SHIFT                          0  /* DACENL */
#define WM8985_DACENL_WIDTH                          1  /* DACENL */

/*
 * R4 (0x04) - Audio Interface
 */
#define WM8985_BCP                              0x0100  /* BCP */
#define WM8985_BCP_MASK                         0x0100  /* BCP */
#define WM8985_BCP_SHIFT                             8  /* BCP */
#define WM8985_BCP_WIDTH                             1  /* BCP */
#define WM8985_LRP                              0x0080  /* LRP */
#define WM8985_LRP_MASK                         0x0080  /* LRP */
#define WM8985_LRP_SHIFT                             7  /* LRP */
#define WM8985_LRP_WIDTH                             1  /* LRP */
#define WM8985_WL_MASK                          0x0060  /* WL - [6:5] */
#define WM8985_WL_SHIFT                              5  /* WL - [6:5] */
#define WM8985_WL_WIDTH                              2  /* WL - [6:5] */
#define WM8985_FMT_MASK                         0x0018  /* FMT - [4:3] */
#define WM8985_FMT_SHIFT                             3  /* FMT - [4:3] */
#define WM8985_FMT_WIDTH                             2  /* FMT - [4:3] */
#define WM8985_DLRSWAP                          0x0004  /* DLRSWAP */
#define WM8985_DLRSWAP_MASK                     0x0004  /* DLRSWAP */
#define WM8985_DLRSWAP_SHIFT                         2  /* DLRSWAP */
#define WM8985_DLRSWAP_WIDTH                         1  /* DLRSWAP */
#define WM8985_ALRSWAP                          0x0002  /* ALRSWAP */
#define WM8985_ALRSWAP_MASK                     0x0002  /* ALRSWAP */
#define WM8985_ALRSWAP_SHIFT                         1  /* ALRSWAP */
#define WM8985_ALRSWAP_WIDTH                         1  /* ALRSWAP */
#define WM8985_MONO                             0x0001  /* MONO */
#define WM8985_MONO_MASK                        0x0001  /* MONO */
#define WM8985_MONO_SHIFT                            0  /* MONO */
#define WM8985_MONO_WIDTH                            1  /* MONO */

/*
 * R5 (0x05) - Companding control
 */
#define WM8985_WL8                              0x0020  /* WL8 */
#define WM8985_WL8_MASK                         0x0020  /* WL8 */
#define WM8985_WL8_SHIFT                             5  /* WL8 */
#define WM8985_WL8_WIDTH                             1  /* WL8 */
#define WM8985_DAC_COMP_MASK                    0x0018  /* DAC_COMP - [4:3] */
#define WM8985_DAC_COMP_SHIFT                        3  /* DAC_COMP - [4:3] */
#define WM8985_DAC_COMP_WIDTH                        2  /* DAC_COMP - [4:3] */
#define WM8985_ADC_COMP_MASK                    0x0006  /* ADC_COMP - [2:1] */
#define WM8985_ADC_COMP_SHIFT                        1  /* ADC_COMP - [2:1] */
#define WM8985_ADC_COMP_WIDTH                        2  /* ADC_COMP - [2:1] */
#define WM8985_LOOPBACK                         0x0001  /* LOOPBACK */
#define WM8985_LOOPBACK_MASK                    0x0001  /* LOOPBACK */
#define WM8985_LOOPBACK_SHIFT                        0  /* LOOPBACK */
#define WM8985_LOOPBACK_WIDTH                        1  /* LOOPBACK */

/*
 * R6 (0x06) - Clock Gen control
 */
#define WM8985_CLKSEL                           0x0100  /* CLKSEL */
#define WM8985_CLKSEL_MASK                      0x0100  /* CLKSEL */
#define WM8985_CLKSEL_SHIFT                          8  /* CLKSEL */
#define WM8985_CLKSEL_WIDTH                          1  /* CLKSEL */
#define WM8985_MCLKDIV_MASK                     0x00E0  /* MCLKDIV - [7:5] */
#define WM8985_MCLKDIV_SHIFT                         5  /* MCLKDIV - [7:5] */
#define WM8985_MCLKDIV_WIDTH                         3  /* MCLKDIV - [7:5] */
#define WM8985_BCLKDIV_MASK                     0x001C  /* BCLKDIV - [4:2] */
#define WM8985_BCLKDIV_SHIFT                         2  /* BCLKDIV - [4:2] */
#define WM8985_BCLKDIV_WIDTH                         3  /* BCLKDIV - [4:2] */
#define WM8985_MS                               0x0001  /* MS */
#define WM8985_MS_MASK                          0x0001  /* MS */
#define WM8985_MS_SHIFT                              0  /* MS */
#define WM8985_MS_WIDTH                              1  /* MS */

/*
 * R7 (0x07) - Additional control
 */
#define WM8985_M128ENB                          0x0100  /* M128ENB */
#define WM8985_M128ENB_MASK                     0x0100  /* M128ENB */
#define WM8985_M128ENB_SHIFT                         8  /* M128ENB */
#define WM8985_M128ENB_WIDTH                         1  /* M128ENB */
#define WM8985_DCLKDIV_MASK                     0x00F0  /* DCLKDIV - [7:4] */
#define WM8985_DCLKDIV_SHIFT                         4  /* DCLKDIV - [7:4] */
#define WM8985_DCLKDIV_WIDTH                         4  /* DCLKDIV - [7:4] */
#define WM8985_SR_MASK                          0x000E  /* SR - [3:1] */
#define WM8985_SR_SHIFT                              1  /* SR - [3:1] */
#define WM8985_SR_WIDTH                              3  /* SR - [3:1] */
#define WM8985_SLOWCLKEN                        0x0001  /* SLOWCLKEN */
#define WM8985_SLOWCLKEN_MASK                   0x0001  /* SLOWCLKEN */
#define WM8985_SLOWCLKEN_SHIFT                       0  /* SLOWCLKEN */
#define WM8985_SLOWCLKEN_WIDTH                       1  /* SLOWCLKEN */

/*
 * R8 (0x08) - GPIO Control
 */
#define WM8985_GPIO1GP                          0x0100  /* GPIO1GP */
#define WM8985_GPIO1GP_MASK                     0x0100  /* GPIO1GP */
#define WM8985_GPIO1GP_SHIFT                         8  /* GPIO1GP */
#define WM8985_GPIO1GP_WIDTH                         1  /* GPIO1GP */
#define WM8985_GPIO1GPU                         0x0080  /* GPIO1GPU */
#define WM8985_GPIO1GPU_MASK                    0x0080  /* GPIO1GPU */
#define WM8985_GPIO1GPU_SHIFT                        7  /* GPIO1GPU */
#define WM8985_GPIO1GPU_WIDTH                        1  /* GPIO1GPU */
#define WM8985_GPIO1GPD                         0x0040  /* GPIO1GPD */
#define WM8985_GPIO1GPD_MASK                    0x0040  /* GPIO1GPD */
#define WM8985_GPIO1GPD_SHIFT                        6  /* GPIO1GPD */
#define WM8985_GPIO1GPD_WIDTH                        1  /* GPIO1GPD */
#define WM8758_OPCLKDIV_MASK                    0x0030  /* OPCLKDIV - [1:0] */
#define WM8758_OPCLKDIV_SHIFT                        4  /* OPCLKDIV - [1:0] */
#define WM8758_OPCLKDIV_WIDTH                        2  /* OPCLKDIV - [1:0] */
#define WM8985_GPIO1POL                         0x0008  /* GPIO1POL */
#define WM8985_GPIO1POL_MASK                    0x0008  /* GPIO1POL */
#define WM8985_GPIO1POL_SHIFT                        3  /* GPIO1POL */
#define WM8985_GPIO1POL_WIDTH                        1  /* GPIO1POL */
#define WM8985_GPIO1SEL_MASK                    0x0007  /* GPIO1SEL - [2:0] */
#define WM8985_GPIO1SEL_SHIFT                        0  /* GPIO1SEL - [2:0] */
#define WM8985_GPIO1SEL_WIDTH                        3  /* GPIO1SEL - [2:0] */

/*
 * R9 (0x09) - Jack Detect Control 1
 */
#define WM8758_JD_VMID1_MASK                    0x0100  /* JD_VMID1 */
#define WM8758_JD_VMID1_SHIFT                        8  /* JD_VMID1 */
#define WM8758_JD_VMID1_WIDTH                        1  /* JD_VMID1 */
#define WM8758_JD_VMID0_MASK                    0x0080  /* JD_VMID0 */
#define WM8758_JD_VMID0_SHIFT                        7  /* JD_VMID0 */
#define WM8758_JD_VMID0_WIDTH                        1  /* JD_VMID0 */
#define WM8985_JD_EN                            0x0040  /* JD_EN */
#define WM8985_JD_EN_MASK                       0x0040  /* JD_EN */
#define WM8985_JD_EN_SHIFT                           6  /* JD_EN */
#define WM8985_JD_EN_WIDTH                           1  /* JD_EN */
#define WM8985_JD_SEL_MASK                      0x0030  /* JD_SEL - [5:4] */
#define WM8985_JD_SEL_SHIFT                          4  /* JD_SEL - [5:4] */
#define WM8985_JD_SEL_WIDTH                          2  /* JD_SEL - [5:4] */

/*
 * R10 (0x0A) - DAC Control
 */
#define WM8985_SOFTMUTE                         0x0040  /* SOFTMUTE */
#define WM8985_SOFTMUTE_MASK                    0x0040  /* SOFTMUTE */
#define WM8985_SOFTMUTE_SHIFT                        6  /* SOFTMUTE */
#define WM8985_SOFTMUTE_WIDTH                        1  /* SOFTMUTE */
#define WM8985_DACOSR128                        0x0008  /* DACOSR128 */
#define WM8985_DACOSR128_MASK                   0x0008  /* DACOSR128 */
#define WM8985_DACOSR128_SHIFT                       3  /* DACOSR128 */
#define WM8985_DACOSR128_WIDTH                       1  /* DACOSR128 */
#define WM8985_AMUTE                            0x0004  /* AMUTE */
#define WM8985_AMUTE_MASK                       0x0004  /* AMUTE */
#define WM8985_AMUTE_SHIFT                           2  /* AMUTE */
#define WM8985_AMUTE_WIDTH                           1  /* AMUTE */
#define WM8985_DACPOLR                          0x0002  /* DACPOLR */
#define WM8985_DACPOLR_MASK                     0x0002  /* DACPOLR */
#define WM8985_DACPOLR_SHIFT                         1  /* DACPOLR */
#define WM8985_DACPOLR_WIDTH                         1  /* DACPOLR */
#define WM8985_DACPOLL                          0x0001  /* DACPOLL */
#define WM8985_DACPOLL_MASK                     0x0001  /* DACPOLL */
#define WM8985_DACPOLL_SHIFT                         0  /* DACPOLL */
#define WM8985_DACPOLL_WIDTH                         1  /* DACPOLL */

/*
 * R11 (0x0B) - Left DAC digital Vol
 */
#define WM8985_DACVU                            0x0100  /* DACVU */
#define WM8985_DACVU_MASK                       0x0100  /* DACVU */
#define WM8985_DACVU_SHIFT                           8  /* DACVU */
#define WM8985_DACVU_WIDTH                           1  /* DACVU */
#define WM8985_DACVOLL_MASK                     0x00FF  /* DACVOLL - [7:0] */
#define WM8985_DACVOLL_SHIFT                         0  /* DACVOLL - [7:0] */
#define WM8985_DACVOLL_WIDTH                         8  /* DACVOLL - [7:0] */

/*
 * R12 (0x0C) - Right DAC digital vol
 */
#define WM8985_DACVU                            0x0100  /* DACVU */
#define WM8985_DACVU_MASK                       0x0100  /* DACVU */
#define WM8985_DACVU_SHIFT                           8  /* DACVU */
#define WM8985_DACVU_WIDTH                           1  /* DACVU */
#define WM8985_DACVOLR_MASK                     0x00FF  /* DACVOLR - [7:0] */
#define WM8985_DACVOLR_SHIFT                         0  /* DACVOLR - [7:0] */
#define WM8985_DACVOLR_WIDTH                         8  /* DACVOLR - [7:0] */

/*
 * R13 (0x0D) - Jack Detect Control 2
 */
#define WM8985_JD_EN1_MASK                      0x00F0  /* JD_EN1 - [7:4] */
#define WM8985_JD_EN1_SHIFT                          4  /* JD_EN1 - [7:4] */
#define WM8985_JD_EN1_WIDTH                          4  /* JD_EN1 - [7:4] */
#define WM8985_JD_EN0_MASK                      0x000F  /* JD_EN0 - [3:0] */
#define WM8985_JD_EN0_SHIFT                          0  /* JD_EN0 - [3:0] */
#define WM8985_JD_EN0_WIDTH                          4  /* JD_EN0 - [3:0] */

/*
 * R14 (0x0E) - ADC Control
 */
#define WM8985_HPFEN                            0x0100  /* HPFEN */
#define WM8985_HPFEN_MASK                       0x0100  /* HPFEN */
#define WM8985_HPFEN_SHIFT                           8  /* HPFEN */
#define WM8985_HPFEN_WIDTH                           1  /* HPFEN */
#define WM8985_HPFAPP                           0x0080  /* HPFAPP */
#define WM8985_HPFAPP_MASK                      0x0080  /* HPFAPP */
#define WM8985_HPFAPP_SHIFT                          7  /* HPFAPP */
#define WM8985_HPFAPP_WIDTH                          1  /* HPFAPP */
#define WM8985_HPFCUT_MASK                      0x0070  /* HPFCUT - [6:4] */
#define WM8985_HPFCUT_SHIFT                          4  /* HPFCUT - [6:4] */
#define WM8985_HPFCUT_WIDTH                          3  /* HPFCUT - [6:4] */
#define WM8985_ADCOSR128                        0x0008  /* ADCOSR128 */
#define WM8985_ADCOSR128_MASK                   0x0008  /* ADCOSR128 */
#define WM8985_ADCOSR128_SHIFT                       3  /* ADCOSR128 */
#define WM8985_ADCOSR128_WIDTH                       1  /* ADCOSR128 */
#define WM8985_ADCRPOL                          0x0002  /* ADCRPOL */
#define WM8985_ADCRPOL_MASK                     0x0002  /* ADCRPOL */
#define WM8985_ADCRPOL_SHIFT                         1  /* ADCRPOL */
#define WM8985_ADCRPOL_WIDTH                         1  /* ADCRPOL */
#define WM8985_ADCLPOL                          0x0001  /* ADCLPOL */
#define WM8985_ADCLPOL_MASK                     0x0001  /* ADCLPOL */
#define WM8985_ADCLPOL_SHIFT                         0  /* ADCLPOL */
#define WM8985_ADCLPOL_WIDTH                         1  /* ADCLPOL */

/*
 * R15 (0x0F) - Left ADC Digital Vol
 */
#define WM8985_ADCVU                            0x0100  /* ADCVU */
#define WM8985_ADCVU_MASK                       0x0100  /* ADCVU */
#define WM8985_ADCVU_SHIFT                           8  /* ADCVU */
#define WM8985_ADCVU_WIDTH                           1  /* ADCVU */
#define WM8985_ADCVOLL_MASK                     0x00FF  /* ADCVOLL - [7:0] */
#define WM8985_ADCVOLL_SHIFT                         0  /* ADCVOLL - [7:0] */
#define WM8985_ADCVOLL_WIDTH                         8  /* ADCVOLL - [7:0] */

/*
 * R16 (0x10) - Right ADC Digital Vol
 */
#define WM8985_ADCVU                            0x0100  /* ADCVU */
#define WM8985_ADCVU_MASK                       0x0100  /* ADCVU */
#define WM8985_ADCVU_SHIFT                           8  /* ADCVU */
#define WM8985_ADCVU_WIDTH                           1  /* ADCVU */
#define WM8985_ADCVOLR_MASK                     0x00FF  /* ADCVOLR - [7:0] */
#define WM8985_ADCVOLR_SHIFT                         0  /* ADCVOLR - [7:0] */
#define WM8985_ADCVOLR_WIDTH                         8  /* ADCVOLR - [7:0] */

/*
 * R18 (0x12) - EQ1 - low shelf
 */
#define WM8985_EQ3DMODE                         0x0100  /* EQ3DMODE */
#define WM8985_EQ3DMODE_MASK                    0x0100  /* EQ3DMODE */
#define WM8985_EQ3DMODE_SHIFT                        8  /* EQ3DMODE */
#define WM8985_EQ3DMODE_WIDTH                        1  /* EQ3DMODE */
#define WM8985_EQ1C_MASK                        0x0060  /* EQ1C - [6:5] */
#define WM8985_EQ1C_SHIFT                            5  /* EQ1C - [6:5] */
#define WM8985_EQ1C_WIDTH                            2  /* EQ1C - [6:5] */
#define WM8985_EQ1G_MASK                        0x001F  /* EQ1G - [4:0] */
#define WM8985_EQ1G_SHIFT                            0  /* EQ1G - [4:0] */
#define WM8985_EQ1G_WIDTH                            5  /* EQ1G - [4:0] */

/*
 * R19 (0x13) - EQ2 - peak 1
 */
#define WM8985_EQ2BW                            0x0100  /* EQ2BW */
#define WM8985_EQ2BW_MASK                       0x0100  /* EQ2BW */
#define WM8985_EQ2BW_SHIFT                           8  /* EQ2BW */
#define WM8985_EQ2BW_WIDTH                           1  /* EQ2BW */
#define WM8985_EQ2C_MASK                        0x0060  /* EQ2C - [6:5] */
#define WM8985_EQ2C_SHIFT                            5  /* EQ2C - [6:5] */
#define WM8985_EQ2C_WIDTH                            2  /* EQ2C - [6:5] */
#define WM8985_EQ2G_MASK                        0x001F  /* EQ2G - [4:0] */
#define WM8985_EQ2G_SHIFT                            0  /* EQ2G - [4:0] */
#define WM8985_EQ2G_WIDTH                            5  /* EQ2G - [4:0] */

/*
 * R20 (0x14) - EQ3 - peak 2
 */
#define WM8985_EQ3BW                            0x0100  /* EQ3BW */
#define WM8985_EQ3BW_MASK                       0x0100  /* EQ3BW */
#define WM8985_EQ3BW_SHIFT                           8  /* EQ3BW */
#define WM8985_EQ3BW_WIDTH                           1  /* EQ3BW */
#define WM8985_EQ3C_MASK                        0x0060  /* EQ3C - [6:5] */
#define WM8985_EQ3C_SHIFT                            5  /* EQ3C - [6:5] */
#define WM8985_EQ3C_WIDTH                            2  /* EQ3C - [6:5] */
#define WM8985_EQ3G_MASK                        0x001F  /* EQ3G - [4:0] */
#define WM8985_EQ3G_SHIFT                            0  /* EQ3G - [4:0] */
#define WM8985_EQ3G_WIDTH                            5  /* EQ3G - [4:0] */

/*
 * R21 (0x15) - EQ4 - peak 3
 */
#define WM8985_EQ4BW                            0x0100  /* EQ4BW */
#define WM8985_EQ4BW_MASK                       0x0100  /* EQ4BW */
#define WM8985_EQ4BW_SHIFT                           8  /* EQ4BW */
#define WM8985_EQ4BW_WIDTH                           1  /* EQ4BW */
#define WM8985_EQ4C_MASK                        0x0060  /* EQ4C - [6:5] */
#define WM8985_EQ4C_SHIFT                            5  /* EQ4C - [6:5] */
#define WM8985_EQ4C_WIDTH                            2  /* EQ4C - [6:5] */
#define WM8985_EQ4G_MASK                        0x001F  /* EQ4G - [4:0] */
#define WM8985_EQ4G_SHIFT                            0  /* EQ4G - [4:0] */
#define WM8985_EQ4G_WIDTH                            5  /* EQ4G - [4:0] */

/*
 * R22 (0x16) - EQ5 - high shelf
 */
#define WM8985_EQ5C_MASK                        0x0060  /* EQ5C - [6:5] */
#define WM8985_EQ5C_SHIFT                            5  /* EQ5C - [6:5] */
#define WM8985_EQ5C_WIDTH                            2  /* EQ5C - [6:5] */
#define WM8985_EQ5G_MASK                        0x001F  /* EQ5G - [4:0] */
#define WM8985_EQ5G_SHIFT                            0  /* EQ5G - [4:0] */
#define WM8985_EQ5G_WIDTH                            5  /* EQ5G - [4:0] */

/*
 * R24 (0x18) - DAC Limiter 1
 */
#define WM8985_LIMEN                            0x0100  /* LIMEN */
#define WM8985_LIMEN_MASK                       0x0100  /* LIMEN */
#define WM8985_LIMEN_SHIFT                           8  /* LIMEN */
#define WM8985_LIMEN_WIDTH                           1  /* LIMEN */
#define WM8985_LIMDCY_MASK                      0x00F0  /* LIMDCY - [7:4] */
#define WM8985_LIMDCY_SHIFT                          4  /* LIMDCY - [7:4] */
#define WM8985_LIMDCY_WIDTH                          4  /* LIMDCY - [7:4] */
#define WM8985_LIMATK_MASK                      0x000F  /* LIMATK - [3:0] */
#define WM8985_LIMATK_SHIFT                          0  /* LIMATK - [3:0] */
#define WM8985_LIMATK_WIDTH                          4  /* LIMATK - [3:0] */

/*
 * R25 (0x19) - DAC Limiter 2
 */
#define WM8985_LIMLVL_MASK                      0x0070  /* LIMLVL - [6:4] */
#define WM8985_LIMLVL_SHIFT                          4  /* LIMLVL - [6:4] */
#define WM8985_LIMLVL_WIDTH                          3  /* LIMLVL - [6:4] */
#define WM8985_LIMBOOST_MASK                    0x000F  /* LIMBOOST - [3:0] */
#define WM8985_LIMBOOST_SHIFT                        0  /* LIMBOOST - [3:0] */
#define WM8985_LIMBOOST_WIDTH                        4  /* LIMBOOST - [3:0] */

/*
 * R27 (0x1B) - Notch Filter 1
 */
#define WM8985_NFU                              0x0100  /* NFU */
#define WM8985_NFU_MASK                         0x0100  /* NFU */
#define WM8985_NFU_SHIFT                             8  /* NFU */
#define WM8985_NFU_WIDTH                             1  /* NFU */
#define WM8985_NFEN                             0x0080  /* NFEN */
#define WM8985_NFEN_MASK                        0x0080  /* NFEN */
#define WM8985_NFEN_SHIFT                            7  /* NFEN */
#define WM8985_NFEN_WIDTH                            1  /* NFEN */
#define WM8985_NFA0_13_7_MASK                   0x007F  /* NFA0(13:7) - [6:0] */
#define WM8985_NFA0_13_7_SHIFT                       0  /* NFA0(13:7) - [6:0] */
#define WM8985_NFA0_13_7_WIDTH                       7  /* NFA0(13:7) - [6:0] */

/*
 * R28 (0x1C) - Notch Filter 2
 */
#define WM8985_NFU                              0x0100  /* NFU */
#define WM8985_NFU_MASK                         0x0100  /* NFU */
#define WM8985_NFU_SHIFT                             8  /* NFU */
#define WM8985_NFU_WIDTH                             1  /* NFU */
#define WM8985_NFA0_6_0_MASK                    0x007F  /* NFA0(6:0) - [6:0] */
#define WM8985_NFA0_6_0_SHIFT                        0  /* NFA0(6:0) - [6:0] */
#define WM8985_NFA0_6_0_WIDTH                        7  /* NFA0(6:0) - [6:0] */

/*
 * R29 (0x1D) - Notch Filter 3
 */
#define WM8985_NFU                              0x0100  /* NFU */
#define WM8985_NFU_MASK                         0x0100  /* NFU */
#define WM8985_NFU_SHIFT                             8  /* NFU */
#define WM8985_NFU_WIDTH                             1  /* NFU */
#define WM8985_NFA1_13_7_MASK                   0x007F  /* NFA1(13:7) - [6:0] */
#define WM8985_NFA1_13_7_SHIFT                       0  /* NFA1(13:7) - [6:0] */
#define WM8985_NFA1_13_7_WIDTH                       7  /* NFA1(13:7) - [6:0] */

/*
 * R30 (0x1E) - Notch Filter 4
 */
#define WM8985_NFU                              0x0100  /* NFU */
#define WM8985_NFU_MASK                         0x0100  /* NFU */
#define WM8985_NFU_SHIFT                             8  /* NFU */
#define WM8985_NFU_WIDTH                             1  /* NFU */
#define WM8985_NFA1_6_0_MASK                    0x007F  /* NFA1(6:0) - [6:0] */
#define WM8985_NFA1_6_0_SHIFT                        0  /* NFA1(6:0) - [6:0] */
#define WM8985_NFA1_6_0_WIDTH                        7  /* NFA1(6:0) - [6:0] */

/*
 * R32 (0x20) - ALC control 1
 */
#define WM8985_ALCSEL_MASK                      0x0180  /* ALCSEL - [8:7] */
#define WM8985_ALCSEL_SHIFT                          7  /* ALCSEL - [8:7] */
#define WM8985_ALCSEL_WIDTH                          2  /* ALCSEL - [8:7] */
#define WM8985_ALCMAX_MASK                      0x0038  /* ALCMAX - [5:3] */
#define WM8985_ALCMAX_SHIFT                          3  /* ALCMAX - [5:3] */
#define WM8985_ALCMAX_WIDTH                          3  /* ALCMAX - [5:3] */
#define WM8985_ALCMIN_MASK                      0x0007  /* ALCMIN - [2:0] */
#define WM8985_ALCMIN_SHIFT                          0  /* ALCMIN - [2:0] */
#define WM8985_ALCMIN_WIDTH                          3  /* ALCMIN - [2:0] */

/*
 * R33 (0x21) - ALC control 2
 */
#define WM8985_ALCHLD_MASK                      0x00F0  /* ALCHLD - [7:4] */
#define WM8985_ALCHLD_SHIFT                          4  /* ALCHLD - [7:4] */
#define WM8985_ALCHLD_WIDTH                          4  /* ALCHLD - [7:4] */
#define WM8985_ALCLVL_MASK                      0x000F  /* ALCLVL - [3:0] */
#define WM8985_ALCLVL_SHIFT                          0  /* ALCLVL - [3:0] */
#define WM8985_ALCLVL_WIDTH                          4  /* ALCLVL - [3:0] */

/*
 * R34 (0x22) - ALC control 3
 */
#define WM8985_ALCMODE                          0x0100  /* ALCMODE */
#define WM8985_ALCMODE_MASK                     0x0100  /* ALCMODE */
#define WM8985_ALCMODE_SHIFT                         8  /* ALCMODE */
#define WM8985_ALCMODE_WIDTH                         1  /* ALCMODE */
#define WM8985_ALCDCY_MASK                      0x00F0  /* ALCDCY - [7:4] */
#define WM8985_ALCDCY_SHIFT                          4  /* ALCDCY - [7:4] */
#define WM8985_ALCDCY_WIDTH                          4  /* ALCDCY - [7:4] */
#define WM8985_ALCATK_MASK                      0x000F  /* ALCATK - [3:0] */
#define WM8985_ALCATK_SHIFT                          0  /* ALCATK - [3:0] */
#define WM8985_ALCATK_WIDTH                          4  /* ALCATK - [3:0] */

/*
 * R35 (0x23) - Noise Gate
 */
#define WM8985_NGEN                             0x0008  /* NGEN */
#define WM8985_NGEN_MASK                        0x0008  /* NGEN */
#define WM8985_NGEN_SHIFT                            3  /* NGEN */
#define WM8985_NGEN_WIDTH                            1  /* NGEN */
#define WM8985_NGTH_MASK                        0x0007  /* NGTH - [2:0] */
#define WM8985_NGTH_SHIFT                            0  /* NGTH - [2:0] */
#define WM8985_NGTH_WIDTH                            3  /* NGTH - [2:0] */

/*
 * R36 (0x24) - PLL N
 */
#define WM8985_PLL_PRESCALE                     0x0010  /* PLL_PRESCALE */
#define WM8985_PLL_PRESCALE_MASK                0x0010  /* PLL_PRESCALE */
#define WM8985_PLL_PRESCALE_SHIFT                    4  /* PLL_PRESCALE */
#define WM8985_PLL_PRESCALE_WIDTH                    1  /* PLL_PRESCALE */
#define WM8985_PLLN_MASK                        0x000F  /* PLLN - [3:0] */
#define WM8985_PLLN_SHIFT                            0  /* PLLN - [3:0] */
#define WM8985_PLLN_WIDTH                            4  /* PLLN - [3:0] */

/*
 * R37 (0x25) - PLL K 1
 */
#define WM8985_PLLK_23_18_MASK                  0x003F  /* PLLK(23:18) - [5:0] */
#define WM8985_PLLK_23_18_SHIFT                      0  /* PLLK(23:18) - [5:0] */
#define WM8985_PLLK_23_18_WIDTH                      6  /* PLLK(23:18) - [5:0] */

/*
 * R38 (0x26) - PLL K 2
 */
#define WM8985_PLLK_17_9_MASK                   0x01FF  /* PLLK(17:9) - [8:0] */
#define WM8985_PLLK_17_9_SHIFT                       0  /* PLLK(17:9) - [8:0] */
#define WM8985_PLLK_17_9_WIDTH                       9  /* PLLK(17:9) - [8:0] */

/*
 * R39 (0x27) - PLL K 3
 */
#define WM8985_PLLK_8_0_MASK                    0x01FF  /* PLLK(8:0) - [8:0] */
#define WM8985_PLLK_8_0_SHIFT                        0  /* PLLK(8:0) - [8:0] */
#define WM8985_PLLK_8_0_WIDTH                        9  /* PLLK(8:0) - [8:0] */

/*
 * R41 (0x29) - 3D control
 */
#define WM8985_DEPTH3D_MASK                     0x000F  /* DEPTH3D - [3:0] */
#define WM8985_DEPTH3D_SHIFT                         0  /* DEPTH3D - [3:0] */
#define WM8985_DEPTH3D_WIDTH                         4  /* DEPTH3D - [3:0] */

/*
 * R42 (0x2A) - OUT4 to ADC
 */
#define WM8985_OUT4_2ADCVOL_MASK                0x01C0  /* OUT4_2ADCVOL - [8:6] */
#define WM8985_OUT4_2ADCVOL_SHIFT                    6  /* OUT4_2ADCVOL - [8:6] */
#define WM8985_OUT4_2ADCVOL_WIDTH                    3  /* OUT4_2ADCVOL - [8:6] */
#define WM8985_OUT4_2LNR                        0x0020  /* OUT4_2LNR */
#define WM8985_OUT4_2LNR_MASK                   0x0020  /* OUT4_2LNR */
#define WM8985_OUT4_2LNR_SHIFT                       5  /* OUT4_2LNR */
#define WM8985_OUT4_2LNR_WIDTH                       1  /* OUT4_2LNR */
#define WM8758_VMIDTOG_MASK                     0x0010  /* VMIDTOG */
#define WM8758_VMIDTOG_SHIFT                         4  /* VMIDTOG */
#define WM8758_VMIDTOG_WIDTH                         1  /* VMIDTOG */
#define WM8758_OUT2DEL_MASK                     0x0008  /* OUT2DEL */
#define WM8758_OUT2DEL_SHIFT                         3  /* OUT2DEL */
#define WM8758_OUT2DEL_WIDTH                         1  /* OUT2DEL */
#define WM8985_POBCTRL                          0x0004  /* POBCTRL */
#define WM8985_POBCTRL_MASK                     0x0004  /* POBCTRL */
#define WM8985_POBCTRL_SHIFT                         2  /* POBCTRL */
#define WM8985_POBCTRL_WIDTH                         1  /* POBCTRL */
#define WM8985_DELEN                            0x0002  /* DELEN */
#define WM8985_DELEN_MASK                       0x0002  /* DELEN */
#define WM8985_DELEN_SHIFT                           1  /* DELEN */
#define WM8985_DELEN_WIDTH                           1  /* DELEN */
#define WM8985_OUT1DEL                          0x0001  /* OUT1DEL */
#define WM8985_OUT1DEL_MASK                     0x0001  /* OUT1DEL */
#define WM8985_OUT1DEL_SHIFT                         0  /* OUT1DEL */
#define WM8985_OUT1DEL_WIDTH                         1  /* OUT1DEL */

/*
 * R43 (0x2B) - Beep control
 */
#define WM8985_BYPL2RMIX                        0x0100  /* BYPL2RMIX */
#define WM8985_BYPL2RMIX_MASK                   0x0100  /* BYPL2RMIX */
#define WM8985_BYPL2RMIX_SHIFT                       8  /* BYPL2RMIX */
#define WM8985_BYPL2RMIX_WIDTH                       1  /* BYPL2RMIX */
#define WM8985_BYPR2LMIX                        0x0080  /* BYPR2LMIX */
#define WM8985_BYPR2LMIX_MASK                   0x0080  /* BYPR2LMIX */
#define WM8985_BYPR2LMIX_SHIFT                       7  /* BYPR2LMIX */
#define WM8985_BYPR2LMIX_WIDTH                       1  /* BYPR2LMIX */
#define WM8985_MUTERPGA2INV                     0x0020  /* MUTERPGA2INV */
#define WM8985_MUTERPGA2INV_MASK                0x0020  /* MUTERPGA2INV */
#define WM8985_MUTERPGA2INV_SHIFT                    5  /* MUTERPGA2INV */
#define WM8985_MUTERPGA2INV_WIDTH                    1  /* MUTERPGA2INV */
#define WM8985_INVROUT2                         0x0010  /* INVROUT2 */
#define WM8985_INVROUT2_MASK                    0x0010  /* INVROUT2 */
#define WM8985_INVROUT2_SHIFT                        4  /* INVROUT2 */
#define WM8985_INVROUT2_WIDTH                        1  /* INVROUT2 */
#define WM8985_BEEPVOL_MASK                     0x000E  /* BEEPVOL - [3:1] */
#define WM8985_BEEPVOL_SHIFT                         1  /* BEEPVOL - [3:1] */
#define WM8985_BEEPVOL_WIDTH                         3  /* BEEPVOL - [3:1] */
#define WM8758_DELEN2_MASK                      0x0004  /* DELEN2 */
#define WM8758_DELEN2_SHIFT                          2  /* DELEN2 */
#define WM8758_DELEN2_WIDTH                          1  /* DELEN2 */
#define WM8985_BEEPEN                           0x0001  /* BEEPEN */
#define WM8985_BEEPEN_MASK                      0x0001  /* BEEPEN */
#define WM8985_BEEPEN_SHIFT                          0  /* BEEPEN */
#define WM8985_BEEPEN_WIDTH                          1  /* BEEPEN */

/*
 * R44 (0x2C) - Input ctrl
 */
#define WM8985_MBVSEL                           0x0100  /* MBVSEL */
#define WM8985_MBVSEL_MASK                      0x0100  /* MBVSEL */
#define WM8985_MBVSEL_SHIFT                          8  /* MBVSEL */
#define WM8985_MBVSEL_WIDTH                          1  /* MBVSEL */
#define WM8985_R2_2INPPGA                       0x0040  /* R2_2INPPGA */
#define WM8985_R2_2INPPGA_MASK                  0x0040  /* R2_2INPPGA */
#define WM8985_R2_2INPPGA_SHIFT                      6  /* R2_2INPPGA */
#define WM8985_R2_2INPPGA_WIDTH                      1  /* R2_2INPPGA */
#define WM8985_RIN2INPPGA                       0x0020  /* RIN2INPPGA */
#define WM8985_RIN2INPPGA_MASK                  0x0020  /* RIN2INPPGA */
#define WM8985_RIN2INPPGA_SHIFT                      5  /* RIN2INPPGA */
#define WM8985_RIN2INPPGA_WIDTH                      1  /* RIN2INPPGA */
#define WM8985_RIP2INPPGA                       0x0010  /* RIP2INPPGA */
#define WM8985_RIP2INPPGA_MASK                  0x0010  /* RIP2INPPGA */
#define WM8985_RIP2INPPGA_SHIFT                      4  /* RIP2INPPGA */
#define WM8985_RIP2INPPGA_WIDTH                      1  /* RIP2INPPGA */
#define WM8985_L2_2INPPGA                       0x0004  /* L2_2INPPGA */
#define WM8985_L2_2INPPGA_MASK                  0x0004  /* L2_2INPPGA */
#define WM8985_L2_2INPPGA_SHIFT                      2  /* L2_2INPPGA */
#define WM8985_L2_2INPPGA_WIDTH                      1  /* L2_2INPPGA */
#define WM8985_LIN2INPPGA                       0x0002  /* LIN2INPPGA */
#define WM8985_LIN2INPPGA_MASK                  0x0002  /* LIN2INPPGA */
#define WM8985_LIN2INPPGA_SHIFT                      1  /* LIN2INPPGA */
#define WM8985_LIN2INPPGA_WIDTH                      1  /* LIN2INPPGA */
#define WM8985_LIP2INPPGA                       0x0001  /* LIP2INPPGA */
#define WM8985_LIP2INPPGA_MASK                  0x0001  /* LIP2INPPGA */
#define WM8985_LIP2INPPGA_SHIFT                      0  /* LIP2INPPGA */
#define WM8985_LIP2INPPGA_WIDTH                      1  /* LIP2INPPGA */

/*
 * R45 (0x2D) - Left INP PGA gain ctrl
 */
#define WM8985_INPGAVU                          0x0100  /* INPGAVU */
#define WM8985_INPGAVU_MASK                     0x0100  /* INPGAVU */
#define WM8985_INPGAVU_SHIFT                         8  /* INPGAVU */
#define WM8985_INPGAVU_WIDTH                         1  /* INPGAVU */
#define WM8985_INPPGAZCL                        0x0080  /* INPPGAZCL */
#define WM8985_INPPGAZCL_MASK                   0x0080  /* INPPGAZCL */
#define WM8985_INPPGAZCL_SHIFT                       7  /* INPPGAZCL */
#define WM8985_INPPGAZCL_WIDTH                       1  /* INPPGAZCL */
#define WM8985_INPPGAMUTEL                      0x0040  /* INPPGAMUTEL */
#define WM8985_INPPGAMUTEL_MASK                 0x0040  /* INPPGAMUTEL */
#define WM8985_INPPGAMUTEL_SHIFT                     6  /* INPPGAMUTEL */
#define WM8985_INPPGAMUTEL_WIDTH                     1  /* INPPGAMUTEL */
#define WM8985_INPPGAVOLL_MASK                  0x003F  /* INPPGAVOLL - [5:0] */
#define WM8985_INPPGAVOLL_SHIFT                      0  /* INPPGAVOLL - [5:0] */
#define WM8985_INPPGAVOLL_WIDTH                      6  /* INPPGAVOLL - [5:0] */

/*
 * R46 (0x2E) - Right INP PGA gain ctrl
 */
#define WM8985_INPGAVU                          0x0100  /* INPGAVU */
#define WM8985_INPGAVU_MASK                     0x0100  /* INPGAVU */
#define WM8985_INPGAVU_SHIFT                         8  /* INPGAVU */
#define WM8985_INPGAVU_WIDTH                         1  /* INPGAVU */
#define WM8985_INPPGAZCR                        0x0080  /* INPPGAZCR */
#define WM8985_INPPGAZCR_MASK                   0x0080  /* INPPGAZCR */
#define WM8985_INPPGAZCR_SHIFT                       7  /* INPPGAZCR */
#define WM8985_INPPGAZCR_WIDTH                       1  /* INPPGAZCR */
#define WM8985_INPPGAMUTER                      0x0040  /* INPPGAMUTER */
#define WM8985_INPPGAMUTER_MASK                 0x0040  /* INPPGAMUTER */
#define WM8985_INPPGAMUTER_SHIFT                     6  /* INPPGAMUTER */
#define WM8985_INPPGAMUTER_WIDTH                     1  /* INPPGAMUTER */
#define WM8985_INPPGAVOLR_MASK                  0x003F  /* INPPGAVOLR - [5:0] */
#define WM8985_INPPGAVOLR_SHIFT                      0  /* INPPGAVOLR - [5:0] */
#define WM8985_INPPGAVOLR_WIDTH                      6  /* INPPGAVOLR - [5:0] */

/*
 * R47 (0x2F) - Left ADC BOOST ctrl
 */
#define WM8985_PGABOOSTL                        0x0100  /* PGABOOSTL */
#define WM8985_PGABOOSTL_MASK                   0x0100  /* PGABOOSTL */
#define WM8985_PGABOOSTL_SHIFT                       8  /* PGABOOSTL */
#define WM8985_PGABOOSTL_WIDTH                       1  /* PGABOOSTL */
#define WM8985_L2_2BOOSTVOL_MASK                0x0070  /* L2_2BOOSTVOL - [6:4] */
#define WM8985_L2_2BOOSTVOL_SHIFT                    4  /* L2_2BOOSTVOL - [6:4] */
#define WM8985_L2_2BOOSTVOL_WIDTH                    3  /* L2_2BOOSTVOL - [6:4] */
#define WM8985_AUXL2BOOSTVOL_MASK               0x0007  /* AUXL2BOOSTVOL - [2:0] */
#define WM8985_AUXL2BOOSTVOL_SHIFT                   0  /* AUXL2BOOSTVOL - [2:0] */
#define WM8985_AUXL2BOOSTVOL_WIDTH                   3  /* AUXL2BOOSTVOL - [2:0] */

/*
 * R48 (0x30) - Right ADC BOOST ctrl
 */
#define WM8985_PGABOOSTR                        0x0100  /* PGABOOSTR */
#define WM8985_PGABOOSTR_MASK                   0x0100  /* PGABOOSTR */
#define WM8985_PGABOOSTR_SHIFT                       8  /* PGABOOSTR */
#define WM8985_PGABOOSTR_WIDTH                       1  /* PGABOOSTR */
#define WM8985_R2_2BOOSTVOL_MASK                0x0070  /* R2_2BOOSTVOL - [6:4] */
#define WM8985_R2_2BOOSTVOL_SHIFT                    4  /* R2_2BOOSTVOL - [6:4] */
#define WM8985_R2_2BOOSTVOL_WIDTH                    3  /* R2_2BOOSTVOL - [6:4] */
#define WM8985_AUXR2BOOSTVOL_MASK               0x0007  /* AUXR2BOOSTVOL - [2:0] */
#define WM8985_AUXR2BOOSTVOL_SHIFT                   0  /* AUXR2BOOSTVOL - [2:0] */
#define WM8985_AUXR2BOOSTVOL_WIDTH                   3  /* AUXR2BOOSTVOL - [2:0] */

/*
 * R49 (0x31) - Output ctrl
 */
#define WM8758_HP_COM                           0x0100  /* HP_COM */
#define WM8758_HP_COM_MASK                      0x0100  /* HP_COM */
#define WM8758_HP_COM_SHIFT                          8  /* HP_COM */
#define WM8758_HP_COM_WIDTH                          1  /* HP_COM */
#define WM8758_LINE_COM                         0x0080  /* LINE_COM */
#define WM8758_LINE_COM_MASK                    0x0080  /* LINE_COM */
#define WM8758_LINE_COM_SHIFT                        7  /* LINE_COM */
#define WM8758_LINE_COM_WIDTH                        1  /* LINE_COM */
#define WM8985_DACL2RMIX                        0x0040  /* DACL2RMIX */
#define WM8985_DACL2RMIX_MASK                   0x0040  /* DACL2RMIX */
#define WM8985_DACL2RMIX_SHIFT                       6  /* DACL2RMIX */
#define WM8985_DACL2RMIX_WIDTH                       1  /* DACL2RMIX */
#define WM8985_DACR2LMIX                        0x0020  /* DACR2LMIX */
#define WM8985_DACR2LMIX_MASK                   0x0020  /* DACR2LMIX */
#define WM8985_DACR2LMIX_SHIFT                       5  /* DACR2LMIX */
#define WM8985_DACR2LMIX_WIDTH                       1  /* DACR2LMIX */
#define WM8985_OUT4BOOST                        0x0010  /* OUT4BOOST */
#define WM8985_OUT4BOOST_MASK                   0x0010  /* OUT4BOOST */
#define WM8985_OUT4BOOST_SHIFT                       4  /* OUT4BOOST */
#define WM8985_OUT4BOOST_WIDTH                       1  /* OUT4BOOST */
#define WM8985_OUT3BOOST                        0x0008  /* OUT3BOOST */
#define WM8985_OUT3BOOST_MASK                   0x0008  /* OUT3BOOST */
#define WM8985_OUT3BOOST_SHIFT                       3  /* OUT3BOOST */
#define WM8985_OUT3BOOST_WIDTH                       1  /* OUT3BOOST */
#define WM8758_OUT4ENDEL                        0x0010  /* OUT4ENDEL */
#define WM8758_OUT4ENDEL_MASK                   0x0010  /* OUT4ENDEL */
#define WM8758_OUT4ENDEL_SHIFT                       4  /* OUT4ENDEL */
#define WM8758_OUT4ENDEL_WIDTH                       1  /* OUT4ENDEL */
#define WM8758_OUT3ENDEL                        0x0008  /* OUT3ENDEL */
#define WM8758_OUT3ENDEL_MASK                   0x0008  /* OUT3ENDEL */
#define WM8758_OUT3ENDEL_SHIFT                       3  /* OUT3ENDEL */
#define WM8758_OUT3ENDEL_WIDTH                       1  /* OUT3ENDEL */
#define WM8985_TSOPCTRL                         0x0004  /* TSOPCTRL */
#define WM8985_TSOPCTRL_MASK                    0x0004  /* TSOPCTRL */
#define WM8985_TSOPCTRL_SHIFT                        2  /* TSOPCTRL */
#define WM8985_TSOPCTRL_WIDTH                        1  /* TSOPCTRL */
#define WM8985_TSDEN                            0x0002  /* TSDEN */
#define WM8985_TSDEN_MASK                       0x0002  /* TSDEN */
#define WM8985_TSDEN_SHIFT                           1  /* TSDEN */
#define WM8985_TSDEN_WIDTH                           1  /* TSDEN */
#define WM8985_VROI                             0x0001  /* VROI */
#define WM8985_VROI_MASK                        0x0001  /* VROI */
#define WM8985_VROI_SHIFT                            0  /* VROI */
#define WM8985_VROI_WIDTH                            1  /* VROI */

/*
 * R50 (0x32) - Left mixer ctrl
 */
#define WM8985_AUXLMIXVOL_MASK                  0x01C0  /* AUXLMIXVOL - [8:6] */
#define WM8985_AUXLMIXVOL_SHIFT                      6  /* AUXLMIXVOL - [8:6] */
#define WM8985_AUXLMIXVOL_WIDTH                      3  /* AUXLMIXVOL - [8:6] */
#define WM8985_AUXL2LMIX                        0x0020  /* AUXL2LMIX */
#define WM8985_AUXL2LMIX_MASK                   0x0020  /* AUXL2LMIX */
#define WM8985_AUXL2LMIX_SHIFT                       5  /* AUXL2LMIX */
#define WM8985_AUXL2LMIX_WIDTH                       1  /* AUXL2LMIX */
#define WM8985_BYPLMIXVOL_MASK                  0x001C  /* BYPLMIXVOL - [4:2] */
#define WM8985_BYPLMIXVOL_SHIFT                      2  /* BYPLMIXVOL - [4:2] */
#define WM8985_BYPLMIXVOL_WIDTH                      3  /* BYPLMIXVOL - [4:2] */
#define WM8985_BYPL2LMIX                        0x0002  /* BYPL2LMIX */
#define WM8985_BYPL2LMIX_MASK                   0x0002  /* BYPL2LMIX */
#define WM8985_BYPL2LMIX_SHIFT                       1  /* BYPL2LMIX */
#define WM8985_BYPL2LMIX_WIDTH                       1  /* BYPL2LMIX */
#define WM8985_DACL2LMIX                        0x0001  /* DACL2LMIX */
#define WM8985_DACL2LMIX_MASK                   0x0001  /* DACL2LMIX */
#define WM8985_DACL2LMIX_SHIFT                       0  /* DACL2LMIX */
#define WM8985_DACL2LMIX_WIDTH                       1  /* DACL2LMIX */

/*
 * R51 (0x33) - Right mixer ctrl
 */
#define WM8985_AUXRMIXVOL_MASK                  0x01C0  /* AUXRMIXVOL - [8:6] */
#define WM8985_AUXRMIXVOL_SHIFT                      6  /* AUXRMIXVOL - [8:6] */
#define WM8985_AUXRMIXVOL_WIDTH                      3  /* AUXRMIXVOL - [8:6] */
#define WM8985_AUXR2RMIX                        0x0020  /* AUXR2RMIX */
#define WM8985_AUXR2RMIX_MASK                   0x0020  /* AUXR2RMIX */
#define WM8985_AUXR2RMIX_SHIFT                       5  /* AUXR2RMIX */
#define WM8985_AUXR2RMIX_WIDTH                       1  /* AUXR2RMIX */
#define WM8985_BYPRMIXVOL_MASK                  0x001C  /* BYPRMIXVOL - [4:2] */
#define WM8985_BYPRMIXVOL_SHIFT                      2  /* BYPRMIXVOL - [4:2] */
#define WM8985_BYPRMIXVOL_WIDTH                      3  /* BYPRMIXVOL - [4:2] */
#define WM8985_BYPR2RMIX                        0x0002  /* BYPR2RMIX */
#define WM8985_BYPR2RMIX_MASK                   0x0002  /* BYPR2RMIX */
#define WM8985_BYPR2RMIX_SHIFT                       1  /* BYPR2RMIX */
#define WM8985_BYPR2RMIX_WIDTH                       1  /* BYPR2RMIX */
#define WM8985_DACR2RMIX                        0x0001  /* DACR2RMIX */
#define WM8985_DACR2RMIX_MASK                   0x0001  /* DACR2RMIX */
#define WM8985_DACR2RMIX_SHIFT                       0  /* DACR2RMIX */
#define WM8985_DACR2RMIX_WIDTH                       1  /* DACR2RMIX */

/*
 * R52 (0x34) - LOUT1 (HP) volume ctrl
 */
#define WM8985_OUT1VU                           0x0100  /* OUT1VU */
#define WM8985_OUT1VU_MASK                      0x0100  /* OUT1VU */
#define WM8985_OUT1VU_SHIFT                          8  /* OUT1VU */
#define WM8985_OUT1VU_WIDTH                          1  /* OUT1VU */
#define WM8985_LOUT1ZC                          0x0080  /* LOUT1ZC */
#define WM8985_LOUT1ZC_MASK                     0x0080  /* LOUT1ZC */
#define WM8985_LOUT1ZC_SHIFT                         7  /* LOUT1ZC */
#define WM8985_LOUT1ZC_WIDTH                         1  /* LOUT1ZC */
#define WM8985_LOUT1MUTE                        0x0040  /* LOUT1MUTE */
#define WM8985_LOUT1MUTE_MASK                   0x0040  /* LOUT1MUTE */
#define WM8985_LOUT1MUTE_SHIFT                       6  /* LOUT1MUTE */
#define WM8985_LOUT1MUTE_WIDTH                       1  /* LOUT1MUTE */
#define WM8985_LOUT1VOL_MASK                    0x003F  /* LOUT1VOL - [5:0] */
#define WM8985_LOUT1VOL_SHIFT                        0  /* LOUT1VOL - [5:0] */
#define WM8985_LOUT1VOL_WIDTH                        6  /* LOUT1VOL - [5:0] */

/*
 * R53 (0x35) - ROUT1 (HP) volume ctrl
 */
#define WM8985_OUT1VU                           0x0100  /* OUT1VU */
#define WM8985_OUT1VU_MASK                      0x0100  /* OUT1VU */
#define WM8985_OUT1VU_SHIFT                          8  /* OUT1VU */
#define WM8985_OUT1VU_WIDTH                          1  /* OUT1VU */
#define WM8985_ROUT1ZC                          0x0080  /* ROUT1ZC */
#define WM8985_ROUT1ZC_MASK                     0x0080  /* ROUT1ZC */
#define WM8985_ROUT1ZC_SHIFT                         7  /* ROUT1ZC */
#define WM8985_ROUT1ZC_WIDTH                         1  /* ROUT1ZC */
#define WM8985_ROUT1MUTE                        0x0040  /* ROUT1MUTE */
#define WM8985_ROUT1MUTE_MASK                   0x0040  /* ROUT1MUTE */
#define WM8985_ROUT1MUTE_SHIFT                       6  /* ROUT1MUTE */
#define WM8985_ROUT1MUTE_WIDTH                       1  /* ROUT1MUTE */
#define WM8985_ROUT1VOL_MASK                    0x003F  /* ROUT1VOL - [5:0] */
#define WM8985_ROUT1VOL_SHIFT                        0  /* ROUT1VOL - [5:0] */
#define WM8985_ROUT1VOL_WIDTH                        6  /* ROUT1VOL - [5:0] */

/*
 * R54 (0x36) - LOUT2 (SPK) volume ctrl
 */
#define WM8985_OUT2VU                           0x0100  /* OUT2VU */
#define WM8985_OUT2VU_MASK                      0x0100  /* OUT2VU */
#define WM8985_OUT2VU_SHIFT                          8  /* OUT2VU */
#define WM8985_OUT2VU_WIDTH                          1  /* OUT2VU */
#define WM8985_LOUT2ZC                          0x0080  /* LOUT2ZC */
#define WM8985_LOUT2ZC_MASK                     0x0080  /* LOUT2ZC */
#define WM8985_LOUT2ZC_SHIFT                         7  /* LOUT2ZC */
#define WM8985_LOUT2ZC_WIDTH                         1  /* LOUT2ZC */
#define WM8985_LOUT2MUTE                        0x0040  /* LOUT2MUTE */
#define WM8985_LOUT2MUTE_MASK                   0x0040  /* LOUT2MUTE */
#define WM8985_LOUT2MUTE_SHIFT                       6  /* LOUT2MUTE */
#define WM8985_LOUT2MUTE_WIDTH                       1  /* LOUT2MUTE */
#define WM8985_LOUT2VOL_MASK                    0x003F  /* LOUT2VOL - [5:0] */
#define WM8985_LOUT2VOL_SHIFT                        0  /* LOUT2VOL - [5:0] */
#define WM8985_LOUT2VOL_WIDTH                        6  /* LOUT2VOL - [5:0] */

/*
 * R55 (0x37) - ROUT2 (SPK) volume ctrl
 */
#define WM8985_OUT2VU                           0x0100  /* OUT2VU */
#define WM8985_OUT2VU_MASK                      0x0100  /* OUT2VU */
#define WM8985_OUT2VU_SHIFT                          8  /* OUT2VU */
#define WM8985_OUT2VU_WIDTH                          1  /* OUT2VU */
#define WM8985_ROUT2ZC                          0x0080  /* ROUT2ZC */
#define WM8985_ROUT2ZC_MASK                     0x0080  /* ROUT2ZC */
#define WM8985_ROUT2ZC_SHIFT                         7  /* ROUT2ZC */
#define WM8985_ROUT2ZC_WIDTH                         1  /* ROUT2ZC */
#define WM8985_ROUT2MUTE                        0x0040  /* ROUT2MUTE */
#define WM8985_ROUT2MUTE_MASK                   0x0040  /* ROUT2MUTE */
#define WM8985_ROUT2MUTE_SHIFT                       6  /* ROUT2MUTE */
#define WM8985_ROUT2MUTE_WIDTH                       1  /* ROUT2MUTE */
#define WM8985_ROUT2VOL_MASK                    0x003F  /* ROUT2VOL - [5:0] */
#define WM8985_ROUT2VOL_SHIFT                        0  /* ROUT2VOL - [5:0] */
#define WM8985_ROUT2VOL_WIDTH                        6  /* ROUT2VOL - [5:0] */

/*
 * R56 (0x38) - OUT3 mixer ctrl
 */
#define WM8985_OUT3MUTE                         0x0040  /* OUT3MUTE */
#define WM8985_OUT3MUTE_MASK                    0x0040  /* OUT3MUTE */
#define WM8985_OUT3MUTE_SHIFT                        6  /* OUT3MUTE */
#define WM8985_OUT3MUTE_WIDTH                        1  /* OUT3MUTE */
#define WM8985_OUT4_2OUT3                       0x0008  /* OUT4_2OUT3 */
#define WM8985_OUT4_2OUT3_MASK                  0x0008  /* OUT4_2OUT3 */
#define WM8985_OUT4_2OUT3_SHIFT                      3  /* OUT4_2OUT3 */
#define WM8985_OUT4_2OUT3_WIDTH                      1  /* OUT4_2OUT3 */
#define WM8985_BYPL2OUT3                        0x0004  /* BYPL2OUT3 */
#define WM8985_BYPL2OUT3_MASK                   0x0004  /* BYPL2OUT3 */
#define WM8985_BYPL2OUT3_SHIFT                       2  /* BYPL2OUT3 */
#define WM8985_BYPL2OUT3_WIDTH                       1  /* BYPL2OUT3 */
#define WM8985_LMIX2OUT3                        0x0002  /* LMIX2OUT3 */
#define WM8985_LMIX2OUT3_MASK                   0x0002  /* LMIX2OUT3 */
#define WM8985_LMIX2OUT3_SHIFT                       1  /* LMIX2OUT3 */
#define WM8985_LMIX2OUT3_WIDTH                       1  /* LMIX2OUT3 */
#define WM8985_LDAC2OUT3                        0x0001  /* LDAC2OUT3 */
#define WM8985_LDAC2OUT3_MASK                   0x0001  /* LDAC2OUT3 */
#define WM8985_LDAC2OUT3_SHIFT                       0  /* LDAC2OUT3 */
#define WM8985_LDAC2OUT3_WIDTH                       1  /* LDAC2OUT3 */

/*
 * R57 (0x39) - OUT4 (MONO) mix ctrl
 */
#define WM8985_OUT3_2OUT4                       0x0080  /* OUT3_2OUT4 */
#define WM8985_OUT3_2OUT4_MASK                  0x0080  /* OUT3_2OUT4 */
#define WM8985_OUT3_2OUT4_SHIFT                      7  /* OUT3_2OUT4 */
#define WM8985_OUT3_2OUT4_WIDTH                      1  /* OUT3_2OUT4 */
#define WM8985_OUT4MUTE                         0x0040  /* OUT4MUTE */
#define WM8985_OUT4MUTE_MASK                    0x0040  /* OUT4MUTE */
#define WM8985_OUT4MUTE_SHIFT                        6  /* OUT4MUTE */
#define WM8985_OUT4MUTE_WIDTH                        1  /* OUT4MUTE */
#define WM8985_OUT4ATTN                         0x0020  /* OUT4ATTN */
#define WM8985_OUT4ATTN_MASK                    0x0020  /* OUT4ATTN */
#define WM8985_OUT4ATTN_SHIFT                        5  /* OUT4ATTN */
#define WM8985_OUT4ATTN_WIDTH                        1  /* OUT4ATTN */
#define WM8985_LMIX2OUT4                        0x0010  /* LMIX2OUT4 */
#define WM8985_LMIX2OUT4_MASK                   0x0010  /* LMIX2OUT4 */
#define WM8985_LMIX2OUT4_SHIFT                       4  /* LMIX2OUT4 */
#define WM8985_LMIX2OUT4_WIDTH                       1  /* LMIX2OUT4 */
#define WM8985_LDAC2OUT4                        0x0008  /* LDAC2OUT4 */
#define WM8985_LDAC2OUT4_MASK                   0x0008  /* LDAC2OUT4 */
#define WM8985_LDAC2OUT4_SHIFT                       3  /* LDAC2OUT4 */
#define WM8985_LDAC2OUT4_WIDTH                       1  /* LDAC2OUT4 */
#define WM8985_BYPR2OUT4                        0x0004  /* BYPR2OUT4 */
#define WM8985_BYPR2OUT4_MASK                   0x0004  /* BYPR2OUT4 */
#define WM8985_BYPR2OUT4_SHIFT                       2  /* BYPR2OUT4 */
#define WM8985_BYPR2OUT4_WIDTH                       1  /* BYPR2OUT4 */
#define WM8985_RMIX2OUT4                        0x0002  /* RMIX2OUT4 */
#define WM8985_RMIX2OUT4_MASK                   0x0002  /* RMIX2OUT4 */
#define WM8985_RMIX2OUT4_SHIFT                       1  /* RMIX2OUT4 */
#define WM8985_RMIX2OUT4_WIDTH                       1  /* RMIX2OUT4 */
#define WM8985_RDAC2OUT4                        0x0001  /* RDAC2OUT4 */
#define WM8985_RDAC2OUT4_MASK                   0x0001  /* RDAC2OUT4 */
#define WM8985_RDAC2OUT4_SHIFT                       0  /* RDAC2OUT4 */
#define WM8985_RDAC2OUT4_WIDTH                       1  /* RDAC2OUT4 */

/*
 * R60 (0x3C) - OUTPUT ctrl
 */
#define WM8985_VIDBUFFTST_MASK                  0x01E0  /* VIDBUFFTST - [8:5] */
#define WM8985_VIDBUFFTST_SHIFT                      5  /* VIDBUFFTST - [8:5] */
#define WM8985_VIDBUFFTST_WIDTH                      4  /* VIDBUFFTST - [8:5] */
#define WM8985_HPTOG                            0x0008  /* HPTOG */
#define WM8985_HPTOG_MASK                       0x0008  /* HPTOG */
#define WM8985_HPTOG_SHIFT                           3  /* HPTOG */
#define WM8985_HPTOG_WIDTH                           1  /* HPTOG */

/*
 * R61 (0x3D) - BIAS CTRL
 */
#define WM8985_BIASCUT                          0x0100  /* BIASCUT */
#define WM8985_BIASCUT_MASK                     0x0100  /* BIASCUT */
#define WM8985_BIASCUT_SHIFT                         8  /* BIASCUT */
#define WM8985_BIASCUT_WIDTH                         1  /* BIASCUT */
#define WM8985_HALFIPBIAS                       0x0080  /* HALFIPBIAS */
#define WM8985_HALFIPBIAS_MASK                  0x0080  /* HALFIPBIAS */
#define WM8985_HALFIPBIAS_SHIFT                      7  /* HALFIPBIAS */
#define WM8985_HALFIPBIAS_WIDTH                      1  /* HALFIPBIAS */
#define WM8758_HALFIPBIAS                       0x0040  /* HALFI_IPGA */
#define WM8758_HALFI_IPGA_MASK                  0x0040  /* HALFI_IPGA */
#define WM8758_HALFI_IPGA_SHIFT                      6  /* HALFI_IPGA */
#define WM8758_HALFI_IPGA_WIDTH                      1  /* HALFI_IPGA */
#define WM8985_VBBIASTST_MASK                   0x0060  /* VBBIASTST - [6:5] */
#define WM8985_VBBIASTST_SHIFT                       5  /* VBBIASTST - [6:5] */
#define WM8985_VBBIASTST_WIDTH                       2  /* VBBIASTST - [6:5] */
#define WM8985_BUFBIAS_MASK                     0x0018  /* BUFBIAS - [4:3] */
#define WM8985_BUFBIAS_SHIFT                         3  /* BUFBIAS - [4:3] */
#define WM8985_BUFBIAS_WIDTH                         2  /* BUFBIAS - [4:3] */
#define WM8985_ADCBIAS_MASK                     0x0006  /* ADCBIAS - [2:1] */
#define WM8985_ADCBIAS_SHIFT                         1  /* ADCBIAS - [2:1] */
#define WM8985_ADCBIAS_WIDTH                         2  /* ADCBIAS - [2:1] */
#define WM8985_HALFOPBIAS                       0x0001  /* HALFOPBIAS */
#define WM8985_HALFOPBIAS_MASK                  0x0001  /* HALFOPBIAS */
#define WM8985_HALFOPBIAS_SHIFT                      0  /* HALFOPBIAS */
#define WM8985_HALFOPBIAS_WIDTH                      1  /* HALFOPBIAS */

enum clk_src {
	WM8985_CLKSRC_MCLK,
	WM8985_CLKSRC_PLL
};

#define WM8985_PLL 0

#endif
