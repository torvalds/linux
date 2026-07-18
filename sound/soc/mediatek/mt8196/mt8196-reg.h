/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt8196-reg.h  --  Mediatek 8196 audio driver reg definition
 *
 *  Copyright (c) 2025 MediaTek Inc.
 *  Author: Darren Ye <darren.ye@mediatek.com>
 */

#ifndef _MT8196_REG_H_
#define _MT8196_REG_H_

 /* reg bit enum */
enum {
	MT8196_MEMIF_PBUF_SIZE_32_BYTES,
	MT8196_MEMIF_PBUF_SIZE_64_BYTES,
	MT8196_MEMIF_PBUF_SIZE_128_BYTES,
	MT8196_MEMIF_PBUF_SIZE_256_BYTES,
	MT8196_MEMIF_PBUF_SIZE_NUM,
};

enum {
	MT8196_MEMIF_MAX_LEN_0_BYTES,
	MT8196_MEMIF_MAX_LEN_16_BYTES,
	MT8196_MEMIF_MAX_LEN_32_BYTES,
	MT8196_MEMIF_MAX_LEN_64_BYTES,
};

enum {
	MT8196_MEMIF_MIN_LEN_NOT_SUPPORT,
	MT8196_MEMIF_MIN_LEN_16_BYTES,
	MT8196_MEMIF_MIN_LEN_32_BYTES,
	MT8196_MEMIF_MIN_LEN_64_BYTES,
};

/*****************************************************************************
 * R E G I S T E R  D E F I N I T I O N
 *****************************************************************************/
/* AUDIO_TOP_CON0 */
#define PDN_MTKAIFV4_SFT                                      25
#define PDN_MTKAIFV4_MASK                                     0x1
#define PDN_MTKAIFV4_MASK_SFT                                 (0x1 << 25)
#define PDN_FM_I2S_SFT                                        24
#define PDN_FM_I2S_MASK                                       0x1
#define PDN_FM_I2S_MASK_SFT                                   (0x1 << 24)
#define PDN_HW_GAIN01_SFT                                     21
#define PDN_HW_GAIN01_MASK                                    0x1
#define PDN_HW_GAIN01_MASK_SFT                                (0x1 << 21)
#define PDN_HW_GAIN23_SFT                                     20
#define PDN_HW_GAIN23_MASK                                    0x1
#define PDN_HW_GAIN23_MASK_SFT                                (0x1 << 20)
#define PDN_STF_SFT                                           19
#define PDN_STF_MASK                                          0x1
#define PDN_STF_MASK_SFT                                      (0x1 << 19)
#define PDN_CM0_SFT                                           18
#define PDN_CM0_MASK                                          0x1
#define PDN_CM0_MASK_SFT                                      (0x1 << 18)
#define PDN_CM1_SFT                                           17
#define PDN_CM1_MASK                                          0x1
#define PDN_CM1_MASK_SFT                                      (0x1 << 17)
#define PDN_CM2_SFT                                           16
#define PDN_CM2_MASK                                          0x1
#define PDN_CM2_MASK_SFT                                      (0x1 << 16)
#define PDN_PCM0_SFT                                          14
#define PDN_PCM0_MASK                                         0x1
#define PDN_PCM0_MASK_SFT                                     (0x1 << 14)
#define PDN_PCM1_SFT                                          13
#define PDN_PCM1_MASK                                         0x1
#define PDN_PCM1_MASK_SFT                                     (0x1 << 13)

/* AUDIO_TOP_CON1 */
#define PDN_UL0_ADC_SFT                                       23
#define PDN_UL0_ADC_MASK                                      0x1
#define PDN_UL0_ADC_MASK_SFT                                  (0x1 << 23)
#define PDN_UL0_TML_SFT                                       22
#define PDN_UL0_TML_MASK                                      0x1
#define PDN_UL0_TML_MASK_SFT                                  (0x1 << 22)
#define PDN_UL0_ADC_HIRES_SFT                                 21
#define PDN_UL0_ADC_HIRES_MASK                                0x1
#define PDN_UL0_ADC_HIRES_MASK_SFT                            (0x1 << 21)
#define PDN_UL0_ADC_HIRES_TML_SFT                             20
#define PDN_UL0_ADC_HIRES_TML_MASK                            0x1
#define PDN_UL0_ADC_HIRES_TML_MASK_SFT                        (0x1 << 20)
#define PDN_UL1_ADC_SFT                                       19
#define PDN_UL1_ADC_MASK                                      0x1
#define PDN_UL1_ADC_MASK_SFT                                  (0x1 << 19)
#define PDN_UL1_TML_SFT                                       18
#define PDN_UL1_TML_MASK                                      0x1
#define PDN_UL1_TML_MASK_SFT                                  (0x1 << 18)
#define PDN_UL1_ADC_HIRES_SFT                                 17
#define PDN_UL1_ADC_HIRES_MASK                                0x1
#define PDN_UL1_ADC_HIRES_MASK_SFT                            (0x1 << 17)
#define PDN_UL1_ADC_HIRES_TML_SFT                             16
#define PDN_UL1_ADC_HIRES_TML_MASK                            0x1
#define PDN_UL1_ADC_HIRES_TML_MASK_SFT                        (0x1 << 16)
#define PDN_UL2_ADC_SFT                                       15
#define PDN_UL2_ADC_MASK                                      0x1
#define PDN_UL2_ADC_MASK_SFT                                  (0x1 << 15)
#define PDN_UL2_TML_SFT                                       14
#define PDN_UL2_TML_MASK                                      0x1
#define PDN_UL2_TML_MASK_SFT                                  (0x1 << 14)
#define PDN_UL2_ADC_HIRES_SFT                                 13
#define PDN_UL2_ADC_HIRES_MASK                                0x1
#define PDN_UL2_ADC_HIRES_MASK_SFT                            (0x1 << 13)
#define PDN_UL2_ADC_HIRES_TML_SFT                             12
#define PDN_UL2_ADC_HIRES_TML_MASK                            0x1
#define PDN_UL2_ADC_HIRES_TML_MASK_SFT                        (0x1 << 12)

/* AUDIO_TOP_CON2 */
#define PDN_TDM_OUT_SFT                                       24
#define PDN_TDM_OUT_MASK                                      0x1
#define PDN_TDM_OUT_MASK_SFT                                  (0x1 << 24)
#define PDN_ETDM_OUT0_SFT                                     21
#define PDN_ETDM_OUT0_MASK                                    0x1
#define PDN_ETDM_OUT0_MASK_SFT                                (0x1 << 21)
#define PDN_ETDM_OUT1_SFT                                     20
#define PDN_ETDM_OUT1_MASK                                    0x1
#define PDN_ETDM_OUT1_MASK_SFT                                (0x1 << 20)
#define PDN_ETDM_OUT2_SFT                                     19
#define PDN_ETDM_OUT2_MASK                                    0x1
#define PDN_ETDM_OUT2_MASK_SFT                                (0x1 << 19)
#define PDN_ETDM_OUT3_SFT                                     18
#define PDN_ETDM_OUT3_MASK                                    0x1
#define PDN_ETDM_OUT3_MASK_SFT                                (0x1 << 18)
#define PDN_ETDM_OUT4_SFT                                     17
#define PDN_ETDM_OUT4_MASK                                    0x1
#define PDN_ETDM_OUT4_MASK_SFT                                (0x1 << 17)
#define PDN_ETDM_OUT5_SFT                                     16
#define PDN_ETDM_OUT5_MASK                                    0x1
#define PDN_ETDM_OUT5_MASK_SFT                                (0x1 << 16)
#define PDN_ETDM_OUT6_SFT                                     15
#define PDN_ETDM_OUT6_MASK                                    0x1
#define PDN_ETDM_OUT6_MASK_SFT                                (0x1 << 15)
#define PDN_ETDM_IN0_SFT                                      13
#define PDN_ETDM_IN0_MASK                                     0x1
#define PDN_ETDM_IN0_MASK_SFT                                 (0x1 << 13)
#define PDN_ETDM_IN1_SFT                                      12
#define PDN_ETDM_IN1_MASK                                     0x1
#define PDN_ETDM_IN1_MASK_SFT                                 (0x1 << 12)
#define PDN_ETDM_IN2_SFT                                      11
#define PDN_ETDM_IN2_MASK                                     0x1
#define PDN_ETDM_IN2_MASK_SFT                                 (0x1 << 11)
#define PDN_ETDM_IN3_SFT                                      10
#define PDN_ETDM_IN3_MASK                                     0x1
#define PDN_ETDM_IN3_MASK_SFT                                 (0x1 << 10)
#define PDN_ETDM_IN4_SFT                                      9
#define PDN_ETDM_IN4_MASK                                     0x1
#define PDN_ETDM_IN4_MASK_SFT                                 (0x1 << 9)
#define PDN_ETDM_IN5_SFT                                      8
#define PDN_ETDM_IN5_MASK                                     0x1
#define PDN_ETDM_IN5_MASK_SFT                                 (0x1 << 8)
#define PDN_ETDM_IN6_SFT                                      7
#define PDN_ETDM_IN6_MASK                                     0x1
#define PDN_ETDM_IN6_MASK_SFT                                 (0x1 << 7)

/* AUDIO_TOP_CON3 */
#define PDN_CONNSYS_I2S_ASRC_SFT                              25
#define PDN_CONNSYS_I2S_ASRC_MASK                             0x1
#define PDN_CONNSYS_I2S_ASRC_MASK_SFT                         (0x1 << 25)
#define PDN_GENERAL0_ASRC_SFT                                 24
#define PDN_GENERAL0_ASRC_MASK                                0x1
#define PDN_GENERAL0_ASRC_MASK_SFT                            (0x1 << 24)
#define PDN_GENERAL1_ASRC_SFT                                 23
#define PDN_GENERAL1_ASRC_MASK                                0x1
#define PDN_GENERAL1_ASRC_MASK_SFT                            (0x1 << 23)
#define PDN_GENERAL2_ASRC_SFT                                 22
#define PDN_GENERAL2_ASRC_MASK                                0x1
#define PDN_GENERAL2_ASRC_MASK_SFT                            (0x1 << 22)
#define PDN_GENERAL3_ASRC_SFT                                 21
#define PDN_GENERAL3_ASRC_MASK                                0x1
#define PDN_GENERAL3_ASRC_MASK_SFT                            (0x1 << 21)
#define PDN_GENERAL4_ASRC_SFT                                 20
#define PDN_GENERAL4_ASRC_MASK                                0x1
#define PDN_GENERAL4_ASRC_MASK_SFT                            (0x1 << 20)
#define PDN_GENERAL5_ASRC_SFT                                 19
#define PDN_GENERAL5_ASRC_MASK                                0x1
#define PDN_GENERAL5_ASRC_MASK_SFT                            (0x1 << 19)
#define PDN_GENERAL6_ASRC_SFT                                 18
#define PDN_GENERAL6_ASRC_MASK                                0x1
#define PDN_GENERAL6_ASRC_MASK_SFT                            (0x1 << 18)
#define PDN_GENERAL7_ASRC_SFT                                 17
#define PDN_GENERAL7_ASRC_MASK                                0x1
#define PDN_GENERAL7_ASRC_MASK_SFT                            (0x1 << 17)
#define PDN_GENERAL8_ASRC_SFT                                 16
#define PDN_GENERAL8_ASRC_MASK                                0x1
#define PDN_GENERAL8_ASRC_MASK_SFT                            (0x1 << 16)
#define PDN_GENERAL9_ASRC_SFT                                 15
#define PDN_GENERAL9_ASRC_MASK                                0x1
#define PDN_GENERAL9_ASRC_MASK_SFT                            (0x1 << 15)
#define PDN_GENERAL10_ASRC_SFT                                14
#define PDN_GENERAL10_ASRC_MASK                               0x1
#define PDN_GENERAL10_ASRC_MASK_SFT                           (0x1 << 14)
#define PDN_GENERAL11_ASRC_SFT                                13
#define PDN_GENERAL11_ASRC_MASK                               0x1
#define PDN_GENERAL11_ASRC_MASK_SFT                           (0x1 << 13)
#define PDN_GENERAL12_ASRC_SFT                                12
#define PDN_GENERAL12_ASRC_MASK                               0x1
#define PDN_GENERAL12_ASRC_MASK_SFT                           (0x1 << 12)
#define PDN_GENERAL13_ASRC_SFT                                11
#define PDN_GENERAL13_ASRC_MASK                               0x1
#define PDN_GENERAL13_ASRC_MASK_SFT                           (0x1 << 11)
#define PDN_GENERAL14_ASRC_SFT                                10
#define PDN_GENERAL14_ASRC_MASK                               0x1
#define PDN_GENERAL14_ASRC_MASK_SFT                           (0x1 << 10)
#define PDN_GENERAL15_ASRC_SFT                                9
#define PDN_GENERAL15_ASRC_MASK                               0x1
#define PDN_GENERAL15_ASRC_MASK_SFT                           (0x1 << 9)

/* AUDIO_TOP_CON4 */
#define PDN_APLL_TUNER1_SFT                                   13
#define PDN_APLL_TUNER1_MASK                                  0x1
#define PDN_APLL_TUNER1_MASK_SFT                              (0x1 << 13)
#define PDN_APLL_TUNER2_SFT                                   12
#define PDN_APLL_TUNER2_MASK                                  0x1
#define PDN_APLL_TUNER2_MASK_SFT                              (0x1 << 12)
#define CG_H208M_CK_SFT                                       4
#define CG_H208M_CK_MASK                                      0x1
#define CG_H208M_CK_MASK_SFT                                  (0x1 << 4)
#define CG_APLL2_CK_SFT                                       3
#define CG_APLL2_CK_MASK                                      0x1
#define CG_APLL2_CK_MASK_SFT                                  (0x1 << 3)
#define CG_APLL1_CK_SFT                                       2
#define CG_APLL1_CK_MASK                                      0x1
#define CG_APLL1_CK_MASK_SFT                                  (0x1 << 2)
#define CG_AUDIO_F26M_CK_SFT                                  1
#define CG_AUDIO_F26M_CK_MASK                                 0x1
#define CG_AUDIO_F26M_CK_MASK_SFT                             (0x1 << 1)
#define CG_AUDIO_HOPPING_CK_SFT                               0
#define CG_AUDIO_HOPPING_CK_MASK                              0x1
#define CG_AUDIO_HOPPING_CK_MASK_SFT                          (0x1 << 0)

/* AUDIO_ENGEN_CON0 */
/* AUDIO_ENGEN_CON0_USER1 */
/* AUDIO_ENGEN_CON0_USER1 */
#define MULTI_USER_BYPASS_SFT                                 17
#define MULTI_USER_BYPASS_MASK                                0x1
#define MULTI_USER_BYPASS_MASK_SFT                            (0x1 << 17)
#define MULTI_USER_RST_SFT                                    16
#define MULTI_USER_RST_MASK                                   0x1
#define MULTI_USER_RST_MASK_SFT                               (0x1 << 16)
#define AUDIO_F26M_EN_RST_SFT                                 8
#define AUDIO_F26M_EN_RST_MASK                                0x1
#define AUDIO_F26M_EN_RST_MASK_SFT                            (0x1 << 8)
#define AUDIO_APLL2_EN_ON_SFT                                 3
#define AUDIO_APLL2_EN_ON_MASK                                0x1
#define AUDIO_APLL2_EN_ON_MASK_SFT                            (0x1 << 3)
#define AUDIO_APLL1_EN_ON_SFT                                 2
#define AUDIO_APLL1_EN_ON_MASK                                0x1
#define AUDIO_APLL1_EN_ON_MASK_SFT                            (0x1 << 2)
#define AUDIO_F3P25M_EN_ON_SFT                                1
#define AUDIO_F3P25M_EN_ON_MASK                               0x1
#define AUDIO_F3P25M_EN_ON_MASK_SFT                           (0x1 << 1)
#define AUDIO_26M_EN_ON_SFT                                   0
#define AUDIO_26M_EN_ON_MASK                                  0x1
#define AUDIO_26M_EN_ON_MASK_SFT                              (0x1 << 0)

/* AFE_SINEGEN_CON0 */
#define DAC_EN_SFT                                            26
#define DAC_EN_MASK                                           0x1
#define DAC_EN_MASK_SFT                                       (0x1 << 26)
#define TIE_SW_CH2_SFT                                        25
#define TIE_SW_CH2_MASK                                       0x1
#define TIE_SW_CH2_MASK_SFT                                   (0x1 << 25)
#define TIE_SW_CH1_SFT                                        24
#define TIE_SW_CH1_MASK                                       0x1
#define TIE_SW_CH1_MASK_SFT                                   (0x1 << 24)
#define AMP_DIV_CH2_SFT                                       20
#define AMP_DIV_CH2_MASK                                      0xf
#define AMP_DIV_CH2_MASK_SFT                                  (0xf << 20)
#define FREQ_DIV_CH2_SFT                                      12
#define FREQ_DIV_CH2_MASK                                     0x1f
#define FREQ_DIV_CH2_MASK_SFT                                 (0x1f << 12)
#define AMP_DIV_CH1_SFT                                       8
#define AMP_DIV_CH1_MASK                                      0xf
#define AMP_DIV_CH1_MASK_SFT                                  (0xf << 8)
#define FREQ_DIV_CH1_SFT                                      0
#define FREQ_DIV_CH1_MASK                                     0x1f
#define FREQ_DIV_CH1_MASK_SFT                                 (0x1f << 0)

/* AFE_SINEGEN_CON1 */
#define SINE_DOMAIN_SFT                                       20
#define SINE_DOMAIN_MASK                                      0x7
#define SINE_DOMAIN_MASK_SFT                                  (0x7 << 20)
#define SINE_MODE_SFT                                         12
#define SINE_MODE_MASK                                        0x1f
#define SINE_MODE_MASK_SFT                                    (0x1f << 12)
#define INNER_LOOP_BACKI_SEL_SFT                              8
#define INNER_LOOP_BACKI_SEL_MASK                             0x1
#define INNER_LOOP_BACKI_SEL_MASK_SFT                         (0x1 << 8)
#define INNER_LOOP_BACK_MODE_SFT                              0
#define INNER_LOOP_BACK_MODE_MASK                             0xff
#define INNER_LOOP_BACK_MODE_MASK_SFT                         (0xff << 0)

/* AFE_SINEGEN_CON2 */
#define TIE_CH1_CONSTANT_SFT                                  0
#define TIE_CH1_CONSTANT_MASK                                 0xffffffff
#define TIE_CH1_CONSTANT_MASK_SFT                             (0xffffffff << 0)

/* AFE_SINEGEN_CON3 */
#define TIE_CH2_CONSTANT_SFT                                  0
#define TIE_CH2_CONSTANT_MASK                                 0xffffffff
#define TIE_CH2_CONSTANT_MASK_SFT                             (0xffffffff << 0)

/* AFE_APLL1_TUNER_CFG */
/* AFE_APLL2_TUNER_CFG */
#define UPPER_BOUND_SFT                                       8
#define UPPER_BOUND_MASK                                      0xff
#define UPPER_BOUND_MASK_SFT                                  (0xff << 8)
#define APLL_DIV_SFT                                          4
#define APLL_DIV_MASK                                         0xf
#define APLL_DIV_MASK_SFT                                     (0xf << 4)
#define XTAL_EN_128FS_SEL_SFT                                 1
#define XTAL_EN_128FS_SEL_MASK                                0x3
#define XTAL_EN_128FS_SEL_MASK_SFT                            (0x3 << 1)
#define FREQ_TUNER_EN_SFT                                     0
#define FREQ_TUNER_EN_MASK                                    0x1
#define FREQ_TUNER_EN_MASK_SFT                                (0x1 << 0)

/* AFE_APLL1_TUNER_MON0 */
/* AFE_APLL2_TUNER_MON0 */
#define TUNER_MON_SFT                                         0
#define TUNER_MON_MASK                                        0xffffffff
#define TUNER_MON_MASK_SFT                                    (0xffffffff << 0)

/* AUDIO_TOP_RG0 */
/* AUDIO_TOP_RG1 */
/* AUDIO_TOP_RG2 */
/* AUDIO_TOP_RG3 */
/* AUDIO_TOP_RG4 */
#define RESERVE_RG_SFT                                        0
#define RESERVE_RG_MASK                                       0xffffffff
#define RESERVE_RG_MASK_SFT                                   (0xffffffff << 0)

/* AFE_SPM_CONTROL_REQ */
#define AFE_DDREN_REQ_SFT                                     4
#define AFE_DDREN_REQ_MASK                                    0x1
#define AFE_DDREN_REQ_MASK_SFT                                (0x1 << 4)
#define AFE_INFRA_REQ_SFT                                     3
#define AFE_INFRA_REQ_MASK                                    0x1
#define AFE_INFRA_REQ_MASK_SFT                                (0x1 << 3)
#define AFE_VRF18_REQ_SFT                                     2
#define AFE_VRF18_REQ_MASK                                    0x1
#define AFE_VRF18_REQ_MASK_SFT                                (0x1 << 2)
#define AFE_APSRC_REQ_SFT                                     1
#define AFE_APSRC_REQ_MASK                                    0x1
#define AFE_APSRC_REQ_MASK_SFT                                (0x1 << 1)
#define AFE_SRCCLKENA_REQ_SFT                                 0
#define AFE_SRCCLKENA_REQ_MASK                                0x1
#define AFE_SRCCLKENA_REQ_MASK_SFT                            (0x1 << 0)

/* AFE_SPM_CONTROL_ACK */
#define SPM_RESOURCE_CONTROL_ACK_SFT                          0
#define SPM_RESOURCE_CONTROL_ACK_MASK                         0xffffffff
#define SPM_RESOURCE_CONTROL_ACK_MASK_SFT                     (0xffffffff << 0)

/* AUD_TOP_CFG_VCORE_RG */
#define AUD_TOP_CFG_SFT                                       0
#define AUD_TOP_CFG_MASK                                      0xffffffff
#define AUD_TOP_CFG_MASK_SFT                                  (0xffffffff << 0)

/* AUDIO_TOP_IP_VERSION */
#define AUDIO_TOP_IP_VERSION_SFT                              0
#define AUDIO_TOP_IP_VERSION_MASK                             0xffffffff
#define AUDIO_TOP_IP_VERSION_MASK_SFT                         (0xffffffff << 0)

/* AUDIO_ENGEN_CON0_MON */
#define AUDIO_ENGEN_MON_SFT                                   0
#define AUDIO_ENGEN_MON_MASK                                  0xffffffff
#define AUDIO_ENGEN_MON_MASK_SFT                              (0xffffffff << 0)

/* AUD_TOP_CFG_VLP_RG */
#define I2SIN1_DAT_SEL_SFT                                    31
#define I2SIN1_DAT_SEL_MASK                                   0x1
#define I2SIN1_DAT_SEL_MASK_SFT                               (0x1 << 31)
#define FMI2S_IN_SEL_SFT                                      30
#define FMI2S_IN_SEL_MASK                                     0x1
#define FMI2S_IN_SEL_MASK_SFT                                 (0x1 << 30)
#define RG_I2S4_IN_BCK_NEG_EG_LATCH_SFT                       21
#define RG_I2S4_IN_BCK_NEG_EG_LATCH_MASK                      0x1
#define RG_I2S4_IN_BCK_NEG_EG_LATCH_MASK_SFT                  (0x1 << 21)
#define RG_I2S4_OUT_BCK_NEG_EG_LATCH_SFT                      20
#define RG_I2S4_OUT_BCK_NEG_EG_LATCH_MASK                     0x1
#define RG_I2S4_OUT_BCK_NEG_EG_LATCH_MASK_SFT                 (0x1 << 20)
#define RG_I2S4_IN_SLV_LRCK_LATCH_EDGE_SFT                    19
#define RG_I2S4_IN_SLV_LRCK_LATCH_EDGE_MASK                   0x1
#define RG_I2S4_IN_SLV_LRCK_LATCH_EDGE_MASK_SFT               (0x1 << 19)
#define RG_I2S4_IN_SLV_BCK_INV_SEL_SFT                        18
#define RG_I2S4_IN_SLV_BCK_INV_SEL_MASK                       0x1
#define RG_I2S4_IN_SLV_BCK_INV_SEL_MASK_SFT                   (0x1 << 18)
#define RG_I2S4_OUT_SLV_LRCK_LATCH_EDGE_SFT                   17
#define RG_I2S4_OUT_SLV_LRCK_LATCH_EDGE_MASK                  0x1
#define RG_I2S4_OUT_SLV_LRCK_LATCH_EDGE_MASK_SFT              (0x1 << 17)
#define RG_I2S4_OUT_SLV_BCK_INV_SEL_SFT                       16
#define RG_I2S4_OUT_SLV_BCK_INV_SEL_MASK                      0x1
#define RG_I2S4_OUT_SLV_BCK_INV_SEL_MASK_SFT                  (0x1 << 16)
#define RG_I2S5_IN_BCK_NEG_EG_LATCH_SFT                       13
#define RG_I2S5_IN_BCK_NEG_EG_LATCH_MASK                      0x1
#define RG_I2S5_IN_BCK_NEG_EG_LATCH_MASK_SFT                  (0x1 << 13)
#define RG_I2S5_OUT_BCK_NEG_EG_LATCH_SFT                      12
#define RG_I2S5_OUT_BCK_NEG_EG_LATCH_MASK                     0x1
#define RG_I2S5_OUT_BCK_NEG_EG_LATCH_MASK_SFT                 (0x1 << 12)
#define RG_I2S5_IN_SLV_LRCK_LATCH_EDGE_SFT                    11
#define RG_I2S5_IN_SLV_LRCK_LATCH_EDGE_MASK                   0x1
#define RG_I2S5_IN_SLV_LRCK_LATCH_EDGE_MASK_SFT               (0x1 << 11)
#define RG_I2S5_IN_SLV_BCK_INV_SEL_SFT                        10
#define RG_I2S5_IN_SLV_BCK_INV_SEL_MASK                       0x1
#define RG_I2S5_IN_SLV_BCK_INV_SEL_MASK_SFT                   (0x1 << 10)
#define RG_I2S5_OUT_SLV_LRCK_LATCH_EDGE_SFT                   9
#define RG_I2S5_OUT_SLV_LRCK_LATCH_EDGE_MASK                  0x1
#define RG_I2S5_OUT_SLV_LRCK_LATCH_EDGE_MASK_SFT              (0x1 << 9)
#define RG_I2S5_OUT_SLV_BCK_INV_SEL_SFT                       8
#define RG_I2S5_OUT_SLV_BCK_INV_SEL_MASK                      0x1
#define RG_I2S5_OUT_SLV_BCK_INV_SEL_MASK_SFT                  (0x1 << 8)
#define RG_I2S4_PAD_TOP_CK_EN_SFT                             5
#define RG_I2S4_PAD_TOP_CK_EN_MASK                            0x1
#define RG_I2S4_PAD_TOP_CK_EN_MASK_SFT                        (0x1 << 5)
#define RG_I2S5_PAD_TOP_CK_EN_SFT                             4
#define RG_I2S5_PAD_TOP_CK_EN_MASK                            0x1
#define RG_I2S5_PAD_TOP_CK_EN_MASK_SFT                        (0x1 << 4)
#define RG_TEST_TYPE_SFT                                      2
#define RG_TEST_TYPE_MASK                                     0x1
#define RG_TEST_TYPE_MASK_SFT                                 (0x1 << 2)
#define RG_SW_RESET_SFT                                       1
#define RG_SW_RESET_MASK                                      0x1
#define RG_SW_RESET_MASK_SFT                                  (0x1 << 1)
#define RG_TEST_ON_SFT                                        0
#define RG_TEST_ON_MASK                                       0x1
#define RG_TEST_ON_MASK_SFT                                   (0x1 << 0)

/* AUD_TOP_MON_RG */
#define AUD_TOP_MON_SFT                                       0
#define AUD_TOP_MON_MASK                                      0xffffffff
#define AUD_TOP_MON_MASK_SFT                                  (0xffffffff << 0)

/* AUDIO_USE_DEFAULT_DELSEL0 */
#define USE_DEFAULT_DELSEL_RG_SFT                             0
#define USE_DEFAULT_DELSEL_RG_MASK                            0xffffffff
#define USE_DEFAULT_DELSEL_RG_MASK_SFT                        (0xffffffff << 0)

/* AUDIO_USE_DEFAULT_DELSEL1 */
#define USE_DEFAULT_DELSEL_RG_SFT                             0
#define USE_DEFAULT_DELSEL_RG_MASK                            0xffffffff
#define USE_DEFAULT_DELSEL_RG_MASK_SFT                        (0xffffffff << 0)

/* AUDIO_USE_DEFAULT_DELSEL2 */
#define USE_DEFAULT_DELSEL_RG_SFT                             0
#define USE_DEFAULT_DELSEL_RG_MASK                            0xffffffff
#define USE_DEFAULT_DELSEL_RG_MASK_SFT                        (0xffffffff << 0)

/* AFE_CONNSYS_I2S_IPM_VER_MON */
#define RG_CONNSYS_I2S_IPM_VER_MON_SFT                        0
#define RG_CONNSYS_I2S_IPM_VER_MON_MASK                       0xffffffff
#define RG_CONNSYS_I2S_IPM_VER_MON_MASK_SFT                   (0xffffffff << 0)

/* AFE_CONNSYS_I2S_MON_SEL */
#define RG_CONNSYS_I2S_MON_SEL_SFT                            0
#define RG_CONNSYS_I2S_MON_SEL_MASK                           0xff
#define RG_CONNSYS_I2S_MON_SEL_MASK_SFT                       (0xff << 0)

/* AFE_CONNSYS_I2S_MON */
#define RG_CONNSYS_I2S_MON_SFT                                0
#define RG_CONNSYS_I2S_MON_MASK                               0xffffffff
#define RG_CONNSYS_I2S_MON_MASK_SFT                           (0xffffffff << 0)

/* AFE_CONNSYS_I2S_CON */
#define I2S_SOFT_RST_SFT                                      31
#define I2S_SOFT_RST_MASK                                     0x1
#define I2S_SOFT_RST_MASK_SFT                                 (0x1 << 31)
#define BCK_NEG_EG_LATCH_SFT                                  30
#define BCK_NEG_EG_LATCH_MASK                                 0x1
#define BCK_NEG_EG_LATCH_MASK_SFT                             (0x1 << 30)
#define BCK_INV_SFT                                           29
#define BCK_INV_MASK                                          0x1
#define BCK_INV_MASK_SFT                                      (0x1 << 29)
#define I2SIN_PAD_SEL_SFT                                     28
#define I2SIN_PAD_SEL_MASK                                    0x1
#define I2SIN_PAD_SEL_MASK_SFT                                (0x1 << 28)
#define I2S_LOOPBACK_SFT                                      20
#define I2S_LOOPBACK_MASK                                     0x1
#define I2S_LOOPBACK_MASK_SFT                                 (0x1 << 20)
#define I2S_HDEN_SFT                                          12
#define I2S_HDEN_MASK                                         0x1
#define I2S_HDEN_MASK_SFT                                     (0x1 << 12)
#define I2S_MODE_SFT                                          8
#define I2S_MODE_MASK                                         0xf
#define I2S_MODE_MASK_SFT                                     (0xf << 8)
#define I2S_BYPSRC_SFT                                        6
#define I2S_BYPSRC_MASK                                       0x1
#define I2S_BYPSRC_MASK_SFT                                   (0x1 << 6)
#define INV_LRCK_SFT                                          5
#define INV_LRCK_MASK                                         0x1
#define INV_LRCK_MASK_SFT                                     (0x1 << 5)
#define I2S_FMT_SFT                                           3
#define I2S_FMT_MASK                                          0x1
#define I2S_FMT_MASK_SFT                                      (0x1 << 3)
#define I2S_SRC_SFT                                           2
#define I2S_SRC_MASK                                          0x1
#define I2S_SRC_MASK_SFT                                      (0x1 << 2)
#define I2S_WLEN_SFT                                          1
#define I2S_WLEN_MASK                                         0x1
#define I2S_WLEN_MASK_SFT                                     (0x1 << 1)
#define I2S_EN_SFT                                            0
#define I2S_EN_MASK                                           0x1
#define I2S_EN_MASK_SFT                                       (0x1 << 0)

/* AFE_PCM0_INTF_CON0 */
#define PCM0_HDEN_SFT                                         26
#define PCM0_HDEN_MASK                                        0x1
#define PCM0_HDEN_MASK_SFT                                    (0x1 << 26)
#define PCM0_SYNC_DELSEL_SFT                                  25
#define PCM0_SYNC_DELSEL_MASK                                 0x1
#define PCM0_SYNC_DELSEL_MASK_SFT                             (0x1 << 25)
#define PCM0_TX_LR_SWAP_SFT                                   24
#define PCM0_TX_LR_SWAP_MASK                                  0x1
#define PCM0_TX_LR_SWAP_MASK_SFT                              (0x1 << 24)
#define PCM0_SYNC_OUT_INV_SFT                                 23
#define PCM0_SYNC_OUT_INV_MASK                                0x1
#define PCM0_SYNC_OUT_INV_MASK_SFT                            (0x1 << 23)
#define PCM0_BCLK_OUT_INV_SFT                                 22
#define PCM0_BCLK_OUT_INV_MASK                                0x1
#define PCM0_BCLK_OUT_INV_MASK_SFT                            (0x1 << 22)
#define PCM0_SYNC_IN_INV_SFT                                  21
#define PCM0_SYNC_IN_INV_MASK                                 0x1
#define PCM0_SYNC_IN_INV_MASK_SFT                             (0x1 << 21)
#define PCM0_BCLK_IN_INV_SFT                                  20
#define PCM0_BCLK_IN_INV_MASK                                 0x1
#define PCM0_BCLK_IN_INV_MASK_SFT                             (0x1 << 20)
#define PCM0_TX_LCH_RPT_SFT                                   19
#define PCM0_TX_LCH_RPT_MASK                                  0x1
#define PCM0_TX_LCH_RPT_MASK_SFT                              (0x1 << 19)
#define PCM0_VBT_16K_MODE_SFT                                 18
#define PCM0_VBT_16K_MODE_MASK                                0x1
#define PCM0_VBT_16K_MODE_MASK_SFT                            (0x1 << 18)
#define PCM0_BIT_LENGTH_SFT                                   16
#define PCM0_BIT_LENGTH_MASK                                  0x3
#define PCM0_BIT_LENGTH_MASK_SFT                              (0x3 << 16)
#define PCM0_WLEN_SFT                                         14
#define PCM0_WLEN_MASK                                        0x3
#define PCM0_WLEN_MASK_SFT                                    (0x3 << 14)
#define PCM0_SYNC_LENGTH_SFT                                  9
#define PCM0_SYNC_LENGTH_MASK                                 0x1f
#define PCM0_SYNC_LENGTH_MASK_SFT                             (0x1f << 9)
#define PCM0_SYNC_TYPE_SFT                                    8
#define PCM0_SYNC_TYPE_MASK                                   0x1
#define PCM0_SYNC_TYPE_MASK_SFT                               (0x1 << 8)
#define PCM0_BYP_ASRC_SFT                                     7
#define PCM0_BYP_ASRC_MASK                                    0x1
#define PCM0_BYP_ASRC_MASK_SFT                                (0x1 << 7)
#define PCM0_SLAVE_SFT                                         6
#define PCM0_SLAVE_MASK                                        0x1
#define PCM0_SLAVE_MASK_SFT                                    (0x1 << 6)
#define PCM0_MODE_SFT                                          3
#define PCM0_MODE_MASK                                         0x7
#define PCM0_MODE_MASK_SFT                                     (0x7 << 3)
#define PCM0_FMT_SFT                                           1
#define PCM0_FMT_MASK                                          0x3
#define PCM0_FMT_MASK_SFT                                      (0x3 << 1)
#define PCM0_EN_SFT                                            0
#define PCM0_EN_MASK                                           0x1
#define PCM0_EN_MASK_SFT                                       (0x1 << 0)

/* AFE_PCM0_INTF_CON1 */
#define PCM0_TX_RX_LOOPBACK_SFT                               31
#define PCM0_TX_RX_LOOPBACK_MASK                              0x1
#define PCM0_TX_RX_LOOPBACK_MASK_SFT                          (0x1 << 31)
#define PCM0_BUFFER_LOOPBACK_SFT                              30
#define PCM0_BUFFER_LOOPBACK_MASK                             0x1
#define PCM0_BUFFER_LOOPBACK_MASK_SFT                         (0x1 << 30)
#define PCM0_PARALLEL_LOOPBACK_SFT                            29
#define PCM0_PARALLEL_LOOPBACK_MASK                           0x1
#define PCM0_PARALLEL_LOOPBACK_MASK_SFT                       (0x1 << 29)
#define PCM0_SERIAL_LOOPBACK_SFT                              28
#define PCM0_SERIAL_LOOPBACK_MASK                             0x1
#define PCM0_SERIAL_LOOPBACK_MASK_SFT                         (0x1 << 28)
#define PCM0_DAI_LOOPBACK_SFT                                 27
#define PCM0_DAI_LOOPBACK_MASK                                0x1
#define PCM0_DAI_LOOPBACK_MASK_SFT                            (0x1 << 27)
#define PCM0_I2S_LOOPBACK_SFT                                 26
#define PCM0_I2S_LOOPBACK_MASK                                0x1
#define PCM0_I2S_LOOPBACK_MASK_SFT                            (0x1 << 26)
#define PCM0_1X_EN_DOMAIN_SFT                                 23
#define PCM0_1X_EN_DOMAIN_MASK                                0x7
#define PCM0_1X_EN_DOMAIN_MASK_SFT                            (0x7 << 23)
#define PCM0_1X_EN_MODE_SFT                                   18
#define PCM0_1X_EN_MODE_MASK                                  0x1f
#define PCM0_1X_EN_MODE_MASK_SFT                              (0x1f << 18)
#define PCM0_TX3_RCH_DBG_MODE_SFT                             17
#define PCM0_TX3_RCH_DBG_MODE_MASK                            0x1
#define PCM0_TX3_RCH_DBG_MODE_MASK_SFT                        (0x1 << 17)
#define PCM0_PCM1_LOOPBACK_SFT                                16
#define PCM0_PCM1_LOOPBACK_MASK                               0x1
#define PCM0_PCM1_LOOPBACK_MASK_SFT                           (0x1 << 16)
#define PCM0_LOOPBACK_CH_SEL_SFT                              12
#define PCM0_LOOPBACK_CH_SEL_MASK                             0x3
#define PCM0_LOOPBACK_CH_SEL_MASK_SFT                         (0x3 << 12)
#define PCM0_BT_MODE_SFT                                      11
#define PCM0_BT_MODE_MASK                                     0x1
#define PCM0_BT_MODE_MASK_SFT                                 (0x1 << 11)
#define PCM0_EXT_MODEM_SFT                                    10
#define PCM0_EXT_MODEM_MASK                                   0x1
#define PCM0_EXT_MODEM_MASK_SFT                               (0x1 << 10)
#define PCM0_USE_MD3_SFT                                      9
#define PCM0_USE_MD3_MASK                                     0x1
#define PCM0_USE_MD3_MASK_SFT                                 (0x1 << 9)
#define PCM0_FIX_VALUE_SEL_SFT                                8
#define PCM0_FIX_VALUE_SEL_MASK                               0x1
#define PCM0_FIX_VALUE_SEL_MASK_SFT                           (0x1 << 8)
#define PCM0_TX_FIX_VALUE_SFT                                 0
#define PCM0_TX_FIX_VALUE_MASK                                0xff
#define PCM0_TX_FIX_VALUE_MASK_SFT                            (0xff << 0)

/* AFE_PCM_INTF_MON */
#define PCM0_TX_FIFO_OV_SFT                                   5
#define PCM0_TX_FIFO_OV_MASK                                  0x1
#define PCM0_TX_FIFO_OV_MASK_SFT                              (0x1 << 5)
#define PCM0_RX_FIFO_OV_SFT                                   4
#define PCM0_RX_FIFO_OV_MASK                                  0x1
#define PCM0_RX_FIFO_OV_MASK_SFT                              (0x1 << 4)
#define PCM1_TX_FIFO_OV_SFT                                   3
#define PCM1_TX_FIFO_OV_MASK                                  0x1
#define PCM1_TX_FIFO_OV_MASK_SFT                              (0x1 << 3)
#define PCM1_RX_FIFO_OV_SFT                                   2
#define PCM1_RX_FIFO_OV_MASK                                  0x1
#define PCM1_RX_FIFO_OV_MASK_SFT                              (0x1 << 2)
#define PCM0_SYNC_GLITCH_SFT                                  1
#define PCM0_SYNC_GLITCH_MASK                                 0x1
#define PCM0_SYNC_GLITCH_MASK_SFT                             (0x1 << 1)
#define PCM1_SYNC_GLITCH_SFT                                  0
#define PCM1_SYNC_GLITCH_MASK                                 0x1
#define PCM1_SYNC_GLITCH_MASK_SFT                             (0x1 << 0)

/* AFE_PCM1_INTF_CON0 */
#define PCM1_TX_FIX_VALUE_SFT                                 24
#define PCM1_TX_FIX_VALUE_MASK                                0xff
#define PCM1_TX_FIX_VALUE_MASK_SFT                            (0xff << 24)
#define PCM1_FIX_VALUE_SEL_SFT                                23
#define PCM1_FIX_VALUE_SEL_MASK                               0x1
#define PCM1_FIX_VALUE_SEL_MASK_SFT                           (0x1 << 23)
#define PCM1_BUFFER_LOOPBACK_SFT                              22
#define PCM1_BUFFER_LOOPBACK_MASK                             0x1
#define PCM1_BUFFER_LOOPBACK_MASK_SFT                         (0x1 << 22)
#define PCM1_PARALLEL_LOOPBACK_SFT                            21
#define PCM1_PARALLEL_LOOPBACK_MASK                           0x1
#define PCM1_PARALLEL_LOOPBACK_MASK_SFT                       (0x1 << 21)
#define PCM1_SERIAL_LOOPBACK_SFT                              20
#define PCM1_SERIAL_LOOPBACK_MASK                             0x1
#define PCM1_SERIAL_LOOPBACK_MASK_SFT                         (0x1 << 20)
#define PCM1_DAI_PCM1_LOOPBACK_SFT                            19
#define PCM1_DAI_PCM1_LOOPBACK_MASK                           0x1
#define PCM1_DAI_PCM1_LOOPBACK_MASK_SFT                       (0x1 << 19)
#define PCM1_I2S_PCM1_LOOPBACK_SFT                            18
#define PCM1_I2S_PCM1_LOOPBACK_MASK                           0x1
#define PCM1_I2S_PCM1_LOOPBACK_MASK_SFT                       (0x1 << 18)
#define PCM1_SYNC_DELSEL_SFT                                  17
#define PCM1_SYNC_DELSEL_MASK                                 0x1
#define PCM1_SYNC_DELSEL_MASK_SFT                             (0x1 << 17)
#define PCM1_TX_LR_SWAP_SFT                                   16
#define PCM1_TX_LR_SWAP_MASK                                  0x1
#define PCM1_TX_LR_SWAP_MASK_SFT                              (0x1 << 16)
#define PCM1_SYNC_IN_INV_SFT                                  15
#define PCM1_SYNC_IN_INV_MASK                                 0x1
#define PCM1_SYNC_IN_INV_MASK_SFT                             (0x1 << 15)
#define PCM1_BCLK_IN_INV_SFT                                  14
#define PCM1_BCLK_IN_INV_MASK                                 0x1
#define PCM1_BCLK_IN_INV_MASK_SFT                             (0x1 << 14)
#define PCM1_TX_LCH_RPT_SFT                                   13
#define PCM1_TX_LCH_RPT_MASK                                  0x1
#define PCM1_TX_LCH_RPT_MASK_SFT                              (0x1 << 13)
#define PCM1_VBT_16K_MODE_SFT                                 12
#define PCM1_VBT_16K_MODE_MASK                                0x1
#define PCM1_VBT_16K_MODE_MASK_SFT                            (0x1 << 12)
#define PCM1_LOOPBACK_CH_SEL_SFT                              10
#define PCM1_LOOPBACK_CH_SEL_MASK                             0x3
#define PCM1_LOOPBACK_CH_SEL_MASK_SFT                         (0x3 << 10)
#define PCM1_TX2_BT_MODE_SFT                                  8
#define PCM1_TX2_BT_MODE_MASK                                 0x1
#define PCM1_TX2_BT_MODE_MASK_SFT                             (0x1 << 8)
#define PCM1_BT_MODE_SFT                                      7
#define PCM1_BT_MODE_MASK                                     0x1
#define PCM1_BT_MODE_MASK_SFT                                 (0x1 << 7)
#define PCM1_AFIFO_SFT                                        6
#define PCM1_AFIFO_MASK                                       0x1
#define PCM1_AFIFO_MASK_SFT                                   (0x1 << 6)
#define PCM1_WLEN_SFT                                         5
#define PCM1_WLEN_MASK                                        0x1
#define PCM1_WLEN_MASK_SFT                                    (0x1 << 5)
#define PCM1_MODE_SFT                                         3
#define PCM1_MODE_MASK                                        0x3
#define PCM1_MODE_MASK_SFT                                    (0x3 << 3)
#define PCM1_FMT_SFT                                          1
#define PCM1_FMT_MASK                                         0x3
#define PCM1_FMT_MASK_SFT                                     (0x3 << 1)
#define PCM1_EN_SFT                                           0
#define PCM1_EN_MASK                                          0x1
#define PCM1_EN_MASK_SFT                                      (0x1 << 0)

/* AFE_PCM1_INTF_CON1 */
#define PCM1_1X_EN_DOMAIN_SFT                                 23
#define PCM1_1X_EN_DOMAIN_MASK                                0x7
#define PCM1_1X_EN_DOMAIN_MASK_SFT                            (0x7 << 23)
#define PCM1_1X_EN_MODE_SFT                                   18
#define PCM1_1X_EN_MODE_MASK                                  0x1f
#define PCM1_1X_EN_MODE_MASK_SFT                              (0x1f << 18)

/* AFE_PCM_TOP_IP_VERSION */
#define AFE_PCM_TOP_IP_VERSION_SFT                            0
#define AFE_PCM_TOP_IP_VERSION_MASK                           0xffffffff
#define AFE_PCM_TOP_IP_VERSION_MASK_SFT                       (0xffffffff << 0)

/* AFE_IRQ_MCU_EN */
#define AFE_IRQ_MCU_EN_SFT                                    0
#define AFE_IRQ_MCU_EN_MASK                                   0xffffffff
#define AFE_IRQ_MCU_EN_MASK_SFT                               (0xffffffff << 0)

/* AFE_IRQ_MCU_DSP_EN */
#define AFE_IRQ_DSP_EN_SFT                                    0
#define AFE_IRQ_DSP_EN_MASK                                   0xffffffff
#define AFE_IRQ_DSP_EN_MASK_SFT                               (0xffffffff << 0)

/* AFE_IRQ_MCU_DSP2_EN */
#define AFE_IRQ_DSP2_EN_SFT                                   0
#define AFE_IRQ_DSP2_EN_MASK                                  0xffffffff
#define AFE_IRQ_DSP2_EN_MASK_SFT                              (0xffffffff << 0)

/* AFE_IRQ_MCU_SCP_EN */
#define IRQ31_MCU_SCP_EN_SFT                                  31
#define IRQ30_MCU_SCP_EN_SFT                                  30
#define IRQ29_MCU_SCP_EN_SFT                                  29
#define IRQ28_MCU_SCP_EN_SFT                                  28
#define IRQ27_MCU_SCP_EN_SFT                                  27
#define IRQ26_MCU_SCP_EN_SFT                                  26
#define IRQ25_MCU_SCP_EN_SFT                                  25
#define IRQ24_MCU_SCP_EN_SFT                                  24
#define IRQ23_MCU_SCP_EN_SFT                                  23
#define IRQ22_MCU_SCP_EN_SFT                                  22
#define IRQ21_MCU_SCP_EN_SFT                                  21
#define IRQ20_MCU_SCP_EN_SFT                                  20
#define IRQ19_MCU_SCP_EN_SFT                                  19
#define IRQ18_MCU_SCP_EN_SFT                                  18
#define IRQ17_MCU_SCP_EN_SFT                                  17
#define IRQ16_MCU_SCP_EN_SFT                                  16
#define IRQ15_MCU_SCP_EN_SFT                                  15
#define IRQ14_MCU_SCP_EN_SFT                                  14
#define IRQ13_MCU_SCP_EN_SFT                                  13
#define IRQ12_MCU_SCP_EN_SFT                                  12
#define IRQ11_MCU_SCP_EN_SFT                                  11
#define IRQ10_MCU_SCP_EN_SFT                                  10
#define IRQ9_MCU_SCP_EN_SFT                                   9
#define IRQ8_MCU_SCP_EN_SFT                                   8
#define IRQ7_MCU_SCP_EN_SFT                                   7
#define IRQ6_MCU_SCP_EN_SFT                                   6
#define IRQ5_MCU_SCP_EN_SFT                                   5
#define IRQ4_MCU_SCP_EN_SFT                                   4
#define IRQ3_MCU_SCP_EN_SFT                                   3
#define IRQ2_MCU_SCP_EN_SFT                                   2
#define IRQ1_MCU_SCP_EN_SFT                                   1
#define IRQ0_MCU_SCP_EN_SFT                                   0

/* AFE_CUSTOM_IRQ_MCU_EN */
#define AFE_CUSTOM_IRQ_MCU_EN_SFT                             0
#define AFE_CUSTOM_IRQ_MCU_EN_MASK                            0xffffffff
#define AFE_CUSTOM_IRQ_MCU_EN_MASK_SFT                        (0xffffffff << 0)

/* AFE_CUSTOM_IRQ_MCU_DSP_EN */
#define AFE_CUSTOM_IRQ_DSP_EN_SFT                             0
#define AFE_CUSTOM_IRQ_DSP_EN_MASK                            0xffffffff
#define AFE_CUSTOM_IRQ_DSP_EN_MASK_SFT                        (0xffffffff << 0)

/* AFE_CUSTOM_IRQ_MCU_DSP2_EN */
#define AFE_CUSTOM_IRQ_DSP2_EN_SFT                            0
#define AFE_CUSTOM_IRQ_DSP2_EN_MASK                           0xffffffff
#define AFE_CUSTOM_IRQ_DSP2_EN_MASK_SFT                       (0xffffffff << 0)

/* AFE_CUSTOM_IRQ_MCU_SCP_EN */
#define AFE_CUSTOM_IRQ_SCP_EN_SFT                             0
#define AFE_CUSTOM_IRQ_SCP_EN_MASK                            0xffffffff
#define AFE_CUSTOM_IRQ_SCP_EN_MASK_SFT                        (0xffffffff << 0)

/* AFE_IRQ_MCU_STATUS */
#define IRQ26_MCU_SFT                                         26
#define IRQ26_MCU_MASK                                        0x1
#define IRQ26_MCU_MASK_SFT                                    (0x1 << 26)
#define IRQ25_MCU_SFT                                         25
#define IRQ25_MCU_MASK                                        0x1
#define IRQ25_MCU_MASK_SFT                                    (0x1 << 25)
#define IRQ24_MCU_SFT                                         24
#define IRQ24_MCU_MASK                                        0x1
#define IRQ24_MCU_MASK_SFT                                    (0x1 << 24)
#define IRQ23_MCU_SFT                                         23
#define IRQ23_MCU_MASK                                        0x1
#define IRQ23_MCU_MASK_SFT                                    (0x1 << 23)
#define IRQ22_MCU_SFT                                         22
#define IRQ22_MCU_MASK                                        0x1
#define IRQ22_MCU_MASK_SFT                                    (0x1 << 22)
#define IRQ21_MCU_SFT                                         21
#define IRQ21_MCU_MASK                                        0x1
#define IRQ21_MCU_MASK_SFT                                    (0x1 << 21)
#define IRQ20_MCU_SFT                                         20
#define IRQ20_MCU_MASK                                        0x1
#define IRQ20_MCU_MASK_SFT                                    (0x1 << 20)
#define IRQ19_MCU_SFT                                         19
#define IRQ19_MCU_MASK                                        0x1
#define IRQ19_MCU_MASK_SFT                                    (0x1 << 19)
#define IRQ18_MCU_SFT                                         18
#define IRQ18_MCU_MASK                                        0x1
#define IRQ18_MCU_MASK_SFT                                    (0x1 << 18)
#define IRQ17_MCU_SFT                                         17
#define IRQ17_MCU_MASK                                        0x1
#define IRQ17_MCU_MASK_SFT                                    (0x1 << 17)
#define IRQ16_MCU_SFT                                         16
#define IRQ16_MCU_MASK                                        0x1
#define IRQ16_MCU_MASK_SFT                                    (0x1 << 16)
#define IRQ15_MCU_SFT                                         15
#define IRQ15_MCU_MASK                                        0x1
#define IRQ15_MCU_MASK_SFT                                    (0x1 << 15)
#define IRQ14_MCU_SFT                                         14
#define IRQ14_MCU_MASK                                        0x1
#define IRQ14_MCU_MASK_SFT                                    (0x1 << 14)
#define IRQ13_MCU_SFT                                         13
#define IRQ13_MCU_MASK                                        0x1
#define IRQ13_MCU_MASK_SFT                                    (0x1 << 13)
#define IRQ12_MCU_SFT                                         12
#define IRQ12_MCU_MASK                                        0x1
#define IRQ12_MCU_MASK_SFT                                    (0x1 << 12)
#define IRQ11_MCU_SFT                                         11
#define IRQ11_MCU_MASK                                        0x1
#define IRQ11_MCU_MASK_SFT                                    (0x1 << 11)
#define IRQ10_MCU_SFT                                         10
#define IRQ10_MCU_MASK                                        0x1
#define IRQ10_MCU_MASK_SFT                                    (0x1 << 10)
#define IRQ9_MCU_SFT                                          9
#define IRQ9_MCU_MASK                                         0x1
#define IRQ9_MCU_MASK_SFT                                     (0x1 << 9)
#define IRQ8_MCU_SFT                                          8
#define IRQ8_MCU_MASK                                         0x1
#define IRQ8_MCU_MASK_SFT                                     (0x1 << 8)
#define IRQ7_MCU_SFT                                          7
#define IRQ7_MCU_MASK                                         0x1
#define IRQ7_MCU_MASK_SFT                                     (0x1 << 7)
#define IRQ6_MCU_SFT                                          6
#define IRQ6_MCU_MASK                                         0x1
#define IRQ6_MCU_MASK_SFT                                     (0x1 << 6)
#define IRQ5_MCU_SFT                                          5
#define IRQ5_MCU_MASK                                         0x1
#define IRQ5_MCU_MASK_SFT                                     (0x1 << 5)
#define IRQ4_MCU_SFT                                          4
#define IRQ4_MCU_MASK                                         0x1
#define IRQ4_MCU_MASK_SFT                                     (0x1 << 4)
#define IRQ3_MCU_SFT                                          3
#define IRQ3_MCU_MASK                                         0x1
#define IRQ3_MCU_MASK_SFT                                     (0x1 << 3)
#define IRQ2_MCU_SFT                                          2
#define IRQ2_MCU_MASK                                         0x1
#define IRQ2_MCU_MASK_SFT                                     (0x1 << 2)
#define IRQ1_MCU_SFT                                          1
#define IRQ1_MCU_MASK                                         0x1
#define IRQ1_MCU_MASK_SFT                                     (0x1 << 1)
#define IRQ0_MCU_SFT                                          0
#define IRQ0_MCU_MASK                                         0x1
#define IRQ0_MCU_MASK_SFT                                     (0x1 << 0)

/* AFE_CUSTOM_IRQ_MCU_STATUS */
#define CUSTOM_IRQ21_MCU_SFT                                  21
#define CUSTOM_IRQ21_MCU_MASK                                 0x1
#define CUSTOM_IRQ21_MCU_MASK_SFT                             (0x1 << 21)
#define CUSTOM_IRQ20_MCU_SFT                                  20
#define CUSTOM_IRQ20_MCU_MASK                                 0x1
#define CUSTOM_IRQ20_MCU_MASK_SFT                             (0x1 << 20)
#define CUSTOM_IRQ19_MCU_SFT                                  19
#define CUSTOM_IRQ19_MCU_MASK                                 0x1
#define CUSTOM_IRQ19_MCU_MASK_SFT                             (0x1 << 19)
#define CUSTOM_IRQ18_MCU_SFT                                  18
#define CUSTOM_IRQ18_MCU_MASK                                 0x1
#define CUSTOM_IRQ18_MCU_MASK_SFT                             (0x1 << 18)
#define CUSTOM_IRQ17_MCU_SFT                                  17
#define CUSTOM_IRQ17_MCU_MASK                                 0x1
#define CUSTOM_IRQ17_MCU_MASK_SFT                             (0x1 << 17)
#define CUSTOM_IRQ16_MCU_SFT                                  16
#define CUSTOM_IRQ16_MCU_MASK                                 0x1
#define CUSTOM_IRQ16_MCU_MASK_SFT                             (0x1 << 16)
#define CUSTOM_IRQ9_MCU_SFT                                   9
#define CUSTOM_IRQ9_MCU_MASK                                  0x1
#define CUSTOM_IRQ9_MCU_MASK_SFT                              (0x1 << 9)
#define CUSTOM_IRQ8_MCU_SFT                                   8
#define CUSTOM_IRQ8_MCU_MASK                                  0x1
#define CUSTOM_IRQ8_MCU_MASK_SFT                              (0x1 << 8)
#define CUSTOM_IRQ7_MCU_SFT                                   7
#define CUSTOM_IRQ7_MCU_MASK                                  0x1
#define CUSTOM_IRQ7_MCU_MASK_SFT                              (0x1 << 7)
#define CUSTOM_IRQ6_MCU_SFT                                   6
#define CUSTOM_IRQ6_MCU_MASK                                  0x1
#define CUSTOM_IRQ6_MCU_MASK_SFT                              (0x1 << 6)
#define CUSTOM_IRQ5_MCU_SFT                                   5
#define CUSTOM_IRQ5_MCU_MASK                                  0x1
#define CUSTOM_IRQ5_MCU_MASK_SFT                              (0x1 << 5)
#define CUSTOM_IRQ4_MCU_SFT                                   4
#define CUSTOM_IRQ4_MCU_MASK                                  0x1
#define CUSTOM_IRQ4_MCU_MASK_SFT                              (0x1 << 4)
#define CUSTOM_IRQ3_MCU_SFT                                   3
#define CUSTOM_IRQ3_MCU_MASK                                  0x1
#define CUSTOM_IRQ3_MCU_MASK_SFT                              (0x1 << 3)
#define CUSTOM_IRQ2_MCU_SFT                                   2
#define CUSTOM_IRQ2_MCU_MASK                                  0x1
#define CUSTOM_IRQ2_MCU_MASK_SFT                              (0x1 << 2)
#define CUSTOM_IRQ1_MCU_SFT                                   1
#define CUSTOM_IRQ1_MCU_MASK                                  0x1
#define CUSTOM_IRQ1_MCU_MASK_SFT                              (0x1 << 1)
#define CUSTOM_IRQ0_MCU_SFT                                   0
#define CUSTOM_IRQ0_MCU_MASK                                  0x1
#define CUSTOM_IRQ0_MCU_MASK_SFT                              (0x1 << 0)

/* AFE_IRQ_MCU_CFG */
#define AFE_IRQ_CLR_CFG_SFT                                   31
#define AFE_IRQ_CLR_CFG_MASK                                  0x1
#define AFE_IRQ_CLR_CFG_MASK_SFT                              (0x1 << 31)
#define AFE_IRQ_MISS_FLAG_CLR_CFG_SFT                         30
#define AFE_IRQ_MISS_FLAG_CLR_CFG_MASK                        0x1
#define AFE_IRQ_MISS_FLAG_CLR_CFG_MASK_SFT                    (0x1 << 30)
#define AFE_IRQ_MCU_CNT_SFT                                   0
#define AFE_IRQ_MCU_CNT_MASK                                  0xffffff
#define AFE_IRQ_MCU_CNT_MASK_SFT                              (0xffffff << 0)

/* AFE_IRQ0_MCU_CFG0 */
#define AFE_IRQ0_MCU_DOMAIN_SFT                               9
#define AFE_IRQ0_MCU_DOMAIN_MASK                              0x7
#define AFE_IRQ0_MCU_DOMAIN_MASK_SFT                          (0x7 << 9)
#define AFE_IRQ0_MCU_FS_SFT                                   4
#define AFE_IRQ0_MCU_FS_MASK                                  0x1f
#define AFE_IRQ0_MCU_FS_MASK_SFT                              (0x1f << 4)
#define AFE_IRQ0_MCU_ON_SFT                                   0
#define AFE_IRQ0_MCU_ON_MASK                                  0x1
#define AFE_IRQ0_MCU_ON_MASK_SFT                              (0x1 << 0)

/* AFE_IRQ0_MCU_CFG1 */
#define AFE_IRQ0_CLR_CFG_SFT                                  31
#define AFE_IRQ0_CLR_CFG_MASK                                 0x1
#define AFE_IRQ0_CLR_CFG_MASK_SFT                             (0x1 << 31)
#define AFE_IRQ0_MISS_FLAG_CLR_CFG_SFT                        30
#define AFE_IRQ0_MISS_FLAG_CLR_CFG_MASK                       0x1
#define AFE_IRQ0_MISS_FLAG_CLR_CFG_MASK_SFT                   (0x1 << 30)
#define AFE_IRQ0_MCU_CNT_SFT                                  0
#define AFE_IRQ0_MCU_CNT_MASK                                 0xffffff
#define AFE_IRQ0_MCU_CNT_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ1_MCU_CFG0 */
#define AFE_IRQ1_MCU_DOMAIN_SFT                               9
#define AFE_IRQ1_MCU_DOMAIN_MASK                              0x7
#define AFE_IRQ1_MCU_DOMAIN_MASK_SFT                          (0x7 << 9)
#define AFE_IRQ1_MCU_FS_SFT                                   4
#define AFE_IRQ1_MCU_FS_MASK                                  0x1f
#define AFE_IRQ1_MCU_FS_MASK_SFT                              (0x1f << 4)
#define AFE_IRQ1_MCU_ON_SFT                                   0
#define AFE_IRQ1_MCU_ON_MASK                                  0x1
#define AFE_IRQ1_MCU_ON_MASK_SFT                              (0x1 << 0)

/* AFE_IRQ1_MCU_CFG1 */
#define AFE_IRQ1_CLR_CFG_SFT                                  31
#define AFE_IRQ1_CLR_CFG_MASK                                 0x1
#define AFE_IRQ1_CLR_CFG_MASK_SFT                             (0x1 << 31)
#define AFE_IRQ1_MISS_FLAG_CLR_CFG_SFT                        30
#define AFE_IRQ1_MISS_FLAG_CLR_CFG_MASK                       0x1
#define AFE_IRQ1_MISS_FLAG_CLR_CFG_MASK_SFT                   (0x1 << 30)
#define AFE_IRQ1_MCU_CNT_SFT                                  0
#define AFE_IRQ1_MCU_CNT_MASK                                 0xffffff
#define AFE_IRQ1_MCU_CNT_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ2_MCU_CFG0 */
#define AFE_IRQ2_MCU_DOMAIN_SFT                               9
#define AFE_IRQ2_MCU_DOMAIN_MASK                              0x7
#define AFE_IRQ2_MCU_DOMAIN_MASK_SFT                          (0x7 << 9)
#define AFE_IRQ2_MCU_FS_SFT                                   4
#define AFE_IRQ2_MCU_FS_MASK                                  0x1f
#define AFE_IRQ2_MCU_FS_MASK_SFT                              (0x1f << 4)
#define AFE_IRQ2_MCU_ON_SFT                                   0
#define AFE_IRQ2_MCU_ON_MASK                                  0x1
#define AFE_IRQ2_MCU_ON_MASK_SFT                              (0x1 << 0)

/* AFE_IRQ2_MCU_CFG1 */
#define AFE_IRQ2_CLR_CFG_SFT                                  31
#define AFE_IRQ2_CLR_CFG_MASK                                 0x1
#define AFE_IRQ2_CLR_CFG_MASK_SFT                             (0x1 << 31)
#define AFE_IRQ2_MISS_FLAG_CLR_CFG_SFT                        30
#define AFE_IRQ2_MISS_FLAG_CLR_CFG_MASK                       0x1
#define AFE_IRQ2_MISS_FLAG_CLR_CFG_MASK_SFT                   (0x1 << 30)
#define AFE_IRQ2_MCU_CNT_SFT                                  0
#define AFE_IRQ2_MCU_CNT_MASK                                 0xffffff
#define AFE_IRQ2_MCU_CNT_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ3_MCU_CFG0 */
#define AFE_IRQ3_MCU_DOMAIN_SFT                               9
#define AFE_IRQ3_MCU_DOMAIN_MASK                              0x7
#define AFE_IRQ3_MCU_DOMAIN_MASK_SFT                          (0x7 << 9)
#define AFE_IRQ3_MCU_FS_SFT                                   4
#define AFE_IRQ3_MCU_FS_MASK                                  0x1f
#define AFE_IRQ3_MCU_FS_MASK_SFT                              (0x1f << 4)
#define AFE_IRQ3_MCU_ON_SFT                                   0
#define AFE_IRQ3_MCU_ON_MASK                                  0x1
#define AFE_IRQ3_MCU_ON_MASK_SFT                              (0x1 << 0)

/* AFE_IRQ3_MCU_CFG1 */
#define AFE_IRQ3_CLR_CFG_SFT                                  31
#define AFE_IRQ3_CLR_CFG_MASK                                 0x1
#define AFE_IRQ3_CLR_CFG_MASK_SFT                             (0x1 << 31)
#define AFE_IRQ3_MISS_FLAG_CLR_CFG_SFT                        30
#define AFE_IRQ3_MISS_FLAG_CLR_CFG_MASK                       0x1
#define AFE_IRQ3_MISS_FLAG_CLR_CFG_MASK_SFT                   (0x1 << 30)
#define AFE_IRQ3_MCU_CNT_SFT                                  0
#define AFE_IRQ3_MCU_CNT_MASK                                 0xffffff
#define AFE_IRQ3_MCU_CNT_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ4_MCU_CFG0 */
#define AFE_IRQ4_MCU_DOMAIN_SFT                               9
#define AFE_IRQ4_MCU_DOMAIN_MASK                              0x7
#define AFE_IRQ4_MCU_DOMAIN_MASK_SFT                          (0x7 << 9)
#define AFE_IRQ4_MCU_FS_SFT                                   4
#define AFE_IRQ4_MCU_FS_MASK                                  0x1f
#define AFE_IRQ4_MCU_FS_MASK_SFT                              (0x1f << 4)
#define AFE_IRQ4_MCU_ON_SFT                                   0
#define AFE_IRQ4_MCU_ON_MASK                                  0x1
#define AFE_IRQ4_MCU_ON_MASK_SFT                              (0x1 << 0)

/* AFE_IRQ4_MCU_CFG1 */
#define AFE_IRQ4_CLR_CFG_SFT                                  31
#define AFE_IRQ4_CLR_CFG_MASK                                 0x1
#define AFE_IRQ4_CLR_CFG_MASK_SFT                             (0x1 << 31)
#define AFE_IRQ4_MISS_FLAG_CLR_CFG_SFT                        30
#define AFE_IRQ4_MISS_FLAG_CLR_CFG_MASK                       0x1
#define AFE_IRQ4_MISS_FLAG_CLR_CFG_MASK_SFT                   (0x1 << 30)
#define AFE_IRQ4_MCU_CNT_SFT                                  0
#define AFE_IRQ4_MCU_CNT_MASK                                 0xffffff
#define AFE_IRQ4_MCU_CNT_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ5_MCU_CFG0 */
#define AFE_IRQ5_MCU_DOMAIN_SFT                               9
#define AFE_IRQ5_MCU_DOMAIN_MASK                              0x7
#define AFE_IRQ5_MCU_DOMAIN_MASK_SFT                          (0x7 << 9)
#define AFE_IRQ5_MCU_FS_SFT                                   4
#define AFE_IRQ5_MCU_FS_MASK                                  0x1f
#define AFE_IRQ5_MCU_FS_MASK_SFT                              (0x1f << 4)
#define AFE_IRQ5_MCU_ON_SFT                                   0
#define AFE_IRQ5_MCU_ON_MASK                                  0x1
#define AFE_IRQ5_MCU_ON_MASK_SFT                              (0x1 << 0)

/* AFE_IRQ5_MCU_CFG1 */
#define AFE_IRQ5_CLR_CFG_SFT                                  31
#define AFE_IRQ5_CLR_CFG_MASK                                 0x1
#define AFE_IRQ5_CLR_CFG_MASK_SFT                             (0x1 << 31)
#define AFE_IRQ5_MISS_FLAG_CLR_CFG_SFT                        30
#define AFE_IRQ5_MISS_FLAG_CLR_CFG_MASK                       0x1
#define AFE_IRQ5_MISS_FLAG_CLR_CFG_MASK_SFT                   (0x1 << 30)
#define AFE_IRQ5_MCU_CNT_SFT                                  0
#define AFE_IRQ5_MCU_CNT_MASK                                 0xffffff
#define AFE_IRQ5_MCU_CNT_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ6_MCU_CFG0 */
#define AFE_IRQ6_MCU_DOMAIN_SFT                               9
#define AFE_IRQ6_MCU_DOMAIN_MASK                              0x7
#define AFE_IRQ6_MCU_DOMAIN_MASK_SFT                          (0x7 << 9)
#define AFE_IRQ6_MCU_FS_SFT                                   4
#define AFE_IRQ6_MCU_FS_MASK                                  0x1f
#define AFE_IRQ6_MCU_FS_MASK_SFT                              (0x1f << 4)
#define AFE_IRQ6_MCU_ON_SFT                                   0
#define AFE_IRQ6_MCU_ON_MASK                                  0x1
#define AFE_IRQ6_MCU_ON_MASK_SFT                              (0x1 << 0)

/* AFE_IRQ6_MCU_CFG1 */
#define AFE_IRQ6_CLR_CFG_SFT                                  31
#define AFE_IRQ6_CLR_CFG_MASK                                 0x1
#define AFE_IRQ6_CLR_CFG_MASK_SFT                             (0x1 << 31)
#define AFE_IRQ6_MISS_FLAG_CLR_CFG_SFT                        30
#define AFE_IRQ6_MISS_FLAG_CLR_CFG_MASK                       0x1
#define AFE_IRQ6_MISS_FLAG_CLR_CFG_MASK_SFT                   (0x1 << 30)
#define AFE_IRQ6_MCU_CNT_SFT                                  0
#define AFE_IRQ6_MCU_CNT_MASK                                 0xffffff
#define AFE_IRQ6_MCU_CNT_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ7_MCU_CFG0 */
#define AFE_IRQ7_MCU_DOMAIN_SFT                               9
#define AFE_IRQ7_MCU_DOMAIN_MASK                              0x7
#define AFE_IRQ7_MCU_DOMAIN_MASK_SFT                          (0x7 << 9)
#define AFE_IRQ7_MCU_FS_SFT                                   4
#define AFE_IRQ7_MCU_FS_MASK                                  0x1f
#define AFE_IRQ7_MCU_FS_MASK_SFT                              (0x1f << 4)
#define AFE_IRQ7_MCU_ON_SFT                                   0
#define AFE_IRQ7_MCU_ON_MASK                                  0x1
#define AFE_IRQ7_MCU_ON_MASK_SFT                              (0x1 << 0)

/* AFE_IRQ7_MCU_CFG1 */
#define AFE_IRQ7_CLR_CFG_SFT                                  31
#define AFE_IRQ7_CLR_CFG_MASK                                 0x1
#define AFE_IRQ7_CLR_CFG_MASK_SFT                             (0x1 << 31)
#define AFE_IRQ7_MISS_FLAG_CLR_CFG_SFT                        30
#define AFE_IRQ7_MISS_FLAG_CLR_CFG_MASK                       0x1
#define AFE_IRQ7_MISS_FLAG_CLR_CFG_MASK_SFT                   (0x1 << 30)
#define AFE_IRQ7_MCU_CNT_SFT                                  0
#define AFE_IRQ7_MCU_CNT_MASK                                 0xffffff
#define AFE_IRQ7_MCU_CNT_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ8_MCU_CFG0 */
#define AFE_IRQ8_MCU_DOMAIN_SFT                               9
#define AFE_IRQ8_MCU_DOMAIN_MASK                              0x7
#define AFE_IRQ8_MCU_DOMAIN_MASK_SFT                          (0x7 << 9)
#define AFE_IRQ8_MCU_FS_SFT                                   4
#define AFE_IRQ8_MCU_FS_MASK                                  0x1f
#define AFE_IRQ8_MCU_FS_MASK_SFT                              (0x1f << 4)
#define AFE_IRQ8_MCU_ON_SFT                                   0
#define AFE_IRQ8_MCU_ON_MASK                                  0x1
#define AFE_IRQ8_MCU_ON_MASK_SFT                              (0x1 << 0)

/* AFE_IRQ8_MCU_CFG1 */
#define AFE_IRQ8_CLR_CFG_SFT                                  31
#define AFE_IRQ8_CLR_CFG_MASK                                 0x1
#define AFE_IRQ8_CLR_CFG_MASK_SFT                             (0x1 << 31)
#define AFE_IRQ8_MISS_FLAG_CLR_CFG_SFT                        30
#define AFE_IRQ8_MISS_FLAG_CLR_CFG_MASK                       0x1
#define AFE_IRQ8_MISS_FLAG_CLR_CFG_MASK_SFT                   (0x1 << 30)
#define AFE_IRQ8_MCU_CNT_SFT                                  0
#define AFE_IRQ8_MCU_CNT_MASK                                 0xffffff
#define AFE_IRQ8_MCU_CNT_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ9_MCU_CFG0 */
#define AFE_IRQ9_MCU_DOMAIN_SFT                               9
#define AFE_IRQ9_MCU_DOMAIN_MASK                              0x7
#define AFE_IRQ9_MCU_DOMAIN_MASK_SFT                          (0x7 << 9)
#define AFE_IRQ9_MCU_FS_SFT                                   4
#define AFE_IRQ9_MCU_FS_MASK                                  0x1f
#define AFE_IRQ9_MCU_FS_MASK_SFT                              (0x1f << 4)
#define AFE_IRQ9_MCU_ON_SFT                                   0
#define AFE_IRQ9_MCU_ON_MASK                                  0x1
#define AFE_IRQ9_MCU_ON_MASK_SFT                              (0x1 << 0)

/* AFE_IRQ9_MCU_CFG1 */
#define AFE_IRQ9_CLR_CFG_SFT                                  31
#define AFE_IRQ9_CLR_CFG_MASK                                 0x1
#define AFE_IRQ9_CLR_CFG_MASK_SFT                             (0x1 << 31)
#define AFE_IRQ9_MISS_FLAG_CLR_CFG_SFT                        30
#define AFE_IRQ9_MISS_FLAG_CLR_CFG_MASK                       0x1
#define AFE_IRQ9_MISS_FLAG_CLR_CFG_MASK_SFT                   (0x1 << 30)
#define AFE_IRQ9_MCU_CNT_SFT                                  0
#define AFE_IRQ9_MCU_CNT_MASK                                 0xffffff
#define AFE_IRQ9_MCU_CNT_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ10_MCU_CFG0 */
#define AFE_IRQ10_MCU_DOMAIN_SFT                              9
#define AFE_IRQ10_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ10_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ10_MCU_FS_SFT                                  4
#define AFE_IRQ10_MCU_FS_MASK                                 0x1f
#define AFE_IRQ10_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ10_MCU_ON_SFT                                  0
#define AFE_IRQ10_MCU_ON_MASK                                 0x1
#define AFE_IRQ10_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ10_MCU_CFG1 */
#define AFE_IRQ10_CLR_CFG_SFT                                 31
#define AFE_IRQ10_CLR_CFG_MASK                                0x1
#define AFE_IRQ10_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ10_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ10_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ10_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ10_MCU_CNT_SFT                                 0
#define AFE_IRQ10_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ10_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ11_MCU_CFG0 */
#define AFE_IRQ11_MCU_DOMAIN_SFT                              9
#define AFE_IRQ11_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ11_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ11_MCU_FS_SFT                                  4
#define AFE_IRQ11_MCU_FS_MASK                                 0x1f
#define AFE_IRQ11_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ11_MCU_ON_SFT                                  0
#define AFE_IRQ11_MCU_ON_MASK                                 0x1
#define AFE_IRQ11_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ11_MCU_CFG1 */
#define AFE_IRQ11_CLR_CFG_SFT                                 31
#define AFE_IRQ11_CLR_CFG_MASK                                0x1
#define AFE_IRQ11_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ11_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ11_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ11_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ11_MCU_CNT_SFT                                 0
#define AFE_IRQ11_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ11_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ12_MCU_CFG0 */
#define AFE_IRQ12_MCU_DOMAIN_SFT                              9
#define AFE_IRQ12_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ12_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ12_MCU_FS_SFT                                  4
#define AFE_IRQ12_MCU_FS_MASK                                 0x1f
#define AFE_IRQ12_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ12_MCU_ON_SFT                                  0
#define AFE_IRQ12_MCU_ON_MASK                                 0x1
#define AFE_IRQ12_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ12_MCU_CFG1 */
#define AFE_IRQ12_CLR_CFG_SFT                                 31
#define AFE_IRQ12_CLR_CFG_MASK                                0x1
#define AFE_IRQ12_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ12_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ12_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ12_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ12_MCU_CNT_SFT                                 0
#define AFE_IRQ12_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ12_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ13_MCU_CFG0 */
#define AFE_IRQ13_MCU_DOMAIN_SFT                              9
#define AFE_IRQ13_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ13_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ13_MCU_FS_SFT                                  4
#define AFE_IRQ13_MCU_FS_MASK                                 0x1f
#define AFE_IRQ13_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ13_MCU_ON_SFT                                  0
#define AFE_IRQ13_MCU_ON_MASK                                 0x1
#define AFE_IRQ13_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ13_MCU_CFG1 */
#define AFE_IRQ13_CLR_CFG_SFT                                 31
#define AFE_IRQ13_CLR_CFG_MASK                                0x1
#define AFE_IRQ13_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ13_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ13_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ13_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ13_MCU_CNT_SFT                                 0
#define AFE_IRQ13_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ13_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ14_MCU_CFG0 */
#define AFE_IRQ14_MCU_DOMAIN_SFT                              9
#define AFE_IRQ14_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ14_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ14_MCU_FS_SFT                                  4
#define AFE_IRQ14_MCU_FS_MASK                                 0x1f
#define AFE_IRQ14_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ14_MCU_ON_SFT                                  0
#define AFE_IRQ14_MCU_ON_MASK                                 0x1
#define AFE_IRQ14_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ14_MCU_CFG1 */
#define AFE_IRQ14_CLR_CFG_SFT                                 31
#define AFE_IRQ14_CLR_CFG_MASK                                0x1
#define AFE_IRQ14_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ14_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ14_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ14_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ14_MCU_CNT_SFT                                 0
#define AFE_IRQ14_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ14_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ15_MCU_CFG0 */
#define AFE_IRQ15_MCU_DOMAIN_SFT                              9
#define AFE_IRQ15_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ15_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ15_MCU_FS_SFT                                  4
#define AFE_IRQ15_MCU_FS_MASK                                 0x1f
#define AFE_IRQ15_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ15_MCU_ON_SFT                                  0
#define AFE_IRQ15_MCU_ON_MASK                                 0x1
#define AFE_IRQ15_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ15_MCU_CFG1 */
#define AFE_IRQ15_CLR_CFG_SFT                                 31
#define AFE_IRQ15_CLR_CFG_MASK                                0x1
#define AFE_IRQ15_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ15_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ15_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ15_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ15_MCU_CNT_SFT                                 0
#define AFE_IRQ15_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ15_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ16_MCU_CFG0 */
#define AFE_IRQ16_MCU_DOMAIN_SFT                              9
#define AFE_IRQ16_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ16_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ16_MCU_FS_SFT                                  4
#define AFE_IRQ16_MCU_FS_MASK                                 0x1f
#define AFE_IRQ16_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ16_MCU_ON_SFT                                  0
#define AFE_IRQ16_MCU_ON_MASK                                 0x1
#define AFE_IRQ16_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ16_MCU_CFG1 */
#define AFE_IRQ16_CLR_CFG_SFT                                 31
#define AFE_IRQ16_CLR_CFG_MASK                                0x1
#define AFE_IRQ16_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ16_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ16_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ16_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ16_MCU_CNT_SFT                                 0
#define AFE_IRQ16_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ16_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ17_MCU_CFG0 */
#define AFE_IRQ17_MCU_DOMAIN_SFT                              9
#define AFE_IRQ17_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ17_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ17_MCU_FS_SFT                                  4
#define AFE_IRQ17_MCU_FS_MASK                                 0x1f
#define AFE_IRQ17_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ17_MCU_ON_SFT                                  0
#define AFE_IRQ17_MCU_ON_MASK                                 0x1
#define AFE_IRQ17_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ17_MCU_CFG1 */
#define AFE_IRQ17_CLR_CFG_SFT                                 31
#define AFE_IRQ17_CLR_CFG_MASK                                0x1
#define AFE_IRQ17_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ17_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ17_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ17_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ17_MCU_CNT_SFT                                 0
#define AFE_IRQ17_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ17_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ18_MCU_CFG0 */
#define AFE_IRQ18_MCU_DOMAIN_SFT                              9
#define AFE_IRQ18_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ18_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ18_MCU_FS_SFT                                  4
#define AFE_IRQ18_MCU_FS_MASK                                 0x1f
#define AFE_IRQ18_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ18_MCU_ON_SFT                                  0
#define AFE_IRQ18_MCU_ON_MASK                                 0x1
#define AFE_IRQ18_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ18_MCU_CFG1 */
#define AFE_IRQ18_CLR_CFG_SFT                                 31
#define AFE_IRQ18_CLR_CFG_MASK                                0x1
#define AFE_IRQ18_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ18_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ18_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ18_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ18_MCU_CNT_SFT                                 0
#define AFE_IRQ18_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ18_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ19_MCU_CFG0 */
#define AFE_IRQ19_MCU_DOMAIN_SFT                              9
#define AFE_IRQ19_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ19_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ19_MCU_FS_SFT                                  4
#define AFE_IRQ19_MCU_FS_MASK                                 0x1f
#define AFE_IRQ19_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ19_MCU_ON_SFT                                  0
#define AFE_IRQ19_MCU_ON_MASK                                 0x1
#define AFE_IRQ19_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ19_MCU_CFG1 */
#define AFE_IRQ19_CLR_CFG_SFT                                 31
#define AFE_IRQ19_CLR_CFG_MASK                                0x1
#define AFE_IRQ19_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ19_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ19_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ19_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ19_MCU_CNT_SFT                                 0
#define AFE_IRQ19_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ19_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ20_MCU_CFG0 */
#define AFE_IRQ20_MCU_DOMAIN_SFT                              9
#define AFE_IRQ20_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ20_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ20_MCU_FS_SFT                                  4
#define AFE_IRQ20_MCU_FS_MASK                                 0x1f
#define AFE_IRQ20_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ20_MCU_ON_SFT                                  0
#define AFE_IRQ20_MCU_ON_MASK                                 0x1
#define AFE_IRQ20_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ20_MCU_CFG1 */
#define AFE_IRQ20_CLR_CFG_SFT                                 31
#define AFE_IRQ20_CLR_CFG_MASK                                0x1
#define AFE_IRQ20_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ20_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ20_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ20_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ20_MCU_CNT_SFT                                 0
#define AFE_IRQ20_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ20_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ21_MCU_CFG0 */
#define AFE_IRQ21_MCU_DOMAIN_SFT                              9
#define AFE_IRQ21_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ21_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ21_MCU_FS_SFT                                  4
#define AFE_IRQ21_MCU_FS_MASK                                 0x1f
#define AFE_IRQ21_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ21_MCU_ON_SFT                                  0
#define AFE_IRQ21_MCU_ON_MASK                                 0x1
#define AFE_IRQ21_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ21_MCU_CFG1 */
#define AFE_IRQ21_CLR_CFG_SFT                                 31
#define AFE_IRQ21_CLR_CFG_MASK                                0x1
#define AFE_IRQ21_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ21_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ21_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ21_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ21_MCU_CNT_SFT                                 0
#define AFE_IRQ21_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ21_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ22_MCU_CFG0 */
#define AFE_IRQ22_MCU_DOMAIN_SFT                              9
#define AFE_IRQ22_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ22_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ22_MCU_FS_SFT                                  4
#define AFE_IRQ22_MCU_FS_MASK                                 0x1f
#define AFE_IRQ22_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ22_MCU_ON_SFT                                  0
#define AFE_IRQ22_MCU_ON_MASK                                 0x1
#define AFE_IRQ22_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ22_MCU_CFG1 */
#define AFE_IRQ22_CLR_CFG_SFT                                 31
#define AFE_IRQ22_CLR_CFG_MASK                                0x1
#define AFE_IRQ22_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ22_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ22_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ22_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ22_MCU_CNT_SFT                                 0
#define AFE_IRQ22_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ22_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ23_MCU_CFG0 */
#define AFE_IRQ23_MCU_DOMAIN_SFT                              9
#define AFE_IRQ23_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ23_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ23_MCU_FS_SFT                                  4
#define AFE_IRQ23_MCU_FS_MASK                                 0x1f
#define AFE_IRQ23_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ23_MCU_ON_SFT                                  0
#define AFE_IRQ23_MCU_ON_MASK                                 0x1
#define AFE_IRQ23_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ23_MCU_CFG1 */
#define AFE_IRQ23_CLR_CFG_SFT                                 31
#define AFE_IRQ23_CLR_CFG_MASK                                0x1
#define AFE_IRQ23_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ23_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ23_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ23_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ23_MCU_CNT_SFT                                 0
#define AFE_IRQ23_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ23_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ24_MCU_CFG0 */
#define AFE_IRQ24_MCU_DOMAIN_SFT                              9
#define AFE_IRQ24_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ24_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ24_MCU_FS_SFT                                  4
#define AFE_IRQ24_MCU_FS_MASK                                 0x1f
#define AFE_IRQ24_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ24_MCU_ON_SFT                                  0
#define AFE_IRQ24_MCU_ON_MASK                                 0x1
#define AFE_IRQ24_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ24_MCU_CFG1 */
#define AFE_IRQ24_CLR_CFG_SFT                                 31
#define AFE_IRQ24_CLR_CFG_MASK                                0x1
#define AFE_IRQ24_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ24_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ24_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ24_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ24_MCU_CNT_SFT                                 0
#define AFE_IRQ24_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ24_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ25_MCU_CFG0 */
#define AFE_IRQ25_MCU_DOMAIN_SFT                              9
#define AFE_IRQ25_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ25_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ25_MCU_FS_SFT                                  4
#define AFE_IRQ25_MCU_FS_MASK                                 0x1f
#define AFE_IRQ25_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ25_MCU_ON_SFT                                  0
#define AFE_IRQ25_MCU_ON_MASK                                 0x1
#define AFE_IRQ25_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ25_MCU_CFG1 */
#define AFE_IRQ25_CLR_CFG_SFT                                 31
#define AFE_IRQ25_CLR_CFG_MASK                                0x1
#define AFE_IRQ25_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ25_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ25_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ25_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ25_MCU_CNT_SFT                                 0
#define AFE_IRQ25_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ25_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ26_MCU_CFG0 */
#define AFE_IRQ26_MCU_DOMAIN_SFT                              9
#define AFE_IRQ26_MCU_DOMAIN_MASK                             0x7
#define AFE_IRQ26_MCU_DOMAIN_MASK_SFT                         (0x7 << 9)
#define AFE_IRQ26_MCU_FS_SFT                                  4
#define AFE_IRQ26_MCU_FS_MASK                                 0x1f
#define AFE_IRQ26_MCU_FS_MASK_SFT                             (0x1f << 4)
#define AFE_IRQ26_MCU_ON_SFT                                  0
#define AFE_IRQ26_MCU_ON_MASK                                 0x1
#define AFE_IRQ26_MCU_ON_MASK_SFT                             (0x1 << 0)

/* AFE_IRQ26_MCU_CFG1 */
#define AFE_IRQ26_CLR_CFG_SFT                                 31
#define AFE_IRQ26_CLR_CFG_MASK                                0x1
#define AFE_IRQ26_CLR_CFG_MASK_SFT                            (0x1 << 31)
#define AFE_IRQ26_MISS_FLAG_CLR_CFG_SFT                       30
#define AFE_IRQ26_MISS_FLAG_CLR_CFG_MASK                      0x1
#define AFE_IRQ26_MISS_FLAG_CLR_CFG_MASK_SFT                  (0x1 << 30)
#define AFE_IRQ26_MCU_CNT_SFT                                 0
#define AFE_IRQ26_MCU_CNT_MASK                                0xffffff
#define AFE_IRQ26_MCU_CNT_MASK_SFT                            (0xffffff << 0)

/* AFE_CUSTOM_IRQ0_MCU_CFG0 */
#define AFE_CUSTOM_IRQ0_MCU_ON_SFT                            0
#define AFE_CUSTOM_IRQ0_MCU_ON_MASK                           0x1
#define AFE_CUSTOM_IRQ0_MCU_ON_MASK_SFT                       (0x1 << 0)

/* AFE_IRQ_MCU_MON0 */
#define AFE_IRQ26_MISS_FLAG_SFT                               26
#define AFE_IRQ26_MISS_FLAG_MASK                              0x1
#define AFE_IRQ26_MISS_FLAG_MASK_SFT                          (0x1 << 26)
#define AFE_IRQ25_MISS_FLAG_SFT                               25
#define AFE_IRQ25_MISS_FLAG_MASK                              0x1
#define AFE_IRQ25_MISS_FLAG_MASK_SFT                          (0x1 << 25)
#define AFE_IRQ24_MISS_FLAG_SFT                               24
#define AFE_IRQ24_MISS_FLAG_MASK                              0x1
#define AFE_IRQ24_MISS_FLAG_MASK_SFT                          (0x1 << 24)
#define AFE_IRQ23_MISS_FLAG_SFT                               23
#define AFE_IRQ23_MISS_FLAG_MASK                              0x1
#define AFE_IRQ23_MISS_FLAG_MASK_SFT                          (0x1 << 23)
#define AFE_IRQ22_MISS_FLAG_SFT                               22
#define AFE_IRQ22_MISS_FLAG_MASK                              0x1
#define AFE_IRQ22_MISS_FLAG_MASK_SFT                          (0x1 << 22)
#define AFE_IRQ21_MISS_FLAG_SFT                               21
#define AFE_IRQ21_MISS_FLAG_MASK                              0x1
#define AFE_IRQ21_MISS_FLAG_MASK_SFT                          (0x1 << 21)
#define AFE_IRQ20_MISS_FLAG_SFT                               20
#define AFE_IRQ20_MISS_FLAG_MASK                              0x1
#define AFE_IRQ20_MISS_FLAG_MASK_SFT                          (0x1 << 20)
#define AFE_IRQ19_MISS_FLAG_SFT                               19
#define AFE_IRQ19_MISS_FLAG_MASK                              0x1
#define AFE_IRQ19_MISS_FLAG_MASK_SFT                          (0x1 << 19)
#define AFE_IRQ18_MISS_FLAG_SFT                               18
#define AFE_IRQ18_MISS_FLAG_MASK                              0x1
#define AFE_IRQ18_MISS_FLAG_MASK_SFT                          (0x1 << 18)
#define AFE_IRQ17_MISS_FLAG_SFT                               17
#define AFE_IRQ17_MISS_FLAG_MASK                              0x1
#define AFE_IRQ17_MISS_FLAG_MASK_SFT                          (0x1 << 17)
#define AFE_IRQ16_MISS_FLAG_SFT                               16
#define AFE_IRQ16_MISS_FLAG_MASK                              0x1
#define AFE_IRQ16_MISS_FLAG_MASK_SFT                          (0x1 << 16)
#define AFE_IRQ15_MISS_FLAG_SFT                               15
#define AFE_IRQ15_MISS_FLAG_MASK                              0x1
#define AFE_IRQ15_MISS_FLAG_MASK_SFT                          (0x1 << 15)
#define AFE_IRQ14_MISS_FLAG_SFT                               14
#define AFE_IRQ14_MISS_FLAG_MASK                              0x1
#define AFE_IRQ14_MISS_FLAG_MASK_SFT                          (0x1 << 14)
#define AFE_IRQ13_MISS_FLAG_SFT                               13
#define AFE_IRQ13_MISS_FLAG_MASK                              0x1
#define AFE_IRQ13_MISS_FLAG_MASK_SFT                          (0x1 << 13)
#define AFE_IRQ12_MISS_FLAG_SFT                               12
#define AFE_IRQ12_MISS_FLAG_MASK                              0x1
#define AFE_IRQ12_MISS_FLAG_MASK_SFT                          (0x1 << 12)
#define AFE_IRQ11_MISS_FLAG_SFT                               11
#define AFE_IRQ11_MISS_FLAG_MASK                              0x1
#define AFE_IRQ11_MISS_FLAG_MASK_SFT                          (0x1 << 11)
#define AFE_IRQ10_MISS_FLAG_SFT                               10
#define AFE_IRQ10_MISS_FLAG_MASK                              0x1
#define AFE_IRQ10_MISS_FLAG_MASK_SFT                          (0x1 << 10)
#define AFE_IRQ9_MISS_FLAG_SFT                                9
#define AFE_IRQ9_MISS_FLAG_MASK                               0x1
#define AFE_IRQ9_MISS_FLAG_MASK_SFT                           (0x1 << 9)
#define AFE_IRQ8_MISS_FLAG_SFT                                8
#define AFE_IRQ8_MISS_FLAG_MASK                               0x1
#define AFE_IRQ8_MISS_FLAG_MASK_SFT                           (0x1 << 8)
#define AFE_IRQ7_MISS_FLAG_SFT                                7
#define AFE_IRQ7_MISS_FLAG_MASK                               0x1
#define AFE_IRQ7_MISS_FLAG_MASK_SFT                           (0x1 << 7)
#define AFE_IRQ6_MISS_FLAG_SFT                                6
#define AFE_IRQ6_MISS_FLAG_MASK                               0x1
#define AFE_IRQ6_MISS_FLAG_MASK_SFT                           (0x1 << 6)
#define AFE_IRQ5_MISS_FLAG_SFT                                5
#define AFE_IRQ5_MISS_FLAG_MASK                               0x1
#define AFE_IRQ5_MISS_FLAG_MASK_SFT                           (0x1 << 5)
#define AFE_IRQ4_MISS_FLAG_SFT                                4
#define AFE_IRQ4_MISS_FLAG_MASK                               0x1
#define AFE_IRQ4_MISS_FLAG_MASK_SFT                           (0x1 << 4)
#define AFE_IRQ3_MISS_FLAG_SFT                                3
#define AFE_IRQ3_MISS_FLAG_MASK                               0x1
#define AFE_IRQ3_MISS_FLAG_MASK_SFT                           (0x1 << 3)
#define AFE_IRQ2_MISS_FLAG_SFT                                2
#define AFE_IRQ2_MISS_FLAG_MASK                               0x1
#define AFE_IRQ2_MISS_FLAG_MASK_SFT                           (0x1 << 2)
#define AFE_IRQ1_MISS_FLAG_SFT                                1
#define AFE_IRQ1_MISS_FLAG_MASK                               0x1
#define AFE_IRQ1_MISS_FLAG_MASK_SFT                           (0x1 << 1)
#define AFE_IRQ0_MISS_FLAG_SFT                                0
#define AFE_IRQ0_MISS_FLAG_MASK                               0x1
#define AFE_IRQ0_MISS_FLAG_MASK_SFT                           (0x1 << 0)

/* AFE_IRQ_MCU_MON1 */
#define AFE_CUSTOM_IRQ21_MISS_FLAG_SFT                        21
#define AFE_CUSTOM_IRQ21_MISS_FLAG_MASK                       0x1
#define AFE_CUSTOM_IRQ21_MISS_FLAG_MASK_SFT                   (0x1 << 21)
#define AFE_CUSTOM_IRQ20_MISS_FLAG_SFT                        20
#define AFE_CUSTOM_IRQ20_MISS_FLAG_MASK                       0x1
#define AFE_CUSTOM_IRQ20_MISS_FLAG_MASK_SFT                   (0x1 << 20)
#define AFE_CUSTOM_IRQ19_MISS_FLAG_SFT                        19
#define AFE_CUSTOM_IRQ19_MISS_FLAG_MASK                       0x1
#define AFE_CUSTOM_IRQ19_MISS_FLAG_MASK_SFT                   (0x1 << 19)
#define AFE_CUSTOM_IRQ18_MISS_FLAG_SFT                        18
#define AFE_CUSTOM_IRQ18_MISS_FLAG_MASK                       0x1
#define AFE_CUSTOM_IRQ18_MISS_FLAG_MASK_SFT                   (0x1 << 18)
#define AFE_CUSTOM_IRQ17_MISS_FLAG_SFT                        17
#define AFE_CUSTOM_IRQ17_MISS_FLAG_MASK                       0x1
#define AFE_CUSTOM_IRQ17_MISS_FLAG_MASK_SFT                   (0x1 << 17)
#define AFE_CUSTOM_IRQ16_MISS_FLAG_SFT                        16
#define AFE_CUSTOM_IRQ16_MISS_FLAG_MASK                       0x1
#define AFE_CUSTOM_IRQ16_MISS_FLAG_MASK_SFT                   (0x1 << 16)
#define AFE_CUSTOM_IRQ9_MISS_FLAG_SFT                         9
#define AFE_CUSTOM_IRQ9_MISS_FLAG_MASK                        0x1
#define AFE_CUSTOM_IRQ9_MISS_FLAG_MASK_SFT                    (0x1 << 9)
#define AFE_CUSTOM_IRQ8_MISS_FLAG_SFT                         8
#define AFE_CUSTOM_IRQ8_MISS_FLAG_MASK                        0x1
#define AFE_CUSTOM_IRQ8_MISS_FLAG_MASK_SFT                    (0x1 << 8)
#define AFE_CUSTOM_IRQ7_MISS_FLAG_SFT                         7
#define AFE_CUSTOM_IRQ7_MISS_FLAG_MASK                        0x1
#define AFE_CUSTOM_IRQ7_MISS_FLAG_MASK_SFT                    (0x1 << 7)
#define AFE_CUSTOM_IRQ6_MISS_FLAG_SFT                         6
#define AFE_CUSTOM_IRQ6_MISS_FLAG_MASK                        0x1
#define AFE_CUSTOM_IRQ6_MISS_FLAG_MASK_SFT                    (0x1 << 6)
#define AFE_CUSTOM_IRQ5_MISS_FLAG_SFT                         5
#define AFE_CUSTOM_IRQ5_MISS_FLAG_MASK                        0x1
#define AFE_CUSTOM_IRQ5_MISS_FLAG_MASK_SFT                    (0x1 << 5)
#define AFE_CUSTOM_IRQ4_MISS_FLAG_SFT                         4
#define AFE_CUSTOM_IRQ4_MISS_FLAG_MASK                        0x1
#define AFE_CUSTOM_IRQ4_MISS_FLAG_MASK_SFT                    (0x1 << 4)
#define AFE_CUSTOM_IRQ3_MISS_FLAG_SFT                         3
#define AFE_CUSTOM_IRQ3_MISS_FLAG_MASK                        0x1
#define AFE_CUSTOM_IRQ3_MISS_FLAG_MASK_SFT                    (0x1 << 3)
#define AFE_CUSTOM_IRQ2_MISS_FLAG_SFT                         2
#define AFE_CUSTOM_IRQ2_MISS_FLAG_MASK                        0x1
#define AFE_CUSTOM_IRQ2_MISS_FLAG_MASK_SFT                    (0x1 << 2)
#define AFE_CUSTOM_IRQ1_MISS_FLAG_SFT                         1
#define AFE_CUSTOM_IRQ1_MISS_FLAG_MASK                        0x1
#define AFE_CUSTOM_IRQ1_MISS_FLAG_MASK_SFT                    (0x1 << 1)
#define AFE_CUSTOM_IRQ0_MISS_FLAG_SFT                         0
#define AFE_CUSTOM_IRQ0_MISS_FLAG_MASK                        0x1
#define AFE_CUSTOM_IRQ0_MISS_FLAG_MASK_SFT                    (0x1 << 0)

/* AFE_IRQ_MCU_MON2 */
#define AFE_IRQ_B_R_CNT_SFT                                   8
#define AFE_IRQ_B_R_CNT_MASK                                  0xff
#define AFE_IRQ_B_R_CNT_MASK_SFT                              (0xff << 8)
#define AFE_IRQ_B_F_CNT_SFT                                   0
#define AFE_IRQ_B_F_CNT_MASK                                  0xff
#define AFE_IRQ_B_F_CNT_MASK_SFT                              (0xff << 0)

/* AFE_IRQ0_CNT_MON */
#define AFE_IRQ0_CNT_MON_SFT                                  0
#define AFE_IRQ0_CNT_MON_MASK                                 0xffffff
#define AFE_IRQ0_CNT_MON_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ1_CNT_MON */
#define AFE_IRQ1_CNT_MON_SFT                                  0
#define AFE_IRQ1_CNT_MON_MASK                                 0xffffff
#define AFE_IRQ1_CNT_MON_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ2_CNT_MON */
#define AFE_IRQ2_CNT_MON_SFT                                  0
#define AFE_IRQ2_CNT_MON_MASK                                 0xffffff
#define AFE_IRQ2_CNT_MON_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ3_CNT_MON */
#define AFE_IRQ3_CNT_MON_SFT                                  0
#define AFE_IRQ3_CNT_MON_MASK                                 0xffffff
#define AFE_IRQ3_CNT_MON_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ4_CNT_MON */
#define AFE_IRQ4_CNT_MON_SFT                                  0
#define AFE_IRQ4_CNT_MON_MASK                                 0xffffff
#define AFE_IRQ4_CNT_MON_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ5_CNT_MON */
#define AFE_IRQ5_CNT_MON_SFT                                  0
#define AFE_IRQ5_CNT_MON_MASK                                 0xffffff
#define AFE_IRQ5_CNT_MON_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ6_CNT_MON */
#define AFE_IRQ6_CNT_MON_SFT                                  0
#define AFE_IRQ6_CNT_MON_MASK                                 0xffffff
#define AFE_IRQ6_CNT_MON_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ7_CNT_MON */
#define AFE_IRQ7_CNT_MON_SFT                                  0
#define AFE_IRQ7_CNT_MON_MASK                                 0xffffff
#define AFE_IRQ7_CNT_MON_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ8_CNT_MON */
#define AFE_IRQ8_CNT_MON_SFT                                  0
#define AFE_IRQ8_CNT_MON_MASK                                 0xffffff
#define AFE_IRQ8_CNT_MON_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ9_CNT_MON */
#define AFE_IRQ9_CNT_MON_SFT                                  0
#define AFE_IRQ9_CNT_MON_MASK                                 0xffffff
#define AFE_IRQ9_CNT_MON_MASK_SFT                             (0xffffff << 0)

/* AFE_IRQ10_CNT_MON */
#define AFE_IRQ10_CNT_MON_SFT                                 0
#define AFE_IRQ10_CNT_MON_MASK                                0xffffff
#define AFE_IRQ10_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ11_CNT_MON */
#define AFE_IRQ11_CNT_MON_SFT                                 0
#define AFE_IRQ11_CNT_MON_MASK                                0xffffff
#define AFE_IRQ11_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ12_CNT_MON */
#define AFE_IRQ12_CNT_MON_SFT                                 0
#define AFE_IRQ12_CNT_MON_MASK                                0xffffff
#define AFE_IRQ12_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ13_CNT_MON */
#define AFE_IRQ13_CNT_MON_SFT                                 0
#define AFE_IRQ13_CNT_MON_MASK                                0xffffff
#define AFE_IRQ13_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ14_CNT_MON */
#define AFE_IRQ14_CNT_MON_SFT                                 0
#define AFE_IRQ14_CNT_MON_MASK                                0xffffff
#define AFE_IRQ14_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ15_CNT_MON */
#define AFE_IRQ15_CNT_MON_SFT                                 0
#define AFE_IRQ15_CNT_MON_MASK                                0xffffff
#define AFE_IRQ15_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ16_CNT_MON */
#define AFE_IRQ16_CNT_MON_SFT                                 0
#define AFE_IRQ16_CNT_MON_MASK                                0xffffff
#define AFE_IRQ16_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ17_CNT_MON */
#define AFE_IRQ17_CNT_MON_SFT                                 0
#define AFE_IRQ17_CNT_MON_MASK                                0xffffff
#define AFE_IRQ17_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ18_CNT_MON */
#define AFE_IRQ18_CNT_MON_SFT                                 0
#define AFE_IRQ18_CNT_MON_MASK                                0xffffff
#define AFE_IRQ18_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ19_CNT_MON */
#define AFE_IRQ19_CNT_MON_SFT                                 0
#define AFE_IRQ19_CNT_MON_MASK                                0xffffff
#define AFE_IRQ19_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ20_CNT_MON */
#define AFE_IRQ20_CNT_MON_SFT                                 0
#define AFE_IRQ20_CNT_MON_MASK                                0xffffff
#define AFE_IRQ20_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ21_CNT_MON */
#define AFE_IRQ21_CNT_MON_SFT                                 0
#define AFE_IRQ21_CNT_MON_MASK                                0xffffff
#define AFE_IRQ21_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ22_CNT_MON */
#define AFE_IRQ22_CNT_MON_SFT                                 0
#define AFE_IRQ22_CNT_MON_MASK                                0xffffff
#define AFE_IRQ22_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ23_CNT_MON */
#define AFE_IRQ23_CNT_MON_SFT                                 0
#define AFE_IRQ23_CNT_MON_MASK                                0xffffff
#define AFE_IRQ23_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ24_CNT_MON */
#define AFE_IRQ24_CNT_MON_SFT                                 0
#define AFE_IRQ24_CNT_MON_MASK                                0xffffff
#define AFE_IRQ24_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ25_CNT_MON */
#define AFE_IRQ25_CNT_MON_SFT                                 0
#define AFE_IRQ25_CNT_MON_MASK                                0xffffff
#define AFE_IRQ25_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_IRQ26_CNT_MON */
#define AFE_IRQ26_CNT_MON_SFT                                 0
#define AFE_IRQ26_CNT_MON_MASK                                0xffffff
#define AFE_IRQ26_CNT_MON_MASK_SFT                            (0xffffff << 0)

/* AFE_CUSTOM_IRQ0_CNT_MON */
#define AFE_CUSTOM_IRQ0_CNT_MON_SFT                           0
#define AFE_CUSTOM_IRQ0_CNT_MON_MASK                          0xffffff
#define AFE_CUSTOM_IRQ0_CNT_MON_MASK_SFT                      (0xffffff << 0)

/* AFE_CUSTOM_IRQ0_MCU_CFG1 */
#define AFE_CUSTOM_IRQ0_CLR_CFG_SFT                           31
#define AFE_CUSTOM_IRQ0_CLR_CFG_MASK                          0x1
#define AFE_CUSTOM_IRQ0_CLR_CFG_MASK_SFT                      (0x1 << 31)
#define AFE_CUSTOM_IRQ0_MISS_FLAG_CLR_CFG_SFT                 30
#define AFE_CUSTOM_IRQ0_MISS_FLAG_CLR_CFG_MASK                0x1
#define AFE_CUSTOM_IRQ0_MISS_FLAG_CLR_CFG_MASK_SFT            (0x1 << 30)
#define AFE_CUSTOM_IRQ0_MCU_CNT_SFT                           0
#define AFE_CUSTOM_IRQ0_MCU_CNT_MASK                          0xffffff
#define AFE_CUSTOM_IRQ0_MCU_CNT_MASK_SFT                      (0xffffff << 0)

/* AFE_GAIN0_CON1_R */
/* AFE_GAIN1_CON1_R */
/* AFE_GAIN2_CON1_R */
/* AFE_GAIN3_CON1_R */
#define GAIN_TARGET_R_SFT                                    0
#define GAIN_TARGET_R_MASK                                   0xffffffff
#define GAIN_TARGET_R_MASK_SFT                               (0xffffffff << 0)

/* AFE_GAIN0_CON1_L */
/* AFE_GAIN1_CON1_L */
/* AFE_GAIN2_CON1_L */
/* AFE_GAIN3_CON1_L */
#define GAIN_TARGET_L_SFT                                    0
#define GAIN_TARGET_L_MASK                                   0xffffffff
#define GAIN_TARGET_L_MASK_SFT                               (0xffffffff << 0)

/* AFE_GAIN0_CON2 */
#define GAIN0_DOWN_STEP_SFT                                   0
#define GAIN0_DOWN_STEP_MASK                                  0x3fffff
#define GAIN0_DOWN_STEP_MASK_SFT                              (0x3fffff << 0)

/* AFE_GAIN0_CON3 */
#define GAIN0_UP_STEP_SFT                                     0
#define GAIN0_UP_STEP_MASK                                    0x3fffff
#define GAIN0_UP_STEP_MASK_SFT                                (0x3fffff << 0)

/* AFE_GAIN0_CUR_R */
/* AFE_GAIN1_CUR_R */
/* AFE_GAIN2_CUR_R */
/* AFE_GAIN3_CUR_R */
#define AFE_GAIN_CUR_R_SFT                                   0
#define AFE_GAIN_CUR_R_MASK                                  0xffffffff
#define AFE_GAIN_CUR_R_MASK_SFT                              (0xffffffff << 0)

/* AFE_GAIN0_CUR_L */
/* AFE_GAIN1_CUR_L */
/* AFE_GAIN2_CUR_L */
/* AFE_GAIN3_CUR_L */
#define AFE_GAIN_CUR_L_SFT                                   0
#define AFE_GAIN_CUR_L_MASK                                  0xffffffff
#define AFE_GAIN_CUR_L_MASK_SFT                              (0xffffffff << 0)

/* AFE_GAIN0_CON0 */
/* AFE_GAIN1_CON0 */
/* AFE_GAIN2_CON0 */
/* AFE_GAIN3_CON0 */
#define GAIN_TARGET_SYNC_ON_SFT                              24
#define GAIN_TARGET_SYNC_ON_MASK                             0x1
#define GAIN_TARGET_SYNC_ON_MASK_SFT                         (0x1 << 24)
#define GAIN_TIMEOUT_SFT                                     18
#define GAIN_TIMEOUT_MASK                                    0x3f
#define GAIN_TIMEOUT_MASK_SFT                                (0x3f << 18)
#define GAIN_TRIG_SFT                                        17
#define GAIN_TRIG_MASK                                       0x1
#define GAIN_TRIG_MASK_SFT                                   (0x1 << 17)
#define GAIN_ON_SFT                                          16
#define GAIN_ON_MASK                                         0x1
#define GAIN_ON_MASK_SFT                                     (0x1 << 16)
#define GAIN_SAMPLE_PER_STEP_SFT                             8
#define GAIN_SAMPLE_PER_STEP_MASK                            0xff
#define GAIN_SAMPLE_PER_STEP_MASK_SFT                        (0xff << 8)
#define GAIN_SEL_DOMAIN_SFT                                  5
#define GAIN_SEL_DOMAIN_MASK                                 0x7
#define GAIN_SEL_DOMAIN_MASK_SFT                             (0x7 << 5)
#define GAIN_SEL_FS_SFT                                      0
#define GAIN_SEL_FS_MASK                                     0x1f
#define GAIN_SEL_FS_MASK_SFT                                 (0x1f << 0)

/* AFE_GAIN1_CON2 */
#define GAIN1_DOWN_STEP_SFT                                   0
#define GAIN1_DOWN_STEP_MASK                                  0x3fffff
#define GAIN1_DOWN_STEP_MASK_SFT                              (0x3fffff << 0)

/* AFE_GAIN1_CON3 */
#define GAIN1_UP_STEP_SFT                                     0
#define GAIN1_UP_STEP_MASK                                    0x3fffff
#define GAIN1_UP_STEP_MASK_SFT                                (0x3fffff << 0)

/* AFE_GAIN2_CON2 */
#define GAIN2_DOWN_STEP_SFT                                   0
#define GAIN2_DOWN_STEP_MASK                                  0x3fffff
#define GAIN2_DOWN_STEP_MASK_SFT                              (0x3fffff << 0)

/* AFE_GAIN2_CON3 */
#define GAIN2_UP_STEP_SFT                                     0
#define GAIN2_UP_STEP_MASK                                    0x3fffff
#define GAIN2_UP_STEP_MASK_SFT                                (0x3fffff << 0)

/* AFE_GAIN3_CON2 */
#define GAIN3_DOWN_STEP_SFT                                   0
#define GAIN3_DOWN_STEP_MASK                                  0x3fffff
#define GAIN3_DOWN_STEP_MASK_SFT                              (0x3fffff << 0)

/* AFE_GAIN3_CON3 */
#define GAIN3_UP_STEP_SFT                                     0
#define GAIN3_UP_STEP_MASK                                    0x3fffff
#define GAIN3_UP_STEP_MASK_SFT                                (0x3fffff << 0)

/* AFE_STF_CON0 */
#define SLT_CNT_FLAG_RESET_SFT                                28
#define SLT_CNT_FLAG_RESET_MASK                               0x1
#define SLT_CNT_FLAG_RESET_MASK_SFT                           (0x1 << 28)
#define SLT_CNT_THD_SFT                                       16
#define SLT_CNT_THD_MASK                                      0xfff
#define SLT_CNT_THD_MASK_SFT                                  (0xfff << 16)
#define SIDE_TONE_HALF_TAP_NUM_SFT                            4
#define SIDE_TONE_HALF_TAP_NUM_MASK                           0x7f
#define SIDE_TONE_HALF_TAP_NUM_MASK_SFT                       (0x7f << 4)
#define SIDE_TONE_ODD_MODE_SFT                                1
#define SIDE_TONE_ODD_MODE_MASK                               0x1
#define SIDE_TONE_ODD_MODE_MASK_SFT                           (0x1 << 1)
#define SIDE_TONE_ON_SFT                                      0
#define SIDE_TONE_ON_MASK                                     0x1
#define SIDE_TONE_ON_MASK_SFT                                 (0x1 << 0)

/* AFE_STF_CON1 */
#define SIDE_TONE_IN_EN_SEL_DOMAIN_SFT                        5
#define SIDE_TONE_IN_EN_SEL_DOMAIN_MASK                       0x7
#define SIDE_TONE_IN_EN_SEL_DOMAIN_MASK_SFT                   (0x7 << 5)
#define SIDE_TONE_IN_EN_SEL_FS_SFT                            0
#define SIDE_TONE_IN_EN_SEL_FS_MASK                           0x1f
#define SIDE_TONE_IN_EN_SEL_FS_MASK_SFT                       (0x1f << 0)

/* AFE_STF_COEFF */
#define SIDE_TONE_COEFFICIENT_R_W_SEL_SFT                     24
#define SIDE_TONE_COEFFICIENT_R_W_SEL_MASK                    0x1
#define SIDE_TONE_COEFFICIENT_R_W_SEL_MASK_SFT                (0x1 << 24)
#define SIDE_TONE_COEFFICIENT_ADDR_SFT                        16
#define SIDE_TONE_COEFFICIENT_ADDR_MASK                       0x1f
#define SIDE_TONE_COEFFICIENT_ADDR_MASK_SFT                   (0x1f << 16)
#define SIDE_TONE_COEFFICIENT_SFT                             0
#define SIDE_TONE_COEFFICIENT_MASK                            0xffff
#define SIDE_TONE_COEFFICIENT_MASK_SFT                        (0xffff << 0)

/* AFE_STF_GAIN */
#define SIDE_TONE_POSITIVE_GAIN_SFT                           16
#define SIDE_TONE_POSITIVE_GAIN_MASK                          0x7
#define SIDE_TONE_POSITIVE_GAIN_MASK_SFT                      (0x7 << 16)
#define SIDE_TONE_GAIN_SFT                                    0
#define SIDE_TONE_GAIN_MASK                                   0xffff
#define SIDE_TONE_GAIN_MASK_SFT                               (0xffff << 0)

/* AFE_STF_MON */
#define SIDE_TONE_R_RDY_SFT                                   30
#define SIDE_TONE_R_RDY_MASK                                  0x1
#define SIDE_TONE_R_RDY_MASK_SFT                              (0x1 << 30)
#define SIDE_TONE_W_RDY_SFT                                   29
#define SIDE_TONE_W_RDY_MASK                                  0x1
#define SIDE_TONE_W_RDY_MASK_SFT                              (0x1 << 29)
#define SLT_CNT_FLAG_SFT                                      28
#define SLT_CNT_FLAG_MASK                                     0x1
#define SLT_CNT_FLAG_MASK_SFT                                 (0x1 << 28)
#define SLT_CNT_SFT                                           16
#define SLT_CNT_MASK                                          0xfff
#define SLT_CNT_MASK_SFT                                      (0xfff << 16)
#define SIDE_TONE_COEFF_SFT                                   0
#define SIDE_TONE_COEFF_MASK                                  0xffff
#define SIDE_TONE_COEFF_MASK_SFT                              (0xffff << 0)

/* AFE_STF_IP_VERSION */
#define SIDE_TONE_IP_VERSION_SFT                              0
#define SIDE_TONE_IP_VERSION_MASK                             0xffffffff
#define SIDE_TONE_IP_VERSION_MASK_SFT                         (0xffffffff << 0)

/* AFE_CM_REG */
#define AFE_CM_UPDATE_CNT_SFT                                 16
#define AFE_CM_UPDATE_CNT_MASK                                0x7fff
#define AFE_CM_UPDATE_CNT_MASK_SFT                            (0x7fff << 16)
#define AFE_CM_1X_EN_SEL_FS_SFT                               8
#define AFE_CM_1X_EN_SEL_FS_MASK                              0x1f
#define AFE_CM_1X_EN_SEL_FS_MASK_SFT                          (0x1f << 8)
#define AFE_CM_CH_NUM_SFT                                     2
#define AFE_CM_CH_NUM_MASK                                    0x1f
#define AFE_CM_CH_NUM_MASK_SFT                                (0x1f << 2)
#define AFE_CM_BYTE_SWAP_SFT                                  1
#define AFE_CM_BYTE_SWAP_MASK                                 0x1
#define AFE_CM_BYTE_SWAP_MASK_SFT                             (0x1 << 1)
#define AFE_CM_BYPASS_MODE_SFT                                31
#define AFE_CM_BYPASS_MODE_MASK                               0x1
#define AFE_CM_BYPASS_MODE_MASK_SFT                           (0x1 << 31)

/* AFE_CM0_CON0 */
#define AFE_CM0_BYPASS_MODE_SFT                               31
#define AFE_CM0_BYPASS_MODE_MASK                              0x1
#define AFE_CM0_BYPASS_MODE_MASK_SFT                          (0x1 << 31)
#define AFE_CM0_UPDATE_CNT_SFT                                16
#define AFE_CM0_UPDATE_CNT_MASK                               0x7fff
#define AFE_CM0_UPDATE_CNT_MASK_SFT                           (0x7fff << 16)
#define AFE_CM0_1X_EN_SEL_DOMAIN_SFT                          13
#define AFE_CM0_1X_EN_SEL_DOMAIN_MASK                         0x7
#define AFE_CM0_1X_EN_SEL_DOMAIN_MASK_SFT                     (0x7 << 13)
#define AFE_CM0_1X_EN_SEL_FS_SFT                              8
#define AFE_CM0_1X_EN_SEL_FS_MASK                             0x1f
#define AFE_CM0_1X_EN_SEL_FS_MASK_SFT                         (0x1f << 8)
#define AFE_CM0_OUTPUT_MUX_SFT                                7
#define AFE_CM0_OUTPUT_MUX_MASK                               0x1
#define AFE_CM0_OUTPUT_MUX_MASK_SFT                           (0x1 << 7)
#define AFE_CM0_CH_NUM_SFT                                    2
#define AFE_CM0_CH_NUM_MASK                                   0x1f
#define AFE_CM0_CH_NUM_MASK_SFT                               (0x1f << 2)
#define AFE_CM0_BYTE_SWAP_SFT                                 1
#define AFE_CM0_BYTE_SWAP_MASK                                0x1
#define AFE_CM0_BYTE_SWAP_MASK_SFT                            (0x1 << 1)
#define AFE_CM0_ON_SFT                                        0
#define AFE_CM0_ON_MASK                                       0x1
#define AFE_CM0_ON_MASK_SFT                                   (0x1 << 0)

/* AFE_CM0_MON */
#define AFE_CM0_BYPASS_MODE_MON_SFT                           31
#define AFE_CM0_BYPASS_MODE_MON_MASK                          0x1
#define AFE_CM0_BYPASS_MODE_MON_MASK_SFT                      (0x1 << 31)
#define AFE_CM0_OUTPUT_CNT_MON_SFT                            16
#define AFE_CM0_OUTPUT_CNT_MON_MASK                           0x7fff
#define AFE_CM0_OUTPUT_CNT_MON_MASK_SFT                       (0x7fff << 16)
#define AFE_CM0_CUR_CHSET_MON_SFT                             5
#define AFE_CM0_CUR_CHSET_MON_MASK                            0xf
#define AFE_CM0_CUR_CHSET_MON_MASK_SFT                        (0xf << 5)
#define AFE_CM0_ODD_FLAG_MON_SFT                              4
#define AFE_CM0_ODD_FLAG_MON_MASK                             0x1
#define AFE_CM0_ODD_FLAG_MON_MASK_SFT                         (0x1 << 4)
#define AFE_CM0_BYTE_SWAP_MON_SFT                             1
#define AFE_CM0_BYTE_SWAP_MON_MASK                            0x1
#define AFE_CM0_BYTE_SWAP_MON_MASK_SFT                        (0x1 << 1)
#define AFE_CM0_ON_MON_SFT                                    0
#define AFE_CM0_ON_MON_MASK                                   0x1
#define AFE_CM0_ON_MON_MASK_SFT                               (0x1 << 0)

/* AFE_CM0_IP_VERSION */
#define AFE_CM0_IP_VERSION_SFT                                0
#define AFE_CM0_IP_VERSION_MASK                               0xffffffff
#define AFE_CM0_IP_VERSION_MASK_SFT                           (0xffffffff << 0)

/* AFE_CM1_CON0 */
#define AFE_CM1_BYPASS_MODE_SFT                               31
#define AFE_CM1_BYPASS_MODE_MASK                              0x1
#define AFE_CM1_BYPASS_MODE_MASK_SFT                          (0x1 << 31)
#define AFE_CM1_UPDATE_CNT_SFT                                16
#define AFE_CM1_UPDATE_CNT_MASK                               0x7fff
#define AFE_CM1_UPDATE_CNT_MASK_SFT                           (0x7fff << 16)
#define AFE_CM1_1X_EN_SEL_DOMAIN_SFT                          13
#define AFE_CM1_1X_EN_SEL_DOMAIN_MASK                         0x7
#define AFE_CM1_1X_EN_SEL_DOMAIN_MASK_SFT                     (0x7 << 13)
#define AFE_CM1_1X_EN_SEL_FS_SFT                              8
#define AFE_CM1_1X_EN_SEL_FS_MASK                             0x1f
#define AFE_CM1_1X_EN_SEL_FS_MASK_SFT                         (0x1f << 8)
#define AFE_CM1_OUTPUT_MUX_SFT                                7
#define AFE_CM1_OUTPUT_MUX_MASK                               0x1
#define AFE_CM1_OUTPUT_MUX_MASK_SFT                           (0x1 << 7)
#define AFE_CM1_CH_NUM_SFT                                    2
#define AFE_CM1_CH_NUM_MASK                                   0x1f
#define AFE_CM1_CH_NUM_MASK_SFT                               (0x1f << 2)
#define AFE_CM1_BYTE_SWAP_SFT                                 1
#define AFE_CM1_BYTE_SWAP_MASK                                0x1
#define AFE_CM1_BYTE_SWAP_MASK_SFT                            (0x1 << 1)
#define AFE_CM1_ON_SFT                                        0
#define AFE_CM1_ON_MASK                                       0x1
#define AFE_CM1_ON_MASK_SFT                                   (0x1 << 0)

/* AFE_CM1_MON */
#define AFE_CM1_BYPASS_MODE_MON_SFT                           31
#define AFE_CM1_BYPASS_MODE_MON_MASK                          0x1
#define AFE_CM1_BYPASS_MODE_MON_MASK_SFT                      (0x1 << 31)
#define AFE_CM1_OUTPUT_CNT_MON_SFT                            16
#define AFE_CM1_OUTPUT_CNT_MON_MASK                           0x7fff
#define AFE_CM1_OUTPUT_CNT_MON_MASK_SFT                       (0x7fff << 16)
#define AFE_CM1_CUR_CHSET_MON_SFT                             5
#define AFE_CM1_CUR_CHSET_MON_MASK                            0xf
#define AFE_CM1_CUR_CHSET_MON_MASK_SFT                        (0xf << 5)
#define AFE_CM1_ODD_FLAG_MON_SFT                              4
#define AFE_CM1_ODD_FLAG_MON_MASK                             0x1
#define AFE_CM1_ODD_FLAG_MON_MASK_SFT                         (0x1 << 4)
#define AFE_CM1_BYTE_SWAP_MON_SFT                             1
#define AFE_CM1_BYTE_SWAP_MON_MASK                            0x1
#define AFE_CM1_BYTE_SWAP_MON_MASK_SFT                        (0x1 << 1)
#define AFE_CM1_ON_MON_SFT                                    0
#define AFE_CM1_ON_MON_MASK                                   0x1
#define AFE_CM1_ON_MON_MASK_SFT                               (0x1 << 0)

/* AFE_CM1_IP_VERSION */
#define AFE_CM1_IP_VERSION_SFT                                0
#define AFE_CM1_IP_VERSION_MASK                               0xffffffff
#define AFE_CM1_IP_VERSION_MASK_SFT                           (0xffffffff << 0)

/* AFE_CM2_CON0 */
#define AFE_CM2_BYPASS_MODE_SFT                               31
#define AFE_CM2_BYPASS_MODE_MASK                              0x1
#define AFE_CM2_BYPASS_MODE_MASK_SFT                          (0x1 << 31)
#define AFE_CM2_UPDATE_CNT_SFT                                16
#define AFE_CM2_UPDATE_CNT_MASK                               0x7fff
#define AFE_CM2_UPDATE_CNT_MASK_SFT                           (0x7fff << 16)
#define AFE_CM2_1X_EN_SEL_DOMAIN_SFT                          13
#define AFE_CM2_1X_EN_SEL_DOMAIN_MASK                         0x7
#define AFE_CM2_1X_EN_SEL_DOMAIN_MASK_SFT                     (0x7 << 13)
#define AFE_CM2_1X_EN_SEL_FS_SFT                              8
#define AFE_CM2_1X_EN_SEL_FS_MASK                             0x1f
#define AFE_CM2_1X_EN_SEL_FS_MASK_SFT                         (0x1f << 8)
#define AFE_CM2_OUTPUT_MUX_SFT                                7
#define AFE_CM2_OUTPUT_MUX_MASK                               0x1
#define AFE_CM2_OUTPUT_MUX_MASK_SFT                           (0x1 << 7)
#define AFE_CM2_CH_NUM_SFT                                    2
#define AFE_CM2_CH_NUM_MASK                                   0x1f
#define AFE_CM2_CH_NUM_MASK_SFT                               (0x1f << 2)
#define AFE_CM2_BYTE_SWAP_SFT                                 1
#define AFE_CM2_BYTE_SWAP_MASK                                0x1
#define AFE_CM2_BYTE_SWAP_MASK_SFT                            (0x1 << 1)
#define AFE_CM2_ON_SFT                                        0
#define AFE_CM2_ON_MASK                                       0x1
#define AFE_CM2_ON_MASK_SFT                                   (0x1 << 0)

/* AFE_CM2_MON */
#define AFE_CM2_BYPASS_MODE_MON_SFT                           31
#define AFE_CM2_BYPASS_MODE_MON_MASK                          0x1
#define AFE_CM2_BYPASS_MODE_MON_MASK_SFT                      (0x1 << 31)
#define AFE_CM2_OUTPUT_CNT_MON_SFT                            16
#define AFE_CM2_OUTPUT_CNT_MON_MASK                           0x7fff
#define AFE_CM2_OUTPUT_CNT_MON_MASK_SFT                       (0x7fff << 16)
#define AFE_CM2_CUR_CHSET_MON_SFT                             5
#define AFE_CM2_CUR_CHSET_MON_MASK                            0xf
#define AFE_CM2_CUR_CHSET_MON_MASK_SFT                        (0xf << 5)
#define AFE_CM2_ODD_FLAG_MON_SFT                              4
#define AFE_CM2_ODD_FLAG_MON_MASK                             0x1
#define AFE_CM2_ODD_FLAG_MON_MASK_SFT                         (0x1 << 4)
#define AFE_CM2_BYTE_SWAP_MON_SFT                             1
#define AFE_CM2_BYTE_SWAP_MON_MASK                            0x1
#define AFE_CM2_BYTE_SWAP_MON_MASK_SFT                        (0x1 << 1)
#define AFE_CM2_ON_MON_SFT                                    0
#define AFE_CM2_ON_MON_MASK                                   0x1
#define AFE_CM2_ON_MON_MASK_SFT                               (0x1 << 0)

/* AFE_CM2_IP_VERSION */
#define AFE_CM2_IP_VERSION_SFT                                0
#define AFE_CM2_IP_VERSION_MASK                               0xffffffff
#define AFE_CM2_IP_VERSION_MASK_SFT                           (0xffffffff << 0)

/* AFE_ADDA_UL0_SRC_CON0 */
#define ULCF_CFG_EN_CTL_SFT                                   31
#define ULCF_CFG_EN_CTL_MASK                                  0x1
#define ULCF_CFG_EN_CTL_MASK_SFT                              (0x1 << 31)
#define UL_DMIC_PHASE_SEL_CH1_SFT                             27
#define UL_DMIC_PHASE_SEL_CH1_MASK                            0x7
#define UL_DMIC_PHASE_SEL_CH1_MASK_SFT                        (0x7 << 27)
#define UL_DMIC_PHASE_SEL_CH2_SFT                             24
#define UL_DMIC_PHASE_SEL_CH2_MASK                            0x7
#define UL_DMIC_PHASE_SEL_CH2_MASK_SFT                        (0x7 << 24)
#define UL_DMIC_TWO_WIRE_CTL_SFT                              23
#define UL_DMIC_TWO_WIRE_CTL_MASK                             0x1
#define UL_DMIC_TWO_WIRE_CTL_MASK_SFT                         (0x1 << 23)
#define UL_MODE_3P25M_CH2_CTL_SFT                             22
#define UL_MODE_3P25M_CH2_CTL_MASK                            0x1
#define UL_MODE_3P25M_CH2_CTL_MASK_SFT                        (0x1 << 22)
#define UL_MODE_3P25M_CH1_CTL_SFT                             21
#define UL_MODE_3P25M_CH1_CTL_MASK                            0x1
#define UL_MODE_3P25M_CH1_CTL_MASK_SFT                        (0x1 << 21)
#define UL_VOICE_MODE_CH1_CH2_CTL_SFT                         17
#define UL_VOICE_MODE_CH1_CH2_CTL_MASK                        0x7
#define UL_VOICE_MODE_CH1_CH2_CTL_MASK_SFT                    (0x7 << 17)
#define UL_AP_DMIC_ON_SFT                                     16
#define UL_AP_DMIC_ON_MASK                                    0x1
#define UL_AP_DMIC_ON_MASK_SFT                                (0x1 << 16)
#define DMIC_LOW_POWER_MODE_CTL_SFT                           14
#define DMIC_LOW_POWER_MODE_CTL_MASK                          0x3
#define DMIC_LOW_POWER_MODE_CTL_MASK_SFT                      (0x3 << 14)
#define UL_DISABLE_HW_CG_CTL_SFT                              12
#define UL_DISABLE_HW_CG_CTL_MASK                             0x1
#define UL_DISABLE_HW_CG_CTL_MASK_SFT                         (0x1 << 12)
#define AMIC_26M_SEL_CTL_SFT                                  11
#define AMIC_26M_SEL_CTL_MASK                                 0x1
#define AMIC_26M_SEL_CTL_MASK_SFT                             (0x1 << 11)
#define UL_IIR_ON_TMP_CTL_SFT                                 10
#define UL_IIR_ON_TMP_CTL_MASK                                0x1
#define UL_IIR_ON_TMP_CTL_MASK_SFT                            (0x1 << 10)
#define UL_IIRMODE_CTL_SFT                                    7
#define UL_IIRMODE_CTL_MASK                                   0x7
#define UL_IIRMODE_CTL_MASK_SFT                               (0x7 << 7)
#define DIGMIC_4P33M_SEL_SFT                                  6
#define DIGMIC_4P33M_SEL_MASK                                 0x1
#define DIGMIC_4P33M_SEL_MASK_SFT                             (0x1 << 6)
#define DIGMIC_3P25M_1P625M_SEL_CTL_SFT                       5
#define DIGMIC_3P25M_1P625M_SEL_CTL_MASK                      0x1
#define DIGMIC_3P25M_1P625M_SEL_CTL_MASK_SFT                  (0x1 << 5)
#define AMIC_6P5M_SEL_CTL_SFT                                 4
#define AMIC_6P5M_SEL_CTL_MASK                                0x1
#define AMIC_6P5M_SEL_CTL_MASK_SFT                            (0x1 << 4)
#define AMIC_1P625M_SEL_CTL_SFT                               3
#define AMIC_1P625M_SEL_CTL_MASK                              0x1
#define AMIC_1P625M_SEL_CTL_MASK_SFT                          (0x1 << 3)
#define UL_LOOP_BACK_MODE_CTL_SFT                             2
#define UL_LOOP_BACK_MODE_CTL_MASK                            0x1
#define UL_LOOP_BACK_MODE_CTL_MASK_SFT                        (0x1 << 2)
#define UL_SDM_3_LEVEL_CTL_SFT                                1
#define UL_SDM_3_LEVEL_CTL_MASK                               0x1
#define UL_SDM_3_LEVEL_CTL_MASK_SFT                           (0x1 << 1)
#define UL_SRC_ON_TMP_CTL_SFT                                 0
#define UL_SRC_ON_TMP_CTL_MASK                                0x1
#define UL_SRC_ON_TMP_CTL_MASK_SFT                            (0x1 << 0)

/* AFE_ADDA_UL0_SRC_CON1 */
#define ADDA_UL_GAIN_VALUE_SFT                                16
#define ADDA_UL_GAIN_VALUE_MASK                               0xffff
#define ADDA_UL_GAIN_VALUE_MASK_SFT                           (0xffff << 16)
#define ADDA_UL_POSTIVEGAIN_SFT                               12
#define ADDA_UL_POSTIVEGAIN_MASK                              0x7
#define ADDA_UL_POSTIVEGAIN_MASK_SFT                          (0x7 << 12)
#define ADDA_UL_ODDTAP_MODE_SFT                               11
#define ADDA_UL_ODDTAP_MODE_MASK                              0x1
#define ADDA_UL_ODDTAP_MODE_MASK_SFT                          (0x1 << 11)
#define ADDA_UL_HALF_TAP_NUM_SFT                              5
#define ADDA_UL_HALF_TAP_NUM_MASK                             0x3f
#define ADDA_UL_HALF_TAP_NUM_MASK_SFT                         (0x3f << 5)
#define FIFO_SOFT_RST_SFT                                     4
#define FIFO_SOFT_RST_MASK                                    0x1
#define FIFO_SOFT_RST_MASK_SFT                                (0x1 << 4)
#define FIFO_SOFT_RST_EN_SFT                                  3
#define FIFO_SOFT_RST_EN_MASK                                 0x1
#define FIFO_SOFT_RST_EN_MASK_SFT                             (0x1 << 3)
#define LR_SWAP_SFT                                           2
#define LR_SWAP_MASK                                          0x1
#define LR_SWAP_MASK_SFT                                      (0x1 << 2)
#define GAIN_MODE_SFT                                         0
#define GAIN_MODE_MASK                                        0x3
#define GAIN_MODE_MASK_SFT                                    (0x3 << 0)

/* AFE_ADDA_UL0_SRC_CON2 */
#define C_DAC_EN_CTL_SFT                                      27
#define C_DAC_EN_CTL_MASK                                     0x1
#define C_DAC_EN_CTL_MASK_SFT                                 (0x1 << 27)
#define C_MUTE_SW_CTL_SFT                                     26
#define C_MUTE_SW_CTL_MASK                                    0x1
#define C_MUTE_SW_CTL_MASK_SFT                                (0x1 << 26)
#define C_AMP_DIV_CH2_CTL_SFT                                 21
#define C_AMP_DIV_CH2_CTL_MASK                                0x7
#define C_AMP_DIV_CH2_CTL_MASK_SFT                            (0x7 << 21)
#define C_FREQ_DIV_CH2_CTL_SFT                                16
#define C_FREQ_DIV_CH2_CTL_MASK                               0x1f
#define C_FREQ_DIV_CH2_CTL_MASK_SFT                           (0x1f << 16)
#define C_SINE_MODE_CH2_CTL_SFT                               12
#define C_SINE_MODE_CH2_CTL_MASK                              0xf
#define C_SINE_MODE_CH2_CTL_MASK_SFT                          (0xf << 12)
#define C_AMP_DIV_CH1_CTL_SFT                                 9
#define C_AMP_DIV_CH1_CTL_MASK                                0x7
#define C_AMP_DIV_CH1_CTL_MASK_SFT                            (0x7 << 9)
#define C_FREQ_DIV_CH1_CTL_SFT                                4
#define C_FREQ_DIV_CH1_CTL_MASK                               0x1f
#define C_FREQ_DIV_CH1_CTL_MASK_SFT                           (0x1f << 4)
#define C_SINE_MODE_CH1_CTL_SFT                               0
#define C_SINE_MODE_CH1_CTL_MASK                              0xf
#define C_SINE_MODE_CH1_CTL_MASK_SFT                          (0xf << 0)

/* AFE_ADDA_UL0_SRC_DEBUG */
#define UL_SLT_CNT_FLAG_RESET_CTL_SFT                         16
#define UL_SLT_CNT_FLAG_RESET_CTL_MASK                        0x1
#define UL_SLT_CNT_FLAG_RESET_CTL_MASK_SFT                    (0x1 << 16)
#define FIFO_DIGMIC_TESTIN_SFT                                12
#define FIFO_DIGMIC_TESTIN_MASK                               0x3
#define FIFO_DIGMIC_TESTIN_MASK_SFT                           (0x3 << 12)
#define FIFO_DIGMIC_WDATA_TESTEN_SFT                          11
#define FIFO_DIGMIC_WDATA_TESTEN_MASK                         0x1
#define FIFO_DIGMIC_WDATA_TESTEN_MASK_SFT                     (0x1 << 11)
#define SLT_CNT_THD_CTL_SFT                                   0
#define SLT_CNT_THD_CTL_MASK                                  0x7ff
#define SLT_CNT_THD_CTL_MASK_SFT                              (0x7ff << 0)

/* AFE_ADDA_UL0_SRC_DEBUG_MON0 */
#define SLT_CNT_FLAG_CTL_SFT                                  16
#define SLT_CNT_FLAG_CTL_MASK                                 0x1
#define SLT_CNT_FLAG_CTL_MASK_SFT                             (0x1 << 16)
#define SLT_COUNTER_CTL_SFT                                   0
#define SLT_COUNTER_CTL_MASK                                  0x7ff
#define SLT_COUNTER_CTL_MASK_SFT                              (0x7ff << 0)

/* AFE_ADDA_UL0_IIR_COEF_02_01 */
#define ADDA_IIR_COEF_02_01_SFT                               0
#define ADDA_IIR_COEF_02_01_MASK                              0xffffffff
#define ADDA_IIR_COEF_02_01_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_IIR_COEF_04_03 */
#define ADDA_IIR_COEF_04_03_SFT                               0
#define ADDA_IIR_COEF_04_03_MASK                              0xffffffff
#define ADDA_IIR_COEF_04_03_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_IIR_COEF_06_05 */
#define ADDA_IIR_COEF_06_05_SFT                               0
#define ADDA_IIR_COEF_06_05_MASK                              0xffffffff
#define ADDA_IIR_COEF_06_05_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_IIR_COEF_08_07 */
#define ADDA_IIR_COEF_08_07_SFT                               0
#define ADDA_IIR_COEF_08_07_MASK                              0xffffffff
#define ADDA_IIR_COEF_08_07_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_IIR_COEF_10_09 */
#define ADDA_IIR_COEF_10_09_SFT                               0
#define ADDA_IIR_COEF_10_09_MASK                              0xffffffff
#define ADDA_IIR_COEF_10_09_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_02_01 */
#define ADDA_ULCF_CFG_02_01_SFT                               0
#define ADDA_ULCF_CFG_02_01_MASK                              0xffffffff
#define ADDA_ULCF_CFG_02_01_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_04_03 */
#define ADDA_ULCF_CFG_04_03_SFT                               0
#define ADDA_ULCF_CFG_04_03_MASK                              0xffffffff
#define ADDA_ULCF_CFG_04_03_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_06_05 */
#define ADDA_ULCF_CFG_06_05_SFT                               0
#define ADDA_ULCF_CFG_06_05_MASK                              0xffffffff
#define ADDA_ULCF_CFG_06_05_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_08_07 */
#define ADDA_ULCF_CFG_08_07_SFT                               0
#define ADDA_ULCF_CFG_08_07_MASK                              0xffffffff
#define ADDA_ULCF_CFG_08_07_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_10_09 */
#define ADDA_ULCF_CFG_10_09_SFT                               0
#define ADDA_ULCF_CFG_10_09_MASK                              0xffffffff
#define ADDA_ULCF_CFG_10_09_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_12_11 */
#define ADDA_ULCF_CFG_12_11_SFT                               0
#define ADDA_ULCF_CFG_12_11_MASK                              0xffffffff
#define ADDA_ULCF_CFG_12_11_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_14_13 */
#define ADDA_ULCF_CFG_14_13_SFT                               0
#define ADDA_ULCF_CFG_14_13_MASK                              0xffffffff
#define ADDA_ULCF_CFG_14_13_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_16_15 */
#define ADDA_ULCF_CFG_16_15_SFT                               0
#define ADDA_ULCF_CFG_16_15_MASK                              0xffffffff
#define ADDA_ULCF_CFG_16_15_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_18_17 */
#define ADDA_ULCF_CFG_18_17_SFT                               0
#define ADDA_ULCF_CFG_18_17_MASK                              0xffffffff
#define ADDA_ULCF_CFG_18_17_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_20_19 */
#define ADDA_ULCF_CFG_20_19_SFT                               0
#define ADDA_ULCF_CFG_20_19_MASK                              0xffffffff
#define ADDA_ULCF_CFG_20_19_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_22_21 */
#define ADDA_ULCF_CFG_22_21_SFT                               0
#define ADDA_ULCF_CFG_22_21_MASK                              0xffffffff
#define ADDA_ULCF_CFG_22_21_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_24_23 */
#define ADDA_ULCF_CFG_24_23_SFT                               0
#define ADDA_ULCF_CFG_24_23_MASK                              0xffffffff
#define ADDA_ULCF_CFG_24_23_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_26_25 */
#define ADDA_ULCF_CFG_26_25_SFT                               0
#define ADDA_ULCF_CFG_26_25_MASK                              0xffffffff
#define ADDA_ULCF_CFG_26_25_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_28_27 */
#define ADDA_ULCF_CFG_28_27_SFT                               0
#define ADDA_ULCF_CFG_28_27_MASK                              0xffffffff
#define ADDA_ULCF_CFG_28_27_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_30_29 */
#define ADDA_ULCF_CFG_30_29_SFT                               0
#define ADDA_ULCF_CFG_30_29_MASK                              0xffffffff
#define ADDA_ULCF_CFG_30_29_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_ULCF_CFG_32_31 */
#define ADDA_ULCF_CFG_32_31_SFT                               0
#define ADDA_ULCF_CFG_32_31_MASK                              0xffffffff
#define ADDA_ULCF_CFG_32_31_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL0_IP_VERSION */
#define ADDA_ULCF_IP_VERSION_SFT                              0
#define ADDA_ULCF_IP_VERSION_MASK                             0xffffffff
#define ADDA_ULCF_IP_VERSION_MASK_SFT                         (0xffffffff << 0)

/* AFE_ADDA_UL1_SRC_CON0 */
#define ULCF_CFG_EN_CTL_SFT                                   31
#define ULCF_CFG_EN_CTL_MASK                                  0x1
#define ULCF_CFG_EN_CTL_MASK_SFT                              (0x1 << 31)
#define UL_DMIC_PHASE_SEL_CH1_SFT                             27
#define UL_DMIC_PHASE_SEL_CH1_MASK                            0x7
#define UL_DMIC_PHASE_SEL_CH1_MASK_SFT                        (0x7 << 27)
#define UL_DMIC_PHASE_SEL_CH2_SFT                             24
#define UL_DMIC_PHASE_SEL_CH2_MASK                            0x7
#define UL_DMIC_PHASE_SEL_CH2_MASK_SFT                        (0x7 << 24)
#define UL_DMIC_TWO_WIRE_CTL_SFT                              23
#define UL_DMIC_TWO_WIRE_CTL_MASK                             0x1
#define UL_DMIC_TWO_WIRE_CTL_MASK_SFT                         (0x1 << 23)
#define UL_MODE_3P25M_CH2_CTL_SFT                             22
#define UL_MODE_3P25M_CH2_CTL_MASK                            0x1
#define UL_MODE_3P25M_CH2_CTL_MASK_SFT                        (0x1 << 22)
#define UL_MODE_3P25M_CH1_CTL_SFT                             21
#define UL_MODE_3P25M_CH1_CTL_MASK                            0x1
#define UL_MODE_3P25M_CH1_CTL_MASK_SFT                        (0x1 << 21)
#define UL_VOICE_MODE_CH1_CH2_CTL_SFT                         17
#define UL_VOICE_MODE_CH1_CH2_CTL_MASK                        0x7
#define UL_VOICE_MODE_CH1_CH2_CTL_MASK_SFT                    (0x7 << 17)
#define UL_AP_DMIC_ON_SFT                                     16
#define UL_AP_DMIC_ON_MASK                                    0x1
#define UL_AP_DMIC_ON_MASK_SFT                                (0x1 << 16)
#define DMIC_LOW_POWER_MODE_CTL_SFT                           14
#define DMIC_LOW_POWER_MODE_CTL_MASK                          0x3
#define DMIC_LOW_POWER_MODE_CTL_MASK_SFT                      (0x3 << 14)
#define UL_DISABLE_HW_CG_CTL_SFT                              12
#define UL_DISABLE_HW_CG_CTL_MASK                             0x1
#define UL_DISABLE_HW_CG_CTL_MASK_SFT                         (0x1 << 12)
#define AMIC_26M_SEL_CTL_SFT                                  11
#define AMIC_26M_SEL_CTL_MASK                                 0x1
#define AMIC_26M_SEL_CTL_MASK_SFT                             (0x1 << 11)
#define UL_IIR_ON_TMP_CTL_SFT                                 10
#define UL_IIR_ON_TMP_CTL_MASK                                0x1
#define UL_IIR_ON_TMP_CTL_MASK_SFT                            (0x1 << 10)
#define UL_IIRMODE_CTL_SFT                                    7
#define UL_IIRMODE_CTL_MASK                                   0x7
#define UL_IIRMODE_CTL_MASK_SFT                               (0x7 << 7)
#define DIGMIC_4P33M_SEL_SFT                                  6
#define DIGMIC_4P33M_SEL_MASK                                 0x1
#define DIGMIC_4P33M_SEL_MASK_SFT                             (0x1 << 6)
#define DIGMIC_3P25M_1P625M_SEL_CTL_SFT                       5
#define DIGMIC_3P25M_1P625M_SEL_CTL_MASK                      0x1
#define DIGMIC_3P25M_1P625M_SEL_CTL_MASK_SFT                  (0x1 << 5)
#define AMIC_6P5M_SEL_CTL_SFT                                 4
#define AMIC_6P5M_SEL_CTL_MASK                                0x1
#define AMIC_6P5M_SEL_CTL_MASK_SFT                            (0x1 << 4)
#define AMIC_1P625M_SEL_CTL_SFT                               3
#define AMIC_1P625M_SEL_CTL_MASK                              0x1
#define AMIC_1P625M_SEL_CTL_MASK_SFT                          (0x1 << 3)
#define UL_LOOP_BACK_MODE_CTL_SFT                             2
#define UL_LOOP_BACK_MODE_CTL_MASK                            0x1
#define UL_LOOP_BACK_MODE_CTL_MASK_SFT                        (0x1 << 2)
#define UL_SDM_3_LEVEL_CTL_SFT                                1
#define UL_SDM_3_LEVEL_CTL_MASK                               0x1
#define UL_SDM_3_LEVEL_CTL_MASK_SFT                           (0x1 << 1)
#define UL_SRC_ON_TMP_CTL_SFT                                 0
#define UL_SRC_ON_TMP_CTL_MASK                                0x1
#define UL_SRC_ON_TMP_CTL_MASK_SFT                            (0x1 << 0)

/* AFE_ADDA_UL1_SRC_CON1 */
#define ADDA_UL_GAIN_VALUE_SFT                                16
#define ADDA_UL_GAIN_VALUE_MASK                               0xffff
#define ADDA_UL_GAIN_VALUE_MASK_SFT                           (0xffff << 16)
#define ADDA_UL_POSTIVEGAIN_SFT                               12
#define ADDA_UL_POSTIVEGAIN_MASK                              0x7
#define ADDA_UL_POSTIVEGAIN_MASK_SFT                          (0x7 << 12)
#define ADDA_UL_ODDTAP_MODE_SFT                               11
#define ADDA_UL_ODDTAP_MODE_MASK                              0x1
#define ADDA_UL_ODDTAP_MODE_MASK_SFT                          (0x1 << 11)
#define ADDA_UL_HALF_TAP_NUM_SFT                              5
#define ADDA_UL_HALF_TAP_NUM_MASK                             0x3f
#define ADDA_UL_HALF_TAP_NUM_MASK_SFT                         (0x3f << 5)
#define FIFO_SOFT_RST_SFT                                     4
#define FIFO_SOFT_RST_MASK                                    0x1
#define FIFO_SOFT_RST_MASK_SFT                                (0x1 << 4)
#define FIFO_SOFT_RST_EN_SFT                                  3
#define FIFO_SOFT_RST_EN_MASK                                 0x1
#define FIFO_SOFT_RST_EN_MASK_SFT                             (0x1 << 3)
#define LR_SWAP_SFT                                           2
#define LR_SWAP_MASK                                          0x1
#define LR_SWAP_MASK_SFT                                      (0x1 << 2)
#define GAIN_MODE_SFT                                         0
#define GAIN_MODE_MASK                                        0x3
#define GAIN_MODE_MASK_SFT                                    (0x3 << 0)

/* AFE_ADDA_UL1_SRC_CON2 */
#define C_DAC_EN_CTL_SFT                                      27
#define C_DAC_EN_CTL_MASK                                     0x1
#define C_DAC_EN_CTL_MASK_SFT                                 (0x1 << 27)
#define C_MUTE_SW_CTL_SFT                                     26
#define C_MUTE_SW_CTL_MASK                                    0x1
#define C_MUTE_SW_CTL_MASK_SFT                                (0x1 << 26)
#define C_AMP_DIV_CH2_CTL_SFT                                 21
#define C_AMP_DIV_CH2_CTL_MASK                                0x7
#define C_AMP_DIV_CH2_CTL_MASK_SFT                            (0x7 << 21)
#define C_FREQ_DIV_CH2_CTL_SFT                                16
#define C_FREQ_DIV_CH2_CTL_MASK                               0x1f
#define C_FREQ_DIV_CH2_CTL_MASK_SFT                           (0x1f << 16)
#define C_SINE_MODE_CH2_CTL_SFT                               12
#define C_SINE_MODE_CH2_CTL_MASK                              0xf
#define C_SINE_MODE_CH2_CTL_MASK_SFT                          (0xf << 12)
#define C_AMP_DIV_CH1_CTL_SFT                                 9
#define C_AMP_DIV_CH1_CTL_MASK                                0x7
#define C_AMP_DIV_CH1_CTL_MASK_SFT                            (0x7 << 9)
#define C_FREQ_DIV_CH1_CTL_SFT                                4
#define C_FREQ_DIV_CH1_CTL_MASK                               0x1f
#define C_FREQ_DIV_CH1_CTL_MASK_SFT                           (0x1f << 4)
#define C_SINE_MODE_CH1_CTL_SFT                               0
#define C_SINE_MODE_CH1_CTL_MASK                              0xf
#define C_SINE_MODE_CH1_CTL_MASK_SFT                          (0xf << 0)

/* AFE_ADDA_UL1_SRC_DEBUG */
#define UL_SLT_CNT_FLAG_RESET_CTL_SFT                         16
#define UL_SLT_CNT_FLAG_RESET_CTL_MASK                        0x1
#define UL_SLT_CNT_FLAG_RESET_CTL_MASK_SFT                    (0x1 << 16)
#define FIFO_DIGMIC_TESTIN_SFT                                12
#define FIFO_DIGMIC_TESTIN_MASK                               0x3
#define FIFO_DIGMIC_TESTIN_MASK_SFT                           (0x3 << 12)
#define FIFO_DIGMIC_WDATA_TESTEN_SFT                          11
#define FIFO_DIGMIC_WDATA_TESTEN_MASK                         0x1
#define FIFO_DIGMIC_WDATA_TESTEN_MASK_SFT                     (0x1 << 11)
#define SLT_CNT_THD_CTL_SFT                                   0
#define SLT_CNT_THD_CTL_MASK                                  0x7ff
#define SLT_CNT_THD_CTL_MASK_SFT                              (0x7ff << 0)

/* AFE_ADDA_UL1_SRC_DEBUG_MON0 */
#define SLT_CNT_FLAG_CTL_SFT                                  16
#define SLT_CNT_FLAG_CTL_MASK                                 0x1
#define SLT_CNT_FLAG_CTL_MASK_SFT                             (0x1 << 16)
#define SLT_COUNTER_CTL_SFT                                   0
#define SLT_COUNTER_CTL_MASK                                  0x7ff
#define SLT_COUNTER_CTL_MASK_SFT                              (0x7ff << 0)

/* AFE_ADDA_UL1_IIR_COEF_02_01 */
#define ADDA_IIR_COEF_02_01_SFT                               0
#define ADDA_IIR_COEF_02_01_MASK                              0xffffffff
#define ADDA_IIR_COEF_02_01_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_IIR_COEF_04_03 */
#define ADDA_IIR_COEF_04_03_SFT                               0
#define ADDA_IIR_COEF_04_03_MASK                              0xffffffff
#define ADDA_IIR_COEF_04_03_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_IIR_COEF_06_05 */
#define ADDA_IIR_COEF_06_05_SFT                               0
#define ADDA_IIR_COEF_06_05_MASK                              0xffffffff
#define ADDA_IIR_COEF_06_05_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_IIR_COEF_08_07 */
#define ADDA_IIR_COEF_08_07_SFT                               0
#define ADDA_IIR_COEF_08_07_MASK                              0xffffffff
#define ADDA_IIR_COEF_08_07_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_IIR_COEF_10_09 */
#define ADDA_IIR_COEF_10_09_SFT                               0
#define ADDA_IIR_COEF_10_09_MASK                              0xffffffff
#define ADDA_IIR_COEF_10_09_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_02_01 */
#define ADDA_ULCF_CFG_02_01_SFT                               0
#define ADDA_ULCF_CFG_02_01_MASK                              0xffffffff
#define ADDA_ULCF_CFG_02_01_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_04_03 */
#define ADDA_ULCF_CFG_04_03_SFT                               0
#define ADDA_ULCF_CFG_04_03_MASK                              0xffffffff
#define ADDA_ULCF_CFG_04_03_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_06_05 */
#define ADDA_ULCF_CFG_06_05_SFT                               0
#define ADDA_ULCF_CFG_06_05_MASK                              0xffffffff
#define ADDA_ULCF_CFG_06_05_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_08_07 */
#define ADDA_ULCF_CFG_08_07_SFT                               0
#define ADDA_ULCF_CFG_08_07_MASK                              0xffffffff
#define ADDA_ULCF_CFG_08_07_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_10_09 */
#define ADDA_ULCF_CFG_10_09_SFT                               0
#define ADDA_ULCF_CFG_10_09_MASK                              0xffffffff
#define ADDA_ULCF_CFG_10_09_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_12_11 */
#define ADDA_ULCF_CFG_12_11_SFT                               0
#define ADDA_ULCF_CFG_12_11_MASK                              0xffffffff
#define ADDA_ULCF_CFG_12_11_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_14_13 */
#define ADDA_ULCF_CFG_14_13_SFT                               0
#define ADDA_ULCF_CFG_14_13_MASK                              0xffffffff
#define ADDA_ULCF_CFG_14_13_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_16_15 */
#define ADDA_ULCF_CFG_16_15_SFT                               0
#define ADDA_ULCF_CFG_16_15_MASK                              0xffffffff
#define ADDA_ULCF_CFG_16_15_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_18_17 */
#define ADDA_ULCF_CFG_18_17_SFT                               0
#define ADDA_ULCF_CFG_18_17_MASK                              0xffffffff
#define ADDA_ULCF_CFG_18_17_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_20_19 */
#define ADDA_ULCF_CFG_20_19_SFT                               0
#define ADDA_ULCF_CFG_20_19_MASK                              0xffffffff
#define ADDA_ULCF_CFG_20_19_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_22_21 */
#define ADDA_ULCF_CFG_22_21_SFT                               0
#define ADDA_ULCF_CFG_22_21_MASK                              0xffffffff
#define ADDA_ULCF_CFG_22_21_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_24_23 */
#define ADDA_ULCF_CFG_24_23_SFT                               0
#define ADDA_ULCF_CFG_24_23_MASK                              0xffffffff
#define ADDA_ULCF_CFG_24_23_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_26_25 */
#define ADDA_ULCF_CFG_26_25_SFT                               0
#define ADDA_ULCF_CFG_26_25_MASK                              0xffffffff
#define ADDA_ULCF_CFG_26_25_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_28_27 */
#define ADDA_ULCF_CFG_28_27_SFT                               0
#define ADDA_ULCF_CFG_28_27_MASK                              0xffffffff
#define ADDA_ULCF_CFG_28_27_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_30_29 */
#define ADDA_ULCF_CFG_30_29_SFT                               0
#define ADDA_ULCF_CFG_30_29_MASK                              0xffffffff
#define ADDA_ULCF_CFG_30_29_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_ULCF_CFG_32_31 */
#define ADDA_ULCF_CFG_32_31_SFT                               0
#define ADDA_ULCF_CFG_32_31_MASK                              0xffffffff
#define ADDA_ULCF_CFG_32_31_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL1_IP_VERSION */
#define ADDA_ULCF_IP_VERSION_SFT                              0
#define ADDA_ULCF_IP_VERSION_MASK                             0xffffffff
#define ADDA_ULCF_IP_VERSION_MASK_SFT                         (0xffffffff << 0)

/* AFE_ADDA_UL2_SRC_CON0 */
#define ULCF_CFG_EN_CTL_SFT                                   31
#define ULCF_CFG_EN_CTL_MASK                                  0x1
#define ULCF_CFG_EN_CTL_MASK_SFT                              (0x1 << 31)
#define UL_DMIC_PHASE_SEL_CH1_SFT                             27
#define UL_DMIC_PHASE_SEL_CH1_MASK                            0x7
#define UL_DMIC_PHASE_SEL_CH1_MASK_SFT                        (0x7 << 27)
#define UL_DMIC_PHASE_SEL_CH2_SFT                             24
#define UL_DMIC_PHASE_SEL_CH2_MASK                            0x7
#define UL_DMIC_PHASE_SEL_CH2_MASK_SFT                        (0x7 << 24)
#define UL_DMIC_TWO_WIRE_CTL_SFT                              23
#define UL_DMIC_TWO_WIRE_CTL_MASK                             0x1
#define UL_DMIC_TWO_WIRE_CTL_MASK_SFT                         (0x1 << 23)
#define UL_MODE_3P25M_CH2_CTL_SFT                             22
#define UL_MODE_3P25M_CH2_CTL_MASK                            0x1
#define UL_MODE_3P25M_CH2_CTL_MASK_SFT                        (0x1 << 22)
#define UL_MODE_3P25M_CH1_CTL_SFT                             21
#define UL_MODE_3P25M_CH1_CTL_MASK                            0x1
#define UL_MODE_3P25M_CH1_CTL_MASK_SFT                        (0x1 << 21)
#define UL_VOICE_MODE_CH1_CH2_CTL_SFT                         17
#define UL_VOICE_MODE_CH1_CH2_CTL_MASK                        0x7
#define UL_VOICE_MODE_CH1_CH2_CTL_MASK_SFT                    (0x7 << 17)
#define UL_AP_DMIC_ON_SFT                                     16
#define UL_AP_DMIC_ON_MASK                                    0x1
#define UL_AP_DMIC_ON_MASK_SFT                                (0x1 << 16)
#define DMIC_LOW_POWER_MODE_CTL_SFT                           14
#define DMIC_LOW_POWER_MODE_CTL_MASK                          0x3
#define DMIC_LOW_POWER_MODE_CTL_MASK_SFT                      (0x3 << 14)
#define UL_DISABLE_HW_CG_CTL_SFT                              12
#define UL_DISABLE_HW_CG_CTL_MASK                             0x1
#define UL_DISABLE_HW_CG_CTL_MASK_SFT                         (0x1 << 12)
#define AMIC_26M_SEL_CTL_SFT                                  11
#define AMIC_26M_SEL_CTL_MASK                                 0x1
#define AMIC_26M_SEL_CTL_MASK_SFT                             (0x1 << 11)
#define UL_IIR_ON_TMP_CTL_SFT                                 10
#define UL_IIR_ON_TMP_CTL_MASK                                0x1
#define UL_IIR_ON_TMP_CTL_MASK_SFT                            (0x1 << 10)
#define UL_IIRMODE_CTL_SFT                                    7
#define UL_IIRMODE_CTL_MASK                                   0x7
#define UL_IIRMODE_CTL_MASK_SFT                               (0x7 << 7)
#define DIGMIC_4P33M_SEL_SFT                                  6
#define DIGMIC_4P33M_SEL_MASK                                 0x1
#define DIGMIC_4P33M_SEL_MASK_SFT                             (0x1 << 6)
#define DIGMIC_3P25M_1P625M_SEL_CTL_SFT                       5
#define DIGMIC_3P25M_1P625M_SEL_CTL_MASK                      0x1
#define DIGMIC_3P25M_1P625M_SEL_CTL_MASK_SFT                  (0x1 << 5)
#define AMIC_6P5M_SEL_CTL_SFT                                 4
#define AMIC_6P5M_SEL_CTL_MASK                                0x1
#define AMIC_6P5M_SEL_CTL_MASK_SFT                            (0x1 << 4)
#define AMIC_1P625M_SEL_CTL_SFT                               3
#define AMIC_1P625M_SEL_CTL_MASK                              0x1
#define AMIC_1P625M_SEL_CTL_MASK_SFT                          (0x1 << 3)
#define UL_LOOP_BACK_MODE_CTL_SFT                             2
#define UL_LOOP_BACK_MODE_CTL_MASK                            0x1
#define UL_LOOP_BACK_MODE_CTL_MASK_SFT                        (0x1 << 2)
#define UL_SDM_3_LEVEL_CTL_SFT                                1
#define UL_SDM_3_LEVEL_CTL_MASK                               0x1
#define UL_SDM_3_LEVEL_CTL_MASK_SFT                           (0x1 << 1)
#define UL_SRC_ON_TMP_CTL_SFT                                 0
#define UL_SRC_ON_TMP_CTL_MASK                                0x1
#define UL_SRC_ON_TMP_CTL_MASK_SFT                            (0x1 << 0)

/* AFE_ADDA_UL2_SRC_CON1 */
#define ADDA_UL_GAIN_VALUE_SFT                                16
#define ADDA_UL_GAIN_VALUE_MASK                               0xffff
#define ADDA_UL_GAIN_VALUE_MASK_SFT                           (0xffff << 16)
#define ADDA_UL_POSTIVEGAIN_SFT                               12
#define ADDA_UL_POSTIVEGAIN_MASK                              0x7
#define ADDA_UL_POSTIVEGAIN_MASK_SFT                          (0x7 << 12)
#define ADDA_UL_ODDTAP_MODE_SFT                               11
#define ADDA_UL_ODDTAP_MODE_MASK                              0x1
#define ADDA_UL_ODDTAP_MODE_MASK_SFT                          (0x1 << 11)
#define ADDA_UL_HALF_TAP_NUM_SFT                              5
#define ADDA_UL_HALF_TAP_NUM_MASK                             0x3f
#define ADDA_UL_HALF_TAP_NUM_MASK_SFT                         (0x3f << 5)
#define FIFO_SOFT_RST_SFT                                     4
#define FIFO_SOFT_RST_MASK                                    0x1
#define FIFO_SOFT_RST_MASK_SFT                                (0x1 << 4)
#define FIFO_SOFT_RST_EN_SFT                                  3
#define FIFO_SOFT_RST_EN_MASK                                 0x1
#define FIFO_SOFT_RST_EN_MASK_SFT                             (0x1 << 3)
#define LR_SWAP_SFT                                           2
#define LR_SWAP_MASK                                          0x1
#define LR_SWAP_MASK_SFT                                      (0x1 << 2)
#define GAIN_MODE_SFT                                         0
#define GAIN_MODE_MASK                                        0x3
#define GAIN_MODE_MASK_SFT                                    (0x3 << 0)

/* AFE_ADDA_UL2_SRC_CON2 */
#define C_DAC_EN_CTL_SFT                                      27
#define C_DAC_EN_CTL_MASK                                     0x1
#define C_DAC_EN_CTL_MASK_SFT                                 (0x1 << 27)
#define C_MUTE_SW_CTL_SFT                                     26
#define C_MUTE_SW_CTL_MASK                                    0x1
#define C_MUTE_SW_CTL_MASK_SFT                                (0x1 << 26)
#define C_AMP_DIV_CH2_CTL_SFT                                 21
#define C_AMP_DIV_CH2_CTL_MASK                                0x7
#define C_AMP_DIV_CH2_CTL_MASK_SFT                            (0x7 << 21)
#define C_FREQ_DIV_CH2_CTL_SFT                                16
#define C_FREQ_DIV_CH2_CTL_MASK                               0x1f
#define C_FREQ_DIV_CH2_CTL_MASK_SFT                           (0x1f << 16)
#define C_SINE_MODE_CH2_CTL_SFT                               12
#define C_SINE_MODE_CH2_CTL_MASK                              0xf
#define C_SINE_MODE_CH2_CTL_MASK_SFT                          (0xf << 12)
#define C_AMP_DIV_CH1_CTL_SFT                                 9
#define C_AMP_DIV_CH1_CTL_MASK                                0x7
#define C_AMP_DIV_CH1_CTL_MASK_SFT                            (0x7 << 9)
#define C_FREQ_DIV_CH1_CTL_SFT                                4
#define C_FREQ_DIV_CH1_CTL_MASK                               0x1f
#define C_FREQ_DIV_CH1_CTL_MASK_SFT                           (0x1f << 4)
#define C_SINE_MODE_CH1_CTL_SFT                               0
#define C_SINE_MODE_CH1_CTL_MASK                              0xf
#define C_SINE_MODE_CH1_CTL_MASK_SFT                          (0xf << 0)

/* AFE_ADDA_UL2_SRC_DEBUG */
#define UL_SLT_CNT_FLAG_RESET_CTL_SFT                         16
#define UL_SLT_CNT_FLAG_RESET_CTL_MASK                        0x1
#define UL_SLT_CNT_FLAG_RESET_CTL_MASK_SFT                    (0x1 << 16)
#define FIFO_DIGMIC_TESTIN_SFT                                12
#define FIFO_DIGMIC_TESTIN_MASK                               0x3
#define FIFO_DIGMIC_TESTIN_MASK_SFT                           (0x3 << 12)
#define FIFO_DIGMIC_WDATA_TESTEN_SFT                          11
#define FIFO_DIGMIC_WDATA_TESTEN_MASK                         0x1
#define FIFO_DIGMIC_WDATA_TESTEN_MASK_SFT                     (0x1 << 11)
#define SLT_CNT_THD_CTL_SFT                                   0
#define SLT_CNT_THD_CTL_MASK                                  0x7ff
#define SLT_CNT_THD_CTL_MASK_SFT                              (0x7ff << 0)

/* AFE_ADDA_UL2_SRC_DEBUG_MON0 */
#define SLT_CNT_FLAG_CTL_SFT                                  16
#define SLT_CNT_FLAG_CTL_MASK                                 0x1
#define SLT_CNT_FLAG_CTL_MASK_SFT                             (0x1 << 16)
#define SLT_COUNTER_CTL_SFT                                   0
#define SLT_COUNTER_CTL_MASK                                  0x7ff
#define SLT_COUNTER_CTL_MASK_SFT                              (0x7ff << 0)

/* AFE_ADDA_UL2_IIR_COEF_02_01 */
#define ADDA_IIR_COEF_02_01_SFT                               0
#define ADDA_IIR_COEF_02_01_MASK                              0xffffffff
#define ADDA_IIR_COEF_02_01_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_IIR_COEF_04_03 */
#define ADDA_IIR_COEF_04_03_SFT                               0
#define ADDA_IIR_COEF_04_03_MASK                              0xffffffff
#define ADDA_IIR_COEF_04_03_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_IIR_COEF_06_05 */
#define ADDA_IIR_COEF_06_05_SFT                               0
#define ADDA_IIR_COEF_06_05_MASK                              0xffffffff
#define ADDA_IIR_COEF_06_05_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_IIR_COEF_08_07 */
#define ADDA_IIR_COEF_08_07_SFT                               0
#define ADDA_IIR_COEF_08_07_MASK                              0xffffffff
#define ADDA_IIR_COEF_08_07_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_IIR_COEF_10_09 */
#define ADDA_IIR_COEF_10_09_SFT                               0
#define ADDA_IIR_COEF_10_09_MASK                              0xffffffff
#define ADDA_IIR_COEF_10_09_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_02_01 */
#define ADDA_ULCF_CFG_02_01_SFT                               0
#define ADDA_ULCF_CFG_02_01_MASK                              0xffffffff
#define ADDA_ULCF_CFG_02_01_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_04_03 */
#define ADDA_ULCF_CFG_04_03_SFT                               0
#define ADDA_ULCF_CFG_04_03_MASK                              0xffffffff
#define ADDA_ULCF_CFG_04_03_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_06_05 */
#define ADDA_ULCF_CFG_06_05_SFT                               0
#define ADDA_ULCF_CFG_06_05_MASK                              0xffffffff
#define ADDA_ULCF_CFG_06_05_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_08_07 */
#define ADDA_ULCF_CFG_08_07_SFT                               0
#define ADDA_ULCF_CFG_08_07_MASK                              0xffffffff
#define ADDA_ULCF_CFG_08_07_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_10_09 */
#define ADDA_ULCF_CFG_10_09_SFT                               0
#define ADDA_ULCF_CFG_10_09_MASK                              0xffffffff
#define ADDA_ULCF_CFG_10_09_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_12_11 */
#define ADDA_ULCF_CFG_12_11_SFT                               0
#define ADDA_ULCF_CFG_12_11_MASK                              0xffffffff
#define ADDA_ULCF_CFG_12_11_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_14_13 */
#define ADDA_ULCF_CFG_14_13_SFT                               0
#define ADDA_ULCF_CFG_14_13_MASK                              0xffffffff
#define ADDA_ULCF_CFG_14_13_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_16_15 */
#define ADDA_ULCF_CFG_16_15_SFT                               0
#define ADDA_ULCF_CFG_16_15_MASK                              0xffffffff
#define ADDA_ULCF_CFG_16_15_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_18_17 */
#define ADDA_ULCF_CFG_18_17_SFT                               0
#define ADDA_ULCF_CFG_18_17_MASK                              0xffffffff
#define ADDA_ULCF_CFG_18_17_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_20_19 */
#define ADDA_ULCF_CFG_20_19_SFT                               0
#define ADDA_ULCF_CFG_20_19_MASK                              0xffffffff
#define ADDA_ULCF_CFG_20_19_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_22_21 */
#define ADDA_ULCF_CFG_22_21_SFT                               0
#define ADDA_ULCF_CFG_22_21_MASK                              0xffffffff
#define ADDA_ULCF_CFG_22_21_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_24_23 */
#define ADDA_ULCF_CFG_24_23_SFT                               0
#define ADDA_ULCF_CFG_24_23_MASK                              0xffffffff
#define ADDA_ULCF_CFG_24_23_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_26_25 */
#define ADDA_ULCF_CFG_26_25_SFT                               0
#define ADDA_ULCF_CFG_26_25_MASK                              0xffffffff
#define ADDA_ULCF_CFG_26_25_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_28_27 */
#define ADDA_ULCF_CFG_28_27_SFT                               0
#define ADDA_ULCF_CFG_28_27_MASK                              0xffffffff
#define ADDA_ULCF_CFG_28_27_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_30_29 */
#define ADDA_ULCF_CFG_30_29_SFT                               0
#define ADDA_ULCF_CFG_30_29_MASK                              0xffffffff
#define ADDA_ULCF_CFG_30_29_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_ULCF_CFG_32_31 */
#define ADDA_ULCF_CFG_32_31_SFT                               0
#define ADDA_ULCF_CFG_32_31_MASK                              0xffffffff
#define ADDA_ULCF_CFG_32_31_MASK_SFT                          (0xffffffff << 0)

/* AFE_ADDA_UL2_IP_VERSION */
#define ADDA_ULCF_IP_VERSION_SFT                              0
#define ADDA_ULCF_IP_VERSION_MASK                             0xffffffff
#define ADDA_ULCF_IP_VERSION_MASK_SFT                         (0xffffffff << 0)

/* AFE_ADDA_PROXIMITY_CON0 */
#define PROXIMITY_CH1_ON_SFT                                  12
#define PROXIMITY_CH1_ON_MASK                                 0x1
#define PROXIMITY_CH1_ON_MASK_SFT                             (0x1 << 12)
#define PROXIMITY_CH1_SEL_SFT                                 8
#define PROXIMITY_CH1_SEL_MASK                                0xf
#define PROXIMITY_CH1_SEL_MASK_SFT                            (0xf << 8)
#define PROXIMITY_CH2_ON_SFT                                  4
#define PROXIMITY_CH2_ON_MASK                                 0x1
#define PROXIMITY_CH2_ON_MASK_SFT                             (0x1 << 4)
#define PROXIMITY_CH2_SEL_SFT                                 0
#define PROXIMITY_CH2_SEL_MASK                                0xf
#define PROXIMITY_CH2_SEL_MASK_SFT                            (0xf << 0)

/* AFE_ADDA_ULSRC_PHASE_CON0 */
#define DMIC1_PHASE_FCLK_SEL_SFT                              30
#define DMIC1_PHASE_FCLK_SEL_MASK                             0x3
#define DMIC1_PHASE_FCLK_SEL_MASK_SFT                         (0x3 << 30)
#define DMIC0_PHASE_FCLK_SEL_SFT                              28
#define DMIC0_PHASE_FCLK_SEL_MASK                             0x3
#define DMIC0_PHASE_FCLK_SEL_MASK_SFT                         (0x3 << 28)
#define UL3_PHASE_FCLK_SEL_SFT                                26
#define UL3_PHASE_FCLK_SEL_MASK                               0x3
#define UL3_PHASE_FCLK_SEL_MASK_SFT                           (0x3 << 26)
#define UL2_PHASE_FCLK_SEL_SFT                                24
#define UL2_PHASE_FCLK_SEL_MASK                               0x3
#define UL2_PHASE_FCLK_SEL_MASK_SFT                           (0x3 << 24)
#define UL1_PHASE_FCLK_SEL_SFT                                22
#define UL1_PHASE_FCLK_SEL_MASK                               0x3
#define UL1_PHASE_FCLK_SEL_MASK_SFT                           (0x3 << 22)
#define UL0_PHASE_FCLK_SEL_SFT                                20
#define UL0_PHASE_FCLK_SEL_MASK                               0x3
#define UL0_PHASE_FCLK_SEL_MASK_SFT                           (0x3 << 20)
#define UL_PHASE_SYNC_FCLK_2_ON_SFT                           18
#define UL_PHASE_SYNC_FCLK_2_ON_MASK                          0x1
#define UL_PHASE_SYNC_FCLK_2_ON_MASK_SFT                      (0x1 << 18)
#define UL_PHASE_SYNC_FCLK_1_ON_SFT                           17
#define UL_PHASE_SYNC_FCLK_1_ON_MASK                          0x1
#define UL_PHASE_SYNC_FCLK_1_ON_MASK_SFT                      (0x1 << 17)
#define UL_PHASE_SYNC_FCLK_0_ON_SFT                           16
#define UL_PHASE_SYNC_FCLK_0_ON_MASK                          0x1
#define UL_PHASE_SYNC_FCLK_0_ON_MASK_SFT                      (0x1 << 16)
#define DMIC1_PHASE_HCLK_SEL_SFT                              14
#define DMIC1_PHASE_HCLK_SEL_MASK                             0x3
#define DMIC1_PHASE_HCLK_SEL_MASK_SFT                         (0x3 << 14)
#define DMIC0_PHASE_HCLK_SEL_SFT                              12
#define DMIC0_PHASE_HCLK_SEL_MASK                             0x3
#define DMIC0_PHASE_HCLK_SEL_MASK_SFT                         (0x3 << 12)
#define UL3_PHASE_HCLK_SEL_SFT                                10
#define UL3_PHASE_HCLK_SEL_MASK                               0x3
#define UL3_PHASE_HCLK_SEL_MASK_SFT                           (0x3 << 10)
#define UL2_PHASE_HCLK_SEL_SFT                                8
#define UL2_PHASE_HCLK_SEL_MASK                               0x3
#define UL2_PHASE_HCLK_SEL_MASK_SFT                           (0x3 << 8)
#define UL1_PHASE_HCLK_SEL_SFT                                6
#define UL1_PHASE_HCLK_SEL_MASK                               0x3
#define UL1_PHASE_HCLK_SEL_MASK_SFT                           (0x3 << 6)
#define UL0_PHASE_HCLK_SEL_SFT                                4
#define UL0_PHASE_HCLK_SEL_MASK                               0x3
#define UL0_PHASE_HCLK_SEL_MASK_SFT                           (0x3 << 4)
#define UL_PHASE_SYNC_HCLK_2_ON_SFT                           2
#define UL_PHASE_SYNC_HCLK_2_ON_MASK                          0x1
#define UL_PHASE_SYNC_HCLK_2_ON_MASK_SFT                      (0x1 << 2)
#define UL_PHASE_SYNC_HCLK_1_ON_SFT                           1
#define UL_PHASE_SYNC_HCLK_1_ON_MASK                          0x1
#define UL_PHASE_SYNC_HCLK_1_ON_MASK_SFT                      (0x1 << 1)
#define UL_PHASE_SYNC_HCLK_0_ON_SFT                           0
#define UL_PHASE_SYNC_HCLK_0_ON_MASK                          0x1
#define UL_PHASE_SYNC_HCLK_0_ON_MASK_SFT                      (0x1 << 0)

/* AFE_ADDA_ULSRC_PHASE_CON1 */
#define DMIC_CLK_PHASE_SYNC_SET_SFT                           31
#define DMIC_CLK_PHASE_SYNC_SET_MASK                          0x1
#define DMIC_CLK_PHASE_SYNC_SET_MASK_SFT                      (0x1 << 31)
#define DMIC1_PHASE_SYNC_FCLK_SET_SFT                         11
#define DMIC1_PHASE_SYNC_FCLK_SET_MASK                        0x1
#define DMIC1_PHASE_SYNC_FCLK_SET_MASK_SFT                    (0x1 << 11)
#define DMIC1_PHASE_SYNC_HCLK_SET_SFT                         10
#define DMIC1_PHASE_SYNC_HCLK_SET_MASK                        0x1
#define DMIC1_PHASE_SYNC_HCLK_SET_MASK_SFT                    (0x1 << 10)
#define DMIC0_PHASE_SYNC_FCLK_SET_SFT                         9
#define DMIC0_PHASE_SYNC_FCLK_SET_MASK                        0x1
#define DMIC0_PHASE_SYNC_FCLK_SET_MASK_SFT                    (0x1 << 9)
#define DMIC0_PHASE_SYNC_HCLK_SET_SFT                         8
#define DMIC0_PHASE_SYNC_HCLK_SET_MASK                        0x1
#define DMIC0_PHASE_SYNC_HCLK_SET_MASK_SFT                    (0x1 << 8)
#define UL3_PHASE_SYNC_FCLK_SET_SFT                           7
#define UL3_PHASE_SYNC_FCLK_SET_MASK                          0x1
#define UL3_PHASE_SYNC_FCLK_SET_MASK_SFT                      (0x1 << 7)
#define UL3_PHASE_SYNC_HCLK_SET_SFT                           6
#define UL3_PHASE_SYNC_HCLK_SET_MASK                          0x1
#define UL3_PHASE_SYNC_HCLK_SET_MASK_SFT                      (0x1 << 6)
#define UL2_PHASE_SYNC_FCLK_SET_SFT                           5
#define UL2_PHASE_SYNC_FCLK_SET_MASK                          0x1
#define UL2_PHASE_SYNC_FCLK_SET_MASK_SFT                      (0x1 << 5)
#define UL2_PHASE_SYNC_HCLK_SET_SFT                           4
#define UL2_PHASE_SYNC_HCLK_SET_MASK                          0x1
#define UL2_PHASE_SYNC_HCLK_SET_MASK_SFT                      (0x1 << 4)
#define UL1_PHASE_SYNC_FCLK_SET_SFT                           3
#define UL1_PHASE_SYNC_FCLK_SET_MASK                          0x1
#define UL1_PHASE_SYNC_FCLK_SET_MASK_SFT                      (0x1 << 3)
#define UL1_PHASE_SYNC_HCLK_SET_SFT                           2
#define UL1_PHASE_SYNC_HCLK_SET_MASK                          0x1
#define UL1_PHASE_SYNC_HCLK_SET_MASK_SFT                      (0x1 << 2)
#define UL0_PHASE_SYNC_FCLK_SET_SFT                           1
#define UL0_PHASE_SYNC_FCLK_SET_MASK                          0x1
#define UL0_PHASE_SYNC_FCLK_SET_MASK_SFT                      (0x1 << 1)
#define UL0_PHASE_SYNC_HCLK_SET_SFT                           0
#define UL0_PHASE_SYNC_HCLK_SET_MASK                          0x1
#define UL0_PHASE_SYNC_HCLK_SET_MASK_SFT                      (0x1 << 0)

/* AFE_ADDA_ULSRC_PHASE_CON2 */
#define DMIC1_PHASE_SYNC_1X_EN_SEL_SFT                        26
#define DMIC1_PHASE_SYNC_1X_EN_SEL_MASK                       0x3
#define DMIC1_PHASE_SYNC_1X_EN_SEL_MASK_SFT                   (0x3 << 26)
#define DMIC0_PHASE_SYNC_1X_EN_SEL_SFT                        24
#define DMIC0_PHASE_SYNC_1X_EN_SEL_MASK                       0x3
#define DMIC0_PHASE_SYNC_1X_EN_SEL_MASK_SFT                   (0x3 << 24)
#define UL3_PHASE_SYNC_1X_EN_SEL_SFT                          22
#define UL3_PHASE_SYNC_1X_EN_SEL_MASK                         0x3
#define UL3_PHASE_SYNC_1X_EN_SEL_MASK_SFT                     (0x3 << 22)
#define UL2_PHASE_SYNC_1X_EN_SEL_SFT                          20
#define UL2_PHASE_SYNC_1X_EN_SEL_MASK                         0x3
#define UL2_PHASE_SYNC_1X_EN_SEL_MASK_SFT                     (0x3 << 20)
#define UL1_PHASE_SYNC_1X_EN_SEL_SFT                          18
#define UL1_PHASE_SYNC_1X_EN_SEL_MASK                         0x3
#define UL1_PHASE_SYNC_1X_EN_SEL_MASK_SFT                     (0x3 << 18)
#define UL0_PHASE_SYNC_1X_EN_SEL_SFT                          16
#define UL0_PHASE_SYNC_1X_EN_SEL_MASK                         0x3
#define UL0_PHASE_SYNC_1X_EN_SEL_MASK_SFT                     (0x3 << 16)
#define UL_PHASE_SYNC_FCLK_1X_EN_2_ON_SFT                     5
#define UL_PHASE_SYNC_FCLK_1X_EN_2_ON_MASK                    0x1
#define UL_PHASE_SYNC_FCLK_1X_EN_2_ON_MASK_SFT                (0x1 << 5)
#define UL_PHASE_SYNC_FCLK_1X_EN_1_ON_SFT                     4
#define UL_PHASE_SYNC_FCLK_1X_EN_1_ON_MASK                    0x1
#define UL_PHASE_SYNC_FCLK_1X_EN_1_ON_MASK_SFT                (0x1 << 4)
#define UL_PHASE_SYNC_FCLK_1X_EN_0_ON_SFT                     3
#define UL_PHASE_SYNC_FCLK_1X_EN_0_ON_MASK                    0x1
#define UL_PHASE_SYNC_FCLK_1X_EN_0_ON_MASK_SFT                (0x1 << 3)
#define UL_PHASE_SYNC_HCLK_1X_EN_2_ON_SFT                     2
#define UL_PHASE_SYNC_HCLK_1X_EN_2_ON_MASK                    0x1
#define UL_PHASE_SYNC_HCLK_1X_EN_2_ON_MASK_SFT                (0x1 << 2)
#define UL_PHASE_SYNC_HCLK_1X_EN_1_ON_SFT                     1
#define UL_PHASE_SYNC_HCLK_1X_EN_1_ON_MASK                    0x1
#define UL_PHASE_SYNC_HCLK_1X_EN_1_ON_MASK_SFT                (0x1 << 1)
#define UL_PHASE_SYNC_HCLK_1X_EN_0_ON_SFT                     0
#define UL_PHASE_SYNC_HCLK_1X_EN_0_ON_MASK                    0x1
#define UL_PHASE_SYNC_HCLK_1X_EN_0_ON_MASK_SFT                (0x1 << 0)

/* AFE_ADDA_ULSRC_PHASE_CON3 */
#define DMIC1_PHASE_SYNC_SOFT_RST_SEL_SFT                     26
#define DMIC1_PHASE_SYNC_SOFT_RST_SEL_MASK                    0x3
#define DMIC1_PHASE_SYNC_SOFT_RST_SEL_MASK_SFT                (0x3 << 26)
#define DMIC0_PHASE_SYNC_SOFT_RST_SEL_SFT                     24
#define DMIC0_PHASE_SYNC_SOFT_RST_SEL_MASK                    0x3
#define DMIC0_PHASE_SYNC_SOFT_RST_SEL_MASK_SFT                (0x3 << 24)
#define UL3_PHASE_SYNC_SOFT_RST_SEL_SFT                       22
#define UL3_PHASE_SYNC_SOFT_RST_SEL_MASK                      0x3
#define UL3_PHASE_SYNC_SOFT_RST_SEL_MASK_SFT                  (0x3 << 22)
#define UL2_PHASE_SYNC_SOFT_RST_SEL_SFT                       20
#define UL2_PHASE_SYNC_SOFT_RST_SEL_MASK                      0x3
#define UL2_PHASE_SYNC_SOFT_RST_SEL_MASK_SFT                  (0x3 << 20)
#define UL1_PHASE_SYNC_SOFT_RST_SEL_SFT                       18
#define UL1_PHASE_SYNC_SOFT_RST_SEL_MASK                      0x3
#define UL1_PHASE_SYNC_SOFT_RST_SEL_MASK_SFT                  (0x3 << 18)
#define UL0_PHASE_SYNC_SOFT_RST_SEL_SFT                       16
#define UL0_PHASE_SYNC_SOFT_RST_SEL_MASK                      0x3
#define UL0_PHASE_SYNC_SOFT_RST_SEL_MASK_SFT                  (0x3 << 16)
#define DMIC1_PHASE_SYNC_CH1_FIFO_SEL_SFT                     13
#define DMIC1_PHASE_SYNC_CH1_FIFO_SEL_MASK                    0x1
#define DMIC1_PHASE_SYNC_CH1_FIFO_SEL_MASK_SFT                (0x1 << 13)
#define DMIC0_PHASE_SYNC_CH1_FIFO_SEL_SFT                     12
#define DMIC0_PHASE_SYNC_CH1_FIFO_SEL_MASK                    0x1
#define DMIC0_PHASE_SYNC_CH1_FIFO_SEL_MASK_SFT                (0x1 << 12)
#define UL3_PHASE_SYNC_CH1_FIFO_SEL_SFT                       11
#define UL3_PHASE_SYNC_CH1_FIFO_SEL_MASK                      0x1
#define UL3_PHASE_SYNC_CH1_FIFO_SEL_MASK_SFT                  (0x1 << 11)
#define UL2_PHASE_SYNC_CH1_FIFO_SEL_SFT                       10
#define UL2_PHASE_SYNC_CH1_FIFO_SEL_MASK                      0x1
#define UL2_PHASE_SYNC_CH1_FIFO_SEL_MASK_SFT                  (0x1 << 10)
#define UL1_PHASE_SYNC_CH1_FIFO_SEL_SFT                       9
#define UL1_PHASE_SYNC_CH1_FIFO_SEL_MASK                      0x1
#define UL1_PHASE_SYNC_CH1_FIFO_SEL_MASK_SFT                  (0x1 << 9)
#define UL0_PHASE_SYNC_CH1_FIFO_SEL_SFT                       8
#define UL0_PHASE_SYNC_CH1_FIFO_SEL_MASK                      0x1
#define UL0_PHASE_SYNC_CH1_FIFO_SEL_MASK_SFT                  (0x1 << 8)
#define UL_PHASE_SYNC_SOFT_RST_EN_2_ON_SFT                    5
#define UL_PHASE_SYNC_SOFT_RST_EN_2_ON_MASK                   0x1
#define UL_PHASE_SYNC_SOFT_RST_EN_2_ON_MASK_SFT               (0x1 << 5)
#define UL_PHASE_SYNC_SOFT_RST_EN_1_ON_SFT                    4
#define UL_PHASE_SYNC_SOFT_RST_EN_1_ON_MASK                   0x1
#define UL_PHASE_SYNC_SOFT_RST_EN_1_ON_MASK_SFT               (0x1 << 4)
#define UL_PHASE_SYNC_SOFT_RST_EN_0_ON_SFT                    3
#define UL_PHASE_SYNC_SOFT_RST_EN_0_ON_MASK                   0x1
#define UL_PHASE_SYNC_SOFT_RST_EN_0_ON_MASK_SFT               (0x1 << 3)
#define UL_PHASE_SYNC_SOFT_RST_2_ON_SFT                       2
#define UL_PHASE_SYNC_SOFT_RST_2_ON_MASK                      0x1
#define UL_PHASE_SYNC_SOFT_RST_2_ON_MASK_SFT                  (0x1 << 2)
#define UL_PHASE_SYNC_SOFT_RST_1_ON_SFT                       1
#define UL_PHASE_SYNC_SOFT_RST_1_ON_MASK                      0x1
#define UL_PHASE_SYNC_SOFT_RST_1_ON_MASK_SFT                  (0x1 << 1)
#define UL_PHASE_SYNC_SOFT_RST_0_ON_SFT                       0
#define UL_PHASE_SYNC_SOFT_RST_0_ON_MASK                      0x1
#define UL_PHASE_SYNC_SOFT_RST_0_ON_MASK_SFT                  (0x1 << 0)

/* AFE_MTKAIF_IPM_VER_MON */
#define RG_MTKAIF_IPM_VER_MON_SFT                             0
#define RG_MTKAIF_IPM_VER_MON_MASK                            0xffffffff
#define RG_MTKAIF_IPM_VER_MON_MASK_SFT                        (0xffffffff << 0)

/* AFE_MTKAIF_MON_SEL */
#define RG_MTKAIF_MON_SEL_SFT                                 0
#define RG_MTKAIF_MON_SEL_MASK                                0xff
#define RG_MTKAIF_MON_SEL_MASK_SFT                            (0xff << 0)

/* AFE_MTKAIF_MON */
#define RG_MTKAIF_MON_SFT                                     0
#define RG_MTKAIF_MON_MASK                                    0xffffffff
#define RG_MTKAIF_MON_MASK_SFT                                (0xffffffff << 0)

/* AFE_MTKAIF0_CFG0 */
#define RG_MTKAIF0_RXIF_CLKINV_SFT                            31
#define RG_MTKAIF0_RXIF_CLKINV_MASK                           0x1
#define RG_MTKAIF0_RXIF_CLKINV_MASK_SFT                       (0x1 << 31)
#define RG_MTKAIF0_RXIF_BYPASS_SRC_SFT                        17
#define RG_MTKAIF0_RXIF_BYPASS_SRC_MASK                       0x1
#define RG_MTKAIF0_RXIF_BYPASS_SRC_MASK_SFT                   (0x1 << 17)
#define RG_MTKAIF0_RXIF_PROTOCOL2_SFT                         16
#define RG_MTKAIF0_RXIF_PROTOCOL2_MASK                        0x1
#define RG_MTKAIF0_RXIF_PROTOCOL2_MASK_SFT                    (0x1 << 16)
#define RG_MTKAIF0_TXIF_NLE_DEBUG_SFT                         8
#define RG_MTKAIF0_TXIF_NLE_DEBUG_MASK                        0x1
#define RG_MTKAIF0_TXIF_NLE_DEBUG_MASK_SFT                    (0x1 << 8)
#define RG_MTKAIF0_TXIF_BYPASS_SRC_SFT                        5
#define RG_MTKAIF0_TXIF_BYPASS_SRC_MASK                       0x1
#define RG_MTKAIF0_TXIF_BYPASS_SRC_MASK_SFT                   (0x1 << 5)
#define RG_MTKAIF0_TXIF_PROTOCOL2_SFT                         4
#define RG_MTKAIF0_TXIF_PROTOCOL2_MASK                        0x1
#define RG_MTKAIF0_TXIF_PROTOCOL2_MASK_SFT                    (0x1 << 4)
#define RG_MTKAIF0_TXIF_8TO5_SFT                              2
#define RG_MTKAIF0_TXIF_8TO5_MASK                             0x1
#define RG_MTKAIF0_TXIF_8TO5_MASK_SFT                         (0x1 << 2)
#define RG_MTKAIF0_RXIF_8TO5_SFT                              1
#define RG_MTKAIF0_RXIF_8TO5_MASK                             0x1
#define RG_MTKAIF0_RXIF_8TO5_MASK_SFT                         (0x1 << 1)
#define RG_MTKAIF0_TX2RX_LOOPBACK1_SFT                        0
#define RG_MTKAIF0_TX2RX_LOOPBACK1_MASK                       0x1
#define RG_MTKAIF0_TX2RX_LOOPBACK1_MASK_SFT                   (0x1 << 0)

/* AFE_MTKAIF0_TX_CFG0 */
#define RG_MTKAIF0_TXIF_NLE_FIFO_SWAP_SFT                     23
#define RG_MTKAIF0_TXIF_NLE_FIFO_SWAP_MASK                    0x1
#define RG_MTKAIF0_TXIF_NLE_FIFO_SWAP_MASK_SFT                (0x1 << 23)
#define RG_MTKAIF0_TXIF_NLE_FIFO_RSP_SFT                      20
#define RG_MTKAIF0_TXIF_NLE_FIFO_RSP_MASK                     0x7
#define RG_MTKAIF0_TXIF_NLE_FIFO_RSP_MASK_SFT                 (0x7 << 20)
#define RG_MTKAIF0_TXIF_FIFO_SWAP_SFT                         15
#define RG_MTKAIF0_TXIF_FIFO_SWAP_MASK                        0x1
#define RG_MTKAIF0_TXIF_FIFO_SWAP_MASK_SFT                    (0x1 << 15)
#define RG_MTKAIF0_TXIF_FIFO_RSP_SFT                          12
#define RG_MTKAIF0_TXIF_FIFO_RSP_MASK                         0x7
#define RG_MTKAIF0_TXIF_FIFO_RSP_MASK_SFT                     (0x7 << 12)
#define RG_MTKAIF0_TXIF_SYNC_WORD1_SFT                        4
#define RG_MTKAIF0_TXIF_SYNC_WORD1_MASK                       0x7
#define RG_MTKAIF0_TXIF_SYNC_WORD1_MASK_SFT                   (0x7 << 4)
#define RG_MTKAIF0_TXIF_SYNC_WORD0_SFT                        0
#define RG_MTKAIF0_TXIF_SYNC_WORD0_MASK                       0x7
#define RG_MTKAIF0_TXIF_SYNC_WORD0_MASK_SFT                   (0x7 << 0)

/* AFE_MTKAIF0_RX_CFG0 */
#define RG_MTKAIF0_RXIF_VOICE_MODE_SFT                        20
#define RG_MTKAIF0_RXIF_VOICE_MODE_MASK                       0xf
#define RG_MTKAIF0_RXIF_VOICE_MODE_MASK_SFT                   (0xf << 20)
#define RG_MTKAIF0_RXIF_DETECT_ON_SFT                         16
#define RG_MTKAIF0_RXIF_DETECT_ON_MASK                        0x1
#define RG_MTKAIF0_RXIF_DETECT_ON_MASK_SFT                    (0x1 << 16)
#define RG_MTKAIF0_RXIF_DATA_BIT_SFT                          8
#define RG_MTKAIF0_RXIF_DATA_BIT_MASK                         0x7
#define RG_MTKAIF0_RXIF_DATA_BIT_MASK_SFT                     (0x7 << 8)
#define RG_MTKAIF0_RXIF_FIFO_RSP_SFT                          4
#define RG_MTKAIF0_RXIF_FIFO_RSP_MASK                         0x7
#define RG_MTKAIF0_RXIF_FIFO_RSP_MASK_SFT                     (0x7 << 4)
#define RG_MTKAIF0_RXIF_DATA_MODE_SFT                         0
#define RG_MTKAIF0_RXIF_DATA_MODE_MASK                        0x1
#define RG_MTKAIF0_RXIF_DATA_MODE_MASK_SFT                    (0x1 << 0)

/* AFE_MTKAIF0_RX_CFG1 */
#define RG_MTKAIF0_RXIF_CLEAR_SYNC_FAIL_SFT                   28
#define RG_MTKAIF0_RXIF_CLEAR_SYNC_FAIL_MASK                  0x1
#define RG_MTKAIF0_RXIF_CLEAR_SYNC_FAIL_MASK_SFT              (0x1 << 28)
#define RG_MTKAIF0_RXIF_SYNC_CNT_TABLE_SFT                    16
#define RG_MTKAIF0_RXIF_SYNC_CNT_TABLE_MASK                   0xfff
#define RG_MTKAIF0_RXIF_SYNC_CNT_TABLE_MASK_SFT               (0xfff << 16)
#define RG_MTKAIF0_RXIF_SYNC_SEARCH_TABLE_SFT                 12
#define RG_MTKAIF0_RXIF_SYNC_SEARCH_TABLE_MASK                0xf
#define RG_MTKAIF0_RXIF_SYNC_SEARCH_TABLE_MASK_SFT            (0xf << 12)
#define RG_MTKAIF0_RXIF_INVALID_SYNC_CHECK_ROUND_SFT          8
#define RG_MTKAIF0_RXIF_INVALID_SYNC_CHECK_ROUND_MASK         0xf
#define RG_MTKAIF0_RXIF_INVALID_SYNC_CHECK_ROUND_MASK_SFT     (0xf << 8)
#define RG_MTKAIF0_RXIF_SYNC_CHECK_ROUND_SFT                  4
#define RG_MTKAIF0_RXIF_SYNC_CHECK_ROUND_MASK                 0xf
#define RG_MTKAIF0_RXIF_SYNC_CHECK_ROUND_MASK_SFT             (0xf << 4)

/* AFE_MTKAIF0_RX_CFG2 */
#define RG_MTKAIF0_RXIF_SYNC_WORD1_DISABLE_SFT                27
#define RG_MTKAIF0_RXIF_SYNC_WORD1_DISABLE_MASK               0x1
#define RG_MTKAIF0_RXIF_SYNC_WORD1_DISABLE_MASK_SFT           (0x1 << 27)
#define RG_MTKAIF0_RXIF_SYNC_WORD1_SFT                        24
#define RG_MTKAIF0_RXIF_SYNC_WORD1_MASK                       0x7
#define RG_MTKAIF0_RXIF_SYNC_WORD1_MASK_SFT                   (0x7 << 24)
#define RG_MTKAIF0_RXIF_SYNC_WORD0_DISABLE_SFT                23
#define RG_MTKAIF0_RXIF_SYNC_WORD0_DISABLE_MASK               0x1
#define RG_MTKAIF0_RXIF_SYNC_WORD0_DISABLE_MASK_SFT           (0x1 << 23)
#define RG_MTKAIF0_RXIF_SYNC_WORD0_SFT                        20
#define RG_MTKAIF0_RXIF_SYNC_WORD0_MASK                       0x7
#define RG_MTKAIF0_RXIF_SYNC_WORD0_MASK_SFT                   (0x7 << 20)
#define RG_MTKAIF0_RXIF_DELAY_CYCLE_SFT                       12
#define RG_MTKAIF0_RXIF_DELAY_CYCLE_MASK                      0xf
#define RG_MTKAIF0_RXIF_DELAY_CYCLE_MASK_SFT                  (0xf << 12)
#define RG_MTKAIF0_RXIF_DELAY_DATA_SFT                        8
#define RG_MTKAIF0_RXIF_DELAY_DATA_MASK                       0x1
#define RG_MTKAIF0_RXIF_DELAY_DATA_MASK_SFT                   (0x1 << 8)

/* AFE_MTKAIF1_CFG0 */
#define RG_MTKAIF1_RXIF_CLKINV_ADC_SFT                        31
#define RG_MTKAIF1_RXIF_CLKINV_ADC_MASK                       0x1
#define RG_MTKAIF1_RXIF_CLKINV_ADC_MASK_SFT                   (0x1 << 31)
#define RG_MTKAIF1_RXIF_BYPASS_SRC_SFT                        17
#define RG_MTKAIF1_RXIF_BYPASS_SRC_MASK                       0x1
#define RG_MTKAIF1_RXIF_BYPASS_SRC_MASK_SFT                   (0x1 << 17)
#define RG_MTKAIF1_RXIF_PROTOCOL2_SFT                         16
#define RG_MTKAIF1_RXIF_PROTOCOL2_MASK                        0x1
#define RG_MTKAIF1_RXIF_PROTOCOL2_MASK_SFT                    (0x1 << 16)
#define RG_MTKAIF1_TXIF_NLE_DEBUG_SFT                         8
#define RG_MTKAIF1_TXIF_NLE_DEBUG_MASK                        0x1
#define RG_MTKAIF1_TXIF_NLE_DEBUG_MASK_SFT                    (0x1 << 8)
#define RG_MTKAIF1_TXIF_BYPASS_SRC_SFT                        5
#define RG_MTKAIF1_TXIF_BYPASS_SRC_MASK                       0x1
#define RG_MTKAIF1_TXIF_BYPASS_SRC_MASK_SFT                   (0x1 << 5)
#define RG_MTKAIF1_TXIF_PROTOCOL2_SFT                         4
#define RG_MTKAIF1_TXIF_PROTOCOL2_MASK                        0x1
#define RG_MTKAIF1_TXIF_PROTOCOL2_MASK_SFT                    (0x1 << 4)
#define RG_MTKAIF1_TXIF_8TO5_SFT                              2
#define RG_MTKAIF1_TXIF_8TO5_MASK                             0x1
#define RG_MTKAIF1_TXIF_8TO5_MASK_SFT                         (0x1 << 2)
#define RG_MTKAIF1_RXIF_8TO5_SFT                              1
#define RG_MTKAIF1_RXIF_8TO5_MASK                             0x1
#define RG_MTKAIF1_RXIF_8TO5_MASK_SFT                         (0x1 << 1)
#define RG_MTKAIF1_IF_LOOPBACK1_SFT                           0
#define RG_MTKAIF1_IF_LOOPBACK1_MASK                          0x1
#define RG_MTKAIF1_IF_LOOPBACK1_MASK_SFT                      (0x1 << 0)

/* AFE_MTKAIF1_TX_CFG0 */
#define RG_MTKAIF1_TXIF_NLE_FIFO_SWAP_SFT                     23
#define RG_MTKAIF1_TXIF_NLE_FIFO_SWAP_MASK                    0x1
#define RG_MTKAIF1_TXIF_NLE_FIFO_SWAP_MASK_SFT                (0x1 << 23)
#define RG_MTKAIF1_TXIF_NLE_FIFO_RSP_SFT                      20
#define RG_MTKAIF1_TXIF_NLE_FIFO_RSP_MASK                     0x7
#define RG_MTKAIF1_TXIF_NLE_FIFO_RSP_MASK_SFT                 (0x7 << 20)
#define RG_MTKAIF1_TXIF_FIFO_SWAP_SFT                         15
#define RG_MTKAIF1_TXIF_FIFO_SWAP_MASK                        0x1
#define RG_MTKAIF1_TXIF_FIFO_SWAP_MASK_SFT                    (0x1 << 15)
#define RG_MTKAIF1_TXIF_FIFO_RSP_SFT                          12
#define RG_MTKAIF1_TXIF_FIFO_RSP_MASK                         0x7
#define RG_MTKAIF1_TXIF_FIFO_RSP_MASK_SFT                     (0x7 << 12)
#define RG_MTKAIF1_TXIF_SYNC_WORD1_SFT                        4
#define RG_MTKAIF1_TXIF_SYNC_WORD1_MASK                       0x7
#define RG_MTKAIF1_TXIF_SYNC_WORD1_MASK_SFT                   (0x7 << 4)
#define RG_MTKAIF1_TXIF_SYNC_WORD0_SFT                        0
#define RG_MTKAIF1_TXIF_SYNC_WORD0_MASK                       0x7
#define RG_MTKAIF1_TXIF_SYNC_WORD0_MASK_SFT                   (0x7 << 0)

/* AFE_MTKAIF1_RX_CFG0 */
#define RG_MTKAIF1_RXIF_VOICE_MODE_SFT                        20
#define RG_MTKAIF1_RXIF_VOICE_MODE_MASK                       0xf
#define RG_MTKAIF1_RXIF_VOICE_MODE_MASK_SFT                   (0xf << 20)
#define RG_MTKAIF1_RXIF_DETECT_ON_SFT                         16
#define RG_MTKAIF1_RXIF_DETECT_ON_MASK                        0x1
#define RG_MTKAIF1_RXIF_DETECT_ON_MASK_SFT                    (0x1 << 16)
#define RG_MTKAIF1_RXIF_DATA_BIT_SFT                          8
#define RG_MTKAIF1_RXIF_DATA_BIT_MASK                         0x7
#define RG_MTKAIF1_RXIF_DATA_BIT_MASK_SFT                     (0x7 << 8)
#define RG_MTKAIF1_RXIF_FIFO_RSP_SFT                          4
#define RG_MTKAIF1_RXIF_FIFO_RSP_MASK                         0x7
#define RG_MTKAIF1_RXIF_FIFO_RSP_MASK_SFT                     (0x7 << 4)
#define RG_MTKAIF1_RXIF_DATA_MODE_SFT                         0
#define RG_MTKAIF1_RXIF_DATA_MODE_MASK                        0x1
#define RG_MTKAIF1_RXIF_DATA_MODE_MASK_SFT                    (0x1 << 0)

/* AFE_MTKAIF1_RX_CFG1 */
#define RG_MTKAIF1_RXIF_CLEAR_SYNC_FAIL_SFT                   28
#define RG_MTKAIF1_RXIF_CLEAR_SYNC_FAIL_MASK                  0x1
#define RG_MTKAIF1_RXIF_CLEAR_SYNC_FAIL_MASK_SFT              (0x1 << 28)
#define RG_MTKAIF1_RXIF_SYNC_CNT_TABLE_SFT                    16
#define RG_MTKAIF1_RXIF_SYNC_CNT_TABLE_MASK                   0xfff
#define RG_MTKAIF1_RXIF_SYNC_CNT_TABLE_MASK_SFT               (0xfff << 16)
#define RG_MTKAIF1_RXIF_SYNC_SEARCH_TABLE_SFT                 12
#define RG_MTKAIF1_RXIF_SYNC_SEARCH_TABLE_MASK                0xf
#define RG_MTKAIF1_RXIF_SYNC_SEARCH_TABLE_MASK_SFT            (0xf << 12)
#define RG_MTKAIF1_RXIF_INVALID_SYNC_CHECK_ROUND_SFT          8
#define RG_MTKAIF1_RXIF_INVALID_SYNC_CHECK_ROUND_MASK         0xf
#define RG_MTKAIF1_RXIF_INVALID_SYNC_CHECK_ROUND_MASK_SFT     (0xf << 8)
#define RG_MTKAIF1_RXIF_SYNC_CHECK_ROUND_SFT                  4
#define RG_MTKAIF1_RXIF_SYNC_CHECK_ROUND_MASK                 0xf
#define RG_MTKAIF1_RXIF_SYNC_CHECK_ROUND_MASK_SFT             (0xf << 4)

/* AFE_MTKAIF1_RX_CFG2 */
#define RG_MTKAIF1_RXIF_SYNC_WORD1_DISABLE_SFT                27
#define RG_MTKAIF1_RXIF_SYNC_WORD1_DISABLE_MASK               0x1
#define RG_MTKAIF1_RXIF_SYNC_WORD1_DISABLE_MASK_SFT           (0x1 << 27)
#define RG_MTKAIF1_RXIF_SYNC_WORD1_SFT                        24
#define RG_MTKAIF1_RXIF_SYNC_WORD1_MASK                       0x7
#define RG_MTKAIF1_RXIF_SYNC_WORD1_MASK_SFT                   (0x7 << 24)
#define RG_MTKAIF1_RXIF_SYNC_WORD0_DISABLE_SFT                23
#define RG_MTKAIF1_RXIF_SYNC_WORD0_DISABLE_MASK               0x1
#define RG_MTKAIF1_RXIF_SYNC_WORD0_DISABLE_MASK_SFT           (0x1 << 23)
#define RG_MTKAIF1_RXIF_SYNC_WORD0_SFT                        20
#define RG_MTKAIF1_RXIF_SYNC_WORD0_MASK                       0x7
#define RG_MTKAIF1_RXIF_SYNC_WORD0_MASK_SFT                   (0x7 << 20)
#define RG_MTKAIF1_RXIF_DELAY_CYCLE_SFT                       12
#define RG_MTKAIF1_RXIF_DELAY_CYCLE_MASK                      0xf
#define RG_MTKAIF1_RXIF_DELAY_CYCLE_MASK_SFT                  (0xf << 12)
#define RG_MTKAIF1_RXIF_DELAY_DATA_SFT                        8
#define RG_MTKAIF1_RXIF_DELAY_DATA_MASK                       0x1
#define RG_MTKAIF1_RXIF_DELAY_DATA_MASK_SFT                   (0x1 << 8)

/* AFE_AUD_PAD_TOP_CFG0 */
#define AUD_PAD_TOP_FIFO_RSP_SFT                              4
#define AUD_PAD_TOP_FIFO_RSP_MASK                             0xf
#define AUD_PAD_TOP_FIFO_RSP_MASK_SFT                         (0xf << 4)
#define RG_RX_PROTOCOL2_SFT                                   3
#define RG_RX_PROTOCOL2_MASK                                  0x1
#define RG_RX_PROTOCOL2_MASK_SFT                              (0x1 << 3)
#define RG_RX_FIFO_ON_SFT                                     0
#define RG_RX_FIFO_ON_MASK                                    0x1
#define RG_RX_FIFO_ON_MASK_SFT                                (0x1 << 0)

/* AFE_AUD_PAD_TOP_MON */
#define AUD_PAD_TOP_MON_SFT                                   0
#define AUD_PAD_TOP_MON_MASK                                  0xffff
#define AUD_PAD_TOP_MON_MASK_SFT                              (0xffff << 0)

/* AFE_ADDA_MTKAIFV4_TX_CFG0 */
#define MTKAIFV4_TXIF_EN_SEL_SFT                              12
#define MTKAIFV4_TXIF_EN_SEL_MASK                             0x1
#define MTKAIFV4_TXIF_EN_SEL_MASK_SFT                         (0x1 << 12)
#define MTKAIFV4_TXIF_V4_SFT                                  11
#define MTKAIFV4_TXIF_V4_MASK                                 0x1
#define MTKAIFV4_TXIF_V4_MASK_SFT                             (0x1 << 11)
#define MTKAIFV4_ADDA6_OUT_EN_SEL_SFT                         10
#define MTKAIFV4_ADDA6_OUT_EN_SEL_MASK                        0x1
#define MTKAIFV4_ADDA6_OUT_EN_SEL_MASK_SFT                    (0x1 << 10)
#define MTKAIFV4_ADDA_OUT_EN_SEL_SFT                          9
#define MTKAIFV4_ADDA_OUT_EN_SEL_MASK                         0x1
#define MTKAIFV4_ADDA_OUT_EN_SEL_MASK_SFT                     (0x1 << 9)
#define MTKAIFV4_TXIF_INPUT_MODE_SFT                          4
#define MTKAIFV4_TXIF_INPUT_MODE_MASK                         0x1f
#define MTKAIFV4_TXIF_INPUT_MODE_MASK_SFT                     (0x1f << 4)
#define MTKAIFV4_TXIF_FOUR_CHANNEL_SFT                        1
#define MTKAIFV4_TXIF_FOUR_CHANNEL_MASK                       0x1
#define MTKAIFV4_TXIF_FOUR_CHANNEL_MASK_SFT                   (0x1 << 1)
#define MTKAIFV4_TXIF_AFE_ON_SFT                              0
#define MTKAIFV4_TXIF_AFE_ON_MASK                             0x1
#define MTKAIFV4_TXIF_AFE_ON_MASK_SFT                         (0x1 << 0)

/* AFE_ADDA6_MTKAIFV4_TX_CFG0 */
#define ADDA6_MTKAIFV4_TXIF_EN_SEL_SFT                        12
#define ADDA6_MTKAIFV4_TXIF_EN_SEL_MASK                       0x1
#define ADDA6_MTKAIFV4_TXIF_EN_SEL_MASK_SFT                   (0x1 << 12)
#define ADDA6_MTKAIFV4_TXIF_INPUT_MODE_SFT                    4
#define ADDA6_MTKAIFV4_TXIF_INPUT_MODE_MASK                   0x1f
#define ADDA6_MTKAIFV4_TXIF_INPUT_MODE_MASK_SFT               (0x1f << 4)
#define ADDA6_MTKAIFV4_TXIF_FOUR_CHANNEL_SFT                  1
#define ADDA6_MTKAIFV4_TXIF_FOUR_CHANNEL_MASK                 0x1
#define ADDA6_MTKAIFV4_TXIF_FOUR_CHANNEL_MASK_SFT             (0x1 << 1)
#define ADDA6_MTKAIFV4_TXIF_AFE_ON_SFT                        0
#define ADDA6_MTKAIFV4_TXIF_AFE_ON_MASK                       0x1
#define ADDA6_MTKAIFV4_TXIF_AFE_ON_MASK_SFT                   (0x1 << 0)

/* AFE_ADDA_MTKAIFV4_RX_CFG0 */
#define MTKAIFV4_RXIF_CLKINV_SFT                              31
#define MTKAIFV4_RXIF_CLKINV_MASK                             0x1
#define MTKAIFV4_RXIF_CLKINV_MASK_SFT                         (0x1 << 31)
#define MTKAIFV4_RXIF_LOOPBACK_MODE_SFT                       28
#define MTKAIFV4_RXIF_LOOPBACK_MODE_MASK                      0x1
#define MTKAIFV4_RXIF_LOOPBACK_MODE_MASK_SFT                  (0x1 << 28)
#define MTKAIFV4_UL_CH7CH8_IN_EN_SEL_SFT                      19
#define MTKAIFV4_UL_CH7CH8_IN_EN_SEL_MASK                     0x1
#define MTKAIFV4_UL_CH7CH8_IN_EN_SEL_MASK_SFT                 (0x1 << 19)
#define MTKAIFV4_UL_CH5CH6_IN_EN_SEL_SFT                      18
#define MTKAIFV4_UL_CH5CH6_IN_EN_SEL_MASK                     0x1
#define MTKAIFV4_UL_CH5CH6_IN_EN_SEL_MASK_SFT                 (0x1 << 18)
#define MTKAIFV4_UL_CH3CH4_IN_EN_SEL_SFT                      17
#define MTKAIFV4_UL_CH3CH4_IN_EN_SEL_MASK                     0x1
#define MTKAIFV4_UL_CH3CH4_IN_EN_SEL_MASK_SFT                 (0x1 << 17)
#define MTKAIFV4_UL_CH1CH2_IN_EN_SEL_SFT                      16
#define MTKAIFV4_UL_CH1CH2_IN_EN_SEL_MASK                     0x1
#define MTKAIFV4_UL_CH1CH2_IN_EN_SEL_MASK_SFT                 (0x1 << 16)
#define MTKAIFV4_RXIF_EN_SEL_SFT                              12
#define MTKAIFV4_RXIF_EN_SEL_MASK                             0x1
#define MTKAIFV4_RXIF_EN_SEL_MASK_SFT                         (0x1 << 12)
#define MTKAIFV4_RXIF_INPUT_MODE_SFT                          4
#define MTKAIFV4_RXIF_INPUT_MODE_MASK                         0x1f
#define MTKAIFV4_RXIF_INPUT_MODE_MASK_SFT                     (0x1f << 4)
#define MTKAIFV4_RXIF_FOUR_CHANNEL_SFT                        1
#define MTKAIFV4_RXIF_FOUR_CHANNEL_MASK                       0x1
#define MTKAIFV4_RXIF_FOUR_CHANNEL_MASK_SFT                   (0x1 << 1)
#define MTKAIFV4_RXIF_AFE_ON_SFT                              0
#define MTKAIFV4_RXIF_AFE_ON_MASK                             0x1
#define MTKAIFV4_RXIF_AFE_ON_MASK_SFT                         (0x1 << 0)

/* AFE_ADDA_MTKAIFV4_RX_CFG1 */
#define MTKAIFV4_RXIF_SYNC_CNT_TABLE_SFT                      17
#define MTKAIFV4_RXIF_SYNC_CNT_TABLE_MASK                     0xfff
#define MTKAIFV4_RXIF_SYNC_CNT_TABLE_MASK_SFT                 (0xfff << 17)
#define MTKAIFV4_RXIF_SYNC_SEARCH_TABLE_SFT                   12
#define MTKAIFV4_RXIF_SYNC_SEARCH_TABLE_MASK                  0x1f
#define MTKAIFV4_RXIF_SYNC_SEARCH_TABLE_MASK_SFT              (0x1f << 12)
#define MTKAIFV4_RXIF_INVAILD_SYNC_CHECK_ROUND_SFT            8
#define MTKAIFV4_RXIF_INVAILD_SYNC_CHECK_ROUND_MASK           0xf
#define MTKAIFV4_RXIF_INVAILD_SYNC_CHECK_ROUND_MASK_SFT       (0xf << 8)
#define MTKAIFV4_RXIF_SYNC_CHECK_ROUND_SFT                    4
#define MTKAIFV4_RXIF_SYNC_CHECK_ROUND_MASK                   0xf
#define MTKAIFV4_RXIF_SYNC_CHECK_ROUND_MASK_SFT               (0xf << 4)
#define MTKAIFV4_RXIF_FIFO_RSP_SFT                            1
#define MTKAIFV4_RXIF_FIFO_RSP_MASK                           0x7
#define MTKAIFV4_RXIF_FIFO_RSP_MASK_SFT                       (0x7 << 1)
#define MTKAIFV4_RXIF_SELF_DEFINE_TABLE_SFT                   0
#define MTKAIFV4_RXIF_SELF_DEFINE_TABLE_MASK                  0x1
#define MTKAIFV4_RXIF_SELF_DEFINE_TABLE_MASK_SFT              (0x1 << 0)

/* AFE_ADDA6_MTKAIFV4_RX_CFG0 */
#define ADDA6_MTKAIFV4_RXIF_CLKINV_SFT                        31
#define ADDA6_MTKAIFV4_RXIF_CLKINV_MASK                       0x1
#define ADDA6_MTKAIFV4_RXIF_CLKINV_MASK_SFT                   (0x1 << 31)
#define ADDA6_MTKAIFV4_RXIF_LOOPBACK_MODE_SFT                 28
#define ADDA6_MTKAIFV4_RXIF_LOOPBACK_MODE_MASK                0x1
#define ADDA6_MTKAIFV4_RXIF_LOOPBACK_MODE_MASK_SFT            (0x1 << 28)
#define ADDA6_MTKAIFV4_RXIF_EN_SEL_SFT                        12
#define ADDA6_MTKAIFV4_RXIF_EN_SEL_MASK                       0x1
#define ADDA6_MTKAIFV4_RXIF_EN_SEL_MASK_SFT                   (0x1 << 12)
#define ADDA6_MTKAIFV4_RXIF_INPUT_MODE_SFT                    4
#define ADDA6_MTKAIFV4_RXIF_INPUT_MODE_MASK                   0x1f
#define ADDA6_MTKAIFV4_RXIF_INPUT_MODE_MASK_SFT               (0x1f << 4)
#define ADDA6_MTKAIFV4_RXIF_FOUR_CHANNEL_SFT                  1
#define ADDA6_MTKAIFV4_RXIF_FOUR_CHANNEL_MASK                 0x1
#define ADDA6_MTKAIFV4_RXIF_FOUR_CHANNEL_MASK_SFT             (0x1 << 1)
#define ADDA6_MTKAIFV4_RXIF_AFE_ON_SFT                        0
#define ADDA6_MTKAIFV4_RXIF_AFE_ON_MASK                       0x1
#define ADDA6_MTKAIFV4_RXIF_AFE_ON_MASK_SFT                   (0x1 << 0)

/* AFE_ADDA6_MTKAIFV4_RX_CFG1 */
#define ADDA6_MTKAIFV4_RXIF_SYNC_CNT_TABLE_SFT                17
#define ADDA6_MTKAIFV4_RXIF_SYNC_CNT_TABLE_MASK               0xfff
#define ADDA6_MTKAIFV4_RXIF_SYNC_CNT_TABLE_MASK_SFT           (0xfff << 17)
#define ADDA6_MTKAIFV4_RXIF_SYNC_SEARCH_TABLE_SFT             12
#define ADDA6_MTKAIFV4_RXIF_SYNC_SEARCH_TABLE_MASK            0x1f
#define ADDA6_MTKAIFV4_RXIF_SYNC_SEARCH_TABLE_MASK_SFT        (0x1f << 12)
#define ADDA6_MTKAIFV4_RXIF_INVAILD_SYNC_CHECK_ROUND_SFT      8
#define ADDA6_MTKAIFV4_RXIF_INVAILD_SYNC_CHECK_ROUND_MASK     0xf
#define ADDA6_MTKAIFV4_RXIF_INVAILD_SYNC_CHECK_ROUND_MASK_SFT (0xf << 8)
#define ADDA6_MTKAIFV4_RXIF_SYNC_CHECK_ROUND_SFT              4
#define ADDA6_MTKAIFV4_RXIF_SYNC_CHECK_ROUND_MASK             0xf
#define ADDA6_MTKAIFV4_RXIF_SYNC_CHECK_ROUND_MASK_SFT         (0xf << 4)
#define ADDA6_MTKAIFV4_RXIF_FIFO_RSP_SFT                      1
#define ADDA6_MTKAIFV4_RXIF_FIFO_RSP_MASK                     0x7
#define ADDA6_MTKAIFV4_RXIF_FIFO_RSP_MASK_SFT                 (0x7 << 1)
#define ADDA6_MTKAIFV4_RXIF_SELF_DEFINE_TABLE_SFT             0
#define ADDA6_MTKAIFV4_RXIF_SELF_DEFINE_TABLE_MASK            0x1
#define ADDA6_MTKAIFV4_RXIF_SELF_DEFINE_TABLE_MASK_SFT        (0x1 << 0)

/* AFE_ADDA_MTKAIFV4_TX_SYNCWORD_CFG */
#define ADDA6_MTKAIFV4_TXIF_SYNCWORD_SFT                      16
#define ADDA6_MTKAIFV4_TXIF_SYNCWORD_MASK                     0xffff
#define ADDA6_MTKAIFV4_TXIF_SYNCWORD_MASK_SFT                 (0xffff << 16)
#define ADDA_MTKAIFV4_TXIF_SYNCWORD_SFT                       0
#define ADDA_MTKAIFV4_TXIF_SYNCWORD_MASK                      0xffff
#define ADDA_MTKAIFV4_TXIF_SYNCWORD_MASK_SFT                  (0xffff << 0)

/* AFE_ADDA_MTKAIFV4_RX_SYNCWORD_CFG */
#define ADDA6_MTKAIFV4_RXIF_SYNCWORD_SFT                      16
#define ADDA6_MTKAIFV4_RXIF_SYNCWORD_MASK                     0xffff
#define ADDA6_MTKAIFV4_RXIF_SYNCWORD_MASK_SFT                 (0xffff << 16)
#define ADDA_MTKAIFV4_RXIF_SYNCWORD_SFT                       0
#define ADDA_MTKAIFV4_RXIF_SYNCWORD_MASK                      0xffff
#define ADDA_MTKAIFV4_RXIF_SYNCWORD_MASK_SFT                  (0xffff << 0)

/* AFE_ADDA_MTKAIFV4_MON0 */
#define MTKAIFV4_TXIF_SDATA_OUT_SFT                           23
#define MTKAIFV4_TXIF_SDATA_OUT_MASK                          0x1
#define MTKAIFV4_TXIF_SDATA_OUT_MASK_SFT                      (0x1 << 23)
#define MTKAIFV4_RXIF_SDATA_IN_SFT                            22
#define MTKAIFV4_RXIF_SDATA_IN_MASK                           0x1
#define MTKAIFV4_RXIF_SDATA_IN_MASK_SFT                       (0x1 << 22)
#define MTKAIFV4_RXIF_SEARCH_FAIL_FLAG_SFT                    21
#define MTKAIFV4_RXIF_SEARCH_FAIL_FLAG_MASK                   0x1
#define MTKAIFV4_RXIF_SEARCH_FAIL_FLAG_MASK_SFT               (0x1 << 21)
#define MTKAIFV4_RXIF_ADC_FIFO_STATUS_SFT                     0
#define MTKAIFV4_RXIF_ADC_FIFO_STATUS_MASK                    0xfff
#define MTKAIFV4_RXIF_ADC_FIFO_STATUS_MASK_SFT                (0xfff << 0)

/* AFE_ADDA_MTKAIFV4_MON1 */
#define MTKAIFV4_RXIF_OUT_CH4_SFT                             24
#define MTKAIFV4_RXIF_OUT_CH4_MASK                            0xff
#define MTKAIFV4_RXIF_OUT_CH4_MASK_SFT                        (0xff << 24)
#define MTKAIFV4_RXIF_OUT_CH3_SFT                             16
#define MTKAIFV4_RXIF_OUT_CH3_MASK                            0xff
#define MTKAIFV4_RXIF_OUT_CH3_MASK_SFT                        (0xff << 16)
#define MTKAIFV4_RXIF_OUT_CH2_SFT                             8
#define MTKAIFV4_RXIF_OUT_CH2_MASK                            0xff
#define MTKAIFV4_RXIF_OUT_CH2_MASK_SFT                        (0xff << 8)
#define MTKAIFV4_RXIF_OUT_CH1_SFT                             0
#define MTKAIFV4_RXIF_OUT_CH1_MASK                            0xff
#define MTKAIFV4_RXIF_OUT_CH1_MASK_SFT                        (0xff << 0)

/* AFE_ADDA6_MTKAIFV4_MON0 */
#define ADDA6_MTKAIFV4_TXIF_SDATA_OUT_SFT                     23
#define ADDA6_MTKAIFV4_TXIF_SDATA_OUT_MASK                    0x1
#define ADDA6_MTKAIFV4_TXIF_SDATA_OUT_MASK_SFT                (0x1 << 23)
#define ADDA6_MTKAIFV4_RXIF_SDATA_IN_SFT                      22
#define ADDA6_MTKAIFV4_RXIF_SDATA_IN_MASK                     0x1
#define ADDA6_MTKAIFV4_RXIF_SDATA_IN_MASK_SFT                 (0x1 << 22)
#define ADDA6_MTKAIFV4_RXIF_SEARCH_FAIL_FLAG_SFT              21
#define ADDA6_MTKAIFV4_RXIF_SEARCH_FAIL_FLAG_MASK             0x1
#define ADDA6_MTKAIFV4_RXIF_SEARCH_FAIL_FLAG_MASK_SFT         (0x1 << 21)
#define ADDA6_MTKAIFV3P3_RXIF_ADC_FIFO_STATUS_SFT             0
#define ADDA6_MTKAIFV3P3_RXIF_ADC_FIFO_STATUS_MASK            0xfff
#define ADDA6_MTKAIFV3P3_RXIF_ADC_FIFO_STATUS_MASK_SFT        (0xfff << 0)

/* ETDM_IN0_CON0 */
#define REG_ETDM_IN_EN_SFT                                    0
#define REG_ETDM_IN_EN_MASK                                   0x1
#define REG_ETDM_IN_EN_MASK_SFT                               (0x1 << 0)
#define REG_SYNC_MODE_SFT                                     1
#define REG_SYNC_MODE_MASK                                    0x1
#define REG_SYNC_MODE_MASK_SFT                                (0x1 << 1)
#define REG_LSB_FIRST_SFT                                     3
#define REG_LSB_FIRST_MASK                                    0x1
#define REG_LSB_FIRST_MASK_SFT                                (0x1 << 3)
#define REG_SOFT_RST_SFT                                      4
#define REG_SOFT_RST_MASK                                     0x1
#define REG_SOFT_RST_MASK_SFT                                 (0x1 << 4)
#define REG_SLAVE_MODE_SFT                                    5
#define REG_SLAVE_MODE_MASK                                   0x1
#define REG_SLAVE_MODE_MASK_SFT                               (0x1 << 5)
#define REG_FMT_SFT                                           6
#define REG_FMT_MASK                                          0x7
#define REG_FMT_MASK_SFT                                      (0x7 << 6)
#define REG_LRCK_EDGE_SEL_SFT                                 10
#define REG_LRCK_EDGE_SEL_MASK                                0x1
#define REG_LRCK_EDGE_SEL_MASK_SFT                            (0x1 << 10)
#define REG_BIT_LENGTH_SFT                                    11
#define REG_BIT_LENGTH_MASK                                   0x1f
#define REG_BIT_LENGTH_MASK_SFT                               (0x1f << 11)
#define REG_WORD_LENGTH_SFT                                   16
#define REG_WORD_LENGTH_MASK                                  0x1f
#define REG_WORD_LENGTH_MASK_SFT                              (0x1f << 16)
#define REG_CH_NUM_SFT                                        23
#define REG_CH_NUM_MASK                                       0x1f
#define REG_CH_NUM_MASK_SFT                                   (0x1f << 23)
#define REG_RELATCH_1X_EN_DOMAIN_SEL_SFT                      28
#define REG_RELATCH_1X_EN_DOMAIN_SEL_MASK                     0x7
#define REG_RELATCH_1X_EN_DOMAIN_SEL_MASK_SFT                 (0x7 << 28)
#define REG_VALID_TOGETHER_SFT                                31
#define REG_VALID_TOGETHER_MASK                               0x1
#define REG_VALID_TOGETHER_MASK_SFT                           (0x1 << 31)

/* ETDM_IN0_CON1 */
/* ETDM_IN1_CON1 */
/* ETDM_IN2_CON1 */
/* ETDM_IN3_CON1 */
/* ETDM_IN4_CON1 */
/* ETDM_IN5_CON1 */
/* ETDM_IN6_CON1 */
#define REG_INITIAL_COUNT_SFT                                 0
#define REG_INITIAL_COUNT_MASK                                0x1f
#define REG_INITIAL_COUNT_MASK_SFT                            (0x1f << 0)
#define REG_INITIAL_POINT_SFT                                 5
#define REG_INITIAL_POINT_MASK                                0x1f
#define REG_INITIAL_POINT_MASK_SFT                            (0x1f << 5)
#define REG_LRCK_AUTO_OFF_SFT                                 10
#define REG_LRCK_AUTO_OFF_MASK                                0x1
#define REG_LRCK_AUTO_OFF_MASK_SFT                            (0x1 << 10)
#define REG_BCK_AUTO_OFF_SFT                                  11
#define REG_BCK_AUTO_OFF_MASK                                 0x1
#define REG_BCK_AUTO_OFF_MASK_SFT                             (0x1 << 11)
#define REG_INITIAL_LRCK_SFT                                  13
#define REG_INITIAL_LRCK_MASK                                 0x1
#define REG_INITIAL_LRCK_MASK_SFT                             (0x1 << 13)
#define REG_NO_ALIGN_1X_EN_SFT                                14
#define REG_NO_ALIGN_1X_EN_MASK                               0x1
#define REG_NO_ALIGN_1X_EN_MASK_SFT                           (0x1 << 14)
#define REG_LRCK_RESET_SFT                                    15
#define REG_LRCK_RESET_MASK                                   0x1
#define REG_LRCK_RESET_MASK_SFT                               (0x1 << 15)
#define PINMUX_MCLK_CTRL_OE_SFT                               16
#define PINMUX_MCLK_CTRL_OE_MASK                              0x1
#define PINMUX_MCLK_CTRL_OE_MASK_SFT                          (0x1 << 16)
#define REG_OUTPUT_CR_EN_SFT                                  18
#define REG_OUTPUT_CR_EN_MASK                                 0x1
#define REG_OUTPUT_CR_EN_MASK_SFT                             (0x1 << 18)
#define REG_LR_ALIGN_SFT                                      19
#define REG_LR_ALIGN_MASK                                     0x1
#define REG_LR_ALIGN_MASK_SFT                                 (0x1 << 19)
#define REG_LRCK_WIDTH_SFT                                    20
#define REG_LRCK_WIDTH_MASK                                   0x3ff
#define REG_LRCK_WIDTH_MASK_SFT                               (0x3ff << 20)
#define REG_DIRECT_INPUT_MASTER_BCK_SFT                       30
#define REG_DIRECT_INPUT_MASTER_BCK_MASK                      0x1
#define REG_DIRECT_INPUT_MASTER_BCK_MASK_SFT                  (0x1 << 30)
#define REG_LRCK_AUTO_MODE_SFT                                31
#define REG_LRCK_AUTO_MODE_MASK                               0x1
#define REG_LRCK_AUTO_MODE_MASK_SFT                           (0x1 << 31)

/* ETDM_IN0_CON2 */
/* ETDM_IN1_CON2 */
/* ETDM_IN2_CON2 */
/* ETDM_IN3_CON2 */
/* ETDM_IN4_CON2 */
/* ETDM_IN5_CON2 */
/* ETDM_IN6_CON2 */
#define REG_UPDATE_POINT_SFT                                  0
#define REG_UPDATE_POINT_MASK                                 0x1f
#define REG_UPDATE_POINT_MASK_SFT                             (0x1f << 0)
#define REG_UPDATE_GAP_SFT                                    5
#define REG_UPDATE_GAP_MASK                                   0x1f
#define REG_UPDATE_GAP_MASK_SFT                               (0x1f << 5)
#define REG_CLOCK_SOURCE_SEL_SFT                              10
#define REG_CLOCK_SOURCE_SEL_MASK                             0x7
#define REG_CLOCK_SOURCE_SEL_MASK_SFT                         (0x7 << 10)
#define REG_CK_EN_SEL_AUTO_SFT                                14
#define REG_CK_EN_SEL_AUTO_MASK                               0x1
#define REG_CK_EN_SEL_AUTO_MASK_SFT                           (0x1 << 14)
#define REG_MULTI_IP_TOTAL_CHNUM_SFT                          15
#define REG_MULTI_IP_TOTAL_CHNUM_MASK                         0x1f
#define REG_MULTI_IP_TOTAL_CHNUM_MASK_SFT                     (0x1f << 15)
#define REG_MASK_AUTO_SFT                                     20
#define REG_MASK_AUTO_MASK                                    0x1
#define REG_MASK_AUTO_MASK_SFT                                (0x1 << 20)
#define REG_MASK_NUM_SFT                                      21
#define REG_MASK_NUM_MASK                                     0x1f
#define REG_MASK_NUM_MASK_SFT                                 (0x1f << 21)
#define REG_UPDATE_POINT_AUTO_SFT                             26
#define REG_UPDATE_POINT_AUTO_MASK                            0x1
#define REG_UPDATE_POINT_AUTO_MASK_SFT                        (0x1 << 26)
#define REG_SDATA_DELAY_0P5T_EN_SFT                           27
#define REG_SDATA_DELAY_0P5T_EN_MASK                          0x1
#define REG_SDATA_DELAY_0P5T_EN_MASK_SFT                      (0x1 << 27)
#define REG_SDATA_DELAY_BCK_INV_SFT                           28
#define REG_SDATA_DELAY_BCK_INV_MASK                          0x1
#define REG_SDATA_DELAY_BCK_INV_MASK_SFT                      (0x1 << 28)
#define REG_LRCK_DELAY_0P5T_EN_SFT                            29
#define REG_LRCK_DELAY_0P5T_EN_MASK                           0x1
#define REG_LRCK_DELAY_0P5T_EN_MASK_SFT                       (0x1 << 29)
#define REG_LRCK_DELAY_BCK_INV_SFT                            30
#define REG_LRCK_DELAY_BCK_INV_MASK                           0x1
#define REG_LRCK_DELAY_BCK_INV_MASK_SFT                       (0x1 << 30)
#define REG_MULTI_IP_MODE_SFT                                 31
#define REG_MULTI_IP_MODE_MASK                                0x1
#define REG_MULTI_IP_MODE_MASK_SFT                            (0x1 << 31)

/* ETDM_IN0_CON3 */
/* ETDM_IN1_CON3 */
/* ETDM_IN2_CON3 */
/* ETDM_IN3_CON3 */
/* ETDM_IN4_CON3 */
/* ETDM_IN5_CON3 */
/* ETDM_IN6_CON3 */
#define REG_DISABLE_OUT_SFT                                   0
#define REG_DISABLE_OUT_MASK                                  0xffff
#define REG_DISABLE_OUT_MASK_SFT                              (0xffff << 0)
#define REG_RJ_DATA_RIGHT_ALIGN_SFT                           16
#define REG_RJ_DATA_RIGHT_ALIGN_MASK                          0x1
#define REG_RJ_DATA_RIGHT_ALIGN_MASK_SFT                      (0x1 << 16)
#define REG_MONITOR_SEL_SFT                                   17
#define REG_MONITOR_SEL_MASK                                  0x3
#define REG_MONITOR_SEL_MASK_SFT                              (0x3 << 17)
#define REG_CNT_UPPER_LIMIT_SFT                               19
#define REG_CNT_UPPER_LIMIT_MASK                              0x3f
#define REG_CNT_UPPER_LIMIT_MASK_SFT                          (0x3f << 19)
#define REG_COMPACT_SAMPLE_END_DIS_SFT                        25
#define REG_COMPACT_SAMPLE_END_DIS_MASK                       0x1
#define REG_COMPACT_SAMPLE_END_DIS_MASK_SFT                   (0x1 << 25)
#define REG_FS_TIMING_SEL_SFT                                 26
#define REG_FS_TIMING_SEL_MASK                                0x1f
#define REG_FS_TIMING_SEL_MASK_SFT                            (0x1f << 26)
#define REG_SAMPLE_END_MODE_SFT                               31
#define REG_SAMPLE_END_MODE_MASK                              0x1
#define REG_SAMPLE_END_MODE_MASK_SFT                          (0x1 << 31)

/* ETDM_IN0_CON4 */
/* ETDM_IN1_CON4 */
/* ETDM_IN2_CON4 */
/* ETDM_IN3_CON4 */
/* ETDM_IN4_CON4 */
/* ETDM_IN5_CON4 */
/* ETDM_IN6_CON4 */
#define REG_ALWAYS_OPEN_1X_EN_SFT                             31
#define REG_ALWAYS_OPEN_1X_EN_MASK                            0x1
#define REG_ALWAYS_OPEN_1X_EN_MASK_SFT                        (0x1 << 31)
#define REG_WAIT_LAST_SAMPLE_SFT                              30
#define REG_WAIT_LAST_SAMPLE_MASK                             0x1
#define REG_WAIT_LAST_SAMPLE_MASK_SFT                         (0x1 << 30)
#define REG_SAMPLE_END_POINT_SFT                              25
#define REG_SAMPLE_END_POINT_MASK                             0x1f
#define REG_SAMPLE_END_POINT_MASK_SFT                         (0x1f << 25)
#define REG_RELATCH_1X_EN_SEL_SFT                             20
#define REG_RELATCH_1X_EN_SEL_MASK                            0x1f
#define REG_RELATCH_1X_EN_SEL_MASK_SFT                        (0x1f << 20)
#define REG_MASTER_WS_INV_SFT                                 19
#define REG_MASTER_WS_INV_MASK                                0x1
#define REG_MASTER_WS_INV_MASK_SFT                            (0x1 << 19)
#define REG_MASTER_BCK_INV_SFT                                18
#define REG_MASTER_BCK_INV_MASK                               0x1
#define REG_MASTER_BCK_INV_MASK_SFT                           (0x1 << 18)
#define REG_SLAVE_LRCK_INV_SFT                                17
#define REG_SLAVE_LRCK_INV_MASK                               0x1
#define REG_SLAVE_LRCK_INV_MASK_SFT                           (0x1 << 17)
#define REG_SLAVE_BCK_INV_SFT                                 16
#define REG_SLAVE_BCK_INV_MASK                                0x1
#define REG_SLAVE_BCK_INV_MASK_SFT                            (0x1 << 16)
#define REG_REPACK_CHNUM_SFT                                  12
#define REG_REPACK_CHNUM_MASK                                 0xf
#define REG_REPACK_CHNUM_MASK_SFT                             (0xf << 12)
#define REG_ASYNC_RESET_SFT                                   11
#define REG_ASYNC_RESET_MASK                                  0x1
#define REG_ASYNC_RESET_MASK_SFT                              (0x1 << 11)
#define REG_REPACK_WORD_LENGTH_SFT                            9
#define REG_REPACK_WORD_LENGTH_MASK                           0x3
#define REG_REPACK_WORD_LENGTH_MASK_SFT                       (0x3 << 9)
#define REG_REPACK_AUTO_MODE_SFT                              8
#define REG_REPACK_AUTO_MODE_MASK                             0x1
#define REG_REPACK_AUTO_MODE_MASK_SFT                         (0x1 << 8)
#define REG_REPACK_MODE_SFT                                   0
#define REG_REPACK_MODE_MASK                                  0x3f
#define REG_REPACK_MODE_MASK_SFT                              (0x3f << 0)

/* ETDM_IN0_CON5 */
/* ETDM_IN1_CON5 */
/* ETDM_IN2_CON5 */
/* ETDM_IN3_CON5 */
/* ETDM_IN4_CON5 */
/* ETDM_IN5_CON5 */
/* ETDM_IN6_CON5 */
#define REG_LR_SWAP_SFT                                       16
#define REG_LR_SWAP_MASK                                      0xffff
#define REG_LR_SWAP_MASK_SFT                                  (0xffff << 16)
#define REG_ODD_FLAG_EN_SFT                                   0
#define REG_ODD_FLAG_EN_MASK                                  0xffff
#define REG_ODD_FLAG_EN_MASK_SFT                              (0xffff << 0)

/* ETDM_IN0_CON6 */
/* ETDM_IN1_CON6 */
/* ETDM_IN2_CON6 */
/* ETDM_IN3_CON6 */
/* ETDM_IN4_CON6 */
/* ETDM_IN5_CON6 */
/* ETDM_IN6_CON6 */
#define LCH_DATA_REG_SFT                                      0
#define LCH_DATA_REG_MASK                                     0xffffffff
#define LCH_DATA_REG_MASK_SFT                                 (0xffffffff << 0)

/* ETDM_IN0_CON7 */
/* ETDM_IN1_CON7 */
/* ETDM_IN2_CON7 */
/* ETDM_IN3_CON7 */
/* ETDM_IN4_CON7 */
/* ETDM_IN5_CON7 */
/* ETDM_IN6_CON7 */
#define RCH_DATA_REG_SFT                                      0
#define RCH_DATA_REG_MASK                                     0xffffffff
#define RCH_DATA_REG_MASK_SFT                                 (0xffffffff << 0)

/* ETDM_IN0_CON8 */
/* ETDM_IN1_CON8 */
/* ETDM_IN2_CON8 */
/* ETDM_IN3_CON8 */
/* ETDM_IN4_CON8 */
/* ETDM_IN5_CON8 */
/* ETDM_IN6_CON8 */
#define REG_AFIFO_THRESHOLD_SFT                               29
#define REG_AFIFO_THRESHOLD_MASK                              0x3
#define REG_AFIFO_THRESHOLD_MASK_SFT                          (0x3 << 29)
#define REG_CK_EN_SEL_MANUAL_SFT                              16
#define REG_CK_EN_SEL_MANUAL_MASK                             0x3ff
#define REG_CK_EN_SEL_MANUAL_MASK_SFT                         (0x3ff << 16)
#define REG_AFIFO_SW_RESET_SFT                                15
#define REG_AFIFO_SW_RESET_MASK                               0x1
#define REG_AFIFO_SW_RESET_MASK_SFT                           (0x1 << 15)
#define REG_AFIFO_RESET_SEL_SFT                               14
#define REG_AFIFO_RESET_SEL_MASK                              0x1
#define REG_AFIFO_RESET_SEL_MASK_SFT                          (0x1 << 14)
#define REG_AFIFO_AUTO_RESET_DIS_SFT                          9
#define REG_AFIFO_AUTO_RESET_DIS_MASK                         0x1
#define REG_AFIFO_AUTO_RESET_DIS_MASK_SFT                     (0x1 << 9)
#define REG_ETDM_USE_AFIFO_SFT                                8
#define REG_ETDM_USE_AFIFO_MASK                               0x1
#define REG_ETDM_USE_AFIFO_MASK_SFT                           (0x1 << 8)
#define REG_AFIFO_CLOCK_DOMAIN_SEL_SFT                        5
#define REG_AFIFO_CLOCK_DOMAIN_SEL_MASK                       0x7
#define REG_AFIFO_CLOCK_DOMAIN_SEL_MASK_SFT                   (0x7 << 5)
#define REG_AFIFO_MODE_SFT                                    0
#define REG_AFIFO_MODE_MASK                                   0x1f
#define REG_AFIFO_MODE_MASK_SFT                               (0x1f << 0)

/* ETDM_IN0_CON9 */
/* ETDM_IN1_CON9 */
/* ETDM_IN2_CON9 */
/* ETDM_IN3_CON9 */
/* ETDM_IN4_CON9 */
/* ETDM_IN5_CON9 */
/* ETDM_IN6_CON9 */
#define REG_OUT2LATCH_TIME_SFT                                10
#define REG_OUT2LATCH_TIME_MASK                               0x1f
#define REG_OUT2LATCH_TIME_MASK_SFT                           (0x1f << 10)
#define REG_ALMOST_END_BIT_COUNT_SFT                          5
#define REG_ALMOST_END_BIT_COUNT_MASK                         0x1f
#define REG_ALMOST_END_BIT_COUNT_MASK_SFT                     (0x1f << 5)
#define REG_ALMOST_END_CH_COUNT_SFT                           0
#define REG_ALMOST_END_CH_COUNT_MASK                          0x1f
#define REG_ALMOST_END_CH_COUNT_MASK_SFT                      (0x1f << 0)

/* ETDM_IN0_MON */
/* ETDM_IN1_MON */
/* ETDM_IN2_MON */
/* ETDM_IN3_MON */
/* ETDM_IN4_MON */
/* ETDM_IN5_MON */
/* ETDM_IN6_MON */
#define LRCK_INV_SFT                                          30
#define LRCK_INV_MASK                                         0x1
#define LRCK_INV_MASK_SFT                                     (0x1 << 30)
#define EN_SYNC_OUT_SFT                                       29
#define EN_SYNC_OUT_MASK                                      0x1
#define EN_SYNC_OUT_MASK_SFT                                  (0x1 << 29)
#define HOPPING_EN_SYNC_OUT_PRE_SFT                           28
#define HOPPING_EN_SYNC_OUT_PRE_MASK                          0x1
#define HOPPING_EN_SYNC_OUT_PRE_MASK_SFT                      (0x1 << 28)
#define WFULL_SFT                                             27
#define WFULL_MASK                                            0x1
#define WFULL_MASK_SFT                                        (0x1 << 27)
#define REMPTY_SFT                                            26
#define REMPTY_MASK                                           0x1
#define REMPTY_MASK_SFT                                       (0x1 << 26)
#define ETDM_2X_CK_EN_SFT                                     25
#define ETDM_2X_CK_EN_MASK                                    0x1
#define ETDM_2X_CK_EN_MASK_SFT                                (0x1 << 25)
#define ETDM_1X_CK_EN_SFT                                     24
#define ETDM_1X_CK_EN_MASK                                    0x1
#define ETDM_1X_CK_EN_MASK_SFT                                (0x1 << 24)
#define SDATA0_SFT                                            23
#define SDATA0_MASK                                           0x1
#define SDATA0_MASK_SFT                                       (0x1 << 23)
#define CURRENT_STATUS_SFT                                    21
#define CURRENT_STATUS_MASK                                   0x3
#define CURRENT_STATUS_MASK_SFT                               (0x3 << 21)
#define BIT_POINT_SFT                                         16
#define BIT_POINT_MASK                                        0x1f
#define BIT_POINT_MASK_SFT                                    (0x1f << 16)
#define BIT_CH_COUNT_SFT                                      10
#define BIT_CH_COUNT_MASK                                     0x3f
#define BIT_CH_COUNT_MASK_SFT                                 (0x3f << 10)
#define BIT_COUNT_SFT                                         5
#define BIT_COUNT_MASK                                        0x1f
#define BIT_COUNT_MASK_SFT                                    (0x1f << 5)
#define CH_COUNT_SFT                                          0
#define CH_COUNT_MASK                                         0x1f
#define CH_COUNT_MASK_SFT                                     (0x1f << 0)

/* ETDM_OUT0_CON0 */
/* ETDM_OUT1_CON0 */
/* ETDM_OUT2_CON0 */
/* ETDM_OUT3_CON0 */
/* ETDM_OUT4_CON0 */
/* ETDM_OUT5_CON0 */
/* ETDM_OUT6_CON0 */
#define OUT_REG_ETDM_OUT_EN_SFT                                   0
#define OUT_REG_ETDM_OUT_EN_MASK                                  0x1
#define OUT_REG_ETDM_OUT_EN_MASK_SFT                              (0x1 << 0)
#define OUT_REG_SYNC_MODE_SFT                                     1
#define OUT_REG_SYNC_MODE_MASK                                    0x1
#define OUT_REG_SYNC_MODE_MASK_SFT                                (0x1 << 1)
#define OUT_REG_LSB_FIRST_SFT                                     3
#define OUT_REG_LSB_FIRST_MASK                                    0x1
#define OUT_REG_LSB_FIRST_MASK_SFT                                (0x1 << 3)
#define OUT_REG_SOFT_RST_SFT                                      4
#define OUT_REG_SOFT_RST_MASK                                     0x1
#define OUT_REG_SOFT_RST_MASK_SFT                                 (0x1 << 4)
#define OUT_REG_SLAVE_MODE_SFT                                    5
#define OUT_REG_SLAVE_MODE_MASK                                   0x1
#define OUT_REG_SLAVE_MODE_MASK_SFT                               (0x1 << 5)
#define OUT_REG_FMT_SFT                                           6
#define OUT_REG_FMT_MASK                                          0x7
#define OUT_REG_FMT_MASK_SFT                                      (0x7 << 6)
#define OUT_REG_LRCK_EDGE_SEL_SFT                                 10
#define OUT_REG_LRCK_EDGE_SEL_MASK                                0x1
#define OUT_REG_LRCK_EDGE_SEL_MASK_SFT                            (0x1 << 10)
#define OUT_REG_BIT_LENGTH_SFT                                    11
#define OUT_REG_BIT_LENGTH_MASK                                   0x1f
#define OUT_REG_BIT_LENGTH_MASK_SFT                               (0x1f << 11)
#define OUT_REG_WORD_LENGTH_SFT                                   16
#define OUT_REG_WORD_LENGTH_MASK                                  0x1f
#define OUT_REG_WORD_LENGTH_MASK_SFT                              (0x1f << 16)
#define OUT_REG_CH_NUM_SFT                                        23
#define OUT_REG_CH_NUM_MASK                                       0x1f
#define OUT_REG_CH_NUM_MASK_SFT                                   (0x1f << 23)
#define OUT_REG_RELATCH_DOMAIN_SEL_SFT                            28
#define OUT_REG_RELATCH_DOMAIN_SEL_MASK                           0x7
#define OUT_REG_RELATCH_DOMAIN_SEL_MASK_SFT                       (0x7 << 28)
#define OUT_REG_VALID_TOGETHER_SFT                                31
#define OUT_REG_VALID_TOGETHER_MASK                               0x1
#define OUT_REG_VALID_TOGETHER_MASK_SFT                           (0x1 << 31)

/* ETDM_OUT0_CON1 */
/* ETDM_OUT1_CON1 */
/* ETDM_OUT2_CON1 */
/* ETDM_OUT3_CON1 */
/* ETDM_OUT4_CON1 */
/* ETDM_OUT5_CON1 */
/* ETDM_OUT6_CON1 */
#define OUT_REG_INITIAL_COUNT_SFT                                 0
#define OUT_REG_INITIAL_COUNT_MASK                                0x1f
#define OUT_REG_INITIAL_COUNT_MASK_SFT                            (0x1f << 0)
#define OUT_REG_INITIAL_POINT_SFT                                 5
#define OUT_REG_INITIAL_POINT_MASK                                0x1f
#define OUT_REG_INITIAL_POINT_MASK_SFT                            (0x1f << 5)
#define OUT_REG_LRCK_AUTO_OFF_SFT                                 10
#define OUT_REG_LRCK_AUTO_OFF_MASK                                0x1
#define OUT_REG_LRCK_AUTO_OFF_MASK_SFT                            (0x1 << 10)
#define OUT_REG_BCK_AUTO_OFF_SFT                                  11
#define OUT_REG_BCK_AUTO_OFF_MASK                                 0x1
#define OUT_REG_BCK_AUTO_OFF_MASK_SFT                             (0x1 << 11)
#define OUT_REG_INITIAL_LRCK_SFT                                  13
#define OUT_REG_INITIAL_LRCK_MASK                                 0x1
#define OUT_REG_INITIAL_LRCK_MASK_SFT                             (0x1 << 13)
#define OUT_REG_NO_ALIGN_1X_EN_SFT                                14
#define OUT_REG_NO_ALIGN_1X_EN_MASK                               0x1
#define OUT_REG_NO_ALIGN_1X_EN_MASK_SFT                           (0x1 << 14)
#define OUT_REG_LRCK_RESET_SFT                                    15
#define OUT_REG_LRCK_RESET_MASK                                   0x1
#define OUT_REG_LRCK_RESET_MASK_SFT                               (0x1 << 15)
#define OUT_PINMUX_MCLK_CTRL_OE_SFT                               16
#define OUT_PINMUX_MCLK_CTRL_OE_MASK                              0x1
#define OUT_PINMUX_MCLK_CTRL_OE_MASK_SFT                          (0x1 << 16)
#define OUT_REG_OUTPUT_CR_EN_SFT                                  18
#define OUT_REG_OUTPUT_CR_EN_MASK                                 0x1
#define OUT_REG_OUTPUT_CR_EN_MASK_SFT                             (0x1 << 18)
#define OUT_REG_LRCK_WIDTH_SFT                                    19
#define OUT_REG_LRCK_WIDTH_MASK                                   0x3ff
#define OUT_REG_LRCK_WIDTH_MASK_SFT                               (0x3ff << 19)
#define OUT_REG_LRCK_AUTO_MODE_SFT                                29
#define OUT_REG_LRCK_AUTO_MODE_MASK                               0x1
#define OUT_REG_LRCK_AUTO_MODE_MASK_SFT                           (0x1 << 29)
#define OUT_REG_DIRECT_INPUT_MASTER_BCK_SFT                       30
#define OUT_REG_DIRECT_INPUT_MASTER_BCK_MASK                      0x1
#define OUT_REG_DIRECT_INPUT_MASTER_BCK_MASK_SFT                  (0x1 << 30)
#define OUT_REG_16B_COMPACT_MODE_SFT                              31
#define OUT_REG_16B_COMPACT_MODE_MASK                             0x1
#define OUT_REG_16B_COMPACT_MODE_MASK_SFT                         (0x1 << 31)

/* ETDM_OUT0_CON2 */
/* ETDM_OUT1_CON2 */
/* ETDM_OUT2_CON2 */
/* ETDM_OUT3_CON2 */
/* ETDM_OUT4_CON2 */
/* ETDM_OUT5_CON2 */
/* ETDM_OUT6_CON2 */
#define OUT_REG_IN2LATCH_TIME_SFT                                 0
#define OUT_REG_IN2LATCH_TIME_MASK                                0x1f
#define OUT_REG_IN2LATCH_TIME_MASK_SFT                            (0x1f << 0)
#define OUT_REG_MASK_NUM_SFT                                      5
#define OUT_REG_MASK_NUM_MASK                                     0x1f
#define OUT_REG_MASK_NUM_MASK_SFT                                 (0x1f << 5)
#define OUT_REG_MASK_AUTO_SFT                                     10
#define OUT_REG_MASK_AUTO_MASK                                    0x1
#define OUT_REG_MASK_AUTO_MASK_SFT                                (0x1 << 10)
#define OUT_REG_SDATA_SHIFT_SFT                                   11
#define OUT_REG_SDATA_SHIFT_MASK                                  0x3
#define OUT_REG_SDATA_SHIFT_MASK_SFT                              (0x3 << 11)
#define OUT_REG_ALMOST_END_BIT_COUNT_SFT                          13
#define OUT_REG_ALMOST_END_BIT_COUNT_MASK                         0x1f
#define OUT_REG_ALMOST_END_BIT_COUNT_MASK_SFT                     (0x1f << 13)
#define OUT_REG_SDATA_CON_SFT                                     18
#define OUT_REG_SDATA_CON_MASK                                    0x3
#define OUT_REG_SDATA_CON_MASK_SFT                                (0x3 << 18)
#define OUT_REG_REDUNDANT_0_SFT                                   20
#define OUT_REG_REDUNDANT_0_MASK                                  0x1
#define OUT_REG_REDUNDANT_0_MASK_SFT                              (0x1 << 20)
#define OUT_REG_SDATA_AUTO_OFF_SFT                                21
#define OUT_REG_SDATA_AUTO_OFF_MASK                               0x1
#define OUT_REG_SDATA_AUTO_OFF_MASK_SFT                           (0x1 << 21)
#define OUT_REG_BCK_OFF_TIME_SFT                                  22
#define OUT_REG_BCK_OFF_TIME_MASK                                 0x3
#define OUT_REG_BCK_OFF_TIME_MASK_SFT                             (0x3 << 22)
#define OUT_REG_MONITOR_SEL_SFT                                   24
#define OUT_REG_MONITOR_SEL_MASK                                  0x3
#define OUT_REG_MONITOR_SEL_MASK_SFT                              (0x3 << 24)
#define OUT_REG_SHIFT_AUTO_SFT                                    26
#define OUT_REG_SHIFT_AUTO_MASK                                   0x1
#define OUT_REG_SHIFT_AUTO_MASK_SFT                               (0x1 << 26)
#define OUT_REG_SDATA_DELAY_0P5T_EN_SFT                           27
#define OUT_REG_SDATA_DELAY_0P5T_EN_MASK                          0x1
#define OUT_REG_SDATA_DELAY_0P5T_EN_MASK_SFT                      (0x1 << 27)
#define OUT_REG_SDATA_DELAY_BCK_INV_SFT                           28
#define OUT_REG_SDATA_DELAY_BCK_INV_MASK                          0x1
#define OUT_REG_SDATA_DELAY_BCK_INV_MASK_SFT                      (0x1 << 28)
#define OUT_REG_LRCK_DELAY_0P5T_EN_SFT                            29
#define OUT_REG_LRCK_DELAY_0P5T_EN_MASK                           0x1
#define OUT_REG_LRCK_DELAY_0P5T_EN_MASK_SFT                       (0x1 << 29)
#define OUT_REG_LRCK_DELAY_BCK_INV_SFT                            30
#define OUT_REG_LRCK_DELAY_BCK_INV_MASK                           0x1
#define OUT_REG_LRCK_DELAY_BCK_INV_MASK_SFT                       (0x1 << 30)
#define OUT_REG_OFF_CR_EN_SFT                                     31
#define OUT_REG_OFF_CR_EN_MASK                                    0x1
#define OUT_REG_OFF_CR_EN_MASK_SFT                                (0x1 << 31)

/* ETDM_OUT0_CON3 */
/* ETDM_OUT1_CON3 */
/* ETDM_OUT2_CON3 */
/* ETDM_OUT3_CON3 */
/* ETDM_OUT4_CON3 */
/* ETDM_OUT5_CON3 */
/* ETDM_OUT6_CON3 */
#define OUT_REG_START_CH_PAIR0_SFT                                0
#define OUT_REG_START_CH_PAIR0_MASK                               0xf
#define OUT_REG_START_CH_PAIR0_MASK_SFT                           (0xf << 0)
#define OUT_REG_START_CH_PAIR1_SFT                                4
#define OUT_REG_START_CH_PAIR1_MASK                               0xf
#define OUT_REG_START_CH_PAIR1_MASK_SFT                           (0xf << 4)
#define OUT_REG_START_CH_PAIR2_SFT                                8
#define OUT_REG_START_CH_PAIR2_MASK                               0xf
#define OUT_REG_START_CH_PAIR2_MASK_SFT                           (0xf << 8)
#define OUT_REG_START_CH_PAIR3_SFT                                12
#define OUT_REG_START_CH_PAIR3_MASK                               0xf
#define OUT_REG_START_CH_PAIR3_MASK_SFT                           (0xf << 12)
#define OUT_REG_START_CH_PAIR4_SFT                                16
#define OUT_REG_START_CH_PAIR4_MASK                               0xf
#define OUT_REG_START_CH_PAIR4_MASK_SFT                           (0xf << 16)
#define OUT_REG_START_CH_PAIR5_SFT                                20
#define OUT_REG_START_CH_PAIR5_MASK                               0xf
#define OUT_REG_START_CH_PAIR5_MASK_SFT                           (0xf << 20)
#define OUT_REG_START_CH_PAIR6_SFT                                24
#define OUT_REG_START_CH_PAIR6_MASK                               0xf
#define OUT_REG_START_CH_PAIR6_MASK_SFT                           (0xf << 24)
#define OUT_REG_START_CH_PAIR7_SFT                                28
#define OUT_REG_START_CH_PAIR7_MASK                               0xf
#define OUT_REG_START_CH_PAIR7_MASK_SFT                           (0xf << 28)

/* ETDM_OUT0_CON4 */
/* ETDM_OUT1_CON4 */
/* ETDM_OUT2_CON4 */
/* ETDM_OUT3_CON4 */
/* ETDM_OUT4_CON4 */
/* ETDM_OUT5_CON4 */
/* ETDM_OUT6_CON4 */
#define OUT_REG_FS_TIMING_SEL_SFT                                 0
#define OUT_REG_FS_TIMING_SEL_MASK                                0x1f
#define OUT_REG_FS_TIMING_SEL_MASK_SFT                            (0x1f << 0)
#define OUT_REG_CLOCK_SOURCE_SEL_SFT                              6
#define OUT_REG_CLOCK_SOURCE_SEL_MASK                             0x7
#define OUT_REG_CLOCK_SOURCE_SEL_MASK_SFT                         (0x7 << 6)
#define OUT_REG_CK_EN_SEL_AUTO_SFT                                10
#define OUT_REG_CK_EN_SEL_AUTO_MASK                               0x1
#define OUT_REG_CK_EN_SEL_AUTO_MASK_SFT                           (0x1 << 10)
#define OUT_REG_ASYNC_RESET_SFT                                   11
#define OUT_REG_ASYNC_RESET_MASK                                  0x1
#define OUT_REG_ASYNC_RESET_MASK_SFT                              (0x1 << 11)
#define OUT_REG_CK_EN_SEL_MANUAL_SFT                              14
#define OUT_REG_CK_EN_SEL_MANUAL_MASK                             0x3ff
#define OUT_REG_CK_EN_SEL_MANUAL_MASK_SFT                         (0x3ff << 14)
#define OUT_REG_RELATCH_EN_SEL_SFT                                24
#define OUT_REG_RELATCH_EN_SEL_MASK                               0x1f
#define OUT_REG_RELATCH_EN_SEL_MASK_SFT                           (0x1f << 24)
#define OUT_REG_WAIT_LAST_SAMPLE_SFT                              30
#define OUT_REG_WAIT_LAST_SAMPLE_MASK                             0x1
#define OUT_REG_WAIT_LAST_SAMPLE_MASK_SFT                         (0x1 << 30)
#define OUT_REG_ALWAYS_OPEN_1X_EN_SFT                             31
#define OUT_REG_ALWAYS_OPEN_1X_EN_MASK                            0x1
#define OUT_REG_ALWAYS_OPEN_1X_EN_MASK_SFT                        (0x1 << 31)

/* ETDM_OUT0_CON5 */
/* ETDM_OUT1_CON5 */
/* ETDM_OUT2_CON5 */
/* ETDM_OUT3_CON5 */
/* ETDM_OUT4_CON5 */
/* ETDM_OUT5_CON5 */
/* ETDM_OUT6_CON5 */
#define OUT_REG_REPACK_BITNUM_SFT                                 0
#define OUT_REG_REPACK_BITNUM_MASK                                0x3
#define OUT_REG_REPACK_BITNUM_MASK_SFT                            (0x3 << 0)
#define OUT_REG_REPACK_CHNUM_SFT                                  2
#define OUT_REG_REPACK_CHNUM_MASK                                 0xf
#define OUT_REG_REPACK_CHNUM_MASK_SFT                             (0xf << 2)
#define OUT_REG_SLAVE_BCK_INV_SFT                                 7
#define OUT_REG_SLAVE_BCK_INV_MASK                                0x1
#define OUT_REG_SLAVE_BCK_INV_MASK_SFT                            (0x1 << 7)
#define OUT_REG_SLAVE_LRCK_INV_SFT                                8
#define OUT_REG_SLAVE_LRCK_INV_MASK                               0x1
#define OUT_REG_SLAVE_LRCK_INV_MASK_SFT                           (0x1 << 8)
#define OUT_REG_MASTER_BCK_INV_SFT                                9
#define OUT_REG_MASTER_BCK_INV_MASK                               0x1
#define OUT_REG_MASTER_BCK_INV_MASK_SFT                           (0x1 << 9)
#define OUT_REG_MASTER_WS_INV_SFT                                 10
#define OUT_REG_MASTER_WS_INV_MASK                                0x1
#define OUT_REG_MASTER_WS_INV_MASK_SFT                            (0x1 << 10)
#define OUT_REG_REPACK_24B_MSB_ALIGN_SFT                          11
#define OUT_REG_REPACK_24B_MSB_ALIGN_MASK                         0x1
#define OUT_REG_REPACK_24B_MSB_ALIGN_MASK_SFT                     (0x1 << 11)
#define OUT_REG_LR_SWAP_SFT                                       16
#define OUT_REG_LR_SWAP_MASK                                      0xffff
#define OUT_REG_LR_SWAP_MASK_SFT                                  (0xffff << 16)

/* ETDM_OUT0_CON6 */
/* ETDM_OUT1_CON6 */
/* ETDM_OUT2_CON6 */
/* ETDM_OUT3_CON6 */
/* ETDM_OUT4_CON6 */
/* ETDM_OUT5_CON6 */
/* ETDM_OUT6_CON6 */
#define OUT_LCH_DATA_REG_SFT                                      0
#define OUT_LCH_DATA_REG_MASK                                     0xffffffff
#define OUT_LCH_DATA_REG_MASK_SFT                                 (0xffffffff << 0)

/* ETDM_OUT0_CON7 */
/* ETDM_OUT1_CON7 */
/* ETDM_OUT2_CON7 */
/* ETDM_OUT3_CON7 */
/* ETDM_OUT4_CON7 */
/* ETDM_OUT5_CON7 */
/* ETDM_OUT6_CON7 */
#define OUT_RCH_DATA_REG_SFT                                      0
#define OUT_RCH_DATA_REG_MASK                                     0xffffffff
#define OUT_RCH_DATA_REG_MASK_SFT                                 (0xffffffff << 0)

/* ETDM_OUT0_CON8 */
/* ETDM_OUT1_CON8 */
/* ETDM_OUT2_CON8 */
/* ETDM_OUT3_CON8 */
/* ETDM_OUT4_CON8 */
/* ETDM_OUT5_CON8 */
/* ETDM_OUT6_CON8 */
#define OUT_REG_START_CH_PAIR8_SFT                                0
#define OUT_REG_START_CH_PAIR8_MASK                               0xf
#define OUT_REG_START_CH_PAIR8_MASK_SFT                           (0xf << 0)
#define OUT_REG_START_CH_PAIR9_SFT                                4
#define OUT_REG_START_CH_PAIR9_MASK                               0xf
#define OUT_REG_START_CH_PAIR9_MASK_SFT                           (0xf << 4)
#define OUT_REG_START_CH_PAIR10_SFT                               8
#define OUT_REG_START_CH_PAIR10_MASK                              0xf
#define OUT_REG_START_CH_PAIR10_MASK_SFT                          (0xf << 8)
#define OUT_REG_START_CH_PAIR11_SFT                               12
#define OUT_REG_START_CH_PAIR11_MASK                              0xf
#define OUT_REG_START_CH_PAIR11_MASK_SFT                          (0xf << 12)
#define OUT_REG_START_CH_PAIR12_SFT                               16
#define OUT_REG_START_CH_PAIR12_MASK                              0xf
#define OUT_REG_START_CH_PAIR12_MASK_SFT                          (0xf << 16)
#define OUT_REG_START_CH_PAIR13_SFT                               20
#define OUT_REG_START_CH_PAIR13_MASK                              0xf
#define OUT_REG_START_CH_PAIR13_MASK_SFT                          (0xf << 20)
#define OUT_REG_START_CH_PAIR14_SFT                               24
#define OUT_REG_START_CH_PAIR14_MASK                              0xf
#define OUT_REG_START_CH_PAIR14_MASK_SFT                          (0xf << 24)
#define OUT_REG_START_CH_PAIR15_SFT                               28
#define OUT_REG_START_CH_PAIR15_MASK                              0xf
#define OUT_REG_START_CH_PAIR15_MASK_SFT                          (0xf << 28)

/* ETDM_OUT0_CON9 */
/* ETDM_OUT1_CON9 */
/* ETDM_OUT2_CON9 */
/* ETDM_OUT3_CON9 */
/* ETDM_OUT4_CON9 */
/* ETDM_OUT5_CON9 */
/* ETDM_OUT6_CON9 */
#define OUT_REG_AFIFO_THRESHOLD_SFT                               29
#define OUT_REG_AFIFO_THRESHOLD_MASK                              0x3
#define OUT_REG_AFIFO_THRESHOLD_MASK_SFT                          (0x3 << 29)
#define OUT_REG_AFIFO_SW_RESET_SFT                                15
#define OUT_REG_AFIFO_SW_RESET_MASK                               0x1
#define OUT_REG_AFIFO_SW_RESET_MASK_SFT                           (0x1 << 15)
#define OUT_REG_AFIFO_RESET_SEL_SFT                               14
#define OUT_REG_AFIFO_RESET_SEL_MASK                              0x1
#define OUT_REG_AFIFO_RESET_SEL_MASK_SFT                          (0x1 << 14)
#define OUT_REG_AFIFO_AUTO_RESET_DIS_SFT                          9
#define OUT_REG_AFIFO_AUTO_RESET_DIS_MASK                         0x1
#define OUT_REG_AFIFO_AUTO_RESET_DIS_MASK_SFT                     (0x1 << 9)
#define OUT_REG_ETDM_USE_AFIFO_SFT                                8
#define OUT_REG_ETDM_USE_AFIFO_MASK                               0x1
#define OUT_REG_ETDM_USE_AFIFO_MASK_SFT                           (0x1 << 8)
#define OUT_REG_AFIFO_CLOCK_DOMAIN_SEL_SFT                        5
#define OUT_REG_AFIFO_CLOCK_DOMAIN_SEL_MASK                       0x7
#define OUT_REG_AFIFO_CLOCK_DOMAIN_SEL_MASK_SFT                   (0x7 << 5)
#define OUT_REG_AFIFO_MODE_SFT                                    0
#define OUT_REG_AFIFO_MODE_MASK                                   0x1f
#define OUT_REG_AFIFO_MODE_MASK_SFT                               (0x1f << 0)

/* ETDM_OUT0_MON */
/* ETDM_OUT1_MON */
/* ETDM_OUT2_MON */
/* ETDM_OUT3_MON */
/* ETDM_OUT4_MON */
/* ETDM_OUT5_MON */
/* ETDM_OUT6_MON */
#define LRCK_INV_SFT                                          30
#define LRCK_INV_MASK                                         0x1
#define LRCK_INV_MASK_SFT                                     (0x1 << 30)
#define EN_SYNC_OUT_SFT                                       29
#define EN_SYNC_OUT_MASK                                      0x1
#define EN_SYNC_OUT_MASK_SFT                                  (0x1 << 29)
#define HOPPING_EN_SYNC_OUT_PRE_SFT                           28
#define HOPPING_EN_SYNC_OUT_PRE_MASK                          0x1
#define HOPPING_EN_SYNC_OUT_PRE_MASK_SFT                      (0x1 << 28)
#define ETDM_2X_CK_EN_SFT                                     25
#define ETDM_2X_CK_EN_MASK                                    0x1
#define ETDM_2X_CK_EN_MASK_SFT                                (0x1 << 25)
#define ETDM_1X_CK_EN_SFT                                     24
#define ETDM_1X_CK_EN_MASK                                    0x1
#define ETDM_1X_CK_EN_MASK_SFT                                (0x1 << 24)
#define SDATA0_SFT                                            23
#define SDATA0_MASK                                           0x1
#define SDATA0_MASK_SFT                                       (0x1 << 23)
#define CURRENT_STATUS_SFT                                    21
#define CURRENT_STATUS_MASK                                   0x3
#define CURRENT_STATUS_MASK_SFT                               (0x3 << 21)
#define BIT_POINT_SFT                                         16
#define BIT_POINT_MASK                                        0x1f
#define BIT_POINT_MASK_SFT                                    (0x1f << 16)
#define BIT_CH_COUNT_SFT                                      10
#define BIT_CH_COUNT_MASK                                     0x3f
#define BIT_CH_COUNT_MASK_SFT                                 (0x3f << 10)
#define BIT_COUNT_SFT                                         5
#define BIT_COUNT_MASK                                        0x1f
#define BIT_COUNT_MASK_SFT                                    (0x1f << 5)
#define CH_COUNT_SFT                                          0
#define CH_COUNT_MASK                                         0x1f
#define CH_COUNT_MASK_SFT                                     (0x1f << 0)

/* ETDM_0_3_COWORK_CON0 */
#define ETDM_OUT0_DATA_SEL_SFT                                0
#define ETDM_OUT0_DATA_SEL_MASK                               0xf
#define ETDM_OUT0_DATA_SEL_MASK_SFT                           (0xf << 0)
#define ETDM_OUT0_SYNC_SEL_SFT                                4
#define ETDM_OUT0_SYNC_SEL_MASK                               0xf
#define ETDM_OUT0_SYNC_SEL_MASK_SFT                           (0xf << 4)
#define ETDM_OUT0_SLAVE_SEL_SFT                               8
#define ETDM_OUT0_SLAVE_SEL_MASK                              0xf
#define ETDM_OUT0_SLAVE_SEL_MASK_SFT                          (0xf << 8)
#define ETDM_OUT1_DATA_SEL_SFT                                12
#define ETDM_OUT1_DATA_SEL_MASK                               0xf
#define ETDM_OUT1_DATA_SEL_MASK_SFT                           (0xf << 12)
#define ETDM_OUT1_SYNC_SEL_SFT                                16
#define ETDM_OUT1_SYNC_SEL_MASK                               0xf
#define ETDM_OUT1_SYNC_SEL_MASK_SFT                           (0xf << 16)
#define ETDM_OUT1_SLAVE_SEL_SFT                               20
#define ETDM_OUT1_SLAVE_SEL_MASK                              0xf
#define ETDM_OUT1_SLAVE_SEL_MASK_SFT                          (0xf << 20)
#define ETDM_IN0_SLAVE_SEL_SFT                                24
#define ETDM_IN0_SLAVE_SEL_MASK                               0xf
#define ETDM_IN0_SLAVE_SEL_MASK_SFT                           (0xf << 24)
#define ETDM_IN0_SYNC_SEL_SFT                                 28
#define ETDM_IN0_SYNC_SEL_MASK                                0xf
#define ETDM_IN0_SYNC_SEL_MASK_SFT                            (0xf << 28)

/* ETDM_0_3_COWORK_CON1 */
#define ETDM_IN0_SDATA0_SEL_SFT                               0
#define ETDM_IN0_SDATA0_SEL_MASK                              0xf
#define ETDM_IN0_SDATA0_SEL_MASK_SFT                          (0xf << 0)
#define ETDM_IN0_SDATA1_15_SEL_SFT                            4
#define ETDM_IN0_SDATA1_15_SEL_MASK                           0xf
#define ETDM_IN0_SDATA1_15_SEL_MASK_SFT                       (0xf << 4)
#define ETDM_IN1_SLAVE_SEL_SFT                                8
#define ETDM_IN1_SLAVE_SEL_MASK                               0xf
#define ETDM_IN1_SLAVE_SEL_MASK_SFT                           (0xf << 8)
#define ETDM_IN1_SYNC_SEL_SFT                                 12
#define ETDM_IN1_SYNC_SEL_MASK                                0xf
#define ETDM_IN1_SYNC_SEL_MASK_SFT                            (0xf << 12)
#define ETDM_IN1_SDATA0_SEL_SFT                               16
#define ETDM_IN1_SDATA0_SEL_MASK                              0xf
#define ETDM_IN1_SDATA0_SEL_MASK_SFT                          (0xf << 16)
#define ETDM_IN1_SDATA1_15_SEL_SFT                            20
#define ETDM_IN1_SDATA1_15_SEL_MASK                           0xf
#define ETDM_IN1_SDATA1_15_SEL_MASK_SFT                       (0xf << 20)

/* ETDM_0_3_COWORK_CON2 */
#define ETDM_OUT2_DATA_SEL_SFT                                0
#define ETDM_OUT2_DATA_SEL_MASK                               0xf
#define ETDM_OUT2_DATA_SEL_MASK_SFT                           (0xf << 0)
#define ETDM_OUT2_SYNC_SEL_SFT                                4
#define ETDM_OUT2_SYNC_SEL_MASK                               0xf
#define ETDM_OUT2_SYNC_SEL_MASK_SFT                           (0xf << 4)
#define ETDM_OUT2_SLAVE_SEL_SFT                               8
#define ETDM_OUT2_SLAVE_SEL_MASK                              0xf
#define ETDM_OUT2_SLAVE_SEL_MASK_SFT                          (0xf << 8)
#define ETDM_OUT3_DATA_SEL_SFT                                12
#define ETDM_OUT3_DATA_SEL_MASK                               0xf
#define ETDM_OUT3_DATA_SEL_MASK_SFT                           (0xf << 12)
#define ETDM_OUT3_SYNC_SEL_SFT                                16
#define ETDM_OUT3_SYNC_SEL_MASK                               0xf
#define ETDM_OUT3_SYNC_SEL_MASK_SFT                           (0xf << 16)
#define ETDM_OUT3_SLAVE_SEL_SFT                               20
#define ETDM_OUT3_SLAVE_SEL_MASK                              0xf
#define ETDM_OUT3_SLAVE_SEL_MASK_SFT                          (0xf << 20)
#define ETDM_IN2_SLAVE_SEL_SFT                                24
#define ETDM_IN2_SLAVE_SEL_MASK                               0xf
#define ETDM_IN2_SLAVE_SEL_MASK_SFT                           (0xf << 24)
#define ETDM_IN2_SYNC_SEL_SFT                                 28
#define ETDM_IN2_SYNC_SEL_MASK                                0xf
#define ETDM_IN2_SYNC_SEL_MASK_SFT                            (0xf << 28)

/* ETDM_0_3_COWORK_CON3 */
#define ETDM_IN2_SDATA0_SEL_SFT                               0
#define ETDM_IN2_SDATA0_SEL_MASK                              0xf
#define ETDM_IN2_SDATA0_SEL_MASK_SFT                          (0xf << 0)
#define ETDM_IN2_SDATA1_15_SEL_SFT                            4
#define ETDM_IN2_SDATA1_15_SEL_MASK                           0xf
#define ETDM_IN2_SDATA1_15_SEL_MASK_SFT                       (0xf << 4)
#define ETDM_IN3_SLAVE_SEL_SFT                                8
#define ETDM_IN3_SLAVE_SEL_MASK                               0xf
#define ETDM_IN3_SLAVE_SEL_MASK_SFT                           (0xf << 8)
#define ETDM_IN3_SYNC_SEL_SFT                                 12
#define ETDM_IN3_SYNC_SEL_MASK                                0xf
#define ETDM_IN3_SYNC_SEL_MASK_SFT                            (0xf << 12)
#define ETDM_IN3_SDATA0_SEL_SFT                               16
#define ETDM_IN3_SDATA0_SEL_MASK                              0xf
#define ETDM_IN3_SDATA0_SEL_MASK_SFT                          (0xf << 16)
#define ETDM_IN3_SDATA1_15_SEL_SFT                            20
#define ETDM_IN3_SDATA1_15_SEL_MASK                           0xf
#define ETDM_IN3_SDATA1_15_SEL_MASK_SFT                       (0xf << 20)

/* ETDM_4_7_COWORK_CON0 */
#define ETDM_OUT4_DATA_SEL_SFT                                0
#define ETDM_OUT4_DATA_SEL_MASK                               0xf
#define ETDM_OUT4_DATA_SEL_MASK_SFT                           (0xf << 0)
#define ETDM_OUT4_SYNC_SEL_SFT                                4
#define ETDM_OUT4_SYNC_SEL_MASK                               0xf
#define ETDM_OUT4_SYNC_SEL_MASK_SFT                           (0xf << 4)
#define ETDM_OUT4_SLAVE_SEL_SFT                               8
#define ETDM_OUT4_SLAVE_SEL_MASK                              0xf
#define ETDM_OUT4_SLAVE_SEL_MASK_SFT                          (0xf << 8)
#define ETDM_OUT5_DATA_SEL_SFT                                12
#define ETDM_OUT5_DATA_SEL_MASK                               0xf
#define ETDM_OUT5_DATA_SEL_MASK_SFT                           (0xf << 12)
#define ETDM_OUT5_SYNC_SEL_SFT                                16
#define ETDM_OUT5_SYNC_SEL_MASK                               0xf
#define ETDM_OUT5_SYNC_SEL_MASK_SFT                           (0xf << 16)
#define ETDM_OUT5_SLAVE_SEL_SFT                               20
#define ETDM_OUT5_SLAVE_SEL_MASK                              0xf
#define ETDM_OUT5_SLAVE_SEL_MASK_SFT                          (0xf << 20)
#define ETDM_IN4_SLAVE_SEL_SFT                                24
#define ETDM_IN4_SLAVE_SEL_MASK                               0xf
#define ETDM_IN4_SLAVE_SEL_MASK_SFT                           (0xf << 24)
#define ETDM_IN4_SYNC_SEL_SFT                                 28
#define ETDM_IN4_SYNC_SEL_MASK                                0xf
#define ETDM_IN4_SYNC_SEL_MASK_SFT                            (0xf << 28)

/* ETDM_4_7_COWORK_CON1 */
#define ETDM_IN4_SDATA0_SEL_SFT                               0
#define ETDM_IN4_SDATA0_SEL_MASK                              0xf
#define ETDM_IN4_SDATA0_SEL_MASK_SFT                          (0xf << 0)
#define ETDM_IN4_SDATA1_15_SEL_SFT                            4
#define ETDM_IN4_SDATA1_15_SEL_MASK                           0xf
#define ETDM_IN4_SDATA1_15_SEL_MASK_SFT                       (0xf << 4)
#define ETDM_IN5_SLAVE_SEL_SFT                                8
#define ETDM_IN5_SLAVE_SEL_MASK                               0xf
#define ETDM_IN5_SLAVE_SEL_MASK_SFT                           (0xf << 8)
#define ETDM_IN5_SYNC_SEL_SFT                                 12
#define ETDM_IN5_SYNC_SEL_MASK                                0xf
#define ETDM_IN5_SYNC_SEL_MASK_SFT                            (0xf << 12)
#define ETDM_IN5_SDATA0_SEL_SFT                               16
#define ETDM_IN5_SDATA0_SEL_MASK                              0xf
#define ETDM_IN5_SDATA0_SEL_MASK_SFT                          (0xf << 16)
#define ETDM_IN5_SDATA1_15_SEL_SFT                            20
#define ETDM_IN5_SDATA1_15_SEL_MASK                           0xf
#define ETDM_IN5_SDATA1_15_SEL_MASK_SFT                       (0xf << 20)

/* ETDM_4_7_COWORK_CON2 */
#define ETDM_OUT6_DATA_SEL_SFT                                0
#define ETDM_OUT6_DATA_SEL_MASK                               0xf
#define ETDM_OUT6_DATA_SEL_MASK_SFT                           (0xf << 0)
#define ETDM_OUT6_SYNC_SEL_SFT                                4
#define ETDM_OUT6_SYNC_SEL_MASK                               0xf
#define ETDM_OUT6_SYNC_SEL_MASK_SFT                           (0xf << 4)
#define ETDM_OUT6_SLAVE_SEL_SFT                               8
#define ETDM_OUT6_SLAVE_SEL_MASK                              0xf
#define ETDM_OUT6_SLAVE_SEL_MASK_SFT                          (0xf << 8)
#define ETDM_OUT7_DATA_SEL_SFT                                12
#define ETDM_OUT7_DATA_SEL_MASK                               0xf
#define ETDM_OUT7_DATA_SEL_MASK_SFT                           (0xf << 12)
#define ETDM_OUT7_SYNC_SEL_SFT                                16
#define ETDM_OUT7_SYNC_SEL_MASK                               0xf
#define ETDM_OUT7_SYNC_SEL_MASK_SFT                           (0xf << 16)
#define ETDM_OUT7_SLAVE_SEL_SFT                               20
#define ETDM_OUT7_SLAVE_SEL_MASK                              0xf
#define ETDM_OUT7_SLAVE_SEL_MASK_SFT                          (0xf << 20)
#define ETDM_IN6_SLAVE_SEL_SFT                                24
#define ETDM_IN6_SLAVE_SEL_MASK                               0xf
#define ETDM_IN6_SLAVE_SEL_MASK_SFT                           (0xf << 24)
#define ETDM_IN6_SYNC_SEL_SFT                                 28
#define ETDM_IN6_SYNC_SEL_MASK                                0xf
#define ETDM_IN6_SYNC_SEL_MASK_SFT                            (0xf << 28)

/* ETDM_4_7_COWORK_CON3 */
#define ETDM_IN6_SDATA0_SEL_SFT                               0
#define ETDM_IN6_SDATA0_SEL_MASK                              0xf
#define ETDM_IN6_SDATA0_SEL_MASK_SFT                          (0xf << 0)
#define ETDM_IN6_SDATA1_15_SEL_SFT                            4
#define ETDM_IN6_SDATA1_15_SEL_MASK                           0xf
#define ETDM_IN6_SDATA1_15_SEL_MASK_SFT                       (0xf << 4)
#define ETDM_IN7_SLAVE_SEL_SFT                                8
#define ETDM_IN7_SLAVE_SEL_MASK                               0xf
#define ETDM_IN7_SLAVE_SEL_MASK_SFT                           (0xf << 8)
#define ETDM_IN7_SYNC_SEL_SFT                                 12
#define ETDM_IN7_SYNC_SEL_MASK                                0xf
#define ETDM_IN7_SYNC_SEL_MASK_SFT                            (0xf << 12)
#define ETDM_IN7_SDATA0_SEL_SFT                               16
#define ETDM_IN7_SDATA0_SEL_MASK                              0xf
#define ETDM_IN7_SDATA0_SEL_MASK_SFT                          (0xf << 16)
#define ETDM_IN7_SDATA1_15_SEL_SFT                            20
#define ETDM_IN7_SDATA1_15_SEL_MASK                           0xf
#define ETDM_IN7_SDATA1_15_SEL_MASK_SFT                       (0xf << 20)

/* AFE_DPTX_CON */
#define DPTX_CHANNEL_ENABLE_SFT                               8
#define DPTX_CHANNEL_ENABLE_MASK                              0xff
#define DPTX_CHANNEL_ENABLE_MASK_SFT                          (0xff << 8)
#define DPTX_REGISTER_MONITOR_SELECT_SFT                      3
#define DPTX_REGISTER_MONITOR_SELECT_MASK                     0xf
#define DPTX_REGISTER_MONITOR_SELECT_MASK_SFT                 (0xf << 3)
#define DPTX_16BIT_SFT                                        2
#define DPTX_16BIT_MASK                                       0x1
#define DPTX_16BIT_MASK_SFT                                   (0x1 << 2)
#define DPTX_CHANNEL_NUMBER_SFT                               1
#define DPTX_CHANNEL_NUMBER_MASK                              0x1
#define DPTX_CHANNEL_NUMBER_MASK_SFT                          (0x1 << 1)
#define DPTX_ON_SFT                                           0
#define DPTX_ON_MASK                                          0x1
#define DPTX_ON_MASK_SFT                                      (0x1 << 0)

/* AFE_DPTX_MON */
#define AFE_DPTX_MON0_SFT                                     0
#define AFE_DPTX_MON0_MASK                                    0xffffffff
#define AFE_DPTX_MON0_MASK_SFT                                (0xffffffff << 0)

/* AFE_TDM_CON1 */
#define TDM_EN_SFT                                            0
#define TDM_EN_MASK                                           0x1
#define TDM_EN_MASK_SFT                                       (0x1 << 0)
#define BCK_INVERSE_SFT                                       1
#define BCK_INVERSE_MASK                                      0x1
#define BCK_INVERSE_MASK_SFT                                  (0x1 << 1)
#define LRCK_INVERSE_SFT                                      2
#define LRCK_INVERSE_MASK                                     0x1
#define LRCK_INVERSE_MASK_SFT                                 (0x1 << 2)
#define DELAY_DATA_SFT                                        3
#define DELAY_DATA_MASK                                       0x1
#define DELAY_DATA_MASK_SFT                                   (0x1 << 3)
#define LEFT_ALIGN_SFT                                        4
#define LEFT_ALIGN_MASK                                       0x1
#define LEFT_ALIGN_MASK_SFT                                   (0x1 << 4)
#define TDM_LRCK_D0P5T_SFT                                    5
#define TDM_LRCK_D0P5T_MASK                                   0x1
#define TDM_LRCK_D0P5T_MASK_SFT                               (0x1 << 5)
#define TDM_SDATA_D0P5T_SFT                                   6
#define TDM_SDATA_D0P5T_MASK                                  0x1
#define TDM_SDATA_D0P5T_MASK_SFT                              (0x1 << 6)
#define WLEN_SFT                                              8
#define WLEN_MASK                                             0x3
#define WLEN_MASK_SFT                                         (0x3 << 8)
#define CHANNEL_NUM_SFT                                       10
#define CHANNEL_NUM_MASK                                      0x3
#define CHANNEL_NUM_MASK_SFT                                  (0x3 << 10)
#define CHANNEL_BCK_CYCLES_SFT                                12
#define CHANNEL_BCK_CYCLES_MASK                               0x3
#define CHANNEL_BCK_CYCLES_MASK_SFT                           (0x3 << 12)
#define HDMI_CLK_INV_SEL_SFT                                  15
#define HDMI_CLK_INV_SEL_MASK                                 0x1
#define HDMI_CLK_INV_SEL_MASK_SFT                             (0x1 << 15)
#define DAC_BIT_NUM_SFT                                       16
#define DAC_BIT_NUM_MASK                                      0x1f
#define DAC_BIT_NUM_MASK_SFT                                  (0x1f << 16)
#define LRCK_TDM_WIDTH_SFT                                    24
#define LRCK_TDM_WIDTH_MASK                                   0xff
#define LRCK_TDM_WIDTH_MASK_SFT                               (0xff << 24)

/* AFE_TDM_CON2 */
#define ST_CH_PAIR_SOUT0_SFT                                  0
#define ST_CH_PAIR_SOUT0_MASK                                 0x7
#define ST_CH_PAIR_SOUT0_MASK_SFT                             (0x7 << 0)
#define ST_CH_PAIR_SOUT1_SFT                                  4
#define ST_CH_PAIR_SOUT1_MASK                                 0x7
#define ST_CH_PAIR_SOUT1_MASK_SFT                             (0x7 << 4)
#define ST_CH_PAIR_SOUT2_SFT                                  8
#define ST_CH_PAIR_SOUT2_MASK                                 0x7
#define ST_CH_PAIR_SOUT2_MASK_SFT                             (0x7 << 8)
#define ST_CH_PAIR_SOUT3_SFT                                  12
#define ST_CH_PAIR_SOUT3_MASK                                 0x7
#define ST_CH_PAIR_SOUT3_MASK_SFT                             (0x7 << 12)
#define TDM_FIX_VALUE_SEL_SFT                                 16
#define TDM_FIX_VALUE_SEL_MASK                                0x1
#define TDM_FIX_VALUE_SEL_MASK_SFT                            (0x1 << 16)
#define TDM_I2S_LOOPBACK_SFT                                  20
#define TDM_I2S_LOOPBACK_MASK                                 0x1
#define TDM_I2S_LOOPBACK_MASK_SFT                             (0x1 << 20)
#define TDM_I2S_LOOPBACK_CH_SFT                               21
#define TDM_I2S_LOOPBACK_CH_MASK                              0x3
#define TDM_I2S_LOOPBACK_CH_MASK_SFT                          (0x3 << 21)
#define TDM_USE_SINEGEN_INPUT_SFT                             23
#define TDM_USE_SINEGEN_INPUT_MASK                            0x1
#define TDM_USE_SINEGEN_INPUT_MASK_SFT                        (0x1 << 23)
#define TDM_FIX_VALUE_SFT                                     24
#define TDM_FIX_VALUE_MASK                                    0xff
#define TDM_FIX_VALUE_MASK_SFT                                (0xff << 24)

/* AFE_TDM_CON3 */
#define TDM_OUT_SEL_DOMAIN_SFT                                29
#define TDM_OUT_SEL_DOMAIN_MASK                               0x7
#define TDM_OUT_SEL_DOMAIN_MASK_SFT                           (0x7 << 29)
#define TDM_OUT_SEL_FS_SFT                                    24
#define TDM_OUT_SEL_FS_MASK                                   0x1f
#define TDM_OUT_SEL_FS_MASK_SFT                               (0x1f << 24)
#define TDM_OUT_MON_SEL_SFT                                   3
#define TDM_OUT_MON_SEL_MASK                                  0x1
#define TDM_OUT_MON_SEL_MASK_SFT                              (0x1 << 3)
#define RG_TDM_OUT_ASYNC_FIFO_SOFT_RST_EN_SFT                 2
#define RG_TDM_OUT_ASYNC_FIFO_SOFT_RST_EN_MASK                0x1
#define RG_TDM_OUT_ASYNC_FIFO_SOFT_RST_EN_MASK_SFT            (0x1 << 2)
#define RG_TDM_OUT_ASYNC_FIFO_SOFT_RST_SFT                    1
#define RG_TDM_OUT_ASYNC_FIFO_SOFT_RST_MASK                   0x1
#define RG_TDM_OUT_ASYNC_FIFO_SOFT_RST_MASK_SFT               (0x1 << 1)
#define TDM_UPDATE_EN_SEL_SFT                                 0
#define TDM_UPDATE_EN_SEL_MASK                                0x1
#define TDM_UPDATE_EN_SEL_MASK_SFT                            (0x1 << 0)

/* AFE_TDM_OUT_MON */
#define AFE_TDM_OUT_MON_SFT                                   0
#define AFE_TDM_OUT_MON_MASK                                  0xffffffff
#define AFE_TDM_OUT_MON_MASK_SFT                              (0xffffffff << 0)

/* AFE_HDMI_CONN0 */
#define HDMI_O_7_SFT                                          21
#define HDMI_O_7_MASK                                         0x7
#define HDMI_O_7_MASK_SFT                                     (0x7 << 21)
#define HDMI_O_6_SFT                                          18
#define HDMI_O_6_MASK                                         0x7
#define HDMI_O_6_MASK_SFT                                     (0x7 << 18)
#define HDMI_O_5_SFT                                          15
#define HDMI_O_5_MASK                                         0x7
#define HDMI_O_5_MASK_SFT                                     (0x7 << 15)
#define HDMI_O_4_SFT                                          12
#define HDMI_O_4_MASK                                         0x7
#define HDMI_O_4_MASK_SFT                                     (0x7 << 12)
#define HDMI_O_3_SFT                                          9
#define HDMI_O_3_MASK                                         0x7
#define HDMI_O_3_MASK_SFT                                     (0x7 << 9)
#define HDMI_O_2_SFT                                          6
#define HDMI_O_2_MASK                                         0x7
#define HDMI_O_2_MASK_SFT                                     (0x7 << 6)
#define HDMI_O_1_SFT                                          3
#define HDMI_O_1_MASK                                         0x7
#define HDMI_O_1_MASK_SFT                                     (0x7 << 3)
#define HDMI_O_0_SFT                                          0
#define HDMI_O_0_MASK                                         0x7
#define HDMI_O_0_MASK_SFT                                     (0x7 << 0)

/* AFE_TDM_TOP_IP_VERSION */
#define AFE_TDM_TOP_IP_VERSION_SFT                            0
#define AFE_TDM_TOP_IP_VERSION_MASK                           0xffffffff
#define AFE_TDM_TOP_IP_VERSION_MASK_SFT                       (0xffffffff << 0)

/* AFE_CBIP_CFG0 */
#define CBIP_TOP_SLV_MUX_WAY_EN_SFT                           16
#define CBIP_TOP_SLV_MUX_WAY_EN_MASK                          0xffff
#define CBIP_TOP_SLV_MUX_WAY_EN_MASK_SFT                      (0xffff << 16)
#define RESERVED_04_SFT                                       15
#define RESERVED_04_MASK                                      0x1
#define RESERVED_04_MASK_SFT                                  (0x1 << 15)
#define CBIP_ASYNC_MST_RG_FIFO_THRE_SFT                       13
#define CBIP_ASYNC_MST_RG_FIFO_THRE_MASK                      0x3
#define CBIP_ASYNC_MST_RG_FIFO_THRE_MASK_SFT                  (0x3 << 13)
#define CBIP_ASYNC_MST_POSTWRITE_DIS_SFT                      12
#define CBIP_ASYNC_MST_POSTWRITE_DIS_MASK                     0x1
#define CBIP_ASYNC_MST_POSTWRITE_DIS_MASK_SFT                 (0x1 << 12)
#define RESERVED_03_SFT                                       11
#define RESERVED_03_MASK                                      0x1
#define RESERVED_03_MASK_SFT                                  (0x1 << 11)
#define CBIP_ASYNC_SLV_RG_FIFO_THRE_SFT                       9
#define CBIP_ASYNC_SLV_RG_FIFO_THRE_MASK                      0x3
#define CBIP_ASYNC_SLV_RG_FIFO_THRE_MASK_SFT                  (0x3 << 9)
#define CBIP_ASYNC_SLV_POSTWRITE_DIS_SFT                      8
#define CBIP_ASYNC_SLV_POSTWRITE_DIS_MASK                     0x1
#define CBIP_ASYNC_SLV_POSTWRITE_DIS_MASK_SFT                 (0x1 << 8)
#define AUDIOSYS_BUSY_SFT                                     7
#define AUDIOSYS_BUSY_MASK                                    0x1
#define AUDIOSYS_BUSY_MASK_SFT                                (0x1 << 7)
#define CBIP_SLV_DECODER_ERR_FLAG_EN_SFT                      6
#define CBIP_SLV_DECODER_ERR_FLAG_EN_MASK                     0x1
#define CBIP_SLV_DECODER_ERR_FLAG_EN_MASK_SFT                 (0x1 << 6)
#define CBIP_SLV_DECODER_SLAVE_WAY_EN_SFT                     5
#define CBIP_SLV_DECODER_SLAVE_WAY_EN_MASK                    0x1
#define CBIP_SLV_DECODER_SLAVE_WAY_EN_MASK_SFT                (0x1 << 5)
#define APB_R2T_SFT                                           3
#define APB_R2T_MASK                                          0x1
#define APB_R2T_MASK_SFT                                      (0x1 << 3)
#define APB_W2T_SFT                                           2
#define APB_W2T_MASK                                          0x1
#define APB_W2T_MASK_SFT                                      (0x1 << 2)
#define AHB_IDLE_EN_INT_SFT                                   1
#define AHB_IDLE_EN_INT_MASK                                  0x1
#define AHB_IDLE_EN_INT_MASK_SFT                              (0x1 << 1)
#define AHB_IDLE_EN_EXT_SFT                                   0
#define AHB_IDLE_EN_EXT_MASK                                  0x1
#define AHB_IDLE_EN_EXT_MASK_SFT                              (0x1 << 0)

/* AFE_CBIP_SLV_DECODER_MON0 */
#define CBIP_SLV_DECODER_ERR_DOMAIN_SFT                       4
#define CBIP_SLV_DECODER_ERR_DOMAIN_MASK                      0x1
#define CBIP_SLV_DECODER_ERR_DOMAIN_MASK_SFT                  (0x1 << 4)
#define CBIP_SLV_DECODER_ERR_ID_SFT                           3
#define CBIP_SLV_DECODER_ERR_ID_MASK                          0x1
#define CBIP_SLV_DECODER_ERR_ID_MASK_SFT                      (0x1 << 3)
#define CBIP_SLV_DECODER_ERR_RW_SFT                           2
#define CBIP_SLV_DECODER_ERR_RW_MASK                          0x1
#define CBIP_SLV_DECODER_ERR_RW_MASK_SFT                      (0x1 << 2)
#define CBIP_SLV_DECODER_ERR_DECERR_SFT                       1
#define CBIP_SLV_DECODER_ERR_DECERR_MASK                      0x1
#define CBIP_SLV_DECODER_ERR_DECERR_MASK_SFT                  (0x1 << 1)
#define CBIP_SLV_DECODER_CTRL_UPDATE_STATUS_SFT               0
#define CBIP_SLV_DECODER_CTRL_UPDATE_STATUS_MASK              0x1
#define CBIP_SLV_DECODER_CTRL_UPDATE_STATUS_MASK_SFT          (0x1 << 0)

/* AFE_CBIP_SLV_DECODER_MON1 */
#define CBIP_SLV_DECODER_ERR_ADDR_SFT                         0
#define CBIP_SLV_DECODER_ERR_ADDR_MASK                        0xffffffff
#define CBIP_SLV_DECODER_ERR_ADDR_MASK_SFT                    (0xffffffff << 0)

/* AFE_CBIP_SLV_MUX_MON_CFG */
#define CBIP_SLV_MUX_ERR_FLAG_EN_SFT                          3
#define CBIP_SLV_MUX_ERR_FLAG_EN_MASK                         0x1
#define CBIP_SLV_MUX_ERR_FLAG_EN_MASK_SFT                     (0x1 << 3)
#define CBIP_SLV_MUX_REG_SLAVE_WAY_EN_SFT                     2
#define CBIP_SLV_MUX_REG_SLAVE_WAY_EN_MASK                    0x1
#define CBIP_SLV_MUX_REG_SLAVE_WAY_EN_MASK_SFT                (0x1 << 2)
#define CBIP_SLV_MUX_REG_LAYER_WAY_EN_SFT                     0
#define CBIP_SLV_MUX_REG_LAYER_WAY_EN_MASK                    0x3
#define CBIP_SLV_MUX_REG_LAYER_WAY_EN_MASK_SFT                (0x3 << 0)

/* AFE_CBIP_SLV_MUX_MON0 */
#define CBIP_SLV_MUX_ERR_DOMAIN_SFT                           8
#define CBIP_SLV_MUX_ERR_DOMAIN_MASK                          0x1
#define CBIP_SLV_MUX_ERR_DOMAIN_MASK_SFT                      (0x1 << 8)
#define CBIP_SLV_MUX_ERR_ID_SFT                               7
#define CBIP_SLV_MUX_ERR_ID_MASK                              0x1
#define CBIP_SLV_MUX_ERR_ID_MASK_SFT                          (0x1 << 7)
#define CBIP_SLV_MUX_ERR_RD_SFT                               6
#define CBIP_SLV_MUX_ERR_RD_MASK                              0x1
#define CBIP_SLV_MUX_ERR_RD_MASK_SFT                          (0x1 << 6)
#define CBIP_SLV_MUX_ERR_WR_SFT                               5
#define CBIP_SLV_MUX_ERR_WR_MASK                              0x1
#define CBIP_SLV_MUX_ERR_WR_MASK_SFT                          (0x1 << 5)
#define CBIP_SLV_MUX_ERR_EN_SLV_SFT                           4
#define CBIP_SLV_MUX_ERR_EN_SLV_MASK                          0x1
#define CBIP_SLV_MUX_ERR_EN_SLV_MASK_SFT                      (0x1 << 4)
#define CBIP_SLV_MUX_ERR_EN_MST_SFT                           2
#define CBIP_SLV_MUX_ERR_EN_MST_MASK                          0x3
#define CBIP_SLV_MUX_ERR_EN_MST_MASK_SFT                      (0x3 << 2)
#define CBIP_SLV_MUX_CTRL_UPDATE_STATUS_SFT                   0
#define CBIP_SLV_MUX_CTRL_UPDATE_STATUS_MASK                  0x3
#define CBIP_SLV_MUX_CTRL_UPDATE_STATUS_MASK_SFT              (0x3 << 0)

/* AFE_CBIP_SLV_MUX_MON1 */
#define CBIP_SLV_MUX_ERR_ADDR_SFT                             0
#define CBIP_SLV_MUX_ERR_ADDR_MASK                            0xffffffff
#define CBIP_SLV_MUX_ERR_ADDR_MASK_SFT                        (0xffffffff << 0)

/* AFE_MEMIF_CON0 */
#define CPU_COMPACT_MODE_SFT                                  2
#define CPU_COMPACT_MODE_MASK                                 0x1
#define CPU_COMPACT_MODE_MASK_SFT                             (0x1 << 2)
#define CPU_HD_ALIGN_SFT                                      1
#define CPU_HD_ALIGN_MASK                                     0x1
#define CPU_HD_ALIGN_MASK_SFT                                 (0x1 << 1)
#define SYSRAM_SIGN_SFT                                       0
#define SYSRAM_SIGN_MASK                                      0x1
#define SYSRAM_SIGN_MASK_SFT                                  (0x1 << 0)

/* AFE_MEMIF_ONE_HEART */
#define DL_ONE_HEART_ON_2_SFT                                 2
#define DL_ONE_HEART_ON_2_MASK                                0x1
#define DL_ONE_HEART_ON_2_MASK_SFT                            (0x1 << 2)
#define DL_ONE_HEART_ON_1_SFT                                 1
#define DL_ONE_HEART_ON_1_MASK                                0x1
#define DL_ONE_HEART_ON_1_MASK_SFT                            (0x1 << 1)
#define DL_ONE_HEART_ON_0_SFT                                 0
#define DL_ONE_HEART_ON_0_MASK                                0x1
#define DL_ONE_HEART_ON_0_MASK_SFT                            (0x1 << 0)

/* AFE_DL0_BASE_MSB */
#define DL0_BASE_ADDR_MSB_SFT                                 0
#define DL0_BASE_ADDR_MSB_MASK                                0x1ff
#define DL0_BASE_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_DL0_BASE */
#define DL0_BASE_ADDR_SFT                                     4
#define DL0_BASE_ADDR_MASK                                    0xfffffff
#define DL0_BASE_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_DL0_CUR_MSB */
#define DL0_CUR_PTR_MSB_SFT                                   0
#define DL0_CUR_PTR_MSB_MASK                                  0x1ff
#define DL0_CUR_PTR_MSB_MASK_SFT                              (0x1ff << 0)

/* AFE_DL0_CUR */
#define DL0_CUR_PTR_SFT                                       0
#define DL0_CUR_PTR_MASK                                      0xffffffff
#define DL0_CUR_PTR_MASK_SFT                                  (0xffffffff << 0)

/* AFE_DL0_END_MSB */
#define DL0_END_ADDR_MSB_SFT                                  0
#define DL0_END_ADDR_MSB_MASK                                 0x1ff
#define DL0_END_ADDR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_DL0_END */
#define DL0_END_ADDR_SFT                                      4
#define DL0_END_ADDR_MASK                                     0xfffffff
#define DL0_END_ADDR_MASK_SFT                                 (0xfffffff << 4)

/* AFE_DL0_RCH_MON */
#define DL0_RCH_DATA_SFT                                      0
#define DL0_RCH_DATA_MASK                                     0xffffffff
#define DL0_RCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL0_LCH_MON */
#define DL0_LCH_DATA_SFT                                      0
#define DL0_LCH_DATA_MASK                                     0xffffffff
#define DL0_LCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL0_CON0 */
#define DL0_ON_SFT                                            28
#define DL0_ON_MASK                                           0x1
#define DL0_ON_MASK_SFT                                       (0x1 << 28)
#define DL0_ONE_HEART_SEL_SFT                                 22
#define DL0_ONE_HEART_SEL_MASK                                0x3
#define DL0_ONE_HEART_SEL_MASK_SFT                            (0x3 << 22)
#define DL0_MINLEN_SFT                                        20
#define DL0_MINLEN_MASK                                       0x3
#define DL0_MINLEN_MASK_SFT                                   (0x3 << 20)
#define DL0_MAXLEN_SFT                                        16
#define DL0_MAXLEN_MASK                                       0x3
#define DL0_MAXLEN_MASK_SFT                                   (0x3 << 16)
#define DL0_SEL_DOMAIN_SFT                                    13
#define DL0_SEL_DOMAIN_MASK                                   0x7
#define DL0_SEL_DOMAIN_MASK_SFT                               (0x7 << 13)
#define DL0_SEL_FS_SFT                                        8
#define DL0_SEL_FS_MASK                                       0x1f
#define DL0_SEL_FS_MASK_SFT                                   (0x1f << 8)
#define DL0_SW_CLEAR_BUF_EMPTY_SFT                            7
#define DL0_SW_CLEAR_BUF_EMPTY_MASK                           0x1
#define DL0_SW_CLEAR_BUF_EMPTY_MASK_SFT                       (0x1 << 7)
#define DL0_PBUF_SIZE_SFT                                     5
#define DL0_PBUF_SIZE_MASK                                    0x3
#define DL0_PBUF_SIZE_MASK_SFT                                (0x3 << 5)
#define DL0_MONO_SFT                                          4
#define DL0_MONO_MASK                                         0x1
#define DL0_MONO_MASK_SFT                                     (0x1 << 4)
#define DL0_NORMAL_MODE_SFT                                   3
#define DL0_NORMAL_MODE_MASK                                  0x1
#define DL0_NORMAL_MODE_MASK_SFT                              (0x1 << 3)
#define DL0_HALIGN_SFT                                        2
#define DL0_HALIGN_MASK                                       0x1
#define DL0_HALIGN_MASK_SFT                                   (0x1 << 2)
#define DL0_HD_MODE_SFT                                       0
#define DL0_HD_MODE_MASK                                      0x3
#define DL0_HD_MODE_MASK_SFT                                  (0x3 << 0)

/* AFE_DL1_BASE_MSB */
#define DL1_BASE_ADDR_MSB_SFT                                 0
#define DL1_BASE_ADDR_MSB_MASK                                0x1ff
#define DL1_BASE_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_DL1_BASE */
#define DL1_BASE_ADDR_SFT                                     4
#define DL1_BASE_ADDR_MASK                                    0xfffffff
#define DL1_BASE_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_DL1_CUR_MSB */
#define DL1_CUR_PTR_MSB_SFT                                   0
#define DL1_CUR_PTR_MSB_MASK                                  0x1ff
#define DL1_CUR_PTR_MSB_MASK_SFT                              (0x1ff << 0)

/* AFE_DL1_CUR */
#define DL1_CUR_PTR_SFT                                       0
#define DL1_CUR_PTR_MASK                                      0xffffffff
#define DL1_CUR_PTR_MASK_SFT                                  (0xffffffff << 0)

/* AFE_DL1_END_MSB */
#define DL1_END_ADDR_MSB_SFT                                  0
#define DL1_END_ADDR_MSB_MASK                                 0x1ff
#define DL1_END_ADDR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_DL1_END */
#define DL1_END_ADDR_SFT                                      4
#define DL1_END_ADDR_MASK                                     0xfffffff
#define DL1_END_ADDR_MASK_SFT                                 (0xfffffff << 4)

/* AFE_DL1_RCH_MON */
#define DL1_RCH_DATA_SFT                                      0
#define DL1_RCH_DATA_MASK                                     0xffffffff
#define DL1_RCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL1_LCH_MON */
#define DL1_LCH_DATA_SFT                                      0
#define DL1_LCH_DATA_MASK                                     0xffffffff
#define DL1_LCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL1_CON0 */
#define DL1_ON_SFT                                            28
#define DL1_ON_MASK                                           0x1
#define DL1_ON_MASK_SFT                                       (0x1 << 28)
#define DL1_ONE_HEART_SEL_SFT                                 22
#define DL1_ONE_HEART_SEL_MASK                                0x3
#define DL1_ONE_HEART_SEL_MASK_SFT                            (0x3 << 22)
#define DL1_MINLEN_SFT                                        20
#define DL1_MINLEN_MASK                                       0x3
#define DL1_MINLEN_MASK_SFT                                   (0x3 << 20)
#define DL1_MAXLEN_SFT                                        16
#define DL1_MAXLEN_MASK                                       0x3
#define DL1_MAXLEN_MASK_SFT                                   (0x3 << 16)
#define DL1_SEL_DOMAIN_SFT                                    13
#define DL1_SEL_DOMAIN_MASK                                   0x7
#define DL1_SEL_DOMAIN_MASK_SFT                               (0x7 << 13)
#define DL1_SEL_FS_SFT                                        8
#define DL1_SEL_FS_MASK                                       0x1f
#define DL1_SEL_FS_MASK_SFT                                   (0x1f << 8)
#define DL1_SW_CLEAR_BUF_EMPTY_SFT                            7
#define DL1_SW_CLEAR_BUF_EMPTY_MASK                           0x1
#define DL1_SW_CLEAR_BUF_EMPTY_MASK_SFT                       (0x1 << 7)
#define DL1_PBUF_SIZE_SFT                                     5
#define DL1_PBUF_SIZE_MASK                                    0x3
#define DL1_PBUF_SIZE_MASK_SFT                                (0x3 << 5)
#define DL1_MONO_SFT                                          4
#define DL1_MONO_MASK                                         0x1
#define DL1_MONO_MASK_SFT                                     (0x1 << 4)
#define DL1_NORMAL_MODE_SFT                                   3
#define DL1_NORMAL_MODE_MASK                                  0x1
#define DL1_NORMAL_MODE_MASK_SFT                              (0x1 << 3)
#define DL1_HALIGN_SFT                                        2
#define DL1_HALIGN_MASK                                       0x1
#define DL1_HALIGN_MASK_SFT                                   (0x1 << 2)
#define DL1_HD_MODE_SFT                                       0
#define DL1_HD_MODE_MASK                                      0x3
#define DL1_HD_MODE_MASK_SFT                                  (0x3 << 0)

/* AFE_DL2_BASE_MSB */
#define DL2_BASE__ADDR_MSB_SFT                                0
#define DL2_BASE__ADDR_MSB_MASK                               0x1ff
#define DL2_BASE__ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_DL2_BASE */
#define DL2_BASE_ADDR_SFT                                     4
#define DL2_BASE_ADDR_MASK                                    0xfffffff
#define DL2_BASE_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_DL2_CUR_MSB */
#define DL2_CUR_PTR_MSB_SFT                                   0
#define DL2_CUR_PTR_MSB_MASK                                  0x1ff
#define DL2_CUR_PTR_MSB_MASK_SFT                              (0x1ff << 0)

/* AFE_DL2_CUR */
#define DL2_CUR_PTR_SFT                                       0
#define DL2_CUR_PTR_MASK                                      0xffffffff
#define DL2_CUR_PTR_MASK_SFT                                  (0xffffffff << 0)

/* AFE_DL2_END_MSB */
#define DL2_END_ADDR_MSB_SFT                                  0
#define DL2_END_ADDR_MSB_MASK                                 0x1ff
#define DL2_END_ADDR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_DL2_END */
#define DL2_END_ADDR_SFT                                      4
#define DL2_END_ADDR_MASK                                     0xfffffff
#define DL2_END_ADDR_MASK_SFT                                 (0xfffffff << 4)

/* AFE_DL2_RCH_MON */
#define DL2_RCH_DATA_SFT                                      0
#define DL2_RCH_DATA_MASK                                     0xffffffff
#define DL2_RCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL2_LCH_MON */
#define DL2_LCH_DATA_SFT                                      0
#define DL2_LCH_DATA_MASK                                     0xffffffff
#define DL2_LCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL2_CON0 */
#define DL2_ON_SFT                                            28
#define DL2_ON_MASK                                           0x1
#define DL2_ON_MASK_SFT                                       (0x1 << 28)
#define DL2_ONE_HEART_SEL_SFT                                 22
#define DL2_ONE_HEART_SEL_MASK                                0x3
#define DL2_ONE_HEART_SEL_MASK_SFT                            (0x3 << 22)
#define DL2_MINLEN_SFT                                        20
#define DL2_MINLEN_MASK                                       0x3
#define DL2_MINLEN_MASK_SFT                                   (0x3 << 20)
#define DL2_MAXLEN_SFT                                        16
#define DL2_MAXLEN_MASK                                       0x3
#define DL2_MAXLEN_MASK_SFT                                   (0x3 << 16)
#define DL2_SEL_DOMAIN_SFT                                    13
#define DL2_SEL_DOMAIN_MASK                                   0x7
#define DL2_SEL_DOMAIN_MASK_SFT                               (0x7 << 13)
#define DL2_SEL_FS_SFT                                        8
#define DL2_SEL_FS_MASK                                       0x1f
#define DL2_SEL_FS_MASK_SFT                                   (0x1f << 8)
#define DL2_SW_CLEAR_BUF_EMPTY_SFT                            7
#define DL2_SW_CLEAR_BUF_EMPTY_MASK                           0x1
#define DL2_SW_CLEAR_BUF_EMPTY_MASK_SFT                       (0x1 << 7)
#define DL2_PBUF_SIZE_SFT                                     5
#define DL2_PBUF_SIZE_MASK                                    0x3
#define DL2_PBUF_SIZE_MASK_SFT                                (0x3 << 5)
#define DL2_MONO_SFT                                          4
#define DL2_MONO_MASK                                         0x1
#define DL2_MONO_MASK_SFT                                     (0x1 << 4)
#define DL2_NORMAL_MODE_SFT                                   3
#define DL2_NORMAL_MODE_MASK                                  0x1
#define DL2_NORMAL_MODE_MASK_SFT                              (0x1 << 3)
#define DL2_HALIGN_SFT                                        2
#define DL2_HALIGN_MASK                                       0x1
#define DL2_HALIGN_MASK_SFT                                   (0x1 << 2)
#define DL2_HD_MODE_SFT                                       0
#define DL2_HD_MODE_MASK                                      0x3
#define DL2_HD_MODE_MASK_SFT                                  (0x3 << 0)

/* AFE_DL3_BASE_MSB */
#define DL3_BASE__ADDR_MSB_SFT                                0
#define DL3_BASE__ADDR_MSB_MASK                               0x1ff
#define DL3_BASE__ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_DL3_BASE */
#define DL3_BASE_ADDR_SFT                                     4
#define DL3_BASE_ADDR_MASK                                    0xfffffff
#define DL3_BASE_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_DL3_CUR_MSB */
#define DL3_CUR_PTR_MSB_SFT                                   0
#define DL3_CUR_PTR_MSB_MASK                                  0x1ff
#define DL3_CUR_PTR_MSB_MASK_SFT                              (0x1ff << 0)

/* AFE_DL3_CUR */
#define DL3_CUR_PTR_SFT                                       0
#define DL3_CUR_PTR_MASK                                      0xffffffff
#define DL3_CUR_PTR_MASK_SFT                                  (0xffffffff << 0)

/* AFE_DL3_END_MSB */
#define DL3_END_ADDR_MSB_SFT                                  0
#define DL3_END_ADDR_MSB_MASK                                 0x1ff
#define DL3_END_ADDR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_DL3_END */
#define DL3_END_ADDR_SFT                                      4
#define DL3_END_ADDR_MASK                                     0xfffffff
#define DL3_END_ADDR_MASK_SFT                                 (0xfffffff << 4)

/* AFE_DL3_RCH_MON */
#define DL3_RCH_DATA_SFT                                      0
#define DL3_RCH_DATA_MASK                                     0xffffffff
#define DL3_RCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL3_LCH_MON */
#define DL3_LCH_DATA_SFT                                      0
#define DL3_LCH_DATA_MASK                                     0xffffffff
#define DL3_LCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL3_CON0 */
#define DL3_ON_SFT                                            28
#define DL3_ON_MASK                                           0x1
#define DL3_ON_MASK_SFT                                       (0x1 << 28)
#define DL3_ONE_HEART_SEL_SFT                                 22
#define DL3_ONE_HEART_SEL_MASK                                0x3
#define DL3_ONE_HEART_SEL_MASK_SFT                            (0x3 << 22)
#define DL3_MINLEN_SFT                                        20
#define DL3_MINLEN_MASK                                       0x3
#define DL3_MINLEN_MASK_SFT                                   (0x3 << 20)
#define DL3_MAXLEN_SFT                                        16
#define DL3_MAXLEN_MASK                                       0x3
#define DL3_MAXLEN_MASK_SFT                                   (0x3 << 16)
#define DL3_SEL_DOMAIN_SFT                                    13
#define DL3_SEL_DOMAIN_MASK                                   0x7
#define DL3_SEL_DOMAIN_MASK_SFT                               (0x7 << 13)
#define DL3_SEL_FS_SFT                                        8
#define DL3_SEL_FS_MASK                                       0x1f
#define DL3_SEL_FS_MASK_SFT                                   (0x1f << 8)
#define DL3_SW_CLEAR_BUF_EMPTY_SFT                            7
#define DL3_SW_CLEAR_BUF_EMPTY_MASK                           0x1
#define DL3_SW_CLEAR_BUF_EMPTY_MASK_SFT                       (0x1 << 7)
#define DL3_PBUF_SIZE_SFT                                     5
#define DL3_PBUF_SIZE_MASK                                    0x3
#define DL3_PBUF_SIZE_MASK_SFT                                (0x3 << 5)
#define DL3_MONO_SFT                                          4
#define DL3_MONO_MASK                                         0x1
#define DL3_MONO_MASK_SFT                                     (0x1 << 4)
#define DL3_NORMAL_MODE_SFT                                   3
#define DL3_NORMAL_MODE_MASK                                  0x1
#define DL3_NORMAL_MODE_MASK_SFT                              (0x1 << 3)
#define DL3_HALIGN_SFT                                        2
#define DL3_HALIGN_MASK                                       0x1
#define DL3_HALIGN_MASK_SFT                                   (0x1 << 2)
#define DL3_HD_MODE_SFT                                       0
#define DL3_HD_MODE_MASK                                      0x3
#define DL3_HD_MODE_MASK_SFT                                  (0x3 << 0)

/* AFE_DL4_BASE_MSB */
#define DL4_BASE__ADDR_MSB_SFT                                0
#define DL4_BASE__ADDR_MSB_MASK                               0x1ff
#define DL4_BASE__ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_DL4_BASE */
#define DL4_BASE_ADDR_SFT                                     4
#define DL4_BASE_ADDR_MASK                                    0xfffffff
#define DL4_BASE_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_DL4_CUR_MSB */
#define DL4_CUR_PTR_MSB_SFT                                   0
#define DL4_CUR_PTR_MSB_MASK                                  0x1ff
#define DL4_CUR_PTR_MSB_MASK_SFT                              (0x1ff << 0)

/* AFE_DL4_CUR */
#define DL4_CUR_PTR_SFT                                       0
#define DL4_CUR_PTR_MASK                                      0xffffffff
#define DL4_CUR_PTR_MASK_SFT                                  (0xffffffff << 0)

/* AFE_DL4_END_MSB */
#define DL4_END_ADDR_MSB_SFT                                  0
#define DL4_END_ADDR_MSB_MASK                                 0x1ff
#define DL4_END_ADDR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_DL4_END */
#define DL4_END_ADDR_SFT                                      4
#define DL4_END_ADDR_MASK                                     0xfffffff
#define DL4_END_ADDR_MASK_SFT                                 (0xfffffff << 4)

/* AFE_DL4_RCH_MON */
#define DL4_RCH_DATA_SFT                                      0
#define DL4_RCH_DATA_MASK                                     0xffffffff
#define DL4_RCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL4_LCH_MON */
#define DL4_LCH_DATA_SFT                                      0
#define DL4_LCH_DATA_MASK                                     0xffffffff
#define DL4_LCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL4_CON0 */
#define DL4_ON_SFT                                            28
#define DL4_ON_MASK                                           0x1
#define DL4_ON_MASK_SFT                                       (0x1 << 28)
#define DL4_ONE_HEART_SEL_SFT                                 22
#define DL4_ONE_HEART_SEL_MASK                                0x3
#define DL4_ONE_HEART_SEL_MASK_SFT                            (0x3 << 22)
#define DL4_MINLEN_SFT                                        20
#define DL4_MINLEN_MASK                                       0x3
#define DL4_MINLEN_MASK_SFT                                   (0x3 << 20)
#define DL4_MAXLEN_SFT                                        16
#define DL4_MAXLEN_MASK                                       0x3
#define DL4_MAXLEN_MASK_SFT                                   (0x3 << 16)
#define DL4_SEL_DOMAIN_SFT                                    13
#define DL4_SEL_DOMAIN_MASK                                   0x7
#define DL4_SEL_DOMAIN_MASK_SFT                               (0x7 << 13)
#define DL4_SEL_FS_SFT                                        8
#define DL4_SEL_FS_MASK                                       0x1f
#define DL4_SEL_FS_MASK_SFT                                   (0x1f << 8)
#define DL4_SW_CLEAR_BUF_EMPTY_SFT                            7
#define DL4_SW_CLEAR_BUF_EMPTY_MASK                           0x1
#define DL4_SW_CLEAR_BUF_EMPTY_MASK_SFT                       (0x1 << 7)
#define DL4_PBUF_SIZE_SFT                                     5
#define DL4_PBUF_SIZE_MASK                                    0x3
#define DL4_PBUF_SIZE_MASK_SFT                                (0x3 << 5)
#define DL4_MONO_SFT                                          4
#define DL4_MONO_MASK                                         0x1
#define DL4_MONO_MASK_SFT                                     (0x1 << 4)
#define DL4_NORMAL_MODE_SFT                                   3
#define DL4_NORMAL_MODE_MASK                                  0x1
#define DL4_NORMAL_MODE_MASK_SFT                              (0x1 << 3)
#define DL4_HALIGN_SFT                                        2
#define DL4_HALIGN_MASK                                       0x1
#define DL4_HALIGN_MASK_SFT                                   (0x1 << 2)
#define DL4_HD_MODE_SFT                                       0
#define DL4_HD_MODE_MASK                                      0x3
#define DL4_HD_MODE_MASK_SFT                                  (0x3 << 0)

/* AFE_DL5_BASE_MSB */
#define DL5_BASE__ADDR_MSB_SFT                                0
#define DL5_BASE__ADDR_MSB_MASK                               0x1ff
#define DL5_BASE__ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_DL5_BASE */
#define DL5_BASE_ADDR_SFT                                     4
#define DL5_BASE_ADDR_MASK                                    0xfffffff
#define DL5_BASE_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_DL5_CUR_MSB */
#define DL5_CUR_PTR_MSB_SFT                                   0
#define DL5_CUR_PTR_MSB_MASK                                  0x1ff
#define DL5_CUR_PTR_MSB_MASK_SFT                              (0x1ff << 0)

/* AFE_DL5_CUR */
#define DL5_CUR_PTR_SFT                                       0
#define DL5_CUR_PTR_MASK                                      0xffffffff
#define DL5_CUR_PTR_MASK_SFT                                  (0xffffffff << 0)

/* AFE_DL5_END_MSB */
#define DL5_END_ADDR_MSB_SFT                                  0
#define DL5_END_ADDR_MSB_MASK                                 0x1ff
#define DL5_END_ADDR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_DL5_END */
#define DL5_END_ADDR_SFT                                      4
#define DL5_END_ADDR_MASK                                     0xfffffff
#define DL5_END_ADDR_MASK_SFT                                 (0xfffffff << 4)

/* AFE_DL5_RCH_MON */
#define DL5_RCH_DATA_SFT                                      0
#define DL5_RCH_DATA_MASK                                     0xffffffff
#define DL5_RCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL5_LCH_MON */
#define DL5_LCH_DATA_SFT                                      0
#define DL5_LCH_DATA_MASK                                     0xffffffff
#define DL5_LCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL5_CON0 */
#define DL5_ON_SFT                                            28
#define DL5_ON_MASK                                           0x1
#define DL5_ON_MASK_SFT                                       (0x1 << 28)
#define DL5_ONE_HEART_SEL_SFT                                 22
#define DL5_ONE_HEART_SEL_MASK                                0x3
#define DL5_ONE_HEART_SEL_MASK_SFT                            (0x3 << 22)
#define DL5_MINLEN_SFT                                        20
#define DL5_MINLEN_MASK                                       0x3
#define DL5_MINLEN_MASK_SFT                                   (0x3 << 20)
#define DL5_MAXLEN_SFT                                        16
#define DL5_MAXLEN_MASK                                       0x3
#define DL5_MAXLEN_MASK_SFT                                   (0x3 << 16)
#define DL5_SEL_DOMAIN_SFT                                    13
#define DL5_SEL_DOMAIN_MASK                                   0x7
#define DL5_SEL_DOMAIN_MASK_SFT                               (0x7 << 13)
#define DL5_SEL_FS_SFT                                        8
#define DL5_SEL_FS_MASK                                       0x1f
#define DL5_SEL_FS_MASK_SFT                                   (0x1f << 8)
#define DL5_SW_CLEAR_BUF_EMPTY_SFT                            7
#define DL5_SW_CLEAR_BUF_EMPTY_MASK                           0x1
#define DL5_SW_CLEAR_BUF_EMPTY_MASK_SFT                       (0x1 << 7)
#define DL5_PBUF_SIZE_SFT                                     5
#define DL5_PBUF_SIZE_MASK                                    0x3
#define DL5_PBUF_SIZE_MASK_SFT                                (0x3 << 5)
#define DL5_MONO_SFT                                          4
#define DL5_MONO_MASK                                         0x1
#define DL5_MONO_MASK_SFT                                     (0x1 << 4)
#define DL5_NORMAL_MODE_SFT                                   3
#define DL5_NORMAL_MODE_MASK                                  0x1
#define DL5_NORMAL_MODE_MASK_SFT                              (0x1 << 3)
#define DL5_HALIGN_SFT                                        2
#define DL5_HALIGN_MASK                                       0x1
#define DL5_HALIGN_MASK_SFT                                   (0x1 << 2)
#define DL5_HD_MODE_SFT                                       0
#define DL5_HD_MODE_MASK                                      0x3
#define DL5_HD_MODE_MASK_SFT                                  (0x3 << 0)

/* AFE_DL6_BASE_MSB */
#define DL6_BASE__ADDR_MSB_SFT                                0
#define DL6_BASE__ADDR_MSB_MASK                               0x1ff
#define DL6_BASE__ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_DL6_BASE */
#define DL6_BASE_ADDR_SFT                                     4
#define DL6_BASE_ADDR_MASK                                    0xfffffff
#define DL6_BASE_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_DL6_CUR_MSB */
#define DL6_CUR_PTR_MSB_SFT                                   0
#define DL6_CUR_PTR_MSB_MASK                                  0x1ff
#define DL6_CUR_PTR_MSB_MASK_SFT                              (0x1ff << 0)

/* AFE_DL6_CUR */
#define DL6_CUR_PTR_SFT                                       0
#define DL6_CUR_PTR_MASK                                      0xffffffff
#define DL6_CUR_PTR_MASK_SFT                                  (0xffffffff << 0)

/* AFE_DL6_END_MSB */
#define DL6_END_ADDR_MSB_SFT                                  0
#define DL6_END_ADDR_MSB_MASK                                 0x1ff
#define DL6_END_ADDR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_DL6_END */
#define DL6_END_ADDR_SFT                                      4
#define DL6_END_ADDR_MASK                                     0xfffffff
#define DL6_END_ADDR_MASK_SFT                                 (0xfffffff << 4)

/* AFE_DL6_RCH_MON */
#define DL6_RCH_DATA_SFT                                      0
#define DL6_RCH_DATA_MASK                                     0xffffffff
#define DL6_RCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL6_LCH_MON */
#define DL6_LCH_DATA_SFT                                      0
#define DL6_LCH_DATA_MASK                                     0xffffffff
#define DL6_LCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL6_CON0 */
#define DL6_ON_SFT                                            28
#define DL6_ON_MASK                                           0x1
#define DL6_ON_MASK_SFT                                       (0x1 << 28)
#define DL6_ONE_HEART_SEL_SFT                                 22
#define DL6_ONE_HEART_SEL_MASK                                0x3
#define DL6_ONE_HEART_SEL_MASK_SFT                            (0x3 << 22)
#define DL6_MINLEN_SFT                                        20
#define DL6_MINLEN_MASK                                       0x3
#define DL6_MINLEN_MASK_SFT                                   (0x3 << 20)
#define DL6_MAXLEN_SFT                                        16
#define DL6_MAXLEN_MASK                                       0x3
#define DL6_MAXLEN_MASK_SFT                                   (0x3 << 16)
#define DL6_SEL_DOMAIN_SFT                                    13
#define DL6_SEL_DOMAIN_MASK                                   0x7
#define DL6_SEL_DOMAIN_MASK_SFT                               (0x7 << 13)
#define DL6_SEL_FS_SFT                                        8
#define DL6_SEL_FS_MASK                                       0x1f
#define DL6_SEL_FS_MASK_SFT                                   (0x1f << 8)
#define DL6_SW_CLEAR_BUF_EMPTY_SFT                            7
#define DL6_SW_CLEAR_BUF_EMPTY_MASK                           0x1
#define DL6_SW_CLEAR_BUF_EMPTY_MASK_SFT                       (0x1 << 7)
#define DL6_PBUF_SIZE_SFT                                     5
#define DL6_PBUF_SIZE_MASK                                    0x3
#define DL6_PBUF_SIZE_MASK_SFT                                (0x3 << 5)
#define DL6_MONO_SFT                                          4
#define DL6_MONO_MASK                                         0x1
#define DL6_MONO_MASK_SFT                                     (0x1 << 4)
#define DL6_NORMAL_MODE_SFT                                   3
#define DL6_NORMAL_MODE_MASK                                  0x1
#define DL6_NORMAL_MODE_MASK_SFT                              (0x1 << 3)
#define DL6_HALIGN_SFT                                        2
#define DL6_HALIGN_MASK                                       0x1
#define DL6_HALIGN_MASK_SFT                                   (0x1 << 2)
#define DL6_HD_MODE_SFT                                       0
#define DL6_HD_MODE_MASK                                      0x3
#define DL6_HD_MODE_MASK_SFT                                  (0x3 << 0)

/* AFE_DL7_BASE_MSB */
#define DL7_BASE__ADDR_MSB_SFT                                0
#define DL7_BASE__ADDR_MSB_MASK                               0x1ff
#define DL7_BASE__ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_DL7_BASE */
#define DL7_BASE_ADDR_SFT                                     4
#define DL7_BASE_ADDR_MASK                                    0xfffffff
#define DL7_BASE_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_DL7_CUR_MSB */
#define DL7_CUR_PTR_MSB_SFT                                   0
#define DL7_CUR_PTR_MSB_MASK                                  0x1ff
#define DL7_CUR_PTR_MSB_MASK_SFT                              (0x1ff << 0)

/* AFE_DL7_CUR */
#define DL7_CUR_PTR_SFT                                       0
#define DL7_CUR_PTR_MASK                                      0xffffffff
#define DL7_CUR_PTR_MASK_SFT                                  (0xffffffff << 0)

/* AFE_DL7_END_MSB */
#define DL7_END_ADDR_MSB_SFT                                  0
#define DL7_END_ADDR_MSB_MASK                                 0x1ff
#define DL7_END_ADDR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_DL7_END */
#define DL7_END_ADDR_SFT                                      4
#define DL7_END_ADDR_MASK                                     0xfffffff
#define DL7_END_ADDR_MASK_SFT                                 (0xfffffff << 4)

/* AFE_DL7_RCH_MON */
#define DL7_RCH_DATA_SFT                                      0
#define DL7_RCH_DATA_MASK                                     0xffffffff
#define DL7_RCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL7_LCH_MON */
#define DL7_LCH_DATA_SFT                                      0
#define DL7_LCH_DATA_MASK                                     0xffffffff
#define DL7_LCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL7_CON0 */
#define DL7_ON_SFT                                            28
#define DL7_ON_MASK                                           0x1
#define DL7_ON_MASK_SFT                                       (0x1 << 28)
#define DL7_ONE_HEART_SEL_SFT                                 22
#define DL7_ONE_HEART_SEL_MASK                                0x3
#define DL7_ONE_HEART_SEL_MASK_SFT                            (0x3 << 22)
#define DL7_MINLEN_SFT                                        20
#define DL7_MINLEN_MASK                                       0x3
#define DL7_MINLEN_MASK_SFT                                   (0x3 << 20)
#define DL7_MAXLEN_SFT                                        16
#define DL7_MAXLEN_MASK                                       0x3
#define DL7_MAXLEN_MASK_SFT                                   (0x3 << 16)
#define DL7_SEL_DOMAIN_SFT                                    13
#define DL7_SEL_DOMAIN_MASK                                   0x7
#define DL7_SEL_DOMAIN_MASK_SFT                               (0x7 << 13)
#define DL7_SEL_FS_SFT                                        8
#define DL7_SEL_FS_MASK                                       0x1f
#define DL7_SEL_FS_MASK_SFT                                   (0x1f << 8)
#define DL7_SW_CLEAR_BUF_EMPTY_SFT                            7
#define DL7_SW_CLEAR_BUF_EMPTY_MASK                           0x1
#define DL7_SW_CLEAR_BUF_EMPTY_MASK_SFT                       (0x1 << 7)
#define DL7_PBUF_SIZE_SFT                                     5
#define DL7_PBUF_SIZE_MASK                                    0x3
#define DL7_PBUF_SIZE_MASK_SFT                                (0x3 << 5)
#define DL7_MONO_SFT                                          4
#define DL7_MONO_MASK                                         0x1
#define DL7_MONO_MASK_SFT                                     (0x1 << 4)
#define DL7_NORMAL_MODE_SFT                                   3
#define DL7_NORMAL_MODE_MASK                                  0x1
#define DL7_NORMAL_MODE_MASK_SFT                              (0x1 << 3)
#define DL7_HALIGN_SFT                                        2
#define DL7_HALIGN_MASK                                       0x1
#define DL7_HALIGN_MASK_SFT                                   (0x1 << 2)
#define DL7_HD_MODE_SFT                                       0
#define DL7_HD_MODE_MASK                                      0x3
#define DL7_HD_MODE_MASK_SFT                                  (0x3 << 0)

/* AFE_DL8_BASE_MSB */
#define DL8_BASE__ADDR_MSB_SFT                                0
#define DL8_BASE__ADDR_MSB_MASK                               0x1ff
#define DL8_BASE__ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_DL8_BASE */
#define DL8_BASE_ADDR_SFT                                     4
#define DL8_BASE_ADDR_MASK                                    0xfffffff
#define DL8_BASE_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_DL8_CUR_MSB */
#define DL8_CUR_PTR_MSB_SFT                                   0
#define DL8_CUR_PTR_MSB_MASK                                  0x1ff
#define DL8_CUR_PTR_MSB_MASK_SFT                              (0x1ff << 0)

/* AFE_DL8_CUR */
#define DL8_CUR_PTR_SFT                                       0
#define DL8_CUR_PTR_MASK                                      0xffffffff
#define DL8_CUR_PTR_MASK_SFT                                  (0xffffffff << 0)

/* AFE_DL8_END_MSB */
#define DL8_END_ADDR_MSB_SFT                                  0
#define DL8_END_ADDR_MSB_MASK                                 0x1ff
#define DL8_END_ADDR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_DL8_END */
#define DL8_END_ADDR_SFT                                      4
#define DL8_END_ADDR_MASK                                     0xfffffff
#define DL8_END_ADDR_MASK_SFT                                 (0xfffffff << 4)

/* AFE_DL8_RCH_MON */
#define DL8_RCH_DATA_SFT                                      0
#define DL8_RCH_DATA_MASK                                     0xffffffff
#define DL8_RCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL8_LCH_MON */
#define DL8_LCH_DATA_SFT                                      0
#define DL8_LCH_DATA_MASK                                     0xffffffff
#define DL8_LCH_DATA_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL8_CON0 */
#define DL8_ON_SFT                                            28
#define DL8_ON_MASK                                           0x1
#define DL8_ON_MASK_SFT                                       (0x1 << 28)
#define DL8_ONE_HEART_SEL_SFT                                 22
#define DL8_ONE_HEART_SEL_MASK                                0x3
#define DL8_ONE_HEART_SEL_MASK_SFT                            (0x3 << 22)
#define DL8_MINLEN_SFT                                        20
#define DL8_MINLEN_MASK                                       0x3
#define DL8_MINLEN_MASK_SFT                                   (0x3 << 20)
#define DL8_MAXLEN_SFT                                        16
#define DL8_MAXLEN_MASK                                       0x3
#define DL8_MAXLEN_MASK_SFT                                   (0x3 << 16)
#define DL8_SEL_DOMAIN_SFT                                    13
#define DL8_SEL_DOMAIN_MASK                                   0x7
#define DL8_SEL_DOMAIN_MASK_SFT                               (0x7 << 13)
#define DL8_SEL_FS_SFT                                        8
#define DL8_SEL_FS_MASK                                       0x1f
#define DL8_SEL_FS_MASK_SFT                                   (0x1f << 8)
#define DL8_SW_CLEAR_BUF_EMPTY_SFT                            7
#define DL8_SW_CLEAR_BUF_EMPTY_MASK                           0x1
#define DL8_SW_CLEAR_BUF_EMPTY_MASK_SFT                       (0x1 << 7)
#define DL8_PBUF_SIZE_SFT                                     5
#define DL8_PBUF_SIZE_MASK                                    0x3
#define DL8_PBUF_SIZE_MASK_SFT                                (0x3 << 5)
#define DL8_MONO_SFT                                          4
#define DL8_MONO_MASK                                         0x1
#define DL8_MONO_MASK_SFT                                     (0x1 << 4)
#define DL8_NORMAL_MODE_SFT                                   3
#define DL8_NORMAL_MODE_MASK                                  0x1
#define DL8_NORMAL_MODE_MASK_SFT                              (0x1 << 3)
#define DL8_HALIGN_SFT                                        2
#define DL8_HALIGN_MASK                                       0x1
#define DL8_HALIGN_MASK_SFT                                   (0x1 << 2)
#define DL8_HD_MODE_SFT                                       0
#define DL8_HD_MODE_MASK                                      0x3
#define DL8_HD_MODE_MASK_SFT                                  (0x3 << 0)

/* AFE_DL_4CH_BASE_MSB */
#define DL_4CH_BASE__ADDR_MSB_SFT                             0
#define DL_4CH_BASE__ADDR_MSB_MASK                            0x1ff
#define DL_4CH_BASE__ADDR_MSB_MASK_SFT                        (0x1ff << 0)

/* AFE_DL_4CH_BASE */
#define DL_4CH_BASE_ADDR_SFT                                  4
#define DL_4CH_BASE_ADDR_MASK                                 0xfffffff
#define DL_4CH_BASE_ADDR_MASK_SFT                             (0xfffffff << 4)

/* AFE_DL_4CH_CUR_MSB */
#define DL_4CH_CUR_PTR_MSB_SFT                                0
#define DL_4CH_CUR_PTR_MSB_MASK                               0x1ff
#define DL_4CH_CUR_PTR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_DL_4CH_CUR */
#define DL_4CH_CUR_PTR_SFT                                    0
#define DL_4CH_CUR_PTR_MASK                                   0xffffffff
#define DL_4CH_CUR_PTR_MASK_SFT                               (0xffffffff << 0)

/* AFE_DL_4CH_END_MSB */
#define DL_4CH_END_ADDR_MSB_SFT                               0
#define DL_4CH_END_ADDR_MSB_MASK                              0x1ff
#define DL_4CH_END_ADDR_MSB_MASK_SFT                          (0x1ff << 0)

/* AFE_DL_4CH_END */
#define DL_4CH_END_ADDR_SFT                                   4
#define DL_4CH_END_ADDR_MASK                                  0xfffffff
#define DL_4CH_END_ADDR_MASK_SFT                              (0xfffffff << 4)

/* AFE_DL_4CH_CON0 */
#define DL_4CH_ON_SFT                                         31
#define DL_4CH_ON_MASK                                        0x1
#define DL_4CH_ON_MASK_SFT                                    (0x1 << 31)
#define DL_4CH_NUM_SFT                                        24
#define DL_4CH_NUM_MASK                                       0x1f
#define DL_4CH_NUM_MASK_SFT                                   (0x1f << 24)
#define DL_4CH_ONE_HEART_SEL_SFT                              22
#define DL_4CH_ONE_HEART_SEL_MASK                             0x3
#define DL_4CH_ONE_HEART_SEL_MASK_SFT                         (0x3 << 22)
#define DL_4CH_MINLEN_SFT                                     20
#define DL_4CH_MINLEN_MASK                                    0x3
#define DL_4CH_MINLEN_MASK_SFT                                (0x3 << 20)
#define DL_4CH_MAXLEN_SFT                                     16
#define DL_4CH_MAXLEN_MASK                                    0x3
#define DL_4CH_MAXLEN_MASK_SFT                                (0x3 << 16)
#define DL_4CH_SEL_DOMAIN_SFT                                 13
#define DL_4CH_SEL_DOMAIN_MASK                                0x7
#define DL_4CH_SEL_DOMAIN_MASK_SFT                            (0x7 << 13)
#define DL_4CH_SEL_FS_SFT                                     8
#define DL_4CH_SEL_FS_MASK                                    0x1f
#define DL_4CH_SEL_FS_MASK_SFT                                (0x1f << 8)
#define DL_4CH_BUF_EMPTY_CLR_SFT                              7
#define DL_4CH_BUF_EMPTY_CLR_MASK                             0x1
#define DL_4CH_BUF_EMPTY_CLR_MASK_SFT                         (0x1 << 7)
#define DL_4CH_PBUF_SIZE_SFT                                  5
#define DL_4CH_PBUF_SIZE_MASK                                 0x3
#define DL_4CH_PBUF_SIZE_MASK_SFT                             (0x3 << 5)
#define DL_4CH_HANG_CLR_SFT                                   4
#define DL_4CH_HANG_CLR_MASK                                  0x1
#define DL_4CH_HANG_CLR_MASK_SFT                              (0x1 << 4)
#define DL_4CH_NORMAL_MODE_SFT                                3
#define DL_4CH_NORMAL_MODE_MASK                               0x1
#define DL_4CH_NORMAL_MODE_MASK_SFT                           (0x1 << 3)
#define DL_4CH_HALIGN_SFT                                     2
#define DL_4CH_HALIGN_MASK                                    0x1
#define DL_4CH_HALIGN_MASK_SFT                                (0x1 << 2)
#define DL_4CH_HD_MODE_SFT                                    0
#define DL_4CH_HD_MODE_MASK                                   0x3
#define DL_4CH_HD_MODE_MASK_SFT                               (0x3 << 0)

/* AFE_DL_24CH_BASE_MSB */
#define DL_24CH_BASE__ADDR_MSB_SFT                            0
#define DL_24CH_BASE__ADDR_MSB_MASK                           0x1ff
#define DL_24CH_BASE__ADDR_MSB_MASK_SFT                       (0x1ff << 0)

/* AFE_DL_24CH_BASE */
#define DL_24CH_BASE_ADDR_SFT                                 4
#define DL_24CH_BASE_ADDR_MASK                                0xfffffff
#define DL_24CH_BASE_ADDR_MASK_SFT                            (0xfffffff << 4)

/* AFE_DL_24CH_CUR_MSB */
#define DL_24CH_CUR_PTR_MSB_SFT                               0
#define DL_24CH_CUR_PTR_MSB_MASK                              0x1ff
#define DL_24CH_CUR_PTR_MSB_MASK_SFT                          (0x1ff << 0)

/* AFE_DL_24CH_CUR */
#define DL_24CH_CUR_PTR_SFT                                   0
#define DL_24CH_CUR_PTR_MASK                                  0xffffffff
#define DL_24CH_CUR_PTR_MASK_SFT                              (0xffffffff << 0)

/* AFE_DL_24CH_END_MSB */
#define DL_24CH_END_ADDR_MSB_SFT                              0
#define DL_24CH_END_ADDR_MSB_MASK                             0x1ff
#define DL_24CH_END_ADDR_MSB_MASK_SFT                         (0x1ff << 0)

/* AFE_DL_24CH_END */
#define DL_24CH_END_ADDR_SFT                                  4
#define DL_24CH_END_ADDR_MASK                                 0xfffffff
#define DL_24CH_END_ADDR_MASK_SFT                             (0xfffffff << 4)

/* AFE_DL_24CH_CON0 */
#define DL_24CH_ON_SFT                                        31
#define DL_24CH_ON_MASK                                       0x1
#define DL_24CH_ON_MASK_SFT                                   (0x1 << 31)
#define DL_24CH_NUM_SFT                                       24
#define DL_24CH_NUM_MASK                                      0x3f
#define DL_24CH_NUM_MASK_SFT                                  (0x3f << 24)
#define DL_24CH_ONE_HEART_SEL_SFT                             22
#define DL_24CH_ONE_HEART_SEL_MASK                            0x3
#define DL_24CH_ONE_HEART_SEL_MASK_SFT                        (0x3 << 22)
#define DL_24CH_MINLEN_SFT                                    20
#define DL_24CH_MINLEN_MASK                                   0x3
#define DL_24CH_MINLEN_MASK_SFT                               (0x3 << 20)
#define DL_24CH_MAXLEN_SFT                                    16
#define DL_24CH_MAXLEN_MASK                                   0x3
#define DL_24CH_MAXLEN_MASK_SFT                               (0x3 << 16)
#define DL_24CH_SEL_DOMAIN_SFT                                13
#define DL_24CH_SEL_DOMAIN_MASK                               0x7
#define DL_24CH_SEL_DOMAIN_MASK_SFT                           (0x7 << 13)
#define DL_24CH_SEL_FS_SFT                                    8
#define DL_24CH_SEL_FS_MASK                                   0x1f
#define DL_24CH_SEL_FS_MASK_SFT                               (0x1f << 8)
#define DL_24CH_BUF_EMPTY_CLR_SFT                             7
#define DL_24CH_BUF_EMPTY_CLR_MASK                            0x1
#define DL_24CH_BUF_EMPTY_CLR_MASK_SFT                        (0x1 << 7)
#define DL_24CH_PBUF_SIZE_SFT                                 5
#define DL_24CH_PBUF_SIZE_MASK                                0x3
#define DL_24CH_PBUF_SIZE_MASK_SFT                            (0x3 << 5)
#define DL_24CH_HANG_CLR_SFT                                  4
#define DL_24CH_HANG_CLR_MASK                                 0x1
#define DL_24CH_HANG_CLR_MASK_SFT                             (0x1 << 4)
#define DL_24CH_NORMAL_MODE_SFT                               3
#define DL_24CH_NORMAL_MODE_MASK                              0x1
#define DL_24CH_NORMAL_MODE_MASK_SFT                          (0x1 << 3)
#define DL_24CH_HALIGN_SFT                                    2
#define DL_24CH_HALIGN_MASK                                   0x1
#define DL_24CH_HALIGN_MASK_SFT                               (0x1 << 2)
#define DL_24CH_HD_MODE_SFT                                   0
#define DL_24CH_HD_MODE_MASK                                  0x3
#define DL_24CH_HD_MODE_MASK_SFT                              (0x3 << 0)

/* AFE_DL23_BASE_MSB */
#define DL23_BASE__ADDR_MSB_SFT                               0
#define DL23_BASE__ADDR_MSB_MASK                              0x1ff
#define DL23_BASE__ADDR_MSB_MASK_SFT                          (0x1ff << 0)

/* AFE_DL23_BASE */
#define DL23_BASE_ADDR_SFT                                    4
#define DL23_BASE_ADDR_MASK                                   0xfffffff
#define DL23_BASE_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_DL23_CUR_MSB */
#define DL23_CUR_PTR_MSB_SFT                                  0
#define DL23_CUR_PTR_MSB_MASK                                 0x1ff
#define DL23_CUR_PTR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_DL23_CUR */
#define DL23_CUR_PTR_SFT                                      0
#define DL23_CUR_PTR_MASK                                     0xffffffff
#define DL23_CUR_PTR_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL23_END_MSB */
#define DL23_END_ADDR_MSB_SFT                                 0
#define DL23_END_ADDR_MSB_MASK                                0x1ff
#define DL23_END_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_DL23_END */
#define DL23_END_ADDR_SFT                                     4
#define DL23_END_ADDR_MASK                                    0xfffffff
#define DL23_END_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_DL23_RCH_MON */
#define DL23_RCH_DATA_SFT                                     0
#define DL23_RCH_DATA_MASK                                    0xffffffff
#define DL23_RCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_DL23_LCH_MON */
#define DL23_LCH_DATA_SFT                                     0
#define DL23_LCH_DATA_MASK                                    0xffffffff
#define DL23_LCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_DL23_CON0 */
#define DL23_ON_SFT                                           28
#define DL23_ON_MASK                                          0x1
#define DL23_ON_MASK_SFT                                      (0x1 << 28)
#define DL23_ONE_HEART_SEL_SFT                                22
#define DL23_ONE_HEART_SEL_MASK                               0x3
#define DL23_ONE_HEART_SEL_MASK_SFT                           (0x3 << 22)
#define DL23_MINLEN_SFT                                       20
#define DL23_MINLEN_MASK                                      0x3
#define DL23_MINLEN_MASK_SFT                                  (0x3 << 20)
#define DL23_MAXLEN_SFT                                       16
#define DL23_MAXLEN_MASK                                      0x3
#define DL23_MAXLEN_MASK_SFT                                  (0x3 << 16)
#define DL23_SEL_DOMAIN_SFT                                   13
#define DL23_SEL_DOMAIN_MASK                                  0x7
#define DL23_SEL_DOMAIN_MASK_SFT                              (0x7 << 13)
#define DL23_SEL_FS_SFT                                       8
#define DL23_SEL_FS_MASK                                      0x1f
#define DL23_SEL_FS_MASK_SFT                                  (0x1f << 8)
#define DL23_SW_CLEAR_BUF_EMPTY_SFT                           7
#define DL23_SW_CLEAR_BUF_EMPTY_MASK                          0x1
#define DL23_SW_CLEAR_BUF_EMPTY_MASK_SFT                      (0x1 << 7)
#define DL23_PBUF_SIZE_SFT                                    5
#define DL23_PBUF_SIZE_MASK                                   0x3
#define DL23_PBUF_SIZE_MASK_SFT                               (0x3 << 5)
#define DL23_MONO_SFT                                         4
#define DL23_MONO_MASK                                        0x1
#define DL23_MONO_MASK_SFT                                    (0x1 << 4)
#define DL23_NORMAL_MODE_SFT                                  3
#define DL23_NORMAL_MODE_MASK                                 0x1
#define DL23_NORMAL_MODE_MASK_SFT                             (0x1 << 3)
#define DL23_HALIGN_SFT                                       2
#define DL23_HALIGN_MASK                                      0x1
#define DL23_HALIGN_MASK_SFT                                  (0x1 << 2)
#define DL23_HD_MODE_SFT                                      0
#define DL23_HD_MODE_MASK                                     0x3
#define DL23_HD_MODE_MASK_SFT                                 (0x3 << 0)

/* AFE_DL24_BASE_MSB */
#define DL24_BASE__ADDR_MSB_SFT                               0
#define DL24_BASE__ADDR_MSB_MASK                              0x1ff
#define DL24_BASE__ADDR_MSB_MASK_SFT                          (0x1ff << 0)

/* AFE_DL24_BASE */
#define DL24_BASE_ADDR_SFT                                    4
#define DL24_BASE_ADDR_MASK                                   0xfffffff
#define DL24_BASE_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_DL24_CUR_MSB */
#define DL24_CUR_PTR_MSB_SFT                                  0
#define DL24_CUR_PTR_MSB_MASK                                 0x1ff
#define DL24_CUR_PTR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_DL24_CUR */
#define DL24_CUR_PTR_SFT                                      0
#define DL24_CUR_PTR_MASK                                     0xffffffff
#define DL24_CUR_PTR_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL24_END_MSB */
#define DL24_END_ADDR_MSB_SFT                                 0
#define DL24_END_ADDR_MSB_MASK                                0x1ff
#define DL24_END_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_DL24_END */
#define DL24_END_ADDR_SFT                                     4
#define DL24_END_ADDR_MASK                                    0xfffffff
#define DL24_END_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_DL24_RCH_MON */
#define DL24_RCH_DATA_SFT                                     0
#define DL24_RCH_DATA_MASK                                    0xffffffff
#define DL24_RCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_DL24_LCH_MON */
#define DL24_LCH_DATA_SFT                                     0
#define DL24_LCH_DATA_MASK                                    0xffffffff
#define DL24_LCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_DL24_CON0 */
#define DL24_ON_SFT                                           28
#define DL24_ON_MASK                                          0x1
#define DL24_ON_MASK_SFT                                      (0x1 << 28)
#define DL24_ONE_HEART_SEL_SFT                                22
#define DL24_ONE_HEART_SEL_MASK                               0x3
#define DL24_ONE_HEART_SEL_MASK_SFT                           (0x3 << 22)
#define DL24_MINLEN_SFT                                       20
#define DL24_MINLEN_MASK                                      0x3
#define DL24_MINLEN_MASK_SFT                                  (0x3 << 20)
#define DL24_MAXLEN_SFT                                       16
#define DL24_MAXLEN_MASK                                      0x3
#define DL24_MAXLEN_MASK_SFT                                  (0x3 << 16)
#define DL24_SEL_DOMAIN_SFT                                   13
#define DL24_SEL_DOMAIN_MASK                                  0x7
#define DL24_SEL_DOMAIN_MASK_SFT                              (0x7 << 13)
#define DL24_SEL_FS_SFT                                       8
#define DL24_SEL_FS_MASK                                      0x1f
#define DL24_SEL_FS_MASK_SFT                                  (0x1f << 8)
#define DL24_SW_CLEAR_BUF_EMPTY_SFT                           7
#define DL24_SW_CLEAR_BUF_EMPTY_MASK                          0x1
#define DL24_SW_CLEAR_BUF_EMPTY_MASK_SFT                      (0x1 << 7)
#define DL24_PBUF_SIZE_SFT                                    5
#define DL24_PBUF_SIZE_MASK                                   0x3
#define DL24_PBUF_SIZE_MASK_SFT                               (0x3 << 5)
#define DL24_MONO_SFT                                         4
#define DL24_MONO_MASK                                        0x1
#define DL24_MONO_MASK_SFT                                    (0x1 << 4)
#define DL24_NORMAL_MODE_SFT                                  3
#define DL24_NORMAL_MODE_MASK                                 0x1
#define DL24_NORMAL_MODE_MASK_SFT                             (0x1 << 3)
#define DL24_HALIGN_SFT                                       2
#define DL24_HALIGN_MASK                                      0x1
#define DL24_HALIGN_MASK_SFT                                  (0x1 << 2)
#define DL24_HD_MODE_SFT                                      0
#define DL24_HD_MODE_MASK                                     0x3
#define DL24_HD_MODE_MASK_SFT                                 (0x3 << 0)

/* AFE_DL25_BASE_MSB */
#define DL25_BASE__ADDR_MSB_SFT                               0
#define DL25_BASE__ADDR_MSB_MASK                              0x1ff
#define DL25_BASE__ADDR_MSB_MASK_SFT                          (0x1ff << 0)

/* AFE_DL25_BASE */
#define DL25_BASE_ADDR_SFT                                    4
#define DL25_BASE_ADDR_MASK                                   0xfffffff
#define DL25_BASE_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_DL25_CUR_MSB */
#define DL25_CUR_PTR_MSB_SFT                                  0
#define DL25_CUR_PTR_MSB_MASK                                 0x1ff
#define DL25_CUR_PTR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_DL25_CUR */
#define DL25_CUR_PTR_SFT                                      0
#define DL25_CUR_PTR_MASK                                     0xffffffff
#define DL25_CUR_PTR_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL25_END_MSB */
#define DL25_END_ADDR_MSB_SFT                                 0
#define DL25_END_ADDR_MSB_MASK                                0x1ff
#define DL25_END_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_DL25_END */
#define DL25_END_ADDR_SFT                                     4
#define DL25_END_ADDR_MASK                                    0xfffffff
#define DL25_END_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_DL25_RCH_MON */
#define DL25_RCH_DATA_SFT                                     0
#define DL25_RCH_DATA_MASK                                    0xffffffff
#define DL25_RCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_DL25_LCH_MON */
#define DL25_LCH_DATA_SFT                                     0
#define DL25_LCH_DATA_MASK                                    0xffffffff
#define DL25_LCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_DL25_CON0 */
#define DL25_ON_SFT                                           28
#define DL25_ON_MASK                                          0x1
#define DL25_ON_MASK_SFT                                      (0x1 << 28)
#define DL25_ONE_HEART_SEL_SFT                                22
#define DL25_ONE_HEART_SEL_MASK                               0x3
#define DL25_ONE_HEART_SEL_MASK_SFT                           (0x3 << 22)
#define DL25_MINLEN_SFT                                       20
#define DL25_MINLEN_MASK                                      0x3
#define DL25_MINLEN_MASK_SFT                                  (0x3 << 20)
#define DL25_MAXLEN_SFT                                       16
#define DL25_MAXLEN_MASK                                      0x3
#define DL25_MAXLEN_MASK_SFT                                  (0x3 << 16)
#define DL25_SEL_DOMAIN_SFT                                   13
#define DL25_SEL_DOMAIN_MASK                                  0x7
#define DL25_SEL_DOMAIN_MASK_SFT                              (0x7 << 13)
#define DL25_SEL_FS_SFT                                       8
#define DL25_SEL_FS_MASK                                      0x1f
#define DL25_SEL_FS_MASK_SFT                                  (0x1f << 8)
#define DL25_SW_CLEAR_BUF_EMPTY_SFT                           7
#define DL25_SW_CLEAR_BUF_EMPTY_MASK                          0x1
#define DL25_SW_CLEAR_BUF_EMPTY_MASK_SFT                      (0x1 << 7)
#define DL25_PBUF_SIZE_SFT                                    5
#define DL25_PBUF_SIZE_MASK                                   0x3
#define DL25_PBUF_SIZE_MASK_SFT                               (0x3 << 5)
#define DL25_MONO_SFT                                         4
#define DL25_MONO_MASK                                        0x1
#define DL25_MONO_MASK_SFT                                    (0x1 << 4)
#define DL25_NORMAL_MODE_SFT                                  3
#define DL25_NORMAL_MODE_MASK                                 0x1
#define DL25_NORMAL_MODE_MASK_SFT                             (0x1 << 3)
#define DL25_HALIGN_SFT                                       2
#define DL25_HALIGN_MASK                                      0x1
#define DL25_HALIGN_MASK_SFT                                  (0x1 << 2)
#define DL25_HD_MODE_SFT                                      0
#define DL25_HD_MODE_MASK                                     0x3
#define DL25_HD_MODE_MASK_SFT                                 (0x3 << 0)

/* AFE_DL26_BASE_MSB */
#define DL26_BASE__ADDR_MSB_SFT                               0
#define DL26_BASE__ADDR_MSB_MASK                              0x1ff
#define DL26_BASE__ADDR_MSB_MASK_SFT                          (0x1ff << 0)

/* AFE_DL26_BASE */
#define DL26_BASE_ADDR_SFT                                    4
#define DL26_BASE_ADDR_MASK                                   0xfffffff
#define DL26_BASE_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_DL26_CUR_MSB */
#define DL26_CUR_PTR_MSB_SFT                                  0
#define DL26_CUR_PTR_MSB_MASK                                 0x1ff
#define DL26_CUR_PTR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_DL26_CUR */
#define DL26_CUR_PTR_SFT                                      0
#define DL26_CUR_PTR_MASK                                     0xffffffff
#define DL26_CUR_PTR_MASK_SFT                                 (0xffffffff << 0)

/* AFE_DL26_END_MSB */
#define DL26_END_ADDR_MSB_SFT                                 0
#define DL26_END_ADDR_MSB_MASK                                0x1ff
#define DL26_END_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_DL26_END */
#define DL26_END_ADDR_SFT                                     4
#define DL26_END_ADDR_MASK                                    0xfffffff
#define DL26_END_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_DL26_RCH_MON */
#define DL26_RCH_DATA_SFT                                     0
#define DL26_RCH_DATA_MASK                                    0xffffffff
#define DL26_RCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_DL26_LCH_MON */
#define DL26_LCH_DATA_SFT                                     0
#define DL26_LCH_DATA_MASK                                    0xffffffff
#define DL26_LCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_DL26_CON0 */
#define DL26_ON_SFT                                           28
#define DL26_ON_MASK                                          0x1
#define DL26_ON_MASK_SFT                                      (0x1 << 28)
#define DL26_ONE_HEART_SEL_SFT                                22
#define DL26_ONE_HEART_SEL_MASK                               0x3
#define DL26_ONE_HEART_SEL_MASK_SFT                           (0x3 << 22)
#define DL26_MINLEN_SFT                                       20
#define DL26_MINLEN_MASK                                      0x3
#define DL26_MINLEN_MASK_SFT                                  (0x3 << 20)
#define DL26_MAXLEN_SFT                                       16
#define DL26_MAXLEN_MASK                                      0x3
#define DL26_MAXLEN_MASK_SFT                                  (0x3 << 16)
#define DL26_SEL_DOMAIN_SFT                                   13
#define DL26_SEL_DOMAIN_MASK                                  0x7
#define DL26_SEL_DOMAIN_MASK_SFT                              (0x7 << 13)
#define DL26_SEL_FS_SFT                                       8
#define DL26_SEL_FS_MASK                                      0x1f
#define DL26_SEL_FS_MASK_SFT                                  (0x1f << 8)
#define DL26_SW_CLEAR_BUF_EMPTY_SFT                           7
#define DL26_SW_CLEAR_BUF_EMPTY_MASK                          0x1
#define DL26_SW_CLEAR_BUF_EMPTY_MASK_SFT                      (0x1 << 7)
#define DL26_PBUF_SIZE_SFT                                    5
#define DL26_PBUF_SIZE_MASK                                   0x3
#define DL26_PBUF_SIZE_MASK_SFT                               (0x3 << 5)
#define DL26_MONO_SFT                                         4
#define DL26_MONO_MASK                                        0x1
#define DL26_MONO_MASK_SFT                                    (0x1 << 4)
#define DL26_NORMAL_MODE_SFT                                  3
#define DL26_NORMAL_MODE_MASK                                 0x1
#define DL26_NORMAL_MODE_MASK_SFT                             (0x1 << 3)
#define DL26_HALIGN_SFT                                       2
#define DL26_HALIGN_MASK                                      0x1
#define DL26_HALIGN_MASK_SFT                                  (0x1 << 2)
#define DL26_HD_MODE_SFT                                      0
#define DL26_HD_MODE_MASK                                     0x3
#define DL26_HD_MODE_MASK_SFT                                 (0x3 << 0)

/* AFE_VUL0_BASE_MSB */
#define VUL0_BASE_ADDR_MSB_SFT                                0
#define VUL0_BASE_ADDR_MSB_MASK                               0x1ff
#define VUL0_BASE_ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_VUL0_BASE */
#define VUL0_BASE_ADDR_SFT                                    4
#define VUL0_BASE_ADDR_MASK                                   0xfffffff
#define VUL0_BASE_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_VUL0_CUR_MSB */
#define VUL0_CUR_PTR_MSB_SFT                                  0
#define VUL0_CUR_PTR_MSB_MASK                                 0x1ff
#define VUL0_CUR_PTR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_VUL0_CUR */
#define VUL0_CUR_PTR_SFT                                      0
#define VUL0_CUR_PTR_MASK                                     0xffffffff
#define VUL0_CUR_PTR_MASK_SFT                                 (0xffffffff << 0)

/* AFE_VUL0_END_MSB */
#define VUL0_END_ADDR_MSB_SFT                                 0
#define VUL0_END_ADDR_MSB_MASK                                0x1ff
#define VUL0_END_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_VUL0_END */
#define VUL0_END_ADDR_SFT                                     4
#define VUL0_END_ADDR_MASK                                    0xfffffff
#define VUL0_END_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_VUL0_RCH_MON */
#define VUL0_RCH_DATA_SFT                                     0
#define VUL0_RCH_DATA_MASK                                    0xffffffff
#define VUL0_RCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL0_LCH_MON */
#define VUL0_LCH_DATA_SFT                                     0
#define VUL0_LCH_DATA_MASK                                    0xffffffff
#define VUL0_LCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL0_CON0 */
#define VUL0_ON_SFT                                           28
#define VUL0_ON_MASK                                          0x1
#define VUL0_ON_MASK_SFT                                      (0x1 << 28)
#define VUL0_MINLEN_SFT                                       20
#define VUL0_MINLEN_MASK                                      0x3
#define VUL0_MINLEN_MASK_SFT                                  (0x3 << 20)
#define VUL0_MAXLEN_SFT                                       16
#define VUL0_MAXLEN_MASK                                      0x3
#define VUL0_MAXLEN_MASK_SFT                                  (0x3 << 16)
#define VUL0_SEL_DOMAIN_SFT                                   13
#define VUL0_SEL_DOMAIN_MASK                                  0x7
#define VUL0_SEL_DOMAIN_MASK_SFT                              (0x7 << 13)
#define VUL0_SEL_FS_SFT                                       8
#define VUL0_SEL_FS_MASK                                      0x1f
#define VUL0_SEL_FS_MASK_SFT                                  (0x1f << 8)
#define VUL0_SW_CLEAR_BUF_FULL_SFT                            7
#define VUL0_SW_CLEAR_BUF_FULL_MASK                           0x1
#define VUL0_SW_CLEAR_BUF_FULL_MASK_SFT                       (0x1 << 7)
#define VUL0_WR_SIGN_SFT                                      6
#define VUL0_WR_SIGN_MASK                                     0x1
#define VUL0_WR_SIGN_MASK_SFT                                 (0x1 << 6)
#define VUL0_R_MONO_SFT                                       5
#define VUL0_R_MONO_MASK                                      0x1
#define VUL0_R_MONO_MASK_SFT                                  (0x1 << 5)
#define VUL0_MONO_SFT                                         4
#define VUL0_MONO_MASK                                        0x1
#define VUL0_MONO_MASK_SFT                                    (0x1 << 4)
#define VUL0_NORMAL_MODE_SFT                                  3
#define VUL0_NORMAL_MODE_MASK                                 0x1
#define VUL0_NORMAL_MODE_MASK_SFT                             (0x1 << 3)
#define VUL0_HALIGN_SFT                                       2
#define VUL0_HALIGN_MASK                                      0x1
#define VUL0_HALIGN_MASK_SFT                                  (0x1 << 2)
#define VUL0_HD_MODE_SFT                                      0
#define VUL0_HD_MODE_MASK                                     0x3
#define VUL0_HD_MODE_MASK_SFT                                 (0x3 << 0)

/* AFE_VUL1_BASE_MSB */
#define VUL1_BASE_ADDR_MSB_SFT                                0
#define VUL1_BASE_ADDR_MSB_MASK                               0x1ff
#define VUL1_BASE_ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_VUL1_BASE */
#define VUL1_BASE_ADDR_SFT                                    4
#define VUL1_BASE_ADDR_MASK                                   0xfffffff
#define VUL1_BASE_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_VUL1_CUR_MSB */
#define VUL1_CUR_PTR_MSB_SFT                                  0
#define VUL1_CUR_PTR_MSB_MASK                                 0x1ff
#define VUL1_CUR_PTR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_VUL1_CUR */
#define VUL1_CUR_PTR_SFT                                      0
#define VUL1_CUR_PTR_MASK                                     0xffffffff
#define VUL1_CUR_PTR_MASK_SFT                                 (0xffffffff << 0)

/* AFE_VUL1_END_MSB */
#define VUL1_END_ADDR_MSB_SFT                                 0
#define VUL1_END_ADDR_MSB_MASK                                0x1ff
#define VUL1_END_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_VUL1_END */
#define VUL1_END_ADDR_SFT                                     4
#define VUL1_END_ADDR_MASK                                    0xfffffff
#define VUL1_END_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_VUL1_RCH_MON */
#define VUL1_RCH_DATA_SFT                                     0
#define VUL1_RCH_DATA_MASK                                    0xffffffff
#define VUL1_RCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL1_LCH_MON */
#define VUL1_LCH_DATA_SFT                                     0
#define VUL1_LCH_DATA_MASK                                    0xffffffff
#define VUL1_LCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL1_CON0 */
#define VUL1_ON_SFT                                           28
#define VUL1_ON_MASK                                          0x1
#define VUL1_ON_MASK_SFT                                      (0x1 << 28)
#define VUL1_MINLEN_SFT                                       20
#define VUL1_MINLEN_MASK                                      0x3
#define VUL1_MINLEN_MASK_SFT                                  (0x3 << 20)
#define VUL1_MAXLEN_SFT                                       16
#define VUL1_MAXLEN_MASK                                      0x3
#define VUL1_MAXLEN_MASK_SFT                                  (0x3 << 16)
#define VUL1_SEL_DOMAIN_SFT                                   13
#define VUL1_SEL_DOMAIN_MASK                                  0x7
#define VUL1_SEL_DOMAIN_MASK_SFT                              (0x7 << 13)
#define VUL1_SEL_FS_SFT                                       8
#define VUL1_SEL_FS_MASK                                      0x1f
#define VUL1_SEL_FS_MASK_SFT                                  (0x1f << 8)
#define VUL1_SW_CLEAR_BUF_FULL_SFT                            7
#define VUL1_SW_CLEAR_BUF_FULL_MASK                           0x1
#define VUL1_SW_CLEAR_BUF_FULL_MASK_SFT                       (0x1 << 7)
#define VUL1_WR_SIGN_SFT                                      6
#define VUL1_WR_SIGN_MASK                                     0x1
#define VUL1_WR_SIGN_MASK_SFT                                 (0x1 << 6)
#define VUL1_R_MONO_SFT                                       5
#define VUL1_R_MONO_MASK                                      0x1
#define VUL1_R_MONO_MASK_SFT                                  (0x1 << 5)
#define VUL1_MONO_SFT                                         4
#define VUL1_MONO_MASK                                        0x1
#define VUL1_MONO_MASK_SFT                                    (0x1 << 4)
#define VUL1_NORMAL_MODE_SFT                                  3
#define VUL1_NORMAL_MODE_MASK                                 0x1
#define VUL1_NORMAL_MODE_MASK_SFT                             (0x1 << 3)
#define VUL1_HALIGN_SFT                                       2
#define VUL1_HALIGN_MASK                                      0x1
#define VUL1_HALIGN_MASK_SFT                                  (0x1 << 2)
#define VUL1_HD_MODE_SFT                                      0
#define VUL1_HD_MODE_MASK                                     0x3
#define VUL1_HD_MODE_MASK_SFT                                 (0x3 << 0)

/* AFE_VUL2_BASE_MSB */
#define VUL2_BASE_ADDR_MSB_SFT                                0
#define VUL2_BASE_ADDR_MSB_MASK                               0x1ff
#define VUL2_BASE_ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_VUL2_BASE */
#define VUL2_BASE_ADDR_SFT                                    4
#define VUL2_BASE_ADDR_MASK                                   0xfffffff
#define VUL2_BASE_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_VUL2_CUR_MSB */
#define VUL2_CUR_PTR_MSB_SFT                                  0
#define VUL2_CUR_PTR_MSB_MASK                                 0x1ff
#define VUL2_CUR_PTR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_VUL2_CUR */
#define VUL2_CUR_PTR_SFT                                      0
#define VUL2_CUR_PTR_MASK                                     0xffffffff
#define VUL2_CUR_PTR_MASK_SFT                                 (0xffffffff << 0)

/* AFE_VUL2_END_MSB */
#define VUL2_END_ADDR_MSB_SFT                                 0
#define VUL2_END_ADDR_MSB_MASK                                0x1ff
#define VUL2_END_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_VUL2_END */
#define VUL2_END_ADDR_SFT                                     4
#define VUL2_END_ADDR_MASK                                    0xfffffff
#define VUL2_END_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_VUL2_RCH_MON */
#define VUL2_RCH_DATA_SFT                                     0
#define VUL2_RCH_DATA_MASK                                    0xffffffff
#define VUL2_RCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL2_LCH_MON */
#define VUL2_LCH_DATA_SFT                                     0
#define VUL2_LCH_DATA_MASK                                    0xffffffff
#define VUL2_LCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL2_CON0 */
#define VUL2_ON_SFT                                           28
#define VUL2_ON_MASK                                          0x1
#define VUL2_ON_MASK_SFT                                      (0x1 << 28)
#define VUL2_MINLEN_SFT                                       20
#define VUL2_MINLEN_MASK                                      0x3
#define VUL2_MINLEN_MASK_SFT                                  (0x3 << 20)
#define VUL2_MAXLEN_SFT                                       16
#define VUL2_MAXLEN_MASK                                      0x3
#define VUL2_MAXLEN_MASK_SFT                                  (0x3 << 16)
#define VUL2_SEL_DOMAIN_SFT                                   13
#define VUL2_SEL_DOMAIN_MASK                                  0x7
#define VUL2_SEL_DOMAIN_MASK_SFT                              (0x7 << 13)
#define VUL2_SEL_FS_SFT                                       8
#define VUL2_SEL_FS_MASK                                      0x1f
#define VUL2_SEL_FS_MASK_SFT                                  (0x1f << 8)
#define VUL2_SW_CLEAR_BUF_FULL_SFT                            7
#define VUL2_SW_CLEAR_BUF_FULL_MASK                           0x1
#define VUL2_SW_CLEAR_BUF_FULL_MASK_SFT                       (0x1 << 7)
#define VUL2_WR_SIGN_SFT                                      6
#define VUL2_WR_SIGN_MASK                                     0x1
#define VUL2_WR_SIGN_MASK_SFT                                 (0x1 << 6)
#define VUL2_R_MONO_SFT                                       5
#define VUL2_R_MONO_MASK                                      0x1
#define VUL2_R_MONO_MASK_SFT                                  (0x1 << 5)
#define VUL2_MONO_SFT                                         4
#define VUL2_MONO_MASK                                        0x1
#define VUL2_MONO_MASK_SFT                                    (0x1 << 4)
#define VUL2_NORMAL_MODE_SFT                                  3
#define VUL2_NORMAL_MODE_MASK                                 0x1
#define VUL2_NORMAL_MODE_MASK_SFT                             (0x1 << 3)
#define VUL2_HALIGN_SFT                                       2
#define VUL2_HALIGN_MASK                                      0x1
#define VUL2_HALIGN_MASK_SFT                                  (0x1 << 2)
#define VUL2_HD_MODE_SFT                                      0
#define VUL2_HD_MODE_MASK                                     0x3
#define VUL2_HD_MODE_MASK_SFT                                 (0x3 << 0)

/* AFE_VUL3_BASE_MSB */
#define VUL3_BASE_ADDR_MSB_SFT                                0
#define VUL3_BASE_ADDR_MSB_MASK                               0x1ff
#define VUL3_BASE_ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_VUL3_BASE */
#define VUL3_BASE_ADDR_SFT                                    4
#define VUL3_BASE_ADDR_MASK                                   0xfffffff
#define VUL3_BASE_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_VUL3_CUR_MSB */
#define VUL3_CUR_PTR_MSB_SFT                                  0
#define VUL3_CUR_PTR_MSB_MASK                                 0x1ff
#define VUL3_CUR_PTR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_VUL3_CUR */
#define VUL3_CUR_PTR_SFT                                      0
#define VUL3_CUR_PTR_MASK                                     0xffffffff
#define VUL3_CUR_PTR_MASK_SFT                                 (0xffffffff << 0)

/* AFE_VUL3_END_MSB */
#define VUL3_END_ADDR_MSB_SFT                                 0
#define VUL3_END_ADDR_MSB_MASK                                0x1ff
#define VUL3_END_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_VUL3_END */
#define VUL3_END_ADDR_SFT                                     4
#define VUL3_END_ADDR_MASK                                    0xfffffff
#define VUL3_END_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_VUL3_RCH_MON */
#define VUL3_RCH_DATA_SFT                                     0
#define VUL3_RCH_DATA_MASK                                    0xffffffff
#define VUL3_RCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL3_LCH_MON */
#define VUL3_LCH_DATA_SFT                                     0
#define VUL3_LCH_DATA_MASK                                    0xffffffff
#define VUL3_LCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL3_CON0 */
#define VUL3_ON_SFT                                           28
#define VUL3_ON_MASK                                          0x1
#define VUL3_ON_MASK_SFT                                      (0x1 << 28)
#define VUL3_MINLEN_SFT                                       20
#define VUL3_MINLEN_MASK                                      0x3
#define VUL3_MINLEN_MASK_SFT                                  (0x3 << 20)
#define VUL3_MAXLEN_SFT                                       16
#define VUL3_MAXLEN_MASK                                      0x3
#define VUL3_MAXLEN_MASK_SFT                                  (0x3 << 16)
#define VUL3_SEL_DOMAIN_SFT                                   13
#define VUL3_SEL_DOMAIN_MASK                                  0x7
#define VUL3_SEL_DOMAIN_MASK_SFT                              (0x7 << 13)
#define VUL3_SEL_FS_SFT                                       8
#define VUL3_SEL_FS_MASK                                      0x1f
#define VUL3_SEL_FS_MASK_SFT                                  (0x1f << 8)
#define VUL3_SW_CLEAR_BUF_FULL_SFT                            7
#define VUL3_SW_CLEAR_BUF_FULL_MASK                           0x1
#define VUL3_SW_CLEAR_BUF_FULL_MASK_SFT                       (0x1 << 7)
#define VUL3_WR_SIGN_SFT                                      6
#define VUL3_WR_SIGN_MASK                                     0x1
#define VUL3_WR_SIGN_MASK_SFT                                 (0x1 << 6)
#define VUL3_R_MONO_SFT                                       5
#define VUL3_R_MONO_MASK                                      0x1
#define VUL3_R_MONO_MASK_SFT                                  (0x1 << 5)
#define VUL3_MONO_SFT                                         4
#define VUL3_MONO_MASK                                        0x1
#define VUL3_MONO_MASK_SFT                                    (0x1 << 4)
#define VUL3_NORMAL_MODE_SFT                                  3
#define VUL3_NORMAL_MODE_MASK                                 0x1
#define VUL3_NORMAL_MODE_MASK_SFT                             (0x1 << 3)
#define VUL3_HALIGN_SFT                                       2
#define VUL3_HALIGN_MASK                                      0x1
#define VUL3_HALIGN_MASK_SFT                                  (0x1 << 2)
#define VUL3_HD_MODE_SFT                                      0
#define VUL3_HD_MODE_MASK                                     0x3
#define VUL3_HD_MODE_MASK_SFT                                 (0x3 << 0)

/* AFE_VUL4_BASE_MSB */
#define VUL4_BASE_ADDR_MSB_SFT                                0
#define VUL4_BASE_ADDR_MSB_MASK                               0x1ff
#define VUL4_BASE_ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_VUL4_BASE */
#define VUL4_BASE_ADDR_SFT                                    4
#define VUL4_BASE_ADDR_MASK                                   0xfffffff
#define VUL4_BASE_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_VUL4_CUR_MSB */
#define VUL4_CUR_PTR_MSB_SFT                                  0
#define VUL4_CUR_PTR_MSB_MASK                                 0x1ff
#define VUL4_CUR_PTR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_VUL4_CUR */
#define VUL4_CUR_PTR_SFT                                      0
#define VUL4_CUR_PTR_MASK                                     0xffffffff
#define VUL4_CUR_PTR_MASK_SFT                                 (0xffffffff << 0)

/* AFE_VUL4_END_MSB */
#define VUL4_END_ADDR_MSB_SFT                                 0
#define VUL4_END_ADDR_MSB_MASK                                0x1ff
#define VUL4_END_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_VUL4_END */
#define VUL4_END_ADDR_SFT                                     4
#define VUL4_END_ADDR_MASK                                    0xfffffff
#define VUL4_END_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_VUL4_RCH_MON */
#define VUL4_RCH_DATA_SFT                                     0
#define VUL4_RCH_DATA_MASK                                    0xffffffff
#define VUL4_RCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL4_LCH_MON */
#define VUL4_LCH_DATA_SFT                                     0
#define VUL4_LCH_DATA_MASK                                    0xffffffff
#define VUL4_LCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL4_CON0 */
#define VUL4_ON_SFT                                           28
#define VUL4_ON_MASK                                          0x1
#define VUL4_ON_MASK_SFT                                      (0x1 << 28)
#define VUL4_MINLEN_SFT                                       20
#define VUL4_MINLEN_MASK                                      0x3
#define VUL4_MINLEN_MASK_SFT                                  (0x3 << 20)
#define VUL4_MAXLEN_SFT                                       16
#define VUL4_MAXLEN_MASK                                      0x3
#define VUL4_MAXLEN_MASK_SFT                                  (0x3 << 16)
#define VUL4_SEL_DOMAIN_SFT                                   13
#define VUL4_SEL_DOMAIN_MASK                                  0x7
#define VUL4_SEL_DOMAIN_MASK_SFT                              (0x7 << 13)
#define VUL4_SEL_FS_SFT                                       8
#define VUL4_SEL_FS_MASK                                      0x1f
#define VUL4_SEL_FS_MASK_SFT                                  (0x1f << 8)
#define VUL4_SW_CLEAR_BUF_FULL_SFT                            7
#define VUL4_SW_CLEAR_BUF_FULL_MASK                           0x1
#define VUL4_SW_CLEAR_BUF_FULL_MASK_SFT                       (0x1 << 7)
#define VUL4_WR_SIGN_SFT                                      6
#define VUL4_WR_SIGN_MASK                                     0x1
#define VUL4_WR_SIGN_MASK_SFT                                 (0x1 << 6)
#define VUL4_R_MONO_SFT                                       5
#define VUL4_R_MONO_MASK                                      0x1
#define VUL4_R_MONO_MASK_SFT                                  (0x1 << 5)
#define VUL4_MONO_SFT                                         4
#define VUL4_MONO_MASK                                        0x1
#define VUL4_MONO_MASK_SFT                                    (0x1 << 4)
#define VUL4_NORMAL_MODE_SFT                                  3
#define VUL4_NORMAL_MODE_MASK                                 0x1
#define VUL4_NORMAL_MODE_MASK_SFT                             (0x1 << 3)
#define VUL4_HALIGN_SFT                                       2
#define VUL4_HALIGN_MASK                                      0x1
#define VUL4_HALIGN_MASK_SFT                                  (0x1 << 2)
#define VUL4_HD_MODE_SFT                                      0
#define VUL4_HD_MODE_MASK                                     0x3
#define VUL4_HD_MODE_MASK_SFT                                 (0x3 << 0)

/* AFE_VUL5_BASE_MSB */
#define VUL5_BASE_ADDR_MSB_SFT                                0
#define VUL5_BASE_ADDR_MSB_MASK                               0x1ff
#define VUL5_BASE_ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_VUL5_BASE */
#define VUL5_BASE_ADDR_SFT                                    4
#define VUL5_BASE_ADDR_MASK                                   0xfffffff
#define VUL5_BASE_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_VUL5_CUR_MSB */
#define VUL5_CUR_PTR_MSB_SFT                                  0
#define VUL5_CUR_PTR_MSB_MASK                                 0x1ff
#define VUL5_CUR_PTR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_VUL5_CUR */
#define VUL5_CUR_PTR_SFT                                      0
#define VUL5_CUR_PTR_MASK                                     0xffffffff
#define VUL5_CUR_PTR_MASK_SFT                                 (0xffffffff << 0)

/* AFE_VUL5_END_MSB */
#define VUL5_END_ADDR_MSB_SFT                                 0
#define VUL5_END_ADDR_MSB_MASK                                0x1ff
#define VUL5_END_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_VUL5_END */
#define VUL5_END_ADDR_SFT                                     4
#define VUL5_END_ADDR_MASK                                    0xfffffff
#define VUL5_END_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_VUL5_RCH_MON */
#define VUL5_RCH_DATA_SFT                                     0
#define VUL5_RCH_DATA_MASK                                    0xffffffff
#define VUL5_RCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL5_LCH_MON */
#define VUL5_LCH_DATA_SFT                                     0
#define VUL5_LCH_DATA_MASK                                    0xffffffff
#define VUL5_LCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL5_CON0 */
#define VUL5_ON_SFT                                           28
#define VUL5_ON_MASK                                          0x1
#define VUL5_ON_MASK_SFT                                      (0x1 << 28)
#define VUL5_MINLEN_SFT                                       20
#define VUL5_MINLEN_MASK                                      0x3
#define VUL5_MINLEN_MASK_SFT                                  (0x3 << 20)
#define VUL5_MAXLEN_SFT                                       16
#define VUL5_MAXLEN_MASK                                      0x3
#define VUL5_MAXLEN_MASK_SFT                                  (0x3 << 16)
#define VUL5_SEL_DOMAIN_SFT                                   13
#define VUL5_SEL_DOMAIN_MASK                                  0x7
#define VUL5_SEL_DOMAIN_MASK_SFT                              (0x7 << 13)
#define VUL5_SEL_FS_SFT                                       8
#define VUL5_SEL_FS_MASK                                      0x1f
#define VUL5_SEL_FS_MASK_SFT                                  (0x1f << 8)
#define VUL5_SW_CLEAR_BUF_FULL_SFT                            7
#define VUL5_SW_CLEAR_BUF_FULL_MASK                           0x1
#define VUL5_SW_CLEAR_BUF_FULL_MASK_SFT                       (0x1 << 7)
#define VUL5_WR_SIGN_SFT                                      6
#define VUL5_WR_SIGN_MASK                                     0x1
#define VUL5_WR_SIGN_MASK_SFT                                 (0x1 << 6)
#define VUL5_R_MONO_SFT                                       5
#define VUL5_R_MONO_MASK                                      0x1
#define VUL5_R_MONO_MASK_SFT                                  (0x1 << 5)
#define VUL5_MONO_SFT                                         4
#define VUL5_MONO_MASK                                        0x1
#define VUL5_MONO_MASK_SFT                                    (0x1 << 4)
#define VUL5_NORMAL_MODE_SFT                                  3
#define VUL5_NORMAL_MODE_MASK                                 0x1
#define VUL5_NORMAL_MODE_MASK_SFT                             (0x1 << 3)
#define VUL5_HALIGN_SFT                                       2
#define VUL5_HALIGN_MASK                                      0x1
#define VUL5_HALIGN_MASK_SFT                                  (0x1 << 2)
#define VUL5_HD_MODE_SFT                                      0
#define VUL5_HD_MODE_MASK                                     0x3
#define VUL5_HD_MODE_MASK_SFT                                 (0x3 << 0)

/* AFE_VUL6_BASE_MSB */
#define VUL6_BASE_ADDR_MSB_SFT                                0
#define VUL6_BASE_ADDR_MSB_MASK                               0x1ff
#define VUL6_BASE_ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_VUL6_BASE */
#define VUL6_BASE_ADDR_SFT                                    4
#define VUL6_BASE_ADDR_MASK                                   0xfffffff
#define VUL6_BASE_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_VUL6_CUR_MSB */
#define VUL6_CUR_PTR_MSB_SFT                                  0
#define VUL6_CUR_PTR_MSB_MASK                                 0x1ff
#define VUL6_CUR_PTR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_VUL6_CUR */
#define VUL6_CUR_PTR_SFT                                      0
#define VUL6_CUR_PTR_MASK                                     0xffffffff
#define VUL6_CUR_PTR_MASK_SFT                                 (0xffffffff << 0)

/* AFE_VUL6_END_MSB */
#define VUL6_END_ADDR_MSB_SFT                                 0
#define VUL6_END_ADDR_MSB_MASK                                0x1ff
#define VUL6_END_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_VUL6_END */
#define VUL6_END_ADDR_SFT                                     4
#define VUL6_END_ADDR_MASK                                    0xfffffff
#define VUL6_END_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_VUL6_RCH_MON */
#define VUL6_RCH_DATA_SFT                                     0
#define VUL6_RCH_DATA_MASK                                    0xffffffff
#define VUL6_RCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL6_LCH_MON */
#define VUL6_LCH_DATA_SFT                                     0
#define VUL6_LCH_DATA_MASK                                    0xffffffff
#define VUL6_LCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL6_CON0 */
#define VUL6_ON_SFT                                           28
#define VUL6_ON_MASK                                          0x1
#define VUL6_ON_MASK_SFT                                      (0x1 << 28)
#define VUL6_MINLEN_SFT                                       20
#define VUL6_MINLEN_MASK                                      0x3
#define VUL6_MINLEN_MASK_SFT                                  (0x3 << 20)
#define VUL6_MAXLEN_SFT                                       16
#define VUL6_MAXLEN_MASK                                      0x3
#define VUL6_MAXLEN_MASK_SFT                                  (0x3 << 16)
#define VUL6_SEL_DOMAIN_SFT                                   13
#define VUL6_SEL_DOMAIN_MASK                                  0x7
#define VUL6_SEL_DOMAIN_MASK_SFT                              (0x7 << 13)
#define VUL6_SEL_FS_SFT                                       8
#define VUL6_SEL_FS_MASK                                      0x1f
#define VUL6_SEL_FS_MASK_SFT                                  (0x1f << 8)
#define VUL6_SW_CLEAR_BUF_FULL_SFT                            7
#define VUL6_SW_CLEAR_BUF_FULL_MASK                           0x1
#define VUL6_SW_CLEAR_BUF_FULL_MASK_SFT                       (0x1 << 7)
#define VUL6_WR_SIGN_SFT                                      6
#define VUL6_WR_SIGN_MASK                                     0x1
#define VUL6_WR_SIGN_MASK_SFT                                 (0x1 << 6)
#define VUL6_R_MONO_SFT                                       5
#define VUL6_R_MONO_MASK                                      0x1
#define VUL6_R_MONO_MASK_SFT                                  (0x1 << 5)
#define VUL6_MONO_SFT                                         4
#define VUL6_MONO_MASK                                        0x1
#define VUL6_MONO_MASK_SFT                                    (0x1 << 4)
#define VUL6_NORMAL_MODE_SFT                                  3
#define VUL6_NORMAL_MODE_MASK                                 0x1
#define VUL6_NORMAL_MODE_MASK_SFT                             (0x1 << 3)
#define VUL6_HALIGN_SFT                                       2
#define VUL6_HALIGN_MASK                                      0x1
#define VUL6_HALIGN_MASK_SFT                                  (0x1 << 2)
#define VUL6_HD_MODE_SFT                                      0
#define VUL6_HD_MODE_MASK                                     0x3
#define VUL6_HD_MODE_MASK_SFT                                 (0x3 << 0)

/* AFE_VUL7_BASE_MSB */
#define VUL7_BASE_ADDR_MSB_SFT                                0
#define VUL7_BASE_ADDR_MSB_MASK                               0x1ff
#define VUL7_BASE_ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_VUL7_BASE */
#define VUL7_BASE_ADDR_SFT                                    4
#define VUL7_BASE_ADDR_MASK                                   0xfffffff
#define VUL7_BASE_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_VUL7_CUR_MSB */
#define VUL7_CUR_PTR_MSB_SFT                                  0
#define VUL7_CUR_PTR_MSB_MASK                                 0x1ff
#define VUL7_CUR_PTR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_VUL7_CUR */
#define VUL7_CUR_PTR_SFT                                      0
#define VUL7_CUR_PTR_MASK                                     0xffffffff
#define VUL7_CUR_PTR_MASK_SFT                                 (0xffffffff << 0)

/* AFE_VUL7_END_MSB */
#define VUL7_END_ADDR_MSB_SFT                                 0
#define VUL7_END_ADDR_MSB_MASK                                0x1ff
#define VUL7_END_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_VUL7_END */
#define VUL7_END_ADDR_SFT                                     4
#define VUL7_END_ADDR_MASK                                    0xfffffff
#define VUL7_END_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_VUL7_RCH_MON */
#define VUL7_RCH_DATA_SFT                                     0
#define VUL7_RCH_DATA_MASK                                    0xffffffff
#define VUL7_RCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL7_LCH_MON */
#define VUL7_LCH_DATA_SFT                                     0
#define VUL7_LCH_DATA_MASK                                    0xffffffff
#define VUL7_LCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL7_CON0 */
#define VUL7_ON_SFT                                           28
#define VUL7_ON_MASK                                          0x1
#define VUL7_ON_MASK_SFT                                      (0x1 << 28)
#define VUL7_MINLEN_SFT                                       20
#define VUL7_MINLEN_MASK                                      0x3
#define VUL7_MINLEN_MASK_SFT                                  (0x3 << 20)
#define VUL7_MAXLEN_SFT                                       16
#define VUL7_MAXLEN_MASK                                      0x3
#define VUL7_MAXLEN_MASK_SFT                                  (0x3 << 16)
#define VUL7_SEL_DOMAIN_SFT                                   13
#define VUL7_SEL_DOMAIN_MASK                                  0x7
#define VUL7_SEL_DOMAIN_MASK_SFT                              (0x7 << 13)
#define VUL7_SEL_FS_SFT                                       8
#define VUL7_SEL_FS_MASK                                      0x1f
#define VUL7_SEL_FS_MASK_SFT                                  (0x1f << 8)
#define VUL7_SW_CLEAR_BUF_FULL_SFT                            7
#define VUL7_SW_CLEAR_BUF_FULL_MASK                           0x1
#define VUL7_SW_CLEAR_BUF_FULL_MASK_SFT                       (0x1 << 7)
#define VUL7_WR_SIGN_SFT                                      6
#define VUL7_WR_SIGN_MASK                                     0x1
#define VUL7_WR_SIGN_MASK_SFT                                 (0x1 << 6)
#define VUL7_R_MONO_SFT                                       5
#define VUL7_R_MONO_MASK                                      0x1
#define VUL7_R_MONO_MASK_SFT                                  (0x1 << 5)
#define VUL7_MONO_SFT                                         4
#define VUL7_MONO_MASK                                        0x1
#define VUL7_MONO_MASK_SFT                                    (0x1 << 4)
#define VUL7_NORMAL_MODE_SFT                                  3
#define VUL7_NORMAL_MODE_MASK                                 0x1
#define VUL7_NORMAL_MODE_MASK_SFT                             (0x1 << 3)
#define VUL7_HALIGN_SFT                                       2
#define VUL7_HALIGN_MASK                                      0x1
#define VUL7_HALIGN_MASK_SFT                                  (0x1 << 2)
#define VUL7_HD_MODE_SFT                                      0
#define VUL7_HD_MODE_MASK                                     0x3
#define VUL7_HD_MODE_MASK_SFT                                 (0x3 << 0)

/* AFE_VUL8_BASE_MSB */
#define VUL8_BASE_ADDR_MSB_SFT                                0
#define VUL8_BASE_ADDR_MSB_MASK                               0x1ff
#define VUL8_BASE_ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_VUL8_BASE */
#define VUL8_BASE_ADDR_SFT                                    4
#define VUL8_BASE_ADDR_MASK                                   0xfffffff
#define VUL8_BASE_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_VUL8_CUR_MSB */
#define VUL8_CUR_PTR_MSB_SFT                                  0
#define VUL8_CUR_PTR_MSB_MASK                                 0x1ff
#define VUL8_CUR_PTR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_VUL8_CUR */
#define VUL8_CUR_PTR_SFT                                      0
#define VUL8_CUR_PTR_MASK                                     0xffffffff
#define VUL8_CUR_PTR_MASK_SFT                                 (0xffffffff << 0)

/* AFE_VUL8_END_MSB */
#define VUL8_END_ADDR_MSB_SFT                                 0
#define VUL8_END_ADDR_MSB_MASK                                0x1ff
#define VUL8_END_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_VUL8_END */
#define VUL8_END_ADDR_SFT                                     4
#define VUL8_END_ADDR_MASK                                    0xfffffff
#define VUL8_END_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_VUL8_RCH_MON */
#define VUL8_RCH_DATA_SFT                                     0
#define VUL8_RCH_DATA_MASK                                    0xffffffff
#define VUL8_RCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL8_LCH_MON */
#define VUL8_LCH_DATA_SFT                                     0
#define VUL8_LCH_DATA_MASK                                    0xffffffff
#define VUL8_LCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL8_CON0 */
#define VUL8_ON_SFT                                           28
#define VUL8_ON_MASK                                          0x1
#define VUL8_ON_MASK_SFT                                      (0x1 << 28)
#define VUL8_MINLEN_SFT                                       20
#define VUL8_MINLEN_MASK                                      0x3
#define VUL8_MINLEN_MASK_SFT                                  (0x3 << 20)
#define VUL8_MAXLEN_SFT                                       16
#define VUL8_MAXLEN_MASK                                      0x3
#define VUL8_MAXLEN_MASK_SFT                                  (0x3 << 16)
#define VUL8_SEL_DOMAIN_SFT                                   13
#define VUL8_SEL_DOMAIN_MASK                                  0x7
#define VUL8_SEL_DOMAIN_MASK_SFT                              (0x7 << 13)
#define VUL8_SEL_FS_SFT                                       8
#define VUL8_SEL_FS_MASK                                      0x1f
#define VUL8_SEL_FS_MASK_SFT                                  (0x1f << 8)
#define VUL8_SW_CLEAR_BUF_FULL_SFT                            7
#define VUL8_SW_CLEAR_BUF_FULL_MASK                           0x1
#define VUL8_SW_CLEAR_BUF_FULL_MASK_SFT                       (0x1 << 7)
#define VUL8_WR_SIGN_SFT                                      6
#define VUL8_WR_SIGN_MASK                                     0x1
#define VUL8_WR_SIGN_MASK_SFT                                 (0x1 << 6)
#define VUL8_R_MONO_SFT                                       5
#define VUL8_R_MONO_MASK                                      0x1
#define VUL8_R_MONO_MASK_SFT                                  (0x1 << 5)
#define VUL8_MONO_SFT                                         4
#define VUL8_MONO_MASK                                        0x1
#define VUL8_MONO_MASK_SFT                                    (0x1 << 4)
#define VUL8_NORMAL_MODE_SFT                                  3
#define VUL8_NORMAL_MODE_MASK                                 0x1
#define VUL8_NORMAL_MODE_MASK_SFT                             (0x1 << 3)
#define VUL8_HALIGN_SFT                                       2
#define VUL8_HALIGN_MASK                                      0x1
#define VUL8_HALIGN_MASK_SFT                                  (0x1 << 2)
#define VUL8_HD_MODE_SFT                                      0
#define VUL8_HD_MODE_MASK                                     0x3
#define VUL8_HD_MODE_MASK_SFT                                 (0x3 << 0)

/* AFE_VUL9_BASE_MSB */
#define VUL9_BASE_ADDR_MSB_SFT                                0
#define VUL9_BASE_ADDR_MSB_MASK                               0x1ff
#define VUL9_BASE_ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_VUL9_BASE */
#define VUL9_BASE_ADDR_SFT                                    4
#define VUL9_BASE_ADDR_MASK                                   0xfffffff
#define VUL9_BASE_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_VUL9_CUR_MSB */
#define VUL9_CUR_PTR_MSB_SFT                                  0
#define VUL9_CUR_PTR_MSB_MASK                                 0x1ff
#define VUL9_CUR_PTR_MSB_MASK_SFT                             (0x1ff << 0)

/* AFE_VUL9_CUR */
#define VUL9_CUR_PTR_SFT                                      0
#define VUL9_CUR_PTR_MASK                                     0xffffffff
#define VUL9_CUR_PTR_MASK_SFT                                 (0xffffffff << 0)

/* AFE_VUL9_END_MSB */
#define VUL9_END_ADDR_MSB_SFT                                 0
#define VUL9_END_ADDR_MSB_MASK                                0x1ff
#define VUL9_END_ADDR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_VUL9_END */
#define VUL9_END_ADDR_SFT                                     4
#define VUL9_END_ADDR_MASK                                    0xfffffff
#define VUL9_END_ADDR_MASK_SFT                                (0xfffffff << 4)

/* AFE_VUL9_RCH_MON */
#define VUL9_RCH_DATA_SFT                                     0
#define VUL9_RCH_DATA_MASK                                    0xffffffff
#define VUL9_RCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL9_LCH_MON */
#define VUL9_LCH_DATA_SFT                                     0
#define VUL9_LCH_DATA_MASK                                    0xffffffff
#define VUL9_LCH_DATA_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL9_CON0 */
#define VUL9_ON_SFT                                           28
#define VUL9_ON_MASK                                          0x1
#define VUL9_ON_MASK_SFT                                      (0x1 << 28)
#define VUL9_MINLEN_SFT                                       20
#define VUL9_MINLEN_MASK                                      0x3
#define VUL9_MINLEN_MASK_SFT                                  (0x3 << 20)
#define VUL9_MAXLEN_SFT                                       16
#define VUL9_MAXLEN_MASK                                      0x3
#define VUL9_MAXLEN_MASK_SFT                                  (0x3 << 16)
#define VUL9_SEL_DOMAIN_SFT                                   13
#define VUL9_SEL_DOMAIN_MASK                                  0x7
#define VUL9_SEL_DOMAIN_MASK_SFT                              (0x7 << 13)
#define VUL9_SEL_FS_SFT                                       8
#define VUL9_SEL_FS_MASK                                      0x1f
#define VUL9_SEL_FS_MASK_SFT                                  (0x1f << 8)
#define VUL9_SW_CLEAR_BUF_FULL_SFT                            7
#define VUL9_SW_CLEAR_BUF_FULL_MASK                           0x1
#define VUL9_SW_CLEAR_BUF_FULL_MASK_SFT                       (0x1 << 7)
#define VUL9_WR_SIGN_SFT                                      6
#define VUL9_WR_SIGN_MASK                                     0x1
#define VUL9_WR_SIGN_MASK_SFT                                 (0x1 << 6)
#define VUL9_R_MONO_SFT                                       5
#define VUL9_R_MONO_MASK                                      0x1
#define VUL9_R_MONO_MASK_SFT                                  (0x1 << 5)
#define VUL9_MONO_SFT                                         4
#define VUL9_MONO_MASK                                        0x1
#define VUL9_MONO_MASK_SFT                                    (0x1 << 4)
#define VUL9_NORMAL_MODE_SFT                                  3
#define VUL9_NORMAL_MODE_MASK                                 0x1
#define VUL9_NORMAL_MODE_MASK_SFT                             (0x1 << 3)
#define VUL9_HALIGN_SFT                                       2
#define VUL9_HALIGN_MASK                                      0x1
#define VUL9_HALIGN_MASK_SFT                                  (0x1 << 2)
#define VUL9_HD_MODE_SFT                                      0
#define VUL9_HD_MODE_MASK                                     0x3
#define VUL9_HD_MODE_MASK_SFT                                 (0x3 << 0)

/* AFE_VUL10_BASE_MSB */
#define VUL10_BASE_ADDR_MSB_SFT                               0
#define VUL10_BASE_ADDR_MSB_MASK                              0x1ff
#define VUL10_BASE_ADDR_MSB_MASK_SFT                          (0x1ff << 0)

/* AFE_VUL10_BASE */
#define VUL10_BASE_ADDR_SFT                                   4
#define VUL10_BASE_ADDR_MASK                                  0xfffffff
#define VUL10_BASE_ADDR_MASK_SFT                              (0xfffffff << 4)

/* AFE_VUL10_CUR_MSB */
#define VUL10_CUR_PTR_MSB_SFT                                 0
#define VUL10_CUR_PTR_MSB_MASK                                0x1ff
#define VUL10_CUR_PTR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_VUL10_CUR */
#define VUL10_CUR_PTR_SFT                                     0
#define VUL10_CUR_PTR_MASK                                    0xffffffff
#define VUL10_CUR_PTR_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL10_END_MSB */
#define VUL10_END_ADDR_MSB_SFT                                0
#define VUL10_END_ADDR_MSB_MASK                               0x1ff
#define VUL10_END_ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_VUL10_END */
#define VUL10_END_ADDR_SFT                                    4
#define VUL10_END_ADDR_MASK                                   0xfffffff
#define VUL10_END_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_VUL10_RCH_MON */
#define VUL10_RCH_DATA_SFT                                    0
#define VUL10_RCH_DATA_MASK                                   0xffffffff
#define VUL10_RCH_DATA_MASK_SFT                               (0xffffffff << 0)

/* AFE_VUL10_LCH_MON */
#define VUL10_LCH_DATA_SFT                                    0
#define VUL10_LCH_DATA_MASK                                   0xffffffff
#define VUL10_LCH_DATA_MASK_SFT                               (0xffffffff << 0)

/* AFE_VUL10_CON0 */
#define VUL10_ON_SFT                                          28
#define VUL10_ON_MASK                                         0x1
#define VUL10_ON_MASK_SFT                                     (0x1 << 28)
#define VUL10_MINLEN_SFT                                      20
#define VUL10_MINLEN_MASK                                     0x3
#define VUL10_MINLEN_MASK_SFT                                 (0x3 << 20)
#define VUL10_MAXLEN_SFT                                      16
#define VUL10_MAXLEN_MASK                                     0x3
#define VUL10_MAXLEN_MASK_SFT                                 (0x3 << 16)
#define VUL10_SEL_DOMAIN_SFT                                  13
#define VUL10_SEL_DOMAIN_MASK                                 0x7
#define VUL10_SEL_DOMAIN_MASK_SFT                             (0x7 << 13)
#define VUL10_SEL_FS_SFT                                      8
#define VUL10_SEL_FS_MASK                                     0x1f
#define VUL10_SEL_FS_MASK_SFT                                 (0x1f << 8)
#define VUL10_SW_CLEAR_BUF_FULL_SFT                           7
#define VUL10_SW_CLEAR_BUF_FULL_MASK                          0x1
#define VUL10_SW_CLEAR_BUF_FULL_MASK_SFT                      (0x1 << 7)
#define VUL10_WR_SIGN_SFT                                     6
#define VUL10_WR_SIGN_MASK                                    0x1
#define VUL10_WR_SIGN_MASK_SFT                                (0x1 << 6)
#define VUL10_R_MONO_SFT                                      5
#define VUL10_R_MONO_MASK                                     0x1
#define VUL10_R_MONO_MASK_SFT                                 (0x1 << 5)
#define VUL10_MONO_SFT                                        4
#define VUL10_MONO_MASK                                       0x1
#define VUL10_MONO_MASK_SFT                                   (0x1 << 4)
#define VUL10_NORMAL_MODE_SFT                                 3
#define VUL10_NORMAL_MODE_MASK                                0x1
#define VUL10_NORMAL_MODE_MASK_SFT                            (0x1 << 3)
#define VUL10_HALIGN_SFT                                      2
#define VUL10_HALIGN_MASK                                     0x1
#define VUL10_HALIGN_MASK_SFT                                 (0x1 << 2)
#define VUL10_HD_MODE_SFT                                     0
#define VUL10_HD_MODE_MASK                                    0x3
#define VUL10_HD_MODE_MASK_SFT                                (0x3 << 0)

/* AFE_VUL24_BASE_MSB */
#define VUL24_BASE_ADDR_MSB_SFT                               0
#define VUL24_BASE_ADDR_MSB_MASK                              0x1ff
#define VUL24_BASE_ADDR_MSB_MASK_SFT                          (0x1ff << 0)

/* AFE_VUL24_BASE */
#define VUL24_BASE_ADDR_SFT                                   4
#define VUL24_BASE_ADDR_MASK                                  0xfffffff
#define VUL24_BASE_ADDR_MASK_SFT                              (0xfffffff << 4)

/* AFE_VUL24_CUR_MSB */
#define VUL24_CUR_PTR_MSB_SFT                                 0
#define VUL24_CUR_PTR_MSB_MASK                                0x1ff
#define VUL24_CUR_PTR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_VUL24_CUR */
#define VUL24_CUR_PTR_SFT                                     0
#define VUL24_CUR_PTR_MASK                                    0xffffffff
#define VUL24_CUR_PTR_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL24_END_MSB */
#define VUL24_END_ADDR_MSB_SFT                                0
#define VUL24_END_ADDR_MSB_MASK                               0x1ff
#define VUL24_END_ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_VUL24_END */
#define VUL24_END_ADDR_SFT                                    4
#define VUL24_END_ADDR_MASK                                   0xfffffff
#define VUL24_END_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_VUL24_CON0 */
#define OUT_ON_USE_VUL24_SFT                                  29
#define OUT_ON_USE_VUL24_MASK                                 0x1
#define OUT_ON_USE_VUL24_MASK_SFT                             (0x1 << 29)
#define VUL24_ON_SFT                                          28
#define VUL24_ON_MASK                                         0x1
#define VUL24_ON_MASK_SFT                                     (0x1 << 28)
#define VUL24_MINLEN_SFT                                      20
#define VUL24_MINLEN_MASK                                     0x3
#define VUL24_MINLEN_MASK_SFT                                 (0x3 << 20)
#define VUL24_MAXLEN_SFT                                      16
#define VUL24_MAXLEN_MASK                                     0x3
#define VUL24_MAXLEN_MASK_SFT                                 (0x3 << 16)
#define VUL24_SEL_DOMAIN_SFT                                  13
#define VUL24_SEL_DOMAIN_MASK                                 0x7
#define VUL24_SEL_DOMAIN_MASK_SFT                             (0x7 << 13)
#define VUL24_SEL_FS_SFT                                      8
#define VUL24_SEL_FS_MASK                                     0x1f
#define VUL24_SEL_FS_MASK_SFT                                 (0x1f << 8)
#define VUL24_SW_CLEAR_BUF_FULL_SFT                           7
#define VUL24_SW_CLEAR_BUF_FULL_MASK                          0x1
#define VUL24_SW_CLEAR_BUF_FULL_MASK_SFT                      (0x1 << 7)
#define VUL24_WR_SIGN_SFT                                     6
#define VUL24_WR_SIGN_MASK                                    0x1
#define VUL24_WR_SIGN_MASK_SFT                                (0x1 << 6)
#define VUL24_R_MONO_SFT                                      5
#define VUL24_R_MONO_MASK                                     0x1
#define VUL24_R_MONO_MASK_SFT                                 (0x1 << 5)
#define VUL24_MONO_SFT                                        4
#define VUL24_MONO_MASK                                       0x1
#define VUL24_MONO_MASK_SFT                                   (0x1 << 4)
#define VUL24_NORMAL_MODE_SFT                                 3
#define VUL24_NORMAL_MODE_MASK                                0x1
#define VUL24_NORMAL_MODE_MASK_SFT                            (0x1 << 3)
#define VUL24_HALIGN_SFT                                      2
#define VUL24_HALIGN_MASK                                     0x1
#define VUL24_HALIGN_MASK_SFT                                 (0x1 << 2)
#define VUL24_HD_MODE_SFT                                     0
#define VUL24_HD_MODE_MASK                                    0x3
#define VUL24_HD_MODE_MASK_SFT                                (0x3 << 0)

/* AFE_VUL25_BASE_MSB */
#define VUL25_BASE_ADDR_MSB_SFT                               0
#define VUL25_BASE_ADDR_MSB_MASK                              0x1ff
#define VUL25_BASE_ADDR_MSB_MASK_SFT                          (0x1ff << 0)

/* AFE_VUL25_BASE */
#define VUL25_BASE_ADDR_SFT                                   4
#define VUL25_BASE_ADDR_MASK                                  0xfffffff
#define VUL25_BASE_ADDR_MASK_SFT                              (0xfffffff << 4)

/* AFE_VUL25_CUR_MSB */
#define VUL25_CUR_PTR_MSB_SFT                                 0
#define VUL25_CUR_PTR_MSB_MASK                                0x1ff
#define VUL25_CUR_PTR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_VUL25_CUR */
#define VUL25_CUR_PTR_SFT                                     0
#define VUL25_CUR_PTR_MASK                                    0xffffffff
#define VUL25_CUR_PTR_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL25_END_MSB */
#define VUL25_END_ADDR_MSB_SFT                                0
#define VUL25_END_ADDR_MSB_MASK                               0x1ff
#define VUL25_END_ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_VUL25_END */
#define VUL25_END_ADDR_SFT                                    4
#define VUL25_END_ADDR_MASK                                   0xfffffff
#define VUL25_END_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_VUL25_CON0 */
#define OUT_ON_USE_VUL25_SFT                                  29
#define OUT_ON_USE_VUL25_MASK                                 0x1
#define OUT_ON_USE_VUL25_MASK_SFT                             (0x1 << 29)
#define VUL25_ON_SFT                                          28
#define VUL25_ON_MASK                                         0x1
#define VUL25_ON_MASK_SFT                                     (0x1 << 28)
#define VUL25_MINLEN_SFT                                      20
#define VUL25_MINLEN_MASK                                     0x3
#define VUL25_MINLEN_MASK_SFT                                 (0x3 << 20)
#define VUL25_MAXLEN_SFT                                      16
#define VUL25_MAXLEN_MASK                                     0x3
#define VUL25_MAXLEN_MASK_SFT                                 (0x3 << 16)
#define VUL25_SEL_DOMAIN_SFT                                  13
#define VUL25_SEL_DOMAIN_MASK                                 0x7
#define VUL25_SEL_DOMAIN_MASK_SFT                             (0x7 << 13)
#define VUL25_SEL_FS_SFT                                      8
#define VUL25_SEL_FS_MASK                                     0x1f
#define VUL25_SEL_FS_MASK_SFT                                 (0x1f << 8)
#define VUL25_SW_CLEAR_BUF_FULL_SFT                           7
#define VUL25_SW_CLEAR_BUF_FULL_MASK                          0x1
#define VUL25_SW_CLEAR_BUF_FULL_MASK_SFT                      (0x1 << 7)
#define VUL25_WR_SIGN_SFT                                     6
#define VUL25_WR_SIGN_MASK                                    0x1
#define VUL25_WR_SIGN_MASK_SFT                                (0x1 << 6)
#define VUL25_R_MONO_SFT                                      5
#define VUL25_R_MONO_MASK                                     0x1
#define VUL25_R_MONO_MASK_SFT                                 (0x1 << 5)
#define VUL25_MONO_SFT                                        4
#define VUL25_MONO_MASK                                       0x1
#define VUL25_MONO_MASK_SFT                                   (0x1 << 4)
#define VUL25_NORMAL_MODE_SFT                                 3
#define VUL25_NORMAL_MODE_MASK                                0x1
#define VUL25_NORMAL_MODE_MASK_SFT                            (0x1 << 3)
#define VUL25_HALIGN_SFT                                      2
#define VUL25_HALIGN_MASK                                     0x1
#define VUL25_HALIGN_MASK_SFT                                 (0x1 << 2)
#define VUL25_HD_MODE_SFT                                     0
#define VUL25_HD_MODE_MASK                                    0x3
#define VUL25_HD_MODE_MASK_SFT                                (0x3 << 0)

/* AFE_VUL26_BASE_MSB */
#define VUL26_BASE_ADDR_MSB_SFT                               0
#define VUL26_BASE_ADDR_MSB_MASK                              0x1ff
#define VUL26_BASE_ADDR_MSB_MASK_SFT                          (0x1ff << 0)

/* AFE_VUL26_BASE */
#define VUL26_BASE_ADDR_SFT                                   4
#define VUL26_BASE_ADDR_MASK                                  0xfffffff
#define VUL26_BASE_ADDR_MASK_SFT                              (0xfffffff << 4)

/* AFE_VUL26_CUR_MSB */
#define VUL26_CUR_PTR_MSB_SFT                                 0
#define VUL26_CUR_PTR_MSB_MASK                                0x1ff
#define VUL26_CUR_PTR_MSB_MASK_SFT                            (0x1ff << 0)

/* AFE_VUL26_CUR */
#define VUL26_CUR_PTR_SFT                                     0
#define VUL26_CUR_PTR_MASK                                    0xffffffff
#define VUL26_CUR_PTR_MASK_SFT                                (0xffffffff << 0)

/* AFE_VUL26_END_MSB */
#define VUL26_END_ADDR_MSB_SFT                                0
#define VUL26_END_ADDR_MSB_MASK                               0x1ff
#define VUL26_END_ADDR_MSB_MASK_SFT                           (0x1ff << 0)

/* AFE_VUL26_END */
#define VUL26_END_ADDR_SFT                                    4
#define VUL26_END_ADDR_MASK                                   0xfffffff
#define VUL26_END_ADDR_MASK_SFT                               (0xfffffff << 4)

/* AFE_VUL26_CON0 */
#define OUT_ON_USE_VUL26_SFT                                  29
#define OUT_ON_USE_VUL26_MASK                                 0x1
#define OUT_ON_USE_VUL26_MASK_SFT                             (0x1 << 29)
#define VUL26_ON_SFT                                          28
#define VUL26_ON_MASK                                         0x1
#define VUL26_ON_MASK_SFT                                     (0x1 << 28)
#define VUL26_MINLEN_SFT                                      20
#define VUL26_MINLEN_MASK                                     0x3
#define VUL26_MINLEN_MASK_SFT                                 (0x3 << 20)
#define VUL26_MAXLEN_SFT                                      16
#define VUL26_MAXLEN_MASK                                     0x3
#define VUL26_MAXLEN_MASK_SFT                                 (0x3 << 16)
#define VUL26_SEL_DOMAIN_SFT                                  13
#define VUL26_SEL_DOMAIN_MASK                                 0x7
#define VUL26_SEL_DOMAIN_MASK_SFT                             (0x7 << 13)
#define VUL26_SEL_FS_SFT                                      8
#define VUL26_SEL_FS_MASK                                     0x1f
#define VUL26_SEL_FS_MASK_SFT                                 (0x1f << 8)
#define VUL26_SW_CLEAR_BUF_FULL_SFT                           7
#define VUL26_SW_CLEAR_BUF_FULL_MASK                          0x1
#define VUL26_SW_CLEAR_BUF_FULL_MASK_SFT                      (0x1 << 7)
#define VUL26_WR_SIGN_SFT                                     6
#define VUL26_WR_SIGN_MASK                                    0x1
#define VUL26_WR_SIGN_MASK_SFT                                (0x1 << 6)
#define VUL26_R_MONO_SFT                                      5
#define VUL26_R_MONO_MASK                                     0x1
#define VUL26_R_MONO_MASK_SFT                                 (0x1 << 5)
#define VUL26_MONO_SFT                                        4
#define VUL26_MONO_MASK                                       0x1
#define VUL26_MONO_MASK_SFT                                   (0x1 << 4)
#define VUL26_NORMAL_MODE_SFT                                 3
#define VUL26_NORMAL_MODE_MASK                                0x1
#define VUL26_NORMAL_MODE_MASK_SFT                            (0x1 << 3)
#define VUL26_HALIGN_SFT                                      2
#define VUL26_HALIGN_MASK                                     0x1
#define VUL26_HALIGN_MASK_SFT                                 (0x1 << 2)
#define VUL26_HD_MODE_SFT                                     0
#define VUL26_HD_MODE_MASK                                    0x3
#define VUL26_HD_MODE_MASK_SFT                                (0x3 << 0)

/* AFE_VUL_CM0_BASE_MSB */
#define VUL_CM0_BASE_ADDR_MSB_SFT                             0
#define VUL_CM0_BASE_ADDR_MSB_MASK                            0x1ff
#define VUL_CM0_BASE_ADDR_MSB_MASK_SFT                        (0x1ff << 0)

/* AFE_VUL_CM0_BASE */
#define VUL_CM0_BASE_ADDR_SFT                                 4
#define VUL_CM0_BASE_ADDR_MASK                                0xfffffff
#define VUL_CM0_BASE_ADDR_MASK_SFT                            (0xfffffff << 4)

/* AFE_VUL_CM0_CUR_MSB */
#define VUL_CM0_CUR_PTR_MSB_SFT                               0
#define VUL_CM0_CUR_PTR_MSB_MASK                              0x1ff
#define VUL_CM0_CUR_PTR_MSB_MASK_SFT                          (0x1ff << 0)

/* AFE_VUL_CM0_CUR */
#define VUL_CM0_CUR_PTR_SFT                                   0
#define VUL_CM0_CUR_PTR_MASK                                  0xffffffff
#define VUL_CM0_CUR_PTR_MASK_SFT                              (0xffffffff << 0)

/* AFE_VUL_CM0_END_MSB */
#define VUL_CM0_END_ADDR_MSB_SFT                              0
#define VUL_CM0_END_ADDR_MSB_MASK                             0x1ff
#define VUL_CM0_END_ADDR_MSB_MASK_SFT                         (0x1ff << 0)

/* AFE_VUL_CM0_END */
#define VUL_CM0_END_ADDR_SFT                                  4
#define VUL_CM0_END_ADDR_MASK                                 0xfffffff
#define VUL_CM0_END_ADDR_MASK_SFT                             (0xfffffff << 4)

/* AFE_VUL_CM0_CON0 */
#define VUL_CM0_ON_SFT                                        28
#define VUL_CM0_ON_MASK                                       0x1
#define VUL_CM0_ON_MASK_SFT                                   (0x1 << 28)
#define VUL_CM0_REG_CH_SHIFT_MODE_SFT                         26
#define VUL_CM0_REG_CH_SHIFT_MODE_MASK                        0x1
#define VUL_CM0_REG_CH_SHIFT_MODE_MASK_SFT                    (0x1 << 26)
#define VUL_CM0_RG_FORCE_NO_MASK_EXTRA_SFT                    25
#define VUL_CM0_RG_FORCE_NO_MASK_EXTRA_MASK                   0x1
#define VUL_CM0_RG_FORCE_NO_MASK_EXTRA_MASK_SFT               (0x1 << 25)
#define VUL_CM0_SW_CLEAR_BUF_FULL_SFT                         24
#define VUL_CM0_SW_CLEAR_BUF_FULL_MASK                        0x1
#define VUL_CM0_SW_CLEAR_BUF_FULL_MASK_SFT                    (0x1 << 24)
#define VUL_CM0_ULTRA_TH_SFT                                  20
#define VUL_CM0_ULTRA_TH_MASK                                 0xf
#define VUL_CM0_ULTRA_TH_MASK_SFT                             (0xf << 20)
#define VUL_CM0_NORMAL_MODE_SFT                               17
#define VUL_CM0_NORMAL_MODE_MASK                              0x1
#define VUL_CM0_NORMAL_MODE_MASK_SFT                          (0x1 << 17)
#define VUL_CM0_ODD_USE_EVEN_SFT                              16
#define VUL_CM0_ODD_USE_EVEN_MASK                             0x1
#define VUL_CM0_ODD_USE_EVEN_MASK_SFT                         (0x1 << 16)
#define VUL_CM0_AXI_REQ_MAXLEN_SFT                            12
#define VUL_CM0_AXI_REQ_MAXLEN_MASK                           0x3
#define VUL_CM0_AXI_REQ_MAXLEN_MASK_SFT                       (0x3 << 12)
#define VUL_CM0_AXI_REQ_MINLEN_SFT                            8
#define VUL_CM0_AXI_REQ_MINLEN_MASK                           0x3
#define VUL_CM0_AXI_REQ_MINLEN_MASK_SFT                       (0x3 << 8)
#define VUL_CM0_HALIGN_SFT                                    7
#define VUL_CM0_HALIGN_MASK                                   0x1
#define VUL_CM0_HALIGN_MASK_SFT                               (0x1 << 7)
#define VUL_CM0_SIGN_EXT_SFT                                  6
#define VUL_CM0_SIGN_EXT_MASK                                 0x1
#define VUL_CM0_SIGN_EXT_MASK_SFT                             (0x1 << 6)
#define VUL_CM0_HD_MODE_SFT                                   4
#define VUL_CM0_HD_MODE_MASK                                  0x3
#define VUL_CM0_HD_MODE_MASK_SFT                              (0x3 << 4)
#define VUL_CM0_MAKE_EXTRA_UPDATE_SFT                         3
#define VUL_CM0_MAKE_EXTRA_UPDATE_MASK                        0x1
#define VUL_CM0_MAKE_EXTRA_UPDATE_MASK_SFT                    (0x1 << 3)
#define VUL_CM0_AGENT_FREE_RUN_SFT                            2
#define VUL_CM0_AGENT_FREE_RUN_MASK                           0x1
#define VUL_CM0_AGENT_FREE_RUN_MASK_SFT                       (0x1 << 2)
#define VUL_CM0_USE_INT_ODD_SFT                               1
#define VUL_CM0_USE_INT_ODD_MASK                              0x1
#define VUL_CM0_USE_INT_ODD_MASK_SFT                          (0x1 << 1)
#define VUL_CM0_INT_ODD_FLAG_SFT                              0
#define VUL_CM0_INT_ODD_FLAG_MASK                             0x1
#define VUL_CM0_INT_ODD_FLAG_MASK_SFT                         (0x1 << 0)

/* AFE_VUL_CM1_BASE_MSB */
#define VUL_CM1_BASE_ADDR_MSB_SFT                             0
#define VUL_CM1_BASE_ADDR_MSB_MASK                            0x1ff
#define VUL_CM1_BASE_ADDR_MSB_MASK_SFT                        (0x1ff << 0)

/* AFE_VUL_CM1_BASE */
#define VUL_CM1_BASE_ADDR_SFT                                 4
#define VUL_CM1_BASE_ADDR_MASK                                0xfffffff
#define VUL_CM1_BASE_ADDR_MASK_SFT                            (0xfffffff << 4)

/* AFE_VUL_CM1_CUR_MSB */
#define VUL_CM1_CUR_PTR_MSB_SFT                               0
#define VUL_CM1_CUR_PTR_MSB_MASK                              0x1ff
#define VUL_CM1_CUR_PTR_MSB_MASK_SFT                          (0x1ff << 0)

/* AFE_VUL_CM1_CUR */
#define VUL_CM1_CUR_PTR_SFT                                   0
#define VUL_CM1_CUR_PTR_MASK                                  0xffffffff
#define VUL_CM1_CUR_PTR_MASK_SFT                              (0xffffffff << 0)

/* AFE_VUL_CM1_END_MSB */
#define VUL_CM1_END_ADDR_MSB_SFT                              0
#define VUL_CM1_END_ADDR_MSB_MASK                             0x1ff
#define VUL_CM1_END_ADDR_MSB_MASK_SFT                         (0x1ff << 0)

/* AFE_VUL_CM1_END */
#define VUL_CM1_END_ADDR_SFT                                  4
#define VUL_CM1_END_ADDR_MASK                                 0xfffffff
#define VUL_CM1_END_ADDR_MASK_SFT                             (0xfffffff << 4)

/* AFE_VUL_CM1_CON0 */
#define VUL_CM1_ON_SFT                                        28
#define VUL_CM1_ON_MASK                                       0x1
#define VUL_CM1_ON_MASK_SFT                                   (0x1 << 28)
#define VUL_CM1_REG_CH_SHIFT_MODE_SFT                         26
#define VUL_CM1_REG_CH_SHIFT_MODE_MASK                        0x1
#define VUL_CM1_REG_CH_SHIFT_MODE_MASK_SFT                    (0x1 << 26)
#define VUL_CM1_RG_FORCE_NO_MASK_EXTRA_SFT                    25
#define VUL_CM1_RG_FORCE_NO_MASK_EXTRA_MASK                   0x1
#define VUL_CM1_RG_FORCE_NO_MASK_EXTRA_MASK_SFT               (0x1 << 25)
#define VUL_CM1_SW_CLEAR_BUF_FULL_SFT                         24
#define VUL_CM1_SW_CLEAR_BUF_FULL_MASK                        0x1
#define VUL_CM1_SW_CLEAR_BUF_FULL_MASK_SFT                    (0x1 << 24)
#define VUL_CM1_ULTRA_TH_SFT                                  20
#define VUL_CM1_ULTRA_TH_MASK                                 0xf
#define VUL_CM1_ULTRA_TH_MASK_SFT                             (0xf << 20)
#define VUL_CM1_NORMAL_MODE_SFT                               17
#define VUL_CM1_NORMAL_MODE_MASK                              0x1
#define VUL_CM1_NORMAL_MODE_MASK_SFT                          (0x1 << 17)
#define VUL_CM1_ODD_USE_EVEN_SFT                              16
#define VUL_CM1_ODD_USE_EVEN_MASK                             0x1
#define VUL_CM1_ODD_USE_EVEN_MASK_SFT                         (0x1 << 16)
#define VUL_CM1_AXI_REQ_MAXLEN_SFT                            12
#define VUL_CM1_AXI_REQ_MAXLEN_MASK                           0x3
#define VUL_CM1_AXI_REQ_MAXLEN_MASK_SFT                       (0x3 << 12)
#define VUL_CM1_AXI_REQ_MINLEN_SFT                            8
#define VUL_CM1_AXI_REQ_MINLEN_MASK                           0x3
#define VUL_CM1_AXI_REQ_MINLEN_MASK_SFT                       (0x3 << 8)
#define VUL_CM1_HALIGN_SFT                                    7
#define VUL_CM1_HALIGN_MASK                                   0x1
#define VUL_CM1_HALIGN_MASK_SFT                               (0x1 << 7)
#define VUL_CM1_SIGN_EXT_SFT                                  6
#define VUL_CM1_SIGN_EXT_MASK                                 0x1
#define VUL_CM1_SIGN_EXT_MASK_SFT                             (0x1 << 6)
#define VUL_CM1_HD_MODE_SFT                                   4
#define VUL_CM1_HD_MODE_MASK                                  0x3
#define VUL_CM1_HD_MODE_MASK_SFT                              (0x3 << 4)
#define VUL_CM1_MAKE_EXTRA_UPDATE_SFT                         3
#define VUL_CM1_MAKE_EXTRA_UPDATE_MASK                        0x1
#define VUL_CM1_MAKE_EXTRA_UPDATE_MASK_SFT                    (0x1 << 3)
#define VUL_CM1_AGENT_FREE_RUN_SFT                            2
#define VUL_CM1_AGENT_FREE_RUN_MASK                           0x1
#define VUL_CM1_AGENT_FREE_RUN_MASK_SFT                       (0x1 << 2)
#define VUL_CM1_USE_INT_ODD_SFT                               1
#define VUL_CM1_USE_INT_ODD_MASK                              0x1
#define VUL_CM1_USE_INT_ODD_MASK_SFT                          (0x1 << 1)
#define VUL_CM1_INT_ODD_FLAG_SFT                              0
#define VUL_CM1_INT_ODD_FLAG_MASK                             0x1
#define VUL_CM1_INT_ODD_FLAG_MASK_SFT                         (0x1 << 0)

/* AFE_VUL_CM2_BASE_MSB */
#define VUL_CM2_BASE_ADDR_MSB_SFT                             0
#define VUL_CM2_BASE_ADDR_MSB_MASK                            0x1ff
#define VUL_CM2_BASE_ADDR_MSB_MASK_SFT                        (0x1ff << 0)

/* AFE_VUL_CM2_BASE */
#define VUL_CM2_BASE_ADDR_SFT                                 4
#define VUL_CM2_BASE_ADDR_MASK                                0xfffffff
#define VUL_CM2_BASE_ADDR_MASK_SFT                            (0xfffffff << 4)

/* AFE_VUL_CM2_CUR_MSB */
#define VUL_CM2_CUR_PTR_MSB_SFT                               0
#define VUL_CM2_CUR_PTR_MSB_MASK                              0x1ff
#define VUL_CM2_CUR_PTR_MSB_MASK_SFT                          (0x1ff << 0)

/* AFE_VUL_CM2_CUR */
#define VUL_CM2_CUR_PTR_SFT                                   0
#define VUL_CM2_CUR_PTR_MASK                                  0xffffffff
#define VUL_CM2_CUR_PTR_MASK_SFT                              (0xffffffff << 0)

/* AFE_VUL_CM2_END_MSB */
#define VUL_CM2_END_ADDR_MSB_SFT                              0
#define VUL_CM2_END_ADDR_MSB_MASK                             0x1ff
#define VUL_CM2_END_ADDR_MSB_MASK_SFT                         (0x1ff << 0)

/* AFE_VUL_CM2_END */
#define VUL_CM2_END_ADDR_SFT                                  4
#define VUL_CM2_END_ADDR_MASK                                 0xfffffff
#define VUL_CM2_END_ADDR_MASK_SFT                             (0xfffffff << 4)

/* AFE_VUL_CM2_CON0 */
#define VUL_CM2_ON_SFT                                        28
#define VUL_CM2_ON_MASK                                       0x1
#define VUL_CM2_ON_MASK_SFT                                   (0x1 << 28)
#define VUL_CM2_REG_CH_SHIFT_MODE_SFT                         26
#define VUL_CM2_REG_CH_SHIFT_MODE_MASK                        0x1
#define VUL_CM2_REG_CH_SHIFT_MODE_MASK_SFT                    (0x1 << 26)
#define VUL_CM2_RG_FORCE_NO_MASK_EXTRA_SFT                    25
#define VUL_CM2_RG_FORCE_NO_MASK_EXTRA_MASK                   0x1
#define VUL_CM2_RG_FORCE_NO_MASK_EXTRA_MASK_SFT               (0x1 << 25)
#define VUL_CM2_SW_CLEAR_BUF_FULL_SFT                         24
#define VUL_CM2_SW_CLEAR_BUF_FULL_MASK                        0x1
#define VUL_CM2_SW_CLEAR_BUF_FULL_MASK_SFT                    (0x1 << 24)
#define VUL_CM2_ULTRA_TH_SFT                                  20
#define VUL_CM2_ULTRA_TH_MASK                                 0xf
#define VUL_CM2_ULTRA_TH_MASK_SFT                             (0xf << 20)
#define VUL_CM2_NORMAL_MODE_SFT                               17
#define VUL_CM2_NORMAL_MODE_MASK                              0x1
#define VUL_CM2_NORMAL_MODE_MASK_SFT                          (0x1 << 17)
#define VUL_CM2_ODD_USE_EVEN_SFT                              16
#define VUL_CM2_ODD_USE_EVEN_MASK                             0x1
#define VUL_CM2_ODD_USE_EVEN_MASK_SFT                         (0x1 << 16)
#define VUL_CM2_AXI_REQ_MAXLEN_SFT                            12
#define VUL_CM2_AXI_REQ_MAXLEN_MASK                           0x3
#define VUL_CM2_AXI_REQ_MAXLEN_MASK_SFT                       (0x3 << 12)
#define VUL_CM2_AXI_REQ_MINLEN_SFT                            8
#define VUL_CM2_AXI_REQ_MINLEN_MASK                           0x3
#define VUL_CM2_AXI_REQ_MINLEN_MASK_SFT                       (0x3 << 8)
#define VUL_CM2_HALIGN_SFT                                    7
#define VUL_CM2_HALIGN_MASK                                   0x1
#define VUL_CM2_HALIGN_MASK_SFT                               (0x1 << 7)
#define VUL_CM2_SIGN_EXT_SFT                                  6
#define VUL_CM2_SIGN_EXT_MASK                                 0x1
#define VUL_CM2_SIGN_EXT_MASK_SFT                             (0x1 << 6)
#define VUL_CM2_HD_MODE_SFT                                   4
#define VUL_CM2_HD_MODE_MASK                                  0x3
#define VUL_CM2_HD_MODE_MASK_SFT                              (0x3 << 4)
#define VUL_CM2_MAKE_EXTRA_UPDATE_SFT                         3
#define VUL_CM2_MAKE_EXTRA_UPDATE_MASK                        0x1
#define VUL_CM2_MAKE_EXTRA_UPDATE_MASK_SFT                    (0x1 << 3)
#define VUL_CM2_AGENT_FREE_RUN_SFT                            2
#define VUL_CM2_AGENT_FREE_RUN_MASK                           0x1
#define VUL_CM2_AGENT_FREE_RUN_MASK_SFT                       (0x1 << 2)
#define VUL_CM2_USE_INT_ODD_SFT                               1
#define VUL_CM2_USE_INT_ODD_MASK                              0x1
#define VUL_CM2_USE_INT_ODD_MASK_SFT                          (0x1 << 1)
#define VUL_CM2_INT_ODD_FLAG_SFT                              0
#define VUL_CM2_INT_ODD_FLAG_MASK                             0x1
#define VUL_CM2_INT_ODD_FLAG_MASK_SFT                         (0x1 << 0)

/* AFE_ETDM_IN0_BASE_MSB */
#define ETDM_IN0_BASE_ADDR_MSB_SFT                            0
#define ETDM_IN0_BASE_ADDR_MSB_MASK                           0x1ff
#define ETDM_IN0_BASE_ADDR_MSB_MASK_SFT                       (0x1ff << 0)

/* AFE_ETDM_IN0_BASE */
#define ETDM_IN0_BASE_ADDR_SFT                                4
#define ETDM_IN0_BASE_ADDR_MASK                               0xfffffff
#define ETDM_IN0_BASE_ADDR_MASK_SFT                           (0xfffffff << 4)

/* AFE_ETDM_IN0_CUR_MSB */
#define ETDM_IN0_CUR_PTR_MSB_SFT                              0
#define ETDM_IN0_CUR_PTR_MSB_MASK                             0x1ff
#define ETDM_IN0_CUR_PTR_MSB_MASK_SFT                         (0x1ff << 0)

/* AFE_ETDM_IN0_CUR */
#define ETDM_IN0_CUR_PTR_SFT                                  0
#define ETDM_IN0_CUR_PTR_MASK                                 0xffffffff
#define ETDM_IN0_CUR_PTR_MASK_SFT                             (0xffffffff << 0)

/* AFE_ETDM_IN0_END_MSB */
#define ETDM_IN0_END_ADDR_MSB_SFT                             0
#define ETDM_IN0_END_ADDR_MSB_MASK                            0x1ff
#define ETDM_IN0_END_ADDR_MSB_MASK_SFT                        (0x1ff << 0)

/* AFE_ETDM_IN0_END */
#define ETDM_IN0_END_ADDR_SFT                                 4
#define ETDM_IN0_END_ADDR_MASK                                0xfffffff
#define ETDM_IN0_END_ADDR_MASK_SFT                            (0xfffffff << 4)

/* AFE_ETDM_IN0_CON0 */
#define ETDM_IN0_CH_NUM_SFT                                   28
#define ETDM_IN0_CH_NUM_MASK                                  0xf
#define ETDM_IN0_CH_NUM_MASK_SFT                              (0xf << 28)
#define ETDM_IN0_ON_SFT                                       27
#define ETDM_IN0_ON_MASK                                      0x1
#define ETDM_IN0_ON_MASK_SFT                                  (0x1 << 27)
#define ETDM_IN0_REG_CH_SHIFT_MODE_SFT                        26
#define ETDM_IN0_REG_CH_SHIFT_MODE_MASK                       0x1
#define ETDM_IN0_REG_CH_SHIFT_MODE_MASK_SFT                   (0x1 << 26)
#define ETDM_IN0_RG_FORCE_NO_MASK_EXTRA_SFT                   25
#define ETDM_IN0_RG_FORCE_NO_MASK_EXTRA_MASK                  0x1
#define ETDM_IN0_RG_FORCE_NO_MASK_EXTRA_MASK_SFT              (0x1 << 25)
#define ETDM_IN0_SW_CLEAR_BUF_FULL_SFT                        24
#define ETDM_IN0_SW_CLEAR_BUF_FULL_MASK                       0x1
#define ETDM_IN0_SW_CLEAR_BUF_FULL_MASK_SFT                   (0x1 << 24)
#define ETDM_IN0_ULTRA_TH_SFT                                 20
#define ETDM_IN0_ULTRA_TH_MASK                                0xf
#define ETDM_IN0_ULTRA_TH_MASK_SFT                            (0xf << 20)
#define ETDM_IN0_NORMAL_MODE_SFT                              17
#define ETDM_IN0_NORMAL_MODE_MASK                             0x1
#define ETDM_IN0_NORMAL_MODE_MASK_SFT                         (0x1 << 17)
#define ETDM_IN0_ODD_USE_EVEN_SFT                             16
#define ETDM_IN0_ODD_USE_EVEN_MASK                            0x1
#define ETDM_IN0_ODD_USE_EVEN_MASK_SFT                        (0x1 << 16)
#define ETDM_IN0_AXI_REQ_MAXLEN_SFT                           12
#define ETDM_IN0_AXI_REQ_MAXLEN_MASK                          0x3
#define ETDM_IN0_AXI_REQ_MAXLEN_MASK_SFT                      (0x3 << 12)
#define ETDM_IN0_AXI_REQ_MINLEN_SFT                           8
#define ETDM_IN0_AXI_REQ_MINLEN_MASK                          0x3
#define ETDM_IN0_AXI_REQ_MINLEN_MASK_SFT                      (0x3 << 8)
#define ETDM_IN0_HALIGN_SFT                                   7
#define ETDM_IN0_HALIGN_MASK                                  0x1
#define ETDM_IN0_HALIGN_MASK_SFT                              (0x1 << 7)
#define ETDM_IN0_SIGN_EXT_SFT                                 6
#define ETDM_IN0_SIGN_EXT_MASK                                0x1
#define ETDM_IN0_SIGN_EXT_MASK_SFT                            (0x1 << 6)
#define ETDM_IN0_HD_MODE_SFT                                  4
#define ETDM_IN0_HD_MODE_MASK                                 0x3
#define ETDM_IN0_HD_MODE_MASK_SFT                             (0x3 << 4)
#define ETDM_IN0_MAKE_EXTRA_UPDATE_SFT                        3
#define ETDM_IN0_MAKE_EXTRA_UPDATE_MASK                       0x1
#define ETDM_IN0_MAKE_EXTRA_UPDATE_MASK_SFT                   (0x1 << 3)
#define ETDM_IN0_AGENT_FREE_RUN_SFT                           2
#define ETDM_IN0_AGENT_FREE_RUN_MASK                          0x1
#define ETDM_IN0_AGENT_FREE_RUN_MASK_SFT                      (0x1 << 2)
#define ETDM_IN0_USE_INT_ODD_SFT                              1
#define ETDM_IN0_USE_INT_ODD_MASK                             0x1
#define ETDM_IN0_USE_INT_ODD_MASK_SFT                         (0x1 << 1)
#define ETDM_IN0_INT_ODD_FLAG_SFT                             0
#define ETDM_IN0_INT_ODD_FLAG_MASK                            0x1
#define ETDM_IN0_INT_ODD_FLAG_MASK_SFT                        (0x1 << 0)

/* AFE_ETDM_IN1_BASE_MSB */
#define ETDM_IN1_BASE_ADDR_MSB_SFT                            0
#define ETDM_IN1_BASE_ADDR_MSB_MASK                           0x1ff
#define ETDM_IN1_BASE_ADDR_MSB_MASK_SFT                       (0x1ff << 0)

/* AFE_ETDM_IN1_BASE */
#define ETDM_IN1_BASE_ADDR_SFT                                4
#define ETDM_IN1_BASE_ADDR_MASK                               0xfffffff
#define ETDM_IN1_BASE_ADDR_MASK_SFT                           (0xfffffff << 4)

/* AFE_ETDM_IN1_CUR_MSB */
#define ETDM_IN1_CUR_PTR_MSB_SFT                              0
#define ETDM_IN1_CUR_PTR_MSB_MASK                             0x1ff
#define ETDM_IN1_CUR_PTR_MSB_MASK_SFT                         (0x1ff << 0)

/* AFE_ETDM_IN1_CUR */
#define ETDM_IN1_CUR_PTR_SFT                                  0
#define ETDM_IN1_CUR_PTR_MASK                                 0xffffffff
#define ETDM_IN1_CUR_PTR_MASK_SFT                             (0xffffffff << 0)

/* AFE_ETDM_IN1_END_MSB */
#define ETDM_IN1_END_ADDR_MSB_SFT                             0
#define ETDM_IN1_END_ADDR_MSB_MASK                            0x1ff
#define ETDM_IN1_END_ADDR_MSB_MASK_SFT                        (0x1ff << 0)

/* AFE_ETDM_IN1_END */
#define ETDM_IN1_END_ADDR_SFT                                 4
#define ETDM_IN1_END_ADDR_MASK                                0xfffffff
#define ETDM_IN1_END_ADDR_MASK_SFT                            (0xfffffff << 4)

/* AFE_ETDM_IN1_CON0 */
#define ETDM_IN1_CH_NUM_SFT                                   28
#define ETDM_IN1_CH_NUM_MASK                                  0xf
#define ETDM_IN1_CH_NUM_MASK_SFT                              (0xf << 28)
#define ETDM_IN1_ON_SFT                                       27
#define ETDM_IN1_ON_MASK                                      0x1
#define ETDM_IN1_ON_MASK_SFT                                  (0x1 << 27)
#define ETDM_IN1_REG_CH_SHIFT_MODE_SFT                        26
#define ETDM_IN1_REG_CH_SHIFT_MODE_MASK                       0x1
#define ETDM_IN1_REG_CH_SHIFT_MODE_MASK_SFT                   (0x1 << 26)
#define ETDM_IN1_RG_FORCE_NO_MASK_EXTRA_SFT                   25
#define ETDM_IN1_RG_FORCE_NO_MASK_EXTRA_MASK                  0x1
#define ETDM_IN1_RG_FORCE_NO_MASK_EXTRA_MASK_SFT              (0x1 << 25)
#define ETDM_IN1_SW_CLEAR_BUF_FULL_SFT                        24
#define ETDM_IN1_SW_CLEAR_BUF_FULL_MASK                       0x1
#define ETDM_IN1_SW_CLEAR_BUF_FULL_MASK_SFT                   (0x1 << 24)
#define ETDM_IN1_ULTRA_TH_SFT                                 20
#define ETDM_IN1_ULTRA_TH_MASK                                0xf
#define ETDM_IN1_ULTRA_TH_MASK_SFT                            (0xf << 20)
#define ETDM_IN1_NORMAL_MODE_SFT                              17
#define ETDM_IN1_NORMAL_MODE_MASK                             0x1
#define ETDM_IN1_NORMAL_MODE_MASK_SFT                         (0x1 << 17)
#define ETDM_IN1_ODD_USE_EVEN_SFT                             16
#define ETDM_IN1_ODD_USE_EVEN_MASK                            0x1
#define ETDM_IN1_ODD_USE_EVEN_MASK_SFT                        (0x1 << 16)
#define ETDM_IN1_AXI_REQ_MAXLEN_SFT                           12
#define ETDM_IN1_AXI_REQ_MAXLEN_MASK                          0x3
#define ETDM_IN1_AXI_REQ_MAXLEN_MASK_SFT                      (0x3 << 12)
#define ETDM_IN1_AXI_REQ_MINLEN_SFT                           8
#define ETDM_IN1_AXI_REQ_MINLEN_MASK                          0x3
#define ETDM_IN1_AXI_REQ_MINLEN_MASK_SFT                      (0x3 << 8)
#define ETDM_IN1_HALIGN_SFT                                   7
#define ETDM_IN1_HALIGN_MASK                                  0x1
#define ETDM_IN1_HALIGN_MASK_SFT                              (0x1 << 7)
#define ETDM_IN1_SIGN_EXT_SFT                                 6
#define ETDM_IN1_SIGN_EXT_MASK                                0x1
#define ETDM_IN1_SIGN_EXT_MASK_SFT                            (0x1 << 6)
#define ETDM_IN1_HD_MODE_SFT                                  4
#define ETDM_IN1_HD_MODE_MASK                                 0x3
#define ETDM_IN1_HD_MODE_MASK_SFT                             (0x3 << 4)
#define ETDM_IN1_MAKE_EXTRA_UPDATE_SFT                        3
#define ETDM_IN1_MAKE_EXTRA_UPDATE_MASK                       0x1
#define ETDM_IN1_MAKE_EXTRA_UPDATE_MASK_SFT                   (0x1 << 3)
#define ETDM_IN1_AGENT_FREE_RUN_SFT                           2
#define ETDM_IN1_AGENT_FREE_RUN_MASK                          0x1
#define ETDM_IN1_AGENT_FREE_RUN_MASK_SFT                      (0x1 << 2)
#define ETDM_IN1_USE_INT_ODD_SFT                              1
#define ETDM_IN1_USE_INT_ODD_MASK                             0x1
#define ETDM_IN1_USE_INT_ODD_MASK_SFT                         (0x1 << 1)
#define ETDM_IN1_INT_ODD_FLAG_SFT                             0
#define ETDM_IN1_INT_ODD_FLAG_MASK                            0x1
#define ETDM_IN1_INT_ODD_FLAG_MASK_SFT                        (0x1 << 0)

/* AFE_ETDM_IN2_BASE_MSB */
#define ETDM_IN2_BASE_ADDR_MSB_SFT                            0
#define ETDM_IN2_BASE_ADDR_MSB_MASK                           0x1ff
#define ETDM_IN2_BASE_ADDR_MSB_MASK_SFT                       (0x1ff << 0)

/* AFE_ETDM_IN2_BASE */
#define ETDM_IN2_BASE_ADDR_SFT                                4
#define ETDM_IN2_BASE_ADDR_MASK                               0xfffffff
#define ETDM_IN2_BASE_ADDR_MASK_SFT                           (0xfffffff << 4)

/* AFE_ETDM_IN2_CUR_MSB */
#define ETDM_IN2_CUR_PTR_MSB_SFT                              0
#define ETDM_IN2_CUR_PTR_MSB_MASK                             0x1ff
#define ETDM_IN2_CUR_PTR_MSB_MASK_SFT                         (0x1ff << 0)

/* AFE_ETDM_IN2_CUR */
#define ETDM_IN2_CUR_PTR_SFT                                  0
#define ETDM_IN2_CUR_PTR_MASK                                 0xffffffff
#define ETDM_IN2_CUR_PTR_MASK_SFT                             (0xffffffff << 0)

/* AFE_ETDM_IN2_END_MSB */
#define ETDM_IN2_END_ADDR_MSB_SFT                             0
#define ETDM_IN2_END_ADDR_MSB_MASK                            0x1ff
#define ETDM_IN2_END_ADDR_MSB_MASK_SFT                        (0x1ff << 0)

/* AFE_ETDM_IN2_END */
#define ETDM_IN2_END_ADDR_SFT                                 4
#define ETDM_IN2_END_ADDR_MASK                                0xfffffff
#define ETDM_IN2_END_ADDR_MASK_SFT                            (0xfffffff << 4)

/* AFE_ETDM_IN2_CON0 */
#define ETDM_IN2_CH_NUM_SFT                                   28
#define ETDM_IN2_CH_NUM_MASK                                  0xf
#define ETDM_IN2_CH_NUM_MASK_SFT                              (0xf << 28)
#define ETDM_IN2_ON_SFT                                       27
#define ETDM_IN2_ON_MASK                                      0x1
#define ETDM_IN2_ON_MASK_SFT                                  (0x1 << 27)
#define ETDM_IN2_REG_CH_SHIFT_MODE_SFT                        26
#define ETDM_IN2_REG_CH_SHIFT_MODE_MASK                       0x1
#define ETDM_IN2_REG_CH_SHIFT_MODE_MASK_SFT                   (0x1 << 26)
#define ETDM_IN2_RG_FORCE_NO_MASK_EXTRA_SFT                   25
#define ETDM_IN2_RG_FORCE_NO_MASK_EXTRA_MASK                  0x1
#define ETDM_IN2_RG_FORCE_NO_MASK_EXTRA_MASK_SFT              (0x1 << 25)
#define ETDM_IN2_SW_CLEAR_BUF_FULL_SFT                        24
#define ETDM_IN2_SW_CLEAR_BUF_FULL_MASK                       0x1
#define ETDM_IN2_SW_CLEAR_BUF_FULL_MASK_SFT                   (0x1 << 24)
#define ETDM_IN2_ULTRA_TH_SFT                                 20
#define ETDM_IN2_ULTRA_TH_MASK                                0xf
#define ETDM_IN2_ULTRA_TH_MASK_SFT                            (0xf << 20)
#define ETDM_IN2_NORMAL_MODE_SFT                              17
#define ETDM_IN2_NORMAL_MODE_MASK                             0x1
#define ETDM_IN2_NORMAL_MODE_MASK_SFT                         (0x1 << 17)
#define ETDM_IN2_ODD_USE_EVEN_SFT                             16
#define ETDM_IN2_ODD_USE_EVEN_MASK                            0x1
#define ETDM_IN2_ODD_USE_EVEN_MASK_SFT                        (0x1 << 16)
#define ETDM_IN2_AXI_REQ_MAXLEN_SFT                           12
#define ETDM_IN2_AXI_REQ_MAXLEN_MASK                          0x3
#define ETDM_IN2_AXI_REQ_MAXLEN_MASK_SFT                      (0x3 << 12)
#define ETDM_IN2_AXI_REQ_MINLEN_SFT                           8
#define ETDM_IN2_AXI_REQ_MINLEN_MASK                          0x3
#define ETDM_IN2_AXI_REQ_MINLEN_MASK_SFT                      (0x3 << 8)
#define ETDM_IN2_HALIGN_SFT                                   7
#define ETDM_IN2_HALIGN_MASK                                  0x1
#define ETDM_IN2_HALIGN_MASK_SFT                              (0x1 << 7)
#define ETDM_IN2_SIGN_EXT_SFT                                 6
#define ETDM_IN2_SIGN_EXT_MASK                                0x1
#define ETDM_IN2_SIGN_EXT_MASK_SFT                            (0x1 << 6)
#define ETDM_IN2_HD_MODE_SFT                                  4
#define ETDM_IN2_HD_MODE_MASK                                 0x3
#define ETDM_IN2_HD_MODE_MASK_SFT                             (0x3 << 4)
#define ETDM_IN2_MAKE_EXTRA_UPDATE_SFT                        3
#define ETDM_IN2_MAKE_EXTRA_UPDATE_MASK                       0x1
#define ETDM_IN2_MAKE_EXTRA_UPDATE_MASK_SFT                   (0x1 << 3)
#define ETDM_IN2_AGENT_FREE_RUN_SFT                           2
#define ETDM_IN2_AGENT_FREE_RUN_MASK                          0x1
#define ETDM_IN2_AGENT_FREE_RUN_MASK_SFT                      (0x1 << 2)
#define ETDM_IN2_USE_INT_ODD_SFT                              1
#define ETDM_IN2_USE_INT_ODD_MASK                             0x1
#define ETDM_IN2_USE_INT_ODD_MASK_SFT                         (0x1 << 1)
#define ETDM_IN2_INT_ODD_FLAG_SFT                             0
#define ETDM_IN2_INT_ODD_FLAG_MASK                            0x1
#define ETDM_IN2_INT_ODD_FLAG_MASK_SFT                        (0x1 << 0)

/* AFE_ETDM_IN3_BASE_MSB */
#define ETDM_IN3_BASE_ADDR_MSB_SFT                            0
#define ETDM_IN3_BASE_ADDR_MSB_MASK                           0x1ff
#define ETDM_IN3_BASE_ADDR_MSB_MASK_SFT                       (0x1ff << 0)

/* AFE_ETDM_IN3_BASE */
#define ETDM_IN3_BASE_ADDR_SFT                                4
#define ETDM_IN3_BASE_ADDR_MASK                               0xfffffff
#define ETDM_IN3_BASE_ADDR_MASK_SFT                           (0xfffffff << 4)

/* AFE_ETDM_IN3_CUR_MSB */
#define ETDM_IN3_CUR_PTR_MSB_SFT                              0
#define ETDM_IN3_CUR_PTR_MSB_MASK                             0x1ff
#define ETDM_IN3_CUR_PTR_MSB_MASK_SFT                         (0x1ff << 0)

/* AFE_ETDM_IN3_CUR */
#define ETDM_IN3_CUR_PTR_SFT                                  0
#define ETDM_IN3_CUR_PTR_MASK                                 0xffffffff
#define ETDM_IN3_CUR_PTR_MASK_SFT                             (0xffffffff << 0)

/* AFE_ETDM_IN3_END_MSB */
#define ETDM_IN3_END_ADDR_MSB_SFT                             0
#define ETDM_IN3_END_ADDR_MSB_MASK                            0x1ff
#define ETDM_IN3_END_ADDR_MSB_MASK_SFT                        (0x1ff << 0)

/* AFE_ETDM_IN3_END */
#define ETDM_IN3_END_ADDR_SFT                                 4
#define ETDM_IN3_END_ADDR_MASK                                0xfffffff
#define ETDM_IN3_END_ADDR_MASK_SFT                            (0xfffffff << 4)

/* AFE_ETDM_IN3_CON0 */
#define ETDM_IN3_CH_NUM_SFT                                   28
#define ETDM_IN3_CH_NUM_MASK                                  0xf
#define ETDM_IN3_CH_NUM_MASK_SFT                              (0xf << 28)
#define ETDM_IN3_ON_SFT                                       27
#define ETDM_IN3_ON_MASK                                      0x1
#define ETDM_IN3_ON_MASK_SFT                                  (0x1 << 27)
#define ETDM_IN3_REG_CH_SHIFT_MODE_SFT                        26
#define ETDM_IN3_REG_CH_SHIFT_MODE_MASK                       0x1
#define ETDM_IN3_REG_CH_SHIFT_MODE_MASK_SFT                   (0x1 << 26)
#define ETDM_IN3_RG_FORCE_NO_MASK_EXTRA_SFT                   25
#define ETDM_IN3_RG_FORCE_NO_MASK_EXTRA_MASK                  0x1
#define ETDM_IN3_RG_FORCE_NO_MASK_EXTRA_MASK_SFT              (0x1 << 25)
#define ETDM_IN3_SW_CLEAR_BUF_FULL_SFT                        24
#define ETDM_IN3_SW_CLEAR_BUF_FULL_MASK                       0x1
#define ETDM_IN3_SW_CLEAR_BUF_FULL_MASK_SFT                   (0x1 << 24)
#define ETDM_IN3_ULTRA_TH_SFT                                 20
#define ETDM_IN3_ULTRA_TH_MASK                                0xf
#define ETDM_IN3_ULTRA_TH_MASK_SFT                            (0xf << 20)
#define ETDM_IN3_NORMAL_MODE_SFT                              17
#define ETDM_IN3_NORMAL_MODE_MASK                             0x1
#define ETDM_IN3_NORMAL_MODE_MASK_SFT                         (0x1 << 17)
#define ETDM_IN3_ODD_USE_EVEN_SFT                             16
#define ETDM_IN3_ODD_USE_EVEN_MASK                            0x1
#define ETDM_IN3_ODD_USE_EVEN_MASK_SFT                        (0x1 << 16)
#define ETDM_IN3_AXI_REQ_MAXLEN_SFT                           12
#define ETDM_IN3_AXI_REQ_MAXLEN_MASK                          0x3
#define ETDM_IN3_AXI_REQ_MAXLEN_MASK_SFT                      (0x3 << 12)
#define ETDM_IN3_AXI_REQ_MINLEN_SFT                           8
#define ETDM_IN3_AXI_REQ_MINLEN_MASK                          0x3
#define ETDM_IN3_AXI_REQ_MINLEN_MASK_SFT                      (0x3 << 8)
#define ETDM_IN3_HALIGN_SFT                                   7
#define ETDM_IN3_HALIGN_MASK                                  0x1
#define ETDM_IN3_HALIGN_MASK_SFT                              (0x1 << 7)
#define ETDM_IN3_SIGN_EXT_SFT                                 6
#define ETDM_IN3_SIGN_EXT_MASK                                0x1
#define ETDM_IN3_SIGN_EXT_MASK_SFT                            (0x1 << 6)
#define ETDM_IN3_HD_MODE_SFT                                  4
#define ETDM_IN3_HD_MODE_MASK                                 0x3
#define ETDM_IN3_HD_MODE_MASK_SFT                             (0x3 << 4)
#define ETDM_IN3_MAKE_EXTRA_UPDATE_SFT                        3
#define ETDM_IN3_MAKE_EXTRA_UPDATE_MASK                       0x1
#define ETDM_IN3_MAKE_EXTRA_UPDATE_MASK_SFT                   (0x1 << 3)
#define ETDM_IN3_AGENT_FREE_RUN_SFT                           2
#define ETDM_IN3_AGENT_FREE_RUN_MASK                          0x1
#define ETDM_IN3_AGENT_FREE_RUN_MASK_SFT                      (0x1 << 2)
#define ETDM_IN3_USE_INT_ODD_SFT                              1
#define ETDM_IN3_USE_INT_ODD_MASK                             0x1
#define ETDM_IN3_USE_INT_ODD_MASK_SFT                         (0x1 << 1)
#define ETDM_IN3_INT_ODD_FLAG_SFT                             0
#define ETDM_IN3_INT_ODD_FLAG_MASK                            0x1
#define ETDM_IN3_INT_ODD_FLAG_MASK_SFT                        (0x1 << 0)

/* AFE_ETDM_IN4_BASE_MSB */
#define ETDM_IN4_BASE_ADDR_MSB_SFT                            0
#define ETDM_IN4_BASE_ADDR_MSB_MASK                           0x1ff
#define ETDM_IN4_BASE_ADDR_MSB_MASK_SFT                       (0x1ff << 0)

/* AFE_ETDM_IN4_BASE */
#define ETDM_IN4_BASE_ADDR_SFT                                4
#define ETDM_IN4_BASE_ADDR_MASK                               0xfffffff
#define ETDM_IN4_BASE_ADDR_MASK_SFT                           (0xfffffff << 4)

/* AFE_ETDM_IN4_CUR_MSB */
#define ETDM_IN4_CUR_PTR_MSB_SFT                              0
#define ETDM_IN4_CUR_PTR_MSB_MASK                             0x1ff
#define ETDM_IN4_CUR_PTR_MSB_MASK_SFT                         (0x1ff << 0)

/* AFE_ETDM_IN4_CUR */
#define ETDM_IN4_CUR_PTR_SFT                                  0
#define ETDM_IN4_CUR_PTR_MASK                                 0xffffffff
#define ETDM_IN4_CUR_PTR_MASK_SFT                             (0xffffffff << 0)

/* AFE_ETDM_IN4_END_MSB */
#define ETDM_IN4_END_ADDR_MSB_SFT                             0
#define ETDM_IN4_END_ADDR_MSB_MASK                            0x1ff
#define ETDM_IN4_END_ADDR_MSB_MASK_SFT                        (0x1ff << 0)

/* AFE_ETDM_IN4_END */
#define ETDM_IN4_END_ADDR_SFT                                 4
#define ETDM_IN4_END_ADDR_MASK                                0xfffffff
#define ETDM_IN4_END_ADDR_MASK_SFT                            (0xfffffff << 4)

/* AFE_ETDM_IN4_CON0 */
#define ETDM_IN4_CH_NUM_SFT                                   28
#define ETDM_IN4_CH_NUM_MASK                                  0xf
#define ETDM_IN4_CH_NUM_MASK_SFT                              (0xf << 28)
#define ETDM_IN4_ON_SFT                                       27
#define ETDM_IN4_ON_MASK                                      0x1
#define ETDM_IN4_ON_MASK_SFT                                  (0x1 << 27)
#define ETDM_IN4_REG_CH_SHIFT_MODE_SFT                        26
#define ETDM_IN4_REG_CH_SHIFT_MODE_MASK                       0x1
#define ETDM_IN4_REG_CH_SHIFT_MODE_MASK_SFT                   (0x1 << 26)
#define ETDM_IN4_RG_FORCE_NO_MASK_EXTRA_SFT                   25
#define ETDM_IN4_RG_FORCE_NO_MASK_EXTRA_MASK                  0x1
#define ETDM_IN4_RG_FORCE_NO_MASK_EXTRA_MASK_SFT              (0x1 << 25)
#define ETDM_IN4_SW_CLEAR_BUF_FULL_SFT                        24
#define ETDM_IN4_SW_CLEAR_BUF_FULL_MASK                       0x1
#define ETDM_IN4_SW_CLEAR_BUF_FULL_MASK_SFT                   (0x1 << 24)
#define ETDM_IN4_ULTRA_TH_SFT                                 20
#define ETDM_IN4_ULTRA_TH_MASK                                0xf
#define ETDM_IN4_ULTRA_TH_MASK_SFT                            (0xf << 20)
#define ETDM_IN4_NORMAL_MODE_SFT                              17
#define ETDM_IN4_NORMAL_MODE_MASK                             0x1
#define ETDM_IN4_NORMAL_MODE_MASK_SFT                         (0x1 << 17)
#define ETDM_IN4_ODD_USE_EVEN_SFT                             16
#define ETDM_IN4_ODD_USE_EVEN_MASK                            0x1
#define ETDM_IN4_ODD_USE_EVEN_MASK_SFT                        (0x1 << 16)
#define ETDM_IN4_AXI_REQ_MAXLEN_SFT                           12
#define ETDM_IN4_AXI_REQ_MAXLEN_MASK                          0x3
#define ETDM_IN4_AXI_REQ_MAXLEN_MASK_SFT                      (0x3 << 12)
#define ETDM_IN4_AXI_REQ_MINLEN_SFT                           8
#define ETDM_IN4_AXI_REQ_MINLEN_MASK                          0x3
#define ETDM_IN4_AXI_REQ_MINLEN_MASK_SFT                      (0x3 << 8)
#define ETDM_IN4_HALIGN_SFT                                   7
#define ETDM_IN4_HALIGN_MASK                                  0x1
#define ETDM_IN4_HALIGN_MASK_SFT                              (0x1 << 7)
#define ETDM_IN4_SIGN_EXT_SFT                                 6
#define ETDM_IN4_SIGN_EXT_MASK                                0x1
#define ETDM_IN4_SIGN_EXT_MASK_SFT                            (0x1 << 6)
#define ETDM_IN4_HD_MODE_SFT                                  4
#define ETDM_IN4_HD_MODE_MASK                                 0x3
#define ETDM_IN4_HD_MODE_MASK_SFT                             (0x3 << 4)
#define ETDM_IN4_MAKE_EXTRA_UPDATE_SFT                        3
#define ETDM_IN4_MAKE_EXTRA_UPDATE_MASK                       0x1
#define ETDM_IN4_MAKE_EXTRA_UPDATE_MASK_SFT                   (0x1 << 3)
#define ETDM_IN4_AGENT_FREE_RUN_SFT                           2
#define ETDM_IN4_AGENT_FREE_RUN_MASK                          0x1
#define ETDM_IN4_AGENT_FREE_RUN_MASK_SFT                      (0x1 << 2)
#define ETDM_IN4_USE_INT_ODD_SFT                              1
#define ETDM_IN4_USE_INT_ODD_MASK                             0x1
#define ETDM_IN4_USE_INT_ODD_MASK_SFT                         (0x1 << 1)
#define ETDM_IN4_INT_ODD_FLAG_SFT                             0
#define ETDM_IN4_INT_ODD_FLAG_MASK                            0x1
#define ETDM_IN4_INT_ODD_FLAG_MASK_SFT                        (0x1 << 0)

/* AFE_ETDM_IN5_BASE_MSB */
#define ETDM_IN5_BASE_ADDR_MSB_SFT                            0
#define ETDM_IN5_BASE_ADDR_MSB_MASK                           0x1ff
#define ETDM_IN5_BASE_ADDR_MSB_MASK_SFT                       (0x1ff << 0)

/* AFE_ETDM_IN5_BASE */
#define ETDM_IN5_BASE_ADDR_SFT                                4
#define ETDM_IN5_BASE_ADDR_MASK                               0xfffffff
#define ETDM_IN5_BASE_ADDR_MASK_SFT                           (0xfffffff << 4)

/* AFE_ETDM_IN5_CUR_MSB */
#define ETDM_IN5_CUR_PTR_MSB_SFT                              0
#define ETDM_IN5_CUR_PTR_MSB_MASK                             0x1ff
#define ETDM_IN5_CUR_PTR_MSB_MASK_SFT                         (0x1ff << 0)

/* AFE_ETDM_IN5_CUR */
#define ETDM_IN5_CUR_PTR_SFT                                  0
#define ETDM_IN5_CUR_PTR_MASK                                 0xffffffff
#define ETDM_IN5_CUR_PTR_MASK_SFT                             (0xffffffff << 0)

/* AFE_ETDM_IN5_END_MSB */
#define ETDM_IN5_END_ADDR_MSB_SFT                             0
#define ETDM_IN5_END_ADDR_MSB_MASK                            0x1ff
#define ETDM_IN5_END_ADDR_MSB_MASK_SFT                        (0x1ff << 0)

/* AFE_ETDM_IN5_END */
#define ETDM_IN5_END_ADDR_SFT                                 4
#define ETDM_IN5_END_ADDR_MASK                                0xfffffff
#define ETDM_IN5_END_ADDR_MASK_SFT                            (0xfffffff << 4)

/* AFE_ETDM_IN5_CON0 */
#define ETDM_IN5_CH_NUM_SFT                                   28
#define ETDM_IN5_CH_NUM_MASK                                  0xf
#define ETDM_IN5_CH_NUM_MASK_SFT                              (0xf << 28)
#define ETDM_IN5_ON_SFT                                       27
#define ETDM_IN5_ON_MASK                                      0x1
#define ETDM_IN5_ON_MASK_SFT                                  (0x1 << 27)
#define ETDM_IN5_REG_CH_SHIFT_MODE_SFT                        26
#define ETDM_IN5_REG_CH_SHIFT_MODE_MASK                       0x1
#define ETDM_IN5_REG_CH_SHIFT_MODE_MASK_SFT                   (0x1 << 26)
#define ETDM_IN5_RG_FORCE_NO_MASK_EXTRA_SFT                   25
#define ETDM_IN5_RG_FORCE_NO_MASK_EXTRA_MASK                  0x1
#define ETDM_IN5_RG_FORCE_NO_MASK_EXTRA_MASK_SFT              (0x1 << 25)
#define ETDM_IN5_SW_CLEAR_BUF_FULL_SFT                        24
#define ETDM_IN5_SW_CLEAR_BUF_FULL_MASK                       0x1
#define ETDM_IN5_SW_CLEAR_BUF_FULL_MASK_SFT                   (0x1 << 24)
#define ETDM_IN5_ULTRA_TH_SFT                                 20
#define ETDM_IN5_ULTRA_TH_MASK                                0xf
#define ETDM_IN5_ULTRA_TH_MASK_SFT                            (0xf << 20)
#define ETDM_IN5_NORMAL_MODE_SFT                              17
#define ETDM_IN5_NORMAL_MODE_MASK                             0x1
#define ETDM_IN5_NORMAL_MODE_MASK_SFT                         (0x1 << 17)
#define ETDM_IN5_ODD_USE_EVEN_SFT                             16
#define ETDM_IN5_ODD_USE_EVEN_MASK                            0x1
#define ETDM_IN5_ODD_USE_EVEN_MASK_SFT                        (0x1 << 16)
#define ETDM_IN5_AXI_REQ_MAXLEN_SFT                           12
#define ETDM_IN5_AXI_REQ_MAXLEN_MASK                          0x3
#define ETDM_IN5_AXI_REQ_MAXLEN_MASK_SFT                      (0x3 << 12)
#define ETDM_IN5_AXI_REQ_MINLEN_SFT                           8
#define ETDM_IN5_AXI_REQ_MINLEN_MASK                          0x3
#define ETDM_IN5_AXI_REQ_MINLEN_MASK_SFT                      (0x3 << 8)
#define ETDM_IN5_HALIGN_SFT                                   7
#define ETDM_IN5_HALIGN_MASK                                  0x1
#define ETDM_IN5_HALIGN_MASK_SFT                              (0x1 << 7)
#define ETDM_IN5_SIGN_EXT_SFT                                 6
#define ETDM_IN5_SIGN_EXT_MASK                                0x1
#define ETDM_IN5_SIGN_EXT_MASK_SFT                            (0x1 << 6)
#define ETDM_IN5_HD_MODE_SFT                                  4
#define ETDM_IN5_HD_MODE_MASK                                 0x3
#define ETDM_IN5_HD_MODE_MASK_SFT                             (0x3 << 4)
#define ETDM_IN5_MAKE_EXTRA_UPDATE_SFT                        3
#define ETDM_IN5_MAKE_EXTRA_UPDATE_MASK                       0x1
#define ETDM_IN5_MAKE_EXTRA_UPDATE_MASK_SFT                   (0x1 << 3)
#define ETDM_IN5_AGENT_FREE_RUN_SFT                           2
#define ETDM_IN5_AGENT_FREE_RUN_MASK                          0x1
#define ETDM_IN5_AGENT_FREE_RUN_MASK_SFT                      (0x1 << 2)
#define ETDM_IN5_USE_INT_ODD_SFT                              1
#define ETDM_IN5_USE_INT_ODD_MASK                             0x1
#define ETDM_IN5_USE_INT_ODD_MASK_SFT                         (0x1 << 1)
#define ETDM_IN5_INT_ODD_FLAG_SFT                             0
#define ETDM_IN5_INT_ODD_FLAG_MASK                            0x1
#define ETDM_IN5_INT_ODD_FLAG_MASK_SFT                        (0x1 << 0)

/* AFE_ETDM_IN6_BASE_MSB */
#define ETDM_IN6_BASE_ADDR_MSB_SFT                            0
#define ETDM_IN6_BASE_ADDR_MSB_MASK                           0x1ff
#define ETDM_IN6_BASE_ADDR_MSB_MASK_SFT                       (0x1ff << 0)

/* AFE_ETDM_IN6_BASE */
#define ETDM_IN6_BASE_ADDR_SFT                                4
#define ETDM_IN6_BASE_ADDR_MASK                               0xfffffff
#define ETDM_IN6_BASE_ADDR_MASK_SFT                           (0xfffffff << 4)

/* AFE_ETDM_IN6_CUR_MSB */
#define ETDM_IN6_CUR_PTR_MSB_SFT                              0
#define ETDM_IN6_CUR_PTR_MSB_MASK                             0x1ff
#define ETDM_IN6_CUR_PTR_MSB_MASK_SFT                         (0x1ff << 0)

/* AFE_ETDM_IN6_CUR */
#define ETDM_IN6_CUR_PTR_SFT                                  0
#define ETDM_IN6_CUR_PTR_MASK                                 0xffffffff
#define ETDM_IN6_CUR_PTR_MASK_SFT                             (0xffffffff << 0)

/* AFE_ETDM_IN6_END_MSB */
#define ETDM_IN6_END_ADDR_MSB_SFT                             0
#define ETDM_IN6_END_ADDR_MSB_MASK                            0x1ff
#define ETDM_IN6_END_ADDR_MSB_MASK_SFT                        (0x1ff << 0)

/* AFE_ETDM_IN6_END */
#define ETDM_IN6_END_ADDR_SFT                                 4
#define ETDM_IN6_END_ADDR_MASK                                0xfffffff
#define ETDM_IN6_END_ADDR_MASK_SFT                            (0xfffffff << 4)

/* AFE_ETDM_IN6_CON0 */
#define ETDM_IN6_CH_NUM_SFT                                   28
#define ETDM_IN6_CH_NUM_MASK                                  0xf
#define ETDM_IN6_CH_NUM_MASK_SFT                              (0xf << 28)
#define ETDM_IN6_ON_SFT                                       27
#define ETDM_IN6_ON_MASK                                      0x1
#define ETDM_IN6_ON_MASK_SFT                                  (0x1 << 27)
#define ETDM_IN6_REG_CH_SHIFT_MODE_SFT                        26
#define ETDM_IN6_REG_CH_SHIFT_MODE_MASK                       0x1
#define ETDM_IN6_REG_CH_SHIFT_MODE_MASK_SFT                   (0x1 << 26)
#define ETDM_IN6_RG_FORCE_NO_MASK_EXTRA_SFT                   25
#define ETDM_IN6_RG_FORCE_NO_MASK_EXTRA_MASK                  0x1
#define ETDM_IN6_RG_FORCE_NO_MASK_EXTRA_MASK_SFT              (0x1 << 25)
#define ETDM_IN6_SW_CLEAR_BUF_FULL_SFT                        24
#define ETDM_IN6_SW_CLEAR_BUF_FULL_MASK                       0x1
#define ETDM_IN6_SW_CLEAR_BUF_FULL_MASK_SFT                   (0x1 << 24)
#define ETDM_IN6_ULTRA_TH_SFT                                 20
#define ETDM_IN6_ULTRA_TH_MASK                                0xf
#define ETDM_IN6_ULTRA_TH_MASK_SFT                            (0xf << 20)
#define ETDM_IN6_NORMAL_MODE_SFT                              17
#define ETDM_IN6_NORMAL_MODE_MASK                             0x1
#define ETDM_IN6_NORMAL_MODE_MASK_SFT                         (0x1 << 17)
#define ETDM_IN6_ODD_USE_EVEN_SFT                             16
#define ETDM_IN6_ODD_USE_EVEN_MASK                            0x1
#define ETDM_IN6_ODD_USE_EVEN_MASK_SFT                        (0x1 << 16)
#define ETDM_IN6_AXI_REQ_MAXLEN_SFT                           12
#define ETDM_IN6_AXI_REQ_MAXLEN_MASK                          0x3
#define ETDM_IN6_AXI_REQ_MAXLEN_MASK_SFT                      (0x3 << 12)
#define ETDM_IN6_AXI_REQ_MINLEN_SFT                           8
#define ETDM_IN6_AXI_REQ_MINLEN_MASK                          0x3
#define ETDM_IN6_AXI_REQ_MINLEN_MASK_SFT                      (0x3 << 8)
#define ETDM_IN6_HALIGN_SFT                                   7
#define ETDM_IN6_HALIGN_MASK                                  0x1
#define ETDM_IN6_HALIGN_MASK_SFT                              (0x1 << 7)
#define ETDM_IN6_SIGN_EXT_SFT                                 6
#define ETDM_IN6_SIGN_EXT_MASK                                0x1
#define ETDM_IN6_SIGN_EXT_MASK_SFT                            (0x1 << 6)
#define ETDM_IN6_HD_MODE_SFT                                  4
#define ETDM_IN6_HD_MODE_MASK                                 0x3
#define ETDM_IN6_HD_MODE_MASK_SFT                             (0x3 << 4)
#define ETDM_IN6_MAKE_EXTRA_UPDATE_SFT                        3
#define ETDM_IN6_MAKE_EXTRA_UPDATE_MASK                       0x1
#define ETDM_IN6_MAKE_EXTRA_UPDATE_MASK_SFT                   (0x1 << 3)
#define ETDM_IN6_AGENT_FREE_RUN_SFT                           2
#define ETDM_IN6_AGENT_FREE_RUN_MASK                          0x1
#define ETDM_IN6_AGENT_FREE_RUN_MASK_SFT                      (0x1 << 2)
#define ETDM_IN6_USE_INT_ODD_SFT                              1
#define ETDM_IN6_USE_INT_ODD_MASK                             0x1
#define ETDM_IN6_USE_INT_ODD_MASK_SFT                         (0x1 << 1)
#define ETDM_IN6_INT_ODD_FLAG_SFT                             0
#define ETDM_IN6_INT_ODD_FLAG_MASK                            0x1
#define ETDM_IN6_INT_ODD_FLAG_MASK_SFT                        (0x1 << 0)

/* AFE_HDMI_OUT_BASE_MSB */
#define AFE_HDMI_OUT_BASE_MSB_SFT                             0
#define AFE_HDMI_OUT_BASE_MSB_MASK                            0x1ff
#define AFE_HDMI_OUT_BASE_MSB_MASK_SFT                        (0x1ff << 0)

/* AFE_HDMI_OUT_BASE */
#define AFE_HDMI_OUT_BASE_SFT                                 4
#define AFE_HDMI_OUT_BASE_MASK                                0xfffffff
#define AFE_HDMI_OUT_BASE_MASK_SFT                            (0xfffffff << 4)

/* AFE_HDMI_OUT_CUR_MSB */
#define AFE_HDMI_OUT_CUR_MSB_SFT                              0
#define AFE_HDMI_OUT_CUR_MSB_MASK                             0x1ff
#define AFE_HDMI_OUT_CUR_MSB_MASK_SFT                         (0x1ff << 0)

/* AFE_HDMI_OUT_CUR */
#define AFE_HDMI_OUT_CUR_SFT                                  0
#define AFE_HDMI_OUT_CUR_MASK                                 0xffffffff
#define AFE_HDMI_OUT_CUR_MASK_SFT                             (0xffffffff << 0)

/* AFE_HDMI_OUT_END_MSB */
#define AFE_HDMI_OUT_END_MSB_SFT                              0
#define AFE_HDMI_OUT_END_MSB_MASK                             0x1ff
#define AFE_HDMI_OUT_END_MSB_MASK_SFT                         (0x1ff << 0)

/* AFE_HDMI_OUT_END */
#define AFE_HDMI_OUT_END_SFT                                  4
#define AFE_HDMI_OUT_END_MASK                                 0xfffffff
#define AFE_HDMI_OUT_END_MASK_SFT                             (0xfffffff << 4)
#define AFE_HDMI_OUT_END_LSB_SFT                              0
#define AFE_HDMI_OUT_END_LSB_MASK                             0xf
#define AFE_HDMI_OUT_END_LSB_MASK_SFT                         (0xf << 0)

/* AFE_HDMI_OUT_CON0 */
#define HDMI_OUT_ON_SFT                                       28
#define HDMI_OUT_ON_MASK                                      0x1
#define HDMI_OUT_ON_MASK_SFT                                  (0x1 << 28)
#define HDMI_CH_NUM_SFT                                       24
#define HDMI_CH_NUM_MASK                                      0xf
#define HDMI_CH_NUM_MASK_SFT                                  (0xf << 24)
#define HDMI_OUT_ONE_HEART_SEL_SFT                            22
#define HDMI_OUT_ONE_HEART_SEL_MASK                           0x3
#define HDMI_OUT_ONE_HEART_SEL_MASK_SFT                       (0x3 << 22)
#define HDMI_OUT_MINLEN_SFT                                   20
#define HDMI_OUT_MINLEN_MASK                                  0x3
#define HDMI_OUT_MINLEN_MASK_SFT                              (0x3 << 20)
#define HDMI_OUT_MAXLEN_SFT                                   16
#define HDMI_OUT_MAXLEN_MASK                                  0x3
#define HDMI_OUT_MAXLEN_MASK_SFT                              (0x3 << 16)
#define HDMI_OUT_SW_CLEAR_BUF_EMPTY_SFT                       15
#define HDMI_OUT_SW_CLEAR_BUF_EMPTY_MASK                      0x1
#define HDMI_OUT_SW_CLEAR_BUF_EMPTY_MASK_SFT                  (0x1 << 15)
#define HDMI_OUT_PBUF_SIZE_SFT                                12
#define HDMI_OUT_PBUF_SIZE_MASK                               0x3
#define HDMI_OUT_PBUF_SIZE_MASK_SFT                           (0x3 << 12)
#define HDMI_OUT_SW_CLEAR_HDMI_BUF_EMPTY_SFT                  7
#define HDMI_OUT_SW_CLEAR_HDMI_BUF_EMPTY_MASK                 0x1
#define HDMI_OUT_SW_CLEAR_HDMI_BUF_EMPTY_MASK_SFT             (0x1 << 7)
#define HDMI_OUT_NORMAL_MODE_SFT                              5
#define HDMI_OUT_NORMAL_MODE_MASK                             0x1
#define HDMI_OUT_NORMAL_MODE_MASK_SFT                         (0x1 << 5)
#define HDMI_OUT_HALIGN_SFT                                   4
#define HDMI_OUT_HALIGN_MASK                                  0x1
#define HDMI_OUT_HALIGN_MASK_SFT                              (0x1 << 4)
#define HDMI_OUT_HD_MODE_SFT                                  0
#define HDMI_OUT_HD_MODE_MASK                                 0x3
#define HDMI_OUT_HD_MODE_MASK_SFT                             (0x3 << 0)

/* AFE_VUL24_RCH_MON */
#define VUL24_RCH_DATA_SFT                                    0
#define VUL24_RCH_DATA_MASK                                   0xffffffff
#define VUL24_RCH_DATA_MASK_SFT                               (0xffffffff << 0)

/* AFE_VUL24_LCH_MON */
#define VUL24_LCH_DATA_SFT                                    0
#define VUL24_LCH_DATA_MASK                                   0xffffffff
#define VUL24_LCH_DATA_MASK_SFT                               (0xffffffff << 0)

/* AFE_VUL25_RCH_MON */
#define VUL25_RCH_DATA_SFT                                    0
#define VUL25_RCH_DATA_MASK                                   0xffffffff
#define VUL25_RCH_DATA_MASK_SFT                               (0xffffffff << 0)

/* AFE_VUL25_LCH_MON */
#define VUL25_LCH_DATA_SFT                                    0
#define VUL25_LCH_DATA_MASK                                   0xffffffff
#define VUL25_LCH_DATA_MASK_SFT                               (0xffffffff << 0)

/* AFE_VUL26_RCH_MON */
#define VUL26_RCH_DATA_SFT                                    0
#define VUL26_RCH_DATA_MASK                                   0xffffffff
#define VUL26_RCH_DATA_MASK_SFT                               (0xffffffff << 0)

/* AFE_VUL26_LCH_MON */
#define VUL26_LCH_DATA_SFT                                    0
#define VUL26_LCH_DATA_MASK                                   0xffffffff
#define VUL26_LCH_DATA_MASK_SFT                               (0xffffffff << 0)

/* AFE_VUL_CM0_RCH_MON */
#define VUL_CM0_RCH_DATA_SFT                                  0
#define VUL_CM0_RCH_DATA_MASK                                 0xffffffff
#define VUL_CM0_RCH_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_VUL_CM0_LCH_MON */
#define VUL_CM0_LCH_DATA_SFT                                  0
#define VUL_CM0_LCH_DATA_MASK                                 0xffffffff
#define VUL_CM0_LCH_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_VUL_CM1_RCH_MON */
#define VUL_CM1_RCH_DATA_SFT                                  0
#define VUL_CM1_RCH_DATA_MASK                                 0xffffffff
#define VUL_CM1_RCH_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_VUL_CM1_LCH_MON */
#define VUL_CM1_LCH_DATA_SFT                                  0
#define VUL_CM1_LCH_DATA_MASK                                 0xffffffff
#define VUL_CM1_LCH_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_VUL_CM2_RCH_MON */
#define VUL_CM2_RCH_DATA_SFT                                  0
#define VUL_CM2_RCH_DATA_MASK                                 0xffffffff
#define VUL_CM2_RCH_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_VUL_CM2_LCH_MON */
#define VUL_CM2_LCH_DATA_SFT                                  0
#define VUL_CM2_LCH_DATA_MASK                                 0xffffffff
#define VUL_CM2_LCH_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_DL_4CH_CH0_MON */
#define DL_4CH_CH0_DATA_SFT                                   0
#define DL_4CH_CH0_DATA_MASK                                  0xffffffff
#define DL_4CH_CH0_DATA_MASK_SFT                              (0xffffffff << 0)

/* AFE_DL_4CH_CH1_MON */
#define DL_4CH_CH1_DATA_SFT                                   0
#define DL_4CH_CH1_DATA_MASK                                  0xffffffff
#define DL_4CH_CH1_DATA_MASK_SFT                              (0xffffffff << 0)

/* AFE_DL_4CH_CH2_MON */
#define DL_4CH_CH2_DATA_SFT                                   0
#define DL_4CH_CH2_DATA_MASK                                  0xffffffff
#define DL_4CH_CH2_DATA_MASK_SFT                              (0xffffffff << 0)

/* AFE_DL_4CH_CH3_MON */
#define DL_4CH_CH3_DATA_SFT                                   0
#define DL_4CH_CH3_DATA_MASK                                  0xffffffff
#define DL_4CH_CH3_DATA_MASK_SFT                              (0xffffffff << 0)

/* AFE_DL_24CH_CH0_MON */
#define DL_24CH_CH0_DATA_SFT                                  0
#define DL_24CH_CH0_DATA_MASK                                 0xffffffff
#define DL_24CH_CH0_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_DL_24CH_CH1_MON */
#define DL_24CH_CH1_DATA_SFT                                  0
#define DL_24CH_CH1_DATA_MASK                                 0xffffffff
#define DL_24CH_CH1_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_DL_24CH_CH2_MON */
#define DL_24CH_CH2_DATA_SFT                                  0
#define DL_24CH_CH2_DATA_MASK                                 0xffffffff
#define DL_24CH_CH2_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_DL_24CH_CH3_MON */
#define DL_24CH_CH3_DATA_SFT                                  0
#define DL_24CH_CH3_DATA_MASK                                 0xffffffff
#define DL_24CH_CH3_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_DL_24CH_CH4_MON */
#define DL_24CH_CH4_DATA_SFT                                  0
#define DL_24CH_CH4_DATA_MASK                                 0xffffffff
#define DL_24CH_CH4_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_DL_24CH_CH5_MON */
#define DL_24CH_CH5_DATA_SFT                                  0
#define DL_24CH_CH5_DATA_MASK                                 0xffffffff
#define DL_24CH_CH5_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_DL_24CH_CH6_MON */
#define DL_24CH_CH6_DATA_SFT                                  0
#define DL_24CH_CH6_DATA_MASK                                 0xffffffff
#define DL_24CH_CH6_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_DL_24CH_CH7_MON */
#define DL_24CH_CH7_DATA_SFT                                  0
#define DL_24CH_CH7_DATA_MASK                                 0xffffffff
#define DL_24CH_CH7_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_DL_24CH_CH8_MON */
#define DL_24CH_CH8_DATA_SFT                                  0
#define DL_24CH_CH8_DATA_MASK                                 0xffffffff
#define DL_24CH_CH8_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_DL_24CH_CH9_MON */
#define DL_24CH_CH9_DATA_SFT                                  0
#define DL_24CH_CH9_DATA_MASK                                 0xffffffff
#define DL_24CH_CH9_DATA_MASK_SFT                             (0xffffffff << 0)

/* AFE_DL_24CH_CH10_MON */
#define DL_24CH_CH10_DATA_SFT                                 0
#define DL_24CH_CH10_DATA_MASK                                0xffffffff
#define DL_24CH_CH10_DATA_MASK_SFT                            (0xffffffff << 0)

/* AFE_DL_24CH_CH11_MON */
#define DL_24CH_CH11_DATA_SFT                                 0
#define DL_24CH_CH11_DATA_MASK                                0xffffffff
#define DL_24CH_CH11_DATA_MASK_SFT                            (0xffffffff << 0)

/* AFE_DL_24CH_CH12_MON */
#define DL_24CH_CH12_DATA_SFT                                 0
#define DL_24CH_CH12_DATA_MASK                                0xffffffff
#define DL_24CH_CH12_DATA_MASK_SFT                            (0xffffffff << 0)

/* AFE_DL_24CH_CH13_MON */
#define DL_24CH_CH13_DATA_SFT                                 0
#define DL_24CH_CH13_DATA_MASK                                0xffffffff
#define DL_24CH_CH13_DATA_MASK_SFT                            (0xffffffff << 0)

/* AFE_DL_24CH_CH14_MON */
#define DL_24CH_CH14_DATA_SFT                                 0
#define DL_24CH_CH14_DATA_MASK                                0xffffffff
#define DL_24CH_CH14_DATA_MASK_SFT                            (0xffffffff << 0)

/* AFE_DL_24CH_CH15_MON */
#define DL_24CH_CH15_DATA_SFT                                 0
#define DL_24CH_CH15_DATA_MASK                                0xffffffff
#define DL_24CH_CH15_DATA_MASK_SFT                            (0xffffffff << 0)

/* AFE_SRAM_BOUND */
#define SECURE_BIT_SFT                                        19
#define SECURE_BIT_MASK                                       0x1
#define SECURE_BIT_MASK_SFT                                   (0x1 << 19)
#define SECURE_SRAM_BOUND_SFT                                 0
#define SECURE_SRAM_BOUND_MASK                                0x7ffff
#define SECURE_SRAM_BOUND_MASK_SFT                            (0x7ffff << 0)

/* AFE_SECURE_CON0 */
#define READ_EN15_NS_SFT                                      31
#define READ_EN15_NS_MASK                                     0x1
#define READ_EN15_NS_MASK_SFT                                 (0x1 << 31)
#define WRITE_EN15_NS_SFT                                     30
#define WRITE_EN15_NS_MASK                                    0x1
#define WRITE_EN15_NS_MASK_SFT                                (0x1 << 30)
#define READ_EN14_NS_SFT                                      29
#define READ_EN14_NS_MASK                                     0x1
#define READ_EN14_NS_MASK_SFT                                 (0x1 << 29)
#define WRITE_EN14_NS_SFT                                     28
#define WRITE_EN14_NS_MASK                                    0x1
#define WRITE_EN14_NS_MASK_SFT                                (0x1 << 28)
#define READ_EN13_NS_SFT                                      27
#define READ_EN13_NS_MASK                                     0x1
#define READ_EN13_NS_MASK_SFT                                 (0x1 << 27)
#define WRITE_EN13_NS_SFT                                     26
#define WRITE_EN13_NS_MASK                                    0x1
#define WRITE_EN13_NS_MASK_SFT                                (0x1 << 26)
#define READ_EN12_NS_SFT                                      25
#define READ_EN12_NS_MASK                                     0x1
#define READ_EN12_NS_MASK_SFT                                 (0x1 << 25)
#define WRITE_EN12_NS_SFT                                     24
#define WRITE_EN12_NS_MASK                                    0x1
#define WRITE_EN12_NS_MASK_SFT                                (0x1 << 24)
#define READ_EN11_NS_SFT                                      23
#define READ_EN11_NS_MASK                                     0x1
#define READ_EN11_NS_MASK_SFT                                 (0x1 << 23)
#define WRITE_EN11_NS_SFT                                     22
#define WRITE_EN11_NS_MASK                                    0x1
#define WRITE_EN11_NS_MASK_SFT                                (0x1 << 22)
#define READ_EN10_NS_SFT                                      21
#define READ_EN10_NS_MASK                                     0x1
#define READ_EN10_NS_MASK_SFT                                 (0x1 << 21)
#define WRITE_EN10_NS_SFT                                     20
#define WRITE_EN10_NS_MASK                                    0x1
#define WRITE_EN10_NS_MASK_SFT                                (0x1 << 20)
#define READ_EN9_NS_SFT                                       19
#define READ_EN9_NS_MASK                                      0x1
#define READ_EN9_NS_MASK_SFT                                  (0x1 << 19)
#define WRITE_EN9_NS_SFT                                      18
#define WRITE_EN9_NS_MASK                                     0x1
#define WRITE_EN9_NS_MASK_SFT                                 (0x1 << 18)
#define READ_EN8_NS_SFT                                       17
#define READ_EN8_NS_MASK                                      0x1
#define READ_EN8_NS_MASK_SFT                                  (0x1 << 17)
#define WRITE_EN8_NS_SFT                                      16
#define WRITE_EN8_NS_MASK                                     0x1
#define WRITE_EN8_NS_MASK_SFT                                 (0x1 << 16)
#define READ_EN7_NS_SFT                                       15
#define READ_EN7_NS_MASK                                      0x1
#define READ_EN7_NS_MASK_SFT                                  (0x1 << 15)
#define WRITE_EN7_NS_SFT                                      14
#define WRITE_EN7_NS_MASK                                     0x1
#define WRITE_EN7_NS_MASK_SFT                                 (0x1 << 14)
#define READ_EN6_NS_SFT                                       13
#define READ_EN6_NS_MASK                                      0x1
#define READ_EN6_NS_MASK_SFT                                  (0x1 << 13)
#define WRITE_EN6_NS_SFT                                      12
#define WRITE_EN6_NS_MASK                                     0x1
#define WRITE_EN6_NS_MASK_SFT                                 (0x1 << 12)
#define READ_EN5_NS_SFT                                       11
#define READ_EN5_NS_MASK                                      0x1
#define READ_EN5_NS_MASK_SFT                                  (0x1 << 11)
#define WRITE_EN5_NS_SFT                                      10
#define WRITE_EN5_NS_MASK                                     0x1
#define WRITE_EN5_NS_MASK_SFT                                 (0x1 << 10)
#define READ_EN4_NS_SFT                                       9
#define READ_EN4_NS_MASK                                      0x1
#define READ_EN4_NS_MASK_SFT                                  (0x1 << 9)
#define WRITE_EN4_NS_SFT                                      8
#define WRITE_EN4_NS_MASK                                     0x1
#define WRITE_EN4_NS_MASK_SFT                                 (0x1 << 8)
#define READ_EN3_NS_SFT                                       7
#define READ_EN3_NS_MASK                                      0x1
#define READ_EN3_NS_MASK_SFT                                  (0x1 << 7)
#define WRITE_EN3_NS_SFT                                      6
#define WRITE_EN3_NS_MASK                                     0x1
#define WRITE_EN3_NS_MASK_SFT                                 (0x1 << 6)
#define READ_EN2_NS_SFT                                       5
#define READ_EN2_NS_MASK                                      0x1
#define READ_EN2_NS_MASK_SFT                                  (0x1 << 5)
#define WRITE_EN2_NS_SFT                                      4
#define WRITE_EN2_NS_MASK                                     0x1
#define WRITE_EN2_NS_MASK_SFT                                 (0x1 << 4)
#define READ_EN1_NS_SFT                                       3
#define READ_EN1_NS_MASK                                      0x1
#define READ_EN1_NS_MASK_SFT                                  (0x1 << 3)
#define WRITE_EN1_NS_SFT                                      2
#define WRITE_EN1_NS_MASK                                     0x1
#define WRITE_EN1_NS_MASK_SFT                                 (0x1 << 2)
#define READ_EN0_NS_SFT                                       1
#define READ_EN0_NS_MASK                                      0x1
#define READ_EN0_NS_MASK_SFT                                  (0x1 << 1)
#define WRITE_EN0_NS_SFT                                      0
#define WRITE_EN0_NS_MASK                                     0x1
#define WRITE_EN0_NS_MASK_SFT                                 (0x1 << 0)

/* AFE_SECURE_CON1 */
#define READ_EN15_S_SFT                                       31
#define READ_EN15_S_MASK                                      0x1
#define READ_EN15_S_MASK_SFT                                  (0x1 << 31)
#define WRITE_EN15_S_SFT                                      30
#define WRITE_EN15_S_MASK                                     0x1
#define WRITE_EN15_S_MASK_SFT                                 (0x1 << 30)
#define READ_EN14_S_SFT                                       29
#define READ_EN14_S_MASK                                      0x1
#define READ_EN14_S_MASK_SFT                                  (0x1 << 29)
#define WRITE_EN14_S_SFT                                      28
#define WRITE_EN14_S_MASK                                     0x1
#define WRITE_EN14_S_MASK_SFT                                 (0x1 << 28)
#define READ_EN13_S_SFT                                       27
#define READ_EN13_S_MASK                                      0x1
#define READ_EN13_S_MASK_SFT                                  (0x1 << 27)
#define WRITE_EN13_S_SFT                                      26
#define WRITE_EN13_S_MASK                                     0x1
#define WRITE_EN13_S_MASK_SFT                                 (0x1 << 26)
#define READ_EN12_S_SFT                                       25
#define READ_EN12_S_MASK                                      0x1
#define READ_EN12_S_MASK_SFT                                  (0x1 << 25)
#define WRITE_EN12_S_SFT                                      24
#define WRITE_EN12_S_MASK                                     0x1
#define WRITE_EN12_S_MASK_SFT                                 (0x1 << 24)
#define READ_EN11_S_SFT                                       23
#define READ_EN11_S_MASK                                      0x1
#define READ_EN11_S_MASK_SFT                                  (0x1 << 23)
#define WRITE_EN11_S_SFT                                      22
#define WRITE_EN11_S_MASK                                     0x1
#define WRITE_EN11_S_MASK_SFT                                 (0x1 << 22)
#define READ_EN10_S_SFT                                       21
#define READ_EN10_S_MASK                                      0x1
#define READ_EN10_S_MASK_SFT                                  (0x1 << 21)
#define WRITE_EN10_S_SFT                                      20
#define WRITE_EN10_S_MASK                                     0x1
#define WRITE_EN10_S_MASK_SFT                                 (0x1 << 20)
#define READ_EN9_S_SFT                                        19
#define READ_EN9_S_MASK                                       0x1
#define READ_EN9_S_MASK_SFT                                   (0x1 << 19)
#define WRITE_EN9_S_SFT                                       18
#define WRITE_EN9_S_MASK                                      0x1
#define WRITE_EN9_S_MASK_SFT                                  (0x1 << 18)
#define READ_EN8_S_SFT                                        17
#define READ_EN8_S_MASK                                       0x1
#define READ_EN8_S_MASK_SFT                                   (0x1 << 17)
#define WRITE_EN8_S_SFT                                       16
#define WRITE_EN8_S_MASK                                      0x1
#define WRITE_EN8_S_MASK_SFT                                  (0x1 << 16)
#define READ_EN7_S_SFT                                        15
#define READ_EN7_S_MASK                                       0x1
#define READ_EN7_S_MASK_SFT                                   (0x1 << 15)
#define WRITE_EN7_S_SFT                                       14
#define WRITE_EN7_S_MASK                                      0x1
#define WRITE_EN7_S_MASK_SFT                                  (0x1 << 14)
#define READ_EN6_S_SFT                                        13
#define READ_EN6_S_MASK                                       0x1
#define READ_EN6_S_MASK_SFT                                   (0x1 << 13)
#define WRITE_EN6_S_SFT                                       12
#define WRITE_EN6_S_MASK                                      0x1
#define WRITE_EN6_S_MASK_SFT                                  (0x1 << 12)
#define READ_EN5_S_SFT                                        11
#define READ_EN5_S_MASK                                       0x1
#define READ_EN5_S_MASK_SFT                                   (0x1 << 11)
#define WRITE_EN5_S_SFT                                       10
#define WRITE_EN5_S_MASK                                      0x1
#define WRITE_EN5_S_MASK_SFT                                  (0x1 << 10)
#define READ_EN4_S_SFT                                        9
#define READ_EN4_S_MASK                                       0x1
#define READ_EN4_S_MASK_SFT                                   (0x1 << 9)
#define WRITE_EN4_S_SFT                                       8
#define WRITE_EN4_S_MASK                                      0x1
#define WRITE_EN4_S_MASK_SFT                                  (0x1 << 8)
#define READ_EN3_S_SFT                                        7
#define READ_EN3_S_MASK                                       0x1
#define READ_EN3_S_MASK_SFT                                   (0x1 << 7)
#define WRITE_EN3_S_SFT                                       6
#define WRITE_EN3_S_MASK                                      0x1
#define WRITE_EN3_S_MASK_SFT                                  (0x1 << 6)
#define READ_EN2_S_SFT                                        5
#define READ_EN2_S_MASK                                       0x1
#define READ_EN2_S_MASK_SFT                                   (0x1 << 5)
#define WRITE_EN2_S_SFT                                       4
#define WRITE_EN2_S_MASK                                      0x1
#define WRITE_EN2_S_MASK_SFT                                  (0x1 << 4)
#define READ_EN1_S_SFT                                        3
#define READ_EN1_S_MASK                                       0x1
#define READ_EN1_S_MASK_SFT                                   (0x1 << 3)
#define WRITE_EN1_S_SFT                                       2
#define WRITE_EN1_S_MASK                                      0x1
#define WRITE_EN1_S_MASK_SFT                                  (0x1 << 2)
#define READ_EN0_S_SFT                                        1
#define READ_EN0_S_MASK                                       0x1
#define READ_EN0_S_MASK_SFT                                   (0x1 << 1)
#define WRITE_EN0_S_SFT                                       0
#define WRITE_EN0_S_MASK                                      0x1
#define WRITE_EN0_S_MASK_SFT                                  (0x1 << 0)

/* AFE_SE_SECURE_CON0 */
#define AFE_HDMI_SE_SECURE_BIT_SFT                            11
#define AFE_HDMI_SE_SECURE_BIT_MASK                           0x1
#define AFE_HDMI_SE_SECURE_BIT_MASK_SFT                       (0x1 << 11)
#define AFE_SPDIF2_OUT_SE_SECURE_BIT_SFT                      10
#define AFE_SPDIF2_OUT_SE_SECURE_BIT_MASK                     0x1
#define AFE_SPDIF2_OUT_SE_SECURE_BIT_MASK_SFT                 (0x1 << 10)
#define AFE_SPDIF_OUT_SE_SECURE_BIT_SFT                       9
#define AFE_SPDIF_OUT_SE_SECURE_BIT_MASK                      0x1
#define AFE_SPDIF_OUT_SE_SECURE_BIT_MASK_SFT                  (0x1 << 9)
#define AFE_DL8_SE_SECURE_BIT_SFT                             8
#define AFE_DL8_SE_SECURE_BIT_MASK                            0x1
#define AFE_DL8_SE_SECURE_BIT_MASK_SFT                        (0x1 << 8)
#define AFE_DL7_SE_SECURE_BIT_SFT                             7
#define AFE_DL7_SE_SECURE_BIT_MASK                            0x1
#define AFE_DL7_SE_SECURE_BIT_MASK_SFT                        (0x1 << 7)
#define AFE_DL6_SE_SECURE_BIT_SFT                             6
#define AFE_DL6_SE_SECURE_BIT_MASK                            0x1
#define AFE_DL6_SE_SECURE_BIT_MASK_SFT                        (0x1 << 6)
#define AFE_DL5_SE_SECURE_BIT_SFT                             5
#define AFE_DL5_SE_SECURE_BIT_MASK                            0x1
#define AFE_DL5_SE_SECURE_BIT_MASK_SFT                        (0x1 << 5)
#define AFE_DL4_SE_SECURE_BIT_SFT                             4
#define AFE_DL4_SE_SECURE_BIT_MASK                            0x1
#define AFE_DL4_SE_SECURE_BIT_MASK_SFT                        (0x1 << 4)
#define AFE_DL3_SE_SECURE_BIT_SFT                             3
#define AFE_DL3_SE_SECURE_BIT_MASK                            0x1
#define AFE_DL3_SE_SECURE_BIT_MASK_SFT                        (0x1 << 3)
#define AFE_DL2_SE_SECURE_BIT_SFT                             2
#define AFE_DL2_SE_SECURE_BIT_MASK                            0x1
#define AFE_DL2_SE_SECURE_BIT_MASK_SFT                        (0x1 << 2)
#define AFE_DL1_SE_SECURE_BIT_SFT                             1
#define AFE_DL1_SE_SECURE_BIT_MASK                            0x1
#define AFE_DL1_SE_SECURE_BIT_MASK_SFT                        (0x1 << 1)
#define AFE_DL0_SE_SECURE_BIT_SFT                             0
#define AFE_DL0_SE_SECURE_BIT_MASK                            0x1
#define AFE_DL0_SE_SECURE_BIT_MASK_SFT                        (0x1 << 0)

/* AFE_SE_SECURE_CON1 */
#define AFE_DL46_SE_SECURE_BIT_SFT                            26
#define AFE_DL46_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL46_SE_SECURE_BIT_MASK_SFT                       (0x1 << 26)
#define AFE_DL45_SE_SECURE_BIT_SFT                            25
#define AFE_DL45_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL45_SE_SECURE_BIT_MASK_SFT                       (0x1 << 25)
#define AFE_DL44_SE_SECURE_BIT_SFT                            24
#define AFE_DL44_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL44_SE_SECURE_BIT_MASK_SFT                       (0x1 << 24)
#define AFE_DL43_SE_SECURE_BIT_SFT                            23
#define AFE_DL43_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL43_SE_SECURE_BIT_MASK_SFT                       (0x1 << 23)
#define AFE_DL42_SE_SECURE_BIT_SFT                            22
#define AFE_DL42_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL42_SE_SECURE_BIT_MASK_SFT                       (0x1 << 22)
#define AFE_DL41_SE_SECURE_BIT_SFT                            21
#define AFE_DL41_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL41_SE_SECURE_BIT_MASK_SFT                       (0x1 << 21)
#define AFE_DL40_SE_SECURE_BIT_SFT                            20
#define AFE_DL40_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL40_SE_SECURE_BIT_MASK_SFT                       (0x1 << 20)
#define AFE_DL39_SE_SECURE_BIT_SFT                            19
#define AFE_DL39_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL39_SE_SECURE_BIT_MASK_SFT                       (0x1 << 19)
#define AFE_DL38_SE_SECURE_BIT_SFT                            18
#define AFE_DL38_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL38_SE_SECURE_BIT_MASK_SFT                       (0x1 << 18)
#define AFE_DL37_SE_SECURE_BIT_SFT                            17
#define AFE_DL37_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL37_SE_SECURE_BIT_MASK_SFT                       (0x1 << 17)
#define AFE_DL36_SE_SECURE_BIT_SFT                            16
#define AFE_DL36_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL36_SE_SECURE_BIT_MASK_SFT                       (0x1 << 16)
#define AFE_DL35_SE_SECURE_BIT_SFT                            15
#define AFE_DL35_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL35_SE_SECURE_BIT_MASK_SFT                       (0x1 << 15)
#define AFE_DL34_SE_SECURE_BIT_SFT                            14
#define AFE_DL34_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL34_SE_SECURE_BIT_MASK_SFT                       (0x1 << 14)
#define AFE_DL33_SE_SECURE_BIT_SFT                            13
#define AFE_DL33_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL33_SE_SECURE_BIT_MASK_SFT                       (0x1 << 13)
#define AFE_DL32_SE_SECURE_BIT_SFT                            12
#define AFE_DL32_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL32_SE_SECURE_BIT_MASK_SFT                       (0x1 << 12)
#define AFE_DL31_SE_SECURE_BIT_SFT                            11
#define AFE_DL31_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL31_SE_SECURE_BIT_MASK_SFT                       (0x1 << 11)
#define AFE_DL30_SE_SECURE_BIT_SFT                            10
#define AFE_DL30_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL30_SE_SECURE_BIT_MASK_SFT                       (0x1 << 10)
#define AFE_DL29_SE_SECURE_BIT_SFT                            9
#define AFE_DL29_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL29_SE_SECURE_BIT_MASK_SFT                       (0x1 << 9)
#define AFE_DL28_SE_SECURE_BIT_SFT                            8
#define AFE_DL28_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL28_SE_SECURE_BIT_MASK_SFT                       (0x1 << 8)
#define AFE_DL27_SE_SECURE_BIT_SFT                            7
#define AFE_DL27_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL27_SE_SECURE_BIT_MASK_SFT                       (0x1 << 7)
#define AFE_DL26_SE_SECURE_BIT_SFT                            6
#define AFE_DL26_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL26_SE_SECURE_BIT_MASK_SFT                       (0x1 << 6)
#define AFE_DL25_SE_SECURE_BIT_SFT                            5
#define AFE_DL25_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL25_SE_SECURE_BIT_MASK_SFT                       (0x1 << 5)
#define AFE_DL24_SE_SECURE_BIT_SFT                            4
#define AFE_DL24_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL24_SE_SECURE_BIT_MASK_SFT                       (0x1 << 4)
#define AFE_DL23_SE_SECURE_BIT_SFT                            3
#define AFE_DL23_SE_SECURE_BIT_MASK                           0x1
#define AFE_DL23_SE_SECURE_BIT_MASK_SFT                       (0x1 << 3)
#define AFE_DL_48CH_SE_SECURE_BIT_SFT                         2
#define AFE_DL_48CH_SE_SECURE_BIT_MASK                        0x1
#define AFE_DL_48CH_SE_SECURE_BIT_MASK_SFT                    (0x1 << 2)
#define AFE_DL_24CH_SE_SECURE_BIT_SFT                         1
#define AFE_DL_24CH_SE_SECURE_BIT_MASK                        0x1
#define AFE_DL_24CH_SE_SECURE_BIT_MASK_SFT                    (0x1 << 1)
#define AFE_DL_4CH_SE_SECURE_BIT_SFT                          0
#define AFE_DL_4CH_SE_SECURE_BIT_MASK                         0x1
#define AFE_DL_4CH_SE_SECURE_BIT_MASK_SFT                     (0x1 << 0)

/* AFE_SE_SECURE_CON2 */
#define AFE_VUL38_SE_SECURE_BIT_SFT                           28
#define AFE_VUL38_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL38_SE_SECURE_BIT_MASK_SFT                      (0x1 << 28)
#define AFE_VUL37_SE_SECURE_BIT_SFT                           27
#define AFE_VUL37_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL37_SE_SECURE_BIT_MASK_SFT                      (0x1 << 27)
#define AFE_VUL36_SE_SECURE_BIT_SFT                           26
#define AFE_VUL36_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL36_SE_SECURE_BIT_MASK_SFT                      (0x1 << 26)
#define AFE_VUL35_SE_SECURE_BIT_SFT                           25
#define AFE_VUL35_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL35_SE_SECURE_BIT_MASK_SFT                      (0x1 << 25)
#define AFE_VUL34_SE_SECURE_BIT_SFT                           24
#define AFE_VUL34_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL34_SE_SECURE_BIT_MASK_SFT                      (0x1 << 24)
#define AFE_VUL33_SE_SECURE_BIT_SFT                           23
#define AFE_VUL33_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL33_SE_SECURE_BIT_MASK_SFT                      (0x1 << 23)
#define AFE_VUL32_SE_SECURE_BIT_SFT                           22
#define AFE_VUL32_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL32_SE_SECURE_BIT_MASK_SFT                      (0x1 << 22)
#define AFE_VUL31_SE_SECURE_BIT_SFT                           21
#define AFE_VUL31_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL31_SE_SECURE_BIT_MASK_SFT                      (0x1 << 21)
#define AFE_VUL30_SE_SECURE_BIT_SFT                           20
#define AFE_VUL30_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL30_SE_SECURE_BIT_MASK_SFT                      (0x1 << 20)
#define AFE_VUL29_SE_SECURE_BIT_SFT                           19
#define AFE_VUL29_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL29_SE_SECURE_BIT_MASK_SFT                      (0x1 << 19)
#define AFE_VUL28_SE_SECURE_BIT_SFT                           18
#define AFE_VUL28_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL28_SE_SECURE_BIT_MASK_SFT                      (0x1 << 18)
#define AFE_VUL27_SE_SECURE_BIT_SFT                           17
#define AFE_VUL27_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL27_SE_SECURE_BIT_MASK_SFT                      (0x1 << 17)
#define AFE_VUL26_SE_SECURE_BIT_SFT                           16
#define AFE_VUL26_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL26_SE_SECURE_BIT_MASK_SFT                      (0x1 << 16)
#define AFE_VUL25_SE_SECURE_BIT_SFT                           15
#define AFE_VUL25_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL25_SE_SECURE_BIT_MASK_SFT                      (0x1 << 15)
#define AFE_VUL24_SE_SECURE_BIT_SFT                           14
#define AFE_VUL24_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL24_SE_SECURE_BIT_MASK_SFT                      (0x1 << 14)
#define AFE_VUL_CM2_SE_SECURE_BIT_SFT                         13
#define AFE_VUL_CM2_SE_SECURE_BIT_MASK                        0x1
#define AFE_VUL_CM2_SE_SECURE_BIT_MASK_SFT                    (0x1 << 13)
#define AFE_VUL_CM1_SE_SECURE_BIT_SFT                         12
#define AFE_VUL_CM1_SE_SECURE_BIT_MASK                        0x1
#define AFE_VUL_CM1_SE_SECURE_BIT_MASK_SFT                    (0x1 << 12)
#define AFE_VUL_CM0_SE_SECURE_BIT_SFT                         11
#define AFE_VUL_CM0_SE_SECURE_BIT_MASK                        0x1
#define AFE_VUL_CM0_SE_SECURE_BIT_MASK_SFT                    (0x1 << 11)
#define AFE_VUL10_SE_SECURE_BIT_SFT                           10
#define AFE_VUL10_SE_SECURE_BIT_MASK                          0x1
#define AFE_VUL10_SE_SECURE_BIT_MASK_SFT                      (0x1 << 10)
#define AFE_VUL9_SE_SECURE_BIT_SFT                            9
#define AFE_VUL9_SE_SECURE_BIT_MASK                           0x1
#define AFE_VUL9_SE_SECURE_BIT_MASK_SFT                       (0x1 << 9)
#define AFE_VUL8_SE_SECURE_BIT_SFT                            8
#define AFE_VUL8_SE_SECURE_BIT_MASK                           0x1
#define AFE_VUL8_SE_SECURE_BIT_MASK_SFT                       (0x1 << 8)
#define AFE_VUL7_SE_SECURE_BIT_SFT                            7
#define AFE_VUL7_SE_SECURE_BIT_MASK                           0x1
#define AFE_VUL7_SE_SECURE_BIT_MASK_SFT                       (0x1 << 7)
#define AFE_VUL6_SE_SECURE_BIT_SFT                            6
#define AFE_VUL6_SE_SECURE_BIT_MASK                           0x1
#define AFE_VUL6_SE_SECURE_BIT_MASK_SFT                       (0x1 << 6)
#define AFE_VUL5_SE_SECURE_BIT_SFT                            5
#define AFE_VUL5_SE_SECURE_BIT_MASK                           0x1
#define AFE_VUL5_SE_SECURE_BIT_MASK_SFT                       (0x1 << 5)
#define AFE_VUL4_SE_SECURE_BIT_SFT                            4
#define AFE_VUL4_SE_SECURE_BIT_MASK                           0x1
#define AFE_VUL4_SE_SECURE_BIT_MASK_SFT                       (0x1 << 4)
#define AFE_VUL3_SE_SECURE_BIT_SFT                            3
#define AFE_VUL3_SE_SECURE_BIT_MASK                           0x1
#define AFE_VUL3_SE_SECURE_BIT_MASK_SFT                       (0x1 << 3)
#define AFE_VUL2_SE_SECURE_BIT_SFT                            2
#define AFE_VUL2_SE_SECURE_BIT_MASK                           0x1
#define AFE_VUL2_SE_SECURE_BIT_MASK_SFT                       (0x1 << 2)
#define AFE_VUL1_SE_SECURE_BIT_SFT                            1
#define AFE_VUL1_SE_SECURE_BIT_MASK                           0x1
#define AFE_VUL1_SE_SECURE_BIT_MASK_SFT                       (0x1 << 1)
#define AFE_VUL0_SE_SECURE_BIT_SFT                            0
#define AFE_VUL0_SE_SECURE_BIT_MASK                           0x1
#define AFE_VUL0_SE_SECURE_BIT_MASK_SFT                       (0x1 << 0)

/* AFE_SE_SECURE_CON3 */
#define AFE_SPDIFIN_SE_SECURE_BIT_SFT                         10
#define AFE_SPDIFIN_SE_SECURE_BIT_MASK                        0x1
#define AFE_SPDIFIN_SE_SECURE_BIT_MASK_SFT                    (0x1 << 10)
#define AFE_TDM_IN_SE_SECURE_BIT_SFT                          9
#define AFE_TDM_IN_SE_SECURE_BIT_MASK                         0x1
#define AFE_TDM_IN_SE_SECURE_BIT_MASK_SFT                     (0x1 << 9)
#define AFE_MPHONE_EARC_SE_SECURE_BIT_SFT                     8
#define AFE_MPHONE_EARC_SE_SECURE_BIT_MASK                    0x1
#define AFE_MPHONE_EARC_SE_SECURE_BIT_MASK_SFT                (0x1 << 8)
#define AFE_MPHONE_SPDIF_SE_SECURE_BIT_SFT                    7
#define AFE_MPHONE_SPDIF_SE_SECURE_BIT_MASK                   0x1
#define AFE_MPHONE_SPDIF_SE_SECURE_BIT_MASK_SFT               (0x1 << 7)
#define AFE_ETDM_IN6_SE_SECURE_BIT_SFT                        6
#define AFE_ETDM_IN6_SE_SECURE_BIT_MASK                       0x1
#define AFE_ETDM_IN6_SE_SECURE_BIT_MASK_SFT                   (0x1 << 6)
#define AFE_ETDM_IN5_SE_SECURE_BIT_SFT                        5
#define AFE_ETDM_IN5_SE_SECURE_BIT_MASK                       0x1
#define AFE_ETDM_IN5_SE_SECURE_BIT_MASK_SFT                   (0x1 << 5)
#define AFE_ETDM_IN4_SE_SECURE_BIT_SFT                        4
#define AFE_ETDM_IN4_SE_SECURE_BIT_MASK                       0x1
#define AFE_ETDM_IN4_SE_SECURE_BIT_MASK_SFT                   (0x1 << 4)
#define AFE_ETDM_IN3_SE_SECURE_BIT_SFT                        3
#define AFE_ETDM_IN3_SE_SECURE_BIT_MASK                       0x1
#define AFE_ETDM_IN3_SE_SECURE_BIT_MASK_SFT                   (0x1 << 3)
#define AFE_ETDM_IN2_SE_SECURE_BIT_SFT                        2
#define AFE_ETDM_IN2_SE_SECURE_BIT_MASK                       0x1
#define AFE_ETDM_IN2_SE_SECURE_BIT_MASK_SFT                   (0x1 << 2)
#define AFE_ETDM_IN1_SE_SECURE_BIT_SFT                        1
#define AFE_ETDM_IN1_SE_SECURE_BIT_MASK                       0x1
#define AFE_ETDM_IN1_SE_SECURE_BIT_MASK_SFT                   (0x1 << 1)
#define AFE_ETDM_IN0_SE_SECURE_BIT_SFT                        0
#define AFE_ETDM_IN0_SE_SECURE_BIT_MASK                       0x1
#define AFE_ETDM_IN0_SE_SECURE_BIT_MASK_SFT                   (0x1 << 0)

/* AFE_SE_PROT_SIDEBAND0 */
#define HDMI_HPROT_SFT                                        11
#define HDMI_HPROT_MASK                                       0x1
#define HDMI_HPROT_MASK_SFT                                   (0x1 << 11)
#define SPDIF2_OUT_HPROT_SFT                                  10
#define SPDIF2_OUT_HPROT_MASK                                 0x1
#define SPDIF2_OUT_HPROT_MASK_SFT                             (0x1 << 10)
#define SPDIF_OUT_HPROT_SFT                                   9
#define SPDIF_OUT_HPROT_MASK                                  0x1
#define SPDIF_OUT_HPROT_MASK_SFT                              (0x1 << 9)
#define DL8_HPROT_SFT                                         8
#define DL8_HPROT_MASK                                        0x1
#define DL8_HPROT_MASK_SFT                                    (0x1 << 8)
#define DL7_HPROT_SFT                                         7
#define DL7_HPROT_MASK                                        0x1
#define DL7_HPROT_MASK_SFT                                    (0x1 << 7)
#define DL6_HPROT_SFT                                         6
#define DL6_HPROT_MASK                                        0x1
#define DL6_HPROT_MASK_SFT                                    (0x1 << 6)
#define DL5_HPROT_SFT                                         5
#define DL5_HPROT_MASK                                        0x1
#define DL5_HPROT_MASK_SFT                                    (0x1 << 5)
#define DL4_HPROT_SFT                                         4
#define DL4_HPROT_MASK                                        0x1
#define DL4_HPROT_MASK_SFT                                    (0x1 << 4)
#define DL3_HPROT_SFT                                         3
#define DL3_HPROT_MASK                                        0x1
#define DL3_HPROT_MASK_SFT                                    (0x1 << 3)
#define DL2_HPROT_SFT                                         2
#define DL2_HPROT_MASK                                        0x1
#define DL2_HPROT_MASK_SFT                                    (0x1 << 2)
#define DL1_HPROT_SFT                                         1
#define DL1_HPROT_MASK                                        0x1
#define DL1_HPROT_MASK_SFT                                    (0x1 << 1)
#define DL0_HPROT_SFT                                         0
#define DL0_HPROT_MASK                                        0x1
#define DL0_HPROT_MASK_SFT                                    (0x1 << 0)

/* AFE_SE_PROT_SIDEBAND1 */
#define DL46_HPROT_SFT                                        26
#define DL46_HPROT_MASK                                       0x1
#define DL46_HPROT_MASK_SFT                                   (0x1 << 26)
#define DL45_HPROT_SFT                                        25
#define DL45_HPROT_MASK                                       0x1
#define DL45_HPROT_MASK_SFT                                   (0x1 << 25)
#define DL44_HPROT_SFT                                        24
#define DL44_HPROT_MASK                                       0x1
#define DL44_HPROT_MASK_SFT                                   (0x1 << 24)
#define DL43_HPROT_SFT                                        23
#define DL43_HPROT_MASK                                       0x1
#define DL43_HPROT_MASK_SFT                                   (0x1 << 23)
#define DL42_HPROT_SFT                                        22
#define DL42_HPROT_MASK                                       0x1
#define DL42_HPROT_MASK_SFT                                   (0x1 << 22)
#define DL41_HPROT_SFT                                        21
#define DL41_HPROT_MASK                                       0x1
#define DL41_HPROT_MASK_SFT                                   (0x1 << 21)
#define DL40_HPROT_SFT                                        20
#define DL40_HPROT_MASK                                       0x1
#define DL40_HPROT_MASK_SFT                                   (0x1 << 20)
#define DL39_HPROT_SFT                                        19
#define DL39_HPROT_MASK                                       0x1
#define DL39_HPROT_MASK_SFT                                   (0x1 << 19)
#define DL38_HPROT_SFT                                        18
#define DL38_HPROT_MASK                                       0x1
#define DL38_HPROT_MASK_SFT                                   (0x1 << 18)
#define DL37_HPROT_SFT                                        17
#define DL37_HPROT_MASK                                       0x1
#define DL37_HPROT_MASK_SFT                                   (0x1 << 17)
#define DL36_HPROT_SFT                                        16
#define DL36_HPROT_MASK                                       0x1
#define DL36_HPROT_MASK_SFT                                   (0x1 << 16)
#define DL35_HPROT_SFT                                        15
#define DL35_HPROT_MASK                                       0x1
#define DL35_HPROT_MASK_SFT                                   (0x1 << 15)
#define DL34_HPROT_SFT                                        14
#define DL34_HPROT_MASK                                       0x1
#define DL34_HPROT_MASK_SFT                                   (0x1 << 14)
#define DL33_HPROT_SFT                                        13
#define DL33_HPROT_MASK                                       0x1
#define DL33_HPROT_MASK_SFT                                   (0x1 << 13)
#define DL32_HPROT_SFT                                        12
#define DL32_HPROT_MASK                                       0x1
#define DL32_HPROT_MASK_SFT                                   (0x1 << 12)
#define DL31_HPROT_SFT                                        11
#define DL31_HPROT_MASK                                       0x1
#define DL31_HPROT_MASK_SFT                                   (0x1 << 11)
#define DL30_HPROT_SFT                                        10
#define DL30_HPROT_MASK                                       0x1
#define DL30_HPROT_MASK_SFT                                   (0x1 << 10)
#define DL29_HPROT_SFT                                        9
#define DL29_HPROT_MASK                                       0x1
#define DL29_HPROT_MASK_SFT                                   (0x1 << 9)
#define DL28_HPROT_SFT                                        8
#define DL28_HPROT_MASK                                       0x1
#define DL28_HPROT_MASK_SFT                                   (0x1 << 8)
#define DL27_HPROT_SFT                                        7
#define DL27_HPROT_MASK                                       0x1
#define DL27_HPROT_MASK_SFT                                   (0x1 << 7)
#define DL26_HPROT_SFT                                        6
#define DL26_HPROT_MASK                                       0x1
#define DL26_HPROT_MASK_SFT                                   (0x1 << 6)
#define DL25_HPROT_SFT                                        5
#define DL25_HPROT_MASK                                       0x1
#define DL25_HPROT_MASK_SFT                                   (0x1 << 5)
#define DL24_HPROT_SFT                                        4
#define DL24_HPROT_MASK                                       0x1
#define DL24_HPROT_MASK_SFT                                   (0x1 << 4)
#define DL23_HPROT_SFT                                        3
#define DL23_HPROT_MASK                                       0x1
#define DL23_HPROT_MASK_SFT                                   (0x1 << 3)
#define DL_48CH_PROT_SFT                                      2
#define DL_48CH_PROT_MASK                                     0x1
#define DL_48CH_PROT_MASK_SFT                                 (0x1 << 2)
#define DL_24CH_PROT_SFT                                      1
#define DL_24CH_PROT_MASK                                     0x1
#define DL_24CH_PROT_MASK_SFT                                 (0x1 << 1)
#define DL_4CH_PROT_SFT                                       0
#define DL_4CH_PROT_MASK                                      0x1
#define DL_4CH_PROT_MASK_SFT                                  (0x1 << 0)

/* AFE_SE_PROT_SIDEBAND2 */
#define VUL38_HPROT_SFT                                       28
#define VUL38_HPROT_MASK                                      0x1
#define VUL38_HPROT_MASK_SFT                                  (0x1 << 28)
#define VUL37_HPROT_SFT                                       27
#define VUL37_HPROT_MASK                                      0x1
#define VUL37_HPROT_MASK_SFT                                  (0x1 << 27)
#define VUL36_HPROT_SFT                                       26
#define VUL36_HPROT_MASK                                      0x1
#define VUL36_HPROT_MASK_SFT                                  (0x1 << 26)
#define VUL35_HPROT_SFT                                       25
#define VUL35_HPROT_MASK                                      0x1
#define VUL35_HPROT_MASK_SFT                                  (0x1 << 25)
#define VUL34_HPROT_SFT                                       24
#define VUL34_HPROT_MASK                                      0x1
#define VUL34_HPROT_MASK_SFT                                  (0x1 << 24)
#define VUL33_HPROT_SFT                                       23
#define VUL33_HPROT_MASK                                      0x1
#define VUL33_HPROT_MASK_SFT                                  (0x1 << 23)
#define VUL32_HPROT_SFT                                       22
#define VUL32_HPROT_MASK                                      0x1
#define VUL32_HPROT_MASK_SFT                                  (0x1 << 22)
#define VUL31_HPROT_SFT                                       21
#define VUL31_HPROT_MASK                                      0x1
#define VUL31_HPROT_MASK_SFT                                  (0x1 << 21)
#define VUL30_HPROT_SFT                                       20
#define VUL30_HPROT_MASK                                      0x1
#define VUL30_HPROT_MASK_SFT                                  (0x1 << 20)
#define VUL29_HPROT_SFT                                       19
#define VUL29_HPROT_MASK                                      0x1
#define VUL29_HPROT_MASK_SFT                                  (0x1 << 19)
#define VUL28_HPROT_SFT                                       18
#define VUL28_HPROT_MASK                                      0x1
#define VUL28_HPROT_MASK_SFT                                  (0x1 << 18)
#define VUL27_HPROT_SFT                                       17
#define VUL27_HPROT_MASK                                      0x1
#define VUL27_HPROT_MASK_SFT                                  (0x1 << 17)
#define VUL26_HPROT_SFT                                       16
#define VUL26_HPROT_MASK                                      0x1
#define VUL26_HPROT_MASK_SFT                                  (0x1 << 16)
#define VUL25_HPROT_SFT                                       15
#define VUL25_HPROT_MASK                                      0x1
#define VUL25_HPROT_MASK_SFT                                  (0x1 << 15)
#define VUL24_HPROT_SFT                                       14
#define VUL24_HPROT_MASK                                      0x1
#define VUL24_HPROT_MASK_SFT                                  (0x1 << 14)
#define VUL_CM2_HPROT_SFT                                     13
#define VUL_CM2_HPROT_MASK                                    0x1
#define VUL_CM2_HPROT_MASK_SFT                                (0x1 << 13)
#define VUL_CM1_HPROT_SFT                                     12
#define VUL_CM1_HPROT_MASK                                    0x1
#define VUL_CM1_HPROT_MASK_SFT                                (0x1 << 12)
#define VUL_CM0_HPROT_SFT                                     11
#define VUL_CM0_HPROT_MASK                                    0x1
#define VUL_CM0_HPROT_MASK_SFT                                (0x1 << 11)
#define VUL10_HPROT_SFT                                       10
#define VUL10_HPROT_MASK                                      0x1
#define VUL10_HPROT_MASK_SFT                                  (0x1 << 10)
#define VUL9_HPROT_SFT                                        9
#define VUL9_HPROT_MASK                                       0x1
#define VUL9_HPROT_MASK_SFT                                   (0x1 << 9)
#define VUL8_HPROT_SFT                                        8
#define VUL8_HPROT_MASK                                       0x1
#define VUL8_HPROT_MASK_SFT                                   (0x1 << 8)
#define VUL7_HPROT_SFT                                        7
#define VUL7_HPROT_MASK                                       0x1
#define VUL7_HPROT_MASK_SFT                                   (0x1 << 7)
#define VUL6_HPROT_SFT                                        6
#define VUL6_HPROT_MASK                                       0x1
#define VUL6_HPROT_MASK_SFT                                   (0x1 << 6)
#define VUL5_HPROT_SFT                                        5
#define VUL5_HPROT_MASK                                       0x1
#define VUL5_HPROT_MASK_SFT                                   (0x1 << 5)
#define VUL4_HPROT_SFT                                        4
#define VUL4_HPROT_MASK                                       0x1
#define VUL4_HPROT_MASK_SFT                                   (0x1 << 4)
#define VUL3_HPROT_SFT                                        3
#define VUL3_HPROT_MASK                                       0x1
#define VUL3_HPROT_MASK_SFT                                   (0x1 << 3)
#define VUL2_HPROT_SFT                                        2
#define VUL2_HPROT_MASK                                       0x1
#define VUL2_HPROT_MASK_SFT                                   (0x1 << 2)
#define VUL1_HPROT_SFT                                        1
#define VUL1_HPROT_MASK                                       0x1
#define VUL1_HPROT_MASK_SFT                                   (0x1 << 1)
#define VUL0_HPROT_SFT                                        0
#define VUL0_HPROT_MASK                                       0x1
#define VUL0_HPROT_MASK_SFT                                   (0x1 << 0)

/* AFE_SE_PROT_SIDEBAND3 */
#define MPHONE_EARC_HPROT_SFT                                 10
#define MPHONE_EARC_HPROT_MASK                                0x1
#define MPHONE_EARC_HPROT_MASK_SFT                            (0x1 << 10)
#define MPHONE_SPDIF_HPROT_SFT                                9
#define MPHONE_SPDIF_HPROT_MASK                               0x1
#define MPHONE_SPDIF_HPROT_MASK_SFT                           (0x1 << 9)
#define SPDIFIN_HPROT_SFT                                     8
#define SPDIFIN_HPROT_MASK                                    0x1
#define SPDIFIN_HPROT_MASK_SFT                                (0x1 << 8)
#define TDMIN_HPROT_SFT                                       7
#define TDMIN_HPROT_MASK                                      0x1
#define TDMIN_HPROT_MASK_SFT                                  (0x1 << 7)
#define ETDM_IN6_HPROT_SFT                                    6
#define ETDM_IN6_HPROT_MASK                                   0x1
#define ETDM_IN6_HPROT_MASK_SFT                               (0x1 << 6)
#define ETDM_IN5_HPROT_SFT                                    5
#define ETDM_IN5_HPROT_MASK                                   0x1
#define ETDM_IN5_HPROT_MASK_SFT                               (0x1 << 5)
#define ETDM_IN4_HPROT_SFT                                    4
#define ETDM_IN4_HPROT_MASK                                   0x1
#define ETDM_IN4_HPROT_MASK_SFT                               (0x1 << 4)
#define ETDM_IN3_HPROT_SFT                                    3
#define ETDM_IN3_HPROT_MASK                                   0x1
#define ETDM_IN3_HPROT_MASK_SFT                               (0x1 << 3)
#define ETDM_IN2_HPROT_SFT                                    2
#define ETDM_IN2_HPROT_MASK                                   0x1
#define ETDM_IN2_HPROT_MASK_SFT                               (0x1 << 2)
#define ETDM_IN1_HPROT_SFT                                    1
#define ETDM_IN1_HPROT_MASK                                   0x1
#define ETDM_IN1_HPROT_MASK_SFT                               (0x1 << 1)
#define ETDM_IN0_HPROT_SFT                                    0
#define ETDM_IN0_HPROT_MASK                                   0x1
#define ETDM_IN0_HPROT_MASK_SFT                               (0x1 << 0)

/* AFE_SE_DOMAIN_SIDEBAND0 */
#define DL7_HDOMAIN_SFT                                       28
#define DL7_HDOMAIN_MASK                                      0xf
#define DL7_HDOMAIN_MASK_SFT                                  (0xf << 28)
#define DL6_HDOMAIN_SFT                                       24
#define DL6_HDOMAIN_MASK                                      0xf
#define DL6_HDOMAIN_MASK_SFT                                  (0xf << 24)
#define DL5_HDOMAIN_SFT                                       20
#define DL5_HDOMAIN_MASK                                      0xf
#define DL5_HDOMAIN_MASK_SFT                                  (0xf << 20)
#define DL4_HDOMAIN_SFT                                       16
#define DL4_HDOMAIN_MASK                                      0xf
#define DL4_HDOMAIN_MASK_SFT                                  (0xf << 16)
#define DL3_HDOMAIN_SFT                                       12
#define DL3_HDOMAIN_MASK                                      0xf
#define DL3_HDOMAIN_MASK_SFT                                  (0xf << 12)
#define DL2_HDOMAIN_SFT                                       8
#define DL2_HDOMAIN_MASK                                      0xf
#define DL2_HDOMAIN_MASK_SFT                                  (0xf << 8)
#define DL1_HDOMAIN_SFT                                       4
#define DL1_HDOMAIN_MASK                                      0xf
#define DL1_HDOMAIN_MASK_SFT                                  (0xf << 4)
#define DL0_HDOMAIN_SFT                                       0
#define DL0_HDOMAIN_MASK                                      0xf
#define DL0_HDOMAIN_MASK_SFT                                  (0xf << 0)

/* AFE_SE_DOMAIN_SIDEBAND1 */
#define DL_48CH_HDOMAIN_SFT                                   24
#define DL_48CH_HDOMAIN_MASK                                  0xf
#define DL_48CH_HDOMAIN_MASK_SFT                              (0xf << 24)
#define DL_24CH_HDOMAIN_SFT                                   20
#define DL_24CH_HDOMAIN_MASK                                  0xf
#define DL_24CH_HDOMAIN_MASK_SFT                              (0xf << 20)
#define DL_4CH_HDOMAIN_SFT                                    16
#define DL_4CH_HDOMAIN_MASK                                   0xf
#define DL_4CH_HDOMAIN_MASK_SFT                               (0xf << 16)
#define HDMI_HDOMAIN_SFT                                      12
#define HDMI_HDOMAIN_MASK                                     0xf
#define HDMI_HDOMAIN_MASK_SFT                                 (0xf << 12)
#define SPDIF2_OUT_HDOMAIN_SFT                                8
#define SPDIF2_OUT_HDOMAIN_MASK                               0xf
#define SPDIF2_OUT_HDOMAIN_MASK_SFT                           (0xf << 8)
#define SPDIF_OUT_HDOMAIN_SFT                                 4
#define SPDIF_OUT_HDOMAIN_MASK                                0xf
#define SPDIF_OUT_HDOMAIN_MASK_SFT                            (0xf << 4)
#define DL8_HDOMAIN_SFT                                       0
#define DL8_HDOMAIN_MASK                                      0xf
#define DL8_HDOMAIN_MASK_SFT                                  (0xf << 0)

/* AFE_SE_DOMAIN_SIDEBAND2 */
#define DL30_HDOMAIN_SFT                                      28
#define DL30_HDOMAIN_MASK                                     0xf
#define DL30_HDOMAIN_MASK_SFT                                 (0xf << 28)
#define DL29_HDOMAIN_SFT                                      24
#define DL29_HDOMAIN_MASK                                     0xf
#define DL29_HDOMAIN_MASK_SFT                                 (0xf << 24)
#define DL28_HDOMAIN_SFT                                      20
#define DL28_HDOMAIN_MASK                                     0xf
#define DL28_HDOMAIN_MASK_SFT                                 (0xf << 20)
#define DL27_HDOMAIN_SFT                                      16
#define DL27_HDOMAIN_MASK                                     0xf
#define DL27_HDOMAIN_MASK_SFT                                 (0xf << 16)
#define DL26_HDOMAIN_SFT                                      12
#define DL26_HDOMAIN_MASK                                     0xf
#define DL26_HDOMAIN_MASK_SFT                                 (0xf << 12)
#define DL25_HDOMAIN_SFT                                      8
#define DL25_HDOMAIN_MASK                                     0xf
#define DL25_HDOMAIN_MASK_SFT                                 (0xf << 8)
#define DL24_HDOMAIN_SFT                                      4
#define DL24_HDOMAIN_MASK                                     0xf
#define DL24_HDOMAIN_MASK_SFT                                 (0xf << 4)
#define DL23_HDOMAIN_SFT                                      0
#define DL23_HDOMAIN_MASK                                     0xf
#define DL23_HDOMAIN_MASK_SFT                                 (0xf << 0)

/* AFE_SE_DOMAIN_SIDEBAND3 */
#define DL38_HDOMAIN_SFT                                      28
#define DL38_HDOMAIN_MASK                                     0xf
#define DL38_HDOMAIN_MASK_SFT                                 (0xf << 28)
#define DL37_HDOMAIN_SFT                                      24
#define DL37_HDOMAIN_MASK                                     0xf
#define DL37_HDOMAIN_MASK_SFT                                 (0xf << 24)
#define DL36_HDOMAIN_SFT                                      20
#define DL36_HDOMAIN_MASK                                     0xf
#define DL36_HDOMAIN_MASK_SFT                                 (0xf << 20)
#define DL35_HDOMAIN_SFT                                      16
#define DL35_HDOMAIN_MASK                                     0xf
#define DL35_HDOMAIN_MASK_SFT                                 (0xf << 16)
#define DL34_HDOMAIN_SFT                                      12
#define DL34_HDOMAIN_MASK                                     0xf
#define DL34_HDOMAIN_MASK_SFT                                 (0xf << 12)
#define DL33_HDOMAIN_SFT                                      8
#define DL33_HDOMAIN_MASK                                     0xf
#define DL33_HDOMAIN_MASK_SFT                                 (0xf << 8)
#define DL32_HDOMAIN_SFT                                      4
#define DL32_HDOMAIN_MASK                                     0xf
#define DL32_HDOMAIN_MASK_SFT                                 (0xf << 4)
#define DL31_HDOMAIN_SFT                                      0
#define DL31_HDOMAIN_MASK                                     0xf
#define DL31_HDOMAIN_MASK_SFT                                 (0xf << 0)

/* AFE_SE_DOMAIN_SIDEBAND4 */
#define DL46_HDOMAIN_SFT                                      28
#define DL46_HDOMAIN_MASK                                     0xf
#define DL46_HDOMAIN_MASK_SFT                                 (0xf << 28)
#define DL45_HDOMAIN_SFT                                      24
#define DL45_HDOMAIN_MASK                                     0xf
#define DL45_HDOMAIN_MASK_SFT                                 (0xf << 24)
#define DL44_HDOMAIN_SFT                                      20
#define DL44_HDOMAIN_MASK                                     0xf
#define DL44_HDOMAIN_MASK_SFT                                 (0xf << 20)
#define DL43_HDOMAIN_SFT                                      16
#define DL43_HDOMAIN_MASK                                     0xf
#define DL43_HDOMAIN_MASK_SFT                                 (0xf << 16)
#define DL42_HDOMAIN_SFT                                      12
#define DL42_HDOMAIN_MASK                                     0xf
#define DL42_HDOMAIN_MASK_SFT                                 (0xf << 12)
#define DL41_HDOMAIN_SFT                                      8
#define DL41_HDOMAIN_MASK                                     0xf
#define DL41_HDOMAIN_MASK_SFT                                 (0xf << 8)
#define DL40_HDOMAIN_SFT                                      4
#define DL40_HDOMAIN_MASK                                     0xf
#define DL40_HDOMAIN_MASK_SFT                                 (0xf << 4)
#define DL39_HDOMAIN_SFT                                      0
#define DL39_HDOMAIN_MASK                                     0xf
#define DL39_HDOMAIN_MASK_SFT                                 (0xf << 0)

/* AFE_SE_DOMAIN_SIDEBAND5 */
#define VUL7_HDOMAIN_SFT                                      28
#define VUL7_HDOMAIN_MASK                                     0xf
#define VUL7_HDOMAIN_MASK_SFT                                 (0xf << 28)
#define VUL6_HDOMAIN_SFT                                      24
#define VUL6_HDOMAIN_MASK                                     0xf
#define VUL6_HDOMAIN_MASK_SFT                                 (0xf << 24)
#define VUL5_HDOMAIN_SFT                                      20
#define VUL5_HDOMAIN_MASK                                     0xf
#define VUL5_HDOMAIN_MASK_SFT                                 (0xf << 20)
#define VUL4_HDOMAIN_SFT                                      16
#define VUL4_HDOMAIN_MASK                                     0xf
#define VUL4_HDOMAIN_MASK_SFT                                 (0xf << 16)
#define VUL3_HDOMAIN_SFT                                      12
#define VUL3_HDOMAIN_MASK                                     0xf
#define VUL3_HDOMAIN_MASK_SFT                                 (0xf << 12)
#define VUL2_HDOMAIN_SFT                                      8
#define VUL2_HDOMAIN_MASK                                     0xf
#define VUL2_HDOMAIN_MASK_SFT                                 (0xf << 8)
#define VUL1_HDOMAIN_SFT                                      4
#define VUL1_HDOMAIN_MASK                                     0xf
#define VUL1_HDOMAIN_MASK_SFT                                 (0xf << 4)
#define VUL0_HDOMAIN_SFT                                      0
#define VUL0_HDOMAIN_MASK                                     0xf
#define VUL0_HDOMAIN_MASK_SFT                                 (0xf << 0)

/* AFE_SE_DOMAIN_SIDEBAND6 */
#define VU25_HDOMAIN_SFT                                      28
#define VU25_HDOMAIN_MASK                                     0xf
#define VU25_HDOMAIN_MASK_SFT                                 (0xf << 28)
#define VUL24_HDOMAIN_SFT                                     24
#define VUL24_HDOMAIN_MASK                                    0xf
#define VUL24_HDOMAIN_MASK_SFT                                (0xf << 24)
#define VUL_CM2_HDOMAIN_SFT                                   20
#define VUL_CM2_HDOMAIN_MASK                                  0xf
#define VUL_CM2_HDOMAIN_MASK_SFT                              (0xf << 20)
#define VUL_CM1_HDOMAIN_SFT                                   16
#define VUL_CM1_HDOMAIN_MASK                                  0xf
#define VUL_CM1_HDOMAIN_MASK_SFT                              (0xf << 16)
#define VUL_CM0_HDOMAIN_SFT                                   12
#define VUL_CM0_HDOMAIN_MASK                                  0xf
#define VUL_CM0_HDOMAIN_MASK_SFT                              (0xf << 12)
#define VUL10_HDOMAIN_SFT                                     8
#define VUL10_HDOMAIN_MASK                                    0xf
#define VUL10_HDOMAIN_MASK_SFT                                (0xf << 8)
#define VUL9_HDOMAIN_SFT                                      4
#define VUL9_HDOMAIN_MASK                                     0xf
#define VUL9_HDOMAIN_MASK_SFT                                 (0xf << 4)
#define VUL8_HDOMAIN_SFT                                      0
#define VUL8_HDOMAIN_MASK                                     0xf
#define VUL8_HDOMAIN_MASK_SFT                                 (0xf << 0)

/* AFE_SE_DOMAIN_SIDEBAND7 */
#define VUL33_HDOMAIN_SFT                                     28
#define VUL33_HDOMAIN_MASK                                    0xf
#define VUL33_HDOMAIN_MASK_SFT                                (0xf << 28)
#define VUL32_HDOMAIN_SFT                                     24
#define VUL32_HDOMAIN_MASK                                    0xf
#define VUL32_HDOMAIN_MASK_SFT                                (0xf << 24)
#define VUL31_HDOMAIN_SFT                                     20
#define VUL31_HDOMAIN_MASK                                    0xf
#define VUL31_HDOMAIN_MASK_SFT                                (0xf << 20)
#define VUL30_HDOMAIN_SFT                                     16
#define VUL30_HDOMAIN_MASK                                    0xf
#define VUL30_HDOMAIN_MASK_SFT                                (0xf << 16)
#define VUL29_HDOMAIN_SFT                                     12
#define VUL29_HDOMAIN_MASK                                    0xf
#define VUL29_HDOMAIN_MASK_SFT                                (0xf << 12)
#define VUL28_HDOMAIN_SFT                                     8
#define VUL28_HDOMAIN_MASK                                    0xf
#define VUL28_HDOMAIN_MASK_SFT                                (0xf << 8)
#define VUL27_HDOMAIN_SFT                                     4
#define VUL27_HDOMAIN_MASK                                    0xf
#define VUL27_HDOMAIN_MASK_SFT                                (0xf << 4)
#define VUL26_HDOMAIN_SFT                                     0
#define VUL26_HDOMAIN_MASK                                    0xf
#define VUL26_HDOMAIN_MASK_SFT                                (0xf << 0)

/* AFE_SE_DOMAIN_SIDEBAND8 */
#define ETDM_IN2_HDOMAIN_SFT                                  28
#define ETDM_IN2_HDOMAIN_MASK                                 0xf
#define ETDM_IN2_HDOMAIN_MASK_SFT                             (0xf << 28)
#define ETDM_IN1_HDOMAIN_SFT                                  24
#define ETDM_IN1_HDOMAIN_MASK                                 0xf
#define ETDM_IN1_HDOMAIN_MASK_SFT                             (0xf << 24)
#define ETDM_IN0_HDOMAIN_SFT                                  20
#define ETDM_IN0_HDOMAIN_MASK                                 0xf
#define ETDM_IN0_HDOMAIN_MASK_SFT                             (0xf << 20)
#define VUL38_HDOMAIN_SFT                                     16
#define VUL38_HDOMAIN_MASK                                    0xf
#define VUL38_HDOMAIN_MASK_SFT                                (0xf << 16)
#define VUL37_HDOMAIN_SFT                                     12
#define VUL37_HDOMAIN_MASK                                    0xf
#define VUL37_HDOMAIN_MASK_SFT                                (0xf << 12)
#define VUL36_HDOMAIN_SFT                                     8
#define VUL36_HDOMAIN_MASK                                    0xf
#define VUL36_HDOMAIN_MASK_SFT                                (0xf << 8)
#define VUL35_HDOMAIN_SFT                                     4
#define VUL35_HDOMAIN_MASK                                    0xf
#define VUL35_HDOMAIN_MASK_SFT                                (0xf << 4)
#define VUL34_HDOMAIN_SFT                                     0
#define VUL34_HDOMAIN_MASK                                    0xf
#define VUL34_HDOMAIN_MASK_SFT                                (0xf << 0)

/* AFE_SE_DOMAIN_SIDEBAND9 */
#define MPHONE_EARC_HDOMAIN_SFT                               28
#define MPHONE_EARC_HDOMAIN_MASK                              0xf
#define MPHONE_EARC_HDOMAIN_MASK_SFT                          (0xf << 28)
#define MPHONE_SPDIF_HDOMAIN_SFT                              24
#define MPHONE_SPDIF_HDOMAIN_MASK                             0xf
#define MPHONE_SPDIF_HDOMAIN_MASK_SFT                         (0xf << 24)
#define SPDIFIN_HDOMAIN_SFT                                   20
#define SPDIFIN_HDOMAIN_MASK                                  0xf
#define SPDIFIN_HDOMAIN_MASK_SFT                              (0xf << 20)
#define TDMIN_HDOMAIN_SFT                                     16
#define TDMIN_HDOMAIN_MASK                                    0xf
#define TDMIN_HDOMAIN_MASK_SFT                                (0xf << 16)
#define ETDM_IN6_HDOMAIN_SFT                                  12
#define ETDM_IN6_HDOMAIN_MASK                                 0xf
#define ETDM_IN6_HDOMAIN_MASK_SFT                             (0xf << 12)
#define ETDM_IN5_HDOMAIN_SFT                                  8
#define ETDM_IN5_HDOMAIN_MASK                                 0xf
#define ETDM_IN5_HDOMAIN_MASK_SFT                             (0xf << 8)
#define ETDM_IN4_HDOMAIN_SFT                                  4
#define ETDM_IN4_HDOMAIN_MASK                                 0xf
#define ETDM_IN4_HDOMAIN_MASK_SFT                             (0xf << 4)
#define ETDM_IN3_HDOMAIN_SFT                                  0
#define ETDM_IN3_HDOMAIN_MASK                                 0xf
#define ETDM_IN3_HDOMAIN_MASK_SFT                             (0xf << 0)

/* AFE_PROT_SIDEBAND0_MON */
#define AFE_DOMAIN_SIDEBAN0_MON_SFT                           0
#define AFE_DOMAIN_SIDEBAN0_MON_MASK                          0xffffffff
#define AFE_DOMAIN_SIDEBAN0_MON_MASK_SFT                      (0xffffffff << 0)

/* AFE_PROT_SIDEBAND1_MON */
#define AFE_DOMAIN_SIDEBAN1_MON_SFT                           0
#define AFE_DOMAIN_SIDEBAN1_MON_MASK                          0xffffffff
#define AFE_DOMAIN_SIDEBAN1_MON_MASK_SFT                      (0xffffffff << 0)

/* AFE_PROT_SIDEBAND2_MON */
#define AFE_DOMAIN_SIDEBAN2_MON_SFT                           0
#define AFE_DOMAIN_SIDEBAN2_MON_MASK                          0xffffffff
#define AFE_DOMAIN_SIDEBAN2_MON_MASK_SFT                      (0xffffffff << 0)

/* AFE_PROT_SIDEBAND3_MON */
#define AFE_DOMAIN_SIDEBAN3_MON_SFT                           0
#define AFE_DOMAIN_SIDEBAN3_MON_MASK                          0xffffffff
#define AFE_DOMAIN_SIDEBAN3_MON_MASK_SFT                      (0xffffffff << 0)

/* AFE_DOMAIN_SIDEBAND0_MON */
#define AFE_DOMAIN_SIDEBAN0_MON_SFT                           0
#define AFE_DOMAIN_SIDEBAN0_MON_MASK                          0xffffffff
#define AFE_DOMAIN_SIDEBAN0_MON_MASK_SFT                      (0xffffffff << 0)

/* AFE_DOMAIN_SIDEBAND1_MON */
#define AFE_DOMAIN_SIDEBAN1_MON_SFT                           0
#define AFE_DOMAIN_SIDEBAN1_MON_MASK                          0xffffffff
#define AFE_DOMAIN_SIDEBAN1_MON_MASK_SFT                      (0xffffffff << 0)

/* AFE_DOMAIN_SIDEBAND2_MON */
#define AFE_DOMAIN_SIDEBAN2_MON_SFT                           0
#define AFE_DOMAIN_SIDEBAN2_MON_MASK                          0xffffffff
#define AFE_DOMAIN_SIDEBAN2_MON_MASK_SFT                      (0xffffffff << 0)

/* AFE_DOMAIN_SIDEBAND3_MON */
#define AFE_DOMAIN_SIDEBAN3_MON_SFT                           0
#define AFE_DOMAIN_SIDEBAN3_MON_MASK                          0xffffffff
#define AFE_DOMAIN_SIDEBAN3_MON_MASK_SFT                      (0xffffffff << 0)

/* AFE_DOMAIN_SIDEBAND4_MON */
#define AFE_DOMAIN_SIDEBAN0_MON_SFT                           0
#define AFE_DOMAIN_SIDEBAN0_MON_MASK                          0xffffffff
#define AFE_DOMAIN_SIDEBAN0_MON_MASK_SFT                      (0xffffffff << 0)

/* AFE_DOMAIN_SIDEBAND5_MON */
#define AFE_DOMAIN_SIDEBAN1_MON_SFT                           0
#define AFE_DOMAIN_SIDEBAN1_MON_MASK                          0xffffffff
#define AFE_DOMAIN_SIDEBAN1_MON_MASK_SFT                      (0xffffffff << 0)

/* AFE_DOMAIN_SIDEBAND6_MON */
#define AFE_DOMAIN_SIDEBAN2_MON_SFT                           0
#define AFE_DOMAIN_SIDEBAN2_MON_MASK                          0xffffffff
#define AFE_DOMAIN_SIDEBAN2_MON_MASK_SFT                      (0xffffffff << 0)

/* AFE_DOMAIN_SIDEBAND7_MON */
#define AFE_DOMAIN_SIDEBAN3_MON_SFT                           0
#define AFE_DOMAIN_SIDEBAN3_MON_MASK                          0xffffffff
#define AFE_DOMAIN_SIDEBAN3_MON_MASK_SFT                      (0xffffffff << 0)

/* AFE_DOMAIN_SIDEBAND8_MON */
#define AFE_DOMAIN_SIDEBAN2_MON_SFT                           0
#define AFE_DOMAIN_SIDEBAN2_MON_MASK                          0xffffffff
#define AFE_DOMAIN_SIDEBAN2_MON_MASK_SFT                      (0xffffffff << 0)

/* AFE_DOMAIN_SIDEBAND9_MON */
#define AFE_DOMAIN_SIDEBAN3_MON_SFT                           0
#define AFE_DOMAIN_SIDEBAN3_MON_MASK                          0xffffffff
#define AFE_DOMAIN_SIDEBAN3_MON_MASK_SFT                      (0xffffffff << 0)

/* AFE_SECURE_CONN0 */
#define AFE_SPDIFIN_LPBK_CON_MASK_S_SFT                       26
#define AFE_SPDIFIN_LPBK_CON_MASK_S_MASK                      0x3
#define AFE_SPDIFIN_LPBK_CON_MASK_S_MASK_SFT                  (0x3 << 26)
#define AFE_ADDA_DMIC1_SRC_CON0_MASK_S_SFT                    25
#define AFE_ADDA_DMIC1_SRC_CON0_MASK_S_MASK                   0x1
#define AFE_ADDA_DMIC1_SRC_CON0_MASK_S_MASK_SFT               (0x1 << 25)
#define AFE_ADDA_DMIC0_SRC_CON0_MASK_S_SFT                    24
#define AFE_ADDA_DMIC0_SRC_CON0_MASK_S_MASK                   0x1
#define AFE_ADDA_DMIC0_SRC_CON0_MASK_S_MASK_SFT               (0x1 << 24)
#define AFE_ADDA_UL3_SRC_CON0_MASK_S_SFT                      23
#define AFE_ADDA_UL3_SRC_CON0_MASK_S_MASK                     0x1
#define AFE_ADDA_UL3_SRC_CON0_MASK_S_MASK_SFT                 (0x1 << 23)
#define AFE_ADDA_UL2_SRC_CON0_MASK_S_SFT                      22
#define AFE_ADDA_UL2_SRC_CON0_MASK_S_MASK                     0x1
#define AFE_ADDA_UL2_SRC_CON0_MASK_S_MASK_SFT                 (0x1 << 22)
#define AFE_ADDA_UL1_SRC_CON0_MASK_S_SFT                      21
#define AFE_ADDA_UL1_SRC_CON0_MASK_S_MASK                     0x1
#define AFE_ADDA_UL1_SRC_CON0_MASK_S_MASK_SFT                 (0x1 << 21)
#define AFE_ADDA_UL0_SRC_CON0_MASK_S_SFT                      20
#define AFE_ADDA_UL0_SRC_CON0_MASK_S_MASK                     0x1
#define AFE_ADDA_UL0_SRC_CON0_MASK_S_MASK_SFT                 (0x1 << 20)
#define AFE_MRKAIF1_CFG0_MASK_S_SFT                           19
#define AFE_MRKAIF1_CFG0_MASK_S_MASK                          0x1
#define AFE_MRKAIF1_CFG0_MASK_S_MASK_SFT                      (0x1 << 19)
#define AFE_MRKAIF0_CFG0_MASK_S_SFT                           18
#define AFE_MRKAIF0_CFG0_MASK_S_MASK                          0x1
#define AFE_MRKAIF0_CFG0_MASK_S_MASK_SFT                      (0x1 << 18)
#define AFE_TDMIN_CON1_MASK_S_SFT                             17
#define AFE_TDMIN_CON1_MASK_S_MASK                            0x1
#define AFE_TDMIN_CON1_MASK_S_MASK_SFT                        (0x1 << 17)
#define AFE_TDM_CON2_MASK_S_SFT                               16
#define AFE_TDM_CON2_MASK_S_MASK                              0x1
#define AFE_TDM_CON2_MASK_S_MASK_SFT                          (0x1 << 16)
#define AFE_DAIBT_CON_MASK_S_SFT                              14
#define AFE_DAIBT_CON_MASK_S_MASK                             0x3
#define AFE_DAIBT_CON_MASK_S_MASK_SFT                         (0x3 << 14)
#define AFE_MRGIF_CON_MASK_S_SFT                              12
#define AFE_MRGIF_CON_MASK_S_MASK                             0x3
#define AFE_MRGIF_CON_MASK_S_MASK_SFT                         (0x3 << 12)
#define AFE_CONNSYS_I2S_CON_MASK_S_SFT                        11
#define AFE_CONNSYS_I2S_CON_MASK_S_MASK                       0x1
#define AFE_CONNSYS_I2S_CON_MASK_S_MASK_SFT                   (0x1 << 11)
#define AFE_PCM1_INFT_CON0_MASK_S_SFT                         6
#define AFE_PCM1_INFT_CON0_MASK_S_MASK                        0x1f
#define AFE_PCM1_INFT_CON0_MASK_S_MASK_SFT                    (0x1f << 6)
#define AFE_PCM0_INTF_CON1_MASK_S_SFT                         0
#define AFE_PCM0_INTF_CON1_MASK_S_MASK                        0x3f
#define AFE_PCM0_INTF_CON1_MASK_S_MASK_SFT                    (0x3f << 0)

/* AFE_SECURE_CONN_ETDM0 */
#define ETDM_0_3_COWORK_CON2_OUT3_DATA_SEL_SFT                28
#define ETDM_0_3_COWORK_CON2_OUT3_DATA_SEL_MASK               0xf
#define ETDM_0_3_COWORK_CON2_OUT3_DATA_SEL_MASK_SFT           (0xf << 28)
#define ETDM_0_3_COWORK_CON2_OUT2_DATA_SEL_SFT                24
#define ETDM_0_3_COWORK_CON2_OUT2_DATA_SEL_MASK               0xf
#define ETDM_0_3_COWORK_CON2_OUT2_DATA_SEL_MASK_SFT           (0xf << 24)
#define ETDM_0_3_COWORK_CON2_IN1_SDATA1_15_SEL_SFT            20
#define ETDM_0_3_COWORK_CON2_IN1_SDATA1_15_SEL_MASK           0xf
#define ETDM_0_3_COWORK_CON2_IN1_SDATA1_15_SEL_MASK_SFT       (0xf << 20)
#define ETDM_0_3_COWORK_CON2_IN1_SDATA0_SEL_SFT               16
#define ETDM_0_3_COWORK_CON2_IN1_SDATA0_SEL_MASK              0xf
#define ETDM_0_3_COWORK_CON2_IN1_SDATA0_SEL_MASK_SFT          (0xf << 16)
#define ETDM_0_3_COWORK_CON2_IN0_SDATA1_15_SEL_SFT            12
#define ETDM_0_3_COWORK_CON2_IN0_SDATA1_15_SEL_MASK           0xf
#define ETDM_0_3_COWORK_CON2_IN0_SDATA1_15_SEL_MASK_SFT       (0xf << 12)
#define ETDM_0_3_COWORK_CON2_IN0_SDATA0_SEL_SFT               8
#define ETDM_0_3_COWORK_CON2_IN0_SDATA0_SEL_MASK              0xf
#define ETDM_0_3_COWORK_CON2_IN0_SDATA0_SEL_MASK_SFT          (0xf << 8)
#define ETDM_0_3_COWORK_CON2_OUT1_DATA_SEL_SFT                4
#define ETDM_0_3_COWORK_CON2_OUT1_DATA_SEL_MASK               0xf
#define ETDM_0_3_COWORK_CON2_OUT1_DATA_SEL_MASK_SFT           (0xf << 4)
#define ETDM_0_3_COWORK_CON2_OUT0_DATA_SEL_SFT                0
#define ETDM_0_3_COWORK_CON2_OUT0_DATA_SEL_MASK               0xf
#define ETDM_0_3_COWORK_CON2_OUT0_DATA_SEL_MASK_SFT           (0xf << 0)

/* AFE_SECURE_CONN_ETDM1 */
#define ETDM_4_7_COWORK_CON1_IN4_SDATA1_15_SEL_SFT            28
#define ETDM_4_7_COWORK_CON1_IN4_SDATA1_15_SEL_MASK           0xf
#define ETDM_4_7_COWORK_CON1_IN4_SDATA1_15_SEL_MASK_SFT       (0xf << 28)
#define ETDM_4_7_COWORK_CON1_IN4_SDATA0_SEL_SFT               24
#define ETDM_4_7_COWORK_CON1_IN4_SDATA0_SEL_MASK              0xf
#define ETDM_4_7_COWORK_CON1_IN4_SDATA0_SEL_MASK_SFT          (0xf << 24)
#define ETDM_4_7_COWORK_CON1_OUT5_DATA_SEL_SFT                20
#define ETDM_4_7_COWORK_CON1_OUT5_DATA_SEL_MASK               0xf
#define ETDM_4_7_COWORK_CON1_OUT5_DATA_SEL_MASK_SFT           (0xf << 20)
#define ETDM_4_7_COWORK_CON1_OUT4_DATA_SEL_SFT                16
#define ETDM_4_7_COWORK_CON1_OUT4_DATA_SEL_MASK               0xf
#define ETDM_4_7_COWORK_CON1_OUT4_DATA_SEL_MASK_SFT           (0xf << 16)
#define ETDM_4_7_COWORK_CON1_IN3_SDATA1_15_SEL_SFT            12
#define ETDM_4_7_COWORK_CON1_IN3_SDATA1_15_SEL_MASK           0xf
#define ETDM_4_7_COWORK_CON1_IN3_SDATA1_15_SEL_MASK_SFT       (0xf << 12)
#define ETDM_4_7_COWORK_CON1_IN3_SDATA0_SEL_SFT               8
#define ETDM_4_7_COWORK_CON1_IN3_SDATA0_SEL_MASK              0xf
#define ETDM_4_7_COWORK_CON1_IN3_SDATA0_SEL_MASK_SFT          (0xf << 8)
#define ETDM_4_7_COWORK_CON1_IN2_SDATA1_15_SEL_SFT            4
#define ETDM_4_7_COWORK_CON1_IN2_SDATA1_15_SEL_MASK           0xf
#define ETDM_4_7_COWORK_CON1_IN2_SDATA1_15_SEL_MASK_SFT       (0xf << 4)
#define ETDM_4_7_COWORK_CON1_IN2_SDATA0_SEL_SFT               0
#define ETDM_4_7_COWORK_CON1_IN2_SDATA0_SEL_MASK              0xf
#define ETDM_4_7_COWORK_CON1_IN2_SDATA0_SEL_MASK_SFT          (0xf << 0)

/* AFE_SECURE_CONN_ETDM2 */
#define ETDM_4_7_COWORK_CON3_IN7_SDATA1_15_SEL_SFT                            28
#define ETDM_4_7_COWORK_CON3_IN7_SDATA1_15_SEL_MASK                           0xf
#define ETDM_4_7_COWORK_CON3_IN7_SDATA1_15_SEL_MASK_SFT                       (0xf << 28)
#define ETDM_4_7_COWORK_CON3_IN7_SDATA0_SEL_SFT                               24
#define ETDM_4_7_COWORK_CON3_IN7_SDATA0_SEL_MASK                              0xf
#define ETDM_4_7_COWORK_CON3_IN7_SDATA0_SEL_MASK_SFT                          (0xf << 24)
#define ETDM_4_7_COWORK_CON3_IN6_SDATA1_15_SEL_SFT                            20
#define ETDM_4_7_COWORK_CON3_IN6_SDATA1_15_SEL_MASK                           0xf
#define ETDM_4_7_COWORK_CON3_IN6_SDATA1_15_SEL_MASK_SFT                       (0xf << 20)
#define ETDM_4_7_COWORK_CON3_IN6_SDATA0_SEL_SFT                               16
#define ETDM_4_7_COWORK_CON3_IN6_SDATA0_SEL_MASK                              0xf
#define ETDM_4_7_COWORK_CON3_IN6_SDATA0_SEL_MASK_SFT                          (0xf << 16)
#define ETDM_4_7_COWORK_CON3_OUT7_DATA_SEL_SFT                                12
#define ETDM_4_7_COWORK_CON3_OUT7_DATA_SEL_MASK                               0xf
#define ETDM_4_7_COWORK_CON3_OUT7_DATA_SEL_MASK_SFT                           (0xf << 12)
#define ETDM_4_7_COWORK_CON3_OUT6_DATA_SEL_SFT                                8
#define ETDM_4_7_COWORK_CON3_OUT6_DATA_SEL_MASK                               0xf
#define ETDM_4_7_COWORK_CON3_OUT6_DATA_SEL_MASK_SFT                           (0xf << 8)
#define ETDM_4_7_COWORK_CON3_IN5_SDATA1_15_SEL_SFT                            4
#define ETDM_4_7_COWORK_CON3_IN5_SDATA1_15_SEL_MASK                           0xf
#define ETDM_4_7_COWORK_CON3_IN5_SDATA1_15_SEL_MASK_SFT                       (0xf << 4)
#define ETDM_4_7_COWORK_CON3_IN5_SDATA0_SEL_SFT                               0
#define ETDM_4_7_COWORK_CON3_IN5_SDATA0_SEL_MASK                              0xf
#define ETDM_4_7_COWORK_CON3_IN5_SDATA0_SEL_MASK_SFT                          (0xf << 0)

/* AFE_SECURE_SRAM_CON0 */
#define SRAM_READ_EN15_NS_SFT                                 31
#define SRAM_READ_EN15_NS_MASK                                0x1
#define SRAM_READ_EN15_NS_MASK_SFT                            (0x1 << 31)
#define SRAM_WRITE_EN15_NS_SFT                                30
#define SRAM_WRITE_EN15_NS_MASK                               0x1
#define SRAM_WRITE_EN15_NS_MASK_SFT                           (0x1 << 30)
#define SRAM_READ_EN14_NS_SFT                                 29
#define SRAM_READ_EN14_NS_MASK                                0x1
#define SRAM_READ_EN14_NS_MASK_SFT                            (0x1 << 29)
#define SRAM_WRITE_EN14_NS_SFT                                28
#define SRAM_WRITE_EN14_NS_MASK                               0x1
#define SRAM_WRITE_EN14_NS_MASK_SFT                           (0x1 << 28)
#define SRAM_READ_EN13_NS_SFT                                 27
#define SRAM_READ_EN13_NS_MASK                                0x1
#define SRAM_READ_EN13_NS_MASK_SFT                            (0x1 << 27)
#define SRAM_WRITE_EN13_NS_SFT                                26
#define SRAM_WRITE_EN13_NS_MASK                               0x1
#define SRAM_WRITE_EN13_NS_MASK_SFT                           (0x1 << 26)
#define SRAM_READ_EN12_NS_SFT                                 25
#define SRAM_READ_EN12_NS_MASK                                0x1
#define SRAM_READ_EN12_NS_MASK_SFT                            (0x1 << 25)
#define SRAM_WRITE_EN12_NS_SFT                                24
#define SRAM_WRITE_EN12_NS_MASK                               0x1
#define SRAM_WRITE_EN12_NS_MASK_SFT                           (0x1 << 24)
#define SRAM_READ_EN11_NS_SFT                                 23
#define SRAM_READ_EN11_NS_MASK                                0x1
#define SRAM_READ_EN11_NS_MASK_SFT                            (0x1 << 23)
#define SRAM_WRITE_EN11_NS_SFT                                22
#define SRAM_WRITE_EN11_NS_MASK                               0x1
#define SRAM_WRITE_EN11_NS_MASK_SFT                           (0x1 << 22)
#define SRAM_READ_EN10_NS_SFT                                 21
#define SRAM_READ_EN10_NS_MASK                                0x1
#define SRAM_READ_EN10_NS_MASK_SFT                            (0x1 << 21)
#define SRAM_WRITE_EN10_NS_SFT                                20
#define SRAM_WRITE_EN10_NS_MASK                               0x1
#define SRAM_WRITE_EN10_NS_MASK_SFT                           (0x1 << 20)
#define SRAM_READ_EN9_NS_SFT                                  19
#define SRAM_READ_EN9_NS_MASK                                 0x1
#define SRAM_READ_EN9_NS_MASK_SFT                             (0x1 << 19)
#define SRAM_WRITE_EN9_NS_SFT                                 18
#define SRAM_WRITE_EN9_NS_MASK                                0x1
#define SRAM_WRITE_EN9_NS_MASK_SFT                            (0x1 << 18)
#define SRAM_READ_EN8_NS_SFT                                  17
#define SRAM_READ_EN8_NS_MASK                                 0x1
#define SRAM_READ_EN8_NS_MASK_SFT                             (0x1 << 17)
#define SRAM_WRITE_EN8_NS_SFT                                 16
#define SRAM_WRITE_EN8_NS_MASK                                0x1
#define SRAM_WRITE_EN8_NS_MASK_SFT                            (0x1 << 16)
#define SRAM_READ_EN7_NS_SFT                                  15
#define SRAM_READ_EN7_NS_MASK                                 0x1
#define SRAM_READ_EN7_NS_MASK_SFT                             (0x1 << 15)
#define SRAM_WRITE_EN7_NS_SFT                                 14
#define SRAM_WRITE_EN7_NS_MASK                                0x1
#define SRAM_WRITE_EN7_NS_MASK_SFT                            (0x1 << 14)
#define SRAM_READ_EN6_NS_SFT                                  13
#define SRAM_READ_EN6_NS_MASK                                 0x1
#define SRAM_READ_EN6_NS_MASK_SFT                             (0x1 << 13)
#define SRAM_WRITE_EN6_NS_SFT                                 12
#define SRAM_WRITE_EN6_NS_MASK                                0x1
#define SRAM_WRITE_EN6_NS_MASK_SFT                            (0x1 << 12)
#define SRAM_READ_EN5_NS_SFT                                  11
#define SRAM_READ_EN5_NS_MASK                                 0x1
#define SRAM_READ_EN5_NS_MASK_SFT                             (0x1 << 11)
#define SRAM_WRITE_EN5_NS_SFT                                 10
#define SRAM_WRITE_EN5_NS_MASK                                0x1
#define SRAM_WRITE_EN5_NS_MASK_SFT                            (0x1 << 10)
#define SRAM_READ_EN4_NS_SFT                                  9
#define SRAM_READ_EN4_NS_MASK                                 0x1
#define SRAM_READ_EN4_NS_MASK_SFT                             (0x1 << 9)
#define SRAM_WRITE_EN4_NS_SFT                                 8
#define SRAM_WRITE_EN4_NS_MASK                                0x1
#define SRAM_WRITE_EN4_NS_MASK_SFT                            (0x1 << 8)
#define SRAM_READ_EN3_NS_SFT                                  7
#define SRAM_READ_EN3_NS_MASK                                 0x1
#define SRAM_READ_EN3_NS_MASK_SFT                             (0x1 << 7)
#define SRAM_WRITE_EN3_NS_SFT                                 6
#define SRAM_WRITE_EN3_NS_MASK                                0x1
#define SRAM_WRITE_EN3_NS_MASK_SFT                            (0x1 << 6)
#define SRAM_READ_EN2_NS_SFT                                  5
#define SRAM_READ_EN2_NS_MASK                                 0x1
#define SRAM_READ_EN2_NS_MASK_SFT                             (0x1 << 5)
#define SRAM_WRITE_EN2_NS_SFT                                 4
#define SRAM_WRITE_EN2_NS_MASK                                0x1
#define SRAM_WRITE_EN2_NS_MASK_SFT                            (0x1 << 4)
#define SRAM_READ_EN1_NS_SFT                                  3
#define SRAM_READ_EN1_NS_MASK                                 0x1
#define SRAM_READ_EN1_NS_MASK_SFT                             (0x1 << 3)
#define SRAM_WRITE_EN1_NS_SFT                                 2
#define SRAM_WRITE_EN1_NS_MASK                                0x1
#define SRAM_WRITE_EN1_NS_MASK_SFT                            (0x1 << 2)
#define SRAM_READ_EN0_NS_SFT                                  1
#define SRAM_READ_EN0_NS_MASK                                 0x1
#define SRAM_READ_EN0_NS_MASK_SFT                             (0x1 << 1)
#define SRAM_WRITE_EN0_NS_SFT                                 0
#define SRAM_WRITE_EN0_NS_MASK                                0x1
#define SRAM_WRITE_EN0_NS_MASK_SFT                            (0x1 << 0)

/* AFE_SECURE_SRAM_CON1 */
#define SRAM_READ_EN15_S_SFT                                  31
#define SRAM_READ_EN15_S_MASK                                 0x1
#define SRAM_READ_EN15_S_MASK_SFT                             (0x1 << 31)
#define SRAM_WRITE_EN15_S_SFT                                 30
#define SRAM_WRITE_EN15_S_MASK                                0x1
#define SRAM_WRITE_EN15_S_MASK_SFT                            (0x1 << 30)
#define SRAM_READ_EN14_S_SFT                                  29
#define SRAM_READ_EN14_S_MASK                                 0x1
#define SRAM_READ_EN14_S_MASK_SFT                             (0x1 << 29)
#define SRAM_WRITE_EN14_S_SFT                                 28
#define SRAM_WRITE_EN14_S_MASK                                0x1
#define SRAM_WRITE_EN14_S_MASK_SFT                            (0x1 << 28)
#define SRAM_READ_EN13_S_SFT                                  27
#define SRAM_READ_EN13_S_MASK                                 0x1
#define SRAM_READ_EN13_S_MASK_SFT                             (0x1 << 27)
#define SRAM_WRITE_EN13_S_SFT                                 26
#define SRAM_WRITE_EN13_S_MASK                                0x1
#define SRAM_WRITE_EN13_S_MASK_SFT                            (0x1 << 26)
#define SRAM_READ_EN12_S_SFT                                  25
#define SRAM_READ_EN12_S_MASK                                 0x1
#define SRAM_READ_EN12_S_MASK_SFT                             (0x1 << 25)
#define SRAM_WRITE_EN12_S_SFT                                 24
#define SRAM_WRITE_EN12_S_MASK                                0x1
#define SRAM_WRITE_EN12_S_MASK_SFT                            (0x1 << 24)
#define SRAM_READ_EN11_S_SFT                                  23
#define SRAM_READ_EN11_S_MASK                                 0x1
#define SRAM_READ_EN11_S_MASK_SFT                             (0x1 << 23)
#define SRAM_WRITE_EN11_S_SFT                                 22
#define SRAM_WRITE_EN11_S_MASK                                0x1
#define SRAM_WRITE_EN11_S_MASK_SFT                            (0x1 << 22)
#define SRAM_READ_EN10_S_SFT                                  21
#define SRAM_READ_EN10_S_MASK                                 0x1
#define SRAM_READ_EN10_S_MASK_SFT                             (0x1 << 21)
#define SRAM_WRITE_EN10_S_SFT                                 20
#define SRAM_WRITE_EN10_S_MASK                                0x1
#define SRAM_WRITE_EN10_S_MASK_SFT                            (0x1 << 20)
#define SRAM_READ_EN9_S_SFT                                   19
#define SRAM_READ_EN9_S_MASK                                  0x1
#define SRAM_READ_EN9_S_MASK_SFT                              (0x1 << 19)
#define SRAM_WRITE_EN9_S_SFT                                  18
#define SRAM_WRITE_EN9_S_MASK                                 0x1
#define SRAM_WRITE_EN9_S_MASK_SFT                             (0x1 << 18)
#define SRAM_READ_EN8_S_SFT                                   17
#define SRAM_READ_EN8_S_MASK                                  0x1
#define SRAM_READ_EN8_S_MASK_SFT                              (0x1 << 17)
#define SRAM_WRITE_EN8_S_SFT                                  16
#define SRAM_WRITE_EN8_S_MASK                                 0x1
#define SRAM_WRITE_EN8_S_MASK_SFT                             (0x1 << 16)
#define SRAM_READ_EN7_S_SFT                                   15
#define SRAM_READ_EN7_S_MASK                                  0x1
#define SRAM_READ_EN7_S_MASK_SFT                              (0x1 << 15)
#define SRAM_WRITE_EN7_S_SFT                                  14
#define SRAM_WRITE_EN7_S_MASK                                 0x1
#define SRAM_WRITE_EN7_S_MASK_SFT                             (0x1 << 14)
#define SRAM_READ_EN6_S_SFT                                   13
#define SRAM_READ_EN6_S_MASK                                  0x1
#define SRAM_READ_EN6_S_MASK_SFT                              (0x1 << 13)
#define SRAM_WRITE_EN6_S_SFT                                  12
#define SRAM_WRITE_EN6_S_MASK                                 0x1
#define SRAM_WRITE_EN6_S_MASK_SFT                             (0x1 << 12)
#define SRAM_READ_EN5_S_SFT                                   11
#define SRAM_READ_EN5_S_MASK                                  0x1
#define SRAM_READ_EN5_S_MASK_SFT                              (0x1 << 11)
#define SRAM_WRITE_EN5_S_SFT                                  10
#define SRAM_WRITE_EN5_S_MASK                                 0x1
#define SRAM_WRITE_EN5_S_MASK_SFT                             (0x1 << 10)
#define SRAM_READ_EN4_S_SFT                                   9
#define SRAM_READ_EN4_S_MASK                                  0x1
#define SRAM_READ_EN4_S_MASK_SFT                              (0x1 << 9)
#define SRAM_WRITE_EN4_S_SFT                                  8
#define SRAM_WRITE_EN4_S_MASK                                 0x1
#define SRAM_WRITE_EN4_S_MASK_SFT                             (0x1 << 8)
#define SRAM_READ_EN3_S_SFT                                   7
#define SRAM_READ_EN3_S_MASK                                  0x1
#define SRAM_READ_EN3_S_MASK_SFT                              (0x1 << 7)
#define SRAM_WRITE_EN3_S_SFT                                  6
#define SRAM_WRITE_EN3_S_MASK                                 0x1
#define SRAM_WRITE_EN3_S_MASK_SFT                             (0x1 << 6)
#define SRAM_READ_EN2_S_SFT                                   5
#define SRAM_READ_EN2_S_MASK                                  0x1
#define SRAM_READ_EN2_S_MASK_SFT                              (0x1 << 5)
#define SRAM_WRITE_EN2_S_SFT                                  4
#define SRAM_WRITE_EN2_S_MASK                                 0x1
#define SRAM_WRITE_EN2_S_MASK_SFT                             (0x1 << 4)
#define SRAM_READ_EN1_S_SFT                                   3
#define SRAM_READ_EN1_S_MASK                                  0x1
#define SRAM_READ_EN1_S_MASK_SFT                              (0x1 << 3)
#define SRAM_WRITE_EN1_S_SFT                                  2
#define SRAM_WRITE_EN1_S_MASK                                 0x1
#define SRAM_WRITE_EN1_S_MASK_SFT                             (0x1 << 2)
#define SRAM_READ_EN0_S_SFT                                   1
#define SRAM_READ_EN0_S_MASK                                  0x1
#define SRAM_READ_EN0_S_MASK_SFT                              (0x1 << 1)
#define SRAM_WRITE_EN0_S_SFT                                  0
#define SRAM_WRITE_EN0_S_MASK                                 0x1
#define SRAM_WRITE_EN0_S_MASK_SFT                             (0x1 << 0)

/* AFE_SE_CONN_INPUT_MASK0 */
#define SECURE_INTRCONN_I0_I31_S_SFT                          0
#define SECURE_INTRCONN_I0_I31_S_MASK                         0xffffffff
#define SECURE_INTRCONN_I0_I31_S_MASK_SFT                     (0xffffffff << 0)

/* AFE_SE_CONN_INPUT_MASK1 */
#define SECURE_INTRCONN_I32_I63_S_SFT                         0
#define SECURE_INTRCONN_I32_I63_S_MASK                        0xffffffff
#define SECURE_INTRCONN_I32_I63_S_MASK_SFT                    (0xffffffff << 0)

/* AFE_SE_CONN_INPUT_MASK2 */
#define SECURE_INTRCONN_I64_I95_S_SFT                         0
#define SECURE_INTRCONN_I64_I95_S_MASK                        0xffffffff
#define SECURE_INTRCONN_I64_I95_S_MASK_SFT                    (0xffffffff << 0)

/* AFE_SE_CONN_INPUT_MASK3 */
#define SECURE_INTRCONN_I96_I127_S_SFT                        0
#define SECURE_INTRCONN_I96_I127_S_MASK                       0xffffffff
#define SECURE_INTRCONN_I96_I127_S_MASK_SFT                   (0xffffffff << 0)

/* AFE_SE_CONN_INPUT_MASK4 */
#define SECURE_INTRCONN_I128_I159_S_SFT                       0
#define SECURE_INTRCONN_I128_I159_S_MASK                      0xffffffff
#define SECURE_INTRCONN_I128_I159_S_MASK_SFT                  (0xffffffff << 0)

/* AFE_SE_CONN_INPUT_MASK5 */
#define SECURE_INTRCONN_I160_I191_S_SFT                       0
#define SECURE_INTRCONN_I160_I191_S_MASK                      0xffffffff
#define SECURE_INTRCONN_I160_I191_S_MASK_SFT                  (0xffffffff << 0)

/* AFE_SE_CONN_INPUT_MASK6 */
#define SECURE_INTRCONN_I192_I223_S_SFT                       0
#define SECURE_INTRCONN_I192_I223_S_MASK                      0xffffffff
#define SECURE_INTRCONN_I192_I223_S_MASK_SFT                  (0xffffffff << 0)

/* AFE_SE_CONN_INPUT_MASK7 */
#define SECURE_INTRCONN_I224_I256_S_SFT                       0
#define SECURE_INTRCONN_I224_I256_S_MASK                      0xffffffff
#define SECURE_INTRCONN_I224_I256_S_MASK_SFT                  (0xffffffff << 0)

/* AFE_NON_SE_CONN_INPUT_MASK0 */
#define NORMAL_INTRCONN_I0_I31_S_SFT                          0
#define NORMAL_INTRCONN_I0_I31_S_MASK                         0xffffffff
#define NORMAL_INTRCONN_I0_I31_S_MASK_SFT                     (0xffffffff << 0)

/* AFE_NON_SE_CONN_INPUT_MASK1 */
#define NORMAL_INTRCONN_I32_I63_S_SFT                         0
#define NORMAL_INTRCONN_I32_I63_S_MASK                        0xffffffff
#define NORMAL_INTRCONN_I32_I63_S_MASK_SFT                    (0xffffffff << 0)

/* AFE_NON_SE_CONN_INPUT_MASK2 */
#define NORMAL_INTRCONN_I64_I95_S_SFT                         0
#define NORMAL_INTRCONN_I64_I95_S_MASK                        0xffffffff
#define NORMAL_INTRCONN_I64_I95_S_MASK_SFT                    (0xffffffff << 0)

/* AFE_NON_SE_CONN_INPUT_MASK3 */
#define NORMAL_INTRCONN_I96_I127_S_SFT                        0
#define NORMAL_INTRCONN_I96_I127_S_MASK                       0xffffffff
#define NORMAL_INTRCONN_I96_I127_S_MASK_SFT                   (0xffffffff << 0)

/* AFE_NON_SE_CONN_INPUT_MASK4 */
#define NORMAL_INTRCONN_I128_I159_S_SFT                       0
#define NORMAL_INTRCONN_I128_I159_S_MASK                      0xffffffff
#define NORMAL_INTRCONN_I128_I159_S_MASK_SFT                  (0xffffffff << 0)

/* AFE_NON_SE_CONN_INPUT_MASK5 */
#define NORMAL_INTRCONN_I160_I191_S_SFT                       0
#define NORMAL_INTRCONN_I160_I191_S_MASK                      0xffffffff
#define NORMAL_INTRCONN_I160_I191_S_MASK_SFT                  (0xffffffff << 0)

/* AFE_NON_SE_CONN_INPUT_MASK6 */
#define NORMAL_INTRCONN_I192_I223_S_SFT                       0
#define NORMAL_INTRCONN_I192_I223_S_MASK                      0xffffffff
#define NORMAL_INTRCONN_I192_I223_S_MASK_SFT                  (0xffffffff << 0)

/* AFE_NON_SE_CONN_INPUT_MASK7 */
#define NORMAL_INTRCONN_I224_I256_S_SFT                       0
#define NORMAL_INTRCONN_I224_I256_S_MASK                      0xffffffff
#define NORMAL_INTRCONN_I224_I256_S_MASK_SFT                  (0xffffffff << 0)

/* AFE_SE_CONN_OUTPUT_SEL0 */
#define SECURE_INTRCONN_O0_O31_S_SFT                          0
#define SECURE_INTRCONN_O0_O31_S_MASK                         0xffffffff
#define SECURE_INTRCONN_O0_O31_S_MASK_SFT                     (0xffffffff << 0)

/* AFE_SE_CONN_OUTPUT_SEL1 */
#define SECURE_INTRCONN_O32_O63_S_SFT                         0
#define SECURE_INTRCONN_O32_O63_S_MASK                        0xffffffff
#define SECURE_INTRCONN_O32_O63_S_MASK_SFT                    (0xffffffff << 0)

/* AFE_SE_CONN_OUTPUT_SEL2 */
#define SECURE_INTRCONN_O64_O95_S_SFT                         0
#define SECURE_INTRCONN_O64_O95_S_MASK                        0xffffffff
#define SECURE_INTRCONN_O64_O95_S_MASK_SFT                    (0xffffffff << 0)

/* AFE_SE_CONN_OUTPUT_SEL3 */
#define SECURE_INTRCONN_O96_O127_S_SFT                        0
#define SECURE_INTRCONN_O96_O127_S_MASK                       0xffffffff
#define SECURE_INTRCONN_O96_O127_S_MASK_SFT                   (0xffffffff << 0)

/* AFE_SE_CONN_OUTPUT_SEL4 */
#define SECURE_INTRCONN_O128_O159_S_SFT                       0
#define SECURE_INTRCONN_O128_O159_S_MASK                      0xffffffff
#define SECURE_INTRCONN_O128_O159_S_MASK_SFT                  (0xffffffff << 0)

/* AFE_SE_CONN_OUTPUT_SEL5 */
#define SECURE_INTRCONN_O160_O191_S_SFT                       0
#define SECURE_INTRCONN_O160_O191_S_MASK                      0xffffffff
#define SECURE_INTRCONN_O160_O191_S_MASK_SFT                  (0xffffffff << 0)

/* AFE_SE_CONN_OUTPUT_SEL6 */
#define SECURE_INTRCONN_O192_O223_S_SFT                       0
#define SECURE_INTRCONN_O192_O223_S_MASK                      0xffffffff
#define SECURE_INTRCONN_O192_O223_S_MASK_SFT                  (0xffffffff << 0)

/* AFE_SE_CONN_OUTPUT_SEL7 */
#define SECURE_INTRCONN_O224_O256_S_SFT                       0
#define SECURE_INTRCONN_O224_O256_S_MASK                      0xffffffff
#define SECURE_INTRCONN_O224_O256_S_MASK_SFT                  (0xffffffff << 0)

/* AFE_PCM0_INTF_CON1_MASK_MON */
#define AFE_PCM0_INTF_CON1_MASK_MON_SFT                       0
#define AFE_PCM0_INTF_CON1_MASK_MON_MASK                      0xffffffff
#define AFE_PCM0_INTF_CON1_MASK_MON_MASK_SFT                  (0xffffffff << 0)

/* AFE_PCM0_INTF_CON0_MASK_MON */
#define AFE_PCM0_INTF_CON0_MASK_MON_SFT                       0
#define AFE_PCM0_INTF_CON0_MASK_MON_MASK                      0xffffffff
#define AFE_PCM0_INTF_CON0_MASK_MON_MASK_SFT                  (0xffffffff << 0)

/* AFE_CONNSYS_I2S_CON_MASK_MON */
#define AFE_CONNSYS_I2S_CON_MASK_MON_SFT                      0
#define AFE_CONNSYS_I2S_CON_MASK_MON_MASK                     0xffffffff
#define AFE_CONNSYS_I2S_CON_MASK_MON_MASK_SFT                 (0xffffffff << 0)

/* AFE_TDM_CON2_MASK_MON */
#define AFE_TDM_CON2_MASK_MON_SFT                             0
#define AFE_TDM_CON2_MASK_MON_MASK                            0xffffffff
#define AFE_TDM_CON2_MASK_MON_MASK_SFT                        (0xffffffff << 0)

/* AFE_MTKAIF0_CFG0_MASK_MON */
#define AFE_MTKAIF0_CFG0_MASK_MON_SFT                         0
#define AFE_MTKAIF0_CFG0_MASK_MON_MASK                        0xffffffff
#define AFE_MTKAIF0_CFG0_MASK_MON_MASK_SFT                    (0xffffffff << 0)

/* AFE_MTKAIF1_CFG0_MASK_MON */
#define AFE_MTKAIF1_CFG0_MASK_MON_SFT                         0
#define AFE_MTKAIF1_CFG0_MASK_MON_MASK                        0xffffffff
#define AFE_MTKAIF1_CFG0_MASK_MON_MASK_SFT                    (0xffffffff << 0)

/* AFE_ADDA_UL0_SRC_CON0_MASK_MON */
#define AFE_ADDA_UL0_SRC_CON0_MASK_MON_SFT                    0
#define AFE_ADDA_UL0_SRC_CON0_MASK_MON_MASK                   0xffffffff
#define AFE_ADDA_UL0_SRC_CON0_MASK_MON_MASK_SFT               (0xffffffff << 0)

/* AFE_ADDA_UL1_SRC_CON0_MASK_MON */
#define AFE_ADDA_UL1_SRC_CON0_MASK_MON_SFT                    0
#define AFE_ADDA_UL1_SRC_CON0_MASK_MON_MASK                   0xffffffff
#define AFE_ADDA_UL1_SRC_CON0_MASK_MON_MASK_SFT               (0xffffffff << 0)

/* AFE_ADDA_UL2_SRC_CON0_MASK_MON */
#define AFE_ADDA_UL2_SRC_CON0_MASK_MON_SFT                    0
#define AFE_ADDA_UL2_SRC_CON0_MASK_MON_MASK                   0xffffffff
#define AFE_ADDA_UL2_SRC_CON0_MASK_MON_MASK_SFT               (0xffffffff << 0)

/* AFE_ASRC_NEW_CON0 */
#define ONE_HEART_SFT                                         31
#define ONE_HEART_MASK                                        0x1
#define ONE_HEART_MASK_SFT                                    (0x1 << 31)
#define CHSET0_OFS_ONE_HEART_DISABLE_SFT                      30
#define CHSET0_OFS_ONE_HEART_DISABLE_MASK                     0x1
#define CHSET0_OFS_ONE_HEART_DISABLE_MASK_SFT                 (0x1 << 30)
#define USE_SHORT_DELAY_COEFF_SFT                             29
#define USE_SHORT_DELAY_COEFF_MASK                            0x1
#define USE_SHORT_DELAY_COEFF_MASK_SFT                        (0x1 << 29)
#define CHSET0_O16BIT_SFT                                     19
#define CHSET0_O16BIT_MASK                                    0x1
#define CHSET0_O16BIT_MASK_SFT                                (0x1 << 19)
#define CHSET0_CLR_IIR_HISTORY_SFT                            17
#define CHSET0_CLR_IIR_HISTORY_MASK                           0x1
#define CHSET0_CLR_IIR_HISTORY_MASK_SFT                       (0x1 << 17)
#define CHSET0_IS_MONO_SFT                                    16
#define CHSET0_IS_MONO_MASK                                   0x1
#define CHSET0_IS_MONO_MASK_SFT                               (0x1 << 16)
#define CHSET0_OFS_SEL_SFT                                    14
#define CHSET0_OFS_SEL_MASK                                   0x3
#define CHSET0_OFS_SEL_MASK_SFT                               (0x3 << 14)
#define CHSET0_IFS_SEL_SFT                                    12
#define CHSET0_IFS_SEL_MASK                                   0x3
#define CHSET0_IFS_SEL_MASK_SFT                               (0x3 << 12)
#define CHSET0_IIR_EN_SFT                                     11
#define CHSET0_IIR_EN_MASK                                    0x1
#define CHSET0_IIR_EN_MASK_SFT                                (0x1 << 11)
#define CHSET0_IIR_STAGE_SFT                                  8
#define CHSET0_IIR_STAGE_MASK                                 0x7
#define CHSET0_IIR_STAGE_MASK_SFT                             (0x7 << 8)
#define ASM_ON_MOD_SFT                                        7
#define ASM_ON_MOD_MASK                                       0x1
#define ASM_ON_MOD_MASK_SFT                                   (0x1 << 7)
#define CHSET_STR_CLR_SFT                                     4
#define CHSET_STR_CLR_MASK                                    0x1
#define CHSET_STR_CLR_MASK_SFT                                (0x1 << 4)
#define CHSET_ON_SFT                                          2
#define CHSET_ON_MASK                                         0x1
#define CHSET_ON_MASK_SFT                                     (0x1 << 2)
#define COEFF_SRAM_CTRL_SFT                                   1
#define COEFF_SRAM_CTRL_MASK                                  0x1
#define COEFF_SRAM_CTRL_MASK_SFT                              (0x1 << 1)
#define ASM_ON_SFT                                            0
#define ASM_ON_MASK                                           0x1
#define ASM_ON_MASK_SFT                                       (0x1 << 0)

/* AFE_ASRC_NEW_CON1 */
#define ASM_FREQ_0_SFT                                        0
#define ASM_FREQ_0_MASK                                       0xffffff
#define ASM_FREQ_0_MASK_SFT                                   (0xffffff << 0)

/* AFE_ASRC_NEW_CON2 */
#define ASM_FREQ_1_SFT                                        0
#define ASM_FREQ_1_MASK                                       0xffffff
#define ASM_FREQ_1_MASK_SFT                                   (0xffffff << 0)

/* AFE_ASRC_NEW_CON3 */
#define ASM_FREQ_2_SFT                                        0
#define ASM_FREQ_2_MASK                                       0xffffff
#define ASM_FREQ_2_MASK_SFT                                   (0xffffff << 0)

/* AFE_ASRC_NEW_CON4 */
#define ASM_FREQ_3_SFT                                        0
#define ASM_FREQ_3_MASK                                       0xffffff
#define ASM_FREQ_3_MASK_SFT                                   (0xffffff << 0)

/* AFE_ASRC_NEW_CON5 */
#define OUT_EN_SEL_DOMAIN_SFT                                 29
#define OUT_EN_SEL_DOMAIN_MASK                                0x7
#define OUT_EN_SEL_DOMAIN_MASK_SFT                            (0x7 << 29)
#define OUT_EN_SEL_FS_SFT                                     24
#define OUT_EN_SEL_FS_MASK                                    0x1f
#define OUT_EN_SEL_FS_MASK_SFT                                (0x1f << 24)
#define IN_EN_SEL_DOMAIN_SFT                                  21
#define IN_EN_SEL_DOMAIN_MASK                                 0x7
#define IN_EN_SEL_DOMAIN_MASK_SFT                             (0x7 << 21)
#define IN_EN_SEL_FS_SFT                                      16
#define IN_EN_SEL_FS_MASK                                     0x1f
#define IN_EN_SEL_FS_MASK_SFT                                 (0x1f << 16)
#define RESULT_SEL_SFT                                        8
#define RESULT_SEL_MASK                                       0x7
#define RESULT_SEL_MASK_SFT                                   (0x7 << 8)
#define CALI_CK_SEL_SFT                                       4
#define CALI_CK_SEL_MASK                                      0x7
#define CALI_CK_SEL_MASK_SFT                                  (0x7 << 4)
#define CALI_LRCK_SEL_SFT                                     1
#define CALI_LRCK_SEL_MASK                                    0x7
#define CALI_LRCK_SEL_MASK_SFT                                (0x7 << 1)
#define SOFT_RESET_SFT                                        0
#define SOFT_RESET_MASK                                       0x1
#define SOFT_RESET_MASK_SFT                                   (0x1 << 0)

/* AFE_ASRC_NEW_CON6 */
#define FREQ_CALI_CYCLE_SFT                                   16
#define FREQ_CALI_CYCLE_MASK                                  0xffff
#define FREQ_CALI_CYCLE_MASK_SFT                              (0xffff << 16)
#define FREQ_CALI_AUTORST_EN_SFT                              15
#define FREQ_CALI_AUTORST_EN_MASK                             0x1
#define FREQ_CALI_AUTORST_EN_MASK_SFT                         (0x1 << 15)
#define CALI_AUTORST_DETECT_SFT                               14
#define CALI_AUTORST_DETECT_MASK                              0x1
#define CALI_AUTORST_DETECT_MASK_SFT                          (0x1 << 14)
#define FREQ_CALC_RUNNING_SFT                                 13
#define FREQ_CALC_RUNNING_MASK                                0x1
#define FREQ_CALC_RUNNING_MASK_SFT                            (0x1 << 13)
#define AUTO_TUNE_FREQ3_SFT                                   12
#define AUTO_TUNE_FREQ3_MASK                                  0x1
#define AUTO_TUNE_FREQ3_MASK_SFT                              (0x1 << 12)
#define COMP_FREQ_RES_EN_SFT                                  11
#define COMP_FREQ_RES_EN_MASK                                 0x1
#define COMP_FREQ_RES_EN_MASK_SFT                             (0x1 << 11)
#define FREQ_CALI_SEL_SFT                                     8
#define FREQ_CALI_SEL_MASK                                    0x3
#define FREQ_CALI_SEL_MASK_SFT                                (0x3 << 8)
#define FREQ_CALI_BP_DGL_SFT                                  7
#define FREQ_CALI_BP_DGL_MASK                                 0x1
#define FREQ_CALI_BP_DGL_MASK_SFT                             (0x1 << 7)
#define FREQ_CALI_MAX_GWIDTH_SFT                              4
#define FREQ_CALI_MAX_GWIDTH_MASK                             0x7
#define FREQ_CALI_MAX_GWIDTH_MASK_SFT                         (0x7 << 4)
#define AUTO_TUNE_FREQ2_SFT                                   3
#define AUTO_TUNE_FREQ2_MASK                                  0x1
#define AUTO_TUNE_FREQ2_MASK_SFT                              (0x1 << 3)
#define FREQ_CALI_AUTO_RESTART_SFT                            2
#define FREQ_CALI_AUTO_RESTART_MASK                           0x1
#define FREQ_CALI_AUTO_RESTART_MASK_SFT                       (0x1 << 2)
#define CALI_USE_FREQ_OUT_SFT                                 1
#define CALI_USE_FREQ_OUT_MASK                                0x1
#define CALI_USE_FREQ_OUT_MASK_SFT                            (0x1 << 1)
#define CALI_EN_SFT                                           0
#define CALI_EN_MASK                                          0x1
#define CALI_EN_MASK_SFT                                      (0x1 << 0)

/* AFE_ASRC_NEW_CON7 */
#define FREQ_CALC_DENOMINATOR_SFT                             0
#define FREQ_CALC_DENOMINATOR_MASK                            0xffffff
#define FREQ_CALC_DENOMINATOR_MASK_SFT                        (0xffffff << 0)

/* AFE_ASRC_NEW_CON8 */
#define PRD_CALI_RESULT_RECORD_SFT                            0
#define PRD_CALI_RESULT_RECORD_MASK                           0xffffff
#define PRD_CALI_RESULT_RECORD_MASK_SFT                       (0xffffff << 0)

/* AFE_ASRC_NEW_CON9 */
#define FREQ_CALI_RESULT_SFT                                  0
#define FREQ_CALI_RESULT_MASK                                 0xffffff
#define FREQ_CALI_RESULT_MASK_SFT                             (0xffffff << 0)

/* AFE_ASRC_NEW_CON10 */
#define COEFF_SRAM_DATA_SFT                                   0
#define COEFF_SRAM_DATA_MASK                                  0xffffffff
#define COEFF_SRAM_DATA_MASK_SFT                              (0xffffffff << 0)

/* AFE_ASRC_NEW_CON11 */
#define COEFF_SRAM_ADR_SFT                                    0
#define COEFF_SRAM_ADR_MASK                                   0x3f
#define COEFF_SRAM_ADR_MASK_SFT                               (0x3f << 0)

/* AFE_ASRC_NEW_CON12 */
#define RING_DBG_RD_SFT                                       0
#define RING_DBG_RD_MASK                                      0x3ffffff
#define RING_DBG_RD_MASK_SFT                                  (0x3ffffff << 0)

/* AFE_ASRC_NEW_CON13 */
#define FREQ_CALI_AUTORST_TH_HIGH_SFT                         0
#define FREQ_CALI_AUTORST_TH_HIGH_MASK                        0xffffff
#define FREQ_CALI_AUTORST_TH_HIGH_MASK_SFT                    (0xffffff << 0)

/* AFE_ASRC_NEW_CON14 */
#define FREQ_CALI_AUTORST_TH_LOW_SFT                          0
#define FREQ_CALI_AUTORST_TH_LOW_MASK                         0xffffff
#define FREQ_CALI_AUTORST_TH_LOW_MASK_SFT                     (0xffffff << 0)

/* AFE_ASRC_NEW_IP_VERSION */
#define IP_VERSION_SFT                                        0
#define IP_VERSION_MASK                                       0xffffffff
#define IP_VERSION_MASK_SFT                                   (0xffffffff << 0)

#define AUDIO_TOP_CON0                       0x0
#define AUDIO_TOP_CON1                       0x4
#define AUDIO_TOP_CON2                       0x8
#define AUDIO_TOP_CON3                       0xc
#define AUDIO_TOP_CON4                       0x10
#define AUDIO_ENGEN_CON0                     0x14
#define AUDIO_ENGEN_CON0_USER1               0x18
#define AUDIO_ENGEN_CON0_USER2               0x1c
#define AFE_SINEGEN_CON0                     0x20
#define AFE_SINEGEN_CON1                     0x24
#define AFE_SINEGEN_CON2                     0x28
#define AFE_SINEGEN_CON3                     0x2c
#define AFE_APLL1_TUNER_CFG                  0x30
#define AFE_APLL1_TUNER_MON0                 0x34
#define AFE_APLL2_TUNER_CFG                  0x38
#define AFE_APLL2_TUNER_MON0                 0x3c
#define AUDIO_TOP_RG0                        0x4c
#define AUDIO_TOP_RG1                        0x50
#define AUDIO_TOP_RG2                        0x54
#define AUDIO_TOP_RG3                        0x58
#define AUDIO_TOP_RG4                        0x5c
#define AFE_SPM_CONTROL_REQ                  0x60
#define AFE_SPM_CONTROL_ACK                  0x64
#define AUD_TOP_CFG_VCORE_RG                 0x68
#define AUDIO_TOP_IP_VERSION                 0x6c
#define AUDIO_ENGEN_CON0_MON                 0x7c
#define AUD_TOP_CFG_VLP_RG                   0x98
#define AUD_TOP_MON_RG                       0x9c
#define AUDIO_USE_DEFAULT_DELSEL0            0xa0
#define AUDIO_USE_DEFAULT_DELSEL1            0xa4
#define AUDIO_USE_DEFAULT_DELSEL2            0xa8
#define AFE_CONNSYS_I2S_IPM_VER_MON          0xb0
#define AFE_CONNSYS_I2S_MON_SEL              0xb4
#define AFE_CONNSYS_I2S_MON                  0xb8
#define AFE_CONNSYS_I2S_CON                  0xbc
#define AFE_PCM0_INTF_CON0                   0xc0
#define AFE_PCM0_INTF_CON1                   0xc4
#define AFE_PCM_INTF_MON                     0xc8
#define AFE_PCM1_INTF_CON0                   0xd0
#define AFE_PCM1_INTF_CON1                   0xd4
#define AFE_PCM_TOP_IP_VERSION               0xe8
#define AFE_IRQ_MCU_EN                       0x100
#define AFE_IRQ_MCU_DSP_EN                   0x104
#define AFE_IRQ_MCU_DSP2_EN                  0x108
#define AFE_IRQ_MCU_SCP_EN                   0x10c
#define AFE_CUSTOM_IRQ_MCU_EN                0x110
#define AFE_CUSTOM_IRQ_MCU_DSP_EN            0x114
#define AFE_CUSTOM_IRQ_MCU_DSP2_EN           0x118
#define AFE_CUSTOM_IRQ_MCU_SCP_EN            0x11c
#define AFE_IRQ_MCU_STATUS                   0x120
#define AFE_CUSTOM_IRQ_MCU_STATUS            0x124
#define AFE_IRQ0_MCU_CFG0                    0x140
#define AFE_IRQ0_MCU_CFG1                    0x144
#define AFE_IRQ1_MCU_CFG0                    0x148
#define AFE_IRQ1_MCU_CFG1                    0x14c
#define AFE_IRQ2_MCU_CFG0                    0x150
#define AFE_IRQ2_MCU_CFG1                    0x154
#define AFE_IRQ3_MCU_CFG0                    0x158
#define AFE_IRQ3_MCU_CFG1                    0x15c
#define AFE_IRQ4_MCU_CFG0                    0x160
#define AFE_IRQ4_MCU_CFG1                    0x164
#define AFE_IRQ5_MCU_CFG0                    0x168
#define AFE_IRQ5_MCU_CFG1                    0x16c
#define AFE_IRQ6_MCU_CFG0                    0x170
#define AFE_IRQ6_MCU_CFG1                    0x174
#define AFE_IRQ7_MCU_CFG0                    0x178
#define AFE_IRQ7_MCU_CFG1                    0x17c
#define AFE_IRQ8_MCU_CFG0                    0x180
#define AFE_IRQ8_MCU_CFG1                    0x184
#define AFE_IRQ9_MCU_CFG0                    0x188
#define AFE_IRQ9_MCU_CFG1                    0x18c
#define AFE_IRQ10_MCU_CFG0                   0x190
#define AFE_IRQ10_MCU_CFG1                   0x194
#define AFE_IRQ11_MCU_CFG0                   0x198
#define AFE_IRQ11_MCU_CFG1                   0x19c
#define AFE_IRQ12_MCU_CFG0                   0x1a0
#define AFE_IRQ12_MCU_CFG1                   0x1a4
#define AFE_IRQ13_MCU_CFG0                   0x1a8
#define AFE_IRQ13_MCU_CFG1                   0x1ac
#define AFE_IRQ14_MCU_CFG0                   0x1b0
#define AFE_IRQ14_MCU_CFG1                   0x1b4
#define AFE_IRQ15_MCU_CFG0                   0x1b8
#define AFE_IRQ15_MCU_CFG1                   0x1bc
#define AFE_IRQ16_MCU_CFG0                   0x1c0
#define AFE_IRQ16_MCU_CFG1                   0x1c4
#define AFE_IRQ17_MCU_CFG0                   0x1c8
#define AFE_IRQ17_MCU_CFG1                   0x1cc
#define AFE_IRQ18_MCU_CFG0                   0x1d0
#define AFE_IRQ18_MCU_CFG1                   0x1d4
#define AFE_IRQ19_MCU_CFG0                   0x1d8
#define AFE_IRQ19_MCU_CFG1                   0x1dc
#define AFE_IRQ20_MCU_CFG0                   0x1e0
#define AFE_IRQ20_MCU_CFG1                   0x1e4
#define AFE_IRQ21_MCU_CFG0                   0x1e8
#define AFE_IRQ21_MCU_CFG1                   0x1ec
#define AFE_IRQ22_MCU_CFG0                   0x1f0
#define AFE_IRQ22_MCU_CFG1                   0x1f4
#define AFE_IRQ23_MCU_CFG0                   0x1f8
#define AFE_IRQ23_MCU_CFG1                   0x1fc
#define AFE_IRQ24_MCU_CFG0                   0x200
#define AFE_IRQ24_MCU_CFG1                   0x204
#define AFE_IRQ25_MCU_CFG0                   0x208
#define AFE_IRQ25_MCU_CFG1                   0x20c
#define AFE_IRQ26_MCU_CFG0                   0x210
#define AFE_IRQ26_MCU_CFG1                   0x214
#define AFE_CUSTOM_IRQ0_MCU_CFG0             0x268
#define AFE_IRQ_MCU_MON0                     0x300
#define AFE_IRQ_MCU_MON1                     0x304
#define AFE_IRQ_MCU_MON2                     0x308
#define AFE_IRQ0_CNT_MON                     0x310
#define AFE_IRQ1_CNT_MON                     0x314
#define AFE_IRQ2_CNT_MON                     0x318
#define AFE_IRQ3_CNT_MON                     0x31c
#define AFE_IRQ4_CNT_MON                     0x320
#define AFE_IRQ5_CNT_MON                     0x324
#define AFE_IRQ6_CNT_MON                     0x328
#define AFE_IRQ7_CNT_MON                     0x32c
#define AFE_IRQ8_CNT_MON                     0x330
#define AFE_IRQ9_CNT_MON                     0x334
#define AFE_IRQ10_CNT_MON                    0x338
#define AFE_IRQ11_CNT_MON                    0x33c
#define AFE_IRQ12_CNT_MON                    0x340
#define AFE_IRQ13_CNT_MON                    0x344
#define AFE_IRQ14_CNT_MON                    0x348
#define AFE_IRQ15_CNT_MON                    0x34c
#define AFE_IRQ16_CNT_MON                    0x350
#define AFE_IRQ17_CNT_MON                    0x354
#define AFE_IRQ18_CNT_MON                    0x358
#define AFE_IRQ19_CNT_MON                    0x35c
#define AFE_IRQ20_CNT_MON                    0x360
#define AFE_IRQ21_CNT_MON                    0x364
#define AFE_IRQ22_CNT_MON                    0x368
#define AFE_IRQ23_CNT_MON                    0x36c
#define AFE_IRQ24_CNT_MON                    0x370
#define AFE_IRQ25_CNT_MON                    0x374
#define AFE_IRQ26_CNT_MON                    0x378
#define AFE_CUSTOM_IRQ0_CNT_MON              0x390
#define AFE_CUSTOM_IRQ0_MCU_CFG1             0x3dc
#define AFE_GAIN0_CON0                       0x400
#define AFE_GAIN0_CON1_R                     0x404
#define AFE_GAIN0_CON1_L                     0x408
#define AFE_GAIN0_CON2                       0x40c
#define AFE_GAIN0_CON3                       0x410
#define AFE_GAIN0_CUR_R                      0x414
#define AFE_GAIN0_CUR_L                      0x418
#define AFE_GAIN1_CON0                       0x41c
#define AFE_GAIN1_CON1_R                     0x420
#define AFE_GAIN1_CON1_L                     0x424
#define AFE_GAIN1_CON2                       0x428
#define AFE_GAIN1_CON3                       0x42c
#define AFE_GAIN1_CUR_R                      0x430
#define AFE_GAIN1_CUR_L                      0x434
#define AFE_GAIN2_CON0                       0x438
#define AFE_GAIN2_CON1_R                     0x43c
#define AFE_GAIN2_CON1_L                     0x440
#define AFE_GAIN2_CON2                       0x444
#define AFE_GAIN2_CON3                       0x448
#define AFE_GAIN2_CUR_R                      0x44c
#define AFE_GAIN2_CUR_L                      0x450
#define AFE_GAIN3_CON0                       0x454
#define AFE_GAIN3_CON1_R                     0x458
#define AFE_GAIN3_CON1_L                     0x45c
#define AFE_GAIN3_CON2                       0x460
#define AFE_GAIN3_CON3                       0x464
#define AFE_GAIN3_CUR_R                      0x468
#define AFE_GAIN3_CUR_L                      0x46c
#define AFE_STF_CON0                         0xb80
#define AFE_STF_CON1                         0xb84
#define AFE_STF_COEFF                        0xb88
#define AFE_STF_GAIN                         0xb8c
#define AFE_STF_MON                          0xb90
#define AFE_STF_IP_VERSION                   0xb94
#define AFE_CM0_CON0                         0xba0
#define AFE_CM0_MON                          0xba4
#define AFE_CM0_IP_VERSION                   0xba8
#define AFE_CM1_CON0                         0xbb0
#define AFE_CM1_MON                          0xbb4
#define AFE_CM1_IP_VERSION                   0xbb8
#define AFE_CM2_CON0                         0xbc0
#define AFE_CM2_MON                          0xbc4
#define AFE_CM2_IP_VERSION                   0xbc8
#define AFE_ADDA_UL0_SRC_CON0                0xbd0
#define AFE_ADDA_UL0_SRC_CON1                0xbd4
#define AFE_ADDA_UL0_SRC_CON2                0xbd8
#define AFE_ADDA_UL0_SRC_DEBUG               0xbdc
#define AFE_ADDA_UL0_SRC_DEBUG_MON0          0xbe0
#define AFE_ADDA_UL0_SRC_MON0                0xbe4
#define AFE_ADDA_UL0_SRC_MON1                0xbe8
#define AFE_ADDA_UL0_IIR_COEF_02_01          0xbec
#define AFE_ADDA_UL0_IIR_COEF_04_03          0xbf0
#define AFE_ADDA_UL0_IIR_COEF_06_05          0xbf4
#define AFE_ADDA_UL0_IIR_COEF_08_07          0xbf8
#define AFE_ADDA_UL0_IIR_COEF_10_09          0xbfc
#define AFE_ADDA_UL0_ULCF_CFG_02_01          0xc00
#define AFE_ADDA_UL0_ULCF_CFG_04_03          0xc04
#define AFE_ADDA_UL0_ULCF_CFG_06_05          0xc08
#define AFE_ADDA_UL0_ULCF_CFG_08_07          0xc0c
#define AFE_ADDA_UL0_ULCF_CFG_10_09          0xc10
#define AFE_ADDA_UL0_ULCF_CFG_12_11          0xc14
#define AFE_ADDA_UL0_ULCF_CFG_14_13          0xc18
#define AFE_ADDA_UL0_ULCF_CFG_16_15          0xc1c
#define AFE_ADDA_UL0_ULCF_CFG_18_17          0xc20
#define AFE_ADDA_UL0_ULCF_CFG_20_19          0xc24
#define AFE_ADDA_UL0_ULCF_CFG_22_21          0xc28
#define AFE_ADDA_UL0_ULCF_CFG_24_23          0xc2c
#define AFE_ADDA_UL0_ULCF_CFG_26_25          0xc30
#define AFE_ADDA_UL0_ULCF_CFG_28_27          0xc34
#define AFE_ADDA_UL0_ULCF_CFG_30_29          0xc38
#define AFE_ADDA_UL0_ULCF_CFG_32_31          0xc3c
#define AFE_ADDA_UL0_IP_VERSION              0xc4c
#define AFE_ADDA_UL1_SRC_CON0                0xc50
#define AFE_ADDA_UL1_SRC_CON1                0xc54
#define AFE_ADDA_UL1_SRC_CON2                0xc58
#define AFE_ADDA_UL1_SRC_DEBUG               0xc5c
#define AFE_ADDA_UL1_SRC_DEBUG_MON0          0xc60
#define AFE_ADDA_UL1_SRC_MON0                0xc64
#define AFE_ADDA_UL1_SRC_MON1                0xc68
#define AFE_ADDA_UL1_IIR_COEF_02_01          0xc6c
#define AFE_ADDA_UL1_IIR_COEF_04_03          0xc70
#define AFE_ADDA_UL1_IIR_COEF_06_05          0xc74
#define AFE_ADDA_UL1_IIR_COEF_08_07          0xc78
#define AFE_ADDA_UL1_IIR_COEF_10_09          0xc7c
#define AFE_ADDA_UL1_ULCF_CFG_02_01          0xc80
#define AFE_ADDA_UL1_ULCF_CFG_04_03          0xc84
#define AFE_ADDA_UL1_ULCF_CFG_06_05          0xc88
#define AFE_ADDA_UL1_ULCF_CFG_08_07          0xc8c
#define AFE_ADDA_UL1_ULCF_CFG_10_09          0xc90
#define AFE_ADDA_UL1_ULCF_CFG_12_11          0xc94
#define AFE_ADDA_UL1_ULCF_CFG_14_13          0xc98
#define AFE_ADDA_UL1_ULCF_CFG_16_15          0xc9c
#define AFE_ADDA_UL1_ULCF_CFG_18_17          0xca0
#define AFE_ADDA_UL1_ULCF_CFG_20_19          0xca4
#define AFE_ADDA_UL1_ULCF_CFG_22_21          0xca8
#define AFE_ADDA_UL1_ULCF_CFG_24_23          0xcac
#define AFE_ADDA_UL1_ULCF_CFG_26_25          0xcb0
#define AFE_ADDA_UL1_ULCF_CFG_28_27          0xcb4
#define AFE_ADDA_UL1_ULCF_CFG_30_29          0xcb8
#define AFE_ADDA_UL1_ULCF_CFG_32_31          0xcbc
#define AFE_ADDA_UL1_IP_VERSION              0xccc
#define AFE_ADDA_UL2_SRC_CON0                0xcd0
#define AFE_ADDA_UL2_SRC_CON1                0xcd4
#define AFE_ADDA_UL2_SRC_CON2                0xcd8
#define AFE_ADDA_UL2_SRC_DEBUG               0xcdc
#define AFE_ADDA_UL2_SRC_DEBUG_MON0          0xce0
#define AFE_ADDA_UL2_SRC_MON0                0xce4
#define AFE_ADDA_UL2_SRC_MON1                0xce8
#define AFE_ADDA_UL2_IIR_COEF_02_01          0xcec
#define AFE_ADDA_UL2_IIR_COEF_04_03          0xcf0
#define AFE_ADDA_UL2_IIR_COEF_06_05          0xcf4
#define AFE_ADDA_UL2_IIR_COEF_08_07          0xcf8
#define AFE_ADDA_UL2_IIR_COEF_10_09          0xcfc
#define AFE_ADDA_UL2_ULCF_CFG_02_01          0xd00
#define AFE_ADDA_UL2_ULCF_CFG_04_03          0xd04
#define AFE_ADDA_UL2_ULCF_CFG_06_05          0xd08
#define AFE_ADDA_UL2_ULCF_CFG_08_07          0xd0c
#define AFE_ADDA_UL2_ULCF_CFG_10_09          0xd10
#define AFE_ADDA_UL2_ULCF_CFG_12_11          0xd14
#define AFE_ADDA_UL2_ULCF_CFG_14_13          0xd18
#define AFE_ADDA_UL2_ULCF_CFG_16_15          0xd1c
#define AFE_ADDA_UL2_ULCF_CFG_18_17          0xd20
#define AFE_ADDA_UL2_ULCF_CFG_20_19          0xd24
#define AFE_ADDA_UL2_ULCF_CFG_22_21          0xd28
#define AFE_ADDA_UL2_ULCF_CFG_24_23          0xd2c
#define AFE_ADDA_UL2_ULCF_CFG_26_25          0xd30
#define AFE_ADDA_UL2_ULCF_CFG_28_27          0xd34
#define AFE_ADDA_UL2_ULCF_CFG_30_29          0xd38
#define AFE_ADDA_UL2_ULCF_CFG_32_31          0xd3c
#define AFE_ADDA_UL2_IP_VERSION              0xd4c
#define AFE_ADDA_PROXIMITY_CON0              0xed0
#define AFE_ADDA_ULSRC_PHASE_CON0            0xf00
#define AFE_ADDA_ULSRC_PHASE_CON1            0xf04
#define AFE_ADDA_ULSRC_PHASE_CON2            0xf08
#define AFE_ADDA_ULSRC_PHASE_CON3            0xf0c
#define AFE_MTKAIF_IPM_VER_MON               0x1180
#define AFE_MTKAIF_MON_SEL                   0x1184
#define AFE_MTKAIF_MON                       0x1188
#define AFE_MTKAIF0_CFG0                     0x1190
#define AFE_MTKAIF0_TX_CFG0                  0x1194
#define AFE_MTKAIF0_RX_CFG0                  0x1198
#define AFE_MTKAIF0_RX_CFG1                  0x119c
#define AFE_MTKAIF0_RX_CFG2                  0x11a0
#define AFE_MTKAIF1_CFG0                     0x11f0
#define AFE_MTKAIF1_TX_CFG0                  0x11f4
#define AFE_MTKAIF1_RX_CFG0                  0x11f8
#define AFE_MTKAIF1_RX_CFG1                  0x11fc
#define AFE_MTKAIF1_RX_CFG2                  0x1200
#define AFE_AUD_PAD_TOP_CFG0                 0x1204
#define AFE_AUD_PAD_TOP_MON                  0x1208
#define AFE_ADDA_MTKAIFV4_TX_CFG0            0x1280
#define AFE_ADDA6_MTKAIFV4_TX_CFG0           0x1284
#define AFE_ADDA_MTKAIFV4_RX_CFG0            0x1288
#define AFE_ADDA_MTKAIFV4_RX_CFG1            0x128c
#define AFE_ADDA6_MTKAIFV4_RX_CFG0           0x1290
#define AFE_ADDA6_MTKAIFV4_RX_CFG1           0x1294
#define AFE_ADDA_MTKAIFV4_TX_SYNCWORD_CFG    0x1298
#define AFE_ADDA_MTKAIFV4_RX_SYNCWORD_CFG    0x129c
#define AFE_ADDA_MTKAIFV4_MON0               0x12a0
#define AFE_ADDA_MTKAIFV4_MON1               0x12a4
#define AFE_ADDA6_MTKAIFV4_MON0              0x12a8
#define ETDM_IN0_CON0                        0x1300
#define ETDM_IN0_CON1                        0x1304
#define ETDM_IN0_CON2                        0x1308
#define ETDM_IN0_CON3                        0x130c
#define ETDM_IN0_CON4                        0x1310
#define ETDM_IN0_CON5                        0x1314
#define ETDM_IN0_CON6                        0x1318
#define ETDM_IN0_CON7                        0x131c
#define ETDM_IN0_CON8                        0x1320
#define ETDM_IN0_CON9                        0x1324
#define ETDM_IN0_MON                         0x1328
#define ETDM_IN1_CON0                        0x1330
#define ETDM_IN1_CON1                        0x1334
#define ETDM_IN1_CON2                        0x1338
#define ETDM_IN1_CON3                        0x133c
#define ETDM_IN1_CON4                        0x1340
#define ETDM_IN1_CON5                        0x1344
#define ETDM_IN1_CON6                        0x1348
#define ETDM_IN1_CON7                        0x134c
#define ETDM_IN1_CON8                        0x1350
#define ETDM_IN1_CON9                        0x1354
#define ETDM_IN1_MON                         0x1358
#define ETDM_IN2_CON0                        0x1360
#define ETDM_IN2_CON1                        0x1364
#define ETDM_IN2_CON2                        0x1368
#define ETDM_IN2_CON3                        0x136c
#define ETDM_IN2_CON4                        0x1370
#define ETDM_IN2_CON5                        0x1374
#define ETDM_IN2_CON6                        0x1378
#define ETDM_IN2_CON7                        0x137c
#define ETDM_IN2_CON8                        0x1380
#define ETDM_IN2_CON9                        0x1384
#define ETDM_IN2_MON                         0x1388
#define ETDM_IN3_CON0                        0x1390
#define ETDM_IN3_CON1                        0x1394
#define ETDM_IN3_CON2                        0x1398
#define ETDM_IN3_CON3                        0x139c
#define ETDM_IN3_CON4                        0x13a0
#define ETDM_IN3_CON5                        0x13a4
#define ETDM_IN3_CON6                        0x13a8
#define ETDM_IN3_CON7                        0x13ac
#define ETDM_IN3_CON8                        0x13b0
#define ETDM_IN3_CON9                        0x13b4
#define ETDM_IN3_MON                         0x13b8
#define ETDM_IN4_CON0                        0x13c0
#define ETDM_IN4_CON1                        0x13c4
#define ETDM_IN4_CON2                        0x13c8
#define ETDM_IN4_CON3                        0x13cc
#define ETDM_IN4_CON4                        0x13d0
#define ETDM_IN4_CON5                        0x13d4
#define ETDM_IN4_CON6                        0x13d8
#define ETDM_IN4_CON7                        0x13dc
#define ETDM_IN4_CON8                        0x13e0
#define ETDM_IN4_CON9                        0x13e4
#define ETDM_IN4_MON                         0x13e8
#define ETDM_IN5_CON0                        0x13f0
#define ETDM_IN5_CON1                        0x13f4
#define ETDM_IN5_CON2                        0x13f8
#define ETDM_IN5_CON3                        0x13fc
#define ETDM_IN5_CON4                        0x1400
#define ETDM_IN5_CON5                        0x1404
#define ETDM_IN5_CON6                        0x1408
#define ETDM_IN5_CON7                        0x140c
#define ETDM_IN5_CON8                        0x1410
#define ETDM_IN5_CON9                        0x1414
#define ETDM_IN5_MON                         0x1418
#define ETDM_IN6_CON0                        0x1420
#define ETDM_IN6_CON1                        0x1424
#define ETDM_IN6_CON2                        0x1428
#define ETDM_IN6_CON3                        0x142c
#define ETDM_IN6_CON4                        0x1430
#define ETDM_IN6_CON5                        0x1434
#define ETDM_IN6_CON6                        0x1438
#define ETDM_IN6_CON7                        0x143c
#define ETDM_IN6_CON8                        0x1440
#define ETDM_IN6_CON9                        0x1444
#define ETDM_IN6_MON                         0x1448
#define ETDM_OUT0_CON0                       0x1480
#define ETDM_OUT0_CON1                       0x1484
#define ETDM_OUT0_CON2                       0x1488
#define ETDM_OUT0_CON3                       0x148c
#define ETDM_OUT0_CON4                       0x1490
#define ETDM_OUT0_CON5                       0x1494
#define ETDM_OUT0_CON6                       0x1498
#define ETDM_OUT0_CON7                       0x149c
#define ETDM_OUT0_CON8                       0x14a0
#define ETDM_OUT0_CON9                       0x14a4
#define ETDM_OUT0_MON                        0x14a8
#define ETDM_OUT1_CON0                       0x14c0
#define ETDM_OUT1_CON1                       0x14c4
#define ETDM_OUT1_CON2                       0x14c8
#define ETDM_OUT1_CON3                       0x14cc
#define ETDM_OUT1_CON4                       0x14d0
#define ETDM_OUT1_CON5                       0x14d4
#define ETDM_OUT1_CON6                       0x14d8
#define ETDM_OUT1_CON7                       0x14dc
#define ETDM_OUT1_CON8                       0x14e0
#define ETDM_OUT1_CON9                       0x14e4
#define ETDM_OUT1_MON                        0x14e8
#define ETDM_OUT2_CON0                       0x1500
#define ETDM_OUT2_CON1                       0x1504
#define ETDM_OUT2_CON2                       0x1508
#define ETDM_OUT2_CON3                       0x150c
#define ETDM_OUT2_CON4                       0x1510
#define ETDM_OUT2_CON5                       0x1514
#define ETDM_OUT2_CON6                       0x1518
#define ETDM_OUT2_CON7                       0x151c
#define ETDM_OUT2_CON8                       0x1520
#define ETDM_OUT2_CON9                       0x1524
#define ETDM_OUT2_MON                        0x1528
#define ETDM_OUT3_CON0                       0x1540
#define ETDM_OUT3_CON1                       0x1544
#define ETDM_OUT3_CON2                       0x1548
#define ETDM_OUT3_CON3                       0x154c
#define ETDM_OUT3_CON4                       0x1550
#define ETDM_OUT3_CON5                       0x1554
#define ETDM_OUT3_CON6                       0x1558
#define ETDM_OUT3_CON7                       0x155c
#define ETDM_OUT3_CON8                       0x1560
#define ETDM_OUT3_CON9                       0x1564
#define ETDM_OUT3_MON                        0x1568
#define ETDM_OUT4_CON0                       0x1580
#define ETDM_OUT4_CON1                       0x1584
#define ETDM_OUT4_CON2                       0x1588
#define ETDM_OUT4_CON3                       0x158c
#define ETDM_OUT4_CON4                       0x1590
#define ETDM_OUT4_CON5                       0x1594
#define ETDM_OUT4_CON6                       0x1598
#define ETDM_OUT4_CON7                       0x159c
#define ETDM_OUT4_CON8                       0x15a0
#define ETDM_OUT4_CON9                       0x15a4
#define ETDM_OUT4_MON                        0x15a8
#define ETDM_OUT5_CON0                       0x15c0
#define ETDM_OUT5_CON1                       0x15c4
#define ETDM_OUT5_CON2                       0x15c8
#define ETDM_OUT5_CON3                       0x15cc
#define ETDM_OUT5_CON4                       0x15d0
#define ETDM_OUT5_CON5                       0x15d4
#define ETDM_OUT5_CON6                       0x15d8
#define ETDM_OUT5_CON7                       0x15dc
#define ETDM_OUT5_CON8                       0x15e0
#define ETDM_OUT5_CON9                       0x15e4
#define ETDM_OUT5_MON                        0x15e8
#define ETDM_OUT6_CON0                       0x1600
#define ETDM_OUT6_CON1                       0x1604
#define ETDM_OUT6_CON2                       0x1608
#define ETDM_OUT6_CON3                       0x160c
#define ETDM_OUT6_CON4                       0x1610
#define ETDM_OUT6_CON5                       0x1614
#define ETDM_OUT6_CON6                       0x1618
#define ETDM_OUT6_CON7                       0x161c
#define ETDM_OUT6_CON8                       0x1620
#define ETDM_OUT6_CON9                       0x1624
#define ETDM_OUT6_MON                        0x1628
#define ETDM_0_3_COWORK_CON0                 0x1680
#define ETDM_0_3_COWORK_CON1                 0x1684
#define ETDM_0_3_COWORK_CON2                 0x1688
#define ETDM_0_3_COWORK_CON3                 0x168c
#define ETDM_4_7_COWORK_CON0                 0x1690
#define ETDM_4_7_COWORK_CON1                 0x1694
#define ETDM_4_7_COWORK_CON2                 0x1698
#define ETDM_4_7_COWORK_CON3                 0x169c
#define AFE_DPTX_CON                         0x2040
#define AFE_DPTX_MON                         0x2044
#define AFE_TDM_CON1                         0x2048
#define AFE_TDM_CON2                         0x204c
#define AFE_TDM_CON3                         0x2050
#define AFE_TDM_OUT_MON                      0x2054
#define AFE_HDMI_CONN0                       0x2078
#define AFE_TDM_TOP_IP_VERSION               0x207c
#define AFE_CONN004_0                        0x2100
#define AFE_CONN004_1                        0x2104
#define AFE_CONN004_2                        0x2108
#define AFE_CONN004_4                        0x2110
#define AFE_CONN004_5                        0x2114
#define AFE_CONN004_6                        0x2118
#define AFE_CONN004_7                        0x211c
#define AFE_CONN005_0                        0x2120
#define AFE_CONN005_1                        0x2124
#define AFE_CONN005_2                        0x2128
#define AFE_CONN005_4                        0x2130
#define AFE_CONN005_5                        0x2134
#define AFE_CONN005_6                        0x2138
#define AFE_CONN005_7                        0x213c
#define AFE_CONN006_0                        0x2140
#define AFE_CONN006_1                        0x2144
#define AFE_CONN006_2                        0x2148
#define AFE_CONN006_4                        0x2150
#define AFE_CONN006_5                        0x2154
#define AFE_CONN006_6                        0x2158
#define AFE_CONN006_7                        0x215c
#define AFE_CONN007_0                        0x2160
#define AFE_CONN007_1                        0x2164
#define AFE_CONN007_2                        0x2168
#define AFE_CONN007_4                        0x2170
#define AFE_CONN007_5                        0x2174
#define AFE_CONN007_6                        0x2178
#define AFE_CONN007_7                        0x217c
#define AFE_CONN008_0                        0x2180
#define AFE_CONN008_1                        0x2184
#define AFE_CONN008_2                        0x2188
#define AFE_CONN008_4                        0x2190
#define AFE_CONN008_5                        0x2194
#define AFE_CONN008_6                        0x2198
#define AFE_CONN008_7                        0x219c
#define AFE_CONN009_0                        0x21a0
#define AFE_CONN009_1                        0x21a4
#define AFE_CONN009_2                        0x21a8
#define AFE_CONN009_4                        0x21b0
#define AFE_CONN009_5                        0x21b4
#define AFE_CONN009_6                        0x21b8
#define AFE_CONN009_7                        0x21bc
#define AFE_CONN010_0                        0x21c0
#define AFE_CONN010_1                        0x21c4
#define AFE_CONN010_2                        0x21c8
#define AFE_CONN010_4                        0x21d0
#define AFE_CONN010_5                        0x21d4
#define AFE_CONN010_6                        0x21d8
#define AFE_CONN010_7                        0x21dc
#define AFE_CONN011_0                        0x21e0
#define AFE_CONN011_1                        0x21e4
#define AFE_CONN011_2                        0x21e8
#define AFE_CONN011_4                        0x21f0
#define AFE_CONN011_5                        0x21f4
#define AFE_CONN011_6                        0x21f8
#define AFE_CONN011_7                        0x21fc
#define AFE_CONN012_0                        0x2200
#define AFE_CONN012_1                        0x2204
#define AFE_CONN012_2                        0x2208
#define AFE_CONN012_4                        0x2210
#define AFE_CONN012_5                        0x2214
#define AFE_CONN012_6                        0x2218
#define AFE_CONN012_7                        0x221c
#define AFE_CONN014_0                        0x2240
#define AFE_CONN014_1                        0x2244
#define AFE_CONN014_2                        0x2248
#define AFE_CONN014_4                        0x2250
#define AFE_CONN014_5                        0x2254
#define AFE_CONN014_6                        0x2258
#define AFE_CONN014_7                        0x225c
#define AFE_CONN015_0                        0x2260
#define AFE_CONN015_1                        0x2264
#define AFE_CONN015_2                        0x2268
#define AFE_CONN015_4                        0x2270
#define AFE_CONN015_5                        0x2274
#define AFE_CONN015_6                        0x2278
#define AFE_CONN015_7                        0x227c
#define AFE_CONN016_0                        0x2280
#define AFE_CONN016_1                        0x2284
#define AFE_CONN016_2                        0x2288
#define AFE_CONN016_4                        0x2290
#define AFE_CONN016_5                        0x2294
#define AFE_CONN016_6                        0x2298
#define AFE_CONN016_7                        0x229c
#define AFE_CONN017_0                        0x22a0
#define AFE_CONN017_1                        0x22a4
#define AFE_CONN017_2                        0x22a8
#define AFE_CONN017_4                        0x22b0
#define AFE_CONN017_5                        0x22b4
#define AFE_CONN017_6                        0x22b8
#define AFE_CONN017_7                        0x22bc
#define AFE_CONN018_0                        0x22c0
#define AFE_CONN018_1                        0x22c4
#define AFE_CONN018_2                        0x22c8
#define AFE_CONN018_4                        0x22d0
#define AFE_CONN018_5                        0x22d4
#define AFE_CONN018_6                        0x22d8
#define AFE_CONN018_7                        0x22dc
#define AFE_CONN019_0                        0x22e0
#define AFE_CONN019_1                        0x22e4
#define AFE_CONN019_2                        0x22e8
#define AFE_CONN019_4                        0x22f0
#define AFE_CONN019_5                        0x22f4
#define AFE_CONN019_6                        0x22f8
#define AFE_CONN019_7                        0x22fc
#define AFE_CONN020_0                        0x2300
#define AFE_CONN020_1                        0x2304
#define AFE_CONN020_2                        0x2308
#define AFE_CONN020_4                        0x2310
#define AFE_CONN020_5                        0x2314
#define AFE_CONN020_6                        0x2318
#define AFE_CONN020_7                        0x231c
#define AFE_CONN021_0                        0x2320
#define AFE_CONN021_1                        0x2324
#define AFE_CONN021_2                        0x2328
#define AFE_CONN021_4                        0x2330
#define AFE_CONN021_5                        0x2334
#define AFE_CONN021_6                        0x2338
#define AFE_CONN021_7                        0x233c
#define AFE_CONN022_0                        0x2340
#define AFE_CONN022_1                        0x2344
#define AFE_CONN022_2                        0x2348
#define AFE_CONN022_4                        0x2350
#define AFE_CONN022_5                        0x2354
#define AFE_CONN022_6                        0x2358
#define AFE_CONN022_7                        0x235c
#define AFE_CONN023_0                        0x2360
#define AFE_CONN023_1                        0x2364
#define AFE_CONN023_2                        0x2368
#define AFE_CONN023_4                        0x2370
#define AFE_CONN023_5                        0x2374
#define AFE_CONN023_6                        0x2378
#define AFE_CONN023_7                        0x237c
#define AFE_CONN024_0                        0x2380
#define AFE_CONN024_1                        0x2384
#define AFE_CONN024_2                        0x2388
#define AFE_CONN024_4                        0x2390
#define AFE_CONN024_5                        0x2394
#define AFE_CONN024_6                        0x2398
#define AFE_CONN024_7                        0x239c
#define AFE_CONN025_0                        0x23a0
#define AFE_CONN025_1                        0x23a4
#define AFE_CONN025_2                        0x23a8
#define AFE_CONN025_4                        0x23b0
#define AFE_CONN025_5                        0x23b4
#define AFE_CONN025_6                        0x23b8
#define AFE_CONN025_7                        0x23bc
#define AFE_CONN026_0                        0x23c0
#define AFE_CONN026_1                        0x23c4
#define AFE_CONN026_2                        0x23c8
#define AFE_CONN026_4                        0x23d0
#define AFE_CONN026_5                        0x23d4
#define AFE_CONN026_6                        0x23d8
#define AFE_CONN026_7                        0x23dc
#define AFE_CONN027_0                        0x23e0
#define AFE_CONN027_1                        0x23e4
#define AFE_CONN027_2                        0x23e8
#define AFE_CONN027_4                        0x23f0
#define AFE_CONN027_5                        0x23f4
#define AFE_CONN027_6                        0x23f8
#define AFE_CONN027_7                        0x23fc
#define AFE_CONN028_0                        0x2400
#define AFE_CONN028_1                        0x2404
#define AFE_CONN028_2                        0x2408
#define AFE_CONN028_4                        0x2410
#define AFE_CONN028_5                        0x2414
#define AFE_CONN028_6                        0x2418
#define AFE_CONN028_7                        0x241c
#define AFE_CONN029_0                        0x2420
#define AFE_CONN029_1                        0x2424
#define AFE_CONN029_2                        0x2428
#define AFE_CONN029_4                        0x2430
#define AFE_CONN029_5                        0x2434
#define AFE_CONN029_6                        0x2438
#define AFE_CONN029_7                        0x243c
#define AFE_CONN030_0                        0x2440
#define AFE_CONN030_1                        0x2444
#define AFE_CONN030_2                        0x2448
#define AFE_CONN030_4                        0x2450
#define AFE_CONN030_5                        0x2454
#define AFE_CONN030_6                        0x2458
#define AFE_CONN030_7                        0x245c
#define AFE_CONN031_0                        0x2460
#define AFE_CONN031_1                        0x2464
#define AFE_CONN031_2                        0x2468
#define AFE_CONN031_4                        0x2470
#define AFE_CONN031_5                        0x2474
#define AFE_CONN031_6                        0x2478
#define AFE_CONN031_7                        0x247c
#define AFE_CONN032_0                        0x2480
#define AFE_CONN032_1                        0x2484
#define AFE_CONN032_2                        0x2488
#define AFE_CONN032_4                        0x2490
#define AFE_CONN032_5                        0x2494
#define AFE_CONN032_6                        0x2498
#define AFE_CONN032_7                        0x249c
#define AFE_CONN033_0                        0x24a0
#define AFE_CONN033_1                        0x24a4
#define AFE_CONN033_2                        0x24a8
#define AFE_CONN033_4                        0x24b0
#define AFE_CONN033_5                        0x24b4
#define AFE_CONN033_6                        0x24b8
#define AFE_CONN033_7                        0x24bc
#define AFE_CONN034_0                        0x24c0
#define AFE_CONN034_1                        0x24c4
#define AFE_CONN034_2                        0x24c8
#define AFE_CONN034_4                        0x24d0
#define AFE_CONN034_5                        0x24d4
#define AFE_CONN034_6                        0x24d8
#define AFE_CONN034_7                        0x24dc
#define AFE_CONN035_0                        0x24e0
#define AFE_CONN035_1                        0x24e4
#define AFE_CONN035_2                        0x24e8
#define AFE_CONN035_4                        0x24f0
#define AFE_CONN035_5                        0x24f4
#define AFE_CONN035_6                        0x24f8
#define AFE_CONN035_7                        0x24fc
#define AFE_CONN036_0                        0x2500
#define AFE_CONN036_1                        0x2504
#define AFE_CONN036_2                        0x2508
#define AFE_CONN036_4                        0x2510
#define AFE_CONN036_5                        0x2514
#define AFE_CONN036_6                        0x2518
#define AFE_CONN036_7                        0x251c
#define AFE_CONN037_0                        0x2520
#define AFE_CONN037_1                        0x2524
#define AFE_CONN037_2                        0x2528
#define AFE_CONN037_4                        0x2530
#define AFE_CONN037_5                        0x2534
#define AFE_CONN037_6                        0x2538
#define AFE_CONN037_7                        0x253c
#define AFE_CONN038_0                        0x2540
#define AFE_CONN038_1                        0x2544
#define AFE_CONN038_2                        0x2548
#define AFE_CONN038_4                        0x2550
#define AFE_CONN038_5                        0x2554
#define AFE_CONN038_6                        0x2558
#define AFE_CONN038_7                        0x255c
#define AFE_CONN039_0                        0x2560
#define AFE_CONN039_1                        0x2564
#define AFE_CONN039_2                        0x2568
#define AFE_CONN039_4                        0x2570
#define AFE_CONN039_5                        0x2574
#define AFE_CONN039_6                        0x2578
#define AFE_CONN039_7                        0x257c
#define AFE_CONN040_0                        0x2580
#define AFE_CONN040_1                        0x2584
#define AFE_CONN040_2                        0x2588
#define AFE_CONN040_4                        0x2590
#define AFE_CONN040_5                        0x2594
#define AFE_CONN040_6                        0x2598
#define AFE_CONN040_7                        0x259c
#define AFE_CONN041_0                        0x25a0
#define AFE_CONN041_1                        0x25a4
#define AFE_CONN041_2                        0x25a8
#define AFE_CONN041_4                        0x25b0
#define AFE_CONN041_5                        0x25b4
#define AFE_CONN041_6                        0x25b8
#define AFE_CONN041_7                        0x25bc
#define AFE_CONN042_0                        0x25c0
#define AFE_CONN042_1                        0x25c4
#define AFE_CONN042_2                        0x25c8
#define AFE_CONN042_4                        0x25d0
#define AFE_CONN042_5                        0x25d4
#define AFE_CONN042_6                        0x25d8
#define AFE_CONN042_7                        0x25dc
#define AFE_CONN043_0                        0x25e0
#define AFE_CONN043_1                        0x25e4
#define AFE_CONN043_2                        0x25e8
#define AFE_CONN043_4                        0x25f0
#define AFE_CONN043_5                        0x25f4
#define AFE_CONN043_6                        0x25f8
#define AFE_CONN043_7                        0x25fc
#define AFE_CONN044_0                        0x2600
#define AFE_CONN044_1                        0x2604
#define AFE_CONN044_2                        0x2608
#define AFE_CONN044_4                        0x2610
#define AFE_CONN044_5                        0x2614
#define AFE_CONN044_6                        0x2618
#define AFE_CONN044_7                        0x261c
#define AFE_CONN045_0                        0x2620
#define AFE_CONN045_1                        0x2624
#define AFE_CONN045_2                        0x2628
#define AFE_CONN045_4                        0x2630
#define AFE_CONN045_5                        0x2634
#define AFE_CONN045_6                        0x2638
#define AFE_CONN045_7                        0x263c
#define AFE_CONN046_0                        0x2640
#define AFE_CONN046_1                        0x2644
#define AFE_CONN046_2                        0x2648
#define AFE_CONN046_4                        0x2650
#define AFE_CONN046_5                        0x2654
#define AFE_CONN046_6                        0x2658
#define AFE_CONN046_7                        0x265c
#define AFE_CONN047_0                        0x2660
#define AFE_CONN047_1                        0x2664
#define AFE_CONN047_2                        0x2668
#define AFE_CONN047_4                        0x2670
#define AFE_CONN047_5                        0x2674
#define AFE_CONN047_6                        0x2678
#define AFE_CONN047_7                        0x267c
#define AFE_CONN048_0                        0x2680
#define AFE_CONN048_1                        0x2684
#define AFE_CONN048_2                        0x2688
#define AFE_CONN048_4                        0x2690
#define AFE_CONN048_5                        0x2694
#define AFE_CONN048_6                        0x2698
#define AFE_CONN048_7                        0x269c
#define AFE_CONN049_0                        0x26a0
#define AFE_CONN049_1                        0x26a4
#define AFE_CONN049_2                        0x26a8
#define AFE_CONN049_4                        0x26b0
#define AFE_CONN049_5                        0x26b4
#define AFE_CONN049_6                        0x26b8
#define AFE_CONN049_7                        0x26bc
#define AFE_CONN050_0                        0x26c0
#define AFE_CONN050_1                        0x26c4
#define AFE_CONN050_2                        0x26c8
#define AFE_CONN050_4                        0x26d0
#define AFE_CONN050_5                        0x26d4
#define AFE_CONN050_6                        0x26d8
#define AFE_CONN050_7                        0x26dc
#define AFE_CONN051_0                        0x26e0
#define AFE_CONN051_1                        0x26e4
#define AFE_CONN051_2                        0x26e8
#define AFE_CONN051_4                        0x26f0
#define AFE_CONN051_5                        0x26f4
#define AFE_CONN051_6                        0x26f8
#define AFE_CONN051_7                        0x26fc
#define AFE_CONN052_0                        0x2700
#define AFE_CONN052_1                        0x2704
#define AFE_CONN052_2                        0x2708
#define AFE_CONN052_4                        0x2710
#define AFE_CONN052_5                        0x2714
#define AFE_CONN052_6                        0x2718
#define AFE_CONN052_7                        0x271c
#define AFE_CONN053_0                        0x2720
#define AFE_CONN053_1                        0x2724
#define AFE_CONN053_2                        0x2728
#define AFE_CONN053_4                        0x2730
#define AFE_CONN053_5                        0x2734
#define AFE_CONN053_6                        0x2738
#define AFE_CONN053_7                        0x273c
#define AFE_CONN054_0                        0x2740
#define AFE_CONN054_1                        0x2744
#define AFE_CONN054_2                        0x2748
#define AFE_CONN054_4                        0x2750
#define AFE_CONN054_5                        0x2754
#define AFE_CONN054_6                        0x2758
#define AFE_CONN054_7                        0x275c
#define AFE_CONN055_0                        0x2760
#define AFE_CONN055_1                        0x2764
#define AFE_CONN055_2                        0x2768
#define AFE_CONN055_4                        0x2770
#define AFE_CONN055_5                        0x2774
#define AFE_CONN055_6                        0x2778
#define AFE_CONN055_7                        0x277c
#define AFE_CONN056_0                        0x2780
#define AFE_CONN056_1                        0x2784
#define AFE_CONN056_2                        0x2788
#define AFE_CONN056_4                        0x2790
#define AFE_CONN056_5                        0x2794
#define AFE_CONN056_6                        0x2798
#define AFE_CONN056_7                        0x279c
#define AFE_CONN057_0                        0x27a0
#define AFE_CONN057_1                        0x27a4
#define AFE_CONN057_2                        0x27a8
#define AFE_CONN057_4                        0x27b0
#define AFE_CONN057_5                        0x27b4
#define AFE_CONN057_6                        0x27b8
#define AFE_CONN057_7                        0x27bc
#define AFE_CONN058_0                        0x27c0
#define AFE_CONN058_1                        0x27c4
#define AFE_CONN058_2                        0x27c8
#define AFE_CONN058_4                        0x27d0
#define AFE_CONN058_5                        0x27d4
#define AFE_CONN058_6                        0x27d8
#define AFE_CONN058_7                        0x27dc
#define AFE_CONN059_0                        0x27e0
#define AFE_CONN059_1                        0x27e4
#define AFE_CONN059_2                        0x27e8
#define AFE_CONN059_4                        0x27f0
#define AFE_CONN059_5                        0x27f4
#define AFE_CONN059_6                        0x27f8
#define AFE_CONN059_7                        0x27fc
#define AFE_CONN060_0                        0x2800
#define AFE_CONN060_1                        0x2804
#define AFE_CONN060_2                        0x2808
#define AFE_CONN060_4                        0x2810
#define AFE_CONN060_5                        0x2814
#define AFE_CONN060_6                        0x2818
#define AFE_CONN060_7                        0x281c
#define AFE_CONN061_0                        0x2820
#define AFE_CONN061_1                        0x2824
#define AFE_CONN061_2                        0x2828
#define AFE_CONN061_4                        0x2830
#define AFE_CONN061_5                        0x2834
#define AFE_CONN061_6                        0x2838
#define AFE_CONN061_7                        0x283c
#define AFE_CONN062_0                        0x2840
#define AFE_CONN062_1                        0x2844
#define AFE_CONN062_2                        0x2848
#define AFE_CONN062_4                        0x2850
#define AFE_CONN062_5                        0x2854
#define AFE_CONN062_6                        0x2858
#define AFE_CONN062_7                        0x285c
#define AFE_CONN063_0                        0x2860
#define AFE_CONN063_1                        0x2864
#define AFE_CONN063_2                        0x2868
#define AFE_CONN063_4                        0x2870
#define AFE_CONN063_5                        0x2874
#define AFE_CONN063_6                        0x2878
#define AFE_CONN063_7                        0x287c
#define AFE_CONN064_0                        0x2880
#define AFE_CONN064_1                        0x2884
#define AFE_CONN064_2                        0x2888
#define AFE_CONN064_4                        0x2890
#define AFE_CONN064_5                        0x2894
#define AFE_CONN064_6                        0x2898
#define AFE_CONN064_7                        0x289c
#define AFE_CONN065_0                        0x28a0
#define AFE_CONN065_1                        0x28a4
#define AFE_CONN065_2                        0x28a8
#define AFE_CONN065_4                        0x28b0
#define AFE_CONN065_5                        0x28b4
#define AFE_CONN065_6                        0x28b8
#define AFE_CONN065_7                        0x28bc
#define AFE_CONN066_0                        0x28c0
#define AFE_CONN066_1                        0x28c4
#define AFE_CONN066_2                        0x28c8
#define AFE_CONN066_4                        0x28d0
#define AFE_CONN066_5                        0x28d4
#define AFE_CONN066_6                        0x28d8
#define AFE_CONN066_7                        0x28dc
#define AFE_CONN067_0                        0x28e0
#define AFE_CONN067_1                        0x28e4
#define AFE_CONN067_2                        0x28e8
#define AFE_CONN067_4                        0x28f0
#define AFE_CONN067_5                        0x28f4
#define AFE_CONN067_6                        0x28f8
#define AFE_CONN067_7                        0x28fc
#define AFE_CONN068_0                        0x2900
#define AFE_CONN068_1                        0x2904
#define AFE_CONN068_2                        0x2908
#define AFE_CONN068_4                        0x2910
#define AFE_CONN068_5                        0x2914
#define AFE_CONN068_6                        0x2918
#define AFE_CONN068_7                        0x291c
#define AFE_CONN069_0                        0x2920
#define AFE_CONN069_1                        0x2924
#define AFE_CONN069_2                        0x2928
#define AFE_CONN069_4                        0x2930
#define AFE_CONN069_5                        0x2934
#define AFE_CONN069_6                        0x2938
#define AFE_CONN069_7                        0x293c
#define AFE_CONN070_0                        0x2940
#define AFE_CONN070_1                        0x2944
#define AFE_CONN070_2                        0x2948
#define AFE_CONN070_4                        0x2950
#define AFE_CONN070_5                        0x2954
#define AFE_CONN070_6                        0x2958
#define AFE_CONN070_7                        0x295c
#define AFE_CONN071_0                        0x2960
#define AFE_CONN071_1                        0x2964
#define AFE_CONN071_2                        0x2968
#define AFE_CONN071_4                        0x2970
#define AFE_CONN071_5                        0x2974
#define AFE_CONN071_6                        0x2978
#define AFE_CONN071_7                        0x297c
#define AFE_CONN072_0                        0x2980
#define AFE_CONN072_1                        0x2984
#define AFE_CONN072_2                        0x2988
#define AFE_CONN072_4                        0x2990
#define AFE_CONN072_5                        0x2994
#define AFE_CONN072_6                        0x2998
#define AFE_CONN072_7                        0x299c
#define AFE_CONN073_0                        0x29a0
#define AFE_CONN073_1                        0x29a4
#define AFE_CONN073_2                        0x29a8
#define AFE_CONN073_4                        0x29b0
#define AFE_CONN073_5                        0x29b4
#define AFE_CONN073_6                        0x29b8
#define AFE_CONN073_7                        0x29bc
#define AFE_CONN074_0                        0x29c0
#define AFE_CONN074_1                        0x29c4
#define AFE_CONN074_2                        0x29c8
#define AFE_CONN074_4                        0x29d0
#define AFE_CONN074_5                        0x29d4
#define AFE_CONN074_6                        0x29d8
#define AFE_CONN074_7                        0x29dc
#define AFE_CONN075_0                        0x29e0
#define AFE_CONN075_1                        0x29e4
#define AFE_CONN075_2                        0x29e8
#define AFE_CONN075_4                        0x29f0
#define AFE_CONN075_5                        0x29f4
#define AFE_CONN075_6                        0x29f8
#define AFE_CONN075_7                        0x29fc
#define AFE_CONN076_0                        0x2a00
#define AFE_CONN076_1                        0x2a04
#define AFE_CONN076_2                        0x2a08
#define AFE_CONN076_4                        0x2a10
#define AFE_CONN076_5                        0x2a14
#define AFE_CONN076_6                        0x2a18
#define AFE_CONN076_7                        0x2a1c
#define AFE_CONN077_0                        0x2a20
#define AFE_CONN077_1                        0x2a24
#define AFE_CONN077_2                        0x2a28
#define AFE_CONN077_4                        0x2a30
#define AFE_CONN077_5                        0x2a34
#define AFE_CONN077_6                        0x2a38
#define AFE_CONN077_7                        0x2a3c
#define AFE_CONN078_0                        0x2a40
#define AFE_CONN078_1                        0x2a44
#define AFE_CONN078_2                        0x2a48
#define AFE_CONN078_4                        0x2a50
#define AFE_CONN078_5                        0x2a54
#define AFE_CONN078_6                        0x2a58
#define AFE_CONN078_7                        0x2a5c
#define AFE_CONN079_0                        0x2a60
#define AFE_CONN079_1                        0x2a64
#define AFE_CONN079_2                        0x2a68
#define AFE_CONN079_4                        0x2a70
#define AFE_CONN079_5                        0x2a74
#define AFE_CONN079_6                        0x2a78
#define AFE_CONN079_7                        0x2a7c
#define AFE_CONN080_0                        0x2a80
#define AFE_CONN080_1                        0x2a84
#define AFE_CONN080_2                        0x2a88
#define AFE_CONN080_4                        0x2a90
#define AFE_CONN080_5                        0x2a94
#define AFE_CONN080_6                        0x2a98
#define AFE_CONN080_7                        0x2a9c
#define AFE_CONN081_0                        0x2aa0
#define AFE_CONN081_1                        0x2aa4
#define AFE_CONN081_2                        0x2aa8
#define AFE_CONN081_4                        0x2ab0
#define AFE_CONN081_5                        0x2ab4
#define AFE_CONN081_6                        0x2ab8
#define AFE_CONN081_7                        0x2abc
#define AFE_CONN082_0                        0x2ac0
#define AFE_CONN082_1                        0x2ac4
#define AFE_CONN082_2                        0x2ac8
#define AFE_CONN082_4                        0x2ad0
#define AFE_CONN082_5                        0x2ad4
#define AFE_CONN082_6                        0x2ad8
#define AFE_CONN082_7                        0x2adc
#define AFE_CONN083_0                        0x2ae0
#define AFE_CONN083_1                        0x2ae4
#define AFE_CONN083_2                        0x2ae8
#define AFE_CONN083_4                        0x2af0
#define AFE_CONN083_5                        0x2af4
#define AFE_CONN083_6                        0x2af8
#define AFE_CONN083_7                        0x2afc
#define AFE_CONN084_0                        0x2b00
#define AFE_CONN084_1                        0x2b04
#define AFE_CONN084_2                        0x2b08
#define AFE_CONN084_4                        0x2b10
#define AFE_CONN084_5                        0x2b14
#define AFE_CONN084_6                        0x2b18
#define AFE_CONN084_7                        0x2b1c
#define AFE_CONN085_0                        0x2b20
#define AFE_CONN085_1                        0x2b24
#define AFE_CONN085_2                        0x2b28
#define AFE_CONN085_4                        0x2b30
#define AFE_CONN085_5                        0x2b34
#define AFE_CONN085_6                        0x2b38
#define AFE_CONN085_7                        0x2b3c
#define AFE_CONN086_0                        0x2b40
#define AFE_CONN086_1                        0x2b44
#define AFE_CONN086_2                        0x2b48
#define AFE_CONN086_4                        0x2b50
#define AFE_CONN086_5                        0x2b54
#define AFE_CONN086_6                        0x2b58
#define AFE_CONN086_7                        0x2b5c
#define AFE_CONN087_0                        0x2b60
#define AFE_CONN087_1                        0x2b64
#define AFE_CONN087_2                        0x2b68
#define AFE_CONN087_4                        0x2b70
#define AFE_CONN087_5                        0x2b74
#define AFE_CONN087_6                        0x2b78
#define AFE_CONN087_7                        0x2b7c
#define AFE_CONN088_0                        0x2b80
#define AFE_CONN088_1                        0x2b84
#define AFE_CONN088_2                        0x2b88
#define AFE_CONN088_4                        0x2b90
#define AFE_CONN088_5                        0x2b94
#define AFE_CONN088_6                        0x2b98
#define AFE_CONN088_7                        0x2b9c
#define AFE_CONN089_0                        0x2ba0
#define AFE_CONN089_1                        0x2ba4
#define AFE_CONN089_2                        0x2ba8
#define AFE_CONN089_4                        0x2bb0
#define AFE_CONN089_5                        0x2bb4
#define AFE_CONN089_6                        0x2bb8
#define AFE_CONN089_7                        0x2bbc
#define AFE_CONN090_0                        0x2bc0
#define AFE_CONN090_1                        0x2bc4
#define AFE_CONN090_2                        0x2bc8
#define AFE_CONN090_4                        0x2bd0
#define AFE_CONN090_5                        0x2bd4
#define AFE_CONN090_6                        0x2bd8
#define AFE_CONN090_7                        0x2bdc
#define AFE_CONN091_0                        0x2be0
#define AFE_CONN091_1                        0x2be4
#define AFE_CONN091_2                        0x2be8
#define AFE_CONN091_4                        0x2bf0
#define AFE_CONN091_5                        0x2bf4
#define AFE_CONN091_6                        0x2bf8
#define AFE_CONN091_7                        0x2bfc
#define AFE_CONN092_0                        0x2c00
#define AFE_CONN092_1                        0x2c04
#define AFE_CONN092_2                        0x2c08
#define AFE_CONN092_4                        0x2c10
#define AFE_CONN092_5                        0x2c14
#define AFE_CONN092_6                        0x2c18
#define AFE_CONN092_7                        0x2c1c
#define AFE_CONN093_0                        0x2c20
#define AFE_CONN093_1                        0x2c24
#define AFE_CONN093_2                        0x2c28
#define AFE_CONN093_4                        0x2c30
#define AFE_CONN093_5                        0x2c34
#define AFE_CONN093_6                        0x2c38
#define AFE_CONN093_7                        0x2c3c
#define AFE_CONN094_0                        0x2c40
#define AFE_CONN094_1                        0x2c44
#define AFE_CONN094_2                        0x2c48
#define AFE_CONN094_4                        0x2c50
#define AFE_CONN094_5                        0x2c54
#define AFE_CONN094_6                        0x2c58
#define AFE_CONN094_7                        0x2c5c
#define AFE_CONN095_0                        0x2c60
#define AFE_CONN095_1                        0x2c64
#define AFE_CONN095_2                        0x2c68
#define AFE_CONN095_4                        0x2c70
#define AFE_CONN095_5                        0x2c74
#define AFE_CONN095_6                        0x2c78
#define AFE_CONN095_7                        0x2c7c
#define AFE_CONN096_0                        0x2c80
#define AFE_CONN096_1                        0x2c84
#define AFE_CONN096_2                        0x2c88
#define AFE_CONN096_4                        0x2c90
#define AFE_CONN096_5                        0x2c94
#define AFE_CONN096_6                        0x2c98
#define AFE_CONN096_7                        0x2c9c
#define AFE_CONN097_0                        0x2ca0
#define AFE_CONN097_1                        0x2ca4
#define AFE_CONN097_2                        0x2ca8
#define AFE_CONN097_4                        0x2cb0
#define AFE_CONN097_5                        0x2cb4
#define AFE_CONN097_6                        0x2cb8
#define AFE_CONN097_7                        0x2cbc
#define AFE_CONN098_0                        0x2cc0
#define AFE_CONN098_1                        0x2cc4
#define AFE_CONN098_2                        0x2cc8
#define AFE_CONN098_4                        0x2cd0
#define AFE_CONN098_5                        0x2cd4
#define AFE_CONN098_6                        0x2cd8
#define AFE_CONN098_7                        0x2cdc
#define AFE_CONN099_0                        0x2ce0
#define AFE_CONN099_1                        0x2ce4
#define AFE_CONN099_2                        0x2ce8
#define AFE_CONN099_4                        0x2cf0
#define AFE_CONN099_5                        0x2cf4
#define AFE_CONN099_6                        0x2cf8
#define AFE_CONN099_7                        0x2cfc
#define AFE_CONN100_0                        0x2d00
#define AFE_CONN100_1                        0x2d04
#define AFE_CONN100_2                        0x2d08
#define AFE_CONN100_4                        0x2d10
#define AFE_CONN100_5                        0x2d14
#define AFE_CONN100_6                        0x2d18
#define AFE_CONN100_7                        0x2d1c
#define AFE_CONN102_0                        0x2d40
#define AFE_CONN102_1                        0x2d44
#define AFE_CONN102_2                        0x2d48
#define AFE_CONN102_4                        0x2d50
#define AFE_CONN102_5                        0x2d54
#define AFE_CONN102_6                        0x2d58
#define AFE_CONN102_7                        0x2d5c
#define AFE_CONN103_0                        0x2d60
#define AFE_CONN103_1                        0x2d64
#define AFE_CONN103_2                        0x2d68
#define AFE_CONN103_4                        0x2d70
#define AFE_CONN103_5                        0x2d74
#define AFE_CONN103_6                        0x2d78
#define AFE_CONN103_7                        0x2d7c
#define AFE_CONN104_0                        0x2d80
#define AFE_CONN104_1                        0x2d84
#define AFE_CONN104_2                        0x2d88
#define AFE_CONN104_4                        0x2d90
#define AFE_CONN104_5                        0x2d94
#define AFE_CONN104_6                        0x2d98
#define AFE_CONN104_7                        0x2d9c
#define AFE_CONN105_0                        0x2da0
#define AFE_CONN105_1                        0x2da4
#define AFE_CONN105_2                        0x2da8
#define AFE_CONN105_4                        0x2db0
#define AFE_CONN105_5                        0x2db4
#define AFE_CONN105_6                        0x2db8
#define AFE_CONN105_7                        0x2dbc
#define AFE_CONN106_0                        0x2dc0
#define AFE_CONN106_1                        0x2dc4
#define AFE_CONN106_2                        0x2dc8
#define AFE_CONN106_4                        0x2dd0
#define AFE_CONN106_5                        0x2dd4
#define AFE_CONN106_6                        0x2dd8
#define AFE_CONN106_7                        0x2ddc
#define AFE_CONN108_0                        0x2e00
#define AFE_CONN108_1                        0x2e04
#define AFE_CONN108_2                        0x2e08
#define AFE_CONN108_4                        0x2e10
#define AFE_CONN108_5                        0x2e14
#define AFE_CONN108_6                        0x2e18
#define AFE_CONN108_7                        0x2e1c
#define AFE_CONN109_0                        0x2e20
#define AFE_CONN109_1                        0x2e24
#define AFE_CONN109_2                        0x2e28
#define AFE_CONN109_4                        0x2e30
#define AFE_CONN109_5                        0x2e34
#define AFE_CONN109_6                        0x2e38
#define AFE_CONN109_7                        0x2e3c
#define AFE_CONN110_0                        0x2e40
#define AFE_CONN110_1                        0x2e44
#define AFE_CONN110_2                        0x2e48
#define AFE_CONN110_4                        0x2e50
#define AFE_CONN110_5                        0x2e54
#define AFE_CONN110_6                        0x2e58
#define AFE_CONN110_7                        0x2e5c
#define AFE_CONN111_0                        0x2e60
#define AFE_CONN111_1                        0x2e64
#define AFE_CONN111_2                        0x2e68
#define AFE_CONN111_4                        0x2e70
#define AFE_CONN111_5                        0x2e74
#define AFE_CONN111_6                        0x2e78
#define AFE_CONN111_7                        0x2e7c
#define AFE_CONN112_0                        0x2e80
#define AFE_CONN112_1                        0x2e84
#define AFE_CONN112_2                        0x2e88
#define AFE_CONN112_4                        0x2e90
#define AFE_CONN112_5                        0x2e94
#define AFE_CONN112_6                        0x2e98
#define AFE_CONN112_7                        0x2e9c
#define AFE_CONN113_0                        0x2ea0
#define AFE_CONN113_1                        0x2ea4
#define AFE_CONN113_2                        0x2ea8
#define AFE_CONN113_4                        0x2eb0
#define AFE_CONN113_5                        0x2eb4
#define AFE_CONN113_6                        0x2eb8
#define AFE_CONN113_7                        0x2ebc
#define AFE_CONN114_0                        0x2ec0
#define AFE_CONN114_1                        0x2ec4
#define AFE_CONN114_2                        0x2ec8
#define AFE_CONN114_4                        0x2ed0
#define AFE_CONN114_5                        0x2ed4
#define AFE_CONN114_6                        0x2ed8
#define AFE_CONN114_7                        0x2edc
#define AFE_CONN115_0                        0x2ee0
#define AFE_CONN115_1                        0x2ee4
#define AFE_CONN115_2                        0x2ee8
#define AFE_CONN115_4                        0x2ef0
#define AFE_CONN115_5                        0x2ef4
#define AFE_CONN115_6                        0x2ef8
#define AFE_CONN115_7                        0x2efc
#define AFE_CONN116_0                        0x2f00
#define AFE_CONN116_1                        0x2f04
#define AFE_CONN116_2                        0x2f08
#define AFE_CONN116_4                        0x2f10
#define AFE_CONN116_5                        0x2f14
#define AFE_CONN116_6                        0x2f18
#define AFE_CONN116_7                        0x2f1c
#define AFE_CONN117_0                        0x2f20
#define AFE_CONN117_1                        0x2f24
#define AFE_CONN117_2                        0x2f28
#define AFE_CONN117_4                        0x2f30
#define AFE_CONN117_5                        0x2f34
#define AFE_CONN117_6                        0x2f38
#define AFE_CONN117_7                        0x2f3c
#define AFE_CONN118_0                        0x2f40
#define AFE_CONN118_1                        0x2f44
#define AFE_CONN118_2                        0x2f48
#define AFE_CONN118_4                        0x2f50
#define AFE_CONN118_5                        0x2f54
#define AFE_CONN118_6                        0x2f58
#define AFE_CONN118_7                        0x2f5c
#define AFE_CONN119_0                        0x2f60
#define AFE_CONN119_1                        0x2f64
#define AFE_CONN119_2                        0x2f68
#define AFE_CONN119_4                        0x2f70
#define AFE_CONN119_5                        0x2f74
#define AFE_CONN119_6                        0x2f78
#define AFE_CONN119_7                        0x2f7c
#define AFE_CONN120_0                        0x2f80
#define AFE_CONN120_1                        0x2f84
#define AFE_CONN120_2                        0x2f88
#define AFE_CONN120_4                        0x2f90
#define AFE_CONN120_5                        0x2f94
#define AFE_CONN120_6                        0x2f98
#define AFE_CONN120_7                        0x2f9c
#define AFE_CONN121_0                        0x2fa0
#define AFE_CONN121_1                        0x2fa4
#define AFE_CONN121_2                        0x2fa8
#define AFE_CONN121_4                        0x2fb0
#define AFE_CONN121_5                        0x2fb4
#define AFE_CONN121_6                        0x2fb8
#define AFE_CONN121_7                        0x2fbc
#define AFE_CONN122_0                        0x2fc0
#define AFE_CONN122_1                        0x2fc4
#define AFE_CONN122_2                        0x2fc8
#define AFE_CONN122_4                        0x2fd0
#define AFE_CONN122_5                        0x2fd4
#define AFE_CONN122_6                        0x2fd8
#define AFE_CONN122_7                        0x2fdc
#define AFE_CONN123_0                        0x2fe0
#define AFE_CONN123_1                        0x2fe4
#define AFE_CONN123_2                        0x2fe8
#define AFE_CONN123_4                        0x2ff0
#define AFE_CONN123_5                        0x2ff4
#define AFE_CONN123_6                        0x2ff8
#define AFE_CONN123_7                        0x2ffc
#define AFE_CONN124_0                        0x3000
#define AFE_CONN124_1                        0x3004
#define AFE_CONN124_2                        0x3008
#define AFE_CONN124_4                        0x3010
#define AFE_CONN124_5                        0x3014
#define AFE_CONN124_6                        0x3018
#define AFE_CONN124_7                        0x301c
#define AFE_CONN125_0                        0x3020
#define AFE_CONN125_1                        0x3024
#define AFE_CONN125_2                        0x3028
#define AFE_CONN125_4                        0x3030
#define AFE_CONN125_5                        0x3034
#define AFE_CONN125_6                        0x3038
#define AFE_CONN125_7                        0x303c
#define AFE_CONN126_0                        0x3040
#define AFE_CONN126_1                        0x3044
#define AFE_CONN126_2                        0x3048
#define AFE_CONN126_4                        0x3050
#define AFE_CONN126_5                        0x3054
#define AFE_CONN126_6                        0x3058
#define AFE_CONN126_7                        0x305c
#define AFE_CONN127_0                        0x3060
#define AFE_CONN127_1                        0x3064
#define AFE_CONN127_2                        0x3068
#define AFE_CONN127_4                        0x3070
#define AFE_CONN127_5                        0x3074
#define AFE_CONN127_6                        0x3078
#define AFE_CONN127_7                        0x307c
#define AFE_CONN128_0                        0x3080
#define AFE_CONN128_1                        0x3084
#define AFE_CONN128_2                        0x3088
#define AFE_CONN128_4                        0x3090
#define AFE_CONN128_5                        0x3094
#define AFE_CONN128_6                        0x3098
#define AFE_CONN128_7                        0x309c
#define AFE_CONN129_0                        0x30a0
#define AFE_CONN129_1                        0x30a4
#define AFE_CONN129_2                        0x30a8
#define AFE_CONN129_4                        0x30b0
#define AFE_CONN129_5                        0x30b4
#define AFE_CONN129_6                        0x30b8
#define AFE_CONN129_7                        0x30bc
#define AFE_CONN130_0                        0x30c0
#define AFE_CONN130_1                        0x30c4
#define AFE_CONN130_2                        0x30c8
#define AFE_CONN130_4                        0x30d0
#define AFE_CONN130_5                        0x30d4
#define AFE_CONN130_6                        0x30d8
#define AFE_CONN130_7                        0x30dc
#define AFE_CONN131_0                        0x30e0
#define AFE_CONN131_1                        0x30e4
#define AFE_CONN131_2                        0x30e8
#define AFE_CONN131_4                        0x30f0
#define AFE_CONN131_5                        0x30f4
#define AFE_CONN131_6                        0x30f8
#define AFE_CONN131_7                        0x30fc
#define AFE_CONN132_0                        0x3100
#define AFE_CONN132_1                        0x3104
#define AFE_CONN132_2                        0x3108
#define AFE_CONN132_4                        0x3110
#define AFE_CONN132_5                        0x3114
#define AFE_CONN132_6                        0x3118
#define AFE_CONN132_7                        0x311c
#define AFE_CONN133_0                        0x3120
#define AFE_CONN133_1                        0x3124
#define AFE_CONN133_2                        0x3128
#define AFE_CONN133_4                        0x3130
#define AFE_CONN133_5                        0x3134
#define AFE_CONN133_6                        0x3138
#define AFE_CONN133_7                        0x313c
#define AFE_CONN134_0                        0x3140
#define AFE_CONN134_1                        0x3144
#define AFE_CONN134_2                        0x3148
#define AFE_CONN134_4                        0x3150
#define AFE_CONN134_5                        0x3154
#define AFE_CONN134_6                        0x3158
#define AFE_CONN134_7                        0x315c
#define AFE_CONN135_0                        0x3160
#define AFE_CONN135_1                        0x3164
#define AFE_CONN135_2                        0x3168
#define AFE_CONN135_4                        0x3170
#define AFE_CONN135_5                        0x3174
#define AFE_CONN135_6                        0x3178
#define AFE_CONN135_7                        0x317c
#define AFE_CONN136_0                        0x3180
#define AFE_CONN136_1                        0x3184
#define AFE_CONN136_2                        0x3188
#define AFE_CONN136_4                        0x3190
#define AFE_CONN136_5                        0x3194
#define AFE_CONN136_6                        0x3198
#define AFE_CONN136_7                        0x319c
#define AFE_CONN137_0                        0x31a0
#define AFE_CONN137_1                        0x31a4
#define AFE_CONN137_2                        0x31a8
#define AFE_CONN137_4                        0x31b0
#define AFE_CONN137_5                        0x31b4
#define AFE_CONN137_6                        0x31b8
#define AFE_CONN137_7                        0x31bc
#define AFE_CONN138_0                        0x31c0
#define AFE_CONN138_1                        0x31c4
#define AFE_CONN138_2                        0x31c8
#define AFE_CONN138_4                        0x31d0
#define AFE_CONN138_5                        0x31d4
#define AFE_CONN138_6                        0x31d8
#define AFE_CONN138_7                        0x31dc
#define AFE_CONN139_0                        0x31e0
#define AFE_CONN139_1                        0x31e4
#define AFE_CONN139_2                        0x31e8
#define AFE_CONN139_4                        0x31f0
#define AFE_CONN139_5                        0x31f4
#define AFE_CONN139_6                        0x31f8
#define AFE_CONN139_7                        0x31fc
#define AFE_CONN148_0                        0x3300
#define AFE_CONN148_1                        0x3304
#define AFE_CONN148_2                        0x3308
#define AFE_CONN148_4                        0x3310
#define AFE_CONN148_5                        0x3314
#define AFE_CONN148_6                        0x3318
#define AFE_CONN148_7                        0x331c
#define AFE_CONN149_0                        0x3320
#define AFE_CONN149_1                        0x3324
#define AFE_CONN149_2                        0x3328
#define AFE_CONN149_4                        0x3330
#define AFE_CONN149_5                        0x3334
#define AFE_CONN149_6                        0x3338
#define AFE_CONN149_7                        0x333c
#define AFE_CONN180_0                        0x3700
#define AFE_CONN180_1                        0x3704
#define AFE_CONN180_2                        0x3708
#define AFE_CONN180_4                        0x3710
#define AFE_CONN180_5                        0x3714
#define AFE_CONN180_6                        0x3718
#define AFE_CONN180_7                        0x371c
#define AFE_CONN181_0                        0x3720
#define AFE_CONN181_1                        0x3724
#define AFE_CONN181_2                        0x3728
#define AFE_CONN181_4                        0x3730
#define AFE_CONN181_5                        0x3734
#define AFE_CONN181_6                        0x3738
#define AFE_CONN181_7                        0x373c
#define AFE_CONN182_0                        0x3740
#define AFE_CONN182_1                        0x3744
#define AFE_CONN182_2                        0x3748
#define AFE_CONN182_4                        0x3750
#define AFE_CONN182_5                        0x3754
#define AFE_CONN182_6                        0x3758
#define AFE_CONN182_7                        0x375c
#define AFE_CONN183_0                        0x3760
#define AFE_CONN183_1                        0x3764
#define AFE_CONN183_2                        0x3768
#define AFE_CONN183_4                        0x3770
#define AFE_CONN183_5                        0x3774
#define AFE_CONN183_6                        0x3778
#define AFE_CONN183_7                        0x377c
#define AFE_CONN184_0                        0x3780
#define AFE_CONN184_1                        0x3784
#define AFE_CONN184_2                        0x3788
#define AFE_CONN184_4                        0x3790
#define AFE_CONN184_5                        0x3794
#define AFE_CONN184_6                        0x3798
#define AFE_CONN184_7                        0x379c
#define AFE_CONN185_0                        0x37a0
#define AFE_CONN185_1                        0x37a4
#define AFE_CONN185_2                        0x37a8
#define AFE_CONN185_4                        0x37b0
#define AFE_CONN185_5                        0x37b4
#define AFE_CONN185_6                        0x37b8
#define AFE_CONN185_7                        0x37bc
#define AFE_CONN186_0                        0x37c0
#define AFE_CONN186_1                        0x37c4
#define AFE_CONN186_2                        0x37c8
#define AFE_CONN186_4                        0x37d0
#define AFE_CONN186_5                        0x37d4
#define AFE_CONN186_6                        0x37d8
#define AFE_CONN186_7                        0x37dc
#define AFE_CONN187_0                        0x37e0
#define AFE_CONN187_1                        0x37e4
#define AFE_CONN187_2                        0x37e8
#define AFE_CONN187_4                        0x37f0
#define AFE_CONN187_5                        0x37f4
#define AFE_CONN187_6                        0x37f8
#define AFE_CONN187_7                        0x37fc
#define AFE_CONN188_0                        0x3800
#define AFE_CONN188_1                        0x3804
#define AFE_CONN188_2                        0x3808
#define AFE_CONN188_4                        0x3810
#define AFE_CONN188_5                        0x3814
#define AFE_CONN188_6                        0x3818
#define AFE_CONN188_7                        0x381c
#define AFE_CONN189_0                        0x3820
#define AFE_CONN189_1                        0x3824
#define AFE_CONN189_2                        0x3828
#define AFE_CONN189_4                        0x3830
#define AFE_CONN189_5                        0x3834
#define AFE_CONN189_6                        0x3838
#define AFE_CONN189_7                        0x383c
#define AFE_CONN190_0                        0x3840
#define AFE_CONN190_1                        0x3844
#define AFE_CONN190_2                        0x3848
#define AFE_CONN190_4                        0x3850
#define AFE_CONN190_5                        0x3854
#define AFE_CONN190_6                        0x3858
#define AFE_CONN190_7                        0x385c
#define AFE_CONN191_0                        0x3860
#define AFE_CONN191_1                        0x3864
#define AFE_CONN191_2                        0x3868
#define AFE_CONN191_4                        0x3870
#define AFE_CONN191_5                        0x3874
#define AFE_CONN191_6                        0x3878
#define AFE_CONN191_7                        0x387c
#define AFE_CONN192_0                        0x3880
#define AFE_CONN192_1                        0x3884
#define AFE_CONN192_2                        0x3888
#define AFE_CONN192_4                        0x3890
#define AFE_CONN192_5                        0x3894
#define AFE_CONN192_6                        0x3898
#define AFE_CONN192_7                        0x389c
#define AFE_CONN193_0                        0x38a0
#define AFE_CONN193_1                        0x38a4
#define AFE_CONN193_2                        0x38a8
#define AFE_CONN193_4                        0x38b0
#define AFE_CONN193_5                        0x38b4
#define AFE_CONN193_6                        0x38b8
#define AFE_CONN193_7                        0x38bc
#define AFE_CONN194_0                        0x38c0
#define AFE_CONN194_1                        0x38c4
#define AFE_CONN194_2                        0x38c8
#define AFE_CONN194_4                        0x38d0
#define AFE_CONN194_5                        0x38d4
#define AFE_CONN194_6                        0x38d8
#define AFE_CONN194_7                        0x38dc
#define AFE_CONN195_0                        0x38e0
#define AFE_CONN195_1                        0x38e4
#define AFE_CONN195_2                        0x38e8
#define AFE_CONN195_4                        0x38f0
#define AFE_CONN195_5                        0x38f4
#define AFE_CONN195_6                        0x38f8
#define AFE_CONN195_7                        0x38fc
#define AFE_CONN196_0                        0x3900
#define AFE_CONN196_1                        0x3904
#define AFE_CONN196_2                        0x3908
#define AFE_CONN196_4                        0x3910
#define AFE_CONN196_5                        0x3914
#define AFE_CONN196_6                        0x3918
#define AFE_CONN196_7                        0x391c
#define AFE_CONN197_0                        0x3920
#define AFE_CONN197_1                        0x3924
#define AFE_CONN197_2                        0x3928
#define AFE_CONN197_4                        0x3930
#define AFE_CONN197_5                        0x3934
#define AFE_CONN197_6                        0x3938
#define AFE_CONN197_7                        0x393c
#define AFE_CONN198_0                        0x3940
#define AFE_CONN198_1                        0x3944
#define AFE_CONN198_2                        0x3948
#define AFE_CONN198_4                        0x3950
#define AFE_CONN198_5                        0x3954
#define AFE_CONN198_6                        0x3958
#define AFE_CONN198_7                        0x395c
#define AFE_CONN199_0                        0x3960
#define AFE_CONN199_1                        0x3964
#define AFE_CONN199_2                        0x3968
#define AFE_CONN199_4                        0x3970
#define AFE_CONN199_5                        0x3974
#define AFE_CONN199_6                        0x3978
#define AFE_CONN199_7                        0x397c
#define AFE_CONN200_0                        0x3980
#define AFE_CONN200_1                        0x3984
#define AFE_CONN200_2                        0x3988
#define AFE_CONN200_4                        0x3990
#define AFE_CONN200_5                        0x3994
#define AFE_CONN200_6                        0x3998
#define AFE_CONN200_7                        0x399c
#define AFE_CONN201_0                        0x39a0
#define AFE_CONN201_1                        0x39a4
#define AFE_CONN201_2                        0x39a8
#define AFE_CONN201_4                        0x39b0
#define AFE_CONN201_5                        0x39b4
#define AFE_CONN201_6                        0x39b8
#define AFE_CONN201_7                        0x39bc
#define AFE_CONN202_0                        0x39c0
#define AFE_CONN202_1                        0x39c4
#define AFE_CONN202_2                        0x39c8
#define AFE_CONN202_4                        0x39d0
#define AFE_CONN202_5                        0x39d4
#define AFE_CONN202_6                        0x39d8
#define AFE_CONN202_7                        0x39dc
#define AFE_CONN203_0                        0x39e0
#define AFE_CONN203_1                        0x39e4
#define AFE_CONN203_2                        0x39e8
#define AFE_CONN203_4                        0x39f0
#define AFE_CONN203_5                        0x39f4
#define AFE_CONN203_6                        0x39f8
#define AFE_CONN203_7                        0x39fc
#define AFE_CONN204_0                        0x3a00
#define AFE_CONN204_1                        0x3a04
#define AFE_CONN204_2                        0x3a08
#define AFE_CONN204_4                        0x3a10
#define AFE_CONN204_5                        0x3a14
#define AFE_CONN204_6                        0x3a18
#define AFE_CONN204_7                        0x3a1c
#define AFE_CONN205_0                        0x3a20
#define AFE_CONN205_1                        0x3a24
#define AFE_CONN205_2                        0x3a28
#define AFE_CONN205_4                        0x3a30
#define AFE_CONN205_5                        0x3a34
#define AFE_CONN205_6                        0x3a38
#define AFE_CONN205_7                        0x3a3c
#define AFE_CONN206_0                        0x3a40
#define AFE_CONN206_1                        0x3a44
#define AFE_CONN206_2                        0x3a48
#define AFE_CONN206_4                        0x3a50
#define AFE_CONN206_5                        0x3a54
#define AFE_CONN206_6                        0x3a58
#define AFE_CONN206_7                        0x3a5c
#define AFE_CONN207_0                        0x3a60
#define AFE_CONN207_1                        0x3a64
#define AFE_CONN207_2                        0x3a68
#define AFE_CONN207_4                        0x3a70
#define AFE_CONN207_5                        0x3a74
#define AFE_CONN207_6                        0x3a78
#define AFE_CONN207_7                        0x3a7c
#define AFE_CONN208_0                        0x3a80
#define AFE_CONN208_1                        0x3a84
#define AFE_CONN208_2                        0x3a88
#define AFE_CONN208_4                        0x3a90
#define AFE_CONN208_5                        0x3a94
#define AFE_CONN208_6                        0x3a98
#define AFE_CONN208_7                        0x3a9c
#define AFE_CONN209_0                        0x3aa0
#define AFE_CONN209_1                        0x3aa4
#define AFE_CONN209_2                        0x3aa8
#define AFE_CONN209_4                        0x3ab0
#define AFE_CONN209_5                        0x3ab4
#define AFE_CONN209_6                        0x3ab8
#define AFE_CONN209_7                        0x3abc
#define AFE_CONN210_0                        0x3ac0
#define AFE_CONN210_1                        0x3ac4
#define AFE_CONN210_2                        0x3ac8
#define AFE_CONN210_4                        0x3ad0
#define AFE_CONN210_5                        0x3ad4
#define AFE_CONN210_6                        0x3ad8
#define AFE_CONN210_7                        0x3adc
#define AFE_CONN211_0                        0x3ae0
#define AFE_CONN211_1                        0x3ae4
#define AFE_CONN211_2                        0x3ae8
#define AFE_CONN211_4                        0x3af0
#define AFE_CONN211_5                        0x3af4
#define AFE_CONN211_6                        0x3af8
#define AFE_CONN211_7                        0x3afc
#define AFE_CONN_MON_CFG                     0x4080
#define AFE_CONN_MON0                        0x4084
#define AFE_CONN_MON1                        0x4088
#define AFE_CONN_MON2                        0x408c
#define AFE_CONN_MON3                        0x4090
#define AFE_CONN_MON4                        0x4094
#define AFE_CONN_MON5                        0x4098
#define AFE_CONN_RS_0                        0x40a0
#define AFE_CONN_RS_1                        0x40a4
#define AFE_CONN_RS_2                        0x40a8
#define AFE_CONN_RS_3                        0x40ac
#define AFE_CONN_RS_4                        0x40b0
#define AFE_CONN_RS_5                        0x40b4
#define AFE_CONN_RS_6                        0x40b8
#define AFE_CONN_DI_0                        0x40c0
#define AFE_CONN_DI_1                        0x40c4
#define AFE_CONN_DI_2                        0x40c8
#define AFE_CONN_DI_3                        0x40cc
#define AFE_CONN_DI_4                        0x40d0
#define AFE_CONN_DI_5                        0x40d4
#define AFE_CONN_DI_6                        0x40d8
#define AFE_CONN_16BIT_0                     0x40e0
#define AFE_CONN_16BIT_1                     0x40e4
#define AFE_CONN_16BIT_2                     0x40e8
#define AFE_CONN_16BIT_3                     0x40ec
#define AFE_CONN_16BIT_4                     0x40f0
#define AFE_CONN_16BIT_5                     0x40f4
#define AFE_CONN_16BIT_6                     0x40f8
#define AFE_CONN_24BIT_0                     0x4100
#define AFE_CONN_24BIT_1                     0x4104
#define AFE_CONN_24BIT_2                     0x4108
#define AFE_CONN_24BIT_3                     0x410c
#define AFE_CONN_24BIT_4                     0x4110
#define AFE_CONN_24BIT_5                     0x4114
#define AFE_CONN_24BIT_6                     0x4118
#define AFE_CBIP_CFG0                        0x4380
#define AFE_CBIP_SLV_DECODER_MON0            0x4384
#define AFE_CBIP_SLV_DECODER_MON1            0x4388
#define AFE_CBIP_SLV_MUX_MON_CFG             0x438c
#define AFE_CBIP_SLV_MUX_MON0                0x4390
#define AFE_CBIP_SLV_MUX_MON1                0x4394
#define AFE_MEMIF_CON0                       0x4400
#define AFE_MEMIF_ONE_HEART                  0x4420
#define AFE_DL0_BASE_MSB                     0x4440
#define AFE_DL0_BASE                         0x4444
#define AFE_DL0_CUR_MSB                      0x4448
#define AFE_DL0_CUR                          0x444c
#define AFE_DL0_END_MSB                      0x4450
#define AFE_DL0_END                          0x4454
#define AFE_DL0_RCH_MON                      0x4458
#define AFE_DL0_LCH_MON                      0x445c
#define AFE_DL0_CON0                         0x4460
#define AFE_DL0_MON0                         0x4464
#define AFE_DL1_BASE_MSB                     0x4470
#define AFE_DL1_BASE                         0x4474
#define AFE_DL1_CUR_MSB                      0x4478
#define AFE_DL1_CUR                          0x447c
#define AFE_DL1_END_MSB                      0x4480
#define AFE_DL1_END                          0x4484
#define AFE_DL1_RCH_MON                      0x4488
#define AFE_DL1_LCH_MON                      0x448c
#define AFE_DL1_CON0                         0x4490
#define AFE_DL1_MON0                         0x4494
#define AFE_DL2_BASE_MSB                     0x44a0
#define AFE_DL2_BASE                         0x44a4
#define AFE_DL2_CUR_MSB                      0x44a8
#define AFE_DL2_CUR                          0x44ac
#define AFE_DL2_END_MSB                      0x44b0
#define AFE_DL2_END                          0x44b4
#define AFE_DL2_RCH_MON                      0x44b8
#define AFE_DL2_LCH_MON                      0x44bc
#define AFE_DL2_CON0                         0x44c0
#define AFE_DL2_MON0                         0x44c4
#define AFE_DL3_BASE_MSB                     0x44d0
#define AFE_DL3_BASE                         0x44d4
#define AFE_DL3_CUR_MSB                      0x44d8
#define AFE_DL3_CUR                          0x44dc
#define AFE_DL3_END_MSB                      0x44e0
#define AFE_DL3_END                          0x44e4
#define AFE_DL3_RCH_MON                      0x44e8
#define AFE_DL3_LCH_MON                      0x44ec
#define AFE_DL3_CON0                         0x44f0
#define AFE_DL3_MON0                         0x44f4
#define AFE_DL4_BASE_MSB                     0x4500
#define AFE_DL4_BASE                         0x4504
#define AFE_DL4_CUR_MSB                      0x4508
#define AFE_DL4_CUR                          0x450c
#define AFE_DL4_END_MSB                      0x4510
#define AFE_DL4_END                          0x4514
#define AFE_DL4_RCH_MON                      0x4518
#define AFE_DL4_LCH_MON                      0x451c
#define AFE_DL4_CON0                         0x4520
#define AFE_DL4_MON0                         0x4524
#define AFE_DL5_BASE_MSB                     0x4530
#define AFE_DL5_BASE                         0x4534
#define AFE_DL5_CUR_MSB                      0x4538
#define AFE_DL5_CUR                          0x453c
#define AFE_DL5_END_MSB                      0x4540
#define AFE_DL5_END                          0x4544
#define AFE_DL5_RCH_MON                      0x4548
#define AFE_DL5_LCH_MON                      0x454c
#define AFE_DL5_CON0                         0x4550
#define AFE_DL5_MON0                         0x4554
#define AFE_DL6_BASE_MSB                     0x4560
#define AFE_DL6_BASE                         0x4564
#define AFE_DL6_CUR_MSB                      0x4568
#define AFE_DL6_CUR                          0x456c
#define AFE_DL6_END_MSB                      0x4570
#define AFE_DL6_END                          0x4574
#define AFE_DL6_RCH_MON                      0x4578
#define AFE_DL6_LCH_MON                      0x457c
#define AFE_DL6_CON0                         0x4580
#define AFE_DL6_MON0                         0x4584
#define AFE_DL7_BASE_MSB                     0x4590
#define AFE_DL7_BASE                         0x4594
#define AFE_DL7_CUR_MSB                      0x4598
#define AFE_DL7_CUR                          0x459c
#define AFE_DL7_END_MSB                      0x45a0
#define AFE_DL7_END                          0x45a4
#define AFE_DL7_RCH_MON                      0x45a8
#define AFE_DL7_LCH_MON                      0x45ac
#define AFE_DL7_CON0                         0x45b0
#define AFE_DL7_MON0                         0x45b4
#define AFE_DL8_BASE_MSB                     0x45c0
#define AFE_DL8_BASE                         0x45c4
#define AFE_DL8_CUR_MSB                      0x45c8
#define AFE_DL8_CUR                          0x45cc
#define AFE_DL8_END_MSB                      0x45d0
#define AFE_DL8_END                          0x45d4
#define AFE_DL8_RCH_MON                      0x45d8
#define AFE_DL8_LCH_MON                      0x45dc
#define AFE_DL8_CON0                         0x45e0
#define AFE_DL8_MON0                         0x45e4
#define AFE_DL_4CH_BASE_MSB                  0x45f0
#define AFE_DL_4CH_BASE                      0x45f4
#define AFE_DL_4CH_CUR_MSB                   0x45f8
#define AFE_DL_4CH_CUR                       0x45fc
#define AFE_DL_4CH_END_MSB                   0x4600
#define AFE_DL_4CH_END                       0x4604
#define AFE_DL_4CH_CON0                      0x4610
#define AFE_DL_4CH_MON0                      0x4618
#define AFE_DL_24CH_BASE_MSB                 0x4620
#define AFE_DL_24CH_BASE                     0x4624
#define AFE_DL_24CH_CUR_MSB                  0x4628
#define AFE_DL_24CH_CUR                      0x462c
#define AFE_DL_24CH_END_MSB                  0x4630
#define AFE_DL_24CH_END                      0x4634
#define AFE_DL_24CH_CON0                     0x4640
#define AFE_DL_24CH_MON0                     0x4648
#define AFE_DL23_BASE_MSB                    0x4680
#define AFE_DL23_BASE                        0x4684
#define AFE_DL23_CUR_MSB                     0x4688
#define AFE_DL23_CUR                         0x468c
#define AFE_DL23_END_MSB                     0x4690
#define AFE_DL23_END                         0x4694
#define AFE_DL23_RCH_MON                     0x4698
#define AFE_DL23_LCH_MON                     0x469c
#define AFE_DL23_CON0                        0x46a0
#define AFE_DL23_MON0                        0x46a4
#define AFE_DL24_BASE_MSB                    0x46b0
#define AFE_DL24_BASE                        0x46b4
#define AFE_DL24_CUR_MSB                     0x46b8
#define AFE_DL24_CUR                         0x46bc
#define AFE_DL24_END_MSB                     0x46c0
#define AFE_DL24_END                         0x46c4
#define AFE_DL24_RCH_MON                     0x46c8
#define AFE_DL24_LCH_MON                     0x46cc
#define AFE_DL24_CON0                        0x46d0
#define AFE_DL24_MON0                        0x46d4
#define AFE_DL25_BASE_MSB                    0x46e0
#define AFE_DL25_BASE                        0x46e4
#define AFE_DL25_CUR_MSB                     0x46e8
#define AFE_DL25_CUR                         0x46ec
#define AFE_DL25_END_MSB                     0x46f0
#define AFE_DL25_END                         0x46f4
#define AFE_DL25_RCH_MON                     0x46f8
#define AFE_DL25_LCH_MON                     0x46fc
#define AFE_DL25_CON0                        0x4700
#define AFE_DL25_MON0                        0x4704
#define AFE_DL26_BASE_MSB                    0x4710
#define AFE_DL26_BASE                        0x4714
#define AFE_DL26_CUR_MSB                     0x4718
#define AFE_DL26_CUR                         0x471c
#define AFE_DL26_END_MSB                     0x4720
#define AFE_DL26_END                         0x4724
#define AFE_DL26_RCH_MON                     0x4728
#define AFE_DL26_LCH_MON                     0x472c
#define AFE_DL26_CON0                        0x4730
#define AFE_DL26_MON0                        0x4734
#define AFE_VUL0_BASE_MSB                    0x4d60
#define AFE_VUL0_BASE                        0x4d64
#define AFE_VUL0_CUR_MSB                     0x4d68
#define AFE_VUL0_CUR                         0x4d6c
#define AFE_VUL0_END_MSB                     0x4d70
#define AFE_VUL0_END                         0x4d74
#define AFE_VUL0_RCH_MON                     0x4d78
#define AFE_VUL0_LCH_MON                     0x4d7c
#define AFE_VUL0_CON0                        0x4d80
#define AFE_VUL0_MON0                        0x4d84
#define AFE_VUL1_BASE_MSB                    0x4d90
#define AFE_VUL1_BASE                        0x4d94
#define AFE_VUL1_CUR_MSB                     0x4d98
#define AFE_VUL1_CUR                         0x4d9c
#define AFE_VUL1_END_MSB                     0x4da0
#define AFE_VUL1_END                         0x4da4
#define AFE_VUL1_RCH_MON                     0x4da8
#define AFE_VUL1_LCH_MON                     0x4dac
#define AFE_VUL1_CON0                        0x4db0
#define AFE_VUL1_MON0                        0x4db4
#define AFE_VUL2_BASE_MSB                    0x4dc0
#define AFE_VUL2_BASE                        0x4dc4
#define AFE_VUL2_CUR_MSB                     0x4dc8
#define AFE_VUL2_CUR                         0x4dcc
#define AFE_VUL2_END_MSB                     0x4dd0
#define AFE_VUL2_END                         0x4dd4
#define AFE_VUL2_RCH_MON                     0x4dd8
#define AFE_VUL2_LCH_MON                     0x4ddc
#define AFE_VUL2_CON0                        0x4de0
#define AFE_VUL2_MON0                        0x4de4
#define AFE_VUL3_BASE_MSB                    0x4df0
#define AFE_VUL3_BASE                        0x4df4
#define AFE_VUL3_CUR_MSB                     0x4df8
#define AFE_VUL3_CUR                         0x4dfc
#define AFE_VUL3_END_MSB                     0x4e00
#define AFE_VUL3_END                         0x4e04
#define AFE_VUL3_RCH_MON                     0x4e08
#define AFE_VUL3_LCH_MON                     0x4e0c
#define AFE_VUL3_CON0                        0x4e10
#define AFE_VUL3_MON0                        0x4e14
#define AFE_VUL4_BASE_MSB                    0x4e20
#define AFE_VUL4_BASE                        0x4e24
#define AFE_VUL4_CUR_MSB                     0x4e28
#define AFE_VUL4_CUR                         0x4e2c
#define AFE_VUL4_END_MSB                     0x4e30
#define AFE_VUL4_END                         0x4e34
#define AFE_VUL4_RCH_MON                     0x4e38
#define AFE_VUL4_LCH_MON                     0x4e3c
#define AFE_VUL4_CON0                        0x4e40
#define AFE_VUL4_MON0                        0x4e44
#define AFE_VUL5_BASE_MSB                    0x4e50
#define AFE_VUL5_BASE                        0x4e54
#define AFE_VUL5_CUR_MSB                     0x4e58
#define AFE_VUL5_CUR                         0x4e5c
#define AFE_VUL5_END_MSB                     0x4e60
#define AFE_VUL5_END                         0x4e64
#define AFE_VUL5_RCH_MON                     0x4e68
#define AFE_VUL5_LCH_MON                     0x4e6c
#define AFE_VUL5_CON0                        0x4e70
#define AFE_VUL5_MON0                        0x4e74
#define AFE_VUL6_BASE_MSB                    0x4e80
#define AFE_VUL6_BASE                        0x4e84
#define AFE_VUL6_CUR_MSB                     0x4e88
#define AFE_VUL6_CUR                         0x4e8c
#define AFE_VUL6_END_MSB                     0x4e90
#define AFE_VUL6_END                         0x4e94
#define AFE_VUL6_RCH_MON                     0x4e98
#define AFE_VUL6_LCH_MON                     0x4e9c
#define AFE_VUL6_CON0                        0x4ea0
#define AFE_VUL6_MON0                        0x4ea4
#define AFE_VUL7_BASE_MSB                    0x4eb0
#define AFE_VUL7_BASE                        0x4eb4
#define AFE_VUL7_CUR_MSB                     0x4eb8
#define AFE_VUL7_CUR                         0x4ebc
#define AFE_VUL7_END_MSB                     0x4ec0
#define AFE_VUL7_END                         0x4ec4
#define AFE_VUL7_RCH_MON                     0x4ec8
#define AFE_VUL7_LCH_MON                     0x4ecc
#define AFE_VUL7_CON0                        0x4ed0
#define AFE_VUL7_MON0                        0x4ed4
#define AFE_VUL8_BASE_MSB                    0x4ee0
#define AFE_VUL8_BASE                        0x4ee4
#define AFE_VUL8_CUR_MSB                     0x4ee8
#define AFE_VUL8_CUR                         0x4eec
#define AFE_VUL8_END_MSB                     0x4ef0
#define AFE_VUL8_END                         0x4ef4
#define AFE_VUL8_RCH_MON                     0x4ef8
#define AFE_VUL8_LCH_MON                     0x4efc
#define AFE_VUL8_CON0                        0x4f00
#define AFE_VUL8_MON0                        0x4f04
#define AFE_VUL9_BASE_MSB                    0x4f10
#define AFE_VUL9_BASE                        0x4f14
#define AFE_VUL9_CUR_MSB                     0x4f18
#define AFE_VUL9_CUR                         0x4f1c
#define AFE_VUL9_END_MSB                     0x4f20
#define AFE_VUL9_END                         0x4f24
#define AFE_VUL9_RCH_MON                     0x4f28
#define AFE_VUL9_LCH_MON                     0x4f2c
#define AFE_VUL9_CON0                        0x4f30
#define AFE_VUL9_MON0                        0x4f34
#define AFE_VUL10_BASE_MSB                   0x4f40
#define AFE_VUL10_BASE                       0x4f44
#define AFE_VUL10_CUR_MSB                    0x4f48
#define AFE_VUL10_CUR                        0x4f4c
#define AFE_VUL10_END_MSB                    0x4f50
#define AFE_VUL10_END                        0x4f54
#define AFE_VUL10_RCH_MON                    0x4f58
#define AFE_VUL10_LCH_MON                    0x4f5c
#define AFE_VUL10_CON0                       0x4f60
#define AFE_VUL10_MON0                       0x4f64
#define AFE_VUL24_BASE_MSB                   0x4fa0
#define AFE_VUL24_BASE                       0x4fa4
#define AFE_VUL24_CUR_MSB                    0x4fa8
#define AFE_VUL24_CUR                        0x4fac
#define AFE_VUL24_END_MSB                    0x4fb0
#define AFE_VUL24_END                        0x4fb4
#define AFE_VUL24_CON0                       0x4fb8
#define AFE_VUL24_MON0                       0x4fbc
#define AFE_VUL25_BASE_MSB                   0x4fc0
#define AFE_VUL25_BASE                       0x4fc4
#define AFE_VUL25_CUR_MSB                    0x4fc8
#define AFE_VUL25_CUR                        0x4fcc
#define AFE_VUL25_END_MSB                    0x4fd0
#define AFE_VUL25_END                        0x4fd4
#define AFE_VUL25_CON0                       0x4fd8
#define AFE_VUL25_MON0                       0x4fdc
#define AFE_VUL26_BASE_MSB                   0x4fe0
#define AFE_VUL26_BASE                       0x4fe4
#define AFE_VUL26_CUR_MSB                    0x4fe8
#define AFE_VUL26_CUR                        0x4fec
#define AFE_VUL26_END_MSB                    0x4ff0
#define AFE_VUL26_END                        0x4ff4
#define AFE_VUL26_CON0                       0x4ff8
#define AFE_VUL26_MON0                       0x4ffc
#define AFE_VUL_CM0_BASE_MSB                 0x51c0
#define AFE_VUL_CM0_BASE                     0x51c4
#define AFE_VUL_CM0_CUR_MSB                  0x51c8
#define AFE_VUL_CM0_CUR                      0x51cc
#define AFE_VUL_CM0_END_MSB                  0x51d0
#define AFE_VUL_CM0_END                      0x51d4
#define AFE_VUL_CM0_CON0                     0x51d8
#define AFE_VUL_CM1_BASE_MSB                 0x51e0
#define AFE_VUL_CM1_BASE                     0x51e4
#define AFE_VUL_CM1_CUR_MSB                  0x51e8
#define AFE_VUL_CM1_CUR                      0x51ec
#define AFE_VUL_CM1_END_MSB                  0x51f0
#define AFE_VUL_CM1_END                      0x51f4
#define AFE_VUL_CM1_CON0                     0x51f8
#define AFE_VUL_CM2_BASE_MSB                 0x5200
#define AFE_VUL_CM2_BASE                     0x5204
#define AFE_VUL_CM2_CUR_MSB                  0x5208
#define AFE_VUL_CM2_CUR                      0x520c
#define AFE_VUL_CM2_END_MSB                  0x5210
#define AFE_VUL_CM2_END                      0x5214
#define AFE_VUL_CM2_CON0                     0x5218
#define AFE_ETDM_IN0_BASE_MSB                0x5220
#define AFE_ETDM_IN0_BASE                    0x5224
#define AFE_ETDM_IN0_CUR_MSB                 0x5228
#define AFE_ETDM_IN0_CUR                     0x522c
#define AFE_ETDM_IN0_END_MSB                 0x5230
#define AFE_ETDM_IN0_END                     0x5234
#define AFE_ETDM_IN0_CON0                    0x5238
#define AFE_ETDM_IN1_BASE_MSB                0x5240
#define AFE_ETDM_IN1_BASE                    0x5244
#define AFE_ETDM_IN1_CUR_MSB                 0x5248
#define AFE_ETDM_IN1_CUR                     0x524c
#define AFE_ETDM_IN1_END_MSB                 0x5250
#define AFE_ETDM_IN1_END                     0x5254
#define AFE_ETDM_IN1_CON0                    0x5258
#define AFE_ETDM_IN2_BASE_MSB                0x5260
#define AFE_ETDM_IN2_BASE                    0x5264
#define AFE_ETDM_IN2_CUR_MSB                 0x5268
#define AFE_ETDM_IN2_CUR                     0x526c
#define AFE_ETDM_IN2_END_MSB                 0x5270
#define AFE_ETDM_IN2_END                     0x5274
#define AFE_ETDM_IN2_CON0                    0x5278
#define AFE_ETDM_IN3_BASE_MSB                0x5280
#define AFE_ETDM_IN3_BASE                    0x5284
#define AFE_ETDM_IN3_CUR_MSB                 0x5288
#define AFE_ETDM_IN3_CUR                     0x528c
#define AFE_ETDM_IN3_END_MSB                 0x5290
#define AFE_ETDM_IN3_END                     0x5294
#define AFE_ETDM_IN3_CON0                    0x5298
#define AFE_ETDM_IN4_BASE_MSB                0x52a0
#define AFE_ETDM_IN4_BASE                    0x52a4
#define AFE_ETDM_IN4_CUR_MSB                 0x52a8
#define AFE_ETDM_IN4_CUR                     0x52ac
#define AFE_ETDM_IN4_END_MSB                 0x52b0
#define AFE_ETDM_IN4_END                     0x52b4
#define AFE_ETDM_IN4_CON0                    0x52b8
#define AFE_ETDM_IN5_BASE_MSB                0x52c0
#define AFE_ETDM_IN5_BASE                    0x52c4
#define AFE_ETDM_IN5_CUR_MSB                 0x52c8
#define AFE_ETDM_IN5_CUR                     0x52cc
#define AFE_ETDM_IN5_END_MSB                 0x52d0
#define AFE_ETDM_IN5_END                     0x52d4
#define AFE_ETDM_IN5_CON0                    0x52d8
#define AFE_ETDM_IN6_BASE_MSB                0x52e0
#define AFE_ETDM_IN6_BASE                    0x52e4
#define AFE_ETDM_IN6_CUR_MSB                 0x52e8
#define AFE_ETDM_IN6_CUR                     0x52ec
#define AFE_ETDM_IN6_END_MSB                 0x52f0
#define AFE_ETDM_IN6_END                     0x52f4
#define AFE_ETDM_IN6_CON0                    0x52f8
#define AFE_HDMI_OUT_BASE_MSB                0x5360
#define AFE_HDMI_OUT_BASE                    0x5364
#define AFE_HDMI_OUT_CUR_MSB                 0x5368
#define AFE_HDMI_OUT_CUR                     0x536c
#define AFE_HDMI_OUT_END_MSB                 0x5370
#define AFE_HDMI_OUT_END                     0x5374
#define AFE_HDMI_OUT_CON0                    0x5378
#define AFE_VUL24_RCH_MON                    0x53e0
#define AFE_VUL24_LCH_MON                    0x53e4
#define AFE_VUL25_RCH_MON                    0x53e8
#define AFE_VUL25_LCH_MON                    0x53ec
#define AFE_VUL26_RCH_MON                    0x53f0
#define AFE_VUL26_LCH_MON                    0x53f4
#define AFE_VUL_CM0_RCH_MON                  0x5458
#define AFE_VUL_CM0_LCH_MON                  0x545c
#define AFE_VUL_CM1_RCH_MON                  0x5460
#define AFE_VUL_CM1_LCH_MON                  0x5464
#define AFE_VUL_CM2_RCH_MON                  0x5468
#define AFE_VUL_CM2_LCH_MON                  0x546c
#define AFE_DL_4CH_CH0_MON                   0x54f4
#define AFE_DL_4CH_CH1_MON                   0x54f8
#define AFE_DL_4CH_CH2_MON                   0x54fc
#define AFE_DL_4CH_CH3_MON                   0x5500
#define AFE_DL_24CH_CH0_MON                  0x5504
#define AFE_DL_24CH_CH1_MON                  0x5508
#define AFE_DL_24CH_CH2_MON                  0x550c
#define AFE_DL_24CH_CH3_MON                  0x5510
#define AFE_DL_24CH_CH4_MON                  0x5514
#define AFE_DL_24CH_CH5_MON                  0x5518
#define AFE_DL_24CH_CH6_MON                  0x551c
#define AFE_DL_24CH_CH7_MON                  0x5520
#define AFE_DL_24CH_CH8_MON                  0x5524
#define AFE_DL_24CH_CH9_MON                  0x5528
#define AFE_DL_24CH_CH10_MON                 0x552c
#define AFE_DL_24CH_CH11_MON                 0x5530
#define AFE_DL_24CH_CH12_MON                 0x5534
#define AFE_DL_24CH_CH13_MON                 0x5538
#define AFE_DL_24CH_CH14_MON                 0x553c
#define AFE_DL_24CH_CH15_MON                 0x5540
#define AFE_SRAM_BOUND                       0x5620
#define AFE_SECURE_CON0                      0x5624
#define AFE_SECURE_CON1                      0x5628
#define AFE_SE_SECURE_CON0                   0x5630
#define AFE_SE_SECURE_CON1                   0x5634
#define AFE_SE_SECURE_CON2                   0x5638
#define AFE_SE_SECURE_CON3                   0x563c
#define AFE_SE_PROT_SIDEBAND0                0x5640
#define AFE_SE_PROT_SIDEBAND1                0x5644
#define AFE_SE_PROT_SIDEBAND2                0x5648
#define AFE_SE_PROT_SIDEBAND3                0x564c
#define AFE_SE_DOMAIN_SIDEBAND0              0x5650
#define AFE_SE_DOMAIN_SIDEBAND1              0x5654
#define AFE_SE_DOMAIN_SIDEBAND2              0x5658
#define AFE_SE_DOMAIN_SIDEBAND3              0x565c
#define AFE_SE_DOMAIN_SIDEBAND4              0x5660
#define AFE_SE_DOMAIN_SIDEBAND5              0x5664
#define AFE_SE_DOMAIN_SIDEBAND6              0x5668
#define AFE_SE_DOMAIN_SIDEBAND7              0x566c
#define AFE_SE_DOMAIN_SIDEBAND8              0x5670
#define AFE_SE_DOMAIN_SIDEBAND9              0x5674
#define AFE_PROT_SIDEBAND0_MON               0x5678
#define AFE_PROT_SIDEBAND1_MON               0x567c
#define AFE_PROT_SIDEBAND2_MON               0x5680
#define AFE_PROT_SIDEBAND3_MON               0x5684
#define AFE_DOMAIN_SIDEBAND0_MON             0x5688
#define AFE_DOMAIN_SIDEBAND1_MON             0x568c
#define AFE_DOMAIN_SIDEBAND2_MON             0x5690
#define AFE_DOMAIN_SIDEBAND3_MON             0x5694
#define AFE_DOMAIN_SIDEBAND4_MON             0x5698
#define AFE_DOMAIN_SIDEBAND5_MON             0x569c
#define AFE_DOMAIN_SIDEBAND6_MON             0x56a0
#define AFE_DOMAIN_SIDEBAND7_MON             0x56a4
#define AFE_DOMAIN_SIDEBAND8_MON             0x56a8
#define AFE_DOMAIN_SIDEBAND9_MON             0x56ac
#define AFE_SECURE_CONN0                     0x56b0
#define AFE_SECURE_CONN_ETDM0                0x56b4
#define AFE_SECURE_CONN_ETDM1                0x56b8
#define AFE_SECURE_CONN_ETDM2                0x56bc
#define AFE_SECURE_SRAM_CON0                 0x56c0
#define AFE_SECURE_SRAM_CON1                 0x56c4
#define AFE_SE_CONN_INPUT_MASK0              0x56d0
#define AFE_SE_CONN_INPUT_MASK1              0x56d4
#define AFE_SE_CONN_INPUT_MASK2              0x56d8
#define AFE_SE_CONN_INPUT_MASK3              0x56dc
#define AFE_SE_CONN_INPUT_MASK4              0x56e0
#define AFE_SE_CONN_INPUT_MASK5              0x56e4
#define AFE_SE_CONN_INPUT_MASK6              0x56e8
#define AFE_SE_CONN_INPUT_MASK7              0x56ec
#define AFE_NON_SE_CONN_INPUT_MASK0          0x56f0
#define AFE_NON_SE_CONN_INPUT_MASK1          0x56f4
#define AFE_NON_SE_CONN_INPUT_MASK2          0x56f8
#define AFE_NON_SE_CONN_INPUT_MASK3          0x56fc
#define AFE_NON_SE_CONN_INPUT_MASK4          0x5700
#define AFE_NON_SE_CONN_INPUT_MASK5          0x5704
#define AFE_NON_SE_CONN_INPUT_MASK6          0x5708
#define AFE_NON_SE_CONN_INPUT_MASK7          0x570c
#define AFE_SE_CONN_OUTPUT_SEL0              0x5710
#define AFE_SE_CONN_OUTPUT_SEL1              0x5714
#define AFE_SE_CONN_OUTPUT_SEL2              0x5718
#define AFE_SE_CONN_OUTPUT_SEL3              0x571c
#define AFE_SE_CONN_OUTPUT_SEL4              0x5720
#define AFE_SE_CONN_OUTPUT_SEL5              0x5724
#define AFE_SE_CONN_OUTPUT_SEL6              0x5728
#define AFE_SE_CONN_OUTPUT_SEL7              0x572c
#define AFE_PCM0_INTF_CON1_MASK_MON          0x5730
#define AFE_PCM0_INTF_CON0_MASK_MON          0x5734
#define AFE_CONNSYS_I2S_CON_MASK_MON         0x5738
#define AFE_TDM_CON2_MASK_MON                0x5744
#define AFE_MTKAIF0_CFG0_MASK_MON            0x574c
#define AFE_MTKAIF1_CFG0_MASK_MON            0x5750
#define AFE_ADDA_UL0_SRC_CON0_MASK_MON       0x5754
#define AFE_ADDA_UL1_SRC_CON0_MASK_MON       0x5758
#define AFE_ADDA_UL2_SRC_CON0_MASK_MON       0x575c
#define AFE_ASRC_NEW_CON0                    0x7800
#define AFE_ASRC_NEW_CON1                    0x7804
#define AFE_ASRC_NEW_CON2                    0x7808
#define AFE_ASRC_NEW_CON3                    0x780c
#define AFE_ASRC_NEW_CON4                    0x7810
#define AFE_ASRC_NEW_CON5                    0x7814
#define AFE_ASRC_NEW_CON6                    0x7818
#define AFE_ASRC_NEW_CON7                    0x781c
#define AFE_ASRC_NEW_CON8                    0x7820
#define AFE_ASRC_NEW_CON9                    0x7824
#define AFE_ASRC_NEW_CON10                   0x7828
#define AFE_ASRC_NEW_CON11                   0x782c
#define AFE_ASRC_NEW_CON12                   0x7830
#define AFE_ASRC_NEW_CON13                   0x7834
#define AFE_ASRC_NEW_CON14                   0x7838
#define AFE_ASRC_NEW_IP_VERSION              0x783c
#define AFE_GASRC0_NEW_CON0                  0x7840
#define AFE_GASRC0_NEW_CON1                  0x7844
#define AFE_GASRC0_NEW_CON2                  0x7848
#define AFE_GASRC0_NEW_CON3                  0x784c
#define AFE_GASRC0_NEW_CON4                  0x7850
#define AFE_GASRC0_NEW_CON5                  0x7854
#define AFE_GASRC0_NEW_CON6                  0x7858
#define AFE_GASRC0_NEW_CON7                  0x785c
#define AFE_GASRC0_NEW_CON8                  0x7860
#define AFE_GASRC0_NEW_CON9                  0x7864
#define AFE_GASRC0_NEW_CON10                 0x7868
#define AFE_GASRC0_NEW_CON11                 0x786c
#define AFE_GASRC0_NEW_CON12                 0x7870
#define AFE_GASRC0_NEW_CON13                 0x7874
#define AFE_GASRC0_NEW_CON14                 0x7878
#define AFE_GASRC0_NEW_IP_VERSION            0x787c
#define AFE_GASRC1_NEW_CON0                  0x7880
#define AFE_GASRC1_NEW_CON1                  0x7884
#define AFE_GASRC1_NEW_CON2                  0x7888
#define AFE_GASRC1_NEW_CON3                  0x788c
#define AFE_GASRC1_NEW_CON4                  0x7890
#define AFE_GASRC1_NEW_CON5                  0x7894
#define AFE_GASRC1_NEW_CON6                  0x7898
#define AFE_GASRC1_NEW_CON7                  0x789c
#define AFE_GASRC1_NEW_CON8                  0x78a0
#define AFE_GASRC1_NEW_CON9                  0x78a4
#define AFE_GASRC1_NEW_CON10                 0x78a8
#define AFE_GASRC1_NEW_CON11                 0x78ac
#define AFE_GASRC1_NEW_CON12                 0x78b0
#define AFE_GASRC1_NEW_CON13                 0x78b4
#define AFE_GASRC1_NEW_CON14                 0x78b8
#define AFE_GASRC1_NEW_IP_VERSION            0x78bc
#define AFE_GASRC2_NEW_CON0                  0x78c0
#define AFE_GASRC2_NEW_CON1                  0x78c4
#define AFE_GASRC2_NEW_CON2                  0x78c8
#define AFE_GASRC2_NEW_CON3                  0x78cc
#define AFE_GASRC2_NEW_CON4                  0x78d0
#define AFE_GASRC2_NEW_CON5                  0x78d4
#define AFE_GASRC2_NEW_CON6                  0x78d8
#define AFE_GASRC2_NEW_CON7                  0x78dc
#define AFE_GASRC2_NEW_CON8                  0x78e0
#define AFE_GASRC2_NEW_CON9                  0x78e4
#define AFE_GASRC2_NEW_CON10                 0x78e8
#define AFE_GASRC2_NEW_CON11                 0x78ec
#define AFE_GASRC2_NEW_CON12                 0x78f0
#define AFE_GASRC2_NEW_CON13                 0x78f4
#define AFE_GASRC2_NEW_CON14                 0x78f8
#define AFE_GASRC2_NEW_IP_VERSION            0x78fc
#define AFE_GASRC3_NEW_CON0                  0x7900
#define AFE_GASRC3_NEW_CON1                  0x7904
#define AFE_GASRC3_NEW_CON2                  0x7908
#define AFE_GASRC3_NEW_CON3                  0x790c
#define AFE_GASRC3_NEW_CON4                  0x7910
#define AFE_GASRC3_NEW_CON5                  0x7914
#define AFE_GASRC3_NEW_CON6                  0x7918
#define AFE_GASRC3_NEW_CON7                  0x791c
#define AFE_GASRC3_NEW_CON8                  0x7920
#define AFE_GASRC3_NEW_CON9                  0x7924
#define AFE_GASRC3_NEW_CON10                 0x7928
#define AFE_GASRC3_NEW_CON11                 0x792c
#define AFE_GASRC3_NEW_CON12                 0x7930
#define AFE_GASRC3_NEW_CON13                 0x7934
#define AFE_GASRC3_NEW_CON14                 0x7938
#define AFE_GASRC3_NEW_IP_VERSION            0x793c
#define AFE_GASRC4_NEW_CON0                  0x7940
#define AFE_GASRC4_NEW_CON1                  0x7944
#define AFE_GASRC4_NEW_CON2                  0x7948
#define AFE_GASRC4_NEW_CON3                  0x794c
#define AFE_GASRC4_NEW_CON4                  0x7950
#define AFE_GASRC4_NEW_CON5                  0x7954
#define AFE_GASRC4_NEW_CON6                  0x7958
#define AFE_GASRC4_NEW_CON7                  0x795c
#define AFE_GASRC4_NEW_CON8                  0x7960
#define AFE_GASRC4_NEW_CON9                  0x7964
#define AFE_GASRC4_NEW_CON10                 0x7968
#define AFE_GASRC4_NEW_CON11                 0x796c
#define AFE_GASRC4_NEW_CON12                 0x7970
#define AFE_GASRC4_NEW_CON13                 0x7974
#define AFE_GASRC4_NEW_CON14                 0x7978
#define AFE_GASRC4_NEW_IP_VERSION            0x797c
#define AFE_GASRC5_NEW_CON0                  0x7980
#define AFE_GASRC5_NEW_CON1                  0x7984
#define AFE_GASRC5_NEW_CON2                  0x7988
#define AFE_GASRC5_NEW_CON3                  0x798c
#define AFE_GASRC5_NEW_CON4                  0x7990
#define AFE_GASRC5_NEW_CON5                  0x7994
#define AFE_GASRC5_NEW_CON6                  0x7998
#define AFE_GASRC5_NEW_CON7                  0x799c
#define AFE_GASRC5_NEW_CON8                  0x79a0
#define AFE_GASRC5_NEW_CON9                  0x79a4
#define AFE_GASRC5_NEW_CON10                 0x79a8
#define AFE_GASRC5_NEW_CON11                 0x79ac
#define AFE_GASRC5_NEW_CON12                 0x79b0
#define AFE_GASRC5_NEW_CON13                 0x79b4
#define AFE_GASRC5_NEW_CON14                 0x79b8
#define AFE_GASRC5_NEW_IP_VERSION            0x79bc
#define AFE_GASRC6_NEW_CON0                  0x79c0
#define AFE_GASRC6_NEW_CON1                  0x79c4
#define AFE_GASRC6_NEW_CON2                  0x79c8
#define AFE_GASRC6_NEW_CON3                  0x79cc
#define AFE_GASRC6_NEW_CON4                  0x79d0
#define AFE_GASRC6_NEW_CON5                  0x79d4
#define AFE_GASRC6_NEW_CON6                  0x79d8
#define AFE_GASRC6_NEW_CON7                  0x79dc
#define AFE_GASRC6_NEW_CON8                  0x79e0
#define AFE_GASRC6_NEW_CON9                  0x79e4
#define AFE_GASRC6_NEW_CON10                 0x79e8
#define AFE_GASRC6_NEW_CON11                 0x79ec
#define AFE_GASRC6_NEW_CON12                 0x79f0
#define AFE_GASRC6_NEW_CON13                 0x79f4
#define AFE_GASRC6_NEW_CON14                 0x79f8
#define AFE_GASRC6_NEW_IP_VERSION            0x79fc
#define AFE_GASRC7_NEW_CON0                  0x7a00
#define AFE_GASRC7_NEW_CON1                  0x7a04
#define AFE_GASRC7_NEW_CON2                  0x7a08
#define AFE_GASRC7_NEW_CON3                  0x7a0c
#define AFE_GASRC7_NEW_CON4                  0x7a10
#define AFE_GASRC7_NEW_CON5                  0x7a14
#define AFE_GASRC7_NEW_CON6                  0x7a18
#define AFE_GASRC7_NEW_CON7                  0x7a1c
#define AFE_GASRC7_NEW_CON8                  0x7a20
#define AFE_GASRC7_NEW_CON9                  0x7a24
#define AFE_GASRC7_NEW_CON10                 0x7a28
#define AFE_GASRC7_NEW_CON11                 0x7a2c
#define AFE_GASRC7_NEW_CON12                 0x7a30
#define AFE_GASRC7_NEW_CON13                 0x7a34
#define AFE_GASRC7_NEW_CON14                 0x7a38
#define AFE_GASRC7_NEW_IP_VERSION            0x7a3c
#define AFE_GASRC8_NEW_CON0                  0x7a40
#define AFE_GASRC8_NEW_CON1                  0x7a44
#define AFE_GASRC8_NEW_CON2                  0x7a48
#define AFE_GASRC8_NEW_CON3                  0x7a4c
#define AFE_GASRC8_NEW_CON4                  0x7a50
#define AFE_GASRC8_NEW_CON5                  0x7a54
#define AFE_GASRC8_NEW_CON6                  0x7a58
#define AFE_GASRC8_NEW_CON7                  0x7a5c
#define AFE_GASRC8_NEW_CON8                  0x7a60
#define AFE_GASRC8_NEW_CON9                  0x7a64
#define AFE_GASRC8_NEW_CON10                 0x7a68
#define AFE_GASRC8_NEW_CON11                 0x7a6c
#define AFE_GASRC8_NEW_CON12                 0x7a70
#define AFE_GASRC8_NEW_CON13                 0x7a74
#define AFE_GASRC8_NEW_CON14                 0x7a78
#define AFE_GASRC8_NEW_IP_VERSION            0x7a7c
#define AFE_GASRC9_NEW_CON0                  0x7a80
#define AFE_GASRC9_NEW_CON1                  0x7a84
#define AFE_GASRC9_NEW_CON2                  0x7a88
#define AFE_GASRC9_NEW_CON3                  0x7a8c
#define AFE_GASRC9_NEW_CON4                  0x7a90
#define AFE_GASRC9_NEW_CON5                  0x7a94
#define AFE_GASRC9_NEW_CON6                  0x7a98
#define AFE_GASRC9_NEW_CON7                  0x7a9c
#define AFE_GASRC9_NEW_CON8                  0x7aa0
#define AFE_GASRC9_NEW_CON9                  0x7aa4
#define AFE_GASRC9_NEW_CON10                 0x7aa8
#define AFE_GASRC9_NEW_CON11                 0x7aac
#define AFE_GASRC9_NEW_CON12                 0x7ab0
#define AFE_GASRC9_NEW_CON13                 0x7ab4
#define AFE_GASRC9_NEW_CON14                 0x7ab8
#define AFE_GASRC9_NEW_IP_VERSION            0x7abc
#define AFE_GASRC10_NEW_CON0                 0x7ac0
#define AFE_GASRC10_NEW_CON1                 0x7ac4
#define AFE_GASRC10_NEW_CON2                 0x7ac8
#define AFE_GASRC10_NEW_CON3                 0x7acc
#define AFE_GASRC10_NEW_CON4                 0x7ad0
#define AFE_GASRC10_NEW_CON5                 0x7ad4
#define AFE_GASRC10_NEW_CON6                 0x7ad8
#define AFE_GASRC10_NEW_CON7                 0x7adc
#define AFE_GASRC10_NEW_CON8                 0x7ae0
#define AFE_GASRC10_NEW_CON9                 0x7ae4
#define AFE_GASRC10_NEW_CON10                0x7ae8
#define AFE_GASRC10_NEW_CON11                0x7aec
#define AFE_GASRC10_NEW_CON12                0x7af0
#define AFE_GASRC10_NEW_CON13                0x7af4
#define AFE_GASRC10_NEW_CON14                0x7af8
#define AFE_GASRC10_NEW_IP_VERSION           0x7afc
#define AFE_GASRC11_NEW_CON0                 0x7b00
#define AFE_GASRC11_NEW_CON1                 0x7b04
#define AFE_GASRC11_NEW_CON2                 0x7b08
#define AFE_GASRC11_NEW_CON3                 0x7b0c
#define AFE_GASRC11_NEW_CON4                 0x7b10
#define AFE_GASRC11_NEW_CON5                 0x7b14
#define AFE_GASRC11_NEW_CON6                 0x7b18
#define AFE_GASRC11_NEW_CON7                 0x7b1c
#define AFE_GASRC11_NEW_CON8                 0x7b20
#define AFE_GASRC11_NEW_CON9                 0x7b24
#define AFE_GASRC11_NEW_CON10                0x7b28
#define AFE_GASRC11_NEW_CON11                0x7b2c
#define AFE_GASRC11_NEW_CON12                0x7b30
#define AFE_GASRC11_NEW_CON13                0x7b34
#define AFE_GASRC11_NEW_CON14                0x7b38
#define AFE_GASRC11_NEW_IP_VERSION           0x7b3c
#define AFE_GASRC12_NEW_CON0                 0x7b40
#define AFE_GASRC12_NEW_CON1                 0x7b44
#define AFE_GASRC12_NEW_CON2                 0x7b48
#define AFE_GASRC12_NEW_CON3                 0x7b4c
#define AFE_GASRC12_NEW_CON4                 0x7b50
#define AFE_GASRC12_NEW_CON5                 0x7b54
#define AFE_GASRC12_NEW_CON6                 0x7b58
#define AFE_GASRC12_NEW_CON7                 0x7b5c
#define AFE_GASRC12_NEW_CON8                 0x7b60
#define AFE_GASRC12_NEW_CON9                 0x7b64
#define AFE_GASRC12_NEW_CON10                0x7b68
#define AFE_GASRC12_NEW_CON11                0x7b6c
#define AFE_GASRC12_NEW_CON12                0x7b70
#define AFE_GASRC12_NEW_CON13                0x7b74
#define AFE_GASRC12_NEW_CON14                0x7b78
#define AFE_GASRC12_NEW_IP_VERSION           0x7b7c
#define AFE_GASRC13_NEW_CON0                 0x7b80
#define AFE_GASRC13_NEW_CON1                 0x7b84
#define AFE_GASRC13_NEW_CON2                 0x7b88
#define AFE_GASRC13_NEW_CON3                 0x7b8c
#define AFE_GASRC13_NEW_CON4                 0x7b90
#define AFE_GASRC13_NEW_CON5                 0x7b94
#define AFE_GASRC13_NEW_CON6                 0x7b98
#define AFE_GASRC13_NEW_CON7                 0x7b9c
#define AFE_GASRC13_NEW_CON8                 0x7ba0
#define AFE_GASRC13_NEW_CON9                 0x7ba4
#define AFE_GASRC13_NEW_CON10                0x7ba8
#define AFE_GASRC13_NEW_CON11                0x7bac
#define AFE_GASRC13_NEW_CON12                0x7bb0
#define AFE_GASRC13_NEW_CON13                0x7bb4
#define AFE_GASRC13_NEW_CON14                0x7bb8
#define AFE_GASRC13_NEW_IP_VERSION           0x7bbc
#define AFE_GASRC14_NEW_CON0                 0x7bc0
#define AFE_GASRC14_NEW_CON1                 0x7bc4
#define AFE_GASRC14_NEW_CON2                 0x7bc8
#define AFE_GASRC14_NEW_CON3                 0x7bcc
#define AFE_GASRC14_NEW_CON4                 0x7bd0
#define AFE_GASRC14_NEW_CON5                 0x7bd4
#define AFE_GASRC14_NEW_CON6                 0x7bd8
#define AFE_GASRC14_NEW_CON7                 0x7bdc
#define AFE_GASRC14_NEW_CON8                 0x7be0
#define AFE_GASRC14_NEW_CON9                 0x7be4
#define AFE_GASRC14_NEW_CON10                0x7be8
#define AFE_GASRC14_NEW_CON11                0x7bec
#define AFE_GASRC14_NEW_CON12                0x7bf0
#define AFE_GASRC14_NEW_CON13                0x7bf4
#define AFE_GASRC14_NEW_CON14                0x7bf8
#define AFE_GASRC14_NEW_IP_VERSION           0x7bfc
#define AFE_GASRC15_NEW_CON0                 0x7c00
#define AFE_GASRC15_NEW_CON1                 0x7c04
#define AFE_GASRC15_NEW_CON2                 0x7c08
#define AFE_GASRC15_NEW_CON3                 0x7c0c
#define AFE_GASRC15_NEW_CON4                 0x7c10
#define AFE_GASRC15_NEW_CON5                 0x7c14
#define AFE_GASRC15_NEW_CON6                 0x7c18
#define AFE_GASRC15_NEW_CON7                 0x7c1c
#define AFE_GASRC15_NEW_CON8                 0x7c20
#define AFE_GASRC15_NEW_CON9                 0x7c24
#define AFE_GASRC15_NEW_CON10                0x7c28
#define AFE_GASRC15_NEW_CON11                0x7c2c
#define AFE_GASRC15_NEW_CON12                0x7c30
#define AFE_GASRC15_NEW_CON13                0x7c34
#define AFE_GASRC15_NEW_CON14                0x7c38
#define AFE_GASRC15_NEW_IP_VERSION           0x7c3c

#define AFE_MAX_REGISTER AFE_GASRC15_NEW_IP_VERSION

#define AFE_IRQ_STATUS_BITS 0x87FFFFFF
#define AFE_IRQ_CNT_SHIFT 0
#define AFE_IRQ_CNT_MASK 0xffffff
#endif

