// SPDX-License-Identifier: GPL-2.0
/*
 * trace_export.c - export basic ftrace utilities to user space
 *
 * Copyright (C) 2009 Steven Rostedt <srostedt@redhat.com>
 */
#include <linux/stringify.h>
#include <linux/kallsyms.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/init.h>

#include "trace_output.h"

/* Stub function for events with triggers */
static int ftrace_event_register(struct trace_event_call *call,
				 enum trace_reg type, void *data)
{
	return 0;
}

#undef TRACE_SYSTEM
#define TRACE_SYSTEM	ftrace

/*
 * The FTRACE_ENTRY_REG macro allows ftrace entry to define register
 * function and thus become accesible via perf.
 */
#undef FTRACE_ENTRY_REG
#define FTRACE_ENTRY_REG(name, struct_name, id, tstruct, print, \
			 filter, regfn) \
	FTRACE_ENTRY(name, struct_name, id, PARAMS(tstruct), PARAMS(print), \
		     filter)

/* not needed for this file */
#undef __field_struct
#define __field_struct(type, item)

#undef __field
#define __field(type, item)				type item;

#undef __field_desc
#define __field_desc(type, container, item)		type item;

#undef __array
#define __array(type, item, size)			type item[size];

#undef __array_desc
#define __array_desc(type, container, item, size)	type item[size];

#undef __dynamic_array
#define __dynamic_array(type, item)			type item[];

#undef F_STRUCT
#define F_STRUCT(args...)				args

#undef F_printk
#define F_printk(fmt, args...) fmt, args

#undef FTRACE_ENTRY
#define FTRACE_ENTRY(name, struct_name, id, tstruct, print, filter)	\
struct ____ftrace_##name {						\
	tstruct								\
};									\
static void __always_unused ____ftrace_check_##name(void)		\
{									\
	struct ____ftrace_##name *__entry = NULL;			\
									\
	/* force compile-time check on F_printk() */			\
	printk(print);							\
}

#undef FTRACE_ENTRY_DUP
#define FTRACE_ENTRY_DUP(name, struct_name, id, tstruct, print, filter)	\
	FTRACE_ENTRY(name, struct_name, id, PARAMS(tstruct), PARAMS(print), \
		     filter)

#include "trace_entries.h"

#undef __field
#define __field(type, item)						\
	ret = trace_define_field(event_call, #type, #item,		\
				 offsetof(typeof(field), item),		\
				 sizeof(field.item),			\
				 is_signed_type(type), filter_type);	\
	if (ret)							\
		return ret;

#undef __field_desc
#define __field_desc(type, container, item)	\
	ret = trace_define_field(event_call, #type, #item,		\
				 offsetof(typeof(field),		\
					  container.item),		\
				 sizeof(field.container.item),		\
				 is_signed_type(type), filter_type);	\
	if (ret)							\
		return ret;

#undef __array
#define __array(type, item, len)					\
	do {								\
		char *type_str = #type"["__stringify(len)"]";		\
		BUILD_BUG_ON(len > MAX_FILTER_STR_VAL);			\
		ret = trace_define_field(event_call, type_str, #item,	\
				 offsetof(typeof(field), item),		\
				 sizeof(field.item),			\
				 is_signed_type(type), filter_type);	\
		if (ret)						\
			return ret;					\
	} while (0);

#undef __array_desc
#define __array_desc(type, container, item, len)			\
	BUILD_BUG_ON(len > MAX_FILTER_STR_VAL);				\
	ret = trace_define_field(event_call, #type "[" #len "]", #item,	\
				 offsetof(typeof(field),		\
					  container.item),		\
				 sizeof(field.container.item),		\
				 is_signed_type(type), filter_type);	\
	if (ret)							\
		return ret;

#undef __dynamic_array
#define __dynamic_array(type, item)					\
	ret = trace_define_field(event_call, #type "[]", #item,  \
				 offsetof(typeof(field), item),		\
				 0, is_signed_type(type), filter_type);\
	if (ret)							\
		return ret;

#undef FTRACE_ENTRY
#define FTRACE_ENTRY(name, struct_name, id, tstruct, print, filter)	\
static int __init							\
ftrace_define_fields_##name(struct trace_event_call *event_call)	\
{									\
	struct struct_name field;					\
	int ret;							\
	int filter_type = filter;					\
									\
	tstruct;							\
									\
	return ret;							\
}

#include "trace_entries.h"

#undef __entry
#define __entry REC

#undef __field
#define __field(type, item)

#undef __field_desc
#define __field_desc(type, container, item)

#undef __array
#define __array(type, item, len)

#undef __array_desc
#define __array_desc(type, container, item, len)

#undef __dynamic_array
#define __dynamic_array(type, item)

#undef F_printk
#define F_printk(fmt, args...) __stringify(fmt) ", "  __stringify(args)

#undef FTRACE_ENTRY_REG
#define FTRACE_ENTRY_REG(call, struct_name, etype, tstruct, print, filter,\
			 regfn)						\
									\
struct trace_event_class __refdata event_class_ftrace_##call = {	\
	.system			= __stringify(TRACE_SYSTEM),		\
	.define_fields		= ftrace_define_fields_##call,		\
	.fields			= LIST_HEAD_INIT(event_class_ftrace_##call.fields),\
	.reg			= regfn,				\
};									\
									\
struct trace_event_call __used event_##call = {				\
	.class			= &event_class_ftrace_##call,		\
	{								\
		.name			= #call,			\
	},								\
	.event.type		= etype,				\
	.print_fmt		= print,				\
	.flags			= TRACE_EVENT_FL_IGNORE_ENABLE,		\
};									\
struct trace_event_call __used						\
__attribute__((section("_ftrace_events"))) *__event_##call = &event_##call;

#undef FTRACE_ENTRY
#define FTRACE_ENTRY(call, struct_name, etype, tstruct, print, filter)	\
	FTRACE_ENTRY_REG(call, struct_name, etype,			\
			 PARAMS(tstruct), PARAMS(print), filter, NULL)

bool ftrace_event_is_function(struct trace_event_call *call)
{
	return call == &event_function;
}

#include "trace_entries.h"
