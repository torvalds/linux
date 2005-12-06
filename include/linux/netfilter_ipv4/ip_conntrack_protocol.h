/* Header for use in defining a given protocol for connection tracking. */
#ifndef _IP_CONNTRACK_PROTOCOL_H
#define _IP_CONNTRACK_PROTOCOL_H
#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

struct seq_file;

struct ip_conntrack_protocol
{
	/* Protocol number. */
	u_int8_t proto;

	/* Protocol name */
	const char *name;

	/* Try to fill in the third arg: dataoff is offset past IP
           hdr.  Return true if possible. */
	int (*pkt_to_tuple)(const struct sk_buff *skb,
			   unsigned int dataoff,
			   struct ip_conntrack_tuple *tuple);

	/* Invert the per-proto part of the tuple: ie. turn xmit into reply.
	 * Some packets can't be inverted: return 0 in that case.
	 */
	int (*invert_tuple)(struct ip_conntrack_tuple *inverse,
			    const struct ip_conntrack_tuple *orig);

	/* Print out the per-protocol part of the tuple. Return like seq_* */
	int (*print_tuple)(struct seq_file *,
			   const struct ip_conntrack_tuple *);

	/* Print out the private part of the conntrack. */
	int (*print_conntrack)(struct seq_file *, const struct ip_conntrack *);

	/* Returns verdict for packet, or -1 for invalid. */
	int (*packet)(struct ip_conntrack *conntrack,
		      const struct sk_buff *skb,
		      enum ip_conntrack_info ctinfo);

	/* Called when a new connection for this protocol found;
	 * returns TRUE if it's OK.  If so, packet() called next. */
	int (*new)(struct ip_conntrack *conntrack, const struct sk_buff *skb);

	/* Called when a conntrack entry is destroyed */
	void (*destroy)(struct ip_conntrack *conntrack);

	int (*error)(struct sk_buff *skb, enum ip_conntrack_info *ctinfo,
		     unsigned int hooknum);

	/* convert protoinfo to nfnetink attributes */
	int (*to_nfattr)(struct sk_buff *skb, struct nfattr *nfa,
			 const struct ip_conntrack *ct);

	/* convert nfnetlink attributes to protoinfo */
	int (*from_nfattr)(struct nfattr *tb[], struct ip_conntrack *ct);

	int (*tuple_to_nfattr)(struct sk_buff *skb,
			       const struct ip_conntrack_tuple *t);
	int (*nfattr_to_tuple)(struct nfattr *tb[],
			       struct ip_conntrack_tuple *t);

	/* Module (if any) which this is connected to. */
	struct module *me;
};

/* Protocol registration. */
extern int ip_conntrack_protocol_register(struct ip_conntrack_protocol *proto);
extern void ip_conntrack_protocol_unregister(struct ip_conntrack_protocol *proto);
/* Existing built-in protocols */
extern struct ip_conntrack_protocol ip_conntrack_protocol_tcp;
extern struct ip_conntrack_protocol ip_conntrack_protocol_udp;
extern struct ip_conntrack_protocol ip_conntrack_protocol_icmp;
extern struct ip_conntrack_protocol ip_conntrack_generic_protocol;
extern int ip_conntrack_protocol_tcp_init(void);

/* Log invalid packets */
extern unsigned int ip_ct_log_invalid;

extern int ip_ct_port_tuple_to_nfattr(struct sk_buff *,
				      const struct ip_conntrack_tuple *);
extern int ip_ct_port_nfattr_to_tuple(struct nfattr *tb[],
				      struct ip_conntrack_tuple *);

#ifdef CONFIG_SYSCTL
#ifdef DEBUG_INVALID_PACKETS
#define LOG_INVALID(proto) \
	(ip_ct_log_invalid == (proto) || ip_ct_log_invalid == IPPROTO_RAW)
#else
#define LOG_INVALID(proto) \
	((ip_ct_log_invalid == (proto) || ip_ct_log_invalid == IPPROTO_RAW) \
	 && net_ratelimit())
#endif
#else
#define LOG_INVALID(proto) 0
#endif /* CONFIG_SYSCTL */

#endif /*_IP_CONNTRACK_PROTOCOL_H*/
