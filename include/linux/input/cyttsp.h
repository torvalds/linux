/*
 * Header file for:
 * Cypress TrueTouch(TM) Standard Product (TTSP) touchscreen drivers.
 * For use with Cypress Txx3xx parts.
 * Supported parts include:
 * CY8CTST341
 * CY8CTMA340
 *
 * Copyright (C) 2009, 2010, 2011 Cypress Semiconductor, Inc.
 * Copyright (C) 2012 Javier Martinez Canillas <javier@dowhile0.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contact Cypress Semiconductor at www.cypress.com (kev@cypress.com)
 *
 */
#ifndef _CYTTSP_H_
#define _CYTTSP_H_

#define CY_SPI_NAME "cyttsp-spi"
#define CY_I2C_NAME "cyttsp-i2c"
/* Active Power state scanning/processing refresh interval */
#define CY_ACT_INTRVL_DFLT 0x00 /* ms */
/* touch timeout for the Active power */
#define CY_TCH_TMOUT_DFLT 0xFF /* ms */
/* Low Power state scanning/processing refresh interval */
#define CY_LP_INTRVL_DFLT 0x0A /* ms */
/* Active distance in pixels for a gesture to be reported */
#define CY_ACT_DIST_DFLT 0xF8 /* pixels */

struct cyttsp_platform_data {
	u32 maxx;
	u32 maxy;
	bool use_hndshk;
	u8 act_dist;	/* Active distance */
	u8 act_intrvl;  /* Active refresh interval; ms */
	u8 tch_tmout;   /* Active touch timeout; ms */
	u8 lp_intrvl;   /* Low power refresh interval; ms */
	int (*init)(void);
	void (*exit)(void);
	char *name;
	s16 irq_gpio;
	u8 *bl_keys;
};

#endif /* _CYTTSP_H_ */
