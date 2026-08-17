// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Google LLC */
#include <linux/bpf.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <test_progs.h>
#include <cgroup_helpers.h>
#include "cgroup_skb_direct_packet_access.skel.h"

#define OLD_QUERY_SIZE		offsetofend(union bpf_attr, query.prog_cnt)
#define FULL_QUERY_SIZE		offsetofend(union bpf_attr, query.revision)

static void test_query_size_boundaries(void)
{
	struct cgroup_skb_direct_packet_access *skel;
	struct bpf_link *link = NULL;
	union bpf_attr attr;
	int cg_fd = -1;
	int err;

	skel = cgroup_skb_direct_packet_access__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		return;

	cg_fd = test__join_cgroup("/attr_size_cg");
	if (!ASSERT_GE(cg_fd, 0, "join_cgroup"))
		goto cleanup;

	link = bpf_program__attach_cgroup(skel->progs.direct_packet_access,
					  cg_fd);
	if (!ASSERT_OK_PTR(link, "cg_attach"))
		goto cleanup;

	memset(&attr, 0, sizeof(attr));
	attr.query.target_fd = cg_fd;
	attr.query.attach_type = BPF_CGROUP_INET_INGRESS;
	attr.query.revision = 0xdeadbeefdeadbeefULL;

	err = syscall(__NR_bpf, BPF_PROG_QUERY, &attr, OLD_QUERY_SIZE);
	if (ASSERT_OK(err, "query_old_size")) {
		ASSERT_EQ(attr.query.prog_cnt, 1, "prog_cnt_written_old");
		ASSERT_EQ(attr.query.revision, 0xdeadbeefdeadbeefULL,
			  "revision_not_written_old");
	}

	memset(&attr, 0, sizeof(attr));
	attr.query.target_fd = cg_fd;
	attr.query.attach_type = BPF_CGROUP_INET_INGRESS;

	err = syscall(__NR_bpf, BPF_PROG_QUERY, &attr, FULL_QUERY_SIZE);
	if (!ASSERT_OK(err, "query_full_size"))
		goto cleanup;

	ASSERT_EQ(attr.query.prog_cnt, 1, "prog_cnt_written");
	ASSERT_GT(attr.query.revision, 0, "revision_written");

cleanup:
	if (link)
		bpf_link__destroy(link);
	if (cg_fd >= 0)
		close(cg_fd);
	cgroup_skb_direct_packet_access__destroy(skel);
}

static void test_map_info_tail_zero(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, map_opts);
	struct bpf_map_info_fake {
		__u8 info[offsetofend(struct bpf_map_info, hash_size)];
		__u32 pad;
	} info = {
		.pad = 1,
	};
	int map_fd, err;
	__u32 info_len;

	map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "arr", sizeof(int), 1, 1, &map_opts);
	if (!ASSERT_GE(map_fd, 0, "bpf_map_create"))
		return;

	info_len = sizeof(info);
	err = bpf_obj_get_info_by_fd(map_fd, &info, &info_len);
	ASSERT_EQ(err, -E2BIG, "bpf_obj_get_info_by_fd");

	close(map_fd);
}

static void test_prog_info_tail_zero(void)
{
	LIBBPF_OPTS(bpf_prog_load_opts, prog_opts);
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	struct bpf_prog_info_fake {
		__u8 info[offsetofend(struct bpf_prog_info, attach_btf_id)];
		__u32 pad;
	} info = {
		.pad = 1,
	};
	int prog_fd, err;
	__u32 info_len;

	prog_fd = bpf_prog_load(BPF_PROG_TYPE_SOCKET_FILTER, "test_prog", "GPL", insns,
				ARRAY_SIZE(insns), &prog_opts);
	if (!ASSERT_GE(prog_fd, 0, "bpf_prog_load"))
		return;

	info_len = sizeof(info);
	err = bpf_obj_get_info_by_fd(prog_fd, &info, &info_len);
	ASSERT_EQ(err, -E2BIG, "bpf_obj_get_info_by_fd");

	close(prog_fd);
}

void test_bpf_attr_size(void)
{
	if (test__start_subtest("query_size_boundaries"))
		test_query_size_boundaries();
	if (test__start_subtest("map_info_tail_zero"))
		test_map_info_tail_zero();
	if (test__start_subtest("prog_info_tail_zero"))
		test_prog_info_tail_zero();
}
