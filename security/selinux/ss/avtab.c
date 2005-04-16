/*
 * Implementation of the access vector table type.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */

/* Updated: Frank Mayer <mayerf@tresys.com> and Karl MacMillan <kmacmillan@tresys.com>
 *
 * 	Added conditional policy language extensions
 *
 * Copyright (C) 2003 Tresys Technology, LLC
 *	This program is free software; you can redistribute it and/or modify
 *  	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, version 2.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/errno.h>

#include "avtab.h"
#include "policydb.h"

#define AVTAB_HASH(keyp) \
((keyp->target_class + \
 (keyp->target_type << 2) + \
 (keyp->source_type << 9)) & \
 AVTAB_HASH_MASK)

static kmem_cache_t *avtab_node_cachep;

static struct avtab_node*
avtab_insert_node(struct avtab *h, int hvalue,
		  struct avtab_node * prev, struct avtab_node * cur,
		  struct avtab_key *key, struct avtab_datum *datum)
{
	struct avtab_node * newnode;
	newnode = kmem_cache_alloc(avtab_node_cachep, SLAB_KERNEL);
	if (newnode == NULL)
		return NULL;
	memset(newnode, 0, sizeof(struct avtab_node));
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

	if (!h)
		return -EINVAL;

	hvalue = AVTAB_HASH(key);
	for (prev = NULL, cur = h->htable[hvalue];
	     cur;
	     prev = cur, cur = cur->next) {
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class == cur->key.target_class &&
		    (datum->specified & cur->datum.specified))
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
	if(!newnode)
		return -ENOMEM;

	return 0;
}

/* Unlike avtab_insert(), this function allow multiple insertions of the same
 * key/specified mask into the table, as needed by the conditional avtab.
 * It also returns a pointer to the node inserted.
 */
struct avtab_node *
avtab_insert_nonunique(struct avtab * h, struct avtab_key * key, struct avtab_datum * datum)
{
	int hvalue;
	struct avtab_node *prev, *cur, *newnode;

	if (!h)
		return NULL;
	hvalue = AVTAB_HASH(key);
	for (prev = NULL, cur = h->htable[hvalue];
	     cur;
	     prev = cur, cur = cur->next) {
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class == cur->key.target_class &&
		    (datum->specified & cur->datum.specified))
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
	newnode = avtab_insert_node(h, hvalue, prev, cur, key, datum);

	return newnode;
}

struct avtab_datum *avtab_search(struct avtab *h, struct avtab_key *key, int specified)
{
	int hvalue;
	struct avtab_node *cur;

	if (!h)
		return NULL;

	hvalue = AVTAB_HASH(key);
	for (cur = h->htable[hvalue]; cur; cur = cur->next) {
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class == cur->key.target_class &&
		    (specified & cur->datum.specified))
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
avtab_search_node(struct avtab *h, struct avtab_key *key, int specified)
{
	int hvalue;
	struct avtab_node *cur;

	if (!h)
		return NULL;

	hvalue = AVTAB_HASH(key);
	for (cur = h->htable[hvalue]; cur; cur = cur->next) {
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class == cur->key.target_class &&
		    (specified & cur->datum.specified))
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

	for (cur = node->next; cur; cur = cur->next) {
		if (node->key.source_type == cur->key.source_type &&
		    node->key.target_type == cur->key.target_type &&
		    node->key.target_class == cur->key.target_class &&
		    (specified & cur->datum.specified))
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

	for (i = 0; i < AVTAB_SIZE; i++) {
		cur = h->htable[i];
		while (cur != NULL) {
			temp = cur;
			cur = cur->next;
			kmem_cache_free(avtab_node_cachep, temp);
		}
		h->htable[i] = NULL;
	}
	vfree(h->htable);
	h->htable = NULL;
}


int avtab_init(struct avtab *h)
{
	int i;

	h->htable = vmalloc(sizeof(*(h->htable)) * AVTAB_SIZE);
	if (!h->htable)
		return -ENOMEM;
	for (i = 0; i < AVTAB_SIZE; i++)
		h->htable[i] = NULL;
	h->nel = 0;
	return 0;
}

void avtab_hash_eval(struct avtab *h, char *tag)
{
	int i, chain_len, slots_used, max_chain_len;
	struct avtab_node *cur;

	slots_used = 0;
	max_chain_len = 0;
	for (i = 0; i < AVTAB_SIZE; i++) {
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
		}
	}

	printk(KERN_INFO "%s:  %d entries and %d/%d buckets used, longest "
	       "chain length %d\n", tag, h->nel, slots_used, AVTAB_SIZE,
	       max_chain_len);
}

int avtab_read_item(void *fp, struct avtab_datum *avdatum, struct avtab_key *avkey)
{
	u32 buf[7];
	u32 items, items2;
	int rc;

	memset(avkey, 0, sizeof(struct avtab_key));
	memset(avdatum, 0, sizeof(struct avtab_datum));

	rc = next_entry(buf, fp, sizeof(u32));
	if (rc < 0) {
		printk(KERN_ERR "security: avtab: truncated entry\n");
		goto bad;
	}
	items2 = le32_to_cpu(buf[0]);
	if (items2 > ARRAY_SIZE(buf)) {
		printk(KERN_ERR "security: avtab: entry overflow\n");
		goto bad;
	}
	rc = next_entry(buf, fp, sizeof(u32)*items2);
	if (rc < 0) {
		printk(KERN_ERR "security: avtab: truncated entry\n");
		goto bad;
	}
	items = 0;
	avkey->source_type = le32_to_cpu(buf[items++]);
	avkey->target_type = le32_to_cpu(buf[items++]);
	avkey->target_class = le32_to_cpu(buf[items++]);
	avdatum->specified = le32_to_cpu(buf[items++]);
	if (!(avdatum->specified & (AVTAB_AV | AVTAB_TYPE))) {
		printk(KERN_ERR "security: avtab: null entry\n");
		goto bad;
	}
	if ((avdatum->specified & AVTAB_AV) &&
	    (avdatum->specified & AVTAB_TYPE)) {
		printk(KERN_ERR "security: avtab: entry has both access vectors and types\n");
		goto bad;
	}
	if (avdatum->specified & AVTAB_AV) {
		if (avdatum->specified & AVTAB_ALLOWED)
			avtab_allowed(avdatum) = le32_to_cpu(buf[items++]);
		if (avdatum->specified & AVTAB_AUDITDENY)
			avtab_auditdeny(avdatum) = le32_to_cpu(buf[items++]);
		if (avdatum->specified & AVTAB_AUDITALLOW)
			avtab_auditallow(avdatum) = le32_to_cpu(buf[items++]);
	} else {
		if (avdatum->specified & AVTAB_TRANSITION)
			avtab_transition(avdatum) = le32_to_cpu(buf[items++]);
		if (avdatum->specified & AVTAB_CHANGE)
			avtab_change(avdatum) = le32_to_cpu(buf[items++]);
		if (avdatum->specified & AVTAB_MEMBER)
			avtab_member(avdatum) = le32_to_cpu(buf[items++]);
	}
	if (items != items2) {
		printk(KERN_ERR "security: avtab: entry only had %d items, expected %d\n",
		       items2, items);
		goto bad;
	}

	return 0;
bad:
	return -1;
}

int avtab_read(struct avtab *a, void *fp, u32 config)
{
	int rc;
	struct avtab_key avkey;
	struct avtab_datum avdatum;
	u32 buf[1];
	u32 nel, i;


	rc = next_entry(buf, fp, sizeof(u32));
	if (rc < 0) {
		printk(KERN_ERR "security: avtab: truncated table\n");
		goto bad;
	}
	nel = le32_to_cpu(buf[0]);
	if (!nel) {
		printk(KERN_ERR "security: avtab: table is empty\n");
		rc = -EINVAL;
		goto bad;
	}
	for (i = 0; i < nel; i++) {
		if (avtab_read_item(fp, &avdatum, &avkey)) {
			rc = -EINVAL;
			goto bad;
		}
		rc = avtab_insert(a, &avkey, &avdatum);
		if (rc) {
			if (rc == -ENOMEM)
				printk(KERN_ERR "security: avtab: out of memory\n");
			if (rc == -EEXIST)
				printk(KERN_ERR "security: avtab: duplicate entry\n");
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
					      0, SLAB_PANIC, NULL, NULL);
}

void avtab_cache_destroy(void)
{
	kmem_cache_destroy (avtab_node_cachep);
}
