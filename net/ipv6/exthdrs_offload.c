// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	IPV6 GSO/GRO offload support
 *	Linux INET6 implementation
 *
 *      IPV6 Extension Header GSO/GRO support
 */
#include <net/protocol.h>
#include "ip6_offload.h"

static const struct net_offload rthdr_offload = {
	.flags		=	INET6_PROTO_GSO_EXTHDR,
};

static const struct net_offload dstopt_offload = {
	.flags		=	INET6_PROTO_GSO_EXTHDR,
};

static const struct net_offload hbh_offload = {
	.flags		=	INET6_PROTO_GSO_EXTHDR,
};

int __init ipv6_exthdrs_offload_init(void)
{
	int ret;

	ret = inet6_add_offload(&rthdr_offload, IPPROTO_ROUTING);
	if (ret)
		goto out;

	ret = inet6_add_offload(&dstopt_offload, IPPROTO_DSTOPTS);
	if (ret)
		goto out_rt;

	ret = inet6_add_offload(&hbh_offload, IPPROTO_HOPOPTS);
	if (ret)
		goto out_dstopts;

out:
	return ret;

out_dstopts:
	inet6_del_offload(&dstopt_offload, IPPROTO_DSTOPTS);

out_rt:
	inet6_del_offload(&rthdr_offload, IPPROTO_ROUTING);
	goto out;
}
