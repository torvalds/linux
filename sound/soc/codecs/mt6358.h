/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt6358.h  --  mt6358 ALSA SoC audio codec driver
 *
 * Copyright (c) 2018 MediaTek Inc.
 * Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>
 */

#ifndef __MT6358_H__
#define __MT6358_H__

/* Reg bit define */
/* MT6358_DCXO_CW14 */
#define RG_XO_AUDIO_EN_M_SFT 13

/* MT6358_DCXO_CW13 */
#define RG_XO_VOW_EN_SFT 8

/* MT6358_AUD_TOP_CKPDN_CON0 */
#define RG_VOW13M_CK_PDN_SFT                              13
#define RG_VOW13M_CK_PDN_MASK                             0x1
#define RG_VOW13M_CK_PDN_MASK_SFT                         (0x1 << 13)
#define RG_VOW32K_CK_PDN_SFT                              12
#define RG_VOW32K_CK_PDN_MASK                             0x1
#define RG_VOW32K_CK_PDN_MASK_SFT                         (0x1 << 12)
#define RG_AUD_INTRP_CK_PDN_SFT                           8
#define RG_AUD_INTRP_CK_PDN_MASK                          0x1
#define RG_AUD_INTRP_CK_PDN_MASK_SFT                      (0x1 << 8)
#define RG_PAD_AUD_CLK_MISO_CK_PDN_SFT                    7
#define RG_PAD_AUD_CLK_MISO_CK_PDN_MASK                   0x1
#define RG_PAD_AUD_CLK_MISO_CK_PDN_MASK_SFT               (0x1 << 7)
#define RG_AUDNCP_CK_PDN_SFT                              6
#define RG_AUDNCP_CK_PDN_MASK                             0x1
#define RG_AUDNCP_CK_PDN_MASK_SFT                         (0x1 << 6)
#define RG_ZCD13M_CK_PDN_SFT                              5
#define RG_ZCD13M_CK_PDN_MASK                             0x1
#define RG_ZCD13M_CK_PDN_MASK_SFT                         (0x1 << 5)
#define RG_AUDIF_CK_PDN_SFT                               2
#define RG_AUDIF_CK_PDN_MASK                              0x1
#define RG_AUDIF_CK_PDN_MASK_SFT                          (0x1 << 2)
#define RG_AUD_CK_PDN_SFT                                 1
#define RG_AUD_CK_PDN_MASK                                0x1
#define RG_AUD_CK_PDN_MASK_SFT                            (0x1 << 1)
#define RG_ACCDET_CK_PDN_SFT                              0
#define RG_ACCDET_CK_PDN_MASK                             0x1
#define RG_ACCDET_CK_PDN_MASK_SFT                         (0x1 << 0)

/* MT6358_AUD_TOP_CKPDN_CON0_SET */
#define RG_AUD_TOP_CKPDN_CON0_SET_SFT                     0
#define RG_AUD_TOP_CKPDN_CON0_SET_MASK                    0x3fff
#define RG_AUD_TOP_CKPDN_CON0_SET_MASK_SFT                (0x3fff << 0)

/* MT6358_AUD_TOP_CKPDN_CON0_CLR */
#define RG_AUD_TOP_CKPDN_CON0_CLR_SFT                     0
#define RG_AUD_TOP_CKPDN_CON0_CLR_MASK                    0x3fff
#define RG_AUD_TOP_CKPDN_CON0_CLR_MASK_SFT                (0x3fff << 0)

/* MT6358_AUD_TOP_CKSEL_CON0 */
#define RG_AUDIF_CK_CKSEL_SFT                             3
#define RG_AUDIF_CK_CKSEL_MASK                            0x1
#define RG_AUDIF_CK_CKSEL_MASK_SFT                        (0x1 << 3)
#define RG_AUD_CK_CKSEL_SFT                               2
#define RG_AUD_CK_CKSEL_MASK                              0x1
#define RG_AUD_CK_CKSEL_MASK_SFT                          (0x1 << 2)

/* MT6358_AUD_TOP_CKSEL_CON0_SET */
#define RG_AUD_TOP_CKSEL_CON0_SET_SFT                     0
#define RG_AUD_TOP_CKSEL_CON0_SET_MASK                    0xf
#define RG_AUD_TOP_CKSEL_CON0_SET_MASK_SFT                (0xf << 0)

/* MT6358_AUD_TOP_CKSEL_CON0_CLR */
#define RG_AUD_TOP_CKSEL_CON0_CLR_SFT                     0
#define RG_AUD_TOP_CKSEL_CON0_CLR_MASK                    0xf
#define RG_AUD_TOP_CKSEL_CON0_CLR_MASK_SFT                (0xf << 0)

/* MT6358_AUD_TOP_CKTST_CON0 */
#define RG_VOW13M_CK_TSTSEL_SFT                           9
#define RG_VOW13M_CK_TSTSEL_MASK                          0x1
#define RG_VOW13M_CK_TSTSEL_MASK_SFT                      (0x1 << 9)
#define RG_VOW13M_CK_TST_DIS_SFT                          8
#define RG_VOW13M_CK_TST_DIS_MASK                         0x1
#define RG_VOW13M_CK_TST_DIS_MASK_SFT                     (0x1 << 8)
#define RG_AUD26M_CK_TSTSEL_SFT                           4
#define RG_AUD26M_CK_TSTSEL_MASK                          0x1
#define RG_AUD26M_CK_TSTSEL_MASK_SFT                      (0x1 << 4)
#define RG_AUDIF_CK_TSTSEL_SFT                            3
#define RG_AUDIF_CK_TSTSEL_MASK                           0x1
#define RG_AUDIF_CK_TSTSEL_MASK_SFT                       (0x1 << 3)
#define RG_AUD_CK_TSTSEL_SFT                              2
#define RG_AUD_CK_TSTSEL_MASK                             0x1
#define RG_AUD_CK_TSTSEL_MASK_SFT                         (0x1 << 2)
#define RG_AUD26M_CK_TST_DIS_SFT                          0
#define RG_AUD26M_CK_TST_DIS_MASK                         0x1
#define RG_AUD26M_CK_TST_DIS_MASK_SFT                     (0x1 << 0)

/* MT6358_AUD_TOP_CLK_HWEN_CON0 */
#define RG_AUD_INTRP_CK_PDN_HWEN_SFT                      0
#define RG_AUD_INTRP_CK_PDN_HWEN_MASK                     0x1
#define RG_AUD_INTRP_CK_PDN_HWEN_MASK_SFT                 (0x1 << 0)

/* MT6358_AUD_TOP_CLK_HWEN_CON0_SET */
#define RG_AUD_INTRP_CK_PND_HWEN_CON0_SET_SFT             0
#define RG_AUD_INTRP_CK_PND_HWEN_CON0_SET_MASK            0xffff
#define RG_AUD_INTRP_CK_PND_HWEN_CON0_SET_MASK_SFT        (0xffff << 0)

/* MT6358_AUD_TOP_CLK_HWEN_CON0_CLR */
#define RG_AUD_INTRP_CLK_PDN_HWEN_CON0_CLR_SFT            0
#define RG_AUD_INTRP_CLK_PDN_HWEN_CON0_CLR_MASK           0xffff
#define RG_AUD_INTRP_CLK_PDN_HWEN_CON0_CLR_MASK_SFT       (0xffff << 0)

/* MT6358_AUD_TOP_RST_CON0 */
#define RG_AUDNCP_RST_SFT                                 3
#define RG_AUDNCP_RST_MASK                                0x1
#define RG_AUDNCP_RST_MASK_SFT                            (0x1 << 3)
#define RG_ZCD_RST_SFT                                    2
#define RG_ZCD_RST_MASK                                   0x1
#define RG_ZCD_RST_MASK_SFT                               (0x1 << 2)
#define RG_ACCDET_RST_SFT                                 1
#define RG_ACCDET_RST_MASK                                0x1
#define RG_ACCDET_RST_MASK_SFT                            (0x1 << 1)
#define RG_AUDIO_RST_SFT                                  0
#define RG_AUDIO_RST_MASK                                 0x1
#define RG_AUDIO_RST_MASK_SFT                             (0x1 << 0)

/* MT6358_AUD_TOP_RST_CON0_SET */
#define RG_AUD_TOP_RST_CON0_SET_SFT                       0
#define RG_AUD_TOP_RST_CON0_SET_MASK                      0xf
#define RG_AUD_TOP_RST_CON0_SET_MASK_SFT                  (0xf << 0)

/* MT6358_AUD_TOP_RST_CON0_CLR */
#define RG_AUD_TOP_RST_CON0_CLR_SFT                       0
#define RG_AUD_TOP_RST_CON0_CLR_MASK                      0xf
#define RG_AUD_TOP_RST_CON0_CLR_MASK_SFT                  (0xf << 0)

/* MT6358_AUD_TOP_RST_BANK_CON0 */
#define BANK_AUDZCD_SWRST_SFT                             2
#define BANK_AUDZCD_SWRST_MASK                            0x1
#define BANK_AUDZCD_SWRST_MASK_SFT                        (0x1 << 2)
#define BANK_AUDIO_SWRST_SFT                              1
#define BANK_AUDIO_SWRST_MASK                             0x1
#define BANK_AUDIO_SWRST_MASK_SFT                         (0x1 << 1)
#define BANK_ACCDET_SWRST_SFT                             0
#define BANK_ACCDET_SWRST_MASK                            0x1
#define BANK_ACCDET_SWRST_MASK_SFT                        (0x1 << 0)

/* MT6358_AUD_TOP_INT_CON0 */
#define RG_INT_EN_AUDIO_SFT                               0
#define RG_INT_EN_AUDIO_MASK                              0x1
#define RG_INT_EN_AUDIO_MASK_SFT                          (0x1 << 0)
#define RG_INT_EN_ACCDET_SFT                              5
#define RG_INT_EN_ACCDET_MASK                             0x1
#define RG_INT_EN_ACCDET_MASK_SFT                         (0x1 << 5)
#define RG_INT_EN_ACCDET_EINT0_SFT                        6
#define RG_INT_EN_ACCDET_EINT0_MASK                       0x1
#define RG_INT_EN_ACCDET_EINT0_MASK_SFT                   (0x1 << 6)
#define RG_INT_EN_ACCDET_EINT1_SFT                        7
#define RG_INT_EN_ACCDET_EINT1_MASK                       0x1
#define RG_INT_EN_ACCDET_EINT1_MASK_SFT                   (0x1 << 7)

/* MT6358_AUD_TOP_INT_CON0_SET */
#define RG_AUD_INT_CON0_SET_SFT                           0
#define RG_AUD_INT_CON0_SET_MASK                          0xffff
#define RG_AUD_INT_CON0_SET_MASK_SFT                      (0xffff << 0)

/* MT6358_AUD_TOP_INT_CON0_CLR */
#define RG_AUD_INT_CON0_CLR_SFT                           0
#define RG_AUD_INT_CON0_CLR_MASK                          0xffff
#define RG_AUD_INT_CON0_CLR_MASK_SFT                      (0xffff << 0)

/* MT6358_AUD_TOP_INT_MASK_CON0 */
#define RG_INT_MASK_AUDIO_SFT                             0
#define RG_INT_MASK_AUDIO_MASK                            0x1
#define RG_INT_MASK_AUDIO_MASK_SFT                        (0x1 << 0)
#define RG_INT_MASK_ACCDET_SFT                            5
#define RG_INT_MASK_ACCDET_MASK                           0x1
#define RG_INT_MASK_ACCDET_MASK_SFT                       (0x1 << 5)
#define RG_INT_MASK_ACCDET_EINT0_SFT                      6
#define RG_INT_MASK_ACCDET_EINT0_MASK                     0x1
#define RG_INT_MASK_ACCDET_EINT0_MASK_SFT                 (0x1 << 6)
#define RG_INT_MASK_ACCDET_EINT1_SFT                      7
#define RG_INT_MASK_ACCDET_EINT1_MASK                     0x1
#define RG_INT_MASK_ACCDET_EINT1_MASK_SFT                 (0x1 << 7)

/* MT6358_AUD_TOP_INT_MASK_CON0_SET */
#define RG_AUD_INT_MASK_CON0_SET_SFT                      0
#define RG_AUD_INT_MASK_CON0_SET_MASK                     0xff
#define RG_AUD_INT_MASK_CON0_SET_MASK_SFT                 (0xff << 0)

/* MT6358_AUD_TOP_INT_MASK_CON0_CLR */
#define RG_AUD_INT_MASK_CON0_CLR_SFT                      0
#define RG_AUD_INT_MASK_CON0_CLR_MASK                     0xff
#define RG_AUD_INT_MASK_CON0_CLR_MASK_SFT                 (0xff << 0)

/* MT6358_AUD_TOP_INT_STATUS0 */
#define RG_INT_STATUS_AUDIO_SFT                           0
#define RG_INT_STATUS_AUDIO_MASK                          0x1
#define RG_INT_STATUS_AUDIO_MASK_SFT                      (0x1 << 0)
#define RG_INT_STATUS_ACCDET_SFT                          5
#define RG_INT_STATUS_ACCDET_MASK                         0x1
#define RG_INT_STATUS_ACCDET_MASK_SFT                     (0x1 << 5)
#define RG_INT_STATUS_ACCDET_EINT0_SFT                    6
#define RG_INT_STATUS_ACCDET_EINT0_MASK                   0x1
#define RG_INT_STATUS_ACCDET_EINT0_MASK_SFT               (0x1 << 6)
#define RG_INT_STATUS_ACCDET_EINT1_SFT                    7
#define RG_INT_STATUS_ACCDET_EINT1_MASK                   0x1
#define RG_INT_STATUS_ACCDET_EINT1_MASK_SFT               (0x1 << 7)

/* MT6358_AUD_TOP_INT_RAW_STATUS0 */
#define RG_INT_RAW_STATUS_AUDIO_SFT                       0
#define RG_INT_RAW_STATUS_AUDIO_MASK                      0x1
#define RG_INT_RAW_STATUS_AUDIO_MASK_SFT                  (0x1 << 0)
#define RG_INT_RAW_STATUS_ACCDET_SFT                      5
#define RG_INT_RAW_STATUS_ACCDET_MASK                     0x1
#define RG_INT_RAW_STATUS_ACCDET_MASK_SFT                 (0x1 << 5)
#define RG_INT_RAW_STATUS_ACCDET_EINT0_SFT                6
#define RG_INT_RAW_STATUS_ACCDET_EINT0_MASK               0x1
#define RG_INT_RAW_STATUS_ACCDET_EINT0_MASK_SFT           (0x1 << 6)
#define RG_INT_RAW_STATUS_ACCDET_EINT1_SFT                7
#define RG_INT_RAW_STATUS_ACCDET_EINT1_MASK               0x1
#define RG_INT_RAW_STATUS_ACCDET_EINT1_MASK_SFT           (0x1 << 7)

/* MT6358_AUD_TOP_INT_MISC_CON0 */
#define RG_AUD_TOP_INT_POLARITY_SFT                       0
#define RG_AUD_TOP_INT_POLARITY_MASK                      0x1
#define RG_AUD_TOP_INT_POLARITY_MASK_SFT                  (0x1 << 0)

/* MT6358_AUDNCP_CLKDIV_CON0 */
#define RG_DIVCKS_CHG_SFT                                 0
#define RG_DIVCKS_CHG_MASK                                0x1
#define RG_DIVCKS_CHG_MASK_SFT                            (0x1 << 0)

/* MT6358_AUDNCP_CLKDIV_CON1 */
#define RG_DIVCKS_ON_SFT                                  0
#define RG_DIVCKS_ON_MASK                                 0x1
#define RG_DIVCKS_ON_MASK_SFT                             (0x1 << 0)

/* MT6358_AUDNCP_CLKDIV_CON2 */
#define RG_DIVCKS_PRG_SFT                                 0
#define RG_DIVCKS_PRG_MASK                                0x1ff
#define RG_DIVCKS_PRG_MASK_SFT                            (0x1ff << 0)

/* MT6358_AUDNCP_CLKDIV_CON3 */
#define RG_DIVCKS_PWD_NCP_SFT                             0
#define RG_DIVCKS_PWD_NCP_MASK                            0x1
#define RG_DIVCKS_PWD_NCP_MASK_SFT                        (0x1 << 0)

/* MT6358_AUDNCP_CLKDIV_CON4 */
#define RG_DIVCKS_PWD_NCP_ST_SEL_SFT                      0
#define RG_DIVCKS_PWD_NCP_ST_SEL_MASK                     0x3
#define RG_DIVCKS_PWD_NCP_ST_SEL_MASK_SFT                 (0x3 << 0)

/* MT6358_AUD_TOP_MON_CON0 */
#define RG_AUD_TOP_MON_SEL_SFT                            0
#define RG_AUD_TOP_MON_SEL_MASK                           0x7
#define RG_AUD_TOP_MON_SEL_MASK_SFT                       (0x7 << 0)
#define RG_AUD_CLK_INT_MON_FLAG_SEL_SFT                   3
#define RG_AUD_CLK_INT_MON_FLAG_SEL_MASK                  0xff
#define RG_AUD_CLK_INT_MON_FLAG_SEL_MASK_SFT              (0xff << 3)
#define RG_AUD_CLK_INT_MON_FLAG_EN_SFT                    11
#define RG_AUD_CLK_INT_MON_FLAG_EN_MASK                   0x1
#define RG_AUD_CLK_INT_MON_FLAG_EN_MASK_SFT               (0x1 << 11)

/* MT6358_AUDIO_DIG_DSN_ID */
#define AUDIO_DIG_ANA_ID_SFT                              0
#define AUDIO_DIG_ANA_ID_MASK                             0xff
#define AUDIO_DIG_ANA_ID_MASK_SFT                         (0xff << 0)
#define AUDIO_DIG_DIG_ID_SFT                              8
#define AUDIO_DIG_DIG_ID_MASK                             0xff
#define AUDIO_DIG_DIG_ID_MASK_SFT                         (0xff << 8)

/* MT6358_AUDIO_DIG_DSN_REV0 */
#define AUDIO_DIG_ANA_MINOR_REV_SFT                       0
#define AUDIO_DIG_ANA_MINOR_REV_MASK                      0xf
#define AUDIO_DIG_ANA_MINOR_REV_MASK_SFT                  (0xf << 0)
#define AUDIO_DIG_ANA_MAJOR_REV_SFT                       4
#define AUDIO_DIG_ANA_MAJOR_REV_MASK                      0xf
#define AUDIO_DIG_ANA_MAJOR_REV_MASK_SFT                  (0xf << 4)
#define AUDIO_DIG_DIG_MINOR_REV_SFT                       8
#define AUDIO_DIG_DIG_MINOR_REV_MASK                      0xf
#define AUDIO_DIG_DIG_MINOR_REV_MASK_SFT                  (0xf << 8)
#define AUDIO_DIG_DIG_MAJOR_REV_SFT                       12
#define AUDIO_DIG_DIG_MAJOR_REV_MASK                      0xf
#define AUDIO_DIG_DIG_MAJOR_REV_MASK_SFT                  (0xf << 12)

/* MT6358_AUDIO_DIG_DSN_DBI */
#define AUDIO_DIG_DSN_CBS_SFT                             0
#define AUDIO_DIG_DSN_CBS_MASK                            0x3
#define AUDIO_DIG_DSN_CBS_MASK_SFT                        (0x3 << 0)
#define AUDIO_DIG_DSN_BIX_SFT                             2
#define AUDIO_DIG_DSN_BIX_MASK                            0x3
#define AUDIO_DIG_DSN_BIX_MASK_SFT                        (0x3 << 2)
#define AUDIO_DIG_ESP_SFT                                 8
#define AUDIO_DIG_ESP_MASK                                0xff
#define AUDIO_DIG_ESP_MASK_SFT                            (0xff << 8)

/* MT6358_AUDIO_DIG_DSN_DXI */
#define AUDIO_DIG_DSN_FPI_SFT                             0
#define AUDIO_DIG_DSN_FPI_MASK                            0xff
#define AUDIO_DIG_DSN_FPI_MASK_SFT                        (0xff << 0)

/* MT6358_AFE_UL_DL_CON0 */
#define AFE_UL_LR_SWAP_SFT                                15
#define AFE_UL_LR_SWAP_MASK                               0x1
#define AFE_UL_LR_SWAP_MASK_SFT                           (0x1 << 15)
#define AFE_DL_LR_SWAP_SFT                                14
#define AFE_DL_LR_SWAP_MASK                               0x1
#define AFE_DL_LR_SWAP_MASK_SFT                           (0x1 << 14)
#define AFE_ON_SFT                                        0
#define AFE_ON_MASK                                       0x1
#define AFE_ON_MASK_SFT                                   (0x1 << 0)

/* MT6358_AFE_DL_SRC2_CON0_L */
#define DL_2_SRC_ON_TMP_CTL_PRE_SFT                       0
#define DL_2_SRC_ON_TMP_CTL_PRE_MASK                      0x1
#define DL_2_SRC_ON_TMP_CTL_PRE_MASK_SFT                  (0x1 << 0)

/* MT6358_AFE_UL_SRC_CON0_H */
#define C_DIGMIC_PHASE_SEL_CH1_CTL_SFT                    11
#define C_DIGMIC_PHASE_SEL_CH1_CTL_MASK                   0x7
#define C_DIGMIC_PHASE_SEL_CH1_CTL_MASK_SFT               (0x7 << 11)
#define C_DIGMIC_PHASE_SEL_CH2_CTL_SFT                    8
#define C_DIGMIC_PHASE_SEL_CH2_CTL_MASK                   0x7
#define C_DIGMIC_PHASE_SEL_CH2_CTL_MASK_SFT               (0x7 << 8)
#define C_TWO_DIGITAL_MIC_CTL_SFT                         7
#define C_TWO_DIGITAL_MIC_CTL_MASK                        0x1
#define C_TWO_DIGITAL_MIC_CTL_MASK_SFT                    (0x1 << 7)

/* MT6358_AFE_UL_SRC_CON0_L */
#define DMIC_LOW_POWER_MODE_CTL_SFT                       14
#define DMIC_LOW_POWER_MODE_CTL_MASK                      0x3
#define DMIC_LOW_POWER_MODE_CTL_MASK_SFT                  (0x3 << 14)
#define DIGMIC_3P25M_1P625M_SEL_CTL_SFT                   5
#define DIGMIC_3P25M_1P625M_SEL_CTL_MASK                  0x1
#define DIGMIC_3P25M_1P625M_SEL_CTL_MASK_SFT              (0x1 << 5)
#define UL_LOOP_BACK_MODE_CTL_SFT                         2
#define UL_LOOP_BACK_MODE_CTL_MASK                        0x1
#define UL_LOOP_BACK_MODE_CTL_MASK_SFT                    (0x1 << 2)
#define UL_SDM_3_LEVEL_CTL_SFT                            1
#define UL_SDM_3_LEVEL_CTL_MASK                           0x1
#define UL_SDM_3_LEVEL_CTL_MASK_SFT                       (0x1 << 1)
#define UL_SRC_ON_TMP_CTL_SFT                             0
#define UL_SRC_ON_TMP_CTL_MASK                            0x1
#define UL_SRC_ON_TMP_CTL_MASK_SFT                        (0x1 << 0)

/* MT6358_AFE_TOP_CON0 */
#define MTKAIF_SINE_ON_SFT                                2
#define MTKAIF_SINE_ON_MASK                               0x1
#define MTKAIF_SINE_ON_MASK_SFT                           (0x1 << 2)
#define UL_SINE_ON_SFT                                    1
#define UL_SINE_ON_MASK                                   0x1
#define UL_SINE_ON_MASK_SFT                               (0x1 << 1)
#define DL_SINE_ON_SFT                                    0
#define DL_SINE_ON_MASK                                   0x1
#define DL_SINE_ON_MASK_SFT                               (0x1 << 0)

/* MT6358_AUDIO_TOP_CON0 */
#define PDN_AFE_CTL_SFT                                   7
#define PDN_AFE_CTL_MASK                                  0x1
#define PDN_AFE_CTL_MASK_SFT                              (0x1 << 7)
#define PDN_DAC_CTL_SFT                                   6
#define PDN_DAC_CTL_MASK                                  0x1
#define PDN_DAC_CTL_MASK_SFT                              (0x1 << 6)
#define PDN_ADC_CTL_SFT                                   5
#define PDN_ADC_CTL_MASK                                  0x1
#define PDN_ADC_CTL_MASK_SFT                              (0x1 << 5)
#define PDN_I2S_DL_CTL_SFT                                3
#define PDN_I2S_DL_CTL_MASK                               0x1
#define PDN_I2S_DL_CTL_MASK_SFT                           (0x1 << 3)
#define PWR_CLK_DIS_CTL_SFT                               2
#define PWR_CLK_DIS_CTL_MASK                              0x1
#define PWR_CLK_DIS_CTL_MASK_SFT                          (0x1 << 2)
#define PDN_AFE_TESTMODEL_CTL_SFT                         1
#define PDN_AFE_TESTMODEL_CTL_MASK                        0x1
#define PDN_AFE_TESTMODEL_CTL_MASK_SFT                    (0x1 << 1)
#define PDN_RESERVED_SFT                                  0
#define PDN_RESERVED_MASK                                 0x1
#define PDN_RESERVED_MASK_SFT                             (0x1 << 0)

/* MT6358_AFE_MON_DEBUG0 */
#define AUDIO_SYS_TOP_MON_SWAP_SFT                        14
#define AUDIO_SYS_TOP_MON_SWAP_MASK                       0x3
#define AUDIO_SYS_TOP_MON_SWAP_MASK_SFT                   (0x3 << 14)
#define AUDIO_SYS_TOP_MON_SEL_SFT                         8
#define AUDIO_SYS_TOP_MON_SEL_MASK                        0x1f
#define AUDIO_SYS_TOP_MON_SEL_MASK_SFT                    (0x1f << 8)
#define AFE_MON_SEL_SFT                                   0
#define AFE_MON_SEL_MASK                                  0xff
#define AFE_MON_SEL_MASK_SFT                              (0xff << 0)

/* MT6358_AFUNC_AUD_CON0 */
#define CCI_AUD_ANACK_SEL_SFT                             15
#define CCI_AUD_ANACK_SEL_MASK                            0x1
#define CCI_AUD_ANACK_SEL_MASK_SFT                        (0x1 << 15)
#define CCI_AUDIO_FIFO_WPTR_SFT                           12
#define CCI_AUDIO_FIFO_WPTR_MASK                          0x7
#define CCI_AUDIO_FIFO_WPTR_MASK_SFT                      (0x7 << 12)
#define CCI_SCRAMBLER_CG_EN_SFT                           11
#define CCI_SCRAMBLER_CG_EN_MASK                          0x1
#define CCI_SCRAMBLER_CG_EN_MASK_SFT                      (0x1 << 11)
#define CCI_LCH_INV_SFT                                   10
#define CCI_LCH_INV_MASK                                  0x1
#define CCI_LCH_INV_MASK_SFT                              (0x1 << 10)
#define CCI_RAND_EN_SFT                                   9
#define CCI_RAND_EN_MASK                                  0x1
#define CCI_RAND_EN_MASK_SFT                              (0x1 << 9)
#define CCI_SPLT_SCRMB_CLK_ON_SFT                         8
#define CCI_SPLT_SCRMB_CLK_ON_MASK                        0x1
#define CCI_SPLT_SCRMB_CLK_ON_MASK_SFT                    (0x1 << 8)
#define CCI_SPLT_SCRMB_ON_SFT                             7
#define CCI_SPLT_SCRMB_ON_MASK                            0x1
#define CCI_SPLT_SCRMB_ON_MASK_SFT                        (0x1 << 7)
#define CCI_AUD_IDAC_TEST_EN_SFT                          6
#define CCI_AUD_IDAC_TEST_EN_MASK                         0x1
#define CCI_AUD_IDAC_TEST_EN_MASK_SFT                     (0x1 << 6)
#define CCI_ZERO_PAD_DISABLE_SFT                          5
#define CCI_ZERO_PAD_DISABLE_MASK                         0x1
#define CCI_ZERO_PAD_DISABLE_MASK_SFT                     (0x1 << 5)
#define CCI_AUD_SPLIT_TEST_EN_SFT                         4
#define CCI_AUD_SPLIT_TEST_EN_MASK                        0x1
#define CCI_AUD_SPLIT_TEST_EN_MASK_SFT                    (0x1 << 4)
#define CCI_AUD_SDM_MUTEL_SFT                             3
#define CCI_AUD_SDM_MUTEL_MASK                            0x1
#define CCI_AUD_SDM_MUTEL_MASK_SFT                        (0x1 << 3)
#define CCI_AUD_SDM_MUTER_SFT                             2
#define CCI_AUD_SDM_MUTER_MASK                            0x1
#define CCI_AUD_SDM_MUTER_MASK_SFT                        (0x1 << 2)
#define CCI_AUD_SDM_7BIT_SEL_SFT                          1
#define CCI_AUD_SDM_7BIT_SEL_MASK                         0x1
#define CCI_AUD_SDM_7BIT_SEL_MASK_SFT                     (0x1 << 1)
#define CCI_SCRAMBLER_EN_SFT                              0
#define CCI_SCRAMBLER_EN_MASK                             0x1
#define CCI_SCRAMBLER_EN_MASK_SFT                         (0x1 << 0)

/* MT6358_AFUNC_AUD_CON1 */
#define AUD_SDM_TEST_L_SFT                                8
#define AUD_SDM_TEST_L_MASK                               0xff
#define AUD_SDM_TEST_L_MASK_SFT                           (0xff << 8)
#define AUD_SDM_TEST_R_SFT                                0
#define AUD_SDM_TEST_R_MASK                               0xff
#define AUD_SDM_TEST_R_MASK_SFT                           (0xff << 0)

/* MT6358_AFUNC_AUD_CON2 */
#define CCI_AUD_DAC_ANA_MUTE_SFT                          7
#define CCI_AUD_DAC_ANA_MUTE_MASK                         0x1
#define CCI_AUD_DAC_ANA_MUTE_MASK_SFT                     (0x1 << 7)
#define CCI_AUD_DAC_ANA_RSTB_SEL_SFT                      6
#define CCI_AUD_DAC_ANA_RSTB_SEL_MASK                     0x1
#define CCI_AUD_DAC_ANA_RSTB_SEL_MASK_SFT                 (0x1 << 6)
#define CCI_AUDIO_FIFO_CLKIN_INV_SFT                      4
#define CCI_AUDIO_FIFO_CLKIN_INV_MASK                     0x1
#define CCI_AUDIO_FIFO_CLKIN_INV_MASK_SFT                 (0x1 << 4)
#define CCI_AUDIO_FIFO_ENABLE_SFT                         3
#define CCI_AUDIO_FIFO_ENABLE_MASK                        0x1
#define CCI_AUDIO_FIFO_ENABLE_MASK_SFT                    (0x1 << 3)
#define CCI_ACD_MODE_SFT                                  2
#define CCI_ACD_MODE_MASK                                 0x1
#define CCI_ACD_MODE_MASK_SFT                             (0x1 << 2)
#define CCI_AFIFO_CLK_PWDB_SFT                            1
#define CCI_AFIFO_CLK_PWDB_MASK                           0x1
#define CCI_AFIFO_CLK_PWDB_MASK_SFT                       (0x1 << 1)
#define CCI_ACD_FUNC_RSTB_SFT                             0
#define CCI_ACD_FUNC_RSTB_MASK                            0x1
#define CCI_ACD_FUNC_RSTB_MASK_SFT                        (0x1 << 0)

/* MT6358_AFUNC_AUD_CON3 */
#define SDM_ANA13M_TESTCK_SEL_SFT                         15
#define SDM_ANA13M_TESTCK_SEL_MASK                        0x1
#define SDM_ANA13M_TESTCK_SEL_MASK_SFT                    (0x1 << 15)
#define SDM_ANA13M_TESTCK_SRC_SEL_SFT                     12
#define SDM_ANA13M_TESTCK_SRC_SEL_MASK                    0x7
#define SDM_ANA13M_TESTCK_SRC_SEL_MASK_SFT                (0x7 << 12)
#define SDM_TESTCK_SRC_SEL_SFT                            8
#define SDM_TESTCK_SRC_SEL_MASK                           0x7
#define SDM_TESTCK_SRC_SEL_MASK_SFT                       (0x7 << 8)
#define DIGMIC_TESTCK_SRC_SEL_SFT                         4
#define DIGMIC_TESTCK_SRC_SEL_MASK                        0x7
#define DIGMIC_TESTCK_SRC_SEL_MASK_SFT                    (0x7 << 4)
#define DIGMIC_TESTCK_SEL_SFT                             0
#define DIGMIC_TESTCK_SEL_MASK                            0x1
#define DIGMIC_TESTCK_SEL_MASK_SFT                        (0x1 << 0)

/* MT6358_AFUNC_AUD_CON4 */
#define UL_FIFO_WCLK_INV_SFT                              8
#define UL_FIFO_WCLK_INV_MASK                             0x1
#define UL_FIFO_WCLK_INV_MASK_SFT                         (0x1 << 8)
#define UL_FIFO_DIGMIC_WDATA_TESTSRC_SEL_SFT              6
#define UL_FIFO_DIGMIC_WDATA_TESTSRC_SEL_MASK             0x1
#define UL_FIFO_DIGMIC_WDATA_TESTSRC_SEL_MASK_SFT         (0x1 << 6)
#define UL_FIFO_WDATA_TESTEN_SFT                          5
#define UL_FIFO_WDATA_TESTEN_MASK                         0x1
#define UL_FIFO_WDATA_TESTEN_MASK_SFT                     (0x1 << 5)
#define UL_FIFO_WDATA_TESTSRC_SEL_SFT                     4
#define UL_FIFO_WDATA_TESTSRC_SEL_MASK                    0x1
#define UL_FIFO_WDATA_TESTSRC_SEL_MASK_SFT                (0x1 << 4)
#define UL_FIFO_WCLK_6P5M_TESTCK_SEL_SFT                  3
#define UL_FIFO_WCLK_6P5M_TESTCK_SEL_MASK                 0x1
#define UL_FIFO_WCLK_6P5M_TESTCK_SEL_MASK_SFT             (0x1 << 3)
#define UL_FIFO_WCLK_6P5M_TESTCK_SRC_SEL_SFT              0
#define UL_FIFO_WCLK_6P5M_TESTCK_SRC_SEL_MASK             0x7
#define UL_FIFO_WCLK_6P5M_TESTCK_SRC_SEL_MASK_SFT         (0x7 << 0)

/* MT6358_AFUNC_AUD_CON5 */
#define R_AUD_DAC_POS_LARGE_MONO_SFT                      8
#define R_AUD_DAC_POS_LARGE_MONO_MASK                     0xff
#define R_AUD_DAC_POS_LARGE_MONO_MASK_SFT                 (0xff << 8)
#define R_AUD_DAC_NEG_LARGE_MONO_SFT                      0
#define R_AUD_DAC_NEG_LARGE_MONO_MASK                     0xff
#define R_AUD_DAC_NEG_LARGE_MONO_MASK_SFT                 (0xff << 0)

/* MT6358_AFUNC_AUD_CON6 */
#define R_AUD_DAC_POS_SMALL_MONO_SFT                      12
#define R_AUD_DAC_POS_SMALL_MONO_MASK                     0xf
#define R_AUD_DAC_POS_SMALL_MONO_MASK_SFT                 (0xf << 12)
#define R_AUD_DAC_NEG_SMALL_MONO_SFT                      8
#define R_AUD_DAC_NEG_SMALL_MONO_MASK                     0xf
#define R_AUD_DAC_NEG_SMALL_MONO_MASK_SFT                 (0xf << 8)
#define R_AUD_DAC_POS_TINY_MONO_SFT                       6
#define R_AUD_DAC_POS_TINY_MONO_MASK                      0x3
#define R_AUD_DAC_POS_TINY_MONO_MASK_SFT                  (0x3 << 6)
#define R_AUD_DAC_NEG_TINY_MONO_SFT                       4
#define R_AUD_DAC_NEG_TINY_MONO_MASK                      0x3
#define R_AUD_DAC_NEG_TINY_MONO_MASK_SFT                  (0x3 << 4)
#define R_AUD_DAC_MONO_SEL_SFT                            3
#define R_AUD_DAC_MONO_SEL_MASK                           0x1
#define R_AUD_DAC_MONO_SEL_MASK_SFT                       (0x1 << 3)
#define R_AUD_DAC_SW_RSTB_SFT                             0
#define R_AUD_DAC_SW_RSTB_MASK                            0x1
#define R_AUD_DAC_SW_RSTB_MASK_SFT                        (0x1 << 0)

/* MT6358_AFUNC_AUD_MON0 */
#define AUD_SCR_OUT_L_SFT                                 8
#define AUD_SCR_OUT_L_MASK                                0xff
#define AUD_SCR_OUT_L_MASK_SFT                            (0xff << 8)
#define AUD_SCR_OUT_R_SFT                                 0
#define AUD_SCR_OUT_R_MASK                                0xff
#define AUD_SCR_OUT_R_MASK_SFT                            (0xff << 0)

/* MT6358_AUDRC_TUNE_MON0 */
#define ASYNC_TEST_OUT_BCK_SFT                            15
#define ASYNC_TEST_OUT_BCK_MASK                           0x1
#define ASYNC_TEST_OUT_BCK_MASK_SFT                       (0x1 << 15)
#define RGS_AUDRCTUNE1READ_SFT                            8
#define RGS_AUDRCTUNE1READ_MASK                           0x1f
#define RGS_AUDRCTUNE1READ_MASK_SFT                       (0x1f << 8)
#define RGS_AUDRCTUNE0READ_SFT                            0
#define RGS_AUDRCTUNE0READ_MASK                           0x1f
#define RGS_AUDRCTUNE0READ_MASK_SFT                       (0x1f << 0)

/* MT6358_AFE_ADDA_MTKAIF_FIFO_CFG0 */
#define AFE_RESERVED_SFT                                  1
#define AFE_RESERVED_MASK                                 0x7fff
#define AFE_RESERVED_MASK_SFT                             (0x7fff << 1)
#define RG_MTKAIF_RXIF_FIFO_INTEN_SFT                     0
#define RG_MTKAIF_RXIF_FIFO_INTEN_MASK                    0x1
#define RG_MTKAIF_RXIF_FIFO_INTEN_MASK_SFT                (0x1 << 0)

/* MT6358_AFE_ADDA_MTKAIF_FIFO_LOG_MON1 */
#define MTKAIF_RXIF_WR_FULL_STATUS_SFT                    1
#define MTKAIF_RXIF_WR_FULL_STATUS_MASK                   0x1
#define MTKAIF_RXIF_WR_FULL_STATUS_MASK_SFT               (0x1 << 1)
#define MTKAIF_RXIF_RD_EMPTY_STATUS_SFT                   0
#define MTKAIF_RXIF_RD_EMPTY_STATUS_MASK                  0x1
#define MTKAIF_RXIF_RD_EMPTY_STATUS_MASK_SFT              (0x1 << 0)

/* MT6358_AFE_ADDA_MTKAIF_MON0 */
#define MTKAIFTX_V3_SYNC_OUT_SFT                          14
#define MTKAIFTX_V3_SYNC_OUT_MASK                         0x1
#define MTKAIFTX_V3_SYNC_OUT_MASK_SFT                     (0x1 << 14)
#define MTKAIFTX_V3_SDATA_OUT2_SFT                        13
#define MTKAIFTX_V3_SDATA_OUT2_MASK                       0x1
#define MTKAIFTX_V3_SDATA_OUT2_MASK_SFT                   (0x1 << 13)
#define MTKAIFTX_V3_SDATA_OUT1_SFT                        12
#define MTKAIFTX_V3_SDATA_OUT1_MASK                       0x1
#define MTKAIFTX_V3_SDATA_OUT1_MASK_SFT                   (0x1 << 12)
#define MTKAIF_RXIF_FIFO_STATUS_SFT                       0
#define MTKAIF_RXIF_FIFO_STATUS_MASK                      0xfff
#define MTKAIF_RXIF_FIFO_STATUS_MASK_SFT                  (0xfff << 0)

/* MT6358_AFE_ADDA_MTKAIF_MON1 */
#define MTKAIFRX_V3_SYNC_IN_SFT                           14
#define MTKAIFRX_V3_SYNC_IN_MASK                          0x1
#define MTKAIFRX_V3_SYNC_IN_MASK_SFT                      (0x1 << 14)
#define MTKAIFRX_V3_SDATA_IN2_SFT                         13
#define MTKAIFRX_V3_SDATA_IN2_MASK                        0x1
#define MTKAIFRX_V3_SDATA_IN2_MASK_SFT                    (0x1 << 13)
#define MTKAIFRX_V3_SDATA_IN1_SFT                         12
#define MTKAIFRX_V3_SDATA_IN1_MASK                        0x1
#define MTKAIFRX_V3_SDATA_IN1_MASK_SFT                    (0x1 << 12)
#define MTKAIF_RXIF_SEARCH_FAIL_FLAG_SFT                  11
#define MTKAIF_RXIF_SEARCH_FAIL_FLAG_MASK                 0x1
#define MTKAIF_RXIF_SEARCH_FAIL_FLAG_MASK_SFT             (0x1 << 11)
#define MTKAIF_RXIF_INVALID_FLAG_SFT                      8
#define MTKAIF_RXIF_INVALID_FLAG_MASK                     0x1
#define MTKAIF_RXIF_INVALID_FLAG_MASK_SFT                 (0x1 << 8)
#define MTKAIF_RXIF_INVALID_CYCLE_SFT                     0
#define MTKAIF_RXIF_INVALID_CYCLE_MASK                    0xff
#define MTKAIF_RXIF_INVALID_CYCLE_MASK_SFT                (0xff << 0)

/* MT6358_AFE_ADDA_MTKAIF_MON2 */
#define MTKAIF_TXIF_IN_CH2_SFT                            8
#define MTKAIF_TXIF_IN_CH2_MASK                           0xff
#define MTKAIF_TXIF_IN_CH2_MASK_SFT                       (0xff << 8)
#define MTKAIF_TXIF_IN_CH1_SFT                            0
#define MTKAIF_TXIF_IN_CH1_MASK                           0xff
#define MTKAIF_TXIF_IN_CH1_MASK_SFT                       (0xff << 0)

/* MT6358_AFE_ADDA_MTKAIF_MON3 */
#define MTKAIF_RXIF_OUT_CH2_SFT                           8
#define MTKAIF_RXIF_OUT_CH2_MASK                          0xff
#define MTKAIF_RXIF_OUT_CH2_MASK_SFT                      (0xff << 8)
#define MTKAIF_RXIF_OUT_CH1_SFT                           0
#define MTKAIF_RXIF_OUT_CH1_MASK                          0xff
#define MTKAIF_RXIF_OUT_CH1_MASK_SFT                      (0xff << 0)

/* MT6358_AFE_ADDA_MTKAIF_CFG0 */
#define RG_MTKAIF_RXIF_CLKINV_SFT                         15
#define RG_MTKAIF_RXIF_CLKINV_MASK                        0x1
#define RG_MTKAIF_RXIF_CLKINV_MASK_SFT                    (0x1 << 15)
#define RG_MTKAIF_RXIF_PROTOCOL2_SFT                      8
#define RG_MTKAIF_RXIF_PROTOCOL2_MASK                     0x1
#define RG_MTKAIF_RXIF_PROTOCOL2_MASK_SFT                 (0x1 << 8)
#define RG_MTKAIF_BYPASS_SRC_MODE_SFT                     6
#define RG_MTKAIF_BYPASS_SRC_MODE_MASK                    0x3
#define RG_MTKAIF_BYPASS_SRC_MODE_MASK_SFT                (0x3 << 6)
#define RG_MTKAIF_BYPASS_SRC_TEST_SFT                     5
#define RG_MTKAIF_BYPASS_SRC_TEST_MASK                    0x1
#define RG_MTKAIF_BYPASS_SRC_TEST_MASK_SFT                (0x1 << 5)
#define RG_MTKAIF_TXIF_PROTOCOL2_SFT                      4
#define RG_MTKAIF_TXIF_PROTOCOL2_MASK                     0x1
#define RG_MTKAIF_TXIF_PROTOCOL2_MASK_SFT                 (0x1 << 4)
#define RG_MTKAIF_PMIC_TXIF_8TO5_SFT                      2
#define RG_MTKAIF_PMIC_TXIF_8TO5_MASK                     0x1
#define RG_MTKAIF_PMIC_TXIF_8TO5_MASK_SFT                 (0x1 << 2)
#define RG_MTKAIF_LOOPBACK_TEST2_SFT                      1
#define RG_MTKAIF_LOOPBACK_TEST2_MASK                     0x1
#define RG_MTKAIF_LOOPBACK_TEST2_MASK_SFT                 (0x1 << 1)
#define RG_MTKAIF_LOOPBACK_TEST1_SFT                      0
#define RG_MTKAIF_LOOPBACK_TEST1_MASK                     0x1
#define RG_MTKAIF_LOOPBACK_TEST1_MASK_SFT                 (0x1 << 0)

/* MT6358_AFE_ADDA_MTKAIF_RX_CFG0 */
#define RG_MTKAIF_RXIF_VOICE_MODE_SFT                     12
#define RG_MTKAIF_RXIF_VOICE_MODE_MASK                    0xf
#define RG_MTKAIF_RXIF_VOICE_MODE_MASK_SFT                (0xf << 12)
#define RG_MTKAIF_RXIF_DATA_BIT_SFT                       8
#define RG_MTKAIF_RXIF_DATA_BIT_MASK                      0x7
#define RG_MTKAIF_RXIF_DATA_BIT_MASK_SFT                  (0x7 << 8)
#define RG_MTKAIF_RXIF_FIFO_RSP_SFT                       4
#define RG_MTKAIF_RXIF_FIFO_RSP_MASK                      0x7
#define RG_MTKAIF_RXIF_FIFO_RSP_MASK_SFT                  (0x7 << 4)
#define RG_MTKAIF_RXIF_DETECT_ON_SFT                      3
#define RG_MTKAIF_RXIF_DETECT_ON_MASK                     0x1
#define RG_MTKAIF_RXIF_DETECT_ON_MASK_SFT                 (0x1 << 3)
#define RG_MTKAIF_RXIF_DATA_MODE_SFT                      0
#define RG_MTKAIF_RXIF_DATA_MODE_MASK                     0x1
#define RG_MTKAIF_RXIF_DATA_MODE_MASK_SFT                 (0x1 << 0)

/* MT6358_AFE_ADDA_MTKAIF_RX_CFG1 */
#define RG_MTKAIF_RXIF_SYNC_SEARCH_TABLE_SFT              12
#define RG_MTKAIF_RXIF_SYNC_SEARCH_TABLE_MASK             0xf
#define RG_MTKAIF_RXIF_SYNC_SEARCH_TABLE_MASK_SFT         (0xf << 12)
#define RG_MTKAIF_RXIF_INVALID_SYNC_CHECK_ROUND_SFT       8
#define RG_MTKAIF_RXIF_INVALID_SYNC_CHECK_ROUND_MASK      0xf
#define RG_MTKAIF_RXIF_INVALID_SYNC_CHECK_ROUND_MASK_SFT  (0xf << 8)
#define RG_MTKAIF_RXIF_SYNC_CHECK_ROUND_SFT               4
#define RG_MTKAIF_RXIF_SYNC_CHECK_ROUND_MASK              0xf
#define RG_MTKAIF_RXIF_SYNC_CHECK_ROUND_MASK_SFT          (0xf << 4)
#define RG_MTKAIF_RXIF_VOICE_MODE_PROTOCOL2_SFT           0
#define RG_MTKAIF_RXIF_VOICE_MODE_PROTOCOL2_MASK          0xf
#define RG_MTKAIF_RXIF_VOICE_MODE_PROTOCOL2_MASK_SFT      (0xf << 0)

/* MT6358_AFE_ADDA_MTKAIF_RX_CFG2 */
#define RG_MTKAIF_RXIF_CLEAR_SYNC_FAIL_SFT                12
#define RG_MTKAIF_RXIF_CLEAR_SYNC_FAIL_MASK               0x1
#define RG_MTKAIF_RXIF_CLEAR_SYNC_FAIL_MASK_SFT           (0x1 << 12)
#define RG_MTKAIF_RXIF_SYNC_CNT_TABLE_SFT                 0
#define RG_MTKAIF_RXIF_SYNC_CNT_TABLE_MASK                0xfff
#define RG_MTKAIF_RXIF_SYNC_CNT_TABLE_MASK_SFT            (0xfff << 0)

/* MT6358_AFE_ADDA_MTKAIF_RX_CFG3 */
#define RG_MTKAIF_RXIF_LOOPBACK_USE_NLE_SFT               7
#define RG_MTKAIF_RXIF_LOOPBACK_USE_NLE_MASK              0x1
#define RG_MTKAIF_RXIF_LOOPBACK_USE_NLE_MASK_SFT          (0x1 << 7)
#define RG_MTKAIF_RXIF_FIFO_RSP_PROTOCOL2_SFT             4
#define RG_MTKAIF_RXIF_FIFO_RSP_PROTOCOL2_MASK            0x7
#define RG_MTKAIF_RXIF_FIFO_RSP_PROTOCOL2_MASK_SFT        (0x7 << 4)
#define RG_MTKAIF_RXIF_DETECT_ON_PROTOCOL2_SFT            3
#define RG_MTKAIF_RXIF_DETECT_ON_PROTOCOL2_MASK           0x1
#define RG_MTKAIF_RXIF_DETECT_ON_PROTOCOL2_MASK_SFT       (0x1 << 3)

/* MT6358_AFE_ADDA_MTKAIF_TX_CFG1 */
#define RG_MTKAIF_SYNC_WORD2_SFT                          4
#define RG_MTKAIF_SYNC_WORD2_MASK                         0x7
#define RG_MTKAIF_SYNC_WORD2_MASK_SFT                     (0x7 << 4)
#define RG_MTKAIF_SYNC_WORD1_SFT                          0
#define RG_MTKAIF_SYNC_WORD1_MASK                         0x7
#define RG_MTKAIF_SYNC_WORD1_MASK_SFT                     (0x7 << 0)

/* MT6358_AFE_SGEN_CFG0 */
#define SGEN_AMP_DIV_CH1_CTL_SFT                          12
#define SGEN_AMP_DIV_CH1_CTL_MASK                         0xf
#define SGEN_AMP_DIV_CH1_CTL_MASK_SFT                     (0xf << 12)
#define SGEN_DAC_EN_CTL_SFT                               7
#define SGEN_DAC_EN_CTL_MASK                              0x1
#define SGEN_DAC_EN_CTL_MASK_SFT                          (0x1 << 7)
#define SGEN_MUTE_SW_CTL_SFT                              6
#define SGEN_MUTE_SW_CTL_MASK                             0x1
#define SGEN_MUTE_SW_CTL_MASK_SFT                         (0x1 << 6)
#define R_AUD_SDM_MUTE_L_SFT                              5
#define R_AUD_SDM_MUTE_L_MASK                             0x1
#define R_AUD_SDM_MUTE_L_MASK_SFT                         (0x1 << 5)
#define R_AUD_SDM_MUTE_R_SFT                              4
#define R_AUD_SDM_MUTE_R_MASK                             0x1
#define R_AUD_SDM_MUTE_R_MASK_SFT                         (0x1 << 4)

/* MT6358_AFE_SGEN_CFG1 */
#define C_SGEN_RCH_INV_5BIT_SFT                           15
#define C_SGEN_RCH_INV_5BIT_MASK                          0x1
#define C_SGEN_RCH_INV_5BIT_MASK_SFT                      (0x1 << 15)
#define C_SGEN_RCH_INV_8BIT_SFT                           14
#define C_SGEN_RCH_INV_8BIT_MASK                          0x1
#define C_SGEN_RCH_INV_8BIT_MASK_SFT                      (0x1 << 14)
#define SGEN_FREQ_DIV_CH1_CTL_SFT                         0
#define SGEN_FREQ_DIV_CH1_CTL_MASK                        0x1f
#define SGEN_FREQ_DIV_CH1_CTL_MASK_SFT                    (0x1f << 0)

/* MT6358_AFE_ADC_ASYNC_FIFO_CFG */
#define RG_UL_ASYNC_FIFO_SOFT_RST_EN_SFT                  5
#define RG_UL_ASYNC_FIFO_SOFT_RST_EN_MASK                 0x1
#define RG_UL_ASYNC_FIFO_SOFT_RST_EN_MASK_SFT             (0x1 << 5)
#define RG_UL_ASYNC_FIFO_SOFT_RST_SFT                     4
#define RG_UL_ASYNC_FIFO_SOFT_RST_MASK                    0x1
#define RG_UL_ASYNC_FIFO_SOFT_RST_MASK_SFT                (0x1 << 4)
#define RG_AMIC_UL_ADC_CLK_SEL_SFT                        1
#define RG_AMIC_UL_ADC_CLK_SEL_MASK                       0x1
#define RG_AMIC_UL_ADC_CLK_SEL_MASK_SFT                   (0x1 << 1)

/* MT6358_AFE_DCCLK_CFG0 */
#define DCCLK_DIV_SFT                                     5
#define DCCLK_DIV_MASK                                    0x7ff
#define DCCLK_DIV_MASK_SFT                                (0x7ff << 5)
#define DCCLK_INV_SFT                                     4
#define DCCLK_INV_MASK                                    0x1
#define DCCLK_INV_MASK_SFT                                (0x1 << 4)
#define DCCLK_PDN_SFT                                     1
#define DCCLK_PDN_MASK                                    0x1
#define DCCLK_PDN_MASK_SFT                                (0x1 << 1)
#define DCCLK_GEN_ON_SFT                                  0
#define DCCLK_GEN_ON_MASK                                 0x1
#define DCCLK_GEN_ON_MASK_SFT                             (0x1 << 0)

/* MT6358_AFE_DCCLK_CFG1 */
#define RESYNC_SRC_SEL_SFT                                10
#define RESYNC_SRC_SEL_MASK                               0x3
#define RESYNC_SRC_SEL_MASK_SFT                           (0x3 << 10)
#define RESYNC_SRC_CK_INV_SFT                             9
#define RESYNC_SRC_CK_INV_MASK                            0x1
#define RESYNC_SRC_CK_INV_MASK_SFT                        (0x1 << 9)
#define DCCLK_RESYNC_BYPASS_SFT                           8
#define DCCLK_RESYNC_BYPASS_MASK                          0x1
#define DCCLK_RESYNC_BYPASS_MASK_SFT                      (0x1 << 8)
#define DCCLK_PHASE_SEL_SFT                               4
#define DCCLK_PHASE_SEL_MASK                              0xf
#define DCCLK_PHASE_SEL_MASK_SFT                          (0xf << 4)

/* MT6358_AUDIO_DIG_CFG */
#define RG_AUD_PAD_TOP_DAT_MISO2_LOOPBACK_SFT             15
#define RG_AUD_PAD_TOP_DAT_MISO2_LOOPBACK_MASK            0x1
#define RG_AUD_PAD_TOP_DAT_MISO2_LOOPBACK_MASK_SFT        (0x1 << 15)
#define RG_AUD_PAD_TOP_PHASE_MODE2_SFT                    8
#define RG_AUD_PAD_TOP_PHASE_MODE2_MASK                   0x7f
#define RG_AUD_PAD_TOP_PHASE_MODE2_MASK_SFT               (0x7f << 8)
#define RG_AUD_PAD_TOP_DAT_MISO_LOOPBACK_SFT              7
#define RG_AUD_PAD_TOP_DAT_MISO_LOOPBACK_MASK             0x1
#define RG_AUD_PAD_TOP_DAT_MISO_LOOPBACK_MASK_SFT         (0x1 << 7)
#define RG_AUD_PAD_TOP_PHASE_MODE_SFT                     0
#define RG_AUD_PAD_TOP_PHASE_MODE_MASK                    0x7f
#define RG_AUD_PAD_TOP_PHASE_MODE_MASK_SFT                (0x7f << 0)

/* MT6358_AFE_AUD_PAD_TOP */
#define RG_AUD_PAD_TOP_TX_FIFO_RSP_SFT                    12
#define RG_AUD_PAD_TOP_TX_FIFO_RSP_MASK                   0x7
#define RG_AUD_PAD_TOP_TX_FIFO_RSP_MASK_SFT               (0x7 << 12)
#define RG_AUD_PAD_TOP_MTKAIF_CLK_PROTOCOL2_SFT           11
#define RG_AUD_PAD_TOP_MTKAIF_CLK_PROTOCOL2_MASK          0x1
#define RG_AUD_PAD_TOP_MTKAIF_CLK_PROTOCOL2_MASK_SFT      (0x1 << 11)
#define RG_AUD_PAD_TOP_TX_FIFO_ON_SFT                     8
#define RG_AUD_PAD_TOP_TX_FIFO_ON_MASK                    0x1
#define RG_AUD_PAD_TOP_TX_FIFO_ON_MASK_SFT                (0x1 << 8)

/* MT6358_AFE_AUD_PAD_TOP_MON */
#define ADDA_AUD_PAD_TOP_MON_SFT                          0
#define ADDA_AUD_PAD_TOP_MON_MASK                         0xffff
#define ADDA_AUD_PAD_TOP_MON_MASK_SFT                     (0xffff << 0)

/* MT6358_AFE_AUD_PAD_TOP_MON1 */
#define ADDA_AUD_PAD_TOP_MON1_SFT                         0
#define ADDA_AUD_PAD_TOP_MON1_MASK                        0xffff
#define ADDA_AUD_PAD_TOP_MON1_MASK_SFT                    (0xffff << 0)

/* MT6358_AFE_DL_NLE_CFG */
#define NLE_RCH_HPGAIN_SEL_SFT                            10
#define NLE_RCH_HPGAIN_SEL_MASK                           0x1
#define NLE_RCH_HPGAIN_SEL_MASK_SFT                       (0x1 << 10)
#define NLE_RCH_CH_SEL_SFT                                9
#define NLE_RCH_CH_SEL_MASK                               0x1
#define NLE_RCH_CH_SEL_MASK_SFT                           (0x1 << 9)
#define NLE_RCH_ON_SFT                                    8
#define NLE_RCH_ON_MASK                                   0x1
#define NLE_RCH_ON_MASK_SFT                               (0x1 << 8)
#define NLE_LCH_HPGAIN_SEL_SFT                            2
#define NLE_LCH_HPGAIN_SEL_MASK                           0x1
#define NLE_LCH_HPGAIN_SEL_MASK_SFT                       (0x1 << 2)
#define NLE_LCH_CH_SEL_SFT                                1
#define NLE_LCH_CH_SEL_MASK                               0x1
#define NLE_LCH_CH_SEL_MASK_SFT                           (0x1 << 1)
#define NLE_LCH_ON_SFT                                    0
#define NLE_LCH_ON_MASK                                   0x1
#define NLE_LCH_ON_MASK_SFT                               (0x1 << 0)

/* MT6358_AFE_DL_NLE_MON */
#define NLE_MONITOR_SFT                                   0
#define NLE_MONITOR_MASK                                  0x3fff
#define NLE_MONITOR_MASK_SFT                              (0x3fff << 0)

/* MT6358_AFE_CG_EN_MON */
#define CK_CG_EN_MON_SFT                                  0
#define CK_CG_EN_MON_MASK                                 0x3f
#define CK_CG_EN_MON_MASK_SFT                             (0x3f << 0)

/* MT6358_AFE_VOW_TOP */
#define PDN_VOW_SFT                                       15
#define PDN_VOW_MASK                                      0x1
#define PDN_VOW_MASK_SFT                                  (0x1 << 15)
#define VOW_1P6M_800K_SEL_SFT                             14
#define VOW_1P6M_800K_SEL_MASK                            0x1
#define VOW_1P6M_800K_SEL_MASK_SFT                        (0x1 << 14)
#define VOW_DIGMIC_ON_SFT                                 13
#define VOW_DIGMIC_ON_MASK                                0x1
#define VOW_DIGMIC_ON_MASK_SFT                            (0x1 << 13)
#define VOW_CK_DIV_RST_SFT                                12
#define VOW_CK_DIV_RST_MASK                               0x1
#define VOW_CK_DIV_RST_MASK_SFT                           (0x1 << 12)
#define VOW_ON_SFT                                        11
#define VOW_ON_MASK                                       0x1
#define VOW_ON_MASK_SFT                                   (0x1 << 11)
#define VOW_DIGMIC_CK_PHASE_SEL_SFT                       8
#define VOW_DIGMIC_CK_PHASE_SEL_MASK                      0x7
#define VOW_DIGMIC_CK_PHASE_SEL_MASK_SFT                  (0x7 << 8)
#define MAIN_DMIC_CK_VOW_SEL_SFT                          7
#define MAIN_DMIC_CK_VOW_SEL_MASK                         0x1
#define MAIN_DMIC_CK_VOW_SEL_MASK_SFT                     (0x1 << 7)
#define VOW_SDM_3_LEVEL_SFT                               6
#define VOW_SDM_3_LEVEL_MASK                              0x1
#define VOW_SDM_3_LEVEL_MASK_SFT                          (0x1 << 6)
#define VOW_LOOP_BACK_MODE_SFT                            5
#define VOW_LOOP_BACK_MODE_MASK                           0x1
#define VOW_LOOP_BACK_MODE_MASK_SFT                       (0x1 << 5)
#define VOW_INTR_SOURCE_SEL_SFT                           4
#define VOW_INTR_SOURCE_SEL_MASK                          0x1
#define VOW_INTR_SOURCE_SEL_MASK_SFT                      (0x1 << 4)
#define VOW_INTR_CLR_SFT                                  3
#define VOW_INTR_CLR_MASK                                 0x1
#define VOW_INTR_CLR_MASK_SFT                             (0x1 << 3)
#define S_N_VALUE_RST_SFT                                 2
#define S_N_VALUE_RST_MASK                                0x1
#define S_N_VALUE_RST_MASK_SFT                            (0x1 << 2)
#define SAMPLE_BASE_MODE_SFT                              1
#define SAMPLE_BASE_MODE_MASK                             0x1
#define SAMPLE_BASE_MODE_MASK_SFT                         (0x1 << 1)
#define VOW_INTR_FLAG_SFT                                 0
#define VOW_INTR_FLAG_MASK                                0x1
#define VOW_INTR_FLAG_MASK_SFT                            (0x1 << 0)

/* MT6358_AFE_VOW_CFG0 */
#define AMPREF_SFT                                        0
#define AMPREF_MASK                                       0xffff
#define AMPREF_MASK_SFT                                   (0xffff << 0)

/* MT6358_AFE_VOW_CFG1 */
#define TIMERINI_SFT                                      0
#define TIMERINI_MASK                                     0xffff
#define TIMERINI_MASK_SFT                                 (0xffff << 0)

/* MT6358_AFE_VOW_CFG2 */
#define B_DEFAULT_SFT                                     12
#define B_DEFAULT_MASK                                    0x7
#define B_DEFAULT_MASK_SFT                                (0x7 << 12)
#define A_DEFAULT_SFT                                     8
#define A_DEFAULT_MASK                                    0x7
#define A_DEFAULT_MASK_SFT                                (0x7 << 8)
#define B_INI_SFT                                         4
#define B_INI_MASK                                        0x7
#define B_INI_MASK_SFT                                    (0x7 << 4)
#define A_INI_SFT                                         0
#define A_INI_MASK                                        0x7
#define A_INI_MASK_SFT                                    (0x7 << 0)

/* MT6358_AFE_VOW_CFG3 */
#define K_BETA_RISE_SFT                                   12
#define K_BETA_RISE_MASK                                  0xf
#define K_BETA_RISE_MASK_SFT                              (0xf << 12)
#define K_BETA_FALL_SFT                                   8
#define K_BETA_FALL_MASK                                  0xf
#define K_BETA_FALL_MASK_SFT                              (0xf << 8)
#define K_ALPHA_RISE_SFT                                  4
#define K_ALPHA_RISE_MASK                                 0xf
#define K_ALPHA_RISE_MASK_SFT                             (0xf << 4)
#define K_ALPHA_FALL_SFT                                  0
#define K_ALPHA_FALL_MASK                                 0xf
#define K_ALPHA_FALL_MASK_SFT                             (0xf << 0)

/* MT6358_AFE_VOW_CFG4 */
#define VOW_TXIF_SCK_INV_SFT                              15
#define VOW_TXIF_SCK_INV_MASK                             0x1
#define VOW_TXIF_SCK_INV_MASK_SFT                         (0x1 << 15)
#define VOW_ADC_TESTCK_SRC_SEL_SFT                        12
#define VOW_ADC_TESTCK_SRC_SEL_MASK                       0x7
#define VOW_ADC_TESTCK_SRC_SEL_MASK_SFT                   (0x7 << 12)
#define VOW_ADC_TESTCK_SEL_SFT                            11
#define VOW_ADC_TESTCK_SEL_MASK                           0x1
#define VOW_ADC_TESTCK_SEL_MASK_SFT                       (0x1 << 11)
#define VOW_ADC_CLK_INV_SFT                               10
#define VOW_ADC_CLK_INV_MASK                              0x1
#define VOW_ADC_CLK_INV_MASK_SFT                          (0x1 << 10)
#define VOW_TXIF_MONO_SFT                                 9
#define VOW_TXIF_MONO_MASK                                0x1
#define VOW_TXIF_MONO_MASK_SFT                            (0x1 << 9)
#define VOW_TXIF_SCK_DIV_SFT                              4
#define VOW_TXIF_SCK_DIV_MASK                             0x1f
#define VOW_TXIF_SCK_DIV_MASK_SFT                         (0x1f << 4)
#define K_GAMMA_SFT                                       0
#define K_GAMMA_MASK                                      0xf
#define K_GAMMA_MASK_SFT                                  (0xf << 0)

/* MT6358_AFE_VOW_CFG5 */
#define N_MIN_SFT                                         0
#define N_MIN_MASK                                        0xffff
#define N_MIN_MASK_SFT                                    (0xffff << 0)

/* MT6358_AFE_VOW_CFG6 */
#define RG_WINDOW_SIZE_SEL_SFT                            12
#define RG_WINDOW_SIZE_SEL_MASK                           0x1
#define RG_WINDOW_SIZE_SEL_MASK_SFT                       (0x1 << 12)
#define RG_FLR_BYPASS_SFT                                 11
#define RG_FLR_BYPASS_MASK                                0x1
#define RG_FLR_BYPASS_MASK_SFT                            (0x1 << 11)
#define RG_FLR_RATIO_SFT                                  8
#define RG_FLR_RATIO_MASK                                 0x7
#define RG_FLR_RATIO_MASK_SFT                             (0x7 << 8)
#define RG_BUCK_DVFS_DONE_SW_CTL_SFT                      7
#define RG_BUCK_DVFS_DONE_SW_CTL_MASK                     0x1
#define RG_BUCK_DVFS_DONE_SW_CTL_MASK_SFT                 (0x1 << 7)
#define RG_BUCK_DVFS_DONE_HW_MODE_SFT                     6
#define RG_BUCK_DVFS_DONE_HW_MODE_MASK                    0x1
#define RG_BUCK_DVFS_DONE_HW_MODE_MASK_SFT                (0x1 << 6)
#define RG_BUCK_DVFS_HW_CNT_THR_SFT                       0
#define RG_BUCK_DVFS_HW_CNT_THR_MASK                      0x3f
#define RG_BUCK_DVFS_HW_CNT_THR_MASK_SFT                  (0x3f << 0)

/* MT6358_AFE_VOW_MON0 */
#define VOW_DOWNCNT_SFT                                   0
#define VOW_DOWNCNT_MASK                                  0xffff
#define VOW_DOWNCNT_MASK_SFT                              (0xffff << 0)

/* MT6358_AFE_VOW_MON1 */
#define K_TMP_MON_SFT                                     10
#define K_TMP_MON_MASK                                    0xf
#define K_TMP_MON_MASK_SFT                                (0xf << 10)
#define SLT_COUNTER_MON_SFT                               7
#define SLT_COUNTER_MON_MASK                              0x7
#define SLT_COUNTER_MON_MASK_SFT                          (0x7 << 7)
#define VOW_B_SFT                                         4
#define VOW_B_MASK                                        0x7
#define VOW_B_MASK_SFT                                    (0x7 << 4)
#define VOW_A_SFT                                         1
#define VOW_A_MASK                                        0x7
#define VOW_A_MASK_SFT                                    (0x7 << 1)
#define SECOND_CNT_START_SFT                              0
#define SECOND_CNT_START_MASK                             0x1
#define SECOND_CNT_START_MASK_SFT                         (0x1 << 0)

/* MT6358_AFE_VOW_MON2 */
#define VOW_S_L_SFT                                       0
#define VOW_S_L_MASK                                      0xffff
#define VOW_S_L_MASK_SFT                                  (0xffff << 0)

/* MT6358_AFE_VOW_MON3 */
#define VOW_S_H_SFT                                       0
#define VOW_S_H_MASK                                      0xffff
#define VOW_S_H_MASK_SFT                                  (0xffff << 0)

/* MT6358_AFE_VOW_MON4 */
#define VOW_N_L_SFT                                       0
#define VOW_N_L_MASK                                      0xffff
#define VOW_N_L_MASK_SFT                                  (0xffff << 0)

/* MT6358_AFE_VOW_MON5 */
#define VOW_N_H_SFT                                       0
#define VOW_N_H_MASK                                      0xffff
#define VOW_N_H_MASK_SFT                                  (0xffff << 0)

/* MT6358_AFE_VOW_SN_INI_CFG */
#define VOW_SN_INI_CFG_EN_SFT                             15
#define VOW_SN_INI_CFG_EN_MASK                            0x1
#define VOW_SN_INI_CFG_EN_MASK_SFT                        (0x1 << 15)
#define VOW_SN_INI_CFG_VAL_SFT                            0
#define VOW_SN_INI_CFG_VAL_MASK                           0x7fff
#define VOW_SN_INI_CFG_VAL_MASK_SFT                       (0x7fff << 0)

/* MT6358_AFE_VOW_TGEN_CFG0 */
#define VOW_TGEN_EN_SFT                                   15
#define VOW_TGEN_EN_MASK                                  0x1
#define VOW_TGEN_EN_MASK_SFT                              (0x1 << 15)
#define VOW_TGEN_MUTE_SW_SFT                              14
#define VOW_TGEN_MUTE_SW_MASK                             0x1
#define VOW_TGEN_MUTE_SW_MASK_SFT                         (0x1 << 14)
#define VOW_TGEN_FREQ_DIV_SFT                             0
#define VOW_TGEN_FREQ_DIV_MASK                            0x3fff
#define VOW_TGEN_FREQ_DIV_MASK_SFT                        (0x3fff << 0)

/* MT6358_AFE_VOW_POSDIV_CFG0 */
#define BUCK_DVFS_DONE_SFT                                15
#define BUCK_DVFS_DONE_MASK                               0x1
#define BUCK_DVFS_DONE_MASK_SFT                           (0x1 << 15)
#define VOW_32K_MODE_SFT                                  13
#define VOW_32K_MODE_MASK                                 0x1
#define VOW_32K_MODE_MASK_SFT                             (0x1 << 13)
#define RG_BUCK_CLK_DIV_SFT                               8
#define RG_BUCK_CLK_DIV_MASK                              0x1f
#define RG_BUCK_CLK_DIV_MASK_SFT                          (0x1f << 8)
#define RG_A1P6M_EN_SEL_SFT                               7
#define RG_A1P6M_EN_SEL_MASK                              0x1
#define RG_A1P6M_EN_SEL_MASK_SFT                          (0x1 << 7)
#define VOW_CLK_SEL_SFT                                   6
#define VOW_CLK_SEL_MASK                                  0x1
#define VOW_CLK_SEL_MASK_SFT                              (0x1 << 6)
#define VOW_INTR_SW_MODE_SFT                              5
#define VOW_INTR_SW_MODE_MASK                             0x1
#define VOW_INTR_SW_MODE_MASK_SFT                         (0x1 << 5)
#define VOW_INTR_SW_VAL_SFT                               4
#define VOW_INTR_SW_VAL_MASK                              0x1
#define VOW_INTR_SW_VAL_MASK_SFT                          (0x1 << 4)
#define VOW_CIC_MODE_SEL_SFT                              2
#define VOW_CIC_MODE_SEL_MASK                             0x3
#define VOW_CIC_MODE_SEL_MASK_SFT                         (0x3 << 2)
#define RG_VOW_POSDIV_SFT                                 0
#define RG_VOW_POSDIV_MASK                                0x3
#define RG_VOW_POSDIV_MASK_SFT                            (0x3 << 0)

/* MT6358_AFE_VOW_HPF_CFG0 */
#define VOW_HPF_DC_TEST_SFT                               12
#define VOW_HPF_DC_TEST_MASK                              0xf
#define VOW_HPF_DC_TEST_MASK_SFT                          (0xf << 12)
#define VOW_IRQ_LATCH_SNR_EN_SFT                          10
#define VOW_IRQ_LATCH_SNR_EN_MASK                         0x1
#define VOW_IRQ_LATCH_SNR_EN_MASK_SFT                     (0x1 << 10)
#define VOW_DMICCLK_PDN_SFT                               9
#define VOW_DMICCLK_PDN_MASK                              0x1
#define VOW_DMICCLK_PDN_MASK_SFT                          (0x1 << 9)
#define VOW_POSDIVCLK_PDN_SFT                             8
#define VOW_POSDIVCLK_PDN_MASK                            0x1
#define VOW_POSDIVCLK_PDN_MASK_SFT                        (0x1 << 8)
#define RG_BASELINE_ALPHA_ORDER_SFT                       4
#define RG_BASELINE_ALPHA_ORDER_MASK                      0xf
#define RG_BASELINE_ALPHA_ORDER_MASK_SFT                  (0xf << 4)
#define RG_MTKAIF_HPF_BYPASS_SFT                          2
#define RG_MTKAIF_HPF_BYPASS_MASK                         0x1
#define RG_MTKAIF_HPF_BYPASS_MASK_SFT                     (0x1 << 2)
#define RG_SNRDET_HPF_BYPASS_SFT                          1
#define RG_SNRDET_HPF_BYPASS_MASK                         0x1
#define RG_SNRDET_HPF_BYPASS_MASK_SFT                     (0x1 << 1)
#define RG_HPF_ON_SFT                                     0
#define RG_HPF_ON_MASK                                    0x1
#define RG_HPF_ON_MASK_SFT                                (0x1 << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG0 */
#define RG_PERIODIC_EN_SFT                                15
#define RG_PERIODIC_EN_MASK                               0x1
#define RG_PERIODIC_EN_MASK_SFT                           (0x1 << 15)
#define RG_PERIODIC_CNT_CLR_SFT                           14
#define RG_PERIODIC_CNT_CLR_MASK                          0x1
#define RG_PERIODIC_CNT_CLR_MASK_SFT                      (0x1 << 14)
#define RG_PERIODIC_CNT_PERIOD_SFT                        0
#define RG_PERIODIC_CNT_PERIOD_MASK                       0x3fff
#define RG_PERIODIC_CNT_PERIOD_MASK_SFT                   (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG1 */
#define RG_PERIODIC_CNT_SET_SFT                           15
#define RG_PERIODIC_CNT_SET_MASK                          0x1
#define RG_PERIODIC_CNT_SET_MASK_SFT                      (0x1 << 15)
#define RG_PERIODIC_CNT_PAUSE_SFT                         14
#define RG_PERIODIC_CNT_PAUSE_MASK                        0x1
#define RG_PERIODIC_CNT_PAUSE_MASK_SFT                    (0x1 << 14)
#define RG_PERIODIC_CNT_SET_VALUE_SFT                     0
#define RG_PERIODIC_CNT_SET_VALUE_MASK                    0x3fff
#define RG_PERIODIC_CNT_SET_VALUE_MASK_SFT                (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG2 */
#define AUDPREAMPLON_PERIODIC_MODE_SFT                    15
#define AUDPREAMPLON_PERIODIC_MODE_MASK                   0x1
#define AUDPREAMPLON_PERIODIC_MODE_MASK_SFT               (0x1 << 15)
#define AUDPREAMPLON_PERIODIC_INVERSE_SFT                 14
#define AUDPREAMPLON_PERIODIC_INVERSE_MASK                0x1
#define AUDPREAMPLON_PERIODIC_INVERSE_MASK_SFT            (0x1 << 14)
#define AUDPREAMPLON_PERIODIC_ON_CYCLE_SFT                0
#define AUDPREAMPLON_PERIODIC_ON_CYCLE_MASK               0x3fff
#define AUDPREAMPLON_PERIODIC_ON_CYCLE_MASK_SFT           (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG3 */
#define AUDPREAMPLDCPRECHARGE_PERIODIC_MODE_SFT           15
#define AUDPREAMPLDCPRECHARGE_PERIODIC_MODE_MASK          0x1
#define AUDPREAMPLDCPRECHARGE_PERIODIC_MODE_MASK_SFT      (0x1 << 15)
#define AUDPREAMPLDCPRECHARGE_PERIODIC_INVERSE_SFT        14
#define AUDPREAMPLDCPRECHARGE_PERIODIC_INVERSE_MASK       0x1
#define AUDPREAMPLDCPRECHARGE_PERIODIC_INVERSE_MASK_SFT   (0x1 << 14)
#define AUDPREAMPLDCPRECHARGE_PERIODIC_ON_CYCLE_SFT       0
#define AUDPREAMPLDCPRECHARGE_PERIODIC_ON_CYCLE_MASK      0x3fff
#define AUDPREAMPLDCPRECHARGE_PERIODIC_ON_CYCLE_MASK_SFT  (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG4 */
#define AUDADCLPWRUP_PERIODIC_MODE_SFT                    15
#define AUDADCLPWRUP_PERIODIC_MODE_MASK                   0x1
#define AUDADCLPWRUP_PERIODIC_MODE_MASK_SFT               (0x1 << 15)
#define AUDADCLPWRUP_PERIODIC_INVERSE_SFT                 14
#define AUDADCLPWRUP_PERIODIC_INVERSE_MASK                0x1
#define AUDADCLPWRUP_PERIODIC_INVERSE_MASK_SFT            (0x1 << 14)
#define AUDADCLPWRUP_PERIODIC_ON_CYCLE_SFT                0
#define AUDADCLPWRUP_PERIODIC_ON_CYCLE_MASK               0x3fff
#define AUDADCLPWRUP_PERIODIC_ON_CYCLE_MASK_SFT           (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG5 */
#define AUDGLBVOWLPWEN_PERIODIC_MODE_SFT                  15
#define AUDGLBVOWLPWEN_PERIODIC_MODE_MASK                 0x1
#define AUDGLBVOWLPWEN_PERIODIC_MODE_MASK_SFT             (0x1 << 15)
#define AUDGLBVOWLPWEN_PERIODIC_INVERSE_SFT               14
#define AUDGLBVOWLPWEN_PERIODIC_INVERSE_MASK              0x1
#define AUDGLBVOWLPWEN_PERIODIC_INVERSE_MASK_SFT          (0x1 << 14)
#define AUDGLBVOWLPWEN_PERIODIC_ON_CYCLE_SFT              0
#define AUDGLBVOWLPWEN_PERIODIC_ON_CYCLE_MASK             0x3fff
#define AUDGLBVOWLPWEN_PERIODIC_ON_CYCLE_MASK_SFT         (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG6 */
#define AUDDIGMICEN_PERIODIC_MODE_SFT                     15
#define AUDDIGMICEN_PERIODIC_MODE_MASK                    0x1
#define AUDDIGMICEN_PERIODIC_MODE_MASK_SFT                (0x1 << 15)
#define AUDDIGMICEN_PERIODIC_INVERSE_SFT                  14
#define AUDDIGMICEN_PERIODIC_INVERSE_MASK                 0x1
#define AUDDIGMICEN_PERIODIC_INVERSE_MASK_SFT             (0x1 << 14)
#define AUDDIGMICEN_PERIODIC_ON_CYCLE_SFT                 0
#define AUDDIGMICEN_PERIODIC_ON_CYCLE_MASK                0x3fff
#define AUDDIGMICEN_PERIODIC_ON_CYCLE_MASK_SFT            (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG7 */
#define AUDPWDBMICBIAS0_PERIODIC_MODE_SFT                 15
#define AUDPWDBMICBIAS0_PERIODIC_MODE_MASK                0x1
#define AUDPWDBMICBIAS0_PERIODIC_MODE_MASK_SFT            (0x1 << 15)
#define AUDPWDBMICBIAS0_PERIODIC_INVERSE_SFT              14
#define AUDPWDBMICBIAS0_PERIODIC_INVERSE_MASK             0x1
#define AUDPWDBMICBIAS0_PERIODIC_INVERSE_MASK_SFT         (0x1 << 14)
#define AUDPWDBMICBIAS0_PERIODIC_ON_CYCLE_SFT             0
#define AUDPWDBMICBIAS0_PERIODIC_ON_CYCLE_MASK            0x3fff
#define AUDPWDBMICBIAS0_PERIODIC_ON_CYCLE_MASK_SFT        (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG8 */
#define AUDPWDBMICBIAS1_PERIODIC_MODE_SFT                 15
#define AUDPWDBMICBIAS1_PERIODIC_MODE_MASK                0x1
#define AUDPWDBMICBIAS1_PERIODIC_MODE_MASK_SFT            (0x1 << 15)
#define AUDPWDBMICBIAS1_PERIODIC_INVERSE_SFT              14
#define AUDPWDBMICBIAS1_PERIODIC_INVERSE_MASK             0x1
#define AUDPWDBMICBIAS1_PERIODIC_INVERSE_MASK_SFT         (0x1 << 14)
#define AUDPWDBMICBIAS1_PERIODIC_ON_CYCLE_SFT             0
#define AUDPWDBMICBIAS1_PERIODIC_ON_CYCLE_MASK            0x3fff
#define AUDPWDBMICBIAS1_PERIODIC_ON_CYCLE_MASK_SFT        (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG9 */
#define XO_VOW_CK_EN_PERIODIC_MODE_SFT                    15
#define XO_VOW_CK_EN_PERIODIC_MODE_MASK                   0x1
#define XO_VOW_CK_EN_PERIODIC_MODE_MASK_SFT               (0x1 << 15)
#define XO_VOW_CK_EN_PERIODIC_INVERSE_SFT                 14
#define XO_VOW_CK_EN_PERIODIC_INVERSE_MASK                0x1
#define XO_VOW_CK_EN_PERIODIC_INVERSE_MASK_SFT            (0x1 << 14)
#define XO_VOW_CK_EN_PERIODIC_ON_CYCLE_SFT                0
#define XO_VOW_CK_EN_PERIODIC_ON_CYCLE_MASK               0x3fff
#define XO_VOW_CK_EN_PERIODIC_ON_CYCLE_MASK_SFT           (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG10 */
#define AUDGLB_PWRDN_PERIODIC_MODE_SFT                    15
#define AUDGLB_PWRDN_PERIODIC_MODE_MASK                   0x1
#define AUDGLB_PWRDN_PERIODIC_MODE_MASK_SFT               (0x1 << 15)
#define AUDGLB_PWRDN_PERIODIC_INVERSE_SFT                 14
#define AUDGLB_PWRDN_PERIODIC_INVERSE_MASK                0x1
#define AUDGLB_PWRDN_PERIODIC_INVERSE_MASK_SFT            (0x1 << 14)
#define AUDGLB_PWRDN_PERIODIC_ON_CYCLE_SFT                0
#define AUDGLB_PWRDN_PERIODIC_ON_CYCLE_MASK               0x3fff
#define AUDGLB_PWRDN_PERIODIC_ON_CYCLE_MASK_SFT           (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG11 */
#define VOW_ON_PERIODIC_MODE_SFT                          15
#define VOW_ON_PERIODIC_MODE_MASK                         0x1
#define VOW_ON_PERIODIC_MODE_MASK_SFT                     (0x1 << 15)
#define VOW_ON_PERIODIC_INVERSE_SFT                       14
#define VOW_ON_PERIODIC_INVERSE_MASK                      0x1
#define VOW_ON_PERIODIC_INVERSE_MASK_SFT                  (0x1 << 14)
#define VOW_ON_PERIODIC_ON_CYCLE_SFT                      0
#define VOW_ON_PERIODIC_ON_CYCLE_MASK                     0x3fff
#define VOW_ON_PERIODIC_ON_CYCLE_MASK_SFT                 (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG12 */
#define DMIC_ON_PERIODIC_MODE_SFT                         15
#define DMIC_ON_PERIODIC_MODE_MASK                        0x1
#define DMIC_ON_PERIODIC_MODE_MASK_SFT                    (0x1 << 15)
#define DMIC_ON_PERIODIC_INVERSE_SFT                      14
#define DMIC_ON_PERIODIC_INVERSE_MASK                     0x1
#define DMIC_ON_PERIODIC_INVERSE_MASK_SFT                 (0x1 << 14)
#define DMIC_ON_PERIODIC_ON_CYCLE_SFT                     0
#define DMIC_ON_PERIODIC_ON_CYCLE_MASK                    0x3fff
#define DMIC_ON_PERIODIC_ON_CYCLE_MASK_SFT                (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG13 */
#define PDN_VOW_F32K_CK_SFT                               15
#define PDN_VOW_F32K_CK_MASK                              0x1
#define PDN_VOW_F32K_CK_MASK_SFT                          (0x1 << 15)
#define AUDPREAMPLON_PERIODIC_OFF_CYCLE_SFT               0
#define AUDPREAMPLON_PERIODIC_OFF_CYCLE_MASK              0x3fff
#define AUDPREAMPLON_PERIODIC_OFF_CYCLE_MASK_SFT          (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG14 */
#define VOW_SNRDET_PERIODIC_CFG_SFT                       15
#define VOW_SNRDET_PERIODIC_CFG_MASK                      0x1
#define VOW_SNRDET_PERIODIC_CFG_MASK_SFT                  (0x1 << 15)
#define AUDPREAMPLDCPRECHARGE_PERIODIC_OFF_CYCLE_SFT      0
#define AUDPREAMPLDCPRECHARGE_PERIODIC_OFF_CYCLE_MASK     0x3fff
#define AUDPREAMPLDCPRECHARGE_PERIODIC_OFF_CYCLE_MASK_SFT (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG15 */
#define AUDADCLPWRUP_PERIODIC_OFF_CYCLE_SFT               0
#define AUDADCLPWRUP_PERIODIC_OFF_CYCLE_MASK              0x3fff
#define AUDADCLPWRUP_PERIODIC_OFF_CYCLE_MASK_SFT          (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG16 */
#define AUDGLBVOWLPWEN_PERIODIC_OFF_CYCLE_SFT             0
#define AUDGLBVOWLPWEN_PERIODIC_OFF_CYCLE_MASK            0x3fff
#define AUDGLBVOWLPWEN_PERIODIC_OFF_CYCLE_MASK_SFT        (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG17 */
#define AUDDIGMICEN_PERIODIC_OFF_CYCLE_SFT                0
#define AUDDIGMICEN_PERIODIC_OFF_CYCLE_MASK               0x3fff
#define AUDDIGMICEN_PERIODIC_OFF_CYCLE_MASK_SFT           (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG18 */
#define AUDPWDBMICBIAS0_PERIODIC_OFF_CYCLE_SFT            0
#define AUDPWDBMICBIAS0_PERIODIC_OFF_CYCLE_MASK           0x3fff
#define AUDPWDBMICBIAS0_PERIODIC_OFF_CYCLE_MASK_SFT       (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG19 */
#define AUDPWDBMICBIAS1_PERIODIC_OFF_CYCLE_SFT            0
#define AUDPWDBMICBIAS1_PERIODIC_OFF_CYCLE_MASK           0x3fff
#define AUDPWDBMICBIAS1_PERIODIC_OFF_CYCLE_MASK_SFT       (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG20 */
#define CLKSQ_EN_VOW_PERIODIC_MODE_SFT                    15
#define CLKSQ_EN_VOW_PERIODIC_MODE_MASK                   0x1
#define CLKSQ_EN_VOW_PERIODIC_MODE_MASK_SFT               (0x1 << 15)
#define XO_VOW_CK_EN_PERIODIC_OFF_CYCLE_SFT               0
#define XO_VOW_CK_EN_PERIODIC_OFF_CYCLE_MASK              0x3fff
#define XO_VOW_CK_EN_PERIODIC_OFF_CYCLE_MASK_SFT          (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG21 */
#define AUDGLB_PWRDN_PERIODIC_OFF_CYCLE_SFT               0
#define AUDGLB_PWRDN_PERIODIC_OFF_CYCLE_MASK              0x3fff
#define AUDGLB_PWRDN_PERIODIC_OFF_CYCLE_MASK_SFT          (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG22 */
#define VOW_ON_PERIODIC_OFF_CYCLE_SFT                     0
#define VOW_ON_PERIODIC_OFF_CYCLE_MASK                    0x3fff
#define VOW_ON_PERIODIC_OFF_CYCLE_MASK_SFT                (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_CFG23 */
#define DMIC_ON_PERIODIC_OFF_CYCLE_SFT                    0
#define DMIC_ON_PERIODIC_OFF_CYCLE_MASK                   0x3fff
#define DMIC_ON_PERIODIC_OFF_CYCLE_MASK_SFT               (0x3fff << 0)

/* MT6358_AFE_VOW_PERIODIC_MON0 */
#define VOW_PERIODIC_MON_SFT                              0
#define VOW_PERIODIC_MON_MASK                             0xffff
#define VOW_PERIODIC_MON_MASK_SFT                         (0xffff << 0)

/* MT6358_AFE_VOW_PERIODIC_MON1 */
#define VOW_PERIODIC_COUNT_MON_SFT                        0
#define VOW_PERIODIC_COUNT_MON_MASK                       0xffff
#define VOW_PERIODIC_COUNT_MON_MASK_SFT                   (0xffff << 0)

/* MT6358_AUDENC_DSN_ID */
#define AUDENC_ANA_ID_SFT                                 0
#define AUDENC_ANA_ID_MASK                                0xff
#define AUDENC_ANA_ID_MASK_SFT                            (0xff << 0)
#define AUDENC_DIG_ID_SFT                                 8
#define AUDENC_DIG_ID_MASK                                0xff
#define AUDENC_DIG_ID_MASK_SFT                            (0xff << 8)

/* MT6358_AUDENC_DSN_REV0 */
#define AUDENC_ANA_MINOR_REV_SFT                          0
#define AUDENC_ANA_MINOR_REV_MASK                         0xf
#define AUDENC_ANA_MINOR_REV_MASK_SFT                     (0xf << 0)
#define AUDENC_ANA_MAJOR_REV_SFT                          4
#define AUDENC_ANA_MAJOR_REV_MASK                         0xf
#define AUDENC_ANA_MAJOR_REV_MASK_SFT                     (0xf << 4)
#define AUDENC_DIG_MINOR_REV_SFT                          8
#define AUDENC_DIG_MINOR_REV_MASK                         0xf
#define AUDENC_DIG_MINOR_REV_MASK_SFT                     (0xf << 8)
#define AUDENC_DIG_MAJOR_REV_SFT                          12
#define AUDENC_DIG_MAJOR_REV_MASK                         0xf
#define AUDENC_DIG_MAJOR_REV_MASK_SFT                     (0xf << 12)

/* MT6358_AUDENC_DSN_DBI */
#define AUDENC_DSN_CBS_SFT                                0
#define AUDENC_DSN_CBS_MASK                               0x3
#define AUDENC_DSN_CBS_MASK_SFT                           (0x3 << 0)
#define AUDENC_DSN_BIX_SFT                                2
#define AUDENC_DSN_BIX_MASK                               0x3
#define AUDENC_DSN_BIX_MASK_SFT                           (0x3 << 2)
#define AUDENC_DSN_ESP_SFT                                8
#define AUDENC_DSN_ESP_MASK                               0xff
#define AUDENC_DSN_ESP_MASK_SFT                           (0xff << 8)

/* MT6358_AUDENC_DSN_FPI */
#define AUDENC_DSN_FPI_SFT                                0
#define AUDENC_DSN_FPI_MASK                               0xff
#define AUDENC_DSN_FPI_MASK_SFT                           (0xff << 0)

/* MT6358_AUDENC_ANA_CON0 */
#define RG_AUDPREAMPLON_SFT                               0
#define RG_AUDPREAMPLON_MASK                              0x1
#define RG_AUDPREAMPLON_MASK_SFT                          (0x1 << 0)
#define RG_AUDPREAMPLDCCEN_SFT                            1
#define RG_AUDPREAMPLDCCEN_MASK                           0x1
#define RG_AUDPREAMPLDCCEN_MASK_SFT                       (0x1 << 1)
#define RG_AUDPREAMPLDCPRECHARGE_SFT                      2
#define RG_AUDPREAMPLDCPRECHARGE_MASK                     0x1
#define RG_AUDPREAMPLDCPRECHARGE_MASK_SFT                 (0x1 << 2)
#define RG_AUDPREAMPLPGATEST_SFT                          3
#define RG_AUDPREAMPLPGATEST_MASK                         0x1
#define RG_AUDPREAMPLPGATEST_MASK_SFT                     (0x1 << 3)
#define RG_AUDPREAMPLVSCALE_SFT                           4
#define RG_AUDPREAMPLVSCALE_MASK                          0x3
#define RG_AUDPREAMPLVSCALE_MASK_SFT                      (0x3 << 4)
#define RG_AUDPREAMPLINPUTSEL_SFT                         6
#define RG_AUDPREAMPLINPUTSEL_MASK                        0x3
#define RG_AUDPREAMPLINPUTSEL_MASK_SFT                    (0x3 << 6)
#define RG_AUDPREAMPLGAIN_SFT                             8
#define RG_AUDPREAMPLGAIN_MASK                            0x7
#define RG_AUDPREAMPLGAIN_MASK_SFT                        (0x7 << 8)
#define RG_AUDADCLPWRUP_SFT                               12
#define RG_AUDADCLPWRUP_MASK                              0x1
#define RG_AUDADCLPWRUP_MASK_SFT                          (0x1 << 12)
#define RG_AUDADCLINPUTSEL_SFT                            13
#define RG_AUDADCLINPUTSEL_MASK                           0x3
#define RG_AUDADCLINPUTSEL_MASK_SFT                       (0x3 << 13)

/* MT6358_AUDENC_ANA_CON1 */
#define RG_AUDPREAMPRON_SFT                               0
#define RG_AUDPREAMPRON_MASK                              0x1
#define RG_AUDPREAMPRON_MASK_SFT                          (0x1 << 0)
#define RG_AUDPREAMPRDCCEN_SFT                            1
#define RG_AUDPREAMPRDCCEN_MASK                           0x1
#define RG_AUDPREAMPRDCCEN_MASK_SFT                       (0x1 << 1)
#define RG_AUDPREAMPRDCPRECHARGE_SFT                      2
#define RG_AUDPREAMPRDCPRECHARGE_MASK                     0x1
#define RG_AUDPREAMPRDCPRECHARGE_MASK_SFT                 (0x1 << 2)
#define RG_AUDPREAMPRPGATEST_SFT                          3
#define RG_AUDPREAMPRPGATEST_MASK                         0x1
#define RG_AUDPREAMPRPGATEST_MASK_SFT                     (0x1 << 3)
#define RG_AUDPREAMPRVSCALE_SFT                           4
#define RG_AUDPREAMPRVSCALE_MASK                          0x3
#define RG_AUDPREAMPRVSCALE_MASK_SFT                      (0x3 << 4)
#define RG_AUDPREAMPRINPUTSEL_SFT                         6
#define RG_AUDPREAMPRINPUTSEL_MASK                        0x3
#define RG_AUDPREAMPRINPUTSEL_MASK_SFT                    (0x3 << 6)
#define RG_AUDPREAMPRGAIN_SFT                             8
#define RG_AUDPREAMPRGAIN_MASK                            0x7
#define RG_AUDPREAMPRGAIN_MASK_SFT                        (0x7 << 8)
#define RG_AUDIO_VOW_EN_SFT                               11
#define RG_AUDIO_VOW_EN_MASK                              0x1
#define RG_AUDIO_VOW_EN_MASK_SFT                          (0x1 << 11)
#define RG_AUDADCRPWRUP_SFT                               12
#define RG_AUDADCRPWRUP_MASK                              0x1
#define RG_AUDADCRPWRUP_MASK_SFT                          (0x1 << 12)
#define RG_AUDADCRINPUTSEL_SFT                            13
#define RG_AUDADCRINPUTSEL_MASK                           0x3
#define RG_AUDADCRINPUTSEL_MASK_SFT                       (0x3 << 13)
#define RG_CLKSQ_EN_VOW_SFT                               15
#define RG_CLKSQ_EN_VOW_MASK                              0x1
#define RG_CLKSQ_EN_VOW_MASK_SFT                          (0x1 << 15)

/* MT6358_AUDENC_ANA_CON2 */
#define RG_AUDULHALFBIAS_SFT                              0
#define RG_AUDULHALFBIAS_MASK                             0x1
#define RG_AUDULHALFBIAS_MASK_SFT                         (0x1 << 0)
#define RG_AUDGLBVOWLPWEN_SFT                             1
#define RG_AUDGLBVOWLPWEN_MASK                            0x1
#define RG_AUDGLBVOWLPWEN_MASK_SFT                        (0x1 << 1)
#define RG_AUDPREAMPLPEN_SFT                              2
#define RG_AUDPREAMPLPEN_MASK                             0x1
#define RG_AUDPREAMPLPEN_MASK_SFT                         (0x1 << 2)
#define RG_AUDADC1STSTAGELPEN_SFT                         3
#define RG_AUDADC1STSTAGELPEN_MASK                        0x1
#define RG_AUDADC1STSTAGELPEN_MASK_SFT                    (0x1 << 3)
#define RG_AUDADC2NDSTAGELPEN_SFT                         4
#define RG_AUDADC2NDSTAGELPEN_MASK                        0x1
#define RG_AUDADC2NDSTAGELPEN_MASK_SFT                    (0x1 << 4)
#define RG_AUDADCFLASHLPEN_SFT                            5
#define RG_AUDADCFLASHLPEN_MASK                           0x1
#define RG_AUDADCFLASHLPEN_MASK_SFT                       (0x1 << 5)
#define RG_AUDPREAMPIDDTEST_SFT                           6
#define RG_AUDPREAMPIDDTEST_MASK                          0x3
#define RG_AUDPREAMPIDDTEST_MASK_SFT                      (0x3 << 6)
#define RG_AUDADC1STSTAGEIDDTEST_SFT                      8
#define RG_AUDADC1STSTAGEIDDTEST_MASK                     0x3
#define RG_AUDADC1STSTAGEIDDTEST_MASK_SFT                 (0x3 << 8)
#define RG_AUDADC2NDSTAGEIDDTEST_SFT                      10
#define RG_AUDADC2NDSTAGEIDDTEST_MASK                     0x3
#define RG_AUDADC2NDSTAGEIDDTEST_MASK_SFT                 (0x3 << 10)
#define RG_AUDADCREFBUFIDDTEST_SFT                        12
#define RG_AUDADCREFBUFIDDTEST_MASK                       0x3
#define RG_AUDADCREFBUFIDDTEST_MASK_SFT                   (0x3 << 12)
#define RG_AUDADCFLASHIDDTEST_SFT                         14
#define RG_AUDADCFLASHIDDTEST_MASK                        0x3
#define RG_AUDADCFLASHIDDTEST_MASK_SFT                    (0x3 << 14)

/* MT6358_AUDENC_ANA_CON3 */
#define RG_AUDADCDAC0P25FS_SFT                            0
#define RG_AUDADCDAC0P25FS_MASK                           0x1
#define RG_AUDADCDAC0P25FS_MASK_SFT                       (0x1 << 0)
#define RG_AUDADCCLKSEL_SFT                               1
#define RG_AUDADCCLKSEL_MASK                              0x1
#define RG_AUDADCCLKSEL_MASK_SFT                          (0x1 << 1)
#define RG_AUDADCCLKSOURCE_SFT                            2
#define RG_AUDADCCLKSOURCE_MASK                           0x3
#define RG_AUDADCCLKSOURCE_MASK_SFT                       (0x3 << 2)
#define RG_AUDPREAMPAAFEN_SFT                             8
#define RG_AUDPREAMPAAFEN_MASK                            0x1
#define RG_AUDPREAMPAAFEN_MASK_SFT                        (0x1 << 8)
#define RG_DCCVCMBUFLPMODSEL_SFT                          9
#define RG_DCCVCMBUFLPMODSEL_MASK                         0x1
#define RG_DCCVCMBUFLPMODSEL_MASK_SFT                     (0x1 << 9)
#define RG_DCCVCMBUFLPSWEN_SFT                            10
#define RG_DCCVCMBUFLPSWEN_MASK                           0x1
#define RG_DCCVCMBUFLPSWEN_MASK_SFT                       (0x1 << 10)
#define RG_CMSTBENH_SFT                                   11
#define RG_CMSTBENH_MASK                                  0x1
#define RG_CMSTBENH_MASK_SFT                              (0x1 << 11)
#define RG_PGABODYSW_SFT                                  12
#define RG_PGABODYSW_MASK                                 0x1
#define RG_PGABODYSW_MASK_SFT                             (0x1 << 12)

/* MT6358_AUDENC_ANA_CON4 */
#define RG_AUDADC1STSTAGESDENB_SFT                        0
#define RG_AUDADC1STSTAGESDENB_MASK                       0x1
#define RG_AUDADC1STSTAGESDENB_MASK_SFT                   (0x1 << 0)
#define RG_AUDADC2NDSTAGERESET_SFT                        1
#define RG_AUDADC2NDSTAGERESET_MASK                       0x1
#define RG_AUDADC2NDSTAGERESET_MASK_SFT                   (0x1 << 1)
#define RG_AUDADC3RDSTAGERESET_SFT                        2
#define RG_AUDADC3RDSTAGERESET_MASK                       0x1
#define RG_AUDADC3RDSTAGERESET_MASK_SFT                   (0x1 << 2)
#define RG_AUDADCFSRESET_SFT                              3
#define RG_AUDADCFSRESET_MASK                             0x1
#define RG_AUDADCFSRESET_MASK_SFT                         (0x1 << 3)
#define RG_AUDADCWIDECM_SFT                               4
#define RG_AUDADCWIDECM_MASK                              0x1
#define RG_AUDADCWIDECM_MASK_SFT                          (0x1 << 4)
#define RG_AUDADCNOPATEST_SFT                             5
#define RG_AUDADCNOPATEST_MASK                            0x1
#define RG_AUDADCNOPATEST_MASK_SFT                        (0x1 << 5)
#define RG_AUDADCBYPASS_SFT                               6
#define RG_AUDADCBYPASS_MASK                              0x1
#define RG_AUDADCBYPASS_MASK_SFT                          (0x1 << 6)
#define RG_AUDADCFFBYPASS_SFT                             7
#define RG_AUDADCFFBYPASS_MASK                            0x1
#define RG_AUDADCFFBYPASS_MASK_SFT                        (0x1 << 7)
#define RG_AUDADCDACFBCURRENT_SFT                         8
#define RG_AUDADCDACFBCURRENT_MASK                        0x1
#define RG_AUDADCDACFBCURRENT_MASK_SFT                    (0x1 << 8)
#define RG_AUDADCDACIDDTEST_SFT                           9
#define RG_AUDADCDACIDDTEST_MASK                          0x3
#define RG_AUDADCDACIDDTEST_MASK_SFT                      (0x3 << 9)
#define RG_AUDADCDACNRZ_SFT                               11
#define RG_AUDADCDACNRZ_MASK                              0x1
#define RG_AUDADCDACNRZ_MASK_SFT                          (0x1 << 11)
#define RG_AUDADCNODEM_SFT                                12
#define RG_AUDADCNODEM_MASK                               0x1
#define RG_AUDADCNODEM_MASK_SFT                           (0x1 << 12)
#define RG_AUDADCDACTEST_SFT                              13
#define RG_AUDADCDACTEST_MASK                             0x1
#define RG_AUDADCDACTEST_MASK_SFT                         (0x1 << 13)

/* MT6358_AUDENC_ANA_CON5 */
#define RG_AUDRCTUNEL_SFT                                 0
#define RG_AUDRCTUNEL_MASK                                0x1f
#define RG_AUDRCTUNEL_MASK_SFT                            (0x1f << 0)
#define RG_AUDRCTUNELSEL_SFT                              5
#define RG_AUDRCTUNELSEL_MASK                             0x1
#define RG_AUDRCTUNELSEL_MASK_SFT                         (0x1 << 5)
#define RG_AUDRCTUNER_SFT                                 8
#define RG_AUDRCTUNER_MASK                                0x1f
#define RG_AUDRCTUNER_MASK_SFT                            (0x1f << 8)
#define RG_AUDRCTUNERSEL_SFT                              13
#define RG_AUDRCTUNERSEL_MASK                             0x1
#define RG_AUDRCTUNERSEL_MASK_SFT                         (0x1 << 13)

/* MT6358_AUDENC_ANA_CON6 */
#define RG_CLKSQ_EN_SFT                                   0
#define RG_CLKSQ_EN_MASK                                  0x1
#define RG_CLKSQ_EN_MASK_SFT                              (0x1 << 0)
#define RG_CLKSQ_IN_SEL_TEST_SFT                          1
#define RG_CLKSQ_IN_SEL_TEST_MASK                         0x1
#define RG_CLKSQ_IN_SEL_TEST_MASK_SFT                     (0x1 << 1)
#define RG_CM_REFGENSEL_SFT                               2
#define RG_CM_REFGENSEL_MASK                              0x1
#define RG_CM_REFGENSEL_MASK_SFT                          (0x1 << 2)
#define RG_AUDSPARE_SFT                                   4
#define RG_AUDSPARE_MASK                                  0xf
#define RG_AUDSPARE_MASK_SFT                              (0xf << 4)
#define RG_AUDENCSPARE_SFT                                8
#define RG_AUDENCSPARE_MASK                               0x3f
#define RG_AUDENCSPARE_MASK_SFT                           (0x3f << 8)

/* MT6358_AUDENC_ANA_CON7 */
#define RG_AUDENCSPARE2_SFT                               0
#define RG_AUDENCSPARE2_MASK                              0xff
#define RG_AUDENCSPARE2_MASK_SFT                          (0xff << 0)

/* MT6358_AUDENC_ANA_CON8 */
#define RG_AUDDIGMICEN_SFT                                0
#define RG_AUDDIGMICEN_MASK                               0x1
#define RG_AUDDIGMICEN_MASK_SFT                           (0x1 << 0)
#define RG_AUDDIGMICBIAS_SFT                              1
#define RG_AUDDIGMICBIAS_MASK                             0x3
#define RG_AUDDIGMICBIAS_MASK_SFT                         (0x3 << 1)
#define RG_DMICHPCLKEN_SFT                                3
#define RG_DMICHPCLKEN_MASK                               0x1
#define RG_DMICHPCLKEN_MASK_SFT                           (0x1 << 3)
#define RG_AUDDIGMICPDUTY_SFT                             4
#define RG_AUDDIGMICPDUTY_MASK                            0x3
#define RG_AUDDIGMICPDUTY_MASK_SFT                        (0x3 << 4)
#define RG_AUDDIGMICNDUTY_SFT                             6
#define RG_AUDDIGMICNDUTY_MASK                            0x3
#define RG_AUDDIGMICNDUTY_MASK_SFT                        (0x3 << 6)
#define RG_DMICMONEN_SFT                                  8
#define RG_DMICMONEN_MASK                                 0x1
#define RG_DMICMONEN_MASK_SFT                             (0x1 << 8)
#define RG_DMICMONSEL_SFT                                 9
#define RG_DMICMONSEL_MASK                                0x7
#define RG_DMICMONSEL_MASK_SFT                            (0x7 << 9)
#define RG_AUDSPAREVMIC_SFT                               12
#define RG_AUDSPAREVMIC_MASK                              0xf
#define RG_AUDSPAREVMIC_MASK_SFT                          (0xf << 12)

/* MT6358_AUDENC_ANA_CON9 */
#define RG_AUDPWDBMICBIAS0_SFT                            0
#define RG_AUDPWDBMICBIAS0_MASK                           0x1
#define RG_AUDPWDBMICBIAS0_MASK_SFT                       (0x1 << 0)
#define RG_AUDMICBIAS0BYPASSEN_SFT                        1
#define RG_AUDMICBIAS0BYPASSEN_MASK                       0x1
#define RG_AUDMICBIAS0BYPASSEN_MASK_SFT                   (0x1 << 1)
#define RG_AUDMICBIAS0LOWPEN_SFT                          2
#define RG_AUDMICBIAS0LOWPEN_MASK                         0x1
#define RG_AUDMICBIAS0LOWPEN_MASK_SFT                     (0x1 << 2)
#define RG_AUDMICBIAS0VREF_SFT                            4
#define RG_AUDMICBIAS0VREF_MASK                           0x7
#define RG_AUDMICBIAS0VREF_MASK_SFT                       (0x7 << 4)
#define RG_AUDMICBIAS0DCSW0P1EN_SFT                       8
#define RG_AUDMICBIAS0DCSW0P1EN_MASK                      0x1
#define RG_AUDMICBIAS0DCSW0P1EN_MASK_SFT                  (0x1 << 8)
#define RG_AUDMICBIAS0DCSW0P2EN_SFT                       9
#define RG_AUDMICBIAS0DCSW0P2EN_MASK                      0x1
#define RG_AUDMICBIAS0DCSW0P2EN_MASK_SFT                  (0x1 << 9)
#define RG_AUDMICBIAS0DCSW0NEN_SFT                        10
#define RG_AUDMICBIAS0DCSW0NEN_MASK                       0x1
#define RG_AUDMICBIAS0DCSW0NEN_MASK_SFT                   (0x1 << 10)
#define RG_AUDMICBIAS0DCSW2P1EN_SFT                       12
#define RG_AUDMICBIAS0DCSW2P1EN_MASK                      0x1
#define RG_AUDMICBIAS0DCSW2P1EN_MASK_SFT                  (0x1 << 12)
#define RG_AUDMICBIAS0DCSW2P2EN_SFT                       13
#define RG_AUDMICBIAS0DCSW2P2EN_MASK                      0x1
#define RG_AUDMICBIAS0DCSW2P2EN_MASK_SFT                  (0x1 << 13)
#define RG_AUDMICBIAS0DCSW2NEN_SFT                        14
#define RG_AUDMICBIAS0DCSW2NEN_MASK                       0x1
#define RG_AUDMICBIAS0DCSW2NEN_MASK_SFT                   (0x1 << 14)

/* MT6358_AUDENC_ANA_CON10 */
#define RG_AUDPWDBMICBIAS1_SFT                            0
#define RG_AUDPWDBMICBIAS1_MASK                           0x1
#define RG_AUDPWDBMICBIAS1_MASK_SFT                       (0x1 << 0)
#define RG_AUDMICBIAS1BYPASSEN_SFT                        1
#define RG_AUDMICBIAS1BYPASSEN_MASK                       0x1
#define RG_AUDMICBIAS1BYPASSEN_MASK_SFT                   (0x1 << 1)
#define RG_AUDMICBIAS1LOWPEN_SFT                          2
#define RG_AUDMICBIAS1LOWPEN_MASK                         0x1
#define RG_AUDMICBIAS1LOWPEN_MASK_SFT                     (0x1 << 2)
#define RG_AUDMICBIAS1VREF_SFT                            4
#define RG_AUDMICBIAS1VREF_MASK                           0x7
#define RG_AUDMICBIAS1VREF_MASK_SFT                       (0x7 << 4)
#define RG_AUDMICBIAS1DCSW1PEN_SFT                        8
#define RG_AUDMICBIAS1DCSW1PEN_MASK                       0x1
#define RG_AUDMICBIAS1DCSW1PEN_MASK_SFT                   (0x1 << 8)
#define RG_AUDMICBIAS1DCSW1NEN_SFT                        9
#define RG_AUDMICBIAS1DCSW1NEN_MASK                       0x1
#define RG_AUDMICBIAS1DCSW1NEN_MASK_SFT                   (0x1 << 9)
#define RG_BANDGAPGEN_SFT                                 12
#define RG_BANDGAPGEN_MASK                                0x1
#define RG_BANDGAPGEN_MASK_SFT                            (0x1 << 12)
#define RG_MTEST_EN_SFT                                   13
#define RG_MTEST_EN_MASK                                  0x1
#define RG_MTEST_EN_MASK_SFT                              (0x1 << 13)
#define RG_MTEST_SEL_SFT                                  14
#define RG_MTEST_SEL_MASK                                 0x1
#define RG_MTEST_SEL_MASK_SFT                             (0x1 << 14)
#define RG_MTEST_CURRENT_SFT                              15
#define RG_MTEST_CURRENT_MASK                             0x1
#define RG_MTEST_CURRENT_MASK_SFT                         (0x1 << 15)

/* MT6358_AUDENC_ANA_CON11 */
#define RG_AUDACCDETMICBIAS0PULLLOW_SFT                   0
#define RG_AUDACCDETMICBIAS0PULLLOW_MASK                  0x1
#define RG_AUDACCDETMICBIAS0PULLLOW_MASK_SFT              (0x1 << 0)
#define RG_AUDACCDETMICBIAS1PULLLOW_SFT                   1
#define RG_AUDACCDETMICBIAS1PULLLOW_MASK                  0x1
#define RG_AUDACCDETMICBIAS1PULLLOW_MASK_SFT              (0x1 << 1)
#define RG_AUDACCDETVIN1PULLLOW_SFT                       2
#define RG_AUDACCDETVIN1PULLLOW_MASK                      0x1
#define RG_AUDACCDETVIN1PULLLOW_MASK_SFT                  (0x1 << 2)
#define RG_AUDACCDETVTHACAL_SFT                           4
#define RG_AUDACCDETVTHACAL_MASK                          0x1
#define RG_AUDACCDETVTHACAL_MASK_SFT                      (0x1 << 4)
#define RG_AUDACCDETVTHBCAL_SFT                           5
#define RG_AUDACCDETVTHBCAL_MASK                          0x1
#define RG_AUDACCDETVTHBCAL_MASK_SFT                      (0x1 << 5)
#define RG_AUDACCDETTVDET_SFT                             6
#define RG_AUDACCDETTVDET_MASK                            0x1
#define RG_AUDACCDETTVDET_MASK_SFT                        (0x1 << 6)
#define RG_ACCDETSEL_SFT                                  7
#define RG_ACCDETSEL_MASK                                 0x1
#define RG_ACCDETSEL_MASK_SFT                             (0x1 << 7)
#define RG_SWBUFMODSEL_SFT                                8
#define RG_SWBUFMODSEL_MASK                               0x1
#define RG_SWBUFMODSEL_MASK_SFT                           (0x1 << 8)
#define RG_SWBUFSWEN_SFT                                  9
#define RG_SWBUFSWEN_MASK                                 0x1
#define RG_SWBUFSWEN_MASK_SFT                             (0x1 << 9)
#define RG_EINTCOMPVTH_SFT                                10
#define RG_EINTCOMPVTH_MASK                               0x1
#define RG_EINTCOMPVTH_MASK_SFT                           (0x1 << 10)
#define RG_EINTCONFIGACCDET_SFT                           11
#define RG_EINTCONFIGACCDET_MASK                          0x1
#define RG_EINTCONFIGACCDET_MASK_SFT                      (0x1 << 11)
#define RG_EINTHIRENB_SFT                                 12
#define RG_EINTHIRENB_MASK                                0x1
#define RG_EINTHIRENB_MASK_SFT                            (0x1 << 12)
#define RG_ACCDET2AUXRESBYPASS_SFT                        13
#define RG_ACCDET2AUXRESBYPASS_MASK                       0x1
#define RG_ACCDET2AUXRESBYPASS_MASK_SFT                   (0x1 << 13)
#define RG_ACCDET2AUXBUFFERBYPASS_SFT                     14
#define RG_ACCDET2AUXBUFFERBYPASS_MASK                    0x1
#define RG_ACCDET2AUXBUFFERBYPASS_MASK_SFT                (0x1 << 14)
#define RG_ACCDET2AUXSWEN_SFT                             15
#define RG_ACCDET2AUXSWEN_MASK                            0x1
#define RG_ACCDET2AUXSWEN_MASK_SFT                        (0x1 << 15)

/* MT6358_AUDENC_ANA_CON12 */
#define RGS_AUDRCTUNELREAD_SFT                            0
#define RGS_AUDRCTUNELREAD_MASK                           0x1f
#define RGS_AUDRCTUNELREAD_MASK_SFT                       (0x1f << 0)
#define RGS_AUDRCTUNERREAD_SFT                            8
#define RGS_AUDRCTUNERREAD_MASK                           0x1f
#define RGS_AUDRCTUNERREAD_MASK_SFT                       (0x1f << 8)

/* MT6358_AUDDEC_DSN_ID */
#define AUDDEC_ANA_ID_SFT                                 0
#define AUDDEC_ANA_ID_MASK                                0xff
#define AUDDEC_ANA_ID_MASK_SFT                            (0xff << 0)
#define AUDDEC_DIG_ID_SFT                                 8
#define AUDDEC_DIG_ID_MASK                                0xff
#define AUDDEC_DIG_ID_MASK_SFT                            (0xff << 8)

/* MT6358_AUDDEC_DSN_REV0 */
#define AUDDEC_ANA_MINOR_REV_SFT                          0
#define AUDDEC_ANA_MINOR_REV_MASK                         0xf
#define AUDDEC_ANA_MINOR_REV_MASK_SFT                     (0xf << 0)
#define AUDDEC_ANA_MAJOR_REV_SFT                          4
#define AUDDEC_ANA_MAJOR_REV_MASK                         0xf
#define AUDDEC_ANA_MAJOR_REV_MASK_SFT                     (0xf << 4)
#define AUDDEC_DIG_MINOR_REV_SFT                          8
#define AUDDEC_DIG_MINOR_REV_MASK                         0xf
#define AUDDEC_DIG_MINOR_REV_MASK_SFT                     (0xf << 8)
#define AUDDEC_DIG_MAJOR_REV_SFT                          12
#define AUDDEC_DIG_MAJOR_REV_MASK                         0xf
#define AUDDEC_DIG_MAJOR_REV_MASK_SFT                     (0xf << 12)

/* MT6358_AUDDEC_DSN_DBI */
#define AUDDEC_DSN_CBS_SFT                                0
#define AUDDEC_DSN_CBS_MASK                               0x3
#define AUDDEC_DSN_CBS_MASK_SFT                           (0x3 << 0)
#define AUDDEC_DSN_BIX_SFT                                2
#define AUDDEC_DSN_BIX_MASK                               0x3
#define AUDDEC_DSN_BIX_MASK_SFT                           (0x3 << 2)
#define AUDDEC_DSN_ESP_SFT                                8
#define AUDDEC_DSN_ESP_MASK                               0xff
#define AUDDEC_DSN_ESP_MASK_SFT                           (0xff << 8)

/* MT6358_AUDDEC_DSN_FPI */
#define AUDDEC_DSN_FPI_SFT                                0
#define AUDDEC_DSN_FPI_MASK                               0xff
#define AUDDEC_DSN_FPI_MASK_SFT                           (0xff << 0)

/* MT6358_AUDDEC_ANA_CON0 */
#define RG_AUDDACLPWRUP_VAUDP15_SFT                       0
#define RG_AUDDACLPWRUP_VAUDP15_MASK                      0x1
#define RG_AUDDACLPWRUP_VAUDP15_MASK_SFT                  (0x1 << 0)
#define RG_AUDDACRPWRUP_VAUDP15_SFT                       1
#define RG_AUDDACRPWRUP_VAUDP15_MASK                      0x1
#define RG_AUDDACRPWRUP_VAUDP15_MASK_SFT                  (0x1 << 1)
#define RG_AUD_DAC_PWR_UP_VA28_SFT                        2
#define RG_AUD_DAC_PWR_UP_VA28_MASK                       0x1
#define RG_AUD_DAC_PWR_UP_VA28_MASK_SFT                   (0x1 << 2)
#define RG_AUD_DAC_PWL_UP_VA28_SFT                        3
#define RG_AUD_DAC_PWL_UP_VA28_MASK                       0x1
#define RG_AUD_DAC_PWL_UP_VA28_MASK_SFT                   (0x1 << 3)
#define RG_AUDHPLPWRUP_VAUDP15_SFT                        4
#define RG_AUDHPLPWRUP_VAUDP15_MASK                       0x1
#define RG_AUDHPLPWRUP_VAUDP15_MASK_SFT                   (0x1 << 4)
#define RG_AUDHPRPWRUP_VAUDP15_SFT                        5
#define RG_AUDHPRPWRUP_VAUDP15_MASK                       0x1
#define RG_AUDHPRPWRUP_VAUDP15_MASK_SFT                   (0x1 << 5)
#define RG_AUDHPLPWRUP_IBIAS_VAUDP15_SFT                  6
#define RG_AUDHPLPWRUP_IBIAS_VAUDP15_MASK                 0x1
#define RG_AUDHPLPWRUP_IBIAS_VAUDP15_MASK_SFT             (0x1 << 6)
#define RG_AUDHPRPWRUP_IBIAS_VAUDP15_SFT                  7
#define RG_AUDHPRPWRUP_IBIAS_VAUDP15_MASK                 0x1
#define RG_AUDHPRPWRUP_IBIAS_VAUDP15_MASK_SFT             (0x1 << 7)
#define RG_AUDHPLMUXINPUTSEL_VAUDP15_SFT                  8
#define RG_AUDHPLMUXINPUTSEL_VAUDP15_MASK                 0x3
#define RG_AUDHPLMUXINPUTSEL_VAUDP15_MASK_SFT             (0x3 << 8)
#define RG_AUDHPRMUXINPUTSEL_VAUDP15_SFT                  10
#define RG_AUDHPRMUXINPUTSEL_VAUDP15_MASK                 0x3
#define RG_AUDHPRMUXINPUTSEL_VAUDP15_MASK_SFT             (0x3 << 10)
#define RG_AUDHPLSCDISABLE_VAUDP15_SFT                    12
#define RG_AUDHPLSCDISABLE_VAUDP15_MASK                   0x1
#define RG_AUDHPLSCDISABLE_VAUDP15_MASK_SFT               (0x1 << 12)
#define RG_AUDHPRSCDISABLE_VAUDP15_SFT                    13
#define RG_AUDHPRSCDISABLE_VAUDP15_MASK                   0x1
#define RG_AUDHPRSCDISABLE_VAUDP15_MASK_SFT               (0x1 << 13)
#define RG_AUDHPLBSCCURRENT_VAUDP15_SFT                   14
#define RG_AUDHPLBSCCURRENT_VAUDP15_MASK                  0x1
#define RG_AUDHPLBSCCURRENT_VAUDP15_MASK_SFT              (0x1 << 14)
#define RG_AUDHPRBSCCURRENT_VAUDP15_SFT                   15
#define RG_AUDHPRBSCCURRENT_VAUDP15_MASK                  0x1
#define RG_AUDHPRBSCCURRENT_VAUDP15_MASK_SFT              (0x1 << 15)

/* MT6358_AUDDEC_ANA_CON1 */
#define RG_AUDHPLOUTPWRUP_VAUDP15_SFT                     0
#define RG_AUDHPLOUTPWRUP_VAUDP15_MASK                    0x1
#define RG_AUDHPLOUTPWRUP_VAUDP15_MASK_SFT                (0x1 << 0)
#define RG_AUDHPROUTPWRUP_VAUDP15_SFT                     1
#define RG_AUDHPROUTPWRUP_VAUDP15_MASK                    0x1
#define RG_AUDHPROUTPWRUP_VAUDP15_MASK_SFT                (0x1 << 1)
#define RG_AUDHPLOUTAUXPWRUP_VAUDP15_SFT                  2
#define RG_AUDHPLOUTAUXPWRUP_VAUDP15_MASK                 0x1
#define RG_AUDHPLOUTAUXPWRUP_VAUDP15_MASK_SFT             (0x1 << 2)
#define RG_AUDHPROUTAUXPWRUP_VAUDP15_SFT                  3
#define RG_AUDHPROUTAUXPWRUP_VAUDP15_MASK                 0x1
#define RG_AUDHPROUTAUXPWRUP_VAUDP15_MASK_SFT             (0x1 << 3)
#define RG_HPLAUXFBRSW_EN_VAUDP15_SFT                     4
#define RG_HPLAUXFBRSW_EN_VAUDP15_MASK                    0x1
#define RG_HPLAUXFBRSW_EN_VAUDP15_MASK_SFT                (0x1 << 4)
#define RG_HPRAUXFBRSW_EN_VAUDP15_SFT                     5
#define RG_HPRAUXFBRSW_EN_VAUDP15_MASK                    0x1
#define RG_HPRAUXFBRSW_EN_VAUDP15_MASK_SFT                (0x1 << 5)
#define RG_HPLSHORT2HPLAUX_EN_VAUDP15_SFT                 6
#define RG_HPLSHORT2HPLAUX_EN_VAUDP15_MASK                0x1
#define RG_HPLSHORT2HPLAUX_EN_VAUDP15_MASK_SFT            (0x1 << 6)
#define RG_HPRSHORT2HPRAUX_EN_VAUDP15_SFT                 7
#define RG_HPRSHORT2HPRAUX_EN_VAUDP15_MASK                0x1
#define RG_HPRSHORT2HPRAUX_EN_VAUDP15_MASK_SFT            (0x1 << 7)
#define RG_HPLOUTSTGCTRL_VAUDP15_SFT                      8
#define RG_HPLOUTSTGCTRL_VAUDP15_MASK                     0x7
#define RG_HPLOUTSTGCTRL_VAUDP15_MASK_SFT                 (0x7 << 8)
#define RG_HPROUTSTGCTRL_VAUDP15_SFT                      11
#define RG_HPROUTSTGCTRL_VAUDP15_MASK                     0x7
#define RG_HPROUTSTGCTRL_VAUDP15_MASK_SFT                 (0x7 << 11)

/* MT6358_AUDDEC_ANA_CON2 */
#define RG_HPLOUTPUTSTBENH_VAUDP15_SFT                    0
#define RG_HPLOUTPUTSTBENH_VAUDP15_MASK                   0x7
#define RG_HPLOUTPUTSTBENH_VAUDP15_MASK_SFT               (0x7 << 0)
#define RG_HPROUTPUTSTBENH_VAUDP15_SFT                    4
#define RG_HPROUTPUTSTBENH_VAUDP15_MASK                   0x7
#define RG_HPROUTPUTSTBENH_VAUDP15_MASK_SFT               (0x7 << 4)
#define RG_AUDHPSTARTUP_VAUDP15_SFT                       13
#define RG_AUDHPSTARTUP_VAUDP15_MASK                      0x1
#define RG_AUDHPSTARTUP_VAUDP15_MASK_SFT                  (0x1 << 13)
#define RG_AUDREFN_DERES_EN_VAUDP15_SFT                   14
#define RG_AUDREFN_DERES_EN_VAUDP15_MASK                  0x1
#define RG_AUDREFN_DERES_EN_VAUDP15_MASK_SFT              (0x1 << 14)
#define RG_HPPSHORT2VCM_VAUDP15_SFT                       15
#define RG_HPPSHORT2VCM_VAUDP15_MASK                      0x1
#define RG_HPPSHORT2VCM_VAUDP15_MASK_SFT                  (0x1 << 15)

/* MT6358_AUDDEC_ANA_CON3 */
#define RG_HPINPUTSTBENH_VAUDP15_SFT                      13
#define RG_HPINPUTSTBENH_VAUDP15_MASK                     0x1
#define RG_HPINPUTSTBENH_VAUDP15_MASK_SFT                 (0x1 << 13)
#define RG_HPINPUTRESET0_VAUDP15_SFT                      14
#define RG_HPINPUTRESET0_VAUDP15_MASK                     0x1
#define RG_HPINPUTRESET0_VAUDP15_MASK_SFT                 (0x1 << 14)
#define RG_HPOUTPUTRESET0_VAUDP15_SFT                     15
#define RG_HPOUTPUTRESET0_VAUDP15_MASK                    0x1
#define RG_HPOUTPUTRESET0_VAUDP15_MASK_SFT                (0x1 << 15)

/* MT6358_AUDDEC_ANA_CON4 */
#define RG_ABIDEC_RSVD0_VAUDP28_SFT                       0
#define RG_ABIDEC_RSVD0_VAUDP28_MASK                      0xff
#define RG_ABIDEC_RSVD0_VAUDP28_MASK_SFT                  (0xff << 0)

/* MT6358_AUDDEC_ANA_CON5 */
#define RG_AUDHPDECMGAINADJ_VAUDP15_SFT                   0
#define RG_AUDHPDECMGAINADJ_VAUDP15_MASK                  0x7
#define RG_AUDHPDECMGAINADJ_VAUDP15_MASK_SFT              (0x7 << 0)
#define RG_AUDHPDEDMGAINADJ_VAUDP15_SFT                   4
#define RG_AUDHPDEDMGAINADJ_VAUDP15_MASK                  0x7
#define RG_AUDHPDEDMGAINADJ_VAUDP15_MASK_SFT              (0x7 << 4)

/* MT6358_AUDDEC_ANA_CON6 */
#define RG_AUDHSPWRUP_VAUDP15_SFT                         0
#define RG_AUDHSPWRUP_VAUDP15_MASK                        0x1
#define RG_AUDHSPWRUP_VAUDP15_MASK_SFT                    (0x1 << 0)
#define RG_AUDHSPWRUP_IBIAS_VAUDP15_SFT                   1
#define RG_AUDHSPWRUP_IBIAS_VAUDP15_MASK                  0x1
#define RG_AUDHSPWRUP_IBIAS_VAUDP15_MASK_SFT              (0x1 << 1)
#define RG_AUDHSMUXINPUTSEL_VAUDP15_SFT                   2
#define RG_AUDHSMUXINPUTSEL_VAUDP15_MASK                  0x3
#define RG_AUDHSMUXINPUTSEL_VAUDP15_MASK_SFT              (0x3 << 2)
#define RG_AUDHSSCDISABLE_VAUDP15_SFT                     4
#define RG_AUDHSSCDISABLE_VAUDP15_MASK                    0x1
#define RG_AUDHSSCDISABLE_VAUDP15_MASK_SFT                (0x1 << 4)
#define RG_AUDHSBSCCURRENT_VAUDP15_SFT                    5
#define RG_AUDHSBSCCURRENT_VAUDP15_MASK                   0x1
#define RG_AUDHSBSCCURRENT_VAUDP15_MASK_SFT               (0x1 << 5)
#define RG_AUDHSSTARTUP_VAUDP15_SFT                       6
#define RG_AUDHSSTARTUP_VAUDP15_MASK                      0x1
#define RG_AUDHSSTARTUP_VAUDP15_MASK_SFT                  (0x1 << 6)
#define RG_HSOUTPUTSTBENH_VAUDP15_SFT                     7
#define RG_HSOUTPUTSTBENH_VAUDP15_MASK                    0x1
#define RG_HSOUTPUTSTBENH_VAUDP15_MASK_SFT                (0x1 << 7)
#define RG_HSINPUTSTBENH_VAUDP15_SFT                      8
#define RG_HSINPUTSTBENH_VAUDP15_MASK                     0x1
#define RG_HSINPUTSTBENH_VAUDP15_MASK_SFT                 (0x1 << 8)
#define RG_HSINPUTRESET0_VAUDP15_SFT                      9
#define RG_HSINPUTRESET0_VAUDP15_MASK                     0x1
#define RG_HSINPUTRESET0_VAUDP15_MASK_SFT                 (0x1 << 9)
#define RG_HSOUTPUTRESET0_VAUDP15_SFT                     10
#define RG_HSOUTPUTRESET0_VAUDP15_MASK                    0x1
#define RG_HSOUTPUTRESET0_VAUDP15_MASK_SFT                (0x1 << 10)
#define RG_HSOUT_SHORTVCM_VAUDP15_SFT                     11
#define RG_HSOUT_SHORTVCM_VAUDP15_MASK                    0x1
#define RG_HSOUT_SHORTVCM_VAUDP15_MASK_SFT                (0x1 << 11)

/* MT6358_AUDDEC_ANA_CON7 */
#define RG_AUDLOLPWRUP_VAUDP15_SFT                        0
#define RG_AUDLOLPWRUP_VAUDP15_MASK                       0x1
#define RG_AUDLOLPWRUP_VAUDP15_MASK_SFT                   (0x1 << 0)
#define RG_AUDLOLPWRUP_IBIAS_VAUDP15_SFT                  1
#define RG_AUDLOLPWRUP_IBIAS_VAUDP15_MASK                 0x1
#define RG_AUDLOLPWRUP_IBIAS_VAUDP15_MASK_SFT             (0x1 << 1)
#define RG_AUDLOLMUXINPUTSEL_VAUDP15_SFT                  2
#define RG_AUDLOLMUXINPUTSEL_VAUDP15_MASK                 0x3
#define RG_AUDLOLMUXINPUTSEL_VAUDP15_MASK_SFT             (0x3 << 2)
#define RG_AUDLOLSCDISABLE_VAUDP15_SFT                    4
#define RG_AUDLOLSCDISABLE_VAUDP15_MASK                   0x1
#define RG_AUDLOLSCDISABLE_VAUDP15_MASK_SFT               (0x1 << 4)
#define RG_AUDLOLBSCCURRENT_VAUDP15_SFT                   5
#define RG_AUDLOLBSCCURRENT_VAUDP15_MASK                  0x1
#define RG_AUDLOLBSCCURRENT_VAUDP15_MASK_SFT              (0x1 << 5)
#define RG_AUDLOSTARTUP_VAUDP15_SFT                       6
#define RG_AUDLOSTARTUP_VAUDP15_MASK                      0x1
#define RG_AUDLOSTARTUP_VAUDP15_MASK_SFT                  (0x1 << 6)
#define RG_LOINPUTSTBENH_VAUDP15_SFT                      7
#define RG_LOINPUTSTBENH_VAUDP15_MASK                     0x1
#define RG_LOINPUTSTBENH_VAUDP15_MASK_SFT                 (0x1 << 7)
#define RG_LOOUTPUTSTBENH_VAUDP15_SFT                     8
#define RG_LOOUTPUTSTBENH_VAUDP15_MASK                    0x1
#define RG_LOOUTPUTSTBENH_VAUDP15_MASK_SFT                (0x1 << 8)
#define RG_LOINPUTRESET0_VAUDP15_SFT                      9
#define RG_LOINPUTRESET0_VAUDP15_MASK                     0x1
#define RG_LOINPUTRESET0_VAUDP15_MASK_SFT                 (0x1 << 9)
#define RG_LOOUTPUTRESET0_VAUDP15_SFT                     10
#define RG_LOOUTPUTRESET0_VAUDP15_MASK                    0x1
#define RG_LOOUTPUTRESET0_VAUDP15_MASK_SFT                (0x1 << 10)
#define RG_LOOUT_SHORTVCM_VAUDP15_SFT                     11
#define RG_LOOUT_SHORTVCM_VAUDP15_MASK                    0x1
#define RG_LOOUT_SHORTVCM_VAUDP15_MASK_SFT                (0x1 << 11)

/* MT6358_AUDDEC_ANA_CON8 */
#define RG_AUDTRIMBUF_INPUTMUXSEL_VAUDP15_SFT             0
#define RG_AUDTRIMBUF_INPUTMUXSEL_VAUDP15_MASK            0xf
#define RG_AUDTRIMBUF_INPUTMUXSEL_VAUDP15_MASK_SFT        (0xf << 0)
#define RG_AUDTRIMBUF_GAINSEL_VAUDP15_SFT                 4
#define RG_AUDTRIMBUF_GAINSEL_VAUDP15_MASK                0x3
#define RG_AUDTRIMBUF_GAINSEL_VAUDP15_MASK_SFT            (0x3 << 4)
#define RG_AUDTRIMBUF_EN_VAUDP15_SFT                      6
#define RG_AUDTRIMBUF_EN_VAUDP15_MASK                     0x1
#define RG_AUDTRIMBUF_EN_VAUDP15_MASK_SFT                 (0x1 << 6)
#define RG_AUDHPSPKDET_INPUTMUXSEL_VAUDP15_SFT            8
#define RG_AUDHPSPKDET_INPUTMUXSEL_VAUDP15_MASK           0x3
#define RG_AUDHPSPKDET_INPUTMUXSEL_VAUDP15_MASK_SFT       (0x3 << 8)
#define RG_AUDHPSPKDET_OUTPUTMUXSEL_VAUDP15_SFT           10
#define RG_AUDHPSPKDET_OUTPUTMUXSEL_VAUDP15_MASK          0x3
#define RG_AUDHPSPKDET_OUTPUTMUXSEL_VAUDP15_MASK_SFT      (0x3 << 10)
#define RG_AUDHPSPKDET_EN_VAUDP15_SFT                     12
#define RG_AUDHPSPKDET_EN_VAUDP15_MASK                    0x1
#define RG_AUDHPSPKDET_EN_VAUDP15_MASK_SFT                (0x1 << 12)

/* MT6358_AUDDEC_ANA_CON9 */
#define RG_ABIDEC_RSVD0_VA28_SFT                          0
#define RG_ABIDEC_RSVD0_VA28_MASK                         0xff
#define RG_ABIDEC_RSVD0_VA28_MASK_SFT                     (0xff << 0)
#define RG_ABIDEC_RSVD0_VAUDP15_SFT                       8
#define RG_ABIDEC_RSVD0_VAUDP15_MASK                      0xff
#define RG_ABIDEC_RSVD0_VAUDP15_MASK_SFT                  (0xff << 8)

/* MT6358_AUDDEC_ANA_CON10 */
#define RG_ABIDEC_RSVD1_VAUDP15_SFT                       0
#define RG_ABIDEC_RSVD1_VAUDP15_MASK                      0xff
#define RG_ABIDEC_RSVD1_VAUDP15_MASK_SFT                  (0xff << 0)
#define RG_ABIDEC_RSVD2_VAUDP15_SFT                       8
#define RG_ABIDEC_RSVD2_VAUDP15_MASK                      0xff
#define RG_ABIDEC_RSVD2_VAUDP15_MASK_SFT                  (0xff << 8)

/* MT6358_AUDDEC_ANA_CON11 */
#define RG_AUDZCDMUXSEL_VAUDP15_SFT                       0
#define RG_AUDZCDMUXSEL_VAUDP15_MASK                      0x7
#define RG_AUDZCDMUXSEL_VAUDP15_MASK_SFT                  (0x7 << 0)
#define RG_AUDZCDCLKSEL_VAUDP15_SFT                       3
#define RG_AUDZCDCLKSEL_VAUDP15_MASK                      0x1
#define RG_AUDZCDCLKSEL_VAUDP15_MASK_SFT                  (0x1 << 3)
#define RG_AUDBIASADJ_0_VAUDP15_SFT                       7
#define RG_AUDBIASADJ_0_VAUDP15_MASK                      0x1ff
#define RG_AUDBIASADJ_0_VAUDP15_MASK_SFT                  (0x1ff << 7)

/* MT6358_AUDDEC_ANA_CON12 */
#define RG_AUDBIASADJ_1_VAUDP15_SFT                       0
#define RG_AUDBIASADJ_1_VAUDP15_MASK                      0xff
#define RG_AUDBIASADJ_1_VAUDP15_MASK_SFT                  (0xff << 0)
#define RG_AUDIBIASPWRDN_VAUDP15_SFT                      8
#define RG_AUDIBIASPWRDN_VAUDP15_MASK                     0x1
#define RG_AUDIBIASPWRDN_VAUDP15_MASK_SFT                 (0x1 << 8)

/* MT6358_AUDDEC_ANA_CON13 */
#define RG_RSTB_DECODER_VA28_SFT                          0
#define RG_RSTB_DECODER_VA28_MASK                         0x1
#define RG_RSTB_DECODER_VA28_MASK_SFT                     (0x1 << 0)
#define RG_SEL_DECODER_96K_VA28_SFT                       1
#define RG_SEL_DECODER_96K_VA28_MASK                      0x1
#define RG_SEL_DECODER_96K_VA28_MASK_SFT                  (0x1 << 1)
#define RG_SEL_DELAY_VCORE_SFT                            2
#define RG_SEL_DELAY_VCORE_MASK                           0x1
#define RG_SEL_DELAY_VCORE_MASK_SFT                       (0x1 << 2)
#define RG_AUDGLB_PWRDN_VA28_SFT                          4
#define RG_AUDGLB_PWRDN_VA28_MASK                         0x1
#define RG_AUDGLB_PWRDN_VA28_MASK_SFT                     (0x1 << 4)
#define RG_RSTB_ENCODER_VA28_SFT                          5
#define RG_RSTB_ENCODER_VA28_MASK                         0x1
#define RG_RSTB_ENCODER_VA28_MASK_SFT                     (0x1 << 5)
#define RG_SEL_ENCODER_96K_VA28_SFT                       6
#define RG_SEL_ENCODER_96K_VA28_MASK                      0x1
#define RG_SEL_ENCODER_96K_VA28_MASK_SFT                  (0x1 << 6)

/* MT6358_AUDDEC_ANA_CON14 */
#define RG_HCLDO_EN_VA18_SFT                              0
#define RG_HCLDO_EN_VA18_MASK                             0x1
#define RG_HCLDO_EN_VA18_MASK_SFT                         (0x1 << 0)
#define RG_HCLDO_PDDIS_EN_VA18_SFT                        1
#define RG_HCLDO_PDDIS_EN_VA18_MASK                       0x1
#define RG_HCLDO_PDDIS_EN_VA18_MASK_SFT                   (0x1 << 1)
#define RG_HCLDO_REMOTE_SENSE_VA18_SFT                    2
#define RG_HCLDO_REMOTE_SENSE_VA18_MASK                   0x1
#define RG_HCLDO_REMOTE_SENSE_VA18_MASK_SFT               (0x1 << 2)
#define RG_LCLDO_EN_VA18_SFT                              4
#define RG_LCLDO_EN_VA18_MASK                             0x1
#define RG_LCLDO_EN_VA18_MASK_SFT                         (0x1 << 4)
#define RG_LCLDO_PDDIS_EN_VA18_SFT                        5
#define RG_LCLDO_PDDIS_EN_VA18_MASK                       0x1
#define RG_LCLDO_PDDIS_EN_VA18_MASK_SFT                   (0x1 << 5)
#define RG_LCLDO_REMOTE_SENSE_VA18_SFT                    6
#define RG_LCLDO_REMOTE_SENSE_VA18_MASK                   0x1
#define RG_LCLDO_REMOTE_SENSE_VA18_MASK_SFT               (0x1 << 6)
#define RG_LCLDO_ENC_EN_VA28_SFT                          8
#define RG_LCLDO_ENC_EN_VA28_MASK                         0x1
#define RG_LCLDO_ENC_EN_VA28_MASK_SFT                     (0x1 << 8)
#define RG_LCLDO_ENC_PDDIS_EN_VA28_SFT                    9
#define RG_LCLDO_ENC_PDDIS_EN_VA28_MASK                   0x1
#define RG_LCLDO_ENC_PDDIS_EN_VA28_MASK_SFT               (0x1 << 9)
#define RG_LCLDO_ENC_REMOTE_SENSE_VA28_SFT                10
#define RG_LCLDO_ENC_REMOTE_SENSE_VA28_MASK               0x1
#define RG_LCLDO_ENC_REMOTE_SENSE_VA28_MASK_SFT           (0x1 << 10)
#define RG_VA33REFGEN_EN_VA18_SFT                         12
#define RG_VA33REFGEN_EN_VA18_MASK                        0x1
#define RG_VA33REFGEN_EN_VA18_MASK_SFT                    (0x1 << 12)
#define RG_VA28REFGEN_EN_VA28_SFT                         13
#define RG_VA28REFGEN_EN_VA28_MASK                        0x1
#define RG_VA28REFGEN_EN_VA28_MASK_SFT                    (0x1 << 13)
#define RG_HCLDO_VOSEL_VA18_SFT                           14
#define RG_HCLDO_VOSEL_VA18_MASK                          0x1
#define RG_HCLDO_VOSEL_VA18_MASK_SFT                      (0x1 << 14)
#define RG_LCLDO_VOSEL_VA18_SFT                           15
#define RG_LCLDO_VOSEL_VA18_MASK                          0x1
#define RG_LCLDO_VOSEL_VA18_MASK_SFT                      (0x1 << 15)

/* MT6358_AUDDEC_ANA_CON15 */
#define RG_NVREG_EN_VAUDP15_SFT                           0
#define RG_NVREG_EN_VAUDP15_MASK                          0x1
#define RG_NVREG_EN_VAUDP15_MASK_SFT                      (0x1 << 0)
#define RG_NVREG_PULL0V_VAUDP15_SFT                       1
#define RG_NVREG_PULL0V_VAUDP15_MASK                      0x1
#define RG_NVREG_PULL0V_VAUDP15_MASK_SFT                  (0x1 << 1)
#define RG_AUDPMU_RSD0_VAUDP15_SFT                        4
#define RG_AUDPMU_RSD0_VAUDP15_MASK                       0xf
#define RG_AUDPMU_RSD0_VAUDP15_MASK_SFT                   (0xf << 4)
#define RG_AUDPMU_RSD0_VA18_SFT                           8
#define RG_AUDPMU_RSD0_VA18_MASK                          0xf
#define RG_AUDPMU_RSD0_VA18_MASK_SFT                      (0xf << 8)
#define RG_AUDPMU_RSD0_VA28_SFT                           12
#define RG_AUDPMU_RSD0_VA28_MASK                          0xf
#define RG_AUDPMU_RSD0_VA28_MASK_SFT                      (0xf << 12)

/* MT6358_ZCD_CON0 */
#define RG_AUDZCDENABLE_SFT                               0
#define RG_AUDZCDENABLE_MASK                              0x1
#define RG_AUDZCDENABLE_MASK_SFT                          (0x1 << 0)
#define RG_AUDZCDGAINSTEPTIME_SFT                         1
#define RG_AUDZCDGAINSTEPTIME_MASK                        0x7
#define RG_AUDZCDGAINSTEPTIME_MASK_SFT                    (0x7 << 1)
#define RG_AUDZCDGAINSTEPSIZE_SFT                         4
#define RG_AUDZCDGAINSTEPSIZE_MASK                        0x3
#define RG_AUDZCDGAINSTEPSIZE_MASK_SFT                    (0x3 << 4)
#define RG_AUDZCDTIMEOUTMODESEL_SFT                       6
#define RG_AUDZCDTIMEOUTMODESEL_MASK                      0x1
#define RG_AUDZCDTIMEOUTMODESEL_MASK_SFT                  (0x1 << 6)

/* MT6358_ZCD_CON1 */
#define RG_AUDLOLGAIN_SFT                                 0
#define RG_AUDLOLGAIN_MASK                                0x1f
#define RG_AUDLOLGAIN_MASK_SFT                            (0x1f << 0)
#define RG_AUDLORGAIN_SFT                                 7
#define RG_AUDLORGAIN_MASK                                0x1f
#define RG_AUDLORGAIN_MASK_SFT                            (0x1f << 7)

/* MT6358_ZCD_CON2 */
#define RG_AUDHPLGAIN_SFT                                 0
#define RG_AUDHPLGAIN_MASK                                0x1f
#define RG_AUDHPLGAIN_MASK_SFT                            (0x1f << 0)
#define RG_AUDHPRGAIN_SFT                                 7
#define RG_AUDHPRGAIN_MASK                                0x1f
#define RG_AUDHPRGAIN_MASK_SFT                            (0x1f << 7)

/* MT6358_ZCD_CON3 */
#define RG_AUDHSGAIN_SFT                                  0
#define RG_AUDHSGAIN_MASK                                 0x1f
#define RG_AUDHSGAIN_MASK_SFT                             (0x1f << 0)

/* MT6358_ZCD_CON4 */
#define RG_AUDIVLGAIN_SFT                                 0
#define RG_AUDIVLGAIN_MASK                                0x7
#define RG_AUDIVLGAIN_MASK_SFT                            (0x7 << 0)
#define RG_AUDIVRGAIN_SFT                                 8
#define RG_AUDIVRGAIN_MASK                                0x7
#define RG_AUDIVRGAIN_MASK_SFT                            (0x7 << 8)

/* MT6358_ZCD_CON5 */
#define RG_AUDINTGAIN1_SFT                                0
#define RG_AUDINTGAIN1_MASK                               0x3f
#define RG_AUDINTGAIN1_MASK_SFT                           (0x3f << 0)
#define RG_AUDINTGAIN2_SFT                                8
#define RG_AUDINTGAIN2_MASK                               0x3f
#define RG_AUDINTGAIN2_MASK_SFT                           (0x3f << 8)

/* audio register */
#define MT6358_DRV_CON3            0x3c
#define MT6358_GPIO_DIR0           0x88

#define MT6358_GPIO_MODE2          0xd8	/* mosi */
#define MT6358_GPIO_MODE2_SET      0xda
#define MT6358_GPIO_MODE2_CLR      0xdc

#define MT6358_GPIO_MODE3          0xde	/* miso */
#define MT6358_GPIO_MODE3_SET      0xe0
#define MT6358_GPIO_MODE3_CLR      0xe2

#define MT6358_TOP_CKPDN_CON0      0x10c
#define MT6358_TOP_CKPDN_CON0_SET  0x10e
#define MT6358_TOP_CKPDN_CON0_CLR  0x110

#define MT6358_TOP_CKHWEN_CON0     0x12a
#define MT6358_TOP_CKHWEN_CON0_SET 0x12c
#define MT6358_TOP_CKHWEN_CON0_CLR 0x12e

#define MT6358_OTP_CON0            0x38a
#define MT6358_OTP_CON8            0x39a
#define MT6358_OTP_CON11           0x3a0
#define MT6358_OTP_CON12           0x3a2
#define MT6358_OTP_CON13           0x3a4

#define MT6358_DCXO_CW13           0x7aa
#define MT6358_DCXO_CW14           0x7ac

#define MT6358_AUXADC_CON10        0x11a0

/* audio register */
#define MT6358_AUD_TOP_ID                    0x2200
#define MT6358_AUD_TOP_REV0                  0x2202
#define MT6358_AUD_TOP_DBI                   0x2204
#define MT6358_AUD_TOP_DXI                   0x2206
#define MT6358_AUD_TOP_CKPDN_TPM0            0x2208
#define MT6358_AUD_TOP_CKPDN_TPM1            0x220a
#define MT6358_AUD_TOP_CKPDN_CON0            0x220c
#define MT6358_AUD_TOP_CKPDN_CON0_SET        0x220e
#define MT6358_AUD_TOP_CKPDN_CON0_CLR        0x2210
#define MT6358_AUD_TOP_CKSEL_CON0            0x2212
#define MT6358_AUD_TOP_CKSEL_CON0_SET        0x2214
#define MT6358_AUD_TOP_CKSEL_CON0_CLR        0x2216
#define MT6358_AUD_TOP_CKTST_CON0            0x2218
#define MT6358_AUD_TOP_CLK_HWEN_CON0         0x221a
#define MT6358_AUD_TOP_CLK_HWEN_CON0_SET     0x221c
#define MT6358_AUD_TOP_CLK_HWEN_CON0_CLR     0x221e
#define MT6358_AUD_TOP_RST_CON0              0x2220
#define MT6358_AUD_TOP_RST_CON0_SET          0x2222
#define MT6358_AUD_TOP_RST_CON0_CLR          0x2224
#define MT6358_AUD_TOP_RST_BANK_CON0         0x2226
#define MT6358_AUD_TOP_INT_CON0              0x2228
#define MT6358_AUD_TOP_INT_CON0_SET          0x222a
#define MT6358_AUD_TOP_INT_CON0_CLR          0x222c
#define MT6358_AUD_TOP_INT_MASK_CON0         0x222e
#define MT6358_AUD_TOP_INT_MASK_CON0_SET     0x2230
#define MT6358_AUD_TOP_INT_MASK_CON0_CLR     0x2232
#define MT6358_AUD_TOP_INT_STATUS0           0x2234
#define MT6358_AUD_TOP_INT_RAW_STATUS0       0x2236
#define MT6358_AUD_TOP_INT_MISC_CON0         0x2238
#define MT6358_AUDNCP_CLKDIV_CON0            0x223a
#define MT6358_AUDNCP_CLKDIV_CON1            0x223c
#define MT6358_AUDNCP_CLKDIV_CON2            0x223e
#define MT6358_AUDNCP_CLKDIV_CON3            0x2240
#define MT6358_AUDNCP_CLKDIV_CON4            0x2242
#define MT6358_AUD_TOP_MON_CON0              0x2244
#define MT6358_AUDIO_DIG_DSN_ID              0x2280
#define MT6358_AUDIO_DIG_DSN_REV0            0x2282
#define MT6358_AUDIO_DIG_DSN_DBI             0x2284
#define MT6358_AUDIO_DIG_DSN_DXI             0x2286
#define MT6358_AFE_UL_DL_CON0                0x2288
#define MT6358_AFE_DL_SRC2_CON0_L            0x228a
#define MT6358_AFE_UL_SRC_CON0_H             0x228c
#define MT6358_AFE_UL_SRC_CON0_L             0x228e
#define MT6358_AFE_TOP_CON0                  0x2290
#define MT6358_AUDIO_TOP_CON0                0x2292
#define MT6358_AFE_MON_DEBUG0                0x2294
#define MT6358_AFUNC_AUD_CON0                0x2296
#define MT6358_AFUNC_AUD_CON1                0x2298
#define MT6358_AFUNC_AUD_CON2                0x229a
#define MT6358_AFUNC_AUD_CON3                0x229c
#define MT6358_AFUNC_AUD_CON4                0x229e
#define MT6358_AFUNC_AUD_CON5                0x22a0
#define MT6358_AFUNC_AUD_CON6                0x22a2
#define MT6358_AFUNC_AUD_MON0                0x22a4
#define MT6358_AUDRC_TUNE_MON0               0x22a6
#define MT6358_AFE_ADDA_MTKAIF_FIFO_CFG0     0x22a8
#define MT6358_AFE_ADDA_MTKAIF_FIFO_LOG_MON1 0x22aa
#define MT6358_AFE_ADDA_MTKAIF_MON0          0x22ac
#define MT6358_AFE_ADDA_MTKAIF_MON1          0x22ae
#define MT6358_AFE_ADDA_MTKAIF_MON2          0x22b0
#define MT6358_AFE_ADDA_MTKAIF_MON3          0x22b2
#define MT6358_AFE_ADDA_MTKAIF_CFG0          0x22b4
#define MT6358_AFE_ADDA_MTKAIF_RX_CFG0       0x22b6
#define MT6358_AFE_ADDA_MTKAIF_RX_CFG1       0x22b8
#define MT6358_AFE_ADDA_MTKAIF_RX_CFG2       0x22ba
#define MT6358_AFE_ADDA_MTKAIF_RX_CFG3       0x22bc
#define MT6358_AFE_ADDA_MTKAIF_TX_CFG1       0x22be
#define MT6358_AFE_SGEN_CFG0                 0x22c0
#define MT6358_AFE_SGEN_CFG1                 0x22c2
#define MT6358_AFE_ADC_ASYNC_FIFO_CFG        0x22c4
#define MT6358_AFE_DCCLK_CFG0                0x22c6
#define MT6358_AFE_DCCLK_CFG1                0x22c8
#define MT6358_AUDIO_DIG_CFG                 0x22ca
#define MT6358_AFE_AUD_PAD_TOP               0x22cc
#define MT6358_AFE_AUD_PAD_TOP_MON           0x22ce
#define MT6358_AFE_AUD_PAD_TOP_MON1          0x22d0
#define MT6358_AFE_DL_NLE_CFG                0x22d2
#define MT6358_AFE_DL_NLE_MON                0x22d4
#define MT6358_AFE_CG_EN_MON                 0x22d6
#define MT6358_AUDIO_DIG_2ND_DSN_ID          0x2300
#define MT6358_AUDIO_DIG_2ND_DSN_REV0        0x2302
#define MT6358_AUDIO_DIG_2ND_DSN_DBI         0x2304
#define MT6358_AUDIO_DIG_2ND_DSN_DXI         0x2306
#define MT6358_AFE_PMIC_NEWIF_CFG3           0x2308
#define MT6358_AFE_VOW_TOP                   0x230a
#define MT6358_AFE_VOW_CFG0                  0x230c
#define MT6358_AFE_VOW_CFG1                  0x230e
#define MT6358_AFE_VOW_CFG2                  0x2310
#define MT6358_AFE_VOW_CFG3                  0x2312
#define MT6358_AFE_VOW_CFG4                  0x2314
#define MT6358_AFE_VOW_CFG5                  0x2316
#define MT6358_AFE_VOW_CFG6                  0x2318
#define MT6358_AFE_VOW_MON0                  0x231a
#define MT6358_AFE_VOW_MON1                  0x231c
#define MT6358_AFE_VOW_MON2                  0x231e
#define MT6358_AFE_VOW_MON3                  0x2320
#define MT6358_AFE_VOW_MON4                  0x2322
#define MT6358_AFE_VOW_MON5                  0x2324
#define MT6358_AFE_VOW_SN_INI_CFG            0x2326
#define MT6358_AFE_VOW_TGEN_CFG0             0x2328
#define MT6358_AFE_VOW_POSDIV_CFG0           0x232a
#define MT6358_AFE_VOW_HPF_CFG0              0x232c
#define MT6358_AFE_VOW_PERIODIC_CFG0         0x232e
#define MT6358_AFE_VOW_PERIODIC_CFG1         0x2330
#define MT6358_AFE_VOW_PERIODIC_CFG2         0x2332
#define MT6358_AFE_VOW_PERIODIC_CFG3         0x2334
#define MT6358_AFE_VOW_PERIODIC_CFG4         0x2336
#define MT6358_AFE_VOW_PERIODIC_CFG5         0x2338
#define MT6358_AFE_VOW_PERIODIC_CFG6         0x233a
#define MT6358_AFE_VOW_PERIODIC_CFG7         0x233c
#define MT6358_AFE_VOW_PERIODIC_CFG8         0x233e
#define MT6358_AFE_VOW_PERIODIC_CFG9         0x2340
#define MT6358_AFE_VOW_PERIODIC_CFG10        0x2342
#define MT6358_AFE_VOW_PERIODIC_CFG11        0x2344
#define MT6358_AFE_VOW_PERIODIC_CFG12        0x2346
#define MT6358_AFE_VOW_PERIODIC_CFG13        0x2348
#define MT6358_AFE_VOW_PERIODIC_CFG14        0x234a
#define MT6358_AFE_VOW_PERIODIC_CFG15        0x234c
#define MT6358_AFE_VOW_PERIODIC_CFG16        0x234e
#define MT6358_AFE_VOW_PERIODIC_CFG17        0x2350
#define MT6358_AFE_VOW_PERIODIC_CFG18        0x2352
#define MT6358_AFE_VOW_PERIODIC_CFG19        0x2354
#define MT6358_AFE_VOW_PERIODIC_CFG20        0x2356
#define MT6358_AFE_VOW_PERIODIC_CFG21        0x2358
#define MT6358_AFE_VOW_PERIODIC_CFG22        0x235a
#define MT6358_AFE_VOW_PERIODIC_CFG23        0x235c
#define MT6358_AFE_VOW_PERIODIC_MON0         0x235e
#define MT6358_AFE_VOW_PERIODIC_MON1         0x2360
#define MT6358_AUDENC_DSN_ID                 0x2380
#define MT6358_AUDENC_DSN_REV0               0x2382
#define MT6358_AUDENC_DSN_DBI                0x2384
#define MT6358_AUDENC_DSN_FPI                0x2386
#define MT6358_AUDENC_ANA_CON0               0x2388
#define MT6358_AUDENC_ANA_CON1               0x238a
#define MT6358_AUDENC_ANA_CON2               0x238c
#define MT6358_AUDENC_ANA_CON3               0x238e
#define MT6358_AUDENC_ANA_CON4               0x2390
#define MT6358_AUDENC_ANA_CON5               0x2392
#define MT6358_AUDENC_ANA_CON6               0x2394
#define MT6358_AUDENC_ANA_CON7               0x2396
#define MT6358_AUDENC_ANA_CON8               0x2398
#define MT6358_AUDENC_ANA_CON9               0x239a
#define MT6358_AUDENC_ANA_CON10              0x239c
#define MT6358_AUDENC_ANA_CON11              0x239e
#define MT6358_AUDENC_ANA_CON12              0x23a0
#define MT6358_AUDDEC_DSN_ID                 0x2400
#define MT6358_AUDDEC_DSN_REV0               0x2402
#define MT6358_AUDDEC_DSN_DBI                0x2404
#define MT6358_AUDDEC_DSN_FPI                0x2406
#define MT6358_AUDDEC_ANA_CON0               0x2408
#define MT6358_AUDDEC_ANA_CON1               0x240a
#define MT6358_AUDDEC_ANA_CON2               0x240c
#define MT6358_AUDDEC_ANA_CON3               0x240e
#define MT6358_AUDDEC_ANA_CON4               0x2410
#define MT6358_AUDDEC_ANA_CON5               0x2412
#define MT6358_AUDDEC_ANA_CON6               0x2414
#define MT6358_AUDDEC_ANA_CON7               0x2416
#define MT6358_AUDDEC_ANA_CON8               0x2418
#define MT6358_AUDDEC_ANA_CON9               0x241a
#define MT6358_AUDDEC_ANA_CON10              0x241c
#define MT6358_AUDDEC_ANA_CON11              0x241e
#define MT6358_AUDDEC_ANA_CON12              0x2420
#define MT6358_AUDDEC_ANA_CON13              0x2422
#define MT6358_AUDDEC_ANA_CON14              0x2424
#define MT6358_AUDDEC_ANA_CON15              0x2426
#define MT6358_AUDDEC_ELR_NUM                0x2428
#define MT6358_AUDDEC_ELR_0                  0x242a
#define MT6358_AUDZCD_DSN_ID                 0x2480
#define MT6358_AUDZCD_DSN_REV0               0x2482
#define MT6358_AUDZCD_DSN_DBI                0x2484
#define MT6358_AUDZCD_DSN_FPI                0x2486
#define MT6358_ZCD_CON0                      0x2488
#define MT6358_ZCD_CON1                      0x248a
#define MT6358_ZCD_CON2                      0x248c
#define MT6358_ZCD_CON3                      0x248e
#define MT6358_ZCD_CON4                      0x2490
#define MT6358_ZCD_CON5                      0x2492
#define MT6358_ACCDET_CON13                  0x2522

#define MT6358_MAX_REGISTER MT6358_ZCD_CON5

enum {
	MT6358_MTKAIF_PROTOCOL_1 = 0,
	MT6358_MTKAIF_PROTOCOL_2,
	MT6358_MTKAIF_PROTOCOL_2_CLK_P2,
};

/* set only during init */
int mt6358_set_mtkaif_protocol(struct snd_soc_component *cmpnt,
			       int mtkaif_protocol);
#endif /* __MT6358_H__ */
