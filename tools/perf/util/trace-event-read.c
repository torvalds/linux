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
#define _LARGEFILE64_SOURCE

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
#include <ctype.h>
#include <errno.h>

#include "../perf.h"
#include "util.h"
#include "trace-event.h"

static int input_fd;

static int read_page;

int file_bigendian;
int host_bigendian;
static int long_size;

static unsigned long	page_size;

static int read_or_die(void *data, int size)
{
	int r;

	r = read(input_fd, data, size);
	if (r != size)
		die("reading input file (size expected=%d received=%d)",
		    size, r);
	return r;
}

static unsigned int read4(void)
{
	unsigned int data;

	read_or_die(&data, 4);
	return __data2host4(data);
}

static unsigned long long read8(void)
{
	unsigned long long data;

	read_or_die(&data, 8);
	return __data2host8(data);
}

static char *read_string(void)
{
	char buf[BUFSIZ];
	char *str = NULL;
	int size = 0;
	int i;
	int r;

	for (;;) {
		r = read(input_fd, buf, BUFSIZ);
		if (r < 0)
			die("reading input file");

		if (!r)
			die("no data");

		for (i = 0; i < r; i++) {
			if (!buf[i])
				break;
		}
		if (i < r)
			break;

		if (str) {
			size += BUFSIZ;
			str = realloc(str, size);
			if (!str)
				die("malloc of size %d", size);
			memcpy(str + (size - BUFSIZ), buf, BUFSIZ);
		} else {
			size = BUFSIZ;
			str = malloc_or_die(size);
			memcpy(str, buf, size);
		}
	}

	/* trailing \0: */
	i++;

	/* move the file descriptor to the end of the string */
	r = lseek(input_fd, -(r - i), SEEK_CUR);
	if (r < 0)
		die("lseek");

	if (str) {
		size += i;
		str = realloc(str, size);
		if (!str)
			die("malloc of size %d", size);
		memcpy(str + (size - i), buf, i);
	} else {
		size = i;
		str = malloc_or_die(i);
		memcpy(str, buf, i);
	}

	return str;
}

static void read_proc_kallsyms(void)
{
	unsigned int size;
	char *buf;

	size = read4();
	if (!size)
		return;

	buf = malloc_or_die(size);
	read_or_die(buf, size);

	parse_proc_kallsyms(buf, size);

	free(buf);
}

static void read_ftrace_printk(void)
{
	unsigned int size;
	char *buf;

	size = read4();
	if (!size)
		return;

	buf = malloc_or_die(size);
	read_or_die(buf, size);

	parse_ftrace_printk(buf, size);

	free(buf);
}

static void read_header_files(void)
{
	unsigned long long size;
	char *header_page;
	char *header_event;
	char buf[BUFSIZ];

	read_or_die(buf, 12);

	if (memcmp(buf, "header_page", 12) != 0)
		die("did not read header page");

	size = read8();
	header_page = malloc_or_die(size);
	read_or_die(header_page, size);
	parse_header_page(header_page, size);
	free(header_page);

	/*
	 * The size field in the page is of type long,
	 * use that instead, since it represents the kernel.
	 */
	long_size = header_page_size_size;

	read_or_die(buf, 13);
	if (memcmp(buf, "header_event", 13) != 0)
		die("did not read header event");

	size = read8();
	header_event = malloc_or_die(size);
	read_or_die(header_event, size);
	free(header_event);
}

static void read_ftrace_file(unsigned long long size)
{
	char *buf;

	buf = malloc_or_die(size);
	read_or_die(buf, size);
	parse_ftrace_file(buf, size);
	free(buf);
}

static void read_event_file(char *sys, unsigned long long size)
{
	char *buf;

	buf = malloc_or_die(size);
	read_or_die(buf, size);
	parse_event_file(buf, size, sys);
	free(buf);
}

static void read_ftrace_files(void)
{
	unsigned long long size;
	int count;
	int i;

	count = read4();

	for (i = 0; i < count; i++) {
		size = read8();
		read_ftrace_file(size);
	}
}

static void read_event_files(void)
{
	unsigned long long size;
	char *sys;
	int systems;
	int count;
	int i,x;

	systems = read4();

	for (i = 0; i < systems; i++) {
		sys = read_string();

		count = read4();
		for (x=0; x < count; x++) {
			size = read8();
			read_event_file(sys, size);
		}
	}
}

struct cpu_data {
	unsigned long long	offset;
	unsigned long long	size;
	unsigned long long	timestamp;
	struct record		*next;
	char			*page;
	int			cpu;
	int			index;
	int			page_size;
};

static struct cpu_data *cpu_data;

static void update_cpu_data_index(int cpu)
{
	cpu_data[cpu].offset += page_size;
	cpu_data[cpu].size -= page_size;
	cpu_data[cpu].index = 0;
}

static void get_next_page(int cpu)
{
	off64_t save_seek;
	off64_t ret;

	if (!cpu_data[cpu].page)
		return;

	if (read_page) {
		if (cpu_data[cpu].size <= page_size) {
			free(cpu_data[cpu].page);
			cpu_data[cpu].page = NULL;
			return;
		}

		update_cpu_data_index(cpu);

		/* other parts of the code may expect the pointer to not move */
		save_seek = lseek64(input_fd, 0, SEEK_CUR);

		ret = lseek64(input_fd, cpu_data[cpu].offset, SEEK_SET);
		if (ret < 0)
			die("failed to lseek");
		ret = read(input_fd, cpu_data[cpu].page, page_size);
		if (ret < 0)
			die("failed to read page");

		/* reset the file pointer back */
		lseek64(input_fd, save_seek, SEEK_SET);

		return;
	}

	munmap(cpu_data[cpu].page, page_size);
	cpu_data[cpu].page = NULL;

	if (cpu_data[cpu].size <= page_size)
		return;

	update_cpu_data_index(cpu);

	cpu_data[cpu].page = mmap(NULL, page_size, PROT_READ, MAP_PRIVATE,
				  input_fd, cpu_data[cpu].offset);
	if (cpu_data[cpu].page == MAP_FAILED)
		die("failed to mmap cpu %d at offset 0x%llx",
		    cpu, cpu_data[cpu].offset);
}

static unsigned int type_len4host(unsigned int type_len_ts)
{
	if (file_bigendian)
		return (type_len_ts >> 27) & ((1 << 5) - 1);
	else
		return type_len_ts & ((1 << 5) - 1);
}

static unsigned int ts4host(unsigned int type_len_ts)
{
	if (file_bigendian)
		return type_len_ts & ((1 << 27) - 1);
	else
		return type_len_ts >> 5;
}

static int calc_index(void *ptr, int cpu)
{
	return (unsigned long)ptr - (unsigned long)cpu_data[cpu].page;
}

struct record *trace_peek_data(int cpu)
{
	struct record *data;
	void *page = cpu_data[cpu].page;
	int idx = cpu_data[cpu].index;
	void *ptr = page + idx;
	unsigned long long extend;
	unsigned int type_len_ts;
	unsigned int type_len;
	unsigned int delta;
	unsigned int length = 0;

	if (cpu_data[cpu].next)
		return cpu_data[cpu].next;

	if (!page)
		return NULL;

	if (!idx) {
		/* FIXME: handle header page */
		if (header_page_ts_size != 8)
			die("expected a long long type for timestamp");
		cpu_data[cpu].timestamp = data2host8(ptr);
		ptr += 8;
		switch (header_page_size_size) {
		case 4:
			cpu_data[cpu].page_size = data2host4(ptr);
			ptr += 4;
			break;
		case 8:
			cpu_data[cpu].page_size = data2host8(ptr);
			ptr += 8;
			break;
		default:
			die("bad long size");
		}
		ptr = cpu_data[cpu].page + header_page_data_offset;
	}

read_again:
	idx = calc_index(ptr, cpu);

	if (idx >= cpu_data[cpu].page_size) {
		get_next_page(cpu);
		return trace_peek_data(cpu);
	}

	type_len_ts = data2host4(ptr);
	ptr += 4;

	type_len = type_len4host(type_len_ts);
	delta = ts4host(type_len_ts);

	switch (type_len) {
	case RINGBUF_TYPE_PADDING:
		if (!delta)
			die("error, hit unexpected end of page");
		length = data2host4(ptr);
		ptr += 4;
		length *= 4;
		ptr += length;
		goto read_again;

	case RINGBUF_TYPE_TIME_EXTEND:
		extend = data2host4(ptr);
		ptr += 4;
		extend <<= TS_SHIFT;
		extend += delta;
		cpu_data[cpu].timestamp += extend;
		goto read_again;

	case RINGBUF_TYPE_TIME_STAMP:
		ptr += 12;
		break;
	case 0:
		length = data2host4(ptr);
		ptr += 4;
		die("here! length=%d", length);
		break;
	default:
		length = type_len * 4;
		break;
	}

	cpu_data[cpu].timestamp += delta;

	data = malloc_or_die(sizeof(*data));
	memset(data, 0, sizeof(*data));

	data->ts = cpu_data[cpu].timestamp;
	data->size = length;
	data->data = ptr;
	ptr += length;

	cpu_data[cpu].index = calc_index(ptr, cpu);
	cpu_data[cpu].next = data;

	return data;
}

struct record *trace_read_data(int cpu)
{
	struct record *data;

	data = trace_peek_data(cpu);
	cpu_data[cpu].next = NULL;

	return data;
}

void trace_report(void)
{
	const char *input_file = "trace.info";
	char buf[BUFSIZ];
	char test[] = { 23, 8, 68 };
	char *version;
	int show_version = 0;
	int show_funcs = 0;
	int show_printk = 0;

	input_fd = open(input_file, O_RDONLY);
	if (input_fd < 0)
		die("opening '%s'\n", input_file);

	read_or_die(buf, 3);
	if (memcmp(buf, test, 3) != 0)
		die("not an trace data file");

	read_or_die(buf, 7);
	if (memcmp(buf, "tracing", 7) != 0)
		die("not a trace file (missing tracing)");

	version = read_string();
	if (show_version)
		printf("version = %s\n", version);
	free(version);

	read_or_die(buf, 1);
	file_bigendian = buf[0];
	host_bigendian = bigendian();

	read_or_die(buf, 1);
	long_size = buf[0];

	page_size = read4();

	read_header_files();

	read_ftrace_files();
	read_event_files();
	read_proc_kallsyms();
	read_ftrace_printk();

	if (show_funcs) {
		print_funcs();
		return;
	}
	if (show_printk) {
		print_printk();
		return;
	}

	return;
}
