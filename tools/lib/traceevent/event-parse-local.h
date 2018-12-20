// SPDX-License-Identifier: LGPL-2.1
/*
 * Copyright (C) 2009, 2010 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 */

#ifndef _PARSE_EVENTS_INT_H
#define _PARSE_EVENTS_INT_H

struct cmdline;
struct cmdline_list;
struct func_map;
struct func_list;
struct event_handler;
struct func_resolver;

struct tep_handle {
	int ref_count;

	int header_page_ts_offset;
	int header_page_ts_size;
	int header_page_size_offset;
	int header_page_size_size;
	int header_page_data_offset;
	int header_page_data_size;
	int header_page_overwrite;

	enum tep_endian file_bigendian;
	enum tep_endian host_bigendian;

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


	struct tep_event_format **events;
	int nr_events;
	struct tep_event_format **sort_events;
	enum tep_event_sort_type last_type;

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

	struct tep_format_field *bprint_ip_field;
	struct tep_format_field *bprint_fmt_field;
	struct tep_format_field *bprint_buf_field;

	struct event_handler *handlers;
	struct tep_function_handler *func_handlers;

	/* cache */
	struct tep_event_format *last_event;

	char *trace_clock;
};

#endif /* _PARSE_EVENTS_INT_H */
