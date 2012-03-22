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

#undef TRACE_SYSTEM
#define TRACE_SYSTEM	ftrace

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
#define FTRACE_ENTRY(name, struct_name, id, tstruct, print)	\
struct ____ftrace_##name {					\
	tstruct							\
};								\
static void __always_unused ____ftrace_check_##name(void)	\
{								\
	struct ____ftrace_##name *__entry = NULL;		\
								\
	/* force compile-time check on F_printk() */		\
	printk(print);						\
}

#undef FTRACE_ENTRY_DUP
#define FTRACE_ENTRY_DUP(name, struct_name, id, tstruct, print)	\
	FTRACE_ENTRY(name, struct_name, id, PARAMS(tstruct), PARAMS(print))

#include "trace_entries.h"

#undef __field
#define __field(type, item)						\
	ret = trace_define_field(event_call, #type, #item,		\
				 offsetof(typeof(field), item),		\
				 sizeof(field.item),			\
				 is_signed_type(type), FILTER_OTHER);	\
	if (ret)							\
		return ret;

#undef __field_desc
#define __field_desc(type, container, item)	\
	ret = trace_define_field(event_call, #type, #item,		\
				 offsetof(typeof(field),		\
					  container.item),		\
				 sizeof(field.container.item),		\
				 is_signed_type(type), FILTER_OTHER);	\
	if (ret)							\
		return ret;

#undef __array
#define __array(type, item, len)					\
	do {								\
		BUILD_BUG_ON(len > MAX_FILTER_STR_VAL);			\
		mutex_lock(&event_storage_mutex);			\
		snprintf(event_storage, sizeof(event_storage),		\
			 "%s[%d]", #type, len);				\
		ret = trace_define_field(event_call, event_storage, #item, \
				 offsetof(typeof(field), item),		\
				 sizeof(field.item),			\
				 is_signed_type(type), FILTER_OTHER);	\
		mutex_unlock(&event_storage_mutex);			\
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
				 is_signed_type(type), FILTER_OTHER);	\
	if (ret)							\
		return ret;

#undef __dynamic_array
#define __dynamic_array(type, item)					\
	ret = trace_define_field(event_call, #type, #item,		\
				 offsetof(typeof(field), item),		\
				 0, is_signed_type(type), FILTER_OTHER);\
	if (ret)							\
		return ret;

#undef FTRACE_ENTRY
#define FTRACE_ENTRY(name, struct_name, id, tstruct, print)		\
int									\
ftrace_define_fields_##name(struct ftrace_event_call *event_call)	\
{									\
	struct struct_name field;					\
	int ret;							\
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

#undef FTRACE_ENTRY
#define FTRACE_ENTRY(call, struct_name, etype, tstruct, print)		\
									\
struct ftrace_event_class event_class_ftrace_##call = {			\
	.system			= __stringify(TRACE_SYSTEM),		\
	.define_fields		= ftrace_define_fields_##call,		\
	.fields			= LIST_HEAD_INIT(event_class_ftrace_##call.fields),\
};									\
									\
struct ftrace_event_call __used event_##call = {			\
	.name			= #call,				\
	.event.type		= etype,				\
	.class			= &event_class_ftrace_##call,		\
	.print_fmt		= print,				\
};									\
struct ftrace_event_call __used						\
__attribute__((section("_ftrace_events"))) *__event_##call = &event_##call;

#include "trace_entries.h"
