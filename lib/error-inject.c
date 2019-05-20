// SPDX-License-Identifier: GPL-2.0
// error-inject.c: Function-level error injection table
#include <linux/error-injection.h>
#include <linux/debugfs.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/slab.h>

/* Whitelist of symbols that can be overridden for error injection. */
static LIST_HEAD(error_injection_list);
static DEFINE_MUTEX(ei_mutex);
struct ei_entry {
	struct list_head list;
	unsigned long start_addr;
	unsigned long end_addr;
	int etype;
	void *priv;
};

bool within_error_injection_list(unsigned long addr)
{
	struct ei_entry *ent;
	bool ret = false;

	mutex_lock(&ei_mutex);
	list_for_each_entry(ent, &error_injection_list, list) {
		if (addr >= ent->start_addr && addr < ent->end_addr) {
			ret = true;
			break;
		}
	}
	mutex_unlock(&ei_mutex);
	return ret;
}

int get_injectable_error_type(unsigned long addr)
{
	struct ei_entry *ent;

	list_for_each_entry(ent, &error_injection_list, list) {
		if (addr >= ent->start_addr && addr < ent->end_addr)
			return ent->etype;
	}
	return EI_ETYPE_NONE;
}

/*
 * Lookup and populate the error_injection_list.
 *
 * For safety reasons we only allow certain functions to be overridden with
 * bpf_error_injection, so we need to populate the list of the symbols that have
 * been marked as safe for overriding.
 */
static void populate_error_injection_list(struct error_injection_entry *start,
					  struct error_injection_entry *end,
					  void *priv)
{
	struct error_injection_entry *iter;
	struct ei_entry *ent;
	unsigned long entry, offset = 0, size = 0;

	mutex_lock(&ei_mutex);
	for (iter = start; iter < end; iter++) {
		entry = arch_deref_entry_point((void *)iter->addr);

		if (!kernel_text_address(entry) ||
		    !kallsyms_lookup_size_offset(entry, &size, &offset)) {
			pr_err("Failed to find error inject entry at %p\n",
				(void *)entry);
			continue;
		}

		ent = kmalloc(sizeof(*ent), GFP_KERNEL);
		if (!ent)
			break;
		ent->start_addr = entry;
		ent->end_addr = entry + size;
		ent->etype = iter->etype;
		ent->priv = priv;
		INIT_LIST_HEAD(&ent->list);
		list_add_tail(&ent->list, &error_injection_list);
	}
	mutex_unlock(&ei_mutex);
}

/* Markers of the _error_inject_whitelist section */
extern struct error_injection_entry __start_error_injection_whitelist[];
extern struct error_injection_entry __stop_error_injection_whitelist[];

static void __init populate_kernel_ei_list(void)
{
	populate_error_injection_list(__start_error_injection_whitelist,
				      __stop_error_injection_whitelist,
				      NULL);
}

#ifdef CONFIG_MODULES
static void module_load_ei_list(struct module *mod)
{
	if (!mod->num_ei_funcs)
		return;

	populate_error_injection_list(mod->ei_funcs,
				      mod->ei_funcs + mod->num_ei_funcs, mod);
}

static void module_unload_ei_list(struct module *mod)
{
	struct ei_entry *ent, *n;

	if (!mod->num_ei_funcs)
		return;

	mutex_lock(&ei_mutex);
	list_for_each_entry_safe(ent, n, &error_injection_list, list) {
		if (ent->priv == mod) {
			list_del_init(&ent->list);
			kfree(ent);
		}
	}
	mutex_unlock(&ei_mutex);
}

/* Module notifier call back, checking error injection table on the module */
static int ei_module_callback(struct notifier_block *nb,
			      unsigned long val, void *data)
{
	struct module *mod = data;

	if (val == MODULE_STATE_COMING)
		module_load_ei_list(mod);
	else if (val == MODULE_STATE_GOING)
		module_unload_ei_list(mod);

	return NOTIFY_DONE;
}

static struct notifier_block ei_module_nb = {
	.notifier_call = ei_module_callback,
	.priority = 0
};

static __init int module_ei_init(void)
{
	return register_module_notifier(&ei_module_nb);
}
#else /* !CONFIG_MODULES */
#define module_ei_init()	(0)
#endif

/*
 * error_injection/whitelist -- shows which functions can be overridden for
 * error injection.
 */
static void *ei_seq_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&ei_mutex);
	return seq_list_start(&error_injection_list, *pos);
}

static void ei_seq_stop(struct seq_file *m, void *v)
{
	mutex_unlock(&ei_mutex);
}

static void *ei_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	return seq_list_next(v, &error_injection_list, pos);
}

static const char *error_type_string(int etype)
{
	switch (etype) {
	case EI_ETYPE_NULL:
		return "NULL";
	case EI_ETYPE_ERRNO:
		return "ERRNO";
	case EI_ETYPE_ERRNO_NULL:
		return "ERRNO_NULL";
	default:
		return "(unknown)";
	}
}

static int ei_seq_show(struct seq_file *m, void *v)
{
	struct ei_entry *ent = list_entry(v, struct ei_entry, list);

	seq_printf(m, "%ps\t%s\n", (void *)ent->start_addr,
		   error_type_string(ent->etype));
	return 0;
}

static const struct seq_operations ei_seq_ops = {
	.start = ei_seq_start,
	.next  = ei_seq_next,
	.stop  = ei_seq_stop,
	.show  = ei_seq_show,
};

static int ei_open(struct inode *inode, struct file *filp)
{
	return seq_open(filp, &ei_seq_ops);
}

static const struct file_operations debugfs_ei_ops = {
	.open           = ei_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release,
};

static int __init ei_debugfs_init(void)
{
	struct dentry *dir, *file;

	dir = debugfs_create_dir("error_injection", NULL);
	if (!dir)
		return -ENOMEM;

	file = debugfs_create_file("list", 0444, dir, NULL, &debugfs_ei_ops);
	if (!file) {
		debugfs_remove(dir);
		return -ENOMEM;
	}

	return 0;
}

static int __init init_error_injection(void)
{
	populate_kernel_ei_list();

	if (!module_ei_init())
		ei_debugfs_init();

	return 0;
}
late_initcall(init_error_injection);
