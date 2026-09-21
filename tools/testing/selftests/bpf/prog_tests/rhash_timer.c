// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <sched.h>

#include <test_progs.h>
#include "rhash_timer.skel.h"

#define MAX_ATTEMPTS 256
#define RCU_SYNC_INTERVAL 64

static int pin_to_first_cpu(cpu_set_t *old_mask)
{
	cpu_set_t new_mask;
	int cpu;

	if (sched_getaffinity(0, sizeof(*old_mask), old_mask))
		return -errno;

	for (cpu = 0; cpu < CPU_SETSIZE; cpu++)
		if (CPU_ISSET(cpu, old_mask))
			break;
	if (cpu == CPU_SETSIZE)
		return -EINVAL;

	CPU_ZERO(&new_mask);
	CPU_SET(cpu, &new_mask);
	if (sched_setaffinity(0, sizeof(new_mask), &new_mask))
		return -errno;
	return 0;
}

static int update_timer_map(int map_fd, __u64 key)
{
	__u64 value[3] = {};

	return bpf_map_update_elem(map_fd, &key, value, BPF_NOEXIST);
}

static int run_prog(int prog_fd, struct bpf_test_run_opts *opts)
{
	int err;

	err = bpf_prog_test_run_opts(prog_fd, opts);
	if (err)
		return err;
	return opts->retval;
}

void test_rhash_timer(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct rhash_timer *skel = NULL;
	cpu_set_t old_mask;
	int map_fd = -1, arm_fd, cancel_fd;
	bool affinity_set = false;
	__u64 key = 1;
	int attempt, err;

	err = pin_to_first_cpu(&old_mask);
	if (!ASSERT_OK(err, "pin_to_first_cpu"))
		return;
	affinity_set = true;

	skel = rhash_timer__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		goto out;

	map_fd = bpf_map__fd(skel->maps.timer_map);
	if (!ASSERT_GE(map_fd, 0, "timer_map fd"))
		goto out;
	arm_fd = bpf_program__fd(skel->progs.arm_deleted_timer);
	if (!ASSERT_GE(arm_fd, 0, "arm_deleted_timer fd"))
		goto out;
	cancel_fd = bpf_program__fd(skel->progs.cancel_recycled_timer);
	if (!ASSERT_GE(cancel_fd, 0, "cancel_recycled_timer fd"))
		goto out;

	err = update_timer_map(map_fd, key);
	if (!ASSERT_OK(err, "seed_timer_map"))
		goto out;

	for (attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
		err = run_prog(arm_fd, &opts);
		if (err) {
			ASSERT_OK(err, "arm_deleted_timer");
			goto out;
		}
		if (skel->bss->armed != attempt + 1) {
			ASSERT_EQ(skel->bss->armed, attempt + 1, "armed");
			goto out;
		}
		if (skel->bss->timer_init_err) {
			ASSERT_OK(skel->bss->timer_init_err, "timer_init_err");
			goto out;
		}
		if (skel->bss->timer_set_callback_err) {
			ASSERT_OK(skel->bss->timer_set_callback_err,
				  "timer_set_callback_err");
			goto out;
		}
		if (skel->bss->timer_start_err) {
			ASSERT_OK(skel->bss->timer_start_err, "timer_start_err");
			goto out;
		}

		if ((attempt + 1) % RCU_SYNC_INTERVAL == 0) {
			err = kern_sync_rcu();
			if (err) {
				ASSERT_OK(err, "kern_sync_rcu");
				goto out;
			}
		}

		err = update_timer_map(map_fd, ++key);
		if (err) {
			ASSERT_OK(err, "replace_timer_map");
			goto out;
		}

		err = run_prog(cancel_fd, &opts);
		if (err) {
			ASSERT_OK(err, "cancel_recycled_timer");
			goto out;
		}
		if (skel->bss->timer_cancel_err) {
			ASSERT_OK(skel->bss->timer_cancel_err, "timer_cancel_err");
			goto out;
		}
		if (skel->bss->cancelled)
			break;
	}

	ASSERT_GT(skel->bss->cancelled, 0, "preserved timer");
out:
	if (map_fd >= 0)
		bpf_map_delete_elem(map_fd, &key);
	rhash_timer__destroy(skel);
	if (affinity_set)
		sched_setaffinity(0, sizeof(old_mask), &old_mask);
}
