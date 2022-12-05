// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2009, Steven Rostedt <srostedt@redhat.com>
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <traceevent/event-parse.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "trace-event.h"
#include "debug.h"
#include "util.h"

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
	}
}

static unsigned int read4(struct tep_handle *pevent)
{
	unsigned int data;

	if (do_read(&data, 4) < 0)
		return 0;
	return tep_read_number(pevent, &data, 4);
}

static unsigned long long read8(struct tep_handle *pevent)
{
	unsigned long long data;

	if (do_read(&data, 8) < 0)
		return 0;
	return tep_read_number(pevent, &data, 8);
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

static int read_proc_kallsyms(struct tep_handle *pevent)
{
	unsigned int size;

	size = read4(pevent);
	if (!size)
		return 0;
	/*
	 * Just skip it, now that we configure libtraceevent to use the
	 * tools/perf/ symbol resolver.
	 *
	 * We need to skip it so that we can continue parsing old perf.data
	 * files, that contains this /proc/kallsyms payload.
	 *
	 * Newer perf.data files will have just the 4-bytes zeros "kallsyms
	 * payload", so that older tools can continue reading it and interpret
	 * it as "no kallsyms payload is present".
	 */
	lseek(input_fd, size, SEEK_CUR);
	trace_data_size += size;
	return 0;
}

static int read_ftrace_printk(struct tep_handle *pevent)
{
	unsigned int size;
	char *buf;

	/* it can have 0 size */
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

	parse_ftrace_printk(pevent, buf, size);

	free(buf);
	return 0;
}

static int read_header_files(struct tep_handle *pevent)
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

	if (!tep_parse_header_page(pevent, header_page, size,
				   tep_get_long_size(pevent))) {
		/*
		 * The commit field in the page is of type long,
		 * use that instead, since it represents the kernel.
		 */
		tep_set_long_size(pevent, tep_get_header_page_size(pevent));
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

static int read_ftrace_file(struct tep_handle *pevent, unsigned long long size)
{
	int ret;
	char *buf;

	buf = malloc(size);
	if (buf == NULL) {
		pr_debug("memory allocation failure\n");
		return -1;
	}

	ret = do_read(buf, size);
	if (ret < 0) {
		pr_debug("error reading ftrace file.\n");
		goto out;
	}

	ret = parse_ftrace_file(pevent, buf, size);
	if (ret < 0)
		pr_debug("error parsing ftrace file.\n");
out:
	free(buf);
	return ret;
}

static int read_event_file(struct tep_handle *pevent, char *sys,
			   unsigned long long size)
{
	int ret;
	char *buf;

	buf = malloc(size);
	if (buf == NULL) {
		pr_debug("memory allocation failure\n");
		return -1;
	}

	ret = do_read(buf, size);
	if (ret < 0)
		goto out;

	ret = parse_event_file(pevent, buf, size, sys);
	if (ret < 0)
		pr_debug("error parsing event file.\n");
out:
	free(buf);
	return ret;
}

static int read_ftrace_files(struct tep_handle *pevent)
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

static int read_event_files(struct tep_handle *pevent)
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
			if (ret) {
				free(sys);
				return ret;
			}
		}
		free(sys);
	}
	return 0;
}

static int read_saved_cmdline(struct tep_handle *pevent)
{
	unsigned long long size;
	char *buf;
	int ret;

	/* it can have 0 size */
	size = read8(pevent);
	if (!size)
		return 0;

	buf = malloc(size + 1);
	if (buf == NULL) {
		pr_debug("memory allocation failure\n");
		return -1;
	}

	ret = do_read(buf, size);
	if (ret < 0) {
		pr_debug("error reading saved cmdlines\n");
		goto out;
	}
	buf[ret] = '\0';

	parse_saved_cmdline(pevent, buf, size);
	ret = 0;
out:
	free(buf);
	return ret;
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
	struct tep_handle *pevent = NULL;
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

	if (do_read(buf, 1) < 0) {
		free(version);
		return -1;
	}
	file_bigendian = buf[0];
	host_bigendian = host_is_bigendian() ? 1 : 0;

	if (trace_event__init(tevent)) {
		pr_debug("trace_event__init failed");
		goto out;
	}

	pevent = tevent->pevent;

	tep_set_flag(pevent, TEP_NSEC_OUTPUT);
	tep_set_file_bigendian(pevent, file_bigendian);
	tep_set_local_bigendian(pevent, host_bigendian);

	if (do_read(buf, 1) < 0)
		goto out;
	file_long_size = buf[0];

	file_page_size = read4(pevent);
	if (!file_page_size)
		goto out;

	tep_set_long_size(pevent, file_long_size);
	tep_set_page_size(pevent, file_page_size);

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
	if (atof(version) >= 0.6) {
		err = read_saved_cmdline(pevent);
		if (err)
			goto out;
	}

	size = trace_data_size;
	repipe = false;

	if (show_funcs) {
		tep_print_funcs(pevent);
	} else if (show_printk) {
		tep_print_printk(pevent);
	}

	pevent = NULL;

out:
	if (pevent)
		trace_event__cleanup(tevent);
	free(version);
	return size;
}
