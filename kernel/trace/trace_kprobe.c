/*
 * kprobe based kernel tracer
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
#include <linux/kprobes.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/debugfs.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/ptrace.h>

#include "trace.h"
#include "trace_output.h"

#define MAX_TRACE_ARGS 128
#define MAX_ARGSTR_LEN 63

/* currently, trace_kprobe only supports X86. */

struct fetch_func {
	unsigned long (*func)(struct pt_regs *, void *);
	void *data;
};

static __kprobes unsigned long call_fetch(struct fetch_func *f,
					  struct pt_regs *regs)
{
	return f->func(regs, f->data);
}

/* fetch handlers */
static __kprobes unsigned long fetch_register(struct pt_regs *regs,
					      void *offset)
{
	return regs_get_register(regs, (unsigned int)((unsigned long)offset));
}

static __kprobes unsigned long fetch_stack(struct pt_regs *regs,
					   void *num)
{
	return regs_get_kernel_stack_nth(regs,
					 (unsigned int)((unsigned long)num));
}

static __kprobes unsigned long fetch_memory(struct pt_regs *regs, void *addr)
{
	unsigned long retval;

	if (probe_kernel_address(addr, retval))
		return 0;
	return retval;
}

static __kprobes unsigned long fetch_argument(struct pt_regs *regs, void *num)
{
	return regs_get_argument_nth(regs, (unsigned int)((unsigned long)num));
}

static __kprobes unsigned long fetch_retvalue(struct pt_regs *regs,
					      void *dummy)
{
	return regs_return_value(regs);
}

static __kprobes unsigned long fetch_ip(struct pt_regs *regs, void *dummy)
{
	return instruction_pointer(regs);
}

static __kprobes unsigned long fetch_stack_address(struct pt_regs *regs,
						   void *dummy)
{
	return kernel_stack_pointer(regs);
}

/* Memory fetching by symbol */
struct symbol_cache {
	char *symbol;
	long offset;
	unsigned long addr;
};

static unsigned long update_symbol_cache(struct symbol_cache *sc)
{
	sc->addr = (unsigned long)kallsyms_lookup_name(sc->symbol);
	if (sc->addr)
		sc->addr += sc->offset;
	return sc->addr;
}

static void free_symbol_cache(struct symbol_cache *sc)
{
	kfree(sc->symbol);
	kfree(sc);
}

static struct symbol_cache *alloc_symbol_cache(const char *sym, long offset)
{
	struct symbol_cache *sc;

	if (!sym || strlen(sym) == 0)
		return NULL;
	sc = kzalloc(sizeof(struct symbol_cache), GFP_KERNEL);
	if (!sc)
		return NULL;

	sc->symbol = kstrdup(sym, GFP_KERNEL);
	if (!sc->symbol) {
		kfree(sc);
		return NULL;
	}
	sc->offset = offset;

	update_symbol_cache(sc);
	return sc;
}

static __kprobes unsigned long fetch_symbol(struct pt_regs *regs, void *data)
{
	struct symbol_cache *sc = data;

	if (sc->addr)
		return fetch_memory(regs, (void *)sc->addr);
	else
		return 0;
}

/* Special indirect memory access interface */
struct indirect_fetch_data {
	struct fetch_func orig;
	long offset;
};

static __kprobes unsigned long fetch_indirect(struct pt_regs *regs, void *data)
{
	struct indirect_fetch_data *ind = data;
	unsigned long addr;

	addr = call_fetch(&ind->orig, regs);
	if (addr) {
		addr += ind->offset;
		return fetch_memory(regs, (void *)addr);
	} else
		return 0;
}

static __kprobes void free_indirect_fetch_data(struct indirect_fetch_data *data)
{
	if (data->orig.func == fetch_indirect)
		free_indirect_fetch_data(data->orig.data);
	else if (data->orig.func == fetch_symbol)
		free_symbol_cache(data->orig.data);
	kfree(data);
}

/**
 * kprobe_trace_core
 */

struct trace_probe {
	struct list_head	list;
	union {
		struct kprobe		kp;
		struct kretprobe	rp;
	};
	const char		*symbol;	/* symbol name */
	struct ftrace_event_call	call;
	unsigned int		nr_args;
	struct fetch_func	args[];
};

#define SIZEOF_TRACE_PROBE(n)			\
	(offsetof(struct trace_probe, args) +	\
	(sizeof(struct fetch_func) * (n)))

static int kprobe_trace_func(struct kprobe *kp, struct pt_regs *regs);
static int kretprobe_trace_func(struct kretprobe_instance *ri,
				struct pt_regs *regs);

static __kprobes int probe_is_return(struct trace_probe *tp)
{
	return (tp->rp.handler == kretprobe_trace_func);
}

static __kprobes const char *probe_symbol(struct trace_probe *tp)
{
	return tp->symbol ? tp->symbol : "unknown";
}

static __kprobes long probe_offset(struct trace_probe *tp)
{
	return (probe_is_return(tp)) ? tp->rp.kp.offset : tp->kp.offset;
}

static __kprobes void *probe_address(struct trace_probe *tp)
{
	return (probe_is_return(tp)) ? tp->rp.kp.addr : tp->kp.addr;
}

static int trace_arg_string(char *buf, size_t n, struct fetch_func *ff)
{
	int ret = -EINVAL;

	if (ff->func == fetch_argument)
		ret = snprintf(buf, n, "a%lu", (unsigned long)ff->data);
	else if (ff->func == fetch_register) {
		const char *name;
		name = regs_query_register_name((unsigned int)((long)ff->data));
		ret = snprintf(buf, n, "%%%s", name);
	} else if (ff->func == fetch_stack)
		ret = snprintf(buf, n, "s%lu", (unsigned long)ff->data);
	else if (ff->func == fetch_memory)
		ret = snprintf(buf, n, "@0x%p", ff->data);
	else if (ff->func == fetch_symbol) {
		struct symbol_cache *sc = ff->data;
		ret = snprintf(buf, n, "@%s%+ld", sc->symbol, sc->offset);
	} else if (ff->func == fetch_retvalue)
		ret = snprintf(buf, n, "rv");
	else if (ff->func == fetch_ip)
		ret = snprintf(buf, n, "ra");
	else if (ff->func == fetch_stack_address)
		ret = snprintf(buf, n, "sa");
	else if (ff->func == fetch_indirect) {
		struct indirect_fetch_data *id = ff->data;
		size_t l = 0;
		ret = snprintf(buf, n, "%+ld(", id->offset);
		if (ret >= n)
			goto end;
		l += ret;
		ret = trace_arg_string(buf + l, n - l, &id->orig);
		if (ret < 0)
			goto end;
		l += ret;
		ret = snprintf(buf + l, n - l, ")");
		ret += l;
	}
end:
	if (ret >= n)
		return -ENOSPC;
	return ret;
}

static int register_probe_event(struct trace_probe *tp);
static void unregister_probe_event(struct trace_probe *tp);

static DEFINE_MUTEX(probe_lock);
static LIST_HEAD(probe_list);

static struct trace_probe *alloc_trace_probe(const char *symbol,
					     const char *event, int nargs)
{
	struct trace_probe *tp;

	tp = kzalloc(SIZEOF_TRACE_PROBE(nargs), GFP_KERNEL);
	if (!tp)
		return ERR_PTR(-ENOMEM);

	if (symbol) {
		tp->symbol = kstrdup(symbol, GFP_KERNEL);
		if (!tp->symbol)
			goto error;
	}
	if (event) {
		tp->call.name = kstrdup(event, GFP_KERNEL);
		if (!tp->call.name)
			goto error;
	}

	INIT_LIST_HEAD(&tp->list);
	return tp;
error:
	kfree(tp->symbol);
	kfree(tp);
	return ERR_PTR(-ENOMEM);
}

static void free_trace_probe(struct trace_probe *tp)
{
	int i;

	for (i = 0; i < tp->nr_args; i++)
		if (tp->args[i].func == fetch_symbol)
			free_symbol_cache(tp->args[i].data);
		else if (tp->args[i].func == fetch_indirect)
			free_indirect_fetch_data(tp->args[i].data);

	kfree(tp->call.name);
	kfree(tp->symbol);
	kfree(tp);
}

static struct trace_probe *find_probe_event(const char *event)
{
	struct trace_probe *tp;

	list_for_each_entry(tp, &probe_list, list)
		if (tp->call.name && !strcmp(tp->call.name, event))
			return tp;
	return NULL;
}

static void __unregister_trace_probe(struct trace_probe *tp)
{
	if (probe_is_return(tp))
		unregister_kretprobe(&tp->rp);
	else
		unregister_kprobe(&tp->kp);
}

/* Unregister a trace_probe and probe_event: call with locking probe_lock */
static void unregister_trace_probe(struct trace_probe *tp)
{
	if (tp->call.name)
		unregister_probe_event(tp);
	__unregister_trace_probe(tp);
	list_del(&tp->list);
}

/* Register a trace_probe and probe_event */
static int register_trace_probe(struct trace_probe *tp)
{
	struct trace_probe *old_tp;
	int ret;

	mutex_lock(&probe_lock);

	if (probe_is_return(tp))
		ret = register_kretprobe(&tp->rp);
	else
		ret = register_kprobe(&tp->kp);

	if (ret) {
		pr_warning("Could not insert probe(%d)\n", ret);
		if (ret == -EILSEQ) {
			pr_warning("Probing address(0x%p) is not an "
				   "instruction boundary.\n",
				   probe_address(tp));
			ret = -EINVAL;
		}
		goto end;
	}
	/* register as an event */
	if (tp->call.name) {
		old_tp = find_probe_event(tp->call.name);
		if (old_tp) {
			/* delete old event */
			unregister_trace_probe(old_tp);
			free_trace_probe(old_tp);
		}
		ret = register_probe_event(tp);
		if (ret) {
			pr_warning("Faild to register probe event(%d)\n", ret);
			__unregister_trace_probe(tp);
		}
	}
	list_add_tail(&tp->list, &probe_list);
end:
	mutex_unlock(&probe_lock);
	return ret;
}

/* Split symbol and offset. */
static int split_symbol_offset(char *symbol, long *offset)
{
	char *tmp;
	int ret;

	if (!offset)
		return -EINVAL;

	tmp = strchr(symbol, '+');
	if (!tmp)
		tmp = strchr(symbol, '-');

	if (tmp) {
		/* skip sign because strict_strtol doesn't accept '+' */
		ret = strict_strtol(tmp + 1, 0, offset);
		if (ret)
			return ret;
		if (*tmp == '-')
			*offset = -(*offset);
		*tmp = '\0';
	} else
		*offset = 0;
	return 0;
}

#define PARAM_MAX_ARGS 16
#define PARAM_MAX_STACK (THREAD_SIZE / sizeof(unsigned long))

static int parse_trace_arg(char *arg, struct fetch_func *ff, int is_return)
{
	int ret = 0;
	unsigned long param;
	long offset;
	char *tmp;

	switch (arg[0]) {
	case 'a':	/* argument */
		ret = strict_strtoul(arg + 1, 10, &param);
		if (ret || param > PARAM_MAX_ARGS)
			ret = -EINVAL;
		else {
			ff->func = fetch_argument;
			ff->data = (void *)param;
		}
		break;
	case 'r':	/* retval or retaddr */
		if (is_return && arg[1] == 'v') {
			ff->func = fetch_retvalue;
			ff->data = NULL;
		} else if (is_return && arg[1] == 'a') {
			ff->func = fetch_ip;
			ff->data = NULL;
		} else
			ret = -EINVAL;
		break;
	case '%':	/* named register */
		ret = regs_query_register_offset(arg + 1);
		if (ret >= 0) {
			ff->func = fetch_register;
			ff->data = (void *)(unsigned long)ret;
			ret = 0;
		}
		break;
	case 's':	/* stack */
		if (arg[1] == 'a') {
			ff->func = fetch_stack_address;
			ff->data = NULL;
		} else {
			ret = strict_strtoul(arg + 1, 10, &param);
			if (ret || param > PARAM_MAX_STACK)
				ret = -EINVAL;
			else {
				ff->func = fetch_stack;
				ff->data = (void *)param;
			}
		}
		break;
	case '@':	/* memory or symbol */
		if (isdigit(arg[1])) {
			ret = strict_strtoul(arg + 1, 0, &param);
			if (ret)
				break;
			ff->func = fetch_memory;
			ff->data = (void *)param;
		} else {
			ret = split_symbol_offset(arg + 1, &offset);
			if (ret)
				break;
			ff->data = alloc_symbol_cache(arg + 1,
							      offset);
			if (ff->data)
				ff->func = fetch_symbol;
			else
				ret = -EINVAL;
		}
		break;
	case '+':	/* indirect memory */
	case '-':
		tmp = strchr(arg, '(');
		if (!tmp) {
			ret = -EINVAL;
			break;
		}
		*tmp = '\0';
		ret = strict_strtol(arg + 1, 0, &offset);
		if (ret)
			break;
		if (arg[0] == '-')
			offset = -offset;
		arg = tmp + 1;
		tmp = strrchr(arg, ')');
		if (tmp) {
			struct indirect_fetch_data *id;
			*tmp = '\0';
			id = kzalloc(sizeof(struct indirect_fetch_data),
				     GFP_KERNEL);
			if (!id)
				return -ENOMEM;
			id->offset = offset;
			ret = parse_trace_arg(arg, &id->orig, is_return);
			if (ret)
				kfree(id);
			else {
				ff->func = fetch_indirect;
				ff->data = (void *)id;
			}
		} else
			ret = -EINVAL;
		break;
	default:
		/* TODO: support custom handler */
		ret = -EINVAL;
	}
	return ret;
}

static int create_trace_probe(int argc, char **argv)
{
	/*
	 * Argument syntax:
	 *  - Add kprobe: p[:EVENT] SYMBOL[+OFFS|-OFFS]|ADDRESS [FETCHARGS]
	 *  - Add kretprobe: r[:EVENT] SYMBOL[+0] [FETCHARGS]
	 * Fetch args:
	 *  aN	: fetch Nth of function argument. (N:0-)
	 *  rv	: fetch return value
	 *  ra	: fetch return address
	 *  sa	: fetch stack address
	 *  sN	: fetch Nth of stack (N:0-)
	 *  @ADDR	: fetch memory at ADDR (ADDR should be in kernel)
	 *  @SYM[+|-offs] : fetch memory at SYM +|- offs (SYM is a data symbol)
	 *  %REG	: fetch register REG
	 * Indirect memory fetch:
	 *  +|-offs(ARG) : fetch memory at ARG +|- offs address.
	 */
	struct trace_probe *tp;
	struct kprobe *kp;
	int i, ret = 0;
	int is_return = 0;
	char *symbol = NULL, *event = NULL;
	long offset = 0;
	void *addr = NULL;

	if (argc < 2)
		return -EINVAL;

	if (argv[0][0] == 'p')
		is_return = 0;
	else if (argv[0][0] == 'r')
		is_return = 1;
	else
		return -EINVAL;

	if (argv[0][1] == ':') {
		event = &argv[0][2];
		if (strlen(event) == 0) {
			pr_info("Event name is not specifiled\n");
			return -EINVAL;
		}
	}

	if (isdigit(argv[1][0])) {
		if (is_return)
			return -EINVAL;
		/* an address specified */
		ret = strict_strtoul(&argv[0][2], 0, (unsigned long *)&addr);
		if (ret)
			return ret;
	} else {
		/* a symbol specified */
		symbol = argv[1];
		/* TODO: support .init module functions */
		ret = split_symbol_offset(symbol, &offset);
		if (ret)
			return ret;
		if (offset && is_return)
			return -EINVAL;
	}
	argc -= 2; argv += 2;

	/* setup a probe */
	tp = alloc_trace_probe(symbol, event, argc);
	if (IS_ERR(tp))
		return PTR_ERR(tp);

	if (is_return) {
		kp = &tp->rp.kp;
		tp->rp.handler = kretprobe_trace_func;
	} else {
		kp = &tp->kp;
		tp->kp.pre_handler = kprobe_trace_func;
	}

	if (tp->symbol) {
		kp->symbol_name = tp->symbol;
		kp->offset = offset;
	} else
		kp->addr = addr;

	/* parse arguments */
	ret = 0;
	for (i = 0; i < argc && i < MAX_TRACE_ARGS; i++) {
		if (strlen(argv[i]) > MAX_ARGSTR_LEN) {
			pr_info("Argument%d(%s) is too long.\n", i, argv[i]);
			ret = -ENOSPC;
			goto error;
		}
		ret = parse_trace_arg(argv[i], &tp->args[i], is_return);
		if (ret)
			goto error;
	}
	tp->nr_args = i;

	ret = register_trace_probe(tp);
	if (ret)
		goto error;
	return 0;

error:
	free_trace_probe(tp);
	return ret;
}

static void cleanup_all_probes(void)
{
	struct trace_probe *tp;

	mutex_lock(&probe_lock);
	/* TODO: Use batch unregistration */
	while (!list_empty(&probe_list)) {
		tp = list_entry(probe_list.next, struct trace_probe, list);
		unregister_trace_probe(tp);
		free_trace_probe(tp);
	}
	mutex_unlock(&probe_lock);
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
	int i, ret;
	char buf[MAX_ARGSTR_LEN + 1];

	seq_printf(m, "%c", probe_is_return(tp) ? 'r' : 'p');
	if (tp->call.name)
		seq_printf(m, ":%s", tp->call.name);

	if (tp->symbol)
		seq_printf(m, " %s%+ld", probe_symbol(tp), probe_offset(tp));
	else
		seq_printf(m, " 0x%p", probe_address(tp));

	for (i = 0; i < tp->nr_args; i++) {
		ret = trace_arg_string(buf, MAX_ARGSTR_LEN, &tp->args[i]);
		if (ret < 0) {
			pr_warning("Argument%d decoding error(%d).\n", i, ret);
			return ret;
		}
		seq_printf(m, " %s", buf);
	}
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
	if ((file->f_mode & FMODE_WRITE) &&
	    (file->f_flags & O_TRUNC))
		cleanup_all_probes();

	return seq_open(file, &probes_seq_op);
}

static int command_trace_probe(const char *buf)
{
	char **argv;
	int argc = 0, ret = 0;

	argv = argv_split(GFP_KERNEL, buf, &argc);
	if (!argv)
		return -ENOMEM;

	if (argc)
		ret = create_trace_probe(argc, argv);

	argv_free(argv);
	return ret;
}

#define WRITE_BUFSIZE 128

static ssize_t probes_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *ppos)
{
	char *kbuf, *tmp;
	int ret;
	size_t done;
	size_t size;

	kbuf = kmalloc(WRITE_BUFSIZE, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	ret = done = 0;
	while (done < count) {
		size = count - done;
		if (size >= WRITE_BUFSIZE)
			size = WRITE_BUFSIZE - 1;
		if (copy_from_user(kbuf, buffer + done, size)) {
			ret = -EFAULT;
			goto out;
		}
		kbuf[size] = '\0';
		tmp = strchr(kbuf, '\n');
		if (tmp) {
			*tmp = '\0';
			size = tmp - kbuf + 1;
		} else if (done + size < count) {
			pr_warning("Line length is too long: "
				   "Should be less than %d.", WRITE_BUFSIZE);
			ret = -EINVAL;
			goto out;
		}
		done += size;
		/* Remove comments */
		tmp = strchr(kbuf, '#');
		if (tmp)
			*tmp = '\0';

		ret = command_trace_probe(kbuf);
		if (ret)
			goto out;
	}
	ret = done;
out:
	kfree(kbuf);
	return ret;
}

static const struct file_operations kprobe_events_ops = {
	.owner          = THIS_MODULE,
	.open           = probes_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release,
	.write		= probes_write,
};

/* Kprobe handler */
static __kprobes int kprobe_trace_func(struct kprobe *kp, struct pt_regs *regs)
{
	struct trace_probe *tp = container_of(kp, struct trace_probe, kp);
	struct kprobe_trace_entry *entry;
	struct ring_buffer_event *event;
	int size, i, pc;
	unsigned long irq_flags;
	struct ftrace_event_call *call = &event_kprobe;

	if (&tp->call.name)
		call = &tp->call;

	local_save_flags(irq_flags);
	pc = preempt_count();

	size = SIZEOF_KPROBE_TRACE_ENTRY(tp->nr_args);

	event = trace_current_buffer_lock_reserve(TRACE_KPROBE, size,
						  irq_flags, pc);
	if (!event)
		return 0;

	entry = ring_buffer_event_data(event);
	entry->nargs = tp->nr_args;
	entry->ip = (unsigned long)kp->addr;
	for (i = 0; i < tp->nr_args; i++)
		entry->args[i] = call_fetch(&tp->args[i], regs);

	if (!filter_current_check_discard(call, entry, event))
		trace_nowake_buffer_unlock_commit(event, irq_flags, pc);
	return 0;
}

/* Kretprobe handler */
static __kprobes int kretprobe_trace_func(struct kretprobe_instance *ri,
					  struct pt_regs *regs)
{
	struct trace_probe *tp = container_of(ri->rp, struct trace_probe, rp);
	struct kretprobe_trace_entry *entry;
	struct ring_buffer_event *event;
	int size, i, pc;
	unsigned long irq_flags;
	struct ftrace_event_call *call = &event_kretprobe;

	if (&tp->call.name)
		call = &tp->call;

	local_save_flags(irq_flags);
	pc = preempt_count();

	size = SIZEOF_KRETPROBE_TRACE_ENTRY(tp->nr_args);

	event = trace_current_buffer_lock_reserve(TRACE_KRETPROBE, size,
						  irq_flags, pc);
	if (!event)
		return 0;

	entry = ring_buffer_event_data(event);
	entry->nargs = tp->nr_args;
	entry->func = (unsigned long)probe_address(tp);
	entry->ret_ip = (unsigned long)ri->ret_addr;
	for (i = 0; i < tp->nr_args; i++)
		entry->args[i] = call_fetch(&tp->args[i], regs);

	if (!filter_current_check_discard(call, entry, event))
		trace_nowake_buffer_unlock_commit(event, irq_flags, pc);

	return 0;
}

/* Event entry printers */
enum print_line_t
print_kprobe_event(struct trace_iterator *iter, int flags)
{
	struct kprobe_trace_entry *field;
	struct trace_seq *s = &iter->seq;
	int i;

	trace_assign_type(field, iter->ent);

	if (!seq_print_ip_sym(s, field->ip, flags | TRACE_ITER_SYM_OFFSET))
		goto partial;

	if (!trace_seq_puts(s, ":"))
		goto partial;

	for (i = 0; i < field->nargs; i++)
		if (!trace_seq_printf(s, " 0x%lx", field->args[i]))
			goto partial;

	if (!trace_seq_puts(s, "\n"))
		goto partial;

	return TRACE_TYPE_HANDLED;
partial:
	return TRACE_TYPE_PARTIAL_LINE;
}

enum print_line_t
print_kretprobe_event(struct trace_iterator *iter, int flags)
{
	struct kretprobe_trace_entry *field;
	struct trace_seq *s = &iter->seq;
	int i;

	trace_assign_type(field, iter->ent);

	if (!seq_print_ip_sym(s, field->ret_ip, flags | TRACE_ITER_SYM_OFFSET))
		goto partial;

	if (!trace_seq_puts(s, " <- "))
		goto partial;

	if (!seq_print_ip_sym(s, field->func, flags & ~TRACE_ITER_SYM_OFFSET))
		goto partial;

	if (!trace_seq_puts(s, ":"))
		goto partial;

	for (i = 0; i < field->nargs; i++)
		if (!trace_seq_printf(s, " 0x%lx", field->args[i]))
			goto partial;

	if (!trace_seq_puts(s, "\n"))
		goto partial;

	return TRACE_TYPE_HANDLED;
partial:
	return TRACE_TYPE_PARTIAL_LINE;
}

static struct trace_event kprobe_trace_event = {
	.type	 	= TRACE_KPROBE,
	.trace		= print_kprobe_event,
};

static struct trace_event kretprobe_trace_event = {
	.type	 	= TRACE_KRETPROBE,
	.trace		= print_kretprobe_event,
};

static int probe_event_enable(struct ftrace_event_call *call)
{
	struct trace_probe *tp = (struct trace_probe *)call->data;

	if (probe_is_return(tp))
		return enable_kretprobe(&tp->rp);
	else
		return enable_kprobe(&tp->kp);
}

static void probe_event_disable(struct ftrace_event_call *call)
{
	struct trace_probe *tp = (struct trace_probe *)call->data;

	if (probe_is_return(tp))
		disable_kretprobe(&tp->rp);
	else
		disable_kprobe(&tp->kp);
}

static int probe_event_raw_init(struct ftrace_event_call *event_call)
{
	INIT_LIST_HEAD(&event_call->fields);
	init_preds(event_call);
	return 0;
}

#undef DEFINE_FIELD
#define DEFINE_FIELD(type, item, name, is_signed)			\
	do {								\
		ret = trace_define_field(event_call, #type, name,	\
					 offsetof(typeof(field), item),	\
					 sizeof(field.item), is_signed, \
					 FILTER_OTHER);			\
		if (ret)						\
			return ret;					\
	} while (0)

static int kprobe_event_define_fields(struct ftrace_event_call *event_call)
{
	int ret, i;
	struct kprobe_trace_entry field;
	char buf[MAX_ARGSTR_LEN + 1];
	struct trace_probe *tp = (struct trace_probe *)event_call->data;

	ret = trace_define_common_fields(event_call);
	if (!ret)
		return ret;

	DEFINE_FIELD(unsigned long, ip, "ip", 0);
	DEFINE_FIELD(int, nargs, "nargs", 1);
	for (i = 0; i < tp->nr_args; i++) {
		/* Set argN as a field */
		sprintf(buf, "arg%d", i);
		DEFINE_FIELD(unsigned long, args[i], buf, 0);
		/* Set argument string as an alias field */
		ret = trace_arg_string(buf, MAX_ARGSTR_LEN, &tp->args[i]);
		if (ret < 0)
			return ret;
		DEFINE_FIELD(unsigned long, args[i], buf, 0);
	}
	return 0;
}

static int kretprobe_event_define_fields(struct ftrace_event_call *event_call)
{
	int ret, i;
	struct kretprobe_trace_entry field;
	char buf[MAX_ARGSTR_LEN + 1];
	struct trace_probe *tp = (struct trace_probe *)event_call->data;

	ret = trace_define_common_fields(event_call);
	if (!ret)
		return ret;

	DEFINE_FIELD(unsigned long, func, "func", 0);
	DEFINE_FIELD(unsigned long, ret_ip, "ret_ip", 0);
	DEFINE_FIELD(int, nargs, "nargs", 1);
	for (i = 0; i < tp->nr_args; i++) {
		/* Set argN as a field */
		sprintf(buf, "arg%d", i);
		DEFINE_FIELD(unsigned long, args[i], buf, 0);
		/* Set argument string as an alias field */
		ret = trace_arg_string(buf, MAX_ARGSTR_LEN, &tp->args[i]);
		if (ret < 0)
			return ret;
		DEFINE_FIELD(unsigned long, args[i], buf, 0);
	}
	return 0;
}

static int __probe_event_show_format(struct trace_seq *s,
				     struct trace_probe *tp, const char *fmt,
				     const char *arg)
{
	int i, ret;
	char buf[MAX_ARGSTR_LEN + 1];

	/* Show aliases */
	for (i = 0; i < tp->nr_args; i++) {
		ret = trace_arg_string(buf, MAX_ARGSTR_LEN, &tp->args[i]);
		if (ret < 0)
			return ret;
		if (!trace_seq_printf(s, "\talias: %s;\toriginal: arg%d;\n",
				      buf, i))
			return 0;
	}
	/* Show format */
	if (!trace_seq_printf(s, "\nprint fmt: \"%s", fmt))
		return 0;

	for (i = 0; i < tp->nr_args; i++)
		if (!trace_seq_puts(s, " 0x%lx"))
			return 0;

	if (!trace_seq_printf(s, "\", %s", arg))
		return 0;

	for (i = 0; i < tp->nr_args; i++)
		if (!trace_seq_printf(s, ", arg%d", i))
			return 0;

	return trace_seq_puts(s, "\n");
}

#undef SHOW_FIELD
#define SHOW_FIELD(type, item, name)					\
	do {								\
		ret = trace_seq_printf(s, "\tfield: " #type " %s;\t"	\
				"offset:%u;tsize:%u;\n", name,		\
				(unsigned int)offsetof(typeof(field), item),\
				(unsigned int)sizeof(type));		\
		if (!ret)						\
			return 0;					\
	} while (0)

static int kprobe_event_show_format(struct ftrace_event_call *call,
				    struct trace_seq *s)
{
	struct kprobe_trace_entry field __attribute__((unused));
	int ret, i;
	char buf[8];
	struct trace_probe *tp = (struct trace_probe *)call->data;

	SHOW_FIELD(unsigned long, ip, "ip");
	SHOW_FIELD(int, nargs, "nargs");

	/* Show fields */
	for (i = 0; i < tp->nr_args; i++) {
		sprintf(buf, "arg%d", i);
		SHOW_FIELD(unsigned long, args[i], buf);
	}
	trace_seq_puts(s, "\n");

	return __probe_event_show_format(s, tp, "%lx:", "ip");
}

static int kretprobe_event_show_format(struct ftrace_event_call *call,
				       struct trace_seq *s)
{
	struct kretprobe_trace_entry field __attribute__((unused));
	int ret, i;
	char buf[8];
	struct trace_probe *tp = (struct trace_probe *)call->data;

	SHOW_FIELD(unsigned long, func, "func");
	SHOW_FIELD(unsigned long, ret_ip, "ret_ip");
	SHOW_FIELD(int, nargs, "nargs");

	/* Show fields */
	for (i = 0; i < tp->nr_args; i++) {
		sprintf(buf, "arg%d", i);
		SHOW_FIELD(unsigned long, args[i], buf);
	}
	trace_seq_puts(s, "\n");

	return __probe_event_show_format(s, tp, "%lx <- %lx:",
					  "func, ret_ip");
}

static int register_probe_event(struct trace_probe *tp)
{
	struct ftrace_event_call *call = &tp->call;
	int ret;

	/* Initialize ftrace_event_call */
	call->system = "kprobes";
	if (probe_is_return(tp)) {
		call->event = &kretprobe_trace_event;
		call->id = TRACE_KRETPROBE;
		call->raw_init = probe_event_raw_init;
		call->show_format = kretprobe_event_show_format;
		call->define_fields = kretprobe_event_define_fields;
	} else {
		call->event = &kprobe_trace_event;
		call->id = TRACE_KPROBE;
		call->raw_init = probe_event_raw_init;
		call->show_format = kprobe_event_show_format;
		call->define_fields = kprobe_event_define_fields;
	}
	call->enabled = 1;
	call->regfunc = probe_event_enable;
	call->unregfunc = probe_event_disable;
	call->data = tp;
	ret = trace_add_event_call(call);
	if (ret)
		pr_info("Failed to register kprobe event: %s\n", call->name);
	return ret;
}

static void unregister_probe_event(struct trace_probe *tp)
{
	/*
	 * Prevent to unregister event itself because the event is shared
	 * among other probes.
	 */
	tp->call.event = NULL;
	trace_remove_event_call(&tp->call);
}

/* Make a debugfs interface for controling probe points */
static __init int init_kprobe_trace(void)
{
	struct dentry *d_tracer;
	struct dentry *entry;
	int ret;

	ret = register_ftrace_event(&kprobe_trace_event);
	if (!ret) {
		pr_warning("Could not register kprobe_trace_event type.\n");
		return 0;
	}
	ret = register_ftrace_event(&kretprobe_trace_event);
	if (!ret) {
		pr_warning("Could not register kretprobe_trace_event type.\n");
		return 0;
	}

	d_tracer = tracing_init_dentry();
	if (!d_tracer)
		return 0;

	entry = debugfs_create_file("kprobe_events", 0644, d_tracer,
				    NULL, &kprobe_events_ops);

	if (!entry)
		pr_warning("Could not create debugfs "
			   "'kprobe_events' entry\n");
	return 0;
}
fs_initcall(init_kprobe_trace);


#ifdef CONFIG_FTRACE_STARTUP_TEST

static int kprobe_trace_selftest_target(int a1, int a2, int a3,
					int a4, int a5, int a6)
{
	return a1 + a2 + a3 + a4 + a5 + a6;
}

static __init int kprobe_trace_self_tests_init(void)
{
	int ret;
	int (*target)(int, int, int, int, int, int);

	target = kprobe_trace_selftest_target;

	pr_info("Testing kprobe tracing: ");

	ret = command_trace_probe("p:testprobe kprobe_trace_selftest_target "
				  "a1 a2 a3 a4 a5 a6");
	if (WARN_ON_ONCE(ret))
		pr_warning("error enabling function entry\n");

	ret = command_trace_probe("r:testprobe2 kprobe_trace_selftest_target "
				  "ra rv");
	if (WARN_ON_ONCE(ret))
		pr_warning("error enabling function return\n");

	ret = target(1, 2, 3, 4, 5, 6);

	cleanup_all_probes();

	pr_cont("OK\n");
	return 0;
}

late_initcall(kprobe_trace_self_tests_init);

#endif
