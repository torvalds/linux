// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <linux/fsverity.h>
#include <unistd.h>
#include <test_progs.h>
#include "test_get_xattr.skel.h"
#include "test_fsverity.skel.h"

static const char testfile[] = "/tmp/test_progs_fs_kfuncs";

static void test_xattr(void)
{
	struct test_get_xattr *skel = NULL;
	int fd = -1, err;
	int v[32];

	fd = open(testfile, O_CREAT | O_RDONLY, 0644);
	if (!ASSERT_GE(fd, 0, "create_file"))
		return;

	close(fd);
	fd = -1;

	err = setxattr(testfile, "user.kfuncs", "hello", sizeof("hello"), 0);
	if (err && errno == EOPNOTSUPP) {
		printf("%s:SKIP:local fs doesn't support xattr (%d)\n"
		       "To run this test, make sure /tmp filesystem supports xattr.\n",
		       __func__, errno);
		test__skip();
		goto out;
	}

	if (!ASSERT_OK(err, "setxattr"))
		goto out;

	skel = test_get_xattr__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_get_xattr__open_and_load"))
		goto out;

	skel->bss->monitored_pid = getpid();
	err = test_get_xattr__attach(skel);

	if (!ASSERT_OK(err, "test_get_xattr__attach"))
		goto out;

	fd = open(testfile, O_RDONLY, 0644);
	if (!ASSERT_GE(fd, 0, "open_file"))
		goto out;

	ASSERT_EQ(skel->bss->found_xattr_from_file, 1, "found_xattr_from_file");

	/* Trigger security_inode_getxattr */
	err = getxattr(testfile, "user.kfuncs", v, sizeof(v));
	ASSERT_EQ(err, -1, "getxattr_return");
	ASSERT_EQ(errno, EINVAL, "getxattr_errno");
	ASSERT_EQ(skel->bss->found_xattr_from_dentry, 1, "found_xattr_from_dentry");

out:
	close(fd);
	test_get_xattr__destroy(skel);
	remove(testfile);
}

#ifndef SHA256_DIGEST_SIZE
#define SHA256_DIGEST_SIZE      32
#endif

static void test_fsverity(void)
{
	struct fsverity_enable_arg arg = {0};
	struct test_fsverity *skel = NULL;
	struct fsverity_digest *d;
	int fd, err;
	char buffer[4096];

	fd = open(testfile, O_CREAT | O_RDWR, 0644);
	if (!ASSERT_GE(fd, 0, "create_file"))
		return;

	/* Write random buffer, so the file is not empty */
	err = write(fd, buffer, 4096);
	if (!ASSERT_EQ(err, 4096, "write_file"))
		goto out;
	close(fd);

	/* Reopen read-only, otherwise FS_IOC_ENABLE_VERITY will fail */
	fd = open(testfile, O_RDONLY, 0644);
	if (!ASSERT_GE(fd, 0, "open_file1"))
		return;

	/* Enable fsverity for the file.
	 * If the file system doesn't support verity, this will fail. Skip
	 * the test in such case.
	 */
	arg.version = 1;
	arg.hash_algorithm = FS_VERITY_HASH_ALG_SHA256;
	arg.block_size = 4096;
	err = ioctl(fd, FS_IOC_ENABLE_VERITY, &arg);
	if (err) {
		printf("%s:SKIP:local fs doesn't support fsverity (%d)\n"
		       "To run this test, try enable CONFIG_FS_VERITY and enable FSVerity for the filesystem.\n",
		       __func__, errno);
		test__skip();
		goto out;
	}

	skel = test_fsverity__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_fsverity__open_and_load"))
		goto out;

	/* Get fsverity_digest from ioctl */
	d = (struct fsverity_digest *)skel->bss->expected_digest;
	d->digest_algorithm = FS_VERITY_HASH_ALG_SHA256;
	d->digest_size = SHA256_DIGEST_SIZE;
	err = ioctl(fd, FS_IOC_MEASURE_VERITY, skel->bss->expected_digest);
	if (!ASSERT_OK(err, "ioctl_FS_IOC_MEASURE_VERITY"))
		goto out;

	skel->bss->monitored_pid = getpid();
	err = test_fsverity__attach(skel);
	if (!ASSERT_OK(err, "test_fsverity__attach"))
		goto out;

	/* Reopen the file to trigger the program */
	close(fd);
	fd = open(testfile, O_RDONLY);
	if (!ASSERT_GE(fd, 0, "open_file2"))
		goto out;

	ASSERT_EQ(skel->bss->got_fsverity, 1, "got_fsverity");
	ASSERT_EQ(skel->bss->digest_matches, 1, "digest_matches");
out:
	close(fd);
	test_fsverity__destroy(skel);
	remove(testfile);
}

void test_fs_kfuncs(void)
{
	if (test__start_subtest("xattr"))
		test_xattr();

	if (test__start_subtest("fsverity"))
		test_fsverity();
}
