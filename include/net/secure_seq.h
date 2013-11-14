#ifndef _NET_SECURE_SEQ
#define _NET_SECURE_SEQ

#include <linux/types.h>

__u32 secure_ip_id(__be32 daddr);
__u32 secure_ipv6_id(const __be32 daddr[4]);
u32 secure_ipv4_port_ephemeral(__be32 saddr, __be32 daddr, __be16 dport);
u32 secure_ipv6_port_ephemeral(const __be32 *saddr, const __be32 *daddr,
			       __be16 dport);
__u32 secure_tcp_sequence_number(__be32 saddr, __be32 daddr,
				 __be16 sport, __be16 dport);
__u32 secure_tcpv6_sequence_number(const __be32 *saddr, const __be32 *daddr,
				   __be16 sport, __be16 dport);
u64 secure_dccp_sequence_number(__be32 saddr, __be32 daddr,
				__be16 sport, __be16 dport);
u64 secure_dccpv6_sequence_number(__be32 *saddr, __be32 *daddr,
				  __be16 sport, __be16 dport);

#endif /* _NET_SECURE_SEQ */
