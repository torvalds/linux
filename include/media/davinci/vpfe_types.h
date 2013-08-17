/*
 * Copyright (C) 2008-2009 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option)any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef _VPFE_TYPES_H
#define _VPFE_TYPES_H

#ifdef __KERNEL__

enum vpfe_pin_pol {
	VPFE_PINPOL_POSITIVE,
	VPFE_PINPOL_NEGATIVE
};

enum vpfe_hw_if_type {
	/* BT656 - 8 bit */
	VPFE_BT656,
	/* BT1120 - 16 bit */
	VPFE_BT1120,
	/* Raw Bayer */
	VPFE_RAW_BAYER,
	/* YCbCr - 8 bit with external sync */
	VPFE_YCBCR_SYNC_8,
	/* YCbCr - 16 bit with external sync */
	VPFE_YCBCR_SYNC_16,
	/* BT656 - 10 bit */
	VPFE_BT656_10BIT
};

/* interface description */
struct vpfe_hw_if_param {
	enum vpfe_hw_if_type if_type;
	enum vpfe_pin_pol hdpol;
	enum vpfe_pin_pol vdpol;
};

#endif
#endif
