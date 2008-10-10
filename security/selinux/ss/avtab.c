/*
 * Implementation of the access vector table type.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */

/* Updated: Frank Mayer <mayerf@tresys.com> and Karl MacMillan <kmacmillan@tresys.com>
 *
 *	Added conditional policy language extensions
 *
 * Copyright (C) 2003 Tresys Technology, LLC
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, version 2.
 *
 * Updated: Yuichi Nakamura <ynakam@hitachisoft.jp>
 *	Tuned number of hash slots for avtab to reduce memory usage
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include "avtab.h"
#include "policydb.h"

static struct kmem_cache *avtab_node_cachep;

static inline int avtab_hash(struct avtab_key *keyp, u16 mask)
{
	return ((keyp->target_class + (keyp->target_type << 2) +
		 (keyp->source_type << 9)) & mask);
}

static struct avtab_node*
avtab_insert_node(struct avtab *h, int hvalue,
		  struct avtab_node *prev, struct avtab_node *cur,
		  struct avtab_key *key, struct avtab_datum *datum)
{
	struct avtab_node *newnode;
	newnode = kmem_cache_zalloc(avtab_node_cachep, GFP_KERNEL);
	if (newnode == NULL)
		return NULL;
	newnode->key = *key;
	newnode->datum = *datum;
	if (prev) {
		newnode->next = prev->next;
		prev->next = newnode;
	} else {
		newnode->next = h->htable[hvalue];
		h->htable[hvalue] = newnode;
	}

	h->nel++;
	return newnode;
}

static int avtab_insert(struct avtab *h, struct avtab_key *key, struct avtab_datum *datum)
{
	int hvalue;
	struct avtab_node *prev, *cur, *newnode;
	u16 specified = key->specified & ~(AVTAB_ENABLED|AVTAB_ENABLED_OLD);

	if (!h || !h->htable)
		return -EINVAL;

	hvalue = avtab_hash(key, h->mask);
	for (prev = NULL, cur = h->htable[hvalue];
	     cur;
	     prev = cur, cur = cur->next) {
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class == cur->key.target_class &&
		    (specified & cur->key.specified))
			return -EEXIST;
		if (key->source_type < cur->key.source_type)
			break;
		if (key->source_type == cur->key.source_type &&
		    key->target_type < cur->key.target_type)
			break;
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class < cur->key.target_class)
			break;
	}

	newnode = avtab_insert_node(h, hvalue, prev, cur, key, datum);
	if (!newnode)
		return -ENOMEM;

	return 0;
}

/* Unlike avtab_insert(), this function allow multiple insertions of the same
 * key/specified mask into the table, as needed by the conditional avtab.
 * It also returns a pointer to the node inserted.
 */
struct avtab_node *
avtab_insert_nonunique(struct avtab *h, struct avtab_key *key, struct avtab_datum *datum)
{
	int hvalue;
	struct avtab_node *prev, *cur;
	u16 specified = key->specified & ~(AVTAB_ENABLED|AVTAB_ENABLED_OLD);

	if (!h || !h->htable)
		return NULL;
	hvalue = avtab_hash(key, h->mask);
	for (prev = NULL, cur = h->htable[hvalue];
	     cur;
	     prev = cur, cur = cur->next) {
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class == cur->key.target_class &&
		    (specified & cur->key.specified))
			break;
		if (key->source_type < cur->key.source_type)
			break;
		if (key->source_type == cur->key.source_type &&
		    key->target_type < cur->key.target_type)
			break;
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class < cur->key.target_class)
			break;
	}
	return avtab_insert_node(h, hvalue, prev, cur, key, datum);
}

struct avtab_datum *avtab_search(struct avtab *h, struct avtab_key *key)
{
	int hvalue;
	struct avtab_node *cur;
	u16 specified = key->specified & ~(AVTAB_ENABLED|AVTAB_ENABLED_OLD);

	if (!h || !h->htable)
		return NULL;

	hvalue = avtab_hash(key, h->mask);
	for (cur = h->htable[hvalue]; cur; cur = cur->next) {
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class == cur->key.target_class &&
		    (specified & cur->key.specified))
			return &cur->datum;

		if (key->source_type < cur->key.source_type)
			break;
		if (key->source_type == cur->key.source_type &&
		    key->target_type < cur->key.target_type)
			break;
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class < cur->key.target_class)
			break;
	}

	return NULL;
}

/* This search function returns a node pointer, and can be used in
 * conjunction with avtab_search_next_node()
 */
struct avtab_node*
avtab_search_node(struct avtab *h, struct avtab_key *key)
{
	int hvalue;
	struct avtab_node *cur;
	u16 specified = key->specified & ~(AVTAB_ENABLED|AVTAB_ENABLED_OLD);

	if (!h || !h->htable)
		return NULL;

	hvalue = avtab_hash(key, h->mask);
	for (cur = h->htable[hvalue]; cur; cur = cur->next) {
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class == cur->key.target_class &&
		    (specified & cur->key.specified))
			return cur;

		if (key->source_type < cur->key.source_type)
			break;
		if (key->source_type == cur->key.source_type &&
		    key->target_type < cur->key.target_type)
			break;
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class < cur->key.target_class)
			break;
	}
	return NULL;
}

struct avtab_node*
avtab_search_node_next(struct avtab_node *node, int specified)
{
	struct avtab_node *cur;

	if (!node)
		return NULL;

	specified &= ~(AVTAB_ENABLED|AVTAB_ENABLED_OLD);
	for (cur = node->next; cur; cur = cur->next) {
		if (node->key.source_type == cur->key.source_type &&
		    node->key.target_type == cur->key.target_type &&
		    node->key.target_class == cur->key.target_class &&
		    (specified & cur->key.specified))
			return cur;

		if (node->key.source_type < cur->key.source_type)
			break;
		if (node->key.source_type == cur->key.source_type &&
		    node->key.target_type < cur->key.target_type)
			break;
		if (node->key.source_type == cur->key.source_type &&
		    node->key.target_type == cur->key.target_type &&
		    node->key.target_class < cur->key.target_class)
			break;
	}
	return NULL;
}

void avtab_destroy(struct avtab *h)
{
	int i;
	struct avtab_node *cur, *temp;

	if (!h || !h->htable)
		return;

	for (i = 0; i < h->nslot; i++) {
		cur = h->htable[i];
		while (cur) {
			temp = cur;
			cur = cur->next;
			kmem_cache_free(avtab_node_cachep, temp);
		}
		h->htable[i] = NULL;
	}
	kfree(h->htable);
	h->htable = NULL;
	h->nslot = 0;
	h->mask = 0;
}

int avtab_init(struct avtab *h)
{
	h->htable = NULL;
	h->nel = 0;
	return 0;
}

int avtab_alloc(struct avtab *h, u32 nrules)
{
	u16 mask = 0;
	u32 shift = 0;
	u32 work = nrules;
	u32 nslot = 0;

	if (nrules == 0)
		goto avtab_alloc_out;

	while (work) {
		work  = work >> 1;
		shift++;
	}
	if (shift > 2)
		shift = shift - 2;
	nslot = 1 << shift;
	if (nslot > MAX_AVTAB_SIZE)
		nslot = MAX_AVTAB_SIZE;
	mask = nslot - 1;

	h->htable = kcalloc(nslot, sizeof(*(h->htable)), GFP_KERNEL);
	if (!h->htable)
		return -ENOMEM;

 avtab_alloc_out:
	h->nel = 0;
	h->nslot = nslot;
	h->mask = mask;
	printk(KERN_DEBUG "SELinux: %d avtab hash slots, %d rules.\n",
	       h->nslot, nrules);
	return 0;
}

void avtab_hash_eval(struct avtab *h, char *tag)
{
	int i, chain_len, slots_used, max_chain_len;
	unsigned long long chain2_len_sum;
	struct avtab_node *cur;

	slots_used = 0;
	max_chain_len = 0;
	chain2_len_sum = 0;
	for (i = 0; i < h->nslot; i++) {
		cur = h->htable[i];
		if (cur) {
			slots_used++;
			chain_len = 0;
			while (cur) {
				chain_len++;
				cur = cur->next;
			}

			if (chain_len > max_chain_len)
				max_chain_len = chain_len;
			chain2_len_sum += chain_len * chain_len;
		}
	}

	printk(KERN_DEBUG "SELinux: %s:  %d entries and %d/%d buckets used, "
	       "longest chain length %d sum of chain length^2 %llu\n",
	       tag, h->nel, slots_used, h->nslot, max_chain_len,
	       chain2_len_sum);
}

static uint16_t spec_order[] = {
	AVTAB_ALLOWED,
	AVTAB_AUDITDENY,
	AVTAB_AUDITALLOW,
	AVTAB_TRANSITION,
	AVTAB_CHANGE,
	AVTAB_MEMBER
};

int avtab_read_item(struct avtab *a, void *fp, struct policydb *pol,
		    int (*insertf)(struct avtab *a, struct avtab_key *k,
				   struct avtab_datum *d, void *p),
		    void *p)
{
	__le16 buf16[4];
	u16 enabled;
	__le32 buf32[7];
	u32 items, items2, val, vers = pol->policyvers;
	struct avtab_key key;
	struct avtab_datum datum;
	int i, rc;
	unsigned set;

	memset(&key, 0, sizeof(struct avtab_key));
	memset(&datum, 0, sizeof(struct avtab_datum));

	if (vers < POLICYDB_VERSION_AVTAB) {
		rc = next_entry(buf32, fp, sizeof(u32));
		if (rc < 0) {
			printk(KERN_ERR "SELinux: avtab: truncated entry\n");
			return -1;
		}
		items2 = le32_to_cpu(buf32[0]);
		if (items2 > ARRAY_SIZE(buf32)) {
			printk(KERN_ERR "SELinux: avtab: entry overflow\n");
			return -1;

		}
		rc = next_entry(buf32, fp, sizeof(u32)*items2);
		if (rc < 0) {
			printk(KERN_ERR "SELinux: avtab: truncated entry\n");
			return -1;
		}
		items = 0;

		val = le32_to_cpu(buf32[items++]);
		key.source_type = (u16)val;
		if (key.source_type != val) {
			printk(KERN_ERR "SELinux: avtab: truncated source type\n");
			return -1;
		}
		val = le32_to_cpu(buf32[items++]);
		key.target_type = (u16)val;
		if (key.target_type != val) {
			printk(KERN_ERR "SELinux: avtab: truncated target type\n");
			return -1;
		}
		val = le32_to_cpu(buf32[items++]);
		key.target_class = (u16)val;
		if (key.target_class != val) {
			printk(KERN_ERR "SELinux: avtab: truncated target class\n");
			return -1;
		}

		val = le32_to_cpu(buf32[items++]);
		enabled = (val & AVTAB_ENABLED_OLD) ? AVTAB_ENABLED : 0;

		if (!(val & (AVTAB_AV | AVTAB_TYPE))) {
			printk(KERN_ERR "SELinux: avtab: null entry\n");
			return -1;
		}
		if ((val & AVTAB_AV) &&
		    (val & AVTAB_TYPE)) {
			printk(KERN_ERR "SELinux: avtab: entry has both access vectors and types\n");
			return -1;
		}

		for (i = 0; i < ARRAY_SIZE(spec_order); i++) {
			if (val & spec_order[i]) {
				key.specified = spec_order[i] | enabled;
				datum.data = le32_to_cpu(buf32[items++]);
				rc = insertf(a, &key, &datum, p);
				if (rc)
					return rc;
			}
		}

		if (items != items2) {
			printk(KERN_ERR "SELinux: avtab: entry only had %d items, expected %d\n", items2, items);
			return -1;
		}
		return 0;
	}

	rc = next_entry(buf16, fp, sizeof(u16)*4);
	if (rc < 0) {
		printk(KERN_ERR "SELinux: avtab: truncated entry\n");
		return -1;
	}

	items = 0;
	key.source_type = le16_to_cpu(buf16[items++]);
	key.target_type = le16_to_cpu(buf16[items++]);
	key.target_class = le16_to_cpu(buf16[items++]);
	key.specified = le16_to_cpu(buf16[items++]);

	if (!policydb_type_isvalid(pol, key.source_type) ||
	    !policydb_type_isvalid(pol, key.target_type) ||
	    !policydb_class_isvalid(pol, key.target_class)) {
		printk(KERN_ERR "SELinux: avtab: invalid type or class\n");
		return -1;
	}

	set = 0;
	for (i = 0; i < ARRAY_SIZE(spec_order); i++) {
		if (key.specified & spec_order[i])
			set++;
	}
	if (!set || set > 1) {
		printk(KERN_ERR "SELinux:  avtab:  more than one specifier\n");
		return -1;
	}

	rc = next_entry(buf32, fp, sizeof(u32));
	if (rc < 0) {
		printk(KERN_ERR "SELinux: avtab: truncated entry\n");
		return -1;
	}
	datum.data = le32_to_cpu(*buf32);
	if ((key.specified & AVTAB_TYPE) &&
	    !policydb_type_isvalid(pol, datum.data)) {
		printk(KERN_ERR "SELinux: avtab: invalid type\n");
		return -1;
	}
	return insertf(a, &key, &datum, p);
}

static int avtab_insertf(struct avtab *a, struct avtab_key *k,
			 struct avtab_datum *d, void *p)
{
	return avtab_insert(a, k, d);
}

int avtab_read(struct avtab *a, void *fp, struct policydb *pol)
{
	int rc;
	__le32 buf[1];
	u32 nel, i;


	rc = next_entry(buf, fp, sizeof(u32));
	if (rc < 0) {
		printk(KERN_ERR "SELinux: avtab: truncated table\n");
		goto bad;
	}
	nel = le32_to_cpu(buf[0]);
	if (!nel) {
		printk(KERN_ERR "SELinux: avtab: table is empty\n");
		rc = -EINVAL;
		goto bad;
	}

	rc = avtab_alloc(a, nel);
	if (rc)
		goto bad;

	for (i = 0; i < nel; i++) {
		rc = avtab_read_item(a, fp, pol, avtab_insertf, NULL);
		if (rc) {
			if (rc == -ENOMEM)
				printk(KERN_ERR "SELinux: avtab: out of memory\n");
			else if (rc == -EEXIST)
				printk(KERN_ERR "SELinux: avtab: duplicate entry\n");
			else
				rc = -EINVAL;
			goto bad;
		}
	}

	rc = 0;
out:
	return rc;

bad:
	avtab_destroy(a);
	goto out;
}

void avtab_cache_init(void)
{
	avtab_node_cachep = kmem_cache_create("avtab_node",
					      sizeof(struct avtab_node),
					      0, SLAB_PANIC, NULL);
}

void avtab_cache_destroy(void)
{
	kmem_cache_destroy(avtab_node_cachep);
}
