#ifndef _NET_FLOW_KEYS_H
#define _NET_FLOW_KEYS_H

/* struct flow_keys:
 *	@src: source ip address in case of IPv4
 *	      For IPv6 it contains 32bit hash of src address
 *	@dst: destination ip address in case of IPv4
 *	      For IPv6 it contains 32bit hash of dst address
 *	@ports: port numbers of Transport header
 *		port16[0]: src port number
 *		port16[1]: dst port number
 *	@thoff: Transport header offset
 *	@n_proto: Network header protocol (eg. IPv4/IPv6)
 *	@ip_proto: Transport header protocol (eg. TCP/UDP)
 * All the members, except thoff, are in network byte order.
 */
struct flow_keys {
	/* (src,dst) must be grouped, in the same way than in IP header */
	__be32 src;
	__be32 dst;
	union {
		__be32 ports;
		__be16 port16[2];
	};
	u16 thoff;
	u16 n_proto;
	u8 ip_proto;
};

bool skb_flow_dissect(const struct sk_buff *skb, struct flow_keys *flow);
__be32 skb_flow_get_ports(const struct sk_buff *skb, int thoff, u8 ip_proto);
#endif
