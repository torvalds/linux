// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <bpf/btf.h>

#include "test_log_fixup.skel.h"

enum trunc_type {
	TRUNC_NONE,
	TRUNC_PARTIAL,
	TRUNC_FULL,
};

static void bad_core_relo(size_t log_buf_size, enum trunc_type trunc_type)
{
	char log_buf[8 * 1024];
	struct test_log_fixup* skel;
	int err;

	skel = test_log_fixup__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	bpf_program__set_autoload(skel->progs.bad_relo, true);
	memset(log_buf, 0, sizeof(log_buf));
	bpf_program__set_log_buf(skel->progs.bad_relo, log_buf, log_buf_size ?: sizeof(log_buf));
	bpf_program__set_log_level(skel->progs.bad_relo, 1 | 8); /* BPF_LOG_FIXED to force truncation */

	err = test_log_fixup__load(skel);
	if (!ASSERT_ERR(err, "load_fail"))
		goto cleanup;

	ASSERT_HAS_SUBSTR(log_buf,
			  "0: <invalid CO-RE relocation>\n"
			  "failed to resolve CO-RE relocation <byte_sz> ",
			  "log_buf_part1");

	switch (trunc_type) {
	case TRUNC_NONE:
		ASSERT_HAS_SUBSTR(log_buf,
				  "struct task_struct___bad.fake_field (0:1 @ offset 4)\n",
				  "log_buf_part2");
		ASSERT_HAS_SUBSTR(log_buf,
				  "max_states_per_insn 0 total_states 0 peak_states 0 mark_read 0\n",
				  "log_buf_end");
		break;
	case TRUNC_PARTIAL:
		/* we should get full libbpf message patch */
		ASSERT_HAS_SUBSTR(log_buf,
				  "struct task_struct___bad.fake_field (0:1 @ offset 4)\n",
				  "log_buf_part2");
		/* we shouldn't get full end of BPF verifier log */
		ASSERT_NULL(strstr(log_buf, "max_states_per_insn 0 total_states 0 peak_states 0 mark_read 0\n"),
			    "log_buf_end");
		break;
	case TRUNC_FULL:
		/* we shouldn't get second part of libbpf message patch */
		ASSERT_NULL(strstr(log_buf, "struct task_struct___bad.fake_field (0:1 @ offset 4)\n"),
			    "log_buf_part2");
		/* we shouldn't get full end of BPF verifier log */
		ASSERT_NULL(strstr(log_buf, "max_states_per_insn 0 total_states 0 peak_states 0 mark_read 0\n"),
			    "log_buf_end");
		break;
	}

	if (env.verbosity > VERBOSE_NONE)
		printf("LOG:   \n=================\n%s=================\n", log_buf);
cleanup:
	test_log_fixup__destroy(skel);
}

static void bad_core_relo_subprog(void)
{
	char log_buf[8 * 1024];
	struct test_log_fixup* skel;
	int err;

	skel = test_log_fixup__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	bpf_program__set_autoload(skel->progs.bad_relo_subprog, true);
	bpf_program__set_log_buf(skel->progs.bad_relo_subprog, log_buf, sizeof(log_buf));

	err = test_log_fixup__load(skel);
	if (!ASSERT_ERR(err, "load_fail"))
		goto cleanup;

	ASSERT_HAS_SUBSTR(log_buf,
			  ": <invalid CO-RE relocation>\n"
			  "failed to resolve CO-RE relocation <byte_off> ",
			  "log_buf");
	ASSERT_HAS_SUBSTR(log_buf,
			  "struct task_struct___bad.fake_field_subprog (0:2 @ offset 8)\n",
			  "log_buf");

	if (env.verbosity > VERBOSE_NONE)
		printf("LOG:   \n=================\n%s=================\n", log_buf);

cleanup:
	test_log_fixup__destroy(skel);
}

static void missing_map(void)
{
	char log_buf[8 * 1024];
	struct test_log_fixup* skel;
	int err;

	skel = test_log_fixup__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	bpf_map__set_autocreate(skel->maps.missing_map, false);

	bpf_program__set_autoload(skel->progs.use_missing_map, true);
	bpf_program__set_log_buf(skel->progs.use_missing_map, log_buf, sizeof(log_buf));

	err = test_log_fixup__load(skel);
	if (!ASSERT_ERR(err, "load_fail"))
		goto cleanup;

	ASSERT_TRUE(bpf_map__autocreate(skel->maps.existing_map), "existing_map_autocreate");
	ASSERT_FALSE(bpf_map__autocreate(skel->maps.missing_map), "missing_map_autocreate");

	ASSERT_HAS_SUBSTR(log_buf,
			  ": <invalid BPF map reference>\n"
			  "BPF map 'missing_map' is referenced but wasn't created\n",
			  "log_buf");

	if (env.verbosity > VERBOSE_NONE)
		printf("LOG:   \n=================\n%s=================\n", log_buf);

cleanup:
	test_log_fixup__destroy(skel);
}

static void missing_kfunc(void)
{
	char log_buf[8 * 1024];
	struct test_log_fixup* skel;
	int err;

	skel = test_log_fixup__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	bpf_program__set_autoload(skel->progs.use_missing_kfunc, true);
	bpf_program__set_log_buf(skel->progs.use_missing_kfunc, log_buf, sizeof(log_buf));

	err = test_log_fixup__load(skel);
	if (!ASSERT_ERR(err, "load_fail"))
		goto cleanup;

	ASSERT_HAS_SUBSTR(log_buf,
			  "0: <invalid kfunc call>\n"
			  "kfunc 'bpf_nonexistent_kfunc' is referenced but wasn't resolved\n",
			  "log_buf");

	if (env.verbosity > VERBOSE_NONE)
		printf("LOG:   \n=================\n%s=================\n", log_buf);

cleanup:
	test_log_fixup__destroy(skel);
}

void test_log_fixup(void)
{
	if (test__start_subtest("bad_core_relo_trunc_none"))
		bad_core_relo(0, TRUNC_NONE /* full buf */);
	if (test__start_subtest("bad_core_relo_trunc_partial"))
		bad_core_relo(300, TRUNC_PARTIAL /* truncate original log a bit */);
	if (test__start_subtest("bad_core_relo_trunc_full"))
		bad_core_relo(210, TRUNC_FULL  /* truncate also libbpf's message patch */);
	if (test__start_subtest("bad_core_relo_subprog"))
		bad_core_relo_subprog();
	if (test__start_subtest("missing_map"))
		missing_map();
	if (test__start_subtest("missing_kfunc"))
		missing_kfunc();
}
