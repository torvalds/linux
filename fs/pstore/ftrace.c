/*
 * Copyright 2012  Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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

	if (unlikely(oops_in_progress))
		return;

	local_irq_save(flags);

	rec.ip = ip;
	rec.parent_ip = parent_ip;
	pstore_ftrace_write_timestamp(&rec, pstore_ftrace_stamp++);
	pstore_ftrace_encode_cpu(&rec, raw_smp_processor_id());
	psinfo->write_buf(PSTORE_TYPE_FTRACE, 0, NULL, 0, (void *)&rec,
			  0, sizeof(rec), psinfo);

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
	struct dentry *file;

	if (!psinfo->write_buf)
		return;

	pstore_ftrace_dir = debugfs_create_dir("pstore", NULL);
	if (!pstore_ftrace_dir) {
		pr_err("%s: unable to create pstore directory\n", __func__);
		return;
	}

	file = debugfs_create_file("record_ftrace", 0600, pstore_ftrace_dir,
				   NULL, &pstore_knob_fops);
	if (!file) {
		pr_err("%s: unable to create record_ftrace file\n", __func__);
		goto err_file;
	}

	return;
err_file:
	debugfs_remove(pstore_ftrace_dir);
}

void pstore_unregister_ftrace(void)
{
	mutex_lock(&pstore_ftrace_lock);
	if (pstore_ftrace_enabled) {
		unregister_ftrace_function(&pstore_ftrace_ops);
		pstore_ftrace_enabled = 0;
	}
	mutex_unlock(&pstore_ftrace_lock);

	debugfs_remove_recursive(pstore_ftrace_dir);
}
