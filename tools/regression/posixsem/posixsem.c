/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/_semaphore.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "test.h"

#define	TEST_PATH	"/tmp/posixsem_regression_test"

#define	ELAPSED(elapsed, limit)		(abs((elapsed) - (limit)) < 100)

/* Macros for passing child status to parent over a pipe. */
#define	CSTAT(class, error)		((class) << 16 | (error))
#define	CSTAT_CLASS(stat)		((stat) >> 16)
#define	CSTAT_ERROR(stat)		((stat) & 0xffff)

/*
 * Helper routine for tests that use a child process.  This routine
 * creates a pipe and forks a child process.  The child process runs
 * the 'func' routine which returns a status integer.  The status
 * integer gets written over the pipe to the parent and returned in
 * '*stat'.  If there is an error in pipe(), fork(), or wait() this
 * returns -1 and fails the test.
 */
static int
child_worker(int (*func)(void *arg), void *arg, int *stat)
{
	pid_t pid;
	int pfd[2], cstat;

	if (pipe(pfd) < 0) {
		fail_errno("pipe");
		return (-1);
	}

	pid = fork();
	switch (pid) {
	case -1:
		/* Error. */
		fail_errno("fork");
		close(pfd[0]);
		close(pfd[1]);
		return (-1);
	case 0:
		/* Child. */
		cstat = func(arg);
		write(pfd[1], &cstat, sizeof(cstat));
		exit(0);
	}

	if (read(pfd[0], stat, sizeof(*stat)) < 0) {
		fail_errno("read(pipe)");
		close(pfd[0]);
		close(pfd[1]);
		return (-1);
	}
	if (waitpid(pid, NULL, 0) < 0) {
		fail_errno("wait");
		close(pfd[0]);
		close(pfd[1]);
		return (-1);
	}
	close(pfd[0]);
	close(pfd[1]);
	return (0);
}

/*
 * Attempt a ksem_open() that should fail with an expected error of
 * 'error'.
 */
static void
ksem_open_should_fail(const char *path, int flags, mode_t mode, unsigned int
    value, int error)
{
	semid_t id;

	if (ksem_open(&id, path, flags, mode, value) >= 0) {
		fail_err("ksem_open() didn't fail");
		ksem_close(id);
		return;
	}
	if (errno != error) {
		fail_errno("ksem_open");
		return;
	}
	pass();
}

/*
 * Attempt a ksem_unlink() that should fail with an expected error of
 * 'error'.
 */
static void
ksem_unlink_should_fail(const char *path, int error)
{

	if (ksem_unlink(path) >= 0) {
		fail_err("ksem_unlink() didn't fail");
		return;
	}
	if (errno != error) {
		fail_errno("ksem_unlink");
		return;
	}
	pass();
}

/*
 * Attempt a ksem_close() that should fail with an expected error of
 * 'error'.
 */
static void
ksem_close_should_fail(semid_t id, int error)
{

	if (ksem_close(id) >= 0) {
		fail_err("ksem_close() didn't fail");
		return;
	}
	if (errno != error) {
		fail_errno("ksem_close");
		return;
	}
	pass();
}

/*
 * Attempt a ksem_init() that should fail with an expected error of
 * 'error'.
 */
static void
ksem_init_should_fail(unsigned int value, int error)
{
	semid_t id;

	if (ksem_init(&id, value) >= 0) {
		fail_err("ksem_init() didn't fail");
		ksem_destroy(id);
		return;
	}
	if (errno != error) {
		fail_errno("ksem_init");
		return;
	}
	pass();
}

/*
 * Attempt a ksem_destroy() that should fail with an expected error of
 * 'error'.
 */
static void
ksem_destroy_should_fail(semid_t id, int error)
{

	if (ksem_destroy(id) >= 0) {
		fail_err("ksem_destroy() didn't fail");
		return;
	}
	if (errno != error) {
		fail_errno("ksem_destroy");
		return;
	}
	pass();
}

/*
 * Attempt a ksem_post() that should fail with an expected error of
 * 'error'.
 */
static void
ksem_post_should_fail(semid_t id, int error)
{

	if (ksem_post(id) >= 0) {
		fail_err("ksem_post() didn't fail");
		return;
	}
	if (errno != error) {
		fail_errno("ksem_post");
		return;
	}
	pass();
}

static void
open_after_unlink(void)
{
	semid_t id;

	if (ksem_open(&id, TEST_PATH, O_CREAT, 0777, 1) < 0) {
		fail_errno("ksem_open(1)");
		return;
	}
	ksem_close(id);

	if (ksem_unlink(TEST_PATH) < 0) {
		fail_errno("ksem_unlink");
		return;
	}

	ksem_open_should_fail(TEST_PATH, O_RDONLY, 0777, 1, ENOENT);
}
TEST(open_after_unlink, "open after unlink");

static void
open_invalid_path(void)
{

	ksem_open_should_fail("blah", 0, 0777, 1, EINVAL);
}
TEST(open_invalid_path, "open invalid path");

static void
open_extra_flags(void)
{

	ksem_open_should_fail(TEST_PATH, O_RDONLY | O_DIRECT, 0777, 1, EINVAL);
}
TEST(open_extra_flags, "open with extra flags");

static void
open_bad_value(void)
{

	(void)ksem_unlink(TEST_PATH);

	ksem_open_should_fail(TEST_PATH, O_CREAT, 0777, UINT_MAX, EINVAL);
}
TEST(open_bad_value, "open with invalid initial value");

static void
open_bad_path_pointer(void)
{

	ksem_open_should_fail((char *)1024, O_RDONLY, 0777, 1, EFAULT);
}
TEST(open_bad_path_pointer, "open bad path pointer");

static void
open_path_too_long(void)
{
	char *page;

	page = malloc(MAXPATHLEN + 1);
	memset(page, 'a', MAXPATHLEN);
	page[MAXPATHLEN] = '\0';
	ksem_open_should_fail(page, O_RDONLY, 0777, 1, ENAMETOOLONG);
	free(page);
}
TEST(open_path_too_long, "open pathname too long");

static void
open_nonexisting_semaphore(void)
{

	ksem_open_should_fail("/notreallythere", 0, 0777, 1, ENOENT);
}
TEST(open_nonexisting_semaphore, "open nonexistent semaphore");

static void
exclusive_create_existing_semaphore(void)
{
	semid_t id;

	if (ksem_open(&id, TEST_PATH, O_CREAT, 0777, 1) < 0) {
		fail_errno("ksem_open(O_CREAT)");
		return;
	}
	ksem_close(id);

	ksem_open_should_fail(TEST_PATH, O_CREAT | O_EXCL, 0777, 1, EEXIST);

	ksem_unlink(TEST_PATH);
}
TEST(exclusive_create_existing_semaphore, "O_EXCL of existing semaphore");

static void
init_bad_value(void)
{

	ksem_init_should_fail(UINT_MAX, EINVAL);
}
TEST(init_bad_value, "init with invalid initial value");

static void
unlink_bad_path_pointer(void)
{

	ksem_unlink_should_fail((char *)1024, EFAULT);
}
TEST(unlink_bad_path_pointer, "unlink bad path pointer");

static void
unlink_path_too_long(void)
{
	char *page;

	page = malloc(MAXPATHLEN + 1);
	memset(page, 'a', MAXPATHLEN);
	page[MAXPATHLEN] = '\0';
	ksem_unlink_should_fail(page, ENAMETOOLONG);
	free(page);
}
TEST(unlink_path_too_long, "unlink pathname too long");

static void
destroy_named_semaphore(void)
{
	semid_t id;

	if (ksem_open(&id, TEST_PATH, O_CREAT, 0777, 1) < 0) {
		fail_errno("ksem_open(O_CREAT)");
		return;
	}

	ksem_destroy_should_fail(id, EINVAL);

	ksem_close(id);
	ksem_unlink(TEST_PATH);
}
TEST(destroy_named_semaphore, "destroy named semaphore");

static void
close_unnamed_semaphore(void)
{
	semid_t id;

	if (ksem_init(&id, 1) < 0) {
		fail_errno("ksem_init");
		return;
	}

	ksem_close_should_fail(id, EINVAL);

	ksem_destroy(id);
}
TEST(close_unnamed_semaphore, "close unnamed semaphore");

static void
destroy_invalid_fd(void)
{

	ksem_destroy_should_fail(STDERR_FILENO, EINVAL);
}
TEST(destroy_invalid_fd, "destroy non-semaphore file descriptor");

static void
close_invalid_fd(void)
{

	ksem_close_should_fail(STDERR_FILENO, EINVAL);
}
TEST(close_invalid_fd, "close non-semaphore file descriptor");

static void
create_unnamed_semaphore(void)
{
	semid_t id;

	if (ksem_init(&id, 1) < 0) {
		fail_errno("ksem_init");
		return;
	}

	if (ksem_destroy(id) < 0) {
		fail_errno("ksem_destroy");
		return;
	}
	pass();
}
TEST(create_unnamed_semaphore, "create unnamed semaphore");

static void
open_named_semaphore(void)
{
	semid_t id;

	if (ksem_open(&id, TEST_PATH, O_CREAT, 0777, 1) < 0) {
		fail_errno("ksem_open(O_CREAT)");
		return;
	}

	if (ksem_close(id) < 0) {
		fail_errno("ksem_close");
		return;
	}

	if (ksem_unlink(TEST_PATH) < 0) {
		fail_errno("ksem_unlink");
		return;
	}
	pass();
}
TEST(open_named_semaphore, "create named semaphore");

static void
getvalue_invalid_semaphore(void)
{
	int val;

	if (ksem_getvalue(STDERR_FILENO, &val) >= 0) {
		fail_err("ksem_getvalue() didn't fail");
		return;
	}
	if (errno != EINVAL) {
		fail_errno("ksem_getvalue");
		return;
	}
	pass();
}
TEST(getvalue_invalid_semaphore, "get value of invalid semaphore");

static void
post_invalid_semaphore(void)
{

	ksem_post_should_fail(STDERR_FILENO, EINVAL);
}
TEST(post_invalid_semaphore, "post of invalid semaphore");

static void
wait_invalid_semaphore(void)
{

	if (ksem_wait(STDERR_FILENO) >= 0) {
		fail_err("ksem_wait() didn't fail");
		return;
	}
	if (errno != EINVAL) {
		fail_errno("ksem_wait");
		return;
	}
	pass();
}
TEST(wait_invalid_semaphore, "wait for invalid semaphore");

static void
trywait_invalid_semaphore(void)
{

	if (ksem_trywait(STDERR_FILENO) >= 0) {
		fail_err("ksem_trywait() didn't fail");
		return;
	}
	if (errno != EINVAL) {
		fail_errno("ksem_trywait");
		return;
	}
	pass();
}
TEST(trywait_invalid_semaphore, "try wait for invalid semaphore");

static void
timedwait_invalid_semaphore(void)
{

	if (ksem_timedwait(STDERR_FILENO, NULL) >= 0) {
		fail_err("ksem_timedwait() didn't fail");
		return;
	}
	if (errno != EINVAL) {
		fail_errno("ksem_timedwait");
		return;
	}
	pass();
}
TEST(timedwait_invalid_semaphore, "timed wait for invalid semaphore");

static int
checkvalue(semid_t id, int expected)
{
	int val;

	if (ksem_getvalue(id, &val) < 0) {
		fail_errno("ksem_getvalue");
		return (-1);
	}
	if (val != expected) {
		fail_err("sem value should be %d instead of %d", expected, val);
		return (-1);
	}
	return (0);
}

static void
post_test(void)
{
	semid_t id;

	if (ksem_init(&id, 1) < 0) {
		fail_errno("ksem_init");
		return;
	}
	if (checkvalue(id, 1) < 0) {
		ksem_destroy(id);
		return;
	}
	if (ksem_post(id) < 0) {
		fail_errno("ksem_post");
		ksem_destroy(id);
		return;
	}
	if (checkvalue(id, 2) < 0) {
		ksem_destroy(id);
		return;
	}
	if (ksem_destroy(id) < 0) {
		fail_errno("ksem_destroy");
		return;
	}
	pass();
}
TEST(post_test, "simple post");

static void
use_after_unlink_test(void)
{
	semid_t id;

	/*
	 * Create named semaphore with value of 1 and then unlink it
	 * while still retaining the initial reference.
	 */
	if (ksem_open(&id, TEST_PATH, O_CREAT | O_EXCL, 0777, 1) < 0) {
		fail_errno("ksem_open(O_CREAT | O_EXCL)");
		return;
	}
	if (ksem_unlink(TEST_PATH) < 0) {
		fail_errno("ksem_unlink");
		ksem_close(id);
		return;
	}
	if (checkvalue(id, 1) < 0) {
		ksem_close(id);
		return;
	}

	/* Post the semaphore to set its value to 2. */
	if (ksem_post(id) < 0) {
		fail_errno("ksem_post");
		ksem_close(id);
		return;
	}
	if (checkvalue(id, 2) < 0) {
		ksem_close(id);
		return;
	}

	/* Wait on the semaphore which should set its value to 1. */
	if (ksem_wait(id) < 0) {
		fail_errno("ksem_wait");
		ksem_close(id);
		return;
	}
	if (checkvalue(id, 1) < 0) {
		ksem_close(id);
		return;
	}

	if (ksem_close(id) < 0) {
		fail_errno("ksem_close");
		return;
	}
	pass();
}
TEST(use_after_unlink_test, "use named semaphore after unlink");

static void
unlocked_trywait(void)
{
	semid_t id;

	if (ksem_init(&id, 1) < 0) {
		fail_errno("ksem_init");
		return;
	}

	/* This should succeed and decrement the value to 0. */
	if (ksem_trywait(id) < 0) {
		fail_errno("ksem_trywait()");
		ksem_destroy(id);
		return;
	}
	if (checkvalue(id, 0) < 0) {
		ksem_destroy(id);
		return;
	}

	if (ksem_destroy(id) < 0) {
		fail_errno("ksem_destroy");
		return;
	}
	pass();
}
TEST(unlocked_trywait, "unlocked trywait");

static void
locked_trywait(void)
{
	semid_t id;

	if (ksem_init(&id, 0) < 0) {
		fail_errno("ksem_init");
		return;
	}

	/* This should fail with EAGAIN and leave the value at 0. */
	if (ksem_trywait(id) >= 0) {
		fail_err("ksem_trywait() didn't fail");
		ksem_destroy(id);
		return;
	}
	if (errno != EAGAIN) {
		fail_errno("wrong error from ksem_trywait()");
		ksem_destroy(id);
		return;
	}
	if (checkvalue(id, 0) < 0) {
		ksem_destroy(id);
		return;
	}

	if (ksem_destroy(id) < 0) {
		fail_errno("ksem_destroy");
		return;
	}
	pass();
}
TEST(locked_trywait, "locked trywait");

/*
 * Use a timer to post a specific semaphore after a timeout.  A timer
 * is scheduled via schedule_post().  check_alarm() must be called
 * afterwards to clean up and check for errors.
 */
static semid_t alarm_id = -1;
static int alarm_errno;
static int alarm_handler_installed;

static void
alarm_handler(int signo)
{

	if (ksem_post(alarm_id) < 0)
		alarm_errno = errno;
}

static int
check_alarm(int just_clear)
{
	struct itimerval it;

	bzero(&it, sizeof(it));
	if (just_clear) {
		setitimer(ITIMER_REAL, &it, NULL);
		alarm_errno = 0;
		alarm_id = -1;
		return (0);
	}
	if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
		fail_errno("setitimer");
		return (-1);
	}
	if (alarm_errno != 0 && !just_clear) {
		errno = alarm_errno;
		fail_errno("ksem_post() (via timeout)");
		alarm_errno = 0;
		return (-1);
	}
	alarm_id = -1;
	
	return (0);
}

static int
schedule_post(semid_t id, u_int msec)
{
	struct itimerval it;

	if (!alarm_handler_installed) {
		if (signal(SIGALRM, alarm_handler) == SIG_ERR) {
			fail_errno("signal(SIGALRM)");
			return (-1);
		}
		alarm_handler_installed = 1;
	}
	if (alarm_id != -1) {
		fail_err("ksem_post() already scheduled");
		return (-1);
	}
	alarm_id = id;
	bzero(&it, sizeof(it));
	it.it_value.tv_sec = msec / 1000;
	it.it_value.tv_usec = (msec % 1000) * 1000;
	if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
		fail_errno("setitimer");
		return (-1);
	}
	return (0);
}

static int
timedwait(semid_t id, u_int msec, u_int *delta, int error)
{
	struct timespec start, end;

	if (clock_gettime(CLOCK_REALTIME, &start) < 0) {
		fail_errno("clock_gettime(CLOCK_REALTIME)");
		return (-1);
	}
	end.tv_sec = msec / 1000;
	end.tv_nsec = msec % 1000 * 1000000;
	timespecadd(&end, &start, &end);
	if (ksem_timedwait(id, &end) < 0) {
		if (errno != error) {
			fail_errno("ksem_timedwait");
			return (-1);
		}
	} else if (error != 0) {
		fail_err("ksem_timedwait() didn't fail");
		return (-1);
	}
	if (clock_gettime(CLOCK_REALTIME, &end) < 0) {
		fail_errno("clock_gettime(CLOCK_REALTIME)");
		return (-1);
	}
	timespecsub(&end, &start, &end);
	*delta = end.tv_nsec / 1000000;
	*delta += end.tv_sec * 1000;
	return (0);
}

static void
unlocked_timedwait(void)
{
	semid_t id;
	u_int elapsed;

	if (ksem_init(&id, 1) < 0) {
		fail_errno("ksem_init");
		return;
	}

	/* This should succeed right away and set the value to 0. */
	if (timedwait(id, 5000, &elapsed, 0) < 0) {
		ksem_destroy(id);
		return;
	}
	if (!ELAPSED(elapsed, 0)) {
		fail_err("ksem_timedwait() of unlocked sem took %ums", elapsed);
		ksem_destroy(id);
		return;
	}
	if (checkvalue(id, 0) < 0) {
		ksem_destroy(id);
		return;
	}

	if (ksem_destroy(id) < 0) {
		fail_errno("ksem_destroy");
		return;
	}
	pass();
}
TEST(unlocked_timedwait, "unlocked timedwait");

static void
expired_timedwait(void)
{
	semid_t id;
	u_int elapsed;

	if (ksem_init(&id, 0) < 0) {
		fail_errno("ksem_init");
		return;
	}

	/* This should fail with a timeout and leave the value at 0. */
	if (timedwait(id, 2500, &elapsed, ETIMEDOUT) < 0) {
		ksem_destroy(id);
		return;
	}
	if (!ELAPSED(elapsed, 2500)) {
		fail_err(
	    "ksem_timedwait() of locked sem took %ums instead of 2500ms",
		    elapsed);
		ksem_destroy(id);
		return;
	}
	if (checkvalue(id, 0) < 0) {
		ksem_destroy(id);
		return;
	}

	if (ksem_destroy(id) < 0) {
		fail_errno("ksem_destroy");
		return;
	}
	pass();
}
TEST(expired_timedwait, "locked timedwait timeout");

static void
locked_timedwait(void)
{
	semid_t id;
	u_int elapsed;

	if (ksem_init(&id, 0) < 0) {
		fail_errno("ksem_init");
		return;
	}

	/*
	 * Schedule a post to trigger after 1000 ms.  The subsequent
	 * timedwait should succeed after 1000 ms as a result w/o
	 * timing out.
	 */
	if (schedule_post(id, 1000) < 0) {
		ksem_destroy(id);
		return;
	}
	if (timedwait(id, 2000, &elapsed, 0) < 0) {
		check_alarm(1);
		ksem_destroy(id);
		return;
	}
	if (!ELAPSED(elapsed, 1000)) {
		fail_err(
	    "ksem_timedwait() with delayed post took %ums instead of 1000ms",
		    elapsed);
		check_alarm(1);
		ksem_destroy(id);
		return;
	}
	if (check_alarm(0) < 0) {
		ksem_destroy(id);
		return;
	}

	if (ksem_destroy(id) < 0) {
		fail_errno("ksem_destroy");
		return;
	}
	pass();
}
TEST(locked_timedwait, "locked timedwait");

static int
testwait(semid_t id, u_int *delta)
{
	struct timespec start, end;

	if (clock_gettime(CLOCK_REALTIME, &start) < 0) {
		fail_errno("clock_gettime(CLOCK_REALTIME)");
		return (-1);
	}
	if (ksem_wait(id) < 0) {
		fail_errno("ksem_wait");
		return (-1);
	}
	if (clock_gettime(CLOCK_REALTIME, &end) < 0) {
		fail_errno("clock_gettime(CLOCK_REALTIME)");
		return (-1);
	}
	timespecsub(&end, &start, &end);
	*delta = end.tv_nsec / 1000000;
	*delta += end.tv_sec * 1000;
	return (0);
}

static void
unlocked_wait(void)
{
	semid_t id;
	u_int elapsed;

	if (ksem_init(&id, 1) < 0) {
		fail_errno("ksem_init");
		return;
	}

	/* This should succeed right away and set the value to 0. */
	if (testwait(id, &elapsed) < 0) {
		ksem_destroy(id);
		return;
	}
	if (!ELAPSED(elapsed, 0)) {
		fail_err("ksem_wait() of unlocked sem took %ums", elapsed);
		ksem_destroy(id);
		return;
	}
	if (checkvalue(id, 0) < 0) {
		ksem_destroy(id);
		return;
	}

	if (ksem_destroy(id) < 0) {
		fail_errno("ksem_destroy");
		return;
	}
	pass();
}
TEST(unlocked_wait, "unlocked wait");

static void
locked_wait(void)
{
	semid_t id;
	u_int elapsed;

	if (ksem_init(&id, 0) < 0) {
		fail_errno("ksem_init");
		return;
	}

	/*
	 * Schedule a post to trigger after 1000 ms.  The subsequent
	 * wait should succeed after 1000 ms as a result.
	 */
	if (schedule_post(id, 1000) < 0) {
		ksem_destroy(id);
		return;
	}
	if (testwait(id, &elapsed) < 0) {
		check_alarm(1);
		ksem_destroy(id);
		return;
	}
	if (!ELAPSED(elapsed, 1000)) {
		fail_err(
	    "ksem_wait() with delayed post took %ums instead of 1000ms",
		    elapsed);
		check_alarm(1);
		ksem_destroy(id);
		return;
	}
	if (check_alarm(0) < 0) {
		ksem_destroy(id);
		return;
	}

	if (ksem_destroy(id) < 0) {
		fail_errno("ksem_destroy");
		return;
	}
	pass();
}
TEST(locked_wait, "locked wait");

/*
 * Fork off a child process.  The child will open the semaphore via
 * the same name.  The child will then block on the semaphore waiting
 * for the parent to post it.
 */
static int
wait_twoproc_child(void *arg)
{
	semid_t id;

	if (ksem_open(&id, TEST_PATH, 0, 0, 0) < 0)
		return (CSTAT(1, errno));
	if (ksem_wait(id) < 0)
		return (CSTAT(2, errno));
	if (ksem_close(id) < 0)
		return (CSTAT(3, errno));
	return (CSTAT(0, 0));
}

static void
wait_twoproc_test(void)
{
	semid_t id;
	int stat;

	if (ksem_open(&id, TEST_PATH, O_CREAT, 0777, 0)) {
		fail_errno("ksem_open");
		return;
	}

	if (schedule_post(id, 500) < 0) {
		ksem_close(id);
		ksem_unlink(TEST_PATH);
		return;
	}		

	if (child_worker(wait_twoproc_child, NULL, &stat) < 0) {
		check_alarm(1);
		ksem_close(id);
		ksem_unlink(TEST_PATH);
		return;
	}

	errno = CSTAT_ERROR(stat);
	switch (CSTAT_CLASS(stat)) {
	case 0:
		pass();
		break;
	case 1:
		fail_errno("child ksem_open()");
		break;
	case 2:
		fail_errno("child ksem_wait()");
		break;
	case 3:
		fail_errno("child ksem_close()");
		break;
	default:
		fail_err("bad child state %#x", stat);
		break;
	}

	check_alarm(1);
	ksem_close(id);
	ksem_unlink(TEST_PATH);
}
TEST(wait_twoproc_test, "two proc wait");

static void
maxvalue_test(void)
{
	semid_t id;
	int val;

	if (ksem_init(&id, SEM_VALUE_MAX) < 0) {
		fail_errno("ksem_init");
		return;
	}
	if (ksem_getvalue(id, &val) < 0) {
		fail_errno("ksem_getvalue");
		ksem_destroy(id);
		return;
	}
	if (val != SEM_VALUE_MAX) {
		fail_err("value %d != SEM_VALUE_MAX");
		ksem_destroy(id);
		return;
	}
	if (val < 0) {
		fail_err("value < 0");
		ksem_destroy(id);
		return;
	}
	if (ksem_destroy(id) < 0) {
		fail_errno("ksem_destroy");
		return;
	}
	pass();
}
TEST(maxvalue_test, "get value of SEM_VALUE_MAX semaphore");

static void
maxvalue_post_test(void)
{
	semid_t id;

	if (ksem_init(&id, SEM_VALUE_MAX) < 0) {
		fail_errno("ksem_init");
		return;
	}

	ksem_post_should_fail(id, EOVERFLOW);

	ksem_destroy(id);
}
TEST(maxvalue_post_test, "post SEM_VALUE_MAX semaphore");

static void
busy_destroy_test(void)
{
	char errbuf[_POSIX2_LINE_MAX];
	struct kinfo_proc *kp;
	semid_t id;
	pid_t pid;
	kvm_t *kd;
	int count;

	kd = kvm_openfiles(NULL, "/dev/null", NULL, O_RDONLY, errbuf);
	if (kd == NULL) {
		fail_err("kvm_openfiles: %s", errbuf);
		return;
	}

	if (ksem_init(&id, 0) < 0) {
		fail_errno("ksem_init");
		kvm_close(kd);
		return;
	}

	pid = fork();
	switch (pid) {
	case -1:
		/* Error. */
		fail_errno("fork");
		ksem_destroy(id);
		kvm_close(kd);
		return;
	case 0:
		/* Child. */
		ksem_wait(id);
		exit(0);
	}

	/*
	 * Wait for the child process to block on the semaphore.  This
	 * is a bit gross.
	 */
	for (;;) {
		kp = kvm_getprocs(kd, KERN_PROC_PID, pid, &count);
		if (kp == NULL) {
			fail_err("kvm_getprocs: %s", kvm_geterr(kd));
			kvm_close(kd);
			ksem_destroy(id);
			return;
		}
		if (kp->ki_stat == SSLEEP &&
		    (strcmp(kp->ki_wmesg, "sem") == 0 ||
		    strcmp(kp->ki_wmesg, "ksem") == 0))
			break;
		usleep(1000);
	}
	kvm_close(kd);

	ksem_destroy_should_fail(id, EBUSY);

	/* Cleanup. */
	ksem_post(id);
	waitpid(pid, NULL, 0);
	ksem_destroy(id);
}
TEST(busy_destroy_test, "destroy unnamed semaphore with waiter");

static int
exhaust_unnamed_child(void *arg)
{
	semid_t id;
	int i, max;

	max = (intptr_t)arg;
	for (i = 0; i < max + 1; i++) {
		if (ksem_init(&id, 1) < 0) {
			if (errno == ENOSPC)
				return (CSTAT(0, 0));
			return (CSTAT(1, errno));
		}
	}
	return (CSTAT(2, 0));
}

static void
exhaust_unnamed_sems(void)
{
	size_t len;
	int nsems_max, stat;

	len = sizeof(nsems_max);
	if (sysctlbyname("p1003_1b.sem_nsems_max", &nsems_max, &len, NULL, 0) <
	    0) {
		fail_errno("sysctl(p1003_1b.sem_nsems_max)");
		return;
	}

	if (child_worker(exhaust_unnamed_child, (void *)(uintptr_t)nsems_max,
	    &stat))
		return;
	errno = CSTAT_ERROR(stat);
	switch (CSTAT_CLASS(stat)) {
	case 0:
		pass();
		break;
	case 1:
		fail_errno("ksem_init");
		break;
	case 2:
		fail_err("Limit of %d semaphores not enforced", nsems_max);
		break;
	default:
		fail_err("bad child state %#x", stat);
		break;
	}
}
TEST(exhaust_unnamed_sems, "exhaust unnamed semaphores (1)");

static int
exhaust_named_child(void *arg)
{
	char buffer[64];
	semid_t id;
	int i, max;

	max = (intptr_t)arg;
	for (i = 0; i < max + 1; i++) {
		snprintf(buffer, sizeof(buffer), "%s%d", TEST_PATH, i);
		if (ksem_open(&id, buffer, O_CREAT, 0777, 1) < 0) {
			if (errno == ENOSPC || errno == EMFILE ||
			    errno == ENFILE)
				return (CSTAT(0, 0));
			return (CSTAT(1, errno));
		}
	}
	return (CSTAT(2, errno));
}

static void
exhaust_named_sems(void)
{
	char buffer[64];
	size_t len;
	int i, nsems_max, stat;

	len = sizeof(nsems_max);
	if (sysctlbyname("p1003_1b.sem_nsems_max", &nsems_max, &len, NULL, 0) <
	    0) {
		fail_errno("sysctl(p1003_1b.sem_nsems_max)");
		return;
	}

	if (child_worker(exhaust_named_child, (void *)(uintptr_t)nsems_max,
	    &stat) < 0)
		return;
	errno = CSTAT_ERROR(stat);
	switch (CSTAT_CLASS(stat)) {
	case 0:
		pass();
		break;
	case 1:
		fail_errno("ksem_open");
		break;
	case 2:
		fail_err("Limit of %d semaphores not enforced", nsems_max);
		break;
	default:
		fail_err("bad child state %#x", stat);
		break;
	}

	/* Cleanup any semaphores created by the child. */
	for (i = 0; i < nsems_max + 1; i++) {
		snprintf(buffer, sizeof(buffer), "%s%d", TEST_PATH, i);
		ksem_unlink(buffer);
	}
}
TEST(exhaust_named_sems, "exhaust named semaphores (1)");

static int
fdlimit_set(void *arg)
{
	struct rlimit rlim;
	int max;

	max = (intptr_t)arg;
	if (getrlimit(RLIMIT_NOFILE, &rlim) < 0)
		return (CSTAT(3, errno));
	rlim.rlim_cur = max;
	if (setrlimit(RLIMIT_NOFILE, &rlim) < 0)
		return (CSTAT(4, errno));
	return (0);
}

static int
fdlimit_unnamed_child(void *arg)
{
	int stat;

	stat = fdlimit_set(arg);
	if (stat == 0)
		stat = exhaust_unnamed_child(arg);
	return (stat);
}

static void
fdlimit_unnamed_sems(void)
{
	int nsems_max, stat;

	nsems_max = 10;
	if (child_worker(fdlimit_unnamed_child, (void *)(uintptr_t)nsems_max,
	    &stat))
		return;
	errno = CSTAT_ERROR(stat);
	switch (CSTAT_CLASS(stat)) {
	case 0:
		pass();
		break;
	case 1:
		fail_errno("ksem_init");
		break;
	case 2:
		fail_err("Limit of %d semaphores not enforced", nsems_max);
		break;
	case 3:
		fail_errno("getrlimit");
		break;
	case 4:
		fail_errno("getrlimit");
		break;
	default:
		fail_err("bad child state %#x", stat);
		break;
	}
}
TEST(fdlimit_unnamed_sems, "exhaust unnamed semaphores (2)");

static int
fdlimit_named_child(void *arg)
{
	int stat;

	stat = fdlimit_set(arg);
	if (stat == 0)
		stat = exhaust_named_child(arg);
	return (stat);
}

static void
fdlimit_named_sems(void)
{
	char buffer[64];
	int i, nsems_max, stat;

	nsems_max = 10;
	if (child_worker(fdlimit_named_child, (void *)(uintptr_t)nsems_max,
	    &stat) < 0)
		return;
	errno = CSTAT_ERROR(stat);
	switch (CSTAT_CLASS(stat)) {
	case 0:
		pass();
		break;
	case 1:
		fail_errno("ksem_open");
		break;
	case 2:
		fail_err("Limit of %d semaphores not enforced", nsems_max);
		break;
	case 3:
		fail_errno("getrlimit");
		break;
	case 4:
		fail_errno("getrlimit");
		break;
	default:
		fail_err("bad child state %#x", stat);
		break;
	}

	/* Cleanup any semaphores created by the child. */
	for (i = 0; i < nsems_max + 1; i++) {
		snprintf(buffer, sizeof(buffer), "%s%d", TEST_PATH, i);
		ksem_unlink(buffer);
	}
}
TEST(fdlimit_named_sems, "exhaust named semaphores (2)");

int
main(int argc, char *argv[])
{

	signal(SIGSYS, SIG_IGN);
	run_tests();
	return (0);
}
