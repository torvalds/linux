/*
 * include/video/epson13xx.h -- Epson 13xx frame buffer
 *
 * Copyright (C) Hewlett-Packard Company.  All rights reserved.
 *
 * Written by Christopher Hoover <ch@hpl.hp.com>
 *
 */

#ifndef _EPSON13XX_H_
#define _EPSON13XX_H_

#define REG_REVISION_CODE              0x00
#define REG_MEMORY_CONFIG              0x01
#define REG_PANEL_TYPE                 0x02
#define REG_MOD_RATE                   0x03
#define REG_HORZ_DISP_WIDTH            0x04
#define REG_HORZ_NONDISP_PERIOD        0x05
#define REG_HRTC_START_POSITION        0x06
#define REG_HRTC_PULSE_WIDTH           0x07
#define REG_VERT_DISP_HEIGHT0          0x08
#define REG_VERT_DISP_HEIGHT1          0x09
#define REG_VERT_NONDISP_PERIOD        0x0A
#define REG_VRTC_START_POSITION        0x0B
#define REG_VRTC_PULSE_WIDTH           0x0C
#define REG_DISPLAY_MODE               0x0D
#define REG_SCRN1_LINE_COMPARE0        0x0E
#define REG_SCRN1_LINE_COMPARE1        0x0F
#define REG_SCRN1_DISP_START_ADDR0     0x10
#define REG_SCRN1_DISP_START_ADDR1     0x11
#define REG_SCRN1_DISP_START_ADDR2     0x12
#define REG_SCRN2_DISP_START_ADDR0     0x13
#define REG_SCRN2_DISP_START_ADDR1     0x14
#define REG_SCRN2_DISP_START_ADDR2     0x15
#define REG_MEM_ADDR_OFFSET0           0x16
#define REG_MEM_ADDR_OFFSET1           0x17
#define REG_PIXEL_PANNING              0x18
#define REG_CLOCK_CONFIG               0x19
#define REG_POWER_SAVE_CONFIG          0x1A
#define REG_MISC                       0x1B
#define REG_MD_CONFIG_READBACK0        0x1C
#define REG_MD_CONFIG_READBACK1        0x1D
#define REG_GPIO_CONFIG0               0x1E
#define REG_GPIO_CONFIG1               0x1F
#define REG_GPIO_CONTROL0              0x20
#define REG_GPIO_CONTROL1              0x21
#define REG_PERF_ENHANCEMENT0          0x22
#define REG_PERF_ENHANCEMENT1          0x23
#define REG_LUT_ADDR                   0x24
#define REG_RESERVED_1                 0x25
#define REG_LUT_DATA                   0x26
#define REG_INK_CURSOR_CONTROL         0x27
#define REG_CURSOR_X_POSITION0         0x28
#define REG_CURSOR_X_POSITION1         0x29
#define REG_CURSOR_Y_POSITION0         0x2A
#define REG_CURSOR_Y_POSITION1         0x2B
#define REG_INK_CURSOR_COLOR0_0        0x2C
#define REG_INK_CURSOR_COLOR0_1        0x2D
#define REG_INK_CURSOR_COLOR1_0        0x2E
#define REG_INK_CURSOR_COLOR1_1        0x2F
#define REG_INK_CURSOR_START_ADDR      0x30
#define REG_ALTERNATE_FRM              0x31

#endif
