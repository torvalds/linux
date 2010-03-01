/*
 * wm8955.h  --  WM8904 ASoC driver
 *
 * Copyright 2009 Wolfson Microelectronics, plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _WM8955_H
#define _WM8955_H

#define WM8955_CLK_MCLK 1

extern struct snd_soc_dai wm8955_dai;
extern struct snd_soc_codec_device soc_codec_dev_wm8955;

/*
 * Register values.
 */
#define WM8955_LOUT1_VOLUME                     0x02
#define WM8955_ROUT1_VOLUME                     0x03
#define WM8955_DAC_CONTROL                      0x05
#define WM8955_AUDIO_INTERFACE                  0x07
#define WM8955_SAMPLE_RATE                      0x08
#define WM8955_LEFT_DAC_VOLUME                  0x0A
#define WM8955_RIGHT_DAC_VOLUME                 0x0B
#define WM8955_BASS_CONTROL                     0x0C
#define WM8955_TREBLE_CONTROL                   0x0D
#define WM8955_RESET                            0x0F
#define WM8955_ADDITIONAL_CONTROL_1             0x17
#define WM8955_ADDITIONAL_CONTROL_2             0x18
#define WM8955_POWER_MANAGEMENT_1               0x19
#define WM8955_POWER_MANAGEMENT_2               0x1A
#define WM8955_ADDITIONAL_CONTROL_3             0x1B
#define WM8955_LEFT_OUT_MIX_1                   0x22
#define WM8955_LEFT_OUT_MIX_2                   0x23
#define WM8955_RIGHT_OUT_MIX_1                  0x24
#define WM8955_RIGHT_OUT_MIX_2                  0x25
#define WM8955_MONO_OUT_MIX_1                   0x26
#define WM8955_MONO_OUT_MIX_2                   0x27
#define WM8955_LOUT2_VOLUME                     0x28
#define WM8955_ROUT2_VOLUME                     0x29
#define WM8955_MONOOUT_VOLUME                   0x2A
#define WM8955_CLOCKING_PLL                     0x2B
#define WM8955_PLL_CONTROL_1                    0x2C
#define WM8955_PLL_CONTROL_2                    0x2D
#define WM8955_PLL_CONTROL_3                    0x2E
#define WM8955_PLL_CONTROL_4                    0x3B

#define WM8955_REGISTER_COUNT                   29
#define WM8955_MAX_REGISTER                     0x3B

/*
 * Field Definitions.
 */

/*
 * R2 (0x02) - LOUT1 volume
 */
#define WM8955_LO1VU                            0x0100  /* LO1VU */
#define WM8955_LO1VU_MASK                       0x0100  /* LO1VU */
#define WM8955_LO1VU_SHIFT                           8  /* LO1VU */
#define WM8955_LO1VU_WIDTH                           1  /* LO1VU */
#define WM8955_LO1ZC                            0x0080  /* LO1ZC */
#define WM8955_LO1ZC_MASK                       0x0080  /* LO1ZC */
#define WM8955_LO1ZC_SHIFT                           7  /* LO1ZC */
#define WM8955_LO1ZC_WIDTH                           1  /* LO1ZC */
#define WM8955_LOUTVOL_MASK                     0x007F  /* LOUTVOL - [6:0] */
#define WM8955_LOUTVOL_SHIFT                         0  /* LOUTVOL - [6:0] */
#define WM8955_LOUTVOL_WIDTH                         7  /* LOUTVOL - [6:0] */

/*
 * R3 (0x03) - ROUT1 volume
 */
#define WM8955_RO1VU                            0x0100  /* RO1VU */
#define WM8955_RO1VU_MASK                       0x0100  /* RO1VU */
#define WM8955_RO1VU_SHIFT                           8  /* RO1VU */
#define WM8955_RO1VU_WIDTH                           1  /* RO1VU */
#define WM8955_RO1ZC                            0x0080  /* RO1ZC */
#define WM8955_RO1ZC_MASK                       0x0080  /* RO1ZC */
#define WM8955_RO1ZC_SHIFT                           7  /* RO1ZC */
#define WM8955_RO1ZC_WIDTH                           1  /* RO1ZC */
#define WM8955_ROUTVOL_MASK                     0x007F  /* ROUTVOL - [6:0] */
#define WM8955_ROUTVOL_SHIFT                         0  /* ROUTVOL - [6:0] */
#define WM8955_ROUTVOL_WIDTH                         7  /* ROUTVOL - [6:0] */

/*
 * R5 (0x05) - DAC Control
 */
#define WM8955_DAT                              0x0080  /* DAT */
#define WM8955_DAT_MASK                         0x0080  /* DAT */
#define WM8955_DAT_SHIFT                             7  /* DAT */
#define WM8955_DAT_WIDTH                             1  /* DAT */
#define WM8955_DACMU                            0x0008  /* DACMU */
#define WM8955_DACMU_MASK                       0x0008  /* DACMU */
#define WM8955_DACMU_SHIFT                           3  /* DACMU */
#define WM8955_DACMU_WIDTH                           1  /* DACMU */
#define WM8955_DEEMPH_MASK                      0x0006  /* DEEMPH - [2:1] */
#define WM8955_DEEMPH_SHIFT                          1  /* DEEMPH - [2:1] */
#define WM8955_DEEMPH_WIDTH                          2  /* DEEMPH - [2:1] */

/*
 * R7 (0x07) - Audio Interface
 */
#define WM8955_BCLKINV                          0x0080  /* BCLKINV */
#define WM8955_BCLKINV_MASK                     0x0080  /* BCLKINV */
#define WM8955_BCLKINV_SHIFT                         7  /* BCLKINV */
#define WM8955_BCLKINV_WIDTH                         1  /* BCLKINV */
#define WM8955_MS                               0x0040  /* MS */
#define WM8955_MS_MASK                          0x0040  /* MS */
#define WM8955_MS_SHIFT                              6  /* MS */
#define WM8955_MS_WIDTH                              1  /* MS */
#define WM8955_LRSWAP                           0x0020  /* LRSWAP */
#define WM8955_LRSWAP_MASK                      0x0020  /* LRSWAP */
#define WM8955_LRSWAP_SHIFT                          5  /* LRSWAP */
#define WM8955_LRSWAP_WIDTH                          1  /* LRSWAP */
#define WM8955_LRP                              0x0010  /* LRP */
#define WM8955_LRP_MASK                         0x0010  /* LRP */
#define WM8955_LRP_SHIFT                             4  /* LRP */
#define WM8955_LRP_WIDTH                             1  /* LRP */
#define WM8955_WL_MASK                          0x000C  /* WL - [3:2] */
#define WM8955_WL_SHIFT                              2  /* WL - [3:2] */
#define WM8955_WL_WIDTH                              2  /* WL - [3:2] */
#define WM8955_FORMAT_MASK                      0x0003  /* FORMAT - [1:0] */
#define WM8955_FORMAT_SHIFT                          0  /* FORMAT - [1:0] */
#define WM8955_FORMAT_WIDTH                          2  /* FORMAT - [1:0] */

/*
 * R8 (0x08) - Sample Rate
 */
#define WM8955_BCLKDIV2                         0x0080  /* BCLKDIV2 */
#define WM8955_BCLKDIV2_MASK                    0x0080  /* BCLKDIV2 */
#define WM8955_BCLKDIV2_SHIFT                        7  /* BCLKDIV2 */
#define WM8955_BCLKDIV2_WIDTH                        1  /* BCLKDIV2 */
#define WM8955_MCLKDIV2                         0x0040  /* MCLKDIV2 */
#define WM8955_MCLKDIV2_MASK                    0x0040  /* MCLKDIV2 */
#define WM8955_MCLKDIV2_SHIFT                        6  /* MCLKDIV2 */
#define WM8955_MCLKDIV2_WIDTH                        1  /* MCLKDIV2 */
#define WM8955_SR_MASK                          0x003E  /* SR - [5:1] */
#define WM8955_SR_SHIFT                              1  /* SR - [5:1] */
#define WM8955_SR_WIDTH                              5  /* SR - [5:1] */
#define WM8955_USB                              0x0001  /* USB */
#define WM8955_USB_MASK                         0x0001  /* USB */
#define WM8955_USB_SHIFT                             0  /* USB */
#define WM8955_USB_WIDTH                             1  /* USB */

/*
 * R10 (0x0A) - Left DAC volume
 */
#define WM8955_LDVU                             0x0100  /* LDVU */
#define WM8955_LDVU_MASK                        0x0100  /* LDVU */
#define WM8955_LDVU_SHIFT                            8  /* LDVU */
#define WM8955_LDVU_WIDTH                            1  /* LDVU */
#define WM8955_LDACVOL_MASK                     0x00FF  /* LDACVOL - [7:0] */
#define WM8955_LDACVOL_SHIFT                         0  /* LDACVOL - [7:0] */
#define WM8955_LDACVOL_WIDTH                         8  /* LDACVOL - [7:0] */

/*
 * R11 (0x0B) - Right DAC volume
 */
#define WM8955_RDVU                             0x0100  /* RDVU */
#define WM8955_RDVU_MASK                        0x0100  /* RDVU */
#define WM8955_RDVU_SHIFT                            8  /* RDVU */
#define WM8955_RDVU_WIDTH                            1  /* RDVU */
#define WM8955_RDACVOL_MASK                     0x00FF  /* RDACVOL - [7:0] */
#define WM8955_RDACVOL_SHIFT                         0  /* RDACVOL - [7:0] */
#define WM8955_RDACVOL_WIDTH                         8  /* RDACVOL - [7:0] */

/*
 * R12 (0x0C) - Bass control
 */
#define WM8955_BB                               0x0080  /* BB */
#define WM8955_BB_MASK                          0x0080  /* BB */
#define WM8955_BB_SHIFT                              7  /* BB */
#define WM8955_BB_WIDTH                              1  /* BB */
#define WM8955_BC                               0x0040  /* BC */
#define WM8955_BC_MASK                          0x0040  /* BC */
#define WM8955_BC_SHIFT                              6  /* BC */
#define WM8955_BC_WIDTH                              1  /* BC */
#define WM8955_BASS_MASK                        0x000F  /* BASS - [3:0] */
#define WM8955_BASS_SHIFT                            0  /* BASS - [3:0] */
#define WM8955_BASS_WIDTH                            4  /* BASS - [3:0] */

/*
 * R13 (0x0D) - Treble control
 */
#define WM8955_TC                               0x0040  /* TC */
#define WM8955_TC_MASK                          0x0040  /* TC */
#define WM8955_TC_SHIFT                              6  /* TC */
#define WM8955_TC_WIDTH                              1  /* TC */
#define WM8955_TRBL_MASK                        0x000F  /* TRBL - [3:0] */
#define WM8955_TRBL_SHIFT                            0  /* TRBL - [3:0] */
#define WM8955_TRBL_WIDTH                            4  /* TRBL - [3:0] */

/*
 * R15 (0x0F) - Reset
 */
#define WM8955_RESET_MASK                       0x01FF  /* RESET - [8:0] */
#define WM8955_RESET_SHIFT                           0  /* RESET - [8:0] */
#define WM8955_RESET_WIDTH                           9  /* RESET - [8:0] */

/*
 * R23 (0x17) - Additional control (1)
 */
#define WM8955_TSDEN                            0x0100  /* TSDEN */
#define WM8955_TSDEN_MASK                       0x0100  /* TSDEN */
#define WM8955_TSDEN_SHIFT                           8  /* TSDEN */
#define WM8955_TSDEN_WIDTH                           1  /* TSDEN */
#define WM8955_VSEL_MASK                        0x00C0  /* VSEL - [7:6] */
#define WM8955_VSEL_SHIFT                            6  /* VSEL - [7:6] */
#define WM8955_VSEL_WIDTH                            2  /* VSEL - [7:6] */
#define WM8955_DMONOMIX_MASK                    0x0030  /* DMONOMIX - [5:4] */
#define WM8955_DMONOMIX_SHIFT                        4  /* DMONOMIX - [5:4] */
#define WM8955_DMONOMIX_WIDTH                        2  /* DMONOMIX - [5:4] */
#define WM8955_DACINV                           0x0002  /* DACINV */
#define WM8955_DACINV_MASK                      0x0002  /* DACINV */
#define WM8955_DACINV_SHIFT                          1  /* DACINV */
#define WM8955_DACINV_WIDTH                          1  /* DACINV */
#define WM8955_TOEN                             0x0001  /* TOEN */
#define WM8955_TOEN_MASK                        0x0001  /* TOEN */
#define WM8955_TOEN_SHIFT                            0  /* TOEN */
#define WM8955_TOEN_WIDTH                            1  /* TOEN */

/*
 * R24 (0x18) - Additional control (2)
 */
#define WM8955_OUT3SW_MASK                      0x0180  /* OUT3SW - [8:7] */
#define WM8955_OUT3SW_SHIFT                          7  /* OUT3SW - [8:7] */
#define WM8955_OUT3SW_WIDTH                          2  /* OUT3SW - [8:7] */
#define WM8955_ROUT2INV                         0x0010  /* ROUT2INV */
#define WM8955_ROUT2INV_MASK                    0x0010  /* ROUT2INV */
#define WM8955_ROUT2INV_SHIFT                        4  /* ROUT2INV */
#define WM8955_ROUT2INV_WIDTH                        1  /* ROUT2INV */
#define WM8955_DACOSR                           0x0001  /* DACOSR */
#define WM8955_DACOSR_MASK                      0x0001  /* DACOSR */
#define WM8955_DACOSR_SHIFT                          0  /* DACOSR */
#define WM8955_DACOSR_WIDTH                          1  /* DACOSR */

/*
 * R25 (0x19) - Power Management (1)
 */
#define WM8955_VMIDSEL_MASK                     0x0180  /* VMIDSEL - [8:7] */
#define WM8955_VMIDSEL_SHIFT                         7  /* VMIDSEL - [8:7] */
#define WM8955_VMIDSEL_WIDTH                         2  /* VMIDSEL - [8:7] */
#define WM8955_VREF                             0x0040  /* VREF */
#define WM8955_VREF_MASK                        0x0040  /* VREF */
#define WM8955_VREF_SHIFT                            6  /* VREF */
#define WM8955_VREF_WIDTH                            1  /* VREF */
#define WM8955_DIGENB                           0x0001  /* DIGENB */
#define WM8955_DIGENB_MASK                      0x0001  /* DIGENB */
#define WM8955_DIGENB_SHIFT                          0  /* DIGENB */
#define WM8955_DIGENB_WIDTH                          1  /* DIGENB */

/*
 * R26 (0x1A) - Power Management (2)
 */
#define WM8955_DACL                             0x0100  /* DACL */
#define WM8955_DACL_MASK                        0x0100  /* DACL */
#define WM8955_DACL_SHIFT                            8  /* DACL */
#define WM8955_DACL_WIDTH                            1  /* DACL */
#define WM8955_DACR                             0x0080  /* DACR */
#define WM8955_DACR_MASK                        0x0080  /* DACR */
#define WM8955_DACR_SHIFT                            7  /* DACR */
#define WM8955_DACR_WIDTH                            1  /* DACR */
#define WM8955_LOUT1                            0x0040  /* LOUT1 */
#define WM8955_LOUT1_MASK                       0x0040  /* LOUT1 */
#define WM8955_LOUT1_SHIFT                           6  /* LOUT1 */
#define WM8955_LOUT1_WIDTH                           1  /* LOUT1 */
#define WM8955_ROUT1                            0x0020  /* ROUT1 */
#define WM8955_ROUT1_MASK                       0x0020  /* ROUT1 */
#define WM8955_ROUT1_SHIFT                           5  /* ROUT1 */
#define WM8955_ROUT1_WIDTH                           1  /* ROUT1 */
#define WM8955_LOUT2                            0x0010  /* LOUT2 */
#define WM8955_LOUT2_MASK                       0x0010  /* LOUT2 */
#define WM8955_LOUT2_SHIFT                           4  /* LOUT2 */
#define WM8955_LOUT2_WIDTH                           1  /* LOUT2 */
#define WM8955_ROUT2                            0x0008  /* ROUT2 */
#define WM8955_ROUT2_MASK                       0x0008  /* ROUT2 */
#define WM8955_ROUT2_SHIFT                           3  /* ROUT2 */
#define WM8955_ROUT2_WIDTH                           1  /* ROUT2 */
#define WM8955_MONO                             0x0004  /* MONO */
#define WM8955_MONO_MASK                        0x0004  /* MONO */
#define WM8955_MONO_SHIFT                            2  /* MONO */
#define WM8955_MONO_WIDTH                            1  /* MONO */
#define WM8955_OUT3                             0x0002  /* OUT3 */
#define WM8955_OUT3_MASK                        0x0002  /* OUT3 */
#define WM8955_OUT3_SHIFT                            1  /* OUT3 */
#define WM8955_OUT3_WIDTH                            1  /* OUT3 */

/*
 * R27 (0x1B) - Additional Control (3)
 */
#define WM8955_VROI                             0x0040  /* VROI */
#define WM8955_VROI_MASK                        0x0040  /* VROI */
#define WM8955_VROI_SHIFT                            6  /* VROI */
#define WM8955_VROI_WIDTH                            1  /* VROI */

/*
 * R34 (0x22) - Left out Mix (1)
 */
#define WM8955_LD2LO                            0x0100  /* LD2LO */
#define WM8955_LD2LO_MASK                       0x0100  /* LD2LO */
#define WM8955_LD2LO_SHIFT                           8  /* LD2LO */
#define WM8955_LD2LO_WIDTH                           1  /* LD2LO */
#define WM8955_LI2LO                            0x0080  /* LI2LO */
#define WM8955_LI2LO_MASK                       0x0080  /* LI2LO */
#define WM8955_LI2LO_SHIFT                           7  /* LI2LO */
#define WM8955_LI2LO_WIDTH                           1  /* LI2LO */
#define WM8955_LI2LOVOL_MASK                    0x0070  /* LI2LOVOL - [6:4] */
#define WM8955_LI2LOVOL_SHIFT                        4  /* LI2LOVOL - [6:4] */
#define WM8955_LI2LOVOL_WIDTH                        3  /* LI2LOVOL - [6:4] */

/*
 * R35 (0x23) - Left out Mix (2)
 */
#define WM8955_RD2LO                            0x0100  /* RD2LO */
#define WM8955_RD2LO_MASK                       0x0100  /* RD2LO */
#define WM8955_RD2LO_SHIFT                           8  /* RD2LO */
#define WM8955_RD2LO_WIDTH                           1  /* RD2LO */
#define WM8955_RI2LO                            0x0080  /* RI2LO */
#define WM8955_RI2LO_MASK                       0x0080  /* RI2LO */
#define WM8955_RI2LO_SHIFT                           7  /* RI2LO */
#define WM8955_RI2LO_WIDTH                           1  /* RI2LO */
#define WM8955_RI2LOVOL_MASK                    0x0070  /* RI2LOVOL - [6:4] */
#define WM8955_RI2LOVOL_SHIFT                        4  /* RI2LOVOL - [6:4] */
#define WM8955_RI2LOVOL_WIDTH                        3  /* RI2LOVOL - [6:4] */

/*
 * R36 (0x24) - Right out Mix (1)
 */
#define WM8955_LD2RO                            0x0100  /* LD2RO */
#define WM8955_LD2RO_MASK                       0x0100  /* LD2RO */
#define WM8955_LD2RO_SHIFT                           8  /* LD2RO */
#define WM8955_LD2RO_WIDTH                           1  /* LD2RO */
#define WM8955_LI2RO                            0x0080  /* LI2RO */
#define WM8955_LI2RO_MASK                       0x0080  /* LI2RO */
#define WM8955_LI2RO_SHIFT                           7  /* LI2RO */
#define WM8955_LI2RO_WIDTH                           1  /* LI2RO */
#define WM8955_LI2ROVOL_MASK                    0x0070  /* LI2ROVOL - [6:4] */
#define WM8955_LI2ROVOL_SHIFT                        4  /* LI2ROVOL - [6:4] */
#define WM8955_LI2ROVOL_WIDTH                        3  /* LI2ROVOL - [6:4] */

/*
 * R37 (0x25) - Right Out Mix (2)
 */
#define WM8955_RD2RO                            0x0100  /* RD2RO */
#define WM8955_RD2RO_MASK                       0x0100  /* RD2RO */
#define WM8955_RD2RO_SHIFT                           8  /* RD2RO */
#define WM8955_RD2RO_WIDTH                           1  /* RD2RO */
#define WM8955_RI2RO                            0x0080  /* RI2RO */
#define WM8955_RI2RO_MASK                       0x0080  /* RI2RO */
#define WM8955_RI2RO_SHIFT                           7  /* RI2RO */
#define WM8955_RI2RO_WIDTH                           1  /* RI2RO */
#define WM8955_RI2ROVOL_MASK                    0x0070  /* RI2ROVOL - [6:4] */
#define WM8955_RI2ROVOL_SHIFT                        4  /* RI2ROVOL - [6:4] */
#define WM8955_RI2ROVOL_WIDTH                        3  /* RI2ROVOL - [6:4] */

/*
 * R38 (0x26) - Mono out Mix (1)
 */
#define WM8955_LD2MO                            0x0100  /* LD2MO */
#define WM8955_LD2MO_MASK                       0x0100  /* LD2MO */
#define WM8955_LD2MO_SHIFT                           8  /* LD2MO */
#define WM8955_LD2MO_WIDTH                           1  /* LD2MO */
#define WM8955_LI2MO                            0x0080  /* LI2MO */
#define WM8955_LI2MO_MASK                       0x0080  /* LI2MO */
#define WM8955_LI2MO_SHIFT                           7  /* LI2MO */
#define WM8955_LI2MO_WIDTH                           1  /* LI2MO */
#define WM8955_LI2MOVOL_MASK                    0x0070  /* LI2MOVOL - [6:4] */
#define WM8955_LI2MOVOL_SHIFT                        4  /* LI2MOVOL - [6:4] */
#define WM8955_LI2MOVOL_WIDTH                        3  /* LI2MOVOL - [6:4] */
#define WM8955_DMEN                             0x0001  /* DMEN */
#define WM8955_DMEN_MASK                        0x0001  /* DMEN */
#define WM8955_DMEN_SHIFT                            0  /* DMEN */
#define WM8955_DMEN_WIDTH                            1  /* DMEN */

/*
 * R39 (0x27) - Mono out Mix (2)
 */
#define WM8955_RD2MO                            0x0100  /* RD2MO */
#define WM8955_RD2MO_MASK                       0x0100  /* RD2MO */
#define WM8955_RD2MO_SHIFT                           8  /* RD2MO */
#define WM8955_RD2MO_WIDTH                           1  /* RD2MO */
#define WM8955_RI2MO                            0x0080  /* RI2MO */
#define WM8955_RI2MO_MASK                       0x0080  /* RI2MO */
#define WM8955_RI2MO_SHIFT                           7  /* RI2MO */
#define WM8955_RI2MO_WIDTH                           1  /* RI2MO */
#define WM8955_RI2MOVOL_MASK                    0x0070  /* RI2MOVOL - [6:4] */
#define WM8955_RI2MOVOL_SHIFT                        4  /* RI2MOVOL - [6:4] */
#define WM8955_RI2MOVOL_WIDTH                        3  /* RI2MOVOL - [6:4] */

/*
 * R40 (0x28) - LOUT2 volume
 */
#define WM8955_LO2VU                            0x0100  /* LO2VU */
#define WM8955_LO2VU_MASK                       0x0100  /* LO2VU */
#define WM8955_LO2VU_SHIFT                           8  /* LO2VU */
#define WM8955_LO2VU_WIDTH                           1  /* LO2VU */
#define WM8955_LO2ZC                            0x0080  /* LO2ZC */
#define WM8955_LO2ZC_MASK                       0x0080  /* LO2ZC */
#define WM8955_LO2ZC_SHIFT                           7  /* LO2ZC */
#define WM8955_LO2ZC_WIDTH                           1  /* LO2ZC */
#define WM8955_LOUT2VOL_MASK                    0x007F  /* LOUT2VOL - [6:0] */
#define WM8955_LOUT2VOL_SHIFT                        0  /* LOUT2VOL - [6:0] */
#define WM8955_LOUT2VOL_WIDTH                        7  /* LOUT2VOL - [6:0] */

/*
 * R41 (0x29) - ROUT2 volume
 */
#define WM8955_RO2VU                            0x0100  /* RO2VU */
#define WM8955_RO2VU_MASK                       0x0100  /* RO2VU */
#define WM8955_RO2VU_SHIFT                           8  /* RO2VU */
#define WM8955_RO2VU_WIDTH                           1  /* RO2VU */
#define WM8955_RO2ZC                            0x0080  /* RO2ZC */
#define WM8955_RO2ZC_MASK                       0x0080  /* RO2ZC */
#define WM8955_RO2ZC_SHIFT                           7  /* RO2ZC */
#define WM8955_RO2ZC_WIDTH                           1  /* RO2ZC */
#define WM8955_ROUT2VOL_MASK                    0x007F  /* ROUT2VOL - [6:0] */
#define WM8955_ROUT2VOL_SHIFT                        0  /* ROUT2VOL - [6:0] */
#define WM8955_ROUT2VOL_WIDTH                        7  /* ROUT2VOL - [6:0] */

/*
 * R42 (0x2A) - MONOOUT volume
 */
#define WM8955_MOZC                             0x0080  /* MOZC */
#define WM8955_MOZC_MASK                        0x0080  /* MOZC */
#define WM8955_MOZC_SHIFT                            7  /* MOZC */
#define WM8955_MOZC_WIDTH                            1  /* MOZC */
#define WM8955_MOUTVOL_MASK                     0x007F  /* MOUTVOL - [6:0] */
#define WM8955_MOUTVOL_SHIFT                         0  /* MOUTVOL - [6:0] */
#define WM8955_MOUTVOL_WIDTH                         7  /* MOUTVOL - [6:0] */

/*
 * R43 (0x2B) - Clocking / PLL
 */
#define WM8955_MCLKSEL                          0x0100  /* MCLKSEL */
#define WM8955_MCLKSEL_MASK                     0x0100  /* MCLKSEL */
#define WM8955_MCLKSEL_SHIFT                         8  /* MCLKSEL */
#define WM8955_MCLKSEL_WIDTH                         1  /* MCLKSEL */
#define WM8955_PLLOUTDIV2                       0x0020  /* PLLOUTDIV2 */
#define WM8955_PLLOUTDIV2_MASK                  0x0020  /* PLLOUTDIV2 */
#define WM8955_PLLOUTDIV2_SHIFT                      5  /* PLLOUTDIV2 */
#define WM8955_PLLOUTDIV2_WIDTH                      1  /* PLLOUTDIV2 */
#define WM8955_PLL_RB                           0x0010  /* PLL_RB */
#define WM8955_PLL_RB_MASK                      0x0010  /* PLL_RB */
#define WM8955_PLL_RB_SHIFT                          4  /* PLL_RB */
#define WM8955_PLL_RB_WIDTH                          1  /* PLL_RB */
#define WM8955_PLLEN                            0x0008  /* PLLEN */
#define WM8955_PLLEN_MASK                       0x0008  /* PLLEN */
#define WM8955_PLLEN_SHIFT                           3  /* PLLEN */
#define WM8955_PLLEN_WIDTH                           1  /* PLLEN */

/*
 * R44 (0x2C) - PLL Control 1
 */
#define WM8955_N_MASK                           0x01E0  /* N - [8:5] */
#define WM8955_N_SHIFT                               5  /* N - [8:5] */
#define WM8955_N_WIDTH                               4  /* N - [8:5] */
#define WM8955_K_21_18_MASK                     0x000F  /* K(21:18) - [3:0] */
#define WM8955_K_21_18_SHIFT                         0  /* K(21:18) - [3:0] */
#define WM8955_K_21_18_WIDTH                         4  /* K(21:18) - [3:0] */

/*
 * R45 (0x2D) - PLL Control 2
 */
#define WM8955_K_17_9_MASK                      0x01FF  /* K(17:9) - [8:0] */
#define WM8955_K_17_9_SHIFT                          0  /* K(17:9) - [8:0] */
#define WM8955_K_17_9_WIDTH                          9  /* K(17:9) - [8:0] */

/*
 * R46 (0x2E) - PLL Control 3
 */
#define WM8955_K_8_0_MASK                       0x01FF  /* K(8:0) - [8:0] */
#define WM8955_K_8_0_SHIFT                           0  /* K(8:0) - [8:0] */
#define WM8955_K_8_0_WIDTH                           9  /* K(8:0) - [8:0] */

/*
 * R59 (0x3B) - PLL Control 4
 */
#define WM8955_KEN                              0x0080  /* KEN */
#define WM8955_KEN_MASK                         0x0080  /* KEN */
#define WM8955_KEN_SHIFT                             7  /* KEN */
#define WM8955_KEN_WIDTH                             1  /* KEN */

#endif
