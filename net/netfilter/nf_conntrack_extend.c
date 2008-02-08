/* Structure dynamic extension infrastructure
 * Copyright (C) 2004 Rusty Russell IBM Corporation
 * Copyright (C) 2007 Netfilter Core Team <coreteam@netfilter.org>
 * Copyright (C) 2007 USAGI/WIDE Project <http://www.linux-ipv6.org>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <net/netfilter/nf_conntrack_extend.h>

static struct nf_ct_ext_type *nf_ct_ext_types[NF_CT_EXT_NUM];
static DEFINE_MUTEX(nf_ct_ext_type_mutex);

/* Horrible trick to figure out smallest amount worth kmallocing. */
#define CACHE(x) (x) + 0 *
enum {
	NF_CT_EXT_MIN_SIZE =
#include <linux/kmalloc_sizes.h>
	1 };
#undef CACHE

void __nf_ct_ext_destroy(struct nf_conn *ct)
{
	unsigned int i;
	struct nf_ct_ext_type *t;

	for (i = 0; i < NF_CT_EXT_NUM; i++) {
		if (!nf_ct_ext_exist(ct, i))
			continue;

		rcu_read_lock();
		t = rcu_dereference(nf_ct_ext_types[i]);

		/* Here the nf_ct_ext_type might have been unregisterd.
		 * I.e., it has responsible to cleanup private
		 * area in all conntracks when it is unregisterd.
		 */
		if (t && t->destroy)
			t->destroy(ct);
		rcu_read_unlock();
	}
}
EXPORT_SYMBOL(__nf_ct_ext_destroy);

static void *
nf_ct_ext_create(struct nf_ct_ext **ext, enum nf_ct_ext_id id, gfp_t gfp)
{
	unsigned int off, len, real_len;
	struct nf_ct_ext_type *t;

	rcu_read_lock();
	t = rcu_dereference(nf_ct_ext_types[id]);
	BUG_ON(t == NULL);
	off = ALIGN(sizeof(struct nf_ct_ext), t->align);
	len = off + t->len;
	real_len = t->alloc_size;
	rcu_read_unlock();

	*ext = kzalloc(real_len, gfp);
	if (!*ext)
		return NULL;

	(*ext)->offset[id] = off;
	(*ext)->len = len;
	(*ext)->real_len = real_len;

	return (void *)(*ext) + off;
}

void *__nf_ct_ext_add(struct nf_conn *ct, enum nf_ct_ext_id id, gfp_t gfp)
{
	struct nf_ct_ext *new;
	int i, newlen, newoff;
	struct nf_ct_ext_type *t;

	if (!ct->ext)
		return nf_ct_ext_create(&ct->ext, id, gfp);

	if (nf_ct_ext_exist(ct, id))
		return NULL;

	rcu_read_lock();
	t = rcu_dereference(nf_ct_ext_types[id]);
	BUG_ON(t == NULL);

	newoff = ALIGN(ct->ext->len, t->align);
	newlen = newoff + t->len;
	rcu_read_unlock();

	if (newlen >= ct->ext->real_len) {
		new = kmalloc(newlen, gfp);
		if (!new)
			return NULL;

		memcpy(new, ct->ext, ct->ext->len);

		for (i = 0; i < NF_CT_EXT_NUM; i++) {
			if (!nf_ct_ext_exist(ct, i))
				continue;

			rcu_read_lock();
			t = rcu_dereference(nf_ct_ext_types[i]);
			if (t && t->move)
				t->move((void *)new + new->offset[i],
					(void *)ct->ext + ct->ext->offset[i]);
			rcu_read_unlock();
		}
		kfree(ct->ext);
		new->real_len = newlen;
		ct->ext = new;
	}

	ct->ext->offset[id] = newoff;
	ct->ext->len = newlen;
	memset((void *)ct->ext + newoff, 0, newlen - newoff);
	return (void *)ct->ext + newoff;
}
EXPORT_SYMBOL(__nf_ct_ext_add);

static void update_alloc_size(struct nf_ct_ext_type *type)
{
	int i, j;
	struct nf_ct_ext_type *t1, *t2;
	enum nf_ct_ext_id min = 0, max = NF_CT_EXT_NUM - 1;

	/* unnecessary to update all types */
	if ((type->flags & NF_CT_EXT_F_PREALLOC) == 0) {
		min = type->id;
		max = type->id;
	}

	/* This assumes that extended areas in conntrack for the types
	   whose NF_CT_EXT_F_PREALLOC bit set are allocated in order */
	for (i = min; i <= max; i++) {
		t1 = nf_ct_ext_types[i];
		if (!t1)
			continue;

		t1->alloc_size = sizeof(struct nf_ct_ext)
				 + ALIGN(sizeof(struct nf_ct_ext), t1->align)
				 + t1->len;
		for (j = 0; j < NF_CT_EXT_NUM; j++) {
			t2 = nf_ct_ext_types[j];
			if (t2 == NULL || t2 == t1 ||
			    (t2->flags & NF_CT_EXT_F_PREALLOC) == 0)
				continue;

			t1->alloc_size = ALIGN(t1->alloc_size, t2->align)
					 + t2->len;
		}
		if (t1->alloc_size < NF_CT_EXT_MIN_SIZE)
			t1->alloc_size = NF_CT_EXT_MIN_SIZE;
	}
}

/* This MUST be called in process context. */
int nf_ct_extend_register(struct nf_ct_ext_type *type)
{
	int ret = 0;

	mutex_lock(&nf_ct_ext_type_mutex);
	if (nf_ct_ext_types[type->id]) {
		ret = -EBUSY;
		goto out;
	}

	/* This ensures that nf_ct_ext_create() can allocate enough area
	   before updating alloc_size */
	type->alloc_size = ALIGN(sizeof(struct nf_ct_ext), type->align)
			   + type->len;
	rcu_assign_pointer(nf_ct_ext_types[type->id], type);
	update_alloc_size(type);
out:
	mutex_unlock(&nf_ct_ext_type_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_ct_extend_register);

/* This MUST be called in process context. */
void nf_ct_extend_unregister(struct nf_ct_ext_type *type)
{
	mutex_lock(&nf_ct_ext_type_mutex);
	rcu_assign_pointer(nf_ct_ext_types[type->id], NULL);
	update_alloc_size(type);
	mutex_unlock(&nf_ct_ext_type_mutex);
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(nf_ct_extend_unregister);
