/*
 * ICMPv6 tracking.
 *
 * 21 Apl 2004: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- separated from nf_conntrack_icmp.h
 *
 * Derived from include/linux/netfiter_ipv4/ip_conntrack_icmp.h
 */

#ifndef _NF_CONNTRACK_ICMPV6_H
#define _NF_CONNTRACK_ICMPV6_H
#include <asm/atomic.h>

#ifndef ICMPV6_NI_QUERY
#define ICMPV6_NI_QUERY 139
#endif
#ifndef ICMPV6_NI_REPLY
#define ICMPV6_NI_REPLY 140
#endif

struct nf_ct_icmpv6
{
	/* Optimization: when number in == number out, forget immediately. */
	atomic_t count;
};

#endif /* _NF_CONNTRACK_ICMPV6_H */
