/*-
 * Copyright (c) 2005 Robert N. M. Watson
 * All rights reserved.
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
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Regression test to exercise various POSIX-defined parts of fifo behavior
 * described for open(2):
 *
 * O_NONBLOCK
 * When opening a FIFO with O_RDONLY or O_WRONLY set:
 *
 * - If O_NONBLOCK is set, an open() for reading-only shall return without
 *   delay. An open() for writing-only shall return an error if no process
 *   currently has the file open for reading.
 *
 * - If O_NONBLOCK is clear, an open() for reading-only shall block the
 *   calling thread until a thread opens the file for writing. An open()
 *   for writing-only shall block the calling thread until a thread opens
 *   the file for reading.
 *
 * When opening a block special or character special file that supports
 * non-blocking opens:
 *
 * - If O_NONBLOCK is set, the open() function shall return without blocking
 *   for the device to be ready or available. Subsequent behavior of the
 *   device is device-specific.
 *
 * - If O_NONBLOCK is clear, the open() function shall block the calling
 *   thread until the device is ready or available before returning.
 *
 * Special errors:
 *
 * [ENXIO]
 * O_NONBLOCK is set, the named file is a FIFO, O_WRONLY is set, and no
 * process has the file open for reading.
 */

/*
 * In order to test blocking/non-blocking behavior, test processes must
 * potentially block themselves until released.  As bugs in blocking result
 * in processes that won't un-block, we must sacrifice a process to the task,
 * watching and potentially killing it after a time-out.  The main test
 * process is never used to open or act directly on a fifo (other than to
 * create or unlink it) in order to avoid the main test process being
 * blocked.
 */

/*
 * All activity occurs within a temporary directory created early in the
 * test.
 */
static char	temp_dir[PATH_MAX];

static void __unused
atexit_temp_dir(void)
{

	rmdir(temp_dir);
}

/*
 * Run a function in a particular test process.
 */
static int
run_in_process(int (*func)(void), pid_t *pidp, const char *errstr)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		warn("%s: run_in_process: fork", errstr);
		return (-1);
	}

	if (pid == 0)
		exit(func());

	if (pidp != NULL)
		*pidp = pid;

	return (0);
}

/*
 * Wait for a process on a timeout, and if the timeout expires, kill the
 * process.  Test each second rather than waiting the full timeout at once to
 * minimize the amount of time spent hanging around unnecessarily.
 */
static int
wait_and_timeout(pid_t pid, int timeout, int *status, const char *errstr)
{
	pid_t wpid;
	int i;

	/*
	 * Count up to the timeout, but do a non-hanging waitpid() after each
	 * second so we can avoid waiting a lot of extra time.
	 */
	for (i = 0; i < timeout; i++) {
		wpid = waitpid(pid, status, WNOHANG);
		if (wpid < 0) {
			warn("%s: wait_and_timeout: waitpid %d", errstr, pid);
			return (-1);
		}

		if (wpid == pid)
			return (0);

		sleep(1);
	}

	wpid = waitpid(pid, status, WNOHANG);
	if (wpid < 0) {
		warn("%s: wait_and_timeout: waitpid %d", errstr, pid);
		return (-1);
	}

	if (wpid == pid)
		return (0);

	if (kill(pid, SIGTERM) < 0) {
		warn("%s: wait_and_timeout: kill %d", errstr, pid);
		return (-1);
	}

	wpid = waitpid(pid, status, 0);
	if (wpid < 0) {
		warn("%s: wait_and_timeout: waitpid %d", errstr, pid);
		return (-1);
	}

	if (wpid != pid) {
		warn("%s: waitpid: returned %d not %d", errstr, wpid, pid);
		return (-1);
	}

	warnx("%s: process blocked", errstr);
	return (-1);
}

static int
non_blocking_open_reader(void)
{
	int fd;

	fd = open("testfifo", O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return (errno);
	close(fd);

	return (0);
}

static int
non_blocking_open_writer(void)
{
	int fd;

	fd = open("testfifo", O_WRONLY | O_NONBLOCK);
	if (fd < 0)
		return (errno);
	close(fd);

	return (0);
}

static int
blocking_open_reader(void)
{
	int fd;

	fd = open("testfifo", O_RDONLY);
	if (fd < 0)
		return (errno);
	close(fd);

	return (0);
}

static int
blocking_open_writer(void)
{
	int fd;

	fd = open("testfifo", O_WRONLY);
	if (fd < 0)
		return (errno);
	close(fd);

	return (0);
}

static void
test_blocking_reader(void)
{
	pid_t reader_pid, writer_pid, wpid;
	int error, status;

	if (mkfifo("testfifo", 0600) < 0)
		err(-1, "test_blocking_reader: mkfifo: testfifo");

	/*
	 * Block a process in opening the fifo.
	 */
	if (run_in_process(blocking_open_reader, &reader_pid,
	    "test_blocking_reader: blocking_open_reader") < 0) {
		(void)unlink("testfifo");
		exit(-1);
	}

	/*
	 * Test that it blocked.
	 */
	sleep(5);
	wpid = waitpid(reader_pid, &status, WNOHANG);
	if (wpid < 0) {
		error = errno;
		(void)unlink("testfifo");
		errno = error;
		err(-1, "test_blocking_reader: waitpid %d", reader_pid);
	}

	if (wpid != 0 && wpid != reader_pid) {
		(void)unlink("testfifo");
		errx(-1, "test_blocking_reader: waitpid %d returned %d",
		    reader_pid, wpid);
	}

	if (wpid == reader_pid) {
		(void)unlink("testfifo");
		errx(-1, "test_blocking_reader: blocking child didn't "
		    "block");
	}

	/*
	 * Unblock the blocking reader.
	 */
	if (run_in_process(blocking_open_writer, &writer_pid,
	    "test_blocking_reader: blocking_open_writer") < 0) {
		(void)unlink("testfifo");
		(void)kill(reader_pid, SIGTERM);
		(void)waitpid(reader_pid, &status, 0);
		exit(-1);
	}

	/*
	 * Make sure both processes exited quickly (<1 second) to make sure
	 * they didn't block, and GC.
	 */
	if (wait_and_timeout(reader_pid, 1, &status,
	    "test_blocking_reader: blocking_open_reader") < 0) {
		(void)unlink("testinfo");
		(void)kill(reader_pid, SIGTERM);
		(void)kill(writer_pid, SIGTERM);
		exit(-1);
	}
	
	if (wait_and_timeout(writer_pid, 1, &status,
	    "test_blocking_reader: blocking_open_writer") < 0) {
		(void)unlink("testinfo");
		(void)kill(writer_pid, SIGTERM);
		exit(-1);
	}
	
	if (unlink("testfifo") < 0)
		err(-1, "test_blocking_reader: unlink: testfifo");
}
static void
test_blocking_writer(void)
{
	pid_t reader_pid, writer_pid, wpid;
	int error, status;

	if (mkfifo("testfifo", 0600) < 0)
		err(-1, "test_blocking_writer: mkfifo: testfifo");

	/*
	 * Block a process in opening the fifo.
	 */
	if (run_in_process(blocking_open_writer, &writer_pid,
	    "test_blocking_writer: blocking_open_writer") < 0) {
		(void)unlink("testfifo");
		exit(-1);
	}

	/*
	 * Test that it blocked.
	 */
	sleep(5);
	wpid = waitpid(writer_pid, &status, WNOHANG);
	if (wpid < 0) {
		error = errno;
		(void)unlink("testfifo");
		errno = error;
		err(-1, "test_blocking_writer: waitpid %d", writer_pid);
	}

	if (wpid != 0 && wpid != writer_pid) {
		(void)unlink("testfifo");
		errx(-1, "test_blocking_writer: waitpid %d returned %d",
		    writer_pid, wpid);
	}

	if (wpid == writer_pid) {
		(void)unlink("testfifo");
		errx(-1, "test_blocking_writer: blocking child didn't "
		    "block");
	}

	/*
	 * Unblock the blocking writer.
	 */
	if (run_in_process(blocking_open_reader, &reader_pid,
	    "test_blocking_writer: blocking_open_reader") < 0) {
		(void)unlink("testfifo");
		(void)kill(writer_pid, SIGTERM);
		(void)waitpid(writer_pid, &status, 0);
		exit(-1);
	}

	/*
	 * Make sure both processes exited quickly (<1 second) to make sure
	 * they didn't block, and GC.
	 */
	if (wait_and_timeout(writer_pid, 1, &status,
	    "test_blocking_writer: blocking_open_writer") < 0) {
		(void)unlink("testinfo");
		(void)kill(writer_pid, SIGTERM);
		(void)kill(reader_pid, SIGTERM);
		(void)waitpid(writer_pid, &status, 0);
		(void)waitpid(reader_pid, &status, 0);
		exit(-1);
	}
	
	if (wait_and_timeout(reader_pid, 1, &status,
	    "test_blocking_writer: blocking_open_reader") < 0) {
		(void)unlink("testinfo");
		(void)kill(reader_pid, SIGTERM);
		(void)waitpid(reader_pid, &status, 0);
		exit(-1);
	}
	
	if (unlink("testfifo") < 0)
		err(-1, "test_blocking_writer: unlink: testfifo");
}

static void
test_non_blocking_reader(void)
{
	int status;
	pid_t pid;

	if (mkfifo("testfifo", 0600) < 0)
		err(-1, "test_non_blocking_reader: mkfifo: testfifo");

	if (run_in_process(non_blocking_open_reader, &pid,
	    "test_non_blocking_reader: non_blocking_open_reader") < 0) {
		(void)unlink("testfifo");
		exit(-1);
	}

	status = -1;
	if (wait_and_timeout(pid, 5, &status,
	    "test_non_blocking_reader: non_blocking_open_reader") < 0) {
		(void)unlink("testfifo");
		exit(-1);
	}

	if (WEXITSTATUS(status) != 0) {
		(void)unlink("testfifo");
		errno = WEXITSTATUS(status);
		err(-1, "test_non_blocking_reader: "
		    "non_blocking_open_reader: open: testfifo");
	}

	if (unlink("testfifo") < 0)
		err(-1, "test_non_blocking_reader: unlink: testfifo");
}

static void
test_non_blocking_writer(void)
{
	int status;
	pid_t pid;

	if (mkfifo("testfifo", 0600) < 0)
		err(-1, "test_non_blocking_writer: mkfifo: testfifo");

	if (run_in_process(non_blocking_open_writer, &pid,
	    "test_non_blocking_writer: non_blocking_open_writer") < 0) {
		(void)unlink("testfifo");
		exit(-1);
	}

	status = -1;
	if (wait_and_timeout(pid, 5, &status,
	    "test_non_blocking_writer: non_blocking_open_writer") < 0) {
		(void)unlink("testfifo");
		exit(-1);
	}

	if (WEXITSTATUS(status) != ENXIO) {
		(void)unlink("testfifo");

		errno = WEXITSTATUS(status);
		if (errno == 0)
			errx(-1, "test_non_blocking_writer: "
			    "non_blocking_open_writer: open succeeded");
		err(-1, "test_non_blocking_writer: "
		    "non_blocking_open_writer: open: testfifo");
	}

	if (unlink("testfifo") < 0)
		err(-1, "test_non_blocking_writer: unlink: testfifo");
}

int
main(void)
{

	if (geteuid() != 0)
		errx(-1, "must be run as root");

	strcpy(temp_dir, "fifo_open.XXXXXXXXXXX");
	if (mkdtemp(temp_dir) == NULL)
		err(-1, "mkdtemp");
	if (chdir(temp_dir) < 0)
		err(-1, "chdir: %s", temp_dir);
	atexit(atexit_temp_dir);

	test_non_blocking_reader();
	test_non_blocking_writer();

	test_blocking_reader();
	test_blocking_writer();

	return (0);
}
