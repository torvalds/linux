#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/types.h>

#define MB (1UL << 20)
#define PAGE_SIZE sysconf(_SC_PAGESIZE)

#define GUP_FAST_BENCHMARK	_IOWR('g', 1, struct gup_benchmark)
#define GUP_LONGTERM_BENCHMARK	_IOWR('g', 2, struct gup_benchmark)
#define GUP_BENCHMARK		_IOWR('g', 3, struct gup_benchmark)

struct gup_benchmark {
	__u64 get_delta_usec;
	__u64 put_delta_usec;
	__u64 addr;
	__u64 size;
	__u32 nr_pages_per_call;
	__u32 flags;
};

int main(int argc, char **argv)
{
	struct gup_benchmark gup;
	unsigned long size = 128 * MB;
	int i, fd, opt, nr_pages = 1, thp = -1, repeats = 1, write = 0;
	int cmd = GUP_FAST_BENCHMARK;
	char *p;

	while ((opt = getopt(argc, argv, "m:r:n:tTLU")) != -1) {
		switch (opt) {
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
		case 'L':
			cmd = GUP_LONGTERM_BENCHMARK;
			break;
		case 'U':
			cmd = GUP_BENCHMARK;
			break;
		case 'w':
			write = 1;
		default:
			return -1;
		}
	}

	gup.nr_pages_per_call = nr_pages;
	gup.flags = write;

	fd = open("/sys/kernel/debug/gup_benchmark", O_RDWR);
	if (fd == -1)
		perror("open"), exit(1);

	p = mmap(NULL, size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED)
		perror("mmap"), exit(1);
	gup.addr = (unsigned long)p;

	if (thp == 1)
		madvise(p, size, MADV_HUGEPAGE);
	else if (thp == 0)
		madvise(p, size, MADV_NOHUGEPAGE);

	for (; (unsigned long)p < gup.addr + size; p += PAGE_SIZE)
		p[0] = 0;

	for (i = 0; i < repeats; i++) {
		gup.size = size;
		if (ioctl(fd, cmd, &gup))
			perror("ioctl"), exit(1);

		printf("Time: get:%lld put:%lld us", gup.get_delta_usec,
			gup.put_delta_usec);
		if (gup.size != size)
			printf(", truncated (size: %lld)", gup.size);
		printf("\n");
	}

	return 0;
}
