// SPDX-License-Identifier: GPL-2.0-or-later
/* Structure dynamic extension infrastructure
 * Copyright (C) 2004 Rusty Russell IBM Corporation
 * Copyright (C) 2007 Netfilter Core Team <coreteam@netfilter.org>
 * Copyright (C) 2007 USAGI/WIDE Project <http://www.linux-ipv6.org>
 */
#include <linux/kernel.h>
#include <linux/kmemleak.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <net/netfilter/nf_conntrack_extend.h>

#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_acct.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_timestamp.h>
#include <net/netfilter/nf_conntrack_timeout.h>
#include <net/netfilter/nf_conntrack_labels.h>
#include <net/netfilter/nf_conntrack_synproxy.h>
#include <net/netfilter/nf_conntrack_act_ct.h>
#include <net/netfilter/nf_nat.h>

static struct nf_ct_ext_type __rcu *nf_ct_ext_types[NF_CT_EXT_NUM];
static DEFINE_MUTEX(nf_ct_ext_type_mutex);
#define NF_CT_EXT_PREALLOC	128u /* conntrack events are on by default */

static const u8 nf_ct_ext_type_len[NF_CT_EXT_NUM] = {
	[NF_CT_EXT_HELPER] = sizeof(struct nf_conn_help),
#if IS_ENABLED(CONFIG_NF_NAT)
	[NF_CT_EXT_NAT] = sizeof(struct nf_conn_nat),
#endif
	[NF_CT_EXT_SEQADJ] = sizeof(struct nf_conn_seqadj),
	[NF_CT_EXT_ACCT] = sizeof(struct nf_conn_acct),
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	[NF_CT_EXT_ECACHE] = sizeof(struct nf_conntrack_ecache),
#endif
#ifdef CONFIG_NF_CONNTRACK_TIMESTAMP
	[NF_CT_EXT_TSTAMP] = sizeof(struct nf_conn_acct),
#endif
#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
	[NF_CT_EXT_TIMEOUT] = sizeof(struct nf_conn_tstamp),
#endif
#ifdef CONFIG_NF_CONNTRACK_LABELS
	[NF_CT_EXT_LABELS] = sizeof(struct nf_conn_labels),
#endif
#if IS_ENABLED(CONFIG_NETFILTER_SYNPROXY)
	[NF_CT_EXT_SYNPROXY] = sizeof(struct nf_conn_synproxy),
#endif
#if IS_ENABLED(CONFIG_NET_ACT_CT)
	[NF_CT_EXT_ACT_CT] = sizeof(struct nf_conn_act_ct_ext),
#endif
};

static __always_inline unsigned int total_extension_size(void)
{
	/* remember to add new extensions below */
	BUILD_BUG_ON(NF_CT_EXT_NUM > 10);

	return sizeof(struct nf_ct_ext) +
	       sizeof(struct nf_conn_help)
#if IS_ENABLED(CONFIG_NF_NAT)
		+ sizeof(struct nf_conn_nat)
#endif
		+ sizeof(struct nf_conn_seqadj)
		+ sizeof(struct nf_conn_acct)
#ifdef CONFIG_NF_CONNTRACK_EVENTS
		+ sizeof(struct nf_conntrack_ecache)
#endif
#ifdef CONFIG_NF_CONNTRACK_TIMESTAMP
		+ sizeof(struct nf_conn_tstamp)
#endif
#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
		+ sizeof(struct nf_conn_timeout)
#endif
#ifdef CONFIG_NF_CONNTRACK_LABELS
		+ sizeof(struct nf_conn_labels)
#endif
#if IS_ENABLED(CONFIG_NETFILTER_SYNPROXY)
		+ sizeof(struct nf_conn_synproxy)
#endif
#if IS_ENABLED(CONFIG_NET_ACT_CT)
		+ sizeof(struct nf_conn_act_ct_ext)
#endif
	;
}

void *nf_ct_ext_add(struct nf_conn *ct, enum nf_ct_ext_id id, gfp_t gfp)
{
	unsigned int newlen, newoff, oldlen, alloc;
	struct nf_ct_ext *new;

	/* Conntrack must not be confirmed to avoid races on reallocation. */
	WARN_ON(nf_ct_is_confirmed(ct));


	if (ct->ext) {
		const struct nf_ct_ext *old = ct->ext;

		if (__nf_ct_ext_exist(old, id))
			return NULL;
		oldlen = old->len;
	} else {
		oldlen = sizeof(*new);
	}

	newoff = ALIGN(oldlen, __alignof__(struct nf_ct_ext));
	newlen = newoff + nf_ct_ext_type_len[id];

	alloc = max(newlen, NF_CT_EXT_PREALLOC);
	new = krealloc(ct->ext, alloc, gfp);
	if (!new)
		return NULL;

	if (!ct->ext)
		memset(new->offset, 0, sizeof(new->offset));

	new->offset[id] = newoff;
	new->len = newlen;
	memset((void *)new + newoff, 0, newlen - newoff);

	ct->ext = new;
	return (void *)new + newoff;
}
EXPORT_SYMBOL(nf_ct_ext_add);

/* This MUST be called in process context. */
int nf_ct_extend_register(const struct nf_ct_ext_type *type)
{
	int ret = 0;

	/* struct nf_ct_ext uses u8 to store offsets/size */
	BUILD_BUG_ON(total_extension_size() > 255u);

	mutex_lock(&nf_ct_ext_type_mutex);
	if (nf_ct_ext_types[type->id]) {
		ret = -EBUSY;
		goto out;
	}

	rcu_assign_pointer(nf_ct_ext_types[type->id], type);
out:
	mutex_unlock(&nf_ct_ext_type_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_ct_extend_register);

/* This MUST be called in process context. */
void nf_ct_extend_unregister(const struct nf_ct_ext_type *type)
{
	mutex_lock(&nf_ct_ext_type_mutex);
	RCU_INIT_POINTER(nf_ct_ext_types[type->id], NULL);
	mutex_unlock(&nf_ct_ext_type_mutex);
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(nf_ct_extend_unregister);
