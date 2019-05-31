// SPDX-License-Identifier: GPL-2.0
/*
 * Kprobes-based tracing events
 *
 * Created by Masami Hiramatsu <mhiramat@redhat.com>
 *
 */
#define pr_fmt(fmt)	"trace_kprobe: " fmt

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/rculist.h>
#include <linux/error-injection.h>

#include <asm/setup.h>  /* for COMMAND_LINE_SIZE */

#include "trace_dynevent.h"
#include "trace_kprobe_selftest.h"
#include "trace_probe.h"
#include "trace_probe_tmpl.h"

#define KPROBE_EVENT_SYSTEM "kprobes"
#define KRETPROBE_MAXACTIVE_MAX 4096
#define MAX_KPROBE_CMDLINE_SIZE 1024

/* Kprobe early definition from command line */
static char kprobe_boot_events_buf[COMMAND_LINE_SIZE] __initdata;
static bool kprobe_boot_events_enabled __initdata;

static int __init set_kprobe_boot_events(char *str)
{
	strlcpy(kprobe_boot_events_buf, str, COMMAND_LINE_SIZE);
	return 0;
}
__setup("kprobe_event=", set_kprobe_boot_events);

static int trace_kprobe_create(int argc, const char **argv);
static int trace_kprobe_show(struct seq_file *m, struct dyn_event *ev);
static int trace_kprobe_release(struct dyn_event *ev);
static bool trace_kprobe_is_busy(struct dyn_event *ev);
static bool trace_kprobe_match(const char *system, const char *event,
			       struct dyn_event *ev);

static struct dyn_event_operations trace_kprobe_ops = {
	.create = trace_kprobe_create,
	.show = trace_kprobe_show,
	.is_busy = trace_kprobe_is_busy,
	.free = trace_kprobe_release,
	.match = trace_kprobe_match,
};

/*
 * Kprobe event core functions
 */
struct trace_kprobe {
	struct dyn_event	devent;
	struct kretprobe	rp;	/* Use rp.kp for kprobe use */
	unsigned long __percpu *nhit;
	const char		*symbol;	/* symbol name */
	struct trace_probe	tp;
};

static bool is_trace_kprobe(struct dyn_event *ev)
{
	return ev->ops == &trace_kprobe_ops;
}

static struct trace_kprobe *to_trace_kprobe(struct dyn_event *ev)
{
	return container_of(ev, struct trace_kprobe, devent);
}

/**
 * for_each_trace_kprobe - iterate over the trace_kprobe list
 * @pos:	the struct trace_kprobe * for each entry
 * @dpos:	the struct dyn_event * to use as a loop cursor
 */
#define for_each_trace_kprobe(pos, dpos)	\
	for_each_dyn_event(dpos)		\
		if (is_trace_kprobe(dpos) && (pos = to_trace_kprobe(dpos)))

#define SIZEOF_TRACE_KPROBE(n)				\
	(offsetof(struct trace_kprobe, tp.args) +	\
	(sizeof(struct probe_arg) * (n)))

static nokprobe_inline bool trace_kprobe_is_return(struct trace_kprobe *tk)
{
	return tk->rp.handler != NULL;
}

static nokprobe_inline const char *trace_kprobe_symbol(struct trace_kprobe *tk)
{
	return tk->symbol ? tk->symbol : "unknown";
}

static nokprobe_inline unsigned long trace_kprobe_offset(struct trace_kprobe *tk)
{
	return tk->rp.kp.offset;
}

static nokprobe_inline bool trace_kprobe_has_gone(struct trace_kprobe *tk)
{
	return !!(kprobe_gone(&tk->rp.kp));
}

static nokprobe_inline bool trace_kprobe_within_module(struct trace_kprobe *tk,
						 struct module *mod)
{
	int len = strlen(mod->name);
	const char *name = trace_kprobe_symbol(tk);
	return strncmp(mod->name, name, len) == 0 && name[len] == ':';
}

static nokprobe_inline bool trace_kprobe_module_exist(struct trace_kprobe *tk)
{
	char *p;
	bool ret;

	if (!tk->symbol)
		return false;
	p = strchr(tk->symbol, ':');
	if (!p)
		return true;
	*p = '\0';
	mutex_lock(&module_mutex);
	ret = !!find_module(tk->symbol);
	mutex_unlock(&module_mutex);
	*p = ':';

	return ret;
}

static bool trace_kprobe_is_busy(struct dyn_event *ev)
{
	struct trace_kprobe *tk = to_trace_kprobe(ev);

	return trace_probe_is_enabled(&tk->tp);
}

static bool trace_kprobe_match(const char *system, const char *event,
			       struct dyn_event *ev)
{
	struct trace_kprobe *tk = to_trace_kprobe(ev);

	return strcmp(trace_event_name(&tk->tp.call), event) == 0 &&
	    (!system || strcmp(tk->tp.call.class->system, system) == 0);
}

static nokprobe_inline unsigned long trace_kprobe_nhit(struct trace_kprobe *tk)
{
	unsigned long nhit = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		nhit += *per_cpu_ptr(tk->nhit, cpu);

	return nhit;
}

/* Return 0 if it fails to find the symbol address */
static nokprobe_inline
unsigned long trace_kprobe_address(struct trace_kprobe *tk)
{
	unsigned long addr;

	if (tk->symbol) {
		addr = (unsigned long)
			kallsyms_lookup_name(trace_kprobe_symbol(tk));
		if (addr)
			addr += tk->rp.kp.offset;
	} else {
		addr = (unsigned long)tk->rp.kp.addr;
	}
	return addr;
}

bool trace_kprobe_on_func_entry(struct trace_event_call *call)
{
	struct trace_kprobe *tk = (struct trace_kprobe *)call->data;

	return kprobe_on_func_entry(tk->rp.kp.addr,
			tk->rp.kp.addr ? NULL : tk->rp.kp.symbol_name,
			tk->rp.kp.addr ? 0 : tk->rp.kp.offset);
}

bool trace_kprobe_error_injectable(struct trace_event_call *call)
{
	struct trace_kprobe *tk = (struct trace_kprobe *)call->data;

	return within_error_injection_list(trace_kprobe_address(tk));
}

static int register_kprobe_event(struct trace_kprobe *tk);
static int unregister_kprobe_event(struct trace_kprobe *tk);

static int kprobe_dispatcher(struct kprobe *kp, struct pt_regs *regs);
static int kretprobe_dispatcher(struct kretprobe_instance *ri,
				struct pt_regs *regs);

/*
 * Allocate new trace_probe and initialize it (including kprobes).
 */
static struct trace_kprobe *alloc_trace_kprobe(const char *group,
					     const char *event,
					     void *addr,
					     const char *symbol,
					     unsigned long offs,
					     int maxactive,
					     int nargs, bool is_return)
{
	struct trace_kprobe *tk;
	int ret = -ENOMEM;

	tk = kzalloc(SIZEOF_TRACE_KPROBE(nargs), GFP_KERNEL);
	if (!tk)
		return ERR_PTR(ret);

	tk->nhit = alloc_percpu(unsigned long);
	if (!tk->nhit)
		goto error;

	if (symbol) {
		tk->symbol = kstrdup(symbol, GFP_KERNEL);
		if (!tk->symbol)
			goto error;
		tk->rp.kp.symbol_name = tk->symbol;
		tk->rp.kp.offset = offs;
	} else
		tk->rp.kp.addr = addr;

	if (is_return)
		tk->rp.handler = kretprobe_dispatcher;
	else
		tk->rp.kp.pre_handler = kprobe_dispatcher;

	tk->rp.maxactive = maxactive;

	if (!event || !group) {
		ret = -EINVAL;
		goto error;
	}

	tk->tp.call.class = &tk->tp.class;
	tk->tp.call.name = kstrdup(event, GFP_KERNEL);
	if (!tk->tp.call.name)
		goto error;

	tk->tp.class.system = kstrdup(group, GFP_KERNEL);
	if (!tk->tp.class.system)
		goto error;

	dyn_event_init(&tk->devent, &trace_kprobe_ops);
	INIT_LIST_HEAD(&tk->tp.files);
	return tk;
error:
	kfree(tk->tp.call.name);
	kfree(tk->symbol);
	free_percpu(tk->nhit);
	kfree(tk);
	return ERR_PTR(ret);
}

static void free_trace_kprobe(struct trace_kprobe *tk)
{
	int i;

	if (!tk)
		return;

	for (i = 0; i < tk->tp.nr_args; i++)
		traceprobe_free_probe_arg(&tk->tp.args[i]);

	kfree(tk->tp.call.class->system);
	kfree(tk->tp.call.name);
	kfree(tk->tp.call.print_fmt);
	kfree(tk->symbol);
	free_percpu(tk->nhit);
	kfree(tk);
}

static struct trace_kprobe *find_trace_kprobe(const char *event,
					      const char *group)
{
	struct dyn_event *pos;
	struct trace_kprobe *tk;

	for_each_trace_kprobe(tk, pos)
		if (strcmp(trace_event_name(&tk->tp.call), event) == 0 &&
		    strcmp(tk->tp.call.class->system, group) == 0)
			return tk;
	return NULL;
}

static inline int __enable_trace_kprobe(struct trace_kprobe *tk)
{
	int ret = 0;

	if (trace_probe_is_registered(&tk->tp) && !trace_kprobe_has_gone(tk)) {
		if (trace_kprobe_is_return(tk))
			ret = enable_kretprobe(&tk->rp);
		else
			ret = enable_kprobe(&tk->rp.kp);
	}

	return ret;
}

/*
 * Enable trace_probe
 * if the file is NULL, enable "perf" handler, or enable "trace" handler.
 */
static int
enable_trace_kprobe(struct trace_kprobe *tk, struct trace_event_file *file)
{
	struct event_file_link *link;
	int ret = 0;

	if (file) {
		link = kmalloc(sizeof(*link), GFP_KERNEL);
		if (!link) {
			ret = -ENOMEM;
			goto out;
		}

		link->file = file;
		list_add_tail_rcu(&link->list, &tk->tp.files);

		tk->tp.flags |= TP_FLAG_TRACE;
		ret = __enable_trace_kprobe(tk);
		if (ret) {
			list_del_rcu(&link->list);
			kfree(link);
			tk->tp.flags &= ~TP_FLAG_TRACE;
		}

	} else {
		tk->tp.flags |= TP_FLAG_PROFILE;
		ret = __enable_trace_kprobe(tk);
		if (ret)
			tk->tp.flags &= ~TP_FLAG_PROFILE;
	}
 out:
	return ret;
}

/*
 * Disable trace_probe
 * if the file is NULL, disable "perf" handler, or disable "trace" handler.
 */
static int
disable_trace_kprobe(struct trace_kprobe *tk, struct trace_event_file *file)
{
	struct event_file_link *link = NULL;
	int wait = 0;
	int ret = 0;

	if (file) {
		link = find_event_file_link(&tk->tp, file);
		if (!link) {
			ret = -EINVAL;
			goto out;
		}

		list_del_rcu(&link->list);
		wait = 1;
		if (!list_empty(&tk->tp.files))
			goto out;

		tk->tp.flags &= ~TP_FLAG_TRACE;
	} else
		tk->tp.flags &= ~TP_FLAG_PROFILE;

	if (!trace_probe_is_enabled(&tk->tp) && trace_probe_is_registered(&tk->tp)) {
		if (trace_kprobe_is_return(tk))
			disable_kretprobe(&tk->rp);
		else
			disable_kprobe(&tk->rp.kp);
		wait = 1;
	}

	/*
	 * if tk is not added to any list, it must be a local trace_kprobe
	 * created with perf_event_open. We don't need to wait for these
	 * trace_kprobes
	 */
	if (list_empty(&tk->devent.list))
		wait = 0;
 out:
	if (wait) {
		/*
		 * Synchronize with kprobe_trace_func/kretprobe_trace_func
		 * to ensure disabled (all running handlers are finished).
		 * This is not only for kfree(), but also the caller,
		 * trace_remove_event_call() supposes it for releasing
		 * event_call related objects, which will be accessed in
		 * the kprobe_trace_func/kretprobe_trace_func.
		 */
		synchronize_rcu();
		kfree(link);	/* Ignored if link == NULL */
	}

	return ret;
}

#if defined(CONFIG_KPROBES_ON_FTRACE) && \
	!defined(CONFIG_KPROBE_EVENTS_ON_NOTRACE)
static bool within_notrace_func(struct trace_kprobe *tk)
{
	unsigned long offset, size, addr;

	addr = trace_kprobe_address(tk);
	if (!addr || !kallsyms_lookup_size_offset(addr, &size, &offset))
		return false;

	/* Get the entry address of the target function */
	addr -= offset;

	/*
	 * Since ftrace_location_range() does inclusive range check, we need
	 * to subtract 1 byte from the end address.
	 */
	return !ftrace_location_range(addr, addr + size - 1);
}
#else
#define within_notrace_func(tk)	(false)
#endif

/* Internal register function - just handle k*probes and flags */
static int __register_trace_kprobe(struct trace_kprobe *tk)
{
	int i, ret;

	if (trace_probe_is_registered(&tk->tp))
		return -EINVAL;

	if (within_notrace_func(tk)) {
		pr_warn("Could not probe notrace function %s\n",
			trace_kprobe_symbol(tk));
		return -EINVAL;
	}

	for (i = 0; i < tk->tp.nr_args; i++) {
		ret = traceprobe_update_arg(&tk->tp.args[i]);
		if (ret)
			return ret;
	}

	/* Set/clear disabled flag according to tp->flag */
	if (trace_probe_is_enabled(&tk->tp))
		tk->rp.kp.flags &= ~KPROBE_FLAG_DISABLED;
	else
		tk->rp.kp.flags |= KPROBE_FLAG_DISABLED;

	if (trace_kprobe_is_return(tk))
		ret = register_kretprobe(&tk->rp);
	else
		ret = register_kprobe(&tk->rp.kp);

	if (ret == 0)
		tk->tp.flags |= TP_FLAG_REGISTERED;
	return ret;
}

/* Internal unregister function - just handle k*probes and flags */
static void __unregister_trace_kprobe(struct trace_kprobe *tk)
{
	if (trace_probe_is_registered(&tk->tp)) {
		if (trace_kprobe_is_return(tk))
			unregister_kretprobe(&tk->rp);
		else
			unregister_kprobe(&tk->rp.kp);
		tk->tp.flags &= ~TP_FLAG_REGISTERED;
		/* Cleanup kprobe for reuse */
		if (tk->rp.kp.symbol_name)
			tk->rp.kp.addr = NULL;
	}
}

/* Unregister a trace_probe and probe_event */
static int unregister_trace_kprobe(struct trace_kprobe *tk)
{
	/* Enabled event can not be unregistered */
	if (trace_probe_is_enabled(&tk->tp))
		return -EBUSY;

	/* Will fail if probe is being used by ftrace or perf */
	if (unregister_kprobe_event(tk))
		return -EBUSY;

	__unregister_trace_kprobe(tk);
	dyn_event_remove(&tk->devent);

	return 0;
}

/* Register a trace_probe and probe_event */
static int register_trace_kprobe(struct trace_kprobe *tk)
{
	struct trace_kprobe *old_tk;
	int ret;

	mutex_lock(&event_mutex);

	/* Delete old (same name) event if exist */
	old_tk = find_trace_kprobe(trace_event_name(&tk->tp.call),
			tk->tp.call.class->system);
	if (old_tk) {
		ret = unregister_trace_kprobe(old_tk);
		if (ret < 0)
			goto end;
		free_trace_kprobe(old_tk);
	}

	/* Register new event */
	ret = register_kprobe_event(tk);
	if (ret) {
		pr_warn("Failed to register probe event(%d)\n", ret);
		goto end;
	}

	/* Register k*probe */
	ret = __register_trace_kprobe(tk);
	if (ret == -ENOENT && !trace_kprobe_module_exist(tk)) {
		pr_warn("This probe might be able to register after target module is loaded. Continue.\n");
		ret = 0;
	}

	if (ret < 0)
		unregister_kprobe_event(tk);
	else
		dyn_event_add(&tk->devent);

end:
	mutex_unlock(&event_mutex);
	return ret;
}

/* Module notifier call back, checking event on the module */
static int trace_kprobe_module_callback(struct notifier_block *nb,
				       unsigned long val, void *data)
{
	struct module *mod = data;
	struct dyn_event *pos;
	struct trace_kprobe *tk;
	int ret;

	if (val != MODULE_STATE_COMING)
		return NOTIFY_DONE;

	/* Update probes on coming module */
	mutex_lock(&event_mutex);
	for_each_trace_kprobe(tk, pos) {
		if (trace_kprobe_within_module(tk, mod)) {
			/* Don't need to check busy - this should have gone. */
			__unregister_trace_kprobe(tk);
			ret = __register_trace_kprobe(tk);
			if (ret)
				pr_warn("Failed to re-register probe %s on %s: %d\n",
					trace_event_name(&tk->tp.call),
					mod->name, ret);
		}
	}
	mutex_unlock(&event_mutex);

	return NOTIFY_DONE;
}

static struct notifier_block trace_kprobe_module_nb = {
	.notifier_call = trace_kprobe_module_callback,
	.priority = 1	/* Invoked after kprobe module callback */
};

/* Convert certain expected symbols into '_' when generating event names */
static inline void sanitize_event_name(char *name)
{
	while (*name++ != '\0')
		if (*name == ':' || *name == '.')
			*name = '_';
}

static int trace_kprobe_create(int argc, const char *argv[])
{
	/*
	 * Argument syntax:
	 *  - Add kprobe:
	 *      p[:[GRP/]EVENT] [MOD:]KSYM[+OFFS]|KADDR [FETCHARGS]
	 *  - Add kretprobe:
	 *      r[MAXACTIVE][:[GRP/]EVENT] [MOD:]KSYM[+0] [FETCHARGS]
	 * Fetch args:
	 *  $retval	: fetch return value
	 *  $stack	: fetch stack address
	 *  $stackN	: fetch Nth of stack (N:0-)
	 *  $comm       : fetch current task comm
	 *  @ADDR	: fetch memory at ADDR (ADDR should be in kernel)
	 *  @SYM[+|-offs] : fetch memory at SYM +|- offs (SYM is a data symbol)
	 *  %REG	: fetch register REG
	 * Dereferencing memory fetch:
	 *  +|-offs(ARG) : fetch memory at ARG +|- offs address.
	 * Alias name of args:
	 *  NAME=FETCHARG : set NAME as alias of FETCHARG.
	 * Type of args:
	 *  FETCHARG:TYPE : use TYPE instead of unsigned long.
	 */
	struct trace_kprobe *tk = NULL;
	int i, len, ret = 0;
	bool is_return = false;
	char *symbol = NULL, *tmp = NULL;
	const char *event = NULL, *group = KPROBE_EVENT_SYSTEM;
	int maxactive = 0;
	long offset = 0;
	void *addr = NULL;
	char buf[MAX_EVENT_NAME_LEN];
	unsigned int flags = TPARG_FL_KERNEL;

	switch (argv[0][0]) {
	case 'r':
		is_return = true;
		flags |= TPARG_FL_RETURN;
		break;
	case 'p':
		break;
	default:
		return -ECANCELED;
	}
	if (argc < 2)
		return -ECANCELED;

	trace_probe_log_init("trace_kprobe", argc, argv);

	event = strchr(&argv[0][1], ':');
	if (event)
		event++;

	if (isdigit(argv[0][1])) {
		if (!is_return) {
			trace_probe_log_err(1, MAXACT_NO_KPROBE);
			goto parse_error;
		}
		if (event)
			len = event - &argv[0][1] - 1;
		else
			len = strlen(&argv[0][1]);
		if (len > MAX_EVENT_NAME_LEN - 1) {
			trace_probe_log_err(1, BAD_MAXACT);
			goto parse_error;
		}
		memcpy(buf, &argv[0][1], len);
		buf[len] = '\0';
		ret = kstrtouint(buf, 0, &maxactive);
		if (ret || !maxactive) {
			trace_probe_log_err(1, BAD_MAXACT);
			goto parse_error;
		}
		/* kretprobes instances are iterated over via a list. The
		 * maximum should stay reasonable.
		 */
		if (maxactive > KRETPROBE_MAXACTIVE_MAX) {
			trace_probe_log_err(1, MAXACT_TOO_BIG);
			goto parse_error;
		}
	}

	/* try to parse an address. if that fails, try to read the
	 * input as a symbol. */
	if (kstrtoul(argv[1], 0, (unsigned long *)&addr)) {
		trace_probe_log_set_index(1);
		/* Check whether uprobe event specified */
		if (strchr(argv[1], '/') && strchr(argv[1], ':')) {
			ret = -ECANCELED;
			goto error;
		}
		/* a symbol specified */
		symbol = kstrdup(argv[1], GFP_KERNEL);
		if (!symbol)
			return -ENOMEM;
		/* TODO: support .init module functions */
		ret = traceprobe_split_symbol_offset(symbol, &offset);
		if (ret || offset < 0 || offset > UINT_MAX) {
			trace_probe_log_err(0, BAD_PROBE_ADDR);
			goto parse_error;
		}
		if (kprobe_on_func_entry(NULL, symbol, offset))
			flags |= TPARG_FL_FENTRY;
		if (offset && is_return && !(flags & TPARG_FL_FENTRY)) {
			trace_probe_log_err(0, BAD_RETPROBE);
			goto parse_error;
		}
	}

	trace_probe_log_set_index(0);
	if (event) {
		ret = traceprobe_parse_event_name(&event, &group, buf,
						  event - argv[0]);
		if (ret)
			goto parse_error;
	} else {
		/* Make a new event name */
		if (symbol)
			snprintf(buf, MAX_EVENT_NAME_LEN, "%c_%s_%ld",
				 is_return ? 'r' : 'p', symbol, offset);
		else
			snprintf(buf, MAX_EVENT_NAME_LEN, "%c_0x%p",
				 is_return ? 'r' : 'p', addr);
		sanitize_event_name(buf);
		event = buf;
	}

	/* setup a probe */
	tk = alloc_trace_kprobe(group, event, addr, symbol, offset, maxactive,
			       argc - 2, is_return);
	if (IS_ERR(tk)) {
		ret = PTR_ERR(tk);
		/* This must return -ENOMEM, else there is a bug */
		WARN_ON_ONCE(ret != -ENOMEM);
		goto out;	/* We know tk is not allocated */
	}
	argc -= 2; argv += 2;

	/* parse arguments */
	for (i = 0; i < argc && i < MAX_TRACE_ARGS; i++) {
		tmp = kstrdup(argv[i], GFP_KERNEL);
		if (!tmp) {
			ret = -ENOMEM;
			goto error;
		}

		trace_probe_log_set_index(i + 2);
		ret = traceprobe_parse_probe_arg(&tk->tp, i, tmp, flags);
		kfree(tmp);
		if (ret)
			goto error;	/* This can be -ENOMEM */
	}

	ret = traceprobe_set_print_fmt(&tk->tp, is_return);
	if (ret < 0)
		goto error;

	ret = register_trace_kprobe(tk);
	if (ret) {
		trace_probe_log_set_index(1);
		if (ret == -EILSEQ)
			trace_probe_log_err(0, BAD_INSN_BNDRY);
		else if (ret == -ENOENT)
			trace_probe_log_err(0, BAD_PROBE_ADDR);
		else if (ret != -ENOMEM)
			trace_probe_log_err(0, FAIL_REG_PROBE);
		goto error;
	}

out:
	trace_probe_log_clear();
	kfree(symbol);
	return ret;

parse_error:
	ret = -EINVAL;
error:
	free_trace_kprobe(tk);
	goto out;
}

static int create_or_delete_trace_kprobe(int argc, char **argv)
{
	int ret;

	if (argv[0][0] == '-')
		return dyn_event_release(argc, argv, &trace_kprobe_ops);

	ret = trace_kprobe_create(argc, (const char **)argv);
	return ret == -ECANCELED ? -EINVAL : ret;
}

static int trace_kprobe_release(struct dyn_event *ev)
{
	struct trace_kprobe *tk = to_trace_kprobe(ev);
	int ret = unregister_trace_kprobe(tk);

	if (!ret)
		free_trace_kprobe(tk);
	return ret;
}

static int trace_kprobe_show(struct seq_file *m, struct dyn_event *ev)
{
	struct trace_kprobe *tk = to_trace_kprobe(ev);
	int i;

	seq_putc(m, trace_kprobe_is_return(tk) ? 'r' : 'p');
	seq_printf(m, ":%s/%s", tk->tp.call.class->system,
			trace_event_name(&tk->tp.call));

	if (!tk->symbol)
		seq_printf(m, " 0x%p", tk->rp.kp.addr);
	else if (tk->rp.kp.offset)
		seq_printf(m, " %s+%u", trace_kprobe_symbol(tk),
			   tk->rp.kp.offset);
	else
		seq_printf(m, " %s", trace_kprobe_symbol(tk));

	for (i = 0; i < tk->tp.nr_args; i++)
		seq_printf(m, " %s=%s", tk->tp.args[i].name, tk->tp.args[i].comm);
	seq_putc(m, '\n');

	return 0;
}

static int probes_seq_show(struct seq_file *m, void *v)
{
	struct dyn_event *ev = v;

	if (!is_trace_kprobe(ev))
		return 0;

	return trace_kprobe_show(m, ev);
}

static const struct seq_operations probes_seq_op = {
	.start  = dyn_event_seq_start,
	.next   = dyn_event_seq_next,
	.stop   = dyn_event_seq_stop,
	.show   = probes_seq_show
};

static int probes_open(struct inode *inode, struct file *file)
{
	int ret;

	if ((file->f_mode & FMODE_WRITE) && (file->f_flags & O_TRUNC)) {
		ret = dyn_events_release_all(&trace_kprobe_ops);
		if (ret < 0)
			return ret;
	}

	return seq_open(file, &probes_seq_op);
}

static ssize_t probes_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *ppos)
{
	return trace_parse_run_command(file, buffer, count, ppos,
				       create_or_delete_trace_kprobe);
}

static const struct file_operations kprobe_events_ops = {
	.owner          = THIS_MODULE,
	.open           = probes_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release,
	.write		= probes_write,
};

/* Probes profiling interfaces */
static int probes_profile_seq_show(struct seq_file *m, void *v)
{
	struct dyn_event *ev = v;
	struct trace_kprobe *tk;

	if (!is_trace_kprobe(ev))
		return 0;

	tk = to_trace_kprobe(ev);
	seq_printf(m, "  %-44s %15lu %15lu\n",
		   trace_event_name(&tk->tp.call),
		   trace_kprobe_nhit(tk),
		   tk->rp.kp.nmissed);

	return 0;
}

static const struct seq_operations profile_seq_op = {
	.start  = dyn_event_seq_start,
	.next   = dyn_event_seq_next,
	.stop   = dyn_event_seq_stop,
	.show   = probes_profile_seq_show
};

static int profile_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &profile_seq_op);
}

static const struct file_operations kprobe_profile_ops = {
	.owner          = THIS_MODULE,
	.open           = profile_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release,
};

/* Kprobe specific fetch functions */

/* Return the length of string -- including null terminal byte */
static nokprobe_inline int
fetch_store_strlen(unsigned long addr)
{
	int ret, len = 0;
	u8 c;

	do {
		ret = probe_kernel_read(&c, (u8 *)addr + len, 1);
		len++;
	} while (c && ret == 0 && len < MAX_STRING_SIZE);

	return (ret < 0) ? ret : len;
}

/* Return the length of string -- including null terminal byte */
static nokprobe_inline int
fetch_store_strlen_user(unsigned long addr)
{
	const void __user *uaddr =  (__force const void __user *)addr;

	return strnlen_unsafe_user(uaddr, MAX_STRING_SIZE);
}

/*
 * Fetch a null-terminated string. Caller MUST set *(u32 *)buf with max
 * length and relative data location.
 */
static nokprobe_inline int
fetch_store_string(unsigned long addr, void *dest, void *base)
{
	int maxlen = get_loc_len(*(u32 *)dest);
	void *__dest;
	long ret;

	if (unlikely(!maxlen))
		return -ENOMEM;

	__dest = get_loc_data(dest, base);

	/*
	 * Try to get string again, since the string can be changed while
	 * probing.
	 */
	ret = strncpy_from_unsafe(__dest, (void *)addr, maxlen);
	if (ret >= 0)
		*(u32 *)dest = make_data_loc(ret, __dest - base);

	return ret;
}

/*
 * Fetch a null-terminated string from user. Caller MUST set *(u32 *)buf
 * with max length and relative data location.
 */
static nokprobe_inline int
fetch_store_string_user(unsigned long addr, void *dest, void *base)
{
	const void __user *uaddr =  (__force const void __user *)addr;
	int maxlen = get_loc_len(*(u32 *)dest);
	void *__dest;
	long ret;

	if (unlikely(!maxlen))
		return -ENOMEM;

	__dest = get_loc_data(dest, base);

	ret = strncpy_from_unsafe_user(__dest, uaddr, maxlen);
	if (ret >= 0)
		*(u32 *)dest = make_data_loc(ret, __dest - base);

	return ret;
}

static nokprobe_inline int
probe_mem_read(void *dest, void *src, size_t size)
{
	return probe_kernel_read(dest, src, size);
}

static nokprobe_inline int
probe_mem_read_user(void *dest, void *src, size_t size)
{
	const void __user *uaddr =  (__force const void __user *)src;

	return probe_user_read(dest, uaddr, size);
}

/* Note that we don't verify it, since the code does not come from user space */
static int
process_fetch_insn(struct fetch_insn *code, struct pt_regs *regs, void *dest,
		   void *base)
{
	unsigned long val;

retry:
	/* 1st stage: get value from context */
	switch (code->op) {
	case FETCH_OP_REG:
		val = regs_get_register(regs, code->param);
		break;
	case FETCH_OP_STACK:
		val = regs_get_kernel_stack_nth(regs, code->param);
		break;
	case FETCH_OP_STACKP:
		val = kernel_stack_pointer(regs);
		break;
	case FETCH_OP_RETVAL:
		val = regs_return_value(regs);
		break;
	case FETCH_OP_IMM:
		val = code->immediate;
		break;
	case FETCH_OP_COMM:
		val = (unsigned long)current->comm;
		break;
#ifdef CONFIG_HAVE_FUNCTION_ARG_ACCESS_API
	case FETCH_OP_ARG:
		val = regs_get_kernel_argument(regs, code->param);
		break;
#endif
	case FETCH_NOP_SYMBOL:	/* Ignore a place holder */
		code++;
		goto retry;
	default:
		return -EILSEQ;
	}
	code++;

	return process_fetch_insn_bottom(code, val, dest, base);
}
NOKPROBE_SYMBOL(process_fetch_insn)

/* Kprobe handler */
static nokprobe_inline void
__kprobe_trace_func(struct trace_kprobe *tk, struct pt_regs *regs,
		    struct trace_event_file *trace_file)
{
	struct kprobe_trace_entry_head *entry;
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	int size, dsize, pc;
	unsigned long irq_flags;
	struct trace_event_call *call = &tk->tp.call;

	WARN_ON(call != trace_file->event_call);

	if (trace_trigger_soft_disabled(trace_file))
		return;

	local_save_flags(irq_flags);
	pc = preempt_count();

	dsize = __get_data_size(&tk->tp, regs);
	size = sizeof(*entry) + tk->tp.size + dsize;

	event = trace_event_buffer_lock_reserve(&buffer, trace_file,
						call->event.type,
						size, irq_flags, pc);
	if (!event)
		return;

	entry = ring_buffer_event_data(event);
	entry->ip = (unsigned long)tk->rp.kp.addr;
	store_trace_args(&entry[1], &tk->tp, regs, sizeof(*entry), dsize);

	event_trigger_unlock_commit_regs(trace_file, buffer, event,
					 entry, irq_flags, pc, regs);
}

static void
kprobe_trace_func(struct trace_kprobe *tk, struct pt_regs *regs)
{
	struct event_file_link *link;

	list_for_each_entry_rcu(link, &tk->tp.files, list)
		__kprobe_trace_func(tk, regs, link->file);
}
NOKPROBE_SYMBOL(kprobe_trace_func);

/* Kretprobe handler */
static nokprobe_inline void
__kretprobe_trace_func(struct trace_kprobe *tk, struct kretprobe_instance *ri,
		       struct pt_regs *regs,
		       struct trace_event_file *trace_file)
{
	struct kretprobe_trace_entry_head *entry;
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	int size, pc, dsize;
	unsigned long irq_flags;
	struct trace_event_call *call = &tk->tp.call;

	WARN_ON(call != trace_file->event_call);

	if (trace_trigger_soft_disabled(trace_file))
		return;

	local_save_flags(irq_flags);
	pc = preempt_count();

	dsize = __get_data_size(&tk->tp, regs);
	size = sizeof(*entry) + tk->tp.size + dsize;

	event = trace_event_buffer_lock_reserve(&buffer, trace_file,
						call->event.type,
						size, irq_flags, pc);
	if (!event)
		return;

	entry = ring_buffer_event_data(event);
	entry->func = (unsigned long)tk->rp.kp.addr;
	entry->ret_ip = (unsigned long)ri->ret_addr;
	store_trace_args(&entry[1], &tk->tp, regs, sizeof(*entry), dsize);

	event_trigger_unlock_commit_regs(trace_file, buffer, event,
					 entry, irq_flags, pc, regs);
}

static void
kretprobe_trace_func(struct trace_kprobe *tk, struct kretprobe_instance *ri,
		     struct pt_regs *regs)
{
	struct event_file_link *link;

	list_for_each_entry_rcu(link, &tk->tp.files, list)
		__kretprobe_trace_func(tk, ri, regs, link->file);
}
NOKPROBE_SYMBOL(kretprobe_trace_func);

/* Event entry printers */
static enum print_line_t
print_kprobe_event(struct trace_iterator *iter, int flags,
		   struct trace_event *event)
{
	struct kprobe_trace_entry_head *field;
	struct trace_seq *s = &iter->seq;
	struct trace_probe *tp;

	field = (struct kprobe_trace_entry_head *)iter->ent;
	tp = container_of(event, struct trace_probe, call.event);

	trace_seq_printf(s, "%s: (", trace_event_name(&tp->call));

	if (!seq_print_ip_sym(s, field->ip, flags | TRACE_ITER_SYM_OFFSET))
		goto out;

	trace_seq_putc(s, ')');

	if (print_probe_args(s, tp->args, tp->nr_args,
			     (u8 *)&field[1], field) < 0)
		goto out;

	trace_seq_putc(s, '\n');
 out:
	return trace_handle_return(s);
}

static enum print_line_t
print_kretprobe_event(struct trace_iterator *iter, int flags,
		      struct trace_event *event)
{
	struct kretprobe_trace_entry_head *field;
	struct trace_seq *s = &iter->seq;
	struct trace_probe *tp;

	field = (struct kretprobe_trace_entry_head *)iter->ent;
	tp = container_of(event, struct trace_probe, call.event);

	trace_seq_printf(s, "%s: (", trace_event_name(&tp->call));

	if (!seq_print_ip_sym(s, field->ret_ip, flags | TRACE_ITER_SYM_OFFSET))
		goto out;

	trace_seq_puts(s, " <- ");

	if (!seq_print_ip_sym(s, field->func, flags & ~TRACE_ITER_SYM_OFFSET))
		goto out;

	trace_seq_putc(s, ')');

	if (print_probe_args(s, tp->args, tp->nr_args,
			     (u8 *)&field[1], field) < 0)
		goto out;

	trace_seq_putc(s, '\n');

 out:
	return trace_handle_return(s);
}


static int kprobe_event_define_fields(struct trace_event_call *event_call)
{
	int ret;
	struct kprobe_trace_entry_head field;
	struct trace_kprobe *tk = (struct trace_kprobe *)event_call->data;

	DEFINE_FIELD(unsigned long, ip, FIELD_STRING_IP, 0);

	return traceprobe_define_arg_fields(event_call, sizeof(field), &tk->tp);
}

static int kretprobe_event_define_fields(struct trace_event_call *event_call)
{
	int ret;
	struct kretprobe_trace_entry_head field;
	struct trace_kprobe *tk = (struct trace_kprobe *)event_call->data;

	DEFINE_FIELD(unsigned long, func, FIELD_STRING_FUNC, 0);
	DEFINE_FIELD(unsigned long, ret_ip, FIELD_STRING_RETIP, 0);

	return traceprobe_define_arg_fields(event_call, sizeof(field), &tk->tp);
}

#ifdef CONFIG_PERF_EVENTS

/* Kprobe profile handler */
static int
kprobe_perf_func(struct trace_kprobe *tk, struct pt_regs *regs)
{
	struct trace_event_call *call = &tk->tp.call;
	struct kprobe_trace_entry_head *entry;
	struct hlist_head *head;
	int size, __size, dsize;
	int rctx;

	if (bpf_prog_array_valid(call)) {
		unsigned long orig_ip = instruction_pointer(regs);
		int ret;

		ret = trace_call_bpf(call, regs);

		/*
		 * We need to check and see if we modified the pc of the
		 * pt_regs, and if so return 1 so that we don't do the
		 * single stepping.
		 */
		if (orig_ip != instruction_pointer(regs))
			return 1;
		if (!ret)
			return 0;
	}

	head = this_cpu_ptr(call->perf_events);
	if (hlist_empty(head))
		return 0;

	dsize = __get_data_size(&tk->tp, regs);
	__size = sizeof(*entry) + tk->tp.size + dsize;
	size = ALIGN(__size + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);

	entry = perf_trace_buf_alloc(size, NULL, &rctx);
	if (!entry)
		return 0;

	entry->ip = (unsigned long)tk->rp.kp.addr;
	memset(&entry[1], 0, dsize);
	store_trace_args(&entry[1], &tk->tp, regs, sizeof(*entry), dsize);
	perf_trace_buf_submit(entry, size, rctx, call->event.type, 1, regs,
			      head, NULL);
	return 0;
}
NOKPROBE_SYMBOL(kprobe_perf_func);

/* Kretprobe profile handler */
static void
kretprobe_perf_func(struct trace_kprobe *tk, struct kretprobe_instance *ri,
		    struct pt_regs *regs)
{
	struct trace_event_call *call = &tk->tp.call;
	struct kretprobe_trace_entry_head *entry;
	struct hlist_head *head;
	int size, __size, dsize;
	int rctx;

	if (bpf_prog_array_valid(call) && !trace_call_bpf(call, regs))
		return;

	head = this_cpu_ptr(call->perf_events);
	if (hlist_empty(head))
		return;

	dsize = __get_data_size(&tk->tp, regs);
	__size = sizeof(*entry) + tk->tp.size + dsize;
	size = ALIGN(__size + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);

	entry = perf_trace_buf_alloc(size, NULL, &rctx);
	if (!entry)
		return;

	entry->func = (unsigned long)tk->rp.kp.addr;
	entry->ret_ip = (unsigned long)ri->ret_addr;
	store_trace_args(&entry[1], &tk->tp, regs, sizeof(*entry), dsize);
	perf_trace_buf_submit(entry, size, rctx, call->event.type, 1, regs,
			      head, NULL);
}
NOKPROBE_SYMBOL(kretprobe_perf_func);

int bpf_get_kprobe_info(const struct perf_event *event, u32 *fd_type,
			const char **symbol, u64 *probe_offset,
			u64 *probe_addr, bool perf_type_tracepoint)
{
	const char *pevent = trace_event_name(event->tp_event);
	const char *group = event->tp_event->class->system;
	struct trace_kprobe *tk;

	if (perf_type_tracepoint)
		tk = find_trace_kprobe(pevent, group);
	else
		tk = event->tp_event->data;
	if (!tk)
		return -EINVAL;

	*fd_type = trace_kprobe_is_return(tk) ? BPF_FD_TYPE_KRETPROBE
					      : BPF_FD_TYPE_KPROBE;
	if (tk->symbol) {
		*symbol = tk->symbol;
		*probe_offset = tk->rp.kp.offset;
		*probe_addr = 0;
	} else {
		*symbol = NULL;
		*probe_offset = 0;
		*probe_addr = (unsigned long)tk->rp.kp.addr;
	}
	return 0;
}
#endif	/* CONFIG_PERF_EVENTS */

/*
 * called by perf_trace_init() or __ftrace_set_clr_event() under event_mutex.
 *
 * kprobe_trace_self_tests_init() does enable_trace_probe/disable_trace_probe
 * lockless, but we can't race with this __init function.
 */
static int kprobe_register(struct trace_event_call *event,
			   enum trace_reg type, void *data)
{
	struct trace_kprobe *tk = (struct trace_kprobe *)event->data;
	struct trace_event_file *file = data;

	switch (type) {
	case TRACE_REG_REGISTER:
		return enable_trace_kprobe(tk, file);
	case TRACE_REG_UNREGISTER:
		return disable_trace_kprobe(tk, file);

#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
		return enable_trace_kprobe(tk, NULL);
	case TRACE_REG_PERF_UNREGISTER:
		return disable_trace_kprobe(tk, NULL);
	case TRACE_REG_PERF_OPEN:
	case TRACE_REG_PERF_CLOSE:
	case TRACE_REG_PERF_ADD:
	case TRACE_REG_PERF_DEL:
		return 0;
#endif
	}
	return 0;
}

static int kprobe_dispatcher(struct kprobe *kp, struct pt_regs *regs)
{
	struct trace_kprobe *tk = container_of(kp, struct trace_kprobe, rp.kp);
	int ret = 0;

	raw_cpu_inc(*tk->nhit);

	if (tk->tp.flags & TP_FLAG_TRACE)
		kprobe_trace_func(tk, regs);
#ifdef CONFIG_PERF_EVENTS
	if (tk->tp.flags & TP_FLAG_PROFILE)
		ret = kprobe_perf_func(tk, regs);
#endif
	return ret;
}
NOKPROBE_SYMBOL(kprobe_dispatcher);

static int
kretprobe_dispatcher(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct trace_kprobe *tk = container_of(ri->rp, struct trace_kprobe, rp);

	raw_cpu_inc(*tk->nhit);

	if (tk->tp.flags & TP_FLAG_TRACE)
		kretprobe_trace_func(tk, ri, regs);
#ifdef CONFIG_PERF_EVENTS
	if (tk->tp.flags & TP_FLAG_PROFILE)
		kretprobe_perf_func(tk, ri, regs);
#endif
	return 0;	/* We don't tweek kernel, so just return 0 */
}
NOKPROBE_SYMBOL(kretprobe_dispatcher);

static struct trace_event_functions kretprobe_funcs = {
	.trace		= print_kretprobe_event
};

static struct trace_event_functions kprobe_funcs = {
	.trace		= print_kprobe_event
};

static inline void init_trace_event_call(struct trace_kprobe *tk,
					 struct trace_event_call *call)
{
	INIT_LIST_HEAD(&call->class->fields);
	if (trace_kprobe_is_return(tk)) {
		call->event.funcs = &kretprobe_funcs;
		call->class->define_fields = kretprobe_event_define_fields;
	} else {
		call->event.funcs = &kprobe_funcs;
		call->class->define_fields = kprobe_event_define_fields;
	}

	call->flags = TRACE_EVENT_FL_KPROBE;
	call->class->reg = kprobe_register;
	call->data = tk;
}

static int register_kprobe_event(struct trace_kprobe *tk)
{
	struct trace_event_call *call = &tk->tp.call;
	int ret = 0;

	init_trace_event_call(tk, call);

	ret = register_trace_event(&call->event);
	if (!ret)
		return -ENODEV;

	ret = trace_add_event_call(call);
	if (ret) {
		pr_info("Failed to register kprobe event: %s\n",
			trace_event_name(call));
		unregister_trace_event(&call->event);
	}
	return ret;
}

static int unregister_kprobe_event(struct trace_kprobe *tk)
{
	/* tp->event is unregistered in trace_remove_event_call() */
	return trace_remove_event_call(&tk->tp.call);
}

#ifdef CONFIG_PERF_EVENTS
/* create a trace_kprobe, but don't add it to global lists */
struct trace_event_call *
create_local_trace_kprobe(char *func, void *addr, unsigned long offs,
			  bool is_return)
{
	struct trace_kprobe *tk;
	int ret;
	char *event;

	/*
	 * local trace_kprobes are not added to dyn_event, so they are never
	 * searched in find_trace_kprobe(). Therefore, there is no concern of
	 * duplicated name here.
	 */
	event = func ? func : "DUMMY_EVENT";

	tk = alloc_trace_kprobe(KPROBE_EVENT_SYSTEM, event, (void *)addr, func,
				offs, 0 /* maxactive */, 0 /* nargs */,
				is_return);

	if (IS_ERR(tk)) {
		pr_info("Failed to allocate trace_probe.(%d)\n",
			(int)PTR_ERR(tk));
		return ERR_CAST(tk);
	}

	init_trace_event_call(tk, &tk->tp.call);

	if (traceprobe_set_print_fmt(&tk->tp, trace_kprobe_is_return(tk)) < 0) {
		ret = -ENOMEM;
		goto error;
	}

	ret = __register_trace_kprobe(tk);
	if (ret < 0)
		goto error;

	return &tk->tp.call;
error:
	free_trace_kprobe(tk);
	return ERR_PTR(ret);
}

void destroy_local_trace_kprobe(struct trace_event_call *event_call)
{
	struct trace_kprobe *tk;

	tk = container_of(event_call, struct trace_kprobe, tp.call);

	if (trace_probe_is_enabled(&tk->tp)) {
		WARN_ON(1);
		return;
	}

	__unregister_trace_kprobe(tk);

	free_trace_kprobe(tk);
}
#endif /* CONFIG_PERF_EVENTS */

static __init void enable_boot_kprobe_events(void)
{
	struct trace_array *tr = top_trace_array();
	struct trace_event_file *file;
	struct trace_kprobe *tk;
	struct dyn_event *pos;

	mutex_lock(&event_mutex);
	for_each_trace_kprobe(tk, pos) {
		list_for_each_entry(file, &tr->events, list)
			if (file->event_call == &tk->tp.call)
				trace_event_enable_disable(file, 1, 0);
	}
	mutex_unlock(&event_mutex);
}

static __init void setup_boot_kprobe_events(void)
{
	char *p, *cmd = kprobe_boot_events_buf;
	int ret;

	strreplace(kprobe_boot_events_buf, ',', ' ');

	while (cmd && *cmd != '\0') {
		p = strchr(cmd, ';');
		if (p)
			*p++ = '\0';

		ret = trace_run_command(cmd, create_or_delete_trace_kprobe);
		if (ret)
			pr_warn("Failed to add event(%d): %s\n", ret, cmd);
		else
			kprobe_boot_events_enabled = true;

		cmd = p;
	}

	enable_boot_kprobe_events();
}

/* Make a tracefs interface for controlling probe points */
static __init int init_kprobe_trace(void)
{
	struct dentry *d_tracer;
	struct dentry *entry;
	int ret;

	ret = dyn_event_register(&trace_kprobe_ops);
	if (ret)
		return ret;

	if (register_module_notifier(&trace_kprobe_module_nb))
		return -EINVAL;

	d_tracer = tracing_init_dentry();
	if (IS_ERR(d_tracer))
		return 0;

	entry = tracefs_create_file("kprobe_events", 0644, d_tracer,
				    NULL, &kprobe_events_ops);

	/* Event list interface */
	if (!entry)
		pr_warn("Could not create tracefs 'kprobe_events' entry\n");

	/* Profile interface */
	entry = tracefs_create_file("kprobe_profile", 0444, d_tracer,
				    NULL, &kprobe_profile_ops);

	if (!entry)
		pr_warn("Could not create tracefs 'kprobe_profile' entry\n");

	setup_boot_kprobe_events();

	return 0;
}
fs_initcall(init_kprobe_trace);


#ifdef CONFIG_FTRACE_STARTUP_TEST
static __init struct trace_event_file *
find_trace_probe_file(struct trace_kprobe *tk, struct trace_array *tr)
{
	struct trace_event_file *file;

	list_for_each_entry(file, &tr->events, list)
		if (file->event_call == &tk->tp.call)
			return file;

	return NULL;
}

/*
 * Nobody but us can call enable_trace_kprobe/disable_trace_kprobe at this
 * stage, we can do this lockless.
 */
static __init int kprobe_trace_self_tests_init(void)
{
	int ret, warn = 0;
	int (*target)(int, int, int, int, int, int);
	struct trace_kprobe *tk;
	struct trace_event_file *file;

	if (tracing_is_disabled())
		return -ENODEV;

	if (kprobe_boot_events_enabled) {
		pr_info("Skipping kprobe tests due to kprobe_event on cmdline\n");
		return 0;
	}

	target = kprobe_trace_selftest_target;

	pr_info("Testing kprobe tracing: ");

	ret = trace_run_command("p:testprobe kprobe_trace_selftest_target $stack $stack0 +0($stack)",
				create_or_delete_trace_kprobe);
	if (WARN_ON_ONCE(ret)) {
		pr_warn("error on probing function entry.\n");
		warn++;
	} else {
		/* Enable trace point */
		tk = find_trace_kprobe("testprobe", KPROBE_EVENT_SYSTEM);
		if (WARN_ON_ONCE(tk == NULL)) {
			pr_warn("error on getting new probe.\n");
			warn++;
		} else {
			file = find_trace_probe_file(tk, top_trace_array());
			if (WARN_ON_ONCE(file == NULL)) {
				pr_warn("error on getting probe file.\n");
				warn++;
			} else
				enable_trace_kprobe(tk, file);
		}
	}

	ret = trace_run_command("r:testprobe2 kprobe_trace_selftest_target $retval",
				create_or_delete_trace_kprobe);
	if (WARN_ON_ONCE(ret)) {
		pr_warn("error on probing function return.\n");
		warn++;
	} else {
		/* Enable trace point */
		tk = find_trace_kprobe("testprobe2", KPROBE_EVENT_SYSTEM);
		if (WARN_ON_ONCE(tk == NULL)) {
			pr_warn("error on getting 2nd new probe.\n");
			warn++;
		} else {
			file = find_trace_probe_file(tk, top_trace_array());
			if (WARN_ON_ONCE(file == NULL)) {
				pr_warn("error on getting probe file.\n");
				warn++;
			} else
				enable_trace_kprobe(tk, file);
		}
	}

	if (warn)
		goto end;

	ret = target(1, 2, 3, 4, 5, 6);

	/*
	 * Not expecting an error here, the check is only to prevent the
	 * optimizer from removing the call to target() as otherwise there
	 * are no side-effects and the call is never performed.
	 */
	if (ret != 21)
		warn++;

	/* Disable trace points before removing it */
	tk = find_trace_kprobe("testprobe", KPROBE_EVENT_SYSTEM);
	if (WARN_ON_ONCE(tk == NULL)) {
		pr_warn("error on getting test probe.\n");
		warn++;
	} else {
		if (trace_kprobe_nhit(tk) != 1) {
			pr_warn("incorrect number of testprobe hits\n");
			warn++;
		}

		file = find_trace_probe_file(tk, top_trace_array());
		if (WARN_ON_ONCE(file == NULL)) {
			pr_warn("error on getting probe file.\n");
			warn++;
		} else
			disable_trace_kprobe(tk, file);
	}

	tk = find_trace_kprobe("testprobe2", KPROBE_EVENT_SYSTEM);
	if (WARN_ON_ONCE(tk == NULL)) {
		pr_warn("error on getting 2nd test probe.\n");
		warn++;
	} else {
		if (trace_kprobe_nhit(tk) != 1) {
			pr_warn("incorrect number of testprobe2 hits\n");
			warn++;
		}

		file = find_trace_probe_file(tk, top_trace_array());
		if (WARN_ON_ONCE(file == NULL)) {
			pr_warn("error on getting probe file.\n");
			warn++;
		} else
			disable_trace_kprobe(tk, file);
	}

	ret = trace_run_command("-:testprobe", create_or_delete_trace_kprobe);
	if (WARN_ON_ONCE(ret)) {
		pr_warn("error on deleting a probe.\n");
		warn++;
	}

	ret = trace_run_command("-:testprobe2", create_or_delete_trace_kprobe);
	if (WARN_ON_ONCE(ret)) {
		pr_warn("error on deleting a probe.\n");
		warn++;
	}

end:
	ret = dyn_events_release_all(&trace_kprobe_ops);
	if (WARN_ON_ONCE(ret)) {
		pr_warn("error on cleaning up probes.\n");
		warn++;
	}
	/*
	 * Wait for the optimizer work to finish. Otherwise it might fiddle
	 * with probes in already freed __init text.
	 */
	wait_for_kprobe_optimizer();
	if (warn)
		pr_cont("NG: Some tests are failed. Please check them.\n");
	else
		pr_cont("OK\n");
	return 0;
}

late_initcall(kprobe_trace_self_tests_init);

#endif
