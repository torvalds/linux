// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
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
#include <net/if.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/vfs.h>

#include <linux/filter.h>
#include <linux/limits.h>
#include <linux/magic.h>
#include <linux/unistd.h>

#include <bpf/bpf.h>
#include <bpf/hashmap.h>
#include <bpf/libbpf.h> /* libbpf_num_possible_cpus */
#include <bpf/btf.h>

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

static bool is_bpffs(const char *path)
{
	struct statfs st_fs;

	if (statfs(path, &st_fs) < 0)
		return false;

	return (unsigned long)st_fs.f_type == BPF_FS_MAGIC;
}

/* Probe whether kernel switched from memlock-based (RLIMIT_MEMLOCK) to
 * memcg-based memory accounting for BPF maps and programs. This was done in
 * commit 97306be45fbe ("Merge branch 'switch to memcg-based memory
 * accounting'"), in Linux 5.11.
 *
 * Libbpf also offers to probe for memcg-based accounting vs rlimit, but does
 * so by checking for the availability of a given BPF helper and this has
 * failed on some kernels with backports in the past, see commit 6b4384ff1088
 * ("Revert "bpftool: Use libbpf 1.0 API mode instead of RLIMIT_MEMLOCK"").
 * Instead, we can probe by lowering the process-based rlimit to 0, trying to
 * load a BPF object, and resetting the rlimit. If the load succeeds then
 * memcg-based accounting is supported.
 *
 * This would be too dangerous to do in the library, because multithreaded
 * applications might attempt to load items while the rlimit is at 0. Given
 * that bpftool is single-threaded, this is fine to do here.
 */
static bool known_to_need_rlimit(void)
{
	struct rlimit rlim_init, rlim_cur_zero = {};
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	size_t insn_cnt = ARRAY_SIZE(insns);
	union bpf_attr attr;
	int prog_fd, err;

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	attr.insns = ptr_to_u64(insns);
	attr.insn_cnt = insn_cnt;
	attr.license = ptr_to_u64("GPL");

	if (getrlimit(RLIMIT_MEMLOCK, &rlim_init))
		return false;

	/* Drop the soft limit to zero. We maintain the hard limit to its
	 * current value, because lowering it would be a permanent operation
	 * for unprivileged users.
	 */
	rlim_cur_zero.rlim_max = rlim_init.rlim_max;
	if (setrlimit(RLIMIT_MEMLOCK, &rlim_cur_zero))
		return false;

	/* Do not use bpf_prog_load() from libbpf here, because it calls
	 * bump_rlimit_memlock(), interfering with the current probe.
	 */
	prog_fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
	err = errno;

	/* reset soft rlimit to its initial value */
	setrlimit(RLIMIT_MEMLOCK, &rlim_init);

	if (prog_fd < 0)
		return err == EPERM;

	close(prog_fd);
	return false;
}

void set_max_rlimit(void)
{
	struct rlimit rinf = { RLIM_INFINITY, RLIM_INFINITY };

	if (known_to_need_rlimit())
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

int create_and_mount_bpffs_dir(const char *dir_name)
{
	char err_str[ERR_MAX_LEN];
	bool dir_exists;
	int err = 0;

	if (is_bpffs(dir_name))
		return err;

	dir_exists = access(dir_name, F_OK) == 0;

	if (!dir_exists) {
		char *temp_name;
		char *parent_name;

		temp_name = strdup(dir_name);
		if (!temp_name) {
			p_err("mem alloc failed");
			return -1;
		}

		parent_name = dirname(temp_name);

		if (is_bpffs(parent_name)) {
			/* nothing to do if already mounted */
			free(temp_name);
			return err;
		}

		if (access(parent_name, F_OK) == -1) {
			p_err("can't create dir '%s' to pin BPF object: parent dir '%s' doesn't exist",
			      dir_name, parent_name);
			free(temp_name);
			return -1;
		}

		free(temp_name);
	}

	if (block_mount) {
		p_err("no BPF file system found, not mounting it due to --nomount option");
		return -1;
	}

	if (!dir_exists) {
		err = mkdir(dir_name, S_IRWXU);
		if (err) {
			p_err("failed to create dir '%s': %s", dir_name, strerror(errno));
			return err;
		}
	}

	err = mnt_fs(dir_name, "bpf", err_str, ERR_MAX_LEN);
	if (err) {
		err_str[ERR_MAX_LEN - 1] = '\0';
		p_err("can't mount BPF file system on given dir '%s': %s",
		      dir_name, err_str);

		if (!dir_exists)
			rmdir(dir_name);
	}

	return err;
}

int mount_bpffs_for_file(const char *file_name)
{
	char err_str[ERR_MAX_LEN];
	char *temp_name;
	char *dir;
	int err = 0;

	if (access(file_name, F_OK) != -1) {
		p_err("can't pin BPF object: path '%s' already exists", file_name);
		return -1;
	}

	temp_name = strdup(file_name);
	if (!temp_name) {
		p_err("mem alloc failed");
		return -1;
	}

	dir = dirname(temp_name);

	if (is_bpffs(dir))
		/* nothing to do if already mounted */
		goto out_free;

	if (access(dir, F_OK) == -1) {
		p_err("can't pin BPF object: dir '%s' doesn't exist", dir);
		err = -1;
		goto out_free;
	}

	if (block_mount) {
		p_err("no BPF file system found, not mounting it due to --nomount option");
		err = -1;
		goto out_free;
	}

	err = mnt_fs(dir, "bpf", err_str, ERR_MAX_LEN);
	if (err) {
		err_str[ERR_MAX_LEN - 1] = '\0';
		p_err("can't mount BPF file system to pin the object '%s': %s",
		      file_name, err_str);
	}

out_free:
	free(temp_name);
	return err;
}

int do_pin_fd(int fd, const char *name)
{
	int err;

	err = mount_bpffs_for_file(name);
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

	if (!REQ_ARGS(3))
		return -EINVAL;

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
		[BPF_OBJ_LINK]		= "link",
	};

	if (type < 0 || type >= ARRAY_SIZE(names) || !names[type])
		return names[BPF_OBJ_UNKNOWN];

	return names[type];
}

void get_prog_full_name(const struct bpf_prog_info *prog_info, int prog_fd,
			char *name_buff, size_t buff_len)
{
	const char *prog_name = prog_info->name;
	const struct btf_type *func_type;
	struct bpf_func_info finfo = {};
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	struct btf *prog_btf = NULL;

	if (buff_len <= BPF_OBJ_NAME_LEN ||
	    strlen(prog_info->name) < BPF_OBJ_NAME_LEN - 1)
		goto copy_name;

	if (!prog_info->btf_id || prog_info->nr_func_info == 0)
		goto copy_name;

	info.nr_func_info = 1;
	info.func_info_rec_size = prog_info->func_info_rec_size;
	if (info.func_info_rec_size > sizeof(finfo))
		info.func_info_rec_size = sizeof(finfo);
	info.func_info = ptr_to_u64(&finfo);

	if (bpf_prog_get_info_by_fd(prog_fd, &info, &info_len))
		goto copy_name;

	prog_btf = btf__load_from_kernel_by_id(info.btf_id);
	if (!prog_btf)
		goto copy_name;

	func_type = btf__type_by_id(prog_btf, finfo.type_id);
	if (!func_type || !btf_is_func(func_type))
		goto copy_name;

	prog_name = btf__name_by_offset(prog_btf, func_type->name_off);

copy_name:
	snprintf(name_buff, buff_len, "%s", prog_name);

	if (prog_btf)
		btf__free(prog_btf);
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
	if (bpf_prog_get_info_by_fd(fd, &pinned_info, &len))
		goto out_close;

	path = strdup(fpath);
	if (!path) {
		err = -1;
		goto out_close;
	}

	err = hashmap__append(build_fn_table, pinned_info.id, path);
	if (err) {
		p_err("failed to append entry to hashmap for ID %u, path '%s': %s",
		      pinned_info.id, path, strerror(errno));
		free(path);
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
		free(entry->pvalue);

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
ifindex_to_arch(__u32 ifindex, __u64 ns_dev, __u64 ns_ino, const char **opt)
{
	__maybe_unused int device_id;
	char devname[IF_NAMESIZE];
	int vendor_id;

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
#ifdef HAVE_LIBBFD_SUPPORT
	case 0x19ee:
		device_id = read_sysfs_netdev_hex_int(devname, "device");
		if (device_id != 0x4000 &&
		    device_id != 0x6000 &&
		    device_id != 0x6003)
			p_info("Unknown NFP device ID, assuming it is NFP-6xxx arch");
		*opt = "ctx4";
		return "NFP-6xxx";
#endif /* HAVE_LIBBFD_SUPPORT */
	/* No NFP support in LLVM, we have no valid triple to return. */
	default:
		p_err("Can't get arch name for device vendor id 0x%04x",
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
	char prog_name[MAX_PROG_FULL_NAME];
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

		err = bpf_prog_get_info_by_fd(fd, &info, &len);
		if (err) {
			p_err("can't get prog info (%u): %s",
			      id, strerror(errno));
			goto err_close_fd;
		}

		if (tag && memcmp(nametag, info.tag, BPF_TAG_SIZE)) {
			close(fd);
			continue;
		}

		if (!tag) {
			get_prog_full_name(&info, fd, prog_name,
					   sizeof(prog_name));
			if (strncmp(nametag, prog_name, sizeof(prog_name))) {
				close(fd);
				continue;
			}
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
		if (strlen(name) > MAX_PROG_FULL_NAME - 1) {
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

		err = bpf_map_get_info_by_fd(fd, &info, &len);
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

int map_parse_fd_and_info(int *argc, char ***argv, struct bpf_map_info *info,
			  __u32 *info_len)
{
	int err;
	int fd;

	fd = map_parse_fd(argc, argv);
	if (fd < 0)
		return -1;

	err = bpf_map_get_info_by_fd(fd, info, info_len);
	if (err) {
		p_err("can't get map info: %s", strerror(errno));
		close(fd);
		return err;
	}

	return fd;
}

size_t hash_fn_for_key_as_id(long key, void *ctx)
{
	return key;
}

bool equal_fn_for_key_as_id(long k1, long k2, void *ctx)
{
	return k1 == k2;
}

const char *bpf_attach_type_input_str(enum bpf_attach_type t)
{
	switch (t) {
	case BPF_CGROUP_INET_INGRESS:		return "ingress";
	case BPF_CGROUP_INET_EGRESS:		return "egress";
	case BPF_CGROUP_INET_SOCK_CREATE:	return "sock_create";
	case BPF_CGROUP_INET_SOCK_RELEASE:	return "sock_release";
	case BPF_CGROUP_SOCK_OPS:		return "sock_ops";
	case BPF_CGROUP_DEVICE:			return "device";
	case BPF_CGROUP_INET4_BIND:		return "bind4";
	case BPF_CGROUP_INET6_BIND:		return "bind6";
	case BPF_CGROUP_INET4_CONNECT:		return "connect4";
	case BPF_CGROUP_INET6_CONNECT:		return "connect6";
	case BPF_CGROUP_INET4_POST_BIND:	return "post_bind4";
	case BPF_CGROUP_INET6_POST_BIND:	return "post_bind6";
	case BPF_CGROUP_INET4_GETPEERNAME:	return "getpeername4";
	case BPF_CGROUP_INET6_GETPEERNAME:	return "getpeername6";
	case BPF_CGROUP_INET4_GETSOCKNAME:	return "getsockname4";
	case BPF_CGROUP_INET6_GETSOCKNAME:	return "getsockname6";
	case BPF_CGROUP_UDP4_SENDMSG:		return "sendmsg4";
	case BPF_CGROUP_UDP6_SENDMSG:		return "sendmsg6";
	case BPF_CGROUP_SYSCTL:			return "sysctl";
	case BPF_CGROUP_UDP4_RECVMSG:		return "recvmsg4";
	case BPF_CGROUP_UDP6_RECVMSG:		return "recvmsg6";
	case BPF_CGROUP_GETSOCKOPT:		return "getsockopt";
	case BPF_CGROUP_SETSOCKOPT:		return "setsockopt";
	case BPF_TRACE_RAW_TP:			return "raw_tp";
	case BPF_TRACE_FENTRY:			return "fentry";
	case BPF_TRACE_FEXIT:			return "fexit";
	case BPF_MODIFY_RETURN:			return "mod_ret";
	case BPF_SK_REUSEPORT_SELECT:		return "sk_skb_reuseport_select";
	case BPF_SK_REUSEPORT_SELECT_OR_MIGRATE:	return "sk_skb_reuseport_select_or_migrate";
	default:	return libbpf_bpf_attach_type_str(t);
	}
}

int pathname_concat(char *buf, int buf_sz, const char *path,
		    const char *name)
{
	int len;

	len = snprintf(buf, buf_sz, "%s/%s", path, name);
	if (len < 0)
		return -EINVAL;
	if (len >= buf_sz)
		return -ENAMETOOLONG;

	return 0;
}
