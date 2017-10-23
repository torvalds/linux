#define _GNU_SOURCE
#define __EXPORTED_HEADERS__

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/falloc.h>
#include <linux/fcntl.h>
#include <linux/memfd.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define MEMFD_STR	"memfd:"
#define SHARED_FT_STR	"(shared file-table)"

#define MFD_DEF_SIZE 8192
#define STACK_SIZE 65536

/*
 * Default is not to test hugetlbfs
 */
static int hugetlbfs_test;
static size_t mfd_def_size = MFD_DEF_SIZE;

/*
 * Copied from mlock2-tests.c
 */
static unsigned long default_huge_page_size(void)
{
	unsigned long hps = 0;
	char *line = NULL;
	size_t linelen = 0;
	FILE *f = fopen("/proc/meminfo", "r");

	if (!f)
		return 0;
	while (getline(&line, &linelen, f) > 0) {
		if (sscanf(line, "Hugepagesize:       %lu kB", &hps) == 1) {
			hps <<= 10;
			break;
		}
	}

	free(line);
	fclose(f);
	return hps;
}

static int sys_memfd_create(const char *name,
			    unsigned int flags)
{
	if (hugetlbfs_test)
		flags |= MFD_HUGETLB;

	return syscall(__NR_memfd_create, name, flags);
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

static void mfd_fail_new(const char *name, unsigned int flags)
{
	int r;

	r = sys_memfd_create(name, flags);
	if (r >= 0) {
		printf("memfd_create(\"%s\", %u) succeeded, but failure expected\n",
		       name, flags);
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
	unsigned int s;

	s = mfd_assert_get_seals(fd);
	if (s != seals) {
		printf("%u != %u = GET_SEALS(%d)\n", seals, s, fd);
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

	buf = malloc(mfd_def_size * 8);
	if (!buf) {
		printf("malloc(%d) failed: %m\n", mfd_def_size * 8);
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

	buf = malloc(mfd_def_size * 8);
	if (!buf) {
		printf("malloc(%d) failed: %m\n", mfd_def_size * 8);
		abort();
	}

	l = pwrite(fd, buf, mfd_def_size * 8, 0);
	if (l == (mfd_def_size * 8)) {
		printf("pwrite() didn't fail as expected\n");
		abort();
	}
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

static pid_t spawn_idle_thread(unsigned int flags)
{
	uint8_t *stack;
	pid_t pid;

	stack = malloc(STACK_SIZE);
	if (!stack) {
		printf("malloc(STACK_SIZE) failed: %m\n");
		abort();
	}

	pid = clone(idle_thread_fn,
		    stack + STACK_SIZE,
		    SIGCHLD | flags,
		    NULL);
	if (pid < 0) {
		printf("clone() failed: %m\n");
		abort();
	}

	return pid;
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

	printf("%s CREATE\n", MEMFD_STR);

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

	/* verify MFD_CLOEXEC is allowed */
	fd = mfd_assert_new("", 0, MFD_CLOEXEC);
	close(fd);

	if (!hugetlbfs_test) {
		/* verify MFD_ALLOW_SEALING is allowed */
		fd = mfd_assert_new("", 0, MFD_ALLOW_SEALING);
		close(fd);

		/* verify MFD_ALLOW_SEALING | MFD_CLOEXEC is allowed */
		fd = mfd_assert_new("", 0, MFD_ALLOW_SEALING | MFD_CLOEXEC);
		close(fd);
	} else {
		/* sealing is not supported on hugetlbfs */
		mfd_fail_new("", MFD_ALLOW_SEALING);
	}
}

/*
 * Test basic sealing
 * A very basic sealing test to see whether setting/retrieving seals works.
 */
static void test_basic(void)
{
	int fd;

	/* hugetlbfs does not contain sealing support */
	if (hugetlbfs_test)
		return;

	printf("%s BASIC\n", MEMFD_STR);

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
 * hugetlbfs doesn't support seals or write, so just verify grow and shrink
 * on a hugetlbfs file created via memfd_create.
 */
static void test_hugetlbfs_grow_shrink(void)
{
	int fd;

	printf("%s HUGETLBFS-GROW-SHRINK\n", MEMFD_STR);

	fd = mfd_assert_new("kern_memfd_seal_write",
			    mfd_def_size,
			    MFD_CLOEXEC);

	mfd_assert_read(fd);
	mfd_assert_write(fd);
	mfd_assert_shrink(fd);
	mfd_assert_grow(fd);

	close(fd);
}

/*
 * Test SEAL_WRITE
 * Test whether SEAL_WRITE actually prevents modifications.
 */
static void test_seal_write(void)
{
	int fd;

	/*
	 * hugetlbfs does not contain sealing or write support.  Just test
	 * basic grow and shrink via test_hugetlbfs_grow_shrink.
	 */
	if (hugetlbfs_test)
		return test_hugetlbfs_grow_shrink();

	printf("%s SEAL-WRITE\n", MEMFD_STR);

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
 * Test SEAL_SHRINK
 * Test whether SEAL_SHRINK actually prevents shrinking
 */
static void test_seal_shrink(void)
{
	int fd;

	/* hugetlbfs does not contain sealing support */
	if (hugetlbfs_test)
		return;

	printf("%s SEAL-SHRINK\n", MEMFD_STR);

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

	/* hugetlbfs does not contain sealing support */
	if (hugetlbfs_test)
		return;

	printf("%s SEAL-GROW\n", MEMFD_STR);

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

	/* hugetlbfs does not contain sealing support */
	if (hugetlbfs_test)
		return;

	printf("%s SEAL-RESIZE\n", MEMFD_STR);

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
 * hugetlbfs does not support seals.  Basic test to dup the memfd created
 * fd and perform some basic operations on it.
 */
static void hugetlbfs_dup(char *b_suffix)
{
	int fd, fd2;

	printf("%s HUGETLBFS-DUP %s\n", MEMFD_STR, b_suffix);

	fd = mfd_assert_new("kern_memfd_share_dup",
			    mfd_def_size,
			    MFD_CLOEXEC);

	fd2 = mfd_assert_dup(fd);

	mfd_assert_read(fd);
	mfd_assert_write(fd);

	mfd_assert_shrink(fd2);
	mfd_assert_grow(fd2);

	close(fd2);
	close(fd);
}

/*
 * Test sharing via dup()
 * Test that seals are shared between dupped FDs and they're all equal.
 */
static void test_share_dup(char *banner, char *b_suffix)
{
	int fd, fd2;

	/*
	 * hugetlbfs does not contain sealing support.  Perform some
	 * basic testing on dup'ed fd instead via hugetlbfs_dup.
	 */
	if (hugetlbfs_test) {
		hugetlbfs_dup(b_suffix);
		return;
	}

	printf("%s %s %s\n", MEMFD_STR, banner, b_suffix);

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

	/* hugetlbfs does not contain sealing support */
	if (hugetlbfs_test)
		return;

	printf("%s %s %s\n", MEMFD_STR,  banner, b_suffix);

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
 * Basic test to make sure we can open the hugetlbfs fd via /proc and
 * perform some simple operations on it.
 */
static void hugetlbfs_proc_open(char *b_suffix)
{
	int fd, fd2;

	printf("%s HUGETLBFS-PROC-OPEN %s\n", MEMFD_STR, b_suffix);

	fd = mfd_assert_new("kern_memfd_share_open",
			    mfd_def_size,
			    MFD_CLOEXEC);

	fd2 = mfd_assert_open(fd, O_RDWR, 0);

	mfd_assert_read(fd);
	mfd_assert_write(fd);

	mfd_assert_shrink(fd2);
	mfd_assert_grow(fd2);

	close(fd2);
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

	/*
	 * hugetlbfs does not contain sealing support.  So test basic
	 * functionality of using /proc fd via hugetlbfs_proc_open
	 */
	if (hugetlbfs_test) {
		hugetlbfs_proc_open(b_suffix);
		return;
	}

	printf("%s %s %s\n", MEMFD_STR, banner, b_suffix);

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
 * Test whether seal-modifications work as expected with forked childs.
 */
static void test_share_fork(char *banner, char *b_suffix)
{
	int fd;
	pid_t pid;

	/* hugetlbfs does not contain sealing support */
	if (hugetlbfs_test)
		return;

	printf("%s %s %s\n", MEMFD_STR, banner, b_suffix);

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
			mfd_def_size = hpage_size * 2;
		}
	}

	test_create();
	test_basic();

	test_seal_write();
	test_seal_shrink();
	test_seal_grow();
	test_seal_resize();

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
