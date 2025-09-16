// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Red Hat, Inc. */
#include <test_progs.h>
#include "fentry_recursive.skel.h"
#include "fentry_recursive_target.skel.h"
#include <bpf/btf.h>
#include "bpf/libbpf_internal.h"

/* Test recursive attachment of tracing progs with more than one nesting level
 * is not possible. Create a chain of attachment, verify that the last prog
 * will fail. Depending on the arguments, following cases are tested:
 *
 * - Recursive loading of tracing progs, without attaching (attach = false,
 *   detach = false). The chain looks like this:
 *       load target
 *       load fentry1 -> target
 *       load fentry2 -> fentry1 (fail)
 *
 * - Recursive attach of tracing progs (attach = true, detach = false). The
 *   chain looks like this:
 *       load target
 *       load fentry1 -> target
 *       attach fentry1 -> target
 *       load fentry2 -> fentry1 (fail)
 *
 * - Recursive attach and detach of tracing progs (attach = true, detach =
 *   true). This validates that attach_tracing_prog flag will be set throughout
 *   the whole lifecycle of an fentry prog, independently from whether it's
 *   detached. The chain looks like this:
 *       load target
 *       load fentry1 -> target
 *       attach fentry1 -> target
 *       detach fentry1
 *       load fentry2 -> fentry1 (fail)
 */
static void test_recursive_fentry_chain(bool attach, bool detach)
{
	struct fentry_recursive_target *target_skel = NULL;
	struct fentry_recursive *tracing_chain[2] = {};
	struct bpf_program *prog;
	int prev_fd, err;

	target_skel = fentry_recursive_target__open_and_load();
	if (!ASSERT_OK_PTR(target_skel, "fentry_recursive_target__open_and_load"))
		return;

	/* Create an attachment chain with two fentry progs */
	for (int i = 0; i < 2; i++) {
		tracing_chain[i] = fentry_recursive__open();
		if (!ASSERT_OK_PTR(tracing_chain[i], "fentry_recursive__open"))
			goto close_prog;

		/* The first prog in the chain is going to be attached to the target
		 * fentry program, the second one to the previous in the chain.
		 */
		prog = tracing_chain[i]->progs.recursive_attach;
		if (i == 0) {
			prev_fd = bpf_program__fd(target_skel->progs.test1);
			err = bpf_program__set_attach_target(prog, prev_fd, "test1");
		} else {
			prev_fd = bpf_program__fd(tracing_chain[i-1]->progs.recursive_attach);
			err = bpf_program__set_attach_target(prog, prev_fd, "recursive_attach");
		}

		if (!ASSERT_OK(err, "bpf_program__set_attach_target"))
			goto close_prog;

		err = fentry_recursive__load(tracing_chain[i]);
		/* The first attach should succeed, the second fail */
		if (i == 0) {
			if (!ASSERT_OK(err, "fentry_recursive__load"))
				goto close_prog;

			if (attach) {
				err = fentry_recursive__attach(tracing_chain[i]);
				if (!ASSERT_OK(err, "fentry_recursive__attach"))
					goto close_prog;
			}

			if (detach) {
				/* Flag attach_tracing_prog should still be set, preventing
				 * attachment of the following prog.
				 */
				fentry_recursive__detach(tracing_chain[i]);
			}
		} else {
			if (!ASSERT_ERR(err, "fentry_recursive__load"))
				goto close_prog;
		}
	}

close_prog:
	fentry_recursive_target__destroy(target_skel);
	for (int i = 0; i < 2; i++) {
		fentry_recursive__destroy(tracing_chain[i]);
	}
}

void test_recursive_fentry(void)
{
	if (test__start_subtest("attach"))
		test_recursive_fentry_chain(true, false);
	if (test__start_subtest("load"))
		test_recursive_fentry_chain(false, false);
	if (test__start_subtest("detach"))
		test_recursive_fentry_chain(true, true);
}

/* Test that a tracing prog reattachment (when we land in
 * "prog->aux->dst_trampoline and tgt_prog is NULL" branch in
 * bpf_tracing_prog_attach) does not lead to a crash due to missing attach_btf
 */
void test_fentry_attach_btf_presence(void)
{
	struct fentry_recursive_target *target_skel = NULL;
	struct fentry_recursive *tracing_skel = NULL;
	struct bpf_program *prog;
	int err, link_fd, tgt_prog_fd;

	target_skel = fentry_recursive_target__open_and_load();
	if (!ASSERT_OK_PTR(target_skel, "fentry_recursive_target__open_and_load"))
		goto close_prog;

	tracing_skel = fentry_recursive__open();
	if (!ASSERT_OK_PTR(tracing_skel, "fentry_recursive__open"))
		goto close_prog;

	prog = tracing_skel->progs.recursive_attach;
	tgt_prog_fd = bpf_program__fd(target_skel->progs.fentry_target);
	err = bpf_program__set_attach_target(prog, tgt_prog_fd, "fentry_target");
	if (!ASSERT_OK(err, "bpf_program__set_attach_target"))
		goto close_prog;

	err = fentry_recursive__load(tracing_skel);
	if (!ASSERT_OK(err, "fentry_recursive__load"))
		goto close_prog;

	tgt_prog_fd = bpf_program__fd(tracing_skel->progs.recursive_attach);
	link_fd = bpf_link_create(tgt_prog_fd, 0, BPF_TRACE_FENTRY, NULL);
	if (!ASSERT_GE(link_fd, 0, "link_fd"))
		goto close_prog;

	fentry_recursive__detach(tracing_skel);

	err = fentry_recursive__attach(tracing_skel);
	ASSERT_ERR(err, "fentry_recursive__attach");

close_prog:
	fentry_recursive_target__destroy(target_skel);
	fentry_recursive__destroy(tracing_skel);
}

static void *fentry_target_test_run(void *arg)
{
	for (;;) {
		int prog_fd = __atomic_load_n((int *)arg, __ATOMIC_SEQ_CST);
		LIBBPF_OPTS(bpf_test_run_opts, topts);
		int err;

		if (prog_fd == -1)
			break;
		err = bpf_prog_test_run_opts(prog_fd, &topts);
		if (!ASSERT_OK(err, "fentry_target test_run"))
			break;
	}

	return NULL;
}

void test_fentry_attach_stress(void)
{
	struct fentry_recursive_target *target_skel = NULL;
	struct fentry_recursive *tracing_skel = NULL;
	struct bpf_program *prog;
	int err, i, tgt_prog_fd;
	pthread_t thread;

	target_skel = fentry_recursive_target__open_and_load();
	if (!ASSERT_OK_PTR(target_skel,
			   "fentry_recursive_target__open_and_load"))
		goto close_prog;
	tgt_prog_fd = bpf_program__fd(target_skel->progs.fentry_target);
	err = pthread_create(&thread, NULL,
			     fentry_target_test_run, &tgt_prog_fd);
	if (!ASSERT_OK(err, "bpf_program__set_attach_target"))
		goto close_prog;

	for (i = 0; i < 1000; i++) {
		tracing_skel = fentry_recursive__open();
		if (!ASSERT_OK_PTR(tracing_skel, "fentry_recursive__open"))
			goto stop_thread;

		prog = tracing_skel->progs.recursive_attach;
		err = bpf_program__set_attach_target(prog, tgt_prog_fd,
						     "fentry_target");
		if (!ASSERT_OK(err, "bpf_program__set_attach_target"))
			goto stop_thread;

		err = fentry_recursive__load(tracing_skel);
		if (!ASSERT_OK(err, "fentry_recursive__load"))
			goto stop_thread;

		err = fentry_recursive__attach(tracing_skel);
		if (!ASSERT_OK(err, "fentry_recursive__attach"))
			goto stop_thread;

		fentry_recursive__destroy(tracing_skel);
		tracing_skel = NULL;
	}

stop_thread:
	__atomic_store_n(&tgt_prog_fd, -1, __ATOMIC_SEQ_CST);
	err = pthread_join(thread, NULL);
	ASSERT_OK(err, "pthread_join");
close_prog:
	fentry_recursive__destroy(tracing_skel);
	fentry_recursive_target__destroy(target_skel);
}
