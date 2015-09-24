#ifndef _NF_CONNTRACK_ZONES_H
#define _NF_CONNTRACK_ZONES_H

#include <linux/netfilter/nf_conntrack_zones_common.h>

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
nf_ct_zone_init(struct nf_conntrack_zone *zone, u16 id, u8 dir, u8 flags)
{
	zone->id = id;
	zone->flags = flags;
	zone->dir = dir;

	return zone;
}

static inline const struct nf_conntrack_zone *
nf_ct_zone_tmpl(const struct nf_conn *tmpl, const struct sk_buff *skb,
		struct nf_conntrack_zone *tmp)
{
	const struct nf_conntrack_zone *zone;

	if (!tmpl)
		return &nf_ct_zone_dflt;

	zone = nf_ct_zone(tmpl);
	if (zone->flags & NF_CT_FLAG_MARK)
		zone = nf_ct_zone_init(tmp, skb->mark, zone->dir, 0);

	return zone;
}

static inline int nf_ct_zone_add(struct nf_conn *ct, gfp_t flags,
				 const struct nf_conntrack_zone *info)
{
#ifdef CONFIG_NF_CONNTRACK_ZONES
	struct nf_conntrack_zone *nf_ct_zone;

	nf_ct_zone = nf_ct_ext_add(ct, NF_CT_EXT_ZONE, flags);
	if (!nf_ct_zone)
		return -ENOMEM;

	nf_ct_zone_init(nf_ct_zone, info->id, info->dir,
			info->flags);
#endif
	return 0;
}

static inline bool nf_ct_zone_matches_dir(const struct nf_conntrack_zone *zone,
					  enum ip_conntrack_dir dir)
{
	return zone->dir & (1 << dir);
}

static inline u16 nf_ct_zone_id(const struct nf_conntrack_zone *zone,
				enum ip_conntrack_dir dir)
{
	return nf_ct_zone_matches_dir(zone, dir) ?
	       zone->id : NF_CT_DEFAULT_ZONE_ID;
}

static inline bool nf_ct_zone_equal(const struct nf_conn *a,
				    const struct nf_conntrack_zone *b,
				    enum ip_conntrack_dir dir)
{
	return nf_ct_zone_id(nf_ct_zone(a), dir) ==
	       nf_ct_zone_id(b, dir);
}

static inline bool nf_ct_zone_equal_any(const struct nf_conn *a,
					const struct nf_conntrack_zone *b)
{
	return nf_ct_zone(a)->id == b->id;
}
#endif /* IS_ENABLED(CONFIG_NF_CONNTRACK) */
#endif /* _NF_CONNTRACK_ZONES_H */
