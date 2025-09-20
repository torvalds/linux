// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Isovalent */
#include <uapi/linux/if_link.h>
#include <net/if.h>
#include <test_progs.h>

#define loopback 1
#define ping_cmd "ping -q -c1 -w1 127.0.0.1 > /dev/null"

#include "test_tc_link.skel.h"
#include "tc_helpers.h"

void test_ns_tc_opts_basic(void)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	__u32 fd1, fd2, id1, id2;
	struct test_tc_link *skel;
	__u32 prog_ids[2];
	int err;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	fd2 = bpf_program__fd(skel->progs.tc2);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);

	ASSERT_NEQ(id1, id2, "prog_ids_1_2");

	assert_mprog_count(BPF_TCX_INGRESS, 0);
	assert_mprog_count(BPF_TCX_EGRESS, 0);

	ASSERT_EQ(skel->bss->seen_tc1, false, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");

	err = bpf_prog_attach_opts(fd1, loopback, BPF_TCX_INGRESS, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count(BPF_TCX_INGRESS, 1);
	assert_mprog_count(BPF_TCX_EGRESS, 0);

	optq.prog_ids = prog_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, BPF_TCX_INGRESS, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_in;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 2, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");

	err = bpf_prog_attach_opts(fd2, loopback, BPF_TCX_EGRESS, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_in;

	assert_mprog_count(BPF_TCX_INGRESS, 1);
	assert_mprog_count(BPF_TCX_EGRESS, 1);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, BPF_TCX_EGRESS, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_eg;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 2, "revision");
	ASSERT_EQ(optq.prog_ids[0], id2, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");

cleanup_eg:
	err = bpf_prog_detach_opts(fd2, loopback, BPF_TCX_EGRESS, &optd);
	ASSERT_OK(err, "prog_detach_eg");

	assert_mprog_count(BPF_TCX_INGRESS, 1);
	assert_mprog_count(BPF_TCX_EGRESS, 0);

cleanup_in:
	err = bpf_prog_detach_opts(fd1, loopback, BPF_TCX_INGRESS, &optd);
	ASSERT_OK(err, "prog_detach_in");

	assert_mprog_count(BPF_TCX_INGRESS, 0);
	assert_mprog_count(BPF_TCX_EGRESS, 0);

cleanup:
	test_tc_link__destroy(skel);
}

static void test_tc_opts_before_target(int target)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	__u32 fd1, fd2, fd3, fd4, id1, id2, id3, id4;
	struct test_tc_link *skel;
	__u32 prog_ids[5];
	int err;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	fd2 = bpf_program__fd(skel->progs.tc2);
	fd3 = bpf_program__fd(skel->progs.tc3);
	fd4 = bpf_program__fd(skel->progs.tc4);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);
	id3 = id_from_prog_fd(fd3);
	id4 = id_from_prog_fd(fd4);

	ASSERT_NEQ(id1, id2, "prog_ids_1_2");
	ASSERT_NEQ(id3, id4, "prog_ids_3_4");
	ASSERT_NEQ(id2, id3, "prog_ids_2_3");

	assert_mprog_count(target, 0);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count(target, 1);

	err = bpf_prog_attach_opts(fd2, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target;

	assert_mprog_count(target, 2);

	optq.prog_ids = prog_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target2;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 3, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id2, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, false, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, false, "seen_tc4");

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_BEFORE,
		.relative_fd = fd2,
	);

	err = bpf_prog_attach_opts(fd3, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target2;

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target3;

	ASSERT_EQ(optq.count, 3, "count");
	ASSERT_EQ(optq.revision, 4, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id3, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id2, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], 0, "prog_ids[3]");

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_BEFORE,
		.relative_id = id1,
	);

	err = bpf_prog_attach_opts(fd4, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target3;

	assert_mprog_count(target, 4);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target4;

	ASSERT_EQ(optq.count, 4, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], id4, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id1, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id3, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], id2, "prog_ids[3]");
	ASSERT_EQ(optq.prog_ids[4], 0, "prog_ids[4]");

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, true, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, true, "seen_tc4");

cleanup_target4:
	err = bpf_prog_detach_opts(fd4, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 3);

cleanup_target3:
	err = bpf_prog_detach_opts(fd3, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 2);

cleanup_target2:
	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 1);

cleanup_target:
	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 0);

cleanup:
	test_tc_link__destroy(skel);
}

void test_ns_tc_opts_before(void)
{
	test_tc_opts_before_target(BPF_TCX_INGRESS);
	test_tc_opts_before_target(BPF_TCX_EGRESS);
}

static void test_tc_opts_after_target(int target)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	__u32 fd1, fd2, fd3, fd4, id1, id2, id3, id4;
	struct test_tc_link *skel;
	__u32 prog_ids[5];
	int err;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	fd2 = bpf_program__fd(skel->progs.tc2);
	fd3 = bpf_program__fd(skel->progs.tc3);
	fd4 = bpf_program__fd(skel->progs.tc4);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);
	id3 = id_from_prog_fd(fd3);
	id4 = id_from_prog_fd(fd4);

	ASSERT_NEQ(id1, id2, "prog_ids_1_2");
	ASSERT_NEQ(id3, id4, "prog_ids_3_4");
	ASSERT_NEQ(id2, id3, "prog_ids_2_3");

	assert_mprog_count(target, 0);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count(target, 1);

	err = bpf_prog_attach_opts(fd2, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target;

	assert_mprog_count(target, 2);

	optq.prog_ids = prog_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target2;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 3, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id2, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, false, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, false, "seen_tc4");

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_AFTER,
		.relative_fd = fd1,
	);

	err = bpf_prog_attach_opts(fd3, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target2;

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target3;

	ASSERT_EQ(optq.count, 3, "count");
	ASSERT_EQ(optq.revision, 4, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id3, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id2, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], 0, "prog_ids[3]");

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_AFTER,
		.relative_id = id2,
	);

	err = bpf_prog_attach_opts(fd4, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target3;

	assert_mprog_count(target, 4);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target4;

	ASSERT_EQ(optq.count, 4, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id3, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id2, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], id4, "prog_ids[3]");
	ASSERT_EQ(optq.prog_ids[4], 0, "prog_ids[4]");

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, true, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, true, "seen_tc4");

cleanup_target4:
	err = bpf_prog_detach_opts(fd4, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 3);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target3;

	ASSERT_EQ(optq.count, 3, "count");
	ASSERT_EQ(optq.revision, 6, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id3, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id2, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], 0, "prog_ids[3]");

cleanup_target3:
	err = bpf_prog_detach_opts(fd3, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 2);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target2;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 7, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id2, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");

cleanup_target2:
	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 1);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 8, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");

cleanup_target:
	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 0);

cleanup:
	test_tc_link__destroy(skel);
}

void test_ns_tc_opts_after(void)
{
	test_tc_opts_after_target(BPF_TCX_INGRESS);
	test_tc_opts_after_target(BPF_TCX_EGRESS);
}

static void test_tc_opts_revision_target(int target)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	__u32 fd1, fd2, id1, id2;
	struct test_tc_link *skel;
	__u32 prog_ids[3];
	int err;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	fd2 = bpf_program__fd(skel->progs.tc2);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);

	ASSERT_NEQ(id1, id2, "prog_ids_1_2");

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(opta,
		.expected_revision = 1,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(opta,
		.expected_revision = 1,
	);

	err = bpf_prog_attach_opts(fd2, loopback, target, &opta);
	if (!ASSERT_EQ(err, -ESTALE, "prog_attach"))
		goto cleanup_target;

	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(opta,
		.expected_revision = 2,
	);

	err = bpf_prog_attach_opts(fd2, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target;

	assert_mprog_count(target, 2);

	optq.prog_ids = prog_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target2;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 3, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id2, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");

	LIBBPF_OPTS_RESET(optd,
		.expected_revision = 2,
	);

	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_EQ(err, -ESTALE, "prog_detach");
	assert_mprog_count(target, 2);

cleanup_target2:
	LIBBPF_OPTS_RESET(optd,
		.expected_revision = 3,
	);

	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 1);

cleanup_target:
	LIBBPF_OPTS_RESET(optd);

	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 0);

cleanup:
	test_tc_link__destroy(skel);
}

void test_ns_tc_opts_revision(void)
{
	test_tc_opts_revision_target(BPF_TCX_INGRESS);
	test_tc_opts_revision_target(BPF_TCX_EGRESS);
}

static void test_tc_chain_classic(int target, bool chain_tc_old)
{
	LIBBPF_OPTS(bpf_tc_opts, tc_opts, .handle = 1, .priority = 1);
	LIBBPF_OPTS(bpf_tc_hook, tc_hook, .ifindex = loopback);
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	bool hook_created = false, tc_attached = false;
	__u32 fd1, fd2, fd3, id1, id2, id3;
	struct test_tc_link *skel;
	int err;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	fd2 = bpf_program__fd(skel->progs.tc2);
	fd3 = bpf_program__fd(skel->progs.tc3);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);
	id3 = id_from_prog_fd(fd3);

	ASSERT_NEQ(id1, id2, "prog_ids_1_2");
	ASSERT_NEQ(id2, id3, "prog_ids_2_3");

	assert_mprog_count(target, 0);

	if (chain_tc_old) {
		tc_hook.attach_point = target == BPF_TCX_INGRESS ?
				       BPF_TC_INGRESS : BPF_TC_EGRESS;
		err = bpf_tc_hook_create(&tc_hook);
		if (err == 0)
			hook_created = true;
		err = err == -EEXIST ? 0 : err;
		if (!ASSERT_OK(err, "bpf_tc_hook_create"))
			goto cleanup;

		tc_opts.prog_fd = fd3;
		err = bpf_tc_attach(&tc_hook, &tc_opts);
		if (!ASSERT_OK(err, "bpf_tc_attach"))
			goto cleanup;
		tc_attached = true;
	}

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	err = bpf_prog_attach_opts(fd2, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_detach;

	assert_mprog_count(target, 2);

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, chain_tc_old, "seen_tc3");

	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	if (!ASSERT_OK(err, "prog_detach"))
		goto cleanup_detach;

	assert_mprog_count(target, 1);

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, chain_tc_old, "seen_tc3");

cleanup_detach:
	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	if (!ASSERT_OK(err, "prog_detach"))
		goto cleanup;

	assert_mprog_count(target, 0);
cleanup:
	if (tc_attached) {
		tc_opts.flags = tc_opts.prog_fd = tc_opts.prog_id = 0;
		err = bpf_tc_detach(&tc_hook, &tc_opts);
		ASSERT_OK(err, "bpf_tc_detach");
	}
	if (hook_created) {
		tc_hook.attach_point = BPF_TC_INGRESS | BPF_TC_EGRESS;
		bpf_tc_hook_destroy(&tc_hook);
	}
	test_tc_link__destroy(skel);
	assert_mprog_count(target, 0);
}

void test_ns_tc_opts_chain_classic(void)
{
	test_tc_chain_classic(BPF_TCX_INGRESS, false);
	test_tc_chain_classic(BPF_TCX_EGRESS, false);
	test_tc_chain_classic(BPF_TCX_INGRESS, true);
	test_tc_chain_classic(BPF_TCX_EGRESS, true);
}

static void test_tc_opts_replace_target(int target)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	__u32 fd1, fd2, fd3, id1, id2, id3, detach_fd;
	__u32 prog_ids[4], prog_flags[4];
	struct test_tc_link *skel;
	int err;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	fd2 = bpf_program__fd(skel->progs.tc2);
	fd3 = bpf_program__fd(skel->progs.tc3);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);
	id3 = id_from_prog_fd(fd3);

	ASSERT_NEQ(id1, id2, "prog_ids_1_2");
	ASSERT_NEQ(id2, id3, "prog_ids_2_3");

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(opta,
		.expected_revision = 1,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_BEFORE,
		.relative_id = id1,
		.expected_revision = 2,
	);

	err = bpf_prog_attach_opts(fd2, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target;

	detach_fd = fd2;

	assert_mprog_count(target, 2);

	optq.prog_attach_flags = prog_flags;
	optq.prog_ids = prog_ids;

	memset(prog_flags, 0, sizeof(prog_flags));
	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target2;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 3, "revision");
	ASSERT_EQ(optq.prog_ids[0], id2, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id1, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");

	ASSERT_EQ(optq.prog_attach_flags[0], 0, "prog_flags[0]");
	ASSERT_EQ(optq.prog_attach_flags[1], 0, "prog_flags[1]");
	ASSERT_EQ(optq.prog_attach_flags[2], 0, "prog_flags[2]");

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, false, "seen_tc3");

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_REPLACE,
		.replace_prog_fd = fd2,
		.expected_revision = 3,
	);

	err = bpf_prog_attach_opts(fd3, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target2;

	detach_fd = fd3;

	assert_mprog_count(target, 2);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target2;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 4, "revision");
	ASSERT_EQ(optq.prog_ids[0], id3, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id1, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, true, "seen_tc3");

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_REPLACE | BPF_F_BEFORE,
		.replace_prog_fd = fd3,
		.relative_fd = fd1,
		.expected_revision = 4,
	);

	err = bpf_prog_attach_opts(fd2, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target2;

	detach_fd = fd2;

	assert_mprog_count(target, 2);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target2;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], id2, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id1, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, false, "seen_tc3");

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_REPLACE,
		.replace_prog_fd = fd2,
	);

	err = bpf_prog_attach_opts(fd2, loopback, target, &opta);
	ASSERT_EQ(err, -EEXIST, "prog_attach");
	assert_mprog_count(target, 2);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_REPLACE | BPF_F_AFTER,
		.replace_prog_fd = fd2,
		.relative_fd = fd1,
		.expected_revision = 5,
	);

	err = bpf_prog_attach_opts(fd3, loopback, target, &opta);
	ASSERT_EQ(err, -ERANGE, "prog_attach");
	assert_mprog_count(target, 2);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_BEFORE | BPF_F_AFTER | BPF_F_REPLACE,
		.replace_prog_fd = fd2,
		.relative_fd = fd1,
		.expected_revision = 5,
	);

	err = bpf_prog_attach_opts(fd3, loopback, target, &opta);
	ASSERT_EQ(err, -ERANGE, "prog_attach");
	assert_mprog_count(target, 2);

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_BEFORE,
		.relative_id = id1,
		.expected_revision = 5,
	);

cleanup_target2:
	err = bpf_prog_detach_opts(detach_fd, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 1);

cleanup_target:
	LIBBPF_OPTS_RESET(optd);

	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 0);

cleanup:
	test_tc_link__destroy(skel);
}

void test_ns_tc_opts_replace(void)
{
	test_tc_opts_replace_target(BPF_TCX_INGRESS);
	test_tc_opts_replace_target(BPF_TCX_EGRESS);
}

static void test_tc_opts_invalid_target(int target)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	__u32 fd1, fd2, id1, id2;
	struct test_tc_link *skel;
	int err;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	fd2 = bpf_program__fd(skel->progs.tc2);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);

	ASSERT_NEQ(id1, id2, "prog_ids_1_2");

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_BEFORE | BPF_F_AFTER,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	ASSERT_EQ(err, -ERANGE, "prog_attach");
	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_BEFORE | BPF_F_ID,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	ASSERT_EQ(err, -ENOENT, "prog_attach");
	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_AFTER | BPF_F_ID,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	ASSERT_EQ(err, -ENOENT, "prog_attach");
	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(opta,
		.relative_fd = fd2,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	ASSERT_EQ(err, -EINVAL, "prog_attach");
	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_BEFORE | BPF_F_AFTER,
		.relative_fd = fd2,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	ASSERT_EQ(err, -ENOENT, "prog_attach");
	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ID,
		.relative_id = id2,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	ASSERT_EQ(err, -EINVAL, "prog_attach");
	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_BEFORE,
		.relative_fd = fd1,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	ASSERT_EQ(err, -ENOENT, "prog_attach");
	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_AFTER,
		.relative_fd = fd1,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	ASSERT_EQ(err, -ENOENT, "prog_attach");
	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(opta);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(opta);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	ASSERT_EQ(err, -EEXIST, "prog_attach");
	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_BEFORE,
		.relative_fd = fd1,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	ASSERT_EQ(err, -EEXIST, "prog_attach");
	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_AFTER,
		.relative_fd = fd1,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	ASSERT_EQ(err, -EEXIST, "prog_attach");
	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_REPLACE,
		.relative_fd = fd1,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	ASSERT_EQ(err, -EINVAL, "prog_attach_x1");
	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_REPLACE,
		.replace_prog_fd = fd1,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	ASSERT_EQ(err, -EEXIST, "prog_attach");
	assert_mprog_count(target, 1);

	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 0);
cleanup:
	test_tc_link__destroy(skel);
}

void test_ns_tc_opts_invalid(void)
{
	test_tc_opts_invalid_target(BPF_TCX_INGRESS);
	test_tc_opts_invalid_target(BPF_TCX_EGRESS);
}

static void test_tc_opts_prepend_target(int target)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	__u32 fd1, fd2, fd3, fd4, id1, id2, id3, id4;
	struct test_tc_link *skel;
	__u32 prog_ids[5];
	int err;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	fd2 = bpf_program__fd(skel->progs.tc2);
	fd3 = bpf_program__fd(skel->progs.tc3);
	fd4 = bpf_program__fd(skel->progs.tc4);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);
	id3 = id_from_prog_fd(fd3);
	id4 = id_from_prog_fd(fd4);

	ASSERT_NEQ(id1, id2, "prog_ids_1_2");
	ASSERT_NEQ(id3, id4, "prog_ids_3_4");
	ASSERT_NEQ(id2, id3, "prog_ids_2_3");

	assert_mprog_count(target, 0);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_BEFORE,
	);

	err = bpf_prog_attach_opts(fd2, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target;

	assert_mprog_count(target, 2);

	optq.prog_ids = prog_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target2;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 3, "revision");
	ASSERT_EQ(optq.prog_ids[0], id2, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id1, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, false, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, false, "seen_tc4");

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_BEFORE,
	);

	err = bpf_prog_attach_opts(fd3, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target2;

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_BEFORE,
	);

	err = bpf_prog_attach_opts(fd4, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target3;

	assert_mprog_count(target, 4);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target4;

	ASSERT_EQ(optq.count, 4, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], id4, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id3, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id2, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], id1, "prog_ids[3]");
	ASSERT_EQ(optq.prog_ids[4], 0, "prog_ids[4]");

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, true, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, true, "seen_tc4");

cleanup_target4:
	err = bpf_prog_detach_opts(fd4, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 3);

cleanup_target3:
	err = bpf_prog_detach_opts(fd3, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 2);

cleanup_target2:
	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 1);

cleanup_target:
	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 0);

cleanup:
	test_tc_link__destroy(skel);
}

void test_ns_tc_opts_prepend(void)
{
	test_tc_opts_prepend_target(BPF_TCX_INGRESS);
	test_tc_opts_prepend_target(BPF_TCX_EGRESS);
}

static void test_tc_opts_append_target(int target)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	__u32 fd1, fd2, fd3, fd4, id1, id2, id3, id4;
	struct test_tc_link *skel;
	__u32 prog_ids[5];
	int err;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	fd2 = bpf_program__fd(skel->progs.tc2);
	fd3 = bpf_program__fd(skel->progs.tc3);
	fd4 = bpf_program__fd(skel->progs.tc4);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);
	id3 = id_from_prog_fd(fd3);
	id4 = id_from_prog_fd(fd4);

	ASSERT_NEQ(id1, id2, "prog_ids_1_2");
	ASSERT_NEQ(id3, id4, "prog_ids_3_4");
	ASSERT_NEQ(id2, id3, "prog_ids_2_3");

	assert_mprog_count(target, 0);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_AFTER,
	);

	err = bpf_prog_attach_opts(fd2, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target;

	assert_mprog_count(target, 2);

	optq.prog_ids = prog_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target2;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 3, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id2, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, false, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, false, "seen_tc4");

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_AFTER,
	);

	err = bpf_prog_attach_opts(fd3, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target2;

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_AFTER,
	);

	err = bpf_prog_attach_opts(fd4, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_target3;

	assert_mprog_count(target, 4);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_target4;

	ASSERT_EQ(optq.count, 4, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id2, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id3, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], id4, "prog_ids[3]");
	ASSERT_EQ(optq.prog_ids[4], 0, "prog_ids[4]");

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, true, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, true, "seen_tc4");

cleanup_target4:
	err = bpf_prog_detach_opts(fd4, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 3);

cleanup_target3:
	err = bpf_prog_detach_opts(fd3, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 2);

cleanup_target2:
	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 1);

cleanup_target:
	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 0);

cleanup:
	test_tc_link__destroy(skel);
}

void test_ns_tc_opts_append(void)
{
	test_tc_opts_append_target(BPF_TCX_INGRESS);
	test_tc_opts_append_target(BPF_TCX_EGRESS);
}

static void test_tc_opts_dev_cleanup_target(int target)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	__u32 fd1, fd2, fd3, fd4, id1, id2, id3, id4;
	struct test_tc_link *skel;
	int err, ifindex;

	ASSERT_OK(system("ip link add dev tcx_opts1 type veth peer name tcx_opts2"), "add veth");
	ifindex = if_nametoindex("tcx_opts1");
	ASSERT_NEQ(ifindex, 0, "non_zero_ifindex");

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	fd2 = bpf_program__fd(skel->progs.tc2);
	fd3 = bpf_program__fd(skel->progs.tc3);
	fd4 = bpf_program__fd(skel->progs.tc4);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);
	id3 = id_from_prog_fd(fd3);
	id4 = id_from_prog_fd(fd4);

	ASSERT_NEQ(id1, id2, "prog_ids_1_2");
	ASSERT_NEQ(id3, id4, "prog_ids_3_4");
	ASSERT_NEQ(id2, id3, "prog_ids_2_3");

	assert_mprog_count_ifindex(ifindex, target, 0);

	err = bpf_prog_attach_opts(fd1, ifindex, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count_ifindex(ifindex, target, 1);

	err = bpf_prog_attach_opts(fd2, ifindex, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup1;

	assert_mprog_count_ifindex(ifindex, target, 2);

	err = bpf_prog_attach_opts(fd3, ifindex, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup2;

	assert_mprog_count_ifindex(ifindex, target, 3);

	err = bpf_prog_attach_opts(fd4, ifindex, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup3;

	assert_mprog_count_ifindex(ifindex, target, 4);

	ASSERT_OK(system("ip link del dev tcx_opts1"), "del veth");
	ASSERT_EQ(if_nametoindex("tcx_opts1"), 0, "dev1_removed");
	ASSERT_EQ(if_nametoindex("tcx_opts2"), 0, "dev2_removed");
	return;
cleanup3:
	err = bpf_prog_detach_opts(fd3, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");

	assert_mprog_count_ifindex(ifindex, target, 2);
cleanup2:
	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");

	assert_mprog_count_ifindex(ifindex, target, 1);
cleanup1:
	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");

	assert_mprog_count_ifindex(ifindex, target, 0);
cleanup:
	test_tc_link__destroy(skel);

	ASSERT_OK(system("ip link del dev tcx_opts1"), "del veth");
	ASSERT_EQ(if_nametoindex("tcx_opts1"), 0, "dev1_removed");
	ASSERT_EQ(if_nametoindex("tcx_opts2"), 0, "dev2_removed");
}

void test_ns_tc_opts_dev_cleanup(void)
{
	test_tc_opts_dev_cleanup_target(BPF_TCX_INGRESS);
	test_tc_opts_dev_cleanup_target(BPF_TCX_EGRESS);
}

static void test_tc_opts_mixed_target(int target)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	LIBBPF_OPTS(bpf_tcx_opts, optl);
	__u32 pid1, pid2, pid3, pid4, lid2, lid4;
	__u32 prog_flags[4], link_flags[4];
	__u32 prog_ids[4], link_ids[4];
	struct test_tc_link *skel;
	struct bpf_link *link;
	int err, detach_fd;

	skel = test_tc_link__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		goto cleanup;

	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc1, target),
		  0, "tc1_attach_type");
	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc2, target),
		  0, "tc2_attach_type");
	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc3, target),
		  0, "tc3_attach_type");
	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc4, target),
		  0, "tc4_attach_type");

	err = test_tc_link__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	pid1 = id_from_prog_fd(bpf_program__fd(skel->progs.tc1));
	pid2 = id_from_prog_fd(bpf_program__fd(skel->progs.tc2));
	pid3 = id_from_prog_fd(bpf_program__fd(skel->progs.tc3));
	pid4 = id_from_prog_fd(bpf_program__fd(skel->progs.tc4));

	ASSERT_NEQ(pid1, pid2, "prog_ids_1_2");
	ASSERT_NEQ(pid3, pid4, "prog_ids_3_4");
	ASSERT_NEQ(pid2, pid3, "prog_ids_2_3");

	assert_mprog_count(target, 0);

	err = bpf_prog_attach_opts(bpf_program__fd(skel->progs.tc1),
				   loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	detach_fd = bpf_program__fd(skel->progs.tc1);

	assert_mprog_count(target, 1);

	link = bpf_program__attach_tcx(skel->progs.tc2, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup1;
	skel->links.tc2 = link;

	lid2 = id_from_link_fd(bpf_link__fd(skel->links.tc2));

	assert_mprog_count(target, 2);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_REPLACE,
		.replace_prog_fd = bpf_program__fd(skel->progs.tc1),
	);

	err = bpf_prog_attach_opts(bpf_program__fd(skel->progs.tc2),
				   loopback, target, &opta);
	ASSERT_EQ(err, -EEXIST, "prog_attach");

	assert_mprog_count(target, 2);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_REPLACE,
		.replace_prog_fd = bpf_program__fd(skel->progs.tc2),
	);

	err = bpf_prog_attach_opts(bpf_program__fd(skel->progs.tc1),
				   loopback, target, &opta);
	ASSERT_EQ(err, -EEXIST, "prog_attach");

	assert_mprog_count(target, 2);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_REPLACE,
		.replace_prog_fd = bpf_program__fd(skel->progs.tc2),
	);

	err = bpf_prog_attach_opts(bpf_program__fd(skel->progs.tc3),
				   loopback, target, &opta);
	ASSERT_EQ(err, -EBUSY, "prog_attach");

	assert_mprog_count(target, 2);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_REPLACE,
		.replace_prog_fd = bpf_program__fd(skel->progs.tc1),
	);

	err = bpf_prog_attach_opts(bpf_program__fd(skel->progs.tc3),
				   loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup1;

	detach_fd = bpf_program__fd(skel->progs.tc3);

	assert_mprog_count(target, 2);

	link = bpf_program__attach_tcx(skel->progs.tc4, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup1;
	skel->links.tc4 = link;

	lid4 = id_from_link_fd(bpf_link__fd(skel->links.tc4));

	assert_mprog_count(target, 3);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_REPLACE,
		.replace_prog_fd = bpf_program__fd(skel->progs.tc4),
	);

	err = bpf_prog_attach_opts(bpf_program__fd(skel->progs.tc2),
				   loopback, target, &opta);
	ASSERT_EQ(err, -EEXIST, "prog_attach");

	optq.prog_ids = prog_ids;
	optq.prog_attach_flags = prog_flags;
	optq.link_ids = link_ids;
	optq.link_attach_flags = link_flags;

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(prog_flags, 0, sizeof(prog_flags));
	memset(link_ids, 0, sizeof(link_ids));
	memset(link_flags, 0, sizeof(link_flags));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup1;

	ASSERT_EQ(optq.count, 3, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid3, "prog_ids[0]");
	ASSERT_EQ(optq.prog_attach_flags[0], 0, "prog_flags[0]");
	ASSERT_EQ(optq.link_ids[0], 0, "link_ids[0]");
	ASSERT_EQ(optq.link_attach_flags[0], 0, "link_flags[0]");
	ASSERT_EQ(optq.prog_ids[1], pid2, "prog_ids[1]");
	ASSERT_EQ(optq.prog_attach_flags[1], 0, "prog_flags[1]");
	ASSERT_EQ(optq.link_ids[1], lid2, "link_ids[1]");
	ASSERT_EQ(optq.link_attach_flags[1], 0, "link_flags[1]");
	ASSERT_EQ(optq.prog_ids[2], pid4, "prog_ids[2]");
	ASSERT_EQ(optq.prog_attach_flags[2], 0, "prog_flags[2]");
	ASSERT_EQ(optq.link_ids[2], lid4, "link_ids[2]");
	ASSERT_EQ(optq.link_attach_flags[2], 0, "link_flags[2]");
	ASSERT_EQ(optq.prog_ids[3], 0, "prog_ids[3]");
	ASSERT_EQ(optq.prog_attach_flags[3], 0, "prog_flags[3]");
	ASSERT_EQ(optq.link_ids[3], 0, "link_ids[3]");
	ASSERT_EQ(optq.link_attach_flags[3], 0, "link_flags[3]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

cleanup1:
	err = bpf_prog_detach_opts(detach_fd, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 2);

cleanup:
	test_tc_link__destroy(skel);
	assert_mprog_count(target, 0);
}

void test_ns_tc_opts_mixed(void)
{
	test_tc_opts_mixed_target(BPF_TCX_INGRESS);
	test_tc_opts_mixed_target(BPF_TCX_EGRESS);
}

static void test_tc_opts_demixed_target(int target)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_tcx_opts, optl);
	struct test_tc_link *skel;
	struct bpf_link *link;
	__u32 pid1, pid2;
	int err;

	skel = test_tc_link__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		goto cleanup;

	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc1, target),
		  0, "tc1_attach_type");
	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc2, target),
		  0, "tc2_attach_type");

	err = test_tc_link__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	pid1 = id_from_prog_fd(bpf_program__fd(skel->progs.tc1));
	pid2 = id_from_prog_fd(bpf_program__fd(skel->progs.tc2));
	ASSERT_NEQ(pid1, pid2, "prog_ids_1_2");

	assert_mprog_count(target, 0);

	err = bpf_prog_attach_opts(bpf_program__fd(skel->progs.tc1),
				   loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count(target, 1);

	link = bpf_program__attach_tcx(skel->progs.tc2, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup1;
	skel->links.tc2 = link;

	assert_mprog_count(target, 2);

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_AFTER,
	);

	err = bpf_prog_detach_opts(0, loopback, target, &optd);
	ASSERT_EQ(err, -EBUSY, "prog_detach");

	assert_mprog_count(target, 2);

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_BEFORE,
	);

	err = bpf_prog_detach_opts(0, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");

	assert_mprog_count(target, 1);
	goto cleanup;

cleanup1:
	err = bpf_prog_detach_opts(bpf_program__fd(skel->progs.tc1),
				   loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 2);

cleanup:
	test_tc_link__destroy(skel);
	assert_mprog_count(target, 0);
}

void test_ns_tc_opts_demixed(void)
{
	test_tc_opts_demixed_target(BPF_TCX_INGRESS);
	test_tc_opts_demixed_target(BPF_TCX_EGRESS);
}

static void test_tc_opts_detach_target(int target)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	__u32 fd1, fd2, fd3, fd4, id1, id2, id3, id4;
	struct test_tc_link *skel;
	__u32 prog_ids[5];
	int err;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	fd2 = bpf_program__fd(skel->progs.tc2);
	fd3 = bpf_program__fd(skel->progs.tc3);
	fd4 = bpf_program__fd(skel->progs.tc4);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);
	id3 = id_from_prog_fd(fd3);
	id4 = id_from_prog_fd(fd4);

	ASSERT_NEQ(id1, id2, "prog_ids_1_2");
	ASSERT_NEQ(id3, id4, "prog_ids_3_4");
	ASSERT_NEQ(id2, id3, "prog_ids_2_3");

	assert_mprog_count(target, 0);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count(target, 1);

	err = bpf_prog_attach_opts(fd2, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup1;

	assert_mprog_count(target, 2);

	err = bpf_prog_attach_opts(fd3, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup2;

	assert_mprog_count(target, 3);

	err = bpf_prog_attach_opts(fd4, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup3;

	assert_mprog_count(target, 4);

	optq.prog_ids = prog_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(optq.count, 4, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id2, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id3, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], id4, "prog_ids[3]");
	ASSERT_EQ(optq.prog_ids[4], 0, "prog_ids[4]");

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_BEFORE,
	);

	err = bpf_prog_detach_opts(0, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");

	assert_mprog_count(target, 3);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(optq.count, 3, "count");
	ASSERT_EQ(optq.revision, 6, "revision");
	ASSERT_EQ(optq.prog_ids[0], id2, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id3, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id4, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], 0, "prog_ids[3]");

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_AFTER,
	);

	err = bpf_prog_detach_opts(0, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");

	assert_mprog_count(target, 2);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 7, "revision");
	ASSERT_EQ(optq.prog_ids[0], id2, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id3, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");

	LIBBPF_OPTS_RESET(optd);

	err = bpf_prog_detach_opts(fd3, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 1);

	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_BEFORE,
	);

	err = bpf_prog_detach_opts(0, loopback, target, &optd);
	ASSERT_EQ(err, -ENOENT, "prog_detach");

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_AFTER,
	);

	err = bpf_prog_detach_opts(0, loopback, target, &optd);
	ASSERT_EQ(err, -ENOENT, "prog_detach");
	goto cleanup;

cleanup4:
	err = bpf_prog_detach_opts(fd4, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 3);

cleanup3:
	err = bpf_prog_detach_opts(fd3, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 2);

cleanup2:
	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 1);

cleanup1:
	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 0);

cleanup:
	test_tc_link__destroy(skel);
}

void test_ns_tc_opts_detach(void)
{
	test_tc_opts_detach_target(BPF_TCX_INGRESS);
	test_tc_opts_detach_target(BPF_TCX_EGRESS);
}

static void test_tc_opts_detach_before_target(int target)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	__u32 fd1, fd2, fd3, fd4, id1, id2, id3, id4;
	struct test_tc_link *skel;
	__u32 prog_ids[5];
	int err;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	fd2 = bpf_program__fd(skel->progs.tc2);
	fd3 = bpf_program__fd(skel->progs.tc3);
	fd4 = bpf_program__fd(skel->progs.tc4);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);
	id3 = id_from_prog_fd(fd3);
	id4 = id_from_prog_fd(fd4);

	ASSERT_NEQ(id1, id2, "prog_ids_1_2");
	ASSERT_NEQ(id3, id4, "prog_ids_3_4");
	ASSERT_NEQ(id2, id3, "prog_ids_2_3");

	assert_mprog_count(target, 0);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count(target, 1);

	err = bpf_prog_attach_opts(fd2, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup1;

	assert_mprog_count(target, 2);

	err = bpf_prog_attach_opts(fd3, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup2;

	assert_mprog_count(target, 3);

	err = bpf_prog_attach_opts(fd4, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup3;

	assert_mprog_count(target, 4);

	optq.prog_ids = prog_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(optq.count, 4, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id2, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id3, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], id4, "prog_ids[3]");
	ASSERT_EQ(optq.prog_ids[4], 0, "prog_ids[4]");

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_BEFORE,
		.relative_fd = fd2,
	);

	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");

	assert_mprog_count(target, 3);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(optq.count, 3, "count");
	ASSERT_EQ(optq.revision, 6, "revision");
	ASSERT_EQ(optq.prog_ids[0], id2, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id3, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id4, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], 0, "prog_ids[3]");

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_BEFORE,
		.relative_fd = fd2,
	);

	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_EQ(err, -ENOENT, "prog_detach");
	assert_mprog_count(target, 3);

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_BEFORE,
		.relative_fd = fd4,
	);

	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_EQ(err, -ERANGE, "prog_detach");
	assert_mprog_count(target, 3);

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_BEFORE,
		.relative_fd = fd1,
	);

	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_EQ(err, -ENOENT, "prog_detach");
	assert_mprog_count(target, 3);

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_BEFORE,
		.relative_fd = fd3,
	);

	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");

	assert_mprog_count(target, 2);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 7, "revision");
	ASSERT_EQ(optq.prog_ids[0], id3, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id4, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_BEFORE,
		.relative_fd = fd4,
	);

	err = bpf_prog_detach_opts(0, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");

	assert_mprog_count(target, 1);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 8, "revision");
	ASSERT_EQ(optq.prog_ids[0], id4, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_BEFORE,
	);

	err = bpf_prog_detach_opts(0, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");

	assert_mprog_count(target, 0);
	goto cleanup;

cleanup4:
	err = bpf_prog_detach_opts(fd4, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 3);

cleanup3:
	err = bpf_prog_detach_opts(fd3, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 2);

cleanup2:
	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 1);

cleanup1:
	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 0);

cleanup:
	test_tc_link__destroy(skel);
}

void test_ns_tc_opts_detach_before(void)
{
	test_tc_opts_detach_before_target(BPF_TCX_INGRESS);
	test_tc_opts_detach_before_target(BPF_TCX_EGRESS);
}

static void test_tc_opts_detach_after_target(int target)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	__u32 fd1, fd2, fd3, fd4, id1, id2, id3, id4;
	struct test_tc_link *skel;
	__u32 prog_ids[5];
	int err;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	fd2 = bpf_program__fd(skel->progs.tc2);
	fd3 = bpf_program__fd(skel->progs.tc3);
	fd4 = bpf_program__fd(skel->progs.tc4);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);
	id3 = id_from_prog_fd(fd3);
	id4 = id_from_prog_fd(fd4);

	ASSERT_NEQ(id1, id2, "prog_ids_1_2");
	ASSERT_NEQ(id3, id4, "prog_ids_3_4");
	ASSERT_NEQ(id2, id3, "prog_ids_2_3");

	assert_mprog_count(target, 0);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count(target, 1);

	err = bpf_prog_attach_opts(fd2, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup1;

	assert_mprog_count(target, 2);

	err = bpf_prog_attach_opts(fd3, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup2;

	assert_mprog_count(target, 3);

	err = bpf_prog_attach_opts(fd4, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup3;

	assert_mprog_count(target, 4);

	optq.prog_ids = prog_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(optq.count, 4, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id2, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id3, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], id4, "prog_ids[3]");
	ASSERT_EQ(optq.prog_ids[4], 0, "prog_ids[4]");

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_AFTER,
		.relative_fd = fd1,
	);

	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");

	assert_mprog_count(target, 3);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(optq.count, 3, "count");
	ASSERT_EQ(optq.revision, 6, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id3, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id4, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], 0, "prog_ids[3]");

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_AFTER,
		.relative_fd = fd1,
	);

	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_EQ(err, -ENOENT, "prog_detach");
	assert_mprog_count(target, 3);

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_AFTER,
		.relative_fd = fd4,
	);

	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_EQ(err, -ERANGE, "prog_detach");
	assert_mprog_count(target, 3);

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_AFTER,
		.relative_fd = fd3,
	);

	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_EQ(err, -ERANGE, "prog_detach");
	assert_mprog_count(target, 3);

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_AFTER,
		.relative_fd = fd1,
	);

	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_EQ(err, -ERANGE, "prog_detach");
	assert_mprog_count(target, 3);

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_AFTER,
		.relative_fd = fd1,
	);

	err = bpf_prog_detach_opts(fd3, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");

	assert_mprog_count(target, 2);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 7, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id4, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_AFTER,
		.relative_fd = fd1,
	);

	err = bpf_prog_detach_opts(0, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");

	assert_mprog_count(target, 1);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 8, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");

	LIBBPF_OPTS_RESET(optd,
		.flags = BPF_F_AFTER,
	);

	err = bpf_prog_detach_opts(0, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");

	assert_mprog_count(target, 0);
	goto cleanup;

cleanup4:
	err = bpf_prog_detach_opts(fd4, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 3);

cleanup3:
	err = bpf_prog_detach_opts(fd3, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 2);

cleanup2:
	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 1);

cleanup1:
	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 0);

cleanup:
	test_tc_link__destroy(skel);
}

void test_ns_tc_opts_detach_after(void)
{
	test_tc_opts_detach_after_target(BPF_TCX_INGRESS);
	test_tc_opts_detach_after_target(BPF_TCX_EGRESS);
}

static void test_tc_opts_delete_empty(int target, bool chain_tc_old)
{
	LIBBPF_OPTS(bpf_tc_hook, tc_hook, .ifindex = loopback);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	int err;

	assert_mprog_count(target, 0);
	if (chain_tc_old) {
		tc_hook.attach_point = target == BPF_TCX_INGRESS ?
				       BPF_TC_INGRESS : BPF_TC_EGRESS;
		err = bpf_tc_hook_create(&tc_hook);
		ASSERT_OK(err, "bpf_tc_hook_create");
		assert_mprog_count(target, 0);
	}
	err = bpf_prog_detach_opts(0, loopback, target, &optd);
	ASSERT_EQ(err, -ENOENT, "prog_detach");
	if (chain_tc_old) {
		tc_hook.attach_point = BPF_TC_INGRESS | BPF_TC_EGRESS;
		bpf_tc_hook_destroy(&tc_hook);
	}
	assert_mprog_count(target, 0);
}

void test_ns_tc_opts_delete_empty(void)
{
	test_tc_opts_delete_empty(BPF_TCX_INGRESS, false);
	test_tc_opts_delete_empty(BPF_TCX_EGRESS, false);
	test_tc_opts_delete_empty(BPF_TCX_INGRESS, true);
	test_tc_opts_delete_empty(BPF_TCX_EGRESS, true);
}

static void test_tc_chain_mixed(int target)
{
	LIBBPF_OPTS(bpf_tc_opts, tc_opts, .handle = 1, .priority = 1);
	LIBBPF_OPTS(bpf_tc_hook, tc_hook, .ifindex = loopback);
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	__u32 fd1, fd2, fd3, id1, id2, id3;
	struct test_tc_link *skel;
	int err, detach_fd;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc4);
	fd2 = bpf_program__fd(skel->progs.tc5);
	fd3 = bpf_program__fd(skel->progs.tc6);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);
	id3 = id_from_prog_fd(fd3);

	ASSERT_NEQ(id1, id2, "prog_ids_1_2");
	ASSERT_NEQ(id2, id3, "prog_ids_2_3");

	assert_mprog_count(target, 0);

	tc_hook.attach_point = target == BPF_TCX_INGRESS ?
			       BPF_TC_INGRESS : BPF_TC_EGRESS;
	err = bpf_tc_hook_create(&tc_hook);
	err = err == -EEXIST ? 0 : err;
	if (!ASSERT_OK(err, "bpf_tc_hook_create"))
		goto cleanup;

	tc_opts.prog_fd = fd2;
	err = bpf_tc_attach(&tc_hook, &tc_opts);
	if (!ASSERT_OK(err, "bpf_tc_attach"))
		goto cleanup_hook;

	err = bpf_prog_attach_opts(fd3, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_filter;

	detach_fd = fd3;

	assert_mprog_count(target, 1);

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc4, false, "seen_tc4");
	ASSERT_EQ(skel->bss->seen_tc5, false, "seen_tc5");
	ASSERT_EQ(skel->bss->seen_tc6, true, "seen_tc6");

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_REPLACE,
		.replace_prog_fd = fd3,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_opts;

	detach_fd = fd1;

	assert_mprog_count(target, 1);

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc4, true, "seen_tc4");
	ASSERT_EQ(skel->bss->seen_tc5, true, "seen_tc5");
	ASSERT_EQ(skel->bss->seen_tc6, false, "seen_tc6");

cleanup_opts:
	err = bpf_prog_detach_opts(detach_fd, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 0);

	tc_skel_reset_all_seen(skel);
	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc4, false, "seen_tc4");
	ASSERT_EQ(skel->bss->seen_tc5, true, "seen_tc5");
	ASSERT_EQ(skel->bss->seen_tc6, false, "seen_tc6");

cleanup_filter:
	tc_opts.flags = tc_opts.prog_fd = tc_opts.prog_id = 0;
	err = bpf_tc_detach(&tc_hook, &tc_opts);
	ASSERT_OK(err, "bpf_tc_detach");

cleanup_hook:
	tc_hook.attach_point = BPF_TC_INGRESS | BPF_TC_EGRESS;
	bpf_tc_hook_destroy(&tc_hook);

cleanup:
	test_tc_link__destroy(skel);
}

void test_ns_tc_opts_chain_mixed(void)
{
	test_tc_chain_mixed(BPF_TCX_INGRESS);
	test_tc_chain_mixed(BPF_TCX_EGRESS);
}

static int generate_dummy_prog(void)
{
	const struct bpf_insn prog_insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	const size_t prog_insn_cnt = ARRAY_SIZE(prog_insns);
	LIBBPF_OPTS(bpf_prog_load_opts, opts);
	const size_t log_buf_sz = 256;
	char log_buf[log_buf_sz];
	int fd = -1;

	opts.log_buf = log_buf;
	opts.log_size = log_buf_sz;

	log_buf[0] = '\0';
	opts.log_level = 0;
	fd = bpf_prog_load(BPF_PROG_TYPE_SCHED_CLS, "tcx_prog", "GPL",
			   prog_insns, prog_insn_cnt, &opts);
	ASSERT_STREQ(log_buf, "", "log_0");
	ASSERT_GE(fd, 0, "prog_fd");
	return fd;
}

static void test_tc_opts_max_target(int target, int flags, bool relative)
{
	int err, ifindex, i, prog_fd, last_fd = -1;
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	const int max_progs = 63;

	ASSERT_OK(system("ip link add dev tcx_opts1 type veth peer name tcx_opts2"), "add veth");
	ifindex = if_nametoindex("tcx_opts1");
	ASSERT_NEQ(ifindex, 0, "non_zero_ifindex");

	assert_mprog_count_ifindex(ifindex, target, 0);

	for (i = 0; i < max_progs; i++) {
		prog_fd = generate_dummy_prog();
		if (!ASSERT_GE(prog_fd, 0, "dummy_prog"))
			goto cleanup;
		err = bpf_prog_attach_opts(prog_fd, ifindex, target, &opta);
		if (!ASSERT_EQ(err, 0, "prog_attach"))
			goto cleanup;
		assert_mprog_count_ifindex(ifindex, target, i + 1);
		if (i == max_progs - 1 && relative)
			last_fd = prog_fd;
		else
			close(prog_fd);
	}

	prog_fd = generate_dummy_prog();
	if (!ASSERT_GE(prog_fd, 0, "dummy_prog"))
		goto cleanup;
	opta.flags = flags;
	if (last_fd > 0)
		opta.relative_fd = last_fd;
	err = bpf_prog_attach_opts(prog_fd, ifindex, target, &opta);
	ASSERT_EQ(err, -ERANGE, "prog_64_attach");
	assert_mprog_count_ifindex(ifindex, target, max_progs);
	close(prog_fd);
cleanup:
	if (last_fd > 0)
		close(last_fd);
	ASSERT_OK(system("ip link del dev tcx_opts1"), "del veth");
	ASSERT_EQ(if_nametoindex("tcx_opts1"), 0, "dev1_removed");
	ASSERT_EQ(if_nametoindex("tcx_opts2"), 0, "dev2_removed");
}

void test_ns_tc_opts_max(void)
{
	test_tc_opts_max_target(BPF_TCX_INGRESS, 0, false);
	test_tc_opts_max_target(BPF_TCX_EGRESS, 0, false);

	test_tc_opts_max_target(BPF_TCX_INGRESS, BPF_F_BEFORE, false);
	test_tc_opts_max_target(BPF_TCX_EGRESS, BPF_F_BEFORE, true);

	test_tc_opts_max_target(BPF_TCX_INGRESS, BPF_F_AFTER, true);
	test_tc_opts_max_target(BPF_TCX_EGRESS, BPF_F_AFTER, false);
}

static void test_tc_opts_query_target(int target)
{
	const size_t attr_size = offsetofend(union bpf_attr, query);
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	__u32 fd1, fd2, fd3, fd4, id1, id2, id3, id4;
	struct test_tc_link *skel;
	union bpf_attr attr;
	__u32 prog_ids[10];
	int err;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	fd2 = bpf_program__fd(skel->progs.tc2);
	fd3 = bpf_program__fd(skel->progs.tc3);
	fd4 = bpf_program__fd(skel->progs.tc4);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);
	id3 = id_from_prog_fd(fd3);
	id4 = id_from_prog_fd(fd4);

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(opta,
		.expected_revision = 1,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(opta,
		.expected_revision = 2,
	);

	err = bpf_prog_attach_opts(fd2, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup1;

	assert_mprog_count(target, 2);

	LIBBPF_OPTS_RESET(opta,
		.expected_revision = 3,
	);

	err = bpf_prog_attach_opts(fd3, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup2;

	assert_mprog_count(target, 3);

	LIBBPF_OPTS_RESET(opta,
		.expected_revision = 4,
	);

	err = bpf_prog_attach_opts(fd4, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup3;

	assert_mprog_count(target, 4);

	/* Test 1: Double query via libbpf API */
	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(optq.count, 4, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids, NULL, "prog_ids");
	ASSERT_EQ(optq.link_ids, NULL, "link_ids");

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.prog_ids = prog_ids;

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(optq.count, 4, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id2, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id3, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], id4, "prog_ids[3]");
	ASSERT_EQ(optq.prog_ids[4], 0, "prog_ids[4]");
	ASSERT_EQ(optq.link_ids, NULL, "link_ids");

	/* Test 2: Double query via bpf_attr & bpf(2) directly */
	memset(&attr, 0, attr_size);
	attr.query.target_ifindex = loopback;
	attr.query.attach_type = target;

	err = syscall(__NR_bpf, BPF_PROG_QUERY, &attr, attr_size);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(attr.query.count, 4, "count");
	ASSERT_EQ(attr.query.revision, 5, "revision");
	ASSERT_EQ(attr.query.query_flags, 0, "query_flags");
	ASSERT_EQ(attr.query.attach_flags, 0, "attach_flags");
	ASSERT_EQ(attr.query.target_ifindex, loopback, "target_ifindex");
	ASSERT_EQ(attr.query.attach_type, target, "attach_type");
	ASSERT_EQ(attr.query.prog_ids, 0, "prog_ids");
	ASSERT_EQ(attr.query.prog_attach_flags, 0, "prog_attach_flags");
	ASSERT_EQ(attr.query.link_ids, 0, "link_ids");
	ASSERT_EQ(attr.query.link_attach_flags, 0, "link_attach_flags");

	memset(prog_ids, 0, sizeof(prog_ids));
	attr.query.prog_ids = ptr_to_u64(prog_ids);

	err = syscall(__NR_bpf, BPF_PROG_QUERY, &attr, attr_size);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(attr.query.count, 4, "count");
	ASSERT_EQ(attr.query.revision, 5, "revision");
	ASSERT_EQ(attr.query.query_flags, 0, "query_flags");
	ASSERT_EQ(attr.query.attach_flags, 0, "attach_flags");
	ASSERT_EQ(attr.query.target_ifindex, loopback, "target_ifindex");
	ASSERT_EQ(attr.query.attach_type, target, "attach_type");
	ASSERT_EQ(attr.query.prog_ids, ptr_to_u64(prog_ids), "prog_ids");
	ASSERT_EQ(prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(prog_ids[1], id2, "prog_ids[1]");
	ASSERT_EQ(prog_ids[2], id3, "prog_ids[2]");
	ASSERT_EQ(prog_ids[3], id4, "prog_ids[3]");
	ASSERT_EQ(prog_ids[4], 0, "prog_ids[4]");
	ASSERT_EQ(attr.query.prog_attach_flags, 0, "prog_attach_flags");
	ASSERT_EQ(attr.query.link_ids, 0, "link_ids");
	ASSERT_EQ(attr.query.link_attach_flags, 0, "link_attach_flags");

	/* Test 3: Query with smaller prog_ids array */
	memset(&attr, 0, attr_size);
	attr.query.target_ifindex = loopback;
	attr.query.attach_type = target;

	memset(prog_ids, 0, sizeof(prog_ids));
	attr.query.prog_ids = ptr_to_u64(prog_ids);
	attr.query.count = 2;

	err = syscall(__NR_bpf, BPF_PROG_QUERY, &attr, attr_size);
	ASSERT_EQ(err, -1, "prog_query_should_fail");
	ASSERT_EQ(errno, ENOSPC, "prog_query_should_fail");

	ASSERT_EQ(attr.query.count, 4, "count");
	ASSERT_EQ(attr.query.revision, 5, "revision");
	ASSERT_EQ(attr.query.query_flags, 0, "query_flags");
	ASSERT_EQ(attr.query.attach_flags, 0, "attach_flags");
	ASSERT_EQ(attr.query.target_ifindex, loopback, "target_ifindex");
	ASSERT_EQ(attr.query.attach_type, target, "attach_type");
	ASSERT_EQ(attr.query.prog_ids, ptr_to_u64(prog_ids), "prog_ids");
	ASSERT_EQ(prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(prog_ids[1], id2, "prog_ids[1]");
	ASSERT_EQ(prog_ids[2], 0, "prog_ids[2]");
	ASSERT_EQ(prog_ids[3], 0, "prog_ids[3]");
	ASSERT_EQ(prog_ids[4], 0, "prog_ids[4]");
	ASSERT_EQ(attr.query.prog_attach_flags, 0, "prog_attach_flags");
	ASSERT_EQ(attr.query.link_ids, 0, "link_ids");
	ASSERT_EQ(attr.query.link_attach_flags, 0, "link_attach_flags");

	/* Test 4: Query with larger prog_ids array */
	memset(&attr, 0, attr_size);
	attr.query.target_ifindex = loopback;
	attr.query.attach_type = target;

	memset(prog_ids, 0, sizeof(prog_ids));
	attr.query.prog_ids = ptr_to_u64(prog_ids);
	attr.query.count = 10;

	err = syscall(__NR_bpf, BPF_PROG_QUERY, &attr, attr_size);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(attr.query.count, 4, "count");
	ASSERT_EQ(attr.query.revision, 5, "revision");
	ASSERT_EQ(attr.query.query_flags, 0, "query_flags");
	ASSERT_EQ(attr.query.attach_flags, 0, "attach_flags");
	ASSERT_EQ(attr.query.target_ifindex, loopback, "target_ifindex");
	ASSERT_EQ(attr.query.attach_type, target, "attach_type");
	ASSERT_EQ(attr.query.prog_ids, ptr_to_u64(prog_ids), "prog_ids");
	ASSERT_EQ(prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(prog_ids[1], id2, "prog_ids[1]");
	ASSERT_EQ(prog_ids[2], id3, "prog_ids[2]");
	ASSERT_EQ(prog_ids[3], id4, "prog_ids[3]");
	ASSERT_EQ(prog_ids[4], 0, "prog_ids[4]");
	ASSERT_EQ(attr.query.prog_attach_flags, 0, "prog_attach_flags");
	ASSERT_EQ(attr.query.link_ids, 0, "link_ids");
	ASSERT_EQ(attr.query.link_attach_flags, 0, "link_attach_flags");

	/* Test 5: Query with NULL prog_ids array but with count > 0 */
	memset(&attr, 0, attr_size);
	attr.query.target_ifindex = loopback;
	attr.query.attach_type = target;

	memset(prog_ids, 0, sizeof(prog_ids));
	attr.query.count = sizeof(prog_ids);

	err = syscall(__NR_bpf, BPF_PROG_QUERY, &attr, attr_size);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(attr.query.count, 4, "count");
	ASSERT_EQ(attr.query.revision, 5, "revision");
	ASSERT_EQ(attr.query.query_flags, 0, "query_flags");
	ASSERT_EQ(attr.query.attach_flags, 0, "attach_flags");
	ASSERT_EQ(attr.query.target_ifindex, loopback, "target_ifindex");
	ASSERT_EQ(attr.query.attach_type, target, "attach_type");
	ASSERT_EQ(prog_ids[0], 0, "prog_ids[0]");
	ASSERT_EQ(prog_ids[1], 0, "prog_ids[1]");
	ASSERT_EQ(prog_ids[2], 0, "prog_ids[2]");
	ASSERT_EQ(prog_ids[3], 0, "prog_ids[3]");
	ASSERT_EQ(prog_ids[4], 0, "prog_ids[4]");
	ASSERT_EQ(attr.query.prog_ids, 0, "prog_ids");
	ASSERT_EQ(attr.query.prog_attach_flags, 0, "prog_attach_flags");
	ASSERT_EQ(attr.query.link_ids, 0, "link_ids");
	ASSERT_EQ(attr.query.link_attach_flags, 0, "link_attach_flags");

	/* Test 6: Query with non-NULL prog_ids array but with count == 0 */
	memset(&attr, 0, attr_size);
	attr.query.target_ifindex = loopback;
	attr.query.attach_type = target;

	memset(prog_ids, 0, sizeof(prog_ids));
	attr.query.prog_ids = ptr_to_u64(prog_ids);

	err = syscall(__NR_bpf, BPF_PROG_QUERY, &attr, attr_size);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(attr.query.count, 4, "count");
	ASSERT_EQ(attr.query.revision, 5, "revision");
	ASSERT_EQ(attr.query.query_flags, 0, "query_flags");
	ASSERT_EQ(attr.query.attach_flags, 0, "attach_flags");
	ASSERT_EQ(attr.query.target_ifindex, loopback, "target_ifindex");
	ASSERT_EQ(attr.query.attach_type, target, "attach_type");
	ASSERT_EQ(prog_ids[0], 0, "prog_ids[0]");
	ASSERT_EQ(prog_ids[1], 0, "prog_ids[1]");
	ASSERT_EQ(prog_ids[2], 0, "prog_ids[2]");
	ASSERT_EQ(prog_ids[3], 0, "prog_ids[3]");
	ASSERT_EQ(prog_ids[4], 0, "prog_ids[4]");
	ASSERT_EQ(attr.query.prog_ids, ptr_to_u64(prog_ids), "prog_ids");
	ASSERT_EQ(attr.query.prog_attach_flags, 0, "prog_attach_flags");
	ASSERT_EQ(attr.query.link_ids, 0, "link_ids");
	ASSERT_EQ(attr.query.link_attach_flags, 0, "link_attach_flags");

	/* Test 7: Query with invalid flags */
	attr.query.attach_flags = 0;
	attr.query.query_flags = 1;

	err = syscall(__NR_bpf, BPF_PROG_QUERY, &attr, attr_size);
	ASSERT_EQ(err, -1, "prog_query_should_fail");
	ASSERT_EQ(errno, EINVAL, "prog_query_should_fail");

	attr.query.attach_flags = 1;
	attr.query.query_flags = 0;

	err = syscall(__NR_bpf, BPF_PROG_QUERY, &attr, attr_size);
	ASSERT_EQ(err, -1, "prog_query_should_fail");
	ASSERT_EQ(errno, EINVAL, "prog_query_should_fail");

cleanup4:
	err = bpf_prog_detach_opts(fd4, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 3);

cleanup3:
	err = bpf_prog_detach_opts(fd3, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 2);

cleanup2:
	err = bpf_prog_detach_opts(fd2, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 1);

cleanup1:
	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 0);

cleanup:
	test_tc_link__destroy(skel);
}

void test_ns_tc_opts_query(void)
{
	test_tc_opts_query_target(BPF_TCX_INGRESS);
	test_tc_opts_query_target(BPF_TCX_EGRESS);
}

static void test_tc_opts_query_attach_target(int target)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	struct test_tc_link *skel;
	__u32 prog_ids[2];
	__u32 fd1, id1;
	int err;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	id1 = id_from_prog_fd(fd1);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 0, "count");
	ASSERT_EQ(optq.revision, 1, "revision");

	LIBBPF_OPTS_RESET(opta,
		.expected_revision = optq.revision,
	);

	err = bpf_prog_attach_opts(fd1, loopback, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.prog_ids = prog_ids;
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup1;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 2, "revision");
	ASSERT_EQ(optq.prog_ids[0], id1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");

cleanup1:
	err = bpf_prog_detach_opts(fd1, loopback, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(target, 0);
cleanup:
	test_tc_link__destroy(skel);
}

void test_ns_tc_opts_query_attach(void)
{
	test_tc_opts_query_attach_target(BPF_TCX_INGRESS);
	test_tc_opts_query_attach_target(BPF_TCX_EGRESS);
}
