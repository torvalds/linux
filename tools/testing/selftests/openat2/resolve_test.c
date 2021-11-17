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

#include "../kselftest.h"
#include "helpers.h"

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
 *     |-- passwd -> ../cheeky/../cheeky/../etc/../etc/passwd
 *     |-- abspasswd -> /../cheeky/../cheeky/../etc/../etc/passwd
 *     |-- dotdotlink -> ../../../../../../../../../../../../../../etc/passwd
 *     `-- garbagelink -> /../../../../../../../../../../../../../../etc/passwd
 */
int setup_testdir(void)
{
	int dfd, tmpfd;
	char dirname[] = "/tmp/ksft-openat2-testdir.XXXXXX";

	/* Unshare and make /tmp a new directory. */
	E_unshare(CLONE_NEWNS);
	E_mount("", "/tmp", "", MS_PRIVATE, "");

	/* Make the top-level directory. */
	if (!mkdtemp(dirname))
		ksft_exit_fail_msg("setup_testdir: failed to create tmpdir\n");
	dfd = open(dirname, O_PATH | O_DIRECTORY);
	if (dfd < 0)
		ksft_exit_fail_msg("setup_testdir: failed to open tmpdir\n");

	/* A sub-directory which is actually used for tests. */
	E_mkdirat(dfd, "root", 0755);
	tmpfd = openat(dfd, "root", O_PATH | O_DIRECTORY);
	if (tmpfd < 0)
		ksft_exit_fail_msg("setup_testdir: failed to open tmpdir\n");
	close(dfd);
	dfd = tmpfd;

	E_symlinkat("/proc/self/exe", dfd, "procexe");
	E_symlinkat("/proc/self/root", dfd, "procroot");
	E_mkdirat(dfd, "root", 0755);

	/* There is no mountat(2), so use chdir. */
	E_mkdirat(dfd, "mnt", 0755);
	E_fchdir(dfd);
	E_mount("tmpfs", "./mnt", "tmpfs", MS_NOSUID | MS_NODEV, "");
	E_symlinkat("../mnt/", dfd, "mnt/self");
	E_symlinkat("/mnt/", dfd, "mnt/absself");

	E_mkdirat(dfd, "etc", 0755);
	E_touchat(dfd, "etc/passwd");

	E_symlinkat("/newfile3", dfd, "creatlink");
	E_symlinkat("etc/", dfd, "reletc");
	E_symlinkat("etc/passwd", dfd, "relsym");
	E_symlinkat("/etc/", dfd, "absetc");
	E_symlinkat("/etc/passwd", dfd, "abssym");
	E_symlinkat("/cheeky", dfd, "abscheeky");

	E_mkdirat(dfd, "cheeky", 0755);

	E_symlinkat("/", dfd, "cheeky/absself");
	E_symlinkat("../../root/", dfd, "cheeky/self");
	E_symlinkat("/../../root/", dfd, "cheeky/garbageself");

	E_symlinkat("../cheeky/../etc/../etc/passwd", dfd, "cheeky/passwd");
	E_symlinkat("/../cheeky/../etc/../etc/passwd", dfd, "cheeky/abspasswd");

	E_symlinkat("../../../../../../../../../../../../../../etc/passwd",
		    dfd, "cheeky/dotdotlink");
	E_symlinkat("/../../../../../../../../../../../../../../etc/passwd",
		    dfd, "cheeky/garbagelink");

	return dfd;
}

struct basic_test {
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

#define NUM_OPENAT2_OPATH_TESTS 88

void test_openat2_opath_tests(void)
{
	int rootfd, hardcoded_fd;
	char *procselfexe, *hardcoded_fdpath;

	E_asprintf(&procselfexe, "/proc/%d/exe", getpid());
	rootfd = setup_testdir();

	hardcoded_fd = open("/dev/null", O_RDONLY);
	E_assert(hardcoded_fd >= 0, "open fd to hardcode");
	E_asprintf(&hardcoded_fdpath, "self/fd/%d", hardcoded_fd);

	struct basic_test tests[] = {
		/** RESOLVE_BENEATH **/
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

		/** RESOLVE_IN_ROOT **/
		/* All attempts to cross the dirfd will be scoped-to-root. */
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

		/** RESOLVE_NO_XDEV **/
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
		  .dir = "/proc", .path = hardcoded_fdpath, .how.resolve = RESOLVE_NO_XDEV,
		  .out.path = "/proc",			    .pass = true, },

		/** RESOLVE_NO_MAGICLINKS **/
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
		  .out.path = procselfexe,	.pass = true },
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

		/** RESOLVE_NO_SYMLINKS **/
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

	BUILD_BUG_ON(ARRAY_LEN(tests) != NUM_OPENAT2_OPATH_TESTS);

	for (int i = 0; i < ARRAY_LEN(tests); i++) {
		int dfd, fd;
		char *fdpath = NULL;
		bool failed;
		void (*resultfn)(const char *msg, ...) = ksft_test_result_pass;
		struct basic_test *test = &tests[i];

		if (!openat2_supported) {
			ksft_print_msg("openat2(2) unsupported\n");
			resultfn = ksft_test_result_skip;
			goto skip;
		}

		/* Auto-set O_PATH. */
		if (!(test->how.flags & O_CREAT))
			test->how.flags |= O_PATH;

		if (test->dir)
			dfd = openat(rootfd, test->dir, O_PATH | O_DIRECTORY);
		else
			dfd = dup(rootfd);
		E_assert(dfd, "failed to openat root '%s': %m", test->dir);

		E_dup2(dfd, hardcoded_fd);

		fd = sys_openat2(dfd, test->path, &test->how);
		if (test->pass)
			failed = (fd < 0 || !fdequal(fd, rootfd, test->out.path));
		else
			failed = (fd != test->out.err);
		if (fd >= 0) {
			fdpath = fdreadlink(fd);
			close(fd);
		}
		close(dfd);

		if (failed) {
			resultfn = ksft_test_result_fail;

			ksft_print_msg("openat2 unexpectedly returned ");
			if (fdpath)
				ksft_print_msg("%d['%s']\n", fd, fdpath);
			else
				ksft_print_msg("%d (%s)\n", fd, strerror(-fd));
		}

skip:
		if (test->pass)
			resultfn("%s gives path '%s'\n", test->name,
				 test->out.path ?: ".");
		else
			resultfn("%s fails with %d (%s)\n", test->name,
				 test->out.err, strerror(-test->out.err));

		fflush(stdout);
		free(fdpath);
	}

	free(procselfexe);
	close(rootfd);

	free(hardcoded_fdpath);
	close(hardcoded_fd);
}

#define NUM_TESTS NUM_OPENAT2_OPATH_TESTS

int main(int argc, char **argv)
{
	ksft_print_header();
	ksft_set_plan(NUM_TESTS);

	/* NOTE: We should be checking for CAP_SYS_ADMIN here... */
	if (geteuid() != 0)
		ksft_exit_skip("all tests require euid == 0\n");

	test_openat2_opath_tests();

	if (ksft_get_fail_cnt() + ksft_get_error_cnt() > 0)
		ksft_exit_fail();
	else
		ksft_exit_pass();
}
