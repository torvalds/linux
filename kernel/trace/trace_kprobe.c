/*
 * Kprobes-based tracing events
 *
 * Created by Masami Hiramatsu <mhiramat@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/uaccess.h>

#include "trace_probe.h"

#define KPROBE_EVENT_SYSTEM "kprobes"

/**
 * Kprobe event core functions
 */

struct trace_probe {
	struct list_head	list;
	struct kretprobe	rp;	/* Use rp.kp for kprobe use */
	unsigned long 		nhit;
	unsigned int		flags;	/* For TP_FLAG_* */
	const char		*symbol;	/* symbol name */
	struct ftrace_event_class	class;
	struct ftrace_event_call	call;
	ssize_t			size;		/* trace entry size */
	unsigned int		nr_args;
	struct probe_arg	args[];
};

#define SIZEOF_TRACE_PROBE(n)			\
	(offsetof(struct trace_probe, args) +	\
	(sizeof(struct probe_arg) * (n)))


static __kprobes bool trace_probe_is_return(struct trace_probe *tp)
{
	return tp->rp.handler != NULL;
}

static __kprobes const char *trace_probe_symbol(struct trace_probe *tp)
{
	return tp->symbol ? tp->symbol : "unknown";
}

static __kprobes unsigned long trace_probe_offset(struct trace_probe *tp)
{
	return tp->rp.kp.offset;
}

static __kprobes bool trace_probe_is_enabled(struct trace_probe *tp)
{
	return !!(tp->flags & (TP_FLAG_TRACE | TP_FLAG_PROFILE));
}

static __kprobes bool trace_probe_is_registered(struct trace_probe *tp)
{
	return !!(tp->flags & TP_FLAG_REGISTERED);
}

static __kprobes bool trace_probe_has_gone(struct trace_probe *tp)
{
	return !!(kprobe_gone(&tp->rp.kp));
}

static __kprobes bool trace_probe_within_module(struct trace_probe *tp,
						struct module *mod)
{
	int len = strlen(mod->name);
	const char *name = trace_probe_symbol(tp);
	return strncmp(mod->name, name, len) == 0 && name[len] == ':';
}

static __kprobes bool trace_probe_is_on_module(struct trace_probe *tp)
{
	return !!strchr(trace_probe_symbol(tp), ':');
}

static int register_probe_event(struct trace_probe *tp);
static void unregister_probe_event(struct trace_probe *tp);

static DEFINE_MUTEX(probe_lock);
static LIST_HEAD(probe_list);

static int kprobe_dispatcher(struct kprobe *kp, struct pt_regs *regs);
static int kretprobe_dispatcher(struct kretprobe_instance *ri,
				struct pt_regs *regs);

/*
 * Allocate new trace_probe and initialize it (including kprobes).
 */
static struct trace_probe *alloc_trace_probe(const char *group,
					     const char *event,
					     void *addr,
					     const char *symbol,
					     unsigned long offs,
					     int nargs, bool is_return)
{
	struct trace_probe *tp;
	int ret = -ENOMEM;

	tp = kzalloc(SIZEOF_TRACE_PROBE(nargs), GFP_KERNEL);
	if (!tp)
		return ERR_PTR(ret);

	if (symbol) {
		tp->symbol = kstrdup(symbol, GFP_KERNEL);
		if (!tp->symbol)
			goto error;
		tp->rp.kp.symbol_name = tp->symbol;
		tp->rp.kp.offset = offs;
	} else
		tp->rp.kp.addr = addr;

	if (is_return)
		tp->rp.handler = kretprobe_dispatcher;
	else
		tp->rp.kp.pre_handler = kprobe_dispatcher;

	if (!event || !is_good_name(event)) {
		ret = -EINVAL;
		goto error;
	}

	tp->call.class = &tp->class;
	tp->call.name = kstrdup(event, GFP_KERNEL);
	if (!tp->call.name)
		goto error;

	if (!group || !is_good_name(group)) {
		ret = -EINVAL;
		goto error;
	}

	tp->class.system = kstrdup(group, GFP_KERNEL);
	if (!tp->class.system)
		goto error;

	INIT_LIST_HEAD(&tp->list);
	return tp;
error:
	kfree(tp->call.name);
	kfree(tp->symbol);
	kfree(tp);
	return ERR_PTR(ret);
}

static void free_trace_probe(struct trace_probe *tp)
{
	int i;

	for (i = 0; i < tp->nr_args; i++)
		traceprobe_free_probe_arg(&tp->args[i]);

	kfree(tp->call.class->system);
	kfree(tp->call.name);
	kfree(tp->symbol);
	kfree(tp);
}

static struct trace_probe *find_trace_probe(const char *event,
					    const char *group)
{
	struct trace_probe *tp;

	list_for_each_entry(tp, &probe_list, list)
		if (strcmp(tp->call.name, event) == 0 &&
		    strcmp(tp->call.class->system, group) == 0)
			return tp;
	return NULL;
}

/* Enable trace_probe - @flag must be TP_FLAG_TRACE or TP_FLAG_PROFILE */
static int enable_trace_probe(struct trace_probe *tp, int flag)
{
	int ret = 0;

	tp->flags |= flag;
	if (trace_probe_is_enabled(tp) && trace_probe_is_registered(tp) &&
	    !trace_probe_has_gone(tp)) {
		if (trace_probe_is_return(tp))
			ret = enable_kretprobe(&tp->rp);
		else
			ret = enable_kprobe(&tp->rp.kp);
	}

	return ret;
}

/* Disable trace_probe - @flag must be TP_FLAG_TRACE or TP_FLAG_PROFILE */
static void disable_trace_probe(struct trace_probe *tp, int flag)
{
	tp->flags &= ~flag;
	if (!trace_probe_is_enabled(tp) && trace_probe_is_registered(tp)) {
		if (trace_probe_is_return(tp))
			disable_kretprobe(&tp->rp);
		else
			disable_kprobe(&tp->rp.kp);
	}
}

/* Internal register function - just handle k*probes and flags */
static int __register_trace_probe(struct trace_probe *tp)
{
	int i, ret;

	if (trace_probe_is_registered(tp))
		return -EINVAL;

	for (i = 0; i < tp->nr_args; i++)
		traceprobe_update_arg(&tp->args[i]);

	/* Set/clear disabled flag according to tp->flag */
	if (trace_probe_is_enabled(tp))
		tp->rp.kp.flags &= ~KPROBE_FLAG_DISABLED;
	else
		tp->rp.kp.flags |= KPROBE_FLAG_DISABLED;

	if (trace_probe_is_return(tp))
		ret = register_kretprobe(&tp->rp);
	else
		ret = register_kprobe(&tp->rp.kp);

	if (ret == 0)
		tp->flags |= TP_FLAG_REGISTERED;
	else {
		pr_warning("Could not insert probe at %s+%lu: %d\n",
			   trace_probe_symbol(tp), trace_probe_offset(tp), ret);
		if (ret == -ENOENT && trace_probe_is_on_module(tp)) {
			pr_warning("This probe might be able to register after"
				   "target module is loaded. Continue.\n");
			ret = 0;
		} else if (ret == -EILSEQ) {
			pr_warning("Probing address(0x%p) is not an "
				   "instruction boundary.\n",
				   tp->rp.kp.addr);
			ret = -EINVAL;
		}
	}

	return ret;
}

/* Internal unregister function - just handle k*probes and flags */
static void __unregister_trace_probe(struct trace_probe *tp)
{
	if (trace_probe_is_registered(tp)) {
		if (trace_probe_is_return(tp))
			unregister_kretprobe(&tp->rp);
		else
			unregister_kprobe(&tp->rp.kp);
		tp->flags &= ~TP_FLAG_REGISTERED;
		/* Cleanup kprobe for reuse */
		if (tp->rp.kp.symbol_name)
			tp->rp.kp.addr = NULL;
	}
}

/* Unregister a trace_probe and probe_event: call with locking probe_lock */
static int unregister_trace_probe(struct trace_probe *tp)
{
	/* Enabled event can not be unregistered */
	if (trace_probe_is_enabled(tp))
		return -EBUSY;

	__unregister_trace_probe(tp);
	list_del(&tp->list);
	unregister_probe_event(tp);

	return 0;
}

/* Register a trace_probe and probe_event */
static int register_trace_probe(struct trace_probe *tp)
{
	struct trace_probe *old_tp;
	int ret;

	mutex_lock(&probe_lock);

	/* Delete old (same name) event if exist */
	old_tp = find_trace_probe(tp->call.name, tp->call.class->system);
	if (old_tp) {
		ret = unregister_trace_probe(old_tp);
		if (ret < 0)
			goto end;
		free_trace_probe(old_tp);
	}

	/* Register new event */
	ret = register_probe_event(tp);
	if (ret) {
		pr_warning("Failed to register probe event(%d)\n", ret);
		goto end;
	}

	/* Register k*probe */
	ret = __register_trace_probe(tp);
	if (ret < 0)
		unregister_probe_event(tp);
	else
		list_add_tail(&tp->list, &probe_list);

end:
	mutex_unlock(&probe_lock);
	return ret;
}

/* Module notifier call back, checking event on the module */
static int trace_probe_module_callback(struct notifier_block *nb,
				       unsigned long val, void *data)
{
	struct module *mod = data;
	struct trace_probe *tp;
	int ret;

	if (val != MODULE_STATE_COMING)
		return NOTIFY_DONE;

	/* Update probes on coming module */
	mutex_lock(&probe_lock);
	list_for_each_entry(tp, &probe_list, list) {
		if (trace_probe_within_module(tp, mod)) {
			/* Don't need to check busy - this should have gone. */
			__unregister_trace_probe(tp);
			ret = __register_trace_probe(tp);
			if (ret)
				pr_warning("Failed to re-register probe %s on"
					   "%s: %d\n",
					   tp->call.name, mod->name, ret);
		}
	}
	mutex_unlock(&probe_lock);

	return NOTIFY_DONE;
}

static struct notifier_block trace_probe_module_nb = {
	.notifier_call = trace_probe_module_callback,
	.priority = 1	/* Invoked after kprobe module callback */
};

static int create_trace_probe(int argc, char **argv)
{
	/*
	 * Argument syntax:
	 *  - Add kprobe: p[:[GRP/]EVENT] [MOD:]KSYM[+OFFS]|KADDR [FETCHARGS]
	 *  - Add kretprobe: r[:[GRP/]EVENT] [MOD:]KSYM[+0] [FETCHARGS]
	 * Fetch args:
	 *  $retval	: fetch return value
	 *  $stack	: fetch stack address
	 *  $stackN	: fetch Nth of stack (N:0-)
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
	struct trace_probe *tp;
	int i, ret = 0;
	bool is_return = false, is_delete = false;
	char *symbol = NULL, *event = NULL, *group = NULL;
	char *arg;
	unsigned long offset = 0;
	void *addr = NULL;
	char buf[MAX_EVENT_NAME_LEN];

	/* argc must be >= 1 */
	if (argv[0][0] == 'p')
		is_return = false;
	else if (argv[0][0] == 'r')
		is_return = true;
	else if (argv[0][0] == '-')
		is_delete = true;
	else {
		pr_info("Probe definition must be started with 'p', 'r' or"
			" '-'.\n");
		return -EINVAL;
	}

	if (argv[0][1] == ':') {
		event = &argv[0][2];
		if (strchr(event, '/')) {
			group = event;
			event = strchr(group, '/') + 1;
			event[-1] = '\0';
			if (strlen(group) == 0) {
				pr_info("Group name is not specified\n");
				return -EINVAL;
			}
		}
		if (strlen(event) == 0) {
			pr_info("Event name is not specified\n");
			return -EINVAL;
		}
	}
	if (!group)
		group = KPROBE_EVENT_SYSTEM;

	if (is_delete) {
		if (!event) {
			pr_info("Delete command needs an event name.\n");
			return -EINVAL;
		}
		mutex_lock(&probe_lock);
		tp = find_trace_probe(event, group);
		if (!tp) {
			mutex_unlock(&probe_lock);
			pr_info("Event %s/%s doesn't exist.\n", group, event);
			return -ENOENT;
		}
		/* delete an event */
		ret = unregister_trace_probe(tp);
		if (ret == 0)
			free_trace_probe(tp);
		mutex_unlock(&probe_lock);
		return ret;
	}

	if (argc < 2) {
		pr_info("Probe point is not specified.\n");
		return -EINVAL;
	}
	if (isdigit(argv[1][0])) {
		if (is_return) {
			pr_info("Return probe point must be a symbol.\n");
			return -EINVAL;
		}
		/* an address specified */
		ret = kstrtoul(&argv[1][0], 0, (unsigned long *)&addr);
		if (ret) {
			pr_info("Failed to parse address.\n");
			return ret;
		}
	} else {
		/* a symbol specified */
		symbol = argv[1];
		/* TODO: support .init module functions */
		ret = traceprobe_split_symbol_offset(symbol, &offset);
		if (ret) {
			pr_info("Failed to parse symbol.\n");
			return ret;
		}
		if (offset && is_return) {
			pr_info("Return probe must be used without offset.\n");
			return -EINVAL;
		}
	}
	argc -= 2; argv += 2;

	/* setup a probe */
	if (!event) {
		/* Make a new event name */
		if (symbol)
			snprintf(buf, MAX_EVENT_NAME_LEN, "%c_%s_%ld",
				 is_return ? 'r' : 'p', symbol, offset);
		else
			snprintf(buf, MAX_EVENT_NAME_LEN, "%c_0x%p",
				 is_return ? 'r' : 'p', addr);
		event = buf;
	}
	tp = alloc_trace_probe(group, event, addr, symbol, offset, argc,
			       is_return);
	if (IS_ERR(tp)) {
		pr_info("Failed to allocate trace_probe.(%d)\n",
			(int)PTR_ERR(tp));
		return PTR_ERR(tp);
	}

	/* parse arguments */
	ret = 0;
	for (i = 0; i < argc && i < MAX_TRACE_ARGS; i++) {
		/* Increment count for freeing args in error case */
		tp->nr_args++;

		/* Parse argument name */
		arg = strchr(argv[i], '=');
		if (arg) {
			*arg++ = '\0';
			tp->args[i].name = kstrdup(argv[i], GFP_KERNEL);
		} else {
			arg = argv[i];
			/* If argument name is omitted, set "argN" */
			snprintf(buf, MAX_EVENT_NAME_LEN, "arg%d", i + 1);
			tp->args[i].name = kstrdup(buf, GFP_KERNEL);
		}

		if (!tp->args[i].name) {
			pr_info("Failed to allocate argument[%d] name.\n", i);
			ret = -ENOMEM;
			goto error;
		}

		if (!is_good_name(tp->args[i].name)) {
			pr_info("Invalid argument[%d] name: %s\n",
				i, tp->args[i].name);
			ret = -EINVAL;
			goto error;
		}

		if (traceprobe_conflict_field_name(tp->args[i].name,
							tp->args, i)) {
			pr_info("Argument[%d] name '%s' conflicts with "
				"another field.\n", i, argv[i]);
			ret = -EINVAL;
			goto error;
		}

		/* Parse fetch argument */
		ret = traceprobe_parse_probe_arg(arg, &tp->size, &tp->args[i],
						is_return, true);
		if (ret) {
			pr_info("Parse error at argument[%d]. (%d)\n", i, ret);
			goto error;
		}
	}

	ret = register_trace_probe(tp);
	if (ret)
		goto error;
	return 0;

error:
	free_trace_probe(tp);
	return ret;
}

static int release_all_trace_probes(void)
{
	struct trace_probe *tp;
	int ret = 0;

	mutex_lock(&probe_lock);
	/* Ensure no probe is in use. */
	list_for_each_entry(tp, &probe_list, list)
		if (trace_probe_is_enabled(tp)) {
			ret = -EBUSY;
			goto end;
		}
	/* TODO: Use batch unregistration */
	while (!list_empty(&probe_list)) {
		tp = list_entry(probe_list.next, struct trace_probe, list);
		unregister_trace_probe(tp);
		free_trace_probe(tp);
	}

end:
	mutex_unlock(&probe_lock);

	return ret;
}

/* Probes listing interfaces */
static void *probes_seq_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&probe_lock);
	return seq_list_start(&probe_list, *pos);
}

static void *probes_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	return seq_list_next(v, &probe_list, pos);
}

static void probes_seq_stop(struct seq_file *m, void *v)
{
	mutex_unlock(&probe_lock);
}

static int probes_seq_show(struct seq_file *m, void *v)
{
	struct trace_probe *tp = v;
	int i;

	seq_printf(m, "%c", trace_probe_is_return(tp) ? 'r' : 'p');
	seq_printf(m, ":%s/%s", tp->call.class->system, tp->call.name);

	if (!tp->symbol)
		seq_printf(m, " 0x%p", tp->rp.kp.addr);
	else if (tp->rp.kp.offset)
		seq_printf(m, " %s+%u", trace_probe_symbol(tp),
			   tp->rp.kp.offset);
	else
		seq_printf(m, " %s", trace_probe_symbol(tp));

	for (i = 0; i < tp->nr_args; i++)
		seq_printf(m, " %s=%s", tp->args[i].name, tp->args[i].comm);
	seq_printf(m, "\n");

	return 0;
}

static const struct seq_operations probes_seq_op = {
	.start  = probes_seq_start,
	.next   = probes_seq_next,
	.stop   = probes_seq_stop,
	.show   = probes_seq_show
};

static int probes_open(struct inode *inode, struct file *file)
{
	int ret;

	if ((file->f_mode & FMODE_WRITE) && (file->f_flags & O_TRUNC)) {
		ret = release_all_trace_probes();
		if (ret < 0)
			return ret;
	}

	return seq_open(file, &probes_seq_op);
}

static ssize_t probes_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *ppos)
{
	return traceprobe_probes_write(file, buffer, count, ppos,
			create_trace_probe);
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
	struct trace_probe *tp = v;

	seq_printf(m, "  %-44s %15lu %15lu\n", tp->call.name, tp->nhit,
		   tp->rp.kp.nmissed);

	return 0;
}

static const struct seq_operations profile_seq_op = {
	.start  = probes_seq_start,
	.next   = probes_seq_next,
	.stop   = probes_seq_stop,
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

/* Sum up total data length for dynamic arraies (strings) */
static __kprobes int __get_data_size(struct trace_probe *tp,
				     struct pt_regs *regs)
{
	int i, ret = 0;
	u32 len;

	for (i = 0; i < tp->nr_args; i++)
		if (unlikely(tp->args[i].fetch_size.fn)) {
			call_fetch(&tp->args[i].fetch_size, regs, &len);
			ret += len;
		}

	return ret;
}

/* Store the value of each argument */
static __kprobes void store_trace_args(int ent_size, struct trace_probe *tp,
				       struct pt_regs *regs,
				       u8 *data, int maxlen)
{
	int i;
	u32 end = tp->size;
	u32 *dl;	/* Data (relative) location */

	for (i = 0; i < tp->nr_args; i++) {
		if (unlikely(tp->args[i].fetch_size.fn)) {
			/*
			 * First, we set the relative location and
			 * maximum data length to *dl
			 */
			dl = (u32 *)(data + tp->args[i].offset);
			*dl = make_data_rloc(maxlen, end - tp->args[i].offset);
			/* Then try to fetch string or dynamic array data */
			call_fetch(&tp->args[i].fetch, regs, dl);
			/* Reduce maximum length */
			end += get_rloc_len(*dl);
			maxlen -= get_rloc_len(*dl);
			/* Trick here, convert data_rloc to data_loc */
			*dl = convert_rloc_to_loc(*dl,
				 ent_size + tp->args[i].offset);
		} else
			/* Just fetching data normally */
			call_fetch(&tp->args[i].fetch, regs,
				   data + tp->args[i].offset);
	}
}

/* Kprobe handler */
static __kprobes void kprobe_trace_func(struct kprobe *kp, struct pt_regs *regs)
{
	struct trace_probe *tp = container_of(kp, struct trace_probe, rp.kp);
	struct kprobe_trace_entry_head *entry;
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	int size, dsize, pc;
	unsigned long irq_flags;
	struct ftrace_event_call *call = &tp->call;

	local_save_flags(irq_flags);
	pc = preempt_count();

	dsize = __get_data_size(tp, regs);
	size = sizeof(*entry) + tp->size + dsize;

	event = trace_current_buffer_lock_reserve(&buffer, call->event.type,
						  size, irq_flags, pc);
	if (!event)
		return;

	entry = ring_buffer_event_data(event);
	entry->ip = (unsigned long)kp->addr;
	store_trace_args(sizeof(*entry), tp, regs, (u8 *)&entry[1], dsize);

	if (!filter_current_check_discard(buffer, call, entry, event))
		trace_buffer_unlock_commit_regs(buffer, event,
						irq_flags, pc, regs);
}

/* Kretprobe handler */
static __kprobes void kretprobe_trace_func(struct kretprobe_instance *ri,
					  struct pt_regs *regs)
{
	struct trace_probe *tp = container_of(ri->rp, struct trace_probe, rp);
	struct kretprobe_trace_entry_head *entry;
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	int size, pc, dsize;
	unsigned long irq_flags;
	struct ftrace_event_call *call = &tp->call;

	local_save_flags(irq_flags);
	pc = preempt_count();

	dsize = __get_data_size(tp, regs);
	size = sizeof(*entry) + tp->size + dsize;

	event = trace_current_buffer_lock_reserve(&buffer, call->event.type,
						  size, irq_flags, pc);
	if (!event)
		return;

	entry = ring_buffer_event_data(event);
	entry->func = (unsigned long)tp->rp.kp.addr;
	entry->ret_ip = (unsigned long)ri->ret_addr;
	store_trace_args(sizeof(*entry), tp, regs, (u8 *)&entry[1], dsize);

	if (!filter_current_check_discard(buffer, call, entry, event))
		trace_buffer_unlock_commit_regs(buffer, event,
						irq_flags, pc, regs);
}

/* Event entry printers */
enum print_line_t
print_kprobe_event(struct trace_iterator *iter, int flags,
		   struct trace_event *event)
{
	struct kprobe_trace_entry_head *field;
	struct trace_seq *s = &iter->seq;
	struct trace_probe *tp;
	u8 *data;
	int i;

	field = (struct kprobe_trace_entry_head *)iter->ent;
	tp = container_of(event, struct trace_probe, call.event);

	if (!trace_seq_printf(s, "%s: (", tp->call.name))
		goto partial;

	if (!seq_print_ip_sym(s, field->ip, flags | TRACE_ITER_SYM_OFFSET))
		goto partial;

	if (!trace_seq_puts(s, ")"))
		goto partial;

	data = (u8 *)&field[1];
	for (i = 0; i < tp->nr_args; i++)
		if (!tp->args[i].type->print(s, tp->args[i].name,
					     data + tp->args[i].offset, field))
			goto partial;

	if (!trace_seq_puts(s, "\n"))
		goto partial;

	return TRACE_TYPE_HANDLED;
partial:
	return TRACE_TYPE_PARTIAL_LINE;
}

enum print_line_t
print_kretprobe_event(struct trace_iterator *iter, int flags,
		      struct trace_event *event)
{
	struct kretprobe_trace_entry_head *field;
	struct trace_seq *s = &iter->seq;
	struct trace_probe *tp;
	u8 *data;
	int i;

	field = (struct kretprobe_trace_entry_head *)iter->ent;
	tp = container_of(event, struct trace_probe, call.event);

	if (!trace_seq_printf(s, "%s: (", tp->call.name))
		goto partial;

	if (!seq_print_ip_sym(s, field->ret_ip, flags | TRACE_ITER_SYM_OFFSET))
		goto partial;

	if (!trace_seq_puts(s, " <- "))
		goto partial;

	if (!seq_print_ip_sym(s, field->func, flags & ~TRACE_ITER_SYM_OFFSET))
		goto partial;

	if (!trace_seq_puts(s, ")"))
		goto partial;

	data = (u8 *)&field[1];
	for (i = 0; i < tp->nr_args; i++)
		if (!tp->args[i].type->print(s, tp->args[i].name,
					     data + tp->args[i].offset, field))
			goto partial;

	if (!trace_seq_puts(s, "\n"))
		goto partial;

	return TRACE_TYPE_HANDLED;
partial:
	return TRACE_TYPE_PARTIAL_LINE;
}


static int kprobe_event_define_fields(struct ftrace_event_call *event_call)
{
	int ret, i;
	struct kprobe_trace_entry_head field;
	struct trace_probe *tp = (struct trace_probe *)event_call->data;

	DEFINE_FIELD(unsigned long, ip, FIELD_STRING_IP, 0);
	/* Set argument names as fields */
	for (i = 0; i < tp->nr_args; i++) {
		ret = trace_define_field(event_call, tp->args[i].type->fmttype,
					 tp->args[i].name,
					 sizeof(field) + tp->args[i].offset,
					 tp->args[i].type->size,
					 tp->args[i].type->is_signed,
					 FILTER_OTHER);
		if (ret)
			return ret;
	}
	return 0;
}

static int kretprobe_event_define_fields(struct ftrace_event_call *event_call)
{
	int ret, i;
	struct kretprobe_trace_entry_head field;
	struct trace_probe *tp = (struct trace_probe *)event_call->data;

	DEFINE_FIELD(unsigned long, func, FIELD_STRING_FUNC, 0);
	DEFINE_FIELD(unsigned long, ret_ip, FIELD_STRING_RETIP, 0);
	/* Set argument names as fields */
	for (i = 0; i < tp->nr_args; i++) {
		ret = trace_define_field(event_call, tp->args[i].type->fmttype,
					 tp->args[i].name,
					 sizeof(field) + tp->args[i].offset,
					 tp->args[i].type->size,
					 tp->args[i].type->is_signed,
					 FILTER_OTHER);
		if (ret)
			return ret;
	}
	return 0;
}

static int __set_print_fmt(struct trace_probe *tp, char *buf, int len)
{
	int i;
	int pos = 0;

	const char *fmt, *arg;

	if (!trace_probe_is_return(tp)) {
		fmt = "(%lx)";
		arg = "REC->" FIELD_STRING_IP;
	} else {
		fmt = "(%lx <- %lx)";
		arg = "REC->" FIELD_STRING_FUNC ", REC->" FIELD_STRING_RETIP;
	}

	/* When len=0, we just calculate the needed length */
#define LEN_OR_ZERO (len ? len - pos : 0)

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\"%s", fmt);

	for (i = 0; i < tp->nr_args; i++) {
		pos += snprintf(buf + pos, LEN_OR_ZERO, " %s=%s",
				tp->args[i].name, tp->args[i].type->fmt);
	}

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\", %s", arg);

	for (i = 0; i < tp->nr_args; i++) {
		if (strcmp(tp->args[i].type->name, "string") == 0)
			pos += snprintf(buf + pos, LEN_OR_ZERO,
					", __get_str(%s)",
					tp->args[i].name);
		else
			pos += snprintf(buf + pos, LEN_OR_ZERO, ", REC->%s",
					tp->args[i].name);
	}

#undef LEN_OR_ZERO

	/* return the length of print_fmt */
	return pos;
}

static int set_print_fmt(struct trace_probe *tp)
{
	int len;
	char *print_fmt;

	/* First: called with 0 length to calculate the needed length */
	len = __set_print_fmt(tp, NULL, 0);
	print_fmt = kmalloc(len + 1, GFP_KERNEL);
	if (!print_fmt)
		return -ENOMEM;

	/* Second: actually write the @print_fmt */
	__set_print_fmt(tp, print_fmt, len + 1);
	tp->call.print_fmt = print_fmt;

	return 0;
}

#ifdef CONFIG_PERF_EVENTS

/* Kprobe profile handler */
static __kprobes void kprobe_perf_func(struct kprobe *kp,
					 struct pt_regs *regs)
{
	struct trace_probe *tp = container_of(kp, struct trace_probe, rp.kp);
	struct ftrace_event_call *call = &tp->call;
	struct kprobe_trace_entry_head *entry;
	struct hlist_head *head;
	int size, __size, dsize;
	int rctx;

	dsize = __get_data_size(tp, regs);
	__size = sizeof(*entry) + tp->size + dsize;
	size = ALIGN(__size + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);
	if (WARN_ONCE(size > PERF_MAX_TRACE_SIZE,
		     "profile buffer not large enough"))
		return;

	entry = perf_trace_buf_prepare(size, call->event.type, regs, &rctx);
	if (!entry)
		return;

	entry->ip = (unsigned long)kp->addr;
	memset(&entry[1], 0, dsize);
	store_trace_args(sizeof(*entry), tp, regs, (u8 *)&entry[1], dsize);

	head = this_cpu_ptr(call->perf_events);
	perf_trace_buf_submit(entry, size, rctx,
					entry->ip, 1, regs, head, NULL);
}

/* Kretprobe profile handler */
static __kprobes void kretprobe_perf_func(struct kretprobe_instance *ri,
					    struct pt_regs *regs)
{
	struct trace_probe *tp = container_of(ri->rp, struct trace_probe, rp);
	struct ftrace_event_call *call = &tp->call;
	struct kretprobe_trace_entry_head *entry;
	struct hlist_head *head;
	int size, __size, dsize;
	int rctx;

	dsize = __get_data_size(tp, regs);
	__size = sizeof(*entry) + tp->size + dsize;
	size = ALIGN(__size + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);
	if (WARN_ONCE(size > PERF_MAX_TRACE_SIZE,
		     "profile buffer not large enough"))
		return;

	entry = perf_trace_buf_prepare(size, call->event.type, regs, &rctx);
	if (!entry)
		return;

	entry->func = (unsigned long)tp->rp.kp.addr;
	entry->ret_ip = (unsigned long)ri->ret_addr;
	store_trace_args(sizeof(*entry), tp, regs, (u8 *)&entry[1], dsize);

	head = this_cpu_ptr(call->perf_events);
	perf_trace_buf_submit(entry, size, rctx,
					entry->ret_ip, 1, regs, head, NULL);
}
#endif	/* CONFIG_PERF_EVENTS */

static __kprobes
int kprobe_register(struct ftrace_event_call *event,
		    enum trace_reg type, void *data)
{
	struct trace_probe *tp = (struct trace_probe *)event->data;

	switch (type) {
	case TRACE_REG_REGISTER:
		return enable_trace_probe(tp, TP_FLAG_TRACE);
	case TRACE_REG_UNREGISTER:
		disable_trace_probe(tp, TP_FLAG_TRACE);
		return 0;

#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
		return enable_trace_probe(tp, TP_FLAG_PROFILE);
	case TRACE_REG_PERF_UNREGISTER:
		disable_trace_probe(tp, TP_FLAG_PROFILE);
		return 0;
	case TRACE_REG_PERF_OPEN:
	case TRACE_REG_PERF_CLOSE:
	case TRACE_REG_PERF_ADD:
	case TRACE_REG_PERF_DEL:
		return 0;
#endif
	}
	return 0;
}

static __kprobes
int kprobe_dispatcher(struct kprobe *kp, struct pt_regs *regs)
{
	struct trace_probe *tp = container_of(kp, struct trace_probe, rp.kp);

	tp->nhit++;

	if (tp->flags & TP_FLAG_TRACE)
		kprobe_trace_func(kp, regs);
#ifdef CONFIG_PERF_EVENTS
	if (tp->flags & TP_FLAG_PROFILE)
		kprobe_perf_func(kp, regs);
#endif
	return 0;	/* We don't tweek kernel, so just return 0 */
}

static __kprobes
int kretprobe_dispatcher(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct trace_probe *tp = container_of(ri->rp, struct trace_probe, rp);

	tp->nhit++;

	if (tp->flags & TP_FLAG_TRACE)
		kretprobe_trace_func(ri, regs);
#ifdef CONFIG_PERF_EVENTS
	if (tp->flags & TP_FLAG_PROFILE)
		kretprobe_perf_func(ri, regs);
#endif
	return 0;	/* We don't tweek kernel, so just return 0 */
}

static struct trace_event_functions kretprobe_funcs = {
	.trace		= print_kretprobe_event
};

static struct trace_event_functions kprobe_funcs = {
	.trace		= print_kprobe_event
};

static int register_probe_event(struct trace_probe *tp)
{
	struct ftrace_event_call *call = &tp->call;
	int ret;

	/* Initialize ftrace_event_call */
	INIT_LIST_HEAD(&call->class->fields);
	if (trace_probe_is_return(tp)) {
		call->event.funcs = &kretprobe_funcs;
		call->class->define_fields = kretprobe_event_define_fields;
	} else {
		call->event.funcs = &kprobe_funcs;
		call->class->define_fields = kprobe_event_define_fields;
	}
	if (set_print_fmt(tp) < 0)
		return -ENOMEM;
	ret = register_ftrace_event(&call->event);
	if (!ret) {
		kfree(call->print_fmt);
		return -ENODEV;
	}
	call->flags = 0;
	call->class->reg = kprobe_register;
	call->data = tp;
	ret = trace_add_event_call(call);
	if (ret) {
		pr_info("Failed to register kprobe event: %s\n", call->name);
		kfree(call->print_fmt);
		unregister_ftrace_event(&call->event);
	}
	return ret;
}

static void unregister_probe_event(struct trace_probe *tp)
{
	/* tp->event is unregistered in trace_remove_event_call() */
	trace_remove_event_call(&tp->call);
	kfree(tp->call.print_fmt);
}

/* Make a debugfs interface for controlling probe points */
static __init int init_kprobe_trace(void)
{
	struct dentry *d_tracer;
	struct dentry *entry;

	if (register_module_notifier(&trace_probe_module_nb))
		return -EINVAL;

	d_tracer = tracing_init_dentry();
	if (!d_tracer)
		return 0;

	entry = debugfs_create_file("kprobe_events", 0644, d_tracer,
				    NULL, &kprobe_events_ops);

	/* Event list interface */
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'kprobe_events' entry\n");

	/* Profile interface */
	entry = debugfs_create_file("kprobe_profile", 0444, d_tracer,
				    NULL, &kprobe_profile_ops);

	if (!entry)
		pr_warning("Could not create debugfs "
			   "'kprobe_profile' entry\n");
	return 0;
}
fs_initcall(init_kprobe_trace);


#ifdef CONFIG_FTRACE_STARTUP_TEST

/*
 * The "__used" keeps gcc from removing the function symbol
 * from the kallsyms table.
 */
static __used int kprobe_trace_selftest_target(int a1, int a2, int a3,
					       int a4, int a5, int a6)
{
	return a1 + a2 + a3 + a4 + a5 + a6;
}

static __init int kprobe_trace_self_tests_init(void)
{
	int ret, warn = 0;
	int (*target)(int, int, int, int, int, int);
	struct trace_probe *tp;

	target = kprobe_trace_selftest_target;

	pr_info("Testing kprobe tracing: ");

	ret = traceprobe_command("p:testprobe kprobe_trace_selftest_target "
				  "$stack $stack0 +0($stack)",
				  create_trace_probe);
	if (WARN_ON_ONCE(ret)) {
		pr_warning("error on probing function entry.\n");
		warn++;
	} else {
		/* Enable trace point */
		tp = find_trace_probe("testprobe", KPROBE_EVENT_SYSTEM);
		if (WARN_ON_ONCE(tp == NULL)) {
			pr_warning("error on getting new probe.\n");
			warn++;
		} else
			enable_trace_probe(tp, TP_FLAG_TRACE);
	}

	ret = traceprobe_command("r:testprobe2 kprobe_trace_selftest_target "
				  "$retval", create_trace_probe);
	if (WARN_ON_ONCE(ret)) {
		pr_warning("error on probing function return.\n");
		warn++;
	} else {
		/* Enable trace point */
		tp = find_trace_probe("testprobe2", KPROBE_EVENT_SYSTEM);
		if (WARN_ON_ONCE(tp == NULL)) {
			pr_warning("error on getting new probe.\n");
			warn++;
		} else
			enable_trace_probe(tp, TP_FLAG_TRACE);
	}

	if (warn)
		goto end;

	ret = target(1, 2, 3, 4, 5, 6);

	/* Disable trace points before removing it */
	tp = find_trace_probe("testprobe", KPROBE_EVENT_SYSTEM);
	if (WARN_ON_ONCE(tp == NULL)) {
		pr_warning("error on getting test probe.\n");
		warn++;
	} else
		disable_trace_probe(tp, TP_FLAG_TRACE);

	tp = find_trace_probe("testprobe2", KPROBE_EVENT_SYSTEM);
	if (WARN_ON_ONCE(tp == NULL)) {
		pr_warning("error on getting 2nd test probe.\n");
		warn++;
	} else
		disable_trace_probe(tp, TP_FLAG_TRACE);

	ret = traceprobe_command("-:testprobe", create_trace_probe);
	if (WARN_ON_ONCE(ret)) {
		pr_warning("error on deleting a probe.\n");
		warn++;
	}

	ret = traceprobe_command("-:testprobe2", create_trace_probe);
	if (WARN_ON_ONCE(ret)) {
		pr_warning("error on deleting a probe.\n");
		warn++;
	}

end:
	release_all_trace_probes();
	if (warn)
		pr_cont("NG: Some tests are failed. Please check them.\n");
	else
		pr_cont("OK\n");
	return 0;
}

late_initcall(kprobe_trace_self_tests_init);

#endif
