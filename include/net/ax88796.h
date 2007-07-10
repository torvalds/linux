/* include/net/ax88796.h
 *
 * Copyright 2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#ifndef __NET_AX88796_PLAT_H
#define __NET_AX88796_PLAT_H

#define AXFLG_HAS_EEPROM		(1<<0)
#define AXFLG_MAC_FROMDEV		(1<<1)	/* device already has MAC */

struct ax_plat_data {
	unsigned int	 flags;
	unsigned char	 wordlength;		/* 1 or 2 */
	unsigned char	 dcr_val;		/* default value for DCR */
	unsigned char	 rcr_val;		/* default value for RCR */
	unsigned char	 gpoc_val;		/* default value for GPOC */
	u32		*reg_offsets;		/* register offsets */
};

#endif /* __NET_AX88796_PLAT_H */
