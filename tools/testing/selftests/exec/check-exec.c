// SPDX-License-Identifier: GPL-2.0
/*
 * Test execveat(2) with AT_EXECVE_CHECK, and prctl(2) with
 * SECBIT_EXEC_RESTRICT_FILE, SECBIT_EXEC_DENY_INTERACTIVE, and their locked
 * counterparts.
 *
 * Copyright © 2018-2020 ANSSI
 * Copyright © 2024 Microsoft Corporation
 *
 * Author: Mickaël Salaün <mic@digikod.net>
 */

#include <asm-generic/unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/prctl.h>
#include <linux/securebits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/capability.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>

/* Defines AT_EXECVE_CHECK without type conflicts. */
#define _ASM_GENERIC_FCNTL_H
#include <linux/fcntl.h>

#include "../kselftest_harness.h"

static int sys_execveat(int dirfd, const char *pathname, char *const argv[],
			char *const envp[], int flags)
{
	return syscall(__NR_execveat, dirfd, pathname, argv, envp, flags);
}

static void drop_privileges(struct __test_metadata *const _metadata)
{
	const unsigned int noroot = SECBIT_NOROOT | SECBIT_NOROOT_LOCKED;
	cap_t cap_p;

	if ((cap_get_secbits() & noroot) != noroot)
		EXPECT_EQ(0, cap_set_secbits(noroot));

	cap_p = cap_get_proc();
	EXPECT_NE(NULL, cap_p);
	EXPECT_NE(-1, cap_clear(cap_p));

	/*
	 * Drops everything, especially CAP_SETPCAP, CAP_DAC_OVERRIDE, and
	 * CAP_DAC_READ_SEARCH.
	 */
	EXPECT_NE(-1, cap_set_proc(cap_p));
	EXPECT_NE(-1, cap_free(cap_p));
}

static int test_secbits_set(const unsigned int secbits)
{
	int err;

	err = prctl(PR_SET_SECUREBITS, secbits);
	if (err)
		return errno;
	return 0;
}

FIXTURE(access)
{
	int memfd, pipefd;
	int pipe_fds[2], socket_fds[2];
};

FIXTURE_VARIANT(access)
{
	const bool mount_exec;
	const bool file_exec;
};

/* clang-format off */
FIXTURE_VARIANT_ADD(access, mount_exec_file_exec) {
	/* clang-format on */
	.mount_exec = true,
	.file_exec = true,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(access, mount_exec_file_noexec) {
	/* clang-format on */
	.mount_exec = true,
	.file_exec = false,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(access, mount_noexec_file_exec) {
	/* clang-format on */
	.mount_exec = false,
	.file_exec = true,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(access, mount_noexec_file_noexec) {
	/* clang-format on */
	.mount_exec = false,
	.file_exec = false,
};

static const char binary_path[] = "./false";
static const char workdir_path[] = "./test-mount";
static const char reg_file_path[] = "./test-mount/regular_file";
static const char dir_path[] = "./test-mount/directory";
static const char block_dev_path[] = "./test-mount/block_device";
static const char char_dev_path[] = "./test-mount/character_device";
static const char fifo_path[] = "./test-mount/fifo";

FIXTURE_SETUP(access)
{
	int procfd_path_size;
	static const char path_template[] = "/proc/self/fd/%d";
	char procfd_path[sizeof(path_template) + 10];

	/* Makes sure we are not already restricted nor locked. */
	EXPECT_EQ(0, test_secbits_set(0));

	/*
	 * Cleans previous workspace if any error previously happened (don't
	 * check errors).
	 */
	umount(workdir_path);
	rmdir(workdir_path);

	/* Creates a clean mount point. */
	ASSERT_EQ(0, mkdir(workdir_path, 00700));
	ASSERT_EQ(0, mount("test", workdir_path, "tmpfs",
			   MS_MGC_VAL | (variant->mount_exec ? 0 : MS_NOEXEC),
			   "mode=0700,size=9m"));

	/* Creates a regular file. */
	ASSERT_EQ(0, mknod(reg_file_path,
			   S_IFREG | (variant->file_exec ? 0700 : 0600), 0));
	/* Creates a directory. */
	ASSERT_EQ(0, mkdir(dir_path, variant->file_exec ? 0700 : 0600));
	/* Creates a character device: /dev/null. */
	ASSERT_EQ(0, mknod(char_dev_path, S_IFCHR | 0400, makedev(1, 3)));
	/* Creates a block device: /dev/loop0 */
	ASSERT_EQ(0, mknod(block_dev_path, S_IFBLK | 0400, makedev(7, 0)));
	/* Creates a fifo. */
	ASSERT_EQ(0, mknod(fifo_path, S_IFIFO | 0600, 0));

	/* Creates a regular file without user mount point. */
	self->memfd = memfd_create("test-exec-probe", MFD_CLOEXEC);
	ASSERT_LE(0, self->memfd);
	/* Sets mode, which must be ignored by the exec check. */
	ASSERT_EQ(0, fchmod(self->memfd, variant->file_exec ? 0700 : 0600));

	/* Creates a pipefs file descriptor. */
	ASSERT_EQ(0, pipe(self->pipe_fds));
	procfd_path_size = snprintf(procfd_path, sizeof(procfd_path),
				    path_template, self->pipe_fds[0]);
	ASSERT_LT(procfd_path_size, sizeof(procfd_path));
	self->pipefd = open(procfd_path, O_RDWR | O_CLOEXEC);
	ASSERT_LE(0, self->pipefd);
	ASSERT_EQ(0, fchmod(self->pipefd, variant->file_exec ? 0700 : 0600));

	/* Creates a socket file descriptor. */
	ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0,
				self->socket_fds));
}

FIXTURE_TEARDOWN_PARENT(access)
{
	/* There is no need to unlink the test files. */
	EXPECT_EQ(0, umount(workdir_path));
	EXPECT_EQ(0, rmdir(workdir_path));
}

static void fill_exec_fd(struct __test_metadata *_metadata, const int fd_out)
{
	char buf[1024];
	size_t len;
	int fd_in;

	fd_in = open(binary_path, O_CLOEXEC | O_RDONLY);
	ASSERT_LE(0, fd_in);
	/* Cannot use copy_file_range(2) because of EXDEV. */
	len = read(fd_in, buf, sizeof(buf));
	EXPECT_LE(0, len);
	while (len > 0) {
		EXPECT_EQ(len, write(fd_out, buf, len))
		{
			TH_LOG("Failed to write: %s (%d)", strerror(errno),
			       errno);
		}
		len = read(fd_in, buf, sizeof(buf));
		EXPECT_LE(0, len);
	}
	EXPECT_EQ(0, close(fd_in));
}

static void fill_exec_path(struct __test_metadata *_metadata,
			   const char *const path)
{
	int fd_out;

	fd_out = open(path, O_CLOEXEC | O_WRONLY);
	ASSERT_LE(0, fd_out)
	{
		TH_LOG("Failed to open %s: %s", path, strerror(errno));
	}
	fill_exec_fd(_metadata, fd_out);
	EXPECT_EQ(0, close(fd_out));
}

static void test_exec_fd(struct __test_metadata *_metadata, const int fd,
			 const int err_code)
{
	char *const argv[] = { "", NULL };
	int access_ret, access_errno;

	/*
	 * If we really execute fd, filled with the "false" binary, the current
	 * thread will exits with an error, which will be interpreted by the
	 * test framework as an error.  With AT_EXECVE_CHECK, we only check a
	 * potential successful execution.
	 */
	access_ret = sys_execveat(fd, "", argv, NULL,
				  AT_EMPTY_PATH | AT_EXECVE_CHECK);
	access_errno = errno;
	if (err_code) {
		EXPECT_EQ(-1, access_ret);
		EXPECT_EQ(err_code, access_errno)
		{
			TH_LOG("Wrong error for execveat(2): %s (%d)",
			       strerror(access_errno), errno);
		}
	} else {
		EXPECT_EQ(0, access_ret)
		{
			TH_LOG("Access denied: %s", strerror(access_errno));
		}
	}
}

static void test_exec_path(struct __test_metadata *_metadata,
			   const char *const path, const int err_code)
{
	int flags = O_CLOEXEC;
	int fd;

	/* Do not block on pipes. */
	if (path == fifo_path)
		flags |= O_NONBLOCK;

	fd = open(path, flags | O_RDONLY);
	ASSERT_LE(0, fd)
	{
		TH_LOG("Failed to open %s: %s", path, strerror(errno));
	}
	test_exec_fd(_metadata, fd, err_code);
	EXPECT_EQ(0, close(fd));
}

/* Tests that we don't get ENOEXEC. */
TEST_F(access, regular_file_empty)
{
	const int exec = variant->mount_exec && variant->file_exec;

	test_exec_path(_metadata, reg_file_path, exec ? 0 : EACCES);

	drop_privileges(_metadata);
	test_exec_path(_metadata, reg_file_path, exec ? 0 : EACCES);
}

TEST_F(access, regular_file_elf)
{
	const int exec = variant->mount_exec && variant->file_exec;

	fill_exec_path(_metadata, reg_file_path);

	test_exec_path(_metadata, reg_file_path, exec ? 0 : EACCES);

	drop_privileges(_metadata);
	test_exec_path(_metadata, reg_file_path, exec ? 0 : EACCES);
}

/* Tests that we don't get ENOEXEC. */
TEST_F(access, memfd_empty)
{
	const int exec = variant->file_exec;

	test_exec_fd(_metadata, self->memfd, exec ? 0 : EACCES);

	drop_privileges(_metadata);
	test_exec_fd(_metadata, self->memfd, exec ? 0 : EACCES);
}

TEST_F(access, memfd_elf)
{
	const int exec = variant->file_exec;

	fill_exec_fd(_metadata, self->memfd);

	test_exec_fd(_metadata, self->memfd, exec ? 0 : EACCES);

	drop_privileges(_metadata);
	test_exec_fd(_metadata, self->memfd, exec ? 0 : EACCES);
}

TEST_F(access, non_regular_files)
{
	test_exec_path(_metadata, dir_path, EACCES);
	test_exec_path(_metadata, block_dev_path, EACCES);
	test_exec_path(_metadata, char_dev_path, EACCES);
	test_exec_path(_metadata, fifo_path, EACCES);
	test_exec_fd(_metadata, self->socket_fds[0], EACCES);
	test_exec_fd(_metadata, self->pipefd, EACCES);
}

/* clang-format off */
FIXTURE(secbits) {};
/* clang-format on */

FIXTURE_VARIANT(secbits)
{
	const bool is_privileged;
	const int error;
};

/* clang-format off */
FIXTURE_VARIANT_ADD(secbits, priv) {
	/* clang-format on */
	.is_privileged = true,
	.error = 0,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(secbits, unpriv) {
	/* clang-format on */
	.is_privileged = false,
	.error = EPERM,
};

FIXTURE_SETUP(secbits)
{
	/* Makes sure no exec bits are set. */
	EXPECT_EQ(0, test_secbits_set(0));
	EXPECT_EQ(0, prctl(PR_GET_SECUREBITS));

	if (!variant->is_privileged)
		drop_privileges(_metadata);
}

FIXTURE_TEARDOWN(secbits)
{
}

TEST_F(secbits, legacy)
{
	EXPECT_EQ(variant->error, test_secbits_set(0));
}

#define CHILD(...)                     \
	do {                           \
		pid_t child = vfork(); \
		EXPECT_LE(0, child);   \
		if (child == 0) {      \
			__VA_ARGS__;   \
			_exit(0);      \
		}                      \
	} while (0)

TEST_F(secbits, exec)
{
	unsigned int secbits = prctl(PR_GET_SECUREBITS);

	secbits |= SECBIT_EXEC_RESTRICT_FILE;
	EXPECT_EQ(0, test_secbits_set(secbits));
	EXPECT_EQ(secbits, prctl(PR_GET_SECUREBITS));
	CHILD(EXPECT_EQ(secbits, prctl(PR_GET_SECUREBITS)));

	secbits |= SECBIT_EXEC_DENY_INTERACTIVE;
	EXPECT_EQ(0, test_secbits_set(secbits));
	EXPECT_EQ(secbits, prctl(PR_GET_SECUREBITS));
	CHILD(EXPECT_EQ(secbits, prctl(PR_GET_SECUREBITS)));

	secbits &= ~(SECBIT_EXEC_RESTRICT_FILE | SECBIT_EXEC_DENY_INTERACTIVE);
	EXPECT_EQ(0, test_secbits_set(secbits));
	EXPECT_EQ(secbits, prctl(PR_GET_SECUREBITS));
	CHILD(EXPECT_EQ(secbits, prctl(PR_GET_SECUREBITS)));
}

TEST_F(secbits, check_locked_set)
{
	unsigned int secbits = prctl(PR_GET_SECUREBITS);

	secbits |= SECBIT_EXEC_RESTRICT_FILE;
	EXPECT_EQ(0, test_secbits_set(secbits));
	secbits |= SECBIT_EXEC_RESTRICT_FILE_LOCKED;
	EXPECT_EQ(0, test_secbits_set(secbits));

	/* Checks lock set but unchanged. */
	EXPECT_EQ(variant->error, test_secbits_set(secbits));
	CHILD(EXPECT_EQ(variant->error, test_secbits_set(secbits)));

	secbits &= ~SECBIT_EXEC_RESTRICT_FILE;
	EXPECT_EQ(EPERM, test_secbits_set(0));
	CHILD(EXPECT_EQ(EPERM, test_secbits_set(0)));
}

TEST_F(secbits, check_locked_unset)
{
	unsigned int secbits = prctl(PR_GET_SECUREBITS);

	secbits |= SECBIT_EXEC_RESTRICT_FILE_LOCKED;
	EXPECT_EQ(0, test_secbits_set(secbits));

	/* Checks lock unset but unchanged. */
	EXPECT_EQ(variant->error, test_secbits_set(secbits));
	CHILD(EXPECT_EQ(variant->error, test_secbits_set(secbits)));

	secbits &= ~SECBIT_EXEC_RESTRICT_FILE;
	EXPECT_EQ(EPERM, test_secbits_set(0));
	CHILD(EXPECT_EQ(EPERM, test_secbits_set(0)));
}

TEST_F(secbits, restrict_locked_set)
{
	unsigned int secbits = prctl(PR_GET_SECUREBITS);

	secbits |= SECBIT_EXEC_DENY_INTERACTIVE;
	EXPECT_EQ(0, test_secbits_set(secbits));
	secbits |= SECBIT_EXEC_DENY_INTERACTIVE_LOCKED;
	EXPECT_EQ(0, test_secbits_set(secbits));

	/* Checks lock set but unchanged. */
	EXPECT_EQ(variant->error, test_secbits_set(secbits));
	CHILD(EXPECT_EQ(variant->error, test_secbits_set(secbits)));

	secbits &= ~SECBIT_EXEC_DENY_INTERACTIVE;
	EXPECT_EQ(EPERM, test_secbits_set(0));
	CHILD(EXPECT_EQ(EPERM, test_secbits_set(0)));
}

TEST_F(secbits, restrict_locked_unset)
{
	unsigned int secbits = prctl(PR_GET_SECUREBITS);

	secbits |= SECBIT_EXEC_DENY_INTERACTIVE_LOCKED;
	EXPECT_EQ(0, test_secbits_set(secbits));

	/* Checks lock unset but unchanged. */
	EXPECT_EQ(variant->error, test_secbits_set(secbits));
	CHILD(EXPECT_EQ(variant->error, test_secbits_set(secbits)));

	secbits &= ~SECBIT_EXEC_DENY_INTERACTIVE;
	EXPECT_EQ(EPERM, test_secbits_set(0));
	CHILD(EXPECT_EQ(EPERM, test_secbits_set(0)));
}

TEST_HARNESS_MAIN
