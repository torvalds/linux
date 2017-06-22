/*
 * ILI9341 LCD controller
 *
 * Copyright 2016 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_ILI9341_H
#define __LINUX_ILI9341_H

#define ILI9341_FRMCTR1    0xb1
#define ILI9341_FRMCTR2    0xb2
#define ILI9341_FRMCTR3    0xb3
#define ILI9341_INVTR      0xb4
#define ILI9341_PRCTR      0xb5
#define ILI9341_DISCTRL    0xb6
#define ILI9341_ETMOD      0xb7

#define ILI9341_PWCTRL1    0xc0
#define ILI9341_PWCTRL2    0xc1
#define ILI9341_VMCTRL1    0xc5
#define ILI9341_VMCTRL2    0xc7
#define ILI9341_PWCTRLA    0xcb
#define ILI9341_PWCTRLB    0xcf

#define ILI9341_RDID1      0xda
#define ILI9341_RDID2      0xdb
#define ILI9341_RDID3      0xdc
#define ILI9341_RDID4      0xd3

#define ILI9341_PGAMCTRL   0xe0
#define ILI9341_NGAMCTRL   0xe1
#define ILI9341_DGAMCTRL1  0xe2
#define ILI9341_DGAMCTRL2  0xe3
#define ILI9341_DTCTRLA    0xe8
#define ILI9341_DTCTRLB    0xea
#define ILI9341_PWRSEQ     0xed

#define ILI9341_EN3GAM     0xf2
#define ILI9341_IFCTRL     0xf6
#define ILI9341_PUMPCTRL   0xf7

#define ILI9341_MADCTL_MH  BIT(2)
#define ILI9341_MADCTL_BGR BIT(3)
#define ILI9341_MADCTL_ML  BIT(4)
#define ILI9341_MADCTL_MV  BIT(5)
#define ILI9341_MADCTL_MX  BIT(6)
#define ILI9341_MADCTL_MY  BIT(7)

#endif /* __LINUX_ILI9341_H */
