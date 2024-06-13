/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _NF_CONNTRACK_LABELS_H
#define _NF_CONNTRACK_LABELS_H

#include <linux/netfilter/nf_conntrack_common.h>
#include <linux/netfilter/nf_conntrack_tuple_common.h>
#include <linux/types.h>
#include <net/net_namespace.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <uapi/linux/netfilter/xt_connlabel.h>

#define NF_CT_LABELS_MAX_SIZE ((XT_CONNLABEL_MAXBIT + 1) / BITS_PER_BYTE)

struct nf_conn_labels {
	unsigned long bits[NF_CT_LABELS_MAX_SIZE / sizeof(long)];
};

/* Can't use nf_ct_ext_find(), flow dissector cannot use symbols
 * exported by nf_conntrack module.
 */
static inline struct nf_conn_labels *nf_ct_labels_find(const struct nf_conn *ct)
{
#ifdef CONFIG_NF_CONNTRACK_LABELS
	struct nf_ct_ext *ext = ct->ext;

	if (!ext || !__nf_ct_ext_exist(ext, NF_CT_EXT_LABELS))
		return NULL;

	return (void *)ct->ext + ct->ext->offset[NF_CT_EXT_LABELS];
#else
	return NULL;
#endif
}

static inline struct nf_conn_labels *nf_ct_labels_ext_add(struct nf_conn *ct)
{
#ifdef CONFIG_NF_CONNTRACK_LABELS
	struct net *net = nf_ct_net(ct);

	if (net->ct.labels_used == 0)
		return NULL;

	return nf_ct_ext_add(ct, NF_CT_EXT_LABELS, GFP_ATOMIC);
#else
	return NULL;
#endif
}

int nf_connlabels_replace(struct nf_conn *ct,
			  const u32 *data, const u32 *mask, unsigned int words);

#ifdef CONFIG_NF_CONNTRACK_LABELS
int nf_conntrack_labels_init(void);
int nf_connlabels_get(struct net *net, unsigned int bit);
void nf_connlabels_put(struct net *net);
#else
static inline int nf_connlabels_get(struct net *net, unsigned int bit) { return 0; }
static inline void nf_connlabels_put(struct net *net) {}
#endif

#endif /* _NF_CONNTRACK_LABELS_H */
