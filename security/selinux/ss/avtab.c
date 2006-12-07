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
	newnode = kmem_cache_alloc(avtab_node_cachep, GFP_KERNEL);
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
	u16 specified = key->specified & ~(AVTAB_ENABLED|AVTAB_ENABLED_OLD);

	if (!h)
		return -EINVAL;

	hvalue = AVTAB_HASH(key);
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
	u16 specified = key->specified & ~(AVTAB_ENABLED|AVTAB_ENABLED_OLD);

	if (!h)
		return NULL;
	hvalue = AVTAB_HASH(key);
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
	newnode = avtab_insert_node(h, hvalue, prev, cur, key, datum);

	return newnode;
}

struct avtab_datum *avtab_search(struct avtab *h, struct avtab_key *key)
{
	int hvalue;
	struct avtab_node *cur;
	u16 specified = key->specified & ~(AVTAB_ENABLED|AVTAB_ENABLED_OLD);

	if (!h)
		return NULL;

	hvalue = AVTAB_HASH(key);
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

	if (!h)
		return NULL;

	hvalue = AVTAB_HASH(key);
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

static uint16_t spec_order[] = {
	AVTAB_ALLOWED,
	AVTAB_AUDITDENY,
	AVTAB_AUDITALLOW,
	AVTAB_TRANSITION,
	AVTAB_CHANGE,
	AVTAB_MEMBER
};

int avtab_read_item(void *fp, u32 vers, struct avtab *a,
	            int (*insertf)(struct avtab *a, struct avtab_key *k,
				   struct avtab_datum *d, void *p),
		    void *p)
{
	__le16 buf16[4];
	u16 enabled;
	__le32 buf32[7];
	u32 items, items2, val;
	struct avtab_key key;
	struct avtab_datum datum;
	int i, rc;

	memset(&key, 0, sizeof(struct avtab_key));
	memset(&datum, 0, sizeof(struct avtab_datum));

	if (vers < POLICYDB_VERSION_AVTAB) {
		rc = next_entry(buf32, fp, sizeof(u32));
		if (rc < 0) {
			printk(KERN_ERR "security: avtab: truncated entry\n");
			return -1;
		}
		items2 = le32_to_cpu(buf32[0]);
		if (items2 > ARRAY_SIZE(buf32)) {
			printk(KERN_ERR "security: avtab: entry overflow\n");
			return -1;

		}
		rc = next_entry(buf32, fp, sizeof(u32)*items2);
		if (rc < 0) {
			printk(KERN_ERR "security: avtab: truncated entry\n");
			return -1;
		}
		items = 0;

		val = le32_to_cpu(buf32[items++]);
		key.source_type = (u16)val;
		if (key.source_type != val) {
			printk("security: avtab: truncated source type\n");
			return -1;
		}
		val = le32_to_cpu(buf32[items++]);
		key.target_type = (u16)val;
		if (key.target_type != val) {
			printk("security: avtab: truncated target type\n");
			return -1;
		}
		val = le32_to_cpu(buf32[items++]);
		key.target_class = (u16)val;
		if (key.target_class != val) {
			printk("security: avtab: truncated target class\n");
			return -1;
		}

		val = le32_to_cpu(buf32[items++]);
		enabled = (val & AVTAB_ENABLED_OLD) ? AVTAB_ENABLED : 0;

		if (!(val & (AVTAB_AV | AVTAB_TYPE))) {
			printk("security: avtab: null entry\n");
			return -1;
		}
		if ((val & AVTAB_AV) &&
		    (val & AVTAB_TYPE)) {
			printk("security: avtab: entry has both access vectors and types\n");
			return -1;
		}

		for (i = 0; i < ARRAY_SIZE(spec_order); i++) {
			if (val & spec_order[i]) {
				key.specified = spec_order[i] | enabled;
				datum.data = le32_to_cpu(buf32[items++]);
				rc = insertf(a, &key, &datum, p);
				if (rc) return rc;
			}
		}

		if (items != items2) {
			printk("security: avtab: entry only had %d items, expected %d\n", items2, items);
			return -1;
		}
		return 0;
	}

	rc = next_entry(buf16, fp, sizeof(u16)*4);
	if (rc < 0) {
		printk("security: avtab: truncated entry\n");
		return -1;
	}

	items = 0;
	key.source_type = le16_to_cpu(buf16[items++]);
	key.target_type = le16_to_cpu(buf16[items++]);
	key.target_class = le16_to_cpu(buf16[items++]);
	key.specified = le16_to_cpu(buf16[items++]);

	rc = next_entry(buf32, fp, sizeof(u32));
	if (rc < 0) {
		printk("security: avtab: truncated entry\n");
		return -1;
	}
	datum.data = le32_to_cpu(*buf32);
	return insertf(a, &key, &datum, p);
}

static int avtab_insertf(struct avtab *a, struct avtab_key *k,
			 struct avtab_datum *d, void *p)
{
	return avtab_insert(a, k, d);
}

int avtab_read(struct avtab *a, void *fp, u32 vers)
{
	int rc;
	__le32 buf[1];
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
		rc = avtab_read_item(fp,vers, a, avtab_insertf, NULL);
		if (rc) {
			if (rc == -ENOMEM)
				printk(KERN_ERR "security: avtab: out of memory\n");
			else if (rc == -EEXIST)
				printk(KERN_ERR "security: avtab: duplicate entry\n");
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
					      0, SLAB_PANIC, NULL, NULL);
}

void avtab_cache_destroy(void)
{
	kmem_cache_destroy (avtab_node_cachep);
}
