/*
 * Copyright (c) 2011 Patrick Mchardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on Rusty Russell's IPv4 ICMP NAT code. Development of IPv6
 * NAT funded by Astaro.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/icmpv6.h>

#include <linux/netfilter.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_l3proto.h>
#include <net/netfilter/nf_nat_l4proto.h>

const struct nf_nat_l4proto nf_nat_l4proto_icmpv6 = {
	.l4proto		= IPPROTO_ICMPV6,
};
