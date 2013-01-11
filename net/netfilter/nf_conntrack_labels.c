/*
 * test/set flag bits stored in conntrack extension area.
 *
 * (C) 2013 Astaro GmbH & Co KG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ctype.h>
#include <linux/export.h>
#include <linux/jhash.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/slab.h>

#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_labels.h>

static unsigned int label_bits(const struct nf_conn_labels *l)
{
	unsigned int longs = l->words;
	return longs * BITS_PER_LONG;
}

bool nf_connlabel_match(const struct nf_conn *ct, u16 bit)
{
	struct nf_conn_labels *labels = nf_ct_labels_find(ct);

	if (!labels)
		return false;

	return bit < label_bits(labels) && test_bit(bit, labels->bits);
}
EXPORT_SYMBOL_GPL(nf_connlabel_match);

int nf_connlabel_set(struct nf_conn *ct, u16 bit)
{
	struct nf_conn_labels *labels = nf_ct_labels_find(ct);

	if (!labels || bit >= label_bits(labels))
		return -ENOSPC;

	if (test_bit(bit, labels->bits))
		return 0;

	if (test_and_set_bit(bit, labels->bits))
		return 0;

	return 0;
}
EXPORT_SYMBOL_GPL(nf_connlabel_set);

static struct nf_ct_ext_type labels_extend __read_mostly = {
	.len    = sizeof(struct nf_conn_labels),
	.align  = __alignof__(struct nf_conn_labels),
	.id     = NF_CT_EXT_LABELS,
};

int nf_conntrack_labels_init(struct net *net)
{
	if (net_eq(net, &init_net))
		return nf_ct_extend_register(&labels_extend);
	return 0;
}

void nf_conntrack_labels_fini(struct net *net)
{
	if (net_eq(net, &init_net))
		nf_ct_extend_unregister(&labels_extend);
}
