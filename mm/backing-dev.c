
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
 * bdi_lock protects updates to bdi_list and bdi_pending_list, as well as
 * reader side protection for bdi_pending_list. bdi_list has RCU reader side
 * locking.
 */
DEFINE_SPINLOCK(bdi_lock);
LIST_HEAD(bdi_list);
LIST_HEAD(bdi_pending_list);

static struct task_struct *sync_supers_tsk;
static struct timer_list sync_supers_timer;

static int bdi_sync_supers(void *);
static void sync_supers_timer_fn(unsigned long);

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
	char *end;
	unsigned long read_ahead_kb;
	ssize_t ret = -EINVAL;

	read_ahead_kb = simple_strtoul(buf, &end, 10);
	if (*buf && (end[0] == '\0' || (end[0] == '\n' && end[1] == '\0'))) {
		bdi->ra_pages = read_ahead_kb >> (PAGE_SHIFT - 10);
		ret = count;
	}
	return ret;
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
	char *end;
	unsigned int ratio;
	ssize_t ret = -EINVAL;

	ratio = simple_strtoul(buf, &end, 10);
	if (*buf && (end[0] == '\0' || (end[0] == '\n' && end[1] == '\0'))) {
		ret = bdi_set_min_ratio(bdi, ratio);
		if (!ret)
			ret = count;
	}
	return ret;
}
BDI_SHOW(min_ratio, bdi->min_ratio)

static ssize_t max_ratio_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct backing_dev_info *bdi = dev_get_drvdata(dev);
	char *end;
	unsigned int ratio;
	ssize_t ret = -EINVAL;

	ratio = simple_strtoul(buf, &end, 10);
	if (*buf && (end[0] == '\0' || (end[0] == '\n' && end[1] == '\0'))) {
		ret = bdi_set_max_ratio(bdi, ratio);
		if (!ret)
			ret = count;
	}
	return ret;
}
BDI_SHOW(max_ratio, bdi->max_ratio)

#define __ATTR_RW(attr) __ATTR(attr, 0644, attr##_show, attr##_store)

static struct device_attribute bdi_dev_attrs[] = {
	__ATTR_RW(read_ahead_kb),
	__ATTR_RW(min_ratio),
	__ATTR_RW(max_ratio),
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

	sync_supers_tsk = kthread_run(bdi_sync_supers, NULL, "sync_supers");
	BUG_ON(IS_ERR(sync_supers_tsk));

	setup_timer(&sync_supers_timer, sync_supers_timer_fn, 0);
	bdi_arm_supers_timer();

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
 * kupdated() used to do this. We cannot do it from the bdi_forker_thread()
 * or we risk deadlocking on ->s_umount. The longer term solution would be
 * to implement sync_supers_bdi() or similar and simply do it from the
 * bdi writeback thread individually.
 */
static int bdi_sync_supers(void *unused)
{
	set_user_nice(current, 0);

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();

		/*
		 * Do this periodically, like kupdated() did before.
		 */
		sync_supers();
	}

	return 0;
}

void bdi_arm_supers_timer(void)
{
	unsigned long next;

	if (!dirty_writeback_interval)
		return;

	next = msecs_to_jiffies(dirty_writeback_interval * 10) + jiffies;
	mod_timer(&sync_supers_timer, round_jiffies_up(next));
}

static void sync_supers_timer_fn(unsigned long unused)
{
	wake_up_process(sync_supers_tsk);
	bdi_arm_supers_timer();
}

static void wakeup_timer_fn(unsigned long data)
{
	struct backing_dev_info *bdi = (struct backing_dev_info *)data;

	spin_lock_bh(&bdi->wb_lock);
	if (bdi->wb.task) {
		trace_writeback_wake_thread(bdi);
		wake_up_process(bdi->wb.task);
	} else if (bdi->dev) {
		/*
		 * When bdi tasks are inactive for long time, they are killed.
		 * In this case we have to wake-up the forker thread which
		 * should create and run the bdi thread.
		 */
		trace_writeback_wake_forker_thread(bdi);
		wake_up_process(default_backing_dev_info.wb.task);
	}
	spin_unlock_bh(&bdi->wb_lock);
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
 */
void bdi_wakeup_thread_delayed(struct backing_dev_info *bdi)
{
	unsigned long timeout;

	timeout = msecs_to_jiffies(dirty_writeback_interval * 10);
	mod_timer(&bdi->wb.wakeup_timer, jiffies + timeout);
}

/*
 * Calculate the longest interval (jiffies) bdi threads are allowed to be
 * inactive.
 */
static unsigned long bdi_longest_inactive(void)
{
	unsigned long interval;

	interval = msecs_to_jiffies(dirty_writeback_interval * 10);
	return max(5UL * 60 * HZ, interval);
}

/*
 * Clear pending bit and wakeup anybody waiting for flusher thread creation or
 * shutdown
 */
static void bdi_clear_pending(struct backing_dev_info *bdi)
{
	clear_bit(BDI_pending, &bdi->state);
	smp_mb__after_clear_bit();
	wake_up_bit(&bdi->state, BDI_pending);
}

static int bdi_forker_thread(void *ptr)
{
	struct bdi_writeback *me = ptr;

	current->flags |= PF_SWAPWRITE;
	set_freezable();

	/*
	 * Our parent may run at a different priority, just set us to normal
	 */
	set_user_nice(current, 0);

	for (;;) {
		struct task_struct *task = NULL;
		struct backing_dev_info *bdi;
		enum {
			NO_ACTION,   /* Nothing to do */
			FORK_THREAD, /* Fork bdi thread */
			KILL_THREAD, /* Kill inactive bdi thread */
		} action = NO_ACTION;

		/*
		 * Temporary measure, we want to make sure we don't see
		 * dirty data on the default backing_dev_info
		 */
		if (wb_has_dirty_io(me) || !list_empty(&me->bdi->work_list)) {
			del_timer(&me->wakeup_timer);
			wb_do_writeback(me, 0);
		}

		spin_lock_bh(&bdi_lock);
		/*
		 * In the following loop we are going to check whether we have
		 * some work to do without any synchronization with tasks
		 * waking us up to do work for them. Set the task state here
		 * so that we don't miss wakeups after verifying conditions.
		 */
		set_current_state(TASK_INTERRUPTIBLE);

		list_for_each_entry(bdi, &bdi_list, bdi_list) {
			bool have_dirty_io;

			if (!bdi_cap_writeback_dirty(bdi) ||
			     bdi_cap_flush_forker(bdi))
				continue;

			WARN(!test_bit(BDI_registered, &bdi->state),
			     "bdi %p/%s is not registered!\n", bdi, bdi->name);

			have_dirty_io = !list_empty(&bdi->work_list) ||
					wb_has_dirty_io(&bdi->wb);

			/*
			 * If the bdi has work to do, but the thread does not
			 * exist - create it.
			 */
			if (!bdi->wb.task && have_dirty_io) {
				/*
				 * Set the pending bit - if someone will try to
				 * unregister this bdi - it'll wait on this bit.
				 */
				set_bit(BDI_pending, &bdi->state);
				action = FORK_THREAD;
				break;
			}

			spin_lock(&bdi->wb_lock);

			/*
			 * If there is no work to do and the bdi thread was
			 * inactive long enough - kill it. The wb_lock is taken
			 * to make sure no-one adds more work to this bdi and
			 * wakes the bdi thread up.
			 */
			if (bdi->wb.task && !have_dirty_io &&
			    time_after(jiffies, bdi->wb.last_active +
						bdi_longest_inactive())) {
				task = bdi->wb.task;
				bdi->wb.task = NULL;
				spin_unlock(&bdi->wb_lock);
				set_bit(BDI_pending, &bdi->state);
				action = KILL_THREAD;
				break;
			}
			spin_unlock(&bdi->wb_lock);
		}
		spin_unlock_bh(&bdi_lock);

		/* Keep working if default bdi still has things to do */
		if (!list_empty(&me->bdi->work_list))
			__set_current_state(TASK_RUNNING);

		switch (action) {
		case FORK_THREAD:
			__set_current_state(TASK_RUNNING);
			task = kthread_create(bdi_writeback_thread, &bdi->wb,
					      "flush-%s", dev_name(bdi->dev));
			if (IS_ERR(task)) {
				/*
				 * If thread creation fails, force writeout of
				 * the bdi from the thread. Hopefully 1024 is
				 * large enough for efficient IO.
				 */
				writeback_inodes_wb(&bdi->wb, 1024,
						    WB_REASON_FORKER_THREAD);
			} else {
				/*
				 * The spinlock makes sure we do not lose
				 * wake-ups when racing with 'bdi_queue_work()'.
				 * And as soon as the bdi thread is visible, we
				 * can start it.
				 */
				spin_lock_bh(&bdi->wb_lock);
				bdi->wb.task = task;
				spin_unlock_bh(&bdi->wb_lock);
				wake_up_process(task);
			}
			bdi_clear_pending(bdi);
			break;

		case KILL_THREAD:
			__set_current_state(TASK_RUNNING);
			kthread_stop(task);
			bdi_clear_pending(bdi);
			break;

		case NO_ACTION:
			if (!wb_has_dirty_io(me) || !dirty_writeback_interval)
				/*
				 * There are no dirty data. The only thing we
				 * should now care about is checking for
				 * inactive bdi threads and killing them. Thus,
				 * let's sleep for longer time, save energy and
				 * be friendly for battery-driven devices.
				 */
				schedule_timeout(bdi_longest_inactive());
			else
				schedule_timeout(msecs_to_jiffies(dirty_writeback_interval * 10));
			try_to_freeze();
			break;
		}
	}

	return 0;
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

	/*
	 * Just start the forker thread for our default backing_dev_info,
	 * and add other bdi's to the list. They will get a thread created
	 * on-demand when they need it.
	 */
	if (bdi_cap_flush_forker(bdi)) {
		struct bdi_writeback *wb = &bdi->wb;

		wb->task = kthread_run(bdi_forker_thread, wb, "bdi-%s",
						dev_name(dev));
		if (IS_ERR(wb->task))
			return PTR_ERR(wb->task);
	}

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
	struct task_struct *task;

	if (!bdi_cap_writeback_dirty(bdi))
		return;

	/*
	 * Make sure nobody finds us on the bdi_list anymore
	 */
	bdi_remove_from_list(bdi);

	/*
	 * If setup is pending, wait for that to complete first
	 */
	wait_on_bit(&bdi->state, BDI_pending, bdi_sched_wait,
			TASK_UNINTERRUPTIBLE);

	/*
	 * Finally, kill the kernel thread. We don't need to be RCU
	 * safe anymore, since the bdi is gone from visibility.
	 */
	spin_lock_bh(&bdi->wb_lock);
	task = bdi->wb.task;
	bdi->wb.task = NULL;
	spin_unlock_bh(&bdi->wb_lock);

	if (task)
		kthread_stop(task);
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
		del_timer_sync(&bdi->wb.wakeup_timer);

		if (!bdi_cap_flush_forker(bdi))
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
	setup_timer(&wb->wakeup_timer, wakeup_timer_fn, (unsigned long)bdi);
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
	 * If bdi_unregister() had already been called earlier, the
	 * wakeup_timer could still be armed because bdi_prune_sb()
	 * can race with the bdi_wakeup_thread_delayed() calls from
	 * __mark_inode_dirty().
	 */
	del_timer_sync(&bdi->wb.wakeup_timer);

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
