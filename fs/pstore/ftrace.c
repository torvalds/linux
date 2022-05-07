// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2012  Google, Inc.
 */

#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/ftrace.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/cache.h>
#include <linux/slab.h>
#include <asm/barrier.h>
#include "internal.h"

/* This doesn't need to be atomic: speed is chosen over correctness here. */
static u64 pstore_ftrace_stamp;

static void notrace pstore_ftrace_call(unsigned long ip,
				       unsigned long parent_ip,
				       struct ftrace_ops *op,
				       struct pt_regs *regs)
{
	unsigned long flags;
	struct pstore_ftrace_record rec = {};
	struct pstore_record record = {
		.type = PSTORE_TYPE_FTRACE,
		.buf = (char *)&rec,
		.size = sizeof(rec),
		.psi = psinfo,
	};

	if (unlikely(oops_in_progress))
		return;

	local_irq_save(flags);

	rec.ip = ip;
	rec.parent_ip = parent_ip;
	pstore_ftrace_write_timestamp(&rec, pstore_ftrace_stamp++);
	pstore_ftrace_encode_cpu(&rec, raw_smp_processor_id());
	psinfo->write(&record);

	local_irq_restore(flags);
}

static struct ftrace_ops pstore_ftrace_ops __read_mostly = {
	.func	= pstore_ftrace_call,
};

static DEFINE_MUTEX(pstore_ftrace_lock);
static bool pstore_ftrace_enabled;

static ssize_t pstore_ftrace_knob_write(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	u8 on;
	ssize_t ret;

	ret = kstrtou8_from_user(buf, count, 2, &on);
	if (ret)
		return ret;

	mutex_lock(&pstore_ftrace_lock);

	if (!on ^ pstore_ftrace_enabled)
		goto out;

	if (on) {
		ftrace_ops_set_global_filter(&pstore_ftrace_ops);
		ret = register_ftrace_function(&pstore_ftrace_ops);
	} else {
		ret = unregister_ftrace_function(&pstore_ftrace_ops);
	}

	if (ret) {
		pr_err("%s: unable to %sregister ftrace ops: %zd\n",
		       __func__, on ? "" : "un", ret);
		goto err;
	}

	pstore_ftrace_enabled = on;
out:
	ret = count;
err:
	mutex_unlock(&pstore_ftrace_lock);

	return ret;
}

static ssize_t pstore_ftrace_knob_read(struct file *f, char __user *buf,
				       size_t count, loff_t *ppos)
{
	char val[] = { '0' + pstore_ftrace_enabled, '\n' };

	return simple_read_from_buffer(buf, count, ppos, val, sizeof(val));
}

static const struct file_operations pstore_knob_fops = {
	.open	= simple_open,
	.read	= pstore_ftrace_knob_read,
	.write	= pstore_ftrace_knob_write,
};

static struct dentry *pstore_ftrace_dir;

void pstore_register_ftrace(void)
{
	if (!psinfo->write)
		return;

	pstore_ftrace_dir = debugfs_create_dir("pstore", NULL);

	debugfs_create_file("record_ftrace", 0600, pstore_ftrace_dir, NULL,
			    &pstore_knob_fops);
}

void pstore_unregister_ftrace(void)
{
	mutex_lock(&pstore_ftrace_lock);
	if (pstore_ftrace_enabled) {
		unregister_ftrace_function(&pstore_ftrace_ops);
		pstore_ftrace_enabled = false;
	}
	mutex_unlock(&pstore_ftrace_lock);

	debugfs_remove_recursive(pstore_ftrace_dir);
}

ssize_t pstore_ftrace_combine_log(char **dest_log, size_t *dest_log_size,
				  const char *src_log, size_t src_log_size)
{
	size_t dest_size, src_size, total, dest_off, src_off;
	size_t dest_idx = 0, src_idx = 0, merged_idx = 0;
	void *merged_buf;
	struct pstore_ftrace_record *drec, *srec, *mrec;
	size_t record_size = sizeof(struct pstore_ftrace_record);

	dest_off = *dest_log_size % record_size;
	dest_size = *dest_log_size - dest_off;

	src_off = src_log_size % record_size;
	src_size = src_log_size - src_off;

	total = dest_size + src_size;
	merged_buf = kmalloc(total, GFP_KERNEL);
	if (!merged_buf)
		return -ENOMEM;

	drec = (struct pstore_ftrace_record *)(*dest_log + dest_off);
	srec = (struct pstore_ftrace_record *)(src_log + src_off);
	mrec = (struct pstore_ftrace_record *)(merged_buf);

	while (dest_size > 0 && src_size > 0) {
		if (pstore_ftrace_read_timestamp(&drec[dest_idx]) <
		    pstore_ftrace_read_timestamp(&srec[src_idx])) {
			mrec[merged_idx++] = drec[dest_idx++];
			dest_size -= record_size;
		} else {
			mrec[merged_idx++] = srec[src_idx++];
			src_size -= record_size;
		}
	}

	while (dest_size > 0) {
		mrec[merged_idx++] = drec[dest_idx++];
		dest_size -= record_size;
	}

	while (src_size > 0) {
		mrec[merged_idx++] = srec[src_idx++];
		src_size -= record_size;
	}

	kfree(*dest_log);
	*dest_log = merged_buf;
	*dest_log_size = total;

	return 0;
}
EXPORT_SYMBOL_GPL(pstore_ftrace_combine_log);
