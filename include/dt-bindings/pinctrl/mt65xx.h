/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Hongzhou.Yang <hongzhou.yang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _DT_BINDINGS_PINCTRL_MT65XX_H
#define _DT_BINDINGS_PINCTRL_MT65XX_H

#define MTK_PIN_NO(x) ((x) << 8)
#define MTK_GET_PIN_NO(x) ((x) >> 8)
#define MTK_GET_PIN_FUNC(x) ((x) & 0xf)

#define MTK_PUPD_SET_R1R0_00 100
#define MTK_PUPD_SET_R1R0_01 101
#define MTK_PUPD_SET_R1R0_10 102
#define MTK_PUPD_SET_R1R0_11 103

#define MTK_DRIVE_2mA  2
#define MTK_DRIVE_4mA  4
#define MTK_DRIVE_6mA  6
#define MTK_DRIVE_8mA  8
#define MTK_DRIVE_10mA 10
#define MTK_DRIVE_12mA 12
#define MTK_DRIVE_14mA 14
#define MTK_DRIVE_16mA 16
#define MTK_DRIVE_20mA 20
#define MTK_DRIVE_24mA 24
#define MTK_DRIVE_28mA 28
#define MTK_DRIVE_32mA 32

#endif /* _DT_BINDINGS_PINCTRL_MT65XX_H */
