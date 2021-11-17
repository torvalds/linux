/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ks8861_mll platform data struct definition
 * Copyright (c) 2012 BTicino S.p.A.
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
