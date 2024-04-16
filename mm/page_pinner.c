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
static unsigned long pp_buf_size = 4096;

struct page_pinner {
	depot_stack_handle_t handle;
	u64 ts_usec;
	atomic_t count;
};

enum pp_state {
	PP_PUT,
	PP_FREE,
	PP_FAIL_DETECTED,
};

struct captured_pinner {
	depot_stack_handle_t handle;
	union {
		u64 ts_usec;
		u64 elapsed;
	};

	/* struct page fields */
	unsigned long pfn;
	int count;
	int mapcount;
	struct address_space *mapping;
	unsigned long flags;
	enum pp_state state;
};

struct page_pinner_buffer {
	spinlock_t lock;
	unsigned long index;
	struct captured_pinner *buffer;
};

/* alloc_contig failed pinner */
static struct page_pinner_buffer pp_buffer;

static bool page_pinner_enabled;
DEFINE_STATIC_KEY_FALSE(page_pinner_inited);
EXPORT_SYMBOL_GPL(page_pinner_inited);

DEFINE_STATIC_KEY_TRUE(failure_tracking);

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

	pp_buffer.buffer = kvmalloc_array(pp_buf_size, sizeof(*pp_buffer.buffer),
				GFP_KERNEL);
	if (!pp_buffer.buffer) {
		pr_info("page_pinner disabled due to failure of buffer allocation\n");
		return;
	}

	spin_lock_init(&pp_buffer.lock);
	pp_buffer.index = 0;

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

static void add_record(struct page_pinner_buffer *pp_buf,
		       struct captured_pinner *record)
{
	unsigned long flags;
	unsigned int idx;

	spin_lock_irqsave(&pp_buf->lock, flags);
	idx = pp_buf->index++;
	pp_buf->index %= pp_buf_size;
	pp_buf->buffer[idx] = *record;
	spin_unlock_irqrestore(&pp_buf->lock, flags);
}

void __free_page_pinner(struct page *page, unsigned int order)
{
	struct page_pinner *page_pinner;
	struct page_ext *page_ext;
	int i;

	/* free_page could be called before buffer is initialized */
	if (!pp_buffer.buffer)
		return;

	page_ext = page_ext_get(page);
	if (unlikely(!page_ext))
		return;

	for (i = 0; i < (1 << order); i++) {
		struct captured_pinner record;

		if (!test_bit(PAGE_EXT_PINNER_MIGRATION_FAILED, &page_ext->flags))
			continue;

		page_pinner = get_page_pinner(page_ext);

		record.handle = save_stack(GFP_NOWAIT|__GFP_NOWARN);
		record.ts_usec = (u64)ktime_to_us(ktime_get_boottime());
		record.state = PP_FREE;
		capture_page_state(page, &record);

		add_record(&pp_buffer, &record);

		atomic_set(&page_pinner->count, 0);
		page_pinner->ts_usec = 0;
		clear_bit(PAGE_EXT_PINNER_MIGRATION_FAILED, &page_ext->flags);
		page_ext = page_ext_next(page_ext);
	}
	page_ext_put(page_ext);
}

static ssize_t
print_page_pinner(char __user *buf, size_t count, struct captured_pinner *record)
{
	int ret;
	unsigned long *entries;
	unsigned int nr_entries;
	char *kbuf;

	count = min_t(size_t, count, PAGE_SIZE);
	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (record->state == PP_PUT) {
		ret = snprintf(kbuf, count, "At least, pinned for %llu us\n",
			       record->elapsed);
	} else {
		u64 ts_usec = record->ts_usec;
		unsigned long rem_usec = do_div(ts_usec, 1000000);

		ret = snprintf(kbuf, count,
			       "%s [%5lu.%06lu]\n",
			       record->state == PP_FREE ? "Freed at" :
							  "Failure detected at",
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

void __page_pinner_failure_detect(struct page *page)
{
	struct page_ext *page_ext;
	struct page_pinner *page_pinner;
	struct captured_pinner record;
	u64 now;

	if (!static_branch_unlikely(&failure_tracking))
		return;

	page_ext = page_ext_get(page);
	if (unlikely(!page_ext))
		return;

	if (test_bit(PAGE_EXT_PINNER_MIGRATION_FAILED, &page_ext->flags)) {
		page_ext_put(page_ext);
		return;
	}

	now = (u64)ktime_to_us(ktime_get_boottime());
	page_pinner = get_page_pinner(page_ext);
	if (!page_pinner->ts_usec)
		page_pinner->ts_usec = now;
	set_bit(PAGE_EXT_PINNER_MIGRATION_FAILED, &page_ext->flags);
	record.handle = save_stack(GFP_NOWAIT|__GFP_NOWARN);
	record.ts_usec = now;
	record.state = PP_FAIL_DETECTED;
	capture_page_state(page, &record);

	add_record(&pp_buffer, &record);
	page_ext_put(page_ext);
}
EXPORT_SYMBOL_GPL(__page_pinner_failure_detect);

void __page_pinner_put_page(struct page *page)
{
	struct page_ext *page_ext;
	struct page_pinner *page_pinner;
	struct captured_pinner record;
	u64 now, ts_usec;

	if (!static_branch_unlikely(&failure_tracking))
		return;

	page_ext = page_ext_get(page);
	if (unlikely(!page_ext))
		return;

	if (!test_bit(PAGE_EXT_PINNER_MIGRATION_FAILED, &page_ext->flags)) {
		page_ext_put(page_ext);
		return;
	}

	page_pinner = get_page_pinner(page_ext);
	record.handle = save_stack(GFP_NOWAIT|__GFP_NOWARN);
	now = (u64)ktime_to_us(ktime_get_boottime());
	ts_usec = page_pinner->ts_usec;

	if (now > ts_usec)
		record.elapsed = now - ts_usec;
	else
		record.elapsed = 0;
	record.state = PP_PUT;
	capture_page_state(page, &record);

	add_record(&pp_buffer, &record);
	page_ext_put(page_ext);
}
EXPORT_SYMBOL_GPL(__page_pinner_put_page);

static ssize_t read_buffer(struct file *file, char __user *buf,
					size_t count, loff_t *ppos)
{
	u64 tmp;
	loff_t i, idx;
	struct captured_pinner record;
	unsigned long flags;

	if (!static_branch_unlikely(&failure_tracking))
		return -EINVAL;

	if (*ppos >= pp_buf_size)
		return 0;

	i = *ppos;
	*ppos = i + 1;

	/*
	 * reading the records in the reverse order with newest one
	 * being read first followed by older ones
	 */
	tmp = pp_buffer.index - 1 - i + pp_buf_size;
	idx = do_div(tmp, pp_buf_size);

	spin_lock_irqsave(&pp_buffer.lock, flags);
	record = pp_buffer.buffer[idx];
	spin_unlock_irqrestore(&pp_buffer.lock, flags);
	if (!record.handle)
		return 0;

	return print_page_pinner(buf, count, &record);
}

static const struct file_operations proc_buffer_operations = {
	.read		= read_buffer,
};

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

static int buffer_size_set(void *data, u64 val)
{
	unsigned long flags;
	struct captured_pinner *new, *old;

	new = kvmalloc_array(val, sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	spin_lock_irqsave(&pp_buffer.lock, flags);
	old = pp_buffer.buffer;
	pp_buffer.buffer = new;
	pp_buffer.index = 0;
	pp_buf_size = val;
	spin_unlock_irqrestore(&pp_buffer.lock, flags);
	kvfree(old);

	return 0;
}

static int buffer_size_get(void *data, u64 *val)
{
	*val = pp_buf_size;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(buffer_size_fops,
			 buffer_size_get,
			 buffer_size_set, "%llu\n");

static int __init page_pinner_init(void)
{
	struct dentry *pp_debugfs_root;

	if (!static_branch_unlikely(&page_pinner_inited))
		return 0;

	pr_info("page_pinner enabled\n");

	pp_debugfs_root = debugfs_create_dir("page_pinner", NULL);

	debugfs_create_file("buffer", 0444,
			    pp_debugfs_root, NULL,
			    &proc_buffer_operations);

	debugfs_create_file("failure_tracking", 0644,
			    pp_debugfs_root, NULL,
			    &failure_tracking_fops);

	debugfs_create_file("buffer_size", 0644,
			    pp_debugfs_root, NULL,
			    &buffer_size_fops);
	return 0;
}
late_initcall(page_pinner_init)
