/*-
 * Copyright (c) 2008 Robert N. M. Watson
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
 */

/*-
 * This regression test attempts to confirm that the flags used at open-time
 * for a file descriptor properly limit system calls that should be affected
 * by those flags.  Currently:
 *
 * System call                    Policy                      Tested
 * __acl_aclcheck_fd(2)           any                         no
 * __acl_delete_fd(2)             any                         no
 * __acl_get_fd(2)                any                         no
 * __acl_set_fd(2)                any                         no
 * aio_fsync(2)                   any                         no
 * aio_read(2)                    O_RDONLY or O_RDWR          yes
 * aio_write(2)                   O_WRONLY or O_RDWR          yes
 * dup(2)                         any                         yes
 * dup2(2)                        any                         yes
 * extattr_delete_fd(2)           O_WRONLY or O_RDWR          no
 * extattr_get_fd(2)              O_RDONLY or O_RDWR          no
 * extattr_list_fd(2)             O_RDONLY or O_RDWR          no
 * extattr_set_fd(2)              O_WRONLY or O_RDWR          no
 * fchdir(2)                      any directory               yes
 * fchflags(2)                    any                         yes
 * fchmod(2)                      any                         yes
 * fchown(2)                      any                         yes
 * flock(2)                       any                         yes
 * fpathconf(2)                   any                         yes
 * fstat(2)                       any                         yes
 * fstatfs(2)                     any                         yes
 * fsync(2)                       any                         yes
 * ftruncate(2)                   O_WRONLY or O_RDWR          yes
 * futimes(2)                     any                         yes
 * getdents(2)                    O_RDONLY directory          yes
 * lseek(2)                       any                         yes
 * mmap(2) PROT_READ              O_RDONLY or O_RDWR          yes
 * mmap(2) PROT_WRITE             O_WRONLY or O_RDWR          yes
 * mmap(2) PROT_WRITE + MAP_PRIV  O_RDONLY or O_RDWR          yes
 * mmap(2) PROT_EXEC              O_RDONLY or O_RDWR          yes
 * pread(2)                       O_RDONLY or O_RDWR          yes
 * preadv(2)                      O_RDONLY or O_RDWR          yes
 * pwrite(2)                      O_WRONLY or O_RDWR          yes
 * pwritev(2)                     O_WRONLY or O_RDWR          yes
 * read(2)                        O_RDONLY or O_RDWR          yes
 * readv(2)                       O_RDONLY or O_RDWR          yes
 * sendfile(2)                    O_RDONLY or O_RDWR on file  yes
 * write(2)                       O_WRONLY or O_RDWR          yes
 * writev(2)                      O_WRONLY or O_RDWR          yes
 * 
 * These checks do not verify that original permissions would allow the
 * operation or that open is properly impacted by permissions, just that once
 * a file descriptor is held, open-time limitations are implemented.
 *
 * We do, however, test that directories cannot be opened as writable.
 *
 * XXXRW: Arguably we should also test combinations of bits to mmap(2).
 *
 * XXXRW: Should verify mprotect() remapping limits.
 *
 * XXXRW: kqueue(2)/kevent(2), poll(2), select(2)
 *
 * XXXRW: oaio_read(2), oaio_write(2), freebsd6_*(2).
 *
 * XXXRW: __mac*(2)
 *
 * XXXRW: message queue and shared memory fds?
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <aio.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	PERM_FILE	0644		/* Allow read, write.  Someday exec? */
#define	PERM_DIR	0755		/* Allow read, write, exec. */

/*
 * Modes to try all tests with.
 */
static const int file_modes[] = { O_RDONLY, O_WRONLY, O_RDWR,
    O_RDONLY | O_TRUNC, O_WRONLY | O_TRUNC, O_RDWR | O_TRUNC };
static const int file_modes_count = nitems(file_modes);

static const int dir_modes[] = { O_RDONLY };
static const int dir_modes_count = nitems(dir_modes);

static int testnum;
static int aio_present;

static void
ok_mode(const char *testname, const char *comment, int mode)
{

	testnum++;
	if (comment == NULL)
		printf("ok %d - %s # mode 0x%x\n", testnum, testname, mode);
	else
		printf("ok %d - %s # mode 0x%x - %s\n", testnum, testname,
		    mode, comment);
}

static void
notok_mode(const char *testname, const char *comment, int mode)
{

	testnum++;
	if (comment == NULL)
		printf("not ok %d - %s # mode 0x%x\n", testnum, testname,
		    mode);
	else
		printf("not ok %d - %s # mode 0x%x - %s\n", testnum, testname,
		    mode, comment);
}

/*
 * Before we get started, confirm that we can't open directories writable.
 */
static void
try_directory_open(const char *testname, const char *directory,
    int mode, int expected_errno)
{
	int dfd;

	dfd = open(directory, mode);
	if (dfd >= 0) {
		if (expected_errno)
			notok_mode(testname, "opened", mode);
		else
			ok_mode(testname, NULL, mode);
		close(dfd);
	} else {
		if (expected_errno && expected_errno == errno)
			ok_mode(testname, NULL, mode);
		else if (expected_errno != 0)
			notok_mode(testname, "wrong errno", mode);
		else
			notok_mode(testname, "failed", mode);
	}
}

static void
check_directory_open_modes(const char *directory, const int *modes,
    int modes_count)
{
	int expected_errno, i, mode;

	/*
	 * Directories should only open with O_RDONLY.  Notice that we use
	 * file_modes and not dirmodes.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		if (mode == O_RDONLY)
			expected_errno = 0;
		else
			expected_errno = EISDIR;
		try_directory_open(__func__, directory, mode,
		    expected_errno);
	}
}

static void
check_dup(const char *testname, const char *path, const int *modes,
    int modes_count)
{
	int dfd, fd, i, mode;

	/*
	 * dup() should work regardless of open mode.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		dfd = dup(fd);
		if (dfd >= 0) {
			ok_mode(testname, NULL, mode);
			close(dfd);
		} else
			notok_mode(testname, NULL, mode);
		close(fd);
	}
}

static void
check_dup2(const char *testname, const char *path, const int *modes,
    int modes_count)
{
	int dfd, fd, i, mode;

	/*
	 * dup2() should work regardless of open mode.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		dfd = dup2(fd, 500);	/* Arbitrary but high number. */
		if (dfd >= 0) {
			ok_mode(testname, NULL, mode);
			close(dfd);
		} else
			notok_mode(testname, NULL, mode);
		close(fd);
	}
}

static void
check_fchdir(const char *testname, const char *path, const int *modes,
    int modes_count)
{
	int fd, i, mode;

	/*
	 * fchdir() should work regardless of open mode.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		if (fchdir(fd) == 0)
			ok_mode(testname, NULL, mode);
		else
			notok_mode(testname, "failed", mode);
		close(fd);
	}
}

static void
check_fchflags(const char *testname, const char *path, const int *modes,
    int modes_count)
{
	int fd, i, mode;

	/*
	 * fchflags() should work regardless of open mode.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		if (fchflags(fd, UF_NODUMP) == 0)
			ok_mode(testname, NULL, mode);
		else
			notok_mode(testname, "failed", mode);
		close(fd);
	}
}

static void
check_fchmod(const char *testname, const char *path, int setmode,
    const int *modes, int modes_count)
{
	int fd, i, mode;

	/*
	 * fchmod() should work regardless of open mode.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		if (fchmod(fd, setmode) == 0)
			ok_mode(testname, NULL, mode);
		else
			notok_mode(testname, "failed", mode);
		close(fd);
	}
}

static void
check_fchown(const char *testname, const char *path, const int *modes,
    int modes_count)
{
	int fd, i, mode;

	/*
	 * fchown() should work regardless of open mode.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		if (fchown(fd, -1, -1) == 0)
			ok_mode(testname, NULL, mode);
		else
			notok_mode(testname, "failed", mode);
		close(fd);
	}
}

static void
check_flock(const char *testname, const char *path, const int *modes,
    int modes_count)
{
	int fd, i, mode;

	/*
	 * flock() should work regardless of open mode.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		if (flock(fd, LOCK_EX) == 0)
			ok_mode(testname, NULL, mode);
		else
			notok_mode(testname, "failed", mode);
		close(fd);
	}
}

static void
check_fpathconf(const char *testname, const char *path, const int *modes,
    int modes_count)
{
	int fd, i, mode;
	long l;

	/*
	 * fpathconf() should work regardless of open mode.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		l = fpathconf(fd, _PC_FILESIZEBITS);
		if (l >= 0)
			ok_mode(testname, NULL, mode);
		else
			notok_mode(testname, "failed", mode);
		close(fd);
	}
}

static void
check_fstat(const char *testname, const char *path, const int *modes,
    int modes_count)
{
	struct stat sb;
	int fd, i, mode;

	/*
	 * fstat() should work regardless of open mode.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		if (fstat(fd, &sb) == 0)
			ok_mode(testname, NULL, mode);
		else
			notok_mode(testname, "failed", mode);
		close(fd);
	}
}

static void
check_fstatfs(const char *testname, const char *path, const int *modes,
    int modes_count)
{
	struct statfs statfs;
	int fd, i, mode;

	/*
	 * fstatfs() should work regardless of open mode.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		if (fstatfs(fd, &statfs) == 0)
			ok_mode(testname, NULL, mode);
		else
			notok_mode(testname, "failed", mode);
		close(fd);
	}
}

static void
check_fsync(const char *testname, const char *path, const int *modes,
    int modes_count)
{
	int fd, i, mode;

	/*
	 * fstatfs() should work regardless of open mode.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		if (fsync(fd) == 0)
			ok_mode(testname, NULL, mode);
		else
			notok_mode(testname, "failed", mode);
		close(fd);
	}
}

static void
check_ftruncate(const char *testname, const char *path, const int *modes,
    int modes_count)
{
	struct stat sb;
	int fd, i, mode;

	/*
	 * ftruncate() should work as long as long as (mode & O_ACCMODE) is
	 * O_RDWR or O_WRONLY.
	 *
	 * Directories should never be writable, so this test should always
	 * pass for directories...
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			notok_mode(testname, "truncate1 skipped", mode);
			notok_mode(testname, "truncate2 skipped", mode);
			notok_mode(testname, "truncate3 skipped", mode);
			continue;
		}
		if (fstat(fd, &sb) < 0) {
			notok_mode(testname, "fstat", mode);
			notok_mode(testname, "truncate1 skipped", mode);
			notok_mode(testname, "truncate2 skipped", mode);
			notok_mode(testname, "truncate3 skipped", mode);
			close(fd);
			continue;
		}
		ok_mode(testname, "setup", mode);

		/* Truncate to grow file. */
		if (ftruncate(fd, sb.st_size + 1) == 0) {
			if (((mode & O_ACCMODE) == O_WRONLY) ||
			    ((mode & O_ACCMODE) == O_RDWR))
				ok_mode(testname, "truncate1 succeeded",
				    mode);
			else {
				notok_mode(testname, "truncate1 succeeded",
				    mode);
				notok_mode(testname, "truncate2 skipped",
				    mode);
				notok_mode(testname, "truncate3 skipped",
				    mode);
				close(fd);
				continue;
			}
		} else {
			if (((mode & O_ACCMODE) == O_WRONLY) ||
			    ((mode & O_ACCMODE) == O_RDWR)) {
				notok_mode(testname, "truncate1 failed",
				    mode);
				notok_mode(testname, "truncate2 skipped",
				    mode);
				notok_mode(testname, "truncate3 skipped",
				    mode);
				close(fd);
				continue;
			} else
				ok_mode(testname, "truncate1 failed", mode);
		}

		/* Truncate to same size. */
		if (ftruncate(fd, sb.st_size + 1) == 0) {
			if (((mode & O_ACCMODE) == O_WRONLY) ||
			    ((mode & O_ACCMODE) == O_RDWR))
				ok_mode(testname, "truncate2 succeeded",
				    mode);
			else {
				notok_mode(testname, "truncate2 succeeded",
				    mode);
				notok_mode(testname, "truncate3 skipped",
				    mode);
				close(fd);
				continue;
			}
		} else {
			if (((mode & O_ACCMODE) == O_WRONLY) ||
			    ((mode & O_ACCMODE) == O_RDWR)) {
				notok_mode(testname, "truncate2 failed",
				    mode);
				notok_mode(testname, "truncate3 skipped",
				    mode);
				close(fd);
				continue;
			} else
				ok_mode(testname, "truncate2 failed", mode);
		}

		/* Truncate to shrink. */
		if (ftruncate(fd, sb.st_size) == 0) {
			if (((mode & O_ACCMODE) == O_WRONLY) ||
			    ((mode & O_ACCMODE) == O_RDWR))
				ok_mode(testname, "truncate3 succeeded",
				    mode);
			else
				notok_mode(testname, "truncate3 succeeded",
				    mode);
		} else {
			if (((mode & O_ACCMODE) == O_WRONLY) ||
			    ((mode & O_ACCMODE) == O_RDWR))
				notok_mode(testname, "truncate3 failed",
				    mode);
			else
				ok_mode(testname, "truncate3 failed", mode);
		}
		close(fd);
	}
}

static void
check_futimes(const char *testname, const char *path, const int *modes,
    int modes_count)
{
	int fd, i, mode;

	/*
	 * futimes() should work regardless of open mode.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		if (futimes(fd, NULL) == 0)
			ok_mode(testname, NULL, mode);
		else
			notok_mode(testname, "failed", mode);
		close(fd);
	}
}

static void
check_lseek(const char *testname, const char *path, const int *modes,
    int modes_count)
{
	int fd, i, mode;

	/*
	 * lseek() should work regardless of open mode.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		if (lseek(fd, 100, SEEK_SET) == 100)
			ok_mode(testname, NULL, mode);
		else
			notok_mode(testname, "failed", mode);
		close(fd);
	}
}

static void
check_getdents(const char *testname, const char *path, int isdir,
    const int *modes, int modes_count)
{
	int fd, i, mode;
	char buf[8192];

	/*
	 * getdents() should always work on directories and never on files,
	 * assuming directories are always opened for read (which they are).
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		if (getdents(fd, buf, sizeof(buf)) >= 0) {
			if (isdir && ((mode & O_ACCMODE) == O_RDONLY))
				ok_mode(testname, "directory succeeded",
				    mode);
			else if (isdir)
				notok_mode(testname, "directory succeeded",
				    mode);
			else
				notok_mode(testname, "file succeeded", mode);
		} else {
			if (isdir && ((mode & O_ACCMODE) == O_RDONLY))
				notok_mode(testname, "directory failed",
				    mode);
			else if (isdir)
				ok_mode(testname, "directory failed", mode);
			else
				ok_mode(testname, "file failed", mode);
		}
		close(fd);
	}
}

static void
check_sendfile(const char *testname, const char *path, int isdir,
    const int *modes, int modes_count)
{
	int fd, i, mode, sv[2];
	off_t sent;

	/*
	 * sendfile() should work only on files, and only when the access mode
	 * is O_RDONLY or O_RDWR.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		if (socketpair(PF_LOCAL, SOCK_STREAM, 0, sv) < 0) {
			notok_mode(testname, "socketpair", mode);
			continue;
		}
		if (sendfile(fd, sv[0], 0, 1, NULL, &sent, 0) == 0) {
			if (isdir)
				notok_mode(testname, "directory succeeded",
				    mode);
			else if (((mode & O_ACCMODE) == O_RDONLY) ||
			    ((mode & O_ACCMODE) == O_RDWR))
				ok_mode(testname, "succeeded", mode);
			else
				notok_mode(testname, "succeeded", mode);
		} else {
			if (isdir)
				ok_mode(testname, "directory failed", mode);
			else if (((mode & O_ACCMODE) == O_RDONLY) ||
			    ((mode & O_ACCMODE) == O_RDWR))
				notok_mode(testname, "failed", mode);
			else
				ok_mode(testname, "failed", mode);
		}
		close(sv[0]);
		close(sv[1]);
		close(fd);
	}
}

/*
 * Various functions write, so just make write-like wrappers for them.
 */
typedef ssize_t (*write_fn)(int d, const void *buf, size_t nbytes);

static ssize_t
writev_wrapper(int d, const void *buf, size_t nbytes)
{
	struct iovec iov;

	iov.iov_base = (void *)buf;
	iov.iov_len = nbytes;
	return (writev(d, &iov, 1));
}

static ssize_t
pwrite_wrapper(int d, const void *buf, size_t nbytes)
{

	return (pwrite(d, buf, nbytes, 0));
}

static ssize_t
pwritev_wrapper(int d, const void *buf, size_t nbytes)
{
	struct iovec iov;

	iov.iov_base = (void *)buf;
	iov.iov_len = nbytes;
	return (pwritev(d, &iov, 1, 0));
}

static ssize_t
aio_write_wrapper(int d, const void *buf, size_t nbytes)
{
	struct aiocb aiocb;
	struct aiocb const *aiocb_array[] = { &aiocb };

	bzero(&aiocb, sizeof(aiocb));
	aiocb.aio_fildes = d;
	aiocb.aio_buf = (void *)buf;
	aiocb.aio_nbytes = nbytes;
	if (aio_write(&aiocb) < 0)
		return (-1);
	aiocb_array[0] = &aiocb;
	if (aio_suspend(aiocb_array, 1, NULL) < 0)
		return (-1);
	return (aio_return(&aiocb));
}

static void
check_write(const char *testname, write_fn fn, const char *path,
    const int *modes, int modes_count)
{
	int fd, i, mode;
	char ch;

	/*
	 * write() should never succeed for directories, but especially
	 * because they can only be opened read-only.  write() on files
	 * should succeed for O_WRONLY and O_RDWR descriptors.
	 */

	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		if (fn(fd, &ch, sizeof(ch)) < 0) {
			if ((mode & O_ACCMODE) == O_WRONLY ||
			    (mode & O_ACCMODE) == O_RDWR)
				notok_mode(testname, "write failed", mode);
			else
				ok_mode(testname, "write failed", mode);
		} else {
			if (!((mode & O_ACCMODE) == O_WRONLY ||
			    (mode & O_ACCMODE) == O_RDWR))
				notok_mode(testname, "write succeeded", mode);
			else
				ok_mode(testname, "write succeeded", mode);
		}
		close(fd);
	}
}

/*
 * Various functions read, so just make read-like wrappers for them.
 */
typedef ssize_t (*read_fn)(int d, void *buf, size_t nbytes);

static ssize_t
readv_wrapper(int d, void *buf, size_t nbytes)
{
	struct iovec iov;

	iov.iov_base = buf;
	iov.iov_len = nbytes;
	return (readv(d, &iov, 1));
}

static ssize_t
pread_wrapper(int d, void *buf, size_t nbytes)
{

	return (pread(d, buf, nbytes, 0));
}

static ssize_t
preadv_wrapper(int d, void *buf, size_t nbytes)
{
	struct iovec iov;

	iov.iov_base = buf;
	iov.iov_len = nbytes;
	return (preadv(d, &iov, 1, 0));
}

static ssize_t
aio_read_wrapper(int d, void *buf, size_t nbytes)
{
	struct aiocb aiocb;
	struct aiocb const *aiocb_array[] = { &aiocb };

	bzero(&aiocb, sizeof(aiocb));
	aiocb.aio_fildes = d;
	aiocb.aio_buf = buf;
	aiocb.aio_nbytes = nbytes;
	if (aio_read(&aiocb) < 0)
		return (-1);
	if (aio_suspend(aiocb_array, 1, NULL) < 0)
		return (-1);
	return (aio_return(&aiocb));
}

static void
check_read(const char *testname, read_fn fn, const char *path,
    const int *modes, int modes_count)
{
	int fd, i, mode;
	char ch;

	/*
	 * read() should (generally) succeeded on directories.  read() on
	 * files should succeed for O_RDONLY and O_RDWR descriptors.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		if (fn(fd, &ch, sizeof(ch)) < 0) {
			if ((mode & O_ACCMODE) == O_RDONLY ||
			    (mode & O_ACCMODE) == O_RDWR)
				notok_mode(testname, "read failed", mode);
			else
				ok_mode(testname, "read failed", mode);
		} else {
			if (!((mode & O_ACCMODE) == O_RDONLY ||
			    (mode & O_ACCMODE) == O_RDWR))
				notok_mode(testname, "read succeeded", mode);
			else
				ok_mode(testname, "read succeeded", mode);
		}
		close(fd);
	}
}

static void
check_mmap_read(const char *testname, const char *path, int isdir,
    const int *modes, int modes_count)
{
	int fd, i, mode;
	char *addr;

	/*
	 * mmap() read should fail for directories (ideally?) but succeed for
	 * O_RDONLY and O_RDWR file descriptors.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		addr = mmap(NULL, getpagesize(), PROT_READ, MAP_SHARED, fd,
		    0);
		if (addr == MAP_FAILED) {
			if (isdir)
				ok_mode(testname, "mmap dir failed", mode);
			else if ((mode & O_ACCMODE) == O_RDONLY ||
			    (mode & O_ACCMODE) == O_RDWR)
				notok_mode(testname, "mmap file failed",
				    mode);
			else
				ok_mode(testname, "mmap file failed", mode);
		} else {
			if (isdir)
				notok_mode(testname, "mmap dir succeeded",
				    mode);
			else if ((mode & O_ACCMODE) == O_RDONLY ||
			    (mode & O_ACCMODE) == O_RDWR)
				ok_mode(testname, "mmap file succeeded",
				    mode);
			else
				notok_mode(testname, "mmap file succeeded",
				    mode);
			(void)munmap(addr, getpagesize());
		}
		close(fd);
	}
}

static void
check_mmap_write(const char *testname, const char *path, const int *modes,
    int modes_count)
{
	int fd, i, mode;
	char *addr;

	/*
	 * mmap() will always fail for directories (ideally) as they are
	 * always open O_RDONLY.  Check for O_WRONLY or O_RDWR to permit a
	 * write mapping.  This variant does a MAP_SHARED mapping, but we
	 * are also interested in MAP_PRIVATE.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		addr = mmap(NULL, getpagesize(), PROT_WRITE, MAP_SHARED, fd,
		    0);
		if (addr == MAP_FAILED) {
			if ((mode & O_ACCMODE) == O_WRONLY ||
			    (mode & O_ACCMODE) == O_RDWR)
				notok_mode(testname, "mmap failed",
				    mode);
			else
				ok_mode(testname, "mmap failed", mode);
		} else {
			if ((mode & O_ACCMODE) == O_WRONLY ||
			    (mode & O_ACCMODE) == O_RDWR)
				ok_mode(testname, "mmap succeeded",
				    mode);
			else
				notok_mode(testname, "mmap succeeded", mode);
			(void)munmap(addr, getpagesize());
		}
		close(fd);
	}
}

static void
check_mmap_exec(const char *testname, const char *path, int isdir,
    const int *modes, int modes_count)
{
	int fd, i, mode;
	char *addr;

	/*
	 * mmap() exec should fail for directories (ideally?) but succeed for
	 * O_RDONLY and O_RDWR file descriptors.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		addr = mmap(NULL, getpagesize(), PROT_EXEC, MAP_SHARED, fd,
		    0);
		if (addr == MAP_FAILED) {
			if (isdir)
				ok_mode(testname, "mmap dir failed", mode);
			else if ((mode & O_ACCMODE) == O_RDONLY ||
			    (mode & O_ACCMODE) == O_RDWR)
				notok_mode(testname, "mmap file failed",
				    mode);
			else
				ok_mode(testname, "mmap file failed", mode);
		} else {
			if (isdir)
				notok_mode(testname, "mmap dir succeeded",
				    mode);
			else
				ok_mode(testname, "mmap file succeeded",
				    mode);
			(void)munmap(addr, getpagesize());
		}
		close(fd);
	}
}

static void
check_mmap_write_private(const char *testname, const char *path, int isdir,
    const int *modes, int modes_count)
{
	int fd, i, mode;
	char *addr;

	/*
	 * mmap() write private should succeed for readable descriptors
	 * except for directories.
	 */
	for (i = 0; i < modes_count; i++) {
		mode = modes[i];
		fd = open(path, mode);
		if (fd < 0) {
			notok_mode(testname, "open", mode);
			continue;
		}
		addr = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE,
		    MAP_PRIVATE, fd, 0);
		if (addr == MAP_FAILED) {
			if (isdir)
				ok_mode(testname, "mmap dir failed", mode);
			else if ((mode & O_ACCMODE) == O_RDONLY ||
			    (mode & O_ACCMODE) == O_RDWR)
				notok_mode(testname, "mmap file failed",
				    mode);
			else
				ok_mode(testname, "mmap file failed", mode);
		} else {
			if (isdir)
				notok_mode(testname, "mmap dir succeeded",
				    mode);
			else if ((mode & O_ACCMODE) == O_RDONLY ||
			    (mode & O_ACCMODE) == O_RDWR)
				ok_mode(testname, "mmap file succeeded",
				    mode);
			else
				notok_mode(testname, "mmap file succeeded",
				    mode);
			(void)munmap(addr, getpagesize());
		}
		close(fd);
	}
}

int
main(void)
{
	char dir_path[PATH_MAX], file_path[PATH_MAX];
	int dummy, fd;
	size_t size;

	aio_present = 0;
	size = sizeof(dummy);
	if (sysctlbyname("vfs.aio", &dummy, &size, NULL, 0) < 0) {
		if (errno == EISDIR)
			aio_present = 1;
	}

	strlcpy(dir_path, "/tmp/open-dir.XXXXXXXXXXX", sizeof(dir_path));
	if (mkdtemp(dir_path) == NULL)
		err(1, "mkdtemp");
	if (chmod(dir_path, PERM_DIR) < 0) {
		warn("chmod %s", dir_path);
		(void)rmdir(dir_path);
		exit(1);
	}
	strlcpy(file_path, "/tmp/open-file.XXXXXXXXXXX", sizeof(file_path));
	fd = mkstemp(file_path);
	if (fd < 0) {
		warn("mkstemp");
		(void)rmdir(dir_path);
		exit(1);
	}
	close(fd);
	if (chmod(file_path, PERM_FILE) < 0) {
		warn("chmod %s", file_path);
		(void)unlink(file_path);
		(void)rmdir(dir_path);
		exit(1);
	}
	check_directory_open_modes(dir_path, file_modes, file_modes_count);

	check_dup("check_dup_dir", dir_path, dir_modes, dir_modes_count);
	check_dup("check_dup_file", file_path, file_modes, file_modes_count);

	check_dup2("check_dup2_dir", dir_path, dir_modes, dir_modes_count);
	check_dup2("check_dup2_file", file_path, file_modes,
	    file_modes_count);

	check_fchdir("check_fchdir", dir_path, dir_modes, dir_modes_count);

	check_fchflags("check_fchflags_dir", dir_path, dir_modes,
	    dir_modes_count);
	check_fchflags("check_fchflags_file", file_path, file_modes,
	    file_modes_count);

	check_fchmod("check_fchmod_dir", dir_path, PERM_DIR, dir_modes,
	    dir_modes_count);
	check_fchmod("check_fchmod_file", file_path, PERM_FILE, file_modes,
	    file_modes_count);

	check_fchown("check_fchown_dir", dir_path, dir_modes,
	    dir_modes_count);
	check_fchown("check_fchown_file", file_path, file_modes,
	    file_modes_count);

	check_flock("check_flock_dir", dir_path, dir_modes, dir_modes_count);
	check_flock("check_flock_file", file_path, file_modes,
	    file_modes_count);

	check_fpathconf("check_fpathconf_dir", dir_path, dir_modes,
	    dir_modes_count);
	check_fpathconf("check_fpathconf_file", file_path, file_modes,
	    file_modes_count);

	check_fstat("check_fstat_dir", dir_path, dir_modes, dir_modes_count);
	check_fstat("check_fstat_file", file_path, file_modes,
	    file_modes_count);

	check_fstatfs("check_fstatfs_dir", dir_path, dir_modes,
	    dir_modes_count);
	check_fstatfs("check_fstatfs_file", file_path, file_modes,
	    file_modes_count);

	check_fsync("check_fsync_dir", dir_path, dir_modes, dir_modes_count);
	check_fsync("check_fsync_file", file_path, file_modes,
	    file_modes_count);

	check_ftruncate("check_ftruncate_dir", dir_path, dir_modes,
	    dir_modes_count);
	check_ftruncate("check_ftruncate_file", file_path, file_modes,
	    file_modes_count);

	check_futimes("check_futimes_dir", dir_path, dir_modes,
	    dir_modes_count);
	check_futimes("check_futimes_file", file_path, file_modes,
	    file_modes_count);

	check_lseek("check_lseek_dir", dir_path, dir_modes, dir_modes_count);
	check_lseek("check_lseek_file", file_path, file_modes,
	    file_modes_count);

	check_getdents("check_getdents_dir", dir_path, 1, dir_modes,
	    dir_modes_count);
	check_getdents("check_getdents_file", file_path, 0, file_modes,
	    file_modes_count);

	check_sendfile("check_sendfile_dir", dir_path, 1, dir_modes,
	    dir_modes_count);
	check_sendfile("check_sendfile_file", file_path, 0, file_modes,
	    file_modes_count);

	check_write("check_write_dir", write, dir_path, dir_modes,
	    dir_modes_count);
	check_write("check_write_file", write, file_path, file_modes,
	    file_modes_count);

	check_write("check_writev_dir", writev_wrapper, dir_path, dir_modes,
	    dir_modes_count);
	check_write("check_writev_file", writev_wrapper, file_path,
	    file_modes, file_modes_count);

	check_write("check_pwrite_dir", pwrite_wrapper, dir_path, dir_modes,
	    dir_modes_count);
	check_write("check_pwrite_file", pwrite_wrapper, file_path,
	    file_modes, file_modes_count);

	check_write("check_pwritev_dir", pwritev_wrapper, dir_path,
	    dir_modes, dir_modes_count);
	check_write("check_pwritev_file", pwritev_wrapper, file_path,
	    file_modes, file_modes_count);

	if (aio_present) {
		check_write("check_aio_write_dir", aio_write_wrapper,
		    dir_path, dir_modes, dir_modes_count);
		check_write("check_aio_write_file", aio_write_wrapper,
		    file_path, file_modes, file_modes_count);
	}

	check_read("check_read_dir", read, dir_path, dir_modes,
	    dir_modes_count);
	check_read("check_read_file", read, file_path, file_modes,
	    file_modes_count);

	check_read("check_readv_dir", readv_wrapper, dir_path, dir_modes,
	    dir_modes_count);
	check_read("check_readv_file", readv_wrapper, file_path,
	    file_modes, file_modes_count);

	check_read("check_pread_dir", pread_wrapper, dir_path, dir_modes,
	    dir_modes_count);
	check_read("check_pread_file", pread_wrapper, file_path,
	    file_modes, file_modes_count);

	check_read("check_preadv_dir", preadv_wrapper, dir_path,
	    dir_modes, dir_modes_count);
	check_read("check_preadv_file", preadv_wrapper, file_path,
	    file_modes, file_modes_count);

	if (aio_present) {
		check_read("check_aio_read_dir", aio_read_wrapper, dir_path,
		    dir_modes, dir_modes_count);
		check_read("check_aio_read_file", aio_read_wrapper,
		    file_path, file_modes, file_modes_count);
	}

	check_mmap_read("check_mmap_read_dir", dir_path, 1, dir_modes,
	    dir_modes_count);
	check_mmap_read("check_mmap_read_file", file_path, 0, file_modes,
	    file_modes_count);

	check_mmap_write("check_mmap_write_dir", dir_path, dir_modes,
	    dir_modes_count);
	check_mmap_write("check_mmap_write_file", file_path, file_modes,
	    file_modes_count);

	check_mmap_exec("check_mmap_exec_dir", dir_path, 1, dir_modes,
	    dir_modes_count);
	check_mmap_exec("check_mmap_exec_file", file_path, 0, file_modes,
	    file_modes_count);

	check_mmap_write_private("check_mmap_write_private_dir", dir_path, 1,
	    dir_modes, dir_modes_count);
	check_mmap_write_private("check_mmap_write_private_file", file_path,
	    0, file_modes, file_modes_count);

	(void)unlink(file_path);
	(void)rmdir(dir_path);
	exit(0);
}
