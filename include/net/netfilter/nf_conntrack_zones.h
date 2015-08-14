#ifndef _NF_CONNTRACK_ZONES_H
#define _NF_CONNTRACK_ZONES_H

#include <linux/netfilter/nf_conntrack_tuple_common.h>

#define NF_CT_DEFAULT_ZONE_ID	0

#define NF_CT_ZONE_DIR_ORIG	(1 << IP_CT_DIR_ORIGINAL)
#define NF_CT_ZONE_DIR_REPL	(1 << IP_CT_DIR_REPLY)

#define NF_CT_DEFAULT_ZONE_DIR	(NF_CT_ZONE_DIR_ORIG | NF_CT_ZONE_DIR_REPL)

struct nf_conntrack_zone {
	u16	id;
	u16	dir;
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
