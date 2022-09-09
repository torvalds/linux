/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_CONNTRACK_SYNPROXY_H
#define _NF_CONNTRACK_SYNPROXY_H

#include <net/netfilter/nf_conntrack_seqadj.h>
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
#if IS_ENABLED(CONFIG_NETFILTER_SYNPROXY)
	if (tmpl && nfct_synproxy(tmpl)) {
		if (!nfct_seqadj_ext_add(ct))
			return false;

		if (!nfct_synproxy_ext_add(ct))
			return false;
	}
#endif

	return true;
}

#endif /* _NF_CONNTRACK_SYNPROXY_H */
