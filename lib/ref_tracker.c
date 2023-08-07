// SPDX-License-Identifier: GPL-2.0-or-later

#define pr_fmt(fmt) "ref_tracker: " fmt

#include <linux/export.h>
#include <linux/list_sort.h>
#include <linux/ref_tracker.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/stackdepot.h>

#define REF_TRACKER_STACK_ENTRIES 16
#define STACK_BUF_SIZE 1024

struct ref_tracker {
	struct list_head	head;   /* anchor into dir->list or dir->quarantine */
	bool			dead;
	depot_stack_handle_t	alloc_stack_handle;
	depot_stack_handle_t	free_stack_handle;
};

struct ref_tracker_dir_stats {
	int total;
	int count;
	struct {
		depot_stack_handle_t stack_handle;
		unsigned int count;
	} stacks[];
};

static struct ref_tracker_dir_stats *
ref_tracker_get_stats(struct ref_tracker_dir *dir, unsigned int limit)
{
	struct ref_tracker_dir_stats *stats;
	struct ref_tracker *tracker;

	stats = kmalloc(struct_size(stats, stacks, limit),
			GFP_NOWAIT | __GFP_NOWARN);
	if (!stats)
		return ERR_PTR(-ENOMEM);
	stats->total = 0;
	stats->count = 0;

	list_for_each_entry(tracker, &dir->list, head) {
		depot_stack_handle_t stack = tracker->alloc_stack_handle;
		int i;

		++stats->total;
		for (i = 0; i < stats->count; ++i)
			if (stats->stacks[i].stack_handle == stack)
				break;
		if (i >= limit)
			continue;
		if (i >= stats->count) {
			stats->stacks[i].stack_handle = stack;
			stats->stacks[i].count = 0;
			++stats->count;
		}
		++stats->stacks[i].count;
	}

	return stats;
}

struct ostream {
	char *buf;
	int size, used;
};

#define pr_ostream(stream, fmt, args...) \
({ \
	struct ostream *_s = (stream); \
\
	if (!_s->buf) { \
		pr_err(fmt, ##args); \
	} else { \
		int ret, len = _s->size - _s->used; \
		ret = snprintf(_s->buf + _s->used, len, pr_fmt(fmt), ##args); \
		_s->used += min(ret, len); \
	} \
})

static void
__ref_tracker_dir_pr_ostream(struct ref_tracker_dir *dir,
			     unsigned int display_limit, struct ostream *s)
{
	struct ref_tracker_dir_stats *stats;
	unsigned int i = 0, skipped;
	depot_stack_handle_t stack;
	char *sbuf;

	lockdep_assert_held(&dir->lock);

	if (list_empty(&dir->list))
		return;

	stats = ref_tracker_get_stats(dir, display_limit);
	if (IS_ERR(stats)) {
		pr_ostream(s, "%s@%pK: couldn't get stats, error %pe\n",
			   dir->name, dir, stats);
		return;
	}

	sbuf = kmalloc(STACK_BUF_SIZE, GFP_NOWAIT | __GFP_NOWARN);

	for (i = 0, skipped = stats->total; i < stats->count; ++i) {
		stack = stats->stacks[i].stack_handle;
		if (sbuf && !stack_depot_snprint(stack, sbuf, STACK_BUF_SIZE, 4))
			sbuf[0] = 0;
		pr_ostream(s, "%s@%pK has %d/%d users at\n%s\n", dir->name, dir,
			   stats->stacks[i].count, stats->total, sbuf);
		skipped -= stats->stacks[i].count;
	}

	if (skipped)
		pr_ostream(s, "%s@%pK skipped reports about %d/%d users.\n",
			   dir->name, dir, skipped, stats->total);

	kfree(sbuf);

	kfree(stats);
}

void ref_tracker_dir_print_locked(struct ref_tracker_dir *dir,
				  unsigned int display_limit)
{
	struct ostream os = {};

	__ref_tracker_dir_pr_ostream(dir, display_limit, &os);
}
EXPORT_SYMBOL(ref_tracker_dir_print_locked);

void ref_tracker_dir_print(struct ref_tracker_dir *dir,
			   unsigned int display_limit)
{
	unsigned long flags;

	spin_lock_irqsave(&dir->lock, flags);
	ref_tracker_dir_print_locked(dir, display_limit);
	spin_unlock_irqrestore(&dir->lock, flags);
}
EXPORT_SYMBOL(ref_tracker_dir_print);

int ref_tracker_dir_snprint(struct ref_tracker_dir *dir, char *buf, size_t size)
{
	struct ostream os = { .buf = buf, .size = size };
	unsigned long flags;

	spin_lock_irqsave(&dir->lock, flags);
	__ref_tracker_dir_pr_ostream(dir, 16, &os);
	spin_unlock_irqrestore(&dir->lock, flags);

	return os.used;
}
EXPORT_SYMBOL(ref_tracker_dir_snprint);

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
	if (!list_empty(&dir->list)) {
		ref_tracker_dir_print_locked(dir, 16);
		leak = true;
		list_for_each_entry_safe(tracker, n, &dir->list, head) {
			list_del(&tracker->head);
			kfree(tracker);
		}
	}
	spin_unlock_irqrestore(&dir->lock, flags);
	WARN_ON_ONCE(leak);
	WARN_ON_ONCE(refcount_read(&dir->untracked) != 1);
	WARN_ON_ONCE(refcount_read(&dir->no_tracker) != 1);
}
EXPORT_SYMBOL(ref_tracker_dir_exit);

int ref_tracker_alloc(struct ref_tracker_dir *dir,
		      struct ref_tracker **trackerp,
		      gfp_t gfp)
{
	unsigned long entries[REF_TRACKER_STACK_ENTRIES];
	struct ref_tracker *tracker;
	unsigned int nr_entries;
	gfp_t gfp_mask = gfp | __GFP_NOWARN;
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
	stack_handle = stack_depot_save(entries, nr_entries,
					GFP_NOWAIT | __GFP_NOWARN);

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
