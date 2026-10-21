// SPDX-License-Identifier: GPL-2.0
/* Exercise re-entry into call_srcu() from BPF; see progs/rcu_reentry.c. */
#define _GNU_SOURCE
#include <sched.h>
#include <test_progs.h>
#include "task_local_storage_helpers.h"
#include "trace_helpers.h"
#include "rcu_reentry.skel.h"

/* Tiny RCU has no rcu_segcblist_enqueue() to attach to. */
static bool have_attach_target(void)
{
	unsigned long long addr;

	return kallsyms_find("rcu_segcblist_enqueue", &addr) == 0;
}

/* Tiny SRCU stubs out srcu_expedite_current(); Tree SRCU exports it. */
static bool have_tree_srcu(void)
{
	unsigned long long addr;

	return kallsyms_find("srcu_expedite_current", &addr) == 0;
}

void test_rcu_reentry(void)
{
	struct rcu_reentry *skel;
	int err, pidfd = -1, map_fd;
	cpu_set_t set, old_set;
	bool affinity_saved;
	__u64 val = 1;
	int cpu;

	if (!have_attach_target()) {
		test__skip();
		return;
	}

	skel = rcu_reentry__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	err = rcu_reentry__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	/* Keep the re-entry on a single CPU; a cpuset may exclude CPU 0. */
	affinity_saved = !sched_getaffinity(0, sizeof(old_set), &old_set);
	cpu = sched_getcpu();
	if (!ASSERT_GE(cpu, 0, "getcpu"))
		goto out;
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (!ASSERT_OK(sched_setaffinity(0, sizeof(set), &set), "setaffinity"))
		goto out;

	pidfd = sys_pidfd_open(getpid(), 0);
	if (!ASSERT_GE(pidfd, 0, "pidfd_open"))
		goto restore;
	map_fd = bpf_map__fd(skel->maps.task_stg);
	err = bpf_map_update_elem(map_fd, &pidfd, &val, BPF_NOEXIST);
	if (!ASSERT_OK(err, "boot_create"))
		goto restore;

	/* Arm the handler for this thread, then trigger call_rcu_tasks_trace(). */
	skel->bss->target_pid = syscall(__NR_gettid);
	err = bpf_map_delete_elem(map_fd, &pidfd);
	if (!ASSERT_OK(err, "boot_delete"))
		goto restore;

	/*
	 * Only Tree SRCU reaches rcu_segcblist_enqueue() from call_srcu(); a
	 * UP+PREEMPT kernel pairs Tree RCU with Tiny SRCU, so the attach
	 * succeeds but nothing fires.  On Tree SRCU it must fire.
	 */
	if (!skel->bss->hits) {
		if (have_tree_srcu())
			ASSERT_GT(skel->bss->hits, 0, "prog_fired");
		else
			test__skip();
		goto restore;
	}
	ASSERT_EQ(skel->bss->get_errs, 0, "nested_storage_get");
	ASSERT_EQ(skel->bss->del_errs, 0, "nested_storage_delete");
restore:
	if (affinity_saved)
		sched_setaffinity(0, sizeof(old_set), &old_set);
out:
	if (pidfd >= 0)
		close(pidfd);
	rcu_reentry__destroy(skel);
}
