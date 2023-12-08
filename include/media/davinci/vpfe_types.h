/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2008-2009 Texas Instruments Inc
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
