/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>

#include <net/netfilter/nf_nat_l4proto.h>


const struct nf_nat_l4proto nf_nat_l4proto_sctp = {
	.l4proto		= IPPROTO_SCTP,
};
