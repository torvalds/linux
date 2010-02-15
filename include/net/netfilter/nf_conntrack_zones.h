#ifndef _NF_CONNTRACK_ZONES_H
#define _NF_CONNTRACK_ZONES_H

#include <net/netfilter/nf_conntrack_extend.h>

#define NF_CT_DEFAULT_ZONE	0

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

#endif /* _NF_CONNTRACK_ZONES_H */
