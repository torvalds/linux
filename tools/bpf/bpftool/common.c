// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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
#include <net/if.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/vfs.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h> /* libbpf_num_possible_cpus */

#include "main.h"

#ifndef BPF_FS_MAGIC
#define BPF_FS_MAGIC		0xcafe4a11
#endif

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

void set_max_rlimit(void)
{
	struct rlimit rinf = { RLIM_INFINITY, RLIM_INFINITY };

	setrlimit(RLIMIT_MEMLOCK, &rinf);
}

static int
mnt_fs(const char *target, const char *type, char *buff, size_t bufflen)
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

	if (mount(type, target, type, 0, "mode=0700")) {
		snprintf(buff, bufflen, "mount -t %s %s %s failed: %s",
			 type, type, target, strerror(errno));
		return -1;
	}

	return 0;
}

int mount_tracefs(const char *target)
{
	char err_str[ERR_MAX_LEN];
	int err;

	err = mnt_fs(target, "tracefs", err_str, ERR_MAX_LEN);
	if (err) {
		err_str[ERR_MAX_LEN - 1] = '\0';
		p_err("can't mount tracefs: %s", err_str);
	}

	return err;
}

int open_obj_pinned(char *path, bool quiet)
{
	int fd;

	fd = bpf_obj_get(path);
	if (fd < 0) {
		if (!quiet)
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

	fd = open_obj_pinned(path, false);
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

int mount_bpffs_for_pin(const char *name)
{
	char err_str[ERR_MAX_LEN];
	char *file;
	char *dir;
	int err = 0;

	file = malloc(strlen(name) + 1);
	strcpy(file, name);
	dir = dirname(file);

	if (is_bpffs(dir))
		/* nothing to do if already mounted */
		goto out_free;

	if (block_mount) {
		p_err("no BPF file system found, not mounting it due to --nomount option");
		err = -1;
		goto out_free;
	}

	err = mnt_fs(dir, "bpf", err_str, ERR_MAX_LEN);
	if (err) {
		err_str[ERR_MAX_LEN - 1] = '\0';
		p_err("can't mount BPF file system to pin the object (%s): %s",
		      name, err_str);
	}

out_free:
	free(file);
	return err;
}

int do_pin_fd(int fd, const char *name)
{
	int err;

	err = mount_bpffs_for_pin(name);
	if (err)
		return err;

	err = bpf_obj_pin(fd, name);
	if (err)
		p_err("can't pin the object (%s): %s", name, strerror(errno));

	return err;
}

int do_pin_any(int argc, char **argv, int (*get_fd_by_id)(__u32))
{
	unsigned int id;
	char *endptr;
	int err;
	int fd;

	if (argc < 3) {
		p_err("too few arguments, id ID and FILE path is required");
		return -1;
	} else if (argc > 3) {
		p_err("too many arguments");
		return -1;
	}

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

	fd = get_fd_by_id(id);
	if (fd < 0) {
		p_err("can't open object by id (%u): %s", id, strerror(errno));
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

	snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

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

	snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", fd);

	fdi = fopen(path, "r");
	if (!fdi)
		return NULL;

	while ((n = getline(&line, &line_n, fdi)) > 0) {
		char *value;
		int len;

		if (!strstr(line, key))
			continue;

		fclose(fdi);

		value = strchr(line, '\t');
		if (!value || !value[1]) {
			free(line);
			return NULL;
		}
		value++;

		len = strlen(value);
		memmove(line, value, len);
		line[len - 1] = '\0';

		return line;
	}

	free(line);
	fclose(fdi);
	return NULL;
}

void print_data_json(uint8_t *data, size_t len)
{
	unsigned int i;

	jsonw_start_array(json_wtr);
	for (i = 0; i < len; i++)
		jsonw_printf(json_wtr, "%d", data[i]);
	jsonw_end_array(json_wtr);
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
			fd = open_obj_pinned(ftse->fts_path, true);
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

unsigned int get_page_size(void)
{
	static int result;

	if (!result)
		result = getpagesize();
	return result;
}

unsigned int get_possible_cpus(void)
{
	int cpus = libbpf_num_possible_cpus();

	if (cpus < 0) {
		p_err("Can't get # of possible cpus: %s", strerror(-cpus));
		exit(-1);
	}
	return cpus;
}

static char *
ifindex_to_name_ns(__u32 ifindex, __u32 ns_dev, __u32 ns_ino, char *buf)
{
	struct stat st;
	int err;

	err = stat("/proc/self/ns/net", &st);
	if (err) {
		p_err("Can't stat /proc/self: %s", strerror(errno));
		return NULL;
	}

	if (st.st_dev != ns_dev || st.st_ino != ns_ino)
		return NULL;

	return if_indextoname(ifindex, buf);
}

static int read_sysfs_hex_int(char *path)
{
	char vendor_id_buf[8];
	int len;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		p_err("Can't open %s: %s", path, strerror(errno));
		return -1;
	}

	len = read(fd, vendor_id_buf, sizeof(vendor_id_buf));
	close(fd);
	if (len < 0) {
		p_err("Can't read %s: %s", path, strerror(errno));
		return -1;
	}
	if (len >= (int)sizeof(vendor_id_buf)) {
		p_err("Value in %s too long", path);
		return -1;
	}

	vendor_id_buf[len] = 0;

	return strtol(vendor_id_buf, NULL, 0);
}

static int read_sysfs_netdev_hex_int(char *devname, const char *entry_name)
{
	char full_path[64];

	snprintf(full_path, sizeof(full_path), "/sys/class/net/%s/device/%s",
		 devname, entry_name);

	return read_sysfs_hex_int(full_path);
}

const char *
ifindex_to_bfd_params(__u32 ifindex, __u64 ns_dev, __u64 ns_ino,
		      const char **opt)
{
	char devname[IF_NAMESIZE];
	int vendor_id;
	int device_id;

	if (!ifindex_to_name_ns(ifindex, ns_dev, ns_ino, devname)) {
		p_err("Can't get net device name for ifindex %d: %s", ifindex,
		      strerror(errno));
		return NULL;
	}

	vendor_id = read_sysfs_netdev_hex_int(devname, "vendor");
	if (vendor_id < 0) {
		p_err("Can't get device vendor id for %s", devname);
		return NULL;
	}

	switch (vendor_id) {
	case 0x19ee:
		device_id = read_sysfs_netdev_hex_int(devname, "device");
		if (device_id != 0x4000 &&
		    device_id != 0x6000 &&
		    device_id != 0x6003)
			p_info("Unknown NFP device ID, assuming it is NFP-6xxx arch");
		*opt = "ctx4";
		return "NFP-6xxx";
	default:
		p_err("Can't get bfd arch name for device vendor id 0x%04x",
		      vendor_id);
		return NULL;
	}
}

void print_dev_plain(__u32 ifindex, __u64 ns_dev, __u64 ns_inode)
{
	char name[IF_NAMESIZE];

	if (!ifindex)
		return;

	printf("  offloaded_to ");
	if (ifindex_to_name_ns(ifindex, ns_dev, ns_inode, name))
		printf("%s", name);
	else
		printf("ifindex %u ns_dev %llu ns_ino %llu",
		       ifindex, ns_dev, ns_inode);
}

void print_dev_json(__u32 ifindex, __u64 ns_dev, __u64 ns_inode)
{
	char name[IF_NAMESIZE];

	if (!ifindex)
		return;

	jsonw_name(json_wtr, "dev");
	jsonw_start_object(json_wtr);
	jsonw_uint_field(json_wtr, "ifindex", ifindex);
	jsonw_uint_field(json_wtr, "ns_dev", ns_dev);
	jsonw_uint_field(json_wtr, "ns_inode", ns_inode);
	if (ifindex_to_name_ns(ifindex, ns_dev, ns_inode, name))
		jsonw_string_field(json_wtr, "ifname", name);
	jsonw_end_object(json_wtr);
}

int parse_u32_arg(int *argc, char ***argv, __u32 *val, const char *what)
{
	char *endptr;

	NEXT_ARGP();

	if (*val) {
		p_err("%s already specified", what);
		return -1;
	}

	*val = strtoul(**argv, &endptr, 0);
	if (*endptr) {
		p_err("can't parse %s as %s", **argv, what);
		return -1;
	}
	NEXT_ARGP();

	return 0;
}
