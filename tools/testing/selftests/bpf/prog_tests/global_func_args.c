// SPDX-License-Identifier: GPL-2.0
#include "test_progs.h"
#include "network_helpers.h"

static __u32 duration;

static void test_global_func_args0(struct bpf_object *obj)
{
	int err, i, map_fd, actual_value;
	const char *map_name = "values";

	map_fd = bpf_find_map(__func__, obj, map_name);
	if (CHECK(map_fd < 0, "bpf_find_map", "cannot find BPF map %s: %s\n",
		map_name, strerror(errno)))
		return;

	struct {
		const char *descr;
		int expected_value;
	} tests[] = {
		{"passing NULL pointer", 0},
		{"returning value", 1},
		{"reading local variable", 100 },
		{"writing local variable", 101 },
		{"reading global variable", 42 },
		{"writing global variable", 43 },
		{"writing to pointer-to-pointer", 1 },
	};

	for (i = 0; i < ARRAY_SIZE(tests); ++i) {
		const int expected_value = tests[i].expected_value;

		err = bpf_map_lookup_elem(map_fd, &i, &actual_value);

		CHECK(err || actual_value != expected_value, tests[i].descr,
			 "err %d result %d expected %d\n", err, actual_value, expected_value);
	}
}

void test_global_func_args(void)
{
	const char *file = "./test_global_func_args.o";
	__u32 retval;
	struct bpf_object *obj;
	int err, prog_fd;

	err = bpf_prog_load(file, BPF_PROG_TYPE_CGROUP_SKB, &obj, &prog_fd);
	if (CHECK(err, "load program", "error %d loading %s\n", err, file))
		return;

	err = bpf_prog_test_run(prog_fd, 1, &pkt_v4, sizeof(pkt_v4),
				NULL, NULL, &retval, &duration);
	CHECK(err || retval, "pass global func args run",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, retval, duration);

	test_global_func_args0(obj);

	bpf_object__close(obj);
}
