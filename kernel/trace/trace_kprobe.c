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
#include <linux/kprobes.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/debugfs.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/ptrace.h>
#include <linux/perf_event.h>
#include <linux/stringify.h>
#include <asm/bitsperlong.h>

#include "trace.h"
#include "trace_output.h"

#define MAX_TRACE_ARGS 128
#define MAX_ARGSTR_LEN 63
#define MAX_EVENT_NAME_LEN 64
#define KPROBE_EVENT_SYSTEM "kprobes"

/* Reserved field names */
#define FIELD_STRING_IP "__probe_ip"
#define FIELD_STRING_RETIP "__probe_ret_ip"
#define FIELD_STRING_FUNC "__probe_func"

const char *reserved_field_names[] = {
	"common_type",
	"common_flags",
	"common_preempt_count",
	"common_pid",
	"common_tgid",
	"common_lock_depth",
	FIELD_STRING_IP,
	FIELD_STRING_RETIP,
	FIELD_STRING_FUNC,
};

/* Printing function type */
typedef int (*print_type_func_t)(struct trace_seq *, const char *, void *);
#define PRINT_TYPE_FUNC_NAME(type)	print_type_##type
#define PRINT_TYPE_FMT_NAME(type)	print_type_format_##type

/* Printing  in basic type function template */
#define DEFINE_BASIC_PRINT_TYPE_FUNC(type, fmt, cast)			\
static __kprobes int PRINT_TYPE_FUNC_NAME(type)(struct trace_seq *s,	\
						const char *name, void *data)\
{									\
	return trace_seq_printf(s, " %s=" fmt, name, (cast)*(type *)data);\
}									\
static const char PRINT_TYPE_FMT_NAME(type)[] = fmt;

DEFINE_BASIC_PRINT_TYPE_FUNC(u8, "%x", unsigned int)
DEFINE_BASIC_PRINT_TYPE_FUNC(u16, "%x", unsigned int)
DEFINE_BASIC_PRINT_TYPE_FUNC(u32, "%lx", unsigned long)
DEFINE_BASIC_PRINT_TYPE_FUNC(u64, "%llx", unsigned long long)
DEFINE_BASIC_PRINT_TYPE_FUNC(s8, "%d", int)
DEFINE_BASIC_PRINT_TYPE_FUNC(s16, "%d", int)
DEFINE_BASIC_PRINT_TYPE_FUNC(s32, "%ld", long)
DEFINE_BASIC_PRINT_TYPE_FUNC(s64, "%lld", long long)

/* Data fetch function type */
typedef	void (*fetch_func_t)(struct pt_regs *, void *, void *);

struct fetch_param {
	fetch_func_t	fn;
	void *data;
};

static __kprobes void call_fetch(struct fetch_param *fprm,
				 struct pt_regs *regs, void *dest)
{
	return fprm->fn(regs, fprm->data, dest);
}

#define FETCH_FUNC_NAME(kind, type)	fetch_##kind##_##type
/*
 * Define macro for basic types - we don't need to define s* types, because
 * we have to care only about bitwidth at recording time.
 */
#define DEFINE_BASIC_FETCH_FUNCS(kind)  \
DEFINE_FETCH_##kind(u8)			\
DEFINE_FETCH_##kind(u16)		\
DEFINE_FETCH_##kind(u32)		\
DEFINE_FETCH_##kind(u64)

#define CHECK_BASIC_FETCH_FUNCS(kind, fn)	\
	((FETCH_FUNC_NAME(kind, u8) == fn) ||	\
	 (FETCH_FUNC_NAME(kind, u16) == fn) ||	\
	 (FETCH_FUNC_NAME(kind, u32) == fn) ||	\
	 (FETCH_FUNC_NAME(kind, u64) == fn))

/* Data fetch function templates */
#define DEFINE_FETCH_reg(type)						\
static __kprobes void FETCH_FUNC_NAME(reg, type)(struct pt_regs *regs,	\
					  void *offset, void *dest)	\
{									\
	*(type *)dest = (type)regs_get_register(regs,			\
				(unsigned int)((unsigned long)offset));	\
}
DEFINE_BASIC_FETCH_FUNCS(reg)

#define DEFINE_FETCH_stack(type)					\
static __kprobes void FETCH_FUNC_NAME(stack, type)(struct pt_regs *regs,\
					  void *offset, void *dest)	\
{									\
	*(type *)dest = (type)regs_get_kernel_stack_nth(regs,		\
				(unsigned int)((unsigned long)offset));	\
}
DEFINE_BASIC_FETCH_FUNCS(stack)

#define DEFINE_FETCH_retval(type)					\
static __kprobes void FETCH_FUNC_NAME(retval, type)(struct pt_regs *regs,\
					  void *dummy, void *dest)	\
{									\
	*(type *)dest = (type)regs_return_value(regs);			\
}
DEFINE_BASIC_FETCH_FUNCS(retval)

#define DEFINE_FETCH_memory(type)					\
static __kprobes void FETCH_FUNC_NAME(memory, type)(struct pt_regs *regs,\
					  void *addr, void *dest)	\
{									\
	type retval;							\
	if (probe_kernel_address(addr, retval))				\
		*(type *)dest = 0;					\
	else								\
		*(type *)dest = retval;					\
}
DEFINE_BASIC_FETCH_FUNCS(memory)

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

#define DEFINE_FETCH_symbol(type)					\
static __kprobes void FETCH_FUNC_NAME(symbol, type)(struct pt_regs *regs,\
					  void *data, void *dest)	\
{									\
	struct symbol_cache *sc = data;					\
	if (sc->addr)							\
		fetch_memory_##type(regs, (void *)sc->addr, dest);	\
	else								\
		*(type *)dest = 0;					\
}
DEFINE_BASIC_FETCH_FUNCS(symbol)

/* Dereference memory access function */
struct deref_fetch_param {
	struct fetch_param orig;
	long offset;
};

#define DEFINE_FETCH_deref(type)					\
static __kprobes void FETCH_FUNC_NAME(deref, type)(struct pt_regs *regs,\
					    void *data, void *dest)	\
{									\
	struct deref_fetch_param *dprm = data;				\
	unsigned long addr;						\
	call_fetch(&dprm->orig, regs, &addr);				\
	if (addr) {							\
		addr += dprm->offset;					\
		fetch_memory_##type(regs, (void *)addr, dest);		\
	} else								\
		*(type *)dest = 0;					\
}
DEFINE_BASIC_FETCH_FUNCS(deref)

static __kprobes void free_deref_fetch_param(struct deref_fetch_param *data)
{
	if (CHECK_BASIC_FETCH_FUNCS(deref, data->orig.fn))
		free_deref_fetch_param(data->orig.data);
	else if (CHECK_BASIC_FETCH_FUNCS(symbol, data->orig.fn))
		free_symbol_cache(data->orig.data);
	kfree(data);
}

/* Default (unsigned long) fetch type */
#define __DEFAULT_FETCH_TYPE(t) u##t
#define _DEFAULT_FETCH_TYPE(t) __DEFAULT_FETCH_TYPE(t)
#define DEFAULT_FETCH_TYPE _DEFAULT_FETCH_TYPE(BITS_PER_LONG)
#define DEFAULT_FETCH_TYPE_STR __stringify(DEFAULT_FETCH_TYPE)

#define ASSIGN_FETCH_FUNC(kind, type)	\
	.kind = FETCH_FUNC_NAME(kind, type)

#define ASSIGN_FETCH_TYPE(ptype, ftype, sign)	\
	{.name = #ptype,			\
	 .size = sizeof(ftype),			\
	 .is_signed = sign,			\
	 .print = PRINT_TYPE_FUNC_NAME(ptype),	\
	 .fmt = PRINT_TYPE_FMT_NAME(ptype),	\
ASSIGN_FETCH_FUNC(reg, ftype),			\
ASSIGN_FETCH_FUNC(stack, ftype),		\
ASSIGN_FETCH_FUNC(retval, ftype),		\
ASSIGN_FETCH_FUNC(memory, ftype),		\
ASSIGN_FETCH_FUNC(symbol, ftype),		\
ASSIGN_FETCH_FUNC(deref, ftype),		\
	}

/* Fetch type information table */
static const struct fetch_type {
	const char	*name;		/* Name of type */
	size_t		size;		/* Byte size of type */
	int		is_signed;	/* Signed flag */
	print_type_func_t	print;	/* Print functions */
	const char	*fmt;		/* Fromat string */
	/* Fetch functions */
	fetch_func_t	reg;
	fetch_func_t	stack;
	fetch_func_t	retval;
	fetch_func_t	memory;
	fetch_func_t	symbol;
	fetch_func_t	deref;
} fetch_type_table[] = {
	ASSIGN_FETCH_TYPE(u8,  u8,  0),
	ASSIGN_FETCH_TYPE(u16, u16, 0),
	ASSIGN_FETCH_TYPE(u32, u32, 0),
	ASSIGN_FETCH_TYPE(u64, u64, 0),
	ASSIGN_FETCH_TYPE(s8,  u8,  1),
	ASSIGN_FETCH_TYPE(s16, u16, 1),
	ASSIGN_FETCH_TYPE(s32, u32, 1),
	ASSIGN_FETCH_TYPE(s64, u64, 1),
};

static const struct fetch_type *find_fetch_type(const char *type)
{
	int i;

	if (!type)
		type = DEFAULT_FETCH_TYPE_STR;

	for (i = 0; i < ARRAY_SIZE(fetch_type_table); i++)
		if (strcmp(type, fetch_type_table[i].name) == 0)
			return &fetch_type_table[i];
	return NULL;
}

/* Special function : only accept unsigned long */
static __kprobes void fetch_stack_address(struct pt_regs *regs,
					  void *dummy, void *dest)
{
	*(unsigned long *)dest = kernel_stack_pointer(regs);
}

/**
 * Kprobe event core functions
 */

struct probe_arg {
	struct fetch_param	fetch;
	unsigned int		offset;	/* Offset from argument entry */
	const char		*name;	/* Name of this argument */
	const char		*comm;	/* Command of this argument */
	const struct fetch_type	*type;	/* Type of this argument */
};

/* Flags for trace_probe */
#define TP_FLAG_TRACE	1
#define TP_FLAG_PROFILE	2

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


static __kprobes int probe_is_return(struct trace_probe *tp)
{
	return tp->rp.handler != NULL;
}

static __kprobes const char *probe_symbol(struct trace_probe *tp)
{
	return tp->symbol ? tp->symbol : "unknown";
}

static int register_probe_event(struct trace_probe *tp);
static void unregister_probe_event(struct trace_probe *tp);

static DEFINE_MUTEX(probe_lock);
static LIST_HEAD(probe_list);

static int kprobe_dispatcher(struct kprobe *kp, struct pt_regs *regs);
static int kretprobe_dispatcher(struct kretprobe_instance *ri,
				struct pt_regs *regs);

/* Check the name is good for event/group */
static int check_event_name(const char *name)
{
	if (!isalpha(*name) && *name != '_')
		return 0;
	while (*++name != '\0') {
		if (!isalpha(*name) && !isdigit(*name) && *name != '_')
			return 0;
	}
	return 1;
}

/*
 * Allocate new trace_probe and initialize it (including kprobes).
 */
static struct trace_probe *alloc_trace_probe(const char *group,
					     const char *event,
					     void *addr,
					     const char *symbol,
					     unsigned long offs,
					     int nargs, int is_return)
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

	if (!event || !check_event_name(event)) {
		ret = -EINVAL;
		goto error;
	}

	tp->call.class = &tp->class;
	tp->call.name = kstrdup(event, GFP_KERNEL);
	if (!tp->call.name)
		goto error;

	if (!group || !check_event_name(group)) {
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

static void free_probe_arg(struct probe_arg *arg)
{
	if (CHECK_BASIC_FETCH_FUNCS(deref, arg->fetch.fn))
		free_deref_fetch_param(arg->fetch.data);
	else if (CHECK_BASIC_FETCH_FUNCS(symbol, arg->fetch.fn))
		free_symbol_cache(arg->fetch.data);
	kfree(arg->name);
	kfree(arg->comm);
}

static void free_trace_probe(struct trace_probe *tp)
{
	int i;

	for (i = 0; i < tp->nr_args; i++)
		free_probe_arg(&tp->args[i]);

	kfree(tp->call.class->system);
	kfree(tp->call.name);
	kfree(tp->symbol);
	kfree(tp);
}

static struct trace_probe *find_probe_event(const char *event,
					    const char *group)
{
	struct trace_probe *tp;

	list_for_each_entry(tp, &probe_list, list)
		if (strcmp(tp->call.name, event) == 0 &&
		    strcmp(tp->call.class->system, group) == 0)
			return tp;
	return NULL;
}

/* Unregister a trace_probe and probe_event: call with locking probe_lock */
static void unregister_trace_probe(struct trace_probe *tp)
{
	if (probe_is_return(tp))
		unregister_kretprobe(&tp->rp);
	else
		unregister_kprobe(&tp->rp.kp);
	list_del(&tp->list);
	unregister_probe_event(tp);
}

/* Register a trace_probe and probe_event */
static int register_trace_probe(struct trace_probe *tp)
{
	struct trace_probe *old_tp;
	int ret;

	mutex_lock(&probe_lock);

	/* register as an event */
	old_tp = find_probe_event(tp->call.name, tp->call.class->system);
	if (old_tp) {
		/* delete old event */
		unregister_trace_probe(old_tp);
		free_trace_probe(old_tp);
	}
	ret = register_probe_event(tp);
	if (ret) {
		pr_warning("Faild to register probe event(%d)\n", ret);
		goto end;
	}

	tp->rp.kp.flags |= KPROBE_FLAG_DISABLED;
	if (probe_is_return(tp))
		ret = register_kretprobe(&tp->rp);
	else
		ret = register_kprobe(&tp->rp.kp);

	if (ret) {
		pr_warning("Could not insert probe(%d)\n", ret);
		if (ret == -EILSEQ) {
			pr_warning("Probing address(0x%p) is not an "
				   "instruction boundary.\n",
				   tp->rp.kp.addr);
			ret = -EINVAL;
		}
		unregister_probe_event(tp);
	} else
		list_add_tail(&tp->list, &probe_list);
end:
	mutex_unlock(&probe_lock);
	return ret;
}

/* Split symbol and offset. */
static int split_symbol_offset(char *symbol, unsigned long *offset)
{
	char *tmp;
	int ret;

	if (!offset)
		return -EINVAL;

	tmp = strchr(symbol, '+');
	if (tmp) {
		/* skip sign because strict_strtol doesn't accept '+' */
		ret = strict_strtoul(tmp + 1, 0, offset);
		if (ret)
			return ret;
		*tmp = '\0';
	} else
		*offset = 0;
	return 0;
}

#define PARAM_MAX_ARGS 16
#define PARAM_MAX_STACK (THREAD_SIZE / sizeof(unsigned long))

static int parse_probe_vars(char *arg, const struct fetch_type *t,
			    struct fetch_param *f, int is_return)
{
	int ret = 0;
	unsigned long param;

	if (strcmp(arg, "retval") == 0) {
		if (is_return)
			f->fn = t->retval;
		else
			ret = -EINVAL;
	} else if (strncmp(arg, "stack", 5) == 0) {
		if (arg[5] == '\0') {
			if (strcmp(t->name, DEFAULT_FETCH_TYPE_STR) == 0)
				f->fn = fetch_stack_address;
			else
				ret = -EINVAL;
		} else if (isdigit(arg[5])) {
			ret = strict_strtoul(arg + 5, 10, &param);
			if (ret || param > PARAM_MAX_STACK)
				ret = -EINVAL;
			else {
				f->fn = t->stack;
				f->data = (void *)param;
			}
		} else
			ret = -EINVAL;
	} else
		ret = -EINVAL;
	return ret;
}

/* Recursive argument parser */
static int __parse_probe_arg(char *arg, const struct fetch_type *t,
			     struct fetch_param *f, int is_return)
{
	int ret = 0;
	unsigned long param;
	long offset;
	char *tmp;

	switch (arg[0]) {
	case '$':
		ret = parse_probe_vars(arg + 1, t, f, is_return);
		break;
	case '%':	/* named register */
		ret = regs_query_register_offset(arg + 1);
		if (ret >= 0) {
			f->fn = t->reg;
			f->data = (void *)(unsigned long)ret;
			ret = 0;
		}
		break;
	case '@':	/* memory or symbol */
		if (isdigit(arg[1])) {
			ret = strict_strtoul(arg + 1, 0, &param);
			if (ret)
				break;
			f->fn = t->memory;
			f->data = (void *)param;
		} else {
			ret = split_symbol_offset(arg + 1, &offset);
			if (ret)
				break;
			f->data = alloc_symbol_cache(arg + 1, offset);
			if (f->data)
				f->fn = t->symbol;
		}
		break;
	case '+':	/* deref memory */
	case '-':
		tmp = strchr(arg, '(');
		if (!tmp)
			break;
		*tmp = '\0';
		ret = strict_strtol(arg + 1, 0, &offset);
		if (ret)
			break;
		if (arg[0] == '-')
			offset = -offset;
		arg = tmp + 1;
		tmp = strrchr(arg, ')');
		if (tmp) {
			struct deref_fetch_param *dprm;
			const struct fetch_type *t2 = find_fetch_type(NULL);
			*tmp = '\0';
			dprm = kzalloc(sizeof(struct deref_fetch_param),
				       GFP_KERNEL);
			if (!dprm)
				return -ENOMEM;
			dprm->offset = offset;
			ret = __parse_probe_arg(arg, t2, &dprm->orig,
						is_return);
			if (ret)
				kfree(dprm);
			else {
				f->fn = t->deref;
				f->data = (void *)dprm;
			}
		}
		break;
	}
	if (!ret && !f->fn)
		ret = -EINVAL;
	return ret;
}

/* String length checking wrapper */
static int parse_probe_arg(char *arg, struct trace_probe *tp,
			   struct probe_arg *parg, int is_return)
{
	const char *t;

	if (strlen(arg) > MAX_ARGSTR_LEN) {
		pr_info("Argument is too long.: %s\n",  arg);
		return -ENOSPC;
	}
	parg->comm = kstrdup(arg, GFP_KERNEL);
	if (!parg->comm) {
		pr_info("Failed to allocate memory for command '%s'.\n", arg);
		return -ENOMEM;
	}
	t = strchr(parg->comm, ':');
	if (t) {
		arg[t - parg->comm] = '\0';
		t++;
	}
	parg->type = find_fetch_type(t);
	if (!parg->type) {
		pr_info("Unsupported type: %s\n", t);
		return -EINVAL;
	}
	parg->offset = tp->size;
	tp->size += parg->type->size;
	return __parse_probe_arg(arg, parg->type, &parg->fetch, is_return);
}

/* Return 1 if name is reserved or already used by another argument */
static int conflict_field_name(const char *name,
			       struct probe_arg *args, int narg)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(reserved_field_names); i++)
		if (strcmp(reserved_field_names[i], name) == 0)
			return 1;
	for (i = 0; i < narg; i++)
		if (strcmp(args[i].name, name) == 0)
			return 1;
	return 0;
}

static int create_trace_probe(int argc, char **argv)
{
	/*
	 * Argument syntax:
	 *  - Add kprobe: p[:[GRP/]EVENT] KSYM[+OFFS]|KADDR [FETCHARGS]
	 *  - Add kretprobe: r[:[GRP/]EVENT] KSYM[+0] [FETCHARGS]
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
	int is_return = 0, is_delete = 0;
	char *symbol = NULL, *event = NULL, *group = NULL;
	char *arg, *tmp;
	unsigned long offset = 0;
	void *addr = NULL;
	char buf[MAX_EVENT_NAME_LEN];

	/* argc must be >= 1 */
	if (argv[0][0] == 'p')
		is_return = 0;
	else if (argv[0][0] == 'r')
		is_return = 1;
	else if (argv[0][0] == '-')
		is_delete = 1;
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
		tp = find_probe_event(event, group);
		if (!tp) {
			pr_info("Event %s/%s doesn't exist.\n", group, event);
			return -ENOENT;
		}
		/* delete an event */
		unregister_trace_probe(tp);
		free_trace_probe(tp);
		return 0;
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
		ret = strict_strtoul(&argv[1][0], 0, (unsigned long *)&addr);
		if (ret) {
			pr_info("Failed to parse address.\n");
			return ret;
		}
	} else {
		/* a symbol specified */
		symbol = argv[1];
		/* TODO: support .init module functions */
		ret = split_symbol_offset(symbol, &offset);
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
		/* Parse argument name */
		arg = strchr(argv[i], '=');
		if (arg)
			*arg++ = '\0';
		else
			arg = argv[i];

		tp->args[i].name = kstrdup(argv[i], GFP_KERNEL);
		if (!tp->args[i].name) {
			pr_info("Failed to allocate argument%d name '%s'.\n",
				i, argv[i]);
			ret = -ENOMEM;
			goto error;
		}
		tmp = strchr(tp->args[i].name, ':');
		if (tmp)
			*tmp = '_';	/* convert : to _ */

		if (conflict_field_name(tp->args[i].name, tp->args, i)) {
			pr_info("Argument%d name '%s' conflicts with "
				"another field.\n", i, argv[i]);
			ret = -EINVAL;
			goto error;
		}

		/* Parse fetch argument */
		ret = parse_probe_arg(arg, tp, &tp->args[i], is_return);
		if (ret) {
			pr_info("Parse error at argument%d. (%d)\n", i, ret);
			kfree(tp->args[i].name);
			goto error;
		}

		tp->nr_args++;
	}

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
	int i;

	seq_printf(m, "%c", probe_is_return(tp) ? 'r' : 'p');
	seq_printf(m, ":%s/%s", tp->call.class->system, tp->call.name);

	if (!tp->symbol)
		seq_printf(m, " 0x%p", tp->rp.kp.addr);
	else if (tp->rp.kp.offset)
		seq_printf(m, " %s+%u", probe_symbol(tp), tp->rp.kp.offset);
	else
		seq_printf(m, " %s", probe_symbol(tp));

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

/* Kprobe handler */
static __kprobes void kprobe_trace_func(struct kprobe *kp, struct pt_regs *regs)
{
	struct trace_probe *tp = container_of(kp, struct trace_probe, rp.kp);
	struct kprobe_trace_entry_head *entry;
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	u8 *data;
	int size, i, pc;
	unsigned long irq_flags;
	struct ftrace_event_call *call = &tp->call;

	tp->nhit++;

	local_save_flags(irq_flags);
	pc = preempt_count();

	size = sizeof(*entry) + tp->size;

	event = trace_current_buffer_lock_reserve(&buffer, call->event.type,
						  size, irq_flags, pc);
	if (!event)
		return;

	entry = ring_buffer_event_data(event);
	entry->ip = (unsigned long)kp->addr;
	data = (u8 *)&entry[1];
	for (i = 0; i < tp->nr_args; i++)
		call_fetch(&tp->args[i].fetch, regs, data + tp->args[i].offset);

	if (!filter_current_check_discard(buffer, call, entry, event))
		trace_nowake_buffer_unlock_commit(buffer, event, irq_flags, pc);
}

/* Kretprobe handler */
static __kprobes void kretprobe_trace_func(struct kretprobe_instance *ri,
					  struct pt_regs *regs)
{
	struct trace_probe *tp = container_of(ri->rp, struct trace_probe, rp);
	struct kretprobe_trace_entry_head *entry;
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	u8 *data;
	int size, i, pc;
	unsigned long irq_flags;
	struct ftrace_event_call *call = &tp->call;

	local_save_flags(irq_flags);
	pc = preempt_count();

	size = sizeof(*entry) + tp->size;

	event = trace_current_buffer_lock_reserve(&buffer, call->event.type,
						  size, irq_flags, pc);
	if (!event)
		return;

	entry = ring_buffer_event_data(event);
	entry->func = (unsigned long)tp->rp.kp.addr;
	entry->ret_ip = (unsigned long)ri->ret_addr;
	data = (u8 *)&entry[1];
	for (i = 0; i < tp->nr_args; i++)
		call_fetch(&tp->args[i].fetch, regs, data + tp->args[i].offset);

	if (!filter_current_check_discard(buffer, call, entry, event))
		trace_nowake_buffer_unlock_commit(buffer, event, irq_flags, pc);
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
					     data + tp->args[i].offset))
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
					     data + tp->args[i].offset))
			goto partial;

	if (!trace_seq_puts(s, "\n"))
		goto partial;

	return TRACE_TYPE_HANDLED;
partial:
	return TRACE_TYPE_PARTIAL_LINE;
}

static int probe_event_enable(struct ftrace_event_call *call)
{
	struct trace_probe *tp = (struct trace_probe *)call->data;

	tp->flags |= TP_FLAG_TRACE;
	if (probe_is_return(tp))
		return enable_kretprobe(&tp->rp);
	else
		return enable_kprobe(&tp->rp.kp);
}

static void probe_event_disable(struct ftrace_event_call *call)
{
	struct trace_probe *tp = (struct trace_probe *)call->data;

	tp->flags &= ~TP_FLAG_TRACE;
	if (!(tp->flags & (TP_FLAG_TRACE | TP_FLAG_PROFILE))) {
		if (probe_is_return(tp))
			disable_kretprobe(&tp->rp);
		else
			disable_kprobe(&tp->rp.kp);
	}
}

static int probe_event_raw_init(struct ftrace_event_call *event_call)
{
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
	struct kprobe_trace_entry_head field;
	struct trace_probe *tp = (struct trace_probe *)event_call->data;

	DEFINE_FIELD(unsigned long, ip, FIELD_STRING_IP, 0);
	/* Set argument names as fields */
	for (i = 0; i < tp->nr_args; i++) {
		ret = trace_define_field(event_call, tp->args[i].type->name,
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
		ret = trace_define_field(event_call, tp->args[i].type->name,
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

	if (!probe_is_return(tp)) {
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
	u8 *data;
	int size, __size, i;
	int rctx;

	__size = sizeof(*entry) + tp->size;
	size = ALIGN(__size + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);
	if (WARN_ONCE(size > PERF_MAX_TRACE_SIZE,
		     "profile buffer not large enough"))
		return;

	entry = perf_trace_buf_prepare(size, call->event.type, regs, &rctx);
	if (!entry)
		return;

	entry->ip = (unsigned long)kp->addr;
	data = (u8 *)&entry[1];
	for (i = 0; i < tp->nr_args; i++)
		call_fetch(&tp->args[i].fetch, regs, data + tp->args[i].offset);

	head = this_cpu_ptr(call->perf_events);
	perf_trace_buf_submit(entry, size, rctx, entry->ip, 1, regs, head);
}

/* Kretprobe profile handler */
static __kprobes void kretprobe_perf_func(struct kretprobe_instance *ri,
					    struct pt_regs *regs)
{
	struct trace_probe *tp = container_of(ri->rp, struct trace_probe, rp);
	struct ftrace_event_call *call = &tp->call;
	struct kretprobe_trace_entry_head *entry;
	struct hlist_head *head;
	u8 *data;
	int size, __size, i;
	int rctx;

	__size = sizeof(*entry) + tp->size;
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
	data = (u8 *)&entry[1];
	for (i = 0; i < tp->nr_args; i++)
		call_fetch(&tp->args[i].fetch, regs, data + tp->args[i].offset);

	head = this_cpu_ptr(call->perf_events);
	perf_trace_buf_submit(entry, size, rctx, entry->ret_ip, 1, regs, head);
}

static int probe_perf_enable(struct ftrace_event_call *call)
{
	struct trace_probe *tp = (struct trace_probe *)call->data;

	tp->flags |= TP_FLAG_PROFILE;

	if (probe_is_return(tp))
		return enable_kretprobe(&tp->rp);
	else
		return enable_kprobe(&tp->rp.kp);
}

static void probe_perf_disable(struct ftrace_event_call *call)
{
	struct trace_probe *tp = (struct trace_probe *)call->data;

	tp->flags &= ~TP_FLAG_PROFILE;

	if (!(tp->flags & TP_FLAG_TRACE)) {
		if (probe_is_return(tp))
			disable_kretprobe(&tp->rp);
		else
			disable_kprobe(&tp->rp.kp);
	}
}
#endif	/* CONFIG_PERF_EVENTS */

static __kprobes
int kprobe_register(struct ftrace_event_call *event, enum trace_reg type)
{
	switch (type) {
	case TRACE_REG_REGISTER:
		return probe_event_enable(event);
	case TRACE_REG_UNREGISTER:
		probe_event_disable(event);
		return 0;

#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
		return probe_perf_enable(event);
	case TRACE_REG_PERF_UNREGISTER:
		probe_perf_disable(event);
		return 0;
#endif
	}
	return 0;
}

static __kprobes
int kprobe_dispatcher(struct kprobe *kp, struct pt_regs *regs)
{
	struct trace_probe *tp = container_of(kp, struct trace_probe, rp.kp);

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
	if (probe_is_return(tp)) {
		INIT_LIST_HEAD(&call->class->fields);
		call->event.funcs = &kretprobe_funcs;
		call->class->raw_init = probe_event_raw_init;
		call->class->define_fields = kretprobe_event_define_fields;
	} else {
		INIT_LIST_HEAD(&call->class->fields);
		call->event.funcs = &kprobe_funcs;
		call->class->raw_init = probe_event_raw_init;
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

/* Make a debugfs interface for controling probe points */
static __init int init_kprobe_trace(void)
{
	struct dentry *d_tracer;
	struct dentry *entry;

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

static int kprobe_trace_selftest_target(int a1, int a2, int a3,
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

	ret = command_trace_probe("p:testprobe kprobe_trace_selftest_target "
				  "$stack $stack0 +0($stack)");
	if (WARN_ON_ONCE(ret)) {
		pr_warning("error on probing function entry.\n");
		warn++;
	} else {
		/* Enable trace point */
		tp = find_probe_event("testprobe", KPROBE_EVENT_SYSTEM);
		if (WARN_ON_ONCE(tp == NULL)) {
			pr_warning("error on getting new probe.\n");
			warn++;
		} else
			probe_event_enable(&tp->call);
	}

	ret = command_trace_probe("r:testprobe2 kprobe_trace_selftest_target "
				  "$retval");
	if (WARN_ON_ONCE(ret)) {
		pr_warning("error on probing function return.\n");
		warn++;
	} else {
		/* Enable trace point */
		tp = find_probe_event("testprobe2", KPROBE_EVENT_SYSTEM);
		if (WARN_ON_ONCE(tp == NULL)) {
			pr_warning("error on getting new probe.\n");
			warn++;
		} else
			probe_event_enable(&tp->call);
	}

	if (warn)
		goto end;

	ret = target(1, 2, 3, 4, 5, 6);

	ret = command_trace_probe("-:testprobe");
	if (WARN_ON_ONCE(ret)) {
		pr_warning("error on deleting a probe.\n");
		warn++;
	}

	ret = command_trace_probe("-:testprobe2");
	if (WARN_ON_ONCE(ret)) {
		pr_warning("error on deleting a probe.\n");
		warn++;
	}

end:
	cleanup_all_probes();
	if (warn)
		pr_cont("NG: Some tests are failed. Please check them.\n");
	else
		pr_cont("OK\n");
	return 0;
}

late_initcall(kprobe_trace_self_tests_init);

#endif
