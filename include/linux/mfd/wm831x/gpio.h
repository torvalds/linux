/*
 * include/linux/mfd/wm831x/gpio.h -- GPIO for WM831x
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

#ifndef __MFD_WM831X_GPIO_H__
#define __MFD_WM831X_GPIO_H__

/*
 * R16440-16455 (0x4038-0x4047) - GPIOx Control
 */
#define WM831X_GPN_DIR                          0x8000  /* GPN_DIR */
#define WM831X_GPN_DIR_MASK                     0x8000  /* GPN_DIR */
#define WM831X_GPN_DIR_SHIFT                        15  /* GPN_DIR */
#define WM831X_GPN_DIR_WIDTH                         1  /* GPN_DIR */
#define WM831X_GPN_PULL_MASK                    0x6000  /* GPN_PULL - [14:13] */
#define WM831X_GPN_PULL_SHIFT                       13  /* GPN_PULL - [14:13] */
#define WM831X_GPN_PULL_WIDTH                        2  /* GPN_PULL - [14:13] */
#define WM831X_GPN_INT_MODE                     0x1000  /* GPN_INT_MODE */
#define WM831X_GPN_INT_MODE_MASK                0x1000  /* GPN_INT_MODE */
#define WM831X_GPN_INT_MODE_SHIFT                   12  /* GPN_INT_MODE */
#define WM831X_GPN_INT_MODE_WIDTH                    1  /* GPN_INT_MODE */
#define WM831X_GPN_PWR_DOM                      0x0800  /* GPN_PWR_DOM */
#define WM831X_GPN_PWR_DOM_MASK                 0x0800  /* GPN_PWR_DOM */
#define WM831X_GPN_PWR_DOM_SHIFT                    11  /* GPN_PWR_DOM */
#define WM831X_GPN_PWR_DOM_WIDTH                     1  /* GPN_PWR_DOM */
#define WM831X_GPN_POL                          0x0400  /* GPN_POL */
#define WM831X_GPN_POL_MASK                     0x0400  /* GPN_POL */
#define WM831X_GPN_POL_SHIFT                        10  /* GPN_POL */
#define WM831X_GPN_POL_WIDTH                         1  /* GPN_POL */
#define WM831X_GPN_OD                           0x0200  /* GPN_OD */
#define WM831X_GPN_OD_MASK                      0x0200  /* GPN_OD */
#define WM831X_GPN_OD_SHIFT                          9  /* GPN_OD */
#define WM831X_GPN_OD_WIDTH                          1  /* GPN_OD */
#define WM831X_GPN_ENA                          0x0080  /* GPN_ENA */
#define WM831X_GPN_ENA_MASK                     0x0080  /* GPN_ENA */
#define WM831X_GPN_ENA_SHIFT                         7  /* GPN_ENA */
#define WM831X_GPN_ENA_WIDTH                         1  /* GPN_ENA */
#define WM831X_GPN_TRI                          0x0080  /* GPN_TRI */
#define WM831X_GPN_TRI_MASK                     0x0080  /* GPN_TRI */
#define WM831X_GPN_TRI_SHIFT                         7  /* GPN_TRI */
#define WM831X_GPN_TRI_WIDTH                         1  /* GPN_TRI */
#define WM831X_GPN_FN_MASK                      0x000F  /* GPN_FN - [3:0] */
#define WM831X_GPN_FN_SHIFT                          0  /* GPN_FN - [3:0] */
#define WM831X_GPN_FN_WIDTH                          4  /* GPN_FN - [3:0] */

#define WM831X_GPIO_PULL_NONE (0 << WM831X_GPN_PULL_SHIFT)
#define WM831X_GPIO_PULL_DOWN (1 << WM831X_GPN_PULL_SHIFT)
#define WM831X_GPIO_PULL_UP   (2 << WM831X_GPN_PULL_SHIFT)
#endif
