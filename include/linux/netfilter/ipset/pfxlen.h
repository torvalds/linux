#ifndef _PFXLEN_H
#define _PFXLEN_H

#include <asm/byteorder.h>
#include <linux/netfilter.h> 

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

#endif /*_PFXLEN_H */
