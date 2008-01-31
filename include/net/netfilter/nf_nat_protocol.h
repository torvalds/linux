/* Header for use in defining a given protocol. */
#ifndef _NF_NAT_PROTOCOL_H
#define _NF_NAT_PROTOCOL_H
#include <net/netfilter/nf_nat.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

struct nf_nat_range;

struct nf_nat_protocol
{
	/* Protocol name */
	const char *name;

	/* Protocol number. */
	unsigned int protonum;

	struct module *me;

	/* Translate a packet to the target according to manip type.
	   Return true if succeeded. */
	int (*manip_pkt)(struct sk_buff *skb,
			 unsigned int iphdroff,
			 const struct nf_conntrack_tuple *tuple,
			 enum nf_nat_manip_type maniptype);

	/* Is the manipable part of the tuple between min and max incl? */
	int (*in_range)(const struct nf_conntrack_tuple *tuple,
			enum nf_nat_manip_type maniptype,
			const union nf_conntrack_man_proto *min,
			const union nf_conntrack_man_proto *max);

	/* Alter the per-proto part of the tuple (depending on
	   maniptype), to give a unique tuple in the given range if
	   possible; return false if not.  Per-protocol part of tuple
	   is initialized to the incoming packet. */
	int (*unique_tuple)(struct nf_conntrack_tuple *tuple,
			    const struct nf_nat_range *range,
			    enum nf_nat_manip_type maniptype,
			    const struct nf_conn *ct);

	int (*range_to_nlattr)(struct sk_buff *skb,
			       const struct nf_nat_range *range);

	int (*nlattr_to_range)(struct nlattr *tb[],
			       struct nf_nat_range *range);
};

/* Protocol registration. */
extern int nf_nat_protocol_register(const struct nf_nat_protocol *proto);
extern void nf_nat_protocol_unregister(const struct nf_nat_protocol *proto);

extern const struct nf_nat_protocol *nf_nat_proto_find_get(u_int8_t protocol);
extern void nf_nat_proto_put(const struct nf_nat_protocol *proto);

/* Built-in protocols. */
extern const struct nf_nat_protocol nf_nat_protocol_tcp;
extern const struct nf_nat_protocol nf_nat_protocol_udp;
extern const struct nf_nat_protocol nf_nat_protocol_icmp;
extern const struct nf_nat_protocol nf_nat_unknown_protocol;

extern int init_protocols(void) __init;
extern void cleanup_protocols(void);
extern const struct nf_nat_protocol *find_nat_proto(u_int16_t protonum);

extern int nf_nat_port_range_to_nlattr(struct sk_buff *skb,
				       const struct nf_nat_range *range);
extern int nf_nat_port_nlattr_to_range(struct nlattr *tb[],
				       struct nf_nat_range *range);

#endif /*_NF_NAT_PROTO_H*/
