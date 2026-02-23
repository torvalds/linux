// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock filesystem benchmark
 *
 * This program benchmarks the time required for file access checks.  We use a
 * large number (-d flag) of nested directories where each directory inode has
 * an associated Landlock rule, and we repeatedly (-n flag) exercise a file
 * access for which Landlock has to walk the path all the way up to the root.
 *
 * With an increasing number of nested subdirectories, Landlock's portion of the
 * overall system call time increases, which makes the effects of Landlock
 * refactorings more measurable.
 *
 * This benchmark does *not* measure the building of the Landlock ruleset.  The
 * time required to add all these rules is not large enough to be easily
 * measurable.  A separate benchmark tool would be better to test that, and that
 * tool could then also use a simpler file system layout.
 *
 * Copyright © 2026 Google LLC
 */

#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <linux/prctl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <time.h>
#include <unistd.h>

#include "wrappers.h"

static void usage(const char *const argv0)
{
	printf("Usage:\n");
	printf("  %s [OPTIONS]\n", argv0);
	printf("\n");
	printf("  Benchmark expensive Landlock checks for D nested dirs\n");
	printf("\n");
	printf("Options:\n");
	printf("  -h	help\n");
	printf("  -L	disable Landlock (as a baseline)\n");
	printf("  -d D	set directory depth to D\n");
	printf("  -n N	set number of benchmark iterations to N\n");
}

/*
 * Build a deep directory, enforce Landlock and return the FD to the
 * deepest dir.  On any failure, exit the process with an error.
 */
static int build_directory(size_t depth, const bool use_landlock)
{
	const char *path = "d"; /* directory name */
	int abi, ruleset_fd, curr, prev;

	if (use_landlock) {
		abi = landlock_create_ruleset(NULL, 0,
					      LANDLOCK_CREATE_RULESET_VERSION);
		if (abi < 7)
			err(1, "Landlock ABI too low: got %d, wanted 7+", abi);
	}

	ruleset_fd = -1;
	if (use_landlock) {
		struct landlock_ruleset_attr attr = {
			.handled_access_fs = LANDLOCK_ACCESS_FS_IOCTL_DEV |
					     LANDLOCK_ACCESS_FS_WRITE_FILE |
					     LANDLOCK_ACCESS_FS_MAKE_REG,
		};
		ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0U);
		if (ruleset_fd < 0)
			err(1, "landlock_create_ruleset");
	}

	curr = open(".", O_PATH);
	if (curr < 0)
		err(1, "open(.)");

	while (depth--) {
		if (use_landlock) {
			struct landlock_path_beneath_attr attr = {
				.allowed_access = LANDLOCK_ACCESS_FS_IOCTL_DEV,
				.parent_fd = curr,
			};
			if (landlock_add_rule(ruleset_fd,
					      LANDLOCK_RULE_PATH_BENEATH, &attr,
					      0) < 0)
				err(1, "landlock_add_rule");
		}

		if (mkdirat(curr, path, 0700) < 0)
			err(1, "mkdirat(%s)", path);

		prev = curr;
		curr = openat(curr, path, O_PATH);
		if (curr < 0)
			err(1, "openat(%s)", path);

		close(prev);
	}

	if (use_landlock) {
		if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0)
			err(1, "prctl");

		if (landlock_restrict_self(ruleset_fd, 0) < 0)
			err(1, "landlock_restrict_self");
	}

	close(ruleset_fd);
	return curr;
}

static void remove_recursively(const size_t depth)
{
	const char *path = "d"; /* directory name */

	int fd = openat(AT_FDCWD, ".", O_PATH);

	if (fd < 0)
		err(1, "openat(.)");

	for (size_t i = 0; i < depth - 1; i++) {
		int oldfd = fd;

		fd = openat(fd, path, O_PATH);
		if (fd < 0)
			err(1, "openat(%s)", path);
		close(oldfd);
	}

	for (size_t i = 0; i < depth; i++) {
		if (unlinkat(fd, path, AT_REMOVEDIR) < 0)
			err(1, "unlinkat(%s)", path);
		int newfd = openat(fd, "..", O_PATH);

		close(fd);
		fd = newfd;
	}
	close(fd);
}

int main(int argc, char *argv[])
{
	bool use_landlock = true;
	size_t num_iterations = 100000;
	size_t num_subdirs = 10000;
	int c, curr, fd;
	struct tms start_time, end_time;

	setbuf(stdout, NULL);
	while ((c = getopt(argc, argv, "hLd:n:")) != -1) {
		switch (c) {
		case 'h':
			usage(argv[0]);
			return EXIT_SUCCESS;
		case 'L':
			use_landlock = false;
			break;
		case 'd':
			num_subdirs = atoi(optarg);
			break;
		case 'n':
			num_iterations = atoi(optarg);
			break;
		default:
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	printf("*** Benchmark ***\n");
	printf("%zu dirs, %zu iterations, %s Landlock\n", num_subdirs,
	       num_iterations, use_landlock ? "with" : "without");

	if (times(&start_time) == -1)
		err(1, "times");

	curr = build_directory(num_subdirs, use_landlock);

	for (int i = 0; i < num_iterations; i++) {
		fd = openat(curr, "file.txt", O_CREAT | O_TRUNC | O_WRONLY,
			    0600);
		if (use_landlock) {
			if (fd == 0)
				errx(1, "openat succeeded, expected EACCES");
			if (errno != EACCES)
				err(1, "openat expected EACCES, but got");
		}
		if (fd != -1)
			close(fd);
	}

	if (times(&end_time) == -1)
		err(1, "times");

	printf("*** Benchmark concluded ***\n");
	printf("System: %ld clocks\n",
	       end_time.tms_stime - start_time.tms_stime);
	printf("User  : %ld clocks\n",
	       end_time.tms_utime - start_time.tms_utime);
	printf("Clocks per second: %ld\n", CLOCKS_PER_SEC);

	close(curr);

	remove_recursively(num_subdirs);
}
