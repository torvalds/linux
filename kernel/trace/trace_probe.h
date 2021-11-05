// SPDX-License-Identifier: GPL-2.0
/*
 * Common header file for probe-based Dynamic events.
 *
 * This code was copied from kernel/trace/trace_kprobe.h written by
 * Masami Hiramatsu <masami.hiramatsu.pt@hitachi.com>
 *
 * Updates to make this generic:
 * Copyright (C) IBM Corporation, 2010-2011
 * Author:     Srikar Dronamraju
 */

#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/tracefs.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/perf_event.h>
#include <linux/kprobes.h>
#include <linux/stringify.h>
#include <linux/limits.h>
#include <linux/uaccess.h>
#include <linux/bitops.h>
#include <asm/bitsperlong.h>

#include "trace.h"
#include "trace_output.h"

#define MAX_TRACE_ARGS		128
#define MAX_ARGSTR_LEN		63
#define MAX_ARRAY_LEN		64
#define MAX_ARG_NAME_LEN	32
#define MAX_STRING_SIZE		PATH_MAX

/* Reserved field names */
#define FIELD_STRING_IP		"__probe_ip"
#define FIELD_STRING_RETIP	"__probe_ret_ip"
#define FIELD_STRING_FUNC	"__probe_func"
#define FIELD_STRING_TYPE	"__probe_type"

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


/* Flags for trace_probe */
#define TP_FLAG_TRACE		1
#define TP_FLAG_PROFILE		2

/* data_loc: data location, compatible with u32 */
#define make_data_loc(len, offs)	\
	(((u32)(len) << 16) | ((u32)(offs) & 0xffff))
#define get_loc_len(dl)		((u32)(dl) >> 16)
#define get_loc_offs(dl)	((u32)(dl) & 0xffff)

static nokprobe_inline void *get_loc_data(u32 *dl, void *ent)
{
	return (u8 *)ent + get_loc_offs(*dl);
}

static nokprobe_inline u32 update_data_loc(u32 loc, int consumed)
{
	u32 maxlen = get_loc_len(loc);
	u32 offset = get_loc_offs(loc);

	return make_data_loc(maxlen - consumed, offset + consumed);
}

/* Printing function type */
typedef int (*print_type_func_t)(struct trace_seq *, void *, void *);

enum fetch_op {
	FETCH_OP_NOP = 0,
	// Stage 1 (load) ops
	FETCH_OP_REG,		/* Register : .param = offset */
	FETCH_OP_STACK,		/* Stack : .param = index */
	FETCH_OP_STACKP,	/* Stack pointer */
	FETCH_OP_RETVAL,	/* Return value */
	FETCH_OP_IMM,		/* Immediate : .immediate */
	FETCH_OP_COMM,		/* Current comm */
	FETCH_OP_ARG,		/* Function argument : .param */
	FETCH_OP_FOFFS,		/* File offset: .immediate */
	FETCH_OP_DATA,		/* Allocated data: .data */
	// Stage 2 (dereference) op
	FETCH_OP_DEREF,		/* Dereference: .offset */
	FETCH_OP_UDEREF,	/* User-space Dereference: .offset */
	// Stage 3 (store) ops
	FETCH_OP_ST_RAW,	/* Raw: .size */
	FETCH_OP_ST_MEM,	/* Mem: .offset, .size */
	FETCH_OP_ST_UMEM,	/* Mem: .offset, .size */
	FETCH_OP_ST_STRING,	/* String: .offset, .size */
	FETCH_OP_ST_USTRING,	/* User String: .offset, .size */
	// Stage 4 (modify) op
	FETCH_OP_MOD_BF,	/* Bitfield: .basesize, .lshift, .rshift */
	// Stage 5 (loop) op
	FETCH_OP_LP_ARRAY,	/* Array: .param = loop count */
	FETCH_OP_TP_ARG,	/* Trace Point argument */
	FETCH_OP_END,
	FETCH_NOP_SYMBOL,	/* Unresolved Symbol holder */
};

struct fetch_insn {
	enum fetch_op op;
	union {
		unsigned int param;
		struct {
			unsigned int size;
			int offset;
		};
		struct {
			unsigned char basesize;
			unsigned char lshift;
			unsigned char rshift;
		};
		unsigned long immediate;
		void *data;
	};
};

/* fetch + deref*N + store + mod + end <= 16, this allows N=12, enough */
#define FETCH_INSN_MAX	16
#define FETCH_TOKEN_COMM	(-ECOMM)

/* Fetch type information table */
struct fetch_type {
	const char		*name;		/* Name of type */
	size_t			size;		/* Byte size of type */
	int			is_signed;	/* Signed flag */
	print_type_func_t	print;		/* Print functions */
	const char		*fmt;		/* Format string */
	const char		*fmttype;	/* Name in format file */
};

/* For defining macros, define string/string_size types */
typedef u32 string;
typedef u32 string_size;

#define PRINT_TYPE_FUNC_NAME(type)	print_type_##type
#define PRINT_TYPE_FMT_NAME(type)	print_type_format_##type

/* Printing  in basic type function template */
#define DECLARE_BASIC_PRINT_TYPE_FUNC(type)				\
int PRINT_TYPE_FUNC_NAME(type)(struct trace_seq *s, void *data, void *ent);\
extern const char PRINT_TYPE_FMT_NAME(type)[]

DECLARE_BASIC_PRINT_TYPE_FUNC(u8);
DECLARE_BASIC_PRINT_TYPE_FUNC(u16);
DECLARE_BASIC_PRINT_TYPE_FUNC(u32);
DECLARE_BASIC_PRINT_TYPE_FUNC(u64);
DECLARE_BASIC_PRINT_TYPE_FUNC(s8);
DECLARE_BASIC_PRINT_TYPE_FUNC(s16);
DECLARE_BASIC_PRINT_TYPE_FUNC(s32);
DECLARE_BASIC_PRINT_TYPE_FUNC(s64);
DECLARE_BASIC_PRINT_TYPE_FUNC(x8);
DECLARE_BASIC_PRINT_TYPE_FUNC(x16);
DECLARE_BASIC_PRINT_TYPE_FUNC(x32);
DECLARE_BASIC_PRINT_TYPE_FUNC(x64);

DECLARE_BASIC_PRINT_TYPE_FUNC(string);
DECLARE_BASIC_PRINT_TYPE_FUNC(symbol);

/* Default (unsigned long) fetch type */
#define __DEFAULT_FETCH_TYPE(t) x##t
#define _DEFAULT_FETCH_TYPE(t) __DEFAULT_FETCH_TYPE(t)
#define DEFAULT_FETCH_TYPE _DEFAULT_FETCH_TYPE(BITS_PER_LONG)
#define DEFAULT_FETCH_TYPE_STR __stringify(DEFAULT_FETCH_TYPE)

#define __ADDR_FETCH_TYPE(t) u##t
#define _ADDR_FETCH_TYPE(t) __ADDR_FETCH_TYPE(t)
#define ADDR_FETCH_TYPE _ADDR_FETCH_TYPE(BITS_PER_LONG)

#define __ASSIGN_FETCH_TYPE(_name, ptype, ftype, _size, sign, _fmttype)	\
	{.name = _name,				\
	 .size = _size,					\
	 .is_signed = sign,				\
	 .print = PRINT_TYPE_FUNC_NAME(ptype),		\
	 .fmt = PRINT_TYPE_FMT_NAME(ptype),		\
	 .fmttype = _fmttype,				\
	}
#define _ASSIGN_FETCH_TYPE(_name, ptype, ftype, _size, sign, _fmttype)	\
	__ASSIGN_FETCH_TYPE(_name, ptype, ftype, _size, sign, #_fmttype)
#define ASSIGN_FETCH_TYPE(ptype, ftype, sign)			\
	_ASSIGN_FETCH_TYPE(#ptype, ptype, ftype, sizeof(ftype), sign, ptype)

/* If ptype is an alias of atype, use this macro (show atype in format) */
#define ASSIGN_FETCH_TYPE_ALIAS(ptype, atype, ftype, sign)		\
	_ASSIGN_FETCH_TYPE(#ptype, ptype, ftype, sizeof(ftype), sign, atype)

#define ASSIGN_FETCH_TYPE_END {}
#define MAX_ARRAY_LEN	64

#ifdef CONFIG_KPROBE_EVENTS
bool trace_kprobe_on_func_entry(struct trace_event_call *call);
bool trace_kprobe_error_injectable(struct trace_event_call *call);
#else
static inline bool trace_kprobe_on_func_entry(struct trace_event_call *call)
{
	return false;
}

static inline bool trace_kprobe_error_injectable(struct trace_event_call *call)
{
	return false;
}
#endif /* CONFIG_KPROBE_EVENTS */

struct probe_arg {
	struct fetch_insn	*code;
	bool			dynamic;/* Dynamic array (string) is used */
	unsigned int		offset;	/* Offset from argument entry */
	unsigned int		count;	/* Array count */
	const char		*name;	/* Name of this argument */
	const char		*comm;	/* Command of this argument */
	char			*fmt;	/* Format string if needed */
	const struct fetch_type	*type;	/* Type of this argument */
};

struct trace_uprobe_filter {
	rwlock_t		rwlock;
	int			nr_systemwide;
	struct list_head	perf_events;
};

/* Event call and class holder */
struct trace_probe_event {
	unsigned int			flags;	/* For TP_FLAG_* */
	struct trace_event_class	class;
	struct trace_event_call		call;
	struct list_head 		files;
	struct list_head		probes;
	struct trace_uprobe_filter	filter[];
};

struct trace_probe {
	struct list_head		list;
	struct trace_probe_event	*event;
	ssize_t				size;	/* trace entry size */
	unsigned int			nr_args;
	struct probe_arg		args[];
};

struct event_file_link {
	struct trace_event_file		*file;
	struct list_head		list;
};

static inline bool trace_probe_test_flag(struct trace_probe *tp,
					 unsigned int flag)
{
	return !!(tp->event->flags & flag);
}

static inline void trace_probe_set_flag(struct trace_probe *tp,
					unsigned int flag)
{
	tp->event->flags |= flag;
}

static inline void trace_probe_clear_flag(struct trace_probe *tp,
					  unsigned int flag)
{
	tp->event->flags &= ~flag;
}

static inline bool trace_probe_is_enabled(struct trace_probe *tp)
{
	return trace_probe_test_flag(tp, TP_FLAG_TRACE | TP_FLAG_PROFILE);
}

static inline const char *trace_probe_name(struct trace_probe *tp)
{
	return trace_event_name(&tp->event->call);
}

static inline const char *trace_probe_group_name(struct trace_probe *tp)
{
	return tp->event->call.class->system;
}

static inline struct trace_event_call *
	trace_probe_event_call(struct trace_probe *tp)
{
	return &tp->event->call;
}

static inline struct trace_probe_event *
trace_probe_event_from_call(struct trace_event_call *event_call)
{
	return container_of(event_call, struct trace_probe_event, call);
}

static inline struct trace_probe *
trace_probe_primary_from_call(struct trace_event_call *call)
{
	struct trace_probe_event *tpe = trace_probe_event_from_call(call);

	return list_first_entry(&tpe->probes, struct trace_probe, list);
}

static inline struct list_head *trace_probe_probe_list(struct trace_probe *tp)
{
	return &tp->event->probes;
}

static inline bool trace_probe_has_sibling(struct trace_probe *tp)
{
	struct list_head *list = trace_probe_probe_list(tp);

	return !list_empty(list) && !list_is_singular(list);
}

static inline int trace_probe_unregister_event_call(struct trace_probe *tp)
{
	/* tp->event is unregistered in trace_remove_event_call() */
	return trace_remove_event_call(&tp->event->call);
}

static inline bool trace_probe_has_single_file(struct trace_probe *tp)
{
	return !!list_is_singular(&tp->event->files);
}

int trace_probe_init(struct trace_probe *tp, const char *event,
		     const char *group, bool alloc_filter);
void trace_probe_cleanup(struct trace_probe *tp);
int trace_probe_append(struct trace_probe *tp, struct trace_probe *to);
void trace_probe_unlink(struct trace_probe *tp);
int trace_probe_register_event_call(struct trace_probe *tp);
int trace_probe_add_file(struct trace_probe *tp, struct trace_event_file *file);
int trace_probe_remove_file(struct trace_probe *tp,
			    struct trace_event_file *file);
struct event_file_link *trace_probe_get_file_link(struct trace_probe *tp,
						struct trace_event_file *file);
int trace_probe_compare_arg_type(struct trace_probe *a, struct trace_probe *b);
bool trace_probe_match_command_args(struct trace_probe *tp,
				    int argc, const char **argv);
int trace_probe_create(const char *raw_command, int (*createfn)(int, const char **));

#define trace_probe_for_each_link(pos, tp)	\
	list_for_each_entry(pos, &(tp)->event->files, list)
#define trace_probe_for_each_link_rcu(pos, tp)	\
	list_for_each_entry_rcu(pos, &(tp)->event->files, list)

#define TPARG_FL_RETURN BIT(0)
#define TPARG_FL_KERNEL BIT(1)
#define TPARG_FL_FENTRY BIT(2)
#define TPARG_FL_TPOINT BIT(3)
#define TPARG_FL_MASK	GENMASK(3, 0)

extern int traceprobe_parse_probe_arg(struct trace_probe *tp, int i,
				const char *argv, unsigned int flags);

extern int traceprobe_update_arg(struct probe_arg *arg);
extern void traceprobe_free_probe_arg(struct probe_arg *arg);

extern int traceprobe_split_symbol_offset(char *symbol, long *offset);
int traceprobe_parse_event_name(const char **pevent, const char **pgroup,
				char *buf, int offset);

enum probe_print_type {
	PROBE_PRINT_NORMAL,
	PROBE_PRINT_RETURN,
	PROBE_PRINT_EVENT,
};

extern int traceprobe_set_print_fmt(struct trace_probe *tp, enum probe_print_type ptype);

#ifdef CONFIG_PERF_EVENTS
extern struct trace_event_call *
create_local_trace_kprobe(char *func, void *addr, unsigned long offs,
			  bool is_return);
extern void destroy_local_trace_kprobe(struct trace_event_call *event_call);

extern struct trace_event_call *
create_local_trace_uprobe(char *name, unsigned long offs,
			  unsigned long ref_ctr_offset, bool is_return);
extern void destroy_local_trace_uprobe(struct trace_event_call *event_call);
#endif
extern int traceprobe_define_arg_fields(struct trace_event_call *event_call,
					size_t offset, struct trace_probe *tp);

#undef ERRORS
#define ERRORS	\
	C(FILE_NOT_FOUND,	"Failed to find the given file"),	\
	C(NO_REGULAR_FILE,	"Not a regular file"),			\
	C(BAD_REFCNT,		"Invalid reference counter offset"),	\
	C(REFCNT_OPEN_BRACE,	"Reference counter brace is not closed"), \
	C(BAD_REFCNT_SUFFIX,	"Reference counter has wrong suffix"),	\
	C(BAD_UPROBE_OFFS,	"Invalid uprobe offset"),		\
	C(MAXACT_NO_KPROBE,	"Maxactive is not for kprobe"),		\
	C(BAD_MAXACT,		"Invalid maxactive number"),		\
	C(MAXACT_TOO_BIG,	"Maxactive is too big"),		\
	C(BAD_PROBE_ADDR,	"Invalid probed address or symbol"),	\
	C(BAD_RETPROBE,		"Retprobe address must be an function entry"), \
	C(BAD_ADDR_SUFFIX,	"Invalid probed address suffix"), \
	C(NO_GROUP_NAME,	"Group name is not specified"),		\
	C(GROUP_TOO_LONG,	"Group name is too long"),		\
	C(BAD_GROUP_NAME,	"Group name must follow the same rules as C identifiers"), \
	C(NO_EVENT_NAME,	"Event name is not specified"),		\
	C(EVENT_TOO_LONG,	"Event name is too long"),		\
	C(BAD_EVENT_NAME,	"Event name must follow the same rules as C identifiers"), \
	C(EVENT_EXIST,		"Given group/event name is already used by another event"), \
	C(RETVAL_ON_PROBE,	"$retval is not available on probe"),	\
	C(BAD_STACK_NUM,	"Invalid stack number"),		\
	C(BAD_ARG_NUM,		"Invalid argument number"),		\
	C(BAD_VAR,		"Invalid $-valiable specified"),	\
	C(BAD_REG_NAME,		"Invalid register name"),		\
	C(BAD_MEM_ADDR,		"Invalid memory address"),		\
	C(BAD_IMM,		"Invalid immediate value"),		\
	C(IMMSTR_NO_CLOSE,	"String is not closed with '\"'"),	\
	C(FILE_ON_KPROBE,	"File offset is not available with kprobe"), \
	C(BAD_FILE_OFFS,	"Invalid file offset value"),		\
	C(SYM_ON_UPROBE,	"Symbol is not available with uprobe"),	\
	C(TOO_MANY_OPS,		"Dereference is too much nested"), 	\
	C(DEREF_NEED_BRACE,	"Dereference needs a brace"),		\
	C(BAD_DEREF_OFFS,	"Invalid dereference offset"),		\
	C(DEREF_OPEN_BRACE,	"Dereference brace is not closed"),	\
	C(COMM_CANT_DEREF,	"$comm can not be dereferenced"),	\
	C(BAD_FETCH_ARG,	"Invalid fetch argument"),		\
	C(ARRAY_NO_CLOSE,	"Array is not closed"),			\
	C(BAD_ARRAY_SUFFIX,	"Array has wrong suffix"),		\
	C(BAD_ARRAY_NUM,	"Invalid array size"),			\
	C(ARRAY_TOO_BIG,	"Array number is too big"),		\
	C(BAD_TYPE,		"Unknown type is specified"),		\
	C(BAD_STRING,		"String accepts only memory argument"),	\
	C(BAD_BITFIELD,		"Invalid bitfield"),			\
	C(ARG_NAME_TOO_LONG,	"Argument name is too long"),		\
	C(NO_ARG_NAME,		"Argument name is not specified"),	\
	C(BAD_ARG_NAME,		"Argument name must follow the same rules as C identifiers"), \
	C(USED_ARG_NAME,	"This argument name is already used"),	\
	C(ARG_TOO_LONG,		"Argument expression is too long"),	\
	C(NO_ARG_BODY,		"No argument expression"),		\
	C(BAD_INSN_BNDRY,	"Probe point is not an instruction boundary"),\
	C(FAIL_REG_PROBE,	"Failed to register probe event"),\
	C(DIFF_PROBE_TYPE,	"Probe type is different from existing probe"),\
	C(DIFF_ARG_TYPE,	"Argument type or name is different from existing probe"),\
	C(SAME_PROBE,		"There is already the exact same probe event"),

#undef C
#define C(a, b)		TP_ERR_##a

/* Define TP_ERR_ */
enum { ERRORS };

/* Error text is defined in trace_probe.c */

struct trace_probe_log {
	const char	*subsystem;
	const char	**argv;
	int		argc;
	int		index;
};

void trace_probe_log_init(const char *subsystem, int argc, const char **argv);
void trace_probe_log_set_index(int index);
void trace_probe_log_clear(void);
void __trace_probe_log_err(int offset, int err);

#define trace_probe_log_err(offs, err)	\
	__trace_probe_log_err(offs, TP_ERR_##err)
