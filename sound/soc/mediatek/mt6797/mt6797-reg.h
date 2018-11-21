/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt6797-reg.h  --  Mediatek 6797 audio driver reg definition
 *
 * Copyright (c) 2018 MediaTek Inc.
 * Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>
 */

#ifndef _MT6797_REG_H_
#define _MT6797_REG_H_

#define AUDIO_TOP_CON0            0x0000
#define AUDIO_TOP_CON1            0x0004
#define AUDIO_TOP_CON3            0x000c
#define AFE_DAC_CON0              0x0010
#define AFE_DAC_CON1              0x0014
#define AFE_I2S_CON               0x0018
#define AFE_DAIBT_CON0            0x001c
#define AFE_CONN0                 0x0020
#define AFE_CONN1                 0x0024
#define AFE_CONN2                 0x0028
#define AFE_CONN3                 0x002c
#define AFE_CONN4                 0x0030
#define AFE_I2S_CON1              0x0034
#define AFE_I2S_CON2              0x0038
#define AFE_MRGIF_CON             0x003c
#define AFE_DL1_BASE              0x0040
#define AFE_DL1_CUR               0x0044
#define AFE_DL1_END               0x0048
#define AFE_I2S_CON3              0x004c
#define AFE_DL2_BASE              0x0050
#define AFE_DL2_CUR               0x0054
#define AFE_DL2_END               0x0058
#define AFE_CONN5                 0x005c
#define AFE_CONN_24BIT            0x006c
#define AFE_AWB_BASE              0x0070
#define AFE_AWB_END               0x0078
#define AFE_AWB_CUR               0x007c
#define AFE_VUL_BASE              0x0080
#define AFE_VUL_END               0x0088
#define AFE_VUL_CUR               0x008c
#define AFE_DAI_BASE              0x0090
#define AFE_DAI_END               0x0098
#define AFE_DAI_CUR               0x009c
#define AFE_CONN6                 0x00bc
#define AFE_MEMIF_MSB             0x00cc
#define AFE_MEMIF_MON0            0x00d0
#define AFE_MEMIF_MON1            0x00d4
#define AFE_MEMIF_MON2            0x00d8
#define AFE_MEMIF_MON4            0x00e0
#define AFE_ADDA_DL_SRC2_CON0     0x0108
#define AFE_ADDA_DL_SRC2_CON1     0x010c
#define AFE_ADDA_UL_SRC_CON0      0x0114
#define AFE_ADDA_UL_SRC_CON1      0x0118
#define AFE_ADDA_TOP_CON0         0x0120
#define AFE_ADDA_UL_DL_CON0       0x0124
#define AFE_ADDA_SRC_DEBUG        0x012c
#define AFE_ADDA_SRC_DEBUG_MON0   0x0130
#define AFE_ADDA_SRC_DEBUG_MON1   0x0134
#define AFE_ADDA_NEWIF_CFG0       0x0138
#define AFE_ADDA_NEWIF_CFG1       0x013c
#define AFE_ADDA_NEWIF_CFG2       0x0140
#define AFE_DMA_CTL               0x0150
#define AFE_DMA_MON0              0x0154
#define AFE_DMA_MON1              0x0158
#define AFE_SIDETONE_DEBUG        0x01d0
#define AFE_SIDETONE_MON          0x01d4
#define AFE_SIDETONE_CON0         0x01e0
#define AFE_SIDETONE_COEFF        0x01e4
#define AFE_SIDETONE_CON1         0x01e8
#define AFE_SIDETONE_GAIN         0x01ec
#define AFE_SGEN_CON0             0x01f0
#define AFE_SINEGEN_CON_TDM       0x01fc
#define AFE_TOP_CON0              0x0200
#define AFE_ADDA_PREDIS_CON0      0x0260
#define AFE_ADDA_PREDIS_CON1      0x0264
#define AFE_MRGIF_MON0            0x0270
#define AFE_MRGIF_MON1            0x0274
#define AFE_MRGIF_MON2            0x0278
#define AFE_I2S_MON               0x027c
#define AFE_MOD_DAI_BASE          0x0330
#define AFE_MOD_DAI_END           0x0338
#define AFE_MOD_DAI_CUR           0x033c
#define AFE_VUL_D2_BASE           0x0350
#define AFE_VUL_D2_END            0x0358
#define AFE_VUL_D2_CUR            0x035c
#define AFE_DL3_BASE              0x0360
#define AFE_DL3_CUR               0x0364
#define AFE_DL3_END               0x0368
#define AFE_HDMI_OUT_CON0         0x0370
#define AFE_HDMI_BASE             0x0374
#define AFE_HDMI_CUR              0x0378
#define AFE_HDMI_END              0x037c
#define AFE_HDMI_CONN0            0x0390
#define AFE_IRQ3_MCU_CNT_MON      0x0398
#define AFE_IRQ4_MCU_CNT_MON      0x039c
#define AFE_IRQ_MCU_CON           0x03a0
#define AFE_IRQ_MCU_STATUS        0x03a4
#define AFE_IRQ_MCU_CLR           0x03a8
#define AFE_IRQ_MCU_CNT1          0x03ac
#define AFE_IRQ_MCU_CNT2          0x03b0
#define AFE_IRQ_MCU_EN            0x03b4
#define AFE_IRQ_MCU_MON2          0x03b8
#define AFE_IRQ_MCU_CNT5          0x03bc
#define AFE_IRQ1_MCU_CNT_MON      0x03c0
#define AFE_IRQ2_MCU_CNT_MON      0x03c4
#define AFE_IRQ1_MCU_EN_CNT_MON   0x03c8
#define AFE_IRQ5_MCU_CNT_MON      0x03cc
#define AFE_MEMIF_MINLEN          0x03d0
#define AFE_MEMIF_MAXLEN          0x03d4
#define AFE_MEMIF_PBUF_SIZE       0x03d8
#define AFE_IRQ_MCU_CNT7          0x03dc
#define AFE_IRQ7_MCU_CNT_MON      0x03e0
#define AFE_IRQ_MCU_CNT3          0x03e4
#define AFE_IRQ_MCU_CNT4          0x03e8
#define AFE_APLL1_TUNER_CFG       0x03f0
#define AFE_APLL2_TUNER_CFG       0x03f4
#define AFE_MEMIF_HD_MODE         0x03f8
#define AFE_MEMIF_HDALIGN         0x03fc
#define AFE_GAIN1_CON0            0x0410
#define AFE_GAIN1_CON1            0x0414
#define AFE_GAIN1_CON2            0x0418
#define AFE_GAIN1_CON3            0x041c
#define AFE_CONN7                 0x0420
#define AFE_GAIN1_CUR             0x0424
#define AFE_GAIN2_CON0            0x0428
#define AFE_GAIN2_CON1            0x042c
#define AFE_GAIN2_CON2            0x0430
#define AFE_GAIN2_CON3            0x0434
#define AFE_CONN8                 0x0438
#define AFE_GAIN2_CUR             0x043c
#define AFE_CONN9                 0x0440
#define AFE_CONN10                0x0444
#define AFE_CONN11                0x0448
#define AFE_CONN12                0x044c
#define AFE_CONN13                0x0450
#define AFE_CONN14                0x0454
#define AFE_CONN15                0x0458
#define AFE_CONN16                0x045c
#define AFE_CONN17                0x0460
#define AFE_CONN18                0x0464
#define AFE_CONN19                0x0468
#define AFE_CONN20                0x046c
#define AFE_CONN21                0x0470
#define AFE_CONN22                0x0474
#define AFE_CONN23                0x0478
#define AFE_CONN24                0x047c
#define AFE_CONN_RS               0x0494
#define AFE_CONN_DI               0x0498
#define AFE_CONN25                0x04b0
#define AFE_CONN26                0x04b4
#define AFE_CONN27                0x04b8
#define AFE_CONN28                0x04bc
#define AFE_CONN29                0x04c0
#define AFE_SRAM_DELSEL_CON0      0x04f0
#define AFE_SRAM_DELSEL_CON1      0x04f4
#define AFE_ASRC_CON0             0x0500
#define AFE_ASRC_CON1             0x0504
#define AFE_ASRC_CON2             0x0508
#define AFE_ASRC_CON3             0x050c
#define AFE_ASRC_CON4             0x0510
#define AFE_ASRC_CON5             0x0514
#define AFE_ASRC_CON6             0x0518
#define AFE_ASRC_CON7             0x051c
#define AFE_ASRC_CON8             0x0520
#define AFE_ASRC_CON9             0x0524
#define AFE_ASRC_CON10            0x0528
#define AFE_ASRC_CON11            0x052c
#define PCM_INTF_CON1             0x0530
#define PCM_INTF_CON2             0x0538
#define PCM2_INTF_CON             0x053c
#define AFE_TDM_CON1              0x0548
#define AFE_TDM_CON2              0x054c
#define AFE_ASRC_CON13            0x0550
#define AFE_ASRC_CON14            0x0554
#define AFE_ASRC_CON15            0x0558
#define AFE_ASRC_CON16            0x055c
#define AFE_ASRC_CON17            0x0560
#define AFE_ASRC_CON18            0x0564
#define AFE_ASRC_CON19            0x0568
#define AFE_ASRC_CON20            0x056c
#define AFE_ASRC_CON21            0x0570
#define CLK_AUDDIV_0              0x05a0
#define CLK_AUDDIV_1              0x05a4
#define CLK_AUDDIV_2              0x05a8
#define CLK_AUDDIV_3              0x05ac
#define AUDIO_TOP_DBG_CON         0x05c8
#define AUDIO_TOP_DBG_MON0        0x05cc
#define AUDIO_TOP_DBG_MON1        0x05d0
#define AUDIO_TOP_DBG_MON2        0x05d4
#define AFE_ADDA2_TOP_CON0        0x0600
#define AFE_ASRC4_CON0            0x06c0
#define AFE_ASRC4_CON1            0x06c4
#define AFE_ASRC4_CON2            0x06c8
#define AFE_ASRC4_CON3            0x06cc
#define AFE_ASRC4_CON4            0x06d0
#define AFE_ASRC4_CON5            0x06d4
#define AFE_ASRC4_CON6            0x06d8
#define AFE_ASRC4_CON7            0x06dc
#define AFE_ASRC4_CON8            0x06e0
#define AFE_ASRC4_CON9            0x06e4
#define AFE_ASRC4_CON10           0x06e8
#define AFE_ASRC4_CON11           0x06ec
#define AFE_ASRC4_CON12           0x06f0
#define AFE_ASRC4_CON13           0x06f4
#define AFE_ASRC4_CON14           0x06f8
#define AFE_ASRC2_CON0            0x0700
#define AFE_ASRC2_CON1            0x0704
#define AFE_ASRC2_CON2            0x0708
#define AFE_ASRC2_CON3            0x070c
#define AFE_ASRC2_CON4            0x0710
#define AFE_ASRC2_CON5            0x0714
#define AFE_ASRC2_CON6            0x0718
#define AFE_ASRC2_CON7            0x071c
#define AFE_ASRC2_CON8            0x0720
#define AFE_ASRC2_CON9            0x0724
#define AFE_ASRC2_CON10           0x0728
#define AFE_ASRC2_CON11           0x072c
#define AFE_ASRC2_CON12           0x0730
#define AFE_ASRC2_CON13           0x0734
#define AFE_ASRC2_CON14           0x0738
#define AFE_ASRC3_CON0            0x0740
#define AFE_ASRC3_CON1            0x0744
#define AFE_ASRC3_CON2            0x0748
#define AFE_ASRC3_CON3            0x074c
#define AFE_ASRC3_CON4            0x0750
#define AFE_ASRC3_CON5            0x0754
#define AFE_ASRC3_CON6            0x0758
#define AFE_ASRC3_CON7            0x075c
#define AFE_ASRC3_CON8            0x0760
#define AFE_ASRC3_CON9            0x0764
#define AFE_ASRC3_CON10           0x0768
#define AFE_ASRC3_CON11           0x076c
#define AFE_ASRC3_CON12           0x0770
#define AFE_ASRC3_CON13           0x0774
#define AFE_ASRC3_CON14           0x0778
#define AFE_GENERAL_REG0          0x0800
#define AFE_GENERAL_REG1          0x0804
#define AFE_GENERAL_REG2          0x0808
#define AFE_GENERAL_REG3          0x080c
#define AFE_GENERAL_REG4          0x0810
#define AFE_GENERAL_REG5          0x0814
#define AFE_GENERAL_REG6          0x0818
#define AFE_GENERAL_REG7          0x081c
#define AFE_GENERAL_REG8          0x0820
#define AFE_GENERAL_REG9          0x0824
#define AFE_GENERAL_REG10         0x0828
#define AFE_GENERAL_REG11         0x082c
#define AFE_GENERAL_REG12         0x0830
#define AFE_GENERAL_REG13         0x0834
#define AFE_GENERAL_REG14         0x0838
#define AFE_GENERAL_REG15         0x083c
#define AFE_CBIP_CFG0             0x0840
#define AFE_CBIP_MON0             0x0844
#define AFE_CBIP_SLV_MUX_MON0     0x0848
#define AFE_CBIP_SLV_DECODER_MON0 0x084c

#define AFE_MAX_REGISTER AFE_CBIP_SLV_DECODER_MON0
#define AFE_IRQ_STATUS_BITS 0x5f

/* AUDIO_TOP_CON0 */
#define AHB_IDLE_EN_INT_SFT                                 30
#define AHB_IDLE_EN_INT_MASK                                0x1
#define AHB_IDLE_EN_INT_MASK_SFT                            (0x1 << 30)
#define AHB_IDLE_EN_EXT_SFT                                 29
#define AHB_IDLE_EN_EXT_MASK                                0x1
#define AHB_IDLE_EN_EXT_MASK_SFT                            (0x1 << 29)
#define PDN_TML_SFT                                         27
#define PDN_TML_MASK                                        0x1
#define PDN_TML_MASK_SFT                                    (0x1 << 27)
#define PDN_DAC_PREDIS_SFT                                  26
#define PDN_DAC_PREDIS_MASK                                 0x1
#define PDN_DAC_PREDIS_MASK_SFT                             (0x1 << 26)
#define PDN_DAC_SFT                                         25
#define PDN_DAC_MASK                                        0x1
#define PDN_DAC_MASK_SFT                                    (0x1 << 25)
#define PDN_ADC_SFT                                         24
#define PDN_ADC_MASK                                        0x1
#define PDN_ADC_MASK_SFT                                    (0x1 << 24)
#define PDN_TDM_CK_SFT                                      20
#define PDN_TDM_CK_MASK                                     0x1
#define PDN_TDM_CK_MASK_SFT                                 (0x1 << 20)
#define PDN_APLL_TUNER_SFT                                  19
#define PDN_APLL_TUNER_MASK                                 0x1
#define PDN_APLL_TUNER_MASK_SFT                             (0x1 << 19)
#define PDN_APLL2_TUNER_SFT                                 18
#define PDN_APLL2_TUNER_MASK                                0x1
#define PDN_APLL2_TUNER_MASK_SFT                            (0x1 << 18)
#define APB3_SEL_SFT                                        14
#define APB3_SEL_MASK                                       0x1
#define APB3_SEL_MASK_SFT                                   (0x1 << 14)
#define APB_R2T_SFT                                         13
#define APB_R2T_MASK                                        0x1
#define APB_R2T_MASK_SFT                                    (0x1 << 13)
#define APB_W2T_SFT                                         12
#define APB_W2T_MASK                                        0x1
#define APB_W2T_MASK_SFT                                    (0x1 << 12)
#define PDN_24M_SFT                                         9
#define PDN_24M_MASK                                        0x1
#define PDN_24M_MASK_SFT                                    (0x1 << 9)
#define PDN_22M_SFT                                         8
#define PDN_22M_MASK                                        0x1
#define PDN_22M_MASK_SFT                                    (0x1 << 8)
#define PDN_ADDA4_ADC_SFT                                   7
#define PDN_ADDA4_ADC_MASK                                  0x1
#define PDN_ADDA4_ADC_MASK_SFT                              (0x1 << 7)
#define PDN_I2S_SFT                                         6
#define PDN_I2S_MASK                                        0x1
#define PDN_I2S_MASK_SFT                                    (0x1 << 6)
#define PDN_AFE_SFT                                         2
#define PDN_AFE_MASK                                        0x1
#define PDN_AFE_MASK_SFT                                    (0x1 << 2)

/* AUDIO_TOP_CON1 */
#define PDN_ADC_HIRES_TML_SFT                               17
#define PDN_ADC_HIRES_TML_MASK                              0x1
#define PDN_ADC_HIRES_TML_MASK_SFT                          (0x1 << 17)
#define PDN_ADC_HIRES_SFT                                   16
#define PDN_ADC_HIRES_MASK                                  0x1
#define PDN_ADC_HIRES_MASK_SFT                              (0x1 << 16)
#define I2S4_BCLK_SW_CG_SFT                                 7
#define I2S4_BCLK_SW_CG_MASK                                0x1
#define I2S4_BCLK_SW_CG_MASK_SFT                            (0x1 << 7)
#define I2S3_BCLK_SW_CG_SFT                                 6
#define I2S3_BCLK_SW_CG_MASK                                0x1
#define I2S3_BCLK_SW_CG_MASK_SFT                            (0x1 << 6)
#define I2S2_BCLK_SW_CG_SFT                                 5
#define I2S2_BCLK_SW_CG_MASK                                0x1
#define I2S2_BCLK_SW_CG_MASK_SFT                            (0x1 << 5)
#define I2S1_BCLK_SW_CG_SFT                                 4
#define I2S1_BCLK_SW_CG_MASK                                0x1
#define I2S1_BCLK_SW_CG_MASK_SFT                            (0x1 << 4)
#define I2S_SOFT_RST2_SFT                                   2
#define I2S_SOFT_RST2_MASK                                  0x1
#define I2S_SOFT_RST2_MASK_SFT                              (0x1 << 2)
#define I2S_SOFT_RST_SFT                                    1
#define I2S_SOFT_RST_MASK                                   0x1
#define I2S_SOFT_RST_MASK_SFT                               (0x1 << 1)

/* AFE_DAC_CON0 */
#define AFE_AWB_RETM_SFT                                    31
#define AFE_AWB_RETM_MASK                                   0x1
#define AFE_AWB_RETM_MASK_SFT                               (0x1 << 31)
#define AFE_DL1_DATA2_RETM_SFT                              30
#define AFE_DL1_DATA2_RETM_MASK                             0x1
#define AFE_DL1_DATA2_RETM_MASK_SFT                         (0x1 << 30)
#define AFE_DL2_RETM_SFT                                    29
#define AFE_DL2_RETM_MASK                                   0x1
#define AFE_DL2_RETM_MASK_SFT                               (0x1 << 29)
#define AFE_DL1_RETM_SFT                                    28
#define AFE_DL1_RETM_MASK                                   0x1
#define AFE_DL1_RETM_MASK_SFT                               (0x1 << 28)
#define AFE_ON_RETM_SFT                                     27
#define AFE_ON_RETM_MASK                                    0x1
#define AFE_ON_RETM_MASK_SFT                                (0x1 << 27)
#define MOD_DAI_DUP_WR_SFT                                  26
#define MOD_DAI_DUP_WR_MASK                                 0x1
#define MOD_DAI_DUP_WR_MASK_SFT                             (0x1 << 26)
#define DAI_MODE_SFT                                        24
#define DAI_MODE_MASK                                       0x3
#define DAI_MODE_MASK_SFT                                   (0x3 << 24)
#define VUL_DATA2_MODE_SFT                                  20
#define VUL_DATA2_MODE_MASK                                 0xf
#define VUL_DATA2_MODE_MASK_SFT                             (0xf << 20)
#define DL1_DATA2_MODE_SFT                                  16
#define DL1_DATA2_MODE_MASK                                 0xf
#define DL1_DATA2_MODE_MASK_SFT                             (0xf << 16)
#define DL3_MODE_SFT                                        12
#define DL3_MODE_MASK                                       0xf
#define DL3_MODE_MASK_SFT                                   (0xf << 12)
#define VUL_DATA2_R_MONO_SFT                                11
#define VUL_DATA2_R_MONO_MASK                               0x1
#define VUL_DATA2_R_MONO_MASK_SFT                           (0x1 << 11)
#define VUL_DATA2_DATA_SFT                                  10
#define VUL_DATA2_DATA_MASK                                 0x1
#define VUL_DATA2_DATA_MASK_SFT                             (0x1 << 10)
#define VUL_DATA2_ON_SFT                                    9
#define VUL_DATA2_ON_MASK                                   0x1
#define VUL_DATA2_ON_MASK_SFT                               (0x1 << 9)
#define DL1_DATA2_ON_SFT                                    8
#define DL1_DATA2_ON_MASK                                   0x1
#define DL1_DATA2_ON_MASK_SFT                               (0x1 << 8)
#define MOD_DAI_ON_SFT                                      7
#define MOD_DAI_ON_MASK                                     0x1
#define MOD_DAI_ON_MASK_SFT                                 (0x1 << 7)
#define AWB_ON_SFT                                          6
#define AWB_ON_MASK                                         0x1
#define AWB_ON_MASK_SFT                                     (0x1 << 6)
#define DL3_ON_SFT                                          5
#define DL3_ON_MASK                                         0x1
#define DL3_ON_MASK_SFT                                     (0x1 << 5)
#define DAI_ON_SFT                                          4
#define DAI_ON_MASK                                         0x1
#define DAI_ON_MASK_SFT                                     (0x1 << 4)
#define VUL_ON_SFT                                          3
#define VUL_ON_MASK                                         0x1
#define VUL_ON_MASK_SFT                                     (0x1 << 3)
#define DL2_ON_SFT                                          2
#define DL2_ON_MASK                                         0x1
#define DL2_ON_MASK_SFT                                     (0x1 << 2)
#define DL1_ON_SFT                                          1
#define DL1_ON_MASK                                         0x1
#define DL1_ON_MASK_SFT                                     (0x1 << 1)
#define AFE_ON_SFT                                          0
#define AFE_ON_MASK                                         0x1
#define AFE_ON_MASK_SFT                                     (0x1 << 0)

/* AFE_DAC_CON1 */
#define MOD_DAI_MODE_SFT                                    30
#define MOD_DAI_MODE_MASK                                   0x3
#define MOD_DAI_MODE_MASK_SFT                               (0x3 << 30)
#define DAI_DUP_WR_SFT                                      29
#define DAI_DUP_WR_MASK                                     0x1
#define DAI_DUP_WR_MASK_SFT                                 (0x1 << 29)
#define VUL_R_MONO_SFT                                      28
#define VUL_R_MONO_MASK                                     0x1
#define VUL_R_MONO_MASK_SFT                                 (0x1 << 28)
#define VUL_DATA_SFT                                        27
#define VUL_DATA_MASK                                       0x1
#define VUL_DATA_MASK_SFT                                   (0x1 << 27)
#define AXI_2X1_CG_DISABLE_SFT                              26
#define AXI_2X1_CG_DISABLE_MASK                             0x1
#define AXI_2X1_CG_DISABLE_MASK_SFT                         (0x1 << 26)
#define AWB_R_MONO_SFT                                      25
#define AWB_R_MONO_MASK                                     0x1
#define AWB_R_MONO_MASK_SFT                                 (0x1 << 25)
#define AWB_DATA_SFT                                        24
#define AWB_DATA_MASK                                       0x1
#define AWB_DATA_MASK_SFT                                   (0x1 << 24)
#define DL3_DATA_SFT                                        23
#define DL3_DATA_MASK                                       0x1
#define DL3_DATA_MASK_SFT                                   (0x1 << 23)
#define DL2_DATA_SFT                                        22
#define DL2_DATA_MASK                                       0x1
#define DL2_DATA_MASK_SFT                                   (0x1 << 22)
#define DL1_DATA_SFT                                        21
#define DL1_DATA_MASK                                       0x1
#define DL1_DATA_MASK_SFT                                   (0x1 << 21)
#define DL1_DATA2_DATA_SFT                                  20
#define DL1_DATA2_DATA_MASK                                 0x1
#define DL1_DATA2_DATA_MASK_SFT                             (0x1 << 20)
#define VUL_MODE_SFT                                        16
#define VUL_MODE_MASK                                       0xf
#define VUL_MODE_MASK_SFT                                   (0xf << 16)
#define AWB_MODE_SFT                                        12
#define AWB_MODE_MASK                                       0xf
#define AWB_MODE_MASK_SFT                                   (0xf << 12)
#define I2S_MODE_SFT                                        8
#define I2S_MODE_MASK                                       0xf
#define I2S_MODE_MASK_SFT                                   (0xf << 8)
#define DL2_MODE_SFT                                        4
#define DL2_MODE_MASK                                       0xf
#define DL2_MODE_MASK_SFT                                   (0xf << 4)
#define DL1_MODE_SFT                                        0
#define DL1_MODE_MASK                                       0xf
#define DL1_MODE_MASK_SFT                                   (0xf << 0)

/* AFE_ADDA_DL_SRC2_CON0 */
#define DL_2_INPUT_MODE_CTL_SFT                             28
#define DL_2_INPUT_MODE_CTL_MASK                            0xf
#define DL_2_INPUT_MODE_CTL_MASK_SFT                        (0xf << 28)
#define DL_2_CH1_SATURATION_EN_CTL_SFT                      27
#define DL_2_CH1_SATURATION_EN_CTL_MASK                     0x1
#define DL_2_CH1_SATURATION_EN_CTL_MASK_SFT                 (0x1 << 27)
#define DL_2_CH2_SATURATION_EN_CTL_SFT                      26
#define DL_2_CH2_SATURATION_EN_CTL_MASK                     0x1
#define DL_2_CH2_SATURATION_EN_CTL_MASK_SFT                 (0x1 << 26)
#define DL_2_OUTPUT_SEL_CTL_SFT                             24
#define DL_2_OUTPUT_SEL_CTL_MASK                            0x3
#define DL_2_OUTPUT_SEL_CTL_MASK_SFT                        (0x3 << 24)
#define DL_2_FADEIN_0START_EN_SFT                           16
#define DL_2_FADEIN_0START_EN_MASK                          0x3
#define DL_2_FADEIN_0START_EN_MASK_SFT                      (0x3 << 16)
#define DL_DISABLE_HW_CG_CTL_SFT                            15
#define DL_DISABLE_HW_CG_CTL_MASK                           0x1
#define DL_DISABLE_HW_CG_CTL_MASK_SFT                       (0x1 << 15)
#define C_DATA_EN_SEL_CTL_PRE_SFT                           14
#define C_DATA_EN_SEL_CTL_PRE_MASK                          0x1
#define C_DATA_EN_SEL_CTL_PRE_MASK_SFT                      (0x1 << 14)
#define DL_2_SIDE_TONE_ON_CTL_PRE_SFT                       13
#define DL_2_SIDE_TONE_ON_CTL_PRE_MASK                      0x1
#define DL_2_SIDE_TONE_ON_CTL_PRE_MASK_SFT                  (0x1 << 13)
#define DL_2_MUTE_CH1_OFF_CTL_PRE_SFT                       12
#define DL_2_MUTE_CH1_OFF_CTL_PRE_MASK                      0x1
#define DL_2_MUTE_CH1_OFF_CTL_PRE_MASK_SFT                  (0x1 << 12)
#define DL_2_MUTE_CH2_OFF_CTL_PRE_SFT                       11
#define DL_2_MUTE_CH2_OFF_CTL_PRE_MASK                      0x1
#define DL_2_MUTE_CH2_OFF_CTL_PRE_MASK_SFT                  (0x1 << 11)
#define DL2_ARAMPSP_CTL_PRE_SFT                             9
#define DL2_ARAMPSP_CTL_PRE_MASK                            0x3
#define DL2_ARAMPSP_CTL_PRE_MASK_SFT                        (0x3 << 9)
#define DL_2_IIRMODE_CTL_PRE_SFT                            6
#define DL_2_IIRMODE_CTL_PRE_MASK                           0x7
#define DL_2_IIRMODE_CTL_PRE_MASK_SFT                       (0x7 << 6)
#define DL_2_VOICE_MODE_CTL_PRE_SFT                         5
#define DL_2_VOICE_MODE_CTL_PRE_MASK                        0x1
#define DL_2_VOICE_MODE_CTL_PRE_MASK_SFT                    (0x1 << 5)
#define D2_2_MUTE_CH1_ON_CTL_PRE_SFT                        4
#define D2_2_MUTE_CH1_ON_CTL_PRE_MASK                       0x1
#define D2_2_MUTE_CH1_ON_CTL_PRE_MASK_SFT                   (0x1 << 4)
#define D2_2_MUTE_CH2_ON_CTL_PRE_SFT                        3
#define D2_2_MUTE_CH2_ON_CTL_PRE_MASK                       0x1
#define D2_2_MUTE_CH2_ON_CTL_PRE_MASK_SFT                   (0x1 << 3)
#define DL_2_IIR_ON_CTL_PRE_SFT                             2
#define DL_2_IIR_ON_CTL_PRE_MASK                            0x1
#define DL_2_IIR_ON_CTL_PRE_MASK_SFT                        (0x1 << 2)
#define DL_2_GAIN_ON_CTL_PRE_SFT                            1
#define DL_2_GAIN_ON_CTL_PRE_MASK                           0x1
#define DL_2_GAIN_ON_CTL_PRE_MASK_SFT                       (0x1 << 1)
#define DL_2_SRC_ON_TMP_CTL_PRE_SFT                         0
#define DL_2_SRC_ON_TMP_CTL_PRE_MASK                        0x1
#define DL_2_SRC_ON_TMP_CTL_PRE_MASK_SFT                    (0x1 << 0)

/* AFE_ADDA_DL_SRC2_CON1 */
#define DL_2_GAIN_CTL_PRE_SFT                               16
#define DL_2_GAIN_CTL_PRE_MASK                              0xffff
#define DL_2_GAIN_CTL_PRE_MASK_SFT                          (0xffff << 16)
#define DL_2_GAIN_MODE_CTL_SFT                              0
#define DL_2_GAIN_MODE_CTL_MASK                             0x1
#define DL_2_GAIN_MODE_CTL_MASK_SFT                         (0x1 << 0)

/* AFE_ADDA_UL_SRC_CON0 */
#define C_COMB_OUT_SIN_GEN_CTL_SFT                          31
#define C_COMB_OUT_SIN_GEN_CTL_MASK                         0x1
#define C_COMB_OUT_SIN_GEN_CTL_MASK_SFT                     (0x1 << 31)
#define C_BASEBAND_SIN_GEN_CTL_SFT                          30
#define C_BASEBAND_SIN_GEN_CTL_MASK                         0x1
#define C_BASEBAND_SIN_GEN_CTL_MASK_SFT                     (0x1 << 30)
#define C_DIGMIC_PHASE_SEL_CH1_CTL_SFT                      27
#define C_DIGMIC_PHASE_SEL_CH1_CTL_MASK                     0x7
#define C_DIGMIC_PHASE_SEL_CH1_CTL_MASK_SFT                 (0x7 << 27)
#define C_DIGMIC_PHASE_SEL_CH2_CTL_SFT                      24
#define C_DIGMIC_PHASE_SEL_CH2_CTL_MASK                     0x7
#define C_DIGMIC_PHASE_SEL_CH2_CTL_MASK_SFT                 (0x7 << 24)
#define C_TWO_DIGITAL_MIC_CTL_SFT                           23
#define C_TWO_DIGITAL_MIC_CTL_MASK                          0x1
#define C_TWO_DIGITAL_MIC_CTL_MASK_SFT                      (0x1 << 23)
#define UL_MODE_3P25M_CH2_CTL_SFT                           22
#define UL_MODE_3P25M_CH2_CTL_MASK                          0x1
#define UL_MODE_3P25M_CH2_CTL_MASK_SFT                      (0x1 << 22)
#define UL_MODE_3P25M_CH1_CTL_SFT                           21
#define UL_MODE_3P25M_CH1_CTL_MASK                          0x1
#define UL_MODE_3P25M_CH1_CTL_MASK_SFT                      (0x1 << 21)
#define UL_SRC_USE_CIC_OUT_CTL_SFT                          20
#define UL_SRC_USE_CIC_OUT_CTL_MASK                         0x1
#define UL_SRC_USE_CIC_OUT_CTL_MASK_SFT                     (0x1 << 20)
#define UL_VOICE_MODE_CH1_CH2_CTL_SFT                       17
#define UL_VOICE_MODE_CH1_CH2_CTL_MASK                      0x7
#define UL_VOICE_MODE_CH1_CH2_CTL_MASK_SFT                  (0x7 << 17)
#define DMIC_LOW_POWER_MODE_CTL_SFT                         14
#define DMIC_LOW_POWER_MODE_CTL_MASK                        0x3
#define DMIC_LOW_POWER_MODE_CTL_MASK_SFT                    (0x3 << 14)
#define DMIC_48K_SEL_CTL_SFT                                13
#define DMIC_48K_SEL_CTL_MASK                               0x1
#define DMIC_48K_SEL_CTL_MASK_SFT                           (0x1 << 13)
#define UL_DISABLE_HW_CG_CTL_SFT                            12
#define UL_DISABLE_HW_CG_CTL_MASK                           0x1
#define UL_DISABLE_HW_CG_CTL_MASK_SFT                       (0x1 << 12)
#define UL_IIR_ON_TMP_CTL_SFT                               10
#define UL_IIR_ON_TMP_CTL_MASK                              0x1
#define UL_IIR_ON_TMP_CTL_MASK_SFT                          (0x1 << 10)
#define UL_IIRMODE_CTL_SFT                                  7
#define UL_IIRMODE_CTL_MASK                                 0x7
#define UL_IIRMODE_CTL_MASK_SFT                             (0x7 << 7)
#define DIGMIC_3P25M_1P625M_SEL_CTL_SFT                     5
#define DIGMIC_3P25M_1P625M_SEL_CTL_MASK                    0x1
#define DIGMIC_3P25M_1P625M_SEL_CTL_MASK_SFT                (0x1 << 5)
#define AGC_260K_SEL_CH2_CTL_SFT                            4
#define AGC_260K_SEL_CH2_CTL_MASK                           0x1
#define AGC_260K_SEL_CH2_CTL_MASK_SFT                       (0x1 << 4)
#define AGC_260K_SEL_CH1_CTL_SFT                            3
#define AGC_260K_SEL_CH1_CTL_MASK                           0x1
#define AGC_260K_SEL_CH1_CTL_MASK_SFT                       (0x1 << 3)
#define UL_LOOP_BACK_MODE_CTL_SFT                           2
#define UL_LOOP_BACK_MODE_CTL_MASK                          0x1
#define UL_LOOP_BACK_MODE_CTL_MASK_SFT                      (0x1 << 2)
#define UL_SDM_3_LEVEL_CTL_SFT                              1
#define UL_SDM_3_LEVEL_CTL_MASK                             0x1
#define UL_SDM_3_LEVEL_CTL_MASK_SFT                         (0x1 << 1)
#define UL_SRC_ON_TMP_CTL_SFT                               0
#define UL_SRC_ON_TMP_CTL_MASK                              0x1
#define UL_SRC_ON_TMP_CTL_MASK_SFT                          (0x1 << 0)

/* AFE_ADDA_UL_SRC_CON1 */
#define C_SDM_RESET_CTL_SFT                                 31
#define C_SDM_RESET_CTL_MASK                                0x1
#define C_SDM_RESET_CTL_MASK_SFT                            (0x1 << 31)
#define ADITHON_CTL_SFT                                     30
#define ADITHON_CTL_MASK                                    0x1
#define ADITHON_CTL_MASK_SFT                                (0x1 << 30)
#define ADITHVAL_CTL_SFT                                    28
#define ADITHVAL_CTL_MASK                                   0x3
#define ADITHVAL_CTL_MASK_SFT                               (0x3 << 28)
#define C_DAC_EN_CTL_SFT                                    27
#define C_DAC_EN_CTL_MASK                                   0x1
#define C_DAC_EN_CTL_MASK_SFT                               (0x1 << 27)
#define C_MUTE_SW_CTL_SFT                                   26
#define C_MUTE_SW_CTL_MASK                                  0x1
#define C_MUTE_SW_CTL_MASK_SFT                              (0x1 << 26)
#define ASDM_SRC_SEL_CTL_SFT                                25
#define ASDM_SRC_SEL_CTL_MASK                               0x1
#define ASDM_SRC_SEL_CTL_MASK_SFT                           (0x1 << 25)
#define C_AMP_DIV_CH2_CTL_SFT                               21
#define C_AMP_DIV_CH2_CTL_MASK                              0x7
#define C_AMP_DIV_CH2_CTL_MASK_SFT                          (0x7 << 21)
#define C_FREQ_DIV_CH2_CTL_SFT                              16
#define C_FREQ_DIV_CH2_CTL_MASK                             0x1f
#define C_FREQ_DIV_CH2_CTL_MASK_SFT                         (0x1f << 16)
#define C_SINE_MODE_CH2_CTL_SFT                             12
#define C_SINE_MODE_CH2_CTL_MASK                            0xf
#define C_SINE_MODE_CH2_CTL_MASK_SFT                        (0xf << 12)
#define C_AMP_DIV_CH1_CTL_SFT                               9
#define C_AMP_DIV_CH1_CTL_MASK                              0x7
#define C_AMP_DIV_CH1_CTL_MASK_SFT                          (0x7 << 9)
#define C_FREQ_DIV_CH1_CTL_SFT                              4
#define C_FREQ_DIV_CH1_CTL_MASK                             0x1f
#define C_FREQ_DIV_CH1_CTL_MASK_SFT                         (0x1f << 4)
#define C_SINE_MODE_CH1_CTL_SFT                             0
#define C_SINE_MODE_CH1_CTL_MASK                            0xf
#define C_SINE_MODE_CH1_CTL_MASK_SFT                        (0xf << 0)

/* AFE_ADDA_TOP_CON0 */
#define C_LOOP_BACK_MODE_CTL_SFT                            12
#define C_LOOP_BACK_MODE_CTL_MASK                           0xf
#define C_LOOP_BACK_MODE_CTL_MASK_SFT                       (0xf << 12)
#define C_EXT_ADC_CTL_SFT                                   0
#define C_EXT_ADC_CTL_MASK                                  0x1
#define C_EXT_ADC_CTL_MASK_SFT                              (0x1 << 0)

/* AFE_ADDA_UL_DL_CON0 */
#define AFE_UL_DL_CON0_RESERVED_SFT                         1
#define AFE_UL_DL_CON0_RESERVED_MASK                        0x3fff
#define AFE_UL_DL_CON0_RESERVED_MASK_SFT                    (0x3fff << 1)
#define ADDA_AFE_ON_SFT                                     0
#define ADDA_AFE_ON_MASK                                    0x1
#define ADDA_AFE_ON_MASK_SFT                                (0x1 << 0)

/* AFE_IRQ_MCU_CON */
#define IRQ7_MCU_MODE_SFT                                   24
#define IRQ7_MCU_MODE_MASK                                  0xf
#define IRQ7_MCU_MODE_MASK_SFT                              (0xf << 24)
#define IRQ4_MCU_MODE_SFT                                   20
#define IRQ4_MCU_MODE_MASK                                  0xf
#define IRQ4_MCU_MODE_MASK_SFT                              (0xf << 20)
#define IRQ3_MCU_MODE_SFT                                   16
#define IRQ3_MCU_MODE_MASK                                  0xf
#define IRQ3_MCU_MODE_MASK_SFT                              (0xf << 16)
#define IRQ7_MCU_ON_SFT                                     14
#define IRQ7_MCU_ON_MASK                                    0x1
#define IRQ7_MCU_ON_MASK_SFT                                (0x1 << 14)
#define IRQ5_MCU_ON_SFT                                     12
#define IRQ5_MCU_ON_MASK                                    0x1
#define IRQ5_MCU_ON_MASK_SFT                                (0x1 << 12)
#define IRQ2_MCU_MODE_SFT                                   8
#define IRQ2_MCU_MODE_MASK                                  0xf
#define IRQ2_MCU_MODE_MASK_SFT                              (0xf << 8)
#define IRQ1_MCU_MODE_SFT                                   4
#define IRQ1_MCU_MODE_MASK                                  0xf
#define IRQ1_MCU_MODE_MASK_SFT                              (0xf << 4)
#define IRQ4_MCU_ON_SFT                                     3
#define IRQ4_MCU_ON_MASK                                    0x1
#define IRQ4_MCU_ON_MASK_SFT                                (0x1 << 3)
#define IRQ3_MCU_ON_SFT                                     2
#define IRQ3_MCU_ON_MASK                                    0x1
#define IRQ3_MCU_ON_MASK_SFT                                (0x1 << 2)
#define IRQ2_MCU_ON_SFT                                     1
#define IRQ2_MCU_ON_MASK                                    0x1
#define IRQ2_MCU_ON_MASK_SFT                                (0x1 << 1)
#define IRQ1_MCU_ON_SFT                                     0
#define IRQ1_MCU_ON_MASK                                    0x1
#define IRQ1_MCU_ON_MASK_SFT                                (0x1 << 0)

/* AFE_IRQ_MCU_EN */
#define AFE_IRQ_CM4_EN_SFT                                  16
#define AFE_IRQ_CM4_EN_MASK                                 0x7f
#define AFE_IRQ_CM4_EN_MASK_SFT                             (0x7f << 16)
#define AFE_IRQ_MD32_EN_SFT                                 8
#define AFE_IRQ_MD32_EN_MASK                                0x7f
#define AFE_IRQ_MD32_EN_MASK_SFT                            (0x7f << 8)
#define AFE_IRQ_MCU_EN_SFT                                  0
#define AFE_IRQ_MCU_EN_MASK                                 0x7f
#define AFE_IRQ_MCU_EN_MASK_SFT                             (0x7f << 0)

/* AFE_IRQ_MCU_CLR */
#define IRQ7_MCU_CLR_SFT                                    6
#define IRQ7_MCU_CLR_MASK                                   0x1
#define IRQ7_MCU_CLR_MASK_SFT                               (0x1 << 6)
#define IRQ5_MCU_CLR_SFT                                    4
#define IRQ5_MCU_CLR_MASK                                   0x1
#define IRQ5_MCU_CLR_MASK_SFT                               (0x1 << 4)
#define IRQ4_MCU_CLR_SFT                                    3
#define IRQ4_MCU_CLR_MASK                                   0x1
#define IRQ4_MCU_CLR_MASK_SFT                               (0x1 << 3)
#define IRQ3_MCU_CLR_SFT                                    2
#define IRQ3_MCU_CLR_MASK                                   0x1
#define IRQ3_MCU_CLR_MASK_SFT                               (0x1 << 2)
#define IRQ2_MCU_CLR_SFT                                    1
#define IRQ2_MCU_CLR_MASK                                   0x1
#define IRQ2_MCU_CLR_MASK_SFT                               (0x1 << 1)
#define IRQ1_MCU_CLR_SFT                                    0
#define IRQ1_MCU_CLR_MASK                                   0x1
#define IRQ1_MCU_CLR_MASK_SFT                               (0x1 << 0)

/* AFE_IRQ_MCU_CNT1 */
#define AFE_IRQ_MCU_CNT1_SFT                                0
#define AFE_IRQ_MCU_CNT1_MASK                               0x3ffff
#define AFE_IRQ_MCU_CNT1_MASK_SFT                           (0x3ffff << 0)

/* AFE_IRQ_MCU_CNT2 */
#define AFE_IRQ_MCU_CNT2_SFT                                0
#define AFE_IRQ_MCU_CNT2_MASK                               0x3ffff
#define AFE_IRQ_MCU_CNT2_MASK_SFT                           (0x3ffff << 0)

/* AFE_IRQ_MCU_CNT3 */
#define AFE_IRQ_MCU_CNT3_SFT                                0
#define AFE_IRQ_MCU_CNT3_MASK                               0x3ffff
#define AFE_IRQ_MCU_CNT3_MASK_SFT                           (0x3ffff << 0)

/* AFE_IRQ_MCU_CNT4 */
#define AFE_IRQ_MCU_CNT4_SFT                                0
#define AFE_IRQ_MCU_CNT4_MASK                               0x3ffff
#define AFE_IRQ_MCU_CNT4_MASK_SFT                           (0x3ffff << 0)

/* AFE_IRQ_MCU_CNT5 */
#define AFE_IRQ_MCU_CNT5_SFT                                0
#define AFE_IRQ_MCU_CNT5_MASK                               0x3ffff
#define AFE_IRQ_MCU_CNT5_MASK_SFT                           (0x3ffff << 0)

/* AFE_IRQ_MCU_CNT7 */
#define AFE_IRQ_MCU_CNT7_SFT                                0
#define AFE_IRQ_MCU_CNT7_MASK                               0x3ffff
#define AFE_IRQ_MCU_CNT7_MASK_SFT                           (0x3ffff << 0)

/* AFE_MEMIF_MSB */
#define CPU_COMPACT_MODE_SFT                                23
#define CPU_COMPACT_MODE_MASK                               0x1
#define CPU_COMPACT_MODE_MASK_SFT                           (0x1 << 23)
#define CPU_HD_ALIGN_SFT                                    22
#define CPU_HD_ALIGN_MASK                                   0x1
#define CPU_HD_ALIGN_MASK_SFT                               (0x1 << 22)

/* AFE_MEMIF_HD_MODE */
#define HDMI_HD_SFT                                         20
#define HDMI_HD_MASK                                        0x3
#define HDMI_HD_MASK_SFT                                    (0x3 << 20)
#define MOD_DAI_HD_SFT                                      18
#define MOD_DAI_HD_MASK                                     0x3
#define MOD_DAI_HD_MASK_SFT                                 (0x3 << 18)
#define DAI_HD_SFT                                          16
#define DAI_HD_MASK                                         0x3
#define DAI_HD_MASK_SFT                                     (0x3 << 16)
#define VUL_DATA2_HD_SFT                                    12
#define VUL_DATA2_HD_MASK                                   0x3
#define VUL_DATA2_HD_MASK_SFT                               (0x3 << 12)
#define VUL_HD_SFT                                          10
#define VUL_HD_MASK                                         0x3
#define VUL_HD_MASK_SFT                                     (0x3 << 10)
#define AWB_HD_SFT                                          8
#define AWB_HD_MASK                                         0x3
#define AWB_HD_MASK_SFT                                     (0x3 << 8)
#define DL3_HD_SFT                                          6
#define DL3_HD_MASK                                         0x3
#define DL3_HD_MASK_SFT                                     (0x3 << 6)
#define DL2_HD_SFT                                          4
#define DL2_HD_MASK                                         0x3
#define DL2_HD_MASK_SFT                                     (0x3 << 4)
#define DL1_DATA2_HD_SFT                                    2
#define DL1_DATA2_HD_MASK                                   0x3
#define DL1_DATA2_HD_MASK_SFT                               (0x3 << 2)
#define DL1_HD_SFT                                          0
#define DL1_HD_MASK                                         0x3
#define DL1_HD_MASK_SFT                                     (0x3 << 0)

/* AFE_MEMIF_HDALIGN */
#define HDMI_NORMAL_MODE_SFT                                26
#define HDMI_NORMAL_MODE_MASK                               0x1
#define HDMI_NORMAL_MODE_MASK_SFT                           (0x1 << 26)
#define MOD_DAI_NORMAL_MODE_SFT                             25
#define MOD_DAI_NORMAL_MODE_MASK                            0x1
#define MOD_DAI_NORMAL_MODE_MASK_SFT                        (0x1 << 25)
#define DAI_NORMAL_MODE_SFT                                 24
#define DAI_NORMAL_MODE_MASK                                0x1
#define DAI_NORMAL_MODE_MASK_SFT                            (0x1 << 24)
#define VUL_DATA2_NORMAL_MODE_SFT                           22
#define VUL_DATA2_NORMAL_MODE_MASK                          0x1
#define VUL_DATA2_NORMAL_MODE_MASK_SFT                      (0x1 << 22)
#define VUL_NORMAL_MODE_SFT                                 21
#define VUL_NORMAL_MODE_MASK                                0x1
#define VUL_NORMAL_MODE_MASK_SFT                            (0x1 << 21)
#define AWB_NORMAL_MODE_SFT                                 20
#define AWB_NORMAL_MODE_MASK                                0x1
#define AWB_NORMAL_MODE_MASK_SFT                            (0x1 << 20)
#define DL3_NORMAL_MODE_SFT                                 19
#define DL3_NORMAL_MODE_MASK                                0x1
#define DL3_NORMAL_MODE_MASK_SFT                            (0x1 << 19)
#define DL2_NORMAL_MODE_SFT                                 18
#define DL2_NORMAL_MODE_MASK                                0x1
#define DL2_NORMAL_MODE_MASK_SFT                            (0x1 << 18)
#define DL1_DATA2_NORMAL_MODE_SFT                           17
#define DL1_DATA2_NORMAL_MODE_MASK                          0x1
#define DL1_DATA2_NORMAL_MODE_MASK_SFT                      (0x1 << 17)
#define DL1_NORMAL_MODE_SFT                                 16
#define DL1_NORMAL_MODE_MASK                                0x1
#define DL1_NORMAL_MODE_MASK_SFT                            (0x1 << 16)
#define HDMI_HD_ALIGN_SFT                                   10
#define HDMI_HD_ALIGN_MASK                                  0x1
#define HDMI_HD_ALIGN_MASK_SFT                              (0x1 << 10)
#define MOD_DAI_HD_ALIGN_SFT                                9
#define MOD_DAI_HD_ALIGN_MASK                               0x1
#define MOD_DAI_HD_ALIGN_MASK_SFT                           (0x1 << 9)
#define DAI_ALIGN_SFT                                       8
#define DAI_ALIGN_MASK                                      0x1
#define DAI_ALIGN_MASK_SFT                                  (0x1 << 8)
#define VUL2_HD_ALIGN_SFT                                   7
#define VUL2_HD_ALIGN_MASK                                  0x1
#define VUL2_HD_ALIGN_MASK_SFT                              (0x1 << 7)
#define VUL_DATA2_HD_ALIGN_SFT                              6
#define VUL_DATA2_HD_ALIGN_MASK                             0x1
#define VUL_DATA2_HD_ALIGN_MASK_SFT                         (0x1 << 6)
#define VUL_HD_ALIGN_SFT                                    5
#define VUL_HD_ALIGN_MASK                                   0x1
#define VUL_HD_ALIGN_MASK_SFT                               (0x1 << 5)
#define AWB_HD_ALIGN_SFT                                    4
#define AWB_HD_ALIGN_MASK                                   0x1
#define AWB_HD_ALIGN_MASK_SFT                               (0x1 << 4)
#define DL3_HD_ALIGN_SFT                                    3
#define DL3_HD_ALIGN_MASK                                   0x1
#define DL3_HD_ALIGN_MASK_SFT                               (0x1 << 3)
#define DL2_HD_ALIGN_SFT                                    2
#define DL2_HD_ALIGN_MASK                                   0x1
#define DL2_HD_ALIGN_MASK_SFT                               (0x1 << 2)
#define DL1_DATA2_HD_ALIGN_SFT                              1
#define DL1_DATA2_HD_ALIGN_MASK                             0x1
#define DL1_DATA2_HD_ALIGN_MASK_SFT                         (0x1 << 1)
#define DL1_HD_ALIGN_SFT                                    0
#define DL1_HD_ALIGN_MASK                                   0x1
#define DL1_HD_ALIGN_MASK_SFT                               (0x1 << 0)

/* PCM_INTF_CON1 */
#define PCM_FIX_VALUE_SEL_SFT                               31
#define PCM_FIX_VALUE_SEL_MASK                              0x1
#define PCM_FIX_VALUE_SEL_MASK_SFT                          (0x1 << 31)
#define PCM_BUFFER_LOOPBACK_SFT                             30
#define PCM_BUFFER_LOOPBACK_MASK                            0x1
#define PCM_BUFFER_LOOPBACK_MASK_SFT                        (0x1 << 30)
#define PCM_PARALLEL_LOOPBACK_SFT                           29
#define PCM_PARALLEL_LOOPBACK_MASK                          0x1
#define PCM_PARALLEL_LOOPBACK_MASK_SFT                      (0x1 << 29)
#define PCM_SERIAL_LOOPBACK_SFT                             28
#define PCM_SERIAL_LOOPBACK_MASK                            0x1
#define PCM_SERIAL_LOOPBACK_MASK_SFT                        (0x1 << 28)
#define PCM_DAI_PCM_LOOPBACK_SFT                            27
#define PCM_DAI_PCM_LOOPBACK_MASK                           0x1
#define PCM_DAI_PCM_LOOPBACK_MASK_SFT                       (0x1 << 27)
#define PCM_I2S_PCM_LOOPBACK_SFT                            26
#define PCM_I2S_PCM_LOOPBACK_MASK                           0x1
#define PCM_I2S_PCM_LOOPBACK_MASK_SFT                       (0x1 << 26)
#define PCM_SYNC_DELSEL_SFT                                 25
#define PCM_SYNC_DELSEL_MASK                                0x1
#define PCM_SYNC_DELSEL_MASK_SFT                            (0x1 << 25)
#define PCM_TX_LR_SWAP_SFT                                  24
#define PCM_TX_LR_SWAP_MASK                                 0x1
#define PCM_TX_LR_SWAP_MASK_SFT                             (0x1 << 24)
#define PCM_SYNC_OUT_INV_SFT                                23
#define PCM_SYNC_OUT_INV_MASK                               0x1
#define PCM_SYNC_OUT_INV_MASK_SFT                           (0x1 << 23)
#define PCM_BCLK_OUT_INV_SFT                                22
#define PCM_BCLK_OUT_INV_MASK                               0x1
#define PCM_BCLK_OUT_INV_MASK_SFT                           (0x1 << 22)
#define PCM_SYNC_IN_INV_SFT                                 21
#define PCM_SYNC_IN_INV_MASK                                0x1
#define PCM_SYNC_IN_INV_MASK_SFT                            (0x1 << 21)
#define PCM_BCLK_IN_INV_SFT                                 20
#define PCM_BCLK_IN_INV_MASK                                0x1
#define PCM_BCLK_IN_INV_MASK_SFT                            (0x1 << 20)
#define PCM_TX_LCH_RPT_SFT                                  19
#define PCM_TX_LCH_RPT_MASK                                 0x1
#define PCM_TX_LCH_RPT_MASK_SFT                             (0x1 << 19)
#define PCM_VBT_16K_MODE_SFT                                18
#define PCM_VBT_16K_MODE_MASK                               0x1
#define PCM_VBT_16K_MODE_MASK_SFT                           (0x1 << 18)
#define PCM_EXT_MODEM_SFT                                   17
#define PCM_EXT_MODEM_MASK                                  0x1
#define PCM_EXT_MODEM_MASK_SFT                              (0x1 << 17)
#define PCM_24BIT_SFT                                       16
#define PCM_24BIT_MASK                                      0x1
#define PCM_24BIT_MASK_SFT                                  (0x1 << 16)
#define PCM_WLEN_SFT                                        14
#define PCM_WLEN_MASK                                       0x3
#define PCM_WLEN_MASK_SFT                                   (0x3 << 14)
#define PCM_SYNC_LENGTH_SFT                                 9
#define PCM_SYNC_LENGTH_MASK                                0x1f
#define PCM_SYNC_LENGTH_MASK_SFT                            (0x1f << 9)
#define PCM_SYNC_TYPE_SFT                                   8
#define PCM_SYNC_TYPE_MASK                                  0x1
#define PCM_SYNC_TYPE_MASK_SFT                              (0x1 << 8)
#define PCM_BT_MODE_SFT                                     7
#define PCM_BT_MODE_MASK                                    0x1
#define PCM_BT_MODE_MASK_SFT                                (0x1 << 7)
#define PCM_BYP_ASRC_SFT                                    6
#define PCM_BYP_ASRC_MASK                                   0x1
#define PCM_BYP_ASRC_MASK_SFT                               (0x1 << 6)
#define PCM_SLAVE_SFT                                       5
#define PCM_SLAVE_MASK                                      0x1
#define PCM_SLAVE_MASK_SFT                                  (0x1 << 5)
#define PCM_MODE_SFT                                        3
#define PCM_MODE_MASK                                       0x3
#define PCM_MODE_MASK_SFT                                   (0x3 << 3)
#define PCM_FMT_SFT                                         1
#define PCM_FMT_MASK                                        0x3
#define PCM_FMT_MASK_SFT                                    (0x3 << 1)
#define PCM_EN_SFT                                          0
#define PCM_EN_MASK                                         0x1
#define PCM_EN_MASK_SFT                                     (0x1 << 0)

/* PCM_INTF_CON2 */
#define PCM1_TX_FIFO_OV_SFT                                 31
#define PCM1_TX_FIFO_OV_MASK                                0x1
#define PCM1_TX_FIFO_OV_MASK_SFT                            (0x1 << 31)
#define PCM1_RX_FIFO_OV_SFT                                 30
#define PCM1_RX_FIFO_OV_MASK                                0x1
#define PCM1_RX_FIFO_OV_MASK_SFT                            (0x1 << 30)
#define PCM2_TX_FIFO_OV_SFT                                 29
#define PCM2_TX_FIFO_OV_MASK                                0x1
#define PCM2_TX_FIFO_OV_MASK_SFT                            (0x1 << 29)
#define PCM2_RX_FIFO_OV_SFT                                 28
#define PCM2_RX_FIFO_OV_MASK                                0x1
#define PCM2_RX_FIFO_OV_MASK_SFT                            (0x1 << 28)
#define PCM1_SYNC_GLITCH_SFT                                27
#define PCM1_SYNC_GLITCH_MASK                               0x1
#define PCM1_SYNC_GLITCH_MASK_SFT                           (0x1 << 27)
#define PCM2_SYNC_GLITCH_SFT                                26
#define PCM2_SYNC_GLITCH_MASK                               0x1
#define PCM2_SYNC_GLITCH_MASK_SFT                           (0x1 << 26)
#define PCM1_PCM2_LOOPBACK_SFT                              15
#define PCM1_PCM2_LOOPBACK_MASK                             0x1
#define PCM1_PCM2_LOOPBACK_MASK_SFT                         (0x1 << 15)
#define DAI_PCM_LOOPBACK_CH_SFT                             13
#define DAI_PCM_LOOPBACK_CH_MASK                            0x1
#define DAI_PCM_LOOPBACK_CH_MASK_SFT                        (0x1 << 13)
#define I2S_PCM_LOOPBACK_CH_SFT                             12
#define I2S_PCM_LOOPBACK_CH_MASK                            0x1
#define I2S_PCM_LOOPBACK_CH_MASK_SFT                        (0x1 << 12)
#define PCM_USE_MD3_SFT                                     8
#define PCM_USE_MD3_MASK                                    0x1
#define PCM_USE_MD3_MASK_SFT                                (0x1 << 8)
#define TX_FIX_VALUE_SFT                                    0
#define TX_FIX_VALUE_MASK                                   0xff
#define TX_FIX_VALUE_MASK_SFT                               (0xff << 0)

/* PCM2_INTF_CON */
#define PCM2_TX_FIX_VALUE_SFT                                24
#define PCM2_TX_FIX_VALUE_MASK                               0xff
#define PCM2_TX_FIX_VALUE_MASK_SFT                           (0xff << 24)
#define PCM2_FIX_VALUE_SEL_SFT                               23
#define PCM2_FIX_VALUE_SEL_MASK                              0x1
#define PCM2_FIX_VALUE_SEL_MASK_SFT                          (0x1 << 23)
#define PCM2_BUFFER_LOOPBACK_SFT                             22
#define PCM2_BUFFER_LOOPBACK_MASK                            0x1
#define PCM2_BUFFER_LOOPBACK_MASK_SFT                        (0x1 << 22)
#define PCM2_PARALLEL_LOOPBACK_SFT                           21
#define PCM2_PARALLEL_LOOPBACK_MASK                          0x1
#define PCM2_PARALLEL_LOOPBACK_MASK_SFT                      (0x1 << 21)
#define PCM2_SERIAL_LOOPBACK_SFT                             20
#define PCM2_SERIAL_LOOPBACK_MASK                            0x1
#define PCM2_SERIAL_LOOPBACK_MASK_SFT                        (0x1 << 20)
#define PCM2_DAI_PCM_LOOPBACK_SFT                            19
#define PCM2_DAI_PCM_LOOPBACK_MASK                           0x1
#define PCM2_DAI_PCM_LOOPBACK_MASK_SFT                       (0x1 << 19)
#define PCM2_I2S_PCM_LOOPBACK_SFT                            18
#define PCM2_I2S_PCM_LOOPBACK_MASK                           0x1
#define PCM2_I2S_PCM_LOOPBACK_MASK_SFT                       (0x1 << 18)
#define PCM2_SYNC_DELSEL_SFT                                 17
#define PCM2_SYNC_DELSEL_MASK                                0x1
#define PCM2_SYNC_DELSEL_MASK_SFT                            (0x1 << 17)
#define PCM2_TX_LR_SWAP_SFT                                  16
#define PCM2_TX_LR_SWAP_MASK                                 0x1
#define PCM2_TX_LR_SWAP_MASK_SFT                             (0x1 << 16)
#define PCM2_SYNC_IN_INV_SFT                                 15
#define PCM2_SYNC_IN_INV_MASK                                0x1
#define PCM2_SYNC_IN_INV_MASK_SFT                            (0x1 << 15)
#define PCM2_BCLK_IN_INV_SFT                                 14
#define PCM2_BCLK_IN_INV_MASK                                0x1
#define PCM2_BCLK_IN_INV_MASK_SFT                            (0x1 << 14)
#define PCM2_TX_LCH_RPT_SFT                                  13
#define PCM2_TX_LCH_RPT_MASK                                 0x1
#define PCM2_TX_LCH_RPT_MASK_SFT                             (0x1 << 13)
#define PCM2_VBT_16K_MODE_SFT                                12
#define PCM2_VBT_16K_MODE_MASK                               0x1
#define PCM2_VBT_16K_MODE_MASK_SFT                           (0x1 << 12)
#define PCM2_LOOPBACK_CH_SEL_SFT                             10
#define PCM2_LOOPBACK_CH_SEL_MASK                            0x3
#define PCM2_LOOPBACK_CH_SEL_MASK_SFT                        (0x3 << 10)
#define PCM2_TX2_BT_MODE_SFT                                 8
#define PCM2_TX2_BT_MODE_MASK                                0x1
#define PCM2_TX2_BT_MODE_MASK_SFT                            (0x1 << 8)
#define PCM2_BT_MODE_SFT                                     7
#define PCM2_BT_MODE_MASK                                    0x1
#define PCM2_BT_MODE_MASK_SFT                                (0x1 << 7)
#define PCM2_AFIFO_SFT                                       6
#define PCM2_AFIFO_MASK                                      0x1
#define PCM2_AFIFO_MASK_SFT                                  (0x1 << 6)
#define PCM2_WLEN_SFT                                        5
#define PCM2_WLEN_MASK                                       0x1
#define PCM2_WLEN_MASK_SFT                                   (0x1 << 5)
#define PCM2_MODE_SFT                                        3
#define PCM2_MODE_MASK                                       0x3
#define PCM2_MODE_MASK_SFT                                   (0x3 << 3)
#define PCM2_FMT_SFT                                         1
#define PCM2_FMT_MASK                                        0x3
#define PCM2_FMT_MASK_SFT                                    (0x3 << 1)
#define PCM2_EN_SFT                                          0
#define PCM2_EN_MASK                                         0x1
#define PCM2_EN_MASK_SFT                                     (0x1 << 0)
#endif
