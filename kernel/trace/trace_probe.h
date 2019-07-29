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
#include <linux/ctype.h>
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
#define MAX_STRING_SIZE		PATH_MAX

/* Reserved field names */
#define FIELD_STRING_IP		"__probe_ip"
#define FIELD_STRING_RETIP	"__probe_ret_ip"
#define FIELD_STRING_FUNC	"__probe_func"

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
#define TP_FLAG_REGISTERED	4

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
	// Stage 2 (dereference) op
	FETCH_OP_DEREF,		/* Dereference: .offset */
	// Stage 3 (store) ops
	FETCH_OP_ST_RAW,	/* Raw: .size */
	FETCH_OP_ST_MEM,	/* Mem: .offset, .size */
	FETCH_OP_ST_STRING,	/* String: .offset, .size */
	// Stage 4 (modify) op
	FETCH_OP_MOD_BF,	/* Bitfield: .basesize, .lshift, .rshift */
	// Stage 5 (loop) op
	FETCH_OP_LP_ARRAY,	/* Array: .param = loop count */
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

/* Fetch type information table */
struct fetch_type {
	const char		*name;		/* Name of type */
	size_t			size;		/* Byte size of type */
	int			is_signed;	/* Signed flag */
	print_type_func_t	print;		/* Print functions */
	const char		*fmt;		/* Fromat string */
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

struct trace_probe {
	unsigned int			flags;	/* For TP_FLAG_* */
	struct trace_event_class	class;
	struct trace_event_call		call;
	struct list_head 		files;
	ssize_t				size;	/* trace entry size */
	unsigned int			nr_args;
	struct probe_arg		args[];
};

struct event_file_link {
	struct trace_event_file		*file;
	struct list_head		list;
};

static inline bool trace_probe_is_enabled(struct trace_probe *tp)
{
	return !!(tp->flags & (TP_FLAG_TRACE | TP_FLAG_PROFILE));
}

static inline bool trace_probe_is_registered(struct trace_probe *tp)
{
	return !!(tp->flags & TP_FLAG_REGISTERED);
}

/* Check the name is good for event/group/fields */
static inline bool is_good_name(const char *name)
{
	if (!isalpha(*name) && *name != '_')
		return false;
	while (*++name != '\0') {
		if (!isalpha(*name) && !isdigit(*name) && *name != '_')
			return false;
	}
	return true;
}

static inline struct event_file_link *
find_event_file_link(struct trace_probe *tp, struct trace_event_file *file)
{
	struct event_file_link *link;

	list_for_each_entry(link, &tp->files, list)
		if (link->file == file)
			return link;

	return NULL;
}

#define TPARG_FL_RETURN BIT(0)
#define TPARG_FL_KERNEL BIT(1)
#define TPARG_FL_FENTRY BIT(2)
#define TPARG_FL_MASK	GENMASK(2, 0)

extern int traceprobe_parse_probe_arg(struct trace_probe *tp, int i,
				char *arg, unsigned int flags);

extern int traceprobe_update_arg(struct probe_arg *arg);
extern void traceprobe_free_probe_arg(struct probe_arg *arg);

extern int traceprobe_split_symbol_offset(char *symbol, long *offset);
extern int traceprobe_parse_event_name(const char **pevent,
				       const char **pgroup, char *buf);

extern int traceprobe_set_print_fmt(struct trace_probe *tp, bool is_return);

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
