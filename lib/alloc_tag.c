// SPDX-License-Identifier: GPL-2.0-only
#include <linux/alloc_tag.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/page_ext.h>
#include <linux/proc_fs.h>
#include <linux/seq_buf.h>
#include <linux/seq_file.h>

static struct codetag_type *alloc_tag_cttype;

DEFINE_PER_CPU(struct alloc_tag_counters, _shared_alloc_tag);
EXPORT_SYMBOL(_shared_alloc_tag);

DEFINE_STATIC_KEY_MAYBE(CONFIG_MEM_ALLOC_PROFILING_ENABLED_BY_DEFAULT,
			mem_alloc_profiling_key);

struct allocinfo_private {
	struct codetag_iterator iter;
	bool print_header;
};

static void *allocinfo_start(struct seq_file *m, loff_t *pos)
{
	struct allocinfo_private *priv;
	struct codetag *ct;
	loff_t node = *pos;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	m->private = priv;
	if (!priv)
		return NULL;

	priv->print_header = (node == 0);
	codetag_lock_module_list(alloc_tag_cttype, true);
	priv->iter = codetag_get_ct_iter(alloc_tag_cttype);
	while ((ct = codetag_next_ct(&priv->iter)) != NULL && node)
		node--;

	return ct ? priv : NULL;
}

static void *allocinfo_next(struct seq_file *m, void *arg, loff_t *pos)
{
	struct allocinfo_private *priv = (struct allocinfo_private *)arg;
	struct codetag *ct = codetag_next_ct(&priv->iter);

	(*pos)++;
	if (!ct)
		return NULL;

	return priv;
}

static void allocinfo_stop(struct seq_file *m, void *arg)
{
	struct allocinfo_private *priv = (struct allocinfo_private *)m->private;

	if (priv) {
		codetag_lock_module_list(alloc_tag_cttype, false);
		kfree(priv);
	}
}

static void print_allocinfo_header(struct seq_buf *buf)
{
	/* Output format version, so we can change it. */
	seq_buf_printf(buf, "allocinfo - version: 1.0\n");
	seq_buf_printf(buf, "#     <size>  <calls> <tag info>\n");
}

static void alloc_tag_to_text(struct seq_buf *out, struct codetag *ct)
{
	struct alloc_tag *tag = ct_to_alloc_tag(ct);
	struct alloc_tag_counters counter = alloc_tag_read(tag);
	s64 bytes = counter.bytes;

	seq_buf_printf(out, "%12lli %8llu ", bytes, counter.calls);
	codetag_to_text(out, ct);
	seq_buf_putc(out, ' ');
	seq_buf_putc(out, '\n');
}

static int allocinfo_show(struct seq_file *m, void *arg)
{
	struct allocinfo_private *priv = (struct allocinfo_private *)arg;
	char *bufp;
	size_t n = seq_get_buf(m, &bufp);
	struct seq_buf buf;

	seq_buf_init(&buf, bufp, n);
	if (priv->print_header) {
		print_allocinfo_header(&buf);
		priv->print_header = false;
	}
	alloc_tag_to_text(&buf, priv->iter.ct);
	seq_commit(m, seq_buf_used(&buf));
	return 0;
}

static const struct seq_operations allocinfo_seq_op = {
	.start	= allocinfo_start,
	.next	= allocinfo_next,
	.stop	= allocinfo_stop,
	.show	= allocinfo_show,
};

size_t alloc_tag_top_users(struct codetag_bytes *tags, size_t count, bool can_sleep)
{
	struct codetag_iterator iter;
	struct codetag *ct;
	struct codetag_bytes n;
	unsigned int i, nr = 0;

	if (can_sleep)
		codetag_lock_module_list(alloc_tag_cttype, true);
	else if (!codetag_trylock_module_list(alloc_tag_cttype))
		return 0;

	iter = codetag_get_ct_iter(alloc_tag_cttype);
	while ((ct = codetag_next_ct(&iter))) {
		struct alloc_tag_counters counter = alloc_tag_read(ct_to_alloc_tag(ct));

		n.ct	= ct;
		n.bytes = counter.bytes;

		for (i = 0; i < nr; i++)
			if (n.bytes > tags[i].bytes)
				break;

		if (i < count) {
			nr -= nr == count;
			memmove(&tags[i + 1],
				&tags[i],
				sizeof(tags[0]) * (nr - i));
			nr++;
			tags[i] = n;
		}
	}

	codetag_lock_module_list(alloc_tag_cttype, false);

	return nr;
}

static void __init procfs_init(void)
{
	proc_create_seq("allocinfo", 0400, NULL, &allocinfo_seq_op);
}

static bool alloc_tag_module_unload(struct codetag_type *cttype,
				    struct codetag_module *cmod)
{
	struct codetag_iterator iter = codetag_get_ct_iter(cttype);
	struct alloc_tag_counters counter;
	bool module_unused = true;
	struct alloc_tag *tag;
	struct codetag *ct;

	for (ct = codetag_next_ct(&iter); ct; ct = codetag_next_ct(&iter)) {
		if (iter.cmod != cmod)
			continue;

		tag = ct_to_alloc_tag(ct);
		counter = alloc_tag_read(tag);

		if (WARN(counter.bytes,
			 "%s:%u module %s func:%s has %llu allocated at module unload",
			 ct->filename, ct->lineno, ct->modname, ct->function, counter.bytes))
			module_unused = false;
	}

	return module_unused;
}

#ifdef CONFIG_MEM_ALLOC_PROFILING_ENABLED_BY_DEFAULT
static bool mem_profiling_support __meminitdata = true;
#else
static bool mem_profiling_support __meminitdata;
#endif

static int __init setup_early_mem_profiling(char *str)
{
	bool enable;

	if (!str || !str[0])
		return -EINVAL;

	if (!strncmp(str, "never", 5)) {
		enable = false;
		mem_profiling_support = false;
	} else {
		int res;

		res = kstrtobool(str, &enable);
		if (res)
			return res;

		mem_profiling_support = true;
	}

	if (enable != static_key_enabled(&mem_alloc_profiling_key)) {
		if (enable)
			static_branch_enable(&mem_alloc_profiling_key);
		else
			static_branch_disable(&mem_alloc_profiling_key);
	}

	return 0;
}
early_param("sysctl.vm.mem_profiling", setup_early_mem_profiling);

static __init bool need_page_alloc_tagging(void)
{
	return mem_profiling_support;
}

static __init void init_page_alloc_tagging(void)
{
}

struct page_ext_operations page_alloc_tagging_ops = {
	.size = sizeof(union codetag_ref),
	.need = need_page_alloc_tagging,
	.init = init_page_alloc_tagging,
};
EXPORT_SYMBOL(page_alloc_tagging_ops);

static struct ctl_table memory_allocation_profiling_sysctls[] = {
	{
		.procname	= "mem_profiling",
		.data		= &mem_alloc_profiling_key,
#ifdef CONFIG_MEM_ALLOC_PROFILING_DEBUG
		.mode		= 0444,
#else
		.mode		= 0644,
#endif
		.proc_handler	= proc_do_static_key,
	},
	{ }
};

static int __init alloc_tag_init(void)
{
	const struct codetag_type_desc desc = {
		.section	= "alloc_tags",
		.tag_size	= sizeof(struct alloc_tag),
		.module_unload	= alloc_tag_module_unload,
	};

	alloc_tag_cttype = codetag_register_type(&desc);
	if (IS_ERR(alloc_tag_cttype))
		return PTR_ERR(alloc_tag_cttype);

	if (!mem_profiling_support)
		memory_allocation_profiling_sysctls[0].mode = 0444;
	register_sysctl_init("vm", memory_allocation_profiling_sysctls);
	procfs_init();

	return 0;
}
module_init(alloc_tag_init);
