/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 */

#ifndef _SELFTESTS_POWERPC_PMU_EBB_TRACE_H
#define _SELFTESTS_POWERPC_PMU_EBB_TRACE_H

#include "utils.h"

#define TRACE_TYPE_REG		1
#define TRACE_TYPE_COUNTER	2
#define TRACE_TYPE_STRING	3
#define TRACE_TYPE_INDENT	4
#define TRACE_TYPE_OUTDENT	5

struct trace_entry
{
	u8 type;
	u8 length;
	u8 data[0];
};

struct trace_buffer
{
	u64  size;
	bool overflow;
	void *tail;
	u8   data[0];
};

struct trace_buffer *trace_buffer_allocate(u64 size);
int trace_log_reg(struct trace_buffer *tb, u64 reg, u64 value);
int trace_log_counter(struct trace_buffer *tb, u64 value);
int trace_log_string(struct trace_buffer *tb, char *str);
int trace_log_indent(struct trace_buffer *tb);
int trace_log_outdent(struct trace_buffer *tb);
void trace_buffer_print(struct trace_buffer *tb);
void trace_print_location(struct trace_buffer *tb);

#endif /* _SELFTESTS_POWERPC_PMU_EBB_TRACE_H */
