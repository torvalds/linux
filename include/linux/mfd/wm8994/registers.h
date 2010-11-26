/*
 * include/linux/mfd/wm8994/registers.h -- Register definitions for WM8994
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __MFD_WM8994_REGISTERS_H__
#define __MFD_WM8994_REGISTERS_H__

/*
 * Register values.
 */
#define WM8994_SOFTWARE_RESET                   0x00
#define WM8994_POWER_MANAGEMENT_1               0x01
#define WM8994_POWER_MANAGEMENT_2               0x02
#define WM8994_POWER_MANAGEMENT_3               0x03
#define WM8994_POWER_MANAGEMENT_4               0x04
#define WM8994_POWER_MANAGEMENT_5               0x05
#define WM8994_POWER_MANAGEMENT_6               0x06
#define WM8994_INPUT_MIXER_1                    0x15
#define WM8994_LEFT_LINE_INPUT_1_2_VOLUME       0x18
#define WM8994_LEFT_LINE_INPUT_3_4_VOLUME       0x19
#define WM8994_RIGHT_LINE_INPUT_1_2_VOLUME      0x1A
#define WM8994_RIGHT_LINE_INPUT_3_4_VOLUME      0x1B
#define WM8994_LEFT_OUTPUT_VOLUME               0x1C
#define WM8994_RIGHT_OUTPUT_VOLUME              0x1D
#define WM8994_LINE_OUTPUTS_VOLUME              0x1E
#define WM8994_HPOUT2_VOLUME                    0x1F
#define WM8994_LEFT_OPGA_VOLUME                 0x20
#define WM8994_RIGHT_OPGA_VOLUME                0x21
#define WM8994_SPKMIXL_ATTENUATION              0x22
#define WM8994_SPKMIXR_ATTENUATION              0x23
#define WM8994_SPKOUT_MIXERS                    0x24
#define WM8994_CLASSD                           0x25
#define WM8994_SPEAKER_VOLUME_LEFT              0x26
#define WM8994_SPEAKER_VOLUME_RIGHT             0x27
#define WM8994_INPUT_MIXER_2                    0x28
#define WM8994_INPUT_MIXER_3                    0x29
#define WM8994_INPUT_MIXER_4                    0x2A
#define WM8994_INPUT_MIXER_5                    0x2B
#define WM8994_INPUT_MIXER_6                    0x2C
#define WM8994_OUTPUT_MIXER_1                   0x2D
#define WM8994_OUTPUT_MIXER_2                   0x2E
#define WM8994_OUTPUT_MIXER_3                   0x2F
#define WM8994_OUTPUT_MIXER_4                   0x30
#define WM8994_OUTPUT_MIXER_5                   0x31
#define WM8994_OUTPUT_MIXER_6                   0x32
#define WM8994_HPOUT2_MIXER                     0x33
#define WM8994_LINE_MIXER_1                     0x34
#define WM8994_LINE_MIXER_2                     0x35
#define WM8994_SPEAKER_MIXER                    0x36
#define WM8994_ADDITIONAL_CONTROL               0x37
#define WM8994_ANTIPOP_1                        0x38
#define WM8994_ANTIPOP_2                        0x39
#define WM8994_MICBIAS                          0x3A
#define WM8994_LDO_1                            0x3B
#define WM8994_LDO_2                            0x3C
#define WM8994_CHARGE_PUMP_1                    0x4C
#define WM8994_CLASS_W_1                        0x51
#define WM8994_DC_SERVO_1                       0x54
#define WM8994_DC_SERVO_2                       0x55
#define WM8994_DC_SERVO_4                       0x57
#define WM8994_DC_SERVO_READBACK                0x58
#define WM8994_ANALOGUE_HP_1                    0x60
#define WM8958_MIC_DETECT_1                     0xD0
#define WM8958_MIC_DETECT_2                     0xD1
#define WM8958_MIC_DETECT_3                     0xD2
#define WM8994_CHIP_REVISION                    0x100
#define WM8994_CONTROL_INTERFACE                0x101
#define WM8994_WRITE_SEQUENCER_CTRL_1           0x110
#define WM8994_WRITE_SEQUENCER_CTRL_2           0x111
#define WM8994_AIF1_CLOCKING_1                  0x200
#define WM8994_AIF1_CLOCKING_2                  0x201
#define WM8994_AIF2_CLOCKING_1                  0x204
#define WM8994_AIF2_CLOCKING_2                  0x205
#define WM8994_CLOCKING_1                       0x208
#define WM8994_CLOCKING_2                       0x209
#define WM8994_AIF1_RATE                        0x210
#define WM8994_AIF2_RATE                        0x211
#define WM8994_RATE_STATUS                      0x212
#define WM8994_FLL1_CONTROL_1                   0x220
#define WM8994_FLL1_CONTROL_2                   0x221
#define WM8994_FLL1_CONTROL_3                   0x222
#define WM8994_FLL1_CONTROL_4                   0x223
#define WM8994_FLL1_CONTROL_5                   0x224
#define WM8994_FLL2_CONTROL_1                   0x240
#define WM8994_FLL2_CONTROL_2                   0x241
#define WM8994_FLL2_CONTROL_3                   0x242
#define WM8994_FLL2_CONTROL_4                   0x243
#define WM8994_FLL2_CONTROL_5                   0x244
#define WM8994_AIF1_CONTROL_1                   0x300
#define WM8994_AIF1_CONTROL_2                   0x301
#define WM8994_AIF1_MASTER_SLAVE                0x302
#define WM8994_AIF1_BCLK                        0x303
#define WM8994_AIF1ADC_LRCLK                    0x304
#define WM8994_AIF1DAC_LRCLK                    0x305
#define WM8994_AIF1DAC_DATA                     0x306
#define WM8994_AIF1ADC_DATA                     0x307
#define WM8994_AIF2_CONTROL_1                   0x310
#define WM8994_AIF2_CONTROL_2                   0x311
#define WM8994_AIF2_MASTER_SLAVE                0x312
#define WM8994_AIF2_BCLK                        0x313
#define WM8994_AIF2ADC_LRCLK                    0x314
#define WM8994_AIF2DAC_LRCLK                    0x315
#define WM8994_AIF2DAC_DATA                     0x316
#define WM8994_AIF2ADC_DATA                     0x317
#define WM8958_AIF3_CONTROL_1                   0x320
#define WM8958_AIF3_CONTROL_2                   0x321
#define WM8958_AIF3DAC_DATA                     0x322
#define WM8958_AIF3ADC_DATA                     0x323
#define WM8994_AIF1_ADC1_LEFT_VOLUME            0x400
#define WM8994_AIF1_ADC1_RIGHT_VOLUME           0x401
#define WM8994_AIF1_DAC1_LEFT_VOLUME            0x402
#define WM8994_AIF1_DAC1_RIGHT_VOLUME           0x403
#define WM8994_AIF1_ADC2_LEFT_VOLUME            0x404
#define WM8994_AIF1_ADC2_RIGHT_VOLUME           0x405
#define WM8994_AIF1_DAC2_LEFT_VOLUME            0x406
#define WM8994_AIF1_DAC2_RIGHT_VOLUME           0x407
#define WM8994_AIF1_ADC1_FILTERS                0x410
#define WM8994_AIF1_ADC2_FILTERS                0x411
#define WM8994_AIF1_DAC1_FILTERS_1              0x420
#define WM8994_AIF1_DAC1_FILTERS_2              0x421
#define WM8994_AIF1_DAC2_FILTERS_1              0x422
#define WM8994_AIF1_DAC2_FILTERS_2              0x423
#define WM8994_AIF1_DRC1_1                      0x440
#define WM8994_AIF1_DRC1_2                      0x441
#define WM8994_AIF1_DRC1_3                      0x442
#define WM8994_AIF1_DRC1_4                      0x443
#define WM8994_AIF1_DRC1_5                      0x444
#define WM8994_AIF1_DRC2_1                      0x450
#define WM8994_AIF1_DRC2_2                      0x451
#define WM8994_AIF1_DRC2_3                      0x452
#define WM8994_AIF1_DRC2_4                      0x453
#define WM8994_AIF1_DRC2_5                      0x454
#define WM8994_AIF1_DAC1_EQ_GAINS_1             0x480
#define WM8994_AIF1_DAC1_EQ_GAINS_2             0x481
#define WM8994_AIF1_DAC1_EQ_BAND_1_A            0x482
#define WM8994_AIF1_DAC1_EQ_BAND_1_B            0x483
#define WM8994_AIF1_DAC1_EQ_BAND_1_PG           0x484
#define WM8994_AIF1_DAC1_EQ_BAND_2_A            0x485
#define WM8994_AIF1_DAC1_EQ_BAND_2_B            0x486
#define WM8994_AIF1_DAC1_EQ_BAND_2_C            0x487
#define WM8994_AIF1_DAC1_EQ_BAND_2_PG           0x488
#define WM8994_AIF1_DAC1_EQ_BAND_3_A            0x489
#define WM8994_AIF1_DAC1_EQ_BAND_3_B            0x48A
#define WM8994_AIF1_DAC1_EQ_BAND_3_C            0x48B
#define WM8994_AIF1_DAC1_EQ_BAND_3_PG           0x48C
#define WM8994_AIF1_DAC1_EQ_BAND_4_A            0x48D
#define WM8994_AIF1_DAC1_EQ_BAND_4_B            0x48E
#define WM8994_AIF1_DAC1_EQ_BAND_4_C            0x48F
#define WM8994_AIF1_DAC1_EQ_BAND_4_PG           0x490
#define WM8994_AIF1_DAC1_EQ_BAND_5_A            0x491
#define WM8994_AIF1_DAC1_EQ_BAND_5_B            0x492
#define WM8994_AIF1_DAC1_EQ_BAND_5_PG           0x493
#define WM8994_AIF1_DAC2_EQ_GAINS_1             0x4A0
#define WM8994_AIF1_DAC2_EQ_GAINS_2             0x4A1
#define WM8994_AIF1_DAC2_EQ_BAND_1_A            0x4A2
#define WM8994_AIF1_DAC2_EQ_BAND_1_B            0x4A3
#define WM8994_AIF1_DAC2_EQ_BAND_1_PG           0x4A4
#define WM8994_AIF1_DAC2_EQ_BAND_2_A            0x4A5
#define WM8994_AIF1_DAC2_EQ_BAND_2_B            0x4A6
#define WM8994_AIF1_DAC2_EQ_BAND_2_C            0x4A7
#define WM8994_AIF1_DAC2_EQ_BAND_2_PG           0x4A8
#define WM8994_AIF1_DAC2_EQ_BAND_3_A            0x4A9
#define WM8994_AIF1_DAC2_EQ_BAND_3_B            0x4AA
#define WM8994_AIF1_DAC2_EQ_BAND_3_C            0x4AB
#define WM8994_AIF1_DAC2_EQ_BAND_3_PG           0x4AC
#define WM8994_AIF1_DAC2_EQ_BAND_4_A            0x4AD
#define WM8994_AIF1_DAC2_EQ_BAND_4_B            0x4AE
#define WM8994_AIF1_DAC2_EQ_BAND_4_C            0x4AF
#define WM8994_AIF1_DAC2_EQ_BAND_4_PG           0x4B0
#define WM8994_AIF1_DAC2_EQ_BAND_5_A            0x4B1
#define WM8994_AIF1_DAC2_EQ_BAND_5_B            0x4B2
#define WM8994_AIF1_DAC2_EQ_BAND_5_PG           0x4B3
#define WM8994_AIF2_ADC_LEFT_VOLUME             0x500
#define WM8994_AIF2_ADC_RIGHT_VOLUME            0x501
#define WM8994_AIF2_DAC_LEFT_VOLUME             0x502
#define WM8994_AIF2_DAC_RIGHT_VOLUME            0x503
#define WM8994_AIF2_ADC_FILTERS                 0x510
#define WM8994_AIF2_DAC_FILTERS_1               0x520
#define WM8994_AIF2_DAC_FILTERS_2               0x521
#define WM8994_AIF2_DRC_1                       0x540
#define WM8994_AIF2_DRC_2                       0x541
#define WM8994_AIF2_DRC_3                       0x542
#define WM8994_AIF2_DRC_4                       0x543
#define WM8994_AIF2_DRC_5                       0x544
#define WM8994_AIF2_EQ_GAINS_1                  0x580
#define WM8994_AIF2_EQ_GAINS_2                  0x581
#define WM8994_AIF2_EQ_BAND_1_A                 0x582
#define WM8994_AIF2_EQ_BAND_1_B                 0x583
#define WM8994_AIF2_EQ_BAND_1_PG                0x584
#define WM8994_AIF2_EQ_BAND_2_A                 0x585
#define WM8994_AIF2_EQ_BAND_2_B                 0x586
#define WM8994_AIF2_EQ_BAND_2_C                 0x587
#define WM8994_AIF2_EQ_BAND_2_PG                0x588
#define WM8994_AIF2_EQ_BAND_3_A                 0x589
#define WM8994_AIF2_EQ_BAND_3_B                 0x58A
#define WM8994_AIF2_EQ_BAND_3_C                 0x58B
#define WM8994_AIF2_EQ_BAND_3_PG                0x58C
#define WM8994_AIF2_EQ_BAND_4_A                 0x58D
#define WM8994_AIF2_EQ_BAND_4_B                 0x58E
#define WM8994_AIF2_EQ_BAND_4_C                 0x58F
#define WM8994_AIF2_EQ_BAND_4_PG                0x590
#define WM8994_AIF2_EQ_BAND_5_A                 0x591
#define WM8994_AIF2_EQ_BAND_5_B                 0x592
#define WM8994_AIF2_EQ_BAND_5_PG                0x593
#define WM8994_DAC1_MIXER_VOLUMES               0x600
#define WM8994_DAC1_LEFT_MIXER_ROUTING          0x601
#define WM8994_DAC1_RIGHT_MIXER_ROUTING         0x602
#define WM8994_DAC2_MIXER_VOLUMES               0x603
#define WM8994_DAC2_LEFT_MIXER_ROUTING          0x604
#define WM8994_DAC2_RIGHT_MIXER_ROUTING         0x605
#define WM8994_AIF1_ADC1_LEFT_MIXER_ROUTING     0x606
#define WM8994_AIF1_ADC1_RIGHT_MIXER_ROUTING    0x607
#define WM8994_AIF1_ADC2_LEFT_MIXER_ROUTING     0x608
#define WM8994_AIF1_ADC2_RIGHT_MIXER_ROUTING    0x609
#define WM8994_DAC1_LEFT_VOLUME                 0x610
#define WM8994_DAC1_RIGHT_VOLUME                0x611
#define WM8994_DAC2_LEFT_VOLUME                 0x612
#define WM8994_DAC2_RIGHT_VOLUME                0x613
#define WM8994_DAC_SOFTMUTE                     0x614
#define WM8994_OVERSAMPLING                     0x620
#define WM8994_SIDETONE                         0x621
#define WM8994_GPIO_1                           0x700
#define WM8994_GPIO_2                           0x701
#define WM8994_GPIO_3                           0x702
#define WM8994_GPIO_4                           0x703
#define WM8994_GPIO_5                           0x704
#define WM8994_GPIO_6                           0x705
#define WM8994_GPIO_7                           0x706
#define WM8994_GPIO_8                           0x707
#define WM8994_GPIO_9                           0x708
#define WM8994_GPIO_10                          0x709
#define WM8994_GPIO_11                          0x70A
#define WM8994_PULL_CONTROL_1                   0x720
#define WM8994_PULL_CONTROL_2                   0x721
#define WM8994_INTERRUPT_STATUS_1               0x730
#define WM8994_INTERRUPT_STATUS_2               0x731
#define WM8994_INTERRUPT_RAW_STATUS_2           0x732
#define WM8994_INTERRUPT_STATUS_1_MASK          0x738
#define WM8994_INTERRUPT_STATUS_2_MASK          0x739
#define WM8994_INTERRUPT_CONTROL                0x740
#define WM8994_IRQ_DEBOUNCE                     0x748
#define WM8958_DSP2_PROGRAM                     0x900
#define WM8958_DSP2_CONFIG                      0x901
#define WM8958_DSP2_MAGICNUM                    0xA00
#define WM8958_DSP2_RELEASEYEAR                 0xA01
#define WM8958_DSP2_RELEASEMONTHDAY             0xA02
#define WM8958_DSP2_RELEASETIME                 0xA03
#define WM8958_DSP2_VERMAJMIN                   0xA04
#define WM8958_DSP2_VERBUILD                    0xA05
#define WM8958_DSP2_EXECCONTROL                 0xA0D
#define WM8994_WRITE_SEQUENCER_0                0x3000
#define WM8994_WRITE_SEQUENCER_1                0x3001
#define WM8994_WRITE_SEQUENCER_2                0x3002
#define WM8994_WRITE_SEQUENCER_3                0x3003
#define WM8994_WRITE_SEQUENCER_4                0x3004
#define WM8994_WRITE_SEQUENCER_5                0x3005
#define WM8994_WRITE_SEQUENCER_6                0x3006
#define WM8994_WRITE_SEQUENCER_7                0x3007
#define WM8994_WRITE_SEQUENCER_8                0x3008
#define WM8994_WRITE_SEQUENCER_9                0x3009
#define WM8994_WRITE_SEQUENCER_10               0x300A
#define WM8994_WRITE_SEQUENCER_11               0x300B
#define WM8994_WRITE_SEQUENCER_12               0x300C
#define WM8994_WRITE_SEQUENCER_13               0x300D
#define WM8994_WRITE_SEQUENCER_14               0x300E
#define WM8994_WRITE_SEQUENCER_15               0x300F
#define WM8994_WRITE_SEQUENCER_16               0x3010
#define WM8994_WRITE_SEQUENCER_17               0x3011
#define WM8994_WRITE_SEQUENCER_18               0x3012
#define WM8994_WRITE_SEQUENCER_19               0x3013
#define WM8994_WRITE_SEQUENCER_20               0x3014
#define WM8994_WRITE_SEQUENCER_21               0x3015
#define WM8994_WRITE_SEQUENCER_22               0x3016
#define WM8994_WRITE_SEQUENCER_23               0x3017
#define WM8994_WRITE_SEQUENCER_24               0x3018
#define WM8994_WRITE_SEQUENCER_25               0x3019
#define WM8994_WRITE_SEQUENCER_26               0x301A
#define WM8994_WRITE_SEQUENCER_27               0x301B
#define WM8994_WRITE_SEQUENCER_28               0x301C
#define WM8994_WRITE_SEQUENCER_29               0x301D
#define WM8994_WRITE_SEQUENCER_30               0x301E
#define WM8994_WRITE_SEQUENCER_31               0x301F
#define WM8994_WRITE_SEQUENCER_32               0x3020
#define WM8994_WRITE_SEQUENCER_33               0x3021
#define WM8994_WRITE_SEQUENCER_34               0x3022
#define WM8994_WRITE_SEQUENCER_35               0x3023
#define WM8994_WRITE_SEQUENCER_36               0x3024
#define WM8994_WRITE_SEQUENCER_37               0x3025
#define WM8994_WRITE_SEQUENCER_38               0x3026
#define WM8994_WRITE_SEQUENCER_39               0x3027
#define WM8994_WRITE_SEQUENCER_40               0x3028
#define WM8994_WRITE_SEQUENCER_41               0x3029
#define WM8994_WRITE_SEQUENCER_42               0x302A
#define WM8994_WRITE_SEQUENCER_43               0x302B
#define WM8994_WRITE_SEQUENCER_44               0x302C
#define WM8994_WRITE_SEQUENCER_45               0x302D
#define WM8994_WRITE_SEQUENCER_46               0x302E
#define WM8994_WRITE_SEQUENCER_47               0x302F
#define WM8994_WRITE_SEQUENCER_48               0x3030
#define WM8994_WRITE_SEQUENCER_49               0x3031
#define WM8994_WRITE_SEQUENCER_50               0x3032
#define WM8994_WRITE_SEQUENCER_51               0x3033
#define WM8994_WRITE_SEQUENCER_52               0x3034
#define WM8994_WRITE_SEQUENCER_53               0x3035
#define WM8994_WRITE_SEQUENCER_54               0x3036
#define WM8994_WRITE_SEQUENCER_55               0x3037
#define WM8994_WRITE_SEQUENCER_56               0x3038
#define WM8994_WRITE_SEQUENCER_57               0x3039
#define WM8994_WRITE_SEQUENCER_58               0x303A
#define WM8994_WRITE_SEQUENCER_59               0x303B
#define WM8994_WRITE_SEQUENCER_60               0x303C
#define WM8994_WRITE_SEQUENCER_61               0x303D
#define WM8994_WRITE_SEQUENCER_62               0x303E
#define WM8994_WRITE_SEQUENCER_63               0x303F
#define WM8994_WRITE_SEQUENCER_64               0x3040
#define WM8994_WRITE_SEQUENCER_65               0x3041
#define WM8994_WRITE_SEQUENCER_66               0x3042
#define WM8994_WRITE_SEQUENCER_67               0x3043
#define WM8994_WRITE_SEQUENCER_68               0x3044
#define WM8994_WRITE_SEQUENCER_69               0x3045
#define WM8994_WRITE_SEQUENCER_70               0x3046
#define WM8994_WRITE_SEQUENCER_71               0x3047
#define WM8994_WRITE_SEQUENCER_72               0x3048
#define WM8994_WRITE_SEQUENCER_73               0x3049
#define WM8994_WRITE_SEQUENCER_74               0x304A
#define WM8994_WRITE_SEQUENCER_75               0x304B
#define WM8994_WRITE_SEQUENCER_76               0x304C
#define WM8994_WRITE_SEQUENCER_77               0x304D
#define WM8994_WRITE_SEQUENCER_78               0x304E
#define WM8994_WRITE_SEQUENCER_79               0x304F
#define WM8994_WRITE_SEQUENCER_80               0x3050
#define WM8994_WRITE_SEQUENCER_81               0x3051
#define WM8994_WRITE_SEQUENCER_82               0x3052
#define WM8994_WRITE_SEQUENCER_83               0x3053
#define WM8994_WRITE_SEQUENCER_84               0x3054
#define WM8994_WRITE_SEQUENCER_85               0x3055
#define WM8994_WRITE_SEQUENCER_86               0x3056
#define WM8994_WRITE_SEQUENCER_87               0x3057
#define WM8994_WRITE_SEQUENCER_88               0x3058
#define WM8994_WRITE_SEQUENCER_89               0x3059
#define WM8994_WRITE_SEQUENCER_90               0x305A
#define WM8994_WRITE_SEQUENCER_91               0x305B
#define WM8994_WRITE_SEQUENCER_92               0x305C
#define WM8994_WRITE_SEQUENCER_93               0x305D
#define WM8994_WRITE_SEQUENCER_94               0x305E
#define WM8994_WRITE_SEQUENCER_95               0x305F
#define WM8994_WRITE_SEQUENCER_96               0x3060
#define WM8994_WRITE_SEQUENCER_97               0x3061
#define WM8994_WRITE_SEQUENCER_98               0x3062
#define WM8994_WRITE_SEQUENCER_99               0x3063
#define WM8994_WRITE_SEQUENCER_100              0x3064
#define WM8994_WRITE_SEQUENCER_101              0x3065
#define WM8994_WRITE_SEQUENCER_102              0x3066
#define WM8994_WRITE_SEQUENCER_103              0x3067
#define WM8994_WRITE_SEQUENCER_104              0x3068
#define WM8994_WRITE_SEQUENCER_105              0x3069
#define WM8994_WRITE_SEQUENCER_106              0x306A
#define WM8994_WRITE_SEQUENCER_107              0x306B
#define WM8994_WRITE_SEQUENCER_108              0x306C
#define WM8994_WRITE_SEQUENCER_109              0x306D
#define WM8994_WRITE_SEQUENCER_110              0x306E
#define WM8994_WRITE_SEQUENCER_111              0x306F
#define WM8994_WRITE_SEQUENCER_112              0x3070
#define WM8994_WRITE_SEQUENCER_113              0x3071
#define WM8994_WRITE_SEQUENCER_114              0x3072
#define WM8994_WRITE_SEQUENCER_115              0x3073
#define WM8994_WRITE_SEQUENCER_116              0x3074
#define WM8994_WRITE_SEQUENCER_117              0x3075
#define WM8994_WRITE_SEQUENCER_118              0x3076
#define WM8994_WRITE_SEQUENCER_119              0x3077
#define WM8994_WRITE_SEQUENCER_120              0x3078
#define WM8994_WRITE_SEQUENCER_121              0x3079
#define WM8994_WRITE_SEQUENCER_122              0x307A
#define WM8994_WRITE_SEQUENCER_123              0x307B
#define WM8994_WRITE_SEQUENCER_124              0x307C
#define WM8994_WRITE_SEQUENCER_125              0x307D
#define WM8994_WRITE_SEQUENCER_126              0x307E
#define WM8994_WRITE_SEQUENCER_127              0x307F
#define WM8994_WRITE_SEQUENCER_128              0x3080
#define WM8994_WRITE_SEQUENCER_129              0x3081
#define WM8994_WRITE_SEQUENCER_130              0x3082
#define WM8994_WRITE_SEQUENCER_131              0x3083
#define WM8994_WRITE_SEQUENCER_132              0x3084
#define WM8994_WRITE_SEQUENCER_133              0x3085
#define WM8994_WRITE_SEQUENCER_134              0x3086
#define WM8994_WRITE_SEQUENCER_135              0x3087
#define WM8994_WRITE_SEQUENCER_136              0x3088
#define WM8994_WRITE_SEQUENCER_137              0x3089
#define WM8994_WRITE_SEQUENCER_138              0x308A
#define WM8994_WRITE_SEQUENCER_139              0x308B
#define WM8994_WRITE_SEQUENCER_140              0x308C
#define WM8994_WRITE_SEQUENCER_141              0x308D
#define WM8994_WRITE_SEQUENCER_142              0x308E
#define WM8994_WRITE_SEQUENCER_143              0x308F
#define WM8994_WRITE_SEQUENCER_144              0x3090
#define WM8994_WRITE_SEQUENCER_145              0x3091
#define WM8994_WRITE_SEQUENCER_146              0x3092
#define WM8994_WRITE_SEQUENCER_147              0x3093
#define WM8994_WRITE_SEQUENCER_148              0x3094
#define WM8994_WRITE_SEQUENCER_149              0x3095
#define WM8994_WRITE_SEQUENCER_150              0x3096
#define WM8994_WRITE_SEQUENCER_151              0x3097
#define WM8994_WRITE_SEQUENCER_152              0x3098
#define WM8994_WRITE_SEQUENCER_153              0x3099
#define WM8994_WRITE_SEQUENCER_154              0x309A
#define WM8994_WRITE_SEQUENCER_155              0x309B
#define WM8994_WRITE_SEQUENCER_156              0x309C
#define WM8994_WRITE_SEQUENCER_157              0x309D
#define WM8994_WRITE_SEQUENCER_158              0x309E
#define WM8994_WRITE_SEQUENCER_159              0x309F
#define WM8994_WRITE_SEQUENCER_160              0x30A0
#define WM8994_WRITE_SEQUENCER_161              0x30A1
#define WM8994_WRITE_SEQUENCER_162              0x30A2
#define WM8994_WRITE_SEQUENCER_163              0x30A3
#define WM8994_WRITE_SEQUENCER_164              0x30A4
#define WM8994_WRITE_SEQUENCER_165              0x30A5
#define WM8994_WRITE_SEQUENCER_166              0x30A6
#define WM8994_WRITE_SEQUENCER_167              0x30A7
#define WM8994_WRITE_SEQUENCER_168              0x30A8
#define WM8994_WRITE_SEQUENCER_169              0x30A9
#define WM8994_WRITE_SEQUENCER_170              0x30AA
#define WM8994_WRITE_SEQUENCER_171              0x30AB
#define WM8994_WRITE_SEQUENCER_172              0x30AC
#define WM8994_WRITE_SEQUENCER_173              0x30AD
#define WM8994_WRITE_SEQUENCER_174              0x30AE
#define WM8994_WRITE_SEQUENCER_175              0x30AF
#define WM8994_WRITE_SEQUENCER_176              0x30B0
#define WM8994_WRITE_SEQUENCER_177              0x30B1
#define WM8994_WRITE_SEQUENCER_178              0x30B2
#define WM8994_WRITE_SEQUENCER_179              0x30B3
#define WM8994_WRITE_SEQUENCER_180              0x30B4
#define WM8994_WRITE_SEQUENCER_181              0x30B5
#define WM8994_WRITE_SEQUENCER_182              0x30B6
#define WM8994_WRITE_SEQUENCER_183              0x30B7
#define WM8994_WRITE_SEQUENCER_184              0x30B8
#define WM8994_WRITE_SEQUENCER_185              0x30B9
#define WM8994_WRITE_SEQUENCER_186              0x30BA
#define WM8994_WRITE_SEQUENCER_187              0x30BB
#define WM8994_WRITE_SEQUENCER_188              0x30BC
#define WM8994_WRITE_SEQUENCER_189              0x30BD
#define WM8994_WRITE_SEQUENCER_190              0x30BE
#define WM8994_WRITE_SEQUENCER_191              0x30BF
#define WM8994_WRITE_SEQUENCER_192              0x30C0
#define WM8994_WRITE_SEQUENCER_193              0x30C1
#define WM8994_WRITE_SEQUENCER_194              0x30C2
#define WM8994_WRITE_SEQUENCER_195              0x30C3
#define WM8994_WRITE_SEQUENCER_196              0x30C4
#define WM8994_WRITE_SEQUENCER_197              0x30C5
#define WM8994_WRITE_SEQUENCER_198              0x30C6
#define WM8994_WRITE_SEQUENCER_199              0x30C7
#define WM8994_WRITE_SEQUENCER_200              0x30C8
#define WM8994_WRITE_SEQUENCER_201              0x30C9
#define WM8994_WRITE_SEQUENCER_202              0x30CA
#define WM8994_WRITE_SEQUENCER_203              0x30CB
#define WM8994_WRITE_SEQUENCER_204              0x30CC
#define WM8994_WRITE_SEQUENCER_205              0x30CD
#define WM8994_WRITE_SEQUENCER_206              0x30CE
#define WM8994_WRITE_SEQUENCER_207              0x30CF
#define WM8994_WRITE_SEQUENCER_208              0x30D0
#define WM8994_WRITE_SEQUENCER_209              0x30D1
#define WM8994_WRITE_SEQUENCER_210              0x30D2
#define WM8994_WRITE_SEQUENCER_211              0x30D3
#define WM8994_WRITE_SEQUENCER_212              0x30D4
#define WM8994_WRITE_SEQUENCER_213              0x30D5
#define WM8994_WRITE_SEQUENCER_214              0x30D6
#define WM8994_WRITE_SEQUENCER_215              0x30D7
#define WM8994_WRITE_SEQUENCER_216              0x30D8
#define WM8994_WRITE_SEQUENCER_217              0x30D9
#define WM8994_WRITE_SEQUENCER_218              0x30DA
#define WM8994_WRITE_SEQUENCER_219              0x30DB
#define WM8994_WRITE_SEQUENCER_220              0x30DC
#define WM8994_WRITE_SEQUENCER_221              0x30DD
#define WM8994_WRITE_SEQUENCER_222              0x30DE
#define WM8994_WRITE_SEQUENCER_223              0x30DF
#define WM8994_WRITE_SEQUENCER_224              0x30E0
#define WM8994_WRITE_SEQUENCER_225              0x30E1
#define WM8994_WRITE_SEQUENCER_226              0x30E2
#define WM8994_WRITE_SEQUENCER_227              0x30E3
#define WM8994_WRITE_SEQUENCER_228              0x30E4
#define WM8994_WRITE_SEQUENCER_229              0x30E5
#define WM8994_WRITE_SEQUENCER_230              0x30E6
#define WM8994_WRITE_SEQUENCER_231              0x30E7
#define WM8994_WRITE_SEQUENCER_232              0x30E8
#define WM8994_WRITE_SEQUENCER_233              0x30E9
#define WM8994_WRITE_SEQUENCER_234              0x30EA
#define WM8994_WRITE_SEQUENCER_235              0x30EB
#define WM8994_WRITE_SEQUENCER_236              0x30EC
#define WM8994_WRITE_SEQUENCER_237              0x30ED
#define WM8994_WRITE_SEQUENCER_238              0x30EE
#define WM8994_WRITE_SEQUENCER_239              0x30EF
#define WM8994_WRITE_SEQUENCER_240              0x30F0
#define WM8994_WRITE_SEQUENCER_241              0x30F1
#define WM8994_WRITE_SEQUENCER_242              0x30F2
#define WM8994_WRITE_SEQUENCER_243              0x30F3
#define WM8994_WRITE_SEQUENCER_244              0x30F4
#define WM8994_WRITE_SEQUENCER_245              0x30F5
#define WM8994_WRITE_SEQUENCER_246              0x30F6
#define WM8994_WRITE_SEQUENCER_247              0x30F7
#define WM8994_WRITE_SEQUENCER_248              0x30F8
#define WM8994_WRITE_SEQUENCER_249              0x30F9
#define WM8994_WRITE_SEQUENCER_250              0x30FA
#define WM8994_WRITE_SEQUENCER_251              0x30FB
#define WM8994_WRITE_SEQUENCER_252              0x30FC
#define WM8994_WRITE_SEQUENCER_253              0x30FD
#define WM8994_WRITE_SEQUENCER_254              0x30FE
#define WM8994_WRITE_SEQUENCER_255              0x30FF
#define WM8994_WRITE_SEQUENCER_256              0x3100
#define WM8994_WRITE_SEQUENCER_257              0x3101
#define WM8994_WRITE_SEQUENCER_258              0x3102
#define WM8994_WRITE_SEQUENCER_259              0x3103
#define WM8994_WRITE_SEQUENCER_260              0x3104
#define WM8994_WRITE_SEQUENCER_261              0x3105
#define WM8994_WRITE_SEQUENCER_262              0x3106
#define WM8994_WRITE_SEQUENCER_263              0x3107
#define WM8994_WRITE_SEQUENCER_264              0x3108
#define WM8994_WRITE_SEQUENCER_265              0x3109
#define WM8994_WRITE_SEQUENCER_266              0x310A
#define WM8994_WRITE_SEQUENCER_267              0x310B
#define WM8994_WRITE_SEQUENCER_268              0x310C
#define WM8994_WRITE_SEQUENCER_269              0x310D
#define WM8994_WRITE_SEQUENCER_270              0x310E
#define WM8994_WRITE_SEQUENCER_271              0x310F
#define WM8994_WRITE_SEQUENCER_272              0x3110
#define WM8994_WRITE_SEQUENCER_273              0x3111
#define WM8994_WRITE_SEQUENCER_274              0x3112
#define WM8994_WRITE_SEQUENCER_275              0x3113
#define WM8994_WRITE_SEQUENCER_276              0x3114
#define WM8994_WRITE_SEQUENCER_277              0x3115
#define WM8994_WRITE_SEQUENCER_278              0x3116
#define WM8994_WRITE_SEQUENCER_279              0x3117
#define WM8994_WRITE_SEQUENCER_280              0x3118
#define WM8994_WRITE_SEQUENCER_281              0x3119
#define WM8994_WRITE_SEQUENCER_282              0x311A
#define WM8994_WRITE_SEQUENCER_283              0x311B
#define WM8994_WRITE_SEQUENCER_284              0x311C
#define WM8994_WRITE_SEQUENCER_285              0x311D
#define WM8994_WRITE_SEQUENCER_286              0x311E
#define WM8994_WRITE_SEQUENCER_287              0x311F
#define WM8994_WRITE_SEQUENCER_288              0x3120
#define WM8994_WRITE_SEQUENCER_289              0x3121
#define WM8994_WRITE_SEQUENCER_290              0x3122
#define WM8994_WRITE_SEQUENCER_291              0x3123
#define WM8994_WRITE_SEQUENCER_292              0x3124
#define WM8994_WRITE_SEQUENCER_293              0x3125
#define WM8994_WRITE_SEQUENCER_294              0x3126
#define WM8994_WRITE_SEQUENCER_295              0x3127
#define WM8994_WRITE_SEQUENCER_296              0x3128
#define WM8994_WRITE_SEQUENCER_297              0x3129
#define WM8994_WRITE_SEQUENCER_298              0x312A
#define WM8994_WRITE_SEQUENCER_299              0x312B
#define WM8994_WRITE_SEQUENCER_300              0x312C
#define WM8994_WRITE_SEQUENCER_301              0x312D
#define WM8994_WRITE_SEQUENCER_302              0x312E
#define WM8994_WRITE_SEQUENCER_303              0x312F
#define WM8994_WRITE_SEQUENCER_304              0x3130
#define WM8994_WRITE_SEQUENCER_305              0x3131
#define WM8994_WRITE_SEQUENCER_306              0x3132
#define WM8994_WRITE_SEQUENCER_307              0x3133
#define WM8994_WRITE_SEQUENCER_308              0x3134
#define WM8994_WRITE_SEQUENCER_309              0x3135
#define WM8994_WRITE_SEQUENCER_310              0x3136
#define WM8994_WRITE_SEQUENCER_311              0x3137
#define WM8994_WRITE_SEQUENCER_312              0x3138
#define WM8994_WRITE_SEQUENCER_313              0x3139
#define WM8994_WRITE_SEQUENCER_314              0x313A
#define WM8994_WRITE_SEQUENCER_315              0x313B
#define WM8994_WRITE_SEQUENCER_316              0x313C
#define WM8994_WRITE_SEQUENCER_317              0x313D
#define WM8994_WRITE_SEQUENCER_318              0x313E
#define WM8994_WRITE_SEQUENCER_319              0x313F
#define WM8994_WRITE_SEQUENCER_320              0x3140
#define WM8994_WRITE_SEQUENCER_321              0x3141
#define WM8994_WRITE_SEQUENCER_322              0x3142
#define WM8994_WRITE_SEQUENCER_323              0x3143
#define WM8994_WRITE_SEQUENCER_324              0x3144
#define WM8994_WRITE_SEQUENCER_325              0x3145
#define WM8994_WRITE_SEQUENCER_326              0x3146
#define WM8994_WRITE_SEQUENCER_327              0x3147
#define WM8994_WRITE_SEQUENCER_328              0x3148
#define WM8994_WRITE_SEQUENCER_329              0x3149
#define WM8994_WRITE_SEQUENCER_330              0x314A
#define WM8994_WRITE_SEQUENCER_331              0x314B
#define WM8994_WRITE_SEQUENCER_332              0x314C
#define WM8994_WRITE_SEQUENCER_333              0x314D
#define WM8994_WRITE_SEQUENCER_334              0x314E
#define WM8994_WRITE_SEQUENCER_335              0x314F
#define WM8994_WRITE_SEQUENCER_336              0x3150
#define WM8994_WRITE_SEQUENCER_337              0x3151
#define WM8994_WRITE_SEQUENCER_338              0x3152
#define WM8994_WRITE_SEQUENCER_339              0x3153
#define WM8994_WRITE_SEQUENCER_340              0x3154
#define WM8994_WRITE_SEQUENCER_341              0x3155
#define WM8994_WRITE_SEQUENCER_342              0x3156
#define WM8994_WRITE_SEQUENCER_343              0x3157
#define WM8994_WRITE_SEQUENCER_344              0x3158
#define WM8994_WRITE_SEQUENCER_345              0x3159
#define WM8994_WRITE_SEQUENCER_346              0x315A
#define WM8994_WRITE_SEQUENCER_347              0x315B
#define WM8994_WRITE_SEQUENCER_348              0x315C
#define WM8994_WRITE_SEQUENCER_349              0x315D
#define WM8994_WRITE_SEQUENCER_350              0x315E
#define WM8994_WRITE_SEQUENCER_351              0x315F
#define WM8994_WRITE_SEQUENCER_352              0x3160
#define WM8994_WRITE_SEQUENCER_353              0x3161
#define WM8994_WRITE_SEQUENCER_354              0x3162
#define WM8994_WRITE_SEQUENCER_355              0x3163
#define WM8994_WRITE_SEQUENCER_356              0x3164
#define WM8994_WRITE_SEQUENCER_357              0x3165
#define WM8994_WRITE_SEQUENCER_358              0x3166
#define WM8994_WRITE_SEQUENCER_359              0x3167
#define WM8994_WRITE_SEQUENCER_360              0x3168
#define WM8994_WRITE_SEQUENCER_361              0x3169
#define WM8994_WRITE_SEQUENCER_362              0x316A
#define WM8994_WRITE_SEQUENCER_363              0x316B
#define WM8994_WRITE_SEQUENCER_364              0x316C
#define WM8994_WRITE_SEQUENCER_365              0x316D
#define WM8994_WRITE_SEQUENCER_366              0x316E
#define WM8994_WRITE_SEQUENCER_367              0x316F
#define WM8994_WRITE_SEQUENCER_368              0x3170
#define WM8994_WRITE_SEQUENCER_369              0x3171
#define WM8994_WRITE_SEQUENCER_370              0x3172
#define WM8994_WRITE_SEQUENCER_371              0x3173
#define WM8994_WRITE_SEQUENCER_372              0x3174
#define WM8994_WRITE_SEQUENCER_373              0x3175
#define WM8994_WRITE_SEQUENCER_374              0x3176
#define WM8994_WRITE_SEQUENCER_375              0x3177
#define WM8994_WRITE_SEQUENCER_376              0x3178
#define WM8994_WRITE_SEQUENCER_377              0x3179
#define WM8994_WRITE_SEQUENCER_378              0x317A
#define WM8994_WRITE_SEQUENCER_379              0x317B
#define WM8994_WRITE_SEQUENCER_380              0x317C
#define WM8994_WRITE_SEQUENCER_381              0x317D
#define WM8994_WRITE_SEQUENCER_382              0x317E
#define WM8994_WRITE_SEQUENCER_383              0x317F
#define WM8994_WRITE_SEQUENCER_384              0x3180
#define WM8994_WRITE_SEQUENCER_385              0x3181
#define WM8994_WRITE_SEQUENCER_386              0x3182
#define WM8994_WRITE_SEQUENCER_387              0x3183
#define WM8994_WRITE_SEQUENCER_388              0x3184
#define WM8994_WRITE_SEQUENCER_389              0x3185
#define WM8994_WRITE_SEQUENCER_390              0x3186
#define WM8994_WRITE_SEQUENCER_391              0x3187
#define WM8994_WRITE_SEQUENCER_392              0x3188
#define WM8994_WRITE_SEQUENCER_393              0x3189
#define WM8994_WRITE_SEQUENCER_394              0x318A
#define WM8994_WRITE_SEQUENCER_395              0x318B
#define WM8994_WRITE_SEQUENCER_396              0x318C
#define WM8994_WRITE_SEQUENCER_397              0x318D
#define WM8994_WRITE_SEQUENCER_398              0x318E
#define WM8994_WRITE_SEQUENCER_399              0x318F
#define WM8994_WRITE_SEQUENCER_400              0x3190
#define WM8994_WRITE_SEQUENCER_401              0x3191
#define WM8994_WRITE_SEQUENCER_402              0x3192
#define WM8994_WRITE_SEQUENCER_403              0x3193
#define WM8994_WRITE_SEQUENCER_404              0x3194
#define WM8994_WRITE_SEQUENCER_405              0x3195
#define WM8994_WRITE_SEQUENCER_406              0x3196
#define WM8994_WRITE_SEQUENCER_407              0x3197
#define WM8994_WRITE_SEQUENCER_408              0x3198
#define WM8994_WRITE_SEQUENCER_409              0x3199
#define WM8994_WRITE_SEQUENCER_410              0x319A
#define WM8994_WRITE_SEQUENCER_411              0x319B
#define WM8994_WRITE_SEQUENCER_412              0x319C
#define WM8994_WRITE_SEQUENCER_413              0x319D
#define WM8994_WRITE_SEQUENCER_414              0x319E
#define WM8994_WRITE_SEQUENCER_415              0x319F
#define WM8994_WRITE_SEQUENCER_416              0x31A0
#define WM8994_WRITE_SEQUENCER_417              0x31A1
#define WM8994_WRITE_SEQUENCER_418              0x31A2
#define WM8994_WRITE_SEQUENCER_419              0x31A3
#define WM8994_WRITE_SEQUENCER_420              0x31A4
#define WM8994_WRITE_SEQUENCER_421              0x31A5
#define WM8994_WRITE_SEQUENCER_422              0x31A6
#define WM8994_WRITE_SEQUENCER_423              0x31A7
#define WM8994_WRITE_SEQUENCER_424              0x31A8
#define WM8994_WRITE_SEQUENCER_425              0x31A9
#define WM8994_WRITE_SEQUENCER_426              0x31AA
#define WM8994_WRITE_SEQUENCER_427              0x31AB
#define WM8994_WRITE_SEQUENCER_428              0x31AC
#define WM8994_WRITE_SEQUENCER_429              0x31AD
#define WM8994_WRITE_SEQUENCER_430              0x31AE
#define WM8994_WRITE_SEQUENCER_431              0x31AF
#define WM8994_WRITE_SEQUENCER_432              0x31B0
#define WM8994_WRITE_SEQUENCER_433              0x31B1
#define WM8994_WRITE_SEQUENCER_434              0x31B2
#define WM8994_WRITE_SEQUENCER_435              0x31B3
#define WM8994_WRITE_SEQUENCER_436              0x31B4
#define WM8994_WRITE_SEQUENCER_437              0x31B5
#define WM8994_WRITE_SEQUENCER_438              0x31B6
#define WM8994_WRITE_SEQUENCER_439              0x31B7
#define WM8994_WRITE_SEQUENCER_440              0x31B8
#define WM8994_WRITE_SEQUENCER_441              0x31B9
#define WM8994_WRITE_SEQUENCER_442              0x31BA
#define WM8994_WRITE_SEQUENCER_443              0x31BB
#define WM8994_WRITE_SEQUENCER_444              0x31BC
#define WM8994_WRITE_SEQUENCER_445              0x31BD
#define WM8994_WRITE_SEQUENCER_446              0x31BE
#define WM8994_WRITE_SEQUENCER_447              0x31BF
#define WM8994_WRITE_SEQUENCER_448              0x31C0
#define WM8994_WRITE_SEQUENCER_449              0x31C1
#define WM8994_WRITE_SEQUENCER_450              0x31C2
#define WM8994_WRITE_SEQUENCER_451              0x31C3
#define WM8994_WRITE_SEQUENCER_452              0x31C4
#define WM8994_WRITE_SEQUENCER_453              0x31C5
#define WM8994_WRITE_SEQUENCER_454              0x31C6
#define WM8994_WRITE_SEQUENCER_455              0x31C7
#define WM8994_WRITE_SEQUENCER_456              0x31C8
#define WM8994_WRITE_SEQUENCER_457              0x31C9
#define WM8994_WRITE_SEQUENCER_458              0x31CA
#define WM8994_WRITE_SEQUENCER_459              0x31CB
#define WM8994_WRITE_SEQUENCER_460              0x31CC
#define WM8994_WRITE_SEQUENCER_461              0x31CD
#define WM8994_WRITE_SEQUENCER_462              0x31CE
#define WM8994_WRITE_SEQUENCER_463              0x31CF
#define WM8994_WRITE_SEQUENCER_464              0x31D0
#define WM8994_WRITE_SEQUENCER_465              0x31D1
#define WM8994_WRITE_SEQUENCER_466              0x31D2
#define WM8994_WRITE_SEQUENCER_467              0x31D3
#define WM8994_WRITE_SEQUENCER_468              0x31D4
#define WM8994_WRITE_SEQUENCER_469              0x31D5
#define WM8994_WRITE_SEQUENCER_470              0x31D6
#define WM8994_WRITE_SEQUENCER_471              0x31D7
#define WM8994_WRITE_SEQUENCER_472              0x31D8
#define WM8994_WRITE_SEQUENCER_473              0x31D9
#define WM8994_WRITE_SEQUENCER_474              0x31DA
#define WM8994_WRITE_SEQUENCER_475              0x31DB
#define WM8994_WRITE_SEQUENCER_476              0x31DC
#define WM8994_WRITE_SEQUENCER_477              0x31DD
#define WM8994_WRITE_SEQUENCER_478              0x31DE
#define WM8994_WRITE_SEQUENCER_479              0x31DF
#define WM8994_WRITE_SEQUENCER_480              0x31E0
#define WM8994_WRITE_SEQUENCER_481              0x31E1
#define WM8994_WRITE_SEQUENCER_482              0x31E2
#define WM8994_WRITE_SEQUENCER_483              0x31E3
#define WM8994_WRITE_SEQUENCER_484              0x31E4
#define WM8994_WRITE_SEQUENCER_485              0x31E5
#define WM8994_WRITE_SEQUENCER_486              0x31E6
#define WM8994_WRITE_SEQUENCER_487              0x31E7
#define WM8994_WRITE_SEQUENCER_488              0x31E8
#define WM8994_WRITE_SEQUENCER_489              0x31E9
#define WM8994_WRITE_SEQUENCER_490              0x31EA
#define WM8994_WRITE_SEQUENCER_491              0x31EB
#define WM8994_WRITE_SEQUENCER_492              0x31EC
#define WM8994_WRITE_SEQUENCER_493              0x31ED
#define WM8994_WRITE_SEQUENCER_494              0x31EE
#define WM8994_WRITE_SEQUENCER_495              0x31EF
#define WM8994_WRITE_SEQUENCER_496              0x31F0
#define WM8994_WRITE_SEQUENCER_497              0x31F1
#define WM8994_WRITE_SEQUENCER_498              0x31F2
#define WM8994_WRITE_SEQUENCER_499              0x31F3
#define WM8994_WRITE_SEQUENCER_500              0x31F4
#define WM8994_WRITE_SEQUENCER_501              0x31F5
#define WM8994_WRITE_SEQUENCER_502              0x31F6
#define WM8994_WRITE_SEQUENCER_503              0x31F7
#define WM8994_WRITE_SEQUENCER_504              0x31F8
#define WM8994_WRITE_SEQUENCER_505              0x31F9
#define WM8994_WRITE_SEQUENCER_506              0x31FA
#define WM8994_WRITE_SEQUENCER_507              0x31FB
#define WM8994_WRITE_SEQUENCER_508              0x31FC
#define WM8994_WRITE_SEQUENCER_509              0x31FD
#define WM8994_WRITE_SEQUENCER_510              0x31FE
#define WM8994_WRITE_SEQUENCER_511              0x31FF

#define WM8994_REGISTER_COUNT                   736
#define WM8994_MAX_REGISTER                     0x31FF
#define WM8994_MAX_CACHED_REGISTER              0x749

/*
 * Field Definitions.
 */

/*
 * R0 (0x00) - Software Reset
 */
#define WM8994_SW_RESET_MASK                    0xFFFF  /* SW_RESET - [15:0] */
#define WM8994_SW_RESET_SHIFT                        0  /* SW_RESET - [15:0] */
#define WM8994_SW_RESET_WIDTH                       16  /* SW_RESET - [15:0] */

/*
 * R1 (0x01) - Power Management (1)
 */
#define WM8994_SPKOUTR_ENA                      0x2000  /* SPKOUTR_ENA */
#define WM8994_SPKOUTR_ENA_MASK                 0x2000  /* SPKOUTR_ENA */
#define WM8994_SPKOUTR_ENA_SHIFT                    13  /* SPKOUTR_ENA */
#define WM8994_SPKOUTR_ENA_WIDTH                     1  /* SPKOUTR_ENA */
#define WM8994_SPKOUTL_ENA                      0x1000  /* SPKOUTL_ENA */
#define WM8994_SPKOUTL_ENA_MASK                 0x1000  /* SPKOUTL_ENA */
#define WM8994_SPKOUTL_ENA_SHIFT                    12  /* SPKOUTL_ENA */
#define WM8994_SPKOUTL_ENA_WIDTH                     1  /* SPKOUTL_ENA */
#define WM8994_HPOUT2_ENA                       0x0800  /* HPOUT2_ENA */
#define WM8994_HPOUT2_ENA_MASK                  0x0800  /* HPOUT2_ENA */
#define WM8994_HPOUT2_ENA_SHIFT                     11  /* HPOUT2_ENA */
#define WM8994_HPOUT2_ENA_WIDTH                      1  /* HPOUT2_ENA */
#define WM8994_HPOUT1L_ENA                      0x0200  /* HPOUT1L_ENA */
#define WM8994_HPOUT1L_ENA_MASK                 0x0200  /* HPOUT1L_ENA */
#define WM8994_HPOUT1L_ENA_SHIFT                     9  /* HPOUT1L_ENA */
#define WM8994_HPOUT1L_ENA_WIDTH                     1  /* HPOUT1L_ENA */
#define WM8994_HPOUT1R_ENA                      0x0100  /* HPOUT1R_ENA */
#define WM8994_HPOUT1R_ENA_MASK                 0x0100  /* HPOUT1R_ENA */
#define WM8994_HPOUT1R_ENA_SHIFT                     8  /* HPOUT1R_ENA */
#define WM8994_HPOUT1R_ENA_WIDTH                     1  /* HPOUT1R_ENA */
#define WM8994_MICB2_ENA                        0x0020  /* MICB2_ENA */
#define WM8994_MICB2_ENA_MASK                   0x0020  /* MICB2_ENA */
#define WM8994_MICB2_ENA_SHIFT                       5  /* MICB2_ENA */
#define WM8994_MICB2_ENA_WIDTH                       1  /* MICB2_ENA */
#define WM8994_MICB1_ENA                        0x0010  /* MICB1_ENA */
#define WM8994_MICB1_ENA_MASK                   0x0010  /* MICB1_ENA */
#define WM8994_MICB1_ENA_SHIFT                       4  /* MICB1_ENA */
#define WM8994_MICB1_ENA_WIDTH                       1  /* MICB1_ENA */
#define WM8994_VMID_SEL_MASK                    0x0006  /* VMID_SEL - [2:1] */
#define WM8994_VMID_SEL_SHIFT                        1  /* VMID_SEL - [2:1] */
#define WM8994_VMID_SEL_WIDTH                        2  /* VMID_SEL - [2:1] */
#define WM8994_BIAS_ENA                         0x0001  /* BIAS_ENA */
#define WM8994_BIAS_ENA_MASK                    0x0001  /* BIAS_ENA */
#define WM8994_BIAS_ENA_SHIFT                        0  /* BIAS_ENA */
#define WM8994_BIAS_ENA_WIDTH                        1  /* BIAS_ENA */

/*
 * R2 (0x02) - Power Management (2)
 */
#define WM8994_TSHUT_ENA                        0x4000  /* TSHUT_ENA */
#define WM8994_TSHUT_ENA_MASK                   0x4000  /* TSHUT_ENA */
#define WM8994_TSHUT_ENA_SHIFT                      14  /* TSHUT_ENA */
#define WM8994_TSHUT_ENA_WIDTH                       1  /* TSHUT_ENA */
#define WM8994_TSHUT_OPDIS                      0x2000  /* TSHUT_OPDIS */
#define WM8994_TSHUT_OPDIS_MASK                 0x2000  /* TSHUT_OPDIS */
#define WM8994_TSHUT_OPDIS_SHIFT                    13  /* TSHUT_OPDIS */
#define WM8994_TSHUT_OPDIS_WIDTH                     1  /* TSHUT_OPDIS */
#define WM8994_OPCLK_ENA                        0x0800  /* OPCLK_ENA */
#define WM8994_OPCLK_ENA_MASK                   0x0800  /* OPCLK_ENA */
#define WM8994_OPCLK_ENA_SHIFT                      11  /* OPCLK_ENA */
#define WM8994_OPCLK_ENA_WIDTH                       1  /* OPCLK_ENA */
#define WM8994_MIXINL_ENA                       0x0200  /* MIXINL_ENA */
#define WM8994_MIXINL_ENA_MASK                  0x0200  /* MIXINL_ENA */
#define WM8994_MIXINL_ENA_SHIFT                      9  /* MIXINL_ENA */
#define WM8994_MIXINL_ENA_WIDTH                      1  /* MIXINL_ENA */
#define WM8994_MIXINR_ENA                       0x0100  /* MIXINR_ENA */
#define WM8994_MIXINR_ENA_MASK                  0x0100  /* MIXINR_ENA */
#define WM8994_MIXINR_ENA_SHIFT                      8  /* MIXINR_ENA */
#define WM8994_MIXINR_ENA_WIDTH                      1  /* MIXINR_ENA */
#define WM8994_IN2L_ENA                         0x0080  /* IN2L_ENA */
#define WM8994_IN2L_ENA_MASK                    0x0080  /* IN2L_ENA */
#define WM8994_IN2L_ENA_SHIFT                        7  /* IN2L_ENA */
#define WM8994_IN2L_ENA_WIDTH                        1  /* IN2L_ENA */
#define WM8994_IN1L_ENA                         0x0040  /* IN1L_ENA */
#define WM8994_IN1L_ENA_MASK                    0x0040  /* IN1L_ENA */
#define WM8994_IN1L_ENA_SHIFT                        6  /* IN1L_ENA */
#define WM8994_IN1L_ENA_WIDTH                        1  /* IN1L_ENA */
#define WM8994_IN2R_ENA                         0x0020  /* IN2R_ENA */
#define WM8994_IN2R_ENA_MASK                    0x0020  /* IN2R_ENA */
#define WM8994_IN2R_ENA_SHIFT                        5  /* IN2R_ENA */
#define WM8994_IN2R_ENA_WIDTH                        1  /* IN2R_ENA */
#define WM8994_IN1R_ENA                         0x0010  /* IN1R_ENA */
#define WM8994_IN1R_ENA_MASK                    0x0010  /* IN1R_ENA */
#define WM8994_IN1R_ENA_SHIFT                        4  /* IN1R_ENA */
#define WM8994_IN1R_ENA_WIDTH                        1  /* IN1R_ENA */

/*
 * R3 (0x03) - Power Management (3)
 */
#define WM8994_LINEOUT1N_ENA                    0x2000  /* LINEOUT1N_ENA */
#define WM8994_LINEOUT1N_ENA_MASK               0x2000  /* LINEOUT1N_ENA */
#define WM8994_LINEOUT1N_ENA_SHIFT                  13  /* LINEOUT1N_ENA */
#define WM8994_LINEOUT1N_ENA_WIDTH                   1  /* LINEOUT1N_ENA */
#define WM8994_LINEOUT1P_ENA                    0x1000  /* LINEOUT1P_ENA */
#define WM8994_LINEOUT1P_ENA_MASK               0x1000  /* LINEOUT1P_ENA */
#define WM8994_LINEOUT1P_ENA_SHIFT                  12  /* LINEOUT1P_ENA */
#define WM8994_LINEOUT1P_ENA_WIDTH                   1  /* LINEOUT1P_ENA */
#define WM8994_LINEOUT2N_ENA                    0x0800  /* LINEOUT2N_ENA */
#define WM8994_LINEOUT2N_ENA_MASK               0x0800  /* LINEOUT2N_ENA */
#define WM8994_LINEOUT2N_ENA_SHIFT                  11  /* LINEOUT2N_ENA */
#define WM8994_LINEOUT2N_ENA_WIDTH                   1  /* LINEOUT2N_ENA */
#define WM8994_LINEOUT2P_ENA                    0x0400  /* LINEOUT2P_ENA */
#define WM8994_LINEOUT2P_ENA_MASK               0x0400  /* LINEOUT2P_ENA */
#define WM8994_LINEOUT2P_ENA_SHIFT                  10  /* LINEOUT2P_ENA */
#define WM8994_LINEOUT2P_ENA_WIDTH                   1  /* LINEOUT2P_ENA */
#define WM8994_SPKRVOL_ENA                      0x0200  /* SPKRVOL_ENA */
#define WM8994_SPKRVOL_ENA_MASK                 0x0200  /* SPKRVOL_ENA */
#define WM8994_SPKRVOL_ENA_SHIFT                     9  /* SPKRVOL_ENA */
#define WM8994_SPKRVOL_ENA_WIDTH                     1  /* SPKRVOL_ENA */
#define WM8994_SPKLVOL_ENA                      0x0100  /* SPKLVOL_ENA */
#define WM8994_SPKLVOL_ENA_MASK                 0x0100  /* SPKLVOL_ENA */
#define WM8994_SPKLVOL_ENA_SHIFT                     8  /* SPKLVOL_ENA */
#define WM8994_SPKLVOL_ENA_WIDTH                     1  /* SPKLVOL_ENA */
#define WM8994_MIXOUTLVOL_ENA                   0x0080  /* MIXOUTLVOL_ENA */
#define WM8994_MIXOUTLVOL_ENA_MASK              0x0080  /* MIXOUTLVOL_ENA */
#define WM8994_MIXOUTLVOL_ENA_SHIFT                  7  /* MIXOUTLVOL_ENA */
#define WM8994_MIXOUTLVOL_ENA_WIDTH                  1  /* MIXOUTLVOL_ENA */
#define WM8994_MIXOUTRVOL_ENA                   0x0040  /* MIXOUTRVOL_ENA */
#define WM8994_MIXOUTRVOL_ENA_MASK              0x0040  /* MIXOUTRVOL_ENA */
#define WM8994_MIXOUTRVOL_ENA_SHIFT                  6  /* MIXOUTRVOL_ENA */
#define WM8994_MIXOUTRVOL_ENA_WIDTH                  1  /* MIXOUTRVOL_ENA */
#define WM8994_MIXOUTL_ENA                      0x0020  /* MIXOUTL_ENA */
#define WM8994_MIXOUTL_ENA_MASK                 0x0020  /* MIXOUTL_ENA */
#define WM8994_MIXOUTL_ENA_SHIFT                     5  /* MIXOUTL_ENA */
#define WM8994_MIXOUTL_ENA_WIDTH                     1  /* MIXOUTL_ENA */
#define WM8994_MIXOUTR_ENA                      0x0010  /* MIXOUTR_ENA */
#define WM8994_MIXOUTR_ENA_MASK                 0x0010  /* MIXOUTR_ENA */
#define WM8994_MIXOUTR_ENA_SHIFT                     4  /* MIXOUTR_ENA */
#define WM8994_MIXOUTR_ENA_WIDTH                     1  /* MIXOUTR_ENA */

/*
 * R4 (0x04) - Power Management (4)
 */
#define WM8994_AIF2ADCL_ENA                     0x2000  /* AIF2ADCL_ENA */
#define WM8994_AIF2ADCL_ENA_MASK                0x2000  /* AIF2ADCL_ENA */
#define WM8994_AIF2ADCL_ENA_SHIFT                   13  /* AIF2ADCL_ENA */
#define WM8994_AIF2ADCL_ENA_WIDTH                    1  /* AIF2ADCL_ENA */
#define WM8994_AIF2ADCR_ENA                     0x1000  /* AIF2ADCR_ENA */
#define WM8994_AIF2ADCR_ENA_MASK                0x1000  /* AIF2ADCR_ENA */
#define WM8994_AIF2ADCR_ENA_SHIFT                   12  /* AIF2ADCR_ENA */
#define WM8994_AIF2ADCR_ENA_WIDTH                    1  /* AIF2ADCR_ENA */
#define WM8994_AIF1ADC2L_ENA                    0x0800  /* AIF1ADC2L_ENA */
#define WM8994_AIF1ADC2L_ENA_MASK               0x0800  /* AIF1ADC2L_ENA */
#define WM8994_AIF1ADC2L_ENA_SHIFT                  11  /* AIF1ADC2L_ENA */
#define WM8994_AIF1ADC2L_ENA_WIDTH                   1  /* AIF1ADC2L_ENA */
#define WM8994_AIF1ADC2R_ENA                    0x0400  /* AIF1ADC2R_ENA */
#define WM8994_AIF1ADC2R_ENA_MASK               0x0400  /* AIF1ADC2R_ENA */
#define WM8994_AIF1ADC2R_ENA_SHIFT                  10  /* AIF1ADC2R_ENA */
#define WM8994_AIF1ADC2R_ENA_WIDTH                   1  /* AIF1ADC2R_ENA */
#define WM8994_AIF1ADC1L_ENA                    0x0200  /* AIF1ADC1L_ENA */
#define WM8994_AIF1ADC1L_ENA_MASK               0x0200  /* AIF1ADC1L_ENA */
#define WM8994_AIF1ADC1L_ENA_SHIFT                   9  /* AIF1ADC1L_ENA */
#define WM8994_AIF1ADC1L_ENA_WIDTH                   1  /* AIF1ADC1L_ENA */
#define WM8994_AIF1ADC1R_ENA                    0x0100  /* AIF1ADC1R_ENA */
#define WM8994_AIF1ADC1R_ENA_MASK               0x0100  /* AIF1ADC1R_ENA */
#define WM8994_AIF1ADC1R_ENA_SHIFT                   8  /* AIF1ADC1R_ENA */
#define WM8994_AIF1ADC1R_ENA_WIDTH                   1  /* AIF1ADC1R_ENA */
#define WM8994_DMIC2L_ENA                       0x0020  /* DMIC2L_ENA */
#define WM8994_DMIC2L_ENA_MASK                  0x0020  /* DMIC2L_ENA */
#define WM8994_DMIC2L_ENA_SHIFT                      5  /* DMIC2L_ENA */
#define WM8994_DMIC2L_ENA_WIDTH                      1  /* DMIC2L_ENA */
#define WM8994_DMIC2R_ENA                       0x0010  /* DMIC2R_ENA */
#define WM8994_DMIC2R_ENA_MASK                  0x0010  /* DMIC2R_ENA */
#define WM8994_DMIC2R_ENA_SHIFT                      4  /* DMIC2R_ENA */
#define WM8994_DMIC2R_ENA_WIDTH                      1  /* DMIC2R_ENA */
#define WM8994_DMIC1L_ENA                       0x0008  /* DMIC1L_ENA */
#define WM8994_DMIC1L_ENA_MASK                  0x0008  /* DMIC1L_ENA */
#define WM8994_DMIC1L_ENA_SHIFT                      3  /* DMIC1L_ENA */
#define WM8994_DMIC1L_ENA_WIDTH                      1  /* DMIC1L_ENA */
#define WM8994_DMIC1R_ENA                       0x0004  /* DMIC1R_ENA */
#define WM8994_DMIC1R_ENA_MASK                  0x0004  /* DMIC1R_ENA */
#define WM8994_DMIC1R_ENA_SHIFT                      2  /* DMIC1R_ENA */
#define WM8994_DMIC1R_ENA_WIDTH                      1  /* DMIC1R_ENA */
#define WM8994_ADCL_ENA                         0x0002  /* ADCL_ENA */
#define WM8994_ADCL_ENA_MASK                    0x0002  /* ADCL_ENA */
#define WM8994_ADCL_ENA_SHIFT                        1  /* ADCL_ENA */
#define WM8994_ADCL_ENA_WIDTH                        1  /* ADCL_ENA */
#define WM8994_ADCR_ENA                         0x0001  /* ADCR_ENA */
#define WM8994_ADCR_ENA_MASK                    0x0001  /* ADCR_ENA */
#define WM8994_ADCR_ENA_SHIFT                        0  /* ADCR_ENA */
#define WM8994_ADCR_ENA_WIDTH                        1  /* ADCR_ENA */

/*
 * R5 (0x05) - Power Management (5)
 */
#define WM8994_AIF2DACL_ENA                     0x2000  /* AIF2DACL_ENA */
#define WM8994_AIF2DACL_ENA_MASK                0x2000  /* AIF2DACL_ENA */
#define WM8994_AIF2DACL_ENA_SHIFT                   13  /* AIF2DACL_ENA */
#define WM8994_AIF2DACL_ENA_WIDTH                    1  /* AIF2DACL_ENA */
#define WM8994_AIF2DACR_ENA                     0x1000  /* AIF2DACR_ENA */
#define WM8994_AIF2DACR_ENA_MASK                0x1000  /* AIF2DACR_ENA */
#define WM8994_AIF2DACR_ENA_SHIFT                   12  /* AIF2DACR_ENA */
#define WM8994_AIF2DACR_ENA_WIDTH                    1  /* AIF2DACR_ENA */
#define WM8994_AIF1DAC2L_ENA                    0x0800  /* AIF1DAC2L_ENA */
#define WM8994_AIF1DAC2L_ENA_MASK               0x0800  /* AIF1DAC2L_ENA */
#define WM8994_AIF1DAC2L_ENA_SHIFT                  11  /* AIF1DAC2L_ENA */
#define WM8994_AIF1DAC2L_ENA_WIDTH                   1  /* AIF1DAC2L_ENA */
#define WM8994_AIF1DAC2R_ENA                    0x0400  /* AIF1DAC2R_ENA */
#define WM8994_AIF1DAC2R_ENA_MASK               0x0400  /* AIF1DAC2R_ENA */
#define WM8994_AIF1DAC2R_ENA_SHIFT                  10  /* AIF1DAC2R_ENA */
#define WM8994_AIF1DAC2R_ENA_WIDTH                   1  /* AIF1DAC2R_ENA */
#define WM8994_AIF1DAC1L_ENA                    0x0200  /* AIF1DAC1L_ENA */
#define WM8994_AIF1DAC1L_ENA_MASK               0x0200  /* AIF1DAC1L_ENA */
#define WM8994_AIF1DAC1L_ENA_SHIFT                   9  /* AIF1DAC1L_ENA */
#define WM8994_AIF1DAC1L_ENA_WIDTH                   1  /* AIF1DAC1L_ENA */
#define WM8994_AIF1DAC1R_ENA                    0x0100  /* AIF1DAC1R_ENA */
#define WM8994_AIF1DAC1R_ENA_MASK               0x0100  /* AIF1DAC1R_ENA */
#define WM8994_AIF1DAC1R_ENA_SHIFT                   8  /* AIF1DAC1R_ENA */
#define WM8994_AIF1DAC1R_ENA_WIDTH                   1  /* AIF1DAC1R_ENA */
#define WM8994_DAC2L_ENA                        0x0008  /* DAC2L_ENA */
#define WM8994_DAC2L_ENA_MASK                   0x0008  /* DAC2L_ENA */
#define WM8994_DAC2L_ENA_SHIFT                       3  /* DAC2L_ENA */
#define WM8994_DAC2L_ENA_WIDTH                       1  /* DAC2L_ENA */
#define WM8994_DAC2R_ENA                        0x0004  /* DAC2R_ENA */
#define WM8994_DAC2R_ENA_MASK                   0x0004  /* DAC2R_ENA */
#define WM8994_DAC2R_ENA_SHIFT                       2  /* DAC2R_ENA */
#define WM8994_DAC2R_ENA_WIDTH                       1  /* DAC2R_ENA */
#define WM8994_DAC1L_ENA                        0x0002  /* DAC1L_ENA */
#define WM8994_DAC1L_ENA_MASK                   0x0002  /* DAC1L_ENA */
#define WM8994_DAC1L_ENA_SHIFT                       1  /* DAC1L_ENA */
#define WM8994_DAC1L_ENA_WIDTH                       1  /* DAC1L_ENA */
#define WM8994_DAC1R_ENA                        0x0001  /* DAC1R_ENA */
#define WM8994_DAC1R_ENA_MASK                   0x0001  /* DAC1R_ENA */
#define WM8994_DAC1R_ENA_SHIFT                       0  /* DAC1R_ENA */
#define WM8994_DAC1R_ENA_WIDTH                       1  /* DAC1R_ENA */

/*
 * R6 (0x06) - Power Management (6)
 */
#define WM8958_AIF3ADC_SRC_MASK                 0x0600  /* AIF3ADC_SRC - [10:9] */
#define WM8958_AIF3ADC_SRC_SHIFT                     9  /* AIF3ADC_SRC - [10:9] */
#define WM8958_AIF3ADC_SRC_WIDTH                     2  /* AIF3ADC_SRC - [10:9] */
#define WM8958_AIF2DAC_SRC_MASK                 0x0180  /* AIF2DAC_SRC - [8:7] */
#define WM8958_AIF2DAC_SRC_SHIFT                     7  /* AIF2DAC_SRC - [8:7] */
#define WM8958_AIF2DAC_SRC_WIDTH                     2  /* AIF2DAC_SRC - [8:7] */
#define WM8994_AIF3_TRI                         0x0020  /* AIF3_TRI */
#define WM8994_AIF3_TRI_MASK                    0x0020  /* AIF3_TRI */
#define WM8994_AIF3_TRI_SHIFT                        5  /* AIF3_TRI */
#define WM8994_AIF3_TRI_WIDTH                        1  /* AIF3_TRI */
#define WM8994_AIF3_ADCDAT_SRC_MASK             0x0018  /* AIF3_ADCDAT_SRC - [4:3] */
#define WM8994_AIF3_ADCDAT_SRC_SHIFT                 3  /* AIF3_ADCDAT_SRC - [4:3] */
#define WM8994_AIF3_ADCDAT_SRC_WIDTH                 2  /* AIF3_ADCDAT_SRC - [4:3] */
#define WM8994_AIF2_ADCDAT_SRC                  0x0004  /* AIF2_ADCDAT_SRC */
#define WM8994_AIF2_ADCDAT_SRC_MASK             0x0004  /* AIF2_ADCDAT_SRC */
#define WM8994_AIF2_ADCDAT_SRC_SHIFT                 2  /* AIF2_ADCDAT_SRC */
#define WM8994_AIF2_ADCDAT_SRC_WIDTH                 1  /* AIF2_ADCDAT_SRC */
#define WM8994_AIF2_DACDAT_SRC                  0x0002  /* AIF2_DACDAT_SRC */
#define WM8994_AIF2_DACDAT_SRC_MASK             0x0002  /* AIF2_DACDAT_SRC */
#define WM8994_AIF2_DACDAT_SRC_SHIFT                 1  /* AIF2_DACDAT_SRC */
#define WM8994_AIF2_DACDAT_SRC_WIDTH                 1  /* AIF2_DACDAT_SRC */
#define WM8994_AIF1_DACDAT_SRC                  0x0001  /* AIF1_DACDAT_SRC */
#define WM8994_AIF1_DACDAT_SRC_MASK             0x0001  /* AIF1_DACDAT_SRC */
#define WM8994_AIF1_DACDAT_SRC_SHIFT                 0  /* AIF1_DACDAT_SRC */
#define WM8994_AIF1_DACDAT_SRC_WIDTH                 1  /* AIF1_DACDAT_SRC */

/*
 * R21 (0x15) - Input Mixer (1)
 */
#define WM8994_IN1RP_MIXINR_BOOST               0x0100  /* IN1RP_MIXINR_BOOST */
#define WM8994_IN1RP_MIXINR_BOOST_MASK          0x0100  /* IN1RP_MIXINR_BOOST */
#define WM8994_IN1RP_MIXINR_BOOST_SHIFT              8  /* IN1RP_MIXINR_BOOST */
#define WM8994_IN1RP_MIXINR_BOOST_WIDTH              1  /* IN1RP_MIXINR_BOOST */
#define WM8994_IN1LP_MIXINL_BOOST               0x0080  /* IN1LP_MIXINL_BOOST */
#define WM8994_IN1LP_MIXINL_BOOST_MASK          0x0080  /* IN1LP_MIXINL_BOOST */
#define WM8994_IN1LP_MIXINL_BOOST_SHIFT              7  /* IN1LP_MIXINL_BOOST */
#define WM8994_IN1LP_MIXINL_BOOST_WIDTH              1  /* IN1LP_MIXINL_BOOST */
#define WM8994_INPUTS_CLAMP                     0x0040  /* INPUTS_CLAMP */
#define WM8994_INPUTS_CLAMP_MASK                0x0040  /* INPUTS_CLAMP */
#define WM8994_INPUTS_CLAMP_SHIFT                    6  /* INPUTS_CLAMP */
#define WM8994_INPUTS_CLAMP_WIDTH                    1  /* INPUTS_CLAMP */

/*
 * R24 (0x18) - Left Line Input 1&2 Volume
 */
#define WM8994_IN1_VU                           0x0100  /* IN1_VU */
#define WM8994_IN1_VU_MASK                      0x0100  /* IN1_VU */
#define WM8994_IN1_VU_SHIFT                          8  /* IN1_VU */
#define WM8994_IN1_VU_WIDTH                          1  /* IN1_VU */
#define WM8994_IN1L_MUTE                        0x0080  /* IN1L_MUTE */
#define WM8994_IN1L_MUTE_MASK                   0x0080  /* IN1L_MUTE */
#define WM8994_IN1L_MUTE_SHIFT                       7  /* IN1L_MUTE */
#define WM8994_IN1L_MUTE_WIDTH                       1  /* IN1L_MUTE */
#define WM8994_IN1L_ZC                          0x0040  /* IN1L_ZC */
#define WM8994_IN1L_ZC_MASK                     0x0040  /* IN1L_ZC */
#define WM8994_IN1L_ZC_SHIFT                         6  /* IN1L_ZC */
#define WM8994_IN1L_ZC_WIDTH                         1  /* IN1L_ZC */
#define WM8994_IN1L_VOL_MASK                    0x001F  /* IN1L_VOL - [4:0] */
#define WM8994_IN1L_VOL_SHIFT                        0  /* IN1L_VOL - [4:0] */
#define WM8994_IN1L_VOL_WIDTH                        5  /* IN1L_VOL - [4:0] */

/*
 * R25 (0x19) - Left Line Input 3&4 Volume
 */
#define WM8994_IN2_VU                           0x0100  /* IN2_VU */
#define WM8994_IN2_VU_MASK                      0x0100  /* IN2_VU */
#define WM8994_IN2_VU_SHIFT                          8  /* IN2_VU */
#define WM8994_IN2_VU_WIDTH                          1  /* IN2_VU */
#define WM8994_IN2L_MUTE                        0x0080  /* IN2L_MUTE */
#define WM8994_IN2L_MUTE_MASK                   0x0080  /* IN2L_MUTE */
#define WM8994_IN2L_MUTE_SHIFT                       7  /* IN2L_MUTE */
#define WM8994_IN2L_MUTE_WIDTH                       1  /* IN2L_MUTE */
#define WM8994_IN2L_ZC                          0x0040  /* IN2L_ZC */
#define WM8994_IN2L_ZC_MASK                     0x0040  /* IN2L_ZC */
#define WM8994_IN2L_ZC_SHIFT                         6  /* IN2L_ZC */
#define WM8994_IN2L_ZC_WIDTH                         1  /* IN2L_ZC */
#define WM8994_IN2L_VOL_MASK                    0x001F  /* IN2L_VOL - [4:0] */
#define WM8994_IN2L_VOL_SHIFT                        0  /* IN2L_VOL - [4:0] */
#define WM8994_IN2L_VOL_WIDTH                        5  /* IN2L_VOL - [4:0] */

/*
 * R26 (0x1A) - Right Line Input 1&2 Volume
 */
#define WM8994_IN1_VU                           0x0100  /* IN1_VU */
#define WM8994_IN1_VU_MASK                      0x0100  /* IN1_VU */
#define WM8994_IN1_VU_SHIFT                          8  /* IN1_VU */
#define WM8994_IN1_VU_WIDTH                          1  /* IN1_VU */
#define WM8994_IN1R_MUTE                        0x0080  /* IN1R_MUTE */
#define WM8994_IN1R_MUTE_MASK                   0x0080  /* IN1R_MUTE */
#define WM8994_IN1R_MUTE_SHIFT                       7  /* IN1R_MUTE */
#define WM8994_IN1R_MUTE_WIDTH                       1  /* IN1R_MUTE */
#define WM8994_IN1R_ZC                          0x0040  /* IN1R_ZC */
#define WM8994_IN1R_ZC_MASK                     0x0040  /* IN1R_ZC */
#define WM8994_IN1R_ZC_SHIFT                         6  /* IN1R_ZC */
#define WM8994_IN1R_ZC_WIDTH                         1  /* IN1R_ZC */
#define WM8994_IN1R_VOL_MASK                    0x001F  /* IN1R_VOL - [4:0] */
#define WM8994_IN1R_VOL_SHIFT                        0  /* IN1R_VOL - [4:0] */
#define WM8994_IN1R_VOL_WIDTH                        5  /* IN1R_VOL - [4:0] */

/*
 * R27 (0x1B) - Right Line Input 3&4 Volume
 */
#define WM8994_IN2_VU                           0x0100  /* IN2_VU */
#define WM8994_IN2_VU_MASK                      0x0100  /* IN2_VU */
#define WM8994_IN2_VU_SHIFT                          8  /* IN2_VU */
#define WM8994_IN2_VU_WIDTH                          1  /* IN2_VU */
#define WM8994_IN2R_MUTE                        0x0080  /* IN2R_MUTE */
#define WM8994_IN2R_MUTE_MASK                   0x0080  /* IN2R_MUTE */
#define WM8994_IN2R_MUTE_SHIFT                       7  /* IN2R_MUTE */
#define WM8994_IN2R_MUTE_WIDTH                       1  /* IN2R_MUTE */
#define WM8994_IN2R_ZC                          0x0040  /* IN2R_ZC */
#define WM8994_IN2R_ZC_MASK                     0x0040  /* IN2R_ZC */
#define WM8994_IN2R_ZC_SHIFT                         6  /* IN2R_ZC */
#define WM8994_IN2R_ZC_WIDTH                         1  /* IN2R_ZC */
#define WM8994_IN2R_VOL_MASK                    0x001F  /* IN2R_VOL - [4:0] */
#define WM8994_IN2R_VOL_SHIFT                        0  /* IN2R_VOL - [4:0] */
#define WM8994_IN2R_VOL_WIDTH                        5  /* IN2R_VOL - [4:0] */

/*
 * R28 (0x1C) - Left Output Volume
 */
#define WM8994_HPOUT1_VU                        0x0100  /* HPOUT1_VU */
#define WM8994_HPOUT1_VU_MASK                   0x0100  /* HPOUT1_VU */
#define WM8994_HPOUT1_VU_SHIFT                       8  /* HPOUT1_VU */
#define WM8994_HPOUT1_VU_WIDTH                       1  /* HPOUT1_VU */
#define WM8994_HPOUT1L_ZC                       0x0080  /* HPOUT1L_ZC */
#define WM8994_HPOUT1L_ZC_MASK                  0x0080  /* HPOUT1L_ZC */
#define WM8994_HPOUT1L_ZC_SHIFT                      7  /* HPOUT1L_ZC */
#define WM8994_HPOUT1L_ZC_WIDTH                      1  /* HPOUT1L_ZC */
#define WM8994_HPOUT1L_MUTE_N                   0x0040  /* HPOUT1L_MUTE_N */
#define WM8994_HPOUT1L_MUTE_N_MASK              0x0040  /* HPOUT1L_MUTE_N */
#define WM8994_HPOUT1L_MUTE_N_SHIFT                  6  /* HPOUT1L_MUTE_N */
#define WM8994_HPOUT1L_MUTE_N_WIDTH                  1  /* HPOUT1L_MUTE_N */
#define WM8994_HPOUT1L_VOL_MASK                 0x003F  /* HPOUT1L_VOL - [5:0] */
#define WM8994_HPOUT1L_VOL_SHIFT                     0  /* HPOUT1L_VOL - [5:0] */
#define WM8994_HPOUT1L_VOL_WIDTH                     6  /* HPOUT1L_VOL - [5:0] */

/*
 * R29 (0x1D) - Right Output Volume
 */
#define WM8994_HPOUT1_VU                        0x0100  /* HPOUT1_VU */
#define WM8994_HPOUT1_VU_MASK                   0x0100  /* HPOUT1_VU */
#define WM8994_HPOUT1_VU_SHIFT                       8  /* HPOUT1_VU */
#define WM8994_HPOUT1_VU_WIDTH                       1  /* HPOUT1_VU */
#define WM8994_HPOUT1R_ZC                       0x0080  /* HPOUT1R_ZC */
#define WM8994_HPOUT1R_ZC_MASK                  0x0080  /* HPOUT1R_ZC */
#define WM8994_HPOUT1R_ZC_SHIFT                      7  /* HPOUT1R_ZC */
#define WM8994_HPOUT1R_ZC_WIDTH                      1  /* HPOUT1R_ZC */
#define WM8994_HPOUT1R_MUTE_N                   0x0040  /* HPOUT1R_MUTE_N */
#define WM8994_HPOUT1R_MUTE_N_MASK              0x0040  /* HPOUT1R_MUTE_N */
#define WM8994_HPOUT1R_MUTE_N_SHIFT                  6  /* HPOUT1R_MUTE_N */
#define WM8994_HPOUT1R_MUTE_N_WIDTH                  1  /* HPOUT1R_MUTE_N */
#define WM8994_HPOUT1R_VOL_MASK                 0x003F  /* HPOUT1R_VOL - [5:0] */
#define WM8994_HPOUT1R_VOL_SHIFT                     0  /* HPOUT1R_VOL - [5:0] */
#define WM8994_HPOUT1R_VOL_WIDTH                     6  /* HPOUT1R_VOL - [5:0] */

/*
 * R30 (0x1E) - Line Outputs Volume
 */
#define WM8994_LINEOUT1N_MUTE                   0x0040  /* LINEOUT1N_MUTE */
#define WM8994_LINEOUT1N_MUTE_MASK              0x0040  /* LINEOUT1N_MUTE */
#define WM8994_LINEOUT1N_MUTE_SHIFT                  6  /* LINEOUT1N_MUTE */
#define WM8994_LINEOUT1N_MUTE_WIDTH                  1  /* LINEOUT1N_MUTE */
#define WM8994_LINEOUT1P_MUTE                   0x0020  /* LINEOUT1P_MUTE */
#define WM8994_LINEOUT1P_MUTE_MASK              0x0020  /* LINEOUT1P_MUTE */
#define WM8994_LINEOUT1P_MUTE_SHIFT                  5  /* LINEOUT1P_MUTE */
#define WM8994_LINEOUT1P_MUTE_WIDTH                  1  /* LINEOUT1P_MUTE */
#define WM8994_LINEOUT1_VOL                     0x0010  /* LINEOUT1_VOL */
#define WM8994_LINEOUT1_VOL_MASK                0x0010  /* LINEOUT1_VOL */
#define WM8994_LINEOUT1_VOL_SHIFT                    4  /* LINEOUT1_VOL */
#define WM8994_LINEOUT1_VOL_WIDTH                    1  /* LINEOUT1_VOL */
#define WM8994_LINEOUT2N_MUTE                   0x0004  /* LINEOUT2N_MUTE */
#define WM8994_LINEOUT2N_MUTE_MASK              0x0004  /* LINEOUT2N_MUTE */
#define WM8994_LINEOUT2N_MUTE_SHIFT                  2  /* LINEOUT2N_MUTE */
#define WM8994_LINEOUT2N_MUTE_WIDTH                  1  /* LINEOUT2N_MUTE */
#define WM8994_LINEOUT2P_MUTE                   0x0002  /* LINEOUT2P_MUTE */
#define WM8994_LINEOUT2P_MUTE_MASK              0x0002  /* LINEOUT2P_MUTE */
#define WM8994_LINEOUT2P_MUTE_SHIFT                  1  /* LINEOUT2P_MUTE */
#define WM8994_LINEOUT2P_MUTE_WIDTH                  1  /* LINEOUT2P_MUTE */
#define WM8994_LINEOUT2_VOL                     0x0001  /* LINEOUT2_VOL */
#define WM8994_LINEOUT2_VOL_MASK                0x0001  /* LINEOUT2_VOL */
#define WM8994_LINEOUT2_VOL_SHIFT                    0  /* LINEOUT2_VOL */
#define WM8994_LINEOUT2_VOL_WIDTH                    1  /* LINEOUT2_VOL */

/*
 * R31 (0x1F) - HPOUT2 Volume
 */
#define WM8994_HPOUT2_MUTE                      0x0020  /* HPOUT2_MUTE */
#define WM8994_HPOUT2_MUTE_MASK                 0x0020  /* HPOUT2_MUTE */
#define WM8994_HPOUT2_MUTE_SHIFT                     5  /* HPOUT2_MUTE */
#define WM8994_HPOUT2_MUTE_WIDTH                     1  /* HPOUT2_MUTE */
#define WM8994_HPOUT2_VOL                       0x0010  /* HPOUT2_VOL */
#define WM8994_HPOUT2_VOL_MASK                  0x0010  /* HPOUT2_VOL */
#define WM8994_HPOUT2_VOL_SHIFT                      4  /* HPOUT2_VOL */
#define WM8994_HPOUT2_VOL_WIDTH                      1  /* HPOUT2_VOL */

/*
 * R32 (0x20) - Left OPGA Volume
 */
#define WM8994_MIXOUT_VU                        0x0100  /* MIXOUT_VU */
#define WM8994_MIXOUT_VU_MASK                   0x0100  /* MIXOUT_VU */
#define WM8994_MIXOUT_VU_SHIFT                       8  /* MIXOUT_VU */
#define WM8994_MIXOUT_VU_WIDTH                       1  /* MIXOUT_VU */
#define WM8994_MIXOUTL_ZC                       0x0080  /* MIXOUTL_ZC */
#define WM8994_MIXOUTL_ZC_MASK                  0x0080  /* MIXOUTL_ZC */
#define WM8994_MIXOUTL_ZC_SHIFT                      7  /* MIXOUTL_ZC */
#define WM8994_MIXOUTL_ZC_WIDTH                      1  /* MIXOUTL_ZC */
#define WM8994_MIXOUTL_MUTE_N                   0x0040  /* MIXOUTL_MUTE_N */
#define WM8994_MIXOUTL_MUTE_N_MASK              0x0040  /* MIXOUTL_MUTE_N */
#define WM8994_MIXOUTL_MUTE_N_SHIFT                  6  /* MIXOUTL_MUTE_N */
#define WM8994_MIXOUTL_MUTE_N_WIDTH                  1  /* MIXOUTL_MUTE_N */
#define WM8994_MIXOUTL_VOL_MASK                 0x003F  /* MIXOUTL_VOL - [5:0] */
#define WM8994_MIXOUTL_VOL_SHIFT                     0  /* MIXOUTL_VOL - [5:0] */
#define WM8994_MIXOUTL_VOL_WIDTH                     6  /* MIXOUTL_VOL - [5:0] */

/*
 * R33 (0x21) - Right OPGA Volume
 */
#define WM8994_MIXOUT_VU                        0x0100  /* MIXOUT_VU */
#define WM8994_MIXOUT_VU_MASK                   0x0100  /* MIXOUT_VU */
#define WM8994_MIXOUT_VU_SHIFT                       8  /* MIXOUT_VU */
#define WM8994_MIXOUT_VU_WIDTH                       1  /* MIXOUT_VU */
#define WM8994_MIXOUTR_ZC                       0x0080  /* MIXOUTR_ZC */
#define WM8994_MIXOUTR_ZC_MASK                  0x0080  /* MIXOUTR_ZC */
#define WM8994_MIXOUTR_ZC_SHIFT                      7  /* MIXOUTR_ZC */
#define WM8994_MIXOUTR_ZC_WIDTH                      1  /* MIXOUTR_ZC */
#define WM8994_MIXOUTR_MUTE_N                   0x0040  /* MIXOUTR_MUTE_N */
#define WM8994_MIXOUTR_MUTE_N_MASK              0x0040  /* MIXOUTR_MUTE_N */
#define WM8994_MIXOUTR_MUTE_N_SHIFT                  6  /* MIXOUTR_MUTE_N */
#define WM8994_MIXOUTR_MUTE_N_WIDTH                  1  /* MIXOUTR_MUTE_N */
#define WM8994_MIXOUTR_VOL_MASK                 0x003F  /* MIXOUTR_VOL - [5:0] */
#define WM8994_MIXOUTR_VOL_SHIFT                     0  /* MIXOUTR_VOL - [5:0] */
#define WM8994_MIXOUTR_VOL_WIDTH                     6  /* MIXOUTR_VOL - [5:0] */

/*
 * R34 (0x22) - SPKMIXL Attenuation
 */
#define WM8994_DAC2L_SPKMIXL_VOL                0x0040  /* DAC2L_SPKMIXL_VOL */
#define WM8994_DAC2L_SPKMIXL_VOL_MASK           0x0040  /* DAC2L_SPKMIXL_VOL */
#define WM8994_DAC2L_SPKMIXL_VOL_SHIFT               6  /* DAC2L_SPKMIXL_VOL */
#define WM8994_DAC2L_SPKMIXL_VOL_WIDTH               1  /* DAC2L_SPKMIXL_VOL */
#define WM8994_MIXINL_SPKMIXL_VOL               0x0020  /* MIXINL_SPKMIXL_VOL */
#define WM8994_MIXINL_SPKMIXL_VOL_MASK          0x0020  /* MIXINL_SPKMIXL_VOL */
#define WM8994_MIXINL_SPKMIXL_VOL_SHIFT              5  /* MIXINL_SPKMIXL_VOL */
#define WM8994_MIXINL_SPKMIXL_VOL_WIDTH              1  /* MIXINL_SPKMIXL_VOL */
#define WM8994_IN1LP_SPKMIXL_VOL                0x0010  /* IN1LP_SPKMIXL_VOL */
#define WM8994_IN1LP_SPKMIXL_VOL_MASK           0x0010  /* IN1LP_SPKMIXL_VOL */
#define WM8994_IN1LP_SPKMIXL_VOL_SHIFT               4  /* IN1LP_SPKMIXL_VOL */
#define WM8994_IN1LP_SPKMIXL_VOL_WIDTH               1  /* IN1LP_SPKMIXL_VOL */
#define WM8994_MIXOUTL_SPKMIXL_VOL              0x0008  /* MIXOUTL_SPKMIXL_VOL */
#define WM8994_MIXOUTL_SPKMIXL_VOL_MASK         0x0008  /* MIXOUTL_SPKMIXL_VOL */
#define WM8994_MIXOUTL_SPKMIXL_VOL_SHIFT             3  /* MIXOUTL_SPKMIXL_VOL */
#define WM8994_MIXOUTL_SPKMIXL_VOL_WIDTH             1  /* MIXOUTL_SPKMIXL_VOL */
#define WM8994_DAC1L_SPKMIXL_VOL                0x0004  /* DAC1L_SPKMIXL_VOL */
#define WM8994_DAC1L_SPKMIXL_VOL_MASK           0x0004  /* DAC1L_SPKMIXL_VOL */
#define WM8994_DAC1L_SPKMIXL_VOL_SHIFT               2  /* DAC1L_SPKMIXL_VOL */
#define WM8994_DAC1L_SPKMIXL_VOL_WIDTH               1  /* DAC1L_SPKMIXL_VOL */
#define WM8994_SPKMIXL_VOL_MASK                 0x0003  /* SPKMIXL_VOL - [1:0] */
#define WM8994_SPKMIXL_VOL_SHIFT                     0  /* SPKMIXL_VOL - [1:0] */
#define WM8994_SPKMIXL_VOL_WIDTH                     2  /* SPKMIXL_VOL - [1:0] */

/*
 * R35 (0x23) - SPKMIXR Attenuation
 */
#define WM8994_SPKOUT_CLASSAB                   0x0100  /* SPKOUT_CLASSAB */
#define WM8994_SPKOUT_CLASSAB_MASK              0x0100  /* SPKOUT_CLASSAB */
#define WM8994_SPKOUT_CLASSAB_SHIFT                  8  /* SPKOUT_CLASSAB */
#define WM8994_SPKOUT_CLASSAB_WIDTH                  1  /* SPKOUT_CLASSAB */
#define WM8994_DAC2R_SPKMIXR_VOL                0x0040  /* DAC2R_SPKMIXR_VOL */
#define WM8994_DAC2R_SPKMIXR_VOL_MASK           0x0040  /* DAC2R_SPKMIXR_VOL */
#define WM8994_DAC2R_SPKMIXR_VOL_SHIFT               6  /* DAC2R_SPKMIXR_VOL */
#define WM8994_DAC2R_SPKMIXR_VOL_WIDTH               1  /* DAC2R_SPKMIXR_VOL */
#define WM8994_MIXINR_SPKMIXR_VOL               0x0020  /* MIXINR_SPKMIXR_VOL */
#define WM8994_MIXINR_SPKMIXR_VOL_MASK          0x0020  /* MIXINR_SPKMIXR_VOL */
#define WM8994_MIXINR_SPKMIXR_VOL_SHIFT              5  /* MIXINR_SPKMIXR_VOL */
#define WM8994_MIXINR_SPKMIXR_VOL_WIDTH              1  /* MIXINR_SPKMIXR_VOL */
#define WM8994_IN1RP_SPKMIXR_VOL                0x0010  /* IN1RP_SPKMIXR_VOL */
#define WM8994_IN1RP_SPKMIXR_VOL_MASK           0x0010  /* IN1RP_SPKMIXR_VOL */
#define WM8994_IN1RP_SPKMIXR_VOL_SHIFT               4  /* IN1RP_SPKMIXR_VOL */
#define WM8994_IN1RP_SPKMIXR_VOL_WIDTH               1  /* IN1RP_SPKMIXR_VOL */
#define WM8994_MIXOUTR_SPKMIXR_VOL              0x0008  /* MIXOUTR_SPKMIXR_VOL */
#define WM8994_MIXOUTR_SPKMIXR_VOL_MASK         0x0008  /* MIXOUTR_SPKMIXR_VOL */
#define WM8994_MIXOUTR_SPKMIXR_VOL_SHIFT             3  /* MIXOUTR_SPKMIXR_VOL */
#define WM8994_MIXOUTR_SPKMIXR_VOL_WIDTH             1  /* MIXOUTR_SPKMIXR_VOL */
#define WM8994_DAC1R_SPKMIXR_VOL                0x0004  /* DAC1R_SPKMIXR_VOL */
#define WM8994_DAC1R_SPKMIXR_VOL_MASK           0x0004  /* DAC1R_SPKMIXR_VOL */
#define WM8994_DAC1R_SPKMIXR_VOL_SHIFT               2  /* DAC1R_SPKMIXR_VOL */
#define WM8994_DAC1R_SPKMIXR_VOL_WIDTH               1  /* DAC1R_SPKMIXR_VOL */
#define WM8994_SPKMIXR_VOL_MASK                 0x0003  /* SPKMIXR_VOL - [1:0] */
#define WM8994_SPKMIXR_VOL_SHIFT                     0  /* SPKMIXR_VOL - [1:0] */
#define WM8994_SPKMIXR_VOL_WIDTH                     2  /* SPKMIXR_VOL - [1:0] */

/*
 * R36 (0x24) - SPKOUT Mixers
 */
#define WM8994_IN2LRP_TO_SPKOUTL                0x0020  /* IN2LRP_TO_SPKOUTL */
#define WM8994_IN2LRP_TO_SPKOUTL_MASK           0x0020  /* IN2LRP_TO_SPKOUTL */
#define WM8994_IN2LRP_TO_SPKOUTL_SHIFT               5  /* IN2LRP_TO_SPKOUTL */
#define WM8994_IN2LRP_TO_SPKOUTL_WIDTH               1  /* IN2LRP_TO_SPKOUTL */
#define WM8994_SPKMIXL_TO_SPKOUTL               0x0010  /* SPKMIXL_TO_SPKOUTL */
#define WM8994_SPKMIXL_TO_SPKOUTL_MASK          0x0010  /* SPKMIXL_TO_SPKOUTL */
#define WM8994_SPKMIXL_TO_SPKOUTL_SHIFT              4  /* SPKMIXL_TO_SPKOUTL */
#define WM8994_SPKMIXL_TO_SPKOUTL_WIDTH              1  /* SPKMIXL_TO_SPKOUTL */
#define WM8994_SPKMIXR_TO_SPKOUTL               0x0008  /* SPKMIXR_TO_SPKOUTL */
#define WM8994_SPKMIXR_TO_SPKOUTL_MASK          0x0008  /* SPKMIXR_TO_SPKOUTL */
#define WM8994_SPKMIXR_TO_SPKOUTL_SHIFT              3  /* SPKMIXR_TO_SPKOUTL */
#define WM8994_SPKMIXR_TO_SPKOUTL_WIDTH              1  /* SPKMIXR_TO_SPKOUTL */
#define WM8994_IN2LRP_TO_SPKOUTR                0x0004  /* IN2LRP_TO_SPKOUTR */
#define WM8994_IN2LRP_TO_SPKOUTR_MASK           0x0004  /* IN2LRP_TO_SPKOUTR */
#define WM8994_IN2LRP_TO_SPKOUTR_SHIFT               2  /* IN2LRP_TO_SPKOUTR */
#define WM8994_IN2LRP_TO_SPKOUTR_WIDTH               1  /* IN2LRP_TO_SPKOUTR */
#define WM8994_SPKMIXL_TO_SPKOUTR               0x0002  /* SPKMIXL_TO_SPKOUTR */
#define WM8994_SPKMIXL_TO_SPKOUTR_MASK          0x0002  /* SPKMIXL_TO_SPKOUTR */
#define WM8994_SPKMIXL_TO_SPKOUTR_SHIFT              1  /* SPKMIXL_TO_SPKOUTR */
#define WM8994_SPKMIXL_TO_SPKOUTR_WIDTH              1  /* SPKMIXL_TO_SPKOUTR */
#define WM8994_SPKMIXR_TO_SPKOUTR               0x0001  /* SPKMIXR_TO_SPKOUTR */
#define WM8994_SPKMIXR_TO_SPKOUTR_MASK          0x0001  /* SPKMIXR_TO_SPKOUTR */
#define WM8994_SPKMIXR_TO_SPKOUTR_SHIFT              0  /* SPKMIXR_TO_SPKOUTR */
#define WM8994_SPKMIXR_TO_SPKOUTR_WIDTH              1  /* SPKMIXR_TO_SPKOUTR */

/*
 * R37 (0x25) - ClassD
 */
#define WM8994_SPKOUTL_BOOST_MASK               0x0038  /* SPKOUTL_BOOST - [5:3] */
#define WM8994_SPKOUTL_BOOST_SHIFT                   3  /* SPKOUTL_BOOST - [5:3] */
#define WM8994_SPKOUTL_BOOST_WIDTH                   3  /* SPKOUTL_BOOST - [5:3] */
#define WM8994_SPKOUTR_BOOST_MASK               0x0007  /* SPKOUTR_BOOST - [2:0] */
#define WM8994_SPKOUTR_BOOST_SHIFT                   0  /* SPKOUTR_BOOST - [2:0] */
#define WM8994_SPKOUTR_BOOST_WIDTH                   3  /* SPKOUTR_BOOST - [2:0] */

/*
 * R38 (0x26) - Speaker Volume Left
 */
#define WM8994_SPKOUT_VU                        0x0100  /* SPKOUT_VU */
#define WM8994_SPKOUT_VU_MASK                   0x0100  /* SPKOUT_VU */
#define WM8994_SPKOUT_VU_SHIFT                       8  /* SPKOUT_VU */
#define WM8994_SPKOUT_VU_WIDTH                       1  /* SPKOUT_VU */
#define WM8994_SPKOUTL_ZC                       0x0080  /* SPKOUTL_ZC */
#define WM8994_SPKOUTL_ZC_MASK                  0x0080  /* SPKOUTL_ZC */
#define WM8994_SPKOUTL_ZC_SHIFT                      7  /* SPKOUTL_ZC */
#define WM8994_SPKOUTL_ZC_WIDTH                      1  /* SPKOUTL_ZC */
#define WM8994_SPKOUTL_MUTE_N                   0x0040  /* SPKOUTL_MUTE_N */
#define WM8994_SPKOUTL_MUTE_N_MASK              0x0040  /* SPKOUTL_MUTE_N */
#define WM8994_SPKOUTL_MUTE_N_SHIFT                  6  /* SPKOUTL_MUTE_N */
#define WM8994_SPKOUTL_MUTE_N_WIDTH                  1  /* SPKOUTL_MUTE_N */
#define WM8994_SPKOUTL_VOL_MASK                 0x003F  /* SPKOUTL_VOL - [5:0] */
#define WM8994_SPKOUTL_VOL_SHIFT                     0  /* SPKOUTL_VOL - [5:0] */
#define WM8994_SPKOUTL_VOL_WIDTH                     6  /* SPKOUTL_VOL - [5:0] */

/*
 * R39 (0x27) - Speaker Volume Right
 */
#define WM8994_SPKOUT_VU                        0x0100  /* SPKOUT_VU */
#define WM8994_SPKOUT_VU_MASK                   0x0100  /* SPKOUT_VU */
#define WM8994_SPKOUT_VU_SHIFT                       8  /* SPKOUT_VU */
#define WM8994_SPKOUT_VU_WIDTH                       1  /* SPKOUT_VU */
#define WM8994_SPKOUTR_ZC                       0x0080  /* SPKOUTR_ZC */
#define WM8994_SPKOUTR_ZC_MASK                  0x0080  /* SPKOUTR_ZC */
#define WM8994_SPKOUTR_ZC_SHIFT                      7  /* SPKOUTR_ZC */
#define WM8994_SPKOUTR_ZC_WIDTH                      1  /* SPKOUTR_ZC */
#define WM8994_SPKOUTR_MUTE_N                   0x0040  /* SPKOUTR_MUTE_N */
#define WM8994_SPKOUTR_MUTE_N_MASK              0x0040  /* SPKOUTR_MUTE_N */
#define WM8994_SPKOUTR_MUTE_N_SHIFT                  6  /* SPKOUTR_MUTE_N */
#define WM8994_SPKOUTR_MUTE_N_WIDTH                  1  /* SPKOUTR_MUTE_N */
#define WM8994_SPKOUTR_VOL_MASK                 0x003F  /* SPKOUTR_VOL - [5:0] */
#define WM8994_SPKOUTR_VOL_SHIFT                     0  /* SPKOUTR_VOL - [5:0] */
#define WM8994_SPKOUTR_VOL_WIDTH                     6  /* SPKOUTR_VOL - [5:0] */

/*
 * R40 (0x28) - Input Mixer (2)
 */
#define WM8994_IN2LP_TO_IN2L                    0x0080  /* IN2LP_TO_IN2L */
#define WM8994_IN2LP_TO_IN2L_MASK               0x0080  /* IN2LP_TO_IN2L */
#define WM8994_IN2LP_TO_IN2L_SHIFT                   7  /* IN2LP_TO_IN2L */
#define WM8994_IN2LP_TO_IN2L_WIDTH                   1  /* IN2LP_TO_IN2L */
#define WM8994_IN2LN_TO_IN2L                    0x0040  /* IN2LN_TO_IN2L */
#define WM8994_IN2LN_TO_IN2L_MASK               0x0040  /* IN2LN_TO_IN2L */
#define WM8994_IN2LN_TO_IN2L_SHIFT                   6  /* IN2LN_TO_IN2L */
#define WM8994_IN2LN_TO_IN2L_WIDTH                   1  /* IN2LN_TO_IN2L */
#define WM8994_IN1LP_TO_IN1L                    0x0020  /* IN1LP_TO_IN1L */
#define WM8994_IN1LP_TO_IN1L_MASK               0x0020  /* IN1LP_TO_IN1L */
#define WM8994_IN1LP_TO_IN1L_SHIFT                   5  /* IN1LP_TO_IN1L */
#define WM8994_IN1LP_TO_IN1L_WIDTH                   1  /* IN1LP_TO_IN1L */
#define WM8994_IN1LN_TO_IN1L                    0x0010  /* IN1LN_TO_IN1L */
#define WM8994_IN1LN_TO_IN1L_MASK               0x0010  /* IN1LN_TO_IN1L */
#define WM8994_IN1LN_TO_IN1L_SHIFT                   4  /* IN1LN_TO_IN1L */
#define WM8994_IN1LN_TO_IN1L_WIDTH                   1  /* IN1LN_TO_IN1L */
#define WM8994_IN2RP_TO_IN2R                    0x0008  /* IN2RP_TO_IN2R */
#define WM8994_IN2RP_TO_IN2R_MASK               0x0008  /* IN2RP_TO_IN2R */
#define WM8994_IN2RP_TO_IN2R_SHIFT                   3  /* IN2RP_TO_IN2R */
#define WM8994_IN2RP_TO_IN2R_WIDTH                   1  /* IN2RP_TO_IN2R */
#define WM8994_IN2RN_TO_IN2R                    0x0004  /* IN2RN_TO_IN2R */
#define WM8994_IN2RN_TO_IN2R_MASK               0x0004  /* IN2RN_TO_IN2R */
#define WM8994_IN2RN_TO_IN2R_SHIFT                   2  /* IN2RN_TO_IN2R */
#define WM8994_IN2RN_TO_IN2R_WIDTH                   1  /* IN2RN_TO_IN2R */
#define WM8994_IN1RP_TO_IN1R                    0x0002  /* IN1RP_TO_IN1R */
#define WM8994_IN1RP_TO_IN1R_MASK               0x0002  /* IN1RP_TO_IN1R */
#define WM8994_IN1RP_TO_IN1R_SHIFT                   1  /* IN1RP_TO_IN1R */
#define WM8994_IN1RP_TO_IN1R_WIDTH                   1  /* IN1RP_TO_IN1R */
#define WM8994_IN1RN_TO_IN1R                    0x0001  /* IN1RN_TO_IN1R */
#define WM8994_IN1RN_TO_IN1R_MASK               0x0001  /* IN1RN_TO_IN1R */
#define WM8994_IN1RN_TO_IN1R_SHIFT                   0  /* IN1RN_TO_IN1R */
#define WM8994_IN1RN_TO_IN1R_WIDTH                   1  /* IN1RN_TO_IN1R */

/*
 * R41 (0x29) - Input Mixer (3)
 */
#define WM8994_IN2L_TO_MIXINL                   0x0100  /* IN2L_TO_MIXINL */
#define WM8994_IN2L_TO_MIXINL_MASK              0x0100  /* IN2L_TO_MIXINL */
#define WM8994_IN2L_TO_MIXINL_SHIFT                  8  /* IN2L_TO_MIXINL */
#define WM8994_IN2L_TO_MIXINL_WIDTH                  1  /* IN2L_TO_MIXINL */
#define WM8994_IN2L_MIXINL_VOL                  0x0080  /* IN2L_MIXINL_VOL */
#define WM8994_IN2L_MIXINL_VOL_MASK             0x0080  /* IN2L_MIXINL_VOL */
#define WM8994_IN2L_MIXINL_VOL_SHIFT                 7  /* IN2L_MIXINL_VOL */
#define WM8994_IN2L_MIXINL_VOL_WIDTH                 1  /* IN2L_MIXINL_VOL */
#define WM8994_IN1L_TO_MIXINL                   0x0020  /* IN1L_TO_MIXINL */
#define WM8994_IN1L_TO_MIXINL_MASK              0x0020  /* IN1L_TO_MIXINL */
#define WM8994_IN1L_TO_MIXINL_SHIFT                  5  /* IN1L_TO_MIXINL */
#define WM8994_IN1L_TO_MIXINL_WIDTH                  1  /* IN1L_TO_MIXINL */
#define WM8994_IN1L_MIXINL_VOL                  0x0010  /* IN1L_MIXINL_VOL */
#define WM8994_IN1L_MIXINL_VOL_MASK             0x0010  /* IN1L_MIXINL_VOL */
#define WM8994_IN1L_MIXINL_VOL_SHIFT                 4  /* IN1L_MIXINL_VOL */
#define WM8994_IN1L_MIXINL_VOL_WIDTH                 1  /* IN1L_MIXINL_VOL */
#define WM8994_MIXOUTL_MIXINL_VOL_MASK          0x0007  /* MIXOUTL_MIXINL_VOL - [2:0] */
#define WM8994_MIXOUTL_MIXINL_VOL_SHIFT              0  /* MIXOUTL_MIXINL_VOL - [2:0] */
#define WM8994_MIXOUTL_MIXINL_VOL_WIDTH              3  /* MIXOUTL_MIXINL_VOL - [2:0] */

/*
 * R42 (0x2A) - Input Mixer (4)
 */
#define WM8994_IN2R_TO_MIXINR                   0x0100  /* IN2R_TO_MIXINR */
#define WM8994_IN2R_TO_MIXINR_MASK              0x0100  /* IN2R_TO_MIXINR */
#define WM8994_IN2R_TO_MIXINR_SHIFT                  8  /* IN2R_TO_MIXINR */
#define WM8994_IN2R_TO_MIXINR_WIDTH                  1  /* IN2R_TO_MIXINR */
#define WM8994_IN2R_MIXINR_VOL                  0x0080  /* IN2R_MIXINR_VOL */
#define WM8994_IN2R_MIXINR_VOL_MASK             0x0080  /* IN2R_MIXINR_VOL */
#define WM8994_IN2R_MIXINR_VOL_SHIFT                 7  /* IN2R_MIXINR_VOL */
#define WM8994_IN2R_MIXINR_VOL_WIDTH                 1  /* IN2R_MIXINR_VOL */
#define WM8994_IN1R_TO_MIXINR                   0x0020  /* IN1R_TO_MIXINR */
#define WM8994_IN1R_TO_MIXINR_MASK              0x0020  /* IN1R_TO_MIXINR */
#define WM8994_IN1R_TO_MIXINR_SHIFT                  5  /* IN1R_TO_MIXINR */
#define WM8994_IN1R_TO_MIXINR_WIDTH                  1  /* IN1R_TO_MIXINR */
#define WM8994_IN1R_MIXINR_VOL                  0x0010  /* IN1R_MIXINR_VOL */
#define WM8994_IN1R_MIXINR_VOL_MASK             0x0010  /* IN1R_MIXINR_VOL */
#define WM8994_IN1R_MIXINR_VOL_SHIFT                 4  /* IN1R_MIXINR_VOL */
#define WM8994_IN1R_MIXINR_VOL_WIDTH                 1  /* IN1R_MIXINR_VOL */
#define WM8994_MIXOUTR_MIXINR_VOL_MASK          0x0007  /* MIXOUTR_MIXINR_VOL - [2:0] */
#define WM8994_MIXOUTR_MIXINR_VOL_SHIFT              0  /* MIXOUTR_MIXINR_VOL - [2:0] */
#define WM8994_MIXOUTR_MIXINR_VOL_WIDTH              3  /* MIXOUTR_MIXINR_VOL - [2:0] */

/*
 * R43 (0x2B) - Input Mixer (5)
 */
#define WM8994_IN1LP_MIXINL_VOL_MASK            0x01C0  /* IN1LP_MIXINL_VOL - [8:6] */
#define WM8994_IN1LP_MIXINL_VOL_SHIFT                6  /* IN1LP_MIXINL_VOL - [8:6] */
#define WM8994_IN1LP_MIXINL_VOL_WIDTH                3  /* IN1LP_MIXINL_VOL - [8:6] */
#define WM8994_IN2LRP_MIXINL_VOL_MASK           0x0007  /* IN2LRP_MIXINL_VOL - [2:0] */
#define WM8994_IN2LRP_MIXINL_VOL_SHIFT               0  /* IN2LRP_MIXINL_VOL - [2:0] */
#define WM8994_IN2LRP_MIXINL_VOL_WIDTH               3  /* IN2LRP_MIXINL_VOL - [2:0] */

/*
 * R44 (0x2C) - Input Mixer (6)
 */
#define WM8994_IN1RP_MIXINR_VOL_MASK            0x01C0  /* IN1RP_MIXINR_VOL - [8:6] */
#define WM8994_IN1RP_MIXINR_VOL_SHIFT                6  /* IN1RP_MIXINR_VOL - [8:6] */
#define WM8994_IN1RP_MIXINR_VOL_WIDTH                3  /* IN1RP_MIXINR_VOL - [8:6] */
#define WM8994_IN2LRP_MIXINR_VOL_MASK           0x0007  /* IN2LRP_MIXINR_VOL - [2:0] */
#define WM8994_IN2LRP_MIXINR_VOL_SHIFT               0  /* IN2LRP_MIXINR_VOL - [2:0] */
#define WM8994_IN2LRP_MIXINR_VOL_WIDTH               3  /* IN2LRP_MIXINR_VOL - [2:0] */

/*
 * R45 (0x2D) - Output Mixer (1)
 */
#define WM8994_DAC1L_TO_HPOUT1L                 0x0100  /* DAC1L_TO_HPOUT1L */
#define WM8994_DAC1L_TO_HPOUT1L_MASK            0x0100  /* DAC1L_TO_HPOUT1L */
#define WM8994_DAC1L_TO_HPOUT1L_SHIFT                8  /* DAC1L_TO_HPOUT1L */
#define WM8994_DAC1L_TO_HPOUT1L_WIDTH                1  /* DAC1L_TO_HPOUT1L */
#define WM8994_MIXINR_TO_MIXOUTL                0x0080  /* MIXINR_TO_MIXOUTL */
#define WM8994_MIXINR_TO_MIXOUTL_MASK           0x0080  /* MIXINR_TO_MIXOUTL */
#define WM8994_MIXINR_TO_MIXOUTL_SHIFT               7  /* MIXINR_TO_MIXOUTL */
#define WM8994_MIXINR_TO_MIXOUTL_WIDTH               1  /* MIXINR_TO_MIXOUTL */
#define WM8994_MIXINL_TO_MIXOUTL                0x0040  /* MIXINL_TO_MIXOUTL */
#define WM8994_MIXINL_TO_MIXOUTL_MASK           0x0040  /* MIXINL_TO_MIXOUTL */
#define WM8994_MIXINL_TO_MIXOUTL_SHIFT               6  /* MIXINL_TO_MIXOUTL */
#define WM8994_MIXINL_TO_MIXOUTL_WIDTH               1  /* MIXINL_TO_MIXOUTL */
#define WM8994_IN2RN_TO_MIXOUTL                 0x0020  /* IN2RN_TO_MIXOUTL */
#define WM8994_IN2RN_TO_MIXOUTL_MASK            0x0020  /* IN2RN_TO_MIXOUTL */
#define WM8994_IN2RN_TO_MIXOUTL_SHIFT                5  /* IN2RN_TO_MIXOUTL */
#define WM8994_IN2RN_TO_MIXOUTL_WIDTH                1  /* IN2RN_TO_MIXOUTL */
#define WM8994_IN2LN_TO_MIXOUTL                 0x0010  /* IN2LN_TO_MIXOUTL */
#define WM8994_IN2LN_TO_MIXOUTL_MASK            0x0010  /* IN2LN_TO_MIXOUTL */
#define WM8994_IN2LN_TO_MIXOUTL_SHIFT                4  /* IN2LN_TO_MIXOUTL */
#define WM8994_IN2LN_TO_MIXOUTL_WIDTH                1  /* IN2LN_TO_MIXOUTL */
#define WM8994_IN1R_TO_MIXOUTL                  0x0008  /* IN1R_TO_MIXOUTL */
#define WM8994_IN1R_TO_MIXOUTL_MASK             0x0008  /* IN1R_TO_MIXOUTL */
#define WM8994_IN1R_TO_MIXOUTL_SHIFT                 3  /* IN1R_TO_MIXOUTL */
#define WM8994_IN1R_TO_MIXOUTL_WIDTH                 1  /* IN1R_TO_MIXOUTL */
#define WM8994_IN1L_TO_MIXOUTL                  0x0004  /* IN1L_TO_MIXOUTL */
#define WM8994_IN1L_TO_MIXOUTL_MASK             0x0004  /* IN1L_TO_MIXOUTL */
#define WM8994_IN1L_TO_MIXOUTL_SHIFT                 2  /* IN1L_TO_MIXOUTL */
#define WM8994_IN1L_TO_MIXOUTL_WIDTH                 1  /* IN1L_TO_MIXOUTL */
#define WM8994_IN2LP_TO_MIXOUTL                 0x0002  /* IN2LP_TO_MIXOUTL */
#define WM8994_IN2LP_TO_MIXOUTL_MASK            0x0002  /* IN2LP_TO_MIXOUTL */
#define WM8994_IN2LP_TO_MIXOUTL_SHIFT                1  /* IN2LP_TO_MIXOUTL */
#define WM8994_IN2LP_TO_MIXOUTL_WIDTH                1  /* IN2LP_TO_MIXOUTL */
#define WM8994_DAC1L_TO_MIXOUTL                 0x0001  /* DAC1L_TO_MIXOUTL */
#define WM8994_DAC1L_TO_MIXOUTL_MASK            0x0001  /* DAC1L_TO_MIXOUTL */
#define WM8994_DAC1L_TO_MIXOUTL_SHIFT                0  /* DAC1L_TO_MIXOUTL */
#define WM8994_DAC1L_TO_MIXOUTL_WIDTH                1  /* DAC1L_TO_MIXOUTL */

/*
 * R46 (0x2E) - Output Mixer (2)
 */
#define WM8994_DAC1R_TO_HPOUT1R                 0x0100  /* DAC1R_TO_HPOUT1R */
#define WM8994_DAC1R_TO_HPOUT1R_MASK            0x0100  /* DAC1R_TO_HPOUT1R */
#define WM8994_DAC1R_TO_HPOUT1R_SHIFT                8  /* DAC1R_TO_HPOUT1R */
#define WM8994_DAC1R_TO_HPOUT1R_WIDTH                1  /* DAC1R_TO_HPOUT1R */
#define WM8994_MIXINL_TO_MIXOUTR                0x0080  /* MIXINL_TO_MIXOUTR */
#define WM8994_MIXINL_TO_MIXOUTR_MASK           0x0080  /* MIXINL_TO_MIXOUTR */
#define WM8994_MIXINL_TO_MIXOUTR_SHIFT               7  /* MIXINL_TO_MIXOUTR */
#define WM8994_MIXINL_TO_MIXOUTR_WIDTH               1  /* MIXINL_TO_MIXOUTR */
#define WM8994_MIXINR_TO_MIXOUTR                0x0040  /* MIXINR_TO_MIXOUTR */
#define WM8994_MIXINR_TO_MIXOUTR_MASK           0x0040  /* MIXINR_TO_MIXOUTR */
#define WM8994_MIXINR_TO_MIXOUTR_SHIFT               6  /* MIXINR_TO_MIXOUTR */
#define WM8994_MIXINR_TO_MIXOUTR_WIDTH               1  /* MIXINR_TO_MIXOUTR */
#define WM8994_IN2LN_TO_MIXOUTR                 0x0020  /* IN2LN_TO_MIXOUTR */
#define WM8994_IN2LN_TO_MIXOUTR_MASK            0x0020  /* IN2LN_TO_MIXOUTR */
#define WM8994_IN2LN_TO_MIXOUTR_SHIFT                5  /* IN2LN_TO_MIXOUTR */
#define WM8994_IN2LN_TO_MIXOUTR_WIDTH                1  /* IN2LN_TO_MIXOUTR */
#define WM8994_IN2RN_TO_MIXOUTR                 0x0010  /* IN2RN_TO_MIXOUTR */
#define WM8994_IN2RN_TO_MIXOUTR_MASK            0x0010  /* IN2RN_TO_MIXOUTR */
#define WM8994_IN2RN_TO_MIXOUTR_SHIFT                4  /* IN2RN_TO_MIXOUTR */
#define WM8994_IN2RN_TO_MIXOUTR_WIDTH                1  /* IN2RN_TO_MIXOUTR */
#define WM8994_IN1L_TO_MIXOUTR                  0x0008  /* IN1L_TO_MIXOUTR */
#define WM8994_IN1L_TO_MIXOUTR_MASK             0x0008  /* IN1L_TO_MIXOUTR */
#define WM8994_IN1L_TO_MIXOUTR_SHIFT                 3  /* IN1L_TO_MIXOUTR */
#define WM8994_IN1L_TO_MIXOUTR_WIDTH                 1  /* IN1L_TO_MIXOUTR */
#define WM8994_IN1R_TO_MIXOUTR                  0x0004  /* IN1R_TO_MIXOUTR */
#define WM8994_IN1R_TO_MIXOUTR_MASK             0x0004  /* IN1R_TO_MIXOUTR */
#define WM8994_IN1R_TO_MIXOUTR_SHIFT                 2  /* IN1R_TO_MIXOUTR */
#define WM8994_IN1R_TO_MIXOUTR_WIDTH                 1  /* IN1R_TO_MIXOUTR */
#define WM8994_IN2RP_TO_MIXOUTR                 0x0002  /* IN2RP_TO_MIXOUTR */
#define WM8994_IN2RP_TO_MIXOUTR_MASK            0x0002  /* IN2RP_TO_MIXOUTR */
#define WM8994_IN2RP_TO_MIXOUTR_SHIFT                1  /* IN2RP_TO_MIXOUTR */
#define WM8994_IN2RP_TO_MIXOUTR_WIDTH                1  /* IN2RP_TO_MIXOUTR */
#define WM8994_DAC1R_TO_MIXOUTR                 0x0001  /* DAC1R_TO_MIXOUTR */
#define WM8994_DAC1R_TO_MIXOUTR_MASK            0x0001  /* DAC1R_TO_MIXOUTR */
#define WM8994_DAC1R_TO_MIXOUTR_SHIFT                0  /* DAC1R_TO_MIXOUTR */
#define WM8994_DAC1R_TO_MIXOUTR_WIDTH                1  /* DAC1R_TO_MIXOUTR */

/*
 * R47 (0x2F) - Output Mixer (3)
 */
#define WM8994_IN2LP_MIXOUTL_VOL_MASK           0x0E00  /* IN2LP_MIXOUTL_VOL - [11:9] */
#define WM8994_IN2LP_MIXOUTL_VOL_SHIFT               9  /* IN2LP_MIXOUTL_VOL - [11:9] */
#define WM8994_IN2LP_MIXOUTL_VOL_WIDTH               3  /* IN2LP_MIXOUTL_VOL - [11:9] */
#define WM8994_IN2LN_MIXOUTL_VOL_MASK           0x01C0  /* IN2LN_MIXOUTL_VOL - [8:6] */
#define WM8994_IN2LN_MIXOUTL_VOL_SHIFT               6  /* IN2LN_MIXOUTL_VOL - [8:6] */
#define WM8994_IN2LN_MIXOUTL_VOL_WIDTH               3  /* IN2LN_MIXOUTL_VOL - [8:6] */
#define WM8994_IN1R_MIXOUTL_VOL_MASK            0x0038  /* IN1R_MIXOUTL_VOL - [5:3] */
#define WM8994_IN1R_MIXOUTL_VOL_SHIFT                3  /* IN1R_MIXOUTL_VOL - [5:3] */
#define WM8994_IN1R_MIXOUTL_VOL_WIDTH                3  /* IN1R_MIXOUTL_VOL - [5:3] */
#define WM8994_IN1L_MIXOUTL_VOL_MASK            0x0007  /* IN1L_MIXOUTL_VOL - [2:0] */
#define WM8994_IN1L_MIXOUTL_VOL_SHIFT                0  /* IN1L_MIXOUTL_VOL - [2:0] */
#define WM8994_IN1L_MIXOUTL_VOL_WIDTH                3  /* IN1L_MIXOUTL_VOL - [2:0] */

/*
 * R48 (0x30) - Output Mixer (4)
 */
#define WM8994_IN2RP_MIXOUTR_VOL_MASK           0x0E00  /* IN2RP_MIXOUTR_VOL - [11:9] */
#define WM8994_IN2RP_MIXOUTR_VOL_SHIFT               9  /* IN2RP_MIXOUTR_VOL - [11:9] */
#define WM8994_IN2RP_MIXOUTR_VOL_WIDTH               3  /* IN2RP_MIXOUTR_VOL - [11:9] */
#define WM8994_IN2RN_MIXOUTR_VOL_MASK           0x01C0  /* IN2RN_MIXOUTR_VOL - [8:6] */
#define WM8994_IN2RN_MIXOUTR_VOL_SHIFT               6  /* IN2RN_MIXOUTR_VOL - [8:6] */
#define WM8994_IN2RN_MIXOUTR_VOL_WIDTH               3  /* IN2RN_MIXOUTR_VOL - [8:6] */
#define WM8994_IN1L_MIXOUTR_VOL_MASK            0x0038  /* IN1L_MIXOUTR_VOL - [5:3] */
#define WM8994_IN1L_MIXOUTR_VOL_SHIFT                3  /* IN1L_MIXOUTR_VOL - [5:3] */
#define WM8994_IN1L_MIXOUTR_VOL_WIDTH                3  /* IN1L_MIXOUTR_VOL - [5:3] */
#define WM8994_IN1R_MIXOUTR_VOL_MASK            0x0007  /* IN1R_MIXOUTR_VOL - [2:0] */
#define WM8994_IN1R_MIXOUTR_VOL_SHIFT                0  /* IN1R_MIXOUTR_VOL - [2:0] */
#define WM8994_IN1R_MIXOUTR_VOL_WIDTH                3  /* IN1R_MIXOUTR_VOL - [2:0] */

/*
 * R49 (0x31) - Output Mixer (5)
 */
#define WM8994_DAC1L_MIXOUTL_VOL_MASK           0x0E00  /* DAC1L_MIXOUTL_VOL - [11:9] */
#define WM8994_DAC1L_MIXOUTL_VOL_SHIFT               9  /* DAC1L_MIXOUTL_VOL - [11:9] */
#define WM8994_DAC1L_MIXOUTL_VOL_WIDTH               3  /* DAC1L_MIXOUTL_VOL - [11:9] */
#define WM8994_IN2RN_MIXOUTL_VOL_MASK           0x01C0  /* IN2RN_MIXOUTL_VOL - [8:6] */
#define WM8994_IN2RN_MIXOUTL_VOL_SHIFT               6  /* IN2RN_MIXOUTL_VOL - [8:6] */
#define WM8994_IN2RN_MIXOUTL_VOL_WIDTH               3  /* IN2RN_MIXOUTL_VOL - [8:6] */
#define WM8994_MIXINR_MIXOUTL_VOL_MASK          0x0038  /* MIXINR_MIXOUTL_VOL - [5:3] */
#define WM8994_MIXINR_MIXOUTL_VOL_SHIFT              3  /* MIXINR_MIXOUTL_VOL - [5:3] */
#define WM8994_MIXINR_MIXOUTL_VOL_WIDTH              3  /* MIXINR_MIXOUTL_VOL - [5:3] */
#define WM8994_MIXINL_MIXOUTL_VOL_MASK          0x0007  /* MIXINL_MIXOUTL_VOL - [2:0] */
#define WM8994_MIXINL_MIXOUTL_VOL_SHIFT              0  /* MIXINL_MIXOUTL_VOL - [2:0] */
#define WM8994_MIXINL_MIXOUTL_VOL_WIDTH              3  /* MIXINL_MIXOUTL_VOL - [2:0] */

/*
 * R50 (0x32) - Output Mixer (6)
 */
#define WM8994_DAC1R_MIXOUTR_VOL_MASK           0x0E00  /* DAC1R_MIXOUTR_VOL - [11:9] */
#define WM8994_DAC1R_MIXOUTR_VOL_SHIFT               9  /* DAC1R_MIXOUTR_VOL - [11:9] */
#define WM8994_DAC1R_MIXOUTR_VOL_WIDTH               3  /* DAC1R_MIXOUTR_VOL - [11:9] */
#define WM8994_IN2LN_MIXOUTR_VOL_MASK           0x01C0  /* IN2LN_MIXOUTR_VOL - [8:6] */
#define WM8994_IN2LN_MIXOUTR_VOL_SHIFT               6  /* IN2LN_MIXOUTR_VOL - [8:6] */
#define WM8994_IN2LN_MIXOUTR_VOL_WIDTH               3  /* IN2LN_MIXOUTR_VOL - [8:6] */
#define WM8994_MIXINL_MIXOUTR_VOL_MASK          0x0038  /* MIXINL_MIXOUTR_VOL - [5:3] */
#define WM8994_MIXINL_MIXOUTR_VOL_SHIFT              3  /* MIXINL_MIXOUTR_VOL - [5:3] */
#define WM8994_MIXINL_MIXOUTR_VOL_WIDTH              3  /* MIXINL_MIXOUTR_VOL - [5:3] */
#define WM8994_MIXINR_MIXOUTR_VOL_MASK          0x0007  /* MIXINR_MIXOUTR_VOL - [2:0] */
#define WM8994_MIXINR_MIXOUTR_VOL_SHIFT              0  /* MIXINR_MIXOUTR_VOL - [2:0] */
#define WM8994_MIXINR_MIXOUTR_VOL_WIDTH              3  /* MIXINR_MIXOUTR_VOL - [2:0] */

/*
 * R51 (0x33) - HPOUT2 Mixer
 */
#define WM8994_IN2LRP_TO_HPOUT2                 0x0020  /* IN2LRP_TO_HPOUT2 */
#define WM8994_IN2LRP_TO_HPOUT2_MASK            0x0020  /* IN2LRP_TO_HPOUT2 */
#define WM8994_IN2LRP_TO_HPOUT2_SHIFT                5  /* IN2LRP_TO_HPOUT2 */
#define WM8994_IN2LRP_TO_HPOUT2_WIDTH                1  /* IN2LRP_TO_HPOUT2 */
#define WM8994_MIXOUTLVOL_TO_HPOUT2             0x0010  /* MIXOUTLVOL_TO_HPOUT2 */
#define WM8994_MIXOUTLVOL_TO_HPOUT2_MASK        0x0010  /* MIXOUTLVOL_TO_HPOUT2 */
#define WM8994_MIXOUTLVOL_TO_HPOUT2_SHIFT            4  /* MIXOUTLVOL_TO_HPOUT2 */
#define WM8994_MIXOUTLVOL_TO_HPOUT2_WIDTH            1  /* MIXOUTLVOL_TO_HPOUT2 */
#define WM8994_MIXOUTRVOL_TO_HPOUT2             0x0008  /* MIXOUTRVOL_TO_HPOUT2 */
#define WM8994_MIXOUTRVOL_TO_HPOUT2_MASK        0x0008  /* MIXOUTRVOL_TO_HPOUT2 */
#define WM8994_MIXOUTRVOL_TO_HPOUT2_SHIFT            3  /* MIXOUTRVOL_TO_HPOUT2 */
#define WM8994_MIXOUTRVOL_TO_HPOUT2_WIDTH            1  /* MIXOUTRVOL_TO_HPOUT2 */

/*
 * R52 (0x34) - Line Mixer (1)
 */
#define WM8994_MIXOUTL_TO_LINEOUT1N             0x0040  /* MIXOUTL_TO_LINEOUT1N */
#define WM8994_MIXOUTL_TO_LINEOUT1N_MASK        0x0040  /* MIXOUTL_TO_LINEOUT1N */
#define WM8994_MIXOUTL_TO_LINEOUT1N_SHIFT            6  /* MIXOUTL_TO_LINEOUT1N */
#define WM8994_MIXOUTL_TO_LINEOUT1N_WIDTH            1  /* MIXOUTL_TO_LINEOUT1N */
#define WM8994_MIXOUTR_TO_LINEOUT1N             0x0020  /* MIXOUTR_TO_LINEOUT1N */
#define WM8994_MIXOUTR_TO_LINEOUT1N_MASK        0x0020  /* MIXOUTR_TO_LINEOUT1N */
#define WM8994_MIXOUTR_TO_LINEOUT1N_SHIFT            5  /* MIXOUTR_TO_LINEOUT1N */
#define WM8994_MIXOUTR_TO_LINEOUT1N_WIDTH            1  /* MIXOUTR_TO_LINEOUT1N */
#define WM8994_LINEOUT1_MODE                    0x0010  /* LINEOUT1_MODE */
#define WM8994_LINEOUT1_MODE_MASK               0x0010  /* LINEOUT1_MODE */
#define WM8994_LINEOUT1_MODE_SHIFT                   4  /* LINEOUT1_MODE */
#define WM8994_LINEOUT1_MODE_WIDTH                   1  /* LINEOUT1_MODE */
#define WM8994_IN1R_TO_LINEOUT1P                0x0004  /* IN1R_TO_LINEOUT1P */
#define WM8994_IN1R_TO_LINEOUT1P_MASK           0x0004  /* IN1R_TO_LINEOUT1P */
#define WM8994_IN1R_TO_LINEOUT1P_SHIFT               2  /* IN1R_TO_LINEOUT1P */
#define WM8994_IN1R_TO_LINEOUT1P_WIDTH               1  /* IN1R_TO_LINEOUT1P */
#define WM8994_IN1L_TO_LINEOUT1P                0x0002  /* IN1L_TO_LINEOUT1P */
#define WM8994_IN1L_TO_LINEOUT1P_MASK           0x0002  /* IN1L_TO_LINEOUT1P */
#define WM8994_IN1L_TO_LINEOUT1P_SHIFT               1  /* IN1L_TO_LINEOUT1P */
#define WM8994_IN1L_TO_LINEOUT1P_WIDTH               1  /* IN1L_TO_LINEOUT1P */
#define WM8994_MIXOUTL_TO_LINEOUT1P             0x0001  /* MIXOUTL_TO_LINEOUT1P */
#define WM8994_MIXOUTL_TO_LINEOUT1P_MASK        0x0001  /* MIXOUTL_TO_LINEOUT1P */
#define WM8994_MIXOUTL_TO_LINEOUT1P_SHIFT            0  /* MIXOUTL_TO_LINEOUT1P */
#define WM8994_MIXOUTL_TO_LINEOUT1P_WIDTH            1  /* MIXOUTL_TO_LINEOUT1P */

/*
 * R53 (0x35) - Line Mixer (2)
 */
#define WM8994_MIXOUTR_TO_LINEOUT2N             0x0040  /* MIXOUTR_TO_LINEOUT2N */
#define WM8994_MIXOUTR_TO_LINEOUT2N_MASK        0x0040  /* MIXOUTR_TO_LINEOUT2N */
#define WM8994_MIXOUTR_TO_LINEOUT2N_SHIFT            6  /* MIXOUTR_TO_LINEOUT2N */
#define WM8994_MIXOUTR_TO_LINEOUT2N_WIDTH            1  /* MIXOUTR_TO_LINEOUT2N */
#define WM8994_MIXOUTL_TO_LINEOUT2N             0x0020  /* MIXOUTL_TO_LINEOUT2N */
#define WM8994_MIXOUTL_TO_LINEOUT2N_MASK        0x0020  /* MIXOUTL_TO_LINEOUT2N */
#define WM8994_MIXOUTL_TO_LINEOUT2N_SHIFT            5  /* MIXOUTL_TO_LINEOUT2N */
#define WM8994_MIXOUTL_TO_LINEOUT2N_WIDTH            1  /* MIXOUTL_TO_LINEOUT2N */
#define WM8994_LINEOUT2_MODE                    0x0010  /* LINEOUT2_MODE */
#define WM8994_LINEOUT2_MODE_MASK               0x0010  /* LINEOUT2_MODE */
#define WM8994_LINEOUT2_MODE_SHIFT                   4  /* LINEOUT2_MODE */
#define WM8994_LINEOUT2_MODE_WIDTH                   1  /* LINEOUT2_MODE */
#define WM8994_IN1L_TO_LINEOUT2P                0x0004  /* IN1L_TO_LINEOUT2P */
#define WM8994_IN1L_TO_LINEOUT2P_MASK           0x0004  /* IN1L_TO_LINEOUT2P */
#define WM8994_IN1L_TO_LINEOUT2P_SHIFT               2  /* IN1L_TO_LINEOUT2P */
#define WM8994_IN1L_TO_LINEOUT2P_WIDTH               1  /* IN1L_TO_LINEOUT2P */
#define WM8994_IN1R_TO_LINEOUT2P                0x0002  /* IN1R_TO_LINEOUT2P */
#define WM8994_IN1R_TO_LINEOUT2P_MASK           0x0002  /* IN1R_TO_LINEOUT2P */
#define WM8994_IN1R_TO_LINEOUT2P_SHIFT               1  /* IN1R_TO_LINEOUT2P */
#define WM8994_IN1R_TO_LINEOUT2P_WIDTH               1  /* IN1R_TO_LINEOUT2P */
#define WM8994_MIXOUTR_TO_LINEOUT2P             0x0001  /* MIXOUTR_TO_LINEOUT2P */
#define WM8994_MIXOUTR_TO_LINEOUT2P_MASK        0x0001  /* MIXOUTR_TO_LINEOUT2P */
#define WM8994_MIXOUTR_TO_LINEOUT2P_SHIFT            0  /* MIXOUTR_TO_LINEOUT2P */
#define WM8994_MIXOUTR_TO_LINEOUT2P_WIDTH            1  /* MIXOUTR_TO_LINEOUT2P */

/*
 * R54 (0x36) - Speaker Mixer
 */
#define WM8994_DAC2L_TO_SPKMIXL                 0x0200  /* DAC2L_TO_SPKMIXL */
#define WM8994_DAC2L_TO_SPKMIXL_MASK            0x0200  /* DAC2L_TO_SPKMIXL */
#define WM8994_DAC2L_TO_SPKMIXL_SHIFT                9  /* DAC2L_TO_SPKMIXL */
#define WM8994_DAC2L_TO_SPKMIXL_WIDTH                1  /* DAC2L_TO_SPKMIXL */
#define WM8994_DAC2R_TO_SPKMIXR                 0x0100  /* DAC2R_TO_SPKMIXR */
#define WM8994_DAC2R_TO_SPKMIXR_MASK            0x0100  /* DAC2R_TO_SPKMIXR */
#define WM8994_DAC2R_TO_SPKMIXR_SHIFT                8  /* DAC2R_TO_SPKMIXR */
#define WM8994_DAC2R_TO_SPKMIXR_WIDTH                1  /* DAC2R_TO_SPKMIXR */
#define WM8994_MIXINL_TO_SPKMIXL                0x0080  /* MIXINL_TO_SPKMIXL */
#define WM8994_MIXINL_TO_SPKMIXL_MASK           0x0080  /* MIXINL_TO_SPKMIXL */
#define WM8994_MIXINL_TO_SPKMIXL_SHIFT               7  /* MIXINL_TO_SPKMIXL */
#define WM8994_MIXINL_TO_SPKMIXL_WIDTH               1  /* MIXINL_TO_SPKMIXL */
#define WM8994_MIXINR_TO_SPKMIXR                0x0040  /* MIXINR_TO_SPKMIXR */
#define WM8994_MIXINR_TO_SPKMIXR_MASK           0x0040  /* MIXINR_TO_SPKMIXR */
#define WM8994_MIXINR_TO_SPKMIXR_SHIFT               6  /* MIXINR_TO_SPKMIXR */
#define WM8994_MIXINR_TO_SPKMIXR_WIDTH               1  /* MIXINR_TO_SPKMIXR */
#define WM8994_IN1LP_TO_SPKMIXL                 0x0020  /* IN1LP_TO_SPKMIXL */
#define WM8994_IN1LP_TO_SPKMIXL_MASK            0x0020  /* IN1LP_TO_SPKMIXL */
#define WM8994_IN1LP_TO_SPKMIXL_SHIFT                5  /* IN1LP_TO_SPKMIXL */
#define WM8994_IN1LP_TO_SPKMIXL_WIDTH                1  /* IN1LP_TO_SPKMIXL */
#define WM8994_IN1RP_TO_SPKMIXR                 0x0010  /* IN1RP_TO_SPKMIXR */
#define WM8994_IN1RP_TO_SPKMIXR_MASK            0x0010  /* IN1RP_TO_SPKMIXR */
#define WM8994_IN1RP_TO_SPKMIXR_SHIFT                4  /* IN1RP_TO_SPKMIXR */
#define WM8994_IN1RP_TO_SPKMIXR_WIDTH                1  /* IN1RP_TO_SPKMIXR */
#define WM8994_MIXOUTL_TO_SPKMIXL               0x0008  /* MIXOUTL_TO_SPKMIXL */
#define WM8994_MIXOUTL_TO_SPKMIXL_MASK          0x0008  /* MIXOUTL_TO_SPKMIXL */
#define WM8994_MIXOUTL_TO_SPKMIXL_SHIFT              3  /* MIXOUTL_TO_SPKMIXL */
#define WM8994_MIXOUTL_TO_SPKMIXL_WIDTH              1  /* MIXOUTL_TO_SPKMIXL */
#define WM8994_MIXOUTR_TO_SPKMIXR               0x0004  /* MIXOUTR_TO_SPKMIXR */
#define WM8994_MIXOUTR_TO_SPKMIXR_MASK          0x0004  /* MIXOUTR_TO_SPKMIXR */
#define WM8994_MIXOUTR_TO_SPKMIXR_SHIFT              2  /* MIXOUTR_TO_SPKMIXR */
#define WM8994_MIXOUTR_TO_SPKMIXR_WIDTH              1  /* MIXOUTR_TO_SPKMIXR */
#define WM8994_DAC1L_TO_SPKMIXL                 0x0002  /* DAC1L_TO_SPKMIXL */
#define WM8994_DAC1L_TO_SPKMIXL_MASK            0x0002  /* DAC1L_TO_SPKMIXL */
#define WM8994_DAC1L_TO_SPKMIXL_SHIFT                1  /* DAC1L_TO_SPKMIXL */
#define WM8994_DAC1L_TO_SPKMIXL_WIDTH                1  /* DAC1L_TO_SPKMIXL */
#define WM8994_DAC1R_TO_SPKMIXR                 0x0001  /* DAC1R_TO_SPKMIXR */
#define WM8994_DAC1R_TO_SPKMIXR_MASK            0x0001  /* DAC1R_TO_SPKMIXR */
#define WM8994_DAC1R_TO_SPKMIXR_SHIFT                0  /* DAC1R_TO_SPKMIXR */
#define WM8994_DAC1R_TO_SPKMIXR_WIDTH                1  /* DAC1R_TO_SPKMIXR */

/*
 * R55 (0x37) - Additional Control
 */
#define WM8994_LINEOUT1_FB                      0x0080  /* LINEOUT1_FB */
#define WM8994_LINEOUT1_FB_MASK                 0x0080  /* LINEOUT1_FB */
#define WM8994_LINEOUT1_FB_SHIFT                     7  /* LINEOUT1_FB */
#define WM8994_LINEOUT1_FB_WIDTH                     1  /* LINEOUT1_FB */
#define WM8994_LINEOUT2_FB                      0x0040  /* LINEOUT2_FB */
#define WM8994_LINEOUT2_FB_MASK                 0x0040  /* LINEOUT2_FB */
#define WM8994_LINEOUT2_FB_SHIFT                     6  /* LINEOUT2_FB */
#define WM8994_LINEOUT2_FB_WIDTH                     1  /* LINEOUT2_FB */
#define WM8994_VROI                             0x0001  /* VROI */
#define WM8994_VROI_MASK                        0x0001  /* VROI */
#define WM8994_VROI_SHIFT                            0  /* VROI */
#define WM8994_VROI_WIDTH                            1  /* VROI */

/*
 * R56 (0x38) - AntiPOP (1)
 */
#define WM8994_LINEOUT_VMID_BUF_ENA             0x0080  /* LINEOUT_VMID_BUF_ENA */
#define WM8994_LINEOUT_VMID_BUF_ENA_MASK        0x0080  /* LINEOUT_VMID_BUF_ENA */
#define WM8994_LINEOUT_VMID_BUF_ENA_SHIFT            7  /* LINEOUT_VMID_BUF_ENA */
#define WM8994_LINEOUT_VMID_BUF_ENA_WIDTH            1  /* LINEOUT_VMID_BUF_ENA */
#define WM8994_HPOUT2_IN_ENA                    0x0040  /* HPOUT2_IN_ENA */
#define WM8994_HPOUT2_IN_ENA_MASK               0x0040  /* HPOUT2_IN_ENA */
#define WM8994_HPOUT2_IN_ENA_SHIFT                   6  /* HPOUT2_IN_ENA */
#define WM8994_HPOUT2_IN_ENA_WIDTH                   1  /* HPOUT2_IN_ENA */
#define WM8994_LINEOUT1_DISCH                   0x0020  /* LINEOUT1_DISCH */
#define WM8994_LINEOUT1_DISCH_MASK              0x0020  /* LINEOUT1_DISCH */
#define WM8994_LINEOUT1_DISCH_SHIFT                  5  /* LINEOUT1_DISCH */
#define WM8994_LINEOUT1_DISCH_WIDTH                  1  /* LINEOUT1_DISCH */
#define WM8994_LINEOUT2_DISCH                   0x0010  /* LINEOUT2_DISCH */
#define WM8994_LINEOUT2_DISCH_MASK              0x0010  /* LINEOUT2_DISCH */
#define WM8994_LINEOUT2_DISCH_SHIFT                  4  /* LINEOUT2_DISCH */
#define WM8994_LINEOUT2_DISCH_WIDTH                  1  /* LINEOUT2_DISCH */

/*
 * R57 (0x39) - AntiPOP (2)
 */
#define WM8994_MICB2_DISCH                      0x0100  /* MICB2_DISCH */
#define WM8994_MICB2_DISCH_MASK                 0x0100  /* MICB2_DISCH */
#define WM8994_MICB2_DISCH_SHIFT                     8  /* MICB2_DISCH */
#define WM8994_MICB2_DISCH_WIDTH                     1  /* MICB2_DISCH */
#define WM8994_MICB1_DISCH                      0x0080  /* MICB1_DISCH */
#define WM8994_MICB1_DISCH_MASK                 0x0080  /* MICB1_DISCH */
#define WM8994_MICB1_DISCH_SHIFT                     7  /* MICB1_DISCH */
#define WM8994_MICB1_DISCH_WIDTH                     1  /* MICB1_DISCH */
#define WM8994_VMID_RAMP_MASK                   0x0060  /* VMID_RAMP - [6:5] */
#define WM8994_VMID_RAMP_SHIFT                       5  /* VMID_RAMP - [6:5] */
#define WM8994_VMID_RAMP_WIDTH                       2  /* VMID_RAMP - [6:5] */
#define WM8994_VMID_BUF_ENA                     0x0008  /* VMID_BUF_ENA */
#define WM8994_VMID_BUF_ENA_MASK                0x0008  /* VMID_BUF_ENA */
#define WM8994_VMID_BUF_ENA_SHIFT                    3  /* VMID_BUF_ENA */
#define WM8994_VMID_BUF_ENA_WIDTH                    1  /* VMID_BUF_ENA */
#define WM8994_STARTUP_BIAS_ENA                 0x0004  /* STARTUP_BIAS_ENA */
#define WM8994_STARTUP_BIAS_ENA_MASK            0x0004  /* STARTUP_BIAS_ENA */
#define WM8994_STARTUP_BIAS_ENA_SHIFT                2  /* STARTUP_BIAS_ENA */
#define WM8994_STARTUP_BIAS_ENA_WIDTH                1  /* STARTUP_BIAS_ENA */
#define WM8994_BIAS_SRC                         0x0002  /* BIAS_SRC */
#define WM8994_BIAS_SRC_MASK                    0x0002  /* BIAS_SRC */
#define WM8994_BIAS_SRC_SHIFT                        1  /* BIAS_SRC */
#define WM8994_BIAS_SRC_WIDTH                        1  /* BIAS_SRC */
#define WM8994_VMID_DISCH                       0x0001  /* VMID_DISCH */
#define WM8994_VMID_DISCH_MASK                  0x0001  /* VMID_DISCH */
#define WM8994_VMID_DISCH_SHIFT                      0  /* VMID_DISCH */
#define WM8994_VMID_DISCH_WIDTH                      1  /* VMID_DISCH */

/*
 * R58 (0x3A) - MICBIAS
 */
#define WM8994_MICD_SCTHR_MASK                  0x00C0  /* MICD_SCTHR - [7:6] */
#define WM8994_MICD_SCTHR_SHIFT                      6  /* MICD_SCTHR - [7:6] */
#define WM8994_MICD_SCTHR_WIDTH                      2  /* MICD_SCTHR - [7:6] */
#define WM8994_MICD_THR_MASK                    0x0038  /* MICD_THR - [5:3] */
#define WM8994_MICD_THR_SHIFT                        3  /* MICD_THR - [5:3] */
#define WM8994_MICD_THR_WIDTH                        3  /* MICD_THR - [5:3] */
#define WM8994_MICD_ENA                         0x0004  /* MICD_ENA */
#define WM8994_MICD_ENA_MASK                    0x0004  /* MICD_ENA */
#define WM8994_MICD_ENA_SHIFT                        2  /* MICD_ENA */
#define WM8994_MICD_ENA_WIDTH                        1  /* MICD_ENA */
#define WM8994_MICB2_LVL                        0x0002  /* MICB2_LVL */
#define WM8994_MICB2_LVL_MASK                   0x0002  /* MICB2_LVL */
#define WM8994_MICB2_LVL_SHIFT                       1  /* MICB2_LVL */
#define WM8994_MICB2_LVL_WIDTH                       1  /* MICB2_LVL */
#define WM8994_MICB1_LVL                        0x0001  /* MICB1_LVL */
#define WM8994_MICB1_LVL_MASK                   0x0001  /* MICB1_LVL */
#define WM8994_MICB1_LVL_SHIFT                       0  /* MICB1_LVL */
#define WM8994_MICB1_LVL_WIDTH                       1  /* MICB1_LVL */

/*
 * R59 (0x3B) - LDO 1
 */
#define WM8994_LDO1_VSEL_MASK                   0x000E  /* LDO1_VSEL - [3:1] */
#define WM8994_LDO1_VSEL_SHIFT                       1  /* LDO1_VSEL - [3:1] */
#define WM8994_LDO1_VSEL_WIDTH                       3  /* LDO1_VSEL - [3:1] */
#define WM8994_LDO1_DISCH                       0x0001  /* LDO1_DISCH */
#define WM8994_LDO1_DISCH_MASK                  0x0001  /* LDO1_DISCH */
#define WM8994_LDO1_DISCH_SHIFT                      0  /* LDO1_DISCH */
#define WM8994_LDO1_DISCH_WIDTH                      1  /* LDO1_DISCH */

/*
 * R60 (0x3C) - LDO 2
 */
#define WM8994_LDO2_VSEL_MASK                   0x0006  /* LDO2_VSEL - [2:1] */
#define WM8994_LDO2_VSEL_SHIFT                       1  /* LDO2_VSEL - [2:1] */
#define WM8994_LDO2_VSEL_WIDTH                       2  /* LDO2_VSEL - [2:1] */
#define WM8994_LDO2_DISCH                       0x0001  /* LDO2_DISCH */
#define WM8994_LDO2_DISCH_MASK                  0x0001  /* LDO2_DISCH */
#define WM8994_LDO2_DISCH_SHIFT                      0  /* LDO2_DISCH */
#define WM8994_LDO2_DISCH_WIDTH                      1  /* LDO2_DISCH */

/*
 * R76 (0x4C) - Charge Pump (1)
 */
#define WM8994_CP_ENA                           0x8000  /* CP_ENA */
#define WM8994_CP_ENA_MASK                      0x8000  /* CP_ENA */
#define WM8994_CP_ENA_SHIFT                         15  /* CP_ENA */
#define WM8994_CP_ENA_WIDTH                          1  /* CP_ENA */

/*
 * R81 (0x51) - Class W (1)
 */
#define WM8994_CP_DYN_SRC_SEL_MASK              0x0300  /* CP_DYN_SRC_SEL - [9:8] */
#define WM8994_CP_DYN_SRC_SEL_SHIFT                  8  /* CP_DYN_SRC_SEL - [9:8] */
#define WM8994_CP_DYN_SRC_SEL_WIDTH                  2  /* CP_DYN_SRC_SEL - [9:8] */
#define WM8994_CP_DYN_PWR                       0x0001  /* CP_DYN_PWR */
#define WM8994_CP_DYN_PWR_MASK                  0x0001  /* CP_DYN_PWR */
#define WM8994_CP_DYN_PWR_SHIFT                      0  /* CP_DYN_PWR */
#define WM8994_CP_DYN_PWR_WIDTH                      1  /* CP_DYN_PWR */

/*
 * R84 (0x54) - DC Servo (1)
 */
#define WM8994_DCS_TRIG_SINGLE_1                0x2000  /* DCS_TRIG_SINGLE_1 */
#define WM8994_DCS_TRIG_SINGLE_1_MASK           0x2000  /* DCS_TRIG_SINGLE_1 */
#define WM8994_DCS_TRIG_SINGLE_1_SHIFT              13  /* DCS_TRIG_SINGLE_1 */
#define WM8994_DCS_TRIG_SINGLE_1_WIDTH               1  /* DCS_TRIG_SINGLE_1 */
#define WM8994_DCS_TRIG_SINGLE_0                0x1000  /* DCS_TRIG_SINGLE_0 */
#define WM8994_DCS_TRIG_SINGLE_0_MASK           0x1000  /* DCS_TRIG_SINGLE_0 */
#define WM8994_DCS_TRIG_SINGLE_0_SHIFT              12  /* DCS_TRIG_SINGLE_0 */
#define WM8994_DCS_TRIG_SINGLE_0_WIDTH               1  /* DCS_TRIG_SINGLE_0 */
#define WM8994_DCS_TRIG_SERIES_1                0x0200  /* DCS_TRIG_SERIES_1 */
#define WM8994_DCS_TRIG_SERIES_1_MASK           0x0200  /* DCS_TRIG_SERIES_1 */
#define WM8994_DCS_TRIG_SERIES_1_SHIFT               9  /* DCS_TRIG_SERIES_1 */
#define WM8994_DCS_TRIG_SERIES_1_WIDTH               1  /* DCS_TRIG_SERIES_1 */
#define WM8994_DCS_TRIG_SERIES_0                0x0100  /* DCS_TRIG_SERIES_0 */
#define WM8994_DCS_TRIG_SERIES_0_MASK           0x0100  /* DCS_TRIG_SERIES_0 */
#define WM8994_DCS_TRIG_SERIES_0_SHIFT               8  /* DCS_TRIG_SERIES_0 */
#define WM8994_DCS_TRIG_SERIES_0_WIDTH               1  /* DCS_TRIG_SERIES_0 */
#define WM8994_DCS_TRIG_STARTUP_1               0x0020  /* DCS_TRIG_STARTUP_1 */
#define WM8994_DCS_TRIG_STARTUP_1_MASK          0x0020  /* DCS_TRIG_STARTUP_1 */
#define WM8994_DCS_TRIG_STARTUP_1_SHIFT              5  /* DCS_TRIG_STARTUP_1 */
#define WM8994_DCS_TRIG_STARTUP_1_WIDTH              1  /* DCS_TRIG_STARTUP_1 */
#define WM8994_DCS_TRIG_STARTUP_0               0x0010  /* DCS_TRIG_STARTUP_0 */
#define WM8994_DCS_TRIG_STARTUP_0_MASK          0x0010  /* DCS_TRIG_STARTUP_0 */
#define WM8994_DCS_TRIG_STARTUP_0_SHIFT              4  /* DCS_TRIG_STARTUP_0 */
#define WM8994_DCS_TRIG_STARTUP_0_WIDTH              1  /* DCS_TRIG_STARTUP_0 */
#define WM8994_DCS_TRIG_DAC_WR_1                0x0008  /* DCS_TRIG_DAC_WR_1 */
#define WM8994_DCS_TRIG_DAC_WR_1_MASK           0x0008  /* DCS_TRIG_DAC_WR_1 */
#define WM8994_DCS_TRIG_DAC_WR_1_SHIFT               3  /* DCS_TRIG_DAC_WR_1 */
#define WM8994_DCS_TRIG_DAC_WR_1_WIDTH               1  /* DCS_TRIG_DAC_WR_1 */
#define WM8994_DCS_TRIG_DAC_WR_0                0x0004  /* DCS_TRIG_DAC_WR_0 */
#define WM8994_DCS_TRIG_DAC_WR_0_MASK           0x0004  /* DCS_TRIG_DAC_WR_0 */
#define WM8994_DCS_TRIG_DAC_WR_0_SHIFT               2  /* DCS_TRIG_DAC_WR_0 */
#define WM8994_DCS_TRIG_DAC_WR_0_WIDTH               1  /* DCS_TRIG_DAC_WR_0 */
#define WM8994_DCS_ENA_CHAN_1                   0x0002  /* DCS_ENA_CHAN_1 */
#define WM8994_DCS_ENA_CHAN_1_MASK              0x0002  /* DCS_ENA_CHAN_1 */
#define WM8994_DCS_ENA_CHAN_1_SHIFT                  1  /* DCS_ENA_CHAN_1 */
#define WM8994_DCS_ENA_CHAN_1_WIDTH                  1  /* DCS_ENA_CHAN_1 */
#define WM8994_DCS_ENA_CHAN_0                   0x0001  /* DCS_ENA_CHAN_0 */
#define WM8994_DCS_ENA_CHAN_0_MASK              0x0001  /* DCS_ENA_CHAN_0 */
#define WM8994_DCS_ENA_CHAN_0_SHIFT                  0  /* DCS_ENA_CHAN_0 */
#define WM8994_DCS_ENA_CHAN_0_WIDTH                  1  /* DCS_ENA_CHAN_0 */

/*
 * R85 (0x55) - DC Servo (2)
 */
#define WM8994_DCS_SERIES_NO_01_MASK            0x0FE0  /* DCS_SERIES_NO_01 - [11:5] */
#define WM8994_DCS_SERIES_NO_01_SHIFT                5  /* DCS_SERIES_NO_01 - [11:5] */
#define WM8994_DCS_SERIES_NO_01_WIDTH                7  /* DCS_SERIES_NO_01 - [11:5] */
#define WM8994_DCS_TIMER_PERIOD_01_MASK         0x000F  /* DCS_TIMER_PERIOD_01 - [3:0] */
#define WM8994_DCS_TIMER_PERIOD_01_SHIFT             0  /* DCS_TIMER_PERIOD_01 - [3:0] */
#define WM8994_DCS_TIMER_PERIOD_01_WIDTH             4  /* DCS_TIMER_PERIOD_01 - [3:0] */

/*
 * R87 (0x57) - DC Servo (4)
 */
#define WM8994_DCS_DAC_WR_VAL_1_MASK            0xFF00  /* DCS_DAC_WR_VAL_1 - [15:8] */
#define WM8994_DCS_DAC_WR_VAL_1_SHIFT                8  /* DCS_DAC_WR_VAL_1 - [15:8] */
#define WM8994_DCS_DAC_WR_VAL_1_WIDTH                8  /* DCS_DAC_WR_VAL_1 - [15:8] */
#define WM8994_DCS_DAC_WR_VAL_0_MASK            0x00FF  /* DCS_DAC_WR_VAL_0 - [7:0] */
#define WM8994_DCS_DAC_WR_VAL_0_SHIFT                0  /* DCS_DAC_WR_VAL_0 - [7:0] */
#define WM8994_DCS_DAC_WR_VAL_0_WIDTH                8  /* DCS_DAC_WR_VAL_0 - [7:0] */

/*
 * R88 (0x58) - DC Servo Readback
 */
#define WM8994_DCS_CAL_COMPLETE_MASK            0x0300  /* DCS_CAL_COMPLETE - [9:8] */
#define WM8994_DCS_CAL_COMPLETE_SHIFT                8  /* DCS_CAL_COMPLETE - [9:8] */
#define WM8994_DCS_CAL_COMPLETE_WIDTH                2  /* DCS_CAL_COMPLETE - [9:8] */
#define WM8994_DCS_DAC_WR_COMPLETE_MASK         0x0030  /* DCS_DAC_WR_COMPLETE - [5:4] */
#define WM8994_DCS_DAC_WR_COMPLETE_SHIFT             4  /* DCS_DAC_WR_COMPLETE - [5:4] */
#define WM8994_DCS_DAC_WR_COMPLETE_WIDTH             2  /* DCS_DAC_WR_COMPLETE - [5:4] */
#define WM8994_DCS_STARTUP_COMPLETE_MASK        0x0003  /* DCS_STARTUP_COMPLETE - [1:0] */
#define WM8994_DCS_STARTUP_COMPLETE_SHIFT            0  /* DCS_STARTUP_COMPLETE - [1:0] */
#define WM8994_DCS_STARTUP_COMPLETE_WIDTH            2  /* DCS_STARTUP_COMPLETE - [1:0] */

/*
 * R96 (0x60) - Analogue HP (1)
 */
#define WM8994_HPOUT1L_RMV_SHORT                0x0080  /* HPOUT1L_RMV_SHORT */
#define WM8994_HPOUT1L_RMV_SHORT_MASK           0x0080  /* HPOUT1L_RMV_SHORT */
#define WM8994_HPOUT1L_RMV_SHORT_SHIFT               7  /* HPOUT1L_RMV_SHORT */
#define WM8994_HPOUT1L_RMV_SHORT_WIDTH               1  /* HPOUT1L_RMV_SHORT */
#define WM8994_HPOUT1L_OUTP                     0x0040  /* HPOUT1L_OUTP */
#define WM8994_HPOUT1L_OUTP_MASK                0x0040  /* HPOUT1L_OUTP */
#define WM8994_HPOUT1L_OUTP_SHIFT                    6  /* HPOUT1L_OUTP */
#define WM8994_HPOUT1L_OUTP_WIDTH                    1  /* HPOUT1L_OUTP */
#define WM8994_HPOUT1L_DLY                      0x0020  /* HPOUT1L_DLY */
#define WM8994_HPOUT1L_DLY_MASK                 0x0020  /* HPOUT1L_DLY */
#define WM8994_HPOUT1L_DLY_SHIFT                     5  /* HPOUT1L_DLY */
#define WM8994_HPOUT1L_DLY_WIDTH                     1  /* HPOUT1L_DLY */
#define WM8994_HPOUT1R_RMV_SHORT                0x0008  /* HPOUT1R_RMV_SHORT */
#define WM8994_HPOUT1R_RMV_SHORT_MASK           0x0008  /* HPOUT1R_RMV_SHORT */
#define WM8994_HPOUT1R_RMV_SHORT_SHIFT               3  /* HPOUT1R_RMV_SHORT */
#define WM8994_HPOUT1R_RMV_SHORT_WIDTH               1  /* HPOUT1R_RMV_SHORT */
#define WM8994_HPOUT1R_OUTP                     0x0004  /* HPOUT1R_OUTP */
#define WM8994_HPOUT1R_OUTP_MASK                0x0004  /* HPOUT1R_OUTP */
#define WM8994_HPOUT1R_OUTP_SHIFT                    2  /* HPOUT1R_OUTP */
#define WM8994_HPOUT1R_OUTP_WIDTH                    1  /* HPOUT1R_OUTP */
#define WM8994_HPOUT1R_DLY                      0x0002  /* HPOUT1R_DLY */
#define WM8994_HPOUT1R_DLY_MASK                 0x0002  /* HPOUT1R_DLY */
#define WM8994_HPOUT1R_DLY_SHIFT                     1  /* HPOUT1R_DLY */
#define WM8994_HPOUT1R_DLY_WIDTH                     1  /* HPOUT1R_DLY */

/*
 * R208 (0xD0) - Mic Detect 1
 */
#define WM8958_MICD_BIAS_STARTTIME_MASK         0xF000  /* MICD_BIAS_STARTTIME - [15:12] */
#define WM8958_MICD_BIAS_STARTTIME_SHIFT            12  /* MICD_BIAS_STARTTIME - [15:12] */
#define WM8958_MICD_BIAS_STARTTIME_WIDTH             4  /* MICD_BIAS_STARTTIME - [15:12] */
#define WM8958_MICD_RATE_MASK                   0x0F00  /* MICD_RATE - [11:8] */
#define WM8958_MICD_RATE_SHIFT                       8  /* MICD_RATE - [11:8] */
#define WM8958_MICD_RATE_WIDTH                       4  /* MICD_RATE - [11:8] */
#define WM8958_MICD_DBTIME                      0x0002  /* MICD_DBTIME */
#define WM8958_MICD_DBTIME_MASK                 0x0002  /* MICD_DBTIME */
#define WM8958_MICD_DBTIME_SHIFT                     1  /* MICD_DBTIME */
#define WM8958_MICD_DBTIME_WIDTH                     1  /* MICD_DBTIME */
#define WM8958_MICD_ENA                         0x0001  /* MICD_ENA */
#define WM8958_MICD_ENA_MASK                    0x0001  /* MICD_ENA */
#define WM8958_MICD_ENA_SHIFT                        0  /* MICD_ENA */
#define WM8958_MICD_ENA_WIDTH                        1  /* MICD_ENA */

/*
 * R209 (0xD1) - Mic Detect 2
 */
#define WM8958_MICD_LVL_SEL_MASK                0x00FF  /* MICD_LVL_SEL - [7:0] */
#define WM8958_MICD_LVL_SEL_SHIFT                    0  /* MICD_LVL_SEL - [7:0] */
#define WM8958_MICD_LVL_SEL_WIDTH                    8  /* MICD_LVL_SEL - [7:0] */

/*
 * R210 (0xD2) - Mic Detect 3
 */
#define WM8958_MICD_LVL_MASK                    0x07FC  /* MICD_LVL - [10:2] */
#define WM8958_MICD_LVL_SHIFT                        2  /* MICD_LVL - [10:2] */
#define WM8958_MICD_LVL_WIDTH                        9  /* MICD_LVL - [10:2] */
#define WM8958_MICD_VALID                       0x0002  /* MICD_VALID */
#define WM8958_MICD_VALID_MASK                  0x0002  /* MICD_VALID */
#define WM8958_MICD_VALID_SHIFT                      1  /* MICD_VALID */
#define WM8958_MICD_VALID_WIDTH                      1  /* MICD_VALID */
#define WM8958_MICD_STS                         0x0001  /* MICD_STS */
#define WM8958_MICD_STS_MASK                    0x0001  /* MICD_STS */
#define WM8958_MICD_STS_SHIFT                        0  /* MICD_STS */
#define WM8958_MICD_STS_WIDTH                        1  /* MICD_STS */

/*
 * R256 (0x100) - Chip Revision
 */
#define WM8994_CHIP_REV_MASK                    0x000F  /* CHIP_REV - [3:0] */
#define WM8994_CHIP_REV_SHIFT                        0  /* CHIP_REV - [3:0] */
#define WM8994_CHIP_REV_WIDTH                        4  /* CHIP_REV - [3:0] */

/*
 * R257 (0x101) - Control Interface
 */
#define WM8994_SPI_CONTRD                       0x0040  /* SPI_CONTRD */
#define WM8994_SPI_CONTRD_MASK                  0x0040  /* SPI_CONTRD */
#define WM8994_SPI_CONTRD_SHIFT                      6  /* SPI_CONTRD */
#define WM8994_SPI_CONTRD_WIDTH                      1  /* SPI_CONTRD */
#define WM8994_SPI_4WIRE                        0x0020  /* SPI_4WIRE */
#define WM8994_SPI_4WIRE_MASK                   0x0020  /* SPI_4WIRE */
#define WM8994_SPI_4WIRE_SHIFT                       5  /* SPI_4WIRE */
#define WM8994_SPI_4WIRE_WIDTH                       1  /* SPI_4WIRE */
#define WM8994_SPI_CFG                          0x0010  /* SPI_CFG */
#define WM8994_SPI_CFG_MASK                     0x0010  /* SPI_CFG */
#define WM8994_SPI_CFG_SHIFT                         4  /* SPI_CFG */
#define WM8994_SPI_CFG_WIDTH                         1  /* SPI_CFG */
#define WM8994_AUTO_INC                         0x0004  /* AUTO_INC */
#define WM8994_AUTO_INC_MASK                    0x0004  /* AUTO_INC */
#define WM8994_AUTO_INC_SHIFT                        2  /* AUTO_INC */
#define WM8994_AUTO_INC_WIDTH                        1  /* AUTO_INC */

/*
 * R272 (0x110) - Write Sequencer Ctrl (1)
 */
#define WM8994_WSEQ_ENA                         0x8000  /* WSEQ_ENA */
#define WM8994_WSEQ_ENA_MASK                    0x8000  /* WSEQ_ENA */
#define WM8994_WSEQ_ENA_SHIFT                       15  /* WSEQ_ENA */
#define WM8994_WSEQ_ENA_WIDTH                        1  /* WSEQ_ENA */
#define WM8994_WSEQ_ABORT                       0x0200  /* WSEQ_ABORT */
#define WM8994_WSEQ_ABORT_MASK                  0x0200  /* WSEQ_ABORT */
#define WM8994_WSEQ_ABORT_SHIFT                      9  /* WSEQ_ABORT */
#define WM8994_WSEQ_ABORT_WIDTH                      1  /* WSEQ_ABORT */
#define WM8994_WSEQ_START                       0x0100  /* WSEQ_START */
#define WM8994_WSEQ_START_MASK                  0x0100  /* WSEQ_START */
#define WM8994_WSEQ_START_SHIFT                      8  /* WSEQ_START */
#define WM8994_WSEQ_START_WIDTH                      1  /* WSEQ_START */
#define WM8994_WSEQ_START_INDEX_MASK            0x007F  /* WSEQ_START_INDEX - [6:0] */
#define WM8994_WSEQ_START_INDEX_SHIFT                0  /* WSEQ_START_INDEX - [6:0] */
#define WM8994_WSEQ_START_INDEX_WIDTH                7  /* WSEQ_START_INDEX - [6:0] */

/*
 * R273 (0x111) - Write Sequencer Ctrl (2)
 */
#define WM8994_WSEQ_BUSY                        0x0100  /* WSEQ_BUSY */
#define WM8994_WSEQ_BUSY_MASK                   0x0100  /* WSEQ_BUSY */
#define WM8994_WSEQ_BUSY_SHIFT                       8  /* WSEQ_BUSY */
#define WM8994_WSEQ_BUSY_WIDTH                       1  /* WSEQ_BUSY */
#define WM8994_WSEQ_CURRENT_INDEX_MASK          0x007F  /* WSEQ_CURRENT_INDEX - [6:0] */
#define WM8994_WSEQ_CURRENT_INDEX_SHIFT              0  /* WSEQ_CURRENT_INDEX - [6:0] */
#define WM8994_WSEQ_CURRENT_INDEX_WIDTH              7  /* WSEQ_CURRENT_INDEX - [6:0] */

/*
 * R512 (0x200) - AIF1 Clocking (1)
 */
#define WM8994_AIF1CLK_SRC_MASK                 0x0018  /* AIF1CLK_SRC - [4:3] */
#define WM8994_AIF1CLK_SRC_SHIFT                     3  /* AIF1CLK_SRC - [4:3] */
#define WM8994_AIF1CLK_SRC_WIDTH                     2  /* AIF1CLK_SRC - [4:3] */
#define WM8994_AIF1CLK_INV                      0x0004  /* AIF1CLK_INV */
#define WM8994_AIF1CLK_INV_MASK                 0x0004  /* AIF1CLK_INV */
#define WM8994_AIF1CLK_INV_SHIFT                     2  /* AIF1CLK_INV */
#define WM8994_AIF1CLK_INV_WIDTH                     1  /* AIF1CLK_INV */
#define WM8994_AIF1CLK_DIV                      0x0002  /* AIF1CLK_DIV */
#define WM8994_AIF1CLK_DIV_MASK                 0x0002  /* AIF1CLK_DIV */
#define WM8994_AIF1CLK_DIV_SHIFT                     1  /* AIF1CLK_DIV */
#define WM8994_AIF1CLK_DIV_WIDTH                     1  /* AIF1CLK_DIV */
#define WM8994_AIF1CLK_ENA                      0x0001  /* AIF1CLK_ENA */
#define WM8994_AIF1CLK_ENA_MASK                 0x0001  /* AIF1CLK_ENA */
#define WM8994_AIF1CLK_ENA_SHIFT                     0  /* AIF1CLK_ENA */
#define WM8994_AIF1CLK_ENA_WIDTH                     1  /* AIF1CLK_ENA */

/*
 * R513 (0x201) - AIF1 Clocking (2)
 */
#define WM8994_AIF1DAC_DIV_MASK                 0x0038  /* AIF1DAC_DIV - [5:3] */
#define WM8994_AIF1DAC_DIV_SHIFT                     3  /* AIF1DAC_DIV - [5:3] */
#define WM8994_AIF1DAC_DIV_WIDTH                     3  /* AIF1DAC_DIV - [5:3] */
#define WM8994_AIF1ADC_DIV_MASK                 0x0007  /* AIF1ADC_DIV - [2:0] */
#define WM8994_AIF1ADC_DIV_SHIFT                     0  /* AIF1ADC_DIV - [2:0] */
#define WM8994_AIF1ADC_DIV_WIDTH                     3  /* AIF1ADC_DIV - [2:0] */

/*
 * R516 (0x204) - AIF2 Clocking (1)
 */
#define WM8994_AIF2CLK_SRC_MASK                 0x0018  /* AIF2CLK_SRC - [4:3] */
#define WM8994_AIF2CLK_SRC_SHIFT                     3  /* AIF2CLK_SRC - [4:3] */
#define WM8994_AIF2CLK_SRC_WIDTH                     2  /* AIF2CLK_SRC - [4:3] */
#define WM8994_AIF2CLK_INV                      0x0004  /* AIF2CLK_INV */
#define WM8994_AIF2CLK_INV_MASK                 0x0004  /* AIF2CLK_INV */
#define WM8994_AIF2CLK_INV_SHIFT                     2  /* AIF2CLK_INV */
#define WM8994_AIF2CLK_INV_WIDTH                     1  /* AIF2CLK_INV */
#define WM8994_AIF2CLK_DIV                      0x0002  /* AIF2CLK_DIV */
#define WM8994_AIF2CLK_DIV_MASK                 0x0002  /* AIF2CLK_DIV */
#define WM8994_AIF2CLK_DIV_SHIFT                     1  /* AIF2CLK_DIV */
#define WM8994_AIF2CLK_DIV_WIDTH                     1  /* AIF2CLK_DIV */
#define WM8994_AIF2CLK_ENA                      0x0001  /* AIF2CLK_ENA */
#define WM8994_AIF2CLK_ENA_MASK                 0x0001  /* AIF2CLK_ENA */
#define WM8994_AIF2CLK_ENA_SHIFT                     0  /* AIF2CLK_ENA */
#define WM8994_AIF2CLK_ENA_WIDTH                     1  /* AIF2CLK_ENA */

/*
 * R517 (0x205) - AIF2 Clocking (2)
 */
#define WM8994_AIF2DAC_DIV_MASK                 0x0038  /* AIF2DAC_DIV - [5:3] */
#define WM8994_AIF2DAC_DIV_SHIFT                     3  /* AIF2DAC_DIV - [5:3] */
#define WM8994_AIF2DAC_DIV_WIDTH                     3  /* AIF2DAC_DIV - [5:3] */
#define WM8994_AIF2ADC_DIV_MASK                 0x0007  /* AIF2ADC_DIV - [2:0] */
#define WM8994_AIF2ADC_DIV_SHIFT                     0  /* AIF2ADC_DIV - [2:0] */
#define WM8994_AIF2ADC_DIV_WIDTH                     3  /* AIF2ADC_DIV - [2:0] */

/*
 * R520 (0x208) - Clocking (1)
 */
#define WM8958_DSP2CLK_ENA                      0x4000  /* DSP2CLK_ENA */
#define WM8958_DSP2CLK_ENA_MASK                 0x4000  /* DSP2CLK_ENA */
#define WM8958_DSP2CLK_ENA_SHIFT                    14  /* DSP2CLK_ENA */
#define WM8958_DSP2CLK_ENA_WIDTH                     1  /* DSP2CLK_ENA */
#define WM8958_DSP2CLK_SRC                      0x1000  /* DSP2CLK_SRC */
#define WM8958_DSP2CLK_SRC_MASK                 0x1000  /* DSP2CLK_SRC */
#define WM8958_DSP2CLK_SRC_SHIFT                    12  /* DSP2CLK_SRC */
#define WM8958_DSP2CLK_SRC_WIDTH                     1  /* DSP2CLK_SRC */
#define WM8994_TOCLK_ENA                        0x0010  /* TOCLK_ENA */
#define WM8994_TOCLK_ENA_MASK                   0x0010  /* TOCLK_ENA */
#define WM8994_TOCLK_ENA_SHIFT                       4  /* TOCLK_ENA */
#define WM8994_TOCLK_ENA_WIDTH                       1  /* TOCLK_ENA */
#define WM8994_AIF1DSPCLK_ENA                   0x0008  /* AIF1DSPCLK_ENA */
#define WM8994_AIF1DSPCLK_ENA_MASK              0x0008  /* AIF1DSPCLK_ENA */
#define WM8994_AIF1DSPCLK_ENA_SHIFT                  3  /* AIF1DSPCLK_ENA */
#define WM8994_AIF1DSPCLK_ENA_WIDTH                  1  /* AIF1DSPCLK_ENA */
#define WM8994_AIF2DSPCLK_ENA                   0x0004  /* AIF2DSPCLK_ENA */
#define WM8994_AIF2DSPCLK_ENA_MASK              0x0004  /* AIF2DSPCLK_ENA */
#define WM8994_AIF2DSPCLK_ENA_SHIFT                  2  /* AIF2DSPCLK_ENA */
#define WM8994_AIF2DSPCLK_ENA_WIDTH                  1  /* AIF2DSPCLK_ENA */
#define WM8994_SYSDSPCLK_ENA                    0x0002  /* SYSDSPCLK_ENA */
#define WM8994_SYSDSPCLK_ENA_MASK               0x0002  /* SYSDSPCLK_ENA */
#define WM8994_SYSDSPCLK_ENA_SHIFT                   1  /* SYSDSPCLK_ENA */
#define WM8994_SYSDSPCLK_ENA_WIDTH                   1  /* SYSDSPCLK_ENA */
#define WM8994_SYSCLK_SRC                       0x0001  /* SYSCLK_SRC */
#define WM8994_SYSCLK_SRC_MASK                  0x0001  /* SYSCLK_SRC */
#define WM8994_SYSCLK_SRC_SHIFT                      0  /* SYSCLK_SRC */
#define WM8994_SYSCLK_SRC_WIDTH                      1  /* SYSCLK_SRC */

/*
 * R521 (0x209) - Clocking (2)
 */
#define WM8994_TOCLK_DIV_MASK                   0x0700  /* TOCLK_DIV - [10:8] */
#define WM8994_TOCLK_DIV_SHIFT                       8  /* TOCLK_DIV - [10:8] */
#define WM8994_TOCLK_DIV_WIDTH                       3  /* TOCLK_DIV - [10:8] */
#define WM8994_DBCLK_DIV_MASK                   0x0070  /* DBCLK_DIV - [6:4] */
#define WM8994_DBCLK_DIV_SHIFT                       4  /* DBCLK_DIV - [6:4] */
#define WM8994_DBCLK_DIV_WIDTH                       3  /* DBCLK_DIV - [6:4] */
#define WM8994_OPCLK_DIV_MASK                   0x0007  /* OPCLK_DIV - [2:0] */
#define WM8994_OPCLK_DIV_SHIFT                       0  /* OPCLK_DIV - [2:0] */
#define WM8994_OPCLK_DIV_WIDTH                       3  /* OPCLK_DIV - [2:0] */

/*
 * R528 (0x210) - AIF1 Rate
 */
#define WM8994_AIF1_SR_MASK                     0x00F0  /* AIF1_SR - [7:4] */
#define WM8994_AIF1_SR_SHIFT                         4  /* AIF1_SR - [7:4] */
#define WM8994_AIF1_SR_WIDTH                         4  /* AIF1_SR - [7:4] */
#define WM8994_AIF1CLK_RATE_MASK                0x000F  /* AIF1CLK_RATE - [3:0] */
#define WM8994_AIF1CLK_RATE_SHIFT                    0  /* AIF1CLK_RATE - [3:0] */
#define WM8994_AIF1CLK_RATE_WIDTH                    4  /* AIF1CLK_RATE - [3:0] */

/*
 * R529 (0x211) - AIF2 Rate
 */
#define WM8994_AIF2_SR_MASK                     0x00F0  /* AIF2_SR - [7:4] */
#define WM8994_AIF2_SR_SHIFT                         4  /* AIF2_SR - [7:4] */
#define WM8994_AIF2_SR_WIDTH                         4  /* AIF2_SR - [7:4] */
#define WM8994_AIF2CLK_RATE_MASK                0x000F  /* AIF2CLK_RATE - [3:0] */
#define WM8994_AIF2CLK_RATE_SHIFT                    0  /* AIF2CLK_RATE - [3:0] */
#define WM8994_AIF2CLK_RATE_WIDTH                    4  /* AIF2CLK_RATE - [3:0] */

/*
 * R530 (0x212) - Rate Status
 */
#define WM8994_SR_ERROR_MASK                    0x000F  /* SR_ERROR - [3:0] */
#define WM8994_SR_ERROR_SHIFT                        0  /* SR_ERROR - [3:0] */
#define WM8994_SR_ERROR_WIDTH                        4  /* SR_ERROR - [3:0] */

/*
 * R544 (0x220) - FLL1 Control (1)
 */
#define WM8994_FLL1_FRAC                        0x0004  /* FLL1_FRAC */
#define WM8994_FLL1_FRAC_MASK                   0x0004  /* FLL1_FRAC */
#define WM8994_FLL1_FRAC_SHIFT                       2  /* FLL1_FRAC */
#define WM8994_FLL1_FRAC_WIDTH                       1  /* FLL1_FRAC */
#define WM8994_FLL1_OSC_ENA                     0x0002  /* FLL1_OSC_ENA */
#define WM8994_FLL1_OSC_ENA_MASK                0x0002  /* FLL1_OSC_ENA */
#define WM8994_FLL1_OSC_ENA_SHIFT                    1  /* FLL1_OSC_ENA */
#define WM8994_FLL1_OSC_ENA_WIDTH                    1  /* FLL1_OSC_ENA */
#define WM8994_FLL1_ENA                         0x0001  /* FLL1_ENA */
#define WM8994_FLL1_ENA_MASK                    0x0001  /* FLL1_ENA */
#define WM8994_FLL1_ENA_SHIFT                        0  /* FLL1_ENA */
#define WM8994_FLL1_ENA_WIDTH                        1  /* FLL1_ENA */

/*
 * R545 (0x221) - FLL1 Control (2)
 */
#define WM8994_FLL1_OUTDIV_MASK                 0x3F00  /* FLL1_OUTDIV - [13:8] */
#define WM8994_FLL1_OUTDIV_SHIFT                     8  /* FLL1_OUTDIV - [13:8] */
#define WM8994_FLL1_OUTDIV_WIDTH                     6  /* FLL1_OUTDIV - [13:8] */
#define WM8994_FLL1_CTRL_RATE_MASK              0x0070  /* FLL1_CTRL_RATE - [6:4] */
#define WM8994_FLL1_CTRL_RATE_SHIFT                  4  /* FLL1_CTRL_RATE - [6:4] */
#define WM8994_FLL1_CTRL_RATE_WIDTH                  3  /* FLL1_CTRL_RATE - [6:4] */
#define WM8994_FLL1_FRATIO_MASK                 0x0007  /* FLL1_FRATIO - [2:0] */
#define WM8994_FLL1_FRATIO_SHIFT                     0  /* FLL1_FRATIO - [2:0] */
#define WM8994_FLL1_FRATIO_WIDTH                     3  /* FLL1_FRATIO - [2:0] */

/*
 * R546 (0x222) - FLL1 Control (3)
 */
#define WM8994_FLL1_K_MASK                      0xFFFF  /* FLL1_K - [15:0] */
#define WM8994_FLL1_K_SHIFT                          0  /* FLL1_K - [15:0] */
#define WM8994_FLL1_K_WIDTH                         16  /* FLL1_K - [15:0] */

/*
 * R547 (0x223) - FLL1 Control (4)
 */
#define WM8994_FLL1_N_MASK                      0x7FE0  /* FLL1_N - [14:5] */
#define WM8994_FLL1_N_SHIFT                          5  /* FLL1_N - [14:5] */
#define WM8994_FLL1_N_WIDTH                         10  /* FLL1_N - [14:5] */
#define WM8994_FLL1_LOOP_GAIN_MASK              0x000F  /* FLL1_LOOP_GAIN - [3:0] */
#define WM8994_FLL1_LOOP_GAIN_SHIFT                  0  /* FLL1_LOOP_GAIN - [3:0] */
#define WM8994_FLL1_LOOP_GAIN_WIDTH                  4  /* FLL1_LOOP_GAIN - [3:0] */

/*
 * R548 (0x224) - FLL1 Control (5)
 */
#define WM8994_FLL1_FRC_NCO_VAL_MASK            0x1F80  /* FLL1_FRC_NCO_VAL - [12:7] */
#define WM8994_FLL1_FRC_NCO_VAL_SHIFT                7  /* FLL1_FRC_NCO_VAL - [12:7] */
#define WM8994_FLL1_FRC_NCO_VAL_WIDTH                6  /* FLL1_FRC_NCO_VAL - [12:7] */
#define WM8994_FLL1_FRC_NCO                     0x0040  /* FLL1_FRC_NCO */
#define WM8994_FLL1_FRC_NCO_MASK                0x0040  /* FLL1_FRC_NCO */
#define WM8994_FLL1_FRC_NCO_SHIFT                    6  /* FLL1_FRC_NCO */
#define WM8994_FLL1_FRC_NCO_WIDTH                    1  /* FLL1_FRC_NCO */
#define WM8994_FLL1_REFCLK_DIV_MASK             0x0018  /* FLL1_REFCLK_DIV - [4:3] */
#define WM8994_FLL1_REFCLK_DIV_SHIFT                 3  /* FLL1_REFCLK_DIV - [4:3] */
#define WM8994_FLL1_REFCLK_DIV_WIDTH                 2  /* FLL1_REFCLK_DIV - [4:3] */
#define WM8994_FLL1_REFCLK_SRC_MASK             0x0003  /* FLL1_REFCLK_SRC - [1:0] */
#define WM8994_FLL1_REFCLK_SRC_SHIFT                 0  /* FLL1_REFCLK_SRC - [1:0] */
#define WM8994_FLL1_REFCLK_SRC_WIDTH                 2  /* FLL1_REFCLK_SRC - [1:0] */

/*
 * R576 (0x240) - FLL2 Control (1)
 */
#define WM8994_FLL2_FRAC                        0x0004  /* FLL2_FRAC */
#define WM8994_FLL2_FRAC_MASK                   0x0004  /* FLL2_FRAC */
#define WM8994_FLL2_FRAC_SHIFT                       2  /* FLL2_FRAC */
#define WM8994_FLL2_FRAC_WIDTH                       1  /* FLL2_FRAC */
#define WM8994_FLL2_OSC_ENA                     0x0002  /* FLL2_OSC_ENA */
#define WM8994_FLL2_OSC_ENA_MASK                0x0002  /* FLL2_OSC_ENA */
#define WM8994_FLL2_OSC_ENA_SHIFT                    1  /* FLL2_OSC_ENA */
#define WM8994_FLL2_OSC_ENA_WIDTH                    1  /* FLL2_OSC_ENA */
#define WM8994_FLL2_ENA                         0x0001  /* FLL2_ENA */
#define WM8994_FLL2_ENA_MASK                    0x0001  /* FLL2_ENA */
#define WM8994_FLL2_ENA_SHIFT                        0  /* FLL2_ENA */
#define WM8994_FLL2_ENA_WIDTH                        1  /* FLL2_ENA */

/*
 * R577 (0x241) - FLL2 Control (2)
 */
#define WM8994_FLL2_OUTDIV_MASK                 0x3F00  /* FLL2_OUTDIV - [13:8] */
#define WM8994_FLL2_OUTDIV_SHIFT                     8  /* FLL2_OUTDIV - [13:8] */
#define WM8994_FLL2_OUTDIV_WIDTH                     6  /* FLL2_OUTDIV - [13:8] */
#define WM8994_FLL2_CTRL_RATE_MASK              0x0070  /* FLL2_CTRL_RATE - [6:4] */
#define WM8994_FLL2_CTRL_RATE_SHIFT                  4  /* FLL2_CTRL_RATE - [6:4] */
#define WM8994_FLL2_CTRL_RATE_WIDTH                  3  /* FLL2_CTRL_RATE - [6:4] */
#define WM8994_FLL2_FRATIO_MASK                 0x0007  /* FLL2_FRATIO - [2:0] */
#define WM8994_FLL2_FRATIO_SHIFT                     0  /* FLL2_FRATIO - [2:0] */
#define WM8994_FLL2_FRATIO_WIDTH                     3  /* FLL2_FRATIO - [2:0] */

/*
 * R578 (0x242) - FLL2 Control (3)
 */
#define WM8994_FLL2_K_MASK                      0xFFFF  /* FLL2_K - [15:0] */
#define WM8994_FLL2_K_SHIFT                          0  /* FLL2_K - [15:0] */
#define WM8994_FLL2_K_WIDTH                         16  /* FLL2_K - [15:0] */

/*
 * R579 (0x243) - FLL2 Control (4)
 */
#define WM8994_FLL2_N_MASK                      0x7FE0  /* FLL2_N - [14:5] */
#define WM8994_FLL2_N_SHIFT                          5  /* FLL2_N - [14:5] */
#define WM8994_FLL2_N_WIDTH                         10  /* FLL2_N - [14:5] */
#define WM8994_FLL2_LOOP_GAIN_MASK              0x000F  /* FLL2_LOOP_GAIN - [3:0] */
#define WM8994_FLL2_LOOP_GAIN_SHIFT                  0  /* FLL2_LOOP_GAIN - [3:0] */
#define WM8994_FLL2_LOOP_GAIN_WIDTH                  4  /* FLL2_LOOP_GAIN - [3:0] */

/*
 * R580 (0x244) - FLL2 Control (5)
 */
#define WM8994_FLL2_FRC_NCO_VAL_MASK            0x1F80  /* FLL2_FRC_NCO_VAL - [12:7] */
#define WM8994_FLL2_FRC_NCO_VAL_SHIFT                7  /* FLL2_FRC_NCO_VAL - [12:7] */
#define WM8994_FLL2_FRC_NCO_VAL_WIDTH                6  /* FLL2_FRC_NCO_VAL - [12:7] */
#define WM8994_FLL2_FRC_NCO                     0x0040  /* FLL2_FRC_NCO */
#define WM8994_FLL2_FRC_NCO_MASK                0x0040  /* FLL2_FRC_NCO */
#define WM8994_FLL2_FRC_NCO_SHIFT                    6  /* FLL2_FRC_NCO */
#define WM8994_FLL2_FRC_NCO_WIDTH                    1  /* FLL2_FRC_NCO */
#define WM8994_FLL2_REFCLK_DIV_MASK             0x0018  /* FLL2_REFCLK_DIV - [4:3] */
#define WM8994_FLL2_REFCLK_DIV_SHIFT                 3  /* FLL2_REFCLK_DIV - [4:3] */
#define WM8994_FLL2_REFCLK_DIV_WIDTH                 2  /* FLL2_REFCLK_DIV - [4:3] */
#define WM8994_FLL2_REFCLK_SRC_MASK             0x0003  /* FLL2_REFCLK_SRC - [1:0] */
#define WM8994_FLL2_REFCLK_SRC_SHIFT                 0  /* FLL2_REFCLK_SRC - [1:0] */
#define WM8994_FLL2_REFCLK_SRC_WIDTH                 2  /* FLL2_REFCLK_SRC - [1:0] */

/*
 * R768 (0x300) - AIF1 Control (1)
 */
#define WM8994_AIF1ADCL_SRC                     0x8000  /* AIF1ADCL_SRC */
#define WM8994_AIF1ADCL_SRC_MASK                0x8000  /* AIF1ADCL_SRC */
#define WM8994_AIF1ADCL_SRC_SHIFT                   15  /* AIF1ADCL_SRC */
#define WM8994_AIF1ADCL_SRC_WIDTH                    1  /* AIF1ADCL_SRC */
#define WM8994_AIF1ADCR_SRC                     0x4000  /* AIF1ADCR_SRC */
#define WM8994_AIF1ADCR_SRC_MASK                0x4000  /* AIF1ADCR_SRC */
#define WM8994_AIF1ADCR_SRC_SHIFT                   14  /* AIF1ADCR_SRC */
#define WM8994_AIF1ADCR_SRC_WIDTH                    1  /* AIF1ADCR_SRC */
#define WM8994_AIF1ADC_TDM                      0x2000  /* AIF1ADC_TDM */
#define WM8994_AIF1ADC_TDM_MASK                 0x2000  /* AIF1ADC_TDM */
#define WM8994_AIF1ADC_TDM_SHIFT                    13  /* AIF1ADC_TDM */
#define WM8994_AIF1ADC_TDM_WIDTH                     1  /* AIF1ADC_TDM */
#define WM8994_AIF1_BCLK_INV                    0x0100  /* AIF1_BCLK_INV */
#define WM8994_AIF1_BCLK_INV_MASK               0x0100  /* AIF1_BCLK_INV */
#define WM8994_AIF1_BCLK_INV_SHIFT                   8  /* AIF1_BCLK_INV */
#define WM8994_AIF1_BCLK_INV_WIDTH                   1  /* AIF1_BCLK_INV */
#define WM8994_AIF1_LRCLK_INV                   0x0080  /* AIF1_LRCLK_INV */
#define WM8994_AIF1_LRCLK_INV_MASK              0x0080  /* AIF1_LRCLK_INV */
#define WM8994_AIF1_LRCLK_INV_SHIFT                  7  /* AIF1_LRCLK_INV */
#define WM8994_AIF1_LRCLK_INV_WIDTH                  1  /* AIF1_LRCLK_INV */
#define WM8994_AIF1_WL_MASK                     0x0060  /* AIF1_WL - [6:5] */
#define WM8994_AIF1_WL_SHIFT                         5  /* AIF1_WL - [6:5] */
#define WM8994_AIF1_WL_WIDTH                         2  /* AIF1_WL - [6:5] */
#define WM8994_AIF1_FMT_MASK                    0x0018  /* AIF1_FMT - [4:3] */
#define WM8994_AIF1_FMT_SHIFT                        3  /* AIF1_FMT - [4:3] */
#define WM8994_AIF1_FMT_WIDTH                        2  /* AIF1_FMT - [4:3] */

/*
 * R769 (0x301) - AIF1 Control (2)
 */
#define WM8994_AIF1DACL_SRC                     0x8000  /* AIF1DACL_SRC */
#define WM8994_AIF1DACL_SRC_MASK                0x8000  /* AIF1DACL_SRC */
#define WM8994_AIF1DACL_SRC_SHIFT                   15  /* AIF1DACL_SRC */
#define WM8994_AIF1DACL_SRC_WIDTH                    1  /* AIF1DACL_SRC */
#define WM8994_AIF1DACR_SRC                     0x4000  /* AIF1DACR_SRC */
#define WM8994_AIF1DACR_SRC_MASK                0x4000  /* AIF1DACR_SRC */
#define WM8994_AIF1DACR_SRC_SHIFT                   14  /* AIF1DACR_SRC */
#define WM8994_AIF1DACR_SRC_WIDTH                    1  /* AIF1DACR_SRC */
#define WM8994_AIF1DAC_BOOST_MASK               0x0C00  /* AIF1DAC_BOOST - [11:10] */
#define WM8994_AIF1DAC_BOOST_SHIFT                  10  /* AIF1DAC_BOOST - [11:10] */
#define WM8994_AIF1DAC_BOOST_WIDTH                   2  /* AIF1DAC_BOOST - [11:10] */
#define WM8994_AIF1_MONO                        0x0100  /* AIF1_MONO */
#define WM8994_AIF1_MONO_MASK                   0x0100  /* AIF1_MONO */
#define WM8994_AIF1_MONO_SHIFT                       8  /* AIF1_MONO */
#define WM8994_AIF1_MONO_WIDTH                       1  /* AIF1_MONO */
#define WM8994_AIF1DAC_COMP                     0x0010  /* AIF1DAC_COMP */
#define WM8994_AIF1DAC_COMP_MASK                0x0010  /* AIF1DAC_COMP */
#define WM8994_AIF1DAC_COMP_SHIFT                    4  /* AIF1DAC_COMP */
#define WM8994_AIF1DAC_COMP_WIDTH                    1  /* AIF1DAC_COMP */
#define WM8994_AIF1DAC_COMPMODE                 0x0008  /* AIF1DAC_COMPMODE */
#define WM8994_AIF1DAC_COMPMODE_MASK            0x0008  /* AIF1DAC_COMPMODE */
#define WM8994_AIF1DAC_COMPMODE_SHIFT                3  /* AIF1DAC_COMPMODE */
#define WM8994_AIF1DAC_COMPMODE_WIDTH                1  /* AIF1DAC_COMPMODE */
#define WM8994_AIF1ADC_COMP                     0x0004  /* AIF1ADC_COMP */
#define WM8994_AIF1ADC_COMP_MASK                0x0004  /* AIF1ADC_COMP */
#define WM8994_AIF1ADC_COMP_SHIFT                    2  /* AIF1ADC_COMP */
#define WM8994_AIF1ADC_COMP_WIDTH                    1  /* AIF1ADC_COMP */
#define WM8994_AIF1ADC_COMPMODE                 0x0002  /* AIF1ADC_COMPMODE */
#define WM8994_AIF1ADC_COMPMODE_MASK            0x0002  /* AIF1ADC_COMPMODE */
#define WM8994_AIF1ADC_COMPMODE_SHIFT                1  /* AIF1ADC_COMPMODE */
#define WM8994_AIF1ADC_COMPMODE_WIDTH                1  /* AIF1ADC_COMPMODE */
#define WM8994_AIF1_LOOPBACK                    0x0001  /* AIF1_LOOPBACK */
#define WM8994_AIF1_LOOPBACK_MASK               0x0001  /* AIF1_LOOPBACK */
#define WM8994_AIF1_LOOPBACK_SHIFT                   0  /* AIF1_LOOPBACK */
#define WM8994_AIF1_LOOPBACK_WIDTH                   1  /* AIF1_LOOPBACK */

/*
 * R770 (0x302) - AIF1 Master/Slave
 */
#define WM8994_AIF1_TRI                         0x8000  /* AIF1_TRI */
#define WM8994_AIF1_TRI_MASK                    0x8000  /* AIF1_TRI */
#define WM8994_AIF1_TRI_SHIFT                       15  /* AIF1_TRI */
#define WM8994_AIF1_TRI_WIDTH                        1  /* AIF1_TRI */
#define WM8994_AIF1_MSTR                        0x4000  /* AIF1_MSTR */
#define WM8994_AIF1_MSTR_MASK                   0x4000  /* AIF1_MSTR */
#define WM8994_AIF1_MSTR_SHIFT                      14  /* AIF1_MSTR */
#define WM8994_AIF1_MSTR_WIDTH                       1  /* AIF1_MSTR */
#define WM8994_AIF1_CLK_FRC                     0x2000  /* AIF1_CLK_FRC */
#define WM8994_AIF1_CLK_FRC_MASK                0x2000  /* AIF1_CLK_FRC */
#define WM8994_AIF1_CLK_FRC_SHIFT                   13  /* AIF1_CLK_FRC */
#define WM8994_AIF1_CLK_FRC_WIDTH                    1  /* AIF1_CLK_FRC */
#define WM8994_AIF1_LRCLK_FRC                   0x1000  /* AIF1_LRCLK_FRC */
#define WM8994_AIF1_LRCLK_FRC_MASK              0x1000  /* AIF1_LRCLK_FRC */
#define WM8994_AIF1_LRCLK_FRC_SHIFT                 12  /* AIF1_LRCLK_FRC */
#define WM8994_AIF1_LRCLK_FRC_WIDTH                  1  /* AIF1_LRCLK_FRC */

/*
 * R771 (0x303) - AIF1 BCLK
 */
#define WM8994_AIF1_BCLK_DIV_MASK               0x01F0  /* AIF1_BCLK_DIV - [8:4] */
#define WM8994_AIF1_BCLK_DIV_SHIFT                   4  /* AIF1_BCLK_DIV - [8:4] */
#define WM8994_AIF1_BCLK_DIV_WIDTH                   5  /* AIF1_BCLK_DIV - [8:4] */

/*
 * R772 (0x304) - AIF1ADC LRCLK
 */
#define WM8994_AIF1ADC_LRCLK_DIR                0x0800  /* AIF1ADC_LRCLK_DIR */
#define WM8994_AIF1ADC_LRCLK_DIR_MASK           0x0800  /* AIF1ADC_LRCLK_DIR */
#define WM8994_AIF1ADC_LRCLK_DIR_SHIFT              11  /* AIF1ADC_LRCLK_DIR */
#define WM8994_AIF1ADC_LRCLK_DIR_WIDTH               1  /* AIF1ADC_LRCLK_DIR */
#define WM8994_AIF1ADC_RATE_MASK                0x07FF  /* AIF1ADC_RATE - [10:0] */
#define WM8994_AIF1ADC_RATE_SHIFT                    0  /* AIF1ADC_RATE - [10:0] */
#define WM8994_AIF1ADC_RATE_WIDTH                   11  /* AIF1ADC_RATE - [10:0] */

/*
 * R773 (0x305) - AIF1DAC LRCLK
 */
#define WM8994_AIF1DAC_LRCLK_DIR                0x0800  /* AIF1DAC_LRCLK_DIR */
#define WM8994_AIF1DAC_LRCLK_DIR_MASK           0x0800  /* AIF1DAC_LRCLK_DIR */
#define WM8994_AIF1DAC_LRCLK_DIR_SHIFT              11  /* AIF1DAC_LRCLK_DIR */
#define WM8994_AIF1DAC_LRCLK_DIR_WIDTH               1  /* AIF1DAC_LRCLK_DIR */
#define WM8994_AIF1DAC_RATE_MASK                0x07FF  /* AIF1DAC_RATE - [10:0] */
#define WM8994_AIF1DAC_RATE_SHIFT                    0  /* AIF1DAC_RATE - [10:0] */
#define WM8994_AIF1DAC_RATE_WIDTH                   11  /* AIF1DAC_RATE - [10:0] */

/*
 * R774 (0x306) - AIF1DAC Data
 */
#define WM8994_AIF1DACL_DAT_INV                 0x0002  /* AIF1DACL_DAT_INV */
#define WM8994_AIF1DACL_DAT_INV_MASK            0x0002  /* AIF1DACL_DAT_INV */
#define WM8994_AIF1DACL_DAT_INV_SHIFT                1  /* AIF1DACL_DAT_INV */
#define WM8994_AIF1DACL_DAT_INV_WIDTH                1  /* AIF1DACL_DAT_INV */
#define WM8994_AIF1DACR_DAT_INV                 0x0001  /* AIF1DACR_DAT_INV */
#define WM8994_AIF1DACR_DAT_INV_MASK            0x0001  /* AIF1DACR_DAT_INV */
#define WM8994_AIF1DACR_DAT_INV_SHIFT                0  /* AIF1DACR_DAT_INV */
#define WM8994_AIF1DACR_DAT_INV_WIDTH                1  /* AIF1DACR_DAT_INV */

/*
 * R775 (0x307) - AIF1ADC Data
 */
#define WM8994_AIF1ADCL_DAT_INV                 0x0002  /* AIF1ADCL_DAT_INV */
#define WM8994_AIF1ADCL_DAT_INV_MASK            0x0002  /* AIF1ADCL_DAT_INV */
#define WM8994_AIF1ADCL_DAT_INV_SHIFT                1  /* AIF1ADCL_DAT_INV */
#define WM8994_AIF1ADCL_DAT_INV_WIDTH                1  /* AIF1ADCL_DAT_INV */
#define WM8994_AIF1ADCR_DAT_INV                 0x0001  /* AIF1ADCR_DAT_INV */
#define WM8994_AIF1ADCR_DAT_INV_MASK            0x0001  /* AIF1ADCR_DAT_INV */
#define WM8994_AIF1ADCR_DAT_INV_SHIFT                0  /* AIF1ADCR_DAT_INV */
#define WM8994_AIF1ADCR_DAT_INV_WIDTH                1  /* AIF1ADCR_DAT_INV */

/*
 * R784 (0x310) - AIF2 Control (1)
 */
#define WM8994_AIF2ADCL_SRC                     0x8000  /* AIF2ADCL_SRC */
#define WM8994_AIF2ADCL_SRC_MASK                0x8000  /* AIF2ADCL_SRC */
#define WM8994_AIF2ADCL_SRC_SHIFT                   15  /* AIF2ADCL_SRC */
#define WM8994_AIF2ADCL_SRC_WIDTH                    1  /* AIF2ADCL_SRC */
#define WM8994_AIF2ADCR_SRC                     0x4000  /* AIF2ADCR_SRC */
#define WM8994_AIF2ADCR_SRC_MASK                0x4000  /* AIF2ADCR_SRC */
#define WM8994_AIF2ADCR_SRC_SHIFT                   14  /* AIF2ADCR_SRC */
#define WM8994_AIF2ADCR_SRC_WIDTH                    1  /* AIF2ADCR_SRC */
#define WM8994_AIF2ADC_TDM                      0x2000  /* AIF2ADC_TDM */
#define WM8994_AIF2ADC_TDM_MASK                 0x2000  /* AIF2ADC_TDM */
#define WM8994_AIF2ADC_TDM_SHIFT                    13  /* AIF2ADC_TDM */
#define WM8994_AIF2ADC_TDM_WIDTH                     1  /* AIF2ADC_TDM */
#define WM8994_AIF2ADC_TDM_CHAN                 0x1000  /* AIF2ADC_TDM_CHAN */
#define WM8994_AIF2ADC_TDM_CHAN_MASK            0x1000  /* AIF2ADC_TDM_CHAN */
#define WM8994_AIF2ADC_TDM_CHAN_SHIFT               12  /* AIF2ADC_TDM_CHAN */
#define WM8994_AIF2ADC_TDM_CHAN_WIDTH                1  /* AIF2ADC_TDM_CHAN */
#define WM8994_AIF2_BCLK_INV                    0x0100  /* AIF2_BCLK_INV */
#define WM8994_AIF2_BCLK_INV_MASK               0x0100  /* AIF2_BCLK_INV */
#define WM8994_AIF2_BCLK_INV_SHIFT                   8  /* AIF2_BCLK_INV */
#define WM8994_AIF2_BCLK_INV_WIDTH                   1  /* AIF2_BCLK_INV */
#define WM8994_AIF2_LRCLK_INV                   0x0080  /* AIF2_LRCLK_INV */
#define WM8994_AIF2_LRCLK_INV_MASK              0x0080  /* AIF2_LRCLK_INV */
#define WM8994_AIF2_LRCLK_INV_SHIFT                  7  /* AIF2_LRCLK_INV */
#define WM8994_AIF2_LRCLK_INV_WIDTH                  1  /* AIF2_LRCLK_INV */
#define WM8994_AIF2_WL_MASK                     0x0060  /* AIF2_WL - [6:5] */
#define WM8994_AIF2_WL_SHIFT                         5  /* AIF2_WL - [6:5] */
#define WM8994_AIF2_WL_WIDTH                         2  /* AIF2_WL - [6:5] */
#define WM8994_AIF2_FMT_MASK                    0x0018  /* AIF2_FMT - [4:3] */
#define WM8994_AIF2_FMT_SHIFT                        3  /* AIF2_FMT - [4:3] */
#define WM8994_AIF2_FMT_WIDTH                        2  /* AIF2_FMT - [4:3] */

/*
 * R785 (0x311) - AIF2 Control (2)
 */
#define WM8994_AIF2DACL_SRC                     0x8000  /* AIF2DACL_SRC */
#define WM8994_AIF2DACL_SRC_MASK                0x8000  /* AIF2DACL_SRC */
#define WM8994_AIF2DACL_SRC_SHIFT                   15  /* AIF2DACL_SRC */
#define WM8994_AIF2DACL_SRC_WIDTH                    1  /* AIF2DACL_SRC */
#define WM8994_AIF2DACR_SRC                     0x4000  /* AIF2DACR_SRC */
#define WM8994_AIF2DACR_SRC_MASK                0x4000  /* AIF2DACR_SRC */
#define WM8994_AIF2DACR_SRC_SHIFT                   14  /* AIF2DACR_SRC */
#define WM8994_AIF2DACR_SRC_WIDTH                    1  /* AIF2DACR_SRC */
#define WM8994_AIF2DAC_TDM                      0x2000  /* AIF2DAC_TDM */
#define WM8994_AIF2DAC_TDM_MASK                 0x2000  /* AIF2DAC_TDM */
#define WM8994_AIF2DAC_TDM_SHIFT                    13  /* AIF2DAC_TDM */
#define WM8994_AIF2DAC_TDM_WIDTH                     1  /* AIF2DAC_TDM */
#define WM8994_AIF2DAC_TDM_CHAN                 0x1000  /* AIF2DAC_TDM_CHAN */
#define WM8994_AIF2DAC_TDM_CHAN_MASK            0x1000  /* AIF2DAC_TDM_CHAN */
#define WM8994_AIF2DAC_TDM_CHAN_SHIFT               12  /* AIF2DAC_TDM_CHAN */
#define WM8994_AIF2DAC_TDM_CHAN_WIDTH                1  /* AIF2DAC_TDM_CHAN */
#define WM8994_AIF2DAC_BOOST_MASK               0x0C00  /* AIF2DAC_BOOST - [11:10] */
#define WM8994_AIF2DAC_BOOST_SHIFT                  10  /* AIF2DAC_BOOST - [11:10] */
#define WM8994_AIF2DAC_BOOST_WIDTH                   2  /* AIF2DAC_BOOST - [11:10] */
#define WM8994_AIF2_MONO                        0x0100  /* AIF2_MONO */
#define WM8994_AIF2_MONO_MASK                   0x0100  /* AIF2_MONO */
#define WM8994_AIF2_MONO_SHIFT                       8  /* AIF2_MONO */
#define WM8994_AIF2_MONO_WIDTH                       1  /* AIF2_MONO */
#define WM8994_AIF2DAC_COMP                     0x0010  /* AIF2DAC_COMP */
#define WM8994_AIF2DAC_COMP_MASK                0x0010  /* AIF2DAC_COMP */
#define WM8994_AIF2DAC_COMP_SHIFT                    4  /* AIF2DAC_COMP */
#define WM8994_AIF2DAC_COMP_WIDTH                    1  /* AIF2DAC_COMP */
#define WM8994_AIF2DAC_COMPMODE                 0x0008  /* AIF2DAC_COMPMODE */
#define WM8994_AIF2DAC_COMPMODE_MASK            0x0008  /* AIF2DAC_COMPMODE */
#define WM8994_AIF2DAC_COMPMODE_SHIFT                3  /* AIF2DAC_COMPMODE */
#define WM8994_AIF2DAC_COMPMODE_WIDTH                1  /* AIF2DAC_COMPMODE */
#define WM8994_AIF2ADC_COMP                     0x0004  /* AIF2ADC_COMP */
#define WM8994_AIF2ADC_COMP_MASK                0x0004  /* AIF2ADC_COMP */
#define WM8994_AIF2ADC_COMP_SHIFT                    2  /* AIF2ADC_COMP */
#define WM8994_AIF2ADC_COMP_WIDTH                    1  /* AIF2ADC_COMP */
#define WM8994_AIF2ADC_COMPMODE                 0x0002  /* AIF2ADC_COMPMODE */
#define WM8994_AIF2ADC_COMPMODE_MASK            0x0002  /* AIF2ADC_COMPMODE */
#define WM8994_AIF2ADC_COMPMODE_SHIFT                1  /* AIF2ADC_COMPMODE */
#define WM8994_AIF2ADC_COMPMODE_WIDTH                1  /* AIF2ADC_COMPMODE */
#define WM8994_AIF2_LOOPBACK                    0x0001  /* AIF2_LOOPBACK */
#define WM8994_AIF2_LOOPBACK_MASK               0x0001  /* AIF2_LOOPBACK */
#define WM8994_AIF2_LOOPBACK_SHIFT                   0  /* AIF2_LOOPBACK */
#define WM8994_AIF2_LOOPBACK_WIDTH                   1  /* AIF2_LOOPBACK */

/*
 * R786 (0x312) - AIF2 Master/Slave
 */
#define WM8994_AIF2_TRI                         0x8000  /* AIF2_TRI */
#define WM8994_AIF2_TRI_MASK                    0x8000  /* AIF2_TRI */
#define WM8994_AIF2_TRI_SHIFT                       15  /* AIF2_TRI */
#define WM8994_AIF2_TRI_WIDTH                        1  /* AIF2_TRI */
#define WM8994_AIF2_MSTR                        0x4000  /* AIF2_MSTR */
#define WM8994_AIF2_MSTR_MASK                   0x4000  /* AIF2_MSTR */
#define WM8994_AIF2_MSTR_SHIFT                      14  /* AIF2_MSTR */
#define WM8994_AIF2_MSTR_WIDTH                       1  /* AIF2_MSTR */
#define WM8994_AIF2_CLK_FRC                     0x2000  /* AIF2_CLK_FRC */
#define WM8994_AIF2_CLK_FRC_MASK                0x2000  /* AIF2_CLK_FRC */
#define WM8994_AIF2_CLK_FRC_SHIFT                   13  /* AIF2_CLK_FRC */
#define WM8994_AIF2_CLK_FRC_WIDTH                    1  /* AIF2_CLK_FRC */
#define WM8994_AIF2_LRCLK_FRC                   0x1000  /* AIF2_LRCLK_FRC */
#define WM8994_AIF2_LRCLK_FRC_MASK              0x1000  /* AIF2_LRCLK_FRC */
#define WM8994_AIF2_LRCLK_FRC_SHIFT                 12  /* AIF2_LRCLK_FRC */
#define WM8994_AIF2_LRCLK_FRC_WIDTH                  1  /* AIF2_LRCLK_FRC */

/*
 * R787 (0x313) - AIF2 BCLK
 */
#define WM8994_AIF2_BCLK_DIV_MASK               0x01F0  /* AIF2_BCLK_DIV - [8:4] */
#define WM8994_AIF2_BCLK_DIV_SHIFT                   4  /* AIF2_BCLK_DIV - [8:4] */
#define WM8994_AIF2_BCLK_DIV_WIDTH                   5  /* AIF2_BCLK_DIV - [8:4] */

/*
 * R788 (0x314) - AIF2ADC LRCLK
 */
#define WM8994_AIF2ADC_LRCLK_DIR                0x0800  /* AIF2ADC_LRCLK_DIR */
#define WM8994_AIF2ADC_LRCLK_DIR_MASK           0x0800  /* AIF2ADC_LRCLK_DIR */
#define WM8994_AIF2ADC_LRCLK_DIR_SHIFT              11  /* AIF2ADC_LRCLK_DIR */
#define WM8994_AIF2ADC_LRCLK_DIR_WIDTH               1  /* AIF2ADC_LRCLK_DIR */
#define WM8994_AIF2ADC_RATE_MASK                0x07FF  /* AIF2ADC_RATE - [10:0] */
#define WM8994_AIF2ADC_RATE_SHIFT                    0  /* AIF2ADC_RATE - [10:0] */
#define WM8994_AIF2ADC_RATE_WIDTH                   11  /* AIF2ADC_RATE - [10:0] */

/*
 * R789 (0x315) - AIF2DAC LRCLK
 */
#define WM8994_AIF2DAC_LRCLK_DIR                0x0800  /* AIF2DAC_LRCLK_DIR */
#define WM8994_AIF2DAC_LRCLK_DIR_MASK           0x0800  /* AIF2DAC_LRCLK_DIR */
#define WM8994_AIF2DAC_LRCLK_DIR_SHIFT              11  /* AIF2DAC_LRCLK_DIR */
#define WM8994_AIF2DAC_LRCLK_DIR_WIDTH               1  /* AIF2DAC_LRCLK_DIR */
#define WM8994_AIF2DAC_RATE_MASK                0x07FF  /* AIF2DAC_RATE - [10:0] */
#define WM8994_AIF2DAC_RATE_SHIFT                    0  /* AIF2DAC_RATE - [10:0] */
#define WM8994_AIF2DAC_RATE_WIDTH                   11  /* AIF2DAC_RATE - [10:0] */

/*
 * R790 (0x316) - AIF2DAC Data
 */
#define WM8994_AIF2DACL_DAT_INV                 0x0002  /* AIF2DACL_DAT_INV */
#define WM8994_AIF2DACL_DAT_INV_MASK            0x0002  /* AIF2DACL_DAT_INV */
#define WM8994_AIF2DACL_DAT_INV_SHIFT                1  /* AIF2DACL_DAT_INV */
#define WM8994_AIF2DACL_DAT_INV_WIDTH                1  /* AIF2DACL_DAT_INV */
#define WM8994_AIF2DACR_DAT_INV                 0x0001  /* AIF2DACR_DAT_INV */
#define WM8994_AIF2DACR_DAT_INV_MASK            0x0001  /* AIF2DACR_DAT_INV */
#define WM8994_AIF2DACR_DAT_INV_SHIFT                0  /* AIF2DACR_DAT_INV */
#define WM8994_AIF2DACR_DAT_INV_WIDTH                1  /* AIF2DACR_DAT_INV */

/*
 * R791 (0x317) - AIF2ADC Data
 */
#define WM8994_AIF2ADCL_DAT_INV                 0x0002  /* AIF2ADCL_DAT_INV */
#define WM8994_AIF2ADCL_DAT_INV_MASK            0x0002  /* AIF2ADCL_DAT_INV */
#define WM8994_AIF2ADCL_DAT_INV_SHIFT                1  /* AIF2ADCL_DAT_INV */
#define WM8994_AIF2ADCL_DAT_INV_WIDTH                1  /* AIF2ADCL_DAT_INV */
#define WM8994_AIF2ADCR_DAT_INV                 0x0001  /* AIF2ADCR_DAT_INV */
#define WM8994_AIF2ADCR_DAT_INV_MASK            0x0001  /* AIF2ADCR_DAT_INV */
#define WM8994_AIF2ADCR_DAT_INV_SHIFT                0  /* AIF2ADCR_DAT_INV */
#define WM8994_AIF2ADCR_DAT_INV_WIDTH                1  /* AIF2ADCR_DAT_INV */

/*
 * R800 (0x320) - AIF3 Control (1)
 */
#define WM8958_AIF3_LRCLK_INV                   0x0080  /* AIF3_LRCLK_INV */
#define WM8958_AIF3_LRCLK_INV_MASK              0x0080  /* AIF3_LRCLK_INV */
#define WM8958_AIF3_LRCLK_INV_SHIFT                  7  /* AIF3_LRCLK_INV */
#define WM8958_AIF3_LRCLK_INV_WIDTH                  1  /* AIF3_LRCLK_INV */
#define WM8958_AIF3_WL_MASK                     0x0060  /* AIF3_WL - [6:5] */
#define WM8958_AIF3_WL_SHIFT                         5  /* AIF3_WL - [6:5] */
#define WM8958_AIF3_WL_WIDTH                         2  /* AIF3_WL - [6:5] */
#define WM8958_AIF3_FMT_MASK                    0x0018  /* AIF3_FMT - [4:3] */
#define WM8958_AIF3_FMT_SHIFT                        3  /* AIF3_FMT - [4:3] */
#define WM8958_AIF3_FMT_WIDTH                        2  /* AIF3_FMT - [4:3] */

/*
 * R801 (0x321) - AIF3 Control (2)
 */
#define WM8958_AIF3DAC_BOOST_MASK               0x0C00  /* AIF3DAC_BOOST - [11:10] */
#define WM8958_AIF3DAC_BOOST_SHIFT                  10  /* AIF3DAC_BOOST - [11:10] */
#define WM8958_AIF3DAC_BOOST_WIDTH                   2  /* AIF3DAC_BOOST - [11:10] */
#define WM8958_AIF3DAC_COMP                     0x0010  /* AIF3DAC_COMP */
#define WM8958_AIF3DAC_COMP_MASK                0x0010  /* AIF3DAC_COMP */
#define WM8958_AIF3DAC_COMP_SHIFT                    4  /* AIF3DAC_COMP */
#define WM8958_AIF3DAC_COMP_WIDTH                    1  /* AIF3DAC_COMP */
#define WM8958_AIF3DAC_COMPMODE                 0x0008  /* AIF3DAC_COMPMODE */
#define WM8958_AIF3DAC_COMPMODE_MASK            0x0008  /* AIF3DAC_COMPMODE */
#define WM8958_AIF3DAC_COMPMODE_SHIFT                3  /* AIF3DAC_COMPMODE */
#define WM8958_AIF3DAC_COMPMODE_WIDTH                1  /* AIF3DAC_COMPMODE */
#define WM8958_AIF3ADC_COMP                     0x0004  /* AIF3ADC_COMP */
#define WM8958_AIF3ADC_COMP_MASK                0x0004  /* AIF3ADC_COMP */
#define WM8958_AIF3ADC_COMP_SHIFT                    2  /* AIF3ADC_COMP */
#define WM8958_AIF3ADC_COMP_WIDTH                    1  /* AIF3ADC_COMP */
#define WM8958_AIF3ADC_COMPMODE                 0x0002  /* AIF3ADC_COMPMODE */
#define WM8958_AIF3ADC_COMPMODE_MASK            0x0002  /* AIF3ADC_COMPMODE */
#define WM8958_AIF3ADC_COMPMODE_SHIFT                1  /* AIF3ADC_COMPMODE */
#define WM8958_AIF3ADC_COMPMODE_WIDTH                1  /* AIF3ADC_COMPMODE */
#define WM8958_AIF3_LOOPBACK                    0x0001  /* AIF3_LOOPBACK */
#define WM8958_AIF3_LOOPBACK_MASK               0x0001  /* AIF3_LOOPBACK */
#define WM8958_AIF3_LOOPBACK_SHIFT                   0  /* AIF3_LOOPBACK */
#define WM8958_AIF3_LOOPBACK_WIDTH                   1  /* AIF3_LOOPBACK */

/*
 * R802 (0x322) - AIF3DAC Data
 */
#define WM8958_AIF3DAC_DAT_INV                  0x0001  /* AIF3DAC_DAT_INV */
#define WM8958_AIF3DAC_DAT_INV_MASK             0x0001  /* AIF3DAC_DAT_INV */
#define WM8958_AIF3DAC_DAT_INV_SHIFT                 0  /* AIF3DAC_DAT_INV */
#define WM8958_AIF3DAC_DAT_INV_WIDTH                 1  /* AIF3DAC_DAT_INV */

/*
 * R803 (0x323) - AIF3ADC Data
 */
#define WM8958_AIF3ADC_DAT_INV                  0x0001  /* AIF3ADC_DAT_INV */
#define WM8958_AIF3ADC_DAT_INV_MASK             0x0001  /* AIF3ADC_DAT_INV */
#define WM8958_AIF3ADC_DAT_INV_SHIFT                 0  /* AIF3ADC_DAT_INV */
#define WM8958_AIF3ADC_DAT_INV_WIDTH                 1  /* AIF3ADC_DAT_INV */

/*
 * R1024 (0x400) - AIF1 ADC1 Left Volume
 */
#define WM8994_AIF1ADC1_VU                      0x0100  /* AIF1ADC1_VU */
#define WM8994_AIF1ADC1_VU_MASK                 0x0100  /* AIF1ADC1_VU */
#define WM8994_AIF1ADC1_VU_SHIFT                     8  /* AIF1ADC1_VU */
#define WM8994_AIF1ADC1_VU_WIDTH                     1  /* AIF1ADC1_VU */
#define WM8994_AIF1ADC1L_VOL_MASK               0x00FF  /* AIF1ADC1L_VOL - [7:0] */
#define WM8994_AIF1ADC1L_VOL_SHIFT                   0  /* AIF1ADC1L_VOL - [7:0] */
#define WM8994_AIF1ADC1L_VOL_WIDTH                   8  /* AIF1ADC1L_VOL - [7:0] */

/*
 * R1025 (0x401) - AIF1 ADC1 Right Volume
 */
#define WM8994_AIF1ADC1_VU                      0x0100  /* AIF1ADC1_VU */
#define WM8994_AIF1ADC1_VU_MASK                 0x0100  /* AIF1ADC1_VU */
#define WM8994_AIF1ADC1_VU_SHIFT                     8  /* AIF1ADC1_VU */
#define WM8994_AIF1ADC1_VU_WIDTH                     1  /* AIF1ADC1_VU */
#define WM8994_AIF1ADC1R_VOL_MASK               0x00FF  /* AIF1ADC1R_VOL - [7:0] */
#define WM8994_AIF1ADC1R_VOL_SHIFT                   0  /* AIF1ADC1R_VOL - [7:0] */
#define WM8994_AIF1ADC1R_VOL_WIDTH                   8  /* AIF1ADC1R_VOL - [7:0] */

/*
 * R1026 (0x402) - AIF1 DAC1 Left Volume
 */
#define WM8994_AIF1DAC1_VU                      0x0100  /* AIF1DAC1_VU */
#define WM8994_AIF1DAC1_VU_MASK                 0x0100  /* AIF1DAC1_VU */
#define WM8994_AIF1DAC1_VU_SHIFT                     8  /* AIF1DAC1_VU */
#define WM8994_AIF1DAC1_VU_WIDTH                     1  /* AIF1DAC1_VU */
#define WM8994_AIF1DAC1L_VOL_MASK               0x00FF  /* AIF1DAC1L_VOL - [7:0] */
#define WM8994_AIF1DAC1L_VOL_SHIFT                   0  /* AIF1DAC1L_VOL - [7:0] */
#define WM8994_AIF1DAC1L_VOL_WIDTH                   8  /* AIF1DAC1L_VOL - [7:0] */

/*
 * R1027 (0x403) - AIF1 DAC1 Right Volume
 */
#define WM8994_AIF1DAC1_VU                      0x0100  /* AIF1DAC1_VU */
#define WM8994_AIF1DAC1_VU_MASK                 0x0100  /* AIF1DAC1_VU */
#define WM8994_AIF1DAC1_VU_SHIFT                     8  /* AIF1DAC1_VU */
#define WM8994_AIF1DAC1_VU_WIDTH                     1  /* AIF1DAC1_VU */
#define WM8994_AIF1DAC1R_VOL_MASK               0x00FF  /* AIF1DAC1R_VOL - [7:0] */
#define WM8994_AIF1DAC1R_VOL_SHIFT                   0  /* AIF1DAC1R_VOL - [7:0] */
#define WM8994_AIF1DAC1R_VOL_WIDTH                   8  /* AIF1DAC1R_VOL - [7:0] */

/*
 * R1028 (0x404) - AIF1 ADC2 Left Volume
 */
#define WM8994_AIF1ADC2_VU                      0x0100  /* AIF1ADC2_VU */
#define WM8994_AIF1ADC2_VU_MASK                 0x0100  /* AIF1ADC2_VU */
#define WM8994_AIF1ADC2_VU_SHIFT                     8  /* AIF1ADC2_VU */
#define WM8994_AIF1ADC2_VU_WIDTH                     1  /* AIF1ADC2_VU */
#define WM8994_AIF1ADC2L_VOL_MASK               0x00FF  /* AIF1ADC2L_VOL - [7:0] */
#define WM8994_AIF1ADC2L_VOL_SHIFT                   0  /* AIF1ADC2L_VOL - [7:0] */
#define WM8994_AIF1ADC2L_VOL_WIDTH                   8  /* AIF1ADC2L_VOL - [7:0] */

/*
 * R1029 (0x405) - AIF1 ADC2 Right Volume
 */
#define WM8994_AIF1ADC2_VU                      0x0100  /* AIF1ADC2_VU */
#define WM8994_AIF1ADC2_VU_MASK                 0x0100  /* AIF1ADC2_VU */
#define WM8994_AIF1ADC2_VU_SHIFT                     8  /* AIF1ADC2_VU */
#define WM8994_AIF1ADC2_VU_WIDTH                     1  /* AIF1ADC2_VU */
#define WM8994_AIF1ADC2R_VOL_MASK               0x00FF  /* AIF1ADC2R_VOL - [7:0] */
#define WM8994_AIF1ADC2R_VOL_SHIFT                   0  /* AIF1ADC2R_VOL - [7:0] */
#define WM8994_AIF1ADC2R_VOL_WIDTH                   8  /* AIF1ADC2R_VOL - [7:0] */

/*
 * R1030 (0x406) - AIF1 DAC2 Left Volume
 */
#define WM8994_AIF1DAC2_VU                      0x0100  /* AIF1DAC2_VU */
#define WM8994_AIF1DAC2_VU_MASK                 0x0100  /* AIF1DAC2_VU */
#define WM8994_AIF1DAC2_VU_SHIFT                     8  /* AIF1DAC2_VU */
#define WM8994_AIF1DAC2_VU_WIDTH                     1  /* AIF1DAC2_VU */
#define WM8994_AIF1DAC2L_VOL_MASK               0x00FF  /* AIF1DAC2L_VOL - [7:0] */
#define WM8994_AIF1DAC2L_VOL_SHIFT                   0  /* AIF1DAC2L_VOL - [7:0] */
#define WM8994_AIF1DAC2L_VOL_WIDTH                   8  /* AIF1DAC2L_VOL - [7:0] */

/*
 * R1031 (0x407) - AIF1 DAC2 Right Volume
 */
#define WM8994_AIF1DAC2_VU                      0x0100  /* AIF1DAC2_VU */
#define WM8994_AIF1DAC2_VU_MASK                 0x0100  /* AIF1DAC2_VU */
#define WM8994_AIF1DAC2_VU_SHIFT                     8  /* AIF1DAC2_VU */
#define WM8994_AIF1DAC2_VU_WIDTH                     1  /* AIF1DAC2_VU */
#define WM8994_AIF1DAC2R_VOL_MASK               0x00FF  /* AIF1DAC2R_VOL - [7:0] */
#define WM8994_AIF1DAC2R_VOL_SHIFT                   0  /* AIF1DAC2R_VOL - [7:0] */
#define WM8994_AIF1DAC2R_VOL_WIDTH                   8  /* AIF1DAC2R_VOL - [7:0] */

/*
 * R1040 (0x410) - AIF1 ADC1 Filters
 */
#define WM8994_AIF1ADC_4FS                      0x8000  /* AIF1ADC_4FS */
#define WM8994_AIF1ADC_4FS_MASK                 0x8000  /* AIF1ADC_4FS */
#define WM8994_AIF1ADC_4FS_SHIFT                    15  /* AIF1ADC_4FS */
#define WM8994_AIF1ADC_4FS_WIDTH                     1  /* AIF1ADC_4FS */
#define WM8994_AIF1ADC1_HPF_CUT_MASK            0x6000  /* AIF1ADC1_HPF_CUT - [14:13] */
#define WM8994_AIF1ADC1_HPF_CUT_SHIFT               13  /* AIF1ADC1_HPF_CUT - [14:13] */
#define WM8994_AIF1ADC1_HPF_CUT_WIDTH                2  /* AIF1ADC1_HPF_CUT - [14:13] */
#define WM8994_AIF1ADC1L_HPF                    0x1000  /* AIF1ADC1L_HPF */
#define WM8994_AIF1ADC1L_HPF_MASK               0x1000  /* AIF1ADC1L_HPF */
#define WM8994_AIF1ADC1L_HPF_SHIFT                  12  /* AIF1ADC1L_HPF */
#define WM8994_AIF1ADC1L_HPF_WIDTH                   1  /* AIF1ADC1L_HPF */
#define WM8994_AIF1ADC1R_HPF                    0x0800  /* AIF1ADC1R_HPF */
#define WM8994_AIF1ADC1R_HPF_MASK               0x0800  /* AIF1ADC1R_HPF */
#define WM8994_AIF1ADC1R_HPF_SHIFT                  11  /* AIF1ADC1R_HPF */
#define WM8994_AIF1ADC1R_HPF_WIDTH                   1  /* AIF1ADC1R_HPF */

/*
 * R1041 (0x411) - AIF1 ADC2 Filters
 */
#define WM8994_AIF1ADC2_HPF_CUT_MASK            0x6000  /* AIF1ADC2_HPF_CUT - [14:13] */
#define WM8994_AIF1ADC2_HPF_CUT_SHIFT               13  /* AIF1ADC2_HPF_CUT - [14:13] */
#define WM8994_AIF1ADC2_HPF_CUT_WIDTH                2  /* AIF1ADC2_HPF_CUT - [14:13] */
#define WM8994_AIF1ADC2L_HPF                    0x1000  /* AIF1ADC2L_HPF */
#define WM8994_AIF1ADC2L_HPF_MASK               0x1000  /* AIF1ADC2L_HPF */
#define WM8994_AIF1ADC2L_HPF_SHIFT                  12  /* AIF1ADC2L_HPF */
#define WM8994_AIF1ADC2L_HPF_WIDTH                   1  /* AIF1ADC2L_HPF */
#define WM8994_AIF1ADC2R_HPF                    0x0800  /* AIF1ADC2R_HPF */
#define WM8994_AIF1ADC2R_HPF_MASK               0x0800  /* AIF1ADC2R_HPF */
#define WM8994_AIF1ADC2R_HPF_SHIFT                  11  /* AIF1ADC2R_HPF */
#define WM8994_AIF1ADC2R_HPF_WIDTH                   1  /* AIF1ADC2R_HPF */

/*
 * R1056 (0x420) - AIF1 DAC1 Filters (1)
 */
#define WM8994_AIF1DAC1_MUTE                    0x0200  /* AIF1DAC1_MUTE */
#define WM8994_AIF1DAC1_MUTE_MASK               0x0200  /* AIF1DAC1_MUTE */
#define WM8994_AIF1DAC1_MUTE_SHIFT                   9  /* AIF1DAC1_MUTE */
#define WM8994_AIF1DAC1_MUTE_WIDTH                   1  /* AIF1DAC1_MUTE */
#define WM8994_AIF1DAC1_MONO                    0x0080  /* AIF1DAC1_MONO */
#define WM8994_AIF1DAC1_MONO_MASK               0x0080  /* AIF1DAC1_MONO */
#define WM8994_AIF1DAC1_MONO_SHIFT                   7  /* AIF1DAC1_MONO */
#define WM8994_AIF1DAC1_MONO_WIDTH                   1  /* AIF1DAC1_MONO */
#define WM8994_AIF1DAC1_MUTERATE                0x0020  /* AIF1DAC1_MUTERATE */
#define WM8994_AIF1DAC1_MUTERATE_MASK           0x0020  /* AIF1DAC1_MUTERATE */
#define WM8994_AIF1DAC1_MUTERATE_SHIFT               5  /* AIF1DAC1_MUTERATE */
#define WM8994_AIF1DAC1_MUTERATE_WIDTH               1  /* AIF1DAC1_MUTERATE */
#define WM8994_AIF1DAC1_UNMUTE_RAMP             0x0010  /* AIF1DAC1_UNMUTE_RAMP */
#define WM8994_AIF1DAC1_UNMUTE_RAMP_MASK        0x0010  /* AIF1DAC1_UNMUTE_RAMP */
#define WM8994_AIF1DAC1_UNMUTE_RAMP_SHIFT            4  /* AIF1DAC1_UNMUTE_RAMP */
#define WM8994_AIF1DAC1_UNMUTE_RAMP_WIDTH            1  /* AIF1DAC1_UNMUTE_RAMP */
#define WM8994_AIF1DAC1_DEEMP_MASK              0x0006  /* AIF1DAC1_DEEMP - [2:1] */
#define WM8994_AIF1DAC1_DEEMP_SHIFT                  1  /* AIF1DAC1_DEEMP - [2:1] */
#define WM8994_AIF1DAC1_DEEMP_WIDTH                  2  /* AIF1DAC1_DEEMP - [2:1] */

/*
 * R1057 (0x421) - AIF1 DAC1 Filters (2)
 */
#define WM8994_AIF1DAC1_3D_GAIN_MASK            0x3E00  /* AIF1DAC1_3D_GAIN - [13:9] */
#define WM8994_AIF1DAC1_3D_GAIN_SHIFT                9  /* AIF1DAC1_3D_GAIN - [13:9] */
#define WM8994_AIF1DAC1_3D_GAIN_WIDTH                5  /* AIF1DAC1_3D_GAIN - [13:9] */
#define WM8994_AIF1DAC1_3D_ENA                  0x0100  /* AIF1DAC1_3D_ENA */
#define WM8994_AIF1DAC1_3D_ENA_MASK             0x0100  /* AIF1DAC1_3D_ENA */
#define WM8994_AIF1DAC1_3D_ENA_SHIFT                 8  /* AIF1DAC1_3D_ENA */
#define WM8994_AIF1DAC1_3D_ENA_WIDTH                 1  /* AIF1DAC1_3D_ENA */

/*
 * R1058 (0x422) - AIF1 DAC2 Filters (1)
 */
#define WM8994_AIF1DAC2_MUTE                    0x0200  /* AIF1DAC2_MUTE */
#define WM8994_AIF1DAC2_MUTE_MASK               0x0200  /* AIF1DAC2_MUTE */
#define WM8994_AIF1DAC2_MUTE_SHIFT                   9  /* AIF1DAC2_MUTE */
#define WM8994_AIF1DAC2_MUTE_WIDTH                   1  /* AIF1DAC2_MUTE */
#define WM8994_AIF1DAC2_MONO                    0x0080  /* AIF1DAC2_MONO */
#define WM8994_AIF1DAC2_MONO_MASK               0x0080  /* AIF1DAC2_MONO */
#define WM8994_AIF1DAC2_MONO_SHIFT                   7  /* AIF1DAC2_MONO */
#define WM8994_AIF1DAC2_MONO_WIDTH                   1  /* AIF1DAC2_MONO */
#define WM8994_AIF1DAC2_MUTERATE                0x0020  /* AIF1DAC2_MUTERATE */
#define WM8994_AIF1DAC2_MUTERATE_MASK           0x0020  /* AIF1DAC2_MUTERATE */
#define WM8994_AIF1DAC2_MUTERATE_SHIFT               5  /* AIF1DAC2_MUTERATE */
#define WM8994_AIF1DAC2_MUTERATE_WIDTH               1  /* AIF1DAC2_MUTERATE */
#define WM8994_AIF1DAC2_UNMUTE_RAMP             0x0010  /* AIF1DAC2_UNMUTE_RAMP */
#define WM8994_AIF1DAC2_UNMUTE_RAMP_MASK        0x0010  /* AIF1DAC2_UNMUTE_RAMP */
#define WM8994_AIF1DAC2_UNMUTE_RAMP_SHIFT            4  /* AIF1DAC2_UNMUTE_RAMP */
#define WM8994_AIF1DAC2_UNMUTE_RAMP_WIDTH            1  /* AIF1DAC2_UNMUTE_RAMP */
#define WM8994_AIF1DAC2_DEEMP_MASK              0x0006  /* AIF1DAC2_DEEMP - [2:1] */
#define WM8994_AIF1DAC2_DEEMP_SHIFT                  1  /* AIF1DAC2_DEEMP - [2:1] */
#define WM8994_AIF1DAC2_DEEMP_WIDTH                  2  /* AIF1DAC2_DEEMP - [2:1] */

/*
 * R1059 (0x423) - AIF1 DAC2 Filters (2)
 */
#define WM8994_AIF1DAC2_3D_GAIN_MASK            0x3E00  /* AIF1DAC2_3D_GAIN - [13:9] */
#define WM8994_AIF1DAC2_3D_GAIN_SHIFT                9  /* AIF1DAC2_3D_GAIN - [13:9] */
#define WM8994_AIF1DAC2_3D_GAIN_WIDTH                5  /* AIF1DAC2_3D_GAIN - [13:9] */
#define WM8994_AIF1DAC2_3D_ENA                  0x0100  /* AIF1DAC2_3D_ENA */
#define WM8994_AIF1DAC2_3D_ENA_MASK             0x0100  /* AIF1DAC2_3D_ENA */
#define WM8994_AIF1DAC2_3D_ENA_SHIFT                 8  /* AIF1DAC2_3D_ENA */
#define WM8994_AIF1DAC2_3D_ENA_WIDTH                 1  /* AIF1DAC2_3D_ENA */

/*
 * R1088 (0x440) - AIF1 DRC1 (1)
 */
#define WM8994_AIF1DRC1_SIG_DET_RMS_MASK        0xF800  /* AIF1DRC1_SIG_DET_RMS - [15:11] */
#define WM8994_AIF1DRC1_SIG_DET_RMS_SHIFT           11  /* AIF1DRC1_SIG_DET_RMS - [15:11] */
#define WM8994_AIF1DRC1_SIG_DET_RMS_WIDTH            5  /* AIF1DRC1_SIG_DET_RMS - [15:11] */
#define WM8994_AIF1DRC1_SIG_DET_PK_MASK         0x0600  /* AIF1DRC1_SIG_DET_PK - [10:9] */
#define WM8994_AIF1DRC1_SIG_DET_PK_SHIFT             9  /* AIF1DRC1_SIG_DET_PK - [10:9] */
#define WM8994_AIF1DRC1_SIG_DET_PK_WIDTH             2  /* AIF1DRC1_SIG_DET_PK - [10:9] */
#define WM8994_AIF1DRC1_NG_ENA                  0x0100  /* AIF1DRC1_NG_ENA */
#define WM8994_AIF1DRC1_NG_ENA_MASK             0x0100  /* AIF1DRC1_NG_ENA */
#define WM8994_AIF1DRC1_NG_ENA_SHIFT                 8  /* AIF1DRC1_NG_ENA */
#define WM8994_AIF1DRC1_NG_ENA_WIDTH                 1  /* AIF1DRC1_NG_ENA */
#define WM8994_AIF1DRC1_SIG_DET_MODE            0x0080  /* AIF1DRC1_SIG_DET_MODE */
#define WM8994_AIF1DRC1_SIG_DET_MODE_MASK       0x0080  /* AIF1DRC1_SIG_DET_MODE */
#define WM8994_AIF1DRC1_SIG_DET_MODE_SHIFT           7  /* AIF1DRC1_SIG_DET_MODE */
#define WM8994_AIF1DRC1_SIG_DET_MODE_WIDTH           1  /* AIF1DRC1_SIG_DET_MODE */
#define WM8994_AIF1DRC1_SIG_DET                 0x0040  /* AIF1DRC1_SIG_DET */
#define WM8994_AIF1DRC1_SIG_DET_MASK            0x0040  /* AIF1DRC1_SIG_DET */
#define WM8994_AIF1DRC1_SIG_DET_SHIFT                6  /* AIF1DRC1_SIG_DET */
#define WM8994_AIF1DRC1_SIG_DET_WIDTH                1  /* AIF1DRC1_SIG_DET */
#define WM8994_AIF1DRC1_KNEE2_OP_ENA            0x0020  /* AIF1DRC1_KNEE2_OP_ENA */
#define WM8994_AIF1DRC1_KNEE2_OP_ENA_MASK       0x0020  /* AIF1DRC1_KNEE2_OP_ENA */
#define WM8994_AIF1DRC1_KNEE2_OP_ENA_SHIFT           5  /* AIF1DRC1_KNEE2_OP_ENA */
#define WM8994_AIF1DRC1_KNEE2_OP_ENA_WIDTH           1  /* AIF1DRC1_KNEE2_OP_ENA */
#define WM8994_AIF1DRC1_QR                      0x0010  /* AIF1DRC1_QR */
#define WM8994_AIF1DRC1_QR_MASK                 0x0010  /* AIF1DRC1_QR */
#define WM8994_AIF1DRC1_QR_SHIFT                     4  /* AIF1DRC1_QR */
#define WM8994_AIF1DRC1_QR_WIDTH                     1  /* AIF1DRC1_QR */
#define WM8994_AIF1DRC1_ANTICLIP                0x0008  /* AIF1DRC1_ANTICLIP */
#define WM8994_AIF1DRC1_ANTICLIP_MASK           0x0008  /* AIF1DRC1_ANTICLIP */
#define WM8994_AIF1DRC1_ANTICLIP_SHIFT               3  /* AIF1DRC1_ANTICLIP */
#define WM8994_AIF1DRC1_ANTICLIP_WIDTH               1  /* AIF1DRC1_ANTICLIP */
#define WM8994_AIF1DAC1_DRC_ENA                 0x0004  /* AIF1DAC1_DRC_ENA */
#define WM8994_AIF1DAC1_DRC_ENA_MASK            0x0004  /* AIF1DAC1_DRC_ENA */
#define WM8994_AIF1DAC1_DRC_ENA_SHIFT                2  /* AIF1DAC1_DRC_ENA */
#define WM8994_AIF1DAC1_DRC_ENA_WIDTH                1  /* AIF1DAC1_DRC_ENA */
#define WM8994_AIF1ADC1L_DRC_ENA                0x0002  /* AIF1ADC1L_DRC_ENA */
#define WM8994_AIF1ADC1L_DRC_ENA_MASK           0x0002  /* AIF1ADC1L_DRC_ENA */
#define WM8994_AIF1ADC1L_DRC_ENA_SHIFT               1  /* AIF1ADC1L_DRC_ENA */
#define WM8994_AIF1ADC1L_DRC_ENA_WIDTH               1  /* AIF1ADC1L_DRC_ENA */
#define WM8994_AIF1ADC1R_DRC_ENA                0x0001  /* AIF1ADC1R_DRC_ENA */
#define WM8994_AIF1ADC1R_DRC_ENA_MASK           0x0001  /* AIF1ADC1R_DRC_ENA */
#define WM8994_AIF1ADC1R_DRC_ENA_SHIFT               0  /* AIF1ADC1R_DRC_ENA */
#define WM8994_AIF1ADC1R_DRC_ENA_WIDTH               1  /* AIF1ADC1R_DRC_ENA */

/*
 * R1089 (0x441) - AIF1 DRC1 (2)
 */
#define WM8994_AIF1DRC1_ATK_MASK                0x1E00  /* AIF1DRC1_ATK - [12:9] */
#define WM8994_AIF1DRC1_ATK_SHIFT                    9  /* AIF1DRC1_ATK - [12:9] */
#define WM8994_AIF1DRC1_ATK_WIDTH                    4  /* AIF1DRC1_ATK - [12:9] */
#define WM8994_AIF1DRC1_DCY_MASK                0x01E0  /* AIF1DRC1_DCY - [8:5] */
#define WM8994_AIF1DRC1_DCY_SHIFT                    5  /* AIF1DRC1_DCY - [8:5] */
#define WM8994_AIF1DRC1_DCY_WIDTH                    4  /* AIF1DRC1_DCY - [8:5] */
#define WM8994_AIF1DRC1_MINGAIN_MASK            0x001C  /* AIF1DRC1_MINGAIN - [4:2] */
#define WM8994_AIF1DRC1_MINGAIN_SHIFT                2  /* AIF1DRC1_MINGAIN - [4:2] */
#define WM8994_AIF1DRC1_MINGAIN_WIDTH                3  /* AIF1DRC1_MINGAIN - [4:2] */
#define WM8994_AIF1DRC1_MAXGAIN_MASK            0x0003  /* AIF1DRC1_MAXGAIN - [1:0] */
#define WM8994_AIF1DRC1_MAXGAIN_SHIFT                0  /* AIF1DRC1_MAXGAIN - [1:0] */
#define WM8994_AIF1DRC1_MAXGAIN_WIDTH                2  /* AIF1DRC1_MAXGAIN - [1:0] */

/*
 * R1090 (0x442) - AIF1 DRC1 (3)
 */
#define WM8994_AIF1DRC1_NG_MINGAIN_MASK         0xF000  /* AIF1DRC1_NG_MINGAIN - [15:12] */
#define WM8994_AIF1DRC1_NG_MINGAIN_SHIFT            12  /* AIF1DRC1_NG_MINGAIN - [15:12] */
#define WM8994_AIF1DRC1_NG_MINGAIN_WIDTH             4  /* AIF1DRC1_NG_MINGAIN - [15:12] */
#define WM8994_AIF1DRC1_NG_EXP_MASK             0x0C00  /* AIF1DRC1_NG_EXP - [11:10] */
#define WM8994_AIF1DRC1_NG_EXP_SHIFT                10  /* AIF1DRC1_NG_EXP - [11:10] */
#define WM8994_AIF1DRC1_NG_EXP_WIDTH                 2  /* AIF1DRC1_NG_EXP - [11:10] */
#define WM8994_AIF1DRC1_QR_THR_MASK             0x0300  /* AIF1DRC1_QR_THR - [9:8] */
#define WM8994_AIF1DRC1_QR_THR_SHIFT                 8  /* AIF1DRC1_QR_THR - [9:8] */
#define WM8994_AIF1DRC1_QR_THR_WIDTH                 2  /* AIF1DRC1_QR_THR - [9:8] */
#define WM8994_AIF1DRC1_QR_DCY_MASK             0x00C0  /* AIF1DRC1_QR_DCY - [7:6] */
#define WM8994_AIF1DRC1_QR_DCY_SHIFT                 6  /* AIF1DRC1_QR_DCY - [7:6] */
#define WM8994_AIF1DRC1_QR_DCY_WIDTH                 2  /* AIF1DRC1_QR_DCY - [7:6] */
#define WM8994_AIF1DRC1_HI_COMP_MASK            0x0038  /* AIF1DRC1_HI_COMP - [5:3] */
#define WM8994_AIF1DRC1_HI_COMP_SHIFT                3  /* AIF1DRC1_HI_COMP - [5:3] */
#define WM8994_AIF1DRC1_HI_COMP_WIDTH                3  /* AIF1DRC1_HI_COMP - [5:3] */
#define WM8994_AIF1DRC1_LO_COMP_MASK            0x0007  /* AIF1DRC1_LO_COMP - [2:0] */
#define WM8994_AIF1DRC1_LO_COMP_SHIFT                0  /* AIF1DRC1_LO_COMP - [2:0] */
#define WM8994_AIF1DRC1_LO_COMP_WIDTH                3  /* AIF1DRC1_LO_COMP - [2:0] */

/*
 * R1091 (0x443) - AIF1 DRC1 (4)
 */
#define WM8994_AIF1DRC1_KNEE_IP_MASK            0x07E0  /* AIF1DRC1_KNEE_IP - [10:5] */
#define WM8994_AIF1DRC1_KNEE_IP_SHIFT                5  /* AIF1DRC1_KNEE_IP - [10:5] */
#define WM8994_AIF1DRC1_KNEE_IP_WIDTH                6  /* AIF1DRC1_KNEE_IP - [10:5] */
#define WM8994_AIF1DRC1_KNEE_OP_MASK            0x001F  /* AIF1DRC1_KNEE_OP - [4:0] */
#define WM8994_AIF1DRC1_KNEE_OP_SHIFT                0  /* AIF1DRC1_KNEE_OP - [4:0] */
#define WM8994_AIF1DRC1_KNEE_OP_WIDTH                5  /* AIF1DRC1_KNEE_OP - [4:0] */

/*
 * R1092 (0x444) - AIF1 DRC1 (5)
 */
#define WM8994_AIF1DRC1_KNEE2_IP_MASK           0x03E0  /* AIF1DRC1_KNEE2_IP - [9:5] */
#define WM8994_AIF1DRC1_KNEE2_IP_SHIFT               5  /* AIF1DRC1_KNEE2_IP - [9:5] */
#define WM8994_AIF1DRC1_KNEE2_IP_WIDTH               5  /* AIF1DRC1_KNEE2_IP - [9:5] */
#define WM8994_AIF1DRC1_KNEE2_OP_MASK           0x001F  /* AIF1DRC1_KNEE2_OP - [4:0] */
#define WM8994_AIF1DRC1_KNEE2_OP_SHIFT               0  /* AIF1DRC1_KNEE2_OP - [4:0] */
#define WM8994_AIF1DRC1_KNEE2_OP_WIDTH               5  /* AIF1DRC1_KNEE2_OP - [4:0] */

/*
 * R1104 (0x450) - AIF1 DRC2 (1)
 */
#define WM8994_AIF1DRC2_SIG_DET_RMS_MASK        0xF800  /* AIF1DRC2_SIG_DET_RMS - [15:11] */
#define WM8994_AIF1DRC2_SIG_DET_RMS_SHIFT           11  /* AIF1DRC2_SIG_DET_RMS - [15:11] */
#define WM8994_AIF1DRC2_SIG_DET_RMS_WIDTH            5  /* AIF1DRC2_SIG_DET_RMS - [15:11] */
#define WM8994_AIF1DRC2_SIG_DET_PK_MASK         0x0600  /* AIF1DRC2_SIG_DET_PK - [10:9] */
#define WM8994_AIF1DRC2_SIG_DET_PK_SHIFT             9  /* AIF1DRC2_SIG_DET_PK - [10:9] */
#define WM8994_AIF1DRC2_SIG_DET_PK_WIDTH             2  /* AIF1DRC2_SIG_DET_PK - [10:9] */
#define WM8994_AIF1DRC2_NG_ENA                  0x0100  /* AIF1DRC2_NG_ENA */
#define WM8994_AIF1DRC2_NG_ENA_MASK             0x0100  /* AIF1DRC2_NG_ENA */
#define WM8994_AIF1DRC2_NG_ENA_SHIFT                 8  /* AIF1DRC2_NG_ENA */
#define WM8994_AIF1DRC2_NG_ENA_WIDTH                 1  /* AIF1DRC2_NG_ENA */
#define WM8994_AIF1DRC2_SIG_DET_MODE            0x0080  /* AIF1DRC2_SIG_DET_MODE */
#define WM8994_AIF1DRC2_SIG_DET_MODE_MASK       0x0080  /* AIF1DRC2_SIG_DET_MODE */
#define WM8994_AIF1DRC2_SIG_DET_MODE_SHIFT           7  /* AIF1DRC2_SIG_DET_MODE */
#define WM8994_AIF1DRC2_SIG_DET_MODE_WIDTH           1  /* AIF1DRC2_SIG_DET_MODE */
#define WM8994_AIF1DRC2_SIG_DET                 0x0040  /* AIF1DRC2_SIG_DET */
#define WM8994_AIF1DRC2_SIG_DET_MASK            0x0040  /* AIF1DRC2_SIG_DET */
#define WM8994_AIF1DRC2_SIG_DET_SHIFT                6  /* AIF1DRC2_SIG_DET */
#define WM8994_AIF1DRC2_SIG_DET_WIDTH                1  /* AIF1DRC2_SIG_DET */
#define WM8994_AIF1DRC2_KNEE2_OP_ENA            0x0020  /* AIF1DRC2_KNEE2_OP_ENA */
#define WM8994_AIF1DRC2_KNEE2_OP_ENA_MASK       0x0020  /* AIF1DRC2_KNEE2_OP_ENA */
#define WM8994_AIF1DRC2_KNEE2_OP_ENA_SHIFT           5  /* AIF1DRC2_KNEE2_OP_ENA */
#define WM8994_AIF1DRC2_KNEE2_OP_ENA_WIDTH           1  /* AIF1DRC2_KNEE2_OP_ENA */
#define WM8994_AIF1DRC2_QR                      0x0010  /* AIF1DRC2_QR */
#define WM8994_AIF1DRC2_QR_MASK                 0x0010  /* AIF1DRC2_QR */
#define WM8994_AIF1DRC2_QR_SHIFT                     4  /* AIF1DRC2_QR */
#define WM8994_AIF1DRC2_QR_WIDTH                     1  /* AIF1DRC2_QR */
#define WM8994_AIF1DRC2_ANTICLIP                0x0008  /* AIF1DRC2_ANTICLIP */
#define WM8994_AIF1DRC2_ANTICLIP_MASK           0x0008  /* AIF1DRC2_ANTICLIP */
#define WM8994_AIF1DRC2_ANTICLIP_SHIFT               3  /* AIF1DRC2_ANTICLIP */
#define WM8994_AIF1DRC2_ANTICLIP_WIDTH               1  /* AIF1DRC2_ANTICLIP */
#define WM8994_AIF1DAC2_DRC_ENA                 0x0004  /* AIF1DAC2_DRC_ENA */
#define WM8994_AIF1DAC2_DRC_ENA_MASK            0x0004  /* AIF1DAC2_DRC_ENA */
#define WM8994_AIF1DAC2_DRC_ENA_SHIFT                2  /* AIF1DAC2_DRC_ENA */
#define WM8994_AIF1DAC2_DRC_ENA_WIDTH                1  /* AIF1DAC2_DRC_ENA */
#define WM8994_AIF1ADC2L_DRC_ENA                0x0002  /* AIF1ADC2L_DRC_ENA */
#define WM8994_AIF1ADC2L_DRC_ENA_MASK           0x0002  /* AIF1ADC2L_DRC_ENA */
#define WM8994_AIF1ADC2L_DRC_ENA_SHIFT               1  /* AIF1ADC2L_DRC_ENA */
#define WM8994_AIF1ADC2L_DRC_ENA_WIDTH               1  /* AIF1ADC2L_DRC_ENA */
#define WM8994_AIF1ADC2R_DRC_ENA                0x0001  /* AIF1ADC2R_DRC_ENA */
#define WM8994_AIF1ADC2R_DRC_ENA_MASK           0x0001  /* AIF1ADC2R_DRC_ENA */
#define WM8994_AIF1ADC2R_DRC_ENA_SHIFT               0  /* AIF1ADC2R_DRC_ENA */
#define WM8994_AIF1ADC2R_DRC_ENA_WIDTH               1  /* AIF1ADC2R_DRC_ENA */

/*
 * R1105 (0x451) - AIF1 DRC2 (2)
 */
#define WM8994_AIF1DRC2_ATK_MASK                0x1E00  /* AIF1DRC2_ATK - [12:9] */
#define WM8994_AIF1DRC2_ATK_SHIFT                    9  /* AIF1DRC2_ATK - [12:9] */
#define WM8994_AIF1DRC2_ATK_WIDTH                    4  /* AIF1DRC2_ATK - [12:9] */
#define WM8994_AIF1DRC2_DCY_MASK                0x01E0  /* AIF1DRC2_DCY - [8:5] */
#define WM8994_AIF1DRC2_DCY_SHIFT                    5  /* AIF1DRC2_DCY - [8:5] */
#define WM8994_AIF1DRC2_DCY_WIDTH                    4  /* AIF1DRC2_DCY - [8:5] */
#define WM8994_AIF1DRC2_MINGAIN_MASK            0x001C  /* AIF1DRC2_MINGAIN - [4:2] */
#define WM8994_AIF1DRC2_MINGAIN_SHIFT                2  /* AIF1DRC2_MINGAIN - [4:2] */
#define WM8994_AIF1DRC2_MINGAIN_WIDTH                3  /* AIF1DRC2_MINGAIN - [4:2] */
#define WM8994_AIF1DRC2_MAXGAIN_MASK            0x0003  /* AIF1DRC2_MAXGAIN - [1:0] */
#define WM8994_AIF1DRC2_MAXGAIN_SHIFT                0  /* AIF1DRC2_MAXGAIN - [1:0] */
#define WM8994_AIF1DRC2_MAXGAIN_WIDTH                2  /* AIF1DRC2_MAXGAIN - [1:0] */

/*
 * R1106 (0x452) - AIF1 DRC2 (3)
 */
#define WM8994_AIF1DRC2_NG_MINGAIN_MASK         0xF000  /* AIF1DRC2_NG_MINGAIN - [15:12] */
#define WM8994_AIF1DRC2_NG_MINGAIN_SHIFT            12  /* AIF1DRC2_NG_MINGAIN - [15:12] */
#define WM8994_AIF1DRC2_NG_MINGAIN_WIDTH             4  /* AIF1DRC2_NG_MINGAIN - [15:12] */
#define WM8994_AIF1DRC2_NG_EXP_MASK             0x0C00  /* AIF1DRC2_NG_EXP - [11:10] */
#define WM8994_AIF1DRC2_NG_EXP_SHIFT                10  /* AIF1DRC2_NG_EXP - [11:10] */
#define WM8994_AIF1DRC2_NG_EXP_WIDTH                 2  /* AIF1DRC2_NG_EXP - [11:10] */
#define WM8994_AIF1DRC2_QR_THR_MASK             0x0300  /* AIF1DRC2_QR_THR - [9:8] */
#define WM8994_AIF1DRC2_QR_THR_SHIFT                 8  /* AIF1DRC2_QR_THR - [9:8] */
#define WM8994_AIF1DRC2_QR_THR_WIDTH                 2  /* AIF1DRC2_QR_THR - [9:8] */
#define WM8994_AIF1DRC2_QR_DCY_MASK             0x00C0  /* AIF1DRC2_QR_DCY - [7:6] */
#define WM8994_AIF1DRC2_QR_DCY_SHIFT                 6  /* AIF1DRC2_QR_DCY - [7:6] */
#define WM8994_AIF1DRC2_QR_DCY_WIDTH                 2  /* AIF1DRC2_QR_DCY - [7:6] */
#define WM8994_AIF1DRC2_HI_COMP_MASK            0x0038  /* AIF1DRC2_HI_COMP - [5:3] */
#define WM8994_AIF1DRC2_HI_COMP_SHIFT                3  /* AIF1DRC2_HI_COMP - [5:3] */
#define WM8994_AIF1DRC2_HI_COMP_WIDTH                3  /* AIF1DRC2_HI_COMP - [5:3] */
#define WM8994_AIF1DRC2_LO_COMP_MASK            0x0007  /* AIF1DRC2_LO_COMP - [2:0] */
#define WM8994_AIF1DRC2_LO_COMP_SHIFT                0  /* AIF1DRC2_LO_COMP - [2:0] */
#define WM8994_AIF1DRC2_LO_COMP_WIDTH                3  /* AIF1DRC2_LO_COMP - [2:0] */

/*
 * R1107 (0x453) - AIF1 DRC2 (4)
 */
#define WM8994_AIF1DRC2_KNEE_IP_MASK            0x07E0  /* AIF1DRC2_KNEE_IP - [10:5] */
#define WM8994_AIF1DRC2_KNEE_IP_SHIFT                5  /* AIF1DRC2_KNEE_IP - [10:5] */
#define WM8994_AIF1DRC2_KNEE_IP_WIDTH                6  /* AIF1DRC2_KNEE_IP - [10:5] */
#define WM8994_AIF1DRC2_KNEE_OP_MASK            0x001F  /* AIF1DRC2_KNEE_OP - [4:0] */
#define WM8994_AIF1DRC2_KNEE_OP_SHIFT                0  /* AIF1DRC2_KNEE_OP - [4:0] */
#define WM8994_AIF1DRC2_KNEE_OP_WIDTH                5  /* AIF1DRC2_KNEE_OP - [4:0] */

/*
 * R1108 (0x454) - AIF1 DRC2 (5)
 */
#define WM8994_AIF1DRC2_KNEE2_IP_MASK           0x03E0  /* AIF1DRC2_KNEE2_IP - [9:5] */
#define WM8994_AIF1DRC2_KNEE2_IP_SHIFT               5  /* AIF1DRC2_KNEE2_IP - [9:5] */
#define WM8994_AIF1DRC2_KNEE2_IP_WIDTH               5  /* AIF1DRC2_KNEE2_IP - [9:5] */
#define WM8994_AIF1DRC2_KNEE2_OP_MASK           0x001F  /* AIF1DRC2_KNEE2_OP - [4:0] */
#define WM8994_AIF1DRC2_KNEE2_OP_SHIFT               0  /* AIF1DRC2_KNEE2_OP - [4:0] */
#define WM8994_AIF1DRC2_KNEE2_OP_WIDTH               5  /* AIF1DRC2_KNEE2_OP - [4:0] */

/*
 * R1152 (0x480) - AIF1 DAC1 EQ Gains (1)
 */
#define WM8994_AIF1DAC1_EQ_B1_GAIN_MASK         0xF800  /* AIF1DAC1_EQ_B1_GAIN - [15:11] */
#define WM8994_AIF1DAC1_EQ_B1_GAIN_SHIFT            11  /* AIF1DAC1_EQ_B1_GAIN - [15:11] */
#define WM8994_AIF1DAC1_EQ_B1_GAIN_WIDTH             5  /* AIF1DAC1_EQ_B1_GAIN - [15:11] */
#define WM8994_AIF1DAC1_EQ_B2_GAIN_MASK         0x07C0  /* AIF1DAC1_EQ_B2_GAIN - [10:6] */
#define WM8994_AIF1DAC1_EQ_B2_GAIN_SHIFT             6  /* AIF1DAC1_EQ_B2_GAIN - [10:6] */
#define WM8994_AIF1DAC1_EQ_B2_GAIN_WIDTH             5  /* AIF1DAC1_EQ_B2_GAIN - [10:6] */
#define WM8994_AIF1DAC1_EQ_B3_GAIN_MASK         0x003E  /* AIF1DAC1_EQ_B3_GAIN - [5:1] */
#define WM8994_AIF1DAC1_EQ_B3_GAIN_SHIFT             1  /* AIF1DAC1_EQ_B3_GAIN - [5:1] */
#define WM8994_AIF1DAC1_EQ_B3_GAIN_WIDTH             5  /* AIF1DAC1_EQ_B3_GAIN - [5:1] */
#define WM8994_AIF1DAC1_EQ_ENA                  0x0001  /* AIF1DAC1_EQ_ENA */
#define WM8994_AIF1DAC1_EQ_ENA_MASK             0x0001  /* AIF1DAC1_EQ_ENA */
#define WM8994_AIF1DAC1_EQ_ENA_SHIFT                 0  /* AIF1DAC1_EQ_ENA */
#define WM8994_AIF1DAC1_EQ_ENA_WIDTH                 1  /* AIF1DAC1_EQ_ENA */

/*
 * R1153 (0x481) - AIF1 DAC1 EQ Gains (2)
 */
#define WM8994_AIF1DAC1_EQ_B4_GAIN_MASK         0xF800  /* AIF1DAC1_EQ_B4_GAIN - [15:11] */
#define WM8994_AIF1DAC1_EQ_B4_GAIN_SHIFT            11  /* AIF1DAC1_EQ_B4_GAIN - [15:11] */
#define WM8994_AIF1DAC1_EQ_B4_GAIN_WIDTH             5  /* AIF1DAC1_EQ_B4_GAIN - [15:11] */
#define WM8994_AIF1DAC1_EQ_B5_GAIN_MASK         0x07C0  /* AIF1DAC1_EQ_B5_GAIN - [10:6] */
#define WM8994_AIF1DAC1_EQ_B5_GAIN_SHIFT             6  /* AIF1DAC1_EQ_B5_GAIN - [10:6] */
#define WM8994_AIF1DAC1_EQ_B5_GAIN_WIDTH             5  /* AIF1DAC1_EQ_B5_GAIN - [10:6] */

/*
 * R1154 (0x482) - AIF1 DAC1 EQ Band 1 A
 */
#define WM8994_AIF1DAC1_EQ_B1_A_MASK            0xFFFF  /* AIF1DAC1_EQ_B1_A - [15:0] */
#define WM8994_AIF1DAC1_EQ_B1_A_SHIFT                0  /* AIF1DAC1_EQ_B1_A - [15:0] */
#define WM8994_AIF1DAC1_EQ_B1_A_WIDTH               16  /* AIF1DAC1_EQ_B1_A - [15:0] */

/*
 * R1155 (0x483) - AIF1 DAC1 EQ Band 1 B
 */
#define WM8994_AIF1DAC1_EQ_B1_B_MASK            0xFFFF  /* AIF1DAC1_EQ_B1_B - [15:0] */
#define WM8994_AIF1DAC1_EQ_B1_B_SHIFT                0  /* AIF1DAC1_EQ_B1_B - [15:0] */
#define WM8994_AIF1DAC1_EQ_B1_B_WIDTH               16  /* AIF1DAC1_EQ_B1_B - [15:0] */

/*
 * R1156 (0x484) - AIF1 DAC1 EQ Band 1 PG
 */
#define WM8994_AIF1DAC1_EQ_B1_PG_MASK           0xFFFF  /* AIF1DAC1_EQ_B1_PG - [15:0] */
#define WM8994_AIF1DAC1_EQ_B1_PG_SHIFT               0  /* AIF1DAC1_EQ_B1_PG - [15:0] */
#define WM8994_AIF1DAC1_EQ_B1_PG_WIDTH              16  /* AIF1DAC1_EQ_B1_PG - [15:0] */

/*
 * R1157 (0x485) - AIF1 DAC1 EQ Band 2 A
 */
#define WM8994_AIF1DAC1_EQ_B2_A_MASK            0xFFFF  /* AIF1DAC1_EQ_B2_A - [15:0] */
#define WM8994_AIF1DAC1_EQ_B2_A_SHIFT                0  /* AIF1DAC1_EQ_B2_A - [15:0] */
#define WM8994_AIF1DAC1_EQ_B2_A_WIDTH               16  /* AIF1DAC1_EQ_B2_A - [15:0] */

/*
 * R1158 (0x486) - AIF1 DAC1 EQ Band 2 B
 */
#define WM8994_AIF1DAC1_EQ_B2_B_MASK            0xFFFF  /* AIF1DAC1_EQ_B2_B - [15:0] */
#define WM8994_AIF1DAC1_EQ_B2_B_SHIFT                0  /* AIF1DAC1_EQ_B2_B - [15:0] */
#define WM8994_AIF1DAC1_EQ_B2_B_WIDTH               16  /* AIF1DAC1_EQ_B2_B - [15:0] */

/*
 * R1159 (0x487) - AIF1 DAC1 EQ Band 2 C
 */
#define WM8994_AIF1DAC1_EQ_B2_C_MASK            0xFFFF  /* AIF1DAC1_EQ_B2_C - [15:0] */
#define WM8994_AIF1DAC1_EQ_B2_C_SHIFT                0  /* AIF1DAC1_EQ_B2_C - [15:0] */
#define WM8994_AIF1DAC1_EQ_B2_C_WIDTH               16  /* AIF1DAC1_EQ_B2_C - [15:0] */

/*
 * R1160 (0x488) - AIF1 DAC1 EQ Band 2 PG
 */
#define WM8994_AIF1DAC1_EQ_B2_PG_MASK           0xFFFF  /* AIF1DAC1_EQ_B2_PG - [15:0] */
#define WM8994_AIF1DAC1_EQ_B2_PG_SHIFT               0  /* AIF1DAC1_EQ_B2_PG - [15:0] */
#define WM8994_AIF1DAC1_EQ_B2_PG_WIDTH              16  /* AIF1DAC1_EQ_B2_PG - [15:0] */

/*
 * R1161 (0x489) - AIF1 DAC1 EQ Band 3 A
 */
#define WM8994_AIF1DAC1_EQ_B3_A_MASK            0xFFFF  /* AIF1DAC1_EQ_B3_A - [15:0] */
#define WM8994_AIF1DAC1_EQ_B3_A_SHIFT                0  /* AIF1DAC1_EQ_B3_A - [15:0] */
#define WM8994_AIF1DAC1_EQ_B3_A_WIDTH               16  /* AIF1DAC1_EQ_B3_A - [15:0] */

/*
 * R1162 (0x48A) - AIF1 DAC1 EQ Band 3 B
 */
#define WM8994_AIF1DAC1_EQ_B3_B_MASK            0xFFFF  /* AIF1DAC1_EQ_B3_B - [15:0] */
#define WM8994_AIF1DAC1_EQ_B3_B_SHIFT                0  /* AIF1DAC1_EQ_B3_B - [15:0] */
#define WM8994_AIF1DAC1_EQ_B3_B_WIDTH               16  /* AIF1DAC1_EQ_B3_B - [15:0] */

/*
 * R1163 (0x48B) - AIF1 DAC1 EQ Band 3 C
 */
#define WM8994_AIF1DAC1_EQ_B3_C_MASK            0xFFFF  /* AIF1DAC1_EQ_B3_C - [15:0] */
#define WM8994_AIF1DAC1_EQ_B3_C_SHIFT                0  /* AIF1DAC1_EQ_B3_C - [15:0] */
#define WM8994_AIF1DAC1_EQ_B3_C_WIDTH               16  /* AIF1DAC1_EQ_B3_C - [15:0] */

/*
 * R1164 (0x48C) - AIF1 DAC1 EQ Band 3 PG
 */
#define WM8994_AIF1DAC1_EQ_B3_PG_MASK           0xFFFF  /* AIF1DAC1_EQ_B3_PG - [15:0] */
#define WM8994_AIF1DAC1_EQ_B3_PG_SHIFT               0  /* AIF1DAC1_EQ_B3_PG - [15:0] */
#define WM8994_AIF1DAC1_EQ_B3_PG_WIDTH              16  /* AIF1DAC1_EQ_B3_PG - [15:0] */

/*
 * R1165 (0x48D) - AIF1 DAC1 EQ Band 4 A
 */
#define WM8994_AIF1DAC1_EQ_B4_A_MASK            0xFFFF  /* AIF1DAC1_EQ_B4_A - [15:0] */
#define WM8994_AIF1DAC1_EQ_B4_A_SHIFT                0  /* AIF1DAC1_EQ_B4_A - [15:0] */
#define WM8994_AIF1DAC1_EQ_B4_A_WIDTH               16  /* AIF1DAC1_EQ_B4_A - [15:0] */

/*
 * R1166 (0x48E) - AIF1 DAC1 EQ Band 4 B
 */
#define WM8994_AIF1DAC1_EQ_B4_B_MASK            0xFFFF  /* AIF1DAC1_EQ_B4_B - [15:0] */
#define WM8994_AIF1DAC1_EQ_B4_B_SHIFT                0  /* AIF1DAC1_EQ_B4_B - [15:0] */
#define WM8994_AIF1DAC1_EQ_B4_B_WIDTH               16  /* AIF1DAC1_EQ_B4_B - [15:0] */

/*
 * R1167 (0x48F) - AIF1 DAC1 EQ Band 4 C
 */
#define WM8994_AIF1DAC1_EQ_B4_C_MASK            0xFFFF  /* AIF1DAC1_EQ_B4_C - [15:0] */
#define WM8994_AIF1DAC1_EQ_B4_C_SHIFT                0  /* AIF1DAC1_EQ_B4_C - [15:0] */
#define WM8994_AIF1DAC1_EQ_B4_C_WIDTH               16  /* AIF1DAC1_EQ_B4_C - [15:0] */

/*
 * R1168 (0x490) - AIF1 DAC1 EQ Band 4 PG
 */
#define WM8994_AIF1DAC1_EQ_B4_PG_MASK           0xFFFF  /* AIF1DAC1_EQ_B4_PG - [15:0] */
#define WM8994_AIF1DAC1_EQ_B4_PG_SHIFT               0  /* AIF1DAC1_EQ_B4_PG - [15:0] */
#define WM8994_AIF1DAC1_EQ_B4_PG_WIDTH              16  /* AIF1DAC1_EQ_B4_PG - [15:0] */

/*
 * R1169 (0x491) - AIF1 DAC1 EQ Band 5 A
 */
#define WM8994_AIF1DAC1_EQ_B5_A_MASK            0xFFFF  /* AIF1DAC1_EQ_B5_A - [15:0] */
#define WM8994_AIF1DAC1_EQ_B5_A_SHIFT                0  /* AIF1DAC1_EQ_B5_A - [15:0] */
#define WM8994_AIF1DAC1_EQ_B5_A_WIDTH               16  /* AIF1DAC1_EQ_B5_A - [15:0] */

/*
 * R1170 (0x492) - AIF1 DAC1 EQ Band 5 B
 */
#define WM8994_AIF1DAC1_EQ_B5_B_MASK            0xFFFF  /* AIF1DAC1_EQ_B5_B - [15:0] */
#define WM8994_AIF1DAC1_EQ_B5_B_SHIFT                0  /* AIF1DAC1_EQ_B5_B - [15:0] */
#define WM8994_AIF1DAC1_EQ_B5_B_WIDTH               16  /* AIF1DAC1_EQ_B5_B - [15:0] */

/*
 * R1171 (0x493) - AIF1 DAC1 EQ Band 5 PG
 */
#define WM8994_AIF1DAC1_EQ_B5_PG_MASK           0xFFFF  /* AIF1DAC1_EQ_B5_PG - [15:0] */
#define WM8994_AIF1DAC1_EQ_B5_PG_SHIFT               0  /* AIF1DAC1_EQ_B5_PG - [15:0] */
#define WM8994_AIF1DAC1_EQ_B5_PG_WIDTH              16  /* AIF1DAC1_EQ_B5_PG - [15:0] */

/*
 * R1184 (0x4A0) - AIF1 DAC2 EQ Gains (1)
 */
#define WM8994_AIF1DAC2_EQ_B1_GAIN_MASK         0xF800  /* AIF1DAC2_EQ_B1_GAIN - [15:11] */
#define WM8994_AIF1DAC2_EQ_B1_GAIN_SHIFT            11  /* AIF1DAC2_EQ_B1_GAIN - [15:11] */
#define WM8994_AIF1DAC2_EQ_B1_GAIN_WIDTH             5  /* AIF1DAC2_EQ_B1_GAIN - [15:11] */
#define WM8994_AIF1DAC2_EQ_B2_GAIN_MASK         0x07C0  /* AIF1DAC2_EQ_B2_GAIN - [10:6] */
#define WM8994_AIF1DAC2_EQ_B2_GAIN_SHIFT             6  /* AIF1DAC2_EQ_B2_GAIN - [10:6] */
#define WM8994_AIF1DAC2_EQ_B2_GAIN_WIDTH             5  /* AIF1DAC2_EQ_B2_GAIN - [10:6] */
#define WM8994_AIF1DAC2_EQ_B3_GAIN_MASK         0x003E  /* AIF1DAC2_EQ_B3_GAIN - [5:1] */
#define WM8994_AIF1DAC2_EQ_B3_GAIN_SHIFT             1  /* AIF1DAC2_EQ_B3_GAIN - [5:1] */
#define WM8994_AIF1DAC2_EQ_B3_GAIN_WIDTH             5  /* AIF1DAC2_EQ_B3_GAIN - [5:1] */
#define WM8994_AIF1DAC2_EQ_ENA                  0x0001  /* AIF1DAC2_EQ_ENA */
#define WM8994_AIF1DAC2_EQ_ENA_MASK             0x0001  /* AIF1DAC2_EQ_ENA */
#define WM8994_AIF1DAC2_EQ_ENA_SHIFT                 0  /* AIF1DAC2_EQ_ENA */
#define WM8994_AIF1DAC2_EQ_ENA_WIDTH                 1  /* AIF1DAC2_EQ_ENA */

/*
 * R1185 (0x4A1) - AIF1 DAC2 EQ Gains (2)
 */
#define WM8994_AIF1DAC2_EQ_B4_GAIN_MASK         0xF800  /* AIF1DAC2_EQ_B4_GAIN - [15:11] */
#define WM8994_AIF1DAC2_EQ_B4_GAIN_SHIFT            11  /* AIF1DAC2_EQ_B4_GAIN - [15:11] */
#define WM8994_AIF1DAC2_EQ_B4_GAIN_WIDTH             5  /* AIF1DAC2_EQ_B4_GAIN - [15:11] */
#define WM8994_AIF1DAC2_EQ_B5_GAIN_MASK         0x07C0  /* AIF1DAC2_EQ_B5_GAIN - [10:6] */
#define WM8994_AIF1DAC2_EQ_B5_GAIN_SHIFT             6  /* AIF1DAC2_EQ_B5_GAIN - [10:6] */
#define WM8994_AIF1DAC2_EQ_B5_GAIN_WIDTH             5  /* AIF1DAC2_EQ_B5_GAIN - [10:6] */

/*
 * R1186 (0x4A2) - AIF1 DAC2 EQ Band 1 A
 */
#define WM8994_AIF1DAC2_EQ_B1_A_MASK            0xFFFF  /* AIF1DAC2_EQ_B1_A - [15:0] */
#define WM8994_AIF1DAC2_EQ_B1_A_SHIFT                0  /* AIF1DAC2_EQ_B1_A - [15:0] */
#define WM8994_AIF1DAC2_EQ_B1_A_WIDTH               16  /* AIF1DAC2_EQ_B1_A - [15:0] */

/*
 * R1187 (0x4A3) - AIF1 DAC2 EQ Band 1 B
 */
#define WM8994_AIF1DAC2_EQ_B1_B_MASK            0xFFFF  /* AIF1DAC2_EQ_B1_B - [15:0] */
#define WM8994_AIF1DAC2_EQ_B1_B_SHIFT                0  /* AIF1DAC2_EQ_B1_B - [15:0] */
#define WM8994_AIF1DAC2_EQ_B1_B_WIDTH               16  /* AIF1DAC2_EQ_B1_B - [15:0] */

/*
 * R1188 (0x4A4) - AIF1 DAC2 EQ Band 1 PG
 */
#define WM8994_AIF1DAC2_EQ_B1_PG_MASK           0xFFFF  /* AIF1DAC2_EQ_B1_PG - [15:0] */
#define WM8994_AIF1DAC2_EQ_B1_PG_SHIFT               0  /* AIF1DAC2_EQ_B1_PG - [15:0] */
#define WM8994_AIF1DAC2_EQ_B1_PG_WIDTH              16  /* AIF1DAC2_EQ_B1_PG - [15:0] */

/*
 * R1189 (0x4A5) - AIF1 DAC2 EQ Band 2 A
 */
#define WM8994_AIF1DAC2_EQ_B2_A_MASK            0xFFFF  /* AIF1DAC2_EQ_B2_A - [15:0] */
#define WM8994_AIF1DAC2_EQ_B2_A_SHIFT                0  /* AIF1DAC2_EQ_B2_A - [15:0] */
#define WM8994_AIF1DAC2_EQ_B2_A_WIDTH               16  /* AIF1DAC2_EQ_B2_A - [15:0] */

/*
 * R1190 (0x4A6) - AIF1 DAC2 EQ Band 2 B
 */
#define WM8994_AIF1DAC2_EQ_B2_B_MASK            0xFFFF  /* AIF1DAC2_EQ_B2_B - [15:0] */
#define WM8994_AIF1DAC2_EQ_B2_B_SHIFT                0  /* AIF1DAC2_EQ_B2_B - [15:0] */
#define WM8994_AIF1DAC2_EQ_B2_B_WIDTH               16  /* AIF1DAC2_EQ_B2_B - [15:0] */

/*
 * R1191 (0x4A7) - AIF1 DAC2 EQ Band 2 C
 */
#define WM8994_AIF1DAC2_EQ_B2_C_MASK            0xFFFF  /* AIF1DAC2_EQ_B2_C - [15:0] */
#define WM8994_AIF1DAC2_EQ_B2_C_SHIFT                0  /* AIF1DAC2_EQ_B2_C - [15:0] */
#define WM8994_AIF1DAC2_EQ_B2_C_WIDTH               16  /* AIF1DAC2_EQ_B2_C - [15:0] */

/*
 * R1192 (0x4A8) - AIF1 DAC2 EQ Band 2 PG
 */
#define WM8994_AIF1DAC2_EQ_B2_PG_MASK           0xFFFF  /* AIF1DAC2_EQ_B2_PG - [15:0] */
#define WM8994_AIF1DAC2_EQ_B2_PG_SHIFT               0  /* AIF1DAC2_EQ_B2_PG - [15:0] */
#define WM8994_AIF1DAC2_EQ_B2_PG_WIDTH              16  /* AIF1DAC2_EQ_B2_PG - [15:0] */

/*
 * R1193 (0x4A9) - AIF1 DAC2 EQ Band 3 A
 */
#define WM8994_AIF1DAC2_EQ_B3_A_MASK            0xFFFF  /* AIF1DAC2_EQ_B3_A - [15:0] */
#define WM8994_AIF1DAC2_EQ_B3_A_SHIFT                0  /* AIF1DAC2_EQ_B3_A - [15:0] */
#define WM8994_AIF1DAC2_EQ_B3_A_WIDTH               16  /* AIF1DAC2_EQ_B3_A - [15:0] */

/*
 * R1194 (0x4AA) - AIF1 DAC2 EQ Band 3 B
 */
#define WM8994_AIF1DAC2_EQ_B3_B_MASK            0xFFFF  /* AIF1DAC2_EQ_B3_B - [15:0] */
#define WM8994_AIF1DAC2_EQ_B3_B_SHIFT                0  /* AIF1DAC2_EQ_B3_B - [15:0] */
#define WM8994_AIF1DAC2_EQ_B3_B_WIDTH               16  /* AIF1DAC2_EQ_B3_B - [15:0] */

/*
 * R1195 (0x4AB) - AIF1 DAC2 EQ Band 3 C
 */
#define WM8994_AIF1DAC2_EQ_B3_C_MASK            0xFFFF  /* AIF1DAC2_EQ_B3_C - [15:0] */
#define WM8994_AIF1DAC2_EQ_B3_C_SHIFT                0  /* AIF1DAC2_EQ_B3_C - [15:0] */
#define WM8994_AIF1DAC2_EQ_B3_C_WIDTH               16  /* AIF1DAC2_EQ_B3_C - [15:0] */

/*
 * R1196 (0x4AC) - AIF1 DAC2 EQ Band 3 PG
 */
#define WM8994_AIF1DAC2_EQ_B3_PG_MASK           0xFFFF  /* AIF1DAC2_EQ_B3_PG - [15:0] */
#define WM8994_AIF1DAC2_EQ_B3_PG_SHIFT               0  /* AIF1DAC2_EQ_B3_PG - [15:0] */
#define WM8994_AIF1DAC2_EQ_B3_PG_WIDTH              16  /* AIF1DAC2_EQ_B3_PG - [15:0] */

/*
 * R1197 (0x4AD) - AIF1 DAC2 EQ Band 4 A
 */
#define WM8994_AIF1DAC2_EQ_B4_A_MASK            0xFFFF  /* AIF1DAC2_EQ_B4_A - [15:0] */
#define WM8994_AIF1DAC2_EQ_B4_A_SHIFT                0  /* AIF1DAC2_EQ_B4_A - [15:0] */
#define WM8994_AIF1DAC2_EQ_B4_A_WIDTH               16  /* AIF1DAC2_EQ_B4_A - [15:0] */

/*
 * R1198 (0x4AE) - AIF1 DAC2 EQ Band 4 B
 */
#define WM8994_AIF1DAC2_EQ_B4_B_MASK            0xFFFF  /* AIF1DAC2_EQ_B4_B - [15:0] */
#define WM8994_AIF1DAC2_EQ_B4_B_SHIFT                0  /* AIF1DAC2_EQ_B4_B - [15:0] */
#define WM8994_AIF1DAC2_EQ_B4_B_WIDTH               16  /* AIF1DAC2_EQ_B4_B - [15:0] */

/*
 * R1199 (0x4AF) - AIF1 DAC2 EQ Band 4 C
 */
#define WM8994_AIF1DAC2_EQ_B4_C_MASK            0xFFFF  /* AIF1DAC2_EQ_B4_C - [15:0] */
#define WM8994_AIF1DAC2_EQ_B4_C_SHIFT                0  /* AIF1DAC2_EQ_B4_C - [15:0] */
#define WM8994_AIF1DAC2_EQ_B4_C_WIDTH               16  /* AIF1DAC2_EQ_B4_C - [15:0] */

/*
 * R1200 (0x4B0) - AIF1 DAC2 EQ Band 4 PG
 */
#define WM8994_AIF1DAC2_EQ_B4_PG_MASK           0xFFFF  /* AIF1DAC2_EQ_B4_PG - [15:0] */
#define WM8994_AIF1DAC2_EQ_B4_PG_SHIFT               0  /* AIF1DAC2_EQ_B4_PG - [15:0] */
#define WM8994_AIF1DAC2_EQ_B4_PG_WIDTH              16  /* AIF1DAC2_EQ_B4_PG - [15:0] */

/*
 * R1201 (0x4B1) - AIF1 DAC2 EQ Band 5 A
 */
#define WM8994_AIF1DAC2_EQ_B5_A_MASK            0xFFFF  /* AIF1DAC2_EQ_B5_A - [15:0] */
#define WM8994_AIF1DAC2_EQ_B5_A_SHIFT                0  /* AIF1DAC2_EQ_B5_A - [15:0] */
#define WM8994_AIF1DAC2_EQ_B5_A_WIDTH               16  /* AIF1DAC2_EQ_B5_A - [15:0] */

/*
 * R1202 (0x4B2) - AIF1 DAC2 EQ Band 5 B
 */
#define WM8994_AIF1DAC2_EQ_B5_B_MASK            0xFFFF  /* AIF1DAC2_EQ_B5_B - [15:0] */
#define WM8994_AIF1DAC2_EQ_B5_B_SHIFT                0  /* AIF1DAC2_EQ_B5_B - [15:0] */
#define WM8994_AIF1DAC2_EQ_B5_B_WIDTH               16  /* AIF1DAC2_EQ_B5_B - [15:0] */

/*
 * R1203 (0x4B3) - AIF1 DAC2 EQ Band 5 PG
 */
#define WM8994_AIF1DAC2_EQ_B5_PG_MASK           0xFFFF  /* AIF1DAC2_EQ_B5_PG - [15:0] */
#define WM8994_AIF1DAC2_EQ_B5_PG_SHIFT               0  /* AIF1DAC2_EQ_B5_PG - [15:0] */
#define WM8994_AIF1DAC2_EQ_B5_PG_WIDTH              16  /* AIF1DAC2_EQ_B5_PG - [15:0] */

/*
 * R1280 (0x500) - AIF2 ADC Left Volume
 */
#define WM8994_AIF2ADC_VU                       0x0100  /* AIF2ADC_VU */
#define WM8994_AIF2ADC_VU_MASK                  0x0100  /* AIF2ADC_VU */
#define WM8994_AIF2ADC_VU_SHIFT                      8  /* AIF2ADC_VU */
#define WM8994_AIF2ADC_VU_WIDTH                      1  /* AIF2ADC_VU */
#define WM8994_AIF2ADCL_VOL_MASK                0x00FF  /* AIF2ADCL_VOL - [7:0] */
#define WM8994_AIF2ADCL_VOL_SHIFT                    0  /* AIF2ADCL_VOL - [7:0] */
#define WM8994_AIF2ADCL_VOL_WIDTH                    8  /* AIF2ADCL_VOL - [7:0] */

/*
 * R1281 (0x501) - AIF2 ADC Right Volume
 */
#define WM8994_AIF2ADC_VU                       0x0100  /* AIF2ADC_VU */
#define WM8994_AIF2ADC_VU_MASK                  0x0100  /* AIF2ADC_VU */
#define WM8994_AIF2ADC_VU_SHIFT                      8  /* AIF2ADC_VU */
#define WM8994_AIF2ADC_VU_WIDTH                      1  /* AIF2ADC_VU */
#define WM8994_AIF2ADCR_VOL_MASK                0x00FF  /* AIF2ADCR_VOL - [7:0] */
#define WM8994_AIF2ADCR_VOL_SHIFT                    0  /* AIF2ADCR_VOL - [7:0] */
#define WM8994_AIF2ADCR_VOL_WIDTH                    8  /* AIF2ADCR_VOL - [7:0] */

/*
 * R1282 (0x502) - AIF2 DAC Left Volume
 */
#define WM8994_AIF2DAC_VU                       0x0100  /* AIF2DAC_VU */
#define WM8994_AIF2DAC_VU_MASK                  0x0100  /* AIF2DAC_VU */
#define WM8994_AIF2DAC_VU_SHIFT                      8  /* AIF2DAC_VU */
#define WM8994_AIF2DAC_VU_WIDTH                      1  /* AIF2DAC_VU */
#define WM8994_AIF2DACL_VOL_MASK                0x00FF  /* AIF2DACL_VOL - [7:0] */
#define WM8994_AIF2DACL_VOL_SHIFT                    0  /* AIF2DACL_VOL - [7:0] */
#define WM8994_AIF2DACL_VOL_WIDTH                    8  /* AIF2DACL_VOL - [7:0] */

/*
 * R1283 (0x503) - AIF2 DAC Right Volume
 */
#define WM8994_AIF2DAC_VU                       0x0100  /* AIF2DAC_VU */
#define WM8994_AIF2DAC_VU_MASK                  0x0100  /* AIF2DAC_VU */
#define WM8994_AIF2DAC_VU_SHIFT                      8  /* AIF2DAC_VU */
#define WM8994_AIF2DAC_VU_WIDTH                      1  /* AIF2DAC_VU */
#define WM8994_AIF2DACR_VOL_MASK                0x00FF  /* AIF2DACR_VOL - [7:0] */
#define WM8994_AIF2DACR_VOL_SHIFT                    0  /* AIF2DACR_VOL - [7:0] */
#define WM8994_AIF2DACR_VOL_WIDTH                    8  /* AIF2DACR_VOL - [7:0] */

/*
 * R1296 (0x510) - AIF2 ADC Filters
 */
#define WM8994_AIF2ADC_4FS                      0x8000  /* AIF2ADC_4FS */
#define WM8994_AIF2ADC_4FS_MASK                 0x8000  /* AIF2ADC_4FS */
#define WM8994_AIF2ADC_4FS_SHIFT                    15  /* AIF2ADC_4FS */
#define WM8994_AIF2ADC_4FS_WIDTH                     1  /* AIF2ADC_4FS */
#define WM8994_AIF2ADC_HPF_CUT_MASK             0x6000  /* AIF2ADC_HPF_CUT - [14:13] */
#define WM8994_AIF2ADC_HPF_CUT_SHIFT                13  /* AIF2ADC_HPF_CUT - [14:13] */
#define WM8994_AIF2ADC_HPF_CUT_WIDTH                 2  /* AIF2ADC_HPF_CUT - [14:13] */
#define WM8994_AIF2ADCL_HPF                     0x1000  /* AIF2ADCL_HPF */
#define WM8994_AIF2ADCL_HPF_MASK                0x1000  /* AIF2ADCL_HPF */
#define WM8994_AIF2ADCL_HPF_SHIFT                   12  /* AIF2ADCL_HPF */
#define WM8994_AIF2ADCL_HPF_WIDTH                    1  /* AIF2ADCL_HPF */
#define WM8994_AIF2ADCR_HPF                     0x0800  /* AIF2ADCR_HPF */
#define WM8994_AIF2ADCR_HPF_MASK                0x0800  /* AIF2ADCR_HPF */
#define WM8994_AIF2ADCR_HPF_SHIFT                   11  /* AIF2ADCR_HPF */
#define WM8994_AIF2ADCR_HPF_WIDTH                    1  /* AIF2ADCR_HPF */

/*
 * R1312 (0x520) - AIF2 DAC Filters (1)
 */
#define WM8994_AIF2DAC_MUTE                     0x0200  /* AIF2DAC_MUTE */
#define WM8994_AIF2DAC_MUTE_MASK                0x0200  /* AIF2DAC_MUTE */
#define WM8994_AIF2DAC_MUTE_SHIFT                    9  /* AIF2DAC_MUTE */
#define WM8994_AIF2DAC_MUTE_WIDTH                    1  /* AIF2DAC_MUTE */
#define WM8994_AIF2DAC_MONO                     0x0080  /* AIF2DAC_MONO */
#define WM8994_AIF2DAC_MONO_MASK                0x0080  /* AIF2DAC_MONO */
#define WM8994_AIF2DAC_MONO_SHIFT                    7  /* AIF2DAC_MONO */
#define WM8994_AIF2DAC_MONO_WIDTH                    1  /* AIF2DAC_MONO */
#define WM8994_AIF2DAC_MUTERATE                 0x0020  /* AIF2DAC_MUTERATE */
#define WM8994_AIF2DAC_MUTERATE_MASK            0x0020  /* AIF2DAC_MUTERATE */
#define WM8994_AIF2DAC_MUTERATE_SHIFT                5  /* AIF2DAC_MUTERATE */
#define WM8994_AIF2DAC_MUTERATE_WIDTH                1  /* AIF2DAC_MUTERATE */
#define WM8994_AIF2DAC_UNMUTE_RAMP              0x0010  /* AIF2DAC_UNMUTE_RAMP */
#define WM8994_AIF2DAC_UNMUTE_RAMP_MASK         0x0010  /* AIF2DAC_UNMUTE_RAMP */
#define WM8994_AIF2DAC_UNMUTE_RAMP_SHIFT             4  /* AIF2DAC_UNMUTE_RAMP */
#define WM8994_AIF2DAC_UNMUTE_RAMP_WIDTH             1  /* AIF2DAC_UNMUTE_RAMP */
#define WM8994_AIF2DAC_DEEMP_MASK               0x0006  /* AIF2DAC_DEEMP - [2:1] */
#define WM8994_AIF2DAC_DEEMP_SHIFT                   1  /* AIF2DAC_DEEMP - [2:1] */
#define WM8994_AIF2DAC_DEEMP_WIDTH                   2  /* AIF2DAC_DEEMP - [2:1] */

/*
 * R1313 (0x521) - AIF2 DAC Filters (2)
 */
#define WM8994_AIF2DAC_3D_GAIN_MASK             0x3E00  /* AIF2DAC_3D_GAIN - [13:9] */
#define WM8994_AIF2DAC_3D_GAIN_SHIFT                 9  /* AIF2DAC_3D_GAIN - [13:9] */
#define WM8994_AIF2DAC_3D_GAIN_WIDTH                 5  /* AIF2DAC_3D_GAIN - [13:9] */
#define WM8994_AIF2DAC_3D_ENA                   0x0100  /* AIF2DAC_3D_ENA */
#define WM8994_AIF2DAC_3D_ENA_MASK              0x0100  /* AIF2DAC_3D_ENA */
#define WM8994_AIF2DAC_3D_ENA_SHIFT                  8  /* AIF2DAC_3D_ENA */
#define WM8994_AIF2DAC_3D_ENA_WIDTH                  1  /* AIF2DAC_3D_ENA */

/*
 * R1344 (0x540) - AIF2 DRC (1)
 */
#define WM8994_AIF2DRC_SIG_DET_RMS_MASK         0xF800  /* AIF2DRC_SIG_DET_RMS - [15:11] */
#define WM8994_AIF2DRC_SIG_DET_RMS_SHIFT            11  /* AIF2DRC_SIG_DET_RMS - [15:11] */
#define WM8994_AIF2DRC_SIG_DET_RMS_WIDTH             5  /* AIF2DRC_SIG_DET_RMS - [15:11] */
#define WM8994_AIF2DRC_SIG_DET_PK_MASK          0x0600  /* AIF2DRC_SIG_DET_PK - [10:9] */
#define WM8994_AIF2DRC_SIG_DET_PK_SHIFT              9  /* AIF2DRC_SIG_DET_PK - [10:9] */
#define WM8994_AIF2DRC_SIG_DET_PK_WIDTH              2  /* AIF2DRC_SIG_DET_PK - [10:9] */
#define WM8994_AIF2DRC_NG_ENA                   0x0100  /* AIF2DRC_NG_ENA */
#define WM8994_AIF2DRC_NG_ENA_MASK              0x0100  /* AIF2DRC_NG_ENA */
#define WM8994_AIF2DRC_NG_ENA_SHIFT                  8  /* AIF2DRC_NG_ENA */
#define WM8994_AIF2DRC_NG_ENA_WIDTH                  1  /* AIF2DRC_NG_ENA */
#define WM8994_AIF2DRC_SIG_DET_MODE             0x0080  /* AIF2DRC_SIG_DET_MODE */
#define WM8994_AIF2DRC_SIG_DET_MODE_MASK        0x0080  /* AIF2DRC_SIG_DET_MODE */
#define WM8994_AIF2DRC_SIG_DET_MODE_SHIFT            7  /* AIF2DRC_SIG_DET_MODE */
#define WM8994_AIF2DRC_SIG_DET_MODE_WIDTH            1  /* AIF2DRC_SIG_DET_MODE */
#define WM8994_AIF2DRC_SIG_DET                  0x0040  /* AIF2DRC_SIG_DET */
#define WM8994_AIF2DRC_SIG_DET_MASK             0x0040  /* AIF2DRC_SIG_DET */
#define WM8994_AIF2DRC_SIG_DET_SHIFT                 6  /* AIF2DRC_SIG_DET */
#define WM8994_AIF2DRC_SIG_DET_WIDTH                 1  /* AIF2DRC_SIG_DET */
#define WM8994_AIF2DRC_KNEE2_OP_ENA             0x0020  /* AIF2DRC_KNEE2_OP_ENA */
#define WM8994_AIF2DRC_KNEE2_OP_ENA_MASK        0x0020  /* AIF2DRC_KNEE2_OP_ENA */
#define WM8994_AIF2DRC_KNEE2_OP_ENA_SHIFT            5  /* AIF2DRC_KNEE2_OP_ENA */
#define WM8994_AIF2DRC_KNEE2_OP_ENA_WIDTH            1  /* AIF2DRC_KNEE2_OP_ENA */
#define WM8994_AIF2DRC_QR                       0x0010  /* AIF2DRC_QR */
#define WM8994_AIF2DRC_QR_MASK                  0x0010  /* AIF2DRC_QR */
#define WM8994_AIF2DRC_QR_SHIFT                      4  /* AIF2DRC_QR */
#define WM8994_AIF2DRC_QR_WIDTH                      1  /* AIF2DRC_QR */
#define WM8994_AIF2DRC_ANTICLIP                 0x0008  /* AIF2DRC_ANTICLIP */
#define WM8994_AIF2DRC_ANTICLIP_MASK            0x0008  /* AIF2DRC_ANTICLIP */
#define WM8994_AIF2DRC_ANTICLIP_SHIFT                3  /* AIF2DRC_ANTICLIP */
#define WM8994_AIF2DRC_ANTICLIP_WIDTH                1  /* AIF2DRC_ANTICLIP */
#define WM8994_AIF2DAC_DRC_ENA                  0x0004  /* AIF2DAC_DRC_ENA */
#define WM8994_AIF2DAC_DRC_ENA_MASK             0x0004  /* AIF2DAC_DRC_ENA */
#define WM8994_AIF2DAC_DRC_ENA_SHIFT                 2  /* AIF2DAC_DRC_ENA */
#define WM8994_AIF2DAC_DRC_ENA_WIDTH                 1  /* AIF2DAC_DRC_ENA */
#define WM8994_AIF2ADCL_DRC_ENA                 0x0002  /* AIF2ADCL_DRC_ENA */
#define WM8994_AIF2ADCL_DRC_ENA_MASK            0x0002  /* AIF2ADCL_DRC_ENA */
#define WM8994_AIF2ADCL_DRC_ENA_SHIFT                1  /* AIF2ADCL_DRC_ENA */
#define WM8994_AIF2ADCL_DRC_ENA_WIDTH                1  /* AIF2ADCL_DRC_ENA */
#define WM8994_AIF2ADCR_DRC_ENA                 0x0001  /* AIF2ADCR_DRC_ENA */
#define WM8994_AIF2ADCR_DRC_ENA_MASK            0x0001  /* AIF2ADCR_DRC_ENA */
#define WM8994_AIF2ADCR_DRC_ENA_SHIFT                0  /* AIF2ADCR_DRC_ENA */
#define WM8994_AIF2ADCR_DRC_ENA_WIDTH                1  /* AIF2ADCR_DRC_ENA */

/*
 * R1345 (0x541) - AIF2 DRC (2)
 */
#define WM8994_AIF2DRC_ATK_MASK                 0x1E00  /* AIF2DRC_ATK - [12:9] */
#define WM8994_AIF2DRC_ATK_SHIFT                     9  /* AIF2DRC_ATK - [12:9] */
#define WM8994_AIF2DRC_ATK_WIDTH                     4  /* AIF2DRC_ATK - [12:9] */
#define WM8994_AIF2DRC_DCY_MASK                 0x01E0  /* AIF2DRC_DCY - [8:5] */
#define WM8994_AIF2DRC_DCY_SHIFT                     5  /* AIF2DRC_DCY - [8:5] */
#define WM8994_AIF2DRC_DCY_WIDTH                     4  /* AIF2DRC_DCY - [8:5] */
#define WM8994_AIF2DRC_MINGAIN_MASK             0x001C  /* AIF2DRC_MINGAIN - [4:2] */
#define WM8994_AIF2DRC_MINGAIN_SHIFT                 2  /* AIF2DRC_MINGAIN - [4:2] */
#define WM8994_AIF2DRC_MINGAIN_WIDTH                 3  /* AIF2DRC_MINGAIN - [4:2] */
#define WM8994_AIF2DRC_MAXGAIN_MASK             0x0003  /* AIF2DRC_MAXGAIN - [1:0] */
#define WM8994_AIF2DRC_MAXGAIN_SHIFT                 0  /* AIF2DRC_MAXGAIN - [1:0] */
#define WM8994_AIF2DRC_MAXGAIN_WIDTH                 2  /* AIF2DRC_MAXGAIN - [1:0] */

/*
 * R1346 (0x542) - AIF2 DRC (3)
 */
#define WM8994_AIF2DRC_NG_MINGAIN_MASK          0xF000  /* AIF2DRC_NG_MINGAIN - [15:12] */
#define WM8994_AIF2DRC_NG_MINGAIN_SHIFT             12  /* AIF2DRC_NG_MINGAIN - [15:12] */
#define WM8994_AIF2DRC_NG_MINGAIN_WIDTH              4  /* AIF2DRC_NG_MINGAIN - [15:12] */
#define WM8994_AIF2DRC_NG_EXP_MASK              0x0C00  /* AIF2DRC_NG_EXP - [11:10] */
#define WM8994_AIF2DRC_NG_EXP_SHIFT                 10  /* AIF2DRC_NG_EXP - [11:10] */
#define WM8994_AIF2DRC_NG_EXP_WIDTH                  2  /* AIF2DRC_NG_EXP - [11:10] */
#define WM8994_AIF2DRC_QR_THR_MASK              0x0300  /* AIF2DRC_QR_THR - [9:8] */
#define WM8994_AIF2DRC_QR_THR_SHIFT                  8  /* AIF2DRC_QR_THR - [9:8] */
#define WM8994_AIF2DRC_QR_THR_WIDTH                  2  /* AIF2DRC_QR_THR - [9:8] */
#define WM8994_AIF2DRC_QR_DCY_MASK              0x00C0  /* AIF2DRC_QR_DCY - [7:6] */
#define WM8994_AIF2DRC_QR_DCY_SHIFT                  6  /* AIF2DRC_QR_DCY - [7:6] */
#define WM8994_AIF2DRC_QR_DCY_WIDTH                  2  /* AIF2DRC_QR_DCY - [7:6] */
#define WM8994_AIF2DRC_HI_COMP_MASK             0x0038  /* AIF2DRC_HI_COMP - [5:3] */
#define WM8994_AIF2DRC_HI_COMP_SHIFT                 3  /* AIF2DRC_HI_COMP - [5:3] */
#define WM8994_AIF2DRC_HI_COMP_WIDTH                 3  /* AIF2DRC_HI_COMP - [5:3] */
#define WM8994_AIF2DRC_LO_COMP_MASK             0x0007  /* AIF2DRC_LO_COMP - [2:0] */
#define WM8994_AIF2DRC_LO_COMP_SHIFT                 0  /* AIF2DRC_LO_COMP - [2:0] */
#define WM8994_AIF2DRC_LO_COMP_WIDTH                 3  /* AIF2DRC_LO_COMP - [2:0] */

/*
 * R1347 (0x543) - AIF2 DRC (4)
 */
#define WM8994_AIF2DRC_KNEE_IP_MASK             0x07E0  /* AIF2DRC_KNEE_IP - [10:5] */
#define WM8994_AIF2DRC_KNEE_IP_SHIFT                 5  /* AIF2DRC_KNEE_IP - [10:5] */
#define WM8994_AIF2DRC_KNEE_IP_WIDTH                 6  /* AIF2DRC_KNEE_IP - [10:5] */
#define WM8994_AIF2DRC_KNEE_OP_MASK             0x001F  /* AIF2DRC_KNEE_OP - [4:0] */
#define WM8994_AIF2DRC_KNEE_OP_SHIFT                 0  /* AIF2DRC_KNEE_OP - [4:0] */
#define WM8994_AIF2DRC_KNEE_OP_WIDTH                 5  /* AIF2DRC_KNEE_OP - [4:0] */

/*
 * R1348 (0x544) - AIF2 DRC (5)
 */
#define WM8994_AIF2DRC_KNEE2_IP_MASK            0x03E0  /* AIF2DRC_KNEE2_IP - [9:5] */
#define WM8994_AIF2DRC_KNEE2_IP_SHIFT                5  /* AIF2DRC_KNEE2_IP - [9:5] */
#define WM8994_AIF2DRC_KNEE2_IP_WIDTH                5  /* AIF2DRC_KNEE2_IP - [9:5] */
#define WM8994_AIF2DRC_KNEE2_OP_MASK            0x001F  /* AIF2DRC_KNEE2_OP - [4:0] */
#define WM8994_AIF2DRC_KNEE2_OP_SHIFT                0  /* AIF2DRC_KNEE2_OP - [4:0] */
#define WM8994_AIF2DRC_KNEE2_OP_WIDTH                5  /* AIF2DRC_KNEE2_OP - [4:0] */

/*
 * R1408 (0x580) - AIF2 EQ Gains (1)
 */
#define WM8994_AIF2DAC_EQ_B1_GAIN_MASK          0xF800  /* AIF2DAC_EQ_B1_GAIN - [15:11] */
#define WM8994_AIF2DAC_EQ_B1_GAIN_SHIFT             11  /* AIF2DAC_EQ_B1_GAIN - [15:11] */
#define WM8994_AIF2DAC_EQ_B1_GAIN_WIDTH              5  /* AIF2DAC_EQ_B1_GAIN - [15:11] */
#define WM8994_AIF2DAC_EQ_B2_GAIN_MASK          0x07C0  /* AIF2DAC_EQ_B2_GAIN - [10:6] */
#define WM8994_AIF2DAC_EQ_B2_GAIN_SHIFT              6  /* AIF2DAC_EQ_B2_GAIN - [10:6] */
#define WM8994_AIF2DAC_EQ_B2_GAIN_WIDTH              5  /* AIF2DAC_EQ_B2_GAIN - [10:6] */
#define WM8994_AIF2DAC_EQ_B3_GAIN_MASK          0x003E  /* AIF2DAC_EQ_B3_GAIN - [5:1] */
#define WM8994_AIF2DAC_EQ_B3_GAIN_SHIFT              1  /* AIF2DAC_EQ_B3_GAIN - [5:1] */
#define WM8994_AIF2DAC_EQ_B3_GAIN_WIDTH              5  /* AIF2DAC_EQ_B3_GAIN - [5:1] */
#define WM8994_AIF2DAC_EQ_ENA                   0x0001  /* AIF2DAC_EQ_ENA */
#define WM8994_AIF2DAC_EQ_ENA_MASK              0x0001  /* AIF2DAC_EQ_ENA */
#define WM8994_AIF2DAC_EQ_ENA_SHIFT                  0  /* AIF2DAC_EQ_ENA */
#define WM8994_AIF2DAC_EQ_ENA_WIDTH                  1  /* AIF2DAC_EQ_ENA */

/*
 * R1409 (0x581) - AIF2 EQ Gains (2)
 */
#define WM8994_AIF2DAC_EQ_B4_GAIN_MASK          0xF800  /* AIF2DAC_EQ_B4_GAIN - [15:11] */
#define WM8994_AIF2DAC_EQ_B4_GAIN_SHIFT             11  /* AIF2DAC_EQ_B4_GAIN - [15:11] */
#define WM8994_AIF2DAC_EQ_B4_GAIN_WIDTH              5  /* AIF2DAC_EQ_B4_GAIN - [15:11] */
#define WM8994_AIF2DAC_EQ_B5_GAIN_MASK          0x07C0  /* AIF2DAC_EQ_B5_GAIN - [10:6] */
#define WM8994_AIF2DAC_EQ_B5_GAIN_SHIFT              6  /* AIF2DAC_EQ_B5_GAIN - [10:6] */
#define WM8994_AIF2DAC_EQ_B5_GAIN_WIDTH              5  /* AIF2DAC_EQ_B5_GAIN - [10:6] */

/*
 * R1410 (0x582) - AIF2 EQ Band 1 A
 */
#define WM8994_AIF2DAC_EQ_B1_A_MASK             0xFFFF  /* AIF2DAC_EQ_B1_A - [15:0] */
#define WM8994_AIF2DAC_EQ_B1_A_SHIFT                 0  /* AIF2DAC_EQ_B1_A - [15:0] */
#define WM8994_AIF2DAC_EQ_B1_A_WIDTH                16  /* AIF2DAC_EQ_B1_A - [15:0] */

/*
 * R1411 (0x583) - AIF2 EQ Band 1 B
 */
#define WM8994_AIF2DAC_EQ_B1_B_MASK             0xFFFF  /* AIF2DAC_EQ_B1_B - [15:0] */
#define WM8994_AIF2DAC_EQ_B1_B_SHIFT                 0  /* AIF2DAC_EQ_B1_B - [15:0] */
#define WM8994_AIF2DAC_EQ_B1_B_WIDTH                16  /* AIF2DAC_EQ_B1_B - [15:0] */

/*
 * R1412 (0x584) - AIF2 EQ Band 1 PG
 */
#define WM8994_AIF2DAC_EQ_B1_PG_MASK            0xFFFF  /* AIF2DAC_EQ_B1_PG - [15:0] */
#define WM8994_AIF2DAC_EQ_B1_PG_SHIFT                0  /* AIF2DAC_EQ_B1_PG - [15:0] */
#define WM8994_AIF2DAC_EQ_B1_PG_WIDTH               16  /* AIF2DAC_EQ_B1_PG - [15:0] */

/*
 * R1413 (0x585) - AIF2 EQ Band 2 A
 */
#define WM8994_AIF2DAC_EQ_B2_A_MASK             0xFFFF  /* AIF2DAC_EQ_B2_A - [15:0] */
#define WM8994_AIF2DAC_EQ_B2_A_SHIFT                 0  /* AIF2DAC_EQ_B2_A - [15:0] */
#define WM8994_AIF2DAC_EQ_B2_A_WIDTH                16  /* AIF2DAC_EQ_B2_A - [15:0] */

/*
 * R1414 (0x586) - AIF2 EQ Band 2 B
 */
#define WM8994_AIF2DAC_EQ_B2_B_MASK             0xFFFF  /* AIF2DAC_EQ_B2_B - [15:0] */
#define WM8994_AIF2DAC_EQ_B2_B_SHIFT                 0  /* AIF2DAC_EQ_B2_B - [15:0] */
#define WM8994_AIF2DAC_EQ_B2_B_WIDTH                16  /* AIF2DAC_EQ_B2_B - [15:0] */

/*
 * R1415 (0x587) - AIF2 EQ Band 2 C
 */
#define WM8994_AIF2DAC_EQ_B2_C_MASK             0xFFFF  /* AIF2DAC_EQ_B2_C - [15:0] */
#define WM8994_AIF2DAC_EQ_B2_C_SHIFT                 0  /* AIF2DAC_EQ_B2_C - [15:0] */
#define WM8994_AIF2DAC_EQ_B2_C_WIDTH                16  /* AIF2DAC_EQ_B2_C - [15:0] */

/*
 * R1416 (0x588) - AIF2 EQ Band 2 PG
 */
#define WM8994_AIF2DAC_EQ_B2_PG_MASK            0xFFFF  /* AIF2DAC_EQ_B2_PG - [15:0] */
#define WM8994_AIF2DAC_EQ_B2_PG_SHIFT                0  /* AIF2DAC_EQ_B2_PG - [15:0] */
#define WM8994_AIF2DAC_EQ_B2_PG_WIDTH               16  /* AIF2DAC_EQ_B2_PG - [15:0] */

/*
 * R1417 (0x589) - AIF2 EQ Band 3 A
 */
#define WM8994_AIF2DAC_EQ_B3_A_MASK             0xFFFF  /* AIF2DAC_EQ_B3_A - [15:0] */
#define WM8994_AIF2DAC_EQ_B3_A_SHIFT                 0  /* AIF2DAC_EQ_B3_A - [15:0] */
#define WM8994_AIF2DAC_EQ_B3_A_WIDTH                16  /* AIF2DAC_EQ_B3_A - [15:0] */

/*
 * R1418 (0x58A) - AIF2 EQ Band 3 B
 */
#define WM8994_AIF2DAC_EQ_B3_B_MASK             0xFFFF  /* AIF2DAC_EQ_B3_B - [15:0] */
#define WM8994_AIF2DAC_EQ_B3_B_SHIFT                 0  /* AIF2DAC_EQ_B3_B - [15:0] */
#define WM8994_AIF2DAC_EQ_B3_B_WIDTH                16  /* AIF2DAC_EQ_B3_B - [15:0] */

/*
 * R1419 (0x58B) - AIF2 EQ Band 3 C
 */
#define WM8994_AIF2DAC_EQ_B3_C_MASK             0xFFFF  /* AIF2DAC_EQ_B3_C - [15:0] */
#define WM8994_AIF2DAC_EQ_B3_C_SHIFT                 0  /* AIF2DAC_EQ_B3_C - [15:0] */
#define WM8994_AIF2DAC_EQ_B3_C_WIDTH                16  /* AIF2DAC_EQ_B3_C - [15:0] */

/*
 * R1420 (0x58C) - AIF2 EQ Band 3 PG
 */
#define WM8994_AIF2DAC_EQ_B3_PG_MASK            0xFFFF  /* AIF2DAC_EQ_B3_PG - [15:0] */
#define WM8994_AIF2DAC_EQ_B3_PG_SHIFT                0  /* AIF2DAC_EQ_B3_PG - [15:0] */
#define WM8994_AIF2DAC_EQ_B3_PG_WIDTH               16  /* AIF2DAC_EQ_B3_PG - [15:0] */

/*
 * R1421 (0x58D) - AIF2 EQ Band 4 A
 */
#define WM8994_AIF2DAC_EQ_B4_A_MASK             0xFFFF  /* AIF2DAC_EQ_B4_A - [15:0] */
#define WM8994_AIF2DAC_EQ_B4_A_SHIFT                 0  /* AIF2DAC_EQ_B4_A - [15:0] */
#define WM8994_AIF2DAC_EQ_B4_A_WIDTH                16  /* AIF2DAC_EQ_B4_A - [15:0] */

/*
 * R1422 (0x58E) - AIF2 EQ Band 4 B
 */
#define WM8994_AIF2DAC_EQ_B4_B_MASK             0xFFFF  /* AIF2DAC_EQ_B4_B - [15:0] */
#define WM8994_AIF2DAC_EQ_B4_B_SHIFT                 0  /* AIF2DAC_EQ_B4_B - [15:0] */
#define WM8994_AIF2DAC_EQ_B4_B_WIDTH                16  /* AIF2DAC_EQ_B4_B - [15:0] */

/*
 * R1423 (0x58F) - AIF2 EQ Band 4 C
 */
#define WM8994_AIF2DAC_EQ_B4_C_MASK             0xFFFF  /* AIF2DAC_EQ_B4_C - [15:0] */
#define WM8994_AIF2DAC_EQ_B4_C_SHIFT                 0  /* AIF2DAC_EQ_B4_C - [15:0] */
#define WM8994_AIF2DAC_EQ_B4_C_WIDTH                16  /* AIF2DAC_EQ_B4_C - [15:0] */

/*
 * R1424 (0x590) - AIF2 EQ Band 4 PG
 */
#define WM8994_AIF2DAC_EQ_B4_PG_MASK            0xFFFF  /* AIF2DAC_EQ_B4_PG - [15:0] */
#define WM8994_AIF2DAC_EQ_B4_PG_SHIFT                0  /* AIF2DAC_EQ_B4_PG - [15:0] */
#define WM8994_AIF2DAC_EQ_B4_PG_WIDTH               16  /* AIF2DAC_EQ_B4_PG - [15:0] */

/*
 * R1425 (0x591) - AIF2 EQ Band 5 A
 */
#define WM8994_AIF2DAC_EQ_B5_A_MASK             0xFFFF  /* AIF2DAC_EQ_B5_A - [15:0] */
#define WM8994_AIF2DAC_EQ_B5_A_SHIFT                 0  /* AIF2DAC_EQ_B5_A - [15:0] */
#define WM8994_AIF2DAC_EQ_B5_A_WIDTH                16  /* AIF2DAC_EQ_B5_A - [15:0] */

/*
 * R1426 (0x592) - AIF2 EQ Band 5 B
 */
#define WM8994_AIF2DAC_EQ_B5_B_MASK             0xFFFF  /* AIF2DAC_EQ_B5_B - [15:0] */
#define WM8994_AIF2DAC_EQ_B5_B_SHIFT                 0  /* AIF2DAC_EQ_B5_B - [15:0] */
#define WM8994_AIF2DAC_EQ_B5_B_WIDTH                16  /* AIF2DAC_EQ_B5_B - [15:0] */

/*
 * R1427 (0x593) - AIF2 EQ Band 5 PG
 */
#define WM8994_AIF2DAC_EQ_B5_PG_MASK            0xFFFF  /* AIF2DAC_EQ_B5_PG - [15:0] */
#define WM8994_AIF2DAC_EQ_B5_PG_SHIFT                0  /* AIF2DAC_EQ_B5_PG - [15:0] */
#define WM8994_AIF2DAC_EQ_B5_PG_WIDTH               16  /* AIF2DAC_EQ_B5_PG - [15:0] */

/*
 * R1536 (0x600) - DAC1 Mixer Volumes
 */
#define WM8994_ADCR_DAC1_VOL_MASK               0x01E0  /* ADCR_DAC1_VOL - [8:5] */
#define WM8994_ADCR_DAC1_VOL_SHIFT                   5  /* ADCR_DAC1_VOL - [8:5] */
#define WM8994_ADCR_DAC1_VOL_WIDTH                   4  /* ADCR_DAC1_VOL - [8:5] */
#define WM8994_ADCL_DAC1_VOL_MASK               0x000F  /* ADCL_DAC1_VOL - [3:0] */
#define WM8994_ADCL_DAC1_VOL_SHIFT                   0  /* ADCL_DAC1_VOL - [3:0] */
#define WM8994_ADCL_DAC1_VOL_WIDTH                   4  /* ADCL_DAC1_VOL - [3:0] */

/*
 * R1537 (0x601) - DAC1 Left Mixer Routing
 */
#define WM8994_ADCR_TO_DAC1L                    0x0020  /* ADCR_TO_DAC1L */
#define WM8994_ADCR_TO_DAC1L_MASK               0x0020  /* ADCR_TO_DAC1L */
#define WM8994_ADCR_TO_DAC1L_SHIFT                   5  /* ADCR_TO_DAC1L */
#define WM8994_ADCR_TO_DAC1L_WIDTH                   1  /* ADCR_TO_DAC1L */
#define WM8994_ADCL_TO_DAC1L                    0x0010  /* ADCL_TO_DAC1L */
#define WM8994_ADCL_TO_DAC1L_MASK               0x0010  /* ADCL_TO_DAC1L */
#define WM8994_ADCL_TO_DAC1L_SHIFT                   4  /* ADCL_TO_DAC1L */
#define WM8994_ADCL_TO_DAC1L_WIDTH                   1  /* ADCL_TO_DAC1L */
#define WM8994_AIF2DACL_TO_DAC1L                0x0004  /* AIF2DACL_TO_DAC1L */
#define WM8994_AIF2DACL_TO_DAC1L_MASK           0x0004  /* AIF2DACL_TO_DAC1L */
#define WM8994_AIF2DACL_TO_DAC1L_SHIFT               2  /* AIF2DACL_TO_DAC1L */
#define WM8994_AIF2DACL_TO_DAC1L_WIDTH               1  /* AIF2DACL_TO_DAC1L */
#define WM8994_AIF1DAC2L_TO_DAC1L               0x0002  /* AIF1DAC2L_TO_DAC1L */
#define WM8994_AIF1DAC2L_TO_DAC1L_MASK          0x0002  /* AIF1DAC2L_TO_DAC1L */
#define WM8994_AIF1DAC2L_TO_DAC1L_SHIFT              1  /* AIF1DAC2L_TO_DAC1L */
#define WM8994_AIF1DAC2L_TO_DAC1L_WIDTH              1  /* AIF1DAC2L_TO_DAC1L */
#define WM8994_AIF1DAC1L_TO_DAC1L               0x0001  /* AIF1DAC1L_TO_DAC1L */
#define WM8994_AIF1DAC1L_TO_DAC1L_MASK          0x0001  /* AIF1DAC1L_TO_DAC1L */
#define WM8994_AIF1DAC1L_TO_DAC1L_SHIFT              0  /* AIF1DAC1L_TO_DAC1L */
#define WM8994_AIF1DAC1L_TO_DAC1L_WIDTH              1  /* AIF1DAC1L_TO_DAC1L */

/*
 * R1538 (0x602) - DAC1 Right Mixer Routing
 */
#define WM8994_ADCR_TO_DAC1R                    0x0020  /* ADCR_TO_DAC1R */
#define WM8994_ADCR_TO_DAC1R_MASK               0x0020  /* ADCR_TO_DAC1R */
#define WM8994_ADCR_TO_DAC1R_SHIFT                   5  /* ADCR_TO_DAC1R */
#define WM8994_ADCR_TO_DAC1R_WIDTH                   1  /* ADCR_TO_DAC1R */
#define WM8994_ADCL_TO_DAC1R                    0x0010  /* ADCL_TO_DAC1R */
#define WM8994_ADCL_TO_DAC1R_MASK               0x0010  /* ADCL_TO_DAC1R */
#define WM8994_ADCL_TO_DAC1R_SHIFT                   4  /* ADCL_TO_DAC1R */
#define WM8994_ADCL_TO_DAC1R_WIDTH                   1  /* ADCL_TO_DAC1R */
#define WM8994_AIF2DACR_TO_DAC1R                0x0004  /* AIF2DACR_TO_DAC1R */
#define WM8994_AIF2DACR_TO_DAC1R_MASK           0x0004  /* AIF2DACR_TO_DAC1R */
#define WM8994_AIF2DACR_TO_DAC1R_SHIFT               2  /* AIF2DACR_TO_DAC1R */
#define WM8994_AIF2DACR_TO_DAC1R_WIDTH               1  /* AIF2DACR_TO_DAC1R */
#define WM8994_AIF1DAC2R_TO_DAC1R               0x0002  /* AIF1DAC2R_TO_DAC1R */
#define WM8994_AIF1DAC2R_TO_DAC1R_MASK          0x0002  /* AIF1DAC2R_TO_DAC1R */
#define WM8994_AIF1DAC2R_TO_DAC1R_SHIFT              1  /* AIF1DAC2R_TO_DAC1R */
#define WM8994_AIF1DAC2R_TO_DAC1R_WIDTH              1  /* AIF1DAC2R_TO_DAC1R */
#define WM8994_AIF1DAC1R_TO_DAC1R               0x0001  /* AIF1DAC1R_TO_DAC1R */
#define WM8994_AIF1DAC1R_TO_DAC1R_MASK          0x0001  /* AIF1DAC1R_TO_DAC1R */
#define WM8994_AIF1DAC1R_TO_DAC1R_SHIFT              0  /* AIF1DAC1R_TO_DAC1R */
#define WM8994_AIF1DAC1R_TO_DAC1R_WIDTH              1  /* AIF1DAC1R_TO_DAC1R */

/*
 * R1539 (0x603) - DAC2 Mixer Volumes
 */
#define WM8994_ADCR_DAC2_VOL_MASK               0x01E0  /* ADCR_DAC2_VOL - [8:5] */
#define WM8994_ADCR_DAC2_VOL_SHIFT                   5  /* ADCR_DAC2_VOL - [8:5] */
#define WM8994_ADCR_DAC2_VOL_WIDTH                   4  /* ADCR_DAC2_VOL - [8:5] */
#define WM8994_ADCL_DAC2_VOL_MASK               0x000F  /* ADCL_DAC2_VOL - [3:0] */
#define WM8994_ADCL_DAC2_VOL_SHIFT                   0  /* ADCL_DAC2_VOL - [3:0] */
#define WM8994_ADCL_DAC2_VOL_WIDTH                   4  /* ADCL_DAC2_VOL - [3:0] */

/*
 * R1540 (0x604) - DAC2 Left Mixer Routing
 */
#define WM8994_ADCR_TO_DAC2L                    0x0020  /* ADCR_TO_DAC2L */
#define WM8994_ADCR_TO_DAC2L_MASK               0x0020  /* ADCR_TO_DAC2L */
#define WM8994_ADCR_TO_DAC2L_SHIFT                   5  /* ADCR_TO_DAC2L */
#define WM8994_ADCR_TO_DAC2L_WIDTH                   1  /* ADCR_TO_DAC2L */
#define WM8994_ADCL_TO_DAC2L                    0x0010  /* ADCL_TO_DAC2L */
#define WM8994_ADCL_TO_DAC2L_MASK               0x0010  /* ADCL_TO_DAC2L */
#define WM8994_ADCL_TO_DAC2L_SHIFT                   4  /* ADCL_TO_DAC2L */
#define WM8994_ADCL_TO_DAC2L_WIDTH                   1  /* ADCL_TO_DAC2L */
#define WM8994_AIF2DACL_TO_DAC2L                0x0004  /* AIF2DACL_TO_DAC2L */
#define WM8994_AIF2DACL_TO_DAC2L_MASK           0x0004  /* AIF2DACL_TO_DAC2L */
#define WM8994_AIF2DACL_TO_DAC2L_SHIFT               2  /* AIF2DACL_TO_DAC2L */
#define WM8994_AIF2DACL_TO_DAC2L_WIDTH               1  /* AIF2DACL_TO_DAC2L */
#define WM8994_AIF1DAC2L_TO_DAC2L               0x0002  /* AIF1DAC2L_TO_DAC2L */
#define WM8994_AIF1DAC2L_TO_DAC2L_MASK          0x0002  /* AIF1DAC2L_TO_DAC2L */
#define WM8994_AIF1DAC2L_TO_DAC2L_SHIFT              1  /* AIF1DAC2L_TO_DAC2L */
#define WM8994_AIF1DAC2L_TO_DAC2L_WIDTH              1  /* AIF1DAC2L_TO_DAC2L */
#define WM8994_AIF1DAC1L_TO_DAC2L               0x0001  /* AIF1DAC1L_TO_DAC2L */
#define WM8994_AIF1DAC1L_TO_DAC2L_MASK          0x0001  /* AIF1DAC1L_TO_DAC2L */
#define WM8994_AIF1DAC1L_TO_DAC2L_SHIFT              0  /* AIF1DAC1L_TO_DAC2L */
#define WM8994_AIF1DAC1L_TO_DAC2L_WIDTH              1  /* AIF1DAC1L_TO_DAC2L */

/*
 * R1541 (0x605) - DAC2 Right Mixer Routing
 */
#define WM8994_ADCR_TO_DAC2R                    0x0020  /* ADCR_TO_DAC2R */
#define WM8994_ADCR_TO_DAC2R_MASK               0x0020  /* ADCR_TO_DAC2R */
#define WM8994_ADCR_TO_DAC2R_SHIFT                   5  /* ADCR_TO_DAC2R */
#define WM8994_ADCR_TO_DAC2R_WIDTH                   1  /* ADCR_TO_DAC2R */
#define WM8994_ADCL_TO_DAC2R                    0x0010  /* ADCL_TO_DAC2R */
#define WM8994_ADCL_TO_DAC2R_MASK               0x0010  /* ADCL_TO_DAC2R */
#define WM8994_ADCL_TO_DAC2R_SHIFT                   4  /* ADCL_TO_DAC2R */
#define WM8994_ADCL_TO_DAC2R_WIDTH                   1  /* ADCL_TO_DAC2R */
#define WM8994_AIF2DACR_TO_DAC2R                0x0004  /* AIF2DACR_TO_DAC2R */
#define WM8994_AIF2DACR_TO_DAC2R_MASK           0x0004  /* AIF2DACR_TO_DAC2R */
#define WM8994_AIF2DACR_TO_DAC2R_SHIFT               2  /* AIF2DACR_TO_DAC2R */
#define WM8994_AIF2DACR_TO_DAC2R_WIDTH               1  /* AIF2DACR_TO_DAC2R */
#define WM8994_AIF1DAC2R_TO_DAC2R               0x0002  /* AIF1DAC2R_TO_DAC2R */
#define WM8994_AIF1DAC2R_TO_DAC2R_MASK          0x0002  /* AIF1DAC2R_TO_DAC2R */
#define WM8994_AIF1DAC2R_TO_DAC2R_SHIFT              1  /* AIF1DAC2R_TO_DAC2R */
#define WM8994_AIF1DAC2R_TO_DAC2R_WIDTH              1  /* AIF1DAC2R_TO_DAC2R */
#define WM8994_AIF1DAC1R_TO_DAC2R               0x0001  /* AIF1DAC1R_TO_DAC2R */
#define WM8994_AIF1DAC1R_TO_DAC2R_MASK          0x0001  /* AIF1DAC1R_TO_DAC2R */
#define WM8994_AIF1DAC1R_TO_DAC2R_SHIFT              0  /* AIF1DAC1R_TO_DAC2R */
#define WM8994_AIF1DAC1R_TO_DAC2R_WIDTH              1  /* AIF1DAC1R_TO_DAC2R */

/*
 * R1542 (0x606) - AIF1 ADC1 Left Mixer Routing
 */
#define WM8994_ADC1L_TO_AIF1ADC1L               0x0002  /* ADC1L_TO_AIF1ADC1L */
#define WM8994_ADC1L_TO_AIF1ADC1L_MASK          0x0002  /* ADC1L_TO_AIF1ADC1L */
#define WM8994_ADC1L_TO_AIF1ADC1L_SHIFT              1  /* ADC1L_TO_AIF1ADC1L */
#define WM8994_ADC1L_TO_AIF1ADC1L_WIDTH              1  /* ADC1L_TO_AIF1ADC1L */
#define WM8994_AIF2DACL_TO_AIF1ADC1L            0x0001  /* AIF2DACL_TO_AIF1ADC1L */
#define WM8994_AIF2DACL_TO_AIF1ADC1L_MASK       0x0001  /* AIF2DACL_TO_AIF1ADC1L */
#define WM8994_AIF2DACL_TO_AIF1ADC1L_SHIFT           0  /* AIF2DACL_TO_AIF1ADC1L */
#define WM8994_AIF2DACL_TO_AIF1ADC1L_WIDTH           1  /* AIF2DACL_TO_AIF1ADC1L */

/*
 * R1543 (0x607) - AIF1 ADC1 Right Mixer Routing
 */
#define WM8994_ADC1R_TO_AIF1ADC1R               0x0002  /* ADC1R_TO_AIF1ADC1R */
#define WM8994_ADC1R_TO_AIF1ADC1R_MASK          0x0002  /* ADC1R_TO_AIF1ADC1R */
#define WM8994_ADC1R_TO_AIF1ADC1R_SHIFT              1  /* ADC1R_TO_AIF1ADC1R */
#define WM8994_ADC1R_TO_AIF1ADC1R_WIDTH              1  /* ADC1R_TO_AIF1ADC1R */
#define WM8994_AIF2DACR_TO_AIF1ADC1R            0x0001  /* AIF2DACR_TO_AIF1ADC1R */
#define WM8994_AIF2DACR_TO_AIF1ADC1R_MASK       0x0001  /* AIF2DACR_TO_AIF1ADC1R */
#define WM8994_AIF2DACR_TO_AIF1ADC1R_SHIFT           0  /* AIF2DACR_TO_AIF1ADC1R */
#define WM8994_AIF2DACR_TO_AIF1ADC1R_WIDTH           1  /* AIF2DACR_TO_AIF1ADC1R */

/*
 * R1544 (0x608) - AIF1 ADC2 Left Mixer Routing
 */
#define WM8994_ADC2L_TO_AIF1ADC2L               0x0002  /* ADC2L_TO_AIF1ADC2L */
#define WM8994_ADC2L_TO_AIF1ADC2L_MASK          0x0002  /* ADC2L_TO_AIF1ADC2L */
#define WM8994_ADC2L_TO_AIF1ADC2L_SHIFT              1  /* ADC2L_TO_AIF1ADC2L */
#define WM8994_ADC2L_TO_AIF1ADC2L_WIDTH              1  /* ADC2L_TO_AIF1ADC2L */
#define WM8994_AIF2DACL_TO_AIF1ADC2L            0x0001  /* AIF2DACL_TO_AIF1ADC2L */
#define WM8994_AIF2DACL_TO_AIF1ADC2L_MASK       0x0001  /* AIF2DACL_TO_AIF1ADC2L */
#define WM8994_AIF2DACL_TO_AIF1ADC2L_SHIFT           0  /* AIF2DACL_TO_AIF1ADC2L */
#define WM8994_AIF2DACL_TO_AIF1ADC2L_WIDTH           1  /* AIF2DACL_TO_AIF1ADC2L */

/*
 * R1545 (0x609) - AIF1 ADC2 Right mixer Routing
 */
#define WM8994_ADC2R_TO_AIF1ADC2R               0x0002  /* ADC2R_TO_AIF1ADC2R */
#define WM8994_ADC2R_TO_AIF1ADC2R_MASK          0x0002  /* ADC2R_TO_AIF1ADC2R */
#define WM8994_ADC2R_TO_AIF1ADC2R_SHIFT              1  /* ADC2R_TO_AIF1ADC2R */
#define WM8994_ADC2R_TO_AIF1ADC2R_WIDTH              1  /* ADC2R_TO_AIF1ADC2R */
#define WM8994_AIF2DACR_TO_AIF1ADC2R            0x0001  /* AIF2DACR_TO_AIF1ADC2R */
#define WM8994_AIF2DACR_TO_AIF1ADC2R_MASK       0x0001  /* AIF2DACR_TO_AIF1ADC2R */
#define WM8994_AIF2DACR_TO_AIF1ADC2R_SHIFT           0  /* AIF2DACR_TO_AIF1ADC2R */
#define WM8994_AIF2DACR_TO_AIF1ADC2R_WIDTH           1  /* AIF2DACR_TO_AIF1ADC2R */

/*
 * R1552 (0x610) - DAC1 Left Volume
 */
#define WM8994_DAC1L_MUTE                       0x0200  /* DAC1L_MUTE */
#define WM8994_DAC1L_MUTE_MASK                  0x0200  /* DAC1L_MUTE */
#define WM8994_DAC1L_MUTE_SHIFT                      9  /* DAC1L_MUTE */
#define WM8994_DAC1L_MUTE_WIDTH                      1  /* DAC1L_MUTE */
#define WM8994_DAC1_VU                          0x0100  /* DAC1_VU */
#define WM8994_DAC1_VU_MASK                     0x0100  /* DAC1_VU */
#define WM8994_DAC1_VU_SHIFT                         8  /* DAC1_VU */
#define WM8994_DAC1_VU_WIDTH                         1  /* DAC1_VU */
#define WM8994_DAC1L_VOL_MASK                   0x00FF  /* DAC1L_VOL - [7:0] */
#define WM8994_DAC1L_VOL_SHIFT                       0  /* DAC1L_VOL - [7:0] */
#define WM8994_DAC1L_VOL_WIDTH                       8  /* DAC1L_VOL - [7:0] */

/*
 * R1553 (0x611) - DAC1 Right Volume
 */
#define WM8994_DAC1R_MUTE                       0x0200  /* DAC1R_MUTE */
#define WM8994_DAC1R_MUTE_MASK                  0x0200  /* DAC1R_MUTE */
#define WM8994_DAC1R_MUTE_SHIFT                      9  /* DAC1R_MUTE */
#define WM8994_DAC1R_MUTE_WIDTH                      1  /* DAC1R_MUTE */
#define WM8994_DAC1_VU                          0x0100  /* DAC1_VU */
#define WM8994_DAC1_VU_MASK                     0x0100  /* DAC1_VU */
#define WM8994_DAC1_VU_SHIFT                         8  /* DAC1_VU */
#define WM8994_DAC1_VU_WIDTH                         1  /* DAC1_VU */
#define WM8994_DAC1R_VOL_MASK                   0x00FF  /* DAC1R_VOL - [7:0] */
#define WM8994_DAC1R_VOL_SHIFT                       0  /* DAC1R_VOL - [7:0] */
#define WM8994_DAC1R_VOL_WIDTH                       8  /* DAC1R_VOL - [7:0] */

/*
 * R1554 (0x612) - DAC2 Left Volume
 */
#define WM8994_DAC2L_MUTE                       0x0200  /* DAC2L_MUTE */
#define WM8994_DAC2L_MUTE_MASK                  0x0200  /* DAC2L_MUTE */
#define WM8994_DAC2L_MUTE_SHIFT                      9  /* DAC2L_MUTE */
#define WM8994_DAC2L_MUTE_WIDTH                      1  /* DAC2L_MUTE */
#define WM8994_DAC2_VU                          0x0100  /* DAC2_VU */
#define WM8994_DAC2_VU_MASK                     0x0100  /* DAC2_VU */
#define WM8994_DAC2_VU_SHIFT                         8  /* DAC2_VU */
#define WM8994_DAC2_VU_WIDTH                         1  /* DAC2_VU */
#define WM8994_DAC2L_VOL_MASK                   0x00FF  /* DAC2L_VOL - [7:0] */
#define WM8994_DAC2L_VOL_SHIFT                       0  /* DAC2L_VOL - [7:0] */
#define WM8994_DAC2L_VOL_WIDTH                       8  /* DAC2L_VOL - [7:0] */

/*
 * R1555 (0x613) - DAC2 Right Volume
 */
#define WM8994_DAC2R_MUTE                       0x0200  /* DAC2R_MUTE */
#define WM8994_DAC2R_MUTE_MASK                  0x0200  /* DAC2R_MUTE */
#define WM8994_DAC2R_MUTE_SHIFT                      9  /* DAC2R_MUTE */
#define WM8994_DAC2R_MUTE_WIDTH                      1  /* DAC2R_MUTE */
#define WM8994_DAC2_VU                          0x0100  /* DAC2_VU */
#define WM8994_DAC2_VU_MASK                     0x0100  /* DAC2_VU */
#define WM8994_DAC2_VU_SHIFT                         8  /* DAC2_VU */
#define WM8994_DAC2_VU_WIDTH                         1  /* DAC2_VU */
#define WM8994_DAC2R_VOL_MASK                   0x00FF  /* DAC2R_VOL - [7:0] */
#define WM8994_DAC2R_VOL_SHIFT                       0  /* DAC2R_VOL - [7:0] */
#define WM8994_DAC2R_VOL_WIDTH                       8  /* DAC2R_VOL - [7:0] */

/*
 * R1556 (0x614) - DAC Softmute
 */
#define WM8994_DAC_SOFTMUTEMODE                 0x0002  /* DAC_SOFTMUTEMODE */
#define WM8994_DAC_SOFTMUTEMODE_MASK            0x0002  /* DAC_SOFTMUTEMODE */
#define WM8994_DAC_SOFTMUTEMODE_SHIFT                1  /* DAC_SOFTMUTEMODE */
#define WM8994_DAC_SOFTMUTEMODE_WIDTH                1  /* DAC_SOFTMUTEMODE */
#define WM8994_DAC_MUTERATE                     0x0001  /* DAC_MUTERATE */
#define WM8994_DAC_MUTERATE_MASK                0x0001  /* DAC_MUTERATE */
#define WM8994_DAC_MUTERATE_SHIFT                    0  /* DAC_MUTERATE */
#define WM8994_DAC_MUTERATE_WIDTH                    1  /* DAC_MUTERATE */

/*
 * R1568 (0x620) - Oversampling
 */
#define WM8994_ADC_OSR128                       0x0002  /* ADC_OSR128 */
#define WM8994_ADC_OSR128_MASK                  0x0002  /* ADC_OSR128 */
#define WM8994_ADC_OSR128_SHIFT                      1  /* ADC_OSR128 */
#define WM8994_ADC_OSR128_WIDTH                      1  /* ADC_OSR128 */
#define WM8994_DAC_OSR128                       0x0001  /* DAC_OSR128 */
#define WM8994_DAC_OSR128_MASK                  0x0001  /* DAC_OSR128 */
#define WM8994_DAC_OSR128_SHIFT                      0  /* DAC_OSR128 */
#define WM8994_DAC_OSR128_WIDTH                      1  /* DAC_OSR128 */

/*
 * R1569 (0x621) - Sidetone
 */
#define WM8994_ST_HPF_CUT_MASK                  0x0380  /* ST_HPF_CUT - [9:7] */
#define WM8994_ST_HPF_CUT_SHIFT                      7  /* ST_HPF_CUT - [9:7] */
#define WM8994_ST_HPF_CUT_WIDTH                      3  /* ST_HPF_CUT - [9:7] */
#define WM8994_ST_HPF                           0x0040  /* ST_HPF */
#define WM8994_ST_HPF_MASK                      0x0040  /* ST_HPF */
#define WM8994_ST_HPF_SHIFT                          6  /* ST_HPF */
#define WM8994_ST_HPF_WIDTH                          1  /* ST_HPF */
#define WM8994_STR_SEL                          0x0002  /* STR_SEL */
#define WM8994_STR_SEL_MASK                     0x0002  /* STR_SEL */
#define WM8994_STR_SEL_SHIFT                         1  /* STR_SEL */
#define WM8994_STR_SEL_WIDTH                         1  /* STR_SEL */
#define WM8994_STL_SEL                          0x0001  /* STL_SEL */
#define WM8994_STL_SEL_MASK                     0x0001  /* STL_SEL */
#define WM8994_STL_SEL_SHIFT                         0  /* STL_SEL */
#define WM8994_STL_SEL_WIDTH                         1  /* STL_SEL */

/*
 * R1824 (0x720) - Pull Control (1)
 */
#define WM8994_DMICDAT2_PU                      0x0800  /* DMICDAT2_PU */
#define WM8994_DMICDAT2_PU_MASK                 0x0800  /* DMICDAT2_PU */
#define WM8994_DMICDAT2_PU_SHIFT                    11  /* DMICDAT2_PU */
#define WM8994_DMICDAT2_PU_WIDTH                     1  /* DMICDAT2_PU */
#define WM8994_DMICDAT2_PD                      0x0400  /* DMICDAT2_PD */
#define WM8994_DMICDAT2_PD_MASK                 0x0400  /* DMICDAT2_PD */
#define WM8994_DMICDAT2_PD_SHIFT                    10  /* DMICDAT2_PD */
#define WM8994_DMICDAT2_PD_WIDTH                     1  /* DMICDAT2_PD */
#define WM8994_DMICDAT1_PU                      0x0200  /* DMICDAT1_PU */
#define WM8994_DMICDAT1_PU_MASK                 0x0200  /* DMICDAT1_PU */
#define WM8994_DMICDAT1_PU_SHIFT                     9  /* DMICDAT1_PU */
#define WM8994_DMICDAT1_PU_WIDTH                     1  /* DMICDAT1_PU */
#define WM8994_DMICDAT1_PD                      0x0100  /* DMICDAT1_PD */
#define WM8994_DMICDAT1_PD_MASK                 0x0100  /* DMICDAT1_PD */
#define WM8994_DMICDAT1_PD_SHIFT                     8  /* DMICDAT1_PD */
#define WM8994_DMICDAT1_PD_WIDTH                     1  /* DMICDAT1_PD */
#define WM8994_MCLK1_PU                         0x0080  /* MCLK1_PU */
#define WM8994_MCLK1_PU_MASK                    0x0080  /* MCLK1_PU */
#define WM8994_MCLK1_PU_SHIFT                        7  /* MCLK1_PU */
#define WM8994_MCLK1_PU_WIDTH                        1  /* MCLK1_PU */
#define WM8994_MCLK1_PD                         0x0040  /* MCLK1_PD */
#define WM8994_MCLK1_PD_MASK                    0x0040  /* MCLK1_PD */
#define WM8994_MCLK1_PD_SHIFT                        6  /* MCLK1_PD */
#define WM8994_MCLK1_PD_WIDTH                        1  /* MCLK1_PD */
#define WM8994_DACDAT1_PU                       0x0020  /* DACDAT1_PU */
#define WM8994_DACDAT1_PU_MASK                  0x0020  /* DACDAT1_PU */
#define WM8994_DACDAT1_PU_SHIFT                      5  /* DACDAT1_PU */
#define WM8994_DACDAT1_PU_WIDTH                      1  /* DACDAT1_PU */
#define WM8994_DACDAT1_PD                       0x0010  /* DACDAT1_PD */
#define WM8994_DACDAT1_PD_MASK                  0x0010  /* DACDAT1_PD */
#define WM8994_DACDAT1_PD_SHIFT                      4  /* DACDAT1_PD */
#define WM8994_DACDAT1_PD_WIDTH                      1  /* DACDAT1_PD */
#define WM8994_DACLRCLK1_PU                     0x0008  /* DACLRCLK1_PU */
#define WM8994_DACLRCLK1_PU_MASK                0x0008  /* DACLRCLK1_PU */
#define WM8994_DACLRCLK1_PU_SHIFT                    3  /* DACLRCLK1_PU */
#define WM8994_DACLRCLK1_PU_WIDTH                    1  /* DACLRCLK1_PU */
#define WM8994_DACLRCLK1_PD                     0x0004  /* DACLRCLK1_PD */
#define WM8994_DACLRCLK1_PD_MASK                0x0004  /* DACLRCLK1_PD */
#define WM8994_DACLRCLK1_PD_SHIFT                    2  /* DACLRCLK1_PD */
#define WM8994_DACLRCLK1_PD_WIDTH                    1  /* DACLRCLK1_PD */
#define WM8994_BCLK1_PU                         0x0002  /* BCLK1_PU */
#define WM8994_BCLK1_PU_MASK                    0x0002  /* BCLK1_PU */
#define WM8994_BCLK1_PU_SHIFT                        1  /* BCLK1_PU */
#define WM8994_BCLK1_PU_WIDTH                        1  /* BCLK1_PU */
#define WM8994_BCLK1_PD                         0x0001  /* BCLK1_PD */
#define WM8994_BCLK1_PD_MASK                    0x0001  /* BCLK1_PD */
#define WM8994_BCLK1_PD_SHIFT                        0  /* BCLK1_PD */
#define WM8994_BCLK1_PD_WIDTH                        1  /* BCLK1_PD */

/*
 * R1825 (0x721) - Pull Control (2)
 */
#define WM8994_CSNADDR_PD                       0x0100  /* CSNADDR_PD */
#define WM8994_CSNADDR_PD_MASK                  0x0100  /* CSNADDR_PD */
#define WM8994_CSNADDR_PD_SHIFT                      8  /* CSNADDR_PD */
#define WM8994_CSNADDR_PD_WIDTH                      1  /* CSNADDR_PD */
#define WM8994_LDO2ENA_PD                       0x0040  /* LDO2ENA_PD */
#define WM8994_LDO2ENA_PD_MASK                  0x0040  /* LDO2ENA_PD */
#define WM8994_LDO2ENA_PD_SHIFT                      6  /* LDO2ENA_PD */
#define WM8994_LDO2ENA_PD_WIDTH                      1  /* LDO2ENA_PD */
#define WM8994_LDO1ENA_PD                       0x0010  /* LDO1ENA_PD */
#define WM8994_LDO1ENA_PD_MASK                  0x0010  /* LDO1ENA_PD */
#define WM8994_LDO1ENA_PD_SHIFT                      4  /* LDO1ENA_PD */
#define WM8994_LDO1ENA_PD_WIDTH                      1  /* LDO1ENA_PD */
#define WM8994_CIFMODE_PD                       0x0004  /* CIFMODE_PD */
#define WM8994_CIFMODE_PD_MASK                  0x0004  /* CIFMODE_PD */
#define WM8994_CIFMODE_PD_SHIFT                      2  /* CIFMODE_PD */
#define WM8994_CIFMODE_PD_WIDTH                      1  /* CIFMODE_PD */
#define WM8994_SPKMODE_PU                       0x0002  /* SPKMODE_PU */
#define WM8994_SPKMODE_PU_MASK                  0x0002  /* SPKMODE_PU */
#define WM8994_SPKMODE_PU_SHIFT                      1  /* SPKMODE_PU */
#define WM8994_SPKMODE_PU_WIDTH                      1  /* SPKMODE_PU */

/*
 * R1840 (0x730) - Interrupt Status 1
 */
#define WM8994_GP11_EINT                        0x0400  /* GP11_EINT */
#define WM8994_GP11_EINT_MASK                   0x0400  /* GP11_EINT */
#define WM8994_GP11_EINT_SHIFT                      10  /* GP11_EINT */
#define WM8994_GP11_EINT_WIDTH                       1  /* GP11_EINT */
#define WM8994_GP10_EINT                        0x0200  /* GP10_EINT */
#define WM8994_GP10_EINT_MASK                   0x0200  /* GP10_EINT */
#define WM8994_GP10_EINT_SHIFT                       9  /* GP10_EINT */
#define WM8994_GP10_EINT_WIDTH                       1  /* GP10_EINT */
#define WM8994_GP9_EINT                         0x0100  /* GP9_EINT */
#define WM8994_GP9_EINT_MASK                    0x0100  /* GP9_EINT */
#define WM8994_GP9_EINT_SHIFT                        8  /* GP9_EINT */
#define WM8994_GP9_EINT_WIDTH                        1  /* GP9_EINT */
#define WM8994_GP8_EINT                         0x0080  /* GP8_EINT */
#define WM8994_GP8_EINT_MASK                    0x0080  /* GP8_EINT */
#define WM8994_GP8_EINT_SHIFT                        7  /* GP8_EINT */
#define WM8994_GP8_EINT_WIDTH                        1  /* GP8_EINT */
#define WM8994_GP7_EINT                         0x0040  /* GP7_EINT */
#define WM8994_GP7_EINT_MASK                    0x0040  /* GP7_EINT */
#define WM8994_GP7_EINT_SHIFT                        6  /* GP7_EINT */
#define WM8994_GP7_EINT_WIDTH                        1  /* GP7_EINT */
#define WM8994_GP6_EINT                         0x0020  /* GP6_EINT */
#define WM8994_GP6_EINT_MASK                    0x0020  /* GP6_EINT */
#define WM8994_GP6_EINT_SHIFT                        5  /* GP6_EINT */
#define WM8994_GP6_EINT_WIDTH                        1  /* GP6_EINT */
#define WM8994_GP5_EINT                         0x0010  /* GP5_EINT */
#define WM8994_GP5_EINT_MASK                    0x0010  /* GP5_EINT */
#define WM8994_GP5_EINT_SHIFT                        4  /* GP5_EINT */
#define WM8994_GP5_EINT_WIDTH                        1  /* GP5_EINT */
#define WM8994_GP4_EINT                         0x0008  /* GP4_EINT */
#define WM8994_GP4_EINT_MASK                    0x0008  /* GP4_EINT */
#define WM8994_GP4_EINT_SHIFT                        3  /* GP4_EINT */
#define WM8994_GP4_EINT_WIDTH                        1  /* GP4_EINT */
#define WM8994_GP3_EINT                         0x0004  /* GP3_EINT */
#define WM8994_GP3_EINT_MASK                    0x0004  /* GP3_EINT */
#define WM8994_GP3_EINT_SHIFT                        2  /* GP3_EINT */
#define WM8994_GP3_EINT_WIDTH                        1  /* GP3_EINT */
#define WM8994_GP2_EINT                         0x0002  /* GP2_EINT */
#define WM8994_GP2_EINT_MASK                    0x0002  /* GP2_EINT */
#define WM8994_GP2_EINT_SHIFT                        1  /* GP2_EINT */
#define WM8994_GP2_EINT_WIDTH                        1  /* GP2_EINT */
#define WM8994_GP1_EINT                         0x0001  /* GP1_EINT */
#define WM8994_GP1_EINT_MASK                    0x0001  /* GP1_EINT */
#define WM8994_GP1_EINT_SHIFT                        0  /* GP1_EINT */
#define WM8994_GP1_EINT_WIDTH                        1  /* GP1_EINT */

/*
 * R1841 (0x731) - Interrupt Status 2
 */
#define WM8994_TEMP_WARN_EINT                   0x8000  /* TEMP_WARN_EINT */
#define WM8994_TEMP_WARN_EINT_MASK              0x8000  /* TEMP_WARN_EINT */
#define WM8994_TEMP_WARN_EINT_SHIFT                 15  /* TEMP_WARN_EINT */
#define WM8994_TEMP_WARN_EINT_WIDTH                  1  /* TEMP_WARN_EINT */
#define WM8994_DCS_DONE_EINT                    0x4000  /* DCS_DONE_EINT */
#define WM8994_DCS_DONE_EINT_MASK               0x4000  /* DCS_DONE_EINT */
#define WM8994_DCS_DONE_EINT_SHIFT                  14  /* DCS_DONE_EINT */
#define WM8994_DCS_DONE_EINT_WIDTH                   1  /* DCS_DONE_EINT */
#define WM8994_WSEQ_DONE_EINT                   0x2000  /* WSEQ_DONE_EINT */
#define WM8994_WSEQ_DONE_EINT_MASK              0x2000  /* WSEQ_DONE_EINT */
#define WM8994_WSEQ_DONE_EINT_SHIFT                 13  /* WSEQ_DONE_EINT */
#define WM8994_WSEQ_DONE_EINT_WIDTH                  1  /* WSEQ_DONE_EINT */
#define WM8994_FIFOS_ERR_EINT                   0x1000  /* FIFOS_ERR_EINT */
#define WM8994_FIFOS_ERR_EINT_MASK              0x1000  /* FIFOS_ERR_EINT */
#define WM8994_FIFOS_ERR_EINT_SHIFT                 12  /* FIFOS_ERR_EINT */
#define WM8994_FIFOS_ERR_EINT_WIDTH                  1  /* FIFOS_ERR_EINT */
#define WM8994_AIF2DRC_SIG_DET_EINT             0x0800  /* AIF2DRC_SIG_DET_EINT */
#define WM8994_AIF2DRC_SIG_DET_EINT_MASK        0x0800  /* AIF2DRC_SIG_DET_EINT */
#define WM8994_AIF2DRC_SIG_DET_EINT_SHIFT           11  /* AIF2DRC_SIG_DET_EINT */
#define WM8994_AIF2DRC_SIG_DET_EINT_WIDTH            1  /* AIF2DRC_SIG_DET_EINT */
#define WM8994_AIF1DRC2_SIG_DET_EINT            0x0400  /* AIF1DRC2_SIG_DET_EINT */
#define WM8994_AIF1DRC2_SIG_DET_EINT_MASK       0x0400  /* AIF1DRC2_SIG_DET_EINT */
#define WM8994_AIF1DRC2_SIG_DET_EINT_SHIFT          10  /* AIF1DRC2_SIG_DET_EINT */
#define WM8994_AIF1DRC2_SIG_DET_EINT_WIDTH           1  /* AIF1DRC2_SIG_DET_EINT */
#define WM8994_AIF1DRC1_SIG_DET_EINT            0x0200  /* AIF1DRC1_SIG_DET_EINT */
#define WM8994_AIF1DRC1_SIG_DET_EINT_MASK       0x0200  /* AIF1DRC1_SIG_DET_EINT */
#define WM8994_AIF1DRC1_SIG_DET_EINT_SHIFT           9  /* AIF1DRC1_SIG_DET_EINT */
#define WM8994_AIF1DRC1_SIG_DET_EINT_WIDTH           1  /* AIF1DRC1_SIG_DET_EINT */
#define WM8994_SRC2_LOCK_EINT                   0x0100  /* SRC2_LOCK_EINT */
#define WM8994_SRC2_LOCK_EINT_MASK              0x0100  /* SRC2_LOCK_EINT */
#define WM8994_SRC2_LOCK_EINT_SHIFT                  8  /* SRC2_LOCK_EINT */
#define WM8994_SRC2_LOCK_EINT_WIDTH                  1  /* SRC2_LOCK_EINT */
#define WM8994_SRC1_LOCK_EINT                   0x0080  /* SRC1_LOCK_EINT */
#define WM8994_SRC1_LOCK_EINT_MASK              0x0080  /* SRC1_LOCK_EINT */
#define WM8994_SRC1_LOCK_EINT_SHIFT                  7  /* SRC1_LOCK_EINT */
#define WM8994_SRC1_LOCK_EINT_WIDTH                  1  /* SRC1_LOCK_EINT */
#define WM8994_FLL2_LOCK_EINT                   0x0040  /* FLL2_LOCK_EINT */
#define WM8994_FLL2_LOCK_EINT_MASK              0x0040  /* FLL2_LOCK_EINT */
#define WM8994_FLL2_LOCK_EINT_SHIFT                  6  /* FLL2_LOCK_EINT */
#define WM8994_FLL2_LOCK_EINT_WIDTH                  1  /* FLL2_LOCK_EINT */
#define WM8994_FLL1_LOCK_EINT                   0x0020  /* FLL1_LOCK_EINT */
#define WM8994_FLL1_LOCK_EINT_MASK              0x0020  /* FLL1_LOCK_EINT */
#define WM8994_FLL1_LOCK_EINT_SHIFT                  5  /* FLL1_LOCK_EINT */
#define WM8994_FLL1_LOCK_EINT_WIDTH                  1  /* FLL1_LOCK_EINT */
#define WM8994_MIC2_SHRT_EINT                   0x0010  /* MIC2_SHRT_EINT */
#define WM8994_MIC2_SHRT_EINT_MASK              0x0010  /* MIC2_SHRT_EINT */
#define WM8994_MIC2_SHRT_EINT_SHIFT                  4  /* MIC2_SHRT_EINT */
#define WM8994_MIC2_SHRT_EINT_WIDTH                  1  /* MIC2_SHRT_EINT */
#define WM8994_MIC2_DET_EINT                    0x0008  /* MIC2_DET_EINT */
#define WM8994_MIC2_DET_EINT_MASK               0x0008  /* MIC2_DET_EINT */
#define WM8994_MIC2_DET_EINT_SHIFT                   3  /* MIC2_DET_EINT */
#define WM8994_MIC2_DET_EINT_WIDTH                   1  /* MIC2_DET_EINT */
#define WM8994_MIC1_SHRT_EINT                   0x0004  /* MIC1_SHRT_EINT */
#define WM8994_MIC1_SHRT_EINT_MASK              0x0004  /* MIC1_SHRT_EINT */
#define WM8994_MIC1_SHRT_EINT_SHIFT                  2  /* MIC1_SHRT_EINT */
#define WM8994_MIC1_SHRT_EINT_WIDTH                  1  /* MIC1_SHRT_EINT */
#define WM8994_MIC1_DET_EINT                    0x0002  /* MIC1_DET_EINT */
#define WM8994_MIC1_DET_EINT_MASK               0x0002  /* MIC1_DET_EINT */
#define WM8994_MIC1_DET_EINT_SHIFT                   1  /* MIC1_DET_EINT */
#define WM8994_MIC1_DET_EINT_WIDTH                   1  /* MIC1_DET_EINT */
#define WM8994_TEMP_SHUT_EINT                   0x0001  /* TEMP_SHUT_EINT */
#define WM8994_TEMP_SHUT_EINT_MASK              0x0001  /* TEMP_SHUT_EINT */
#define WM8994_TEMP_SHUT_EINT_SHIFT                  0  /* TEMP_SHUT_EINT */
#define WM8994_TEMP_SHUT_EINT_WIDTH                  1  /* TEMP_SHUT_EINT */

/*
 * R1842 (0x732) - Interrupt Raw Status 2
 */
#define WM8994_TEMP_WARN_STS                    0x8000  /* TEMP_WARN_STS */
#define WM8994_TEMP_WARN_STS_MASK               0x8000  /* TEMP_WARN_STS */
#define WM8994_TEMP_WARN_STS_SHIFT                  15  /* TEMP_WARN_STS */
#define WM8994_TEMP_WARN_STS_WIDTH                   1  /* TEMP_WARN_STS */
#define WM8994_DCS_DONE_STS                     0x4000  /* DCS_DONE_STS */
#define WM8994_DCS_DONE_STS_MASK                0x4000  /* DCS_DONE_STS */
#define WM8994_DCS_DONE_STS_SHIFT                   14  /* DCS_DONE_STS */
#define WM8994_DCS_DONE_STS_WIDTH                    1  /* DCS_DONE_STS */
#define WM8994_WSEQ_DONE_STS                    0x2000  /* WSEQ_DONE_STS */
#define WM8994_WSEQ_DONE_STS_MASK               0x2000  /* WSEQ_DONE_STS */
#define WM8994_WSEQ_DONE_STS_SHIFT                  13  /* WSEQ_DONE_STS */
#define WM8994_WSEQ_DONE_STS_WIDTH                   1  /* WSEQ_DONE_STS */
#define WM8994_FIFOS_ERR_STS                    0x1000  /* FIFOS_ERR_STS */
#define WM8994_FIFOS_ERR_STS_MASK               0x1000  /* FIFOS_ERR_STS */
#define WM8994_FIFOS_ERR_STS_SHIFT                  12  /* FIFOS_ERR_STS */
#define WM8994_FIFOS_ERR_STS_WIDTH                   1  /* FIFOS_ERR_STS */
#define WM8994_AIF2DRC_SIG_DET_STS              0x0800  /* AIF2DRC_SIG_DET_STS */
#define WM8994_AIF2DRC_SIG_DET_STS_MASK         0x0800  /* AIF2DRC_SIG_DET_STS */
#define WM8994_AIF2DRC_SIG_DET_STS_SHIFT            11  /* AIF2DRC_SIG_DET_STS */
#define WM8994_AIF2DRC_SIG_DET_STS_WIDTH             1  /* AIF2DRC_SIG_DET_STS */
#define WM8994_AIF1DRC2_SIG_DET_STS             0x0400  /* AIF1DRC2_SIG_DET_STS */
#define WM8994_AIF1DRC2_SIG_DET_STS_MASK        0x0400  /* AIF1DRC2_SIG_DET_STS */
#define WM8994_AIF1DRC2_SIG_DET_STS_SHIFT           10  /* AIF1DRC2_SIG_DET_STS */
#define WM8994_AIF1DRC2_SIG_DET_STS_WIDTH            1  /* AIF1DRC2_SIG_DET_STS */
#define WM8994_AIF1DRC1_SIG_DET_STS             0x0200  /* AIF1DRC1_SIG_DET_STS */
#define WM8994_AIF1DRC1_SIG_DET_STS_MASK        0x0200  /* AIF1DRC1_SIG_DET_STS */
#define WM8994_AIF1DRC1_SIG_DET_STS_SHIFT            9  /* AIF1DRC1_SIG_DET_STS */
#define WM8994_AIF1DRC1_SIG_DET_STS_WIDTH            1  /* AIF1DRC1_SIG_DET_STS */
#define WM8994_SRC2_LOCK_STS                    0x0100  /* SRC2_LOCK_STS */
#define WM8994_SRC2_LOCK_STS_MASK               0x0100  /* SRC2_LOCK_STS */
#define WM8994_SRC2_LOCK_STS_SHIFT                   8  /* SRC2_LOCK_STS */
#define WM8994_SRC2_LOCK_STS_WIDTH                   1  /* SRC2_LOCK_STS */
#define WM8994_SRC1_LOCK_STS                    0x0080  /* SRC1_LOCK_STS */
#define WM8994_SRC1_LOCK_STS_MASK               0x0080  /* SRC1_LOCK_STS */
#define WM8994_SRC1_LOCK_STS_SHIFT                   7  /* SRC1_LOCK_STS */
#define WM8994_SRC1_LOCK_STS_WIDTH                   1  /* SRC1_LOCK_STS */
#define WM8994_FLL2_LOCK_STS                    0x0040  /* FLL2_LOCK_STS */
#define WM8994_FLL2_LOCK_STS_MASK               0x0040  /* FLL2_LOCK_STS */
#define WM8994_FLL2_LOCK_STS_SHIFT                   6  /* FLL2_LOCK_STS */
#define WM8994_FLL2_LOCK_STS_WIDTH                   1  /* FLL2_LOCK_STS */
#define WM8994_FLL1_LOCK_STS                    0x0020  /* FLL1_LOCK_STS */
#define WM8994_FLL1_LOCK_STS_MASK               0x0020  /* FLL1_LOCK_STS */
#define WM8994_FLL1_LOCK_STS_SHIFT                   5  /* FLL1_LOCK_STS */
#define WM8994_FLL1_LOCK_STS_WIDTH                   1  /* FLL1_LOCK_STS */
#define WM8994_MIC2_SHRT_STS                    0x0010  /* MIC2_SHRT_STS */
#define WM8994_MIC2_SHRT_STS_MASK               0x0010  /* MIC2_SHRT_STS */
#define WM8994_MIC2_SHRT_STS_SHIFT                   4  /* MIC2_SHRT_STS */
#define WM8994_MIC2_SHRT_STS_WIDTH                   1  /* MIC2_SHRT_STS */
#define WM8994_MIC2_DET_STS                     0x0008  /* MIC2_DET_STS */
#define WM8994_MIC2_DET_STS_MASK                0x0008  /* MIC2_DET_STS */
#define WM8994_MIC2_DET_STS_SHIFT                    3  /* MIC2_DET_STS */
#define WM8994_MIC2_DET_STS_WIDTH                    1  /* MIC2_DET_STS */
#define WM8994_MIC1_SHRT_STS                    0x0004  /* MIC1_SHRT_STS */
#define WM8994_MIC1_SHRT_STS_MASK               0x0004  /* MIC1_SHRT_STS */
#define WM8994_MIC1_SHRT_STS_SHIFT                   2  /* MIC1_SHRT_STS */
#define WM8994_MIC1_SHRT_STS_WIDTH                   1  /* MIC1_SHRT_STS */
#define WM8994_MIC1_DET_STS                     0x0002  /* MIC1_DET_STS */
#define WM8994_MIC1_DET_STS_MASK                0x0002  /* MIC1_DET_STS */
#define WM8994_MIC1_DET_STS_SHIFT                    1  /* MIC1_DET_STS */
#define WM8994_MIC1_DET_STS_WIDTH                    1  /* MIC1_DET_STS */
#define WM8994_TEMP_SHUT_STS                    0x0001  /* TEMP_SHUT_STS */
#define WM8994_TEMP_SHUT_STS_MASK               0x0001  /* TEMP_SHUT_STS */
#define WM8994_TEMP_SHUT_STS_SHIFT                   0  /* TEMP_SHUT_STS */
#define WM8994_TEMP_SHUT_STS_WIDTH                   1  /* TEMP_SHUT_STS */

/*
 * R1848 (0x738) - Interrupt Status 1 Mask
 */
#define WM8994_IM_GP11_EINT                     0x0400  /* IM_GP11_EINT */
#define WM8994_IM_GP11_EINT_MASK                0x0400  /* IM_GP11_EINT */
#define WM8994_IM_GP11_EINT_SHIFT                   10  /* IM_GP11_EINT */
#define WM8994_IM_GP11_EINT_WIDTH                    1  /* IM_GP11_EINT */
#define WM8994_IM_GP10_EINT                     0x0200  /* IM_GP10_EINT */
#define WM8994_IM_GP10_EINT_MASK                0x0200  /* IM_GP10_EINT */
#define WM8994_IM_GP10_EINT_SHIFT                    9  /* IM_GP10_EINT */
#define WM8994_IM_GP10_EINT_WIDTH                    1  /* IM_GP10_EINT */
#define WM8994_IM_GP9_EINT                      0x0100  /* IM_GP9_EINT */
#define WM8994_IM_GP9_EINT_MASK                 0x0100  /* IM_GP9_EINT */
#define WM8994_IM_GP9_EINT_SHIFT                     8  /* IM_GP9_EINT */
#define WM8994_IM_GP9_EINT_WIDTH                     1  /* IM_GP9_EINT */
#define WM8994_IM_GP8_EINT                      0x0080  /* IM_GP8_EINT */
#define WM8994_IM_GP8_EINT_MASK                 0x0080  /* IM_GP8_EINT */
#define WM8994_IM_GP8_EINT_SHIFT                     7  /* IM_GP8_EINT */
#define WM8994_IM_GP8_EINT_WIDTH                     1  /* IM_GP8_EINT */
#define WM8994_IM_GP7_EINT                      0x0040  /* IM_GP7_EINT */
#define WM8994_IM_GP7_EINT_MASK                 0x0040  /* IM_GP7_EINT */
#define WM8994_IM_GP7_EINT_SHIFT                     6  /* IM_GP7_EINT */
#define WM8994_IM_GP7_EINT_WIDTH                     1  /* IM_GP7_EINT */
#define WM8994_IM_GP6_EINT                      0x0020  /* IM_GP6_EINT */
#define WM8994_IM_GP6_EINT_MASK                 0x0020  /* IM_GP6_EINT */
#define WM8994_IM_GP6_EINT_SHIFT                     5  /* IM_GP6_EINT */
#define WM8994_IM_GP6_EINT_WIDTH                     1  /* IM_GP6_EINT */
#define WM8994_IM_GP5_EINT                      0x0010  /* IM_GP5_EINT */
#define WM8994_IM_GP5_EINT_MASK                 0x0010  /* IM_GP5_EINT */
#define WM8994_IM_GP5_EINT_SHIFT                     4  /* IM_GP5_EINT */
#define WM8994_IM_GP5_EINT_WIDTH                     1  /* IM_GP5_EINT */
#define WM8994_IM_GP4_EINT                      0x0008  /* IM_GP4_EINT */
#define WM8994_IM_GP4_EINT_MASK                 0x0008  /* IM_GP4_EINT */
#define WM8994_IM_GP4_EINT_SHIFT                     3  /* IM_GP4_EINT */
#define WM8994_IM_GP4_EINT_WIDTH                     1  /* IM_GP4_EINT */
#define WM8994_IM_GP3_EINT                      0x0004  /* IM_GP3_EINT */
#define WM8994_IM_GP3_EINT_MASK                 0x0004  /* IM_GP3_EINT */
#define WM8994_IM_GP3_EINT_SHIFT                     2  /* IM_GP3_EINT */
#define WM8994_IM_GP3_EINT_WIDTH                     1  /* IM_GP3_EINT */
#define WM8994_IM_GP2_EINT                      0x0002  /* IM_GP2_EINT */
#define WM8994_IM_GP2_EINT_MASK                 0x0002  /* IM_GP2_EINT */
#define WM8994_IM_GP2_EINT_SHIFT                     1  /* IM_GP2_EINT */
#define WM8994_IM_GP2_EINT_WIDTH                     1  /* IM_GP2_EINT */
#define WM8994_IM_GP1_EINT                      0x0001  /* IM_GP1_EINT */
#define WM8994_IM_GP1_EINT_MASK                 0x0001  /* IM_GP1_EINT */
#define WM8994_IM_GP1_EINT_SHIFT                     0  /* IM_GP1_EINT */
#define WM8994_IM_GP1_EINT_WIDTH                     1  /* IM_GP1_EINT */

/*
 * R1849 (0x739) - Interrupt Status 2 Mask
 */
#define WM8994_IM_TEMP_WARN_EINT                0x8000  /* IM_TEMP_WARN_EINT */
#define WM8994_IM_TEMP_WARN_EINT_MASK           0x8000  /* IM_TEMP_WARN_EINT */
#define WM8994_IM_TEMP_WARN_EINT_SHIFT              15  /* IM_TEMP_WARN_EINT */
#define WM8994_IM_TEMP_WARN_EINT_WIDTH               1  /* IM_TEMP_WARN_EINT */
#define WM8994_IM_DCS_DONE_EINT                 0x4000  /* IM_DCS_DONE_EINT */
#define WM8994_IM_DCS_DONE_EINT_MASK            0x4000  /* IM_DCS_DONE_EINT */
#define WM8994_IM_DCS_DONE_EINT_SHIFT               14  /* IM_DCS_DONE_EINT */
#define WM8994_IM_DCS_DONE_EINT_WIDTH                1  /* IM_DCS_DONE_EINT */
#define WM8994_IM_WSEQ_DONE_EINT                0x2000  /* IM_WSEQ_DONE_EINT */
#define WM8994_IM_WSEQ_DONE_EINT_MASK           0x2000  /* IM_WSEQ_DONE_EINT */
#define WM8994_IM_WSEQ_DONE_EINT_SHIFT              13  /* IM_WSEQ_DONE_EINT */
#define WM8994_IM_WSEQ_DONE_EINT_WIDTH               1  /* IM_WSEQ_DONE_EINT */
#define WM8994_IM_FIFOS_ERR_EINT                0x1000  /* IM_FIFOS_ERR_EINT */
#define WM8994_IM_FIFOS_ERR_EINT_MASK           0x1000  /* IM_FIFOS_ERR_EINT */
#define WM8994_IM_FIFOS_ERR_EINT_SHIFT              12  /* IM_FIFOS_ERR_EINT */
#define WM8994_IM_FIFOS_ERR_EINT_WIDTH               1  /* IM_FIFOS_ERR_EINT */
#define WM8994_IM_AIF2DRC_SIG_DET_EINT          0x0800  /* IM_AIF2DRC_SIG_DET_EINT */
#define WM8994_IM_AIF2DRC_SIG_DET_EINT_MASK     0x0800  /* IM_AIF2DRC_SIG_DET_EINT */
#define WM8994_IM_AIF2DRC_SIG_DET_EINT_SHIFT        11  /* IM_AIF2DRC_SIG_DET_EINT */
#define WM8994_IM_AIF2DRC_SIG_DET_EINT_WIDTH         1  /* IM_AIF2DRC_SIG_DET_EINT */
#define WM8994_IM_AIF1DRC2_SIG_DET_EINT         0x0400  /* IM_AIF1DRC2_SIG_DET_EINT */
#define WM8994_IM_AIF1DRC2_SIG_DET_EINT_MASK    0x0400  /* IM_AIF1DRC2_SIG_DET_EINT */
#define WM8994_IM_AIF1DRC2_SIG_DET_EINT_SHIFT       10  /* IM_AIF1DRC2_SIG_DET_EINT */
#define WM8994_IM_AIF1DRC2_SIG_DET_EINT_WIDTH        1  /* IM_AIF1DRC2_SIG_DET_EINT */
#define WM8994_IM_AIF1DRC1_SIG_DET_EINT         0x0200  /* IM_AIF1DRC1_SIG_DET_EINT */
#define WM8994_IM_AIF1DRC1_SIG_DET_EINT_MASK    0x0200  /* IM_AIF1DRC1_SIG_DET_EINT */
#define WM8994_IM_AIF1DRC1_SIG_DET_EINT_SHIFT        9  /* IM_AIF1DRC1_SIG_DET_EINT */
#define WM8994_IM_AIF1DRC1_SIG_DET_EINT_WIDTH        1  /* IM_AIF1DRC1_SIG_DET_EINT */
#define WM8994_IM_SRC2_LOCK_EINT                0x0100  /* IM_SRC2_LOCK_EINT */
#define WM8994_IM_SRC2_LOCK_EINT_MASK           0x0100  /* IM_SRC2_LOCK_EINT */
#define WM8994_IM_SRC2_LOCK_EINT_SHIFT               8  /* IM_SRC2_LOCK_EINT */
#define WM8994_IM_SRC2_LOCK_EINT_WIDTH               1  /* IM_SRC2_LOCK_EINT */
#define WM8994_IM_SRC1_LOCK_EINT                0x0080  /* IM_SRC1_LOCK_EINT */
#define WM8994_IM_SRC1_LOCK_EINT_MASK           0x0080  /* IM_SRC1_LOCK_EINT */
#define WM8994_IM_SRC1_LOCK_EINT_SHIFT               7  /* IM_SRC1_LOCK_EINT */
#define WM8994_IM_SRC1_LOCK_EINT_WIDTH               1  /* IM_SRC1_LOCK_EINT */
#define WM8994_IM_FLL2_LOCK_EINT                0x0040  /* IM_FLL2_LOCK_EINT */
#define WM8994_IM_FLL2_LOCK_EINT_MASK           0x0040  /* IM_FLL2_LOCK_EINT */
#define WM8994_IM_FLL2_LOCK_EINT_SHIFT               6  /* IM_FLL2_LOCK_EINT */
#define WM8994_IM_FLL2_LOCK_EINT_WIDTH               1  /* IM_FLL2_LOCK_EINT */
#define WM8994_IM_FLL1_LOCK_EINT                0x0020  /* IM_FLL1_LOCK_EINT */
#define WM8994_IM_FLL1_LOCK_EINT_MASK           0x0020  /* IM_FLL1_LOCK_EINT */
#define WM8994_IM_FLL1_LOCK_EINT_SHIFT               5  /* IM_FLL1_LOCK_EINT */
#define WM8994_IM_FLL1_LOCK_EINT_WIDTH               1  /* IM_FLL1_LOCK_EINT */
#define WM8994_IM_MIC2_SHRT_EINT                0x0010  /* IM_MIC2_SHRT_EINT */
#define WM8994_IM_MIC2_SHRT_EINT_MASK           0x0010  /* IM_MIC2_SHRT_EINT */
#define WM8994_IM_MIC2_SHRT_EINT_SHIFT               4  /* IM_MIC2_SHRT_EINT */
#define WM8994_IM_MIC2_SHRT_EINT_WIDTH               1  /* IM_MIC2_SHRT_EINT */
#define WM8994_IM_MIC2_DET_EINT                 0x0008  /* IM_MIC2_DET_EINT */
#define WM8994_IM_MIC2_DET_EINT_MASK            0x0008  /* IM_MIC2_DET_EINT */
#define WM8994_IM_MIC2_DET_EINT_SHIFT                3  /* IM_MIC2_DET_EINT */
#define WM8994_IM_MIC2_DET_EINT_WIDTH                1  /* IM_MIC2_DET_EINT */
#define WM8994_IM_MIC1_SHRT_EINT                0x0004  /* IM_MIC1_SHRT_EINT */
#define WM8994_IM_MIC1_SHRT_EINT_MASK           0x0004  /* IM_MIC1_SHRT_EINT */
#define WM8994_IM_MIC1_SHRT_EINT_SHIFT               2  /* IM_MIC1_SHRT_EINT */
#define WM8994_IM_MIC1_SHRT_EINT_WIDTH               1  /* IM_MIC1_SHRT_EINT */
#define WM8994_IM_MIC1_DET_EINT                 0x0002  /* IM_MIC1_DET_EINT */
#define WM8994_IM_MIC1_DET_EINT_MASK            0x0002  /* IM_MIC1_DET_EINT */
#define WM8994_IM_MIC1_DET_EINT_SHIFT                1  /* IM_MIC1_DET_EINT */
#define WM8994_IM_MIC1_DET_EINT_WIDTH                1  /* IM_MIC1_DET_EINT */
#define WM8994_IM_TEMP_SHUT_EINT                0x0001  /* IM_TEMP_SHUT_EINT */
#define WM8994_IM_TEMP_SHUT_EINT_MASK           0x0001  /* IM_TEMP_SHUT_EINT */
#define WM8994_IM_TEMP_SHUT_EINT_SHIFT               0  /* IM_TEMP_SHUT_EINT */
#define WM8994_IM_TEMP_SHUT_EINT_WIDTH               1  /* IM_TEMP_SHUT_EINT */

/*
 * R1856 (0x740) - Interrupt Control
 */
#define WM8994_IM_IRQ                           0x0001  /* IM_IRQ */
#define WM8994_IM_IRQ_MASK                      0x0001  /* IM_IRQ */
#define WM8994_IM_IRQ_SHIFT                          0  /* IM_IRQ */
#define WM8994_IM_IRQ_WIDTH                          1  /* IM_IRQ */

/*
 * R1864 (0x748) - IRQ Debounce
 */
#define WM8994_TEMP_WARN_DB                     0x0020  /* TEMP_WARN_DB */
#define WM8994_TEMP_WARN_DB_MASK                0x0020  /* TEMP_WARN_DB */
#define WM8994_TEMP_WARN_DB_SHIFT                    5  /* TEMP_WARN_DB */
#define WM8994_TEMP_WARN_DB_WIDTH                    1  /* TEMP_WARN_DB */
#define WM8994_MIC2_SHRT_DB                     0x0010  /* MIC2_SHRT_DB */
#define WM8994_MIC2_SHRT_DB_MASK                0x0010  /* MIC2_SHRT_DB */
#define WM8994_MIC2_SHRT_DB_SHIFT                    4  /* MIC2_SHRT_DB */
#define WM8994_MIC2_SHRT_DB_WIDTH                    1  /* MIC2_SHRT_DB */
#define WM8994_MIC2_DET_DB                      0x0008  /* MIC2_DET_DB */
#define WM8994_MIC2_DET_DB_MASK                 0x0008  /* MIC2_DET_DB */
#define WM8994_MIC2_DET_DB_SHIFT                     3  /* MIC2_DET_DB */
#define WM8994_MIC2_DET_DB_WIDTH                     1  /* MIC2_DET_DB */
#define WM8994_MIC1_SHRT_DB                     0x0004  /* MIC1_SHRT_DB */
#define WM8994_MIC1_SHRT_DB_MASK                0x0004  /* MIC1_SHRT_DB */
#define WM8994_MIC1_SHRT_DB_SHIFT                    2  /* MIC1_SHRT_DB */
#define WM8994_MIC1_SHRT_DB_WIDTH                    1  /* MIC1_SHRT_DB */
#define WM8994_MIC1_DET_DB                      0x0002  /* MIC1_DET_DB */
#define WM8994_MIC1_DET_DB_MASK                 0x0002  /* MIC1_DET_DB */
#define WM8994_MIC1_DET_DB_SHIFT                     1  /* MIC1_DET_DB */
#define WM8994_MIC1_DET_DB_WIDTH                     1  /* MIC1_DET_DB */
#define WM8994_TEMP_SHUT_DB                     0x0001  /* TEMP_SHUT_DB */
#define WM8994_TEMP_SHUT_DB_MASK                0x0001  /* TEMP_SHUT_DB */
#define WM8994_TEMP_SHUT_DB_SHIFT                    0  /* TEMP_SHUT_DB */
#define WM8994_TEMP_SHUT_DB_WIDTH                    1  /* TEMP_SHUT_DB */

/*
 * R2304 (0x900) - DSP2_Program
 */
#define WM8958_DSP2_ENA                         0x0001  /* DSP2_ENA */
#define WM8958_DSP2_ENA_MASK                    0x0001  /* DSP2_ENA */
#define WM8958_DSP2_ENA_SHIFT                        0  /* DSP2_ENA */
#define WM8958_DSP2_ENA_WIDTH                        1  /* DSP2_ENA */

/*
 * R2305 (0x901) - DSP2_Config
 */
#define WM8958_MBC_SEL_MASK                     0x0030  /* MBC_SEL - [5:4] */
#define WM8958_MBC_SEL_SHIFT                         4  /* MBC_SEL - [5:4] */
#define WM8958_MBC_SEL_WIDTH                         2  /* MBC_SEL - [5:4] */
#define WM8958_MBC_ENA                          0x0001  /* MBC_ENA */
#define WM8958_MBC_ENA_MASK                     0x0001  /* MBC_ENA */
#define WM8958_MBC_ENA_SHIFT                         0  /* MBC_ENA */
#define WM8958_MBC_ENA_WIDTH                         1  /* MBC_ENA */

/*
 * R2560 (0xA00) - DSP2_MagicNum
 */
#define WM8958_DSP2_MAGIC_NUM_MASK              0xFFFF  /* DSP2_MAGIC_NUM - [15:0] */
#define WM8958_DSP2_MAGIC_NUM_SHIFT                  0  /* DSP2_MAGIC_NUM - [15:0] */
#define WM8958_DSP2_MAGIC_NUM_WIDTH                 16  /* DSP2_MAGIC_NUM - [15:0] */

/*
 * R2561 (0xA01) - DSP2_ReleaseYear
 */
#define WM8958_DSP2_RELEASE_YEAR_MASK           0xFFFF  /* DSP2_RELEASE_YEAR - [15:0] */
#define WM8958_DSP2_RELEASE_YEAR_SHIFT               0  /* DSP2_RELEASE_YEAR - [15:0] */
#define WM8958_DSP2_RELEASE_YEAR_WIDTH              16  /* DSP2_RELEASE_YEAR - [15:0] */

/*
 * R2562 (0xA02) - DSP2_ReleaseMonthDay
 */
#define WM8958_DSP2_RELEASE_MONTH_MASK          0xFF00  /* DSP2_RELEASE_MONTH - [15:8] */
#define WM8958_DSP2_RELEASE_MONTH_SHIFT              8  /* DSP2_RELEASE_MONTH - [15:8] */
#define WM8958_DSP2_RELEASE_MONTH_WIDTH              8  /* DSP2_RELEASE_MONTH - [15:8] */
#define WM8958_DSP2_RELEASE_DAY_MASK            0x00FF  /* DSP2_RELEASE_DAY - [7:0] */
#define WM8958_DSP2_RELEASE_DAY_SHIFT                0  /* DSP2_RELEASE_DAY - [7:0] */
#define WM8958_DSP2_RELEASE_DAY_WIDTH                8  /* DSP2_RELEASE_DAY - [7:0] */

/*
 * R2563 (0xA03) - DSP2_ReleaseTime
 */
#define WM8958_DSP2_RELEASE_HOURS_MASK          0xFF00  /* DSP2_RELEASE_HOURS - [15:8] */
#define WM8958_DSP2_RELEASE_HOURS_SHIFT              8  /* DSP2_RELEASE_HOURS - [15:8] */
#define WM8958_DSP2_RELEASE_HOURS_WIDTH              8  /* DSP2_RELEASE_HOURS - [15:8] */
#define WM8958_DSP2_RELEASE_MINS_MASK           0x00FF  /* DSP2_RELEASE_MINS - [7:0] */
#define WM8958_DSP2_RELEASE_MINS_SHIFT               0  /* DSP2_RELEASE_MINS - [7:0] */
#define WM8958_DSP2_RELEASE_MINS_WIDTH               8  /* DSP2_RELEASE_MINS - [7:0] */

/*
 * R2564 (0xA04) - DSP2_VerMajMin
 */
#define WM8958_DSP2_MAJOR_VER_MASK              0xFF00  /* DSP2_MAJOR_VER - [15:8] */
#define WM8958_DSP2_MAJOR_VER_SHIFT                  8  /* DSP2_MAJOR_VER - [15:8] */
#define WM8958_DSP2_MAJOR_VER_WIDTH                  8  /* DSP2_MAJOR_VER - [15:8] */
#define WM8958_DSP2_MINOR_VER_MASK              0x00FF  /* DSP2_MINOR_VER - [7:0] */
#define WM8958_DSP2_MINOR_VER_SHIFT                  0  /* DSP2_MINOR_VER - [7:0] */
#define WM8958_DSP2_MINOR_VER_WIDTH                  8  /* DSP2_MINOR_VER - [7:0] */

/*
 * R2565 (0xA05) - DSP2_VerBuild
 */
#define WM8958_DSP2_BUILD_VER_MASK              0xFFFF  /* DSP2_BUILD_VER - [15:0] */
#define WM8958_DSP2_BUILD_VER_SHIFT                  0  /* DSP2_BUILD_VER - [15:0] */
#define WM8958_DSP2_BUILD_VER_WIDTH                 16  /* DSP2_BUILD_VER - [15:0] */

/*
 * R2573 (0xA0D) - DSP2_ExecControl
 */
#define WM8958_DSP2_STOPC                       0x0020  /* DSP2_STOPC */
#define WM8958_DSP2_STOPC_MASK                  0x0020  /* DSP2_STOPC */
#define WM8958_DSP2_STOPC_SHIFT                      5  /* DSP2_STOPC */
#define WM8958_DSP2_STOPC_WIDTH                      1  /* DSP2_STOPC */
#define WM8958_DSP2_STOPS                       0x0010  /* DSP2_STOPS */
#define WM8958_DSP2_STOPS_MASK                  0x0010  /* DSP2_STOPS */
#define WM8958_DSP2_STOPS_SHIFT                      4  /* DSP2_STOPS */
#define WM8958_DSP2_STOPS_WIDTH                      1  /* DSP2_STOPS */
#define WM8958_DSP2_STOPI                       0x0008  /* DSP2_STOPI */
#define WM8958_DSP2_STOPI_MASK                  0x0008  /* DSP2_STOPI */
#define WM8958_DSP2_STOPI_SHIFT                      3  /* DSP2_STOPI */
#define WM8958_DSP2_STOPI_WIDTH                      1  /* DSP2_STOPI */
#define WM8958_DSP2_STOP                        0x0004  /* DSP2_STOP */
#define WM8958_DSP2_STOP_MASK                   0x0004  /* DSP2_STOP */
#define WM8958_DSP2_STOP_SHIFT                       2  /* DSP2_STOP */
#define WM8958_DSP2_STOP_WIDTH                       1  /* DSP2_STOP */
#define WM8958_DSP2_RUNR                        0x0002  /* DSP2_RUNR */
#define WM8958_DSP2_RUNR_MASK                   0x0002  /* DSP2_RUNR */
#define WM8958_DSP2_RUNR_SHIFT                       1  /* DSP2_RUNR */
#define WM8958_DSP2_RUNR_WIDTH                       1  /* DSP2_RUNR */
#define WM8958_DSP2_RUN                         0x0001  /* DSP2_RUN */
#define WM8958_DSP2_RUN_MASK                    0x0001  /* DSP2_RUN */
#define WM8958_DSP2_RUN_SHIFT                        0  /* DSP2_RUN */
#define WM8958_DSP2_RUN_WIDTH                        1  /* DSP2_RUN */

#endif
