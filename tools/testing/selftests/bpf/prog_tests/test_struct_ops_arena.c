// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>

#include "struct_ops_arena.skel.h"
#include "struct_ops_arena_attach.skel.h"
#include "struct_ops_arena_fail.skel.h"
#include "struct_ops_arena_member.skel.h"
#include "struct_ops_arena_member_render.skel.h"
#include "struct_ops_arena_member_fail__union.skel.h"
#include "struct_ops_arena_member_fail__xchg.skel.h"
#include "struct_ops_arena_member_fail__mismatch.skel.h"
#include "struct_ops_arena_member_fail__mismatch_mem.skel.h"
#include "struct_ops_arena_member_fail__write.skel.h"
#include "struct_ops_arena_member_fail__no_arena.skel.h"

#if defined(__x86_64__) || defined(__aarch64__)
/*
 * Attach callbacks with __arena and __arena__nullable arguments and drive
 * them through the bpf_testmod_ops3_call_test_arena*() kfuncs.
 */
static void arena_arg(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct struct_ops_arena *skel;
	struct bpf_link *link = NULL;
	int err;

	skel = struct_ops_arena__open_and_load();
	if (!ASSERT_OK_PTR(skel, "struct_ops_arena__open_and_load"))
		return;

	link = bpf_map__attach_struct_ops(skel->maps.testmod_arena);
	if (!ASSERT_OK_PTR(link, "attach_struct_ops"))
		goto out;

	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.trigger),
				     &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "trigger_retval");

out:
	bpf_link__destroy(link);
	struct_ops_arena__destroy(skel);
}

/*
 * A program with no arena cannot attach to a member with an __arena
 * argument.
 */
static void arena_arg_fail(void)
{
	struct struct_ops_arena_fail *skel;

	skel = struct_ops_arena_fail__open_and_load();
	if (ASSERT_ERR_PTR(skel, "struct_ops_arena_fail__open_and_load"))
		return;

	struct_ops_arena_fail__destroy(skel);
}

static void arena_arg_attach_one(int target_fd, const char *prog_name)
{
	struct struct_ops_arena_attach *skel;
	struct bpf_program *prog, *pos;
	char log_buf[64 * 1024];
	int err;

	skel = struct_ops_arena_attach__open();
	if (!ASSERT_OK_PTR(skel, "struct_ops_arena_attach__open"))
		return;

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, prog_name))
		goto out;

	bpf_object__for_each_program(pos, skel->obj)
		bpf_program__set_autoload(pos, pos == prog);

	err = bpf_program__set_attach_target(prog, target_fd, "test_arena_cb");
	if (!ASSERT_OK(err, "set_attach_target"))
		goto out;

	log_buf[0] = '\0';
	bpf_program__set_log_buf(prog, log_buf, sizeof(log_buf));
	err = struct_ops_arena_attach__load(skel);

	ASSERT_EQ(err, -EOPNOTSUPP, prog_name);
	ASSERT_HAS_SUBSTR(log_buf, "Cannot attach to a target with arena context arguments",
			  "verifier_log");

out:
	struct_ops_arena_attach__destroy(skel);
}

static void arena_arg_attach(void)
{
	struct struct_ops_arena *skel;
	int target_fd;

	skel = struct_ops_arena__open_and_load();
	if (!ASSERT_OK_PTR(skel, "struct_ops_arena__open_and_load"))
		return;

	target_fd = bpf_program__fd(skel->progs.test_arena_cb);
	arena_arg_attach_one(target_fd, "fentry_test_arena");
	arena_arg_attach_one(target_fd, "fexit_test_arena");
	arena_arg_attach_one(target_fd, "freplace_test_arena");

	struct_ops_arena__destroy(skel);
}

/* Attach @st_ops and run @trigger once, it returns 0 when everything checked out. */
static void attach_and_trigger(struct bpf_map *st_ops, struct bpf_program *trigger)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct bpf_link *link;
	int err;

	link = bpf_map__attach_struct_ops(st_ops);
	if (!ASSERT_OK_PTR(link, "attach_struct_ops"))
		return;

	err = bpf_prog_test_run_opts(bpf_program__fd(trigger), &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "trigger_retval");

	bpf_link__destroy(link);
}

/*
 * Attach a callback that reads arena pointers out of its argument struct.
 * The header is shared with the module, so the members are arena pointers
 * on the program side as well.
 */
static void arena_member(void)
{
	struct struct_ops_arena_member *skel;

	skel = struct_ops_arena_member__open_and_load();
	if (!ASSERT_OK_PTR(skel, "struct_ops_arena_member__open_and_load"))
		return;

	attach_and_trigger(skel->maps.testmod_arena_member, skel->progs.trigger);
	struct_ops_arena_member__destroy(skel);
}

/* The same through the vmlinux.h rendering of the members, which takes casts. */
static void arena_member_render(void)
{
	struct struct_ops_arena_member_render *skel;

	skel = struct_ops_arena_member_render__open_and_load();
	if (!ASSERT_OK_PTR(skel, "struct_ops_arena_member_render__open_and_load"))
		return;

	attach_and_trigger(skel->maps.testmod_arena_member_render, skel->progs.trigger);
	struct_ops_arena_member_render__destroy(skel);
}
#endif

/*
 * Serialized because it attaches the singleton bpf_testmod_ops3, which
 * test_struct_ops_private_stack also attaches; registering it twice fails
 * with -EEXIST.
 */
void serial_test_struct_ops_arena(void)
{
	/*
	 * Arena struct_ops arguments need JIT support, currently x86-64 and
	 * arm64 only. Elsewhere verification fails with "JIT does not support
	 * arena arguments", so the programs cannot even load.
	 */
#if defined(__x86_64__) || defined(__aarch64__)
	int has_tag;

	if (test__start_subtest("arena_arg"))
		arena_arg();
	if (test__start_subtest("arena_arg_fail"))
		arena_arg_fail();
	if (test__start_subtest("arena_arg_attach"))
		arena_arg_attach();
	/* the tags reach the module BTF only from a toolchain with btf_type_tag support */
	has_tag = testmod_member_has_type_tag("bpf_testmod_arena_member_args", "ptr", "arena");
	if (ASSERT_GE(has_tag, 0, "testmod_btf") && has_tag) {
		if (test__start_subtest("arena_member"))
			arena_member();
		if (test__start_subtest("arena_member_render"))
			arena_member_render();
		RUN_TESTS(struct_ops_arena_member_fail__union);
		RUN_TESTS(struct_ops_arena_member_fail__xchg);
		RUN_TESTS(struct_ops_arena_member_fail__mismatch);
		RUN_TESTS(struct_ops_arena_member_fail__mismatch_mem);
		RUN_TESTS(struct_ops_arena_member_fail__write);
		RUN_TESTS(struct_ops_arena_member_fail__no_arena);
	} else if (test__start_subtest("arena_member")) {
		/* so that the missing member tests show up as skipped */
		test__skip();
	}
#else
	test__skip();
#endif
}
