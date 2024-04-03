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
#include "vm_util.h"
#include "../kselftest.h"

int backing_fd = -1;
int mmap_flags = MAP_ANONYMOUS | MAP_NORESERVE | MAP_PRIVATE;
#define PROT_RW (PROT_READ | PROT_WRITE)

int main(int argc, char **argv)
{
	size_t ram, len;
	void *ptr, *p;
	struct timespec start, a, b;
	int i = 0;
	char *name = NULL;
	double s;
	uint8_t *map;
	size_t map_len;
	int pagemap_fd;
	int duration = 0;

	ksft_print_header();

	ram = sysconf(_SC_PHYS_PAGES);
	if (ram > SIZE_MAX / psize() / 4)
		ram = SIZE_MAX / 4;
	else
		ram *= psize();
	len = ram;

	while (++i < argc) {
		if (!strcmp(argv[i], "-h"))
			ksft_exit_fail_msg("usage: %s [-f <filename>] [-d <duration>] [size in MiB]\n",
					   argv[0]);
		else if (!strcmp(argv[i], "-f"))
			name = argv[++i];
		else if (!strcmp(argv[i], "-d"))
			duration = atoi(argv[++i]);
		else
			len = atoll(argv[i]) << 20;
	}

	ksft_set_plan(1);

	if (name) {
		backing_fd = open(name, O_RDWR);
		if (backing_fd == -1)
			ksft_exit_fail_msg("open %s\n", name);
		mmap_flags = MAP_SHARED;
	}

	warnx("allocate %zd transhuge pages, using %zd MiB virtual memory"
	      " and %zd MiB of ram", len >> HPAGE_SHIFT, len >> 20,
	      ram >> (20 + HPAGE_SHIFT - pshift() - 1));

	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd < 0)
		ksft_exit_fail_msg("open pagemap\n");

	len -= len % HPAGE_SIZE;
	ptr = mmap(NULL, len + HPAGE_SIZE, PROT_RW, mmap_flags, backing_fd, 0);
	if (ptr == MAP_FAILED)
		ksft_exit_fail_msg("initial mmap");
	ptr += HPAGE_SIZE - (uintptr_t)ptr % HPAGE_SIZE;

	if (madvise(ptr, len, MADV_HUGEPAGE))
		ksft_exit_fail_msg("MADV_HUGEPAGE");

	map_len = ram >> (HPAGE_SHIFT - 1);
	map = malloc(map_len);
	if (!map)
		ksft_exit_fail_msg("map malloc\n");

	clock_gettime(CLOCK_MONOTONIC, &start);

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
				size_t idx = pfn >> (HPAGE_SHIFT - pshift());

				nr_succeed++;
				if (idx >= map_len) {
					map = realloc(map, idx + 1);
					if (!map)
						ksft_exit_fail_msg("map realloc\n");
					memset(map + map_len, 0, idx + 1 - map_len);
					map_len = idx + 1;
				}
				if (!map[idx])
					nr_pages++;
				map[idx] = 1;
			}

			/* split transhuge page, keep last page */
			if (madvise(p, HPAGE_SIZE - psize(), MADV_DONTNEED))
				ksft_exit_fail_msg("MADV_DONTNEED");
		}
		clock_gettime(CLOCK_MONOTONIC, &b);
		s = b.tv_sec - a.tv_sec + (b.tv_nsec - a.tv_nsec) / 1000000000.;

		ksft_print_msg("%.3f s/loop, %.3f ms/page, %10.3f MiB/s\t"
			       "%4d succeed, %4d failed, %4d different pages\n",
			       s, s * 1000 / (len >> HPAGE_SHIFT), len / s / (1 << 20),
			       nr_succeed, nr_failed, nr_pages);

		if (duration > 0 && b.tv_sec - start.tv_sec >= duration) {
			ksft_test_result_pass("Completed\n");
			ksft_finished();
		}
	}
}
