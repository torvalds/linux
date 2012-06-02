/*
 * include/linux/mfd/wm831x/pmu.h -- PMU for WM831x
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

#ifndef __MFD_WM831X_PMU_H__
#define __MFD_WM831X_PMU_H__
/*    rtc cntrol (0x4025) */
#define WM831X_RTC_ALAM_ENA_MASK 0x0400
/*
 * R16387 (0x4003) - Power State
 */
#define WM831X_CHIP_ON                          0x8000  /* CHIP_ON */
#define WM831X_CHIP_ON_MASK                     0x8000  /* CHIP_ON */
#define WM831X_CHIP_ON_SHIFT                        15  /* CHIP_ON */
#define WM831X_CHIP_ON_WIDTH                         1  /* CHIP_ON */
#define WM831X_CHIP_SLP                         0x4000  /* CHIP_SLP */
#define WM831X_CHIP_SLP_MASK                    0x4000  /* CHIP_SLP */
#define WM831X_CHIP_SLP_SHIFT                       14  /* CHIP_SLP */
#define WM831X_CHIP_SLP_WIDTH                        1  /* CHIP_SLP */
#define WM831X_REF_LP                           0x1000  /* REF_LP */
#define WM831X_REF_LP_MASK                      0x1000  /* REF_LP */
#define WM831X_REF_LP_SHIFT                         12  /* REF_LP */
#define WM831X_REF_LP_WIDTH                          1  /* REF_LP */
#define WM831X_PWRSTATE_DLY_MASK                0x0C00  /* PWRSTATE_DLY - [11:10] */
#define WM831X_PWRSTATE_DLY_SHIFT                   10  /* PWRSTATE_DLY - [11:10] */
#define WM831X_PWRSTATE_DLY_WIDTH                    2  /* PWRSTATE_DLY - [11:10] */
#define WM831X_SWRST_DLY                        0x0200  /* SWRST_DLY */
#define WM831X_SWRST_DLY_MASK                   0x0200  /* SWRST_DLY */
#define WM831X_SWRST_DLY_SHIFT                       9  /* SWRST_DLY */
#define WM831X_SWRST_DLY_WIDTH                       1  /* SWRST_DLY */
#define WM831X_USB100MA_STARTUP_MASK            0x0030  /* USB100MA_STARTUP - [5:4] */
#define WM831X_USB100MA_STARTUP_SHIFT                4  /* USB100MA_STARTUP - [5:4] */
#define WM831X_USB100MA_STARTUP_WIDTH                2  /* USB100MA_STARTUP - [5:4] */
#define WM831X_USB_CURR_STS                     0x0008  /* USB_CURR_STS */
#define WM831X_USB_CURR_STS_MASK                0x0008  /* USB_CURR_STS */
#define WM831X_USB_CURR_STS_SHIFT                    3  /* USB_CURR_STS */
#define WM831X_USB_CURR_STS_WIDTH                    1  /* USB_CURR_STS */
#define WM831X_USB_ILIM_MASK                    0x0007  /* USB_ILIM - [2:0] */
#define WM831X_USB_ILIM_SHIFT                        0  /* USB_ILIM - [2:0] */
#define WM831X_USB_ILIM_WIDTH                        3  /* USB_ILIM - [2:0] */

/*
 * R16397 (0x400D) - System Status
 */
#define WM831X_THW_STS                          0x8000  /* THW_STS */
#define WM831X_THW_STS_MASK                     0x8000  /* THW_STS */
#define WM831X_THW_STS_SHIFT                        15  /* THW_STS */
#define WM831X_THW_STS_WIDTH                         1  /* THW_STS */
#define WM831X_PWR_SRC_BATT                     0x0400  /* PWR_SRC_BATT */
#define WM831X_PWR_SRC_BATT_MASK                0x0400  /* PWR_SRC_BATT */
#define WM831X_PWR_SRC_BATT_SHIFT                   10  /* PWR_SRC_BATT */
#define WM831X_PWR_SRC_BATT_WIDTH                    1  /* PWR_SRC_BATT */
#define WM831X_PWR_WALL                         0x0200  /* PWR_WALL */
#define WM831X_PWR_WALL_MASK                    0x0200  /* PWR_WALL */
#define WM831X_PWR_WALL_SHIFT                        9  /* PWR_WALL */
#define WM831X_PWR_WALL_WIDTH                        1  /* PWR_WALL */
#define WM831X_PWR_USB                          0x0100  /* PWR_USB */
#define WM831X_PWR_USB_MASK                     0x0100  /* PWR_USB */
#define WM831X_PWR_USB_SHIFT                         8  /* PWR_USB */
#define WM831X_PWR_USB_WIDTH                         1  /* PWR_USB */
#define WM831X_MAIN_STATE_MASK                  0x001F  /* MAIN_STATE - [4:0] */
#define WM831X_MAIN_STATE_SHIFT                      0  /* MAIN_STATE - [4:0] */
#define WM831X_MAIN_STATE_WIDTH                      5  /* MAIN_STATE - [4:0] */

/*
 * R16456 (0x4048) - Charger Control 1
 */
#define WM831X_CHG_ENA                          0x8000  /* CHG_ENA */
#define WM831X_CHG_ENA_MASK                     0x8000  /* CHG_ENA */
#define WM831X_CHG_ENA_SHIFT                        15  /* CHG_ENA */
#define WM831X_CHG_ENA_WIDTH                         1  /* CHG_ENA */
#define WM831X_CHG_FRC                          0x4000  /* CHG_FRC */
#define WM831X_CHG_FRC_MASK                     0x4000  /* CHG_FRC */
#define WM831X_CHG_FRC_SHIFT                        14  /* CHG_FRC */
#define WM831X_CHG_FRC_WIDTH                         1  /* CHG_FRC */
#define WM831X_CHG_ITERM_MASK                   0x1C00  /* CHG_ITERM - [12:10] */
#define WM831X_CHG_ITERM_SHIFT                      10  /* CHG_ITERM - [12:10] */
#define WM831X_CHG_ITERM_WIDTH                       3  /* CHG_ITERM - [12:10] */
#define WM831X_CHG_FAST                         0x0020  /* CHG_FAST */
#define WM831X_CHG_FAST_MASK                    0x0020  /* CHG_FAST */
#define WM831X_CHG_FAST_SHIFT                        5  /* CHG_FAST */
#define WM831X_CHG_FAST_WIDTH                        1  /* CHG_FAST */
#define WM831X_CHG_IMON_ENA                     0x0002  /* CHG_IMON_ENA */
#define WM831X_CHG_IMON_ENA_MASK                0x0002  /* CHG_IMON_ENA */
#define WM831X_CHG_IMON_ENA_SHIFT                    1  /* CHG_IMON_ENA */
#define WM831X_CHG_IMON_ENA_WIDTH                    1  /* CHG_IMON_ENA */
#define WM831X_CHG_CHIP_TEMP_MON                0x0001  /* CHG_CHIP_TEMP_MON */
#define WM831X_CHG_CHIP_TEMP_MON_MASK           0x0001  /* CHG_CHIP_TEMP_MON */
#define WM831X_CHG_CHIP_TEMP_MON_SHIFT               0  /* CHG_CHIP_TEMP_MON */
#define WM831X_CHG_CHIP_TEMP_MON_WIDTH               1  /* CHG_CHIP_TEMP_MON */

/*
 * R16457 (0x4049) - Charger Control 2
 */
#define WM831X_CHG_OFF_MSK                      0x4000  /* CHG_OFF_MSK */
#define WM831X_CHG_OFF_MSK_MASK                 0x4000  /* CHG_OFF_MSK */
#define WM831X_CHG_OFF_MSK_SHIFT                    14  /* CHG_OFF_MSK */
#define WM831X_CHG_OFF_MSK_WIDTH                     1  /* CHG_OFF_MSK */
#define WM831X_CHG_TIME_MASK                    0x0F00  /* CHG_TIME - [11:8] */
#define WM831X_CHG_TIME_SHIFT                        8  /* CHG_TIME - [11:8] */
#define WM831X_CHG_TIME_WIDTH                        4  /* CHG_TIME - [11:8] */
#define WM831X_CHG_TRKL_ILIM_MASK               0x00C0  /* CHG_TRKL_ILIM - [7:6] */
#define WM831X_CHG_TRKL_ILIM_SHIFT                   6  /* CHG_TRKL_ILIM - [7:6] */
#define WM831X_CHG_TRKL_ILIM_WIDTH                   2  /* CHG_TRKL_ILIM - [7:6] */
#define WM831X_CHG_VSEL_MASK                    0x0030  /* CHG_VSEL - [5:4] */
#define WM831X_CHG_VSEL_SHIFT                        4  /* CHG_VSEL - [5:4] */
#define WM831X_CHG_VSEL_WIDTH                        2  /* CHG_VSEL - [5:4] */
#define WM831X_CHG_FAST_ILIM_MASK               0x000F  /* CHG_FAST_ILIM - [3:0] */
#define WM831X_CHG_FAST_ILIM_SHIFT                   0  /* CHG_FAST_ILIM - [3:0] */
#define WM831X_CHG_FAST_ILIM_WIDTH                   4  /* CHG_FAST_ILIM - [3:0] */

/*
 * R16458 (0x404A) - Charger Status
 */
#define WM831X_BATT_OV_STS                      0x8000  /* BATT_OV_STS */
#define WM831X_BATT_OV_STS_MASK                 0x8000  /* BATT_OV_STS */
#define WM831X_BATT_OV_STS_SHIFT                    15  /* BATT_OV_STS */
#define WM831X_BATT_OV_STS_WIDTH                     1  /* BATT_OV_STS */
#define WM831X_CHG_STATE_MASK                   0x7000  /* CHG_STATE - [14:12] */
#define WM831X_CHG_STATE_SHIFT                      12  /* CHG_STATE - [14:12] */
#define WM831X_CHG_STATE_WIDTH                       3  /* CHG_STATE - [14:12] */
#define WM831X_BATT_HOT_STS                     0x0800  /* BATT_HOT_STS */
#define WM831X_BATT_HOT_STS_MASK                0x0800  /* BATT_HOT_STS */
#define WM831X_BATT_HOT_STS_SHIFT                   11  /* BATT_HOT_STS */
#define WM831X_BATT_HOT_STS_WIDTH                    1  /* BATT_HOT_STS */
#define WM831X_BATT_COLD_STS                    0x0400  /* BATT_COLD_STS */
#define WM831X_BATT_COLD_STS_MASK               0x0400  /* BATT_COLD_STS */
#define WM831X_BATT_COLD_STS_SHIFT                  10  /* BATT_COLD_STS */
#define WM831X_BATT_COLD_STS_WIDTH                   1  /* BATT_COLD_STS */
#define WM831X_CHG_TOPOFF                       0x0200  /* CHG_TOPOFF */
#define WM831X_CHG_TOPOFF_MASK                  0x0200  /* CHG_TOPOFF */
#define WM831X_CHG_TOPOFF_SHIFT                      9  /* CHG_TOPOFF */
#define WM831X_CHG_TOPOFF_WIDTH                      1  /* CHG_TOPOFF */
#define WM831X_CHG_ACTIVE                       0x0100  /* CHG_ACTIVE */
#define WM831X_CHG_ACTIVE_MASK                  0x0100  /* CHG_ACTIVE */
#define WM831X_CHG_ACTIVE_SHIFT                      8  /* CHG_ACTIVE */
#define WM831X_CHG_ACTIVE_WIDTH                      1  /* CHG_ACTIVE */
#define WM831X_CHG_TIME_ELAPSED_MASK            0x00FF  /* CHG_TIME_ELAPSED - [7:0] */
#define WM831X_CHG_TIME_ELAPSED_SHIFT                0  /* CHG_TIME_ELAPSED - [7:0] */
#define WM831X_CHG_TIME_ELAPSED_WIDTH                8  /* CHG_TIME_ELAPSED - [7:0] */

#define WM831X_CHG_STATE_OFF         (0 << WM831X_CHG_STATE_SHIFT)
#define WM831X_CHG_STATE_TRICKLE     (1 << WM831X_CHG_STATE_SHIFT)
#define WM831X_CHG_STATE_FAST        (2 << WM831X_CHG_STATE_SHIFT)
#define WM831X_CHG_STATE_TRICKLE_OT  (3 << WM831X_CHG_STATE_SHIFT)
#define WM831X_CHG_STATE_FAST_OT     (4 << WM831X_CHG_STATE_SHIFT)
#define WM831X_CHG_STATE_DEFECTIVE   (5 << WM831X_CHG_STATE_SHIFT)

/*
 * R16459 (0x404B) - Backup Charger Control
 */
#define WM831X_BKUP_CHG_ENA                     0x8000  /* BKUP_CHG_ENA */
#define WM831X_BKUP_CHG_ENA_MASK                0x8000  /* BKUP_CHG_ENA */
#define WM831X_BKUP_CHG_ENA_SHIFT                   15  /* BKUP_CHG_ENA */
#define WM831X_BKUP_CHG_ENA_WIDTH                    1  /* BKUP_CHG_ENA */
#define WM831X_BKUP_CHG_STS                     0x4000  /* BKUP_CHG_STS */
#define WM831X_BKUP_CHG_STS_MASK                0x4000  /* BKUP_CHG_STS */
#define WM831X_BKUP_CHG_STS_SHIFT                   14  /* BKUP_CHG_STS */
#define WM831X_BKUP_CHG_STS_WIDTH                    1  /* BKUP_CHG_STS */
#define WM831X_BKUP_CHG_MODE                    0x1000  /* BKUP_CHG_MODE */
#define WM831X_BKUP_CHG_MODE_MASK               0x1000  /* BKUP_CHG_MODE */
#define WM831X_BKUP_CHG_MODE_SHIFT                  12  /* BKUP_CHG_MODE */
#define WM831X_BKUP_CHG_MODE_WIDTH                   1  /* BKUP_CHG_MODE */
#define WM831X_BKUP_BATT_DET_ENA                0x0800  /* BKUP_BATT_DET_ENA */
#define WM831X_BKUP_BATT_DET_ENA_MASK           0x0800  /* BKUP_BATT_DET_ENA */
#define WM831X_BKUP_BATT_DET_ENA_SHIFT              11  /* BKUP_BATT_DET_ENA */
#define WM831X_BKUP_BATT_DET_ENA_WIDTH               1  /* BKUP_BATT_DET_ENA */
#define WM831X_BKUP_BATT_STS                    0x0400  /* BKUP_BATT_STS */
#define WM831X_BKUP_BATT_STS_MASK               0x0400  /* BKUP_BATT_STS */
#define WM831X_BKUP_BATT_STS_SHIFT                  10  /* BKUP_BATT_STS */
#define WM831X_BKUP_BATT_STS_WIDTH                   1  /* BKUP_BATT_STS */
#define WM831X_BKUP_CHG_VLIM                    0x0010  /* BKUP_CHG_VLIM */
#define WM831X_BKUP_CHG_VLIM_MASK               0x0010  /* BKUP_CHG_VLIM */
#define WM831X_BKUP_CHG_VLIM_SHIFT                   4  /* BKUP_CHG_VLIM */
#define WM831X_BKUP_CHG_VLIM_WIDTH                   1  /* BKUP_CHG_VLIM */
#define WM831X_BKUP_CHG_ILIM_MASK               0x0003  /* BKUP_CHG_ILIM - [1:0] */
#define WM831X_BKUP_CHG_ILIM_SHIFT                   0  /* BKUP_CHG_ILIM - [1:0] */
#define WM831X_BKUP_CHG_ILIM_WIDTH                   2  /* BKUP_CHG_ILIM - [1:0] */

#endif
