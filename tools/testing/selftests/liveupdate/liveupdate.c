// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

/*
 * Selftests for the Live Update Orchestrator.
 * This test suite verifies the functionality and behavior of the
 * /dev/liveupdate character device and its session management capabilities.
 *
 * Tests include:
 * - Device access: basic open/close, and enforcement of exclusive access.
 * - Session management: creation of unique sessions, and duplicate name detection.
 * - Resource preservation: successfully preserving individual and multiple memfds,
 *   verifying contents remain accessible.
 * - Complex multi-session scenarios involving mixed empty and populated files.
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/liveupdate.h>

#include "../kselftest.h"
#include "../kselftest_harness.h"

#define LIVEUPDATE_DEV "/dev/liveupdate"

FIXTURE(liveupdate_device) {
	int fd1;
	int fd2;
};

FIXTURE_SETUP(liveupdate_device)
{
	self->fd1 = -1;
	self->fd2 = -1;
}

FIXTURE_TEARDOWN(liveupdate_device)
{
	if (self->fd1 >= 0)
		close(self->fd1);
	if (self->fd2 >= 0)
		close(self->fd2);
}

/*
 * Test Case: Basic Open and Close
 *
 * Verifies that the /dev/liveupdate device can be opened and subsequently
 * closed without errors. Skips if the device does not exist.
 */
TEST_F(liveupdate_device, basic_open_close)
{
	self->fd1 = open(LIVEUPDATE_DEV, O_RDWR);

	if (self->fd1 < 0 && errno == ENOENT)
		SKIP(return, "%s does not exist.", LIVEUPDATE_DEV);

	ASSERT_GE(self->fd1, 0);
	ASSERT_EQ(close(self->fd1), 0);
	self->fd1 = -1;
}

/*
 * Test Case: Exclusive Open Enforcement
 *
 * Verifies that the /dev/liveupdate device can only be opened by one process
 * at a time. It checks that a second attempt to open the device fails with
 * the EBUSY error code.
 */
TEST_F(liveupdate_device, exclusive_open)
{
	self->fd1 = open(LIVEUPDATE_DEV, O_RDWR);

	if (self->fd1 < 0 && errno == ENOENT)
		SKIP(return, "%s does not exist.", LIVEUPDATE_DEV);

	ASSERT_GE(self->fd1, 0);
	self->fd2 = open(LIVEUPDATE_DEV, O_RDWR);
	EXPECT_LT(self->fd2, 0);
	EXPECT_EQ(errno, EBUSY);
}

/* Helper function to create a LUO session via ioctl. */
static int create_session(int lu_fd, const char *name)
{
	struct liveupdate_ioctl_create_session args = {};

	args.size = sizeof(args);
	strncpy((char *)args.name, name, sizeof(args.name) - 1);

	if (ioctl(lu_fd, LIVEUPDATE_IOCTL_CREATE_SESSION, &args))
		return -errno;

	return args.fd;
}

/*
 * Test Case: Create Duplicate Session
 *
 * Verifies that attempting to create two sessions with the same name fails
 * on the second attempt with EEXIST.
 */
TEST_F(liveupdate_device, create_duplicate_session)
{
	int session_fd1, session_fd2;

	self->fd1 = open(LIVEUPDATE_DEV, O_RDWR);
	if (self->fd1 < 0 && errno == ENOENT)
		SKIP(return, "%s does not exist", LIVEUPDATE_DEV);

	ASSERT_GE(self->fd1, 0);

	session_fd1 = create_session(self->fd1, "duplicate-session-test");
	ASSERT_GE(session_fd1, 0);

	session_fd2 = create_session(self->fd1, "duplicate-session-test");
	EXPECT_LT(session_fd2, 0);
	EXPECT_EQ(-session_fd2, EEXIST);

	ASSERT_EQ(close(session_fd1), 0);
}

/*
 * Test Case: Create Distinct Sessions
 *
 * Verifies that creating two sessions with different names succeeds.
 */
TEST_F(liveupdate_device, create_distinct_sessions)
{
	int session_fd1, session_fd2;

	self->fd1 = open(LIVEUPDATE_DEV, O_RDWR);
	if (self->fd1 < 0 && errno == ENOENT)
		SKIP(return, "%s does not exist", LIVEUPDATE_DEV);

	ASSERT_GE(self->fd1, 0);

	session_fd1 = create_session(self->fd1, "distinct-session-1");
	ASSERT_GE(session_fd1, 0);

	session_fd2 = create_session(self->fd1, "distinct-session-2");
	ASSERT_GE(session_fd2, 0);

	ASSERT_EQ(close(session_fd1), 0);
	ASSERT_EQ(close(session_fd2), 0);
}

static int preserve_fd(int session_fd, int fd_to_preserve, __u64 token)
{
	struct liveupdate_session_preserve_fd args = {};

	args.size = sizeof(args);
	args.fd = fd_to_preserve;
	args.token = token;

	if (ioctl(session_fd, LIVEUPDATE_SESSION_PRESERVE_FD, &args))
		return -errno;

	return 0;
}

/*
 * Test Case: Preserve MemFD
 *
 * Verifies that a valid memfd can be successfully preserved in a session and
 * that its contents remain intact after the preservation call.
 */
TEST_F(liveupdate_device, preserve_memfd)
{
	const char *test_str = "hello liveupdate";
	char read_buf[64] = {};
	int session_fd, mem_fd;

	self->fd1 = open(LIVEUPDATE_DEV, O_RDWR);
	if (self->fd1 < 0 && errno == ENOENT)
		SKIP(return, "%s does not exist", LIVEUPDATE_DEV);
	ASSERT_GE(self->fd1, 0);

	session_fd = create_session(self->fd1, "preserve-memfd-test");
	ASSERT_GE(session_fd, 0);

	mem_fd = memfd_create("test-memfd", 0);
	ASSERT_GE(mem_fd, 0);

	ASSERT_EQ(write(mem_fd, test_str, strlen(test_str)), strlen(test_str));
	ASSERT_EQ(preserve_fd(session_fd, mem_fd, 0x1234), 0);
	ASSERT_EQ(close(session_fd), 0);

	ASSERT_EQ(lseek(mem_fd, 0, SEEK_SET), 0);
	ASSERT_EQ(read(mem_fd, read_buf, sizeof(read_buf)), strlen(test_str));
	ASSERT_STREQ(read_buf, test_str);
	ASSERT_EQ(close(mem_fd), 0);
}

/*
 * Test Case: Preserve Multiple MemFDs
 *
 * Verifies that multiple memfds can be preserved in a single session,
 * each with a unique token, and that their contents remain distinct and
 * correct after preservation.
 */
TEST_F(liveupdate_device, preserve_multiple_memfds)
{
	const char *test_str1 = "data for memfd one";
	const char *test_str2 = "data for memfd two";
	char read_buf[64] = {};
	int session_fd, mem_fd1, mem_fd2;

	self->fd1 = open(LIVEUPDATE_DEV, O_RDWR);
	if (self->fd1 < 0 && errno == ENOENT)
		SKIP(return, "%s does not exist", LIVEUPDATE_DEV);
	ASSERT_GE(self->fd1, 0);

	session_fd = create_session(self->fd1, "preserve-multi-memfd-test");
	ASSERT_GE(session_fd, 0);

	mem_fd1 = memfd_create("test-memfd-1", 0);
	ASSERT_GE(mem_fd1, 0);
	mem_fd2 = memfd_create("test-memfd-2", 0);
	ASSERT_GE(mem_fd2, 0);

	ASSERT_EQ(write(mem_fd1, test_str1, strlen(test_str1)), strlen(test_str1));
	ASSERT_EQ(write(mem_fd2, test_str2, strlen(test_str2)), strlen(test_str2));

	ASSERT_EQ(preserve_fd(session_fd, mem_fd1, 0xAAAA), 0);
	ASSERT_EQ(preserve_fd(session_fd, mem_fd2, 0xBBBB), 0);

	memset(read_buf, 0, sizeof(read_buf));
	ASSERT_EQ(lseek(mem_fd1, 0, SEEK_SET), 0);
	ASSERT_EQ(read(mem_fd1, read_buf, sizeof(read_buf)), strlen(test_str1));
	ASSERT_STREQ(read_buf, test_str1);

	memset(read_buf, 0, sizeof(read_buf));
	ASSERT_EQ(lseek(mem_fd2, 0, SEEK_SET), 0);
	ASSERT_EQ(read(mem_fd2, read_buf, sizeof(read_buf)), strlen(test_str2));
	ASSERT_STREQ(read_buf, test_str2);

	ASSERT_EQ(close(mem_fd1), 0);
	ASSERT_EQ(close(mem_fd2), 0);
	ASSERT_EQ(close(session_fd), 0);
}

/*
 * Test Case: Preserve Complex Scenario
 *
 * Verifies a more complex scenario with multiple sessions and a mix of empty
 * and non-empty memfds distributed across them.
 */
TEST_F(liveupdate_device, preserve_complex_scenario)
{
	const char *data1 = "data for session 1";
	const char *data2 = "data for session 2";
	char read_buf[64] = {};
	int session_fd1, session_fd2;
	int mem_fd_data1, mem_fd_empty1, mem_fd_data2, mem_fd_empty2;

	self->fd1 = open(LIVEUPDATE_DEV, O_RDWR);
	if (self->fd1 < 0 && errno == ENOENT)
		SKIP(return, "%s does not exist", LIVEUPDATE_DEV);
	ASSERT_GE(self->fd1, 0);

	session_fd1 = create_session(self->fd1, "complex-session-1");
	ASSERT_GE(session_fd1, 0);
	session_fd2 = create_session(self->fd1, "complex-session-2");
	ASSERT_GE(session_fd2, 0);

	mem_fd_data1 = memfd_create("data1", 0);
	ASSERT_GE(mem_fd_data1, 0);
	ASSERT_EQ(write(mem_fd_data1, data1, strlen(data1)), strlen(data1));

	mem_fd_empty1 = memfd_create("empty1", 0);
	ASSERT_GE(mem_fd_empty1, 0);

	mem_fd_data2 = memfd_create("data2", 0);
	ASSERT_GE(mem_fd_data2, 0);
	ASSERT_EQ(write(mem_fd_data2, data2, strlen(data2)), strlen(data2));

	mem_fd_empty2 = memfd_create("empty2", 0);
	ASSERT_GE(mem_fd_empty2, 0);

	ASSERT_EQ(preserve_fd(session_fd1, mem_fd_data1, 0x1111), 0);
	ASSERT_EQ(preserve_fd(session_fd1, mem_fd_empty1, 0x2222), 0);
	ASSERT_EQ(preserve_fd(session_fd2, mem_fd_data2, 0x3333), 0);
	ASSERT_EQ(preserve_fd(session_fd2, mem_fd_empty2, 0x4444), 0);

	ASSERT_EQ(lseek(mem_fd_data1, 0, SEEK_SET), 0);
	ASSERT_EQ(read(mem_fd_data1, read_buf, sizeof(read_buf)), strlen(data1));
	ASSERT_STREQ(read_buf, data1);

	memset(read_buf, 0, sizeof(read_buf));
	ASSERT_EQ(lseek(mem_fd_data2, 0, SEEK_SET), 0);
	ASSERT_EQ(read(mem_fd_data2, read_buf, sizeof(read_buf)), strlen(data2));
	ASSERT_STREQ(read_buf, data2);

	ASSERT_EQ(lseek(mem_fd_empty1, 0, SEEK_SET), 0);
	ASSERT_EQ(read(mem_fd_empty1, read_buf, sizeof(read_buf)), 0);

	ASSERT_EQ(lseek(mem_fd_empty2, 0, SEEK_SET), 0);
	ASSERT_EQ(read(mem_fd_empty2, read_buf, sizeof(read_buf)), 0);

	ASSERT_EQ(close(mem_fd_data1), 0);
	ASSERT_EQ(close(mem_fd_empty1), 0);
	ASSERT_EQ(close(mem_fd_data2), 0);
	ASSERT_EQ(close(mem_fd_empty2), 0);
	ASSERT_EQ(close(session_fd1), 0);
	ASSERT_EQ(close(session_fd2), 0);
}

/*
 * Test Case: Preserve Unsupported File Descriptor
 *
 * Verifies that attempting to preserve a file descriptor that does not have
 * a registered Live Update handler fails gracefully.
 * Uses /dev/null as a representative of a file type (character device)
 * that is not supported by the orchestrator.
 */
TEST_F(liveupdate_device, preserve_unsupported_fd)
{
	int session_fd, unsupported_fd;
	int ret;

	self->fd1 = open(LIVEUPDATE_DEV, O_RDWR);
	if (self->fd1 < 0 && errno == ENOENT)
		SKIP(return, "%s does not exist", LIVEUPDATE_DEV);
	ASSERT_GE(self->fd1, 0);

	session_fd = create_session(self->fd1, "unsupported-fd-test");
	ASSERT_GE(session_fd, 0);

	unsupported_fd = open("/dev/null", O_RDWR);
	ASSERT_GE(unsupported_fd, 0);

	ret = preserve_fd(session_fd, unsupported_fd, 0xDEAD);
	EXPECT_EQ(ret, -ENOENT);

	ASSERT_EQ(close(unsupported_fd), 0);
	ASSERT_EQ(close(session_fd), 0);
}

TEST_HARNESS_MAIN
