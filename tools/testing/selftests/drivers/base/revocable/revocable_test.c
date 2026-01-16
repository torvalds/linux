// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 Google LLC
 *
 * A selftest for the revocable API.
 *
 * The test cases cover the following scenarios:
 *
 * - Basic: Verifies that a consumer can successfully access the resource
 *   provided via the provider.
 *
 * - Revocation: Verifies that after the provider revokes the resource,
 *   the consumer correctly receives a NULL pointer on a subsequent access.
 *
 * - Try Access Macro: Same as "Revocation" but uses the
 *   REVOCABLE_TRY_ACCESS_WITH() and REVOCABLE_TRY_ACCESS_SCOPED().
 */

#include <fcntl.h>
#include <unistd.h>

#include "../../../kselftest_harness.h"

#define DEBUGFS_PATH "/sys/kernel/debug/revocable_test"
#define TEST_CMD_RESOURCE_GONE "resource_gone"
#define TEST_DATA "12345678"
#define TEST_MAGIC_OFFSET 0x1234
#define TEST_MAGIC_OFFSET2 0x5678

FIXTURE(revocable_fixture) {
	int pfd;
	int cfd;
};

FIXTURE_SETUP(revocable_fixture) {
	int ret;

	self->pfd = open(DEBUGFS_PATH "/provider", O_WRONLY);
	ASSERT_NE(-1, self->pfd)
		TH_LOG("failed to open provider fd");

	ret = write(self->pfd, TEST_DATA, strlen(TEST_DATA));
	ASSERT_NE(-1, ret) {
		close(self->pfd);
		TH_LOG("failed to write test data");
	}

	self->cfd = open(DEBUGFS_PATH "/consumer", O_RDONLY);
	ASSERT_NE(-1, self->cfd)
		TH_LOG("failed to open consumer fd");
}

FIXTURE_TEARDOWN(revocable_fixture) {
	close(self->cfd);
	close(self->pfd);
}

/*
 * ASSERT_* is only available in TEST or TEST_F block.  Use
 * macro for the helper.
 */
#define READ_TEST_DATA(_fd, _offset, _data, _msg)			\
	do {								\
		int ret;						\
									\
		ret = lseek(_fd, _offset, SEEK_SET);			\
		ASSERT_NE(-1, ret)					\
			TH_LOG("failed to lseek");			\
									\
		ret = read(_fd, _data, sizeof(_data) - 1);		\
		ASSERT_NE(-1, ret)					\
			TH_LOG(_msg);					\
		data[ret] = '\0';					\
	} while (0)

TEST_F(revocable_fixture, basic) {
	char data[16];

	READ_TEST_DATA(self->cfd, 0, data, "failed to read test data");
	EXPECT_STREQ(TEST_DATA, data);
}

TEST_F(revocable_fixture, revocation) {
	char data[16];
	int ret;

	READ_TEST_DATA(self->cfd, 0, data, "failed to read test data");
	EXPECT_STREQ(TEST_DATA, data);

	ret = write(self->pfd, TEST_CMD_RESOURCE_GONE,
		    strlen(TEST_CMD_RESOURCE_GONE));
	ASSERT_NE(-1, ret)
		TH_LOG("failed to write resource gone cmd");

	READ_TEST_DATA(self->cfd, 0, data,
		       "failed to read test data after resource gone");
	EXPECT_STREQ("(null)", data);
}

TEST_F(revocable_fixture, try_access_macro) {
	char data[16];
	int ret;

	READ_TEST_DATA(self->cfd, TEST_MAGIC_OFFSET, data,
		       "failed to read test data");
	EXPECT_STREQ(TEST_DATA, data);

	ret = write(self->pfd, TEST_CMD_RESOURCE_GONE,
		    strlen(TEST_CMD_RESOURCE_GONE));
	ASSERT_NE(-1, ret)
		TH_LOG("failed to write resource gone cmd");

	READ_TEST_DATA(self->cfd, TEST_MAGIC_OFFSET, data,
		       "failed to read test data after resource gone");
	EXPECT_STREQ("(null)", data);
}

TEST_F(revocable_fixture, try_access_macro2) {
	char data[16];
	int ret;

	READ_TEST_DATA(self->cfd, TEST_MAGIC_OFFSET2, data,
		       "failed to read test data");
	EXPECT_STREQ(TEST_DATA, data);

	ret = write(self->pfd, TEST_CMD_RESOURCE_GONE,
		    strlen(TEST_CMD_RESOURCE_GONE));
	ASSERT_NE(-1, ret)
		TH_LOG("failed to write resource gone cmd");

	READ_TEST_DATA(self->cfd, TEST_MAGIC_OFFSET2, data,
		       "failed to read test data after resource gone");
	EXPECT_STREQ("(null)", data);
}

TEST_HARNESS_MAIN
