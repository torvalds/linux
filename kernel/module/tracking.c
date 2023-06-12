// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Module taint unload tracking support
 *
 * Copyright (C) 2022 Aaron Tomlin
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/rculist.h>
#include "internal.h"

static LIST_HEAD(unloaded_tainted_modules);
extern struct dentry *mod_debugfs_root;

int try_add_tainted_module(struct module *mod)
{
	struct mod_unload_taint *mod_taint;

	module_assert_mutex_or_preempt();

	if (!mod->taints)
		goto out;

	list_for_each_entry_rcu(mod_taint, &unloaded_tainted_modules, list,
				lockdep_is_held(&module_mutex)) {
		if (!strcmp(mod_taint->name, mod->name) &&
		    mod_taint->taints & mod->taints) {
			mod_taint->count++;
			goto out;
		}
	}

	mod_taint = kmalloc(sizeof(*mod_taint), GFP_KERNEL);
	if (unlikely(!mod_taint))
		return -ENOMEM;
	strscpy(mod_taint->name, mod->name, MODULE_NAME_LEN);
	mod_taint->taints = mod->taints;
	list_add_rcu(&mod_taint->list, &unloaded_tainted_modules);
	mod_taint->count = 1;
out:
	return 0;
}

void print_unloaded_tainted_modules(void)
{
	struct mod_unload_taint *mod_taint;
	char buf[MODULE_FLAGS_BUF_SIZE];

	if (!list_empty(&unloaded_tainted_modules)) {
		printk(KERN_DEFAULT "Unloaded tainted modules:");
		list_for_each_entry_rcu(mod_taint, &unloaded_tainted_modules,
					list) {
			size_t l;

			l = module_flags_taint(mod_taint->taints, buf);
			buf[l++] = '\0';
			pr_cont(" %s(%s):%llu", mod_taint->name, buf,
				mod_taint->count);
		}
	}
}

#ifdef CONFIG_DEBUG_FS
static void *unloaded_tainted_modules_seq_start(struct seq_file *m, loff_t *pos)
	__acquires(rcu)
{
	rcu_read_lock();
	return seq_list_start_rcu(&unloaded_tainted_modules, *pos);
}

static void *unloaded_tainted_modules_seq_next(struct seq_file *m, void *p, loff_t *pos)
{
	return seq_list_next_rcu(p, &unloaded_tainted_modules, pos);
}

static void unloaded_tainted_modules_seq_stop(struct seq_file *m, void *p)
	__releases(rcu)
{
	rcu_read_unlock();
}

static int unloaded_tainted_modules_seq_show(struct seq_file *m, void *p)
{
	struct mod_unload_taint *mod_taint;
	char buf[MODULE_FLAGS_BUF_SIZE];
	size_t l;

	mod_taint = list_entry(p, struct mod_unload_taint, list);
	l = module_flags_taint(mod_taint->taints, buf);
	buf[l++] = '\0';

	seq_printf(m, "%s (%s) %llu", mod_taint->name, buf, mod_taint->count);
	seq_puts(m, "\n");

	return 0;
}

static const struct seq_operations unloaded_tainted_modules_seq_ops = {
	.start = unloaded_tainted_modules_seq_start,
	.next  = unloaded_tainted_modules_seq_next,
	.stop  = unloaded_tainted_modules_seq_stop,
	.show  = unloaded_tainted_modules_seq_show,
};

static int unloaded_tainted_modules_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &unloaded_tainted_modules_seq_ops);
}

static const struct file_operations unloaded_tainted_modules_fops = {
	.open = unloaded_tainted_modules_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int __init unloaded_tainted_modules_init(void)
{
	debugfs_create_file("unloaded_tainted", 0444, mod_debugfs_root, NULL,
			    &unloaded_tainted_modules_fops);
	return 0;
}
module_init(unloaded_tainted_modules_init);
#endif /* CONFIG_DEBUG_FS */
