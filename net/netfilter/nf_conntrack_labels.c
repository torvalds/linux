/*
 * test/set flag bits stored in conntrack extension area.
 *
 * (C) 2013 Astaro GmbH & Co KG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <linux/types.h>

#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_labels.h>

static spinlock_t nf_connlabels_lock;

int nf_connlabel_set(struct nf_conn *ct, u16 bit)
{
	struct nf_conn_labels *labels = nf_ct_labels_find(ct);

	if (!labels)
		return -ENOSPC;

	if (test_bit(bit, labels->bits))
		return 0;

	if (!test_and_set_bit(bit, labels->bits))
		nf_conntrack_event_cache(IPCT_LABEL, ct);

	return 0;
}
EXPORT_SYMBOL_GPL(nf_connlabel_set);

static int replace_u32(u32 *address, u32 mask, u32 new)
{
	u32 old, tmp;

	do {
		old = *address;
		tmp = (old & mask) ^ new;
		if (old == tmp)
			return 0;
	} while (cmpxchg(address, old, tmp) != old);

	return 1;
}

int nf_connlabels_replace(struct nf_conn *ct,
			  const u32 *data,
			  const u32 *mask, unsigned int words32)
{
	struct nf_conn_labels *labels;
	unsigned int size, i;
	int changed = 0;
	u32 *dst;

	labels = nf_ct_labels_find(ct);
	if (!labels)
		return -ENOSPC;

	size = sizeof(labels->bits);
	if (size < (words32 * sizeof(u32)))
		words32 = size / sizeof(u32);

	dst = (u32 *) labels->bits;
	for (i = 0; i < words32; i++)
		changed |= replace_u32(&dst[i], mask ? ~mask[i] : 0, data[i]);

	size /= sizeof(u32);
	for (i = words32; i < size; i++) /* pad */
		replace_u32(&dst[i], 0, 0);

	if (changed)
		nf_conntrack_event_cache(IPCT_LABEL, ct);
	return 0;
}
EXPORT_SYMBOL_GPL(nf_connlabels_replace);

int nf_connlabels_get(struct net *net, unsigned int bits)
{
	if (BIT_WORD(bits) >= NF_CT_LABELS_MAX_SIZE / sizeof(long))
		return -ERANGE;

	spin_lock(&nf_connlabels_lock);
	net->ct.labels_used++;
	spin_unlock(&nf_connlabels_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(nf_connlabels_get);

void nf_connlabels_put(struct net *net)
{
	spin_lock(&nf_connlabels_lock);
	net->ct.labels_used--;
	spin_unlock(&nf_connlabels_lock);
}
EXPORT_SYMBOL_GPL(nf_connlabels_put);

static struct nf_ct_ext_type labels_extend __read_mostly = {
	.len    = sizeof(struct nf_conn_labels),
	.align  = __alignof__(struct nf_conn_labels),
	.id     = NF_CT_EXT_LABELS,
};

int nf_conntrack_labels_init(void)
{
	BUILD_BUG_ON(NF_CT_LABELS_MAX_SIZE / sizeof(long) >= U8_MAX);

	spin_lock_init(&nf_connlabels_lock);
	return nf_ct_extend_register(&labels_extend);
}

void nf_conntrack_labels_fini(void)
{
	nf_ct_extend_unregister(&labels_extend);
}
