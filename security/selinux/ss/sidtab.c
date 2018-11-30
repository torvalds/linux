// SPDX-License-Identifier: GPL-2.0
/*
 * Implementation of the SID table type.
 *
 * Author : Stephen Smalley, <sds@tycho.nsa.gov>
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include "flask.h"
#include "security.h"
#include "sidtab.h"

#define SIDTAB_HASH(sid) \
(sid & SIDTAB_HASH_MASK)

int sidtab_init(struct sidtab *s)
{
	int i;

	s->htable = kmalloc_array(SIDTAB_SIZE, sizeof(*s->htable), GFP_ATOMIC);
	if (!s->htable)
		return -ENOMEM;

	for (i = 0; i < SECINITSID_NUM; i++)
		s->isids[i].set = 0;

	for (i = 0; i < SIDTAB_SIZE; i++)
		s->htable[i] = NULL;

	for (i = 0; i < SIDTAB_CACHE_LEN; i++)
		s->cache[i] = NULL;

	s->nel = 0;
	s->next_sid = 0;
	s->shutdown = 0;
	spin_lock_init(&s->lock);
	return 0;
}

static int sidtab_insert(struct sidtab *s, u32 sid, struct context *context)
{
	int hvalue;
	struct sidtab_node *prev, *cur, *newnode;

	if (!s)
		return -ENOMEM;

	hvalue = SIDTAB_HASH(sid);
	prev = NULL;
	cur = s->htable[hvalue];
	while (cur && sid > cur->sid) {
		prev = cur;
		cur = cur->next;
	}

	if (cur && sid == cur->sid)
		return -EEXIST;

	newnode = kmalloc(sizeof(*newnode), GFP_ATOMIC);
	if (!newnode)
		return -ENOMEM;

	newnode->sid = sid;
	if (context_cpy(&newnode->context, context)) {
		kfree(newnode);
		return -ENOMEM;
	}

	if (prev) {
		newnode->next = prev->next;
		wmb();
		prev->next = newnode;
	} else {
		newnode->next = s->htable[hvalue];
		wmb();
		s->htable[hvalue] = newnode;
	}

	s->nel++;
	if (sid >= s->next_sid)
		s->next_sid = sid + 1;
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

static struct context *sidtab_lookup(struct sidtab *s, u32 sid)
{
	int hvalue;
	struct sidtab_node *cur;

	hvalue = SIDTAB_HASH(sid);
	cur = s->htable[hvalue];
	while (cur && sid > cur->sid)
		cur = cur->next;

	if (!cur || sid != cur->sid)
		return NULL;

	return &cur->context;
}

static struct context *sidtab_lookup_initial(struct sidtab *s, u32 sid)
{
	return s->isids[sid - 1].set ? &s->isids[sid - 1].context : NULL;
}

static struct context *sidtab_search_core(struct sidtab *s, u32 sid, int force)
{
	struct context *context;

	if (!s)
		return NULL;

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

static int sidtab_map(struct sidtab *s,
		      int (*apply)(u32 sid,
				   struct context *context,
				   void *args),
		      void *args)
{
	int i, rc = 0;
	struct sidtab_node *cur;

	if (!s)
		goto out;

	for (i = 0; i < SIDTAB_SIZE; i++) {
		cur = s->htable[i];
		while (cur) {
			rc = apply(cur->sid, &cur->context, args);
			if (rc)
				goto out;
			cur = cur->next;
		}
	}
out:
	return rc;
}

/* Clone the SID into the new SID table. */
static int clone_sid(u32 sid, struct context *context, void *arg)
{
	struct sidtab *s = arg;
	return sidtab_insert(s, sid, context);
}

int sidtab_convert(struct sidtab *s, struct sidtab *news,
		   int (*convert)(u32 sid,
				  struct context *context,
				  void *args),
		   void *args)
{
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&s->lock, flags);
	s->shutdown = 1;
	spin_unlock_irqrestore(&s->lock, flags);

	rc = sidtab_map(s, clone_sid, news);
	if (rc)
		return rc;

	return sidtab_map(news, convert, args);
}

static void sidtab_update_cache(struct sidtab *s, struct sidtab_node *n, int loc)
{
	BUG_ON(loc >= SIDTAB_CACHE_LEN);

	while (loc > 0) {
		s->cache[loc] = s->cache[loc - 1];
		loc--;
	}
	s->cache[0] = n;
}

static inline int sidtab_search_context(struct sidtab *s,
					struct context *context, u32 *sid)
{
	int i;
	struct sidtab_node *cur;

	for (i = 0; i < SIDTAB_SIZE; i++) {
		cur = s->htable[i];
		while (cur) {
			if (context_cmp(&cur->context, context)) {
				sidtab_update_cache(s, cur, SIDTAB_CACHE_LEN - 1);
				*sid = cur->sid;
				return 0;
			}
			cur = cur->next;
		}
	}
	return -ENOENT;
}

static inline int sidtab_search_cache(struct sidtab *s, struct context *context,
				      u32 *sid)
{
	int i;
	struct sidtab_node *node;

	for (i = 0; i < SIDTAB_CACHE_LEN; i++) {
		node = s->cache[i];
		if (unlikely(!node))
			return -ENOENT;
		if (context_cmp(&node->context, context)) {
			sidtab_update_cache(s, node, i);
			*sid = node->sid;
			return 0;
		}
	}
	return -ENOENT;
}

static int sidtab_reverse_lookup(struct sidtab *s, struct context *context,
				 u32 *sid)
{
	int ret;
	unsigned long flags;

	ret = sidtab_search_cache(s, context, sid);
	if (ret)
		ret = sidtab_search_context(s, context, sid);
	if (ret) {
		spin_lock_irqsave(&s->lock, flags);
		/* Rescan now that we hold the lock. */
		ret = sidtab_search_context(s, context, sid);
		if (!ret)
			goto unlock_out;
		/* No SID exists for the context.  Allocate a new one. */
		if (s->next_sid == (UINT_MAX - SECINITSID_NUM - 1) ||
		    s->shutdown) {
			ret = -ENOMEM;
			goto unlock_out;
		}
		*sid = s->next_sid++;
		if (context->len)
			pr_info("SELinux:  Context %s is not valid (left unmapped).\n",
			       context->str);
		ret = sidtab_insert(s, *sid, context);
		if (ret)
			s->next_sid--;
unlock_out:
		spin_unlock_irqrestore(&s->lock, flags);
	}

	return ret;
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

void sidtab_hash_eval(struct sidtab *h, char *tag)
{
	int i, chain_len, slots_used, max_chain_len;
	struct sidtab_node *cur;

	slots_used = 0;
	max_chain_len = 0;
	for (i = 0; i < SIDTAB_SIZE; i++) {
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

	pr_debug("%s:  %d entries and %d/%d buckets used, longest "
	       "chain length %d\n", tag, h->nel, slots_used, SIDTAB_SIZE,
	       max_chain_len);
}

void sidtab_destroy(struct sidtab *s)
{
	int i;
	struct sidtab_node *cur, *temp;

	if (!s)
		return;

	for (i = 0; i < SECINITSID_NUM; i++)
		if (s->isids[i].set)
			context_destroy(&s->isids[i].context);

	for (i = 0; i < SIDTAB_SIZE; i++) {
		cur = s->htable[i];
		while (cur) {
			temp = cur;
			cur = cur->next;
			context_destroy(&temp->context);
			kfree(temp);
		}
		s->htable[i] = NULL;
	}
	kfree(s->htable);
	s->htable = NULL;
	s->nel = 0;
	s->next_sid = 1;
}
