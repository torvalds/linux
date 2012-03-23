/*
 * Copyright (C) 2008,2009, Steven Rostedt <srostedt@redhat.com>
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
#include "util.h"
#include <dirent.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <linux/list.h>
#include <linux/kernel.h>

#include "../perf.h"
#include "trace-event.h"
#include "debugfs.h"
#include "evsel.h"

#define VERSION "0.5"

#define TRACE_CTRL	"tracing_on"
#define TRACE		"trace"
#define AVAILABLE	"available_tracers"
#define CURRENT		"current_tracer"
#define ITER_CTRL	"trace_options"
#define MAX_LATENCY	"tracing_max_latency"

unsigned int page_size;

static const char *output_file = "trace.info";
static int output_fd;

struct event_list {
	struct event_list *next;
	const char *event;
};

struct events {
	struct events *sibling;
	struct events *children;
	struct events *next;
	char *name;
};


void *malloc_or_die(unsigned int size)
{
	void *data;

	data = malloc(size);
	if (!data)
		die("malloc");
	return data;
}

static const char *find_debugfs(void)
{
	const char *path = debugfs_mount(NULL);

	if (!path)
		die("Your kernel not support debugfs filesystem");

	return path;
}

/*
 * Finds the path to the debugfs/tracing
 * Allocates the string and stores it.
 */
static const char *find_tracing_dir(void)
{
	static char *tracing;
	static int tracing_found;
	const char *debugfs;

	if (tracing_found)
		return tracing;

	debugfs = find_debugfs();

	tracing = malloc_or_die(strlen(debugfs) + 9);

	sprintf(tracing, "%s/tracing", debugfs);

	tracing_found = 1;
	return tracing;
}

static char *get_tracing_file(const char *name)
{
	const char *tracing;
	char *file;

	tracing = find_tracing_dir();
	if (!tracing)
		return NULL;

	file = malloc_or_die(strlen(tracing) + strlen(name) + 2);

	sprintf(file, "%s/%s", tracing, name);
	return file;
}

static void put_tracing_file(char *file)
{
	free(file);
}

static ssize_t calc_data_size;

static ssize_t write_or_die(const void *buf, size_t len)
{
	int ret;

	if (calc_data_size) {
		calc_data_size += len;
		return len;
	}

	ret = write(output_fd, buf, len);
	if (ret < 0)
		die("writing to '%s'", output_file);

	return ret;
}

int bigendian(void)
{
	unsigned char str[] = { 0x1, 0x2, 0x3, 0x4, 0x0, 0x0, 0x0, 0x0};
	unsigned int *ptr;

	ptr = (unsigned int *)(void *)str;
	return *ptr == 0x01020304;
}

/* unfortunately, you can not stat debugfs or proc files for size */
static void record_file(const char *file, size_t hdr_sz)
{
	unsigned long long size = 0;
	char buf[BUFSIZ], *sizep;
	off_t hdr_pos = lseek(output_fd, 0, SEEK_CUR);
	int r, fd;

	fd = open(file, O_RDONLY);
	if (fd < 0)
		die("Can't read '%s'", file);

	/* put in zeros for file size, then fill true size later */
	if (hdr_sz)
		write_or_die(&size, hdr_sz);

	do {
		r = read(fd, buf, BUFSIZ);
		if (r > 0) {
			size += r;
			write_or_die(buf, r);
		}
	} while (r > 0);
	close(fd);

	/* ugh, handle big-endian hdr_size == 4 */
	sizep = (char*)&size;
	if (bigendian())
		sizep += sizeof(u64) - hdr_sz;

	if (hdr_sz && pwrite(output_fd, sizep, hdr_sz, hdr_pos) < 0)
		die("writing to %s", output_file);
}

static void read_header_files(void)
{
	char *path;
	struct stat st;

	path = get_tracing_file("events/header_page");
	if (stat(path, &st) < 0)
		die("can't read '%s'", path);

	write_or_die("header_page", 12);
	record_file(path, 8);
	put_tracing_file(path);

	path = get_tracing_file("events/header_event");
	if (stat(path, &st) < 0)
		die("can't read '%s'", path);

	write_or_die("header_event", 13);
	record_file(path, 8);
	put_tracing_file(path);
}

static bool name_in_tp_list(char *sys, struct tracepoint_path *tps)
{
	while (tps) {
		if (!strcmp(sys, tps->name))
			return true;
		tps = tps->next;
	}

	return false;
}

static void copy_event_system(const char *sys, struct tracepoint_path *tps)
{
	struct dirent *dent;
	struct stat st;
	char *format;
	DIR *dir;
	int count = 0;
	int ret;

	dir = opendir(sys);
	if (!dir)
		die("can't read directory '%s'", sys);

	while ((dent = readdir(dir))) {
		if (dent->d_type != DT_DIR ||
		    strcmp(dent->d_name, ".") == 0 ||
		    strcmp(dent->d_name, "..") == 0 ||
		    !name_in_tp_list(dent->d_name, tps))
			continue;
		format = malloc_or_die(strlen(sys) + strlen(dent->d_name) + 10);
		sprintf(format, "%s/%s/format", sys, dent->d_name);
		ret = stat(format, &st);
		free(format);
		if (ret < 0)
			continue;
		count++;
	}

	write_or_die(&count, 4);

	rewinddir(dir);
	while ((dent = readdir(dir))) {
		if (dent->d_type != DT_DIR ||
		    strcmp(dent->d_name, ".") == 0 ||
		    strcmp(dent->d_name, "..") == 0 ||
		    !name_in_tp_list(dent->d_name, tps))
			continue;
		format = malloc_or_die(strlen(sys) + strlen(dent->d_name) + 10);
		sprintf(format, "%s/%s/format", sys, dent->d_name);
		ret = stat(format, &st);

		if (ret >= 0)
			record_file(format, 8);

		free(format);
	}
	closedir(dir);
}

static void read_ftrace_files(struct tracepoint_path *tps)
{
	char *path;

	path = get_tracing_file("events/ftrace");

	copy_event_system(path, tps);

	put_tracing_file(path);
}

static bool system_in_tp_list(char *sys, struct tracepoint_path *tps)
{
	while (tps) {
		if (!strcmp(sys, tps->system))
			return true;
		tps = tps->next;
	}

	return false;
}

static void read_event_files(struct tracepoint_path *tps)
{
	struct dirent *dent;
	struct stat st;
	char *path;
	char *sys;
	DIR *dir;
	int count = 0;
	int ret;

	path = get_tracing_file("events");

	dir = opendir(path);
	if (!dir)
		die("can't read directory '%s'", path);

	while ((dent = readdir(dir))) {
		if (dent->d_type != DT_DIR ||
		    strcmp(dent->d_name, ".") == 0 ||
		    strcmp(dent->d_name, "..") == 0 ||
		    strcmp(dent->d_name, "ftrace") == 0 ||
		    !system_in_tp_list(dent->d_name, tps))
			continue;
		count++;
	}

	write_or_die(&count, 4);

	rewinddir(dir);
	while ((dent = readdir(dir))) {
		if (dent->d_type != DT_DIR ||
		    strcmp(dent->d_name, ".") == 0 ||
		    strcmp(dent->d_name, "..") == 0 ||
		    strcmp(dent->d_name, "ftrace") == 0 ||
		    !system_in_tp_list(dent->d_name, tps))
			continue;
		sys = malloc_or_die(strlen(path) + strlen(dent->d_name) + 2);
		sprintf(sys, "%s/%s", path, dent->d_name);
		ret = stat(sys, &st);
		if (ret >= 0) {
			write_or_die(dent->d_name, strlen(dent->d_name) + 1);
			copy_event_system(sys, tps);
		}
		free(sys);
	}

	closedir(dir);
	put_tracing_file(path);
}

static void read_proc_kallsyms(void)
{
	unsigned int size;
	const char *path = "/proc/kallsyms";
	struct stat st;
	int ret;

	ret = stat(path, &st);
	if (ret < 0) {
		/* not found */
		size = 0;
		write_or_die(&size, 4);
		return;
	}
	record_file(path, 4);
}

static void read_ftrace_printk(void)
{
	unsigned int size;
	char *path;
	struct stat st;
	int ret;

	path = get_tracing_file("printk_formats");
	ret = stat(path, &st);
	if (ret < 0) {
		/* not found */
		size = 0;
		write_or_die(&size, 4);
		goto out;
	}
	record_file(path, 4);

out:
	put_tracing_file(path);
}

static struct tracepoint_path *
get_tracepoints_path(struct list_head *pattrs)
{
	struct tracepoint_path path, *ppath = &path;
	struct perf_evsel *pos;
	int nr_tracepoints = 0;

	list_for_each_entry(pos, pattrs, node) {
		if (pos->attr.type != PERF_TYPE_TRACEPOINT)
			continue;
		++nr_tracepoints;
		ppath->next = tracepoint_id_to_path(pos->attr.config);
		if (!ppath->next)
			die("%s\n", "No memory to alloc tracepoints list");
		ppath = ppath->next;
	}

	return nr_tracepoints > 0 ? path.next : NULL;
}

static void
put_tracepoints_path(struct tracepoint_path *tps)
{
	while (tps) {
		struct tracepoint_path *t = tps;

		tps = tps->next;
		free(t->name);
		free(t->system);
		free(t);
	}
}

bool have_tracepoints(struct list_head *pattrs)
{
	struct perf_evsel *pos;

	list_for_each_entry(pos, pattrs, node)
		if (pos->attr.type == PERF_TYPE_TRACEPOINT)
			return true;

	return false;
}

static void tracing_data_header(void)
{
	char buf[20];

	/* just guessing this is someone's birthday.. ;) */
	buf[0] = 23;
	buf[1] = 8;
	buf[2] = 68;
	memcpy(buf + 3, "tracing", 7);

	write_or_die(buf, 10);

	write_or_die(VERSION, strlen(VERSION) + 1);

	/* save endian */
	if (bigendian())
		buf[0] = 1;
	else
		buf[0] = 0;

	write_or_die(buf, 1);

	/* save size of long */
	buf[0] = sizeof(long);
	write_or_die(buf, 1);

	/* save page_size */
	page_size = sysconf(_SC_PAGESIZE);
	write_or_die(&page_size, 4);
}

struct tracing_data *tracing_data_get(struct list_head *pattrs,
				      int fd, bool temp)
{
	struct tracepoint_path *tps;
	struct tracing_data *tdata;

	output_fd = fd;

	tps = get_tracepoints_path(pattrs);
	if (!tps)
		return NULL;

	tdata = malloc_or_die(sizeof(*tdata));
	tdata->temp = temp;
	tdata->size = 0;

	if (temp) {
		int temp_fd;

		snprintf(tdata->temp_file, sizeof(tdata->temp_file),
			 "/tmp/perf-XXXXXX");
		if (!mkstemp(tdata->temp_file))
			die("Can't make temp file");

		temp_fd = open(tdata->temp_file, O_RDWR);
		if (temp_fd < 0)
			die("Can't read '%s'", tdata->temp_file);

		/*
		 * Set the temp file the default output, so all the
		 * tracing data are stored into it.
		 */
		output_fd = temp_fd;
	}

	tracing_data_header();
	read_header_files();
	read_ftrace_files(tps);
	read_event_files(tps);
	read_proc_kallsyms();
	read_ftrace_printk();

	/*
	 * All tracing data are stored by now, we can restore
	 * the default output file in case we used temp file.
	 */
	if (temp) {
		tdata->size = lseek(output_fd, 0, SEEK_CUR);
		close(output_fd);
		output_fd = fd;
	}

	put_tracepoints_path(tps);
	return tdata;
}

void tracing_data_put(struct tracing_data *tdata)
{
	if (tdata->temp) {
		record_file(tdata->temp_file, 0);
		unlink(tdata->temp_file);
	}

	free(tdata);
}

int read_tracing_data(int fd, struct list_head *pattrs)
{
	struct tracing_data *tdata;

	/*
	 * We work over the real file, so we can write data
	 * directly, no temp file is needed.
	 */
	tdata = tracing_data_get(pattrs, fd, false);
	if (!tdata)
		return -ENOMEM;

	tracing_data_put(tdata);
	return 0;
}
