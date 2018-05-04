/*
 * AppArmor security module
 *
 * This file contains AppArmor security identifier (secid) manipulation fns
 *
 * Copyright 2009-2017 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 *
 * AppArmor allocates a unique secid for every label used. If a label
 * is replaced it receives the secid of the label it is replacing.
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "include/cred.h"
#include "include/lib.h"
#include "include/secid.h"
#include "include/label.h"
#include "include/policy_ns.h"

/*
 * secids - do not pin labels with a refcount. They rely on the label
 * properly updating/freeing them
 *
 * A singly linked free list is used to track secids that have been
 * freed and reuse them before allocating new ones
 */

#define FREE_LIST_HEAD 1

static RADIX_TREE(aa_secids_map, GFP_ATOMIC);
static DEFINE_SPINLOCK(secid_lock);
static u32 alloced_secid = FREE_LIST_HEAD;
static u32 free_list = FREE_LIST_HEAD;
static unsigned long free_count;

/*
 * TODO: allow policy to reserve a secid range?
 * TODO: add secid pinning
 * TODO: use secid_update in label replace
 */

#define SECID_MAX U32_MAX

/* TODO: mark free list as exceptional */
static void *to_ptr(u32 secid)
{
	return (void *)
		((((unsigned long) secid) << RADIX_TREE_EXCEPTIONAL_SHIFT));
}

static u32 to_secid(void *ptr)
{
	return (u32) (((unsigned long) ptr) >> RADIX_TREE_EXCEPTIONAL_SHIFT);
}


/* TODO: tag free_list entries to mark them as different */
static u32 __pop(struct aa_label *label)
{
	u32 secid = free_list;
	void __rcu **slot;
	void *entry;

	if (free_list == FREE_LIST_HEAD)
		return AA_SECID_INVALID;

	slot = radix_tree_lookup_slot(&aa_secids_map, secid);
	AA_BUG(!slot);
	entry = radix_tree_deref_slot_protected(slot, &secid_lock);
	free_list = to_secid(entry);
	radix_tree_replace_slot(&aa_secids_map, slot, label);
	free_count--;

	return secid;
}

static void __push(u32 secid)
{
	void __rcu **slot;

	slot = radix_tree_lookup_slot(&aa_secids_map, secid);
	AA_BUG(!slot);
	radix_tree_replace_slot(&aa_secids_map, slot, to_ptr(free_list));
	free_list = secid;
	free_count++;
}

static struct aa_label * __secid_update(u32 secid, struct aa_label *label)
{
	struct aa_label *old;
	void __rcu **slot;

	slot = radix_tree_lookup_slot(&aa_secids_map, secid);
	AA_BUG(!slot);
	old = radix_tree_deref_slot_protected(slot, &secid_lock);
	radix_tree_replace_slot(&aa_secids_map, slot, label);

	return old;
}

/**
 * aa_secid_update - update a secid mapping to a new label
 * @secid: secid to update
 * @label: label the secid will now map to
 */
void aa_secid_update(u32 secid, struct aa_label *label)
{
	struct aa_label *old;
	unsigned long flags;

	spin_lock_irqsave(&secid_lock, flags);
	old = __secid_update(secid, label);
	spin_unlock_irqrestore(&secid_lock, flags);
}

/**
 *
 * see label for inverse aa_label_to_secid
 */
struct aa_label *aa_secid_to_label(u32 secid)
{
	struct aa_label *label;

	rcu_read_lock();
	label = radix_tree_lookup(&aa_secids_map, secid);
	rcu_read_unlock();

	return label;
}

int apparmor_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	/* TODO: cache secctx and ref count so we don't have to recreate */
	struct aa_label *label = aa_secid_to_label(secid);
	int len;

	AA_BUG(!secdata);
	AA_BUG(!seclen);

	if (!label)
		return -EINVAL;

	if (secdata)
		len = aa_label_asxprint(secdata, root_ns, label,
					FLAG_SHOW_MODE | FLAG_VIEW_SUBNS |
					FLAG_HIDDEN_UNCONFINED | FLAG_ABS_ROOT,
					GFP_ATOMIC);
	else
		len = aa_label_snxprint(NULL, 0, root_ns, label,
					FLAG_SHOW_MODE | FLAG_VIEW_SUBNS |
					FLAG_HIDDEN_UNCONFINED | FLAG_ABS_ROOT);
	if (len < 0)
		return -ENOMEM;

	*seclen = len;

	return 0;
}


int apparmor_secctx_to_secid(const char *secdata, u32 seclen, u32 *secid)
{
	struct aa_label *label;

	label = aa_label_strn_parse(&root_ns->unconfined->label, secdata,
				    seclen, GFP_KERNEL, false, false);
	if (IS_ERR(label))
		return PTR_ERR(label);
	*secid = label->secid;

	return 0;
}

void apparmor_release_secctx(char *secdata, u32 seclen)
{
	kfree(secdata);
}


/**
 * aa_alloc_secid - allocate a new secid for a profile
 */
u32 aa_alloc_secid(struct aa_label *label, gfp_t gfp)
{
	unsigned long flags;
	u32 secid;

	/* racey, but at worst causes new allocation instead of reuse */
	if (free_list == FREE_LIST_HEAD) {
		bool preload = 0;
		int res;

retry:
		if (gfpflags_allow_blocking(gfp) && !radix_tree_preload(gfp))
			preload = 1;
		spin_lock_irqsave(&secid_lock, flags);
		if (alloced_secid != SECID_MAX) {
			secid = ++alloced_secid;
			res = radix_tree_insert(&aa_secids_map, secid, label);
			AA_BUG(res == -EEXIST);
		} else {
			secid = AA_SECID_INVALID;
		}
		spin_unlock_irqrestore(&secid_lock, flags);
		if (preload)
			radix_tree_preload_end();
	} else {
		spin_lock_irqsave(&secid_lock, flags);
		/* remove entry from free list */
		secid = __pop(label);
		if (secid == AA_SECID_INVALID) {
			spin_unlock_irqrestore(&secid_lock, flags);
			goto retry;
		}
		spin_unlock_irqrestore(&secid_lock, flags);
	}

	return secid;
}

/**
 * aa_free_secid - free a secid
 * @secid: secid to free
 */
void aa_free_secid(u32 secid)
{
	unsigned long flags;

	spin_lock_irqsave(&secid_lock, flags);
	__push(secid);
	spin_unlock_irqrestore(&secid_lock, flags);
}
