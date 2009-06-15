#ifndef WM9081_H
#define WM9081_H

/*
 * wm9081.c  --  WM9081 ALSA SoC Audio driver
 *
 * Author: Mark Brown
 *
 * Copyright 2009 Wolfson Microelectronics plc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <sound/soc.h>

extern struct snd_soc_dai wm9081_dai;
extern struct snd_soc_codec_device soc_codec_dev_wm9081;

/*
 * SYSCLK sources
 */
#define WM9081_SYSCLK_MCLK      1   /* Use MCLK without FLL */
#define WM9081_SYSCLK_FLL_MCLK  2   /* Use MCLK, enabling FLL if required */

/*
 * Register values.
 */
#define WM9081_SOFTWARE_RESET                   0x00
#define WM9081_ANALOGUE_LINEOUT                 0x02
#define WM9081_ANALOGUE_SPEAKER_PGA             0x03
#define WM9081_VMID_CONTROL                     0x04
#define WM9081_BIAS_CONTROL_1                   0x05
#define WM9081_ANALOGUE_MIXER                   0x07
#define WM9081_ANTI_POP_CONTROL                 0x08
#define WM9081_ANALOGUE_SPEAKER_1               0x09
#define WM9081_ANALOGUE_SPEAKER_2               0x0A
#define WM9081_POWER_MANAGEMENT                 0x0B
#define WM9081_CLOCK_CONTROL_1                  0x0C
#define WM9081_CLOCK_CONTROL_2                  0x0D
#define WM9081_CLOCK_CONTROL_3                  0x0E
#define WM9081_FLL_CONTROL_1                    0x10
#define WM9081_FLL_CONTROL_2                    0x11
#define WM9081_FLL_CONTROL_3                    0x12
#define WM9081_FLL_CONTROL_4                    0x13
#define WM9081_FLL_CONTROL_5                    0x14
#define WM9081_AUDIO_INTERFACE_1                0x16
#define WM9081_AUDIO_INTERFACE_2                0x17
#define WM9081_AUDIO_INTERFACE_3                0x18
#define WM9081_AUDIO_INTERFACE_4                0x19
#define WM9081_INTERRUPT_STATUS                 0x1A
#define WM9081_INTERRUPT_STATUS_MASK            0x1B
#define WM9081_INTERRUPT_POLARITY               0x1C
#define WM9081_INTERRUPT_CONTROL                0x1D
#define WM9081_DAC_DIGITAL_1                    0x1E
#define WM9081_DAC_DIGITAL_2                    0x1F
#define WM9081_DRC_1                            0x20
#define WM9081_DRC_2                            0x21
#define WM9081_DRC_3                            0x22
#define WM9081_DRC_4                            0x23
#define WM9081_WRITE_SEQUENCER_1                0x26
#define WM9081_WRITE_SEQUENCER_2                0x27
#define WM9081_MW_SLAVE_1                       0x28
#define WM9081_EQ_1                             0x2A
#define WM9081_EQ_2                             0x2B
#define WM9081_EQ_3                             0x2C
#define WM9081_EQ_4                             0x2D
#define WM9081_EQ_5                             0x2E
#define WM9081_EQ_6                             0x2F
#define WM9081_EQ_7                             0x30
#define WM9081_EQ_8                             0x31
#define WM9081_EQ_9                             0x32
#define WM9081_EQ_10                            0x33
#define WM9081_EQ_11                            0x34
#define WM9081_EQ_12                            0x35
#define WM9081_EQ_13                            0x36
#define WM9081_EQ_14                            0x37
#define WM9081_EQ_15                            0x38
#define WM9081_EQ_16                            0x39
#define WM9081_EQ_17                            0x3A
#define WM9081_EQ_18                            0x3B
#define WM9081_EQ_19                            0x3C
#define WM9081_EQ_20                            0x3D

#define WM9081_REGISTER_COUNT                   55
#define WM9081_MAX_REGISTER                     0x3D

/*
 * Field Definitions.
 */

/*
 * R0 (0x00) - Software Reset
 */
#define WM9081_SW_RST_DEV_ID1_MASK              0xFFFF  /* SW_RST_DEV_ID1 - [15:0] */
#define WM9081_SW_RST_DEV_ID1_SHIFT                  0  /* SW_RST_DEV_ID1 - [15:0] */
#define WM9081_SW_RST_DEV_ID1_WIDTH                 16  /* SW_RST_DEV_ID1 - [15:0] */

/*
 * R2 (0x02) - Analogue Lineout
 */
#define WM9081_LINEOUT_MUTE                     0x0080  /* LINEOUT_MUTE */
#define WM9081_LINEOUT_MUTE_MASK                0x0080  /* LINEOUT_MUTE */
#define WM9081_LINEOUT_MUTE_SHIFT                    7  /* LINEOUT_MUTE */
#define WM9081_LINEOUT_MUTE_WIDTH                    1  /* LINEOUT_MUTE */
#define WM9081_LINEOUTZC                        0x0040  /* LINEOUTZC */
#define WM9081_LINEOUTZC_MASK                   0x0040  /* LINEOUTZC */
#define WM9081_LINEOUTZC_SHIFT                       6  /* LINEOUTZC */
#define WM9081_LINEOUTZC_WIDTH                       1  /* LINEOUTZC */
#define WM9081_LINEOUT_VOL_MASK                 0x003F  /* LINEOUT_VOL - [5:0] */
#define WM9081_LINEOUT_VOL_SHIFT                     0  /* LINEOUT_VOL - [5:0] */
#define WM9081_LINEOUT_VOL_WIDTH                     6  /* LINEOUT_VOL - [5:0] */

/*
 * R3 (0x03) - Analogue Speaker PGA
 */
#define WM9081_SPKPGA_MUTE                      0x0080  /* SPKPGA_MUTE */
#define WM9081_SPKPGA_MUTE_MASK                 0x0080  /* SPKPGA_MUTE */
#define WM9081_SPKPGA_MUTE_SHIFT                     7  /* SPKPGA_MUTE */
#define WM9081_SPKPGA_MUTE_WIDTH                     1  /* SPKPGA_MUTE */
#define WM9081_SPKPGAZC                         0x0040  /* SPKPGAZC */
#define WM9081_SPKPGAZC_MASK                    0x0040  /* SPKPGAZC */
#define WM9081_SPKPGAZC_SHIFT                        6  /* SPKPGAZC */
#define WM9081_SPKPGAZC_WIDTH                        1  /* SPKPGAZC */
#define WM9081_SPKPGA_VOL_MASK                  0x003F  /* SPKPGA_VOL - [5:0] */
#define WM9081_SPKPGA_VOL_SHIFT                      0  /* SPKPGA_VOL - [5:0] */
#define WM9081_SPKPGA_VOL_WIDTH                      6  /* SPKPGA_VOL - [5:0] */

/*
 * R4 (0x04) - VMID Control
 */
#define WM9081_VMID_BUF_ENA                     0x0020  /* VMID_BUF_ENA */
#define WM9081_VMID_BUF_ENA_MASK                0x0020  /* VMID_BUF_ENA */
#define WM9081_VMID_BUF_ENA_SHIFT                    5  /* VMID_BUF_ENA */
#define WM9081_VMID_BUF_ENA_WIDTH                    1  /* VMID_BUF_ENA */
#define WM9081_VMID_RAMP                        0x0008  /* VMID_RAMP */
#define WM9081_VMID_RAMP_MASK                   0x0008  /* VMID_RAMP */
#define WM9081_VMID_RAMP_SHIFT                       3  /* VMID_RAMP */
#define WM9081_VMID_RAMP_WIDTH                       1  /* VMID_RAMP */
#define WM9081_VMID_SEL_MASK                    0x0006  /* VMID_SEL - [2:1] */
#define WM9081_VMID_SEL_SHIFT                        1  /* VMID_SEL - [2:1] */
#define WM9081_VMID_SEL_WIDTH                        2  /* VMID_SEL - [2:1] */
#define WM9081_VMID_FAST_ST                     0x0001  /* VMID_FAST_ST */
#define WM9081_VMID_FAST_ST_MASK                0x0001  /* VMID_FAST_ST */
#define WM9081_VMID_FAST_ST_SHIFT                    0  /* VMID_FAST_ST */
#define WM9081_VMID_FAST_ST_WIDTH                    1  /* VMID_FAST_ST */

/*
 * R5 (0x05) - Bias Control 1
 */
#define WM9081_BIAS_SRC                         0x0040  /* BIAS_SRC */
#define WM9081_BIAS_SRC_MASK                    0x0040  /* BIAS_SRC */
#define WM9081_BIAS_SRC_SHIFT                        6  /* BIAS_SRC */
#define WM9081_BIAS_SRC_WIDTH                        1  /* BIAS_SRC */
#define WM9081_STBY_BIAS_LVL                    0x0020  /* STBY_BIAS_LVL */
#define WM9081_STBY_BIAS_LVL_MASK               0x0020  /* STBY_BIAS_LVL */
#define WM9081_STBY_BIAS_LVL_SHIFT                   5  /* STBY_BIAS_LVL */
#define WM9081_STBY_BIAS_LVL_WIDTH                   1  /* STBY_BIAS_LVL */
#define WM9081_STBY_BIAS_ENA                    0x0010  /* STBY_BIAS_ENA */
#define WM9081_STBY_BIAS_ENA_MASK               0x0010  /* STBY_BIAS_ENA */
#define WM9081_STBY_BIAS_ENA_SHIFT                   4  /* STBY_BIAS_ENA */
#define WM9081_STBY_BIAS_ENA_WIDTH                   1  /* STBY_BIAS_ENA */
#define WM9081_BIAS_LVL_MASK                    0x000C  /* BIAS_LVL - [3:2] */
#define WM9081_BIAS_LVL_SHIFT                        2  /* BIAS_LVL - [3:2] */
#define WM9081_BIAS_LVL_WIDTH                        2  /* BIAS_LVL - [3:2] */
#define WM9081_BIAS_ENA                         0x0002  /* BIAS_ENA */
#define WM9081_BIAS_ENA_MASK                    0x0002  /* BIAS_ENA */
#define WM9081_BIAS_ENA_SHIFT                        1  /* BIAS_ENA */
#define WM9081_BIAS_ENA_WIDTH                        1  /* BIAS_ENA */
#define WM9081_STARTUP_BIAS_ENA                 0x0001  /* STARTUP_BIAS_ENA */
#define WM9081_STARTUP_BIAS_ENA_MASK            0x0001  /* STARTUP_BIAS_ENA */
#define WM9081_STARTUP_BIAS_ENA_SHIFT                0  /* STARTUP_BIAS_ENA */
#define WM9081_STARTUP_BIAS_ENA_WIDTH                1  /* STARTUP_BIAS_ENA */

/*
 * R7 (0x07) - Analogue Mixer
 */
#define WM9081_DAC_SEL                          0x0010  /* DAC_SEL */
#define WM9081_DAC_SEL_MASK                     0x0010  /* DAC_SEL */
#define WM9081_DAC_SEL_SHIFT                         4  /* DAC_SEL */
#define WM9081_DAC_SEL_WIDTH                         1  /* DAC_SEL */
#define WM9081_IN2_VOL                          0x0008  /* IN2_VOL */
#define WM9081_IN2_VOL_MASK                     0x0008  /* IN2_VOL */
#define WM9081_IN2_VOL_SHIFT                         3  /* IN2_VOL */
#define WM9081_IN2_VOL_WIDTH                         1  /* IN2_VOL */
#define WM9081_IN2_ENA                          0x0004  /* IN2_ENA */
#define WM9081_IN2_ENA_MASK                     0x0004  /* IN2_ENA */
#define WM9081_IN2_ENA_SHIFT                         2  /* IN2_ENA */
#define WM9081_IN2_ENA_WIDTH                         1  /* IN2_ENA */
#define WM9081_IN1_VOL                          0x0002  /* IN1_VOL */
#define WM9081_IN1_VOL_MASK                     0x0002  /* IN1_VOL */
#define WM9081_IN1_VOL_SHIFT                         1  /* IN1_VOL */
#define WM9081_IN1_VOL_WIDTH                         1  /* IN1_VOL */
#define WM9081_IN1_ENA                          0x0001  /* IN1_ENA */
#define WM9081_IN1_ENA_MASK                     0x0001  /* IN1_ENA */
#define WM9081_IN1_ENA_SHIFT                         0  /* IN1_ENA */
#define WM9081_IN1_ENA_WIDTH                         1  /* IN1_ENA */

/*
 * R8 (0x08) - Anti Pop Control
 */
#define WM9081_LINEOUT_DISCH                    0x0004  /* LINEOUT_DISCH */
#define WM9081_LINEOUT_DISCH_MASK               0x0004  /* LINEOUT_DISCH */
#define WM9081_LINEOUT_DISCH_SHIFT                   2  /* LINEOUT_DISCH */
#define WM9081_LINEOUT_DISCH_WIDTH                   1  /* LINEOUT_DISCH */
#define WM9081_LINEOUT_VROI                     0x0002  /* LINEOUT_VROI */
#define WM9081_LINEOUT_VROI_MASK                0x0002  /* LINEOUT_VROI */
#define WM9081_LINEOUT_VROI_SHIFT                    1  /* LINEOUT_VROI */
#define WM9081_LINEOUT_VROI_WIDTH                    1  /* LINEOUT_VROI */
#define WM9081_LINEOUT_CLAMP                    0x0001  /* LINEOUT_CLAMP */
#define WM9081_LINEOUT_CLAMP_MASK               0x0001  /* LINEOUT_CLAMP */
#define WM9081_LINEOUT_CLAMP_SHIFT                   0  /* LINEOUT_CLAMP */
#define WM9081_LINEOUT_CLAMP_WIDTH                   1  /* LINEOUT_CLAMP */

/*
 * R9 (0x09) - Analogue Speaker 1
 */
#define WM9081_SPK_DCGAIN_MASK                  0x0038  /* SPK_DCGAIN - [5:3] */
#define WM9081_SPK_DCGAIN_SHIFT                      3  /* SPK_DCGAIN - [5:3] */
#define WM9081_SPK_DCGAIN_WIDTH                      3  /* SPK_DCGAIN - [5:3] */
#define WM9081_SPK_ACGAIN_MASK                  0x0007  /* SPK_ACGAIN - [2:0] */
#define WM9081_SPK_ACGAIN_SHIFT                      0  /* SPK_ACGAIN - [2:0] */
#define WM9081_SPK_ACGAIN_WIDTH                      3  /* SPK_ACGAIN - [2:0] */

/*
 * R10 (0x0A) - Analogue Speaker 2
 */
#define WM9081_SPK_MODE                         0x0040  /* SPK_MODE */
#define WM9081_SPK_MODE_MASK                    0x0040  /* SPK_MODE */
#define WM9081_SPK_MODE_SHIFT                        6  /* SPK_MODE */
#define WM9081_SPK_MODE_WIDTH                        1  /* SPK_MODE */
#define WM9081_SPK_INV_MUTE                     0x0010  /* SPK_INV_MUTE */
#define WM9081_SPK_INV_MUTE_MASK                0x0010  /* SPK_INV_MUTE */
#define WM9081_SPK_INV_MUTE_SHIFT                    4  /* SPK_INV_MUTE */
#define WM9081_SPK_INV_MUTE_WIDTH                    1  /* SPK_INV_MUTE */
#define WM9081_OUT_SPK_CTRL                     0x0008  /* OUT_SPK_CTRL */
#define WM9081_OUT_SPK_CTRL_MASK                0x0008  /* OUT_SPK_CTRL */
#define WM9081_OUT_SPK_CTRL_SHIFT                    3  /* OUT_SPK_CTRL */
#define WM9081_OUT_SPK_CTRL_WIDTH                    1  /* OUT_SPK_CTRL */

/*
 * R11 (0x0B) - Power Management
 */
#define WM9081_TSHUT_ENA                        0x0100  /* TSHUT_ENA */
#define WM9081_TSHUT_ENA_MASK                   0x0100  /* TSHUT_ENA */
#define WM9081_TSHUT_ENA_SHIFT                       8  /* TSHUT_ENA */
#define WM9081_TSHUT_ENA_WIDTH                       1  /* TSHUT_ENA */
#define WM9081_TSENSE_ENA                       0x0080  /* TSENSE_ENA */
#define WM9081_TSENSE_ENA_MASK                  0x0080  /* TSENSE_ENA */
#define WM9081_TSENSE_ENA_SHIFT                      7  /* TSENSE_ENA */
#define WM9081_TSENSE_ENA_WIDTH                      1  /* TSENSE_ENA */
#define WM9081_TEMP_SHUT                        0x0040  /* TEMP_SHUT */
#define WM9081_TEMP_SHUT_MASK                   0x0040  /* TEMP_SHUT */
#define WM9081_TEMP_SHUT_SHIFT                       6  /* TEMP_SHUT */
#define WM9081_TEMP_SHUT_WIDTH                       1  /* TEMP_SHUT */
#define WM9081_LINEOUT_ENA                      0x0010  /* LINEOUT_ENA */
#define WM9081_LINEOUT_ENA_MASK                 0x0010  /* LINEOUT_ENA */
#define WM9081_LINEOUT_ENA_SHIFT                     4  /* LINEOUT_ENA */
#define WM9081_LINEOUT_ENA_WIDTH                     1  /* LINEOUT_ENA */
#define WM9081_SPKPGA_ENA                       0x0004  /* SPKPGA_ENA */
#define WM9081_SPKPGA_ENA_MASK                  0x0004  /* SPKPGA_ENA */
#define WM9081_SPKPGA_ENA_SHIFT                      2  /* SPKPGA_ENA */
#define WM9081_SPKPGA_ENA_WIDTH                      1  /* SPKPGA_ENA */
#define WM9081_SPK_ENA                          0x0002  /* SPK_ENA */
#define WM9081_SPK_ENA_MASK                     0x0002  /* SPK_ENA */
#define WM9081_SPK_ENA_SHIFT                         1  /* SPK_ENA */
#define WM9081_SPK_ENA_WIDTH                         1  /* SPK_ENA */
#define WM9081_DAC_ENA                          0x0001  /* DAC_ENA */
#define WM9081_DAC_ENA_MASK                     0x0001  /* DAC_ENA */
#define WM9081_DAC_ENA_SHIFT                         0  /* DAC_ENA */
#define WM9081_DAC_ENA_WIDTH                         1  /* DAC_ENA */

/*
 * R12 (0x0C) - Clock Control 1
 */
#define WM9081_CLK_OP_DIV_MASK                  0x1C00  /* CLK_OP_DIV - [12:10] */
#define WM9081_CLK_OP_DIV_SHIFT                     10  /* CLK_OP_DIV - [12:10] */
#define WM9081_CLK_OP_DIV_WIDTH                      3  /* CLK_OP_DIV - [12:10] */
#define WM9081_CLK_TO_DIV_MASK                  0x0300  /* CLK_TO_DIV - [9:8] */
#define WM9081_CLK_TO_DIV_SHIFT                      8  /* CLK_TO_DIV - [9:8] */
#define WM9081_CLK_TO_DIV_WIDTH                      2  /* CLK_TO_DIV - [9:8] */
#define WM9081_MCLKDIV2                         0x0080  /* MCLKDIV2 */
#define WM9081_MCLKDIV2_MASK                    0x0080  /* MCLKDIV2 */
#define WM9081_MCLKDIV2_SHIFT                        7  /* MCLKDIV2 */
#define WM9081_MCLKDIV2_WIDTH                        1  /* MCLKDIV2 */

/*
 * R13 (0x0D) - Clock Control 2
 */
#define WM9081_CLK_SYS_RATE_MASK                0x00F0  /* CLK_SYS_RATE - [7:4] */
#define WM9081_CLK_SYS_RATE_SHIFT                    4  /* CLK_SYS_RATE - [7:4] */
#define WM9081_CLK_SYS_RATE_WIDTH                    4  /* CLK_SYS_RATE - [7:4] */
#define WM9081_SAMPLE_RATE_MASK                 0x000F  /* SAMPLE_RATE - [3:0] */
#define WM9081_SAMPLE_RATE_SHIFT                     0  /* SAMPLE_RATE - [3:0] */
#define WM9081_SAMPLE_RATE_WIDTH                     4  /* SAMPLE_RATE - [3:0] */

/*
 * R14 (0x0E) - Clock Control 3
 */
#define WM9081_CLK_SRC_SEL                      0x2000  /* CLK_SRC_SEL */
#define WM9081_CLK_SRC_SEL_MASK                 0x2000  /* CLK_SRC_SEL */
#define WM9081_CLK_SRC_SEL_SHIFT                    13  /* CLK_SRC_SEL */
#define WM9081_CLK_SRC_SEL_WIDTH                     1  /* CLK_SRC_SEL */
#define WM9081_CLK_OP_ENA                       0x0020  /* CLK_OP_ENA */
#define WM9081_CLK_OP_ENA_MASK                  0x0020  /* CLK_OP_ENA */
#define WM9081_CLK_OP_ENA_SHIFT                      5  /* CLK_OP_ENA */
#define WM9081_CLK_OP_ENA_WIDTH                      1  /* CLK_OP_ENA */
#define WM9081_CLK_TO_ENA                       0x0004  /* CLK_TO_ENA */
#define WM9081_CLK_TO_ENA_MASK                  0x0004  /* CLK_TO_ENA */
#define WM9081_CLK_TO_ENA_SHIFT                      2  /* CLK_TO_ENA */
#define WM9081_CLK_TO_ENA_WIDTH                      1  /* CLK_TO_ENA */
#define WM9081_CLK_DSP_ENA                      0x0002  /* CLK_DSP_ENA */
#define WM9081_CLK_DSP_ENA_MASK                 0x0002  /* CLK_DSP_ENA */
#define WM9081_CLK_DSP_ENA_SHIFT                     1  /* CLK_DSP_ENA */
#define WM9081_CLK_DSP_ENA_WIDTH                     1  /* CLK_DSP_ENA */
#define WM9081_CLK_SYS_ENA                      0x0001  /* CLK_SYS_ENA */
#define WM9081_CLK_SYS_ENA_MASK                 0x0001  /* CLK_SYS_ENA */
#define WM9081_CLK_SYS_ENA_SHIFT                     0  /* CLK_SYS_ENA */
#define WM9081_CLK_SYS_ENA_WIDTH                     1  /* CLK_SYS_ENA */

/*
 * R16 (0x10) - FLL Control 1
 */
#define WM9081_FLL_HOLD                         0x0008  /* FLL_HOLD */
#define WM9081_FLL_HOLD_MASK                    0x0008  /* FLL_HOLD */
#define WM9081_FLL_HOLD_SHIFT                        3  /* FLL_HOLD */
#define WM9081_FLL_HOLD_WIDTH                        1  /* FLL_HOLD */
#define WM9081_FLL_FRAC                         0x0004  /* FLL_FRAC */
#define WM9081_FLL_FRAC_MASK                    0x0004  /* FLL_FRAC */
#define WM9081_FLL_FRAC_SHIFT                        2  /* FLL_FRAC */
#define WM9081_FLL_FRAC_WIDTH                        1  /* FLL_FRAC */
#define WM9081_FLL_ENA                          0x0001  /* FLL_ENA */
#define WM9081_FLL_ENA_MASK                     0x0001  /* FLL_ENA */
#define WM9081_FLL_ENA_SHIFT                         0  /* FLL_ENA */
#define WM9081_FLL_ENA_WIDTH                         1  /* FLL_ENA */

/*
 * R17 (0x11) - FLL Control 2
 */
#define WM9081_FLL_OUTDIV_MASK                  0x0700  /* FLL_OUTDIV - [10:8] */
#define WM9081_FLL_OUTDIV_SHIFT                      8  /* FLL_OUTDIV - [10:8] */
#define WM9081_FLL_OUTDIV_WIDTH                      3  /* FLL_OUTDIV - [10:8] */
#define WM9081_FLL_CTRL_RATE_MASK               0x0070  /* FLL_CTRL_RATE - [6:4] */
#define WM9081_FLL_CTRL_RATE_SHIFT                   4  /* FLL_CTRL_RATE - [6:4] */
#define WM9081_FLL_CTRL_RATE_WIDTH                   3  /* FLL_CTRL_RATE - [6:4] */
#define WM9081_FLL_FRATIO_MASK                  0x0007  /* FLL_FRATIO - [2:0] */
#define WM9081_FLL_FRATIO_SHIFT                      0  /* FLL_FRATIO - [2:0] */
#define WM9081_FLL_FRATIO_WIDTH                      3  /* FLL_FRATIO - [2:0] */

/*
 * R18 (0x12) - FLL Control 3
 */
#define WM9081_FLL_K_MASK                       0xFFFF  /* FLL_K - [15:0] */
#define WM9081_FLL_K_SHIFT                           0  /* FLL_K - [15:0] */
#define WM9081_FLL_K_WIDTH                          16  /* FLL_K - [15:0] */

/*
 * R19 (0x13) - FLL Control 4
 */
#define WM9081_FLL_N_MASK                       0x7FE0  /* FLL_N - [14:5] */
#define WM9081_FLL_N_SHIFT                           5  /* FLL_N - [14:5] */
#define WM9081_FLL_N_WIDTH                          10  /* FLL_N - [14:5] */
#define WM9081_FLL_GAIN_MASK                    0x000F  /* FLL_GAIN - [3:0] */
#define WM9081_FLL_GAIN_SHIFT                        0  /* FLL_GAIN - [3:0] */
#define WM9081_FLL_GAIN_WIDTH                        4  /* FLL_GAIN - [3:0] */

/*
 * R20 (0x14) - FLL Control 5
 */
#define WM9081_FLL_CLK_REF_DIV_MASK             0x0018  /* FLL_CLK_REF_DIV - [4:3] */
#define WM9081_FLL_CLK_REF_DIV_SHIFT                 3  /* FLL_CLK_REF_DIV - [4:3] */
#define WM9081_FLL_CLK_REF_DIV_WIDTH                 2  /* FLL_CLK_REF_DIV - [4:3] */
#define WM9081_FLL_CLK_SRC_MASK                 0x0003  /* FLL_CLK_SRC - [1:0] */
#define WM9081_FLL_CLK_SRC_SHIFT                     0  /* FLL_CLK_SRC - [1:0] */
#define WM9081_FLL_CLK_SRC_WIDTH                     2  /* FLL_CLK_SRC - [1:0] */

/*
 * R22 (0x16) - Audio Interface 1
 */
#define WM9081_AIFDAC_CHAN                      0x0040  /* AIFDAC_CHAN */
#define WM9081_AIFDAC_CHAN_MASK                 0x0040  /* AIFDAC_CHAN */
#define WM9081_AIFDAC_CHAN_SHIFT                     6  /* AIFDAC_CHAN */
#define WM9081_AIFDAC_CHAN_WIDTH                     1  /* AIFDAC_CHAN */
#define WM9081_AIFDAC_TDM_SLOT_MASK             0x0030  /* AIFDAC_TDM_SLOT - [5:4] */
#define WM9081_AIFDAC_TDM_SLOT_SHIFT                 4  /* AIFDAC_TDM_SLOT - [5:4] */
#define WM9081_AIFDAC_TDM_SLOT_WIDTH                 2  /* AIFDAC_TDM_SLOT - [5:4] */
#define WM9081_AIFDAC_TDM_MODE_MASK             0x000C  /* AIFDAC_TDM_MODE - [3:2] */
#define WM9081_AIFDAC_TDM_MODE_SHIFT                 2  /* AIFDAC_TDM_MODE - [3:2] */
#define WM9081_AIFDAC_TDM_MODE_WIDTH                 2  /* AIFDAC_TDM_MODE - [3:2] */
#define WM9081_DAC_COMP                         0x0002  /* DAC_COMP */
#define WM9081_DAC_COMP_MASK                    0x0002  /* DAC_COMP */
#define WM9081_DAC_COMP_SHIFT                        1  /* DAC_COMP */
#define WM9081_DAC_COMP_WIDTH                        1  /* DAC_COMP */
#define WM9081_DAC_COMPMODE                     0x0001  /* DAC_COMPMODE */
#define WM9081_DAC_COMPMODE_MASK                0x0001  /* DAC_COMPMODE */
#define WM9081_DAC_COMPMODE_SHIFT                    0  /* DAC_COMPMODE */
#define WM9081_DAC_COMPMODE_WIDTH                    1  /* DAC_COMPMODE */

/*
 * R23 (0x17) - Audio Interface 2
 */
#define WM9081_AIF_TRIS                         0x0200  /* AIF_TRIS */
#define WM9081_AIF_TRIS_MASK                    0x0200  /* AIF_TRIS */
#define WM9081_AIF_TRIS_SHIFT                        9  /* AIF_TRIS */
#define WM9081_AIF_TRIS_WIDTH                        1  /* AIF_TRIS */
#define WM9081_DAC_DAT_INV                      0x0100  /* DAC_DAT_INV */
#define WM9081_DAC_DAT_INV_MASK                 0x0100  /* DAC_DAT_INV */
#define WM9081_DAC_DAT_INV_SHIFT                     8  /* DAC_DAT_INV */
#define WM9081_DAC_DAT_INV_WIDTH                     1  /* DAC_DAT_INV */
#define WM9081_AIF_BCLK_INV                     0x0080  /* AIF_BCLK_INV */
#define WM9081_AIF_BCLK_INV_MASK                0x0080  /* AIF_BCLK_INV */
#define WM9081_AIF_BCLK_INV_SHIFT                    7  /* AIF_BCLK_INV */
#define WM9081_AIF_BCLK_INV_WIDTH                    1  /* AIF_BCLK_INV */
#define WM9081_BCLK_DIR                         0x0040  /* BCLK_DIR */
#define WM9081_BCLK_DIR_MASK                    0x0040  /* BCLK_DIR */
#define WM9081_BCLK_DIR_SHIFT                        6  /* BCLK_DIR */
#define WM9081_BCLK_DIR_WIDTH                        1  /* BCLK_DIR */
#define WM9081_LRCLK_DIR                        0x0020  /* LRCLK_DIR */
#define WM9081_LRCLK_DIR_MASK                   0x0020  /* LRCLK_DIR */
#define WM9081_LRCLK_DIR_SHIFT                       5  /* LRCLK_DIR */
#define WM9081_LRCLK_DIR_WIDTH                       1  /* LRCLK_DIR */
#define WM9081_AIF_LRCLK_INV                    0x0010  /* AIF_LRCLK_INV */
#define WM9081_AIF_LRCLK_INV_MASK               0x0010  /* AIF_LRCLK_INV */
#define WM9081_AIF_LRCLK_INV_SHIFT                   4  /* AIF_LRCLK_INV */
#define WM9081_AIF_LRCLK_INV_WIDTH                   1  /* AIF_LRCLK_INV */
#define WM9081_AIF_WL_MASK                      0x000C  /* AIF_WL - [3:2] */
#define WM9081_AIF_WL_SHIFT                          2  /* AIF_WL - [3:2] */
#define WM9081_AIF_WL_WIDTH                          2  /* AIF_WL - [3:2] */
#define WM9081_AIF_FMT_MASK                     0x0003  /* AIF_FMT - [1:0] */
#define WM9081_AIF_FMT_SHIFT                         0  /* AIF_FMT - [1:0] */
#define WM9081_AIF_FMT_WIDTH                         2  /* AIF_FMT - [1:0] */

/*
 * R24 (0x18) - Audio Interface 3
 */
#define WM9081_BCLK_DIV_MASK                    0x001F  /* BCLK_DIV - [4:0] */
#define WM9081_BCLK_DIV_SHIFT                        0  /* BCLK_DIV - [4:0] */
#define WM9081_BCLK_DIV_WIDTH                        5  /* BCLK_DIV - [4:0] */

/*
 * R25 (0x19) - Audio Interface 4
 */
#define WM9081_LRCLK_RATE_MASK                  0x07FF  /* LRCLK_RATE - [10:0] */
#define WM9081_LRCLK_RATE_SHIFT                      0  /* LRCLK_RATE - [10:0] */
#define WM9081_LRCLK_RATE_WIDTH                     11  /* LRCLK_RATE - [10:0] */

/*
 * R26 (0x1A) - Interrupt Status
 */
#define WM9081_WSEQ_BUSY_EINT                   0x0004  /* WSEQ_BUSY_EINT */
#define WM9081_WSEQ_BUSY_EINT_MASK              0x0004  /* WSEQ_BUSY_EINT */
#define WM9081_WSEQ_BUSY_EINT_SHIFT                  2  /* WSEQ_BUSY_EINT */
#define WM9081_WSEQ_BUSY_EINT_WIDTH                  1  /* WSEQ_BUSY_EINT */
#define WM9081_TSHUT_EINT                       0x0001  /* TSHUT_EINT */
#define WM9081_TSHUT_EINT_MASK                  0x0001  /* TSHUT_EINT */
#define WM9081_TSHUT_EINT_SHIFT                      0  /* TSHUT_EINT */
#define WM9081_TSHUT_EINT_WIDTH                      1  /* TSHUT_EINT */

/*
 * R27 (0x1B) - Interrupt Status Mask
 */
#define WM9081_IM_WSEQ_BUSY_EINT                0x0004  /* IM_WSEQ_BUSY_EINT */
#define WM9081_IM_WSEQ_BUSY_EINT_MASK           0x0004  /* IM_WSEQ_BUSY_EINT */
#define WM9081_IM_WSEQ_BUSY_EINT_SHIFT               2  /* IM_WSEQ_BUSY_EINT */
#define WM9081_IM_WSEQ_BUSY_EINT_WIDTH               1  /* IM_WSEQ_BUSY_EINT */
#define WM9081_IM_TSHUT_EINT                    0x0001  /* IM_TSHUT_EINT */
#define WM9081_IM_TSHUT_EINT_MASK               0x0001  /* IM_TSHUT_EINT */
#define WM9081_IM_TSHUT_EINT_SHIFT                   0  /* IM_TSHUT_EINT */
#define WM9081_IM_TSHUT_EINT_WIDTH                   1  /* IM_TSHUT_EINT */

/*
 * R28 (0x1C) - Interrupt Polarity
 */
#define WM9081_TSHUT_INV                        0x0001  /* TSHUT_INV */
#define WM9081_TSHUT_INV_MASK                   0x0001  /* TSHUT_INV */
#define WM9081_TSHUT_INV_SHIFT                       0  /* TSHUT_INV */
#define WM9081_TSHUT_INV_WIDTH                       1  /* TSHUT_INV */

/*
 * R29 (0x1D) - Interrupt Control
 */
#define WM9081_IRQ_POL                          0x8000  /* IRQ_POL */
#define WM9081_IRQ_POL_MASK                     0x8000  /* IRQ_POL */
#define WM9081_IRQ_POL_SHIFT                        15  /* IRQ_POL */
#define WM9081_IRQ_POL_WIDTH                         1  /* IRQ_POL */
#define WM9081_IRQ_OP_CTRL                      0x0001  /* IRQ_OP_CTRL */
#define WM9081_IRQ_OP_CTRL_MASK                 0x0001  /* IRQ_OP_CTRL */
#define WM9081_IRQ_OP_CTRL_SHIFT                     0  /* IRQ_OP_CTRL */
#define WM9081_IRQ_OP_CTRL_WIDTH                     1  /* IRQ_OP_CTRL */

/*
 * R30 (0x1E) - DAC Digital 1
 */
#define WM9081_DAC_VOL_MASK                     0x00FF  /* DAC_VOL - [7:0] */
#define WM9081_DAC_VOL_SHIFT                         0  /* DAC_VOL - [7:0] */
#define WM9081_DAC_VOL_WIDTH                         8  /* DAC_VOL - [7:0] */

/*
 * R31 (0x1F) - DAC Digital 2
 */
#define WM9081_DAC_MUTERATE                     0x0400  /* DAC_MUTERATE */
#define WM9081_DAC_MUTERATE_MASK                0x0400  /* DAC_MUTERATE */
#define WM9081_DAC_MUTERATE_SHIFT                   10  /* DAC_MUTERATE */
#define WM9081_DAC_MUTERATE_WIDTH                    1  /* DAC_MUTERATE */
#define WM9081_DAC_MUTEMODE                     0x0200  /* DAC_MUTEMODE */
#define WM9081_DAC_MUTEMODE_MASK                0x0200  /* DAC_MUTEMODE */
#define WM9081_DAC_MUTEMODE_SHIFT                    9  /* DAC_MUTEMODE */
#define WM9081_DAC_MUTEMODE_WIDTH                    1  /* DAC_MUTEMODE */
#define WM9081_DAC_MUTE                         0x0008  /* DAC_MUTE */
#define WM9081_DAC_MUTE_MASK                    0x0008  /* DAC_MUTE */
#define WM9081_DAC_MUTE_SHIFT                        3  /* DAC_MUTE */
#define WM9081_DAC_MUTE_WIDTH                        1  /* DAC_MUTE */
#define WM9081_DEEMPH_MASK                      0x0006  /* DEEMPH - [2:1] */
#define WM9081_DEEMPH_SHIFT                          1  /* DEEMPH - [2:1] */
#define WM9081_DEEMPH_WIDTH                          2  /* DEEMPH - [2:1] */

/*
 * R32 (0x20) - DRC 1
 */
#define WM9081_DRC_ENA                          0x8000  /* DRC_ENA */
#define WM9081_DRC_ENA_MASK                     0x8000  /* DRC_ENA */
#define WM9081_DRC_ENA_SHIFT                        15  /* DRC_ENA */
#define WM9081_DRC_ENA_WIDTH                         1  /* DRC_ENA */
#define WM9081_DRC_STARTUP_GAIN_MASK            0x07C0  /* DRC_STARTUP_GAIN - [10:6] */
#define WM9081_DRC_STARTUP_GAIN_SHIFT                6  /* DRC_STARTUP_GAIN - [10:6] */
#define WM9081_DRC_STARTUP_GAIN_WIDTH                5  /* DRC_STARTUP_GAIN - [10:6] */
#define WM9081_DRC_FF_DLY                       0x0020  /* DRC_FF_DLY */
#define WM9081_DRC_FF_DLY_MASK                  0x0020  /* DRC_FF_DLY */
#define WM9081_DRC_FF_DLY_SHIFT                      5  /* DRC_FF_DLY */
#define WM9081_DRC_FF_DLY_WIDTH                      1  /* DRC_FF_DLY */
#define WM9081_DRC_QR                           0x0004  /* DRC_QR */
#define WM9081_DRC_QR_MASK                      0x0004  /* DRC_QR */
#define WM9081_DRC_QR_SHIFT                          2  /* DRC_QR */
#define WM9081_DRC_QR_WIDTH                          1  /* DRC_QR */
#define WM9081_DRC_ANTICLIP                     0x0002  /* DRC_ANTICLIP */
#define WM9081_DRC_ANTICLIP_MASK                0x0002  /* DRC_ANTICLIP */
#define WM9081_DRC_ANTICLIP_SHIFT                    1  /* DRC_ANTICLIP */
#define WM9081_DRC_ANTICLIP_WIDTH                    1  /* DRC_ANTICLIP */

/*
 * R33 (0x21) - DRC 2
 */
#define WM9081_DRC_ATK_MASK                     0xF000  /* DRC_ATK - [15:12] */
#define WM9081_DRC_ATK_SHIFT                        12  /* DRC_ATK - [15:12] */
#define WM9081_DRC_ATK_WIDTH                         4  /* DRC_ATK - [15:12] */
#define WM9081_DRC_DCY_MASK                     0x0F00  /* DRC_DCY - [11:8] */
#define WM9081_DRC_DCY_SHIFT                         8  /* DRC_DCY - [11:8] */
#define WM9081_DRC_DCY_WIDTH                         4  /* DRC_DCY - [11:8] */
#define WM9081_DRC_QR_THR_MASK                  0x00C0  /* DRC_QR_THR - [7:6] */
#define WM9081_DRC_QR_THR_SHIFT                      6  /* DRC_QR_THR - [7:6] */
#define WM9081_DRC_QR_THR_WIDTH                      2  /* DRC_QR_THR - [7:6] */
#define WM9081_DRC_QR_DCY_MASK                  0x0030  /* DRC_QR_DCY - [5:4] */
#define WM9081_DRC_QR_DCY_SHIFT                      4  /* DRC_QR_DCY - [5:4] */
#define WM9081_DRC_QR_DCY_WIDTH                      2  /* DRC_QR_DCY - [5:4] */
#define WM9081_DRC_MINGAIN_MASK                 0x000C  /* DRC_MINGAIN - [3:2] */
#define WM9081_DRC_MINGAIN_SHIFT                     2  /* DRC_MINGAIN - [3:2] */
#define WM9081_DRC_MINGAIN_WIDTH                     2  /* DRC_MINGAIN - [3:2] */
#define WM9081_DRC_MAXGAIN_MASK                 0x0003  /* DRC_MAXGAIN - [1:0] */
#define WM9081_DRC_MAXGAIN_SHIFT                     0  /* DRC_MAXGAIN - [1:0] */
#define WM9081_DRC_MAXGAIN_WIDTH                     2  /* DRC_MAXGAIN - [1:0] */

/*
 * R34 (0x22) - DRC 3
 */
#define WM9081_DRC_HI_COMP_MASK                 0x0038  /* DRC_HI_COMP - [5:3] */
#define WM9081_DRC_HI_COMP_SHIFT                     3  /* DRC_HI_COMP - [5:3] */
#define WM9081_DRC_HI_COMP_WIDTH                     3  /* DRC_HI_COMP - [5:3] */
#define WM9081_DRC_LO_COMP_MASK                 0x0007  /* DRC_LO_COMP - [2:0] */
#define WM9081_DRC_LO_COMP_SHIFT                     0  /* DRC_LO_COMP - [2:0] */
#define WM9081_DRC_LO_COMP_WIDTH                     3  /* DRC_LO_COMP - [2:0] */

/*
 * R35 (0x23) - DRC 4
 */
#define WM9081_DRC_KNEE_IP_MASK                 0x07E0  /* DRC_KNEE_IP - [10:5] */
#define WM9081_DRC_KNEE_IP_SHIFT                     5  /* DRC_KNEE_IP - [10:5] */
#define WM9081_DRC_KNEE_IP_WIDTH                     6  /* DRC_KNEE_IP - [10:5] */
#define WM9081_DRC_KNEE_OP_MASK                 0x001F  /* DRC_KNEE_OP - [4:0] */
#define WM9081_DRC_KNEE_OP_SHIFT                     0  /* DRC_KNEE_OP - [4:0] */
#define WM9081_DRC_KNEE_OP_WIDTH                     5  /* DRC_KNEE_OP - [4:0] */

/*
 * R38 (0x26) - Write Sequencer 1
 */
#define WM9081_WSEQ_ENA                         0x8000  /* WSEQ_ENA */
#define WM9081_WSEQ_ENA_MASK                    0x8000  /* WSEQ_ENA */
#define WM9081_WSEQ_ENA_SHIFT                       15  /* WSEQ_ENA */
#define WM9081_WSEQ_ENA_WIDTH                        1  /* WSEQ_ENA */
#define WM9081_WSEQ_ABORT                       0x0200  /* WSEQ_ABORT */
#define WM9081_WSEQ_ABORT_MASK                  0x0200  /* WSEQ_ABORT */
#define WM9081_WSEQ_ABORT_SHIFT                      9  /* WSEQ_ABORT */
#define WM9081_WSEQ_ABORT_WIDTH                      1  /* WSEQ_ABORT */
#define WM9081_WSEQ_START                       0x0100  /* WSEQ_START */
#define WM9081_WSEQ_START_MASK                  0x0100  /* WSEQ_START */
#define WM9081_WSEQ_START_SHIFT                      8  /* WSEQ_START */
#define WM9081_WSEQ_START_WIDTH                      1  /* WSEQ_START */
#define WM9081_WSEQ_START_INDEX_MASK            0x007F  /* WSEQ_START_INDEX - [6:0] */
#define WM9081_WSEQ_START_INDEX_SHIFT                0  /* WSEQ_START_INDEX - [6:0] */
#define WM9081_WSEQ_START_INDEX_WIDTH                7  /* WSEQ_START_INDEX - [6:0] */

/*
 * R39 (0x27) - Write Sequencer 2
 */
#define WM9081_WSEQ_CURRENT_INDEX_MASK          0x07F0  /* WSEQ_CURRENT_INDEX - [10:4] */
#define WM9081_WSEQ_CURRENT_INDEX_SHIFT              4  /* WSEQ_CURRENT_INDEX - [10:4] */
#define WM9081_WSEQ_CURRENT_INDEX_WIDTH              7  /* WSEQ_CURRENT_INDEX - [10:4] */
#define WM9081_WSEQ_BUSY                        0x0001  /* WSEQ_BUSY */
#define WM9081_WSEQ_BUSY_MASK                   0x0001  /* WSEQ_BUSY */
#define WM9081_WSEQ_BUSY_SHIFT                       0  /* WSEQ_BUSY */
#define WM9081_WSEQ_BUSY_WIDTH                       1  /* WSEQ_BUSY */

/*
 * R40 (0x28) - MW Slave 1
 */
#define WM9081_SPI_CFG                          0x0020  /* SPI_CFG */
#define WM9081_SPI_CFG_MASK                     0x0020  /* SPI_CFG */
#define WM9081_SPI_CFG_SHIFT                         5  /* SPI_CFG */
#define WM9081_SPI_CFG_WIDTH                         1  /* SPI_CFG */
#define WM9081_SPI_4WIRE                        0x0010  /* SPI_4WIRE */
#define WM9081_SPI_4WIRE_MASK                   0x0010  /* SPI_4WIRE */
#define WM9081_SPI_4WIRE_SHIFT                       4  /* SPI_4WIRE */
#define WM9081_SPI_4WIRE_WIDTH                       1  /* SPI_4WIRE */
#define WM9081_ARA_ENA                          0x0008  /* ARA_ENA */
#define WM9081_ARA_ENA_MASK                     0x0008  /* ARA_ENA */
#define WM9081_ARA_ENA_SHIFT                         3  /* ARA_ENA */
#define WM9081_ARA_ENA_WIDTH                         1  /* ARA_ENA */
#define WM9081_AUTO_INC                         0x0002  /* AUTO_INC */
#define WM9081_AUTO_INC_MASK                    0x0002  /* AUTO_INC */
#define WM9081_AUTO_INC_SHIFT                        1  /* AUTO_INC */
#define WM9081_AUTO_INC_WIDTH                        1  /* AUTO_INC */

/*
 * R42 (0x2A) - EQ 1
 */
#define WM9081_EQ_B1_GAIN_MASK                  0xF800  /* EQ_B1_GAIN - [15:11] */
#define WM9081_EQ_B1_GAIN_SHIFT                     11  /* EQ_B1_GAIN - [15:11] */
#define WM9081_EQ_B1_GAIN_WIDTH                      5  /* EQ_B1_GAIN - [15:11] */
#define WM9081_EQ_B2_GAIN_MASK                  0x07C0  /* EQ_B2_GAIN - [10:6] */
#define WM9081_EQ_B2_GAIN_SHIFT                      6  /* EQ_B2_GAIN - [10:6] */
#define WM9081_EQ_B2_GAIN_WIDTH                      5  /* EQ_B2_GAIN - [10:6] */
#define WM9081_EQ_B4_GAIN_MASK                  0x003E  /* EQ_B4_GAIN - [5:1] */
#define WM9081_EQ_B4_GAIN_SHIFT                      1  /* EQ_B4_GAIN - [5:1] */
#define WM9081_EQ_B4_GAIN_WIDTH                      5  /* EQ_B4_GAIN - [5:1] */
#define WM9081_EQ_ENA                           0x0001  /* EQ_ENA */
#define WM9081_EQ_ENA_MASK                      0x0001  /* EQ_ENA */
#define WM9081_EQ_ENA_SHIFT                          0  /* EQ_ENA */
#define WM9081_EQ_ENA_WIDTH                          1  /* EQ_ENA */

/*
 * R43 (0x2B) - EQ 2
 */
#define WM9081_EQ_B3_GAIN_MASK                  0xF800  /* EQ_B3_GAIN - [15:11] */
#define WM9081_EQ_B3_GAIN_SHIFT                     11  /* EQ_B3_GAIN - [15:11] */
#define WM9081_EQ_B3_GAIN_WIDTH                      5  /* EQ_B3_GAIN - [15:11] */
#define WM9081_EQ_B5_GAIN_MASK                  0x07C0  /* EQ_B5_GAIN - [10:6] */
#define WM9081_EQ_B5_GAIN_SHIFT                      6  /* EQ_B5_GAIN - [10:6] */
#define WM9081_EQ_B5_GAIN_WIDTH                      5  /* EQ_B5_GAIN - [10:6] */

/*
 * R44 (0x2C) - EQ 3
 */
#define WM9081_EQ_B1_A_MASK                     0xFFFF  /* EQ_B1_A - [15:0] */
#define WM9081_EQ_B1_A_SHIFT                         0  /* EQ_B1_A - [15:0] */
#define WM9081_EQ_B1_A_WIDTH                        16  /* EQ_B1_A - [15:0] */

/*
 * R45 (0x2D) - EQ 4
 */
#define WM9081_EQ_B1_B_MASK                     0xFFFF  /* EQ_B1_B - [15:0] */
#define WM9081_EQ_B1_B_SHIFT                         0  /* EQ_B1_B - [15:0] */
#define WM9081_EQ_B1_B_WIDTH                        16  /* EQ_B1_B - [15:0] */

/*
 * R46 (0x2E) - EQ 5
 */
#define WM9081_EQ_B1_PG_MASK                    0xFFFF  /* EQ_B1_PG - [15:0] */
#define WM9081_EQ_B1_PG_SHIFT                        0  /* EQ_B1_PG - [15:0] */
#define WM9081_EQ_B1_PG_WIDTH                       16  /* EQ_B1_PG - [15:0] */

/*
 * R47 (0x2F) - EQ 6
 */
#define WM9081_EQ_B2_A_MASK                     0xFFFF  /* EQ_B2_A - [15:0] */
#define WM9081_EQ_B2_A_SHIFT                         0  /* EQ_B2_A - [15:0] */
#define WM9081_EQ_B2_A_WIDTH                        16  /* EQ_B2_A - [15:0] */

/*
 * R48 (0x30) - EQ 7
 */
#define WM9081_EQ_B2_B_MASK                     0xFFFF  /* EQ_B2_B - [15:0] */
#define WM9081_EQ_B2_B_SHIFT                         0  /* EQ_B2_B - [15:0] */
#define WM9081_EQ_B2_B_WIDTH                        16  /* EQ_B2_B - [15:0] */

/*
 * R49 (0x31) - EQ 8
 */
#define WM9081_EQ_B2_C_MASK                     0xFFFF  /* EQ_B2_C - [15:0] */
#define WM9081_EQ_B2_C_SHIFT                         0  /* EQ_B2_C - [15:0] */
#define WM9081_EQ_B2_C_WIDTH                        16  /* EQ_B2_C - [15:0] */

/*
 * R50 (0x32) - EQ 9
 */
#define WM9081_EQ_B2_PG_MASK                    0xFFFF  /* EQ_B2_PG - [15:0] */
#define WM9081_EQ_B2_PG_SHIFT                        0  /* EQ_B2_PG - [15:0] */
#define WM9081_EQ_B2_PG_WIDTH                       16  /* EQ_B2_PG - [15:0] */

/*
 * R51 (0x33) - EQ 10
 */
#define WM9081_EQ_B4_A_MASK                     0xFFFF  /* EQ_B4_A - [15:0] */
#define WM9081_EQ_B4_A_SHIFT                         0  /* EQ_B4_A - [15:0] */
#define WM9081_EQ_B4_A_WIDTH                        16  /* EQ_B4_A - [15:0] */

/*
 * R52 (0x34) - EQ 11
 */
#define WM9081_EQ_B4_B_MASK                     0xFFFF  /* EQ_B4_B - [15:0] */
#define WM9081_EQ_B4_B_SHIFT                         0  /* EQ_B4_B - [15:0] */
#define WM9081_EQ_B4_B_WIDTH                        16  /* EQ_B4_B - [15:0] */

/*
 * R53 (0x35) - EQ 12
 */
#define WM9081_EQ_B4_C_MASK                     0xFFFF  /* EQ_B4_C - [15:0] */
#define WM9081_EQ_B4_C_SHIFT                         0  /* EQ_B4_C - [15:0] */
#define WM9081_EQ_B4_C_WIDTH                        16  /* EQ_B4_C - [15:0] */

/*
 * R54 (0x36) - EQ 13
 */
#define WM9081_EQ_B4_PG_MASK                    0xFFFF  /* EQ_B4_PG - [15:0] */
#define WM9081_EQ_B4_PG_SHIFT                        0  /* EQ_B4_PG - [15:0] */
#define WM9081_EQ_B4_PG_WIDTH                       16  /* EQ_B4_PG - [15:0] */

/*
 * R55 (0x37) - EQ 14
 */
#define WM9081_EQ_B3_A_MASK                     0xFFFF  /* EQ_B3_A - [15:0] */
#define WM9081_EQ_B3_A_SHIFT                         0  /* EQ_B3_A - [15:0] */
#define WM9081_EQ_B3_A_WIDTH                        16  /* EQ_B3_A - [15:0] */

/*
 * R56 (0x38) - EQ 15
 */
#define WM9081_EQ_B3_B_MASK                     0xFFFF  /* EQ_B3_B - [15:0] */
#define WM9081_EQ_B3_B_SHIFT                         0  /* EQ_B3_B - [15:0] */
#define WM9081_EQ_B3_B_WIDTH                        16  /* EQ_B3_B - [15:0] */

/*
 * R57 (0x39) - EQ 16
 */
#define WM9081_EQ_B3_C_MASK                     0xFFFF  /* EQ_B3_C - [15:0] */
#define WM9081_EQ_B3_C_SHIFT                         0  /* EQ_B3_C - [15:0] */
#define WM9081_EQ_B3_C_WIDTH                        16  /* EQ_B3_C - [15:0] */

/*
 * R58 (0x3A) - EQ 17
 */
#define WM9081_EQ_B3_PG_MASK                    0xFFFF  /* EQ_B3_PG - [15:0] */
#define WM9081_EQ_B3_PG_SHIFT                        0  /* EQ_B3_PG - [15:0] */
#define WM9081_EQ_B3_PG_WIDTH                       16  /* EQ_B3_PG - [15:0] */

/*
 * R59 (0x3B) - EQ 18
 */
#define WM9081_EQ_B5_A_MASK                     0xFFFF  /* EQ_B5_A - [15:0] */
#define WM9081_EQ_B5_A_SHIFT                         0  /* EQ_B5_A - [15:0] */
#define WM9081_EQ_B5_A_WIDTH                        16  /* EQ_B5_A - [15:0] */

/*
 * R60 (0x3C) - EQ 19
 */
#define WM9081_EQ_B5_B_MASK                     0xFFFF  /* EQ_B5_B - [15:0] */
#define WM9081_EQ_B5_B_SHIFT                         0  /* EQ_B5_B - [15:0] */
#define WM9081_EQ_B5_B_WIDTH                        16  /* EQ_B5_B - [15:0] */

/*
 * R61 (0x3D) - EQ 20
 */
#define WM9081_EQ_B5_PG_MASK                    0xFFFF  /* EQ_B5_PG - [15:0] */
#define WM9081_EQ_B5_PG_SHIFT                        0  /* EQ_B5_PG - [15:0] */
#define WM9081_EQ_B5_PG_WIDTH                       16  /* EQ_B5_PG - [15:0] */


#endif
