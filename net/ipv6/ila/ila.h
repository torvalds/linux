/*
 * Copyright (c) 2015 Tom Herbert <tom@herbertland.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef __ILA_H
#define __ILA_H

#include <linux/errno.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <net/checksum.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <uapi/linux/ila.h>

struct ila_params {
	__be64 locator;
	__be64 locator_match;
	__wsum csum_diff;
};

static inline __wsum compute_csum_diff8(const __be32 *from, const __be32 *to)
{
	__be32 diff[] = {
		~from[0], ~from[1], to[0], to[1],
	};

	return csum_partial(diff, sizeof(diff), 0);
}

void update_ipv6_locator(struct sk_buff *skb, struct ila_params *p);

int ila_lwt_init(void);
void ila_lwt_fini(void);
int ila_xlat_init(void);
void ila_xlat_fini(void);

#endif /* __ILA_H */
