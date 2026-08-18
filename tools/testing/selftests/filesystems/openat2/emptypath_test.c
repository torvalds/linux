// SPDX-License-Identifier: GPL-2.0-or-later

#define _GNU_SOURCE
#define __SANE_USERSPACE_TYPES__
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "kselftest_harness.h"

#ifndef O_EMPTYPATH
#define O_EMPTYPATH	(1 << 26)
#endif

#define EMPTYPATH_TEST_FILE "/tmp/emptypath_test"

FIXTURE(emptypath) {
	int opath_fd;
};

FIXTURE_SETUP(emptypath)
{
	int fd;

	self->opath_fd = -1;

	fd = open(EMPTYPATH_TEST_FILE, O_CREAT | O_WRONLY, S_IRWXU);
	ASSERT_GE(fd, 0) {
		TH_LOG("create %s: %s", EMPTYPATH_TEST_FILE, strerror(errno));
	}
	close(fd);

	self->opath_fd = open(EMPTYPATH_TEST_FILE, O_PATH);
	ASSERT_GE(self->opath_fd, 0) {
		TH_LOG("open %s O_PATH: %s", EMPTYPATH_TEST_FILE, strerror(errno));
	}
}

FIXTURE_TEARDOWN(emptypath)
{
	if (self->opath_fd >= 0)
		close(self->opath_fd);
	unlink(EMPTYPATH_TEST_FILE);
}

/* An empty path is rejected with ENOENT unless O_EMPTYPATH is set. */
TEST_F(emptypath, without_flag_returns_enoent)
{
	int fd = openat(self->opath_fd, "", O_RDONLY);

	if (fd >= 0)
		close(fd);
	ASSERT_LT(fd, 0) {
		TH_LOG("empty path without O_EMPTYPATH unexpectedly succeeded");
	}
	EXPECT_EQ(errno, ENOENT) {
		TH_LOG("expected ENOENT, got %s", strerror(errno));
	}
}

/* O_EMPTYPATH reopens the O_PATH fd through an empty path. */
TEST_F(emptypath, reopens_opath_fd)
{
	int fd = openat(self->opath_fd, "", O_RDONLY | O_EMPTYPATH);

	if (fd < 0 && errno == EINVAL)
		SKIP(return, "O_EMPTYPATH not supported");

	ASSERT_GE(fd, 0) {
		TH_LOG("O_EMPTYPATH failed: %s", strerror(errno));
	}
	close(fd);
}

TEST_HARNESS_MAIN
