// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

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
	__u32 retval, duration;
	char prog_name[32];
	char buff[128] = {};

	err = bpf_prog_load("tailcall1.o", BPF_PROG_TYPE_SCHED_CLS, &obj,
			    &prog_fd);
	if (CHECK_FAIL(err))
		return;

	prog = bpf_object__find_program_by_title(obj, "classifier");
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

	for (i = 0; i < bpf_map__def(prog_array)->max_entries; i++) {
		snprintf(prog_name, sizeof(prog_name), "classifier/%i", i);

		prog = bpf_object__find_program_by_title(obj, prog_name);
		if (CHECK_FAIL(!prog))
			goto out;

		prog_fd = bpf_program__fd(prog);
		if (CHECK_FAIL(prog_fd < 0))
			goto out;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;
	}

	for (i = 0; i < bpf_map__def(prog_array)->max_entries; i++) {
		err = bpf_prog_test_run(main_fd, 1, buff, sizeof(buff), 0,
					&duration, &retval, NULL);
		CHECK(err || retval != i, "tailcall",
		      "err %d errno %d retval %d\n", err, errno, retval);

		err = bpf_map_delete_elem(map_fd, &i);
		if (CHECK_FAIL(err))
			goto out;
	}

	err = bpf_prog_test_run(main_fd, 1, buff, sizeof(buff), 0,
				&duration, &retval, NULL);
	CHECK(err || retval != 3, "tailcall", "err %d errno %d retval %d\n",
	      err, errno, retval);

	for (i = 0; i < bpf_map__def(prog_array)->max_entries; i++) {
		snprintf(prog_name, sizeof(prog_name), "classifier/%i", i);

		prog = bpf_object__find_program_by_title(obj, prog_name);
		if (CHECK_FAIL(!prog))
			goto out;

		prog_fd = bpf_program__fd(prog);
		if (CHECK_FAIL(prog_fd < 0))
			goto out;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;
	}

	err = bpf_prog_test_run(main_fd, 1, buff, sizeof(buff), 0,
				&duration, &retval, NULL);
	CHECK(err || retval != 0, "tailcall", "err %d errno %d retval %d\n",
	      err, errno, retval);

	for (i = 0; i < bpf_map__def(prog_array)->max_entries; i++) {
		j = bpf_map__def(prog_array)->max_entries - 1 - i;
		snprintf(prog_name, sizeof(prog_name), "classifier/%i", j);

		prog = bpf_object__find_program_by_title(obj, prog_name);
		if (CHECK_FAIL(!prog))
			goto out;

		prog_fd = bpf_program__fd(prog);
		if (CHECK_FAIL(prog_fd < 0))
			goto out;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;
	}

	for (i = 0; i < bpf_map__def(prog_array)->max_entries; i++) {
		j = bpf_map__def(prog_array)->max_entries - 1 - i;

		err = bpf_prog_test_run(main_fd, 1, buff, sizeof(buff), 0,
					&duration, &retval, NULL);
		CHECK(err || retval != j, "tailcall",
		      "err %d errno %d retval %d\n", err, errno, retval);

		err = bpf_map_delete_elem(map_fd, &i);
		if (CHECK_FAIL(err))
			goto out;
	}

	err = bpf_prog_test_run(main_fd, 1, buff, sizeof(buff), 0,
				&duration, &retval, NULL);
	CHECK(err || retval != 3, "tailcall", "err %d errno %d retval %d\n",
	      err, errno, retval);

	for (i = 0; i < bpf_map__def(prog_array)->max_entries; i++) {
		err = bpf_map_delete_elem(map_fd, &i);
		if (CHECK_FAIL(err >= 0 || errno != ENOENT))
			goto out;

		err = bpf_prog_test_run(main_fd, 1, buff, sizeof(buff), 0,
					&duration, &retval, NULL);
		CHECK(err || retval != 3, "tailcall",
		      "err %d errno %d retval %d\n", err, errno, retval);
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
	__u32 retval, duration;
	char prog_name[32];
	char buff[128] = {};

	err = bpf_prog_load("tailcall2.o", BPF_PROG_TYPE_SCHED_CLS, &obj,
			    &prog_fd);
	if (CHECK_FAIL(err))
		return;

	prog = bpf_object__find_program_by_title(obj, "classifier");
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

	for (i = 0; i < bpf_map__def(prog_array)->max_entries; i++) {
		snprintf(prog_name, sizeof(prog_name), "classifier/%i", i);

		prog = bpf_object__find_program_by_title(obj, prog_name);
		if (CHECK_FAIL(!prog))
			goto out;

		prog_fd = bpf_program__fd(prog);
		if (CHECK_FAIL(prog_fd < 0))
			goto out;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;
	}

	err = bpf_prog_test_run(main_fd, 1, buff, sizeof(buff), 0,
				&duration, &retval, NULL);
	CHECK(err || retval != 2, "tailcall", "err %d errno %d retval %d\n",
	      err, errno, retval);

	i = 2;
	err = bpf_map_delete_elem(map_fd, &i);
	if (CHECK_FAIL(err))
		goto out;

	err = bpf_prog_test_run(main_fd, 1, buff, sizeof(buff), 0,
				&duration, &retval, NULL);
	CHECK(err || retval != 1, "tailcall", "err %d errno %d retval %d\n",
	      err, errno, retval);

	i = 0;
	err = bpf_map_delete_elem(map_fd, &i);
	if (CHECK_FAIL(err))
		goto out;

	err = bpf_prog_test_run(main_fd, 1, buff, sizeof(buff), 0,
				&duration, &retval, NULL);
	CHECK(err || retval != 3, "tailcall", "err %d errno %d retval %d\n",
	      err, errno, retval);
out:
	bpf_object__close(obj);
}

/* test_tailcall_3 checks that the count value of the tail call limit
 * enforcement matches with expectations.
 */
static void test_tailcall_3(void)
{
	int err, map_fd, prog_fd, main_fd, data_fd, i, val;
	struct bpf_map *prog_array, *data_map;
	struct bpf_program *prog;
	struct bpf_object *obj;
	__u32 retval, duration;
	char buff[128] = {};

	err = bpf_prog_load("tailcall3.o", BPF_PROG_TYPE_SCHED_CLS, &obj,
			    &prog_fd);
	if (CHECK_FAIL(err))
		return;

	prog = bpf_object__find_program_by_title(obj, "classifier");
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

	prog = bpf_object__find_program_by_title(obj, "classifier/0");
	if (CHECK_FAIL(!prog))
		goto out;

	prog_fd = bpf_program__fd(prog);
	if (CHECK_FAIL(prog_fd < 0))
		goto out;

	i = 0;
	err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
	if (CHECK_FAIL(err))
		goto out;

	err = bpf_prog_test_run(main_fd, 1, buff, sizeof(buff), 0,
				&duration, &retval, NULL);
	CHECK(err || retval != 1, "tailcall", "err %d errno %d retval %d\n",
	      err, errno, retval);

	data_map = bpf_object__find_map_by_name(obj, "tailcall.bss");
	if (CHECK_FAIL(!data_map || !bpf_map__is_internal(data_map)))
		return;

	data_fd = bpf_map__fd(data_map);
	if (CHECK_FAIL(map_fd < 0))
		return;

	i = 0;
	err = bpf_map_lookup_elem(data_fd, &i, &val);
	CHECK(err || val != 33, "tailcall count", "err %d errno %d count %d\n",
	      err, errno, val);

	i = 0;
	err = bpf_map_delete_elem(map_fd, &i);
	if (CHECK_FAIL(err))
		goto out;

	err = bpf_prog_test_run(main_fd, 1, buff, sizeof(buff), 0,
				&duration, &retval, NULL);
	CHECK(err || retval != 0, "tailcall", "err %d errno %d retval %d\n",
	      err, errno, retval);
out:
	bpf_object__close(obj);
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
	__u32 retval, duration;
	static const int zero = 0;
	char buff[128] = {};
	char prog_name[32];

	err = bpf_prog_load("tailcall4.o", BPF_PROG_TYPE_SCHED_CLS, &obj,
			    &prog_fd);
	if (CHECK_FAIL(err))
		return;

	prog = bpf_object__find_program_by_title(obj, "classifier");
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

	for (i = 0; i < bpf_map__def(prog_array)->max_entries; i++) {
		snprintf(prog_name, sizeof(prog_name), "classifier/%i", i);

		prog = bpf_object__find_program_by_title(obj, prog_name);
		if (CHECK_FAIL(!prog))
			goto out;

		prog_fd = bpf_program__fd(prog);
		if (CHECK_FAIL(prog_fd < 0))
			goto out;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;
	}

	for (i = 0; i < bpf_map__def(prog_array)->max_entries; i++) {
		err = bpf_map_update_elem(data_fd, &zero, &i, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;

		err = bpf_prog_test_run(main_fd, 1, buff, sizeof(buff), 0,
					&duration, &retval, NULL);
		CHECK(err || retval != i, "tailcall",
		      "err %d errno %d retval %d\n", err, errno, retval);
	}

	for (i = 0; i < bpf_map__def(prog_array)->max_entries; i++) {
		err = bpf_map_update_elem(data_fd, &zero, &i, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;

		err = bpf_map_delete_elem(map_fd, &i);
		if (CHECK_FAIL(err))
			goto out;

		err = bpf_prog_test_run(main_fd, 1, buff, sizeof(buff), 0,
					&duration, &retval, NULL);
		CHECK(err || retval != 3, "tailcall",
		      "err %d errno %d retval %d\n", err, errno, retval);
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
	__u32 retval, duration;
	static const int zero = 0;
	char buff[128] = {};
	char prog_name[32];

	err = bpf_prog_load("tailcall5.o", BPF_PROG_TYPE_SCHED_CLS, &obj,
			    &prog_fd);
	if (CHECK_FAIL(err))
		return;

	prog = bpf_object__find_program_by_title(obj, "classifier");
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

	for (i = 0; i < bpf_map__def(prog_array)->max_entries; i++) {
		snprintf(prog_name, sizeof(prog_name), "classifier/%i", i);

		prog = bpf_object__find_program_by_title(obj, prog_name);
		if (CHECK_FAIL(!prog))
			goto out;

		prog_fd = bpf_program__fd(prog);
		if (CHECK_FAIL(prog_fd < 0))
			goto out;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;
	}

	for (i = 0; i < bpf_map__def(prog_array)->max_entries; i++) {
		err = bpf_map_update_elem(data_fd, &zero, &key[i], BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;

		err = bpf_prog_test_run(main_fd, 1, buff, sizeof(buff), 0,
					&duration, &retval, NULL);
		CHECK(err || retval != i, "tailcall",
		      "err %d errno %d retval %d\n", err, errno, retval);
	}

	for (i = 0; i < bpf_map__def(prog_array)->max_entries; i++) {
		err = bpf_map_update_elem(data_fd, &zero, &key[i], BPF_ANY);
		if (CHECK_FAIL(err))
			goto out;

		err = bpf_map_delete_elem(map_fd, &i);
		if (CHECK_FAIL(err))
			goto out;

		err = bpf_prog_test_run(main_fd, 1, buff, sizeof(buff), 0,
					&duration, &retval, NULL);
		CHECK(err || retval != 3, "tailcall",
		      "err %d errno %d retval %d\n", err, errno, retval);
	}
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
}
