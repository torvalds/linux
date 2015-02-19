/*
 * DRV260X haptics driver family
 *
 * Author: Dan Murphy <dmurphy@ti.com>
 *
 * Copyright:   (C) 2014 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef _DT_BINDINGS_TI_DRV260X_H
#define _DT_BINDINGS_TI_DRV260X_H

/* Calibration Types */
#define DRV260X_LRA_MODE		0x00
#define DRV260X_LRA_NO_CAL_MODE	0x01
#define DRV260X_ERM_MODE		0x02

/* Library Selection */
#define DRV260X_LIB_EMPTY			0x00
#define DRV260X_ERM_LIB_A			0x01
#define DRV260X_ERM_LIB_B			0x02
#define DRV260X_ERM_LIB_C			0x03
#define DRV260X_ERM_LIB_D			0x04
#define DRV260X_ERM_LIB_E			0x05
#define DRV260X_LIB_LRA			0x06
#define DRV260X_ERM_LIB_F			0x07

#endif
