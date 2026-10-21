// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

/*
 * Copyright (C) 2020 Google LLC.
 */

#include <test_progs.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <linux/mount.h>

#include "lsm.skel.h"
#include "lsm_tailcall.skel.h"

char *CMD_ARGS[] = {"true", NULL};

enum {
	INODE_IDMAP_ALL = (1U << 6) - 1,
};

int exec_cmd(int *monitored_pid)
{
	int child_pid, child_status;

	child_pid = fork();
	if (child_pid == 0) {
		*monitored_pid = getpid();
		execvp(CMD_ARGS[0], CMD_ARGS);
		return -EINVAL;
	} else if (child_pid > 0) {
		waitpid(child_pid, &child_status, 0);
		return child_status;
	}

	return -EINVAL;
}

static ssize_t write_nointr(int fd, const void *buf, size_t count)
{
	ssize_t ret;

	do {
		ret = write(fd, buf, count);
	} while (ret < 0 && errno == EINTR);

	return ret;
}

static int write_file(const char *path, const char *value)
{
	size_t len = strlen(value);
	int fd, saved_errno = 0;
	ssize_t ret;

	fd = open(path, O_WRONLY | O_CLOEXEC | O_NOCTTY | O_NOFOLLOW);
	if (fd < 0)
		return -1;

	ret = write_nointr(fd, value, len);
	if (ret < 0)
		saved_errno = errno;
	else if ((size_t)ret != len)
		saved_errno = EIO;
	close(fd);
	if (saved_errno) {
		errno = saved_errno;
		return -1;
	}
	return 0;
}

static int write_userns_file(pid_t pid, const char *name, const char *value)
{
	char path[64];
	int len;

	len = snprintf(path, sizeof(path), "/proc/%d/%s", pid, name);
	if (len < 0 || (size_t)len >= sizeof(path)) {
		errno = EOVERFLOW;
		return -1;
	}

	return write_file(path, value);
}

static int create_userns_fd(void)
{
	char path[64];
	pid_t pid, waited;
	int fd = -1, len, saved_errno, status;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		if (unshare(CLONE_NEWUSER))
			_exit(1);
		raise(SIGSTOP);
		_exit(0);
	}

	do {
		waited = waitpid(pid, &status, WUNTRACED);
	} while (waited < 0 && errno == EINTR);
	if (waited != pid)
		goto out;
	if (!WIFSTOPPED(status)) {
		pid = -1;
		goto out;
	}

	/* A one-entry map is identity for root but remains distinct from nop_mnt_idmap. */
	if (write_userns_file(pid, "setgroups", "deny") && errno != ENOENT)
		goto out;
	if (write_userns_file(pid, "uid_map", "0 0 1") ||
	    write_userns_file(pid, "gid_map", "0 0 1"))
		goto out;

	len = snprintf(path, sizeof(path), "/proc/%d/ns/user", pid);
	if (len < 0 || (size_t)len >= sizeof(path)) {
		errno = EOVERFLOW;
		goto out;
	}
	fd = open(path, O_RDONLY | O_CLOEXEC);

out:
	saved_errno = errno;
	if (pid > 0) {
		kill(pid, SIGKILL);
		do {
			waited = waitpid(pid, NULL, 0);
		} while (waited < 0 && errno == EINTR);
	}
	errno = saved_errno;
	return fd;
}

static int create_idmapped_tmpfs(void)
{
	struct mount_attr attr = {
		.attr_set = MOUNT_ATTR_IDMAP,
	};
	int fsfd = -1, mntfd = -1, saved_errno, userns_fd = -1;

	userns_fd = create_userns_fd();
	if (userns_fd < 0)
		goto out;

	/* A detached tmpfs avoids relying on the host test directory supporting idmaps. */
	fsfd = syscall(__NR_fsopen, "tmpfs", FSOPEN_CLOEXEC);
	if (fsfd < 0)
		goto out;
	if (syscall(__NR_fsconfig, fsfd, FSCONFIG_CMD_CREATE, NULL, NULL, 0))
		goto out;

	mntfd = syscall(__NR_fsmount, fsfd, FSMOUNT_CLOEXEC, 0);
	if (mntfd < 0)
		goto out;

	attr.userns_fd = userns_fd;
	if (syscall(__NR_mount_setattr, mntfd, "", AT_EMPTY_PATH, &attr,
		    sizeof(attr))) {
		close(mntfd);
		mntfd = -1;
	}

out:
	saved_errno = errno;
	if (fsfd >= 0)
		close(fsfd);
	if (userns_fd >= 0)
		close(userns_fd);
	errno = saved_errno;
	return mntfd;
}

static int exercise_inode_idmap_hooks(int dirfd)
{
	int fd = -1, ret = -1;

	fd = openat(dirfd, "file", O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC,
		    0600);
	if (!ASSERT_GE(fd, 0, "create"))
		goto out;
	close(fd);
	fd = -1;

	if (!ASSERT_OK(mkdirat(dirfd, "dir", 0700), "mkdir"))
		goto out;
	if (!ASSERT_OK(symlinkat("target", dirfd, "symlink"), "symlink"))
		goto out;
	if (!ASSERT_OK(linkat(dirfd, "file", dirfd, "link", 0), "link"))
		goto out;
	if (!ASSERT_OK(mkfifoat(dirfd, "fifo", 0600), "mknod"))
		goto out;

	ret = 0;
out:
	if (fd >= 0)
		close(fd);
	unlinkat(dirfd, "link", 0);
	unlinkat(dirfd, "fifo", 0);
	unlinkat(dirfd, "symlink", 0);
	unlinkat(dirfd, "file", 0);
	unlinkat(dirfd, "dir", AT_REMOVEDIR);
	return ret;
}

static int test_lsm_inode_idmap(struct lsm *skel)
{
	char tmpdir[] = "/var/tmp/test_lsm_idmap.XXXXXX";
	__u32 expected = INODE_IDMAP_ALL;
	int dirfd = -1, idmapped_dirfd = -1;
	int ret = -1;

	if (!ASSERT_OK_PTR(mkdtemp(tmpdir), "mkdtemp"))
		return -1;

	dirfd = open(tmpdir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (!ASSERT_GE(dirfd, 0, "open_tmpdir"))
		goto out;

	idmapped_dirfd = create_idmapped_tmpfs();
	if (!ASSERT_GE(idmapped_dirfd, 0, "create_idmapped_tmpfs"))
		goto out;

	skel->bss->inode_identity_idmap_seen = 0;
	skel->bss->inode_idmapped_mount_seen = 0;

	if (!ASSERT_OK(exercise_inode_idmap_hooks(dirfd), "identity_idmap"))
		goto out;
	if (!ASSERT_OK(exercise_inode_idmap_hooks(idmapped_dirfd),
		       "idmapped_mount"))
		goto out;

	if (!ASSERT_EQ(skel->bss->inode_identity_idmap_seen, expected,
		       "inode_identity_idmap_seen"))
		goto out;
	ret = ASSERT_EQ(skel->bss->inode_idmapped_mount_seen, expected,
			"inode_idmapped_mount_seen") ? 0 : -1;

out:
	if (idmapped_dirfd >= 0)
		close(idmapped_dirfd);
	if (dirfd >= 0)
		close(dirfd);
	rmdir(tmpdir);
	return ret;
}

static int test_lsm(struct lsm *skel)
{
	struct bpf_link *link;
	int buf = 1234;
	int err;

	err = lsm__attach(skel);
	if (!ASSERT_OK(err, "attach"))
		return err;

	/* Check that already linked program can't be attached again. */
	link = bpf_program__attach(skel->progs.test_int_hook);
	if (!ASSERT_ERR_PTR(link, "attach_link"))
		return -1;

	err = exec_cmd(&skel->bss->monitored_pid);
	if (!ASSERT_OK(err, "exec_cmd"))
		return err;

	ASSERT_EQ(skel->bss->bprm_count, 1, "bprm_count");

	skel->bss->monitored_pid = getpid();

	err = test_lsm_inode_idmap(skel);
	if (!ASSERT_OK(err, "test_lsm_inode_idmap"))
		return err;

	err = stack_mprotect();
	if (!ASSERT_EQ(err, -1, "stack_mprotect") ||
	    !ASSERT_EQ(errno, EPERM, "stack_mprotect"))
		return err;

	ASSERT_EQ(skel->bss->mprotect_count, 1, "mprotect_count");

	syscall(__NR_setdomainname, &buf, -2L);
	syscall(__NR_setdomainname, 0, -3L);
	syscall(__NR_setdomainname, ~0L, -4L);

	ASSERT_EQ(skel->bss->copy_test, 3, "copy_test");

	lsm__detach(skel);

	skel->bss->copy_test = 0;
	skel->bss->bprm_count = 0;
	skel->bss->mprotect_count = 0;
	skel->bss->inode_identity_idmap_seen = 0;
	skel->bss->inode_idmapped_mount_seen = 0;
	return 0;
}

static void test_lsm_basic(void)
{
	struct lsm *skel = NULL;
	int err;

	skel = lsm__open_and_load();
	if (!ASSERT_OK_PTR(skel, "lsm_skel_load"))
		goto close_prog;

	err = test_lsm(skel);
	if (!ASSERT_OK(err, "test_lsm_first_attach"))
		goto close_prog;

	err = test_lsm(skel);
	ASSERT_OK(err, "test_lsm_second_attach");

close_prog:
	lsm__destroy(skel);
}

static void test_lsm_tailcall(void)
{
	struct lsm_tailcall *skel = NULL;
	int map_fd, prog_fd;
	int err, key;

	skel = lsm_tailcall__open_and_load();
	if (!ASSERT_OK_PTR(skel, "lsm_tailcall__skel_load"))
		goto close_prog;

	map_fd = bpf_map__fd(skel->maps.jmp_table);
	if (CHECK_FAIL(map_fd < 0))
		goto close_prog;

	prog_fd = bpf_program__fd(skel->progs.lsm_file_permission_prog);
	if (CHECK_FAIL(prog_fd < 0))
		goto close_prog;

	key = 0;
	err = bpf_map_update_elem(map_fd, &key, &prog_fd, BPF_ANY);
	if (CHECK_FAIL(!err))
		goto close_prog;

	prog_fd = bpf_program__fd(skel->progs.lsm_kernfs_init_security_prog);
	if (CHECK_FAIL(prog_fd < 0))
		goto close_prog;

	err = bpf_map_update_elem(map_fd, &key, &prog_fd, BPF_ANY);
	if (CHECK_FAIL(err))
		goto close_prog;

close_prog:
	lsm_tailcall__destroy(skel);
}

void test_test_lsm(void)
{
	if (test__start_subtest("lsm_basic"))
		test_lsm_basic();
	if (test__start_subtest("lsm_tailcall"))
		test_lsm_tailcall();
}
