/*
 * Stress test for transparent huge pages, memory compaction and migration.
 *
 * Authors: Konstantin Khlebnikov <koct9i@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <err.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include "util.h"

int backing_fd = -1;
int mmap_flags = MAP_ANONYMOUS | MAP_NORESERVE | MAP_PRIVATE;
#define PROT_RW (PROT_READ | PROT_WRITE)

int main(int argc, char **argv)
{
	size_t ram, len;
	void *ptr, *p;
	struct timespec a, b;
	int i = 0;
	char *name = NULL;
	double s;
	uint8_t *map;
	size_t map_len;
	int pagemap_fd;

	ram = sysconf(_SC_PHYS_PAGES);
	if (ram > SIZE_MAX / sysconf(_SC_PAGESIZE) / 4)
		ram = SIZE_MAX / 4;
	else
		ram *= sysconf(_SC_PAGESIZE);
	len = ram;

	while (++i < argc) {
		if (!strcmp(argv[i], "-h"))
			errx(1, "usage: %s [size in MiB]", argv[0]);
		else if (!strcmp(argv[i], "-f"))
			name = argv[++i];
		else
			len = atoll(argv[i]) << 20;
	}

	if (name) {
		backing_fd = open(name, O_RDWR);
		if (backing_fd == -1)
			errx(2, "open %s", name);
		mmap_flags = MAP_SHARED;
	}

	warnx("allocate %zd transhuge pages, using %zd MiB virtual memory"
	      " and %zd MiB of ram", len >> HPAGE_SHIFT, len >> 20,
	      ram >> (20 + HPAGE_SHIFT - PAGE_SHIFT - 1));

	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd < 0)
		err(2, "open pagemap");

	len -= len % HPAGE_SIZE;
	ptr = mmap(NULL, len + HPAGE_SIZE, PROT_RW, mmap_flags, backing_fd, 0);
	if (ptr == MAP_FAILED)
		err(2, "initial mmap");
	ptr += HPAGE_SIZE - (uintptr_t)ptr % HPAGE_SIZE;

	if (madvise(ptr, len, MADV_HUGEPAGE))
		err(2, "MADV_HUGEPAGE");

	map_len = ram >> (HPAGE_SHIFT - 1);
	map = malloc(map_len);
	if (!map)
		errx(2, "map malloc");

	while (1) {
		int nr_succeed = 0, nr_failed = 0, nr_pages = 0;

		memset(map, 0, map_len);

		clock_gettime(CLOCK_MONOTONIC, &a);
		for (p = ptr; p < ptr + len; p += HPAGE_SIZE) {
			int64_t pfn;

			pfn = allocate_transhuge(p, pagemap_fd);

			if (pfn < 0) {
				nr_failed++;
			} else {
				size_t idx = pfn >> (HPAGE_SHIFT - PAGE_SHIFT);

				nr_succeed++;
				if (idx >= map_len) {
					map = realloc(map, idx + 1);
					if (!map)
						errx(2, "map realloc");
					memset(map + map_len, 0, idx + 1 - map_len);
					map_len = idx + 1;
				}
				if (!map[idx])
					nr_pages++;
				map[idx] = 1;
			}

			/* split transhuge page, keep last page */
			if (madvise(p, HPAGE_SIZE - PAGE_SIZE, MADV_DONTNEED))
				err(2, "MADV_DONTNEED");
		}
		clock_gettime(CLOCK_MONOTONIC, &b);
		s = b.tv_sec - a.tv_sec + (b.tv_nsec - a.tv_nsec) / 1000000000.;

		warnx("%.3f s/loop, %.3f ms/page, %10.3f MiB/s\t"
		      "%4d succeed, %4d failed, %4d different pages",
		      s, s * 1000 / (len >> HPAGE_SHIFT), len / s / (1 << 20),
		      nr_succeed, nr_failed, nr_pages);
	}
}
