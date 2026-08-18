// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "kselftest.h"

static int lock_set(int fd, struct flock *fl)
{
	int ret;

	fl->l_pid = 0;		// needed for OFD locks
	fl->l_whence = SEEK_SET;
	ret = fcntl(fd, F_OFD_SETLK, fl);
	if (ret)
		ksft_perror("fcntl()");
	return ret;
}

static int lock_get(int fd, struct flock *fl)
{
	int ret;

	fl->l_pid = 0;		// needed for OFD locks
	fl->l_whence = SEEK_SET;
	ret = fcntl(fd, F_OFD_GETLK, fl);
	if (ret)
		ksft_perror("fcntl()");
	return ret;
}

int main(void)
{
	int rc;
	struct flock fl, fl2;
	int fd = open("/tmp/aa", O_RDWR | O_CREAT | O_EXCL, 0600);
	int fd2 = open("/tmp/aa", O_RDONLY);

	ksft_print_header();
	ksft_set_plan(4);

	unlink("/tmp/aa");
	assert(fd != -1);
	assert(fd2 != -1);
	ksft_print_msg("opened fds %i %i\n", fd, fd2);

	/* Set some read lock */
	fl.l_type = F_RDLCK;
	fl.l_start = 5;
	fl.l_len = 3;
	rc = lock_set(fd, &fl);
	ksft_test_result(rc == 0, "set OFD read lock on first fd\n");
	if (rc != 0)
		ksft_finished();

	/* Make sure read locks do not conflict on different fds. */
	fl.l_type = F_RDLCK;
	fl.l_start = 5;
	fl.l_len = 1;
	rc = lock_get(fd2, &fl);
	if (rc != 0)
		ksft_finished();
	if (fl.l_type != F_UNLCK)
		ksft_exit_fail_msg("read locks conflicted\n");

	/* Make sure read/write locks do conflict on different fds. */
	fl.l_type = F_WRLCK;
	fl.l_start = 5;
	fl.l_len = 1;
	rc = lock_get(fd2, &fl);
	if (rc != 0)
		ksft_finished();
	ksft_test_result(fl.l_type != F_UNLCK,
			 "read and write locks conflicted\n");
	if (fl.l_type == F_UNLCK)
		ksft_finished();

	/* Get info about the lock on first fd. */
	fl.l_type = F_UNLCK;
	fl.l_start = 5;
	fl.l_len = 1;
	rc = lock_get(fd, &fl);
	if (rc != 0)
		ksft_exit_fail_msg("F_OFD_GETLK with F_UNLCK not supported\n");
	ksft_test_result(fl.l_type != F_UNLCK,
			 "F_OFD_GETLK with F_UNLCK returned lock info\n");
	if (fl.l_type == F_UNLCK)
		ksft_exit_fail();
	ksft_print_msg("F_UNLCK test returns: locked, type %i pid %i len %zi\n",
		       fl.l_type, fl.l_pid, fl.l_len);

	/* Try the same but by locking everything by len==0. */
	fl2.l_type = F_UNLCK;
	fl2.l_start = 0;
	fl2.l_len = 0;
	rc = lock_get(fd, &fl2);
	if (rc != 0)
		ksft_exit_fail_msg
		    ("F_OFD_GETLK with F_UNLCK not supported\n");
	ksft_test_result(memcmp(&fl, &fl2, sizeof(fl)) == 0,
			 "F_UNLCK with len==0 returned the same\n");
	if (memcmp(&fl, &fl2, sizeof(fl))) {
		ksft_exit_fail_msg
		    ("F_UNLCK test returns: locked, type %i pid %i len %zi\n",
		     fl.l_type, fl.l_pid, fl.l_len);
	}

	/* Get info about the lock on second fd - no locks on it. */
	fl.l_type = F_UNLCK;
	fl.l_start = 0;
	fl.l_len = 0;
	lock_get(fd2, &fl);
	ksft_test_result(fl.l_type == F_UNLCK,
			 "F_OFD_GETLK with F_UNLCK return lock info from another fd\n");

	ksft_finished();
}
