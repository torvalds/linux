// SPDX-License-Identifier: GPL-2.0-or-later

#define pr_fmt(fmt) "ref_tracker: " fmt

#include <linux/export.h>
#include <linux/list_sort.h>
#include <linux/ref_tracker.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/stackdepot.h>
#include <linux/seq_file.h>

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

#ifdef CONFIG_DEBUG_FS
#include <linux/xarray.h>

/*
 * ref_tracker_dir_init() is usually called in allocation-safe contexts, but
 * the same is not true of ref_tracker_dir_exit() which can be called from
 * anywhere an object is freed. Removing debugfs dentries is a blocking
 * operation, so we defer that work to the debugfs_reap_worker.
 *
 * Each dentry is tracked in the appropriate xarray.  When
 * ref_tracker_dir_exit() is called, its entries in the xarrays are marked and
 * the workqueue job is scheduled. The worker then runs and deletes any marked
 * dentries asynchronously.
 */
static struct xarray		debugfs_dentries;
static struct xarray		debugfs_symlinks;
static struct work_struct	debugfs_reap_worker;

#define REF_TRACKER_DIR_DEAD	XA_MARK_0
static inline void ref_tracker_debugfs_mark(struct ref_tracker_dir *dir)
{
	unsigned long flags;

	xa_lock_irqsave(&debugfs_dentries, flags);
	__xa_set_mark(&debugfs_dentries, (unsigned long)dir, REF_TRACKER_DIR_DEAD);
	xa_unlock_irqrestore(&debugfs_dentries, flags);

	xa_lock_irqsave(&debugfs_symlinks, flags);
	__xa_set_mark(&debugfs_symlinks, (unsigned long)dir, REF_TRACKER_DIR_DEAD);
	xa_unlock_irqrestore(&debugfs_symlinks, flags);

	schedule_work(&debugfs_reap_worker);
}
#else
static inline void ref_tracker_debugfs_mark(struct ref_tracker_dir *dir)
{
}
#endif

static struct ref_tracker_dir_stats *
ref_tracker_get_stats(struct ref_tracker_dir *dir, unsigned int limit)
{
	struct ref_tracker_dir_stats *stats;
	struct ref_tracker *tracker;

	stats = kmalloc(struct_size(stats, stacks, limit),
			GFP_NOWAIT);
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
	void __ostream_printf (*func)(struct ostream *stream, char *fmt, ...);
	char *prefix;
	char *buf;
	struct seq_file *seq;
	int size, used;
};

static void __ostream_printf pr_ostream_log(struct ostream *stream, char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);
}

static void __ostream_printf pr_ostream_buf(struct ostream *stream, char *fmt, ...)
{
	int ret, len = stream->size - stream->used;
	va_list args;

	va_start(args, fmt);
	ret = vsnprintf(stream->buf + stream->used, len, fmt, args);
	va_end(args);
	if (ret > 0)
		stream->used += min(ret, len);
}

#define pr_ostream(stream, fmt, args...) \
({ \
	struct ostream *_s = (stream); \
\
	_s->func(_s, fmt, ##args); \
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
		pr_ostream(s, "%s%s@%p: couldn't get stats, error %pe\n",
			   s->prefix, dir->class, dir, stats);
		return;
	}

	sbuf = kmalloc(STACK_BUF_SIZE, GFP_NOWAIT);

	for (i = 0, skipped = stats->total; i < stats->count; ++i) {
		stack = stats->stacks[i].stack_handle;
		if (sbuf && !stack_depot_snprint(stack, sbuf, STACK_BUF_SIZE, 4))
			sbuf[0] = 0;
		pr_ostream(s, "%s%s@%p has %d/%d users at\n%s\n", s->prefix,
			   dir->class, dir, stats->stacks[i].count,
			   stats->total, sbuf);
		skipped -= stats->stacks[i].count;
	}

	if (skipped)
		pr_ostream(s, "%s%s@%p skipped reports about %d/%d users.\n",
			   s->prefix, dir->class, dir, skipped, stats->total);

	kfree(sbuf);

	kfree(stats);
}

void ref_tracker_dir_print_locked(struct ref_tracker_dir *dir,
				  unsigned int display_limit)
{
	struct ostream os = { .func = pr_ostream_log,
			      .prefix = "ref_tracker: " };

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
	struct ostream os = { .func = pr_ostream_buf,
			      .prefix = "ref_tracker: ",
			      .buf = buf,
			      .size = size };
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
	/*
	 * The xarray entries must be marked before the dir->lock is taken to
	 * protect simultaneous debugfs readers.
	 */
	ref_tracker_debugfs_mark(dir);
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
					GFP_NOWAIT);

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

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

static struct dentry *ref_tracker_debug_dir = (struct dentry *)-ENOENT;

static void __ostream_printf pr_ostream_seq(struct ostream *stream, char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	seq_vprintf(stream->seq, fmt, args);
	va_end(args);
}

static int ref_tracker_dir_seq_print(struct ref_tracker_dir *dir, struct seq_file *seq)
{
	struct ostream os = { .func = pr_ostream_seq,
			      .prefix = "",
			      .seq = seq };

	__ref_tracker_dir_pr_ostream(dir, 16, &os);

	return os.used;
}

static int ref_tracker_debugfs_show(struct seq_file *f, void *v)
{
	struct ref_tracker_dir *dir = f->private;
	unsigned long index = (unsigned long)dir;
	unsigned long flags;
	int ret;

	/*
	 * "dir" may not exist at this point if ref_tracker_dir_exit() has
	 * already been called. Take care not to dereference it until its
	 * legitimacy is established.
	 *
	 * The xa_lock is necessary to ensure that "dir" doesn't disappear
	 * before its lock can be taken. If it's in the hash and not marked
	 * dead, then it's safe to take dir->lock which prevents
	 * ref_tracker_dir_exit() from completing. Once the dir->lock is
	 * acquired, the xa_lock can be released. All of this must be IRQ-safe.
	 */
	xa_lock_irqsave(&debugfs_dentries, flags);
	if (!xa_load(&debugfs_dentries, index) ||
	    xa_get_mark(&debugfs_dentries, index, REF_TRACKER_DIR_DEAD)) {
		xa_unlock_irqrestore(&debugfs_dentries, flags);
		return -ENODATA;
	}

	spin_lock(&dir->lock);
	xa_unlock(&debugfs_dentries);
	ret = ref_tracker_dir_seq_print(dir, f);
	spin_unlock_irqrestore(&dir->lock, flags);
	return ret;
}

static int ref_tracker_debugfs_open(struct inode *inode, struct file *filp)
{
	struct ref_tracker_dir *dir = inode->i_private;

	return single_open(filp, ref_tracker_debugfs_show, dir);
}

static const struct file_operations ref_tracker_debugfs_fops = {
	.owner		= THIS_MODULE,
	.open		= ref_tracker_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * ref_tracker_dir_debugfs - create debugfs file for ref_tracker_dir
 * @dir: ref_tracker_dir to be associated with debugfs file
 *
 * In most cases, a debugfs file will be created automatically for every
 * ref_tracker_dir. If the object was created before debugfs is brought up
 * then that may fail. In those cases, it is safe to call this at a later
 * time to create the file.
 */
void ref_tracker_dir_debugfs(struct ref_tracker_dir *dir)
{
	char name[NAME_MAX + 1];
	struct dentry *dentry;
	int ret;

	/* No-op if already created */
	dentry = xa_load(&debugfs_dentries, (unsigned long)dir);
	if (dentry && !xa_is_err(dentry))
		return;

	ret = snprintf(name, sizeof(name), "%s@%p", dir->class, dir);
	name[sizeof(name) - 1] = '\0';

	if (ret < sizeof(name)) {
		dentry = debugfs_create_file(name, S_IFREG | 0400,
					     ref_tracker_debug_dir, dir,
					     &ref_tracker_debugfs_fops);
		if (!IS_ERR(dentry)) {
			void *old;

			old = xa_store_irq(&debugfs_dentries, (unsigned long)dir,
					   dentry, GFP_KERNEL);

			if (xa_is_err(old))
				debugfs_remove(dentry);
			else
				WARN_ON_ONCE(old);
		}
	}
}
EXPORT_SYMBOL(ref_tracker_dir_debugfs);

void __ostream_printf ref_tracker_dir_symlink(struct ref_tracker_dir *dir, const char *fmt, ...)
{
	char name[NAME_MAX + 1];
	struct dentry *symlink, *dentry;
	va_list args;
	int ret;

	symlink = xa_load(&debugfs_symlinks, (unsigned long)dir);
	dentry = xa_load(&debugfs_dentries, (unsigned long)dir);

	/* Already created?*/
	if (symlink && !xa_is_err(symlink))
		return;

	if (!dentry || xa_is_err(dentry))
		return;

	va_start(args, fmt);
	ret = vsnprintf(name, sizeof(name), fmt, args);
	va_end(args);
	name[sizeof(name) - 1] = '\0';

	if (ret < sizeof(name)) {
		symlink = debugfs_create_symlink(name, ref_tracker_debug_dir,
						 dentry->d_name.name);
		if (!IS_ERR(symlink)) {
			void *old;

			old = xa_store_irq(&debugfs_symlinks, (unsigned long)dir,
					   symlink, GFP_KERNEL);
			if (xa_is_err(old))
				debugfs_remove(symlink);
			else
				WARN_ON_ONCE(old);
		}
	}
}
EXPORT_SYMBOL(ref_tracker_dir_symlink);

static void debugfs_reap_work(struct work_struct *work)
{
	struct dentry *dentry;
	unsigned long index;
	bool reaped;

	do {
		reaped = false;
		xa_for_each_marked(&debugfs_symlinks, index, dentry, REF_TRACKER_DIR_DEAD) {
			xa_erase_irq(&debugfs_symlinks, index);
			debugfs_remove(dentry);
			reaped = true;
		}
		xa_for_each_marked(&debugfs_dentries, index, dentry, REF_TRACKER_DIR_DEAD) {
			xa_erase_irq(&debugfs_dentries, index);
			debugfs_remove(dentry);
			reaped = true;
		}
	} while (reaped);
}

static int __init ref_tracker_debugfs_postcore_init(void)
{
	INIT_WORK(&debugfs_reap_worker, debugfs_reap_work);
	xa_init_flags(&debugfs_dentries, XA_FLAGS_LOCK_IRQ);
	xa_init_flags(&debugfs_symlinks, XA_FLAGS_LOCK_IRQ);
	return 0;
}
postcore_initcall(ref_tracker_debugfs_postcore_init);

static int __init ref_tracker_debugfs_late_init(void)
{
	ref_tracker_debug_dir = debugfs_create_dir("ref_tracker", NULL);
	return 0;
}
late_initcall(ref_tracker_debugfs_late_init);
#endif /* CONFIG_DEBUG_FS */
