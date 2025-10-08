// SPDX-License-Identifier: GPL-2.0
#include <unistd.h>
#include <test_progs.h>
#include <network_helpers.h>
#include "tailcall_poke.skel.h"
#include "tailcall_bpf2bpf_hierarchy2.skel.h"
#include "tailcall_bpf2bpf_hierarchy3.skel.h"
#include "tailcall_freplace.skel.h"
#include "tc_bpf2bpf.skel.h"
#include "tailcall_fail.skel.h"

/* test_tailcall_1 checks basic functionality by patching multiple locations
 * in a single program for a single tail call slot with nop->jmp, jmp->nop
 * and jmp->jmp rewrites. Also checks for nop->nop.
 */
static void test_tailcall_1(void)
{
	int err, map_fd, prog_fd, main_fd, i, j;
	struct bpf_map *prog_array;
	struct bpf_program *prog;
	struct bpf_object *obj;
	char prog_name[32];
	char buff[128] = {};
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = buff,
		.data_size_in = sizeof(buff),
		.repeat = 1,
	);

	err = bpf_prog_test_load("tailcall1.bpf.o", BPF_PROG_TYPE_SCHED_CLS, &obj,
				 &prog_fd);
	if (CHECK_FAIL(err))
		return;

	prog = bpf_object__find_program_by_name(obj, "entry");
	if (CHECK_FAIL(!prog))
		goto out;

	main_fd = bpf_program__fd(prog);
	if (CHECK_FAIL(main_fd < 0))
		goto out;

	prog_array = bpf_object__find_map_by_name(obj, "jmp_table");
	if (CHECK_FAIL(!prog_array))
		goto out;

	map_fd = bpf_map__fd(prog_array);
	if (CHECK_FAIL(map_fd < 0))
		goto out;

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		snprintf(prog_name, sizeof(prog_name), "classifier_%d", i);

		prog = bpf_object__find_program_by_name(obj, prog_name);
		if (CHECK_FAIL(!prog))
			goto out;

		prog_fd = bpf_program__fd(prog);
		if (CHECK_FAIL(prog_fd < 0))
			goto out;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;
	}

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		err = bpf_prog_test_run_opts(main_fd, &topts);
		ASSERT_OK(err, "tailcall");
		ASSERT_EQ(topts.retval, i, "tailcall retval");

		err = bpf_map_delete_elem(map_fd, &i);
		if (CHECK_FAIL(err))
			goto out;
	}

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, 3, "tailcall retval");

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		snprintf(prog_name, sizeof(prog_name), "classifier_%d", i);

		prog = bpf_object__find_program_by_name(obj, prog_name);
		if (CHECK_FAIL(!prog))
			goto out;

		prog_fd = bpf_program__fd(prog);
		if (CHECK_FAIL(prog_fd < 0))
			goto out;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;
	}

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_OK(topts.retval, "tailcall retval");

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		j = bpf_map__max_entries(prog_array) - 1 - i;
		snprintf(prog_name, sizeof(prog_name), "classifier_%d", j);

		prog = bpf_object__find_program_by_name(obj, prog_name);
		if (CHECK_FAIL(!prog))
			goto out;

		prog_fd = bpf_program__fd(prog);
		if (CHECK_FAIL(prog_fd < 0))
			goto out;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;
	}

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		j = bpf_map__max_entries(prog_array) - 1 - i;

		err = bpf_prog_test_run_opts(main_fd, &topts);
		ASSERT_OK(err, "tailcall");
		ASSERT_EQ(topts.retval, j, "tailcall retval");

		err = bpf_map_delete_elem(map_fd, &i);
		if (CHECK_FAIL(err))
			goto out;
	}

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, 3, "tailcall retval");

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		err = bpf_map_delete_elem(map_fd, &i);
		if (CHECK_FAIL(err >= 0 || errno != ENOENT))
			goto out;

		err = bpf_prog_test_run_opts(main_fd, &topts);
		ASSERT_OK(err, "tailcall");
		ASSERT_EQ(topts.retval, 3, "tailcall retval");
	}

out:
	bpf_object__close(obj);
}

/* test_tailcall_2 checks that patching multiple programs for a single
 * tail call slot works. It also jumps through several programs and tests
 * the tail call limit counter.
 */
static void test_tailcall_2(void)
{
	int err, map_fd, prog_fd, main_fd, i;
	struct bpf_map *prog_array;
	struct bpf_program *prog;
	struct bpf_object *obj;
	char prog_name[32];
	char buff[128] = {};
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = buff,
		.data_size_in = sizeof(buff),
		.repeat = 1,
	);

	err = bpf_prog_test_load("tailcall2.bpf.o", BPF_PROG_TYPE_SCHED_CLS, &obj,
				 &prog_fd);
	if (CHECK_FAIL(err))
		return;

	prog = bpf_object__find_program_by_name(obj, "entry");
	if (CHECK_FAIL(!prog))
		goto out;

	main_fd = bpf_program__fd(prog);
	if (CHECK_FAIL(main_fd < 0))
		goto out;

	prog_array = bpf_object__find_map_by_name(obj, "jmp_table");
	if (CHECK_FAIL(!prog_array))
		goto out;

	map_fd = bpf_map__fd(prog_array);
	if (CHECK_FAIL(map_fd < 0))
		goto out;

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		snprintf(prog_name, sizeof(prog_name), "classifier_%d", i);

		prog = bpf_object__find_program_by_name(obj, prog_name);
		if (CHECK_FAIL(!prog))
			goto out;

		prog_fd = bpf_program__fd(prog);
		if (CHECK_FAIL(prog_fd < 0))
			goto out;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;
	}

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, 2, "tailcall retval");

	i = 2;
	err = bpf_map_delete_elem(map_fd, &i);
	if (CHECK_FAIL(err))
		goto out;

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, 1, "tailcall retval");

	i = 0;
	err = bpf_map_delete_elem(map_fd, &i);
	if (CHECK_FAIL(err))
		goto out;

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, 3, "tailcall retval");
out:
	bpf_object__close(obj);
}

static void test_tailcall_count(const char *which, bool test_fentry,
				bool test_fexit)
{
	struct bpf_object *obj = NULL, *fentry_obj = NULL, *fexit_obj = NULL;
	struct bpf_link *fentry_link = NULL, *fexit_link = NULL;
	int err, map_fd, prog_fd, main_fd, data_fd, i, val;
	struct bpf_map *prog_array, *data_map;
	struct bpf_program *prog;
	char buff[128] = {};
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = buff,
		.data_size_in = sizeof(buff),
		.repeat = 1,
	);

	err = bpf_prog_test_load(which, BPF_PROG_TYPE_SCHED_CLS, &obj,
			    &prog_fd);
	if (CHECK_FAIL(err))
		return;

	prog = bpf_object__find_program_by_name(obj, "entry");
	if (CHECK_FAIL(!prog))
		goto out;

	main_fd = bpf_program__fd(prog);
	if (CHECK_FAIL(main_fd < 0))
		goto out;

	prog_array = bpf_object__find_map_by_name(obj, "jmp_table");
	if (CHECK_FAIL(!prog_array))
		goto out;

	map_fd = bpf_map__fd(prog_array);
	if (CHECK_FAIL(map_fd < 0))
		goto out;

	prog = bpf_object__find_program_by_name(obj, "classifier_0");
	if (CHECK_FAIL(!prog))
		goto out;

	prog_fd = bpf_program__fd(prog);
	if (CHECK_FAIL(prog_fd < 0))
		goto out;

	i = 0;
	err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
	if (CHECK_FAIL(err))
		goto out;

	if (test_fentry) {
		fentry_obj = bpf_object__open_file("tailcall_bpf2bpf_fentry.bpf.o",
						   NULL);
		if (!ASSERT_OK_PTR(fentry_obj, "open fentry_obj file"))
			goto out;

		prog = bpf_object__find_program_by_name(fentry_obj, "fentry");
		if (!ASSERT_OK_PTR(prog, "find fentry prog"))
			goto out;

		err = bpf_program__set_attach_target(prog, prog_fd,
						     "subprog_tail");
		if (!ASSERT_OK(err, "set_attach_target subprog_tail"))
			goto out;

		err = bpf_object__load(fentry_obj);
		if (!ASSERT_OK(err, "load fentry_obj"))
			goto out;

		fentry_link = bpf_program__attach_trace(prog);
		if (!ASSERT_OK_PTR(fentry_link, "attach_trace"))
			goto out;
	}

	if (test_fexit) {
		fexit_obj = bpf_object__open_file("tailcall_bpf2bpf_fexit.bpf.o",
						  NULL);
		if (!ASSERT_OK_PTR(fexit_obj, "open fexit_obj file"))
			goto out;

		prog = bpf_object__find_program_by_name(fexit_obj, "fexit");
		if (!ASSERT_OK_PTR(prog, "find fexit prog"))
			goto out;

		err = bpf_program__set_attach_target(prog, prog_fd,
						     "subprog_tail");
		if (!ASSERT_OK(err, "set_attach_target subprog_tail"))
			goto out;

		err = bpf_object__load(fexit_obj);
		if (!ASSERT_OK(err, "load fexit_obj"))
			goto out;

		fexit_link = bpf_program__attach_trace(prog);
		if (!ASSERT_OK_PTR(fexit_link, "attach_trace"))
			goto out;
	}

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, 1, "tailcall retval");

	data_map = bpf_object__find_map_by_name(obj, "tailcall.bss");
	if (CHECK_FAIL(!data_map || !bpf_map__is_internal(data_map)))
		goto out;

	data_fd = bpf_map__fd(data_map);
	if (CHECK_FAIL(data_fd < 0))
		goto out;

	i = 0;
	err = bpf_map_lookup_elem(data_fd, &i, &val);
	ASSERT_OK(err, "tailcall count");
	ASSERT_EQ(val, 33, "tailcall count");

	if (test_fentry) {
		data_map = bpf_object__find_map_by_name(fentry_obj, ".bss");
		if (!ASSERT_FALSE(!data_map || !bpf_map__is_internal(data_map),
				  "find tailcall_bpf2bpf_fentry.bss map"))
			goto out;

		data_fd = bpf_map__fd(data_map);
		if (!ASSERT_FALSE(data_fd < 0,
				  "find tailcall_bpf2bpf_fentry.bss map fd"))
			goto out;

		i = 0;
		err = bpf_map_lookup_elem(data_fd, &i, &val);
		ASSERT_OK(err, "fentry count");
		ASSERT_EQ(val, 33, "fentry count");
	}

	if (test_fexit) {
		data_map = bpf_object__find_map_by_name(fexit_obj, ".bss");
		if (!ASSERT_FALSE(!data_map || !bpf_map__is_internal(data_map),
				  "find tailcall_bpf2bpf_fexit.bss map"))
			goto out;

		data_fd = bpf_map__fd(data_map);
		if (!ASSERT_FALSE(data_fd < 0,
				  "find tailcall_bpf2bpf_fexit.bss map fd"))
			goto out;

		i = 0;
		err = bpf_map_lookup_elem(data_fd, &i, &val);
		ASSERT_OK(err, "fexit count");
		ASSERT_EQ(val, 33, "fexit count");
	}

	i = 0;
	err = bpf_map_delete_elem(map_fd, &i);
	if (CHECK_FAIL(err))
		goto out;

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_OK(topts.retval, "tailcall retval");
out:
	bpf_link__destroy(fentry_link);
	bpf_link__destroy(fexit_link);
	bpf_object__close(fentry_obj);
	bpf_object__close(fexit_obj);
	bpf_object__close(obj);
}

/* test_tailcall_3 checks that the count value of the tail call limit
 * enforcement matches with expectations. JIT uses direct jump.
 */
static void test_tailcall_3(void)
{
	test_tailcall_count("tailcall3.bpf.o", false, false);
}

/* test_tailcall_6 checks that the count value of the tail call limit
 * enforcement matches with expectations. JIT uses indirect jump.
 */
static void test_tailcall_6(void)
{
	test_tailcall_count("tailcall6.bpf.o", false, false);
}

/* test_tailcall_4 checks that the kernel properly selects indirect jump
 * for the case where the key is not known. Latter is passed via global
 * data to select different targets we can compare return value of.
 */
static void test_tailcall_4(void)
{
	int err, map_fd, prog_fd, main_fd, data_fd, i;
	struct bpf_map *prog_array, *data_map;
	struct bpf_program *prog;
	struct bpf_object *obj;
	static const int zero = 0;
	char buff[128] = {};
	char prog_name[32];
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = buff,
		.data_size_in = sizeof(buff),
		.repeat = 1,
	);

	err = bpf_prog_test_load("tailcall4.bpf.o", BPF_PROG_TYPE_SCHED_CLS, &obj,
				 &prog_fd);
	if (CHECK_FAIL(err))
		return;

	prog = bpf_object__find_program_by_name(obj, "entry");
	if (CHECK_FAIL(!prog))
		goto out;

	main_fd = bpf_program__fd(prog);
	if (CHECK_FAIL(main_fd < 0))
		goto out;

	prog_array = bpf_object__find_map_by_name(obj, "jmp_table");
	if (CHECK_FAIL(!prog_array))
		goto out;

	map_fd = bpf_map__fd(prog_array);
	if (CHECK_FAIL(map_fd < 0))
		goto out;

	data_map = bpf_object__find_map_by_name(obj, "tailcall.bss");
	if (CHECK_FAIL(!data_map || !bpf_map__is_internal(data_map)))
		goto out;

	data_fd = bpf_map__fd(data_map);
	if (CHECK_FAIL(data_fd < 0))
		goto out;

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		snprintf(prog_name, sizeof(prog_name), "classifier_%d", i);

		prog = bpf_object__find_program_by_name(obj, prog_name);
		if (CHECK_FAIL(!prog))
			goto out;

		prog_fd = bpf_program__fd(prog);
		if (CHECK_FAIL(prog_fd < 0))
			goto out;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;
	}

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		err = bpf_map_update_elem(data_fd, &zero, &i, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;

		err = bpf_prog_test_run_opts(main_fd, &topts);
		ASSERT_OK(err, "tailcall");
		ASSERT_EQ(topts.retval, i, "tailcall retval");
	}

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		err = bpf_map_update_elem(data_fd, &zero, &i, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;

		err = bpf_map_delete_elem(map_fd, &i);
		if (CHECK_FAIL(err))
			goto out;

		err = bpf_prog_test_run_opts(main_fd, &topts);
		ASSERT_OK(err, "tailcall");
		ASSERT_EQ(topts.retval, 3, "tailcall retval");
	}
out:
	bpf_object__close(obj);
}

/* test_tailcall_5 probes similarly to test_tailcall_4 that the kernel generates
 * an indirect jump when the keys are const but different from different branches.
 */
static void test_tailcall_5(void)
{
	int err, map_fd, prog_fd, main_fd, data_fd, i, key[] = { 1111, 1234, 5678 };
	struct bpf_map *prog_array, *data_map;
	struct bpf_program *prog;
	struct bpf_object *obj;
	static const int zero = 0;
	char buff[128] = {};
	char prog_name[32];
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = buff,
		.data_size_in = sizeof(buff),
		.repeat = 1,
	);

	err = bpf_prog_test_load("tailcall5.bpf.o", BPF_PROG_TYPE_SCHED_CLS, &obj,
				 &prog_fd);
	if (CHECK_FAIL(err))
		return;

	prog = bpf_object__find_program_by_name(obj, "entry");
	if (CHECK_FAIL(!prog))
		goto out;

	main_fd = bpf_program__fd(prog);
	if (CHECK_FAIL(main_fd < 0))
		goto out;

	prog_array = bpf_object__find_map_by_name(obj, "jmp_table");
	if (CHECK_FAIL(!prog_array))
		goto out;

	map_fd = bpf_map__fd(prog_array);
	if (CHECK_FAIL(map_fd < 0))
		goto out;

	data_map = bpf_object__find_map_by_name(obj, "tailcall.bss");
	if (CHECK_FAIL(!data_map || !bpf_map__is_internal(data_map)))
		goto out;

	data_fd = bpf_map__fd(data_map);
	if (CHECK_FAIL(data_fd < 0))
		goto out;

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		snprintf(prog_name, sizeof(prog_name), "classifier_%d", i);

		prog = bpf_object__find_program_by_name(obj, prog_name);
		if (CHECK_FAIL(!prog))
			goto out;

		prog_fd = bpf_program__fd(prog);
		if (CHECK_FAIL(prog_fd < 0))
			goto out;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;
	}

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		err = bpf_map_update_elem(data_fd, &zero, &key[i], BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;

		err = bpf_prog_test_run_opts(main_fd, &topts);
		ASSERT_OK(err, "tailcall");
		ASSERT_EQ(topts.retval, i, "tailcall retval");
	}

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		err = bpf_map_update_elem(data_fd, &zero, &key[i], BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;

		err = bpf_map_delete_elem(map_fd, &i);
		if (CHECK_FAIL(err))
			goto out;

		err = bpf_prog_test_run_opts(main_fd, &topts);
		ASSERT_OK(err, "tailcall");
		ASSERT_EQ(topts.retval, 3, "tailcall retval");
	}
out:
	bpf_object__close(obj);
}

/* test_tailcall_bpf2bpf_1 purpose is to make sure that tailcalls are working
 * correctly in correlation with BPF subprograms
 */
static void test_tailcall_bpf2bpf_1(void)
{
	int err, map_fd, prog_fd, main_fd, i;
	struct bpf_map *prog_array;
	struct bpf_program *prog;
	struct bpf_object *obj;
	char prog_name[32];
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	err = bpf_prog_test_load("tailcall_bpf2bpf1.bpf.o", BPF_PROG_TYPE_SCHED_CLS,
				 &obj, &prog_fd);
	if (CHECK_FAIL(err))
		return;

	prog = bpf_object__find_program_by_name(obj, "entry");
	if (CHECK_FAIL(!prog))
		goto out;

	main_fd = bpf_program__fd(prog);
	if (CHECK_FAIL(main_fd < 0))
		goto out;

	prog_array = bpf_object__find_map_by_name(obj, "jmp_table");
	if (CHECK_FAIL(!prog_array))
		goto out;

	map_fd = bpf_map__fd(prog_array);
	if (CHECK_FAIL(map_fd < 0))
		goto out;

	/* nop -> jmp */
	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		snprintf(prog_name, sizeof(prog_name), "classifier_%d", i);

		prog = bpf_object__find_program_by_name(obj, prog_name);
		if (CHECK_FAIL(!prog))
			goto out;

		prog_fd = bpf_program__fd(prog);
		if (CHECK_FAIL(prog_fd < 0))
			goto out;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;
	}

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, 1, "tailcall retval");

	/* jmp -> nop, call subprog that will do tailcall */
	i = 1;
	err = bpf_map_delete_elem(map_fd, &i);
	if (CHECK_FAIL(err))
		goto out;

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_OK(topts.retval, "tailcall retval");

	/* make sure that subprog can access ctx and entry prog that
	 * called this subprog can properly return
	 */
	i = 0;
	err = bpf_map_delete_elem(map_fd, &i);
	if (CHECK_FAIL(err))
		goto out;

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, sizeof(pkt_v4) * 2, "tailcall retval");
out:
	bpf_object__close(obj);
}

/* test_tailcall_bpf2bpf_2 checks that the count value of the tail call limit
 * enforcement matches with expectations when tailcall is preceded with
 * bpf2bpf call.
 */
static void test_tailcall_bpf2bpf_2(void)
{
	int err, map_fd, prog_fd, main_fd, data_fd, i, val;
	struct bpf_map *prog_array, *data_map;
	struct bpf_program *prog;
	struct bpf_object *obj;
	char buff[128] = {};
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = buff,
		.data_size_in = sizeof(buff),
		.repeat = 1,
	);

	err = bpf_prog_test_load("tailcall_bpf2bpf2.bpf.o", BPF_PROG_TYPE_SCHED_CLS,
				 &obj, &prog_fd);
	if (CHECK_FAIL(err))
		return;

	prog = bpf_object__find_program_by_name(obj, "entry");
	if (CHECK_FAIL(!prog))
		goto out;

	main_fd = bpf_program__fd(prog);
	if (CHECK_FAIL(main_fd < 0))
		goto out;

	prog_array = bpf_object__find_map_by_name(obj, "jmp_table");
	if (CHECK_FAIL(!prog_array))
		goto out;

	map_fd = bpf_map__fd(prog_array);
	if (CHECK_FAIL(map_fd < 0))
		goto out;

	prog = bpf_object__find_program_by_name(obj, "classifier_0");
	if (CHECK_FAIL(!prog))
		goto out;

	prog_fd = bpf_program__fd(prog);
	if (CHECK_FAIL(prog_fd < 0))
		goto out;

	i = 0;
	err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
	if (CHECK_FAIL(err))
		goto out;

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, 1, "tailcall retval");

	data_map = bpf_object__find_map_by_name(obj, "tailcall.bss");
	if (CHECK_FAIL(!data_map || !bpf_map__is_internal(data_map)))
		goto out;

	data_fd = bpf_map__fd(data_map);
	if (CHECK_FAIL(data_fd < 0))
		goto out;

	i = 0;
	err = bpf_map_lookup_elem(data_fd, &i, &val);
	ASSERT_OK(err, "tailcall count");
	ASSERT_EQ(val, 33, "tailcall count");

	i = 0;
	err = bpf_map_delete_elem(map_fd, &i);
	if (CHECK_FAIL(err))
		goto out;

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_OK(topts.retval, "tailcall retval");
out:
	bpf_object__close(obj);
}

/* test_tailcall_bpf2bpf_3 checks that non-trivial amount of stack (up to
 * 256 bytes) can be used within bpf subprograms that have the tailcalls
 * in them
 */
static void test_tailcall_bpf2bpf_3(void)
{
	int err, map_fd, prog_fd, main_fd, i;
	struct bpf_map *prog_array;
	struct bpf_program *prog;
	struct bpf_object *obj;
	char prog_name[32];
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	err = bpf_prog_test_load("tailcall_bpf2bpf3.bpf.o", BPF_PROG_TYPE_SCHED_CLS,
				 &obj, &prog_fd);
	if (CHECK_FAIL(err))
		return;

	prog = bpf_object__find_program_by_name(obj, "entry");
	if (CHECK_FAIL(!prog))
		goto out;

	main_fd = bpf_program__fd(prog);
	if (CHECK_FAIL(main_fd < 0))
		goto out;

	prog_array = bpf_object__find_map_by_name(obj, "jmp_table");
	if (CHECK_FAIL(!prog_array))
		goto out;

	map_fd = bpf_map__fd(prog_array);
	if (CHECK_FAIL(map_fd < 0))
		goto out;

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		snprintf(prog_name, sizeof(prog_name), "classifier_%d", i);

		prog = bpf_object__find_program_by_name(obj, prog_name);
		if (CHECK_FAIL(!prog))
			goto out;

		prog_fd = bpf_program__fd(prog);
		if (CHECK_FAIL(prog_fd < 0))
			goto out;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;
	}

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, sizeof(pkt_v4) * 3, "tailcall retval");

	i = 1;
	err = bpf_map_delete_elem(map_fd, &i);
	if (CHECK_FAIL(err))
		goto out;

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, sizeof(pkt_v4), "tailcall retval");

	i = 0;
	err = bpf_map_delete_elem(map_fd, &i);
	if (CHECK_FAIL(err))
		goto out;

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, sizeof(pkt_v4) * 2, "tailcall retval");
out:
	bpf_object__close(obj);
}

#include "tailcall_bpf2bpf4.skel.h"

/* test_tailcall_bpf2bpf_4 checks that tailcall counter is correctly preserved
 * across tailcalls combined with bpf2bpf calls. for making sure that tailcall
 * counter behaves correctly, bpf program will go through following flow:
 *
 * entry -> entry_subprog -> tailcall0 -> bpf_func0 -> subprog0 ->
 * -> tailcall1 -> bpf_func1 -> subprog1 -> tailcall2 -> bpf_func2 ->
 * subprog2 [here bump global counter] --------^
 *
 * We go through first two tailcalls and start counting from the subprog2 where
 * the loop begins. At the end of the test make sure that the global counter is
 * equal to 31, because tailcall counter includes the first two tailcalls
 * whereas global counter is incremented only on loop presented on flow above.
 *
 * The noise parameter is used to insert bpf_map_update calls into the logic
 * to force verifier to patch instructions. This allows us to ensure jump
 * logic remains correct with instruction movement.
 */
static void test_tailcall_bpf2bpf_4(bool noise)
{
	int err, map_fd, prog_fd, main_fd, data_fd, i;
	struct tailcall_bpf2bpf4__bss val;
	struct bpf_map *prog_array, *data_map;
	struct bpf_program *prog;
	struct bpf_object *obj;
	char prog_name[32];
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	err = bpf_prog_test_load("tailcall_bpf2bpf4.bpf.o", BPF_PROG_TYPE_SCHED_CLS,
				 &obj, &prog_fd);
	if (CHECK_FAIL(err))
		return;

	prog = bpf_object__find_program_by_name(obj, "entry");
	if (CHECK_FAIL(!prog))
		goto out;

	main_fd = bpf_program__fd(prog);
	if (CHECK_FAIL(main_fd < 0))
		goto out;

	prog_array = bpf_object__find_map_by_name(obj, "jmp_table");
	if (CHECK_FAIL(!prog_array))
		goto out;

	map_fd = bpf_map__fd(prog_array);
	if (CHECK_FAIL(map_fd < 0))
		goto out;

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		snprintf(prog_name, sizeof(prog_name), "classifier_%d", i);

		prog = bpf_object__find_program_by_name(obj, prog_name);
		if (CHECK_FAIL(!prog))
			goto out;

		prog_fd = bpf_program__fd(prog);
		if (CHECK_FAIL(prog_fd < 0))
			goto out;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;
	}

	data_map = bpf_object__find_map_by_name(obj, "tailcall.bss");
	if (CHECK_FAIL(!data_map || !bpf_map__is_internal(data_map)))
		goto out;

	data_fd = bpf_map__fd(data_map);
	if (CHECK_FAIL(data_fd < 0))
		goto out;

	i = 0;
	val.noise = noise;
	val.count = 0;
	err = bpf_map_update_elem(data_fd, &i, &val, BPF_ANY);
	if (CHECK_FAIL(err))
		goto out;

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, sizeof(pkt_v4) * 3, "tailcall retval");

	i = 0;
	err = bpf_map_lookup_elem(data_fd, &i, &val);
	ASSERT_OK(err, "tailcall count");
	ASSERT_EQ(val.count, 31, "tailcall count");

out:
	bpf_object__close(obj);
}

#include "tailcall_bpf2bpf6.skel.h"

/* Tail call counting works even when there is data on stack which is
 * not aligned to 8 bytes.
 */
static void test_tailcall_bpf2bpf_6(void)
{
	struct tailcall_bpf2bpf6 *obj;
	int err, map_fd, prog_fd, main_fd, data_fd, i, val;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	obj = tailcall_bpf2bpf6__open_and_load();
	if (!ASSERT_OK_PTR(obj, "open and load"))
		return;

	main_fd = bpf_program__fd(obj->progs.entry);
	if (!ASSERT_GE(main_fd, 0, "entry prog fd"))
		goto out;

	map_fd = bpf_map__fd(obj->maps.jmp_table);
	if (!ASSERT_GE(map_fd, 0, "jmp_table map fd"))
		goto out;

	prog_fd = bpf_program__fd(obj->progs.classifier_0);
	if (!ASSERT_GE(prog_fd, 0, "classifier_0 prog fd"))
		goto out;

	i = 0;
	err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
	if (!ASSERT_OK(err, "jmp_table map update"))
		goto out;

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "entry prog test run");
	ASSERT_EQ(topts.retval, 0, "tailcall retval");

	data_fd = bpf_map__fd(obj->maps.bss);
	if (!ASSERT_GE(data_fd, 0, "bss map fd"))
		goto out;

	i = 0;
	err = bpf_map_lookup_elem(data_fd, &i, &val);
	ASSERT_OK(err, "bss map lookup");
	ASSERT_EQ(val, 1, "done flag is set");

out:
	tailcall_bpf2bpf6__destroy(obj);
}

/* test_tailcall_bpf2bpf_fentry checks that the count value of the tail call
 * limit enforcement matches with expectations when tailcall is preceded with
 * bpf2bpf call, and the bpf2bpf call is traced by fentry.
 */
static void test_tailcall_bpf2bpf_fentry(void)
{
	test_tailcall_count("tailcall_bpf2bpf2.bpf.o", true, false);
}

/* test_tailcall_bpf2bpf_fexit checks that the count value of the tail call
 * limit enforcement matches with expectations when tailcall is preceded with
 * bpf2bpf call, and the bpf2bpf call is traced by fexit.
 */
static void test_tailcall_bpf2bpf_fexit(void)
{
	test_tailcall_count("tailcall_bpf2bpf2.bpf.o", false, true);
}

/* test_tailcall_bpf2bpf_fentry_fexit checks that the count value of the tail
 * call limit enforcement matches with expectations when tailcall is preceded
 * with bpf2bpf call, and the bpf2bpf call is traced by both fentry and fexit.
 */
static void test_tailcall_bpf2bpf_fentry_fexit(void)
{
	test_tailcall_count("tailcall_bpf2bpf2.bpf.o", true, true);
}

/* test_tailcall_bpf2bpf_fentry_entry checks that the count value of the tail
 * call limit enforcement matches with expectations when tailcall is preceded
 * with bpf2bpf call, and the bpf2bpf caller is traced by fentry.
 */
static void test_tailcall_bpf2bpf_fentry_entry(void)
{
	struct bpf_object *tgt_obj = NULL, *fentry_obj = NULL;
	int err, map_fd, prog_fd, data_fd, i, val;
	struct bpf_map *prog_array, *data_map;
	struct bpf_link *fentry_link = NULL;
	struct bpf_program *prog;
	char buff[128] = {};

	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = buff,
		.data_size_in = sizeof(buff),
		.repeat = 1,
	);

	err = bpf_prog_test_load("tailcall_bpf2bpf2.bpf.o",
				 BPF_PROG_TYPE_SCHED_CLS,
				 &tgt_obj, &prog_fd);
	if (!ASSERT_OK(err, "load tgt_obj"))
		return;

	prog_array = bpf_object__find_map_by_name(tgt_obj, "jmp_table");
	if (!ASSERT_OK_PTR(prog_array, "find jmp_table map"))
		goto out;

	map_fd = bpf_map__fd(prog_array);
	if (!ASSERT_FALSE(map_fd < 0, "find jmp_table map fd"))
		goto out;

	prog = bpf_object__find_program_by_name(tgt_obj, "classifier_0");
	if (!ASSERT_OK_PTR(prog, "find classifier_0 prog"))
		goto out;

	prog_fd = bpf_program__fd(prog);
	if (!ASSERT_FALSE(prog_fd < 0, "find classifier_0 prog fd"))
		goto out;

	i = 0;
	err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
	if (!ASSERT_OK(err, "update jmp_table"))
		goto out;

	fentry_obj = bpf_object__open_file("tailcall_bpf2bpf_fentry.bpf.o",
					   NULL);
	if (!ASSERT_OK_PTR(fentry_obj, "open fentry_obj file"))
		goto out;

	prog = bpf_object__find_program_by_name(fentry_obj, "fentry");
	if (!ASSERT_OK_PTR(prog, "find fentry prog"))
		goto out;

	err = bpf_program__set_attach_target(prog, prog_fd, "classifier_0");
	if (!ASSERT_OK(err, "set_attach_target classifier_0"))
		goto out;

	err = bpf_object__load(fentry_obj);
	if (!ASSERT_OK(err, "load fentry_obj"))
		goto out;

	fentry_link = bpf_program__attach_trace(prog);
	if (!ASSERT_OK_PTR(fentry_link, "attach_trace"))
		goto out;

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, 1, "tailcall retval");

	data_map = bpf_object__find_map_by_name(tgt_obj, "tailcall.bss");
	if (!ASSERT_FALSE(!data_map || !bpf_map__is_internal(data_map),
			  "find tailcall.bss map"))
		goto out;

	data_fd = bpf_map__fd(data_map);
	if (!ASSERT_FALSE(data_fd < 0, "find tailcall.bss map fd"))
		goto out;

	i = 0;
	err = bpf_map_lookup_elem(data_fd, &i, &val);
	ASSERT_OK(err, "tailcall count");
	ASSERT_EQ(val, 34, "tailcall count");

	data_map = bpf_object__find_map_by_name(fentry_obj, ".bss");
	if (!ASSERT_FALSE(!data_map || !bpf_map__is_internal(data_map),
			  "find tailcall_bpf2bpf_fentry.bss map"))
		goto out;

	data_fd = bpf_map__fd(data_map);
	if (!ASSERT_FALSE(data_fd < 0,
			  "find tailcall_bpf2bpf_fentry.bss map fd"))
		goto out;

	i = 0;
	err = bpf_map_lookup_elem(data_fd, &i, &val);
	ASSERT_OK(err, "fentry count");
	ASSERT_EQ(val, 1, "fentry count");

out:
	bpf_link__destroy(fentry_link);
	bpf_object__close(fentry_obj);
	bpf_object__close(tgt_obj);
}

#define JMP_TABLE "/sys/fs/bpf/jmp_table"

static int poke_thread_exit;

static void *poke_update(void *arg)
{
	__u32 zero = 0, prog1_fd, prog2_fd, map_fd;
	struct tailcall_poke *call = arg;

	map_fd = bpf_map__fd(call->maps.jmp_table);
	prog1_fd = bpf_program__fd(call->progs.call1);
	prog2_fd = bpf_program__fd(call->progs.call2);

	while (!poke_thread_exit) {
		bpf_map_update_elem(map_fd, &zero, &prog1_fd, BPF_ANY);
		bpf_map_update_elem(map_fd, &zero, &prog2_fd, BPF_ANY);
	}

	return NULL;
}

/*
 * We are trying to hit prog array update during another program load
 * that shares the same prog array map.
 *
 * For that we share the jmp_table map between two skeleton instances
 * by pinning the jmp_table to same path. Then first skeleton instance
 * periodically updates jmp_table in 'poke update' thread while we load
 * the second skeleton instance in the main thread.
 */
static void test_tailcall_poke(void)
{
	struct tailcall_poke *call, *test;
	int err, cnt = 10;
	pthread_t thread;

	unlink(JMP_TABLE);

	call = tailcall_poke__open_and_load();
	if (!ASSERT_OK_PTR(call, "tailcall_poke__open"))
		return;

	err = bpf_map__pin(call->maps.jmp_table, JMP_TABLE);
	if (!ASSERT_OK(err, "bpf_map__pin"))
		goto out;

	err = pthread_create(&thread, NULL, poke_update, call);
	if (!ASSERT_OK(err, "new toggler"))
		goto out;

	while (cnt--) {
		test = tailcall_poke__open();
		if (!ASSERT_OK_PTR(test, "tailcall_poke__open"))
			break;

		err = bpf_map__set_pin_path(test->maps.jmp_table, JMP_TABLE);
		if (!ASSERT_OK(err, "bpf_map__pin")) {
			tailcall_poke__destroy(test);
			break;
		}

		bpf_program__set_autoload(test->progs.test, true);
		bpf_program__set_autoload(test->progs.call1, false);
		bpf_program__set_autoload(test->progs.call2, false);

		err = tailcall_poke__load(test);
		tailcall_poke__destroy(test);
		if (!ASSERT_OK(err, "tailcall_poke__load"))
			break;
	}

	poke_thread_exit = 1;
	ASSERT_OK(pthread_join(thread, NULL), "pthread_join");

out:
	bpf_map__unpin(call->maps.jmp_table, JMP_TABLE);
	tailcall_poke__destroy(call);
}

static void test_tailcall_hierarchy_count(const char *which, bool test_fentry,
					  bool test_fexit,
					  bool test_fentry_entry)
{
	int err, map_fd, prog_fd, main_data_fd, fentry_data_fd = 0, fexit_data_fd = 0, i, val;
	struct bpf_object *obj = NULL, *fentry_obj = NULL, *fexit_obj = NULL;
	struct bpf_link *fentry_link = NULL, *fexit_link = NULL;
	struct bpf_program *prog, *fentry_prog;
	struct bpf_map *prog_array, *data_map;
	int fentry_prog_fd;
	char buff[128] = {};

	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = buff,
		.data_size_in = sizeof(buff),
		.repeat = 1,
	);

	err = bpf_prog_test_load(which, BPF_PROG_TYPE_SCHED_CLS, &obj,
				 &prog_fd);
	if (!ASSERT_OK(err, "load obj"))
		return;

	prog = bpf_object__find_program_by_name(obj, "entry");
	if (!ASSERT_OK_PTR(prog, "find entry prog"))
		goto out;

	prog_fd = bpf_program__fd(prog);
	if (!ASSERT_GE(prog_fd, 0, "prog_fd"))
		goto out;

	if (test_fentry_entry) {
		fentry_obj = bpf_object__open_file("tailcall_bpf2bpf_hierarchy_fentry.bpf.o",
						   NULL);
		if (!ASSERT_OK_PTR(fentry_obj, "open fentry_obj file"))
			goto out;

		fentry_prog = bpf_object__find_program_by_name(fentry_obj,
							       "fentry");
		if (!ASSERT_OK_PTR(prog, "find fentry prog"))
			goto out;

		err = bpf_program__set_attach_target(fentry_prog, prog_fd,
						     "entry");
		if (!ASSERT_OK(err, "set_attach_target entry"))
			goto out;

		err = bpf_object__load(fentry_obj);
		if (!ASSERT_OK(err, "load fentry_obj"))
			goto out;

		fentry_link = bpf_program__attach_trace(fentry_prog);
		if (!ASSERT_OK_PTR(fentry_link, "attach_trace"))
			goto out;

		fentry_prog_fd = bpf_program__fd(fentry_prog);
		if (!ASSERT_GE(fentry_prog_fd, 0, "fentry_prog_fd"))
			goto out;

		prog_array = bpf_object__find_map_by_name(fentry_obj, "jmp_table");
		if (!ASSERT_OK_PTR(prog_array, "find jmp_table"))
			goto out;

		map_fd = bpf_map__fd(prog_array);
		if (!ASSERT_GE(map_fd, 0, "map_fd"))
			goto out;

		i = 0;
		err = bpf_map_update_elem(map_fd, &i, &fentry_prog_fd, BPF_ANY);
		if (!ASSERT_OK(err, "update jmp_table"))
			goto out;

		data_map = bpf_object__find_map_by_name(fentry_obj, ".bss");
		if (!ASSERT_FALSE(!data_map || !bpf_map__is_internal(data_map),
				  "find data_map"))
			goto out;

	} else {
		prog_array = bpf_object__find_map_by_name(obj, "jmp_table");
		if (!ASSERT_OK_PTR(prog_array, "find jmp_table"))
			goto out;

		map_fd = bpf_map__fd(prog_array);
		if (!ASSERT_GE(map_fd, 0, "map_fd"))
			goto out;

		i = 0;
		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (!ASSERT_OK(err, "update jmp_table"))
			goto out;

		data_map = bpf_object__find_map_by_name(obj, ".bss");
		if (!ASSERT_FALSE(!data_map || !bpf_map__is_internal(data_map),
				  "find data_map"))
			goto out;
	}

	if (test_fentry) {
		fentry_obj = bpf_object__open_file("tailcall_bpf2bpf_fentry.bpf.o",
						   NULL);
		if (!ASSERT_OK_PTR(fentry_obj, "open fentry_obj file"))
			goto out;

		prog = bpf_object__find_program_by_name(fentry_obj, "fentry");
		if (!ASSERT_OK_PTR(prog, "find fentry prog"))
			goto out;

		err = bpf_program__set_attach_target(prog, prog_fd,
						     "subprog_tail");
		if (!ASSERT_OK(err, "set_attach_target subprog_tail"))
			goto out;

		err = bpf_object__load(fentry_obj);
		if (!ASSERT_OK(err, "load fentry_obj"))
			goto out;

		fentry_link = bpf_program__attach_trace(prog);
		if (!ASSERT_OK_PTR(fentry_link, "attach_trace"))
			goto out;
	}

	if (test_fexit) {
		fexit_obj = bpf_object__open_file("tailcall_bpf2bpf_fexit.bpf.o",
						  NULL);
		if (!ASSERT_OK_PTR(fexit_obj, "open fexit_obj file"))
			goto out;

		prog = bpf_object__find_program_by_name(fexit_obj, "fexit");
		if (!ASSERT_OK_PTR(prog, "find fexit prog"))
			goto out;

		err = bpf_program__set_attach_target(prog, prog_fd,
						     "subprog_tail");
		if (!ASSERT_OK(err, "set_attach_target subprog_tail"))
			goto out;

		err = bpf_object__load(fexit_obj);
		if (!ASSERT_OK(err, "load fexit_obj"))
			goto out;

		fexit_link = bpf_program__attach_trace(prog);
		if (!ASSERT_OK_PTR(fexit_link, "attach_trace"))
			goto out;
	}

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, 1, "tailcall retval");

	main_data_fd = bpf_map__fd(data_map);
	if (!ASSERT_GE(main_data_fd, 0, "main_data_fd"))
		goto out;

	i = 0;
	err = bpf_map_lookup_elem(main_data_fd, &i, &val);
	ASSERT_OK(err, "tailcall count");
	ASSERT_EQ(val, 34, "tailcall count");

	if (test_fentry) {
		data_map = bpf_object__find_map_by_name(fentry_obj, ".bss");
		if (!ASSERT_FALSE(!data_map || !bpf_map__is_internal(data_map),
				  "find tailcall_bpf2bpf_fentry.bss map"))
			goto out;

		fentry_data_fd = bpf_map__fd(data_map);
		if (!ASSERT_GE(fentry_data_fd, 0,
				  "find tailcall_bpf2bpf_fentry.bss map fd"))
			goto out;

		i = 0;
		err = bpf_map_lookup_elem(fentry_data_fd, &i, &val);
		ASSERT_OK(err, "fentry count");
		ASSERT_EQ(val, 68, "fentry count");
	}

	if (test_fexit) {
		data_map = bpf_object__find_map_by_name(fexit_obj, ".bss");
		if (!ASSERT_FALSE(!data_map || !bpf_map__is_internal(data_map),
				  "find tailcall_bpf2bpf_fexit.bss map"))
			goto out;

		fexit_data_fd = bpf_map__fd(data_map);
		if (!ASSERT_GE(fexit_data_fd, 0,
				  "find tailcall_bpf2bpf_fexit.bss map fd"))
			goto out;

		i = 0;
		err = bpf_map_lookup_elem(fexit_data_fd, &i, &val);
		ASSERT_OK(err, "fexit count");
		ASSERT_EQ(val, 68, "fexit count");
	}

	i = 0;
	err = bpf_map_delete_elem(map_fd, &i);
	if (!ASSERT_OK(err, "delete_elem from jmp_table"))
		goto out;

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, 1, "tailcall retval");

	i = 0;
	err = bpf_map_lookup_elem(main_data_fd, &i, &val);
	ASSERT_OK(err, "tailcall count");
	ASSERT_EQ(val, 35, "tailcall count");

	if (test_fentry) {
		i = 0;
		err = bpf_map_lookup_elem(fentry_data_fd, &i, &val);
		ASSERT_OK(err, "fentry count");
		ASSERT_EQ(val, 70, "fentry count");
	}

	if (test_fexit) {
		i = 0;
		err = bpf_map_lookup_elem(fexit_data_fd, &i, &val);
		ASSERT_OK(err, "fexit count");
		ASSERT_EQ(val, 70, "fexit count");
	}

out:
	bpf_link__destroy(fentry_link);
	bpf_link__destroy(fexit_link);
	bpf_object__close(fentry_obj);
	bpf_object__close(fexit_obj);
	bpf_object__close(obj);
}

/* test_tailcall_bpf2bpf_hierarchy_1 checks that the count value of the tail
 * call limit enforcement matches with expectations when tailcalls are preceded
 * with two bpf2bpf calls.
 *
 *         subprog --tailcall-> entry
 * entry <
 *         subprog --tailcall-> entry
 */
static void test_tailcall_bpf2bpf_hierarchy_1(void)
{
	test_tailcall_hierarchy_count("tailcall_bpf2bpf_hierarchy1.bpf.o",
				      false, false, false);
}

/* test_tailcall_bpf2bpf_hierarchy_fentry checks that the count value of the
 * tail call limit enforcement matches with expectations when tailcalls are
 * preceded with two bpf2bpf calls, and the two subprogs are traced by fentry.
 */
static void test_tailcall_bpf2bpf_hierarchy_fentry(void)
{
	test_tailcall_hierarchy_count("tailcall_bpf2bpf_hierarchy1.bpf.o",
				      true, false, false);
}

/* test_tailcall_bpf2bpf_hierarchy_fexit checks that the count value of the tail
 * call limit enforcement matches with expectations when tailcalls are preceded
 * with two bpf2bpf calls, and the two subprogs are traced by fexit.
 */
static void test_tailcall_bpf2bpf_hierarchy_fexit(void)
{
	test_tailcall_hierarchy_count("tailcall_bpf2bpf_hierarchy1.bpf.o",
				      false, true, false);
}

/* test_tailcall_bpf2bpf_hierarchy_fentry_fexit checks that the count value of
 * the tail call limit enforcement matches with expectations when tailcalls are
 * preceded with two bpf2bpf calls, and the two subprogs are traced by both
 * fentry and fexit.
 */
static void test_tailcall_bpf2bpf_hierarchy_fentry_fexit(void)
{
	test_tailcall_hierarchy_count("tailcall_bpf2bpf_hierarchy1.bpf.o",
				      true, true, false);
}

/* test_tailcall_bpf2bpf_hierarchy_fentry_entry checks that the count value of
 * the tail call limit enforcement matches with expectations when tailcalls are
 * preceded with two bpf2bpf calls in fentry.
 */
static void test_tailcall_bpf2bpf_hierarchy_fentry_entry(void)
{
	test_tailcall_hierarchy_count("tc_dummy.bpf.o", false, false, true);
}

/* test_tailcall_bpf2bpf_hierarchy_2 checks that the count value of the tail
 * call limit enforcement matches with expectations:
 *
 *         subprog_tail0 --tailcall-> classifier_0 -> subprog_tail0
 * entry <
 *         subprog_tail1 --tailcall-> classifier_1 -> subprog_tail1
 */
static void test_tailcall_bpf2bpf_hierarchy_2(void)
{
	RUN_TESTS(tailcall_bpf2bpf_hierarchy2);
}

/* test_tailcall_bpf2bpf_hierarchy_3 checks that the count value of the tail
 * call limit enforcement matches with expectations:
 *
 *                                   subprog with jmp_table0 to classifier_0
 * entry --tailcall-> classifier_0 <
 *                                   subprog with jmp_table1 to classifier_0
 */
static void test_tailcall_bpf2bpf_hierarchy_3(void)
{
	RUN_TESTS(tailcall_bpf2bpf_hierarchy3);
}

/* test_tailcall_freplace checks that the freplace prog fails to update the
 * prog_array map, no matter whether the freplace prog attaches to its target.
 */
static void test_tailcall_freplace(void)
{
	struct tailcall_freplace *freplace_skel = NULL;
	struct bpf_link *freplace_link = NULL;
	struct bpf_program *freplace_prog;
	struct tc_bpf2bpf *tc_skel = NULL;
	int prog_fd, tc_prog_fd, map_fd;
	char buff[128] = {};
	int err, key;

	LIBBPF_OPTS(bpf_test_run_opts, topts,
		    .data_in = buff,
		    .data_size_in = sizeof(buff),
		    .repeat = 1,
	);

	freplace_skel = tailcall_freplace__open();
	if (!ASSERT_OK_PTR(freplace_skel, "tailcall_freplace__open"))
		return;

	tc_skel = tc_bpf2bpf__open_and_load();
	if (!ASSERT_OK_PTR(tc_skel, "tc_bpf2bpf__open_and_load"))
		goto out;

	tc_prog_fd = bpf_program__fd(tc_skel->progs.entry_tc);
	freplace_prog = freplace_skel->progs.entry_freplace;
	err = bpf_program__set_attach_target(freplace_prog, tc_prog_fd,
					     "subprog_tc");
	if (!ASSERT_OK(err, "set_attach_target"))
		goto out;

	err = tailcall_freplace__load(freplace_skel);
	if (!ASSERT_OK(err, "tailcall_freplace__load"))
		goto out;

	map_fd = bpf_map__fd(freplace_skel->maps.jmp_table);
	prog_fd = bpf_program__fd(freplace_prog);
	key = 0;
	err = bpf_map_update_elem(map_fd, &key, &prog_fd, BPF_ANY);
	ASSERT_ERR(err, "update jmp_table failure");

	freplace_link = bpf_program__attach_freplace(freplace_prog, tc_prog_fd,
						     "subprog_tc");
	if (!ASSERT_OK_PTR(freplace_link, "attach_freplace"))
		goto out;

	err = bpf_map_update_elem(map_fd, &key, &prog_fd, BPF_ANY);
	ASSERT_ERR(err, "update jmp_table failure");

out:
	bpf_link__destroy(freplace_link);
	tailcall_freplace__destroy(freplace_skel);
	tc_bpf2bpf__destroy(tc_skel);
}

/* test_tailcall_bpf2bpf_freplace checks the failure that fails to attach a tail
 * callee prog with freplace prog or fails to update an extended prog to
 * prog_array map.
 */
static void test_tailcall_bpf2bpf_freplace(void)
{
	struct tailcall_freplace *freplace_skel = NULL;
	struct bpf_link *freplace_link = NULL;
	struct tc_bpf2bpf *tc_skel = NULL;
	char buff[128] = {};
	int prog_fd, map_fd;
	int err, key;

	LIBBPF_OPTS(bpf_test_run_opts, topts,
		    .data_in = buff,
		    .data_size_in = sizeof(buff),
		    .repeat = 1,
	);

	tc_skel = tc_bpf2bpf__open_and_load();
	if (!ASSERT_OK_PTR(tc_skel, "tc_bpf2bpf__open_and_load"))
		goto out;

	prog_fd = bpf_program__fd(tc_skel->progs.entry_tc);
	freplace_skel = tailcall_freplace__open();
	if (!ASSERT_OK_PTR(freplace_skel, "tailcall_freplace__open"))
		goto out;

	err = bpf_program__set_attach_target(freplace_skel->progs.entry_freplace,
					     prog_fd, "subprog_tc");
	if (!ASSERT_OK(err, "set_attach_target"))
		goto out;

	err = tailcall_freplace__load(freplace_skel);
	if (!ASSERT_OK(err, "tailcall_freplace__load"))
		goto out;

	/* OK to attach then detach freplace prog. */

	freplace_link = bpf_program__attach_freplace(freplace_skel->progs.entry_freplace,
						     prog_fd, "subprog_tc");
	if (!ASSERT_OK_PTR(freplace_link, "attach_freplace"))
		goto out;

	err = bpf_link__destroy(freplace_link);
	freplace_link = NULL;
	if (!ASSERT_OK(err, "destroy link"))
		goto out;

	/* OK to update prog_array map then delete element from the map. */

	key = 0;
	map_fd = bpf_map__fd(freplace_skel->maps.jmp_table);
	err = bpf_map_update_elem(map_fd, &key, &prog_fd, BPF_ANY);
	if (!ASSERT_OK(err, "update jmp_table"))
		goto out;

	err = bpf_map_delete_elem(map_fd, &key);
	if (!ASSERT_OK(err, "delete_elem from jmp_table"))
		goto out;

	/* Fail to attach a tail callee prog with freplace prog. */

	err = bpf_map_update_elem(map_fd, &key, &prog_fd, BPF_ANY);
	if (!ASSERT_OK(err, "update jmp_table"))
		goto out;

	freplace_link = bpf_program__attach_freplace(freplace_skel->progs.entry_freplace,
						     prog_fd, "subprog_tc");
	if (!ASSERT_ERR_PTR(freplace_link, "attach_freplace failure"))
		goto out;

	err = bpf_map_delete_elem(map_fd, &key);
	if (!ASSERT_OK(err, "delete_elem from jmp_table"))
		goto out;

	/* Fail to update an extended prog to prog_array map. */

	freplace_link = bpf_program__attach_freplace(freplace_skel->progs.entry_freplace,
						     prog_fd, "subprog_tc");
	if (!ASSERT_OK_PTR(freplace_link, "attach_freplace"))
		goto out;

	err = bpf_map_update_elem(map_fd, &key, &prog_fd, BPF_ANY);
	if (!ASSERT_ERR(err, "update jmp_table failure"))
		goto out;

out:
	bpf_link__destroy(freplace_link);
	tailcall_freplace__destroy(freplace_skel);
	tc_bpf2bpf__destroy(tc_skel);
}

static void test_tailcall_failure()
{
	RUN_TESTS(tailcall_fail);
}

void test_tailcalls(void)
{
	if (test__start_subtest("tailcall_1"))
		test_tailcall_1();
	if (test__start_subtest("tailcall_2"))
		test_tailcall_2();
	if (test__start_subtest("tailcall_3"))
		test_tailcall_3();
	if (test__start_subtest("tailcall_4"))
		test_tailcall_4();
	if (test__start_subtest("tailcall_5"))
		test_tailcall_5();
	if (test__start_subtest("tailcall_6"))
		test_tailcall_6();
	if (test__start_subtest("tailcall_bpf2bpf_1"))
		test_tailcall_bpf2bpf_1();
	if (test__start_subtest("tailcall_bpf2bpf_2"))
		test_tailcall_bpf2bpf_2();
	if (test__start_subtest("tailcall_bpf2bpf_3"))
		test_tailcall_bpf2bpf_3();
	if (test__start_subtest("tailcall_bpf2bpf_4"))
		test_tailcall_bpf2bpf_4(false);
	if (test__start_subtest("tailcall_bpf2bpf_5"))
		test_tailcall_bpf2bpf_4(true);
	if (test__start_subtest("tailcall_bpf2bpf_6"))
		test_tailcall_bpf2bpf_6();
	if (test__start_subtest("tailcall_bpf2bpf_fentry"))
		test_tailcall_bpf2bpf_fentry();
	if (test__start_subtest("tailcall_bpf2bpf_fexit"))
		test_tailcall_bpf2bpf_fexit();
	if (test__start_subtest("tailcall_bpf2bpf_fentry_fexit"))
		test_tailcall_bpf2bpf_fentry_fexit();
	if (test__start_subtest("tailcall_bpf2bpf_fentry_entry"))
		test_tailcall_bpf2bpf_fentry_entry();
	if (test__start_subtest("tailcall_poke"))
		test_tailcall_poke();
	if (test__start_subtest("tailcall_bpf2bpf_hierarchy_1"))
		test_tailcall_bpf2bpf_hierarchy_1();
	if (test__start_subtest("tailcall_bpf2bpf_hierarchy_fentry"))
		test_tailcall_bpf2bpf_hierarchy_fentry();
	if (test__start_subtest("tailcall_bpf2bpf_hierarchy_fexit"))
		test_tailcall_bpf2bpf_hierarchy_fexit();
	if (test__start_subtest("tailcall_bpf2bpf_hierarchy_fentry_fexit"))
		test_tailcall_bpf2bpf_hierarchy_fentry_fexit();
	if (test__start_subtest("tailcall_bpf2bpf_hierarchy_fentry_entry"))
		test_tailcall_bpf2bpf_hierarchy_fentry_entry();
	test_tailcall_bpf2bpf_hierarchy_2();
	test_tailcall_bpf2bpf_hierarchy_3();
	if (test__start_subtest("tailcall_freplace"))
		test_tailcall_freplace();
	if (test__start_subtest("tailcall_bpf2bpf_freplace"))
		test_tailcall_bpf2bpf_freplace();
	if (test__start_subtest("tailcall_failure"))
		test_tailcall_failure();
}
