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

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
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

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
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

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		err = bpf_map_lookup_elem(map_fd, &tests[i].key, &val);
		CHECK(err || memcmp(&val, &tests[i].val, sizeof(val)),
		      tests[i].name, "err %d result { %u, %u, %llu } expected { %u, %u, %llu }\n",
		      err, val.a, val.b, val.c, tests[i].val.a, tests[i].val.b, tests[i].val.c);
	}
}

static void test_global_data_rdonly(struct bpf_object *obj, __u32 duration)
{
	int err = -ENOMEM, map_fd, zero = 0;
	struct bpf_map *map, *map2;
	__u8 *buff;

	map = bpf_object__find_map_by_name(obj, "test_glo.rodata");
	if (!ASSERT_OK_PTR(map, "map"))
		return;
	if (!ASSERT_TRUE(bpf_map__is_internal(map), "is_internal"))
		return;

	/* ensure we can lookup internal maps by their ELF names */
	map2 = bpf_object__find_map_by_name(obj, ".rodata");
	if (!ASSERT_EQ(map, map2, "same_maps"))
		return;

	map_fd = bpf_map__fd(map);
	if (CHECK_FAIL(map_fd < 0))
		return;

	buff = malloc(bpf_map__value_size(map));
	if (buff)
		err = bpf_map_update_elem(map_fd, &zero, buff, 0);
	free(buff);
	CHECK(!err || errno != EPERM, "test .rodata read-only map",
	      "err %d errno %d\n", err, errno);
}

void test_global_data(void)
{
	const char *file = "./test_global_data.o";
	struct bpf_object *obj;
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_SCHED_CLS, &obj, &prog_fd);
	if (!ASSERT_OK(err, "load program"))
		return;

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "pass global data run err");
	ASSERT_OK(topts.retval, "pass global data run retval");

	test_global_data_number(obj, topts.duration);
	test_global_data_string(obj, topts.duration);
	test_global_data_struct(obj, topts.duration);
	test_global_data_rdonly(obj, topts.duration);

	bpf_object__close(obj);
}
