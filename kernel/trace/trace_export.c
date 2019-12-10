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
#define FTRACE_ENTRY_REG(name, struct_name, id, tstruct, print, regfn) \
	FTRACE_ENTRY(name, struct_name, id, PARAMS(tstruct), PARAMS(print))

/* not needed for this file */
#undef __field_struct
#define __field_struct(type, item)

#undef __field
#define __field(type, item)				type item;

#undef __field_fn
#define __field_fn(type, item)				type item;

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
#define FTRACE_ENTRY(name, struct_name, id, tstruct, print)		\
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
#define FTRACE_ENTRY_DUP(name, struct_name, id, tstruct, print)		\
	FTRACE_ENTRY(name, struct_name, id, PARAMS(tstruct), PARAMS(print))

#include "trace_entries.h"

#undef __field_ext
#define __field_ext(_type, _item, _filter_type) {			\
	.type = #_type, .name = #_item,					\
	.size = sizeof(_type), .align = __alignof__(_type),		\
	is_signed_type(_type), .filter_type = _filter_type },

#undef __field
#define __field(_type, _item) __field_ext(_type, _item, FILTER_OTHER)

#undef __field_fn
#define __field_fn(_type, _item) __field_ext(_type, _item, FILTER_TRACE_FN)

#undef __field_desc
#define __field_desc(_type, _container, _item) __field_ext(_type, _item, FILTER_OTHER)

#undef __array
#define __array(_type, _item, _len) {					\
	.type = #_type"["__stringify(_len)"]", .name = #_item,		\
	.size = sizeof(_type[_len]), .align = __alignof__(_type),	\
	is_signed_type(_type), .filter_type = FILTER_OTHER },

#undef __array_desc
#define __array_desc(_type, _container, _item, _len) __array(_type, _item, _len)

#undef __dynamic_array
#define __dynamic_array(_type, _item) {					\
	.type = #_type "[]", .name = #_item,				\
	.size = 0, .align = __alignof__(_type),				\
	is_signed_type(_type), .filter_type = FILTER_OTHER },

#undef FTRACE_ENTRY
#define FTRACE_ENTRY(name, struct_name, id, tstruct, print)		\
static struct trace_event_fields ftrace_event_fields_##name[] = {	\
	tstruct								\
	{} };

#include "trace_entries.h"

#undef __entry
#define __entry REC

#undef __field
#define __field(type, item)

#undef __field_fn
#define __field_fn(type, item)

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
#define FTRACE_ENTRY_REG(call, struct_name, etype, tstruct, print, regfn) \
static struct trace_event_class __refdata event_class_ftrace_##call = {	\
	.system			= __stringify(TRACE_SYSTEM),		\
	.fields_array		= ftrace_event_fields_##call,		\
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
static struct trace_event_call __used						\
__attribute__((section("_ftrace_events"))) *__event_##call = &event_##call;

#undef FTRACE_ENTRY
#define FTRACE_ENTRY(call, struct_name, etype, tstruct, print)		\
	FTRACE_ENTRY_REG(call, struct_name, etype,			\
			 PARAMS(tstruct), PARAMS(print), NULL)

bool ftrace_event_is_function(struct trace_event_call *call)
{
	return call == &event_function;
}

#include "trace_entries.h"
