#ifndef _WM8737_H
#define _WM8737_H

/*
 * wm8737.c  --  WM8523 ALSA SoC Audio driver
 *
 * Copyright 2010 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Register values.
 */
#define WM8737_LEFT_PGA_VOLUME                  0x00
#define WM8737_RIGHT_PGA_VOLUME                 0x01
#define WM8737_AUDIO_PATH_L                     0x02
#define WM8737_AUDIO_PATH_R                     0x03
#define WM8737_3D_ENHANCE                       0x04
#define WM8737_ADC_CONTROL                      0x05
#define WM8737_POWER_MANAGEMENT                 0x06
#define WM8737_AUDIO_FORMAT                     0x07
#define WM8737_CLOCKING                         0x08
#define WM8737_MIC_PREAMP_CONTROL               0x09
#define WM8737_MISC_BIAS_CONTROL                0x0A
#define WM8737_NOISE_GATE                       0x0B
#define WM8737_ALC1                             0x0C
#define WM8737_ALC2                             0x0D
#define WM8737_ALC3                             0x0E
#define WM8737_RESET                            0x0F

#define WM8737_REGISTER_COUNT                   16
#define WM8737_MAX_REGISTER                     0x0F

/*
 * Field Definitions.
 */

/*
 * R0 (0x00) - Left PGA volume
 */
#define WM8737_LVU                              0x0100  /* LVU */
#define WM8737_LVU_MASK                         0x0100  /* LVU */
#define WM8737_LVU_SHIFT                             8  /* LVU */
#define WM8737_LVU_WIDTH                             1  /* LVU */
#define WM8737_LINVOL_MASK                      0x00FF  /* LINVOL - [7:0] */
#define WM8737_LINVOL_SHIFT                          0  /* LINVOL - [7:0] */
#define WM8737_LINVOL_WIDTH                          8  /* LINVOL - [7:0] */

/*
 * R1 (0x01) - Right PGA volume
 */
#define WM8737_RVU                              0x0100  /* RVU */
#define WM8737_RVU_MASK                         0x0100  /* RVU */
#define WM8737_RVU_SHIFT                             8  /* RVU */
#define WM8737_RVU_WIDTH                             1  /* RVU */
#define WM8737_RINVOL_MASK                      0x00FF  /* RINVOL - [7:0] */
#define WM8737_RINVOL_SHIFT                          0  /* RINVOL - [7:0] */
#define WM8737_RINVOL_WIDTH                          8  /* RINVOL - [7:0] */

/*
 * R2 (0x02) - AUDIO path L
 */
#define WM8737_LINSEL_MASK                      0x0180  /* LINSEL - [8:7] */
#define WM8737_LINSEL_SHIFT                          7  /* LINSEL - [8:7] */
#define WM8737_LINSEL_WIDTH                          2  /* LINSEL - [8:7] */
#define WM8737_LMICBOOST_MASK                   0x0060  /* LMICBOOST - [6:5] */
#define WM8737_LMICBOOST_SHIFT                       5  /* LMICBOOST - [6:5] */
#define WM8737_LMICBOOST_WIDTH                       2  /* LMICBOOST - [6:5] */
#define WM8737_LMBE                             0x0010  /* LMBE */
#define WM8737_LMBE_MASK                        0x0010  /* LMBE */
#define WM8737_LMBE_SHIFT                            4  /* LMBE */
#define WM8737_LMBE_WIDTH                            1  /* LMBE */
#define WM8737_LMZC                             0x0008  /* LMZC */
#define WM8737_LMZC_MASK                        0x0008  /* LMZC */
#define WM8737_LMZC_SHIFT                            3  /* LMZC */
#define WM8737_LMZC_WIDTH                            1  /* LMZC */
#define WM8737_LPZC                             0x0004  /* LPZC */
#define WM8737_LPZC_MASK                        0x0004  /* LPZC */
#define WM8737_LPZC_SHIFT                            2  /* LPZC */
#define WM8737_LPZC_WIDTH                            1  /* LPZC */
#define WM8737_LZCTO_MASK                       0x0003  /* LZCTO - [1:0] */
#define WM8737_LZCTO_SHIFT                           0  /* LZCTO - [1:0] */
#define WM8737_LZCTO_WIDTH                           2  /* LZCTO - [1:0] */

/*
 * R3 (0x03) - AUDIO path R
 */
#define WM8737_RINSEL_MASK                      0x0180  /* RINSEL - [8:7] */
#define WM8737_RINSEL_SHIFT                          7  /* RINSEL - [8:7] */
#define WM8737_RINSEL_WIDTH                          2  /* RINSEL - [8:7] */
#define WM8737_RMICBOOST_MASK                   0x0060  /* RMICBOOST - [6:5] */
#define WM8737_RMICBOOST_SHIFT                       5  /* RMICBOOST - [6:5] */
#define WM8737_RMICBOOST_WIDTH                       2  /* RMICBOOST - [6:5] */
#define WM8737_RMBE                             0x0010  /* RMBE */
#define WM8737_RMBE_MASK                        0x0010  /* RMBE */
#define WM8737_RMBE_SHIFT                            4  /* RMBE */
#define WM8737_RMBE_WIDTH                            1  /* RMBE */
#define WM8737_RMZC                             0x0008  /* RMZC */
#define WM8737_RMZC_MASK                        0x0008  /* RMZC */
#define WM8737_RMZC_SHIFT                            3  /* RMZC */
#define WM8737_RMZC_WIDTH                            1  /* RMZC */
#define WM8737_RPZC                             0x0004  /* RPZC */
#define WM8737_RPZC_MASK                        0x0004  /* RPZC */
#define WM8737_RPZC_SHIFT                            2  /* RPZC */
#define WM8737_RPZC_WIDTH                            1  /* RPZC */
#define WM8737_RZCTO_MASK                       0x0003  /* RZCTO - [1:0] */
#define WM8737_RZCTO_SHIFT                           0  /* RZCTO - [1:0] */
#define WM8737_RZCTO_WIDTH                           2  /* RZCTO - [1:0] */

/*
 * R4 (0x04) - 3D Enhance
 */
#define WM8737_DIV2                             0x0080  /* DIV2 */
#define WM8737_DIV2_MASK                        0x0080  /* DIV2 */
#define WM8737_DIV2_SHIFT                            7  /* DIV2 */
#define WM8737_DIV2_WIDTH                            1  /* DIV2 */
#define WM8737_3DLC                             0x0040  /* 3DLC */
#define WM8737_3DLC_MASK                        0x0040  /* 3DLC */
#define WM8737_3DLC_SHIFT                            6  /* 3DLC */
#define WM8737_3DLC_WIDTH                            1  /* 3DLC */
#define WM8737_3DUC                             0x0020  /* 3DUC */
#define WM8737_3DUC_MASK                        0x0020  /* 3DUC */
#define WM8737_3DUC_SHIFT                            5  /* 3DUC */
#define WM8737_3DUC_WIDTH                            1  /* 3DUC */
#define WM8737_3DDEPTH_MASK                     0x001E  /* 3DDEPTH - [4:1] */
#define WM8737_3DDEPTH_SHIFT                         1  /* 3DDEPTH - [4:1] */
#define WM8737_3DDEPTH_WIDTH                         4  /* 3DDEPTH - [4:1] */
#define WM8737_3DE                              0x0001  /* 3DE */
#define WM8737_3DE_MASK                         0x0001  /* 3DE */
#define WM8737_3DE_SHIFT                             0  /* 3DE */
#define WM8737_3DE_WIDTH                             1  /* 3DE */

/*
 * R5 (0x05) - ADC Control
 */
#define WM8737_MONOMIX_MASK                     0x0180  /* MONOMIX - [8:7] */
#define WM8737_MONOMIX_SHIFT                         7  /* MONOMIX - [8:7] */
#define WM8737_MONOMIX_WIDTH                         2  /* MONOMIX - [8:7] */
#define WM8737_POLARITY_MASK                    0x0060  /* POLARITY - [6:5] */
#define WM8737_POLARITY_SHIFT                        5  /* POLARITY - [6:5] */
#define WM8737_POLARITY_WIDTH                        2  /* POLARITY - [6:5] */
#define WM8737_HPOR                             0x0010  /* HPOR */
#define WM8737_HPOR_MASK                        0x0010  /* HPOR */
#define WM8737_HPOR_SHIFT                            4  /* HPOR */
#define WM8737_HPOR_WIDTH                            1  /* HPOR */
#define WM8737_LP                               0x0004  /* LP */
#define WM8737_LP_MASK                          0x0004  /* LP */
#define WM8737_LP_SHIFT                              2  /* LP */
#define WM8737_LP_WIDTH                              1  /* LP */
#define WM8737_MONOUT                           0x0002  /* MONOUT */
#define WM8737_MONOUT_MASK                      0x0002  /* MONOUT */
#define WM8737_MONOUT_SHIFT                          1  /* MONOUT */
#define WM8737_MONOUT_WIDTH                          1  /* MONOUT */
#define WM8737_ADCHPD                           0x0001  /* ADCHPD */
#define WM8737_ADCHPD_MASK                      0x0001  /* ADCHPD */
#define WM8737_ADCHPD_SHIFT                          0  /* ADCHPD */
#define WM8737_ADCHPD_WIDTH                          1  /* ADCHPD */

/*
 * R6 (0x06) - Power Management
 */
#define WM8737_VMID                             0x0100  /* VMID */
#define WM8737_VMID_MASK                        0x0100  /* VMID */
#define WM8737_VMID_SHIFT                            8  /* VMID */
#define WM8737_VMID_WIDTH                            1  /* VMID */
#define WM8737_VREF                             0x0080  /* VREF */
#define WM8737_VREF_MASK                        0x0080  /* VREF */
#define WM8737_VREF_SHIFT                            7  /* VREF */
#define WM8737_VREF_WIDTH                            1  /* VREF */
#define WM8737_AI                               0x0040  /* AI */
#define WM8737_AI_MASK                          0x0040  /* AI */
#define WM8737_AI_SHIFT                              6  /* AI */
#define WM8737_AI_WIDTH                              1  /* AI */
#define WM8737_PGL                              0x0020  /* PGL */
#define WM8737_PGL_MASK                         0x0020  /* PGL */
#define WM8737_PGL_SHIFT                             5  /* PGL */
#define WM8737_PGL_WIDTH                             1  /* PGL */
#define WM8737_PGR                              0x0010  /* PGR */
#define WM8737_PGR_MASK                         0x0010  /* PGR */
#define WM8737_PGR_SHIFT                             4  /* PGR */
#define WM8737_PGR_WIDTH                             1  /* PGR */
#define WM8737_ADL                              0x0008  /* ADL */
#define WM8737_ADL_MASK                         0x0008  /* ADL */
#define WM8737_ADL_SHIFT                             3  /* ADL */
#define WM8737_ADL_WIDTH                             1  /* ADL */
#define WM8737_ADR                              0x0004  /* ADR */
#define WM8737_ADR_MASK                         0x0004  /* ADR */
#define WM8737_ADR_SHIFT                             2  /* ADR */
#define WM8737_ADR_WIDTH                             1  /* ADR */
#define WM8737_MICBIAS_MASK                     0x0003  /* MICBIAS - [1:0] */
#define WM8737_MICBIAS_SHIFT                         0  /* MICBIAS - [1:0] */
#define WM8737_MICBIAS_WIDTH                         2  /* MICBIAS - [1:0] */

/*
 * R7 (0x07) - Audio Format
 */
#define WM8737_SDODIS                           0x0080  /* SDODIS */
#define WM8737_SDODIS_MASK                      0x0080  /* SDODIS */
#define WM8737_SDODIS_SHIFT                          7  /* SDODIS */
#define WM8737_SDODIS_WIDTH                          1  /* SDODIS */
#define WM8737_MS                               0x0040  /* MS */
#define WM8737_MS_MASK                          0x0040  /* MS */
#define WM8737_MS_SHIFT                              6  /* MS */
#define WM8737_MS_WIDTH                              1  /* MS */
#define WM8737_LRP                              0x0010  /* LRP */
#define WM8737_LRP_MASK                         0x0010  /* LRP */
#define WM8737_LRP_SHIFT                             4  /* LRP */
#define WM8737_LRP_WIDTH                             1  /* LRP */
#define WM8737_WL_MASK                          0x000C  /* WL - [3:2] */
#define WM8737_WL_SHIFT                              2  /* WL - [3:2] */
#define WM8737_WL_WIDTH                              2  /* WL - [3:2] */
#define WM8737_FORMAT_MASK                      0x0003  /* FORMAT - [1:0] */
#define WM8737_FORMAT_SHIFT                          0  /* FORMAT - [1:0] */
#define WM8737_FORMAT_WIDTH                          2  /* FORMAT - [1:0] */

/*
 * R8 (0x08) - Clocking
 */
#define WM8737_AUTODETECT                       0x0080  /* AUTODETECT */
#define WM8737_AUTODETECT_MASK                  0x0080  /* AUTODETECT */
#define WM8737_AUTODETECT_SHIFT                      7  /* AUTODETECT */
#define WM8737_AUTODETECT_WIDTH                      1  /* AUTODETECT */
#define WM8737_CLKDIV2                          0x0040  /* CLKDIV2 */
#define WM8737_CLKDIV2_MASK                     0x0040  /* CLKDIV2 */
#define WM8737_CLKDIV2_SHIFT                         6  /* CLKDIV2 */
#define WM8737_CLKDIV2_WIDTH                         1  /* CLKDIV2 */
#define WM8737_SR_MASK                          0x003E  /* SR - [5:1] */
#define WM8737_SR_SHIFT                              1  /* SR - [5:1] */
#define WM8737_SR_WIDTH                              5  /* SR - [5:1] */
#define WM8737_USB_MODE                         0x0001  /* USB MODE */
#define WM8737_USB_MODE_MASK                    0x0001  /* USB MODE */
#define WM8737_USB_MODE_SHIFT                        0  /* USB MODE */
#define WM8737_USB_MODE_WIDTH                        1  /* USB MODE */

/*
 * R9 (0x09) - MIC Preamp Control
 */
#define WM8737_RBYPEN                           0x0008  /* RBYPEN */
#define WM8737_RBYPEN_MASK                      0x0008  /* RBYPEN */
#define WM8737_RBYPEN_SHIFT                          3  /* RBYPEN */
#define WM8737_RBYPEN_WIDTH                          1  /* RBYPEN */
#define WM8737_LBYPEN                           0x0004  /* LBYPEN */
#define WM8737_LBYPEN_MASK                      0x0004  /* LBYPEN */
#define WM8737_LBYPEN_SHIFT                          2  /* LBYPEN */
#define WM8737_LBYPEN_WIDTH                          1  /* LBYPEN */
#define WM8737_MBCTRL_MASK                      0x0003  /* MBCTRL - [1:0] */
#define WM8737_MBCTRL_SHIFT                          0  /* MBCTRL - [1:0] */
#define WM8737_MBCTRL_WIDTH                          2  /* MBCTRL - [1:0] */

/*
 * R10 (0x0A) - Misc Bias Control
 */
#define WM8737_VMIDSEL_MASK                     0x000C  /* VMIDSEL - [3:2] */
#define WM8737_VMIDSEL_SHIFT                         2  /* VMIDSEL - [3:2] */
#define WM8737_VMIDSEL_WIDTH                         2  /* VMIDSEL - [3:2] */
#define WM8737_LINPUT1_DC_BIAS_ENABLE           0x0002  /* LINPUT1 DC BIAS ENABLE */
#define WM8737_LINPUT1_DC_BIAS_ENABLE_MASK      0x0002  /* LINPUT1 DC BIAS ENABLE */
#define WM8737_LINPUT1_DC_BIAS_ENABLE_SHIFT          1  /* LINPUT1 DC BIAS ENABLE */
#define WM8737_LINPUT1_DC_BIAS_ENABLE_WIDTH          1  /* LINPUT1 DC BIAS ENABLE */
#define WM8737_RINPUT1_DC_BIAS_ENABLE           0x0001  /* RINPUT1 DC BIAS ENABLE */
#define WM8737_RINPUT1_DC_BIAS_ENABLE_MASK      0x0001  /* RINPUT1 DC BIAS ENABLE */
#define WM8737_RINPUT1_DC_BIAS_ENABLE_SHIFT          0  /* RINPUT1 DC BIAS ENABLE */
#define WM8737_RINPUT1_DC_BIAS_ENABLE_WIDTH          1  /* RINPUT1 DC BIAS ENABLE */

/*
 * R11 (0x0B) - Noise Gate
 */
#define WM8737_NGTH_MASK                        0x001C  /* NGTH - [4:2] */
#define WM8737_NGTH_SHIFT                            2  /* NGTH - [4:2] */
#define WM8737_NGTH_WIDTH                            3  /* NGTH - [4:2] */
#define WM8737_NGAT                             0x0001  /* NGAT */
#define WM8737_NGAT_MASK                        0x0001  /* NGAT */
#define WM8737_NGAT_SHIFT                            0  /* NGAT */
#define WM8737_NGAT_WIDTH                            1  /* NGAT */

/*
 * R12 (0x0C) - ALC1
 */
#define WM8737_ALCSEL_MASK                      0x0180  /* ALCSEL - [8:7] */
#define WM8737_ALCSEL_SHIFT                          7  /* ALCSEL - [8:7] */
#define WM8737_ALCSEL_WIDTH                          2  /* ALCSEL - [8:7] */
#define WM8737_MAX_GAIN_MASK                    0x0070  /* MAX GAIN - [6:4] */
#define WM8737_MAX_GAIN_SHIFT                        4  /* MAX GAIN - [6:4] */
#define WM8737_MAX_GAIN_WIDTH                        3  /* MAX GAIN - [6:4] */
#define WM8737_ALCL_MASK                        0x000F  /* ALCL - [3:0] */
#define WM8737_ALCL_SHIFT                            0  /* ALCL - [3:0] */
#define WM8737_ALCL_WIDTH                            4  /* ALCL - [3:0] */

/*
 * R13 (0x0D) - ALC2
 */
#define WM8737_ALCZCE                           0x0010  /* ALCZCE */
#define WM8737_ALCZCE_MASK                      0x0010  /* ALCZCE */
#define WM8737_ALCZCE_SHIFT                          4  /* ALCZCE */
#define WM8737_ALCZCE_WIDTH                          1  /* ALCZCE */
#define WM8737_HLD_MASK                         0x000F  /* HLD - [3:0] */
#define WM8737_HLD_SHIFT                             0  /* HLD - [3:0] */
#define WM8737_HLD_WIDTH                             4  /* HLD - [3:0] */

/*
 * R14 (0x0E) - ALC3
 */
#define WM8737_DCY_MASK                         0x00F0  /* DCY - [7:4] */
#define WM8737_DCY_SHIFT                             4  /* DCY - [7:4] */
#define WM8737_DCY_WIDTH                             4  /* DCY - [7:4] */
#define WM8737_ATK_MASK                         0x000F  /* ATK - [3:0] */
#define WM8737_ATK_SHIFT                             0  /* ATK - [3:0] */
#define WM8737_ATK_WIDTH                             4  /* ATK - [3:0] */

/*
 * R15 (0x0F) - Reset
 */
#define WM8737_RESET_MASK                       0x01FF  /* RESET - [8:0] */
#define WM8737_RESET_SHIFT                           0  /* RESET - [8:0] */
#define WM8737_RESET_WIDTH                           9  /* RESET - [8:0] */

#endif
