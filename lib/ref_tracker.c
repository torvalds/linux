// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/export.h>
#include <linux/ref_tracker.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/stackdepot.h>

#define REF_TRACKER_STACK_ENTRIES 16

struct ref_tracker {
	struct list_head	head;   /* anchor into dir->list or dir->quarantine */
	bool			dead;
	depot_stack_handle_t	alloc_stack_handle;
	depot_stack_handle_t	free_stack_handle;
};

void ref_tracker_dir_exit(struct ref_tracker_dir *dir)
{
	struct ref_tracker *tracker, *n;
	unsigned long flags;
	bool leak = false;

	dir->dead = true;
	spin_lock_irqsave(&dir->lock, flags);
	list_for_each_entry_safe(tracker, n, &dir->quarantine, head) {
		list_del(&tracker->head);
		kfree(tracker);
		dir->quarantine_avail++;
	}
	list_for_each_entry_safe(tracker, n, &dir->list, head) {
		pr_err("leaked reference.\n");
		if (tracker->alloc_stack_handle)
			stack_depot_print(tracker->alloc_stack_handle);
		leak = true;
		list_del(&tracker->head);
		kfree(tracker);
	}
	spin_unlock_irqrestore(&dir->lock, flags);
	WARN_ON_ONCE(leak);
	WARN_ON_ONCE(refcount_read(&dir->untracked) != 1);
	WARN_ON_ONCE(refcount_read(&dir->no_tracker) != 1);
}
EXPORT_SYMBOL(ref_tracker_dir_exit);

void ref_tracker_dir_print(struct ref_tracker_dir *dir,
			   unsigned int display_limit)
{
	struct ref_tracker *tracker;
	unsigned long flags;
	unsigned int i = 0;

	spin_lock_irqsave(&dir->lock, flags);
	list_for_each_entry(tracker, &dir->list, head) {
		if (i < display_limit) {
			pr_err("leaked reference.\n");
			if (tracker->alloc_stack_handle)
				stack_depot_print(tracker->alloc_stack_handle);
			i++;
		} else {
			break;
		}
	}
	spin_unlock_irqrestore(&dir->lock, flags);
}
EXPORT_SYMBOL(ref_tracker_dir_print);

int ref_tracker_alloc(struct ref_tracker_dir *dir,
		      struct ref_tracker **trackerp,
		      gfp_t gfp)
{
	unsigned long entries[REF_TRACKER_STACK_ENTRIES];
	struct ref_tracker *tracker;
	unsigned int nr_entries;
	gfp_t gfp_mask = gfp;
	unsigned long flags;

	WARN_ON_ONCE(dir->dead);

	if (!trackerp) {
		refcount_inc(&dir->no_tracker);
		return 0;
	}
	if (gfp & __GFP_DIRECT_RECLAIM)
		gfp_mask |= __GFP_NOFAIL;
	*trackerp = tracker = kzalloc(sizeof(*tracker), gfp_mask);
	if (unlikely(!tracker)) {
		pr_err_once("memory allocation failure, unreliable refcount tracker.\n");
		refcount_inc(&dir->untracked);
		return -ENOMEM;
	}
	nr_entries = stack_trace_save(entries, ARRAY_SIZE(entries), 1);
	nr_entries = filter_irq_stacks(entries, nr_entries);
	tracker->alloc_stack_handle = stack_depot_save(entries, nr_entries, gfp);

	spin_lock_irqsave(&dir->lock, flags);
	list_add(&tracker->head, &dir->list);
	spin_unlock_irqrestore(&dir->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(ref_tracker_alloc);

int ref_tracker_free(struct ref_tracker_dir *dir,
		     struct ref_tracker **trackerp)
{
	unsigned long entries[REF_TRACKER_STACK_ENTRIES];
	depot_stack_handle_t stack_handle;
	struct ref_tracker *tracker;
	unsigned int nr_entries;
	unsigned long flags;

	WARN_ON_ONCE(dir->dead);

	if (!trackerp) {
		refcount_dec(&dir->no_tracker);
		return 0;
	}
	tracker = *trackerp;
	if (!tracker) {
		refcount_dec(&dir->untracked);
		return -EEXIST;
	}
	nr_entries = stack_trace_save(entries, ARRAY_SIZE(entries), 1);
	nr_entries = filter_irq_stacks(entries, nr_entries);
	stack_handle = stack_depot_save(entries, nr_entries, GFP_ATOMIC);

	spin_lock_irqsave(&dir->lock, flags);
	if (tracker->dead) {
		pr_err("reference already released.\n");
		if (tracker->alloc_stack_handle) {
			pr_err("allocated in:\n");
			stack_depot_print(tracker->alloc_stack_handle);
		}
		if (tracker->free_stack_handle) {
			pr_err("freed in:\n");
			stack_depot_print(tracker->free_stack_handle);
		}
		spin_unlock_irqrestore(&dir->lock, flags);
		WARN_ON_ONCE(1);
		return -EINVAL;
	}
	tracker->dead = true;

	tracker->free_stack_handle = stack_handle;

	list_move_tail(&tracker->head, &dir->quarantine);
	if (!dir->quarantine_avail) {
		tracker = list_first_entry(&dir->quarantine, struct ref_tracker, head);
		list_del(&tracker->head);
	} else {
		dir->quarantine_avail--;
		tracker = NULL;
	}
	spin_unlock_irqrestore(&dir->lock, flags);

	kfree(tracker);
	return 0;
}
EXPORT_SYMBOL_GPL(ref_tracker_free);
