// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
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
#include <bpf/hashmap.h>
#include <bpf/libbpf.h> /* libbpf_num_possible_cpus */

#include "main.h"

#ifndef BPF_FS_MAGIC
#define BPF_FS_MAGIC		0xcafe4a11
#endif

const char * const attach_type_name[__MAX_BPF_ATTACH_TYPE] = {
	[BPF_CGROUP_INET_INGRESS]	= "ingress",
	[BPF_CGROUP_INET_EGRESS]	= "egress",
	[BPF_CGROUP_INET_SOCK_CREATE]	= "sock_create",
	[BPF_CGROUP_INET_SOCK_RELEASE]	= "sock_release",
	[BPF_CGROUP_SOCK_OPS]		= "sock_ops",
	[BPF_CGROUP_DEVICE]		= "device",
	[BPF_CGROUP_INET4_BIND]		= "bind4",
	[BPF_CGROUP_INET6_BIND]		= "bind6",
	[BPF_CGROUP_INET4_CONNECT]	= "connect4",
	[BPF_CGROUP_INET6_CONNECT]	= "connect6",
	[BPF_CGROUP_INET4_POST_BIND]	= "post_bind4",
	[BPF_CGROUP_INET6_POST_BIND]	= "post_bind6",
	[BPF_CGROUP_INET4_GETPEERNAME]	= "getpeername4",
	[BPF_CGROUP_INET6_GETPEERNAME]	= "getpeername6",
	[BPF_CGROUP_INET4_GETSOCKNAME]	= "getsockname4",
	[BPF_CGROUP_INET6_GETSOCKNAME]	= "getsockname6",
	[BPF_CGROUP_UDP4_SENDMSG]	= "sendmsg4",
	[BPF_CGROUP_UDP6_SENDMSG]	= "sendmsg6",
	[BPF_CGROUP_SYSCTL]		= "sysctl",
	[BPF_CGROUP_UDP4_RECVMSG]	= "recvmsg4",
	[BPF_CGROUP_UDP6_RECVMSG]	= "recvmsg6",
	[BPF_CGROUP_GETSOCKOPT]		= "getsockopt",
	[BPF_CGROUP_SETSOCKOPT]		= "setsockopt",

	[BPF_SK_SKB_STREAM_PARSER]	= "sk_skb_stream_parser",
	[BPF_SK_SKB_STREAM_VERDICT]	= "sk_skb_stream_verdict",
	[BPF_SK_SKB_VERDICT]		= "sk_skb_verdict",
	[BPF_SK_MSG_VERDICT]		= "sk_msg_verdict",
	[BPF_LIRC_MODE2]		= "lirc_mode2",
	[BPF_FLOW_DISSECTOR]		= "flow_dissector",
	[BPF_TRACE_RAW_TP]		= "raw_tp",
	[BPF_TRACE_FENTRY]		= "fentry",
	[BPF_TRACE_FEXIT]		= "fexit",
	[BPF_MODIFY_RETURN]		= "mod_ret",
	[BPF_LSM_MAC]			= "lsm_mac",
	[BPF_SK_LOOKUP]			= "sk_lookup",
	[BPF_TRACE_ITER]		= "trace_iter",
	[BPF_XDP_DEVMAP]		= "xdp_devmap",
	[BPF_XDP_CPUMAP]		= "xdp_cpumap",
	[BPF_XDP]			= "xdp",
	[BPF_SK_REUSEPORT_SELECT]	= "sk_skb_reuseport_select",
	[BPF_SK_REUSEPORT_SELECT_OR_MIGRATE]	= "sk_skb_reuseport_select_or_migrate",
	[BPF_PERF_EVENT]		= "perf_event",
};

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

int open_obj_pinned(const char *path, bool quiet)
{
	char *pname;
	int fd = -1;

	pname = strdup(path);
	if (!pname) {
		if (!quiet)
			p_err("mem alloc failed");
		goto out_ret;
	}

	fd = bpf_obj_get(pname);
	if (fd < 0) {
		if (!quiet)
			p_err("bpf obj get (%s): %s", pname,
			      errno == EACCES && !is_bpffs(dirname(pname)) ?
			    "directory not in bpf file system (bpffs)" :
			    strerror(errno));
		goto out_free;
	}

out_free:
	free(pname);
out_ret:
	return fd;
}

int open_obj_pinned_any(const char *path, enum bpf_obj_type exp_type)
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
	if (!file) {
		p_err("mem alloc failed");
		return -1;
	}

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

int do_pin_any(int argc, char **argv, int (*get_fd)(int *, char ***))
{
	int err;
	int fd;

	fd = get_fd(&argc, &argv);
	if (fd < 0)
		return fd;

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
	else if (strstr(buf, "bpf-link"))
		return BPF_OBJ_LINK;

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

/* extra params for nftw cb */
static struct hashmap *build_fn_table;
static enum bpf_obj_type build_fn_type;

static int do_build_table_cb(const char *fpath, const struct stat *sb,
			     int typeflag, struct FTW *ftwbuf)
{
	struct bpf_prog_info pinned_info;
	__u32 len = sizeof(pinned_info);
	enum bpf_obj_type objtype;
	int fd, err = 0;
	char *path;

	if (typeflag != FTW_F)
		goto out_ret;

	fd = open_obj_pinned(fpath, true);
	if (fd < 0)
		goto out_ret;

	objtype = get_fd_type(fd);
	if (objtype != build_fn_type)
		goto out_close;

	memset(&pinned_info, 0, sizeof(pinned_info));
	if (bpf_obj_get_info_by_fd(fd, &pinned_info, &len))
		goto out_close;

	path = strdup(fpath);
	if (!path) {
		err = -1;
		goto out_close;
	}

	err = hashmap__append(build_fn_table, u32_as_hash_field(pinned_info.id), path);
	if (err) {
		p_err("failed to append entry to hashmap for ID %u, path '%s': %s",
		      pinned_info.id, path, strerror(errno));
		goto out_close;
	}

out_close:
	close(fd);
out_ret:
	return err;
}

int build_pinned_obj_table(struct hashmap *tab,
			   enum bpf_obj_type type)
{
	struct mntent *mntent = NULL;
	FILE *mntfile = NULL;
	int flags = FTW_PHYS;
	int nopenfd = 16;
	int err = 0;

	mntfile = setmntent("/proc/mounts", "r");
	if (!mntfile)
		return -1;

	build_fn_table = tab;
	build_fn_type = type;

	while ((mntent = getmntent(mntfile))) {
		char *path = mntent->mnt_dir;

		if (strncmp(mntent->mnt_type, "bpf", 3) != 0)
			continue;
		err = nftw(path, do_build_table_cb, nopenfd, flags);
		if (err)
			break;
	}
	fclose(mntfile);
	return err;
}

void delete_pinned_obj_table(struct hashmap *map)
{
	struct hashmap_entry *entry;
	size_t bkt;

	if (!map)
		return;

	hashmap__for_each_entry(map, entry, bkt)
		free(entry->value);

	hashmap__free(map);
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

int __printf(2, 0)
print_all_levels(__maybe_unused enum libbpf_print_level level,
		 const char *format, va_list args)
{
	return vfprintf(stderr, format, args);
}

static int prog_fd_by_nametag(void *nametag, int **fds, bool tag)
{
	unsigned int id = 0;
	int fd, nb_fds = 0;
	void *tmp;
	int err;

	while (true) {
		struct bpf_prog_info info = {};
		__u32 len = sizeof(info);

		err = bpf_prog_get_next_id(id, &id);
		if (err) {
			if (errno != ENOENT) {
				p_err("%s", strerror(errno));
				goto err_close_fds;
			}
			return nb_fds;
		}

		fd = bpf_prog_get_fd_by_id(id);
		if (fd < 0) {
			p_err("can't get prog by id (%u): %s",
			      id, strerror(errno));
			goto err_close_fds;
		}

		err = bpf_obj_get_info_by_fd(fd, &info, &len);
		if (err) {
			p_err("can't get prog info (%u): %s",
			      id, strerror(errno));
			goto err_close_fd;
		}

		if ((tag && memcmp(nametag, info.tag, BPF_TAG_SIZE)) ||
		    (!tag && strncmp(nametag, info.name, BPF_OBJ_NAME_LEN))) {
			close(fd);
			continue;
		}

		if (nb_fds > 0) {
			tmp = realloc(*fds, (nb_fds + 1) * sizeof(int));
			if (!tmp) {
				p_err("failed to realloc");
				goto err_close_fd;
			}
			*fds = tmp;
		}
		(*fds)[nb_fds++] = fd;
	}

err_close_fd:
	close(fd);
err_close_fds:
	while (--nb_fds >= 0)
		close((*fds)[nb_fds]);
	return -1;
}

int prog_parse_fds(int *argc, char ***argv, int **fds)
{
	if (is_prefix(**argv, "id")) {
		unsigned int id;
		char *endptr;

		NEXT_ARGP();

		id = strtoul(**argv, &endptr, 0);
		if (*endptr) {
			p_err("can't parse %s as ID", **argv);
			return -1;
		}
		NEXT_ARGP();

		(*fds)[0] = bpf_prog_get_fd_by_id(id);
		if ((*fds)[0] < 0) {
			p_err("get by id (%u): %s", id, strerror(errno));
			return -1;
		}
		return 1;
	} else if (is_prefix(**argv, "tag")) {
		unsigned char tag[BPF_TAG_SIZE];

		NEXT_ARGP();

		if (sscanf(**argv, BPF_TAG_FMT, tag, tag + 1, tag + 2,
			   tag + 3, tag + 4, tag + 5, tag + 6, tag + 7)
		    != BPF_TAG_SIZE) {
			p_err("can't parse tag");
			return -1;
		}
		NEXT_ARGP();

		return prog_fd_by_nametag(tag, fds, true);
	} else if (is_prefix(**argv, "name")) {
		char *name;

		NEXT_ARGP();

		name = **argv;
		if (strlen(name) > BPF_OBJ_NAME_LEN - 1) {
			p_err("can't parse name");
			return -1;
		}
		NEXT_ARGP();

		return prog_fd_by_nametag(name, fds, false);
	} else if (is_prefix(**argv, "pinned")) {
		char *path;

		NEXT_ARGP();

		path = **argv;
		NEXT_ARGP();

		(*fds)[0] = open_obj_pinned_any(path, BPF_OBJ_PROG);
		if ((*fds)[0] < 0)
			return -1;
		return 1;
	}

	p_err("expected 'id', 'tag', 'name' or 'pinned', got: '%s'?", **argv);
	return -1;
}

int prog_parse_fd(int *argc, char ***argv)
{
	int *fds = NULL;
	int nb_fds, fd;

	fds = malloc(sizeof(int));
	if (!fds) {
		p_err("mem alloc failed");
		return -1;
	}
	nb_fds = prog_parse_fds(argc, argv, &fds);
	if (nb_fds != 1) {
		if (nb_fds > 1) {
			p_err("several programs match this handle");
			while (nb_fds--)
				close(fds[nb_fds]);
		}
		fd = -1;
		goto exit_free;
	}

	fd = fds[0];
exit_free:
	free(fds);
	return fd;
}

static int map_fd_by_name(char *name, int **fds)
{
	unsigned int id = 0;
	int fd, nb_fds = 0;
	void *tmp;
	int err;

	while (true) {
		struct bpf_map_info info = {};
		__u32 len = sizeof(info);

		err = bpf_map_get_next_id(id, &id);
		if (err) {
			if (errno != ENOENT) {
				p_err("%s", strerror(errno));
				goto err_close_fds;
			}
			return nb_fds;
		}

		fd = bpf_map_get_fd_by_id(id);
		if (fd < 0) {
			p_err("can't get map by id (%u): %s",
			      id, strerror(errno));
			goto err_close_fds;
		}

		err = bpf_obj_get_info_by_fd(fd, &info, &len);
		if (err) {
			p_err("can't get map info (%u): %s",
			      id, strerror(errno));
			goto err_close_fd;
		}

		if (strncmp(name, info.name, BPF_OBJ_NAME_LEN)) {
			close(fd);
			continue;
		}

		if (nb_fds > 0) {
			tmp = realloc(*fds, (nb_fds + 1) * sizeof(int));
			if (!tmp) {
				p_err("failed to realloc");
				goto err_close_fd;
			}
			*fds = tmp;
		}
		(*fds)[nb_fds++] = fd;
	}

err_close_fd:
	close(fd);
err_close_fds:
	while (--nb_fds >= 0)
		close((*fds)[nb_fds]);
	return -1;
}

int map_parse_fds(int *argc, char ***argv, int **fds)
{
	if (is_prefix(**argv, "id")) {
		unsigned int id;
		char *endptr;

		NEXT_ARGP();

		id = strtoul(**argv, &endptr, 0);
		if (*endptr) {
			p_err("can't parse %s as ID", **argv);
			return -1;
		}
		NEXT_ARGP();

		(*fds)[0] = bpf_map_get_fd_by_id(id);
		if ((*fds)[0] < 0) {
			p_err("get map by id (%u): %s", id, strerror(errno));
			return -1;
		}
		return 1;
	} else if (is_prefix(**argv, "name")) {
		char *name;

		NEXT_ARGP();

		name = **argv;
		if (strlen(name) > BPF_OBJ_NAME_LEN - 1) {
			p_err("can't parse name");
			return -1;
		}
		NEXT_ARGP();

		return map_fd_by_name(name, fds);
	} else if (is_prefix(**argv, "pinned")) {
		char *path;

		NEXT_ARGP();

		path = **argv;
		NEXT_ARGP();

		(*fds)[0] = open_obj_pinned_any(path, BPF_OBJ_MAP);
		if ((*fds)[0] < 0)
			return -1;
		return 1;
	}

	p_err("expected 'id', 'name' or 'pinned', got: '%s'?", **argv);
	return -1;
}

int map_parse_fd(int *argc, char ***argv)
{
	int *fds = NULL;
	int nb_fds, fd;

	fds = malloc(sizeof(int));
	if (!fds) {
		p_err("mem alloc failed");
		return -1;
	}
	nb_fds = map_parse_fds(argc, argv, &fds);
	if (nb_fds != 1) {
		if (nb_fds > 1) {
			p_err("several maps match this handle");
			while (nb_fds--)
				close(fds[nb_fds]);
		}
		fd = -1;
		goto exit_free;
	}

	fd = fds[0];
exit_free:
	free(fds);
	return fd;
}

int map_parse_fd_and_info(int *argc, char ***argv, void *info, __u32 *info_len)
{
	int err;
	int fd;

	fd = map_parse_fd(argc, argv);
	if (fd < 0)
		return -1;

	err = bpf_obj_get_info_by_fd(fd, info, info_len);
	if (err) {
		p_err("can't get map info: %s", strerror(errno));
		close(fd);
		return err;
	}

	return fd;
}

size_t hash_fn_for_key_as_id(const void *key, void *ctx)
{
	return (size_t)key;
}

bool equal_fn_for_key_as_id(const void *k1, const void *k2, void *ctx)
{
	return k1 == k2;
}
