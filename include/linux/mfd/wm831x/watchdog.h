/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * include/linux/mfd/wm831x/watchdog.h -- Watchdog for WM831x
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#ifndef __MFD_WM831X_WATCHDOG_H__
#define __MFD_WM831X_WATCHDOG_H__


/*
 * R16388 (0x4004) - Watchdog
 */
#define WM831X_WDOG_ENA                         0x8000  /* WDOG_ENA */
#define WM831X_WDOG_ENA_MASK                    0x8000  /* WDOG_ENA */
#define WM831X_WDOG_ENA_SHIFT                       15  /* WDOG_ENA */
#define WM831X_WDOG_ENA_WIDTH                        1  /* WDOG_ENA */
#define WM831X_WDOG_DEBUG                       0x4000  /* WDOG_DEBUG */
#define WM831X_WDOG_DEBUG_MASK                  0x4000  /* WDOG_DEBUG */
#define WM831X_WDOG_DEBUG_SHIFT                     14  /* WDOG_DEBUG */
#define WM831X_WDOG_DEBUG_WIDTH                      1  /* WDOG_DEBUG */
#define WM831X_WDOG_RST_SRC                     0x2000  /* WDOG_RST_SRC */
#define WM831X_WDOG_RST_SRC_MASK                0x2000  /* WDOG_RST_SRC */
#define WM831X_WDOG_RST_SRC_SHIFT                   13  /* WDOG_RST_SRC */
#define WM831X_WDOG_RST_SRC_WIDTH                    1  /* WDOG_RST_SRC */
#define WM831X_WDOG_SLPENA                      0x1000  /* WDOG_SLPENA */
#define WM831X_WDOG_SLPENA_MASK                 0x1000  /* WDOG_SLPENA */
#define WM831X_WDOG_SLPENA_SHIFT                    12  /* WDOG_SLPENA */
#define WM831X_WDOG_SLPENA_WIDTH                     1  /* WDOG_SLPENA */
#define WM831X_WDOG_RESET                       0x0800  /* WDOG_RESET */
#define WM831X_WDOG_RESET_MASK                  0x0800  /* WDOG_RESET */
#define WM831X_WDOG_RESET_SHIFT                     11  /* WDOG_RESET */
#define WM831X_WDOG_RESET_WIDTH                      1  /* WDOG_RESET */
#define WM831X_WDOG_SECACT_MASK                 0x0300  /* WDOG_SECACT - [9:8] */
#define WM831X_WDOG_SECACT_SHIFT                     8  /* WDOG_SECACT - [9:8] */
#define WM831X_WDOG_SECACT_WIDTH                     2  /* WDOG_SECACT - [9:8] */
#define WM831X_WDOG_PRIMACT_MASK                0x0030  /* WDOG_PRIMACT - [5:4] */
#define WM831X_WDOG_PRIMACT_SHIFT                    4  /* WDOG_PRIMACT - [5:4] */
#define WM831X_WDOG_PRIMACT_WIDTH                    2  /* WDOG_PRIMACT - [5:4] */
#define WM831X_WDOG_TO_MASK                     0x0007  /* WDOG_TO - [2:0] */
#define WM831X_WDOG_TO_SHIFT                         0  /* WDOG_TO - [2:0] */
#define WM831X_WDOG_TO_WIDTH                         3  /* WDOG_TO - [2:0] */

#endif
