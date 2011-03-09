#ifndef _NF_CONNTRACK_ZONES_H
#define _NF_CONNTRACK_ZONES_H

#define NF_CT_DEFAULT_ZONE	0

#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
#include <net/netfilter/nf_conntrack_extend.h>

struct nf_conntrack_zone {
	u16	id;
};

static inline u16 nf_ct_zone(const struct nf_conn *ct)
{
#ifdef CONFIG_NF_CONNTRACK_ZONES
	struct nf_conntrack_zone *nf_ct_zone;
	nf_ct_zone = nf_ct_ext_find(ct, NF_CT_EXT_ZONE);
	if (nf_ct_zone)
		return nf_ct_zone->id;
#endif
	return NF_CT_DEFAULT_ZONE;
}

#endif /* CONFIG_NF_CONNTRACK || CONFIG_NF_CONNTRACK_MODULE */
#endif /* _NF_CONNTRACK_ZONES_H */
