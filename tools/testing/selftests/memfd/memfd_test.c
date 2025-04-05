// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#define __EXPORTED_HEADERS__

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/falloc.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ctype.h>

#include "common.h"

#define MEMFD_STR	"memfd:"
#define MEMFD_HUGE_STR	"memfd-hugetlb:"
#define SHARED_FT_STR	"(shared file-table)"

#define MFD_DEF_SIZE 8192
#define STACK_SIZE 65536

#define F_SEAL_EXEC	0x0020

#define F_WX_SEALS (F_SEAL_SHRINK | \
		    F_SEAL_GROW | \
		    F_SEAL_WRITE | \
		    F_SEAL_FUTURE_WRITE | \
		    F_SEAL_EXEC)

#define MFD_NOEXEC_SEAL	0x0008U

/*
 * Default is not to test hugetlbfs
 */
static size_t mfd_def_size = MFD_DEF_SIZE;
static const char *memfd_str = MEMFD_STR;

static ssize_t fd2name(int fd, char *buf, size_t bufsize)
{
	char buf1[PATH_MAX];
	int size;
	ssize_t nbytes;

	size = snprintf(buf1, PATH_MAX, "/proc/self/fd/%d", fd);
	if (size < 0) {
		printf("snprintf(%d) failed on %m\n", fd);
		abort();
	}

	/*
	 * reserver one byte for string termination.
	 */
	nbytes = readlink(buf1, buf, bufsize-1);
	if (nbytes == -1) {
		printf("readlink(%s) failed %m\n", buf1);
		abort();
	}
	buf[nbytes] = '\0';
	return nbytes;
}

static int mfd_assert_new(const char *name, loff_t sz, unsigned int flags)
{
	int r, fd;

	fd = sys_memfd_create(name, flags);
	if (fd < 0) {
		printf("memfd_create(\"%s\", %u) failed: %m\n",
		       name, flags);
		abort();
	}

	r = ftruncate(fd, sz);
	if (r < 0) {
		printf("ftruncate(%llu) failed: %m\n", (unsigned long long)sz);
		abort();
	}

	return fd;
}

static void sysctl_assert_write(const char *val)
{
	int fd = open("/proc/sys/vm/memfd_noexec", O_WRONLY | O_CLOEXEC);

	if (fd < 0) {
		printf("open sysctl failed: %m\n");
		abort();
	}

	if (write(fd, val, strlen(val)) < 0) {
		printf("write sysctl %s failed: %m\n", val);
		abort();
	}
}

static void sysctl_fail_write(const char *val)
{
	int fd = open("/proc/sys/vm/memfd_noexec", O_WRONLY | O_CLOEXEC);

	if (fd < 0) {
		printf("open sysctl failed: %m\n");
		abort();
	}

	if (write(fd, val, strlen(val)) >= 0) {
		printf("write sysctl %s succeeded, but failure expected\n",
				val);
		abort();
	}
}

static void sysctl_assert_equal(const char *val)
{
	char *p, buf[128] = {};
	int fd = open("/proc/sys/vm/memfd_noexec", O_RDONLY | O_CLOEXEC);

	if (fd < 0) {
		printf("open sysctl failed: %m\n");
		abort();
	}

	if (read(fd, buf, sizeof(buf)) < 0) {
		printf("read sysctl failed: %m\n");
		abort();
	}

	/* Strip trailing whitespace. */
	p = buf;
	while (!isspace(*p))
		p++;
	*p = '\0';

	if (strcmp(buf, val) != 0) {
		printf("unexpected sysctl value: expected %s, got %s\n", val, buf);
		abort();
	}
}

static int mfd_assert_reopen_fd(int fd_in)
{
	int fd;
	char path[100];

	sprintf(path, "/proc/self/fd/%d", fd_in);

	fd = open(path, O_RDWR);
	if (fd < 0) {
		printf("re-open of existing fd %d failed\n", fd_in);
		abort();
	}

	return fd;
}

static void mfd_fail_new(const char *name, unsigned int flags)
{
	int r;

	r = sys_memfd_create(name, flags);
	if (r >= 0) {
		printf("memfd_create(\"%s\", %u) succeeded, but failure expected\n",
		       name ? name : "NULL", flags);
		close(r);
		abort();
	}
}

static unsigned int mfd_assert_get_seals(int fd)
{
	int r;

	r = fcntl(fd, F_GET_SEALS);
	if (r < 0) {
		printf("GET_SEALS(%d) failed: %m\n", fd);
		abort();
	}

	return (unsigned int)r;
}

static void mfd_assert_has_seals(int fd, unsigned int seals)
{
	char buf[PATH_MAX];
	unsigned int s;
	fd2name(fd, buf, PATH_MAX);

	s = mfd_assert_get_seals(fd);
	if (s != seals) {
		printf("%u != %u = GET_SEALS(%s)\n", seals, s, buf);
		abort();
	}
}

static void mfd_assert_add_seals(int fd, unsigned int seals)
{
	int r;
	unsigned int s;

	s = mfd_assert_get_seals(fd);
	r = fcntl(fd, F_ADD_SEALS, seals);
	if (r < 0) {
		printf("ADD_SEALS(%d, %u -> %u) failed: %m\n", fd, s, seals);
		abort();
	}
}

static void mfd_fail_add_seals(int fd, unsigned int seals)
{
	int r;
	unsigned int s;

	r = fcntl(fd, F_GET_SEALS);
	if (r < 0)
		s = 0;
	else
		s = (unsigned int)r;

	r = fcntl(fd, F_ADD_SEALS, seals);
	if (r >= 0) {
		printf("ADD_SEALS(%d, %u -> %u) didn't fail as expected\n",
				fd, s, seals);
		abort();
	}
}

static void mfd_assert_size(int fd, size_t size)
{
	struct stat st;
	int r;

	r = fstat(fd, &st);
	if (r < 0) {
		printf("fstat(%d) failed: %m\n", fd);
		abort();
	} else if (st.st_size != size) {
		printf("wrong file size %lld, but expected %lld\n",
		       (long long)st.st_size, (long long)size);
		abort();
	}
}

static int mfd_assert_dup(int fd)
{
	int r;

	r = dup(fd);
	if (r < 0) {
		printf("dup(%d) failed: %m\n", fd);
		abort();
	}

	return r;
}

static void *mfd_assert_mmap_shared(int fd)
{
	void *p;

	p = mmap(NULL,
		 mfd_def_size,
		 PROT_READ | PROT_WRITE,
		 MAP_SHARED,
		 fd,
		 0);
	if (p == MAP_FAILED) {
		printf("mmap() failed: %m\n");
		abort();
	}

	return p;
}

static void *mfd_assert_mmap_read_shared(int fd)
{
	void *p;

	p = mmap(NULL,
		 mfd_def_size,
		 PROT_READ,
		 MAP_SHARED,
		 fd,
		 0);
	if (p == MAP_FAILED) {
		printf("mmap() failed: %m\n");
		abort();
	}

	return p;
}

static void *mfd_assert_mmap_private(int fd)
{
	void *p;

	p = mmap(NULL,
		 mfd_def_size,
		 PROT_READ,
		 MAP_PRIVATE,
		 fd,
		 0);
	if (p == MAP_FAILED) {
		printf("mmap() failed: %m\n");
		abort();
	}

	return p;
}

static int mfd_assert_open(int fd, int flags, mode_t mode)
{
	char buf[512];
	int r;

	sprintf(buf, "/proc/self/fd/%d", fd);
	r = open(buf, flags, mode);
	if (r < 0) {
		printf("open(%s) failed: %m\n", buf);
		abort();
	}

	return r;
}

static void mfd_fail_open(int fd, int flags, mode_t mode)
{
	char buf[512];
	int r;

	sprintf(buf, "/proc/self/fd/%d", fd);
	r = open(buf, flags, mode);
	if (r >= 0) {
		printf("open(%s) didn't fail as expected\n", buf);
		abort();
	}
}

static void mfd_assert_read(int fd)
{
	char buf[16];
	void *p;
	ssize_t l;

	l = read(fd, buf, sizeof(buf));
	if (l != sizeof(buf)) {
		printf("read() failed: %m\n");
		abort();
	}

	/* verify PROT_READ *is* allowed */
	p = mmap(NULL,
		 mfd_def_size,
		 PROT_READ,
		 MAP_PRIVATE,
		 fd,
		 0);
	if (p == MAP_FAILED) {
		printf("mmap() failed: %m\n");
		abort();
	}
	munmap(p, mfd_def_size);

	/* verify MAP_PRIVATE is *always* allowed (even writable) */
	p = mmap(NULL,
		 mfd_def_size,
		 PROT_READ | PROT_WRITE,
		 MAP_PRIVATE,
		 fd,
		 0);
	if (p == MAP_FAILED) {
		printf("mmap() failed: %m\n");
		abort();
	}
	munmap(p, mfd_def_size);
}

/* Test that PROT_READ + MAP_SHARED mappings work. */
static void mfd_assert_read_shared(int fd)
{
	void *p;

	/* verify PROT_READ and MAP_SHARED *is* allowed */
	p = mmap(NULL,
		 mfd_def_size,
		 PROT_READ,
		 MAP_SHARED,
		 fd,
		 0);
	if (p == MAP_FAILED) {
		printf("mmap() failed: %m\n");
		abort();
	}
	munmap(p, mfd_def_size);
}

static void mfd_assert_fork_private_write(int fd)
{
	int *p;
	pid_t pid;

	p = mmap(NULL,
		 mfd_def_size,
		 PROT_READ | PROT_WRITE,
		 MAP_PRIVATE,
		 fd,
		 0);
	if (p == MAP_FAILED) {
		printf("mmap() failed: %m\n");
		abort();
	}

	p[0] = 22;

	pid = fork();
	if (pid == 0) {
		p[0] = 33;
		exit(0);
	} else {
		waitpid(pid, NULL, 0);

		if (p[0] != 22) {
			printf("MAP_PRIVATE copy-on-write failed: %m\n");
			abort();
		}
	}

	munmap(p, mfd_def_size);
}

static void mfd_assert_write(int fd)
{
	ssize_t l;
	void *p;
	int r;

	/*
	 * huegtlbfs does not support write, but we want to
	 * verify everything else here.
	 */
	if (!hugetlbfs_test) {
		/* verify write() succeeds */
		l = write(fd, "\0\0\0\0", 4);
		if (l != 4) {
			printf("write() failed: %m\n");
			abort();
		}
	}

	/* verify PROT_READ | PROT_WRITE is allowed */
	p = mmap(NULL,
		 mfd_def_size,
		 PROT_READ | PROT_WRITE,
		 MAP_SHARED,
		 fd,
		 0);
	if (p == MAP_FAILED) {
		printf("mmap() failed: %m\n");
		abort();
	}
	*(char *)p = 0;
	munmap(p, mfd_def_size);

	/* verify PROT_WRITE is allowed */
	p = mmap(NULL,
		 mfd_def_size,
		 PROT_WRITE,
		 MAP_SHARED,
		 fd,
		 0);
	if (p == MAP_FAILED) {
		printf("mmap() failed: %m\n");
		abort();
	}
	*(char *)p = 0;
	munmap(p, mfd_def_size);

	/* verify PROT_READ with MAP_SHARED is allowed and a following
	 * mprotect(PROT_WRITE) allows writing */
	p = mmap(NULL,
		 mfd_def_size,
		 PROT_READ,
		 MAP_SHARED,
		 fd,
		 0);
	if (p == MAP_FAILED) {
		printf("mmap() failed: %m\n");
		abort();
	}

	r = mprotect(p, mfd_def_size, PROT_READ | PROT_WRITE);
	if (r < 0) {
		printf("mprotect() failed: %m\n");
		abort();
	}

	*(char *)p = 0;
	munmap(p, mfd_def_size);

	/* verify PUNCH_HOLE works */
	r = fallocate(fd,
		      FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
		      0,
		      mfd_def_size);
	if (r < 0) {
		printf("fallocate(PUNCH_HOLE) failed: %m\n");
		abort();
	}
}

static void mfd_fail_write(int fd)
{
	ssize_t l;
	void *p;
	int r;

	/* verify write() fails */
	l = write(fd, "data", 4);
	if (l != -EPERM) {
		printf("expected EPERM on write(), but got %d: %m\n", (int)l);
		abort();
	}

	/* verify PROT_READ | PROT_WRITE is not allowed */
	p = mmap(NULL,
		 mfd_def_size,
		 PROT_READ | PROT_WRITE,
		 MAP_SHARED,
		 fd,
		 0);
	if (p != MAP_FAILED) {
		printf("mmap() didn't fail as expected\n");
		abort();
	}

	/* verify PROT_WRITE is not allowed */
	p = mmap(NULL,
		 mfd_def_size,
		 PROT_WRITE,
		 MAP_SHARED,
		 fd,
		 0);
	if (p != MAP_FAILED) {
		printf("mmap() didn't fail as expected\n");
		abort();
	}

	/* Verify PROT_READ with MAP_SHARED with a following mprotect is not
	 * allowed. Note that for r/w the kernel already prevents the mmap. */
	p = mmap(NULL,
		 mfd_def_size,
		 PROT_READ,
		 MAP_SHARED,
		 fd,
		 0);
	if (p != MAP_FAILED) {
		r = mprotect(p, mfd_def_size, PROT_READ | PROT_WRITE);
		if (r >= 0) {
			printf("mmap()+mprotect() didn't fail as expected\n");
			abort();
		}
		munmap(p, mfd_def_size);
	}

	/* verify PUNCH_HOLE fails */
	r = fallocate(fd,
		      FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
		      0,
		      mfd_def_size);
	if (r >= 0) {
		printf("fallocate(PUNCH_HOLE) didn't fail as expected\n");
		abort();
	}
}

static void mfd_assert_shrink(int fd)
{
	int r, fd2;

	r = ftruncate(fd, mfd_def_size / 2);
	if (r < 0) {
		printf("ftruncate(SHRINK) failed: %m\n");
		abort();
	}

	mfd_assert_size(fd, mfd_def_size / 2);

	fd2 = mfd_assert_open(fd,
			      O_RDWR | O_CREAT | O_TRUNC,
			      S_IRUSR | S_IWUSR);
	close(fd2);

	mfd_assert_size(fd, 0);
}

static void mfd_fail_shrink(int fd)
{
	int r;

	r = ftruncate(fd, mfd_def_size / 2);
	if (r >= 0) {
		printf("ftruncate(SHRINK) didn't fail as expected\n");
		abort();
	}

	mfd_fail_open(fd,
		      O_RDWR | O_CREAT | O_TRUNC,
		      S_IRUSR | S_IWUSR);
}

static void mfd_assert_grow(int fd)
{
	int r;

	r = ftruncate(fd, mfd_def_size * 2);
	if (r < 0) {
		printf("ftruncate(GROW) failed: %m\n");
		abort();
	}

	mfd_assert_size(fd, mfd_def_size * 2);

	r = fallocate(fd,
		      0,
		      0,
		      mfd_def_size * 4);
	if (r < 0) {
		printf("fallocate(ALLOC) failed: %m\n");
		abort();
	}

	mfd_assert_size(fd, mfd_def_size * 4);
}

static void mfd_fail_grow(int fd)
{
	int r;

	r = ftruncate(fd, mfd_def_size * 2);
	if (r >= 0) {
		printf("ftruncate(GROW) didn't fail as expected\n");
		abort();
	}

	r = fallocate(fd,
		      0,
		      0,
		      mfd_def_size * 4);
	if (r >= 0) {
		printf("fallocate(ALLOC) didn't fail as expected\n");
		abort();
	}
}

static void mfd_assert_grow_write(int fd)
{
	static char *buf;
	ssize_t l;

	/* hugetlbfs does not support write */
	if (hugetlbfs_test)
		return;

	buf = malloc(mfd_def_size * 8);
	if (!buf) {
		printf("malloc(%zu) failed: %m\n", mfd_def_size * 8);
		abort();
	}

	l = pwrite(fd, buf, mfd_def_size * 8, 0);
	if (l != (mfd_def_size * 8)) {
		printf("pwrite() failed: %m\n");
		abort();
	}

	mfd_assert_size(fd, mfd_def_size * 8);
}

static void mfd_fail_grow_write(int fd)
{
	static char *buf;
	ssize_t l;

	/* hugetlbfs does not support write */
	if (hugetlbfs_test)
		return;

	buf = malloc(mfd_def_size * 8);
	if (!buf) {
		printf("malloc(%zu) failed: %m\n", mfd_def_size * 8);
		abort();
	}

	l = pwrite(fd, buf, mfd_def_size * 8, 0);
	if (l == (mfd_def_size * 8)) {
		printf("pwrite() didn't fail as expected\n");
		abort();
	}
}

static void mfd_assert_mode(int fd, int mode)
{
	struct stat st;
	char buf[PATH_MAX];

	fd2name(fd, buf, PATH_MAX);

	if (fstat(fd, &st) < 0) {
		printf("fstat(%s) failed: %m\n", buf);
		abort();
	}

	if ((st.st_mode & 07777) != mode) {
		printf("fstat(%s) wrong file mode 0%04o, but expected 0%04o\n",
		       buf, (int)st.st_mode & 07777, mode);
		abort();
	}
}

static void mfd_assert_chmod(int fd, int mode)
{
	char buf[PATH_MAX];

	fd2name(fd, buf, PATH_MAX);

	if (fchmod(fd, mode) < 0) {
		printf("fchmod(%s, 0%04o) failed: %m\n", buf, mode);
		abort();
	}

	mfd_assert_mode(fd, mode);
}

static void mfd_fail_chmod(int fd, int mode)
{
	struct stat st;
	char buf[PATH_MAX];

	fd2name(fd, buf, PATH_MAX);

	if (fstat(fd, &st) < 0) {
		printf("fstat(%s) failed: %m\n", buf);
		abort();
	}

	if (fchmod(fd, mode) == 0) {
		printf("fchmod(%s, 0%04o) didn't fail as expected\n",
		       buf, mode);
		abort();
	}

	/* verify that file mode bits did not change */
	mfd_assert_mode(fd, st.st_mode & 07777);
}

static int idle_thread_fn(void *arg)
{
	sigset_t set;
	int sig;

	/* dummy waiter; SIGTERM terminates us anyway */
	sigemptyset(&set);
	sigaddset(&set, SIGTERM);
	sigwait(&set, &sig);

	return 0;
}

static pid_t spawn_thread(unsigned int flags, int (*fn)(void *), void *arg)
{
	uint8_t *stack;
	pid_t pid;

	stack = malloc(STACK_SIZE);
	if (!stack) {
		printf("malloc(STACK_SIZE) failed: %m\n");
		abort();
	}

	pid = clone(fn, stack + STACK_SIZE, SIGCHLD | flags, arg);
	if (pid < 0) {
		printf("clone() failed: %m\n");
		abort();
	}

	return pid;
}

static void join_thread(pid_t pid)
{
	int wstatus;

	if (waitpid(pid, &wstatus, 0) < 0) {
		printf("newpid thread: waitpid() failed: %m\n");
		abort();
	}

	if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != 0) {
		printf("newpid thread: exited with non-zero error code %d\n",
		       WEXITSTATUS(wstatus));
		abort();
	}

	if (WIFSIGNALED(wstatus)) {
		printf("newpid thread: killed by signal %d\n",
		       WTERMSIG(wstatus));
		abort();
	}
}

static pid_t spawn_idle_thread(unsigned int flags)
{
	return spawn_thread(flags, idle_thread_fn, NULL);
}

static void join_idle_thread(pid_t pid)
{
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
}

/*
 * Test memfd_create() syscall
 * Verify syscall-argument validation, including name checks, flag validation
 * and more.
 */
static void test_create(void)
{
	char buf[2048];
	int fd;

	printf("%s CREATE\n", memfd_str);

	/* test NULL name */
	mfd_fail_new(NULL, 0);

	/* test over-long name (not zero-terminated) */
	memset(buf, 0xff, sizeof(buf));
	mfd_fail_new(buf, 0);

	/* test over-long zero-terminated name */
	memset(buf, 0xff, sizeof(buf));
	buf[sizeof(buf) - 1] = 0;
	mfd_fail_new(buf, 0);

	/* verify "" is a valid name */
	fd = mfd_assert_new("", 0, 0);
	close(fd);

	/* verify invalid O_* open flags */
	mfd_fail_new("", 0x0100);
	mfd_fail_new("", ~MFD_CLOEXEC);
	mfd_fail_new("", ~MFD_ALLOW_SEALING);
	mfd_fail_new("", ~0);
	mfd_fail_new("", 0x80000000U);

	/* verify EXEC and NOEXEC_SEAL can't both be set */
	mfd_fail_new("", MFD_EXEC | MFD_NOEXEC_SEAL);

	/* verify MFD_CLOEXEC is allowed */
	fd = mfd_assert_new("", 0, MFD_CLOEXEC);
	close(fd);

	/* verify MFD_ALLOW_SEALING is allowed */
	fd = mfd_assert_new("", 0, MFD_ALLOW_SEALING);
	close(fd);

	/* verify MFD_ALLOW_SEALING | MFD_CLOEXEC is allowed */
	fd = mfd_assert_new("", 0, MFD_ALLOW_SEALING | MFD_CLOEXEC);
	close(fd);
}

/*
 * Test basic sealing
 * A very basic sealing test to see whether setting/retrieving seals works.
 */
static void test_basic(void)
{
	int fd;

	printf("%s BASIC\n", memfd_str);

	fd = mfd_assert_new("kern_memfd_basic",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING);

	/* add basic seals */
	mfd_assert_has_seals(fd, 0);
	mfd_assert_add_seals(fd, F_SEAL_SHRINK |
				 F_SEAL_WRITE);
	mfd_assert_has_seals(fd, F_SEAL_SHRINK |
				 F_SEAL_WRITE);

	/* add them again */
	mfd_assert_add_seals(fd, F_SEAL_SHRINK |
				 F_SEAL_WRITE);
	mfd_assert_has_seals(fd, F_SEAL_SHRINK |
				 F_SEAL_WRITE);

	/* add more seals and seal against sealing */
	mfd_assert_add_seals(fd, F_SEAL_GROW | F_SEAL_SEAL);
	mfd_assert_has_seals(fd, F_SEAL_SHRINK |
				 F_SEAL_GROW |
				 F_SEAL_WRITE |
				 F_SEAL_SEAL);

	/* verify that sealing no longer works */
	mfd_fail_add_seals(fd, F_SEAL_GROW);
	mfd_fail_add_seals(fd, 0);

	close(fd);

	/* verify sealing does not work without MFD_ALLOW_SEALING */
	fd = mfd_assert_new("kern_memfd_basic",
			    mfd_def_size,
			    MFD_CLOEXEC);
	mfd_assert_has_seals(fd, F_SEAL_SEAL);
	mfd_fail_add_seals(fd, F_SEAL_SHRINK |
			       F_SEAL_GROW |
			       F_SEAL_WRITE);
	mfd_assert_has_seals(fd, F_SEAL_SEAL);
	close(fd);
}

/*
 * Test SEAL_WRITE
 * Test whether SEAL_WRITE actually prevents modifications.
 */
static void test_seal_write(void)
{
	int fd;

	printf("%s SEAL-WRITE\n", memfd_str);

	fd = mfd_assert_new("kern_memfd_seal_write",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING);
	mfd_assert_has_seals(fd, 0);
	mfd_assert_add_seals(fd, F_SEAL_WRITE);
	mfd_assert_has_seals(fd, F_SEAL_WRITE);

	mfd_assert_read(fd);
	mfd_fail_write(fd);
	mfd_assert_shrink(fd);
	mfd_assert_grow(fd);
	mfd_fail_grow_write(fd);

	close(fd);
}

/*
 * Test SEAL_FUTURE_WRITE
 * Test whether SEAL_FUTURE_WRITE actually prevents modifications.
 */
static void test_seal_future_write(void)
{
	int fd, fd2;
	void *p;

	printf("%s SEAL-FUTURE-WRITE\n", memfd_str);

	fd = mfd_assert_new("kern_memfd_seal_future_write",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING);

	p = mfd_assert_mmap_shared(fd);

	mfd_assert_has_seals(fd, 0);

	mfd_assert_add_seals(fd, F_SEAL_FUTURE_WRITE);
	mfd_assert_has_seals(fd, F_SEAL_FUTURE_WRITE);

	/* read should pass, writes should fail */
	mfd_assert_read(fd);
	mfd_assert_read_shared(fd);
	mfd_fail_write(fd);

	fd2 = mfd_assert_reopen_fd(fd);
	/* read should pass, writes should still fail */
	mfd_assert_read(fd2);
	mfd_assert_read_shared(fd2);
	mfd_fail_write(fd2);

	mfd_assert_fork_private_write(fd);

	munmap(p, mfd_def_size);
	close(fd2);
	close(fd);
}

static void test_seal_write_map_read_shared(void)
{
	int fd;
	void *p;

	printf("%s SEAL-WRITE-MAP-READ\n", memfd_str);

	fd = mfd_assert_new("kern_memfd_seal_write_map_read",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING);

	mfd_assert_add_seals(fd, F_SEAL_WRITE);
	mfd_assert_has_seals(fd, F_SEAL_WRITE);

	p = mfd_assert_mmap_read_shared(fd);

	mfd_assert_read(fd);
	mfd_assert_read_shared(fd);
	mfd_fail_write(fd);

	munmap(p, mfd_def_size);
	close(fd);
}

/*
 * Test SEAL_SHRINK
 * Test whether SEAL_SHRINK actually prevents shrinking
 */
static void test_seal_shrink(void)
{
	int fd;

	printf("%s SEAL-SHRINK\n", memfd_str);

	fd = mfd_assert_new("kern_memfd_seal_shrink",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING);
	mfd_assert_has_seals(fd, 0);
	mfd_assert_add_seals(fd, F_SEAL_SHRINK);
	mfd_assert_has_seals(fd, F_SEAL_SHRINK);

	mfd_assert_read(fd);
	mfd_assert_write(fd);
	mfd_fail_shrink(fd);
	mfd_assert_grow(fd);
	mfd_assert_grow_write(fd);

	close(fd);
}

/*
 * Test SEAL_GROW
 * Test whether SEAL_GROW actually prevents growing
 */
static void test_seal_grow(void)
{
	int fd;

	printf("%s SEAL-GROW\n", memfd_str);

	fd = mfd_assert_new("kern_memfd_seal_grow",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING);
	mfd_assert_has_seals(fd, 0);
	mfd_assert_add_seals(fd, F_SEAL_GROW);
	mfd_assert_has_seals(fd, F_SEAL_GROW);

	mfd_assert_read(fd);
	mfd_assert_write(fd);
	mfd_assert_shrink(fd);
	mfd_fail_grow(fd);
	mfd_fail_grow_write(fd);

	close(fd);
}

/*
 * Test SEAL_SHRINK | SEAL_GROW
 * Test whether SEAL_SHRINK | SEAL_GROW actually prevents resizing
 */
static void test_seal_resize(void)
{
	int fd;

	printf("%s SEAL-RESIZE\n", memfd_str);

	fd = mfd_assert_new("kern_memfd_seal_resize",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING);
	mfd_assert_has_seals(fd, 0);
	mfd_assert_add_seals(fd, F_SEAL_SHRINK | F_SEAL_GROW);
	mfd_assert_has_seals(fd, F_SEAL_SHRINK | F_SEAL_GROW);

	mfd_assert_read(fd);
	mfd_assert_write(fd);
	mfd_fail_shrink(fd);
	mfd_fail_grow(fd);
	mfd_fail_grow_write(fd);

	close(fd);
}

/*
 * Test SEAL_EXEC
 * Test fd is created with exec and allow sealing.
 * chmod() cannot change x bits after sealing.
 */
static void test_exec_seal(void)
{
	int fd;

	printf("%s SEAL-EXEC\n", memfd_str);

	printf("%s	Apply SEAL_EXEC\n", memfd_str);
	fd = mfd_assert_new("kern_memfd_seal_exec",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_EXEC);

	mfd_assert_mode(fd, 0777);
	mfd_assert_chmod(fd, 0644);

	mfd_assert_has_seals(fd, 0);
	mfd_assert_add_seals(fd, F_SEAL_EXEC);
	mfd_assert_has_seals(fd, F_SEAL_EXEC);

	mfd_assert_chmod(fd, 0600);
	mfd_fail_chmod(fd, 0777);
	mfd_fail_chmod(fd, 0670);
	mfd_fail_chmod(fd, 0605);
	mfd_fail_chmod(fd, 0700);
	mfd_fail_chmod(fd, 0100);
	mfd_assert_chmod(fd, 0666);
	mfd_assert_write(fd);
	close(fd);

	printf("%s	Apply ALL_SEALS\n", memfd_str);
	fd = mfd_assert_new("kern_memfd_seal_exec",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_EXEC);

	mfd_assert_mode(fd, 0777);
	mfd_assert_chmod(fd, 0700);

	mfd_assert_has_seals(fd, 0);
	mfd_assert_add_seals(fd, F_SEAL_EXEC);
	mfd_assert_has_seals(fd, F_WX_SEALS);

	mfd_fail_chmod(fd, 0711);
	mfd_fail_chmod(fd, 0600);
	mfd_fail_write(fd);
	close(fd);
}

/*
 * Test EXEC_NO_SEAL
 * Test fd is created with exec and not allow sealing.
 */
static void test_exec_no_seal(void)
{
	int fd;

	printf("%s EXEC_NO_SEAL\n", memfd_str);

	/* Create with EXEC but without ALLOW_SEALING */
	fd = mfd_assert_new("kern_memfd_exec_no_sealing",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_EXEC);
	mfd_assert_mode(fd, 0777);
	mfd_assert_has_seals(fd, F_SEAL_SEAL);
	mfd_assert_chmod(fd, 0666);
	close(fd);
}

/*
 * Test memfd_create with MFD_NOEXEC flag
 */
static void test_noexec_seal(void)
{
	int fd;

	printf("%s NOEXEC_SEAL\n", memfd_str);

	/* Create with NOEXEC and ALLOW_SEALING */
	fd = mfd_assert_new("kern_memfd_noexec",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_NOEXEC_SEAL);
	mfd_assert_mode(fd, 0666);
	mfd_assert_has_seals(fd, F_SEAL_EXEC);
	mfd_fail_chmod(fd, 0777);
	close(fd);

	/* Create with NOEXEC but without ALLOW_SEALING */
	fd = mfd_assert_new("kern_memfd_noexec",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_NOEXEC_SEAL);
	mfd_assert_mode(fd, 0666);
	mfd_assert_has_seals(fd, F_SEAL_EXEC);
	mfd_fail_chmod(fd, 0777);
	close(fd);
}

static void test_sysctl_sysctl0(void)
{
	int fd;

	sysctl_assert_equal("0");

	fd = mfd_assert_new("kern_memfd_sysctl_0_dfl",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING);
	mfd_assert_mode(fd, 0777);
	mfd_assert_has_seals(fd, 0);
	mfd_assert_chmod(fd, 0644);
	close(fd);
}

static void test_sysctl_set_sysctl0(void)
{
	sysctl_assert_write("0");
	test_sysctl_sysctl0();
}

static void test_sysctl_sysctl1(void)
{
	int fd;

	sysctl_assert_equal("1");

	fd = mfd_assert_new("kern_memfd_sysctl_1_dfl",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING);
	mfd_assert_mode(fd, 0666);
	mfd_assert_has_seals(fd, F_SEAL_EXEC);
	mfd_fail_chmod(fd, 0777);
	close(fd);

	fd = mfd_assert_new("kern_memfd_sysctl_1_exec",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_EXEC | MFD_ALLOW_SEALING);
	mfd_assert_mode(fd, 0777);
	mfd_assert_has_seals(fd, 0);
	mfd_assert_chmod(fd, 0644);
	close(fd);

	fd = mfd_assert_new("kern_memfd_sysctl_1_noexec",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_NOEXEC_SEAL | MFD_ALLOW_SEALING);
	mfd_assert_mode(fd, 0666);
	mfd_assert_has_seals(fd, F_SEAL_EXEC);
	mfd_fail_chmod(fd, 0777);
	close(fd);
}

static void test_sysctl_set_sysctl1(void)
{
	sysctl_assert_write("1");
	test_sysctl_sysctl1();
}

static void test_sysctl_sysctl2(void)
{
	int fd;

	sysctl_assert_equal("2");

	fd = mfd_assert_new("kern_memfd_sysctl_2_dfl",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING);
	mfd_assert_mode(fd, 0666);
	mfd_assert_has_seals(fd, F_SEAL_EXEC);
	mfd_fail_chmod(fd, 0777);
	close(fd);

	mfd_fail_new("kern_memfd_sysctl_2_exec",
		     MFD_CLOEXEC | MFD_EXEC | MFD_ALLOW_SEALING);

	fd = mfd_assert_new("kern_memfd_sysctl_2_noexec",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_NOEXEC_SEAL | MFD_ALLOW_SEALING);
	mfd_assert_mode(fd, 0666);
	mfd_assert_has_seals(fd, F_SEAL_EXEC);
	mfd_fail_chmod(fd, 0777);
	close(fd);
}

static void test_sysctl_set_sysctl2(void)
{
	sysctl_assert_write("2");
	test_sysctl_sysctl2();
}

static int sysctl_simple_child(void *arg)
{
	printf("%s sysctl 0\n", memfd_str);
	test_sysctl_set_sysctl0();

	printf("%s sysctl 1\n", memfd_str);
	test_sysctl_set_sysctl1();

	printf("%s sysctl 0\n", memfd_str);
	test_sysctl_set_sysctl0();

	printf("%s sysctl 2\n", memfd_str);
	test_sysctl_set_sysctl2();

	printf("%s sysctl 1\n", memfd_str);
	test_sysctl_set_sysctl1();

	printf("%s sysctl 0\n", memfd_str);
	test_sysctl_set_sysctl0();

	return 0;
}

/*
 * Test sysctl
 * A very basic test to make sure the core sysctl semantics work.
 */
static void test_sysctl_simple(void)
{
	int pid = spawn_thread(CLONE_NEWPID, sysctl_simple_child, NULL);

	join_thread(pid);
}

static int sysctl_nested(void *arg)
{
	void (*fn)(void) = arg;

	fn();
	return 0;
}

static int sysctl_nested_wait(void *arg)
{
	/* Wait for a SIGCONT. */
	kill(getpid(), SIGSTOP);
	return sysctl_nested(arg);
}

static void test_sysctl_sysctl1_failset(void)
{
	sysctl_fail_write("0");
	test_sysctl_sysctl1();
}

static void test_sysctl_sysctl2_failset(void)
{
	sysctl_fail_write("1");
	test_sysctl_sysctl2();

	sysctl_fail_write("0");
	test_sysctl_sysctl2();
}

static int sysctl_nested_child(void *arg)
{
	int pid;

	printf("%s nested sysctl 0\n", memfd_str);
	sysctl_assert_write("0");
	/* A further nested pidns works the same. */
	pid = spawn_thread(CLONE_NEWPID, sysctl_simple_child, NULL);
	join_thread(pid);

	printf("%s nested sysctl 1\n", memfd_str);
	sysctl_assert_write("1");
	/* Child inherits our setting. */
	pid = spawn_thread(CLONE_NEWPID, sysctl_nested, test_sysctl_sysctl1);
	join_thread(pid);
	/* Child cannot raise the setting. */
	pid = spawn_thread(CLONE_NEWPID, sysctl_nested,
			   test_sysctl_sysctl1_failset);
	join_thread(pid);
	/* Child can lower the setting. */
	pid = spawn_thread(CLONE_NEWPID, sysctl_nested,
			   test_sysctl_set_sysctl2);
	join_thread(pid);
	/* Child lowering the setting has no effect on our setting. */
	test_sysctl_sysctl1();

	printf("%s nested sysctl 2\n", memfd_str);
	sysctl_assert_write("2");
	/* Child inherits our setting. */
	pid = spawn_thread(CLONE_NEWPID, sysctl_nested, test_sysctl_sysctl2);
	join_thread(pid);
	/* Child cannot raise the setting. */
	pid = spawn_thread(CLONE_NEWPID, sysctl_nested,
			   test_sysctl_sysctl2_failset);
	join_thread(pid);

	/* Verify that the rules are actually inherited after fork. */
	printf("%s nested sysctl 0 -> 1 after fork\n", memfd_str);
	sysctl_assert_write("0");

	pid = spawn_thread(CLONE_NEWPID, sysctl_nested_wait,
			   test_sysctl_sysctl1_failset);
	sysctl_assert_write("1");
	kill(pid, SIGCONT);
	join_thread(pid);

	printf("%s nested sysctl 0 -> 2 after fork\n", memfd_str);
	sysctl_assert_write("0");

	pid = spawn_thread(CLONE_NEWPID, sysctl_nested_wait,
			   test_sysctl_sysctl2_failset);
	sysctl_assert_write("2");
	kill(pid, SIGCONT);
	join_thread(pid);

	/*
	 * Verify that the current effective setting is saved on fork, meaning
	 * that the parent lowering the sysctl doesn't affect already-forked
	 * children.
	 */
	printf("%s nested sysctl 2 -> 1 after fork\n", memfd_str);
	sysctl_assert_write("2");
	pid = spawn_thread(CLONE_NEWPID, sysctl_nested_wait,
			   test_sysctl_sysctl2);
	sysctl_assert_write("1");
	kill(pid, SIGCONT);
	join_thread(pid);

	printf("%s nested sysctl 2 -> 0 after fork\n", memfd_str);
	sysctl_assert_write("2");
	pid = spawn_thread(CLONE_NEWPID, sysctl_nested_wait,
			   test_sysctl_sysctl2);
	sysctl_assert_write("0");
	kill(pid, SIGCONT);
	join_thread(pid);

	printf("%s nested sysctl 1 -> 0 after fork\n", memfd_str);
	sysctl_assert_write("1");
	pid = spawn_thread(CLONE_NEWPID, sysctl_nested_wait,
			   test_sysctl_sysctl1);
	sysctl_assert_write("0");
	kill(pid, SIGCONT);
	join_thread(pid);

	return 0;
}

/*
 * Test sysctl with nested pid namespaces
 * Make sure that the sysctl nesting semantics work correctly.
 */
static void test_sysctl_nested(void)
{
	int pid = spawn_thread(CLONE_NEWPID, sysctl_nested_child, NULL);

	join_thread(pid);
}

/*
 * Test sharing via dup()
 * Test that seals are shared between dupped FDs and they're all equal.
 */
static void test_share_dup(char *banner, char *b_suffix)
{
	int fd, fd2;

	printf("%s %s %s\n", memfd_str, banner, b_suffix);

	fd = mfd_assert_new("kern_memfd_share_dup",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING);
	mfd_assert_has_seals(fd, 0);

	fd2 = mfd_assert_dup(fd);
	mfd_assert_has_seals(fd2, 0);

	mfd_assert_add_seals(fd, F_SEAL_WRITE);
	mfd_assert_has_seals(fd, F_SEAL_WRITE);
	mfd_assert_has_seals(fd2, F_SEAL_WRITE);

	mfd_assert_add_seals(fd2, F_SEAL_SHRINK);
	mfd_assert_has_seals(fd, F_SEAL_WRITE | F_SEAL_SHRINK);
	mfd_assert_has_seals(fd2, F_SEAL_WRITE | F_SEAL_SHRINK);

	mfd_assert_add_seals(fd, F_SEAL_SEAL);
	mfd_assert_has_seals(fd, F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_SEAL);
	mfd_assert_has_seals(fd2, F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_SEAL);

	mfd_fail_add_seals(fd, F_SEAL_GROW);
	mfd_fail_add_seals(fd2, F_SEAL_GROW);
	mfd_fail_add_seals(fd, F_SEAL_SEAL);
	mfd_fail_add_seals(fd2, F_SEAL_SEAL);

	close(fd2);

	mfd_fail_add_seals(fd, F_SEAL_GROW);
	close(fd);
}

/*
 * Test sealing with active mmap()s
 * Modifying seals is only allowed if no other mmap() refs exist.
 */
static void test_share_mmap(char *banner, char *b_suffix)
{
	int fd;
	void *p;

	printf("%s %s %s\n", memfd_str,  banner, b_suffix);

	fd = mfd_assert_new("kern_memfd_share_mmap",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING);
	mfd_assert_has_seals(fd, 0);

	/* shared/writable ref prevents sealing WRITE, but allows others */
	p = mfd_assert_mmap_shared(fd);
	mfd_fail_add_seals(fd, F_SEAL_WRITE);
	mfd_assert_has_seals(fd, 0);
	mfd_assert_add_seals(fd, F_SEAL_SHRINK);
	mfd_assert_has_seals(fd, F_SEAL_SHRINK);
	munmap(p, mfd_def_size);

	/* readable ref allows sealing */
	p = mfd_assert_mmap_private(fd);
	mfd_assert_add_seals(fd, F_SEAL_WRITE);
	mfd_assert_has_seals(fd, F_SEAL_WRITE | F_SEAL_SHRINK);
	munmap(p, mfd_def_size);

	close(fd);
}

/*
 * Test sealing with open(/proc/self/fd/%d)
 * Via /proc we can get access to a separate file-context for the same memfd.
 * This is *not* like dup(), but like a real separate open(). Make sure the
 * semantics are as expected and we correctly check for RDONLY / WRONLY / RDWR.
 */
static void test_share_open(char *banner, char *b_suffix)
{
	int fd, fd2;

	printf("%s %s %s\n", memfd_str, banner, b_suffix);

	fd = mfd_assert_new("kern_memfd_share_open",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING);
	mfd_assert_has_seals(fd, 0);

	fd2 = mfd_assert_open(fd, O_RDWR, 0);
	mfd_assert_add_seals(fd, F_SEAL_WRITE);
	mfd_assert_has_seals(fd, F_SEAL_WRITE);
	mfd_assert_has_seals(fd2, F_SEAL_WRITE);

	mfd_assert_add_seals(fd2, F_SEAL_SHRINK);
	mfd_assert_has_seals(fd, F_SEAL_WRITE | F_SEAL_SHRINK);
	mfd_assert_has_seals(fd2, F_SEAL_WRITE | F_SEAL_SHRINK);

	close(fd);
	fd = mfd_assert_open(fd2, O_RDONLY, 0);

	mfd_fail_add_seals(fd, F_SEAL_SEAL);
	mfd_assert_has_seals(fd, F_SEAL_WRITE | F_SEAL_SHRINK);
	mfd_assert_has_seals(fd2, F_SEAL_WRITE | F_SEAL_SHRINK);

	close(fd2);
	fd2 = mfd_assert_open(fd, O_RDWR, 0);

	mfd_assert_add_seals(fd2, F_SEAL_SEAL);
	mfd_assert_has_seals(fd, F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_SEAL);
	mfd_assert_has_seals(fd2, F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_SEAL);

	close(fd2);
	close(fd);
}

/*
 * Test sharing via fork()
 * Test whether seal-modifications work as expected with forked children.
 */
static void test_share_fork(char *banner, char *b_suffix)
{
	int fd;
	pid_t pid;

	printf("%s %s %s\n", memfd_str, banner, b_suffix);

	fd = mfd_assert_new("kern_memfd_share_fork",
			    mfd_def_size,
			    MFD_CLOEXEC | MFD_ALLOW_SEALING);
	mfd_assert_has_seals(fd, 0);

	pid = spawn_idle_thread(0);
	mfd_assert_add_seals(fd, F_SEAL_SEAL);
	mfd_assert_has_seals(fd, F_SEAL_SEAL);

	mfd_fail_add_seals(fd, F_SEAL_WRITE);
	mfd_assert_has_seals(fd, F_SEAL_SEAL);

	join_idle_thread(pid);

	mfd_fail_add_seals(fd, F_SEAL_WRITE);
	mfd_assert_has_seals(fd, F_SEAL_SEAL);

	close(fd);
}

static bool pid_ns_supported(void)
{
	return access("/proc/self/ns/pid", F_OK) == 0;
}

int main(int argc, char **argv)
{
	pid_t pid;

	if (argc == 2) {
		if (!strcmp(argv[1], "hugetlbfs")) {
			unsigned long hpage_size = default_huge_page_size();

			if (!hpage_size) {
				printf("Unable to determine huge page size\n");
				abort();
			}

			hugetlbfs_test = 1;
			memfd_str = MEMFD_HUGE_STR;
			mfd_def_size = hpage_size * 2;
		} else {
			printf("Unknown option: %s\n", argv[1]);
			abort();
		}
	}

	test_create();
	test_basic();
	test_exec_seal();
	test_exec_no_seal();
	test_noexec_seal();

	test_seal_write();
	test_seal_future_write();
	test_seal_write_map_read_shared();
	test_seal_shrink();
	test_seal_grow();
	test_seal_resize();

	if (pid_ns_supported()) {
		test_sysctl_simple();
		test_sysctl_nested();
	} else {
		printf("PID namespaces are not supported; skipping sysctl tests\n");
	}

	test_share_dup("SHARE-DUP", "");
	test_share_mmap("SHARE-MMAP", "");
	test_share_open("SHARE-OPEN", "");
	test_share_fork("SHARE-FORK", "");

	/* Run test-suite in a multi-threaded environment with a shared
	 * file-table. */
	pid = spawn_idle_thread(CLONE_FILES | CLONE_FS | CLONE_VM);
	test_share_dup("SHARE-DUP", SHARED_FT_STR);
	test_share_mmap("SHARE-MMAP", SHARED_FT_STR);
	test_share_open("SHARE-OPEN", SHARED_FT_STR);
	test_share_fork("SHARE-FORK", SHARED_FT_STR);
	join_idle_thread(pid);

	printf("memfd: DONE\n");

	return 0;
}
