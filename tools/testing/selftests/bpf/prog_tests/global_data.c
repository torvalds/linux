// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

static void test_global_data_number(struct bpf_object *obj, __u32 duration)
{
	int i, err, map_fd;
	__u64 num;

	map_fd = bpf_find_map(__func__, obj, "result_number");
	if (CHECK_FAIL(map_fd < 0))
		return;

	struct {
		char *name;
		uint32_t key;
		__u64 num;
	} tests[] = {
		{ "relocate .bss reference",     0, 0 },
		{ "relocate .data reference",    1, 42 },
		{ "relocate .rodata reference",  2, 24 },
		{ "relocate .bss reference",     3, 0 },
		{ "relocate .data reference",    4, 0xffeeff },
		{ "relocate .rodata reference",  5, 0xabab },
		{ "relocate .bss reference",     6, 1234 },
		{ "relocate .bss reference",     7, 0 },
		{ "relocate .rodata reference",  8, 0xab },
		{ "relocate .rodata reference",  9, 0x1111111111111111 },
		{ "relocate .rodata reference", 10, ~0 },
	};

	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		err = bpf_map_lookup_elem(map_fd, &tests[i].key, &num);
		CHECK(err || num != tests[i].num, tests[i].name,
		      "err %d result %llx expected %llx\n",
		      err, num, tests[i].num);
	}
}

static void test_global_data_string(struct bpf_object *obj, __u32 duration)
{
	int i, err, map_fd;
	char str[32];

	map_fd = bpf_find_map(__func__, obj, "result_string");
	if (CHECK_FAIL(map_fd < 0))
		return;

	struct {
		char *name;
		uint32_t key;
		char str[32];
	} tests[] = {
		{ "relocate .rodata reference", 0, "abcdefghijklmnopqrstuvwxyz" },
		{ "relocate .data reference",   1, "abcdefghijklmnopqrstuvwxyz" },
		{ "relocate .bss reference",    2, "" },
		{ "relocate .data reference",   3, "abcdexghijklmnopqrstuvwxyz" },
		{ "relocate .bss reference",    4, "\0\0hello" },
	};

	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		err = bpf_map_lookup_elem(map_fd, &tests[i].key, str);
		CHECK(err || memcmp(str, tests[i].str, sizeof(str)),
		      tests[i].name, "err %d result \'%s\' expected \'%s\'\n",
		      err, str, tests[i].str);
	}
}

struct foo {
	__u8  a;
	__u32 b;
	__u64 c;
};

static void test_global_data_struct(struct bpf_object *obj, __u32 duration)
{
	int i, err, map_fd;
	struct foo val;

	map_fd = bpf_find_map(__func__, obj, "result_struct");
	if (CHECK_FAIL(map_fd < 0))
		return;

	struct {
		char *name;
		uint32_t key;
		struct foo val;
	} tests[] = {
		{ "relocate .rodata reference", 0, { 42, 0xfefeefef, 0x1111111111111111ULL, } },
		{ "relocate .bss reference",    1, { } },
		{ "relocate .rodata reference", 2, { } },
		{ "relocate .data reference",   3, { 41, 0xeeeeefef, 0x2111111111111111ULL, } },
	};

	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		err = bpf_map_lookup_elem(map_fd, &tests[i].key, &val);
		CHECK(err || memcmp(&val, &tests[i].val, sizeof(val)),
		      tests[i].name, "err %d result { %u, %u, %llu } expected { %u, %u, %llu }\n",
		      err, val.a, val.b, val.c, tests[i].val.a, tests[i].val.b, tests[i].val.c);
	}
}

static void test_global_data_rdonly(struct bpf_object *obj, __u32 duration)
{
	int err = -ENOMEM, map_fd, zero = 0;
	struct bpf_map *map;
	__u8 *buff;

	map = bpf_object__find_map_by_name(obj, "test_glo.rodata");
	if (CHECK_FAIL(!map || !bpf_map__is_internal(map)))
		return;

	map_fd = bpf_map__fd(map);
	if (CHECK_FAIL(map_fd < 0))
		return;

	buff = malloc(bpf_map__def(map)->value_size);
	if (buff)
		err = bpf_map_update_elem(map_fd, &zero, buff, 0);
	free(buff);
	CHECK(!err || errno != EPERM, "test .rodata read-only map",
	      "err %d errno %d\n", err, errno);
}

void test_global_data(void)
{
	const char *file = "./test_global_data.o";
	__u32 duration = 0, retval;
	struct bpf_object *obj;
	int err, prog_fd;

	err = bpf_prog_load(file, BPF_PROG_TYPE_SCHED_CLS, &obj, &prog_fd);
	if (CHECK(err, "load program", "error %d loading %s\n", err, file))
		return;

	err = bpf_prog_test_run(prog_fd, 1, &pkt_v4, sizeof(pkt_v4),
				NULL, NULL, &retval, &duration);
	CHECK(err || retval, "pass global data run",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, retval, duration);

	test_global_data_number(obj, duration);
	test_global_data_string(obj, duration);
	test_global_data_struct(obj, duration);
	test_global_data_rdonly(obj, duration);

	bpf_object__close(obj);
}
