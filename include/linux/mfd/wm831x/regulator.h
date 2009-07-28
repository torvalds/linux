/*
 * linux/mfd/wm831x/regulator.h -- Regulator definitons for wm831x
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

#ifndef __MFD_WM831X_REGULATOR_H__
#define __MFD_WM831X_REGULATOR_H__

/*
 * R16462 (0x404E) - Current Sink 1
 */
#define WM831X_CS1_ENA                          0x8000  /* CS1_ENA */
#define WM831X_CS1_ENA_MASK                     0x8000  /* CS1_ENA */
#define WM831X_CS1_ENA_SHIFT                        15  /* CS1_ENA */
#define WM831X_CS1_ENA_WIDTH                         1  /* CS1_ENA */
#define WM831X_CS1_DRIVE                        0x4000  /* CS1_DRIVE */
#define WM831X_CS1_DRIVE_MASK                   0x4000  /* CS1_DRIVE */
#define WM831X_CS1_DRIVE_SHIFT                      14  /* CS1_DRIVE */
#define WM831X_CS1_DRIVE_WIDTH                       1  /* CS1_DRIVE */
#define WM831X_CS1_SLPENA                       0x1000  /* CS1_SLPENA */
#define WM831X_CS1_SLPENA_MASK                  0x1000  /* CS1_SLPENA */
#define WM831X_CS1_SLPENA_SHIFT                     12  /* CS1_SLPENA */
#define WM831X_CS1_SLPENA_WIDTH                      1  /* CS1_SLPENA */
#define WM831X_CS1_OFF_RAMP_MASK                0x0C00  /* CS1_OFF_RAMP - [11:10] */
#define WM831X_CS1_OFF_RAMP_SHIFT                   10  /* CS1_OFF_RAMP - [11:10] */
#define WM831X_CS1_OFF_RAMP_WIDTH                    2  /* CS1_OFF_RAMP - [11:10] */
#define WM831X_CS1_ON_RAMP_MASK                 0x0300  /* CS1_ON_RAMP - [9:8] */
#define WM831X_CS1_ON_RAMP_SHIFT                     8  /* CS1_ON_RAMP - [9:8] */
#define WM831X_CS1_ON_RAMP_WIDTH                     2  /* CS1_ON_RAMP - [9:8] */
#define WM831X_CS1_ISEL_MASK                    0x003F  /* CS1_ISEL - [5:0] */
#define WM831X_CS1_ISEL_SHIFT                        0  /* CS1_ISEL - [5:0] */
#define WM831X_CS1_ISEL_WIDTH                        6  /* CS1_ISEL - [5:0] */

/*
 * R16463 (0x404F) - Current Sink 2
 */
#define WM831X_CS2_ENA                          0x8000  /* CS2_ENA */
#define WM831X_CS2_ENA_MASK                     0x8000  /* CS2_ENA */
#define WM831X_CS2_ENA_SHIFT                        15  /* CS2_ENA */
#define WM831X_CS2_ENA_WIDTH                         1  /* CS2_ENA */
#define WM831X_CS2_DRIVE                        0x4000  /* CS2_DRIVE */
#define WM831X_CS2_DRIVE_MASK                   0x4000  /* CS2_DRIVE */
#define WM831X_CS2_DRIVE_SHIFT                      14  /* CS2_DRIVE */
#define WM831X_CS2_DRIVE_WIDTH                       1  /* CS2_DRIVE */
#define WM831X_CS2_SLPENA                       0x1000  /* CS2_SLPENA */
#define WM831X_CS2_SLPENA_MASK                  0x1000  /* CS2_SLPENA */
#define WM831X_CS2_SLPENA_SHIFT                     12  /* CS2_SLPENA */
#define WM831X_CS2_SLPENA_WIDTH                      1  /* CS2_SLPENA */
#define WM831X_CS2_OFF_RAMP_MASK                0x0C00  /* CS2_OFF_RAMP - [11:10] */
#define WM831X_CS2_OFF_RAMP_SHIFT                   10  /* CS2_OFF_RAMP - [11:10] */
#define WM831X_CS2_OFF_RAMP_WIDTH                    2  /* CS2_OFF_RAMP - [11:10] */
#define WM831X_CS2_ON_RAMP_MASK                 0x0300  /* CS2_ON_RAMP - [9:8] */
#define WM831X_CS2_ON_RAMP_SHIFT                     8  /* CS2_ON_RAMP - [9:8] */
#define WM831X_CS2_ON_RAMP_WIDTH                     2  /* CS2_ON_RAMP - [9:8] */
#define WM831X_CS2_ISEL_MASK                    0x003F  /* CS2_ISEL - [5:0] */
#define WM831X_CS2_ISEL_SHIFT                        0  /* CS2_ISEL - [5:0] */
#define WM831X_CS2_ISEL_WIDTH                        6  /* CS2_ISEL - [5:0] */

/*
 * R16464 (0x4050) - DCDC Enable
 */
#define WM831X_EPE2_ENA                         0x0080  /* EPE2_ENA */
#define WM831X_EPE2_ENA_MASK                    0x0080  /* EPE2_ENA */
#define WM831X_EPE2_ENA_SHIFT                        7  /* EPE2_ENA */
#define WM831X_EPE2_ENA_WIDTH                        1  /* EPE2_ENA */
#define WM831X_EPE1_ENA                         0x0040  /* EPE1_ENA */
#define WM831X_EPE1_ENA_MASK                    0x0040  /* EPE1_ENA */
#define WM831X_EPE1_ENA_SHIFT                        6  /* EPE1_ENA */
#define WM831X_EPE1_ENA_WIDTH                        1  /* EPE1_ENA */
#define WM831X_DC4_ENA                          0x0008  /* DC4_ENA */
#define WM831X_DC4_ENA_MASK                     0x0008  /* DC4_ENA */
#define WM831X_DC4_ENA_SHIFT                         3  /* DC4_ENA */
#define WM831X_DC4_ENA_WIDTH                         1  /* DC4_ENA */
#define WM831X_DC3_ENA                          0x0004  /* DC3_ENA */
#define WM831X_DC3_ENA_MASK                     0x0004  /* DC3_ENA */
#define WM831X_DC3_ENA_SHIFT                         2  /* DC3_ENA */
#define WM831X_DC3_ENA_WIDTH                         1  /* DC3_ENA */
#define WM831X_DC2_ENA                          0x0002  /* DC2_ENA */
#define WM831X_DC2_ENA_MASK                     0x0002  /* DC2_ENA */
#define WM831X_DC2_ENA_SHIFT                         1  /* DC2_ENA */
#define WM831X_DC2_ENA_WIDTH                         1  /* DC2_ENA */
#define WM831X_DC1_ENA                          0x0001  /* DC1_ENA */
#define WM831X_DC1_ENA_MASK                     0x0001  /* DC1_ENA */
#define WM831X_DC1_ENA_SHIFT                         0  /* DC1_ENA */
#define WM831X_DC1_ENA_WIDTH                         1  /* DC1_ENA */

/*
 * R16465 (0x4051) - LDO Enable
 */
#define WM831X_LDO11_ENA                        0x0400  /* LDO11_ENA */
#define WM831X_LDO11_ENA_MASK                   0x0400  /* LDO11_ENA */
#define WM831X_LDO11_ENA_SHIFT                      10  /* LDO11_ENA */
#define WM831X_LDO11_ENA_WIDTH                       1  /* LDO11_ENA */
#define WM831X_LDO10_ENA                        0x0200  /* LDO10_ENA */
#define WM831X_LDO10_ENA_MASK                   0x0200  /* LDO10_ENA */
#define WM831X_LDO10_ENA_SHIFT                       9  /* LDO10_ENA */
#define WM831X_LDO10_ENA_WIDTH                       1  /* LDO10_ENA */
#define WM831X_LDO9_ENA                         0x0100  /* LDO9_ENA */
#define WM831X_LDO9_ENA_MASK                    0x0100  /* LDO9_ENA */
#define WM831X_LDO9_ENA_SHIFT                        8  /* LDO9_ENA */
#define WM831X_LDO9_ENA_WIDTH                        1  /* LDO9_ENA */
#define WM831X_LDO8_ENA                         0x0080  /* LDO8_ENA */
#define WM831X_LDO8_ENA_MASK                    0x0080  /* LDO8_ENA */
#define WM831X_LDO8_ENA_SHIFT                        7  /* LDO8_ENA */
#define WM831X_LDO8_ENA_WIDTH                        1  /* LDO8_ENA */
#define WM831X_LDO7_ENA                         0x0040  /* LDO7_ENA */
#define WM831X_LDO7_ENA_MASK                    0x0040  /* LDO7_ENA */
#define WM831X_LDO7_ENA_SHIFT                        6  /* LDO7_ENA */
#define WM831X_LDO7_ENA_WIDTH                        1  /* LDO7_ENA */
#define WM831X_LDO6_ENA                         0x0020  /* LDO6_ENA */
#define WM831X_LDO6_ENA_MASK                    0x0020  /* LDO6_ENA */
#define WM831X_LDO6_ENA_SHIFT                        5  /* LDO6_ENA */
#define WM831X_LDO6_ENA_WIDTH                        1  /* LDO6_ENA */
#define WM831X_LDO5_ENA                         0x0010  /* LDO5_ENA */
#define WM831X_LDO5_ENA_MASK                    0x0010  /* LDO5_ENA */
#define WM831X_LDO5_ENA_SHIFT                        4  /* LDO5_ENA */
#define WM831X_LDO5_ENA_WIDTH                        1  /* LDO5_ENA */
#define WM831X_LDO4_ENA                         0x0008  /* LDO4_ENA */
#define WM831X_LDO4_ENA_MASK                    0x0008  /* LDO4_ENA */
#define WM831X_LDO4_ENA_SHIFT                        3  /* LDO4_ENA */
#define WM831X_LDO4_ENA_WIDTH                        1  /* LDO4_ENA */
#define WM831X_LDO3_ENA                         0x0004  /* LDO3_ENA */
#define WM831X_LDO3_ENA_MASK                    0x0004  /* LDO3_ENA */
#define WM831X_LDO3_ENA_SHIFT                        2  /* LDO3_ENA */
#define WM831X_LDO3_ENA_WIDTH                        1  /* LDO3_ENA */
#define WM831X_LDO2_ENA                         0x0002  /* LDO2_ENA */
#define WM831X_LDO2_ENA_MASK                    0x0002  /* LDO2_ENA */
#define WM831X_LDO2_ENA_SHIFT                        1  /* LDO2_ENA */
#define WM831X_LDO2_ENA_WIDTH                        1  /* LDO2_ENA */
#define WM831X_LDO1_ENA                         0x0001  /* LDO1_ENA */
#define WM831X_LDO1_ENA_MASK                    0x0001  /* LDO1_ENA */
#define WM831X_LDO1_ENA_SHIFT                        0  /* LDO1_ENA */
#define WM831X_LDO1_ENA_WIDTH                        1  /* LDO1_ENA */

/*
 * R16466 (0x4052) - DCDC Status
 */
#define WM831X_EPE2_STS                         0x0080  /* EPE2_STS */
#define WM831X_EPE2_STS_MASK                    0x0080  /* EPE2_STS */
#define WM831X_EPE2_STS_SHIFT                        7  /* EPE2_STS */
#define WM831X_EPE2_STS_WIDTH                        1  /* EPE2_STS */
#define WM831X_EPE1_STS                         0x0040  /* EPE1_STS */
#define WM831X_EPE1_STS_MASK                    0x0040  /* EPE1_STS */
#define WM831X_EPE1_STS_SHIFT                        6  /* EPE1_STS */
#define WM831X_EPE1_STS_WIDTH                        1  /* EPE1_STS */
#define WM831X_DC4_STS                          0x0008  /* DC4_STS */
#define WM831X_DC4_STS_MASK                     0x0008  /* DC4_STS */
#define WM831X_DC4_STS_SHIFT                         3  /* DC4_STS */
#define WM831X_DC4_STS_WIDTH                         1  /* DC4_STS */
#define WM831X_DC3_STS                          0x0004  /* DC3_STS */
#define WM831X_DC3_STS_MASK                     0x0004  /* DC3_STS */
#define WM831X_DC3_STS_SHIFT                         2  /* DC3_STS */
#define WM831X_DC3_STS_WIDTH                         1  /* DC3_STS */
#define WM831X_DC2_STS                          0x0002  /* DC2_STS */
#define WM831X_DC2_STS_MASK                     0x0002  /* DC2_STS */
#define WM831X_DC2_STS_SHIFT                         1  /* DC2_STS */
#define WM831X_DC2_STS_WIDTH                         1  /* DC2_STS */
#define WM831X_DC1_STS                          0x0001  /* DC1_STS */
#define WM831X_DC1_STS_MASK                     0x0001  /* DC1_STS */
#define WM831X_DC1_STS_SHIFT                         0  /* DC1_STS */
#define WM831X_DC1_STS_WIDTH                         1  /* DC1_STS */

/*
 * R16467 (0x4053) - LDO Status
 */
#define WM831X_LDO11_STS                        0x0400  /* LDO11_STS */
#define WM831X_LDO11_STS_MASK                   0x0400  /* LDO11_STS */
#define WM831X_LDO11_STS_SHIFT                      10  /* LDO11_STS */
#define WM831X_LDO11_STS_WIDTH                       1  /* LDO11_STS */
#define WM831X_LDO10_STS                        0x0200  /* LDO10_STS */
#define WM831X_LDO10_STS_MASK                   0x0200  /* LDO10_STS */
#define WM831X_LDO10_STS_SHIFT                       9  /* LDO10_STS */
#define WM831X_LDO10_STS_WIDTH                       1  /* LDO10_STS */
#define WM831X_LDO9_STS                         0x0100  /* LDO9_STS */
#define WM831X_LDO9_STS_MASK                    0x0100  /* LDO9_STS */
#define WM831X_LDO9_STS_SHIFT                        8  /* LDO9_STS */
#define WM831X_LDO9_STS_WIDTH                        1  /* LDO9_STS */
#define WM831X_LDO8_STS                         0x0080  /* LDO8_STS */
#define WM831X_LDO8_STS_MASK                    0x0080  /* LDO8_STS */
#define WM831X_LDO8_STS_SHIFT                        7  /* LDO8_STS */
#define WM831X_LDO8_STS_WIDTH                        1  /* LDO8_STS */
#define WM831X_LDO7_STS                         0x0040  /* LDO7_STS */
#define WM831X_LDO7_STS_MASK                    0x0040  /* LDO7_STS */
#define WM831X_LDO7_STS_SHIFT                        6  /* LDO7_STS */
#define WM831X_LDO7_STS_WIDTH                        1  /* LDO7_STS */
#define WM831X_LDO6_STS                         0x0020  /* LDO6_STS */
#define WM831X_LDO6_STS_MASK                    0x0020  /* LDO6_STS */
#define WM831X_LDO6_STS_SHIFT                        5  /* LDO6_STS */
#define WM831X_LDO6_STS_WIDTH                        1  /* LDO6_STS */
#define WM831X_LDO5_STS                         0x0010  /* LDO5_STS */
#define WM831X_LDO5_STS_MASK                    0x0010  /* LDO5_STS */
#define WM831X_LDO5_STS_SHIFT                        4  /* LDO5_STS */
#define WM831X_LDO5_STS_WIDTH                        1  /* LDO5_STS */
#define WM831X_LDO4_STS                         0x0008  /* LDO4_STS */
#define WM831X_LDO4_STS_MASK                    0x0008  /* LDO4_STS */
#define WM831X_LDO4_STS_SHIFT                        3  /* LDO4_STS */
#define WM831X_LDO4_STS_WIDTH                        1  /* LDO4_STS */
#define WM831X_LDO3_STS                         0x0004  /* LDO3_STS */
#define WM831X_LDO3_STS_MASK                    0x0004  /* LDO3_STS */
#define WM831X_LDO3_STS_SHIFT                        2  /* LDO3_STS */
#define WM831X_LDO3_STS_WIDTH                        1  /* LDO3_STS */
#define WM831X_LDO2_STS                         0x0002  /* LDO2_STS */
#define WM831X_LDO2_STS_MASK                    0x0002  /* LDO2_STS */
#define WM831X_LDO2_STS_SHIFT                        1  /* LDO2_STS */
#define WM831X_LDO2_STS_WIDTH                        1  /* LDO2_STS */
#define WM831X_LDO1_STS                         0x0001  /* LDO1_STS */
#define WM831X_LDO1_STS_MASK                    0x0001  /* LDO1_STS */
#define WM831X_LDO1_STS_SHIFT                        0  /* LDO1_STS */
#define WM831X_LDO1_STS_WIDTH                        1  /* LDO1_STS */

/*
 * R16468 (0x4054) - DCDC UV Status
 */
#define WM831X_DC2_OV_STS                       0x2000  /* DC2_OV_STS */
#define WM831X_DC2_OV_STS_MASK                  0x2000  /* DC2_OV_STS */
#define WM831X_DC2_OV_STS_SHIFT                     13  /* DC2_OV_STS */
#define WM831X_DC2_OV_STS_WIDTH                      1  /* DC2_OV_STS */
#define WM831X_DC1_OV_STS                       0x1000  /* DC1_OV_STS */
#define WM831X_DC1_OV_STS_MASK                  0x1000  /* DC1_OV_STS */
#define WM831X_DC1_OV_STS_SHIFT                     12  /* DC1_OV_STS */
#define WM831X_DC1_OV_STS_WIDTH                      1  /* DC1_OV_STS */
#define WM831X_DC2_HC_STS                       0x0200  /* DC2_HC_STS */
#define WM831X_DC2_HC_STS_MASK                  0x0200  /* DC2_HC_STS */
#define WM831X_DC2_HC_STS_SHIFT                      9  /* DC2_HC_STS */
#define WM831X_DC2_HC_STS_WIDTH                      1  /* DC2_HC_STS */
#define WM831X_DC1_HC_STS                       0x0100  /* DC1_HC_STS */
#define WM831X_DC1_HC_STS_MASK                  0x0100  /* DC1_HC_STS */
#define WM831X_DC1_HC_STS_SHIFT                      8  /* DC1_HC_STS */
#define WM831X_DC1_HC_STS_WIDTH                      1  /* DC1_HC_STS */
#define WM831X_DC4_UV_STS                       0x0008  /* DC4_UV_STS */
#define WM831X_DC4_UV_STS_MASK                  0x0008  /* DC4_UV_STS */
#define WM831X_DC4_UV_STS_SHIFT                      3  /* DC4_UV_STS */
#define WM831X_DC4_UV_STS_WIDTH                      1  /* DC4_UV_STS */
#define WM831X_DC3_UV_STS                       0x0004  /* DC3_UV_STS */
#define WM831X_DC3_UV_STS_MASK                  0x0004  /* DC3_UV_STS */
#define WM831X_DC3_UV_STS_SHIFT                      2  /* DC3_UV_STS */
#define WM831X_DC3_UV_STS_WIDTH                      1  /* DC3_UV_STS */
#define WM831X_DC2_UV_STS                       0x0002  /* DC2_UV_STS */
#define WM831X_DC2_UV_STS_MASK                  0x0002  /* DC2_UV_STS */
#define WM831X_DC2_UV_STS_SHIFT                      1  /* DC2_UV_STS */
#define WM831X_DC2_UV_STS_WIDTH                      1  /* DC2_UV_STS */
#define WM831X_DC1_UV_STS                       0x0001  /* DC1_UV_STS */
#define WM831X_DC1_UV_STS_MASK                  0x0001  /* DC1_UV_STS */
#define WM831X_DC1_UV_STS_SHIFT                      0  /* DC1_UV_STS */
#define WM831X_DC1_UV_STS_WIDTH                      1  /* DC1_UV_STS */

/*
 * R16469 (0x4055) - LDO UV Status
 */
#define WM831X_INTLDO_UV_STS                    0x8000  /* INTLDO_UV_STS */
#define WM831X_INTLDO_UV_STS_MASK               0x8000  /* INTLDO_UV_STS */
#define WM831X_INTLDO_UV_STS_SHIFT                  15  /* INTLDO_UV_STS */
#define WM831X_INTLDO_UV_STS_WIDTH                   1  /* INTLDO_UV_STS */
#define WM831X_LDO10_UV_STS                     0x0200  /* LDO10_UV_STS */
#define WM831X_LDO10_UV_STS_MASK                0x0200  /* LDO10_UV_STS */
#define WM831X_LDO10_UV_STS_SHIFT                    9  /* LDO10_UV_STS */
#define WM831X_LDO10_UV_STS_WIDTH                    1  /* LDO10_UV_STS */
#define WM831X_LDO9_UV_STS                      0x0100  /* LDO9_UV_STS */
#define WM831X_LDO9_UV_STS_MASK                 0x0100  /* LDO9_UV_STS */
#define WM831X_LDO9_UV_STS_SHIFT                     8  /* LDO9_UV_STS */
#define WM831X_LDO9_UV_STS_WIDTH                     1  /* LDO9_UV_STS */
#define WM831X_LDO8_UV_STS                      0x0080  /* LDO8_UV_STS */
#define WM831X_LDO8_UV_STS_MASK                 0x0080  /* LDO8_UV_STS */
#define WM831X_LDO8_UV_STS_SHIFT                     7  /* LDO8_UV_STS */
#define WM831X_LDO8_UV_STS_WIDTH                     1  /* LDO8_UV_STS */
#define WM831X_LDO7_UV_STS                      0x0040  /* LDO7_UV_STS */
#define WM831X_LDO7_UV_STS_MASK                 0x0040  /* LDO7_UV_STS */
#define WM831X_LDO7_UV_STS_SHIFT                     6  /* LDO7_UV_STS */
#define WM831X_LDO7_UV_STS_WIDTH                     1  /* LDO7_UV_STS */
#define WM831X_LDO6_UV_STS                      0x0020  /* LDO6_UV_STS */
#define WM831X_LDO6_UV_STS_MASK                 0x0020  /* LDO6_UV_STS */
#define WM831X_LDO6_UV_STS_SHIFT                     5  /* LDO6_UV_STS */
#define WM831X_LDO6_UV_STS_WIDTH                     1  /* LDO6_UV_STS */
#define WM831X_LDO5_UV_STS                      0x0010  /* LDO5_UV_STS */
#define WM831X_LDO5_UV_STS_MASK                 0x0010  /* LDO5_UV_STS */
#define WM831X_LDO5_UV_STS_SHIFT                     4  /* LDO5_UV_STS */
#define WM831X_LDO5_UV_STS_WIDTH                     1  /* LDO5_UV_STS */
#define WM831X_LDO4_UV_STS                      0x0008  /* LDO4_UV_STS */
#define WM831X_LDO4_UV_STS_MASK                 0x0008  /* LDO4_UV_STS */
#define WM831X_LDO4_UV_STS_SHIFT                     3  /* LDO4_UV_STS */
#define WM831X_LDO4_UV_STS_WIDTH                     1  /* LDO4_UV_STS */
#define WM831X_LDO3_UV_STS                      0x0004  /* LDO3_UV_STS */
#define WM831X_LDO3_UV_STS_MASK                 0x0004  /* LDO3_UV_STS */
#define WM831X_LDO3_UV_STS_SHIFT                     2  /* LDO3_UV_STS */
#define WM831X_LDO3_UV_STS_WIDTH                     1  /* LDO3_UV_STS */
#define WM831X_LDO2_UV_STS                      0x0002  /* LDO2_UV_STS */
#define WM831X_LDO2_UV_STS_MASK                 0x0002  /* LDO2_UV_STS */
#define WM831X_LDO2_UV_STS_SHIFT                     1  /* LDO2_UV_STS */
#define WM831X_LDO2_UV_STS_WIDTH                     1  /* LDO2_UV_STS */
#define WM831X_LDO1_UV_STS                      0x0001  /* LDO1_UV_STS */
#define WM831X_LDO1_UV_STS_MASK                 0x0001  /* LDO1_UV_STS */
#define WM831X_LDO1_UV_STS_SHIFT                     0  /* LDO1_UV_STS */
#define WM831X_LDO1_UV_STS_WIDTH                     1  /* LDO1_UV_STS */

/*
 * R16470 (0x4056) - DC1 Control 1
 */
#define WM831X_DC1_RATE_MASK                    0xC000  /* DC1_RATE - [15:14] */
#define WM831X_DC1_RATE_SHIFT                       14  /* DC1_RATE - [15:14] */
#define WM831X_DC1_RATE_WIDTH                        2  /* DC1_RATE - [15:14] */
#define WM831X_DC1_PHASE                        0x1000  /* DC1_PHASE */
#define WM831X_DC1_PHASE_MASK                   0x1000  /* DC1_PHASE */
#define WM831X_DC1_PHASE_SHIFT                      12  /* DC1_PHASE */
#define WM831X_DC1_PHASE_WIDTH                       1  /* DC1_PHASE */
#define WM831X_DC1_FREQ_MASK                    0x0300  /* DC1_FREQ - [9:8] */
#define WM831X_DC1_FREQ_SHIFT                        8  /* DC1_FREQ - [9:8] */
#define WM831X_DC1_FREQ_WIDTH                        2  /* DC1_FREQ - [9:8] */
#define WM831X_DC1_FLT                          0x0080  /* DC1_FLT */
#define WM831X_DC1_FLT_MASK                     0x0080  /* DC1_FLT */
#define WM831X_DC1_FLT_SHIFT                         7  /* DC1_FLT */
#define WM831X_DC1_FLT_WIDTH                         1  /* DC1_FLT */
#define WM831X_DC1_SOFT_START_MASK              0x0030  /* DC1_SOFT_START - [5:4] */
#define WM831X_DC1_SOFT_START_SHIFT                  4  /* DC1_SOFT_START - [5:4] */
#define WM831X_DC1_SOFT_START_WIDTH                  2  /* DC1_SOFT_START - [5:4] */
#define WM831X_DC1_CAP_MASK                     0x0003  /* DC1_CAP - [1:0] */
#define WM831X_DC1_CAP_SHIFT                         0  /* DC1_CAP - [1:0] */
#define WM831X_DC1_CAP_WIDTH                         2  /* DC1_CAP - [1:0] */

/*
 * R16471 (0x4057) - DC1 Control 2
 */
#define WM831X_DC1_ERR_ACT_MASK                 0xC000  /* DC1_ERR_ACT - [15:14] */
#define WM831X_DC1_ERR_ACT_SHIFT                    14  /* DC1_ERR_ACT - [15:14] */
#define WM831X_DC1_ERR_ACT_WIDTH                     2  /* DC1_ERR_ACT - [15:14] */
#define WM831X_DC1_HWC_SRC_MASK                 0x1800  /* DC1_HWC_SRC - [12:11] */
#define WM831X_DC1_HWC_SRC_SHIFT                    11  /* DC1_HWC_SRC - [12:11] */
#define WM831X_DC1_HWC_SRC_WIDTH                     2  /* DC1_HWC_SRC - [12:11] */
#define WM831X_DC1_HWC_VSEL                     0x0400  /* DC1_HWC_VSEL */
#define WM831X_DC1_HWC_VSEL_MASK                0x0400  /* DC1_HWC_VSEL */
#define WM831X_DC1_HWC_VSEL_SHIFT                   10  /* DC1_HWC_VSEL */
#define WM831X_DC1_HWC_VSEL_WIDTH                    1  /* DC1_HWC_VSEL */
#define WM831X_DC1_HWC_MODE_MASK                0x0300  /* DC1_HWC_MODE - [9:8] */
#define WM831X_DC1_HWC_MODE_SHIFT                    8  /* DC1_HWC_MODE - [9:8] */
#define WM831X_DC1_HWC_MODE_WIDTH                    2  /* DC1_HWC_MODE - [9:8] */
#define WM831X_DC1_HC_THR_MASK                  0x0070  /* DC1_HC_THR - [6:4] */
#define WM831X_DC1_HC_THR_SHIFT                      4  /* DC1_HC_THR - [6:4] */
#define WM831X_DC1_HC_THR_WIDTH                      3  /* DC1_HC_THR - [6:4] */
#define WM831X_DC1_HC_IND_ENA                   0x0001  /* DC1_HC_IND_ENA */
#define WM831X_DC1_HC_IND_ENA_MASK              0x0001  /* DC1_HC_IND_ENA */
#define WM831X_DC1_HC_IND_ENA_SHIFT                  0  /* DC1_HC_IND_ENA */
#define WM831X_DC1_HC_IND_ENA_WIDTH                  1  /* DC1_HC_IND_ENA */

/*
 * R16472 (0x4058) - DC1 ON Config
 */
#define WM831X_DC1_ON_SLOT_MASK                 0xE000  /* DC1_ON_SLOT - [15:13] */
#define WM831X_DC1_ON_SLOT_SHIFT                    13  /* DC1_ON_SLOT - [15:13] */
#define WM831X_DC1_ON_SLOT_WIDTH                     3  /* DC1_ON_SLOT - [15:13] */
#define WM831X_DC1_ON_MODE_MASK                 0x0300  /* DC1_ON_MODE - [9:8] */
#define WM831X_DC1_ON_MODE_SHIFT                     8  /* DC1_ON_MODE - [9:8] */
#define WM831X_DC1_ON_MODE_WIDTH                     2  /* DC1_ON_MODE - [9:8] */
#define WM831X_DC1_ON_VSEL_MASK                 0x007F  /* DC1_ON_VSEL - [6:0] */
#define WM831X_DC1_ON_VSEL_SHIFT                     0  /* DC1_ON_VSEL - [6:0] */
#define WM831X_DC1_ON_VSEL_WIDTH                     7  /* DC1_ON_VSEL - [6:0] */

/*
 * R16473 (0x4059) - DC1 SLEEP Control
 */
#define WM831X_DC1_SLP_SLOT_MASK                0xE000  /* DC1_SLP_SLOT - [15:13] */
#define WM831X_DC1_SLP_SLOT_SHIFT                   13  /* DC1_SLP_SLOT - [15:13] */
#define WM831X_DC1_SLP_SLOT_WIDTH                    3  /* DC1_SLP_SLOT - [15:13] */
#define WM831X_DC1_SLP_MODE_MASK                0x0300  /* DC1_SLP_MODE - [9:8] */
#define WM831X_DC1_SLP_MODE_SHIFT                    8  /* DC1_SLP_MODE - [9:8] */
#define WM831X_DC1_SLP_MODE_WIDTH                    2  /* DC1_SLP_MODE - [9:8] */
#define WM831X_DC1_SLP_VSEL_MASK                0x007F  /* DC1_SLP_VSEL - [6:0] */
#define WM831X_DC1_SLP_VSEL_SHIFT                    0  /* DC1_SLP_VSEL - [6:0] */
#define WM831X_DC1_SLP_VSEL_WIDTH                    7  /* DC1_SLP_VSEL - [6:0] */

/*
 * R16474 (0x405A) - DC1 DVS Control
 */
#define WM831X_DC1_DVS_SRC_MASK                 0x1800  /* DC1_DVS_SRC - [12:11] */
#define WM831X_DC1_DVS_SRC_SHIFT                    11  /* DC1_DVS_SRC - [12:11] */
#define WM831X_DC1_DVS_SRC_WIDTH                     2  /* DC1_DVS_SRC - [12:11] */
#define WM831X_DC1_DVS_VSEL_MASK                0x007F  /* DC1_DVS_VSEL - [6:0] */
#define WM831X_DC1_DVS_VSEL_SHIFT                    0  /* DC1_DVS_VSEL - [6:0] */
#define WM831X_DC1_DVS_VSEL_WIDTH                    7  /* DC1_DVS_VSEL - [6:0] */

/*
 * R16475 (0x405B) - DC2 Control 1
 */
#define WM831X_DC2_RATE_MASK                    0xC000  /* DC2_RATE - [15:14] */
#define WM831X_DC2_RATE_SHIFT                       14  /* DC2_RATE - [15:14] */
#define WM831X_DC2_RATE_WIDTH                        2  /* DC2_RATE - [15:14] */
#define WM831X_DC2_PHASE                        0x1000  /* DC2_PHASE */
#define WM831X_DC2_PHASE_MASK                   0x1000  /* DC2_PHASE */
#define WM831X_DC2_PHASE_SHIFT                      12  /* DC2_PHASE */
#define WM831X_DC2_PHASE_WIDTH                       1  /* DC2_PHASE */
#define WM831X_DC2_FREQ_MASK                    0x0300  /* DC2_FREQ - [9:8] */
#define WM831X_DC2_FREQ_SHIFT                        8  /* DC2_FREQ - [9:8] */
#define WM831X_DC2_FREQ_WIDTH                        2  /* DC2_FREQ - [9:8] */
#define WM831X_DC2_FLT                          0x0080  /* DC2_FLT */
#define WM831X_DC2_FLT_MASK                     0x0080  /* DC2_FLT */
#define WM831X_DC2_FLT_SHIFT                         7  /* DC2_FLT */
#define WM831X_DC2_FLT_WIDTH                         1  /* DC2_FLT */
#define WM831X_DC2_SOFT_START_MASK              0x0030  /* DC2_SOFT_START - [5:4] */
#define WM831X_DC2_SOFT_START_SHIFT                  4  /* DC2_SOFT_START - [5:4] */
#define WM831X_DC2_SOFT_START_WIDTH                  2  /* DC2_SOFT_START - [5:4] */
#define WM831X_DC2_CAP_MASK                     0x0003  /* DC2_CAP - [1:0] */
#define WM831X_DC2_CAP_SHIFT                         0  /* DC2_CAP - [1:0] */
#define WM831X_DC2_CAP_WIDTH                         2  /* DC2_CAP - [1:0] */

/*
 * R16476 (0x405C) - DC2 Control 2
 */
#define WM831X_DC2_ERR_ACT_MASK                 0xC000  /* DC2_ERR_ACT - [15:14] */
#define WM831X_DC2_ERR_ACT_SHIFT                    14  /* DC2_ERR_ACT - [15:14] */
#define WM831X_DC2_ERR_ACT_WIDTH                     2  /* DC2_ERR_ACT - [15:14] */
#define WM831X_DC2_HWC_SRC_MASK                 0x1800  /* DC2_HWC_SRC - [12:11] */
#define WM831X_DC2_HWC_SRC_SHIFT                    11  /* DC2_HWC_SRC - [12:11] */
#define WM831X_DC2_HWC_SRC_WIDTH                     2  /* DC2_HWC_SRC - [12:11] */
#define WM831X_DC2_HWC_VSEL                     0x0400  /* DC2_HWC_VSEL */
#define WM831X_DC2_HWC_VSEL_MASK                0x0400  /* DC2_HWC_VSEL */
#define WM831X_DC2_HWC_VSEL_SHIFT                   10  /* DC2_HWC_VSEL */
#define WM831X_DC2_HWC_VSEL_WIDTH                    1  /* DC2_HWC_VSEL */
#define WM831X_DC2_HWC_MODE_MASK                0x0300  /* DC2_HWC_MODE - [9:8] */
#define WM831X_DC2_HWC_MODE_SHIFT                    8  /* DC2_HWC_MODE - [9:8] */
#define WM831X_DC2_HWC_MODE_WIDTH                    2  /* DC2_HWC_MODE - [9:8] */
#define WM831X_DC2_HC_THR_MASK                  0x0070  /* DC2_HC_THR - [6:4] */
#define WM831X_DC2_HC_THR_SHIFT                      4  /* DC2_HC_THR - [6:4] */
#define WM831X_DC2_HC_THR_WIDTH                      3  /* DC2_HC_THR - [6:4] */
#define WM831X_DC2_HC_IND_ENA                   0x0001  /* DC2_HC_IND_ENA */
#define WM831X_DC2_HC_IND_ENA_MASK              0x0001  /* DC2_HC_IND_ENA */
#define WM831X_DC2_HC_IND_ENA_SHIFT                  0  /* DC2_HC_IND_ENA */
#define WM831X_DC2_HC_IND_ENA_WIDTH                  1  /* DC2_HC_IND_ENA */

/*
 * R16477 (0x405D) - DC2 ON Config
 */
#define WM831X_DC2_ON_SLOT_MASK                 0xE000  /* DC2_ON_SLOT - [15:13] */
#define WM831X_DC2_ON_SLOT_SHIFT                    13  /* DC2_ON_SLOT - [15:13] */
#define WM831X_DC2_ON_SLOT_WIDTH                     3  /* DC2_ON_SLOT - [15:13] */
#define WM831X_DC2_ON_MODE_MASK                 0x0300  /* DC2_ON_MODE - [9:8] */
#define WM831X_DC2_ON_MODE_SHIFT                     8  /* DC2_ON_MODE - [9:8] */
#define WM831X_DC2_ON_MODE_WIDTH                     2  /* DC2_ON_MODE - [9:8] */
#define WM831X_DC2_ON_VSEL_MASK                 0x007F  /* DC2_ON_VSEL - [6:0] */
#define WM831X_DC2_ON_VSEL_SHIFT                     0  /* DC2_ON_VSEL - [6:0] */
#define WM831X_DC2_ON_VSEL_WIDTH                     7  /* DC2_ON_VSEL - [6:0] */

/*
 * R16478 (0x405E) - DC2 SLEEP Control
 */
#define WM831X_DC2_SLP_SLOT_MASK                0xE000  /* DC2_SLP_SLOT - [15:13] */
#define WM831X_DC2_SLP_SLOT_SHIFT                   13  /* DC2_SLP_SLOT - [15:13] */
#define WM831X_DC2_SLP_SLOT_WIDTH                    3  /* DC2_SLP_SLOT - [15:13] */
#define WM831X_DC2_SLP_MODE_MASK                0x0300  /* DC2_SLP_MODE - [9:8] */
#define WM831X_DC2_SLP_MODE_SHIFT                    8  /* DC2_SLP_MODE - [9:8] */
#define WM831X_DC2_SLP_MODE_WIDTH                    2  /* DC2_SLP_MODE - [9:8] */
#define WM831X_DC2_SLP_VSEL_MASK                0x007F  /* DC2_SLP_VSEL - [6:0] */
#define WM831X_DC2_SLP_VSEL_SHIFT                    0  /* DC2_SLP_VSEL - [6:0] */
#define WM831X_DC2_SLP_VSEL_WIDTH                    7  /* DC2_SLP_VSEL - [6:0] */

/*
 * R16479 (0x405F) - DC2 DVS Control
 */
#define WM831X_DC2_DVS_SRC_MASK                 0x1800  /* DC2_DVS_SRC - [12:11] */
#define WM831X_DC2_DVS_SRC_SHIFT                    11  /* DC2_DVS_SRC - [12:11] */
#define WM831X_DC2_DVS_SRC_WIDTH                     2  /* DC2_DVS_SRC - [12:11] */
#define WM831X_DC2_DVS_VSEL_MASK                0x007F  /* DC2_DVS_VSEL - [6:0] */
#define WM831X_DC2_DVS_VSEL_SHIFT                    0  /* DC2_DVS_VSEL - [6:0] */
#define WM831X_DC2_DVS_VSEL_WIDTH                    7  /* DC2_DVS_VSEL - [6:0] */

/*
 * R16480 (0x4060) - DC3 Control 1
 */
#define WM831X_DC3_PHASE                        0x1000  /* DC3_PHASE */
#define WM831X_DC3_PHASE_MASK                   0x1000  /* DC3_PHASE */
#define WM831X_DC3_PHASE_SHIFT                      12  /* DC3_PHASE */
#define WM831X_DC3_PHASE_WIDTH                       1  /* DC3_PHASE */
#define WM831X_DC3_FLT                          0x0080  /* DC3_FLT */
#define WM831X_DC3_FLT_MASK                     0x0080  /* DC3_FLT */
#define WM831X_DC3_FLT_SHIFT                         7  /* DC3_FLT */
#define WM831X_DC3_FLT_WIDTH                         1  /* DC3_FLT */
#define WM831X_DC3_SOFT_START_MASK              0x0030  /* DC3_SOFT_START - [5:4] */
#define WM831X_DC3_SOFT_START_SHIFT                  4  /* DC3_SOFT_START - [5:4] */
#define WM831X_DC3_SOFT_START_WIDTH                  2  /* DC3_SOFT_START - [5:4] */
#define WM831X_DC3_STNBY_LIM_MASK               0x000C  /* DC3_STNBY_LIM - [3:2] */
#define WM831X_DC3_STNBY_LIM_SHIFT                   2  /* DC3_STNBY_LIM - [3:2] */
#define WM831X_DC3_STNBY_LIM_WIDTH                   2  /* DC3_STNBY_LIM - [3:2] */
#define WM831X_DC3_CAP_MASK                     0x0003  /* DC3_CAP - [1:0] */
#define WM831X_DC3_CAP_SHIFT                         0  /* DC3_CAP - [1:0] */
#define WM831X_DC3_CAP_WIDTH                         2  /* DC3_CAP - [1:0] */

/*
 * R16481 (0x4061) - DC3 Control 2
 */
#define WM831X_DC3_ERR_ACT_MASK                 0xC000  /* DC3_ERR_ACT - [15:14] */
#define WM831X_DC3_ERR_ACT_SHIFT                    14  /* DC3_ERR_ACT - [15:14] */
#define WM831X_DC3_ERR_ACT_WIDTH                     2  /* DC3_ERR_ACT - [15:14] */
#define WM831X_DC3_HWC_SRC_MASK                 0x1800  /* DC3_HWC_SRC - [12:11] */
#define WM831X_DC3_HWC_SRC_SHIFT                    11  /* DC3_HWC_SRC - [12:11] */
#define WM831X_DC3_HWC_SRC_WIDTH                     2  /* DC3_HWC_SRC - [12:11] */
#define WM831X_DC3_HWC_VSEL                     0x0400  /* DC3_HWC_VSEL */
#define WM831X_DC3_HWC_VSEL_MASK                0x0400  /* DC3_HWC_VSEL */
#define WM831X_DC3_HWC_VSEL_SHIFT                   10  /* DC3_HWC_VSEL */
#define WM831X_DC3_HWC_VSEL_WIDTH                    1  /* DC3_HWC_VSEL */
#define WM831X_DC3_HWC_MODE_MASK                0x0300  /* DC3_HWC_MODE - [9:8] */
#define WM831X_DC3_HWC_MODE_SHIFT                    8  /* DC3_HWC_MODE - [9:8] */
#define WM831X_DC3_HWC_MODE_WIDTH                    2  /* DC3_HWC_MODE - [9:8] */
#define WM831X_DC3_OVP                          0x0080  /* DC3_OVP */
#define WM831X_DC3_OVP_MASK                     0x0080  /* DC3_OVP */
#define WM831X_DC3_OVP_SHIFT                         7  /* DC3_OVP */
#define WM831X_DC3_OVP_WIDTH                         1  /* DC3_OVP */

/*
 * R16482 (0x4062) - DC3 ON Config
 */
#define WM831X_DC3_ON_SLOT_MASK                 0xE000  /* DC3_ON_SLOT - [15:13] */
#define WM831X_DC3_ON_SLOT_SHIFT                    13  /* DC3_ON_SLOT - [15:13] */
#define WM831X_DC3_ON_SLOT_WIDTH                     3  /* DC3_ON_SLOT - [15:13] */
#define WM831X_DC3_ON_MODE_MASK                 0x0300  /* DC3_ON_MODE - [9:8] */
#define WM831X_DC3_ON_MODE_SHIFT                     8  /* DC3_ON_MODE - [9:8] */
#define WM831X_DC3_ON_MODE_WIDTH                     2  /* DC3_ON_MODE - [9:8] */
#define WM831X_DC3_ON_VSEL_MASK                 0x007F  /* DC3_ON_VSEL - [6:0] */
#define WM831X_DC3_ON_VSEL_SHIFT                     0  /* DC3_ON_VSEL - [6:0] */
#define WM831X_DC3_ON_VSEL_WIDTH                     7  /* DC3_ON_VSEL - [6:0] */

/*
 * R16483 (0x4063) - DC3 SLEEP Control
 */
#define WM831X_DC3_SLP_SLOT_MASK                0xE000  /* DC3_SLP_SLOT - [15:13] */
#define WM831X_DC3_SLP_SLOT_SHIFT                   13  /* DC3_SLP_SLOT - [15:13] */
#define WM831X_DC3_SLP_SLOT_WIDTH                    3  /* DC3_SLP_SLOT - [15:13] */
#define WM831X_DC3_SLP_MODE_MASK                0x0300  /* DC3_SLP_MODE - [9:8] */
#define WM831X_DC3_SLP_MODE_SHIFT                    8  /* DC3_SLP_MODE - [9:8] */
#define WM831X_DC3_SLP_MODE_WIDTH                    2  /* DC3_SLP_MODE - [9:8] */
#define WM831X_DC3_SLP_VSEL_MASK                0x007F  /* DC3_SLP_VSEL - [6:0] */
#define WM831X_DC3_SLP_VSEL_SHIFT                    0  /* DC3_SLP_VSEL - [6:0] */
#define WM831X_DC3_SLP_VSEL_WIDTH                    7  /* DC3_SLP_VSEL - [6:0] */

/*
 * R16484 (0x4064) - DC4 Control
 */
#define WM831X_DC4_ERR_ACT_MASK                 0xC000  /* DC4_ERR_ACT - [15:14] */
#define WM831X_DC4_ERR_ACT_SHIFT                    14  /* DC4_ERR_ACT - [15:14] */
#define WM831X_DC4_ERR_ACT_WIDTH                     2  /* DC4_ERR_ACT - [15:14] */
#define WM831X_DC4_HWC_SRC_MASK                 0x1800  /* DC4_HWC_SRC - [12:11] */
#define WM831X_DC4_HWC_SRC_SHIFT                    11  /* DC4_HWC_SRC - [12:11] */
#define WM831X_DC4_HWC_SRC_WIDTH                     2  /* DC4_HWC_SRC - [12:11] */
#define WM831X_DC4_HWC_MODE                     0x0100  /* DC4_HWC_MODE */
#define WM831X_DC4_HWC_MODE_MASK                0x0100  /* DC4_HWC_MODE */
#define WM831X_DC4_HWC_MODE_SHIFT                    8  /* DC4_HWC_MODE */
#define WM831X_DC4_HWC_MODE_WIDTH                    1  /* DC4_HWC_MODE */
#define WM831X_DC4_RANGE_MASK                   0x000C  /* DC4_RANGE - [3:2] */
#define WM831X_DC4_RANGE_SHIFT                       2  /* DC4_RANGE - [3:2] */
#define WM831X_DC4_RANGE_WIDTH                       2  /* DC4_RANGE - [3:2] */
#define WM831X_DC4_FBSRC                        0x0001  /* DC4_FBSRC */
#define WM831X_DC4_FBSRC_MASK                   0x0001  /* DC4_FBSRC */
#define WM831X_DC4_FBSRC_SHIFT                       0  /* DC4_FBSRC */
#define WM831X_DC4_FBSRC_WIDTH                       1  /* DC4_FBSRC */

/*
 * R16485 (0x4065) - DC4 SLEEP Control
 */
#define WM831X_DC4_SLPENA                       0x0100  /* DC4_SLPENA */
#define WM831X_DC4_SLPENA_MASK                  0x0100  /* DC4_SLPENA */
#define WM831X_DC4_SLPENA_SHIFT                      8  /* DC4_SLPENA */
#define WM831X_DC4_SLPENA_WIDTH                      1  /* DC4_SLPENA */

/*
 * R16526 (0x408E) - Power Good Source 1
 */
#define WM831X_DC4_OK                           0x0008  /* DC4_OK */
#define WM831X_DC4_OK_MASK                      0x0008  /* DC4_OK */
#define WM831X_DC4_OK_SHIFT                          3  /* DC4_OK */
#define WM831X_DC4_OK_WIDTH                          1  /* DC4_OK */
#define WM831X_DC3_OK                           0x0004  /* DC3_OK */
#define WM831X_DC3_OK_MASK                      0x0004  /* DC3_OK */
#define WM831X_DC3_OK_SHIFT                          2  /* DC3_OK */
#define WM831X_DC3_OK_WIDTH                          1  /* DC3_OK */
#define WM831X_DC2_OK                           0x0002  /* DC2_OK */
#define WM831X_DC2_OK_MASK                      0x0002  /* DC2_OK */
#define WM831X_DC2_OK_SHIFT                          1  /* DC2_OK */
#define WM831X_DC2_OK_WIDTH                          1  /* DC2_OK */
#define WM831X_DC1_OK                           0x0001  /* DC1_OK */
#define WM831X_DC1_OK_MASK                      0x0001  /* DC1_OK */
#define WM831X_DC1_OK_SHIFT                          0  /* DC1_OK */
#define WM831X_DC1_OK_WIDTH                          1  /* DC1_OK */

#define WM831X_ISINK_MAX_ISEL 56
extern int wm831x_isinkv_values[WM831X_ISINK_MAX_ISEL];

#endif
