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
struct tep_plugins_dir;

#define __hidden __attribute__((visibility ("hidden")))

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

	struct tep_plugins_dir *plugins_dir;
};

enum tep_print_parse_type {
	PRINT_FMT_STRING,
	PRINT_FMT_ARG_DIGIT,
	PRINT_FMT_ARG_POINTER,
	PRINT_FMT_ARG_STRING,
};

struct tep_print_parse {
	struct tep_print_parse	*next;

	char				*format;
	int				ls;
	enum tep_print_parse_type	type;
	struct tep_print_arg		*arg;
	struct tep_print_arg		*len_as_arg;
};

void free_tep_event(struct tep_event *event);
void free_tep_format_field(struct tep_format_field *field);
void free_tep_plugin_paths(struct tep_handle *tep);

unsigned short data2host2(struct tep_handle *tep, unsigned short data);
unsigned int data2host4(struct tep_handle *tep, unsigned int data);
unsigned long long data2host8(struct tep_handle *tep, unsigned long long data);

/* access to the internal parser */
int peek_char(void);
void init_input_buf(const char *buf, unsigned long long size);
unsigned long long get_input_buf_ptr(void);
const char *get_input_buf(void);
enum tep_event_type read_token(char **tok);
void free_token(char *tok);

#endif /* _PARSE_EVENTS_INT_H */
