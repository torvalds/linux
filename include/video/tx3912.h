/*
 * linux/include/video/tx3912.h
 *
 * Copyright (C) 2001 Steven Hill (sjhill@realitydiluted.com)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 *
 * Includes for TMPR3912/05 and PR31700 LCD controller registers
 */
#include <asm/tx3912.h>

#define VidCtrl1        REG_AT(0x028)
#define VidCtrl2        REG_AT(0x02C)
#define VidCtrl3        REG_AT(0x030)
#define VidCtrl4        REG_AT(0x034)
#define VidCtrl5        REG_AT(0x038)
#define VidCtrl6        REG_AT(0x03C)
#define VidCtrl7        REG_AT(0x040)
#define VidCtrl8        REG_AT(0x044)
#define VidCtrl9        REG_AT(0x048)
#define VidCtrl10       REG_AT(0x04C)
#define VidCtrl11       REG_AT(0x050)
#define VidCtrl12       REG_AT(0x054)
#define VidCtrl13       REG_AT(0x058)
#define VidCtrl14       REG_AT(0x05C)

/* Video Control 1 Register */
#define LINECNT         0xffc00000
#define LINECNT_SHIFT   22
#define LOADDLY         BIT(21)
#define BAUDVAL         (BIT(20) | BIT(19) | BIT(18) | BIT(17) | BIT(16))
#define BAUDVAL_SHIFT   16
#define VIDDONEVAL      (BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10) | BIT(9))
#define VIDDONEVAL_SHIFT  9
#define ENFREEZEFRAME   BIT(8)
#define TX3912_VIDCTRL1_BITSEL_MASK	0x000000c0
#define TX3912_VIDCTRL1_2BIT_GRAY	0x00000040
#define TX3912_VIDCTRL1_4BIT_GRAY	0x00000080
#define TX3912_VIDCTRL1_8BIT_COLOR	0x000000c0
#define BITSEL_SHIFT    6
#define DISPSPLIT       BIT(5)
#define DISP8           BIT(4)
#define DFMODE          BIT(3)
#define INVVID          BIT(2)
#define DISPON          BIT(1)
#define ENVID           BIT(0)

/* Video Control 2 Register */
#define VIDRATE_MASK    0xffc00000
#define VIDRATE_SHIFT   22
#define HORZVAL_MASK    0x001ff000
#define HORZVAL_SHIFT   12
#define LINEVAL_MASK    0x000001ff

/* Video Control 3 Register */
#define TX3912_VIDCTRL3_VIDBANK_MASK    0xfff00000
#define TX3912_VIDCTRL3_VIDBASEHI_MASK  0x000ffff0

/* Video Control 4 Register */
#define TX3912_VIDCTRL4_VIDBASELO_MASK  0x000ffff0
