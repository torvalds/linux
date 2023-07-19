// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Isovalent */
#include <uapi/linux/if_link.h>
#include <net/if.h>
#include <test_progs.h>

#define loopback 1
#define ping_cmd "ping -q -c1 -w1 127.0.0.1 > /dev/null"

#include "test_tc_link.skel.h"
#include "tc_helpers.h"

void serial_test_tc_links_basic(void)
{
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	LIBBPF_OPTS(bpf_tcx_opts, optl);
	__u32 prog_ids[2], link_ids[2];
	__u32 pid1, pid2, lid1, lid2;
	struct test_tc_link *skel;
	struct bpf_link *link;
	int err;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	pid1 = id_from_prog_fd(bpf_program__fd(skel->progs.tc1));
	pid2 = id_from_prog_fd(bpf_program__fd(skel->progs.tc2));

	ASSERT_NEQ(pid1, pid2, "prog_ids_1_2");

	assert_mprog_count(BPF_TCX_INGRESS, 0);
	assert_mprog_count(BPF_TCX_EGRESS, 0);

	ASSERT_EQ(skel->bss->seen_tc1, false, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc1 = link;

	lid1 = id_from_link_fd(bpf_link__fd(skel->links.tc1));

	assert_mprog_count(BPF_TCX_INGRESS, 1);
	assert_mprog_count(BPF_TCX_EGRESS, 0);

	optq.prog_ids = prog_ids;
	optq.link_ids = link_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, BPF_TCX_INGRESS, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 2, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid1, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid1, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], 0, "link_ids[1]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");

	link = bpf_program__attach_tcx(skel->progs.tc2, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc2 = link;

	lid2 = id_from_link_fd(bpf_link__fd(skel->links.tc2));
	ASSERT_NEQ(lid1, lid2, "link_ids_1_2");

	assert_mprog_count(BPF_TCX_INGRESS, 1);
	assert_mprog_count(BPF_TCX_EGRESS, 1);

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, BPF_TCX_EGRESS, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 2, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid2, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid2, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], 0, "link_ids[1]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
cleanup:
	test_tc_link__destroy(skel);

	assert_mprog_count(BPF_TCX_INGRESS, 0);
	assert_mprog_count(BPF_TCX_EGRESS, 0);
}

static void test_tc_links_before_target(int target)
{
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	LIBBPF_OPTS(bpf_tcx_opts, optl);
	__u32 prog_ids[5], link_ids[5];
	__u32 pid1, pid2, pid3, pid4;
	__u32 lid1, lid2, lid3, lid4;
	struct test_tc_link *skel;
	struct bpf_link *link;
	int err;

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

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc1 = link;

	lid1 = id_from_link_fd(bpf_link__fd(skel->links.tc1));

	assert_mprog_count(target, 1);

	link = bpf_program__attach_tcx(skel->progs.tc2, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc2 = link;

	lid2 = id_from_link_fd(bpf_link__fd(skel->links.tc2));

	assert_mprog_count(target, 2);

	optq.prog_ids = prog_ids;
	optq.link_ids = link_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 3, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid1, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid1, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], pid2, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], lid2, "link_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");
	ASSERT_EQ(optq.link_ids[2], 0, "link_ids[2]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, false, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, false, "seen_tc4");

	skel->bss->seen_tc1 = false;
	skel->bss->seen_tc2 = false;

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE,
		.relative_fd = bpf_program__fd(skel->progs.tc2),
	);

	link = bpf_program__attach_tcx(skel->progs.tc3, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc3 = link;

	lid3 = id_from_link_fd(bpf_link__fd(skel->links.tc3));

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE | BPF_F_LINK,
		.relative_id = lid1,
	);

	link = bpf_program__attach_tcx(skel->progs.tc4, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc4 = link;

	lid4 = id_from_link_fd(bpf_link__fd(skel->links.tc4));

	assert_mprog_count(target, 4);

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 4, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid4, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid4, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], pid1, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], lid1, "link_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], pid3, "prog_ids[2]");
	ASSERT_EQ(optq.link_ids[2], lid3, "link_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], pid2, "prog_ids[3]");
	ASSERT_EQ(optq.link_ids[3], lid2, "link_ids[3]");
	ASSERT_EQ(optq.prog_ids[4], 0, "prog_ids[4]");
	ASSERT_EQ(optq.link_ids[4], 0, "link_ids[4]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, true, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, true, "seen_tc4");
cleanup:
	test_tc_link__destroy(skel);
	assert_mprog_count(target, 0);
}

void serial_test_tc_links_before(void)
{
	test_tc_links_before_target(BPF_TCX_INGRESS);
	test_tc_links_before_target(BPF_TCX_EGRESS);
}

static void test_tc_links_after_target(int target)
{
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	LIBBPF_OPTS(bpf_tcx_opts, optl);
	__u32 prog_ids[5], link_ids[5];
	__u32 pid1, pid2, pid3, pid4;
	__u32 lid1, lid2, lid3, lid4;
	struct test_tc_link *skel;
	struct bpf_link *link;
	int err;

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

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc1 = link;

	lid1 = id_from_link_fd(bpf_link__fd(skel->links.tc1));

	assert_mprog_count(target, 1);

	link = bpf_program__attach_tcx(skel->progs.tc2, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc2 = link;

	lid2 = id_from_link_fd(bpf_link__fd(skel->links.tc2));

	assert_mprog_count(target, 2);

	optq.prog_ids = prog_ids;
	optq.link_ids = link_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 3, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid1, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid1, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], pid2, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], lid2, "link_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");
	ASSERT_EQ(optq.link_ids[2], 0, "link_ids[2]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, false, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, false, "seen_tc4");

	skel->bss->seen_tc1 = false;
	skel->bss->seen_tc2 = false;

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_AFTER,
		.relative_fd = bpf_program__fd(skel->progs.tc1),
	);

	link = bpf_program__attach_tcx(skel->progs.tc3, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc3 = link;

	lid3 = id_from_link_fd(bpf_link__fd(skel->links.tc3));

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_AFTER | BPF_F_LINK,
		.relative_fd = bpf_link__fd(skel->links.tc2),
	);

	link = bpf_program__attach_tcx(skel->progs.tc4, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc4 = link;

	lid4 = id_from_link_fd(bpf_link__fd(skel->links.tc4));

	assert_mprog_count(target, 4);

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 4, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid1, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid1, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], pid3, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], lid3, "link_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], pid2, "prog_ids[2]");
	ASSERT_EQ(optq.link_ids[2], lid2, "link_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], pid4, "prog_ids[3]");
	ASSERT_EQ(optq.link_ids[3], lid4, "link_ids[3]");
	ASSERT_EQ(optq.prog_ids[4], 0, "prog_ids[4]");
	ASSERT_EQ(optq.link_ids[4], 0, "link_ids[4]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, true, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, true, "seen_tc4");
cleanup:
	test_tc_link__destroy(skel);
	assert_mprog_count(target, 0);
}

void serial_test_tc_links_after(void)
{
	test_tc_links_after_target(BPF_TCX_INGRESS);
	test_tc_links_after_target(BPF_TCX_EGRESS);
}

static void test_tc_links_revision_target(int target)
{
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	LIBBPF_OPTS(bpf_tcx_opts, optl);
	__u32 prog_ids[3], link_ids[3];
	__u32 pid1, pid2, lid1, lid2;
	struct test_tc_link *skel;
	struct bpf_link *link;
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

	optl.expected_revision = 1;

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc1 = link;

	lid1 = id_from_link_fd(bpf_link__fd(skel->links.tc1));

	assert_mprog_count(target, 1);

	optl.expected_revision = 1;

	link = bpf_program__attach_tcx(skel->progs.tc2, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 1);

	optl.expected_revision = 2;

	link = bpf_program__attach_tcx(skel->progs.tc2, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc2 = link;

	lid2 = id_from_link_fd(bpf_link__fd(skel->links.tc2));

	assert_mprog_count(target, 2);

	optq.prog_ids = prog_ids;
	optq.link_ids = link_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 3, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid1, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid1, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], pid2, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], lid2, "link_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");
	ASSERT_EQ(optq.link_ids[2], 0, "prog_ids[2]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
cleanup:
	test_tc_link__destroy(skel);
	assert_mprog_count(target, 0);
}

void serial_test_tc_links_revision(void)
{
	test_tc_links_revision_target(BPF_TCX_INGRESS);
	test_tc_links_revision_target(BPF_TCX_EGRESS);
}

static void test_tc_chain_classic(int target, bool chain_tc_old)
{
	LIBBPF_OPTS(bpf_tc_opts, tc_opts, .handle = 1, .priority = 1);
	LIBBPF_OPTS(bpf_tc_hook, tc_hook, .ifindex = loopback);
	bool hook_created = false, tc_attached = false;
	LIBBPF_OPTS(bpf_tcx_opts, optl);
	__u32 pid1, pid2, pid3;
	struct test_tc_link *skel;
	struct bpf_link *link;
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
	pid3 = id_from_prog_fd(bpf_program__fd(skel->progs.tc3));

	ASSERT_NEQ(pid1, pid2, "prog_ids_1_2");
	ASSERT_NEQ(pid2, pid3, "prog_ids_2_3");

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

		tc_opts.prog_fd = bpf_program__fd(skel->progs.tc3);
		err = bpf_tc_attach(&tc_hook, &tc_opts);
		if (!ASSERT_OK(err, "bpf_tc_attach"))
			goto cleanup;
		tc_attached = true;
	}

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc1 = link;

	link = bpf_program__attach_tcx(skel->progs.tc2, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc2 = link;

	assert_mprog_count(target, 2);

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, chain_tc_old, "seen_tc3");

	skel->bss->seen_tc1 = false;
	skel->bss->seen_tc2 = false;
	skel->bss->seen_tc3 = false;

	err = bpf_link__detach(skel->links.tc2);
	if (!ASSERT_OK(err, "prog_detach"))
		goto cleanup;

	assert_mprog_count(target, 1);

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, chain_tc_old, "seen_tc3");
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
	assert_mprog_count(target, 1);
	test_tc_link__destroy(skel);
	assert_mprog_count(target, 0);
}

void serial_test_tc_links_chain_classic(void)
{
	test_tc_chain_classic(BPF_TCX_INGRESS, false);
	test_tc_chain_classic(BPF_TCX_EGRESS, false);
	test_tc_chain_classic(BPF_TCX_INGRESS, true);
	test_tc_chain_classic(BPF_TCX_EGRESS, true);
}

static void test_tc_links_replace_target(int target)
{
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	LIBBPF_OPTS(bpf_tcx_opts, optl);
	__u32 pid1, pid2, pid3, lid1, lid2;
	__u32 prog_ids[4], link_ids[4];
	struct test_tc_link *skel;
	struct bpf_link *link;
	int err;

	skel = test_tc_link__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		goto cleanup;

	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc1, target),
		  0, "tc1_attach_type");
	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc2, target),
		  0, "tc2_attach_type");
	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc3, target),
		  0, "tc3_attach_type");

	err = test_tc_link__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	pid1 = id_from_prog_fd(bpf_program__fd(skel->progs.tc1));
	pid2 = id_from_prog_fd(bpf_program__fd(skel->progs.tc2));
	pid3 = id_from_prog_fd(bpf_program__fd(skel->progs.tc3));

	ASSERT_NEQ(pid1, pid2, "prog_ids_1_2");
	ASSERT_NEQ(pid2, pid3, "prog_ids_2_3");

	assert_mprog_count(target, 0);

	optl.expected_revision = 1;

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc1 = link;

	lid1 = id_from_link_fd(bpf_link__fd(skel->links.tc1));

	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE,
		.relative_id = pid1,
		.expected_revision = 2,
	);

	link = bpf_program__attach_tcx(skel->progs.tc2, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc2 = link;

	lid2 = id_from_link_fd(bpf_link__fd(skel->links.tc2));

	assert_mprog_count(target, 2);

	optq.prog_ids = prog_ids;
	optq.link_ids = link_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 3, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid2, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid2, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], pid1, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], lid1, "link_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");
	ASSERT_EQ(optq.link_ids[2], 0, "link_ids[2]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, false, "seen_tc3");

	skel->bss->seen_tc1 = false;
	skel->bss->seen_tc2 = false;
	skel->bss->seen_tc3 = false;

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_REPLACE,
		.relative_fd = bpf_program__fd(skel->progs.tc2),
		.expected_revision = 3,
	);

	link = bpf_program__attach_tcx(skel->progs.tc3, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 2);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_REPLACE | BPF_F_LINK,
		.relative_fd = bpf_link__fd(skel->links.tc2),
		.expected_revision = 3,
	);

	link = bpf_program__attach_tcx(skel->progs.tc3, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 2);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_REPLACE | BPF_F_LINK | BPF_F_AFTER,
		.relative_id = lid2,
	);

	link = bpf_program__attach_tcx(skel->progs.tc3, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 2);

	err = bpf_link__update_program(skel->links.tc2, skel->progs.tc3);
	if (!ASSERT_OK(err, "link_update"))
		goto cleanup;

	assert_mprog_count(target, 2);

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 4, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid3, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid2, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], pid1, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], lid1, "link_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");
	ASSERT_EQ(optq.link_ids[2], 0, "link_ids[2]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, true, "seen_tc3");

	skel->bss->seen_tc1 = false;
	skel->bss->seen_tc2 = false;
	skel->bss->seen_tc3 = false;

	err = bpf_link__detach(skel->links.tc2);
	if (!ASSERT_OK(err, "link_detach"))
		goto cleanup;

	assert_mprog_count(target, 1);

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid1, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid1, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], 0, "link_ids[1]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, false, "seen_tc3");

	skel->bss->seen_tc1 = false;
	skel->bss->seen_tc2 = false;
	skel->bss->seen_tc3 = false;

	err = bpf_link__update_program(skel->links.tc1, skel->progs.tc1);
	if (!ASSERT_OK(err, "link_update_self"))
		goto cleanup;

	assert_mprog_count(target, 1);

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid1, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid1, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], 0, "link_ids[1]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, false, "seen_tc3");
cleanup:
	test_tc_link__destroy(skel);
	assert_mprog_count(target, 0);
}

void serial_test_tc_links_replace(void)
{
	test_tc_links_replace_target(BPF_TCX_INGRESS);
	test_tc_links_replace_target(BPF_TCX_EGRESS);
}

static void test_tc_links_invalid_target(int target)
{
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	LIBBPF_OPTS(bpf_tcx_opts, optl);
	__u32 pid1, pid2, lid1;
	struct test_tc_link *skel;
	struct bpf_link *link;
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

	optl.flags = BPF_F_BEFORE | BPF_F_AFTER;

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE | BPF_F_ID,
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_AFTER | BPF_F_ID,
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_ID,
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_LINK,
		.relative_fd = bpf_program__fd(skel->progs.tc2),
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_LINK,
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl,
		.relative_fd = bpf_program__fd(skel->progs.tc2),
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE | BPF_F_AFTER,
		.relative_fd = bpf_program__fd(skel->progs.tc2),
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE,
		.relative_fd = bpf_program__fd(skel->progs.tc1),
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_ID,
		.relative_id = pid2,
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_ID,
		.relative_id = 42,
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE,
		.relative_fd = bpf_program__fd(skel->progs.tc1),
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE | BPF_F_LINK,
		.relative_fd = bpf_program__fd(skel->progs.tc1),
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_AFTER,
		.relative_fd = bpf_program__fd(skel->progs.tc1),
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl);

	link = bpf_program__attach_tcx(skel->progs.tc1, 0, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_AFTER | BPF_F_LINK,
		.relative_fd = bpf_program__fd(skel->progs.tc1),
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 0);

	LIBBPF_OPTS_RESET(optl);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc1 = link;

	lid1 = id_from_link_fd(bpf_link__fd(skel->links.tc1));

	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_AFTER | BPF_F_LINK,
		.relative_fd = bpf_program__fd(skel->progs.tc1),
	);

	link = bpf_program__attach_tcx(skel->progs.tc2, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE | BPF_F_LINK | BPF_F_ID,
		.relative_id = ~0,
	);

	link = bpf_program__attach_tcx(skel->progs.tc2, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE | BPF_F_LINK | BPF_F_ID,
		.relative_id = lid1,
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE | BPF_F_ID,
		.relative_id = pid1,
	);

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}
	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE | BPF_F_LINK | BPF_F_ID,
		.relative_id = lid1,
	);

	link = bpf_program__attach_tcx(skel->progs.tc2, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc2 = link;

	assert_mprog_count(target, 2);
cleanup:
	test_tc_link__destroy(skel);
	assert_mprog_count(target, 0);
}

void serial_test_tc_links_invalid(void)
{
	test_tc_links_invalid_target(BPF_TCX_INGRESS);
	test_tc_links_invalid_target(BPF_TCX_EGRESS);
}

static void test_tc_links_prepend_target(int target)
{
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	LIBBPF_OPTS(bpf_tcx_opts, optl);
	__u32 prog_ids[5], link_ids[5];
	__u32 pid1, pid2, pid3, pid4;
	__u32 lid1, lid2, lid3, lid4;
	struct test_tc_link *skel;
	struct bpf_link *link;
	int err;

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

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc1 = link;

	lid1 = id_from_link_fd(bpf_link__fd(skel->links.tc1));

	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE,
	);

	link = bpf_program__attach_tcx(skel->progs.tc2, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc2 = link;

	lid2 = id_from_link_fd(bpf_link__fd(skel->links.tc2));

	assert_mprog_count(target, 2);

	optq.prog_ids = prog_ids;
	optq.link_ids = link_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 3, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid2, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid2, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], pid1, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], lid1, "link_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");
	ASSERT_EQ(optq.link_ids[2], 0, "link_ids[2]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, false, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, false, "seen_tc4");

	skel->bss->seen_tc1 = false;
	skel->bss->seen_tc2 = false;

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE,
	);

	link = bpf_program__attach_tcx(skel->progs.tc3, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc3 = link;

	lid3 = id_from_link_fd(bpf_link__fd(skel->links.tc3));

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE,
	);

	link = bpf_program__attach_tcx(skel->progs.tc4, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc4 = link;

	lid4 = id_from_link_fd(bpf_link__fd(skel->links.tc4));

	assert_mprog_count(target, 4);

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 4, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid4, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid4, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], pid3, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], lid3, "link_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], pid2, "prog_ids[2]");
	ASSERT_EQ(optq.link_ids[2], lid2, "link_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], pid1, "prog_ids[3]");
	ASSERT_EQ(optq.link_ids[3], lid1, "link_ids[3]");
	ASSERT_EQ(optq.prog_ids[4], 0, "prog_ids[4]");
	ASSERT_EQ(optq.link_ids[4], 0, "link_ids[4]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, true, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, true, "seen_tc4");
cleanup:
	test_tc_link__destroy(skel);
	assert_mprog_count(target, 0);
}

void serial_test_tc_links_prepend(void)
{
	test_tc_links_prepend_target(BPF_TCX_INGRESS);
	test_tc_links_prepend_target(BPF_TCX_EGRESS);
}

static void test_tc_links_append_target(int target)
{
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	LIBBPF_OPTS(bpf_tcx_opts, optl);
	__u32 prog_ids[5], link_ids[5];
	__u32 pid1, pid2, pid3, pid4;
	__u32 lid1, lid2, lid3, lid4;
	struct test_tc_link *skel;
	struct bpf_link *link;
	int err;

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

	link = bpf_program__attach_tcx(skel->progs.tc1, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc1 = link;

	lid1 = id_from_link_fd(bpf_link__fd(skel->links.tc1));

	assert_mprog_count(target, 1);

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_AFTER,
	);

	link = bpf_program__attach_tcx(skel->progs.tc2, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc2 = link;

	lid2 = id_from_link_fd(bpf_link__fd(skel->links.tc2));

	assert_mprog_count(target, 2);

	optq.prog_ids = prog_ids;
	optq.link_ids = link_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 3, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid1, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid1, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], pid2, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], lid2, "link_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");
	ASSERT_EQ(optq.link_ids[2], 0, "link_ids[2]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, false, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, false, "seen_tc4");

	skel->bss->seen_tc1 = false;
	skel->bss->seen_tc2 = false;

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_AFTER,
	);

	link = bpf_program__attach_tcx(skel->progs.tc3, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc3 = link;

	lid3 = id_from_link_fd(bpf_link__fd(skel->links.tc3));

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_AFTER,
	);

	link = bpf_program__attach_tcx(skel->progs.tc4, loopback, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc4 = link;

	lid4 = id_from_link_fd(bpf_link__fd(skel->links.tc4));

	assert_mprog_count(target, 4);

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(loopback, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 4, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid1, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid1, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], pid2, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], lid2, "link_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], pid3, "prog_ids[2]");
	ASSERT_EQ(optq.link_ids[2], lid3, "link_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], pid4, "prog_ids[3]");
	ASSERT_EQ(optq.link_ids[3], lid4, "link_ids[3]");
	ASSERT_EQ(optq.prog_ids[4], 0, "prog_ids[4]");
	ASSERT_EQ(optq.link_ids[4], 0, "link_ids[4]");

	ASSERT_OK(system(ping_cmd), ping_cmd);

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
	ASSERT_EQ(skel->bss->seen_tc3, true, "seen_tc3");
	ASSERT_EQ(skel->bss->seen_tc4, true, "seen_tc4");
cleanup:
	test_tc_link__destroy(skel);
	assert_mprog_count(target, 0);
}

void serial_test_tc_links_append(void)
{
	test_tc_links_append_target(BPF_TCX_INGRESS);
	test_tc_links_append_target(BPF_TCX_EGRESS);
}

static void test_tc_links_dev_cleanup_target(int target)
{
	LIBBPF_OPTS(bpf_tcx_opts, optl);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	__u32 pid1, pid2, pid3, pid4;
	struct test_tc_link *skel;
	struct bpf_link *link;
	int err, ifindex;

	ASSERT_OK(system("ip link add dev tcx_opts1 type veth peer name tcx_opts2"), "add veth");
	ifindex = if_nametoindex("tcx_opts1");
	ASSERT_NEQ(ifindex, 0, "non_zero_ifindex");

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

	link = bpf_program__attach_tcx(skel->progs.tc1, ifindex, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc1 = link;

	assert_mprog_count_ifindex(ifindex, target, 1);

	link = bpf_program__attach_tcx(skel->progs.tc2, ifindex, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc2 = link;

	assert_mprog_count_ifindex(ifindex, target, 2);

	link = bpf_program__attach_tcx(skel->progs.tc3, ifindex, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc3 = link;

	assert_mprog_count_ifindex(ifindex, target, 3);

	link = bpf_program__attach_tcx(skel->progs.tc4, ifindex, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc4 = link;

	assert_mprog_count_ifindex(ifindex, target, 4);

	ASSERT_OK(system("ip link del dev tcx_opts1"), "del veth");
	ASSERT_EQ(if_nametoindex("tcx_opts1"), 0, "dev1_removed");
	ASSERT_EQ(if_nametoindex("tcx_opts2"), 0, "dev2_removed");

	ASSERT_EQ(ifindex_from_link_fd(bpf_link__fd(skel->links.tc1)), 0, "tc1_ifindex");
	ASSERT_EQ(ifindex_from_link_fd(bpf_link__fd(skel->links.tc2)), 0, "tc2_ifindex");
	ASSERT_EQ(ifindex_from_link_fd(bpf_link__fd(skel->links.tc3)), 0, "tc3_ifindex");
	ASSERT_EQ(ifindex_from_link_fd(bpf_link__fd(skel->links.tc4)), 0, "tc4_ifindex");

	test_tc_link__destroy(skel);
	return;
cleanup:
	test_tc_link__destroy(skel);

	ASSERT_OK(system("ip link del dev tcx_opts1"), "del veth");
	ASSERT_EQ(if_nametoindex("tcx_opts1"), 0, "dev1_removed");
	ASSERT_EQ(if_nametoindex("tcx_opts2"), 0, "dev2_removed");
}

void serial_test_tc_links_dev_cleanup(void)
{
	test_tc_links_dev_cleanup_target(BPF_TCX_INGRESS);
	test_tc_links_dev_cleanup_target(BPF_TCX_EGRESS);
}
