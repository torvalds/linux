/*-
 * Copyright (c) 2005 Robert N. M. Watson
 * Copyright (c) 2012 Jilles Tjoelker
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
#include <sys/event.h>
#include <sys/filio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Regression test for piddling details of fifos.
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

static void
makefifo(const char *fifoname, const char *testname)
{

	if (mkfifo(fifoname, 0700) < 0)
		err(-1, "%s: makefifo: mkfifo: %s", testname, fifoname);
}

static void
cleanfifo(const char *fifoname, int fd1, int fd2)
{

	if (fd1 != -1)
		close(fd1);
	if (fd2 != -1)
		close(fd2);
	(void)unlink(fifoname);
}

static int
openfifo(const char *fifoname, int *reader_fdp, int *writer_fdp)
{
	int error, fd1, fd2;

	fd1 = open(fifoname, O_RDONLY | O_NONBLOCK);
	if (fd1 < 0)
		return (-1);
	fd2 = open(fifoname, O_WRONLY | O_NONBLOCK);
	if (fd2 < 0) {
		error = errno;
		close(fd1);
		errno = error;
		return (-1);
	}
	*reader_fdp = fd1;
	*writer_fdp = fd2;

	return (0);
}

/*
 * POSIX does not allow lseek(2) on fifos, so we expect ESPIPE as a result.
 */
static void
test_lseek(void)
{
	int reader_fd, writer_fd;

	makefifo("testfifo", __func__);

	if (openfifo("testfifo", &reader_fd, &writer_fd) < 0) {
		warn("%s: openfifo", __func__);
		cleanfifo("testfifo", -1, -1);
		exit(-1);
	}

	if (lseek(reader_fd, 1, SEEK_CUR) >= 0) {
		warnx("%s: lseek succeeded instead of returning ESPIPE",
		    __func__);
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}
	if (errno != ESPIPE) {
		warn("%s: lseek returned instead of ESPIPE", __func__);
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}

	cleanfifo("testfifo", reader_fd, writer_fd);
}

/*
 * truncate(2) on FIFO should silently return success.
 */
static void
test_truncate(void)
{

	makefifo("testfifo", __func__);

	if (truncate("testfifo", 1024) != 0) {
		warn("%s: truncate", __func__);
		cleanfifo("testfifo", -1, -1);
		exit(-1);
	}

	cleanfifo("testfifo", -1, -1);
}

static int
test_ioctl_setclearflag(int fd, unsigned long flag, const char *testname,
    const char *fdname, const char *flagname)
{
	int i;

	i = 1;
	if (ioctl(fd, flag, &i) < 0) {
		warn("%s:%s: ioctl(%s, %s, 1)", testname, __func__, fdname,
		    flagname);
		cleanfifo("testfifo", -1, -1);
		exit(-1);
	}

	i = 0;
	if (ioctl(fd, flag, &i) < 0) {
		warn("%s:%s: ioctl(%s, %s, 0)", testname, __func__, fdname,
		    flagname);
		cleanfifo("testfifo", -1, -1);
		exit(-1);
	}

	return (0);
}

/*
 * Test that various ioctls can be issued against the file descriptor.  We
 * don't currently test the semantics of these changes here.
 */
static void
test_ioctl(void)
{
	int reader_fd, writer_fd;

	makefifo("testfifo", __func__);

	if (openfifo("testfifo", &reader_fd, &writer_fd) < 0) {
		warn("%s: openfifo", __func__);
		cleanfifo("testfifo", -1, -1);
		exit(-1);
	}

	/*
	 * Set and remove the non-blocking I/O flag.
	 */
	if (test_ioctl_setclearflag(reader_fd, FIONBIO, __func__,
	    "reader_fd", "FIONBIO") < 0) {
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}

	if (test_ioctl_setclearflag(writer_fd, FIONBIO, __func__,
	    "writer_fd", "FIONBIO") < 0) {
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}

	/*
	 * Set and remove the async I/O flag.
	 */
	if (test_ioctl_setclearflag(reader_fd, FIOASYNC, __func__,
	    "reader_fd", "FIOASYNC") < 0) {
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}

	if (test_ioctl_setclearflag(writer_fd, FIOASYNC, __func__,
	    "writer_fd", "FIONASYNC") < 0) {
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}

	cleanfifo("testfifo", reader_fd, writer_fd);
}

/*
 * fchmod(2)/fchown(2) on FIFO should work.
 */
static void
test_chmodchown(void)
{
	struct stat sb;
	int reader_fd, writer_fd;
	uid_t u;
	gid_t g;

	makefifo("testfifo", __func__);

	if (openfifo("testfifo", &reader_fd, &writer_fd) < 0) {
		warn("%s: openfifo", __func__);
		cleanfifo("testfifo", -1, -1);
		exit(-1);
	}

	if (fchmod(reader_fd, 0666) != 0) {
		warn("%s: fchmod", __func__);
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}

	if (stat("testfifo", &sb) != 0) {
		warn("%s: stat", __func__);
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}

	if ((sb.st_mode & 0777) != 0666) {
		warnx("%s: stat chmod result", __func__);
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}

	if (fstat(writer_fd, &sb) != 0) {
		warn("%s: fstat", __func__);
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}

	if ((sb.st_mode & 0777) != 0666) {
		warnx("%s: fstat chmod result", __func__);
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}

	if (fchown(reader_fd, -1, -1) != 0) {
		warn("%s: fchown 1", __func__);
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}

	u = geteuid();
	if (u == 0)
		u = 1;
	g = getegid();
	if (fchown(reader_fd, u, g) != 0) {
		warn("%s: fchown 2", __func__);
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}
	if (stat("testfifo", &sb) != 0) {
		warn("%s: stat", __func__);
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}

	if (sb.st_uid != u || sb.st_gid != g) {
		warnx("%s: stat chown result", __func__);
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}

	if (fstat(writer_fd, &sb) != 0) {
		warn("%s: fstat", __func__);
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}

	if (sb.st_uid != u || sb.st_gid != g) {
		warnx("%s: fstat chown result", __func__);
		cleanfifo("testfifo", reader_fd, writer_fd);
		exit(-1);
	}

	cleanfifo("testfifo", -1, -1);
}

int
main(void)
{

	strcpy(temp_dir, "fifo_misc.XXXXXXXXXXX");
	if (mkdtemp(temp_dir) == NULL)
		err(-1, "mkdtemp");
	atexit(atexit_temp_dir);

	if (chdir(temp_dir) < 0)
		err(-1, "chdir %s", temp_dir);

	test_lseek();
	test_truncate();
	test_ioctl();
	test_chmodchown();

	return (0);
}
