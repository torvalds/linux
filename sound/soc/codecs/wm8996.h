/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * wm8996.h - WM8996 audio codec interface
 *
 * Copyright 2011 Wolfson Microelectronics PLC.
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#ifndef _WM8996_H
#define _WM8996_H

#define WM8996_SYSCLK_MCLK1 1
#define WM8996_SYSCLK_MCLK2 2
#define WM8996_SYSCLK_FLL   3

#define WM8996_FLL_MCLK1      1
#define WM8996_FLL_MCLK2      2
#define WM8996_FLL_DACLRCLK1  3
#define WM8996_FLL_BCLK1      4

typedef void (*wm8996_polarity_fn)(struct snd_soc_component *component, int polarity);

int wm8996_detect(struct snd_soc_component *component, struct snd_soc_jack *jack,
		  wm8996_polarity_fn polarity_cb);

/*
 * Register values.
 */
#define WM8996_SOFTWARE_RESET                   0x00
#define WM8996_POWER_MANAGEMENT_1               0x01
#define WM8996_POWER_MANAGEMENT_2               0x02
#define WM8996_POWER_MANAGEMENT_3               0x03
#define WM8996_POWER_MANAGEMENT_4               0x04
#define WM8996_POWER_MANAGEMENT_5               0x05
#define WM8996_POWER_MANAGEMENT_6               0x06
#define WM8996_POWER_MANAGEMENT_7               0x07
#define WM8996_POWER_MANAGEMENT_8               0x08
#define WM8996_LEFT_LINE_INPUT_VOLUME           0x10
#define WM8996_RIGHT_LINE_INPUT_VOLUME          0x11
#define WM8996_LINE_INPUT_CONTROL               0x12
#define WM8996_DAC1_HPOUT1_VOLUME               0x15
#define WM8996_DAC2_HPOUT2_VOLUME               0x16
#define WM8996_DAC1_LEFT_VOLUME                 0x18
#define WM8996_DAC1_RIGHT_VOLUME                0x19
#define WM8996_DAC2_LEFT_VOLUME                 0x1A
#define WM8996_DAC2_RIGHT_VOLUME                0x1B
#define WM8996_OUTPUT1_LEFT_VOLUME              0x1C
#define WM8996_OUTPUT1_RIGHT_VOLUME             0x1D
#define WM8996_OUTPUT2_LEFT_VOLUME              0x1E
#define WM8996_OUTPUT2_RIGHT_VOLUME             0x1F
#define WM8996_MICBIAS_1                        0x20
#define WM8996_MICBIAS_2                        0x21
#define WM8996_LDO_1                            0x28
#define WM8996_LDO_2                            0x29
#define WM8996_ACCESSORY_DETECT_MODE_1          0x30
#define WM8996_ACCESSORY_DETECT_MODE_2          0x31
#define WM8996_HEADPHONE_DETECT_1               0x34
#define WM8996_HEADPHONE_DETECT_2               0x35
#define WM8996_MIC_DETECT_1                     0x38
#define WM8996_MIC_DETECT_2                     0x39
#define WM8996_MIC_DETECT_3                     0x3A
#define WM8996_CHARGE_PUMP_1                    0x40
#define WM8996_CHARGE_PUMP_2                    0x41
#define WM8996_DC_SERVO_1                       0x50
#define WM8996_DC_SERVO_2                       0x51
#define WM8996_DC_SERVO_3                       0x52
#define WM8996_DC_SERVO_5                       0x54
#define WM8996_DC_SERVO_6                       0x55
#define WM8996_DC_SERVO_7                       0x56
#define WM8996_DC_SERVO_READBACK_0              0x57
#define WM8996_ANALOGUE_HP_1                    0x60
#define WM8996_ANALOGUE_HP_2                    0x61
#define WM8996_CHIP_REVISION                    0x100
#define WM8996_CONTROL_INTERFACE_1              0x101
#define WM8996_WRITE_SEQUENCER_CTRL_1           0x110
#define WM8996_WRITE_SEQUENCER_CTRL_2           0x111
#define WM8996_AIF_CLOCKING_1                   0x200
#define WM8996_AIF_CLOCKING_2                   0x201
#define WM8996_CLOCKING_1                       0x208
#define WM8996_CLOCKING_2                       0x209
#define WM8996_AIF_RATE                         0x210
#define WM8996_FLL_CONTROL_1                    0x220
#define WM8996_FLL_CONTROL_2                    0x221
#define WM8996_FLL_CONTROL_3                    0x222
#define WM8996_FLL_CONTROL_4                    0x223
#define WM8996_FLL_CONTROL_5                    0x224
#define WM8996_FLL_CONTROL_6                    0x225
#define WM8996_FLL_EFS_1                        0x226
#define WM8996_FLL_EFS_2                        0x227
#define WM8996_AIF1_CONTROL                     0x300
#define WM8996_AIF1_BCLK                        0x301
#define WM8996_AIF1_TX_LRCLK_1                  0x302
#define WM8996_AIF1_TX_LRCLK_2                  0x303
#define WM8996_AIF1_RX_LRCLK_1                  0x304
#define WM8996_AIF1_RX_LRCLK_2                  0x305
#define WM8996_AIF1TX_DATA_CONFIGURATION_1      0x306
#define WM8996_AIF1TX_DATA_CONFIGURATION_2      0x307
#define WM8996_AIF1RX_DATA_CONFIGURATION        0x308
#define WM8996_AIF1TX_CHANNEL_0_CONFIGURATION   0x309
#define WM8996_AIF1TX_CHANNEL_1_CONFIGURATION   0x30A
#define WM8996_AIF1TX_CHANNEL_2_CONFIGURATION   0x30B
#define WM8996_AIF1TX_CHANNEL_3_CONFIGURATION   0x30C
#define WM8996_AIF1TX_CHANNEL_4_CONFIGURATION   0x30D
#define WM8996_AIF1TX_CHANNEL_5_CONFIGURATION   0x30E
#define WM8996_AIF1RX_CHANNEL_0_CONFIGURATION   0x30F
#define WM8996_AIF1RX_CHANNEL_1_CONFIGURATION   0x310
#define WM8996_AIF1RX_CHANNEL_2_CONFIGURATION   0x311
#define WM8996_AIF1RX_CHANNEL_3_CONFIGURATION   0x312
#define WM8996_AIF1RX_CHANNEL_4_CONFIGURATION   0x313
#define WM8996_AIF1RX_CHANNEL_5_CONFIGURATION   0x314
#define WM8996_AIF1RX_MONO_CONFIGURATION        0x315
#define WM8996_AIF1TX_TEST                      0x31A
#define WM8996_AIF2_CONTROL                     0x320
#define WM8996_AIF2_BCLK                        0x321
#define WM8996_AIF2_TX_LRCLK_1                  0x322
#define WM8996_AIF2_TX_LRCLK_2                  0x323
#define WM8996_AIF2_RX_LRCLK_1                  0x324
#define WM8996_AIF2_RX_LRCLK_2                  0x325
#define WM8996_AIF2TX_DATA_CONFIGURATION_1      0x326
#define WM8996_AIF2TX_DATA_CONFIGURATION_2      0x327
#define WM8996_AIF2RX_DATA_CONFIGURATION        0x328
#define WM8996_AIF2TX_CHANNEL_0_CONFIGURATION   0x329
#define WM8996_AIF2TX_CHANNEL_1_CONFIGURATION   0x32A
#define WM8996_AIF2RX_CHANNEL_0_CONFIGURATION   0x32B
#define WM8996_AIF2RX_CHANNEL_1_CONFIGURATION   0x32C
#define WM8996_AIF2RX_MONO_CONFIGURATION        0x32D
#define WM8996_AIF2TX_TEST                      0x32F
#define WM8996_DSP1_TX_LEFT_VOLUME              0x400
#define WM8996_DSP1_TX_RIGHT_VOLUME             0x401
#define WM8996_DSP1_RX_LEFT_VOLUME              0x402
#define WM8996_DSP1_RX_RIGHT_VOLUME             0x403
#define WM8996_DSP1_TX_FILTERS                  0x410
#define WM8996_DSP1_RX_FILTERS_1                0x420
#define WM8996_DSP1_RX_FILTERS_2                0x421
#define WM8996_DSP1_DRC_1                       0x440
#define WM8996_DSP1_DRC_2                       0x441
#define WM8996_DSP1_DRC_3                       0x442
#define WM8996_DSP1_DRC_4                       0x443
#define WM8996_DSP1_DRC_5                       0x444
#define WM8996_DSP1_RX_EQ_GAINS_1               0x480
#define WM8996_DSP1_RX_EQ_GAINS_2               0x481
#define WM8996_DSP1_RX_EQ_BAND_1_A              0x482
#define WM8996_DSP1_RX_EQ_BAND_1_B              0x483
#define WM8996_DSP1_RX_EQ_BAND_1_PG             0x484
#define WM8996_DSP1_RX_EQ_BAND_2_A              0x485
#define WM8996_DSP1_RX_EQ_BAND_2_B              0x486
#define WM8996_DSP1_RX_EQ_BAND_2_C              0x487
#define WM8996_DSP1_RX_EQ_BAND_2_PG             0x488
#define WM8996_DSP1_RX_EQ_BAND_3_A              0x489
#define WM8996_DSP1_RX_EQ_BAND_3_B              0x48A
#define WM8996_DSP1_RX_EQ_BAND_3_C              0x48B
#define WM8996_DSP1_RX_EQ_BAND_3_PG             0x48C
#define WM8996_DSP1_RX_EQ_BAND_4_A              0x48D
#define WM8996_DSP1_RX_EQ_BAND_4_B              0x48E
#define WM8996_DSP1_RX_EQ_BAND_4_C              0x48F
#define WM8996_DSP1_RX_EQ_BAND_4_PG             0x490
#define WM8996_DSP1_RX_EQ_BAND_5_A              0x491
#define WM8996_DSP1_RX_EQ_BAND_5_B              0x492
#define WM8996_DSP1_RX_EQ_BAND_5_PG             0x493
#define WM8996_DSP2_TX_LEFT_VOLUME              0x500
#define WM8996_DSP2_TX_RIGHT_VOLUME             0x501
#define WM8996_DSP2_RX_LEFT_VOLUME              0x502
#define WM8996_DSP2_RX_RIGHT_VOLUME             0x503
#define WM8996_DSP2_TX_FILTERS                  0x510
#define WM8996_DSP2_RX_FILTERS_1                0x520
#define WM8996_DSP2_RX_FILTERS_2                0x521
#define WM8996_DSP2_DRC_1                       0x540
#define WM8996_DSP2_DRC_2                       0x541
#define WM8996_DSP2_DRC_3                       0x542
#define WM8996_DSP2_DRC_4                       0x543
#define WM8996_DSP2_DRC_5                       0x544
#define WM8996_DSP2_RX_EQ_GAINS_1               0x580
#define WM8996_DSP2_RX_EQ_GAINS_2               0x581
#define WM8996_DSP2_RX_EQ_BAND_1_A              0x582
#define WM8996_DSP2_RX_EQ_BAND_1_B              0x583
#define WM8996_DSP2_RX_EQ_BAND_1_PG             0x584
#define WM8996_DSP2_RX_EQ_BAND_2_A              0x585
#define WM8996_DSP2_RX_EQ_BAND_2_B              0x586
#define WM8996_DSP2_RX_EQ_BAND_2_C              0x587
#define WM8996_DSP2_RX_EQ_BAND_2_PG             0x588
#define WM8996_DSP2_RX_EQ_BAND_3_A              0x589
#define WM8996_DSP2_RX_EQ_BAND_3_B              0x58A
#define WM8996_DSP2_RX_EQ_BAND_3_C              0x58B
#define WM8996_DSP2_RX_EQ_BAND_3_PG             0x58C
#define WM8996_DSP2_RX_EQ_BAND_4_A              0x58D
#define WM8996_DSP2_RX_EQ_BAND_4_B              0x58E
#define WM8996_DSP2_RX_EQ_BAND_4_C              0x58F
#define WM8996_DSP2_RX_EQ_BAND_4_PG             0x590
#define WM8996_DSP2_RX_EQ_BAND_5_A              0x591
#define WM8996_DSP2_RX_EQ_BAND_5_B              0x592
#define WM8996_DSP2_RX_EQ_BAND_5_PG             0x593
#define WM8996_DAC1_MIXER_VOLUMES               0x600
#define WM8996_DAC1_LEFT_MIXER_ROUTING          0x601
#define WM8996_DAC1_RIGHT_MIXER_ROUTING         0x602
#define WM8996_DAC2_MIXER_VOLUMES               0x603
#define WM8996_DAC2_LEFT_MIXER_ROUTING          0x604
#define WM8996_DAC2_RIGHT_MIXER_ROUTING         0x605
#define WM8996_DSP1_TX_LEFT_MIXER_ROUTING       0x606
#define WM8996_DSP1_TX_RIGHT_MIXER_ROUTING      0x607
#define WM8996_DSP2_TX_LEFT_MIXER_ROUTING       0x608
#define WM8996_DSP2_TX_RIGHT_MIXER_ROUTING      0x609
#define WM8996_DSP_TX_MIXER_SELECT              0x60A
#define WM8996_DAC_SOFTMUTE                     0x610
#define WM8996_OVERSAMPLING                     0x620
#define WM8996_SIDETONE                         0x621
#define WM8996_GPIO_1                           0x700
#define WM8996_GPIO_2                           0x701
#define WM8996_GPIO_3                           0x702
#define WM8996_GPIO_4                           0x703
#define WM8996_GPIO_5                           0x704
#define WM8996_PULL_CONTROL_1                   0x720
#define WM8996_PULL_CONTROL_2                   0x721
#define WM8996_INTERRUPT_STATUS_1               0x730
#define WM8996_INTERRUPT_STATUS_2               0x731
#define WM8996_INTERRUPT_RAW_STATUS_2           0x732
#define WM8996_INTERRUPT_STATUS_1_MASK          0x738
#define WM8996_INTERRUPT_STATUS_2_MASK          0x739
#define WM8996_INTERRUPT_CONTROL                0x740
#define WM8996_LEFT_PDM_SPEAKER                 0x800
#define WM8996_RIGHT_PDM_SPEAKER                0x801
#define WM8996_PDM_SPEAKER_MUTE_SEQUENCE        0x802
#define WM8996_PDM_SPEAKER_VOLUME               0x803
#define WM8996_WRITE_SEQUENCER_0                0x3000
#define WM8996_WRITE_SEQUENCER_1                0x3001
#define WM8996_WRITE_SEQUENCER_2                0x3002
#define WM8996_WRITE_SEQUENCER_3                0x3003
#define WM8996_WRITE_SEQUENCER_4                0x3004
#define WM8996_WRITE_SEQUENCER_5                0x3005
#define WM8996_WRITE_SEQUENCER_6                0x3006
#define WM8996_WRITE_SEQUENCER_7                0x3007
#define WM8996_WRITE_SEQUENCER_8                0x3008
#define WM8996_WRITE_SEQUENCER_9                0x3009
#define WM8996_WRITE_SEQUENCER_10               0x300A
#define WM8996_WRITE_SEQUENCER_11               0x300B
#define WM8996_WRITE_SEQUENCER_12               0x300C
#define WM8996_WRITE_SEQUENCER_13               0x300D
#define WM8996_WRITE_SEQUENCER_14               0x300E
#define WM8996_WRITE_SEQUENCER_15               0x300F
#define WM8996_WRITE_SEQUENCER_16               0x3010
#define WM8996_WRITE_SEQUENCER_17               0x3011
#define WM8996_WRITE_SEQUENCER_18               0x3012
#define WM8996_WRITE_SEQUENCER_19               0x3013
#define WM8996_WRITE_SEQUENCER_20               0x3014
#define WM8996_WRITE_SEQUENCER_21               0x3015
#define WM8996_WRITE_SEQUENCER_22               0x3016
#define WM8996_WRITE_SEQUENCER_23               0x3017
#define WM8996_WRITE_SEQUENCER_24               0x3018
#define WM8996_WRITE_SEQUENCER_25               0x3019
#define WM8996_WRITE_SEQUENCER_26               0x301A
#define WM8996_WRITE_SEQUENCER_27               0x301B
#define WM8996_WRITE_SEQUENCER_28               0x301C
#define WM8996_WRITE_SEQUENCER_29               0x301D
#define WM8996_WRITE_SEQUENCER_30               0x301E
#define WM8996_WRITE_SEQUENCER_31               0x301F
#define WM8996_WRITE_SEQUENCER_32               0x3020
#define WM8996_WRITE_SEQUENCER_33               0x3021
#define WM8996_WRITE_SEQUENCER_34               0x3022
#define WM8996_WRITE_SEQUENCER_35               0x3023
#define WM8996_WRITE_SEQUENCER_36               0x3024
#define WM8996_WRITE_SEQUENCER_37               0x3025
#define WM8996_WRITE_SEQUENCER_38               0x3026
#define WM8996_WRITE_SEQUENCER_39               0x3027
#define WM8996_WRITE_SEQUENCER_40               0x3028
#define WM8996_WRITE_SEQUENCER_41               0x3029
#define WM8996_WRITE_SEQUENCER_42               0x302A
#define WM8996_WRITE_SEQUENCER_43               0x302B
#define WM8996_WRITE_SEQUENCER_44               0x302C
#define WM8996_WRITE_SEQUENCER_45               0x302D
#define WM8996_WRITE_SEQUENCER_46               0x302E
#define WM8996_WRITE_SEQUENCER_47               0x302F
#define WM8996_WRITE_SEQUENCER_48               0x3030
#define WM8996_WRITE_SEQUENCER_49               0x3031
#define WM8996_WRITE_SEQUENCER_50               0x3032
#define WM8996_WRITE_SEQUENCER_51               0x3033
#define WM8996_WRITE_SEQUENCER_52               0x3034
#define WM8996_WRITE_SEQUENCER_53               0x3035
#define WM8996_WRITE_SEQUENCER_54               0x3036
#define WM8996_WRITE_SEQUENCER_55               0x3037
#define WM8996_WRITE_SEQUENCER_56               0x3038
#define WM8996_WRITE_SEQUENCER_57               0x3039
#define WM8996_WRITE_SEQUENCER_58               0x303A
#define WM8996_WRITE_SEQUENCER_59               0x303B
#define WM8996_WRITE_SEQUENCER_60               0x303C
#define WM8996_WRITE_SEQUENCER_61               0x303D
#define WM8996_WRITE_SEQUENCER_62               0x303E
#define WM8996_WRITE_SEQUENCER_63               0x303F
#define WM8996_WRITE_SEQUENCER_64               0x3040
#define WM8996_WRITE_SEQUENCER_65               0x3041
#define WM8996_WRITE_SEQUENCER_66               0x3042
#define WM8996_WRITE_SEQUENCER_67               0x3043
#define WM8996_WRITE_SEQUENCER_68               0x3044
#define WM8996_WRITE_SEQUENCER_69               0x3045
#define WM8996_WRITE_SEQUENCER_70               0x3046
#define WM8996_WRITE_SEQUENCER_71               0x3047
#define WM8996_WRITE_SEQUENCER_72               0x3048
#define WM8996_WRITE_SEQUENCER_73               0x3049
#define WM8996_WRITE_SEQUENCER_74               0x304A
#define WM8996_WRITE_SEQUENCER_75               0x304B
#define WM8996_WRITE_SEQUENCER_76               0x304C
#define WM8996_WRITE_SEQUENCER_77               0x304D
#define WM8996_WRITE_SEQUENCER_78               0x304E
#define WM8996_WRITE_SEQUENCER_79               0x304F
#define WM8996_WRITE_SEQUENCER_80               0x3050
#define WM8996_WRITE_SEQUENCER_81               0x3051
#define WM8996_WRITE_SEQUENCER_82               0x3052
#define WM8996_WRITE_SEQUENCER_83               0x3053
#define WM8996_WRITE_SEQUENCER_84               0x3054
#define WM8996_WRITE_SEQUENCER_85               0x3055
#define WM8996_WRITE_SEQUENCER_86               0x3056
#define WM8996_WRITE_SEQUENCER_87               0x3057
#define WM8996_WRITE_SEQUENCER_88               0x3058
#define WM8996_WRITE_SEQUENCER_89               0x3059
#define WM8996_WRITE_SEQUENCER_90               0x305A
#define WM8996_WRITE_SEQUENCER_91               0x305B
#define WM8996_WRITE_SEQUENCER_92               0x305C
#define WM8996_WRITE_SEQUENCER_93               0x305D
#define WM8996_WRITE_SEQUENCER_94               0x305E
#define WM8996_WRITE_SEQUENCER_95               0x305F
#define WM8996_WRITE_SEQUENCER_96               0x3060
#define WM8996_WRITE_SEQUENCER_97               0x3061
#define WM8996_WRITE_SEQUENCER_98               0x3062
#define WM8996_WRITE_SEQUENCER_99               0x3063
#define WM8996_WRITE_SEQUENCER_100              0x3064
#define WM8996_WRITE_SEQUENCER_101              0x3065
#define WM8996_WRITE_SEQUENCER_102              0x3066
#define WM8996_WRITE_SEQUENCER_103              0x3067
#define WM8996_WRITE_SEQUENCER_104              0x3068
#define WM8996_WRITE_SEQUENCER_105              0x3069
#define WM8996_WRITE_SEQUENCER_106              0x306A
#define WM8996_WRITE_SEQUENCER_107              0x306B
#define WM8996_WRITE_SEQUENCER_108              0x306C
#define WM8996_WRITE_SEQUENCER_109              0x306D
#define WM8996_WRITE_SEQUENCER_110              0x306E
#define WM8996_WRITE_SEQUENCER_111              0x306F
#define WM8996_WRITE_SEQUENCER_112              0x3070
#define WM8996_WRITE_SEQUENCER_113              0x3071
#define WM8996_WRITE_SEQUENCER_114              0x3072
#define WM8996_WRITE_SEQUENCER_115              0x3073
#define WM8996_WRITE_SEQUENCER_116              0x3074
#define WM8996_WRITE_SEQUENCER_117              0x3075
#define WM8996_WRITE_SEQUENCER_118              0x3076
#define WM8996_WRITE_SEQUENCER_119              0x3077
#define WM8996_WRITE_SEQUENCER_120              0x3078
#define WM8996_WRITE_SEQUENCER_121              0x3079
#define WM8996_WRITE_SEQUENCER_122              0x307A
#define WM8996_WRITE_SEQUENCER_123              0x307B
#define WM8996_WRITE_SEQUENCER_124              0x307C
#define WM8996_WRITE_SEQUENCER_125              0x307D
#define WM8996_WRITE_SEQUENCER_126              0x307E
#define WM8996_WRITE_SEQUENCER_127              0x307F
#define WM8996_WRITE_SEQUENCER_128              0x3080
#define WM8996_WRITE_SEQUENCER_129              0x3081
#define WM8996_WRITE_SEQUENCER_130              0x3082
#define WM8996_WRITE_SEQUENCER_131              0x3083
#define WM8996_WRITE_SEQUENCER_132              0x3084
#define WM8996_WRITE_SEQUENCER_133              0x3085
#define WM8996_WRITE_SEQUENCER_134              0x3086
#define WM8996_WRITE_SEQUENCER_135              0x3087
#define WM8996_WRITE_SEQUENCER_136              0x3088
#define WM8996_WRITE_SEQUENCER_137              0x3089
#define WM8996_WRITE_SEQUENCER_138              0x308A
#define WM8996_WRITE_SEQUENCER_139              0x308B
#define WM8996_WRITE_SEQUENCER_140              0x308C
#define WM8996_WRITE_SEQUENCER_141              0x308D
#define WM8996_WRITE_SEQUENCER_142              0x308E
#define WM8996_WRITE_SEQUENCER_143              0x308F
#define WM8996_WRITE_SEQUENCER_144              0x3090
#define WM8996_WRITE_SEQUENCER_145              0x3091
#define WM8996_WRITE_SEQUENCER_146              0x3092
#define WM8996_WRITE_SEQUENCER_147              0x3093
#define WM8996_WRITE_SEQUENCER_148              0x3094
#define WM8996_WRITE_SEQUENCER_149              0x3095
#define WM8996_WRITE_SEQUENCER_150              0x3096
#define WM8996_WRITE_SEQUENCER_151              0x3097
#define WM8996_WRITE_SEQUENCER_152              0x3098
#define WM8996_WRITE_SEQUENCER_153              0x3099
#define WM8996_WRITE_SEQUENCER_154              0x309A
#define WM8996_WRITE_SEQUENCER_155              0x309B
#define WM8996_WRITE_SEQUENCER_156              0x309C
#define WM8996_WRITE_SEQUENCER_157              0x309D
#define WM8996_WRITE_SEQUENCER_158              0x309E
#define WM8996_WRITE_SEQUENCER_159              0x309F
#define WM8996_WRITE_SEQUENCER_160              0x30A0
#define WM8996_WRITE_SEQUENCER_161              0x30A1
#define WM8996_WRITE_SEQUENCER_162              0x30A2
#define WM8996_WRITE_SEQUENCER_163              0x30A3
#define WM8996_WRITE_SEQUENCER_164              0x30A4
#define WM8996_WRITE_SEQUENCER_165              0x30A5
#define WM8996_WRITE_SEQUENCER_166              0x30A6
#define WM8996_WRITE_SEQUENCER_167              0x30A7
#define WM8996_WRITE_SEQUENCER_168              0x30A8
#define WM8996_WRITE_SEQUENCER_169              0x30A9
#define WM8996_WRITE_SEQUENCER_170              0x30AA
#define WM8996_WRITE_SEQUENCER_171              0x30AB
#define WM8996_WRITE_SEQUENCER_172              0x30AC
#define WM8996_WRITE_SEQUENCER_173              0x30AD
#define WM8996_WRITE_SEQUENCER_174              0x30AE
#define WM8996_WRITE_SEQUENCER_175              0x30AF
#define WM8996_WRITE_SEQUENCER_176              0x30B0
#define WM8996_WRITE_SEQUENCER_177              0x30B1
#define WM8996_WRITE_SEQUENCER_178              0x30B2
#define WM8996_WRITE_SEQUENCER_179              0x30B3
#define WM8996_WRITE_SEQUENCER_180              0x30B4
#define WM8996_WRITE_SEQUENCER_181              0x30B5
#define WM8996_WRITE_SEQUENCER_182              0x30B6
#define WM8996_WRITE_SEQUENCER_183              0x30B7
#define WM8996_WRITE_SEQUENCER_184              0x30B8
#define WM8996_WRITE_SEQUENCER_185              0x30B9
#define WM8996_WRITE_SEQUENCER_186              0x30BA
#define WM8996_WRITE_SEQUENCER_187              0x30BB
#define WM8996_WRITE_SEQUENCER_188              0x30BC
#define WM8996_WRITE_SEQUENCER_189              0x30BD
#define WM8996_WRITE_SEQUENCER_190              0x30BE
#define WM8996_WRITE_SEQUENCER_191              0x30BF
#define WM8996_WRITE_SEQUENCER_192              0x30C0
#define WM8996_WRITE_SEQUENCER_193              0x30C1
#define WM8996_WRITE_SEQUENCER_194              0x30C2
#define WM8996_WRITE_SEQUENCER_195              0x30C3
#define WM8996_WRITE_SEQUENCER_196              0x30C4
#define WM8996_WRITE_SEQUENCER_197              0x30C5
#define WM8996_WRITE_SEQUENCER_198              0x30C6
#define WM8996_WRITE_SEQUENCER_199              0x30C7
#define WM8996_WRITE_SEQUENCER_200              0x30C8
#define WM8996_WRITE_SEQUENCER_201              0x30C9
#define WM8996_WRITE_SEQUENCER_202              0x30CA
#define WM8996_WRITE_SEQUENCER_203              0x30CB
#define WM8996_WRITE_SEQUENCER_204              0x30CC
#define WM8996_WRITE_SEQUENCER_205              0x30CD
#define WM8996_WRITE_SEQUENCER_206              0x30CE
#define WM8996_WRITE_SEQUENCER_207              0x30CF
#define WM8996_WRITE_SEQUENCER_208              0x30D0
#define WM8996_WRITE_SEQUENCER_209              0x30D1
#define WM8996_WRITE_SEQUENCER_210              0x30D2
#define WM8996_WRITE_SEQUENCER_211              0x30D3
#define WM8996_WRITE_SEQUENCER_212              0x30D4
#define WM8996_WRITE_SEQUENCER_213              0x30D5
#define WM8996_WRITE_SEQUENCER_214              0x30D6
#define WM8996_WRITE_SEQUENCER_215              0x30D7
#define WM8996_WRITE_SEQUENCER_216              0x30D8
#define WM8996_WRITE_SEQUENCER_217              0x30D9
#define WM8996_WRITE_SEQUENCER_218              0x30DA
#define WM8996_WRITE_SEQUENCER_219              0x30DB
#define WM8996_WRITE_SEQUENCER_220              0x30DC
#define WM8996_WRITE_SEQUENCER_221              0x30DD
#define WM8996_WRITE_SEQUENCER_222              0x30DE
#define WM8996_WRITE_SEQUENCER_223              0x30DF
#define WM8996_WRITE_SEQUENCER_224              0x30E0
#define WM8996_WRITE_SEQUENCER_225              0x30E1
#define WM8996_WRITE_SEQUENCER_226              0x30E2
#define WM8996_WRITE_SEQUENCER_227              0x30E3
#define WM8996_WRITE_SEQUENCER_228              0x30E4
#define WM8996_WRITE_SEQUENCER_229              0x30E5
#define WM8996_WRITE_SEQUENCER_230              0x30E6
#define WM8996_WRITE_SEQUENCER_231              0x30E7
#define WM8996_WRITE_SEQUENCER_232              0x30E8
#define WM8996_WRITE_SEQUENCER_233              0x30E9
#define WM8996_WRITE_SEQUENCER_234              0x30EA
#define WM8996_WRITE_SEQUENCER_235              0x30EB
#define WM8996_WRITE_SEQUENCER_236              0x30EC
#define WM8996_WRITE_SEQUENCER_237              0x30ED
#define WM8996_WRITE_SEQUENCER_238              0x30EE
#define WM8996_WRITE_SEQUENCER_239              0x30EF
#define WM8996_WRITE_SEQUENCER_240              0x30F0
#define WM8996_WRITE_SEQUENCER_241              0x30F1
#define WM8996_WRITE_SEQUENCER_242              0x30F2
#define WM8996_WRITE_SEQUENCER_243              0x30F3
#define WM8996_WRITE_SEQUENCER_244              0x30F4
#define WM8996_WRITE_SEQUENCER_245              0x30F5
#define WM8996_WRITE_SEQUENCER_246              0x30F6
#define WM8996_WRITE_SEQUENCER_247              0x30F7
#define WM8996_WRITE_SEQUENCER_248              0x30F8
#define WM8996_WRITE_SEQUENCER_249              0x30F9
#define WM8996_WRITE_SEQUENCER_250              0x30FA
#define WM8996_WRITE_SEQUENCER_251              0x30FB
#define WM8996_WRITE_SEQUENCER_252              0x30FC
#define WM8996_WRITE_SEQUENCER_253              0x30FD
#define WM8996_WRITE_SEQUENCER_254              0x30FE
#define WM8996_WRITE_SEQUENCER_255              0x30FF
#define WM8996_WRITE_SEQUENCER_256              0x3100
#define WM8996_WRITE_SEQUENCER_257              0x3101
#define WM8996_WRITE_SEQUENCER_258              0x3102
#define WM8996_WRITE_SEQUENCER_259              0x3103
#define WM8996_WRITE_SEQUENCER_260              0x3104
#define WM8996_WRITE_SEQUENCER_261              0x3105
#define WM8996_WRITE_SEQUENCER_262              0x3106
#define WM8996_WRITE_SEQUENCER_263              0x3107
#define WM8996_WRITE_SEQUENCER_264              0x3108
#define WM8996_WRITE_SEQUENCER_265              0x3109
#define WM8996_WRITE_SEQUENCER_266              0x310A
#define WM8996_WRITE_SEQUENCER_267              0x310B
#define WM8996_WRITE_SEQUENCER_268              0x310C
#define WM8996_WRITE_SEQUENCER_269              0x310D
#define WM8996_WRITE_SEQUENCER_270              0x310E
#define WM8996_WRITE_SEQUENCER_271              0x310F
#define WM8996_WRITE_SEQUENCER_272              0x3110
#define WM8996_WRITE_SEQUENCER_273              0x3111
#define WM8996_WRITE_SEQUENCER_274              0x3112
#define WM8996_WRITE_SEQUENCER_275              0x3113
#define WM8996_WRITE_SEQUENCER_276              0x3114
#define WM8996_WRITE_SEQUENCER_277              0x3115
#define WM8996_WRITE_SEQUENCER_278              0x3116
#define WM8996_WRITE_SEQUENCER_279              0x3117
#define WM8996_WRITE_SEQUENCER_280              0x3118
#define WM8996_WRITE_SEQUENCER_281              0x3119
#define WM8996_WRITE_SEQUENCER_282              0x311A
#define WM8996_WRITE_SEQUENCER_283              0x311B
#define WM8996_WRITE_SEQUENCER_284              0x311C
#define WM8996_WRITE_SEQUENCER_285              0x311D
#define WM8996_WRITE_SEQUENCER_286              0x311E
#define WM8996_WRITE_SEQUENCER_287              0x311F
#define WM8996_WRITE_SEQUENCER_288              0x3120
#define WM8996_WRITE_SEQUENCER_289              0x3121
#define WM8996_WRITE_SEQUENCER_290              0x3122
#define WM8996_WRITE_SEQUENCER_291              0x3123
#define WM8996_WRITE_SEQUENCER_292              0x3124
#define WM8996_WRITE_SEQUENCER_293              0x3125
#define WM8996_WRITE_SEQUENCER_294              0x3126
#define WM8996_WRITE_SEQUENCER_295              0x3127
#define WM8996_WRITE_SEQUENCER_296              0x3128
#define WM8996_WRITE_SEQUENCER_297              0x3129
#define WM8996_WRITE_SEQUENCER_298              0x312A
#define WM8996_WRITE_SEQUENCER_299              0x312B
#define WM8996_WRITE_SEQUENCER_300              0x312C
#define WM8996_WRITE_SEQUENCER_301              0x312D
#define WM8996_WRITE_SEQUENCER_302              0x312E
#define WM8996_WRITE_SEQUENCER_303              0x312F
#define WM8996_WRITE_SEQUENCER_304              0x3130
#define WM8996_WRITE_SEQUENCER_305              0x3131
#define WM8996_WRITE_SEQUENCER_306              0x3132
#define WM8996_WRITE_SEQUENCER_307              0x3133
#define WM8996_WRITE_SEQUENCER_308              0x3134
#define WM8996_WRITE_SEQUENCER_309              0x3135
#define WM8996_WRITE_SEQUENCER_310              0x3136
#define WM8996_WRITE_SEQUENCER_311              0x3137
#define WM8996_WRITE_SEQUENCER_312              0x3138
#define WM8996_WRITE_SEQUENCER_313              0x3139
#define WM8996_WRITE_SEQUENCER_314              0x313A
#define WM8996_WRITE_SEQUENCER_315              0x313B
#define WM8996_WRITE_SEQUENCER_316              0x313C
#define WM8996_WRITE_SEQUENCER_317              0x313D
#define WM8996_WRITE_SEQUENCER_318              0x313E
#define WM8996_WRITE_SEQUENCER_319              0x313F
#define WM8996_WRITE_SEQUENCER_320              0x3140
#define WM8996_WRITE_SEQUENCER_321              0x3141
#define WM8996_WRITE_SEQUENCER_322              0x3142
#define WM8996_WRITE_SEQUENCER_323              0x3143
#define WM8996_WRITE_SEQUENCER_324              0x3144
#define WM8996_WRITE_SEQUENCER_325              0x3145
#define WM8996_WRITE_SEQUENCER_326              0x3146
#define WM8996_WRITE_SEQUENCER_327              0x3147
#define WM8996_WRITE_SEQUENCER_328              0x3148
#define WM8996_WRITE_SEQUENCER_329              0x3149
#define WM8996_WRITE_SEQUENCER_330              0x314A
#define WM8996_WRITE_SEQUENCER_331              0x314B
#define WM8996_WRITE_SEQUENCER_332              0x314C
#define WM8996_WRITE_SEQUENCER_333              0x314D
#define WM8996_WRITE_SEQUENCER_334              0x314E
#define WM8996_WRITE_SEQUENCER_335              0x314F
#define WM8996_WRITE_SEQUENCER_336              0x3150
#define WM8996_WRITE_SEQUENCER_337              0x3151
#define WM8996_WRITE_SEQUENCER_338              0x3152
#define WM8996_WRITE_SEQUENCER_339              0x3153
#define WM8996_WRITE_SEQUENCER_340              0x3154
#define WM8996_WRITE_SEQUENCER_341              0x3155
#define WM8996_WRITE_SEQUENCER_342              0x3156
#define WM8996_WRITE_SEQUENCER_343              0x3157
#define WM8996_WRITE_SEQUENCER_344              0x3158
#define WM8996_WRITE_SEQUENCER_345              0x3159
#define WM8996_WRITE_SEQUENCER_346              0x315A
#define WM8996_WRITE_SEQUENCER_347              0x315B
#define WM8996_WRITE_SEQUENCER_348              0x315C
#define WM8996_WRITE_SEQUENCER_349              0x315D
#define WM8996_WRITE_SEQUENCER_350              0x315E
#define WM8996_WRITE_SEQUENCER_351              0x315F
#define WM8996_WRITE_SEQUENCER_352              0x3160
#define WM8996_WRITE_SEQUENCER_353              0x3161
#define WM8996_WRITE_SEQUENCER_354              0x3162
#define WM8996_WRITE_SEQUENCER_355              0x3163
#define WM8996_WRITE_SEQUENCER_356              0x3164
#define WM8996_WRITE_SEQUENCER_357              0x3165
#define WM8996_WRITE_SEQUENCER_358              0x3166
#define WM8996_WRITE_SEQUENCER_359              0x3167
#define WM8996_WRITE_SEQUENCER_360              0x3168
#define WM8996_WRITE_SEQUENCER_361              0x3169
#define WM8996_WRITE_SEQUENCER_362              0x316A
#define WM8996_WRITE_SEQUENCER_363              0x316B
#define WM8996_WRITE_SEQUENCER_364              0x316C
#define WM8996_WRITE_SEQUENCER_365              0x316D
#define WM8996_WRITE_SEQUENCER_366              0x316E
#define WM8996_WRITE_SEQUENCER_367              0x316F
#define WM8996_WRITE_SEQUENCER_368              0x3170
#define WM8996_WRITE_SEQUENCER_369              0x3171
#define WM8996_WRITE_SEQUENCER_370              0x3172
#define WM8996_WRITE_SEQUENCER_371              0x3173
#define WM8996_WRITE_SEQUENCER_372              0x3174
#define WM8996_WRITE_SEQUENCER_373              0x3175
#define WM8996_WRITE_SEQUENCER_374              0x3176
#define WM8996_WRITE_SEQUENCER_375              0x3177
#define WM8996_WRITE_SEQUENCER_376              0x3178
#define WM8996_WRITE_SEQUENCER_377              0x3179
#define WM8996_WRITE_SEQUENCER_378              0x317A
#define WM8996_WRITE_SEQUENCER_379              0x317B
#define WM8996_WRITE_SEQUENCER_380              0x317C
#define WM8996_WRITE_SEQUENCER_381              0x317D
#define WM8996_WRITE_SEQUENCER_382              0x317E
#define WM8996_WRITE_SEQUENCER_383              0x317F
#define WM8996_WRITE_SEQUENCER_384              0x3180
#define WM8996_WRITE_SEQUENCER_385              0x3181
#define WM8996_WRITE_SEQUENCER_386              0x3182
#define WM8996_WRITE_SEQUENCER_387              0x3183
#define WM8996_WRITE_SEQUENCER_388              0x3184
#define WM8996_WRITE_SEQUENCER_389              0x3185
#define WM8996_WRITE_SEQUENCER_390              0x3186
#define WM8996_WRITE_SEQUENCER_391              0x3187
#define WM8996_WRITE_SEQUENCER_392              0x3188
#define WM8996_WRITE_SEQUENCER_393              0x3189
#define WM8996_WRITE_SEQUENCER_394              0x318A
#define WM8996_WRITE_SEQUENCER_395              0x318B
#define WM8996_WRITE_SEQUENCER_396              0x318C
#define WM8996_WRITE_SEQUENCER_397              0x318D
#define WM8996_WRITE_SEQUENCER_398              0x318E
#define WM8996_WRITE_SEQUENCER_399              0x318F
#define WM8996_WRITE_SEQUENCER_400              0x3190
#define WM8996_WRITE_SEQUENCER_401              0x3191
#define WM8996_WRITE_SEQUENCER_402              0x3192
#define WM8996_WRITE_SEQUENCER_403              0x3193
#define WM8996_WRITE_SEQUENCER_404              0x3194
#define WM8996_WRITE_SEQUENCER_405              0x3195
#define WM8996_WRITE_SEQUENCER_406              0x3196
#define WM8996_WRITE_SEQUENCER_407              0x3197
#define WM8996_WRITE_SEQUENCER_408              0x3198
#define WM8996_WRITE_SEQUENCER_409              0x3199
#define WM8996_WRITE_SEQUENCER_410              0x319A
#define WM8996_WRITE_SEQUENCER_411              0x319B
#define WM8996_WRITE_SEQUENCER_412              0x319C
#define WM8996_WRITE_SEQUENCER_413              0x319D
#define WM8996_WRITE_SEQUENCER_414              0x319E
#define WM8996_WRITE_SEQUENCER_415              0x319F
#define WM8996_WRITE_SEQUENCER_416              0x31A0
#define WM8996_WRITE_SEQUENCER_417              0x31A1
#define WM8996_WRITE_SEQUENCER_418              0x31A2
#define WM8996_WRITE_SEQUENCER_419              0x31A3
#define WM8996_WRITE_SEQUENCER_420              0x31A4
#define WM8996_WRITE_SEQUENCER_421              0x31A5
#define WM8996_WRITE_SEQUENCER_422              0x31A6
#define WM8996_WRITE_SEQUENCER_423              0x31A7
#define WM8996_WRITE_SEQUENCER_424              0x31A8
#define WM8996_WRITE_SEQUENCER_425              0x31A9
#define WM8996_WRITE_SEQUENCER_426              0x31AA
#define WM8996_WRITE_SEQUENCER_427              0x31AB
#define WM8996_WRITE_SEQUENCER_428              0x31AC
#define WM8996_WRITE_SEQUENCER_429              0x31AD
#define WM8996_WRITE_SEQUENCER_430              0x31AE
#define WM8996_WRITE_SEQUENCER_431              0x31AF
#define WM8996_WRITE_SEQUENCER_432              0x31B0
#define WM8996_WRITE_SEQUENCER_433              0x31B1
#define WM8996_WRITE_SEQUENCER_434              0x31B2
#define WM8996_WRITE_SEQUENCER_435              0x31B3
#define WM8996_WRITE_SEQUENCER_436              0x31B4
#define WM8996_WRITE_SEQUENCER_437              0x31B5
#define WM8996_WRITE_SEQUENCER_438              0x31B6
#define WM8996_WRITE_SEQUENCER_439              0x31B7
#define WM8996_WRITE_SEQUENCER_440              0x31B8
#define WM8996_WRITE_SEQUENCER_441              0x31B9
#define WM8996_WRITE_SEQUENCER_442              0x31BA
#define WM8996_WRITE_SEQUENCER_443              0x31BB
#define WM8996_WRITE_SEQUENCER_444              0x31BC
#define WM8996_WRITE_SEQUENCER_445              0x31BD
#define WM8996_WRITE_SEQUENCER_446              0x31BE
#define WM8996_WRITE_SEQUENCER_447              0x31BF
#define WM8996_WRITE_SEQUENCER_448              0x31C0
#define WM8996_WRITE_SEQUENCER_449              0x31C1
#define WM8996_WRITE_SEQUENCER_450              0x31C2
#define WM8996_WRITE_SEQUENCER_451              0x31C3
#define WM8996_WRITE_SEQUENCER_452              0x31C4
#define WM8996_WRITE_SEQUENCER_453              0x31C5
#define WM8996_WRITE_SEQUENCER_454              0x31C6
#define WM8996_WRITE_SEQUENCER_455              0x31C7
#define WM8996_WRITE_SEQUENCER_456              0x31C8
#define WM8996_WRITE_SEQUENCER_457              0x31C9
#define WM8996_WRITE_SEQUENCER_458              0x31CA
#define WM8996_WRITE_SEQUENCER_459              0x31CB
#define WM8996_WRITE_SEQUENCER_460              0x31CC
#define WM8996_WRITE_SEQUENCER_461              0x31CD
#define WM8996_WRITE_SEQUENCER_462              0x31CE
#define WM8996_WRITE_SEQUENCER_463              0x31CF
#define WM8996_WRITE_SEQUENCER_464              0x31D0
#define WM8996_WRITE_SEQUENCER_465              0x31D1
#define WM8996_WRITE_SEQUENCER_466              0x31D2
#define WM8996_WRITE_SEQUENCER_467              0x31D3
#define WM8996_WRITE_SEQUENCER_468              0x31D4
#define WM8996_WRITE_SEQUENCER_469              0x31D5
#define WM8996_WRITE_SEQUENCER_470              0x31D6
#define WM8996_WRITE_SEQUENCER_471              0x31D7
#define WM8996_WRITE_SEQUENCER_472              0x31D8
#define WM8996_WRITE_SEQUENCER_473              0x31D9
#define WM8996_WRITE_SEQUENCER_474              0x31DA
#define WM8996_WRITE_SEQUENCER_475              0x31DB
#define WM8996_WRITE_SEQUENCER_476              0x31DC
#define WM8996_WRITE_SEQUENCER_477              0x31DD
#define WM8996_WRITE_SEQUENCER_478              0x31DE
#define WM8996_WRITE_SEQUENCER_479              0x31DF
#define WM8996_WRITE_SEQUENCER_480              0x31E0
#define WM8996_WRITE_SEQUENCER_481              0x31E1
#define WM8996_WRITE_SEQUENCER_482              0x31E2
#define WM8996_WRITE_SEQUENCER_483              0x31E3
#define WM8996_WRITE_SEQUENCER_484              0x31E4
#define WM8996_WRITE_SEQUENCER_485              0x31E5
#define WM8996_WRITE_SEQUENCER_486              0x31E6
#define WM8996_WRITE_SEQUENCER_487              0x31E7
#define WM8996_WRITE_SEQUENCER_488              0x31E8
#define WM8996_WRITE_SEQUENCER_489              0x31E9
#define WM8996_WRITE_SEQUENCER_490              0x31EA
#define WM8996_WRITE_SEQUENCER_491              0x31EB
#define WM8996_WRITE_SEQUENCER_492              0x31EC
#define WM8996_WRITE_SEQUENCER_493              0x31ED
#define WM8996_WRITE_SEQUENCER_494              0x31EE
#define WM8996_WRITE_SEQUENCER_495              0x31EF
#define WM8996_WRITE_SEQUENCER_496              0x31F0
#define WM8996_WRITE_SEQUENCER_497              0x31F1
#define WM8996_WRITE_SEQUENCER_498              0x31F2
#define WM8996_WRITE_SEQUENCER_499              0x31F3
#define WM8996_WRITE_SEQUENCER_500              0x31F4
#define WM8996_WRITE_SEQUENCER_501              0x31F5
#define WM8996_WRITE_SEQUENCER_502              0x31F6
#define WM8996_WRITE_SEQUENCER_503              0x31F7
#define WM8996_WRITE_SEQUENCER_504              0x31F8
#define WM8996_WRITE_SEQUENCER_505              0x31F9
#define WM8996_WRITE_SEQUENCER_506              0x31FA
#define WM8996_WRITE_SEQUENCER_507              0x31FB
#define WM8996_WRITE_SEQUENCER_508              0x31FC
#define WM8996_WRITE_SEQUENCER_509              0x31FD
#define WM8996_WRITE_SEQUENCER_510              0x31FE
#define WM8996_WRITE_SEQUENCER_511              0x31FF

#define WM8996_REGISTER_COUNT                   706
#define WM8996_MAX_REGISTER                     0x31FF

/*
 * Field Definitions.
 */

/*
 * R0 (0x00) - Software Reset
 */
#define WM8996_SW_RESET_MASK                    0xFFFF  /* SW_RESET - [15:0] */
#define WM8996_SW_RESET_SHIFT                        0  /* SW_RESET - [15:0] */
#define WM8996_SW_RESET_WIDTH                       16  /* SW_RESET - [15:0] */

/*
 * R1 (0x01) - Power Management (1)
 */
#define WM8996_MICB2_ENA                        0x0200  /* MICB2_ENA */
#define WM8996_MICB2_ENA_MASK                   0x0200  /* MICB2_ENA */
#define WM8996_MICB2_ENA_SHIFT                       9  /* MICB2_ENA */
#define WM8996_MICB2_ENA_WIDTH                       1  /* MICB2_ENA */
#define WM8996_MICB1_ENA                        0x0100  /* MICB1_ENA */
#define WM8996_MICB1_ENA_MASK                   0x0100  /* MICB1_ENA */
#define WM8996_MICB1_ENA_SHIFT                       8  /* MICB1_ENA */
#define WM8996_MICB1_ENA_WIDTH                       1  /* MICB1_ENA */
#define WM8996_HPOUT2L_ENA                      0x0080  /* HPOUT2L_ENA */
#define WM8996_HPOUT2L_ENA_MASK                 0x0080  /* HPOUT2L_ENA */
#define WM8996_HPOUT2L_ENA_SHIFT                     7  /* HPOUT2L_ENA */
#define WM8996_HPOUT2L_ENA_WIDTH                     1  /* HPOUT2L_ENA */
#define WM8996_HPOUT2R_ENA                      0x0040  /* HPOUT2R_ENA */
#define WM8996_HPOUT2R_ENA_MASK                 0x0040  /* HPOUT2R_ENA */
#define WM8996_HPOUT2R_ENA_SHIFT                     6  /* HPOUT2R_ENA */
#define WM8996_HPOUT2R_ENA_WIDTH                     1  /* HPOUT2R_ENA */
#define WM8996_HPOUT1L_ENA                      0x0020  /* HPOUT1L_ENA */
#define WM8996_HPOUT1L_ENA_MASK                 0x0020  /* HPOUT1L_ENA */
#define WM8996_HPOUT1L_ENA_SHIFT                     5  /* HPOUT1L_ENA */
#define WM8996_HPOUT1L_ENA_WIDTH                     1  /* HPOUT1L_ENA */
#define WM8996_HPOUT1R_ENA                      0x0010  /* HPOUT1R_ENA */
#define WM8996_HPOUT1R_ENA_MASK                 0x0010  /* HPOUT1R_ENA */
#define WM8996_HPOUT1R_ENA_SHIFT                     4  /* HPOUT1R_ENA */
#define WM8996_HPOUT1R_ENA_WIDTH                     1  /* HPOUT1R_ENA */
#define WM8996_BG_ENA                           0x0001  /* BG_ENA */
#define WM8996_BG_ENA_MASK                      0x0001  /* BG_ENA */
#define WM8996_BG_ENA_SHIFT                          0  /* BG_ENA */
#define WM8996_BG_ENA_WIDTH                          1  /* BG_ENA */

/*
 * R2 (0x02) - Power Management (2)
 */
#define WM8996_OPCLK_ENA                        0x0800  /* OPCLK_ENA */
#define WM8996_OPCLK_ENA_MASK                   0x0800  /* OPCLK_ENA */
#define WM8996_OPCLK_ENA_SHIFT                      11  /* OPCLK_ENA */
#define WM8996_OPCLK_ENA_WIDTH                       1  /* OPCLK_ENA */
#define WM8996_INL_ENA                          0x0020  /* INL_ENA */
#define WM8996_INL_ENA_MASK                     0x0020  /* INL_ENA */
#define WM8996_INL_ENA_SHIFT                         5  /* INL_ENA */
#define WM8996_INL_ENA_WIDTH                         1  /* INL_ENA */
#define WM8996_INR_ENA                          0x0010  /* INR_ENA */
#define WM8996_INR_ENA_MASK                     0x0010  /* INR_ENA */
#define WM8996_INR_ENA_SHIFT                         4  /* INR_ENA */
#define WM8996_INR_ENA_WIDTH                         1  /* INR_ENA */
#define WM8996_LDO2_ENA                         0x0002  /* LDO2_ENA */
#define WM8996_LDO2_ENA_MASK                    0x0002  /* LDO2_ENA */
#define WM8996_LDO2_ENA_SHIFT                        1  /* LDO2_ENA */
#define WM8996_LDO2_ENA_WIDTH                        1  /* LDO2_ENA */

/*
 * R3 (0x03) - Power Management (3)
 */
#define WM8996_DSP2RXL_ENA                      0x0800  /* DSP2RXL_ENA */
#define WM8996_DSP2RXL_ENA_MASK                 0x0800  /* DSP2RXL_ENA */
#define WM8996_DSP2RXL_ENA_SHIFT                    11  /* DSP2RXL_ENA */
#define WM8996_DSP2RXL_ENA_WIDTH                     1  /* DSP2RXL_ENA */
#define WM8996_DSP2RXR_ENA                      0x0400  /* DSP2RXR_ENA */
#define WM8996_DSP2RXR_ENA_MASK                 0x0400  /* DSP2RXR_ENA */
#define WM8996_DSP2RXR_ENA_SHIFT                    10  /* DSP2RXR_ENA */
#define WM8996_DSP2RXR_ENA_WIDTH                     1  /* DSP2RXR_ENA */
#define WM8996_DSP1RXL_ENA                      0x0200  /* DSP1RXL_ENA */
#define WM8996_DSP1RXL_ENA_MASK                 0x0200  /* DSP1RXL_ENA */
#define WM8996_DSP1RXL_ENA_SHIFT                     9  /* DSP1RXL_ENA */
#define WM8996_DSP1RXL_ENA_WIDTH                     1  /* DSP1RXL_ENA */
#define WM8996_DSP1RXR_ENA                      0x0100  /* DSP1RXR_ENA */
#define WM8996_DSP1RXR_ENA_MASK                 0x0100  /* DSP1RXR_ENA */
#define WM8996_DSP1RXR_ENA_SHIFT                     8  /* DSP1RXR_ENA */
#define WM8996_DSP1RXR_ENA_WIDTH                     1  /* DSP1RXR_ENA */
#define WM8996_DMIC2L_ENA                       0x0020  /* DMIC2L_ENA */
#define WM8996_DMIC2L_ENA_MASK                  0x0020  /* DMIC2L_ENA */
#define WM8996_DMIC2L_ENA_SHIFT                      5  /* DMIC2L_ENA */
#define WM8996_DMIC2L_ENA_WIDTH                      1  /* DMIC2L_ENA */
#define WM8996_DMIC2R_ENA                       0x0010  /* DMIC2R_ENA */
#define WM8996_DMIC2R_ENA_MASK                  0x0010  /* DMIC2R_ENA */
#define WM8996_DMIC2R_ENA_SHIFT                      4  /* DMIC2R_ENA */
#define WM8996_DMIC2R_ENA_WIDTH                      1  /* DMIC2R_ENA */
#define WM8996_DMIC1L_ENA                       0x0008  /* DMIC1L_ENA */
#define WM8996_DMIC1L_ENA_MASK                  0x0008  /* DMIC1L_ENA */
#define WM8996_DMIC1L_ENA_SHIFT                      3  /* DMIC1L_ENA */
#define WM8996_DMIC1L_ENA_WIDTH                      1  /* DMIC1L_ENA */
#define WM8996_DMIC1R_ENA                       0x0004  /* DMIC1R_ENA */
#define WM8996_DMIC1R_ENA_MASK                  0x0004  /* DMIC1R_ENA */
#define WM8996_DMIC1R_ENA_SHIFT                      2  /* DMIC1R_ENA */
#define WM8996_DMIC1R_ENA_WIDTH                      1  /* DMIC1R_ENA */
#define WM8996_ADCL_ENA                         0x0002  /* ADCL_ENA */
#define WM8996_ADCL_ENA_MASK                    0x0002  /* ADCL_ENA */
#define WM8996_ADCL_ENA_SHIFT                        1  /* ADCL_ENA */
#define WM8996_ADCL_ENA_WIDTH                        1  /* ADCL_ENA */
#define WM8996_ADCR_ENA                         0x0001  /* ADCR_ENA */
#define WM8996_ADCR_ENA_MASK                    0x0001  /* ADCR_ENA */
#define WM8996_ADCR_ENA_SHIFT                        0  /* ADCR_ENA */
#define WM8996_ADCR_ENA_WIDTH                        1  /* ADCR_ENA */

/*
 * R4 (0x04) - Power Management (4)
 */
#define WM8996_AIF2RX_CHAN1_ENA                 0x0200  /* AIF2RX_CHAN1_ENA */
#define WM8996_AIF2RX_CHAN1_ENA_MASK            0x0200  /* AIF2RX_CHAN1_ENA */
#define WM8996_AIF2RX_CHAN1_ENA_SHIFT                9  /* AIF2RX_CHAN1_ENA */
#define WM8996_AIF2RX_CHAN1_ENA_WIDTH                1  /* AIF2RX_CHAN1_ENA */
#define WM8996_AIF2RX_CHAN0_ENA                 0x0100  /* AIF2RX_CHAN0_ENA */
#define WM8996_AIF2RX_CHAN0_ENA_MASK            0x0100  /* AIF2RX_CHAN0_ENA */
#define WM8996_AIF2RX_CHAN0_ENA_SHIFT                8  /* AIF2RX_CHAN0_ENA */
#define WM8996_AIF2RX_CHAN0_ENA_WIDTH                1  /* AIF2RX_CHAN0_ENA */
#define WM8996_AIF1RX_CHAN5_ENA                 0x0020  /* AIF1RX_CHAN5_ENA */
#define WM8996_AIF1RX_CHAN5_ENA_MASK            0x0020  /* AIF1RX_CHAN5_ENA */
#define WM8996_AIF1RX_CHAN5_ENA_SHIFT                5  /* AIF1RX_CHAN5_ENA */
#define WM8996_AIF1RX_CHAN5_ENA_WIDTH                1  /* AIF1RX_CHAN5_ENA */
#define WM8996_AIF1RX_CHAN4_ENA                 0x0010  /* AIF1RX_CHAN4_ENA */
#define WM8996_AIF1RX_CHAN4_ENA_MASK            0x0010  /* AIF1RX_CHAN4_ENA */
#define WM8996_AIF1RX_CHAN4_ENA_SHIFT                4  /* AIF1RX_CHAN4_ENA */
#define WM8996_AIF1RX_CHAN4_ENA_WIDTH                1  /* AIF1RX_CHAN4_ENA */
#define WM8996_AIF1RX_CHAN3_ENA                 0x0008  /* AIF1RX_CHAN3_ENA */
#define WM8996_AIF1RX_CHAN3_ENA_MASK            0x0008  /* AIF1RX_CHAN3_ENA */
#define WM8996_AIF1RX_CHAN3_ENA_SHIFT                3  /* AIF1RX_CHAN3_ENA */
#define WM8996_AIF1RX_CHAN3_ENA_WIDTH                1  /* AIF1RX_CHAN3_ENA */
#define WM8996_AIF1RX_CHAN2_ENA                 0x0004  /* AIF1RX_CHAN2_ENA */
#define WM8996_AIF1RX_CHAN2_ENA_MASK            0x0004  /* AIF1RX_CHAN2_ENA */
#define WM8996_AIF1RX_CHAN2_ENA_SHIFT                2  /* AIF1RX_CHAN2_ENA */
#define WM8996_AIF1RX_CHAN2_ENA_WIDTH                1  /* AIF1RX_CHAN2_ENA */
#define WM8996_AIF1RX_CHAN1_ENA                 0x0002  /* AIF1RX_CHAN1_ENA */
#define WM8996_AIF1RX_CHAN1_ENA_MASK            0x0002  /* AIF1RX_CHAN1_ENA */
#define WM8996_AIF1RX_CHAN1_ENA_SHIFT                1  /* AIF1RX_CHAN1_ENA */
#define WM8996_AIF1RX_CHAN1_ENA_WIDTH                1  /* AIF1RX_CHAN1_ENA */
#define WM8996_AIF1RX_CHAN0_ENA                 0x0001  /* AIF1RX_CHAN0_ENA */
#define WM8996_AIF1RX_CHAN0_ENA_MASK            0x0001  /* AIF1RX_CHAN0_ENA */
#define WM8996_AIF1RX_CHAN0_ENA_SHIFT                0  /* AIF1RX_CHAN0_ENA */
#define WM8996_AIF1RX_CHAN0_ENA_WIDTH                1  /* AIF1RX_CHAN0_ENA */

/*
 * R5 (0x05) - Power Management (5)
 */
#define WM8996_DSP2TXL_ENA                      0x0800  /* DSP2TXL_ENA */
#define WM8996_DSP2TXL_ENA_MASK                 0x0800  /* DSP2TXL_ENA */
#define WM8996_DSP2TXL_ENA_SHIFT                    11  /* DSP2TXL_ENA */
#define WM8996_DSP2TXL_ENA_WIDTH                     1  /* DSP2TXL_ENA */
#define WM8996_DSP2TXR_ENA                      0x0400  /* DSP2TXR_ENA */
#define WM8996_DSP2TXR_ENA_MASK                 0x0400  /* DSP2TXR_ENA */
#define WM8996_DSP2TXR_ENA_SHIFT                    10  /* DSP2TXR_ENA */
#define WM8996_DSP2TXR_ENA_WIDTH                     1  /* DSP2TXR_ENA */
#define WM8996_DSP1TXL_ENA                      0x0200  /* DSP1TXL_ENA */
#define WM8996_DSP1TXL_ENA_MASK                 0x0200  /* DSP1TXL_ENA */
#define WM8996_DSP1TXL_ENA_SHIFT                     9  /* DSP1TXL_ENA */
#define WM8996_DSP1TXL_ENA_WIDTH                     1  /* DSP1TXL_ENA */
#define WM8996_DSP1TXR_ENA                      0x0100  /* DSP1TXR_ENA */
#define WM8996_DSP1TXR_ENA_MASK                 0x0100  /* DSP1TXR_ENA */
#define WM8996_DSP1TXR_ENA_SHIFT                     8  /* DSP1TXR_ENA */
#define WM8996_DSP1TXR_ENA_WIDTH                     1  /* DSP1TXR_ENA */
#define WM8996_DAC2L_ENA                        0x0008  /* DAC2L_ENA */
#define WM8996_DAC2L_ENA_MASK                   0x0008  /* DAC2L_ENA */
#define WM8996_DAC2L_ENA_SHIFT                       3  /* DAC2L_ENA */
#define WM8996_DAC2L_ENA_WIDTH                       1  /* DAC2L_ENA */
#define WM8996_DAC2R_ENA                        0x0004  /* DAC2R_ENA */
#define WM8996_DAC2R_ENA_MASK                   0x0004  /* DAC2R_ENA */
#define WM8996_DAC2R_ENA_SHIFT                       2  /* DAC2R_ENA */
#define WM8996_DAC2R_ENA_WIDTH                       1  /* DAC2R_ENA */
#define WM8996_DAC1L_ENA                        0x0002  /* DAC1L_ENA */
#define WM8996_DAC1L_ENA_MASK                   0x0002  /* DAC1L_ENA */
#define WM8996_DAC1L_ENA_SHIFT                       1  /* DAC1L_ENA */
#define WM8996_DAC1L_ENA_WIDTH                       1  /* DAC1L_ENA */
#define WM8996_DAC1R_ENA                        0x0001  /* DAC1R_ENA */
#define WM8996_DAC1R_ENA_MASK                   0x0001  /* DAC1R_ENA */
#define WM8996_DAC1R_ENA_SHIFT                       0  /* DAC1R_ENA */
#define WM8996_DAC1R_ENA_WIDTH                       1  /* DAC1R_ENA */

/*
 * R6 (0x06) - Power Management (6)
 */
#define WM8996_AIF2TX_CHAN1_ENA                 0x0200  /* AIF2TX_CHAN1_ENA */
#define WM8996_AIF2TX_CHAN1_ENA_MASK            0x0200  /* AIF2TX_CHAN1_ENA */
#define WM8996_AIF2TX_CHAN1_ENA_SHIFT                9  /* AIF2TX_CHAN1_ENA */
#define WM8996_AIF2TX_CHAN1_ENA_WIDTH                1  /* AIF2TX_CHAN1_ENA */
#define WM8996_AIF2TX_CHAN0_ENA                 0x0100  /* AIF2TX_CHAN0_ENA */
#define WM8996_AIF2TX_CHAN0_ENA_MASK            0x0100  /* AIF2TX_CHAN0_ENA */
#define WM8996_AIF2TX_CHAN0_ENA_SHIFT                8  /* AIF2TX_CHAN0_ENA */
#define WM8996_AIF2TX_CHAN0_ENA_WIDTH                1  /* AIF2TX_CHAN0_ENA */
#define WM8996_AIF1TX_CHAN5_ENA                 0x0020  /* AIF1TX_CHAN5_ENA */
#define WM8996_AIF1TX_CHAN5_ENA_MASK            0x0020  /* AIF1TX_CHAN5_ENA */
#define WM8996_AIF1TX_CHAN5_ENA_SHIFT                5  /* AIF1TX_CHAN5_ENA */
#define WM8996_AIF1TX_CHAN5_ENA_WIDTH                1  /* AIF1TX_CHAN5_ENA */
#define WM8996_AIF1TX_CHAN4_ENA                 0x0010  /* AIF1TX_CHAN4_ENA */
#define WM8996_AIF1TX_CHAN4_ENA_MASK            0x0010  /* AIF1TX_CHAN4_ENA */
#define WM8996_AIF1TX_CHAN4_ENA_SHIFT                4  /* AIF1TX_CHAN4_ENA */
#define WM8996_AIF1TX_CHAN4_ENA_WIDTH                1  /* AIF1TX_CHAN4_ENA */
#define WM8996_AIF1TX_CHAN3_ENA                 0x0008  /* AIF1TX_CHAN3_ENA */
#define WM8996_AIF1TX_CHAN3_ENA_MASK            0x0008  /* AIF1TX_CHAN3_ENA */
#define WM8996_AIF1TX_CHAN3_ENA_SHIFT                3  /* AIF1TX_CHAN3_ENA */
#define WM8996_AIF1TX_CHAN3_ENA_WIDTH                1  /* AIF1TX_CHAN3_ENA */
#define WM8996_AIF1TX_CHAN2_ENA                 0x0004  /* AIF1TX_CHAN2_ENA */
#define WM8996_AIF1TX_CHAN2_ENA_MASK            0x0004  /* AIF1TX_CHAN2_ENA */
#define WM8996_AIF1TX_CHAN2_ENA_SHIFT                2  /* AIF1TX_CHAN2_ENA */
#define WM8996_AIF1TX_CHAN2_ENA_WIDTH                1  /* AIF1TX_CHAN2_ENA */
#define WM8996_AIF1TX_CHAN1_ENA                 0x0002  /* AIF1TX_CHAN1_ENA */
#define WM8996_AIF1TX_CHAN1_ENA_MASK            0x0002  /* AIF1TX_CHAN1_ENA */
#define WM8996_AIF1TX_CHAN1_ENA_SHIFT                1  /* AIF1TX_CHAN1_ENA */
#define WM8996_AIF1TX_CHAN1_ENA_WIDTH                1  /* AIF1TX_CHAN1_ENA */
#define WM8996_AIF1TX_CHAN0_ENA                 0x0001  /* AIF1TX_CHAN0_ENA */
#define WM8996_AIF1TX_CHAN0_ENA_MASK            0x0001  /* AIF1TX_CHAN0_ENA */
#define WM8996_AIF1TX_CHAN0_ENA_SHIFT                0  /* AIF1TX_CHAN0_ENA */
#define WM8996_AIF1TX_CHAN0_ENA_WIDTH                1  /* AIF1TX_CHAN0_ENA */

/*
 * R7 (0x07) - Power Management (7)
 */
#define WM8996_DMIC2_FN                         0x0200  /* DMIC2_FN */
#define WM8996_DMIC2_FN_MASK                    0x0200  /* DMIC2_FN */
#define WM8996_DMIC2_FN_SHIFT                        9  /* DMIC2_FN */
#define WM8996_DMIC2_FN_WIDTH                        1  /* DMIC2_FN */
#define WM8996_DMIC1_FN                         0x0100  /* DMIC1_FN */
#define WM8996_DMIC1_FN_MASK                    0x0100  /* DMIC1_FN */
#define WM8996_DMIC1_FN_SHIFT                        8  /* DMIC1_FN */
#define WM8996_DMIC1_FN_WIDTH                        1  /* DMIC1_FN */
#define WM8996_ADC_DMIC_DSP2R_ENA               0x0080  /* ADC_DMIC_DSP2R_ENA */
#define WM8996_ADC_DMIC_DSP2R_ENA_MASK          0x0080  /* ADC_DMIC_DSP2R_ENA */
#define WM8996_ADC_DMIC_DSP2R_ENA_SHIFT              7  /* ADC_DMIC_DSP2R_ENA */
#define WM8996_ADC_DMIC_DSP2R_ENA_WIDTH              1  /* ADC_DMIC_DSP2R_ENA */
#define WM8996_ADC_DMIC_DSP2L_ENA               0x0040  /* ADC_DMIC_DSP2L_ENA */
#define WM8996_ADC_DMIC_DSP2L_ENA_MASK          0x0040  /* ADC_DMIC_DSP2L_ENA */
#define WM8996_ADC_DMIC_DSP2L_ENA_SHIFT              6  /* ADC_DMIC_DSP2L_ENA */
#define WM8996_ADC_DMIC_DSP2L_ENA_WIDTH              1  /* ADC_DMIC_DSP2L_ENA */
#define WM8996_ADC_DMIC_SRC2_MASK               0x0030  /* ADC_DMIC_SRC2 - [5:4] */
#define WM8996_ADC_DMIC_SRC2_SHIFT                   4  /* ADC_DMIC_SRC2 - [5:4] */
#define WM8996_ADC_DMIC_SRC2_WIDTH                   2  /* ADC_DMIC_SRC2 - [5:4] */
#define WM8996_ADC_DMIC_DSP1R_ENA               0x0008  /* ADC_DMIC_DSP1R_ENA */
#define WM8996_ADC_DMIC_DSP1R_ENA_MASK          0x0008  /* ADC_DMIC_DSP1R_ENA */
#define WM8996_ADC_DMIC_DSP1R_ENA_SHIFT              3  /* ADC_DMIC_DSP1R_ENA */
#define WM8996_ADC_DMIC_DSP1R_ENA_WIDTH              1  /* ADC_DMIC_DSP1R_ENA */
#define WM8996_ADC_DMIC_DSP1L_ENA               0x0004  /* ADC_DMIC_DSP1L_ENA */
#define WM8996_ADC_DMIC_DSP1L_ENA_MASK          0x0004  /* ADC_DMIC_DSP1L_ENA */
#define WM8996_ADC_DMIC_DSP1L_ENA_SHIFT              2  /* ADC_DMIC_DSP1L_ENA */
#define WM8996_ADC_DMIC_DSP1L_ENA_WIDTH              1  /* ADC_DMIC_DSP1L_ENA */
#define WM8996_ADC_DMIC_SRC1_MASK               0x0003  /* ADC_DMIC_SRC1 - [1:0] */
#define WM8996_ADC_DMIC_SRC1_SHIFT                   0  /* ADC_DMIC_SRC1 - [1:0] */
#define WM8996_ADC_DMIC_SRC1_WIDTH                   2  /* ADC_DMIC_SRC1 - [1:0] */

/*
 * R8 (0x08) - Power Management (8)
 */
#define WM8996_AIF2TX_SRC_MASK                  0x00C0  /* AIF2TX_SRC - [7:6] */
#define WM8996_AIF2TX_SRC_SHIFT                      6  /* AIF2TX_SRC - [7:6] */
#define WM8996_AIF2TX_SRC_WIDTH                      2  /* AIF2TX_SRC - [7:6] */
#define WM8996_DSP2RX_SRC                       0x0010  /* DSP2RX_SRC */
#define WM8996_DSP2RX_SRC_MASK                  0x0010  /* DSP2RX_SRC */
#define WM8996_DSP2RX_SRC_SHIFT                      4  /* DSP2RX_SRC */
#define WM8996_DSP2RX_SRC_WIDTH                      1  /* DSP2RX_SRC */
#define WM8996_DSP1RX_SRC                       0x0001  /* DSP1RX_SRC */
#define WM8996_DSP1RX_SRC_MASK                  0x0001  /* DSP1RX_SRC */
#define WM8996_DSP1RX_SRC_SHIFT                      0  /* DSP1RX_SRC */
#define WM8996_DSP1RX_SRC_WIDTH                      1  /* DSP1RX_SRC */

/*
 * R16 (0x10) - Left Line Input Volume
 */
#define WM8996_IN1_VU                           0x0080  /* IN1_VU */
#define WM8996_IN1_VU_MASK                      0x0080  /* IN1_VU */
#define WM8996_IN1_VU_SHIFT                          7  /* IN1_VU */
#define WM8996_IN1_VU_WIDTH                          1  /* IN1_VU */
#define WM8996_IN1L_ZC                          0x0020  /* IN1L_ZC */
#define WM8996_IN1L_ZC_MASK                     0x0020  /* IN1L_ZC */
#define WM8996_IN1L_ZC_SHIFT                         5  /* IN1L_ZC */
#define WM8996_IN1L_ZC_WIDTH                         1  /* IN1L_ZC */
#define WM8996_IN1L_VOL_MASK                    0x001F  /* IN1L_VOL - [4:0] */
#define WM8996_IN1L_VOL_SHIFT                        0  /* IN1L_VOL - [4:0] */
#define WM8996_IN1L_VOL_WIDTH                        5  /* IN1L_VOL - [4:0] */

/*
 * R17 (0x11) - Right Line Input Volume
 */
#define WM8996_IN1_VU                           0x0080  /* IN1_VU */
#define WM8996_IN1_VU_MASK                      0x0080  /* IN1_VU */
#define WM8996_IN1_VU_SHIFT                          7  /* IN1_VU */
#define WM8996_IN1_VU_WIDTH                          1  /* IN1_VU */
#define WM8996_IN1R_ZC                          0x0020  /* IN1R_ZC */
#define WM8996_IN1R_ZC_MASK                     0x0020  /* IN1R_ZC */
#define WM8996_IN1R_ZC_SHIFT                         5  /* IN1R_ZC */
#define WM8996_IN1R_ZC_WIDTH                         1  /* IN1R_ZC */
#define WM8996_IN1R_VOL_MASK                    0x001F  /* IN1R_VOL - [4:0] */
#define WM8996_IN1R_VOL_SHIFT                        0  /* IN1R_VOL - [4:0] */
#define WM8996_IN1R_VOL_WIDTH                        5  /* IN1R_VOL - [4:0] */

/*
 * R18 (0x12) - Line Input Control
 */
#define WM8996_INL_MODE_MASK                    0x000C  /* INL_MODE - [3:2] */
#define WM8996_INL_MODE_SHIFT                        2  /* INL_MODE - [3:2] */
#define WM8996_INL_MODE_WIDTH                        2  /* INL_MODE - [3:2] */
#define WM8996_INR_MODE_MASK                    0x0003  /* INR_MODE - [1:0] */
#define WM8996_INR_MODE_SHIFT                        0  /* INR_MODE - [1:0] */
#define WM8996_INR_MODE_WIDTH                        2  /* INR_MODE - [1:0] */

/*
 * R21 (0x15) - DAC1 HPOUT1 Volume
 */
#define WM8996_DAC1R_HPOUT1R_VOL_MASK           0x00F0  /* DAC1R_HPOUT1R_VOL - [7:4] */
#define WM8996_DAC1R_HPOUT1R_VOL_SHIFT               4  /* DAC1R_HPOUT1R_VOL - [7:4] */
#define WM8996_DAC1R_HPOUT1R_VOL_WIDTH               4  /* DAC1R_HPOUT1R_VOL - [7:4] */
#define WM8996_DAC1L_HPOUT1L_VOL_MASK           0x000F  /* DAC1L_HPOUT1L_VOL - [3:0] */
#define WM8996_DAC1L_HPOUT1L_VOL_SHIFT               0  /* DAC1L_HPOUT1L_VOL - [3:0] */
#define WM8996_DAC1L_HPOUT1L_VOL_WIDTH               4  /* DAC1L_HPOUT1L_VOL - [3:0] */

/*
 * R22 (0x16) - DAC2 HPOUT2 Volume
 */
#define WM8996_DAC2R_HPOUT2R_VOL_MASK           0x00F0  /* DAC2R_HPOUT2R_VOL - [7:4] */
#define WM8996_DAC2R_HPOUT2R_VOL_SHIFT               4  /* DAC2R_HPOUT2R_VOL - [7:4] */
#define WM8996_DAC2R_HPOUT2R_VOL_WIDTH               4  /* DAC2R_HPOUT2R_VOL - [7:4] */
#define WM8996_DAC2L_HPOUT2L_VOL_MASK           0x000F  /* DAC2L_HPOUT2L_VOL - [3:0] */
#define WM8996_DAC2L_HPOUT2L_VOL_SHIFT               0  /* DAC2L_HPOUT2L_VOL - [3:0] */
#define WM8996_DAC2L_HPOUT2L_VOL_WIDTH               4  /* DAC2L_HPOUT2L_VOL - [3:0] */

/*
 * R24 (0x18) - DAC1 Left Volume
 */
#define WM8996_DAC1L_MUTE                       0x0200  /* DAC1L_MUTE */
#define WM8996_DAC1L_MUTE_MASK                  0x0200  /* DAC1L_MUTE */
#define WM8996_DAC1L_MUTE_SHIFT                      9  /* DAC1L_MUTE */
#define WM8996_DAC1L_MUTE_WIDTH                      1  /* DAC1L_MUTE */
#define WM8996_DAC1_VU                          0x0100  /* DAC1_VU */
#define WM8996_DAC1_VU_MASK                     0x0100  /* DAC1_VU */
#define WM8996_DAC1_VU_SHIFT                         8  /* DAC1_VU */
#define WM8996_DAC1_VU_WIDTH                         1  /* DAC1_VU */
#define WM8996_DAC1L_VOL_MASK                   0x00FF  /* DAC1L_VOL - [7:0] */
#define WM8996_DAC1L_VOL_SHIFT                       0  /* DAC1L_VOL - [7:0] */
#define WM8996_DAC1L_VOL_WIDTH                       8  /* DAC1L_VOL - [7:0] */

/*
 * R25 (0x19) - DAC1 Right Volume
 */
#define WM8996_DAC1R_MUTE                       0x0200  /* DAC1R_MUTE */
#define WM8996_DAC1R_MUTE_MASK                  0x0200  /* DAC1R_MUTE */
#define WM8996_DAC1R_MUTE_SHIFT                      9  /* DAC1R_MUTE */
#define WM8996_DAC1R_MUTE_WIDTH                      1  /* DAC1R_MUTE */
#define WM8996_DAC1_VU                          0x0100  /* DAC1_VU */
#define WM8996_DAC1_VU_MASK                     0x0100  /* DAC1_VU */
#define WM8996_DAC1_VU_SHIFT                         8  /* DAC1_VU */
#define WM8996_DAC1_VU_WIDTH                         1  /* DAC1_VU */
#define WM8996_DAC1R_VOL_MASK                   0x00FF  /* DAC1R_VOL - [7:0] */
#define WM8996_DAC1R_VOL_SHIFT                       0  /* DAC1R_VOL - [7:0] */
#define WM8996_DAC1R_VOL_WIDTH                       8  /* DAC1R_VOL - [7:0] */

/*
 * R26 (0x1A) - DAC2 Left Volume
 */
#define WM8996_DAC2L_MUTE                       0x0200  /* DAC2L_MUTE */
#define WM8996_DAC2L_MUTE_MASK                  0x0200  /* DAC2L_MUTE */
#define WM8996_DAC2L_MUTE_SHIFT                      9  /* DAC2L_MUTE */
#define WM8996_DAC2L_MUTE_WIDTH                      1  /* DAC2L_MUTE */
#define WM8996_DAC2_VU                          0x0100  /* DAC2_VU */
#define WM8996_DAC2_VU_MASK                     0x0100  /* DAC2_VU */
#define WM8996_DAC2_VU_SHIFT                         8  /* DAC2_VU */
#define WM8996_DAC2_VU_WIDTH                         1  /* DAC2_VU */
#define WM8996_DAC2L_VOL_MASK                   0x00FF  /* DAC2L_VOL - [7:0] */
#define WM8996_DAC2L_VOL_SHIFT                       0  /* DAC2L_VOL - [7:0] */
#define WM8996_DAC2L_VOL_WIDTH                       8  /* DAC2L_VOL - [7:0] */

/*
 * R27 (0x1B) - DAC2 Right Volume
 */
#define WM8996_DAC2R_MUTE                       0x0200  /* DAC2R_MUTE */
#define WM8996_DAC2R_MUTE_MASK                  0x0200  /* DAC2R_MUTE */
#define WM8996_DAC2R_MUTE_SHIFT                      9  /* DAC2R_MUTE */
#define WM8996_DAC2R_MUTE_WIDTH                      1  /* DAC2R_MUTE */
#define WM8996_DAC2_VU                          0x0100  /* DAC2_VU */
#define WM8996_DAC2_VU_MASK                     0x0100  /* DAC2_VU */
#define WM8996_DAC2_VU_SHIFT                         8  /* DAC2_VU */
#define WM8996_DAC2_VU_WIDTH                         1  /* DAC2_VU */
#define WM8996_DAC2R_VOL_MASK                   0x00FF  /* DAC2R_VOL - [7:0] */
#define WM8996_DAC2R_VOL_SHIFT                       0  /* DAC2R_VOL - [7:0] */
#define WM8996_DAC2R_VOL_WIDTH                       8  /* DAC2R_VOL - [7:0] */

/*
 * R28 (0x1C) - Output1 Left Volume
 */
#define WM8996_DAC1_VU                          0x0100  /* DAC1_VU */
#define WM8996_DAC1_VU_MASK                     0x0100  /* DAC1_VU */
#define WM8996_DAC1_VU_SHIFT                         8  /* DAC1_VU */
#define WM8996_DAC1_VU_WIDTH                         1  /* DAC1_VU */
#define WM8996_HPOUT1L_ZC                       0x0080  /* HPOUT1L_ZC */
#define WM8996_HPOUT1L_ZC_MASK                  0x0080  /* HPOUT1L_ZC */
#define WM8996_HPOUT1L_ZC_SHIFT                      7  /* HPOUT1L_ZC */
#define WM8996_HPOUT1L_ZC_WIDTH                      1  /* HPOUT1L_ZC */
#define WM8996_HPOUT1L_VOL_MASK                 0x000F  /* HPOUT1L_VOL - [3:0] */
#define WM8996_HPOUT1L_VOL_SHIFT                     0  /* HPOUT1L_VOL - [3:0] */
#define WM8996_HPOUT1L_VOL_WIDTH                     4  /* HPOUT1L_VOL - [3:0] */

/*
 * R29 (0x1D) - Output1 Right Volume
 */
#define WM8996_DAC1_VU                          0x0100  /* DAC1_VU */
#define WM8996_DAC1_VU_MASK                     0x0100  /* DAC1_VU */
#define WM8996_DAC1_VU_SHIFT                         8  /* DAC1_VU */
#define WM8996_DAC1_VU_WIDTH                         1  /* DAC1_VU */
#define WM8996_HPOUT1R_ZC                       0x0080  /* HPOUT1R_ZC */
#define WM8996_HPOUT1R_ZC_MASK                  0x0080  /* HPOUT1R_ZC */
#define WM8996_HPOUT1R_ZC_SHIFT                      7  /* HPOUT1R_ZC */
#define WM8996_HPOUT1R_ZC_WIDTH                      1  /* HPOUT1R_ZC */
#define WM8996_HPOUT1R_VOL_MASK                 0x000F  /* HPOUT1R_VOL - [3:0] */
#define WM8996_HPOUT1R_VOL_SHIFT                     0  /* HPOUT1R_VOL - [3:0] */
#define WM8996_HPOUT1R_VOL_WIDTH                     4  /* HPOUT1R_VOL - [3:0] */

/*
 * R30 (0x1E) - Output2 Left Volume
 */
#define WM8996_DAC2_VU                          0x0100  /* DAC2_VU */
#define WM8996_DAC2_VU_MASK                     0x0100  /* DAC2_VU */
#define WM8996_DAC2_VU_SHIFT                         8  /* DAC2_VU */
#define WM8996_DAC2_VU_WIDTH                         1  /* DAC2_VU */
#define WM8996_HPOUT2L_ZC                       0x0080  /* HPOUT2L_ZC */
#define WM8996_HPOUT2L_ZC_MASK                  0x0080  /* HPOUT2L_ZC */
#define WM8996_HPOUT2L_ZC_SHIFT                      7  /* HPOUT2L_ZC */
#define WM8996_HPOUT2L_ZC_WIDTH                      1  /* HPOUT2L_ZC */
#define WM8996_HPOUT2L_VOL_MASK                 0x000F  /* HPOUT2L_VOL - [3:0] */
#define WM8996_HPOUT2L_VOL_SHIFT                     0  /* HPOUT2L_VOL - [3:0] */
#define WM8996_HPOUT2L_VOL_WIDTH                     4  /* HPOUT2L_VOL - [3:0] */

/*
 * R31 (0x1F) - Output2 Right Volume
 */
#define WM8996_DAC2_VU                          0x0100  /* DAC2_VU */
#define WM8996_DAC2_VU_MASK                     0x0100  /* DAC2_VU */
#define WM8996_DAC2_VU_SHIFT                         8  /* DAC2_VU */
#define WM8996_DAC2_VU_WIDTH                         1  /* DAC2_VU */
#define WM8996_HPOUT2R_ZC                       0x0080  /* HPOUT2R_ZC */
#define WM8996_HPOUT2R_ZC_MASK                  0x0080  /* HPOUT2R_ZC */
#define WM8996_HPOUT2R_ZC_SHIFT                      7  /* HPOUT2R_ZC */
#define WM8996_HPOUT2R_ZC_WIDTH                      1  /* HPOUT2R_ZC */
#define WM8996_HPOUT2R_VOL_MASK                 0x000F  /* HPOUT2R_VOL - [3:0] */
#define WM8996_HPOUT2R_VOL_SHIFT                     0  /* HPOUT2R_VOL - [3:0] */
#define WM8996_HPOUT2R_VOL_WIDTH                     4  /* HPOUT2R_VOL - [3:0] */

/*
 * R32 (0x20) - MICBIAS (1)
 */
#define WM8996_MICB1_RATE                       0x0020  /* MICB1_RATE */
#define WM8996_MICB1_RATE_MASK                  0x0020  /* MICB1_RATE */
#define WM8996_MICB1_RATE_SHIFT                      5  /* MICB1_RATE */
#define WM8996_MICB1_RATE_WIDTH                      1  /* MICB1_RATE */
#define WM8996_MICB1_MODE                       0x0010  /* MICB1_MODE */
#define WM8996_MICB1_MODE_MASK                  0x0010  /* MICB1_MODE */
#define WM8996_MICB1_MODE_SHIFT                      4  /* MICB1_MODE */
#define WM8996_MICB1_MODE_WIDTH                      1  /* MICB1_MODE */
#define WM8996_MICB1_LVL_MASK                   0x000E  /* MICB1_LVL - [3:1] */
#define WM8996_MICB1_LVL_SHIFT                       1  /* MICB1_LVL - [3:1] */
#define WM8996_MICB1_LVL_WIDTH                       3  /* MICB1_LVL - [3:1] */
#define WM8996_MICB1_DISCH                      0x0001  /* MICB1_DISCH */
#define WM8996_MICB1_DISCH_MASK                 0x0001  /* MICB1_DISCH */
#define WM8996_MICB1_DISCH_SHIFT                     0  /* MICB1_DISCH */
#define WM8996_MICB1_DISCH_WIDTH                     1  /* MICB1_DISCH */

/*
 * R33 (0x21) - MICBIAS (2)
 */
#define WM8996_MICB2_RATE                       0x0020  /* MICB2_RATE */
#define WM8996_MICB2_RATE_MASK                  0x0020  /* MICB2_RATE */
#define WM8996_MICB2_RATE_SHIFT                      5  /* MICB2_RATE */
#define WM8996_MICB2_RATE_WIDTH                      1  /* MICB2_RATE */
#define WM8996_MICB2_MODE                       0x0010  /* MICB2_MODE */
#define WM8996_MICB2_MODE_MASK                  0x0010  /* MICB2_MODE */
#define WM8996_MICB2_MODE_SHIFT                      4  /* MICB2_MODE */
#define WM8996_MICB2_MODE_WIDTH                      1  /* MICB2_MODE */
#define WM8996_MICB2_LVL_MASK                   0x000E  /* MICB2_LVL - [3:1] */
#define WM8996_MICB2_LVL_SHIFT                       1  /* MICB2_LVL - [3:1] */
#define WM8996_MICB2_LVL_WIDTH                       3  /* MICB2_LVL - [3:1] */
#define WM8996_MICB2_DISCH                      0x0001  /* MICB2_DISCH */
#define WM8996_MICB2_DISCH_MASK                 0x0001  /* MICB2_DISCH */
#define WM8996_MICB2_DISCH_SHIFT                     0  /* MICB2_DISCH */
#define WM8996_MICB2_DISCH_WIDTH                     1  /* MICB2_DISCH */

/*
 * R40 (0x28) - LDO 1
 */
#define WM8996_LDO1_MODE                        0x0020  /* LDO1_MODE */
#define WM8996_LDO1_MODE_MASK                   0x0020  /* LDO1_MODE */
#define WM8996_LDO1_MODE_SHIFT                       5  /* LDO1_MODE */
#define WM8996_LDO1_MODE_WIDTH                       1  /* LDO1_MODE */
#define WM8996_LDO1_VSEL_MASK                   0x0006  /* LDO1_VSEL - [2:1] */
#define WM8996_LDO1_VSEL_SHIFT                       1  /* LDO1_VSEL - [2:1] */
#define WM8996_LDO1_VSEL_WIDTH                       2  /* LDO1_VSEL - [2:1] */
#define WM8996_LDO1_DISCH                       0x0001  /* LDO1_DISCH */
#define WM8996_LDO1_DISCH_MASK                  0x0001  /* LDO1_DISCH */
#define WM8996_LDO1_DISCH_SHIFT                      0  /* LDO1_DISCH */
#define WM8996_LDO1_DISCH_WIDTH                      1  /* LDO1_DISCH */

/*
 * R41 (0x29) - LDO 2
 */
#define WM8996_LDO2_MODE                        0x0020  /* LDO2_MODE */
#define WM8996_LDO2_MODE_MASK                   0x0020  /* LDO2_MODE */
#define WM8996_LDO2_MODE_SHIFT                       5  /* LDO2_MODE */
#define WM8996_LDO2_MODE_WIDTH                       1  /* LDO2_MODE */
#define WM8996_LDO2_VSEL_MASK                   0x001E  /* LDO2_VSEL - [4:1] */
#define WM8996_LDO2_VSEL_SHIFT                       1  /* LDO2_VSEL - [4:1] */
#define WM8996_LDO2_VSEL_WIDTH                       4  /* LDO2_VSEL - [4:1] */
#define WM8996_LDO2_DISCH                       0x0001  /* LDO2_DISCH */
#define WM8996_LDO2_DISCH_MASK                  0x0001  /* LDO2_DISCH */
#define WM8996_LDO2_DISCH_SHIFT                      0  /* LDO2_DISCH */
#define WM8996_LDO2_DISCH_WIDTH                      1  /* LDO2_DISCH */

/*
 * R48 (0x30) - Accessory Detect Mode 1
 */
#define WM8996_JD_MODE_MASK                     0x0003  /* JD_MODE - [1:0] */
#define WM8996_JD_MODE_SHIFT                         0  /* JD_MODE - [1:0] */
#define WM8996_JD_MODE_WIDTH                         2  /* JD_MODE - [1:0] */

/*
 * R49 (0x31) - Accessory Detect Mode 2
 */
#define WM8996_HPOUT1FB_SRC                     0x0004  /* HPOUT1FB_SRC */
#define WM8996_HPOUT1FB_SRC_MASK                0x0004  /* HPOUT1FB_SRC */
#define WM8996_HPOUT1FB_SRC_SHIFT                    2  /* HPOUT1FB_SRC */
#define WM8996_HPOUT1FB_SRC_WIDTH                    1  /* HPOUT1FB_SRC */
#define WM8996_MICD_SRC                         0x0002  /* MICD_SRC */
#define WM8996_MICD_SRC_MASK                    0x0002  /* MICD_SRC */
#define WM8996_MICD_SRC_SHIFT                        1  /* MICD_SRC */
#define WM8996_MICD_SRC_WIDTH                        1  /* MICD_SRC */
#define WM8996_MICD_BIAS_SRC                    0x0001  /* MICD_BIAS_SRC */
#define WM8996_MICD_BIAS_SRC_MASK               0x0001  /* MICD_BIAS_SRC */
#define WM8996_MICD_BIAS_SRC_SHIFT                   0  /* MICD_BIAS_SRC */
#define WM8996_MICD_BIAS_SRC_WIDTH                   1  /* MICD_BIAS_SRC */

/*
 * R52 (0x34) - Headphone Detect 1
 */
#define WM8996_HP_HOLDTIME_MASK                 0x00E0  /* HP_HOLDTIME - [7:5] */
#define WM8996_HP_HOLDTIME_SHIFT                     5  /* HP_HOLDTIME - [7:5] */
#define WM8996_HP_HOLDTIME_WIDTH                     3  /* HP_HOLDTIME - [7:5] */
#define WM8996_HP_CLK_DIV_MASK                  0x0018  /* HP_CLK_DIV - [4:3] */
#define WM8996_HP_CLK_DIV_SHIFT                      3  /* HP_CLK_DIV - [4:3] */
#define WM8996_HP_CLK_DIV_WIDTH                      2  /* HP_CLK_DIV - [4:3] */
#define WM8996_HP_STEP_SIZE                     0x0002  /* HP_STEP_SIZE */
#define WM8996_HP_STEP_SIZE_MASK                0x0002  /* HP_STEP_SIZE */
#define WM8996_HP_STEP_SIZE_SHIFT                    1  /* HP_STEP_SIZE */
#define WM8996_HP_STEP_SIZE_WIDTH                    1  /* HP_STEP_SIZE */
#define WM8996_HP_POLL                          0x0001  /* HP_POLL */
#define WM8996_HP_POLL_MASK                     0x0001  /* HP_POLL */
#define WM8996_HP_POLL_SHIFT                         0  /* HP_POLL */
#define WM8996_HP_POLL_WIDTH                         1  /* HP_POLL */

/*
 * R53 (0x35) - Headphone Detect 2
 */
#define WM8996_HP_DONE                          0x0080  /* HP_DONE */
#define WM8996_HP_DONE_MASK                     0x0080  /* HP_DONE */
#define WM8996_HP_DONE_SHIFT                         7  /* HP_DONE */
#define WM8996_HP_DONE_WIDTH                         1  /* HP_DONE */
#define WM8996_HP_LVL_MASK                      0x007F  /* HP_LVL - [6:0] */
#define WM8996_HP_LVL_SHIFT                          0  /* HP_LVL - [6:0] */
#define WM8996_HP_LVL_WIDTH                          7  /* HP_LVL - [6:0] */

/*
 * R56 (0x38) - Mic Detect 1
 */
#define WM8996_MICD_BIAS_STARTTIME_MASK         0xF000  /* MICD_BIAS_STARTTIME - [15:12] */
#define WM8996_MICD_BIAS_STARTTIME_SHIFT            12  /* MICD_BIAS_STARTTIME - [15:12] */
#define WM8996_MICD_BIAS_STARTTIME_WIDTH             4  /* MICD_BIAS_STARTTIME - [15:12] */
#define WM8996_MICD_RATE_MASK                   0x0F00  /* MICD_RATE - [11:8] */
#define WM8996_MICD_RATE_SHIFT                       8  /* MICD_RATE - [11:8] */
#define WM8996_MICD_RATE_WIDTH                       4  /* MICD_RATE - [11:8] */
#define WM8996_MICD_DBTIME                      0x0002  /* MICD_DBTIME */
#define WM8996_MICD_DBTIME_MASK                 0x0002  /* MICD_DBTIME */
#define WM8996_MICD_DBTIME_SHIFT                     1  /* MICD_DBTIME */
#define WM8996_MICD_DBTIME_WIDTH                     1  /* MICD_DBTIME */
#define WM8996_MICD_ENA                         0x0001  /* MICD_ENA */
#define WM8996_MICD_ENA_MASK                    0x0001  /* MICD_ENA */
#define WM8996_MICD_ENA_SHIFT                        0  /* MICD_ENA */
#define WM8996_MICD_ENA_WIDTH                        1  /* MICD_ENA */

/*
 * R57 (0x39) - Mic Detect 2
 */
#define WM8996_MICD_LVL_SEL_MASK                0x00FF  /* MICD_LVL_SEL - [7:0] */
#define WM8996_MICD_LVL_SEL_SHIFT                    0  /* MICD_LVL_SEL - [7:0] */
#define WM8996_MICD_LVL_SEL_WIDTH                    8  /* MICD_LVL_SEL - [7:0] */

/*
 * R58 (0x3A) - Mic Detect 3
 */
#define WM8996_MICD_LVL_MASK                    0x07FC  /* MICD_LVL - [10:2] */
#define WM8996_MICD_LVL_SHIFT                        2  /* MICD_LVL - [10:2] */
#define WM8996_MICD_LVL_WIDTH                        9  /* MICD_LVL - [10:2] */
#define WM8996_MICD_VALID                       0x0002  /* MICD_VALID */
#define WM8996_MICD_VALID_MASK                  0x0002  /* MICD_VALID */
#define WM8996_MICD_VALID_SHIFT                      1  /* MICD_VALID */
#define WM8996_MICD_VALID_WIDTH                      1  /* MICD_VALID */
#define WM8996_MICD_STS                         0x0001  /* MICD_STS */
#define WM8996_MICD_STS_MASK                    0x0001  /* MICD_STS */
#define WM8996_MICD_STS_SHIFT                        0  /* MICD_STS */
#define WM8996_MICD_STS_WIDTH                        1  /* MICD_STS */

/*
 * R64 (0x40) - Charge Pump (1)
 */
#define WM8996_CP_ENA                           0x8000  /* CP_ENA */
#define WM8996_CP_ENA_MASK                      0x8000  /* CP_ENA */
#define WM8996_CP_ENA_SHIFT                         15  /* CP_ENA */
#define WM8996_CP_ENA_WIDTH                          1  /* CP_ENA */

/*
 * R65 (0x41) - Charge Pump (2)
 */
#define WM8996_CP_DISCH                         0x8000  /* CP_DISCH */
#define WM8996_CP_DISCH_MASK                    0x8000  /* CP_DISCH */
#define WM8996_CP_DISCH_SHIFT                       15  /* CP_DISCH */
#define WM8996_CP_DISCH_WIDTH                        1  /* CP_DISCH */

/*
 * R80 (0x50) - DC Servo (1)
 */
#define WM8996_DCS_ENA_CHAN_3                   0x0008  /* DCS_ENA_CHAN_3 */
#define WM8996_DCS_ENA_CHAN_3_MASK              0x0008  /* DCS_ENA_CHAN_3 */
#define WM8996_DCS_ENA_CHAN_3_SHIFT                  3  /* DCS_ENA_CHAN_3 */
#define WM8996_DCS_ENA_CHAN_3_WIDTH                  1  /* DCS_ENA_CHAN_3 */
#define WM8996_DCS_ENA_CHAN_2                   0x0004  /* DCS_ENA_CHAN_2 */
#define WM8996_DCS_ENA_CHAN_2_MASK              0x0004  /* DCS_ENA_CHAN_2 */
#define WM8996_DCS_ENA_CHAN_2_SHIFT                  2  /* DCS_ENA_CHAN_2 */
#define WM8996_DCS_ENA_CHAN_2_WIDTH                  1  /* DCS_ENA_CHAN_2 */
#define WM8996_DCS_ENA_CHAN_1                   0x0002  /* DCS_ENA_CHAN_1 */
#define WM8996_DCS_ENA_CHAN_1_MASK              0x0002  /* DCS_ENA_CHAN_1 */
#define WM8996_DCS_ENA_CHAN_1_SHIFT                  1  /* DCS_ENA_CHAN_1 */
#define WM8996_DCS_ENA_CHAN_1_WIDTH                  1  /* DCS_ENA_CHAN_1 */
#define WM8996_DCS_ENA_CHAN_0                   0x0001  /* DCS_ENA_CHAN_0 */
#define WM8996_DCS_ENA_CHAN_0_MASK              0x0001  /* DCS_ENA_CHAN_0 */
#define WM8996_DCS_ENA_CHAN_0_SHIFT                  0  /* DCS_ENA_CHAN_0 */
#define WM8996_DCS_ENA_CHAN_0_WIDTH                  1  /* DCS_ENA_CHAN_0 */

/*
 * R81 (0x51) - DC Servo (2)
 */
#define WM8996_DCS_TRIG_SINGLE_3                0x8000  /* DCS_TRIG_SINGLE_3 */
#define WM8996_DCS_TRIG_SINGLE_3_MASK           0x8000  /* DCS_TRIG_SINGLE_3 */
#define WM8996_DCS_TRIG_SINGLE_3_SHIFT              15  /* DCS_TRIG_SINGLE_3 */
#define WM8996_DCS_TRIG_SINGLE_3_WIDTH               1  /* DCS_TRIG_SINGLE_3 */
#define WM8996_DCS_TRIG_SINGLE_2                0x4000  /* DCS_TRIG_SINGLE_2 */
#define WM8996_DCS_TRIG_SINGLE_2_MASK           0x4000  /* DCS_TRIG_SINGLE_2 */
#define WM8996_DCS_TRIG_SINGLE_2_SHIFT              14  /* DCS_TRIG_SINGLE_2 */
#define WM8996_DCS_TRIG_SINGLE_2_WIDTH               1  /* DCS_TRIG_SINGLE_2 */
#define WM8996_DCS_TRIG_SINGLE_1                0x2000  /* DCS_TRIG_SINGLE_1 */
#define WM8996_DCS_TRIG_SINGLE_1_MASK           0x2000  /* DCS_TRIG_SINGLE_1 */
#define WM8996_DCS_TRIG_SINGLE_1_SHIFT              13  /* DCS_TRIG_SINGLE_1 */
#define WM8996_DCS_TRIG_SINGLE_1_WIDTH               1  /* DCS_TRIG_SINGLE_1 */
#define WM8996_DCS_TRIG_SINGLE_0                0x1000  /* DCS_TRIG_SINGLE_0 */
#define WM8996_DCS_TRIG_SINGLE_0_MASK           0x1000  /* DCS_TRIG_SINGLE_0 */
#define WM8996_DCS_TRIG_SINGLE_0_SHIFT              12  /* DCS_TRIG_SINGLE_0 */
#define WM8996_DCS_TRIG_SINGLE_0_WIDTH               1  /* DCS_TRIG_SINGLE_0 */
#define WM8996_DCS_TRIG_SERIES_3                0x0800  /* DCS_TRIG_SERIES_3 */
#define WM8996_DCS_TRIG_SERIES_3_MASK           0x0800  /* DCS_TRIG_SERIES_3 */
#define WM8996_DCS_TRIG_SERIES_3_SHIFT              11  /* DCS_TRIG_SERIES_3 */
#define WM8996_DCS_TRIG_SERIES_3_WIDTH               1  /* DCS_TRIG_SERIES_3 */
#define WM8996_DCS_TRIG_SERIES_2                0x0400  /* DCS_TRIG_SERIES_2 */
#define WM8996_DCS_TRIG_SERIES_2_MASK           0x0400  /* DCS_TRIG_SERIES_2 */
#define WM8996_DCS_TRIG_SERIES_2_SHIFT              10  /* DCS_TRIG_SERIES_2 */
#define WM8996_DCS_TRIG_SERIES_2_WIDTH               1  /* DCS_TRIG_SERIES_2 */
#define WM8996_DCS_TRIG_SERIES_1                0x0200  /* DCS_TRIG_SERIES_1 */
#define WM8996_DCS_TRIG_SERIES_1_MASK           0x0200  /* DCS_TRIG_SERIES_1 */
#define WM8996_DCS_TRIG_SERIES_1_SHIFT               9  /* DCS_TRIG_SERIES_1 */
#define WM8996_DCS_TRIG_SERIES_1_WIDTH               1  /* DCS_TRIG_SERIES_1 */
#define WM8996_DCS_TRIG_SERIES_0                0x0100  /* DCS_TRIG_SERIES_0 */
#define WM8996_DCS_TRIG_SERIES_0_MASK           0x0100  /* DCS_TRIG_SERIES_0 */
#define WM8996_DCS_TRIG_SERIES_0_SHIFT               8  /* DCS_TRIG_SERIES_0 */
#define WM8996_DCS_TRIG_SERIES_0_WIDTH               1  /* DCS_TRIG_SERIES_0 */
#define WM8996_DCS_TRIG_STARTUP_3               0x0080  /* DCS_TRIG_STARTUP_3 */
#define WM8996_DCS_TRIG_STARTUP_3_MASK          0x0080  /* DCS_TRIG_STARTUP_3 */
#define WM8996_DCS_TRIG_STARTUP_3_SHIFT              7  /* DCS_TRIG_STARTUP_3 */
#define WM8996_DCS_TRIG_STARTUP_3_WIDTH              1  /* DCS_TRIG_STARTUP_3 */
#define WM8996_DCS_TRIG_STARTUP_2               0x0040  /* DCS_TRIG_STARTUP_2 */
#define WM8996_DCS_TRIG_STARTUP_2_MASK          0x0040  /* DCS_TRIG_STARTUP_2 */
#define WM8996_DCS_TRIG_STARTUP_2_SHIFT              6  /* DCS_TRIG_STARTUP_2 */
#define WM8996_DCS_TRIG_STARTUP_2_WIDTH              1  /* DCS_TRIG_STARTUP_2 */
#define WM8996_DCS_TRIG_STARTUP_1               0x0020  /* DCS_TRIG_STARTUP_1 */
#define WM8996_DCS_TRIG_STARTUP_1_MASK          0x0020  /* DCS_TRIG_STARTUP_1 */
#define WM8996_DCS_TRIG_STARTUP_1_SHIFT              5  /* DCS_TRIG_STARTUP_1 */
#define WM8996_DCS_TRIG_STARTUP_1_WIDTH              1  /* DCS_TRIG_STARTUP_1 */
#define WM8996_DCS_TRIG_STARTUP_0               0x0010  /* DCS_TRIG_STARTUP_0 */
#define WM8996_DCS_TRIG_STARTUP_0_MASK          0x0010  /* DCS_TRIG_STARTUP_0 */
#define WM8996_DCS_TRIG_STARTUP_0_SHIFT              4  /* DCS_TRIG_STARTUP_0 */
#define WM8996_DCS_TRIG_STARTUP_0_WIDTH              1  /* DCS_TRIG_STARTUP_0 */
#define WM8996_DCS_TRIG_DAC_WR_3                0x0008  /* DCS_TRIG_DAC_WR_3 */
#define WM8996_DCS_TRIG_DAC_WR_3_MASK           0x0008  /* DCS_TRIG_DAC_WR_3 */
#define WM8996_DCS_TRIG_DAC_WR_3_SHIFT               3  /* DCS_TRIG_DAC_WR_3 */
#define WM8996_DCS_TRIG_DAC_WR_3_WIDTH               1  /* DCS_TRIG_DAC_WR_3 */
#define WM8996_DCS_TRIG_DAC_WR_2                0x0004  /* DCS_TRIG_DAC_WR_2 */
#define WM8996_DCS_TRIG_DAC_WR_2_MASK           0x0004  /* DCS_TRIG_DAC_WR_2 */
#define WM8996_DCS_TRIG_DAC_WR_2_SHIFT               2  /* DCS_TRIG_DAC_WR_2 */
#define WM8996_DCS_TRIG_DAC_WR_2_WIDTH               1  /* DCS_TRIG_DAC_WR_2 */
#define WM8996_DCS_TRIG_DAC_WR_1                0x0002  /* DCS_TRIG_DAC_WR_1 */
#define WM8996_DCS_TRIG_DAC_WR_1_MASK           0x0002  /* DCS_TRIG_DAC_WR_1 */
#define WM8996_DCS_TRIG_DAC_WR_1_SHIFT               1  /* DCS_TRIG_DAC_WR_1 */
#define WM8996_DCS_TRIG_DAC_WR_1_WIDTH               1  /* DCS_TRIG_DAC_WR_1 */
#define WM8996_DCS_TRIG_DAC_WR_0                0x0001  /* DCS_TRIG_DAC_WR_0 */
#define WM8996_DCS_TRIG_DAC_WR_0_MASK           0x0001  /* DCS_TRIG_DAC_WR_0 */
#define WM8996_DCS_TRIG_DAC_WR_0_SHIFT               0  /* DCS_TRIG_DAC_WR_0 */
#define WM8996_DCS_TRIG_DAC_WR_0_WIDTH               1  /* DCS_TRIG_DAC_WR_0 */

/*
 * R82 (0x52) - DC Servo (3)
 */
#define WM8996_DCS_TIMER_PERIOD_23_MASK         0x0F00  /* DCS_TIMER_PERIOD_23 - [11:8] */
#define WM8996_DCS_TIMER_PERIOD_23_SHIFT             8  /* DCS_TIMER_PERIOD_23 - [11:8] */
#define WM8996_DCS_TIMER_PERIOD_23_WIDTH             4  /* DCS_TIMER_PERIOD_23 - [11:8] */
#define WM8996_DCS_TIMER_PERIOD_01_MASK         0x000F  /* DCS_TIMER_PERIOD_01 - [3:0] */
#define WM8996_DCS_TIMER_PERIOD_01_SHIFT             0  /* DCS_TIMER_PERIOD_01 - [3:0] */
#define WM8996_DCS_TIMER_PERIOD_01_WIDTH             4  /* DCS_TIMER_PERIOD_01 - [3:0] */

/*
 * R84 (0x54) - DC Servo (5)
 */
#define WM8996_DCS_SERIES_NO_23_MASK            0x7F00  /* DCS_SERIES_NO_23 - [14:8] */
#define WM8996_DCS_SERIES_NO_23_SHIFT                8  /* DCS_SERIES_NO_23 - [14:8] */
#define WM8996_DCS_SERIES_NO_23_WIDTH                7  /* DCS_SERIES_NO_23 - [14:8] */
#define WM8996_DCS_SERIES_NO_01_MASK            0x007F  /* DCS_SERIES_NO_01 - [6:0] */
#define WM8996_DCS_SERIES_NO_01_SHIFT                0  /* DCS_SERIES_NO_01 - [6:0] */
#define WM8996_DCS_SERIES_NO_01_WIDTH                7  /* DCS_SERIES_NO_01 - [6:0] */

/*
 * R85 (0x55) - DC Servo (6)
 */
#define WM8996_DCS_DAC_WR_VAL_3_MASK            0xFF00  /* DCS_DAC_WR_VAL_3 - [15:8] */
#define WM8996_DCS_DAC_WR_VAL_3_SHIFT                8  /* DCS_DAC_WR_VAL_3 - [15:8] */
#define WM8996_DCS_DAC_WR_VAL_3_WIDTH                8  /* DCS_DAC_WR_VAL_3 - [15:8] */
#define WM8996_DCS_DAC_WR_VAL_2_MASK            0x00FF  /* DCS_DAC_WR_VAL_2 - [7:0] */
#define WM8996_DCS_DAC_WR_VAL_2_SHIFT                0  /* DCS_DAC_WR_VAL_2 - [7:0] */
#define WM8996_DCS_DAC_WR_VAL_2_WIDTH                8  /* DCS_DAC_WR_VAL_2 - [7:0] */

/*
 * R86 (0x56) - DC Servo (7)
 */
#define WM8996_DCS_DAC_WR_VAL_1_MASK            0xFF00  /* DCS_DAC_WR_VAL_1 - [15:8] */
#define WM8996_DCS_DAC_WR_VAL_1_SHIFT                8  /* DCS_DAC_WR_VAL_1 - [15:8] */
#define WM8996_DCS_DAC_WR_VAL_1_WIDTH                8  /* DCS_DAC_WR_VAL_1 - [15:8] */
#define WM8996_DCS_DAC_WR_VAL_0_MASK            0x00FF  /* DCS_DAC_WR_VAL_0 - [7:0] */
#define WM8996_DCS_DAC_WR_VAL_0_SHIFT                0  /* DCS_DAC_WR_VAL_0 - [7:0] */
#define WM8996_DCS_DAC_WR_VAL_0_WIDTH                8  /* DCS_DAC_WR_VAL_0 - [7:0] */

/*
 * R87 (0x57) - DC Servo Readback 0
 */
#define WM8996_DCS_CAL_COMPLETE_MASK            0x0F00  /* DCS_CAL_COMPLETE - [11:8] */
#define WM8996_DCS_CAL_COMPLETE_SHIFT                8  /* DCS_CAL_COMPLETE - [11:8] */
#define WM8996_DCS_CAL_COMPLETE_WIDTH                4  /* DCS_CAL_COMPLETE - [11:8] */
#define WM8996_DCS_DAC_WR_COMPLETE_MASK         0x00F0  /* DCS_DAC_WR_COMPLETE - [7:4] */
#define WM8996_DCS_DAC_WR_COMPLETE_SHIFT             4  /* DCS_DAC_WR_COMPLETE - [7:4] */
#define WM8996_DCS_DAC_WR_COMPLETE_WIDTH             4  /* DCS_DAC_WR_COMPLETE - [7:4] */
#define WM8996_DCS_STARTUP_COMPLETE_MASK        0x000F  /* DCS_STARTUP_COMPLETE - [3:0] */
#define WM8996_DCS_STARTUP_COMPLETE_SHIFT            0  /* DCS_STARTUP_COMPLETE - [3:0] */
#define WM8996_DCS_STARTUP_COMPLETE_WIDTH            4  /* DCS_STARTUP_COMPLETE - [3:0] */

/*
 * R96 (0x60) - Analogue HP (1)
 */
#define WM8996_HPOUT1L_RMV_SHORT                0x0080  /* HPOUT1L_RMV_SHORT */
#define WM8996_HPOUT1L_RMV_SHORT_MASK           0x0080  /* HPOUT1L_RMV_SHORT */
#define WM8996_HPOUT1L_RMV_SHORT_SHIFT               7  /* HPOUT1L_RMV_SHORT */
#define WM8996_HPOUT1L_RMV_SHORT_WIDTH               1  /* HPOUT1L_RMV_SHORT */
#define WM8996_HPOUT1L_OUTP                     0x0040  /* HPOUT1L_OUTP */
#define WM8996_HPOUT1L_OUTP_MASK                0x0040  /* HPOUT1L_OUTP */
#define WM8996_HPOUT1L_OUTP_SHIFT                    6  /* HPOUT1L_OUTP */
#define WM8996_HPOUT1L_OUTP_WIDTH                    1  /* HPOUT1L_OUTP */
#define WM8996_HPOUT1L_DLY                      0x0020  /* HPOUT1L_DLY */
#define WM8996_HPOUT1L_DLY_MASK                 0x0020  /* HPOUT1L_DLY */
#define WM8996_HPOUT1L_DLY_SHIFT                     5  /* HPOUT1L_DLY */
#define WM8996_HPOUT1L_DLY_WIDTH                     1  /* HPOUT1L_DLY */
#define WM8996_HPOUT1R_RMV_SHORT                0x0008  /* HPOUT1R_RMV_SHORT */
#define WM8996_HPOUT1R_RMV_SHORT_MASK           0x0008  /* HPOUT1R_RMV_SHORT */
#define WM8996_HPOUT1R_RMV_SHORT_SHIFT               3  /* HPOUT1R_RMV_SHORT */
#define WM8996_HPOUT1R_RMV_SHORT_WIDTH               1  /* HPOUT1R_RMV_SHORT */
#define WM8996_HPOUT1R_OUTP                     0x0004  /* HPOUT1R_OUTP */
#define WM8996_HPOUT1R_OUTP_MASK                0x0004  /* HPOUT1R_OUTP */
#define WM8996_HPOUT1R_OUTP_SHIFT                    2  /* HPOUT1R_OUTP */
#define WM8996_HPOUT1R_OUTP_WIDTH                    1  /* HPOUT1R_OUTP */
#define WM8996_HPOUT1R_DLY                      0x0002  /* HPOUT1R_DLY */
#define WM8996_HPOUT1R_DLY_MASK                 0x0002  /* HPOUT1R_DLY */
#define WM8996_HPOUT1R_DLY_SHIFT                     1  /* HPOUT1R_DLY */
#define WM8996_HPOUT1R_DLY_WIDTH                     1  /* HPOUT1R_DLY */

/*
 * R97 (0x61) - Analogue HP (2)
 */
#define WM8996_HPOUT2L_RMV_SHORT                0x0080  /* HPOUT2L_RMV_SHORT */
#define WM8996_HPOUT2L_RMV_SHORT_MASK           0x0080  /* HPOUT2L_RMV_SHORT */
#define WM8996_HPOUT2L_RMV_SHORT_SHIFT               7  /* HPOUT2L_RMV_SHORT */
#define WM8996_HPOUT2L_RMV_SHORT_WIDTH               1  /* HPOUT2L_RMV_SHORT */
#define WM8996_HPOUT2L_OUTP                     0x0040  /* HPOUT2L_OUTP */
#define WM8996_HPOUT2L_OUTP_MASK                0x0040  /* HPOUT2L_OUTP */
#define WM8996_HPOUT2L_OUTP_SHIFT                    6  /* HPOUT2L_OUTP */
#define WM8996_HPOUT2L_OUTP_WIDTH                    1  /* HPOUT2L_OUTP */
#define WM8996_HPOUT2L_DLY                      0x0020  /* HPOUT2L_DLY */
#define WM8996_HPOUT2L_DLY_MASK                 0x0020  /* HPOUT2L_DLY */
#define WM8996_HPOUT2L_DLY_SHIFT                     5  /* HPOUT2L_DLY */
#define WM8996_HPOUT2L_DLY_WIDTH                     1  /* HPOUT2L_DLY */
#define WM8996_HPOUT2R_RMV_SHORT                0x0008  /* HPOUT2R_RMV_SHORT */
#define WM8996_HPOUT2R_RMV_SHORT_MASK           0x0008  /* HPOUT2R_RMV_SHORT */
#define WM8996_HPOUT2R_RMV_SHORT_SHIFT               3  /* HPOUT2R_RMV_SHORT */
#define WM8996_HPOUT2R_RMV_SHORT_WIDTH               1  /* HPOUT2R_RMV_SHORT */
#define WM8996_HPOUT2R_OUTP                     0x0004  /* HPOUT2R_OUTP */
#define WM8996_HPOUT2R_OUTP_MASK                0x0004  /* HPOUT2R_OUTP */
#define WM8996_HPOUT2R_OUTP_SHIFT                    2  /* HPOUT2R_OUTP */
#define WM8996_HPOUT2R_OUTP_WIDTH                    1  /* HPOUT2R_OUTP */
#define WM8996_HPOUT2R_DLY                      0x0002  /* HPOUT2R_DLY */
#define WM8996_HPOUT2R_DLY_MASK                 0x0002  /* HPOUT2R_DLY */
#define WM8996_HPOUT2R_DLY_SHIFT                     1  /* HPOUT2R_DLY */
#define WM8996_HPOUT2R_DLY_WIDTH                     1  /* HPOUT2R_DLY */

/*
 * R256 (0x100) - Chip Revision
 */
#define WM8996_CHIP_REV_MASK                    0x000F  /* CHIP_REV - [3:0] */
#define WM8996_CHIP_REV_SHIFT                        0  /* CHIP_REV - [3:0] */
#define WM8996_CHIP_REV_WIDTH                        4  /* CHIP_REV - [3:0] */

/*
 * R257 (0x101) - Control Interface (1)
 */
#define WM8996_REG_SYNC                         0x8000  /* REG_SYNC */
#define WM8996_REG_SYNC_MASK                    0x8000  /* REG_SYNC */
#define WM8996_REG_SYNC_SHIFT                       15  /* REG_SYNC */
#define WM8996_REG_SYNC_WIDTH                        1  /* REG_SYNC */
#define WM8996_AUTO_INC                         0x0004  /* AUTO_INC */
#define WM8996_AUTO_INC_MASK                    0x0004  /* AUTO_INC */
#define WM8996_AUTO_INC_SHIFT                        2  /* AUTO_INC */
#define WM8996_AUTO_INC_WIDTH                        1  /* AUTO_INC */

/*
 * R272 (0x110) - Write Sequencer Ctrl (1)
 */
#define WM8996_WSEQ_ENA                         0x8000  /* WSEQ_ENA */
#define WM8996_WSEQ_ENA_MASK                    0x8000  /* WSEQ_ENA */
#define WM8996_WSEQ_ENA_SHIFT                       15  /* WSEQ_ENA */
#define WM8996_WSEQ_ENA_WIDTH                        1  /* WSEQ_ENA */
#define WM8996_WSEQ_ABORT                       0x0200  /* WSEQ_ABORT */
#define WM8996_WSEQ_ABORT_MASK                  0x0200  /* WSEQ_ABORT */
#define WM8996_WSEQ_ABORT_SHIFT                      9  /* WSEQ_ABORT */
#define WM8996_WSEQ_ABORT_WIDTH                      1  /* WSEQ_ABORT */
#define WM8996_WSEQ_START                       0x0100  /* WSEQ_START */
#define WM8996_WSEQ_START_MASK                  0x0100  /* WSEQ_START */
#define WM8996_WSEQ_START_SHIFT                      8  /* WSEQ_START */
#define WM8996_WSEQ_START_WIDTH                      1  /* WSEQ_START */
#define WM8996_WSEQ_START_INDEX_MASK            0x007F  /* WSEQ_START_INDEX - [6:0] */
#define WM8996_WSEQ_START_INDEX_SHIFT                0  /* WSEQ_START_INDEX - [6:0] */
#define WM8996_WSEQ_START_INDEX_WIDTH                7  /* WSEQ_START_INDEX - [6:0] */

/*
 * R273 (0x111) - Write Sequencer Ctrl (2)
 */
#define WM8996_WSEQ_BUSY                        0x0100  /* WSEQ_BUSY */
#define WM8996_WSEQ_BUSY_MASK                   0x0100  /* WSEQ_BUSY */
#define WM8996_WSEQ_BUSY_SHIFT                       8  /* WSEQ_BUSY */
#define WM8996_WSEQ_BUSY_WIDTH                       1  /* WSEQ_BUSY */
#define WM8996_WSEQ_CURRENT_INDEX_MASK          0x007F  /* WSEQ_CURRENT_INDEX - [6:0] */
#define WM8996_WSEQ_CURRENT_INDEX_SHIFT              0  /* WSEQ_CURRENT_INDEX - [6:0] */
#define WM8996_WSEQ_CURRENT_INDEX_WIDTH              7  /* WSEQ_CURRENT_INDEX - [6:0] */

/*
 * R512 (0x200) - AIF Clocking (1)
 */
#define WM8996_SYSCLK_SRC_MASK                  0x0018  /* SYSCLK_SRC - [4:3] */
#define WM8996_SYSCLK_SRC_SHIFT                      3  /* SYSCLK_SRC - [4:3] */
#define WM8996_SYSCLK_SRC_WIDTH                      2  /* SYSCLK_SRC - [4:3] */
#define WM8996_SYSCLK_INV                       0x0004  /* SYSCLK_INV */
#define WM8996_SYSCLK_INV_MASK                  0x0004  /* SYSCLK_INV */
#define WM8996_SYSCLK_INV_SHIFT                      2  /* SYSCLK_INV */
#define WM8996_SYSCLK_INV_WIDTH                      1  /* SYSCLK_INV */
#define WM8996_SYSCLK_DIV                       0x0002  /* SYSCLK_DIV */
#define WM8996_SYSCLK_DIV_MASK                  0x0002  /* SYSCLK_DIV */
#define WM8996_SYSCLK_DIV_SHIFT                      1  /* SYSCLK_DIV */
#define WM8996_SYSCLK_DIV_WIDTH                      1  /* SYSCLK_DIV */
#define WM8996_SYSCLK_ENA                       0x0001  /* SYSCLK_ENA */
#define WM8996_SYSCLK_ENA_MASK                  0x0001  /* SYSCLK_ENA */
#define WM8996_SYSCLK_ENA_SHIFT                      0  /* SYSCLK_ENA */
#define WM8996_SYSCLK_ENA_WIDTH                      1  /* SYSCLK_ENA */

/*
 * R513 (0x201) - AIF Clocking (2)
 */
#define WM8996_DSP2_DIV_MASK                    0x0018  /* DSP2_DIV - [4:3] */
#define WM8996_DSP2_DIV_SHIFT                        3  /* DSP2_DIV - [4:3] */
#define WM8996_DSP2_DIV_WIDTH                        2  /* DSP2_DIV - [4:3] */
#define WM8996_DSP1_DIV_MASK                    0x0003  /* DSP1_DIV - [1:0] */
#define WM8996_DSP1_DIV_SHIFT                        0  /* DSP1_DIV - [1:0] */
#define WM8996_DSP1_DIV_WIDTH                        2  /* DSP1_DIV - [1:0] */

/*
 * R520 (0x208) - Clocking (1)
 */
#define WM8996_LFCLK_ENA                        0x0020  /* LFCLK_ENA */
#define WM8996_LFCLK_ENA_MASK                   0x0020  /* LFCLK_ENA */
#define WM8996_LFCLK_ENA_SHIFT                       5  /* LFCLK_ENA */
#define WM8996_LFCLK_ENA_WIDTH                       1  /* LFCLK_ENA */
#define WM8996_TOCLK_ENA                        0x0010  /* TOCLK_ENA */
#define WM8996_TOCLK_ENA_MASK                   0x0010  /* TOCLK_ENA */
#define WM8996_TOCLK_ENA_SHIFT                       4  /* TOCLK_ENA */
#define WM8996_TOCLK_ENA_WIDTH                       1  /* TOCLK_ENA */
#define WM8996_AIFCLK_ENA                       0x0004  /* AIFCLK_ENA */
#define WM8996_AIFCLK_ENA_MASK                  0x0004  /* AIFCLK_ENA */
#define WM8996_AIFCLK_ENA_SHIFT                      2  /* AIFCLK_ENA */
#define WM8996_AIFCLK_ENA_WIDTH                      1  /* AIFCLK_ENA */
#define WM8996_SYSDSPCLK_ENA                    0x0002  /* SYSDSPCLK_ENA */
#define WM8996_SYSDSPCLK_ENA_MASK               0x0002  /* SYSDSPCLK_ENA */
#define WM8996_SYSDSPCLK_ENA_SHIFT                   1  /* SYSDSPCLK_ENA */
#define WM8996_SYSDSPCLK_ENA_WIDTH                   1  /* SYSDSPCLK_ENA */

/*
 * R521 (0x209) - Clocking (2)
 */
#define WM8996_TOCLK_DIV_MASK                   0x0700  /* TOCLK_DIV - [10:8] */
#define WM8996_TOCLK_DIV_SHIFT                       8  /* TOCLK_DIV - [10:8] */
#define WM8996_TOCLK_DIV_WIDTH                       3  /* TOCLK_DIV - [10:8] */
#define WM8996_DBCLK_DIV_MASK                   0x00F0  /* DBCLK_DIV - [7:4] */
#define WM8996_DBCLK_DIV_SHIFT                       4  /* DBCLK_DIV - [7:4] */
#define WM8996_DBCLK_DIV_WIDTH                       4  /* DBCLK_DIV - [7:4] */
#define WM8996_OPCLK_DIV_MASK                   0x0007  /* OPCLK_DIV - [2:0] */
#define WM8996_OPCLK_DIV_SHIFT                       0  /* OPCLK_DIV - [2:0] */
#define WM8996_OPCLK_DIV_WIDTH                       3  /* OPCLK_DIV - [2:0] */

/*
 * R528 (0x210) - AIF Rate
 */
#define WM8996_SYSCLK_RATE                      0x0001  /* SYSCLK_RATE */
#define WM8996_SYSCLK_RATE_MASK                 0x0001  /* SYSCLK_RATE */
#define WM8996_SYSCLK_RATE_SHIFT                     0  /* SYSCLK_RATE */
#define WM8996_SYSCLK_RATE_WIDTH                     1  /* SYSCLK_RATE */

/*
 * R544 (0x220) - FLL Control (1)
 */
#define WM8996_FLL_OSC_ENA                      0x0002  /* FLL_OSC_ENA */
#define WM8996_FLL_OSC_ENA_MASK                 0x0002  /* FLL_OSC_ENA */
#define WM8996_FLL_OSC_ENA_SHIFT                     1  /* FLL_OSC_ENA */
#define WM8996_FLL_OSC_ENA_WIDTH                     1  /* FLL_OSC_ENA */
#define WM8996_FLL_ENA                          0x0001  /* FLL_ENA */
#define WM8996_FLL_ENA_MASK                     0x0001  /* FLL_ENA */
#define WM8996_FLL_ENA_SHIFT                         0  /* FLL_ENA */
#define WM8996_FLL_ENA_WIDTH                         1  /* FLL_ENA */

/*
 * R545 (0x221) - FLL Control (2)
 */
#define WM8996_FLL_OUTDIV_MASK                  0x3F00  /* FLL_OUTDIV - [13:8] */
#define WM8996_FLL_OUTDIV_SHIFT                      8  /* FLL_OUTDIV - [13:8] */
#define WM8996_FLL_OUTDIV_WIDTH                      6  /* FLL_OUTDIV - [13:8] */
#define WM8996_FLL_FRATIO_MASK                  0x0007  /* FLL_FRATIO - [2:0] */
#define WM8996_FLL_FRATIO_SHIFT                      0  /* FLL_FRATIO - [2:0] */
#define WM8996_FLL_FRATIO_WIDTH                      3  /* FLL_FRATIO - [2:0] */

/*
 * R546 (0x222) - FLL Control (3)
 */
#define WM8996_FLL_THETA_MASK                   0xFFFF  /* FLL_THETA - [15:0] */
#define WM8996_FLL_THETA_SHIFT                       0  /* FLL_THETA - [15:0] */
#define WM8996_FLL_THETA_WIDTH                      16  /* FLL_THETA - [15:0] */

/*
 * R547 (0x223) - FLL Control (4)
 */
#define WM8996_FLL_N_MASK                       0x7FE0  /* FLL_N - [14:5] */
#define WM8996_FLL_N_SHIFT                           5  /* FLL_N - [14:5] */
#define WM8996_FLL_N_WIDTH                          10  /* FLL_N - [14:5] */
#define WM8996_FLL_LOOP_GAIN_MASK               0x000F  /* FLL_LOOP_GAIN - [3:0] */
#define WM8996_FLL_LOOP_GAIN_SHIFT                   0  /* FLL_LOOP_GAIN - [3:0] */
#define WM8996_FLL_LOOP_GAIN_WIDTH                   4  /* FLL_LOOP_GAIN - [3:0] */

/*
 * R548 (0x224) - FLL Control (5)
 */
#define WM8996_FLL_FRC_NCO_VAL_MASK             0x1F80  /* FLL_FRC_NCO_VAL - [12:7] */
#define WM8996_FLL_FRC_NCO_VAL_SHIFT                 7  /* FLL_FRC_NCO_VAL - [12:7] */
#define WM8996_FLL_FRC_NCO_VAL_WIDTH                 6  /* FLL_FRC_NCO_VAL - [12:7] */
#define WM8996_FLL_FRC_NCO                      0x0040  /* FLL_FRC_NCO */
#define WM8996_FLL_FRC_NCO_MASK                 0x0040  /* FLL_FRC_NCO */
#define WM8996_FLL_FRC_NCO_SHIFT                     6  /* FLL_FRC_NCO */
#define WM8996_FLL_FRC_NCO_WIDTH                     1  /* FLL_FRC_NCO */
#define WM8996_FLL_REFCLK_DIV_MASK              0x0018  /* FLL_REFCLK_DIV - [4:3] */
#define WM8996_FLL_REFCLK_DIV_SHIFT                  3  /* FLL_REFCLK_DIV - [4:3] */
#define WM8996_FLL_REFCLK_DIV_WIDTH                  2  /* FLL_REFCLK_DIV - [4:3] */
#define WM8996_FLL_REF_FREQ                     0x0004  /* FLL_REF_FREQ */
#define WM8996_FLL_REF_FREQ_MASK                0x0004  /* FLL_REF_FREQ */
#define WM8996_FLL_REF_FREQ_SHIFT                    2  /* FLL_REF_FREQ */
#define WM8996_FLL_REF_FREQ_WIDTH                    1  /* FLL_REF_FREQ */
#define WM8996_FLL_REFCLK_SRC_MASK              0x0003  /* FLL_REFCLK_SRC - [1:0] */
#define WM8996_FLL_REFCLK_SRC_SHIFT                  0  /* FLL_REFCLK_SRC - [1:0] */
#define WM8996_FLL_REFCLK_SRC_WIDTH                  2  /* FLL_REFCLK_SRC - [1:0] */

/*
 * R549 (0x225) - FLL Control (6)
 */
#define WM8996_FLL_REFCLK_SRC_STS_MASK          0x000C  /* FLL_REFCLK_SRC_STS - [3:2] */
#define WM8996_FLL_REFCLK_SRC_STS_SHIFT              2  /* FLL_REFCLK_SRC_STS - [3:2] */
#define WM8996_FLL_REFCLK_SRC_STS_WIDTH              2  /* FLL_REFCLK_SRC_STS - [3:2] */
#define WM8996_FLL_SWITCH_CLK                   0x0001  /* FLL_SWITCH_CLK */
#define WM8996_FLL_SWITCH_CLK_MASK              0x0001  /* FLL_SWITCH_CLK */
#define WM8996_FLL_SWITCH_CLK_SHIFT                  0  /* FLL_SWITCH_CLK */
#define WM8996_FLL_SWITCH_CLK_WIDTH                  1  /* FLL_SWITCH_CLK */

/*
 * R550 (0x226) - FLL EFS 1
 */
#define WM8996_FLL_LAMBDA_MASK                  0xFFFF  /* FLL_LAMBDA - [15:0] */
#define WM8996_FLL_LAMBDA_SHIFT                      0  /* FLL_LAMBDA - [15:0] */
#define WM8996_FLL_LAMBDA_WIDTH                     16  /* FLL_LAMBDA - [15:0] */

/*
 * R551 (0x227) - FLL EFS 2
 */
#define WM8996_FLL_LFSR_SEL_MASK                0x0006  /* FLL_LFSR_SEL - [2:1] */
#define WM8996_FLL_LFSR_SEL_SHIFT                    1  /* FLL_LFSR_SEL - [2:1] */
#define WM8996_FLL_LFSR_SEL_WIDTH                    2  /* FLL_LFSR_SEL - [2:1] */
#define WM8996_FLL_EFS_ENA                      0x0001  /* FLL_EFS_ENA */
#define WM8996_FLL_EFS_ENA_MASK                 0x0001  /* FLL_EFS_ENA */
#define WM8996_FLL_EFS_ENA_SHIFT                     0  /* FLL_EFS_ENA */
#define WM8996_FLL_EFS_ENA_WIDTH                     1  /* FLL_EFS_ENA */

/*
 * R768 (0x300) - AIF1 Control
 */
#define WM8996_AIF1_TRI                         0x0004  /* AIF1_TRI */
#define WM8996_AIF1_TRI_MASK                    0x0004  /* AIF1_TRI */
#define WM8996_AIF1_TRI_SHIFT                        2  /* AIF1_TRI */
#define WM8996_AIF1_TRI_WIDTH                        1  /* AIF1_TRI */
#define WM8996_AIF1_FMT_MASK                    0x0003  /* AIF1_FMT - [1:0] */
#define WM8996_AIF1_FMT_SHIFT                        0  /* AIF1_FMT - [1:0] */
#define WM8996_AIF1_FMT_WIDTH                        2  /* AIF1_FMT - [1:0] */

/*
 * R769 (0x301) - AIF1 BCLK
 */
#define WM8996_AIF1_BCLK_INV                    0x0400  /* AIF1_BCLK_INV */
#define WM8996_AIF1_BCLK_INV_MASK               0x0400  /* AIF1_BCLK_INV */
#define WM8996_AIF1_BCLK_INV_SHIFT                  10  /* AIF1_BCLK_INV */
#define WM8996_AIF1_BCLK_INV_WIDTH                   1  /* AIF1_BCLK_INV */
#define WM8996_AIF1_BCLK_FRC                    0x0200  /* AIF1_BCLK_FRC */
#define WM8996_AIF1_BCLK_FRC_MASK               0x0200  /* AIF1_BCLK_FRC */
#define WM8996_AIF1_BCLK_FRC_SHIFT                   9  /* AIF1_BCLK_FRC */
#define WM8996_AIF1_BCLK_FRC_WIDTH                   1  /* AIF1_BCLK_FRC */
#define WM8996_AIF1_BCLK_MSTR                   0x0100  /* AIF1_BCLK_MSTR */
#define WM8996_AIF1_BCLK_MSTR_MASK              0x0100  /* AIF1_BCLK_MSTR */
#define WM8996_AIF1_BCLK_MSTR_SHIFT                  8  /* AIF1_BCLK_MSTR */
#define WM8996_AIF1_BCLK_MSTR_WIDTH                  1  /* AIF1_BCLK_MSTR */
#define WM8996_AIF1_BCLK_DIV_MASK               0x000F  /* AIF1_BCLK_DIV - [3:0] */
#define WM8996_AIF1_BCLK_DIV_SHIFT                   0  /* AIF1_BCLK_DIV - [3:0] */
#define WM8996_AIF1_BCLK_DIV_WIDTH                   4  /* AIF1_BCLK_DIV - [3:0] */

/*
 * R770 (0x302) - AIF1 TX LRCLK(1)
 */
#define WM8996_AIF1TX_RATE_MASK                 0x07FF  /* AIF1TX_RATE - [10:0] */
#define WM8996_AIF1TX_RATE_SHIFT                     0  /* AIF1TX_RATE - [10:0] */
#define WM8996_AIF1TX_RATE_WIDTH                    11  /* AIF1TX_RATE - [10:0] */

/*
 * R771 (0x303) - AIF1 TX LRCLK(2)
 */
#define WM8996_AIF1TX_LRCLK_MODE                0x0008  /* AIF1TX_LRCLK_MODE */
#define WM8996_AIF1TX_LRCLK_MODE_MASK           0x0008  /* AIF1TX_LRCLK_MODE */
#define WM8996_AIF1TX_LRCLK_MODE_SHIFT               3  /* AIF1TX_LRCLK_MODE */
#define WM8996_AIF1TX_LRCLK_MODE_WIDTH               1  /* AIF1TX_LRCLK_MODE */
#define WM8996_AIF1TX_LRCLK_INV                 0x0004  /* AIF1TX_LRCLK_INV */
#define WM8996_AIF1TX_LRCLK_INV_MASK            0x0004  /* AIF1TX_LRCLK_INV */
#define WM8996_AIF1TX_LRCLK_INV_SHIFT                2  /* AIF1TX_LRCLK_INV */
#define WM8996_AIF1TX_LRCLK_INV_WIDTH                1  /* AIF1TX_LRCLK_INV */
#define WM8996_AIF1TX_LRCLK_FRC                 0x0002  /* AIF1TX_LRCLK_FRC */
#define WM8996_AIF1TX_LRCLK_FRC_MASK            0x0002  /* AIF1TX_LRCLK_FRC */
#define WM8996_AIF1TX_LRCLK_FRC_SHIFT                1  /* AIF1TX_LRCLK_FRC */
#define WM8996_AIF1TX_LRCLK_FRC_WIDTH                1  /* AIF1TX_LRCLK_FRC */
#define WM8996_AIF1TX_LRCLK_MSTR                0x0001  /* AIF1TX_LRCLK_MSTR */
#define WM8996_AIF1TX_LRCLK_MSTR_MASK           0x0001  /* AIF1TX_LRCLK_MSTR */
#define WM8996_AIF1TX_LRCLK_MSTR_SHIFT               0  /* AIF1TX_LRCLK_MSTR */
#define WM8996_AIF1TX_LRCLK_MSTR_WIDTH               1  /* AIF1TX_LRCLK_MSTR */

/*
 * R772 (0x304) - AIF1 RX LRCLK(1)
 */
#define WM8996_AIF1RX_RATE_MASK                 0x07FF  /* AIF1RX_RATE - [10:0] */
#define WM8996_AIF1RX_RATE_SHIFT                     0  /* AIF1RX_RATE - [10:0] */
#define WM8996_AIF1RX_RATE_WIDTH                    11  /* AIF1RX_RATE - [10:0] */

/*
 * R773 (0x305) - AIF1 RX LRCLK(2)
 */
#define WM8996_AIF1RX_LRCLK_INV                 0x0004  /* AIF1RX_LRCLK_INV */
#define WM8996_AIF1RX_LRCLK_INV_MASK            0x0004  /* AIF1RX_LRCLK_INV */
#define WM8996_AIF1RX_LRCLK_INV_SHIFT                2  /* AIF1RX_LRCLK_INV */
#define WM8996_AIF1RX_LRCLK_INV_WIDTH                1  /* AIF1RX_LRCLK_INV */
#define WM8996_AIF1RX_LRCLK_FRC                 0x0002  /* AIF1RX_LRCLK_FRC */
#define WM8996_AIF1RX_LRCLK_FRC_MASK            0x0002  /* AIF1RX_LRCLK_FRC */
#define WM8996_AIF1RX_LRCLK_FRC_SHIFT                1  /* AIF1RX_LRCLK_FRC */
#define WM8996_AIF1RX_LRCLK_FRC_WIDTH                1  /* AIF1RX_LRCLK_FRC */
#define WM8996_AIF1RX_LRCLK_MSTR                0x0001  /* AIF1RX_LRCLK_MSTR */
#define WM8996_AIF1RX_LRCLK_MSTR_MASK           0x0001  /* AIF1RX_LRCLK_MSTR */
#define WM8996_AIF1RX_LRCLK_MSTR_SHIFT               0  /* AIF1RX_LRCLK_MSTR */
#define WM8996_AIF1RX_LRCLK_MSTR_WIDTH               1  /* AIF1RX_LRCLK_MSTR */

/*
 * R774 (0x306) - AIF1TX Data Configuration (1)
 */
#define WM8996_AIF1TX_WL_MASK                   0xFF00  /* AIF1TX_WL - [15:8] */
#define WM8996_AIF1TX_WL_SHIFT                       8  /* AIF1TX_WL - [15:8] */
#define WM8996_AIF1TX_WL_WIDTH                       8  /* AIF1TX_WL - [15:8] */
#define WM8996_AIF1TX_SLOT_LEN_MASK             0x00FF  /* AIF1TX_SLOT_LEN - [7:0] */
#define WM8996_AIF1TX_SLOT_LEN_SHIFT                 0  /* AIF1TX_SLOT_LEN - [7:0] */
#define WM8996_AIF1TX_SLOT_LEN_WIDTH                 8  /* AIF1TX_SLOT_LEN - [7:0] */

/*
 * R775 (0x307) - AIF1TX Data Configuration (2)
 */
#define WM8996_AIF1TX_DAT_TRI                   0x0001  /* AIF1TX_DAT_TRI */
#define WM8996_AIF1TX_DAT_TRI_MASK              0x0001  /* AIF1TX_DAT_TRI */
#define WM8996_AIF1TX_DAT_TRI_SHIFT                  0  /* AIF1TX_DAT_TRI */
#define WM8996_AIF1TX_DAT_TRI_WIDTH                  1  /* AIF1TX_DAT_TRI */

/*
 * R776 (0x308) - AIF1RX Data Configuration
 */
#define WM8996_AIF1RX_WL_MASK                   0xFF00  /* AIF1RX_WL - [15:8] */
#define WM8996_AIF1RX_WL_SHIFT                       8  /* AIF1RX_WL - [15:8] */
#define WM8996_AIF1RX_WL_WIDTH                       8  /* AIF1RX_WL - [15:8] */
#define WM8996_AIF1RX_SLOT_LEN_MASK             0x00FF  /* AIF1RX_SLOT_LEN - [7:0] */
#define WM8996_AIF1RX_SLOT_LEN_SHIFT                 0  /* AIF1RX_SLOT_LEN - [7:0] */
#define WM8996_AIF1RX_SLOT_LEN_WIDTH                 8  /* AIF1RX_SLOT_LEN - [7:0] */

/*
 * R777 (0x309) - AIF1TX Channel 0 Configuration
 */
#define WM8996_AIF1TX_CHAN0_DAT_INV             0x8000  /* AIF1TX_CHAN0_DAT_INV */
#define WM8996_AIF1TX_CHAN0_DAT_INV_MASK        0x8000  /* AIF1TX_CHAN0_DAT_INV */
#define WM8996_AIF1TX_CHAN0_DAT_INV_SHIFT           15  /* AIF1TX_CHAN0_DAT_INV */
#define WM8996_AIF1TX_CHAN0_DAT_INV_WIDTH            1  /* AIF1TX_CHAN0_DAT_INV */
#define WM8996_AIF1TX_CHAN0_SPACING_MASK        0x7E00  /* AIF1TX_CHAN0_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN0_SPACING_SHIFT            9  /* AIF1TX_CHAN0_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN0_SPACING_WIDTH            6  /* AIF1TX_CHAN0_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN0_SLOTS_MASK          0x01C0  /* AIF1TX_CHAN0_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN0_SLOTS_SHIFT              6  /* AIF1TX_CHAN0_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN0_SLOTS_WIDTH              3  /* AIF1TX_CHAN0_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN0_START_SLOT_MASK     0x003F  /* AIF1TX_CHAN0_START_SLOT - [5:0] */
#define WM8996_AIF1TX_CHAN0_START_SLOT_SHIFT         0  /* AIF1TX_CHAN0_START_SLOT - [5:0] */
#define WM8996_AIF1TX_CHAN0_START_SLOT_WIDTH         6  /* AIF1TX_CHAN0_START_SLOT - [5:0] */

/*
 * R778 (0x30A) - AIF1TX Channel 1 Configuration
 */
#define WM8996_AIF1TX_CHAN1_DAT_INV             0x8000  /* AIF1TX_CHAN1_DAT_INV */
#define WM8996_AIF1TX_CHAN1_DAT_INV_MASK        0x8000  /* AIF1TX_CHAN1_DAT_INV */
#define WM8996_AIF1TX_CHAN1_DAT_INV_SHIFT           15  /* AIF1TX_CHAN1_DAT_INV */
#define WM8996_AIF1TX_CHAN1_DAT_INV_WIDTH            1  /* AIF1TX_CHAN1_DAT_INV */
#define WM8996_AIF1TX_CHAN1_SPACING_MASK        0x7E00  /* AIF1TX_CHAN1_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN1_SPACING_SHIFT            9  /* AIF1TX_CHAN1_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN1_SPACING_WIDTH            6  /* AIF1TX_CHAN1_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN1_SLOTS_MASK          0x01C0  /* AIF1TX_CHAN1_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN1_SLOTS_SHIFT              6  /* AIF1TX_CHAN1_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN1_SLOTS_WIDTH              3  /* AIF1TX_CHAN1_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN1_START_SLOT_MASK     0x003F  /* AIF1TX_CHAN1_START_SLOT - [5:0] */
#define WM8996_AIF1TX_CHAN1_START_SLOT_SHIFT         0  /* AIF1TX_CHAN1_START_SLOT - [5:0] */
#define WM8996_AIF1TX_CHAN1_START_SLOT_WIDTH         6  /* AIF1TX_CHAN1_START_SLOT - [5:0] */

/*
 * R779 (0x30B) - AIF1TX Channel 2 Configuration
 */
#define WM8996_AIF1TX_CHAN2_DAT_INV             0x8000  /* AIF1TX_CHAN2_DAT_INV */
#define WM8996_AIF1TX_CHAN2_DAT_INV_MASK        0x8000  /* AIF1TX_CHAN2_DAT_INV */
#define WM8996_AIF1TX_CHAN2_DAT_INV_SHIFT           15  /* AIF1TX_CHAN2_DAT_INV */
#define WM8996_AIF1TX_CHAN2_DAT_INV_WIDTH            1  /* AIF1TX_CHAN2_DAT_INV */
#define WM8996_AIF1TX_CHAN2_SPACING_MASK        0x7E00  /* AIF1TX_CHAN2_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN2_SPACING_SHIFT            9  /* AIF1TX_CHAN2_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN2_SPACING_WIDTH            6  /* AIF1TX_CHAN2_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN2_SLOTS_MASK          0x01C0  /* AIF1TX_CHAN2_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN2_SLOTS_SHIFT              6  /* AIF1TX_CHAN2_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN2_SLOTS_WIDTH              3  /* AIF1TX_CHAN2_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN2_START_SLOT_MASK     0x003F  /* AIF1TX_CHAN2_START_SLOT - [5:0] */
#define WM8996_AIF1TX_CHAN2_START_SLOT_SHIFT         0  /* AIF1TX_CHAN2_START_SLOT - [5:0] */
#define WM8996_AIF1TX_CHAN2_START_SLOT_WIDTH         6  /* AIF1TX_CHAN2_START_SLOT - [5:0] */

/*
 * R780 (0x30C) - AIF1TX Channel 3 Configuration
 */
#define WM8996_AIF1TX_CHAN3_DAT_INV             0x8000  /* AIF1TX_CHAN3_DAT_INV */
#define WM8996_AIF1TX_CHAN3_DAT_INV_MASK        0x8000  /* AIF1TX_CHAN3_DAT_INV */
#define WM8996_AIF1TX_CHAN3_DAT_INV_SHIFT           15  /* AIF1TX_CHAN3_DAT_INV */
#define WM8996_AIF1TX_CHAN3_DAT_INV_WIDTH            1  /* AIF1TX_CHAN3_DAT_INV */
#define WM8996_AIF1TX_CHAN3_SPACING_MASK        0x7E00  /* AIF1TX_CHAN3_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN3_SPACING_SHIFT            9  /* AIF1TX_CHAN3_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN3_SPACING_WIDTH            6  /* AIF1TX_CHAN3_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN3_SLOTS_MASK          0x01C0  /* AIF1TX_CHAN3_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN3_SLOTS_SHIFT              6  /* AIF1TX_CHAN3_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN3_SLOTS_WIDTH              3  /* AIF1TX_CHAN3_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN3_START_SLOT_MASK     0x003F  /* AIF1TX_CHAN3_START_SLOT - [5:0] */
#define WM8996_AIF1TX_CHAN3_START_SLOT_SHIFT         0  /* AIF1TX_CHAN3_START_SLOT - [5:0] */
#define WM8996_AIF1TX_CHAN3_START_SLOT_WIDTH         6  /* AIF1TX_CHAN3_START_SLOT - [5:0] */

/*
 * R781 (0x30D) - AIF1TX Channel 4 Configuration
 */
#define WM8996_AIF1TX_CHAN4_DAT_INV             0x8000  /* AIF1TX_CHAN4_DAT_INV */
#define WM8996_AIF1TX_CHAN4_DAT_INV_MASK        0x8000  /* AIF1TX_CHAN4_DAT_INV */
#define WM8996_AIF1TX_CHAN4_DAT_INV_SHIFT           15  /* AIF1TX_CHAN4_DAT_INV */
#define WM8996_AIF1TX_CHAN4_DAT_INV_WIDTH            1  /* AIF1TX_CHAN4_DAT_INV */
#define WM8996_AIF1TX_CHAN4_SPACING_MASK        0x7E00  /* AIF1TX_CHAN4_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN4_SPACING_SHIFT            9  /* AIF1TX_CHAN4_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN4_SPACING_WIDTH            6  /* AIF1TX_CHAN4_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN4_SLOTS_MASK          0x01C0  /* AIF1TX_CHAN4_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN4_SLOTS_SHIFT              6  /* AIF1TX_CHAN4_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN4_SLOTS_WIDTH              3  /* AIF1TX_CHAN4_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN4_START_SLOT_MASK     0x003F  /* AIF1TX_CHAN4_START_SLOT - [5:0] */
#define WM8996_AIF1TX_CHAN4_START_SLOT_SHIFT         0  /* AIF1TX_CHAN4_START_SLOT - [5:0] */
#define WM8996_AIF1TX_CHAN4_START_SLOT_WIDTH         6  /* AIF1TX_CHAN4_START_SLOT - [5:0] */

/*
 * R782 (0x30E) - AIF1TX Channel 5 Configuration
 */
#define WM8996_AIF1TX_CHAN5_DAT_INV             0x8000  /* AIF1TX_CHAN5_DAT_INV */
#define WM8996_AIF1TX_CHAN5_DAT_INV_MASK        0x8000  /* AIF1TX_CHAN5_DAT_INV */
#define WM8996_AIF1TX_CHAN5_DAT_INV_SHIFT           15  /* AIF1TX_CHAN5_DAT_INV */
#define WM8996_AIF1TX_CHAN5_DAT_INV_WIDTH            1  /* AIF1TX_CHAN5_DAT_INV */
#define WM8996_AIF1TX_CHAN5_SPACING_MASK        0x7E00  /* AIF1TX_CHAN5_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN5_SPACING_SHIFT            9  /* AIF1TX_CHAN5_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN5_SPACING_WIDTH            6  /* AIF1TX_CHAN5_SPACING - [14:9] */
#define WM8996_AIF1TX_CHAN5_SLOTS_MASK          0x01C0  /* AIF1TX_CHAN5_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN5_SLOTS_SHIFT              6  /* AIF1TX_CHAN5_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN5_SLOTS_WIDTH              3  /* AIF1TX_CHAN5_SLOTS - [8:6] */
#define WM8996_AIF1TX_CHAN5_START_SLOT_MASK     0x003F  /* AIF1TX_CHAN5_START_SLOT - [5:0] */
#define WM8996_AIF1TX_CHAN5_START_SLOT_SHIFT         0  /* AIF1TX_CHAN5_START_SLOT - [5:0] */
#define WM8996_AIF1TX_CHAN5_START_SLOT_WIDTH         6  /* AIF1TX_CHAN5_START_SLOT - [5:0] */

/*
 * R783 (0x30F) - AIF1RX Channel 0 Configuration
 */
#define WM8996_AIF1RX_CHAN0_DAT_INV             0x8000  /* AIF1RX_CHAN0_DAT_INV */
#define WM8996_AIF1RX_CHAN0_DAT_INV_MASK        0x8000  /* AIF1RX_CHAN0_DAT_INV */
#define WM8996_AIF1RX_CHAN0_DAT_INV_SHIFT           15  /* AIF1RX_CHAN0_DAT_INV */
#define WM8996_AIF1RX_CHAN0_DAT_INV_WIDTH            1  /* AIF1RX_CHAN0_DAT_INV */
#define WM8996_AIF1RX_CHAN0_SPACING_MASK        0x7E00  /* AIF1RX_CHAN0_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN0_SPACING_SHIFT            9  /* AIF1RX_CHAN0_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN0_SPACING_WIDTH            6  /* AIF1RX_CHAN0_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN0_SLOTS_MASK          0x01C0  /* AIF1RX_CHAN0_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN0_SLOTS_SHIFT              6  /* AIF1RX_CHAN0_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN0_SLOTS_WIDTH              3  /* AIF1RX_CHAN0_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN0_START_SLOT_MASK     0x003F  /* AIF1RX_CHAN0_START_SLOT - [5:0] */
#define WM8996_AIF1RX_CHAN0_START_SLOT_SHIFT         0  /* AIF1RX_CHAN0_START_SLOT - [5:0] */
#define WM8996_AIF1RX_CHAN0_START_SLOT_WIDTH         6  /* AIF1RX_CHAN0_START_SLOT - [5:0] */

/*
 * R784 (0x310) - AIF1RX Channel 1 Configuration
 */
#define WM8996_AIF1RX_CHAN1_DAT_INV             0x8000  /* AIF1RX_CHAN1_DAT_INV */
#define WM8996_AIF1RX_CHAN1_DAT_INV_MASK        0x8000  /* AIF1RX_CHAN1_DAT_INV */
#define WM8996_AIF1RX_CHAN1_DAT_INV_SHIFT           15  /* AIF1RX_CHAN1_DAT_INV */
#define WM8996_AIF1RX_CHAN1_DAT_INV_WIDTH            1  /* AIF1RX_CHAN1_DAT_INV */
#define WM8996_AIF1RX_CHAN1_SPACING_MASK        0x7E00  /* AIF1RX_CHAN1_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN1_SPACING_SHIFT            9  /* AIF1RX_CHAN1_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN1_SPACING_WIDTH            6  /* AIF1RX_CHAN1_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN1_SLOTS_MASK          0x01C0  /* AIF1RX_CHAN1_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN1_SLOTS_SHIFT              6  /* AIF1RX_CHAN1_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN1_SLOTS_WIDTH              3  /* AIF1RX_CHAN1_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN1_START_SLOT_MASK     0x003F  /* AIF1RX_CHAN1_START_SLOT - [5:0] */
#define WM8996_AIF1RX_CHAN1_START_SLOT_SHIFT         0  /* AIF1RX_CHAN1_START_SLOT - [5:0] */
#define WM8996_AIF1RX_CHAN1_START_SLOT_WIDTH         6  /* AIF1RX_CHAN1_START_SLOT - [5:0] */

/*
 * R785 (0x311) - AIF1RX Channel 2 Configuration
 */
#define WM8996_AIF1RX_CHAN2_DAT_INV             0x8000  /* AIF1RX_CHAN2_DAT_INV */
#define WM8996_AIF1RX_CHAN2_DAT_INV_MASK        0x8000  /* AIF1RX_CHAN2_DAT_INV */
#define WM8996_AIF1RX_CHAN2_DAT_INV_SHIFT           15  /* AIF1RX_CHAN2_DAT_INV */
#define WM8996_AIF1RX_CHAN2_DAT_INV_WIDTH            1  /* AIF1RX_CHAN2_DAT_INV */
#define WM8996_AIF1RX_CHAN2_SPACING_MASK        0x7E00  /* AIF1RX_CHAN2_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN2_SPACING_SHIFT            9  /* AIF1RX_CHAN2_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN2_SPACING_WIDTH            6  /* AIF1RX_CHAN2_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN2_SLOTS_MASK          0x01C0  /* AIF1RX_CHAN2_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN2_SLOTS_SHIFT              6  /* AIF1RX_CHAN2_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN2_SLOTS_WIDTH              3  /* AIF1RX_CHAN2_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN2_START_SLOT_MASK     0x003F  /* AIF1RX_CHAN2_START_SLOT - [5:0] */
#define WM8996_AIF1RX_CHAN2_START_SLOT_SHIFT         0  /* AIF1RX_CHAN2_START_SLOT - [5:0] */
#define WM8996_AIF1RX_CHAN2_START_SLOT_WIDTH         6  /* AIF1RX_CHAN2_START_SLOT - [5:0] */

/*
 * R786 (0x312) - AIF1RX Channel 3 Configuration
 */
#define WM8996_AIF1RX_CHAN3_DAT_INV             0x8000  /* AIF1RX_CHAN3_DAT_INV */
#define WM8996_AIF1RX_CHAN3_DAT_INV_MASK        0x8000  /* AIF1RX_CHAN3_DAT_INV */
#define WM8996_AIF1RX_CHAN3_DAT_INV_SHIFT           15  /* AIF1RX_CHAN3_DAT_INV */
#define WM8996_AIF1RX_CHAN3_DAT_INV_WIDTH            1  /* AIF1RX_CHAN3_DAT_INV */
#define WM8996_AIF1RX_CHAN3_SPACING_MASK        0x7E00  /* AIF1RX_CHAN3_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN3_SPACING_SHIFT            9  /* AIF1RX_CHAN3_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN3_SPACING_WIDTH            6  /* AIF1RX_CHAN3_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN3_SLOTS_MASK          0x01C0  /* AIF1RX_CHAN3_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN3_SLOTS_SHIFT              6  /* AIF1RX_CHAN3_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN3_SLOTS_WIDTH              3  /* AIF1RX_CHAN3_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN3_START_SLOT_MASK     0x003F  /* AIF1RX_CHAN3_START_SLOT - [5:0] */
#define WM8996_AIF1RX_CHAN3_START_SLOT_SHIFT         0  /* AIF1RX_CHAN3_START_SLOT - [5:0] */
#define WM8996_AIF1RX_CHAN3_START_SLOT_WIDTH         6  /* AIF1RX_CHAN3_START_SLOT - [5:0] */

/*
 * R787 (0x313) - AIF1RX Channel 4 Configuration
 */
#define WM8996_AIF1RX_CHAN4_DAT_INV             0x8000  /* AIF1RX_CHAN4_DAT_INV */
#define WM8996_AIF1RX_CHAN4_DAT_INV_MASK        0x8000  /* AIF1RX_CHAN4_DAT_INV */
#define WM8996_AIF1RX_CHAN4_DAT_INV_SHIFT           15  /* AIF1RX_CHAN4_DAT_INV */
#define WM8996_AIF1RX_CHAN4_DAT_INV_WIDTH            1  /* AIF1RX_CHAN4_DAT_INV */
#define WM8996_AIF1RX_CHAN4_SPACING_MASK        0x7E00  /* AIF1RX_CHAN4_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN4_SPACING_SHIFT            9  /* AIF1RX_CHAN4_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN4_SPACING_WIDTH            6  /* AIF1RX_CHAN4_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN4_SLOTS_MASK          0x01C0  /* AIF1RX_CHAN4_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN4_SLOTS_SHIFT              6  /* AIF1RX_CHAN4_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN4_SLOTS_WIDTH              3  /* AIF1RX_CHAN4_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN4_START_SLOT_MASK     0x003F  /* AIF1RX_CHAN4_START_SLOT - [5:0] */
#define WM8996_AIF1RX_CHAN4_START_SLOT_SHIFT         0  /* AIF1RX_CHAN4_START_SLOT - [5:0] */
#define WM8996_AIF1RX_CHAN4_START_SLOT_WIDTH         6  /* AIF1RX_CHAN4_START_SLOT - [5:0] */

/*
 * R788 (0x314) - AIF1RX Channel 5 Configuration
 */
#define WM8996_AIF1RX_CHAN5_DAT_INV             0x8000  /* AIF1RX_CHAN5_DAT_INV */
#define WM8996_AIF1RX_CHAN5_DAT_INV_MASK        0x8000  /* AIF1RX_CHAN5_DAT_INV */
#define WM8996_AIF1RX_CHAN5_DAT_INV_SHIFT           15  /* AIF1RX_CHAN5_DAT_INV */
#define WM8996_AIF1RX_CHAN5_DAT_INV_WIDTH            1  /* AIF1RX_CHAN5_DAT_INV */
#define WM8996_AIF1RX_CHAN5_SPACING_MASK        0x7E00  /* AIF1RX_CHAN5_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN5_SPACING_SHIFT            9  /* AIF1RX_CHAN5_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN5_SPACING_WIDTH            6  /* AIF1RX_CHAN5_SPACING - [14:9] */
#define WM8996_AIF1RX_CHAN5_SLOTS_MASK          0x01C0  /* AIF1RX_CHAN5_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN5_SLOTS_SHIFT              6  /* AIF1RX_CHAN5_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN5_SLOTS_WIDTH              3  /* AIF1RX_CHAN5_SLOTS - [8:6] */
#define WM8996_AIF1RX_CHAN5_START_SLOT_MASK     0x003F  /* AIF1RX_CHAN5_START_SLOT - [5:0] */
#define WM8996_AIF1RX_CHAN5_START_SLOT_SHIFT         0  /* AIF1RX_CHAN5_START_SLOT - [5:0] */
#define WM8996_AIF1RX_CHAN5_START_SLOT_WIDTH         6  /* AIF1RX_CHAN5_START_SLOT - [5:0] */

/*
 * R789 (0x315) - AIF1RX Mono Configuration
 */
#define WM8996_AIF1RX_CHAN4_MONO_MODE           0x0004  /* AIF1RX_CHAN4_MONO_MODE */
#define WM8996_AIF1RX_CHAN4_MONO_MODE_MASK      0x0004  /* AIF1RX_CHAN4_MONO_MODE */
#define WM8996_AIF1RX_CHAN4_MONO_MODE_SHIFT          2  /* AIF1RX_CHAN4_MONO_MODE */
#define WM8996_AIF1RX_CHAN4_MONO_MODE_WIDTH          1  /* AIF1RX_CHAN4_MONO_MODE */
#define WM8996_AIF1RX_CHAN2_MONO_MODE           0x0002  /* AIF1RX_CHAN2_MONO_MODE */
#define WM8996_AIF1RX_CHAN2_MONO_MODE_MASK      0x0002  /* AIF1RX_CHAN2_MONO_MODE */
#define WM8996_AIF1RX_CHAN2_MONO_MODE_SHIFT          1  /* AIF1RX_CHAN2_MONO_MODE */
#define WM8996_AIF1RX_CHAN2_MONO_MODE_WIDTH          1  /* AIF1RX_CHAN2_MONO_MODE */
#define WM8996_AIF1RX_CHAN0_MONO_MODE           0x0001  /* AIF1RX_CHAN0_MONO_MODE */
#define WM8996_AIF1RX_CHAN0_MONO_MODE_MASK      0x0001  /* AIF1RX_CHAN0_MONO_MODE */
#define WM8996_AIF1RX_CHAN0_MONO_MODE_SHIFT          0  /* AIF1RX_CHAN0_MONO_MODE */
#define WM8996_AIF1RX_CHAN0_MONO_MODE_WIDTH          1  /* AIF1RX_CHAN0_MONO_MODE */

/*
 * R794 (0x31A) - AIF1TX Test
 */
#define WM8996_AIF1TX45_DITHER_ENA              0x0004  /* AIF1TX45_DITHER_ENA */
#define WM8996_AIF1TX45_DITHER_ENA_MASK         0x0004  /* AIF1TX45_DITHER_ENA */
#define WM8996_AIF1TX45_DITHER_ENA_SHIFT             2  /* AIF1TX45_DITHER_ENA */
#define WM8996_AIF1TX45_DITHER_ENA_WIDTH             1  /* AIF1TX45_DITHER_ENA */
#define WM8996_AIF1TX23_DITHER_ENA              0x0002  /* AIF1TX23_DITHER_ENA */
#define WM8996_AIF1TX23_DITHER_ENA_MASK         0x0002  /* AIF1TX23_DITHER_ENA */
#define WM8996_AIF1TX23_DITHER_ENA_SHIFT             1  /* AIF1TX23_DITHER_ENA */
#define WM8996_AIF1TX23_DITHER_ENA_WIDTH             1  /* AIF1TX23_DITHER_ENA */
#define WM8996_AIF1TX01_DITHER_ENA              0x0001  /* AIF1TX01_DITHER_ENA */
#define WM8996_AIF1TX01_DITHER_ENA_MASK         0x0001  /* AIF1TX01_DITHER_ENA */
#define WM8996_AIF1TX01_DITHER_ENA_SHIFT             0  /* AIF1TX01_DITHER_ENA */
#define WM8996_AIF1TX01_DITHER_ENA_WIDTH             1  /* AIF1TX01_DITHER_ENA */

/*
 * R800 (0x320) - AIF2 Control
 */
#define WM8996_AIF2_TRI                         0x0004  /* AIF2_TRI */
#define WM8996_AIF2_TRI_MASK                    0x0004  /* AIF2_TRI */
#define WM8996_AIF2_TRI_SHIFT                        2  /* AIF2_TRI */
#define WM8996_AIF2_TRI_WIDTH                        1  /* AIF2_TRI */
#define WM8996_AIF2_FMT_MASK                    0x0003  /* AIF2_FMT - [1:0] */
#define WM8996_AIF2_FMT_SHIFT                        0  /* AIF2_FMT - [1:0] */
#define WM8996_AIF2_FMT_WIDTH                        2  /* AIF2_FMT - [1:0] */

/*
 * R801 (0x321) - AIF2 BCLK
 */
#define WM8996_AIF2_BCLK_INV                    0x0400  /* AIF2_BCLK_INV */
#define WM8996_AIF2_BCLK_INV_MASK               0x0400  /* AIF2_BCLK_INV */
#define WM8996_AIF2_BCLK_INV_SHIFT                  10  /* AIF2_BCLK_INV */
#define WM8996_AIF2_BCLK_INV_WIDTH                   1  /* AIF2_BCLK_INV */
#define WM8996_AIF2_BCLK_FRC                    0x0200  /* AIF2_BCLK_FRC */
#define WM8996_AIF2_BCLK_FRC_MASK               0x0200  /* AIF2_BCLK_FRC */
#define WM8996_AIF2_BCLK_FRC_SHIFT                   9  /* AIF2_BCLK_FRC */
#define WM8996_AIF2_BCLK_FRC_WIDTH                   1  /* AIF2_BCLK_FRC */
#define WM8996_AIF2_BCLK_MSTR                   0x0100  /* AIF2_BCLK_MSTR */
#define WM8996_AIF2_BCLK_MSTR_MASK              0x0100  /* AIF2_BCLK_MSTR */
#define WM8996_AIF2_BCLK_MSTR_SHIFT                  8  /* AIF2_BCLK_MSTR */
#define WM8996_AIF2_BCLK_MSTR_WIDTH                  1  /* AIF2_BCLK_MSTR */
#define WM8996_AIF2_BCLK_DIV_MASK               0x000F  /* AIF2_BCLK_DIV - [3:0] */
#define WM8996_AIF2_BCLK_DIV_SHIFT                   0  /* AIF2_BCLK_DIV - [3:0] */
#define WM8996_AIF2_BCLK_DIV_WIDTH                   4  /* AIF2_BCLK_DIV - [3:0] */

/*
 * R802 (0x322) - AIF2 TX LRCLK(1)
 */
#define WM8996_AIF2TX_RATE_MASK                 0x07FF  /* AIF2TX_RATE - [10:0] */
#define WM8996_AIF2TX_RATE_SHIFT                     0  /* AIF2TX_RATE - [10:0] */
#define WM8996_AIF2TX_RATE_WIDTH                    11  /* AIF2TX_RATE - [10:0] */

/*
 * R803 (0x323) - AIF2 TX LRCLK(2)
 */
#define WM8996_AIF2TX_LRCLK_MODE                0x0008  /* AIF2TX_LRCLK_MODE */
#define WM8996_AIF2TX_LRCLK_MODE_MASK           0x0008  /* AIF2TX_LRCLK_MODE */
#define WM8996_AIF2TX_LRCLK_MODE_SHIFT               3  /* AIF2TX_LRCLK_MODE */
#define WM8996_AIF2TX_LRCLK_MODE_WIDTH               1  /* AIF2TX_LRCLK_MODE */
#define WM8996_AIF2TX_LRCLK_INV                 0x0004  /* AIF2TX_LRCLK_INV */
#define WM8996_AIF2TX_LRCLK_INV_MASK            0x0004  /* AIF2TX_LRCLK_INV */
#define WM8996_AIF2TX_LRCLK_INV_SHIFT                2  /* AIF2TX_LRCLK_INV */
#define WM8996_AIF2TX_LRCLK_INV_WIDTH                1  /* AIF2TX_LRCLK_INV */
#define WM8996_AIF2TX_LRCLK_FRC                 0x0002  /* AIF2TX_LRCLK_FRC */
#define WM8996_AIF2TX_LRCLK_FRC_MASK            0x0002  /* AIF2TX_LRCLK_FRC */
#define WM8996_AIF2TX_LRCLK_FRC_SHIFT                1  /* AIF2TX_LRCLK_FRC */
#define WM8996_AIF2TX_LRCLK_FRC_WIDTH                1  /* AIF2TX_LRCLK_FRC */
#define WM8996_AIF2TX_LRCLK_MSTR                0x0001  /* AIF2TX_LRCLK_MSTR */
#define WM8996_AIF2TX_LRCLK_MSTR_MASK           0x0001  /* AIF2TX_LRCLK_MSTR */
#define WM8996_AIF2TX_LRCLK_MSTR_SHIFT               0  /* AIF2TX_LRCLK_MSTR */
#define WM8996_AIF2TX_LRCLK_MSTR_WIDTH               1  /* AIF2TX_LRCLK_MSTR */

/*
 * R804 (0x324) - AIF2 RX LRCLK(1)
 */
#define WM8996_AIF2RX_RATE_MASK                 0x07FF  /* AIF2RX_RATE - [10:0] */
#define WM8996_AIF2RX_RATE_SHIFT                     0  /* AIF2RX_RATE - [10:0] */
#define WM8996_AIF2RX_RATE_WIDTH                    11  /* AIF2RX_RATE - [10:0] */

/*
 * R805 (0x325) - AIF2 RX LRCLK(2)
 */
#define WM8996_AIF2RX_LRCLK_INV                 0x0004  /* AIF2RX_LRCLK_INV */
#define WM8996_AIF2RX_LRCLK_INV_MASK            0x0004  /* AIF2RX_LRCLK_INV */
#define WM8996_AIF2RX_LRCLK_INV_SHIFT                2  /* AIF2RX_LRCLK_INV */
#define WM8996_AIF2RX_LRCLK_INV_WIDTH                1  /* AIF2RX_LRCLK_INV */
#define WM8996_AIF2RX_LRCLK_FRC                 0x0002  /* AIF2RX_LRCLK_FRC */
#define WM8996_AIF2RX_LRCLK_FRC_MASK            0x0002  /* AIF2RX_LRCLK_FRC */
#define WM8996_AIF2RX_LRCLK_FRC_SHIFT                1  /* AIF2RX_LRCLK_FRC */
#define WM8996_AIF2RX_LRCLK_FRC_WIDTH                1  /* AIF2RX_LRCLK_FRC */
#define WM8996_AIF2RX_LRCLK_MSTR                0x0001  /* AIF2RX_LRCLK_MSTR */
#define WM8996_AIF2RX_LRCLK_MSTR_MASK           0x0001  /* AIF2RX_LRCLK_MSTR */
#define WM8996_AIF2RX_LRCLK_MSTR_SHIFT               0  /* AIF2RX_LRCLK_MSTR */
#define WM8996_AIF2RX_LRCLK_MSTR_WIDTH               1  /* AIF2RX_LRCLK_MSTR */

/*
 * R806 (0x326) - AIF2TX Data Configuration (1)
 */
#define WM8996_AIF2TX_WL_MASK                   0xFF00  /* AIF2TX_WL - [15:8] */
#define WM8996_AIF2TX_WL_SHIFT                       8  /* AIF2TX_WL - [15:8] */
#define WM8996_AIF2TX_WL_WIDTH                       8  /* AIF2TX_WL - [15:8] */
#define WM8996_AIF2TX_SLOT_LEN_MASK             0x00FF  /* AIF2TX_SLOT_LEN - [7:0] */
#define WM8996_AIF2TX_SLOT_LEN_SHIFT                 0  /* AIF2TX_SLOT_LEN - [7:0] */
#define WM8996_AIF2TX_SLOT_LEN_WIDTH                 8  /* AIF2TX_SLOT_LEN - [7:0] */

/*
 * R807 (0x327) - AIF2TX Data Configuration (2)
 */
#define WM8996_AIF2TX_DAT_TRI                   0x0001  /* AIF2TX_DAT_TRI */
#define WM8996_AIF2TX_DAT_TRI_MASK              0x0001  /* AIF2TX_DAT_TRI */
#define WM8996_AIF2TX_DAT_TRI_SHIFT                  0  /* AIF2TX_DAT_TRI */
#define WM8996_AIF2TX_DAT_TRI_WIDTH                  1  /* AIF2TX_DAT_TRI */

/*
 * R808 (0x328) - AIF2RX Data Configuration
 */
#define WM8996_AIF2RX_WL_MASK                   0xFF00  /* AIF2RX_WL - [15:8] */
#define WM8996_AIF2RX_WL_SHIFT                       8  /* AIF2RX_WL - [15:8] */
#define WM8996_AIF2RX_WL_WIDTH                       8  /* AIF2RX_WL - [15:8] */
#define WM8996_AIF2RX_SLOT_LEN_MASK             0x00FF  /* AIF2RX_SLOT_LEN - [7:0] */
#define WM8996_AIF2RX_SLOT_LEN_SHIFT                 0  /* AIF2RX_SLOT_LEN - [7:0] */
#define WM8996_AIF2RX_SLOT_LEN_WIDTH                 8  /* AIF2RX_SLOT_LEN - [7:0] */

/*
 * R809 (0x329) - AIF2TX Channel 0 Configuration
 */
#define WM8996_AIF2TX_CHAN0_DAT_INV             0x8000  /* AIF2TX_CHAN0_DAT_INV */
#define WM8996_AIF2TX_CHAN0_DAT_INV_MASK        0x8000  /* AIF2TX_CHAN0_DAT_INV */
#define WM8996_AIF2TX_CHAN0_DAT_INV_SHIFT           15  /* AIF2TX_CHAN0_DAT_INV */
#define WM8996_AIF2TX_CHAN0_DAT_INV_WIDTH            1  /* AIF2TX_CHAN0_DAT_INV */
#define WM8996_AIF2TX_CHAN0_SPACING_MASK        0x7E00  /* AIF2TX_CHAN0_SPACING - [14:9] */
#define WM8996_AIF2TX_CHAN0_SPACING_SHIFT            9  /* AIF2TX_CHAN0_SPACING - [14:9] */
#define WM8996_AIF2TX_CHAN0_SPACING_WIDTH            6  /* AIF2TX_CHAN0_SPACING - [14:9] */
#define WM8996_AIF2TX_CHAN0_SLOTS_MASK          0x01C0  /* AIF2TX_CHAN0_SLOTS - [8:6] */
#define WM8996_AIF2TX_CHAN0_SLOTS_SHIFT              6  /* AIF2TX_CHAN0_SLOTS - [8:6] */
#define WM8996_AIF2TX_CHAN0_SLOTS_WIDTH              3  /* AIF2TX_CHAN0_SLOTS - [8:6] */
#define WM8996_AIF2TX_CHAN0_START_SLOT_MASK     0x003F  /* AIF2TX_CHAN0_START_SLOT - [5:0] */
#define WM8996_AIF2TX_CHAN0_START_SLOT_SHIFT         0  /* AIF2TX_CHAN0_START_SLOT - [5:0] */
#define WM8996_AIF2TX_CHAN0_START_SLOT_WIDTH         6  /* AIF2TX_CHAN0_START_SLOT - [5:0] */

/*
 * R810 (0x32A) - AIF2TX Channel 1 Configuration
 */
#define WM8996_AIF2TX_CHAN1_DAT_INV             0x8000  /* AIF2TX_CHAN1_DAT_INV */
#define WM8996_AIF2TX_CHAN1_DAT_INV_MASK        0x8000  /* AIF2TX_CHAN1_DAT_INV */
#define WM8996_AIF2TX_CHAN1_DAT_INV_SHIFT           15  /* AIF2TX_CHAN1_DAT_INV */
#define WM8996_AIF2TX_CHAN1_DAT_INV_WIDTH            1  /* AIF2TX_CHAN1_DAT_INV */
#define WM8996_AIF2TX_CHAN1_SPACING_MASK        0x7E00  /* AIF2TX_CHAN1_SPACING - [14:9] */
#define WM8996_AIF2TX_CHAN1_SPACING_SHIFT            9  /* AIF2TX_CHAN1_SPACING - [14:9] */
#define WM8996_AIF2TX_CHAN1_SPACING_WIDTH            6  /* AIF2TX_CHAN1_SPACING - [14:9] */
#define WM8996_AIF2TX_CHAN1_SLOTS_MASK          0x01C0  /* AIF2TX_CHAN1_SLOTS - [8:6] */
#define WM8996_AIF2TX_CHAN1_SLOTS_SHIFT              6  /* AIF2TX_CHAN1_SLOTS - [8:6] */
#define WM8996_AIF2TX_CHAN1_SLOTS_WIDTH              3  /* AIF2TX_CHAN1_SLOTS - [8:6] */
#define WM8996_AIF2TX_CHAN1_START_SLOT_MASK     0x003F  /* AIF2TX_CHAN1_START_SLOT - [5:0] */
#define WM8996_AIF2TX_CHAN1_START_SLOT_SHIFT         0  /* AIF2TX_CHAN1_START_SLOT - [5:0] */
#define WM8996_AIF2TX_CHAN1_START_SLOT_WIDTH         6  /* AIF2TX_CHAN1_START_SLOT - [5:0] */

/*
 * R811 (0x32B) - AIF2RX Channel 0 Configuration
 */
#define WM8996_AIF2RX_CHAN0_DAT_INV             0x8000  /* AIF2RX_CHAN0_DAT_INV */
#define WM8996_AIF2RX_CHAN0_DAT_INV_MASK        0x8000  /* AIF2RX_CHAN0_DAT_INV */
#define WM8996_AIF2RX_CHAN0_DAT_INV_SHIFT           15  /* AIF2RX_CHAN0_DAT_INV */
#define WM8996_AIF2RX_CHAN0_DAT_INV_WIDTH            1  /* AIF2RX_CHAN0_DAT_INV */
#define WM8996_AIF2RX_CHAN0_SPACING_MASK        0x7E00  /* AIF2RX_CHAN0_SPACING - [14:9] */
#define WM8996_AIF2RX_CHAN0_SPACING_SHIFT            9  /* AIF2RX_CHAN0_SPACING - [14:9] */
#define WM8996_AIF2RX_CHAN0_SPACING_WIDTH            6  /* AIF2RX_CHAN0_SPACING - [14:9] */
#define WM8996_AIF2RX_CHAN0_SLOTS_MASK          0x01C0  /* AIF2RX_CHAN0_SLOTS - [8:6] */
#define WM8996_AIF2RX_CHAN0_SLOTS_SHIFT              6  /* AIF2RX_CHAN0_SLOTS - [8:6] */
#define WM8996_AIF2RX_CHAN0_SLOTS_WIDTH              3  /* AIF2RX_CHAN0_SLOTS - [8:6] */
#define WM8996_AIF2RX_CHAN0_START_SLOT_MASK     0x003F  /* AIF2RX_CHAN0_START_SLOT - [5:0] */
#define WM8996_AIF2RX_CHAN0_START_SLOT_SHIFT         0  /* AIF2RX_CHAN0_START_SLOT - [5:0] */
#define WM8996_AIF2RX_CHAN0_START_SLOT_WIDTH         6  /* AIF2RX_CHAN0_START_SLOT - [5:0] */

/*
 * R812 (0x32C) - AIF2RX Channel 1 Configuration
 */
#define WM8996_AIF2RX_CHAN1_DAT_INV             0x8000  /* AIF2RX_CHAN1_DAT_INV */
#define WM8996_AIF2RX_CHAN1_DAT_INV_MASK        0x8000  /* AIF2RX_CHAN1_DAT_INV */
#define WM8996_AIF2RX_CHAN1_DAT_INV_SHIFT           15  /* AIF2RX_CHAN1_DAT_INV */
#define WM8996_AIF2RX_CHAN1_DAT_INV_WIDTH            1  /* AIF2RX_CHAN1_DAT_INV */
#define WM8996_AIF2RX_CHAN1_SPACING_MASK        0x7E00  /* AIF2RX_CHAN1_SPACING - [14:9] */
#define WM8996_AIF2RX_CHAN1_SPACING_SHIFT            9  /* AIF2RX_CHAN1_SPACING - [14:9] */
#define WM8996_AIF2RX_CHAN1_SPACING_WIDTH            6  /* AIF2RX_CHAN1_SPACING - [14:9] */
#define WM8996_AIF2RX_CHAN1_SLOTS_MASK          0x01C0  /* AIF2RX_CHAN1_SLOTS - [8:6] */
#define WM8996_AIF2RX_CHAN1_SLOTS_SHIFT              6  /* AIF2RX_CHAN1_SLOTS - [8:6] */
#define WM8996_AIF2RX_CHAN1_SLOTS_WIDTH              3  /* AIF2RX_CHAN1_SLOTS - [8:6] */
#define WM8996_AIF2RX_CHAN1_START_SLOT_MASK     0x003F  /* AIF2RX_CHAN1_START_SLOT - [5:0] */
#define WM8996_AIF2RX_CHAN1_START_SLOT_SHIFT         0  /* AIF2RX_CHAN1_START_SLOT - [5:0] */
#define WM8996_AIF2RX_CHAN1_START_SLOT_WIDTH         6  /* AIF2RX_CHAN1_START_SLOT - [5:0] */

/*
 * R813 (0x32D) - AIF2RX Mono Configuration
 */
#define WM8996_AIF2RX_CHAN0_MONO_MODE           0x0001  /* AIF2RX_CHAN0_MONO_MODE */
#define WM8996_AIF2RX_CHAN0_MONO_MODE_MASK      0x0001  /* AIF2RX_CHAN0_MONO_MODE */
#define WM8996_AIF2RX_CHAN0_MONO_MODE_SHIFT          0  /* AIF2RX_CHAN0_MONO_MODE */
#define WM8996_AIF2RX_CHAN0_MONO_MODE_WIDTH          1  /* AIF2RX_CHAN0_MONO_MODE */

/*
 * R815 (0x32F) - AIF2TX Test
 */
#define WM8996_AIF2TX_DITHER_ENA                0x0001  /* AIF2TX_DITHER_ENA */
#define WM8996_AIF2TX_DITHER_ENA_MASK           0x0001  /* AIF2TX_DITHER_ENA */
#define WM8996_AIF2TX_DITHER_ENA_SHIFT               0  /* AIF2TX_DITHER_ENA */
#define WM8996_AIF2TX_DITHER_ENA_WIDTH               1  /* AIF2TX_DITHER_ENA */

/*
 * R1024 (0x400) - DSP1 TX Left Volume
 */
#define WM8996_DSP1TX_VU                        0x0100  /* DSP1TX_VU */
#define WM8996_DSP1TX_VU_MASK                   0x0100  /* DSP1TX_VU */
#define WM8996_DSP1TX_VU_SHIFT                       8  /* DSP1TX_VU */
#define WM8996_DSP1TX_VU_WIDTH                       1  /* DSP1TX_VU */
#define WM8996_DSP1TXL_VOL_MASK                 0x00FF  /* DSP1TXL_VOL - [7:0] */
#define WM8996_DSP1TXL_VOL_SHIFT                     0  /* DSP1TXL_VOL - [7:0] */
#define WM8996_DSP1TXL_VOL_WIDTH                     8  /* DSP1TXL_VOL - [7:0] */

/*
 * R1025 (0x401) - DSP1 TX Right Volume
 */
#define WM8996_DSP1TX_VU                        0x0100  /* DSP1TX_VU */
#define WM8996_DSP1TX_VU_MASK                   0x0100  /* DSP1TX_VU */
#define WM8996_DSP1TX_VU_SHIFT                       8  /* DSP1TX_VU */
#define WM8996_DSP1TX_VU_WIDTH                       1  /* DSP1TX_VU */
#define WM8996_DSP1TXR_VOL_MASK                 0x00FF  /* DSP1TXR_VOL - [7:0] */
#define WM8996_DSP1TXR_VOL_SHIFT                     0  /* DSP1TXR_VOL - [7:0] */
#define WM8996_DSP1TXR_VOL_WIDTH                     8  /* DSP1TXR_VOL - [7:0] */

/*
 * R1026 (0x402) - DSP1 RX Left Volume
 */
#define WM8996_DSP1RX_VU                        0x0100  /* DSP1RX_VU */
#define WM8996_DSP1RX_VU_MASK                   0x0100  /* DSP1RX_VU */
#define WM8996_DSP1RX_VU_SHIFT                       8  /* DSP1RX_VU */
#define WM8996_DSP1RX_VU_WIDTH                       1  /* DSP1RX_VU */
#define WM8996_DSP1RXL_VOL_MASK                 0x00FF  /* DSP1RXL_VOL - [7:0] */
#define WM8996_DSP1RXL_VOL_SHIFT                     0  /* DSP1RXL_VOL - [7:0] */
#define WM8996_DSP1RXL_VOL_WIDTH                     8  /* DSP1RXL_VOL - [7:0] */

/*
 * R1027 (0x403) - DSP1 RX Right Volume
 */
#define WM8996_DSP1RX_VU                        0x0100  /* DSP1RX_VU */
#define WM8996_DSP1RX_VU_MASK                   0x0100  /* DSP1RX_VU */
#define WM8996_DSP1RX_VU_SHIFT                       8  /* DSP1RX_VU */
#define WM8996_DSP1RX_VU_WIDTH                       1  /* DSP1RX_VU */
#define WM8996_DSP1RXR_VOL_MASK                 0x00FF  /* DSP1RXR_VOL - [7:0] */
#define WM8996_DSP1RXR_VOL_SHIFT                     0  /* DSP1RXR_VOL - [7:0] */
#define WM8996_DSP1RXR_VOL_WIDTH                     8  /* DSP1RXR_VOL - [7:0] */

/*
 * R1040 (0x410) - DSP1 TX Filters
 */
#define WM8996_DSP1TX_NF                        0x2000  /* DSP1TX_NF */
#define WM8996_DSP1TX_NF_MASK                   0x2000  /* DSP1TX_NF */
#define WM8996_DSP1TX_NF_SHIFT                      13  /* DSP1TX_NF */
#define WM8996_DSP1TX_NF_WIDTH                       1  /* DSP1TX_NF */
#define WM8996_DSP1TXL_HPF                      0x1000  /* DSP1TXL_HPF */
#define WM8996_DSP1TXL_HPF_MASK                 0x1000  /* DSP1TXL_HPF */
#define WM8996_DSP1TXL_HPF_SHIFT                    12  /* DSP1TXL_HPF */
#define WM8996_DSP1TXL_HPF_WIDTH                     1  /* DSP1TXL_HPF */
#define WM8996_DSP1TXR_HPF                      0x0800  /* DSP1TXR_HPF */
#define WM8996_DSP1TXR_HPF_MASK                 0x0800  /* DSP1TXR_HPF */
#define WM8996_DSP1TXR_HPF_SHIFT                    11  /* DSP1TXR_HPF */
#define WM8996_DSP1TXR_HPF_WIDTH                     1  /* DSP1TXR_HPF */
#define WM8996_DSP1TX_HPF_MODE_MASK             0x0018  /* DSP1TX_HPF_MODE - [4:3] */
#define WM8996_DSP1TX_HPF_MODE_SHIFT                 3  /* DSP1TX_HPF_MODE - [4:3] */
#define WM8996_DSP1TX_HPF_MODE_WIDTH                 2  /* DSP1TX_HPF_MODE - [4:3] */
#define WM8996_DSP1TX_HPF_CUT_MASK              0x0007  /* DSP1TX_HPF_CUT - [2:0] */
#define WM8996_DSP1TX_HPF_CUT_SHIFT                  0  /* DSP1TX_HPF_CUT - [2:0] */
#define WM8996_DSP1TX_HPF_CUT_WIDTH                  3  /* DSP1TX_HPF_CUT - [2:0] */

/*
 * R1056 (0x420) - DSP1 RX Filters (1)
 */
#define WM8996_DSP1RX_MUTE                      0x0200  /* DSP1RX_MUTE */
#define WM8996_DSP1RX_MUTE_MASK                 0x0200  /* DSP1RX_MUTE */
#define WM8996_DSP1RX_MUTE_SHIFT                     9  /* DSP1RX_MUTE */
#define WM8996_DSP1RX_MUTE_WIDTH                     1  /* DSP1RX_MUTE */
#define WM8996_DSP1RX_MONO                      0x0080  /* DSP1RX_MONO */
#define WM8996_DSP1RX_MONO_MASK                 0x0080  /* DSP1RX_MONO */
#define WM8996_DSP1RX_MONO_SHIFT                     7  /* DSP1RX_MONO */
#define WM8996_DSP1RX_MONO_WIDTH                     1  /* DSP1RX_MONO */
#define WM8996_DSP1RX_MUTERATE                  0x0020  /* DSP1RX_MUTERATE */
#define WM8996_DSP1RX_MUTERATE_MASK             0x0020  /* DSP1RX_MUTERATE */
#define WM8996_DSP1RX_MUTERATE_SHIFT                 5  /* DSP1RX_MUTERATE */
#define WM8996_DSP1RX_MUTERATE_WIDTH                 1  /* DSP1RX_MUTERATE */
#define WM8996_DSP1RX_UNMUTE_RAMP               0x0010  /* DSP1RX_UNMUTE_RAMP */
#define WM8996_DSP1RX_UNMUTE_RAMP_MASK          0x0010  /* DSP1RX_UNMUTE_RAMP */
#define WM8996_DSP1RX_UNMUTE_RAMP_SHIFT              4  /* DSP1RX_UNMUTE_RAMP */
#define WM8996_DSP1RX_UNMUTE_RAMP_WIDTH              1  /* DSP1RX_UNMUTE_RAMP */

/*
 * R1057 (0x421) - DSP1 RX Filters (2)
 */
#define WM8996_DSP1RX_3D_GAIN_MASK              0x3E00  /* DSP1RX_3D_GAIN - [13:9] */
#define WM8996_DSP1RX_3D_GAIN_SHIFT                  9  /* DSP1RX_3D_GAIN - [13:9] */
#define WM8996_DSP1RX_3D_GAIN_WIDTH                  5  /* DSP1RX_3D_GAIN - [13:9] */
#define WM8996_DSP1RX_3D_ENA                    0x0100  /* DSP1RX_3D_ENA */
#define WM8996_DSP1RX_3D_ENA_MASK               0x0100  /* DSP1RX_3D_ENA */
#define WM8996_DSP1RX_3D_ENA_SHIFT                   8  /* DSP1RX_3D_ENA */
#define WM8996_DSP1RX_3D_ENA_WIDTH                   1  /* DSP1RX_3D_ENA */

/*
 * R1088 (0x440) - DSP1 DRC (1)
 */
#define WM8996_DSP1DRC_SIG_DET_RMS_MASK         0xF800  /* DSP1DRC_SIG_DET_RMS - [15:11] */
#define WM8996_DSP1DRC_SIG_DET_RMS_SHIFT            11  /* DSP1DRC_SIG_DET_RMS - [15:11] */
#define WM8996_DSP1DRC_SIG_DET_RMS_WIDTH             5  /* DSP1DRC_SIG_DET_RMS - [15:11] */
#define WM8996_DSP1DRC_SIG_DET_PK_MASK          0x0600  /* DSP1DRC_SIG_DET_PK - [10:9] */
#define WM8996_DSP1DRC_SIG_DET_PK_SHIFT              9  /* DSP1DRC_SIG_DET_PK - [10:9] */
#define WM8996_DSP1DRC_SIG_DET_PK_WIDTH              2  /* DSP1DRC_SIG_DET_PK - [10:9] */
#define WM8996_DSP1DRC_NG_ENA                   0x0100  /* DSP1DRC_NG_ENA */
#define WM8996_DSP1DRC_NG_ENA_MASK              0x0100  /* DSP1DRC_NG_ENA */
#define WM8996_DSP1DRC_NG_ENA_SHIFT                  8  /* DSP1DRC_NG_ENA */
#define WM8996_DSP1DRC_NG_ENA_WIDTH                  1  /* DSP1DRC_NG_ENA */
#define WM8996_DSP1DRC_SIG_DET_MODE             0x0080  /* DSP1DRC_SIG_DET_MODE */
#define WM8996_DSP1DRC_SIG_DET_MODE_MASK        0x0080  /* DSP1DRC_SIG_DET_MODE */
#define WM8996_DSP1DRC_SIG_DET_MODE_SHIFT            7  /* DSP1DRC_SIG_DET_MODE */
#define WM8996_DSP1DRC_SIG_DET_MODE_WIDTH            1  /* DSP1DRC_SIG_DET_MODE */
#define WM8996_DSP1DRC_SIG_DET                  0x0040  /* DSP1DRC_SIG_DET */
#define WM8996_DSP1DRC_SIG_DET_MASK             0x0040  /* DSP1DRC_SIG_DET */
#define WM8996_DSP1DRC_SIG_DET_SHIFT                 6  /* DSP1DRC_SIG_DET */
#define WM8996_DSP1DRC_SIG_DET_WIDTH                 1  /* DSP1DRC_SIG_DET */
#define WM8996_DSP1DRC_KNEE2_OP_ENA             0x0020  /* DSP1DRC_KNEE2_OP_ENA */
#define WM8996_DSP1DRC_KNEE2_OP_ENA_MASK        0x0020  /* DSP1DRC_KNEE2_OP_ENA */
#define WM8996_DSP1DRC_KNEE2_OP_ENA_SHIFT            5  /* DSP1DRC_KNEE2_OP_ENA */
#define WM8996_DSP1DRC_KNEE2_OP_ENA_WIDTH            1  /* DSP1DRC_KNEE2_OP_ENA */
#define WM8996_DSP1DRC_QR                       0x0010  /* DSP1DRC_QR */
#define WM8996_DSP1DRC_QR_MASK                  0x0010  /* DSP1DRC_QR */
#define WM8996_DSP1DRC_QR_SHIFT                      4  /* DSP1DRC_QR */
#define WM8996_DSP1DRC_QR_WIDTH                      1  /* DSP1DRC_QR */
#define WM8996_DSP1DRC_ANTICLIP                 0x0008  /* DSP1DRC_ANTICLIP */
#define WM8996_DSP1DRC_ANTICLIP_MASK            0x0008  /* DSP1DRC_ANTICLIP */
#define WM8996_DSP1DRC_ANTICLIP_SHIFT                3  /* DSP1DRC_ANTICLIP */
#define WM8996_DSP1DRC_ANTICLIP_WIDTH                1  /* DSP1DRC_ANTICLIP */
#define WM8996_DSP1RX_DRC_ENA                   0x0004  /* DSP1RX_DRC_ENA */
#define WM8996_DSP1RX_DRC_ENA_MASK              0x0004  /* DSP1RX_DRC_ENA */
#define WM8996_DSP1RX_DRC_ENA_SHIFT                  2  /* DSP1RX_DRC_ENA */
#define WM8996_DSP1RX_DRC_ENA_WIDTH                  1  /* DSP1RX_DRC_ENA */
#define WM8996_DSP1TXL_DRC_ENA                  0x0002  /* DSP1TXL_DRC_ENA */
#define WM8996_DSP1TXL_DRC_ENA_MASK             0x0002  /* DSP1TXL_DRC_ENA */
#define WM8996_DSP1TXL_DRC_ENA_SHIFT                 1  /* DSP1TXL_DRC_ENA */
#define WM8996_DSP1TXL_DRC_ENA_WIDTH                 1  /* DSP1TXL_DRC_ENA */
#define WM8996_DSP1TXR_DRC_ENA                  0x0001  /* DSP1TXR_DRC_ENA */
#define WM8996_DSP1TXR_DRC_ENA_MASK             0x0001  /* DSP1TXR_DRC_ENA */
#define WM8996_DSP1TXR_DRC_ENA_SHIFT                 0  /* DSP1TXR_DRC_ENA */
#define WM8996_DSP1TXR_DRC_ENA_WIDTH                 1  /* DSP1TXR_DRC_ENA */

/*
 * R1089 (0x441) - DSP1 DRC (2)
 */
#define WM8996_DSP1DRC_ATK_MASK                 0x1E00  /* DSP1DRC_ATK - [12:9] */
#define WM8996_DSP1DRC_ATK_SHIFT                     9  /* DSP1DRC_ATK - [12:9] */
#define WM8996_DSP1DRC_ATK_WIDTH                     4  /* DSP1DRC_ATK - [12:9] */
#define WM8996_DSP1DRC_DCY_MASK                 0x01E0  /* DSP1DRC_DCY - [8:5] */
#define WM8996_DSP1DRC_DCY_SHIFT                     5  /* DSP1DRC_DCY - [8:5] */
#define WM8996_DSP1DRC_DCY_WIDTH                     4  /* DSP1DRC_DCY - [8:5] */
#define WM8996_DSP1DRC_MINGAIN_MASK             0x001C  /* DSP1DRC_MINGAIN - [4:2] */
#define WM8996_DSP1DRC_MINGAIN_SHIFT                 2  /* DSP1DRC_MINGAIN - [4:2] */
#define WM8996_DSP1DRC_MINGAIN_WIDTH                 3  /* DSP1DRC_MINGAIN - [4:2] */
#define WM8996_DSP1DRC_MAXGAIN_MASK             0x0003  /* DSP1DRC_MAXGAIN - [1:0] */
#define WM8996_DSP1DRC_MAXGAIN_SHIFT                 0  /* DSP1DRC_MAXGAIN - [1:0] */
#define WM8996_DSP1DRC_MAXGAIN_WIDTH                 2  /* DSP1DRC_MAXGAIN - [1:0] */

/*
 * R1090 (0x442) - DSP1 DRC (3)
 */
#define WM8996_DSP1DRC_NG_MINGAIN_MASK          0xF000  /* DSP1DRC_NG_MINGAIN - [15:12] */
#define WM8996_DSP1DRC_NG_MINGAIN_SHIFT             12  /* DSP1DRC_NG_MINGAIN - [15:12] */
#define WM8996_DSP1DRC_NG_MINGAIN_WIDTH              4  /* DSP1DRC_NG_MINGAIN - [15:12] */
#define WM8996_DSP1DRC_NG_EXP_MASK              0x0C00  /* DSP1DRC_NG_EXP - [11:10] */
#define WM8996_DSP1DRC_NG_EXP_SHIFT                 10  /* DSP1DRC_NG_EXP - [11:10] */
#define WM8996_DSP1DRC_NG_EXP_WIDTH                  2  /* DSP1DRC_NG_EXP - [11:10] */
#define WM8996_DSP1DRC_QR_THR_MASK              0x0300  /* DSP1DRC_QR_THR - [9:8] */
#define WM8996_DSP1DRC_QR_THR_SHIFT                  8  /* DSP1DRC_QR_THR - [9:8] */
#define WM8996_DSP1DRC_QR_THR_WIDTH                  2  /* DSP1DRC_QR_THR - [9:8] */
#define WM8996_DSP1DRC_QR_DCY_MASK              0x00C0  /* DSP1DRC_QR_DCY - [7:6] */
#define WM8996_DSP1DRC_QR_DCY_SHIFT                  6  /* DSP1DRC_QR_DCY - [7:6] */
#define WM8996_DSP1DRC_QR_DCY_WIDTH                  2  /* DSP1DRC_QR_DCY - [7:6] */
#define WM8996_DSP1DRC_HI_COMP_MASK             0x0038  /* DSP1DRC_HI_COMP - [5:3] */
#define WM8996_DSP1DRC_HI_COMP_SHIFT                 3  /* DSP1DRC_HI_COMP - [5:3] */
#define WM8996_DSP1DRC_HI_COMP_WIDTH                 3  /* DSP1DRC_HI_COMP - [5:3] */
#define WM8996_DSP1DRC_LO_COMP_MASK             0x0007  /* DSP1DRC_LO_COMP - [2:0] */
#define WM8996_DSP1DRC_LO_COMP_SHIFT                 0  /* DSP1DRC_LO_COMP - [2:0] */
#define WM8996_DSP1DRC_LO_COMP_WIDTH                 3  /* DSP1DRC_LO_COMP - [2:0] */

/*
 * R1091 (0x443) - DSP1 DRC (4)
 */
#define WM8996_DSP1DRC_KNEE_IP_MASK             0x07E0  /* DSP1DRC_KNEE_IP - [10:5] */
#define WM8996_DSP1DRC_KNEE_IP_SHIFT                 5  /* DSP1DRC_KNEE_IP - [10:5] */
#define WM8996_DSP1DRC_KNEE_IP_WIDTH                 6  /* DSP1DRC_KNEE_IP - [10:5] */
#define WM8996_DSP1DRC_KNEE_OP_MASK             0x001F  /* DSP1DRC_KNEE_OP - [4:0] */
#define WM8996_DSP1DRC_KNEE_OP_SHIFT                 0  /* DSP1DRC_KNEE_OP - [4:0] */
#define WM8996_DSP1DRC_KNEE_OP_WIDTH                 5  /* DSP1DRC_KNEE_OP - [4:0] */

/*
 * R1092 (0x444) - DSP1 DRC (5)
 */
#define WM8996_DSP1DRC_KNEE2_IP_MASK            0x03E0  /* DSP1DRC_KNEE2_IP - [9:5] */
#define WM8996_DSP1DRC_KNEE2_IP_SHIFT                5  /* DSP1DRC_KNEE2_IP - [9:5] */
#define WM8996_DSP1DRC_KNEE2_IP_WIDTH                5  /* DSP1DRC_KNEE2_IP - [9:5] */
#define WM8996_DSP1DRC_KNEE2_OP_MASK            0x001F  /* DSP1DRC_KNEE2_OP - [4:0] */
#define WM8996_DSP1DRC_KNEE2_OP_SHIFT                0  /* DSP1DRC_KNEE2_OP - [4:0] */
#define WM8996_DSP1DRC_KNEE2_OP_WIDTH                5  /* DSP1DRC_KNEE2_OP - [4:0] */

/*
 * R1152 (0x480) - DSP1 RX EQ Gains (1)
 */
#define WM8996_DSP1RX_EQ_B1_GAIN_MASK           0xF800  /* DSP1RX_EQ_B1_GAIN - [15:11] */
#define WM8996_DSP1RX_EQ_B1_GAIN_SHIFT              11  /* DSP1RX_EQ_B1_GAIN - [15:11] */
#define WM8996_DSP1RX_EQ_B1_GAIN_WIDTH               5  /* DSP1RX_EQ_B1_GAIN - [15:11] */
#define WM8996_DSP1RX_EQ_B2_GAIN_MASK           0x07C0  /* DSP1RX_EQ_B2_GAIN - [10:6] */
#define WM8996_DSP1RX_EQ_B2_GAIN_SHIFT               6  /* DSP1RX_EQ_B2_GAIN - [10:6] */
#define WM8996_DSP1RX_EQ_B2_GAIN_WIDTH               5  /* DSP1RX_EQ_B2_GAIN - [10:6] */
#define WM8996_DSP1RX_EQ_B3_GAIN_MASK           0x003E  /* DSP1RX_EQ_B3_GAIN - [5:1] */
#define WM8996_DSP1RX_EQ_B3_GAIN_SHIFT               1  /* DSP1RX_EQ_B3_GAIN - [5:1] */
#define WM8996_DSP1RX_EQ_B3_GAIN_WIDTH               5  /* DSP1RX_EQ_B3_GAIN - [5:1] */
#define WM8996_DSP1RX_EQ_ENA                    0x0001  /* DSP1RX_EQ_ENA */
#define WM8996_DSP1RX_EQ_ENA_MASK               0x0001  /* DSP1RX_EQ_ENA */
#define WM8996_DSP1RX_EQ_ENA_SHIFT                   0  /* DSP1RX_EQ_ENA */
#define WM8996_DSP1RX_EQ_ENA_WIDTH                   1  /* DSP1RX_EQ_ENA */

/*
 * R1153 (0x481) - DSP1 RX EQ Gains (2)
 */
#define WM8996_DSP1RX_EQ_B4_GAIN_MASK           0xF800  /* DSP1RX_EQ_B4_GAIN - [15:11] */
#define WM8996_DSP1RX_EQ_B4_GAIN_SHIFT              11  /* DSP1RX_EQ_B4_GAIN - [15:11] */
#define WM8996_DSP1RX_EQ_B4_GAIN_WIDTH               5  /* DSP1RX_EQ_B4_GAIN - [15:11] */
#define WM8996_DSP1RX_EQ_B5_GAIN_MASK           0x07C0  /* DSP1RX_EQ_B5_GAIN - [10:6] */
#define WM8996_DSP1RX_EQ_B5_GAIN_SHIFT               6  /* DSP1RX_EQ_B5_GAIN - [10:6] */
#define WM8996_DSP1RX_EQ_B5_GAIN_WIDTH               5  /* DSP1RX_EQ_B5_GAIN - [10:6] */

/*
 * R1154 (0x482) - DSP1 RX EQ Band 1 A
 */
#define WM8996_DSP1RX_EQ_B1_A_MASK              0xFFFF  /* DSP1RX_EQ_B1_A - [15:0] */
#define WM8996_DSP1RX_EQ_B1_A_SHIFT                  0  /* DSP1RX_EQ_B1_A - [15:0] */
#define WM8996_DSP1RX_EQ_B1_A_WIDTH                 16  /* DSP1RX_EQ_B1_A - [15:0] */

/*
 * R1155 (0x483) - DSP1 RX EQ Band 1 B
 */
#define WM8996_DSP1RX_EQ_B1_B_MASK              0xFFFF  /* DSP1RX_EQ_B1_B - [15:0] */
#define WM8996_DSP1RX_EQ_B1_B_SHIFT                  0  /* DSP1RX_EQ_B1_B - [15:0] */
#define WM8996_DSP1RX_EQ_B1_B_WIDTH                 16  /* DSP1RX_EQ_B1_B - [15:0] */

/*
 * R1156 (0x484) - DSP1 RX EQ Band 1 PG
 */
#define WM8996_DSP1RX_EQ_B1_PG_MASK             0xFFFF  /* DSP1RX_EQ_B1_PG - [15:0] */
#define WM8996_DSP1RX_EQ_B1_PG_SHIFT                 0  /* DSP1RX_EQ_B1_PG - [15:0] */
#define WM8996_DSP1RX_EQ_B1_PG_WIDTH                16  /* DSP1RX_EQ_B1_PG - [15:0] */

/*
 * R1157 (0x485) - DSP1 RX EQ Band 2 A
 */
#define WM8996_DSP1RX_EQ_B2_A_MASK              0xFFFF  /* DSP1RX_EQ_B2_A - [15:0] */
#define WM8996_DSP1RX_EQ_B2_A_SHIFT                  0  /* DSP1RX_EQ_B2_A - [15:0] */
#define WM8996_DSP1RX_EQ_B2_A_WIDTH                 16  /* DSP1RX_EQ_B2_A - [15:0] */

/*
 * R1158 (0x486) - DSP1 RX EQ Band 2 B
 */
#define WM8996_DSP1RX_EQ_B2_B_MASK              0xFFFF  /* DSP1RX_EQ_B2_B - [15:0] */
#define WM8996_DSP1RX_EQ_B2_B_SHIFT                  0  /* DSP1RX_EQ_B2_B - [15:0] */
#define WM8996_DSP1RX_EQ_B2_B_WIDTH                 16  /* DSP1RX_EQ_B2_B - [15:0] */

/*
 * R1159 (0x487) - DSP1 RX EQ Band 2 C
 */
#define WM8996_DSP1RX_EQ_B2_C_MASK              0xFFFF  /* DSP1RX_EQ_B2_C - [15:0] */
#define WM8996_DSP1RX_EQ_B2_C_SHIFT                  0  /* DSP1RX_EQ_B2_C - [15:0] */
#define WM8996_DSP1RX_EQ_B2_C_WIDTH                 16  /* DSP1RX_EQ_B2_C - [15:0] */

/*
 * R1160 (0x488) - DSP1 RX EQ Band 2 PG
 */
#define WM8996_DSP1RX_EQ_B2_PG_MASK             0xFFFF  /* DSP1RX_EQ_B2_PG - [15:0] */
#define WM8996_DSP1RX_EQ_B2_PG_SHIFT                 0  /* DSP1RX_EQ_B2_PG - [15:0] */
#define WM8996_DSP1RX_EQ_B2_PG_WIDTH                16  /* DSP1RX_EQ_B2_PG - [15:0] */

/*
 * R1161 (0x489) - DSP1 RX EQ Band 3 A
 */
#define WM8996_DSP1RX_EQ_B3_A_MASK              0xFFFF  /* DSP1RX_EQ_B3_A - [15:0] */
#define WM8996_DSP1RX_EQ_B3_A_SHIFT                  0  /* DSP1RX_EQ_B3_A - [15:0] */
#define WM8996_DSP1RX_EQ_B3_A_WIDTH                 16  /* DSP1RX_EQ_B3_A - [15:0] */

/*
 * R1162 (0x48A) - DSP1 RX EQ Band 3 B
 */
#define WM8996_DSP1RX_EQ_B3_B_MASK              0xFFFF  /* DSP1RX_EQ_B3_B - [15:0] */
#define WM8996_DSP1RX_EQ_B3_B_SHIFT                  0  /* DSP1RX_EQ_B3_B - [15:0] */
#define WM8996_DSP1RX_EQ_B3_B_WIDTH                 16  /* DSP1RX_EQ_B3_B - [15:0] */

/*
 * R1163 (0x48B) - DSP1 RX EQ Band 3 C
 */
#define WM8996_DSP1RX_EQ_B3_C_MASK              0xFFFF  /* DSP1RX_EQ_B3_C - [15:0] */
#define WM8996_DSP1RX_EQ_B3_C_SHIFT                  0  /* DSP1RX_EQ_B3_C - [15:0] */
#define WM8996_DSP1RX_EQ_B3_C_WIDTH                 16  /* DSP1RX_EQ_B3_C - [15:0] */

/*
 * R1164 (0x48C) - DSP1 RX EQ Band 3 PG
 */
#define WM8996_DSP1RX_EQ_B3_PG_MASK             0xFFFF  /* DSP1RX_EQ_B3_PG - [15:0] */
#define WM8996_DSP1RX_EQ_B3_PG_SHIFT                 0  /* DSP1RX_EQ_B3_PG - [15:0] */
#define WM8996_DSP1RX_EQ_B3_PG_WIDTH                16  /* DSP1RX_EQ_B3_PG - [15:0] */

/*
 * R1165 (0x48D) - DSP1 RX EQ Band 4 A
 */
#define WM8996_DSP1RX_EQ_B4_A_MASK              0xFFFF  /* DSP1RX_EQ_B4_A - [15:0] */
#define WM8996_DSP1RX_EQ_B4_A_SHIFT                  0  /* DSP1RX_EQ_B4_A - [15:0] */
#define WM8996_DSP1RX_EQ_B4_A_WIDTH                 16  /* DSP1RX_EQ_B4_A - [15:0] */

/*
 * R1166 (0x48E) - DSP1 RX EQ Band 4 B
 */
#define WM8996_DSP1RX_EQ_B4_B_MASK              0xFFFF  /* DSP1RX_EQ_B4_B - [15:0] */
#define WM8996_DSP1RX_EQ_B4_B_SHIFT                  0  /* DSP1RX_EQ_B4_B - [15:0] */
#define WM8996_DSP1RX_EQ_B4_B_WIDTH                 16  /* DSP1RX_EQ_B4_B - [15:0] */

/*
 * R1167 (0x48F) - DSP1 RX EQ Band 4 C
 */
#define WM8996_DSP1RX_EQ_B4_C_MASK              0xFFFF  /* DSP1RX_EQ_B4_C - [15:0] */
#define WM8996_DSP1RX_EQ_B4_C_SHIFT                  0  /* DSP1RX_EQ_B4_C - [15:0] */
#define WM8996_DSP1RX_EQ_B4_C_WIDTH                 16  /* DSP1RX_EQ_B4_C - [15:0] */

/*
 * R1168 (0x490) - DSP1 RX EQ Band 4 PG
 */
#define WM8996_DSP1RX_EQ_B4_PG_MASK             0xFFFF  /* DSP1RX_EQ_B4_PG - [15:0] */
#define WM8996_DSP1RX_EQ_B4_PG_SHIFT                 0  /* DSP1RX_EQ_B4_PG - [15:0] */
#define WM8996_DSP1RX_EQ_B4_PG_WIDTH                16  /* DSP1RX_EQ_B4_PG - [15:0] */

/*
 * R1169 (0x491) - DSP1 RX EQ Band 5 A
 */
#define WM8996_DSP1RX_EQ_B5_A_MASK              0xFFFF  /* DSP1RX_EQ_B5_A - [15:0] */
#define WM8996_DSP1RX_EQ_B5_A_SHIFT                  0  /* DSP1RX_EQ_B5_A - [15:0] */
#define WM8996_DSP1RX_EQ_B5_A_WIDTH                 16  /* DSP1RX_EQ_B5_A - [15:0] */

/*
 * R1170 (0x492) - DSP1 RX EQ Band 5 B
 */
#define WM8996_DSP1RX_EQ_B5_B_MASK              0xFFFF  /* DSP1RX_EQ_B5_B - [15:0] */
#define WM8996_DSP1RX_EQ_B5_B_SHIFT                  0  /* DSP1RX_EQ_B5_B - [15:0] */
#define WM8996_DSP1RX_EQ_B5_B_WIDTH                 16  /* DSP1RX_EQ_B5_B - [15:0] */

/*
 * R1171 (0x493) - DSP1 RX EQ Band 5 PG
 */
#define WM8996_DSP1RX_EQ_B5_PG_MASK             0xFFFF  /* DSP1RX_EQ_B5_PG - [15:0] */
#define WM8996_DSP1RX_EQ_B5_PG_SHIFT                 0  /* DSP1RX_EQ_B5_PG - [15:0] */
#define WM8996_DSP1RX_EQ_B5_PG_WIDTH                16  /* DSP1RX_EQ_B5_PG - [15:0] */

/*
 * R1280 (0x500) - DSP2 TX Left Volume
 */
#define WM8996_DSP2TX_VU                        0x0100  /* DSP2TX_VU */
#define WM8996_DSP2TX_VU_MASK                   0x0100  /* DSP2TX_VU */
#define WM8996_DSP2TX_VU_SHIFT                       8  /* DSP2TX_VU */
#define WM8996_DSP2TX_VU_WIDTH                       1  /* DSP2TX_VU */
#define WM8996_DSP2TXL_VOL_MASK                 0x00FF  /* DSP2TXL_VOL - [7:0] */
#define WM8996_DSP2TXL_VOL_SHIFT                     0  /* DSP2TXL_VOL - [7:0] */
#define WM8996_DSP2TXL_VOL_WIDTH                     8  /* DSP2TXL_VOL - [7:0] */

/*
 * R1281 (0x501) - DSP2 TX Right Volume
 */
#define WM8996_DSP2TX_VU                        0x0100  /* DSP2TX_VU */
#define WM8996_DSP2TX_VU_MASK                   0x0100  /* DSP2TX_VU */
#define WM8996_DSP2TX_VU_SHIFT                       8  /* DSP2TX_VU */
#define WM8996_DSP2TX_VU_WIDTH                       1  /* DSP2TX_VU */
#define WM8996_DSP2TXR_VOL_MASK                 0x00FF  /* DSP2TXR_VOL - [7:0] */
#define WM8996_DSP2TXR_VOL_SHIFT                     0  /* DSP2TXR_VOL - [7:0] */
#define WM8996_DSP2TXR_VOL_WIDTH                     8  /* DSP2TXR_VOL - [7:0] */

/*
 * R1282 (0x502) - DSP2 RX Left Volume
 */
#define WM8996_DSP2RX_VU                        0x0100  /* DSP2RX_VU */
#define WM8996_DSP2RX_VU_MASK                   0x0100  /* DSP2RX_VU */
#define WM8996_DSP2RX_VU_SHIFT                       8  /* DSP2RX_VU */
#define WM8996_DSP2RX_VU_WIDTH                       1  /* DSP2RX_VU */
#define WM8996_DSP2RXL_VOL_MASK                 0x00FF  /* DSP2RXL_VOL - [7:0] */
#define WM8996_DSP2RXL_VOL_SHIFT                     0  /* DSP2RXL_VOL - [7:0] */
#define WM8996_DSP2RXL_VOL_WIDTH                     8  /* DSP2RXL_VOL - [7:0] */

/*
 * R1283 (0x503) - DSP2 RX Right Volume
 */
#define WM8996_DSP2RX_VU                        0x0100  /* DSP2RX_VU */
#define WM8996_DSP2RX_VU_MASK                   0x0100  /* DSP2RX_VU */
#define WM8996_DSP2RX_VU_SHIFT                       8  /* DSP2RX_VU */
#define WM8996_DSP2RX_VU_WIDTH                       1  /* DSP2RX_VU */
#define WM8996_DSP2RXR_VOL_MASK                 0x00FF  /* DSP2RXR_VOL - [7:0] */
#define WM8996_DSP2RXR_VOL_SHIFT                     0  /* DSP2RXR_VOL - [7:0] */
#define WM8996_DSP2RXR_VOL_WIDTH                     8  /* DSP2RXR_VOL - [7:0] */

/*
 * R1296 (0x510) - DSP2 TX Filters
 */
#define WM8996_DSP2TX_NF                        0x2000  /* DSP2TX_NF */
#define WM8996_DSP2TX_NF_MASK                   0x2000  /* DSP2TX_NF */
#define WM8996_DSP2TX_NF_SHIFT                      13  /* DSP2TX_NF */
#define WM8996_DSP2TX_NF_WIDTH                       1  /* DSP2TX_NF */
#define WM8996_DSP2TXL_HPF                      0x1000  /* DSP2TXL_HPF */
#define WM8996_DSP2TXL_HPF_MASK                 0x1000  /* DSP2TXL_HPF */
#define WM8996_DSP2TXL_HPF_SHIFT                    12  /* DSP2TXL_HPF */
#define WM8996_DSP2TXL_HPF_WIDTH                     1  /* DSP2TXL_HPF */
#define WM8996_DSP2TXR_HPF                      0x0800  /* DSP2TXR_HPF */
#define WM8996_DSP2TXR_HPF_MASK                 0x0800  /* DSP2TXR_HPF */
#define WM8996_DSP2TXR_HPF_SHIFT                    11  /* DSP2TXR_HPF */
#define WM8996_DSP2TXR_HPF_WIDTH                     1  /* DSP2TXR_HPF */
#define WM8996_DSP2TX_HPF_MODE_MASK             0x0018  /* DSP2TX_HPF_MODE - [4:3] */
#define WM8996_DSP2TX_HPF_MODE_SHIFT                 3  /* DSP2TX_HPF_MODE - [4:3] */
#define WM8996_DSP2TX_HPF_MODE_WIDTH                 2  /* DSP2TX_HPF_MODE - [4:3] */
#define WM8996_DSP2TX_HPF_CUT_MASK              0x0007  /* DSP2TX_HPF_CUT - [2:0] */
#define WM8996_DSP2TX_HPF_CUT_SHIFT                  0  /* DSP2TX_HPF_CUT - [2:0] */
#define WM8996_DSP2TX_HPF_CUT_WIDTH                  3  /* DSP2TX_HPF_CUT - [2:0] */

/*
 * R1312 (0x520) - DSP2 RX Filters (1)
 */
#define WM8996_DSP2RX_MUTE                      0x0200  /* DSP2RX_MUTE */
#define WM8996_DSP2RX_MUTE_MASK                 0x0200  /* DSP2RX_MUTE */
#define WM8996_DSP2RX_MUTE_SHIFT                     9  /* DSP2RX_MUTE */
#define WM8996_DSP2RX_MUTE_WIDTH                     1  /* DSP2RX_MUTE */
#define WM8996_DSP2RX_MONO                      0x0080  /* DSP2RX_MONO */
#define WM8996_DSP2RX_MONO_MASK                 0x0080  /* DSP2RX_MONO */
#define WM8996_DSP2RX_MONO_SHIFT                     7  /* DSP2RX_MONO */
#define WM8996_DSP2RX_MONO_WIDTH                     1  /* DSP2RX_MONO */
#define WM8996_DSP2RX_MUTERATE                  0x0020  /* DSP2RX_MUTERATE */
#define WM8996_DSP2RX_MUTERATE_MASK             0x0020  /* DSP2RX_MUTERATE */
#define WM8996_DSP2RX_MUTERATE_SHIFT                 5  /* DSP2RX_MUTERATE */
#define WM8996_DSP2RX_MUTERATE_WIDTH                 1  /* DSP2RX_MUTERATE */
#define WM8996_DSP2RX_UNMUTE_RAMP               0x0010  /* DSP2RX_UNMUTE_RAMP */
#define WM8996_DSP2RX_UNMUTE_RAMP_MASK          0x0010  /* DSP2RX_UNMUTE_RAMP */
#define WM8996_DSP2RX_UNMUTE_RAMP_SHIFT              4  /* DSP2RX_UNMUTE_RAMP */
#define WM8996_DSP2RX_UNMUTE_RAMP_WIDTH              1  /* DSP2RX_UNMUTE_RAMP */

/*
 * R1313 (0x521) - DSP2 RX Filters (2)
 */
#define WM8996_DSP2RX_3D_GAIN_MASK              0x3E00  /* DSP2RX_3D_GAIN - [13:9] */
#define WM8996_DSP2RX_3D_GAIN_SHIFT                  9  /* DSP2RX_3D_GAIN - [13:9] */
#define WM8996_DSP2RX_3D_GAIN_WIDTH                  5  /* DSP2RX_3D_GAIN - [13:9] */
#define WM8996_DSP2RX_3D_ENA                    0x0100  /* DSP2RX_3D_ENA */
#define WM8996_DSP2RX_3D_ENA_MASK               0x0100  /* DSP2RX_3D_ENA */
#define WM8996_DSP2RX_3D_ENA_SHIFT                   8  /* DSP2RX_3D_ENA */
#define WM8996_DSP2RX_3D_ENA_WIDTH                   1  /* DSP2RX_3D_ENA */

/*
 * R1344 (0x540) - DSP2 DRC (1)
 */
#define WM8996_DSP2DRC_SIG_DET_RMS_MASK         0xF800  /* DSP2DRC_SIG_DET_RMS - [15:11] */
#define WM8996_DSP2DRC_SIG_DET_RMS_SHIFT            11  /* DSP2DRC_SIG_DET_RMS - [15:11] */
#define WM8996_DSP2DRC_SIG_DET_RMS_WIDTH             5  /* DSP2DRC_SIG_DET_RMS - [15:11] */
#define WM8996_DSP2DRC_SIG_DET_PK_MASK          0x0600  /* DSP2DRC_SIG_DET_PK - [10:9] */
#define WM8996_DSP2DRC_SIG_DET_PK_SHIFT              9  /* DSP2DRC_SIG_DET_PK - [10:9] */
#define WM8996_DSP2DRC_SIG_DET_PK_WIDTH              2  /* DSP2DRC_SIG_DET_PK - [10:9] */
#define WM8996_DSP2DRC_NG_ENA                   0x0100  /* DSP2DRC_NG_ENA */
#define WM8996_DSP2DRC_NG_ENA_MASK              0x0100  /* DSP2DRC_NG_ENA */
#define WM8996_DSP2DRC_NG_ENA_SHIFT                  8  /* DSP2DRC_NG_ENA */
#define WM8996_DSP2DRC_NG_ENA_WIDTH                  1  /* DSP2DRC_NG_ENA */
#define WM8996_DSP2DRC_SIG_DET_MODE             0x0080  /* DSP2DRC_SIG_DET_MODE */
#define WM8996_DSP2DRC_SIG_DET_MODE_MASK        0x0080  /* DSP2DRC_SIG_DET_MODE */
#define WM8996_DSP2DRC_SIG_DET_MODE_SHIFT            7  /* DSP2DRC_SIG_DET_MODE */
#define WM8996_DSP2DRC_SIG_DET_MODE_WIDTH            1  /* DSP2DRC_SIG_DET_MODE */
#define WM8996_DSP2DRC_SIG_DET                  0x0040  /* DSP2DRC_SIG_DET */
#define WM8996_DSP2DRC_SIG_DET_MASK             0x0040  /* DSP2DRC_SIG_DET */
#define WM8996_DSP2DRC_SIG_DET_SHIFT                 6  /* DSP2DRC_SIG_DET */
#define WM8996_DSP2DRC_SIG_DET_WIDTH                 1  /* DSP2DRC_SIG_DET */
#define WM8996_DSP2DRC_KNEE2_OP_ENA             0x0020  /* DSP2DRC_KNEE2_OP_ENA */
#define WM8996_DSP2DRC_KNEE2_OP_ENA_MASK        0x0020  /* DSP2DRC_KNEE2_OP_ENA */
#define WM8996_DSP2DRC_KNEE2_OP_ENA_SHIFT            5  /* DSP2DRC_KNEE2_OP_ENA */
#define WM8996_DSP2DRC_KNEE2_OP_ENA_WIDTH            1  /* DSP2DRC_KNEE2_OP_ENA */
#define WM8996_DSP2DRC_QR                       0x0010  /* DSP2DRC_QR */
#define WM8996_DSP2DRC_QR_MASK                  0x0010  /* DSP2DRC_QR */
#define WM8996_DSP2DRC_QR_SHIFT                      4  /* DSP2DRC_QR */
#define WM8996_DSP2DRC_QR_WIDTH                      1  /* DSP2DRC_QR */
#define WM8996_DSP2DRC_ANTICLIP                 0x0008  /* DSP2DRC_ANTICLIP */
#define WM8996_DSP2DRC_ANTICLIP_MASK            0x0008  /* DSP2DRC_ANTICLIP */
#define WM8996_DSP2DRC_ANTICLIP_SHIFT                3  /* DSP2DRC_ANTICLIP */
#define WM8996_DSP2DRC_ANTICLIP_WIDTH                1  /* DSP2DRC_ANTICLIP */
#define WM8996_DSP2RX_DRC_ENA                   0x0004  /* DSP2RX_DRC_ENA */
#define WM8996_DSP2RX_DRC_ENA_MASK              0x0004  /* DSP2RX_DRC_ENA */
#define WM8996_DSP2RX_DRC_ENA_SHIFT                  2  /* DSP2RX_DRC_ENA */
#define WM8996_DSP2RX_DRC_ENA_WIDTH                  1  /* DSP2RX_DRC_ENA */
#define WM8996_DSP2TXL_DRC_ENA                  0x0002  /* DSP2TXL_DRC_ENA */
#define WM8996_DSP2TXL_DRC_ENA_MASK             0x0002  /* DSP2TXL_DRC_ENA */
#define WM8996_DSP2TXL_DRC_ENA_SHIFT                 1  /* DSP2TXL_DRC_ENA */
#define WM8996_DSP2TXL_DRC_ENA_WIDTH                 1  /* DSP2TXL_DRC_ENA */
#define WM8996_DSP2TXR_DRC_ENA                  0x0001  /* DSP2TXR_DRC_ENA */
#define WM8996_DSP2TXR_DRC_ENA_MASK             0x0001  /* DSP2TXR_DRC_ENA */
#define WM8996_DSP2TXR_DRC_ENA_SHIFT                 0  /* DSP2TXR_DRC_ENA */
#define WM8996_DSP2TXR_DRC_ENA_WIDTH                 1  /* DSP2TXR_DRC_ENA */

/*
 * R1345 (0x541) - DSP2 DRC (2)
 */
#define WM8996_DSP2DRC_ATK_MASK                 0x1E00  /* DSP2DRC_ATK - [12:9] */
#define WM8996_DSP2DRC_ATK_SHIFT                     9  /* DSP2DRC_ATK - [12:9] */
#define WM8996_DSP2DRC_ATK_WIDTH                     4  /* DSP2DRC_ATK - [12:9] */
#define WM8996_DSP2DRC_DCY_MASK                 0x01E0  /* DSP2DRC_DCY - [8:5] */
#define WM8996_DSP2DRC_DCY_SHIFT                     5  /* DSP2DRC_DCY - [8:5] */
#define WM8996_DSP2DRC_DCY_WIDTH                     4  /* DSP2DRC_DCY - [8:5] */
#define WM8996_DSP2DRC_MINGAIN_MASK             0x001C  /* DSP2DRC_MINGAIN - [4:2] */
#define WM8996_DSP2DRC_MINGAIN_SHIFT                 2  /* DSP2DRC_MINGAIN - [4:2] */
#define WM8996_DSP2DRC_MINGAIN_WIDTH                 3  /* DSP2DRC_MINGAIN - [4:2] */
#define WM8996_DSP2DRC_MAXGAIN_MASK             0x0003  /* DSP2DRC_MAXGAIN - [1:0] */
#define WM8996_DSP2DRC_MAXGAIN_SHIFT                 0  /* DSP2DRC_MAXGAIN - [1:0] */
#define WM8996_DSP2DRC_MAXGAIN_WIDTH                 2  /* DSP2DRC_MAXGAIN - [1:0] */

/*
 * R1346 (0x542) - DSP2 DRC (3)
 */
#define WM8996_DSP2DRC_NG_MINGAIN_MASK          0xF000  /* DSP2DRC_NG_MINGAIN - [15:12] */
#define WM8996_DSP2DRC_NG_MINGAIN_SHIFT             12  /* DSP2DRC_NG_MINGAIN - [15:12] */
#define WM8996_DSP2DRC_NG_MINGAIN_WIDTH              4  /* DSP2DRC_NG_MINGAIN - [15:12] */
#define WM8996_DSP2DRC_NG_EXP_MASK              0x0C00  /* DSP2DRC_NG_EXP - [11:10] */
#define WM8996_DSP2DRC_NG_EXP_SHIFT                 10  /* DSP2DRC_NG_EXP - [11:10] */
#define WM8996_DSP2DRC_NG_EXP_WIDTH                  2  /* DSP2DRC_NG_EXP - [11:10] */
#define WM8996_DSP2DRC_QR_THR_MASK              0x0300  /* DSP2DRC_QR_THR - [9:8] */
#define WM8996_DSP2DRC_QR_THR_SHIFT                  8  /* DSP2DRC_QR_THR - [9:8] */
#define WM8996_DSP2DRC_QR_THR_WIDTH                  2  /* DSP2DRC_QR_THR - [9:8] */
#define WM8996_DSP2DRC_QR_DCY_MASK              0x00C0  /* DSP2DRC_QR_DCY - [7:6] */
#define WM8996_DSP2DRC_QR_DCY_SHIFT                  6  /* DSP2DRC_QR_DCY - [7:6] */
#define WM8996_DSP2DRC_QR_DCY_WIDTH                  2  /* DSP2DRC_QR_DCY - [7:6] */
#define WM8996_DSP2DRC_HI_COMP_MASK             0x0038  /* DSP2DRC_HI_COMP - [5:3] */
#define WM8996_DSP2DRC_HI_COMP_SHIFT                 3  /* DSP2DRC_HI_COMP - [5:3] */
#define WM8996_DSP2DRC_HI_COMP_WIDTH                 3  /* DSP2DRC_HI_COMP - [5:3] */
#define WM8996_DSP2DRC_LO_COMP_MASK             0x0007  /* DSP2DRC_LO_COMP - [2:0] */
#define WM8996_DSP2DRC_LO_COMP_SHIFT                 0  /* DSP2DRC_LO_COMP - [2:0] */
#define WM8996_DSP2DRC_LO_COMP_WIDTH                 3  /* DSP2DRC_LO_COMP - [2:0] */

/*
 * R1347 (0x543) - DSP2 DRC (4)
 */
#define WM8996_DSP2DRC_KNEE_IP_MASK             0x07E0  /* DSP2DRC_KNEE_IP - [10:5] */
#define WM8996_DSP2DRC_KNEE_IP_SHIFT                 5  /* DSP2DRC_KNEE_IP - [10:5] */
#define WM8996_DSP2DRC_KNEE_IP_WIDTH                 6  /* DSP2DRC_KNEE_IP - [10:5] */
#define WM8996_DSP2DRC_KNEE_OP_MASK             0x001F  /* DSP2DRC_KNEE_OP - [4:0] */
#define WM8996_DSP2DRC_KNEE_OP_SHIFT                 0  /* DSP2DRC_KNEE_OP - [4:0] */
#define WM8996_DSP2DRC_KNEE_OP_WIDTH                 5  /* DSP2DRC_KNEE_OP - [4:0] */

/*
 * R1348 (0x544) - DSP2 DRC (5)
 */
#define WM8996_DSP2DRC_KNEE2_IP_MASK            0x03E0  /* DSP2DRC_KNEE2_IP - [9:5] */
#define WM8996_DSP2DRC_KNEE2_IP_SHIFT                5  /* DSP2DRC_KNEE2_IP - [9:5] */
#define WM8996_DSP2DRC_KNEE2_IP_WIDTH                5  /* DSP2DRC_KNEE2_IP - [9:5] */
#define WM8996_DSP2DRC_KNEE2_OP_MASK            0x001F  /* DSP2DRC_KNEE2_OP - [4:0] */
#define WM8996_DSP2DRC_KNEE2_OP_SHIFT                0  /* DSP2DRC_KNEE2_OP - [4:0] */
#define WM8996_DSP2DRC_KNEE2_OP_WIDTH                5  /* DSP2DRC_KNEE2_OP - [4:0] */

/*
 * R1408 (0x580) - DSP2 RX EQ Gains (1)
 */
#define WM8996_DSP2RX_EQ_B1_GAIN_MASK           0xF800  /* DSP2RX_EQ_B1_GAIN - [15:11] */
#define WM8996_DSP2RX_EQ_B1_GAIN_SHIFT              11  /* DSP2RX_EQ_B1_GAIN - [15:11] */
#define WM8996_DSP2RX_EQ_B1_GAIN_WIDTH               5  /* DSP2RX_EQ_B1_GAIN - [15:11] */
#define WM8996_DSP2RX_EQ_B2_GAIN_MASK           0x07C0  /* DSP2RX_EQ_B2_GAIN - [10:6] */
#define WM8996_DSP2RX_EQ_B2_GAIN_SHIFT               6  /* DSP2RX_EQ_B2_GAIN - [10:6] */
#define WM8996_DSP2RX_EQ_B2_GAIN_WIDTH               5  /* DSP2RX_EQ_B2_GAIN - [10:6] */
#define WM8996_DSP2RX_EQ_B3_GAIN_MASK           0x003E  /* DSP2RX_EQ_B3_GAIN - [5:1] */
#define WM8996_DSP2RX_EQ_B3_GAIN_SHIFT               1  /* DSP2RX_EQ_B3_GAIN - [5:1] */
#define WM8996_DSP2RX_EQ_B3_GAIN_WIDTH               5  /* DSP2RX_EQ_B3_GAIN - [5:1] */
#define WM8996_DSP2RX_EQ_ENA                    0x0001  /* DSP2RX_EQ_ENA */
#define WM8996_DSP2RX_EQ_ENA_MASK               0x0001  /* DSP2RX_EQ_ENA */
#define WM8996_DSP2RX_EQ_ENA_SHIFT                   0  /* DSP2RX_EQ_ENA */
#define WM8996_DSP2RX_EQ_ENA_WIDTH                   1  /* DSP2RX_EQ_ENA */

/*
 * R1409 (0x581) - DSP2 RX EQ Gains (2)
 */
#define WM8996_DSP2RX_EQ_B4_GAIN_MASK           0xF800  /* DSP2RX_EQ_B4_GAIN - [15:11] */
#define WM8996_DSP2RX_EQ_B4_GAIN_SHIFT              11  /* DSP2RX_EQ_B4_GAIN - [15:11] */
#define WM8996_DSP2RX_EQ_B4_GAIN_WIDTH               5  /* DSP2RX_EQ_B4_GAIN - [15:11] */
#define WM8996_DSP2RX_EQ_B5_GAIN_MASK           0x07C0  /* DSP2RX_EQ_B5_GAIN - [10:6] */
#define WM8996_DSP2RX_EQ_B5_GAIN_SHIFT               6  /* DSP2RX_EQ_B5_GAIN - [10:6] */
#define WM8996_DSP2RX_EQ_B5_GAIN_WIDTH               5  /* DSP2RX_EQ_B5_GAIN - [10:6] */

/*
 * R1410 (0x582) - DSP2 RX EQ Band 1 A
 */
#define WM8996_DSP2RX_EQ_B1_A_MASK              0xFFFF  /* DSP2RX_EQ_B1_A - [15:0] */
#define WM8996_DSP2RX_EQ_B1_A_SHIFT                  0  /* DSP2RX_EQ_B1_A - [15:0] */
#define WM8996_DSP2RX_EQ_B1_A_WIDTH                 16  /* DSP2RX_EQ_B1_A - [15:0] */

/*
 * R1411 (0x583) - DSP2 RX EQ Band 1 B
 */
#define WM8996_DSP2RX_EQ_B1_B_MASK              0xFFFF  /* DSP2RX_EQ_B1_B - [15:0] */
#define WM8996_DSP2RX_EQ_B1_B_SHIFT                  0  /* DSP2RX_EQ_B1_B - [15:0] */
#define WM8996_DSP2RX_EQ_B1_B_WIDTH                 16  /* DSP2RX_EQ_B1_B - [15:0] */

/*
 * R1412 (0x584) - DSP2 RX EQ Band 1 PG
 */
#define WM8996_DSP2RX_EQ_B1_PG_MASK             0xFFFF  /* DSP2RX_EQ_B1_PG - [15:0] */
#define WM8996_DSP2RX_EQ_B1_PG_SHIFT                 0  /* DSP2RX_EQ_B1_PG - [15:0] */
#define WM8996_DSP2RX_EQ_B1_PG_WIDTH                16  /* DSP2RX_EQ_B1_PG - [15:0] */

/*
 * R1413 (0x585) - DSP2 RX EQ Band 2 A
 */
#define WM8996_DSP2RX_EQ_B2_A_MASK              0xFFFF  /* DSP2RX_EQ_B2_A - [15:0] */
#define WM8996_DSP2RX_EQ_B2_A_SHIFT                  0  /* DSP2RX_EQ_B2_A - [15:0] */
#define WM8996_DSP2RX_EQ_B2_A_WIDTH                 16  /* DSP2RX_EQ_B2_A - [15:0] */

/*
 * R1414 (0x586) - DSP2 RX EQ Band 2 B
 */
#define WM8996_DSP2RX_EQ_B2_B_MASK              0xFFFF  /* DSP2RX_EQ_B2_B - [15:0] */
#define WM8996_DSP2RX_EQ_B2_B_SHIFT                  0  /* DSP2RX_EQ_B2_B - [15:0] */
#define WM8996_DSP2RX_EQ_B2_B_WIDTH                 16  /* DSP2RX_EQ_B2_B - [15:0] */

/*
 * R1415 (0x587) - DSP2 RX EQ Band 2 C
 */
#define WM8996_DSP2RX_EQ_B2_C_MASK              0xFFFF  /* DSP2RX_EQ_B2_C - [15:0] */
#define WM8996_DSP2RX_EQ_B2_C_SHIFT                  0  /* DSP2RX_EQ_B2_C - [15:0] */
#define WM8996_DSP2RX_EQ_B2_C_WIDTH                 16  /* DSP2RX_EQ_B2_C - [15:0] */

/*
 * R1416 (0x588) - DSP2 RX EQ Band 2 PG
 */
#define WM8996_DSP2RX_EQ_B2_PG_MASK             0xFFFF  /* DSP2RX_EQ_B2_PG - [15:0] */
#define WM8996_DSP2RX_EQ_B2_PG_SHIFT                 0  /* DSP2RX_EQ_B2_PG - [15:0] */
#define WM8996_DSP2RX_EQ_B2_PG_WIDTH                16  /* DSP2RX_EQ_B2_PG - [15:0] */

/*
 * R1417 (0x589) - DSP2 RX EQ Band 3 A
 */
#define WM8996_DSP2RX_EQ_B3_A_MASK              0xFFFF  /* DSP2RX_EQ_B3_A - [15:0] */
#define WM8996_DSP2RX_EQ_B3_A_SHIFT                  0  /* DSP2RX_EQ_B3_A - [15:0] */
#define WM8996_DSP2RX_EQ_B3_A_WIDTH                 16  /* DSP2RX_EQ_B3_A - [15:0] */

/*
 * R1418 (0x58A) - DSP2 RX EQ Band 3 B
 */
#define WM8996_DSP2RX_EQ_B3_B_MASK              0xFFFF  /* DSP2RX_EQ_B3_B - [15:0] */
#define WM8996_DSP2RX_EQ_B3_B_SHIFT                  0  /* DSP2RX_EQ_B3_B - [15:0] */
#define WM8996_DSP2RX_EQ_B3_B_WIDTH                 16  /* DSP2RX_EQ_B3_B - [15:0] */

/*
 * R1419 (0x58B) - DSP2 RX EQ Band 3 C
 */
#define WM8996_DSP2RX_EQ_B3_C_MASK              0xFFFF  /* DSP2RX_EQ_B3_C - [15:0] */
#define WM8996_DSP2RX_EQ_B3_C_SHIFT                  0  /* DSP2RX_EQ_B3_C - [15:0] */
#define WM8996_DSP2RX_EQ_B3_C_WIDTH                 16  /* DSP2RX_EQ_B3_C - [15:0] */

/*
 * R1420 (0x58C) - DSP2 RX EQ Band 3 PG
 */
#define WM8996_DSP2RX_EQ_B3_PG_MASK             0xFFFF  /* DSP2RX_EQ_B3_PG - [15:0] */
#define WM8996_DSP2RX_EQ_B3_PG_SHIFT                 0  /* DSP2RX_EQ_B3_PG - [15:0] */
#define WM8996_DSP2RX_EQ_B3_PG_WIDTH                16  /* DSP2RX_EQ_B3_PG - [15:0] */

/*
 * R1421 (0x58D) - DSP2 RX EQ Band 4 A
 */
#define WM8996_DSP2RX_EQ_B4_A_MASK              0xFFFF  /* DSP2RX_EQ_B4_A - [15:0] */
#define WM8996_DSP2RX_EQ_B4_A_SHIFT                  0  /* DSP2RX_EQ_B4_A - [15:0] */
#define WM8996_DSP2RX_EQ_B4_A_WIDTH                 16  /* DSP2RX_EQ_B4_A - [15:0] */

/*
 * R1422 (0x58E) - DSP2 RX EQ Band 4 B
 */
#define WM8996_DSP2RX_EQ_B4_B_MASK              0xFFFF  /* DSP2RX_EQ_B4_B - [15:0] */
#define WM8996_DSP2RX_EQ_B4_B_SHIFT                  0  /* DSP2RX_EQ_B4_B - [15:0] */
#define WM8996_DSP2RX_EQ_B4_B_WIDTH                 16  /* DSP2RX_EQ_B4_B - [15:0] */

/*
 * R1423 (0x58F) - DSP2 RX EQ Band 4 C
 */
#define WM8996_DSP2RX_EQ_B4_C_MASK              0xFFFF  /* DSP2RX_EQ_B4_C - [15:0] */
#define WM8996_DSP2RX_EQ_B4_C_SHIFT                  0  /* DSP2RX_EQ_B4_C - [15:0] */
#define WM8996_DSP2RX_EQ_B4_C_WIDTH                 16  /* DSP2RX_EQ_B4_C - [15:0] */

/*
 * R1424 (0x590) - DSP2 RX EQ Band 4 PG
 */
#define WM8996_DSP2RX_EQ_B4_PG_MASK             0xFFFF  /* DSP2RX_EQ_B4_PG - [15:0] */
#define WM8996_DSP2RX_EQ_B4_PG_SHIFT                 0  /* DSP2RX_EQ_B4_PG - [15:0] */
#define WM8996_DSP2RX_EQ_B4_PG_WIDTH                16  /* DSP2RX_EQ_B4_PG - [15:0] */

/*
 * R1425 (0x591) - DSP2 RX EQ Band 5 A
 */
#define WM8996_DSP2RX_EQ_B5_A_MASK              0xFFFF  /* DSP2RX_EQ_B5_A - [15:0] */
#define WM8996_DSP2RX_EQ_B5_A_SHIFT                  0  /* DSP2RX_EQ_B5_A - [15:0] */
#define WM8996_DSP2RX_EQ_B5_A_WIDTH                 16  /* DSP2RX_EQ_B5_A - [15:0] */

/*
 * R1426 (0x592) - DSP2 RX EQ Band 5 B
 */
#define WM8996_DSP2RX_EQ_B5_B_MASK              0xFFFF  /* DSP2RX_EQ_B5_B - [15:0] */
#define WM8996_DSP2RX_EQ_B5_B_SHIFT                  0  /* DSP2RX_EQ_B5_B - [15:0] */
#define WM8996_DSP2RX_EQ_B5_B_WIDTH                 16  /* DSP2RX_EQ_B5_B - [15:0] */

/*
 * R1427 (0x593) - DSP2 RX EQ Band 5 PG
 */
#define WM8996_DSP2RX_EQ_B5_PG_MASK             0xFFFF  /* DSP2RX_EQ_B5_PG - [15:0] */
#define WM8996_DSP2RX_EQ_B5_PG_SHIFT                 0  /* DSP2RX_EQ_B5_PG - [15:0] */
#define WM8996_DSP2RX_EQ_B5_PG_WIDTH                16  /* DSP2RX_EQ_B5_PG - [15:0] */

/*
 * R1536 (0x600) - DAC1 Mixer Volumes
 */
#define WM8996_ADCR_DAC1_VOL_MASK               0x03E0  /* ADCR_DAC1_VOL - [9:5] */
#define WM8996_ADCR_DAC1_VOL_SHIFT                   5  /* ADCR_DAC1_VOL - [9:5] */
#define WM8996_ADCR_DAC1_VOL_WIDTH                   5  /* ADCR_DAC1_VOL - [9:5] */
#define WM8996_ADCL_DAC1_VOL_MASK               0x001F  /* ADCL_DAC1_VOL - [4:0] */
#define WM8996_ADCL_DAC1_VOL_SHIFT                   0  /* ADCL_DAC1_VOL - [4:0] */
#define WM8996_ADCL_DAC1_VOL_WIDTH                   5  /* ADCL_DAC1_VOL - [4:0] */

/*
 * R1537 (0x601) - DAC1 Left Mixer Routing
 */
#define WM8996_ADCR_TO_DAC1L                    0x0020  /* ADCR_TO_DAC1L */
#define WM8996_ADCR_TO_DAC1L_MASK               0x0020  /* ADCR_TO_DAC1L */
#define WM8996_ADCR_TO_DAC1L_SHIFT                   5  /* ADCR_TO_DAC1L */
#define WM8996_ADCR_TO_DAC1L_WIDTH                   1  /* ADCR_TO_DAC1L */
#define WM8996_ADCL_TO_DAC1L                    0x0010  /* ADCL_TO_DAC1L */
#define WM8996_ADCL_TO_DAC1L_MASK               0x0010  /* ADCL_TO_DAC1L */
#define WM8996_ADCL_TO_DAC1L_SHIFT                   4  /* ADCL_TO_DAC1L */
#define WM8996_ADCL_TO_DAC1L_WIDTH                   1  /* ADCL_TO_DAC1L */
#define WM8996_DSP2RXL_TO_DAC1L                 0x0002  /* DSP2RXL_TO_DAC1L */
#define WM8996_DSP2RXL_TO_DAC1L_MASK            0x0002  /* DSP2RXL_TO_DAC1L */
#define WM8996_DSP2RXL_TO_DAC1L_SHIFT                1  /* DSP2RXL_TO_DAC1L */
#define WM8996_DSP2RXL_TO_DAC1L_WIDTH                1  /* DSP2RXL_TO_DAC1L */
#define WM8996_DSP1RXL_TO_DAC1L                 0x0001  /* DSP1RXL_TO_DAC1L */
#define WM8996_DSP1RXL_TO_DAC1L_MASK            0x0001  /* DSP1RXL_TO_DAC1L */
#define WM8996_DSP1RXL_TO_DAC1L_SHIFT                0  /* DSP1RXL_TO_DAC1L */
#define WM8996_DSP1RXL_TO_DAC1L_WIDTH                1  /* DSP1RXL_TO_DAC1L */

/*
 * R1538 (0x602) - DAC1 Right Mixer Routing
 */
#define WM8996_ADCR_TO_DAC1R                    0x0020  /* ADCR_TO_DAC1R */
#define WM8996_ADCR_TO_DAC1R_MASK               0x0020  /* ADCR_TO_DAC1R */
#define WM8996_ADCR_TO_DAC1R_SHIFT                   5  /* ADCR_TO_DAC1R */
#define WM8996_ADCR_TO_DAC1R_WIDTH                   1  /* ADCR_TO_DAC1R */
#define WM8996_ADCL_TO_DAC1R                    0x0010  /* ADCL_TO_DAC1R */
#define WM8996_ADCL_TO_DAC1R_MASK               0x0010  /* ADCL_TO_DAC1R */
#define WM8996_ADCL_TO_DAC1R_SHIFT                   4  /* ADCL_TO_DAC1R */
#define WM8996_ADCL_TO_DAC1R_WIDTH                   1  /* ADCL_TO_DAC1R */
#define WM8996_DSP2RXR_TO_DAC1R                 0x0002  /* DSP2RXR_TO_DAC1R */
#define WM8996_DSP2RXR_TO_DAC1R_MASK            0x0002  /* DSP2RXR_TO_DAC1R */
#define WM8996_DSP2RXR_TO_DAC1R_SHIFT                1  /* DSP2RXR_TO_DAC1R */
#define WM8996_DSP2RXR_TO_DAC1R_WIDTH                1  /* DSP2RXR_TO_DAC1R */
#define WM8996_DSP1RXR_TO_DAC1R                 0x0001  /* DSP1RXR_TO_DAC1R */
#define WM8996_DSP1RXR_TO_DAC1R_MASK            0x0001  /* DSP1RXR_TO_DAC1R */
#define WM8996_DSP1RXR_TO_DAC1R_SHIFT                0  /* DSP1RXR_TO_DAC1R */
#define WM8996_DSP1RXR_TO_DAC1R_WIDTH                1  /* DSP1RXR_TO_DAC1R */

/*
 * R1539 (0x603) - DAC2 Mixer Volumes
 */
#define WM8996_ADCR_DAC2_VOL_MASK               0x03E0  /* ADCR_DAC2_VOL - [9:5] */
#define WM8996_ADCR_DAC2_VOL_SHIFT                   5  /* ADCR_DAC2_VOL - [9:5] */
#define WM8996_ADCR_DAC2_VOL_WIDTH                   5  /* ADCR_DAC2_VOL - [9:5] */
#define WM8996_ADCL_DAC2_VOL_MASK               0x001F  /* ADCL_DAC2_VOL - [4:0] */
#define WM8996_ADCL_DAC2_VOL_SHIFT                   0  /* ADCL_DAC2_VOL - [4:0] */
#define WM8996_ADCL_DAC2_VOL_WIDTH                   5  /* ADCL_DAC2_VOL - [4:0] */

/*
 * R1540 (0x604) - DAC2 Left Mixer Routing
 */
#define WM8996_ADCR_TO_DAC2L                    0x0020  /* ADCR_TO_DAC2L */
#define WM8996_ADCR_TO_DAC2L_MASK               0x0020  /* ADCR_TO_DAC2L */
#define WM8996_ADCR_TO_DAC2L_SHIFT                   5  /* ADCR_TO_DAC2L */
#define WM8996_ADCR_TO_DAC2L_WIDTH                   1  /* ADCR_TO_DAC2L */
#define WM8996_ADCL_TO_DAC2L                    0x0010  /* ADCL_TO_DAC2L */
#define WM8996_ADCL_TO_DAC2L_MASK               0x0010  /* ADCL_TO_DAC2L */
#define WM8996_ADCL_TO_DAC2L_SHIFT                   4  /* ADCL_TO_DAC2L */
#define WM8996_ADCL_TO_DAC2L_WIDTH                   1  /* ADCL_TO_DAC2L */
#define WM8996_DSP2RXL_TO_DAC2L                 0x0002  /* DSP2RXL_TO_DAC2L */
#define WM8996_DSP2RXL_TO_DAC2L_MASK            0x0002  /* DSP2RXL_TO_DAC2L */
#define WM8996_DSP2RXL_TO_DAC2L_SHIFT                1  /* DSP2RXL_TO_DAC2L */
#define WM8996_DSP2RXL_TO_DAC2L_WIDTH                1  /* DSP2RXL_TO_DAC2L */
#define WM8996_DSP1RXL_TO_DAC2L                 0x0001  /* DSP1RXL_TO_DAC2L */
#define WM8996_DSP1RXL_TO_DAC2L_MASK            0x0001  /* DSP1RXL_TO_DAC2L */
#define WM8996_DSP1RXL_TO_DAC2L_SHIFT                0  /* DSP1RXL_TO_DAC2L */
#define WM8996_DSP1RXL_TO_DAC2L_WIDTH                1  /* DSP1RXL_TO_DAC2L */

/*
 * R1541 (0x605) - DAC2 Right Mixer Routing
 */
#define WM8996_ADCR_TO_DAC2R                    0x0020  /* ADCR_TO_DAC2R */
#define WM8996_ADCR_TO_DAC2R_MASK               0x0020  /* ADCR_TO_DAC2R */
#define WM8996_ADCR_TO_DAC2R_SHIFT                   5  /* ADCR_TO_DAC2R */
#define WM8996_ADCR_TO_DAC2R_WIDTH                   1  /* ADCR_TO_DAC2R */
#define WM8996_ADCL_TO_DAC2R                    0x0010  /* ADCL_TO_DAC2R */
#define WM8996_ADCL_TO_DAC2R_MASK               0x0010  /* ADCL_TO_DAC2R */
#define WM8996_ADCL_TO_DAC2R_SHIFT                   4  /* ADCL_TO_DAC2R */
#define WM8996_ADCL_TO_DAC2R_WIDTH                   1  /* ADCL_TO_DAC2R */
#define WM8996_DSP2RXR_TO_DAC2R                 0x0002  /* DSP2RXR_TO_DAC2R */
#define WM8996_DSP2RXR_TO_DAC2R_MASK            0x0002  /* DSP2RXR_TO_DAC2R */
#define WM8996_DSP2RXR_TO_DAC2R_SHIFT                1  /* DSP2RXR_TO_DAC2R */
#define WM8996_DSP2RXR_TO_DAC2R_WIDTH                1  /* DSP2RXR_TO_DAC2R */
#define WM8996_DSP1RXR_TO_DAC2R                 0x0001  /* DSP1RXR_TO_DAC2R */
#define WM8996_DSP1RXR_TO_DAC2R_MASK            0x0001  /* DSP1RXR_TO_DAC2R */
#define WM8996_DSP1RXR_TO_DAC2R_SHIFT                0  /* DSP1RXR_TO_DAC2R */
#define WM8996_DSP1RXR_TO_DAC2R_WIDTH                1  /* DSP1RXR_TO_DAC2R */

/*
 * R1542 (0x606) - DSP1 TX Left Mixer Routing
 */
#define WM8996_ADC1L_TO_DSP1TXL                 0x0002  /* ADC1L_TO_DSP1TXL */
#define WM8996_ADC1L_TO_DSP1TXL_MASK            0x0002  /* ADC1L_TO_DSP1TXL */
#define WM8996_ADC1L_TO_DSP1TXL_SHIFT                1  /* ADC1L_TO_DSP1TXL */
#define WM8996_ADC1L_TO_DSP1TXL_WIDTH                1  /* ADC1L_TO_DSP1TXL */
#define WM8996_DACL_TO_DSP1TXL                  0x0001  /* DACL_TO_DSP1TXL */
#define WM8996_DACL_TO_DSP1TXL_MASK             0x0001  /* DACL_TO_DSP1TXL */
#define WM8996_DACL_TO_DSP1TXL_SHIFT                 0  /* DACL_TO_DSP1TXL */
#define WM8996_DACL_TO_DSP1TXL_WIDTH                 1  /* DACL_TO_DSP1TXL */

/*
 * R1543 (0x607) - DSP1 TX Right Mixer Routing
 */
#define WM8996_ADC1R_TO_DSP1TXR                 0x0002  /* ADC1R_TO_DSP1TXR */
#define WM8996_ADC1R_TO_DSP1TXR_MASK            0x0002  /* ADC1R_TO_DSP1TXR */
#define WM8996_ADC1R_TO_DSP1TXR_SHIFT                1  /* ADC1R_TO_DSP1TXR */
#define WM8996_ADC1R_TO_DSP1TXR_WIDTH                1  /* ADC1R_TO_DSP1TXR */
#define WM8996_DACR_TO_DSP1TXR                  0x0001  /* DACR_TO_DSP1TXR */
#define WM8996_DACR_TO_DSP1TXR_MASK             0x0001  /* DACR_TO_DSP1TXR */
#define WM8996_DACR_TO_DSP1TXR_SHIFT                 0  /* DACR_TO_DSP1TXR */
#define WM8996_DACR_TO_DSP1TXR_WIDTH                 1  /* DACR_TO_DSP1TXR */

/*
 * R1544 (0x608) - DSP2 TX Left Mixer Routing
 */
#define WM8996_ADC2L_TO_DSP2TXL                 0x0002  /* ADC2L_TO_DSP2TXL */
#define WM8996_ADC2L_TO_DSP2TXL_MASK            0x0002  /* ADC2L_TO_DSP2TXL */
#define WM8996_ADC2L_TO_DSP2TXL_SHIFT                1  /* ADC2L_TO_DSP2TXL */
#define WM8996_ADC2L_TO_DSP2TXL_WIDTH                1  /* ADC2L_TO_DSP2TXL */
#define WM8996_DACL_TO_DSP2TXL                  0x0001  /* DACL_TO_DSP2TXL */
#define WM8996_DACL_TO_DSP2TXL_MASK             0x0001  /* DACL_TO_DSP2TXL */
#define WM8996_DACL_TO_DSP2TXL_SHIFT                 0  /* DACL_TO_DSP2TXL */
#define WM8996_DACL_TO_DSP2TXL_WIDTH                 1  /* DACL_TO_DSP2TXL */

/*
 * R1545 (0x609) - DSP2 TX Right Mixer Routing
 */
#define WM8996_ADC2R_TO_DSP2TXR                 0x0002  /* ADC2R_TO_DSP2TXR */
#define WM8996_ADC2R_TO_DSP2TXR_MASK            0x0002  /* ADC2R_TO_DSP2TXR */
#define WM8996_ADC2R_TO_DSP2TXR_SHIFT                1  /* ADC2R_TO_DSP2TXR */
#define WM8996_ADC2R_TO_DSP2TXR_WIDTH                1  /* ADC2R_TO_DSP2TXR */
#define WM8996_DACR_TO_DSP2TXR                  0x0001  /* DACR_TO_DSP2TXR */
#define WM8996_DACR_TO_DSP2TXR_MASK             0x0001  /* DACR_TO_DSP2TXR */
#define WM8996_DACR_TO_DSP2TXR_SHIFT                 0  /* DACR_TO_DSP2TXR */
#define WM8996_DACR_TO_DSP2TXR_WIDTH                 1  /* DACR_TO_DSP2TXR */

/*
 * R1546 (0x60A) - DSP TX Mixer Select
 */
#define WM8996_DAC_TO_DSPTX_SRC                 0x0001  /* DAC_TO_DSPTX_SRC */
#define WM8996_DAC_TO_DSPTX_SRC_MASK            0x0001  /* DAC_TO_DSPTX_SRC */
#define WM8996_DAC_TO_DSPTX_SRC_SHIFT                0  /* DAC_TO_DSPTX_SRC */
#define WM8996_DAC_TO_DSPTX_SRC_WIDTH                1  /* DAC_TO_DSPTX_SRC */

/*
 * R1552 (0x610) - DAC Softmute
 */
#define WM8996_DAC_SOFTMUTEMODE                 0x0002  /* DAC_SOFTMUTEMODE */
#define WM8996_DAC_SOFTMUTEMODE_MASK            0x0002  /* DAC_SOFTMUTEMODE */
#define WM8996_DAC_SOFTMUTEMODE_SHIFT                1  /* DAC_SOFTMUTEMODE */
#define WM8996_DAC_SOFTMUTEMODE_WIDTH                1  /* DAC_SOFTMUTEMODE */
#define WM8996_DAC_MUTERATE                     0x0001  /* DAC_MUTERATE */
#define WM8996_DAC_MUTERATE_MASK                0x0001  /* DAC_MUTERATE */
#define WM8996_DAC_MUTERATE_SHIFT                    0  /* DAC_MUTERATE */
#define WM8996_DAC_MUTERATE_WIDTH                    1  /* DAC_MUTERATE */

/*
 * R1568 (0x620) - Oversampling
 */
#define WM8996_SPK_OSR128                       0x0008  /* SPK_OSR128 */
#define WM8996_SPK_OSR128_MASK                  0x0008  /* SPK_OSR128 */
#define WM8996_SPK_OSR128_SHIFT                      3  /* SPK_OSR128 */
#define WM8996_SPK_OSR128_WIDTH                      1  /* SPK_OSR128 */
#define WM8996_DMIC_OSR64                       0x0004  /* DMIC_OSR64 */
#define WM8996_DMIC_OSR64_MASK                  0x0004  /* DMIC_OSR64 */
#define WM8996_DMIC_OSR64_SHIFT                      2  /* DMIC_OSR64 */
#define WM8996_DMIC_OSR64_WIDTH                      1  /* DMIC_OSR64 */
#define WM8996_ADC_OSR128                       0x0002  /* ADC_OSR128 */
#define WM8996_ADC_OSR128_MASK                  0x0002  /* ADC_OSR128 */
#define WM8996_ADC_OSR128_SHIFT                      1  /* ADC_OSR128 */
#define WM8996_ADC_OSR128_WIDTH                      1  /* ADC_OSR128 */
#define WM8996_DAC_OSR128                       0x0001  /* DAC_OSR128 */
#define WM8996_DAC_OSR128_MASK                  0x0001  /* DAC_OSR128 */
#define WM8996_DAC_OSR128_SHIFT                      0  /* DAC_OSR128 */
#define WM8996_DAC_OSR128_WIDTH                      1  /* DAC_OSR128 */

/*
 * R1569 (0x621) - Sidetone
 */
#define WM8996_ST_LPF                           0x1000  /* ST_LPF */
#define WM8996_ST_LPF_MASK                      0x1000  /* ST_LPF */
#define WM8996_ST_LPF_SHIFT                         12  /* ST_LPF */
#define WM8996_ST_LPF_WIDTH                          1  /* ST_LPF */
#define WM8996_ST_HPF_CUT_MASK                  0x0380  /* ST_HPF_CUT - [9:7] */
#define WM8996_ST_HPF_CUT_SHIFT                      7  /* ST_HPF_CUT - [9:7] */
#define WM8996_ST_HPF_CUT_WIDTH                      3  /* ST_HPF_CUT - [9:7] */
#define WM8996_ST_HPF                           0x0040  /* ST_HPF */
#define WM8996_ST_HPF_MASK                      0x0040  /* ST_HPF */
#define WM8996_ST_HPF_SHIFT                          6  /* ST_HPF */
#define WM8996_ST_HPF_WIDTH                          1  /* ST_HPF */
#define WM8996_STR_SEL                          0x0002  /* STR_SEL */
#define WM8996_STR_SEL_MASK                     0x0002  /* STR_SEL */
#define WM8996_STR_SEL_SHIFT                         1  /* STR_SEL */
#define WM8996_STR_SEL_WIDTH                         1  /* STR_SEL */
#define WM8996_STL_SEL                          0x0001  /* STL_SEL */
#define WM8996_STL_SEL_MASK                     0x0001  /* STL_SEL */
#define WM8996_STL_SEL_SHIFT                         0  /* STL_SEL */
#define WM8996_STL_SEL_WIDTH                         1  /* STL_SEL */

/*
 * R1792 (0x700) - GPIO 1
 */
#define WM8996_GP1_DIR                          0x8000  /* GP1_DIR */
#define WM8996_GP1_DIR_MASK                     0x8000  /* GP1_DIR */
#define WM8996_GP1_DIR_SHIFT                        15  /* GP1_DIR */
#define WM8996_GP1_DIR_WIDTH                         1  /* GP1_DIR */
#define WM8996_GP1_PU                           0x4000  /* GP1_PU */
#define WM8996_GP1_PU_MASK                      0x4000  /* GP1_PU */
#define WM8996_GP1_PU_SHIFT                         14  /* GP1_PU */
#define WM8996_GP1_PU_WIDTH                          1  /* GP1_PU */
#define WM8996_GP1_PD                           0x2000  /* GP1_PD */
#define WM8996_GP1_PD_MASK                      0x2000  /* GP1_PD */
#define WM8996_GP1_PD_SHIFT                         13  /* GP1_PD */
#define WM8996_GP1_PD_WIDTH                          1  /* GP1_PD */
#define WM8996_GP1_POL                          0x0400  /* GP1_POL */
#define WM8996_GP1_POL_MASK                     0x0400  /* GP1_POL */
#define WM8996_GP1_POL_SHIFT                        10  /* GP1_POL */
#define WM8996_GP1_POL_WIDTH                         1  /* GP1_POL */
#define WM8996_GP1_OP_CFG                       0x0200  /* GP1_OP_CFG */
#define WM8996_GP1_OP_CFG_MASK                  0x0200  /* GP1_OP_CFG */
#define WM8996_GP1_OP_CFG_SHIFT                      9  /* GP1_OP_CFG */
#define WM8996_GP1_OP_CFG_WIDTH                      1  /* GP1_OP_CFG */
#define WM8996_GP1_DB                           0x0100  /* GP1_DB */
#define WM8996_GP1_DB_MASK                      0x0100  /* GP1_DB */
#define WM8996_GP1_DB_SHIFT                          8  /* GP1_DB */
#define WM8996_GP1_DB_WIDTH                          1  /* GP1_DB */
#define WM8996_GP1_LVL                          0x0040  /* GP1_LVL */
#define WM8996_GP1_LVL_MASK                     0x0040  /* GP1_LVL */
#define WM8996_GP1_LVL_SHIFT                         6  /* GP1_LVL */
#define WM8996_GP1_LVL_WIDTH                         1  /* GP1_LVL */
#define WM8996_GP1_FN_MASK                      0x000F  /* GP1_FN - [3:0] */
#define WM8996_GP1_FN_SHIFT                          0  /* GP1_FN - [3:0] */
#define WM8996_GP1_FN_WIDTH                          4  /* GP1_FN - [3:0] */

/*
 * R1793 (0x701) - GPIO 2
 */
#define WM8996_GP2_DIR                          0x8000  /* GP2_DIR */
#define WM8996_GP2_DIR_MASK                     0x8000  /* GP2_DIR */
#define WM8996_GP2_DIR_SHIFT                        15  /* GP2_DIR */
#define WM8996_GP2_DIR_WIDTH                         1  /* GP2_DIR */
#define WM8996_GP2_PU                           0x4000  /* GP2_PU */
#define WM8996_GP2_PU_MASK                      0x4000  /* GP2_PU */
#define WM8996_GP2_PU_SHIFT                         14  /* GP2_PU */
#define WM8996_GP2_PU_WIDTH                          1  /* GP2_PU */
#define WM8996_GP2_PD                           0x2000  /* GP2_PD */
#define WM8996_GP2_PD_MASK                      0x2000  /* GP2_PD */
#define WM8996_GP2_PD_SHIFT                         13  /* GP2_PD */
#define WM8996_GP2_PD_WIDTH                          1  /* GP2_PD */
#define WM8996_GP2_POL                          0x0400  /* GP2_POL */
#define WM8996_GP2_POL_MASK                     0x0400  /* GP2_POL */
#define WM8996_GP2_POL_SHIFT                        10  /* GP2_POL */
#define WM8996_GP2_POL_WIDTH                         1  /* GP2_POL */
#define WM8996_GP2_OP_CFG                       0x0200  /* GP2_OP_CFG */
#define WM8996_GP2_OP_CFG_MASK                  0x0200  /* GP2_OP_CFG */
#define WM8996_GP2_OP_CFG_SHIFT                      9  /* GP2_OP_CFG */
#define WM8996_GP2_OP_CFG_WIDTH                      1  /* GP2_OP_CFG */
#define WM8996_GP2_DB                           0x0100  /* GP2_DB */
#define WM8996_GP2_DB_MASK                      0x0100  /* GP2_DB */
#define WM8996_GP2_DB_SHIFT                          8  /* GP2_DB */
#define WM8996_GP2_DB_WIDTH                          1  /* GP2_DB */
#define WM8996_GP2_LVL                          0x0040  /* GP2_LVL */
#define WM8996_GP2_LVL_MASK                     0x0040  /* GP2_LVL */
#define WM8996_GP2_LVL_SHIFT                         6  /* GP2_LVL */
#define WM8996_GP2_LVL_WIDTH                         1  /* GP2_LVL */
#define WM8996_GP2_FN_MASK                      0x000F  /* GP2_FN - [3:0] */
#define WM8996_GP2_FN_SHIFT                          0  /* GP2_FN - [3:0] */
#define WM8996_GP2_FN_WIDTH                          4  /* GP2_FN - [3:0] */

/*
 * R1794 (0x702) - GPIO 3
 */
#define WM8996_GP3_DIR                          0x8000  /* GP3_DIR */
#define WM8996_GP3_DIR_MASK                     0x8000  /* GP3_DIR */
#define WM8996_GP3_DIR_SHIFT                        15  /* GP3_DIR */
#define WM8996_GP3_DIR_WIDTH                         1  /* GP3_DIR */
#define WM8996_GP3_PU                           0x4000  /* GP3_PU */
#define WM8996_GP3_PU_MASK                      0x4000  /* GP3_PU */
#define WM8996_GP3_PU_SHIFT                         14  /* GP3_PU */
#define WM8996_GP3_PU_WIDTH                          1  /* GP3_PU */
#define WM8996_GP3_PD                           0x2000  /* GP3_PD */
#define WM8996_GP3_PD_MASK                      0x2000  /* GP3_PD */
#define WM8996_GP3_PD_SHIFT                         13  /* GP3_PD */
#define WM8996_GP3_PD_WIDTH                          1  /* GP3_PD */
#define WM8996_GP3_POL                          0x0400  /* GP3_POL */
#define WM8996_GP3_POL_MASK                     0x0400  /* GP3_POL */
#define WM8996_GP3_POL_SHIFT                        10  /* GP3_POL */
#define WM8996_GP3_POL_WIDTH                         1  /* GP3_POL */
#define WM8996_GP3_OP_CFG                       0x0200  /* GP3_OP_CFG */
#define WM8996_GP3_OP_CFG_MASK                  0x0200  /* GP3_OP_CFG */
#define WM8996_GP3_OP_CFG_SHIFT                      9  /* GP3_OP_CFG */
#define WM8996_GP3_OP_CFG_WIDTH                      1  /* GP3_OP_CFG */
#define WM8996_GP3_DB                           0x0100  /* GP3_DB */
#define WM8996_GP3_DB_MASK                      0x0100  /* GP3_DB */
#define WM8996_GP3_DB_SHIFT                          8  /* GP3_DB */
#define WM8996_GP3_DB_WIDTH                          1  /* GP3_DB */
#define WM8996_GP3_LVL                          0x0040  /* GP3_LVL */
#define WM8996_GP3_LVL_MASK                     0x0040  /* GP3_LVL */
#define WM8996_GP3_LVL_SHIFT                         6  /* GP3_LVL */
#define WM8996_GP3_LVL_WIDTH                         1  /* GP3_LVL */
#define WM8996_GP3_FN_MASK                      0x000F  /* GP3_FN - [3:0] */
#define WM8996_GP3_FN_SHIFT                          0  /* GP3_FN - [3:0] */
#define WM8996_GP3_FN_WIDTH                          4  /* GP3_FN - [3:0] */

/*
 * R1795 (0x703) - GPIO 4
 */
#define WM8996_GP4_DIR                          0x8000  /* GP4_DIR */
#define WM8996_GP4_DIR_MASK                     0x8000  /* GP4_DIR */
#define WM8996_GP4_DIR_SHIFT                        15  /* GP4_DIR */
#define WM8996_GP4_DIR_WIDTH                         1  /* GP4_DIR */
#define WM8996_GP4_PU                           0x4000  /* GP4_PU */
#define WM8996_GP4_PU_MASK                      0x4000  /* GP4_PU */
#define WM8996_GP4_PU_SHIFT                         14  /* GP4_PU */
#define WM8996_GP4_PU_WIDTH                          1  /* GP4_PU */
#define WM8996_GP4_PD                           0x2000  /* GP4_PD */
#define WM8996_GP4_PD_MASK                      0x2000  /* GP4_PD */
#define WM8996_GP4_PD_SHIFT                         13  /* GP4_PD */
#define WM8996_GP4_PD_WIDTH                          1  /* GP4_PD */
#define WM8996_GP4_POL                          0x0400  /* GP4_POL */
#define WM8996_GP4_POL_MASK                     0x0400  /* GP4_POL */
#define WM8996_GP4_POL_SHIFT                        10  /* GP4_POL */
#define WM8996_GP4_POL_WIDTH                         1  /* GP4_POL */
#define WM8996_GP4_OP_CFG                       0x0200  /* GP4_OP_CFG */
#define WM8996_GP4_OP_CFG_MASK                  0x0200  /* GP4_OP_CFG */
#define WM8996_GP4_OP_CFG_SHIFT                      9  /* GP4_OP_CFG */
#define WM8996_GP4_OP_CFG_WIDTH                      1  /* GP4_OP_CFG */
#define WM8996_GP4_DB                           0x0100  /* GP4_DB */
#define WM8996_GP4_DB_MASK                      0x0100  /* GP4_DB */
#define WM8996_GP4_DB_SHIFT                          8  /* GP4_DB */
#define WM8996_GP4_DB_WIDTH                          1  /* GP4_DB */
#define WM8996_GP4_LVL                          0x0040  /* GP4_LVL */
#define WM8996_GP4_LVL_MASK                     0x0040  /* GP4_LVL */
#define WM8996_GP4_LVL_SHIFT                         6  /* GP4_LVL */
#define WM8996_GP4_LVL_WIDTH                         1  /* GP4_LVL */
#define WM8996_GP4_FN_MASK                      0x000F  /* GP4_FN - [3:0] */
#define WM8996_GP4_FN_SHIFT                          0  /* GP4_FN - [3:0] */
#define WM8996_GP4_FN_WIDTH                          4  /* GP4_FN - [3:0] */

/*
 * R1796 (0x704) - GPIO 5
 */
#define WM8996_GP5_DIR                          0x8000  /* GP5_DIR */
#define WM8996_GP5_DIR_MASK                     0x8000  /* GP5_DIR */
#define WM8996_GP5_DIR_SHIFT                        15  /* GP5_DIR */
#define WM8996_GP5_DIR_WIDTH                         1  /* GP5_DIR */
#define WM8996_GP5_PU                           0x4000  /* GP5_PU */
#define WM8996_GP5_PU_MASK                      0x4000  /* GP5_PU */
#define WM8996_GP5_PU_SHIFT                         14  /* GP5_PU */
#define WM8996_GP5_PU_WIDTH                          1  /* GP5_PU */
#define WM8996_GP5_PD                           0x2000  /* GP5_PD */
#define WM8996_GP5_PD_MASK                      0x2000  /* GP5_PD */
#define WM8996_GP5_PD_SHIFT                         13  /* GP5_PD */
#define WM8996_GP5_PD_WIDTH                          1  /* GP5_PD */
#define WM8996_GP5_POL                          0x0400  /* GP5_POL */
#define WM8996_GP5_POL_MASK                     0x0400  /* GP5_POL */
#define WM8996_GP5_POL_SHIFT                        10  /* GP5_POL */
#define WM8996_GP5_POL_WIDTH                         1  /* GP5_POL */
#define WM8996_GP5_OP_CFG                       0x0200  /* GP5_OP_CFG */
#define WM8996_GP5_OP_CFG_MASK                  0x0200  /* GP5_OP_CFG */
#define WM8996_GP5_OP_CFG_SHIFT                      9  /* GP5_OP_CFG */
#define WM8996_GP5_OP_CFG_WIDTH                      1  /* GP5_OP_CFG */
#define WM8996_GP5_DB                           0x0100  /* GP5_DB */
#define WM8996_GP5_DB_MASK                      0x0100  /* GP5_DB */
#define WM8996_GP5_DB_SHIFT                          8  /* GP5_DB */
#define WM8996_GP5_DB_WIDTH                          1  /* GP5_DB */
#define WM8996_GP5_LVL                          0x0040  /* GP5_LVL */
#define WM8996_GP5_LVL_MASK                     0x0040  /* GP5_LVL */
#define WM8996_GP5_LVL_SHIFT                         6  /* GP5_LVL */
#define WM8996_GP5_LVL_WIDTH                         1  /* GP5_LVL */
#define WM8996_GP5_FN_MASK                      0x000F  /* GP5_FN - [3:0] */
#define WM8996_GP5_FN_SHIFT                          0  /* GP5_FN - [3:0] */
#define WM8996_GP5_FN_WIDTH                          4  /* GP5_FN - [3:0] */

/*
 * R1824 (0x720) - Pull Control (1)
 */
#define WM8996_DMICDAT2_PD                      0x1000  /* DMICDAT2_PD */
#define WM8996_DMICDAT2_PD_MASK                 0x1000  /* DMICDAT2_PD */
#define WM8996_DMICDAT2_PD_SHIFT                    12  /* DMICDAT2_PD */
#define WM8996_DMICDAT2_PD_WIDTH                     1  /* DMICDAT2_PD */
#define WM8996_DMICDAT1_PD                      0x0400  /* DMICDAT1_PD */
#define WM8996_DMICDAT1_PD_MASK                 0x0400  /* DMICDAT1_PD */
#define WM8996_DMICDAT1_PD_SHIFT                    10  /* DMICDAT1_PD */
#define WM8996_DMICDAT1_PD_WIDTH                     1  /* DMICDAT1_PD */
#define WM8996_MCLK2_PU                         0x0200  /* MCLK2_PU */
#define WM8996_MCLK2_PU_MASK                    0x0200  /* MCLK2_PU */
#define WM8996_MCLK2_PU_SHIFT                        9  /* MCLK2_PU */
#define WM8996_MCLK2_PU_WIDTH                        1  /* MCLK2_PU */
#define WM8996_MCLK2_PD                         0x0100  /* MCLK2_PD */
#define WM8996_MCLK2_PD_MASK                    0x0100  /* MCLK2_PD */
#define WM8996_MCLK2_PD_SHIFT                        8  /* MCLK2_PD */
#define WM8996_MCLK2_PD_WIDTH                        1  /* MCLK2_PD */
#define WM8996_MCLK1_PU                         0x0080  /* MCLK1_PU */
#define WM8996_MCLK1_PU_MASK                    0x0080  /* MCLK1_PU */
#define WM8996_MCLK1_PU_SHIFT                        7  /* MCLK1_PU */
#define WM8996_MCLK1_PU_WIDTH                        1  /* MCLK1_PU */
#define WM8996_MCLK1_PD                         0x0040  /* MCLK1_PD */
#define WM8996_MCLK1_PD_MASK                    0x0040  /* MCLK1_PD */
#define WM8996_MCLK1_PD_SHIFT                        6  /* MCLK1_PD */
#define WM8996_MCLK1_PD_WIDTH                        1  /* MCLK1_PD */
#define WM8996_DACDAT1_PU                       0x0020  /* DACDAT1_PU */
#define WM8996_DACDAT1_PU_MASK                  0x0020  /* DACDAT1_PU */
#define WM8996_DACDAT1_PU_SHIFT                      5  /* DACDAT1_PU */
#define WM8996_DACDAT1_PU_WIDTH                      1  /* DACDAT1_PU */
#define WM8996_DACDAT1_PD                       0x0010  /* DACDAT1_PD */
#define WM8996_DACDAT1_PD_MASK                  0x0010  /* DACDAT1_PD */
#define WM8996_DACDAT1_PD_SHIFT                      4  /* DACDAT1_PD */
#define WM8996_DACDAT1_PD_WIDTH                      1  /* DACDAT1_PD */
#define WM8996_DACLRCLK1_PU                     0x0008  /* DACLRCLK1_PU */
#define WM8996_DACLRCLK1_PU_MASK                0x0008  /* DACLRCLK1_PU */
#define WM8996_DACLRCLK1_PU_SHIFT                    3  /* DACLRCLK1_PU */
#define WM8996_DACLRCLK1_PU_WIDTH                    1  /* DACLRCLK1_PU */
#define WM8996_DACLRCLK1_PD                     0x0004  /* DACLRCLK1_PD */
#define WM8996_DACLRCLK1_PD_MASK                0x0004  /* DACLRCLK1_PD */
#define WM8996_DACLRCLK1_PD_SHIFT                    2  /* DACLRCLK1_PD */
#define WM8996_DACLRCLK1_PD_WIDTH                    1  /* DACLRCLK1_PD */
#define WM8996_BCLK1_PU                         0x0002  /* BCLK1_PU */
#define WM8996_BCLK1_PU_MASK                    0x0002  /* BCLK1_PU */
#define WM8996_BCLK1_PU_SHIFT                        1  /* BCLK1_PU */
#define WM8996_BCLK1_PU_WIDTH                        1  /* BCLK1_PU */
#define WM8996_BCLK1_PD                         0x0001  /* BCLK1_PD */
#define WM8996_BCLK1_PD_MASK                    0x0001  /* BCLK1_PD */
#define WM8996_BCLK1_PD_SHIFT                        0  /* BCLK1_PD */
#define WM8996_BCLK1_PD_WIDTH                        1  /* BCLK1_PD */

/*
 * R1825 (0x721) - Pull Control (2)
 */
#define WM8996_LDO1ENA_PD                       0x0100  /* LDO1ENA_PD */
#define WM8996_LDO1ENA_PD_MASK                  0x0100  /* LDO1ENA_PD */
#define WM8996_LDO1ENA_PD_SHIFT                      8  /* LDO1ENA_PD */
#define WM8996_LDO1ENA_PD_WIDTH                      1  /* LDO1ENA_PD */
#define WM8996_ADDR_PD                          0x0040  /* ADDR_PD */
#define WM8996_ADDR_PD_MASK                     0x0040  /* ADDR_PD */
#define WM8996_ADDR_PD_SHIFT                         6  /* ADDR_PD */
#define WM8996_ADDR_PD_WIDTH                         1  /* ADDR_PD */
#define WM8996_DACDAT2_PU                       0x0020  /* DACDAT2_PU */
#define WM8996_DACDAT2_PU_MASK                  0x0020  /* DACDAT2_PU */
#define WM8996_DACDAT2_PU_SHIFT                      5  /* DACDAT2_PU */
#define WM8996_DACDAT2_PU_WIDTH                      1  /* DACDAT2_PU */
#define WM8996_DACDAT2_PD                       0x0010  /* DACDAT2_PD */
#define WM8996_DACDAT2_PD_MASK                  0x0010  /* DACDAT2_PD */
#define WM8996_DACDAT2_PD_SHIFT                      4  /* DACDAT2_PD */
#define WM8996_DACDAT2_PD_WIDTH                      1  /* DACDAT2_PD */
#define WM8996_DACLRCLK2_PU                     0x0008  /* DACLRCLK2_PU */
#define WM8996_DACLRCLK2_PU_MASK                0x0008  /* DACLRCLK2_PU */
#define WM8996_DACLRCLK2_PU_SHIFT                    3  /* DACLRCLK2_PU */
#define WM8996_DACLRCLK2_PU_WIDTH                    1  /* DACLRCLK2_PU */
#define WM8996_DACLRCLK2_PD                     0x0004  /* DACLRCLK2_PD */
#define WM8996_DACLRCLK2_PD_MASK                0x0004  /* DACLRCLK2_PD */
#define WM8996_DACLRCLK2_PD_SHIFT                    2  /* DACLRCLK2_PD */
#define WM8996_DACLRCLK2_PD_WIDTH                    1  /* DACLRCLK2_PD */
#define WM8996_BCLK2_PU                         0x0002  /* BCLK2_PU */
#define WM8996_BCLK2_PU_MASK                    0x0002  /* BCLK2_PU */
#define WM8996_BCLK2_PU_SHIFT                        1  /* BCLK2_PU */
#define WM8996_BCLK2_PU_WIDTH                        1  /* BCLK2_PU */
#define WM8996_BCLK2_PD                         0x0001  /* BCLK2_PD */
#define WM8996_BCLK2_PD_MASK                    0x0001  /* BCLK2_PD */
#define WM8996_BCLK2_PD_SHIFT                        0  /* BCLK2_PD */
#define WM8996_BCLK2_PD_WIDTH                        1  /* BCLK2_PD */

/*
 * R1840 (0x730) - Interrupt Status 1
 */
#define WM8996_GP5_EINT                         0x0010  /* GP5_EINT */
#define WM8996_GP5_EINT_MASK                    0x0010  /* GP5_EINT */
#define WM8996_GP5_EINT_SHIFT                        4  /* GP5_EINT */
#define WM8996_GP5_EINT_WIDTH                        1  /* GP5_EINT */
#define WM8996_GP4_EINT                         0x0008  /* GP4_EINT */
#define WM8996_GP4_EINT_MASK                    0x0008  /* GP4_EINT */
#define WM8996_GP4_EINT_SHIFT                        3  /* GP4_EINT */
#define WM8996_GP4_EINT_WIDTH                        1  /* GP4_EINT */
#define WM8996_GP3_EINT                         0x0004  /* GP3_EINT */
#define WM8996_GP3_EINT_MASK                    0x0004  /* GP3_EINT */
#define WM8996_GP3_EINT_SHIFT                        2  /* GP3_EINT */
#define WM8996_GP3_EINT_WIDTH                        1  /* GP3_EINT */
#define WM8996_GP2_EINT                         0x0002  /* GP2_EINT */
#define WM8996_GP2_EINT_MASK                    0x0002  /* GP2_EINT */
#define WM8996_GP2_EINT_SHIFT                        1  /* GP2_EINT */
#define WM8996_GP2_EINT_WIDTH                        1  /* GP2_EINT */
#define WM8996_GP1_EINT                         0x0001  /* GP1_EINT */
#define WM8996_GP1_EINT_MASK                    0x0001  /* GP1_EINT */
#define WM8996_GP1_EINT_SHIFT                        0  /* GP1_EINT */
#define WM8996_GP1_EINT_WIDTH                        1  /* GP1_EINT */

/*
 * R1841 (0x731) - Interrupt Status 2
 */
#define WM8996_DCS_DONE_23_EINT                 0x1000  /* DCS_DONE_23_EINT */
#define WM8996_DCS_DONE_23_EINT_MASK            0x1000  /* DCS_DONE_23_EINT */
#define WM8996_DCS_DONE_23_EINT_SHIFT               12  /* DCS_DONE_23_EINT */
#define WM8996_DCS_DONE_23_EINT_WIDTH                1  /* DCS_DONE_23_EINT */
#define WM8996_DCS_DONE_01_EINT                 0x0800  /* DCS_DONE_01_EINT */
#define WM8996_DCS_DONE_01_EINT_MASK            0x0800  /* DCS_DONE_01_EINT */
#define WM8996_DCS_DONE_01_EINT_SHIFT               11  /* DCS_DONE_01_EINT */
#define WM8996_DCS_DONE_01_EINT_WIDTH                1  /* DCS_DONE_01_EINT */
#define WM8996_WSEQ_DONE_EINT                   0x0400  /* WSEQ_DONE_EINT */
#define WM8996_WSEQ_DONE_EINT_MASK              0x0400  /* WSEQ_DONE_EINT */
#define WM8996_WSEQ_DONE_EINT_SHIFT                 10  /* WSEQ_DONE_EINT */
#define WM8996_WSEQ_DONE_EINT_WIDTH                  1  /* WSEQ_DONE_EINT */
#define WM8996_FIFOS_ERR_EINT                   0x0200  /* FIFOS_ERR_EINT */
#define WM8996_FIFOS_ERR_EINT_MASK              0x0200  /* FIFOS_ERR_EINT */
#define WM8996_FIFOS_ERR_EINT_SHIFT                  9  /* FIFOS_ERR_EINT */
#define WM8996_FIFOS_ERR_EINT_WIDTH                  1  /* FIFOS_ERR_EINT */
#define WM8996_DSP2DRC_SIG_DET_EINT             0x0080  /* DSP2DRC_SIG_DET_EINT */
#define WM8996_DSP2DRC_SIG_DET_EINT_MASK        0x0080  /* DSP2DRC_SIG_DET_EINT */
#define WM8996_DSP2DRC_SIG_DET_EINT_SHIFT            7  /* DSP2DRC_SIG_DET_EINT */
#define WM8996_DSP2DRC_SIG_DET_EINT_WIDTH            1  /* DSP2DRC_SIG_DET_EINT */
#define WM8996_DSP1DRC_SIG_DET_EINT             0x0040  /* DSP1DRC_SIG_DET_EINT */
#define WM8996_DSP1DRC_SIG_DET_EINT_MASK        0x0040  /* DSP1DRC_SIG_DET_EINT */
#define WM8996_DSP1DRC_SIG_DET_EINT_SHIFT            6  /* DSP1DRC_SIG_DET_EINT */
#define WM8996_DSP1DRC_SIG_DET_EINT_WIDTH            1  /* DSP1DRC_SIG_DET_EINT */
#define WM8996_FLL_SW_CLK_DONE_EINT             0x0008  /* FLL_SW_CLK_DONE_EINT */
#define WM8996_FLL_SW_CLK_DONE_EINT_MASK        0x0008  /* FLL_SW_CLK_DONE_EINT */
#define WM8996_FLL_SW_CLK_DONE_EINT_SHIFT            3  /* FLL_SW_CLK_DONE_EINT */
#define WM8996_FLL_SW_CLK_DONE_EINT_WIDTH            1  /* FLL_SW_CLK_DONE_EINT */
#define WM8996_FLL_LOCK_EINT                    0x0004  /* FLL_LOCK_EINT */
#define WM8996_FLL_LOCK_EINT_MASK               0x0004  /* FLL_LOCK_EINT */
#define WM8996_FLL_LOCK_EINT_SHIFT                   2  /* FLL_LOCK_EINT */
#define WM8996_FLL_LOCK_EINT_WIDTH                   1  /* FLL_LOCK_EINT */
#define WM8996_HP_DONE_EINT                     0x0002  /* HP_DONE_EINT */
#define WM8996_HP_DONE_EINT_MASK                0x0002  /* HP_DONE_EINT */
#define WM8996_HP_DONE_EINT_SHIFT                    1  /* HP_DONE_EINT */
#define WM8996_HP_DONE_EINT_WIDTH                    1  /* HP_DONE_EINT */
#define WM8996_MICD_EINT                        0x0001  /* MICD_EINT */
#define WM8996_MICD_EINT_MASK                   0x0001  /* MICD_EINT */
#define WM8996_MICD_EINT_SHIFT                       0  /* MICD_EINT */
#define WM8996_MICD_EINT_WIDTH                       1  /* MICD_EINT */

/*
 * R1842 (0x732) - Interrupt Raw Status 2
 */
#define WM8996_DCS_DONE_23_STS                  0x1000  /* DCS_DONE_23_STS */
#define WM8996_DCS_DONE_23_STS_MASK             0x1000  /* DCS_DONE_23_STS */
#define WM8996_DCS_DONE_23_STS_SHIFT                12  /* DCS_DONE_23_STS */
#define WM8996_DCS_DONE_23_STS_WIDTH                 1  /* DCS_DONE_23_STS */
#define WM8996_DCS_DONE_01_STS                  0x0800  /* DCS_DONE_01_STS */
#define WM8996_DCS_DONE_01_STS_MASK             0x0800  /* DCS_DONE_01_STS */
#define WM8996_DCS_DONE_01_STS_SHIFT                11  /* DCS_DONE_01_STS */
#define WM8996_DCS_DONE_01_STS_WIDTH                 1  /* DCS_DONE_01_STS */
#define WM8996_WSEQ_DONE_STS                    0x0400  /* WSEQ_DONE_STS */
#define WM8996_WSEQ_DONE_STS_MASK               0x0400  /* WSEQ_DONE_STS */
#define WM8996_WSEQ_DONE_STS_SHIFT                  10  /* WSEQ_DONE_STS */
#define WM8996_WSEQ_DONE_STS_WIDTH                   1  /* WSEQ_DONE_STS */
#define WM8996_FIFOS_ERR_STS                    0x0200  /* FIFOS_ERR_STS */
#define WM8996_FIFOS_ERR_STS_MASK               0x0200  /* FIFOS_ERR_STS */
#define WM8996_FIFOS_ERR_STS_SHIFT                   9  /* FIFOS_ERR_STS */
#define WM8996_FIFOS_ERR_STS_WIDTH                   1  /* FIFOS_ERR_STS */
#define WM8996_DSP2DRC_SIG_DET_STS              0x0080  /* DSP2DRC_SIG_DET_STS */
#define WM8996_DSP2DRC_SIG_DET_STS_MASK         0x0080  /* DSP2DRC_SIG_DET_STS */
#define WM8996_DSP2DRC_SIG_DET_STS_SHIFT             7  /* DSP2DRC_SIG_DET_STS */
#define WM8996_DSP2DRC_SIG_DET_STS_WIDTH             1  /* DSP2DRC_SIG_DET_STS */
#define WM8996_DSP1DRC_SIG_DET_STS              0x0040  /* DSP1DRC_SIG_DET_STS */
#define WM8996_DSP1DRC_SIG_DET_STS_MASK         0x0040  /* DSP1DRC_SIG_DET_STS */
#define WM8996_DSP1DRC_SIG_DET_STS_SHIFT             6  /* DSP1DRC_SIG_DET_STS */
#define WM8996_DSP1DRC_SIG_DET_STS_WIDTH             1  /* DSP1DRC_SIG_DET_STS */
#define WM8996_FLL_LOCK_STS                     0x0004  /* FLL_LOCK_STS */
#define WM8996_FLL_LOCK_STS_MASK                0x0004  /* FLL_LOCK_STS */
#define WM8996_FLL_LOCK_STS_SHIFT                    2  /* FLL_LOCK_STS */
#define WM8996_FLL_LOCK_STS_WIDTH                    1  /* FLL_LOCK_STS */

/*
 * R1848 (0x738) - Interrupt Status 1 Mask
 */
#define WM8996_IM_GP5_EINT                      0x0010  /* IM_GP5_EINT */
#define WM8996_IM_GP5_EINT_MASK                 0x0010  /* IM_GP5_EINT */
#define WM8996_IM_GP5_EINT_SHIFT                     4  /* IM_GP5_EINT */
#define WM8996_IM_GP5_EINT_WIDTH                     1  /* IM_GP5_EINT */
#define WM8996_IM_GP4_EINT                      0x0008  /* IM_GP4_EINT */
#define WM8996_IM_GP4_EINT_MASK                 0x0008  /* IM_GP4_EINT */
#define WM8996_IM_GP4_EINT_SHIFT                     3  /* IM_GP4_EINT */
#define WM8996_IM_GP4_EINT_WIDTH                     1  /* IM_GP4_EINT */
#define WM8996_IM_GP3_EINT                      0x0004  /* IM_GP3_EINT */
#define WM8996_IM_GP3_EINT_MASK                 0x0004  /* IM_GP3_EINT */
#define WM8996_IM_GP3_EINT_SHIFT                     2  /* IM_GP3_EINT */
#define WM8996_IM_GP3_EINT_WIDTH                     1  /* IM_GP3_EINT */
#define WM8996_IM_GP2_EINT                      0x0002  /* IM_GP2_EINT */
#define WM8996_IM_GP2_EINT_MASK                 0x0002  /* IM_GP2_EINT */
#define WM8996_IM_GP2_EINT_SHIFT                     1  /* IM_GP2_EINT */
#define WM8996_IM_GP2_EINT_WIDTH                     1  /* IM_GP2_EINT */
#define WM8996_IM_GP1_EINT                      0x0001  /* IM_GP1_EINT */
#define WM8996_IM_GP1_EINT_MASK                 0x0001  /* IM_GP1_EINT */
#define WM8996_IM_GP1_EINT_SHIFT                     0  /* IM_GP1_EINT */
#define WM8996_IM_GP1_EINT_WIDTH                     1  /* IM_GP1_EINT */

/*
 * R1849 (0x739) - Interrupt Status 2 Mask
 */
#define WM8996_IM_DCS_DONE_23_EINT              0x1000  /* IM_DCS_DONE_23_EINT */
#define WM8996_IM_DCS_DONE_23_EINT_MASK         0x1000  /* IM_DCS_DONE_23_EINT */
#define WM8996_IM_DCS_DONE_23_EINT_SHIFT            12  /* IM_DCS_DONE_23_EINT */
#define WM8996_IM_DCS_DONE_23_EINT_WIDTH             1  /* IM_DCS_DONE_23_EINT */
#define WM8996_IM_DCS_DONE_01_EINT              0x0800  /* IM_DCS_DONE_01_EINT */
#define WM8996_IM_DCS_DONE_01_EINT_MASK         0x0800  /* IM_DCS_DONE_01_EINT */
#define WM8996_IM_DCS_DONE_01_EINT_SHIFT            11  /* IM_DCS_DONE_01_EINT */
#define WM8996_IM_DCS_DONE_01_EINT_WIDTH             1  /* IM_DCS_DONE_01_EINT */
#define WM8996_IM_WSEQ_DONE_EINT                0x0400  /* IM_WSEQ_DONE_EINT */
#define WM8996_IM_WSEQ_DONE_EINT_MASK           0x0400  /* IM_WSEQ_DONE_EINT */
#define WM8996_IM_WSEQ_DONE_EINT_SHIFT              10  /* IM_WSEQ_DONE_EINT */
#define WM8996_IM_WSEQ_DONE_EINT_WIDTH               1  /* IM_WSEQ_DONE_EINT */
#define WM8996_IM_FIFOS_ERR_EINT                0x0200  /* IM_FIFOS_ERR_EINT */
#define WM8996_IM_FIFOS_ERR_EINT_MASK           0x0200  /* IM_FIFOS_ERR_EINT */
#define WM8996_IM_FIFOS_ERR_EINT_SHIFT               9  /* IM_FIFOS_ERR_EINT */
#define WM8996_IM_FIFOS_ERR_EINT_WIDTH               1  /* IM_FIFOS_ERR_EINT */
#define WM8996_IM_DSP2DRC_SIG_DET_EINT          0x0080  /* IM_DSP2DRC_SIG_DET_EINT */
#define WM8996_IM_DSP2DRC_SIG_DET_EINT_MASK     0x0080  /* IM_DSP2DRC_SIG_DET_EINT */
#define WM8996_IM_DSP2DRC_SIG_DET_EINT_SHIFT         7  /* IM_DSP2DRC_SIG_DET_EINT */
#define WM8996_IM_DSP2DRC_SIG_DET_EINT_WIDTH         1  /* IM_DSP2DRC_SIG_DET_EINT */
#define WM8996_IM_DSP1DRC_SIG_DET_EINT          0x0040  /* IM_DSP1DRC_SIG_DET_EINT */
#define WM8996_IM_DSP1DRC_SIG_DET_EINT_MASK     0x0040  /* IM_DSP1DRC_SIG_DET_EINT */
#define WM8996_IM_DSP1DRC_SIG_DET_EINT_SHIFT         6  /* IM_DSP1DRC_SIG_DET_EINT */
#define WM8996_IM_DSP1DRC_SIG_DET_EINT_WIDTH         1  /* IM_DSP1DRC_SIG_DET_EINT */
#define WM8996_IM_FLL_SW_CLK_DONE_EINT          0x0008  /* IM_FLL_SW_CLK_DONE_EINT */
#define WM8996_IM_FLL_SW_CLK_DONE_EINT_MASK     0x0008  /* IM_FLL_SW_CLK_DONE_EINT */
#define WM8996_IM_FLL_SW_CLK_DONE_EINT_SHIFT         3  /* IM_FLL_SW_CLK_DONE_EINT */
#define WM8996_IM_FLL_SW_CLK_DONE_EINT_WIDTH         1  /* IM_FLL_SW_CLK_DONE_EINT */
#define WM8996_IM_FLL_LOCK_EINT                 0x0004  /* IM_FLL_LOCK_EINT */
#define WM8996_IM_FLL_LOCK_EINT_MASK            0x0004  /* IM_FLL_LOCK_EINT */
#define WM8996_IM_FLL_LOCK_EINT_SHIFT                2  /* IM_FLL_LOCK_EINT */
#define WM8996_IM_FLL_LOCK_EINT_WIDTH                1  /* IM_FLL_LOCK_EINT */
#define WM8996_IM_HP_DONE_EINT                  0x0002  /* IM_HP_DONE_EINT */
#define WM8996_IM_HP_DONE_EINT_MASK             0x0002  /* IM_HP_DONE_EINT */
#define WM8996_IM_HP_DONE_EINT_SHIFT                 1  /* IM_HP_DONE_EINT */
#define WM8996_IM_HP_DONE_EINT_WIDTH                 1  /* IM_HP_DONE_EINT */
#define WM8996_IM_MICD_EINT                     0x0001  /* IM_MICD_EINT */
#define WM8996_IM_MICD_EINT_MASK                0x0001  /* IM_MICD_EINT */
#define WM8996_IM_MICD_EINT_SHIFT                    0  /* IM_MICD_EINT */
#define WM8996_IM_MICD_EINT_WIDTH                    1  /* IM_MICD_EINT */

/*
 * R1856 (0x740) - Interrupt Control
 */
#define WM8996_IM_IRQ                           0x0001  /* IM_IRQ */
#define WM8996_IM_IRQ_MASK                      0x0001  /* IM_IRQ */
#define WM8996_IM_IRQ_SHIFT                          0  /* IM_IRQ */
#define WM8996_IM_IRQ_WIDTH                          1  /* IM_IRQ */

/*
 * R2048 (0x800) - Left PDM Speaker
 */
#define WM8996_SPKL_ENA                         0x0010  /* SPKL_ENA */
#define WM8996_SPKL_ENA_MASK                    0x0010  /* SPKL_ENA */
#define WM8996_SPKL_ENA_SHIFT                        4  /* SPKL_ENA */
#define WM8996_SPKL_ENA_WIDTH                        1  /* SPKL_ENA */
#define WM8996_SPKL_MUTE                        0x0008  /* SPKL_MUTE */
#define WM8996_SPKL_MUTE_MASK                   0x0008  /* SPKL_MUTE */
#define WM8996_SPKL_MUTE_SHIFT                       3  /* SPKL_MUTE */
#define WM8996_SPKL_MUTE_WIDTH                       1  /* SPKL_MUTE */
#define WM8996_SPKL_MUTE_ZC                     0x0004  /* SPKL_MUTE_ZC */
#define WM8996_SPKL_MUTE_ZC_MASK                0x0004  /* SPKL_MUTE_ZC */
#define WM8996_SPKL_MUTE_ZC_SHIFT                    2  /* SPKL_MUTE_ZC */
#define WM8996_SPKL_MUTE_ZC_WIDTH                    1  /* SPKL_MUTE_ZC */
#define WM8996_SPKL_SRC_MASK                    0x0003  /* SPKL_SRC - [1:0] */
#define WM8996_SPKL_SRC_SHIFT                        0  /* SPKL_SRC - [1:0] */
#define WM8996_SPKL_SRC_WIDTH                        2  /* SPKL_SRC - [1:0] */

/*
 * R2049 (0x801) - Right PDM Speaker
 */
#define WM8996_SPKR_ENA                         0x0010  /* SPKR_ENA */
#define WM8996_SPKR_ENA_MASK                    0x0010  /* SPKR_ENA */
#define WM8996_SPKR_ENA_SHIFT                        4  /* SPKR_ENA */
#define WM8996_SPKR_ENA_WIDTH                        1  /* SPKR_ENA */
#define WM8996_SPKR_MUTE                        0x0008  /* SPKR_MUTE */
#define WM8996_SPKR_MUTE_MASK                   0x0008  /* SPKR_MUTE */
#define WM8996_SPKR_MUTE_SHIFT                       3  /* SPKR_MUTE */
#define WM8996_SPKR_MUTE_WIDTH                       1  /* SPKR_MUTE */
#define WM8996_SPKR_MUTE_ZC                     0x0004  /* SPKR_MUTE_ZC */
#define WM8996_SPKR_MUTE_ZC_MASK                0x0004  /* SPKR_MUTE_ZC */
#define WM8996_SPKR_MUTE_ZC_SHIFT                    2  /* SPKR_MUTE_ZC */
#define WM8996_SPKR_MUTE_ZC_WIDTH                    1  /* SPKR_MUTE_ZC */
#define WM8996_SPKR_SRC_MASK                    0x0003  /* SPKR_SRC - [1:0] */
#define WM8996_SPKR_SRC_SHIFT                        0  /* SPKR_SRC - [1:0] */
#define WM8996_SPKR_SRC_WIDTH                        2  /* SPKR_SRC - [1:0] */

/*
 * R2050 (0x802) - PDM Speaker Mute Sequence
 */
#define WM8996_SPK_MUTE_ENDIAN                  0x0100  /* SPK_MUTE_ENDIAN */
#define WM8996_SPK_MUTE_ENDIAN_MASK             0x0100  /* SPK_MUTE_ENDIAN */
#define WM8996_SPK_MUTE_ENDIAN_SHIFT                 8  /* SPK_MUTE_ENDIAN */
#define WM8996_SPK_MUTE_ENDIAN_WIDTH                 1  /* SPK_MUTE_ENDIAN */
#define WM8996_SPK_MUTE_SEQ1_MASK               0x00FF  /* SPK_MUTE_SEQ1 - [7:0] */
#define WM8996_SPK_MUTE_SEQ1_SHIFT                   0  /* SPK_MUTE_SEQ1 - [7:0] */
#define WM8996_SPK_MUTE_SEQ1_WIDTH                   8  /* SPK_MUTE_SEQ1 - [7:0] */

/*
 * R2051 (0x803) - PDM Speaker Volume
 */
#define WM8996_SPKR_VOL_MASK                    0x00F0  /* SPKR_VOL - [7:4] */
#define WM8996_SPKR_VOL_SHIFT                        4  /* SPKR_VOL - [7:4] */
#define WM8996_SPKR_VOL_WIDTH                        4  /* SPKR_VOL - [7:4] */
#define WM8996_SPKL_VOL_MASK                    0x000F  /* SPKL_VOL - [3:0] */
#define WM8996_SPKL_VOL_SHIFT                        0  /* SPKL_VOL - [3:0] */
#define WM8996_SPKL_VOL_WIDTH                        4  /* SPKL_VOL - [3:0] */

#endif
