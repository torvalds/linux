/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * wm8523.h  --  WM8423 ASoC driver
 *
 * Copyright 2009 Wolfson Microelectronics, plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * Based on wm8753.h
 */

#ifndef _WM8523_H
#define _WM8523_H

/*
 * Register values.
 */
#define WM8523_DEVICE_ID                        0x00
#define WM8523_REVISION                         0x01
#define WM8523_PSCTRL1                          0x02
#define WM8523_AIF_CTRL1                        0x03
#define WM8523_AIF_CTRL2                        0x04
#define WM8523_DAC_CTRL3                        0x05
#define WM8523_DAC_GAINL                        0x06
#define WM8523_DAC_GAINR                        0x07
#define WM8523_ZERO_DETECT                      0x08

#define WM8523_REGISTER_COUNT                   9
#define WM8523_MAX_REGISTER                     0x08

/*
 * Field Definitions.
 */

/*
 * R0 (0x00) - DEVICE_ID
 */
#define WM8523_CHIP_ID_MASK                     0xFFFF  /* CHIP_ID - [15:0] */
#define WM8523_CHIP_ID_SHIFT                         0  /* CHIP_ID - [15:0] */
#define WM8523_CHIP_ID_WIDTH                        16  /* CHIP_ID - [15:0] */

/*
 * R1 (0x01) - REVISION
 */
#define WM8523_CHIP_REV_MASK                    0x0007  /* CHIP_REV - [2:0] */
#define WM8523_CHIP_REV_SHIFT                        0  /* CHIP_REV - [2:0] */
#define WM8523_CHIP_REV_WIDTH                        3  /* CHIP_REV - [2:0] */

/*
 * R2 (0x02) - PSCTRL1
 */
#define WM8523_SYS_ENA_MASK                     0x0003  /* SYS_ENA - [1:0] */
#define WM8523_SYS_ENA_SHIFT                         0  /* SYS_ENA - [1:0] */
#define WM8523_SYS_ENA_WIDTH                         2  /* SYS_ENA - [1:0] */

/*
 * R3 (0x03) - AIF_CTRL1
 */
#define WM8523_TDM_MODE_MASK                    0x1800  /* TDM_MODE - [12:11] */
#define WM8523_TDM_MODE_SHIFT                       11  /* TDM_MODE - [12:11] */
#define WM8523_TDM_MODE_WIDTH                        2  /* TDM_MODE - [12:11] */
#define WM8523_TDM_SLOT_MASK                    0x0600  /* TDM_SLOT - [10:9] */
#define WM8523_TDM_SLOT_SHIFT                        9  /* TDM_SLOT - [10:9] */
#define WM8523_TDM_SLOT_WIDTH                        2  /* TDM_SLOT - [10:9] */
#define WM8523_DEEMPH                           0x0100  /* DEEMPH  */
#define WM8523_DEEMPH_MASK                      0x0100  /* DEEMPH  */
#define WM8523_DEEMPH_SHIFT                          8  /* DEEMPH  */
#define WM8523_DEEMPH_WIDTH                          1  /* DEEMPH  */
#define WM8523_AIF_MSTR                         0x0080  /* AIF_MSTR  */
#define WM8523_AIF_MSTR_MASK                    0x0080  /* AIF_MSTR  */
#define WM8523_AIF_MSTR_SHIFT                        7  /* AIF_MSTR  */
#define WM8523_AIF_MSTR_WIDTH                        1  /* AIF_MSTR  */
#define WM8523_LRCLK_INV                        0x0040  /* LRCLK_INV  */
#define WM8523_LRCLK_INV_MASK                   0x0040  /* LRCLK_INV  */
#define WM8523_LRCLK_INV_SHIFT                       6  /* LRCLK_INV  */
#define WM8523_LRCLK_INV_WIDTH                       1  /* LRCLK_INV  */
#define WM8523_BCLK_INV                         0x0020  /* BCLK_INV  */
#define WM8523_BCLK_INV_MASK                    0x0020  /* BCLK_INV  */
#define WM8523_BCLK_INV_SHIFT                        5  /* BCLK_INV  */
#define WM8523_BCLK_INV_WIDTH                        1  /* BCLK_INV  */
#define WM8523_WL_MASK                          0x0018  /* WL - [4:3] */
#define WM8523_WL_SHIFT                              3  /* WL - [4:3] */
#define WM8523_WL_WIDTH                              2  /* WL - [4:3] */
#define WM8523_FMT_MASK                         0x0007  /* FMT - [2:0] */
#define WM8523_FMT_SHIFT                             0  /* FMT - [2:0] */
#define WM8523_FMT_WIDTH                             3  /* FMT - [2:0] */

/*
 * R4 (0x04) - AIF_CTRL2
 */
#define WM8523_DAC_OP_MUX_MASK                  0x00C0  /* DAC_OP_MUX - [7:6] */
#define WM8523_DAC_OP_MUX_SHIFT                      6  /* DAC_OP_MUX - [7:6] */
#define WM8523_DAC_OP_MUX_WIDTH                      2  /* DAC_OP_MUX - [7:6] */
#define WM8523_BCLKDIV_MASK                     0x0038  /* BCLKDIV - [5:3] */
#define WM8523_BCLKDIV_SHIFT                         3  /* BCLKDIV - [5:3] */
#define WM8523_BCLKDIV_WIDTH                         3  /* BCLKDIV - [5:3] */
#define WM8523_SR_MASK                          0x0007  /* SR - [2:0] */
#define WM8523_SR_SHIFT                              0  /* SR - [2:0] */
#define WM8523_SR_WIDTH                              3  /* SR - [2:0] */

/*
 * R5 (0x05) - DAC_CTRL3
 */
#define WM8523_ZC                               0x0010  /* ZC  */
#define WM8523_ZC_MASK                          0x0010  /* ZC  */
#define WM8523_ZC_SHIFT                              4  /* ZC  */
#define WM8523_ZC_WIDTH                              1  /* ZC  */
#define WM8523_DACR                             0x0008  /* DACR  */
#define WM8523_DACR_MASK                        0x0008  /* DACR  */
#define WM8523_DACR_SHIFT                            3  /* DACR  */
#define WM8523_DACR_WIDTH                            1  /* DACR  */
#define WM8523_DACL                             0x0004  /* DACL  */
#define WM8523_DACL_MASK                        0x0004  /* DACL  */
#define WM8523_DACL_SHIFT                            2  /* DACL  */
#define WM8523_DACL_WIDTH                            1  /* DACL  */
#define WM8523_VOL_UP_RAMP                      0x0002  /* VOL_UP_RAMP  */
#define WM8523_VOL_UP_RAMP_MASK                 0x0002  /* VOL_UP_RAMP  */
#define WM8523_VOL_UP_RAMP_SHIFT                     1  /* VOL_UP_RAMP  */
#define WM8523_VOL_UP_RAMP_WIDTH                     1  /* VOL_UP_RAMP  */
#define WM8523_VOL_DOWN_RAMP                    0x0001  /* VOL_DOWN_RAMP  */
#define WM8523_VOL_DOWN_RAMP_MASK               0x0001  /* VOL_DOWN_RAMP  */
#define WM8523_VOL_DOWN_RAMP_SHIFT                   0  /* VOL_DOWN_RAMP  */
#define WM8523_VOL_DOWN_RAMP_WIDTH                   1  /* VOL_DOWN_RAMP  */

/*
 * R6 (0x06) - DAC_GAINL
 */
#define WM8523_DACL_VU                          0x0200  /* DACL_VU  */
#define WM8523_DACL_VU_MASK                     0x0200  /* DACL_VU  */
#define WM8523_DACL_VU_SHIFT                         9  /* DACL_VU  */
#define WM8523_DACL_VU_WIDTH                         1  /* DACL_VU  */
#define WM8523_DACL_VOL_MASK                    0x01FF  /* DACL_VOL - [8:0] */
#define WM8523_DACL_VOL_SHIFT                        0  /* DACL_VOL - [8:0] */
#define WM8523_DACL_VOL_WIDTH                        9  /* DACL_VOL - [8:0] */

/*
 * R7 (0x07) - DAC_GAINR
 */
#define WM8523_DACR_VU                          0x0200  /* DACR_VU  */
#define WM8523_DACR_VU_MASK                     0x0200  /* DACR_VU  */
#define WM8523_DACR_VU_SHIFT                         9  /* DACR_VU  */
#define WM8523_DACR_VU_WIDTH                         1  /* DACR_VU  */
#define WM8523_DACR_VOL_MASK                    0x01FF  /* DACR_VOL - [8:0] */
#define WM8523_DACR_VOL_SHIFT                        0  /* DACR_VOL - [8:0] */
#define WM8523_DACR_VOL_WIDTH                        9  /* DACR_VOL - [8:0] */

/*
 * R8 (0x08) - ZERO_DETECT
 */
#define WM8523_ZD_COUNT_MASK                    0x0003  /* ZD_COUNT - [1:0] */
#define WM8523_ZD_COUNT_SHIFT                        0  /* ZD_COUNT - [1:0] */
#define WM8523_ZD_COUNT_WIDTH                        2  /* ZD_COUNT - [1:0] */

#endif
