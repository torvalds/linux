// SPDX-License-Identifier: GPL-2.0
/*
 * Exercise the configfs userspace interface through the subsystems
 * registered by samples/configfs.
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates
 * Copyright (c) 2026 Breno Leitao <leitao@debian.org>
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/vfs.h>
#include <unistd.h>

#include "kselftest_harness.h"

/* Private to fs/configfs/mount.c. */
#define CONFIGFS_MAGIC		0x62656570

#define SAMPLE_MODULE		"configfs_sample"

#define CHILDLESS		"01-childless"
#define SIMPLE			"02-simple-children"
#define GROUPS			"03-group-children"
#define SYMLINKS		"04-symlink-children"

#define ITEM_A			SIMPLE "/kselftest-a"
#define ITEM_B			SIMPLE "/kselftest-b"
#define GROUP			GROUPS "/kselftest-group"
#define GROUP_ITEM		GROUP "/kselftest-a"
#define LINK_SRC		SYMLINKS "/kselftest-src"
#define LINK			LINK_SRC "/kselftest-link"

static const char * const test_links[] = {
	LINK,
};

/* Deepest first, so one pass empties the tree. */
static const char * const test_dirs[] = {
	GROUP_ITEM,
	GROUP,
	LINK_SRC,
	ITEM_A,
	ITEM_B,
};

static void drop_test_dirs(void)
{
	size_t i;

	/* Links first: they hold both their source and their target. */
	for (i = 0; i < ARRAY_SIZE(test_links); i++)
		unlink(test_links[i]);

	for (i = 0; i < ARRAY_SIZE(test_dirs); i++)
		rmdir(test_dirs[i]);
}

static ssize_t read_attr(const char *path, char *buf, size_t len)
{
	ssize_t ret;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;

	ret = read(fd, buf, len - 1);
	close(fd);
	if (ret < 0)
		return -1;

	buf[ret] = '\0';
	return ret;
}

static ssize_t write_attr(const char *path, const char *val)
{
	ssize_t ret;
	int fd, err;

	fd = open(path, O_WRONLY);
	if (fd < 0)
		return -1;

	ret = write(fd, val, strlen(val));
	err = errno;
	close(fd);
	errno = err;

	return ret;
}

FIXTURE(configfs) {
	char mnt[sizeof(P_tmpdir "/configfs_XXXXXX")];
	bool mounted;
};

FIXTURE_SETUP(configfs)
{
	char tmpl[] = P_tmpdir "/configfs_XXXXXX";

	if (geteuid())
		SKIP(return, "need root to load modules and mount configfs");

	ASSERT_EQ(system("modprobe -q " SAMPLE_MODULE), 0)
		TH_LOG(SAMPLE_MODULE " missing, is CONFIG_SAMPLE_CONFIGFS=m?");

	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL), 0);

	ASSERT_NE(mkdtemp(tmpl), NULL);
	strcpy(self->mnt, tmpl);

	ASSERT_EQ(mount("configfs", self->mnt, "configfs", 0, NULL), 0);
	ASSERT_EQ(chdir(self->mnt), 0);
	self->mounted = true;

	/* configfs items outlive the mount, so a killed run leaves some. */
	drop_test_dirs();
}

FIXTURE_TEARDOWN(configfs)
{
	if (self->mounted) {
		drop_test_dirs();
		EXPECT_EQ(chdir("/"), 0);
		EXPECT_EQ(umount2(self->mnt, MNT_DETACH), 0);
	}

	if (self->mnt[0])
		EXPECT_EQ(rmdir(self->mnt), 0);
}

TEST_F(configfs, mount_and_subsystems)
{
	const char * const subsys[] = { CHILDLESS, SIMPLE, GROUPS, SYMLINKS };
	struct statfs sfs;
	struct stat st;
	size_t i;

	ASSERT_EQ(statfs(".", &sfs), 0);
	EXPECT_EQ(sfs.f_type, CONFIGFS_MAGIC);

	for (i = 0; i < ARRAY_SIZE(subsys); i++) {
		ASSERT_EQ(stat(subsys[i], &st), 0)
			TH_LOG("%s is missing", subsys[i]);
		EXPECT_TRUE(S_ISDIR(st.st_mode));
	}
}

TEST_F(configfs, mkdir_at_root)
{
	/* The root has no ->mkdir(); only subsystems register there. */
	ASSERT_EQ(mkdir("kselftest-root", 0755), -1);
	EXPECT_EQ(errno, EPERM);
}

TEST_F(configfs, rmdir_subsystem)
{
	ASSERT_EQ(rmdir(CHILDLESS), -1);
	EXPECT_EQ(errno, EPERM);
}

TEST_F(configfs, mkdir_without_group_ops)
{
	/* 01-childless has attributes but no ->make_item()/->make_group(). */
	ASSERT_EQ(mkdir(CHILDLESS "/kselftest-a", 0755), -1);
	EXPECT_EQ(errno, EPERM);
}

TEST_F(configfs, attr_store_and_show)
{
	char buf[64];

	ASSERT_GT(write_attr(CHILDLESS "/storeme", "42"), 0);
	ASSERT_GT(read_attr(CHILDLESS "/storeme", buf, sizeof(buf)), 0);
	EXPECT_STREQ(buf, "42\n");
}

TEST_F(configfs, attr_store_rejects_garbage)
{
	ASSERT_EQ(write_attr(CHILDLESS "/storeme", "not-a-number"), -1);
	EXPECT_EQ(errno, EINVAL);
}

TEST_F(configfs, attr_show_runs_on_every_open)
{
	char first[64], second[64];

	/* 01-childless/showme increments the value it just returned. */
	ASSERT_GT(read_attr(CHILDLESS "/showme", first, sizeof(first)), 0);
	ASSERT_GT(read_attr(CHILDLESS "/showme", second, sizeof(second)), 0);
	EXPECT_EQ(atoi(second), atoi(first) + 1);
}

TEST_F(configfs, attr_read_only)
{
	ASSERT_EQ(open(CHILDLESS "/description", O_WRONLY), -1);
	EXPECT_EQ(errno, EACCES);
}

TEST_F(configfs, attr_unlink)
{
	/* ->unlink() only accepts the symlinks configfs itself created. */
	ASSERT_EQ(unlink(CHILDLESS "/storeme"), -1);
	EXPECT_EQ(errno, EPERM);
}

TEST_F(configfs, attr_read_length)
{
	char buf[8192];
	struct stat st;
	ssize_t n;
	int fd;

	fd = open(CHILDLESS "/description", O_RDONLY);
	ASSERT_GE(fd, 0);

	/* Attributes report a page, whatever ->show() ends up producing. */
	ASSERT_EQ(fstat(fd, &st), 0);
	EXPECT_EQ(st.st_size, sysconf(_SC_PAGESIZE));

	n = read(fd, buf, sizeof(buf));
	ASSERT_GT(n, 0);
	EXPECT_LT(n, st.st_size);
	EXPECT_EQ(read(fd, buf, sizeof(buf)), 0);

	EXPECT_EQ(close(fd), 0);
}

TEST_F(configfs, attr_write_is_not_incremental)
{
	char buf[64];
	int fd;

	/*
	 * Every write hands the whole buffer to ->store() and the file
	 * position is ignored, so the second write replaces the first.
	 */
	fd = open(CHILDLESS "/storeme", O_WRONLY);
	ASSERT_GE(fd, 0);
	ASSERT_EQ(write(fd, "1", 1), 1);
	ASSERT_EQ(write(fd, "2", 1), 1);
	EXPECT_EQ(close(fd), 0);

	ASSERT_GT(read_attr(CHILDLESS "/storeme", buf, sizeof(buf)), 0);
	EXPECT_STREQ(buf, "2\n");
}

TEST_F(configfs, item_create_and_drop)
{
	struct stat st;

	ASSERT_EQ(mkdir(ITEM_A, 0755), 0);
	EXPECT_EQ(stat(ITEM_A "/storeme", &st), 0);

	/* The item carries its own attributes, not the subsystem's. */
	ASSERT_EQ(stat(ITEM_A "/description", &st), -1);
	EXPECT_EQ(errno, ENOENT);

	ASSERT_EQ(rmdir(ITEM_A), 0);
	ASSERT_EQ(stat(ITEM_A, &st), -1);
	EXPECT_EQ(errno, ENOENT);
}

TEST_F(configfs, item_create_twice)
{
	ASSERT_EQ(mkdir(ITEM_A, 0755), 0);
	ASSERT_EQ(mkdir(ITEM_A, 0755), -1);
	EXPECT_EQ(errno, EEXIST);
}

TEST_F(configfs, item_has_no_children)
{
	/* ->make_item() produces an item, so it cannot nest. */
	ASSERT_EQ(mkdir(ITEM_A, 0755), 0);
	ASSERT_EQ(mkdir(ITEM_A "/kselftest-b", 0755), -1);
	EXPECT_EQ(errno, EPERM);
}

TEST_F(configfs, item_attrs_are_private)
{
	char buf[64];

	ASSERT_EQ(mkdir(ITEM_A, 0755), 0);
	ASSERT_EQ(mkdir(ITEM_B, 0755), 0);

	ASSERT_GT(write_attr(ITEM_A "/storeme", "11"), 0);
	ASSERT_GT(write_attr(ITEM_B "/storeme", "22"), 0);

	ASSERT_GT(read_attr(ITEM_A "/storeme", buf, sizeof(buf)), 0);
	EXPECT_STREQ(buf, "11\n");
	ASSERT_GT(read_attr(ITEM_B "/storeme", buf, sizeof(buf)), 0);
	EXPECT_STREQ(buf, "22\n");
}

TEST_F(configfs, group_create_and_drop)
{
	struct stat st;

	/* 03-group-children hands out groups that take items of their own. */
	ASSERT_EQ(mkdir(GROUP, 0755), 0);
	EXPECT_EQ(stat(GROUP "/description", &st), 0);

	ASSERT_EQ(mkdir(GROUP_ITEM, 0755), 0);
	EXPECT_EQ(stat(GROUP_ITEM "/storeme", &st), 0);

	ASSERT_EQ(rmdir(GROUP), -1);
	EXPECT_EQ(errno, ENOTEMPTY);

	ASSERT_EQ(rmdir(GROUP_ITEM), 0);
	ASSERT_EQ(rmdir(GROUP), 0);
}

TEST_F(configfs, rename_item)
{
	ASSERT_EQ(mkdir(ITEM_A, 0755), 0);
	ASSERT_EQ(rename(ITEM_A, ITEM_B), -1);
	EXPECT_EQ(errno, EPERM);
}

TEST_F(configfs, symlink_without_allow_link)
{
	ASSERT_EQ(mkdir(ITEM_A, 0755), 0);
	ASSERT_EQ(symlink(ITEM_A, SIMPLE "/kselftest-link"), -1);
	EXPECT_EQ(errno, EPERM);
}

TEST_F(configfs, symlink_and_unlink)
{
	char buf[PATH_MAX];
	struct stat st;
	ssize_t n;

	ASSERT_EQ(mkdir(ITEM_A, 0755), 0);
	ASSERT_EQ(mkdir(LINK_SRC, 0755), 0);

	ASSERT_EQ(symlink(ITEM_A, LINK), 0);

	/* configfs stores its own body, a path relative to the link. */
	n = readlink(LINK, buf, sizeof(buf) - 1);
	ASSERT_GT(n, 0);
	buf[n] = '\0';
	EXPECT_STREQ(buf, "../../" ITEM_A);
	EXPECT_EQ(stat(LINK "/storeme", &st), 0);

	/* ->allow_link() ran on the source, not on the target. */
	ASSERT_GT(read_attr(LINK_SRC "/nlinks", buf, sizeof(buf)), 0);
	EXPECT_STREQ(buf, "1\n");

	ASSERT_EQ(unlink(LINK), 0);
	ASSERT_GT(read_attr(LINK_SRC "/nlinks", buf, sizeof(buf)), 0);
	EXPECT_STREQ(buf, "0\n");
}

TEST_F(configfs, symlink_pins_both_ends)
{
	ASSERT_EQ(mkdir(ITEM_A, 0755), 0);
	ASSERT_EQ(mkdir(LINK_SRC, 0755), 0);
	ASSERT_EQ(symlink(ITEM_A, LINK), 0);

	/* A linked item cannot go away under the link. */
	ASSERT_EQ(rmdir(ITEM_A), -1);
	EXPECT_EQ(errno, EBUSY);

	/* The link counts as a child of its source. */
	ASSERT_EQ(rmdir(LINK_SRC), -1);
	EXPECT_EQ(errno, ENOTEMPTY);

	ASSERT_EQ(unlink(LINK), 0);
	EXPECT_EQ(rmdir(ITEM_A), 0);
}

TEST_F(configfs, symlink_target_outside_configfs)
{
	ASSERT_EQ(mkdir(LINK_SRC, 0755), 0);
	ASSERT_EQ(symlink("/", LINK), -1);
	EXPECT_EQ(errno, EPERM);
}

TEST_F(configfs, symlink_target_missing)
{
	ASSERT_EQ(mkdir(LINK_SRC, 0755), 0);
	ASSERT_EQ(symlink(SIMPLE "/kselftest-gone", LINK), -1);
	EXPECT_EQ(errno, ENOENT);
}

TEST_F(configfs, symlink_target_is_an_attribute)
{
	ASSERT_EQ(mkdir(LINK_SRC, 0755), 0);

	/* The target is resolved with LOOKUP_DIRECTORY. */
	ASSERT_EQ(symlink(CHILDLESS "/storeme", LINK), -1);
	EXPECT_EQ(errno, ENOTDIR);
}

TEST_F(configfs, module_pinned_by_item)
{
	ASSERT_EQ(mkdir(ITEM_A, 0755), 0);

	/* mkdir() pins both the subsystem's module and the new item's. */
	ASSERT_EQ(syscall(__NR_delete_module, SAMPLE_MODULE, O_NONBLOCK), -1);
	if (errno == ENOSYS)
		SKIP(return, "kernel built without CONFIG_MODULE_UNLOAD");
	EXPECT_EQ(errno, EWOULDBLOCK);
}

TEST_HARNESS_MAIN
