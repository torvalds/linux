/*
 *	IPV6 GSO/GRO offload support
 *	Linux INET6 implementation
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
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

int __init ipv6_exthdrs_offload_init(void)
{
	int ret;

	ret = inet6_add_offload(&rthdr_offload, IPPROTO_ROUTING);
	if (ret)
		goto out;

	ret = inet6_add_offload(&dstopt_offload, IPPROTO_DSTOPTS);
	if (ret)
		goto out_rt;

out:
	return ret;

out_rt:
	inet6_del_offload(&rthdr_offload, IPPROTO_ROUTING);
	goto out;
}
