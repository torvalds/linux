// SPDX-License-Identifier: GPL-2.0
/*
 * Implementation of the SID table type.
 *
 * Original author: Stephen Smalley, <sds@tycho.nsa.gov>
 * Author: Ondrej Mosnacek, <omosnacek@gmail.com>
 *
 * Copyright (C) 2018 Red Hat, Inc.
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include "flask.h"
#include "security.h"
#include "sidtab.h"

int sidtab_init(struct sidtab *s)
{
	u32 i;

	memset(s->roots, 0, sizeof(s->roots));

	for (i = 0; i < SIDTAB_RCACHE_SIZE; i++)
		atomic_set(&s->rcache[i], -1);

	for (i = 0; i < SECINITSID_NUM; i++)
		s->isids[i].set = 0;

	atomic_set(&s->count, 0);

	s->convert = NULL;

	spin_lock_init(&s->lock);
	return 0;
}

int sidtab_set_initial(struct sidtab *s, u32 sid, struct context *context)
{
	struct sidtab_isid_entry *entry;
	int rc;

	if (sid == 0 || sid > SECINITSID_NUM)
		return -EINVAL;

	entry = &s->isids[sid - 1];

	rc = context_cpy(&entry->context, context);
	if (rc)
		return rc;

	entry->set = 1;
	return 0;
}

static u32 sidtab_level_from_count(u32 count)
{
	u32 capacity = SIDTAB_LEAF_ENTRIES;
	u32 level = 0;

	while (count > capacity) {
		capacity <<= SIDTAB_INNER_SHIFT;
		++level;
	}
	return level;
}

static int sidtab_alloc_roots(struct sidtab *s, u32 level)
{
	u32 l;

	if (!s->roots[0].ptr_leaf) {
		s->roots[0].ptr_leaf = kzalloc(SIDTAB_NODE_ALLOC_SIZE,
					       GFP_ATOMIC);
		if (!s->roots[0].ptr_leaf)
			return -ENOMEM;
	}
	for (l = 1; l <= level; ++l)
		if (!s->roots[l].ptr_inner) {
			s->roots[l].ptr_inner = kzalloc(SIDTAB_NODE_ALLOC_SIZE,
							GFP_ATOMIC);
			if (!s->roots[l].ptr_inner)
				return -ENOMEM;
			s->roots[l].ptr_inner->entries[0] = s->roots[l - 1];
		}
	return 0;
}

static struct context *sidtab_do_lookup(struct sidtab *s, u32 index, int alloc)
{
	union sidtab_entry_inner *entry;
	u32 level, capacity_shift, leaf_index = index / SIDTAB_LEAF_ENTRIES;

	/* find the level of the subtree we need */
	level = sidtab_level_from_count(index + 1);
	capacity_shift = level * SIDTAB_INNER_SHIFT;

	/* allocate roots if needed */
	if (alloc && sidtab_alloc_roots(s, level) != 0)
		return NULL;

	/* lookup inside the subtree */
	entry = &s->roots[level];
	while (level != 0) {
		capacity_shift -= SIDTAB_INNER_SHIFT;
		--level;

		entry = &entry->ptr_inner->entries[leaf_index >> capacity_shift];
		leaf_index &= ((u32)1 << capacity_shift) - 1;

		if (!entry->ptr_inner) {
			if (alloc)
				entry->ptr_inner = kzalloc(SIDTAB_NODE_ALLOC_SIZE,
							   GFP_ATOMIC);
			if (!entry->ptr_inner)
				return NULL;
		}
	}
	if (!entry->ptr_leaf) {
		if (alloc)
			entry->ptr_leaf = kzalloc(SIDTAB_NODE_ALLOC_SIZE,
						  GFP_ATOMIC);
		if (!entry->ptr_leaf)
			return NULL;
	}
	return &entry->ptr_leaf->entries[index % SIDTAB_LEAF_ENTRIES].context;
}

static struct context *sidtab_lookup(struct sidtab *s, u32 index)
{
	u32 count = (u32)atomic_read(&s->count);

	if (index >= count)
		return NULL;

	/* read entries after reading count */
	smp_rmb();

	return sidtab_do_lookup(s, index, 0);
}

static struct context *sidtab_lookup_initial(struct sidtab *s, u32 sid)
{
	return s->isids[sid - 1].set ? &s->isids[sid - 1].context : NULL;
}

static struct context *sidtab_search_core(struct sidtab *s, u32 sid, int force)
{
	struct context *context;

	if (sid != 0) {
		if (sid > SECINITSID_NUM)
			context = sidtab_lookup(s, sid - (SECINITSID_NUM + 1));
		else
			context = sidtab_lookup_initial(s, sid);
		if (context && (!context->len || force))
			return context;
	}

	return sidtab_lookup_initial(s, SECINITSID_UNLABELED);
}

struct context *sidtab_search(struct sidtab *s, u32 sid)
{
	return sidtab_search_core(s, sid, 0);
}

struct context *sidtab_search_force(struct sidtab *s, u32 sid)
{
	return sidtab_search_core(s, sid, 1);
}

static int sidtab_find_context(union sidtab_entry_inner entry,
			       u32 *pos, u32 count, u32 level,
			       struct context *context, u32 *index)
{
	int rc;
	u32 i;

	if (level != 0) {
		struct sidtab_node_inner *node = entry.ptr_inner;

		i = 0;
		while (i < SIDTAB_INNER_ENTRIES && *pos < count) {
			rc = sidtab_find_context(node->entries[i],
						 pos, count, level - 1,
						 context, index);
			if (rc == 0)
				return 0;
			i++;
		}
	} else {
		struct sidtab_node_leaf *node = entry.ptr_leaf;

		i = 0;
		while (i < SIDTAB_LEAF_ENTRIES && *pos < count) {
			if (context_cmp(&node->entries[i].context, context)) {
				*index = *pos;
				return 0;
			}
			(*pos)++;
			i++;
		}
	}
	return -ENOENT;
}

static void sidtab_rcache_update(struct sidtab *s, u32 index, u32 pos)
{
	while (pos > 0) {
		atomic_set(&s->rcache[pos], atomic_read(&s->rcache[pos - 1]));
		--pos;
	}
	atomic_set(&s->rcache[0], (int)index);
}

static void sidtab_rcache_push(struct sidtab *s, u32 index)
{
	sidtab_rcache_update(s, index, SIDTAB_RCACHE_SIZE - 1);
}

static int sidtab_rcache_search(struct sidtab *s, struct context *context,
				u32 *index)
{
	u32 i;

	for (i = 0; i < SIDTAB_RCACHE_SIZE; i++) {
		int v = atomic_read(&s->rcache[i]);

		if (v < 0)
			continue;

		if (context_cmp(sidtab_do_lookup(s, (u32)v, 0), context)) {
			sidtab_rcache_update(s, (u32)v, i);
			*index = (u32)v;
			return 0;
		}
	}
	return -ENOENT;
}

static int sidtab_reverse_lookup(struct sidtab *s, struct context *context,
				 u32 *index)
{
	unsigned long flags;
	u32 count = (u32)atomic_read(&s->count);
	u32 count_locked, level, pos;
	struct sidtab_convert_params *convert;
	struct context *dst, *dst_convert;
	int rc;

	rc = sidtab_rcache_search(s, context, index);
	if (rc == 0)
		return 0;

	level = sidtab_level_from_count(count);

	/* read entries after reading count */
	smp_rmb();

	pos = 0;
	rc = sidtab_find_context(s->roots[level], &pos, count, level,
				 context, index);
	if (rc == 0) {
		sidtab_rcache_push(s, *index);
		return 0;
	}

	/* lock-free search failed: lock, re-search, and insert if not found */
	spin_lock_irqsave(&s->lock, flags);

	convert = s->convert;
	count_locked = (u32)atomic_read(&s->count);
	level = sidtab_level_from_count(count_locked);

	/* if count has changed before we acquired the lock, then catch up */
	while (count < count_locked) {
		if (context_cmp(sidtab_do_lookup(s, count, 0), context)) {
			sidtab_rcache_push(s, count);
			*index = count;
			rc = 0;
			goto out_unlock;
		}
		++count;
	}

	/* bail out if we already reached max entries */
	rc = -EOVERFLOW;
	if (count >= SIDTAB_MAX)
		goto out_unlock;

	/* insert context into new entry */
	rc = -ENOMEM;
	dst = sidtab_do_lookup(s, count, 1);
	if (!dst)
		goto out_unlock;

	rc = context_cpy(dst, context);
	if (rc)
		goto out_unlock;

	/*
	 * if we are building a new sidtab, we need to convert the context
	 * and insert it there as well
	 */
	if (convert) {
		rc = -ENOMEM;
		dst_convert = sidtab_do_lookup(convert->target, count, 1);
		if (!dst_convert) {
			context_destroy(dst);
			goto out_unlock;
		}

		rc = convert->func(context, dst_convert, convert->args);
		if (rc) {
			context_destroy(dst);
			goto out_unlock;
		}

		/* at this point we know the insert won't fail */
		atomic_set(&convert->target->count, count + 1);
	}

	if (context->len)
		pr_info("SELinux:  Context %s is not valid (left unmapped).\n",
			context->str);

	sidtab_rcache_push(s, count);
	*index = count;

	/* write entries before writing new count */
	smp_wmb();

	atomic_set(&s->count, count + 1);

	rc = 0;
out_unlock:
	spin_unlock_irqrestore(&s->lock, flags);
	return rc;
}

int sidtab_context_to_sid(struct sidtab *s, struct context *context, u32 *sid)
{
	int rc;
	u32 i;

	for (i = 0; i < SECINITSID_NUM; i++) {
		struct sidtab_isid_entry *entry = &s->isids[i];

		if (entry->set && context_cmp(context, &entry->context)) {
			*sid = i + 1;
			return 0;
		}
	}

	rc = sidtab_reverse_lookup(s, context, sid);
	if (rc)
		return rc;
	*sid += SECINITSID_NUM + 1;
	return 0;
}

static int sidtab_convert_tree(union sidtab_entry_inner *edst,
			       union sidtab_entry_inner *esrc,
			       u32 *pos, u32 count, u32 level,
			       struct sidtab_convert_params *convert)
{
	int rc;
	u32 i;

	if (level != 0) {
		if (!edst->ptr_inner) {
			edst->ptr_inner = kzalloc(SIDTAB_NODE_ALLOC_SIZE,
						  GFP_KERNEL);
			if (!edst->ptr_inner)
				return -ENOMEM;
		}
		i = 0;
		while (i < SIDTAB_INNER_ENTRIES && *pos < count) {
			rc = sidtab_convert_tree(&edst->ptr_inner->entries[i],
						 &esrc->ptr_inner->entries[i],
						 pos, count, level - 1,
						 convert);
			if (rc)
				return rc;
			i++;
		}
	} else {
		if (!edst->ptr_leaf) {
			edst->ptr_leaf = kzalloc(SIDTAB_NODE_ALLOC_SIZE,
						 GFP_KERNEL);
			if (!edst->ptr_leaf)
				return -ENOMEM;
		}
		i = 0;
		while (i < SIDTAB_LEAF_ENTRIES && *pos < count) {
			rc = convert->func(&esrc->ptr_leaf->entries[i].context,
					   &edst->ptr_leaf->entries[i].context,
					   convert->args);
			if (rc)
				return rc;
			(*pos)++;
			i++;
		}
		cond_resched();
	}
	return 0;
}

int sidtab_convert(struct sidtab *s, struct sidtab_convert_params *params)
{
	unsigned long flags;
	u32 count, level, pos;
	int rc;

	spin_lock_irqsave(&s->lock, flags);

	/* concurrent policy loads are not allowed */
	if (s->convert) {
		spin_unlock_irqrestore(&s->lock, flags);
		return -EBUSY;
	}

	count = (u32)atomic_read(&s->count);
	level = sidtab_level_from_count(count);

	/* allocate last leaf in the new sidtab (to avoid race with
	 * live convert)
	 */
	rc = sidtab_do_lookup(params->target, count - 1, 1) ? 0 : -ENOMEM;
	if (rc) {
		spin_unlock_irqrestore(&s->lock, flags);
		return rc;
	}

	/* set count in case no new entries are added during conversion */
	atomic_set(&params->target->count, count);

	/* enable live convert of new entries */
	s->convert = params;

	/* we can safely do the rest of the conversion outside the lock */
	spin_unlock_irqrestore(&s->lock, flags);

	pr_info("SELinux:  Converting %u SID table entries...\n", count);

	/* convert all entries not covered by live convert */
	pos = 0;
	rc = sidtab_convert_tree(&params->target->roots[level],
				 &s->roots[level], &pos, count, level, params);
	if (rc) {
		/* we need to keep the old table - disable live convert */
		spin_lock_irqsave(&s->lock, flags);
		s->convert = NULL;
		spin_unlock_irqrestore(&s->lock, flags);
	}
	return rc;
}

static void sidtab_destroy_tree(union sidtab_entry_inner entry, u32 level)
{
	u32 i;

	if (level != 0) {
		struct sidtab_node_inner *node = entry.ptr_inner;

		if (!node)
			return;

		for (i = 0; i < SIDTAB_INNER_ENTRIES; i++)
			sidtab_destroy_tree(node->entries[i], level - 1);
		kfree(node);
	} else {
		struct sidtab_node_leaf *node = entry.ptr_leaf;

		if (!node)
			return;

		for (i = 0; i < SIDTAB_LEAF_ENTRIES; i++)
			context_destroy(&node->entries[i].context);
		kfree(node);
	}
}

void sidtab_destroy(struct sidtab *s)
{
	u32 i, level;

	for (i = 0; i < SECINITSID_NUM; i++)
		if (s->isids[i].set)
			context_destroy(&s->isids[i].context);

	level = SIDTAB_MAX_LEVEL;
	while (level && !s->roots[level].ptr_inner)
		--level;

	sidtab_destroy_tree(s->roots[level], level);
}
