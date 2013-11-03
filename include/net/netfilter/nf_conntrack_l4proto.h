/*
 * Header for use in defining a given L4 protocol for connection tracking.
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- generalized L3 protocol dependent part.
 *
 * Derived from include/linux/netfiter_ipv4/ip_conntrack_protcol.h
 */

#ifndef _NF_CONNTRACK_L4PROTO_H
#define _NF_CONNTRACK_L4PROTO_H
#include <linux/netlink.h>
#include <net/netlink.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netns/generic.h>

struct seq_file;

struct nf_conntrack_l4proto {
	/* L3 Protocol number. */
	u_int16_t l3proto;

	/* L4 Protocol number. */
	u_int8_t l4proto;

	/* Try to fill in the third arg: dataoff is offset past network protocol
           hdr.  Return true if possible. */
	bool (*pkt_to_tuple)(const struct sk_buff *skb, unsigned int dataoff,
			     struct nf_conntrack_tuple *tuple);

	/* Invert the per-proto part of the tuple: ie. turn xmit into reply.
	 * Some packets can't be inverted: return 0 in that case.
	 */
	bool (*invert_tuple)(struct nf_conntrack_tuple *inverse,
			     const struct nf_conntrack_tuple *orig);

	/* Returns verdict for packet, or -1 for invalid. */
	int (*packet)(struct nf_conn *ct,
		      const struct sk_buff *skb,
		      unsigned int dataoff,
		      enum ip_conntrack_info ctinfo,
		      u_int8_t pf,
		      unsigned int hooknum,
		      unsigned int *timeouts);

	/* Called when a new connection for this protocol found;
	 * returns TRUE if it's OK.  If so, packet() called next. */
	bool (*new)(struct nf_conn *ct, const struct sk_buff *skb,
		    unsigned int dataoff, unsigned int *timeouts);

	/* Called when a conntrack entry is destroyed */
	void (*destroy)(struct nf_conn *ct);

	int (*error)(struct net *net, struct nf_conn *tmpl, struct sk_buff *skb,
		     unsigned int dataoff, enum ip_conntrack_info *ctinfo,
		     u_int8_t pf, unsigned int hooknum);

	/* Print out the per-protocol part of the tuple. Return like seq_* */
	int (*print_tuple)(struct seq_file *s,
			   const struct nf_conntrack_tuple *);

	/* Print out the private part of the conntrack. */
	int (*print_conntrack)(struct seq_file *s, struct nf_conn *);

	/* Return the array of timeouts for this protocol. */
	unsigned int *(*get_timeouts)(struct net *net);

	/* convert protoinfo to nfnetink attributes */
	int (*to_nlattr)(struct sk_buff *skb, struct nlattr *nla,
			 struct nf_conn *ct);
	/* Calculate protoinfo nlattr size */
	int (*nlattr_size)(void);

	/* convert nfnetlink attributes to protoinfo */
	int (*from_nlattr)(struct nlattr *tb[], struct nf_conn *ct);

	int (*tuple_to_nlattr)(struct sk_buff *skb,
			       const struct nf_conntrack_tuple *t);
	/* Calculate tuple nlattr size */
	int (*nlattr_tuple_size)(void);
	int (*nlattr_to_tuple)(struct nlattr *tb[],
			       struct nf_conntrack_tuple *t);
	const struct nla_policy *nla_policy;

	size_t nla_size;

#if IS_ENABLED(CONFIG_NF_CT_NETLINK_TIMEOUT)
	struct {
		size_t obj_size;
		int (*nlattr_to_obj)(struct nlattr *tb[],
				     struct net *net, void *data);
		int (*obj_to_nlattr)(struct sk_buff *skb, const void *data);

		unsigned int nlattr_max;
		const struct nla_policy *nla_policy;
	} ctnl_timeout;
#endif
	int	*net_id;
	/* Init l4proto pernet data */
	int (*init_net)(struct net *net, u_int16_t proto);

	/* Return the per-net protocol part. */
	struct nf_proto_net *(*get_net_proto)(struct net *net);

	/* Protocol name */
	const char *name;

	/* Module (if any) which this is connected to. */
	struct module *me;
};

/* Existing built-in generic protocol */
extern struct nf_conntrack_l4proto nf_conntrack_l4proto_generic;

#define MAX_NF_CT_PROTO 256

extern struct nf_conntrack_l4proto *
__nf_ct_l4proto_find(u_int16_t l3proto, u_int8_t l4proto);

extern struct nf_conntrack_l4proto *
nf_ct_l4proto_find_get(u_int16_t l3proto, u_int8_t l4proto);
extern void nf_ct_l4proto_put(struct nf_conntrack_l4proto *p);

/* Protocol pernet registration. */
extern int nf_ct_l4proto_pernet_register(struct net *net,
					 struct nf_conntrack_l4proto *proto);
extern void nf_ct_l4proto_pernet_unregister(struct net *net,
					    struct nf_conntrack_l4proto *proto);

/* Protocol global registration. */
extern int nf_ct_l4proto_register(struct nf_conntrack_l4proto *proto);
extern void nf_ct_l4proto_unregister(struct nf_conntrack_l4proto *proto);

static inline void nf_ct_kfree_compat_sysctl_table(struct nf_proto_net *pn)
{
#if defined(CONFIG_SYSCTL) && defined(CONFIG_NF_CONNTRACK_PROC_COMPAT)
	kfree(pn->ctl_compat_table);
	pn->ctl_compat_table = NULL;
#endif
}

/* Generic netlink helpers */
extern int nf_ct_port_tuple_to_nlattr(struct sk_buff *skb,
				      const struct nf_conntrack_tuple *tuple);
extern int nf_ct_port_nlattr_to_tuple(struct nlattr *tb[],
				      struct nf_conntrack_tuple *t);
extern int nf_ct_port_nlattr_tuple_size(void);
extern const struct nla_policy nf_ct_port_nla_policy[];

#ifdef CONFIG_SYSCTL
#define LOG_INVALID(net, proto)				\
	((net)->ct.sysctl_log_invalid == (proto) ||	\
	 (net)->ct.sysctl_log_invalid == IPPROTO_RAW)
#else
static inline int LOG_INVALID(struct net *net, int proto) { return 0; }
#endif /* CONFIG_SYSCTL */

#endif /*_NF_CONNTRACK_PROTOCOL_H*/
