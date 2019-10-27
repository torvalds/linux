// SPDX-License-Identifier: LGPL-2.1
/*
 * Copyright (C) 2009, 2010 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 */

#ifndef _PARSE_EVENTS_INT_H
#define _PARSE_EVENTS_INT_H

struct tep_cmdline;
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

	int old_format;

	int cpus;
	int long_size;
	int page_size;

	struct tep_cmdline *cmdlines;
	struct cmdline_list *cmdlist;
	int cmdline_count;

	struct func_map *func_map;
	struct func_resolver *func_resolver;
	struct func_list *funclist;
	unsigned int func_count;

	struct printk_map *printk_map;
	struct printk_list *printklist;
	unsigned int printk_count;


	struct tep_event **events;
	int nr_events;
	struct tep_event **sort_events;
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

	int test_filters;

	int flags;

	struct tep_format_field *bprint_ip_field;
	struct tep_format_field *bprint_fmt_field;
	struct tep_format_field *bprint_buf_field;

	struct event_handler *handlers;
	struct tep_function_handler *func_handlers;

	/* cache */
	struct tep_event *last_event;
};

void tep_free_event(struct tep_event *event);
void tep_free_format_field(struct tep_format_field *field);

unsigned short tep_data2host2(struct tep_handle *tep, unsigned short data);
unsigned int tep_data2host4(struct tep_handle *tep, unsigned int data);
unsigned long long tep_data2host8(struct tep_handle *tep, unsigned long long data);

#endif /* _PARSE_EVENTS_INT_H */
