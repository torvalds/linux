/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com> and was subsequently ported
 * to FreeBSD by Michael Gmelin <freebsd@grem.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_DEV_SMBUS_CYAPA_CYAPA_H_
#define _SYS_DEV_SMBUS_CYAPA_CYAPA_H_

#define CYAPA_MAX_MT    5

/*
 * Boot-time registers.  This is the device map
 * if (stat & CYAPA_STAT_RUNNING) is 0.
 */
struct cyapa_boot_regs {
	uint8_t stat;			/* CYAPA_STAT_xxx */
	uint8_t boot;			/* CYAPA_BOOT_xxx */
	uint8_t error;
} __packed;

#define CYAPA_BOOT_BUSY		0x80
#define CYAPA_BOOT_RUNNING	0x10
#define CYAPA_BOOT_DATA_VALID	0x08
#define CYAPA_BOOT_CSUM_VALID	0x01

#define CYAPA_ERROR_INVALID     0x80
#define CYAPA_ERROR_INVALID_KEY 0x40
#define CYAPA_ERROR_BOOTLOADER	0x20
#define CYAPA_ERROR_CMD_CSUM    0x10
#define CYAPA_ERROR_FLASH_PROT  0x08
#define CYAPA_ERROR_FLASH_CSUM  0x04

struct cyapa_regs {
	uint8_t stat;
	uint8_t fngr;

	struct {
		uint8_t xy_high;        /* 7:4 high 4 bits of x */
		uint8_t x_low;          /* 3:0 high 4 bits of y */
		uint8_t y_low;
		uint8_t pressure;
		uint8_t id;             /* 1-15 incremented each touch */
	} touch[CYAPA_MAX_MT];
} __packed;

struct cyapa_cap {
	uint8_t prod_ida[5];    /* 0x00 - 0x04 */
	uint8_t prod_idb[6];    /* 0x05 - 0x0A */
	uint8_t prod_idc[2];    /* 0x0B - 0x0C */
	uint8_t reserved[6];    /* 0x0D - 0x12 */
	uint8_t buttons;        /* 0x13 */
	uint8_t gen;            /* 0x14, low 4 bits */
	uint8_t max_abs_xy_high;/* 0x15 7:4 high x bits, 3:0 high y bits */
	uint8_t max_abs_x_low;  /* 0x16 */
	uint8_t max_abs_y_low;  /* 0x17 */
	uint8_t phy_siz_xy_high;/* 0x18 7:4 high x bits, 3:0 high y bits */
	uint8_t phy_siz_x_low;  /* 0x19 */
	uint8_t phy_siz_y_low;  /* 0x1A */
} __packed;

#define CYAPA_STAT_RUNNING      0x80
#define CYAPA_STAT_PWR_MASK     0x0C
#define  CYAPA_PWR_OFF          0x00
#define  CYAPA_PWR_IDLE         0x08
#define  CYAPA_PWR_ACTIVE       0x0C

#define CYAPA_STAT_DEV_MASK     0x03
#define  CYAPA_DEV_NORMAL       0x03
#define  CYAPA_DEV_BUSY         0x01

#define CYAPA_FNGR_DATA_VALID   0x08
#define CYAPA_FNGR_MIDDLE       0x04
#define CYAPA_FNGR_RIGHT        0x02
#define CYAPA_FNGR_LEFT         0x01
#define CYAPA_FNGR_NUMFINGERS(c) (((c) >> 4) & 0x0F)

#define CYAPA_TOUCH_X(regs, i)  ((((regs)->touch[i].xy_high << 4) & 0x0F00) | \
				  (regs)->touch[i].x_low)
#define CYAPA_TOUCH_Y(regs, i)  ((((regs)->touch[i].xy_high << 8) & 0x0F00) | \
				  (regs)->touch[i].y_low)
#define CYAPA_TOUCH_P(regs, i)  ((regs)->touch[i].pressure)

#define CMD_BOOT_STATUS		0x00	/* only if in boot state */
#define CMD_DEV_STATUS          0x00	/* only if in operational state */
#define CMD_SOFT_RESET          0x28
#define CMD_POWER_MODE          0x29
#define  CMD_POWER_MODE_OFF	0x00
#define  CMD_POWER_MODE_IDLE	0x14
#define  CMD_POWER_MODE_FULL	0xFC
#define CMD_QUERY_CAPABILITIES  0x2A

#endif
