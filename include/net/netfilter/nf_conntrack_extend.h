#ifndef _NF_CONNTRACK_EXTEND_H
#define _NF_CONNTRACK_EXTEND_H

#include <linux/slab.h>

#include <net/netfilter/nf_conntrack.h>

enum nf_ct_ext_id {
	NF_CT_EXT_HELPER,
#if defined(CONFIG_NF_NAT) || defined(CONFIG_NF_NAT_MODULE)
	NF_CT_EXT_NAT,
#endif
	NF_CT_EXT_ACCT,
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	NF_CT_EXT_ECACHE,
#endif
#ifdef CONFIG_NF_CONNTRACK_ZONES
	NF_CT_EXT_ZONE,
#endif
#ifdef CONFIG_NF_CONNTRACK_TIMESTAMP
	NF_CT_EXT_TSTAMP,
#endif
#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
	NF_CT_EXT_TIMEOUT,
#endif
	NF_CT_EXT_NUM,
};

#define NF_CT_EXT_HELPER_TYPE struct nf_conn_help
#define NF_CT_EXT_NAT_TYPE struct nf_conn_nat
#define NF_CT_EXT_ACCT_TYPE struct nf_conn_counter
#define NF_CT_EXT_ECACHE_TYPE struct nf_conntrack_ecache
#define NF_CT_EXT_ZONE_TYPE struct nf_conntrack_zone
#define NF_CT_EXT_TSTAMP_TYPE struct nf_conn_tstamp
#define NF_CT_EXT_TIMEOUT_TYPE struct nf_conn_timeout

/* Extensions: optional stuff which isn't permanently in struct. */
struct nf_ct_ext {
	struct rcu_head rcu;
	u8 offset[NF_CT_EXT_NUM];
	u8 len;
	char data[0];
};

static inline bool __nf_ct_ext_exist(const struct nf_ct_ext *ext, u8 id)
{
	return !!ext->offset[id];
}

static inline bool nf_ct_ext_exist(const struct nf_conn *ct, u8 id)
{
	return (ct->ext && __nf_ct_ext_exist(ct->ext, id));
}

static inline void *__nf_ct_ext_find(const struct nf_conn *ct, u8 id)
{
	if (!nf_ct_ext_exist(ct, id))
		return NULL;

	return (void *)ct->ext + ct->ext->offset[id];
}
#define nf_ct_ext_find(ext, id)	\
	((id##_TYPE *)__nf_ct_ext_find((ext), (id)))

/* Destroy all relationships */
extern void __nf_ct_ext_destroy(struct nf_conn *ct);
static inline void nf_ct_ext_destroy(struct nf_conn *ct)
{
	if (ct->ext)
		__nf_ct_ext_destroy(ct);
}

/* Free operation. If you want to free a object referred from private area,
 * please implement __nf_ct_ext_free() and call it.
 */
static inline void nf_ct_ext_free(struct nf_conn *ct)
{
	if (ct->ext)
		kfree(ct->ext);
}

/* Add this type, returns pointer to data or NULL. */
void *
__nf_ct_ext_add(struct nf_conn *ct, enum nf_ct_ext_id id, gfp_t gfp);
#define nf_ct_ext_add(ct, id, gfp) \
	((id##_TYPE *)__nf_ct_ext_add((ct), (id), (gfp)))

#define NF_CT_EXT_F_PREALLOC	0x0001

struct nf_ct_ext_type {
	/* Destroys relationships (can be NULL). */
	void (*destroy)(struct nf_conn *ct);
	/* Called when realloacted (can be NULL).
	   Contents has already been moved. */
	void (*move)(void *new, void *old);

	enum nf_ct_ext_id id;

	unsigned int flags;

	/* Length and min alignment. */
	u8 len;
	u8 align;
	/* initial size of nf_ct_ext. */
	u8 alloc_size;
};

int nf_ct_extend_register(struct nf_ct_ext_type *type);
void nf_ct_extend_unregister(struct nf_ct_ext_type *type);
#endif /* _NF_CONNTRACK_EXTEND_H */
