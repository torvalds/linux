// SPDX-License-Identifier: GPL-2.0
/*
 * trace binary printk
 *
 * Copyright (C) 2008 Lai Jiangshan <laijs@cn.fujitsu.com>
 *
 */
#include <linux/seq_file.h>
#include <linux/security.h>
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

	if (!fmt)
		return ERR_PTR(-EINVAL);

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
			if (!IS_ERR(tb_fmt))
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
	return NOTIFY_OK;
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
 * holds the format, we simply use container_of() and then go to the
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
	return NOTIFY_OK;
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

bool trace_is_tracepoint_string(const char *str)
{
	const char **ptr = __start___tracepoint_str;

	for (ptr = __start___tracepoint_str; ptr < __stop___tracepoint_str; ptr++) {
		if (str == *ptr)
			return true;
	}
	return false;
}

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
	int ret;

	ret = security_locked_down(LOCKDOWN_TRACEFS);
	if (ret)
		return ret;

	return seq_open(file, &show_format_seq_ops);
}

static const struct file_operations ftrace_formats_fops = {
	.open = ftrace_formats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static __always_inline bool printk_binsafe(struct trace_array *tr)
{
	/*
	 * The binary format of traceprintk can cause a crash if used
	 * by a buffer from another boot. Force the use of the
	 * non binary version of trace_printk if the trace_printk
	 * buffer is a boot mapped ring buffer.
	 */
	return !(tr->flags & TRACE_ARRAY_FL_BOOT);
}

int __trace_array_puts(struct trace_array *tr, unsigned long ip,
		       const char *str, int size)
{
	struct ring_buffer_event *event;
	struct trace_buffer *buffer;
	struct print_entry *entry;
	unsigned int trace_ctx;
	int alloc;

	if (!(tr->trace_flags & TRACE_ITER(PRINTK)))
		return 0;

	if (unlikely(tracing_selftest_running &&
		     (tr->flags & TRACE_ARRAY_FL_GLOBAL)))
		return 0;

	if (unlikely(tracing_disabled))
		return 0;

	alloc = sizeof(*entry) + size + 2; /* possible \n added */

	trace_ctx = tracing_gen_ctx();
	buffer = tr->array_buffer.buffer;
	guard(ring_buffer_nest)(buffer);
	event = __trace_buffer_lock_reserve(buffer, TRACE_PRINT, alloc,
					    trace_ctx);
	if (!event)
		return 0;

	entry = ring_buffer_event_data(event);
	entry->ip = ip;

	memcpy(&entry->buf, str, size);

	/* Add a newline if necessary */
	if (entry->buf[size - 1] != '\n') {
		entry->buf[size] = '\n';
		entry->buf[size + 1] = '\0';
	} else
		entry->buf[size] = '\0';

	__buffer_unlock_commit(buffer, event);
	ftrace_trace_stack(tr, buffer, trace_ctx, 4, NULL);
	return size;
}
EXPORT_SYMBOL_GPL(__trace_array_puts);

/**
 * __trace_puts - write a constant string into the trace buffer.
 * @ip:	   The address of the caller
 * @str:   The constant string to write
 */
int __trace_puts(unsigned long ip, const char *str)
{
	return __trace_array_puts(printk_trace, ip, str, strlen(str));
}
EXPORT_SYMBOL_GPL(__trace_puts);

/**
 * __trace_bputs - write the pointer to a constant string into trace buffer
 * @ip:	   The address of the caller
 * @str:   The constant string to write to the buffer to
 */
int __trace_bputs(unsigned long ip, const char *str)
{
	struct trace_array *tr = READ_ONCE(printk_trace);
	struct ring_buffer_event *event;
	struct trace_buffer *buffer;
	struct bputs_entry *entry;
	unsigned int trace_ctx;
	int size = sizeof(struct bputs_entry);

	if (!printk_binsafe(tr))
		return __trace_puts(ip, str);

	if (!(tr->trace_flags & TRACE_ITER(PRINTK)))
		return 0;

	if (unlikely(tracing_selftest_running || tracing_disabled))
		return 0;

	trace_ctx = tracing_gen_ctx();
	buffer = tr->array_buffer.buffer;

	guard(ring_buffer_nest)(buffer);
	event = __trace_buffer_lock_reserve(buffer, TRACE_BPUTS, size,
					    trace_ctx);
	if (!event)
		return 0;

	entry = ring_buffer_event_data(event);
	entry->ip			= ip;
	entry->str			= str;

	__buffer_unlock_commit(buffer, event);
	ftrace_trace_stack(tr, buffer, trace_ctx, 4, NULL);

	return 1;
}
EXPORT_SYMBOL_GPL(__trace_bputs);

/* created for use with alloc_percpu */
struct trace_buffer_struct {
	int nesting;
	char buffer[4][TRACE_BUF_SIZE];
};

static struct trace_buffer_struct __percpu *trace_percpu_buffer;

/*
 * This allows for lockless recording.  If we're nested too deeply, then
 * this returns NULL.
 */
static char *get_trace_buf(void)
{
	struct trace_buffer_struct *buffer = this_cpu_ptr(trace_percpu_buffer);

	if (!trace_percpu_buffer || buffer->nesting >= 4)
		return NULL;

	buffer->nesting++;

	/* Interrupts must see nesting incremented before we use the buffer */
	barrier();
	return &buffer->buffer[buffer->nesting - 1][0];
}

static void put_trace_buf(void)
{
	/* Don't let the decrement of nesting leak before this */
	barrier();
	this_cpu_dec(trace_percpu_buffer->nesting);
}

static int alloc_percpu_trace_buffer(void)
{
	struct trace_buffer_struct __percpu *buffers;

	if (trace_percpu_buffer)
		return 0;

	buffers = alloc_percpu(struct trace_buffer_struct);
	if (MEM_FAIL(!buffers, "Could not allocate percpu trace_printk buffer"))
		return -ENOMEM;

	trace_percpu_buffer = buffers;
	return 0;
}

static int buffers_allocated;

void trace_printk_init_buffers(void)
{
	if (buffers_allocated)
		return;

	if (alloc_percpu_trace_buffer())
		return;

	/* trace_printk() is for debug use only. Don't use it in production. */

	pr_warn("\n");
	pr_warn("**********************************************************\n");
	pr_warn("**   NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE   **\n");
	pr_warn("**                                                      **\n");
	pr_warn("** trace_printk() being used. Allocating extra memory.  **\n");
	pr_warn("**                                                      **\n");
	pr_warn("** This means that this is a DEBUG kernel and it is     **\n");
	pr_warn("** unsafe for production use.                           **\n");
	pr_warn("**                                                      **\n");
	pr_warn("** If you see this message and you are not debugging    **\n");
	pr_warn("** the kernel, report this immediately to your vendor!  **\n");
	pr_warn("**                                                      **\n");
	pr_warn("**   NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE   **\n");
	pr_warn("**********************************************************\n");

	/* Expand the buffers to set size */
	if (tracing_update_buffers(NULL) < 0)
		pr_err("Failed to expand tracing buffers for trace_printk() calls\n");
	else
		buffers_allocated = 1;

	/*
	 * trace_printk_init_buffers() can be called by modules.
	 * If that happens, then we need to start cmdline recording
	 * directly here.
	 */
	if (system_state == SYSTEM_RUNNING)
		tracing_start_cmdline_record();
}
EXPORT_SYMBOL_GPL(trace_printk_init_buffers);

void trace_printk_start_comm(void)
{
	/* Start tracing comms if trace printk is set */
	if (!buffers_allocated)
		return;
	tracing_start_cmdline_record();
}

void trace_printk_start_stop_comm(int enabled)
{
	if (!buffers_allocated)
		return;

	if (enabled)
		tracing_start_cmdline_record();
	else
		tracing_stop_cmdline_record();
}

/**
 * trace_vbprintk - write binary msg to tracing buffer
 * @ip:    The address of the caller
 * @fmt:   The string format to write to the buffer
 * @args:  Arguments for @fmt
 */
int trace_vbprintk(unsigned long ip, const char *fmt, va_list args)
{
	struct ring_buffer_event *event;
	struct trace_buffer *buffer;
	struct trace_array *tr = READ_ONCE(printk_trace);
	struct bprint_entry *entry;
	unsigned int trace_ctx;
	char *tbuffer;
	int len = 0, size;

	if (!printk_binsafe(tr))
		return trace_vprintk(ip, fmt, args);

	if (unlikely(tracing_selftest_running || tracing_disabled))
		return 0;

	/* Don't pollute graph traces with trace_vprintk internals */
	pause_graph_tracing();

	trace_ctx = tracing_gen_ctx();
	guard(preempt_notrace)();

	tbuffer = get_trace_buf();
	if (!tbuffer) {
		len = 0;
		goto out_nobuffer;
	}

	len = vbin_printf((u32 *)tbuffer, TRACE_BUF_SIZE/sizeof(int), fmt, args);

	if (len > TRACE_BUF_SIZE/sizeof(int) || len < 0)
		goto out_put;

	size = sizeof(*entry) + sizeof(u32) * len;
	buffer = tr->array_buffer.buffer;
	scoped_guard(ring_buffer_nest, buffer) {
		event = __trace_buffer_lock_reserve(buffer, TRACE_BPRINT, size,
						    trace_ctx);
		if (!event)
			goto out_put;
		entry = ring_buffer_event_data(event);
		entry->ip			= ip;
		entry->fmt			= fmt;

		memcpy(entry->buf, tbuffer, sizeof(u32) * len);
		__buffer_unlock_commit(buffer, event);
		ftrace_trace_stack(tr, buffer, trace_ctx, 6, NULL);
	}
out_put:
	put_trace_buf();

out_nobuffer:
	unpause_graph_tracing();

	return len;
}
EXPORT_SYMBOL_GPL(trace_vbprintk);

static __printf(3, 0)
int __trace_array_vprintk(struct trace_buffer *buffer,
			  unsigned long ip, const char *fmt, va_list args)
{
	struct ring_buffer_event *event;
	int len = 0, size;
	struct print_entry *entry;
	unsigned int trace_ctx;
	char *tbuffer;

	if (unlikely(tracing_disabled))
		return 0;

	/* Don't pollute graph traces with trace_vprintk internals */
	pause_graph_tracing();

	trace_ctx = tracing_gen_ctx();
	guard(preempt_notrace)();


	tbuffer = get_trace_buf();
	if (!tbuffer) {
		len = 0;
		goto out_nobuffer;
	}

	len = vscnprintf(tbuffer, TRACE_BUF_SIZE, fmt, args);

	size = sizeof(*entry) + len + 1;
	scoped_guard(ring_buffer_nest, buffer) {
		event = __trace_buffer_lock_reserve(buffer, TRACE_PRINT, size,
						    trace_ctx);
		if (!event)
			goto out;
		entry = ring_buffer_event_data(event);
		entry->ip = ip;

		memcpy(&entry->buf, tbuffer, len + 1);
		__buffer_unlock_commit(buffer, event);
		ftrace_trace_stack(printk_trace, buffer, trace_ctx, 6, NULL);
	}
out:
	put_trace_buf();

out_nobuffer:
	unpause_graph_tracing();

	return len;
}

int trace_array_vprintk(struct trace_array *tr,
			unsigned long ip, const char *fmt, va_list args)
{
	if (tracing_selftest_running && (tr->flags & TRACE_ARRAY_FL_GLOBAL))
		return 0;

	return __trace_array_vprintk(tr->array_buffer.buffer, ip, fmt, args);
}

/**
 * trace_array_printk - Print a message to a specific instance
 * @tr: The instance trace_array descriptor
 * @ip: The instruction pointer that this is called from.
 * @fmt: The format to print (printf format)
 *
 * If a subsystem sets up its own instance, they have the right to
 * printk strings into their tracing instance buffer using this
 * function. Note, this function will not write into the top level
 * buffer (use trace_printk() for that), as writing into the top level
 * buffer should only have events that can be individually disabled.
 * trace_printk() is only used for debugging a kernel, and should not
 * be ever incorporated in normal use.
 *
 * trace_array_printk() can be used, as it will not add noise to the
 * top level tracing buffer.
 *
 * Note, trace_array_init_printk() must be called on @tr before this
 * can be used.
 */
int trace_array_printk(struct trace_array *tr,
		       unsigned long ip, const char *fmt, ...)
{
	int ret;
	va_list ap;

	if (!tr)
		return -ENOENT;

	/* This is only allowed for created instances */
	if (tr->flags & TRACE_ARRAY_FL_GLOBAL)
		return 0;

	if (!(tr->trace_flags & TRACE_ITER(PRINTK)))
		return 0;

	va_start(ap, fmt);
	ret = trace_array_vprintk(tr, ip, fmt, ap);
	va_end(ap);
	return ret;
}
EXPORT_SYMBOL_GPL(trace_array_printk);

/**
 * trace_array_init_printk - Initialize buffers for trace_array_printk()
 * @tr: The trace array to initialize the buffers for
 *
 * As trace_array_printk() only writes into instances, they are OK to
 * have in the kernel (unlike trace_printk()). This needs to be called
 * before trace_array_printk() can be used on a trace_array.
 */
int trace_array_init_printk(struct trace_array *tr)
{
	if (!tr)
		return -ENOENT;

	/* This is only allowed for created instances */
	if (tr->flags & TRACE_ARRAY_FL_GLOBAL)
		return -EINVAL;

	return alloc_percpu_trace_buffer();
}
EXPORT_SYMBOL_GPL(trace_array_init_printk);

int trace_array_printk_buf(struct trace_buffer *buffer,
			   unsigned long ip, const char *fmt, ...)
{
	int ret;
	va_list ap;

	if (!(printk_trace->trace_flags & TRACE_ITER(PRINTK)))
		return 0;

	va_start(ap, fmt);
	ret = __trace_array_vprintk(buffer, ip, fmt, ap);
	va_end(ap);
	return ret;
}

int trace_vprintk(unsigned long ip, const char *fmt, va_list args)
{
	return trace_array_vprintk(printk_trace, ip, fmt, args);
}
EXPORT_SYMBOL_GPL(trace_vprintk);

static __init int init_trace_printk_function_export(void)
{
	int ret;

	ret = tracing_init_dentry();
	if (ret)
		return 0;

	trace_create_file("printk_formats", TRACE_MODE_READ, NULL,
				    NULL, &ftrace_formats_fops);

	return 0;
}

fs_initcall(init_trace_printk_function_export);

static __init int init_trace_printk(void)
{
	return register_module_notifier(&module_trace_bprintk_format_nb);
}

early_initcall(init_trace_printk);
