/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/include/net/ethoc.h
 *
 * Copyright (C) 2008-2009 Avionic Design GmbH
 *
 * Written by Thierry Reding <thierry.reding@avionic-design.de>
 */

#ifndef LINUX_NET_ETHOC_H
#define LINUX_NET_ETHOC_H 1

#include <linux/if.h>
#include <linux/types.h>

struct ethoc_platform_data {
	u8 hwaddr[IFHWADDRLEN];
	s8 phy_id;
	u32 eth_clkfreq;
	bool big_endian;
};

#endif /* !LINUX_NET_ETHOC_H */
