/*
 * Header for use in defining a given protocol for connection tracking.
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- generalized L3 protocol dependent part.
 *
 * Derived from include/linux/netfiter_ipv4/ip_conntrack_protcol.h
 */

#ifndef _NF_CONNTRACK_PROTOCOL_H
#define _NF_CONNTRACK_PROTOCOL_H
#include <net/netfilter/nf_conntrack.h>

struct seq_file;

struct nf_conntrack_protocol
{
	/* Next pointer. */
	struct list_head list;

	/* L3 Protocol number. */
	u_int16_t l3proto;

	/* Protocol number. */
	u_int8_t proto;

	/* Protocol name */
	const char *name;

	/* Try to fill in the third arg: dataoff is offset past network protocol
           hdr.  Return true if possible. */
	int (*pkt_to_tuple)(const struct sk_buff *skb,
			    unsigned int dataoff,
			    struct nf_conntrack_tuple *tuple);

	/* Invert the per-proto part of the tuple: ie. turn xmit into reply.
	 * Some packets can't be inverted: return 0 in that case.
	 */
	int (*invert_tuple)(struct nf_conntrack_tuple *inverse,
			    const struct nf_conntrack_tuple *orig);

	/* Print out the per-protocol part of the tuple. Return like seq_* */
	int (*print_tuple)(struct seq_file *s,
			   const struct nf_conntrack_tuple *);

	/* Print out the private part of the conntrack. */
	int (*print_conntrack)(struct seq_file *s, const struct nf_conn *);

	/* Returns verdict for packet, or -1 for invalid. */
	int (*packet)(struct nf_conn *conntrack,
		      const struct sk_buff *skb,
		      unsigned int dataoff,
		      enum ip_conntrack_info ctinfo,
		      int pf,
		      unsigned int hooknum);

	/* Called when a new connection for this protocol found;
	 * returns TRUE if it's OK.  If so, packet() called next. */
	int (*new)(struct nf_conn *conntrack, const struct sk_buff *skb,
		   unsigned int dataoff);

	/* Called when a conntrack entry is destroyed */
	void (*destroy)(struct nf_conn *conntrack);

	int (*error)(struct sk_buff *skb, unsigned int dataoff,
		     enum ip_conntrack_info *ctinfo,
		     int pf, unsigned int hooknum);

	/* Module (if any) which this is connected to. */
	struct module *me;
};

/* Existing built-in protocols */
extern struct nf_conntrack_protocol nf_conntrack_protocol_tcp6;
extern struct nf_conntrack_protocol nf_conntrack_protocol_udp4;
extern struct nf_conntrack_protocol nf_conntrack_protocol_udp6;
extern struct nf_conntrack_protocol nf_conntrack_generic_protocol;

#define MAX_NF_CT_PROTO 256
extern struct nf_conntrack_protocol **nf_ct_protos[PF_MAX];

extern struct nf_conntrack_protocol *
nf_ct_find_proto(u_int16_t l3proto, u_int8_t protocol);

/* Protocol registration. */
extern int nf_conntrack_protocol_register(struct nf_conntrack_protocol *proto);
extern void nf_conntrack_protocol_unregister(struct nf_conntrack_protocol *proto);

/* Log invalid packets */
extern unsigned int nf_ct_log_invalid;

#ifdef CONFIG_SYSCTL
#ifdef DEBUG_INVALID_PACKETS
#define LOG_INVALID(proto) \
	(nf_ct_log_invalid == (proto) || nf_ct_log_invalid == IPPROTO_RAW)
#else
#define LOG_INVALID(proto) \
	((nf_ct_log_invalid == (proto) || nf_ct_log_invalid == IPPROTO_RAW) \
	 && net_ratelimit())
#endif
#else
#define LOG_INVALID(proto) 0
#endif /* CONFIG_SYSCTL */

#endif /*_NF_CONNTRACK_PROTOCOL_H*/
