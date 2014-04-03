
#include <linux/wait.h>
#include <linux/backing-dev.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/device.h>
#include <trace/events/writeback.h>

static atomic_long_t bdi_seq = ATOMIC_LONG_INIT(0);

struct backing_dev_info default_backing_dev_info = {
	.name		= "default",
	.ra_pages	= VM_MAX_READAHEAD * 1024 / PAGE_CACHE_SIZE,
	.state		= 0,
	.capabilities	= BDI_CAP_MAP_COPY,
};
EXPORT_SYMBOL_GPL(default_backing_dev_info);

struct backing_dev_info noop_backing_dev_info = {
	.name		= "noop",
	.capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK,
};
EXPORT_SYMBOL_GPL(noop_backing_dev_info);

static struct class *bdi_class;

/*
 * bdi_lock protects updates to bdi_list. bdi_list has RCU reader side
 * locking.
 */
DEFINE_SPINLOCK(bdi_lock);
LIST_HEAD(bdi_list);

/* bdi_wq serves all asynchronous writeback tasks */
struct workqueue_struct *bdi_wq;

void bdi_lock_two(struct bdi_writeback *wb1, struct bdi_writeback *wb2)
{
	if (wb1 < wb2) {
		spin_lock(&wb1->list_lock);
		spin_lock_nested(&wb2->list_lock, 1);
	} else {
		spin_lock(&wb2->list_lock);
		spin_lock_nested(&wb1->list_lock, 1);
	}
}

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>

static struct dentry *bdi_debug_root;

static void bdi_debug_init(void)
{
	bdi_debug_root = debugfs_create_dir("bdi", NULL);
}

static int bdi_debug_stats_show(struct seq_file *m, void *v)
{
	struct backing_dev_info *bdi = m->private;
	struct bdi_writeback *wb = &bdi->wb;
	unsigned long background_thresh;
	unsigned long dirty_thresh;
	unsigned long bdi_thresh;
	unsigned long nr_dirty, nr_io, nr_more_io;
	struct inode *inode;

	nr_dirty = nr_io = nr_more_io = 0;
	spin_lock(&wb->list_lock);
	list_for_each_entry(inode, &wb->b_dirty, i_wb_list)
		nr_dirty++;
	list_for_each_entry(inode, &wb->b_io, i_wb_list)
		nr_io++;
	list_for_each_entry(inode, &wb->b_more_io, i_wb_list)
		nr_more_io++;
	spin_unlock(&wb->list_lock);

	global_dirty_limits(&background_thresh, &dirty_thresh);
	bdi_thresh = bdi_dirty_limit(bdi, dirty_thresh);

#define K(x) ((x) << (PAGE_SHIFT - 10))
	seq_printf(m,
		   "BdiWriteback:       %10lu kB\n"
		   "BdiReclaimable:     %10lu kB\n"
		   "BdiDirtyThresh:     %10lu kB\n"
		   "DirtyThresh:        %10lu kB\n"
		   "BackgroundThresh:   %10lu kB\n"
		   "BdiDirtied:         %10lu kB\n"
		   "BdiWritten:         %10lu kB\n"
		   "BdiWriteBandwidth:  %10lu kBps\n"
		   "b_dirty:            %10lu\n"
		   "b_io:               %10lu\n"
		   "b_more_io:          %10lu\n"
		   "bdi_list:           %10u\n"
		   "state:              %10lx\n",
		   (unsigned long) K(bdi_stat(bdi, BDI_WRITEBACK)),
		   (unsigned long) K(bdi_stat(bdi, BDI_RECLAIMABLE)),
		   K(bdi_thresh),
		   K(dirty_thresh),
		   K(background_thresh),
		   (unsigned long) K(bdi_stat(bdi, BDI_DIRTIED)),
		   (unsigned long) K(bdi_stat(bdi, BDI_WRITTEN)),
		   (unsigned long) K(bdi->write_bandwidth),
		   nr_dirty,
		   nr_io,
		   nr_more_io,
		   !list_empty(&bdi->bdi_list), bdi->state);
#undef K

	return 0;
}

static int bdi_debug_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, bdi_debug_stats_show, inode->i_private);
}

static const struct file_operations bdi_debug_stats_fops = {
	.open		= bdi_debug_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void bdi_debug_register(struct backing_dev_info *bdi, const char *name)
{
	bdi->debug_dir = debugfs_create_dir(name, bdi_debug_root);
	bdi->debug_stats = debugfs_create_file("stats", 0444, bdi->debug_dir,
					       bdi, &bdi_debug_stats_fops);
}

static void bdi_debug_unregister(struct backing_dev_info *bdi)
{
	debugfs_remove(bdi->debug_stats);
	debugfs_remove(bdi->debug_dir);
}
#else
static inline void bdi_debug_init(void)
{
}
static inline void bdi_debug_register(struct backing_dev_info *bdi,
				      const char *name)
{
}
static inline void bdi_debug_unregister(struct backing_dev_info *bdi)
{
}
#endif

static ssize_t read_ahead_kb_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct backing_dev_info *bdi = dev_get_drvdata(dev);
	unsigned long read_ahead_kb;
	ssize_t ret;

	ret = kstrtoul(buf, 10, &read_ahead_kb);
	if (ret < 0)
		return ret;

	bdi->ra_pages = read_ahead_kb >> (PAGE_SHIFT - 10);

	return count;
}

#define K(pages) ((pages) << (PAGE_SHIFT - 10))

#define BDI_SHOW(name, expr)						\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr, char *page)	\
{									\
	struct backing_dev_info *bdi = dev_get_drvdata(dev);		\
									\
	return snprintf(page, PAGE_SIZE-1, "%lld\n", (long long)expr);	\
}

BDI_SHOW(read_ahead_kb, K(bdi->ra_pages))

static ssize_t min_ratio_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct backing_dev_info *bdi = dev_get_drvdata(dev);
	unsigned int ratio;
	ssize_t ret;

	ret = kstrtouint(buf, 10, &ratio);
	if (ret < 0)
		return ret;

	ret = bdi_set_min_ratio(bdi, ratio);
	if (!ret)
		ret = count;

	return ret;
}
BDI_SHOW(min_ratio, bdi->min_ratio)

static ssize_t max_ratio_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct backing_dev_info *bdi = dev_get_drvdata(dev);
	unsigned int ratio;
	ssize_t ret;

	ret = kstrtouint(buf, 10, &ratio);
	if (ret < 0)
		return ret;

	ret = bdi_set_max_ratio(bdi, ratio);
	if (!ret)
		ret = count;

	return ret;
}
BDI_SHOW(max_ratio, bdi->max_ratio)

static ssize_t stable_pages_required_show(struct device *dev,
					  struct device_attribute *attr,
					  char *page)
{
	struct backing_dev_info *bdi = dev_get_drvdata(dev);

	return snprintf(page, PAGE_SIZE-1, "%d\n",
			bdi_cap_stable_pages_required(bdi) ? 1 : 0);
}

#define __ATTR_RW(attr) __ATTR(attr, 0644, attr##_show, attr##_store)

static struct device_attribute bdi_dev_attrs[] = {
	__ATTR_RW(read_ahead_kb),
	__ATTR_RW(min_ratio),
	__ATTR_RW(max_ratio),
	__ATTR_RO(stable_pages_required),
	__ATTR_NULL,
};

static __init int bdi_class_init(void)
{
	bdi_class = class_create(THIS_MODULE, "bdi");
	if (IS_ERR(bdi_class))
		return PTR_ERR(bdi_class);

	bdi_class->dev_attrs = bdi_dev_attrs;
	bdi_debug_init();
	return 0;
}
postcore_initcall(bdi_class_init);

static int __init default_bdi_init(void)
{
	int err;

	bdi_wq = alloc_workqueue("writeback", WQ_MEM_RECLAIM | WQ_FREEZABLE |
					      WQ_UNBOUND | WQ_SYSFS, 0);
	if (!bdi_wq)
		return -ENOMEM;

	err = bdi_init(&default_backing_dev_info);
	if (!err)
		bdi_register(&default_backing_dev_info, NULL, "default");
	err = bdi_init(&noop_backing_dev_info);

	return err;
}
subsys_initcall(default_bdi_init);

int bdi_has_dirty_io(struct backing_dev_info *bdi)
{
	return wb_has_dirty_io(&bdi->wb);
}

/*
 * This function is used when the first inode for this bdi is marked dirty. It
 * wakes-up the corresponding bdi thread which should then take care of the
 * periodic background write-out of dirty inodes. Since the write-out would
 * starts only 'dirty_writeback_interval' centisecs from now anyway, we just
 * set up a timer which wakes the bdi thread up later.
 *
 * Note, we wouldn't bother setting up the timer, but this function is on the
 * fast-path (used by '__mark_inode_dirty()'), so we save few context switches
 * by delaying the wake-up.
 *
 * We have to be careful not to postpone flush work if it is scheduled for
 * earlier. Thus we use queue_delayed_work().
 */
void bdi_wakeup_thread_delayed(struct backing_dev_info *bdi)
{
	unsigned long timeout;

	timeout = msecs_to_jiffies(dirty_writeback_interval * 10);
	spin_lock_bh(&bdi->wb_lock);
	if (test_bit(BDI_registered, &bdi->state))
		queue_delayed_work(bdi_wq, &bdi->wb.dwork, timeout);
	spin_unlock_bh(&bdi->wb_lock);
}

/*
 * Remove bdi from bdi_list, and ensure that it is no longer visible
 */
static void bdi_remove_from_list(struct backing_dev_info *bdi)
{
	spin_lock_bh(&bdi_lock);
	list_del_rcu(&bdi->bdi_list);
	spin_unlock_bh(&bdi_lock);

	synchronize_rcu_expedited();
}

int bdi_register(struct backing_dev_info *bdi, struct device *parent,
		const char *fmt, ...)
{
	va_list args;
	struct device *dev;

	if (bdi->dev)	/* The driver needs to use separate queues per device */
		return 0;

	va_start(args, fmt);
	dev = device_create_vargs(bdi_class, parent, MKDEV(0, 0), bdi, fmt, args);
	va_end(args);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	bdi->dev = dev;

	bdi_debug_register(bdi, dev_name(dev));
	set_bit(BDI_registered, &bdi->state);

	spin_lock_bh(&bdi_lock);
	list_add_tail_rcu(&bdi->bdi_list, &bdi_list);
	spin_unlock_bh(&bdi_lock);

	trace_writeback_bdi_register(bdi);
	return 0;
}
EXPORT_SYMBOL(bdi_register);

int bdi_register_dev(struct backing_dev_info *bdi, dev_t dev)
{
	return bdi_register(bdi, NULL, "%u:%u", MAJOR(dev), MINOR(dev));
}
EXPORT_SYMBOL(bdi_register_dev);

/*
 * Remove bdi from the global list and shutdown any threads we have running
 */
static void bdi_wb_shutdown(struct backing_dev_info *bdi)
{
	if (!bdi_cap_writeback_dirty(bdi))
		return;

	/*
	 * Make sure nobody finds us on the bdi_list anymore
	 */
	bdi_remove_from_list(bdi);

	/* Make sure nobody queues further work */
	spin_lock_bh(&bdi->wb_lock);
	clear_bit(BDI_registered, &bdi->state);
	spin_unlock_bh(&bdi->wb_lock);

	/*
	 * Drain work list and shutdown the delayed_work.  At this point,
	 * @bdi->bdi_list is empty telling bdi_Writeback_workfn() that @bdi
	 * is dying and its work_list needs to be drained no matter what.
	 */
	mod_delayed_work(bdi_wq, &bdi->wb.dwork, 0);
	flush_delayed_work(&bdi->wb.dwork);
	WARN_ON(!list_empty(&bdi->work_list));

	/*
	 * This shouldn't be necessary unless @bdi for some reason has
	 * unflushed dirty IO after work_list is drained.  Do it anyway
	 * just in case.
	 */
	cancel_delayed_work_sync(&bdi->wb.dwork);
}

/*
 * This bdi is going away now, make sure that no super_blocks point to it
 */
static void bdi_prune_sb(struct backing_dev_info *bdi)
{
	struct super_block *sb;

	spin_lock(&sb_lock);
	list_for_each_entry(sb, &super_blocks, s_list) {
		if (sb->s_bdi == bdi)
			sb->s_bdi = &default_backing_dev_info;
	}
	spin_unlock(&sb_lock);
}

void bdi_unregister(struct backing_dev_info *bdi)
{
	struct device *dev = bdi->dev;

	if (dev) {
		bdi_set_min_ratio(bdi, 0);
		trace_writeback_bdi_unregister(bdi);
		bdi_prune_sb(bdi);

		bdi_wb_shutdown(bdi);
		bdi_debug_unregister(bdi);

		spin_lock_bh(&bdi->wb_lock);
		bdi->dev = NULL;
		spin_unlock_bh(&bdi->wb_lock);

		device_unregister(dev);
	}
}
EXPORT_SYMBOL(bdi_unregister);

static void bdi_wb_init(struct bdi_writeback *wb, struct backing_dev_info *bdi)
{
	memset(wb, 0, sizeof(*wb));

	wb->bdi = bdi;
	wb->last_old_flush = jiffies;
	INIT_LIST_HEAD(&wb->b_dirty);
	INIT_LIST_HEAD(&wb->b_io);
	INIT_LIST_HEAD(&wb->b_more_io);
	spin_lock_init(&wb->list_lock);
	INIT_DELAYED_WORK(&wb->dwork, bdi_writeback_workfn);
}

/*
 * Initial write bandwidth: 100 MB/s
 */
#define INIT_BW		(100 << (20 - PAGE_SHIFT))

int bdi_init(struct backing_dev_info *bdi)
{
	int i, err;

	bdi->dev = NULL;

	bdi->min_ratio = 0;
	bdi->max_ratio = 100;
	bdi->max_prop_frac = FPROP_FRAC_BASE;
	spin_lock_init(&bdi->wb_lock);
	INIT_LIST_HEAD(&bdi->bdi_list);
	INIT_LIST_HEAD(&bdi->work_list);

	bdi_wb_init(&bdi->wb, bdi);

	for (i = 0; i < NR_BDI_STAT_ITEMS; i++) {
		err = percpu_counter_init(&bdi->bdi_stat[i], 0);
		if (err)
			goto err;
	}

	bdi->dirty_exceeded = 0;

	bdi->bw_time_stamp = jiffies;
	bdi->written_stamp = 0;

	bdi->balanced_dirty_ratelimit = INIT_BW;
	bdi->dirty_ratelimit = INIT_BW;
	bdi->write_bandwidth = INIT_BW;
	bdi->avg_write_bandwidth = INIT_BW;

	err = fprop_local_init_percpu(&bdi->completions);

	if (err) {
err:
		while (i--)
			percpu_counter_destroy(&bdi->bdi_stat[i]);
	}

	return err;
}
EXPORT_SYMBOL(bdi_init);

void bdi_destroy(struct backing_dev_info *bdi)
{
	int i;

	/*
	 * Splice our entries to the default_backing_dev_info, if this
	 * bdi disappears
	 */
	if (bdi_has_dirty_io(bdi)) {
		struct bdi_writeback *dst = &default_backing_dev_info.wb;

		bdi_lock_two(&bdi->wb, dst);
		list_splice(&bdi->wb.b_dirty, &dst->b_dirty);
		list_splice(&bdi->wb.b_io, &dst->b_io);
		list_splice(&bdi->wb.b_more_io, &dst->b_more_io);
		spin_unlock(&bdi->wb.list_lock);
		spin_unlock(&dst->list_lock);
	}

	bdi_unregister(bdi);

	/*
	 * If bdi_unregister() had already been called earlier, the dwork
	 * could still be pending because bdi_prune_sb() can race with the
	 * bdi_wakeup_thread_delayed() calls from __mark_inode_dirty().
	 */
	cancel_delayed_work_sync(&bdi->wb.dwork);

	for (i = 0; i < NR_BDI_STAT_ITEMS; i++)
		percpu_counter_destroy(&bdi->bdi_stat[i]);

	fprop_local_destroy_percpu(&bdi->completions);
}
EXPORT_SYMBOL(bdi_destroy);

/*
 * For use from filesystems to quickly init and register a bdi associated
 * with dirty writeback
 */
int bdi_setup_and_register(struct backing_dev_info *bdi, char *name,
			   unsigned int cap)
{
	char tmp[32];
	int err;

	bdi->name = name;
	bdi->capabilities = cap;
	err = bdi_init(bdi);
	if (err)
		return err;

	sprintf(tmp, "%.28s%s", name, "-%d");
	err = bdi_register(bdi, NULL, tmp, atomic_long_inc_return(&bdi_seq));
	if (err) {
		bdi_destroy(bdi);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL(bdi_setup_and_register);

static wait_queue_head_t congestion_wqh[2] = {
		__WAIT_QUEUE_HEAD_INITIALIZER(congestion_wqh[0]),
		__WAIT_QUEUE_HEAD_INITIALIZER(congestion_wqh[1])
	};
static atomic_t nr_bdi_congested[2];

void clear_bdi_congested(struct backing_dev_info *bdi, int sync)
{
	enum bdi_state bit;
	wait_queue_head_t *wqh = &congestion_wqh[sync];

	bit = sync ? BDI_sync_congested : BDI_async_congested;
	if (test_and_clear_bit(bit, &bdi->state))
		atomic_dec(&nr_bdi_congested[sync]);
	smp_mb__after_clear_bit();
	if (waitqueue_active(wqh))
		wake_up(wqh);
}
EXPORT_SYMBOL(clear_bdi_congested);

void set_bdi_congested(struct backing_dev_info *bdi, int sync)
{
	enum bdi_state bit;

	bit = sync ? BDI_sync_congested : BDI_async_congested;
	if (!test_and_set_bit(bit, &bdi->state))
		atomic_inc(&nr_bdi_congested[sync]);
}
EXPORT_SYMBOL(set_bdi_congested);

/**
 * congestion_wait - wait for a backing_dev to become uncongested
 * @sync: SYNC or ASYNC IO
 * @timeout: timeout in jiffies
 *
 * Waits for up to @timeout jiffies for a backing_dev (any backing_dev) to exit
 * write congestion.  If no backing_devs are congested then just wait for the
 * next write to be completed.
 */
long congestion_wait(int sync, long timeout)
{
	long ret;
	unsigned long start = jiffies;
	DEFINE_WAIT(wait);
	wait_queue_head_t *wqh = &congestion_wqh[sync];

	prepare_to_wait(wqh, &wait, TASK_UNINTERRUPTIBLE);
	ret = io_schedule_timeout(timeout);
	finish_wait(wqh, &wait);

	trace_writeback_congestion_wait(jiffies_to_usecs(timeout),
					jiffies_to_usecs(jiffies - start));

	return ret;
}
EXPORT_SYMBOL(congestion_wait);

/**
 * wait_iff_congested - Conditionally wait for a backing_dev to become uncongested or a zone to complete writes
 * @zone: A zone to check if it is heavily congested
 * @sync: SYNC or ASYNC IO
 * @timeout: timeout in jiffies
 *
 * In the event of a congested backing_dev (any backing_dev) and the given
 * @zone has experienced recent congestion, this waits for up to @timeout
 * jiffies for either a BDI to exit congestion of the given @sync queue
 * or a write to complete.
 *
 * In the absence of zone congestion, cond_resched() is called to yield
 * the processor if necessary but otherwise does not sleep.
 *
 * The return value is 0 if the sleep is for the full timeout. Otherwise,
 * it is the number of jiffies that were still remaining when the function
 * returned. return_value == timeout implies the function did not sleep.
 */
long wait_iff_congested(struct zone *zone, int sync, long timeout)
{
	long ret;
	unsigned long start = jiffies;
	DEFINE_WAIT(wait);
	wait_queue_head_t *wqh = &congestion_wqh[sync];

	/*
	 * If there is no congestion, or heavy congestion is not being
	 * encountered in the current zone, yield if necessary instead
	 * of sleeping on the congestion queue
	 */
	if (atomic_read(&nr_bdi_congested[sync]) == 0 ||
			!zone_is_reclaim_congested(zone)) {
		cond_resched();

		/* In case we scheduled, work out time remaining */
		ret = timeout - (jiffies - start);
		if (ret < 0)
			ret = 0;

		goto out;
	}

	/* Sleep until uncongested or a write happens */
	prepare_to_wait(wqh, &wait, TASK_UNINTERRUPTIBLE);
	ret = io_schedule_timeout(timeout);
	finish_wait(wqh, &wait);

out:
	trace_writeback_wait_iff_congested(jiffies_to_usecs(timeout),
					jiffies_to_usecs(jiffies - start));

	return ret;
}
EXPORT_SYMBOL(wait_iff_congested);

int pdflush_proc_obsolete(struct ctl_table *table, int write,
			void __user *buffer, size_t *lenp, loff_t *ppos)
{
	char kbuf[] = "0\n";

	if (*ppos) {
		*lenp = 0;
		return 0;
	}

	if (copy_to_user(buffer, kbuf, sizeof(kbuf)))
		return -EFAULT;
	printk_once(KERN_WARNING "%s exported in /proc is scheduled for removal\n",
			table->procname);

	*lenp = 2;
	*ppos += *lenp;
	return 2;
}
