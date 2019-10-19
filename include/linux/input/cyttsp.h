/* SPDX-License-Identifier: GPL-2.0-only */
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
 * Contact Cypress Semiconductor at www.cypress.com (kev@cypress.com)
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

#endif /* _CYTTSP_H_ */
