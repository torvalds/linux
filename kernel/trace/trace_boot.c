// SPDX-License-Identifier: GPL-2.0
/*
 * trace_boot.c
 * Tracing kernel boot-time
 */

#define pr_fmt(fmt)	"trace_boot: " fmt

#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/bootconfig.h>

#include "trace.h"

#define MAX_BUF_LEN 256

extern int trace_set_options(struct trace_array *tr, char *option);
extern int tracing_set_tracer(struct trace_array *tr, const char *buf);
extern ssize_t tracing_resize_ring_buffer(struct trace_array *tr,
					  unsigned long size, int cpu_id);

static void __init
trace_boot_set_ftrace_options(struct trace_array *tr, struct xbc_node *node)
{
	struct xbc_node *anode;
	const char *p;
	char buf[MAX_BUF_LEN];
	unsigned long v = 0;

	/* Common ftrace options */
	xbc_node_for_each_array_value(node, "options", anode, p) {
		if (strlcpy(buf, p, ARRAY_SIZE(buf)) >= ARRAY_SIZE(buf)) {
			pr_err("String is too long: %s\n", p);
			continue;
		}

		if (trace_set_options(tr, buf) < 0)
			pr_err("Failed to set option: %s\n", buf);
	}

	p = xbc_node_find_value(node, "trace_clock", NULL);
	if (p && *p != '\0') {
		if (tracing_set_clock(tr, p) < 0)
			pr_err("Failed to set trace clock: %s\n", p);
	}

	p = xbc_node_find_value(node, "buffer_size", NULL);
	if (p && *p != '\0') {
		v = memparse(p, NULL);
		if (v < PAGE_SIZE)
			pr_err("Buffer size is too small: %s\n", p);
		if (tracing_resize_ring_buffer(tr, v, RING_BUFFER_ALL_CPUS) < 0)
			pr_err("Failed to resize trace buffer to %s\n", p);
	}
}

#ifdef CONFIG_EVENT_TRACING
extern int ftrace_set_clr_event(struct trace_array *tr, char *buf, int set);

static void __init
trace_boot_enable_events(struct trace_array *tr, struct xbc_node *node)
{
	struct xbc_node *anode;
	char buf[MAX_BUF_LEN];
	const char *p;

	xbc_node_for_each_array_value(node, "events", anode, p) {
		if (strlcpy(buf, p, ARRAY_SIZE(buf)) >= ARRAY_SIZE(buf)) {
			pr_err("String is too long: %s\n", p);
			continue;
		}

		if (ftrace_set_clr_event(tr, buf, 1) < 0)
			pr_err("Failed to enable event: %s\n", p);
	}
}
#else
#define trace_boot_enable_events(tr, node) do {} while (0)
#endif

static void __init
trace_boot_enable_tracer(struct trace_array *tr, struct xbc_node *node)
{
	const char *p;

	p = xbc_node_find_value(node, "tracer", NULL);
	if (p && *p != '\0') {
		if (tracing_set_tracer(tr, p) < 0)
			pr_err("Failed to set given tracer: %s\n", p);
	}
}

static int __init trace_boot_init(void)
{
	struct xbc_node *trace_node;
	struct trace_array *tr;

	trace_node = xbc_find_node("ftrace");
	if (!trace_node)
		return 0;

	tr = top_trace_array();
	if (!tr)
		return 0;

	trace_boot_set_ftrace_options(tr, trace_node);
	trace_boot_enable_events(tr, trace_node);
	trace_boot_enable_tracer(tr, trace_node);

	return 0;
}

fs_initcall(trace_boot_init);
