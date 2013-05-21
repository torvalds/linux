#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

typedef unsigned long long u64;

#define PME_PRESENT	(1ULL << 63)
#define PME_SOFT_DIRTY	(1Ull << 55)

#define PAGES_TO_TEST	3
#ifndef PAGE_SIZE
#define PAGE_SIZE	4096
#endif

static void get_pagemap2(char *mem, u64 *map)
{
	int fd;

	fd = open("/proc/self/pagemap2", O_RDONLY);
	if (fd < 0) {
		perror("Can't open pagemap2");
		exit(1);
	}

	lseek(fd, (unsigned long)mem / PAGE_SIZE * sizeof(u64), SEEK_SET);
	read(fd, map, sizeof(u64) * PAGES_TO_TEST);
	close(fd);
}

static inline char map_p(u64 map)
{
	return map & PME_PRESENT ? 'p' : '-';
}

static inline char map_sd(u64 map)
{
	return map & PME_SOFT_DIRTY ? 'd' : '-';
}

static int check_pte(int step, int page, u64 *map, u64 want)
{
	if ((map[page] & want) != want) {
		printf("Step %d Page %d has %c%c, want %c%c\n",
				step, page,
				map_p(map[page]), map_sd(map[page]),
				map_p(want), map_sd(want));
		return 1;
	}

	return 0;
}

static void clear_refs(void)
{
	int fd;
	char *v = "4";

	fd = open("/proc/self/clear_refs", O_WRONLY);
	if (write(fd, v, 3) < 3) {
		perror("Can't clear soft-dirty bit");
		exit(1);
	}
	close(fd);
}

int main(void)
{
	char *mem, x;
	u64 map[PAGES_TO_TEST];

	mem = mmap(NULL, PAGES_TO_TEST * PAGE_SIZE,
			PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, 0, 0);

	x = mem[0];
	mem[2 * PAGE_SIZE] = 'c';
	get_pagemap2(mem, map);

	if (check_pte(1, 0, map, PME_PRESENT))
		return 1;
	if (check_pte(1, 1, map, 0))
		return 1;
	if (check_pte(1, 2, map, PME_PRESENT | PME_SOFT_DIRTY))
		return 1;

	clear_refs();
	get_pagemap2(mem, map);

	if (check_pte(2, 0, map, PME_PRESENT))
		return 1;
	if (check_pte(2, 1, map, 0))
		return 1;
	if (check_pte(2, 2, map, PME_PRESENT))
		return 1;

	mem[0] = 'a';
	mem[PAGE_SIZE] = 'b';
	x = mem[2 * PAGE_SIZE];
	get_pagemap2(mem, map);

	if (check_pte(3, 0, map, PME_PRESENT | PME_SOFT_DIRTY))
		return 1;
	if (check_pte(3, 1, map, PME_PRESENT | PME_SOFT_DIRTY))
		return 1;
	if (check_pte(3, 2, map, PME_PRESENT))
		return 1;

	(void)x; /* gcc warn */

	printf("PASS\n");
	return 0;
}
