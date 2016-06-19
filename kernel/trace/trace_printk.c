/*
 * trace binary printk
 *
 * Copyright (C) 2008 Lai Jiangshan <laijs@cn.fujitsu.com>
 *
 */
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/ftrace.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/ctype.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "trace.h"

#ifdef CONFIG_MODULES

/*
 * modules trace_printk()'s formats are autosaved in struct trace_bprintk_fmt
 * which are queued on trace_bprintk_fmt_list.
 */
static LIST_HEAD(trace_bprintk_fmt_list);

/* serialize accesses to trace_bprintk_fmt_list */
static DEFINE_MUTEX(btrace_mutex);

struct trace_bprintk_fmt {
	struct list_head list;
	const char *fmt;
};

static inline struct trace_bprintk_fmt *lookup_format(const char *fmt)
{
	struct trace_bprintk_fmt *pos;
	list_for_each_entry(pos, &trace_bprintk_fmt_list, list) {
		if (!strcmp(pos->fmt, fmt))
			return pos;
	}
	return NULL;
}

static
void hold_module_trace_bprintk_format(const char **start, const char **end)
{
	const char **iter;
	char *fmt;

	/* allocate the trace_printk per cpu buffers */
	if (start != end)
		trace_printk_init_buffers();

	mutex_lock(&btrace_mutex);
	for (iter = start; iter < end; iter++) {
		struct trace_bprintk_fmt *tb_fmt = lookup_format(*iter);
		if (tb_fmt) {
			*iter = tb_fmt->fmt;
			continue;
		}

		fmt = NULL;
		tb_fmt = kmalloc(sizeof(*tb_fmt), GFP_KERNEL);
		if (tb_fmt) {
			fmt = kmalloc(strlen(*iter) + 1, GFP_KERNEL);
			if (fmt) {
				list_add_tail(&tb_fmt->list, &trace_bprintk_fmt_list);
				strcpy(fmt, *iter);
				tb_fmt->fmt = fmt;
			} else
				kfree(tb_fmt);
		}
		*iter = fmt;

	}
	mutex_unlock(&btrace_mutex);
}

static int module_trace_bprintk_format_notify(struct notifier_block *self,
		unsigned long val, void *data)
{
	struct module *mod = data;
	if (mod->num_trace_bprintk_fmt) {
		const char **start = mod->trace_bprintk_fmt_start;
		const char **end = start + mod->num_trace_bprintk_fmt;

		if (val == MODULE_STATE_COMING)
			hold_module_trace_bprintk_format(start, end);
	}
	return 0;
}

/*
 * The debugfs/tracing/printk_formats file maps the addresses with
 * the ASCII formats that are used in the bprintk events in the
 * buffer. For userspace tools to be able to decode the events from
 * the buffer, they need to be able to map the address with the format.
 *
 * The addresses of the bprintk formats are in their own section
 * __trace_printk_fmt. But for modules we copy them into a link list.
 * The code to print the formats and their addresses passes around the
 * address of the fmt string. If the fmt address passed into the seq
 * functions is within the kernel core __trace_printk_fmt section, then
 * it simply uses the next pointer in the list.
 *
 * When the fmt pointer is outside the kernel core __trace_printk_fmt
 * section, then we need to read the link list pointers. The trick is
 * we pass the address of the string to the seq function just like
 * we do for the kernel core formats. To get back the structure that
 * holds the format, we simply use containerof() and then go to the
 * next format in the list.
 */
static const char **
find_next_mod_format(int start_index, void *v, const char **fmt, loff_t *pos)
{
	struct trace_bprintk_fmt *mod_fmt;

	if (list_empty(&trace_bprintk_fmt_list))
		return NULL;

	/*
	 * v will point to the address of the fmt record from t_next
	 * v will be NULL from t_start.
	 * If this is the first pointer or called from start
	 * then we need to walk the list.
	 */
	if (!v || start_index == *pos) {
		struct trace_bprintk_fmt *p;

		/* search the module list */
		list_for_each_entry(p, &trace_bprintk_fmt_list, list) {
			if (start_index == *pos)
				return &p->fmt;
			start_index++;
		}
		/* pos > index */
		return NULL;
	}

	/*
	 * v points to the address of the fmt field in the mod list
	 * structure that holds the module print format.
	 */
	mod_fmt = container_of(v, typeof(*mod_fmt), fmt);
	if (mod_fmt->list.next == &trace_bprintk_fmt_list)
		return NULL;

	mod_fmt = container_of(mod_fmt->list.next, typeof(*mod_fmt), list);

	return &mod_fmt->fmt;
}

static void format_mod_start(void)
{
	mutex_lock(&btrace_mutex);
}

static void format_mod_stop(void)
{
	mutex_unlock(&btrace_mutex);
}

#else /* !CONFIG_MODULES */
__init static int
module_trace_bprintk_format_notify(struct notifier_block *self,
		unsigned long val, void *data)
{
	return 0;
}
static inline const char **
find_next_mod_format(int start_index, void *v, const char **fmt, loff_t *pos)
{
	return NULL;
}
static inline void format_mod_start(void) { }
static inline void format_mod_stop(void) { }
#endif /* CONFIG_MODULES */

static bool __read_mostly trace_printk_enabled = true;

void trace_printk_control(bool enabled)
{
	trace_printk_enabled = enabled;
}

__initdata_or_module static
struct notifier_block module_trace_bprintk_format_nb = {
	.notifier_call = module_trace_bprintk_format_notify,
};

int __trace_bprintk(unsigned long ip, const char *fmt, ...)
 {
	int ret;
	va_list ap;

	if (unlikely(!fmt))
		return 0;

	if (!trace_printk_enabled)
		return 0;

	va_start(ap, fmt);
	ret = trace_vbprintk(ip, fmt, ap);
	va_end(ap);
	return ret;
}
EXPORT_SYMBOL_GPL(__trace_bprintk);

int __ftrace_vbprintk(unsigned long ip, const char *fmt, va_list ap)
 {
	if (unlikely(!fmt))
		return 0;

	if (!trace_printk_enabled)
		return 0;

	return trace_vbprintk(ip, fmt, ap);
}
EXPORT_SYMBOL_GPL(__ftrace_vbprintk);

int __trace_printk(unsigned long ip, const char *fmt, ...)
{
	int ret;
	va_list ap;

	if (!trace_printk_enabled)
		return 0;

	va_start(ap, fmt);
	ret = trace_vprintk(ip, fmt, ap);
	va_end(ap);
	return ret;
}
EXPORT_SYMBOL_GPL(__trace_printk);

int __ftrace_vprintk(unsigned long ip, const char *fmt, va_list ap)
{
	if (!trace_printk_enabled)
		return 0;

	return trace_vprintk(ip, fmt, ap);
}
EXPORT_SYMBOL_GPL(__ftrace_vprintk);

static const char **find_next(void *v, loff_t *pos)
{
	const char **fmt = v;
	int start_index;
	int last_index;

	start_index = __stop___trace_bprintk_fmt - __start___trace_bprintk_fmt;

	if (*pos < start_index)
		return __start___trace_bprintk_fmt + *pos;

	/*
	 * The __tracepoint_str section is treated the same as the
	 * __trace_printk_fmt section. The difference is that the
	 * __trace_printk_fmt section should only be used by trace_printk()
	 * in a debugging environment, as if anything exists in that section
	 * the trace_prink() helper buffers are allocated, which would just
	 * waste space in a production environment.
	 *
	 * The __tracepoint_str sections on the other hand are used by
	 * tracepoints which need to map pointers to their strings to
	 * the ASCII text for userspace.
	 */
	last_index = start_index;
	start_index = __stop___tracepoint_str - __start___tracepoint_str;

	if (*pos < last_index + start_index)
		return __start___tracepoint_str + (*pos - last_index);

	start_index += last_index;
	return find_next_mod_format(start_index, v, fmt, pos);
}

static void *
t_start(struct seq_file *m, loff_t *pos)
{
	format_mod_start();
	return find_next(NULL, pos);
}

static void *t_next(struct seq_file *m, void * v, loff_t *pos)
{
	(*pos)++;
	return find_next(v, pos);
}

static int t_show(struct seq_file *m, void *v)
{
	const char **fmt = v;
	const char *str = *fmt;
	int i;

	if (!*fmt)
		return 0;

	seq_printf(m, "0x%lx : \"", *(unsigned long *)fmt);

	/*
	 * Tabs and new lines need to be converted.
	 */
	for (i = 0; str[i]; i++) {
		switch (str[i]) {
		case '\n':
			seq_puts(m, "\\n");
			break;
		case '\t':
			seq_puts(m, "\\t");
			break;
		case '\\':
			seq_putc(m, '\\');
			break;
		case '"':
			seq_puts(m, "\\\"");
			break;
		default:
			seq_putc(m, str[i]);
		}
	}
	seq_puts(m, "\"\n");

	return 0;
}

static void t_stop(struct seq_file *m, void *p)
{
	format_mod_stop();
}

static const struct seq_operations show_format_seq_ops = {
	.start = t_start,
	.next = t_next,
	.show = t_show,
	.stop = t_stop,
};

static int
ftrace_formats_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &show_format_seq_ops);
}

static const struct file_operations ftrace_formats_fops = {
	.open = ftrace_formats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static __init int init_trace_printk_function_export(void)
{
	struct dentry *d_tracer;

	d_tracer = tracing_init_dentry();
	if (IS_ERR(d_tracer))
		return 0;

	trace_create_file("printk_formats", 0444, d_tracer,
				    NULL, &ftrace_formats_fops);

	return 0;
}

fs_initcall(init_trace_printk_function_export);

static __init int init_trace_printk(void)
{
	return register_module_notifier(&module_trace_bprintk_format_nb);
}

early_initcall(init_trace_printk);
