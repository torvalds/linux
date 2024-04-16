// SPDX-License-Identifier: GPL-2.0
/*
 * Userspace indexing of printk formats
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string_helpers.h>

#include "internal.h"

extern struct pi_entry *__start_printk_index[];
extern struct pi_entry *__stop_printk_index[];

/* The base dir for module formats, typically debugfs/printk/index/ */
static struct dentry *dfs_index;

static struct pi_entry *pi_get_entry(const struct module *mod, loff_t pos)
{
	struct pi_entry **entries;
	unsigned int nr_entries;

#ifdef CONFIG_MODULES
	if (mod) {
		entries = mod->printk_index_start;
		nr_entries = mod->printk_index_size;
	} else
#endif
	{
		/* vmlinux, comes from linker symbols */
		entries = __start_printk_index;
		nr_entries = __stop_printk_index - __start_printk_index;
	}

	if (pos >= nr_entries)
		return NULL;

	return entries[pos];
}

static void *pi_next(struct seq_file *s, void *v, loff_t *pos)
{
	const struct module *mod = s->file->f_inode->i_private;
	struct pi_entry *entry = pi_get_entry(mod, *pos);

	(*pos)++;

	return entry;
}

static void *pi_start(struct seq_file *s, loff_t *pos)
{
	/*
	 * Make show() print the header line. Do not update *pos because
	 * pi_next() still has to return the entry at index 0 later.
	 */
	if (*pos == 0)
		return SEQ_START_TOKEN;

	return pi_next(s, NULL, pos);
}

/*
 * We need both ESCAPE_ANY and explicit characters from ESCAPE_SPECIAL in @only
 * because otherwise ESCAPE_NAP will cause double quotes and backslashes to be
 * ignored for quoting.
 */
#define seq_escape_printf_format(s, src) \
	seq_escape_str(s, src, ESCAPE_ANY | ESCAPE_NAP | ESCAPE_APPEND, "\"\\")

static int pi_show(struct seq_file *s, void *v)
{
	const struct pi_entry *entry = v;
	int level = LOGLEVEL_DEFAULT;
	enum printk_info_flags flags = 0;
	u16 prefix_len = 0;

	if (v == SEQ_START_TOKEN) {
		seq_puts(s, "# <level/flags> filename:line function \"format\"\n");
		return 0;
	}

	if (!entry->fmt)
		return 0;

	if (entry->level)
		printk_parse_prefix(entry->level, &level, &flags);
	else
		prefix_len = printk_parse_prefix(entry->fmt, &level, &flags);


	if (flags & LOG_CONT) {
		/*
		 * LOGLEVEL_DEFAULT here means "use the same level as the
		 * message we're continuing from", not the default message
		 * loglevel, so don't display it as such.
		 */
		if (level == LOGLEVEL_DEFAULT)
			seq_puts(s, "<c>");
		else
			seq_printf(s, "<%d,c>", level);
	} else
		seq_printf(s, "<%d>", level);

	seq_printf(s, " %s:%d %s \"", entry->file, entry->line, entry->func);
	if (entry->subsys_fmt_prefix)
		seq_escape_printf_format(s, entry->subsys_fmt_prefix);
	seq_escape_printf_format(s, entry->fmt + prefix_len);
	seq_puts(s, "\"\n");

	return 0;
}

static void pi_stop(struct seq_file *p, void *v) { }

static const struct seq_operations dfs_index_sops = {
	.start = pi_start,
	.next  = pi_next,
	.show  = pi_show,
	.stop  = pi_stop,
};

DEFINE_SEQ_ATTRIBUTE(dfs_index);

#ifdef CONFIG_MODULES
static const char *pi_get_module_name(struct module *mod)
{
	return mod ? mod->name : "vmlinux";
}
#else
static const char *pi_get_module_name(struct module *mod)
{
	return "vmlinux";
}
#endif

static void pi_create_file(struct module *mod)
{
	debugfs_create_file(pi_get_module_name(mod), 0444, dfs_index,
				       mod, &dfs_index_fops);
}

#ifdef CONFIG_MODULES
static void pi_remove_file(struct module *mod)
{
	debugfs_lookup_and_remove(pi_get_module_name(mod), dfs_index);
}

static int pi_module_notify(struct notifier_block *nb, unsigned long op,
			    void *data)
{
	struct module *mod = data;

	switch (op) {
	case MODULE_STATE_COMING:
		pi_create_file(mod);
		break;
	case MODULE_STATE_GOING:
		pi_remove_file(mod);
		break;
	default: /* we don't care about other module states */
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block module_printk_fmts_nb = {
	.notifier_call = pi_module_notify,
};

static void __init pi_setup_module_notifier(void)
{
	register_module_notifier(&module_printk_fmts_nb);
}
#else
static inline void __init pi_setup_module_notifier(void) { }
#endif

static int __init pi_init(void)
{
	struct dentry *dfs_root = debugfs_create_dir("printk", NULL);

	dfs_index = debugfs_create_dir("index", dfs_root);
	pi_setup_module_notifier();
	pi_create_file(NULL);

	return 0;
}

/* debugfs comes up on core and must be initialised first */
postcore_initcall(pi_init);
