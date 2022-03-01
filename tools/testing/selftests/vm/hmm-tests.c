// SPDX-License-Identifier: GPL-2.0
/*
 * HMM stands for Heterogeneous Memory Management, it is a helper layer inside
 * the linux kernel to help device drivers mirror a process address space in
 * the device. This allows the device to use the same address space which
 * makes communication and data exchange a lot easier.
 *
 * This framework's sole purpose is to exercise various code paths inside
 * the kernel to make sure that HMM performs as expected and to flush out any
 * bugs.
 */

#include "../kselftest_harness.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <strings.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "./local_config.h"
#ifdef LOCAL_CONFIG_HAVE_LIBHUGETLBFS
#include <hugetlbfs.h>
#endif

/*
 * This is a private UAPI to the kernel test module so it isn't exported
 * in the usual include/uapi/... directory.
 */
#include "../../../../lib/test_hmm_uapi.h"

struct hmm_buffer {
	void		*ptr;
	void		*mirror;
	unsigned long	size;
	int		fd;
	uint64_t	cpages;
	uint64_t	faults;
};

#define TWOMEG		(1 << 21)
#define HMM_BUFFER_SIZE (1024 << 12)
#define HMM_PATH_MAX    64
#define NTIMES		10

#define ALIGN(x, a) (((x) + (a - 1)) & (~((a) - 1)))

FIXTURE(hmm)
{
	int		fd;
	unsigned int	page_size;
	unsigned int	page_shift;
};

FIXTURE(hmm2)
{
	int		fd0;
	int		fd1;
	unsigned int	page_size;
	unsigned int	page_shift;
};

static int hmm_open(int unit)
{
	char pathname[HMM_PATH_MAX];
	int fd;

	snprintf(pathname, sizeof(pathname), "/dev/hmm_dmirror%d", unit);
	fd = open(pathname, O_RDWR, 0);
	if (fd < 0)
		fprintf(stderr, "could not open hmm dmirror driver (%s)\n",
			pathname);
	return fd;
}

FIXTURE_SETUP(hmm)
{
	self->page_size = sysconf(_SC_PAGE_SIZE);
	self->page_shift = ffs(self->page_size) - 1;

	self->fd = hmm_open(0);
	ASSERT_GE(self->fd, 0);
}

FIXTURE_SETUP(hmm2)
{
	self->page_size = sysconf(_SC_PAGE_SIZE);
	self->page_shift = ffs(self->page_size) - 1;

	self->fd0 = hmm_open(0);
	ASSERT_GE(self->fd0, 0);
	self->fd1 = hmm_open(1);
	ASSERT_GE(self->fd1, 0);
}

FIXTURE_TEARDOWN(hmm)
{
	int ret = close(self->fd);

	ASSERT_EQ(ret, 0);
	self->fd = -1;
}

FIXTURE_TEARDOWN(hmm2)
{
	int ret = close(self->fd0);

	ASSERT_EQ(ret, 0);
	self->fd0 = -1;

	ret = close(self->fd1);
	ASSERT_EQ(ret, 0);
	self->fd1 = -1;
}

static int hmm_dmirror_cmd(int fd,
			   unsigned long request,
			   struct hmm_buffer *buffer,
			   unsigned long npages)
{
	struct hmm_dmirror_cmd cmd;
	int ret;

	/* Simulate a device reading system memory. */
	cmd.addr = (__u64)buffer->ptr;
	cmd.ptr = (__u64)buffer->mirror;
	cmd.npages = npages;

	for (;;) {
		ret = ioctl(fd, request, &cmd);
		if (ret == 0)
			break;
		if (errno == EINTR)
			continue;
		return -errno;
	}
	buffer->cpages = cmd.cpages;
	buffer->faults = cmd.faults;

	return 0;
}

static void hmm_buffer_free(struct hmm_buffer *buffer)
{
	if (buffer == NULL)
		return;

	if (buffer->ptr)
		munmap(buffer->ptr, buffer->size);
	free(buffer->mirror);
	free(buffer);
}

/*
 * Create a temporary file that will be deleted on close.
 */
static int hmm_create_file(unsigned long size)
{
	char path[HMM_PATH_MAX];
	int fd;

	strcpy(path, "/tmp");
	fd = open(path, O_TMPFILE | O_EXCL | O_RDWR, 0600);
	if (fd >= 0) {
		int r;

		do {
			r = ftruncate(fd, size);
		} while (r == -1 && errno == EINTR);
		if (!r)
			return fd;
		close(fd);
	}
	return -1;
}

/*
 * Return a random unsigned number.
 */
static unsigned int hmm_random(void)
{
	static int fd = -1;
	unsigned int r;

	if (fd < 0) {
		fd = open("/dev/urandom", O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "%s:%d failed to open /dev/urandom\n",
					__FILE__, __LINE__);
			return ~0U;
		}
	}
	read(fd, &r, sizeof(r));
	return r;
}

static void hmm_nanosleep(unsigned int n)
{
	struct timespec t;

	t.tv_sec = 0;
	t.tv_nsec = n;
	nanosleep(&t, NULL);
}

/*
 * Simple NULL test of device open/close.
 */
TEST_F(hmm, open_close)
{
}

/*
 * Read private anonymous memory.
 */
TEST_F(hmm, anon_read)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	int *ptr;
	int ret;
	int val;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	buffer->ptr = mmap(NULL, size,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/*
	 * Initialize buffer in system memory but leave the first two pages
	 * zero (pte_none and pfn_zero).
	 */
	i = 2 * self->page_size / sizeof(*ptr);
	for (ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Set buffer permission to read-only. */
	ret = mprotect(buffer->ptr, size, PROT_READ);
	ASSERT_EQ(ret, 0);

	/* Populate the CPU page table with a special zero page. */
	val = *(int *)(buffer->ptr + self->page_size);
	ASSERT_EQ(val, 0);

	/* Simulate a device reading system memory. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_READ, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);
	ASSERT_EQ(buffer->faults, 1);

	/* Check what the device read. */
	ptr = buffer->mirror;
	for (i = 0; i < 2 * self->page_size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], 0);
	for (; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	hmm_buffer_free(buffer);
}

/*
 * Read private anonymous memory which has been protected with
 * mprotect() PROT_NONE.
 */
TEST_F(hmm, anon_read_prot)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	int *ptr;
	int ret;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	buffer->ptr = mmap(NULL, size,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/* Initialize buffer in system memory. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Initialize mirror buffer so we can verify it isn't written. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ptr[i] = -i;

	/* Protect buffer from reading. */
	ret = mprotect(buffer->ptr, size, PROT_NONE);
	ASSERT_EQ(ret, 0);

	/* Simulate a device reading system memory. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_READ, buffer, npages);
	ASSERT_EQ(ret, -EFAULT);

	/* Allow CPU to read the buffer so we can check it. */
	ret = mprotect(buffer->ptr, size, PROT_READ);
	ASSERT_EQ(ret, 0);
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	/* Check what the device read. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], -i);

	hmm_buffer_free(buffer);
}

/*
 * Write private anonymous memory.
 */
TEST_F(hmm, anon_write)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	int *ptr;
	int ret;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	buffer->ptr = mmap(NULL, size,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/* Initialize data that the device will write to buffer->ptr. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Simulate a device writing system memory. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_WRITE, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);
	ASSERT_EQ(buffer->faults, 1);

	/* Check what the device wrote. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	hmm_buffer_free(buffer);
}

/*
 * Write private anonymous memory which has been protected with
 * mprotect() PROT_READ.
 */
TEST_F(hmm, anon_write_prot)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	int *ptr;
	int ret;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	buffer->ptr = mmap(NULL, size,
			   PROT_READ,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/* Simulate a device reading a zero page of memory. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_READ, buffer, 1);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, 1);
	ASSERT_EQ(buffer->faults, 1);

	/* Initialize data that the device will write to buffer->ptr. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Simulate a device writing system memory. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_WRITE, buffer, npages);
	ASSERT_EQ(ret, -EPERM);

	/* Check what the device wrote. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], 0);

	/* Now allow writing and see that the zero page is replaced. */
	ret = mprotect(buffer->ptr, size, PROT_WRITE | PROT_READ);
	ASSERT_EQ(ret, 0);

	/* Simulate a device writing system memory. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_WRITE, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);
	ASSERT_EQ(buffer->faults, 1);

	/* Check what the device wrote. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	hmm_buffer_free(buffer);
}

/*
 * Check that a device writing an anonymous private mapping
 * will copy-on-write if a child process inherits the mapping.
 */
TEST_F(hmm, anon_write_child)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	int *ptr;
	pid_t pid;
	int child_fd;
	int ret;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	buffer->ptr = mmap(NULL, size,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/* Initialize buffer->ptr so we can tell if it is written. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Initialize data that the device will write to buffer->ptr. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ptr[i] = -i;

	pid = fork();
	if (pid == -1)
		ASSERT_EQ(pid, 0);
	if (pid != 0) {
		waitpid(pid, &ret, 0);
		ASSERT_EQ(WIFEXITED(ret), 1);

		/* Check that the parent's buffer did not change. */
		for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
			ASSERT_EQ(ptr[i], i);
		return;
	}

	/* Check that we see the parent's values. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], -i);

	/* The child process needs its own mirror to its own mm. */
	child_fd = hmm_open(0);
	ASSERT_GE(child_fd, 0);

	/* Simulate a device writing system memory. */
	ret = hmm_dmirror_cmd(child_fd, HMM_DMIRROR_WRITE, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);
	ASSERT_EQ(buffer->faults, 1);

	/* Check what the device wrote. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], -i);

	close(child_fd);
	exit(0);
}

/*
 * Check that a device writing an anonymous shared mapping
 * will not copy-on-write if a child process inherits the mapping.
 */
TEST_F(hmm, anon_write_child_shared)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	int *ptr;
	pid_t pid;
	int child_fd;
	int ret;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	buffer->ptr = mmap(NULL, size,
			   PROT_READ | PROT_WRITE,
			   MAP_SHARED | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/* Initialize buffer->ptr so we can tell if it is written. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Initialize data that the device will write to buffer->ptr. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ptr[i] = -i;

	pid = fork();
	if (pid == -1)
		ASSERT_EQ(pid, 0);
	if (pid != 0) {
		waitpid(pid, &ret, 0);
		ASSERT_EQ(WIFEXITED(ret), 1);

		/* Check that the parent's buffer did change. */
		for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
			ASSERT_EQ(ptr[i], -i);
		return;
	}

	/* Check that we see the parent's values. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], -i);

	/* The child process needs its own mirror to its own mm. */
	child_fd = hmm_open(0);
	ASSERT_GE(child_fd, 0);

	/* Simulate a device writing system memory. */
	ret = hmm_dmirror_cmd(child_fd, HMM_DMIRROR_WRITE, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);
	ASSERT_EQ(buffer->faults, 1);

	/* Check what the device wrote. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], -i);

	close(child_fd);
	exit(0);
}

/*
 * Write private anonymous huge page.
 */
TEST_F(hmm, anon_write_huge)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	void *old_ptr;
	void *map;
	int *ptr;
	int ret;

	size = 2 * TWOMEG;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	buffer->ptr = mmap(NULL, size,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	size = TWOMEG;
	npages = size >> self->page_shift;
	map = (void *)ALIGN((uintptr_t)buffer->ptr, size);
	ret = madvise(map, size, MADV_HUGEPAGE);
	ASSERT_EQ(ret, 0);
	old_ptr = buffer->ptr;
	buffer->ptr = map;

	/* Initialize data that the device will write to buffer->ptr. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Simulate a device writing system memory. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_WRITE, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);
	ASSERT_EQ(buffer->faults, 1);

	/* Check what the device wrote. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	buffer->ptr = old_ptr;
	hmm_buffer_free(buffer);
}

#ifdef LOCAL_CONFIG_HAVE_LIBHUGETLBFS
/*
 * Write huge TLBFS page.
 */
TEST_F(hmm, anon_write_hugetlbfs)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	int *ptr;
	int ret;
	long pagesizes[4];
	int n, idx;

	/* Skip test if we can't allocate a hugetlbfs page. */

	n = gethugepagesizes(pagesizes, 4);
	if (n <= 0)
		SKIP(return, "Huge page size could not be determined");
	for (idx = 0; --n > 0; ) {
		if (pagesizes[n] < pagesizes[idx])
			idx = n;
	}
	size = ALIGN(TWOMEG, pagesizes[idx]);
	npages = size >> self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->ptr = get_hugepage_region(size, GHR_STRICT);
	if (buffer->ptr == NULL) {
		free(buffer);
		SKIP(return, "Huge page could not be allocated");
	}

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	/* Initialize data that the device will write to buffer->ptr. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Simulate a device writing system memory. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_WRITE, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);
	ASSERT_EQ(buffer->faults, 1);

	/* Check what the device wrote. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	free_hugepage_region(buffer->ptr);
	buffer->ptr = NULL;
	hmm_buffer_free(buffer);
}
#endif /* LOCAL_CONFIG_HAVE_LIBHUGETLBFS */

/*
 * Read mmap'ed file memory.
 */
TEST_F(hmm, file_read)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	int *ptr;
	int ret;
	int fd;
	ssize_t len;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	fd = hmm_create_file(size);
	ASSERT_GE(fd, 0);

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = fd;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	/* Write initial contents of the file. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;
	len = pwrite(fd, buffer->mirror, size, 0);
	ASSERT_EQ(len, size);
	memset(buffer->mirror, 0, size);

	buffer->ptr = mmap(NULL, size,
			   PROT_READ,
			   MAP_SHARED,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/* Simulate a device reading system memory. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_READ, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);
	ASSERT_EQ(buffer->faults, 1);

	/* Check what the device read. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	hmm_buffer_free(buffer);
}

/*
 * Write mmap'ed file memory.
 */
TEST_F(hmm, file_write)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	int *ptr;
	int ret;
	int fd;
	ssize_t len;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	fd = hmm_create_file(size);
	ASSERT_GE(fd, 0);

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = fd;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	buffer->ptr = mmap(NULL, size,
			   PROT_READ | PROT_WRITE,
			   MAP_SHARED,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/* Initialize data that the device will write to buffer->ptr. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Simulate a device writing system memory. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_WRITE, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);
	ASSERT_EQ(buffer->faults, 1);

	/* Check what the device wrote. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	/* Check that the device also wrote the file. */
	len = pread(fd, buffer->mirror, size, 0);
	ASSERT_EQ(len, size);
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	hmm_buffer_free(buffer);
}

/*
 * Migrate anonymous memory to device private memory.
 */
TEST_F(hmm, migrate)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	int *ptr;
	int ret;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	buffer->ptr = mmap(NULL, size,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/* Initialize buffer in system memory. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Migrate memory to device. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_MIGRATE, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);

	/* Check what the device read. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	hmm_buffer_free(buffer);
}

/*
 * Migrate anonymous memory to device private memory and fault some of it back
 * to system memory, then try migrating the resulting mix of system and device
 * private memory to the device.
 */
TEST_F(hmm, migrate_fault)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	int *ptr;
	int ret;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	buffer->ptr = mmap(NULL, size,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/* Initialize buffer in system memory. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Migrate memory to device. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_MIGRATE, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);

	/* Check what the device read. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	/* Fault half the pages back to system memory and check them. */
	for (i = 0, ptr = buffer->ptr; i < size / (2 * sizeof(*ptr)); ++i)
		ASSERT_EQ(ptr[i], i);

	/* Migrate memory to the device again. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_MIGRATE, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);

	/* Check what the device read. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	hmm_buffer_free(buffer);
}

/*
 * Migrate anonymous shared memory to device private memory.
 */
TEST_F(hmm, migrate_shared)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	int ret;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	buffer->ptr = mmap(NULL, size,
			   PROT_READ | PROT_WRITE,
			   MAP_SHARED | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/* Migrate memory to device. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_MIGRATE, buffer, npages);
	ASSERT_EQ(ret, -ENOENT);

	hmm_buffer_free(buffer);
}

/*
 * Try to migrate various memory types to device private memory.
 */
TEST_F(hmm2, migrate_mixed)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	int *ptr;
	unsigned char *p;
	int ret;
	int val;

	npages = 6;
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	/* Reserve a range of addresses. */
	buffer->ptr = mmap(NULL, size,
			   PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);
	p = buffer->ptr;

	/* Migrating a protected area should be an error. */
	ret = hmm_dmirror_cmd(self->fd1, HMM_DMIRROR_MIGRATE, buffer, npages);
	ASSERT_EQ(ret, -EINVAL);

	/* Punch a hole after the first page address. */
	ret = munmap(buffer->ptr + self->page_size, self->page_size);
	ASSERT_EQ(ret, 0);

	/* We expect an error if the vma doesn't cover the range. */
	ret = hmm_dmirror_cmd(self->fd1, HMM_DMIRROR_MIGRATE, buffer, 3);
	ASSERT_EQ(ret, -EINVAL);

	/* Page 2 will be a read-only zero page. */
	ret = mprotect(buffer->ptr + 2 * self->page_size, self->page_size,
				PROT_READ);
	ASSERT_EQ(ret, 0);
	ptr = (int *)(buffer->ptr + 2 * self->page_size);
	val = *ptr + 3;
	ASSERT_EQ(val, 3);

	/* Page 3 will be read-only. */
	ret = mprotect(buffer->ptr + 3 * self->page_size, self->page_size,
				PROT_READ | PROT_WRITE);
	ASSERT_EQ(ret, 0);
	ptr = (int *)(buffer->ptr + 3 * self->page_size);
	*ptr = val;
	ret = mprotect(buffer->ptr + 3 * self->page_size, self->page_size,
				PROT_READ);
	ASSERT_EQ(ret, 0);

	/* Page 4-5 will be read-write. */
	ret = mprotect(buffer->ptr + 4 * self->page_size, 2 * self->page_size,
				PROT_READ | PROT_WRITE);
	ASSERT_EQ(ret, 0);
	ptr = (int *)(buffer->ptr + 4 * self->page_size);
	*ptr = val;
	ptr = (int *)(buffer->ptr + 5 * self->page_size);
	*ptr = val;

	/* Now try to migrate pages 2-5 to device 1. */
	buffer->ptr = p + 2 * self->page_size;
	ret = hmm_dmirror_cmd(self->fd1, HMM_DMIRROR_MIGRATE, buffer, 4);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, 4);

	/* Page 5 won't be migrated to device 0 because it's on device 1. */
	buffer->ptr = p + 5 * self->page_size;
	ret = hmm_dmirror_cmd(self->fd0, HMM_DMIRROR_MIGRATE, buffer, 1);
	ASSERT_EQ(ret, -ENOENT);
	buffer->ptr = p;

	buffer->ptr = p;
	hmm_buffer_free(buffer);
}

/*
 * Migrate anonymous memory to device private memory and fault it back to system
 * memory multiple times.
 */
TEST_F(hmm, migrate_multiple)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	unsigned long c;
	int *ptr;
	int ret;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	for (c = 0; c < NTIMES; c++) {
		buffer = malloc(sizeof(*buffer));
		ASSERT_NE(buffer, NULL);

		buffer->fd = -1;
		buffer->size = size;
		buffer->mirror = malloc(size);
		ASSERT_NE(buffer->mirror, NULL);

		buffer->ptr = mmap(NULL, size,
				   PROT_READ | PROT_WRITE,
				   MAP_PRIVATE | MAP_ANONYMOUS,
				   buffer->fd, 0);
		ASSERT_NE(buffer->ptr, MAP_FAILED);

		/* Initialize buffer in system memory. */
		for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
			ptr[i] = i;

		/* Migrate memory to device. */
		ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_MIGRATE, buffer,
				      npages);
		ASSERT_EQ(ret, 0);
		ASSERT_EQ(buffer->cpages, npages);

		/* Check what the device read. */
		for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
			ASSERT_EQ(ptr[i], i);

		/* Fault pages back to system memory and check them. */
		for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
			ASSERT_EQ(ptr[i], i);

		hmm_buffer_free(buffer);
	}
}

/*
 * Read anonymous memory multiple times.
 */
TEST_F(hmm, anon_read_multiple)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	unsigned long c;
	int *ptr;
	int ret;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	for (c = 0; c < NTIMES; c++) {
		buffer = malloc(sizeof(*buffer));
		ASSERT_NE(buffer, NULL);

		buffer->fd = -1;
		buffer->size = size;
		buffer->mirror = malloc(size);
		ASSERT_NE(buffer->mirror, NULL);

		buffer->ptr = mmap(NULL, size,
				   PROT_READ | PROT_WRITE,
				   MAP_PRIVATE | MAP_ANONYMOUS,
				   buffer->fd, 0);
		ASSERT_NE(buffer->ptr, MAP_FAILED);

		/* Initialize buffer in system memory. */
		for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
			ptr[i] = i + c;

		/* Simulate a device reading system memory. */
		ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_READ, buffer,
				      npages);
		ASSERT_EQ(ret, 0);
		ASSERT_EQ(buffer->cpages, npages);
		ASSERT_EQ(buffer->faults, 1);

		/* Check what the device read. */
		for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
			ASSERT_EQ(ptr[i], i + c);

		hmm_buffer_free(buffer);
	}
}

void *unmap_buffer(void *p)
{
	struct hmm_buffer *buffer = p;

	/* Delay for a bit and then unmap buffer while it is being read. */
	hmm_nanosleep(hmm_random() % 32000);
	munmap(buffer->ptr + buffer->size / 2, buffer->size / 2);
	buffer->ptr = NULL;

	return NULL;
}

/*
 * Try reading anonymous memory while it is being unmapped.
 */
TEST_F(hmm, anon_teardown)
{
	unsigned long npages;
	unsigned long size;
	unsigned long c;
	void *ret;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	for (c = 0; c < NTIMES; ++c) {
		pthread_t thread;
		struct hmm_buffer *buffer;
		unsigned long i;
		int *ptr;
		int rc;

		buffer = malloc(sizeof(*buffer));
		ASSERT_NE(buffer, NULL);

		buffer->fd = -1;
		buffer->size = size;
		buffer->mirror = malloc(size);
		ASSERT_NE(buffer->mirror, NULL);

		buffer->ptr = mmap(NULL, size,
				   PROT_READ | PROT_WRITE,
				   MAP_PRIVATE | MAP_ANONYMOUS,
				   buffer->fd, 0);
		ASSERT_NE(buffer->ptr, MAP_FAILED);

		/* Initialize buffer in system memory. */
		for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
			ptr[i] = i + c;

		rc = pthread_create(&thread, NULL, unmap_buffer, buffer);
		ASSERT_EQ(rc, 0);

		/* Simulate a device reading system memory. */
		rc = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_READ, buffer,
				     npages);
		if (rc == 0) {
			ASSERT_EQ(buffer->cpages, npages);
			ASSERT_EQ(buffer->faults, 1);

			/* Check what the device read. */
			for (i = 0, ptr = buffer->mirror;
			     i < size / sizeof(*ptr);
			     ++i)
				ASSERT_EQ(ptr[i], i + c);
		}

		pthread_join(thread, &ret);
		hmm_buffer_free(buffer);
	}
}

/*
 * Test memory snapshot without faulting in pages accessed by the device.
 */
TEST_F(hmm, mixedmap)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned char *m;
	int ret;

	npages = 1;
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(npages);
	ASSERT_NE(buffer->mirror, NULL);


	/* Reserve a range of addresses. */
	buffer->ptr = mmap(NULL, size,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE,
			   self->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/* Simulate a device snapshotting CPU pagetables. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_SNAPSHOT, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);

	/* Check what the device saw. */
	m = buffer->mirror;
	ASSERT_EQ(m[0], HMM_DMIRROR_PROT_READ);

	hmm_buffer_free(buffer);
}

/*
 * Test memory snapshot without faulting in pages accessed by the device.
 */
TEST_F(hmm2, snapshot)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	int *ptr;
	unsigned char *p;
	unsigned char *m;
	int ret;
	int val;

	npages = 7;
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(npages);
	ASSERT_NE(buffer->mirror, NULL);

	/* Reserve a range of addresses. */
	buffer->ptr = mmap(NULL, size,
			   PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);
	p = buffer->ptr;

	/* Punch a hole after the first page address. */
	ret = munmap(buffer->ptr + self->page_size, self->page_size);
	ASSERT_EQ(ret, 0);

	/* Page 2 will be read-only zero page. */
	ret = mprotect(buffer->ptr + 2 * self->page_size, self->page_size,
				PROT_READ);
	ASSERT_EQ(ret, 0);
	ptr = (int *)(buffer->ptr + 2 * self->page_size);
	val = *ptr + 3;
	ASSERT_EQ(val, 3);

	/* Page 3 will be read-only. */
	ret = mprotect(buffer->ptr + 3 * self->page_size, self->page_size,
				PROT_READ | PROT_WRITE);
	ASSERT_EQ(ret, 0);
	ptr = (int *)(buffer->ptr + 3 * self->page_size);
	*ptr = val;
	ret = mprotect(buffer->ptr + 3 * self->page_size, self->page_size,
				PROT_READ);
	ASSERT_EQ(ret, 0);

	/* Page 4-6 will be read-write. */
	ret = mprotect(buffer->ptr + 4 * self->page_size, 3 * self->page_size,
				PROT_READ | PROT_WRITE);
	ASSERT_EQ(ret, 0);
	ptr = (int *)(buffer->ptr + 4 * self->page_size);
	*ptr = val;

	/* Page 5 will be migrated to device 0. */
	buffer->ptr = p + 5 * self->page_size;
	ret = hmm_dmirror_cmd(self->fd0, HMM_DMIRROR_MIGRATE, buffer, 1);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, 1);

	/* Page 6 will be migrated to device 1. */
	buffer->ptr = p + 6 * self->page_size;
	ret = hmm_dmirror_cmd(self->fd1, HMM_DMIRROR_MIGRATE, buffer, 1);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, 1);

	/* Simulate a device snapshotting CPU pagetables. */
	buffer->ptr = p;
	ret = hmm_dmirror_cmd(self->fd0, HMM_DMIRROR_SNAPSHOT, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);

	/* Check what the device saw. */
	m = buffer->mirror;
	ASSERT_EQ(m[0], HMM_DMIRROR_PROT_ERROR);
	ASSERT_EQ(m[1], HMM_DMIRROR_PROT_ERROR);
	ASSERT_EQ(m[2], HMM_DMIRROR_PROT_ZERO | HMM_DMIRROR_PROT_READ);
	ASSERT_EQ(m[3], HMM_DMIRROR_PROT_READ);
	ASSERT_EQ(m[4], HMM_DMIRROR_PROT_WRITE);
	ASSERT_EQ(m[5], HMM_DMIRROR_PROT_DEV_PRIVATE_LOCAL |
			HMM_DMIRROR_PROT_WRITE);
	ASSERT_EQ(m[6], HMM_DMIRROR_PROT_NONE);

	hmm_buffer_free(buffer);
}

#ifdef LOCAL_CONFIG_HAVE_LIBHUGETLBFS
/*
 * Test the hmm_range_fault() HMM_PFN_PMD flag for large pages that
 * should be mapped by a large page table entry.
 */
TEST_F(hmm, compound)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	int *ptr;
	unsigned char *m;
	int ret;
	long pagesizes[4];
	int n, idx;
	unsigned long i;

	/* Skip test if we can't allocate a hugetlbfs page. */

	n = gethugepagesizes(pagesizes, 4);
	if (n <= 0)
		return;
	for (idx = 0; --n > 0; ) {
		if (pagesizes[n] < pagesizes[idx])
			idx = n;
	}
	size = ALIGN(TWOMEG, pagesizes[idx]);
	npages = size >> self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->ptr = get_hugepage_region(size, GHR_STRICT);
	if (buffer->ptr == NULL) {
		free(buffer);
		return;
	}

	buffer->size = size;
	buffer->mirror = malloc(npages);
	ASSERT_NE(buffer->mirror, NULL);

	/* Initialize the pages the device will snapshot in buffer->ptr. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Simulate a device snapshotting CPU pagetables. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_SNAPSHOT, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);

	/* Check what the device saw. */
	m = buffer->mirror;
	for (i = 0; i < npages; ++i)
		ASSERT_EQ(m[i], HMM_DMIRROR_PROT_WRITE |
				HMM_DMIRROR_PROT_PMD);

	/* Make the region read-only. */
	ret = mprotect(buffer->ptr, size, PROT_READ);
	ASSERT_EQ(ret, 0);

	/* Simulate a device snapshotting CPU pagetables. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_SNAPSHOT, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);

	/* Check what the device saw. */
	m = buffer->mirror;
	for (i = 0; i < npages; ++i)
		ASSERT_EQ(m[i], HMM_DMIRROR_PROT_READ |
				HMM_DMIRROR_PROT_PMD);

	free_hugepage_region(buffer->ptr);
	buffer->ptr = NULL;
	hmm_buffer_free(buffer);
}
#endif /* LOCAL_CONFIG_HAVE_LIBHUGETLBFS */

/*
 * Test two devices reading the same memory (double mapped).
 */
TEST_F(hmm2, double_map)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	int *ptr;
	int ret;

	npages = 6;
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(npages);
	ASSERT_NE(buffer->mirror, NULL);

	/* Reserve a range of addresses. */
	buffer->ptr = mmap(NULL, size,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/* Initialize buffer in system memory. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Make region read-only. */
	ret = mprotect(buffer->ptr, size, PROT_READ);
	ASSERT_EQ(ret, 0);

	/* Simulate device 0 reading system memory. */
	ret = hmm_dmirror_cmd(self->fd0, HMM_DMIRROR_READ, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);
	ASSERT_EQ(buffer->faults, 1);

	/* Check what the device read. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	/* Simulate device 1 reading system memory. */
	ret = hmm_dmirror_cmd(self->fd1, HMM_DMIRROR_READ, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);
	ASSERT_EQ(buffer->faults, 1);

	/* Check what the device read. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	/* Punch a hole after the first page address. */
	ret = munmap(buffer->ptr + self->page_size, self->page_size);
	ASSERT_EQ(ret, 0);

	hmm_buffer_free(buffer);
}

/*
 * Basic check of exclusive faulting.
 */
TEST_F(hmm, exclusive)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	int *ptr;
	int ret;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	buffer->ptr = mmap(NULL, size,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/* Initialize buffer in system memory. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Map memory exclusively for device access. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_EXCLUSIVE, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);

	/* Check what the device read. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	/* Fault pages back to system memory and check them. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i]++, i);

	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i+1);

	/* Check atomic access revoked */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_CHECK_EXCLUSIVE, buffer, npages);
	ASSERT_EQ(ret, 0);

	hmm_buffer_free(buffer);
}

TEST_F(hmm, exclusive_mprotect)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	int *ptr;
	int ret;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	buffer->ptr = mmap(NULL, size,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/* Initialize buffer in system memory. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Map memory exclusively for device access. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_EXCLUSIVE, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);

	/* Check what the device read. */
	for (i = 0, ptr = buffer->mirror; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i);

	ret = mprotect(buffer->ptr, size, PROT_READ);
	ASSERT_EQ(ret, 0);

	/* Simulate a device writing system memory. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_WRITE, buffer, npages);
	ASSERT_EQ(ret, -EPERM);

	hmm_buffer_free(buffer);
}

/*
 * Check copy-on-write works.
 */
TEST_F(hmm, exclusive_cow)
{
	struct hmm_buffer *buffer;
	unsigned long npages;
	unsigned long size;
	unsigned long i;
	int *ptr;
	int ret;

	npages = ALIGN(HMM_BUFFER_SIZE, self->page_size) >> self->page_shift;
	ASSERT_NE(npages, 0);
	size = npages << self->page_shift;

	buffer = malloc(sizeof(*buffer));
	ASSERT_NE(buffer, NULL);

	buffer->fd = -1;
	buffer->size = size;
	buffer->mirror = malloc(size);
	ASSERT_NE(buffer->mirror, NULL);

	buffer->ptr = mmap(NULL, size,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   buffer->fd, 0);
	ASSERT_NE(buffer->ptr, MAP_FAILED);

	/* Initialize buffer in system memory. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ptr[i] = i;

	/* Map memory exclusively for device access. */
	ret = hmm_dmirror_cmd(self->fd, HMM_DMIRROR_EXCLUSIVE, buffer, npages);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(buffer->cpages, npages);

	fork();

	/* Fault pages back to system memory and check them. */
	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i]++, i);

	for (i = 0, ptr = buffer->ptr; i < size / sizeof(*ptr); ++i)
		ASSERT_EQ(ptr[i], i+1);

	hmm_buffer_free(buffer);
}

TEST_HARNESS_MAIN
