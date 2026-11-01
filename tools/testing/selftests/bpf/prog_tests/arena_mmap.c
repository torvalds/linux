// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <sys/mman.h>

#define ARENA_PAGES 8

static void test_refused(int pages, int flags, int err)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts, .map_flags = BPF_F_MMAPABLE);
	void *area;
	int fd;

	fd = bpf_map_create(BPF_MAP_TYPE_ARENA, "arena", 0, 0, ARENA_PAGES, &opts);
	if (fd == -EOPNOTSUPP) {
		test__skip();
		return;
	}
	if (!ASSERT_GE(fd, 0, "bpf_map_create"))
		return;

	area = mmap(NULL, pages * getpagesize(), PROT_READ | PROT_WRITE,
		    MAP_SHARED | flags, fd, 0);
	if (ASSERT_EQ(area, MAP_FAILED, "mmap"))
		ASSERT_EQ(errno, err, "errno");
	else
		munmap(area, pages * getpagesize());
	close(fd);
}

void test_arena_mmap(void)
{
	if (test__start_subtest("long"))
		test_refused(ARENA_PAGES + 1, 0, E2BIG);
	if (test__start_subtest("address_0"))
		test_refused(ARENA_PAGES, MAP_FIXED, EINVAL);
}
