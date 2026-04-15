// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 - Google LLC
 * Author: Vincent Donnefort <vdonnefort@google.com>
 */

#include <linux/kstrtox.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/tracefs.h>
#include <linux/trace_remote.h>
#include <linux/trace_seq.h>
#include <linux/types.h>

#include "trace.h"

#define TRACEFS_DIR		"remotes"
#define TRACEFS_MODE_WRITE	0640
#define TRACEFS_MODE_READ	0440

enum tri_type {
	TRI_CONSUMING,
	TRI_NONCONSUMING,
};

struct trace_remote_iterator {
	struct trace_remote		*remote;
	struct trace_seq		seq;
	struct delayed_work		poll_work;
	unsigned long			lost_events;
	u64				ts;
	struct ring_buffer_iter		*rb_iter;
	struct ring_buffer_iter		**rb_iters;
	struct remote_event_hdr		*evt;
	int				cpu;
	int				evt_cpu;
	loff_t				pos;
	enum tri_type			type;
};

struct trace_remote {
	struct trace_remote_callbacks	*cbs;
	void				*priv;
	struct trace_buffer		*trace_buffer;
	struct trace_buffer_desc	*trace_buffer_desc;
	struct dentry			*dentry;
	struct eventfs_inode		*eventfs;
	struct remote_event		*events;
	unsigned long			nr_events;
	unsigned long			trace_buffer_size;
	struct ring_buffer_remote	rb_remote;
	struct mutex			lock;
	struct rw_semaphore		reader_lock;
	struct rw_semaphore		*pcpu_reader_locks;
	unsigned int			nr_readers;
	unsigned int			poll_ms;
	bool				tracing_on;
};

static bool trace_remote_loaded(struct trace_remote *remote)
{
	return !!remote->trace_buffer;
}

static int trace_remote_load(struct trace_remote *remote)
{
	struct ring_buffer_remote *rb_remote = &remote->rb_remote;
	struct trace_buffer_desc *desc;

	lockdep_assert_held(&remote->lock);

	if (trace_remote_loaded(remote))
		return 0;

	desc = remote->cbs->load_trace_buffer(remote->trace_buffer_size, remote->priv);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	rb_remote->desc = desc;
	rb_remote->swap_reader_page = remote->cbs->swap_reader_page;
	rb_remote->priv = remote->priv;
	rb_remote->reset = remote->cbs->reset;
	remote->trace_buffer = ring_buffer_alloc_remote(rb_remote);
	if (!remote->trace_buffer) {
		remote->cbs->unload_trace_buffer(desc, remote->priv);
		return -ENOMEM;
	}

	remote->trace_buffer_desc = desc;

	return 0;
}

static void trace_remote_try_unload(struct trace_remote *remote)
{
	lockdep_assert_held(&remote->lock);

	if (!trace_remote_loaded(remote))
		return;

	/* The buffer is being read or writable */
	if (remote->nr_readers || remote->tracing_on)
		return;

	/* The buffer has readable data */
	if (!ring_buffer_empty(remote->trace_buffer))
		return;

	ring_buffer_free(remote->trace_buffer);
	remote->trace_buffer = NULL;
	remote->cbs->unload_trace_buffer(remote->trace_buffer_desc, remote->priv);
}

static int trace_remote_enable_tracing(struct trace_remote *remote)
{
	int ret;

	lockdep_assert_held(&remote->lock);

	if (remote->tracing_on)
		return 0;

	ret = trace_remote_load(remote);
	if (ret)
		return ret;

	ret = remote->cbs->enable_tracing(true, remote->priv);
	if (ret) {
		trace_remote_try_unload(remote);
		return ret;
	}

	remote->tracing_on = true;

	return 0;
}

static int trace_remote_disable_tracing(struct trace_remote *remote)
{
	int ret;

	lockdep_assert_held(&remote->lock);

	if (!remote->tracing_on)
		return 0;

	ret = remote->cbs->enable_tracing(false, remote->priv);
	if (ret)
		return ret;

	ring_buffer_poll_remote(remote->trace_buffer, RING_BUFFER_ALL_CPUS);
	remote->tracing_on = false;
	trace_remote_try_unload(remote);

	return 0;
}

static void trace_remote_reset(struct trace_remote *remote, int cpu)
{
	lockdep_assert_held(&remote->lock);

	if (!trace_remote_loaded(remote))
		return;

	if (cpu == RING_BUFFER_ALL_CPUS)
		ring_buffer_reset(remote->trace_buffer);
	else
		ring_buffer_reset_cpu(remote->trace_buffer, cpu);

	trace_remote_try_unload(remote);
}

static ssize_t
tracing_on_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	struct seq_file *seq = filp->private_data;
	struct trace_remote *remote = seq->private;
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	guard(mutex)(&remote->lock);

	ret = val ? trace_remote_enable_tracing(remote) : trace_remote_disable_tracing(remote);
	if (ret)
		return ret;

	return cnt;
}
static int tracing_on_show(struct seq_file *s, void *unused)
{
	struct trace_remote *remote = s->private;

	seq_printf(s, "%d\n", remote->tracing_on);

	return 0;
}
DEFINE_SHOW_STORE_ATTRIBUTE(tracing_on);

static ssize_t buffer_size_kb_write(struct file *filp, const char __user *ubuf, size_t cnt,
				    loff_t *ppos)
{
	struct seq_file *seq = filp->private_data;
	struct trace_remote *remote = seq->private;
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	/* KiB to Bytes */
	if (!val || check_shl_overflow(val, 10, &val))
		return -EINVAL;

	guard(mutex)(&remote->lock);

	if (trace_remote_loaded(remote))
		return -EBUSY;

	remote->trace_buffer_size = val;

	return cnt;
}

static int buffer_size_kb_show(struct seq_file *s, void *unused)
{
	struct trace_remote *remote = s->private;

	seq_printf(s, "%lu (%s)\n", remote->trace_buffer_size >> 10,
		   trace_remote_loaded(remote) ? "loaded" : "unloaded");

	return 0;
}
DEFINE_SHOW_STORE_ATTRIBUTE(buffer_size_kb);

static int trace_remote_get(struct trace_remote *remote, int cpu)
{
	int ret;

	if (remote->nr_readers == UINT_MAX)
		return -EBUSY;

	ret = trace_remote_load(remote);
	if (ret)
		return ret;

	if (cpu != RING_BUFFER_ALL_CPUS && !remote->pcpu_reader_locks) {
		int lock_cpu;

		remote->pcpu_reader_locks = kcalloc(nr_cpu_ids, sizeof(*remote->pcpu_reader_locks),
						    GFP_KERNEL);
		if (!remote->pcpu_reader_locks) {
			trace_remote_try_unload(remote);
			return -ENOMEM;
		}

		for_each_possible_cpu(lock_cpu)
			init_rwsem(&remote->pcpu_reader_locks[lock_cpu]);
	}

	remote->nr_readers++;

	return 0;
}

static void trace_remote_put(struct trace_remote *remote)
{
	if (WARN_ON(!remote->nr_readers))
		return;

	remote->nr_readers--;
	if (remote->nr_readers)
		return;

	kfree(remote->pcpu_reader_locks);
	remote->pcpu_reader_locks = NULL;

	trace_remote_try_unload(remote);
}

static void __poll_remote(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct trace_remote_iterator *iter;

	iter = container_of(dwork, struct trace_remote_iterator, poll_work);
	ring_buffer_poll_remote(iter->remote->trace_buffer, iter->cpu);
	schedule_delayed_work((struct delayed_work *)work,
			      msecs_to_jiffies(iter->remote->poll_ms));
}

static void __free_ring_buffer_iter(struct trace_remote_iterator *iter, int cpu)
{
	if (cpu != RING_BUFFER_ALL_CPUS) {
		ring_buffer_read_finish(iter->rb_iter);
		return;
	}

	for_each_possible_cpu(cpu) {
		if (iter->rb_iters[cpu])
			ring_buffer_read_finish(iter->rb_iters[cpu]);
	}

	kfree(iter->rb_iters);
}

static int __alloc_ring_buffer_iter(struct trace_remote_iterator *iter, int cpu)
{
	if (cpu != RING_BUFFER_ALL_CPUS) {
		iter->rb_iter = ring_buffer_read_start(iter->remote->trace_buffer, cpu, GFP_KERNEL);

		return iter->rb_iter ? 0 : -ENOMEM;
	}

	iter->rb_iters = kcalloc(nr_cpu_ids, sizeof(*iter->rb_iters), GFP_KERNEL);
	if (!iter->rb_iters)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		iter->rb_iters[cpu] = ring_buffer_read_start(iter->remote->trace_buffer, cpu,
							     GFP_KERNEL);
		if (!iter->rb_iters[cpu]) {
			__free_ring_buffer_iter(iter, RING_BUFFER_ALL_CPUS);
			return -ENOMEM;
		}
	}

	return 0;
}

static struct trace_remote_iterator
*trace_remote_iter(struct trace_remote *remote, int cpu, enum tri_type type)
{
	struct trace_remote_iterator *iter = NULL;
	int ret;

	lockdep_assert_held(&remote->lock);

	if (type == TRI_NONCONSUMING && !trace_remote_loaded(remote))
		return NULL;

	ret = trace_remote_get(remote, cpu);
	if (ret)
		return ERR_PTR(ret);

	/* Test the CPU */
	ret = ring_buffer_poll_remote(remote->trace_buffer, cpu);
	if (ret)
		goto err;

	iter = kzalloc_obj(*iter);
	if (iter) {
		iter->remote = remote;
		iter->cpu = cpu;
		iter->type = type;
		trace_seq_init(&iter->seq);

		switch (type) {
		case TRI_CONSUMING:
			INIT_DELAYED_WORK(&iter->poll_work, __poll_remote);
			schedule_delayed_work(&iter->poll_work, msecs_to_jiffies(remote->poll_ms));
			break;
		case TRI_NONCONSUMING:
			ret = __alloc_ring_buffer_iter(iter, cpu);
			break;
		}

		if (ret)
			goto err;

		return iter;
	}
	ret = -ENOMEM;

err:
	kfree(iter);
	trace_remote_put(remote);

	return ERR_PTR(ret);
}

static void trace_remote_iter_free(struct trace_remote_iterator *iter)
{
	struct trace_remote *remote;

	if (!iter)
		return;

	remote = iter->remote;

	lockdep_assert_held(&remote->lock);

	switch (iter->type) {
	case TRI_CONSUMING:
		cancel_delayed_work_sync(&iter->poll_work);
		break;
	case TRI_NONCONSUMING:
		__free_ring_buffer_iter(iter, iter->cpu);
		break;
	}

	kfree(iter);
	trace_remote_put(remote);
}

static void trace_remote_iter_read_start(struct trace_remote_iterator *iter)
{
	struct trace_remote *remote = iter->remote;
	int cpu = iter->cpu;

	/* Acquire global reader lock */
	if (cpu == RING_BUFFER_ALL_CPUS && iter->type == TRI_CONSUMING)
		down_write(&remote->reader_lock);
	else
		down_read(&remote->reader_lock);

	if (cpu == RING_BUFFER_ALL_CPUS)
		return;

	/*
	 * No need for the remote lock here, iter holds a reference on
	 * remote->nr_readers
	 */

	/* Get the per-CPU one */
	if (WARN_ON_ONCE(!remote->pcpu_reader_locks))
		return;

	if (iter->type == TRI_CONSUMING)
		down_write(&remote->pcpu_reader_locks[cpu]);
	else
		down_read(&remote->pcpu_reader_locks[cpu]);
}

static void trace_remote_iter_read_finished(struct trace_remote_iterator *iter)
{
	struct trace_remote *remote = iter->remote;
	int cpu = iter->cpu;

	/* Release per-CPU reader lock */
	if (cpu != RING_BUFFER_ALL_CPUS) {
		/*
		 * No need for the remote lock here, iter holds a reference on
		 * remote->nr_readers
		 */
		if (iter->type == TRI_CONSUMING)
			up_write(&remote->pcpu_reader_locks[cpu]);
		else
			up_read(&remote->pcpu_reader_locks[cpu]);
	}

	/* Release global reader lock */
	if (cpu == RING_BUFFER_ALL_CPUS && iter->type == TRI_CONSUMING)
		up_write(&remote->reader_lock);
	else
		up_read(&remote->reader_lock);
}

static struct ring_buffer_iter *__get_rb_iter(struct trace_remote_iterator *iter, int cpu)
{
	return iter->cpu != RING_BUFFER_ALL_CPUS ? iter->rb_iter : iter->rb_iters[cpu];
}

static struct ring_buffer_event *
__peek_event(struct trace_remote_iterator *iter, int cpu, u64 *ts, unsigned long *lost_events)
{
	struct ring_buffer_event *rb_evt;
	struct ring_buffer_iter *rb_iter;

	switch (iter->type) {
	case TRI_CONSUMING:
		return ring_buffer_peek(iter->remote->trace_buffer, cpu, ts, lost_events);
	case TRI_NONCONSUMING:
		rb_iter = __get_rb_iter(iter, cpu);
		rb_evt = ring_buffer_iter_peek(rb_iter, ts);
		if (!rb_evt)
			return NULL;

		*lost_events = ring_buffer_iter_dropped(rb_iter);

		return rb_evt;
	}

	return NULL;
}

static bool trace_remote_iter_read_event(struct trace_remote_iterator *iter)
{
	struct trace_buffer *trace_buffer = iter->remote->trace_buffer;
	struct ring_buffer_event *rb_evt;
	int cpu = iter->cpu;

	if (cpu != RING_BUFFER_ALL_CPUS) {
		if (ring_buffer_empty_cpu(trace_buffer, cpu))
			return false;

		rb_evt = __peek_event(iter, cpu, &iter->ts, &iter->lost_events);
		if (!rb_evt)
			return false;

		iter->evt_cpu = cpu;
		iter->evt = ring_buffer_event_data(rb_evt);
		return true;
	}

	iter->ts = U64_MAX;
	for_each_possible_cpu(cpu) {
		unsigned long lost_events;
		u64 ts;

		if (ring_buffer_empty_cpu(trace_buffer, cpu))
			continue;

		rb_evt = __peek_event(iter, cpu, &ts, &lost_events);
		if (!rb_evt)
			continue;

		if (ts >= iter->ts)
			continue;

		iter->ts = ts;
		iter->evt_cpu = cpu;
		iter->evt = ring_buffer_event_data(rb_evt);
		iter->lost_events = lost_events;
	}

	return iter->ts != U64_MAX;
}

static void trace_remote_iter_move(struct trace_remote_iterator *iter)
{
	struct trace_buffer *trace_buffer = iter->remote->trace_buffer;

	switch (iter->type) {
	case TRI_CONSUMING:
		ring_buffer_consume(trace_buffer, iter->evt_cpu, NULL, NULL);
		break;
	case TRI_NONCONSUMING:
		ring_buffer_iter_advance(__get_rb_iter(iter, iter->evt_cpu));
		break;
	}
}

static struct remote_event *trace_remote_find_event(struct trace_remote *remote, unsigned short id);

static int trace_remote_iter_print_event(struct trace_remote_iterator *iter)
{
	struct remote_event *evt;
	unsigned long usecs_rem;
	u64 ts = iter->ts;

	if (iter->lost_events)
		trace_seq_printf(&iter->seq, "CPU:%d [LOST %lu EVENTS]\n",
				 iter->evt_cpu, iter->lost_events);

	do_div(ts, 1000);
	usecs_rem = do_div(ts, USEC_PER_SEC);

	trace_seq_printf(&iter->seq, "[%03d]\t%5llu.%06lu: ", iter->evt_cpu,
			 ts, usecs_rem);

	evt = trace_remote_find_event(iter->remote, iter->evt->id);
	if (!evt)
		trace_seq_printf(&iter->seq, "UNKNOWN id=%d\n", iter->evt->id);
	else
		evt->print(iter->evt, &iter->seq);

	return trace_seq_has_overflowed(&iter->seq) ? -EOVERFLOW : 0;
}

static int trace_pipe_open(struct inode *inode, struct file *filp)
{
	struct trace_remote *remote = inode->i_private;
	struct trace_remote_iterator *iter;
	int cpu = tracing_get_cpu(inode);

	guard(mutex)(&remote->lock);

	iter = trace_remote_iter(remote, cpu, TRI_CONSUMING);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	filp->private_data = iter;

	return IS_ERR(iter) ? PTR_ERR(iter) : 0;
}

static int trace_pipe_release(struct inode *inode, struct file *filp)
{
	struct trace_remote_iterator *iter = filp->private_data;
	struct trace_remote *remote = iter->remote;

	guard(mutex)(&remote->lock);

	trace_remote_iter_free(iter);

	return 0;
}

static ssize_t trace_pipe_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	struct trace_remote_iterator *iter = filp->private_data;
	struct trace_buffer *trace_buffer = iter->remote->trace_buffer;
	int ret;

copy_to_user:
	ret = trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (ret != -EBUSY)
		return ret;

	trace_seq_init(&iter->seq);

	ret = ring_buffer_wait(trace_buffer, iter->cpu, 0, NULL, NULL);
	if (ret < 0)
		return ret;

	trace_remote_iter_read_start(iter);

	while (trace_remote_iter_read_event(iter)) {
		int prev_len = iter->seq.seq.len;

		if (trace_remote_iter_print_event(iter)) {
			iter->seq.seq.len = prev_len;
			break;
		}

		trace_remote_iter_move(iter);
	}

	trace_remote_iter_read_finished(iter);

	goto copy_to_user;
}

static const struct file_operations trace_pipe_fops = {
	.open		= trace_pipe_open,
	.read		= trace_pipe_read,
	.release	= trace_pipe_release,
};

static void *trace_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct trace_remote_iterator *iter = m->private;

	++*pos;

	if (!iter || !trace_remote_iter_read_event(iter))
		return NULL;

	trace_remote_iter_move(iter);
	iter->pos++;

	return iter;
}

static void *trace_start(struct seq_file *m, loff_t *pos)
{
	struct trace_remote_iterator *iter = m->private;
	loff_t i;

	if (!iter)
		return NULL;

	trace_remote_iter_read_start(iter);

	if (!*pos) {
		iter->pos = -1;
		return trace_next(m, NULL, &i);
	}

	i = iter->pos;
	while (i < *pos) {
		iter = trace_next(m, NULL, &i);
		if (!iter)
			return NULL;
	}

	return iter;
}

static int trace_show(struct seq_file *m, void *v)
{
	struct trace_remote_iterator *iter = v;

	trace_seq_init(&iter->seq);

	if (trace_remote_iter_print_event(iter)) {
		seq_printf(m, "[EVENT %d PRINT TOO BIG]\n", iter->evt->id);
		return 0;
	}

	return trace_print_seq(m, &iter->seq);
}

static void trace_stop(struct seq_file *m, void *v)
{
	struct trace_remote_iterator *iter = m->private;

	if (iter)
		trace_remote_iter_read_finished(iter);
}

static const struct seq_operations trace_sops = {
	.start		= trace_start,
	.next		= trace_next,
	.show		= trace_show,
	.stop		= trace_stop,
};

static int trace_open(struct inode *inode, struct file *filp)
{
	struct trace_remote *remote = inode->i_private;
	struct trace_remote_iterator *iter = NULL;
	int cpu = tracing_get_cpu(inode);
	int ret;

	if (!(filp->f_mode & FMODE_READ))
		return 0;

	guard(mutex)(&remote->lock);

	iter = trace_remote_iter(remote, cpu, TRI_NONCONSUMING);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	ret = seq_open(filp, &trace_sops);
	if (ret) {
		trace_remote_iter_free(iter);
		return ret;
	}

	((struct seq_file *)filp->private_data)->private = (void *)iter;

	return 0;
}

static int trace_release(struct inode *inode, struct file *filp)
{
	struct trace_remote_iterator *iter;

	if (!(filp->f_mode & FMODE_READ))
		return 0;

	iter = ((struct seq_file *)filp->private_data)->private;
	seq_release(inode, filp);

	if (!iter)
		return 0;

	guard(mutex)(&iter->remote->lock);

	trace_remote_iter_free(iter);

	return 0;
}

static ssize_t trace_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	struct inode *inode = file_inode(filp);
	struct trace_remote *remote = inode->i_private;
	int cpu = tracing_get_cpu(inode);

	guard(mutex)(&remote->lock);

	trace_remote_reset(remote, cpu);

	return cnt;
}

static const struct file_operations trace_fops = {
	.open		= trace_open,
	.write		= trace_write,
	.read		= seq_read,
	.read_iter	= seq_read_iter,
	.release	= trace_release,
};

static int trace_remote_init_tracefs(const char *name, struct trace_remote *remote)
{
	struct dentry *remote_d, *percpu_d, *d;
	static struct dentry *root;
	static DEFINE_MUTEX(lock);
	bool root_inited = false;
	int cpu;

	guard(mutex)(&lock);

	if (!root) {
		root = tracefs_create_dir(TRACEFS_DIR, NULL);
		if (!root) {
			pr_err("Failed to create tracefs dir "TRACEFS_DIR"\n");
			return -ENOMEM;
		}
		root_inited = true;
	}

	remote_d = tracefs_create_dir(name, root);
	if (!remote_d) {
		pr_err("Failed to create tracefs dir "TRACEFS_DIR"%s/\n", name);
		goto err;
	}

	d = trace_create_file("tracing_on", TRACEFS_MODE_WRITE, remote_d, remote, &tracing_on_fops);
	if (!d)
		goto err;

	d = trace_create_file("buffer_size_kb", TRACEFS_MODE_WRITE, remote_d, remote,
			      &buffer_size_kb_fops);
	if (!d)
		goto err;

	d = trace_create_file("trace_pipe", TRACEFS_MODE_READ, remote_d, remote, &trace_pipe_fops);
	if (!d)
		goto err;

	d = trace_create_file("trace", TRACEFS_MODE_WRITE, remote_d, remote, &trace_fops);
	if (!d)
		goto err;

	percpu_d = tracefs_create_dir("per_cpu", remote_d);
	if (!percpu_d) {
		pr_err("Failed to create tracefs dir "TRACEFS_DIR"%s/per_cpu/\n", name);
		goto err;
	}

	for_each_possible_cpu(cpu) {
		struct dentry *cpu_d;
		char cpu_name[16];

		snprintf(cpu_name, sizeof(cpu_name), "cpu%d", cpu);
		cpu_d = tracefs_create_dir(cpu_name, percpu_d);
		if (!cpu_d) {
			pr_err("Failed to create tracefs dir "TRACEFS_DIR"%s/percpu/cpu%d\n",
			       name, cpu);
			goto err;
		}

		d = trace_create_cpu_file("trace_pipe", TRACEFS_MODE_READ, cpu_d, remote, cpu,
					  &trace_pipe_fops);
		if (!d)
			goto err;

		d = trace_create_cpu_file("trace", TRACEFS_MODE_WRITE, cpu_d, remote, cpu,
					  &trace_fops);
		if (!d)
			goto err;
	}

	remote->dentry = remote_d;

	return 0;

err:
	if (root_inited) {
		tracefs_remove(root);
		root = NULL;
	} else {
		tracefs_remove(remote_d);
	}

	return -ENOMEM;
}

static int trace_remote_register_events(const char *remote_name, struct trace_remote *remote,
					struct remote_event *events, size_t nr_events);

/**
 * trace_remote_register() - Register a Tracefs remote
 * @name:	Name of the remote, used for the Tracefs remotes/ directory.
 * @cbs:	Set of callbacks used to control the remote.
 * @priv:	Private data, passed to each callback from @cbs.
 * @events:	Array of events. &remote_event.name and &remote_event.id must be
 *		filled by the caller.
 * @nr_events:	Number of events in the @events array.
 *
 * A trace remote is an entity, outside of the kernel (most likely firmware or
 * hypervisor) capable of writing events into a Tracefs compatible ring-buffer.
 * The kernel would then act as a reader.
 *
 * The registered remote will be found under the Tracefs directory
 * remotes/<name>.
 *
 * Return: 0 on success, negative error code on failure.
 */
int trace_remote_register(const char *name, struct trace_remote_callbacks *cbs, void *priv,
			  struct remote_event *events, size_t nr_events)
{
	struct trace_remote *remote;
	int ret;

	remote = kzalloc_obj(*remote);
	if (!remote)
		return -ENOMEM;

	remote->cbs = cbs;
	remote->priv = priv;
	remote->trace_buffer_size = 7 << 10;
	remote->poll_ms = 100;
	mutex_init(&remote->lock);
	init_rwsem(&remote->reader_lock);

	if (trace_remote_init_tracefs(name, remote)) {
		kfree(remote);
		return -ENOMEM;
	}

	ret = trace_remote_register_events(name, remote, events, nr_events);
	if (ret) {
		pr_err("Failed to register events for trace remote '%s' (%d)\n",
		       name, ret);
		return ret;
	}

	ret = cbs->init ? cbs->init(remote->dentry, priv) : 0;
	if (ret)
		pr_err("Init failed for trace remote '%s' (%d)\n", name, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(trace_remote_register);

/**
 * trace_remote_free_buffer() - Free trace buffer allocated with trace_remote_alloc_buffer()
 * @desc:	Descriptor of the per-CPU ring-buffers, originally filled by
 *		trace_remote_alloc_buffer()
 *
 * Most likely called from &trace_remote_callbacks.unload_trace_buffer.
 */
void trace_remote_free_buffer(struct trace_buffer_desc *desc)
{
	struct ring_buffer_desc *rb_desc;
	int cpu;

	for_each_ring_buffer_desc(rb_desc, cpu, desc) {
		unsigned int id;

		free_page(rb_desc->meta_va);

		for (id = 0; id < rb_desc->nr_page_va; id++)
			free_page(rb_desc->page_va[id]);
	}
}
EXPORT_SYMBOL_GPL(trace_remote_free_buffer);

/**
 * trace_remote_alloc_buffer() - Dynamically allocate a trace buffer
 * @desc:		Uninitialized trace_buffer_desc
 * @desc_size:		Size of the trace_buffer_desc. Must be at least equal to
 *			trace_buffer_desc_size()
 * @buffer_size:	Size in bytes of each per-CPU ring-buffer
 * @cpumask:		CPUs to allocate a ring-buffer for
 *
 * Helper to dynamically allocate a set of pages (enough to cover @buffer_size)
 * for each CPU from @cpumask and fill @desc. Most likely called from
 * &trace_remote_callbacks.load_trace_buffer.
 *
 * Return: 0 on success, negative error code on failure.
 */
int trace_remote_alloc_buffer(struct trace_buffer_desc *desc, size_t desc_size, size_t buffer_size,
			      const struct cpumask *cpumask)
{
	unsigned int nr_pages = max(DIV_ROUND_UP(buffer_size, PAGE_SIZE), 2UL) + 1;
	void *desc_end = desc + desc_size;
	struct ring_buffer_desc *rb_desc;
	int cpu, ret = -ENOMEM;

	if (desc_size < struct_size(desc, __data, 0))
		return -EINVAL;

	desc->nr_cpus = 0;
	desc->struct_len = struct_size(desc, __data, 0);

	rb_desc = (struct ring_buffer_desc *)&desc->__data[0];

	for_each_cpu(cpu, cpumask) {
		unsigned int id;

		if ((void *)rb_desc + struct_size(rb_desc, page_va, nr_pages) > desc_end) {
			ret = -EINVAL;
			goto err;
		}

		rb_desc->cpu = cpu;
		rb_desc->nr_page_va = 0;
		rb_desc->meta_va = (unsigned long)__get_free_page(GFP_KERNEL);
		if (!rb_desc->meta_va)
			goto err;

		for (id = 0; id < nr_pages; id++) {
			rb_desc->page_va[id] = (unsigned long)__get_free_page(GFP_KERNEL);
			if (!rb_desc->page_va[id])
				goto err;

			rb_desc->nr_page_va++;
		}
		desc->nr_cpus++;
		desc->struct_len += offsetof(struct ring_buffer_desc, page_va);
		desc->struct_len += struct_size(rb_desc, page_va, rb_desc->nr_page_va);
		rb_desc = __next_ring_buffer_desc(rb_desc);
	}

	return 0;

err:
	trace_remote_free_buffer(desc);
	return ret;
}
EXPORT_SYMBOL_GPL(trace_remote_alloc_buffer);

static int
trace_remote_enable_event(struct trace_remote *remote, struct remote_event *evt, bool enable)
{
	int ret;

	lockdep_assert_held(&remote->lock);

	if (evt->enabled == enable)
		return 0;

	ret = remote->cbs->enable_event(evt->id, enable, remote->priv);
	if (ret)
		return ret;

	evt->enabled = enable;

	return 0;
}

static int remote_event_enable_show(struct seq_file *s, void *unused)
{
	struct remote_event *evt = s->private;

	seq_printf(s, "%d\n", evt->enabled);

	return 0;
}

static ssize_t remote_event_enable_write(struct file *filp, const char __user *ubuf,
					 size_t count, loff_t *ppos)
{
	struct seq_file *seq = filp->private_data;
	struct remote_event *evt = seq->private;
	struct trace_remote *remote = evt->remote;
	u8 enable;
	int ret;

	ret = kstrtou8_from_user(ubuf, count, 10, &enable);
	if (ret)
		return ret;

	guard(mutex)(&remote->lock);

	ret = trace_remote_enable_event(remote, evt, enable);
	if (ret)
		return ret;

	return count;
}
DEFINE_SHOW_STORE_ATTRIBUTE(remote_event_enable);

static int remote_event_id_show(struct seq_file *s, void *unused)
{
	struct remote_event *evt = s->private;

	seq_printf(s, "%d\n", evt->id);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(remote_event_id);

static int remote_event_format_show(struct seq_file *s, void *unused)
{
	size_t offset = sizeof(struct remote_event_hdr);
	struct remote_event *evt = s->private;
	struct trace_event_fields *field;

	seq_printf(s, "name: %s\n", evt->name);
	seq_printf(s, "ID: %d\n", evt->id);
	seq_puts(s,
		 "format:\n\tfield:unsigned short common_type;\toffset:0;\tsize:2;\tsigned:0;\n\n");

	field = &evt->fields[0];
	while (field->name) {
		seq_printf(s, "\tfield:%s %s;\toffset:%zu;\tsize:%u;\tsigned:%d;\n",
			   field->type, field->name, offset, field->size,
			   field->is_signed);
		offset += field->size;
		field++;
	}

	if (field != &evt->fields[0])
		seq_puts(s, "\n");

	seq_printf(s, "print fmt: %s\n", evt->print_fmt);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(remote_event_format);

static int remote_event_callback(const char *name, umode_t *mode, void **data,
				 const struct file_operations **fops)
{
	if (!strcmp(name, "enable")) {
		*mode = TRACEFS_MODE_WRITE;
		*fops = &remote_event_enable_fops;
		return 1;
	}

	if (!strcmp(name, "id")) {
		*mode = TRACEFS_MODE_READ;
		*fops = &remote_event_id_fops;
		return 1;
	}

	if (!strcmp(name, "format")) {
		*mode = TRACEFS_MODE_READ;
		*fops = &remote_event_format_fops;
		return 1;
	}

	return 0;
}

static ssize_t remote_events_dir_enable_write(struct file *filp, const char __user *ubuf,
					      size_t count, loff_t *ppos)
{
	struct trace_remote *remote = file_inode(filp)->i_private;
	int i, ret;
	u8 enable;

	ret = kstrtou8_from_user(ubuf, count, 10, &enable);
	if (ret)
		return ret;

	guard(mutex)(&remote->lock);

	for (i = 0; i < remote->nr_events; i++) {
		struct remote_event *evt = &remote->events[i];

		trace_remote_enable_event(remote, evt, enable);
	}

	return count;
}

static ssize_t remote_events_dir_enable_read(struct file *filp, char __user *ubuf, size_t cnt,
					     loff_t *ppos)
{
	struct trace_remote *remote = file_inode(filp)->i_private;
	const char enabled_char[] = {'0', '1', 'X'};
	char enabled_str[] = " \n";
	int i, enabled = -1;

	guard(mutex)(&remote->lock);

	for (i = 0; i < remote->nr_events; i++) {
		struct remote_event *evt = &remote->events[i];

		if (enabled == -1) {
			enabled = evt->enabled;
		} else if (enabled != evt->enabled) {
			enabled = 2;
			break;
		}
	}

	enabled_str[0] = enabled_char[enabled == -1 ? 0 : enabled];

	return simple_read_from_buffer(ubuf, cnt, ppos, enabled_str, 2);
}

static const struct file_operations remote_events_dir_enable_fops = {
	.write = remote_events_dir_enable_write,
	.read = remote_events_dir_enable_read,
};

static ssize_t
remote_events_dir_header_page_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	struct trace_seq *s;
	int ret;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	trace_seq_init(s);

	ring_buffer_print_page_header(NULL, s);
	ret = simple_read_from_buffer(ubuf, cnt, ppos, s->buffer, trace_seq_used(s));
	kfree(s);

	return ret;
}

static const struct file_operations remote_events_dir_header_page_fops = {
	.read = remote_events_dir_header_page_read,
};

static ssize_t
remote_events_dir_header_event_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	struct trace_seq *s;
	int ret;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	trace_seq_init(s);

	ring_buffer_print_entry_header(s);
	ret = simple_read_from_buffer(ubuf, cnt, ppos, s->buffer, trace_seq_used(s));
	kfree(s);

	return ret;
}

static const struct file_operations remote_events_dir_header_event_fops = {
	.read = remote_events_dir_header_event_read,
};

static int remote_events_dir_callback(const char *name, umode_t *mode, void **data,
				      const struct file_operations **fops)
{
	if (!strcmp(name, "enable")) {
		*mode = TRACEFS_MODE_WRITE;
		*fops = &remote_events_dir_enable_fops;
		return 1;
	}

	if (!strcmp(name, "header_page")) {
		*mode = TRACEFS_MODE_READ;
		*fops = &remote_events_dir_header_page_fops;
		return 1;
	}

	if (!strcmp(name, "header_event")) {
		*mode = TRACEFS_MODE_READ;
		*fops = &remote_events_dir_header_event_fops;
		return 1;
	}

	return 0;
}

static int trace_remote_init_eventfs(const char *remote_name, struct trace_remote *remote,
				     struct remote_event *evt)
{
	struct eventfs_inode *eventfs = remote->eventfs;
	static struct eventfs_entry dir_entries[] = {
		{
			.name		= "enable",
			.callback	= remote_events_dir_callback,
		}, {
			.name		= "header_page",
			.callback	= remote_events_dir_callback,
		}, {
			.name		= "header_event",
			.callback	= remote_events_dir_callback,
		}
	};
	static struct eventfs_entry entries[] = {
		{
			.name		= "enable",
			.callback	= remote_event_callback,
		}, {
			.name		= "id",
			.callback	= remote_event_callback,
		}, {
			.name		= "format",
			.callback	= remote_event_callback,
		}
	};
	bool eventfs_create = false;

	if (!eventfs) {
		eventfs = eventfs_create_events_dir("events", remote->dentry, dir_entries,
						    ARRAY_SIZE(dir_entries), remote);
		if (IS_ERR(eventfs))
			return PTR_ERR(eventfs);

		/*
		 * Create similar hierarchy as local events even if a single system is supported at
		 * the moment
		 */
		eventfs = eventfs_create_dir(remote_name, eventfs, NULL, 0, NULL);
		if (IS_ERR(eventfs))
			return PTR_ERR(eventfs);

		remote->eventfs = eventfs;
		eventfs_create = true;
	}

	eventfs = eventfs_create_dir(evt->name, eventfs, entries, ARRAY_SIZE(entries), evt);
	if (IS_ERR(eventfs)) {
		if (eventfs_create) {
			eventfs_remove_events_dir(remote->eventfs);
			remote->eventfs = NULL;
		}
		return PTR_ERR(eventfs);
	}

	return 0;
}

static int trace_remote_attach_events(struct trace_remote *remote, struct remote_event *events,
				      size_t nr_events)
{
	int i;

	for (i = 0; i < nr_events; i++) {
		struct remote_event *evt = &events[i];

		if (evt->remote)
			return -EEXIST;

		evt->remote = remote;

		/* We need events to be sorted for efficient lookup */
		if (i && evt->id <= events[i - 1].id)
			return -EINVAL;
	}

	remote->events = events;
	remote->nr_events = nr_events;

	return 0;
}

static int trace_remote_register_events(const char *remote_name, struct trace_remote *remote,
					struct remote_event *events, size_t nr_events)
{
	int i, ret;

	ret = trace_remote_attach_events(remote, events, nr_events);
	if (ret)
		return ret;

	for (i = 0; i < nr_events; i++) {
		struct remote_event *evt = &events[i];

		ret = trace_remote_init_eventfs(remote_name, remote, evt);
		if (ret)
			pr_warn("Failed to init eventfs for event '%s' (%d)",
				evt->name, ret);
	}

	return 0;
}

static int __cmp_events(const void *key, const void *data)
{
	const struct remote_event *evt = data;
	int id = (int)((long)key);

	return id - (int)evt->id;
}

static struct remote_event *trace_remote_find_event(struct trace_remote *remote, unsigned short id)
{
	return bsearch((const void *)(unsigned long)id, remote->events, remote->nr_events,
		       sizeof(*remote->events), __cmp_events);
}
