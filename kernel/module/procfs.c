// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Module proc support
 *
 * Copyright (C) 2008 Alexey Dobriyan
 */

#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include "internal.h"

#ifdef CONFIG_MODULE_UNLOAD
static inline void print_unload_info(struct seq_file *m, struct module *mod)
{
	struct module_use *use;
	int printed_something = 0;

	seq_printf(m, " %i ", module_refcount(mod));

	/*
	 * Always include a trailing , so userspace can differentiate
	 * between this and the old multi-field proc format.
	 */
	list_for_each_entry(use, &mod->source_list, source_list) {
		printed_something = 1;
		seq_printf(m, "%s,", use->source->name);
	}

	if (mod->init && !mod->exit) {
		printed_something = 1;
		seq_puts(m, "[permanent],");
	}

	if (!printed_something)
		seq_puts(m, "-");
}
#else /* !CONFIG_MODULE_UNLOAD */
static inline void print_unload_info(struct seq_file *m, struct module *mod)
{
	/* We don't know the usage count, or what modules are using. */
	seq_puts(m, " - -");
}
#endif /* CONFIG_MODULE_UNLOAD */

/* Called by the /proc file system to return a list of modules. */
static void *m_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&module_mutex);
	return seq_list_start(&modules, *pos);
}

static void *m_next(struct seq_file *m, void *p, loff_t *pos)
{
	return seq_list_next(p, &modules, pos);
}

static void m_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&module_mutex);
}

static int m_show(struct seq_file *m, void *p)
{
	struct module *mod = list_entry(p, struct module, list);
	char buf[MODULE_FLAGS_BUF_SIZE];
	void *value;
	unsigned int size;

	/* We always ignore unformed modules. */
	if (mod->state == MODULE_STATE_UNFORMED)
		return 0;

	size = mod->init_layout.size + mod->core_layout.size;
#ifdef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
	size += mod->data_layout.size;
#endif
	seq_printf(m, "%s %u", mod->name, size);
	print_unload_info(m, mod);

	/* Informative for users. */
	seq_printf(m, " %s",
		   mod->state == MODULE_STATE_GOING ? "Unloading" :
		   mod->state == MODULE_STATE_COMING ? "Loading" :
		   "Live");
	/* Used by oprofile and other similar tools. */
	value = m->private ? NULL : mod->core_layout.base;
	seq_printf(m, " 0x%px", value);

	/* Taints info */
	if (mod->taints)
		seq_printf(m, " %s", module_flags(mod, buf));

	seq_puts(m, "\n");
	return 0;
}

/*
 * Format: modulename size refcount deps address
 *
 * Where refcount is a number or -, and deps is a comma-separated list
 * of depends or -.
 */
static const struct seq_operations modules_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= m_show
};

/*
 * This also sets the "private" pointer to non-NULL if the
 * kernel pointers should be hidden (so you can just test
 * "m->private" to see if you should keep the values private).
 *
 * We use the same logic as for /proc/kallsyms.
 */
static int modules_open(struct inode *inode, struct file *file)
{
	int err = seq_open(file, &modules_op);

	if (!err) {
		struct seq_file *m = file->private_data;

		m->private = kallsyms_show_value(file->f_cred) ? NULL : (void *)8ul;
	}

	return err;
}

static const struct proc_ops modules_proc_ops = {
	.proc_flags	= PROC_ENTRY_PERMANENT,
	.proc_open	= modules_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
};

static int __init proc_modules_init(void)
{
	proc_create("modules", 0, NULL, &modules_proc_ops);
	return 0;
}
module_init(proc_modules_init);
