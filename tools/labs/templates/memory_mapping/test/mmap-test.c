/*
 * PSO - Memory Mapping Lab (#11)
 *
 * Exercise #1, #2: memory mapping between user-space and kernel-space
 *
 * test case
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "mmap-test.h"

#define NPAGES		16
#define MMAP_DEV	"/dev/mymmap"
#define PROC_ENTRY_PATH "/proc/" PROC_ENTRY_NAME

void test_contents(unsigned char *addr,
		unsigned char value1, unsigned char value2,
		unsigned char value3, unsigned char value4)
{
	int i;

	for (i = 0; i < NPAGES * getpagesize(); i += getpagesize()) {
		if (addr[i] != value1 || addr[i + 1] != value2 ||
				addr[i + 2] != value3 || addr[i + 3] != value4)
			printf("0x%x 0x%x 0x%x 0x%x\n", addr[i], addr[i+1],
					addr[i+2], addr[i+3]);
		else
			printf("matched\n");
	}
}

int test_read_write(int fd, unsigned char *mmap_addr)
{
	unsigned char *local_addr;
	int len = NPAGES * getpagesize(), i;

	printf("\nWrite test ...\n");
	/* alloc local memory */
	local_addr = malloc(len);
	if (!local_addr)
		return -1;

	/* init local memory */
	memset(local_addr, 0, len);
	for (i = 0; i < NPAGES * getpagesize(); i += getpagesize()) {
		local_addr[i]   = 0xa0;
		local_addr[i+1] = 0xb0;
		local_addr[i+2] = 0xc0;
		local_addr[i+3] = 0xd0;
	}

	/* write to device */
	write(fd, local_addr, len);

	/* are these values in mapped memory? */
	test_contents(mmap_addr, 0xa0, 0xb0, 0xc0, 0xd0);

	printf("\nRead test ...\n");
	memset(local_addr, 0, len);
	/* read from device */
	read(fd, local_addr, len);
	/* are the values read correct? */
	test_contents(local_addr, 0xa0, 0xb0, 0xc0, 0xd0);
	return 0;
}

static int show_mem_usage(void)
{
	int fd, ret;
	char buf[40];
	unsigned long mem_usage;

	fd = open(PROC_ENTRY_PATH, O_RDONLY);
	if (fd < 0) {
		perror("open " PROC_ENTRY_PATH);
		ret = fd;
		goto out;
	}

	ret = read(fd, buf, sizeof buf);
	if (ret < 0)
		goto no_read;

	sscanf(buf, "%lu", &mem_usage);
	buf[ret] = 0;

	printf("Memory usage: %lu\n", mem_usage);

	ret = mem_usage;
no_read:
	close(fd);
out:
	return ret;
}

int main(int argc, const char **argv)
{
	int fd, test;
	unsigned char *addr;
	int len = NPAGES * getpagesize();
	int i;
	unsigned long usage_before_mmap, usage_after_mmap;

	if (argc > 1)
		test = atoi(argv[1]); 

	assert(system("mknod " MMAP_DEV " c 42 0") == 0);

	fd = open(MMAP_DEV, O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("open");
		assert(system("rm " MMAP_DEV) == 0);
		exit(EXIT_FAILURE);
	}

	addr = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		assert(system("rm " MMAP_DEV) == 0);
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < NPAGES * getpagesize(); i += getpagesize()) {
		if (addr[i] != 0xaa || addr[i + 1] != 0xbb ||
				addr[i + 2] != 0xcc || addr[i + 3] != 0xdd)
			printf("0x%x 0x%x 0x%x 0x%x\n", addr[i], addr[i+1],
					addr[i+2], addr[i+3]);
		else
			printf("matched\n");
	}


	if (test >= 2 && test_read_write(fd, addr)) {
		perror("read/write test");
		assert(system("rm " MMAP_DEV) == 0);
		exit(EXIT_FAILURE);
	}

	if (test >= 3) {
		usage_before_mmap = show_mem_usage();
		if (usage_before_mmap < 0)
			printf("failed to show memory usage\n");

		#define SIZE (10 * 1024 * 1024)
		addr = mmap(NULL, SIZE, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		if (addr == MAP_FAILED)
			perror("mmap_");

		usage_after_mmap = show_mem_usage();
		if (usage_after_mmap < 0)
			printf("failed to show memory usage\n");
		printf("mmaped :%lu MB\n",
		       (usage_after_mmap - usage_before_mmap) >> 20);

		sleep(30);

		munmap(addr, SIZE);
	}

	close(fd);

	assert(system("rm " MMAP_DEV) == 0);

	return 0;
}
