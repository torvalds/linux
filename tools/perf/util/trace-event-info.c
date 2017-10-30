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
#include <api/fs/tracing_path.h>
#include "evsel.h"
#include "debug.h"

#define VERSION "0.6"

static int output_fd;


int bigendian(void)
{
	unsigned char str[] = { 0x1, 0x2, 0x3, 0x4, 0x0, 0x0, 0x0, 0x0};
	unsigned int *ptr;

	ptr = (unsigned int *)(void *)str;
	return *ptr == 0x01020304;
}

/* unfortunately, you can not stat debugfs or proc files for size */
static int record_file(const char *file, ssize_t hdr_sz)
{
	unsigned long long size = 0;
	char buf[BUFSIZ], *sizep;
	off_t hdr_pos = lseek(output_fd, 0, SEEK_CUR);
	int r, fd;
	int err = -EIO;

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		pr_debug("Can't read '%s'", file);
		return -errno;
	}

	/* put in zeros for file size, then fill true size later */
	if (hdr_sz) {
		if (write(output_fd, &size, hdr_sz) != hdr_sz)
			goto out;
	}

	do {
		r = read(fd, buf, BUFSIZ);
		if (r > 0) {
			size += r;
			if (write(output_fd, buf, r) != r)
				goto out;
		}
	} while (r > 0);

	/* ugh, handle big-endian hdr_size == 4 */
	sizep = (char*)&size;
	if (bigendian())
		sizep += sizeof(u64) - hdr_sz;

	if (hdr_sz && pwrite(output_fd, sizep, hdr_sz, hdr_pos) < 0) {
		pr_debug("writing file size failed\n");
		goto out;
	}

	err = 0;
out:
	close(fd);
	return err;
}

static int record_header_files(void)
{
	char *path;
	struct stat st;
	int err = -EIO;

	path = get_tracing_file("events/header_page");
	if (!path) {
		pr_debug("can't get tracing/events/header_page");
		return -ENOMEM;
	}

	if (stat(path, &st) < 0) {
		pr_debug("can't read '%s'", path);
		goto out;
	}

	if (write(output_fd, "header_page", 12) != 12) {
		pr_debug("can't write header_page\n");
		goto out;
	}

	if (record_file(path, 8) < 0) {
		pr_debug("can't record header_page file\n");
		goto out;
	}

	put_tracing_file(path);

	path = get_tracing_file("events/header_event");
	if (!path) {
		pr_debug("can't get tracing/events/header_event");
		err = -ENOMEM;
		goto out;
	}

	if (stat(path, &st) < 0) {
		pr_debug("can't read '%s'", path);
		goto out;
	}

	if (write(output_fd, "header_event", 13) != 13) {
		pr_debug("can't write header_event\n");
		goto out;
	}

	if (record_file(path, 8) < 0) {
		pr_debug("can't record header_event file\n");
		goto out;
	}

	err = 0;
out:
	put_tracing_file(path);
	return err;
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

#define for_each_event(dir, dent, tps)				\
	while ((dent = readdir(dir)))				\
		if (dent->d_type == DT_DIR &&			\
		    (strcmp(dent->d_name, ".")) &&		\
		    (strcmp(dent->d_name, "..")))		\

static int copy_event_system(const char *sys, struct tracepoint_path *tps)
{
	struct dirent *dent;
	struct stat st;
	char *format;
	DIR *dir;
	int count = 0;
	int ret;
	int err;

	dir = opendir(sys);
	if (!dir) {
		pr_debug("can't read directory '%s'", sys);
		return -errno;
	}

	for_each_event(dir, dent, tps) {
		if (!name_in_tp_list(dent->d_name, tps))
			continue;

		if (asprintf(&format, "%s/%s/format", sys, dent->d_name) < 0) {
			err = -ENOMEM;
			goto out;
		}
		ret = stat(format, &st);
		free(format);
		if (ret < 0)
			continue;
		count++;
	}

	if (write(output_fd, &count, 4) != 4) {
		err = -EIO;
		pr_debug("can't write count\n");
		goto out;
	}

	rewinddir(dir);
	for_each_event(dir, dent, tps) {
		if (!name_in_tp_list(dent->d_name, tps))
			continue;

		if (asprintf(&format, "%s/%s/format", sys, dent->d_name) < 0) {
			err = -ENOMEM;
			goto out;
		}
		ret = stat(format, &st);

		if (ret >= 0) {
			err = record_file(format, 8);
			if (err) {
				free(format);
				goto out;
			}
		}
		free(format);
	}
	err = 0;
out:
	closedir(dir);
	return err;
}

static int record_ftrace_files(struct tracepoint_path *tps)
{
	char *path;
	int ret;

	path = get_tracing_file("events/ftrace");
	if (!path) {
		pr_debug("can't get tracing/events/ftrace");
		return -ENOMEM;
	}

	ret = copy_event_system(path, tps);

	put_tracing_file(path);

	return ret;
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

static int record_event_files(struct tracepoint_path *tps)
{
	struct dirent *dent;
	struct stat st;
	char *path;
	char *sys;
	DIR *dir;
	int count = 0;
	int ret;
	int err;

	path = get_tracing_file("events");
	if (!path) {
		pr_debug("can't get tracing/events");
		return -ENOMEM;
	}

	dir = opendir(path);
	if (!dir) {
		err = -errno;
		pr_debug("can't read directory '%s'", path);
		goto out;
	}

	for_each_event(dir, dent, tps) {
		if (strcmp(dent->d_name, "ftrace") == 0 ||
		    !system_in_tp_list(dent->d_name, tps))
			continue;

		count++;
	}

	if (write(output_fd, &count, 4) != 4) {
		err = -EIO;
		pr_debug("can't write count\n");
		goto out;
	}

	rewinddir(dir);
	for_each_event(dir, dent, tps) {
		if (strcmp(dent->d_name, "ftrace") == 0 ||
		    !system_in_tp_list(dent->d_name, tps))
			continue;

		if (asprintf(&sys, "%s/%s", path, dent->d_name) < 0) {
			err = -ENOMEM;
			goto out;
		}
		ret = stat(sys, &st);
		if (ret >= 0) {
			ssize_t size = strlen(dent->d_name) + 1;

			if (write(output_fd, dent->d_name, size) != size ||
			    copy_event_system(sys, tps) < 0) {
				err = -EIO;
				free(sys);
				goto out;
			}
		}
		free(sys);
	}
	err = 0;
out:
	closedir(dir);
	put_tracing_file(path);

	return err;
}

static int record_proc_kallsyms(void)
{
	unsigned long long size = 0;
	/*
	 * Just to keep older perf.data file parsers happy, record a zero
	 * sized kallsyms file, i.e. do the same thing that was done when
	 * /proc/kallsyms (or something specified via --kallsyms, in a
	 * different path) couldn't be read.
	 */
	return write(output_fd, &size, 4) != 4 ? -EIO : 0;
}

static int record_ftrace_printk(void)
{
	unsigned int size;
	char *path;
	struct stat st;
	int ret, err = 0;

	path = get_tracing_file("printk_formats");
	if (!path) {
		pr_debug("can't get tracing/printk_formats");
		return -ENOMEM;
	}

	ret = stat(path, &st);
	if (ret < 0) {
		/* not found */
		size = 0;
		if (write(output_fd, &size, 4) != 4)
			err = -EIO;
		goto out;
	}
	err = record_file(path, 4);

out:
	put_tracing_file(path);
	return err;
}

static int record_saved_cmdline(void)
{
	unsigned int size;
	char *path;
	struct stat st;
	int ret, err = 0;

	path = get_tracing_file("saved_cmdlines");
	if (!path) {
		pr_debug("can't get tracing/saved_cmdline");
		return -ENOMEM;
	}

	ret = stat(path, &st);
	if (ret < 0) {
		/* not found */
		size = 0;
		if (write(output_fd, &size, 8) != 8)
			err = -EIO;
		goto out;
	}
	err = record_file(path, 8);

out:
	put_tracing_file(path);
	return err;
}

static void
put_tracepoints_path(struct tracepoint_path *tps)
{
	while (tps) {
		struct tracepoint_path *t = tps;

		tps = tps->next;
		zfree(&t->name);
		zfree(&t->system);
		free(t);
	}
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

		if (pos->name) {
			ppath->next = tracepoint_name_to_path(pos->name);
			if (ppath->next)
				goto next;

			if (strchr(pos->name, ':') == NULL)
				goto try_id;

			goto error;
		}

try_id:
		ppath->next = tracepoint_id_to_path(pos->attr.config);
		if (!ppath->next) {
error:
			pr_debug("No memory to alloc tracepoints list\n");
			put_tracepoints_path(&path);
			return NULL;
		}
next:
		ppath = ppath->next;
	}

	return nr_tracepoints > 0 ? path.next : NULL;
}

bool have_tracepoints(struct list_head *pattrs)
{
	struct perf_evsel *pos;

	list_for_each_entry(pos, pattrs, node)
		if (pos->attr.type == PERF_TYPE_TRACEPOINT)
			return true;

	return false;
}

static int tracing_data_header(void)
{
	char buf[20];
	ssize_t size;

	/* just guessing this is someone's birthday.. ;) */
	buf[0] = 23;
	buf[1] = 8;
	buf[2] = 68;
	memcpy(buf + 3, "tracing", 7);

	if (write(output_fd, buf, 10) != 10)
		return -1;

	size = strlen(VERSION) + 1;
	if (write(output_fd, VERSION, size) != size)
		return -1;

	/* save endian */
	if (bigendian())
		buf[0] = 1;
	else
		buf[0] = 0;

	if (write(output_fd, buf, 1) != 1)
		return -1;

	/* save size of long */
	buf[0] = sizeof(long);
	if (write(output_fd, buf, 1) != 1)
		return -1;

	/* save page_size */
	if (write(output_fd, &page_size, 4) != 4)
		return -1;

	return 0;
}

struct tracing_data *tracing_data_get(struct list_head *pattrs,
				      int fd, bool temp)
{
	struct tracepoint_path *tps;
	struct tracing_data *tdata;
	int err;

	output_fd = fd;

	tps = get_tracepoints_path(pattrs);
	if (!tps)
		return NULL;

	tdata = malloc(sizeof(*tdata));
	if (!tdata)
		return NULL;

	tdata->temp = temp;
	tdata->size = 0;

	if (temp) {
		int temp_fd;

		snprintf(tdata->temp_file, sizeof(tdata->temp_file),
			 "/tmp/perf-XXXXXX");
		if (!mkstemp(tdata->temp_file)) {
			pr_debug("Can't make temp file");
			return NULL;
		}

		temp_fd = open(tdata->temp_file, O_RDWR);
		if (temp_fd < 0) {
			pr_debug("Can't read '%s'", tdata->temp_file);
			return NULL;
		}

		/*
		 * Set the temp file the default output, so all the
		 * tracing data are stored into it.
		 */
		output_fd = temp_fd;
	}

	err = tracing_data_header();
	if (err)
		goto out;
	err = record_header_files();
	if (err)
		goto out;
	err = record_ftrace_files(tps);
	if (err)
		goto out;
	err = record_event_files(tps);
	if (err)
		goto out;
	err = record_proc_kallsyms();
	if (err)
		goto out;
	err = record_ftrace_printk();
	if (err)
		goto out;
	err = record_saved_cmdline();

out:
	/*
	 * All tracing data are stored by now, we can restore
	 * the default output file in case we used temp file.
	 */
	if (temp) {
		tdata->size = lseek(output_fd, 0, SEEK_CUR);
		close(output_fd);
		output_fd = fd;
	}

	if (err)
		zfree(&tdata);

	put_tracepoints_path(tps);
	return tdata;
}

int tracing_data_put(struct tracing_data *tdata)
{
	int err = 0;

	if (tdata->temp) {
		err = record_file(tdata->temp_file, 0);
		unlink(tdata->temp_file);
	}

	free(tdata);
	return err;
}

int read_tracing_data(int fd, struct list_head *pattrs)
{
	int err;
	struct tracing_data *tdata;

	/*
	 * We work over the real file, so we can write data
	 * directly, no temp file is needed.
	 */
	tdata = tracing_data_get(pattrs, fd, false);
	if (!tdata)
		return -ENOMEM;

	err = tracing_data_put(tdata);
	return err;
}
