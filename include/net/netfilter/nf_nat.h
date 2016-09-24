#ifndef _NF_NAT_H
#define _NF_NAT_H
#include <linux/rhashtable.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter/nf_nat.h>
#include <net/netfilter/nf_conntrack_tuple.h>

enum nf_nat_manip_type {
	NF_NAT_MANIP_SRC,
	NF_NAT_MANIP_DST
};

/* SRC manip occurs POST_ROUTING or LOCAL_IN */
#define HOOK2MANIP(hooknum) ((hooknum) != NF_INET_POST_ROUTING && \
			     (hooknum) != NF_INET_LOCAL_IN)

#include <linux/list.h>
#include <linux/netfilter/nf_conntrack_pptp.h>
#include <net/netfilter/nf_conntrack_extend.h>

/* per conntrack: nat application helper private data */
union nf_conntrack_nat_help {
	/* insert nat helper private data here */
#if defined(CONFIG_NF_NAT_PPTP) || defined(CONFIG_NF_NAT_PPTP_MODULE)
	struct nf_nat_pptp nat_pptp_info;
#endif
};

struct nf_conn;

/* The structure embedded in the conntrack structure. */
struct nf_conn_nat {
	union nf_conntrack_nat_help help;
#if IS_ENABLED(CONFIG_NF_NAT_MASQUERADE_IPV4) || \
    IS_ENABLED(CONFIG_NF_NAT_MASQUERADE_IPV6)
	int masq_index;
#endif
};

/* Set up the info structure to map into this range. */
unsigned int nf_nat_setup_info(struct nf_conn *ct,
			       const struct nf_nat_range *range,
			       enum nf_nat_manip_type maniptype);

extern unsigned int nf_nat_alloc_null_binding(struct nf_conn *ct,
					      unsigned int hooknum);

struct nf_conn_nat *nf_ct_nat_ext_add(struct nf_conn *ct);

/* Is this tuple already taken? (not by us)*/
int nf_nat_used_tuple(const struct nf_conntrack_tuple *tuple,
		      const struct nf_conn *ignored_conntrack);

static inline struct nf_conn_nat *nfct_nat(const struct nf_conn *ct)
{
#if defined(CONFIG_NF_NAT) || defined(CONFIG_NF_NAT_MODULE)
	return nf_ct_ext_find(ct, NF_CT_EXT_NAT);
#else
	return NULL;
#endif
}

static inline bool nf_nat_oif_changed(unsigned int hooknum,
				      enum ip_conntrack_info ctinfo,
				      struct nf_conn_nat *nat,
				      const struct net_device *out)
{
#if IS_ENABLED(CONFIG_NF_NAT_MASQUERADE_IPV4) || \
    IS_ENABLED(CONFIG_NF_NAT_MASQUERADE_IPV6)
	return nat->masq_index && hooknum == NF_INET_POST_ROUTING &&
	       CTINFO2DIR(ctinfo) == IP_CT_DIR_ORIGINAL &&
	       nat->masq_index != out->ifindex;
#else
	return false;
#endif
}

#endif
