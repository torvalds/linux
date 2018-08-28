/*
 * Copyright (C) 2009, 2010 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License (not later!)
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not,  see <http://www.gnu.org/licenses>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#ifndef _PARSE_EVENTS_H
#define _PARSE_EVENTS_H

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <regex.h>
#include <string.h>

#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

/* ----------------------- trace_seq ----------------------- */


#ifndef TRACE_SEQ_BUF_SIZE
#define TRACE_SEQ_BUF_SIZE 4096
#endif

#ifndef DEBUG_RECORD
#define DEBUG_RECORD 0
#endif

struct tep_record {
	unsigned long long	ts;
	unsigned long long	offset;
	long long		missed_events;	/* buffer dropped events before */
	int			record_size;	/* size of binary record */
	int			size;		/* size of data */
	void			*data;
	int			cpu;
	int			ref_count;
	int			locked;		/* Do not free, even if ref_count is zero */
	void			*priv;
#if DEBUG_RECORD
	struct tep_record	*prev;
	struct tep_record	*next;
	long			alloc_addr;
#endif
};

enum trace_seq_fail {
	TRACE_SEQ__GOOD,
	TRACE_SEQ__BUFFER_POISONED,
	TRACE_SEQ__MEM_ALLOC_FAILED,
};

/*
 * Trace sequences are used to allow a function to call several other functions
 * to create a string of data to use (up to a max of PAGE_SIZE).
 */

struct trace_seq {
	char			*buffer;
	unsigned int		buffer_size;
	unsigned int		len;
	unsigned int		readpos;
	enum trace_seq_fail	state;
};

void trace_seq_init(struct trace_seq *s);
void trace_seq_reset(struct trace_seq *s);
void trace_seq_destroy(struct trace_seq *s);

extern int trace_seq_printf(struct trace_seq *s, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));
extern int trace_seq_vprintf(struct trace_seq *s, const char *fmt, va_list args)
	__attribute__ ((format (printf, 2, 0)));

extern int trace_seq_puts(struct trace_seq *s, const char *str);
extern int trace_seq_putc(struct trace_seq *s, unsigned char c);

extern void trace_seq_terminate(struct trace_seq *s);

extern int trace_seq_do_fprintf(struct trace_seq *s, FILE *fp);
extern int trace_seq_do_printf(struct trace_seq *s);


/* ----------------------- pevent ----------------------- */

struct tep_handle;
struct event_format;

typedef int (*tep_event_handler_func)(struct trace_seq *s,
				      struct tep_record *record,
				      struct event_format *event,
				      void *context);

typedef int (*tep_plugin_load_func)(struct tep_handle *pevent);
typedef int (*tep_plugin_unload_func)(struct tep_handle *pevent);

struct tep_plugin_option {
	struct tep_plugin_option	*next;
	void				*handle;
	char				*file;
	char				*name;
	char				*plugin_alias;
	char				*description;
	const char			*value;
	void				*priv;
	int				set;
};

/*
 * Plugin hooks that can be called:
 *
 * TEP_PLUGIN_LOADER:  (required)
 *   The function name to initialized the plugin.
 *
 *   int TEP_PLUGIN_LOADER(struct tep_handle *pevent)
 *
 * TEP_PLUGIN_UNLOADER:  (optional)
 *   The function called just before unloading
 *
 *   int TEP_PLUGIN_UNLOADER(struct tep_handle *pevent)
 *
 * TEP_PLUGIN_OPTIONS:  (optional)
 *   Plugin options that can be set before loading
 *
 *   struct tep_plugin_option TEP_PLUGIN_OPTIONS[] = {
 *	{
 *		.name = "option-name",
 *		.plugin_alias = "override-file-name", (optional)
 *		.description = "description of option to show users",
 *	},
 *	{
 *		.name = NULL,
 *	},
 *   };
 *
 *   Array must end with .name = NULL;
 *
 *
 *   .plugin_alias is used to give a shorter name to access
 *   the vairable. Useful if a plugin handles more than one event.
 *
 *   If .value is not set, then it is considered a boolean and only
 *   .set will be processed. If .value is defined, then it is considered
 *   a string option and .set will be ignored.
 *
 * TEP_PLUGIN_ALIAS: (optional)
 *   The name to use for finding options (uses filename if not defined)
 */
#define TEP_PLUGIN_LOADER tep_plugin_loader
#define TEP_PLUGIN_UNLOADER tep_plugin_unloader
#define TEP_PLUGIN_OPTIONS tep_plugin_options
#define TEP_PLUGIN_ALIAS tep_plugin_alias
#define _MAKE_STR(x)	#x
#define MAKE_STR(x)	_MAKE_STR(x)
#define TEP_PLUGIN_LOADER_NAME MAKE_STR(TEP_PLUGIN_LOADER)
#define TEP_PLUGIN_UNLOADER_NAME MAKE_STR(TEP_PLUGIN_UNLOADER)
#define TEP_PLUGIN_OPTIONS_NAME MAKE_STR(TEP_PLUGIN_OPTIONS)
#define TEP_PLUGIN_ALIAS_NAME MAKE_STR(TEP_PLUGIN_ALIAS)

enum format_flags {
	FIELD_IS_ARRAY		= 1,
	FIELD_IS_POINTER	= 2,
	FIELD_IS_SIGNED		= 4,
	FIELD_IS_STRING		= 8,
	FIELD_IS_DYNAMIC	= 16,
	FIELD_IS_LONG		= 32,
	FIELD_IS_FLAG		= 64,
	FIELD_IS_SYMBOLIC	= 128,
};

struct format_field {
	struct format_field	*next;
	struct event_format	*event;
	char			*type;
	char			*name;
	char			*alias;
	int			offset;
	int			size;
	unsigned int		arraylen;
	unsigned int		elementsize;
	unsigned long		flags;
};

struct format {
	int			nr_common;
	int			nr_fields;
	struct format_field	*common_fields;
	struct format_field	*fields;
};

struct print_arg_atom {
	char			*atom;
};

struct print_arg_string {
	char			*string;
	int			offset;
};

struct print_arg_bitmask {
	char			*bitmask;
	int			offset;
};

struct print_arg_field {
	char			*name;
	struct format_field	*field;
};

struct print_flag_sym {
	struct print_flag_sym	*next;
	char			*value;
	char			*str;
};

struct print_arg_typecast {
	char 			*type;
	struct print_arg	*item;
};

struct print_arg_flags {
	struct print_arg	*field;
	char			*delim;
	struct print_flag_sym	*flags;
};

struct print_arg_symbol {
	struct print_arg	*field;
	struct print_flag_sym	*symbols;
};

struct print_arg_hex {
	struct print_arg	*field;
	struct print_arg	*size;
};

struct print_arg_int_array {
	struct print_arg	*field;
	struct print_arg	*count;
	struct print_arg	*el_size;
};

struct print_arg_dynarray {
	struct format_field	*field;
	struct print_arg	*index;
};

struct print_arg;

struct print_arg_op {
	char			*op;
	int			prio;
	struct print_arg	*left;
	struct print_arg	*right;
};

struct tep_function_handler;

struct print_arg_func {
	struct tep_function_handler	*func;
	struct print_arg		*args;
};

enum print_arg_type {
	PRINT_NULL,
	PRINT_ATOM,
	PRINT_FIELD,
	PRINT_FLAGS,
	PRINT_SYMBOL,
	PRINT_HEX,
	PRINT_INT_ARRAY,
	PRINT_TYPE,
	PRINT_STRING,
	PRINT_BSTRING,
	PRINT_DYNAMIC_ARRAY,
	PRINT_OP,
	PRINT_FUNC,
	PRINT_BITMASK,
	PRINT_DYNAMIC_ARRAY_LEN,
	PRINT_HEX_STR,
};

struct print_arg {
	struct print_arg		*next;
	enum print_arg_type		type;
	union {
		struct print_arg_atom		atom;
		struct print_arg_field		field;
		struct print_arg_typecast	typecast;
		struct print_arg_flags		flags;
		struct print_arg_symbol		symbol;
		struct print_arg_hex		hex;
		struct print_arg_int_array	int_array;
		struct print_arg_func		func;
		struct print_arg_string		string;
		struct print_arg_bitmask	bitmask;
		struct print_arg_op		op;
		struct print_arg_dynarray	dynarray;
	};
};

struct print_fmt {
	char			*format;
	struct print_arg	*args;
};

struct event_format {
	struct tep_handle	*pevent;
	char			*name;
	int			id;
	int			flags;
	struct format		format;
	struct print_fmt	print_fmt;
	char			*system;
	tep_event_handler_func	handler;
	void			*context;
};

enum {
	EVENT_FL_ISFTRACE	= 0x01,
	EVENT_FL_ISPRINT	= 0x02,
	EVENT_FL_ISBPRINT	= 0x04,
	EVENT_FL_ISFUNCENT	= 0x10,
	EVENT_FL_ISFUNCRET	= 0x20,
	EVENT_FL_NOHANDLE	= 0x40,
	EVENT_FL_PRINTRAW	= 0x80,

	EVENT_FL_FAILED		= 0x80000000
};

enum event_sort_type {
	EVENT_SORT_ID,
	EVENT_SORT_NAME,
	EVENT_SORT_SYSTEM,
};

enum event_type {
	EVENT_ERROR,
	EVENT_NONE,
	EVENT_SPACE,
	EVENT_NEWLINE,
	EVENT_OP,
	EVENT_DELIM,
	EVENT_ITEM,
	EVENT_DQUOTE,
	EVENT_SQUOTE,
};

typedef unsigned long long (*tep_func_handler)(struct trace_seq *s,
					       unsigned long long *args);

enum tep_func_arg_type {
	TEP_FUNC_ARG_VOID,
	TEP_FUNC_ARG_INT,
	TEP_FUNC_ARG_LONG,
	TEP_FUNC_ARG_STRING,
	TEP_FUNC_ARG_PTR,
	TEP_FUNC_ARG_MAX_TYPES
};

enum tep_flag {
	TEP_NSEC_OUTPUT		= 1,	/* output in NSECS */
	TEP_DISABLE_SYS_PLUGINS	= 1 << 1,
	TEP_DISABLE_PLUGINS	= 1 << 2,
};

#define TEP_ERRORS 							      \
	_PE(MEM_ALLOC_FAILED,	"failed to allocate memory"),		      \
	_PE(PARSE_EVENT_FAILED,	"failed to parse event"),		      \
	_PE(READ_ID_FAILED,	"failed to read event id"),		      \
	_PE(READ_FORMAT_FAILED,	"failed to read event format"),		      \
	_PE(READ_PRINT_FAILED,	"failed to read event print fmt"), 	      \
	_PE(OLD_FTRACE_ARG_FAILED,"failed to allocate field name for ftrace"),\
	_PE(INVALID_ARG_TYPE,	"invalid argument type"),		      \
	_PE(INVALID_EXP_TYPE,	"invalid expression type"),		      \
	_PE(INVALID_OP_TYPE,	"invalid operator type"),		      \
	_PE(INVALID_EVENT_NAME,	"invalid event name"),			      \
	_PE(EVENT_NOT_FOUND,	"no event found"),			      \
	_PE(SYNTAX_ERROR,	"syntax error"),			      \
	_PE(ILLEGAL_RVALUE,	"illegal rvalue"),			      \
	_PE(ILLEGAL_LVALUE,	"illegal lvalue for string comparison"),      \
	_PE(INVALID_REGEX,	"regex did not compute"),		      \
	_PE(ILLEGAL_STRING_CMP,	"illegal comparison for string"), 	      \
	_PE(ILLEGAL_INTEGER_CMP,"illegal comparison for integer"), 	      \
	_PE(REPARENT_NOT_OP,	"cannot reparent other than OP"),	      \
	_PE(REPARENT_FAILED,	"failed to reparent filter OP"),	      \
	_PE(BAD_FILTER_ARG,	"bad arg in filter tree"),		      \
	_PE(UNEXPECTED_TYPE,	"unexpected type (not a value)"),	      \
	_PE(ILLEGAL_TOKEN,	"illegal token"),			      \
	_PE(INVALID_PAREN,	"open parenthesis cannot come here"), 	      \
	_PE(UNBALANCED_PAREN,	"unbalanced number of parenthesis"),	      \
	_PE(UNKNOWN_TOKEN,	"unknown token"),			      \
	_PE(FILTER_NOT_FOUND,	"no filter found"),			      \
	_PE(NOT_A_NUMBER,	"must have number field"),		      \
	_PE(NO_FILTER,		"no filters exists"),			      \
	_PE(FILTER_MISS,	"record does not match to filter")

#undef _PE
#define _PE(__code, __str) TEP_ERRNO__ ## __code
enum tep_errno {
	TEP_ERRNO__SUCCESS			= 0,
	TEP_ERRNO__FILTER_MATCH			= TEP_ERRNO__SUCCESS,

	/*
	 * Choose an arbitrary negative big number not to clash with standard
	 * errno since SUS requires the errno has distinct positive values.
	 * See 'Issue 6' in the link below.
	 *
	 * http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/errno.h.html
	 */
	__TEP_ERRNO__START			= -100000,

	TEP_ERRORS,

	__TEP_ERRNO__END,
};
#undef _PE

struct plugin_list;

#define INVALID_PLUGIN_LIST_OPTION	((char **)((unsigned long)-1))

struct plugin_list *tep_load_plugins(struct tep_handle *pevent);
void tep_unload_plugins(struct plugin_list *plugin_list,
			struct tep_handle *pevent);
char **tep_plugin_list_options(void);
void tep_plugin_free_options_list(char **list);
int tep_plugin_add_options(const char *name,
			   struct tep_plugin_option *options);
void tep_plugin_remove_options(struct tep_plugin_option *options);
void tep_print_plugins(struct trace_seq *s,
			const char *prefix, const char *suffix,
			const struct plugin_list *list);

struct cmdline;
struct cmdline_list;
struct func_map;
struct func_list;
struct event_handler;
struct func_resolver;

typedef char *(tep_func_resolver_t)(void *priv,
				    unsigned long long *addrp, char **modp);

struct tep_handle {
	int ref_count;

	int header_page_ts_offset;
	int header_page_ts_size;
	int header_page_size_offset;
	int header_page_size_size;
	int header_page_data_offset;
	int header_page_data_size;
	int header_page_overwrite;

	int file_bigendian;
	int host_bigendian;

	int latency_format;

	int old_format;

	int cpus;
	int long_size;
	int page_size;

	struct cmdline *cmdlines;
	struct cmdline_list *cmdlist;
	int cmdline_count;

	struct func_map *func_map;
	struct func_resolver *func_resolver;
	struct func_list *funclist;
	unsigned int func_count;

	struct printk_map *printk_map;
	struct printk_list *printklist;
	unsigned int printk_count;


	struct event_format **events;
	int nr_events;
	struct event_format **sort_events;
	enum event_sort_type last_type;

	int type_offset;
	int type_size;

	int pid_offset;
	int pid_size;

 	int pc_offset;
	int pc_size;

	int flags_offset;
	int flags_size;

	int ld_offset;
	int ld_size;

	int print_raw;

	int test_filters;

	int flags;

	struct format_field *bprint_ip_field;
	struct format_field *bprint_fmt_field;
	struct format_field *bprint_buf_field;

	struct event_handler *handlers;
	struct tep_function_handler *func_handlers;

	/* cache */
	struct event_format *last_event;

	char *trace_clock;
};

static inline void tep_set_flag(struct tep_handle *pevent, int flag)
{
	pevent->flags |= flag;
}

static inline unsigned short
__data2host2(struct tep_handle *pevent, unsigned short data)
{
	unsigned short swap;

	if (pevent->host_bigendian == pevent->file_bigendian)
		return data;

	swap = ((data & 0xffULL) << 8) |
		((data & (0xffULL << 8)) >> 8);

	return swap;
}

static inline unsigned int
__data2host4(struct tep_handle *pevent, unsigned int data)
{
	unsigned int swap;

	if (pevent->host_bigendian == pevent->file_bigendian)
		return data;

	swap = ((data & 0xffULL) << 24) |
		((data & (0xffULL << 8)) << 8) |
		((data & (0xffULL << 16)) >> 8) |
		((data & (0xffULL << 24)) >> 24);

	return swap;
}

static inline unsigned long long
__data2host8(struct tep_handle *pevent, unsigned long long data)
{
	unsigned long long swap;

	if (pevent->host_bigendian == pevent->file_bigendian)
		return data;

	swap = ((data & 0xffULL) << 56) |
		((data & (0xffULL << 8)) << 40) |
		((data & (0xffULL << 16)) << 24) |
		((data & (0xffULL << 24)) << 8) |
		((data & (0xffULL << 32)) >> 8) |
		((data & (0xffULL << 40)) >> 24) |
		((data & (0xffULL << 48)) >> 40) |
		((data & (0xffULL << 56)) >> 56);

	return swap;
}

#define data2host2(pevent, ptr)		__data2host2(pevent, *(unsigned short *)(ptr))
#define data2host4(pevent, ptr)		__data2host4(pevent, *(unsigned int *)(ptr))
#define data2host8(pevent, ptr)					\
({								\
	unsigned long long __val;				\
								\
	memcpy(&__val, (ptr), sizeof(unsigned long long));	\
	__data2host8(pevent, __val);				\
})

static inline int tep_host_bigendian(void)
{
	unsigned char str[] = { 0x1, 0x2, 0x3, 0x4 };
	unsigned int val;

	memcpy(&val, str, 4);
	return val == 0x01020304;
}

/* taken from kernel/trace/trace.h */
enum trace_flag_type {
	TRACE_FLAG_IRQS_OFF		= 0x01,
	TRACE_FLAG_IRQS_NOSUPPORT	= 0x02,
	TRACE_FLAG_NEED_RESCHED		= 0x04,
	TRACE_FLAG_HARDIRQ		= 0x08,
	TRACE_FLAG_SOFTIRQ		= 0x10,
};

int tep_set_function_resolver(struct tep_handle *pevent,
			      tep_func_resolver_t *func, void *priv);
void tep_reset_function_resolver(struct tep_handle *pevent);
int tep_register_comm(struct tep_handle *pevent, const char *comm, int pid);
int tep_register_trace_clock(struct tep_handle *pevent, const char *trace_clock);
int tep_register_function(struct tep_handle *pevent, char *name,
			  unsigned long long addr, char *mod);
int tep_register_print_string(struct tep_handle *pevent, const char *fmt,
			      unsigned long long addr);
int tep_pid_is_registered(struct tep_handle *pevent, int pid);

void tep_print_event_task(struct tep_handle *pevent, struct trace_seq *s,
			  struct event_format *event,
			  struct tep_record *record);
void tep_print_event_time(struct tep_handle *pevent, struct trace_seq *s,
			  struct event_format *event,
			  struct tep_record *record,
			  bool use_trace_clock);
void tep_print_event_data(struct tep_handle *pevent, struct trace_seq *s,
			  struct event_format *event,
			  struct tep_record *record);
void tep_print_event(struct tep_handle *pevent, struct trace_seq *s,
		     struct tep_record *record, bool use_trace_clock);

int tep_parse_header_page(struct tep_handle *pevent, char *buf, unsigned long size,
			  int long_size);

enum tep_errno tep_parse_event(struct tep_handle *pevent, const char *buf,
			       unsigned long size, const char *sys);
enum tep_errno tep_parse_format(struct tep_handle *pevent,
				struct event_format **eventp,
				const char *buf,
				unsigned long size, const char *sys);
void tep_free_format(struct event_format *event);
void tep_free_format_field(struct format_field *field);

void *tep_get_field_raw(struct trace_seq *s, struct event_format *event,
			const char *name, struct tep_record *record,
			int *len, int err);

int tep_get_field_val(struct trace_seq *s, struct event_format *event,
		      const char *name, struct tep_record *record,
		      unsigned long long *val, int err);
int tep_get_common_field_val(struct trace_seq *s, struct event_format *event,
			     const char *name, struct tep_record *record,
			     unsigned long long *val, int err);
int tep_get_any_field_val(struct trace_seq *s, struct event_format *event,
			  const char *name, struct tep_record *record,
			  unsigned long long *val, int err);

int tep_print_num_field(struct trace_seq *s, const char *fmt,
			   struct event_format *event, const char *name,
			   struct tep_record *record, int err);

int tep_print_func_field(struct trace_seq *s, const char *fmt,
			 struct event_format *event, const char *name,
			 struct tep_record *record, int err);

int tep_register_event_handler(struct tep_handle *pevent, int id,
			       const char *sys_name, const char *event_name,
			       tep_event_handler_func func, void *context);
int tep_unregister_event_handler(struct tep_handle *pevent, int id,
				 const char *sys_name, const char *event_name,
				 tep_event_handler_func func, void *context);
int tep_register_print_function(struct tep_handle *pevent,
				tep_func_handler func,
				enum tep_func_arg_type ret_type,
				char *name, ...);
int tep_unregister_print_function(struct tep_handle *pevent,
				  tep_func_handler func, char *name);

struct format_field *tep_find_common_field(struct event_format *event, const char *name);
struct format_field *tep_find_field(struct event_format *event, const char *name);
struct format_field *tep_find_any_field(struct event_format *event, const char *name);

const char *tep_find_function(struct tep_handle *pevent, unsigned long long addr);
unsigned long long
tep_find_function_address(struct tep_handle *pevent, unsigned long long addr);
unsigned long long tep_read_number(struct tep_handle *pevent, const void *ptr, int size);
int tep_read_number_field(struct format_field *field, const void *data,
			  unsigned long long *value);

struct event_format *tep_find_event(struct tep_handle *pevent, int id);

struct event_format *
tep_find_event_by_name(struct tep_handle *pevent, const char *sys, const char *name);

struct event_format *
tep_find_event_by_record(struct tep_handle *pevent, struct tep_record *record);

void tep_data_lat_fmt(struct tep_handle *pevent,
		      struct trace_seq *s, struct tep_record *record);
int tep_data_type(struct tep_handle *pevent, struct tep_record *rec);
struct event_format *tep_data_event_from_type(struct tep_handle *pevent, int type);
int tep_data_pid(struct tep_handle *pevent, struct tep_record *rec);
int tep_data_preempt_count(struct tep_handle *pevent, struct tep_record *rec);
int tep_data_flags(struct tep_handle *pevent, struct tep_record *rec);
const char *tep_data_comm_from_pid(struct tep_handle *pevent, int pid);
struct cmdline;
struct cmdline *tep_data_pid_from_comm(struct tep_handle *pevent, const char *comm,
				       struct cmdline *next);
int tep_cmdline_pid(struct tep_handle *pevent, struct cmdline *cmdline);

void tep_print_field(struct trace_seq *s, void *data,
		     struct format_field *field);
void tep_print_fields(struct trace_seq *s, void *data,
		      int size __maybe_unused, struct event_format *event);
void tep_event_info(struct trace_seq *s, struct event_format *event,
		       struct tep_record *record);
int tep_strerror(struct tep_handle *pevent, enum tep_errno errnum,
		    char *buf, size_t buflen);

struct event_format **tep_list_events(struct tep_handle *pevent, enum event_sort_type);
struct format_field **tep_event_common_fields(struct event_format *event);
struct format_field **tep_event_fields(struct event_format *event);

static inline int tep_get_cpus(struct tep_handle *pevent)
{
	return pevent->cpus;
}

static inline void tep_set_cpus(struct tep_handle *pevent, int cpus)
{
	pevent->cpus = cpus;
}

static inline int tep_get_long_size(struct tep_handle *pevent)
{
	return pevent->long_size;
}

static inline void tep_set_long_size(struct tep_handle *pevent, int long_size)
{
	pevent->long_size = long_size;
}

static inline int tep_get_page_size(struct tep_handle *pevent)
{
	return pevent->page_size;
}

static inline void tep_set_page_size(struct tep_handle *pevent, int _page_size)
{
	pevent->page_size = _page_size;
}

static inline int tep_is_file_bigendian(struct tep_handle *pevent)
{
	return pevent->file_bigendian;
}

static inline void tep_set_file_bigendian(struct tep_handle *pevent, int endian)
{
	pevent->file_bigendian = endian;
}

static inline int tep_is_host_bigendian(struct tep_handle *pevent)
{
	return pevent->host_bigendian;
}

static inline void tep_set_host_bigendian(struct tep_handle *pevent, int endian)
{
	pevent->host_bigendian = endian;
}

static inline int tep_is_latency_format(struct tep_handle *pevent)
{
	return pevent->latency_format;
}

static inline void tep_set_latency_format(struct tep_handle *pevent, int lat)
{
	pevent->latency_format = lat;
}

struct tep_handle *tep_alloc(void);
void tep_free(struct tep_handle *pevent);
void tep_ref(struct tep_handle *pevent);
void tep_unref(struct tep_handle *pevent);

/* access to the internal parser */
void tep_buffer_init(const char *buf, unsigned long long size);
enum event_type tep_read_token(char **tok);
void tep_free_token(char *token);
int tep_peek_char(void);
const char *tep_get_input_buf(void);
unsigned long long tep_get_input_buf_ptr(void);

/* for debugging */
void tep_print_funcs(struct tep_handle *pevent);
void tep_print_printk(struct tep_handle *pevent);

/* ----------------------- filtering ----------------------- */

enum filter_boolean_type {
	FILTER_FALSE,
	FILTER_TRUE,
};

enum filter_op_type {
	FILTER_OP_AND = 1,
	FILTER_OP_OR,
	FILTER_OP_NOT,
};

enum filter_cmp_type {
	FILTER_CMP_NONE,
	FILTER_CMP_EQ,
	FILTER_CMP_NE,
	FILTER_CMP_GT,
	FILTER_CMP_LT,
	FILTER_CMP_GE,
	FILTER_CMP_LE,
	FILTER_CMP_MATCH,
	FILTER_CMP_NOT_MATCH,
	FILTER_CMP_REGEX,
	FILTER_CMP_NOT_REGEX,
};

enum filter_exp_type {
	FILTER_EXP_NONE,
	FILTER_EXP_ADD,
	FILTER_EXP_SUB,
	FILTER_EXP_MUL,
	FILTER_EXP_DIV,
	FILTER_EXP_MOD,
	FILTER_EXP_RSHIFT,
	FILTER_EXP_LSHIFT,
	FILTER_EXP_AND,
	FILTER_EXP_OR,
	FILTER_EXP_XOR,
	FILTER_EXP_NOT,
};

enum filter_arg_type {
	FILTER_ARG_NONE,
	FILTER_ARG_BOOLEAN,
	FILTER_ARG_VALUE,
	FILTER_ARG_FIELD,
	FILTER_ARG_EXP,
	FILTER_ARG_OP,
	FILTER_ARG_NUM,
	FILTER_ARG_STR,
};

enum filter_value_type {
	FILTER_NUMBER,
	FILTER_STRING,
	FILTER_CHAR
};

struct fliter_arg;

struct filter_arg_boolean {
	enum filter_boolean_type	value;
};

struct filter_arg_field {
	struct format_field	*field;
};

struct filter_arg_value {
	enum filter_value_type	type;
	union {
		char			*str;
		unsigned long long	val;
	};
};

struct filter_arg_op {
	enum filter_op_type	type;
	struct filter_arg	*left;
	struct filter_arg	*right;
};

struct filter_arg_exp {
	enum filter_exp_type	type;
	struct filter_arg	*left;
	struct filter_arg	*right;
};

struct filter_arg_num {
	enum filter_cmp_type	type;
	struct filter_arg	*left;
	struct filter_arg	*right;
};

struct filter_arg_str {
	enum filter_cmp_type	type;
	struct format_field	*field;
	char			*val;
	char			*buffer;
	regex_t			reg;
};

struct filter_arg {
	enum filter_arg_type	type;
	union {
		struct filter_arg_boolean	boolean;
		struct filter_arg_field		field;
		struct filter_arg_value		value;
		struct filter_arg_op		op;
		struct filter_arg_exp		exp;
		struct filter_arg_num		num;
		struct filter_arg_str		str;
	};
};

struct filter_type {
	int			event_id;
	struct event_format	*event;
	struct filter_arg	*filter;
};

#define TEP_FILTER_ERROR_BUFSZ  1024

struct event_filter {
	struct tep_handle	*pevent;
	int			filters;
	struct filter_type	*event_filters;
	char			error_buffer[TEP_FILTER_ERROR_BUFSZ];
};

struct event_filter *tep_filter_alloc(struct tep_handle *pevent);

/* for backward compatibility */
#define FILTER_NONE		TEP_ERRNO__NO_FILTER
#define FILTER_NOEXIST		TEP_ERRNO__FILTER_NOT_FOUND
#define FILTER_MISS		TEP_ERRNO__FILTER_MISS
#define FILTER_MATCH		TEP_ERRNO__FILTER_MATCH

enum filter_trivial_type {
	FILTER_TRIVIAL_FALSE,
	FILTER_TRIVIAL_TRUE,
	FILTER_TRIVIAL_BOTH,
};

enum tep_errno tep_filter_add_filter_str(struct event_filter *filter,
					 const char *filter_str);

enum tep_errno tep_filter_match(struct event_filter *filter,
				struct tep_record *record);

int tep_filter_strerror(struct event_filter *filter, enum tep_errno err,
			char *buf, size_t buflen);

int tep_event_filtered(struct event_filter *filter,
		       int event_id);

void tep_filter_reset(struct event_filter *filter);

int tep_filter_clear_trivial(struct event_filter *filter,
			     enum filter_trivial_type type);

void tep_filter_free(struct event_filter *filter);

char *tep_filter_make_string(struct event_filter *filter, int event_id);

int tep_filter_remove_event(struct event_filter *filter,
			    int event_id);

int tep_filter_event_has_trivial(struct event_filter *filter,
				 int event_id,
				 enum filter_trivial_type type);

int tep_filter_copy(struct event_filter *dest, struct event_filter *source);

int tep_update_trivial(struct event_filter *dest, struct event_filter *source,
			enum filter_trivial_type type);

int tep_filter_compare(struct event_filter *filter1, struct event_filter *filter2);

#endif /* _PARSE_EVENTS_H */
