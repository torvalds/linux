// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

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

	err = bpf_prog_test_load("tailcall1.o", BPF_PROG_TYPE_SCHED_CLS, &obj,
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

	err = bpf_prog_test_load("tailcall2.o", BPF_PROG_TYPE_SCHED_CLS, &obj,
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

static void test_tailcall_count(const char *which)
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

	err = bpf_prog_test_run_opts(main_fd, &topts);
	ASSERT_OK(err, "tailcall");
	ASSERT_EQ(topts.retval, 1, "tailcall retval");

	data_map = bpf_object__find_map_by_name(obj, "tailcall.bss");
	if (CHECK_FAIL(!data_map || !bpf_map__is_internal(data_map)))
		return;

	data_fd = bpf_map__fd(data_map);
	if (CHECK_FAIL(map_fd < 0))
		return;

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

/* test_tailcall_3 checks that the count value of the tail call limit
 * enforcement matches with expectations. JIT uses direct jump.
 */
static void test_tailcall_3(void)
{
	test_tailcall_count("tailcall3.o");
}

/* test_tailcall_6 checks that the count value of the tail call limit
 * enforcement matches with expectations. JIT uses indirect jump.
 */
static void test_tailcall_6(void)
{
	test_tailcall_count("tailcall6.o");
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

	err = bpf_prog_test_load("tailcall4.o", BPF_PROG_TYPE_SCHED_CLS, &obj,
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
		return;

	data_fd = bpf_map__fd(data_map);
	if (CHECK_FAIL(map_fd < 0))
		return;

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

	err = bpf_prog_test_load("tailcall5.o", BPF_PROG_TYPE_SCHED_CLS, &obj,
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
		return;

	data_fd = bpf_map__fd(data_map);
	if (CHECK_FAIL(map_fd < 0))
		return;

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

	err = bpf_prog_test_load("tailcall_bpf2bpf1.o", BPF_PROG_TYPE_SCHED_CLS,
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

	err = bpf_prog_test_load("tailcall_bpf2bpf2.o", BPF_PROG_TYPE_SCHED_CLS,
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
		return;

	data_fd = bpf_map__fd(data_map);
	if (CHECK_FAIL(map_fd < 0))
		return;

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

	err = bpf_prog_test_load("tailcall_bpf2bpf3.o", BPF_PROG_TYPE_SCHED_CLS,
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

	err = bpf_prog_test_load("tailcall_bpf2bpf4.o", BPF_PROG_TYPE_SCHED_CLS,
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
		return;

	data_fd = bpf_map__fd(data_map);
	if (CHECK_FAIL(map_fd < 0))
		return;

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
}
