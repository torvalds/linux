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

#include "trace-seq.h"

#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
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

/* ----------------------- tep ----------------------- */

struct tep_handle;
struct tep_event;

typedef int (*tep_event_handler_func)(struct trace_seq *s,
				      struct tep_record *record,
				      struct tep_event *event,
				      void *context);

typedef int (*tep_plugin_load_func)(struct tep_handle *tep);
typedef int (*tep_plugin_unload_func)(struct tep_handle *tep);

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
 *   int TEP_PLUGIN_LOADER(struct tep_handle *tep)
 *
 * TEP_PLUGIN_UNLOADER:  (optional)
 *   The function called just before unloading
 *
 *   int TEP_PLUGIN_UNLOADER(struct tep_handle *tep)
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

enum tep_format_flags {
	TEP_FIELD_IS_ARRAY	= 1,
	TEP_FIELD_IS_POINTER	= 2,
	TEP_FIELD_IS_SIGNED	= 4,
	TEP_FIELD_IS_STRING	= 8,
	TEP_FIELD_IS_DYNAMIC	= 16,
	TEP_FIELD_IS_LONG	= 32,
	TEP_FIELD_IS_FLAG	= 64,
	TEP_FIELD_IS_SYMBOLIC	= 128,
};

struct tep_format_field {
	struct tep_format_field	*next;
	struct tep_event	*event;
	char			*type;
	char			*name;
	char			*alias;
	int			offset;
	int			size;
	unsigned int		arraylen;
	unsigned int		elementsize;
	unsigned long		flags;
};

struct tep_format {
	int			nr_common;
	int			nr_fields;
	struct tep_format_field	*common_fields;
	struct tep_format_field	*fields;
};

struct tep_print_arg_atom {
	char			*atom;
};

struct tep_print_arg_string {
	char			*string;
	int			offset;
};

struct tep_print_arg_bitmask {
	char			*bitmask;
	int			offset;
};

struct tep_print_arg_field {
	char			*name;
	struct tep_format_field	*field;
};

struct tep_print_flag_sym {
	struct tep_print_flag_sym	*next;
	char				*value;
	char				*str;
};

struct tep_print_arg_typecast {
	char 			*type;
	struct tep_print_arg	*item;
};

struct tep_print_arg_flags {
	struct tep_print_arg		*field;
	char				*delim;
	struct tep_print_flag_sym	*flags;
};

struct tep_print_arg_symbol {
	struct tep_print_arg		*field;
	struct tep_print_flag_sym	*symbols;
};

struct tep_print_arg_hex {
	struct tep_print_arg	*field;
	struct tep_print_arg	*size;
};

struct tep_print_arg_int_array {
	struct tep_print_arg	*field;
	struct tep_print_arg	*count;
	struct tep_print_arg	*el_size;
};

struct tep_print_arg_dynarray {
	struct tep_format_field	*field;
	struct tep_print_arg	*index;
};

struct tep_print_arg;

struct tep_print_arg_op {
	char			*op;
	int			prio;
	struct tep_print_arg	*left;
	struct tep_print_arg	*right;
};

struct tep_function_handler;

struct tep_print_arg_func {
	struct tep_function_handler	*func;
	struct tep_print_arg		*args;
};

enum tep_print_arg_type {
	TEP_PRINT_NULL,
	TEP_PRINT_ATOM,
	TEP_PRINT_FIELD,
	TEP_PRINT_FLAGS,
	TEP_PRINT_SYMBOL,
	TEP_PRINT_HEX,
	TEP_PRINT_INT_ARRAY,
	TEP_PRINT_TYPE,
	TEP_PRINT_STRING,
	TEP_PRINT_BSTRING,
	TEP_PRINT_DYNAMIC_ARRAY,
	TEP_PRINT_OP,
	TEP_PRINT_FUNC,
	TEP_PRINT_BITMASK,
	TEP_PRINT_DYNAMIC_ARRAY_LEN,
	TEP_PRINT_HEX_STR,
};

struct tep_print_arg {
	struct tep_print_arg		*next;
	enum tep_print_arg_type		type;
	union {
		struct tep_print_arg_atom	atom;
		struct tep_print_arg_field	field;
		struct tep_print_arg_typecast	typecast;
		struct tep_print_arg_flags	flags;
		struct tep_print_arg_symbol	symbol;
		struct tep_print_arg_hex	hex;
		struct tep_print_arg_int_array	int_array;
		struct tep_print_arg_func	func;
		struct tep_print_arg_string	string;
		struct tep_print_arg_bitmask	bitmask;
		struct tep_print_arg_op		op;
		struct tep_print_arg_dynarray	dynarray;
	};
};

struct tep_print_fmt {
	char			*format;
	struct tep_print_arg	*args;
};

struct tep_event {
	struct tep_handle	*tep;
	char			*name;
	int			id;
	int			flags;
	struct tep_format	format;
	struct tep_print_fmt	print_fmt;
	char			*system;
	tep_event_handler_func	handler;
	void			*context;
};

enum {
	TEP_EVENT_FL_ISFTRACE	= 0x01,
	TEP_EVENT_FL_ISPRINT	= 0x02,
	TEP_EVENT_FL_ISBPRINT	= 0x04,
	TEP_EVENT_FL_ISFUNCENT	= 0x10,
	TEP_EVENT_FL_ISFUNCRET	= 0x20,
	TEP_EVENT_FL_NOHANDLE	= 0x40,
	TEP_EVENT_FL_PRINTRAW	= 0x80,

	TEP_EVENT_FL_FAILED	= 0x80000000
};

enum tep_event_sort_type {
	TEP_EVENT_SORT_ID,
	TEP_EVENT_SORT_NAME,
	TEP_EVENT_SORT_SYSTEM,
};

enum tep_event_type {
	TEP_EVENT_ERROR,
	TEP_EVENT_NONE,
	TEP_EVENT_SPACE,
	TEP_EVENT_NEWLINE,
	TEP_EVENT_OP,
	TEP_EVENT_DELIM,
	TEP_EVENT_ITEM,
	TEP_EVENT_DQUOTE,
	TEP_EVENT_SQUOTE,
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

struct tep_plugin_list;

#define INVALID_PLUGIN_LIST_OPTION	((char **)((unsigned long)-1))

struct tep_plugin_list *tep_load_plugins(struct tep_handle *tep);
void tep_unload_plugins(struct tep_plugin_list *plugin_list,
			struct tep_handle *tep);
char **tep_plugin_list_options(void);
void tep_plugin_free_options_list(char **list);
int tep_plugin_add_options(const char *name,
			   struct tep_plugin_option *options);
void tep_plugin_remove_options(struct tep_plugin_option *options);
void tep_print_plugins(struct trace_seq *s,
			const char *prefix, const char *suffix,
			const struct tep_plugin_list *list);

/* tep_handle */
typedef char *(tep_func_resolver_t)(void *priv,
				    unsigned long long *addrp, char **modp);
void tep_set_flag(struct tep_handle *tep, int flag);
void tep_clear_flag(struct tep_handle *tep, enum tep_flag flag);
bool tep_test_flag(struct tep_handle *tep, enum tep_flag flags);

static inline int tep_is_bigendian(void)
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

int tep_set_function_resolver(struct tep_handle *tep,
			      tep_func_resolver_t *func, void *priv);
void tep_reset_function_resolver(struct tep_handle *tep);
int tep_register_comm(struct tep_handle *tep, const char *comm, int pid);
int tep_override_comm(struct tep_handle *tep, const char *comm, int pid);
int tep_register_trace_clock(struct tep_handle *tep, const char *trace_clock);
int tep_register_function(struct tep_handle *tep, char *name,
			  unsigned long long addr, char *mod);
int tep_register_print_string(struct tep_handle *tep, const char *fmt,
			      unsigned long long addr);
bool tep_is_pid_registered(struct tep_handle *tep, int pid);

void tep_print_event_task(struct tep_handle *tep, struct trace_seq *s,
			  struct tep_event *event,
			  struct tep_record *record);
void tep_print_event_time(struct tep_handle *tep, struct trace_seq *s,
			  struct tep_event *event,
			  struct tep_record *record,
			  bool use_trace_clock);
void tep_print_event_data(struct tep_handle *tep, struct trace_seq *s,
			  struct tep_event *event,
			  struct tep_record *record);
void tep_print_event(struct tep_handle *tep, struct trace_seq *s,
		     struct tep_record *record, bool use_trace_clock);

int tep_parse_header_page(struct tep_handle *tep, char *buf, unsigned long size,
			  int long_size);

enum tep_errno tep_parse_event(struct tep_handle *tep, const char *buf,
			       unsigned long size, const char *sys);
enum tep_errno tep_parse_format(struct tep_handle *tep,
				struct tep_event **eventp,
				const char *buf,
				unsigned long size, const char *sys);

void *tep_get_field_raw(struct trace_seq *s, struct tep_event *event,
			const char *name, struct tep_record *record,
			int *len, int err);

int tep_get_field_val(struct trace_seq *s, struct tep_event *event,
		      const char *name, struct tep_record *record,
		      unsigned long long *val, int err);
int tep_get_common_field_val(struct trace_seq *s, struct tep_event *event,
			     const char *name, struct tep_record *record,
			     unsigned long long *val, int err);
int tep_get_any_field_val(struct trace_seq *s, struct tep_event *event,
			  const char *name, struct tep_record *record,
			  unsigned long long *val, int err);

int tep_print_num_field(struct trace_seq *s, const char *fmt,
			struct tep_event *event, const char *name,
			struct tep_record *record, int err);

int tep_print_func_field(struct trace_seq *s, const char *fmt,
			 struct tep_event *event, const char *name,
			 struct tep_record *record, int err);

enum tep_reg_handler {
	TEP_REGISTER_SUCCESS = 0,
	TEP_REGISTER_SUCCESS_OVERWRITE,
};

int tep_register_event_handler(struct tep_handle *tep, int id,
			       const char *sys_name, const char *event_name,
			       tep_event_handler_func func, void *context);
int tep_unregister_event_handler(struct tep_handle *tep, int id,
				 const char *sys_name, const char *event_name,
				 tep_event_handler_func func, void *context);
int tep_register_print_function(struct tep_handle *tep,
				tep_func_handler func,
				enum tep_func_arg_type ret_type,
				char *name, ...);
int tep_unregister_print_function(struct tep_handle *tep,
				  tep_func_handler func, char *name);

struct tep_format_field *tep_find_common_field(struct tep_event *event, const char *name);
struct tep_format_field *tep_find_field(struct tep_event *event, const char *name);
struct tep_format_field *tep_find_any_field(struct tep_event *event, const char *name);

const char *tep_find_function(struct tep_handle *tep, unsigned long long addr);
unsigned long long
tep_find_function_address(struct tep_handle *tep, unsigned long long addr);
unsigned long long tep_read_number(struct tep_handle *tep, const void *ptr, int size);
int tep_read_number_field(struct tep_format_field *field, const void *data,
			  unsigned long long *value);

struct tep_event *tep_get_first_event(struct tep_handle *tep);
int tep_get_events_count(struct tep_handle *tep);
struct tep_event *tep_find_event(struct tep_handle *tep, int id);

struct tep_event *
tep_find_event_by_name(struct tep_handle *tep, const char *sys, const char *name);
struct tep_event *
tep_find_event_by_record(struct tep_handle *tep, struct tep_record *record);

void tep_data_latency_format(struct tep_handle *tep,
			     struct trace_seq *s, struct tep_record *record);
int tep_data_type(struct tep_handle *tep, struct tep_record *rec);
int tep_data_pid(struct tep_handle *tep, struct tep_record *rec);
int tep_data_preempt_count(struct tep_handle *tep, struct tep_record *rec);
int tep_data_flags(struct tep_handle *tep, struct tep_record *rec);
const char *tep_data_comm_from_pid(struct tep_handle *tep, int pid);
struct tep_cmdline;
struct tep_cmdline *tep_data_pid_from_comm(struct tep_handle *tep, const char *comm,
					   struct tep_cmdline *next);
int tep_cmdline_pid(struct tep_handle *tep, struct tep_cmdline *cmdline);

void tep_print_field(struct trace_seq *s, void *data,
		     struct tep_format_field *field);
void tep_print_fields(struct trace_seq *s, void *data,
		      int size __maybe_unused, struct tep_event *event);
void tep_event_info(struct trace_seq *s, struct tep_event *event,
		    struct tep_record *record);
int tep_strerror(struct tep_handle *tep, enum tep_errno errnum,
		 char *buf, size_t buflen);

struct tep_event **tep_list_events(struct tep_handle *tep, enum tep_event_sort_type);
struct tep_event **tep_list_events_copy(struct tep_handle *tep,
					enum tep_event_sort_type);
struct tep_format_field **tep_event_common_fields(struct tep_event *event);
struct tep_format_field **tep_event_fields(struct tep_event *event);

enum tep_endian {
        TEP_LITTLE_ENDIAN = 0,
        TEP_BIG_ENDIAN
};
int tep_get_cpus(struct tep_handle *tep);
void tep_set_cpus(struct tep_handle *tep, int cpus);
int tep_get_long_size(struct tep_handle *tep);
void tep_set_long_size(struct tep_handle *tep, int long_size);
int tep_get_page_size(struct tep_handle *tep);
void tep_set_page_size(struct tep_handle *tep, int _page_size);
bool tep_is_file_bigendian(struct tep_handle *tep);
void tep_set_file_bigendian(struct tep_handle *tep, enum tep_endian endian);
bool tep_is_local_bigendian(struct tep_handle *tep);
void tep_set_local_bigendian(struct tep_handle *tep, enum tep_endian endian);
bool tep_is_latency_format(struct tep_handle *tep);
void tep_set_latency_format(struct tep_handle *tep, int lat);
int tep_get_header_page_size(struct tep_handle *tep);
int tep_get_header_timestamp_size(struct tep_handle *tep);
bool tep_is_old_format(struct tep_handle *tep);
void tep_set_print_raw(struct tep_handle *tep, int print_raw);
void tep_set_test_filters(struct tep_handle *tep, int test_filters);

struct tep_handle *tep_alloc(void);
void tep_free(struct tep_handle *tep);
void tep_ref(struct tep_handle *tep);
void tep_unref(struct tep_handle *tep);
int tep_get_ref(struct tep_handle *tep);

/* access to the internal parser */
void tep_buffer_init(const char *buf, unsigned long long size);
enum tep_event_type tep_read_token(char **tok);
void tep_free_token(char *token);
int tep_peek_char(void);
const char *tep_get_input_buf(void);
unsigned long long tep_get_input_buf_ptr(void);

/* for debugging */
void tep_print_funcs(struct tep_handle *tep);
void tep_print_printk(struct tep_handle *tep);

/* ----------------------- filtering ----------------------- */

enum tep_filter_boolean_type {
	TEP_FILTER_FALSE,
	TEP_FILTER_TRUE,
};

enum tep_filter_op_type {
	TEP_FILTER_OP_AND = 1,
	TEP_FILTER_OP_OR,
	TEP_FILTER_OP_NOT,
};

enum tep_filter_cmp_type {
	TEP_FILTER_CMP_NONE,
	TEP_FILTER_CMP_EQ,
	TEP_FILTER_CMP_NE,
	TEP_FILTER_CMP_GT,
	TEP_FILTER_CMP_LT,
	TEP_FILTER_CMP_GE,
	TEP_FILTER_CMP_LE,
	TEP_FILTER_CMP_MATCH,
	TEP_FILTER_CMP_NOT_MATCH,
	TEP_FILTER_CMP_REGEX,
	TEP_FILTER_CMP_NOT_REGEX,
};

enum tep_filter_exp_type {
	TEP_FILTER_EXP_NONE,
	TEP_FILTER_EXP_ADD,
	TEP_FILTER_EXP_SUB,
	TEP_FILTER_EXP_MUL,
	TEP_FILTER_EXP_DIV,
	TEP_FILTER_EXP_MOD,
	TEP_FILTER_EXP_RSHIFT,
	TEP_FILTER_EXP_LSHIFT,
	TEP_FILTER_EXP_AND,
	TEP_FILTER_EXP_OR,
	TEP_FILTER_EXP_XOR,
	TEP_FILTER_EXP_NOT,
};

enum tep_filter_arg_type {
	TEP_FILTER_ARG_NONE,
	TEP_FILTER_ARG_BOOLEAN,
	TEP_FILTER_ARG_VALUE,
	TEP_FILTER_ARG_FIELD,
	TEP_FILTER_ARG_EXP,
	TEP_FILTER_ARG_OP,
	TEP_FILTER_ARG_NUM,
	TEP_FILTER_ARG_STR,
};

enum tep_filter_value_type {
	TEP_FILTER_NUMBER,
	TEP_FILTER_STRING,
	TEP_FILTER_CHAR
};

struct tep_filter_arg;

struct tep_filter_arg_boolean {
	enum tep_filter_boolean_type	value;
};

struct tep_filter_arg_field {
	struct tep_format_field		*field;
};

struct tep_filter_arg_value {
	enum tep_filter_value_type	type;
	union {
		char			*str;
		unsigned long long	val;
	};
};

struct tep_filter_arg_op {
	enum tep_filter_op_type		type;
	struct tep_filter_arg		*left;
	struct tep_filter_arg		*right;
};

struct tep_filter_arg_exp {
	enum tep_filter_exp_type	type;
	struct tep_filter_arg		*left;
	struct tep_filter_arg		*right;
};

struct tep_filter_arg_num {
	enum tep_filter_cmp_type	type;
	struct tep_filter_arg		*left;
	struct tep_filter_arg		*right;
};

struct tep_filter_arg_str {
	enum tep_filter_cmp_type	type;
	struct tep_format_field		*field;
	char				*val;
	char				*buffer;
	regex_t				reg;
};

struct tep_filter_arg {
	enum tep_filter_arg_type		type;
	union {
		struct tep_filter_arg_boolean	boolean;
		struct tep_filter_arg_field	field;
		struct tep_filter_arg_value	value;
		struct tep_filter_arg_op	op;
		struct tep_filter_arg_exp	exp;
		struct tep_filter_arg_num	num;
		struct tep_filter_arg_str	str;
	};
};

struct tep_filter_type {
	int			event_id;
	struct tep_event	*event;
	struct tep_filter_arg	*filter;
};

#define TEP_FILTER_ERROR_BUFSZ  1024

struct tep_event_filter {
	struct tep_handle	*tep;
	int			filters;
	struct tep_filter_type	*event_filters;
	char			error_buffer[TEP_FILTER_ERROR_BUFSZ];
};

struct tep_event_filter *tep_filter_alloc(struct tep_handle *tep);

/* for backward compatibility */
#define FILTER_NONE		TEP_ERRNO__NO_FILTER
#define FILTER_NOEXIST		TEP_ERRNO__FILTER_NOT_FOUND
#define FILTER_MISS		TEP_ERRNO__FILTER_MISS
#define FILTER_MATCH		TEP_ERRNO__FILTER_MATCH

enum tep_errno tep_filter_add_filter_str(struct tep_event_filter *filter,
					 const char *filter_str);

enum tep_errno tep_filter_match(struct tep_event_filter *filter,
				struct tep_record *record);

int tep_filter_strerror(struct tep_event_filter *filter, enum tep_errno err,
			char *buf, size_t buflen);

int tep_event_filtered(struct tep_event_filter *filter,
		       int event_id);

void tep_filter_reset(struct tep_event_filter *filter);

void tep_filter_free(struct tep_event_filter *filter);

char *tep_filter_make_string(struct tep_event_filter *filter, int event_id);

int tep_filter_remove_event(struct tep_event_filter *filter,
			    int event_id);

int tep_filter_copy(struct tep_event_filter *dest, struct tep_event_filter *source);

int tep_filter_compare(struct tep_event_filter *filter1, struct tep_event_filter *filter2);

#endif /* _PARSE_EVENTS_H */
