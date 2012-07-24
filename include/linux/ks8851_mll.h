/*
 * ks8861_mll platform data struct definition
 * Copyright (c) 2012 BTicino S.p.A.
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _LINUX_KS8851_MLL_H
#define _LINUX_KS8851_MLL_H

#include <linux/if_ether.h>

/**
 * struct ks8851_mll_platform_data - Platform data of the KS8851_MLL network driver
 * @macaddr:	The MAC address of the device, set to all 0:s to use the on in
 *		the chip.
 */
struct ks8851_mll_platform_data {
	u8 mac_addr[ETH_ALEN];
};

#endif
