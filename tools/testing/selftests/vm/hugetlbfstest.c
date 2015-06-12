#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef unsigned long long u64;

static size_t length = 1 << 24;

static u64 read_rss(void)
{
	char buf[4096], *s = buf;
	int i, fd;
	u64 rss;

	fd = open("/proc/self/statm", O_RDONLY);
	assert(fd > 2);
	memset(buf, 0, sizeof(buf));
	read(fd, buf, sizeof(buf) - 1);
	for (i = 0; i < 1; i++)
		s = strchr(s, ' ') + 1;
	rss = strtoull(s, NULL, 10);
	return rss << 12; /* assumes 4k pagesize */
}

static void do_mmap(int fd, int extra_flags, int unmap)
{
	int *p;
	int flags = MAP_PRIVATE | MAP_POPULATE | extra_flags;
	u64 before, after;
	int ret;

	before = read_rss();
	p = mmap(NULL, length, PROT_READ | PROT_WRITE, flags, fd, 0);
	assert(p != MAP_FAILED ||
			!"mmap returned an unexpected error");
	after = read_rss();
	assert(llabs(after - before - length) < 0x40000 ||
			!"rss didn't grow as expected");
	if (!unmap)
		return;
	ret = munmap(p, length);
	assert(!ret || !"munmap returned an unexpected error");
	after = read_rss();
	assert(llabs(after - before) < 0x40000 ||
			!"rss didn't shrink as expected");
}

static int open_file(const char *path)
{
	int fd, err;

	unlink(path);
	fd = open(path, O_CREAT | O_RDWR | O_TRUNC | O_EXCL
			| O_LARGEFILE | O_CLOEXEC, 0600);
	assert(fd > 2);
	unlink(path);
	err = ftruncate(fd, length);
	assert(!err);
	return fd;
}

int main(void)
{
	int hugefd, fd;

	fd = open_file("/dev/shm/hugetlbhog");
	hugefd = open_file("/hugepages/hugetlbhog");

	system("echo 100 > /proc/sys/vm/nr_hugepages");
	do_mmap(-1, MAP_ANONYMOUS, 1);
	do_mmap(fd, 0, 1);
	do_mmap(-1, MAP_ANONYMOUS | MAP_HUGETLB, 1);
	do_mmap(hugefd, 0, 1);
	do_mmap(hugefd, MAP_HUGETLB, 1);
	/* Leak the last one to test do_exit() */
	do_mmap(-1, MAP_ANONYMOUS | MAP_HUGETLB, 0);
	printf("oll korrekt.\n");
	return 0;
}
