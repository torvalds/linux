/*-
 * Copyright (c) 2009-2011 Robert N. M. Watson
 * Copyright (c) 2011 Jonathan Anderson
 * Copyright (c) 2012 FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Pawel Jakub Dawidek under
 * sponsorship from the FreeBSD Foundation.
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

/*
 * Test whether various operations on capabilities are properly masked for
 * various object types.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cap_test.h"

#define	SYSCALL_FAIL(syscall, message) \
	FAIL("%s:\t%s (rights 0x%jx)", #syscall, message, rights)

/*
 * Ensure that, if the capability had enough rights for the system call to
 * pass, then it did. Otherwise, ensure that the errno is ENOTCAPABLE;
 * capability restrictions should kick in before any other error logic.
 */
#define	CHECK_RESULT(syscall, rights_needed, succeeded)	do {		\
	if ((rights & (rights_needed)) == (rights_needed)) {		\
		if (succeeded) {					\
			if (success == -1)				\
				success = PASSED;			\
		} else {						\
			SYSCALL_FAIL(syscall, "failed");		\
		}							\
	} else {							\
		if (succeeded) {					\
			FAILX("%s:\tsucceeded when it shouldn't have"	\
			    " (rights 0x%jx)", #syscall,		\
			    (uintmax_t)rights);				\
		} else if (errno != ENOTCAPABLE) {			\
			SYSCALL_FAIL(syscall, "errno != ENOTCAPABLE");	\
		}							\
	}								\
	errno = 0;							\
} while (0)

/*
 * As above, but for the special mmap() case: unmap after successful mmap().
 */
#define	CHECK_MMAP_RESULT(rights_needed)	do {			\
	if ((rights & (rights_needed)) == (rights_needed)) {		\
		if (p == MAP_FAILED)					\
			SYSCALL_FAIL(mmap, "failed");			\
		else {							\
			(void)munmap(p, getpagesize());			\
			if (success == -1)				\
				success = PASSED;			\
		}							\
	} else {							\
		if (p != MAP_FAILED) {					\
			FAILX("%s:\tsucceeded when it shouldn't have"	\
			    " (rights 0x%jx)", "mmap", rights);		\
			(void)munmap(p, getpagesize());			\
		} else if (errno != ENOTCAPABLE)			\
			SYSCALL_FAIL(syscall, "errno != ENOTCAPABLE");	\
	}								\
	errno = 0;							\
} while (0)

/*
 * Given a file descriptor, create a capability with specific rights and
 * make sure only those rights work. 
*/
static int
try_file_ops(int filefd, int dirfd, cap_rights_t rights)
{
	struct stat sb;
	struct statfs sf;
	cap_rights_t erights;
	int fd_cap, fd_capcap, dfd_cap;
	ssize_t ssize, ssize2;
	off_t off;
	void *p;
	char ch;
	int ret, is_nfs;
	struct pollfd pollfd;
	int success = -1;

	REQUIRE(fstatfs(filefd, &sf));
	is_nfs = (strcmp("nfs", sf.f_fstypename) == 0);

	REQUIRE(fd_cap = cap_new(filefd, rights));
	CHECK(cap_getrights(fd_cap, &erights) == 0);
	CHECK(rights == erights);
	REQUIRE(fd_capcap = cap_new(fd_cap, rights));
	CHECK(cap_getrights(fd_capcap, &erights) == 0);
	CHECK(rights == erights);
	CHECK(fd_capcap != fd_cap);
	REQUIRE(dfd_cap = cap_new(dirfd, rights));
	CHECK(cap_getrights(dfd_cap, &erights) == 0);
	CHECK(rights == erights);

	ssize = read(fd_cap, &ch, sizeof(ch));
	CHECK_RESULT(read, CAP_READ, ssize >= 0);

	ssize = write(fd_cap, &ch, sizeof(ch));
	CHECK_RESULT(write, CAP_WRITE, ssize >= 0);

	off = lseek(fd_cap, 0, SEEK_SET);
	CHECK_RESULT(lseek, CAP_SEEK, off >= 0);

	ssize = pread(fd_cap, &ch, sizeof(ch), 0);
	ssize2 = pread(fd_cap, &ch, sizeof(ch), 0);
	CHECK_RESULT(pread, CAP_PREAD, ssize >= 0);
	CHECK(ssize == ssize2);

	ssize = pwrite(fd_cap, &ch, sizeof(ch), 0);
	CHECK_RESULT(pwrite, CAP_PWRITE, ssize >= 0);

	p = mmap(NULL, getpagesize(), PROT_NONE, MAP_SHARED, fd_cap, 0);
	CHECK_MMAP_RESULT(CAP_MMAP);

	p = mmap(NULL, getpagesize(), PROT_READ, MAP_SHARED, fd_cap, 0);
	CHECK_MMAP_RESULT(CAP_MMAP_R);

	p = mmap(NULL, getpagesize(), PROT_WRITE, MAP_SHARED, fd_cap, 0);
	CHECK_MMAP_RESULT(CAP_MMAP_W);

	p = mmap(NULL, getpagesize(), PROT_EXEC, MAP_SHARED, fd_cap, 0);
	CHECK_MMAP_RESULT(CAP_MMAP_X);

	p = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE, MAP_SHARED,
	    fd_cap, 0);
	CHECK_MMAP_RESULT(CAP_MMAP_RW);

	p = mmap(NULL, getpagesize(), PROT_READ | PROT_EXEC, MAP_SHARED,
	    fd_cap, 0);
	CHECK_MMAP_RESULT(CAP_MMAP_RX);

	p = mmap(NULL, getpagesize(), PROT_EXEC | PROT_WRITE, MAP_SHARED,
	    fd_cap, 0);
	CHECK_MMAP_RESULT(CAP_MMAP_WX);

	p = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE | PROT_EXEC,
	    MAP_SHARED, fd_cap, 0);
	CHECK_MMAP_RESULT(CAP_MMAP_RWX);

	ret = openat(dfd_cap, "cap_create", O_CREAT | O_RDONLY, 0600);
	CHECK_RESULT(openat(O_CREATE | O_RDONLY),
	    CAP_CREATE | CAP_READ | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	CHECK(ret == -1 || unlinkat(dirfd, "cap_create", 0) == 0);
	ret = openat(dfd_cap, "cap_create", O_CREAT | O_WRONLY | O_APPEND,
	    0600);
	CHECK_RESULT(openat(O_CREATE | O_WRONLY | O_APPEND),
	    CAP_CREATE | CAP_WRITE | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	CHECK(ret == -1 || unlinkat(dirfd, "cap_create", 0) == 0);
	ret = openat(dfd_cap, "cap_create", O_CREAT | O_RDWR | O_APPEND, 0600);
	CHECK_RESULT(openat(O_CREATE | O_RDWR | O_APPEND),
	    CAP_CREATE | CAP_READ | CAP_WRITE | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	CHECK(ret == -1 || unlinkat(dirfd, "cap_create", 0) == 0);

	ret = fsync(fd_cap);
	CHECK_RESULT(fsync, CAP_FSYNC, ret == 0);

	ret = openat(dirfd, "cap_fsync", O_CREAT, 0600);
	CHECK(ret >= 0);
	CHECK(close(ret) == 0);
	ret = openat(dfd_cap, "cap_fsync", O_FSYNC | O_RDONLY);
	CHECK_RESULT(openat(O_FSYNC | O_RDONLY),
	    CAP_FSYNC | CAP_READ | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	ret = openat(dfd_cap, "cap_fsync", O_FSYNC | O_WRONLY | O_APPEND);
	CHECK_RESULT(openat(O_FSYNC | O_WRONLY | O_APPEND),
	    CAP_FSYNC | CAP_WRITE | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	ret = openat(dfd_cap, "cap_fsync", O_FSYNC | O_RDWR | O_APPEND);
	CHECK_RESULT(openat(O_FSYNC | O_RDWR | O_APPEND),
	    CAP_FSYNC | CAP_READ | CAP_WRITE | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	ret = openat(dfd_cap, "cap_fsync", O_SYNC | O_RDONLY);
	CHECK_RESULT(openat(O_SYNC | O_RDONLY),
	    CAP_FSYNC | CAP_READ | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	ret = openat(dfd_cap, "cap_fsync", O_SYNC | O_WRONLY | O_APPEND);
	CHECK_RESULT(openat(O_SYNC | O_WRONLY | O_APPEND),
	    CAP_FSYNC | CAP_WRITE | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	ret = openat(dfd_cap, "cap_fsync", O_SYNC | O_RDWR | O_APPEND);
	CHECK_RESULT(openat(O_SYNC | O_RDWR | O_APPEND),
	    CAP_FSYNC | CAP_READ | CAP_WRITE | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	CHECK(unlinkat(dirfd, "cap_fsync", 0) == 0);

	ret = ftruncate(fd_cap, 0);
	CHECK_RESULT(ftruncate, CAP_FTRUNCATE, ret == 0);

	ret = openat(dirfd, "cap_ftruncate", O_CREAT, 0600);
	CHECK(ret >= 0);
	CHECK(close(ret) == 0);
	ret = openat(dfd_cap, "cap_ftruncate", O_TRUNC | O_RDONLY);
	CHECK_RESULT(openat(O_TRUNC | O_RDONLY),
	    CAP_FTRUNCATE | CAP_READ | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	ret = openat(dfd_cap, "cap_ftruncate", O_TRUNC | O_WRONLY);
	CHECK_RESULT(openat(O_TRUNC | O_WRONLY),
	    CAP_FTRUNCATE | CAP_WRITE | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	ret = openat(dfd_cap, "cap_ftruncate", O_TRUNC | O_RDWR);
	CHECK_RESULT(openat(O_TRUNC | O_RDWR),
	    CAP_FTRUNCATE | CAP_READ | CAP_WRITE | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	CHECK(unlinkat(dirfd, "cap_ftruncate", 0) == 0);

	ret = openat(dfd_cap, "cap_create", O_CREAT | O_WRONLY, 0600);
	CHECK_RESULT(openat(O_CREATE | O_WRONLY),
	    CAP_CREATE | CAP_WRITE | CAP_SEEK | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	CHECK(ret == -1 || unlinkat(dirfd, "cap_create", 0) == 0);
	ret = openat(dfd_cap, "cap_create", O_CREAT | O_RDWR, 0600);
	CHECK_RESULT(openat(O_CREATE | O_RDWR),
	    CAP_CREATE | CAP_READ | CAP_WRITE | CAP_SEEK | CAP_LOOKUP,
	    ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	CHECK(ret == -1 || unlinkat(dirfd, "cap_create", 0) == 0);

	ret = openat(dirfd, "cap_fsync", O_CREAT, 0600);
	CHECK(ret >= 0);
	CHECK(close(ret) == 0);
	ret = openat(dfd_cap, "cap_fsync", O_FSYNC | O_WRONLY);
	CHECK_RESULT(openat(O_FSYNC | O_WRONLY),
	    CAP_FSYNC | CAP_WRITE | CAP_SEEK | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	ret = openat(dfd_cap, "cap_fsync", O_FSYNC | O_RDWR);
	CHECK_RESULT(openat(O_FSYNC | O_RDWR),
	    CAP_FSYNC | CAP_READ | CAP_WRITE | CAP_SEEK | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	ret = openat(dfd_cap, "cap_fsync", O_SYNC | O_WRONLY);
	CHECK_RESULT(openat(O_SYNC | O_WRONLY),
	    CAP_FSYNC | CAP_WRITE | CAP_SEEK | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	ret = openat(dfd_cap, "cap_fsync", O_SYNC | O_RDWR);
	CHECK_RESULT(openat(O_SYNC | O_RDWR),
	    CAP_FSYNC | CAP_READ | CAP_WRITE | CAP_SEEK | CAP_LOOKUP, ret >= 0);
	CHECK(ret == -1 || close(ret) == 0);
	CHECK(unlinkat(dirfd, "cap_fsync", 0) == 0);

	/*
	 * Note: this is not expected to work over NFS.
	 */
	ret = fchflags(fd_cap, UF_NODUMP);
	CHECK_RESULT(fchflags, CAP_FCHFLAGS,
	    ret == 0 || (is_nfs && errno == EOPNOTSUPP));

	ret = openat(dirfd, "cap_chflagsat", O_CREAT, 0600);
	CHECK(ret >= 0);
	CHECK(close(ret) == 0);
	ret = chflagsat(dfd_cap, "cap_chflagsat", UF_NODUMP, 0);
	CHECK_RESULT(chflagsat, CAP_CHFLAGSAT | CAP_LOOKUP, ret == 0);
	CHECK(unlinkat(dirfd, "cap_chflagsat", 0) == 0);

	ret = fchown(fd_cap, -1, -1);
	CHECK_RESULT(fchown, CAP_FCHOWN, ret == 0);

	ret = openat(dirfd, "cap_fchownat", O_CREAT, 0600);
	CHECK(ret >= 0);
	CHECK(close(ret) == 0);
	ret = fchownat(dfd_cap, "cap_fchownat", -1, -1, 0);
	CHECK_RESULT(fchownat, CAP_FCHOWN | CAP_LOOKUP, ret == 0);
	CHECK(unlinkat(dirfd, "cap_fchownat", 0) == 0);

	ret = fchmod(fd_cap, 0644);
	CHECK_RESULT(fchmod, CAP_FCHMOD, ret == 0);

	ret = openat(dirfd, "cap_fchmodat", O_CREAT, 0600);
	CHECK(ret >= 0);
	CHECK(close(ret) == 0);
	ret = fchmodat(dfd_cap, "cap_fchmodat", 0600, 0);
	CHECK_RESULT(fchmodat, CAP_FCHMOD | CAP_LOOKUP, ret == 0);
	CHECK(unlinkat(dirfd, "cap_fchmodat", 0) == 0);

	ret = fcntl(fd_cap, F_GETFL);
	CHECK_RESULT(fcntl(F_GETFL), CAP_FCNTL, ret >= 0);
	ret = fcntl(fd_cap, F_SETFL, ret);
	CHECK_RESULT(fcntl(F_SETFL), CAP_FCNTL, ret == 0);

	/* XXX flock */

	ret = fstat(fd_cap, &sb);
	CHECK_RESULT(fstat, CAP_FSTAT, ret == 0);

	ret = openat(dirfd, "cap_fstatat", O_CREAT, 0600);
	CHECK(ret >= 0);
	CHECK(close(ret) == 0);
	ret = fstatat(dfd_cap, "cap_fstatat", &sb, 0);
	CHECK_RESULT(fstatat, CAP_FSTAT | CAP_LOOKUP, ret == 0);
	CHECK(unlinkat(dirfd, "cap_fstatat", 0) == 0);

	ret = fstatfs(fd_cap, &sf);
	CHECK_RESULT(fstatfs, CAP_FSTATFS, ret == 0);

	ret = fpathconf(fd_cap, _PC_NAME_MAX);
	CHECK_RESULT(fpathconf, CAP_FPATHCONF, ret >= 0);

	ret = futimes(fd_cap, NULL);
	CHECK_RESULT(futimes, CAP_FUTIMES, ret == 0);

	ret = openat(dirfd, "cap_futimesat", O_CREAT, 0600);
	CHECK(ret >= 0);
	CHECK(close(ret) == 0);
	ret = futimesat(dfd_cap, "cap_futimesat", NULL);
	CHECK_RESULT(futimesat, CAP_FUTIMES | CAP_LOOKUP, ret == 0);
	CHECK(unlinkat(dirfd, "cap_futimesat", 0) == 0);

	ret = openat(dirfd, "cap_linkat_src", O_CREAT, 0600);
	CHECK(ret >= 0);
	CHECK(close(ret) == 0);
	ret = linkat(dirfd, "cap_linkat_src", dfd_cap, "cap_linkat_dst", 0);
	CHECK_RESULT(linkat, CAP_LINKAT | CAP_LOOKUP, ret == 0);
	CHECK(unlinkat(dirfd, "cap_linkat_src", 0) == 0);
	CHECK(ret == -1 || unlinkat(dirfd, "cap_linkat_dst", 0) == 0);

	ret = mkdirat(dfd_cap, "cap_mkdirat", 0700);
	CHECK_RESULT(mkdirat, CAP_MKDIRAT | CAP_LOOKUP, ret == 0);
	CHECK(ret == -1 || unlinkat(dirfd, "cap_mkdirat", AT_REMOVEDIR) == 0);

	ret = mkfifoat(dfd_cap, "cap_mkfifoat", 0600);
	CHECK_RESULT(mkfifoat, CAP_MKFIFOAT | CAP_LOOKUP, ret == 0);
	CHECK(ret == -1 || unlinkat(dirfd, "cap_mkfifoat", 0) == 0);

	ret = mknodat(dfd_cap, "cap_mknodat", S_IFCHR | 0600, 0);
	CHECK_RESULT(mknodat, CAP_MKNODAT | CAP_LOOKUP, ret == 0);
	CHECK(ret == -1 || unlinkat(dirfd, "cap_mknodat", 0) == 0);

	/* TODO: renameat(2) */

	ret = symlinkat("test", dfd_cap, "cap_symlinkat");
	CHECK_RESULT(symlinkat, CAP_SYMLINKAT | CAP_LOOKUP, ret == 0);
	CHECK(ret == -1 || unlinkat(dirfd, "cap_symlinkat", 0) == 0);

	ret = openat(dirfd, "cap_unlinkat", O_CREAT, 0600);
	CHECK(ret >= 0);
	CHECK(close(ret) == 0);
	ret = unlinkat(dfd_cap, "cap_unlinkat", 0);
	CHECK_RESULT(unlinkat, CAP_UNLINKAT | CAP_LOOKUP, ret == 0);
	CHECK(ret == 0 || unlinkat(dirfd, "cap_unlinkat", 0) == 0);
	ret = mkdirat(dirfd, "cap_unlinkat", 0700);
	CHECK(ret == 0);
	ret = unlinkat(dfd_cap, "cap_unlinkat", AT_REMOVEDIR);
	CHECK_RESULT(unlinkat, CAP_UNLINKAT | CAP_LOOKUP, ret == 0);
	CHECK(ret == 0 || unlinkat(dirfd, "cap_unlinkat", AT_REMOVEDIR) == 0);

	pollfd.fd = fd_cap;
	pollfd.events = POLLIN | POLLERR | POLLHUP;
	pollfd.revents = 0;

	ret = poll(&pollfd, 1, 0);
	if (rights & CAP_EVENT)
		CHECK((pollfd.revents & POLLNVAL) == 0);
	else
		CHECK((pollfd.revents & POLLNVAL) != 0);

	/* XXX: select, kqueue */

	close(fd_cap);
	close(fd_capcap);

	if (success == -1) {
		fprintf(stderr, "No tests for rights 0x%jx.\n",
		    (uintmax_t)rights);
		success = FAILED;
	}
	return (success);
}

#define TRY(rights) \
do { \
	if (success == PASSED) \
		success = try_file_ops(filefd, dirfd, (rights)); \
	else \
		/* We've already failed, but try the test anyway. */ \
		try_file_ops(filefd, dirfd, (rights)); \
} while (0)

#define	KEEP_ERRNO(...)	do {						\
	int _saved_errno = errno;					\
	__VA_ARGS__;							\
	errno = _saved_errno;						\
} while (0);

int
test_capabilities(void)
{
	int filefd, dirfd, tmpfd;
	int success = PASSED;
	char file[] = "/tmp/cap_test.XXXXXXXXXX";
	char dir[] = "/tmp/cap_test.XXXXXXXXXX";

	filefd = mkstemp(file);
	if (filefd < 0)
		err(-1, "mkstemp");
	if (mkdtemp(dir) == NULL) {
		KEEP_ERRNO(unlink(file));
		err(-1, "mkdtemp");
	}
	dirfd = open(dir, O_RDONLY | O_DIRECTORY);
	if (dirfd == -1) {
		KEEP_ERRNO(unlink(file));
		KEEP_ERRNO(rmdir(dir));
		err(-1, "open");
	}
	tmpfd = open("/tmp", O_RDONLY | O_DIRECTORY);
	if (tmpfd == -1) {
		KEEP_ERRNO(unlink(file));
		KEEP_ERRNO(rmdir(dir));
		err(-1, "open");
	}

	if (cap_enter() == -1) {
		KEEP_ERRNO(unlink(file));
		KEEP_ERRNO(rmdir(dir));
		err(-1, "cap_enter");
	}

	TRY(CAP_READ);
	TRY(CAP_WRITE);
	TRY(CAP_SEEK);
	TRY(CAP_PREAD);
	TRY(CAP_PWRITE);
	TRY(CAP_READ | CAP_WRITE);
	TRY(CAP_PREAD | CAP_PWRITE);
	TRY(CAP_MMAP);
	TRY(CAP_MMAP_R);
	TRY(CAP_MMAP_W);
	TRY(CAP_MMAP_X);
	TRY(CAP_MMAP_RW);
	TRY(CAP_MMAP_RX);
	TRY(CAP_MMAP_WX);
	TRY(CAP_MMAP_RWX);
	TRY(CAP_CREATE | CAP_READ | CAP_LOOKUP);
	TRY(CAP_CREATE | CAP_WRITE | CAP_LOOKUP);
	TRY(CAP_CREATE | CAP_READ | CAP_WRITE | CAP_LOOKUP);
#ifdef TODO
	TRY(CAP_FEXECVE);
#endif
	TRY(CAP_FSYNC);
	TRY(CAP_FSYNC | CAP_READ | CAP_LOOKUP);
	TRY(CAP_FSYNC | CAP_WRITE | CAP_LOOKUP);
	TRY(CAP_FSYNC | CAP_READ | CAP_WRITE | CAP_LOOKUP);
	TRY(CAP_FTRUNCATE);
	TRY(CAP_FTRUNCATE | CAP_READ | CAP_LOOKUP);
	TRY(CAP_FTRUNCATE | CAP_WRITE | CAP_LOOKUP);
	TRY(CAP_FTRUNCATE | CAP_READ | CAP_WRITE | CAP_LOOKUP);
#ifdef TODO
	TRY(CAP_FCHDIR);
#endif
	TRY(CAP_FCHFLAGS);
	TRY(CAP_FCHOWN);
	TRY(CAP_FCHOWN | CAP_LOOKUP);
	TRY(CAP_FCHMOD | CAP_LOOKUP);
	TRY(CAP_FCNTL);
#ifdef TODO
	TRY(CAP_FLOCK);
#endif
	TRY(CAP_FPATHCONF);
#ifdef TODO
	TRY(CAP_FSCK);
#endif
	TRY(CAP_FSTAT | CAP_LOOKUP);
	TRY(CAP_FSTATFS);
	TRY(CAP_FUTIMES | CAP_LOOKUP);
	TRY(CAP_LINKAT | CAP_LOOKUP);
	TRY(CAP_MKDIRAT | CAP_LOOKUP);
	TRY(CAP_MKFIFOAT | CAP_LOOKUP);
	TRY(CAP_MKNODAT | CAP_LOOKUP);
	TRY(CAP_SYMLINKAT | CAP_LOOKUP);
	TRY(CAP_UNLINKAT | CAP_LOOKUP);
	/* Rename needs CAP_RENAMEAT on source directory and CAP_LINKAT on destination directory. */
	TRY(CAP_RENAMEAT | CAP_UNLINKAT | CAP_LOOKUP);
#ifdef TODO
	TRY(CAP_LOOKUP);
	TRY(CAP_EXTATTR_DELETE);
	TRY(CAP_EXTATTR_GET);
	TRY(CAP_EXTATTR_LIST);
	TRY(CAP_EXTATTR_SET);
	TRY(CAP_ACL_CHECK);
	TRY(CAP_ACL_DELETE);
	TRY(CAP_ACL_GET);
	TRY(CAP_ACL_SET);
	TRY(CAP_ACCEPT);
	TRY(CAP_BIND);
	TRY(CAP_CONNECT);
	TRY(CAP_GETPEERNAME);
	TRY(CAP_GETSOCKNAME);
	TRY(CAP_GETSOCKOPT);
	TRY(CAP_LISTEN);
	TRY(CAP_PEELOFF);
	TRY(CAP_RECV);
	TRY(CAP_SEND);
	TRY(CAP_SETSOCKOPT);
	TRY(CAP_SHUTDOWN);
	TRY(CAP_MAC_GET);
	TRY(CAP_MAC_SET);
	TRY(CAP_SEM_GETVALUE);
	TRY(CAP_SEM_POST);
	TRY(CAP_SEM_WAIT);
	TRY(CAP_POST_EVENT);
	TRY(CAP_EVENT);
	TRY(CAP_IOCTL);
	TRY(CAP_TTYHOOK);
	TRY(CAP_PDGETPID);
	TRY(CAP_PDWAIT);
	TRY(CAP_PDKILL);
#endif

	(void)unlinkat(tmpfd, file + strlen("/tmp/"), 0);
	(void)unlinkat(tmpfd, dir + strlen("/tmp/"), AT_REMOVEDIR);

	return (success);
}
