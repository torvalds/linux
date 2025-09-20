// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include "cgroup_helpers.h"
#include "cgroup_mprog.skel.h"

static void assert_mprog_count(int cg, int atype, int expected)
{
	__u32 count = 0, attach_flags = 0;
	int err;

	err = bpf_prog_query(cg, atype, 0, &attach_flags,
			     NULL, &count);
	ASSERT_EQ(count, expected, "count");
	ASSERT_EQ(err, 0, "prog_query");
}

static void test_prog_attach_detach(int atype)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	__u32 fd1, fd2, fd3, fd4, id1, id2, id3, id4;
	struct cgroup_mprog *skel;
	__u32 prog_ids[10];
	int cg, err;

	cg = test__join_cgroup("/prog_attach_detach");
	if (!ASSERT_GE(cg, 0, "join_cgroup /prog_attach_detach"))
		return;

	skel = cgroup_mprog__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.getsockopt_1);
	fd2 = bpf_program__fd(skel->progs.getsockopt_2);
	fd3 = bpf_program__fd(skel->progs.getsockopt_3);
	fd4 = bpf_program__fd(skel->progs.getsockopt_4);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);
	id3 = id_from_prog_fd(fd3);
	id4 = id_from_prog_fd(fd4);

	assert_mprog_count(cg, atype, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI | BPF_F_BEFORE | BPF_F_AFTER,
		.expected_revision = 1,
	);

	/* ordering: [fd1] */
	err = bpf_prog_attach_opts(fd1, cg, atype, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count(cg, atype, 1);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI | BPF_F_BEFORE,
		.expected_revision = 2,
	);

	/* ordering: [fd2, fd1] */
	err = bpf_prog_attach_opts(fd2, cg, atype, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup1;

	assert_mprog_count(cg, atype, 2);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI | BPF_F_AFTER,
		.relative_fd = fd2,
		.expected_revision = 3,
	);

	/* ordering: [fd2, fd3, fd1] */
	err = bpf_prog_attach_opts(fd3, cg, atype, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup2;

	assert_mprog_count(cg, atype, 3);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI,
		.expected_revision = 4,
	);

	/* ordering: [fd2, fd3, fd1, fd4] */
	err = bpf_prog_attach_opts(fd4, cg, atype, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup3;

	assert_mprog_count(cg, atype, 4);

	/* retrieve optq.prog_cnt */
	err = bpf_prog_query_opts(cg, atype, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	/* optq.prog_cnt will be used in below query */
	memset(prog_ids, 0, sizeof(prog_ids));
	optq.prog_ids = prog_ids;
	err = bpf_prog_query_opts(cg, atype, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(optq.count, 4, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], id2, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id3, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id1, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], id4, "prog_ids[3]");
	ASSERT_EQ(optq.prog_ids[4], 0, "prog_ids[4]");
	ASSERT_EQ(optq.link_ids, NULL, "link_ids");

cleanup4:
	optd.expected_revision = 5;
	err = bpf_prog_detach_opts(fd4, cg, atype, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(cg, atype, 3);

cleanup3:
	LIBBPF_OPTS_RESET(optd);
	err = bpf_prog_detach_opts(fd3, cg, atype, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(cg, atype, 2);

	/* Check revision after two detach operations */
	err = bpf_prog_query_opts(cg, atype, &optq);
	ASSERT_OK(err, "prog_query");
	ASSERT_EQ(optq.revision, 7, "revision");

cleanup2:
	err = bpf_prog_detach_opts(fd2, cg, atype, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(cg, atype, 1);

cleanup1:
	err = bpf_prog_detach_opts(fd1, cg, atype, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(cg, atype, 0);

cleanup:
	cgroup_mprog__destroy(skel);
	close(cg);
}

static void test_link_attach_detach(int atype)
{
	LIBBPF_OPTS(bpf_cgroup_opts, opta);
	LIBBPF_OPTS(bpf_cgroup_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	struct bpf_link *link1, *link2, *link3, *link4;
	__u32 fd1, fd2, fd3, fd4, id1, id2, id3, id4;
	struct cgroup_mprog *skel;
	__u32 prog_ids[10];
	int cg, err;

	cg = test__join_cgroup("/link_attach_detach");
	if (!ASSERT_GE(cg, 0, "join_cgroup /link_attach_detach"))
		return;

	skel = cgroup_mprog__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.getsockopt_1);
	fd2 = bpf_program__fd(skel->progs.getsockopt_2);
	fd3 = bpf_program__fd(skel->progs.getsockopt_3);
	fd4 = bpf_program__fd(skel->progs.getsockopt_4);

	id1 = id_from_prog_fd(fd1);
	id2 = id_from_prog_fd(fd2);
	id3 = id_from_prog_fd(fd3);
	id4 = id_from_prog_fd(fd4);

	assert_mprog_count(cg, atype, 0);

	LIBBPF_OPTS_RESET(opta,
		.expected_revision = 1,
	);

	/* ordering: [fd1] */
	link1 = bpf_program__attach_cgroup_opts(skel->progs.getsockopt_1, cg, &opta);
	if (!ASSERT_OK_PTR(link1, "link_attach"))
		goto cleanup;

	assert_mprog_count(cg, atype, 1);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_BEFORE | BPF_F_LINK,
		.relative_id = id_from_link_fd(bpf_link__fd(link1)),
		.expected_revision = 2,
	);

	/* ordering: [fd2, fd1] */
	link2 = bpf_program__attach_cgroup_opts(skel->progs.getsockopt_2, cg, &opta);
	if (!ASSERT_OK_PTR(link2, "link_attach"))
		goto cleanup1;

	assert_mprog_count(cg, atype, 2);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_AFTER | BPF_F_LINK,
		.relative_fd = bpf_link__fd(link2),
		.expected_revision = 3,
	);

	/* ordering: [fd2, fd3, fd1] */
	link3 = bpf_program__attach_cgroup_opts(skel->progs.getsockopt_3, cg, &opta);
	if (!ASSERT_OK_PTR(link3, "link_attach"))
		goto cleanup2;

	assert_mprog_count(cg, atype, 3);

	LIBBPF_OPTS_RESET(opta,
		.expected_revision = 4,
	);

	/* ordering: [fd2, fd3, fd1, fd4] */
	link4 = bpf_program__attach_cgroup_opts(skel->progs.getsockopt_4, cg, &opta);
	if (!ASSERT_OK_PTR(link4, "link_attach"))
		goto cleanup3;

	assert_mprog_count(cg, atype, 4);

	/* retrieve optq.prog_cnt */
	err = bpf_prog_query_opts(cg, atype, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	/* optq.prog_cnt will be used in below query */
	memset(prog_ids, 0, sizeof(prog_ids));
	optq.prog_ids = prog_ids;
	err = bpf_prog_query_opts(cg, atype, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup4;

	ASSERT_EQ(optq.count, 4, "count");
	ASSERT_EQ(optq.revision, 5, "revision");
	ASSERT_EQ(optq.prog_ids[0], id2, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], id3, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], id1, "prog_ids[2]");
	ASSERT_EQ(optq.prog_ids[3], id4, "prog_ids[3]");
	ASSERT_EQ(optq.prog_ids[4], 0, "prog_ids[4]");
	ASSERT_EQ(optq.link_ids, NULL, "link_ids");

cleanup4:
	bpf_link__destroy(link4);
	assert_mprog_count(cg, atype, 3);

cleanup3:
	bpf_link__destroy(link3);
	assert_mprog_count(cg, atype, 2);

	/* Check revision after two detach operations */
	err = bpf_prog_query_opts(cg, atype, &optq);
	ASSERT_OK(err, "prog_query");
	ASSERT_EQ(optq.revision, 7, "revision");

cleanup2:
	bpf_link__destroy(link2);
	assert_mprog_count(cg, atype, 1);

cleanup1:
	bpf_link__destroy(link1);
	assert_mprog_count(cg, atype, 0);

cleanup:
	cgroup_mprog__destroy(skel);
	close(cg);
}

static void test_preorder_prog_attach_detach(int atype)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	__u32 fd1, fd2, fd3, fd4;
	struct cgroup_mprog *skel;
	int cg, err;

	cg = test__join_cgroup("/preorder_prog_attach_detach");
	if (!ASSERT_GE(cg, 0, "join_cgroup /preorder_prog_attach_detach"))
		return;

	skel = cgroup_mprog__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.getsockopt_1);
	fd2 = bpf_program__fd(skel->progs.getsockopt_2);
	fd3 = bpf_program__fd(skel->progs.getsockopt_3);
	fd4 = bpf_program__fd(skel->progs.getsockopt_4);

	assert_mprog_count(cg, atype, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI,
		.expected_revision = 1,
	);

	/* ordering: [fd1] */
	err = bpf_prog_attach_opts(fd1, cg, atype, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count(cg, atype, 1);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI | BPF_F_PREORDER,
		.expected_revision = 2,
	);

	/* ordering: [fd1, fd2] */
	err = bpf_prog_attach_opts(fd2, cg, atype, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup1;

	assert_mprog_count(cg, atype, 2);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI | BPF_F_AFTER,
		.relative_fd = fd2,
		.expected_revision = 3,
	);

	err = bpf_prog_attach_opts(fd3, cg, atype, &opta);
	if (!ASSERT_EQ(err, -EINVAL, "prog_attach"))
		goto cleanup2;

	assert_mprog_count(cg, atype, 2);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI | BPF_F_AFTER | BPF_F_PREORDER,
		.relative_fd = fd2,
		.expected_revision = 3,
	);

	/* ordering: [fd1, fd2, fd3] */
	err = bpf_prog_attach_opts(fd3, cg, atype, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup2;

	assert_mprog_count(cg, atype, 3);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI,
		.expected_revision = 4,
	);

	/* ordering: [fd2, fd3, fd1, fd4] */
	err = bpf_prog_attach_opts(fd4, cg, atype, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup3;

	assert_mprog_count(cg, atype, 4);

	err = bpf_prog_detach_opts(fd4, cg, atype, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(cg, atype, 3);

cleanup3:
	err = bpf_prog_detach_opts(fd3, cg, atype, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(cg, atype, 2);

cleanup2:
	err = bpf_prog_detach_opts(fd2, cg, atype, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(cg, atype, 1);

cleanup1:
	err = bpf_prog_detach_opts(fd1, cg, atype, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count(cg, atype, 0);

cleanup:
	cgroup_mprog__destroy(skel);
	close(cg);
}

static void test_preorder_link_attach_detach(int atype)
{
	LIBBPF_OPTS(bpf_cgroup_opts, opta);
	struct bpf_link *link1, *link2, *link3, *link4;
	struct cgroup_mprog *skel;
	__u32 fd2;
	int cg;

	cg = test__join_cgroup("/preorder_link_attach_detach");
	if (!ASSERT_GE(cg, 0, "join_cgroup /preorder_link_attach_detach"))
		return;

	skel = cgroup_mprog__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd2 = bpf_program__fd(skel->progs.getsockopt_2);

	assert_mprog_count(cg, atype, 0);

	LIBBPF_OPTS_RESET(opta,
		.expected_revision = 1,
	);

	/* ordering: [fd1] */
	link1 = bpf_program__attach_cgroup_opts(skel->progs.getsockopt_1, cg, &opta);
	if (!ASSERT_OK_PTR(link1, "link_attach"))
		goto cleanup;

	assert_mprog_count(cg, atype, 1);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_PREORDER,
		.expected_revision = 2,
	);

	/* ordering: [fd1, fd2] */
	link2 = bpf_program__attach_cgroup_opts(skel->progs.getsockopt_2, cg, &opta);
	if (!ASSERT_OK_PTR(link2, "link_attach"))
		goto cleanup1;

	assert_mprog_count(cg, atype, 2);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_AFTER,
		.relative_fd = fd2,
		.expected_revision = 3,
	);

	link3 = bpf_program__attach_cgroup_opts(skel->progs.getsockopt_3, cg, &opta);
	if (!ASSERT_ERR_PTR(link3, "link_attach"))
		goto cleanup2;

	assert_mprog_count(cg, atype, 2);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_AFTER | BPF_F_PREORDER | BPF_F_LINK,
		.relative_fd = bpf_link__fd(link2),
		.expected_revision = 3,
	);

	/* ordering: [fd1, fd2, fd3] */
	link3 = bpf_program__attach_cgroup_opts(skel->progs.getsockopt_3, cg, &opta);
	if (!ASSERT_OK_PTR(link3, "link_attach"))
		goto cleanup2;

	assert_mprog_count(cg, atype, 3);

	LIBBPF_OPTS_RESET(opta,
		.expected_revision = 4,
	);

	/* ordering: [fd2, fd3, fd1, fd4] */
	link4 = bpf_program__attach_cgroup_opts(skel->progs.getsockopt_4, cg, &opta);
	if (!ASSERT_OK_PTR(link4, "prog_attach"))
		goto cleanup3;

	assert_mprog_count(cg, atype, 4);

	bpf_link__destroy(link4);
	assert_mprog_count(cg, atype, 3);

cleanup3:
	bpf_link__destroy(link3);
	assert_mprog_count(cg, atype, 2);

cleanup2:
	bpf_link__destroy(link2);
	assert_mprog_count(cg, atype, 1);

cleanup1:
	bpf_link__destroy(link1);
	assert_mprog_count(cg, atype, 0);

cleanup:
	cgroup_mprog__destroy(skel);
	close(cg);
}

static void test_invalid_attach_detach(int atype)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	__u32 fd1, fd2, id2;
	struct cgroup_mprog *skel;
	int cg, err;

	cg = test__join_cgroup("/invalid_attach_detach");
	if (!ASSERT_GE(cg, 0, "join_cgroup /invalid_attach_detach"))
		return;

	skel = cgroup_mprog__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.getsockopt_1);
	fd2 = bpf_program__fd(skel->progs.getsockopt_2);

	id2 = id_from_prog_fd(fd2);

	assert_mprog_count(cg, atype, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI | BPF_F_BEFORE | BPF_F_AFTER,
		.relative_id = id2,
	);

	err = bpf_prog_attach_opts(fd1, cg, atype, &opta);
	ASSERT_EQ(err, -EINVAL, "prog_attach");
	assert_mprog_count(cg, atype, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI | BPF_F_BEFORE | BPF_F_ID,
	);

	err = bpf_prog_attach_opts(fd1, cg, atype, &opta);
	ASSERT_EQ(err, -ENOENT, "prog_attach");
	assert_mprog_count(cg, atype, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI | BPF_F_AFTER | BPF_F_ID,
	);

	err = bpf_prog_attach_opts(fd1, cg, atype, &opta);
	ASSERT_EQ(err, -ENOENT, "prog_attach");
	assert_mprog_count(cg, atype, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI | BPF_F_BEFORE | BPF_F_AFTER,
		.relative_id = id2,
	);

	err = bpf_prog_attach_opts(fd1, cg, atype, &opta);
	ASSERT_EQ(err, -EINVAL, "prog_attach");
	assert_mprog_count(cg, atype, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI | BPF_F_LINK,
		.relative_id = id2,
	);

	err = bpf_prog_attach_opts(fd1, cg, atype, &opta);
	ASSERT_EQ(err, -EINVAL, "prog_attach");
	assert_mprog_count(cg, atype, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI,
		.relative_id = id2,
	);

	err = bpf_prog_attach_opts(fd1, cg, atype, &opta);
	ASSERT_EQ(err, -EINVAL, "prog_attach");
	assert_mprog_count(cg, atype, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI | BPF_F_BEFORE,
		.relative_fd = fd1,
	);

	err = bpf_prog_attach_opts(fd1, cg, atype, &opta);
	ASSERT_EQ(err, -ENOENT, "prog_attach");
	assert_mprog_count(cg, atype, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI | BPF_F_AFTER,
		.relative_fd = fd1,
	);

	err = bpf_prog_attach_opts(fd1, cg, atype, &opta);
	ASSERT_EQ(err, -ENOENT, "prog_attach");
	assert_mprog_count(cg, atype, 0);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI,
	);

	err = bpf_prog_attach_opts(fd1, cg, atype, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;
	assert_mprog_count(cg, atype, 1);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI | BPF_F_AFTER,
	);

	err = bpf_prog_attach_opts(fd1, cg, atype, &opta);
	ASSERT_EQ(err, -EINVAL, "prog_attach");
	assert_mprog_count(cg, atype, 1);

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_ALLOW_MULTI | BPF_F_REPLACE | BPF_F_AFTER,
		.replace_prog_fd = fd1,
	);

	err = bpf_prog_attach_opts(fd1, cg, atype, &opta);
	ASSERT_EQ(err, -EINVAL, "prog_attach");
	assert_mprog_count(cg, atype, 1);
cleanup:
	cgroup_mprog__destroy(skel);
	close(cg);
}

void test_cgroup_mprog_opts(void)
{
	if (test__start_subtest("prog_attach_detach"))
		test_prog_attach_detach(BPF_CGROUP_GETSOCKOPT);
	if (test__start_subtest("link_attach_detach"))
		test_link_attach_detach(BPF_CGROUP_GETSOCKOPT);
	if (test__start_subtest("preorder_prog_attach_detach"))
		test_preorder_prog_attach_detach(BPF_CGROUP_GETSOCKOPT);
	if (test__start_subtest("preorder_link_attach_detach"))
		test_preorder_link_attach_detach(BPF_CGROUP_GETSOCKOPT);
	if (test__start_subtest("invalid_attach_detach"))
		test_invalid_attach_detach(BPF_CGROUP_GETSOCKOPT);
}
