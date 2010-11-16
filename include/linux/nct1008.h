/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#ifndef _LINUX_NCT1008_H__
#define _LINUX_NCT1008_H__

/* NCT1008 Read Only Registers */
#define NCT_LOCAL_TEMP_RD			0x00
#define NCT_EXT_TEMP_HIGH_RD			0x01
#define NCT_EXT_TEMP_LOW_RD			0x10
#define NCT_STATUS_RD				0x02

/* NCT1008 Control Registers */
#define NCT_CONFIG_RD				0x03
#define NCT_CONFIG_WR				0x09
#define NCT_CONV_RATE_RD			0x04
#define NCT_CONV_RATE_WR			0x0A

/* NCT1008 Limit Registers */
#define NCT_LOCAL_TEMP_HIGH_LIMIT_RD		0x05
#define NCT_LOCAL_TEMP_LOW_LIMIT_RD		0x06
#define NCT_LOCAL_TEMP_HIGH_LIMIT_WR		0x0B
#define NCT_LOCAL_TEMP_LOW_LIMIT_WR		0x0C

#define NCT_EXT_TEMP_HIGH_LIMIT_HBYTE_RD	0x07
#define NCT_EXT_TEMP_LOW_LIMIT_HBYTE_RD		0x08
#define NCT_EXT_TEMP_HIGH_LIMIT_HBYTE_WR	0x0D
#define NCT_EXT_TEMP_LOW_LIMIT_HBYTE_WR		0x0E
#define NCT_EXT_TEMP_HIGH_LIMIT_LBYTE_RDWR	0x13
#define NCT_EXT_TEMP_LOW_LIMIT_LBYTE_RDWR	0x14
#define NCT_EXT_THERM_LIMIT			0x19
#define NCT_LOCAL_THERM_LIMIT			0x20

#define NCT_EXT_TEMP_OFFSET_HIGH_RDWR		0x11
#define NCT_EXT_TEMP_OFFSET_LOW_RDWR		0x12
#define NCT_THERM_HYST				0x21
#define NCT_CONSEC_ALERT			0x22

#endif	/* _LINUX_NCT1008_H__ */
