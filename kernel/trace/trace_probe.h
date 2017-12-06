/*
 * Common header file for probe-based Dynamic events.
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
#include <asm/bitsperlong.h>

#include "trace.h"
#include "trace_output.h"

#define MAX_TRACE_ARGS		128
#define MAX_ARGSTR_LEN		63
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


/* data_rloc: data relative location, compatible with u32 */
#define make_data_rloc(len, roffs)	\
	(((u32)(len) << 16) | ((u32)(roffs) & 0xffff))
#define get_rloc_len(dl)		((u32)(dl) >> 16)
#define get_rloc_offs(dl)		((u32)(dl) & 0xffff)

/*
 * Convert data_rloc to data_loc:
 *  data_rloc stores the offset from data_rloc itself, but data_loc
 *  stores the offset from event entry.
 */
#define convert_rloc_to_loc(dl, offs)	((u32)(dl) + (offs))

static nokprobe_inline void *get_rloc_data(u32 *dl)
{
	return (u8 *)dl + get_rloc_offs(*dl);
}

/* For data_loc conversion */
static nokprobe_inline void *get_loc_data(u32 *dl, void *ent)
{
	return (u8 *)ent + get_rloc_offs(*dl);
}

/* Data fetch function type */
typedef	void (*fetch_func_t)(struct pt_regs *, void *, void *);
/* Printing function type */
typedef int (*print_type_func_t)(struct trace_seq *, const char *, void *, void *);

/* Fetch types */
enum {
	FETCH_MTD_reg = 0,
	FETCH_MTD_stack,
	FETCH_MTD_retval,
	FETCH_MTD_comm,
	FETCH_MTD_memory,
	FETCH_MTD_symbol,
	FETCH_MTD_deref,
	FETCH_MTD_bitfield,
	FETCH_MTD_file_offset,
	FETCH_MTD_END,
};

/* Fetch type information table */
struct fetch_type {
	const char		*name;		/* Name of type */
	size_t			size;		/* Byte size of type */
	int			is_signed;	/* Signed flag */
	print_type_func_t	print;		/* Print functions */
	const char		*fmt;		/* Fromat string */
	const char		*fmttype;	/* Name in format file */
	/* Fetch functions */
	fetch_func_t		fetch[FETCH_MTD_END];
};

struct fetch_param {
	fetch_func_t		fn;
	void 			*data;
};

/* For defining macros, define string/string_size types */
typedef u32 string;
typedef u32 string_size;

#define PRINT_TYPE_FUNC_NAME(type)	print_type_##type
#define PRINT_TYPE_FMT_NAME(type)	print_type_format_##type

/* Printing  in basic type function template */
#define DECLARE_BASIC_PRINT_TYPE_FUNC(type)				\
int PRINT_TYPE_FUNC_NAME(type)(struct trace_seq *s, const char *name,	\
				void *data, void *ent);			\
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

#define FETCH_FUNC_NAME(method, type)	fetch_##method##_##type

/* Declare macro for basic types */
#define DECLARE_FETCH_FUNC(method, type)				\
extern void FETCH_FUNC_NAME(method, type)(struct pt_regs *regs, 	\
					  void *data, void *dest)

#define DECLARE_BASIC_FETCH_FUNCS(method) 	\
DECLARE_FETCH_FUNC(method, u8);			\
DECLARE_FETCH_FUNC(method, u16);		\
DECLARE_FETCH_FUNC(method, u32);		\
DECLARE_FETCH_FUNC(method, u64)

DECLARE_BASIC_FETCH_FUNCS(reg);
#define fetch_reg_string			NULL
#define fetch_reg_string_size			NULL

DECLARE_BASIC_FETCH_FUNCS(retval);
#define fetch_retval_string			NULL
#define fetch_retval_string_size		NULL

DECLARE_BASIC_FETCH_FUNCS(symbol);
DECLARE_FETCH_FUNC(symbol, string);
DECLARE_FETCH_FUNC(symbol, string_size);

DECLARE_BASIC_FETCH_FUNCS(deref);
DECLARE_FETCH_FUNC(deref, string);
DECLARE_FETCH_FUNC(deref, string_size);

DECLARE_BASIC_FETCH_FUNCS(bitfield);
#define fetch_bitfield_string			NULL
#define fetch_bitfield_string_size		NULL

/* comm only makes sense as a string */
#define fetch_comm_u8		NULL
#define fetch_comm_u16		NULL
#define fetch_comm_u32		NULL
#define fetch_comm_u64		NULL
DECLARE_FETCH_FUNC(comm, string);
DECLARE_FETCH_FUNC(comm, string_size);

/*
 * Define macro for basic types - we don't need to define s* types, because
 * we have to care only about bitwidth at recording time.
 */
#define DEFINE_BASIC_FETCH_FUNCS(method) \
DEFINE_FETCH_##method(u8)		\
DEFINE_FETCH_##method(u16)		\
DEFINE_FETCH_##method(u32)		\
DEFINE_FETCH_##method(u64)

/* Default (unsigned long) fetch type */
#define __DEFAULT_FETCH_TYPE(t) x##t
#define _DEFAULT_FETCH_TYPE(t) __DEFAULT_FETCH_TYPE(t)
#define DEFAULT_FETCH_TYPE _DEFAULT_FETCH_TYPE(BITS_PER_LONG)
#define DEFAULT_FETCH_TYPE_STR __stringify(DEFAULT_FETCH_TYPE)

#define ASSIGN_FETCH_FUNC(method, type)	\
	[FETCH_MTD_##method] = FETCH_FUNC_NAME(method, type)

#define __ASSIGN_FETCH_TYPE(_name, ptype, ftype, _size, sign, _fmttype)	\
	{.name = _name,				\
	 .size = _size,					\
	 .is_signed = sign,				\
	 .print = PRINT_TYPE_FUNC_NAME(ptype),		\
	 .fmt = PRINT_TYPE_FMT_NAME(ptype),		\
	 .fmttype = _fmttype,				\
	 .fetch = {					\
ASSIGN_FETCH_FUNC(reg, ftype),				\
ASSIGN_FETCH_FUNC(stack, ftype),			\
ASSIGN_FETCH_FUNC(retval, ftype),			\
ASSIGN_FETCH_FUNC(comm, ftype),				\
ASSIGN_FETCH_FUNC(memory, ftype),			\
ASSIGN_FETCH_FUNC(symbol, ftype),			\
ASSIGN_FETCH_FUNC(deref, ftype),			\
ASSIGN_FETCH_FUNC(bitfield, ftype),			\
ASSIGN_FETCH_FUNC(file_offset, ftype),			\
	  }						\
	}

#define ASSIGN_FETCH_TYPE(ptype, ftype, sign)			\
	__ASSIGN_FETCH_TYPE(#ptype, ptype, ftype, sizeof(ftype), sign, #ptype)

/* If ptype is an alias of atype, use this macro (show atype in format) */
#define ASSIGN_FETCH_TYPE_ALIAS(ptype, atype, ftype, sign)		\
	__ASSIGN_FETCH_TYPE(#ptype, ptype, ftype, sizeof(ftype), sign, #atype)

#define ASSIGN_FETCH_TYPE_END {}

#define FETCH_TYPE_STRING	0
#define FETCH_TYPE_STRSIZE	1

#ifdef CONFIG_KPROBE_EVENTS
struct symbol_cache;
unsigned long update_symbol_cache(struct symbol_cache *sc);
void free_symbol_cache(struct symbol_cache *sc);
struct symbol_cache *alloc_symbol_cache(const char *sym, long offset);
#else
/* uprobes do not support symbol fetch methods */
#define fetch_symbol_u8			NULL
#define fetch_symbol_u16		NULL
#define fetch_symbol_u32		NULL
#define fetch_symbol_u64		NULL
#define fetch_symbol_string		NULL
#define fetch_symbol_string_size	NULL

struct symbol_cache {
};
static inline unsigned long __used update_symbol_cache(struct symbol_cache *sc)
{
	return 0;
}

static inline void __used free_symbol_cache(struct symbol_cache *sc)
{
}

static inline struct symbol_cache * __used
alloc_symbol_cache(const char *sym, long offset)
{
	return NULL;
}
#endif /* CONFIG_KPROBE_EVENTS */

struct probe_arg {
	struct fetch_param	fetch;
	struct fetch_param	fetch_size;
	unsigned int		offset;	/* Offset from argument entry */
	const char		*name;	/* Name of this argument */
	const char		*comm;	/* Command of this argument */
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

static nokprobe_inline void call_fetch(struct fetch_param *fprm,
				 struct pt_regs *regs, void *dest)
{
	return fprm->fn(regs, fprm->data, dest);
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

extern int traceprobe_parse_probe_arg(char *arg, ssize_t *size,
		   struct probe_arg *parg, bool is_return, bool is_kprobe,
		   const struct fetch_type *ftbl);

extern int traceprobe_conflict_field_name(const char *name,
			       struct probe_arg *args, int narg);

extern void traceprobe_update_arg(struct probe_arg *arg);
extern void traceprobe_free_probe_arg(struct probe_arg *arg);

extern int traceprobe_split_symbol_offset(char *symbol, unsigned long *offset);

/* Sum up total data length for dynamic arraies (strings) */
static nokprobe_inline int
__get_data_size(struct trace_probe *tp, struct pt_regs *regs)
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
static nokprobe_inline void
store_trace_args(int ent_size, struct trace_probe *tp, struct pt_regs *regs,
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

extern int set_print_fmt(struct trace_probe *tp, bool is_return);

#ifdef CONFIG_PERF_EVENTS
extern struct trace_event_call *
create_local_trace_kprobe(char *func, void *addr, unsigned long offs,
			  bool is_return);
extern void destroy_local_trace_kprobe(struct trace_event_call *event_call);

extern struct trace_event_call *
create_local_trace_uprobe(char *name, unsigned long offs, bool is_return);
extern void destroy_local_trace_uprobe(struct trace_event_call *event_call);
#endif
