// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2006 Jens Axboe <axboe@kernel.dk>
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/blktrace_api.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/blk-cgroup.h>

#include "../../block/blk.h"

#include <trace/events/block.h>

#include "trace_output.h"

#ifdef CONFIG_BLK_DEV_IO_TRACE

static unsigned int blktrace_seq __read_mostly = 1;

static struct trace_array *blk_tr;
static bool blk_tracer_enabled __read_mostly;

static LIST_HEAD(running_trace_list);
static __cacheline_aligned_in_smp DEFINE_RAW_SPINLOCK(running_trace_lock);

/* Select an alternative, minimalistic output than the original one */
#define TRACE_BLK_OPT_CLASSIC	0x1
#define TRACE_BLK_OPT_CGROUP	0x2
#define TRACE_BLK_OPT_CGNAME	0x4

static struct tracer_opt blk_tracer_opts[] = {
	/* Default disable the minimalistic output */
	{ TRACER_OPT(blk_classic, TRACE_BLK_OPT_CLASSIC) },
#ifdef CONFIG_BLK_CGROUP
	{ TRACER_OPT(blk_cgroup, TRACE_BLK_OPT_CGROUP) },
	{ TRACER_OPT(blk_cgname, TRACE_BLK_OPT_CGNAME) },
#endif
	{ }
};

static struct tracer_flags blk_tracer_flags = {
	.val  = 0,
	.opts = blk_tracer_opts,
};

/* Global reference count of probes */
static DEFINE_MUTEX(blk_probe_mutex);
static int blk_probes_ref;

static void blk_register_tracepoints(void);
static void blk_unregister_tracepoints(void);

/*
 * Send out a notify message.
 */
static void trace_note(struct blk_trace *bt, pid_t pid, int action,
		       const void *data, size_t len, u64 cgid)
{
	struct blk_io_trace *t;
	struct ring_buffer_event *event = NULL;
	struct trace_buffer *buffer = NULL;
	unsigned int trace_ctx = 0;
	int cpu = smp_processor_id();
	bool blk_tracer = blk_tracer_enabled;
	ssize_t cgid_len = cgid ? sizeof(cgid) : 0;

	if (blk_tracer) {
		buffer = blk_tr->array_buffer.buffer;
		trace_ctx = tracing_gen_ctx_flags(0);
		event = trace_buffer_lock_reserve(buffer, TRACE_BLK,
						  sizeof(*t) + len + cgid_len,
						  trace_ctx);
		if (!event)
			return;
		t = ring_buffer_event_data(event);
		goto record_it;
	}

	if (!bt->rchan)
		return;

	t = relay_reserve(bt->rchan, sizeof(*t) + len + cgid_len);
	if (t) {
		t->magic = BLK_IO_TRACE_MAGIC | BLK_IO_TRACE_VERSION;
		t->time = ktime_to_ns(ktime_get());
record_it:
		t->device = bt->dev;
		t->action = action | (cgid ? __BLK_TN_CGROUP : 0);
		t->pid = pid;
		t->cpu = cpu;
		t->pdu_len = len + cgid_len;
		if (cgid_len)
			memcpy((void *)t + sizeof(*t), &cgid, cgid_len);
		memcpy((void *) t + sizeof(*t) + cgid_len, data, len);

		if (blk_tracer)
			trace_buffer_unlock_commit(blk_tr, buffer, event, trace_ctx);
	}
}

/*
 * Send out a notify for this process, if we haven't done so since a trace
 * started
 */
static void trace_note_tsk(struct task_struct *tsk)
{
	unsigned long flags;
	struct blk_trace *bt;

	tsk->btrace_seq = blktrace_seq;
	raw_spin_lock_irqsave(&running_trace_lock, flags);
	list_for_each_entry(bt, &running_trace_list, running_list) {
		trace_note(bt, tsk->pid, BLK_TN_PROCESS, tsk->comm,
			   sizeof(tsk->comm), 0);
	}
	raw_spin_unlock_irqrestore(&running_trace_lock, flags);
}

static void trace_note_time(struct blk_trace *bt)
{
	struct timespec64 now;
	unsigned long flags;
	u32 words[2];

	/* need to check user space to see if this breaks in y2038 or y2106 */
	ktime_get_real_ts64(&now);
	words[0] = (u32)now.tv_sec;
	words[1] = now.tv_nsec;

	local_irq_save(flags);
	trace_note(bt, 0, BLK_TN_TIMESTAMP, words, sizeof(words), 0);
	local_irq_restore(flags);
}

void __blk_trace_note_message(struct blk_trace *bt,
		struct cgroup_subsys_state *css, const char *fmt, ...)
{
	int n;
	va_list args;
	unsigned long flags;
	char *buf;
	u64 cgid = 0;

	if (unlikely(bt->trace_state != Blktrace_running &&
		     !blk_tracer_enabled))
		return;

	/*
	 * If the BLK_TC_NOTIFY action mask isn't set, don't send any note
	 * message to the trace.
	 */
	if (!(bt->act_mask & BLK_TC_NOTIFY))
		return;

	local_irq_save(flags);
	buf = this_cpu_ptr(bt->msg_data);
	va_start(args, fmt);
	n = vscnprintf(buf, BLK_TN_MAX_MSG, fmt, args);
	va_end(args);

#ifdef CONFIG_BLK_CGROUP
	if (css && (blk_tracer_flags.val & TRACE_BLK_OPT_CGROUP))
		cgid = cgroup_id(css->cgroup);
	else
		cgid = 1;
#endif
	trace_note(bt, current->pid, BLK_TN_MESSAGE, buf, n, cgid);
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(__blk_trace_note_message);

static int act_log_check(struct blk_trace *bt, u32 what, sector_t sector,
			 pid_t pid)
{
	if (((bt->act_mask << BLK_TC_SHIFT) & what) == 0)
		return 1;
	if (sector && (sector < bt->start_lba || sector > bt->end_lba))
		return 1;
	if (bt->pid && pid != bt->pid)
		return 1;

	return 0;
}

/*
 * Data direction bit lookup
 */
static const u32 ddir_act[2] = { BLK_TC_ACT(BLK_TC_READ),
				 BLK_TC_ACT(BLK_TC_WRITE) };

#define BLK_TC_RAHEAD		BLK_TC_AHEAD
#define BLK_TC_PREFLUSH		BLK_TC_FLUSH

/* The ilog2() calls fall out because they're constant */
#define MASK_TC_BIT(rw, __name) ((rw & REQ_ ## __name) << \
	  (ilog2(BLK_TC_ ## __name) + BLK_TC_SHIFT - __REQ_ ## __name))

/*
 * The worker for the various blk_add_trace*() types. Fills out a
 * blk_io_trace structure and places it in a per-cpu subbuffer.
 */
static void __blk_add_trace(struct blk_trace *bt, sector_t sector, int bytes,
		     int op, int op_flags, u32 what, int error, int pdu_len,
		     void *pdu_data, u64 cgid)
{
	struct task_struct *tsk = current;
	struct ring_buffer_event *event = NULL;
	struct trace_buffer *buffer = NULL;
	struct blk_io_trace *t;
	unsigned long flags = 0;
	unsigned long *sequence;
	unsigned int trace_ctx = 0;
	pid_t pid;
	int cpu;
	bool blk_tracer = blk_tracer_enabled;
	ssize_t cgid_len = cgid ? sizeof(cgid) : 0;

	if (unlikely(bt->trace_state != Blktrace_running && !blk_tracer))
		return;

	what |= ddir_act[op_is_write(op) ? WRITE : READ];
	what |= MASK_TC_BIT(op_flags, SYNC);
	what |= MASK_TC_BIT(op_flags, RAHEAD);
	what |= MASK_TC_BIT(op_flags, META);
	what |= MASK_TC_BIT(op_flags, PREFLUSH);
	what |= MASK_TC_BIT(op_flags, FUA);
	if (op == REQ_OP_DISCARD || op == REQ_OP_SECURE_ERASE)
		what |= BLK_TC_ACT(BLK_TC_DISCARD);
	if (op == REQ_OP_FLUSH)
		what |= BLK_TC_ACT(BLK_TC_FLUSH);
	if (cgid)
		what |= __BLK_TA_CGROUP;

	pid = tsk->pid;
	if (act_log_check(bt, what, sector, pid))
		return;
	cpu = raw_smp_processor_id();

	if (blk_tracer) {
		tracing_record_cmdline(current);

		buffer = blk_tr->array_buffer.buffer;
		trace_ctx = tracing_gen_ctx_flags(0);
		event = trace_buffer_lock_reserve(buffer, TRACE_BLK,
						  sizeof(*t) + pdu_len + cgid_len,
						  trace_ctx);
		if (!event)
			return;
		t = ring_buffer_event_data(event);
		goto record_it;
	}

	if (unlikely(tsk->btrace_seq != blktrace_seq))
		trace_note_tsk(tsk);

	/*
	 * A word about the locking here - we disable interrupts to reserve
	 * some space in the relay per-cpu buffer, to prevent an irq
	 * from coming in and stepping on our toes.
	 */
	local_irq_save(flags);
	t = relay_reserve(bt->rchan, sizeof(*t) + pdu_len + cgid_len);
	if (t) {
		sequence = per_cpu_ptr(bt->sequence, cpu);

		t->magic = BLK_IO_TRACE_MAGIC | BLK_IO_TRACE_VERSION;
		t->sequence = ++(*sequence);
		t->time = ktime_to_ns(ktime_get());
record_it:
		/*
		 * These two are not needed in ftrace as they are in the
		 * generic trace_entry, filled by tracing_generic_entry_update,
		 * but for the trace_event->bin() synthesizer benefit we do it
		 * here too.
		 */
		t->cpu = cpu;
		t->pid = pid;

		t->sector = sector;
		t->bytes = bytes;
		t->action = what;
		t->device = bt->dev;
		t->error = error;
		t->pdu_len = pdu_len + cgid_len;

		if (cgid_len)
			memcpy((void *)t + sizeof(*t), &cgid, cgid_len);
		if (pdu_len)
			memcpy((void *)t + sizeof(*t) + cgid_len, pdu_data, pdu_len);

		if (blk_tracer) {
			trace_buffer_unlock_commit(blk_tr, buffer, event, trace_ctx);
			return;
		}
	}

	local_irq_restore(flags);
}

static void blk_trace_free(struct request_queue *q, struct blk_trace *bt)
{
	relay_close(bt->rchan);

	/*
	 * If 'bt->dir' is not set, then both 'dropped' and 'msg' are created
	 * under 'q->debugfs_dir', thus lookup and remove them.
	 */
	if (!bt->dir) {
		debugfs_remove(debugfs_lookup("dropped", q->debugfs_dir));
		debugfs_remove(debugfs_lookup("msg", q->debugfs_dir));
	} else {
		debugfs_remove(bt->dir);
	}
	free_percpu(bt->sequence);
	free_percpu(bt->msg_data);
	kfree(bt);
}

static void get_probe_ref(void)
{
	mutex_lock(&blk_probe_mutex);
	if (++blk_probes_ref == 1)
		blk_register_tracepoints();
	mutex_unlock(&blk_probe_mutex);
}

static void put_probe_ref(void)
{
	mutex_lock(&blk_probe_mutex);
	if (!--blk_probes_ref)
		blk_unregister_tracepoints();
	mutex_unlock(&blk_probe_mutex);
}

static void blk_trace_cleanup(struct request_queue *q, struct blk_trace *bt)
{
	synchronize_rcu();
	blk_trace_free(q, bt);
	put_probe_ref();
}

static int __blk_trace_remove(struct request_queue *q)
{
	struct blk_trace *bt;

	bt = rcu_replace_pointer(q->blk_trace, NULL,
				 lockdep_is_held(&q->debugfs_mutex));
	if (!bt)
		return -EINVAL;

	if (bt->trace_state != Blktrace_running)
		blk_trace_cleanup(q, bt);

	return 0;
}

int blk_trace_remove(struct request_queue *q)
{
	int ret;

	mutex_lock(&q->debugfs_mutex);
	ret = __blk_trace_remove(q);
	mutex_unlock(&q->debugfs_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(blk_trace_remove);

static ssize_t blk_dropped_read(struct file *filp, char __user *buffer,
				size_t count, loff_t *ppos)
{
	struct blk_trace *bt = filp->private_data;
	char buf[16];

	snprintf(buf, sizeof(buf), "%u\n", atomic_read(&bt->dropped));

	return simple_read_from_buffer(buffer, count, ppos, buf, strlen(buf));
}

static const struct file_operations blk_dropped_fops = {
	.owner =	THIS_MODULE,
	.open =		simple_open,
	.read =		blk_dropped_read,
	.llseek =	default_llseek,
};

static ssize_t blk_msg_write(struct file *filp, const char __user *buffer,
				size_t count, loff_t *ppos)
{
	char *msg;
	struct blk_trace *bt;

	if (count >= BLK_TN_MAX_MSG)
		return -EINVAL;

	msg = memdup_user_nul(buffer, count);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	bt = filp->private_data;
	__blk_trace_note_message(bt, NULL, "%s", msg);
	kfree(msg);

	return count;
}

static const struct file_operations blk_msg_fops = {
	.owner =	THIS_MODULE,
	.open =		simple_open,
	.write =	blk_msg_write,
	.llseek =	noop_llseek,
};

/*
 * Keep track of how many times we encountered a full subbuffer, to aid
 * the user space app in telling how many lost events there were.
 */
static int blk_subbuf_start_callback(struct rchan_buf *buf, void *subbuf,
				     void *prev_subbuf, size_t prev_padding)
{
	struct blk_trace *bt;

	if (!relay_buf_full(buf))
		return 1;

	bt = buf->chan->private_data;
	atomic_inc(&bt->dropped);
	return 0;
}

static int blk_remove_buf_file_callback(struct dentry *dentry)
{
	debugfs_remove(dentry);

	return 0;
}

static struct dentry *blk_create_buf_file_callback(const char *filename,
						   struct dentry *parent,
						   umode_t mode,
						   struct rchan_buf *buf,
						   int *is_global)
{
	return debugfs_create_file(filename, mode, parent, buf,
					&relay_file_operations);
}

static const struct rchan_callbacks blk_relay_callbacks = {
	.subbuf_start		= blk_subbuf_start_callback,
	.create_buf_file	= blk_create_buf_file_callback,
	.remove_buf_file	= blk_remove_buf_file_callback,
};

static void blk_trace_setup_lba(struct blk_trace *bt,
				struct block_device *bdev)
{
	if (bdev) {
		bt->start_lba = bdev->bd_start_sect;
		bt->end_lba = bdev->bd_start_sect + bdev_nr_sectors(bdev);
	} else {
		bt->start_lba = 0;
		bt->end_lba = -1ULL;
	}
}

/*
 * Setup everything required to start tracing
 */
static int do_blk_trace_setup(struct request_queue *q, char *name, dev_t dev,
			      struct block_device *bdev,
			      struct blk_user_trace_setup *buts)
{
	struct blk_trace *bt = NULL;
	struct dentry *dir = NULL;
	int ret;

	lockdep_assert_held(&q->debugfs_mutex);

	if (!buts->buf_size || !buts->buf_nr)
		return -EINVAL;

	strncpy(buts->name, name, BLKTRACE_BDEV_SIZE);
	buts->name[BLKTRACE_BDEV_SIZE - 1] = '\0';

	/*
	 * some device names have larger paths - convert the slashes
	 * to underscores for this to work as expected
	 */
	strreplace(buts->name, '/', '_');

	/*
	 * bdev can be NULL, as with scsi-generic, this is a helpful as
	 * we can be.
	 */
	if (rcu_dereference_protected(q->blk_trace,
				      lockdep_is_held(&q->debugfs_mutex))) {
		pr_warn("Concurrent blktraces are not allowed on %s\n",
			buts->name);
		return -EBUSY;
	}

	bt = kzalloc(sizeof(*bt), GFP_KERNEL);
	if (!bt)
		return -ENOMEM;

	ret = -ENOMEM;
	bt->sequence = alloc_percpu(unsigned long);
	if (!bt->sequence)
		goto err;

	bt->msg_data = __alloc_percpu(BLK_TN_MAX_MSG, __alignof__(char));
	if (!bt->msg_data)
		goto err;

	/*
	 * When tracing the whole disk reuse the existing debugfs directory
	 * created by the block layer on init. For partitions block devices,
	 * and scsi-generic block devices we create a temporary new debugfs
	 * directory that will be removed once the trace ends.
	 */
	if (bdev && !bdev_is_partition(bdev))
		dir = q->debugfs_dir;
	else
		bt->dir = dir = debugfs_create_dir(buts->name, blk_debugfs_root);

	/*
	 * As blktrace relies on debugfs for its interface the debugfs directory
	 * is required, contrary to the usual mantra of not checking for debugfs
	 * files or directories.
	 */
	if (IS_ERR_OR_NULL(dir)) {
		pr_warn("debugfs_dir not present for %s so skipping\n",
			buts->name);
		ret = -ENOENT;
		goto err;
	}

	bt->dev = dev;
	atomic_set(&bt->dropped, 0);
	INIT_LIST_HEAD(&bt->running_list);

	ret = -EIO;
	debugfs_create_file("dropped", 0444, dir, bt, &blk_dropped_fops);
	debugfs_create_file("msg", 0222, dir, bt, &blk_msg_fops);

	bt->rchan = relay_open("trace", dir, buts->buf_size,
				buts->buf_nr, &blk_relay_callbacks, bt);
	if (!bt->rchan)
		goto err;

	bt->act_mask = buts->act_mask;
	if (!bt->act_mask)
		bt->act_mask = (u16) -1;

	blk_trace_setup_lba(bt, bdev);

	/* overwrite with user settings */
	if (buts->start_lba)
		bt->start_lba = buts->start_lba;
	if (buts->end_lba)
		bt->end_lba = buts->end_lba;

	bt->pid = buts->pid;
	bt->trace_state = Blktrace_setup;

	rcu_assign_pointer(q->blk_trace, bt);
	get_probe_ref();

	ret = 0;
err:
	if (ret)
		blk_trace_free(q, bt);
	return ret;
}

static int __blk_trace_setup(struct request_queue *q, char *name, dev_t dev,
			     struct block_device *bdev, char __user *arg)
{
	struct blk_user_trace_setup buts;
	int ret;

	ret = copy_from_user(&buts, arg, sizeof(buts));
	if (ret)
		return -EFAULT;

	ret = do_blk_trace_setup(q, name, dev, bdev, &buts);
	if (ret)
		return ret;

	if (copy_to_user(arg, &buts, sizeof(buts))) {
		__blk_trace_remove(q);
		return -EFAULT;
	}
	return 0;
}

int blk_trace_setup(struct request_queue *q, char *name, dev_t dev,
		    struct block_device *bdev,
		    char __user *arg)
{
	int ret;

	mutex_lock(&q->debugfs_mutex);
	ret = __blk_trace_setup(q, name, dev, bdev, arg);
	mutex_unlock(&q->debugfs_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(blk_trace_setup);

#if defined(CONFIG_COMPAT) && defined(CONFIG_X86_64)
static int compat_blk_trace_setup(struct request_queue *q, char *name,
				  dev_t dev, struct block_device *bdev,
				  char __user *arg)
{
	struct blk_user_trace_setup buts;
	struct compat_blk_user_trace_setup cbuts;
	int ret;

	if (copy_from_user(&cbuts, arg, sizeof(cbuts)))
		return -EFAULT;

	buts = (struct blk_user_trace_setup) {
		.act_mask = cbuts.act_mask,
		.buf_size = cbuts.buf_size,
		.buf_nr = cbuts.buf_nr,
		.start_lba = cbuts.start_lba,
		.end_lba = cbuts.end_lba,
		.pid = cbuts.pid,
	};

	ret = do_blk_trace_setup(q, name, dev, bdev, &buts);
	if (ret)
		return ret;

	if (copy_to_user(arg, &buts.name, ARRAY_SIZE(buts.name))) {
		__blk_trace_remove(q);
		return -EFAULT;
	}

	return 0;
}
#endif

static int __blk_trace_startstop(struct request_queue *q, int start)
{
	int ret;
	struct blk_trace *bt;

	bt = rcu_dereference_protected(q->blk_trace,
				       lockdep_is_held(&q->debugfs_mutex));
	if (bt == NULL)
		return -EINVAL;

	/*
	 * For starting a trace, we can transition from a setup or stopped
	 * trace. For stopping a trace, the state must be running
	 */
	ret = -EINVAL;
	if (start) {
		if (bt->trace_state == Blktrace_setup ||
		    bt->trace_state == Blktrace_stopped) {
			blktrace_seq++;
			smp_mb();
			bt->trace_state = Blktrace_running;
			raw_spin_lock_irq(&running_trace_lock);
			list_add(&bt->running_list, &running_trace_list);
			raw_spin_unlock_irq(&running_trace_lock);

			trace_note_time(bt);
			ret = 0;
		}
	} else {
		if (bt->trace_state == Blktrace_running) {
			bt->trace_state = Blktrace_stopped;
			raw_spin_lock_irq(&running_trace_lock);
			list_del_init(&bt->running_list);
			raw_spin_unlock_irq(&running_trace_lock);
			relay_flush(bt->rchan);
			ret = 0;
		}
	}

	return ret;
}

int blk_trace_startstop(struct request_queue *q, int start)
{
	int ret;

	mutex_lock(&q->debugfs_mutex);
	ret = __blk_trace_startstop(q, start);
	mutex_unlock(&q->debugfs_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(blk_trace_startstop);

/*
 * When reading or writing the blktrace sysfs files, the references to the
 * opened sysfs or device files should prevent the underlying block device
 * from being removed. So no further delete protection is really needed.
 */

/**
 * blk_trace_ioctl: - handle the ioctls associated with tracing
 * @bdev:	the block device
 * @cmd:	the ioctl cmd
 * @arg:	the argument data, if any
 *
 **/
int blk_trace_ioctl(struct block_device *bdev, unsigned cmd, char __user *arg)
{
	struct request_queue *q;
	int ret, start = 0;
	char b[BDEVNAME_SIZE];

	q = bdev_get_queue(bdev);
	if (!q)
		return -ENXIO;

	mutex_lock(&q->debugfs_mutex);

	switch (cmd) {
	case BLKTRACESETUP:
		bdevname(bdev, b);
		ret = __blk_trace_setup(q, b, bdev->bd_dev, bdev, arg);
		break;
#if defined(CONFIG_COMPAT) && defined(CONFIG_X86_64)
	case BLKTRACESETUP32:
		bdevname(bdev, b);
		ret = compat_blk_trace_setup(q, b, bdev->bd_dev, bdev, arg);
		break;
#endif
	case BLKTRACESTART:
		start = 1;
		fallthrough;
	case BLKTRACESTOP:
		ret = __blk_trace_startstop(q, start);
		break;
	case BLKTRACETEARDOWN:
		ret = __blk_trace_remove(q);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	mutex_unlock(&q->debugfs_mutex);
	return ret;
}

/**
 * blk_trace_shutdown: - stop and cleanup trace structures
 * @q:    the request queue associated with the device
 *
 **/
void blk_trace_shutdown(struct request_queue *q)
{
	if (rcu_dereference_protected(q->blk_trace,
				      lockdep_is_held(&q->debugfs_mutex))) {
		__blk_trace_startstop(q, 0);
		__blk_trace_remove(q);
	}
}

#ifdef CONFIG_BLK_CGROUP
static u64 blk_trace_bio_get_cgid(struct request_queue *q, struct bio *bio)
{
	struct cgroup_subsys_state *blkcg_css;
	struct blk_trace *bt;

	/* We don't use the 'bt' value here except as an optimization... */
	bt = rcu_dereference_protected(q->blk_trace, 1);
	if (!bt || !(blk_tracer_flags.val & TRACE_BLK_OPT_CGROUP))
		return 0;

	blkcg_css = bio_blkcg_css(bio);
	if (!blkcg_css)
		return 0;
	return cgroup_id(blkcg_css->cgroup);
}
#else
static u64 blk_trace_bio_get_cgid(struct request_queue *q, struct bio *bio)
{
	return 0;
}
#endif

static u64
blk_trace_request_get_cgid(struct request *rq)
{
	if (!rq->bio)
		return 0;
	/* Use the first bio */
	return blk_trace_bio_get_cgid(rq->q, rq->bio);
}

/*
 * blktrace probes
 */

/**
 * blk_add_trace_rq - Add a trace for a request oriented action
 * @rq:		the source request
 * @error:	return status to log
 * @nr_bytes:	number of completed bytes
 * @what:	the action
 * @cgid:	the cgroup info
 *
 * Description:
 *     Records an action against a request. Will log the bio offset + size.
 *
 **/
static void blk_add_trace_rq(struct request *rq, blk_status_t error,
			     unsigned int nr_bytes, u32 what, u64 cgid)
{
	struct blk_trace *bt;

	rcu_read_lock();
	bt = rcu_dereference(rq->q->blk_trace);
	if (likely(!bt)) {
		rcu_read_unlock();
		return;
	}

	if (blk_rq_is_passthrough(rq))
		what |= BLK_TC_ACT(BLK_TC_PC);
	else
		what |= BLK_TC_ACT(BLK_TC_FS);

	__blk_add_trace(bt, blk_rq_trace_sector(rq), nr_bytes, req_op(rq),
			rq->cmd_flags, what, blk_status_to_errno(error), 0,
			NULL, cgid);
	rcu_read_unlock();
}

static void blk_add_trace_rq_insert(void *ignore, struct request *rq)
{
	blk_add_trace_rq(rq, 0, blk_rq_bytes(rq), BLK_TA_INSERT,
			 blk_trace_request_get_cgid(rq));
}

static void blk_add_trace_rq_issue(void *ignore, struct request *rq)
{
	blk_add_trace_rq(rq, 0, blk_rq_bytes(rq), BLK_TA_ISSUE,
			 blk_trace_request_get_cgid(rq));
}

static void blk_add_trace_rq_merge(void *ignore, struct request *rq)
{
	blk_add_trace_rq(rq, 0, blk_rq_bytes(rq), BLK_TA_BACKMERGE,
			 blk_trace_request_get_cgid(rq));
}

static void blk_add_trace_rq_requeue(void *ignore, struct request *rq)
{
	blk_add_trace_rq(rq, 0, blk_rq_bytes(rq), BLK_TA_REQUEUE,
			 blk_trace_request_get_cgid(rq));
}

static void blk_add_trace_rq_complete(void *ignore, struct request *rq,
			blk_status_t error, unsigned int nr_bytes)
{
	blk_add_trace_rq(rq, error, nr_bytes, BLK_TA_COMPLETE,
			 blk_trace_request_get_cgid(rq));
}

/**
 * blk_add_trace_bio - Add a trace for a bio oriented action
 * @q:		queue the io is for
 * @bio:	the source bio
 * @what:	the action
 * @error:	error, if any
 *
 * Description:
 *     Records an action against a bio. Will log the bio offset + size.
 *
 **/
static void blk_add_trace_bio(struct request_queue *q, struct bio *bio,
			      u32 what, int error)
{
	struct blk_trace *bt;

	rcu_read_lock();
	bt = rcu_dereference(q->blk_trace);
	if (likely(!bt)) {
		rcu_read_unlock();
		return;
	}

	__blk_add_trace(bt, bio->bi_iter.bi_sector, bio->bi_iter.bi_size,
			bio_op(bio), bio->bi_opf, what, error, 0, NULL,
			blk_trace_bio_get_cgid(q, bio));
	rcu_read_unlock();
}

static void blk_add_trace_bio_bounce(void *ignore, struct bio *bio)
{
	blk_add_trace_bio(bio->bi_bdev->bd_disk->queue, bio, BLK_TA_BOUNCE, 0);
}

static void blk_add_trace_bio_complete(void *ignore,
				       struct request_queue *q, struct bio *bio)
{
	blk_add_trace_bio(q, bio, BLK_TA_COMPLETE,
			  blk_status_to_errno(bio->bi_status));
}

static void blk_add_trace_bio_backmerge(void *ignore, struct bio *bio)
{
	blk_add_trace_bio(bio->bi_bdev->bd_disk->queue, bio, BLK_TA_BACKMERGE,
			0);
}

static void blk_add_trace_bio_frontmerge(void *ignore, struct bio *bio)
{
	blk_add_trace_bio(bio->bi_bdev->bd_disk->queue, bio, BLK_TA_FRONTMERGE,
			0);
}

static void blk_add_trace_bio_queue(void *ignore, struct bio *bio)
{
	blk_add_trace_bio(bio->bi_bdev->bd_disk->queue, bio, BLK_TA_QUEUE, 0);
}

static void blk_add_trace_getrq(void *ignore, struct bio *bio)
{
	blk_add_trace_bio(bio->bi_bdev->bd_disk->queue, bio, BLK_TA_GETRQ, 0);
}

static void blk_add_trace_plug(void *ignore, struct request_queue *q)
{
	struct blk_trace *bt;

	rcu_read_lock();
	bt = rcu_dereference(q->blk_trace);
	if (bt)
		__blk_add_trace(bt, 0, 0, 0, 0, BLK_TA_PLUG, 0, 0, NULL, 0);
	rcu_read_unlock();
}

static void blk_add_trace_unplug(void *ignore, struct request_queue *q,
				    unsigned int depth, bool explicit)
{
	struct blk_trace *bt;

	rcu_read_lock();
	bt = rcu_dereference(q->blk_trace);
	if (bt) {
		__be64 rpdu = cpu_to_be64(depth);
		u32 what;

		if (explicit)
			what = BLK_TA_UNPLUG_IO;
		else
			what = BLK_TA_UNPLUG_TIMER;

		__blk_add_trace(bt, 0, 0, 0, 0, what, 0, sizeof(rpdu), &rpdu, 0);
	}
	rcu_read_unlock();
}

static void blk_add_trace_split(void *ignore, struct bio *bio, unsigned int pdu)
{
	struct request_queue *q = bio->bi_bdev->bd_disk->queue;
	struct blk_trace *bt;

	rcu_read_lock();
	bt = rcu_dereference(q->blk_trace);
	if (bt) {
		__be64 rpdu = cpu_to_be64(pdu);

		__blk_add_trace(bt, bio->bi_iter.bi_sector,
				bio->bi_iter.bi_size, bio_op(bio), bio->bi_opf,
				BLK_TA_SPLIT,
				blk_status_to_errno(bio->bi_status),
				sizeof(rpdu), &rpdu,
				blk_trace_bio_get_cgid(q, bio));
	}
	rcu_read_unlock();
}

/**
 * blk_add_trace_bio_remap - Add a trace for a bio-remap operation
 * @ignore:	trace callback data parameter (not used)
 * @bio:	the source bio
 * @dev:	source device
 * @from:	source sector
 *
 * Called after a bio is remapped to a different device and/or sector.
 **/
static void blk_add_trace_bio_remap(void *ignore, struct bio *bio, dev_t dev,
				    sector_t from)
{
	struct request_queue *q = bio->bi_bdev->bd_disk->queue;
	struct blk_trace *bt;
	struct blk_io_trace_remap r;

	rcu_read_lock();
	bt = rcu_dereference(q->blk_trace);
	if (likely(!bt)) {
		rcu_read_unlock();
		return;
	}

	r.device_from = cpu_to_be32(dev);
	r.device_to   = cpu_to_be32(bio_dev(bio));
	r.sector_from = cpu_to_be64(from);

	__blk_add_trace(bt, bio->bi_iter.bi_sector, bio->bi_iter.bi_size,
			bio_op(bio), bio->bi_opf, BLK_TA_REMAP,
			blk_status_to_errno(bio->bi_status),
			sizeof(r), &r, blk_trace_bio_get_cgid(q, bio));
	rcu_read_unlock();
}

/**
 * blk_add_trace_rq_remap - Add a trace for a request-remap operation
 * @ignore:	trace callback data parameter (not used)
 * @rq:		the source request
 * @dev:	target device
 * @from:	source sector
 *
 * Description:
 *     Device mapper remaps request to other devices.
 *     Add a trace for that action.
 *
 **/
static void blk_add_trace_rq_remap(void *ignore, struct request *rq, dev_t dev,
				   sector_t from)
{
	struct blk_trace *bt;
	struct blk_io_trace_remap r;

	rcu_read_lock();
	bt = rcu_dereference(rq->q->blk_trace);
	if (likely(!bt)) {
		rcu_read_unlock();
		return;
	}

	r.device_from = cpu_to_be32(dev);
	r.device_to   = cpu_to_be32(disk_devt(rq->q->disk));
	r.sector_from = cpu_to_be64(from);

	__blk_add_trace(bt, blk_rq_pos(rq), blk_rq_bytes(rq),
			rq_data_dir(rq), 0, BLK_TA_REMAP, 0,
			sizeof(r), &r, blk_trace_request_get_cgid(rq));
	rcu_read_unlock();
}

/**
 * blk_add_driver_data - Add binary message with driver-specific data
 * @rq:		io request
 * @data:	driver-specific data
 * @len:	length of driver-specific data
 *
 * Description:
 *     Some drivers might want to write driver-specific data per request.
 *
 **/
void blk_add_driver_data(struct request *rq, void *data, size_t len)
{
	struct blk_trace *bt;

	rcu_read_lock();
	bt = rcu_dereference(rq->q->blk_trace);
	if (likely(!bt)) {
		rcu_read_unlock();
		return;
	}

	__blk_add_trace(bt, blk_rq_trace_sector(rq), blk_rq_bytes(rq), 0, 0,
				BLK_TA_DRV_DATA, 0, len, data,
				blk_trace_request_get_cgid(rq));
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(blk_add_driver_data);

static void blk_register_tracepoints(void)
{
	int ret;

	ret = register_trace_block_rq_insert(blk_add_trace_rq_insert, NULL);
	WARN_ON(ret);
	ret = register_trace_block_rq_issue(blk_add_trace_rq_issue, NULL);
	WARN_ON(ret);
	ret = register_trace_block_rq_merge(blk_add_trace_rq_merge, NULL);
	WARN_ON(ret);
	ret = register_trace_block_rq_requeue(blk_add_trace_rq_requeue, NULL);
	WARN_ON(ret);
	ret = register_trace_block_rq_complete(blk_add_trace_rq_complete, NULL);
	WARN_ON(ret);
	ret = register_trace_block_bio_bounce(blk_add_trace_bio_bounce, NULL);
	WARN_ON(ret);
	ret = register_trace_block_bio_complete(blk_add_trace_bio_complete, NULL);
	WARN_ON(ret);
	ret = register_trace_block_bio_backmerge(blk_add_trace_bio_backmerge, NULL);
	WARN_ON(ret);
	ret = register_trace_block_bio_frontmerge(blk_add_trace_bio_frontmerge, NULL);
	WARN_ON(ret);
	ret = register_trace_block_bio_queue(blk_add_trace_bio_queue, NULL);
	WARN_ON(ret);
	ret = register_trace_block_getrq(blk_add_trace_getrq, NULL);
	WARN_ON(ret);
	ret = register_trace_block_plug(blk_add_trace_plug, NULL);
	WARN_ON(ret);
	ret = register_trace_block_unplug(blk_add_trace_unplug, NULL);
	WARN_ON(ret);
	ret = register_trace_block_split(blk_add_trace_split, NULL);
	WARN_ON(ret);
	ret = register_trace_block_bio_remap(blk_add_trace_bio_remap, NULL);
	WARN_ON(ret);
	ret = register_trace_block_rq_remap(blk_add_trace_rq_remap, NULL);
	WARN_ON(ret);
}

static void blk_unregister_tracepoints(void)
{
	unregister_trace_block_rq_remap(blk_add_trace_rq_remap, NULL);
	unregister_trace_block_bio_remap(blk_add_trace_bio_remap, NULL);
	unregister_trace_block_split(blk_add_trace_split, NULL);
	unregister_trace_block_unplug(blk_add_trace_unplug, NULL);
	unregister_trace_block_plug(blk_add_trace_plug, NULL);
	unregister_trace_block_getrq(blk_add_trace_getrq, NULL);
	unregister_trace_block_bio_queue(blk_add_trace_bio_queue, NULL);
	unregister_trace_block_bio_frontmerge(blk_add_trace_bio_frontmerge, NULL);
	unregister_trace_block_bio_backmerge(blk_add_trace_bio_backmerge, NULL);
	unregister_trace_block_bio_complete(blk_add_trace_bio_complete, NULL);
	unregister_trace_block_bio_bounce(blk_add_trace_bio_bounce, NULL);
	unregister_trace_block_rq_complete(blk_add_trace_rq_complete, NULL);
	unregister_trace_block_rq_requeue(blk_add_trace_rq_requeue, NULL);
	unregister_trace_block_rq_merge(blk_add_trace_rq_merge, NULL);
	unregister_trace_block_rq_issue(blk_add_trace_rq_issue, NULL);
	unregister_trace_block_rq_insert(blk_add_trace_rq_insert, NULL);

	tracepoint_synchronize_unregister();
}

/*
 * struct blk_io_tracer formatting routines
 */

static void fill_rwbs(char *rwbs, const struct blk_io_trace *t)
{
	int i = 0;
	int tc = t->action >> BLK_TC_SHIFT;

	if ((t->action & ~__BLK_TN_CGROUP) == BLK_TN_MESSAGE) {
		rwbs[i++] = 'N';
		goto out;
	}

	if (tc & BLK_TC_FLUSH)
		rwbs[i++] = 'F';

	if (tc & BLK_TC_DISCARD)
		rwbs[i++] = 'D';
	else if (tc & BLK_TC_WRITE)
		rwbs[i++] = 'W';
	else if (t->bytes)
		rwbs[i++] = 'R';
	else
		rwbs[i++] = 'N';

	if (tc & BLK_TC_FUA)
		rwbs[i++] = 'F';
	if (tc & BLK_TC_AHEAD)
		rwbs[i++] = 'A';
	if (tc & BLK_TC_SYNC)
		rwbs[i++] = 'S';
	if (tc & BLK_TC_META)
		rwbs[i++] = 'M';
out:
	rwbs[i] = '\0';
}

static inline
const struct blk_io_trace *te_blk_io_trace(const struct trace_entry *ent)
{
	return (const struct blk_io_trace *)ent;
}

static inline const void *pdu_start(const struct trace_entry *ent, bool has_cg)
{
	return (void *)(te_blk_io_trace(ent) + 1) + (has_cg ? sizeof(u64) : 0);
}

static inline u64 t_cgid(const struct trace_entry *ent)
{
	return *(u64 *)(te_blk_io_trace(ent) + 1);
}

static inline int pdu_real_len(const struct trace_entry *ent, bool has_cg)
{
	return te_blk_io_trace(ent)->pdu_len - (has_cg ? sizeof(u64) : 0);
}

static inline u32 t_action(const struct trace_entry *ent)
{
	return te_blk_io_trace(ent)->action;
}

static inline u32 t_bytes(const struct trace_entry *ent)
{
	return te_blk_io_trace(ent)->bytes;
}

static inline u32 t_sec(const struct trace_entry *ent)
{
	return te_blk_io_trace(ent)->bytes >> 9;
}

static inline unsigned long long t_sector(const struct trace_entry *ent)
{
	return te_blk_io_trace(ent)->sector;
}

static inline __u16 t_error(const struct trace_entry *ent)
{
	return te_blk_io_trace(ent)->error;
}

static __u64 get_pdu_int(const struct trace_entry *ent, bool has_cg)
{
	const __be64 *val = pdu_start(ent, has_cg);
	return be64_to_cpu(*val);
}

typedef void (blk_log_action_t) (struct trace_iterator *iter, const char *act,
	bool has_cg);

static void blk_log_action_classic(struct trace_iterator *iter, const char *act,
	bool has_cg)
{
	char rwbs[RWBS_LEN];
	unsigned long long ts  = iter->ts;
	unsigned long nsec_rem = do_div(ts, NSEC_PER_SEC);
	unsigned secs	       = (unsigned long)ts;
	const struct blk_io_trace *t = te_blk_io_trace(iter->ent);

	fill_rwbs(rwbs, t);

	trace_seq_printf(&iter->seq,
			 "%3d,%-3d %2d %5d.%09lu %5u %2s %3s ",
			 MAJOR(t->device), MINOR(t->device), iter->cpu,
			 secs, nsec_rem, iter->ent->pid, act, rwbs);
}

static void blk_log_action(struct trace_iterator *iter, const char *act,
	bool has_cg)
{
	char rwbs[RWBS_LEN];
	const struct blk_io_trace *t = te_blk_io_trace(iter->ent);

	fill_rwbs(rwbs, t);
	if (has_cg) {
		u64 id = t_cgid(iter->ent);

		if (blk_tracer_flags.val & TRACE_BLK_OPT_CGNAME) {
			char blkcg_name_buf[NAME_MAX + 1] = "<...>";

			cgroup_path_from_kernfs_id(id, blkcg_name_buf,
				sizeof(blkcg_name_buf));
			trace_seq_printf(&iter->seq, "%3d,%-3d %s %2s %3s ",
				 MAJOR(t->device), MINOR(t->device),
				 blkcg_name_buf, act, rwbs);
		} else {
			/*
			 * The cgid portion used to be "INO,GEN".  Userland
			 * builds a FILEID_INO32_GEN fid out of them and
			 * opens the cgroup using open_by_handle_at(2).
			 * While 32bit ino setups are still the same, 64bit
			 * ones now use the 64bit ino as the whole ID and
			 * no longer use generation.
			 *
			 * Regardless of the content, always output
			 * "LOW32,HIGH32" so that FILEID_INO32_GEN fid can
			 * be mapped back to @id on both 64 and 32bit ino
			 * setups.  See __kernfs_fh_to_dentry().
			 */
			trace_seq_printf(&iter->seq,
				 "%3d,%-3d %llx,%-llx %2s %3s ",
				 MAJOR(t->device), MINOR(t->device),
				 id & U32_MAX, id >> 32, act, rwbs);
		}
	} else
		trace_seq_printf(&iter->seq, "%3d,%-3d %2s %3s ",
				 MAJOR(t->device), MINOR(t->device), act, rwbs);
}

static void blk_log_dump_pdu(struct trace_seq *s,
	const struct trace_entry *ent, bool has_cg)
{
	const unsigned char *pdu_buf;
	int pdu_len;
	int i, end;

	pdu_buf = pdu_start(ent, has_cg);
	pdu_len = pdu_real_len(ent, has_cg);

	if (!pdu_len)
		return;

	/* find the last zero that needs to be printed */
	for (end = pdu_len - 1; end >= 0; end--)
		if (pdu_buf[end])
			break;
	end++;

	trace_seq_putc(s, '(');

	for (i = 0; i < pdu_len; i++) {

		trace_seq_printf(s, "%s%02x",
				 i == 0 ? "" : " ", pdu_buf[i]);

		/*
		 * stop when the rest is just zeros and indicate so
		 * with a ".." appended
		 */
		if (i == end && end != pdu_len - 1) {
			trace_seq_puts(s, " ..) ");
			return;
		}
	}

	trace_seq_puts(s, ") ");
}

static void blk_log_generic(struct trace_seq *s, const struct trace_entry *ent, bool has_cg)
{
	char cmd[TASK_COMM_LEN];

	trace_find_cmdline(ent->pid, cmd);

	if (t_action(ent) & BLK_TC_ACT(BLK_TC_PC)) {
		trace_seq_printf(s, "%u ", t_bytes(ent));
		blk_log_dump_pdu(s, ent, has_cg);
		trace_seq_printf(s, "[%s]\n", cmd);
	} else {
		if (t_sec(ent))
			trace_seq_printf(s, "%llu + %u [%s]\n",
						t_sector(ent), t_sec(ent), cmd);
		else
			trace_seq_printf(s, "[%s]\n", cmd);
	}
}

static void blk_log_with_error(struct trace_seq *s,
			      const struct trace_entry *ent, bool has_cg)
{
	if (t_action(ent) & BLK_TC_ACT(BLK_TC_PC)) {
		blk_log_dump_pdu(s, ent, has_cg);
		trace_seq_printf(s, "[%d]\n", t_error(ent));
	} else {
		if (t_sec(ent))
			trace_seq_printf(s, "%llu + %u [%d]\n",
					 t_sector(ent),
					 t_sec(ent), t_error(ent));
		else
			trace_seq_printf(s, "%llu [%d]\n",
					 t_sector(ent), t_error(ent));
	}
}

static void blk_log_remap(struct trace_seq *s, const struct trace_entry *ent, bool has_cg)
{
	const struct blk_io_trace_remap *__r = pdu_start(ent, has_cg);

	trace_seq_printf(s, "%llu + %u <- (%d,%d) %llu\n",
			 t_sector(ent), t_sec(ent),
			 MAJOR(be32_to_cpu(__r->device_from)),
			 MINOR(be32_to_cpu(__r->device_from)),
			 be64_to_cpu(__r->sector_from));
}

static void blk_log_plug(struct trace_seq *s, const struct trace_entry *ent, bool has_cg)
{
	char cmd[TASK_COMM_LEN];

	trace_find_cmdline(ent->pid, cmd);

	trace_seq_printf(s, "[%s]\n", cmd);
}

static void blk_log_unplug(struct trace_seq *s, const struct trace_entry *ent, bool has_cg)
{
	char cmd[TASK_COMM_LEN];

	trace_find_cmdline(ent->pid, cmd);

	trace_seq_printf(s, "[%s] %llu\n", cmd, get_pdu_int(ent, has_cg));
}

static void blk_log_split(struct trace_seq *s, const struct trace_entry *ent, bool has_cg)
{
	char cmd[TASK_COMM_LEN];

	trace_find_cmdline(ent->pid, cmd);

	trace_seq_printf(s, "%llu / %llu [%s]\n", t_sector(ent),
			 get_pdu_int(ent, has_cg), cmd);
}

static void blk_log_msg(struct trace_seq *s, const struct trace_entry *ent,
			bool has_cg)
{

	trace_seq_putmem(s, pdu_start(ent, has_cg),
		pdu_real_len(ent, has_cg));
	trace_seq_putc(s, '\n');
}

/*
 * struct tracer operations
 */

static void blk_tracer_print_header(struct seq_file *m)
{
	if (!(blk_tracer_flags.val & TRACE_BLK_OPT_CLASSIC))
		return;
	seq_puts(m, "# DEV   CPU TIMESTAMP     PID ACT FLG\n"
		    "#  |     |     |           |   |   |\n");
}

static void blk_tracer_start(struct trace_array *tr)
{
	blk_tracer_enabled = true;
}

static int blk_tracer_init(struct trace_array *tr)
{
	blk_tr = tr;
	blk_tracer_start(tr);
	return 0;
}

static void blk_tracer_stop(struct trace_array *tr)
{
	blk_tracer_enabled = false;
}

static void blk_tracer_reset(struct trace_array *tr)
{
	blk_tracer_stop(tr);
}

static const struct {
	const char *act[2];
	void	   (*print)(struct trace_seq *s, const struct trace_entry *ent,
			    bool has_cg);
} what2act[] = {
	[__BLK_TA_QUEUE]	= {{  "Q", "queue" },	   blk_log_generic },
	[__BLK_TA_BACKMERGE]	= {{  "M", "backmerge" },  blk_log_generic },
	[__BLK_TA_FRONTMERGE]	= {{  "F", "frontmerge" }, blk_log_generic },
	[__BLK_TA_GETRQ]	= {{  "G", "getrq" },	   blk_log_generic },
	[__BLK_TA_SLEEPRQ]	= {{  "S", "sleeprq" },	   blk_log_generic },
	[__BLK_TA_REQUEUE]	= {{  "R", "requeue" },	   blk_log_with_error },
	[__BLK_TA_ISSUE]	= {{  "D", "issue" },	   blk_log_generic },
	[__BLK_TA_COMPLETE]	= {{  "C", "complete" },   blk_log_with_error },
	[__BLK_TA_PLUG]		= {{  "P", "plug" },	   blk_log_plug },
	[__BLK_TA_UNPLUG_IO]	= {{  "U", "unplug_io" },  blk_log_unplug },
	[__BLK_TA_UNPLUG_TIMER]	= {{ "UT", "unplug_timer" }, blk_log_unplug },
	[__BLK_TA_INSERT]	= {{  "I", "insert" },	   blk_log_generic },
	[__BLK_TA_SPLIT]	= {{  "X", "split" },	   blk_log_split },
	[__BLK_TA_BOUNCE]	= {{  "B", "bounce" },	   blk_log_generic },
	[__BLK_TA_REMAP]	= {{  "A", "remap" },	   blk_log_remap },
};

static enum print_line_t print_one_line(struct trace_iterator *iter,
					bool classic)
{
	struct trace_array *tr = iter->tr;
	struct trace_seq *s = &iter->seq;
	const struct blk_io_trace *t;
	u16 what;
	bool long_act;
	blk_log_action_t *log_action;
	bool has_cg;

	t	   = te_blk_io_trace(iter->ent);
	what	   = (t->action & ((1 << BLK_TC_SHIFT) - 1)) & ~__BLK_TA_CGROUP;
	long_act   = !!(tr->trace_flags & TRACE_ITER_VERBOSE);
	log_action = classic ? &blk_log_action_classic : &blk_log_action;
	has_cg	   = t->action & __BLK_TA_CGROUP;

	if ((t->action & ~__BLK_TN_CGROUP) == BLK_TN_MESSAGE) {
		log_action(iter, long_act ? "message" : "m", has_cg);
		blk_log_msg(s, iter->ent, has_cg);
		return trace_handle_return(s);
	}

	if (unlikely(what == 0 || what >= ARRAY_SIZE(what2act)))
		trace_seq_printf(s, "Unknown action %x\n", what);
	else {
		log_action(iter, what2act[what].act[long_act], has_cg);
		what2act[what].print(s, iter->ent, has_cg);
	}

	return trace_handle_return(s);
}

static enum print_line_t blk_trace_event_print(struct trace_iterator *iter,
					       int flags, struct trace_event *event)
{
	return print_one_line(iter, false);
}

static void blk_trace_synthesize_old_trace(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct blk_io_trace *t = (struct blk_io_trace *)iter->ent;
	const int offset = offsetof(struct blk_io_trace, sector);
	struct blk_io_trace old = {
		.magic	  = BLK_IO_TRACE_MAGIC | BLK_IO_TRACE_VERSION,
		.time     = iter->ts,
	};

	trace_seq_putmem(s, &old, offset);
	trace_seq_putmem(s, &t->sector,
			 sizeof(old) - offset + t->pdu_len);
}

static enum print_line_t
blk_trace_event_print_binary(struct trace_iterator *iter, int flags,
			     struct trace_event *event)
{
	blk_trace_synthesize_old_trace(iter);

	return trace_handle_return(&iter->seq);
}

static enum print_line_t blk_tracer_print_line(struct trace_iterator *iter)
{
	if (!(blk_tracer_flags.val & TRACE_BLK_OPT_CLASSIC))
		return TRACE_TYPE_UNHANDLED;

	return print_one_line(iter, true);
}

static int
blk_tracer_set_flag(struct trace_array *tr, u32 old_flags, u32 bit, int set)
{
	/* don't output context-info for blk_classic output */
	if (bit == TRACE_BLK_OPT_CLASSIC) {
		if (set)
			tr->trace_flags &= ~TRACE_ITER_CONTEXT_INFO;
		else
			tr->trace_flags |= TRACE_ITER_CONTEXT_INFO;
	}
	return 0;
}

static struct tracer blk_tracer __read_mostly = {
	.name		= "blk",
	.init		= blk_tracer_init,
	.reset		= blk_tracer_reset,
	.start		= blk_tracer_start,
	.stop		= blk_tracer_stop,
	.print_header	= blk_tracer_print_header,
	.print_line	= blk_tracer_print_line,
	.flags		= &blk_tracer_flags,
	.set_flag	= blk_tracer_set_flag,
};

static struct trace_event_functions trace_blk_event_funcs = {
	.trace		= blk_trace_event_print,
	.binary		= blk_trace_event_print_binary,
};

static struct trace_event trace_blk_event = {
	.type		= TRACE_BLK,
	.funcs		= &trace_blk_event_funcs,
};

static int __init init_blk_tracer(void)
{
	if (!register_trace_event(&trace_blk_event)) {
		pr_warn("Warning: could not register block events\n");
		return 1;
	}

	if (register_tracer(&blk_tracer) != 0) {
		pr_warn("Warning: could not register the block tracer\n");
		unregister_trace_event(&trace_blk_event);
		return 1;
	}

	return 0;
}

device_initcall(init_blk_tracer);

static int blk_trace_remove_queue(struct request_queue *q)
{
	struct blk_trace *bt;

	bt = rcu_replace_pointer(q->blk_trace, NULL,
				 lockdep_is_held(&q->debugfs_mutex));
	if (bt == NULL)
		return -EINVAL;

	if (bt->trace_state == Blktrace_running) {
		bt->trace_state = Blktrace_stopped;
		raw_spin_lock_irq(&running_trace_lock);
		list_del_init(&bt->running_list);
		raw_spin_unlock_irq(&running_trace_lock);
		relay_flush(bt->rchan);
	}

	put_probe_ref();
	synchronize_rcu();
	blk_trace_free(q, bt);
	return 0;
}

/*
 * Setup everything required to start tracing
 */
static int blk_trace_setup_queue(struct request_queue *q,
				 struct block_device *bdev)
{
	struct blk_trace *bt = NULL;
	int ret = -ENOMEM;

	bt = kzalloc(sizeof(*bt), GFP_KERNEL);
	if (!bt)
		return -ENOMEM;

	bt->msg_data = __alloc_percpu(BLK_TN_MAX_MSG, __alignof__(char));
	if (!bt->msg_data)
		goto free_bt;

	bt->dev = bdev->bd_dev;
	bt->act_mask = (u16)-1;

	blk_trace_setup_lba(bt, bdev);

	rcu_assign_pointer(q->blk_trace, bt);
	get_probe_ref();
	return 0;

free_bt:
	blk_trace_free(q, bt);
	return ret;
}

/*
 * sysfs interface to enable and configure tracing
 */

static ssize_t sysfs_blk_trace_attr_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);
static ssize_t sysfs_blk_trace_attr_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count);
#define BLK_TRACE_DEVICE_ATTR(_name) \
	DEVICE_ATTR(_name, S_IRUGO | S_IWUSR, \
		    sysfs_blk_trace_attr_show, \
		    sysfs_blk_trace_attr_store)

static BLK_TRACE_DEVICE_ATTR(enable);
static BLK_TRACE_DEVICE_ATTR(act_mask);
static BLK_TRACE_DEVICE_ATTR(pid);
static BLK_TRACE_DEVICE_ATTR(start_lba);
static BLK_TRACE_DEVICE_ATTR(end_lba);

static struct attribute *blk_trace_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_act_mask.attr,
	&dev_attr_pid.attr,
	&dev_attr_start_lba.attr,
	&dev_attr_end_lba.attr,
	NULL
};

struct attribute_group blk_trace_attr_group = {
	.name  = "trace",
	.attrs = blk_trace_attrs,
};

static const struct {
	int mask;
	const char *str;
} mask_maps[] = {
	{ BLK_TC_READ,		"read"		},
	{ BLK_TC_WRITE,		"write"		},
	{ BLK_TC_FLUSH,		"flush"		},
	{ BLK_TC_SYNC,		"sync"		},
	{ BLK_TC_QUEUE,		"queue"		},
	{ BLK_TC_REQUEUE,	"requeue"	},
	{ BLK_TC_ISSUE,		"issue"		},
	{ BLK_TC_COMPLETE,	"complete"	},
	{ BLK_TC_FS,		"fs"		},
	{ BLK_TC_PC,		"pc"		},
	{ BLK_TC_NOTIFY,	"notify"	},
	{ BLK_TC_AHEAD,		"ahead"		},
	{ BLK_TC_META,		"meta"		},
	{ BLK_TC_DISCARD,	"discard"	},
	{ BLK_TC_DRV_DATA,	"drv_data"	},
	{ BLK_TC_FUA,		"fua"		},
};

static int blk_trace_str2mask(const char *str)
{
	int i;
	int mask = 0;
	char *buf, *s, *token;

	buf = kstrdup(str, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;
	s = strstrip(buf);

	while (1) {
		token = strsep(&s, ",");
		if (token == NULL)
			break;

		if (*token == '\0')
			continue;

		for (i = 0; i < ARRAY_SIZE(mask_maps); i++) {
			if (strcasecmp(token, mask_maps[i].str) == 0) {
				mask |= mask_maps[i].mask;
				break;
			}
		}
		if (i == ARRAY_SIZE(mask_maps)) {
			mask = -EINVAL;
			break;
		}
	}
	kfree(buf);

	return mask;
}

static ssize_t blk_trace_mask2str(char *buf, int mask)
{
	int i;
	char *p = buf;

	for (i = 0; i < ARRAY_SIZE(mask_maps); i++) {
		if (mask & mask_maps[i].mask) {
			p += sprintf(p, "%s%s",
				    (p == buf) ? "" : ",", mask_maps[i].str);
		}
	}
	*p++ = '\n';

	return p - buf;
}

static ssize_t sysfs_blk_trace_attr_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct block_device *bdev = dev_to_bdev(dev);
	struct request_queue *q = bdev_get_queue(bdev);
	struct blk_trace *bt;
	ssize_t ret = -ENXIO;

	mutex_lock(&q->debugfs_mutex);

	bt = rcu_dereference_protected(q->blk_trace,
				       lockdep_is_held(&q->debugfs_mutex));
	if (attr == &dev_attr_enable) {
		ret = sprintf(buf, "%u\n", !!bt);
		goto out_unlock_bdev;
	}

	if (bt == NULL)
		ret = sprintf(buf, "disabled\n");
	else if (attr == &dev_attr_act_mask)
		ret = blk_trace_mask2str(buf, bt->act_mask);
	else if (attr == &dev_attr_pid)
		ret = sprintf(buf, "%u\n", bt->pid);
	else if (attr == &dev_attr_start_lba)
		ret = sprintf(buf, "%llu\n", bt->start_lba);
	else if (attr == &dev_attr_end_lba)
		ret = sprintf(buf, "%llu\n", bt->end_lba);

out_unlock_bdev:
	mutex_unlock(&q->debugfs_mutex);
	return ret;
}

static ssize_t sysfs_blk_trace_attr_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct block_device *bdev = dev_to_bdev(dev);
	struct request_queue *q = bdev_get_queue(bdev);
	struct blk_trace *bt;
	u64 value;
	ssize_t ret = -EINVAL;

	if (count == 0)
		goto out;

	if (attr == &dev_attr_act_mask) {
		if (kstrtoull(buf, 0, &value)) {
			/* Assume it is a list of trace category names */
			ret = blk_trace_str2mask(buf);
			if (ret < 0)
				goto out;
			value = ret;
		}
	} else {
		if (kstrtoull(buf, 0, &value))
			goto out;
	}

	mutex_lock(&q->debugfs_mutex);

	bt = rcu_dereference_protected(q->blk_trace,
				       lockdep_is_held(&q->debugfs_mutex));
	if (attr == &dev_attr_enable) {
		if (!!value == !!bt) {
			ret = 0;
			goto out_unlock_bdev;
		}
		if (value)
			ret = blk_trace_setup_queue(q, bdev);
		else
			ret = blk_trace_remove_queue(q);
		goto out_unlock_bdev;
	}

	ret = 0;
	if (bt == NULL) {
		ret = blk_trace_setup_queue(q, bdev);
		bt = rcu_dereference_protected(q->blk_trace,
				lockdep_is_held(&q->debugfs_mutex));
	}

	if (ret == 0) {
		if (attr == &dev_attr_act_mask)
			bt->act_mask = value;
		else if (attr == &dev_attr_pid)
			bt->pid = value;
		else if (attr == &dev_attr_start_lba)
			bt->start_lba = value;
		else if (attr == &dev_attr_end_lba)
			bt->end_lba = value;
	}

out_unlock_bdev:
	mutex_unlock(&q->debugfs_mutex);
out:
	return ret ? ret : count;
}

int blk_trace_init_sysfs(struct device *dev)
{
	return sysfs_create_group(&dev->kobj, &blk_trace_attr_group);
}

void blk_trace_remove_sysfs(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &blk_trace_attr_group);
}

#endif /* CONFIG_BLK_DEV_IO_TRACE */

#ifdef CONFIG_EVENT_TRACING

/**
 * blk_fill_rwbs - Fill the buffer rwbs by mapping op to character string.
 * @rwbs:	buffer to be filled
 * @op:		REQ_OP_XXX for the tracepoint
 *
 * Description:
 *     Maps the REQ_OP_XXX to character and fills the buffer provided by the
 *     caller with resulting string.
 *
 **/
void blk_fill_rwbs(char *rwbs, unsigned int op)
{
	int i = 0;

	if (op & REQ_PREFLUSH)
		rwbs[i++] = 'F';

	switch (op & REQ_OP_MASK) {
	case REQ_OP_WRITE:
		rwbs[i++] = 'W';
		break;
	case REQ_OP_DISCARD:
		rwbs[i++] = 'D';
		break;
	case REQ_OP_SECURE_ERASE:
		rwbs[i++] = 'D';
		rwbs[i++] = 'E';
		break;
	case REQ_OP_FLUSH:
		rwbs[i++] = 'F';
		break;
	case REQ_OP_READ:
		rwbs[i++] = 'R';
		break;
	default:
		rwbs[i++] = 'N';
	}

	if (op & REQ_FUA)
		rwbs[i++] = 'F';
	if (op & REQ_RAHEAD)
		rwbs[i++] = 'A';
	if (op & REQ_SYNC)
		rwbs[i++] = 'S';
	if (op & REQ_META)
		rwbs[i++] = 'M';

	rwbs[i] = '\0';
}
EXPORT_SYMBOL_GPL(blk_fill_rwbs);

#endif /* CONFIG_EVENT_TRACING */

