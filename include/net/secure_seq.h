/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_SECURE_SEQ
#define _NET_SECURE_SEQ

#include <linux/types.h>

struct net;
extern struct net init_net;

union tcp_seq_and_ts_off {
	struct {
		u32 seq;
		u32 ts_off;
	};
	u64 hash64;
};

u64 secure_ipv4_port_ephemeral(__be32 saddr, __be32 daddr, __be16 dport);
u64 secure_ipv6_port_ephemeral(const __be32 *saddr, const __be32 *daddr,
			       __be16 dport);
union tcp_seq_and_ts_off
secure_tcp_seq_and_ts_off(const struct net *net, __be32 saddr, __be32 daddr,
			  __be16 sport, __be16 dport);

static inline u32 secure_tcp_seq(__be32 saddr, __be32 daddr,
				 __be16 sport, __be16 dport)
{
	union tcp_seq_and_ts_off ts;

	ts = secure_tcp_seq_and_ts_off(&init_net, saddr, daddr,
				       sport, dport);

	return ts.seq;
}

union tcp_seq_and_ts_off
secure_tcpv6_seq_and_ts_off(const struct net *net, const __be32 *saddr,
			    const __be32 *daddr,
			    __be16 sport, __be16 dport);

static inline u32 secure_tcpv6_seq(const __be32 *saddr, const __be32 *daddr,
				   __be16 sport, __be16 dport)
{
	union tcp_seq_and_ts_off ts;

	ts = secure_tcpv6_seq_and_ts_off(&init_net, saddr, daddr,
					 sport, dport);

	return ts.seq;
}
#endif /* _NET_SECURE_SEQ */
