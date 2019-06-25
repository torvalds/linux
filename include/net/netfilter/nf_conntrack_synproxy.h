/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_CONNTRACK_SYNPROXY_H
#define _NF_CONNTRACK_SYNPROXY_H

#include <net/netns/generic.h>

struct nf_conn_synproxy {
	u32	isn;
	u32	its;
	u32	tsoff;
};

static inline struct nf_conn_synproxy *nfct_synproxy(const struct nf_conn *ct)
{
#if IS_ENABLED(CONFIG_NETFILTER_SYNPROXY)
	return nf_ct_ext_find(ct, NF_CT_EXT_SYNPROXY);
#else
	return NULL;
#endif
}

static inline struct nf_conn_synproxy *nfct_synproxy_ext_add(struct nf_conn *ct)
{
#if IS_ENABLED(CONFIG_NETFILTER_SYNPROXY)
	return nf_ct_ext_add(ct, NF_CT_EXT_SYNPROXY, GFP_ATOMIC);
#else
	return NULL;
#endif
}

static inline bool nf_ct_add_synproxy(struct nf_conn *ct,
				      const struct nf_conn *tmpl)
{
	if (tmpl && nfct_synproxy(tmpl)) {
		if (!nfct_seqadj_ext_add(ct))
			return false;

		if (!nfct_synproxy_ext_add(ct))
			return false;
	}

	return true;
}

struct synproxy_stats {
	unsigned int			syn_received;
	unsigned int			cookie_invalid;
	unsigned int			cookie_valid;
	unsigned int			cookie_retrans;
	unsigned int			conn_reopened;
};

struct synproxy_net {
	struct nf_conn			*tmpl;
	struct synproxy_stats __percpu	*stats;
	unsigned int			hook_ref4;
	unsigned int			hook_ref6;
};

extern unsigned int synproxy_net_id;
static inline struct synproxy_net *synproxy_pernet(struct net *net)
{
	return net_generic(net, synproxy_net_id);
}

struct synproxy_options {
	u8				options;
	u8				wscale;
	u16				mss;
	u32				tsval;
	u32				tsecr;
};

struct tcphdr;
struct nf_synproxy_info;
bool synproxy_parse_options(const struct sk_buff *skb, unsigned int doff,
			    const struct tcphdr *th,
			    struct synproxy_options *opts);

void synproxy_init_timestamp_cookie(const struct nf_synproxy_info *info,
				    struct synproxy_options *opts);

#endif /* _NF_CONNTRACK_SYNPROXY_H */
