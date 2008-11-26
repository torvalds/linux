/*
 * Implementation of the SID table type.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
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

	s->htable = kmalloc(sizeof(*(s->htable)) * SIDTAB_SIZE, GFP_ATOMIC);
	if (!s->htable)
		return -ENOMEM;
	for (i = 0; i < SIDTAB_SIZE; i++)
		s->htable[i] = NULL;
	s->nel = 0;
	s->next_sid = 1;
	s->shutdown = 0;
	spin_lock_init(&s->lock);
	return 0;
}

int sidtab_insert(struct sidtab *s, u32 sid, struct context *context)
{
	int hvalue, rc = 0;
	struct sidtab_node *prev, *cur, *newnode;

	if (!s) {
		rc = -ENOMEM;
		goto out;
	}

	hvalue = SIDTAB_HASH(sid);
	prev = NULL;
	cur = s->htable[hvalue];
	while (cur && sid > cur->sid) {
		prev = cur;
		cur = cur->next;
	}

	if (cur && sid == cur->sid) {
		rc = -EEXIST;
		goto out;
	}

	newnode = kmalloc(sizeof(*newnode), GFP_ATOMIC);
	if (newnode == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	newnode->sid = sid;
	if (context_cpy(&newnode->context, context)) {
		kfree(newnode);
		rc = -ENOMEM;
		goto out;
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
out:
	return rc;
}

static struct context *sidtab_search_core(struct sidtab *s, u32 sid, int force)
{
	int hvalue;
	struct sidtab_node *cur;

	if (!s)
		return NULL;

	hvalue = SIDTAB_HASH(sid);
	cur = s->htable[hvalue];
	while (cur && sid > cur->sid)
		cur = cur->next;

	if (force && cur && sid == cur->sid && cur->context.len)
		return &cur->context;

	if (cur == NULL || sid != cur->sid || cur->context.len) {
		/* Remap invalid SIDs to the unlabeled SID. */
		sid = SECINITSID_UNLABELED;
		hvalue = SIDTAB_HASH(sid);
		cur = s->htable[hvalue];
		while (cur && sid > cur->sid)
			cur = cur->next;
		if (!cur || sid != cur->sid)
			return NULL;
	}

	return &cur->context;
}

struct context *sidtab_search(struct sidtab *s, u32 sid)
{
	return sidtab_search_core(s, sid, 0);
}

struct context *sidtab_search_force(struct sidtab *s, u32 sid)
{
	return sidtab_search_core(s, sid, 1);
}

int sidtab_map(struct sidtab *s,
	       int (*apply) (u32 sid,
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

static inline u32 sidtab_search_context(struct sidtab *s,
						  struct context *context)
{
	int i;
	struct sidtab_node *cur;

	for (i = 0; i < SIDTAB_SIZE; i++) {
		cur = s->htable[i];
		while (cur) {
			if (context_cmp(&cur->context, context))
				return cur->sid;
			cur = cur->next;
		}
	}
	return 0;
}

int sidtab_context_to_sid(struct sidtab *s,
			  struct context *context,
			  u32 *out_sid)
{
	u32 sid;
	int ret = 0;
	unsigned long flags;

	*out_sid = SECSID_NULL;

	sid = sidtab_search_context(s, context);
	if (!sid) {
		spin_lock_irqsave(&s->lock, flags);
		/* Rescan now that we hold the lock. */
		sid = sidtab_search_context(s, context);
		if (sid)
			goto unlock_out;
		/* No SID exists for the context.  Allocate a new one. */
		if (s->next_sid == UINT_MAX || s->shutdown) {
			ret = -ENOMEM;
			goto unlock_out;
		}
		sid = s->next_sid++;
		if (context->len)
			printk(KERN_INFO
		       "SELinux:  Context %s is not valid (left unmapped).\n",
			       context->str);
		ret = sidtab_insert(s, sid, context);
		if (ret)
			s->next_sid--;
unlock_out:
		spin_unlock_irqrestore(&s->lock, flags);
	}

	if (ret)
		return ret;

	*out_sid = sid;
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

	printk(KERN_DEBUG "%s:  %d entries and %d/%d buckets used, longest "
	       "chain length %d\n", tag, h->nel, slots_used, SIDTAB_SIZE,
	       max_chain_len);
}

void sidtab_destroy(struct sidtab *s)
{
	int i;
	struct sidtab_node *cur, *temp;

	if (!s)
		return;

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

void sidtab_set(struct sidtab *dst, struct sidtab *src)
{
	unsigned long flags;

	spin_lock_irqsave(&src->lock, flags);
	dst->htable = src->htable;
	dst->nel = src->nel;
	dst->next_sid = src->next_sid;
	dst->shutdown = 0;
	spin_unlock_irqrestore(&src->lock, flags);
}

void sidtab_shutdown(struct sidtab *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	s->shutdown = 1;
	spin_unlock_irqrestore(&s->lock, flags);
}
