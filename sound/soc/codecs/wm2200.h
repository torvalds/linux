/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * wm2200.h - WM2200 audio codec interface
 *
 * Copyright 2012 Wolfson Microelectronics PLC.
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#ifndef _WM2200_H
#define _WM2200_H

#define WM2200_CLK_SYSCLK 1

#define WM2200_CLKSRC_MCLK1  0
#define WM2200_CLKSRC_MCLK2  1
#define WM2200_CLKSRC_FLL    4
#define WM2200_CLKSRC_BCLK1  8

#define WM2200_FLL_SRC_MCLK1 0
#define WM2200_FLL_SRC_MCLK2 1
#define WM2200_FLL_SRC_BCLK  2

/*
 * Register values.
 */
#define WM2200_SOFTWARE_RESET                   0x00
#define WM2200_DEVICE_REVISION                  0x01
#define WM2200_TONE_GENERATOR_1                 0x0B
#define WM2200_CLOCKING_3                       0x102
#define WM2200_CLOCKING_4                       0x103
#define WM2200_FLL_CONTROL_1                    0x111
#define WM2200_FLL_CONTROL_2                    0x112
#define WM2200_FLL_CONTROL_3                    0x113
#define WM2200_FLL_CONTROL_4                    0x114
#define WM2200_FLL_CONTROL_6                    0x116
#define WM2200_FLL_CONTROL_7                    0x117
#define WM2200_FLL_EFS_1                        0x119
#define WM2200_FLL_EFS_2                        0x11A
#define WM2200_MIC_CHARGE_PUMP_1                0x200
#define WM2200_MIC_CHARGE_PUMP_2                0x201
#define WM2200_DM_CHARGE_PUMP_1                 0x202
#define WM2200_MIC_BIAS_CTRL_1                  0x20C
#define WM2200_MIC_BIAS_CTRL_2                  0x20D
#define WM2200_EAR_PIECE_CTRL_1                 0x20F
#define WM2200_EAR_PIECE_CTRL_2                 0x210
#define WM2200_INPUT_ENABLES                    0x301
#define WM2200_IN1L_CONTROL                     0x302
#define WM2200_IN1R_CONTROL                     0x303
#define WM2200_IN2L_CONTROL                     0x304
#define WM2200_IN2R_CONTROL                     0x305
#define WM2200_IN3L_CONTROL                     0x306
#define WM2200_IN3R_CONTROL                     0x307
#define WM2200_RXANC_SRC                        0x30A
#define WM2200_INPUT_VOLUME_RAMP                0x30B
#define WM2200_ADC_DIGITAL_VOLUME_1L            0x30C
#define WM2200_ADC_DIGITAL_VOLUME_1R            0x30D
#define WM2200_ADC_DIGITAL_VOLUME_2L            0x30E
#define WM2200_ADC_DIGITAL_VOLUME_2R            0x30F
#define WM2200_ADC_DIGITAL_VOLUME_3L            0x310
#define WM2200_ADC_DIGITAL_VOLUME_3R            0x311
#define WM2200_OUTPUT_ENABLES                   0x400
#define WM2200_DAC_VOLUME_LIMIT_1L              0x401
#define WM2200_DAC_VOLUME_LIMIT_1R              0x402
#define WM2200_DAC_VOLUME_LIMIT_2L              0x403
#define WM2200_DAC_VOLUME_LIMIT_2R              0x404
#define WM2200_DAC_AEC_CONTROL_1                0x409
#define WM2200_OUTPUT_VOLUME_RAMP               0x40A
#define WM2200_DAC_DIGITAL_VOLUME_1L            0x40B
#define WM2200_DAC_DIGITAL_VOLUME_1R            0x40C
#define WM2200_DAC_DIGITAL_VOLUME_2L            0x40D
#define WM2200_DAC_DIGITAL_VOLUME_2R            0x40E
#define WM2200_PDM_1                            0x417
#define WM2200_PDM_2                            0x418
#define WM2200_AUDIO_IF_1_1                     0x500
#define WM2200_AUDIO_IF_1_2                     0x501
#define WM2200_AUDIO_IF_1_3                     0x502
#define WM2200_AUDIO_IF_1_4                     0x503
#define WM2200_AUDIO_IF_1_5                     0x504
#define WM2200_AUDIO_IF_1_6                     0x505
#define WM2200_AUDIO_IF_1_7                     0x506
#define WM2200_AUDIO_IF_1_8                     0x507
#define WM2200_AUDIO_IF_1_9                     0x508
#define WM2200_AUDIO_IF_1_10                    0x509
#define WM2200_AUDIO_IF_1_11                    0x50A
#define WM2200_AUDIO_IF_1_12                    0x50B
#define WM2200_AUDIO_IF_1_13                    0x50C
#define WM2200_AUDIO_IF_1_14                    0x50D
#define WM2200_AUDIO_IF_1_15                    0x50E
#define WM2200_AUDIO_IF_1_16                    0x50F
#define WM2200_AUDIO_IF_1_17                    0x510
#define WM2200_AUDIO_IF_1_18                    0x511
#define WM2200_AUDIO_IF_1_19                    0x512
#define WM2200_AUDIO_IF_1_20                    0x513
#define WM2200_AUDIO_IF_1_21                    0x514
#define WM2200_AUDIO_IF_1_22                    0x515
#define WM2200_OUT1LMIX_INPUT_1_SOURCE          0x600
#define WM2200_OUT1LMIX_INPUT_1_VOLUME          0x601
#define WM2200_OUT1LMIX_INPUT_2_SOURCE          0x602
#define WM2200_OUT1LMIX_INPUT_2_VOLUME          0x603
#define WM2200_OUT1LMIX_INPUT_3_SOURCE          0x604
#define WM2200_OUT1LMIX_INPUT_3_VOLUME          0x605
#define WM2200_OUT1LMIX_INPUT_4_SOURCE          0x606
#define WM2200_OUT1LMIX_INPUT_4_VOLUME          0x607
#define WM2200_OUT1RMIX_INPUT_1_SOURCE          0x608
#define WM2200_OUT1RMIX_INPUT_1_VOLUME          0x609
#define WM2200_OUT1RMIX_INPUT_2_SOURCE          0x60A
#define WM2200_OUT1RMIX_INPUT_2_VOLUME          0x60B
#define WM2200_OUT1RMIX_INPUT_3_SOURCE          0x60C
#define WM2200_OUT1RMIX_INPUT_3_VOLUME          0x60D
#define WM2200_OUT1RMIX_INPUT_4_SOURCE          0x60E
#define WM2200_OUT1RMIX_INPUT_4_VOLUME          0x60F
#define WM2200_OUT2LMIX_INPUT_1_SOURCE          0x610
#define WM2200_OUT2LMIX_INPUT_1_VOLUME          0x611
#define WM2200_OUT2LMIX_INPUT_2_SOURCE          0x612
#define WM2200_OUT2LMIX_INPUT_2_VOLUME          0x613
#define WM2200_OUT2LMIX_INPUT_3_SOURCE          0x614
#define WM2200_OUT2LMIX_INPUT_3_VOLUME          0x615
#define WM2200_OUT2LMIX_INPUT_4_SOURCE          0x616
#define WM2200_OUT2LMIX_INPUT_4_VOLUME          0x617
#define WM2200_OUT2RMIX_INPUT_1_SOURCE          0x618
#define WM2200_OUT2RMIX_INPUT_1_VOLUME          0x619
#define WM2200_OUT2RMIX_INPUT_2_SOURCE          0x61A
#define WM2200_OUT2RMIX_INPUT_2_VOLUME          0x61B
#define WM2200_OUT2RMIX_INPUT_3_SOURCE          0x61C
#define WM2200_OUT2RMIX_INPUT_3_VOLUME          0x61D
#define WM2200_OUT2RMIX_INPUT_4_SOURCE          0x61E
#define WM2200_OUT2RMIX_INPUT_4_VOLUME          0x61F
#define WM2200_AIF1TX1MIX_INPUT_1_SOURCE        0x620
#define WM2200_AIF1TX1MIX_INPUT_1_VOLUME        0x621
#define WM2200_AIF1TX1MIX_INPUT_2_SOURCE        0x622
#define WM2200_AIF1TX1MIX_INPUT_2_VOLUME        0x623
#define WM2200_AIF1TX1MIX_INPUT_3_SOURCE        0x624
#define WM2200_AIF1TX1MIX_INPUT_3_VOLUME        0x625
#define WM2200_AIF1TX1MIX_INPUT_4_SOURCE        0x626
#define WM2200_AIF1TX1MIX_INPUT_4_VOLUME        0x627
#define WM2200_AIF1TX2MIX_INPUT_1_SOURCE        0x628
#define WM2200_AIF1TX2MIX_INPUT_1_VOLUME        0x629
#define WM2200_AIF1TX2MIX_INPUT_2_SOURCE        0x62A
#define WM2200_AIF1TX2MIX_INPUT_2_VOLUME        0x62B
#define WM2200_AIF1TX2MIX_INPUT_3_SOURCE        0x62C
#define WM2200_AIF1TX2MIX_INPUT_3_VOLUME        0x62D
#define WM2200_AIF1TX2MIX_INPUT_4_SOURCE        0x62E
#define WM2200_AIF1TX2MIX_INPUT_4_VOLUME        0x62F
#define WM2200_AIF1TX3MIX_INPUT_1_SOURCE        0x630
#define WM2200_AIF1TX3MIX_INPUT_1_VOLUME        0x631
#define WM2200_AIF1TX3MIX_INPUT_2_SOURCE        0x632
#define WM2200_AIF1TX3MIX_INPUT_2_VOLUME        0x633
#define WM2200_AIF1TX3MIX_INPUT_3_SOURCE        0x634
#define WM2200_AIF1TX3MIX_INPUT_3_VOLUME        0x635
#define WM2200_AIF1TX3MIX_INPUT_4_SOURCE        0x636
#define WM2200_AIF1TX3MIX_INPUT_4_VOLUME        0x637
#define WM2200_AIF1TX4MIX_INPUT_1_SOURCE        0x638
#define WM2200_AIF1TX4MIX_INPUT_1_VOLUME        0x639
#define WM2200_AIF1TX4MIX_INPUT_2_SOURCE        0x63A
#define WM2200_AIF1TX4MIX_INPUT_2_VOLUME        0x63B
#define WM2200_AIF1TX4MIX_INPUT_3_SOURCE        0x63C
#define WM2200_AIF1TX4MIX_INPUT_3_VOLUME        0x63D
#define WM2200_AIF1TX4MIX_INPUT_4_SOURCE        0x63E
#define WM2200_AIF1TX4MIX_INPUT_4_VOLUME        0x63F
#define WM2200_AIF1TX5MIX_INPUT_1_SOURCE        0x640
#define WM2200_AIF1TX5MIX_INPUT_1_VOLUME        0x641
#define WM2200_AIF1TX5MIX_INPUT_2_SOURCE        0x642
#define WM2200_AIF1TX5MIX_INPUT_2_VOLUME        0x643
#define WM2200_AIF1TX5MIX_INPUT_3_SOURCE        0x644
#define WM2200_AIF1TX5MIX_INPUT_3_VOLUME        0x645
#define WM2200_AIF1TX5MIX_INPUT_4_SOURCE        0x646
#define WM2200_AIF1TX5MIX_INPUT_4_VOLUME        0x647
#define WM2200_AIF1TX6MIX_INPUT_1_SOURCE        0x648
#define WM2200_AIF1TX6MIX_INPUT_1_VOLUME        0x649
#define WM2200_AIF1TX6MIX_INPUT_2_SOURCE        0x64A
#define WM2200_AIF1TX6MIX_INPUT_2_VOLUME        0x64B
#define WM2200_AIF1TX6MIX_INPUT_3_SOURCE        0x64C
#define WM2200_AIF1TX6MIX_INPUT_3_VOLUME        0x64D
#define WM2200_AIF1TX6MIX_INPUT_4_SOURCE        0x64E
#define WM2200_AIF1TX6MIX_INPUT_4_VOLUME        0x64F
#define WM2200_EQLMIX_INPUT_1_SOURCE            0x650
#define WM2200_EQLMIX_INPUT_1_VOLUME            0x651
#define WM2200_EQLMIX_INPUT_2_SOURCE            0x652
#define WM2200_EQLMIX_INPUT_2_VOLUME            0x653
#define WM2200_EQLMIX_INPUT_3_SOURCE            0x654
#define WM2200_EQLMIX_INPUT_3_VOLUME            0x655
#define WM2200_EQLMIX_INPUT_4_SOURCE            0x656
#define WM2200_EQLMIX_INPUT_4_VOLUME            0x657
#define WM2200_EQRMIX_INPUT_1_SOURCE            0x658
#define WM2200_EQRMIX_INPUT_1_VOLUME            0x659
#define WM2200_EQRMIX_INPUT_2_SOURCE            0x65A
#define WM2200_EQRMIX_INPUT_2_VOLUME            0x65B
#define WM2200_EQRMIX_INPUT_3_SOURCE            0x65C
#define WM2200_EQRMIX_INPUT_3_VOLUME            0x65D
#define WM2200_EQRMIX_INPUT_4_SOURCE            0x65E
#define WM2200_EQRMIX_INPUT_4_VOLUME            0x65F
#define WM2200_LHPF1MIX_INPUT_1_SOURCE          0x660
#define WM2200_LHPF1MIX_INPUT_1_VOLUME          0x661
#define WM2200_LHPF1MIX_INPUT_2_SOURCE          0x662
#define WM2200_LHPF1MIX_INPUT_2_VOLUME          0x663
#define WM2200_LHPF1MIX_INPUT_3_SOURCE          0x664
#define WM2200_LHPF1MIX_INPUT_3_VOLUME          0x665
#define WM2200_LHPF1MIX_INPUT_4_SOURCE          0x666
#define WM2200_LHPF1MIX_INPUT_4_VOLUME          0x667
#define WM2200_LHPF2MIX_INPUT_1_SOURCE          0x668
#define WM2200_LHPF2MIX_INPUT_1_VOLUME          0x669
#define WM2200_LHPF2MIX_INPUT_2_SOURCE          0x66A
#define WM2200_LHPF2MIX_INPUT_2_VOLUME          0x66B
#define WM2200_LHPF2MIX_INPUT_3_SOURCE          0x66C
#define WM2200_LHPF2MIX_INPUT_3_VOLUME          0x66D
#define WM2200_LHPF2MIX_INPUT_4_SOURCE          0x66E
#define WM2200_LHPF2MIX_INPUT_4_VOLUME          0x66F
#define WM2200_DSP1LMIX_INPUT_1_SOURCE          0x670
#define WM2200_DSP1LMIX_INPUT_1_VOLUME          0x671
#define WM2200_DSP1LMIX_INPUT_2_SOURCE          0x672
#define WM2200_DSP1LMIX_INPUT_2_VOLUME          0x673
#define WM2200_DSP1LMIX_INPUT_3_SOURCE          0x674
#define WM2200_DSP1LMIX_INPUT_3_VOLUME          0x675
#define WM2200_DSP1LMIX_INPUT_4_SOURCE          0x676
#define WM2200_DSP1LMIX_INPUT_4_VOLUME          0x677
#define WM2200_DSP1RMIX_INPUT_1_SOURCE          0x678
#define WM2200_DSP1RMIX_INPUT_1_VOLUME          0x679
#define WM2200_DSP1RMIX_INPUT_2_SOURCE          0x67A
#define WM2200_DSP1RMIX_INPUT_2_VOLUME          0x67B
#define WM2200_DSP1RMIX_INPUT_3_SOURCE          0x67C
#define WM2200_DSP1RMIX_INPUT_3_VOLUME          0x67D
#define WM2200_DSP1RMIX_INPUT_4_SOURCE          0x67E
#define WM2200_DSP1RMIX_INPUT_4_VOLUME          0x67F
#define WM2200_DSP1AUX1MIX_INPUT_1_SOURCE       0x680
#define WM2200_DSP1AUX2MIX_INPUT_1_SOURCE       0x681
#define WM2200_DSP1AUX3MIX_INPUT_1_SOURCE       0x682
#define WM2200_DSP1AUX4MIX_INPUT_1_SOURCE       0x683
#define WM2200_DSP1AUX5MIX_INPUT_1_SOURCE       0x684
#define WM2200_DSP1AUX6MIX_INPUT_1_SOURCE       0x685
#define WM2200_DSP2LMIX_INPUT_1_SOURCE          0x686
#define WM2200_DSP2LMIX_INPUT_1_VOLUME          0x687
#define WM2200_DSP2LMIX_INPUT_2_SOURCE          0x688
#define WM2200_DSP2LMIX_INPUT_2_VOLUME          0x689
#define WM2200_DSP2LMIX_INPUT_3_SOURCE          0x68A
#define WM2200_DSP2LMIX_INPUT_3_VOLUME          0x68B
#define WM2200_DSP2LMIX_INPUT_4_SOURCE          0x68C
#define WM2200_DSP2LMIX_INPUT_4_VOLUME          0x68D
#define WM2200_DSP2RMIX_INPUT_1_SOURCE          0x68E
#define WM2200_DSP2RMIX_INPUT_1_VOLUME          0x68F
#define WM2200_DSP2RMIX_INPUT_2_SOURCE          0x690
#define WM2200_DSP2RMIX_INPUT_2_VOLUME          0x691
#define WM2200_DSP2RMIX_INPUT_3_SOURCE          0x692
#define WM2200_DSP2RMIX_INPUT_3_VOLUME          0x693
#define WM2200_DSP2RMIX_INPUT_4_SOURCE          0x694
#define WM2200_DSP2RMIX_INPUT_4_VOLUME          0x695
#define WM2200_DSP2AUX1MIX_INPUT_1_SOURCE       0x696
#define WM2200_DSP2AUX2MIX_INPUT_1_SOURCE       0x697
#define WM2200_DSP2AUX3MIX_INPUT_1_SOURCE       0x698
#define WM2200_DSP2AUX4MIX_INPUT_1_SOURCE       0x699
#define WM2200_DSP2AUX5MIX_INPUT_1_SOURCE       0x69A
#define WM2200_DSP2AUX6MIX_INPUT_1_SOURCE       0x69B
#define WM2200_GPIO_CTRL_1                      0x700
#define WM2200_GPIO_CTRL_2                      0x701
#define WM2200_GPIO_CTRL_3                      0x702
#define WM2200_GPIO_CTRL_4                      0x703
#define WM2200_ADPS1_IRQ0                       0x707
#define WM2200_ADPS1_IRQ1                       0x708
#define WM2200_MISC_PAD_CTRL_1                  0x709
#define WM2200_INTERRUPT_STATUS_1               0x800
#define WM2200_INTERRUPT_STATUS_1_MASK          0x801
#define WM2200_INTERRUPT_STATUS_2               0x802
#define WM2200_INTERRUPT_RAW_STATUS_2           0x803
#define WM2200_INTERRUPT_STATUS_2_MASK          0x804
#define WM2200_INTERRUPT_CONTROL                0x808
#define WM2200_EQL_1                            0x900
#define WM2200_EQL_2                            0x901
#define WM2200_EQL_3                            0x902
#define WM2200_EQL_4                            0x903
#define WM2200_EQL_5                            0x904
#define WM2200_EQL_6                            0x905
#define WM2200_EQL_7                            0x906
#define WM2200_EQL_8                            0x907
#define WM2200_EQL_9                            0x908
#define WM2200_EQL_10                           0x909
#define WM2200_EQL_11                           0x90A
#define WM2200_EQL_12                           0x90B
#define WM2200_EQL_13                           0x90C
#define WM2200_EQL_14                           0x90D
#define WM2200_EQL_15                           0x90E
#define WM2200_EQL_16                           0x90F
#define WM2200_EQL_17                           0x910
#define WM2200_EQL_18                           0x911
#define WM2200_EQL_19                           0x912
#define WM2200_EQL_20                           0x913
#define WM2200_EQR_1                            0x916
#define WM2200_EQR_2                            0x917
#define WM2200_EQR_3                            0x918
#define WM2200_EQR_4                            0x919
#define WM2200_EQR_5                            0x91A
#define WM2200_EQR_6                            0x91B
#define WM2200_EQR_7                            0x91C
#define WM2200_EQR_8                            0x91D
#define WM2200_EQR_9                            0x91E
#define WM2200_EQR_10                           0x91F
#define WM2200_EQR_11                           0x920
#define WM2200_EQR_12                           0x921
#define WM2200_EQR_13                           0x922
#define WM2200_EQR_14                           0x923
#define WM2200_EQR_15                           0x924
#define WM2200_EQR_16                           0x925
#define WM2200_EQR_17                           0x926
#define WM2200_EQR_18                           0x927
#define WM2200_EQR_19                           0x928
#define WM2200_EQR_20                           0x929
#define WM2200_HPLPF1_1                         0x93E
#define WM2200_HPLPF1_2                         0x93F
#define WM2200_HPLPF2_1                         0x942
#define WM2200_HPLPF2_2                         0x943
#define WM2200_DSP1_CONTROL_1                   0xA00
#define WM2200_DSP1_CONTROL_2                   0xA02
#define WM2200_DSP1_CONTROL_3                   0xA03
#define WM2200_DSP1_CONTROL_4                   0xA04
#define WM2200_DSP1_CONTROL_5                   0xA06
#define WM2200_DSP1_CONTROL_6                   0xA07
#define WM2200_DSP1_CONTROL_7                   0xA08
#define WM2200_DSP1_CONTROL_8                   0xA09
#define WM2200_DSP1_CONTROL_9                   0xA0A
#define WM2200_DSP1_CONTROL_10                  0xA0B
#define WM2200_DSP1_CONTROL_11                  0xA0C
#define WM2200_DSP1_CONTROL_12                  0xA0D
#define WM2200_DSP1_CONTROL_13                  0xA0F
#define WM2200_DSP1_CONTROL_14                  0xA10
#define WM2200_DSP1_CONTROL_15                  0xA11
#define WM2200_DSP1_CONTROL_16                  0xA12
#define WM2200_DSP1_CONTROL_17                  0xA13
#define WM2200_DSP1_CONTROL_18                  0xA14
#define WM2200_DSP1_CONTROL_19                  0xA16
#define WM2200_DSP1_CONTROL_20                  0xA17
#define WM2200_DSP1_CONTROL_21                  0xA18
#define WM2200_DSP1_CONTROL_22                  0xA1A
#define WM2200_DSP1_CONTROL_23                  0xA1B
#define WM2200_DSP1_CONTROL_24                  0xA1C
#define WM2200_DSP1_CONTROL_25                  0xA1E
#define WM2200_DSP1_CONTROL_26                  0xA20
#define WM2200_DSP1_CONTROL_27                  0xA21
#define WM2200_DSP1_CONTROL_28                  0xA22
#define WM2200_DSP1_CONTROL_29                  0xA23
#define WM2200_DSP1_CONTROL_30                  0xA24
#define WM2200_DSP1_CONTROL_31                  0xA26
#define WM2200_DSP2_CONTROL_1                   0xB00
#define WM2200_DSP2_CONTROL_2                   0xB02
#define WM2200_DSP2_CONTROL_3                   0xB03
#define WM2200_DSP2_CONTROL_4                   0xB04
#define WM2200_DSP2_CONTROL_5                   0xB06
#define WM2200_DSP2_CONTROL_6                   0xB07
#define WM2200_DSP2_CONTROL_7                   0xB08
#define WM2200_DSP2_CONTROL_8                   0xB09
#define WM2200_DSP2_CONTROL_9                   0xB0A
#define WM2200_DSP2_CONTROL_10                  0xB0B
#define WM2200_DSP2_CONTROL_11                  0xB0C
#define WM2200_DSP2_CONTROL_12                  0xB0D
#define WM2200_DSP2_CONTROL_13                  0xB0F
#define WM2200_DSP2_CONTROL_14                  0xB10
#define WM2200_DSP2_CONTROL_15                  0xB11
#define WM2200_DSP2_CONTROL_16                  0xB12
#define WM2200_DSP2_CONTROL_17                  0xB13
#define WM2200_DSP2_CONTROL_18                  0xB14
#define WM2200_DSP2_CONTROL_19                  0xB16
#define WM2200_DSP2_CONTROL_20                  0xB17
#define WM2200_DSP2_CONTROL_21                  0xB18
#define WM2200_DSP2_CONTROL_22                  0xB1A
#define WM2200_DSP2_CONTROL_23                  0xB1B
#define WM2200_DSP2_CONTROL_24                  0xB1C
#define WM2200_DSP2_CONTROL_25                  0xB1E
#define WM2200_DSP2_CONTROL_26                  0xB20
#define WM2200_DSP2_CONTROL_27                  0xB21
#define WM2200_DSP2_CONTROL_28                  0xB22
#define WM2200_DSP2_CONTROL_29                  0xB23
#define WM2200_DSP2_CONTROL_30                  0xB24
#define WM2200_DSP2_CONTROL_31                  0xB26
#define WM2200_ANC_CTRL1                        0xD00
#define WM2200_ANC_CTRL2                        0xD01
#define WM2200_ANC_CTRL3                        0xD02
#define WM2200_ANC_CTRL7                        0xD08
#define WM2200_ANC_CTRL8                        0xD09
#define WM2200_ANC_CTRL9                        0xD0A
#define WM2200_ANC_CTRL10                       0xD0B
#define WM2200_ANC_CTRL11                       0xD0C
#define WM2200_ANC_CTRL12                       0xD0D
#define WM2200_ANC_CTRL13                       0xD0E
#define WM2200_ANC_CTRL14                       0xD0F
#define WM2200_ANC_CTRL15                       0xD10
#define WM2200_ANC_CTRL16                       0xD11
#define WM2200_ANC_CTRL17                       0xD12
#define WM2200_ANC_CTRL18                       0xD15
#define WM2200_ANC_CTRL19                       0xD16
#define WM2200_ANC_CTRL20                       0xD17
#define WM2200_ANC_CTRL21                       0xD18
#define WM2200_ANC_CTRL22                       0xD19
#define WM2200_ANC_CTRL23                       0xD1A
#define WM2200_ANC_CTRL24                       0xD1B
#define WM2200_ANC_CTRL25                       0xD1C
#define WM2200_ANC_CTRL26                       0xD1D
#define WM2200_ANC_CTRL27                       0xD1E
#define WM2200_ANC_CTRL28                       0xD1F
#define WM2200_ANC_CTRL29                       0xD20
#define WM2200_ANC_CTRL30                       0xD21
#define WM2200_ANC_CTRL31                       0xD23
#define WM2200_ANC_CTRL32                       0xD24
#define WM2200_ANC_CTRL33                       0xD25
#define WM2200_ANC_CTRL34                       0xD27
#define WM2200_ANC_CTRL35                       0xD28
#define WM2200_ANC_CTRL36                       0xD29
#define WM2200_ANC_CTRL37                       0xD2A
#define WM2200_ANC_CTRL38                       0xD2B
#define WM2200_ANC_CTRL39                       0xD2C
#define WM2200_ANC_CTRL40                       0xD2D
#define WM2200_ANC_CTRL41                       0xD2E
#define WM2200_ANC_CTRL42                       0xD2F
#define WM2200_ANC_CTRL43                       0xD30
#define WM2200_ANC_CTRL44                       0xD31
#define WM2200_ANC_CTRL45                       0xD32
#define WM2200_ANC_CTRL46                       0xD33
#define WM2200_ANC_CTRL47                       0xD34
#define WM2200_ANC_CTRL48                       0xD35
#define WM2200_ANC_CTRL49                       0xD36
#define WM2200_ANC_CTRL50                       0xD37
#define WM2200_ANC_CTRL51                       0xD38
#define WM2200_ANC_CTRL52                       0xD39
#define WM2200_ANC_CTRL53                       0xD3A
#define WM2200_ANC_CTRL54                       0xD3B
#define WM2200_ANC_CTRL55                       0xD3C
#define WM2200_ANC_CTRL56                       0xD3D
#define WM2200_ANC_CTRL57                       0xD3E
#define WM2200_ANC_CTRL58                       0xD3F
#define WM2200_ANC_CTRL59                       0xD40
#define WM2200_ANC_CTRL60                       0xD41
#define WM2200_ANC_CTRL61                       0xD42
#define WM2200_ANC_CTRL62                       0xD43
#define WM2200_ANC_CTRL63                       0xD44
#define WM2200_ANC_CTRL64                       0xD45
#define WM2200_ANC_CTRL65                       0xD46
#define WM2200_ANC_CTRL66                       0xD47
#define WM2200_ANC_CTRL67                       0xD48
#define WM2200_ANC_CTRL68                       0xD49
#define WM2200_ANC_CTRL69                       0xD4A
#define WM2200_ANC_CTRL70                       0xD4B
#define WM2200_ANC_CTRL71                       0xD4C
#define WM2200_ANC_CTRL72                       0xD4D
#define WM2200_ANC_CTRL73                       0xD4E
#define WM2200_ANC_CTRL74                       0xD4F
#define WM2200_ANC_CTRL75                       0xD50
#define WM2200_ANC_CTRL76                       0xD51
#define WM2200_ANC_CTRL77                       0xD52
#define WM2200_ANC_CTRL78                       0xD53
#define WM2200_ANC_CTRL79                       0xD54
#define WM2200_ANC_CTRL80                       0xD55
#define WM2200_ANC_CTRL81                       0xD56
#define WM2200_ANC_CTRL82                       0xD57
#define WM2200_ANC_CTRL83                       0xD58
#define WM2200_ANC_CTRL84                       0xD5B
#define WM2200_ANC_CTRL85                       0xD5C
#define WM2200_ANC_CTRL86                       0xD5F
#define WM2200_ANC_CTRL87                       0xD60
#define WM2200_ANC_CTRL88                       0xD61
#define WM2200_ANC_CTRL89                       0xD62
#define WM2200_ANC_CTRL90                       0xD63
#define WM2200_ANC_CTRL91                       0xD64
#define WM2200_ANC_CTRL92                       0xD65
#define WM2200_ANC_CTRL93                       0xD66
#define WM2200_ANC_CTRL94                       0xD67
#define WM2200_ANC_CTRL95                       0xD68
#define WM2200_ANC_CTRL96                       0xD69
#define WM2200_DSP1_DM_0                        0x3000
#define WM2200_DSP1_DM_1                        0x3001
#define WM2200_DSP1_DM_2                        0x3002
#define WM2200_DSP1_DM_3                        0x3003
#define WM2200_DSP1_DM_2044                     0x37FC
#define WM2200_DSP1_DM_2045                     0x37FD
#define WM2200_DSP1_DM_2046                     0x37FE
#define WM2200_DSP1_DM_2047                     0x37FF
#define WM2200_DSP1_PM_0                        0x3800
#define WM2200_DSP1_PM_1                        0x3801
#define WM2200_DSP1_PM_2                        0x3802
#define WM2200_DSP1_PM_3                        0x3803
#define WM2200_DSP1_PM_4                        0x3804
#define WM2200_DSP1_PM_5                        0x3805
#define WM2200_DSP1_PM_762                      0x3AFA
#define WM2200_DSP1_PM_763                      0x3AFB
#define WM2200_DSP1_PM_764                      0x3AFC
#define WM2200_DSP1_PM_765                      0x3AFD
#define WM2200_DSP1_PM_766                      0x3AFE
#define WM2200_DSP1_PM_767                      0x3AFF
#define WM2200_DSP1_ZM_0                        0x3C00
#define WM2200_DSP1_ZM_1                        0x3C01
#define WM2200_DSP1_ZM_2                        0x3C02
#define WM2200_DSP1_ZM_3                        0x3C03
#define WM2200_DSP1_ZM_1020                     0x3FFC
#define WM2200_DSP1_ZM_1021                     0x3FFD
#define WM2200_DSP1_ZM_1022                     0x3FFE
#define WM2200_DSP1_ZM_1023                     0x3FFF
#define WM2200_DSP2_DM_0                        0x4000
#define WM2200_DSP2_DM_1                        0x4001
#define WM2200_DSP2_DM_2                        0x4002
#define WM2200_DSP2_DM_3                        0x4003
#define WM2200_DSP2_DM_2044                     0x47FC
#define WM2200_DSP2_DM_2045                     0x47FD
#define WM2200_DSP2_DM_2046                     0x47FE
#define WM2200_DSP2_DM_2047                     0x47FF
#define WM2200_DSP2_PM_0                        0x4800
#define WM2200_DSP2_PM_1                        0x4801
#define WM2200_DSP2_PM_2                        0x4802
#define WM2200_DSP2_PM_3                        0x4803
#define WM2200_DSP2_PM_4                        0x4804
#define WM2200_DSP2_PM_5                        0x4805
#define WM2200_DSP2_PM_762                      0x4AFA
#define WM2200_DSP2_PM_763                      0x4AFB
#define WM2200_DSP2_PM_764                      0x4AFC
#define WM2200_DSP2_PM_765                      0x4AFD
#define WM2200_DSP2_PM_766                      0x4AFE
#define WM2200_DSP2_PM_767                      0x4AFF
#define WM2200_DSP2_ZM_0                        0x4C00
#define WM2200_DSP2_ZM_1                        0x4C01
#define WM2200_DSP2_ZM_2                        0x4C02
#define WM2200_DSP2_ZM_3                        0x4C03
#define WM2200_DSP2_ZM_1020                     0x4FFC
#define WM2200_DSP2_ZM_1021                     0x4FFD
#define WM2200_DSP2_ZM_1022                     0x4FFE
#define WM2200_DSP2_ZM_1023                     0x4FFF

#define WM2200_REGISTER_COUNT                   494
#define WM2200_MAX_REGISTER                     0x4FFF

/*
 * Field Definitions.
 */

/*
 * R0 (0x00) - software reset
 */
#define WM2200_SW_RESET_CHIP_ID1_MASK           0xFFFF  /* SW_RESET_CHIP_ID1 - [15:0] */
#define WM2200_SW_RESET_CHIP_ID1_SHIFT               0  /* SW_RESET_CHIP_ID1 - [15:0] */
#define WM2200_SW_RESET_CHIP_ID1_WIDTH              16  /* SW_RESET_CHIP_ID1 - [15:0] */

/*
 * R1 (0x01) - Device Revision
 */
#define WM2200_DEVICE_REVISION_MASK             0x000F  /* DEVICE_REVISION - [3:0] */
#define WM2200_DEVICE_REVISION_SHIFT                 0  /* DEVICE_REVISION - [3:0] */
#define WM2200_DEVICE_REVISION_WIDTH                 4  /* DEVICE_REVISION - [3:0] */

/*
 * R11 (0x0B) - Tone Generator 1
 */
#define WM2200_TONE_ENA                         0x0001  /* TONE_ENA */
#define WM2200_TONE_ENA_MASK                    0x0001  /* TONE_ENA */
#define WM2200_TONE_ENA_SHIFT                        0  /* TONE_ENA */
#define WM2200_TONE_ENA_WIDTH                        1  /* TONE_ENA */

/*
 * R258 (0x102) - Clocking 3
 */
#define WM2200_SYSCLK_FREQ_MASK                 0x0700  /* SYSCLK_FREQ - [10:8] */
#define WM2200_SYSCLK_FREQ_SHIFT                     8  /* SYSCLK_FREQ - [10:8] */
#define WM2200_SYSCLK_FREQ_WIDTH                     3  /* SYSCLK_FREQ - [10:8] */
#define WM2200_SYSCLK_ENA                       0x0040  /* SYSCLK_ENA */
#define WM2200_SYSCLK_ENA_MASK                  0x0040  /* SYSCLK_ENA */
#define WM2200_SYSCLK_ENA_SHIFT                      6  /* SYSCLK_ENA */
#define WM2200_SYSCLK_ENA_WIDTH                      1  /* SYSCLK_ENA */
#define WM2200_SYSCLK_SRC_MASK                  0x000F  /* SYSCLK_SRC - [3:0] */
#define WM2200_SYSCLK_SRC_SHIFT                      0  /* SYSCLK_SRC - [3:0] */
#define WM2200_SYSCLK_SRC_WIDTH                      4  /* SYSCLK_SRC - [3:0] */

/*
 * R259 (0x103) - Clocking 4
 */
#define WM2200_SAMPLE_RATE_1_MASK               0x001F  /* SAMPLE_RATE_1 - [4:0] */
#define WM2200_SAMPLE_RATE_1_SHIFT                   0  /* SAMPLE_RATE_1 - [4:0] */
#define WM2200_SAMPLE_RATE_1_WIDTH                   5  /* SAMPLE_RATE_1 - [4:0] */

/*
 * R273 (0x111) - FLL Control 1
 */
#define WM2200_FLL_ENA                          0x0001  /* FLL_ENA */
#define WM2200_FLL_ENA_MASK                     0x0001  /* FLL_ENA */
#define WM2200_FLL_ENA_SHIFT                         0  /* FLL_ENA */
#define WM2200_FLL_ENA_WIDTH                         1  /* FLL_ENA */

/*
 * R274 (0x112) - FLL Control 2
 */
#define WM2200_FLL_OUTDIV_MASK                  0x3F00  /* FLL_OUTDIV - [13:8] */
#define WM2200_FLL_OUTDIV_SHIFT                      8  /* FLL_OUTDIV - [13:8] */
#define WM2200_FLL_OUTDIV_WIDTH                      6  /* FLL_OUTDIV - [13:8] */
#define WM2200_FLL_FRATIO_MASK                  0x0007  /* FLL_FRATIO - [2:0] */
#define WM2200_FLL_FRATIO_SHIFT                      0  /* FLL_FRATIO - [2:0] */
#define WM2200_FLL_FRATIO_WIDTH                      3  /* FLL_FRATIO - [2:0] */

/*
 * R275 (0x113) - FLL Control 3
 */
#define WM2200_FLL_FRACN_ENA                    0x0001  /* FLL_FRACN_ENA */
#define WM2200_FLL_FRACN_ENA_MASK               0x0001  /* FLL_FRACN_ENA */
#define WM2200_FLL_FRACN_ENA_SHIFT                   0  /* FLL_FRACN_ENA */
#define WM2200_FLL_FRACN_ENA_WIDTH                   1  /* FLL_FRACN_ENA */

/*
 * R276 (0x114) - FLL Control 4
 */
#define WM2200_FLL_THETA_MASK                   0xFFFF  /* FLL_THETA - [15:0] */
#define WM2200_FLL_THETA_SHIFT                       0  /* FLL_THETA - [15:0] */
#define WM2200_FLL_THETA_WIDTH                      16  /* FLL_THETA - [15:0] */

/*
 * R278 (0x116) - FLL Control 6
 */
#define WM2200_FLL_N_MASK                       0x03FF  /* FLL_N - [9:0] */
#define WM2200_FLL_N_SHIFT                           0  /* FLL_N - [9:0] */
#define WM2200_FLL_N_WIDTH                          10  /* FLL_N - [9:0] */

/*
 * R279 (0x117) - FLL Control 7
 */
#define WM2200_FLL_CLK_REF_DIV_MASK             0x0030  /* FLL_CLK_REF_DIV - [5:4] */
#define WM2200_FLL_CLK_REF_DIV_SHIFT                 4  /* FLL_CLK_REF_DIV - [5:4] */
#define WM2200_FLL_CLK_REF_DIV_WIDTH                 2  /* FLL_CLK_REF_DIV - [5:4] */
#define WM2200_FLL_CLK_REF_SRC_MASK             0x0003  /* FLL_CLK_REF_SRC - [1:0] */
#define WM2200_FLL_CLK_REF_SRC_SHIFT                 0  /* FLL_CLK_REF_SRC - [1:0] */
#define WM2200_FLL_CLK_REF_SRC_WIDTH                 2  /* FLL_CLK_REF_SRC - [1:0] */

/*
 * R281 (0x119) - FLL EFS 1
 */
#define WM2200_FLL_LAMBDA_MASK                  0xFFFF  /* FLL_LAMBDA - [15:0] */
#define WM2200_FLL_LAMBDA_SHIFT                      0  /* FLL_LAMBDA - [15:0] */
#define WM2200_FLL_LAMBDA_WIDTH                     16  /* FLL_LAMBDA - [15:0] */

/*
 * R282 (0x11A) - FLL EFS 2
 */
#define WM2200_FLL_EFS_ENA                      0x0001  /* FLL_EFS_ENA */
#define WM2200_FLL_EFS_ENA_MASK                 0x0001  /* FLL_EFS_ENA */
#define WM2200_FLL_EFS_ENA_SHIFT                     0  /* FLL_EFS_ENA */
#define WM2200_FLL_EFS_ENA_WIDTH                     1  /* FLL_EFS_ENA */

/*
 * R512 (0x200) - Mic Charge Pump 1
 */
#define WM2200_CPMIC_BYPASS_MODE                0x0020  /* CPMIC_BYPASS_MODE */
#define WM2200_CPMIC_BYPASS_MODE_MASK           0x0020  /* CPMIC_BYPASS_MODE */
#define WM2200_CPMIC_BYPASS_MODE_SHIFT               5  /* CPMIC_BYPASS_MODE */
#define WM2200_CPMIC_BYPASS_MODE_WIDTH               1  /* CPMIC_BYPASS_MODE */
#define WM2200_CPMIC_ENA                        0x0001  /* CPMIC_ENA */
#define WM2200_CPMIC_ENA_MASK                   0x0001  /* CPMIC_ENA */
#define WM2200_CPMIC_ENA_SHIFT                       0  /* CPMIC_ENA */
#define WM2200_CPMIC_ENA_WIDTH                       1  /* CPMIC_ENA */

/*
 * R513 (0x201) - Mic Charge Pump 2
 */
#define WM2200_CPMIC_LDO_VSEL_OVERRIDE_MASK     0xF800  /* CPMIC_LDO_VSEL_OVERRIDE - [15:11] */
#define WM2200_CPMIC_LDO_VSEL_OVERRIDE_SHIFT        11  /* CPMIC_LDO_VSEL_OVERRIDE - [15:11] */
#define WM2200_CPMIC_LDO_VSEL_OVERRIDE_WIDTH         5  /* CPMIC_LDO_VSEL_OVERRIDE - [15:11] */

/*
 * R514 (0x202) - DM Charge Pump 1
 */
#define WM2200_CPDM_ENA                         0x0001  /* CPDM_ENA */
#define WM2200_CPDM_ENA_MASK                    0x0001  /* CPDM_ENA */
#define WM2200_CPDM_ENA_SHIFT                        0  /* CPDM_ENA */
#define WM2200_CPDM_ENA_WIDTH                        1  /* CPDM_ENA */

/*
 * R524 (0x20C) - Mic Bias Ctrl 1
 */
#define WM2200_MICB1_DISCH                      0x0040  /* MICB1_DISCH */
#define WM2200_MICB1_DISCH_MASK                 0x0040  /* MICB1_DISCH */
#define WM2200_MICB1_DISCH_SHIFT                     6  /* MICB1_DISCH */
#define WM2200_MICB1_DISCH_WIDTH                     1  /* MICB1_DISCH */
#define WM2200_MICB1_RATE                       0x0020  /* MICB1_RATE */
#define WM2200_MICB1_RATE_MASK                  0x0020  /* MICB1_RATE */
#define WM2200_MICB1_RATE_SHIFT                      5  /* MICB1_RATE */
#define WM2200_MICB1_RATE_WIDTH                      1  /* MICB1_RATE */
#define WM2200_MICB1_LVL_MASK                   0x001C  /* MICB1_LVL - [4:2] */
#define WM2200_MICB1_LVL_SHIFT                       2  /* MICB1_LVL - [4:2] */
#define WM2200_MICB1_LVL_WIDTH                       3  /* MICB1_LVL - [4:2] */
#define WM2200_MICB1_MODE                       0x0002  /* MICB1_MODE */
#define WM2200_MICB1_MODE_MASK                  0x0002  /* MICB1_MODE */
#define WM2200_MICB1_MODE_SHIFT                      1  /* MICB1_MODE */
#define WM2200_MICB1_MODE_WIDTH                      1  /* MICB1_MODE */
#define WM2200_MICB1_ENA                        0x0001  /* MICB1_ENA */
#define WM2200_MICB1_ENA_MASK                   0x0001  /* MICB1_ENA */
#define WM2200_MICB1_ENA_SHIFT                       0  /* MICB1_ENA */
#define WM2200_MICB1_ENA_WIDTH                       1  /* MICB1_ENA */

/*
 * R525 (0x20D) - Mic Bias Ctrl 2
 */
#define WM2200_MICB2_DISCH                      0x0040  /* MICB2_DISCH */
#define WM2200_MICB2_DISCH_MASK                 0x0040  /* MICB2_DISCH */
#define WM2200_MICB2_DISCH_SHIFT                     6  /* MICB2_DISCH */
#define WM2200_MICB2_DISCH_WIDTH                     1  /* MICB2_DISCH */
#define WM2200_MICB2_RATE                       0x0020  /* MICB2_RATE */
#define WM2200_MICB2_RATE_MASK                  0x0020  /* MICB2_RATE */
#define WM2200_MICB2_RATE_SHIFT                      5  /* MICB2_RATE */
#define WM2200_MICB2_RATE_WIDTH                      1  /* MICB2_RATE */
#define WM2200_MICB2_LVL_MASK                   0x001C  /* MICB2_LVL - [4:2] */
#define WM2200_MICB2_LVL_SHIFT                       2  /* MICB2_LVL - [4:2] */
#define WM2200_MICB2_LVL_WIDTH                       3  /* MICB2_LVL - [4:2] */
#define WM2200_MICB2_MODE                       0x0002  /* MICB2_MODE */
#define WM2200_MICB2_MODE_MASK                  0x0002  /* MICB2_MODE */
#define WM2200_MICB2_MODE_SHIFT                      1  /* MICB2_MODE */
#define WM2200_MICB2_MODE_WIDTH                      1  /* MICB2_MODE */
#define WM2200_MICB2_ENA                        0x0001  /* MICB2_ENA */
#define WM2200_MICB2_ENA_MASK                   0x0001  /* MICB2_ENA */
#define WM2200_MICB2_ENA_SHIFT                       0  /* MICB2_ENA */
#define WM2200_MICB2_ENA_WIDTH                       1  /* MICB2_ENA */

/*
 * R527 (0x20F) - Ear Piece Ctrl 1
 */
#define WM2200_EPD_LP_ENA                       0x4000  /* EPD_LP_ENA */
#define WM2200_EPD_LP_ENA_MASK                  0x4000  /* EPD_LP_ENA */
#define WM2200_EPD_LP_ENA_SHIFT                     14  /* EPD_LP_ENA */
#define WM2200_EPD_LP_ENA_WIDTH                      1  /* EPD_LP_ENA */
#define WM2200_EPD_OUTP_LP_ENA                  0x2000  /* EPD_OUTP_LP_ENA */
#define WM2200_EPD_OUTP_LP_ENA_MASK             0x2000  /* EPD_OUTP_LP_ENA */
#define WM2200_EPD_OUTP_LP_ENA_SHIFT                13  /* EPD_OUTP_LP_ENA */
#define WM2200_EPD_OUTP_LP_ENA_WIDTH                 1  /* EPD_OUTP_LP_ENA */
#define WM2200_EPD_RMV_SHRT_LP                  0x1000  /* EPD_RMV_SHRT_LP */
#define WM2200_EPD_RMV_SHRT_LP_MASK             0x1000  /* EPD_RMV_SHRT_LP */
#define WM2200_EPD_RMV_SHRT_LP_SHIFT                12  /* EPD_RMV_SHRT_LP */
#define WM2200_EPD_RMV_SHRT_LP_WIDTH                 1  /* EPD_RMV_SHRT_LP */
#define WM2200_EPD_LN_ENA                       0x0800  /* EPD_LN_ENA */
#define WM2200_EPD_LN_ENA_MASK                  0x0800  /* EPD_LN_ENA */
#define WM2200_EPD_LN_ENA_SHIFT                     11  /* EPD_LN_ENA */
#define WM2200_EPD_LN_ENA_WIDTH                      1  /* EPD_LN_ENA */
#define WM2200_EPD_OUTP_LN_ENA                  0x0400  /* EPD_OUTP_LN_ENA */
#define WM2200_EPD_OUTP_LN_ENA_MASK             0x0400  /* EPD_OUTP_LN_ENA */
#define WM2200_EPD_OUTP_LN_ENA_SHIFT                10  /* EPD_OUTP_LN_ENA */
#define WM2200_EPD_OUTP_LN_ENA_WIDTH                 1  /* EPD_OUTP_LN_ENA */
#define WM2200_EPD_RMV_SHRT_LN                  0x0200  /* EPD_RMV_SHRT_LN */
#define WM2200_EPD_RMV_SHRT_LN_MASK             0x0200  /* EPD_RMV_SHRT_LN */
#define WM2200_EPD_RMV_SHRT_LN_SHIFT                 9  /* EPD_RMV_SHRT_LN */
#define WM2200_EPD_RMV_SHRT_LN_WIDTH                 1  /* EPD_RMV_SHRT_LN */

/*
 * R528 (0x210) - Ear Piece Ctrl 2
 */
#define WM2200_EPD_RP_ENA                       0x4000  /* EPD_RP_ENA */
#define WM2200_EPD_RP_ENA_MASK                  0x4000  /* EPD_RP_ENA */
#define WM2200_EPD_RP_ENA_SHIFT                     14  /* EPD_RP_ENA */
#define WM2200_EPD_RP_ENA_WIDTH                      1  /* EPD_RP_ENA */
#define WM2200_EPD_OUTP_RP_ENA                  0x2000  /* EPD_OUTP_RP_ENA */
#define WM2200_EPD_OUTP_RP_ENA_MASK             0x2000  /* EPD_OUTP_RP_ENA */
#define WM2200_EPD_OUTP_RP_ENA_SHIFT                13  /* EPD_OUTP_RP_ENA */
#define WM2200_EPD_OUTP_RP_ENA_WIDTH                 1  /* EPD_OUTP_RP_ENA */
#define WM2200_EPD_RMV_SHRT_RP                  0x1000  /* EPD_RMV_SHRT_RP */
#define WM2200_EPD_RMV_SHRT_RP_MASK             0x1000  /* EPD_RMV_SHRT_RP */
#define WM2200_EPD_RMV_SHRT_RP_SHIFT                12  /* EPD_RMV_SHRT_RP */
#define WM2200_EPD_RMV_SHRT_RP_WIDTH                 1  /* EPD_RMV_SHRT_RP */
#define WM2200_EPD_RN_ENA                       0x0800  /* EPD_RN_ENA */
#define WM2200_EPD_RN_ENA_MASK                  0x0800  /* EPD_RN_ENA */
#define WM2200_EPD_RN_ENA_SHIFT                     11  /* EPD_RN_ENA */
#define WM2200_EPD_RN_ENA_WIDTH                      1  /* EPD_RN_ENA */
#define WM2200_EPD_OUTP_RN_ENA                  0x0400  /* EPD_OUTP_RN_ENA */
#define WM2200_EPD_OUTP_RN_ENA_MASK             0x0400  /* EPD_OUTP_RN_ENA */
#define WM2200_EPD_OUTP_RN_ENA_SHIFT                10  /* EPD_OUTP_RN_ENA */
#define WM2200_EPD_OUTP_RN_ENA_WIDTH                 1  /* EPD_OUTP_RN_ENA */
#define WM2200_EPD_RMV_SHRT_RN                  0x0200  /* EPD_RMV_SHRT_RN */
#define WM2200_EPD_RMV_SHRT_RN_MASK             0x0200  /* EPD_RMV_SHRT_RN */
#define WM2200_EPD_RMV_SHRT_RN_SHIFT                 9  /* EPD_RMV_SHRT_RN */
#define WM2200_EPD_RMV_SHRT_RN_WIDTH                 1  /* EPD_RMV_SHRT_RN */

/*
 * R769 (0x301) - Input Enables
 */
#define WM2200_IN3L_ENA                         0x0020  /* IN3L_ENA */
#define WM2200_IN3L_ENA_MASK                    0x0020  /* IN3L_ENA */
#define WM2200_IN3L_ENA_SHIFT                        5  /* IN3L_ENA */
#define WM2200_IN3L_ENA_WIDTH                        1  /* IN3L_ENA */
#define WM2200_IN3R_ENA                         0x0010  /* IN3R_ENA */
#define WM2200_IN3R_ENA_MASK                    0x0010  /* IN3R_ENA */
#define WM2200_IN3R_ENA_SHIFT                        4  /* IN3R_ENA */
#define WM2200_IN3R_ENA_WIDTH                        1  /* IN3R_ENA */
#define WM2200_IN2L_ENA                         0x0008  /* IN2L_ENA */
#define WM2200_IN2L_ENA_MASK                    0x0008  /* IN2L_ENA */
#define WM2200_IN2L_ENA_SHIFT                        3  /* IN2L_ENA */
#define WM2200_IN2L_ENA_WIDTH                        1  /* IN2L_ENA */
#define WM2200_IN2R_ENA                         0x0004  /* IN2R_ENA */
#define WM2200_IN2R_ENA_MASK                    0x0004  /* IN2R_ENA */
#define WM2200_IN2R_ENA_SHIFT                        2  /* IN2R_ENA */
#define WM2200_IN2R_ENA_WIDTH                        1  /* IN2R_ENA */
#define WM2200_IN1L_ENA                         0x0002  /* IN1L_ENA */
#define WM2200_IN1L_ENA_MASK                    0x0002  /* IN1L_ENA */
#define WM2200_IN1L_ENA_SHIFT                        1  /* IN1L_ENA */
#define WM2200_IN1L_ENA_WIDTH                        1  /* IN1L_ENA */
#define WM2200_IN1R_ENA                         0x0001  /* IN1R_ENA */
#define WM2200_IN1R_ENA_MASK                    0x0001  /* IN1R_ENA */
#define WM2200_IN1R_ENA_SHIFT                        0  /* IN1R_ENA */
#define WM2200_IN1R_ENA_WIDTH                        1  /* IN1R_ENA */

/*
 * R770 (0x302) - IN1L Control
 */
#define WM2200_IN1_OSR                          0x2000  /* IN1_OSR */
#define WM2200_IN1_OSR_MASK                     0x2000  /* IN1_OSR */
#define WM2200_IN1_OSR_SHIFT                        13  /* IN1_OSR */
#define WM2200_IN1_OSR_WIDTH                         1  /* IN1_OSR */
#define WM2200_IN1_DMIC_SUP_MASK                0x1800  /* IN1_DMIC_SUP - [12:11] */
#define WM2200_IN1_DMIC_SUP_SHIFT                   11  /* IN1_DMIC_SUP - [12:11] */
#define WM2200_IN1_DMIC_SUP_WIDTH                    2  /* IN1_DMIC_SUP - [12:11] */
#define WM2200_IN1_MODE_MASK                    0x0600  /* IN1_MODE - [10:9] */
#define WM2200_IN1_MODE_SHIFT                        9  /* IN1_MODE - [10:9] */
#define WM2200_IN1_MODE_WIDTH                        2  /* IN1_MODE - [10:9] */
#define WM2200_IN1L_PGA_VOL_MASK                0x00FE  /* IN1L_PGA_VOL - [7:1] */
#define WM2200_IN1L_PGA_VOL_SHIFT                    1  /* IN1L_PGA_VOL - [7:1] */
#define WM2200_IN1L_PGA_VOL_WIDTH                    7  /* IN1L_PGA_VOL - [7:1] */

/*
 * R771 (0x303) - IN1R Control
 */
#define WM2200_IN1R_PGA_VOL_MASK                0x00FE  /* IN1R_PGA_VOL - [7:1] */
#define WM2200_IN1R_PGA_VOL_SHIFT                    1  /* IN1R_PGA_VOL - [7:1] */
#define WM2200_IN1R_PGA_VOL_WIDTH                    7  /* IN1R_PGA_VOL - [7:1] */

/*
 * R772 (0x304) - IN2L Control
 */
#define WM2200_IN2_OSR                          0x2000  /* IN2_OSR */
#define WM2200_IN2_OSR_MASK                     0x2000  /* IN2_OSR */
#define WM2200_IN2_OSR_SHIFT                        13  /* IN2_OSR */
#define WM2200_IN2_OSR_WIDTH                         1  /* IN2_OSR */
#define WM2200_IN2_DMIC_SUP_MASK                0x1800  /* IN2_DMIC_SUP - [12:11] */
#define WM2200_IN2_DMIC_SUP_SHIFT                   11  /* IN2_DMIC_SUP - [12:11] */
#define WM2200_IN2_DMIC_SUP_WIDTH                    2  /* IN2_DMIC_SUP - [12:11] */
#define WM2200_IN2_MODE_MASK                    0x0600  /* IN2_MODE - [10:9] */
#define WM2200_IN2_MODE_SHIFT                        9  /* IN2_MODE - [10:9] */
#define WM2200_IN2_MODE_WIDTH                        2  /* IN2_MODE - [10:9] */
#define WM2200_IN2L_PGA_VOL_MASK                0x00FE  /* IN2L_PGA_VOL - [7:1] */
#define WM2200_IN2L_PGA_VOL_SHIFT                    1  /* IN2L_PGA_VOL - [7:1] */
#define WM2200_IN2L_PGA_VOL_WIDTH                    7  /* IN2L_PGA_VOL - [7:1] */

/*
 * R773 (0x305) - IN2R Control
 */
#define WM2200_IN2R_PGA_VOL_MASK                0x00FE  /* IN2R_PGA_VOL - [7:1] */
#define WM2200_IN2R_PGA_VOL_SHIFT                    1  /* IN2R_PGA_VOL - [7:1] */
#define WM2200_IN2R_PGA_VOL_WIDTH                    7  /* IN2R_PGA_VOL - [7:1] */

/*
 * R774 (0x306) - IN3L Control
 */
#define WM2200_IN3_OSR                          0x2000  /* IN3_OSR */
#define WM2200_IN3_OSR_MASK                     0x2000  /* IN3_OSR */
#define WM2200_IN3_OSR_SHIFT                        13  /* IN3_OSR */
#define WM2200_IN3_OSR_WIDTH                         1  /* IN3_OSR */
#define WM2200_IN3_DMIC_SUP_MASK                0x1800  /* IN3_DMIC_SUP - [12:11] */
#define WM2200_IN3_DMIC_SUP_SHIFT                   11  /* IN3_DMIC_SUP - [12:11] */
#define WM2200_IN3_DMIC_SUP_WIDTH                    2  /* IN3_DMIC_SUP - [12:11] */
#define WM2200_IN3_MODE_MASK                    0x0600  /* IN3_MODE - [10:9] */
#define WM2200_IN3_MODE_SHIFT                        9  /* IN3_MODE - [10:9] */
#define WM2200_IN3_MODE_WIDTH                        2  /* IN3_MODE - [10:9] */
#define WM2200_IN3L_PGA_VOL_MASK                0x00FE  /* IN3L_PGA_VOL - [7:1] */
#define WM2200_IN3L_PGA_VOL_SHIFT                    1  /* IN3L_PGA_VOL - [7:1] */
#define WM2200_IN3L_PGA_VOL_WIDTH                    7  /* IN3L_PGA_VOL - [7:1] */

/*
 * R775 (0x307) - IN3R Control
 */
#define WM2200_IN3R_PGA_VOL_MASK                0x00FE  /* IN3R_PGA_VOL - [7:1] */
#define WM2200_IN3R_PGA_VOL_SHIFT                    1  /* IN3R_PGA_VOL - [7:1] */
#define WM2200_IN3R_PGA_VOL_WIDTH                    7  /* IN3R_PGA_VOL - [7:1] */

/*
 * R778 (0x30A) - RXANC_SRC
 */
#define WM2200_IN_RXANC_SEL_MASK                0x0007  /* IN_RXANC_SEL - [2:0] */
#define WM2200_IN_RXANC_SEL_SHIFT                    0  /* IN_RXANC_SEL - [2:0] */
#define WM2200_IN_RXANC_SEL_WIDTH                    3  /* IN_RXANC_SEL - [2:0] */

/*
 * R779 (0x30B) - Input Volume Ramp
 */
#define WM2200_IN_VD_RAMP_MASK                  0x0070  /* IN_VD_RAMP - [6:4] */
#define WM2200_IN_VD_RAMP_SHIFT                      4  /* IN_VD_RAMP - [6:4] */
#define WM2200_IN_VD_RAMP_WIDTH                      3  /* IN_VD_RAMP - [6:4] */
#define WM2200_IN_VI_RAMP_MASK                  0x0007  /* IN_VI_RAMP - [2:0] */
#define WM2200_IN_VI_RAMP_SHIFT                      0  /* IN_VI_RAMP - [2:0] */
#define WM2200_IN_VI_RAMP_WIDTH                      3  /* IN_VI_RAMP - [2:0] */

/*
 * R780 (0x30C) - ADC Digital Volume 1L
 */
#define WM2200_IN_VU                            0x0200  /* IN_VU */
#define WM2200_IN_VU_MASK                       0x0200  /* IN_VU */
#define WM2200_IN_VU_SHIFT                           9  /* IN_VU */
#define WM2200_IN_VU_WIDTH                           1  /* IN_VU */
#define WM2200_IN1L_MUTE                        0x0100  /* IN1L_MUTE */
#define WM2200_IN1L_MUTE_MASK                   0x0100  /* IN1L_MUTE */
#define WM2200_IN1L_MUTE_SHIFT                       8  /* IN1L_MUTE */
#define WM2200_IN1L_MUTE_WIDTH                       1  /* IN1L_MUTE */
#define WM2200_IN1L_DIG_VOL_MASK                0x00FF  /* IN1L_DIG_VOL - [7:0] */
#define WM2200_IN1L_DIG_VOL_SHIFT                    0  /* IN1L_DIG_VOL - [7:0] */
#define WM2200_IN1L_DIG_VOL_WIDTH                    8  /* IN1L_DIG_VOL - [7:0] */

/*
 * R781 (0x30D) - ADC Digital Volume 1R
 */
#define WM2200_IN_VU                            0x0200  /* IN_VU */
#define WM2200_IN_VU_MASK                       0x0200  /* IN_VU */
#define WM2200_IN_VU_SHIFT                           9  /* IN_VU */
#define WM2200_IN_VU_WIDTH                           1  /* IN_VU */
#define WM2200_IN1R_MUTE                        0x0100  /* IN1R_MUTE */
#define WM2200_IN1R_MUTE_MASK                   0x0100  /* IN1R_MUTE */
#define WM2200_IN1R_MUTE_SHIFT                       8  /* IN1R_MUTE */
#define WM2200_IN1R_MUTE_WIDTH                       1  /* IN1R_MUTE */
#define WM2200_IN1R_DIG_VOL_MASK                0x00FF  /* IN1R_DIG_VOL - [7:0] */
#define WM2200_IN1R_DIG_VOL_SHIFT                    0  /* IN1R_DIG_VOL - [7:0] */
#define WM2200_IN1R_DIG_VOL_WIDTH                    8  /* IN1R_DIG_VOL - [7:0] */

/*
 * R782 (0x30E) - ADC Digital Volume 2L
 */
#define WM2200_IN_VU                            0x0200  /* IN_VU */
#define WM2200_IN_VU_MASK                       0x0200  /* IN_VU */
#define WM2200_IN_VU_SHIFT                           9  /* IN_VU */
#define WM2200_IN_VU_WIDTH                           1  /* IN_VU */
#define WM2200_IN2L_MUTE                        0x0100  /* IN2L_MUTE */
#define WM2200_IN2L_MUTE_MASK                   0x0100  /* IN2L_MUTE */
#define WM2200_IN2L_MUTE_SHIFT                       8  /* IN2L_MUTE */
#define WM2200_IN2L_MUTE_WIDTH                       1  /* IN2L_MUTE */
#define WM2200_IN2L_DIG_VOL_MASK                0x00FF  /* IN2L_DIG_VOL - [7:0] */
#define WM2200_IN2L_DIG_VOL_SHIFT                    0  /* IN2L_DIG_VOL - [7:0] */
#define WM2200_IN2L_DIG_VOL_WIDTH                    8  /* IN2L_DIG_VOL - [7:0] */

/*
 * R783 (0x30F) - ADC Digital Volume 2R
 */
#define WM2200_IN_VU                            0x0200  /* IN_VU */
#define WM2200_IN_VU_MASK                       0x0200  /* IN_VU */
#define WM2200_IN_VU_SHIFT                           9  /* IN_VU */
#define WM2200_IN_VU_WIDTH                           1  /* IN_VU */
#define WM2200_IN2R_MUTE                        0x0100  /* IN2R_MUTE */
#define WM2200_IN2R_MUTE_MASK                   0x0100  /* IN2R_MUTE */
#define WM2200_IN2R_MUTE_SHIFT                       8  /* IN2R_MUTE */
#define WM2200_IN2R_MUTE_WIDTH                       1  /* IN2R_MUTE */
#define WM2200_IN2R_DIG_VOL_MASK                0x00FF  /* IN2R_DIG_VOL - [7:0] */
#define WM2200_IN2R_DIG_VOL_SHIFT                    0  /* IN2R_DIG_VOL - [7:0] */
#define WM2200_IN2R_DIG_VOL_WIDTH                    8  /* IN2R_DIG_VOL - [7:0] */

/*
 * R784 (0x310) - ADC Digital Volume 3L
 */
#define WM2200_IN_VU                            0x0200  /* IN_VU */
#define WM2200_IN_VU_MASK                       0x0200  /* IN_VU */
#define WM2200_IN_VU_SHIFT                           9  /* IN_VU */
#define WM2200_IN_VU_WIDTH                           1  /* IN_VU */
#define WM2200_IN3L_MUTE                        0x0100  /* IN3L_MUTE */
#define WM2200_IN3L_MUTE_MASK                   0x0100  /* IN3L_MUTE */
#define WM2200_IN3L_MUTE_SHIFT                       8  /* IN3L_MUTE */
#define WM2200_IN3L_MUTE_WIDTH                       1  /* IN3L_MUTE */
#define WM2200_IN3L_DIG_VOL_MASK                0x00FF  /* IN3L_DIG_VOL - [7:0] */
#define WM2200_IN3L_DIG_VOL_SHIFT                    0  /* IN3L_DIG_VOL - [7:0] */
#define WM2200_IN3L_DIG_VOL_WIDTH                    8  /* IN3L_DIG_VOL - [7:0] */

/*
 * R785 (0x311) - ADC Digital Volume 3R
 */
#define WM2200_IN_VU                            0x0200  /* IN_VU */
#define WM2200_IN_VU_MASK                       0x0200  /* IN_VU */
#define WM2200_IN_VU_SHIFT                           9  /* IN_VU */
#define WM2200_IN_VU_WIDTH                           1  /* IN_VU */
#define WM2200_IN3R_MUTE                        0x0100  /* IN3R_MUTE */
#define WM2200_IN3R_MUTE_MASK                   0x0100  /* IN3R_MUTE */
#define WM2200_IN3R_MUTE_SHIFT                       8  /* IN3R_MUTE */
#define WM2200_IN3R_MUTE_WIDTH                       1  /* IN3R_MUTE */
#define WM2200_IN3R_DIG_VOL_MASK                0x00FF  /* IN3R_DIG_VOL - [7:0] */
#define WM2200_IN3R_DIG_VOL_SHIFT                    0  /* IN3R_DIG_VOL - [7:0] */
#define WM2200_IN3R_DIG_VOL_WIDTH                    8  /* IN3R_DIG_VOL - [7:0] */

/*
 * R1024 (0x400) - Output Enables
 */
#define WM2200_OUT2L_ENA                        0x0008  /* OUT2L_ENA */
#define WM2200_OUT2L_ENA_MASK                   0x0008  /* OUT2L_ENA */
#define WM2200_OUT2L_ENA_SHIFT                       3  /* OUT2L_ENA */
#define WM2200_OUT2L_ENA_WIDTH                       1  /* OUT2L_ENA */
#define WM2200_OUT2R_ENA                        0x0004  /* OUT2R_ENA */
#define WM2200_OUT2R_ENA_MASK                   0x0004  /* OUT2R_ENA */
#define WM2200_OUT2R_ENA_SHIFT                       2  /* OUT2R_ENA */
#define WM2200_OUT2R_ENA_WIDTH                       1  /* OUT2R_ENA */
#define WM2200_OUT1L_ENA                        0x0002  /* OUT1L_ENA */
#define WM2200_OUT1L_ENA_MASK                   0x0002  /* OUT1L_ENA */
#define WM2200_OUT1L_ENA_SHIFT                       1  /* OUT1L_ENA */
#define WM2200_OUT1L_ENA_WIDTH                       1  /* OUT1L_ENA */
#define WM2200_OUT1R_ENA                        0x0001  /* OUT1R_ENA */
#define WM2200_OUT1R_ENA_MASK                   0x0001  /* OUT1R_ENA */
#define WM2200_OUT1R_ENA_SHIFT                       0  /* OUT1R_ENA */
#define WM2200_OUT1R_ENA_WIDTH                       1  /* OUT1R_ENA */

/*
 * R1025 (0x401) - DAC Volume Limit 1L
 */
#define WM2200_OUT1_OSR                         0x2000  /* OUT1_OSR */
#define WM2200_OUT1_OSR_MASK                    0x2000  /* OUT1_OSR */
#define WM2200_OUT1_OSR_SHIFT                       13  /* OUT1_OSR */
#define WM2200_OUT1_OSR_WIDTH                        1  /* OUT1_OSR */
#define WM2200_OUT1L_ANC_SRC                    0x0800  /* OUT1L_ANC_SRC */
#define WM2200_OUT1L_ANC_SRC_MASK               0x0800  /* OUT1L_ANC_SRC */
#define WM2200_OUT1L_ANC_SRC_SHIFT                  11  /* OUT1L_ANC_SRC */
#define WM2200_OUT1L_ANC_SRC_WIDTH                   1  /* OUT1L_ANC_SRC */
#define WM2200_OUT1L_PGA_VOL_MASK               0x00FE  /* OUT1L_PGA_VOL - [7:1] */
#define WM2200_OUT1L_PGA_VOL_SHIFT                   1  /* OUT1L_PGA_VOL - [7:1] */
#define WM2200_OUT1L_PGA_VOL_WIDTH                   7  /* OUT1L_PGA_VOL - [7:1] */

/*
 * R1026 (0x402) - DAC Volume Limit 1R
 */
#define WM2200_OUT1R_ANC_SRC                    0x0800  /* OUT1R_ANC_SRC */
#define WM2200_OUT1R_ANC_SRC_MASK               0x0800  /* OUT1R_ANC_SRC */
#define WM2200_OUT1R_ANC_SRC_SHIFT                  11  /* OUT1R_ANC_SRC */
#define WM2200_OUT1R_ANC_SRC_WIDTH                   1  /* OUT1R_ANC_SRC */
#define WM2200_OUT1R_PGA_VOL_MASK               0x00FE  /* OUT1R_PGA_VOL - [7:1] */
#define WM2200_OUT1R_PGA_VOL_SHIFT                   1  /* OUT1R_PGA_VOL - [7:1] */
#define WM2200_OUT1R_PGA_VOL_WIDTH                   7  /* OUT1R_PGA_VOL - [7:1] */

/*
 * R1027 (0x403) - DAC Volume Limit 2L
 */
#define WM2200_OUT2_OSR                         0x2000  /* OUT2_OSR */
#define WM2200_OUT2_OSR_MASK                    0x2000  /* OUT2_OSR */
#define WM2200_OUT2_OSR_SHIFT                       13  /* OUT2_OSR */
#define WM2200_OUT2_OSR_WIDTH                        1  /* OUT2_OSR */
#define WM2200_OUT2L_ANC_SRC                    0x0800  /* OUT2L_ANC_SRC */
#define WM2200_OUT2L_ANC_SRC_MASK               0x0800  /* OUT2L_ANC_SRC */
#define WM2200_OUT2L_ANC_SRC_SHIFT                  11  /* OUT2L_ANC_SRC */
#define WM2200_OUT2L_ANC_SRC_WIDTH                   1  /* OUT2L_ANC_SRC */

/*
 * R1028 (0x404) - DAC Volume Limit 2R
 */
#define WM2200_OUT2R_ANC_SRC                    0x0800  /* OUT2R_ANC_SRC */
#define WM2200_OUT2R_ANC_SRC_MASK               0x0800  /* OUT2R_ANC_SRC */
#define WM2200_OUT2R_ANC_SRC_SHIFT                  11  /* OUT2R_ANC_SRC */
#define WM2200_OUT2R_ANC_SRC_WIDTH                   1  /* OUT2R_ANC_SRC */

/*
 * R1033 (0x409) - DAC AEC Control 1
 */
#define WM2200_AEC_LOOPBACK_ENA                 0x0004  /* AEC_LOOPBACK_ENA */
#define WM2200_AEC_LOOPBACK_ENA_MASK            0x0004  /* AEC_LOOPBACK_ENA */
#define WM2200_AEC_LOOPBACK_ENA_SHIFT                2  /* AEC_LOOPBACK_ENA */
#define WM2200_AEC_LOOPBACK_ENA_WIDTH                1  /* AEC_LOOPBACK_ENA */
#define WM2200_AEC_LOOPBACK_SRC_MASK            0x0003  /* AEC_LOOPBACK_SRC - [1:0] */
#define WM2200_AEC_LOOPBACK_SRC_SHIFT                0  /* AEC_LOOPBACK_SRC - [1:0] */
#define WM2200_AEC_LOOPBACK_SRC_WIDTH                2  /* AEC_LOOPBACK_SRC - [1:0] */

/*
 * R1034 (0x40A) - Output Volume Ramp
 */
#define WM2200_OUT_VD_RAMP_MASK                 0x0070  /* OUT_VD_RAMP - [6:4] */
#define WM2200_OUT_VD_RAMP_SHIFT                     4  /* OUT_VD_RAMP - [6:4] */
#define WM2200_OUT_VD_RAMP_WIDTH                     3  /* OUT_VD_RAMP - [6:4] */
#define WM2200_OUT_VI_RAMP_MASK                 0x0007  /* OUT_VI_RAMP - [2:0] */
#define WM2200_OUT_VI_RAMP_SHIFT                     0  /* OUT_VI_RAMP - [2:0] */
#define WM2200_OUT_VI_RAMP_WIDTH                     3  /* OUT_VI_RAMP - [2:0] */

/*
 * R1035 (0x40B) - DAC Digital Volume 1L
 */
#define WM2200_OUT_VU                           0x0200  /* OUT_VU */
#define WM2200_OUT_VU_MASK                      0x0200  /* OUT_VU */
#define WM2200_OUT_VU_SHIFT                          9  /* OUT_VU */
#define WM2200_OUT_VU_WIDTH                          1  /* OUT_VU */
#define WM2200_OUT1L_MUTE                       0x0100  /* OUT1L_MUTE */
#define WM2200_OUT1L_MUTE_MASK                  0x0100  /* OUT1L_MUTE */
#define WM2200_OUT1L_MUTE_SHIFT                      8  /* OUT1L_MUTE */
#define WM2200_OUT1L_MUTE_WIDTH                      1  /* OUT1L_MUTE */
#define WM2200_OUT1L_VOL_MASK                   0x00FF  /* OUT1L_VOL - [7:0] */
#define WM2200_OUT1L_VOL_SHIFT                       0  /* OUT1L_VOL - [7:0] */
#define WM2200_OUT1L_VOL_WIDTH                       8  /* OUT1L_VOL - [7:0] */

/*
 * R1036 (0x40C) - DAC Digital Volume 1R
 */
#define WM2200_OUT_VU                           0x0200  /* OUT_VU */
#define WM2200_OUT_VU_MASK                      0x0200  /* OUT_VU */
#define WM2200_OUT_VU_SHIFT                          9  /* OUT_VU */
#define WM2200_OUT_VU_WIDTH                          1  /* OUT_VU */
#define WM2200_OUT1R_MUTE                       0x0100  /* OUT1R_MUTE */
#define WM2200_OUT1R_MUTE_MASK                  0x0100  /* OUT1R_MUTE */
#define WM2200_OUT1R_MUTE_SHIFT                      8  /* OUT1R_MUTE */
#define WM2200_OUT1R_MUTE_WIDTH                      1  /* OUT1R_MUTE */
#define WM2200_OUT1R_VOL_MASK                   0x00FF  /* OUT1R_VOL - [7:0] */
#define WM2200_OUT1R_VOL_SHIFT                       0  /* OUT1R_VOL - [7:0] */
#define WM2200_OUT1R_VOL_WIDTH                       8  /* OUT1R_VOL - [7:0] */

/*
 * R1037 (0x40D) - DAC Digital Volume 2L
 */
#define WM2200_OUT_VU                           0x0200  /* OUT_VU */
#define WM2200_OUT_VU_MASK                      0x0200  /* OUT_VU */
#define WM2200_OUT_VU_SHIFT                          9  /* OUT_VU */
#define WM2200_OUT_VU_WIDTH                          1  /* OUT_VU */
#define WM2200_OUT2L_MUTE                       0x0100  /* OUT2L_MUTE */
#define WM2200_OUT2L_MUTE_MASK                  0x0100  /* OUT2L_MUTE */
#define WM2200_OUT2L_MUTE_SHIFT                      8  /* OUT2L_MUTE */
#define WM2200_OUT2L_MUTE_WIDTH                      1  /* OUT2L_MUTE */
#define WM2200_OUT2L_VOL_MASK                   0x00FF  /* OUT2L_VOL - [7:0] */
#define WM2200_OUT2L_VOL_SHIFT                       0  /* OUT2L_VOL - [7:0] */
#define WM2200_OUT2L_VOL_WIDTH                       8  /* OUT2L_VOL - [7:0] */

/*
 * R1038 (0x40E) - DAC Digital Volume 2R
 */
#define WM2200_OUT_VU                           0x0200  /* OUT_VU */
#define WM2200_OUT_VU_MASK                      0x0200  /* OUT_VU */
#define WM2200_OUT_VU_SHIFT                          9  /* OUT_VU */
#define WM2200_OUT_VU_WIDTH                          1  /* OUT_VU */
#define WM2200_OUT2R_MUTE                       0x0100  /* OUT2R_MUTE */
#define WM2200_OUT2R_MUTE_MASK                  0x0100  /* OUT2R_MUTE */
#define WM2200_OUT2R_MUTE_SHIFT                      8  /* OUT2R_MUTE */
#define WM2200_OUT2R_MUTE_WIDTH                      1  /* OUT2R_MUTE */
#define WM2200_OUT2R_VOL_MASK                   0x00FF  /* OUT2R_VOL - [7:0] */
#define WM2200_OUT2R_VOL_SHIFT                       0  /* OUT2R_VOL - [7:0] */
#define WM2200_OUT2R_VOL_WIDTH                       8  /* OUT2R_VOL - [7:0] */

/*
 * R1047 (0x417) - PDM 1
 */
#define WM2200_SPK1R_MUTE                       0x2000  /* SPK1R_MUTE */
#define WM2200_SPK1R_MUTE_MASK                  0x2000  /* SPK1R_MUTE */
#define WM2200_SPK1R_MUTE_SHIFT                     13  /* SPK1R_MUTE */
#define WM2200_SPK1R_MUTE_WIDTH                      1  /* SPK1R_MUTE */
#define WM2200_SPK1L_MUTE                       0x1000  /* SPK1L_MUTE */
#define WM2200_SPK1L_MUTE_MASK                  0x1000  /* SPK1L_MUTE */
#define WM2200_SPK1L_MUTE_SHIFT                     12  /* SPK1L_MUTE */
#define WM2200_SPK1L_MUTE_WIDTH                      1  /* SPK1L_MUTE */
#define WM2200_SPK1_MUTE_ENDIAN                 0x0100  /* SPK1_MUTE_ENDIAN */
#define WM2200_SPK1_MUTE_ENDIAN_MASK            0x0100  /* SPK1_MUTE_ENDIAN */
#define WM2200_SPK1_MUTE_ENDIAN_SHIFT                8  /* SPK1_MUTE_ENDIAN */
#define WM2200_SPK1_MUTE_ENDIAN_WIDTH                1  /* SPK1_MUTE_ENDIAN */
#define WM2200_SPK1_MUTE_SEQL_MASK              0x00FF  /* SPK1_MUTE_SEQL - [7:0] */
#define WM2200_SPK1_MUTE_SEQL_SHIFT                  0  /* SPK1_MUTE_SEQL - [7:0] */
#define WM2200_SPK1_MUTE_SEQL_WIDTH                  8  /* SPK1_MUTE_SEQL - [7:0] */

/*
 * R1048 (0x418) - PDM 2
 */
#define WM2200_SPK1_FMT                         0x0001  /* SPK1_FMT */
#define WM2200_SPK1_FMT_MASK                    0x0001  /* SPK1_FMT */
#define WM2200_SPK1_FMT_SHIFT                        0  /* SPK1_FMT */
#define WM2200_SPK1_FMT_WIDTH                        1  /* SPK1_FMT */

/*
 * R1280 (0x500) - Audio IF 1_1
 */
#define WM2200_AIF1_BCLK_INV                    0x0040  /* AIF1_BCLK_INV */
#define WM2200_AIF1_BCLK_INV_MASK               0x0040  /* AIF1_BCLK_INV */
#define WM2200_AIF1_BCLK_INV_SHIFT                   6  /* AIF1_BCLK_INV */
#define WM2200_AIF1_BCLK_INV_WIDTH                   1  /* AIF1_BCLK_INV */
#define WM2200_AIF1_BCLK_FRC                    0x0020  /* AIF1_BCLK_FRC */
#define WM2200_AIF1_BCLK_FRC_MASK               0x0020  /* AIF1_BCLK_FRC */
#define WM2200_AIF1_BCLK_FRC_SHIFT                   5  /* AIF1_BCLK_FRC */
#define WM2200_AIF1_BCLK_FRC_WIDTH                   1  /* AIF1_BCLK_FRC */
#define WM2200_AIF1_BCLK_MSTR                   0x0010  /* AIF1_BCLK_MSTR */
#define WM2200_AIF1_BCLK_MSTR_MASK              0x0010  /* AIF1_BCLK_MSTR */
#define WM2200_AIF1_BCLK_MSTR_SHIFT                  4  /* AIF1_BCLK_MSTR */
#define WM2200_AIF1_BCLK_MSTR_WIDTH                  1  /* AIF1_BCLK_MSTR */
#define WM2200_AIF1_BCLK_DIV_MASK               0x000F  /* AIF1_BCLK_DIV - [3:0] */
#define WM2200_AIF1_BCLK_DIV_SHIFT                   0  /* AIF1_BCLK_DIV - [3:0] */
#define WM2200_AIF1_BCLK_DIV_WIDTH                   4  /* AIF1_BCLK_DIV - [3:0] */

/*
 * R1281 (0x501) - Audio IF 1_2
 */
#define WM2200_AIF1TX_DAT_TRI                   0x0020  /* AIF1TX_DAT_TRI */
#define WM2200_AIF1TX_DAT_TRI_MASK              0x0020  /* AIF1TX_DAT_TRI */
#define WM2200_AIF1TX_DAT_TRI_SHIFT                  5  /* AIF1TX_DAT_TRI */
#define WM2200_AIF1TX_DAT_TRI_WIDTH                  1  /* AIF1TX_DAT_TRI */
#define WM2200_AIF1TX_LRCLK_SRC                 0x0008  /* AIF1TX_LRCLK_SRC */
#define WM2200_AIF1TX_LRCLK_SRC_MASK            0x0008  /* AIF1TX_LRCLK_SRC */
#define WM2200_AIF1TX_LRCLK_SRC_SHIFT                3  /* AIF1TX_LRCLK_SRC */
#define WM2200_AIF1TX_LRCLK_SRC_WIDTH                1  /* AIF1TX_LRCLK_SRC */
#define WM2200_AIF1TX_LRCLK_INV                 0x0004  /* AIF1TX_LRCLK_INV */
#define WM2200_AIF1TX_LRCLK_INV_MASK            0x0004  /* AIF1TX_LRCLK_INV */
#define WM2200_AIF1TX_LRCLK_INV_SHIFT                2  /* AIF1TX_LRCLK_INV */
#define WM2200_AIF1TX_LRCLK_INV_WIDTH                1  /* AIF1TX_LRCLK_INV */
#define WM2200_AIF1TX_LRCLK_FRC                 0x0002  /* AIF1TX_LRCLK_FRC */
#define WM2200_AIF1TX_LRCLK_FRC_MASK            0x0002  /* AIF1TX_LRCLK_FRC */
#define WM2200_AIF1TX_LRCLK_FRC_SHIFT                1  /* AIF1TX_LRCLK_FRC */
#define WM2200_AIF1TX_LRCLK_FRC_WIDTH                1  /* AIF1TX_LRCLK_FRC */
#define WM2200_AIF1TX_LRCLK_MSTR                0x0001  /* AIF1TX_LRCLK_MSTR */
#define WM2200_AIF1TX_LRCLK_MSTR_MASK           0x0001  /* AIF1TX_LRCLK_MSTR */
#define WM2200_AIF1TX_LRCLK_MSTR_SHIFT               0  /* AIF1TX_LRCLK_MSTR */
#define WM2200_AIF1TX_LRCLK_MSTR_WIDTH               1  /* AIF1TX_LRCLK_MSTR */

/*
 * R1282 (0x502) - Audio IF 1_3
 */
#define WM2200_AIF1RX_LRCLK_INV                 0x0004  /* AIF1RX_LRCLK_INV */
#define WM2200_AIF1RX_LRCLK_INV_MASK            0x0004  /* AIF1RX_LRCLK_INV */
#define WM2200_AIF1RX_LRCLK_INV_SHIFT                2  /* AIF1RX_LRCLK_INV */
#define WM2200_AIF1RX_LRCLK_INV_WIDTH                1  /* AIF1RX_LRCLK_INV */
#define WM2200_AIF1RX_LRCLK_FRC                 0x0002  /* AIF1RX_LRCLK_FRC */
#define WM2200_AIF1RX_LRCLK_FRC_MASK            0x0002  /* AIF1RX_LRCLK_FRC */
#define WM2200_AIF1RX_LRCLK_FRC_SHIFT                1  /* AIF1RX_LRCLK_FRC */
#define WM2200_AIF1RX_LRCLK_FRC_WIDTH                1  /* AIF1RX_LRCLK_FRC */
#define WM2200_AIF1RX_LRCLK_MSTR                0x0001  /* AIF1RX_LRCLK_MSTR */
#define WM2200_AIF1RX_LRCLK_MSTR_MASK           0x0001  /* AIF1RX_LRCLK_MSTR */
#define WM2200_AIF1RX_LRCLK_MSTR_SHIFT               0  /* AIF1RX_LRCLK_MSTR */
#define WM2200_AIF1RX_LRCLK_MSTR_WIDTH               1  /* AIF1RX_LRCLK_MSTR */

/*
 * R1283 (0x503) - Audio IF 1_4
 */
#define WM2200_AIF1_TRI                         0x0040  /* AIF1_TRI */
#define WM2200_AIF1_TRI_MASK                    0x0040  /* AIF1_TRI */
#define WM2200_AIF1_TRI_SHIFT                        6  /* AIF1_TRI */
#define WM2200_AIF1_TRI_WIDTH                        1  /* AIF1_TRI */

/*
 * R1284 (0x504) - Audio IF 1_5
 */
#define WM2200_AIF1_FMT_MASK                    0x0007  /* AIF1_FMT - [2:0] */
#define WM2200_AIF1_FMT_SHIFT                        0  /* AIF1_FMT - [2:0] */
#define WM2200_AIF1_FMT_WIDTH                        3  /* AIF1_FMT - [2:0] */

/*
 * R1285 (0x505) - Audio IF 1_6
 */
#define WM2200_AIF1TX_BCPF_MASK                 0x07FF  /* AIF1TX_BCPF - [10:0] */
#define WM2200_AIF1TX_BCPF_SHIFT                     0  /* AIF1TX_BCPF - [10:0] */
#define WM2200_AIF1TX_BCPF_WIDTH                    11  /* AIF1TX_BCPF - [10:0] */

/*
 * R1286 (0x506) - Audio IF 1_7
 */
#define WM2200_AIF1RX_BCPF_MASK                 0x07FF  /* AIF1RX_BCPF - [10:0] */
#define WM2200_AIF1RX_BCPF_SHIFT                     0  /* AIF1RX_BCPF - [10:0] */
#define WM2200_AIF1RX_BCPF_WIDTH                    11  /* AIF1RX_BCPF - [10:0] */

/*
 * R1287 (0x507) - Audio IF 1_8
 */
#define WM2200_AIF1TX_WL_MASK                   0x3F00  /* AIF1TX_WL - [13:8] */
#define WM2200_AIF1TX_WL_SHIFT                       8  /* AIF1TX_WL - [13:8] */
#define WM2200_AIF1TX_WL_WIDTH                       6  /* AIF1TX_WL - [13:8] */
#define WM2200_AIF1TX_SLOT_LEN_MASK             0x00FF  /* AIF1TX_SLOT_LEN - [7:0] */
#define WM2200_AIF1TX_SLOT_LEN_SHIFT                 0  /* AIF1TX_SLOT_LEN - [7:0] */
#define WM2200_AIF1TX_SLOT_LEN_WIDTH                 8  /* AIF1TX_SLOT_LEN - [7:0] */

/*
 * R1288 (0x508) - Audio IF 1_9
 */
#define WM2200_AIF1RX_WL_MASK                   0x3F00  /* AIF1RX_WL - [13:8] */
#define WM2200_AIF1RX_WL_SHIFT                       8  /* AIF1RX_WL - [13:8] */
#define WM2200_AIF1RX_WL_WIDTH                       6  /* AIF1RX_WL - [13:8] */
#define WM2200_AIF1RX_SLOT_LEN_MASK             0x00FF  /* AIF1RX_SLOT_LEN - [7:0] */
#define WM2200_AIF1RX_SLOT_LEN_SHIFT                 0  /* AIF1RX_SLOT_LEN - [7:0] */
#define WM2200_AIF1RX_SLOT_LEN_WIDTH                 8  /* AIF1RX_SLOT_LEN - [7:0] */

/*
 * R1289 (0x509) - Audio IF 1_10
 */
#define WM2200_AIF1TX1_SLOT_MASK                0x003F  /* AIF1TX1_SLOT - [5:0] */
#define WM2200_AIF1TX1_SLOT_SHIFT                    0  /* AIF1TX1_SLOT - [5:0] */
#define WM2200_AIF1TX1_SLOT_WIDTH                    6  /* AIF1TX1_SLOT - [5:0] */

/*
 * R1290 (0x50A) - Audio IF 1_11
 */
#define WM2200_AIF1TX2_SLOT_MASK                0x003F  /* AIF1TX2_SLOT - [5:0] */
#define WM2200_AIF1TX2_SLOT_SHIFT                    0  /* AIF1TX2_SLOT - [5:0] */
#define WM2200_AIF1TX2_SLOT_WIDTH                    6  /* AIF1TX2_SLOT - [5:0] */

/*
 * R1291 (0x50B) - Audio IF 1_12
 */
#define WM2200_AIF1TX3_SLOT_MASK                0x003F  /* AIF1TX3_SLOT - [5:0] */
#define WM2200_AIF1TX3_SLOT_SHIFT                    0  /* AIF1TX3_SLOT - [5:0] */
#define WM2200_AIF1TX3_SLOT_WIDTH                    6  /* AIF1TX3_SLOT - [5:0] */

/*
 * R1292 (0x50C) - Audio IF 1_13
 */
#define WM2200_AIF1TX4_SLOT_MASK                0x003F  /* AIF1TX4_SLOT - [5:0] */
#define WM2200_AIF1TX4_SLOT_SHIFT                    0  /* AIF1TX4_SLOT - [5:0] */
#define WM2200_AIF1TX4_SLOT_WIDTH                    6  /* AIF1TX4_SLOT - [5:0] */

/*
 * R1293 (0x50D) - Audio IF 1_14
 */
#define WM2200_AIF1TX5_SLOT_MASK                0x003F  /* AIF1TX5_SLOT - [5:0] */
#define WM2200_AIF1TX5_SLOT_SHIFT                    0  /* AIF1TX5_SLOT - [5:0] */
#define WM2200_AIF1TX5_SLOT_WIDTH                    6  /* AIF1TX5_SLOT - [5:0] */

/*
 * R1294 (0x50E) - Audio IF 1_15
 */
#define WM2200_AIF1TX6_SLOT_MASK                0x003F  /* AIF1TX6_SLOT - [5:0] */
#define WM2200_AIF1TX6_SLOT_SHIFT                    0  /* AIF1TX6_SLOT - [5:0] */
#define WM2200_AIF1TX6_SLOT_WIDTH                    6  /* AIF1TX6_SLOT - [5:0] */

/*
 * R1295 (0x50F) - Audio IF 1_16
 */
#define WM2200_AIF1RX1_SLOT_MASK                0x003F  /* AIF1RX1_SLOT - [5:0] */
#define WM2200_AIF1RX1_SLOT_SHIFT                    0  /* AIF1RX1_SLOT - [5:0] */
#define WM2200_AIF1RX1_SLOT_WIDTH                    6  /* AIF1RX1_SLOT - [5:0] */

/*
 * R1296 (0x510) - Audio IF 1_17
 */
#define WM2200_AIF1RX2_SLOT_MASK                0x003F  /* AIF1RX2_SLOT - [5:0] */
#define WM2200_AIF1RX2_SLOT_SHIFT                    0  /* AIF1RX2_SLOT - [5:0] */
#define WM2200_AIF1RX2_SLOT_WIDTH                    6  /* AIF1RX2_SLOT - [5:0] */

/*
 * R1297 (0x511) - Audio IF 1_18
 */
#define WM2200_AIF1RX3_SLOT_MASK                0x003F  /* AIF1RX3_SLOT - [5:0] */
#define WM2200_AIF1RX3_SLOT_SHIFT                    0  /* AIF1RX3_SLOT - [5:0] */
#define WM2200_AIF1RX3_SLOT_WIDTH                    6  /* AIF1RX3_SLOT - [5:0] */

/*
 * R1298 (0x512) - Audio IF 1_19
 */
#define WM2200_AIF1RX4_SLOT_MASK                0x003F  /* AIF1RX4_SLOT - [5:0] */
#define WM2200_AIF1RX4_SLOT_SHIFT                    0  /* AIF1RX4_SLOT - [5:0] */
#define WM2200_AIF1RX4_SLOT_WIDTH                    6  /* AIF1RX4_SLOT - [5:0] */

/*
 * R1299 (0x513) - Audio IF 1_20
 */
#define WM2200_AIF1RX5_SLOT_MASK                0x003F  /* AIF1RX5_SLOT - [5:0] */
#define WM2200_AIF1RX5_SLOT_SHIFT                    0  /* AIF1RX5_SLOT - [5:0] */
#define WM2200_AIF1RX5_SLOT_WIDTH                    6  /* AIF1RX5_SLOT - [5:0] */

/*
 * R1300 (0x514) - Audio IF 1_21
 */
#define WM2200_AIF1RX6_SLOT_MASK                0x003F  /* AIF1RX6_SLOT - [5:0] */
#define WM2200_AIF1RX6_SLOT_SHIFT                    0  /* AIF1RX6_SLOT - [5:0] */
#define WM2200_AIF1RX6_SLOT_WIDTH                    6  /* AIF1RX6_SLOT - [5:0] */

/*
 * R1301 (0x515) - Audio IF 1_22
 */
#define WM2200_AIF1RX6_ENA                      0x0800  /* AIF1RX6_ENA */
#define WM2200_AIF1RX6_ENA_MASK                 0x0800  /* AIF1RX6_ENA */
#define WM2200_AIF1RX6_ENA_SHIFT                    11  /* AIF1RX6_ENA */
#define WM2200_AIF1RX6_ENA_WIDTH                     1  /* AIF1RX6_ENA */
#define WM2200_AIF1RX5_ENA                      0x0400  /* AIF1RX5_ENA */
#define WM2200_AIF1RX5_ENA_MASK                 0x0400  /* AIF1RX5_ENA */
#define WM2200_AIF1RX5_ENA_SHIFT                    10  /* AIF1RX5_ENA */
#define WM2200_AIF1RX5_ENA_WIDTH                     1  /* AIF1RX5_ENA */
#define WM2200_AIF1RX4_ENA                      0x0200  /* AIF1RX4_ENA */
#define WM2200_AIF1RX4_ENA_MASK                 0x0200  /* AIF1RX4_ENA */
#define WM2200_AIF1RX4_ENA_SHIFT                     9  /* AIF1RX4_ENA */
#define WM2200_AIF1RX4_ENA_WIDTH                     1  /* AIF1RX4_ENA */
#define WM2200_AIF1RX3_ENA                      0x0100  /* AIF1RX3_ENA */
#define WM2200_AIF1RX3_ENA_MASK                 0x0100  /* AIF1RX3_ENA */
#define WM2200_AIF1RX3_ENA_SHIFT                     8  /* AIF1RX3_ENA */
#define WM2200_AIF1RX3_ENA_WIDTH                     1  /* AIF1RX3_ENA */
#define WM2200_AIF1RX2_ENA                      0x0080  /* AIF1RX2_ENA */
#define WM2200_AIF1RX2_ENA_MASK                 0x0080  /* AIF1RX2_ENA */
#define WM2200_AIF1RX2_ENA_SHIFT                     7  /* AIF1RX2_ENA */
#define WM2200_AIF1RX2_ENA_WIDTH                     1  /* AIF1RX2_ENA */
#define WM2200_AIF1RX1_ENA                      0x0040  /* AIF1RX1_ENA */
#define WM2200_AIF1RX1_ENA_MASK                 0x0040  /* AIF1RX1_ENA */
#define WM2200_AIF1RX1_ENA_SHIFT                     6  /* AIF1RX1_ENA */
#define WM2200_AIF1RX1_ENA_WIDTH                     1  /* AIF1RX1_ENA */
#define WM2200_AIF1TX6_ENA                      0x0020  /* AIF1TX6_ENA */
#define WM2200_AIF1TX6_ENA_MASK                 0x0020  /* AIF1TX6_ENA */
#define WM2200_AIF1TX6_ENA_SHIFT                     5  /* AIF1TX6_ENA */
#define WM2200_AIF1TX6_ENA_WIDTH                     1  /* AIF1TX6_ENA */
#define WM2200_AIF1TX5_ENA                      0x0010  /* AIF1TX5_ENA */
#define WM2200_AIF1TX5_ENA_MASK                 0x0010  /* AIF1TX5_ENA */
#define WM2200_AIF1TX5_ENA_SHIFT                     4  /* AIF1TX5_ENA */
#define WM2200_AIF1TX5_ENA_WIDTH                     1  /* AIF1TX5_ENA */
#define WM2200_AIF1TX4_ENA                      0x0008  /* AIF1TX4_ENA */
#define WM2200_AIF1TX4_ENA_MASK                 0x0008  /* AIF1TX4_ENA */
#define WM2200_AIF1TX4_ENA_SHIFT                     3  /* AIF1TX4_ENA */
#define WM2200_AIF1TX4_ENA_WIDTH                     1  /* AIF1TX4_ENA */
#define WM2200_AIF1TX3_ENA                      0x0004  /* AIF1TX3_ENA */
#define WM2200_AIF1TX3_ENA_MASK                 0x0004  /* AIF1TX3_ENA */
#define WM2200_AIF1TX3_ENA_SHIFT                     2  /* AIF1TX3_ENA */
#define WM2200_AIF1TX3_ENA_WIDTH                     1  /* AIF1TX3_ENA */
#define WM2200_AIF1TX2_ENA                      0x0002  /* AIF1TX2_ENA */
#define WM2200_AIF1TX2_ENA_MASK                 0x0002  /* AIF1TX2_ENA */
#define WM2200_AIF1TX2_ENA_SHIFT                     1  /* AIF1TX2_ENA */
#define WM2200_AIF1TX2_ENA_WIDTH                     1  /* AIF1TX2_ENA */
#define WM2200_AIF1TX1_ENA                      0x0001  /* AIF1TX1_ENA */
#define WM2200_AIF1TX1_ENA_MASK                 0x0001  /* AIF1TX1_ENA */
#define WM2200_AIF1TX1_ENA_SHIFT                     0  /* AIF1TX1_ENA */
#define WM2200_AIF1TX1_ENA_WIDTH                     1  /* AIF1TX1_ENA */

/*
 * R1536 (0x600) - OUT1LMIX Input 1 Source
 */
#define WM2200_OUT1LMIX_SRC1_MASK               0x007F  /* OUT1LMIX_SRC1 - [6:0] */
#define WM2200_OUT1LMIX_SRC1_SHIFT                   0  /* OUT1LMIX_SRC1 - [6:0] */
#define WM2200_OUT1LMIX_SRC1_WIDTH                   7  /* OUT1LMIX_SRC1 - [6:0] */

/*
 * R1537 (0x601) - OUT1LMIX Input 1 Volume
 */
#define WM2200_OUT1LMIX_VOL1_MASK               0x00FE  /* OUT1LMIX_VOL1 - [7:1] */
#define WM2200_OUT1LMIX_VOL1_SHIFT                   1  /* OUT1LMIX_VOL1 - [7:1] */
#define WM2200_OUT1LMIX_VOL1_WIDTH                   7  /* OUT1LMIX_VOL1 - [7:1] */

/*
 * R1538 (0x602) - OUT1LMIX Input 2 Source
 */
#define WM2200_OUT1LMIX_SRC2_MASK               0x007F  /* OUT1LMIX_SRC2 - [6:0] */
#define WM2200_OUT1LMIX_SRC2_SHIFT                   0  /* OUT1LMIX_SRC2 - [6:0] */
#define WM2200_OUT1LMIX_SRC2_WIDTH                   7  /* OUT1LMIX_SRC2 - [6:0] */

/*
 * R1539 (0x603) - OUT1LMIX Input 2 Volume
 */
#define WM2200_OUT1LMIX_VOL2_MASK               0x00FE  /* OUT1LMIX_VOL2 - [7:1] */
#define WM2200_OUT1LMIX_VOL2_SHIFT                   1  /* OUT1LMIX_VOL2 - [7:1] */
#define WM2200_OUT1LMIX_VOL2_WIDTH                   7  /* OUT1LMIX_VOL2 - [7:1] */

/*
 * R1540 (0x604) - OUT1LMIX Input 3 Source
 */
#define WM2200_OUT1LMIX_SRC3_MASK               0x007F  /* OUT1LMIX_SRC3 - [6:0] */
#define WM2200_OUT1LMIX_SRC3_SHIFT                   0  /* OUT1LMIX_SRC3 - [6:0] */
#define WM2200_OUT1LMIX_SRC3_WIDTH                   7  /* OUT1LMIX_SRC3 - [6:0] */

/*
 * R1541 (0x605) - OUT1LMIX Input 3 Volume
 */
#define WM2200_OUT1LMIX_VOL3_MASK               0x00FE  /* OUT1LMIX_VOL3 - [7:1] */
#define WM2200_OUT1LMIX_VOL3_SHIFT                   1  /* OUT1LMIX_VOL3 - [7:1] */
#define WM2200_OUT1LMIX_VOL3_WIDTH                   7  /* OUT1LMIX_VOL3 - [7:1] */

/*
 * R1542 (0x606) - OUT1LMIX Input 4 Source
 */
#define WM2200_OUT1LMIX_SRC4_MASK               0x007F  /* OUT1LMIX_SRC4 - [6:0] */
#define WM2200_OUT1LMIX_SRC4_SHIFT                   0  /* OUT1LMIX_SRC4 - [6:0] */
#define WM2200_OUT1LMIX_SRC4_WIDTH                   7  /* OUT1LMIX_SRC4 - [6:0] */

/*
 * R1543 (0x607) - OUT1LMIX Input 4 Volume
 */
#define WM2200_OUT1LMIX_VOL4_MASK               0x00FE  /* OUT1LMIX_VOL4 - [7:1] */
#define WM2200_OUT1LMIX_VOL4_SHIFT                   1  /* OUT1LMIX_VOL4 - [7:1] */
#define WM2200_OUT1LMIX_VOL4_WIDTH                   7  /* OUT1LMIX_VOL4 - [7:1] */

/*
 * R1544 (0x608) - OUT1RMIX Input 1 Source
 */
#define WM2200_OUT1RMIX_SRC1_MASK               0x007F  /* OUT1RMIX_SRC1 - [6:0] */
#define WM2200_OUT1RMIX_SRC1_SHIFT                   0  /* OUT1RMIX_SRC1 - [6:0] */
#define WM2200_OUT1RMIX_SRC1_WIDTH                   7  /* OUT1RMIX_SRC1 - [6:0] */

/*
 * R1545 (0x609) - OUT1RMIX Input 1 Volume
 */
#define WM2200_OUT1RMIX_VOL1_MASK               0x00FE  /* OUT1RMIX_VOL1 - [7:1] */
#define WM2200_OUT1RMIX_VOL1_SHIFT                   1  /* OUT1RMIX_VOL1 - [7:1] */
#define WM2200_OUT1RMIX_VOL1_WIDTH                   7  /* OUT1RMIX_VOL1 - [7:1] */

/*
 * R1546 (0x60A) - OUT1RMIX Input 2 Source
 */
#define WM2200_OUT1RMIX_SRC2_MASK               0x007F  /* OUT1RMIX_SRC2 - [6:0] */
#define WM2200_OUT1RMIX_SRC2_SHIFT                   0  /* OUT1RMIX_SRC2 - [6:0] */
#define WM2200_OUT1RMIX_SRC2_WIDTH                   7  /* OUT1RMIX_SRC2 - [6:0] */

/*
 * R1547 (0x60B) - OUT1RMIX Input 2 Volume
 */
#define WM2200_OUT1RMIX_VOL2_MASK               0x00FE  /* OUT1RMIX_VOL2 - [7:1] */
#define WM2200_OUT1RMIX_VOL2_SHIFT                   1  /* OUT1RMIX_VOL2 - [7:1] */
#define WM2200_OUT1RMIX_VOL2_WIDTH                   7  /* OUT1RMIX_VOL2 - [7:1] */

/*
 * R1548 (0x60C) - OUT1RMIX Input 3 Source
 */
#define WM2200_OUT1RMIX_SRC3_MASK               0x007F  /* OUT1RMIX_SRC3 - [6:0] */
#define WM2200_OUT1RMIX_SRC3_SHIFT                   0  /* OUT1RMIX_SRC3 - [6:0] */
#define WM2200_OUT1RMIX_SRC3_WIDTH                   7  /* OUT1RMIX_SRC3 - [6:0] */

/*
 * R1549 (0x60D) - OUT1RMIX Input 3 Volume
 */
#define WM2200_OUT1RMIX_VOL3_MASK               0x00FE  /* OUT1RMIX_VOL3 - [7:1] */
#define WM2200_OUT1RMIX_VOL3_SHIFT                   1  /* OUT1RMIX_VOL3 - [7:1] */
#define WM2200_OUT1RMIX_VOL3_WIDTH                   7  /* OUT1RMIX_VOL3 - [7:1] */

/*
 * R1550 (0x60E) - OUT1RMIX Input 4 Source
 */
#define WM2200_OUT1RMIX_SRC4_MASK               0x007F  /* OUT1RMIX_SRC4 - [6:0] */
#define WM2200_OUT1RMIX_SRC4_SHIFT                   0  /* OUT1RMIX_SRC4 - [6:0] */
#define WM2200_OUT1RMIX_SRC4_WIDTH                   7  /* OUT1RMIX_SRC4 - [6:0] */

/*
 * R1551 (0x60F) - OUT1RMIX Input 4 Volume
 */
#define WM2200_OUT1RMIX_VOL4_MASK               0x00FE  /* OUT1RMIX_VOL4 - [7:1] */
#define WM2200_OUT1RMIX_VOL4_SHIFT                   1  /* OUT1RMIX_VOL4 - [7:1] */
#define WM2200_OUT1RMIX_VOL4_WIDTH                   7  /* OUT1RMIX_VOL4 - [7:1] */

/*
 * R1552 (0x610) - OUT2LMIX Input 1 Source
 */
#define WM2200_OUT2LMIX_SRC1_MASK               0x007F  /* OUT2LMIX_SRC1 - [6:0] */
#define WM2200_OUT2LMIX_SRC1_SHIFT                   0  /* OUT2LMIX_SRC1 - [6:0] */
#define WM2200_OUT2LMIX_SRC1_WIDTH                   7  /* OUT2LMIX_SRC1 - [6:0] */

/*
 * R1553 (0x611) - OUT2LMIX Input 1 Volume
 */
#define WM2200_OUT2LMIX_VOL1_MASK               0x00FE  /* OUT2LMIX_VOL1 - [7:1] */
#define WM2200_OUT2LMIX_VOL1_SHIFT                   1  /* OUT2LMIX_VOL1 - [7:1] */
#define WM2200_OUT2LMIX_VOL1_WIDTH                   7  /* OUT2LMIX_VOL1 - [7:1] */

/*
 * R1554 (0x612) - OUT2LMIX Input 2 Source
 */
#define WM2200_OUT2LMIX_SRC2_MASK               0x007F  /* OUT2LMIX_SRC2 - [6:0] */
#define WM2200_OUT2LMIX_SRC2_SHIFT                   0  /* OUT2LMIX_SRC2 - [6:0] */
#define WM2200_OUT2LMIX_SRC2_WIDTH                   7  /* OUT2LMIX_SRC2 - [6:0] */

/*
 * R1555 (0x613) - OUT2LMIX Input 2 Volume
 */
#define WM2200_OUT2LMIX_VOL2_MASK               0x00FE  /* OUT2LMIX_VOL2 - [7:1] */
#define WM2200_OUT2LMIX_VOL2_SHIFT                   1  /* OUT2LMIX_VOL2 - [7:1] */
#define WM2200_OUT2LMIX_VOL2_WIDTH                   7  /* OUT2LMIX_VOL2 - [7:1] */

/*
 * R1556 (0x614) - OUT2LMIX Input 3 Source
 */
#define WM2200_OUT2LMIX_SRC3_MASK               0x007F  /* OUT2LMIX_SRC3 - [6:0] */
#define WM2200_OUT2LMIX_SRC3_SHIFT                   0  /* OUT2LMIX_SRC3 - [6:0] */
#define WM2200_OUT2LMIX_SRC3_WIDTH                   7  /* OUT2LMIX_SRC3 - [6:0] */

/*
 * R1557 (0x615) - OUT2LMIX Input 3 Volume
 */
#define WM2200_OUT2LMIX_VOL3_MASK               0x00FE  /* OUT2LMIX_VOL3 - [7:1] */
#define WM2200_OUT2LMIX_VOL3_SHIFT                   1  /* OUT2LMIX_VOL3 - [7:1] */
#define WM2200_OUT2LMIX_VOL3_WIDTH                   7  /* OUT2LMIX_VOL3 - [7:1] */

/*
 * R1558 (0x616) - OUT2LMIX Input 4 Source
 */
#define WM2200_OUT2LMIX_SRC4_MASK               0x007F  /* OUT2LMIX_SRC4 - [6:0] */
#define WM2200_OUT2LMIX_SRC4_SHIFT                   0  /* OUT2LMIX_SRC4 - [6:0] */
#define WM2200_OUT2LMIX_SRC4_WIDTH                   7  /* OUT2LMIX_SRC4 - [6:0] */

/*
 * R1559 (0x617) - OUT2LMIX Input 4 Volume
 */
#define WM2200_OUT2LMIX_VOL4_MASK               0x00FE  /* OUT2LMIX_VOL4 - [7:1] */
#define WM2200_OUT2LMIX_VOL4_SHIFT                   1  /* OUT2LMIX_VOL4 - [7:1] */
#define WM2200_OUT2LMIX_VOL4_WIDTH                   7  /* OUT2LMIX_VOL4 - [7:1] */

/*
 * R1560 (0x618) - OUT2RMIX Input 1 Source
 */
#define WM2200_OUT2RMIX_SRC1_MASK               0x007F  /* OUT2RMIX_SRC1 - [6:0] */
#define WM2200_OUT2RMIX_SRC1_SHIFT                   0  /* OUT2RMIX_SRC1 - [6:0] */
#define WM2200_OUT2RMIX_SRC1_WIDTH                   7  /* OUT2RMIX_SRC1 - [6:0] */

/*
 * R1561 (0x619) - OUT2RMIX Input 1 Volume
 */
#define WM2200_OUT2RMIX_VOL1_MASK               0x00FE  /* OUT2RMIX_VOL1 - [7:1] */
#define WM2200_OUT2RMIX_VOL1_SHIFT                   1  /* OUT2RMIX_VOL1 - [7:1] */
#define WM2200_OUT2RMIX_VOL1_WIDTH                   7  /* OUT2RMIX_VOL1 - [7:1] */

/*
 * R1562 (0x61A) - OUT2RMIX Input 2 Source
 */
#define WM2200_OUT2RMIX_SRC2_MASK               0x007F  /* OUT2RMIX_SRC2 - [6:0] */
#define WM2200_OUT2RMIX_SRC2_SHIFT                   0  /* OUT2RMIX_SRC2 - [6:0] */
#define WM2200_OUT2RMIX_SRC2_WIDTH                   7  /* OUT2RMIX_SRC2 - [6:0] */

/*
 * R1563 (0x61B) - OUT2RMIX Input 2 Volume
 */
#define WM2200_OUT2RMIX_VOL2_MASK               0x00FE  /* OUT2RMIX_VOL2 - [7:1] */
#define WM2200_OUT2RMIX_VOL2_SHIFT                   1  /* OUT2RMIX_VOL2 - [7:1] */
#define WM2200_OUT2RMIX_VOL2_WIDTH                   7  /* OUT2RMIX_VOL2 - [7:1] */

/*
 * R1564 (0x61C) - OUT2RMIX Input 3 Source
 */
#define WM2200_OUT2RMIX_SRC3_MASK               0x007F  /* OUT2RMIX_SRC3 - [6:0] */
#define WM2200_OUT2RMIX_SRC3_SHIFT                   0  /* OUT2RMIX_SRC3 - [6:0] */
#define WM2200_OUT2RMIX_SRC3_WIDTH                   7  /* OUT2RMIX_SRC3 - [6:0] */

/*
 * R1565 (0x61D) - OUT2RMIX Input 3 Volume
 */
#define WM2200_OUT2RMIX_VOL3_MASK               0x00FE  /* OUT2RMIX_VOL3 - [7:1] */
#define WM2200_OUT2RMIX_VOL3_SHIFT                   1  /* OUT2RMIX_VOL3 - [7:1] */
#define WM2200_OUT2RMIX_VOL3_WIDTH                   7  /* OUT2RMIX_VOL3 - [7:1] */

/*
 * R1566 (0x61E) - OUT2RMIX Input 4 Source
 */
#define WM2200_OUT2RMIX_SRC4_MASK               0x007F  /* OUT2RMIX_SRC4 - [6:0] */
#define WM2200_OUT2RMIX_SRC4_SHIFT                   0  /* OUT2RMIX_SRC4 - [6:0] */
#define WM2200_OUT2RMIX_SRC4_WIDTH                   7  /* OUT2RMIX_SRC4 - [6:0] */

/*
 * R1567 (0x61F) - OUT2RMIX Input 4 Volume
 */
#define WM2200_OUT2RMIX_VOL4_MASK               0x00FE  /* OUT2RMIX_VOL4 - [7:1] */
#define WM2200_OUT2RMIX_VOL4_SHIFT                   1  /* OUT2RMIX_VOL4 - [7:1] */
#define WM2200_OUT2RMIX_VOL4_WIDTH                   7  /* OUT2RMIX_VOL4 - [7:1] */

/*
 * R1568 (0x620) - AIF1TX1MIX Input 1 Source
 */
#define WM2200_AIF1TX1MIX_SRC1_MASK             0x007F  /* AIF1TX1MIX_SRC1 - [6:0] */
#define WM2200_AIF1TX1MIX_SRC1_SHIFT                 0  /* AIF1TX1MIX_SRC1 - [6:0] */
#define WM2200_AIF1TX1MIX_SRC1_WIDTH                 7  /* AIF1TX1MIX_SRC1 - [6:0] */

/*
 * R1569 (0x621) - AIF1TX1MIX Input 1 Volume
 */
#define WM2200_AIF1TX1MIX_VOL1_MASK             0x00FE  /* AIF1TX1MIX_VOL1 - [7:1] */
#define WM2200_AIF1TX1MIX_VOL1_SHIFT                 1  /* AIF1TX1MIX_VOL1 - [7:1] */
#define WM2200_AIF1TX1MIX_VOL1_WIDTH                 7  /* AIF1TX1MIX_VOL1 - [7:1] */

/*
 * R1570 (0x622) - AIF1TX1MIX Input 2 Source
 */
#define WM2200_AIF1TX1MIX_SRC2_MASK             0x007F  /* AIF1TX1MIX_SRC2 - [6:0] */
#define WM2200_AIF1TX1MIX_SRC2_SHIFT                 0  /* AIF1TX1MIX_SRC2 - [6:0] */
#define WM2200_AIF1TX1MIX_SRC2_WIDTH                 7  /* AIF1TX1MIX_SRC2 - [6:0] */

/*
 * R1571 (0x623) - AIF1TX1MIX Input 2 Volume
 */
#define WM2200_AIF1TX1MIX_VOL2_MASK             0x00FE  /* AIF1TX1MIX_VOL2 - [7:1] */
#define WM2200_AIF1TX1MIX_VOL2_SHIFT                 1  /* AIF1TX1MIX_VOL2 - [7:1] */
#define WM2200_AIF1TX1MIX_VOL2_WIDTH                 7  /* AIF1TX1MIX_VOL2 - [7:1] */

/*
 * R1572 (0x624) - AIF1TX1MIX Input 3 Source
 */
#define WM2200_AIF1TX1MIX_SRC3_MASK             0x007F  /* AIF1TX1MIX_SRC3 - [6:0] */
#define WM2200_AIF1TX1MIX_SRC3_SHIFT                 0  /* AIF1TX1MIX_SRC3 - [6:0] */
#define WM2200_AIF1TX1MIX_SRC3_WIDTH                 7  /* AIF1TX1MIX_SRC3 - [6:0] */

/*
 * R1573 (0x625) - AIF1TX1MIX Input 3 Volume
 */
#define WM2200_AIF1TX1MIX_VOL3_MASK             0x00FE  /* AIF1TX1MIX_VOL3 - [7:1] */
#define WM2200_AIF1TX1MIX_VOL3_SHIFT                 1  /* AIF1TX1MIX_VOL3 - [7:1] */
#define WM2200_AIF1TX1MIX_VOL3_WIDTH                 7  /* AIF1TX1MIX_VOL3 - [7:1] */

/*
 * R1574 (0x626) - AIF1TX1MIX Input 4 Source
 */
#define WM2200_AIF1TX1MIX_SRC4_MASK             0x007F  /* AIF1TX1MIX_SRC4 - [6:0] */
#define WM2200_AIF1TX1MIX_SRC4_SHIFT                 0  /* AIF1TX1MIX_SRC4 - [6:0] */
#define WM2200_AIF1TX1MIX_SRC4_WIDTH                 7  /* AIF1TX1MIX_SRC4 - [6:0] */

/*
 * R1575 (0x627) - AIF1TX1MIX Input 4 Volume
 */
#define WM2200_AIF1TX1MIX_VOL4_MASK             0x00FE  /* AIF1TX1MIX_VOL4 - [7:1] */
#define WM2200_AIF1TX1MIX_VOL4_SHIFT                 1  /* AIF1TX1MIX_VOL4 - [7:1] */
#define WM2200_AIF1TX1MIX_VOL4_WIDTH                 7  /* AIF1TX1MIX_VOL4 - [7:1] */

/*
 * R1576 (0x628) - AIF1TX2MIX Input 1 Source
 */
#define WM2200_AIF1TX2MIX_SRC1_MASK             0x007F  /* AIF1TX2MIX_SRC1 - [6:0] */
#define WM2200_AIF1TX2MIX_SRC1_SHIFT                 0  /* AIF1TX2MIX_SRC1 - [6:0] */
#define WM2200_AIF1TX2MIX_SRC1_WIDTH                 7  /* AIF1TX2MIX_SRC1 - [6:0] */

/*
 * R1577 (0x629) - AIF1TX2MIX Input 1 Volume
 */
#define WM2200_AIF1TX2MIX_VOL1_MASK             0x00FE  /* AIF1TX2MIX_VOL1 - [7:1] */
#define WM2200_AIF1TX2MIX_VOL1_SHIFT                 1  /* AIF1TX2MIX_VOL1 - [7:1] */
#define WM2200_AIF1TX2MIX_VOL1_WIDTH                 7  /* AIF1TX2MIX_VOL1 - [7:1] */

/*
 * R1578 (0x62A) - AIF1TX2MIX Input 2 Source
 */
#define WM2200_AIF1TX2MIX_SRC2_MASK             0x007F  /* AIF1TX2MIX_SRC2 - [6:0] */
#define WM2200_AIF1TX2MIX_SRC2_SHIFT                 0  /* AIF1TX2MIX_SRC2 - [6:0] */
#define WM2200_AIF1TX2MIX_SRC2_WIDTH                 7  /* AIF1TX2MIX_SRC2 - [6:0] */

/*
 * R1579 (0x62B) - AIF1TX2MIX Input 2 Volume
 */
#define WM2200_AIF1TX2MIX_VOL2_MASK             0x00FE  /* AIF1TX2MIX_VOL2 - [7:1] */
#define WM2200_AIF1TX2MIX_VOL2_SHIFT                 1  /* AIF1TX2MIX_VOL2 - [7:1] */
#define WM2200_AIF1TX2MIX_VOL2_WIDTH                 7  /* AIF1TX2MIX_VOL2 - [7:1] */

/*
 * R1580 (0x62C) - AIF1TX2MIX Input 3 Source
 */
#define WM2200_AIF1TX2MIX_SRC3_MASK             0x007F  /* AIF1TX2MIX_SRC3 - [6:0] */
#define WM2200_AIF1TX2MIX_SRC3_SHIFT                 0  /* AIF1TX2MIX_SRC3 - [6:0] */
#define WM2200_AIF1TX2MIX_SRC3_WIDTH                 7  /* AIF1TX2MIX_SRC3 - [6:0] */

/*
 * R1581 (0x62D) - AIF1TX2MIX Input 3 Volume
 */
#define WM2200_AIF1TX2MIX_VOL3_MASK             0x00FE  /* AIF1TX2MIX_VOL3 - [7:1] */
#define WM2200_AIF1TX2MIX_VOL3_SHIFT                 1  /* AIF1TX2MIX_VOL3 - [7:1] */
#define WM2200_AIF1TX2MIX_VOL3_WIDTH                 7  /* AIF1TX2MIX_VOL3 - [7:1] */

/*
 * R1582 (0x62E) - AIF1TX2MIX Input 4 Source
 */
#define WM2200_AIF1TX2MIX_SRC4_MASK             0x007F  /* AIF1TX2MIX_SRC4 - [6:0] */
#define WM2200_AIF1TX2MIX_SRC4_SHIFT                 0  /* AIF1TX2MIX_SRC4 - [6:0] */
#define WM2200_AIF1TX2MIX_SRC4_WIDTH                 7  /* AIF1TX2MIX_SRC4 - [6:0] */

/*
 * R1583 (0x62F) - AIF1TX2MIX Input 4 Volume
 */
#define WM2200_AIF1TX2MIX_VOL4_MASK             0x00FE  /* AIF1TX2MIX_VOL4 - [7:1] */
#define WM2200_AIF1TX2MIX_VOL4_SHIFT                 1  /* AIF1TX2MIX_VOL4 - [7:1] */
#define WM2200_AIF1TX2MIX_VOL4_WIDTH                 7  /* AIF1TX2MIX_VOL4 - [7:1] */

/*
 * R1584 (0x630) - AIF1TX3MIX Input 1 Source
 */
#define WM2200_AIF1TX3MIX_SRC1_MASK             0x007F  /* AIF1TX3MIX_SRC1 - [6:0] */
#define WM2200_AIF1TX3MIX_SRC1_SHIFT                 0  /* AIF1TX3MIX_SRC1 - [6:0] */
#define WM2200_AIF1TX3MIX_SRC1_WIDTH                 7  /* AIF1TX3MIX_SRC1 - [6:0] */

/*
 * R1585 (0x631) - AIF1TX3MIX Input 1 Volume
 */
#define WM2200_AIF1TX3MIX_VOL1_MASK             0x00FE  /* AIF1TX3MIX_VOL1 - [7:1] */
#define WM2200_AIF1TX3MIX_VOL1_SHIFT                 1  /* AIF1TX3MIX_VOL1 - [7:1] */
#define WM2200_AIF1TX3MIX_VOL1_WIDTH                 7  /* AIF1TX3MIX_VOL1 - [7:1] */

/*
 * R1586 (0x632) - AIF1TX3MIX Input 2 Source
 */
#define WM2200_AIF1TX3MIX_SRC2_MASK             0x007F  /* AIF1TX3MIX_SRC2 - [6:0] */
#define WM2200_AIF1TX3MIX_SRC2_SHIFT                 0  /* AIF1TX3MIX_SRC2 - [6:0] */
#define WM2200_AIF1TX3MIX_SRC2_WIDTH                 7  /* AIF1TX3MIX_SRC2 - [6:0] */

/*
 * R1587 (0x633) - AIF1TX3MIX Input 2 Volume
 */
#define WM2200_AIF1TX3MIX_VOL2_MASK             0x00FE  /* AIF1TX3MIX_VOL2 - [7:1] */
#define WM2200_AIF1TX3MIX_VOL2_SHIFT                 1  /* AIF1TX3MIX_VOL2 - [7:1] */
#define WM2200_AIF1TX3MIX_VOL2_WIDTH                 7  /* AIF1TX3MIX_VOL2 - [7:1] */

/*
 * R1588 (0x634) - AIF1TX3MIX Input 3 Source
 */
#define WM2200_AIF1TX3MIX_SRC3_MASK             0x007F  /* AIF1TX3MIX_SRC3 - [6:0] */
#define WM2200_AIF1TX3MIX_SRC3_SHIFT                 0  /* AIF1TX3MIX_SRC3 - [6:0] */
#define WM2200_AIF1TX3MIX_SRC3_WIDTH                 7  /* AIF1TX3MIX_SRC3 - [6:0] */

/*
 * R1589 (0x635) - AIF1TX3MIX Input 3 Volume
 */
#define WM2200_AIF1TX3MIX_VOL3_MASK             0x00FE  /* AIF1TX3MIX_VOL3 - [7:1] */
#define WM2200_AIF1TX3MIX_VOL3_SHIFT                 1  /* AIF1TX3MIX_VOL3 - [7:1] */
#define WM2200_AIF1TX3MIX_VOL3_WIDTH                 7  /* AIF1TX3MIX_VOL3 - [7:1] */

/*
 * R1590 (0x636) - AIF1TX3MIX Input 4 Source
 */
#define WM2200_AIF1TX3MIX_SRC4_MASK             0x007F  /* AIF1TX3MIX_SRC4 - [6:0] */
#define WM2200_AIF1TX3MIX_SRC4_SHIFT                 0  /* AIF1TX3MIX_SRC4 - [6:0] */
#define WM2200_AIF1TX3MIX_SRC4_WIDTH                 7  /* AIF1TX3MIX_SRC4 - [6:0] */

/*
 * R1591 (0x637) - AIF1TX3MIX Input 4 Volume
 */
#define WM2200_AIF1TX3MIX_VOL4_MASK             0x00FE  /* AIF1TX3MIX_VOL4 - [7:1] */
#define WM2200_AIF1TX3MIX_VOL4_SHIFT                 1  /* AIF1TX3MIX_VOL4 - [7:1] */
#define WM2200_AIF1TX3MIX_VOL4_WIDTH                 7  /* AIF1TX3MIX_VOL4 - [7:1] */

/*
 * R1592 (0x638) - AIF1TX4MIX Input 1 Source
 */
#define WM2200_AIF1TX4MIX_SRC1_MASK             0x007F  /* AIF1TX4MIX_SRC1 - [6:0] */
#define WM2200_AIF1TX4MIX_SRC1_SHIFT                 0  /* AIF1TX4MIX_SRC1 - [6:0] */
#define WM2200_AIF1TX4MIX_SRC1_WIDTH                 7  /* AIF1TX4MIX_SRC1 - [6:0] */

/*
 * R1593 (0x639) - AIF1TX4MIX Input 1 Volume
 */
#define WM2200_AIF1TX4MIX_VOL1_MASK             0x00FE  /* AIF1TX4MIX_VOL1 - [7:1] */
#define WM2200_AIF1TX4MIX_VOL1_SHIFT                 1  /* AIF1TX4MIX_VOL1 - [7:1] */
#define WM2200_AIF1TX4MIX_VOL1_WIDTH                 7  /* AIF1TX4MIX_VOL1 - [7:1] */

/*
 * R1594 (0x63A) - AIF1TX4MIX Input 2 Source
 */
#define WM2200_AIF1TX4MIX_SRC2_MASK             0x007F  /* AIF1TX4MIX_SRC2 - [6:0] */
#define WM2200_AIF1TX4MIX_SRC2_SHIFT                 0  /* AIF1TX4MIX_SRC2 - [6:0] */
#define WM2200_AIF1TX4MIX_SRC2_WIDTH                 7  /* AIF1TX4MIX_SRC2 - [6:0] */

/*
 * R1595 (0x63B) - AIF1TX4MIX Input 2 Volume
 */
#define WM2200_AIF1TX4MIX_VOL2_MASK             0x00FE  /* AIF1TX4MIX_VOL2 - [7:1] */
#define WM2200_AIF1TX4MIX_VOL2_SHIFT                 1  /* AIF1TX4MIX_VOL2 - [7:1] */
#define WM2200_AIF1TX4MIX_VOL2_WIDTH                 7  /* AIF1TX4MIX_VOL2 - [7:1] */

/*
 * R1596 (0x63C) - AIF1TX4MIX Input 3 Source
 */
#define WM2200_AIF1TX4MIX_SRC3_MASK             0x007F  /* AIF1TX4MIX_SRC3 - [6:0] */
#define WM2200_AIF1TX4MIX_SRC3_SHIFT                 0  /* AIF1TX4MIX_SRC3 - [6:0] */
#define WM2200_AIF1TX4MIX_SRC3_WIDTH                 7  /* AIF1TX4MIX_SRC3 - [6:0] */

/*
 * R1597 (0x63D) - AIF1TX4MIX Input 3 Volume
 */
#define WM2200_AIF1TX4MIX_VOL3_MASK             0x00FE  /* AIF1TX4MIX_VOL3 - [7:1] */
#define WM2200_AIF1TX4MIX_VOL3_SHIFT                 1  /* AIF1TX4MIX_VOL3 - [7:1] */
#define WM2200_AIF1TX4MIX_VOL3_WIDTH                 7  /* AIF1TX4MIX_VOL3 - [7:1] */

/*
 * R1598 (0x63E) - AIF1TX4MIX Input 4 Source
 */
#define WM2200_AIF1TX4MIX_SRC4_MASK             0x007F  /* AIF1TX4MIX_SRC4 - [6:0] */
#define WM2200_AIF1TX4MIX_SRC4_SHIFT                 0  /* AIF1TX4MIX_SRC4 - [6:0] */
#define WM2200_AIF1TX4MIX_SRC4_WIDTH                 7  /* AIF1TX4MIX_SRC4 - [6:0] */

/*
 * R1599 (0x63F) - AIF1TX4MIX Input 4 Volume
 */
#define WM2200_AIF1TX4MIX_VOL4_MASK             0x00FE  /* AIF1TX4MIX_VOL4 - [7:1] */
#define WM2200_AIF1TX4MIX_VOL4_SHIFT                 1  /* AIF1TX4MIX_VOL4 - [7:1] */
#define WM2200_AIF1TX4MIX_VOL4_WIDTH                 7  /* AIF1TX4MIX_VOL4 - [7:1] */

/*
 * R1600 (0x640) - AIF1TX5MIX Input 1 Source
 */
#define WM2200_AIF1TX5MIX_SRC1_MASK             0x007F  /* AIF1TX5MIX_SRC1 - [6:0] */
#define WM2200_AIF1TX5MIX_SRC1_SHIFT                 0  /* AIF1TX5MIX_SRC1 - [6:0] */
#define WM2200_AIF1TX5MIX_SRC1_WIDTH                 7  /* AIF1TX5MIX_SRC1 - [6:0] */

/*
 * R1601 (0x641) - AIF1TX5MIX Input 1 Volume
 */
#define WM2200_AIF1TX5MIX_VOL1_MASK             0x00FE  /* AIF1TX5MIX_VOL1 - [7:1] */
#define WM2200_AIF1TX5MIX_VOL1_SHIFT                 1  /* AIF1TX5MIX_VOL1 - [7:1] */
#define WM2200_AIF1TX5MIX_VOL1_WIDTH                 7  /* AIF1TX5MIX_VOL1 - [7:1] */

/*
 * R1602 (0x642) - AIF1TX5MIX Input 2 Source
 */
#define WM2200_AIF1TX5MIX_SRC2_MASK             0x007F  /* AIF1TX5MIX_SRC2 - [6:0] */
#define WM2200_AIF1TX5MIX_SRC2_SHIFT                 0  /* AIF1TX5MIX_SRC2 - [6:0] */
#define WM2200_AIF1TX5MIX_SRC2_WIDTH                 7  /* AIF1TX5MIX_SRC2 - [6:0] */

/*
 * R1603 (0x643) - AIF1TX5MIX Input 2 Volume
 */
#define WM2200_AIF1TX5MIX_VOL2_MASK             0x00FE  /* AIF1TX5MIX_VOL2 - [7:1] */
#define WM2200_AIF1TX5MIX_VOL2_SHIFT                 1  /* AIF1TX5MIX_VOL2 - [7:1] */
#define WM2200_AIF1TX5MIX_VOL2_WIDTH                 7  /* AIF1TX5MIX_VOL2 - [7:1] */

/*
 * R1604 (0x644) - AIF1TX5MIX Input 3 Source
 */
#define WM2200_AIF1TX5MIX_SRC3_MASK             0x007F  /* AIF1TX5MIX_SRC3 - [6:0] */
#define WM2200_AIF1TX5MIX_SRC3_SHIFT                 0  /* AIF1TX5MIX_SRC3 - [6:0] */
#define WM2200_AIF1TX5MIX_SRC3_WIDTH                 7  /* AIF1TX5MIX_SRC3 - [6:0] */

/*
 * R1605 (0x645) - AIF1TX5MIX Input 3 Volume
 */
#define WM2200_AIF1TX5MIX_VOL3_MASK             0x00FE  /* AIF1TX5MIX_VOL3 - [7:1] */
#define WM2200_AIF1TX5MIX_VOL3_SHIFT                 1  /* AIF1TX5MIX_VOL3 - [7:1] */
#define WM2200_AIF1TX5MIX_VOL3_WIDTH                 7  /* AIF1TX5MIX_VOL3 - [7:1] */

/*
 * R1606 (0x646) - AIF1TX5MIX Input 4 Source
 */
#define WM2200_AIF1TX5MIX_SRC4_MASK             0x007F  /* AIF1TX5MIX_SRC4 - [6:0] */
#define WM2200_AIF1TX5MIX_SRC4_SHIFT                 0  /* AIF1TX5MIX_SRC4 - [6:0] */
#define WM2200_AIF1TX5MIX_SRC4_WIDTH                 7  /* AIF1TX5MIX_SRC4 - [6:0] */

/*
 * R1607 (0x647) - AIF1TX5MIX Input 4 Volume
 */
#define WM2200_AIF1TX5MIX_VOL4_MASK             0x00FE  /* AIF1TX5MIX_VOL4 - [7:1] */
#define WM2200_AIF1TX5MIX_VOL4_SHIFT                 1  /* AIF1TX5MIX_VOL4 - [7:1] */
#define WM2200_AIF1TX5MIX_VOL4_WIDTH                 7  /* AIF1TX5MIX_VOL4 - [7:1] */

/*
 * R1608 (0x648) - AIF1TX6MIX Input 1 Source
 */
#define WM2200_AIF1TX6MIX_SRC1_MASK             0x007F  /* AIF1TX6MIX_SRC1 - [6:0] */
#define WM2200_AIF1TX6MIX_SRC1_SHIFT                 0  /* AIF1TX6MIX_SRC1 - [6:0] */
#define WM2200_AIF1TX6MIX_SRC1_WIDTH                 7  /* AIF1TX6MIX_SRC1 - [6:0] */

/*
 * R1609 (0x649) - AIF1TX6MIX Input 1 Volume
 */
#define WM2200_AIF1TX6MIX_VOL1_MASK             0x00FE  /* AIF1TX6MIX_VOL1 - [7:1] */
#define WM2200_AIF1TX6MIX_VOL1_SHIFT                 1  /* AIF1TX6MIX_VOL1 - [7:1] */
#define WM2200_AIF1TX6MIX_VOL1_WIDTH                 7  /* AIF1TX6MIX_VOL1 - [7:1] */

/*
 * R1610 (0x64A) - AIF1TX6MIX Input 2 Source
 */
#define WM2200_AIF1TX6MIX_SRC2_MASK             0x007F  /* AIF1TX6MIX_SRC2 - [6:0] */
#define WM2200_AIF1TX6MIX_SRC2_SHIFT                 0  /* AIF1TX6MIX_SRC2 - [6:0] */
#define WM2200_AIF1TX6MIX_SRC2_WIDTH                 7  /* AIF1TX6MIX_SRC2 - [6:0] */

/*
 * R1611 (0x64B) - AIF1TX6MIX Input 2 Volume
 */
#define WM2200_AIF1TX6MIX_VOL2_MASK             0x00FE  /* AIF1TX6MIX_VOL2 - [7:1] */
#define WM2200_AIF1TX6MIX_VOL2_SHIFT                 1  /* AIF1TX6MIX_VOL2 - [7:1] */
#define WM2200_AIF1TX6MIX_VOL2_WIDTH                 7  /* AIF1TX6MIX_VOL2 - [7:1] */

/*
 * R1612 (0x64C) - AIF1TX6MIX Input 3 Source
 */
#define WM2200_AIF1TX6MIX_SRC3_MASK             0x007F  /* AIF1TX6MIX_SRC3 - [6:0] */
#define WM2200_AIF1TX6MIX_SRC3_SHIFT                 0  /* AIF1TX6MIX_SRC3 - [6:0] */
#define WM2200_AIF1TX6MIX_SRC3_WIDTH                 7  /* AIF1TX6MIX_SRC3 - [6:0] */

/*
 * R1613 (0x64D) - AIF1TX6MIX Input 3 Volume
 */
#define WM2200_AIF1TX6MIX_VOL3_MASK             0x00FE  /* AIF1TX6MIX_VOL3 - [7:1] */
#define WM2200_AIF1TX6MIX_VOL3_SHIFT                 1  /* AIF1TX6MIX_VOL3 - [7:1] */
#define WM2200_AIF1TX6MIX_VOL3_WIDTH                 7  /* AIF1TX6MIX_VOL3 - [7:1] */

/*
 * R1614 (0x64E) - AIF1TX6MIX Input 4 Source
 */
#define WM2200_AIF1TX6MIX_SRC4_MASK             0x007F  /* AIF1TX6MIX_SRC4 - [6:0] */
#define WM2200_AIF1TX6MIX_SRC4_SHIFT                 0  /* AIF1TX6MIX_SRC4 - [6:0] */
#define WM2200_AIF1TX6MIX_SRC4_WIDTH                 7  /* AIF1TX6MIX_SRC4 - [6:0] */

/*
 * R1615 (0x64F) - AIF1TX6MIX Input 4 Volume
 */
#define WM2200_AIF1TX6MIX_VOL4_MASK             0x00FE  /* AIF1TX6MIX_VOL4 - [7:1] */
#define WM2200_AIF1TX6MIX_VOL4_SHIFT                 1  /* AIF1TX6MIX_VOL4 - [7:1] */
#define WM2200_AIF1TX6MIX_VOL4_WIDTH                 7  /* AIF1TX6MIX_VOL4 - [7:1] */

/*
 * R1616 (0x650) - EQLMIX Input 1 Source
 */
#define WM2200_EQLMIX_SRC1_MASK                 0x007F  /* EQLMIX_SRC1 - [6:0] */
#define WM2200_EQLMIX_SRC1_SHIFT                     0  /* EQLMIX_SRC1 - [6:0] */
#define WM2200_EQLMIX_SRC1_WIDTH                     7  /* EQLMIX_SRC1 - [6:0] */

/*
 * R1617 (0x651) - EQLMIX Input 1 Volume
 */
#define WM2200_EQLMIX_VOL1_MASK                 0x00FE  /* EQLMIX_VOL1 - [7:1] */
#define WM2200_EQLMIX_VOL1_SHIFT                     1  /* EQLMIX_VOL1 - [7:1] */
#define WM2200_EQLMIX_VOL1_WIDTH                     7  /* EQLMIX_VOL1 - [7:1] */

/*
 * R1618 (0x652) - EQLMIX Input 2 Source
 */
#define WM2200_EQLMIX_SRC2_MASK                 0x007F  /* EQLMIX_SRC2 - [6:0] */
#define WM2200_EQLMIX_SRC2_SHIFT                     0  /* EQLMIX_SRC2 - [6:0] */
#define WM2200_EQLMIX_SRC2_WIDTH                     7  /* EQLMIX_SRC2 - [6:0] */

/*
 * R1619 (0x653) - EQLMIX Input 2 Volume
 */
#define WM2200_EQLMIX_VOL2_MASK                 0x00FE  /* EQLMIX_VOL2 - [7:1] */
#define WM2200_EQLMIX_VOL2_SHIFT                     1  /* EQLMIX_VOL2 - [7:1] */
#define WM2200_EQLMIX_VOL2_WIDTH                     7  /* EQLMIX_VOL2 - [7:1] */

/*
 * R1620 (0x654) - EQLMIX Input 3 Source
 */
#define WM2200_EQLMIX_SRC3_MASK                 0x007F  /* EQLMIX_SRC3 - [6:0] */
#define WM2200_EQLMIX_SRC3_SHIFT                     0  /* EQLMIX_SRC3 - [6:0] */
#define WM2200_EQLMIX_SRC3_WIDTH                     7  /* EQLMIX_SRC3 - [6:0] */

/*
 * R1621 (0x655) - EQLMIX Input 3 Volume
 */
#define WM2200_EQLMIX_VOL3_MASK                 0x00FE  /* EQLMIX_VOL3 - [7:1] */
#define WM2200_EQLMIX_VOL3_SHIFT                     1  /* EQLMIX_VOL3 - [7:1] */
#define WM2200_EQLMIX_VOL3_WIDTH                     7  /* EQLMIX_VOL3 - [7:1] */

/*
 * R1622 (0x656) - EQLMIX Input 4 Source
 */
#define WM2200_EQLMIX_SRC4_MASK                 0x007F  /* EQLMIX_SRC4 - [6:0] */
#define WM2200_EQLMIX_SRC4_SHIFT                     0  /* EQLMIX_SRC4 - [6:0] */
#define WM2200_EQLMIX_SRC4_WIDTH                     7  /* EQLMIX_SRC4 - [6:0] */

/*
 * R1623 (0x657) - EQLMIX Input 4 Volume
 */
#define WM2200_EQLMIX_VOL4_MASK                 0x00FE  /* EQLMIX_VOL4 - [7:1] */
#define WM2200_EQLMIX_VOL4_SHIFT                     1  /* EQLMIX_VOL4 - [7:1] */
#define WM2200_EQLMIX_VOL4_WIDTH                     7  /* EQLMIX_VOL4 - [7:1] */

/*
 * R1624 (0x658) - EQRMIX Input 1 Source
 */
#define WM2200_EQRMIX_SRC1_MASK                 0x007F  /* EQRMIX_SRC1 - [6:0] */
#define WM2200_EQRMIX_SRC1_SHIFT                     0  /* EQRMIX_SRC1 - [6:0] */
#define WM2200_EQRMIX_SRC1_WIDTH                     7  /* EQRMIX_SRC1 - [6:0] */

/*
 * R1625 (0x659) - EQRMIX Input 1 Volume
 */
#define WM2200_EQRMIX_VOL1_MASK                 0x00FE  /* EQRMIX_VOL1 - [7:1] */
#define WM2200_EQRMIX_VOL1_SHIFT                     1  /* EQRMIX_VOL1 - [7:1] */
#define WM2200_EQRMIX_VOL1_WIDTH                     7  /* EQRMIX_VOL1 - [7:1] */

/*
 * R1626 (0x65A) - EQRMIX Input 2 Source
 */
#define WM2200_EQRMIX_SRC2_MASK                 0x007F  /* EQRMIX_SRC2 - [6:0] */
#define WM2200_EQRMIX_SRC2_SHIFT                     0  /* EQRMIX_SRC2 - [6:0] */
#define WM2200_EQRMIX_SRC2_WIDTH                     7  /* EQRMIX_SRC2 - [6:0] */

/*
 * R1627 (0x65B) - EQRMIX Input 2 Volume
 */
#define WM2200_EQRMIX_VOL2_MASK                 0x00FE  /* EQRMIX_VOL2 - [7:1] */
#define WM2200_EQRMIX_VOL2_SHIFT                     1  /* EQRMIX_VOL2 - [7:1] */
#define WM2200_EQRMIX_VOL2_WIDTH                     7  /* EQRMIX_VOL2 - [7:1] */

/*
 * R1628 (0x65C) - EQRMIX Input 3 Source
 */
#define WM2200_EQRMIX_SRC3_MASK                 0x007F  /* EQRMIX_SRC3 - [6:0] */
#define WM2200_EQRMIX_SRC3_SHIFT                     0  /* EQRMIX_SRC3 - [6:0] */
#define WM2200_EQRMIX_SRC3_WIDTH                     7  /* EQRMIX_SRC3 - [6:0] */

/*
 * R1629 (0x65D) - EQRMIX Input 3 Volume
 */
#define WM2200_EQRMIX_VOL3_MASK                 0x00FE  /* EQRMIX_VOL3 - [7:1] */
#define WM2200_EQRMIX_VOL3_SHIFT                     1  /* EQRMIX_VOL3 - [7:1] */
#define WM2200_EQRMIX_VOL3_WIDTH                     7  /* EQRMIX_VOL3 - [7:1] */

/*
 * R1630 (0x65E) - EQRMIX Input 4 Source
 */
#define WM2200_EQRMIX_SRC4_MASK                 0x007F  /* EQRMIX_SRC4 - [6:0] */
#define WM2200_EQRMIX_SRC4_SHIFT                     0  /* EQRMIX_SRC4 - [6:0] */
#define WM2200_EQRMIX_SRC4_WIDTH                     7  /* EQRMIX_SRC4 - [6:0] */

/*
 * R1631 (0x65F) - EQRMIX Input 4 Volume
 */
#define WM2200_EQRMIX_VOL4_MASK                 0x00FE  /* EQRMIX_VOL4 - [7:1] */
#define WM2200_EQRMIX_VOL4_SHIFT                     1  /* EQRMIX_VOL4 - [7:1] */
#define WM2200_EQRMIX_VOL4_WIDTH                     7  /* EQRMIX_VOL4 - [7:1] */

/*
 * R1632 (0x660) - LHPF1MIX Input 1 Source
 */
#define WM2200_LHPF1MIX_SRC1_MASK               0x007F  /* LHPF1MIX_SRC1 - [6:0] */
#define WM2200_LHPF1MIX_SRC1_SHIFT                   0  /* LHPF1MIX_SRC1 - [6:0] */
#define WM2200_LHPF1MIX_SRC1_WIDTH                   7  /* LHPF1MIX_SRC1 - [6:0] */

/*
 * R1633 (0x661) - LHPF1MIX Input 1 Volume
 */
#define WM2200_LHPF1MIX_VOL1_MASK               0x00FE  /* LHPF1MIX_VOL1 - [7:1] */
#define WM2200_LHPF1MIX_VOL1_SHIFT                   1  /* LHPF1MIX_VOL1 - [7:1] */
#define WM2200_LHPF1MIX_VOL1_WIDTH                   7  /* LHPF1MIX_VOL1 - [7:1] */

/*
 * R1634 (0x662) - LHPF1MIX Input 2 Source
 */
#define WM2200_LHPF1MIX_SRC2_MASK               0x007F  /* LHPF1MIX_SRC2 - [6:0] */
#define WM2200_LHPF1MIX_SRC2_SHIFT                   0  /* LHPF1MIX_SRC2 - [6:0] */
#define WM2200_LHPF1MIX_SRC2_WIDTH                   7  /* LHPF1MIX_SRC2 - [6:0] */

/*
 * R1635 (0x663) - LHPF1MIX Input 2 Volume
 */
#define WM2200_LHPF1MIX_VOL2_MASK               0x00FE  /* LHPF1MIX_VOL2 - [7:1] */
#define WM2200_LHPF1MIX_VOL2_SHIFT                   1  /* LHPF1MIX_VOL2 - [7:1] */
#define WM2200_LHPF1MIX_VOL2_WIDTH                   7  /* LHPF1MIX_VOL2 - [7:1] */

/*
 * R1636 (0x664) - LHPF1MIX Input 3 Source
 */
#define WM2200_LHPF1MIX_SRC3_MASK               0x007F  /* LHPF1MIX_SRC3 - [6:0] */
#define WM2200_LHPF1MIX_SRC3_SHIFT                   0  /* LHPF1MIX_SRC3 - [6:0] */
#define WM2200_LHPF1MIX_SRC3_WIDTH                   7  /* LHPF1MIX_SRC3 - [6:0] */

/*
 * R1637 (0x665) - LHPF1MIX Input 3 Volume
 */
#define WM2200_LHPF1MIX_VOL3_MASK               0x00FE  /* LHPF1MIX_VOL3 - [7:1] */
#define WM2200_LHPF1MIX_VOL3_SHIFT                   1  /* LHPF1MIX_VOL3 - [7:1] */
#define WM2200_LHPF1MIX_VOL3_WIDTH                   7  /* LHPF1MIX_VOL3 - [7:1] */

/*
 * R1638 (0x666) - LHPF1MIX Input 4 Source
 */
#define WM2200_LHPF1MIX_SRC4_MASK               0x007F  /* LHPF1MIX_SRC4 - [6:0] */
#define WM2200_LHPF1MIX_SRC4_SHIFT                   0  /* LHPF1MIX_SRC4 - [6:0] */
#define WM2200_LHPF1MIX_SRC4_WIDTH                   7  /* LHPF1MIX_SRC4 - [6:0] */

/*
 * R1639 (0x667) - LHPF1MIX Input 4 Volume
 */
#define WM2200_LHPF1MIX_VOL4_MASK               0x00FE  /* LHPF1MIX_VOL4 - [7:1] */
#define WM2200_LHPF1MIX_VOL4_SHIFT                   1  /* LHPF1MIX_VOL4 - [7:1] */
#define WM2200_LHPF1MIX_VOL4_WIDTH                   7  /* LHPF1MIX_VOL4 - [7:1] */

/*
 * R1640 (0x668) - LHPF2MIX Input 1 Source
 */
#define WM2200_LHPF2MIX_SRC1_MASK               0x007F  /* LHPF2MIX_SRC1 - [6:0] */
#define WM2200_LHPF2MIX_SRC1_SHIFT                   0  /* LHPF2MIX_SRC1 - [6:0] */
#define WM2200_LHPF2MIX_SRC1_WIDTH                   7  /* LHPF2MIX_SRC1 - [6:0] */

/*
 * R1641 (0x669) - LHPF2MIX Input 1 Volume
 */
#define WM2200_LHPF2MIX_VOL1_MASK               0x00FE  /* LHPF2MIX_VOL1 - [7:1] */
#define WM2200_LHPF2MIX_VOL1_SHIFT                   1  /* LHPF2MIX_VOL1 - [7:1] */
#define WM2200_LHPF2MIX_VOL1_WIDTH                   7  /* LHPF2MIX_VOL1 - [7:1] */

/*
 * R1642 (0x66A) - LHPF2MIX Input 2 Source
 */
#define WM2200_LHPF2MIX_SRC2_MASK               0x007F  /* LHPF2MIX_SRC2 - [6:0] */
#define WM2200_LHPF2MIX_SRC2_SHIFT                   0  /* LHPF2MIX_SRC2 - [6:0] */
#define WM2200_LHPF2MIX_SRC2_WIDTH                   7  /* LHPF2MIX_SRC2 - [6:0] */

/*
 * R1643 (0x66B) - LHPF2MIX Input 2 Volume
 */
#define WM2200_LHPF2MIX_VOL2_MASK               0x00FE  /* LHPF2MIX_VOL2 - [7:1] */
#define WM2200_LHPF2MIX_VOL2_SHIFT                   1  /* LHPF2MIX_VOL2 - [7:1] */
#define WM2200_LHPF2MIX_VOL2_WIDTH                   7  /* LHPF2MIX_VOL2 - [7:1] */

/*
 * R1644 (0x66C) - LHPF2MIX Input 3 Source
 */
#define WM2200_LHPF2MIX_SRC3_MASK               0x007F  /* LHPF2MIX_SRC3 - [6:0] */
#define WM2200_LHPF2MIX_SRC3_SHIFT                   0  /* LHPF2MIX_SRC3 - [6:0] */
#define WM2200_LHPF2MIX_SRC3_WIDTH                   7  /* LHPF2MIX_SRC3 - [6:0] */

/*
 * R1645 (0x66D) - LHPF2MIX Input 3 Volume
 */
#define WM2200_LHPF2MIX_VOL3_MASK               0x00FE  /* LHPF2MIX_VOL3 - [7:1] */
#define WM2200_LHPF2MIX_VOL3_SHIFT                   1  /* LHPF2MIX_VOL3 - [7:1] */
#define WM2200_LHPF2MIX_VOL3_WIDTH                   7  /* LHPF2MIX_VOL3 - [7:1] */

/*
 * R1646 (0x66E) - LHPF2MIX Input 4 Source
 */
#define WM2200_LHPF2MIX_SRC4_MASK               0x007F  /* LHPF2MIX_SRC4 - [6:0] */
#define WM2200_LHPF2MIX_SRC4_SHIFT                   0  /* LHPF2MIX_SRC4 - [6:0] */
#define WM2200_LHPF2MIX_SRC4_WIDTH                   7  /* LHPF2MIX_SRC4 - [6:0] */

/*
 * R1647 (0x66F) - LHPF2MIX Input 4 Volume
 */
#define WM2200_LHPF2MIX_VOL4_MASK               0x00FE  /* LHPF2MIX_VOL4 - [7:1] */
#define WM2200_LHPF2MIX_VOL4_SHIFT                   1  /* LHPF2MIX_VOL4 - [7:1] */
#define WM2200_LHPF2MIX_VOL4_WIDTH                   7  /* LHPF2MIX_VOL4 - [7:1] */

/*
 * R1648 (0x670) - DSP1LMIX Input 1 Source
 */
#define WM2200_DSP1LMIX_SRC1_MASK               0x007F  /* DSP1LMIX_SRC1 - [6:0] */
#define WM2200_DSP1LMIX_SRC1_SHIFT                   0  /* DSP1LMIX_SRC1 - [6:0] */
#define WM2200_DSP1LMIX_SRC1_WIDTH                   7  /* DSP1LMIX_SRC1 - [6:0] */

/*
 * R1649 (0x671) - DSP1LMIX Input 1 Volume
 */
#define WM2200_DSP1LMIX_VOL1_MASK               0x00FE  /* DSP1LMIX_VOL1 - [7:1] */
#define WM2200_DSP1LMIX_VOL1_SHIFT                   1  /* DSP1LMIX_VOL1 - [7:1] */
#define WM2200_DSP1LMIX_VOL1_WIDTH                   7  /* DSP1LMIX_VOL1 - [7:1] */

/*
 * R1650 (0x672) - DSP1LMIX Input 2 Source
 */
#define WM2200_DSP1LMIX_SRC2_MASK               0x007F  /* DSP1LMIX_SRC2 - [6:0] */
#define WM2200_DSP1LMIX_SRC2_SHIFT                   0  /* DSP1LMIX_SRC2 - [6:0] */
#define WM2200_DSP1LMIX_SRC2_WIDTH                   7  /* DSP1LMIX_SRC2 - [6:0] */

/*
 * R1651 (0x673) - DSP1LMIX Input 2 Volume
 */
#define WM2200_DSP1LMIX_VOL2_MASK               0x00FE  /* DSP1LMIX_VOL2 - [7:1] */
#define WM2200_DSP1LMIX_VOL2_SHIFT                   1  /* DSP1LMIX_VOL2 - [7:1] */
#define WM2200_DSP1LMIX_VOL2_WIDTH                   7  /* DSP1LMIX_VOL2 - [7:1] */

/*
 * R1652 (0x674) - DSP1LMIX Input 3 Source
 */
#define WM2200_DSP1LMIX_SRC3_MASK               0x007F  /* DSP1LMIX_SRC3 - [6:0] */
#define WM2200_DSP1LMIX_SRC3_SHIFT                   0  /* DSP1LMIX_SRC3 - [6:0] */
#define WM2200_DSP1LMIX_SRC3_WIDTH                   7  /* DSP1LMIX_SRC3 - [6:0] */

/*
 * R1653 (0x675) - DSP1LMIX Input 3 Volume
 */
#define WM2200_DSP1LMIX_VOL3_MASK               0x00FE  /* DSP1LMIX_VOL3 - [7:1] */
#define WM2200_DSP1LMIX_VOL3_SHIFT                   1  /* DSP1LMIX_VOL3 - [7:1] */
#define WM2200_DSP1LMIX_VOL3_WIDTH                   7  /* DSP1LMIX_VOL3 - [7:1] */

/*
 * R1654 (0x676) - DSP1LMIX Input 4 Source
 */
#define WM2200_DSP1LMIX_SRC4_MASK               0x007F  /* DSP1LMIX_SRC4 - [6:0] */
#define WM2200_DSP1LMIX_SRC4_SHIFT                   0  /* DSP1LMIX_SRC4 - [6:0] */
#define WM2200_DSP1LMIX_SRC4_WIDTH                   7  /* DSP1LMIX_SRC4 - [6:0] */

/*
 * R1655 (0x677) - DSP1LMIX Input 4 Volume
 */
#define WM2200_DSP1LMIX_VOL4_MASK               0x00FE  /* DSP1LMIX_VOL4 - [7:1] */
#define WM2200_DSP1LMIX_VOL4_SHIFT                   1  /* DSP1LMIX_VOL4 - [7:1] */
#define WM2200_DSP1LMIX_VOL4_WIDTH                   7  /* DSP1LMIX_VOL4 - [7:1] */

/*
 * R1656 (0x678) - DSP1RMIX Input 1 Source
 */
#define WM2200_DSP1RMIX_SRC1_MASK               0x007F  /* DSP1RMIX_SRC1 - [6:0] */
#define WM2200_DSP1RMIX_SRC1_SHIFT                   0  /* DSP1RMIX_SRC1 - [6:0] */
#define WM2200_DSP1RMIX_SRC1_WIDTH                   7  /* DSP1RMIX_SRC1 - [6:0] */

/*
 * R1657 (0x679) - DSP1RMIX Input 1 Volume
 */
#define WM2200_DSP1RMIX_VOL1_MASK               0x00FE  /* DSP1RMIX_VOL1 - [7:1] */
#define WM2200_DSP1RMIX_VOL1_SHIFT                   1  /* DSP1RMIX_VOL1 - [7:1] */
#define WM2200_DSP1RMIX_VOL1_WIDTH                   7  /* DSP1RMIX_VOL1 - [7:1] */

/*
 * R1658 (0x67A) - DSP1RMIX Input 2 Source
 */
#define WM2200_DSP1RMIX_SRC2_MASK               0x007F  /* DSP1RMIX_SRC2 - [6:0] */
#define WM2200_DSP1RMIX_SRC2_SHIFT                   0  /* DSP1RMIX_SRC2 - [6:0] */
#define WM2200_DSP1RMIX_SRC2_WIDTH                   7  /* DSP1RMIX_SRC2 - [6:0] */

/*
 * R1659 (0x67B) - DSP1RMIX Input 2 Volume
 */
#define WM2200_DSP1RMIX_VOL2_MASK               0x00FE  /* DSP1RMIX_VOL2 - [7:1] */
#define WM2200_DSP1RMIX_VOL2_SHIFT                   1  /* DSP1RMIX_VOL2 - [7:1] */
#define WM2200_DSP1RMIX_VOL2_WIDTH                   7  /* DSP1RMIX_VOL2 - [7:1] */

/*
 * R1660 (0x67C) - DSP1RMIX Input 3 Source
 */
#define WM2200_DSP1RMIX_SRC3_MASK               0x007F  /* DSP1RMIX_SRC3 - [6:0] */
#define WM2200_DSP1RMIX_SRC3_SHIFT                   0  /* DSP1RMIX_SRC3 - [6:0] */
#define WM2200_DSP1RMIX_SRC3_WIDTH                   7  /* DSP1RMIX_SRC3 - [6:0] */

/*
 * R1661 (0x67D) - DSP1RMIX Input 3 Volume
 */
#define WM2200_DSP1RMIX_VOL3_MASK               0x00FE  /* DSP1RMIX_VOL3 - [7:1] */
#define WM2200_DSP1RMIX_VOL3_SHIFT                   1  /* DSP1RMIX_VOL3 - [7:1] */
#define WM2200_DSP1RMIX_VOL3_WIDTH                   7  /* DSP1RMIX_VOL3 - [7:1] */

/*
 * R1662 (0x67E) - DSP1RMIX Input 4 Source
 */
#define WM2200_DSP1RMIX_SRC4_MASK               0x007F  /* DSP1RMIX_SRC4 - [6:0] */
#define WM2200_DSP1RMIX_SRC4_SHIFT                   0  /* DSP1RMIX_SRC4 - [6:0] */
#define WM2200_DSP1RMIX_SRC4_WIDTH                   7  /* DSP1RMIX_SRC4 - [6:0] */

/*
 * R1663 (0x67F) - DSP1RMIX Input 4 Volume
 */
#define WM2200_DSP1RMIX_VOL4_MASK               0x00FE  /* DSP1RMIX_VOL4 - [7:1] */
#define WM2200_DSP1RMIX_VOL4_SHIFT                   1  /* DSP1RMIX_VOL4 - [7:1] */
#define WM2200_DSP1RMIX_VOL4_WIDTH                   7  /* DSP1RMIX_VOL4 - [7:1] */

/*
 * R1664 (0x680) - DSP1AUX1MIX Input 1 Source
 */
#define WM2200_DSP1AUX1MIX_SRC1_MASK            0x007F  /* DSP1AUX1MIX_SRC1 - [6:0] */
#define WM2200_DSP1AUX1MIX_SRC1_SHIFT                0  /* DSP1AUX1MIX_SRC1 - [6:0] */
#define WM2200_DSP1AUX1MIX_SRC1_WIDTH                7  /* DSP1AUX1MIX_SRC1 - [6:0] */

/*
 * R1665 (0x681) - DSP1AUX2MIX Input 1 Source
 */
#define WM2200_DSP1AUX2MIX_SRC1_MASK            0x007F  /* DSP1AUX2MIX_SRC1 - [6:0] */
#define WM2200_DSP1AUX2MIX_SRC1_SHIFT                0  /* DSP1AUX2MIX_SRC1 - [6:0] */
#define WM2200_DSP1AUX2MIX_SRC1_WIDTH                7  /* DSP1AUX2MIX_SRC1 - [6:0] */

/*
 * R1666 (0x682) - DSP1AUX3MIX Input 1 Source
 */
#define WM2200_DSP1AUX3MIX_SRC1_MASK            0x007F  /* DSP1AUX3MIX_SRC1 - [6:0] */
#define WM2200_DSP1AUX3MIX_SRC1_SHIFT                0  /* DSP1AUX3MIX_SRC1 - [6:0] */
#define WM2200_DSP1AUX3MIX_SRC1_WIDTH                7  /* DSP1AUX3MIX_SRC1 - [6:0] */

/*
 * R1667 (0x683) - DSP1AUX4MIX Input 1 Source
 */
#define WM2200_DSP1AUX4MIX_SRC1_MASK            0x007F  /* DSP1AUX4MIX_SRC1 - [6:0] */
#define WM2200_DSP1AUX4MIX_SRC1_SHIFT                0  /* DSP1AUX4MIX_SRC1 - [6:0] */
#define WM2200_DSP1AUX4MIX_SRC1_WIDTH                7  /* DSP1AUX4MIX_SRC1 - [6:0] */

/*
 * R1668 (0x684) - DSP1AUX5MIX Input 1 Source
 */
#define WM2200_DSP1AUX5MIX_SRC1_MASK            0x007F  /* DSP1AUX5MIX_SRC1 - [6:0] */
#define WM2200_DSP1AUX5MIX_SRC1_SHIFT                0  /* DSP1AUX5MIX_SRC1 - [6:0] */
#define WM2200_DSP1AUX5MIX_SRC1_WIDTH                7  /* DSP1AUX5MIX_SRC1 - [6:0] */

/*
 * R1669 (0x685) - DSP1AUX6MIX Input 1 Source
 */
#define WM2200_DSP1AUX6MIX_SRC1_MASK            0x007F  /* DSP1AUX6MIX_SRC1 - [6:0] */
#define WM2200_DSP1AUX6MIX_SRC1_SHIFT                0  /* DSP1AUX6MIX_SRC1 - [6:0] */
#define WM2200_DSP1AUX6MIX_SRC1_WIDTH                7  /* DSP1AUX6MIX_SRC1 - [6:0] */

/*
 * R1670 (0x686) - DSP2LMIX Input 1 Source
 */
#define WM2200_DSP2LMIX_SRC1_MASK               0x007F  /* DSP2LMIX_SRC1 - [6:0] */
#define WM2200_DSP2LMIX_SRC1_SHIFT                   0  /* DSP2LMIX_SRC1 - [6:0] */
#define WM2200_DSP2LMIX_SRC1_WIDTH                   7  /* DSP2LMIX_SRC1 - [6:0] */

/*
 * R1671 (0x687) - DSP2LMIX Input 1 Volume
 */
#define WM2200_DSP2LMIX_VOL1_MASK               0x00FE  /* DSP2LMIX_VOL1 - [7:1] */
#define WM2200_DSP2LMIX_VOL1_SHIFT                   1  /* DSP2LMIX_VOL1 - [7:1] */
#define WM2200_DSP2LMIX_VOL1_WIDTH                   7  /* DSP2LMIX_VOL1 - [7:1] */

/*
 * R1672 (0x688) - DSP2LMIX Input 2 Source
 */
#define WM2200_DSP2LMIX_SRC2_MASK               0x007F  /* DSP2LMIX_SRC2 - [6:0] */
#define WM2200_DSP2LMIX_SRC2_SHIFT                   0  /* DSP2LMIX_SRC2 - [6:0] */
#define WM2200_DSP2LMIX_SRC2_WIDTH                   7  /* DSP2LMIX_SRC2 - [6:0] */

/*
 * R1673 (0x689) - DSP2LMIX Input 2 Volume
 */
#define WM2200_DSP2LMIX_VOL2_MASK               0x00FE  /* DSP2LMIX_VOL2 - [7:1] */
#define WM2200_DSP2LMIX_VOL2_SHIFT                   1  /* DSP2LMIX_VOL2 - [7:1] */
#define WM2200_DSP2LMIX_VOL2_WIDTH                   7  /* DSP2LMIX_VOL2 - [7:1] */

/*
 * R1674 (0x68A) - DSP2LMIX Input 3 Source
 */
#define WM2200_DSP2LMIX_SRC3_MASK               0x007F  /* DSP2LMIX_SRC3 - [6:0] */
#define WM2200_DSP2LMIX_SRC3_SHIFT                   0  /* DSP2LMIX_SRC3 - [6:0] */
#define WM2200_DSP2LMIX_SRC3_WIDTH                   7  /* DSP2LMIX_SRC3 - [6:0] */

/*
 * R1675 (0x68B) - DSP2LMIX Input 3 Volume
 */
#define WM2200_DSP2LMIX_VOL3_MASK               0x00FE  /* DSP2LMIX_VOL3 - [7:1] */
#define WM2200_DSP2LMIX_VOL3_SHIFT                   1  /* DSP2LMIX_VOL3 - [7:1] */
#define WM2200_DSP2LMIX_VOL3_WIDTH                   7  /* DSP2LMIX_VOL3 - [7:1] */

/*
 * R1676 (0x68C) - DSP2LMIX Input 4 Source
 */
#define WM2200_DSP2LMIX_SRC4_MASK               0x007F  /* DSP2LMIX_SRC4 - [6:0] */
#define WM2200_DSP2LMIX_SRC4_SHIFT                   0  /* DSP2LMIX_SRC4 - [6:0] */
#define WM2200_DSP2LMIX_SRC4_WIDTH                   7  /* DSP2LMIX_SRC4 - [6:0] */

/*
 * R1677 (0x68D) - DSP2LMIX Input 4 Volume
 */
#define WM2200_DSP2LMIX_VOL4_MASK               0x00FE  /* DSP2LMIX_VOL4 - [7:1] */
#define WM2200_DSP2LMIX_VOL4_SHIFT                   1  /* DSP2LMIX_VOL4 - [7:1] */
#define WM2200_DSP2LMIX_VOL4_WIDTH                   7  /* DSP2LMIX_VOL4 - [7:1] */

/*
 * R1678 (0x68E) - DSP2RMIX Input 1 Source
 */
#define WM2200_DSP2RMIX_SRC1_MASK               0x007F  /* DSP2RMIX_SRC1 - [6:0] */
#define WM2200_DSP2RMIX_SRC1_SHIFT                   0  /* DSP2RMIX_SRC1 - [6:0] */
#define WM2200_DSP2RMIX_SRC1_WIDTH                   7  /* DSP2RMIX_SRC1 - [6:0] */

/*
 * R1679 (0x68F) - DSP2RMIX Input 1 Volume
 */
#define WM2200_DSP2RMIX_VOL1_MASK               0x00FE  /* DSP2RMIX_VOL1 - [7:1] */
#define WM2200_DSP2RMIX_VOL1_SHIFT                   1  /* DSP2RMIX_VOL1 - [7:1] */
#define WM2200_DSP2RMIX_VOL1_WIDTH                   7  /* DSP2RMIX_VOL1 - [7:1] */

/*
 * R1680 (0x690) - DSP2RMIX Input 2 Source
 */
#define WM2200_DSP2RMIX_SRC2_MASK               0x007F  /* DSP2RMIX_SRC2 - [6:0] */
#define WM2200_DSP2RMIX_SRC2_SHIFT                   0  /* DSP2RMIX_SRC2 - [6:0] */
#define WM2200_DSP2RMIX_SRC2_WIDTH                   7  /* DSP2RMIX_SRC2 - [6:0] */

/*
 * R1681 (0x691) - DSP2RMIX Input 2 Volume
 */
#define WM2200_DSP2RMIX_VOL2_MASK               0x00FE  /* DSP2RMIX_VOL2 - [7:1] */
#define WM2200_DSP2RMIX_VOL2_SHIFT                   1  /* DSP2RMIX_VOL2 - [7:1] */
#define WM2200_DSP2RMIX_VOL2_WIDTH                   7  /* DSP2RMIX_VOL2 - [7:1] */

/*
 * R1682 (0x692) - DSP2RMIX Input 3 Source
 */
#define WM2200_DSP2RMIX_SRC3_MASK               0x007F  /* DSP2RMIX_SRC3 - [6:0] */
#define WM2200_DSP2RMIX_SRC3_SHIFT                   0  /* DSP2RMIX_SRC3 - [6:0] */
#define WM2200_DSP2RMIX_SRC3_WIDTH                   7  /* DSP2RMIX_SRC3 - [6:0] */

/*
 * R1683 (0x693) - DSP2RMIX Input 3 Volume
 */
#define WM2200_DSP2RMIX_VOL3_MASK               0x00FE  /* DSP2RMIX_VOL3 - [7:1] */
#define WM2200_DSP2RMIX_VOL3_SHIFT                   1  /* DSP2RMIX_VOL3 - [7:1] */
#define WM2200_DSP2RMIX_VOL3_WIDTH                   7  /* DSP2RMIX_VOL3 - [7:1] */

/*
 * R1684 (0x694) - DSP2RMIX Input 4 Source
 */
#define WM2200_DSP2RMIX_SRC4_MASK               0x007F  /* DSP2RMIX_SRC4 - [6:0] */
#define WM2200_DSP2RMIX_SRC4_SHIFT                   0  /* DSP2RMIX_SRC4 - [6:0] */
#define WM2200_DSP2RMIX_SRC4_WIDTH                   7  /* DSP2RMIX_SRC4 - [6:0] */

/*
 * R1685 (0x695) - DSP2RMIX Input 4 Volume
 */
#define WM2200_DSP2RMIX_VOL4_MASK               0x00FE  /* DSP2RMIX_VOL4 - [7:1] */
#define WM2200_DSP2RMIX_VOL4_SHIFT                   1  /* DSP2RMIX_VOL4 - [7:1] */
#define WM2200_DSP2RMIX_VOL4_WIDTH                   7  /* DSP2RMIX_VOL4 - [7:1] */

/*
 * R1686 (0x696) - DSP2AUX1MIX Input 1 Source
 */
#define WM2200_DSP2AUX1MIX_SRC1_MASK            0x007F  /* DSP2AUX1MIX_SRC1 - [6:0] */
#define WM2200_DSP2AUX1MIX_SRC1_SHIFT                0  /* DSP2AUX1MIX_SRC1 - [6:0] */
#define WM2200_DSP2AUX1MIX_SRC1_WIDTH                7  /* DSP2AUX1MIX_SRC1 - [6:0] */

/*
 * R1687 (0x697) - DSP2AUX2MIX Input 1 Source
 */
#define WM2200_DSP2AUX2MIX_SRC1_MASK            0x007F  /* DSP2AUX2MIX_SRC1 - [6:0] */
#define WM2200_DSP2AUX2MIX_SRC1_SHIFT                0  /* DSP2AUX2MIX_SRC1 - [6:0] */
#define WM2200_DSP2AUX2MIX_SRC1_WIDTH                7  /* DSP2AUX2MIX_SRC1 - [6:0] */

/*
 * R1688 (0x698) - DSP2AUX3MIX Input 1 Source
 */
#define WM2200_DSP2AUX3MIX_SRC1_MASK            0x007F  /* DSP2AUX3MIX_SRC1 - [6:0] */
#define WM2200_DSP2AUX3MIX_SRC1_SHIFT                0  /* DSP2AUX3MIX_SRC1 - [6:0] */
#define WM2200_DSP2AUX3MIX_SRC1_WIDTH                7  /* DSP2AUX3MIX_SRC1 - [6:0] */

/*
 * R1689 (0x699) - DSP2AUX4MIX Input 1 Source
 */
#define WM2200_DSP2AUX4MIX_SRC1_MASK            0x007F  /* DSP2AUX4MIX_SRC1 - [6:0] */
#define WM2200_DSP2AUX4MIX_SRC1_SHIFT                0  /* DSP2AUX4MIX_SRC1 - [6:0] */
#define WM2200_DSP2AUX4MIX_SRC1_WIDTH                7  /* DSP2AUX4MIX_SRC1 - [6:0] */

/*
 * R1690 (0x69A) - DSP2AUX5MIX Input 1 Source
 */
#define WM2200_DSP2AUX5MIX_SRC1_MASK            0x007F  /* DSP2AUX5MIX_SRC1 - [6:0] */
#define WM2200_DSP2AUX5MIX_SRC1_SHIFT                0  /* DSP2AUX5MIX_SRC1 - [6:0] */
#define WM2200_DSP2AUX5MIX_SRC1_WIDTH                7  /* DSP2AUX5MIX_SRC1 - [6:0] */

/*
 * R1691 (0x69B) - DSP2AUX6MIX Input 1 Source
 */
#define WM2200_DSP2AUX6MIX_SRC1_MASK            0x007F  /* DSP2AUX6MIX_SRC1 - [6:0] */
#define WM2200_DSP2AUX6MIX_SRC1_SHIFT                0  /* DSP2AUX6MIX_SRC1 - [6:0] */
#define WM2200_DSP2AUX6MIX_SRC1_WIDTH                7  /* DSP2AUX6MIX_SRC1 - [6:0] */

/*
 * R1792 (0x700) - GPIO CTRL 1
 */
#define WM2200_GP1_DIR                          0x8000  /* GP1_DIR */
#define WM2200_GP1_DIR_MASK                     0x8000  /* GP1_DIR */
#define WM2200_GP1_DIR_SHIFT                        15  /* GP1_DIR */
#define WM2200_GP1_DIR_WIDTH                         1  /* GP1_DIR */
#define WM2200_GP1_PU                           0x4000  /* GP1_PU */
#define WM2200_GP1_PU_MASK                      0x4000  /* GP1_PU */
#define WM2200_GP1_PU_SHIFT                         14  /* GP1_PU */
#define WM2200_GP1_PU_WIDTH                          1  /* GP1_PU */
#define WM2200_GP1_PD                           0x2000  /* GP1_PD */
#define WM2200_GP1_PD_MASK                      0x2000  /* GP1_PD */
#define WM2200_GP1_PD_SHIFT                         13  /* GP1_PD */
#define WM2200_GP1_PD_WIDTH                          1  /* GP1_PD */
#define WM2200_GP1_POL                          0x0400  /* GP1_POL */
#define WM2200_GP1_POL_MASK                     0x0400  /* GP1_POL */
#define WM2200_GP1_POL_SHIFT                        10  /* GP1_POL */
#define WM2200_GP1_POL_WIDTH                         1  /* GP1_POL */
#define WM2200_GP1_OP_CFG                       0x0200  /* GP1_OP_CFG */
#define WM2200_GP1_OP_CFG_MASK                  0x0200  /* GP1_OP_CFG */
#define WM2200_GP1_OP_CFG_SHIFT                      9  /* GP1_OP_CFG */
#define WM2200_GP1_OP_CFG_WIDTH                      1  /* GP1_OP_CFG */
#define WM2200_GP1_DB                           0x0100  /* GP1_DB */
#define WM2200_GP1_DB_MASK                      0x0100  /* GP1_DB */
#define WM2200_GP1_DB_SHIFT                          8  /* GP1_DB */
#define WM2200_GP1_DB_WIDTH                          1  /* GP1_DB */
#define WM2200_GP1_LVL                          0x0040  /* GP1_LVL */
#define WM2200_GP1_LVL_MASK                     0x0040  /* GP1_LVL */
#define WM2200_GP1_LVL_SHIFT                         6  /* GP1_LVL */
#define WM2200_GP1_LVL_WIDTH                         1  /* GP1_LVL */
#define WM2200_GP1_FN_MASK                      0x003F  /* GP1_FN - [5:0] */
#define WM2200_GP1_FN_SHIFT                          0  /* GP1_FN - [5:0] */
#define WM2200_GP1_FN_WIDTH                          6  /* GP1_FN - [5:0] */

/*
 * R1793 (0x701) - GPIO CTRL 2
 */
#define WM2200_GP2_DIR                          0x8000  /* GP2_DIR */
#define WM2200_GP2_DIR_MASK                     0x8000  /* GP2_DIR */
#define WM2200_GP2_DIR_SHIFT                        15  /* GP2_DIR */
#define WM2200_GP2_DIR_WIDTH                         1  /* GP2_DIR */
#define WM2200_GP2_PU                           0x4000  /* GP2_PU */
#define WM2200_GP2_PU_MASK                      0x4000  /* GP2_PU */
#define WM2200_GP2_PU_SHIFT                         14  /* GP2_PU */
#define WM2200_GP2_PU_WIDTH                          1  /* GP2_PU */
#define WM2200_GP2_PD                           0x2000  /* GP2_PD */
#define WM2200_GP2_PD_MASK                      0x2000  /* GP2_PD */
#define WM2200_GP2_PD_SHIFT                         13  /* GP2_PD */
#define WM2200_GP2_PD_WIDTH                          1  /* GP2_PD */
#define WM2200_GP2_POL                          0x0400  /* GP2_POL */
#define WM2200_GP2_POL_MASK                     0x0400  /* GP2_POL */
#define WM2200_GP2_POL_SHIFT                        10  /* GP2_POL */
#define WM2200_GP2_POL_WIDTH                         1  /* GP2_POL */
#define WM2200_GP2_OP_CFG                       0x0200  /* GP2_OP_CFG */
#define WM2200_GP2_OP_CFG_MASK                  0x0200  /* GP2_OP_CFG */
#define WM2200_GP2_OP_CFG_SHIFT                      9  /* GP2_OP_CFG */
#define WM2200_GP2_OP_CFG_WIDTH                      1  /* GP2_OP_CFG */
#define WM2200_GP2_DB                           0x0100  /* GP2_DB */
#define WM2200_GP2_DB_MASK                      0x0100  /* GP2_DB */
#define WM2200_GP2_DB_SHIFT                          8  /* GP2_DB */
#define WM2200_GP2_DB_WIDTH                          1  /* GP2_DB */
#define WM2200_GP2_LVL                          0x0040  /* GP2_LVL */
#define WM2200_GP2_LVL_MASK                     0x0040  /* GP2_LVL */
#define WM2200_GP2_LVL_SHIFT                         6  /* GP2_LVL */
#define WM2200_GP2_LVL_WIDTH                         1  /* GP2_LVL */
#define WM2200_GP2_FN_MASK                      0x003F  /* GP2_FN - [5:0] */
#define WM2200_GP2_FN_SHIFT                          0  /* GP2_FN - [5:0] */
#define WM2200_GP2_FN_WIDTH                          6  /* GP2_FN - [5:0] */

/*
 * R1794 (0x702) - GPIO CTRL 3
 */
#define WM2200_GP3_DIR                          0x8000  /* GP3_DIR */
#define WM2200_GP3_DIR_MASK                     0x8000  /* GP3_DIR */
#define WM2200_GP3_DIR_SHIFT                        15  /* GP3_DIR */
#define WM2200_GP3_DIR_WIDTH                         1  /* GP3_DIR */
#define WM2200_GP3_PU                           0x4000  /* GP3_PU */
#define WM2200_GP3_PU_MASK                      0x4000  /* GP3_PU */
#define WM2200_GP3_PU_SHIFT                         14  /* GP3_PU */
#define WM2200_GP3_PU_WIDTH                          1  /* GP3_PU */
#define WM2200_GP3_PD                           0x2000  /* GP3_PD */
#define WM2200_GP3_PD_MASK                      0x2000  /* GP3_PD */
#define WM2200_GP3_PD_SHIFT                         13  /* GP3_PD */
#define WM2200_GP3_PD_WIDTH                          1  /* GP3_PD */
#define WM2200_GP3_POL                          0x0400  /* GP3_POL */
#define WM2200_GP3_POL_MASK                     0x0400  /* GP3_POL */
#define WM2200_GP3_POL_SHIFT                        10  /* GP3_POL */
#define WM2200_GP3_POL_WIDTH                         1  /* GP3_POL */
#define WM2200_GP3_OP_CFG                       0x0200  /* GP3_OP_CFG */
#define WM2200_GP3_OP_CFG_MASK                  0x0200  /* GP3_OP_CFG */
#define WM2200_GP3_OP_CFG_SHIFT                      9  /* GP3_OP_CFG */
#define WM2200_GP3_OP_CFG_WIDTH                      1  /* GP3_OP_CFG */
#define WM2200_GP3_DB                           0x0100  /* GP3_DB */
#define WM2200_GP3_DB_MASK                      0x0100  /* GP3_DB */
#define WM2200_GP3_DB_SHIFT                          8  /* GP3_DB */
#define WM2200_GP3_DB_WIDTH                          1  /* GP3_DB */
#define WM2200_GP3_LVL                          0x0040  /* GP3_LVL */
#define WM2200_GP3_LVL_MASK                     0x0040  /* GP3_LVL */
#define WM2200_GP3_LVL_SHIFT                         6  /* GP3_LVL */
#define WM2200_GP3_LVL_WIDTH                         1  /* GP3_LVL */
#define WM2200_GP3_FN_MASK                      0x003F  /* GP3_FN - [5:0] */
#define WM2200_GP3_FN_SHIFT                          0  /* GP3_FN - [5:0] */
#define WM2200_GP3_FN_WIDTH                          6  /* GP3_FN - [5:0] */

/*
 * R1795 (0x703) - GPIO CTRL 4
 */
#define WM2200_GP4_DIR                          0x8000  /* GP4_DIR */
#define WM2200_GP4_DIR_MASK                     0x8000  /* GP4_DIR */
#define WM2200_GP4_DIR_SHIFT                        15  /* GP4_DIR */
#define WM2200_GP4_DIR_WIDTH                         1  /* GP4_DIR */
#define WM2200_GP4_PU                           0x4000  /* GP4_PU */
#define WM2200_GP4_PU_MASK                      0x4000  /* GP4_PU */
#define WM2200_GP4_PU_SHIFT                         14  /* GP4_PU */
#define WM2200_GP4_PU_WIDTH                          1  /* GP4_PU */
#define WM2200_GP4_PD                           0x2000  /* GP4_PD */
#define WM2200_GP4_PD_MASK                      0x2000  /* GP4_PD */
#define WM2200_GP4_PD_SHIFT                         13  /* GP4_PD */
#define WM2200_GP4_PD_WIDTH                          1  /* GP4_PD */
#define WM2200_GP4_POL                          0x0400  /* GP4_POL */
#define WM2200_GP4_POL_MASK                     0x0400  /* GP4_POL */
#define WM2200_GP4_POL_SHIFT                        10  /* GP4_POL */
#define WM2200_GP4_POL_WIDTH                         1  /* GP4_POL */
#define WM2200_GP4_OP_CFG                       0x0200  /* GP4_OP_CFG */
#define WM2200_GP4_OP_CFG_MASK                  0x0200  /* GP4_OP_CFG */
#define WM2200_GP4_OP_CFG_SHIFT                      9  /* GP4_OP_CFG */
#define WM2200_GP4_OP_CFG_WIDTH                      1  /* GP4_OP_CFG */
#define WM2200_GP4_DB                           0x0100  /* GP4_DB */
#define WM2200_GP4_DB_MASK                      0x0100  /* GP4_DB */
#define WM2200_GP4_DB_SHIFT                          8  /* GP4_DB */
#define WM2200_GP4_DB_WIDTH                          1  /* GP4_DB */
#define WM2200_GP4_LVL                          0x0040  /* GP4_LVL */
#define WM2200_GP4_LVL_MASK                     0x0040  /* GP4_LVL */
#define WM2200_GP4_LVL_SHIFT                         6  /* GP4_LVL */
#define WM2200_GP4_LVL_WIDTH                         1  /* GP4_LVL */
#define WM2200_GP4_FN_MASK                      0x003F  /* GP4_FN - [5:0] */
#define WM2200_GP4_FN_SHIFT                          0  /* GP4_FN - [5:0] */
#define WM2200_GP4_FN_WIDTH                          6  /* GP4_FN - [5:0] */

/*
 * R1799 (0x707) - ADPS1 IRQ0
 */
#define WM2200_DSP_IRQ1                         0x0002  /* DSP_IRQ1 */
#define WM2200_DSP_IRQ1_MASK                    0x0002  /* DSP_IRQ1 */
#define WM2200_DSP_IRQ1_SHIFT                        1  /* DSP_IRQ1 */
#define WM2200_DSP_IRQ1_WIDTH                        1  /* DSP_IRQ1 */
#define WM2200_DSP_IRQ0                         0x0001  /* DSP_IRQ0 */
#define WM2200_DSP_IRQ0_MASK                    0x0001  /* DSP_IRQ0 */
#define WM2200_DSP_IRQ0_SHIFT                        0  /* DSP_IRQ0 */
#define WM2200_DSP_IRQ0_WIDTH                        1  /* DSP_IRQ0 */

/*
 * R1800 (0x708) - ADPS1 IRQ1
 */
#define WM2200_DSP_IRQ3                         0x0002  /* DSP_IRQ3 */
#define WM2200_DSP_IRQ3_MASK                    0x0002  /* DSP_IRQ3 */
#define WM2200_DSP_IRQ3_SHIFT                        1  /* DSP_IRQ3 */
#define WM2200_DSP_IRQ3_WIDTH                        1  /* DSP_IRQ3 */
#define WM2200_DSP_IRQ2                         0x0001  /* DSP_IRQ2 */
#define WM2200_DSP_IRQ2_MASK                    0x0001  /* DSP_IRQ2 */
#define WM2200_DSP_IRQ2_SHIFT                        0  /* DSP_IRQ2 */
#define WM2200_DSP_IRQ2_WIDTH                        1  /* DSP_IRQ2 */

/*
 * R1801 (0x709) - Misc Pad Ctrl 1
 */
#define WM2200_LDO1ENA_PD                       0x8000  /* LDO1ENA_PD */
#define WM2200_LDO1ENA_PD_MASK                  0x8000  /* LDO1ENA_PD */
#define WM2200_LDO1ENA_PD_SHIFT                     15  /* LDO1ENA_PD */
#define WM2200_LDO1ENA_PD_WIDTH                      1  /* LDO1ENA_PD */
#define WM2200_MCLK2_PD                         0x2000  /* MCLK2_PD */
#define WM2200_MCLK2_PD_MASK                    0x2000  /* MCLK2_PD */
#define WM2200_MCLK2_PD_SHIFT                       13  /* MCLK2_PD */
#define WM2200_MCLK2_PD_WIDTH                        1  /* MCLK2_PD */
#define WM2200_MCLK1_PD                         0x1000  /* MCLK1_PD */
#define WM2200_MCLK1_PD_MASK                    0x1000  /* MCLK1_PD */
#define WM2200_MCLK1_PD_SHIFT                       12  /* MCLK1_PD */
#define WM2200_MCLK1_PD_WIDTH                        1  /* MCLK1_PD */
#define WM2200_DACLRCLK1_PU                     0x0400  /* DACLRCLK1_PU */
#define WM2200_DACLRCLK1_PU_MASK                0x0400  /* DACLRCLK1_PU */
#define WM2200_DACLRCLK1_PU_SHIFT                   10  /* DACLRCLK1_PU */
#define WM2200_DACLRCLK1_PU_WIDTH                    1  /* DACLRCLK1_PU */
#define WM2200_DACLRCLK1_PD                     0x0200  /* DACLRCLK1_PD */
#define WM2200_DACLRCLK1_PD_MASK                0x0200  /* DACLRCLK1_PD */
#define WM2200_DACLRCLK1_PD_SHIFT                    9  /* DACLRCLK1_PD */
#define WM2200_DACLRCLK1_PD_WIDTH                    1  /* DACLRCLK1_PD */
#define WM2200_BCLK1_PU                         0x0100  /* BCLK1_PU */
#define WM2200_BCLK1_PU_MASK                    0x0100  /* BCLK1_PU */
#define WM2200_BCLK1_PU_SHIFT                        8  /* BCLK1_PU */
#define WM2200_BCLK1_PU_WIDTH                        1  /* BCLK1_PU */
#define WM2200_BCLK1_PD                         0x0080  /* BCLK1_PD */
#define WM2200_BCLK1_PD_MASK                    0x0080  /* BCLK1_PD */
#define WM2200_BCLK1_PD_SHIFT                        7  /* BCLK1_PD */
#define WM2200_BCLK1_PD_WIDTH                        1  /* BCLK1_PD */
#define WM2200_DACDAT1_PU                       0x0040  /* DACDAT1_PU */
#define WM2200_DACDAT1_PU_MASK                  0x0040  /* DACDAT1_PU */
#define WM2200_DACDAT1_PU_SHIFT                      6  /* DACDAT1_PU */
#define WM2200_DACDAT1_PU_WIDTH                      1  /* DACDAT1_PU */
#define WM2200_DACDAT1_PD                       0x0020  /* DACDAT1_PD */
#define WM2200_DACDAT1_PD_MASK                  0x0020  /* DACDAT1_PD */
#define WM2200_DACDAT1_PD_SHIFT                      5  /* DACDAT1_PD */
#define WM2200_DACDAT1_PD_WIDTH                      1  /* DACDAT1_PD */
#define WM2200_DMICDAT3_PD                      0x0010  /* DMICDAT3_PD */
#define WM2200_DMICDAT3_PD_MASK                 0x0010  /* DMICDAT3_PD */
#define WM2200_DMICDAT3_PD_SHIFT                     4  /* DMICDAT3_PD */
#define WM2200_DMICDAT3_PD_WIDTH                     1  /* DMICDAT3_PD */
#define WM2200_DMICDAT2_PD                      0x0008  /* DMICDAT2_PD */
#define WM2200_DMICDAT2_PD_MASK                 0x0008  /* DMICDAT2_PD */
#define WM2200_DMICDAT2_PD_SHIFT                     3  /* DMICDAT2_PD */
#define WM2200_DMICDAT2_PD_WIDTH                     1  /* DMICDAT2_PD */
#define WM2200_DMICDAT1_PD                      0x0004  /* DMICDAT1_PD */
#define WM2200_DMICDAT1_PD_MASK                 0x0004  /* DMICDAT1_PD */
#define WM2200_DMICDAT1_PD_SHIFT                     2  /* DMICDAT1_PD */
#define WM2200_DMICDAT1_PD_WIDTH                     1  /* DMICDAT1_PD */
#define WM2200_RSTB_PU                          0x0002  /* RSTB_PU */
#define WM2200_RSTB_PU_MASK                     0x0002  /* RSTB_PU */
#define WM2200_RSTB_PU_SHIFT                         1  /* RSTB_PU */
#define WM2200_RSTB_PU_WIDTH                         1  /* RSTB_PU */
#define WM2200_ADDR_PD                          0x0001  /* ADDR_PD */
#define WM2200_ADDR_PD_MASK                     0x0001  /* ADDR_PD */
#define WM2200_ADDR_PD_SHIFT                         0  /* ADDR_PD */
#define WM2200_ADDR_PD_WIDTH                         1  /* ADDR_PD */

/*
 * R2048 (0x800) - Interrupt Status 1
 */
#define WM2200_DSP_IRQ0_EINT                    0x0080  /* DSP_IRQ0_EINT */
#define WM2200_DSP_IRQ0_EINT_MASK               0x0080  /* DSP_IRQ0_EINT */
#define WM2200_DSP_IRQ0_EINT_SHIFT                   7  /* DSP_IRQ0_EINT */
#define WM2200_DSP_IRQ0_EINT_WIDTH                   1  /* DSP_IRQ0_EINT */
#define WM2200_DSP_IRQ1_EINT                    0x0040  /* DSP_IRQ1_EINT */
#define WM2200_DSP_IRQ1_EINT_MASK               0x0040  /* DSP_IRQ1_EINT */
#define WM2200_DSP_IRQ1_EINT_SHIFT                   6  /* DSP_IRQ1_EINT */
#define WM2200_DSP_IRQ1_EINT_WIDTH                   1  /* DSP_IRQ1_EINT */
#define WM2200_DSP_IRQ2_EINT                    0x0020  /* DSP_IRQ2_EINT */
#define WM2200_DSP_IRQ2_EINT_MASK               0x0020  /* DSP_IRQ2_EINT */
#define WM2200_DSP_IRQ2_EINT_SHIFT                   5  /* DSP_IRQ2_EINT */
#define WM2200_DSP_IRQ2_EINT_WIDTH                   1  /* DSP_IRQ2_EINT */
#define WM2200_DSP_IRQ3_EINT                    0x0010  /* DSP_IRQ3_EINT */
#define WM2200_DSP_IRQ3_EINT_MASK               0x0010  /* DSP_IRQ3_EINT */
#define WM2200_DSP_IRQ3_EINT_SHIFT                   4  /* DSP_IRQ3_EINT */
#define WM2200_DSP_IRQ3_EINT_WIDTH                   1  /* DSP_IRQ3_EINT */
#define WM2200_GP4_EINT                         0x0008  /* GP4_EINT */
#define WM2200_GP4_EINT_MASK                    0x0008  /* GP4_EINT */
#define WM2200_GP4_EINT_SHIFT                        3  /* GP4_EINT */
#define WM2200_GP4_EINT_WIDTH                        1  /* GP4_EINT */
#define WM2200_GP3_EINT                         0x0004  /* GP3_EINT */
#define WM2200_GP3_EINT_MASK                    0x0004  /* GP3_EINT */
#define WM2200_GP3_EINT_SHIFT                        2  /* GP3_EINT */
#define WM2200_GP3_EINT_WIDTH                        1  /* GP3_EINT */
#define WM2200_GP2_EINT                         0x0002  /* GP2_EINT */
#define WM2200_GP2_EINT_MASK                    0x0002  /* GP2_EINT */
#define WM2200_GP2_EINT_SHIFT                        1  /* GP2_EINT */
#define WM2200_GP2_EINT_WIDTH                        1  /* GP2_EINT */
#define WM2200_GP1_EINT                         0x0001  /* GP1_EINT */
#define WM2200_GP1_EINT_MASK                    0x0001  /* GP1_EINT */
#define WM2200_GP1_EINT_SHIFT                        0  /* GP1_EINT */
#define WM2200_GP1_EINT_WIDTH                        1  /* GP1_EINT */

/*
 * R2049 (0x801) - Interrupt Status 1 Mask
 */
#define WM2200_IM_DSP_IRQ0_EINT                 0x0080  /* IM_DSP_IRQ0_EINT */
#define WM2200_IM_DSP_IRQ0_EINT_MASK            0x0080  /* IM_DSP_IRQ0_EINT */
#define WM2200_IM_DSP_IRQ0_EINT_SHIFT                7  /* IM_DSP_IRQ0_EINT */
#define WM2200_IM_DSP_IRQ0_EINT_WIDTH                1  /* IM_DSP_IRQ0_EINT */
#define WM2200_IM_DSP_IRQ1_EINT                 0x0040  /* IM_DSP_IRQ1_EINT */
#define WM2200_IM_DSP_IRQ1_EINT_MASK            0x0040  /* IM_DSP_IRQ1_EINT */
#define WM2200_IM_DSP_IRQ1_EINT_SHIFT                6  /* IM_DSP_IRQ1_EINT */
#define WM2200_IM_DSP_IRQ1_EINT_WIDTH                1  /* IM_DSP_IRQ1_EINT */
#define WM2200_IM_DSP_IRQ2_EINT                 0x0020  /* IM_DSP_IRQ2_EINT */
#define WM2200_IM_DSP_IRQ2_EINT_MASK            0x0020  /* IM_DSP_IRQ2_EINT */
#define WM2200_IM_DSP_IRQ2_EINT_SHIFT                5  /* IM_DSP_IRQ2_EINT */
#define WM2200_IM_DSP_IRQ2_EINT_WIDTH                1  /* IM_DSP_IRQ2_EINT */
#define WM2200_IM_DSP_IRQ3_EINT                 0x0010  /* IM_DSP_IRQ3_EINT */
#define WM2200_IM_DSP_IRQ3_EINT_MASK            0x0010  /* IM_DSP_IRQ3_EINT */
#define WM2200_IM_DSP_IRQ3_EINT_SHIFT                4  /* IM_DSP_IRQ3_EINT */
#define WM2200_IM_DSP_IRQ3_EINT_WIDTH                1  /* IM_DSP_IRQ3_EINT */
#define WM2200_IM_GP4_EINT                      0x0008  /* IM_GP4_EINT */
#define WM2200_IM_GP4_EINT_MASK                 0x0008  /* IM_GP4_EINT */
#define WM2200_IM_GP4_EINT_SHIFT                     3  /* IM_GP4_EINT */
#define WM2200_IM_GP4_EINT_WIDTH                     1  /* IM_GP4_EINT */
#define WM2200_IM_GP3_EINT                      0x0004  /* IM_GP3_EINT */
#define WM2200_IM_GP3_EINT_MASK                 0x0004  /* IM_GP3_EINT */
#define WM2200_IM_GP3_EINT_SHIFT                     2  /* IM_GP3_EINT */
#define WM2200_IM_GP3_EINT_WIDTH                     1  /* IM_GP3_EINT */
#define WM2200_IM_GP2_EINT                      0x0002  /* IM_GP2_EINT */
#define WM2200_IM_GP2_EINT_MASK                 0x0002  /* IM_GP2_EINT */
#define WM2200_IM_GP2_EINT_SHIFT                     1  /* IM_GP2_EINT */
#define WM2200_IM_GP2_EINT_WIDTH                     1  /* IM_GP2_EINT */
#define WM2200_IM_GP1_EINT                      0x0001  /* IM_GP1_EINT */
#define WM2200_IM_GP1_EINT_MASK                 0x0001  /* IM_GP1_EINT */
#define WM2200_IM_GP1_EINT_SHIFT                     0  /* IM_GP1_EINT */
#define WM2200_IM_GP1_EINT_WIDTH                     1  /* IM_GP1_EINT */

/*
 * R2050 (0x802) - Interrupt Status 2
 */
#define WM2200_WSEQ_BUSY_EINT                   0x0100  /* WSEQ_BUSY_EINT */
#define WM2200_WSEQ_BUSY_EINT_MASK              0x0100  /* WSEQ_BUSY_EINT */
#define WM2200_WSEQ_BUSY_EINT_SHIFT                  8  /* WSEQ_BUSY_EINT */
#define WM2200_WSEQ_BUSY_EINT_WIDTH                  1  /* WSEQ_BUSY_EINT */
#define WM2200_FLL_LOCK_EINT                    0x0002  /* FLL_LOCK_EINT */
#define WM2200_FLL_LOCK_EINT_MASK               0x0002  /* FLL_LOCK_EINT */
#define WM2200_FLL_LOCK_EINT_SHIFT                   1  /* FLL_LOCK_EINT */
#define WM2200_FLL_LOCK_EINT_WIDTH                   1  /* FLL_LOCK_EINT */
#define WM2200_CLKGEN_EINT                      0x0001  /* CLKGEN_EINT */
#define WM2200_CLKGEN_EINT_MASK                 0x0001  /* CLKGEN_EINT */
#define WM2200_CLKGEN_EINT_SHIFT                     0  /* CLKGEN_EINT */
#define WM2200_CLKGEN_EINT_WIDTH                     1  /* CLKGEN_EINT */

/*
 * R2051 (0x803) - Interrupt Raw Status 2
 */
#define WM2200_WSEQ_BUSY_STS                    0x0100  /* WSEQ_BUSY_STS */
#define WM2200_WSEQ_BUSY_STS_MASK               0x0100  /* WSEQ_BUSY_STS */
#define WM2200_WSEQ_BUSY_STS_SHIFT                   8  /* WSEQ_BUSY_STS */
#define WM2200_WSEQ_BUSY_STS_WIDTH                   1  /* WSEQ_BUSY_STS */
#define WM2200_FLL_LOCK_STS                     0x0002  /* FLL_LOCK_STS */
#define WM2200_FLL_LOCK_STS_MASK                0x0002  /* FLL_LOCK_STS */
#define WM2200_FLL_LOCK_STS_SHIFT                    1  /* FLL_LOCK_STS */
#define WM2200_FLL_LOCK_STS_WIDTH                    1  /* FLL_LOCK_STS */
#define WM2200_CLKGEN_STS                       0x0001  /* CLKGEN_STS */
#define WM2200_CLKGEN_STS_MASK                  0x0001  /* CLKGEN_STS */
#define WM2200_CLKGEN_STS_SHIFT                      0  /* CLKGEN_STS */
#define WM2200_CLKGEN_STS_WIDTH                      1  /* CLKGEN_STS */

/*
 * R2052 (0x804) - Interrupt Status 2 Mask
 */
#define WM2200_IM_WSEQ_BUSY_EINT                0x0100  /* IM_WSEQ_BUSY_EINT */
#define WM2200_IM_WSEQ_BUSY_EINT_MASK           0x0100  /* IM_WSEQ_BUSY_EINT */
#define WM2200_IM_WSEQ_BUSY_EINT_SHIFT               8  /* IM_WSEQ_BUSY_EINT */
#define WM2200_IM_WSEQ_BUSY_EINT_WIDTH               1  /* IM_WSEQ_BUSY_EINT */
#define WM2200_IM_FLL_LOCK_EINT                 0x0002  /* IM_FLL_LOCK_EINT */
#define WM2200_IM_FLL_LOCK_EINT_MASK            0x0002  /* IM_FLL_LOCK_EINT */
#define WM2200_IM_FLL_LOCK_EINT_SHIFT                1  /* IM_FLL_LOCK_EINT */
#define WM2200_IM_FLL_LOCK_EINT_WIDTH                1  /* IM_FLL_LOCK_EINT */
#define WM2200_IM_CLKGEN_EINT                   0x0001  /* IM_CLKGEN_EINT */
#define WM2200_IM_CLKGEN_EINT_MASK              0x0001  /* IM_CLKGEN_EINT */
#define WM2200_IM_CLKGEN_EINT_SHIFT                  0  /* IM_CLKGEN_EINT */
#define WM2200_IM_CLKGEN_EINT_WIDTH                  1  /* IM_CLKGEN_EINT */

/*
 * R2056 (0x808) - Interrupt Control
 */
#define WM2200_IM_IRQ                           0x0001  /* IM_IRQ */
#define WM2200_IM_IRQ_MASK                      0x0001  /* IM_IRQ */
#define WM2200_IM_IRQ_SHIFT                          0  /* IM_IRQ */
#define WM2200_IM_IRQ_WIDTH                          1  /* IM_IRQ */

/*
 * R2304 (0x900) - EQL_1
 */
#define WM2200_EQL_B1_GAIN_MASK                 0xF800  /* EQL_B1_GAIN - [15:11] */
#define WM2200_EQL_B1_GAIN_SHIFT                    11  /* EQL_B1_GAIN - [15:11] */
#define WM2200_EQL_B1_GAIN_WIDTH                     5  /* EQL_B1_GAIN - [15:11] */
#define WM2200_EQL_B2_GAIN_MASK                 0x07C0  /* EQL_B2_GAIN - [10:6] */
#define WM2200_EQL_B2_GAIN_SHIFT                     6  /* EQL_B2_GAIN - [10:6] */
#define WM2200_EQL_B2_GAIN_WIDTH                     5  /* EQL_B2_GAIN - [10:6] */
#define WM2200_EQL_B3_GAIN_MASK                 0x003E  /* EQL_B3_GAIN - [5:1] */
#define WM2200_EQL_B3_GAIN_SHIFT                     1  /* EQL_B3_GAIN - [5:1] */
#define WM2200_EQL_B3_GAIN_WIDTH                     5  /* EQL_B3_GAIN - [5:1] */
#define WM2200_EQL_ENA                          0x0001  /* EQL_ENA */
#define WM2200_EQL_ENA_MASK                     0x0001  /* EQL_ENA */
#define WM2200_EQL_ENA_SHIFT                         0  /* EQL_ENA */
#define WM2200_EQL_ENA_WIDTH                         1  /* EQL_ENA */

/*
 * R2305 (0x901) - EQL_2
 */
#define WM2200_EQL_B4_GAIN_MASK                 0xF800  /* EQL_B4_GAIN - [15:11] */
#define WM2200_EQL_B4_GAIN_SHIFT                    11  /* EQL_B4_GAIN - [15:11] */
#define WM2200_EQL_B4_GAIN_WIDTH                     5  /* EQL_B4_GAIN - [15:11] */
#define WM2200_EQL_B5_GAIN_MASK                 0x07C0  /* EQL_B5_GAIN - [10:6] */
#define WM2200_EQL_B5_GAIN_SHIFT                     6  /* EQL_B5_GAIN - [10:6] */
#define WM2200_EQL_B5_GAIN_WIDTH                     5  /* EQL_B5_GAIN - [10:6] */

/*
 * R2306 (0x902) - EQL_3
 */
#define WM2200_EQL_B1_A_MASK                    0xFFFF  /* EQL_B1_A - [15:0] */
#define WM2200_EQL_B1_A_SHIFT                        0  /* EQL_B1_A - [15:0] */
#define WM2200_EQL_B1_A_WIDTH                       16  /* EQL_B1_A - [15:0] */

/*
 * R2307 (0x903) - EQL_4
 */
#define WM2200_EQL_B1_B_MASK                    0xFFFF  /* EQL_B1_B - [15:0] */
#define WM2200_EQL_B1_B_SHIFT                        0  /* EQL_B1_B - [15:0] */
#define WM2200_EQL_B1_B_WIDTH                       16  /* EQL_B1_B - [15:0] */

/*
 * R2308 (0x904) - EQL_5
 */
#define WM2200_EQL_B1_PG_MASK                   0xFFFF  /* EQL_B1_PG - [15:0] */
#define WM2200_EQL_B1_PG_SHIFT                       0  /* EQL_B1_PG - [15:0] */
#define WM2200_EQL_B1_PG_WIDTH                      16  /* EQL_B1_PG - [15:0] */

/*
 * R2309 (0x905) - EQL_6
 */
#define WM2200_EQL_B2_A_MASK                    0xFFFF  /* EQL_B2_A - [15:0] */
#define WM2200_EQL_B2_A_SHIFT                        0  /* EQL_B2_A - [15:0] */
#define WM2200_EQL_B2_A_WIDTH                       16  /* EQL_B2_A - [15:0] */

/*
 * R2310 (0x906) - EQL_7
 */
#define WM2200_EQL_B2_B_MASK                    0xFFFF  /* EQL_B2_B - [15:0] */
#define WM2200_EQL_B2_B_SHIFT                        0  /* EQL_B2_B - [15:0] */
#define WM2200_EQL_B2_B_WIDTH                       16  /* EQL_B2_B - [15:0] */

/*
 * R2311 (0x907) - EQL_8
 */
#define WM2200_EQL_B2_C_MASK                    0xFFFF  /* EQL_B2_C - [15:0] */
#define WM2200_EQL_B2_C_SHIFT                        0  /* EQL_B2_C - [15:0] */
#define WM2200_EQL_B2_C_WIDTH                       16  /* EQL_B2_C - [15:0] */

/*
 * R2312 (0x908) - EQL_9
 */
#define WM2200_EQL_B2_PG_MASK                   0xFFFF  /* EQL_B2_PG - [15:0] */
#define WM2200_EQL_B2_PG_SHIFT                       0  /* EQL_B2_PG - [15:0] */
#define WM2200_EQL_B2_PG_WIDTH                      16  /* EQL_B2_PG - [15:0] */

/*
 * R2313 (0x909) - EQL_10
 */
#define WM2200_EQL_B3_A_MASK                    0xFFFF  /* EQL_B3_A - [15:0] */
#define WM2200_EQL_B3_A_SHIFT                        0  /* EQL_B3_A - [15:0] */
#define WM2200_EQL_B3_A_WIDTH                       16  /* EQL_B3_A - [15:0] */

/*
 * R2314 (0x90A) - EQL_11
 */
#define WM2200_EQL_B3_B_MASK                    0xFFFF  /* EQL_B3_B - [15:0] */
#define WM2200_EQL_B3_B_SHIFT                        0  /* EQL_B3_B - [15:0] */
#define WM2200_EQL_B3_B_WIDTH                       16  /* EQL_B3_B - [15:0] */

/*
 * R2315 (0x90B) - EQL_12
 */
#define WM2200_EQL_B3_C_MASK                    0xFFFF  /* EQL_B3_C - [15:0] */
#define WM2200_EQL_B3_C_SHIFT                        0  /* EQL_B3_C - [15:0] */
#define WM2200_EQL_B3_C_WIDTH                       16  /* EQL_B3_C - [15:0] */

/*
 * R2316 (0x90C) - EQL_13
 */
#define WM2200_EQL_B3_PG_MASK                   0xFFFF  /* EQL_B3_PG - [15:0] */
#define WM2200_EQL_B3_PG_SHIFT                       0  /* EQL_B3_PG - [15:0] */
#define WM2200_EQL_B3_PG_WIDTH                      16  /* EQL_B3_PG - [15:0] */

/*
 * R2317 (0x90D) - EQL_14
 */
#define WM2200_EQL_B4_A_MASK                    0xFFFF  /* EQL_B4_A - [15:0] */
#define WM2200_EQL_B4_A_SHIFT                        0  /* EQL_B4_A - [15:0] */
#define WM2200_EQL_B4_A_WIDTH                       16  /* EQL_B4_A - [15:0] */

/*
 * R2318 (0x90E) - EQL_15
 */
#define WM2200_EQL_B4_B_MASK                    0xFFFF  /* EQL_B4_B - [15:0] */
#define WM2200_EQL_B4_B_SHIFT                        0  /* EQL_B4_B - [15:0] */
#define WM2200_EQL_B4_B_WIDTH                       16  /* EQL_B4_B - [15:0] */

/*
 * R2319 (0x90F) - EQL_16
 */
#define WM2200_EQL_B4_C_MASK                    0xFFFF  /* EQL_B4_C - [15:0] */
#define WM2200_EQL_B4_C_SHIFT                        0  /* EQL_B4_C - [15:0] */
#define WM2200_EQL_B4_C_WIDTH                       16  /* EQL_B4_C - [15:0] */

/*
 * R2320 (0x910) - EQL_17
 */
#define WM2200_EQL_B4_PG_MASK                   0xFFFF  /* EQL_B4_PG - [15:0] */
#define WM2200_EQL_B4_PG_SHIFT                       0  /* EQL_B4_PG - [15:0] */
#define WM2200_EQL_B4_PG_WIDTH                      16  /* EQL_B4_PG - [15:0] */

/*
 * R2321 (0x911) - EQL_18
 */
#define WM2200_EQL_B5_A_MASK                    0xFFFF  /* EQL_B5_A - [15:0] */
#define WM2200_EQL_B5_A_SHIFT                        0  /* EQL_B5_A - [15:0] */
#define WM2200_EQL_B5_A_WIDTH                       16  /* EQL_B5_A - [15:0] */

/*
 * R2322 (0x912) - EQL_19
 */
#define WM2200_EQL_B5_B_MASK                    0xFFFF  /* EQL_B5_B - [15:0] */
#define WM2200_EQL_B5_B_SHIFT                        0  /* EQL_B5_B - [15:0] */
#define WM2200_EQL_B5_B_WIDTH                       16  /* EQL_B5_B - [15:0] */

/*
 * R2323 (0x913) - EQL_20
 */
#define WM2200_EQL_B5_PG_MASK                   0xFFFF  /* EQL_B5_PG - [15:0] */
#define WM2200_EQL_B5_PG_SHIFT                       0  /* EQL_B5_PG - [15:0] */
#define WM2200_EQL_B5_PG_WIDTH                      16  /* EQL_B5_PG - [15:0] */

/*
 * R2326 (0x916) - EQR_1
 */
#define WM2200_EQR_B1_GAIN_MASK                 0xF800  /* EQR_B1_GAIN - [15:11] */
#define WM2200_EQR_B1_GAIN_SHIFT                    11  /* EQR_B1_GAIN - [15:11] */
#define WM2200_EQR_B1_GAIN_WIDTH                     5  /* EQR_B1_GAIN - [15:11] */
#define WM2200_EQR_B2_GAIN_MASK                 0x07C0  /* EQR_B2_GAIN - [10:6] */
#define WM2200_EQR_B2_GAIN_SHIFT                     6  /* EQR_B2_GAIN - [10:6] */
#define WM2200_EQR_B2_GAIN_WIDTH                     5  /* EQR_B2_GAIN - [10:6] */
#define WM2200_EQR_B3_GAIN_MASK                 0x003E  /* EQR_B3_GAIN - [5:1] */
#define WM2200_EQR_B3_GAIN_SHIFT                     1  /* EQR_B3_GAIN - [5:1] */
#define WM2200_EQR_B3_GAIN_WIDTH                     5  /* EQR_B3_GAIN - [5:1] */
#define WM2200_EQR_ENA                          0x0001  /* EQR_ENA */
#define WM2200_EQR_ENA_MASK                     0x0001  /* EQR_ENA */
#define WM2200_EQR_ENA_SHIFT                         0  /* EQR_ENA */
#define WM2200_EQR_ENA_WIDTH                         1  /* EQR_ENA */

/*
 * R2327 (0x917) - EQR_2
 */
#define WM2200_EQR_B4_GAIN_MASK                 0xF800  /* EQR_B4_GAIN - [15:11] */
#define WM2200_EQR_B4_GAIN_SHIFT                    11  /* EQR_B4_GAIN - [15:11] */
#define WM2200_EQR_B4_GAIN_WIDTH                     5  /* EQR_B4_GAIN - [15:11] */
#define WM2200_EQR_B5_GAIN_MASK                 0x07C0  /* EQR_B5_GAIN - [10:6] */
#define WM2200_EQR_B5_GAIN_SHIFT                     6  /* EQR_B5_GAIN - [10:6] */
#define WM2200_EQR_B5_GAIN_WIDTH                     5  /* EQR_B5_GAIN - [10:6] */

/*
 * R2328 (0x918) - EQR_3
 */
#define WM2200_EQR_B1_A_MASK                    0xFFFF  /* EQR_B1_A - [15:0] */
#define WM2200_EQR_B1_A_SHIFT                        0  /* EQR_B1_A - [15:0] */
#define WM2200_EQR_B1_A_WIDTH                       16  /* EQR_B1_A - [15:0] */

/*
 * R2329 (0x919) - EQR_4
 */
#define WM2200_EQR_B1_B_MASK                    0xFFFF  /* EQR_B1_B - [15:0] */
#define WM2200_EQR_B1_B_SHIFT                        0  /* EQR_B1_B - [15:0] */
#define WM2200_EQR_B1_B_WIDTH                       16  /* EQR_B1_B - [15:0] */

/*
 * R2330 (0x91A) - EQR_5
 */
#define WM2200_EQR_B1_PG_MASK                   0xFFFF  /* EQR_B1_PG - [15:0] */
#define WM2200_EQR_B1_PG_SHIFT                       0  /* EQR_B1_PG - [15:0] */
#define WM2200_EQR_B1_PG_WIDTH                      16  /* EQR_B1_PG - [15:0] */

/*
 * R2331 (0x91B) - EQR_6
 */
#define WM2200_EQR_B2_A_MASK                    0xFFFF  /* EQR_B2_A - [15:0] */
#define WM2200_EQR_B2_A_SHIFT                        0  /* EQR_B2_A - [15:0] */
#define WM2200_EQR_B2_A_WIDTH                       16  /* EQR_B2_A - [15:0] */

/*
 * R2332 (0x91C) - EQR_7
 */
#define WM2200_EQR_B2_B_MASK                    0xFFFF  /* EQR_B2_B - [15:0] */
#define WM2200_EQR_B2_B_SHIFT                        0  /* EQR_B2_B - [15:0] */
#define WM2200_EQR_B2_B_WIDTH                       16  /* EQR_B2_B - [15:0] */

/*
 * R2333 (0x91D) - EQR_8
 */
#define WM2200_EQR_B2_C_MASK                    0xFFFF  /* EQR_B2_C - [15:0] */
#define WM2200_EQR_B2_C_SHIFT                        0  /* EQR_B2_C - [15:0] */
#define WM2200_EQR_B2_C_WIDTH                       16  /* EQR_B2_C - [15:0] */

/*
 * R2334 (0x91E) - EQR_9
 */
#define WM2200_EQR_B2_PG_MASK                   0xFFFF  /* EQR_B2_PG - [15:0] */
#define WM2200_EQR_B2_PG_SHIFT                       0  /* EQR_B2_PG - [15:0] */
#define WM2200_EQR_B2_PG_WIDTH                      16  /* EQR_B2_PG - [15:0] */

/*
 * R2335 (0x91F) - EQR_10
 */
#define WM2200_EQR_B3_A_MASK                    0xFFFF  /* EQR_B3_A - [15:0] */
#define WM2200_EQR_B3_A_SHIFT                        0  /* EQR_B3_A - [15:0] */
#define WM2200_EQR_B3_A_WIDTH                       16  /* EQR_B3_A - [15:0] */

/*
 * R2336 (0x920) - EQR_11
 */
#define WM2200_EQR_B3_B_MASK                    0xFFFF  /* EQR_B3_B - [15:0] */
#define WM2200_EQR_B3_B_SHIFT                        0  /* EQR_B3_B - [15:0] */
#define WM2200_EQR_B3_B_WIDTH                       16  /* EQR_B3_B - [15:0] */

/*
 * R2337 (0x921) - EQR_12
 */
#define WM2200_EQR_B3_C_MASK                    0xFFFF  /* EQR_B3_C - [15:0] */
#define WM2200_EQR_B3_C_SHIFT                        0  /* EQR_B3_C - [15:0] */
#define WM2200_EQR_B3_C_WIDTH                       16  /* EQR_B3_C - [15:0] */

/*
 * R2338 (0x922) - EQR_13
 */
#define WM2200_EQR_B3_PG_MASK                   0xFFFF  /* EQR_B3_PG - [15:0] */
#define WM2200_EQR_B3_PG_SHIFT                       0  /* EQR_B3_PG - [15:0] */
#define WM2200_EQR_B3_PG_WIDTH                      16  /* EQR_B3_PG - [15:0] */

/*
 * R2339 (0x923) - EQR_14
 */
#define WM2200_EQR_B4_A_MASK                    0xFFFF  /* EQR_B4_A - [15:0] */
#define WM2200_EQR_B4_A_SHIFT                        0  /* EQR_B4_A - [15:0] */
#define WM2200_EQR_B4_A_WIDTH                       16  /* EQR_B4_A - [15:0] */

/*
 * R2340 (0x924) - EQR_15
 */
#define WM2200_EQR_B4_B_MASK                    0xFFFF  /* EQR_B4_B - [15:0] */
#define WM2200_EQR_B4_B_SHIFT                        0  /* EQR_B4_B - [15:0] */
#define WM2200_EQR_B4_B_WIDTH                       16  /* EQR_B4_B - [15:0] */

/*
 * R2341 (0x925) - EQR_16
 */
#define WM2200_EQR_B4_C_MASK                    0xFFFF  /* EQR_B4_C - [15:0] */
#define WM2200_EQR_B4_C_SHIFT                        0  /* EQR_B4_C - [15:0] */
#define WM2200_EQR_B4_C_WIDTH                       16  /* EQR_B4_C - [15:0] */

/*
 * R2342 (0x926) - EQR_17
 */
#define WM2200_EQR_B4_PG_MASK                   0xFFFF  /* EQR_B4_PG - [15:0] */
#define WM2200_EQR_B4_PG_SHIFT                       0  /* EQR_B4_PG - [15:0] */
#define WM2200_EQR_B4_PG_WIDTH                      16  /* EQR_B4_PG - [15:0] */

/*
 * R2343 (0x927) - EQR_18
 */
#define WM2200_EQR_B5_A_MASK                    0xFFFF  /* EQR_B5_A - [15:0] */
#define WM2200_EQR_B5_A_SHIFT                        0  /* EQR_B5_A - [15:0] */
#define WM2200_EQR_B5_A_WIDTH                       16  /* EQR_B5_A - [15:0] */

/*
 * R2344 (0x928) - EQR_19
 */
#define WM2200_EQR_B5_B_MASK                    0xFFFF  /* EQR_B5_B - [15:0] */
#define WM2200_EQR_B5_B_SHIFT                        0  /* EQR_B5_B - [15:0] */
#define WM2200_EQR_B5_B_WIDTH                       16  /* EQR_B5_B - [15:0] */

/*
 * R2345 (0x929) - EQR_20
 */
#define WM2200_EQR_B5_PG_MASK                   0xFFFF  /* EQR_B5_PG - [15:0] */
#define WM2200_EQR_B5_PG_SHIFT                       0  /* EQR_B5_PG - [15:0] */
#define WM2200_EQR_B5_PG_WIDTH                      16  /* EQR_B5_PG - [15:0] */

/*
 * R2366 (0x93E) - HPLPF1_1
 */
#define WM2200_LHPF1_MODE                       0x0002  /* LHPF1_MODE */
#define WM2200_LHPF1_MODE_MASK                  0x0002  /* LHPF1_MODE */
#define WM2200_LHPF1_MODE_SHIFT                      1  /* LHPF1_MODE */
#define WM2200_LHPF1_MODE_WIDTH                      1  /* LHPF1_MODE */
#define WM2200_LHPF1_ENA                        0x0001  /* LHPF1_ENA */
#define WM2200_LHPF1_ENA_MASK                   0x0001  /* LHPF1_ENA */
#define WM2200_LHPF1_ENA_SHIFT                       0  /* LHPF1_ENA */
#define WM2200_LHPF1_ENA_WIDTH                       1  /* LHPF1_ENA */

/*
 * R2367 (0x93F) - HPLPF1_2
 */
#define WM2200_LHPF1_COEFF_MASK                 0xFFFF  /* LHPF1_COEFF - [15:0] */
#define WM2200_LHPF1_COEFF_SHIFT                     0  /* LHPF1_COEFF - [15:0] */
#define WM2200_LHPF1_COEFF_WIDTH                    16  /* LHPF1_COEFF - [15:0] */

/*
 * R2370 (0x942) - HPLPF2_1
 */
#define WM2200_LHPF2_MODE                       0x0002  /* LHPF2_MODE */
#define WM2200_LHPF2_MODE_MASK                  0x0002  /* LHPF2_MODE */
#define WM2200_LHPF2_MODE_SHIFT                      1  /* LHPF2_MODE */
#define WM2200_LHPF2_MODE_WIDTH                      1  /* LHPF2_MODE */
#define WM2200_LHPF2_ENA                        0x0001  /* LHPF2_ENA */
#define WM2200_LHPF2_ENA_MASK                   0x0001  /* LHPF2_ENA */
#define WM2200_LHPF2_ENA_SHIFT                       0  /* LHPF2_ENA */
#define WM2200_LHPF2_ENA_WIDTH                       1  /* LHPF2_ENA */

/*
 * R2371 (0x943) - HPLPF2_2
 */
#define WM2200_LHPF2_COEFF_MASK                 0xFFFF  /* LHPF2_COEFF - [15:0] */
#define WM2200_LHPF2_COEFF_SHIFT                     0  /* LHPF2_COEFF - [15:0] */
#define WM2200_LHPF2_COEFF_WIDTH                    16  /* LHPF2_COEFF - [15:0] */

/*
 * R2560 (0xA00) - DSP1 Control 1
 */
#define WM2200_DSP1_RW_SEQUENCE_ENA             0x0001  /* DSP1_RW_SEQUENCE_ENA */
#define WM2200_DSP1_RW_SEQUENCE_ENA_MASK        0x0001  /* DSP1_RW_SEQUENCE_ENA */
#define WM2200_DSP1_RW_SEQUENCE_ENA_SHIFT            0  /* DSP1_RW_SEQUENCE_ENA */
#define WM2200_DSP1_RW_SEQUENCE_ENA_WIDTH            1  /* DSP1_RW_SEQUENCE_ENA */

/*
 * R2562 (0xA02) - DSP1 Control 2
 */
#define WM2200_DSP1_PAGE_BASE_PM_0_MASK         0xFF00  /* DSP1_PAGE_BASE_PM - [15:8] */
#define WM2200_DSP1_PAGE_BASE_PM_0_SHIFT             8  /* DSP1_PAGE_BASE_PM - [15:8] */
#define WM2200_DSP1_PAGE_BASE_PM_0_WIDTH             8  /* DSP1_PAGE_BASE_PM - [15:8] */

/*
 * R2563 (0xA03) - DSP1 Control 3
 */
#define WM2200_DSP1_PAGE_BASE_DM_0_MASK         0xFF00  /* DSP1_PAGE_BASE_DM - [15:8] */
#define WM2200_DSP1_PAGE_BASE_DM_0_SHIFT             8  /* DSP1_PAGE_BASE_DM - [15:8] */
#define WM2200_DSP1_PAGE_BASE_DM_0_WIDTH             8  /* DSP1_PAGE_BASE_DM - [15:8] */

/*
 * R2564 (0xA04) - DSP1 Control 4
 */
#define WM2200_DSP1_PAGE_BASE_ZM_0_MASK         0xFF00  /* DSP1_PAGE_BASE_ZM - [15:8] */
#define WM2200_DSP1_PAGE_BASE_ZM_0_SHIFT             8  /* DSP1_PAGE_BASE_ZM - [15:8] */
#define WM2200_DSP1_PAGE_BASE_ZM_0_WIDTH             8  /* DSP1_PAGE_BASE_ZM - [15:8] */

/*
 * R2566 (0xA06) - DSP1 Control 5
 */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_0_MASK 0x3FFF  /* DSP1_START_ADDRESS_WDMA_BUFFER_0 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_0_SHIFT      0  /* DSP1_START_ADDRESS_WDMA_BUFFER_0 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_0_WIDTH     14  /* DSP1_START_ADDRESS_WDMA_BUFFER_0 - [13:0] */

/*
 * R2567 (0xA07) - DSP1 Control 6
 */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_1_MASK 0x3FFF  /* DSP1_START_ADDRESS_WDMA_BUFFER_1 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_1_SHIFT      0  /* DSP1_START_ADDRESS_WDMA_BUFFER_1 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_1_WIDTH     14  /* DSP1_START_ADDRESS_WDMA_BUFFER_1 - [13:0] */

/*
 * R2568 (0xA08) - DSP1 Control 7
 */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_2_MASK 0x3FFF  /* DSP1_START_ADDRESS_WDMA_BUFFER_2 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_2_SHIFT      0  /* DSP1_START_ADDRESS_WDMA_BUFFER_2 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_2_WIDTH     14  /* DSP1_START_ADDRESS_WDMA_BUFFER_2 - [13:0] */

/*
 * R2569 (0xA09) - DSP1 Control 8
 */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_3_MASK 0x3FFF  /* DSP1_START_ADDRESS_WDMA_BUFFER_3 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_3_SHIFT      0  /* DSP1_START_ADDRESS_WDMA_BUFFER_3 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_3_WIDTH     14  /* DSP1_START_ADDRESS_WDMA_BUFFER_3 - [13:0] */

/*
 * R2570 (0xA0A) - DSP1 Control 9
 */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_4_MASK 0x3FFF  /* DSP1_START_ADDRESS_WDMA_BUFFER_4 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_4_SHIFT      0  /* DSP1_START_ADDRESS_WDMA_BUFFER_4 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_4_WIDTH     14  /* DSP1_START_ADDRESS_WDMA_BUFFER_4 - [13:0] */

/*
 * R2571 (0xA0B) - DSP1 Control 10
 */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_5_MASK 0x3FFF  /* DSP1_START_ADDRESS_WDMA_BUFFER_5 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_5_SHIFT      0  /* DSP1_START_ADDRESS_WDMA_BUFFER_5 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_5_WIDTH     14  /* DSP1_START_ADDRESS_WDMA_BUFFER_5 - [13:0] */

/*
 * R2572 (0xA0C) - DSP1 Control 11
 */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_6_MASK 0x3FFF  /* DSP1_START_ADDRESS_WDMA_BUFFER_6 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_6_SHIFT      0  /* DSP1_START_ADDRESS_WDMA_BUFFER_6 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_6_WIDTH     14  /* DSP1_START_ADDRESS_WDMA_BUFFER_6 - [13:0] */

/*
 * R2573 (0xA0D) - DSP1 Control 12
 */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_7_MASK 0x3FFF  /* DSP1_START_ADDRESS_WDMA_BUFFER_7 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_7_SHIFT      0  /* DSP1_START_ADDRESS_WDMA_BUFFER_7 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_WDMA_BUFFER_7_WIDTH     14  /* DSP1_START_ADDRESS_WDMA_BUFFER_7 - [13:0] */

/*
 * R2575 (0xA0F) - DSP1 Control 13
 */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_0_MASK 0x3FFF  /* DSP1_START_ADDRESS_RDMA_BUFFER_0 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_0_SHIFT      0  /* DSP1_START_ADDRESS_RDMA_BUFFER_0 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_0_WIDTH     14  /* DSP1_START_ADDRESS_RDMA_BUFFER_0 - [13:0] */

/*
 * R2576 (0xA10) - DSP1 Control 14
 */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_1_MASK 0x3FFF  /* DSP1_START_ADDRESS_RDMA_BUFFER_1 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_1_SHIFT      0  /* DSP1_START_ADDRESS_RDMA_BUFFER_1 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_1_WIDTH     14  /* DSP1_START_ADDRESS_RDMA_BUFFER_1 - [13:0] */

/*
 * R2577 (0xA11) - DSP1 Control 15
 */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_2_MASK 0x3FFF  /* DSP1_START_ADDRESS_RDMA_BUFFER_2 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_2_SHIFT      0  /* DSP1_START_ADDRESS_RDMA_BUFFER_2 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_2_WIDTH     14  /* DSP1_START_ADDRESS_RDMA_BUFFER_2 - [13:0] */

/*
 * R2578 (0xA12) - DSP1 Control 16
 */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_3_MASK 0x3FFF  /* DSP1_START_ADDRESS_RDMA_BUFFER_3 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_3_SHIFT      0  /* DSP1_START_ADDRESS_RDMA_BUFFER_3 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_3_WIDTH     14  /* DSP1_START_ADDRESS_RDMA_BUFFER_3 - [13:0] */

/*
 * R2579 (0xA13) - DSP1 Control 17
 */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_4_MASK 0x3FFF  /* DSP1_START_ADDRESS_RDMA_BUFFER_4 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_4_SHIFT      0  /* DSP1_START_ADDRESS_RDMA_BUFFER_4 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_4_WIDTH     14  /* DSP1_START_ADDRESS_RDMA_BUFFER_4 - [13:0] */

/*
 * R2580 (0xA14) - DSP1 Control 18
 */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_5_MASK 0x3FFF  /* DSP1_START_ADDRESS_RDMA_BUFFER_5 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_5_SHIFT      0  /* DSP1_START_ADDRESS_RDMA_BUFFER_5 - [13:0] */
#define WM2200_DSP1_START_ADDRESS_RDMA_BUFFER_5_WIDTH     14  /* DSP1_START_ADDRESS_RDMA_BUFFER_5 - [13:0] */

/*
 * R2582 (0xA16) - DSP1 Control 19
 */
#define WM2200_DSP1_WDMA_BUFFER_LENGTH_MASK     0x00FF  /* DSP1_WDMA_BUFFER_LENGTH - [7:0] */
#define WM2200_DSP1_WDMA_BUFFER_LENGTH_SHIFT         0  /* DSP1_WDMA_BUFFER_LENGTH - [7:0] */
#define WM2200_DSP1_WDMA_BUFFER_LENGTH_WIDTH         8  /* DSP1_WDMA_BUFFER_LENGTH - [7:0] */

/*
 * R2583 (0xA17) - DSP1 Control 20
 */
#define WM2200_DSP1_WDMA_CHANNEL_ENABLE_MASK    0x00FF  /* DSP1_WDMA_CHANNEL_ENABLE - [7:0] */
#define WM2200_DSP1_WDMA_CHANNEL_ENABLE_SHIFT        0  /* DSP1_WDMA_CHANNEL_ENABLE - [7:0] */
#define WM2200_DSP1_WDMA_CHANNEL_ENABLE_WIDTH        8  /* DSP1_WDMA_CHANNEL_ENABLE - [7:0] */

/*
 * R2584 (0xA18) - DSP1 Control 21
 */
#define WM2200_DSP1_RDMA_CHANNEL_ENABLE_MASK    0x003F  /* DSP1_RDMA_CHANNEL_ENABLE - [5:0] */
#define WM2200_DSP1_RDMA_CHANNEL_ENABLE_SHIFT        0  /* DSP1_RDMA_CHANNEL_ENABLE - [5:0] */
#define WM2200_DSP1_RDMA_CHANNEL_ENABLE_WIDTH        6  /* DSP1_RDMA_CHANNEL_ENABLE - [5:0] */

/*
 * R2586 (0xA1A) - DSP1 Control 22
 */
#define WM2200_DSP1_DM_SIZE_MASK                0xFFFF  /* DSP1_DM_SIZE - [15:0] */
#define WM2200_DSP1_DM_SIZE_SHIFT                    0  /* DSP1_DM_SIZE - [15:0] */
#define WM2200_DSP1_DM_SIZE_WIDTH                   16  /* DSP1_DM_SIZE - [15:0] */

/*
 * R2587 (0xA1B) - DSP1 Control 23
 */
#define WM2200_DSP1_PM_SIZE_MASK                0xFFFF  /* DSP1_PM_SIZE - [15:0] */
#define WM2200_DSP1_PM_SIZE_SHIFT                    0  /* DSP1_PM_SIZE - [15:0] */
#define WM2200_DSP1_PM_SIZE_WIDTH                   16  /* DSP1_PM_SIZE - [15:0] */

/*
 * R2588 (0xA1C) - DSP1 Control 24
 */
#define WM2200_DSP1_ZM_SIZE_MASK                0xFFFF  /* DSP1_ZM_SIZE - [15:0] */
#define WM2200_DSP1_ZM_SIZE_SHIFT                    0  /* DSP1_ZM_SIZE - [15:0] */
#define WM2200_DSP1_ZM_SIZE_WIDTH                   16  /* DSP1_ZM_SIZE - [15:0] */

/*
 * R2590 (0xA1E) - DSP1 Control 25
 */
#define WM2200_DSP1_PING_FULL                   0x8000  /* DSP1_PING_FULL */
#define WM2200_DSP1_PING_FULL_MASK              0x8000  /* DSP1_PING_FULL */
#define WM2200_DSP1_PING_FULL_SHIFT                 15  /* DSP1_PING_FULL */
#define WM2200_DSP1_PING_FULL_WIDTH                  1  /* DSP1_PING_FULL */
#define WM2200_DSP1_PONG_FULL                   0x4000  /* DSP1_PONG_FULL */
#define WM2200_DSP1_PONG_FULL_MASK              0x4000  /* DSP1_PONG_FULL */
#define WM2200_DSP1_PONG_FULL_SHIFT                 14  /* DSP1_PONG_FULL */
#define WM2200_DSP1_PONG_FULL_WIDTH                  1  /* DSP1_PONG_FULL */
#define WM2200_DSP1_WDMA_ACTIVE_CHANNELS_MASK   0x00FF  /* DSP1_WDMA_ACTIVE_CHANNELS - [7:0] */
#define WM2200_DSP1_WDMA_ACTIVE_CHANNELS_SHIFT       0  /* DSP1_WDMA_ACTIVE_CHANNELS - [7:0] */
#define WM2200_DSP1_WDMA_ACTIVE_CHANNELS_WIDTH       8  /* DSP1_WDMA_ACTIVE_CHANNELS - [7:0] */

/*
 * R2592 (0xA20) - DSP1 Control 26
 */
#define WM2200_DSP1_SCRATCH_0_MASK              0xFFFF  /* DSP1_SCRATCH_0 - [15:0] */
#define WM2200_DSP1_SCRATCH_0_SHIFT                  0  /* DSP1_SCRATCH_0 - [15:0] */
#define WM2200_DSP1_SCRATCH_0_WIDTH                 16  /* DSP1_SCRATCH_0 - [15:0] */

/*
 * R2593 (0xA21) - DSP1 Control 27
 */
#define WM2200_DSP1_SCRATCH_1_MASK              0xFFFF  /* DSP1_SCRATCH_1 - [15:0] */
#define WM2200_DSP1_SCRATCH_1_SHIFT                  0  /* DSP1_SCRATCH_1 - [15:0] */
#define WM2200_DSP1_SCRATCH_1_WIDTH                 16  /* DSP1_SCRATCH_1 - [15:0] */

/*
 * R2594 (0xA22) - DSP1 Control 28
 */
#define WM2200_DSP1_SCRATCH_2_MASK              0xFFFF  /* DSP1_SCRATCH_2 - [15:0] */
#define WM2200_DSP1_SCRATCH_2_SHIFT                  0  /* DSP1_SCRATCH_2 - [15:0] */
#define WM2200_DSP1_SCRATCH_2_WIDTH                 16  /* DSP1_SCRATCH_2 - [15:0] */

/*
 * R2595 (0xA23) - DSP1 Control 29
 */
#define WM2200_DSP1_SCRATCH_3_MASK              0xFFFF  /* DSP1_SCRATCH_3 - [15:0] */
#define WM2200_DSP1_SCRATCH_3_SHIFT                  0  /* DSP1_SCRATCH_3 - [15:0] */
#define WM2200_DSP1_SCRATCH_3_WIDTH                 16  /* DSP1_SCRATCH_3 - [15:0] */

/*
 * R2596 (0xA24) - DSP1 Control 30
 */
#define WM2200_DSP1_DBG_CLK_ENA                 0x0008  /* DSP1_DBG_CLK_ENA */
#define WM2200_DSP1_DBG_CLK_ENA_MASK            0x0008  /* DSP1_DBG_CLK_ENA */
#define WM2200_DSP1_DBG_CLK_ENA_SHIFT                3  /* DSP1_DBG_CLK_ENA */
#define WM2200_DSP1_DBG_CLK_ENA_WIDTH                1  /* DSP1_DBG_CLK_ENA */
#define WM2200_DSP1_SYS_ENA                     0x0004  /* DSP1_SYS_ENA */
#define WM2200_DSP1_SYS_ENA_MASK                0x0004  /* DSP1_SYS_ENA */
#define WM2200_DSP1_SYS_ENA_SHIFT                    2  /* DSP1_SYS_ENA */
#define WM2200_DSP1_SYS_ENA_WIDTH                    1  /* DSP1_SYS_ENA */
#define WM2200_DSP1_CORE_ENA                    0x0002  /* DSP1_CORE_ENA */
#define WM2200_DSP1_CORE_ENA_MASK               0x0002  /* DSP1_CORE_ENA */
#define WM2200_DSP1_CORE_ENA_SHIFT                   1  /* DSP1_CORE_ENA */
#define WM2200_DSP1_CORE_ENA_WIDTH                   1  /* DSP1_CORE_ENA */
#define WM2200_DSP1_START                       0x0001  /* DSP1_START */
#define WM2200_DSP1_START_MASK                  0x0001  /* DSP1_START */
#define WM2200_DSP1_START_SHIFT                      0  /* DSP1_START */
#define WM2200_DSP1_START_WIDTH                      1  /* DSP1_START */

/*
 * R2598 (0xA26) - DSP1 Control 31
 */
#define WM2200_DSP1_CLK_RATE_MASK               0x0018  /* DSP1_CLK_RATE - [4:3] */
#define WM2200_DSP1_CLK_RATE_SHIFT                   3  /* DSP1_CLK_RATE - [4:3] */
#define WM2200_DSP1_CLK_RATE_WIDTH                   2  /* DSP1_CLK_RATE - [4:3] */
#define WM2200_DSP1_CLK_AVAIL                   0x0004  /* DSP1_CLK_AVAIL */
#define WM2200_DSP1_CLK_AVAIL_MASK              0x0004  /* DSP1_CLK_AVAIL */
#define WM2200_DSP1_CLK_AVAIL_SHIFT                  2  /* DSP1_CLK_AVAIL */
#define WM2200_DSP1_CLK_AVAIL_WIDTH                  1  /* DSP1_CLK_AVAIL */
#define WM2200_DSP1_CLK_REQ_MASK                0x0003  /* DSP1_CLK_REQ - [1:0] */
#define WM2200_DSP1_CLK_REQ_SHIFT                    0  /* DSP1_CLK_REQ - [1:0] */
#define WM2200_DSP1_CLK_REQ_WIDTH                    2  /* DSP1_CLK_REQ - [1:0] */

/*
 * R2816 (0xB00) - DSP2 Control 1
 */
#define WM2200_DSP2_RW_SEQUENCE_ENA             0x0001  /* DSP2_RW_SEQUENCE_ENA */
#define WM2200_DSP2_RW_SEQUENCE_ENA_MASK        0x0001  /* DSP2_RW_SEQUENCE_ENA */
#define WM2200_DSP2_RW_SEQUENCE_ENA_SHIFT            0  /* DSP2_RW_SEQUENCE_ENA */
#define WM2200_DSP2_RW_SEQUENCE_ENA_WIDTH            1  /* DSP2_RW_SEQUENCE_ENA */

/*
 * R2818 (0xB02) - DSP2 Control 2
 */
#define WM2200_DSP2_PAGE_BASE_PM_0_MASK         0xFF00  /* DSP2_PAGE_BASE_PM - [15:8] */
#define WM2200_DSP2_PAGE_BASE_PM_0_SHIFT             8  /* DSP2_PAGE_BASE_PM - [15:8] */
#define WM2200_DSP2_PAGE_BASE_PM_0_WIDTH             8  /* DSP2_PAGE_BASE_PM - [15:8] */

/*
 * R2819 (0xB03) - DSP2 Control 3
 */
#define WM2200_DSP2_PAGE_BASE_DM_0_MASK         0xFF00  /* DSP2_PAGE_BASE_DM - [15:8] */
#define WM2200_DSP2_PAGE_BASE_DM_0_SHIFT             8  /* DSP2_PAGE_BASE_DM - [15:8] */
#define WM2200_DSP2_PAGE_BASE_DM_0_WIDTH             8  /* DSP2_PAGE_BASE_DM - [15:8] */

/*
 * R2820 (0xB04) - DSP2 Control 4
 */
#define WM2200_DSP2_PAGE_BASE_ZM_0_MASK         0xFF00  /* DSP2_PAGE_BASE_ZM - [15:8] */
#define WM2200_DSP2_PAGE_BASE_ZM_0_SHIFT             8  /* DSP2_PAGE_BASE_ZM - [15:8] */
#define WM2200_DSP2_PAGE_BASE_ZM_0_WIDTH             8  /* DSP2_PAGE_BASE_ZM - [15:8] */

/*
 * R2822 (0xB06) - DSP2 Control 5
 */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_0_MASK 0x3FFF  /* DSP2_START_ADDRESS_WDMA_BUFFER_0 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_0_SHIFT      0  /* DSP2_START_ADDRESS_WDMA_BUFFER_0 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_0_WIDTH     14  /* DSP2_START_ADDRESS_WDMA_BUFFER_0 - [13:0] */

/*
 * R2823 (0xB07) - DSP2 Control 6
 */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_1_MASK 0x3FFF  /* DSP2_START_ADDRESS_WDMA_BUFFER_1 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_1_SHIFT      0  /* DSP2_START_ADDRESS_WDMA_BUFFER_1 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_1_WIDTH     14  /* DSP2_START_ADDRESS_WDMA_BUFFER_1 - [13:0] */

/*
 * R2824 (0xB08) - DSP2 Control 7
 */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_2_MASK 0x3FFF  /* DSP2_START_ADDRESS_WDMA_BUFFER_2 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_2_SHIFT      0  /* DSP2_START_ADDRESS_WDMA_BUFFER_2 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_2_WIDTH     14  /* DSP2_START_ADDRESS_WDMA_BUFFER_2 - [13:0] */

/*
 * R2825 (0xB09) - DSP2 Control 8
 */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_3_MASK 0x3FFF  /* DSP2_START_ADDRESS_WDMA_BUFFER_3 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_3_SHIFT      0  /* DSP2_START_ADDRESS_WDMA_BUFFER_3 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_3_WIDTH     14  /* DSP2_START_ADDRESS_WDMA_BUFFER_3 - [13:0] */

/*
 * R2826 (0xB0A) - DSP2 Control 9
 */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_4_MASK 0x3FFF  /* DSP2_START_ADDRESS_WDMA_BUFFER_4 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_4_SHIFT      0  /* DSP2_START_ADDRESS_WDMA_BUFFER_4 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_4_WIDTH     14  /* DSP2_START_ADDRESS_WDMA_BUFFER_4 - [13:0] */

/*
 * R2827 (0xB0B) - DSP2 Control 10
 */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_5_MASK 0x3FFF  /* DSP2_START_ADDRESS_WDMA_BUFFER_5 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_5_SHIFT      0  /* DSP2_START_ADDRESS_WDMA_BUFFER_5 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_5_WIDTH     14  /* DSP2_START_ADDRESS_WDMA_BUFFER_5 - [13:0] */

/*
 * R2828 (0xB0C) - DSP2 Control 11
 */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_6_MASK 0x3FFF  /* DSP2_START_ADDRESS_WDMA_BUFFER_6 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_6_SHIFT      0  /* DSP2_START_ADDRESS_WDMA_BUFFER_6 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_6_WIDTH     14  /* DSP2_START_ADDRESS_WDMA_BUFFER_6 - [13:0] */

/*
 * R2829 (0xB0D) - DSP2 Control 12
 */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_7_MASK 0x3FFF  /* DSP2_START_ADDRESS_WDMA_BUFFER_7 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_7_SHIFT      0  /* DSP2_START_ADDRESS_WDMA_BUFFER_7 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_WDMA_BUFFER_7_WIDTH     14  /* DSP2_START_ADDRESS_WDMA_BUFFER_7 - [13:0] */

/*
 * R2831 (0xB0F) - DSP2 Control 13
 */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_0_MASK 0x3FFF  /* DSP2_START_ADDRESS_RDMA_BUFFER_0 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_0_SHIFT      0  /* DSP2_START_ADDRESS_RDMA_BUFFER_0 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_0_WIDTH     14  /* DSP2_START_ADDRESS_RDMA_BUFFER_0 - [13:0] */

/*
 * R2832 (0xB10) - DSP2 Control 14
 */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_1_MASK 0x3FFF  /* DSP2_START_ADDRESS_RDMA_BUFFER_1 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_1_SHIFT      0  /* DSP2_START_ADDRESS_RDMA_BUFFER_1 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_1_WIDTH     14  /* DSP2_START_ADDRESS_RDMA_BUFFER_1 - [13:0] */

/*
 * R2833 (0xB11) - DSP2 Control 15
 */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_2_MASK 0x3FFF  /* DSP2_START_ADDRESS_RDMA_BUFFER_2 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_2_SHIFT      0  /* DSP2_START_ADDRESS_RDMA_BUFFER_2 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_2_WIDTH     14  /* DSP2_START_ADDRESS_RDMA_BUFFER_2 - [13:0] */

/*
 * R2834 (0xB12) - DSP2 Control 16
 */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_3_MASK 0x3FFF  /* DSP2_START_ADDRESS_RDMA_BUFFER_3 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_3_SHIFT      0  /* DSP2_START_ADDRESS_RDMA_BUFFER_3 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_3_WIDTH     14  /* DSP2_START_ADDRESS_RDMA_BUFFER_3 - [13:0] */

/*
 * R2835 (0xB13) - DSP2 Control 17
 */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_4_MASK 0x3FFF  /* DSP2_START_ADDRESS_RDMA_BUFFER_4 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_4_SHIFT      0  /* DSP2_START_ADDRESS_RDMA_BUFFER_4 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_4_WIDTH     14  /* DSP2_START_ADDRESS_RDMA_BUFFER_4 - [13:0] */

/*
 * R2836 (0xB14) - DSP2 Control 18
 */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_5_MASK 0x3FFF  /* DSP2_START_ADDRESS_RDMA_BUFFER_5 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_5_SHIFT      0  /* DSP2_START_ADDRESS_RDMA_BUFFER_5 - [13:0] */
#define WM2200_DSP2_START_ADDRESS_RDMA_BUFFER_5_WIDTH     14  /* DSP2_START_ADDRESS_RDMA_BUFFER_5 - [13:0] */

/*
 * R2838 (0xB16) - DSP2 Control 19
 */
#define WM2200_DSP2_WDMA_BUFFER_LENGTH_MASK     0x00FF  /* DSP2_WDMA_BUFFER_LENGTH - [7:0] */
#define WM2200_DSP2_WDMA_BUFFER_LENGTH_SHIFT         0  /* DSP2_WDMA_BUFFER_LENGTH - [7:0] */
#define WM2200_DSP2_WDMA_BUFFER_LENGTH_WIDTH         8  /* DSP2_WDMA_BUFFER_LENGTH - [7:0] */

/*
 * R2839 (0xB17) - DSP2 Control 20
 */
#define WM2200_DSP2_WDMA_CHANNEL_ENABLE_MASK    0x00FF  /* DSP2_WDMA_CHANNEL_ENABLE - [7:0] */
#define WM2200_DSP2_WDMA_CHANNEL_ENABLE_SHIFT        0  /* DSP2_WDMA_CHANNEL_ENABLE - [7:0] */
#define WM2200_DSP2_WDMA_CHANNEL_ENABLE_WIDTH        8  /* DSP2_WDMA_CHANNEL_ENABLE - [7:0] */

/*
 * R2840 (0xB18) - DSP2 Control 21
 */
#define WM2200_DSP2_RDMA_CHANNEL_ENABLE_MASK    0x003F  /* DSP2_RDMA_CHANNEL_ENABLE - [5:0] */
#define WM2200_DSP2_RDMA_CHANNEL_ENABLE_SHIFT        0  /* DSP2_RDMA_CHANNEL_ENABLE - [5:0] */
#define WM2200_DSP2_RDMA_CHANNEL_ENABLE_WIDTH        6  /* DSP2_RDMA_CHANNEL_ENABLE - [5:0] */

/*
 * R2842 (0xB1A) - DSP2 Control 22
 */
#define WM2200_DSP2_DM_SIZE_MASK                0xFFFF  /* DSP2_DM_SIZE - [15:0] */
#define WM2200_DSP2_DM_SIZE_SHIFT                    0  /* DSP2_DM_SIZE - [15:0] */
#define WM2200_DSP2_DM_SIZE_WIDTH                   16  /* DSP2_DM_SIZE - [15:0] */

/*
 * R2843 (0xB1B) - DSP2 Control 23
 */
#define WM2200_DSP2_PM_SIZE_MASK                0xFFFF  /* DSP2_PM_SIZE - [15:0] */
#define WM2200_DSP2_PM_SIZE_SHIFT                    0  /* DSP2_PM_SIZE - [15:0] */
#define WM2200_DSP2_PM_SIZE_WIDTH                   16  /* DSP2_PM_SIZE - [15:0] */

/*
 * R2844 (0xB1C) - DSP2 Control 24
 */
#define WM2200_DSP2_ZM_SIZE_MASK                0xFFFF  /* DSP2_ZM_SIZE - [15:0] */
#define WM2200_DSP2_ZM_SIZE_SHIFT                    0  /* DSP2_ZM_SIZE - [15:0] */
#define WM2200_DSP2_ZM_SIZE_WIDTH                   16  /* DSP2_ZM_SIZE - [15:0] */

/*
 * R2846 (0xB1E) - DSP2 Control 25
 */
#define WM2200_DSP2_PING_FULL                   0x8000  /* DSP2_PING_FULL */
#define WM2200_DSP2_PING_FULL_MASK              0x8000  /* DSP2_PING_FULL */
#define WM2200_DSP2_PING_FULL_SHIFT                 15  /* DSP2_PING_FULL */
#define WM2200_DSP2_PING_FULL_WIDTH                  1  /* DSP2_PING_FULL */
#define WM2200_DSP2_PONG_FULL                   0x4000  /* DSP2_PONG_FULL */
#define WM2200_DSP2_PONG_FULL_MASK              0x4000  /* DSP2_PONG_FULL */
#define WM2200_DSP2_PONG_FULL_SHIFT                 14  /* DSP2_PONG_FULL */
#define WM2200_DSP2_PONG_FULL_WIDTH                  1  /* DSP2_PONG_FULL */
#define WM2200_DSP2_WDMA_ACTIVE_CHANNELS_MASK   0x00FF  /* DSP2_WDMA_ACTIVE_CHANNELS - [7:0] */
#define WM2200_DSP2_WDMA_ACTIVE_CHANNELS_SHIFT       0  /* DSP2_WDMA_ACTIVE_CHANNELS - [7:0] */
#define WM2200_DSP2_WDMA_ACTIVE_CHANNELS_WIDTH       8  /* DSP2_WDMA_ACTIVE_CHANNELS - [7:0] */

/*
 * R2848 (0xB20) - DSP2 Control 26
 */
#define WM2200_DSP2_SCRATCH_0_MASK              0xFFFF  /* DSP2_SCRATCH_0 - [15:0] */
#define WM2200_DSP2_SCRATCH_0_SHIFT                  0  /* DSP2_SCRATCH_0 - [15:0] */
#define WM2200_DSP2_SCRATCH_0_WIDTH                 16  /* DSP2_SCRATCH_0 - [15:0] */

/*
 * R2849 (0xB21) - DSP2 Control 27
 */
#define WM2200_DSP2_SCRATCH_1_MASK              0xFFFF  /* DSP2_SCRATCH_1 - [15:0] */
#define WM2200_DSP2_SCRATCH_1_SHIFT                  0  /* DSP2_SCRATCH_1 - [15:0] */
#define WM2200_DSP2_SCRATCH_1_WIDTH                 16  /* DSP2_SCRATCH_1 - [15:0] */

/*
 * R2850 (0xB22) - DSP2 Control 28
 */
#define WM2200_DSP2_SCRATCH_2_MASK              0xFFFF  /* DSP2_SCRATCH_2 - [15:0] */
#define WM2200_DSP2_SCRATCH_2_SHIFT                  0  /* DSP2_SCRATCH_2 - [15:0] */
#define WM2200_DSP2_SCRATCH_2_WIDTH                 16  /* DSP2_SCRATCH_2 - [15:0] */

/*
 * R2851 (0xB23) - DSP2 Control 29
 */
#define WM2200_DSP2_SCRATCH_3_MASK              0xFFFF  /* DSP2_SCRATCH_3 - [15:0] */
#define WM2200_DSP2_SCRATCH_3_SHIFT                  0  /* DSP2_SCRATCH_3 - [15:0] */
#define WM2200_DSP2_SCRATCH_3_WIDTH                 16  /* DSP2_SCRATCH_3 - [15:0] */

/*
 * R2852 (0xB24) - DSP2 Control 30
 */
#define WM2200_DSP2_DBG_CLK_ENA                 0x0008  /* DSP2_DBG_CLK_ENA */
#define WM2200_DSP2_DBG_CLK_ENA_MASK            0x0008  /* DSP2_DBG_CLK_ENA */
#define WM2200_DSP2_DBG_CLK_ENA_SHIFT                3  /* DSP2_DBG_CLK_ENA */
#define WM2200_DSP2_DBG_CLK_ENA_WIDTH                1  /* DSP2_DBG_CLK_ENA */
#define WM2200_DSP2_SYS_ENA                     0x0004  /* DSP2_SYS_ENA */
#define WM2200_DSP2_SYS_ENA_MASK                0x0004  /* DSP2_SYS_ENA */
#define WM2200_DSP2_SYS_ENA_SHIFT                    2  /* DSP2_SYS_ENA */
#define WM2200_DSP2_SYS_ENA_WIDTH                    1  /* DSP2_SYS_ENA */
#define WM2200_DSP2_CORE_ENA                    0x0002  /* DSP2_CORE_ENA */
#define WM2200_DSP2_CORE_ENA_MASK               0x0002  /* DSP2_CORE_ENA */
#define WM2200_DSP2_CORE_ENA_SHIFT                   1  /* DSP2_CORE_ENA */
#define WM2200_DSP2_CORE_ENA_WIDTH                   1  /* DSP2_CORE_ENA */
#define WM2200_DSP2_START                       0x0001  /* DSP2_START */
#define WM2200_DSP2_START_MASK                  0x0001  /* DSP2_START */
#define WM2200_DSP2_START_SHIFT                      0  /* DSP2_START */
#define WM2200_DSP2_START_WIDTH                      1  /* DSP2_START */

/*
 * R2854 (0xB26) - DSP2 Control 31
 */
#define WM2200_DSP2_CLK_RATE_MASK               0x0018  /* DSP2_CLK_RATE - [4:3] */
#define WM2200_DSP2_CLK_RATE_SHIFT                   3  /* DSP2_CLK_RATE - [4:3] */
#define WM2200_DSP2_CLK_RATE_WIDTH                   2  /* DSP2_CLK_RATE - [4:3] */
#define WM2200_DSP2_CLK_AVAIL                   0x0004  /* DSP2_CLK_AVAIL */
#define WM2200_DSP2_CLK_AVAIL_MASK              0x0004  /* DSP2_CLK_AVAIL */
#define WM2200_DSP2_CLK_AVAIL_SHIFT                  2  /* DSP2_CLK_AVAIL */
#define WM2200_DSP2_CLK_AVAIL_WIDTH                  1  /* DSP2_CLK_AVAIL */
#define WM2200_DSP2_CLK_REQ_MASK                0x0003  /* DSP2_CLK_REQ - [1:0] */
#define WM2200_DSP2_CLK_REQ_SHIFT                    0  /* DSP2_CLK_REQ - [1:0] */
#define WM2200_DSP2_CLK_REQ_WIDTH                    2  /* DSP2_CLK_REQ - [1:0] */

#endif
