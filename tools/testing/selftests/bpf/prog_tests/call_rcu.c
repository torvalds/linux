// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include "call_rcu.skel.h"
#include "call_rcu_fail.skel.h"

struct elem {
	__u64 pad;
	struct bpf_rcu_head rh;
	__u64 val;
};

/*
 * Force the grace period, then poll: the callback still has to be invoked
 * afterwards, and call_rcu() is lazy on a CONFIG_RCU_LAZY kernel.
 */
static bool wait_for_callbacks(struct call_rcu *skel, int expected)
{
	int i, got = 0;

	kern_sync_rcu();
	for (i = 0; i < 3000; i++) {
		got = __atomic_load_n(&skel->bss->callbacks, __ATOMIC_ACQUIRE);
		if (got >= expected)
			return true;
		usleep(10000);
	}
	return ASSERT_GE(got, expected, "callbacks");
}

static void test_call_rcu_run(bool trace)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct bpf_program *prog;
	struct call_rcu *skel;
	struct elem elem;
	__u32 key = 1;
	int err;

	skel = call_rcu__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	prog = trace ? skel->progs.arm_trace : skel->progs.arm;
	err = bpf_prog_test_run_opts(bpf_program__fd(prog), &opts);
	if (!ASSERT_OK(err, "test_run") || !ASSERT_EQ(opts.retval, 0, "retval"))
		goto out;

	ASSERT_EQ(skel->bss->arm_err, 0, "arm_err");
	ASSERT_EQ(skel->bss->busy_err, -EBUSY, "busy_err");

	if (!wait_for_callbacks(skel, 1))
		goto out;

	ASSERT_EQ(skel->bss->cb_key, key, "cb_key");
	ASSERT_EQ(skel->bss->cb_val, 0xdeadbeef, "cb_val");
	ASSERT_EQ(skel->bss->cb_max_entries, bpf_map__max_entries(skel->maps.arr), "cb_map");

	err = bpf_map__lookup_elem(skel->maps.arr, &key, sizeof(key), &elem, sizeof(elem), 0);
	if (ASSERT_OK(err, "lookup"))
		ASSERT_EQ(elem.val, 0, "value_cleared");

	/* The head is disarmed before the callback runs, so it can be reused. */
	err = bpf_prog_test_run_opts(bpf_program__fd(prog), &opts);
	if (!ASSERT_OK(err, "test_run_again"))
		goto out;
	ASSERT_EQ(skel->bss->arm_err, 0, "rearm_err");
	wait_for_callbacks(skel, 2);
out:
	call_rcu__destroy(skel);
}

static void test_call_rcu_chain(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct call_rcu *skel;

	skel = call_rcu__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	skel->bss->chain = 1;
	if (!ASSERT_OK(bpf_prog_test_run_opts(bpf_program__fd(skel->progs.arm), &opts), "test_run"))
		goto out;

	if (!wait_for_callbacks(skel, 1))
		goto out;
	ASSERT_EQ(skel->bss->chain_err, 0, "chain_err");
	wait_for_callbacks(skel, 2);
out:
	call_rcu__destroy(skel);
}

/*
 * A callback that keeps re-arming must stop once the map loses its last user
 * reference, or it pins the program for good.  Hold an independent fd on .bss
 * so the refusal is still readable after the skeleton is gone.
 */
static void test_call_rcu_teardown(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	int i, err, fd = 0, bss_fd = -1;
	struct bpf_prog_info pinfo = {};
	struct bpf_map_info minfo = {};
	__u32 len, prog_id, zero = 0;
	struct call_rcu *skel;
	char *buf = NULL;
	size_t off, vsz;

	skel = call_rcu__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	len = sizeof(pinfo);
	if (!ASSERT_OK(bpf_prog_get_info_by_fd(bpf_program__fd(skel->progs.arm), &pinfo, &len),
		       "prog_info"))
		goto out;
	prog_id = pinfo.id;

	len = sizeof(minfo);
	if (!ASSERT_OK(bpf_map_get_info_by_fd(bpf_map__fd(skel->maps.bss), &minfo, &len),
		       "bss_info"))
		goto out;
	bss_fd = bpf_map_get_fd_by_id(minfo.id);
	if (!ASSERT_GE(bss_fd, 0, "bss_fd"))
		goto out;

	vsz = bpf_map__value_size(skel->maps.bss);
	off = (char *)&skel->bss->chain_err - (char *)skel->bss;
	buf = malloc(vsz);
	if (!ASSERT_OK_PTR(buf, "buf"))
		goto out;

	skel->bss->chain = INT_MAX;
	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.arm), &opts);
	if (!ASSERT_OK(err, "test_run") || !ASSERT_EQ(opts.retval, 0, "retval"))
		goto out;
	if (!ASSERT_EQ(skel->bss->arm_err, 0, "arm_err"))
		goto out;
	/* The chain has to be running before the map reference goes away. */
	if (!wait_for_callbacks(skel, 2))
		goto out;

	call_rcu__destroy(skel);
	skel = NULL;

	for (i = 0; i < 3000; i++) {
		fd = bpf_prog_get_fd_by_id(prog_id);
		if (fd < 0)
			break;
		close(fd);
		usleep(10000);
	}
	if (!ASSERT_EQ(fd, -ENOENT, "prog_freed"))
		goto out;

	if (ASSERT_OK(bpf_map_lookup_elem(bss_fd, &zero, buf), "bss_lookup"))
		ASSERT_EQ(*(int *)(buf + off), -EPERM, "chain_refused");
out:
	free(buf);
	if (bss_fd >= 0)
		close(bss_fd);
	call_rcu__destroy(skel);
}

static void test_call_rcu_bad_map(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts);
	struct call_rcu *skel;
	int fd;

	skel = call_rcu__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	opts.btf_fd = bpf_object__btf_fd(skel->obj);
	opts.btf_key_type_id = bpf_map__btf_key_type_id(skel->maps.arr);
	opts.btf_value_type_id = bpf_map__btf_value_type_id(skel->maps.arr);

	fd = bpf_map_create(BPF_MAP_TYPE_HASH, "rcu_hash", sizeof(__u32),
			    bpf_map__value_size(skel->maps.arr), 1, &opts);
	ASSERT_EQ(fd, -EOPNOTSUPP, "hash_rejected");
	if (fd >= 0)
		close(fd);

	call_rcu__destroy(skel);
}

/* Iterating would hand the program a writable pointer to the head. */
static void test_call_rcu_iter(void)
{
	LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	union bpf_iter_link_info linfo = {};
	struct bpf_link *link;
	struct call_rcu *skel;

	skel = call_rcu__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	linfo.map.map_fd = bpf_map__fd(skel->maps.arr);
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);

	link = bpf_program__attach_iter(skel->progs.dump, &opts);
	if (!ASSERT_ERR_PTR(link, "iter_rejected"))
		bpf_link__destroy(link);
	else
		ASSERT_EQ(libbpf_get_error(link), -EOPNOTSUPP, "iter_errno");

	call_rcu__destroy(skel);
}

static void test_call_rcu_inner_map(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts);
	struct call_rcu *skel;
	int fd;

	skel = call_rcu__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	opts.inner_map_fd = bpf_map__fd(skel->maps.arr);
	fd = bpf_map_create(BPF_MAP_TYPE_ARRAY_OF_MAPS, "rcu_outer",
			    sizeof(__u32), sizeof(__u32), 1, &opts);
	ASSERT_EQ(fd, -EOPNOTSUPP, "inner_map_rejected");
	if (fd >= 0)
		close(fd);

	call_rcu__destroy(skel);
}

/* Two heads in the same map must be independent. */
static void test_call_rcu_two_heads(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct call_rcu *skel;

	skel = call_rcu__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	if (!ASSERT_OK(bpf_prog_test_run_opts(bpf_program__fd(skel->progs.arm_both), &opts),
		       "test_run"))
		goto out;
	if (!ASSERT_EQ(skel->bss->arm_err, 0, "arm_err"))
		goto out;
	if (!wait_for_callbacks(skel, 2))
		goto out;
	ASSERT_EQ(skel->bss->cb_keys, 0x3, "both_keys");
out:
	call_rcu__destroy(skel);
}

void test_call_rcu(void)
{
	if (test__start_subtest("run"))
		test_call_rcu_run(false);
	if (test__start_subtest("run_tasks_trace"))
		test_call_rcu_run(true);
	if (test__start_subtest("chain"))
		test_call_rcu_chain();
	if (test__start_subtest("two_heads"))
		test_call_rcu_two_heads();
	if (test__start_subtest("teardown"))
		test_call_rcu_teardown();
	if (test__start_subtest("bad_map"))
		test_call_rcu_bad_map();
	if (test__start_subtest("iter"))
		test_call_rcu_iter();
	if (test__start_subtest("inner_map"))
		test_call_rcu_inner_map();
	RUN_TESTS(call_rcu_fail);
}
