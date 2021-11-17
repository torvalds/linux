/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * rtc.h  --  RTC driver for Wolfson WM8350 PMIC
 *
 * Copyright 2007 Wolfson Microelectronics PLC
 */

#ifndef __LINUX_MFD_WM8350_RTC_H
#define __LINUX_MFD_WM8350_RTC_H

#include <linux/platform_device.h>

/*
 * Register values.
 */
#define WM8350_RTC_SECONDS_MINUTES              0x10
#define WM8350_RTC_HOURS_DAY                    0x11
#define WM8350_RTC_DATE_MONTH                   0x12
#define WM8350_RTC_YEAR                         0x13
#define WM8350_ALARM_SECONDS_MINUTES            0x14
#define WM8350_ALARM_HOURS_DAY                  0x15
#define WM8350_ALARM_DATE_MONTH                 0x16
#define WM8350_RTC_TIME_CONTROL                 0x17

/*
 * R16 (0x10) - RTC Seconds/Minutes
 */
#define WM8350_RTC_MINS_MASK                    0x7F00
#define WM8350_RTC_MINS_SHIFT                        8
#define WM8350_RTC_SECS_MASK                    0x007F
#define WM8350_RTC_SECS_SHIFT                        0

/*
 * R17 (0x11) - RTC Hours/Day
 */
#define WM8350_RTC_DAY_MASK                     0x0700
#define WM8350_RTC_DAY_SHIFT                         8
#define WM8350_RTC_HPM_MASK                     0x0020
#define WM8350_RTC_HPM_SHIFT                         5
#define WM8350_RTC_HRS_MASK                     0x001F
#define WM8350_RTC_HRS_SHIFT                         0

/* Bit values for R21 (0x15) */
#define WM8350_RTC_DAY_SUN                           1
#define WM8350_RTC_DAY_MON                           2
#define WM8350_RTC_DAY_TUE                           3
#define WM8350_RTC_DAY_WED                           4
#define WM8350_RTC_DAY_THU                           5
#define WM8350_RTC_DAY_FRI                           6
#define WM8350_RTC_DAY_SAT                           7

#define WM8350_RTC_HPM_AM                            0
#define WM8350_RTC_HPM_PM                            1

/*
 * R18 (0x12) - RTC Date/Month
 */
#define WM8350_RTC_MTH_MASK                     0x1F00
#define WM8350_RTC_MTH_SHIFT                         8
#define WM8350_RTC_DATE_MASK                    0x003F
#define WM8350_RTC_DATE_SHIFT                        0

/* Bit values for R22 (0x16) */
#define WM8350_RTC_MTH_JAN                           1
#define WM8350_RTC_MTH_FEB                           2
#define WM8350_RTC_MTH_MAR                           3
#define WM8350_RTC_MTH_APR                           4
#define WM8350_RTC_MTH_MAY                           5
#define WM8350_RTC_MTH_JUN                           6
#define WM8350_RTC_MTH_JUL                           7
#define WM8350_RTC_MTH_AUG                           8
#define WM8350_RTC_MTH_SEP                           9
#define WM8350_RTC_MTH_OCT                          10
#define WM8350_RTC_MTH_NOV                          11
#define WM8350_RTC_MTH_DEC                          12
#define WM8350_RTC_MTH_JAN_BCD                    0x01
#define WM8350_RTC_MTH_FEB_BCD                    0x02
#define WM8350_RTC_MTH_MAR_BCD                    0x03
#define WM8350_RTC_MTH_APR_BCD                    0x04
#define WM8350_RTC_MTH_MAY_BCD                    0x05
#define WM8350_RTC_MTH_JUN_BCD                    0x06
#define WM8350_RTC_MTH_JUL_BCD                    0x07
#define WM8350_RTC_MTH_AUG_BCD                    0x08
#define WM8350_RTC_MTH_SEP_BCD                    0x09
#define WM8350_RTC_MTH_OCT_BCD                    0x10
#define WM8350_RTC_MTH_NOV_BCD                    0x11
#define WM8350_RTC_MTH_DEC_BCD                    0x12

/*
 * R19 (0x13) - RTC Year
 */
#define WM8350_RTC_YHUNDREDS_MASK               0x3F00
#define WM8350_RTC_YHUNDREDS_SHIFT                   8
#define WM8350_RTC_YUNITS_MASK                  0x00FF
#define WM8350_RTC_YUNITS_SHIFT                      0

/*
 * R20 (0x14) - Alarm Seconds/Minutes
 */
#define WM8350_RTC_ALMMINS_MASK                 0x7F00
#define WM8350_RTC_ALMMINS_SHIFT                     8
#define WM8350_RTC_ALMSECS_MASK                 0x007F
#define WM8350_RTC_ALMSECS_SHIFT                     0

/* Bit values for R20 (0x14) */
#define WM8350_RTC_ALMMINS_DONT_CARE                -1
#define WM8350_RTC_ALMSECS_DONT_CARE                -1

/*
 * R21 (0x15) - Alarm Hours/Day
 */
#define WM8350_RTC_ALMDAY_MASK                  0x0F00
#define WM8350_RTC_ALMDAY_SHIFT                      8
#define WM8350_RTC_ALMHPM_MASK                  0x0020
#define WM8350_RTC_ALMHPM_SHIFT                      5
#define WM8350_RTC_ALMHRS_MASK                  0x001F
#define WM8350_RTC_ALMHRS_SHIFT                      0

/* Bit values for R21 (0x15) */
#define WM8350_RTC_ALMDAY_DONT_CARE                 -1
#define WM8350_RTC_ALMDAY_SUN                        1
#define WM8350_RTC_ALMDAY_MON                        2
#define WM8350_RTC_ALMDAY_TUE                        3
#define WM8350_RTC_ALMDAY_WED                        4
#define WM8350_RTC_ALMDAY_THU                        5
#define WM8350_RTC_ALMDAY_FRI                        6
#define WM8350_RTC_ALMDAY_SAT                        7

#define WM8350_RTC_ALMHPM_AM                         0
#define WM8350_RTC_ALMHPM_PM                         1

#define WM8350_RTC_ALMHRS_DONT_CARE                 -1

/*
 * R22 (0x16) - Alarm Date/Month
 */
#define WM8350_RTC_ALMMTH_MASK                  0x1F00
#define WM8350_RTC_ALMMTH_SHIFT                      8
#define WM8350_RTC_ALMDATE_MASK                 0x003F
#define WM8350_RTC_ALMDATE_SHIFT                     0

/* Bit values for R22 (0x16) */
#define WM8350_RTC_ALMDATE_DONT_CARE                -1

#define WM8350_RTC_ALMMTH_DONT_CARE                 -1
#define WM8350_RTC_ALMMTH_JAN                        1
#define WM8350_RTC_ALMMTH_FEB                        2
#define WM8350_RTC_ALMMTH_MAR                        3
#define WM8350_RTC_ALMMTH_APR                        4
#define WM8350_RTC_ALMMTH_MAY                        5
#define WM8350_RTC_ALMMTH_JUN                        6
#define WM8350_RTC_ALMMTH_JUL                        7
#define WM8350_RTC_ALMMTH_AUG                        8
#define WM8350_RTC_ALMMTH_SEP                        9
#define WM8350_RTC_ALMMTH_OCT                       10
#define WM8350_RTC_ALMMTH_NOV                       11
#define WM8350_RTC_ALMMTH_DEC                       12
#define WM8350_RTC_ALMMTH_JAN_BCD                 0x01
#define WM8350_RTC_ALMMTH_FEB_BCD                 0x02
#define WM8350_RTC_ALMMTH_MAR_BCD                 0x03
#define WM8350_RTC_ALMMTH_APR_BCD                 0x04
#define WM8350_RTC_ALMMTH_MAY_BCD                 0x05
#define WM8350_RTC_ALMMTH_JUN_BCD                 0x06
#define WM8350_RTC_ALMMTH_JUL_BCD                 0x07
#define WM8350_RTC_ALMMTH_AUG_BCD                 0x08
#define WM8350_RTC_ALMMTH_SEP_BCD                 0x09
#define WM8350_RTC_ALMMTH_OCT_BCD                 0x10
#define WM8350_RTC_ALMMTH_NOV_BCD                 0x11
#define WM8350_RTC_ALMMTH_DEC_BCD                 0x12

/*
 * R23 (0x17) - RTC Time Control
 */
#define WM8350_RTC_BCD                          0x8000
#define WM8350_RTC_BCD_MASK                     0x8000
#define WM8350_RTC_BCD_SHIFT                        15
#define WM8350_RTC_12HR                         0x4000
#define WM8350_RTC_12HR_MASK                    0x4000
#define WM8350_RTC_12HR_SHIFT                       14
#define WM8350_RTC_DST                          0x2000
#define WM8350_RTC_DST_MASK                     0x2000
#define WM8350_RTC_DST_SHIFT                        13
#define WM8350_RTC_SET                          0x0800
#define WM8350_RTC_SET_MASK                     0x0800
#define WM8350_RTC_SET_SHIFT                        11
#define WM8350_RTC_STS                          0x0400
#define WM8350_RTC_STS_MASK                     0x0400
#define WM8350_RTC_STS_SHIFT                        10
#define WM8350_RTC_ALMSET                       0x0200
#define WM8350_RTC_ALMSET_MASK                  0x0200
#define WM8350_RTC_ALMSET_SHIFT                      9
#define WM8350_RTC_ALMSTS                       0x0100
#define WM8350_RTC_ALMSTS_MASK                  0x0100
#define WM8350_RTC_ALMSTS_SHIFT                      8
#define WM8350_RTC_PINT                         0x0070
#define WM8350_RTC_PINT_MASK                    0x0070
#define WM8350_RTC_PINT_SHIFT                        4
#define WM8350_RTC_DSW                          0x000F
#define WM8350_RTC_DSW_MASK                     0x000F
#define WM8350_RTC_DSW_SHIFT                         0

/* Bit values for R23 (0x17) */
#define WM8350_RTC_BCD_BINARY                        0
#define WM8350_RTC_BCD_BCD                           1

#define WM8350_RTC_12HR_24HR                         0
#define WM8350_RTC_12HR_12HR                         1

#define WM8350_RTC_DST_DISABLED                      0
#define WM8350_RTC_DST_ENABLED                       1

#define WM8350_RTC_SET_RUN                           0
#define WM8350_RTC_SET_SET                           1

#define WM8350_RTC_STS_RUNNING                       0
#define WM8350_RTC_STS_STOPPED                       1

#define WM8350_RTC_ALMSET_RUN                        0
#define WM8350_RTC_ALMSET_SET                        1

#define WM8350_RTC_ALMSTS_RUNNING                    0
#define WM8350_RTC_ALMSTS_STOPPED                    1

#define WM8350_RTC_PINT_DISABLED                     0
#define WM8350_RTC_PINT_SECS                         1
#define WM8350_RTC_PINT_MINS                         2
#define WM8350_RTC_PINT_HRS                          3
#define WM8350_RTC_PINT_DAYS                         4
#define WM8350_RTC_PINT_MTHS                         5

#define WM8350_RTC_DSW_DISABLED                      0
#define WM8350_RTC_DSW_1HZ                           1
#define WM8350_RTC_DSW_2HZ                           2
#define WM8350_RTC_DSW_4HZ                           3
#define WM8350_RTC_DSW_8HZ                           4
#define WM8350_RTC_DSW_16HZ                          5
#define WM8350_RTC_DSW_32HZ                          6
#define WM8350_RTC_DSW_64HZ                          7
#define WM8350_RTC_DSW_128HZ                         8
#define WM8350_RTC_DSW_256HZ                         9
#define WM8350_RTC_DSW_512HZ                        10
#define WM8350_RTC_DSW_1024HZ                       11

/*
 * R218 (0xDA) - RTC Tick Control
 */
#define WM8350_RTC_TICKSTS                      0x4000
#define WM8350_RTC_CLKSRC                       0x2000
#define WM8350_RTC_TRIM_MASK                    0x03FF

/*
 * RTC Interrupts.
 */
#define WM8350_IRQ_RTC_PER			7
#define WM8350_IRQ_RTC_SEC			8
#define WM8350_IRQ_RTC_ALM			9

struct wm8350_rtc {
	struct platform_device *pdev;
	struct rtc_device *rtc;
	int alarm_enabled;      /* used over suspend/resume */
	int update_enabled;
};

#endif
