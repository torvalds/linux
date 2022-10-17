/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TRACE_H
#define _LINUX_TRACE_H

#define TRACE_EXPORT_FUNCTION	BIT(0)
#define TRACE_EXPORT_EVENT	BIT(1)
#define TRACE_EXPORT_MARKER	BIT(2)

/*
 * The trace export - an export of Ftrace output. The trace_export
 * can process traces and export them to a registered destination as
 * an addition to the current only output of Ftrace - i.e. ring buffer.
 *
 * If you want traces to be sent to some other place rather than ring
 * buffer only, just need to register a new trace_export and implement
 * its own .write() function for writing traces to the storage.
 *
 * next		- pointer to the next trace_export
 * write	- copy traces which have been delt with ->commit() to
 *		  the destination
 * flags	- which ftrace to be exported
 */
struct trace_export {
	struct trace_export __rcu	*next;
	void (*write)(struct trace_export *, const void *, unsigned int);
	int flags;
};

#ifdef CONFIG_TRACING

int register_ftrace_export(struct trace_export *export);
int unregister_ftrace_export(struct trace_export *export);

struct trace_array;

void trace_printk_init_buffers(void);
__printf(3, 4)
int trace_array_printk(struct trace_array *tr, unsigned long ip,
		       const char *fmt, ...);
int trace_array_init_printk(struct trace_array *tr);
void trace_array_put(struct trace_array *tr);
struct trace_array *trace_array_get_by_name(const char *name);
int trace_array_destroy(struct trace_array *tr);

/* For osnoise tracer */
int osnoise_arch_register(void);
void osnoise_arch_unregister(void);
void osnoise_trace_irq_entry(int id);
void osnoise_trace_irq_exit(int id, const char *desc);

#else /* CONFIG_TRACING */
static inline int register_ftrace_export(struct trace_export *export)
{
	return -EINVAL;
}
static inline int unregister_ftrace_export(struct trace_export *export)
{
	return 0;
}
static inline void trace_printk_init_buffers(void)
{
}
static inline int trace_array_printk(struct trace_array *tr, unsigned long ip,
				     const char *fmt, ...)
{
	return 0;
}
static inline int trace_array_init_printk(struct trace_array *tr)
{
	return -EINVAL;
}
static inline void trace_array_put(struct trace_array *tr)
{
}
static inline struct trace_array *trace_array_get_by_name(const char *name)
{
	return NULL;
}
static inline int trace_array_destroy(struct trace_array *tr)
{
	return 0;
}
#endif	/* CONFIG_TRACING */

#endif	/* _LINUX_TRACE_H */
