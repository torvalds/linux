// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <sys/user.h>
#ifndef PAGE_SIZE /* on some archs it comes in sys/user.h */
#include <unistd.h>
#define PAGE_SIZE getpagesize()
#endif

#include "arena_mem_usage.skel.h"

/*
 * arena_map_mem_usage() is surfaced to user space through the map's
 * /proc/<pid>/fdinfo/<fd> "memlock:" line (the same value bpftool map show
 * prints). Read it directly so the test has no external dependency.
 */
static long map_memlock(int map_fd)
{
	char path[64], line[128];
	long memlock = -1;
	FILE *f;

	snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", map_fd);
	f = fopen(path, "r");
	if (!ASSERT_OK_PTR(f, "open_fdinfo"))
		return -1;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "memlock:\t%ld", &memlock) == 1)
			break;
	}
	fclose(f);
	ASSERT_NEQ(memlock, -1, "parse_memlock");
	return memlock;
}

static int run(struct bpf_program *prog, const char *name)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	int err = bpf_prog_test_run_opts(bpf_program__fd(prog), &opts);

	if (!ASSERT_OK(err, name))
		return -1;
	if (!ASSERT_OK(opts.retval, name))
		return -1;
	return 0;
}

void serial_test_arena_mem_usage(void)
{
	struct arena_mem_usage *skel;
	const long ps = PAGE_SIZE;
	char *base;
	size_t sz;
	int fd, i;

	skel = arena_mem_usage__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_load"))
		return;
	fd = bpf_map__fd(skel->maps.arena);

	/* Fresh arena: no data pages, and the scratch page is not counted. */
	ASSERT_EQ(map_memlock(fd), 0, "initial");

	/* BPF-side allocation of 17 pages. */
	skel->bss->alloc_cnt = 17;
	if (run(skel->progs.alloc, "alloc"))
		goto out;
	/*
	 * A NULL ptr means bpf_arena_alloc_pages() itself failed (e.g. the host
	 * is under memory pressure), not a miscount -- flag it distinctly so a
	 * red CI run is not mistaken for a counting bug.
	 */
	if (!ASSERT_OK_PTR(skel->bss->ptr, "arena_alloc_pages"))
		goto out;
	ASSERT_EQ(map_memlock(fd), 17 * ps, "after_alloc");

	/* Free a single page (arena_free_pages page_cnt==1 path). */
	skel->bss->free_byte_off = 0;
	skel->bss->free_cnt = 1;
	if (run(skel->progs.free_pages, "free_one"))
		goto out;
	ASSERT_EQ(map_memlock(fd), 16 * ps, "after_free_one");

	/* Free ten pages in one call (bulk path); only the freed pages count. */
	skel->bss->free_byte_off = 1 * ps;
	skel->bss->free_cnt = 10;
	if (run(skel->progs.free_pages, "free_bulk"))
		goto out;
	ASSERT_EQ(map_memlock(fd), 6 * ps, "after_free_bulk");

	/* Free the remaining six -> arena empty again. */
	skel->bss->free_byte_off = 11 * ps;
	skel->bss->free_cnt = 6;
	if (run(skel->progs.free_pages, "free_rest"))
		goto out;
	ASSERT_EQ(map_memlock(fd), 0, "after_free_rest");

	/*
	 * User-space fault-in: touching unallocated arena pages allocates them
	 * through arena_vm_fault(). libbpf mmap()s the arena at map_extra during
	 * load, so bpf_map__initial_value() hands back that base.
	 */
	base = bpf_map__initial_value(skel->maps.arena, &sz);
	if (!ASSERT_OK_PTR(base, "arena_base"))
		goto out;
	for (i = 0; i < 8; i++)
		base[i * ps] = 1;
	ASSERT_EQ(map_memlock(fd), 8 * ps, "after_faultin");

	/*
	 * Free the faulted-in pages from BPF. They are mapped into the user vma
	 * (elevated refcount), so this also exercises the zap path.
	 */
	skel->bss->ptr = base;
	skel->bss->free_byte_off = 0;
	skel->bss->free_cnt = 8;
	if (run(skel->progs.free_pages, "free_faulted"))
		goto out;
	ASSERT_EQ(map_memlock(fd), 0, "after_free_faulted");
out:
	arena_mem_usage__destroy(skel);
}
