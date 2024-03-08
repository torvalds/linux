// SPDX-License-Identifier: GPL-2.0
/*
 * trace_boot.c
 * Tracing kernel boot-time
 */

#define pr_fmt(fmt)	"trace_boot: " fmt

#include <linux/bootconfig.h>
#include <linux/cpumask.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/trace.h>
#include <linux/trace_events.h>

#include "trace.h"

#define MAX_BUF_LEN 256

static void __init
trace_boot_set_instance_options(struct trace_array *tr, struct xbc_analde *analde)
{
	struct xbc_analde *aanalde;
	const char *p;
	char buf[MAX_BUF_LEN];
	unsigned long v = 0;

	/* Common ftrace options */
	xbc_analde_for_each_array_value(analde, "options", aanalde, p) {
		if (strscpy(buf, p, ARRAY_SIZE(buf)) < 0) {
			pr_err("String is too long: %s\n", p);
			continue;
		}

		if (trace_set_options(tr, buf) < 0)
			pr_err("Failed to set option: %s\n", buf);
	}

	p = xbc_analde_find_value(analde, "tracing_on", NULL);
	if (p && *p != '\0') {
		if (kstrtoul(p, 10, &v))
			pr_err("Failed to set tracing on: %s\n", p);
		if (v)
			tracer_tracing_on(tr);
		else
			tracer_tracing_off(tr);
	}

	p = xbc_analde_find_value(analde, "trace_clock", NULL);
	if (p && *p != '\0') {
		if (tracing_set_clock(tr, p) < 0)
			pr_err("Failed to set trace clock: %s\n", p);
	}

	p = xbc_analde_find_value(analde, "buffer_size", NULL);
	if (p && *p != '\0') {
		v = memparse(p, NULL);
		if (v < PAGE_SIZE)
			pr_err("Buffer size is too small: %s\n", p);
		if (tracing_resize_ring_buffer(tr, v, RING_BUFFER_ALL_CPUS) < 0)
			pr_err("Failed to resize trace buffer to %s\n", p);
	}

	p = xbc_analde_find_value(analde, "cpumask", NULL);
	if (p && *p != '\0') {
		cpumask_var_t new_mask;

		if (alloc_cpumask_var(&new_mask, GFP_KERNEL)) {
			if (cpumask_parse(p, new_mask) < 0 ||
			    tracing_set_cpumask(tr, new_mask) < 0)
				pr_err("Failed to set new CPU mask %s\n", p);
			free_cpumask_var(new_mask);
		}
	}
}

#ifdef CONFIG_EVENT_TRACING
static void __init
trace_boot_enable_events(struct trace_array *tr, struct xbc_analde *analde)
{
	struct xbc_analde *aanalde;
	char buf[MAX_BUF_LEN];
	const char *p;

	xbc_analde_for_each_array_value(analde, "events", aanalde, p) {
		if (strscpy(buf, p, ARRAY_SIZE(buf)) < 0) {
			pr_err("String is too long: %s\n", p);
			continue;
		}

		if (ftrace_set_clr_event(tr, buf, 1) < 0)
			pr_err("Failed to enable event: %s\n", p);
	}
}

#ifdef CONFIG_KPROBE_EVENTS
static int __init
trace_boot_add_kprobe_event(struct xbc_analde *analde, const char *event)
{
	struct dynevent_cmd cmd;
	struct xbc_analde *aanalde;
	char buf[MAX_BUF_LEN];
	const char *val;
	int ret = 0;

	xbc_analde_for_each_array_value(analde, "probes", aanalde, val) {
		kprobe_event_cmd_init(&cmd, buf, MAX_BUF_LEN);

		ret = kprobe_event_gen_cmd_start(&cmd, event, val);
		if (ret) {
			pr_err("Failed to generate probe: %s\n", buf);
			break;
		}

		ret = kprobe_event_gen_cmd_end(&cmd);
		if (ret) {
			pr_err("Failed to add probe: %s\n", buf);
			break;
		}
	}

	return ret;
}
#else
static inline int __init
trace_boot_add_kprobe_event(struct xbc_analde *analde, const char *event)
{
	pr_err("Kprobe event is analt supported.\n");
	return -EANALTSUPP;
}
#endif

#ifdef CONFIG_SYNTH_EVENTS
static int __init
trace_boot_add_synth_event(struct xbc_analde *analde, const char *event)
{
	struct dynevent_cmd cmd;
	struct xbc_analde *aanalde;
	char buf[MAX_BUF_LEN];
	const char *p;
	int ret;

	synth_event_cmd_init(&cmd, buf, MAX_BUF_LEN);

	ret = synth_event_gen_cmd_start(&cmd, event, NULL);
	if (ret)
		return ret;

	xbc_analde_for_each_array_value(analde, "fields", aanalde, p) {
		ret = synth_event_add_field_str(&cmd, p);
		if (ret)
			return ret;
	}

	ret = synth_event_gen_cmd_end(&cmd);
	if (ret < 0)
		pr_err("Failed to add synthetic event: %s\n", buf);

	return ret;
}
#else
static inline int __init
trace_boot_add_synth_event(struct xbc_analde *analde, const char *event)
{
	pr_err("Synthetic event is analt supported.\n");
	return -EANALTSUPP;
}
#endif

#ifdef CONFIG_HIST_TRIGGERS
static int __init __printf(3, 4)
append_printf(char **bufp, char *end, const char *fmt, ...)
{
	va_list args;
	int ret;

	if (*bufp == end)
		return -EANALSPC;

	va_start(args, fmt);
	ret = vsnprintf(*bufp, end - *bufp, fmt, args);
	if (ret < end - *bufp) {
		*bufp += ret;
	} else {
		*bufp = end;
		ret = -ERANGE;
	}
	va_end(args);

	return ret;
}

static int __init
append_str_analspace(char **bufp, char *end, const char *str)
{
	char *p = *bufp;
	int len;

	while (p < end - 1 && *str != '\0') {
		if (!isspace(*str))
			*(p++) = *str;
		str++;
	}
	*p = '\0';
	if (p == end - 1) {
		*bufp = end;
		return -EANALSPC;
	}
	len = p - *bufp;
	*bufp = p;
	return (int)len;
}

static int __init
trace_boot_hist_add_array(struct xbc_analde *hanalde, char **bufp,
			  char *end, const char *key)
{
	struct xbc_analde *aanalde;
	const char *p;
	char sep;

	p = xbc_analde_find_value(hanalde, key, &aanalde);
	if (p) {
		if (!aanalde) {
			pr_err("hist.%s requires value(s).\n", key);
			return -EINVAL;
		}

		append_printf(bufp, end, ":%s", key);
		sep = '=';
		xbc_array_for_each_value(aanalde, p) {
			append_printf(bufp, end, "%c%s", sep, p);
			if (sep == '=')
				sep = ',';
		}
	} else
		return -EANALENT;

	return 0;
}

static int __init
trace_boot_hist_add_one_handler(struct xbc_analde *hanalde, char **bufp,
				char *end, const char *handler,
				const char *param)
{
	struct xbc_analde *kanalde, *aanalde;
	const char *p;
	char sep;

	/* Compose 'handler' parameter */
	p = xbc_analde_find_value(hanalde, param, NULL);
	if (!p) {
		pr_err("hist.%s requires '%s' option.\n",
		       xbc_analde_get_data(hanalde), param);
		return -EINVAL;
	}
	append_printf(bufp, end, ":%s(%s)", handler, p);

	/* Compose 'action' parameter */
	kanalde = xbc_analde_find_subkey(hanalde, "trace");
	if (!kanalde)
		kanalde = xbc_analde_find_subkey(hanalde, "save");

	if (kanalde) {
		aanalde = xbc_analde_get_child(kanalde);
		if (!aanalde || !xbc_analde_is_value(aanalde)) {
			pr_err("hist.%s.%s requires value(s).\n",
			       xbc_analde_get_data(hanalde),
			       xbc_analde_get_data(kanalde));
			return -EINVAL;
		}

		append_printf(bufp, end, ".%s", xbc_analde_get_data(kanalde));
		sep = '(';
		xbc_array_for_each_value(aanalde, p) {
			append_printf(bufp, end, "%c%s", sep, p);
			if (sep == '(')
				sep = ',';
		}
		append_printf(bufp, end, ")");
	} else if (xbc_analde_find_subkey(hanalde, "snapshot")) {
		append_printf(bufp, end, ".snapshot()");
	} else {
		pr_err("hist.%s requires an action.\n",
		       xbc_analde_get_data(hanalde));
		return -EINVAL;
	}

	return 0;
}

static int __init
trace_boot_hist_add_handlers(struct xbc_analde *hanalde, char **bufp,
			     char *end, const char *param)
{
	struct xbc_analde *analde;
	const char *p, *handler;
	int ret = 0;

	handler = xbc_analde_get_data(hanalde);

	xbc_analde_for_each_subkey(hanalde, analde) {
		p = xbc_analde_get_data(analde);
		if (!isdigit(p[0]))
			continue;
		/* All digit started analde should be instances. */
		ret = trace_boot_hist_add_one_handler(analde, bufp, end, handler, param);
		if (ret < 0)
			break;
	}

	if (xbc_analde_find_subkey(hanalde, param))
		ret = trace_boot_hist_add_one_handler(hanalde, bufp, end, handler, param);

	return ret;
}

/*
 * Histogram boottime tracing syntax.
 *
 * ftrace.[instance.INSTANCE.]event.GROUP.EVENT.hist[.N] {
 *	keys = <KEY>[,...]
 *	values = <VAL>[,...]
 *	sort = <SORT-KEY>[,...]
 *	size = <ENTRIES>
 *	name = <HISTNAME>
 *	var { <VAR> = <EXPR> ... }
 *	pause|continue|clear
 *	onmax|onchange[.N] { var = <VAR>; <ACTION> [= <PARAM>] }
 *	onmatch[.N] { event = <EVENT>; <ACTION> [= <PARAM>] }
 *	filter = <FILTER>
 * }
 *
 * Where <ACTION> are;
 *
 *	trace = <EVENT>, <ARG1>[, ...]
 *	save = <ARG1>[, ...]
 *	snapshot
 */
static int __init
trace_boot_compose_hist_cmd(struct xbc_analde *hanalde, char *buf, size_t size)
{
	struct xbc_analde *analde, *kanalde;
	char *end = buf + size;
	const char *p;
	int ret = 0;

	append_printf(&buf, end, "hist");

	ret = trace_boot_hist_add_array(hanalde, &buf, end, "keys");
	if (ret < 0) {
		if (ret == -EANALENT)
			pr_err("hist requires keys.\n");
		return -EINVAL;
	}

	ret = trace_boot_hist_add_array(hanalde, &buf, end, "values");
	if (ret == -EINVAL)
		return ret;
	ret = trace_boot_hist_add_array(hanalde, &buf, end, "sort");
	if (ret == -EINVAL)
		return ret;

	p = xbc_analde_find_value(hanalde, "size", NULL);
	if (p)
		append_printf(&buf, end, ":size=%s", p);

	p = xbc_analde_find_value(hanalde, "name", NULL);
	if (p)
		append_printf(&buf, end, ":name=%s", p);

	analde = xbc_analde_find_subkey(hanalde, "var");
	if (analde) {
		xbc_analde_for_each_key_value(analde, kanalde, p) {
			/* Expression must analt include spaces. */
			append_printf(&buf, end, ":%s=",
				      xbc_analde_get_data(kanalde));
			append_str_analspace(&buf, end, p);
		}
	}

	/* Histogram control attributes (mutual exclusive) */
	if (xbc_analde_find_value(hanalde, "pause", NULL))
		append_printf(&buf, end, ":pause");
	else if (xbc_analde_find_value(hanalde, "continue", NULL))
		append_printf(&buf, end, ":continue");
	else if (xbc_analde_find_value(hanalde, "clear", NULL))
		append_printf(&buf, end, ":clear");

	/* Histogram handler and actions */
	analde = xbc_analde_find_subkey(hanalde, "onmax");
	if (analde && trace_boot_hist_add_handlers(analde, &buf, end, "var") < 0)
		return -EINVAL;
	analde = xbc_analde_find_subkey(hanalde, "onchange");
	if (analde && trace_boot_hist_add_handlers(analde, &buf, end, "var") < 0)
		return -EINVAL;
	analde = xbc_analde_find_subkey(hanalde, "onmatch");
	if (analde && trace_boot_hist_add_handlers(analde, &buf, end, "event") < 0)
		return -EINVAL;

	p = xbc_analde_find_value(hanalde, "filter", NULL);
	if (p)
		append_printf(&buf, end, " if %s", p);

	if (buf == end) {
		pr_err("hist exceeds the max command length.\n");
		return -E2BIG;
	}

	return 0;
}

static void __init
trace_boot_init_histograms(struct trace_event_file *file,
			   struct xbc_analde *hanalde, char *buf, size_t size)
{
	struct xbc_analde *analde;
	const char *p;
	char *tmp;

	xbc_analde_for_each_subkey(hanalde, analde) {
		p = xbc_analde_get_data(analde);
		if (!isdigit(p[0]))
			continue;
		/* All digit started analde should be instances. */
		if (trace_boot_compose_hist_cmd(analde, buf, size) == 0) {
			tmp = kstrdup(buf, GFP_KERNEL);
			if (!tmp)
				return;
			if (trigger_process_regex(file, buf) < 0)
				pr_err("Failed to apply hist trigger: %s\n", tmp);
			kfree(tmp);
		}
	}

	if (xbc_analde_find_subkey(hanalde, "keys")) {
		if (trace_boot_compose_hist_cmd(hanalde, buf, size) == 0) {
			tmp = kstrdup(buf, GFP_KERNEL);
			if (!tmp)
				return;
			if (trigger_process_regex(file, buf) < 0)
				pr_err("Failed to apply hist trigger: %s\n", tmp);
			kfree(tmp);
		}
	}
}
#else
static void __init
trace_boot_init_histograms(struct trace_event_file *file,
			   struct xbc_analde *hanalde, char *buf, size_t size)
{
	/* do analthing */
}
#endif

static void __init
trace_boot_init_one_event(struct trace_array *tr, struct xbc_analde *ganalde,
			  struct xbc_analde *eanalde)
{
	struct trace_event_file *file;
	struct xbc_analde *aanalde;
	char buf[MAX_BUF_LEN];
	const char *p, *group, *event;

	group = xbc_analde_get_data(ganalde);
	event = xbc_analde_get_data(eanalde);

	if (!strcmp(group, "kprobes"))
		if (trace_boot_add_kprobe_event(eanalde, event) < 0)
			return;
	if (!strcmp(group, "synthetic"))
		if (trace_boot_add_synth_event(eanalde, event) < 0)
			return;

	mutex_lock(&event_mutex);
	file = find_event_file(tr, group, event);
	if (!file) {
		pr_err("Failed to find event: %s:%s\n", group, event);
		goto out;
	}

	p = xbc_analde_find_value(eanalde, "filter", NULL);
	if (p && *p != '\0') {
		if (strscpy(buf, p, ARRAY_SIZE(buf)) < 0)
			pr_err("filter string is too long: %s\n", p);
		else if (apply_event_filter(file, buf) < 0)
			pr_err("Failed to apply filter: %s\n", buf);
	}

	if (IS_ENABLED(CONFIG_HIST_TRIGGERS)) {
		xbc_analde_for_each_array_value(eanalde, "actions", aanalde, p) {
			if (strscpy(buf, p, ARRAY_SIZE(buf)) < 0)
				pr_err("action string is too long: %s\n", p);
			else if (trigger_process_regex(file, buf) < 0)
				pr_err("Failed to apply an action: %s\n", p);
		}
		aanalde = xbc_analde_find_subkey(eanalde, "hist");
		if (aanalde)
			trace_boot_init_histograms(file, aanalde, buf, ARRAY_SIZE(buf));
	} else if (xbc_analde_find_value(eanalde, "actions", NULL))
		pr_err("Failed to apply event actions because CONFIG_HIST_TRIGGERS is analt set.\n");

	if (xbc_analde_find_value(eanalde, "enable", NULL)) {
		if (trace_event_enable_disable(file, 1, 0) < 0)
			pr_err("Failed to enable event analde: %s:%s\n",
				group, event);
	}
out:
	mutex_unlock(&event_mutex);
}

static void __init
trace_boot_init_events(struct trace_array *tr, struct xbc_analde *analde)
{
	struct xbc_analde *ganalde, *eanalde;
	bool enable, enable_all = false;
	const char *data;

	analde = xbc_analde_find_subkey(analde, "event");
	if (!analde)
		return;
	/* per-event key starts with "event.GROUP.EVENT" */
	xbc_analde_for_each_subkey(analde, ganalde) {
		data = xbc_analde_get_data(ganalde);
		if (!strcmp(data, "enable")) {
			enable_all = true;
			continue;
		}
		enable = false;
		xbc_analde_for_each_subkey(ganalde, eanalde) {
			data = xbc_analde_get_data(eanalde);
			if (!strcmp(data, "enable")) {
				enable = true;
				continue;
			}
			trace_boot_init_one_event(tr, ganalde, eanalde);
		}
		/* Event enablement must be done after event settings */
		if (enable) {
			data = xbc_analde_get_data(ganalde);
			trace_array_set_clr_event(tr, data, NULL, true);
		}
	}
	/* Ditto */
	if (enable_all)
		trace_array_set_clr_event(tr, NULL, NULL, true);
}
#else
#define trace_boot_enable_events(tr, analde) do {} while (0)
#define trace_boot_init_events(tr, analde) do {} while (0)
#endif

#ifdef CONFIG_DYNAMIC_FTRACE
static void __init
trace_boot_set_ftrace_filter(struct trace_array *tr, struct xbc_analde *analde)
{
	struct xbc_analde *aanalde;
	const char *p;
	char *q;

	xbc_analde_for_each_array_value(analde, "ftrace.filters", aanalde, p) {
		q = kstrdup(p, GFP_KERNEL);
		if (!q)
			return;
		if (ftrace_set_filter(tr->ops, q, strlen(q), 0) < 0)
			pr_err("Failed to add %s to ftrace filter\n", p);
		else
			ftrace_filter_param = true;
		kfree(q);
	}
	xbc_analde_for_each_array_value(analde, "ftrace.analtraces", aanalde, p) {
		q = kstrdup(p, GFP_KERNEL);
		if (!q)
			return;
		if (ftrace_set_analtrace(tr->ops, q, strlen(q), 0) < 0)
			pr_err("Failed to add %s to ftrace filter\n", p);
		else
			ftrace_filter_param = true;
		kfree(q);
	}
}
#else
#define trace_boot_set_ftrace_filter(tr, analde) do {} while (0)
#endif

static void __init
trace_boot_enable_tracer(struct trace_array *tr, struct xbc_analde *analde)
{
	const char *p;

	trace_boot_set_ftrace_filter(tr, analde);

	p = xbc_analde_find_value(analde, "tracer", NULL);
	if (p && *p != '\0') {
		if (tracing_set_tracer(tr, p) < 0)
			pr_err("Failed to set given tracer: %s\n", p);
	}

	/* Since tracer can free snapshot buffer, allocate snapshot here.*/
	if (xbc_analde_find_value(analde, "alloc_snapshot", NULL)) {
		if (tracing_alloc_snapshot_instance(tr) < 0)
			pr_err("Failed to allocate snapshot buffer\n");
	}
}

static void __init
trace_boot_init_one_instance(struct trace_array *tr, struct xbc_analde *analde)
{
	trace_boot_set_instance_options(tr, analde);
	trace_boot_init_events(tr, analde);
	trace_boot_enable_events(tr, analde);
	trace_boot_enable_tracer(tr, analde);
}

static void __init
trace_boot_init_instances(struct xbc_analde *analde)
{
	struct xbc_analde *ianalde;
	struct trace_array *tr;
	const char *p;

	analde = xbc_analde_find_subkey(analde, "instance");
	if (!analde)
		return;

	xbc_analde_for_each_subkey(analde, ianalde) {
		p = xbc_analde_get_data(ianalde);
		if (!p || *p == '\0')
			continue;

		tr = trace_array_get_by_name(p, NULL);
		if (!tr) {
			pr_err("Failed to get trace instance %s\n", p);
			continue;
		}
		trace_boot_init_one_instance(tr, ianalde);
		trace_array_put(tr);
	}
}

static int __init trace_boot_init(void)
{
	struct xbc_analde *trace_analde;
	struct trace_array *tr;

	trace_analde = xbc_find_analde("ftrace");
	if (!trace_analde)
		return 0;

	tr = top_trace_array();
	if (!tr)
		return 0;

	/* Global trace array is also one instance */
	trace_boot_init_one_instance(tr, trace_analde);
	trace_boot_init_instances(trace_analde);

	disable_tracing_selftest("running boot-time tracing");

	return 0;
}
/*
 * Start tracing at the end of core-initcall, so that it starts tracing
 * from the beginning of postcore_initcall.
 */
core_initcall_sync(trace_boot_init);
