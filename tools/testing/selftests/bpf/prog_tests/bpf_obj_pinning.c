// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */
#define _GNU_SOURCE
#include <test_progs.h>
#include <bpf/btf.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <linux/mount.h>
#include <sys/syscall.h>
#include "bpf/libbpf_internal.h"

static inline int sys_fsopen(const char *fsname, unsigned flags)
{
	return syscall(__NR_fsopen, fsname, flags);
}

static inline int sys_fsconfig(int fs_fd, unsigned cmd, const char *key, const void *val, int aux)
{
	return syscall(__NR_fsconfig, fs_fd, cmd, key, val, aux);
}

static inline int sys_fsmount(int fs_fd, unsigned flags, unsigned ms_flags)
{
	return syscall(__NR_fsmount, fs_fd, flags, ms_flags);
}

__attribute__((unused))
static inline int sys_move_mount(int from_dfd, const char *from_path,
			         int to_dfd, const char *to_path,
			         unsigned int ms_flags)
{
	return syscall(__NR_move_mount, from_dfd, from_path, to_dfd, to_path, ms_flags);
}

static void bpf_obj_pinning_detached(void)
{
	LIBBPF_OPTS(bpf_obj_pin_opts, pin_opts);
	LIBBPF_OPTS(bpf_obj_get_opts, get_opts);
	int fs_fd = -1, mnt_fd = -1;
	int map_fd = -1, map_fd2 = -1;
	int zero = 0, src_value, dst_value, err;
	const char *map_name = "fsmount_map";

	/* A bunch of below UAPI calls are constructed based on reading:
	 * https://brauner.io/2023/02/28/mounting-into-mount-namespaces.html
	 */

	/* create VFS context */
	fs_fd = sys_fsopen("bpf", 0);
	if (!ASSERT_GE(fs_fd, 0, "fs_fd"))
		goto cleanup;

	/* instantiate FS object */
	err = sys_fsconfig(fs_fd, FSCONFIG_CMD_CREATE, NULL, NULL, 0);
	if (!ASSERT_OK(err, "fs_create"))
		goto cleanup;

	/* create O_PATH fd for detached mount */
	mnt_fd = sys_fsmount(fs_fd, 0, 0);
	if (!ASSERT_GE(mnt_fd, 0, "mnt_fd"))
		goto cleanup;

	/* If we wanted to expose detached mount in the file system, we'd do
	 * something like below. But the whole point is that we actually don't
	 * even have to expose BPF FS in the file system to be able to work
	 * (pin/get objects) with it.
	 *
	 * err = sys_move_mount(mnt_fd, "", -EBADF, mnt_path, MOVE_MOUNT_F_EMPTY_PATH);
	 * if (!ASSERT_OK(err, "move_mount"))
	 *	goto cleanup;
	 */

	/* create BPF map to pin */
	map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, map_name, 4, 4, 1, NULL);
	if (!ASSERT_GE(map_fd, 0, "map_fd"))
		goto cleanup;

	/* pin BPF map into detached BPF FS through mnt_fd */
	pin_opts.file_flags = BPF_F_PATH_FD;
	pin_opts.path_fd = mnt_fd;
	err = bpf_obj_pin_opts(map_fd, map_name, &pin_opts);
	if (!ASSERT_OK(err, "map_pin"))
		goto cleanup;

	/* get BPF map from detached BPF FS through mnt_fd */
	get_opts.file_flags = BPF_F_PATH_FD;
	get_opts.path_fd = mnt_fd;
	map_fd2 = bpf_obj_get_opts(map_name, &get_opts);
	if (!ASSERT_GE(map_fd2, 0, "map_get"))
		goto cleanup;

	/* update map through one FD */
	src_value = 0xcafebeef;
	err = bpf_map_update_elem(map_fd, &zero, &src_value, 0);
	ASSERT_OK(err, "map_update");

	/* check values written/read through different FDs do match */
	dst_value = 0;
	err = bpf_map_lookup_elem(map_fd2, &zero, &dst_value);
	ASSERT_OK(err, "map_lookup");
	ASSERT_EQ(dst_value, src_value, "map_value_eq1");
	ASSERT_EQ(dst_value, 0xcafebeef, "map_value_eq2");

cleanup:
	if (map_fd >= 0)
		ASSERT_OK(close(map_fd), "close_map_fd");
	if (map_fd2 >= 0)
		ASSERT_OK(close(map_fd2), "close_map_fd2");
	if (fs_fd >= 0)
		ASSERT_OK(close(fs_fd), "close_fs_fd");
	if (mnt_fd >= 0)
		ASSERT_OK(close(mnt_fd), "close_mnt_fd");
}

enum path_kind
{
	PATH_STR_ABS,
	PATH_STR_REL,
	PATH_FD_REL,
};

static void validate_pin(int map_fd, const char *map_name, int src_value,
			 enum path_kind path_kind)
{
	LIBBPF_OPTS(bpf_obj_pin_opts, pin_opts);
	char abs_path[PATH_MAX], old_cwd[PATH_MAX];
	const char *pin_path = NULL;
	int zero = 0, dst_value, map_fd2, err;

	snprintf(abs_path, sizeof(abs_path), "/sys/fs/bpf/%s", map_name);
	old_cwd[0] = '\0';

	switch (path_kind) {
	case PATH_STR_ABS:
		/* absolute path */
		pin_path = abs_path;
		break;
	case PATH_STR_REL:
		/* cwd + relative path */
		ASSERT_OK_PTR(getcwd(old_cwd, sizeof(old_cwd)), "getcwd");
		ASSERT_OK(chdir("/sys/fs/bpf"), "chdir");
		pin_path = map_name;
		break;
	case PATH_FD_REL:
		/* dir fd + relative path */
		pin_opts.file_flags = BPF_F_PATH_FD;
		pin_opts.path_fd = open("/sys/fs/bpf", O_PATH);
		ASSERT_GE(pin_opts.path_fd, 0, "path_fd");
		pin_path = map_name;
		break;
	}

	/* pin BPF map using specified path definition */
	err = bpf_obj_pin_opts(map_fd, pin_path, &pin_opts);
	ASSERT_OK(err, "obj_pin");

	/* cleanup */
	if (path_kind == PATH_FD_REL && pin_opts.path_fd >= 0)
		close(pin_opts.path_fd);
	if (old_cwd[0])
		ASSERT_OK(chdir(old_cwd), "restore_cwd");

	map_fd2 = bpf_obj_get(abs_path);
	if (!ASSERT_GE(map_fd2, 0, "map_get"))
		goto cleanup;

	/* update map through one FD */
	err = bpf_map_update_elem(map_fd, &zero, &src_value, 0);
	ASSERT_OK(err, "map_update");

	/* check values written/read through different FDs do match */
	dst_value = 0;
	err = bpf_map_lookup_elem(map_fd2, &zero, &dst_value);
	ASSERT_OK(err, "map_lookup");
	ASSERT_EQ(dst_value, src_value, "map_value_eq");
cleanup:
	if (map_fd2 >= 0)
		ASSERT_OK(close(map_fd2), "close_map_fd2");
	unlink(abs_path);
}

static void validate_get(int map_fd, const char *map_name, int src_value,
			 enum path_kind path_kind)
{
	LIBBPF_OPTS(bpf_obj_get_opts, get_opts);
	char abs_path[PATH_MAX], old_cwd[PATH_MAX];
	const char *pin_path = NULL;
	int zero = 0, dst_value, map_fd2, err;

	snprintf(abs_path, sizeof(abs_path), "/sys/fs/bpf/%s", map_name);
	/* pin BPF map using specified path definition */
	err = bpf_obj_pin(map_fd, abs_path);
	if (!ASSERT_OK(err, "pin_map"))
		return;

	old_cwd[0] = '\0';

	switch (path_kind) {
	case PATH_STR_ABS:
		/* absolute path */
		pin_path = abs_path;
		break;
	case PATH_STR_REL:
		/* cwd + relative path */
		ASSERT_OK_PTR(getcwd(old_cwd, sizeof(old_cwd)), "getcwd");
		ASSERT_OK(chdir("/sys/fs/bpf"), "chdir");
		pin_path = map_name;
		break;
	case PATH_FD_REL:
		/* dir fd + relative path */
		get_opts.file_flags = BPF_F_PATH_FD;
		get_opts.path_fd = open("/sys/fs/bpf", O_PATH);
		ASSERT_GE(get_opts.path_fd, 0, "path_fd");
		pin_path = map_name;
		break;
	}

	map_fd2 = bpf_obj_get_opts(pin_path, &get_opts);
	if (!ASSERT_GE(map_fd2, 0, "map_get"))
		goto cleanup;

	/* cleanup */
	if (path_kind == PATH_FD_REL && get_opts.path_fd >= 0)
		close(get_opts.path_fd);
	if (old_cwd[0])
		ASSERT_OK(chdir(old_cwd), "restore_cwd");

	/* update map through one FD */
	err = bpf_map_update_elem(map_fd, &zero, &src_value, 0);
	ASSERT_OK(err, "map_update");

	/* check values written/read through different FDs do match */
	dst_value = 0;
	err = bpf_map_lookup_elem(map_fd2, &zero, &dst_value);
	ASSERT_OK(err, "map_lookup");
	ASSERT_EQ(dst_value, src_value, "map_value_eq");
cleanup:
	if (map_fd2 >= 0)
		ASSERT_OK(close(map_fd2), "close_map_fd2");
	unlink(abs_path);
}

static void bpf_obj_pinning_mounted(enum path_kind path_kind)
{
	const char *map_name = "mounted_map";
	int map_fd;

	/* create BPF map to pin */
	map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, map_name, 4, 4, 1, NULL);
	if (!ASSERT_GE(map_fd, 0, "map_fd"))
		return;

	validate_pin(map_fd, map_name, 100 + (int)path_kind, path_kind);
	validate_get(map_fd, map_name, 200 + (int)path_kind, path_kind);
	ASSERT_OK(close(map_fd), "close_map_fd");
}

void test_bpf_obj_pinning()
{
	if (test__start_subtest("detached"))
		bpf_obj_pinning_detached();
	if (test__start_subtest("mounted-str-abs"))
		bpf_obj_pinning_mounted(PATH_STR_ABS);
	if (test__start_subtest("mounted-str-rel"))
		bpf_obj_pinning_mounted(PATH_STR_REL);
	if (test__start_subtest("mounted-fd-rel"))
		bpf_obj_pinning_mounted(PATH_FD_REL);
}
