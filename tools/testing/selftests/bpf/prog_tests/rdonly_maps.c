// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

struct bss {
	unsigned did_run;
	unsigned iters;
	unsigned sum;
};

struct rdonly_map_subtest {
	const char *subtest_name;
	const char *prog_name;
	unsigned exp_iters;
	unsigned exp_sum;
};

void test_rdonly_maps(void)
{
	const char *file = "test_rdonly_maps.o";
	struct rdonly_map_subtest subtests[] = {
		{ "skip loop", "skip_loop", 0, 0 },
		{ "part loop", "part_loop", 3, 2 + 3 + 4 },
		{ "full loop", "full_loop", 4, 2 + 3 + 4 + 5 },
	};
	int i, err, zero = 0, duration = 0;
	struct bpf_link *link = NULL;
	struct bpf_program *prog;
	struct bpf_map *bss_map;
	struct bpf_object *obj;
	struct bss bss;

	obj = bpf_object__open_file(file, NULL);
	if (CHECK(IS_ERR(obj), "obj_open", "err %ld\n", PTR_ERR(obj)))
		return;

	err = bpf_object__load(obj);
	if (CHECK(err, "obj_load", "err %d errno %d\n", err, errno))
		goto cleanup;

	bss_map = bpf_object__find_map_by_name(obj, "test_rdo.bss");
	if (CHECK(!bss_map, "find_bss_map", "failed\n"))
		goto cleanup;

	for (i = 0; i < ARRAY_SIZE(subtests); i++) {
		const struct rdonly_map_subtest *t = &subtests[i];

		if (!test__start_subtest(t->subtest_name))
			continue;

		prog = bpf_object__find_program_by_name(obj, t->prog_name);
		if (CHECK(!prog, "find_prog", "prog '%s' not found\n",
			  t->prog_name))
			goto cleanup;

		memset(&bss, 0, sizeof(bss));
		err = bpf_map_update_elem(bpf_map__fd(bss_map), &zero, &bss, 0);
		if (CHECK(err, "set_bss", "failed to set bss data: %d\n", err))
			goto cleanup;

		link = bpf_program__attach_raw_tracepoint(prog, "sys_enter");
		if (CHECK(IS_ERR(link), "attach_prog", "prog '%s', err %ld\n",
			  t->prog_name, PTR_ERR(link))) {
			link = NULL;
			goto cleanup;
		}

		/* trigger probe */
		usleep(1);

		bpf_link__destroy(link);
		link = NULL;

		err = bpf_map_lookup_elem(bpf_map__fd(bss_map), &zero, &bss);
		if (CHECK(err, "get_bss", "failed to get bss data: %d\n", err))
			goto cleanup;
		if (CHECK(bss.did_run == 0, "check_run",
			  "prog '%s' didn't run?\n", t->prog_name))
			goto cleanup;
		if (CHECK(bss.iters != t->exp_iters, "check_iters",
			  "prog '%s' iters: %d, expected: %d\n",
			  t->prog_name, bss.iters, t->exp_iters))
			goto cleanup;
		if (CHECK(bss.sum != t->exp_sum, "check_sum",
			  "prog '%s' sum: %d, expected: %d\n",
			  t->prog_name, bss.sum, t->exp_sum))
			goto cleanup;
	}

cleanup:
	bpf_link__destroy(link);
	bpf_object__close(obj);
}
