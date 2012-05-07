/*
 * twl6030_madc.h - Header for TWL6030 MADC
 *
 * Copyright (C) 2011 Samsung Telecommunications of America
 *
 * Based on twl4030-madc.h
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * J Keerthy <j-keerthy@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef _TWL6030_MADC_H
#define _TWL6030_MADC_H

#define TWL6030_MADC_MAX_CHANNELS 17
/*
 * twl6030 madc occupies the same offset in the twl6030 map that
 * twl4030 madc does in the twl4030 map.
 * likewise the charger
 */
#define TWL6030_MODULE_MADC          TWL4030_MODULE_MADC
#define TWL6030_MODULE_MAIN_CHARGE   TWL4030_MODULE_MAIN_CHARGE

#define TWL6030_MADC_CTRL		0x00
#define    TWL6030_MADC_TEMP1_EN        (1 << 0)
#define    TWL6030_MADC_TEMP2_EN        (1 << 1)
#define    TWL6030_MADC_SCALER_EN_CH2	(1 << 2)
#define    TWL6030_MADC_VBAT_SCALER_DIV	(1 << 3)
#define    TWL6030_MADC_SCALER_EN_CH11	(1 << 4)
#define    TWL6030_MADC_TMP1_EN_MONITOR	(1 << 5)
#define    TWL6030_MADC_TMP2_EN_MONITOR	(1 << 6)
#define    TWL6030_MADC_ISOURCE_EN	(1 << 7)

#define TWL6030_MADC_RTSELECT_LSB	0x02
#define    TWL6030_MADC_ADCIN0	(1 << 0)
#define    TWL6030_MADC_ADCIN1	(1 << 1)
#define    TWL6030_MADC_ADCIN2	(1 << 2)
#define    TWL6030_MADC_ADCIN3	(1 << 3)
#define    TWL6030_MADC_ADCIN4	(1 << 4)
#define    TWL6030_MADC_ADCIN5	(1 << 5)
#define    TWL6030_MADC_ADCIN6	(1 << 6)
#define    TWL6030_MADC_ADCIN7	(1 << 7)

#define TWL6030_MADC_RTSELECT_ISB	0x03
#define    TWL6030_MADC_ADCIN8		(1 << 0)
#define    TWL6030_MADC_ADCIN9		(1 << 1)
#define    TWL6030_MADC_ADCIN10		(1 << 2)
#define    TWL6030_MADC_ADCIN11		(1 << 3)
#define    TWL6030_MADC_ADCIN12		(1 << 4)
#define    TWL6030_MADC_ADCIN13		(1 << 5)
#define    TWL6030_MADC_ADCIN14		(1 << 6)
#define    TWL6030_MADC_ADCIN15		(1 << 7)

#define TWL6030_MADC_RTSELECT_MSB	0x04
#define    TWL6030_MADC_ADCIN16		(1 << 0)

#define TWL6030_MADC_CTRL_P1		0x05
#define    TWL6030_MADC_BUSY		(1 << 0)
#define    TWL6030_MADC_EOCP1		(1 << 1)
#define    TWL6030_MADC_EOCRT		(1 << 2)
#define    TWL6030_MADC_SP1		(1 << 3)

#define TWL6030_MADC_CTRL_P2		0x06
#define    TWL6030_MADC_BUSYB		(1 << 0)
#define    TWL6030_MADC_EOCP2		(1 << 1)
#define    TWL6030_MADC_SP2		(1 << 2)

#define TWL6030_MADC_RTCH0_LSB		0x07
#define TWL6030_MADC_GPCH0_LSB		0x29

int twl6030_get_madc_conversion(int channel_no);
#endif
