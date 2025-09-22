/*-
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Authors: Doug Rabson <dfr@rabson.org>
 * Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/tools/regression/file/flock/flock.c,v 1.3 2008/06/26 10:21:54 dfr Exp $
 */

#include <sys/time.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#include "util.h"

int verbose = 0;

static void
ignore_alarm(int __unused sig)
{
}

static int
safe_kill(pid_t pid, int sig)
{
	int save_errno;
	int status;

	save_errno = errno;
	errno = 0;
	status = kill(pid, sig);
	errno = save_errno;

	return (status);
}

/*
 * Test 1 - F_GETLK on unlocked region
 *
 * If no lock is found that would prevent this lock from being
 * created, the structure is left unchanged by this function call
 * except for the lock type which is set to F_UNLCK.
 */
static int
test1(int fd)
{
	struct flock fl1, fl2;

	memset(&fl1, 1, sizeof(fl1));
	fl1.l_type = F_WRLCK;
	fl1.l_whence = SEEK_SET;
	fl2 = fl1;

	if (fcntl(fd, F_GETLK, &fl1) < 0)
		err(1, "F_GETLK");

	if (verbose) printf("1 - F_GETLK on unlocked region: ");
	FAIL(fl1.l_start != fl2.l_start);
	FAIL(fl1.l_len != fl2.l_len);
	FAIL(fl1.l_pid != fl2.l_pid);
	FAIL(fl1.l_type != F_UNLCK);
	FAIL(fl1.l_whence != fl2.l_whence);

	SUCCEED;
}

/*
 * Test 2 - F_SETLK on locked region
 *
 * If a shared or exclusive lock cannot be set, fcntl returns
 * immediately with EACCES or EAGAIN.
 */
static int
test2(int fd)
{
	/*
	 * We create a child process to hold the lock which we will
	 * test. We use a pipe to communicate with the child.
	 */
	int pid;
	int pfd[2];
	struct flock fl;
	char ch;
	int res;

	if (pipe(pfd) < 0)
		err(1, "pipe");

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;

	pid = fork();
	if (pid < 0)
		err(1, "fork");

	if (pid == 0) {
		/*
		 * We are the child. We set a write lock and then
		 * write one byte back to the parent to tell it. The
		 * parent will kill us when its done.
		 */
		if (fcntl(fd, F_SETLK, &fl) < 0)
			err(1, "F_SETLK (child)");
		if (write(pfd[1], "a", 1) < 0)
			err(1, "writing to pipe (child)");
		pause();
		exit(0);
	}

	/*
	 * Wait until the child has set its lock and then perform the
	 * test.
	 */
	if (read(pfd[0], &ch, 1) != 1)
		err(1, "reading from pipe (child)");

	/*
	 * fcntl should return -1 with errno set to either EACCES or
	 * EAGAIN.
	 */
	if (verbose) printf("2 - F_SETLK on locked region: ");
	res = fcntl(fd, F_SETLK, &fl);
	safe_kill(pid, SIGTERM);
	safe_waitpid(pid);
	close(pfd[0]);
	close(pfd[1]);
	FAIL(res == 0);
	FAIL(errno != EACCES && errno != EAGAIN);

	SUCCEED;
}

/*
 * Test 3 - F_SETLKW on locked region
 *
 * If a shared or exclusive lock is blocked by other locks, the
 * process waits until the request can be satisfied.
 *
 * XXX this test hangs on FreeBSD NFS filesystems due to limitations
 * in FreeBSD's client (and server) lockd implementation.
 */
static int
test3(int fd)
{
	/*
	 * We create a child process to hold the lock which we will
	 * test. We use a pipe to communicate with the child.
	 */
	int pid;
	int pfd[2];
	struct flock fl;
	char ch;
	int res;

	if (pipe(pfd) < 0)
		err(1, "pipe");

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;

	pid = fork();
	if (pid < 0)
		err(1, "fork");

	if (pid == 0) {
		/*
		 * We are the child. We set a write lock and then
		 * write one byte back to the parent to tell it. The
		 * parent will kill us when its done.
		 */
		if (fcntl(fd, F_SETLK, &fl) < 0)
			err(1, "F_SETLK (child)");
		if (write(pfd[1], "a", 1) < 0)
			err(1, "writing to pipe (child)");
		pause();
		exit(0);
	}

	/*
	 * Wait until the child has set its lock and then perform the
	 * test.
	 */
	if (read(pfd[0], &ch, 1) != 1)
		err(1, "reading from pipe (child)");

	/*
	 * fcntl should wait until the alarm and then return -1 with
	 * errno set to EINTR.
	 */
	if (verbose) printf("3 - F_SETLKW on locked region: ");

	alarm(1);

	res = fcntl(fd, F_SETLKW, &fl);
	safe_kill(pid, SIGTERM);
	safe_waitpid(pid);
	close(pfd[0]);
	close(pfd[1]);
	FAIL(res == 0);
	FAIL(errno != EINTR);

	SUCCEED;
}

/*
 * Test 4 - F_GETLK on locked region
 *
 * Get the first lock that blocks the lock.
 */
static int
test4(int fd)
{
	/*
	 * We create a child process to hold the lock which we will
	 * test. We use a pipe to communicate with the child.
	 */
	int pid;
	int pfd[2];
	struct flock fl;
	char ch;

	if (pipe(pfd) < 0)
		err(1, "pipe");

	fl.l_start = 0;
	fl.l_len = 99;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;

	pid = fork();
	if (pid < 0)
		err(1, "fork");

	if (pid == 0) {
		/*
		 * We are the child. We set a write lock and then
		 * write one byte back to the parent to tell it. The
		 * parent will kill us when its done.
		 */
		if (fcntl(fd, F_SETLK, &fl) < 0)
			err(1, "F_SETLK (child)");
		if (write(pfd[1], "a", 1) < 0)
			err(1, "writing to pipe (child)");
		pause();
		exit(0);
	}

	/*
	 * Wait until the child has set its lock and then perform the
	 * test.
	 */
	if (read(pfd[0], &ch, 1) != 1)
		err(1, "reading from pipe (child)");

	/*
	 * fcntl should return a lock structure reflecting the lock we
	 * made in the child process.
	 */
	if (fcntl(fd, F_GETLK, &fl) < 0)
		err(1, "F_GETLK");

	if (verbose) printf("4 - F_GETLK on locked region: ");
	FAIL(fl.l_start != 0);
	FAIL(fl.l_len != 99);
	FAIL(fl.l_type != F_WRLCK);
	FAIL(fl.l_pid != pid);

	safe_kill(pid, SIGTERM);
	safe_waitpid(pid);
	close(pfd[0]);
	close(pfd[1]);

	SUCCEED;
}

/*
 * Test 5 - F_SETLKW simple deadlock
 *
 * If a blocking shared lock request would cause a deadlock (i.e. the
 * lock request is blocked by a process which is itself blocked on a
 * lock currently owned by the process making the new request),
 * EDEADLK is returned.
 */
static int
test5(int fd)
{
	/*
	 * We create a child process to hold the lock which we will
	 * test. Because our test relies on the child process being
	 * blocked on the parent's lock, we can't easily use a pipe to
	 * synchronize so we just sleep in the parent to given the
	 * child a chance to setup.
	 *
	 * To create the deadlock condition, we arrange for the parent
	 * to lock the first byte of the file and the child to lock
	 * the second byte.  After locking the second byte, the child
	 * will attempt to lock the first byte of the file, and
	 * block. The parent will then attempt to lock the second byte
	 * (owned by the child) which should cause deadlock.
	 */
	int pid;
	struct flock fl;
	int res;

	/*
	 * Lock the first byte in the parent.
	 */
	fl.l_start = 0;
	fl.l_len = 1;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	if (fcntl(fd, F_SETLK, &fl) < 0)
		err(1, "F_SETLK 1 (parent)");

	pid = fork();
	if (pid < 0)
		err(1, "fork");

	if (pid == 0) {
		/*
		 * Lock the second byte in the child and then block on
		 * the parent's lock.
		 */
		fl.l_start = 1;
		if (fcntl(fd, F_SETLK, &fl) < 0)
			err(1, "F_SETLK (child)");
		fl.l_start = 0;
		if (fcntl(fd, F_SETLKW, &fl) < 0)
			err(1, "F_SETLKW (child)");
		exit(0);
	}

	/*
	 * Wait until the child has set its lock and then perform the
	 * test.
	 */
	sleep(1);

	/*
	 * fcntl should immediately return -1 with errno set to
	 * EDEADLK. If the alarm fires, we failed to detect the
	 * deadlock.
	 */
	alarm(1);
	if (verbose) printf("5 - F_SETLKW simple deadlock: ");

	fl.l_start = 1;
	res = fcntl(fd, F_SETLKW, &fl);
	safe_kill(pid, SIGTERM);
	safe_waitpid(pid);
	
	FAIL(res == 0);
	FAIL(errno != EDEADLK);

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = F_UNLCK;
	if (fcntl(fd, F_SETLK, &fl) < 0)
		err(1, "F_UNLCK");

	/*
	 * Cancel the alarm to avoid confusing later tests.
	 */
	alarm(0);

	SUCCEED;
}

/*
 * Test 6 - F_SETLKW complex deadlock.
 *
 * This test involves three process, P, C1 and C2. We set things up so
 * that P locks byte zero, C1 locks byte 1 and C2 locks byte 2. We
 * also block C2 by attempting to lock byte zero. Lastly, P attempts
 * to lock a range including byte 1 and 2. This represents a deadlock
 * (due to C2's blocking attempt to lock byte zero).
 */
static int
test6(int fd)
{
	/*
	 * Because our test relies on the child process being blocked
	 * on the parent's lock, we can't easily use a pipe to
	 * synchronize so we just sleep in the parent to given the
	 * children a chance to setup.
	 */
	int pid1, pid2;
	struct flock fl;
	int res;

	/*
	 * Lock the first byte in the parent.
	 */
	fl.l_start = 0;
	fl.l_len = 1;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	if (fcntl(fd, F_SETLK, &fl) < 0)
		err(1, "F_SETLK 1 (parent)");

	pid1 = fork();
	if (pid1 < 0)
		err(1, "fork");

	if (pid1 == 0) {
		/*
		 * C1
		 * Lock the second byte in the child and then sleep
		 */
		fl.l_start = 1;
		if (fcntl(fd, F_SETLK, &fl) < 0)
			err(1, "F_SETLK (child1)");
		pause();
		exit(0);
	}

	pid2 = fork();
	if (pid2 < 0)
		err(1, "fork");

	if (pid2 == 0) {
		/*
		 * C2
		 * Lock the third byte in the child and then block on
		 * the parent's lock.
		 */
		fl.l_start = 2;
		if (fcntl(fd, F_SETLK, &fl) < 0)
			err(1, "F_SETLK (child2)");
		fl.l_start = 0;
		if (fcntl(fd, F_SETLKW, &fl) < 0)
			err(1, "F_SETLKW (child2)");
		exit(0);
	}

	/*
	 * Wait until the children have set their locks and then
	 * perform the test.
	 */
	sleep(1);

	/*
	 * fcntl should immediately return -1 with errno set to
	 * EDEADLK. If the alarm fires, we failed to detect the
	 * deadlock.
	 */
	alarm(1);
	if (verbose) printf("6 - F_SETLKW complex deadlock: ");

	fl.l_start = 1;
	fl.l_len = 2;
	res = fcntl(fd, F_SETLKW, &fl);
	safe_kill(pid1, SIGTERM);
	safe_waitpid(pid1);
	safe_kill(pid2, SIGTERM);
	safe_waitpid(pid2);

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = F_UNLCK;
	if (fcntl(fd, F_SETLK, &fl) < 0)
		err(1, "F_UNLCK");

	FAIL(res == 0);
	FAIL(errno != EDEADLK);

	/*
	 * Cancel the alarm to avoid confusing later tests.
	 */
	alarm(0);

	SUCCEED;
}

/*
 * Test 7 - F_SETLK shared lock on exclusive locked region
 *
 * If a shared or exclusive lock cannot be set, fcntl returns
 * immediately with EACCES or EAGAIN.
 */
static int
test7(int fd)
{
	/*
	 * We create a child process to hold the lock which we will
	 * test. We use a pipe to communicate with the child.
	 */
	int pid;
	int pfd[2];
	struct flock fl;
	char ch;
	int res;

	if (pipe(pfd) < 0)
		err(1, "pipe");

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;

	pid = fork();
	if (pid < 0)
		err(1, "fork");

	if (pid == 0) {
		/*
		 * We are the child. We set a write lock and then
		 * write one byte back to the parent to tell it. The
		 * parent will kill us when its done.
		 */
		if (fcntl(fd, F_SETLK, &fl) < 0)
			err(1, "F_SETLK (child)");
		if (write(pfd[1], "a", 1) < 0)
			err(1, "writing to pipe (child)");
		pause();
		exit(0);
	}

	/*
	 * Wait until the child has set its lock and then perform the
	 * test.
	 */
	if (read(pfd[0], &ch, 1) != 1)
		err(1, "reading from pipe (child)");

	/*
	 * fcntl should wait until the alarm and then return -1 with
	 * errno set to EINTR.
	 */
	if (verbose) printf("7 - F_SETLK shared lock on exclusive locked region: ");

	fl.l_type = F_RDLCK;
	res = fcntl(fd, F_SETLK, &fl);
	safe_kill(pid, SIGTERM);
	safe_waitpid(pid);
	close(pfd[0]);
	close(pfd[1]);

	FAIL(res == 0);
	FAIL(errno != EACCES && errno != EAGAIN);

	SUCCEED;
}

/*
 * Test 8 - F_SETLK shared lock on share locked region
 *
 * When a shared lock is set on a segment of a file, other processes
 * shall be able to set shared locks on that segment or a portion of
 * it.
 */
static int
test8(int fd)
{
	/*
	 * We create a child process to hold the lock which we will
	 * test. We use a pipe to communicate with the child.
	 */
	int pid;
	int pfd[2];
	struct flock fl;
	char ch;
	int res;

	if (pipe(pfd) < 0)
		err(1, "pipe");

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = F_RDLCK;
	fl.l_whence = SEEK_SET;

	pid = fork();
	if (pid < 0)
		err(1, "fork");

	if (pid == 0) {
		/*
		 * We are the child. We set a write lock and then
		 * write one byte back to the parent to tell it. The
		 * parent will kill us when its done.
		 */
		if (fcntl(fd, F_SETLK, &fl) < 0)
			err(1, "F_SETLK (child)");
		if (write(pfd[1], "a", 1) < 0)
			err(1, "writing to pipe (child)");
		pause();
		exit(0);
	}

	/*
	 * Wait until the child has set its lock and then perform the
	 * test.
	 */
	if (read(pfd[0], &ch, 1) != 1)
		err(1, "reading from pipe (child)");

	/*
	 * fcntl should wait until the alarm and then return -1 with
	 * errno set to EINTR.
	 */
	if (verbose) printf("8 - F_SETLK shared lock on share locked region: ");

	fl.l_type = F_RDLCK;
	res = fcntl(fd, F_SETLK, &fl);

	safe_kill(pid, SIGTERM);
	safe_waitpid(pid);
	close(pfd[0]);
	close(pfd[1]);

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = F_UNLCK;
	if (fcntl(fd, F_SETLK, &fl) < 0)
		err(1, "F_UNLCK");

	FAIL(res != 0);

	SUCCEED;
}

/*
 * Test 9 - F_SETLK exclusive lock on share locked region
 *
 * If a shared or exclusive lock cannot be set, fcntl returns
 * immediately with EACCES or EAGAIN.
 */
static int
test9(int fd)
{
	/*
	 * We create a child process to hold the lock which we will
	 * test. We use a pipe to communicate with the child.
	 */
	int pid;
	int pfd[2];
	struct flock fl;
	char ch;
	int res;

	if (pipe(pfd) < 0)
		err(1, "pipe");

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = F_RDLCK;
	fl.l_whence = SEEK_SET;

	pid = fork();
	if (pid < 0)
		err(1, "fork");

	if (pid == 0) {
		/*
		 * We are the child. We set a write lock and then
		 * write one byte back to the parent to tell it. The
		 * parent will kill us when its done.
		 */
		if (fcntl(fd, F_SETLK, &fl) < 0)
			err(1, "F_SETLK (child)");
		if (write(pfd[1], "a", 1) < 0)
			err(1, "writing to pipe (child)");
		pause();
		exit(0);
	}

	/*
	 * Wait until the child has set its lock and then perform the
	 * test.
	 */
	if (read(pfd[0], &ch, 1) != 1)
		err(1, "reading from pipe (child)");

	/*
	 * fcntl should wait until the alarm and then return -1 with
	 * errno set to EINTR.
	 */
	if (verbose) printf("9 - F_SETLK exclusive lock on share locked region: ");

	fl.l_type = F_WRLCK;
	res = fcntl(fd, F_SETLK, &fl);
	safe_kill(pid, SIGTERM);
	safe_waitpid(pid);
	close(pfd[0]);
	close(pfd[1]);

	FAIL(res == 0);
	FAIL(errno != EACCES && errno != EAGAIN);

	SUCCEED;
}

/*
 * Test 10 - trying to set bogus pid or sysid values
 *
 * The l_pid and l_sysid fields are only used with F_GETLK to return
 * the process ID of the process holding a blocking lock and the
 * system ID of the system that owns that process
 */
static int
test10(int fd)
{
	/*
	 * We create a child process to hold the lock which we will
	 * test. We use a pipe to communicate with the child.
	 */
	int pid;
	int pfd[2];
	struct flock fl;
	char ch;

	if (pipe(pfd) < 0)
		err(1, "pipe");

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_pid = 9999;

	pid = fork();
	if (pid < 0)
		err(1, "fork");

	if (pid == 0) {
		/*
		 * We are the child. We set a write lock and then
		 * write one byte back to the parent to tell it. The
		 * parent will kill us when its done.
		 */
		if (fcntl(fd, F_SETLK, &fl) < 0)
			err(1, "F_SETLK (child)");
		if (write(pfd[1], "a", 1) < 0)
			err(1, "writing to pipe (child)");
		pause();
		exit(0);
	}

	/*
	 * Wait until the child has set its lock and then perform the
	 * test.
	 */
	if (read(pfd[0], &ch, 1) != 1)
		err(1, "reading from pipe (child)");

	if (verbose) printf("10 - trying to set bogus pid or sysid values: ");

	if (fcntl(fd, F_GETLK, &fl) < 0)
		err(1, "F_GETLK");

	safe_kill(pid, SIGTERM);
	safe_waitpid(pid);
	close(pfd[0]);
	close(pfd[1]);

	FAIL(fl.l_pid != pid);

	SUCCEED;
}

/*
 * Test 11 - remote locks
 *
 * XXX temporary interface which will be removed when the kernel lockd
 * is added.
 */
static int
test11(int fd)
{
#ifdef F_SETLK_REMOTE
	struct flock fl;
	int res;

	if (geteuid() != 0)
		return 0;

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_pid = 9999;
	fl.l_sysid = 1001;

	if (verbose) printf("11 - remote locks: ");

	res = fcntl(fd, F_SETLK_REMOTE, &fl);
	FAIL(res != 0);

	fl.l_sysid = 1002;
	res = fcntl(fd, F_SETLK_REMOTE, &fl);
	FAIL(res == 0);
	FAIL(errno != EACCES && errno != EAGAIN);

	res = fcntl(fd, F_GETLK, &fl);
	FAIL(res != 0);
	FAIL(fl.l_pid != 9999);
	FAIL(fl.l_sysid != 1001);

	fl.l_type = F_UNLCK;
	fl.l_sysid = 1001;
	fl.l_start = 0;
	fl.l_len = 0;
	res = fcntl(fd, F_SETLK_REMOTE, &fl);
	FAIL(res != 0);

	fl.l_pid = 1234;
	fl.l_sysid = 1001;
	fl.l_start = 0;
	fl.l_len = 1;
	fl.l_whence = SEEK_SET;
	fl.l_type = F_RDLCK;
	res = fcntl(fd, F_SETLK_REMOTE, &fl);
	FAIL(res != 0);

	fl.l_sysid = 1002;
	res = fcntl(fd, F_SETLK_REMOTE, &fl);
	FAIL(res != 0);

	fl.l_type = F_UNLCKSYS;
	fl.l_sysid = 1001;
	res = fcntl(fd, F_SETLK_REMOTE, &fl);
	FAIL(res != 0);

	fl.l_type = F_WRLCK;
	res = fcntl(fd, F_GETLK, &fl);
	FAIL(res != 0);
	FAIL(fl.l_pid != 1234);
	FAIL(fl.l_sysid != 1002);

	fl.l_type = F_UNLCKSYS;
	fl.l_sysid = 1002;
	res = fcntl(fd, F_SETLK_REMOTE, &fl);
	FAIL(res != 0);

	SUCCEED;
#else
	return 0;
#endif
}

/*
 * Test 12 - F_SETLKW on locked region which is then unlocked
 *
 * If a shared or exclusive lock is blocked by other locks, the
 * process waits until the request can be satisfied.
 */
static int
test12(int fd)
{
	/*
	 * We create a child process to hold the lock which we will
	 * test. We use a pipe to communicate with the child.
	 */
	int pid;
	int pfd[2];
	struct flock fl;
	char ch;
	int res;

	if (pipe(pfd) < 0)
		err(1, "pipe");

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;

	pid = fork();
	if (pid < 0)
		err(1, "fork");

	if (pid == 0) {
		/*
		 * We are the child. We set a write lock and then
		 * write one byte back to the parent to tell it. The
		 * parent will kill us when its done.
		 */
		if (fcntl(fd, F_SETLK, &fl) < 0)
			err(1, "F_SETLK (child)");
		if (write(pfd[1], "a", 1) < 0)
			err(1, "writing to pipe (child)");

		sleep(1);
		exit(0);
	}

	/*
	 * Wait until the child has set its lock and then perform the
	 * test.
	 */
	if (read(pfd[0], &ch, 1) != 1)
		err(1, "reading from pipe (child)");

	/*
	 * fcntl should wait until the alarm and then return -1 with
	 * errno set to EINTR.
	 */
	if (verbose) printf("12 - F_SETLKW on locked region which is then unlocked: ");

	//alarm(1);

	res = fcntl(fd, F_SETLKW, &fl);
	safe_kill(pid, SIGTERM);
	safe_waitpid(pid);
	close(pfd[0]);
	close(pfd[1]);
	FAIL(res != 0);

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = F_UNLCK;
	if (fcntl(fd, F_SETLK, &fl) < 0)
		err(1, "F_UNLCK");

	SUCCEED;
}

/*
 * Test 13 - F_SETLKW on locked region, race with owner
 *
 * If a shared or exclusive lock is blocked by other locks, the
 * process waits until the request can be satisfied.
 */
static int
test13(int fd)
{
	/*
	 * We create a child process to hold the lock which we will
	 * test. We use a pipe to communicate with the child.
	 */
	int i;
	int pid;
	int pfd[2];
	struct flock fl;
	char ch;
	int res;
	struct itimerval itv;

	if (verbose) printf("13 - F_SETLKW on locked region, race with owner: ");
	fflush(stdout);

	for (i = 0; i < 100; i++) {
		if (pipe(pfd) < 0)
			err(1, "pipe");

		fl.l_start = 0;
		fl.l_len = 0;
		fl.l_type = F_WRLCK;
		fl.l_whence = SEEK_SET;

		pid = fork();
		if (pid < 0)
			err(1, "fork");

		if (pid == 0) {
			/*
			 * We are the child. We set a write lock and then
			 * write one byte back to the parent to tell it. The
			 * parent will kill us when its done.
			 */
			if (fcntl(fd, F_SETLK, &fl) < 0)
				err(1, "F_SETLK (child)");
			if (write(pfd[1], "a", 1) < 0)
				err(1, "writing to pipe (child)");

			sleep(1);
			exit(0);
		}

		/*
		 * Wait until the child has set its lock and then perform the
		 * test.
		 */
		while (read(pfd[0], &ch, 1) != 1) {
			if (errno == EINTR)
				continue;
			err(1, "reading from pipe (child)");
		}

		/*
		 * fcntl should wait until the alarm and then return -1 with
		 * errno set to EINTR.
		 */
		itv.it_interval.tv_sec = 0;
		itv.it_interval.tv_usec = 0;
		itv.it_value.tv_sec = 0;
		itv.it_value.tv_usec = 2;
		setitimer(ITIMER_REAL, &itv, NULL);

		res = fcntl(fd, F_SETLKW, &fl);
		safe_kill(pid, SIGTERM);
		safe_waitpid(pid);
		close(pfd[0]);
		close(pfd[1]);
		FAIL(!(res == 0 || (res == -1 && errno == EINTR)));

		fl.l_start = 0;
		fl.l_len = 0;
		fl.l_type = F_UNLCK;
		if (fcntl(fd, F_SETLK, &fl) < 0)
			err(1, "F_UNLCK");
	}
	SUCCEED;
}

/*
 * Test 14 - soak test
 */
static int
test14(int fd)
{
#define CHILD_COUNT 20
	/*
	 * We create a set of child processes and let each one run
	 * through a random sequence of locks and unlocks.
	 */
	int i, j, id;
	int pids[CHILD_COUNT], pid;
	char buf[128];
	char tbuf[128];
	int map[128];
	char outbuf[512];
	struct flock fl;
	struct itimerval itv;
	int status;
	int id_base = 0;

	if (verbose) printf("14 - soak test: ");
	fflush(stdout);

	for (i = 0; i < 128; i++)
		map[i] = F_UNLCK;

	for (i = 0; i < CHILD_COUNT; i++) {

		pid = fork();
		if (pid < 0)
			err(1, "fork");
		if (pid) {
			/*
			 * Parent - record the pid and continue.
			 */
			pids[i] = pid;
			continue;
		}

		/*
		 * Child - do some work and exit.
		 */
		id = id_base + i;
		for (j = 0; j < 50; j++) {
			int start, end, len;
			int set, wrlock;

			do {
				start = arc4random_uniform(128);
				end = arc4random_uniform(128);
			} while (end <= start);

			set = arc4random_uniform(2);
			wrlock = arc4random_uniform(2);

			len = end - start;
			fl.l_start = start;
			fl.l_len = len;
			fl.l_whence = SEEK_SET;
			if (set)
				fl.l_type = wrlock ? F_WRLCK : F_RDLCK;
			else
				fl.l_type = F_UNLCK;

			itv.it_interval.tv_sec = 0;
			itv.it_interval.tv_usec = 0;
			itv.it_value.tv_sec = 0;
			itv.it_value.tv_usec = 3000;
			setitimer(ITIMER_REAL, &itv, NULL);

			if (fcntl(fd, F_SETLKW, &fl) < 0) {
				if (errno == EDEADLK || errno == EINTR) {
					if (verbose) {
						snprintf(outbuf, sizeof(outbuf),
						    "%d[%d]: %s [%d .. %d] %s\n",
						    id, j,
						    set ? (wrlock ? "write lock"
							: "read lock")
						    : "unlock", start, end,
						    errno == EDEADLK
						    ? "deadlock"
						    : "interrupted");
						write(1, outbuf,
						    strlen(outbuf));
					}
					continue;
				} else {
					perror("fcntl");
				}
			}

			itv.it_interval.tv_sec = 0;
			itv.it_interval.tv_usec = 0;
			itv.it_value.tv_sec = 0;
			itv.it_value.tv_usec = 0;
			setitimer(ITIMER_REAL, &itv, NULL);

			if (verbose) {
				snprintf(outbuf, sizeof(outbuf),
				    "%d[%d]: %s [%d .. %d] succeeded\n",
				    id, j,
				    set ? (wrlock ? "write lock" : "read lock")
				    : "unlock", start, end);
				write(1, outbuf, strlen(outbuf));
			}

			if (set) {
				if (wrlock) {
					/*
					 * We got a write lock - write
					 * our ID to each byte that we
					 * managed to claim.
					 */
					for (i = start; i < end; i++)
						map[i] = F_WRLCK;
					memset(&buf[start], id, len);
					if (pwrite(fd, &buf[start], len,
						start) != len) {
						printf("%d: short write\n", id);
						exit(1);
					}
				} else {
					/*
					 * We got a read lock - read
					 * the bytes which we claimed
					 * so that we can check that
					 * they don't change
					 * unexpectedly.
					 */
					for (i = start; i < end; i++)
						map[i] = F_RDLCK;
					if (pread(fd, &buf[start], len,
						start) != len) {
						printf("%d: short read\n", id);
						exit(1);
					}
				}
			} else {
				for (i = start; i < end; i++)
					map[i] = F_UNLCK;
			}

			usleep(1000);

			/*
			 * Read back the whole region so that we can
			 * check that all the bytes we have some kind
			 * of claim to have the correct value.
			 */
			if (pread(fd, tbuf, sizeof(tbuf), 0) != sizeof(tbuf)) {
				printf("%d: short read\n", id);
				exit(1);
			}

			for (i = 0; i < 128; i++) {
				if (map[i] != F_UNLCK && buf[i] != tbuf[i]) {
					snprintf(outbuf, sizeof(outbuf),
					    "%d: byte %d expected %d, "
					    "got %d\n", id, i, buf[i], tbuf[i]);
					write(1, outbuf, strlen(outbuf));
					exit(1);
				}
			}
		}
		if (verbose)
			printf("%d[%d]: done\n", id, j);

		exit(0);
	}

	status = 0;
	for (i = 0; i < CHILD_COUNT; i++) {
		status += safe_waitpid(pids[i]);
	}
	if (status)
		FAIL(status != 0);

	SUCCEED;
}

/*
 * Test 15 - flock(2) semantcs
 *
 * When a lock holder has a shared lock and attempts to upgrade that
 * shared lock to exclusive, it must drop the shared lock before
 * blocking on the exclusive lock.
 *
 * To test this, we first arrange for two shared locks on the file,
 * and then attempt to upgrade one of them to exclusive. This should
 * drop one of the shared locks and block. We interrupt the blocking
 * lock request and examine the lock state of the file after dropping
 * the other shared lock - there should be no active locks at this
 * point.
 */
static int
test15(int fd)
{
#ifdef LOCK_EX
	/*
	 * We create a child process to hold the lock which we will
	 * test. We use a pipe to communicate with the child.
	 *
	 * Since we only have one file descriptors and lock ownership
	 * for flock(2) goes with the file descriptor, we use fcntl to
	 * set the child's shared lock.
	 */
	int pid;
	int pfd[2];
	int fd2;
	struct flock fl;
	char ch;
	int res;

	if (pipe(pfd) < 0)
		err(1, "pipe");

	pid = fork();
	if (pid < 0)
		err(1, "fork");

	if (pid == 0) {
		/*
		 * We are the child. We set a shared lock and then
		 * write one byte back to the parent to tell it. The
		 * parent will kill us when its done.
		 */
		fl.l_start = 0;
		fl.l_len = 0;
		fl.l_type = F_RDLCK;
		fl.l_whence = SEEK_SET;
		if (fcntl(fd, F_SETLK, &fl) < 0)
			err(1, "fcntl(F_SETLK) (child)");
		if (write(pfd[1], "a", 1) < 0)
			err(1, "writing to pipe (child)");
		pause();
		exit(0);
	}

	/*
	 * Wait until the child has set its lock and then perform the
	 * test.
	 */
	if (read(pfd[0], &ch, 1) != 1)
		err(1, "reading from pipe (child)");

	fd2 = dup(fd);
	FAIL(fd2 == -1);
	if (flock(fd, LOCK_SH) < 0)
		err(1, "flock shared");

	/*
	 * flock should wait until the alarm and then return -1 with
	 * errno set to EINTR.
	 */
	if (verbose) printf("15 - flock(2) semantics: ");

	alarm(1);
	flock(fd, LOCK_EX);

	/*
	 * Kill the child to force it to drop its locks.
	 */
	safe_kill(pid, SIGTERM);
	safe_waitpid(pid);

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	res = fcntl(fd, F_GETLK, &fl);

	close(pfd[0]);
	close(pfd[1]);
	FAIL(res != 0);
	FAIL(fl.l_type != F_UNLCK);

	SUCCEED;
#else
	return 0;
#endif
}

/*
 * Test 16 - double free regression
 *
 * Not applicable anymore due to stricter bounds validation.
 */
static int
test16(int fd)
{
#if 0
	struct flock fl;
	int res;

	fl.l_pid = 0;
	fl.l_type = 1;
	fl.l_whence = 0;

	fl.l_start = 0;
	fl.l_len = 0x8000000000000000;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	fl.l_start = 0x10000;
	fl.l_len = 0;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	fl.l_start = 0;
	fl.l_len = 0x8000000000000000;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	fl.l_start = 0x10000;
	fl.l_len = 0;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);
#endif

	SUCCEED;
}

/*
 * Test 17 - lf_findoverlap() case 0
 *
 * No overlap.
 */
static int
test17(int fd)
{
	struct flock fl;
	int nfd, res;

	/* First lock. */
	{
		nfd = dup(fd);
		FAIL(nfd == -1);

		fl.l_start = 0;
		fl.l_len = 100;
		fl.l_pid = 0;
		fl.l_type = F_RDLCK;
		fl.l_whence = 0;
		res = fcntl(fd, F_SETLK, &fl);
		FAIL(res != 0);

		close(nfd);
	}

	/* Insert at end. */
	{
		nfd = dup(fd);
		FAIL(nfd == -1);

		fl.l_start = 100;
		fl.l_len = 100;
		fl.l_pid = 0;
		fl.l_type = F_RDLCK;
		fl.l_whence = 0;
		res = fcntl(fd, F_SETLK, &fl);
		FAIL(res != 0);

		fl.l_start = 200;
		fl.l_len = 100;
		fl.l_pid = 0;
		fl.l_type = F_RDLCK;
		fl.l_whence = 0;
		res = fcntl(fd, F_SETLK, &fl);
		FAIL(res != 0);

		close(nfd);
	}

	/* Insert before overlap. */
	{
		nfd = dup(fd);
		FAIL(nfd == -1);

		fl.l_start = 300;
		fl.l_len = 100;
		fl.l_pid = 0;
		fl.l_type = F_RDLCK;
		fl.l_whence = 0;
		res = fcntl(fd, F_SETLK, &fl);
		FAIL(res != 0);

		fl.l_start = 500;
		fl.l_len = 100;
		fl.l_pid = 0;
		fl.l_type = F_RDLCK;
		fl.l_whence = 0;
		res = fcntl(fd, F_SETLK, &fl);
		FAIL(res != 0);

		fl.l_start = 400;
		fl.l_len = 100;
		fl.l_pid = 0;
		fl.l_type = F_RDLCK;
		fl.l_whence = 0;
		res = fcntl(fd, F_SETLK, &fl);
		FAIL(res != 0);

		close(nfd);
	}

	SUCCEED;
}

/*
 * Test 18 - lf_findoverlap() case 1
 *
 * Overlap and lock are equal.
 */
static int
test18(int fd)
{
	struct flock fl;
	int res;

	fl.l_start = 0;
	fl.l_len = 100;
	fl.l_pid = 0;
	fl.l_type = F_RDLCK;
	fl.l_whence = 0;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	SUCCEED;
}

/*
 * Test 19 - lf_findoverlap() case 2
 *
 * Overlap contains lock.
 */
static int
test19(int fd)
{
	struct flock fl;
	int nfd, res;

	/* Same type. */
	{
		nfd = dup(fd);
		FAIL(nfd == -1);

		fl.l_start = 0;
		fl.l_len = 100;
		fl.l_pid = 0;
		fl.l_type = F_RDLCK;
		fl.l_whence = 0;
		res = fcntl(fd, F_SETLK, &fl);
		FAIL(res != 0);

		fl.l_start = 0;
		fl.l_len = 50;
		fl.l_pid = 0;
		fl.l_type = F_RDLCK;
		fl.l_whence = 0;
		res = fcntl(fd, F_SETLK, &fl);
		FAIL(res != 0);

		close(nfd);
	}

	/* Different type, same start offset. */
	{
		nfd = dup(fd);
		FAIL(nfd == -1);

		fl.l_start = 100;
		fl.l_len = 100;
		fl.l_pid = 0;
		fl.l_type = F_RDLCK;
		fl.l_whence = 0;
		res = fcntl(fd, F_SETLK, &fl);
		FAIL(res != 0);

		fl.l_start = 100;
		fl.l_len = 50;
		fl.l_pid = 0;
		fl.l_type = F_WRLCK;
		fl.l_whence = 0;
		res = fcntl(fd, F_SETLK, &fl);
		FAIL(res != 0);

		close(nfd);
	}

	/* Split fallback. */
	{
		nfd = dup(fd);
		FAIL(nfd == -1);

		fl.l_start = 100;
		fl.l_len = 100;
		fl.l_pid = 0;
		fl.l_type = F_RDLCK;
		fl.l_whence = 0;
		res = fcntl(fd, F_SETLK, &fl);
		FAIL(res != 0);

		fl.l_start = 110;
		fl.l_len = 50;
		fl.l_pid = 0;
		fl.l_type = F_WRLCK;
		fl.l_whence = 0;
		res = fcntl(fd, F_SETLK, &fl);
		FAIL(res != 0);

		close(nfd);
	}

	SUCCEED;
}

/*
 * Test 20 - lf_findoverlap() case 3
 *
 * Lock contains overlap.
 */
static int
test20(int fd)
{
	struct flock fl;
	int res;

	fl.l_start = 0;
	fl.l_len = 100;
	fl.l_pid = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = 0;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	fl.l_start = 0;
	fl.l_len = 200;
	fl.l_pid = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = 0;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	SUCCEED;
}

/*
 * Test 21 - lf_findoverlap() case 4
 *
 * Overlap starts before lock.
 */
static int
test21(int fd)
{
	struct flock fl;
	int res;

	fl.l_start = 0;
	fl.l_len = 100;
	fl.l_pid = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = 0;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	fl.l_start = 50;
	fl.l_len = 100;
	fl.l_pid = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = 0;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	SUCCEED;
}

/*
 * Test 22 - lf_findoverlap() case 5
 *
 * Overlap ends after lock.
 */
static int
test22(int fd)
{
	struct flock fl;
	int res;

	fl.l_start = 10;
	fl.l_len = 100;
	fl.l_pid = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = 0;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	fl.l_start = 0;
	fl.l_len = 50;
	fl.l_pid = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = 0;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	SUCCEED;
}

/*
 * Test 23 - positive length overflow
 */
static int
test23(int fd)
{
	struct flock fl;
	int res;

	fl.l_pid = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 2;
	fl.l_len = LLONG_MAX;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != -1);
	FAIL(errno != EOVERFLOW);

	SUCCEED;
}

/*
 * Test 24 - negative length
 */
static int
test24(int fd)
{
	struct flock fl;
	pid_t pid;
	int res, status;

	fl.l_pid = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;

	/* Start offset plus length must be positive. */
	fl.l_start = 0;
	fl.l_len = LLONG_MIN;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != -1);
	FAIL(errno != EINVAL);

	/* Set exclusive lock on range [2,3] */
	fl.l_start = 4;
	fl.l_len = -2;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	/* Another process must not be able to lock the same range. */
	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		fl.l_start = 2;
		fl.l_len = 2;
		res = fcntl(fd, F_GETLK, &fl);
		FAIL(res != 0);
		FAIL(fl.l_type == F_UNLCK);
		_exit(0);
	}
	status = safe_waitpid(pid);
	FAIL(status != 0);

	SUCCEED;
}

/*
 * Test 25 - use after free regression
 *
 * Discovered by syzkaller.
 */
static int
test25(int fd)
{
	struct flock fl;
	int master, res, slave;

	res = openpty(&master, &slave, NULL, NULL, NULL);
	FAIL(res == -1);
	close(master);

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_pid = 0;
	fl.l_type = F_RDLCK;
	fl.l_whence = SEEK_SET;
	res = fcntl(slave, F_SETLKW, &fl);
	FAIL(res != 0);

	fl.l_start = 3;
	fl.l_len = 0x7ffffffffffffffd;
	fl.l_pid = 0;
	fl.l_type = F_UNLCK;
	fl.l_whence = SEEK_END;
	res = fcntl(slave, F_SETLKW, &fl);
	FAIL(res != 0);

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_pid = 0;
	fl.l_type = F_RDLCK;
	fl.l_whence = SEEK_SET;
	res = fcntl(slave, F_SETLKW, &fl);
	FAIL(res != 0);

	close(slave);

	SUCCEED;
}

/*
 * Test 26 - range end point ambiguity regression
 *
 * When a new lock range [start, LLONG_MAX] overlapped with an existing range,
 * kernel's lock data structure could become inconsistent.
 */
static int
test26(int fd)
{
	struct flock fl;
	pid_t pid;
	int res, status;

	/* Lock the whole range. */
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_pid = 0;
	fl.l_type = F_RDLCK;
	fl.l_whence = SEEK_SET;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	/*
	 * Split the range at the end.
	 * This should be handled as if l_len == 0.
	 */
	fl.l_start = LLONG_MAX;
	fl.l_len = 1;
	fl.l_pid = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	/* Check the resulting ranges. */
	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		fl.l_start = 0;
		fl.l_len = 0;
		fl.l_type = F_WRLCK;
		fl.l_whence = SEEK_SET;
		res = fcntl(fd, F_GETLK, &fl);
		FAIL(res != 0);
		FAIL(fl.l_type != F_RDLCK);
		FAIL(fl.l_start != 0);
		FAIL(fl.l_len != LLONG_MAX);

		fl.l_start = LLONG_MAX;
		fl.l_len = 0;
		fl.l_type = F_WRLCK;
		fl.l_whence = SEEK_SET;
		res = fcntl(fd, F_GETLK, &fl);
		FAIL(res != 0);
		FAIL(fl.l_type != F_WRLCK);
		FAIL(fl.l_start != LLONG_MAX);
		FAIL(fl.l_len != 0);

		_exit(0);
	}
	status = safe_waitpid(pid);
	FAIL(status != 0);

	/* Release all locks. */
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_pid = 0;
	fl.l_type = F_UNLCK;
	fl.l_whence = SEEK_SET;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	/* Check the resulting ranges. */
	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		fl.l_start = 0;
		fl.l_len = 0;
		fl.l_type = F_WRLCK;
		fl.l_whence = SEEK_SET;
		res = fcntl(fd, F_GETLK, &fl);
		FAIL(res != 0);
		FAIL(fl.l_type != F_UNLCK);
		FAIL(fl.l_start != 0);
		FAIL(fl.l_len != 0);

		_exit(0);
	}
	status = safe_waitpid(pid);
	FAIL(status != 0);

	SUCCEED;
}

static struct test tests[] = {
	{	test1,		0	},
	{	test2,		0	},
	{	test3,		1	},
	{	test4,		0	},
	{	test5,		1	},
	{	test6,		1	},
	{	test7,		0	},
	{	test8,		0	},
	{	test9,		0	},
	{	test10,		0	},
	{	test11,		1	},
	{	test12,		0	},
	{	test13,		1	},
	{	test14,		0	},
	{	test15,		1	},
	{	test16,		0	},
	{	test17,		0	},
	{	test18,		0	},
	{	test19,		0	},
	{	test20,		0	},
	{	test21,		0	},
	{	test22,		0	},
	{	test23,		0	},
	{	test24,		0	},
	{	test25,		0	},
	{	test26,		0	},
};

static int test_count = sizeof(tests) / sizeof(tests[0]);

int
main(int argc, char *argv[])
{
	struct sigaction sa;
	const char *errstr;
	int c, fd, i;
	int error = 0;
	int testnum = 0;

	while ((c = getopt(argc, argv, "v")) != -1)
		switch (c) {
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (argc > 1)
		usage();
	if (argc == 1) {
		testnum = strtonum(argv[0], 1, test_count, &errstr);
		if (testnum == 0)
			errx(1, "test number %s", errstr);
	}

	fd = make_file(1024);

	sa.sa_handler = ignore_alarm;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGALRM, &sa, 0);

	for (i = 0; i < test_count; i++) {
		if (testnum == 0 || testnum == i + 1)
			error |= tests[i].testfn(fd);
	}

	return (error ? 1 : 0);
}
