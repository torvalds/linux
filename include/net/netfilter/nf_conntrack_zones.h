#ifndef _NF_CONNTRACK_ZONES_H
#define _NF_CONNTRACK_ZONES_H

#define NF_CT_DEFAULT_ZONE_ID	0

struct nf_conntrack_zone {
	u16	id;
};

extern const struct nf_conntrack_zone nf_ct_zone_dflt;

#if IS_ENABLED(CONFIG_NF_CONNTRACK)
#include <net/netfilter/nf_conntrack_extend.h>

static inline const struct nf_conntrack_zone *
nf_ct_zone(const struct nf_conn *ct)
{
	const struct nf_conntrack_zone *nf_ct_zone = NULL;

#ifdef CONFIG_NF_CONNTRACK_ZONES
	nf_ct_zone = nf_ct_ext_find(ct, NF_CT_EXT_ZONE);
#endif
	return nf_ct_zone ? nf_ct_zone : &nf_ct_zone_dflt;
}

static inline const struct nf_conntrack_zone *
nf_ct_zone_tmpl(const struct nf_conn *tmpl)
{
	return tmpl ? nf_ct_zone(tmpl) : &nf_ct_zone_dflt;
}

static inline bool nf_ct_zone_equal(const struct nf_conn *a,
				    const struct nf_conntrack_zone *b)
{
	return nf_ct_zone(a)->id == b->id;
}
#endif /* IS_ENABLED(CONFIG_NF_CONNTRACK) */
#endif /* _NF_CONNTRACK_ZONES_H */
