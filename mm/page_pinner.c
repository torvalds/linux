// SPDX-License-Identifier: GPL-2.0
#include <linux/debugfs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/memblock.h>
#include <linux/stacktrace.h>
#include <linux/page_pinner.h>
#include <linux/jump_label.h>
#include <linux/migrate.h>
#include <linux/stackdepot.h>
#include <linux/seq_file.h>
#include <linux/sched/clock.h>

#include "internal.h"

#define PAGE_PINNER_STACK_DEPTH 16
#define LONGTERM_PIN_BUCKETS	4096

struct page_pinner {
	depot_stack_handle_t handle;
	s64 ts_usec;
	atomic_t count;
};

struct captured_pinner {
	depot_stack_handle_t handle;
	union {
		s64 ts_usec;
		s64 elapsed;
	};

	/* struct page fields */
	unsigned long pfn;
	int count;
	int mapcount;
	struct address_space *mapping;
	unsigned long flags;
};

struct longterm_pinner {
	spinlock_t lock;
	unsigned int index;
	struct captured_pinner pinner[LONGTERM_PIN_BUCKETS];
};

static struct longterm_pinner lt_pinner = {
	.lock = __SPIN_LOCK_UNLOCKED(lt_pinner.lock),
};

static s64 threshold_usec = 300000;

/* alloc_contig failed pinner */
static struct longterm_pinner acf_pinner = {
	.lock = __SPIN_LOCK_UNLOCKED(acf_pinner.lock),
};

static bool page_pinner_enabled;
DEFINE_STATIC_KEY_FALSE(page_pinner_inited);

DEFINE_STATIC_KEY_TRUE(failure_tracking);
EXPORT_SYMBOL(failure_tracking);

static depot_stack_handle_t failure_handle;

static int __init early_page_pinner_param(char *buf)
{
	page_pinner_enabled = true;
	return 0;
}
early_param("page_pinner", early_page_pinner_param);

static bool need_page_pinner(void)
{
	return page_pinner_enabled;
}

static noinline void register_failure_stack(void)
{
	unsigned long entries[4];
	unsigned int nr_entries;

	nr_entries = stack_trace_save(entries, ARRAY_SIZE(entries), 0);
	failure_handle = stack_depot_save(entries, nr_entries, GFP_KERNEL);
}

static void init_page_pinner(void)
{
	if (!page_pinner_enabled)
		return;

	register_failure_stack();
	static_branch_enable(&page_pinner_inited);
}

struct page_ext_operations page_pinner_ops = {
	.size = sizeof(struct page_pinner),
	.need = need_page_pinner,
	.init = init_page_pinner,
};

static inline struct page_pinner *get_page_pinner(struct page_ext *page_ext)
{
	return (void *)page_ext + page_pinner_ops.offset;
}

static noinline depot_stack_handle_t save_stack(gfp_t flags)
{
	unsigned long entries[PAGE_PINNER_STACK_DEPTH];
	depot_stack_handle_t handle;
	unsigned int nr_entries;

	nr_entries = stack_trace_save(entries, ARRAY_SIZE(entries), 2);
	handle = stack_depot_save(entries, nr_entries, flags);
	if (!handle)
		handle = failure_handle;

	return handle;
}

static void capture_page_state(struct page *page,
			       struct captured_pinner *record)
{
	record->flags = page->flags;
	record->mapping = page_mapping(page);
	record->pfn = page_to_pfn(page);
	record->count = page_count(page);
	record->mapcount = page_mapcount(page);
}

static void check_longterm_pin(struct page_pinner *page_pinner,
			      struct page *page)
{
	s64 now, delta = 0;
	unsigned long flags;
	unsigned int idx;
	struct captured_pinner record;

	now = ktime_to_us(ktime_get_boottime());

	/* get/put_page can be raced. Ignore that case */
	if (page_pinner->ts_usec < now)
		delta = now - page_pinner->ts_usec;

	if (delta <= threshold_usec)
		return;

	record.handle = page_pinner->handle;
	record.elapsed = delta;
	capture_page_state(page, &record);

	spin_lock_irqsave(&lt_pinner.lock, flags);
	idx = lt_pinner.index++;
	lt_pinner.index %= LONGTERM_PIN_BUCKETS;
	lt_pinner.pinner[idx] = record;
	spin_unlock_irqrestore(&lt_pinner.lock, flags);
}

void __reset_page_pinner(struct page *page, unsigned int order, bool free)
{
	struct page_pinner *page_pinner;
	struct page_ext *page_ext;
	int i;

	page_ext = page_ext_get(page);
	if (unlikely(!page_ext))
		return;

	for (i = 0; i < (1 << order); i++) {
		if (!test_bit(PAGE_EXT_GET, &page_ext->flags) &&
			!test_bit(PAGE_EXT_PINNER_MIGRATION_FAILED,
				  &page_ext->flags))
			continue;

		page_pinner = get_page_pinner(page_ext);
		if (free) {
			/* record page free call path */
			__page_pinner_migration_failed(page);
			atomic_set(&page_pinner->count, 0);
			__clear_bit(PAGE_EXT_PINNER_MIGRATION_FAILED, &page_ext->flags);
		} else {
			check_longterm_pin(page_pinner, page);
		}
		clear_bit(PAGE_EXT_GET, &page_ext->flags);
		page_ext = page_ext_next(page_ext);
	}
	page_ext_put(page_ext);
}

static inline void __set_page_pinner_handle(struct page *page,
	struct page_ext *page_ext, depot_stack_handle_t handle,
	unsigned int order)
{
	struct page_pinner *page_pinner;
	int i;
	s64 usec = ktime_to_us(ktime_get_boottime());

	for (i = 0; i < (1 << order); i++) {
		page_pinner = get_page_pinner(page_ext);
		page_pinner->handle = handle;
		page_pinner->ts_usec = usec;
		set_bit(PAGE_EXT_GET, &page_ext->flags);
		atomic_inc(&page_pinner->count);
		page_ext = page_ext_next(page_ext);
	}
}

noinline void __set_page_pinner(struct page *page, unsigned int order)
{
	struct page_ext *page_ext;
	depot_stack_handle_t handle;

	handle = save_stack(GFP_NOWAIT|__GFP_NOWARN);

	page_ext = page_ext_get(page);
	if (unlikely(!page_ext))
		return;
	__set_page_pinner_handle(page, page_ext, handle, order);
	page_ext_put(page_ext);
}

static ssize_t
print_page_pinner(bool longterm, char __user *buf, size_t count, struct captured_pinner *record)
{
	int ret;
	unsigned long *entries;
	unsigned int nr_entries;
	char *kbuf;

	count = min_t(size_t, count, PAGE_SIZE);
	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (longterm) {
		ret = snprintf(kbuf, count, "Page pinned for %lld us\n",
			       record->elapsed);
	} else {
		s64 ts_usec = record->ts_usec;
		unsigned long rem_usec = do_div(ts_usec, 1000000);

		ret = snprintf(kbuf, count,
			       "Page pinned ts [%5lu.%06lu]\n",
			       (unsigned long)ts_usec, rem_usec);
	}

	if (ret >= count)
		goto err;

	/* Print information relevant to grouping pages by mobility */
	ret += snprintf(kbuf + ret, count - ret,
			"PFN 0x%lx Block %lu count %d mapcount %d mapping %pS Flags %#lx(%pGp)\n",
			record->pfn,
			record->pfn >> pageblock_order,
			record->count, record->mapcount,
			record->mapping,
			record->flags, &record->flags);

	if (ret >= count)
		goto err;

	nr_entries = stack_depot_fetch(record->handle, &entries);
	ret += stack_trace_snprint(kbuf + ret, count - ret, entries,
				   nr_entries, 0);
	if (ret >= count)
		goto err;

	ret += snprintf(kbuf + ret, count - ret, "\n");
	if (ret >= count)
		goto err;

	if (copy_to_user(buf, kbuf, ret))
		ret = -EFAULT;

	kfree(kbuf);
	return ret;

err:
	kfree(kbuf);
	return -ENOMEM;
}

void __dump_page_pinner(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);
	struct page_pinner *page_pinner;
	depot_stack_handle_t handle;
	unsigned long *entries;
	unsigned int nr_entries;
	int pageblock_mt;
	unsigned long pfn;
	int count;
	unsigned long rem_usec;
	s64 ts_usec;

	if (unlikely(!page_ext)) {
		pr_alert("There is not page extension available.\n");
		return;
	}

	page_pinner = get_page_pinner(page_ext);

	count = atomic_read(&page_pinner->count);
	if (!count) {
		pr_alert("page_pinner info is not present (never set?)\n");
		page_ext_put(page_ext);
		return;
	}

	pfn = page_to_pfn(page);
	ts_usec = page_pinner->ts_usec;
	rem_usec = do_div(ts_usec, 1000000);
	pr_alert("page last pinned %5lu.%06lu] count %d\n",
		 (unsigned long)ts_usec, rem_usec, count);

	pageblock_mt = get_pageblock_migratetype(page);
	pr_alert("PFN %lu Block %lu type %s Flags %#lx(%pGp)\n",
			pfn,
			pfn >> pageblock_order,
			migratetype_names[pageblock_mt],
			page->flags, &page->flags);

	handle = READ_ONCE(page_pinner->handle);
	if (!handle) {
		pr_alert("page_pinner allocation stack trace missing\n");
	} else {
		nr_entries = stack_depot_fetch(handle, &entries);
		stack_trace_print(entries, nr_entries, 0);
	}
	page_ext_put(page_ext);
}

void __page_pinner_migration_failed(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);
	struct captured_pinner record;
	unsigned long flags;
	unsigned int idx;

	if (unlikely(!page_ext))
		return;

	if (!test_bit(PAGE_EXT_PINNER_MIGRATION_FAILED, &page_ext->flags)) {
		page_ext_put(page_ext);
		return;
	}

	page_ext_put(page_ext);
	record.handle = save_stack(GFP_NOWAIT|__GFP_NOWARN);
	record.ts_usec = ktime_to_us(ktime_get_boottime());
	capture_page_state(page, &record);

	spin_lock_irqsave(&acf_pinner.lock, flags);
	idx = acf_pinner.index++;
	acf_pinner.index %= LONGTERM_PIN_BUCKETS;
	acf_pinner.pinner[idx] = record;
	spin_unlock_irqrestore(&acf_pinner.lock, flags);
}
EXPORT_SYMBOL(__page_pinner_migration_failed);

void __page_pinner_mark_migration_failed_pages(struct list_head *page_list)
{
	struct page *page;
	struct page_ext *page_ext;

	list_for_each_entry(page, page_list, lru) {
		/* The page will be freed by putback_movable_pages soon */
		if (page_count(page) == 1)
			continue;
		page_ext = page_ext_get(page);
		if (unlikely(!page_ext))
			continue;
		__set_bit(PAGE_EXT_PINNER_MIGRATION_FAILED, &page_ext->flags);
		page_ext_put(page_ext);
		__page_pinner_migration_failed(page);
	}
}

static ssize_t
read_longterm_page_pinner(struct file *file, char __user *buf, size_t count,
			  loff_t *ppos)
{
	loff_t i, idx;
	struct captured_pinner record;
	unsigned long flags;

	if (!static_branch_unlikely(&page_pinner_inited))
		return -EINVAL;

	if (*ppos >= LONGTERM_PIN_BUCKETS)
		return 0;

	i = *ppos;
	*ppos = i + 1;

	/*
	 * reading the records in the reverse order with newest one
	 * being read first followed by older ones
	 */
	idx = (lt_pinner.index - 1 - i + LONGTERM_PIN_BUCKETS) %
	       LONGTERM_PIN_BUCKETS;
	spin_lock_irqsave(&lt_pinner.lock, flags);
	record = lt_pinner.pinner[idx];
	spin_unlock_irqrestore(&lt_pinner.lock, flags);
	if (!record.handle)
		return 0;

	return print_page_pinner(true, buf, count, &record);
}

static const struct file_operations proc_longterm_pinner_operations = {
	.read		= read_longterm_page_pinner,
};

static ssize_t read_alloc_contig_failed(struct file *file, char __user *buf,
					size_t count, loff_t *ppos)
{
	loff_t i, idx;
	struct captured_pinner record;
	unsigned long flags;

	if (!static_branch_unlikely(&failure_tracking))
		return -EINVAL;

	if (*ppos >= LONGTERM_PIN_BUCKETS)
		return 0;

	i = *ppos;
	*ppos = i + 1;

	/*
	 * reading the records in the reverse order with newest one
	 * being read first followed by older ones
	 */
	idx = (acf_pinner.index - 1 - i + LONGTERM_PIN_BUCKETS) %
	       LONGTERM_PIN_BUCKETS;

	spin_lock_irqsave(&acf_pinner.lock, flags);
	record = acf_pinner.pinner[idx];
	spin_unlock_irqrestore(&acf_pinner.lock, flags);
	if (!record.handle)
		return 0;

	return print_page_pinner(false, buf, count, &record);
}

static const struct file_operations proc_alloc_contig_failed_operations = {
	.read		= read_alloc_contig_failed,
};

static int pp_threshold_set(void *data, unsigned long long val)
{
	unsigned long flags;

	threshold_usec = (s64)val;

	spin_lock_irqsave(&lt_pinner.lock, flags);
	memset(lt_pinner.pinner, 0,
	       sizeof(struct captured_pinner) * LONGTERM_PIN_BUCKETS);
	lt_pinner.index = 0;
	spin_unlock_irqrestore(&lt_pinner.lock, flags);
	return 0;
}

static int pp_threshold_get(void *data, unsigned long long *val)
{
	*val = (unsigned long long)threshold_usec;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(pp_threshold_fops, pp_threshold_get,
			 pp_threshold_set, "%lld\n");

static int failure_tracking_set(void *data, u64 val)
{
	bool on;

	on = (bool)val;
	if (on)
		static_branch_enable(&failure_tracking);
	else
		static_branch_disable(&failure_tracking);
	return 0;
}

static int failure_tracking_get(void *data, u64 *val)
{
	*val = static_branch_unlikely(&failure_tracking);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(failure_tracking_fops,
			 failure_tracking_get,
			 failure_tracking_set, "%llu\n");

static int __init page_pinner_init(void)
{
	struct dentry *pp_debugfs_root;

	if (!static_branch_unlikely(&page_pinner_inited))
		return 0;

	pr_info("page_pinner enabled\n");

	pp_debugfs_root = debugfs_create_dir("page_pinner", NULL);

	debugfs_create_file("longterm_pinner", 0444, pp_debugfs_root, NULL,
			    &proc_longterm_pinner_operations);

	debugfs_create_file("threshold", 0644, pp_debugfs_root, NULL,
			    &pp_threshold_fops);

	debugfs_create_file("alloc_contig_failed", 0444,
			    pp_debugfs_root, NULL,
			    &proc_alloc_contig_failed_operations);

	debugfs_create_file("failure_tracking", 0644,
			    pp_debugfs_root, NULL,
			    &failure_tracking_fops);
	return 0;
}
late_initcall(page_pinner_init)
