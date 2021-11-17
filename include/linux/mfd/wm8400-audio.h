/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * wm8400 private definitions for audio
 *
 * Copyright 2008 Wolfson Microelectronics plc
 */

#ifndef __LINUX_MFD_WM8400_AUDIO_H
#define __LINUX_MFD_WM8400_AUDIO_H

#include <linux/mfd/wm8400-audio.h>

/*
 * R2 (0x02) - Power Management (1)
 */
#define WM8400_CODEC_ENA                        0x8000  /* CODEC_ENA */
#define WM8400_CODEC_ENA_MASK                   0x8000  /* CODEC_ENA */
#define WM8400_CODEC_ENA_SHIFT                      15  /* CODEC_ENA */
#define WM8400_CODEC_ENA_WIDTH                       1  /* CODEC_ENA */
#define WM8400_SYSCLK_ENA                       0x4000  /* SYSCLK_ENA */
#define WM8400_SYSCLK_ENA_MASK                  0x4000  /* SYSCLK_ENA */
#define WM8400_SYSCLK_ENA_SHIFT                     14  /* SYSCLK_ENA */
#define WM8400_SYSCLK_ENA_WIDTH                      1  /* SYSCLK_ENA */
#define WM8400_SPK_MIX_ENA                      0x2000  /* SPK_MIX_ENA */
#define WM8400_SPK_MIX_ENA_MASK                 0x2000  /* SPK_MIX_ENA */
#define WM8400_SPK_MIX_ENA_SHIFT                    13  /* SPK_MIX_ENA */
#define WM8400_SPK_MIX_ENA_WIDTH                     1  /* SPK_MIX_ENA */
#define WM8400_SPK_ENA                          0x1000  /* SPK_ENA */
#define WM8400_SPK_ENA_MASK                     0x1000  /* SPK_ENA */
#define WM8400_SPK_ENA_SHIFT                        12  /* SPK_ENA */
#define WM8400_SPK_ENA_WIDTH                         1  /* SPK_ENA */
#define WM8400_OUT3_ENA                         0x0800  /* OUT3_ENA */
#define WM8400_OUT3_ENA_MASK                    0x0800  /* OUT3_ENA */
#define WM8400_OUT3_ENA_SHIFT                       11  /* OUT3_ENA */
#define WM8400_OUT3_ENA_WIDTH                        1  /* OUT3_ENA */
#define WM8400_OUT4_ENA                         0x0400  /* OUT4_ENA */
#define WM8400_OUT4_ENA_MASK                    0x0400  /* OUT4_ENA */
#define WM8400_OUT4_ENA_SHIFT                       10  /* OUT4_ENA */
#define WM8400_OUT4_ENA_WIDTH                        1  /* OUT4_ENA */
#define WM8400_LOUT_ENA                         0x0200  /* LOUT_ENA */
#define WM8400_LOUT_ENA_MASK                    0x0200  /* LOUT_ENA */
#define WM8400_LOUT_ENA_SHIFT                        9  /* LOUT_ENA */
#define WM8400_LOUT_ENA_WIDTH                        1  /* LOUT_ENA */
#define WM8400_ROUT_ENA                         0x0100  /* ROUT_ENA */
#define WM8400_ROUT_ENA_MASK                    0x0100  /* ROUT_ENA */
#define WM8400_ROUT_ENA_SHIFT                        8  /* ROUT_ENA */
#define WM8400_ROUT_ENA_WIDTH                        1  /* ROUT_ENA */
#define WM8400_MIC1BIAS_ENA                     0x0010  /* MIC1BIAS_ENA */
#define WM8400_MIC1BIAS_ENA_MASK                0x0010  /* MIC1BIAS_ENA */
#define WM8400_MIC1BIAS_ENA_SHIFT                    4  /* MIC1BIAS_ENA */
#define WM8400_MIC1BIAS_ENA_WIDTH                    1  /* MIC1BIAS_ENA */
#define WM8400_VMID_MODE_MASK                   0x0006  /* VMID_MODE - [2:1] */
#define WM8400_VMID_MODE_SHIFT                       1  /* VMID_MODE - [2:1] */
#define WM8400_VMID_MODE_WIDTH                       2  /* VMID_MODE - [2:1] */
#define WM8400_VREF_ENA                         0x0001  /* VREF_ENA */
#define WM8400_VREF_ENA_MASK                    0x0001  /* VREF_ENA */
#define WM8400_VREF_ENA_SHIFT                        0  /* VREF_ENA */
#define WM8400_VREF_ENA_WIDTH                        1  /* VREF_ENA */

/*
 * R3 (0x03) - Power Management (2)
 */
#define WM8400_FLL_ENA                          0x8000  /* FLL_ENA */
#define WM8400_FLL_ENA_MASK                     0x8000  /* FLL_ENA */
#define WM8400_FLL_ENA_SHIFT                        15  /* FLL_ENA */
#define WM8400_FLL_ENA_WIDTH                         1  /* FLL_ENA */
#define WM8400_TSHUT_ENA                        0x4000  /* TSHUT_ENA */
#define WM8400_TSHUT_ENA_MASK                   0x4000  /* TSHUT_ENA */
#define WM8400_TSHUT_ENA_SHIFT                      14  /* TSHUT_ENA */
#define WM8400_TSHUT_ENA_WIDTH                       1  /* TSHUT_ENA */
#define WM8400_TSHUT_OPDIS                      0x2000  /* TSHUT_OPDIS */
#define WM8400_TSHUT_OPDIS_MASK                 0x2000  /* TSHUT_OPDIS */
#define WM8400_TSHUT_OPDIS_SHIFT                    13  /* TSHUT_OPDIS */
#define WM8400_TSHUT_OPDIS_WIDTH                     1  /* TSHUT_OPDIS */
#define WM8400_OPCLK_ENA                        0x0800  /* OPCLK_ENA */
#define WM8400_OPCLK_ENA_MASK                   0x0800  /* OPCLK_ENA */
#define WM8400_OPCLK_ENA_SHIFT                      11  /* OPCLK_ENA */
#define WM8400_OPCLK_ENA_WIDTH                       1  /* OPCLK_ENA */
#define WM8400_AINL_ENA                         0x0200  /* AINL_ENA */
#define WM8400_AINL_ENA_MASK                    0x0200  /* AINL_ENA */
#define WM8400_AINL_ENA_SHIFT                        9  /* AINL_ENA */
#define WM8400_AINL_ENA_WIDTH                        1  /* AINL_ENA */
#define WM8400_AINR_ENA                         0x0100  /* AINR_ENA */
#define WM8400_AINR_ENA_MASK                    0x0100  /* AINR_ENA */
#define WM8400_AINR_ENA_SHIFT                        8  /* AINR_ENA */
#define WM8400_AINR_ENA_WIDTH                        1  /* AINR_ENA */
#define WM8400_LIN34_ENA                        0x0080  /* LIN34_ENA */
#define WM8400_LIN34_ENA_MASK                   0x0080  /* LIN34_ENA */
#define WM8400_LIN34_ENA_SHIFT                       7  /* LIN34_ENA */
#define WM8400_LIN34_ENA_WIDTH                       1  /* LIN34_ENA */
#define WM8400_LIN12_ENA                        0x0040  /* LIN12_ENA */
#define WM8400_LIN12_ENA_MASK                   0x0040  /* LIN12_ENA */
#define WM8400_LIN12_ENA_SHIFT                       6  /* LIN12_ENA */
#define WM8400_LIN12_ENA_WIDTH                       1  /* LIN12_ENA */
#define WM8400_RIN34_ENA                        0x0020  /* RIN34_ENA */
#define WM8400_RIN34_ENA_MASK                   0x0020  /* RIN34_ENA */
#define WM8400_RIN34_ENA_SHIFT                       5  /* RIN34_ENA */
#define WM8400_RIN34_ENA_WIDTH                       1  /* RIN34_ENA */
#define WM8400_RIN12_ENA                        0x0010  /* RIN12_ENA */
#define WM8400_RIN12_ENA_MASK                   0x0010  /* RIN12_ENA */
#define WM8400_RIN12_ENA_SHIFT                       4  /* RIN12_ENA */
#define WM8400_RIN12_ENA_WIDTH                       1  /* RIN12_ENA */
#define WM8400_ADCL_ENA                         0x0002  /* ADCL_ENA */
#define WM8400_ADCL_ENA_MASK                    0x0002  /* ADCL_ENA */
#define WM8400_ADCL_ENA_SHIFT                        1  /* ADCL_ENA */
#define WM8400_ADCL_ENA_WIDTH                        1  /* ADCL_ENA */
#define WM8400_ADCR_ENA                         0x0001  /* ADCR_ENA */
#define WM8400_ADCR_ENA_MASK                    0x0001  /* ADCR_ENA */
#define WM8400_ADCR_ENA_SHIFT                        0  /* ADCR_ENA */
#define WM8400_ADCR_ENA_WIDTH                        1  /* ADCR_ENA */

/*
 * R4 (0x04) - Power Management (3)
 */
#define WM8400_LON_ENA                          0x2000  /* LON_ENA */
#define WM8400_LON_ENA_MASK                     0x2000  /* LON_ENA */
#define WM8400_LON_ENA_SHIFT                        13  /* LON_ENA */
#define WM8400_LON_ENA_WIDTH                         1  /* LON_ENA */
#define WM8400_LOP_ENA                          0x1000  /* LOP_ENA */
#define WM8400_LOP_ENA_MASK                     0x1000  /* LOP_ENA */
#define WM8400_LOP_ENA_SHIFT                        12  /* LOP_ENA */
#define WM8400_LOP_ENA_WIDTH                         1  /* LOP_ENA */
#define WM8400_RON_ENA                          0x0800  /* RON_ENA */
#define WM8400_RON_ENA_MASK                     0x0800  /* RON_ENA */
#define WM8400_RON_ENA_SHIFT                        11  /* RON_ENA */
#define WM8400_RON_ENA_WIDTH                         1  /* RON_ENA */
#define WM8400_ROP_ENA                          0x0400  /* ROP_ENA */
#define WM8400_ROP_ENA_MASK                     0x0400  /* ROP_ENA */
#define WM8400_ROP_ENA_SHIFT                        10  /* ROP_ENA */
#define WM8400_ROP_ENA_WIDTH                         1  /* ROP_ENA */
#define WM8400_LOPGA_ENA                        0x0080  /* LOPGA_ENA */
#define WM8400_LOPGA_ENA_MASK                   0x0080  /* LOPGA_ENA */
#define WM8400_LOPGA_ENA_SHIFT                       7  /* LOPGA_ENA */
#define WM8400_LOPGA_ENA_WIDTH                       1  /* LOPGA_ENA */
#define WM8400_ROPGA_ENA                        0x0040  /* ROPGA_ENA */
#define WM8400_ROPGA_ENA_MASK                   0x0040  /* ROPGA_ENA */
#define WM8400_ROPGA_ENA_SHIFT                       6  /* ROPGA_ENA */
#define WM8400_ROPGA_ENA_WIDTH                       1  /* ROPGA_ENA */
#define WM8400_LOMIX_ENA                        0x0020  /* LOMIX_ENA */
#define WM8400_LOMIX_ENA_MASK                   0x0020  /* LOMIX_ENA */
#define WM8400_LOMIX_ENA_SHIFT                       5  /* LOMIX_ENA */
#define WM8400_LOMIX_ENA_WIDTH                       1  /* LOMIX_ENA */
#define WM8400_ROMIX_ENA                        0x0010  /* ROMIX_ENA */
#define WM8400_ROMIX_ENA_MASK                   0x0010  /* ROMIX_ENA */
#define WM8400_ROMIX_ENA_SHIFT                       4  /* ROMIX_ENA */
#define WM8400_ROMIX_ENA_WIDTH                       1  /* ROMIX_ENA */
#define WM8400_DACL_ENA                         0x0002  /* DACL_ENA */
#define WM8400_DACL_ENA_MASK                    0x0002  /* DACL_ENA */
#define WM8400_DACL_ENA_SHIFT                        1  /* DACL_ENA */
#define WM8400_DACL_ENA_WIDTH                        1  /* DACL_ENA */
#define WM8400_DACR_ENA                         0x0001  /* DACR_ENA */
#define WM8400_DACR_ENA_MASK                    0x0001  /* DACR_ENA */
#define WM8400_DACR_ENA_SHIFT                        0  /* DACR_ENA */
#define WM8400_DACR_ENA_WIDTH                        1  /* DACR_ENA */

/*
 * R5 (0x05) - Audio Interface (1)
 */
#define WM8400_AIFADCL_SRC                      0x8000  /* AIFADCL_SRC */
#define WM8400_AIFADCL_SRC_MASK                 0x8000  /* AIFADCL_SRC */
#define WM8400_AIFADCL_SRC_SHIFT                    15  /* AIFADCL_SRC */
#define WM8400_AIFADCL_SRC_WIDTH                     1  /* AIFADCL_SRC */
#define WM8400_AIFADCR_SRC                      0x4000  /* AIFADCR_SRC */
#define WM8400_AIFADCR_SRC_MASK                 0x4000  /* AIFADCR_SRC */
#define WM8400_AIFADCR_SRC_SHIFT                    14  /* AIFADCR_SRC */
#define WM8400_AIFADCR_SRC_WIDTH                     1  /* AIFADCR_SRC */
#define WM8400_AIFADC_TDM                       0x2000  /* AIFADC_TDM */
#define WM8400_AIFADC_TDM_MASK                  0x2000  /* AIFADC_TDM */
#define WM8400_AIFADC_TDM_SHIFT                     13  /* AIFADC_TDM */
#define WM8400_AIFADC_TDM_WIDTH                      1  /* AIFADC_TDM */
#define WM8400_AIFADC_TDM_CHAN                  0x1000  /* AIFADC_TDM_CHAN */
#define WM8400_AIFADC_TDM_CHAN_MASK             0x1000  /* AIFADC_TDM_CHAN */
#define WM8400_AIFADC_TDM_CHAN_SHIFT                12  /* AIFADC_TDM_CHAN */
#define WM8400_AIFADC_TDM_CHAN_WIDTH                 1  /* AIFADC_TDM_CHAN */
#define WM8400_AIF_BCLK_INV                     0x0100  /* AIF_BCLK_INV */
#define WM8400_AIF_BCLK_INV_MASK                0x0100  /* AIF_BCLK_INV */
#define WM8400_AIF_BCLK_INV_SHIFT                    8  /* AIF_BCLK_INV */
#define WM8400_AIF_BCLK_INV_WIDTH                    1  /* AIF_BCLK_INV */
#define WM8400_AIF_LRCLK_INV                    0x0080  /* AIF_LRCLK_INV */
#define WM8400_AIF_LRCLK_INV_MASK               0x0080  /* AIF_LRCLK_INV */
#define WM8400_AIF_LRCLK_INV_SHIFT                   7  /* AIF_LRCLK_INV */
#define WM8400_AIF_LRCLK_INV_WIDTH                   1  /* AIF_LRCLK_INV */
#define WM8400_AIF_WL_MASK                      0x0060  /* AIF_WL - [6:5] */
#define WM8400_AIF_WL_SHIFT                          5  /* AIF_WL - [6:5] */
#define WM8400_AIF_WL_WIDTH                          2  /* AIF_WL - [6:5] */
#define WM8400_AIF_WL_16BITS			(0 << 5)
#define WM8400_AIF_WL_20BITS			(1 << 5)
#define WM8400_AIF_WL_24BITS			(2 << 5)
#define WM8400_AIF_WL_32BITS			(3 << 5)
#define WM8400_AIF_FMT_MASK                     0x0018  /* AIF_FMT - [4:3] */
#define WM8400_AIF_FMT_SHIFT                         3  /* AIF_FMT - [4:3] */
#define WM8400_AIF_FMT_WIDTH                         2  /* AIF_FMT - [4:3] */
#define WM8400_AIF_FMT_RIGHTJ			(0 << 3)
#define WM8400_AIF_FMT_LEFTJ			(1 << 3)
#define WM8400_AIF_FMT_I2S			(2 << 3)
#define WM8400_AIF_FMT_DSP			(3 << 3)

/*
 * R6 (0x06) - Audio Interface (2)
 */
#define WM8400_DACL_SRC                         0x8000  /* DACL_SRC */
#define WM8400_DACL_SRC_MASK                    0x8000  /* DACL_SRC */
#define WM8400_DACL_SRC_SHIFT                       15  /* DACL_SRC */
#define WM8400_DACL_SRC_WIDTH                        1  /* DACL_SRC */
#define WM8400_DACR_SRC                         0x4000  /* DACR_SRC */
#define WM8400_DACR_SRC_MASK                    0x4000  /* DACR_SRC */
#define WM8400_DACR_SRC_SHIFT                       14  /* DACR_SRC */
#define WM8400_DACR_SRC_WIDTH                        1  /* DACR_SRC */
#define WM8400_AIFDAC_TDM                       0x2000  /* AIFDAC_TDM */
#define WM8400_AIFDAC_TDM_MASK                  0x2000  /* AIFDAC_TDM */
#define WM8400_AIFDAC_TDM_SHIFT                     13  /* AIFDAC_TDM */
#define WM8400_AIFDAC_TDM_WIDTH                      1  /* AIFDAC_TDM */
#define WM8400_AIFDAC_TDM_CHAN                  0x1000  /* AIFDAC_TDM_CHAN */
#define WM8400_AIFDAC_TDM_CHAN_MASK             0x1000  /* AIFDAC_TDM_CHAN */
#define WM8400_AIFDAC_TDM_CHAN_SHIFT                12  /* AIFDAC_TDM_CHAN */
#define WM8400_AIFDAC_TDM_CHAN_WIDTH                 1  /* AIFDAC_TDM_CHAN */
#define WM8400_DAC_BOOST_MASK                   0x0C00  /* DAC_BOOST - [11:10] */
#define WM8400_DAC_BOOST_SHIFT                      10  /* DAC_BOOST - [11:10] */
#define WM8400_DAC_BOOST_WIDTH                       2  /* DAC_BOOST - [11:10] */
#define WM8400_DAC_COMP                         0x0010  /* DAC_COMP */
#define WM8400_DAC_COMP_MASK                    0x0010  /* DAC_COMP */
#define WM8400_DAC_COMP_SHIFT                        4  /* DAC_COMP */
#define WM8400_DAC_COMP_WIDTH                        1  /* DAC_COMP */
#define WM8400_DAC_COMPMODE                     0x0008  /* DAC_COMPMODE */
#define WM8400_DAC_COMPMODE_MASK                0x0008  /* DAC_COMPMODE */
#define WM8400_DAC_COMPMODE_SHIFT                    3  /* DAC_COMPMODE */
#define WM8400_DAC_COMPMODE_WIDTH                    1  /* DAC_COMPMODE */
#define WM8400_ADC_COMP                         0x0004  /* ADC_COMP */
#define WM8400_ADC_COMP_MASK                    0x0004  /* ADC_COMP */
#define WM8400_ADC_COMP_SHIFT                        2  /* ADC_COMP */
#define WM8400_ADC_COMP_WIDTH                        1  /* ADC_COMP */
#define WM8400_ADC_COMPMODE                     0x0002  /* ADC_COMPMODE */
#define WM8400_ADC_COMPMODE_MASK                0x0002  /* ADC_COMPMODE */
#define WM8400_ADC_COMPMODE_SHIFT                    1  /* ADC_COMPMODE */
#define WM8400_ADC_COMPMODE_WIDTH                    1  /* ADC_COMPMODE */
#define WM8400_LOOPBACK                         0x0001  /* LOOPBACK */
#define WM8400_LOOPBACK_MASK                    0x0001  /* LOOPBACK */
#define WM8400_LOOPBACK_SHIFT                        0  /* LOOPBACK */
#define WM8400_LOOPBACK_WIDTH                        1  /* LOOPBACK */

/*
 * R7 (0x07) - Clocking (1)
 */
#define WM8400_TOCLK_RATE                       0x8000  /* TOCLK_RATE */
#define WM8400_TOCLK_RATE_MASK                  0x8000  /* TOCLK_RATE */
#define WM8400_TOCLK_RATE_SHIFT                     15  /* TOCLK_RATE */
#define WM8400_TOCLK_RATE_WIDTH                      1  /* TOCLK_RATE */
#define WM8400_TOCLK_ENA                        0x4000  /* TOCLK_ENA */
#define WM8400_TOCLK_ENA_MASK                   0x4000  /* TOCLK_ENA */
#define WM8400_TOCLK_ENA_SHIFT                      14  /* TOCLK_ENA */
#define WM8400_TOCLK_ENA_WIDTH                       1  /* TOCLK_ENA */
#define WM8400_OPCLKDIV_MASK                    0x1E00  /* OPCLKDIV - [12:9] */
#define WM8400_OPCLKDIV_SHIFT                        9  /* OPCLKDIV - [12:9] */
#define WM8400_OPCLKDIV_WIDTH                        4  /* OPCLKDIV - [12:9] */
#define WM8400_DCLKDIV_MASK                     0x01C0  /* DCLKDIV - [8:6] */
#define WM8400_DCLKDIV_SHIFT                         6  /* DCLKDIV - [8:6] */
#define WM8400_DCLKDIV_WIDTH                         3  /* DCLKDIV - [8:6] */
#define WM8400_BCLK_DIV_MASK                    0x001E  /* BCLK_DIV - [4:1] */
#define WM8400_BCLK_DIV_SHIFT                        1  /* BCLK_DIV - [4:1] */
#define WM8400_BCLK_DIV_WIDTH                        4  /* BCLK_DIV - [4:1] */

/*
 * R8 (0x08) - Clocking (2)
 */
#define WM8400_MCLK_SRC                         0x8000  /* MCLK_SRC */
#define WM8400_MCLK_SRC_MASK                    0x8000  /* MCLK_SRC */
#define WM8400_MCLK_SRC_SHIFT                       15  /* MCLK_SRC */
#define WM8400_MCLK_SRC_WIDTH                        1  /* MCLK_SRC */
#define WM8400_SYSCLK_SRC                       0x4000  /* SYSCLK_SRC */
#define WM8400_SYSCLK_SRC_MASK                  0x4000  /* SYSCLK_SRC */
#define WM8400_SYSCLK_SRC_SHIFT                     14  /* SYSCLK_SRC */
#define WM8400_SYSCLK_SRC_WIDTH                      1  /* SYSCLK_SRC */
#define WM8400_CLK_FORCE                        0x2000  /* CLK_FORCE */
#define WM8400_CLK_FORCE_MASK                   0x2000  /* CLK_FORCE */
#define WM8400_CLK_FORCE_SHIFT                      13  /* CLK_FORCE */
#define WM8400_CLK_FORCE_WIDTH                       1  /* CLK_FORCE */
#define WM8400_MCLK_DIV_MASK                    0x1800  /* MCLK_DIV - [12:11] */
#define WM8400_MCLK_DIV_SHIFT                       11  /* MCLK_DIV - [12:11] */
#define WM8400_MCLK_DIV_WIDTH                        2  /* MCLK_DIV - [12:11] */
#define WM8400_MCLK_INV                         0x0400  /* MCLK_INV */
#define WM8400_MCLK_INV_MASK                    0x0400  /* MCLK_INV */
#define WM8400_MCLK_INV_SHIFT                       10  /* MCLK_INV */
#define WM8400_MCLK_INV_WIDTH                        1  /* MCLK_INV */
#define WM8400_ADC_CLKDIV_MASK                  0x00E0  /* ADC_CLKDIV - [7:5] */
#define WM8400_ADC_CLKDIV_SHIFT                      5  /* ADC_CLKDIV - [7:5] */
#define WM8400_ADC_CLKDIV_WIDTH                      3  /* ADC_CLKDIV - [7:5] */
#define WM8400_DAC_CLKDIV_MASK                  0x001C  /* DAC_CLKDIV - [4:2] */
#define WM8400_DAC_CLKDIV_SHIFT                      2  /* DAC_CLKDIV - [4:2] */
#define WM8400_DAC_CLKDIV_WIDTH                      3  /* DAC_CLKDIV - [4:2] */

/*
 * R9 (0x09) - Audio Interface (3)
 */
#define WM8400_AIF_MSTR1                        0x8000  /* AIF_MSTR1 */
#define WM8400_AIF_MSTR1_MASK                   0x8000  /* AIF_MSTR1 */
#define WM8400_AIF_MSTR1_SHIFT                      15  /* AIF_MSTR1 */
#define WM8400_AIF_MSTR1_WIDTH                       1  /* AIF_MSTR1 */
#define WM8400_AIF_MSTR2                        0x4000  /* AIF_MSTR2 */
#define WM8400_AIF_MSTR2_MASK                   0x4000  /* AIF_MSTR2 */
#define WM8400_AIF_MSTR2_SHIFT                      14  /* AIF_MSTR2 */
#define WM8400_AIF_MSTR2_WIDTH                       1  /* AIF_MSTR2 */
#define WM8400_AIF_SEL                          0x2000  /* AIF_SEL */
#define WM8400_AIF_SEL_MASK                     0x2000  /* AIF_SEL */
#define WM8400_AIF_SEL_SHIFT                        13  /* AIF_SEL */
#define WM8400_AIF_SEL_WIDTH                         1  /* AIF_SEL */
#define WM8400_ADCLRC_DIR                       0x0800  /* ADCLRC_DIR */
#define WM8400_ADCLRC_DIR_MASK                  0x0800  /* ADCLRC_DIR */
#define WM8400_ADCLRC_DIR_SHIFT                     11  /* ADCLRC_DIR */
#define WM8400_ADCLRC_DIR_WIDTH                      1  /* ADCLRC_DIR */
#define WM8400_ADCLRC_RATE_MASK                 0x07FF  /* ADCLRC_RATE - [10:0] */
#define WM8400_ADCLRC_RATE_SHIFT                     0  /* ADCLRC_RATE - [10:0] */
#define WM8400_ADCLRC_RATE_WIDTH                    11  /* ADCLRC_RATE - [10:0] */

/*
 * R10 (0x0A) - Audio Interface (4)
 */
#define WM8400_ALRCGPIO1                        0x8000  /* ALRCGPIO1 */
#define WM8400_ALRCGPIO1_MASK                   0x8000  /* ALRCGPIO1 */
#define WM8400_ALRCGPIO1_SHIFT                      15  /* ALRCGPIO1 */
#define WM8400_ALRCGPIO1_WIDTH                       1  /* ALRCGPIO1 */
#define WM8400_ALRCBGPIO6                       0x4000  /* ALRCBGPIO6 */
#define WM8400_ALRCBGPIO6_MASK                  0x4000  /* ALRCBGPIO6 */
#define WM8400_ALRCBGPIO6_SHIFT                     14  /* ALRCBGPIO6 */
#define WM8400_ALRCBGPIO6_WIDTH                      1  /* ALRCBGPIO6 */
#define WM8400_AIF_TRIS                         0x2000  /* AIF_TRIS */
#define WM8400_AIF_TRIS_MASK                    0x2000  /* AIF_TRIS */
#define WM8400_AIF_TRIS_SHIFT                       13  /* AIF_TRIS */
#define WM8400_AIF_TRIS_WIDTH                        1  /* AIF_TRIS */
#define WM8400_DACLRC_DIR                       0x0800  /* DACLRC_DIR */
#define WM8400_DACLRC_DIR_MASK                  0x0800  /* DACLRC_DIR */
#define WM8400_DACLRC_DIR_SHIFT                     11  /* DACLRC_DIR */
#define WM8400_DACLRC_DIR_WIDTH                      1  /* DACLRC_DIR */
#define WM8400_DACLRC_RATE_MASK                 0x07FF  /* DACLRC_RATE - [10:0] */
#define WM8400_DACLRC_RATE_SHIFT                     0  /* DACLRC_RATE - [10:0] */
#define WM8400_DACLRC_RATE_WIDTH                    11  /* DACLRC_RATE - [10:0] */

/*
 * R11 (0x0B) - DAC CTRL
 */
#define WM8400_DAC_SDMCLK_RATE                  0x2000  /* DAC_SDMCLK_RATE */
#define WM8400_DAC_SDMCLK_RATE_MASK             0x2000  /* DAC_SDMCLK_RATE */
#define WM8400_DAC_SDMCLK_RATE_SHIFT                13  /* DAC_SDMCLK_RATE */
#define WM8400_DAC_SDMCLK_RATE_WIDTH                 1  /* DAC_SDMCLK_RATE */
#define WM8400_AIF_LRCLKRATE                    0x0400  /* AIF_LRCLKRATE */
#define WM8400_AIF_LRCLKRATE_MASK               0x0400  /* AIF_LRCLKRATE */
#define WM8400_AIF_LRCLKRATE_SHIFT                  10  /* AIF_LRCLKRATE */
#define WM8400_AIF_LRCLKRATE_WIDTH                   1  /* AIF_LRCLKRATE */
#define WM8400_DAC_MONO                         0x0200  /* DAC_MONO */
#define WM8400_DAC_MONO_MASK                    0x0200  /* DAC_MONO */
#define WM8400_DAC_MONO_SHIFT                        9  /* DAC_MONO */
#define WM8400_DAC_MONO_WIDTH                        1  /* DAC_MONO */
#define WM8400_DAC_SB_FILT                      0x0100  /* DAC_SB_FILT */
#define WM8400_DAC_SB_FILT_MASK                 0x0100  /* DAC_SB_FILT */
#define WM8400_DAC_SB_FILT_SHIFT                     8  /* DAC_SB_FILT */
#define WM8400_DAC_SB_FILT_WIDTH                     1  /* DAC_SB_FILT */
#define WM8400_DAC_MUTERATE                     0x0080  /* DAC_MUTERATE */
#define WM8400_DAC_MUTERATE_MASK                0x0080  /* DAC_MUTERATE */
#define WM8400_DAC_MUTERATE_SHIFT                    7  /* DAC_MUTERATE */
#define WM8400_DAC_MUTERATE_WIDTH                    1  /* DAC_MUTERATE */
#define WM8400_DAC_MUTEMODE                     0x0040  /* DAC_MUTEMODE */
#define WM8400_DAC_MUTEMODE_MASK                0x0040  /* DAC_MUTEMODE */
#define WM8400_DAC_MUTEMODE_SHIFT                    6  /* DAC_MUTEMODE */
#define WM8400_DAC_MUTEMODE_WIDTH                    1  /* DAC_MUTEMODE */
#define WM8400_DEEMP_MASK                       0x0030  /* DEEMP - [5:4] */
#define WM8400_DEEMP_SHIFT                           4  /* DEEMP - [5:4] */
#define WM8400_DEEMP_WIDTH                           2  /* DEEMP - [5:4] */
#define WM8400_DAC_MUTE                         0x0004  /* DAC_MUTE */
#define WM8400_DAC_MUTE_MASK                    0x0004  /* DAC_MUTE */
#define WM8400_DAC_MUTE_SHIFT                        2  /* DAC_MUTE */
#define WM8400_DAC_MUTE_WIDTH                        1  /* DAC_MUTE */
#define WM8400_DACL_DATINV                      0x0002  /* DACL_DATINV */
#define WM8400_DACL_DATINV_MASK                 0x0002  /* DACL_DATINV */
#define WM8400_DACL_DATINV_SHIFT                     1  /* DACL_DATINV */
#define WM8400_DACL_DATINV_WIDTH                     1  /* DACL_DATINV */
#define WM8400_DACR_DATINV                      0x0001  /* DACR_DATINV */
#define WM8400_DACR_DATINV_MASK                 0x0001  /* DACR_DATINV */
#define WM8400_DACR_DATINV_SHIFT                     0  /* DACR_DATINV */
#define WM8400_DACR_DATINV_WIDTH                     1  /* DACR_DATINV */

/*
 * R12 (0x0C) - Left DAC Digital Volume
 */
#define WM8400_DAC_VU                           0x0100  /* DAC_VU */
#define WM8400_DAC_VU_MASK                      0x0100  /* DAC_VU */
#define WM8400_DAC_VU_SHIFT                          8  /* DAC_VU */
#define WM8400_DAC_VU_WIDTH                          1  /* DAC_VU */
#define WM8400_DACL_VOL_MASK                    0x00FF  /* DACL_VOL - [7:0] */
#define WM8400_DACL_VOL_SHIFT                        0  /* DACL_VOL - [7:0] */
#define WM8400_DACL_VOL_WIDTH                        8  /* DACL_VOL - [7:0] */

/*
 * R13 (0x0D) - Right DAC Digital Volume
 */
#define WM8400_DAC_VU                           0x0100  /* DAC_VU */
#define WM8400_DAC_VU_MASK                      0x0100  /* DAC_VU */
#define WM8400_DAC_VU_SHIFT                          8  /* DAC_VU */
#define WM8400_DAC_VU_WIDTH                          1  /* DAC_VU */
#define WM8400_DACR_VOL_MASK                    0x00FF  /* DACR_VOL - [7:0] */
#define WM8400_DACR_VOL_SHIFT                        0  /* DACR_VOL - [7:0] */
#define WM8400_DACR_VOL_WIDTH                        8  /* DACR_VOL - [7:0] */

/*
 * R14 (0x0E) - Digital Side Tone
 */
#define WM8400_ADCL_DAC_SVOL_MASK               0x1E00  /*   ADCL_DAC_SVOL - [12:9] */
#define WM8400_ADCL_DAC_SVOL_SHIFT                   9  /*   ADCL_DAC_SVOL - [12:9] */
#define WM8400_ADCL_DAC_SVOL_WIDTH                   4  /*   ADCL_DAC_SVOL - [12:9] */
#define WM8400_ADCR_DAC_SVOL_MASK               0x01E0  /* ADCR_DAC_SVOL - [8:5] */
#define WM8400_ADCR_DAC_SVOL_SHIFT                   5  /* ADCR_DAC_SVOL - [8:5] */
#define WM8400_ADCR_DAC_SVOL_WIDTH                   4  /* ADCR_DAC_SVOL - [8:5] */
#define WM8400_ADC_TO_DACL_MASK                 0x000C  /* ADC_TO_DACL - [3:2] */
#define WM8400_ADC_TO_DACL_SHIFT                     2  /* ADC_TO_DACL - [3:2] */
#define WM8400_ADC_TO_DACL_WIDTH                     2  /* ADC_TO_DACL - [3:2] */
#define WM8400_ADC_TO_DACR_MASK                 0x0003  /* ADC_TO_DACR - [1:0] */
#define WM8400_ADC_TO_DACR_SHIFT                     0  /* ADC_TO_DACR - [1:0] */
#define WM8400_ADC_TO_DACR_WIDTH                     2  /* ADC_TO_DACR - [1:0] */

/*
 * R15 (0x0F) - ADC CTRL
 */
#define WM8400_ADC_HPF_ENA                      0x0100  /* ADC_HPF_ENA */
#define WM8400_ADC_HPF_ENA_MASK                 0x0100  /* ADC_HPF_ENA */
#define WM8400_ADC_HPF_ENA_SHIFT                     8  /* ADC_HPF_ENA */
#define WM8400_ADC_HPF_ENA_WIDTH                     1  /* ADC_HPF_ENA */
#define WM8400_ADC_HPF_CUT_MASK                 0x0060  /* ADC_HPF_CUT - [6:5] */
#define WM8400_ADC_HPF_CUT_SHIFT                     5  /* ADC_HPF_CUT - [6:5] */
#define WM8400_ADC_HPF_CUT_WIDTH                     2  /* ADC_HPF_CUT - [6:5] */
#define WM8400_ADCL_DATINV                      0x0002  /* ADCL_DATINV */
#define WM8400_ADCL_DATINV_MASK                 0x0002  /* ADCL_DATINV */
#define WM8400_ADCL_DATINV_SHIFT                     1  /* ADCL_DATINV */
#define WM8400_ADCL_DATINV_WIDTH                     1  /* ADCL_DATINV */
#define WM8400_ADCR_DATINV                      0x0001  /* ADCR_DATINV */
#define WM8400_ADCR_DATINV_MASK                 0x0001  /* ADCR_DATINV */
#define WM8400_ADCR_DATINV_SHIFT                     0  /* ADCR_DATINV */
#define WM8400_ADCR_DATINV_WIDTH                     1  /* ADCR_DATINV */

/*
 * R16 (0x10) - Left ADC Digital Volume
 */
#define WM8400_ADC_VU                           0x0100  /* ADC_VU */
#define WM8400_ADC_VU_MASK                      0x0100  /* ADC_VU */
#define WM8400_ADC_VU_SHIFT                          8  /* ADC_VU */
#define WM8400_ADC_VU_WIDTH                          1  /* ADC_VU */
#define WM8400_ADCL_VOL_MASK                    0x00FF  /* ADCL_VOL - [7:0] */
#define WM8400_ADCL_VOL_SHIFT                        0  /* ADCL_VOL - [7:0] */
#define WM8400_ADCL_VOL_WIDTH                        8  /* ADCL_VOL - [7:0] */

/*
 * R17 (0x11) - Right ADC Digital Volume
 */
#define WM8400_ADC_VU                           0x0100  /* ADC_VU */
#define WM8400_ADC_VU_MASK                      0x0100  /* ADC_VU */
#define WM8400_ADC_VU_SHIFT                          8  /* ADC_VU */
#define WM8400_ADC_VU_WIDTH                          1  /* ADC_VU */
#define WM8400_ADCR_VOL_MASK                    0x00FF  /* ADCR_VOL - [7:0] */
#define WM8400_ADCR_VOL_SHIFT                        0  /* ADCR_VOL - [7:0] */
#define WM8400_ADCR_VOL_WIDTH                        8  /* ADCR_VOL - [7:0] */

/*
 * R24 (0x18) - Left Line Input 1&2 Volume
 */
#define WM8400_IPVU                             0x0100  /* IPVU */
#define WM8400_IPVU_MASK                        0x0100  /* IPVU */
#define WM8400_IPVU_SHIFT                            8  /* IPVU */
#define WM8400_IPVU_WIDTH                            1  /* IPVU */
#define WM8400_LI12MUTE                         0x0080  /* LI12MUTE */
#define WM8400_LI12MUTE_MASK                    0x0080  /* LI12MUTE */
#define WM8400_LI12MUTE_SHIFT                        7  /* LI12MUTE */
#define WM8400_LI12MUTE_WIDTH                        1  /* LI12MUTE */
#define WM8400_LI12ZC                           0x0040  /* LI12ZC */
#define WM8400_LI12ZC_MASK                      0x0040  /* LI12ZC */
#define WM8400_LI12ZC_SHIFT                          6  /* LI12ZC */
#define WM8400_LI12ZC_WIDTH                          1  /* LI12ZC */
#define WM8400_LIN12VOL_MASK                    0x001F  /* LIN12VOL - [4:0] */
#define WM8400_LIN12VOL_SHIFT                        0  /* LIN12VOL - [4:0] */
#define WM8400_LIN12VOL_WIDTH                        5  /* LIN12VOL - [4:0] */

/*
 * R25 (0x19) - Left Line Input 3&4 Volume
 */
#define WM8400_IPVU                             0x0100  /* IPVU */
#define WM8400_IPVU_MASK                        0x0100  /* IPVU */
#define WM8400_IPVU_SHIFT                            8  /* IPVU */
#define WM8400_IPVU_WIDTH                            1  /* IPVU */
#define WM8400_LI34MUTE                         0x0080  /* LI34MUTE */
#define WM8400_LI34MUTE_MASK                    0x0080  /* LI34MUTE */
#define WM8400_LI34MUTE_SHIFT                        7  /* LI34MUTE */
#define WM8400_LI34MUTE_WIDTH                        1  /* LI34MUTE */
#define WM8400_LI34ZC                           0x0040  /* LI34ZC */
#define WM8400_LI34ZC_MASK                      0x0040  /* LI34ZC */
#define WM8400_LI34ZC_SHIFT                          6  /* LI34ZC */
#define WM8400_LI34ZC_WIDTH                          1  /* LI34ZC */
#define WM8400_LIN34VOL_MASK                    0x001F  /* LIN34VOL - [4:0] */
#define WM8400_LIN34VOL_SHIFT                        0  /* LIN34VOL - [4:0] */
#define WM8400_LIN34VOL_WIDTH                        5  /* LIN34VOL - [4:0] */

/*
 * R26 (0x1A) - Right Line Input 1&2 Volume
 */
#define WM8400_IPVU                             0x0100  /* IPVU */
#define WM8400_IPVU_MASK                        0x0100  /* IPVU */
#define WM8400_IPVU_SHIFT                            8  /* IPVU */
#define WM8400_IPVU_WIDTH                            1  /* IPVU */
#define WM8400_RI12MUTE                         0x0080  /* RI12MUTE */
#define WM8400_RI12MUTE_MASK                    0x0080  /* RI12MUTE */
#define WM8400_RI12MUTE_SHIFT                        7  /* RI12MUTE */
#define WM8400_RI12MUTE_WIDTH                        1  /* RI12MUTE */
#define WM8400_RI12ZC                           0x0040  /* RI12ZC */
#define WM8400_RI12ZC_MASK                      0x0040  /* RI12ZC */
#define WM8400_RI12ZC_SHIFT                          6  /* RI12ZC */
#define WM8400_RI12ZC_WIDTH                          1  /* RI12ZC */
#define WM8400_RIN12VOL_MASK                    0x001F  /* RIN12VOL - [4:0] */
#define WM8400_RIN12VOL_SHIFT                        0  /* RIN12VOL - [4:0] */
#define WM8400_RIN12VOL_WIDTH                        5  /* RIN12VOL - [4:0] */

/*
 * R27 (0x1B) - Right Line Input 3&4 Volume
 */
#define WM8400_IPVU                             0x0100  /* IPVU */
#define WM8400_IPVU_MASK                        0x0100  /* IPVU */
#define WM8400_IPVU_SHIFT                            8  /* IPVU */
#define WM8400_IPVU_WIDTH                            1  /* IPVU */
#define WM8400_RI34MUTE                         0x0080  /* RI34MUTE */
#define WM8400_RI34MUTE_MASK                    0x0080  /* RI34MUTE */
#define WM8400_RI34MUTE_SHIFT                        7  /* RI34MUTE */
#define WM8400_RI34MUTE_WIDTH                        1  /* RI34MUTE */
#define WM8400_RI34ZC                           0x0040  /* RI34ZC */
#define WM8400_RI34ZC_MASK                      0x0040  /* RI34ZC */
#define WM8400_RI34ZC_SHIFT                          6  /* RI34ZC */
#define WM8400_RI34ZC_WIDTH                          1  /* RI34ZC */
#define WM8400_RIN34VOL_MASK                    0x001F  /* RIN34VOL - [4:0] */
#define WM8400_RIN34VOL_SHIFT                        0  /* RIN34VOL - [4:0] */
#define WM8400_RIN34VOL_WIDTH                        5  /* RIN34VOL - [4:0] */

/*
 * R28 (0x1C) - Left Output Volume
 */
#define WM8400_OPVU                             0x0100  /* OPVU */
#define WM8400_OPVU_MASK                        0x0100  /* OPVU */
#define WM8400_OPVU_SHIFT                            8  /* OPVU */
#define WM8400_OPVU_WIDTH                            1  /* OPVU */
#define WM8400_LOZC                             0x0080  /* LOZC */
#define WM8400_LOZC_MASK                        0x0080  /* LOZC */
#define WM8400_LOZC_SHIFT                            7  /* LOZC */
#define WM8400_LOZC_WIDTH                            1  /* LOZC */
#define WM8400_LOUTVOL_MASK                     0x007F  /* LOUTVOL - [6:0] */
#define WM8400_LOUTVOL_SHIFT                         0  /* LOUTVOL - [6:0] */
#define WM8400_LOUTVOL_WIDTH                         7  /* LOUTVOL - [6:0] */

/*
 * R29 (0x1D) - Right Output Volume
 */
#define WM8400_OPVU                             0x0100  /* OPVU */
#define WM8400_OPVU_MASK                        0x0100  /* OPVU */
#define WM8400_OPVU_SHIFT                            8  /* OPVU */
#define WM8400_OPVU_WIDTH                            1  /* OPVU */
#define WM8400_ROZC                             0x0080  /* ROZC */
#define WM8400_ROZC_MASK                        0x0080  /* ROZC */
#define WM8400_ROZC_SHIFT                            7  /* ROZC */
#define WM8400_ROZC_WIDTH                            1  /* ROZC */
#define WM8400_ROUTVOL_MASK                     0x007F  /* ROUTVOL - [6:0] */
#define WM8400_ROUTVOL_SHIFT                         0  /* ROUTVOL - [6:0] */
#define WM8400_ROUTVOL_WIDTH                         7  /* ROUTVOL - [6:0] */

/*
 * R30 (0x1E) - Line Outputs Volume
 */
#define WM8400_LONMUTE                          0x0040  /* LONMUTE */
#define WM8400_LONMUTE_MASK                     0x0040  /* LONMUTE */
#define WM8400_LONMUTE_SHIFT                         6  /* LONMUTE */
#define WM8400_LONMUTE_WIDTH                         1  /* LONMUTE */
#define WM8400_LOPMUTE                          0x0020  /* LOPMUTE */
#define WM8400_LOPMUTE_MASK                     0x0020  /* LOPMUTE */
#define WM8400_LOPMUTE_SHIFT                         5  /* LOPMUTE */
#define WM8400_LOPMUTE_WIDTH                         1  /* LOPMUTE */
#define WM8400_LOATTN                           0x0010  /* LOATTN */
#define WM8400_LOATTN_MASK                      0x0010  /* LOATTN */
#define WM8400_LOATTN_SHIFT                          4  /* LOATTN */
#define WM8400_LOATTN_WIDTH                          1  /* LOATTN */
#define WM8400_RONMUTE                          0x0004  /* RONMUTE */
#define WM8400_RONMUTE_MASK                     0x0004  /* RONMUTE */
#define WM8400_RONMUTE_SHIFT                         2  /* RONMUTE */
#define WM8400_RONMUTE_WIDTH                         1  /* RONMUTE */
#define WM8400_ROPMUTE                          0x0002  /* ROPMUTE */
#define WM8400_ROPMUTE_MASK                     0x0002  /* ROPMUTE */
#define WM8400_ROPMUTE_SHIFT                         1  /* ROPMUTE */
#define WM8400_ROPMUTE_WIDTH                         1  /* ROPMUTE */
#define WM8400_ROATTN                           0x0001  /* ROATTN */
#define WM8400_ROATTN_MASK                      0x0001  /* ROATTN */
#define WM8400_ROATTN_SHIFT                          0  /* ROATTN */
#define WM8400_ROATTN_WIDTH                          1  /* ROATTN */

/*
 * R31 (0x1F) - Out3/4 Volume
 */
#define WM8400_OUT3MUTE                         0x0020  /* OUT3MUTE */
#define WM8400_OUT3MUTE_MASK                    0x0020  /* OUT3MUTE */
#define WM8400_OUT3MUTE_SHIFT                        5  /* OUT3MUTE */
#define WM8400_OUT3MUTE_WIDTH                        1  /* OUT3MUTE */
#define WM8400_OUT3ATTN                         0x0010  /* OUT3ATTN */
#define WM8400_OUT3ATTN_MASK                    0x0010  /* OUT3ATTN */
#define WM8400_OUT3ATTN_SHIFT                        4  /* OUT3ATTN */
#define WM8400_OUT3ATTN_WIDTH                        1  /* OUT3ATTN */
#define WM8400_OUT4MUTE                         0x0002  /* OUT4MUTE */
#define WM8400_OUT4MUTE_MASK                    0x0002  /* OUT4MUTE */
#define WM8400_OUT4MUTE_SHIFT                        1  /* OUT4MUTE */
#define WM8400_OUT4MUTE_WIDTH                        1  /* OUT4MUTE */
#define WM8400_OUT4ATTN                         0x0001  /* OUT4ATTN */
#define WM8400_OUT4ATTN_MASK                    0x0001  /* OUT4ATTN */
#define WM8400_OUT4ATTN_SHIFT                        0  /* OUT4ATTN */
#define WM8400_OUT4ATTN_WIDTH                        1  /* OUT4ATTN */

/*
 * R32 (0x20) - Left OPGA Volume
 */
#define WM8400_OPVU                             0x0100  /* OPVU */
#define WM8400_OPVU_MASK                        0x0100  /* OPVU */
#define WM8400_OPVU_SHIFT                            8  /* OPVU */
#define WM8400_OPVU_WIDTH                            1  /* OPVU */
#define WM8400_LOPGAZC                          0x0080  /* LOPGAZC */
#define WM8400_LOPGAZC_MASK                     0x0080  /* LOPGAZC */
#define WM8400_LOPGAZC_SHIFT                         7  /* LOPGAZC */
#define WM8400_LOPGAZC_WIDTH                         1  /* LOPGAZC */
#define WM8400_LOPGAVOL_MASK                    0x007F  /* LOPGAVOL - [6:0] */
#define WM8400_LOPGAVOL_SHIFT                        0  /* LOPGAVOL - [6:0] */
#define WM8400_LOPGAVOL_WIDTH                        7  /* LOPGAVOL - [6:0] */

/*
 * R33 (0x21) - Right OPGA Volume
 */
#define WM8400_OPVU                             0x0100  /* OPVU */
#define WM8400_OPVU_MASK                        0x0100  /* OPVU */
#define WM8400_OPVU_SHIFT                            8  /* OPVU */
#define WM8400_OPVU_WIDTH                            1  /* OPVU */
#define WM8400_ROPGAZC                          0x0080  /* ROPGAZC */
#define WM8400_ROPGAZC_MASK                     0x0080  /* ROPGAZC */
#define WM8400_ROPGAZC_SHIFT                         7  /* ROPGAZC */
#define WM8400_ROPGAZC_WIDTH                         1  /* ROPGAZC */
#define WM8400_ROPGAVOL_MASK                    0x007F  /* ROPGAVOL - [6:0] */
#define WM8400_ROPGAVOL_SHIFT                        0  /* ROPGAVOL - [6:0] */
#define WM8400_ROPGAVOL_WIDTH                        7  /* ROPGAVOL - [6:0] */

/*
 * R34 (0x22) - Speaker Volume
 */
#define WM8400_SPKATTN_MASK                     0x0003  /* SPKATTN - [1:0] */
#define WM8400_SPKATTN_SHIFT                         0  /* SPKATTN - [1:0] */
#define WM8400_SPKATTN_WIDTH                         2  /* SPKATTN - [1:0] */

/*
 * R35 (0x23) - ClassD1
 */
#define WM8400_CDMODE                           0x0100  /* CDMODE */
#define WM8400_CDMODE_MASK                      0x0100  /* CDMODE */
#define WM8400_CDMODE_SHIFT                          8  /* CDMODE */
#define WM8400_CDMODE_WIDTH                          1  /* CDMODE */
#define WM8400_CLASSD_CLK_SEL                   0x0080  /* CLASSD_CLK_SEL */
#define WM8400_CLASSD_CLK_SEL_MASK              0x0080  /* CLASSD_CLK_SEL */
#define WM8400_CLASSD_CLK_SEL_SHIFT                  7  /* CLASSD_CLK_SEL */
#define WM8400_CLASSD_CLK_SEL_WIDTH                  1  /* CLASSD_CLK_SEL */
#define WM8400_CD_SRCTRL                        0x0040  /* CD_SRCTRL */
#define WM8400_CD_SRCTRL_MASK                   0x0040  /* CD_SRCTRL */
#define WM8400_CD_SRCTRL_SHIFT                       6  /* CD_SRCTRL */
#define WM8400_CD_SRCTRL_WIDTH                       1  /* CD_SRCTRL */
#define WM8400_SPKNOPOP                         0x0020  /* SPKNOPOP */
#define WM8400_SPKNOPOP_MASK                    0x0020  /* SPKNOPOP */
#define WM8400_SPKNOPOP_SHIFT                        5  /* SPKNOPOP */
#define WM8400_SPKNOPOP_WIDTH                        1  /* SPKNOPOP */
#define WM8400_DBLERATE                         0x0010  /* DBLERATE */
#define WM8400_DBLERATE_MASK                    0x0010  /* DBLERATE */
#define WM8400_DBLERATE_SHIFT                        4  /* DBLERATE */
#define WM8400_DBLERATE_WIDTH                        1  /* DBLERATE */
#define WM8400_LOOPTEST                         0x0008  /* LOOPTEST */
#define WM8400_LOOPTEST_MASK                    0x0008  /* LOOPTEST */
#define WM8400_LOOPTEST_SHIFT                        3  /* LOOPTEST */
#define WM8400_LOOPTEST_WIDTH                        1  /* LOOPTEST */
#define WM8400_HALFABBIAS                       0x0004  /* HALFABBIAS */
#define WM8400_HALFABBIAS_MASK                  0x0004  /* HALFABBIAS */
#define WM8400_HALFABBIAS_SHIFT                      2  /* HALFABBIAS */
#define WM8400_HALFABBIAS_WIDTH                      1  /* HALFABBIAS */
#define WM8400_TRIDEL_MASK                      0x0003  /* TRIDEL - [1:0] */
#define WM8400_TRIDEL_SHIFT                          0  /* TRIDEL - [1:0] */
#define WM8400_TRIDEL_WIDTH                          2  /* TRIDEL - [1:0] */

/*
 * R37 (0x25) - ClassD3
 */
#define WM8400_DCGAIN_MASK                      0x0038  /* DCGAIN - [5:3] */
#define WM8400_DCGAIN_SHIFT                          3  /* DCGAIN - [5:3] */
#define WM8400_DCGAIN_WIDTH                          3  /* DCGAIN - [5:3] */
#define WM8400_ACGAIN_MASK                      0x0007  /* ACGAIN - [2:0] */
#define WM8400_ACGAIN_SHIFT                          0  /* ACGAIN - [2:0] */
#define WM8400_ACGAIN_WIDTH                          3  /* ACGAIN - [2:0] */

/*
 * R39 (0x27) - Input Mixer1
 */
#define WM8400_AINLMODE_MASK                    0x000C  /* AINLMODE - [3:2] */
#define WM8400_AINLMODE_SHIFT                        2  /* AINLMODE - [3:2] */
#define WM8400_AINLMODE_WIDTH                        2  /* AINLMODE - [3:2] */
#define WM8400_AINRMODE_MASK                    0x0003  /* AINRMODE - [1:0] */
#define WM8400_AINRMODE_SHIFT                        0  /* AINRMODE - [1:0] */
#define WM8400_AINRMODE_WIDTH                        2  /* AINRMODE - [1:0] */

/*
 * R40 (0x28) - Input Mixer2
 */
#define WM8400_LMP4                             0x0080  /* LMP4 */
#define WM8400_LMP4_MASK                        0x0080  /* LMP4 */
#define WM8400_LMP4_SHIFT                            7  /* LMP4 */
#define WM8400_LMP4_WIDTH                            1  /* LMP4 */
#define WM8400_LMN3                             0x0040  /* LMN3 */
#define WM8400_LMN3_MASK                        0x0040  /* LMN3 */
#define WM8400_LMN3_SHIFT                            6  /* LMN3 */
#define WM8400_LMN3_WIDTH                            1  /* LMN3 */
#define WM8400_LMP2                             0x0020  /* LMP2 */
#define WM8400_LMP2_MASK                        0x0020  /* LMP2 */
#define WM8400_LMP2_SHIFT                            5  /* LMP2 */
#define WM8400_LMP2_WIDTH                            1  /* LMP2 */
#define WM8400_LMN1                             0x0010  /* LMN1 */
#define WM8400_LMN1_MASK                        0x0010  /* LMN1 */
#define WM8400_LMN1_SHIFT                            4  /* LMN1 */
#define WM8400_LMN1_WIDTH                            1  /* LMN1 */
#define WM8400_RMP4                             0x0008  /* RMP4 */
#define WM8400_RMP4_MASK                        0x0008  /* RMP4 */
#define WM8400_RMP4_SHIFT                            3  /* RMP4 */
#define WM8400_RMP4_WIDTH                            1  /* RMP4 */
#define WM8400_RMN3                             0x0004  /* RMN3 */
#define WM8400_RMN3_MASK                        0x0004  /* RMN3 */
#define WM8400_RMN3_SHIFT                            2  /* RMN3 */
#define WM8400_RMN3_WIDTH                            1  /* RMN3 */
#define WM8400_RMP2                             0x0002  /* RMP2 */
#define WM8400_RMP2_MASK                        0x0002  /* RMP2 */
#define WM8400_RMP2_SHIFT                            1  /* RMP2 */
#define WM8400_RMP2_WIDTH                            1  /* RMP2 */
#define WM8400_RMN1                             0x0001  /* RMN1 */
#define WM8400_RMN1_MASK                        0x0001  /* RMN1 */
#define WM8400_RMN1_SHIFT                            0  /* RMN1 */
#define WM8400_RMN1_WIDTH                            1  /* RMN1 */

/*
 * R41 (0x29) - Input Mixer3
 */
#define WM8400_L34MNB                           0x0100  /* L34MNB */
#define WM8400_L34MNB_MASK                      0x0100  /* L34MNB */
#define WM8400_L34MNB_SHIFT                          8  /* L34MNB */
#define WM8400_L34MNB_WIDTH                          1  /* L34MNB */
#define WM8400_L34MNBST                         0x0080  /* L34MNBST */
#define WM8400_L34MNBST_MASK                    0x0080  /* L34MNBST */
#define WM8400_L34MNBST_SHIFT                        7  /* L34MNBST */
#define WM8400_L34MNBST_WIDTH                        1  /* L34MNBST */
#define WM8400_L12MNB                           0x0020  /* L12MNB */
#define WM8400_L12MNB_MASK                      0x0020  /* L12MNB */
#define WM8400_L12MNB_SHIFT                          5  /* L12MNB */
#define WM8400_L12MNB_WIDTH                          1  /* L12MNB */
#define WM8400_L12MNBST                         0x0010  /* L12MNBST */
#define WM8400_L12MNBST_MASK                    0x0010  /* L12MNBST */
#define WM8400_L12MNBST_SHIFT                        4  /* L12MNBST */
#define WM8400_L12MNBST_WIDTH                        1  /* L12MNBST */
#define WM8400_LDBVOL_MASK                      0x0007  /* LDBVOL - [2:0] */
#define WM8400_LDBVOL_SHIFT                          0  /* LDBVOL - [2:0] */
#define WM8400_LDBVOL_WIDTH                          3  /* LDBVOL - [2:0] */

/*
 * R42 (0x2A) - Input Mixer4
 */
#define WM8400_R34MNB                           0x0100  /* R34MNB */
#define WM8400_R34MNB_MASK                      0x0100  /* R34MNB */
#define WM8400_R34MNB_SHIFT                          8  /* R34MNB */
#define WM8400_R34MNB_WIDTH                          1  /* R34MNB */
#define WM8400_R34MNBST                         0x0080  /* R34MNBST */
#define WM8400_R34MNBST_MASK                    0x0080  /* R34MNBST */
#define WM8400_R34MNBST_SHIFT                        7  /* R34MNBST */
#define WM8400_R34MNBST_WIDTH                        1  /* R34MNBST */
#define WM8400_R12MNB                           0x0020  /* R12MNB */
#define WM8400_R12MNB_MASK                      0x0020  /* R12MNB */
#define WM8400_R12MNB_SHIFT                          5  /* R12MNB */
#define WM8400_R12MNB_WIDTH                          1  /* R12MNB */
#define WM8400_R12MNBST                         0x0010  /* R12MNBST */
#define WM8400_R12MNBST_MASK                    0x0010  /* R12MNBST */
#define WM8400_R12MNBST_SHIFT                        4  /* R12MNBST */
#define WM8400_R12MNBST_WIDTH                        1  /* R12MNBST */
#define WM8400_RDBVOL_MASK                      0x0007  /* RDBVOL - [2:0] */
#define WM8400_RDBVOL_SHIFT                          0  /* RDBVOL - [2:0] */
#define WM8400_RDBVOL_WIDTH                          3  /* RDBVOL - [2:0] */

/*
 * R43 (0x2B) - Input Mixer5
 */
#define WM8400_LI2BVOL_MASK                     0x01C0  /* LI2BVOL - [8:6] */
#define WM8400_LI2BVOL_SHIFT                         6  /* LI2BVOL - [8:6] */
#define WM8400_LI2BVOL_WIDTH                         3  /* LI2BVOL - [8:6] */
#define WM8400_LR4BVOL_MASK                     0x0038  /* LR4BVOL - [5:3] */
#define WM8400_LR4BVOL_SHIFT                         3  /* LR4BVOL - [5:3] */
#define WM8400_LR4BVOL_WIDTH                         3  /* LR4BVOL - [5:3] */
#define WM8400_LL4BVOL_MASK                     0x0007  /* LL4BVOL - [2:0] */
#define WM8400_LL4BVOL_SHIFT                         0  /* LL4BVOL - [2:0] */
#define WM8400_LL4BVOL_WIDTH                         3  /* LL4BVOL - [2:0] */

/*
 * R44 (0x2C) - Input Mixer6
 */
#define WM8400_RI2BVOL_MASK                     0x01C0  /* RI2BVOL - [8:6] */
#define WM8400_RI2BVOL_SHIFT                         6  /* RI2BVOL - [8:6] */
#define WM8400_RI2BVOL_WIDTH                         3  /* RI2BVOL - [8:6] */
#define WM8400_RL4BVOL_MASK                     0x0038  /* RL4BVOL - [5:3] */
#define WM8400_RL4BVOL_SHIFT                         3  /* RL4BVOL - [5:3] */
#define WM8400_RL4BVOL_WIDTH                         3  /* RL4BVOL - [5:3] */
#define WM8400_RR4BVOL_MASK                     0x0007  /* RR4BVOL - [2:0] */
#define WM8400_RR4BVOL_SHIFT                         0  /* RR4BVOL - [2:0] */
#define WM8400_RR4BVOL_WIDTH                         3  /* RR4BVOL - [2:0] */

/*
 * R45 (0x2D) - Output Mixer1
 */
#define WM8400_LRBLO                            0x0080  /* LRBLO */
#define WM8400_LRBLO_MASK                       0x0080  /* LRBLO */
#define WM8400_LRBLO_SHIFT                           7  /* LRBLO */
#define WM8400_LRBLO_WIDTH                           1  /* LRBLO */
#define WM8400_LLBLO                            0x0040  /* LLBLO */
#define WM8400_LLBLO_MASK                       0x0040  /* LLBLO */
#define WM8400_LLBLO_SHIFT                           6  /* LLBLO */
#define WM8400_LLBLO_WIDTH                           1  /* LLBLO */
#define WM8400_LRI3LO                           0x0020  /* LRI3LO */
#define WM8400_LRI3LO_MASK                      0x0020  /* LRI3LO */
#define WM8400_LRI3LO_SHIFT                          5  /* LRI3LO */
#define WM8400_LRI3LO_WIDTH                          1  /* LRI3LO */
#define WM8400_LLI3LO                           0x0010  /* LLI3LO */
#define WM8400_LLI3LO_MASK                      0x0010  /* LLI3LO */
#define WM8400_LLI3LO_SHIFT                          4  /* LLI3LO */
#define WM8400_LLI3LO_WIDTH                          1  /* LLI3LO */
#define WM8400_LR12LO                           0x0008  /* LR12LO */
#define WM8400_LR12LO_MASK                      0x0008  /* LR12LO */
#define WM8400_LR12LO_SHIFT                          3  /* LR12LO */
#define WM8400_LR12LO_WIDTH                          1  /* LR12LO */
#define WM8400_LL12LO                           0x0004  /* LL12LO */
#define WM8400_LL12LO_MASK                      0x0004  /* LL12LO */
#define WM8400_LL12LO_SHIFT                          2  /* LL12LO */
#define WM8400_LL12LO_WIDTH                          1  /* LL12LO */
#define WM8400_LDLO                             0x0001  /* LDLO */
#define WM8400_LDLO_MASK                        0x0001  /* LDLO */
#define WM8400_LDLO_SHIFT                            0  /* LDLO */
#define WM8400_LDLO_WIDTH                            1  /* LDLO */

/*
 * R46 (0x2E) - Output Mixer2
 */
#define WM8400_RLBRO                            0x0080  /* RLBRO */
#define WM8400_RLBRO_MASK                       0x0080  /* RLBRO */
#define WM8400_RLBRO_SHIFT                           7  /* RLBRO */
#define WM8400_RLBRO_WIDTH                           1  /* RLBRO */
#define WM8400_RRBRO                            0x0040  /* RRBRO */
#define WM8400_RRBRO_MASK                       0x0040  /* RRBRO */
#define WM8400_RRBRO_SHIFT                           6  /* RRBRO */
#define WM8400_RRBRO_WIDTH                           1  /* RRBRO */
#define WM8400_RLI3RO                           0x0020  /* RLI3RO */
#define WM8400_RLI3RO_MASK                      0x0020  /* RLI3RO */
#define WM8400_RLI3RO_SHIFT                          5  /* RLI3RO */
#define WM8400_RLI3RO_WIDTH                          1  /* RLI3RO */
#define WM8400_RRI3RO                           0x0010  /* RRI3RO */
#define WM8400_RRI3RO_MASK                      0x0010  /* RRI3RO */
#define WM8400_RRI3RO_SHIFT                          4  /* RRI3RO */
#define WM8400_RRI3RO_WIDTH                          1  /* RRI3RO */
#define WM8400_RL12RO                           0x0008  /* RL12RO */
#define WM8400_RL12RO_MASK                      0x0008  /* RL12RO */
#define WM8400_RL12RO_SHIFT                          3  /* RL12RO */
#define WM8400_RL12RO_WIDTH                          1  /* RL12RO */
#define WM8400_RR12RO                           0x0004  /* RR12RO */
#define WM8400_RR12RO_MASK                      0x0004  /* RR12RO */
#define WM8400_RR12RO_SHIFT                          2  /* RR12RO */
#define WM8400_RR12RO_WIDTH                          1  /* RR12RO */
#define WM8400_RDRO                             0x0001  /* RDRO */
#define WM8400_RDRO_MASK                        0x0001  /* RDRO */
#define WM8400_RDRO_SHIFT                            0  /* RDRO */
#define WM8400_RDRO_WIDTH                            1  /* RDRO */

/*
 * R47 (0x2F) - Output Mixer3
 */
#define WM8400_LLI3LOVOL_MASK                   0x01C0  /* LLI3LOVOL - [8:6] */
#define WM8400_LLI3LOVOL_SHIFT                       6  /* LLI3LOVOL - [8:6] */
#define WM8400_LLI3LOVOL_WIDTH                       3  /* LLI3LOVOL - [8:6] */
#define WM8400_LR12LOVOL_MASK                   0x0038  /* LR12LOVOL - [5:3] */
#define WM8400_LR12LOVOL_SHIFT                       3  /* LR12LOVOL - [5:3] */
#define WM8400_LR12LOVOL_WIDTH                       3  /* LR12LOVOL - [5:3] */
#define WM8400_LL12LOVOL_MASK                   0x0007  /* LL12LOVOL - [2:0] */
#define WM8400_LL12LOVOL_SHIFT                       0  /* LL12LOVOL - [2:0] */
#define WM8400_LL12LOVOL_WIDTH                       3  /* LL12LOVOL - [2:0] */

/*
 * R48 (0x30) - Output Mixer4
 */
#define WM8400_RRI3ROVOL_MASK                   0x01C0  /* RRI3ROVOL - [8:6] */
#define WM8400_RRI3ROVOL_SHIFT                       6  /* RRI3ROVOL - [8:6] */
#define WM8400_RRI3ROVOL_WIDTH                       3  /* RRI3ROVOL - [8:6] */
#define WM8400_RL12ROVOL_MASK                   0x0038  /* RL12ROVOL - [5:3] */
#define WM8400_RL12ROVOL_SHIFT                       3  /* RL12ROVOL - [5:3] */
#define WM8400_RL12ROVOL_WIDTH                       3  /* RL12ROVOL - [5:3] */
#define WM8400_RR12ROVOL_MASK                   0x0007  /* RR12ROVOL - [2:0] */
#define WM8400_RR12ROVOL_SHIFT                       0  /* RR12ROVOL - [2:0] */
#define WM8400_RR12ROVOL_WIDTH                       3  /* RR12ROVOL - [2:0] */

/*
 * R49 (0x31) - Output Mixer5
 */
#define WM8400_LRI3LOVOL_MASK                   0x01C0  /* LRI3LOVOL - [8:6] */
#define WM8400_LRI3LOVOL_SHIFT                       6  /* LRI3LOVOL - [8:6] */
#define WM8400_LRI3LOVOL_WIDTH                       3  /* LRI3LOVOL - [8:6] */
#define WM8400_LRBLOVOL_MASK                    0x0038  /* LRBLOVOL - [5:3] */
#define WM8400_LRBLOVOL_SHIFT                        3  /* LRBLOVOL - [5:3] */
#define WM8400_LRBLOVOL_WIDTH                        3  /* LRBLOVOL - [5:3] */
#define WM8400_LLBLOVOL_MASK                    0x0007  /* LLBLOVOL - [2:0] */
#define WM8400_LLBLOVOL_SHIFT                        0  /* LLBLOVOL - [2:0] */
#define WM8400_LLBLOVOL_WIDTH                        3  /* LLBLOVOL - [2:0] */

/*
 * R50 (0x32) - Output Mixer6
 */
#define WM8400_RLI3ROVOL_MASK                   0x01C0  /* RLI3ROVOL - [8:6] */
#define WM8400_RLI3ROVOL_SHIFT                       6  /* RLI3ROVOL - [8:6] */
#define WM8400_RLI3ROVOL_WIDTH                       3  /* RLI3ROVOL - [8:6] */
#define WM8400_RLBROVOL_MASK                    0x0038  /* RLBROVOL - [5:3] */
#define WM8400_RLBROVOL_SHIFT                        3  /* RLBROVOL - [5:3] */
#define WM8400_RLBROVOL_WIDTH                        3  /* RLBROVOL - [5:3] */
#define WM8400_RRBROVOL_MASK                    0x0007  /* RRBROVOL - [2:0] */
#define WM8400_RRBROVOL_SHIFT                        0  /* RRBROVOL - [2:0] */
#define WM8400_RRBROVOL_WIDTH                        3  /* RRBROVOL - [2:0] */

/*
 * R51 (0x33) - Out3/4 Mixer
 */
#define WM8400_VSEL_MASK                        0x0180  /* VSEL - [8:7] */
#define WM8400_VSEL_SHIFT                            7  /* VSEL - [8:7] */
#define WM8400_VSEL_WIDTH                            2  /* VSEL - [8:7] */
#define WM8400_LI4O3                            0x0020  /* LI4O3 */
#define WM8400_LI4O3_MASK                       0x0020  /* LI4O3 */
#define WM8400_LI4O3_SHIFT                           5  /* LI4O3 */
#define WM8400_LI4O3_WIDTH                           1  /* LI4O3 */
#define WM8400_LPGAO3                           0x0010  /* LPGAO3 */
#define WM8400_LPGAO3_MASK                      0x0010  /* LPGAO3 */
#define WM8400_LPGAO3_SHIFT                          4  /* LPGAO3 */
#define WM8400_LPGAO3_WIDTH                          1  /* LPGAO3 */
#define WM8400_RI4O4                            0x0002  /* RI4O4 */
#define WM8400_RI4O4_MASK                       0x0002  /* RI4O4 */
#define WM8400_RI4O4_SHIFT                           1  /* RI4O4 */
#define WM8400_RI4O4_WIDTH                           1  /* RI4O4 */
#define WM8400_RPGAO4                           0x0001  /* RPGAO4 */
#define WM8400_RPGAO4_MASK                      0x0001  /* RPGAO4 */
#define WM8400_RPGAO4_SHIFT                          0  /* RPGAO4 */
#define WM8400_RPGAO4_WIDTH                          1  /* RPGAO4 */

/*
 * R52 (0x34) - Line Mixer1
 */
#define WM8400_LLOPGALON                        0x0040  /* LLOPGALON */
#define WM8400_LLOPGALON_MASK                   0x0040  /* LLOPGALON */
#define WM8400_LLOPGALON_SHIFT                       6  /* LLOPGALON */
#define WM8400_LLOPGALON_WIDTH                       1  /* LLOPGALON */
#define WM8400_LROPGALON                        0x0020  /* LROPGALON */
#define WM8400_LROPGALON_MASK                   0x0020  /* LROPGALON */
#define WM8400_LROPGALON_SHIFT                       5  /* LROPGALON */
#define WM8400_LROPGALON_WIDTH                       1  /* LROPGALON */
#define WM8400_LOPLON                           0x0010  /* LOPLON */
#define WM8400_LOPLON_MASK                      0x0010  /* LOPLON */
#define WM8400_LOPLON_SHIFT                          4  /* LOPLON */
#define WM8400_LOPLON_WIDTH                          1  /* LOPLON */
#define WM8400_LR12LOP                          0x0004  /* LR12LOP */
#define WM8400_LR12LOP_MASK                     0x0004  /* LR12LOP */
#define WM8400_LR12LOP_SHIFT                         2  /* LR12LOP */
#define WM8400_LR12LOP_WIDTH                         1  /* LR12LOP */
#define WM8400_LL12LOP                          0x0002  /* LL12LOP */
#define WM8400_LL12LOP_MASK                     0x0002  /* LL12LOP */
#define WM8400_LL12LOP_SHIFT                         1  /* LL12LOP */
#define WM8400_LL12LOP_WIDTH                         1  /* LL12LOP */
#define WM8400_LLOPGALOP                        0x0001  /* LLOPGALOP */
#define WM8400_LLOPGALOP_MASK                   0x0001  /* LLOPGALOP */
#define WM8400_LLOPGALOP_SHIFT                       0  /* LLOPGALOP */
#define WM8400_LLOPGALOP_WIDTH                       1  /* LLOPGALOP */

/*
 * R53 (0x35) - Line Mixer2
 */
#define WM8400_RROPGARON                        0x0040  /* RROPGARON */
#define WM8400_RROPGARON_MASK                   0x0040  /* RROPGARON */
#define WM8400_RROPGARON_SHIFT                       6  /* RROPGARON */
#define WM8400_RROPGARON_WIDTH                       1  /* RROPGARON */
#define WM8400_RLOPGARON                        0x0020  /* RLOPGARON */
#define WM8400_RLOPGARON_MASK                   0x0020  /* RLOPGARON */
#define WM8400_RLOPGARON_SHIFT                       5  /* RLOPGARON */
#define WM8400_RLOPGARON_WIDTH                       1  /* RLOPGARON */
#define WM8400_ROPRON                           0x0010  /* ROPRON */
#define WM8400_ROPRON_MASK                      0x0010  /* ROPRON */
#define WM8400_ROPRON_SHIFT                          4  /* ROPRON */
#define WM8400_ROPRON_WIDTH                          1  /* ROPRON */
#define WM8400_RL12ROP                          0x0004  /* RL12ROP */
#define WM8400_RL12ROP_MASK                     0x0004  /* RL12ROP */
#define WM8400_RL12ROP_SHIFT                         2  /* RL12ROP */
#define WM8400_RL12ROP_WIDTH                         1  /* RL12ROP */
#define WM8400_RR12ROP                          0x0002  /* RR12ROP */
#define WM8400_RR12ROP_MASK                     0x0002  /* RR12ROP */
#define WM8400_RR12ROP_SHIFT                         1  /* RR12ROP */
#define WM8400_RR12ROP_WIDTH                         1  /* RR12ROP */
#define WM8400_RROPGAROP                        0x0001  /* RROPGAROP */
#define WM8400_RROPGAROP_MASK                   0x0001  /* RROPGAROP */
#define WM8400_RROPGAROP_SHIFT                       0  /* RROPGAROP */
#define WM8400_RROPGAROP_WIDTH                       1  /* RROPGAROP */

/*
 * R54 (0x36) - Speaker Mixer
 */
#define WM8400_LB2SPK                           0x0080  /* LB2SPK */
#define WM8400_LB2SPK_MASK                      0x0080  /* LB2SPK */
#define WM8400_LB2SPK_SHIFT                          7  /* LB2SPK */
#define WM8400_LB2SPK_WIDTH                          1  /* LB2SPK */
#define WM8400_RB2SPK                           0x0040  /* RB2SPK */
#define WM8400_RB2SPK_MASK                      0x0040  /* RB2SPK */
#define WM8400_RB2SPK_SHIFT                          6  /* RB2SPK */
#define WM8400_RB2SPK_WIDTH                          1  /* RB2SPK */
#define WM8400_LI2SPK                           0x0020  /* LI2SPK */
#define WM8400_LI2SPK_MASK                      0x0020  /* LI2SPK */
#define WM8400_LI2SPK_SHIFT                          5  /* LI2SPK */
#define WM8400_LI2SPK_WIDTH                          1  /* LI2SPK */
#define WM8400_RI2SPK                           0x0010  /* RI2SPK */
#define WM8400_RI2SPK_MASK                      0x0010  /* RI2SPK */
#define WM8400_RI2SPK_SHIFT                          4  /* RI2SPK */
#define WM8400_RI2SPK_WIDTH                          1  /* RI2SPK */
#define WM8400_LOPGASPK                         0x0008  /* LOPGASPK */
#define WM8400_LOPGASPK_MASK                    0x0008  /* LOPGASPK */
#define WM8400_LOPGASPK_SHIFT                        3  /* LOPGASPK */
#define WM8400_LOPGASPK_WIDTH                        1  /* LOPGASPK */
#define WM8400_ROPGASPK                         0x0004  /* ROPGASPK */
#define WM8400_ROPGASPK_MASK                    0x0004  /* ROPGASPK */
#define WM8400_ROPGASPK_SHIFT                        2  /* ROPGASPK */
#define WM8400_ROPGASPK_WIDTH                        1  /* ROPGASPK */
#define WM8400_LDSPK                            0x0002  /* LDSPK */
#define WM8400_LDSPK_MASK                       0x0002  /* LDSPK */
#define WM8400_LDSPK_SHIFT                           1  /* LDSPK */
#define WM8400_LDSPK_WIDTH                           1  /* LDSPK */
#define WM8400_RDSPK                            0x0001  /* RDSPK */
#define WM8400_RDSPK_MASK                       0x0001  /* RDSPK */
#define WM8400_RDSPK_SHIFT                           0  /* RDSPK */
#define WM8400_RDSPK_WIDTH                           1  /* RDSPK */

/*
 * R55 (0x37) - Additional Control
 */
#define WM8400_VROI                             0x0001  /* VROI */
#define WM8400_VROI_MASK                        0x0001  /* VROI */
#define WM8400_VROI_SHIFT                            0  /* VROI */
#define WM8400_VROI_WIDTH                            1  /* VROI */

/*
 * R56 (0x38) - AntiPOP1
 */
#define WM8400_DIS_LLINE                        0x0020  /* DIS_LLINE */
#define WM8400_DIS_LLINE_MASK                   0x0020  /* DIS_LLINE */
#define WM8400_DIS_LLINE_SHIFT                       5  /* DIS_LLINE */
#define WM8400_DIS_LLINE_WIDTH                       1  /* DIS_LLINE */
#define WM8400_DIS_RLINE                        0x0010  /* DIS_RLINE */
#define WM8400_DIS_RLINE_MASK                   0x0010  /* DIS_RLINE */
#define WM8400_DIS_RLINE_SHIFT                       4  /* DIS_RLINE */
#define WM8400_DIS_RLINE_WIDTH                       1  /* DIS_RLINE */
#define WM8400_DIS_OUT3                         0x0008  /* DIS_OUT3 */
#define WM8400_DIS_OUT3_MASK                    0x0008  /* DIS_OUT3 */
#define WM8400_DIS_OUT3_SHIFT                        3  /* DIS_OUT3 */
#define WM8400_DIS_OUT3_WIDTH                        1  /* DIS_OUT3 */
#define WM8400_DIS_OUT4                         0x0004  /* DIS_OUT4 */
#define WM8400_DIS_OUT4_MASK                    0x0004  /* DIS_OUT4 */
#define WM8400_DIS_OUT4_SHIFT                        2  /* DIS_OUT4 */
#define WM8400_DIS_OUT4_WIDTH                        1  /* DIS_OUT4 */
#define WM8400_DIS_LOUT                         0x0002  /* DIS_LOUT */
#define WM8400_DIS_LOUT_MASK                    0x0002  /* DIS_LOUT */
#define WM8400_DIS_LOUT_SHIFT                        1  /* DIS_LOUT */
#define WM8400_DIS_LOUT_WIDTH                        1  /* DIS_LOUT */
#define WM8400_DIS_ROUT                         0x0001  /* DIS_ROUT */
#define WM8400_DIS_ROUT_MASK                    0x0001  /* DIS_ROUT */
#define WM8400_DIS_ROUT_SHIFT                        0  /* DIS_ROUT */
#define WM8400_DIS_ROUT_WIDTH                        1  /* DIS_ROUT */

/*
 * R57 (0x39) - AntiPOP2
 */
#define WM8400_SOFTST                           0x0040  /* SOFTST */
#define WM8400_SOFTST_MASK                      0x0040  /* SOFTST */
#define WM8400_SOFTST_SHIFT                          6  /* SOFTST */
#define WM8400_SOFTST_WIDTH                          1  /* SOFTST */
#define WM8400_BUFIOEN                          0x0008  /* BUFIOEN */
#define WM8400_BUFIOEN_MASK                     0x0008  /* BUFIOEN */
#define WM8400_BUFIOEN_SHIFT                         3  /* BUFIOEN */
#define WM8400_BUFIOEN_WIDTH                         1  /* BUFIOEN */
#define WM8400_BUFDCOPEN                        0x0004  /* BUFDCOPEN */
#define WM8400_BUFDCOPEN_MASK                   0x0004  /* BUFDCOPEN */
#define WM8400_BUFDCOPEN_SHIFT                       2  /* BUFDCOPEN */
#define WM8400_BUFDCOPEN_WIDTH                       1  /* BUFDCOPEN */
#define WM8400_POBCTRL                          0x0002  /* POBCTRL */
#define WM8400_POBCTRL_MASK                     0x0002  /* POBCTRL */
#define WM8400_POBCTRL_SHIFT                         1  /* POBCTRL */
#define WM8400_POBCTRL_WIDTH                         1  /* POBCTRL */
#define WM8400_VMIDTOG                          0x0001  /* VMIDTOG */
#define WM8400_VMIDTOG_MASK                     0x0001  /* VMIDTOG */
#define WM8400_VMIDTOG_SHIFT                         0  /* VMIDTOG */
#define WM8400_VMIDTOG_WIDTH                         1  /* VMIDTOG */

/*
 * R58 (0x3A) - MICBIAS
 */
#define WM8400_MCDSCTH_MASK                     0x00C0  /* MCDSCTH - [7:6] */
#define WM8400_MCDSCTH_SHIFT                         6  /* MCDSCTH - [7:6] */
#define WM8400_MCDSCTH_WIDTH                         2  /* MCDSCTH - [7:6] */
#define WM8400_MCDTHR_MASK                      0x0038  /* MCDTHR - [5:3] */
#define WM8400_MCDTHR_SHIFT                          3  /* MCDTHR - [5:3] */
#define WM8400_MCDTHR_WIDTH                          3  /* MCDTHR - [5:3] */
#define WM8400_MCD                              0x0004  /* MCD */
#define WM8400_MCD_MASK                         0x0004  /* MCD */
#define WM8400_MCD_SHIFT                             2  /* MCD */
#define WM8400_MCD_WIDTH                             1  /* MCD */
#define WM8400_MBSEL                            0x0001  /* MBSEL */
#define WM8400_MBSEL_MASK                       0x0001  /* MBSEL */
#define WM8400_MBSEL_SHIFT                           0  /* MBSEL */
#define WM8400_MBSEL_WIDTH                           1  /* MBSEL */

/*
 * R60 (0x3C) - FLL Control 1
 */
#define WM8400_FLL_REF_FREQ                     0x1000  /* FLL_REF_FREQ */
#define WM8400_FLL_REF_FREQ_MASK                0x1000  /* FLL_REF_FREQ */
#define WM8400_FLL_REF_FREQ_SHIFT                   12  /* FLL_REF_FREQ */
#define WM8400_FLL_REF_FREQ_WIDTH                    1  /* FLL_REF_FREQ */
#define WM8400_FLL_CLK_SRC_MASK                 0x0C00  /* FLL_CLK_SRC - [11:10] */
#define WM8400_FLL_CLK_SRC_SHIFT                    10  /* FLL_CLK_SRC - [11:10] */
#define WM8400_FLL_CLK_SRC_WIDTH                     2  /* FLL_CLK_SRC - [11:10] */
#define WM8400_FLL_FRAC                         0x0200  /* FLL_FRAC */
#define WM8400_FLL_FRAC_MASK                    0x0200  /* FLL_FRAC */
#define WM8400_FLL_FRAC_SHIFT                        9  /* FLL_FRAC */
#define WM8400_FLL_FRAC_WIDTH                        1  /* FLL_FRAC */
#define WM8400_FLL_OSC_ENA                      0x0100  /* FLL_OSC_ENA */
#define WM8400_FLL_OSC_ENA_MASK                 0x0100  /* FLL_OSC_ENA */
#define WM8400_FLL_OSC_ENA_SHIFT                     8  /* FLL_OSC_ENA */
#define WM8400_FLL_OSC_ENA_WIDTH                     1  /* FLL_OSC_ENA */
#define WM8400_FLL_CTRL_RATE_MASK               0x00E0  /* FLL_CTRL_RATE - [7:5] */
#define WM8400_FLL_CTRL_RATE_SHIFT                   5  /* FLL_CTRL_RATE - [7:5] */
#define WM8400_FLL_CTRL_RATE_WIDTH                   3  /* FLL_CTRL_RATE - [7:5] */
#define WM8400_FLL_FRATIO_MASK                  0x001F  /* FLL_FRATIO - [4:0] */
#define WM8400_FLL_FRATIO_SHIFT                      0  /* FLL_FRATIO - [4:0] */
#define WM8400_FLL_FRATIO_WIDTH                      5  /* FLL_FRATIO - [4:0] */

/*
 * R61 (0x3D) - FLL Control 2
 */
#define WM8400_FLL_K_MASK                       0xFFFF  /* FLL_K - [15:0] */
#define WM8400_FLL_K_SHIFT                           0  /* FLL_K - [15:0] */
#define WM8400_FLL_K_WIDTH                          16  /* FLL_K - [15:0] */

/*
 * R62 (0x3E) - FLL Control 3
 */
#define WM8400_FLL_N_MASK                       0x03FF  /* FLL_N - [9:0] */
#define WM8400_FLL_N_SHIFT                           0  /* FLL_N - [9:0] */
#define WM8400_FLL_N_WIDTH                          10  /* FLL_N - [9:0] */

/*
 * R63 (0x3F) - FLL Control 4
 */
#define WM8400_FLL_TRK_GAIN_MASK                0x0078  /* FLL_TRK_GAIN - [6:3] */
#define WM8400_FLL_TRK_GAIN_SHIFT                    3  /* FLL_TRK_GAIN - [6:3] */
#define WM8400_FLL_TRK_GAIN_WIDTH                    4  /* FLL_TRK_GAIN - [6:3] */
#define WM8400_FLL_OUTDIV_MASK                  0x0007  /* FLL_OUTDIV - [2:0] */
#define WM8400_FLL_OUTDIV_SHIFT                      0  /* FLL_OUTDIV - [2:0] */
#define WM8400_FLL_OUTDIV_WIDTH                      3  /* FLL_OUTDIV - [2:0] */

struct wm8400;
void wm8400_reset_codec_reg_cache(struct wm8400 *wm8400);

#endif
