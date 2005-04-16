/* Header for use in defining a given protocol. */
#ifndef _IP_NAT_PROTOCOL_H
#define _IP_NAT_PROTOCOL_H
#include <linux/init.h>
#include <linux/list.h>

struct iphdr;
struct ip_nat_range;

struct ip_nat_protocol
{
	/* Protocol name */
	const char *name;

	/* Protocol number. */
	unsigned int protonum;

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

	unsigned int (*print)(char *buffer,
			      const struct ip_conntrack_tuple *match,
			      const struct ip_conntrack_tuple *mask);

	unsigned int (*print_range)(char *buffer,
				    const struct ip_nat_range *range);
};

#define MAX_IP_NAT_PROTO 256
extern struct ip_nat_protocol *ip_nat_protos[MAX_IP_NAT_PROTO];

/* Protocol registration. */
extern int ip_nat_protocol_register(struct ip_nat_protocol *proto);
extern void ip_nat_protocol_unregister(struct ip_nat_protocol *proto);

static inline struct ip_nat_protocol *ip_nat_find_proto(u_int8_t protocol)
{
	return ip_nat_protos[protocol];
}

/* Built-in protocols. */
extern struct ip_nat_protocol ip_nat_protocol_tcp;
extern struct ip_nat_protocol ip_nat_protocol_udp;
extern struct ip_nat_protocol ip_nat_protocol_icmp;
extern struct ip_nat_protocol ip_nat_unknown_protocol;

extern int init_protocols(void) __init;
extern void cleanup_protocols(void);
extern struct ip_nat_protocol *find_nat_proto(u_int16_t protonum);

#endif /*_IP_NAT_PROTO_H*/
