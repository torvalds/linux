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
#define _GNU_SOURCE
#include <dirent.h>
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
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>

#include "../perf.h"
#include "trace-event.h"


#define VERSION "0.5"

#define _STR(x) #x
#define STR(x) _STR(x)
#define MAX_PATH 256

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



static void die(const char *fmt, ...)
{
	va_list ap;
	int ret = errno;

	if (errno)
		perror("trace-cmd");
	else
		ret = -1;

	va_start(ap, fmt);
	fprintf(stderr, "  ");
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	fprintf(stderr, "\n");
	exit(ret);
}

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
	static char debugfs[MAX_PATH+1];
	static int debugfs_found;
	char type[100];
	FILE *fp;

	if (debugfs_found)
		return debugfs;

	if ((fp = fopen("/proc/mounts","r")) == NULL)
		die("Can't open /proc/mounts for read");

	while (fscanf(fp, "%*s %"
		      STR(MAX_PATH)
		      "s %99s %*s %*d %*d\n",
		      debugfs, type) == 2) {
		if (strcmp(type, "debugfs") == 0)
			break;
	}
	fclose(fp);

	if (strcmp(type, "debugfs") != 0)
		die("debugfs not mounted, please mount");

	debugfs_found = 1;

	return debugfs;
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

static ssize_t write_or_die(const void *buf, size_t len)
{
	int ret;

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

static unsigned long long copy_file_fd(int fd)
{
	unsigned long long size = 0;
	char buf[BUFSIZ];
	int r;

	do {
		r = read(fd, buf, BUFSIZ);
		if (r > 0) {
			size += r;
			write_or_die(buf, r);
		}
	} while (r > 0);

	return size;
}

static unsigned long long copy_file(const char *file)
{
	unsigned long long size = 0;
	int fd;

	fd = open(file, O_RDONLY);
	if (fd < 0)
		die("Can't read '%s'", file);
	size = copy_file_fd(fd);
	close(fd);

	return size;
}

static unsigned long get_size_fd(int fd)
{
	unsigned long long size = 0;
	char buf[BUFSIZ];
	int r;

	do {
		r = read(fd, buf, BUFSIZ);
		if (r > 0)
			size += r;
	} while (r > 0);

	lseek(fd, 0, SEEK_SET);

	return size;
}

static unsigned long get_size(const char *file)
{
	unsigned long long size = 0;
	int fd;

	fd = open(file, O_RDONLY);
	if (fd < 0)
		die("Can't read '%s'", file);
	size = get_size_fd(fd);
	close(fd);

	return size;
}

static void read_header_files(void)
{
	unsigned long long size, check_size;
	char *path;
	int fd;

	path = get_tracing_file("events/header_page");
	fd = open(path, O_RDONLY);
	if (fd < 0)
		die("can't read '%s'", path);

	/* unfortunately, you can not stat debugfs files for size */
	size = get_size_fd(fd);

	write_or_die("header_page", 12);
	write_or_die(&size, 8);
	check_size = copy_file_fd(fd);
	if (size != check_size)
		die("wrong size for '%s' size=%lld read=%lld",
		    path, size, check_size);
	put_tracing_file(path);

	path = get_tracing_file("events/header_event");
	fd = open(path, O_RDONLY);
	if (fd < 0)
		die("can't read '%s'", path);

	size = get_size_fd(fd);

	write_or_die("header_event", 13);
	write_or_die(&size, 8);
	check_size = copy_file_fd(fd);
	if (size != check_size)
		die("wrong size for '%s'", path);
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
	unsigned long long size, check_size;
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
		if (strcmp(dent->d_name, ".") == 0 ||
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
		if (strcmp(dent->d_name, ".") == 0 ||
		    strcmp(dent->d_name, "..") == 0 ||
		    !name_in_tp_list(dent->d_name, tps))
			continue;
		format = malloc_or_die(strlen(sys) + strlen(dent->d_name) + 10);
		sprintf(format, "%s/%s/format", sys, dent->d_name);
		ret = stat(format, &st);

		if (ret >= 0) {
			/* unfortunately, you can not stat debugfs files for size */
			size = get_size(format);
			write_or_die(&size, 8);
			check_size = copy_file(format);
			if (size != check_size)
				die("error in size of file '%s'", format);
		}

		free(format);
	}
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
		if (strcmp(dent->d_name, ".") == 0 ||
		    strcmp(dent->d_name, "..") == 0 ||
		    strcmp(dent->d_name, "ftrace") == 0 ||
		    !system_in_tp_list(dent->d_name, tps))
			continue;
		sys = malloc_or_die(strlen(path) + strlen(dent->d_name) + 2);
		sprintf(sys, "%s/%s", path, dent->d_name);
		ret = stat(sys, &st);
		free(sys);
		if (ret < 0)
			continue;
		if (S_ISDIR(st.st_mode))
			count++;
	}

	write_or_die(&count, 4);

	rewinddir(dir);
	while ((dent = readdir(dir))) {
		if (strcmp(dent->d_name, ".") == 0 ||
		    strcmp(dent->d_name, "..") == 0 ||
		    strcmp(dent->d_name, "ftrace") == 0 ||
		    !system_in_tp_list(dent->d_name, tps))
			continue;
		sys = malloc_or_die(strlen(path) + strlen(dent->d_name) + 2);
		sprintf(sys, "%s/%s", path, dent->d_name);
		ret = stat(sys, &st);
		if (ret >= 0) {
			if (S_ISDIR(st.st_mode)) {
				write_or_die(dent->d_name, strlen(dent->d_name) + 1);
				copy_event_system(sys, tps);
			}
		}
		free(sys);
	}

	put_tracing_file(path);
}

static void read_proc_kallsyms(void)
{
	unsigned int size, check_size;
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
	size = get_size(path);
	write_or_die(&size, 4);
	check_size = copy_file(path);
	if (size != check_size)
		die("error in size of file '%s'", path);

}

static void read_ftrace_printk(void)
{
	unsigned int size, check_size;
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
	size = get_size(path);
	write_or_die(&size, 4);
	check_size = copy_file(path);
	if (size != check_size)
		die("error in size of file '%s'", path);
out:
	put_tracing_file(path);
}

static struct tracepoint_path *
get_tracepoints_path(struct perf_event_attr *pattrs, int nb_events)
{
	struct tracepoint_path path, *ppath = &path;
	int i;

	for (i = 0; i < nb_events; i++) {
		if (pattrs[i].type != PERF_TYPE_TRACEPOINT)
			continue;
		ppath->next = tracepoint_id_to_path(pattrs[i].config);
		if (!ppath->next)
			die("%s\n", "No memory to alloc tracepoints list");
		ppath = ppath->next;
	}

	return path.next;
}
void read_tracing_data(struct perf_event_attr *pattrs, int nb_events)
{
	char buf[BUFSIZ];
	struct tracepoint_path *tps;

	output_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC | O_LARGEFILE, 0644);
	if (output_fd < 0)
		die("creating file '%s'", output_file);

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
	page_size = getpagesize();
	write_or_die(&page_size, 4);

	tps = get_tracepoints_path(pattrs, nb_events);

	read_header_files();
	read_ftrace_files(tps);
	read_event_files(tps);
	read_proc_kallsyms();
	read_ftrace_printk();
}
