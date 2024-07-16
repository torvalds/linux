/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_CONNTRACK_EXTEND_H
#define _NF_CONNTRACK_EXTEND_H

#include <linux/slab.h>

#include <net/netfilter/nf_conntrack.h>

enum nf_ct_ext_id {
	NF_CT_EXT_HELPER,
#if IS_ENABLED(CONFIG_NF_NAT)
	NF_CT_EXT_NAT,
#endif
	NF_CT_EXT_SEQADJ,
	NF_CT_EXT_ACCT,
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	NF_CT_EXT_ECACHE,
#endif
#ifdef CONFIG_NF_CONNTRACK_TIMESTAMP
	NF_CT_EXT_TSTAMP,
#endif
#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
	NF_CT_EXT_TIMEOUT,
#endif
#ifdef CONFIG_NF_CONNTRACK_LABELS
	NF_CT_EXT_LABELS,
#endif
#if IS_ENABLED(CONFIG_NETFILTER_SYNPROXY)
	NF_CT_EXT_SYNPROXY,
#endif
#if IS_ENABLED(CONFIG_NET_ACT_CT)
	NF_CT_EXT_ACT_CT,
#endif
	NF_CT_EXT_NUM,
};

/* Extensions: optional stuff which isn't permanently in struct. */
struct nf_ct_ext {
	u8 offset[NF_CT_EXT_NUM];
	u8 len;
	unsigned int gen_id;
	char data[] __aligned(8);
};

static inline bool __nf_ct_ext_exist(const struct nf_ct_ext *ext, u8 id)
{
	return !!ext->offset[id];
}

static inline bool nf_ct_ext_exist(const struct nf_conn *ct, u8 id)
{
	return (ct->ext && __nf_ct_ext_exist(ct->ext, id));
}

void *__nf_ct_ext_find(const struct nf_ct_ext *ext, u8 id);

static inline void *nf_ct_ext_find(const struct nf_conn *ct, u8 id)
{
	struct nf_ct_ext *ext = ct->ext;

	if (!ext || !__nf_ct_ext_exist(ext, id))
		return NULL;

	if (unlikely(ext->gen_id))
		return __nf_ct_ext_find(ext, id);

	return (void *)ct->ext + ct->ext->offset[id];
}

/* Add this type, returns pointer to data or NULL. */
void *nf_ct_ext_add(struct nf_conn *ct, enum nf_ct_ext_id id, gfp_t gfp);

/* ext genid.  if ext->id != ext_genid, extensions cannot be used
 * anymore unless conntrack has CONFIRMED bit set.
 */
extern atomic_t nf_conntrack_ext_genid;
void nf_ct_ext_bump_genid(void);

#endif /* _NF_CONNTRACK_EXTEND_H */
