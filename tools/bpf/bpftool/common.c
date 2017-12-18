/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Author: Jakub Kicinski <kubakici@wp.pl> */

#include <errno.h>
#include <fts.h>
#include <libgen.h>
#include <mntent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/limits.h>
#include <linux/magic.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/vfs.h>

#include <bpf.h>

#include "main.h"

void p_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (json_output) {
		jsonw_start_object(json_wtr);
		jsonw_name(json_wtr, "error");
		jsonw_vprintf_enquote(json_wtr, fmt, ap);
		jsonw_end_object(json_wtr);
	} else {
		fprintf(stderr, "Error: ");
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
	}
	va_end(ap);
}

void p_info(const char *fmt, ...)
{
	va_list ap;

	if (json_output)
		return;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

static bool is_bpffs(char *path)
{
	struct statfs st_fs;

	if (statfs(path, &st_fs) < 0)
		return false;

	return (unsigned long)st_fs.f_type == BPF_FS_MAGIC;
}

static int mnt_bpffs(const char *target, char *buff, size_t bufflen)
{
	bool bind_done = false;

	while (mount("", target, "none", MS_PRIVATE | MS_REC, NULL)) {
		if (errno != EINVAL || bind_done) {
			snprintf(buff, bufflen,
				 "mount --make-private %s failed: %s",
				 target, strerror(errno));
			return -1;
		}

		if (mount(target, target, "none", MS_BIND, NULL)) {
			snprintf(buff, bufflen,
				 "mount --bind %s %s failed: %s",
				 target, target, strerror(errno));
			return -1;
		}

		bind_done = true;
	}

	if (mount("bpf", target, "bpf", 0, "mode=0700")) {
		snprintf(buff, bufflen, "mount -t bpf bpf %s failed: %s",
			 target, strerror(errno));
		return -1;
	}

	return 0;
}

int open_obj_pinned(char *path)
{
	int fd;

	fd = bpf_obj_get(path);
	if (fd < 0) {
		p_err("bpf obj get (%s): %s", path,
		      errno == EACCES && !is_bpffs(dirname(path)) ?
		    "directory not in bpf file system (bpffs)" :
		    strerror(errno));
		return -1;
	}

	return fd;
}

int open_obj_pinned_any(char *path, enum bpf_obj_type exp_type)
{
	enum bpf_obj_type type;
	int fd;

	fd = open_obj_pinned(path);
	if (fd < 0)
		return -1;

	type = get_fd_type(fd);
	if (type < 0) {
		close(fd);
		return type;
	}
	if (type != exp_type) {
		p_err("incorrect object type: %s", get_fd_type_name(type));
		close(fd);
		return -1;
	}

	return fd;
}

int do_pin_fd(int fd, const char *name)
{
	char err_str[ERR_MAX_LEN];
	char *file;
	char *dir;
	int err = 0;

	err = bpf_obj_pin(fd, name);
	if (!err)
		goto out;

	file = malloc(strlen(name) + 1);
	strcpy(file, name);
	dir = dirname(file);

	if (errno != EPERM || is_bpffs(dir)) {
		p_err("can't pin the object (%s): %s", name, strerror(errno));
		goto out_free;
	}

	/* Attempt to mount bpffs, then retry pinning. */
	err = mnt_bpffs(dir, err_str, ERR_MAX_LEN);
	if (!err) {
		err = bpf_obj_pin(fd, name);
		if (err)
			p_err("can't pin the object (%s): %s", name,
			      strerror(errno));
	} else {
		err_str[ERR_MAX_LEN - 1] = '\0';
		p_err("can't mount BPF file system to pin the object (%s): %s",
		      name, err_str);
	}

out_free:
	free(file);
out:
	return err;
}

int do_pin_any(int argc, char **argv, int (*get_fd_by_id)(__u32))
{
	unsigned int id;
	char *endptr;
	int err;
	int fd;

	if (!is_prefix(*argv, "id")) {
		p_err("expected 'id' got %s", *argv);
		return -1;
	}
	NEXT_ARG();

	id = strtoul(*argv, &endptr, 0);
	if (*endptr) {
		p_err("can't parse %s as ID", *argv);
		return -1;
	}
	NEXT_ARG();

	if (argc != 1)
		usage();

	fd = get_fd_by_id(id);
	if (fd < 0) {
		p_err("can't get prog by id (%u): %s", id, strerror(errno));
		return -1;
	}

	err = do_pin_fd(fd, *argv);

	close(fd);
	return err;
}

const char *get_fd_type_name(enum bpf_obj_type type)
{
	static const char * const names[] = {
		[BPF_OBJ_UNKNOWN]	= "unknown",
		[BPF_OBJ_PROG]		= "prog",
		[BPF_OBJ_MAP]		= "map",
	};

	if (type < 0 || type >= ARRAY_SIZE(names) || !names[type])
		return names[BPF_OBJ_UNKNOWN];

	return names[type];
}

int get_fd_type(int fd)
{
	char path[PATH_MAX];
	char buf[512];
	ssize_t n;

	snprintf(path, sizeof(path), "/proc/%d/fd/%d", getpid(), fd);

	n = readlink(path, buf, sizeof(buf));
	if (n < 0) {
		p_err("can't read link type: %s", strerror(errno));
		return -1;
	}
	if (n == sizeof(path)) {
		p_err("can't read link type: path too long!");
		return -1;
	}

	if (strstr(buf, "bpf-map"))
		return BPF_OBJ_MAP;
	else if (strstr(buf, "bpf-prog"))
		return BPF_OBJ_PROG;

	return BPF_OBJ_UNKNOWN;
}

char *get_fdinfo(int fd, const char *key)
{
	char path[PATH_MAX];
	char *line = NULL;
	size_t line_n = 0;
	ssize_t n;
	FILE *fdi;

	snprintf(path, sizeof(path), "/proc/%d/fdinfo/%d", getpid(), fd);

	fdi = fopen(path, "r");
	if (!fdi) {
		p_err("can't open fdinfo: %s", strerror(errno));
		return NULL;
	}

	while ((n = getline(&line, &line_n, fdi))) {
		char *value;
		int len;

		if (!strstr(line, key))
			continue;

		fclose(fdi);

		value = strchr(line, '\t');
		if (!value || !value[1]) {
			p_err("malformed fdinfo!?");
			free(line);
			return NULL;
		}
		value++;

		len = strlen(value);
		memmove(line, value, len);
		line[len - 1] = '\0';

		return line;
	}

	p_err("key '%s' not found in fdinfo", key);
	free(line);
	fclose(fdi);
	return NULL;
}

void print_hex_data_json(uint8_t *data, size_t len)
{
	unsigned int i;

	jsonw_start_array(json_wtr);
	for (i = 0; i < len; i++)
		jsonw_printf(json_wtr, "\"0x%02hhx\"", data[i]);
	jsonw_end_array(json_wtr);
}

int build_pinned_obj_table(struct pinned_obj_table *tab,
			   enum bpf_obj_type type)
{
	struct bpf_prog_info pinned_info = {};
	struct pinned_obj *obj_node = NULL;
	__u32 len = sizeof(pinned_info);
	struct mntent *mntent = NULL;
	enum bpf_obj_type objtype;
	FILE *mntfile = NULL;
	FTSENT *ftse = NULL;
	FTS *fts = NULL;
	int fd, err;

	mntfile = setmntent("/proc/mounts", "r");
	if (!mntfile)
		return -1;

	while ((mntent = getmntent(mntfile))) {
		char *path[] = { mntent->mnt_dir, NULL };

		if (strncmp(mntent->mnt_type, "bpf", 3) != 0)
			continue;

		fts = fts_open(path, 0, NULL);
		if (!fts)
			continue;

		while ((ftse = fts_read(fts))) {
			if (!(ftse->fts_info & FTS_F))
				continue;
			fd = open_obj_pinned(ftse->fts_path);
			if (fd < 0)
				continue;

			objtype = get_fd_type(fd);
			if (objtype != type) {
				close(fd);
				continue;
			}
			memset(&pinned_info, 0, sizeof(pinned_info));
			err = bpf_obj_get_info_by_fd(fd, &pinned_info, &len);
			if (err) {
				close(fd);
				continue;
			}

			obj_node = malloc(sizeof(*obj_node));
			if (!obj_node) {
				close(fd);
				fts_close(fts);
				fclose(mntfile);
				return -1;
			}

			memset(obj_node, 0, sizeof(*obj_node));
			obj_node->id = pinned_info.id;
			obj_node->path = strdup(ftse->fts_path);
			hash_add(tab->table, &obj_node->hash, obj_node->id);

			close(fd);
		}
		fts_close(fts);
	}
	fclose(mntfile);
	return 0;
}

void delete_pinned_obj_table(struct pinned_obj_table *tab)
{
	struct pinned_obj *obj;
	struct hlist_node *tmp;
	unsigned int bkt;

	hash_for_each_safe(tab->table, bkt, tmp, obj, hash) {
		hash_del(&obj->hash);
		free(obj->path);
		free(obj);
	}
}
