/*
 * trace_export.c - export basic ftrace utilities to user space
 *
 * Copyright (C) 2009 Steven Rostedt <srostedt@redhat.com>
 */
#include <linux/stringify.h>
#include <linux/kallsyms.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>

#include "trace_output.h"


#undef TRACE_STRUCT
#define TRACE_STRUCT(args...) args

#undef TRACE_FIELD
#define TRACE_FIELD(type, item, assign)					\
	ret = trace_seq_printf(s, "\tfield:" #type " " #item ";\t"	\
			       "offset:%u;\tsize:%u;\n",		\
			       (unsigned int)offsetof(typeof(field), item), \
			       (unsigned int)sizeof(field.item));	\
	if (!ret)							\
		return 0;


#undef TRACE_FIELD_SPECIAL
#define TRACE_FIELD_SPECIAL(type_item, item, len, cmd)			\
	ret = trace_seq_printf(s, "\tfield special:" #type_item ";\t"	\
			       "offset:%u;\tsize:%u;\n",		\
			       (unsigned int)offsetof(typeof(field), item), \
			       (unsigned int)sizeof(field.item));	\
	if (!ret)							\
		return 0;

#undef TRACE_FIELD_ZERO_CHAR
#define TRACE_FIELD_ZERO_CHAR(item)					\
	ret = trace_seq_printf(s, "\tfield:char " #item ";\t"		\
			       "offset:%u;\tsize:0;\n",			\
			       (unsigned int)offsetof(typeof(field), item)); \
	if (!ret)							\
		return 0;


#undef TP_RAW_FMT
#define TP_RAW_FMT(args...) args

#undef TRACE_EVENT_FORMAT
#define TRACE_EVENT_FORMAT(call, proto, args, fmt, tstruct, tpfmt)	\
static int								\
ftrace_format_##call(struct trace_seq *s)				\
{									\
	struct args field;						\
	int ret;							\
									\
	tstruct;							\
									\
	trace_seq_printf(s, "\nprint fmt: \"%s\"\n", tpfmt);		\
									\
	return ret;							\
}

#include "trace_event_types.h"

#undef TRACE_ZERO_CHAR
#define TRACE_ZERO_CHAR(arg)

#undef TRACE_FIELD
#define TRACE_FIELD(type, item, assign)\
	entry->item = assign;

#undef TRACE_FIELD
#define TRACE_FIELD(type, item, assign)\
	entry->item = assign;

#undef TP_CMD
#define TP_CMD(cmd...)	cmd

#undef TRACE_ENTRY
#define TRACE_ENTRY	entry

#undef TRACE_FIELD_SPECIAL
#define TRACE_FIELD_SPECIAL(type_item, item, len, cmd)	\
	cmd;

#undef TRACE_EVENT_FORMAT
#define TRACE_EVENT_FORMAT(call, proto, args, fmt, tstruct, tpfmt)	\
int ftrace_define_fields_##call(void);					\
static int ftrace_raw_init_event_##call(void);				\
									\
struct ftrace_event_call __used						\
__attribute__((__aligned__(4)))						\
__attribute__((section("_ftrace_events"))) event_##call = {		\
	.name			= #call,				\
	.id			= proto,				\
	.system			= __stringify(TRACE_SYSTEM),		\
	.raw_init		= ftrace_raw_init_event_##call,		\
	.show_format		= ftrace_format_##call,			\
	.define_fields		= ftrace_define_fields_##call,		\
};									\
static int ftrace_raw_init_event_##call(void)				\
{									\
	INIT_LIST_HEAD(&event_##call.fields);				\
	return 0;							\
}									\

#include "trace_event_types.h"

#undef TRACE_FIELD
#define TRACE_FIELD(type, item, assign)					\
	ret = trace_define_field(event_call, #type, #item,		\
				 offsetof(typeof(field), item),		\
				 sizeof(field.item));			\
	if (ret)							\
		return ret;

#undef TRACE_FIELD_SPECIAL
#define TRACE_FIELD_SPECIAL(type, item, len, cmd)			\
	ret = trace_define_field(event_call, #type "[" #len "]", #item,	\
				 offsetof(typeof(field), item),		\
				 sizeof(field.item));			\
	if (ret)							\
		return ret;

#undef TRACE_FIELD_ZERO_CHAR
#define TRACE_FIELD_ZERO_CHAR(item)

#undef TRACE_EVENT_FORMAT
#define TRACE_EVENT_FORMAT(call, proto, args, fmt, tstruct, tpfmt)	\
int									\
ftrace_define_fields_##call(void)					\
{									\
	struct ftrace_event_call *event_call = &event_##call;		\
	struct args field;						\
	int ret;							\
									\
	__common_field(unsigned char, type);				\
	__common_field(unsigned char, flags);				\
	__common_field(unsigned char, preempt_count);			\
	__common_field(int, pid);					\
	__common_field(int, tgid);					\
									\
	tstruct;							\
									\
	return ret;							\
}

#include "trace_event_types.h"
