/*
 * Copyright (C) 2009, Steven Rostedt <srostedt@redhat.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License (not later!)
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "../perf.h"
#include "util.h"
#include "trace-event.h"
#include "debug.h"

static int input_fd;

static ssize_t trace_data_size;
static bool repipe;

static int __do_read(int fd, void *buf, int size)
{
	int rsize = size;

	while (size) {
		int ret = read(fd, buf, size);

		if (ret <= 0)
			return -1;

		if (repipe) {
			int retw = write(STDOUT_FILENO, buf, ret);

			if (retw <= 0 || retw != ret) {
				pr_debug("repiping input file");
				return -1;
			}
		}

		size -= ret;
		buf += ret;
	}

	return rsize;
}

static int do_read(void *data, int size)
{
	int r;

	r = __do_read(input_fd, data, size);
	if (r <= 0) {
		pr_debug("reading input file (size expected=%d received=%d)",
			 size, r);
		return -1;
	}

	trace_data_size += r;

	return r;
}

/* If it fails, the next read will report it */
static void skip(int size)
{
	char buf[BUFSIZ];
	int r;

	while (size) {
		r = size > BUFSIZ ? BUFSIZ : size;
		do_read(buf, r);
		size -= r;
	};
}

static unsigned int read4(struct pevent *pevent)
{
	unsigned int data;

	if (do_read(&data, 4) < 0)
		return 0;
	return __data2host4(pevent, data);
}

static unsigned long long read8(struct pevent *pevent)
{
	unsigned long long data;

	if (do_read(&data, 8) < 0)
		return 0;
	return __data2host8(pevent, data);
}

static char *read_string(void)
{
	char buf[BUFSIZ];
	char *str = NULL;
	int size = 0;
	off_t r;
	char c;

	for (;;) {
		r = read(input_fd, &c, 1);
		if (r < 0) {
			pr_debug("reading input file");
			goto out;
		}

		if (!r) {
			pr_debug("no data");
			goto out;
		}

		if (repipe) {
			int retw = write(STDOUT_FILENO, &c, 1);

			if (retw <= 0 || retw != r) {
				pr_debug("repiping input file string");
				goto out;
			}
		}

		buf[size++] = c;

		if (!c)
			break;
	}

	trace_data_size += size;

	str = malloc(size);
	if (str)
		memcpy(str, buf, size);
out:
	return str;
}

static int read_proc_kallsyms(struct pevent *pevent)
{
	unsigned int size;
	char *buf;

	size = read4(pevent);
	if (!size)
		return 0;

	buf = malloc(size + 1);
	if (buf == NULL)
		return -1;

	if (do_read(buf, size) < 0) {
		free(buf);
		return -1;
	}
	buf[size] = '\0';

	parse_proc_kallsyms(pevent, buf, size);

	free(buf);
	return 0;
}

static int read_ftrace_printk(struct pevent *pevent)
{
	unsigned int size;
	char *buf;

	/* it can have 0 size */
	size = read4(pevent);
	if (!size)
		return 0;

	buf = malloc(size);
	if (buf == NULL)
		return -1;

	if (do_read(buf, size) < 0) {
		free(buf);
		return -1;
	}

	parse_ftrace_printk(pevent, buf, size);

	free(buf);
	return 0;
}

static int read_header_files(struct pevent *pevent)
{
	unsigned long long size;
	char *header_page;
	char buf[BUFSIZ];
	int ret = 0;

	if (do_read(buf, 12) < 0)
		return -1;

	if (memcmp(buf, "header_page", 12) != 0) {
		pr_debug("did not read header page");
		return -1;
	}

	size = read8(pevent);

	header_page = malloc(size);
	if (header_page == NULL)
		return -1;

	if (do_read(header_page, size) < 0) {
		pr_debug("did not read header page");
		free(header_page);
		return -1;
	}

	if (!pevent_parse_header_page(pevent, header_page, size,
				      pevent_get_long_size(pevent))) {
		/*
		 * The commit field in the page is of type long,
		 * use that instead, since it represents the kernel.
		 */
		pevent_set_long_size(pevent, pevent->header_page_size_size);
	}
	free(header_page);

	if (do_read(buf, 13) < 0)
		return -1;

	if (memcmp(buf, "header_event", 13) != 0) {
		pr_debug("did not read header event");
		return -1;
	}

	size = read8(pevent);
	skip(size);

	return ret;
}

static int read_ftrace_file(struct pevent *pevent, unsigned long long size)
{
	char *buf;

	buf = malloc(size);
	if (buf == NULL)
		return -1;

	if (do_read(buf, size) < 0) {
		free(buf);
		return -1;
	}

	parse_ftrace_file(pevent, buf, size);
	free(buf);
	return 0;
}

static int read_event_file(struct pevent *pevent, char *sys,
			    unsigned long long size)
{
	char *buf;

	buf = malloc(size);
	if (buf == NULL)
		return -1;

	if (do_read(buf, size) < 0) {
		free(buf);
		return -1;
	}

	parse_event_file(pevent, buf, size, sys);
	free(buf);
	return 0;
}

static int read_ftrace_files(struct pevent *pevent)
{
	unsigned long long size;
	int count;
	int i;
	int ret;

	count = read4(pevent);

	for (i = 0; i < count; i++) {
		size = read8(pevent);
		ret = read_ftrace_file(pevent, size);
		if (ret)
			return ret;
	}
	return 0;
}

static int read_event_files(struct pevent *pevent)
{
	unsigned long long size;
	char *sys;
	int systems;
	int count;
	int i,x;
	int ret;

	systems = read4(pevent);

	for (i = 0; i < systems; i++) {
		sys = read_string();
		if (sys == NULL)
			return -1;

		count = read4(pevent);

		for (x=0; x < count; x++) {
			size = read8(pevent);
			ret = read_event_file(pevent, sys, size);
			if (ret)
				return ret;
		}
	}
	return 0;
}

ssize_t trace_report(int fd, struct trace_event *tevent, bool __repipe)
{
	char buf[BUFSIZ];
	char test[] = { 23, 8, 68 };
	char *version;
	int show_version = 0;
	int show_funcs = 0;
	int show_printk = 0;
	ssize_t size = -1;
	int file_bigendian;
	int host_bigendian;
	int file_long_size;
	int file_page_size;
	struct pevent *pevent = NULL;
	int err;

	repipe = __repipe;
	input_fd = fd;

	if (do_read(buf, 3) < 0)
		return -1;
	if (memcmp(buf, test, 3) != 0) {
		pr_debug("no trace data in the file");
		return -1;
	}

	if (do_read(buf, 7) < 0)
		return -1;
	if (memcmp(buf, "tracing", 7) != 0) {
		pr_debug("not a trace file (missing 'tracing' tag)");
		return -1;
	}

	version = read_string();
	if (version == NULL)
		return -1;
	if (show_version)
		printf("version = %s\n", version);
	free(version);

	if (do_read(buf, 1) < 0)
		return -1;
	file_bigendian = buf[0];
	host_bigendian = bigendian();

	if (trace_event__init(tevent)) {
		pr_debug("trace_event__init failed");
		goto out;
	}

	pevent = tevent->pevent;

	pevent_set_flag(pevent, PEVENT_NSEC_OUTPUT);
	pevent_set_file_bigendian(pevent, file_bigendian);
	pevent_set_host_bigendian(pevent, host_bigendian);

	if (do_read(buf, 1) < 0)
		goto out;
	file_long_size = buf[0];

	file_page_size = read4(pevent);
	if (!file_page_size)
		goto out;

	pevent_set_long_size(pevent, file_long_size);
	pevent_set_page_size(pevent, file_page_size);

	err = read_header_files(pevent);
	if (err)
		goto out;
	err = read_ftrace_files(pevent);
	if (err)
		goto out;
	err = read_event_files(pevent);
	if (err)
		goto out;
	err = read_proc_kallsyms(pevent);
	if (err)
		goto out;
	err = read_ftrace_printk(pevent);
	if (err)
		goto out;

	size = trace_data_size;
	repipe = false;

	if (show_funcs) {
		pevent_print_funcs(pevent);
	} else if (show_printk) {
		pevent_print_printk(pevent);
	}

	pevent = NULL;

out:
	if (pevent)
		trace_event__cleanup(tevent);
	return size;
}
