// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "../kselftest.h"

static int lock_set(int fd, struct flock *fl)
{
	int ret;

	fl->l_pid = 0;		// needed for OFD locks
	fl->l_whence = SEEK_SET;
	ret = fcntl(fd, F_OFD_SETLK, fl);
	if (ret)
		perror("fcntl()");
	return ret;
}

static int lock_get(int fd, struct flock *fl)
{
	int ret;

	fl->l_pid = 0;		// needed for OFD locks
	fl->l_whence = SEEK_SET;
	ret = fcntl(fd, F_OFD_GETLK, fl);
	if (ret)
		perror("fcntl()");
	return ret;
}

int main(void)
{
	int rc;
	struct flock fl, fl2;
	int fd = open("/tmp/aa", O_RDWR | O_CREAT | O_EXCL, 0600);
	int fd2 = open("/tmp/aa", O_RDONLY);

	unlink("/tmp/aa");
	assert(fd != -1);
	assert(fd2 != -1);
	ksft_print_msg("[INFO] opened fds %i %i\n", fd, fd2);

	/* Set some read lock */
	fl.l_type = F_RDLCK;
	fl.l_start = 5;
	fl.l_len = 3;
	rc = lock_set(fd, &fl);
	if (rc == 0) {
		ksft_print_msg
		    ("[SUCCESS] set OFD read lock on first fd\n");
	} else {
		ksft_print_msg("[FAIL] to set OFD read lock on first fd\n");
		return -1;
	}
	/* Make sure read locks do not conflict on different fds. */
	fl.l_type = F_RDLCK;
	fl.l_start = 5;
	fl.l_len = 1;
	rc = lock_get(fd2, &fl);
	if (rc != 0)
		return -1;
	if (fl.l_type != F_UNLCK) {
		ksft_print_msg("[FAIL] read locks conflicted\n");
		return -1;
	}
	/* Make sure read/write locks do conflict on different fds. */
	fl.l_type = F_WRLCK;
	fl.l_start = 5;
	fl.l_len = 1;
	rc = lock_get(fd2, &fl);
	if (rc != 0)
		return -1;
	if (fl.l_type != F_UNLCK) {
		ksft_print_msg
		    ("[SUCCESS] read and write locks conflicted\n");
	} else {
		ksft_print_msg
		    ("[SUCCESS] read and write locks not conflicted\n");
		return -1;
	}
	/* Get info about the lock on first fd. */
	fl.l_type = F_UNLCK;
	fl.l_start = 5;
	fl.l_len = 1;
	rc = lock_get(fd, &fl);
	if (rc != 0) {
		ksft_print_msg
		    ("[FAIL] F_OFD_GETLK with F_UNLCK not supported\n");
		return -1;
	}
	if (fl.l_type != F_UNLCK) {
		ksft_print_msg
		    ("[SUCCESS] F_UNLCK test returns: locked, type %i pid %i len %zi\n",
		     fl.l_type, fl.l_pid, fl.l_len);
	} else {
		ksft_print_msg
		    ("[FAIL] F_OFD_GETLK with F_UNLCK did not return lock info\n");
		return -1;
	}
	/* Try the same but by locking everything by len==0. */
	fl2.l_type = F_UNLCK;
	fl2.l_start = 0;
	fl2.l_len = 0;
	rc = lock_get(fd, &fl2);
	if (rc != 0) {
		ksft_print_msg
		    ("[FAIL] F_OFD_GETLK with F_UNLCK not supported\n");
		return -1;
	}
	if (memcmp(&fl, &fl2, sizeof(fl))) {
		ksft_print_msg
		    ("[FAIL] F_UNLCK test returns: locked, type %i pid %i len %zi\n",
		     fl.l_type, fl.l_pid, fl.l_len);
		return -1;
	}
	ksft_print_msg("[SUCCESS] F_UNLCK with len==0 returned the same\n");
	/* Get info about the lock on second fd - no locks on it. */
	fl.l_type = F_UNLCK;
	fl.l_start = 0;
	fl.l_len = 0;
	lock_get(fd2, &fl);
	if (fl.l_type != F_UNLCK) {
		ksft_print_msg
		    ("[FAIL] F_OFD_GETLK with F_UNLCK return lock info from another fd\n");
		return -1;
	}
	return 0;
}
