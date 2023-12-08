// SPDX-License-Identifier: GPL-2.0-only
/*
 * tools/testing/selftests/kvm/lib/io.c
 *
 * Copyright (C) 2018, Google LLC.
 */

#include "test_util.h"

/* Test Write
 *
 * A wrapper for write(2), that automatically handles the following
 * special conditions:
 *
 *   + Interrupted system call (EINTR)
 *   + Write of less than requested amount
 *   + Non-block return (EAGAIN)
 *
 * For each of the above, an additional write is performed to automatically
 * continue writing the requested data.
 * There are also many cases where write(2) can return an unexpected
 * error (e.g. EIO).  Such errors cause a TEST_ASSERT failure.
 *
 * Note, for function signature compatibility with write(2), this function
 * returns the number of bytes written, but that value will always be equal
 * to the number of requested bytes.  All other conditions in this and
 * future enhancements to this function either automatically issue another
 * write(2) or cause a TEST_ASSERT failure.
 *
 * Args:
 *  fd    - Opened file descriptor to file to be written.
 *  count - Number of bytes to write.
 *
 * Output:
 *  buf   - Starting address of data to be written.
 *
 * Return:
 *  On success, number of bytes written.
 *  On failure, a TEST_ASSERT failure is caused.
 */
ssize_t test_write(int fd, const void *buf, size_t count)
{
	ssize_t rc;
	ssize_t num_written = 0;
	size_t num_left = count;
	const char *ptr = buf;

	/* Note: Count of zero is allowed (see "RETURN VALUE" portion of
	 * write(2) manpage for details.
	 */
	TEST_ASSERT(count >= 0, "Unexpected count, count: %li", count);

	do {
		rc = write(fd, ptr, num_left);

		switch (rc) {
		case -1:
			TEST_ASSERT(errno == EAGAIN || errno == EINTR,
				    "Unexpected write failure,\n"
				    "  rc: %zi errno: %i", rc, errno);
			continue;

		case 0:
			TEST_FAIL("Unexpected EOF,\n"
				  "  rc: %zi num_written: %zi num_left: %zu",
				  rc, num_written, num_left);
			break;

		default:
			TEST_ASSERT(rc >= 0, "Unexpected ret from write,\n"
				"  rc: %zi errno: %i", rc, errno);
			num_written += rc;
			num_left -= rc;
			ptr += rc;
			break;
		}
	} while (num_written < count);

	return num_written;
}

/* Test Read
 *
 * A wrapper for read(2), that automatically handles the following
 * special conditions:
 *
 *   + Interrupted system call (EINTR)
 *   + Read of less than requested amount
 *   + Non-block return (EAGAIN)
 *
 * For each of the above, an additional read is performed to automatically
 * continue reading the requested data.
 * There are also many cases where read(2) can return an unexpected
 * error (e.g. EIO).  Such errors cause a TEST_ASSERT failure.  Note,
 * it is expected that the file opened by fd at the current file position
 * contains at least the number of requested bytes to be read.  A TEST_ASSERT
 * failure is produced if an End-Of-File condition occurs, before all the
 * data is read.  It is the callers responsibility to assure that sufficient
 * data exists.
 *
 * Note, for function signature compatibility with read(2), this function
 * returns the number of bytes read, but that value will always be equal
 * to the number of requested bytes.  All other conditions in this and
 * future enhancements to this function either automatically issue another
 * read(2) or cause a TEST_ASSERT failure.
 *
 * Args:
 *  fd    - Opened file descriptor to file to be read.
 *  count - Number of bytes to read.
 *
 * Output:
 *  buf   - Starting address of where to write the bytes read.
 *
 * Return:
 *  On success, number of bytes read.
 *  On failure, a TEST_ASSERT failure is caused.
 */
ssize_t test_read(int fd, void *buf, size_t count)
{
	ssize_t rc;
	ssize_t num_read = 0;
	size_t num_left = count;
	char *ptr = buf;

	/* Note: Count of zero is allowed (see "If count is zero" portion of
	 * read(2) manpage for details.
	 */
	TEST_ASSERT(count >= 0, "Unexpected count, count: %li", count);

	do {
		rc = read(fd, ptr, num_left);

		switch (rc) {
		case -1:
			TEST_ASSERT(errno == EAGAIN || errno == EINTR,
				    "Unexpected read failure,\n"
				    "  rc: %zi errno: %i", rc, errno);
			break;

		case 0:
			TEST_FAIL("Unexpected EOF,\n"
				  "   rc: %zi num_read: %zi num_left: %zu",
				  rc, num_read, num_left);
			break;

		default:
			TEST_ASSERT(rc > 0, "Unexpected ret from read,\n"
				    "  rc: %zi errno: %i", rc, errno);
			num_read += rc;
			num_left -= rc;
			ptr += rc;
			break;
		}
	} while (num_read < count);

	return num_read;
}
