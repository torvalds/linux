/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */

/* Included by drivers/net/dsa/sja1105/sja1105.h */

#ifndef _NET_DSA_SJA1105_H
#define _NET_DSA_SJA1105_H

#include <linux/etherdevice.h>

#define ETH_P_SJA1105				ETH_P_DSA_8021Q

/* The switch can only be convinced to stay in unmanaged mode and not trap any
 * link-local traffic by actually telling it to filter frames sent at the
 * 00:00:00:00:00:00 destination MAC.
 */
#define SJA1105_LINKLOCAL_FILTER_A		0x000000000000ull
#define SJA1105_LINKLOCAL_FILTER_A_MASK		0xFFFFFFFFFFFFull
#define SJA1105_LINKLOCAL_FILTER_B		0x000000000000ull
#define SJA1105_LINKLOCAL_FILTER_B_MASK		0xFFFFFFFFFFFFull

#endif /* _NET_DSA_SJA1105_H */
