/*
 * include/linux/mfd/wm831x/auxadc.h -- Auxiliary ADC interface for WM831x
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

#ifndef __MFD_WM831X_AUXADC_H__
#define __MFD_WM831X_AUXADC_H__

struct wm831x;

/*
 * R16429 (0x402D) - AuxADC Data
 */
#define WM831X_AUX_DATA_SRC_MASK                0xF000  /* AUX_DATA_SRC - [15:12] */
#define WM831X_AUX_DATA_SRC_SHIFT                   12  /* AUX_DATA_SRC - [15:12] */
#define WM831X_AUX_DATA_SRC_WIDTH                    4  /* AUX_DATA_SRC - [15:12] */
#define WM831X_AUX_DATA_MASK                    0x0FFF  /* AUX_DATA - [11:0] */
#define WM831X_AUX_DATA_SHIFT                        0  /* AUX_DATA - [11:0] */
#define WM831X_AUX_DATA_WIDTH                       12  /* AUX_DATA - [11:0] */

/*
 * R16430 (0x402E) - AuxADC Control
 */
#define WM831X_AUX_ENA                          0x8000  /* AUX_ENA */
#define WM831X_AUX_ENA_MASK                     0x8000  /* AUX_ENA */
#define WM831X_AUX_ENA_SHIFT                        15  /* AUX_ENA */
#define WM831X_AUX_ENA_WIDTH                         1  /* AUX_ENA */
#define WM831X_AUX_CVT_ENA                      0x4000  /* AUX_CVT_ENA */
#define WM831X_AUX_CVT_ENA_MASK                 0x4000  /* AUX_CVT_ENA */
#define WM831X_AUX_CVT_ENA_SHIFT                    14  /* AUX_CVT_ENA */
#define WM831X_AUX_CVT_ENA_WIDTH                     1  /* AUX_CVT_ENA */
#define WM831X_AUX_SLPENA                       0x1000  /* AUX_SLPENA */
#define WM831X_AUX_SLPENA_MASK                  0x1000  /* AUX_SLPENA */
#define WM831X_AUX_SLPENA_SHIFT                     12  /* AUX_SLPENA */
#define WM831X_AUX_SLPENA_WIDTH                      1  /* AUX_SLPENA */
#define WM831X_AUX_FRC_ENA                      0x0800  /* AUX_FRC_ENA */
#define WM831X_AUX_FRC_ENA_MASK                 0x0800  /* AUX_FRC_ENA */
#define WM831X_AUX_FRC_ENA_SHIFT                    11  /* AUX_FRC_ENA */
#define WM831X_AUX_FRC_ENA_WIDTH                     1  /* AUX_FRC_ENA */
#define WM831X_AUX_RATE_MASK                    0x003F  /* AUX_RATE - [5:0] */
#define WM831X_AUX_RATE_SHIFT                        0  /* AUX_RATE - [5:0] */
#define WM831X_AUX_RATE_WIDTH                        6  /* AUX_RATE - [5:0] */

/*
 * R16431 (0x402F) - AuxADC Source
 */
#define WM831X_AUX_CAL_SEL                      0x8000  /* AUX_CAL_SEL */
#define WM831X_AUX_CAL_SEL_MASK                 0x8000  /* AUX_CAL_SEL */
#define WM831X_AUX_CAL_SEL_SHIFT                    15  /* AUX_CAL_SEL */
#define WM831X_AUX_CAL_SEL_WIDTH                     1  /* AUX_CAL_SEL */
#define WM831X_AUX_BKUP_BATT_SEL                0x0400  /* AUX_BKUP_BATT_SEL */
#define WM831X_AUX_BKUP_BATT_SEL_MASK           0x0400  /* AUX_BKUP_BATT_SEL */
#define WM831X_AUX_BKUP_BATT_SEL_SHIFT              10  /* AUX_BKUP_BATT_SEL */
#define WM831X_AUX_BKUP_BATT_SEL_WIDTH               1  /* AUX_BKUP_BATT_SEL */
#define WM831X_AUX_WALL_SEL                     0x0200  /* AUX_WALL_SEL */
#define WM831X_AUX_WALL_SEL_MASK                0x0200  /* AUX_WALL_SEL */
#define WM831X_AUX_WALL_SEL_SHIFT                    9  /* AUX_WALL_SEL */
#define WM831X_AUX_WALL_SEL_WIDTH                    1  /* AUX_WALL_SEL */
#define WM831X_AUX_BATT_SEL                     0x0100  /* AUX_BATT_SEL */
#define WM831X_AUX_BATT_SEL_MASK                0x0100  /* AUX_BATT_SEL */
#define WM831X_AUX_BATT_SEL_SHIFT                    8  /* AUX_BATT_SEL */
#define WM831X_AUX_BATT_SEL_WIDTH                    1  /* AUX_BATT_SEL */
#define WM831X_AUX_USB_SEL                      0x0080  /* AUX_USB_SEL */
#define WM831X_AUX_USB_SEL_MASK                 0x0080  /* AUX_USB_SEL */
#define WM831X_AUX_USB_SEL_SHIFT                     7  /* AUX_USB_SEL */
#define WM831X_AUX_USB_SEL_WIDTH                     1  /* AUX_USB_SEL */
#define WM831X_AUX_SYSVDD_SEL                   0x0040  /* AUX_SYSVDD_SEL */
#define WM831X_AUX_SYSVDD_SEL_MASK              0x0040  /* AUX_SYSVDD_SEL */
#define WM831X_AUX_SYSVDD_SEL_SHIFT                  6  /* AUX_SYSVDD_SEL */
#define WM831X_AUX_SYSVDD_SEL_WIDTH                  1  /* AUX_SYSVDD_SEL */
#define WM831X_AUX_BATT_TEMP_SEL                0x0020  /* AUX_BATT_TEMP_SEL */
#define WM831X_AUX_BATT_TEMP_SEL_MASK           0x0020  /* AUX_BATT_TEMP_SEL */
#define WM831X_AUX_BATT_TEMP_SEL_SHIFT               5  /* AUX_BATT_TEMP_SEL */
#define WM831X_AUX_BATT_TEMP_SEL_WIDTH               1  /* AUX_BATT_TEMP_SEL */
#define WM831X_AUX_CHIP_TEMP_SEL                0x0010  /* AUX_CHIP_TEMP_SEL */
#define WM831X_AUX_CHIP_TEMP_SEL_MASK           0x0010  /* AUX_CHIP_TEMP_SEL */
#define WM831X_AUX_CHIP_TEMP_SEL_SHIFT               4  /* AUX_CHIP_TEMP_SEL */
#define WM831X_AUX_CHIP_TEMP_SEL_WIDTH               1  /* AUX_CHIP_TEMP_SEL */
#define WM831X_AUX_AUX4_SEL                     0x0008  /* AUX_AUX4_SEL */
#define WM831X_AUX_AUX4_SEL_MASK                0x0008  /* AUX_AUX4_SEL */
#define WM831X_AUX_AUX4_SEL_SHIFT                    3  /* AUX_AUX4_SEL */
#define WM831X_AUX_AUX4_SEL_WIDTH                    1  /* AUX_AUX4_SEL */
#define WM831X_AUX_AUX3_SEL                     0x0004  /* AUX_AUX3_SEL */
#define WM831X_AUX_AUX3_SEL_MASK                0x0004  /* AUX_AUX3_SEL */
#define WM831X_AUX_AUX3_SEL_SHIFT                    2  /* AUX_AUX3_SEL */
#define WM831X_AUX_AUX3_SEL_WIDTH                    1  /* AUX_AUX3_SEL */
#define WM831X_AUX_AUX2_SEL                     0x0002  /* AUX_AUX2_SEL */
#define WM831X_AUX_AUX2_SEL_MASK                0x0002  /* AUX_AUX2_SEL */
#define WM831X_AUX_AUX2_SEL_SHIFT                    1  /* AUX_AUX2_SEL */
#define WM831X_AUX_AUX2_SEL_WIDTH                    1  /* AUX_AUX2_SEL */
#define WM831X_AUX_AUX1_SEL                     0x0001  /* AUX_AUX1_SEL */
#define WM831X_AUX_AUX1_SEL_MASK                0x0001  /* AUX_AUX1_SEL */
#define WM831X_AUX_AUX1_SEL_SHIFT                    0  /* AUX_AUX1_SEL */
#define WM831X_AUX_AUX1_SEL_WIDTH                    1  /* AUX_AUX1_SEL */

/*
 * R16432 (0x4030) - Comparator Control
 */
#define WM831X_DCOMP4_STS                       0x0800  /* DCOMP4_STS */
#define WM831X_DCOMP4_STS_MASK                  0x0800  /* DCOMP4_STS */
#define WM831X_DCOMP4_STS_SHIFT                     11  /* DCOMP4_STS */
#define WM831X_DCOMP4_STS_WIDTH                      1  /* DCOMP4_STS */
#define WM831X_DCOMP3_STS                       0x0400  /* DCOMP3_STS */
#define WM831X_DCOMP3_STS_MASK                  0x0400  /* DCOMP3_STS */
#define WM831X_DCOMP3_STS_SHIFT                     10  /* DCOMP3_STS */
#define WM831X_DCOMP3_STS_WIDTH                      1  /* DCOMP3_STS */
#define WM831X_DCOMP2_STS                       0x0200  /* DCOMP2_STS */
#define WM831X_DCOMP2_STS_MASK                  0x0200  /* DCOMP2_STS */
#define WM831X_DCOMP2_STS_SHIFT                      9  /* DCOMP2_STS */
#define WM831X_DCOMP2_STS_WIDTH                      1  /* DCOMP2_STS */
#define WM831X_DCOMP1_STS                       0x0100  /* DCOMP1_STS */
#define WM831X_DCOMP1_STS_MASK                  0x0100  /* DCOMP1_STS */
#define WM831X_DCOMP1_STS_SHIFT                      8  /* DCOMP1_STS */
#define WM831X_DCOMP1_STS_WIDTH                      1  /* DCOMP1_STS */
#define WM831X_DCMP4_ENA                        0x0008  /* DCMP4_ENA */
#define WM831X_DCMP4_ENA_MASK                   0x0008  /* DCMP4_ENA */
#define WM831X_DCMP4_ENA_SHIFT                       3  /* DCMP4_ENA */
#define WM831X_DCMP4_ENA_WIDTH                       1  /* DCMP4_ENA */
#define WM831X_DCMP3_ENA                        0x0004  /* DCMP3_ENA */
#define WM831X_DCMP3_ENA_MASK                   0x0004  /* DCMP3_ENA */
#define WM831X_DCMP3_ENA_SHIFT                       2  /* DCMP3_ENA */
#define WM831X_DCMP3_ENA_WIDTH                       1  /* DCMP3_ENA */
#define WM831X_DCMP2_ENA                        0x0002  /* DCMP2_ENA */
#define WM831X_DCMP2_ENA_MASK                   0x0002  /* DCMP2_ENA */
#define WM831X_DCMP2_ENA_SHIFT                       1  /* DCMP2_ENA */
#define WM831X_DCMP2_ENA_WIDTH                       1  /* DCMP2_ENA */
#define WM831X_DCMP1_ENA                        0x0001  /* DCMP1_ENA */
#define WM831X_DCMP1_ENA_MASK                   0x0001  /* DCMP1_ENA */
#define WM831X_DCMP1_ENA_SHIFT                       0  /* DCMP1_ENA */
#define WM831X_DCMP1_ENA_WIDTH                       1  /* DCMP1_ENA */

/*
 * R16433 (0x4031) - Comparator 1
 */
#define WM831X_DCMP1_SRC_MASK                   0xE000  /* DCMP1_SRC - [15:13] */
#define WM831X_DCMP1_SRC_SHIFT                      13  /* DCMP1_SRC - [15:13] */
#define WM831X_DCMP1_SRC_WIDTH                       3  /* DCMP1_SRC - [15:13] */
#define WM831X_DCMP1_GT                         0x1000  /* DCMP1_GT */
#define WM831X_DCMP1_GT_MASK                    0x1000  /* DCMP1_GT */
#define WM831X_DCMP1_GT_SHIFT                       12  /* DCMP1_GT */
#define WM831X_DCMP1_GT_WIDTH                        1  /* DCMP1_GT */
#define WM831X_DCMP1_THR_MASK                   0x0FFF  /* DCMP1_THR - [11:0] */
#define WM831X_DCMP1_THR_SHIFT                       0  /* DCMP1_THR - [11:0] */
#define WM831X_DCMP1_THR_WIDTH                      12  /* DCMP1_THR - [11:0] */

/*
 * R16434 (0x4032) - Comparator 2
 */
#define WM831X_DCMP2_SRC_MASK                   0xE000  /* DCMP2_SRC - [15:13] */
#define WM831X_DCMP2_SRC_SHIFT                      13  /* DCMP2_SRC - [15:13] */
#define WM831X_DCMP2_SRC_WIDTH                       3  /* DCMP2_SRC - [15:13] */
#define WM831X_DCMP2_GT                         0x1000  /* DCMP2_GT */
#define WM831X_DCMP2_GT_MASK                    0x1000  /* DCMP2_GT */
#define WM831X_DCMP2_GT_SHIFT                       12  /* DCMP2_GT */
#define WM831X_DCMP2_GT_WIDTH                        1  /* DCMP2_GT */
#define WM831X_DCMP2_THR_MASK                   0x0FFF  /* DCMP2_THR - [11:0] */
#define WM831X_DCMP2_THR_SHIFT                       0  /* DCMP2_THR - [11:0] */
#define WM831X_DCMP2_THR_WIDTH                      12  /* DCMP2_THR - [11:0] */

/*
 * R16435 (0x4033) - Comparator 3
 */
#define WM831X_DCMP3_SRC_MASK                   0xE000  /* DCMP3_SRC - [15:13] */
#define WM831X_DCMP3_SRC_SHIFT                      13  /* DCMP3_SRC - [15:13] */
#define WM831X_DCMP3_SRC_WIDTH                       3  /* DCMP3_SRC - [15:13] */
#define WM831X_DCMP3_GT                         0x1000  /* DCMP3_GT */
#define WM831X_DCMP3_GT_MASK                    0x1000  /* DCMP3_GT */
#define WM831X_DCMP3_GT_SHIFT                       12  /* DCMP3_GT */
#define WM831X_DCMP3_GT_WIDTH                        1  /* DCMP3_GT */
#define WM831X_DCMP3_THR_MASK                   0x0FFF  /* DCMP3_THR - [11:0] */
#define WM831X_DCMP3_THR_SHIFT                       0  /* DCMP3_THR - [11:0] */
#define WM831X_DCMP3_THR_WIDTH                      12  /* DCMP3_THR - [11:0] */

/*
 * R16436 (0x4034) - Comparator 4
 */
#define WM831X_DCMP4_SRC_MASK                   0xE000  /* DCMP4_SRC - [15:13] */
#define WM831X_DCMP4_SRC_SHIFT                      13  /* DCMP4_SRC - [15:13] */
#define WM831X_DCMP4_SRC_WIDTH                       3  /* DCMP4_SRC - [15:13] */
#define WM831X_DCMP4_GT                         0x1000  /* DCMP4_GT */
#define WM831X_DCMP4_GT_MASK                    0x1000  /* DCMP4_GT */
#define WM831X_DCMP4_GT_SHIFT                       12  /* DCMP4_GT */
#define WM831X_DCMP4_GT_WIDTH                        1  /* DCMP4_GT */
#define WM831X_DCMP4_THR_MASK                   0x0FFF  /* DCMP4_THR - [11:0] */
#define WM831X_DCMP4_THR_SHIFT                       0  /* DCMP4_THR - [11:0] */
#define WM831X_DCMP4_THR_WIDTH                      12  /* DCMP4_THR - [11:0] */

#define WM831X_AUX_CAL_FACTOR  0xfff
#define WM831X_AUX_CAL_NOMINAL 0x222

enum wm831x_auxadc {
	WM831X_AUX_CAL = 15,
	WM831X_AUX_BKUP_BATT = 10,
	WM831X_AUX_WALL = 9,
	WM831X_AUX_BATT = 8,
	WM831X_AUX_USB = 7,
	WM831X_AUX_SYSVDD = 6,
	WM831X_AUX_BATT_TEMP = 5,
	WM831X_AUX_CHIP_TEMP = 4,
	WM831X_AUX_AUX4 = 3,
	WM831X_AUX_AUX3 = 2,
	WM831X_AUX_AUX2 = 1,
	WM831X_AUX_AUX1 = 0,
};

int wm831x_auxadc_read(struct wm831x *wm831x, enum wm831x_auxadc input);
int wm831x_auxadc_read_uv(struct wm831x *wm831x, enum wm831x_auxadc input);

#endif
