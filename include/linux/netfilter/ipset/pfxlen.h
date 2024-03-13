/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PFXLEN_H
#define _PFXLEN_H

#include <asm/byteorder.h>
#include <linux/netfilter.h>
#include <net/tcp.h>

/* Prefixlen maps, by Jan Engelhardt  */
extern const union nf_inet_addr ip_set_netmask_map[];
extern const union nf_inet_addr ip_set_hostmask_map[];

static inline __be32
ip_set_netmask(u8 pfxlen)
{
	return ip_set_netmask_map[pfxlen].ip;
}

static inline const __be32 *
ip_set_netmask6(u8 pfxlen)
{
	return &ip_set_netmask_map[pfxlen].ip6[0];
}

static inline u32
ip_set_hostmask(u8 pfxlen)
{
	return (__force u32) ip_set_hostmask_map[pfxlen].ip;
}

static inline const __be32 *
ip_set_hostmask6(u8 pfxlen)
{
	return &ip_set_hostmask_map[pfxlen].ip6[0];
}

extern u32 ip_set_range_to_cidr(u32 from, u32 to, u8 *cidr);

#define ip_set_mask_from_to(from, to, cidr)	\
do {						\
	from &= ip_set_hostmask(cidr);		\
	to = from | ~ip_set_hostmask(cidr);	\
} while (0)

static inline void
ip6_netmask(union nf_inet_addr *ip, u8 prefix)
{
	ip->ip6[0] &= ip_set_netmask6(prefix)[0];
	ip->ip6[1] &= ip_set_netmask6(prefix)[1];
	ip->ip6[2] &= ip_set_netmask6(prefix)[2];
	ip->ip6[3] &= ip_set_netmask6(prefix)[3];
}

#endif /*_PFXLEN_H */
