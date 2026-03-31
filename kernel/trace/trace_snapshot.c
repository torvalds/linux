// SPDX-License-Identifier: GPL-2.0
#include <linux/fsnotify.h>

#include <asm/setup.h> /* COMMAND_LINE_SIZE */

#include "trace.h"

/* Used if snapshot allocated at boot */
static bool allocate_snapshot;
static bool snapshot_at_boot;

static char boot_snapshot_info[COMMAND_LINE_SIZE] __initdata;
static int boot_snapshot_index;

static int __init boot_alloc_snapshot(char *str)
{
	char *slot = boot_snapshot_info + boot_snapshot_index;
	int left = sizeof(boot_snapshot_info) - boot_snapshot_index;
	int ret;

	if (str[0] == '=') {
		str++;
		if (strlen(str) >= left)
			return -1;

		ret = snprintf(slot, left, "%s\t", str);
		boot_snapshot_index += ret;
	} else {
		allocate_snapshot = true;
		/* We also need the main ring buffer expanded */
		trace_set_ring_buffer_expanded(NULL);
	}
	return 1;
}
__setup("alloc_snapshot", boot_alloc_snapshot);


static int __init boot_snapshot(char *str)
{
	snapshot_at_boot = true;
	boot_alloc_snapshot(str);
	return 1;
}
__setup("ftrace_boot_snapshot", boot_snapshot);
static void tracing_snapshot_instance_cond(struct trace_array *tr,
					   void *cond_data)
{
	unsigned long flags;

	if (in_nmi()) {
		trace_array_puts(tr, "*** SNAPSHOT CALLED FROM NMI CONTEXT ***\n");
		trace_array_puts(tr, "*** snapshot is being ignored        ***\n");
		return;
	}

	if (!tr->allocated_snapshot) {
		trace_array_puts(tr, "*** SNAPSHOT NOT ALLOCATED ***\n");
		trace_array_puts(tr, "*** stopping trace here!   ***\n");
		tracer_tracing_off(tr);
		return;
	}

	if (tr->mapped) {
		trace_array_puts(tr, "*** BUFFER MEMORY MAPPED ***\n");
		trace_array_puts(tr, "*** Can not use snapshot (sorry) ***\n");
		return;
	}

	/* Note, snapshot can not be used when the tracer uses it */
	if (tracer_uses_snapshot(tr->current_trace)) {
		trace_array_puts(tr, "*** LATENCY TRACER ACTIVE ***\n");
		trace_array_puts(tr, "*** Can not use snapshot (sorry) ***\n");
		return;
	}

	local_irq_save(flags);
	update_max_tr(tr, current, smp_processor_id(), cond_data);
	local_irq_restore(flags);
}

void tracing_snapshot_instance(struct trace_array *tr)
{
	tracing_snapshot_instance_cond(tr, NULL);
}

/**
 * tracing_snapshot_cond - conditionally take a snapshot of the current buffer.
 * @tr:		The tracing instance to snapshot
 * @cond_data:	The data to be tested conditionally, and possibly saved
 *
 * This is the same as tracing_snapshot() except that the snapshot is
 * conditional - the snapshot will only happen if the
 * cond_snapshot.update() implementation receiving the cond_data
 * returns true, which means that the trace array's cond_snapshot
 * update() operation used the cond_data to determine whether the
 * snapshot should be taken, and if it was, presumably saved it along
 * with the snapshot.
 */
void tracing_snapshot_cond(struct trace_array *tr, void *cond_data)
{
	tracing_snapshot_instance_cond(tr, cond_data);
}
EXPORT_SYMBOL_GPL(tracing_snapshot_cond);

/**
 * tracing_cond_snapshot_data - get the user data associated with a snapshot
 * @tr:		The tracing instance
 *
 * When the user enables a conditional snapshot using
 * tracing_snapshot_cond_enable(), the user-defined cond_data is saved
 * with the snapshot.  This accessor is used to retrieve it.
 *
 * Should not be called from cond_snapshot.update(), since it takes
 * the tr->max_lock lock, which the code calling
 * cond_snapshot.update() has already done.
 *
 * Returns the cond_data associated with the trace array's snapshot.
 */
void *tracing_cond_snapshot_data(struct trace_array *tr)
{
	void *cond_data = NULL;

	local_irq_disable();
	arch_spin_lock(&tr->max_lock);

	if (tr->cond_snapshot)
		cond_data = tr->cond_snapshot->cond_data;

	arch_spin_unlock(&tr->max_lock);
	local_irq_enable();

	return cond_data;
}
EXPORT_SYMBOL_GPL(tracing_cond_snapshot_data);

/* resize @tr's buffer to the size of @size_tr's entries */
int resize_buffer_duplicate_size(struct array_buffer *trace_buf,
				 struct array_buffer *size_buf, int cpu_id)
{
	int cpu, ret = 0;

	if (cpu_id == RING_BUFFER_ALL_CPUS) {
		for_each_tracing_cpu(cpu) {
			ret = ring_buffer_resize(trace_buf->buffer,
				 per_cpu_ptr(size_buf->data, cpu)->entries, cpu);
			if (ret < 0)
				break;
			per_cpu_ptr(trace_buf->data, cpu)->entries =
				per_cpu_ptr(size_buf->data, cpu)->entries;
		}
	} else {
		ret = ring_buffer_resize(trace_buf->buffer,
				 per_cpu_ptr(size_buf->data, cpu_id)->entries, cpu_id);
		if (ret == 0)
			per_cpu_ptr(trace_buf->data, cpu_id)->entries =
				per_cpu_ptr(size_buf->data, cpu_id)->entries;
	}

	return ret;
}

int tracing_alloc_snapshot_instance(struct trace_array *tr)
{
	int order;
	int ret;

	if (!tr->allocated_snapshot) {

		/* Make the snapshot buffer have the same order as main buffer */
		order = ring_buffer_subbuf_order_get(tr->array_buffer.buffer);
		ret = ring_buffer_subbuf_order_set(tr->snapshot_buffer.buffer, order);
		if (ret < 0)
			return ret;

		/* allocate spare buffer */
		ret = resize_buffer_duplicate_size(&tr->snapshot_buffer,
				   &tr->array_buffer, RING_BUFFER_ALL_CPUS);
		if (ret < 0)
			return ret;

		tr->allocated_snapshot = true;
	}

	return 0;
}

void free_snapshot(struct trace_array *tr)
{
	/*
	 * We don't free the ring buffer. instead, resize it because
	 * The max_tr ring buffer has some state (e.g. ring->clock) and
	 * we want preserve it.
	 */
	ring_buffer_subbuf_order_set(tr->snapshot_buffer.buffer, 0);
	ring_buffer_resize(tr->snapshot_buffer.buffer, 1, RING_BUFFER_ALL_CPUS);
	trace_set_buffer_entries(&tr->snapshot_buffer, 1);
	tracing_reset_online_cpus(&tr->snapshot_buffer);
	tr->allocated_snapshot = false;
}

int tracing_arm_snapshot_locked(struct trace_array *tr)
{
	int ret;

	lockdep_assert_held(&trace_types_lock);

	spin_lock(&tr->snapshot_trigger_lock);
	if (tr->snapshot == UINT_MAX || tr->mapped) {
		spin_unlock(&tr->snapshot_trigger_lock);
		return -EBUSY;
	}

	tr->snapshot++;
	spin_unlock(&tr->snapshot_trigger_lock);

	ret = tracing_alloc_snapshot_instance(tr);
	if (ret) {
		spin_lock(&tr->snapshot_trigger_lock);
		tr->snapshot--;
		spin_unlock(&tr->snapshot_trigger_lock);
	}

	return ret;
}

int tracing_arm_snapshot(struct trace_array *tr)
{
	guard(mutex)(&trace_types_lock);
	return tracing_arm_snapshot_locked(tr);
}

void tracing_disarm_snapshot(struct trace_array *tr)
{
	spin_lock(&tr->snapshot_trigger_lock);
	if (!WARN_ON(!tr->snapshot))
		tr->snapshot--;
	spin_unlock(&tr->snapshot_trigger_lock);
}

/**
 * tracing_snapshot_alloc - allocate and take a snapshot of the current buffer.
 *
 * This is similar to tracing_snapshot(), but it will allocate the
 * snapshot buffer if it isn't already allocated. Use this only
 * where it is safe to sleep, as the allocation may sleep.
 *
 * This causes a swap between the snapshot buffer and the current live
 * tracing buffer. You can use this to take snapshots of the live
 * trace when some condition is triggered, but continue to trace.
 */
void tracing_snapshot_alloc(void)
{
	int ret;

	ret = tracing_alloc_snapshot();
	if (ret < 0)
		return;

	tracing_snapshot();
}
EXPORT_SYMBOL_GPL(tracing_snapshot_alloc);

/**
 * tracing_snapshot_cond_enable - enable conditional snapshot for an instance
 * @tr:		The tracing instance
 * @cond_data:	User data to associate with the snapshot
 * @update:	Implementation of the cond_snapshot update function
 *
 * Check whether the conditional snapshot for the given instance has
 * already been enabled, or if the current tracer is already using a
 * snapshot; if so, return -EBUSY, else create a cond_snapshot and
 * save the cond_data and update function inside.
 *
 * Returns 0 if successful, error otherwise.
 */
int tracing_snapshot_cond_enable(struct trace_array *tr, void *cond_data,
				 cond_update_fn_t update)
{
	struct cond_snapshot *cond_snapshot __free(kfree) =
		kzalloc_obj(*cond_snapshot);
	int ret;

	if (!cond_snapshot)
		return -ENOMEM;

	cond_snapshot->cond_data = cond_data;
	cond_snapshot->update = update;

	guard(mutex)(&trace_types_lock);

	if (tracer_uses_snapshot(tr->current_trace))
		return -EBUSY;

	/*
	 * The cond_snapshot can only change to NULL without the
	 * trace_types_lock. We don't care if we race with it going
	 * to NULL, but we want to make sure that it's not set to
	 * something other than NULL when we get here, which we can
	 * do safely with only holding the trace_types_lock and not
	 * having to take the max_lock.
	 */
	if (tr->cond_snapshot)
		return -EBUSY;

	ret = tracing_arm_snapshot_locked(tr);
	if (ret)
		return ret;

	local_irq_disable();
	arch_spin_lock(&tr->max_lock);
	tr->cond_snapshot = no_free_ptr(cond_snapshot);
	arch_spin_unlock(&tr->max_lock);
	local_irq_enable();

	return 0;
}
EXPORT_SYMBOL_GPL(tracing_snapshot_cond_enable);

/**
 * tracing_snapshot_cond_disable - disable conditional snapshot for an instance
 * @tr:		The tracing instance
 *
 * Check whether the conditional snapshot for the given instance is
 * enabled; if so, free the cond_snapshot associated with it,
 * otherwise return -EINVAL.
 *
 * Returns 0 if successful, error otherwise.
 */
int tracing_snapshot_cond_disable(struct trace_array *tr)
{
	int ret = 0;

	local_irq_disable();
	arch_spin_lock(&tr->max_lock);

	if (!tr->cond_snapshot)
		ret = -EINVAL;
	else {
		kfree(tr->cond_snapshot);
		tr->cond_snapshot = NULL;
	}

	arch_spin_unlock(&tr->max_lock);
	local_irq_enable();

	tracing_disarm_snapshot(tr);

	return ret;
}
EXPORT_SYMBOL_GPL(tracing_snapshot_cond_disable);

#ifdef CONFIG_TRACER_MAX_TRACE
#ifdef LATENCY_FS_NOTIFY
static struct workqueue_struct *fsnotify_wq;

static void latency_fsnotify_workfn(struct work_struct *work)
{
	struct trace_array *tr = container_of(work, struct trace_array,
					      fsnotify_work);
	fsnotify_inode(tr->d_max_latency->d_inode, FS_MODIFY);
}

static void latency_fsnotify_workfn_irq(struct irq_work *iwork)
{
	struct trace_array *tr = container_of(iwork, struct trace_array,
					      fsnotify_irqwork);
	queue_work(fsnotify_wq, &tr->fsnotify_work);
}

__init static int latency_fsnotify_init(void)
{
	fsnotify_wq = alloc_workqueue("tr_max_lat_wq",
				      WQ_UNBOUND | WQ_HIGHPRI, 0);
	if (!fsnotify_wq) {
		pr_err("Unable to allocate tr_max_lat_wq\n");
		return -ENOMEM;
	}
	return 0;
}

late_initcall_sync(latency_fsnotify_init);

void latency_fsnotify(struct trace_array *tr)
{
	if (!fsnotify_wq)
		return;
	/*
	 * We cannot call queue_work(&tr->fsnotify_work) from here because it's
	 * possible that we are called from __schedule() or do_idle(), which
	 * could cause a deadlock.
	 */
	irq_work_queue(&tr->fsnotify_irqwork);
}
#endif /* LATENCY_FS_NOTIFY */

static const struct file_operations tracing_max_lat_fops;

void trace_create_maxlat_file(struct trace_array *tr,
			      struct dentry *d_tracer)
{
#ifdef LATENCY_FS_NOTIFY
	INIT_WORK(&tr->fsnotify_work, latency_fsnotify_workfn);
	init_irq_work(&tr->fsnotify_irqwork, latency_fsnotify_workfn_irq);
#endif
	tr->d_max_latency = trace_create_file("tracing_max_latency",
					      TRACE_MODE_WRITE,
					      d_tracer, tr,
					      &tracing_max_lat_fops);
}

/*
 * Copy the new maximum trace into the separate maximum-trace
 * structure. (this way the maximum trace is permanently saved,
 * for later retrieval via /sys/kernel/tracing/tracing_max_latency)
 */
static void
__update_max_tr(struct trace_array *tr, struct task_struct *tsk, int cpu)
{
	struct array_buffer *trace_buf = &tr->array_buffer;
	struct trace_array_cpu *data = per_cpu_ptr(trace_buf->data, cpu);
	struct array_buffer *max_buf = &tr->snapshot_buffer;
	struct trace_array_cpu *max_data = per_cpu_ptr(max_buf->data, cpu);

	max_buf->cpu = cpu;
	max_buf->time_start = data->preempt_timestamp;

	max_data->saved_latency = tr->max_latency;
	max_data->critical_start = data->critical_start;
	max_data->critical_end = data->critical_end;

	strscpy(max_data->comm, tsk->comm);
	max_data->pid = tsk->pid;
	/*
	 * If tsk == current, then use current_uid(), as that does not use
	 * RCU. The irq tracer can be called out of RCU scope.
	 */
	if (tsk == current)
		max_data->uid = current_uid();
	else
		max_data->uid = task_uid(tsk);

	max_data->nice = tsk->static_prio - 20 - MAX_RT_PRIO;
	max_data->policy = tsk->policy;
	max_data->rt_priority = tsk->rt_priority;

	/* record this tasks comm */
	tracing_record_cmdline(tsk);
	latency_fsnotify(tr);
}
#else
static inline void __update_max_tr(struct trace_array *tr,
				   struct task_struct *tsk, int cpu) { }
#endif /* CONFIG_TRACER_MAX_TRACE */

/**
 * update_max_tr - snapshot all trace buffers from global_trace to max_tr
 * @tr: tracer
 * @tsk: the task with the latency
 * @cpu: The cpu that initiated the trace.
 * @cond_data: User data associated with a conditional snapshot
 *
 * Flip the buffers between the @tr and the max_tr and record information
 * about which task was the cause of this latency.
 */
void
update_max_tr(struct trace_array *tr, struct task_struct *tsk, int cpu,
	      void *cond_data)
{
	if (tr->stop_count)
		return;

	WARN_ON_ONCE(!irqs_disabled());

	if (!tr->allocated_snapshot) {
		/* Only the nop tracer should hit this when disabling */
		WARN_ON_ONCE(tr->current_trace != &nop_trace);
		return;
	}

	arch_spin_lock(&tr->max_lock);

	/* Inherit the recordable setting from array_buffer */
	if (ring_buffer_record_is_set_on(tr->array_buffer.buffer))
		ring_buffer_record_on(tr->snapshot_buffer.buffer);
	else
		ring_buffer_record_off(tr->snapshot_buffer.buffer);

	if (tr->cond_snapshot && !tr->cond_snapshot->update(tr, cond_data)) {
		arch_spin_unlock(&tr->max_lock);
		return;
	}

	swap(tr->array_buffer.buffer, tr->snapshot_buffer.buffer);

	__update_max_tr(tr, tsk, cpu);

	arch_spin_unlock(&tr->max_lock);

	/* Any waiters on the old snapshot buffer need to wake up */
	ring_buffer_wake_waiters(tr->array_buffer.buffer, RING_BUFFER_ALL_CPUS);
}

/**
 * update_max_tr_single - only copy one trace over, and reset the rest
 * @tr: tracer
 * @tsk: task with the latency
 * @cpu: the cpu of the buffer to copy.
 *
 * Flip the trace of a single CPU buffer between the @tr and the max_tr.
 */
void
update_max_tr_single(struct trace_array *tr, struct task_struct *tsk, int cpu)
{
	int ret;

	if (tr->stop_count)
		return;

	WARN_ON_ONCE(!irqs_disabled());
	if (!tr->allocated_snapshot) {
		/* Only the nop tracer should hit this when disabling */
		WARN_ON_ONCE(tr->current_trace != &nop_trace);
		return;
	}

	arch_spin_lock(&tr->max_lock);

	ret = ring_buffer_swap_cpu(tr->snapshot_buffer.buffer, tr->array_buffer.buffer, cpu);

	if (ret == -EBUSY) {
		/*
		 * We failed to swap the buffer due to a commit taking
		 * place on this CPU. We fail to record, but we reset
		 * the max trace buffer (no one writes directly to it)
		 * and flag that it failed.
		 * Another reason is resize is in progress.
		 */
		trace_array_printk_buf(tr->snapshot_buffer.buffer, _THIS_IP_,
			"Failed to swap buffers due to commit or resize in progress\n");
	}

	WARN_ON_ONCE(ret && ret != -EAGAIN && ret != -EBUSY);

	__update_max_tr(tr, tsk, cpu);
	arch_spin_unlock(&tr->max_lock);
}

static void show_snapshot_main_help(struct seq_file *m)
{
	seq_puts(m, "# echo 0 > snapshot : Clears and frees snapshot buffer\n"
		    "# echo 1 > snapshot : Allocates snapshot buffer, if not already allocated.\n"
		    "#                      Takes a snapshot of the main buffer.\n"
		    "# echo 2 > snapshot : Clears snapshot buffer (but does not allocate or free)\n"
		    "#                      (Doesn't have to be '2' works with any number that\n"
		    "#                       is not a '0' or '1')\n");
}

static void show_snapshot_percpu_help(struct seq_file *m)
{
	seq_puts(m, "# echo 0 > snapshot : Invalid for per_cpu snapshot file.\n");
#ifdef CONFIG_RING_BUFFER_ALLOW_SWAP
	seq_puts(m, "# echo 1 > snapshot : Allocates snapshot buffer, if not already allocated.\n"
		    "#                      Takes a snapshot of the main buffer for this cpu.\n");
#else
	seq_puts(m, "# echo 1 > snapshot : Not supported with this kernel.\n"
		    "#                     Must use main snapshot file to allocate.\n");
#endif
	seq_puts(m, "# echo 2 > snapshot : Clears this cpu's snapshot buffer (but does not allocate)\n"
		    "#                      (Doesn't have to be '2' works with any number that\n"
		    "#                       is not a '0' or '1')\n");
}

void print_snapshot_help(struct seq_file *m, struct trace_iterator *iter)
{
	if (iter->tr->allocated_snapshot)
		seq_puts(m, "#\n# * Snapshot is allocated *\n#\n");
	else
		seq_puts(m, "#\n# * Snapshot is freed *\n#\n");

	seq_puts(m, "# Snapshot commands:\n");
	if (iter->cpu_file == RING_BUFFER_ALL_CPUS)
		show_snapshot_main_help(m);
	else
		show_snapshot_percpu_help(m);
}

static int tracing_snapshot_open(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;
	struct trace_iterator *iter;
	struct seq_file *m;
	int ret;

	ret = tracing_check_open_get_tr(tr);
	if (ret)
		return ret;

	if (file->f_mode & FMODE_READ) {
		iter = __tracing_open(inode, file, true);
		if (IS_ERR(iter))
			ret = PTR_ERR(iter);
	} else {
		/* Writes still need the seq_file to hold the private data */
		ret = -ENOMEM;
		m = kzalloc_obj(*m);
		if (!m)
			goto out;
		iter = kzalloc_obj(*iter);
		if (!iter) {
			kfree(m);
			goto out;
		}
		ret = 0;

		iter->tr = tr;
		iter->array_buffer = &tr->snapshot_buffer;
		iter->cpu_file = tracing_get_cpu(inode);
		m->private = iter;
		file->private_data = m;
	}
out:
	if (ret < 0)
		trace_array_put(tr);

	return ret;
}

static void tracing_swap_cpu_buffer(void *tr)
{
	update_max_tr_single((struct trace_array *)tr, current, smp_processor_id());
}

static ssize_t
tracing_snapshot_write(struct file *filp, const char __user *ubuf, size_t cnt,
		       loff_t *ppos)
{
	struct seq_file *m = filp->private_data;
	struct trace_iterator *iter = m->private;
	struct trace_array *tr = iter->tr;
	unsigned long val;
	int ret;

	ret = tracing_update_buffers(tr);
	if (ret < 0)
		return ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	guard(mutex)(&trace_types_lock);

	if (tracer_uses_snapshot(tr->current_trace))
		return -EBUSY;

	local_irq_disable();
	arch_spin_lock(&tr->max_lock);
	if (tr->cond_snapshot)
		ret = -EBUSY;
	arch_spin_unlock(&tr->max_lock);
	local_irq_enable();
	if (ret)
		return ret;

	switch (val) {
	case 0:
		if (iter->cpu_file != RING_BUFFER_ALL_CPUS)
			return -EINVAL;
		if (tr->allocated_snapshot)
			free_snapshot(tr);
		break;
	case 1:
/* Only allow per-cpu swap if the ring buffer supports it */
#ifndef CONFIG_RING_BUFFER_ALLOW_SWAP
		if (iter->cpu_file != RING_BUFFER_ALL_CPUS)
			return -EINVAL;
#endif
		if (tr->allocated_snapshot)
			ret = resize_buffer_duplicate_size(&tr->snapshot_buffer,
					&tr->array_buffer, iter->cpu_file);

		ret = tracing_arm_snapshot_locked(tr);
		if (ret)
			return ret;

		/* Now, we're going to swap */
		if (iter->cpu_file == RING_BUFFER_ALL_CPUS) {
			local_irq_disable();
			update_max_tr(tr, current, smp_processor_id(), NULL);
			local_irq_enable();
		} else {
			smp_call_function_single(iter->cpu_file, tracing_swap_cpu_buffer,
						 (void *)tr, 1);
		}
		tracing_disarm_snapshot(tr);
		break;
	default:
		if (tr->allocated_snapshot) {
			if (iter->cpu_file == RING_BUFFER_ALL_CPUS)
				tracing_reset_online_cpus(&tr->snapshot_buffer);
			else
				tracing_reset_cpu(&tr->snapshot_buffer, iter->cpu_file);
		}
		break;
	}

	if (ret >= 0) {
		*ppos += cnt;
		ret = cnt;
	}

	return ret;
}

static int tracing_snapshot_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	int ret;

	ret = tracing_release(inode, file);

	if (file->f_mode & FMODE_READ)
		return ret;

	/* If write only, the seq_file is just a stub */
	if (m)
		kfree(m->private);
	kfree(m);

	return 0;
}

static int snapshot_raw_open(struct inode *inode, struct file *filp)
{
	struct ftrace_buffer_info *info;
	int ret;

	/* The following checks for tracefs lockdown */
	ret = tracing_buffers_open(inode, filp);
	if (ret < 0)
		return ret;

	info = filp->private_data;

	if (tracer_uses_snapshot(info->iter.trace)) {
		tracing_buffers_release(inode, filp);
		return -EBUSY;
	}

	info->iter.snapshot = true;
	info->iter.array_buffer = &info->iter.tr->snapshot_buffer;

	return ret;
}

const struct file_operations snapshot_fops = {
	.open		= tracing_snapshot_open,
	.read		= seq_read,
	.write		= tracing_snapshot_write,
	.llseek		= tracing_lseek,
	.release	= tracing_snapshot_release,
};

const struct file_operations snapshot_raw_fops = {
	.open		= snapshot_raw_open,
	.read		= tracing_buffers_read,
	.release	= tracing_buffers_release,
	.splice_read	= tracing_buffers_splice_read,
};

#ifdef CONFIG_TRACER_MAX_TRACE
static ssize_t
tracing_max_lat_read(struct file *filp, char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;

	return tracing_nsecs_read(&tr->max_latency, ubuf, cnt, ppos);
}

static ssize_t
tracing_max_lat_write(struct file *filp, const char __user *ubuf,
		      size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;

	return tracing_nsecs_write(&tr->max_latency, ubuf, cnt, ppos);
}

static const struct file_operations tracing_max_lat_fops = {
	.open		= tracing_open_generic_tr,
	.read		= tracing_max_lat_read,
	.write		= tracing_max_lat_write,
	.llseek		= generic_file_llseek,
	.release	= tracing_release_generic_tr,
};
#endif /* CONFIG_TRACER_MAX_TRACE */

int get_snapshot_map(struct trace_array *tr)
{
	int err = 0;

	/*
	 * Called with mmap_lock held. lockdep would be unhappy if we would now
	 * take trace_types_lock. Instead use the specific
	 * snapshot_trigger_lock.
	 */
	spin_lock(&tr->snapshot_trigger_lock);

	if (tr->snapshot || tr->mapped == UINT_MAX)
		err = -EBUSY;
	else
		tr->mapped++;

	spin_unlock(&tr->snapshot_trigger_lock);

	/* Wait for update_max_tr() to observe iter->tr->mapped */
	if (tr->mapped == 1)
		synchronize_rcu();

	return err;

}

void put_snapshot_map(struct trace_array *tr)
{
	spin_lock(&tr->snapshot_trigger_lock);
	if (!WARN_ON(!tr->mapped))
		tr->mapped--;
	spin_unlock(&tr->snapshot_trigger_lock);
}

#ifdef CONFIG_DYNAMIC_FTRACE
static void
ftrace_snapshot(unsigned long ip, unsigned long parent_ip,
		struct trace_array *tr, struct ftrace_probe_ops *ops,
		void *data)
{
	tracing_snapshot_instance(tr);
}

static void
ftrace_count_snapshot(unsigned long ip, unsigned long parent_ip,
		      struct trace_array *tr, struct ftrace_probe_ops *ops,
		      void *data)
{
	struct ftrace_func_mapper *mapper = data;
	long *count = NULL;

	if (mapper)
		count = (long *)ftrace_func_mapper_find_ip(mapper, ip);

	if (count) {

		if (*count <= 0)
			return;

		(*count)--;
	}

	tracing_snapshot_instance(tr);
}

static int
ftrace_snapshot_print(struct seq_file *m, unsigned long ip,
		      struct ftrace_probe_ops *ops, void *data)
{
	struct ftrace_func_mapper *mapper = data;
	long *count = NULL;

	seq_printf(m, "%ps:", (void *)ip);

	seq_puts(m, "snapshot");

	if (mapper)
		count = (long *)ftrace_func_mapper_find_ip(mapper, ip);

	if (count)
		seq_printf(m, ":count=%ld\n", *count);
	else
		seq_puts(m, ":unlimited\n");

	return 0;
}

static int
ftrace_snapshot_init(struct ftrace_probe_ops *ops, struct trace_array *tr,
		     unsigned long ip, void *init_data, void **data)
{
	struct ftrace_func_mapper *mapper = *data;

	if (!mapper) {
		mapper = allocate_ftrace_func_mapper();
		if (!mapper)
			return -ENOMEM;
		*data = mapper;
	}

	return ftrace_func_mapper_add_ip(mapper, ip, init_data);
}

static void
ftrace_snapshot_free(struct ftrace_probe_ops *ops, struct trace_array *tr,
		     unsigned long ip, void *data)
{
	struct ftrace_func_mapper *mapper = data;

	if (!ip) {
		if (!mapper)
			return;
		free_ftrace_func_mapper(mapper, NULL);
		return;
	}

	ftrace_func_mapper_remove_ip(mapper, ip);
}

static struct ftrace_probe_ops snapshot_probe_ops = {
	.func			= ftrace_snapshot,
	.print			= ftrace_snapshot_print,
};

static struct ftrace_probe_ops snapshot_count_probe_ops = {
	.func			= ftrace_count_snapshot,
	.print			= ftrace_snapshot_print,
	.init			= ftrace_snapshot_init,
	.free			= ftrace_snapshot_free,
};

static int
ftrace_trace_snapshot_callback(struct trace_array *tr, struct ftrace_hash *hash,
			       char *glob, char *cmd, char *param, int enable)
{
	struct ftrace_probe_ops *ops;
	void *count = (void *)-1;
	char *number;
	int ret;

	if (!tr)
		return -ENODEV;

	/* hash funcs only work with set_ftrace_filter */
	if (!enable)
		return -EINVAL;

	ops = param ? &snapshot_count_probe_ops :  &snapshot_probe_ops;

	if (glob[0] == '!') {
		ret = unregister_ftrace_function_probe_func(glob+1, tr, ops);
		if (!ret)
			tracing_disarm_snapshot(tr);

		return ret;
	}

	if (!param)
		goto out_reg;

	number = strsep(&param, ":");

	if (!strlen(number))
		goto out_reg;

	/*
	 * We use the callback data field (which is a pointer)
	 * as our counter.
	 */
	ret = kstrtoul(number, 0, (unsigned long *)&count);
	if (ret)
		return ret;

 out_reg:
	ret = tracing_arm_snapshot(tr);
	if (ret < 0)
		return ret;

	ret = register_ftrace_function_probe(glob, tr, ops, count);
	if (ret < 0)
		tracing_disarm_snapshot(tr);

	return ret < 0 ? ret : 0;
}

static struct ftrace_func_command ftrace_snapshot_cmd = {
	.name			= "snapshot",
	.func			= ftrace_trace_snapshot_callback,
};

__init int register_snapshot_cmd(void)
{
	return register_ftrace_command(&ftrace_snapshot_cmd);
}
#endif /* CONFIG_DYNAMIC_FTRACE */

int trace_allocate_snapshot(struct trace_array *tr, int size)
{
	int ret;

	/* Fix mapped buffer trace arrays do not have snapshot buffers */
	if (tr->range_addr_start)
		return 0;

	/* allocate_snapshot can only be true during system boot */
	ret = allocate_trace_buffer(tr, &tr->snapshot_buffer,
				    allocate_snapshot ? size : 1);
	if (ret < 0)
		return -ENOMEM;

	tr->allocated_snapshot = allocate_snapshot;

	allocate_snapshot = false;
	return 0;
}

__init static bool tr_needs_alloc_snapshot(const char *name)
{
	char *test;
	int len = strlen(name);
	bool ret;

	if (!boot_snapshot_index)
		return false;

	if (strncmp(name, boot_snapshot_info, len) == 0 &&
	    boot_snapshot_info[len] == '\t')
		return true;

	test = kmalloc(strlen(name) + 3, GFP_KERNEL);
	if (!test)
		return false;

	sprintf(test, "\t%s\t", name);
	ret = strstr(boot_snapshot_info, test) == NULL;
	kfree(test);
	return ret;
}

__init void do_allocate_snapshot(const char *name)
{
	if (!tr_needs_alloc_snapshot(name))
		return;

	/*
	 * When allocate_snapshot is set, the next call to
	 * allocate_trace_buffers() (called by trace_array_get_by_name())
	 * will allocate the snapshot buffer. That will also clear
	 * this flag.
	 */
	allocate_snapshot = true;
}

void __init ftrace_boot_snapshot(void)
{
	struct trace_array *tr;

	if (!snapshot_at_boot)
		return;

	list_for_each_entry(tr, &ftrace_trace_arrays, list) {
		if (!tr->allocated_snapshot)
			continue;

		tracing_snapshot_instance(tr);
		trace_array_puts(tr, "** Boot snapshot taken **\n");
	}
}
