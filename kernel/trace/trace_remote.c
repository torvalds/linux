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

struct trace_remote_iterator {
	struct trace_remote		*remote;
	struct trace_seq		seq;
	struct delayed_work		poll_work;
	unsigned long			lost_events;
	u64				ts;
	int				cpu;
	int				evt_cpu;
};

struct trace_remote {
	struct trace_remote_callbacks	*cbs;
	void				*priv;
	struct trace_buffer		*trace_buffer;
	struct trace_buffer_desc	*trace_buffer_desc;
	unsigned long			trace_buffer_size;
	struct ring_buffer_remote	rb_remote;
	struct mutex			lock;
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

static ssize_t
tracing_on_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	struct trace_remote *remote = filp->private_data;
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
	struct trace_remote *remote = filp->private_data;
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

static struct trace_remote_iterator *trace_remote_iter(struct trace_remote *remote, int cpu)
{
	struct trace_remote_iterator *iter = NULL;
	int ret;

	lockdep_assert_held(&remote->lock);


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
		trace_seq_init(&iter->seq);
		INIT_DELAYED_WORK(&iter->poll_work, __poll_remote);
		schedule_delayed_work(&iter->poll_work, msecs_to_jiffies(remote->poll_ms));

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

	kfree(iter);
	trace_remote_put(remote);
}

static bool trace_remote_iter_read_event(struct trace_remote_iterator *iter)
{
	struct trace_buffer *trace_buffer = iter->remote->trace_buffer;
	int cpu = iter->cpu;

	if (cpu != RING_BUFFER_ALL_CPUS) {
		if (ring_buffer_empty_cpu(trace_buffer, cpu))
			return false;

		if (!ring_buffer_peek(trace_buffer, cpu, &iter->ts, &iter->lost_events))
			return false;

		iter->evt_cpu = cpu;
		return true;
	}

	iter->ts = U64_MAX;
	for_each_possible_cpu(cpu) {
		unsigned long lost_events;
		u64 ts;

		if (ring_buffer_empty_cpu(trace_buffer, cpu))
			continue;

		if (!ring_buffer_peek(trace_buffer, cpu, &ts, &lost_events))
			continue;

		if (ts >= iter->ts)
			continue;

		iter->ts = ts;
		iter->evt_cpu = cpu;
		iter->lost_events = lost_events;
	}

	return iter->ts != U64_MAX;
}

static int trace_remote_iter_print_event(struct trace_remote_iterator *iter)
{
	unsigned long usecs_rem;
	u64 ts = iter->ts;

	if (iter->lost_events)
		trace_seq_printf(&iter->seq, "CPU:%d [LOST %lu EVENTS]\n",
				 iter->evt_cpu, iter->lost_events);

	do_div(ts, 1000);
	usecs_rem = do_div(ts, USEC_PER_SEC);

	trace_seq_printf(&iter->seq, "[%03d]\t%5llu.%06lu: ", iter->evt_cpu,
			 ts, usecs_rem);

	return trace_seq_has_overflowed(&iter->seq) ? -EOVERFLOW : 0;
}

static int trace_pipe_open(struct inode *inode, struct file *filp)
{
	struct trace_remote *remote = inode->i_private;
	struct trace_remote_iterator *iter;
	int cpu = RING_BUFFER_ALL_CPUS;

	if (inode->i_cdev)
		cpu = (long)inode->i_cdev - 1;

	guard(mutex)(&remote->lock);
	iter = trace_remote_iter(remote, cpu);
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

	while (trace_remote_iter_read_event(iter)) {
		int prev_len = iter->seq.seq.len;

		if (trace_remote_iter_print_event(iter)) {
			iter->seq.seq.len = prev_len;
			break;
		}

		ring_buffer_consume(trace_buffer, iter->evt_cpu, NULL, NULL);
	}

	goto copy_to_user;
}

static const struct file_operations trace_pipe_fops = {
	.open		= trace_pipe_open,
	.read		= trace_pipe_read,
	.release	= trace_pipe_release,
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
	}

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
int trace_remote_register(const char *name, struct trace_remote_callbacks *cbs, void *priv)
{
	struct trace_remote *remote;

	remote = kzalloc_obj(*remote);
	if (!remote)
		return -ENOMEM;

	remote->cbs = cbs;
	remote->priv = priv;
	remote->trace_buffer_size = 7 << 10;
	remote->poll_ms = 100;
	mutex_init(&remote->lock);

	if (trace_remote_init_tracefs(name, remote)) {
		kfree(remote);
		return -ENOMEM;
	}

	return 0;
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
