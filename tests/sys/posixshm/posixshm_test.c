/*-
 * Copyright (c) 2006 Robert N. M. Watson
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#define	TEST_PATH_LEN	256
static char test_path[TEST_PATH_LEN];

static void
gen_test_path(void)
{

	snprintf(test_path, sizeof(test_path), "%s/tmp.XXXXXX",
	    getenv("TMPDIR") == NULL ? "/tmp" : getenv("TMPDIR"));
	test_path[sizeof(test_path) - 1] = '\0';
	ATF_REQUIRE_MSG(mkstemp(test_path) != -1,
	    "mkstemp failed; errno=%d", errno);
	ATF_REQUIRE_MSG(unlink(test_path) == 0,
	    "unlink failed; errno=%d", errno);
}

/*
 * Attempt a shm_open() that should fail with an expected error of 'error'.
 */
static void
shm_open_should_fail(const char *path, int flags, mode_t mode, int error)
{
	int fd;

	fd = shm_open(path, flags, mode);
	ATF_CHECK_MSG(fd == -1, "shm_open didn't fail");
	ATF_CHECK_MSG(error == errno,
	    "shm_open didn't fail with expected errno; errno=%d; expected "
	    "errno=%d", errno, error);
}

/*
 * Attempt a shm_unlink() that should fail with an expected error of 'error'.
 */
static void
shm_unlink_should_fail(const char *path, int error)
{

	ATF_CHECK_MSG(shm_unlink(path) == -1, "shm_unlink didn't fail");
	ATF_CHECK_MSG(error == errno,
	    "shm_unlink didn't fail with expected errno; errno=%d; expected "
	    "errno=%d", errno, error);
}

/*
 * Open the test object and write '1' to the first byte.  Returns valid fd
 * on success and -1 on failure.
 */
static int
scribble_object(void)
{
	char *page;
	int fd, pagesize;

	gen_test_path();

	ATF_REQUIRE(0 < (pagesize = getpagesize()));

	fd = shm_open(test_path, O_CREAT|O_EXCL|O_RDWR, 0777);
	if (fd < 0 && errno == EEXIST) {
		if (shm_unlink(test_path) < 0)
			atf_tc_fail("shm_unlink");
		fd = shm_open(test_path, O_CREAT | O_EXCL | O_RDWR, 0777);
	}
	if (fd < 0)
		atf_tc_fail("shm_open failed; errno=%d", errno);
	if (ftruncate(fd, pagesize) < 0)
		atf_tc_fail("ftruncate failed; errno=%d", errno);

	page = mmap(0, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (page == MAP_FAILED)
		atf_tc_fail("mmap failed; errno=%d", errno);

	page[0] = '1';
	ATF_REQUIRE_MSG(munmap(page, pagesize) == 0, "munmap failed; errno=%d",
	    errno);

	return (fd);
}

ATF_TC_WITHOUT_HEAD(remap_object);
ATF_TC_BODY(remap_object, tc)
{
	char *page;
	int fd, pagesize;

	ATF_REQUIRE(0 < (pagesize = getpagesize()));

	fd = scribble_object();

	page = mmap(0, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (page == MAP_FAILED)
		atf_tc_fail("mmap(2) failed; errno=%d", errno);

	if (page[0] != '1')
		atf_tc_fail("missing data ('%c' != '1')", page[0]);

	close(fd);
	ATF_REQUIRE_MSG(munmap(page, pagesize) == 0, "munmap failed; errno=%d",
	    errno);

	ATF_REQUIRE_MSG(shm_unlink(test_path) != -1,
	    "shm_unlink failed; errno=%d", errno);
}

ATF_TC_WITHOUT_HEAD(reopen_object);
ATF_TC_BODY(reopen_object, tc)
{
	char *page;
	int fd, pagesize;

	ATF_REQUIRE(0 < (pagesize = getpagesize()));

	fd = scribble_object();
	close(fd);

	fd = shm_open(test_path, O_RDONLY, 0777);
	if (fd < 0)
		atf_tc_fail("shm_open(2) failed; errno=%d", errno);

	page = mmap(0, pagesize, PROT_READ, MAP_SHARED, fd, 0);
	if (page == MAP_FAILED)
		atf_tc_fail("mmap(2) failed; errno=%d", errno);

	if (page[0] != '1')
		atf_tc_fail("missing data ('%c' != '1')", page[0]);

	ATF_REQUIRE_MSG(munmap(page, pagesize) == 0, "munmap failed; errno=%d",
	    errno);
	close(fd);
	ATF_REQUIRE_MSG(shm_unlink(test_path) != -1,
	    "shm_unlink failed; errno=%d", errno);
}

ATF_TC_WITHOUT_HEAD(readonly_mmap_write);
ATF_TC_BODY(readonly_mmap_write, tc)
{
	char *page;
	int fd, pagesize;

	ATF_REQUIRE(0 < (pagesize = getpagesize()));

	gen_test_path();

	fd = shm_open(test_path, O_RDONLY | O_CREAT, 0777);
	ATF_REQUIRE_MSG(fd >= 0, "shm_open failed; errno=%d", errno);

	/* PROT_WRITE should fail with EACCES. */
	page = mmap(0, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (page != MAP_FAILED)
		atf_tc_fail("mmap(PROT_WRITE) succeeded unexpectedly");

	if (errno != EACCES)
		atf_tc_fail("mmap(PROT_WRITE) didn't fail with EACCES; "
		    "errno=%d", errno);

	close(fd);
	ATF_REQUIRE_MSG(shm_unlink(test_path) != -1,
	    "shm_unlink failed; errno=%d", errno);
}

ATF_TC_WITHOUT_HEAD(open_after_link);
ATF_TC_BODY(open_after_link, tc)
{
	int fd;

	gen_test_path();

	fd = shm_open(test_path, O_RDONLY | O_CREAT, 0777);
	ATF_REQUIRE_MSG(fd >= 0, "shm_open(1) failed; errno=%d", errno);
	close(fd);

	ATF_REQUIRE_MSG(shm_unlink(test_path) != -1, "shm_unlink failed: %d",
	    errno);

	shm_open_should_fail(test_path, O_RDONLY, 0777, ENOENT);
}

ATF_TC_WITHOUT_HEAD(open_invalid_path);
ATF_TC_BODY(open_invalid_path, tc)
{

	shm_open_should_fail("blah", O_RDONLY, 0777, EINVAL);
}

ATF_TC_WITHOUT_HEAD(open_write_only);
ATF_TC_BODY(open_write_only, tc)
{

	gen_test_path();

	shm_open_should_fail(test_path, O_WRONLY, 0777, EINVAL);
}

ATF_TC_WITHOUT_HEAD(open_extra_flags);
ATF_TC_BODY(open_extra_flags, tc)
{

	gen_test_path();

	shm_open_should_fail(test_path, O_RDONLY | O_DIRECT, 0777, EINVAL);
}

ATF_TC_WITHOUT_HEAD(open_anon);
ATF_TC_BODY(open_anon, tc)
{
	int fd;

	fd = shm_open(SHM_ANON, O_RDWR, 0777);
	ATF_REQUIRE_MSG(fd >= 0, "shm_open failed; errno=%d", errno);
	close(fd);
}

ATF_TC_WITHOUT_HEAD(open_anon_readonly);
ATF_TC_BODY(open_anon_readonly, tc)
{

	shm_open_should_fail(SHM_ANON, O_RDONLY, 0777, EINVAL);
}

ATF_TC_WITHOUT_HEAD(open_bad_path_pointer);
ATF_TC_BODY(open_bad_path_pointer, tc)
{

	shm_open_should_fail((char *)1024, O_RDONLY, 0777, EFAULT);
}

ATF_TC_WITHOUT_HEAD(open_path_too_long);
ATF_TC_BODY(open_path_too_long, tc)
{
	char *page;

	page = malloc(MAXPATHLEN + 1);
	memset(page, 'a', MAXPATHLEN);
	page[MAXPATHLEN] = '\0';
	shm_open_should_fail(page, O_RDONLY, 0777, ENAMETOOLONG);
	free(page);
}

ATF_TC_WITHOUT_HEAD(open_nonexisting_object);
ATF_TC_BODY(open_nonexisting_object, tc)
{

	shm_open_should_fail("/notreallythere", O_RDONLY, 0777, ENOENT);
}

ATF_TC_WITHOUT_HEAD(open_create_existing_object);
ATF_TC_BODY(open_create_existing_object, tc)
{
	int fd;

	gen_test_path();

	fd = shm_open(test_path, O_RDONLY|O_CREAT, 0777);
	ATF_REQUIRE_MSG(fd >= 0, "shm_open failed; errno=%d", errno);
	close(fd);

	shm_open_should_fail(test_path, O_RDONLY|O_CREAT|O_EXCL,
	    0777, EEXIST);

	ATF_REQUIRE_MSG(shm_unlink(test_path) != -1,
	    "shm_unlink failed; errno=%d", errno);
}

ATF_TC_WITHOUT_HEAD(trunc_resets_object);
ATF_TC_BODY(trunc_resets_object, tc)
{
	struct stat sb;
	int fd;

	gen_test_path();

	/* Create object and set size to 1024. */
	fd = shm_open(test_path, O_RDWR | O_CREAT, 0777);
	ATF_REQUIRE_MSG(fd >= 0, "shm_open(1) failed; errno=%d", errno);
	ATF_REQUIRE_MSG(ftruncate(fd, 1024) != -1,
	    "ftruncate failed; errno=%d", errno);
	ATF_REQUIRE_MSG(fstat(fd, &sb) != -1,
	    "fstat(1) failed; errno=%d", errno);
	ATF_REQUIRE_MSG(sb.st_size == 1024, "size %d != 1024", (int)sb.st_size);
	close(fd);

	/* Open with O_TRUNC which should reset size to 0. */
	fd = shm_open(test_path, O_RDWR | O_TRUNC, 0777);
	ATF_REQUIRE_MSG(fd >= 0, "shm_open(2) failed; errno=%d", errno);
	ATF_REQUIRE_MSG(fstat(fd, &sb) != -1,
	    "fstat(2) failed; errno=%d", errno);
	ATF_REQUIRE_MSG(sb.st_size == 0,
	    "size was not 0 after truncation: %d", (int)sb.st_size);
	close(fd);
	ATF_REQUIRE_MSG(shm_unlink(test_path) != -1,
	    "shm_unlink failed; errno=%d", errno);
}

ATF_TC_WITHOUT_HEAD(unlink_bad_path_pointer);
ATF_TC_BODY(unlink_bad_path_pointer, tc)
{

	shm_unlink_should_fail((char *)1024, EFAULT);
}

ATF_TC_WITHOUT_HEAD(unlink_path_too_long);
ATF_TC_BODY(unlink_path_too_long, tc)
{
	char *page;

	page = malloc(MAXPATHLEN + 1);
	memset(page, 'a', MAXPATHLEN);
	page[MAXPATHLEN] = '\0';
	shm_unlink_should_fail(page, ENAMETOOLONG);
	free(page);
}

ATF_TC_WITHOUT_HEAD(object_resize);
ATF_TC_BODY(object_resize, tc)
{
	pid_t pid;
	struct stat sb;
	char *page;
	int fd, pagesize, status;

	ATF_REQUIRE(0 < (pagesize = getpagesize()));

	/* Start off with a size of a single page. */
	fd = shm_open(SHM_ANON, O_CREAT|O_RDWR, 0777);
	if (fd < 0)
		atf_tc_fail("shm_open failed; errno=%d", errno);

	if (ftruncate(fd, pagesize) < 0)
		atf_tc_fail("ftruncate(1) failed; errno=%d", errno);

	if (fstat(fd, &sb) < 0)
		atf_tc_fail("fstat(1) failed; errno=%d", errno);

	if (sb.st_size != pagesize)
		atf_tc_fail("first resize failed (%d != %d)",
		    (int)sb.st_size, pagesize);

	/* Write a '1' to the first byte. */
	page = mmap(0, pagesize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (page == MAP_FAILED)
		atf_tc_fail("mmap(1)");

	page[0] = '1';

	ATF_REQUIRE_MSG(munmap(page, pagesize) == 0, "munmap failed; errno=%d",
	    errno);

	/* Grow the object to 2 pages. */
	if (ftruncate(fd, pagesize * 2) < 0)
		atf_tc_fail("ftruncate(2) failed; errno=%d", errno);

	if (fstat(fd, &sb) < 0)
		atf_tc_fail("fstat(2) failed; errno=%d", errno);

	if (sb.st_size != pagesize * 2)
		atf_tc_fail("second resize failed (%d != %d)",
		    (int)sb.st_size, pagesize * 2);

	/* Check for '1' at the first byte. */
	page = mmap(0, pagesize * 2, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (page == MAP_FAILED)
		atf_tc_fail("mmap(2) failed; errno=%d", errno);

	if (page[0] != '1')
		atf_tc_fail("'%c' != '1'", page[0]);

	/* Write a '2' at the start of the second page. */
	page[pagesize] = '2';

	/* Shrink the object back to 1 page. */
	if (ftruncate(fd, pagesize) < 0)
		atf_tc_fail("ftruncate(3) failed; errno=%d", errno);

	if (fstat(fd, &sb) < 0)
		atf_tc_fail("fstat(3) failed; errno=%d", errno);

	if (sb.st_size != pagesize)
		atf_tc_fail("third resize failed (%d != %d)",
		    (int)sb.st_size, pagesize);

	/*
	 * Fork a child process to make sure the second page is no
	 * longer valid.
	 */
	pid = fork();
	if (pid == -1)
		atf_tc_fail("fork failed; errno=%d", errno);

	if (pid == 0) {
		struct rlimit lim;
		char c;

		/* Don't generate a core dump. */
		ATF_REQUIRE(getrlimit(RLIMIT_CORE, &lim) == 0);
		lim.rlim_cur = 0;
		ATF_REQUIRE(setrlimit(RLIMIT_CORE, &lim) == 0);

		/*
		 * The previous ftruncate(2) shrunk the backing object
		 * so that this address is no longer valid, so reading
		 * from it should trigger a SIGSEGV.
		 */
		c = page[pagesize];
		fprintf(stderr, "child: page 1: '%c'\n", c);
		exit(0);
	}

	if (wait(&status) < 0)
		atf_tc_fail("wait failed; errno=%d", errno);

	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGSEGV)
		atf_tc_fail("child terminated with status %x", status);

	/* Grow the object back to 2 pages. */
	if (ftruncate(fd, pagesize * 2) < 0)
		atf_tc_fail("ftruncate(2) failed; errno=%d", errno);

	if (fstat(fd, &sb) < 0)
		atf_tc_fail("fstat(2) failed; errno=%d", errno);

	if (sb.st_size != pagesize * 2)
		atf_tc_fail("fourth resize failed (%d != %d)",
		    (int)sb.st_size, pagesize);

	/*
	 * Note that the mapping at 'page' for the second page is
	 * still valid, and now that the shm object has been grown
	 * back up to 2 pages, there is now memory backing this page
	 * so the read will work.  However, the data should be zero
	 * rather than '2' as the old data was thrown away when the
	 * object was shrunk and the new pages when an object are
	 * grown are zero-filled.
	 */
	if (page[pagesize] != 0)
		atf_tc_fail("invalid data at %d: %x != 0",
		    pagesize, (int)page[pagesize]);

	close(fd);
}

/* Signal handler which does nothing. */
static void
ignoreit(int sig __unused)
{
	;
}

ATF_TC_WITHOUT_HEAD(shm_functionality_across_fork);
ATF_TC_BODY(shm_functionality_across_fork, tc)
{
	char *cp, c;
	int error, desc, rv;
	long scval;
	sigset_t ss;
	struct sigaction sa;
	void *region;
	size_t i, psize;

#ifndef _POSIX_SHARED_MEMORY_OBJECTS
	printf("_POSIX_SHARED_MEMORY_OBJECTS is undefined\n");
#else
	printf("_POSIX_SHARED_MEMORY_OBJECTS is defined as %ld\n", 
	       (long)_POSIX_SHARED_MEMORY_OBJECTS - 0);
	if (_POSIX_SHARED_MEMORY_OBJECTS - 0 == -1)
		printf("***Indicates this feature may be unsupported!\n");
#endif
	errno = 0;
	scval = sysconf(_SC_SHARED_MEMORY_OBJECTS);
	if (scval == -1 && errno != 0) {
		atf_tc_fail("sysconf(_SC_SHARED_MEMORY_OBJECTS) failed; "
		    "errno=%d", errno);
	} else {
		printf("sysconf(_SC_SHARED_MEMORY_OBJECTS) returns %ld\n",
		       scval);
		if (scval == -1)
			printf("***Indicates this feature is unsupported!\n");
	}

	errno = 0;
	scval = sysconf(_SC_PAGESIZE);
	if (scval == -1 && errno != 0) {
		atf_tc_fail("sysconf(_SC_PAGESIZE) failed; errno=%d", errno);
	} else if (scval <= 0) {
		fprintf(stderr, "bogus return from sysconf(_SC_PAGESIZE): %ld",
		    scval);
		psize = 4096;
	} else {
		printf("sysconf(_SC_PAGESIZE) returns %ld\n", scval);
		psize = scval;
	}

	gen_test_path();
	desc = shm_open(test_path, O_EXCL | O_CREAT | O_RDWR, 0600);

	ATF_REQUIRE_MSG(desc >= 0, "shm_open failed; errno=%d", errno);
	ATF_REQUIRE_MSG(shm_unlink(test_path) == 0,
	    "shm_unlink failed; errno=%d", errno);
	ATF_REQUIRE_MSG(ftruncate(desc, (off_t)psize) != -1,
	    "ftruncate failed; errno=%d", errno);

	region = mmap(NULL, psize, PROT_READ | PROT_WRITE, MAP_SHARED, desc, 0);
	ATF_REQUIRE_MSG(region != MAP_FAILED, "mmap failed; errno=%d", errno);
	memset(region, '\377', psize);

	sa.sa_flags = 0;
	sa.sa_handler = ignoreit;
	sigemptyset(&sa.sa_mask);
	ATF_REQUIRE_MSG(sigaction(SIGUSR1, &sa, (struct sigaction *)0) == 0,
	    "sigaction failed; errno=%d", errno);

	sigemptyset(&ss);
	sigaddset(&ss, SIGUSR1);
	ATF_REQUIRE_MSG(sigprocmask(SIG_BLOCK, &ss, (sigset_t *)0) == 0,
	    "sigprocmask failed; errno=%d", errno);

	rv = fork();
	ATF_REQUIRE_MSG(rv != -1, "fork failed; errno=%d", errno);
	if (rv == 0) {
		sigemptyset(&ss);
		sigsuspend(&ss);

		for (cp = region; cp < (char *)region + psize; cp++) {
			if (*cp != '\151')
				_exit(1);
		}
		if (lseek(desc, 0, SEEK_SET) == -1)
			_exit(1);
		for (i = 0; i < psize; i++) {
			error = read(desc, &c, 1);
			if (c != '\151')
				_exit(1);
		}
		_exit(0);
	} else {
		int status;

		memset(region, '\151', psize - 2);
		error = pwrite(desc, region, 2, psize - 2);
		if (error != 2) {
			if (error >= 0)
				atf_tc_fail("short write; %d bytes written",
				    error);
			else
				atf_tc_fail("shmfd write");
		}
		kill(rv, SIGUSR1);
		waitpid(rv, &status, 0);

		if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
			printf("Functionality test successful\n");
		} else if (WIFEXITED(status)) {
			atf_tc_fail("Child process exited with status %d",
			    WEXITSTATUS(status));
		} else {
			atf_tc_fail("Child process terminated with %s",
			    strsignal(WTERMSIG(status)));
		}
	}

	ATF_REQUIRE_MSG(munmap(region, psize) == 0, "munmap failed; errno=%d",
	    errno);
	shm_unlink(test_path);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, remap_object);
	ATF_TP_ADD_TC(tp, reopen_object);
	ATF_TP_ADD_TC(tp, readonly_mmap_write);
	ATF_TP_ADD_TC(tp, open_after_link);
	ATF_TP_ADD_TC(tp, open_invalid_path);
	ATF_TP_ADD_TC(tp, open_write_only);
	ATF_TP_ADD_TC(tp, open_extra_flags);
	ATF_TP_ADD_TC(tp, open_anon);
	ATF_TP_ADD_TC(tp, open_anon_readonly);
	ATF_TP_ADD_TC(tp, open_bad_path_pointer);
	ATF_TP_ADD_TC(tp, open_path_too_long);
	ATF_TP_ADD_TC(tp, open_nonexisting_object);
	ATF_TP_ADD_TC(tp, open_create_existing_object);
	ATF_TP_ADD_TC(tp, shm_functionality_across_fork);
	ATF_TP_ADD_TC(tp, trunc_resets_object);
	ATF_TP_ADD_TC(tp, unlink_bad_path_pointer);
	ATF_TP_ADD_TC(tp, unlink_path_too_long);
	ATF_TP_ADD_TC(tp, object_resize);

	return (atf_no_error());
}
