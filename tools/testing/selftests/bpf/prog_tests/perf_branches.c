// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <test_progs.h>
#include "bpf/libbpf_internal.h"
#include "test_perf_branches.skel.h"

static void check_good_sample(struct test_perf_branches *skel)
{
	int written_global = skel->bss->written_global_out;
	int required_size = skel->bss->required_size_out;
	int written_stack = skel->bss->written_stack_out;
	int pbe_size = sizeof(struct perf_branch_entry);
	int duration = 0;

	if (CHECK(!skel->bss->valid, "output not valid",
		 "no valid sample from prog"))
		return;

	/*
	 * It's hard to validate the contents of the branch entries b/c it
	 * would require some kind of disassembler and also encoding the
	 * valid jump instructions for supported architectures. So just check
	 * the easy stuff for now.
	 */
	CHECK(required_size <= 0, "read_branches_size", "err %d\n", required_size);
	CHECK(written_stack < 0, "read_branches_stack", "err %d\n", written_stack);
	CHECK(written_stack % pbe_size != 0, "read_branches_stack",
	      "stack bytes written=%d not multiple of struct size=%d\n",
	      written_stack, pbe_size);
	CHECK(written_global < 0, "read_branches_global", "err %d\n", written_global);
	CHECK(written_global % pbe_size != 0, "read_branches_global",
	      "global bytes written=%d not multiple of struct size=%d\n",
	      written_global, pbe_size);
	CHECK(written_global < written_stack, "read_branches_size",
	      "written_global=%d < written_stack=%d\n", written_global, written_stack);
}

static void check_bad_sample(struct test_perf_branches *skel)
{
	int written_global = skel->bss->written_global_out;
	int required_size = skel->bss->required_size_out;
	int written_stack = skel->bss->written_stack_out;
	int duration = 0;

	if (CHECK(!skel->bss->valid, "output not valid",
		 "no valid sample from prog"))
		return;

	CHECK((required_size != -EINVAL && required_size != -ENOENT),
	      "read_branches_size", "err %d\n", required_size);
	CHECK((written_stack != -EINVAL && written_stack != -ENOENT),
	      "read_branches_stack", "written %d\n", written_stack);
	CHECK((written_global != -EINVAL && written_global != -ENOENT),
	      "read_branches_global", "written %d\n", written_global);
}

static void test_perf_branches_common(int perf_fd,
				      void (*cb)(struct test_perf_branches *))
{
	struct test_perf_branches *skel;
	int err, i, duration = 0;
	bool detached = false;
	struct bpf_link *link;
	volatile int j = 0;
	cpu_set_t cpu_set;

	skel = test_perf_branches__open_and_load();
	if (CHECK(!skel, "test_perf_branches_load",
		  "perf_branches skeleton failed\n"))
		return;

	/* attach perf_event */
	link = bpf_program__attach_perf_event(skel->progs.perf_branches, perf_fd);
	if (!ASSERT_OK_PTR(link, "attach_perf_event"))
		goto out_destroy_skel;

	/* generate some branches on cpu 0 */
	CPU_ZERO(&cpu_set);
	CPU_SET(0, &cpu_set);
	err = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set);
	if (CHECK(err, "set_affinity", "cpu #0, err %d\n", err))
		goto out_destroy;
	/* spin the loop for a while (random high number) */
	for (i = 0; i < 1000000; ++i)
		++j;

	test_perf_branches__detach(skel);
	detached = true;

	cb(skel);
out_destroy:
	bpf_link__destroy(link);
out_destroy_skel:
	if (!detached)
		test_perf_branches__detach(skel);
	test_perf_branches__destroy(skel);
}

static void test_perf_branches_hw(void)
{
	struct perf_event_attr attr = {0};
	int duration = 0;
	int pfd;

	/* create perf event */
	attr.size = sizeof(attr);
	attr.type = PERF_TYPE_HARDWARE;
	attr.config = PERF_COUNT_HW_CPU_CYCLES;
	attr.freq = 1;
	attr.sample_freq = 1000;
	attr.sample_type = PERF_SAMPLE_BRANCH_STACK;
	attr.branch_sample_type = PERF_SAMPLE_BRANCH_USER | PERF_SAMPLE_BRANCH_ANY;
	pfd = syscall(__NR_perf_event_open, &attr, -1, 0, -1, PERF_FLAG_FD_CLOEXEC);

	/*
	 * Some setups don't support branch records (virtual machines, !x86),
	 * so skip test in this case.
	 */
	if (pfd < 0) {
		if (errno == ENOENT || errno == EOPNOTSUPP) {
			printf("%s:SKIP:no PERF_SAMPLE_BRANCH_STACK\n",
			       __func__);
			test__skip();
			return;
		}
		if (CHECK(pfd < 0, "perf_event_open", "err %d errno %d\n",
			  pfd, errno))
			return;
	}

	test_perf_branches_common(pfd, check_good_sample);

	close(pfd);
}

/*
 * Tests negative case -- run bpf_read_branch_records() on improperly configured
 * perf event.
 */
static void test_perf_branches_no_hw(void)
{
	struct perf_event_attr attr = {0};
	int duration = 0;
	int pfd;

	/* create perf event */
	attr.size = sizeof(attr);
	attr.type = PERF_TYPE_SOFTWARE;
	attr.config = PERF_COUNT_SW_CPU_CLOCK;
	attr.freq = 1;
	attr.sample_freq = 1000;
	pfd = syscall(__NR_perf_event_open, &attr, -1, 0, -1, PERF_FLAG_FD_CLOEXEC);
	if (CHECK(pfd < 0, "perf_event_open", "err %d\n", pfd))
		return;

	test_perf_branches_common(pfd, check_bad_sample);

	close(pfd);
}

void test_perf_branches(void)
{
	if (test__start_subtest("perf_branches_hw"))
		test_perf_branches_hw();
	if (test__start_subtest("perf_branches_no_hw"))
		test_perf_branches_no_hw();
}
