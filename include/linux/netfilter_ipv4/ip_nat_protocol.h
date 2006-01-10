/* Header for use in defining a given protocol. */
#ifndef _IP_NAT_PROTOCOL_H
#define _IP_NAT_PROTOCOL_H
#include <linux/init.h>
#include <linux/list.h>

#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

struct iphdr;
struct ip_nat_range;

struct ip_nat_protocol
{
	/* Protocol name */
	const char *name;

	/* Protocol number. */
	unsigned int protonum;

	struct module *me;

	/* Translate a packet to the target according to manip type.
	   Return true if succeeded. */
	int (*manip_pkt)(struct sk_buff **pskb,
			 unsigned int iphdroff,
			 const struct ip_conntrack_tuple *tuple,
			 enum ip_nat_manip_type maniptype);

	/* Is the manipable part of the tuple between min and max incl? */
	int (*in_range)(const struct ip_conntrack_tuple *tuple,
			enum ip_nat_manip_type maniptype,
			const union ip_conntrack_manip_proto *min,
			const union ip_conntrack_manip_proto *max);

	/* Alter the per-proto part of the tuple (depending on
	   maniptype), to give a unique tuple in the given range if
	   possible; return false if not.  Per-protocol part of tuple
	   is initialized to the incoming packet. */
	int (*unique_tuple)(struct ip_conntrack_tuple *tuple,
			    const struct ip_nat_range *range,
			    enum ip_nat_manip_type maniptype,
			    const struct ip_conntrack *conntrack);

	int (*range_to_nfattr)(struct sk_buff *skb,
			       const struct ip_nat_range *range);

	int (*nfattr_to_range)(struct nfattr *tb[],
			       struct ip_nat_range *range);
};

/* Protocol registration. */
extern int ip_nat_protocol_register(struct ip_nat_protocol *proto);
extern void ip_nat_protocol_unregister(struct ip_nat_protocol *proto);

extern struct ip_nat_protocol *ip_nat_proto_find_get(u_int8_t protocol);
extern void ip_nat_proto_put(struct ip_nat_protocol *proto);

/* Built-in protocols. */
extern struct ip_nat_protocol ip_nat_protocol_tcp;
extern struct ip_nat_protocol ip_nat_protocol_udp;
extern struct ip_nat_protocol ip_nat_protocol_icmp;
extern struct ip_nat_protocol ip_nat_unknown_protocol;

extern int init_protocols(void) __init;
extern void cleanup_protocols(void);
extern struct ip_nat_protocol *find_nat_proto(u_int16_t protonum);

extern int ip_nat_port_range_to_nfattr(struct sk_buff *skb,
				       const struct ip_nat_range *range);
extern int ip_nat_port_nfattr_to_range(struct nfattr *tb[],
				       struct ip_nat_range *range);

#endif /*_IP_NAT_PROTO_H*/
