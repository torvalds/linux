/* SPDX-License-Identifier: GPL-2.0 */
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

	/* Resolve clashes on insertion races. */
	bool allow_clash;

	/* protoinfo nlattr size, closes a hole */
	u16 nlattr_size;

	/* Try to fill in the third arg: dataoff is offset past network protocol
           hdr.  Return true if possible. */
	bool (*pkt_to_tuple)(const struct sk_buff *skb, unsigned int dataoff,
			     struct net *net, struct nf_conntrack_tuple *tuple);

	/* Invert the per-proto part of the tuple: ie. turn xmit into reply.
	 * Only used by icmp, most protocols use a generic version.
	 */
	bool (*invert_tuple)(struct nf_conntrack_tuple *inverse,
			     const struct nf_conntrack_tuple *orig);

	/* Returns verdict for packet, or -1 for invalid. */
	int (*packet)(struct nf_conn *ct,
		      const struct sk_buff *skb,
		      unsigned int dataoff,
		      enum ip_conntrack_info ctinfo,
		      const struct nf_hook_state *state);

	/* Called when a conntrack entry is destroyed */
	void (*destroy)(struct nf_conn *ct);

	int (*error)(struct nf_conn *tmpl, struct sk_buff *skb,
		     unsigned int dataoff,
		     const struct nf_hook_state *state);

	/* called by gc worker if table is full */
	bool (*can_early_drop)(const struct nf_conn *ct);

	/* convert protoinfo to nfnetink attributes */
	int (*to_nlattr)(struct sk_buff *skb, struct nlattr *nla,
			 struct nf_conn *ct);

	/* convert nfnetlink attributes to protoinfo */
	int (*from_nlattr)(struct nlattr *tb[], struct nf_conn *ct);

	int (*tuple_to_nlattr)(struct sk_buff *skb,
			       const struct nf_conntrack_tuple *t);
	/* Calculate tuple nlattr size */
	unsigned int (*nlattr_tuple_size)(void);
	int (*nlattr_to_tuple)(struct nlattr *tb[],
			       struct nf_conntrack_tuple *t);
	const struct nla_policy *nla_policy;

	struct {
		int (*nlattr_to_obj)(struct nlattr *tb[],
				     struct net *net, void *data);
		int (*obj_to_nlattr)(struct sk_buff *skb, const void *data);

		u16 obj_size;
		u16 nlattr_max;
		const struct nla_policy *nla_policy;
	} ctnl_timeout;
#ifdef CONFIG_NF_CONNTRACK_PROCFS
	/* Print out the private part of the conntrack. */
	void (*print_conntrack)(struct seq_file *s, struct nf_conn *);
#endif
	unsigned int	*net_id;
	/* Init l4proto pernet data */
	int (*init_net)(struct net *net, u_int16_t proto);

	/* Return the per-net protocol part. */
	struct nf_proto_net *(*get_net_proto)(struct net *net);

	/* Module (if any) which this is connected to. */
	struct module *me;
};

/* Existing built-in generic protocol */
extern const struct nf_conntrack_l4proto nf_conntrack_l4proto_generic;

#define MAX_NF_CT_PROTO 256

const struct nf_conntrack_l4proto *__nf_ct_l4proto_find(u_int16_t l3proto,
						  u_int8_t l4proto);

const struct nf_conntrack_l4proto *nf_ct_l4proto_find_get(u_int16_t l3proto,
						    u_int8_t l4proto);
void nf_ct_l4proto_put(const struct nf_conntrack_l4proto *p);

/* Protocol pernet registration. */
int nf_ct_l4proto_pernet_register_one(struct net *net,
				const struct nf_conntrack_l4proto *proto);
void nf_ct_l4proto_pernet_unregister_one(struct net *net,
				const struct nf_conntrack_l4proto *proto);
int nf_ct_l4proto_pernet_register(struct net *net,
				  const struct nf_conntrack_l4proto *const proto[],
				  unsigned int num_proto);
void nf_ct_l4proto_pernet_unregister(struct net *net,
				const struct nf_conntrack_l4proto *const proto[],
				unsigned int num_proto);

/* Protocol global registration. */
int nf_ct_l4proto_register_one(const struct nf_conntrack_l4proto *proto);
void nf_ct_l4proto_unregister_one(const struct nf_conntrack_l4proto *proto);

/* Generic netlink helpers */
int nf_ct_port_tuple_to_nlattr(struct sk_buff *skb,
			       const struct nf_conntrack_tuple *tuple);
int nf_ct_port_nlattr_to_tuple(struct nlattr *tb[],
			       struct nf_conntrack_tuple *t);
unsigned int nf_ct_port_nlattr_tuple_size(void);
extern const struct nla_policy nf_ct_port_nla_policy[];

#ifdef CONFIG_SYSCTL
__printf(3, 4) __cold
void nf_ct_l4proto_log_invalid(const struct sk_buff *skb,
			       const struct nf_conn *ct,
			       const char *fmt, ...);
__printf(5, 6) __cold
void nf_l4proto_log_invalid(const struct sk_buff *skb,
			    struct net *net,
			    u16 pf, u8 protonum,
			    const char *fmt, ...);
#else
static inline __printf(5, 6) __cold
void nf_l4proto_log_invalid(const struct sk_buff *skb, struct net *net,
			    u16 pf, u8 protonum, const char *fmt, ...) {}
static inline __printf(3, 4) __cold
void nf_ct_l4proto_log_invalid(const struct sk_buff *skb,
			       const struct nf_conn *ct,
			       const char *fmt, ...) { }
#endif /* CONFIG_SYSCTL */

#endif /*_NF_CONNTRACK_PROTOCOL_H*/
