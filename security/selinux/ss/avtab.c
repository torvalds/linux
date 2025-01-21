/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Implementation of the access vector table type.
 *
 * Author : Stephen Smalley, <stephen.smalley.work@gmail.com>
 */

/* Updated: Frank Mayer <mayerf@tresys.com> and
 *          Karl MacMillan <kmacmillan@tresys.com>
 *          Added conditional policy language extensions
 *          Copyright (C) 2003 Tresys Technology, LLC
 *
 * Updated: Yuichi Nakamura <ynakam@hitachisoft.jp>
 *          Tuned number of hash slots for avtab to reduce memory usage
 */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include "avtab.h"
#include "policydb.h"

static struct kmem_cache *avtab_node_cachep __ro_after_init;
static struct kmem_cache *avtab_xperms_cachep __ro_after_init;

/* Based on MurmurHash3, written by Austin Appleby and placed in the
 * public domain.
 */
static inline u32 avtab_hash(const struct avtab_key *keyp, u32 mask)
{
	static const u32 c1 = 0xcc9e2d51;
	static const u32 c2 = 0x1b873593;
	static const u32 r1 = 15;
	static const u32 r2 = 13;
	static const u32 m = 5;
	static const u32 n = 0xe6546b64;

	u32 hash = 0;

#define mix(input)                                         \
	do {                                               \
		u32 v = input;                             \
		v *= c1;                                   \
		v = (v << r1) | (v >> (32 - r1));          \
		v *= c2;                                   \
		hash ^= v;                                 \
		hash = (hash << r2) | (hash >> (32 - r2)); \
		hash = hash * m + n;                       \
	} while (0)

	mix(keyp->target_class);
	mix(keyp->target_type);
	mix(keyp->source_type);

#undef mix

	hash ^= hash >> 16;
	hash *= 0x85ebca6b;
	hash ^= hash >> 13;
	hash *= 0xc2b2ae35;
	hash ^= hash >> 16;

	return hash & mask;
}

static struct avtab_node *avtab_insert_node(struct avtab *h,
					    struct avtab_node **dst,
					    const struct avtab_key *key,
					    const struct avtab_datum *datum)
{
	struct avtab_node *newnode;
	struct avtab_extended_perms *xperms;
	newnode = kmem_cache_zalloc(avtab_node_cachep, GFP_KERNEL);
	if (newnode == NULL)
		return NULL;
	newnode->key = *key;

	if (key->specified & AVTAB_XPERMS) {
		xperms = kmem_cache_zalloc(avtab_xperms_cachep, GFP_KERNEL);
		if (xperms == NULL) {
			kmem_cache_free(avtab_node_cachep, newnode);
			return NULL;
		}
		*xperms = *(datum->u.xperms);
		newnode->datum.u.xperms = xperms;
	} else {
		newnode->datum.u.data = datum->u.data;
	}

	newnode->next = *dst;
	*dst = newnode;

	h->nel++;
	return newnode;
}

static int avtab_node_cmp(const struct avtab_key *key1,
			  const struct avtab_key *key2)
{
	u16 specified = key1->specified & ~(AVTAB_ENABLED | AVTAB_ENABLED_OLD);

	if (key1->source_type == key2->source_type &&
	    key1->target_type == key2->target_type &&
	    key1->target_class == key2->target_class &&
	    (specified & key2->specified))
		return 0;
	if (key1->source_type < key2->source_type)
		return -1;
	if (key1->source_type == key2->source_type &&
	    key1->target_type < key2->target_type)
		return -1;
	if (key1->source_type == key2->source_type &&
	    key1->target_type == key2->target_type &&
	    key1->target_class < key2->target_class)
		return -1;
	return 1;
}

static int avtab_insert(struct avtab *h, const struct avtab_key *key,
			const struct avtab_datum *datum)
{
	u32 hvalue;
	struct avtab_node *prev, *cur, *newnode;
	int cmp;

	if (!h || !h->nslot || h->nel == U32_MAX)
		return -EINVAL;

	hvalue = avtab_hash(key, h->mask);
	for (prev = NULL, cur = h->htable[hvalue]; cur;
	     prev = cur, cur = cur->next) {
		cmp = avtab_node_cmp(key, &cur->key);
		/* extended perms may not be unique */
		if (cmp == 0 && !(key->specified & AVTAB_XPERMS))
			return -EEXIST;
		if (cmp <= 0)
			break;
	}

	newnode = avtab_insert_node(h, prev ? &prev->next : &h->htable[hvalue],
				    key, datum);
	if (!newnode)
		return -ENOMEM;

	return 0;
}

/* Unlike avtab_insert(), this function allow multiple insertions of the same
 * key/specified mask into the table, as needed by the conditional avtab.
 * It also returns a pointer to the node inserted.
 */
struct avtab_node *avtab_insert_nonunique(struct avtab *h,
					  const struct avtab_key *key,
					  const struct avtab_datum *datum)
{
	u32 hvalue;
	struct avtab_node *prev, *cur;
	int cmp;

	if (!h || !h->nslot || h->nel == U32_MAX)
		return NULL;
	hvalue = avtab_hash(key, h->mask);
	for (prev = NULL, cur = h->htable[hvalue]; cur;
	     prev = cur, cur = cur->next) {
		cmp = avtab_node_cmp(key, &cur->key);
		if (cmp <= 0)
			break;
	}
	return avtab_insert_node(h, prev ? &prev->next : &h->htable[hvalue],
				 key, datum);
}

/* This search function returns a node pointer, and can be used in
 * conjunction with avtab_search_next_node()
 */
struct avtab_node *avtab_search_node(struct avtab *h,
				     const struct avtab_key *key)
{
	u32 hvalue;
	struct avtab_node *cur;
	int cmp;

	if (!h || !h->nslot)
		return NULL;

	hvalue = avtab_hash(key, h->mask);
	for (cur = h->htable[hvalue]; cur; cur = cur->next) {
		cmp = avtab_node_cmp(key, &cur->key);
		if (cmp == 0)
			return cur;
		if (cmp < 0)
			break;
	}
	return NULL;
}

struct avtab_node *avtab_search_node_next(struct avtab_node *node,
					  u16 specified)
{
	struct avtab_key tmp_key;
	struct avtab_node *cur;
	int cmp;

	if (!node)
		return NULL;
	tmp_key = node->key;
	tmp_key.specified = specified;
	for (cur = node->next; cur; cur = cur->next) {
		cmp = avtab_node_cmp(&tmp_key, &cur->key);
		if (cmp == 0)
			return cur;
		if (cmp < 0)
			break;
	}
	return NULL;
}

void avtab_destroy(struct avtab *h)
{
	u32 i;
	struct avtab_node *cur, *temp;

	if (!h)
		return;

	for (i = 0; i < h->nslot; i++) {
		cur = h->htable[i];
		while (cur) {
			temp = cur;
			cur = cur->next;
			if (temp->key.specified & AVTAB_XPERMS)
				kmem_cache_free(avtab_xperms_cachep,
						temp->datum.u.xperms);
			kmem_cache_free(avtab_node_cachep, temp);
		}
	}
	kvfree(h->htable);
	h->htable = NULL;
	h->nel = 0;
	h->nslot = 0;
	h->mask = 0;
}

void avtab_init(struct avtab *h)
{
	h->htable = NULL;
	h->nel = 0;
	h->nslot = 0;
	h->mask = 0;
}

static int avtab_alloc_common(struct avtab *h, u32 nslot)
{
	if (!nslot)
		return 0;

	h->htable = kvcalloc(nslot, sizeof(void *), GFP_KERNEL);
	if (!h->htable)
		return -ENOMEM;

	h->nslot = nslot;
	h->mask = nslot - 1;
	return 0;
}

int avtab_alloc(struct avtab *h, u32 nrules)
{
	int rc;
	u32 nslot = 0;

	if (nrules != 0) {
		nslot = nrules > 3 ? rounddown_pow_of_two(nrules / 2) : 2;
		if (nslot > MAX_AVTAB_HASH_BUCKETS)
			nslot = MAX_AVTAB_HASH_BUCKETS;

		rc = avtab_alloc_common(h, nslot);
		if (rc)
			return rc;
	}

	pr_debug("SELinux: %d avtab hash slots, %d rules.\n", nslot, nrules);
	return 0;
}

int avtab_alloc_dup(struct avtab *new, const struct avtab *orig)
{
	return avtab_alloc_common(new, orig->nslot);
}

#ifdef CONFIG_SECURITY_SELINUX_DEBUG
void avtab_hash_eval(struct avtab *h, const char *tag)
{
	u32 i, chain_len, slots_used, max_chain_len;
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
			chain2_len_sum +=
				(unsigned long long)chain_len * chain_len;
		}
	}

	pr_debug("SELinux: %s:  %d entries and %d/%d buckets used, "
		 "longest chain length %d, sum of chain length^2 %llu\n",
		 tag, h->nel, slots_used, h->nslot, max_chain_len,
		 chain2_len_sum);
}
#endif /* CONFIG_SECURITY_SELINUX_DEBUG */

/* clang-format off */
static const uint16_t spec_order[] = {
	AVTAB_ALLOWED,
	AVTAB_AUDITDENY,
	AVTAB_AUDITALLOW,
	AVTAB_TRANSITION,
	AVTAB_CHANGE,
	AVTAB_MEMBER,
	AVTAB_XPERMS_ALLOWED,
	AVTAB_XPERMS_AUDITALLOW,
	AVTAB_XPERMS_DONTAUDIT
};
/* clang-format on */

int avtab_read_item(struct avtab *a, void *fp, struct policydb *pol,
		    int (*insertf)(struct avtab *a, const struct avtab_key *k,
				   const struct avtab_datum *d, void *p),
		    void *p)
{
	__le16 buf16[4];
	u16 enabled;
	u32 items, items2, val, i;
	struct avtab_key key;
	struct avtab_datum datum;
	struct avtab_extended_perms xperms;
	__le32 buf32[ARRAY_SIZE(xperms.perms.p)];
	int rc;
	unsigned int set, vers = pol->policyvers;

	memset(&key, 0, sizeof(struct avtab_key));
	memset(&datum, 0, sizeof(struct avtab_datum));

	if (vers < POLICYDB_VERSION_AVTAB) {
		rc = next_entry(buf32, fp, sizeof(u32));
		if (rc) {
			pr_err("SELinux: avtab: truncated entry\n");
			return rc;
		}
		items2 = le32_to_cpu(buf32[0]);
		if (items2 > ARRAY_SIZE(buf32)) {
			pr_err("SELinux: avtab: entry overflow\n");
			return -EINVAL;
		}
		rc = next_entry(buf32, fp, sizeof(u32) * items2);
		if (rc) {
			pr_err("SELinux: avtab: truncated entry\n");
			return rc;
		}
		items = 0;

		val = le32_to_cpu(buf32[items++]);
		key.source_type = (u16)val;
		if (key.source_type != val) {
			pr_err("SELinux: avtab: truncated source type\n");
			return -EINVAL;
		}
		val = le32_to_cpu(buf32[items++]);
		key.target_type = (u16)val;
		if (key.target_type != val) {
			pr_err("SELinux: avtab: truncated target type\n");
			return -EINVAL;
		}
		val = le32_to_cpu(buf32[items++]);
		key.target_class = (u16)val;
		if (key.target_class != val) {
			pr_err("SELinux: avtab: truncated target class\n");
			return -EINVAL;
		}

		val = le32_to_cpu(buf32[items++]);
		enabled = (val & AVTAB_ENABLED_OLD) ? AVTAB_ENABLED : 0;

		if (!(val & (AVTAB_AV | AVTAB_TYPE))) {
			pr_err("SELinux: avtab: null entry\n");
			return -EINVAL;
		}
		if ((val & AVTAB_AV) && (val & AVTAB_TYPE)) {
			pr_err("SELinux: avtab: entry has both access vectors and types\n");
			return -EINVAL;
		}
		if (val & AVTAB_XPERMS) {
			pr_err("SELinux: avtab: entry has extended permissions\n");
			return -EINVAL;
		}

		for (i = 0; i < ARRAY_SIZE(spec_order); i++) {
			if (val & spec_order[i]) {
				key.specified = spec_order[i] | enabled;
				datum.u.data = le32_to_cpu(buf32[items++]);
				rc = insertf(a, &key, &datum, p);
				if (rc)
					return rc;
			}
		}

		if (items != items2) {
			pr_err("SELinux: avtab: entry only had %d items, expected %d\n",
			       items2, items);
			return -EINVAL;
		}
		return 0;
	}

	rc = next_entry(buf16, fp, sizeof(u16) * 4);
	if (rc) {
		pr_err("SELinux: avtab: truncated entry\n");
		return rc;
	}

	items = 0;
	key.source_type = le16_to_cpu(buf16[items++]);
	key.target_type = le16_to_cpu(buf16[items++]);
	key.target_class = le16_to_cpu(buf16[items++]);
	key.specified = le16_to_cpu(buf16[items++]);

	if (!policydb_type_isvalid(pol, key.source_type) ||
	    !policydb_type_isvalid(pol, key.target_type) ||
	    !policydb_class_isvalid(pol, key.target_class)) {
		pr_err("SELinux: avtab: invalid type or class\n");
		return -EINVAL;
	}

	set = hweight16(key.specified & (AVTAB_XPERMS | AVTAB_TYPE | AVTAB_AV));
	if (!set || set > 1) {
		pr_err("SELinux:  avtab:  more than one specifier\n");
		return -EINVAL;
	}

	if ((vers < POLICYDB_VERSION_XPERMS_IOCTL) &&
	    (key.specified & AVTAB_XPERMS)) {
		pr_err("SELinux:  avtab:  policy version %u does not "
		       "support extended permissions rules and one "
		       "was specified\n",
		       vers);
		return -EINVAL;
	} else if (key.specified & AVTAB_XPERMS) {
		memset(&xperms, 0, sizeof(struct avtab_extended_perms));
		rc = next_entry(&xperms.specified, fp, sizeof(u8));
		if (rc) {
			pr_err("SELinux: avtab: truncated entry\n");
			return rc;
		}
		rc = next_entry(&xperms.driver, fp, sizeof(u8));
		if (rc) {
			pr_err("SELinux: avtab: truncated entry\n");
			return rc;
		}
		rc = next_entry(buf32, fp,
				sizeof(u32) * ARRAY_SIZE(xperms.perms.p));
		if (rc) {
			pr_err("SELinux: avtab: truncated entry\n");
			return rc;
		}
		for (i = 0; i < ARRAY_SIZE(xperms.perms.p); i++)
			xperms.perms.p[i] = le32_to_cpu(buf32[i]);
		datum.u.xperms = &xperms;
	} else {
		rc = next_entry(buf32, fp, sizeof(u32));
		if (rc) {
			pr_err("SELinux: avtab: truncated entry\n");
			return rc;
		}
		datum.u.data = le32_to_cpu(*buf32);
	}
	if ((key.specified & AVTAB_TYPE) &&
	    !policydb_type_isvalid(pol, datum.u.data)) {
		pr_err("SELinux: avtab: invalid type\n");
		return -EINVAL;
	}
	return insertf(a, &key, &datum, p);
}

static int avtab_insertf(struct avtab *a, const struct avtab_key *k,
			 const struct avtab_datum *d, void *p)
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
		pr_err("SELinux: avtab: truncated table\n");
		goto bad;
	}
	nel = le32_to_cpu(buf[0]);
	if (!nel) {
		pr_err("SELinux: avtab: table is empty\n");
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
				pr_err("SELinux: avtab: out of memory\n");
			else if (rc == -EEXIST)
				pr_err("SELinux: avtab: duplicate entry\n");

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

int avtab_write_item(struct policydb *p, const struct avtab_node *cur, void *fp)
{
	__le16 buf16[4];
	__le32 buf32[ARRAY_SIZE(cur->datum.u.xperms->perms.p)];
	int rc;
	unsigned int i;

	buf16[0] = cpu_to_le16(cur->key.source_type);
	buf16[1] = cpu_to_le16(cur->key.target_type);
	buf16[2] = cpu_to_le16(cur->key.target_class);
	buf16[3] = cpu_to_le16(cur->key.specified);
	rc = put_entry(buf16, sizeof(u16), 4, fp);
	if (rc)
		return rc;

	if (cur->key.specified & AVTAB_XPERMS) {
		rc = put_entry(&cur->datum.u.xperms->specified, sizeof(u8), 1,
			       fp);
		if (rc)
			return rc;
		rc = put_entry(&cur->datum.u.xperms->driver, sizeof(u8), 1, fp);
		if (rc)
			return rc;
		for (i = 0; i < ARRAY_SIZE(cur->datum.u.xperms->perms.p); i++)
			buf32[i] = cpu_to_le32(cur->datum.u.xperms->perms.p[i]);
		rc = put_entry(buf32, sizeof(u32),
			       ARRAY_SIZE(cur->datum.u.xperms->perms.p), fp);
	} else {
		buf32[0] = cpu_to_le32(cur->datum.u.data);
		rc = put_entry(buf32, sizeof(u32), 1, fp);
	}
	if (rc)
		return rc;
	return 0;
}

int avtab_write(struct policydb *p, struct avtab *a, void *fp)
{
	u32 i;
	int rc = 0;
	struct avtab_node *cur;
	__le32 buf[1];

	buf[0] = cpu_to_le32(a->nel);
	rc = put_entry(buf, sizeof(u32), 1, fp);
	if (rc)
		return rc;

	for (i = 0; i < a->nslot; i++) {
		for (cur = a->htable[i]; cur; cur = cur->next) {
			rc = avtab_write_item(p, cur, fp);
			if (rc)
				return rc;
		}
	}

	return rc;
}

void __init avtab_cache_init(void)
{
	avtab_node_cachep = KMEM_CACHE(avtab_node, SLAB_PANIC);
	avtab_xperms_cachep = KMEM_CACHE(avtab_extended_perms, SLAB_PANIC);
}
