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
#define pr_fmt(fmt)	"trace_kprobe: " fmt

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/rculist.h>
#include <linux/error-injection.h>

#include "trace_probe.h"

#define KPROBE_EVENT_SYSTEM "kprobes"
#define KRETPROBE_MAXACTIVE_MAX 4096

/**
 * Kprobe event core functions
 */
struct trace_kprobe {
	struct list_head	list;
	struct kretprobe	rp;	/* Use rp.kp for kprobe use */
	unsigned long __percpu *nhit;
	const char		*symbol;	/* symbol name */
	struct trace_probe	tp;
};

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

static nokprobe_inline bool trace_kprobe_is_on_module(struct trace_kprobe *tk)
{
	return !!strchr(trace_kprobe_symbol(tk), ':');
}

static nokprobe_inline unsigned long trace_kprobe_nhit(struct trace_kprobe *tk)
{
	unsigned long nhit = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		nhit += *per_cpu_ptr(tk->nhit, cpu);

	return nhit;
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
	unsigned long addr;

	if (tk->symbol) {
		addr = (unsigned long)
			kallsyms_lookup_name(trace_kprobe_symbol(tk));
		addr += tk->rp.kp.offset;
	} else {
		addr = (unsigned long)tk->rp.kp.addr;
	}
	return within_error_injection_list(addr);
}

static int register_kprobe_event(struct trace_kprobe *tk);
static int unregister_kprobe_event(struct trace_kprobe *tk);

static DEFINE_MUTEX(probe_lock);
static LIST_HEAD(probe_list);

static int kprobe_dispatcher(struct kprobe *kp, struct pt_regs *regs);
static int kretprobe_dispatcher(struct kretprobe_instance *ri,
				struct pt_regs *regs);

/* Memory fetching by symbol */
struct symbol_cache {
	char		*symbol;
	long		offset;
	unsigned long	addr;
};

unsigned long update_symbol_cache(struct symbol_cache *sc)
{
	sc->addr = (unsigned long)kallsyms_lookup_name(sc->symbol);

	if (sc->addr)
		sc->addr += sc->offset;

	return sc->addr;
}

void free_symbol_cache(struct symbol_cache *sc)
{
	kfree(sc->symbol);
	kfree(sc);
}

struct symbol_cache *alloc_symbol_cache(const char *sym, long offset)
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

/*
 * Kprobes-specific fetch functions
 */
#define DEFINE_FETCH_stack(type)					\
static void FETCH_FUNC_NAME(stack, type)(struct pt_regs *regs,		\
					  void *offset, void *dest)	\
{									\
	*(type *)dest = (type)regs_get_kernel_stack_nth(regs,		\
				(unsigned int)((unsigned long)offset));	\
}									\
NOKPROBE_SYMBOL(FETCH_FUNC_NAME(stack, type));

DEFINE_BASIC_FETCH_FUNCS(stack)
/* No string on the stack entry */
#define fetch_stack_string	NULL
#define fetch_stack_string_size	NULL

#define DEFINE_FETCH_memory(type)					\
static void FETCH_FUNC_NAME(memory, type)(struct pt_regs *regs,		\
					  void *addr, void *dest)	\
{									\
	type retval;							\
	if (probe_kernel_address(addr, retval))				\
		*(type *)dest = 0;					\
	else								\
		*(type *)dest = retval;					\
}									\
NOKPROBE_SYMBOL(FETCH_FUNC_NAME(memory, type));

DEFINE_BASIC_FETCH_FUNCS(memory)
/*
 * Fetch a null-terminated string. Caller MUST set *(u32 *)dest with max
 * length and relative data location.
 */
static void FETCH_FUNC_NAME(memory, string)(struct pt_regs *regs,
					    void *addr, void *dest)
{
	int maxlen = get_rloc_len(*(u32 *)dest);
	u8 *dst = get_rloc_data(dest);
	long ret;

	if (!maxlen)
		return;

	/*
	 * Try to get string again, since the string can be changed while
	 * probing.
	 */
	ret = strncpy_from_unsafe(dst, addr, maxlen);

	if (ret < 0) {	/* Failed to fetch string */
		dst[0] = '\0';
		*(u32 *)dest = make_data_rloc(0, get_rloc_offs(*(u32 *)dest));
	} else {
		*(u32 *)dest = make_data_rloc(ret, get_rloc_offs(*(u32 *)dest));
	}
}
NOKPROBE_SYMBOL(FETCH_FUNC_NAME(memory, string));

/* Return the length of string -- including null terminal byte */
static void FETCH_FUNC_NAME(memory, string_size)(struct pt_regs *regs,
						 void *addr, void *dest)
{
	mm_segment_t old_fs;
	int ret, len = 0;
	u8 c;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	pagefault_disable();

	do {
		ret = __copy_from_user_inatomic(&c, (u8 *)addr + len, 1);
		len++;
	} while (c && ret == 0 && len < MAX_STRING_SIZE);

	pagefault_enable();
	set_fs(old_fs);

	if (ret < 0)	/* Failed to check the length */
		*(u32 *)dest = 0;
	else
		*(u32 *)dest = len;
}
NOKPROBE_SYMBOL(FETCH_FUNC_NAME(memory, string_size));

#define DEFINE_FETCH_symbol(type)					\
void FETCH_FUNC_NAME(symbol, type)(struct pt_regs *regs, void *data, void *dest)\
{									\
	struct symbol_cache *sc = data;					\
	if (sc->addr)							\
		fetch_memory_##type(regs, (void *)sc->addr, dest);	\
	else								\
		*(type *)dest = 0;					\
}									\
NOKPROBE_SYMBOL(FETCH_FUNC_NAME(symbol, type));

DEFINE_BASIC_FETCH_FUNCS(symbol)
DEFINE_FETCH_symbol(string)
DEFINE_FETCH_symbol(string_size)

/* kprobes don't support file_offset fetch methods */
#define fetch_file_offset_u8		NULL
#define fetch_file_offset_u16		NULL
#define fetch_file_offset_u32		NULL
#define fetch_file_offset_u64		NULL
#define fetch_file_offset_string	NULL
#define fetch_file_offset_string_size	NULL

/* Fetch type information table */
static const struct fetch_type kprobes_fetch_type_table[] = {
	/* Special types */
	[FETCH_TYPE_STRING] = __ASSIGN_FETCH_TYPE("string", string, string,
					sizeof(u32), 1, "__data_loc char[]"),
	[FETCH_TYPE_STRSIZE] = __ASSIGN_FETCH_TYPE("string_size", u32,
					string_size, sizeof(u32), 0, "u32"),
	/* Basic types */
	ASSIGN_FETCH_TYPE(u8,  u8,  0),
	ASSIGN_FETCH_TYPE(u16, u16, 0),
	ASSIGN_FETCH_TYPE(u32, u32, 0),
	ASSIGN_FETCH_TYPE(u64, u64, 0),
	ASSIGN_FETCH_TYPE(s8,  u8,  1),
	ASSIGN_FETCH_TYPE(s16, u16, 1),
	ASSIGN_FETCH_TYPE(s32, u32, 1),
	ASSIGN_FETCH_TYPE(s64, u64, 1),
	ASSIGN_FETCH_TYPE_ALIAS(x8,  u8,  u8,  0),
	ASSIGN_FETCH_TYPE_ALIAS(x16, u16, u16, 0),
	ASSIGN_FETCH_TYPE_ALIAS(x32, u32, u32, 0),
	ASSIGN_FETCH_TYPE_ALIAS(x64, u64, u64, 0),

	ASSIGN_FETCH_TYPE_END
};

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

	if (!event || !is_good_name(event)) {
		ret = -EINVAL;
		goto error;
	}

	tk->tp.call.class = &tk->tp.class;
	tk->tp.call.name = kstrdup(event, GFP_KERNEL);
	if (!tk->tp.call.name)
		goto error;

	if (!group || !is_good_name(group)) {
		ret = -EINVAL;
		goto error;
	}

	tk->tp.class.system = kstrdup(group, GFP_KERNEL);
	if (!tk->tp.class.system)
		goto error;

	INIT_LIST_HEAD(&tk->list);
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

	for (i = 0; i < tk->tp.nr_args; i++)
		traceprobe_free_probe_arg(&tk->tp.args[i]);

	kfree(tk->tp.call.class->system);
	kfree(tk->tp.call.name);
	kfree(tk->symbol);
	free_percpu(tk->nhit);
	kfree(tk);
}

static struct trace_kprobe *find_trace_kprobe(const char *event,
					      const char *group)
{
	struct trace_kprobe *tk;

	list_for_each_entry(tk, &probe_list, list)
		if (strcmp(trace_event_name(&tk->tp.call), event) == 0 &&
		    strcmp(tk->tp.call.class->system, group) == 0)
			return tk;
	return NULL;
}

/*
 * Enable trace_probe
 * if the file is NULL, enable "perf" handler, or enable "trace" handler.
 */
static int
enable_trace_kprobe(struct trace_kprobe *tk, struct trace_event_file *file)
{
	int ret = 0;

	if (file) {
		struct event_file_link *link;

		link = kmalloc(sizeof(*link), GFP_KERNEL);
		if (!link) {
			ret = -ENOMEM;
			goto out;
		}

		link->file = file;
		list_add_tail_rcu(&link->list, &tk->tp.files);

		tk->tp.flags |= TP_FLAG_TRACE;
	} else
		tk->tp.flags |= TP_FLAG_PROFILE;

	if (trace_probe_is_registered(&tk->tp) && !trace_kprobe_has_gone(tk)) {
		if (trace_kprobe_is_return(tk))
			ret = enable_kretprobe(&tk->rp);
		else
			ret = enable_kprobe(&tk->rp.kp);
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
		synchronize_sched();
		kfree(link);	/* Ignored if link == NULL */
	}

	return ret;
}

/* Internal register function - just handle k*probes and flags */
static int __register_trace_kprobe(struct trace_kprobe *tk)
{
	int i, ret;

	if (trace_probe_is_registered(&tk->tp))
		return -EINVAL;

	for (i = 0; i < tk->tp.nr_args; i++)
		traceprobe_update_arg(&tk->tp.args[i]);

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
	else {
		pr_warn("Could not insert probe at %s+%lu: %d\n",
			trace_kprobe_symbol(tk), trace_kprobe_offset(tk), ret);
		if (ret == -ENOENT && trace_kprobe_is_on_module(tk)) {
			pr_warn("This probe might be able to register after target module is loaded. Continue.\n");
			ret = 0;
		} else if (ret == -EILSEQ) {
			pr_warn("Probing address(0x%p) is not an instruction boundary.\n",
				tk->rp.kp.addr);
			ret = -EINVAL;
		}
	}

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

/* Unregister a trace_probe and probe_event: call with locking probe_lock */
static int unregister_trace_kprobe(struct trace_kprobe *tk)
{
	/* Enabled event can not be unregistered */
	if (trace_probe_is_enabled(&tk->tp))
		return -EBUSY;

	/* Will fail if probe is being used by ftrace or perf */
	if (unregister_kprobe_event(tk))
		return -EBUSY;

	__unregister_trace_kprobe(tk);
	list_del(&tk->list);

	return 0;
}

/* Register a trace_probe and probe_event */
static int register_trace_kprobe(struct trace_kprobe *tk)
{
	struct trace_kprobe *old_tk;
	int ret;

	mutex_lock(&probe_lock);

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
	if (ret < 0)
		unregister_kprobe_event(tk);
	else
		list_add_tail(&tk->list, &probe_list);

end:
	mutex_unlock(&probe_lock);
	return ret;
}

/* Module notifier call back, checking event on the module */
static int trace_kprobe_module_callback(struct notifier_block *nb,
				       unsigned long val, void *data)
{
	struct module *mod = data;
	struct trace_kprobe *tk;
	int ret;

	if (val != MODULE_STATE_COMING)
		return NOTIFY_DONE;

	/* Update probes on coming module */
	mutex_lock(&probe_lock);
	list_for_each_entry(tk, &probe_list, list) {
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
	mutex_unlock(&probe_lock);

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

static int create_trace_kprobe(int argc, char **argv)
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
	struct trace_kprobe *tk;
	int i, ret = 0;
	bool is_return = false, is_delete = false;
	char *symbol = NULL, *event = NULL, *group = NULL;
	int maxactive = 0;
	char *arg;
	long offset = 0;
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

	event = strchr(&argv[0][1], ':');
	if (event) {
		event[0] = '\0';
		event++;
	}
	if (is_return && isdigit(argv[0][1])) {
		ret = kstrtouint(&argv[0][1], 0, &maxactive);
		if (ret) {
			pr_info("Failed to parse maxactive.\n");
			return ret;
		}
		/* kretprobes instances are iterated over via a list. The
		 * maximum should stay reasonable.
		 */
		if (maxactive > KRETPROBE_MAXACTIVE_MAX) {
			pr_info("Maxactive is too big (%d > %d).\n",
				maxactive, KRETPROBE_MAXACTIVE_MAX);
			return -E2BIG;
		}
	}

	if (event) {
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
		tk = find_trace_kprobe(event, group);
		if (!tk) {
			mutex_unlock(&probe_lock);
			pr_info("Event %s/%s doesn't exist.\n", group, event);
			return -ENOENT;
		}
		/* delete an event */
		ret = unregister_trace_kprobe(tk);
		if (ret == 0)
			free_trace_kprobe(tk);
		mutex_unlock(&probe_lock);
		return ret;
	}

	if (argc < 2) {
		pr_info("Probe point is not specified.\n");
		return -EINVAL;
	}

	/* try to parse an address. if that fails, try to read the
	 * input as a symbol. */
	if (kstrtoul(argv[1], 0, (unsigned long *)&addr)) {
		/* a symbol specified */
		symbol = argv[1];
		/* TODO: support .init module functions */
		ret = traceprobe_split_symbol_offset(symbol, &offset);
		if (ret || offset < 0 || offset > UINT_MAX) {
			pr_info("Failed to parse either an address or a symbol.\n");
			return ret;
		}
		if (offset && is_return &&
		    !kprobe_on_func_entry(NULL, symbol, offset)) {
			pr_info("Given offset is not valid for return probe.\n");
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
		sanitize_event_name(buf);
		event = buf;
	}
	tk = alloc_trace_kprobe(group, event, addr, symbol, offset, maxactive,
			       argc, is_return);
	if (IS_ERR(tk)) {
		pr_info("Failed to allocate trace_probe.(%d)\n",
			(int)PTR_ERR(tk));
		return PTR_ERR(tk);
	}

	/* parse arguments */
	ret = 0;
	for (i = 0; i < argc && i < MAX_TRACE_ARGS; i++) {
		struct probe_arg *parg = &tk->tp.args[i];

		/* Increment count for freeing args in error case */
		tk->tp.nr_args++;

		/* Parse argument name */
		arg = strchr(argv[i], '=');
		if (arg) {
			*arg++ = '\0';
			parg->name = kstrdup(argv[i], GFP_KERNEL);
		} else {
			arg = argv[i];
			/* If argument name is omitted, set "argN" */
			snprintf(buf, MAX_EVENT_NAME_LEN, "arg%d", i + 1);
			parg->name = kstrdup(buf, GFP_KERNEL);
		}

		if (!parg->name) {
			pr_info("Failed to allocate argument[%d] name.\n", i);
			ret = -ENOMEM;
			goto error;
		}

		if (!is_good_name(parg->name)) {
			pr_info("Invalid argument[%d] name: %s\n",
				i, parg->name);
			ret = -EINVAL;
			goto error;
		}

		if (traceprobe_conflict_field_name(parg->name,
							tk->tp.args, i)) {
			pr_info("Argument[%d] name '%s' conflicts with "
				"another field.\n", i, argv[i]);
			ret = -EINVAL;
			goto error;
		}

		/* Parse fetch argument */
		ret = traceprobe_parse_probe_arg(arg, &tk->tp.size, parg,
						is_return, true,
						kprobes_fetch_type_table);
		if (ret) {
			pr_info("Parse error at argument[%d]. (%d)\n", i, ret);
			goto error;
		}
	}

	ret = register_trace_kprobe(tk);
	if (ret)
		goto error;
	return 0;

error:
	free_trace_kprobe(tk);
	return ret;
}

static int release_all_trace_kprobes(void)
{
	struct trace_kprobe *tk;
	int ret = 0;

	mutex_lock(&probe_lock);
	/* Ensure no probe is in use. */
	list_for_each_entry(tk, &probe_list, list)
		if (trace_probe_is_enabled(&tk->tp)) {
			ret = -EBUSY;
			goto end;
		}
	/* TODO: Use batch unregistration */
	while (!list_empty(&probe_list)) {
		tk = list_entry(probe_list.next, struct trace_kprobe, list);
		ret = unregister_trace_kprobe(tk);
		if (ret)
			goto end;
		free_trace_kprobe(tk);
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
	struct trace_kprobe *tk = v;
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
		ret = release_all_trace_kprobes();
		if (ret < 0)
			return ret;
	}

	return seq_open(file, &probes_seq_op);
}

static ssize_t probes_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *ppos)
{
	return trace_parse_run_command(file, buffer, count, ppos,
				       create_trace_kprobe);
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
	struct trace_kprobe *tk = v;

	seq_printf(m, "  %-44s %15lu %15lu\n",
		   trace_event_name(&tk->tp.call),
		   trace_kprobe_nhit(tk),
		   tk->rp.kp.nmissed);

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
	store_trace_args(sizeof(*entry), &tk->tp, regs, (u8 *)&entry[1], dsize);

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
	store_trace_args(sizeof(*entry), &tk->tp, regs, (u8 *)&entry[1], dsize);

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
	u8 *data;
	int i;

	field = (struct kprobe_trace_entry_head *)iter->ent;
	tp = container_of(event, struct trace_probe, call.event);

	trace_seq_printf(s, "%s: (", trace_event_name(&tp->call));

	if (!seq_print_ip_sym(s, field->ip, flags | TRACE_ITER_SYM_OFFSET))
		goto out;

	trace_seq_putc(s, ')');

	data = (u8 *)&field[1];
	for (i = 0; i < tp->nr_args; i++)
		if (!tp->args[i].type->print(s, tp->args[i].name,
					     data + tp->args[i].offset, field))
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
	u8 *data;
	int i;

	field = (struct kretprobe_trace_entry_head *)iter->ent;
	tp = container_of(event, struct trace_probe, call.event);

	trace_seq_printf(s, "%s: (", trace_event_name(&tp->call));

	if (!seq_print_ip_sym(s, field->ret_ip, flags | TRACE_ITER_SYM_OFFSET))
		goto out;

	trace_seq_puts(s, " <- ");

	if (!seq_print_ip_sym(s, field->func, flags & ~TRACE_ITER_SYM_OFFSET))
		goto out;

	trace_seq_putc(s, ')');

	data = (u8 *)&field[1];
	for (i = 0; i < tp->nr_args; i++)
		if (!tp->args[i].type->print(s, tp->args[i].name,
					     data + tp->args[i].offset, field))
			goto out;

	trace_seq_putc(s, '\n');

 out:
	return trace_handle_return(s);
}


static int kprobe_event_define_fields(struct trace_event_call *event_call)
{
	int ret, i;
	struct kprobe_trace_entry_head field;
	struct trace_kprobe *tk = (struct trace_kprobe *)event_call->data;

	DEFINE_FIELD(unsigned long, ip, FIELD_STRING_IP, 0);
	/* Set argument names as fields */
	for (i = 0; i < tk->tp.nr_args; i++) {
		struct probe_arg *parg = &tk->tp.args[i];

		ret = trace_define_field(event_call, parg->type->fmttype,
					 parg->name,
					 sizeof(field) + parg->offset,
					 parg->type->size,
					 parg->type->is_signed,
					 FILTER_OTHER);
		if (ret)
			return ret;
	}
	return 0;
}

static int kretprobe_event_define_fields(struct trace_event_call *event_call)
{
	int ret, i;
	struct kretprobe_trace_entry_head field;
	struct trace_kprobe *tk = (struct trace_kprobe *)event_call->data;

	DEFINE_FIELD(unsigned long, func, FIELD_STRING_FUNC, 0);
	DEFINE_FIELD(unsigned long, ret_ip, FIELD_STRING_RETIP, 0);
	/* Set argument names as fields */
	for (i = 0; i < tk->tp.nr_args; i++) {
		struct probe_arg *parg = &tk->tp.args[i];

		ret = trace_define_field(event_call, parg->type->fmttype,
					 parg->name,
					 sizeof(field) + parg->offset,
					 parg->type->size,
					 parg->type->is_signed,
					 FILTER_OTHER);
		if (ret)
			return ret;
	}
	return 0;
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
		 * pt_regs, and if so clear the kprobe and return 1 so that we
		 * don't do the single stepping.
		 * The ftrace kprobe handler leaves it up to us to re-enable
		 * preemption here before returning if we've modified the ip.
		 */
		if (orig_ip != instruction_pointer(regs)) {
			reset_current_kprobe();
			preempt_enable_no_resched();
			return 1;
		}
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
	store_trace_args(sizeof(*entry), &tk->tp, regs, (u8 *)&entry[1], dsize);
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
	store_trace_args(sizeof(*entry), &tk->tp, regs, (u8 *)&entry[1], dsize);
	perf_trace_buf_submit(entry, size, rctx, call->event.type, 1, regs,
			      head, NULL);
}
NOKPROBE_SYMBOL(kretprobe_perf_func);
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

static int register_kprobe_event(struct trace_kprobe *tk)
{
	struct trace_event_call *call = &tk->tp.call;
	int ret;

	/* Initialize trace_event_call */
	INIT_LIST_HEAD(&call->class->fields);
	if (trace_kprobe_is_return(tk)) {
		call->event.funcs = &kretprobe_funcs;
		call->class->define_fields = kretprobe_event_define_fields;
	} else {
		call->event.funcs = &kprobe_funcs;
		call->class->define_fields = kprobe_event_define_fields;
	}
	if (set_print_fmt(&tk->tp, trace_kprobe_is_return(tk)) < 0)
		return -ENOMEM;
	ret = register_trace_event(&call->event);
	if (!ret) {
		kfree(call->print_fmt);
		return -ENODEV;
	}
	call->flags = TRACE_EVENT_FL_KPROBE;
	call->class->reg = kprobe_register;
	call->data = tk;
	ret = trace_add_event_call(call);
	if (ret) {
		pr_info("Failed to register kprobe event: %s\n",
			trace_event_name(call));
		kfree(call->print_fmt);
		unregister_trace_event(&call->event);
	}
	return ret;
}

static int unregister_kprobe_event(struct trace_kprobe *tk)
{
	int ret;

	/* tp->event is unregistered in trace_remove_event_call() */
	ret = trace_remove_event_call(&tk->tp.call);
	if (!ret)
		kfree(tk->tp.call.print_fmt);
	return ret;
}

/* Make a tracefs interface for controlling probe points */
static __init int init_kprobe_trace(void)
{
	struct dentry *d_tracer;
	struct dentry *entry;

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
	return 0;
}
fs_initcall(init_kprobe_trace);


#ifdef CONFIG_FTRACE_STARTUP_TEST
/*
 * The "__used" keeps gcc from removing the function symbol
 * from the kallsyms table. 'noinline' makes sure that there
 * isn't an inlined version used by the test method below
 */
static __used __init noinline int
kprobe_trace_selftest_target(int a1, int a2, int a3, int a4, int a5, int a6)
{
	return a1 + a2 + a3 + a4 + a5 + a6;
}

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

	target = kprobe_trace_selftest_target;

	pr_info("Testing kprobe tracing: ");

	ret = trace_run_command("p:testprobe kprobe_trace_selftest_target "
				"$stack $stack0 +0($stack)",
				create_trace_kprobe);
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

	ret = trace_run_command("r:testprobe2 kprobe_trace_selftest_target "
				"$retval", create_trace_kprobe);
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

	ret = trace_run_command("-:testprobe", create_trace_kprobe);
	if (WARN_ON_ONCE(ret)) {
		pr_warn("error on deleting a probe.\n");
		warn++;
	}

	ret = trace_run_command("-:testprobe2", create_trace_kprobe);
	if (WARN_ON_ONCE(ret)) {
		pr_warn("error on deleting a probe.\n");
		warn++;
	}

end:
	release_all_trace_kprobes();
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
