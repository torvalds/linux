/*
 * ALSA SoC WM9090 driver
 *
 * Copyright 2009 Wolfson Microelectronics
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef __WM9090_H
#define __WM9090_H

extern struct snd_soc_codec_device soc_codec_dev_wm9090;

/*
 * Register values.
 */
#define WM9090_SOFTWARE_RESET                   0x00
#define WM9090_POWER_MANAGEMENT_1               0x01
#define WM9090_POWER_MANAGEMENT_2               0x02
#define WM9090_POWER_MANAGEMENT_3               0x03
#define WM9090_CLOCKING_1                       0x06
#define WM9090_IN1_LINE_CONTROL                 0x16
#define WM9090_IN2_LINE_CONTROL                 0x17
#define WM9090_IN1_LINE_INPUT_A_VOLUME          0x18
#define WM9090_IN1_LINE_INPUT_B_VOLUME          0x19
#define WM9090_IN2_LINE_INPUT_A_VOLUME          0x1A
#define WM9090_IN2_LINE_INPUT_B_VOLUME          0x1B
#define WM9090_LEFT_OUTPUT_VOLUME               0x1C
#define WM9090_RIGHT_OUTPUT_VOLUME              0x1D
#define WM9090_SPKMIXL_ATTENUATION              0x22
#define WM9090_SPKOUT_MIXERS                    0x24
#define WM9090_CLASSD3                          0x25
#define WM9090_SPEAKER_VOLUME_LEFT              0x26
#define WM9090_OUTPUT_MIXER1                    0x2D
#define WM9090_OUTPUT_MIXER2                    0x2E
#define WM9090_OUTPUT_MIXER3                    0x2F
#define WM9090_OUTPUT_MIXER4                    0x30
#define WM9090_SPEAKER_MIXER                    0x36
#define WM9090_ANTIPOP2                         0x39
#define WM9090_WRITE_SEQUENCER_0                0x46
#define WM9090_WRITE_SEQUENCER_1                0x47
#define WM9090_WRITE_SEQUENCER_2                0x48
#define WM9090_WRITE_SEQUENCER_3                0x49
#define WM9090_WRITE_SEQUENCER_4                0x4A
#define WM9090_WRITE_SEQUENCER_5                0x4B
#define WM9090_CHARGE_PUMP_1                    0x4C
#define WM9090_DC_SERVO_0                       0x54
#define WM9090_DC_SERVO_1                       0x55
#define WM9090_DC_SERVO_3                       0x57
#define WM9090_DC_SERVO_READBACK_0              0x58
#define WM9090_DC_SERVO_READBACK_1              0x59
#define WM9090_DC_SERVO_READBACK_2              0x5A
#define WM9090_ANALOGUE_HP_0                    0x60
#define WM9090_AGC_CONTROL_0                    0x62
#define WM9090_AGC_CONTROL_1                    0x63
#define WM9090_AGC_CONTROL_2                    0x64

#define WM9090_REGISTER_COUNT                   40
#define WM9090_MAX_REGISTER                     0x64

/*
 * Field Definitions.
 */

/*
 * R0 (0x00) - Software Reset
 */
#define WM9090_SW_RESET_MASK                    0xFFFF  /* SW_RESET - [15:0] */
#define WM9090_SW_RESET_SHIFT                        0  /* SW_RESET - [15:0] */
#define WM9090_SW_RESET_WIDTH                       16  /* SW_RESET - [15:0] */

/*
 * R1 (0x01) - Power Management (1)
 */
#define WM9090_SPKOUTL_ENA                      0x1000  /* SPKOUTL_ENA */
#define WM9090_SPKOUTL_ENA_MASK                 0x1000  /* SPKOUTL_ENA */
#define WM9090_SPKOUTL_ENA_SHIFT                    12  /* SPKOUTL_ENA */
#define WM9090_SPKOUTL_ENA_WIDTH                     1  /* SPKOUTL_ENA */
#define WM9090_HPOUT1L_ENA                      0x0200  /* HPOUT1L_ENA */
#define WM9090_HPOUT1L_ENA_MASK                 0x0200  /* HPOUT1L_ENA */
#define WM9090_HPOUT1L_ENA_SHIFT                     9  /* HPOUT1L_ENA */
#define WM9090_HPOUT1L_ENA_WIDTH                     1  /* HPOUT1L_ENA */
#define WM9090_HPOUT1R_ENA                      0x0100  /* HPOUT1R_ENA */
#define WM9090_HPOUT1R_ENA_MASK                 0x0100  /* HPOUT1R_ENA */
#define WM9090_HPOUT1R_ENA_SHIFT                     8  /* HPOUT1R_ENA */
#define WM9090_HPOUT1R_ENA_WIDTH                     1  /* HPOUT1R_ENA */
#define WM9090_OSC_ENA                          0x0008  /* OSC_ENA */
#define WM9090_OSC_ENA_MASK                     0x0008  /* OSC_ENA */
#define WM9090_OSC_ENA_SHIFT                         3  /* OSC_ENA */
#define WM9090_OSC_ENA_WIDTH                         1  /* OSC_ENA */
#define WM9090_VMID_RES_MASK                    0x0006  /* VMID_RES - [2:1] */
#define WM9090_VMID_RES_SHIFT                        1  /* VMID_RES - [2:1] */
#define WM9090_VMID_RES_WIDTH                        2  /* VMID_RES - [2:1] */
#define WM9090_BIAS_ENA                         0x0001  /* BIAS_ENA */
#define WM9090_BIAS_ENA_MASK                    0x0001  /* BIAS_ENA */
#define WM9090_BIAS_ENA_SHIFT                        0  /* BIAS_ENA */
#define WM9090_BIAS_ENA_WIDTH                        1  /* BIAS_ENA */

/*
 * R2 (0x02) - Power Management (2)
 */
#define WM9090_TSHUT                            0x8000  /* TSHUT */
#define WM9090_TSHUT_MASK                       0x8000  /* TSHUT */
#define WM9090_TSHUT_SHIFT                          15  /* TSHUT */
#define WM9090_TSHUT_WIDTH                           1  /* TSHUT */
#define WM9090_TSHUT_ENA                        0x4000  /* TSHUT_ENA */
#define WM9090_TSHUT_ENA_MASK                   0x4000  /* TSHUT_ENA */
#define WM9090_TSHUT_ENA_SHIFT                      14  /* TSHUT_ENA */
#define WM9090_TSHUT_ENA_WIDTH                       1  /* TSHUT_ENA */
#define WM9090_TSHUT_OPDIS                      0x2000  /* TSHUT_OPDIS */
#define WM9090_TSHUT_OPDIS_MASK                 0x2000  /* TSHUT_OPDIS */
#define WM9090_TSHUT_OPDIS_SHIFT                    13  /* TSHUT_OPDIS */
#define WM9090_TSHUT_OPDIS_WIDTH                     1  /* TSHUT_OPDIS */
#define WM9090_IN1A_ENA                         0x0080  /* IN1A_ENA */
#define WM9090_IN1A_ENA_MASK                    0x0080  /* IN1A_ENA */
#define WM9090_IN1A_ENA_SHIFT                        7  /* IN1A_ENA */
#define WM9090_IN1A_ENA_WIDTH                        1  /* IN1A_ENA */
#define WM9090_IN1B_ENA                         0x0040  /* IN1B_ENA */
#define WM9090_IN1B_ENA_MASK                    0x0040  /* IN1B_ENA */
#define WM9090_IN1B_ENA_SHIFT                        6  /* IN1B_ENA */
#define WM9090_IN1B_ENA_WIDTH                        1  /* IN1B_ENA */
#define WM9090_IN2A_ENA                         0x0020  /* IN2A_ENA */
#define WM9090_IN2A_ENA_MASK                    0x0020  /* IN2A_ENA */
#define WM9090_IN2A_ENA_SHIFT                        5  /* IN2A_ENA */
#define WM9090_IN2A_ENA_WIDTH                        1  /* IN2A_ENA */
#define WM9090_IN2B_ENA                         0x0010  /* IN2B_ENA */
#define WM9090_IN2B_ENA_MASK                    0x0010  /* IN2B_ENA */
#define WM9090_IN2B_ENA_SHIFT                        4  /* IN2B_ENA */
#define WM9090_IN2B_ENA_WIDTH                        1  /* IN2B_ENA */

/*
 * R3 (0x03) - Power Management (3)
 */
#define WM9090_AGC_ENA                          0x4000  /* AGC_ENA */
#define WM9090_AGC_ENA_MASK                     0x4000  /* AGC_ENA */
#define WM9090_AGC_ENA_SHIFT                        14  /* AGC_ENA */
#define WM9090_AGC_ENA_WIDTH                         1  /* AGC_ENA */
#define WM9090_SPKLVOL_ENA                      0x0100  /* SPKLVOL_ENA */
#define WM9090_SPKLVOL_ENA_MASK                 0x0100  /* SPKLVOL_ENA */
#define WM9090_SPKLVOL_ENA_SHIFT                     8  /* SPKLVOL_ENA */
#define WM9090_SPKLVOL_ENA_WIDTH                     1  /* SPKLVOL_ENA */
#define WM9090_MIXOUTL_ENA                      0x0020  /* MIXOUTL_ENA */
#define WM9090_MIXOUTL_ENA_MASK                 0x0020  /* MIXOUTL_ENA */
#define WM9090_MIXOUTL_ENA_SHIFT                     5  /* MIXOUTL_ENA */
#define WM9090_MIXOUTL_ENA_WIDTH                     1  /* MIXOUTL_ENA */
#define WM9090_MIXOUTR_ENA                      0x0010  /* MIXOUTR_ENA */
#define WM9090_MIXOUTR_ENA_MASK                 0x0010  /* MIXOUTR_ENA */
#define WM9090_MIXOUTR_ENA_SHIFT                     4  /* MIXOUTR_ENA */
#define WM9090_MIXOUTR_ENA_WIDTH                     1  /* MIXOUTR_ENA */
#define WM9090_SPKMIX_ENA                       0x0008  /* SPKMIX_ENA */
#define WM9090_SPKMIX_ENA_MASK                  0x0008  /* SPKMIX_ENA */
#define WM9090_SPKMIX_ENA_SHIFT                      3  /* SPKMIX_ENA */
#define WM9090_SPKMIX_ENA_WIDTH                      1  /* SPKMIX_ENA */

/*
 * R6 (0x06) - Clocking 1
 */
#define WM9090_TOCLK_RATE                       0x8000  /* TOCLK_RATE */
#define WM9090_TOCLK_RATE_MASK                  0x8000  /* TOCLK_RATE */
#define WM9090_TOCLK_RATE_SHIFT                     15  /* TOCLK_RATE */
#define WM9090_TOCLK_RATE_WIDTH                      1  /* TOCLK_RATE */
#define WM9090_TOCLK_ENA                        0x4000  /* TOCLK_ENA */
#define WM9090_TOCLK_ENA_MASK                   0x4000  /* TOCLK_ENA */
#define WM9090_TOCLK_ENA_SHIFT                      14  /* TOCLK_ENA */
#define WM9090_TOCLK_ENA_WIDTH                       1  /* TOCLK_ENA */

/*
 * R22 (0x16) - IN1 Line Control
 */
#define WM9090_IN1_DIFF                         0x0002  /* IN1_DIFF */
#define WM9090_IN1_DIFF_MASK                    0x0002  /* IN1_DIFF */
#define WM9090_IN1_DIFF_SHIFT                        1  /* IN1_DIFF */
#define WM9090_IN1_DIFF_WIDTH                        1  /* IN1_DIFF */
#define WM9090_IN1_CLAMP                        0x0001  /* IN1_CLAMP */
#define WM9090_IN1_CLAMP_MASK                   0x0001  /* IN1_CLAMP */
#define WM9090_IN1_CLAMP_SHIFT                       0  /* IN1_CLAMP */
#define WM9090_IN1_CLAMP_WIDTH                       1  /* IN1_CLAMP */

/*
 * R23 (0x17) - IN2 Line Control
 */
#define WM9090_IN2_DIFF                         0x0002  /* IN2_DIFF */
#define WM9090_IN2_DIFF_MASK                    0x0002  /* IN2_DIFF */
#define WM9090_IN2_DIFF_SHIFT                        1  /* IN2_DIFF */
#define WM9090_IN2_DIFF_WIDTH                        1  /* IN2_DIFF */
#define WM9090_IN2_CLAMP                        0x0001  /* IN2_CLAMP */
#define WM9090_IN2_CLAMP_MASK                   0x0001  /* IN2_CLAMP */
#define WM9090_IN2_CLAMP_SHIFT                       0  /* IN2_CLAMP */
#define WM9090_IN2_CLAMP_WIDTH                       1  /* IN2_CLAMP */

/*
 * R24 (0x18) - IN1 Line Input A Volume
 */
#define WM9090_IN1_VU                           0x0100  /* IN1_VU */
#define WM9090_IN1_VU_MASK                      0x0100  /* IN1_VU */
#define WM9090_IN1_VU_SHIFT                          8  /* IN1_VU */
#define WM9090_IN1_VU_WIDTH                          1  /* IN1_VU */
#define WM9090_IN1A_MUTE                        0x0080  /* IN1A_MUTE */
#define WM9090_IN1A_MUTE_MASK                   0x0080  /* IN1A_MUTE */
#define WM9090_IN1A_MUTE_SHIFT                       7  /* IN1A_MUTE */
#define WM9090_IN1A_MUTE_WIDTH                       1  /* IN1A_MUTE */
#define WM9090_IN1A_ZC                          0x0040  /* IN1A_ZC */
#define WM9090_IN1A_ZC_MASK                     0x0040  /* IN1A_ZC */
#define WM9090_IN1A_ZC_SHIFT                         6  /* IN1A_ZC */
#define WM9090_IN1A_ZC_WIDTH                         1  /* IN1A_ZC */
#define WM9090_IN1A_VOL_MASK                    0x0007  /* IN1A_VOL - [2:0] */
#define WM9090_IN1A_VOL_SHIFT                        0  /* IN1A_VOL - [2:0] */
#define WM9090_IN1A_VOL_WIDTH                        3  /* IN1A_VOL - [2:0] */

/*
 * R25 (0x19) - IN1  Line Input B Volume
 */
#define WM9090_IN1_VU                           0x0100  /* IN1_VU */
#define WM9090_IN1_VU_MASK                      0x0100  /* IN1_VU */
#define WM9090_IN1_VU_SHIFT                          8  /* IN1_VU */
#define WM9090_IN1_VU_WIDTH                          1  /* IN1_VU */
#define WM9090_IN1B_MUTE                        0x0080  /* IN1B_MUTE */
#define WM9090_IN1B_MUTE_MASK                   0x0080  /* IN1B_MUTE */
#define WM9090_IN1B_MUTE_SHIFT                       7  /* IN1B_MUTE */
#define WM9090_IN1B_MUTE_WIDTH                       1  /* IN1B_MUTE */
#define WM9090_IN1B_ZC                          0x0040  /* IN1B_ZC */
#define WM9090_IN1B_ZC_MASK                     0x0040  /* IN1B_ZC */
#define WM9090_IN1B_ZC_SHIFT                         6  /* IN1B_ZC */
#define WM9090_IN1B_ZC_WIDTH                         1  /* IN1B_ZC */
#define WM9090_IN1B_VOL_MASK                    0x0007  /* IN1B_VOL - [2:0] */
#define WM9090_IN1B_VOL_SHIFT                        0  /* IN1B_VOL - [2:0] */
#define WM9090_IN1B_VOL_WIDTH                        3  /* IN1B_VOL - [2:0] */

/*
 * R26 (0x1A) - IN2 Line Input A Volume
 */
#define WM9090_IN2_VU                           0x0100  /* IN2_VU */
#define WM9090_IN2_VU_MASK                      0x0100  /* IN2_VU */
#define WM9090_IN2_VU_SHIFT                          8  /* IN2_VU */
#define WM9090_IN2_VU_WIDTH                          1  /* IN2_VU */
#define WM9090_IN2A_MUTE                        0x0080  /* IN2A_MUTE */
#define WM9090_IN2A_MUTE_MASK                   0x0080  /* IN2A_MUTE */
#define WM9090_IN2A_MUTE_SHIFT                       7  /* IN2A_MUTE */
#define WM9090_IN2A_MUTE_WIDTH                       1  /* IN2A_MUTE */
#define WM9090_IN2A_ZC                          0x0040  /* IN2A_ZC */
#define WM9090_IN2A_ZC_MASK                     0x0040  /* IN2A_ZC */
#define WM9090_IN2A_ZC_SHIFT                         6  /* IN2A_ZC */
#define WM9090_IN2A_ZC_WIDTH                         1  /* IN2A_ZC */
#define WM9090_IN2A_VOL_MASK                    0x0007  /* IN2A_VOL - [2:0] */
#define WM9090_IN2A_VOL_SHIFT                        0  /* IN2A_VOL - [2:0] */
#define WM9090_IN2A_VOL_WIDTH                        3  /* IN2A_VOL - [2:0] */

/*
 * R27 (0x1B) - IN2 Line Input B Volume
 */
#define WM9090_IN2_VU                           0x0100  /* IN2_VU */
#define WM9090_IN2_VU_MASK                      0x0100  /* IN2_VU */
#define WM9090_IN2_VU_SHIFT                          8  /* IN2_VU */
#define WM9090_IN2_VU_WIDTH                          1  /* IN2_VU */
#define WM9090_IN2B_MUTE                        0x0080  /* IN2B_MUTE */
#define WM9090_IN2B_MUTE_MASK                   0x0080  /* IN2B_MUTE */
#define WM9090_IN2B_MUTE_SHIFT                       7  /* IN2B_MUTE */
#define WM9090_IN2B_MUTE_WIDTH                       1  /* IN2B_MUTE */
#define WM9090_IN2B_ZC                          0x0040  /* IN2B_ZC */
#define WM9090_IN2B_ZC_MASK                     0x0040  /* IN2B_ZC */
#define WM9090_IN2B_ZC_SHIFT                         6  /* IN2B_ZC */
#define WM9090_IN2B_ZC_WIDTH                         1  /* IN2B_ZC */
#define WM9090_IN2B_VOL_MASK                    0x0007  /* IN2B_VOL - [2:0] */
#define WM9090_IN2B_VOL_SHIFT                        0  /* IN2B_VOL - [2:0] */
#define WM9090_IN2B_VOL_WIDTH                        3  /* IN2B_VOL - [2:0] */

/*
 * R28 (0x1C) - Left Output Volume
 */
#define WM9090_HPOUT1_VU                        0x0100  /* HPOUT1_VU */
#define WM9090_HPOUT1_VU_MASK                   0x0100  /* HPOUT1_VU */
#define WM9090_HPOUT1_VU_SHIFT                       8  /* HPOUT1_VU */
#define WM9090_HPOUT1_VU_WIDTH                       1  /* HPOUT1_VU */
#define WM9090_HPOUT1L_ZC                       0x0080  /* HPOUT1L_ZC */
#define WM9090_HPOUT1L_ZC_MASK                  0x0080  /* HPOUT1L_ZC */
#define WM9090_HPOUT1L_ZC_SHIFT                      7  /* HPOUT1L_ZC */
#define WM9090_HPOUT1L_ZC_WIDTH                      1  /* HPOUT1L_ZC */
#define WM9090_HPOUT1L_MUTE                     0x0040  /* HPOUT1L_MUTE */
#define WM9090_HPOUT1L_MUTE_MASK                0x0040  /* HPOUT1L_MUTE */
#define WM9090_HPOUT1L_MUTE_SHIFT                    6  /* HPOUT1L_MUTE */
#define WM9090_HPOUT1L_MUTE_WIDTH                    1  /* HPOUT1L_MUTE */
#define WM9090_HPOUT1L_VOL_MASK                 0x003F  /* HPOUT1L_VOL - [5:0] */
#define WM9090_HPOUT1L_VOL_SHIFT                     0  /* HPOUT1L_VOL - [5:0] */
#define WM9090_HPOUT1L_VOL_WIDTH                     6  /* HPOUT1L_VOL - [5:0] */

/*
 * R29 (0x1D) - Right Output Volume
 */
#define WM9090_HPOUT1_VU                        0x0100  /* HPOUT1_VU */
#define WM9090_HPOUT1_VU_MASK                   0x0100  /* HPOUT1_VU */
#define WM9090_HPOUT1_VU_SHIFT                       8  /* HPOUT1_VU */
#define WM9090_HPOUT1_VU_WIDTH                       1  /* HPOUT1_VU */
#define WM9090_HPOUT1R_ZC                       0x0080  /* HPOUT1R_ZC */
#define WM9090_HPOUT1R_ZC_MASK                  0x0080  /* HPOUT1R_ZC */
#define WM9090_HPOUT1R_ZC_SHIFT                      7  /* HPOUT1R_ZC */
#define WM9090_HPOUT1R_ZC_WIDTH                      1  /* HPOUT1R_ZC */
#define WM9090_HPOUT1R_MUTE                     0x0040  /* HPOUT1R_MUTE */
#define WM9090_HPOUT1R_MUTE_MASK                0x0040  /* HPOUT1R_MUTE */
#define WM9090_HPOUT1R_MUTE_SHIFT                    6  /* HPOUT1R_MUTE */
#define WM9090_HPOUT1R_MUTE_WIDTH                    1  /* HPOUT1R_MUTE */
#define WM9090_HPOUT1R_VOL_MASK                 0x003F  /* HPOUT1R_VOL - [5:0] */
#define WM9090_HPOUT1R_VOL_SHIFT                     0  /* HPOUT1R_VOL - [5:0] */
#define WM9090_HPOUT1R_VOL_WIDTH                     6  /* HPOUT1R_VOL - [5:0] */

/*
 * R34 (0x22) - SPKMIXL Attenuation
 */
#define WM9090_SPKMIX_MUTE                      0x0100  /* SPKMIX_MUTE */
#define WM9090_SPKMIX_MUTE_MASK                 0x0100  /* SPKMIX_MUTE */
#define WM9090_SPKMIX_MUTE_SHIFT                     8  /* SPKMIX_MUTE */
#define WM9090_SPKMIX_MUTE_WIDTH                     1  /* SPKMIX_MUTE */
#define WM9090_IN1A_SPKMIX_VOL_MASK             0x00C0  /* IN1A_SPKMIX_VOL - [7:6] */
#define WM9090_IN1A_SPKMIX_VOL_SHIFT                 6  /* IN1A_SPKMIX_VOL - [7:6] */
#define WM9090_IN1A_SPKMIX_VOL_WIDTH                 2  /* IN1A_SPKMIX_VOL - [7:6] */
#define WM9090_IN1B_SPKMIX_VOL_MASK             0x0030  /* IN1B_SPKMIX_VOL - [5:4] */
#define WM9090_IN1B_SPKMIX_VOL_SHIFT                 4  /* IN1B_SPKMIX_VOL - [5:4] */
#define WM9090_IN1B_SPKMIX_VOL_WIDTH                 2  /* IN1B_SPKMIX_VOL - [5:4] */
#define WM9090_IN2A_SPKMIX_VOL_MASK             0x000C  /* IN2A_SPKMIX_VOL - [3:2] */
#define WM9090_IN2A_SPKMIX_VOL_SHIFT                 2  /* IN2A_SPKMIX_VOL - [3:2] */
#define WM9090_IN2A_SPKMIX_VOL_WIDTH                 2  /* IN2A_SPKMIX_VOL - [3:2] */
#define WM9090_IN2B_SPKMIX_VOL_MASK             0x0003  /* IN2B_SPKMIX_VOL - [1:0] */
#define WM9090_IN2B_SPKMIX_VOL_SHIFT                 0  /* IN2B_SPKMIX_VOL - [1:0] */
#define WM9090_IN2B_SPKMIX_VOL_WIDTH                 2  /* IN2B_SPKMIX_VOL - [1:0] */

/*
 * R36 (0x24) - SPKOUT Mixers
 */
#define WM9090_SPKMIXL_TO_SPKOUTL               0x0010  /* SPKMIXL_TO_SPKOUTL */
#define WM9090_SPKMIXL_TO_SPKOUTL_MASK          0x0010  /* SPKMIXL_TO_SPKOUTL */
#define WM9090_SPKMIXL_TO_SPKOUTL_SHIFT              4  /* SPKMIXL_TO_SPKOUTL */
#define WM9090_SPKMIXL_TO_SPKOUTL_WIDTH              1  /* SPKMIXL_TO_SPKOUTL */

/*
 * R37 (0x25) - ClassD3
 */
#define WM9090_SPKOUTL_BOOST_MASK               0x0038  /* SPKOUTL_BOOST - [5:3] */
#define WM9090_SPKOUTL_BOOST_SHIFT                   3  /* SPKOUTL_BOOST - [5:3] */
#define WM9090_SPKOUTL_BOOST_WIDTH                   3  /* SPKOUTL_BOOST - [5:3] */

/*
 * R38 (0x26) - Speaker Volume Left
 */
#define WM9090_SPKOUT_VU                        0x0100  /* SPKOUT_VU */
#define WM9090_SPKOUT_VU_MASK                   0x0100  /* SPKOUT_VU */
#define WM9090_SPKOUT_VU_SHIFT                       8  /* SPKOUT_VU */
#define WM9090_SPKOUT_VU_WIDTH                       1  /* SPKOUT_VU */
#define WM9090_SPKOUTL_ZC                       0x0080  /* SPKOUTL_ZC */
#define WM9090_SPKOUTL_ZC_MASK                  0x0080  /* SPKOUTL_ZC */
#define WM9090_SPKOUTL_ZC_SHIFT                      7  /* SPKOUTL_ZC */
#define WM9090_SPKOUTL_ZC_WIDTH                      1  /* SPKOUTL_ZC */
#define WM9090_SPKOUTL_MUTE                     0x0040  /* SPKOUTL_MUTE */
#define WM9090_SPKOUTL_MUTE_MASK                0x0040  /* SPKOUTL_MUTE */
#define WM9090_SPKOUTL_MUTE_SHIFT                    6  /* SPKOUTL_MUTE */
#define WM9090_SPKOUTL_MUTE_WIDTH                    1  /* SPKOUTL_MUTE */
#define WM9090_SPKOUTL_VOL_MASK                 0x003F  /* SPKOUTL_VOL - [5:0] */
#define WM9090_SPKOUTL_VOL_SHIFT                     0  /* SPKOUTL_VOL - [5:0] */
#define WM9090_SPKOUTL_VOL_WIDTH                     6  /* SPKOUTL_VOL - [5:0] */

/*
 * R45 (0x2D) - Output Mixer1
 */
#define WM9090_IN1A_TO_MIXOUTL                  0x0040  /* IN1A_TO_MIXOUTL */
#define WM9090_IN1A_TO_MIXOUTL_MASK             0x0040  /* IN1A_TO_MIXOUTL */
#define WM9090_IN1A_TO_MIXOUTL_SHIFT                 6  /* IN1A_TO_MIXOUTL */
#define WM9090_IN1A_TO_MIXOUTL_WIDTH                 1  /* IN1A_TO_MIXOUTL */
#define WM9090_IN2A_TO_MIXOUTL                  0x0004  /* IN2A_TO_MIXOUTL */
#define WM9090_IN2A_TO_MIXOUTL_MASK             0x0004  /* IN2A_TO_MIXOUTL */
#define WM9090_IN2A_TO_MIXOUTL_SHIFT                 2  /* IN2A_TO_MIXOUTL */
#define WM9090_IN2A_TO_MIXOUTL_WIDTH                 1  /* IN2A_TO_MIXOUTL */

/*
 * R46 (0x2E) - Output Mixer2
 */
#define WM9090_IN1A_TO_MIXOUTR                  0x0040  /* IN1A_TO_MIXOUTR */
#define WM9090_IN1A_TO_MIXOUTR_MASK             0x0040  /* IN1A_TO_MIXOUTR */
#define WM9090_IN1A_TO_MIXOUTR_SHIFT                 6  /* IN1A_TO_MIXOUTR */
#define WM9090_IN1A_TO_MIXOUTR_WIDTH                 1  /* IN1A_TO_MIXOUTR */
#define WM9090_IN1B_TO_MIXOUTR                  0x0010  /* IN1B_TO_MIXOUTR */
#define WM9090_IN1B_TO_MIXOUTR_MASK             0x0010  /* IN1B_TO_MIXOUTR */
#define WM9090_IN1B_TO_MIXOUTR_SHIFT                 4  /* IN1B_TO_MIXOUTR */
#define WM9090_IN1B_TO_MIXOUTR_WIDTH                 1  /* IN1B_TO_MIXOUTR */
#define WM9090_IN2A_TO_MIXOUTR                  0x0004  /* IN2A_TO_MIXOUTR */
#define WM9090_IN2A_TO_MIXOUTR_MASK             0x0004  /* IN2A_TO_MIXOUTR */
#define WM9090_IN2A_TO_MIXOUTR_SHIFT                 2  /* IN2A_TO_MIXOUTR */
#define WM9090_IN2A_TO_MIXOUTR_WIDTH                 1  /* IN2A_TO_MIXOUTR */
#define WM9090_IN2B_TO_MIXOUTR                  0x0001  /* IN2B_TO_MIXOUTR */
#define WM9090_IN2B_TO_MIXOUTR_MASK             0x0001  /* IN2B_TO_MIXOUTR */
#define WM9090_IN2B_TO_MIXOUTR_SHIFT                 0  /* IN2B_TO_MIXOUTR */
#define WM9090_IN2B_TO_MIXOUTR_WIDTH                 1  /* IN2B_TO_MIXOUTR */

/*
 * R47 (0x2F) - Output Mixer3
 */
#define WM9090_MIXOUTL_MUTE                     0x0100  /* MIXOUTL_MUTE */
#define WM9090_MIXOUTL_MUTE_MASK                0x0100  /* MIXOUTL_MUTE */
#define WM9090_MIXOUTL_MUTE_SHIFT                    8  /* MIXOUTL_MUTE */
#define WM9090_MIXOUTL_MUTE_WIDTH                    1  /* MIXOUTL_MUTE */
#define WM9090_IN1A_MIXOUTL_VOL_MASK            0x00C0  /* IN1A_MIXOUTL_VOL - [7:6] */
#define WM9090_IN1A_MIXOUTL_VOL_SHIFT                6  /* IN1A_MIXOUTL_VOL - [7:6] */
#define WM9090_IN1A_MIXOUTL_VOL_WIDTH                2  /* IN1A_MIXOUTL_VOL - [7:6] */
#define WM9090_IN2A_MIXOUTL_VOL_MASK            0x000C  /* IN2A_MIXOUTL_VOL - [3:2] */
#define WM9090_IN2A_MIXOUTL_VOL_SHIFT                2  /* IN2A_MIXOUTL_VOL - [3:2] */
#define WM9090_IN2A_MIXOUTL_VOL_WIDTH                2  /* IN2A_MIXOUTL_VOL - [3:2] */

/*
 * R48 (0x30) - Output Mixer4
 */
#define WM9090_MIXOUTR_MUTE                     0x0100  /* MIXOUTR_MUTE */
#define WM9090_MIXOUTR_MUTE_MASK                0x0100  /* MIXOUTR_MUTE */
#define WM9090_MIXOUTR_MUTE_SHIFT                    8  /* MIXOUTR_MUTE */
#define WM9090_MIXOUTR_MUTE_WIDTH                    1  /* MIXOUTR_MUTE */
#define WM9090_IN1A_MIXOUTR_VOL_MASK            0x00C0  /* IN1A_MIXOUTR_VOL - [7:6] */
#define WM9090_IN1A_MIXOUTR_VOL_SHIFT                6  /* IN1A_MIXOUTR_VOL - [7:6] */
#define WM9090_IN1A_MIXOUTR_VOL_WIDTH                2  /* IN1A_MIXOUTR_VOL - [7:6] */
#define WM9090_IN1B_MIXOUTR_VOL_MASK            0x0030  /* IN1B_MIXOUTR_VOL - [5:4] */
#define WM9090_IN1B_MIXOUTR_VOL_SHIFT                4  /* IN1B_MIXOUTR_VOL - [5:4] */
#define WM9090_IN1B_MIXOUTR_VOL_WIDTH                2  /* IN1B_MIXOUTR_VOL - [5:4] */
#define WM9090_IN2A_MIXOUTR_VOL_MASK            0x000C  /* IN2A_MIXOUTR_VOL - [3:2] */
#define WM9090_IN2A_MIXOUTR_VOL_SHIFT                2  /* IN2A_MIXOUTR_VOL - [3:2] */
#define WM9090_IN2A_MIXOUTR_VOL_WIDTH                2  /* IN2A_MIXOUTR_VOL - [3:2] */
#define WM9090_IN2B_MIXOUTR_VOL_MASK            0x0003  /* IN2B_MIXOUTR_VOL - [1:0] */
#define WM9090_IN2B_MIXOUTR_VOL_SHIFT                0  /* IN2B_MIXOUTR_VOL - [1:0] */
#define WM9090_IN2B_MIXOUTR_VOL_WIDTH                2  /* IN2B_MIXOUTR_VOL - [1:0] */

/*
 * R54 (0x36) - Speaker Mixer
 */
#define WM9090_IN1A_TO_SPKMIX                   0x0040  /* IN1A_TO_SPKMIX */
#define WM9090_IN1A_TO_SPKMIX_MASK              0x0040  /* IN1A_TO_SPKMIX */
#define WM9090_IN1A_TO_SPKMIX_SHIFT                  6  /* IN1A_TO_SPKMIX */
#define WM9090_IN1A_TO_SPKMIX_WIDTH                  1  /* IN1A_TO_SPKMIX */
#define WM9090_IN1B_TO_SPKMIX                   0x0010  /* IN1B_TO_SPKMIX */
#define WM9090_IN1B_TO_SPKMIX_MASK              0x0010  /* IN1B_TO_SPKMIX */
#define WM9090_IN1B_TO_SPKMIX_SHIFT                  4  /* IN1B_TO_SPKMIX */
#define WM9090_IN1B_TO_SPKMIX_WIDTH                  1  /* IN1B_TO_SPKMIX */
#define WM9090_IN2A_TO_SPKMIX                   0x0004  /* IN2A_TO_SPKMIX */
#define WM9090_IN2A_TO_SPKMIX_MASK              0x0004  /* IN2A_TO_SPKMIX */
#define WM9090_IN2A_TO_SPKMIX_SHIFT                  2  /* IN2A_TO_SPKMIX */
#define WM9090_IN2A_TO_SPKMIX_WIDTH                  1  /* IN2A_TO_SPKMIX */
#define WM9090_IN2B_TO_SPKMIX                   0x0001  /* IN2B_TO_SPKMIX */
#define WM9090_IN2B_TO_SPKMIX_MASK              0x0001  /* IN2B_TO_SPKMIX */
#define WM9090_IN2B_TO_SPKMIX_SHIFT                  0  /* IN2B_TO_SPKMIX */
#define WM9090_IN2B_TO_SPKMIX_WIDTH                  1  /* IN2B_TO_SPKMIX */

/*
 * R57 (0x39) - AntiPOP2
 */
#define WM9090_VMID_BUF_ENA                     0x0008  /* VMID_BUF_ENA */
#define WM9090_VMID_BUF_ENA_MASK                0x0008  /* VMID_BUF_ENA */
#define WM9090_VMID_BUF_ENA_SHIFT                    3  /* VMID_BUF_ENA */
#define WM9090_VMID_BUF_ENA_WIDTH                    1  /* VMID_BUF_ENA */
#define WM9090_VMID_ENA                         0x0001  /* VMID_ENA */
#define WM9090_VMID_ENA_MASK                    0x0001  /* VMID_ENA */
#define WM9090_VMID_ENA_SHIFT                        0  /* VMID_ENA */
#define WM9090_VMID_ENA_WIDTH                        1  /* VMID_ENA */

/*
 * R70 (0x46) - Write Sequencer 0
 */
#define WM9090_WSEQ_ENA                         0x0100  /* WSEQ_ENA */
#define WM9090_WSEQ_ENA_MASK                    0x0100  /* WSEQ_ENA */
#define WM9090_WSEQ_ENA_SHIFT                        8  /* WSEQ_ENA */
#define WM9090_WSEQ_ENA_WIDTH                        1  /* WSEQ_ENA */
#define WM9090_WSEQ_WRITE_INDEX_MASK            0x000F  /* WSEQ_WRITE_INDEX - [3:0] */
#define WM9090_WSEQ_WRITE_INDEX_SHIFT                0  /* WSEQ_WRITE_INDEX - [3:0] */
#define WM9090_WSEQ_WRITE_INDEX_WIDTH                4  /* WSEQ_WRITE_INDEX - [3:0] */

/*
 * R71 (0x47) - Write Sequencer 1
 */
#define WM9090_WSEQ_DATA_WIDTH_MASK             0x7000  /* WSEQ_DATA_WIDTH - [14:12] */
#define WM9090_WSEQ_DATA_WIDTH_SHIFT                12  /* WSEQ_DATA_WIDTH - [14:12] */
#define WM9090_WSEQ_DATA_WIDTH_WIDTH                 3  /* WSEQ_DATA_WIDTH - [14:12] */
#define WM9090_WSEQ_DATA_START_MASK             0x0F00  /* WSEQ_DATA_START - [11:8] */
#define WM9090_WSEQ_DATA_START_SHIFT                 8  /* WSEQ_DATA_START - [11:8] */
#define WM9090_WSEQ_DATA_START_WIDTH                 4  /* WSEQ_DATA_START - [11:8] */
#define WM9090_WSEQ_ADDR_MASK                   0x00FF  /* WSEQ_ADDR - [7:0] */
#define WM9090_WSEQ_ADDR_SHIFT                       0  /* WSEQ_ADDR - [7:0] */
#define WM9090_WSEQ_ADDR_WIDTH                       8  /* WSEQ_ADDR - [7:0] */

/*
 * R72 (0x48) - Write Sequencer 2
 */
#define WM9090_WSEQ_EOS                         0x4000  /* WSEQ_EOS */
#define WM9090_WSEQ_EOS_MASK                    0x4000  /* WSEQ_EOS */
#define WM9090_WSEQ_EOS_SHIFT                       14  /* WSEQ_EOS */
#define WM9090_WSEQ_EOS_WIDTH                        1  /* WSEQ_EOS */
#define WM9090_WSEQ_DELAY_MASK                  0x0F00  /* WSEQ_DELAY - [11:8] */
#define WM9090_WSEQ_DELAY_SHIFT                      8  /* WSEQ_DELAY - [11:8] */
#define WM9090_WSEQ_DELAY_WIDTH                      4  /* WSEQ_DELAY - [11:8] */
#define WM9090_WSEQ_DATA_MASK                   0x00FF  /* WSEQ_DATA - [7:0] */
#define WM9090_WSEQ_DATA_SHIFT                       0  /* WSEQ_DATA - [7:0] */
#define WM9090_WSEQ_DATA_WIDTH                       8  /* WSEQ_DATA - [7:0] */

/*
 * R73 (0x49) - Write Sequencer 3
 */
#define WM9090_WSEQ_ABORT                       0x0200  /* WSEQ_ABORT */
#define WM9090_WSEQ_ABORT_MASK                  0x0200  /* WSEQ_ABORT */
#define WM9090_WSEQ_ABORT_SHIFT                      9  /* WSEQ_ABORT */
#define WM9090_WSEQ_ABORT_WIDTH                      1  /* WSEQ_ABORT */
#define WM9090_WSEQ_START                       0x0100  /* WSEQ_START */
#define WM9090_WSEQ_START_MASK                  0x0100  /* WSEQ_START */
#define WM9090_WSEQ_START_SHIFT                      8  /* WSEQ_START */
#define WM9090_WSEQ_START_WIDTH                      1  /* WSEQ_START */
#define WM9090_WSEQ_START_INDEX_MASK            0x003F  /* WSEQ_START_INDEX - [5:0] */
#define WM9090_WSEQ_START_INDEX_SHIFT                0  /* WSEQ_START_INDEX - [5:0] */
#define WM9090_WSEQ_START_INDEX_WIDTH                6  /* WSEQ_START_INDEX - [5:0] */

/*
 * R74 (0x4A) - Write Sequencer 4
 */
#define WM9090_WSEQ_BUSY                        0x0001  /* WSEQ_BUSY */
#define WM9090_WSEQ_BUSY_MASK                   0x0001  /* WSEQ_BUSY */
#define WM9090_WSEQ_BUSY_SHIFT                       0  /* WSEQ_BUSY */
#define WM9090_WSEQ_BUSY_WIDTH                       1  /* WSEQ_BUSY */

/*
 * R75 (0x4B) - Write Sequencer 5
 */
#define WM9090_WSEQ_CURRENT_INDEX_MASK          0x003F  /* WSEQ_CURRENT_INDEX - [5:0] */
#define WM9090_WSEQ_CURRENT_INDEX_SHIFT              0  /* WSEQ_CURRENT_INDEX - [5:0] */
#define WM9090_WSEQ_CURRENT_INDEX_WIDTH              6  /* WSEQ_CURRENT_INDEX - [5:0] */

/*
 * R76 (0x4C) - Charge Pump 1
 */
#define WM9090_CP_ENA                           0x8000  /* CP_ENA */
#define WM9090_CP_ENA_MASK                      0x8000  /* CP_ENA */
#define WM9090_CP_ENA_SHIFT                         15  /* CP_ENA */
#define WM9090_CP_ENA_WIDTH                          1  /* CP_ENA */

/*
 * R84 (0x54) - DC Servo 0
 */
#define WM9090_DCS_TRIG_SINGLE_1                0x2000  /* DCS_TRIG_SINGLE_1 */
#define WM9090_DCS_TRIG_SINGLE_1_MASK           0x2000  /* DCS_TRIG_SINGLE_1 */
#define WM9090_DCS_TRIG_SINGLE_1_SHIFT              13  /* DCS_TRIG_SINGLE_1 */
#define WM9090_DCS_TRIG_SINGLE_1_WIDTH               1  /* DCS_TRIG_SINGLE_1 */
#define WM9090_DCS_TRIG_SINGLE_0                0x1000  /* DCS_TRIG_SINGLE_0 */
#define WM9090_DCS_TRIG_SINGLE_0_MASK           0x1000  /* DCS_TRIG_SINGLE_0 */
#define WM9090_DCS_TRIG_SINGLE_0_SHIFT              12  /* DCS_TRIG_SINGLE_0 */
#define WM9090_DCS_TRIG_SINGLE_0_WIDTH               1  /* DCS_TRIG_SINGLE_0 */
#define WM9090_DCS_TRIG_SERIES_1                0x0200  /* DCS_TRIG_SERIES_1 */
#define WM9090_DCS_TRIG_SERIES_1_MASK           0x0200  /* DCS_TRIG_SERIES_1 */
#define WM9090_DCS_TRIG_SERIES_1_SHIFT               9  /* DCS_TRIG_SERIES_1 */
#define WM9090_DCS_TRIG_SERIES_1_WIDTH               1  /* DCS_TRIG_SERIES_1 */
#define WM9090_DCS_TRIG_SERIES_0                0x0100  /* DCS_TRIG_SERIES_0 */
#define WM9090_DCS_TRIG_SERIES_0_MASK           0x0100  /* DCS_TRIG_SERIES_0 */
#define WM9090_DCS_TRIG_SERIES_0_SHIFT               8  /* DCS_TRIG_SERIES_0 */
#define WM9090_DCS_TRIG_SERIES_0_WIDTH               1  /* DCS_TRIG_SERIES_0 */
#define WM9090_DCS_TRIG_STARTUP_1               0x0020  /* DCS_TRIG_STARTUP_1 */
#define WM9090_DCS_TRIG_STARTUP_1_MASK          0x0020  /* DCS_TRIG_STARTUP_1 */
#define WM9090_DCS_TRIG_STARTUP_1_SHIFT              5  /* DCS_TRIG_STARTUP_1 */
#define WM9090_DCS_TRIG_STARTUP_1_WIDTH              1  /* DCS_TRIG_STARTUP_1 */
#define WM9090_DCS_TRIG_STARTUP_0               0x0010  /* DCS_TRIG_STARTUP_0 */
#define WM9090_DCS_TRIG_STARTUP_0_MASK          0x0010  /* DCS_TRIG_STARTUP_0 */
#define WM9090_DCS_TRIG_STARTUP_0_SHIFT              4  /* DCS_TRIG_STARTUP_0 */
#define WM9090_DCS_TRIG_STARTUP_0_WIDTH              1  /* DCS_TRIG_STARTUP_0 */
#define WM9090_DCS_TRIG_DAC_WR_1                0x0008  /* DCS_TRIG_DAC_WR_1 */
#define WM9090_DCS_TRIG_DAC_WR_1_MASK           0x0008  /* DCS_TRIG_DAC_WR_1 */
#define WM9090_DCS_TRIG_DAC_WR_1_SHIFT               3  /* DCS_TRIG_DAC_WR_1 */
#define WM9090_DCS_TRIG_DAC_WR_1_WIDTH               1  /* DCS_TRIG_DAC_WR_1 */
#define WM9090_DCS_TRIG_DAC_WR_0                0x0004  /* DCS_TRIG_DAC_WR_0 */
#define WM9090_DCS_TRIG_DAC_WR_0_MASK           0x0004  /* DCS_TRIG_DAC_WR_0 */
#define WM9090_DCS_TRIG_DAC_WR_0_SHIFT               2  /* DCS_TRIG_DAC_WR_0 */
#define WM9090_DCS_TRIG_DAC_WR_0_WIDTH               1  /* DCS_TRIG_DAC_WR_0 */
#define WM9090_DCS_ENA_CHAN_1                   0x0002  /* DCS_ENA_CHAN_1 */
#define WM9090_DCS_ENA_CHAN_1_MASK              0x0002  /* DCS_ENA_CHAN_1 */
#define WM9090_DCS_ENA_CHAN_1_SHIFT                  1  /* DCS_ENA_CHAN_1 */
#define WM9090_DCS_ENA_CHAN_1_WIDTH                  1  /* DCS_ENA_CHAN_1 */
#define WM9090_DCS_ENA_CHAN_0                   0x0001  /* DCS_ENA_CHAN_0 */
#define WM9090_DCS_ENA_CHAN_0_MASK              0x0001  /* DCS_ENA_CHAN_0 */
#define WM9090_DCS_ENA_CHAN_0_SHIFT                  0  /* DCS_ENA_CHAN_0 */
#define WM9090_DCS_ENA_CHAN_0_WIDTH                  1  /* DCS_ENA_CHAN_0 */

/*
 * R85 (0x55) - DC Servo 1
 */
#define WM9090_DCS_SERIES_NO_01_MASK            0x0FE0  /* DCS_SERIES_NO_01 - [11:5] */
#define WM9090_DCS_SERIES_NO_01_SHIFT                5  /* DCS_SERIES_NO_01 - [11:5] */
#define WM9090_DCS_SERIES_NO_01_WIDTH                7  /* DCS_SERIES_NO_01 - [11:5] */
#define WM9090_DCS_TIMER_PERIOD_01_MASK         0x000F  /* DCS_TIMER_PERIOD_01 - [3:0] */
#define WM9090_DCS_TIMER_PERIOD_01_SHIFT             0  /* DCS_TIMER_PERIOD_01 - [3:0] */
#define WM9090_DCS_TIMER_PERIOD_01_WIDTH             4  /* DCS_TIMER_PERIOD_01 - [3:0] */

/*
 * R87 (0x57) - DC Servo 3
 */
#define WM9090_DCS_DAC_WR_VAL_1_MASK            0xFF00  /* DCS_DAC_WR_VAL_1 - [15:8] */
#define WM9090_DCS_DAC_WR_VAL_1_SHIFT                8  /* DCS_DAC_WR_VAL_1 - [15:8] */
#define WM9090_DCS_DAC_WR_VAL_1_WIDTH                8  /* DCS_DAC_WR_VAL_1 - [15:8] */
#define WM9090_DCS_DAC_WR_VAL_0_MASK            0x00FF  /* DCS_DAC_WR_VAL_0 - [7:0] */
#define WM9090_DCS_DAC_WR_VAL_0_SHIFT                0  /* DCS_DAC_WR_VAL_0 - [7:0] */
#define WM9090_DCS_DAC_WR_VAL_0_WIDTH                8  /* DCS_DAC_WR_VAL_0 - [7:0] */

/*
 * R88 (0x58) - DC Servo Readback 0
 */
#define WM9090_DCS_CAL_COMPLETE_MASK            0x0300  /* DCS_CAL_COMPLETE - [9:8] */
#define WM9090_DCS_CAL_COMPLETE_SHIFT                8  /* DCS_CAL_COMPLETE - [9:8] */
#define WM9090_DCS_CAL_COMPLETE_WIDTH                2  /* DCS_CAL_COMPLETE - [9:8] */
#define WM9090_DCS_DAC_WR_COMPLETE_MASK         0x0030  /* DCS_DAC_WR_COMPLETE - [5:4] */
#define WM9090_DCS_DAC_WR_COMPLETE_SHIFT             4  /* DCS_DAC_WR_COMPLETE - [5:4] */
#define WM9090_DCS_DAC_WR_COMPLETE_WIDTH             2  /* DCS_DAC_WR_COMPLETE - [5:4] */
#define WM9090_DCS_STARTUP_COMPLETE_MASK        0x0003  /* DCS_STARTUP_COMPLETE - [1:0] */
#define WM9090_DCS_STARTUP_COMPLETE_SHIFT            0  /* DCS_STARTUP_COMPLETE - [1:0] */
#define WM9090_DCS_STARTUP_COMPLETE_WIDTH            2  /* DCS_STARTUP_COMPLETE - [1:0] */

/*
 * R89 (0x59) - DC Servo Readback 1
 */
#define WM9090_DCS_DAC_WR_VAL_1_RD_MASK         0x00FF  /* DCS_DAC_WR_VAL_1_RD - [7:0] */
#define WM9090_DCS_DAC_WR_VAL_1_RD_SHIFT             0  /* DCS_DAC_WR_VAL_1_RD - [7:0] */
#define WM9090_DCS_DAC_WR_VAL_1_RD_WIDTH             8  /* DCS_DAC_WR_VAL_1_RD - [7:0] */

/*
 * R90 (0x5A) - DC Servo Readback 2
 */
#define WM9090_DCS_DAC_WR_VAL_0_RD_MASK         0x00FF  /* DCS_DAC_WR_VAL_0_RD - [7:0] */
#define WM9090_DCS_DAC_WR_VAL_0_RD_SHIFT             0  /* DCS_DAC_WR_VAL_0_RD - [7:0] */
#define WM9090_DCS_DAC_WR_VAL_0_RD_WIDTH             8  /* DCS_DAC_WR_VAL_0_RD - [7:0] */

/*
 * R96 (0x60) - Analogue HP 0
 */
#define WM9090_HPOUT1L_RMV_SHORT                0x0080  /* HPOUT1L_RMV_SHORT */
#define WM9090_HPOUT1L_RMV_SHORT_MASK           0x0080  /* HPOUT1L_RMV_SHORT */
#define WM9090_HPOUT1L_RMV_SHORT_SHIFT               7  /* HPOUT1L_RMV_SHORT */
#define WM9090_HPOUT1L_RMV_SHORT_WIDTH               1  /* HPOUT1L_RMV_SHORT */
#define WM9090_HPOUT1L_OUTP                     0x0040  /* HPOUT1L_OUTP */
#define WM9090_HPOUT1L_OUTP_MASK                0x0040  /* HPOUT1L_OUTP */
#define WM9090_HPOUT1L_OUTP_SHIFT                    6  /* HPOUT1L_OUTP */
#define WM9090_HPOUT1L_OUTP_WIDTH                    1  /* HPOUT1L_OUTP */
#define WM9090_HPOUT1L_DLY                      0x0020  /* HPOUT1L_DLY */
#define WM9090_HPOUT1L_DLY_MASK                 0x0020  /* HPOUT1L_DLY */
#define WM9090_HPOUT1L_DLY_SHIFT                     5  /* HPOUT1L_DLY */
#define WM9090_HPOUT1L_DLY_WIDTH                     1  /* HPOUT1L_DLY */
#define WM9090_HPOUT1R_RMV_SHORT                0x0008  /* HPOUT1R_RMV_SHORT */
#define WM9090_HPOUT1R_RMV_SHORT_MASK           0x0008  /* HPOUT1R_RMV_SHORT */
#define WM9090_HPOUT1R_RMV_SHORT_SHIFT               3  /* HPOUT1R_RMV_SHORT */
#define WM9090_HPOUT1R_RMV_SHORT_WIDTH               1  /* HPOUT1R_RMV_SHORT */
#define WM9090_HPOUT1R_OUTP                     0x0004  /* HPOUT1R_OUTP */
#define WM9090_HPOUT1R_OUTP_MASK                0x0004  /* HPOUT1R_OUTP */
#define WM9090_HPOUT1R_OUTP_SHIFT                    2  /* HPOUT1R_OUTP */
#define WM9090_HPOUT1R_OUTP_WIDTH                    1  /* HPOUT1R_OUTP */
#define WM9090_HPOUT1R_DLY                      0x0002  /* HPOUT1R_DLY */
#define WM9090_HPOUT1R_DLY_MASK                 0x0002  /* HPOUT1R_DLY */
#define WM9090_HPOUT1R_DLY_SHIFT                     1  /* HPOUT1R_DLY */
#define WM9090_HPOUT1R_DLY_WIDTH                     1  /* HPOUT1R_DLY */

/*
 * R98 (0x62) - AGC Control 0
 */
#define WM9090_AGC_CLIP_ENA                     0x8000  /* AGC_CLIP_ENA */
#define WM9090_AGC_CLIP_ENA_MASK                0x8000  /* AGC_CLIP_ENA */
#define WM9090_AGC_CLIP_ENA_SHIFT                   15  /* AGC_CLIP_ENA */
#define WM9090_AGC_CLIP_ENA_WIDTH                    1  /* AGC_CLIP_ENA */
#define WM9090_AGC_CLIP_THR_MASK                0x0F00  /* AGC_CLIP_THR - [11:8] */
#define WM9090_AGC_CLIP_THR_SHIFT                    8  /* AGC_CLIP_THR - [11:8] */
#define WM9090_AGC_CLIP_THR_WIDTH                    4  /* AGC_CLIP_THR - [11:8] */
#define WM9090_AGC_CLIP_ATK_MASK                0x0070  /* AGC_CLIP_ATK - [6:4] */
#define WM9090_AGC_CLIP_ATK_SHIFT                    4  /* AGC_CLIP_ATK - [6:4] */
#define WM9090_AGC_CLIP_ATK_WIDTH                    3  /* AGC_CLIP_ATK - [6:4] */
#define WM9090_AGC_CLIP_DCY_MASK                0x0007  /* AGC_CLIP_DCY - [2:0] */
#define WM9090_AGC_CLIP_DCY_SHIFT                    0  /* AGC_CLIP_DCY - [2:0] */
#define WM9090_AGC_CLIP_DCY_WIDTH                    3  /* AGC_CLIP_DCY - [2:0] */

/*
 * R99 (0x63) - AGC Control 1
 */
#define WM9090_AGC_PWR_ENA                      0x8000  /* AGC_PWR_ENA */
#define WM9090_AGC_PWR_ENA_MASK                 0x8000  /* AGC_PWR_ENA */
#define WM9090_AGC_PWR_ENA_SHIFT                    15  /* AGC_PWR_ENA */
#define WM9090_AGC_PWR_ENA_WIDTH                     1  /* AGC_PWR_ENA */
#define WM9090_AGC_PWR_AVG                      0x1000  /* AGC_PWR_AVG */
#define WM9090_AGC_PWR_AVG_MASK                 0x1000  /* AGC_PWR_AVG */
#define WM9090_AGC_PWR_AVG_SHIFT                    12  /* AGC_PWR_AVG */
#define WM9090_AGC_PWR_AVG_WIDTH                     1  /* AGC_PWR_AVG */
#define WM9090_AGC_PWR_THR_MASK                 0x0F00  /* AGC_PWR_THR - [11:8] */
#define WM9090_AGC_PWR_THR_SHIFT                     8  /* AGC_PWR_THR - [11:8] */
#define WM9090_AGC_PWR_THR_WIDTH                     4  /* AGC_PWR_THR - [11:8] */
#define WM9090_AGC_PWR_ATK_MASK                 0x0070  /* AGC_PWR_ATK - [6:4] */
#define WM9090_AGC_PWR_ATK_SHIFT                     4  /* AGC_PWR_ATK - [6:4] */
#define WM9090_AGC_PWR_ATK_WIDTH                     3  /* AGC_PWR_ATK - [6:4] */
#define WM9090_AGC_PWR_DCY_MASK                 0x0007  /* AGC_PWR_DCY - [2:0] */
#define WM9090_AGC_PWR_DCY_SHIFT                     0  /* AGC_PWR_DCY - [2:0] */
#define WM9090_AGC_PWR_DCY_WIDTH                     3  /* AGC_PWR_DCY - [2:0] */

/*
 * R100 (0x64) - AGC Control 2
 */
#define WM9090_AGC_RAMP                         0x0100  /* AGC_RAMP */
#define WM9090_AGC_RAMP_MASK                    0x0100  /* AGC_RAMP */
#define WM9090_AGC_RAMP_SHIFT                        8  /* AGC_RAMP */
#define WM9090_AGC_RAMP_WIDTH                        1  /* AGC_RAMP */
#define WM9090_AGC_MINGAIN_MASK                 0x003F  /* AGC_MINGAIN - [5:0] */
#define WM9090_AGC_MINGAIN_SHIFT                     0  /* AGC_MINGAIN - [5:0] */
#define WM9090_AGC_MINGAIN_WIDTH                     6  /* AGC_MINGAIN - [5:0] */

#endif
