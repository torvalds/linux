#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "../../../../mm/gup_test.h"

#define MB (1UL << 20)
#define PAGE_SIZE sysconf(_SC_PAGESIZE)

/* Just the flags we need, copied from mm.h: */
#define FOLL_WRITE	0x01	/* check pte is writable */

int main(int argc, char **argv)
{
	struct gup_test gup;
	unsigned long size = 128 * MB;
	int i, fd, filed, opt, nr_pages = 1, thp = -1, repeats = 1, write = 0;
	int cmd = GUP_FAST_BENCHMARK, flags = MAP_PRIVATE;
	char *file = "/dev/zero";
	char *p;

	while ((opt = getopt(argc, argv, "m:r:n:f:abtTLUuwSH")) != -1) {
		switch (opt) {
		case 'a':
			cmd = PIN_FAST_BENCHMARK;
			break;
		case 'b':
			cmd = PIN_BENCHMARK;
			break;
		case 'L':
			cmd = PIN_LONGTERM_BENCHMARK;
			break;
		case 'm':
			size = atoi(optarg) * MB;
			break;
		case 'r':
			repeats = atoi(optarg);
			break;
		case 'n':
			nr_pages = atoi(optarg);
			break;
		case 't':
			thp = 1;
			break;
		case 'T':
			thp = 0;
			break;
		case 'U':
			cmd = GUP_BENCHMARK;
			break;
		case 'u':
			cmd = GUP_FAST_BENCHMARK;
			break;
		case 'w':
			write = 1;
			break;
		case 'f':
			file = optarg;
			break;
		case 'S':
			flags &= ~MAP_PRIVATE;
			flags |= MAP_SHARED;
			break;
		case 'H':
			flags |= (MAP_HUGETLB | MAP_ANONYMOUS);
			break;
		default:
			return -1;
		}
	}

	filed = open(file, O_RDWR|O_CREAT);
	if (filed < 0) {
		perror("open");
		exit(filed);
	}

	gup.nr_pages_per_call = nr_pages;
	if (write)
		gup.flags |= FOLL_WRITE;

	fd = open("/sys/kernel/debug/gup_test", O_RDWR);
	if (fd == -1) {
		perror("open");
		exit(1);
	}

	p = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, filed, 0);
	if (p == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	gup.addr = (unsigned long)p;

	if (thp == 1)
		madvise(p, size, MADV_HUGEPAGE);
	else if (thp == 0)
		madvise(p, size, MADV_NOHUGEPAGE);

	for (; (unsigned long)p < gup.addr + size; p += PAGE_SIZE)
		p[0] = 0;

	for (i = 0; i < repeats; i++) {
		gup.size = size;
		if (ioctl(fd, cmd, &gup)) {
			perror("ioctl");
			exit(1);
		}

		printf("Time: get:%lld put:%lld us", gup.get_delta_usec,
			gup.put_delta_usec);
		if (gup.size != size)
			printf(", truncated (size: %lld)", gup.size);
		printf("\n");
	}

	return 0;
}
