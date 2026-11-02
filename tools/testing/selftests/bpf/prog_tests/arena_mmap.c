// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <sys/mman.h>

#define ARENA_PAGES 8

/* err == 0: the range ends above TASK_SIZE. EINVAL or ENOMEM, this depends on the arch */
static void test_refused(__u64 map_extra, __u64 addr, int pages, int flags, int err)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		    .map_flags = BPF_F_MMAPABLE,
		    .map_extra = map_extra);
	void *area;
	int fd;

	fd = bpf_map_create(BPF_MAP_TYPE_ARENA, "arena", 0, 0, ARENA_PAGES, &opts);
	if (fd == -EOPNOTSUPP) {
		test__skip();
		return;
	}
	/* No task can map this range, so the kernel can also refuse the map. */
	if (fd < 0 && map_extra)
		return;
	if (!ASSERT_GE(fd, 0, "bpf_map_create"))
		return;

	area = mmap((void *)(unsigned long)addr, pages * getpagesize(),
		    PROT_READ | PROT_WRITE, MAP_SHARED | flags, fd, 0);
	if (!ASSERT_EQ(area, MAP_FAILED, "mmap"))
		munmap(area, pages * getpagesize());
	else if (err || errno != ENOMEM)
		ASSERT_EQ(errno, err ?: EINVAL, "errno");
	close(fd);
}

void serial_test_arena_mmap(void)
{
	__u64 page = getpagesize(), last = -(ARENA_PAGES * page);

	if (test__start_subtest("long"))
		test_refused(0, 0, ARENA_PAGES + 1, 0, E2BIG);
	if (test__start_subtest("address_0"))
		test_refused(0, 0, ARENA_PAGES, MAP_FIXED, EINVAL);
	if (test__start_subtest("crosses_4g"))
		test_refused(0, (1ULL << 32) - page, ARENA_PAGES, MAP_FIXED, EINVAL);
	if (test__start_subtest("crosses_4g_noreplace"))
		test_refused(0, (1ULL << 32) - page, ARENA_PAGES, MAP_FIXED_NOREPLACE, EINVAL);
	if (test__start_subtest("wraps"))
		test_refused(0, -page, ARENA_PAGES, MAP_FIXED, 0);
	if (test__start_subtest("map_extra_last_pages"))
		test_refused(last, last, ARENA_PAGES, MAP_FIXED, 0);
}
