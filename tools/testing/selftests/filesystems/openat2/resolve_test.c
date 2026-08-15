// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Aleksa Sarai <cyphar@cyphar.com>
 * Copyright (C) 2018-2019 SUSE LLC.
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "helpers.h"
#include "kselftest_harness.h"

struct resolve_test {
	const char *name;
	const char *dir;
	const char *path;
	struct open_how how;
	bool pass;
	union {
		int err;
		const char *path;
	} out;
};

/*
 * Verify a single resolve test case. This must be called from within a TEST_F
 * function with _metadata in scope.
 */
static void verify_resolve_test(struct __test_metadata *_metadata,
				int rootfd, int hardcoded_fd,
				const struct resolve_test *test)
{
	struct open_how how = test->how;
	int dfd, fd;
	char *fdpath = NULL;

	/* Auto-set O_PATH. */
	if (!(how.flags & O_CREAT))
		how.flags |= O_PATH;

	if (test->dir)
		dfd = openat(rootfd, test->dir, O_PATH | O_DIRECTORY);
	else
		dfd = dup(rootfd);
	ASSERT_GE(dfd, 0) TH_LOG("failed to open dir '%s': %m", test->dir ?: ".");
	ASSERT_EQ(dup2(dfd, hardcoded_fd), hardcoded_fd);

	fd = sys_openat2(dfd, test->path, &how);

	if (test->pass) {
		EXPECT_GE(fd, 0) {
			TH_LOG("%s: expected success, got %d (%s)",
			       test->name, fd, strerror(-fd));
		}
		if (fd >= 0) {
			EXPECT_TRUE(fdequal(_metadata, fd, rootfd, test->out.path)) {
				fdpath = fdreadlink(_metadata, fd);
				TH_LOG("%s: wrong path '%s', expected '%s'",
				       test->name, fdpath,
				       test->out.path ?: ".");
				free(fdpath);
			}
		}
	} else {
		EXPECT_EQ(test->out.err, fd) {
			if (fd >= 0) {
				fdpath = fdreadlink(_metadata, fd);
				TH_LOG("%s: expected %d (%s), got %d['%s']",
				       test->name, test->out.err,
				       strerror(-test->out.err), fd, fdpath);
				free(fdpath);
			} else {
				TH_LOG("%s: expected %d (%s), got %d (%s)",
				       test->name, test->out.err,
				       strerror(-test->out.err),
				       fd, strerror(-fd));
			}
		}
	}

	if (fd >= 0)
		close(fd);
	close(dfd);
}

/*
 * Construct a test directory with the following structure:
 *
 * root/
 * |-- procexe -> /proc/self/exe
 * |-- procroot -> /proc/self/root
 * |-- root/
 * |-- mnt/ [mountpoint]
 * |   |-- self -> ../mnt/
 * |   `-- absself -> /mnt/
 * |-- etc/
 * |   `-- passwd
 * |-- creatlink -> /newfile3
 * |-- reletc -> etc/
 * |-- relsym -> etc/passwd
 * |-- absetc -> /etc/
 * |-- abssym -> /etc/passwd
 * |-- abscheeky -> /cheeky
 * `-- cheeky/
 *     |-- absself -> /
 *     |-- self -> ../../root/
 *     |-- garbageself -> /../../root/
 *     |-- passwd -> ../cheeky/../etc/../etc/passwd
 *     |-- abspasswd -> /../cheeky/../etc/../etc/passwd
 *     |-- dotdotlink -> ../../../../../../../../../../../../../../etc/passwd
 *     `-- garbagelink -> /../../../../../../../../../../../../../../etc/passwd
 */
FIXTURE(openat2_resolve) {
	int rootfd;
	int hardcoded_fd;
	char *hardcoded_fdpath;
	char *procselfexe;
};

FIXTURE_SETUP(openat2_resolve)
{
	char dirname[] = "/tmp/ksft-openat2-testdir.XXXXXX";
	int dfd, tmpfd;

	self->rootfd = -1;
	self->hardcoded_fd = -1;
	self->hardcoded_fdpath = NULL;
	self->procselfexe = NULL;

	/* NOTE: We should be checking for CAP_SYS_ADMIN here... */
	if (geteuid() != 0)
		SKIP(return, "all tests require euid == 0");
	if (!openat2_supported)
		SKIP(return, "openat2(2) not supported");

	/* Unshare and make /tmp a new directory. */
	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(mount("", "/tmp", "", MS_PRIVATE, ""), 0);

	/* Make the top-level directory. */
	ASSERT_NE(mkdtemp(dirname), NULL);
	dfd = open(dirname, O_PATH | O_DIRECTORY);
	ASSERT_GE(dfd, 0);

	/* A sub-directory which is actually used for tests. */
	ASSERT_EQ(mkdirat(dfd, "root", 0755), 0);
	tmpfd = openat(dfd, "root", O_PATH | O_DIRECTORY);
	ASSERT_GE(tmpfd, 0);
	close(dfd);
	dfd = tmpfd;

	ASSERT_EQ(symlinkat("/proc/self/exe", dfd, "procexe"), 0);
	ASSERT_EQ(symlinkat("/proc/self/root", dfd, "procroot"), 0);
	ASSERT_EQ(mkdirat(dfd, "root", 0755), 0);

	/* There is no mountat(2), so use chdir. */
	ASSERT_EQ(mkdirat(dfd, "mnt", 0755), 0);
	ASSERT_EQ(fchdir(dfd), 0);
	ASSERT_EQ(mount("tmpfs", "./mnt", "tmpfs", MS_NOSUID | MS_NODEV, ""), 0);
	ASSERT_EQ(symlinkat("../mnt/", dfd, "mnt/self"), 0);
	ASSERT_EQ(symlinkat("/mnt/", dfd, "mnt/absself"), 0);

	ASSERT_EQ(mkdirat(dfd, "etc", 0755), 0);
	ASSERT_GE(touchat(dfd, "etc/passwd"), 0);

	ASSERT_EQ(symlinkat("/newfile3", dfd, "creatlink"), 0);
	ASSERT_EQ(symlinkat("etc/", dfd, "reletc"), 0);
	ASSERT_EQ(symlinkat("etc/passwd", dfd, "relsym"), 0);
	ASSERT_EQ(symlinkat("/etc/", dfd, "absetc"), 0);
	ASSERT_EQ(symlinkat("/etc/passwd", dfd, "abssym"), 0);
	ASSERT_EQ(symlinkat("/cheeky", dfd, "abscheeky"), 0);

	ASSERT_EQ(mkdirat(dfd, "cheeky", 0755), 0);

	ASSERT_EQ(symlinkat("/", dfd, "cheeky/absself"), 0);
	ASSERT_EQ(symlinkat("../../root/", dfd, "cheeky/self"), 0);
	ASSERT_EQ(symlinkat("/../../root/", dfd, "cheeky/garbageself"), 0);

	ASSERT_EQ(symlinkat("../cheeky/../etc/../etc/passwd",
			    dfd, "cheeky/passwd"), 0);
	ASSERT_EQ(symlinkat("/../cheeky/../etc/../etc/passwd",
			    dfd, "cheeky/abspasswd"), 0);

	ASSERT_EQ(symlinkat("../../../../../../../../../../../../../../etc/passwd",
			    dfd, "cheeky/dotdotlink"), 0);
	ASSERT_EQ(symlinkat("/../../../../../../../../../../../../../../etc/passwd",
			    dfd, "cheeky/garbagelink"), 0);

	self->rootfd = dfd;

	self->hardcoded_fd = open("/dev/null", O_RDONLY);
	ASSERT_GE(self->hardcoded_fd, 0);
	ASSERT_GE(asprintf(&self->hardcoded_fdpath, "self/fd/%d",
			   self->hardcoded_fd), 0);
	ASSERT_GE(asprintf(&self->procselfexe, "/proc/%d/exe", getpid()), 0);
}

FIXTURE_TEARDOWN(openat2_resolve)
{
	free(self->procselfexe);
	free(self->hardcoded_fdpath);
	if (self->hardcoded_fd >= 0)
		close(self->hardcoded_fd);
	if (self->rootfd >= 0)
		close(self->rootfd);
}

/* Attempts to cross the dirfd should be blocked with -EXDEV. */
TEST_F(openat2_resolve, resolve_beneath)
{
	struct resolve_test tests[] = {
		/* Attempts to cross dirfd should be blocked. */
		{ .name = "[beneath] jump to /",
		  .path = "/",			.how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[beneath] absolute link to $root",
		  .path = "cheeky/absself",	.how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[beneath] chained absolute links to $root",
		  .path = "abscheeky/absself",	.how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[beneath] jump outside $root",
		  .path = "..",			.how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[beneath] temporary jump outside $root",
		  .path = "../root/",		.how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[beneath] symlink temporary jump outside $root",
		  .path = "cheeky/self",	.how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[beneath] chained symlink temporary jump outside $root",
		  .path = "abscheeky/self",	.how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[beneath] garbage links to $root",
		  .path = "cheeky/garbageself",	.how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[beneath] chained garbage links to $root",
		  .path = "abscheeky/garbageself", .how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		/* Only relative paths that stay inside dirfd should work. */
		{ .name = "[beneath] ordinary path to 'root'",
		  .path = "root",		.how.resolve = RESOLVE_BENEATH,
		  .out.path = "root",		.pass = true },
		{ .name = "[beneath] ordinary path to 'etc'",
		  .path = "etc",		.how.resolve = RESOLVE_BENEATH,
		  .out.path = "etc",		.pass = true },
		{ .name = "[beneath] ordinary path to 'etc/passwd'",
		  .path = "etc/passwd",		.how.resolve = RESOLVE_BENEATH,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[beneath] relative symlink inside $root",
		  .path = "relsym",		.how.resolve = RESOLVE_BENEATH,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[beneath] chained-'..' relative symlink inside $root",
		  .path = "cheeky/passwd",	.how.resolve = RESOLVE_BENEATH,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[beneath] absolute symlink component outside $root",
		  .path = "abscheeky/passwd",	.how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[beneath] absolute symlink target outside $root",
		  .path = "abssym",		.how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[beneath] absolute path outside $root",
		  .path = "/etc/passwd",	.how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[beneath] cheeky absolute path outside $root",
		  .path = "cheeky/abspasswd",	.how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[beneath] chained cheeky absolute path outside $root",
		  .path = "abscheeky/abspasswd", .how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		/* Tricky paths should fail. */
		{ .name = "[beneath] tricky '..'-chained symlink outside $root",
		  .path = "cheeky/dotdotlink",	.how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[beneath] tricky absolute + '..'-chained symlink outside $root",
		  .path = "abscheeky/dotdotlink", .how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[beneath] tricky garbage link outside $root",
		  .path = "cheeky/garbagelink",	.how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[beneath] tricky absolute + garbage link outside $root",
		  .path = "abscheeky/garbagelink", .how.resolve = RESOLVE_BENEATH,
		  .out.err = -EXDEV,		.pass = false },
	};

	for (int i = 0; i < ARRAY_SIZE(tests); i++)
		verify_resolve_test(_metadata, self->rootfd,
				    self->hardcoded_fd, &tests[i]);
}

/* All attempts to cross the dirfd will be scoped-to-root. */
TEST_F(openat2_resolve, resolve_in_root)
{
	struct resolve_test tests[] = {
		{ .name = "[in_root] jump to /",
		  .path = "/",			.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = NULL,		.pass = true },
		{ .name = "[in_root] absolute symlink to /root",
		  .path = "cheeky/absself",	.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = NULL,		.pass = true },
		{ .name = "[in_root] chained absolute symlinks to /root",
		  .path = "abscheeky/absself",	.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = NULL,		.pass = true },
		{ .name = "[in_root] '..' at root",
		  .path = "..",			.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = NULL,		.pass = true },
		{ .name = "[in_root] '../root' at root",
		  .path = "../root/",		.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "root",		.pass = true },
		{ .name = "[in_root] relative symlink containing '..' above root",
		  .path = "cheeky/self",	.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "root",		.pass = true },
		{ .name = "[in_root] garbage link to /root",
		  .path = "cheeky/garbageself",	.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "root",		.pass = true },
		{ .name = "[in_root] chained garbage links to /root",
		  .path = "abscheeky/garbageself", .how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "root",		.pass = true },
		{ .name = "[in_root] relative path to 'root'",
		  .path = "root",		.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "root",		.pass = true },
		{ .name = "[in_root] relative path to 'etc'",
		  .path = "etc",		.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "etc",		.pass = true },
		{ .name = "[in_root] relative path to 'etc/passwd'",
		  .path = "etc/passwd",		.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[in_root] relative symlink to 'etc/passwd'",
		  .path = "relsym",		.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[in_root] chained-'..' relative symlink to 'etc/passwd'",
		  .path = "cheeky/passwd",	.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[in_root] chained-'..' absolute + relative symlink to 'etc/passwd'",
		  .path = "abscheeky/passwd",	.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[in_root] absolute symlink to 'etc/passwd'",
		  .path = "abssym",		.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[in_root] absolute path 'etc/passwd'",
		  .path = "/etc/passwd",	.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[in_root] cheeky absolute path 'etc/passwd'",
		  .path = "cheeky/abspasswd",	.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[in_root] chained cheeky absolute path 'etc/passwd'",
		  .path = "abscheeky/abspasswd", .how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[in_root] tricky '..'-chained symlink outside $root",
		  .path = "cheeky/dotdotlink",	.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[in_root] tricky absolute + '..'-chained symlink outside $root",
		  .path = "abscheeky/dotdotlink", .how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[in_root] tricky absolute path + absolute + '..'-chained symlink outside $root",
		  .path = "/../../../../abscheeky/dotdotlink", .how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[in_root] tricky garbage link outside $root",
		  .path = "cheeky/garbagelink",	.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[in_root] tricky absolute + garbage link outside $root",
		  .path = "abscheeky/garbagelink", .how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "etc/passwd",	.pass = true },
		{ .name = "[in_root] tricky absolute path + absolute + garbage link outside $root",
		  .path = "/../../../../abscheeky/garbagelink", .how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "etc/passwd",	.pass = true },
		/* O_CREAT should handle trailing symlinks correctly. */
		{ .name = "[in_root] O_CREAT of relative path inside $root",
		  .path = "newfile1",		.how.flags = O_CREAT,
						.how.mode = 0700,
						.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "newfile1",	.pass = true },
		{ .name = "[in_root] O_CREAT of absolute path",
		  .path = "/newfile2",		.how.flags = O_CREAT,
						.how.mode = 0700,
						.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "newfile2",	.pass = true },
		{ .name = "[in_root] O_CREAT of tricky symlink outside root",
		  .path = "/creatlink",		.how.flags = O_CREAT,
						.how.mode = 0700,
						.how.resolve = RESOLVE_IN_ROOT,
		  .out.path = "newfile3",	.pass = true },
	};

	for (int i = 0; i < ARRAY_SIZE(tests); i++)
		verify_resolve_test(_metadata, self->rootfd,
				    self->hardcoded_fd, &tests[i]);
}

/* Crossing mount boundaries should be blocked. */
TEST_F(openat2_resolve, resolve_no_xdev)
{
	struct resolve_test tests[] = {
		/* Crossing *down* into a mountpoint is disallowed. */
		{ .name = "[no_xdev] cross into $mnt",
		  .path = "mnt",		.how.resolve = RESOLVE_NO_XDEV,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[no_xdev] cross into $mnt/",
		  .path = "mnt/",		.how.resolve = RESOLVE_NO_XDEV,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[no_xdev] cross into $mnt/.",
		  .path = "mnt/.",		.how.resolve = RESOLVE_NO_XDEV,
		  .out.err = -EXDEV,		.pass = false },
		/* Crossing *up* out of a mountpoint is disallowed. */
		{ .name = "[no_xdev] goto mountpoint root",
		  .dir = "mnt", .path = ".",	.how.resolve = RESOLVE_NO_XDEV,
		  .out.path = "mnt",		.pass = true },
		{ .name = "[no_xdev] cross up through '..'",
		  .dir = "mnt", .path = "..",	.how.resolve = RESOLVE_NO_XDEV,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[no_xdev] temporary cross up through '..'",
		  .dir = "mnt", .path = "../mnt", .how.resolve = RESOLVE_NO_XDEV,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[no_xdev] temporary relative symlink cross up",
		  .dir = "mnt", .path = "self",	.how.resolve = RESOLVE_NO_XDEV,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[no_xdev] temporary absolute symlink cross up",
		  .dir = "mnt", .path = "absself", .how.resolve = RESOLVE_NO_XDEV,
		  .out.err = -EXDEV,		.pass = false },
		/* Jumping to "/" is ok, but later components cannot cross. */
		{ .name = "[no_xdev] jump to / directly",
		  .dir = "mnt", .path = "/",	.how.resolve = RESOLVE_NO_XDEV,
		  .out.path = "/",		.pass = true },
		{ .name = "[no_xdev] jump to / (from /) directly",
		  .dir = "/", .path = "/",	.how.resolve = RESOLVE_NO_XDEV,
		  .out.path = "/",		.pass = true },
		{ .name = "[no_xdev] jump to / then proc",
		  .path = "/proc/1",		.how.resolve = RESOLVE_NO_XDEV,
		  .out.err = -EXDEV,		.pass = false },
		{ .name = "[no_xdev] jump to / then tmp",
		  .path = "/tmp",		.how.resolve = RESOLVE_NO_XDEV,
		  .out.err = -EXDEV,		.pass = false },
		/* Magic-links are blocked since they can switch vfsmounts. */
		{ .name = "[no_xdev] cross through magic-link to self/root",
		  .dir = "/proc", .path = "self/root", 	.how.resolve = RESOLVE_NO_XDEV,
		  .out.err = -EXDEV,			.pass = false },
		{ .name = "[no_xdev] cross through magic-link to self/cwd",
		  .dir = "/proc", .path = "self/cwd",	.how.resolve = RESOLVE_NO_XDEV,
		  .out.err = -EXDEV,			.pass = false },
		/* Except magic-link jumps inside the same vfsmount. */
		{ .name = "[no_xdev] jump through magic-link to same procfs",
		  .dir = "/proc", .path = self->hardcoded_fdpath, .how.resolve = RESOLVE_NO_XDEV,
		  .out.path = "/proc",				  .pass = true, },
	};

	for (int i = 0; i < ARRAY_SIZE(tests); i++)
		verify_resolve_test(_metadata, self->rootfd,
				    self->hardcoded_fd, &tests[i]);
}

/* Procfs-style magic-link resolution should be blocked. */
TEST_F(openat2_resolve, resolve_no_magiclinks)
{
	struct resolve_test tests[] = {
		/* Regular symlinks should work. */
		{ .name = "[no_magiclinks] ordinary relative symlink",
		  .path = "relsym",		.how.resolve = RESOLVE_NO_MAGICLINKS,
		  .out.path = "etc/passwd",	.pass = true },
		/* Magic-links should not work. */
		{ .name = "[no_magiclinks] symlink to magic-link",
		  .path = "procexe",		.how.resolve = RESOLVE_NO_MAGICLINKS,
		  .out.err = -ELOOP,		.pass = false },
		{ .name = "[no_magiclinks] normal path to magic-link",
		  .path = "/proc/self/exe",	.how.resolve = RESOLVE_NO_MAGICLINKS,
		  .out.err = -ELOOP,		.pass = false },
		{ .name = "[no_magiclinks] normal path to magic-link with O_NOFOLLOW",
		  .path = "/proc/self/exe",	.how.flags = O_NOFOLLOW,
						.how.resolve = RESOLVE_NO_MAGICLINKS,
		  .out.path = self->procselfexe, .pass = true },
		{ .name = "[no_magiclinks] symlink to magic-link path component",
		  .path = "procroot/etc",	.how.resolve = RESOLVE_NO_MAGICLINKS,
		  .out.err = -ELOOP,		.pass = false },
		{ .name = "[no_magiclinks] magic-link path component",
		  .path = "/proc/self/root/etc", .how.resolve = RESOLVE_NO_MAGICLINKS,
		  .out.err = -ELOOP,		.pass = false },
		{ .name = "[no_magiclinks] magic-link path component with O_NOFOLLOW",
		  .path = "/proc/self/root/etc", .how.flags = O_NOFOLLOW,
						 .how.resolve = RESOLVE_NO_MAGICLINKS,
		  .out.err = -ELOOP,		.pass = false },
	};

	for (int i = 0; i < ARRAY_SIZE(tests); i++)
		verify_resolve_test(_metadata, self->rootfd,
				    self->hardcoded_fd, &tests[i]);
}

/* All symlink resolution should be blocked. */
TEST_F(openat2_resolve, resolve_no_symlinks)
{
	struct resolve_test tests[] = {
		/* Normal paths should work. */
		{ .name = "[no_symlinks] ordinary path to '.'",
		  .path = ".",			.how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.path = NULL,		.pass = true },
		{ .name = "[no_symlinks] ordinary path to 'root'",
		  .path = "root",		.how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.path = "root",		.pass = true },
		{ .name = "[no_symlinks] ordinary path to 'etc'",
		  .path = "etc",		.how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.path = "etc",		.pass = true },
		{ .name = "[no_symlinks] ordinary path to 'etc/passwd'",
		  .path = "etc/passwd",		.how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.path = "etc/passwd",	.pass = true },
		/* Regular symlinks are blocked. */
		{ .name = "[no_symlinks] relative symlink target",
		  .path = "relsym",		.how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.err = -ELOOP,		.pass = false },
		{ .name = "[no_symlinks] relative symlink component",
		  .path = "reletc/passwd",	.how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.err = -ELOOP,		.pass = false },
		{ .name = "[no_symlinks] absolute symlink target",
		  .path = "abssym",		.how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.err = -ELOOP,		.pass = false },
		{ .name = "[no_symlinks] absolute symlink component",
		  .path = "absetc/passwd",	.how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.err = -ELOOP,		.pass = false },
		{ .name = "[no_symlinks] cheeky garbage link",
		  .path = "cheeky/garbagelink",	.how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.err = -ELOOP,		.pass = false },
		{ .name = "[no_symlinks] cheeky absolute + garbage link",
		  .path = "abscheeky/garbagelink", .how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.err = -ELOOP,		.pass = false },
		{ .name = "[no_symlinks] cheeky absolute + absolute symlink",
		  .path = "abscheeky/absself",	.how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.err = -ELOOP,		.pass = false },
		/* Trailing symlinks with NO_FOLLOW. */
		{ .name = "[no_symlinks] relative symlink with O_NOFOLLOW",
		  .path = "relsym",		.how.flags = O_NOFOLLOW,
						.how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.path = "relsym",		.pass = true },
		{ .name = "[no_symlinks] absolute symlink with O_NOFOLLOW",
		  .path = "abssym",		.how.flags = O_NOFOLLOW,
						.how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.path = "abssym",		.pass = true },
		{ .name = "[no_symlinks] trailing symlink with O_NOFOLLOW",
		  .path = "cheeky/garbagelink",	.how.flags = O_NOFOLLOW,
						.how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.path = "cheeky/garbagelink", .pass = true },
		{ .name = "[no_symlinks] multiple symlink components with O_NOFOLLOW",
		  .path = "abscheeky/absself",	.how.flags = O_NOFOLLOW,
						.how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.err = -ELOOP,		.pass = false },
		{ .name = "[no_symlinks] multiple symlink (and garbage link) components with O_NOFOLLOW",
		  .path = "abscheeky/garbagelink", .how.flags = O_NOFOLLOW,
						   .how.resolve = RESOLVE_NO_SYMLINKS,
		  .out.err = -ELOOP,		.pass = false },
	};

	for (int i = 0; i < ARRAY_SIZE(tests); i++)
		verify_resolve_test(_metadata, self->rootfd,
				    self->hardcoded_fd, &tests[i]);
}

TEST_HARNESS_MAIN
