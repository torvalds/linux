// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "bpf/libbpf_internal.h"
#include "test_global_percpu_data.skel.h"
#include "test_global_percpu_data.lskel.h"

void test_global_data_init(void)
{
	const char *file = "./test_global_data.bpf.o";
	int err = -ENOMEM, map_fd, zero = 0;
	__u8 *buff = NULL, *newval = NULL;
	struct bpf_object *obj;
	struct bpf_map *map;
        __u32 duration = 0;
	size_t sz;

	obj = bpf_object__open_file(file, NULL);
	err = libbpf_get_error(obj);
	if (CHECK_FAIL(err))
		return;

	map = bpf_object__find_map_by_name(obj, ".rodata");
	if (CHECK_FAIL(!map || !bpf_map__is_internal(map)))
		goto out;

	sz = bpf_map__value_size(map);
	newval = malloc(sz);
	if (CHECK_FAIL(!newval))
		goto out;

	memset(newval, 0, sz);
	/* wrong size, should fail */
	err = bpf_map__set_initial_value(map, newval, sz - 1);
	if (CHECK(!err, "reject set initial value wrong size", "err %d\n", err))
		goto out;

	err = bpf_map__set_initial_value(map, newval, sz);
	if (CHECK(err, "set initial value", "err %d\n", err))
		goto out;

	err = bpf_object__load(obj);
	if (CHECK_FAIL(err))
		goto out;

	map_fd = bpf_map__fd(map);
	if (CHECK_FAIL(map_fd < 0))
		goto out;

	buff = malloc(sz);
	if (buff)
		err = bpf_map_lookup_elem(map_fd, &zero, buff);
	if (CHECK(!buff || err || memcmp(buff, newval, sz),
		  "compare .rodata map data override",
		  "err %d errno %d\n", err, errno))
		goto out;

	memset(newval, 1, sz);
	/* object loaded - should fail */
	err = bpf_map__set_initial_value(map, newval, sz);
	CHECK(!err, "reject set initial value after load", "err %d\n", err);
out:
	free(buff);
	free(newval);
	bpf_object__close(obj);
}

static void test_percpu_data_on_cpus(struct bpf_map *map, int map_fd, int prog_fd, int *runp)
{
	struct test_global_percpu_data__percpu *data = NULL;
	int i, err, key = 0, num_online, run = 0;
	__u64 args[2] = {0x1234ULL, 0x5678ULL};
	size_t data_sz;
	bool *online;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		    .ctx_in = args,
		    .ctx_size_in = sizeof(args),
		    .flags = BPF_F_TEST_RUN_ON_CPU,
	);

	err = parse_cpu_mask_file("/sys/devices/system/cpu/online", &online, &num_online);
	if (!ASSERT_OK(err, "parse_cpu_mask_file"))
		return;

	data_sz = map ? bpf_map__value_size(map) : sizeof(*data);
	data = calloc(1, data_sz);
	if (!ASSERT_OK_PTR(data, "calloc percpu data"))
		goto out;

	/* run on every online-CPU */
	for (i = 0; i < num_online; i++) {
		__u64 flags;

		if (!online[i])
			continue;

		topts.cpu = i;
		topts.retval = -1;
		err = bpf_prog_test_run_opts(prog_fd, &topts);
		ASSERT_OK(err, "bpf_prog_test_run_opts");
		ASSERT_EQ(topts.retval, 0, "bpf_prog_test_run_opts retval");

		memset(data, 0, data_sz);
		flags = ((__u64) i << 32) | BPF_F_CPU;
		if (map)
			err = bpf_map__lookup_elem(map, &key, sizeof(key), data, data_sz, flags);
		else
			err = bpf_map_lookup_elem_flags(map_fd, &key, data, flags);
		if (!ASSERT_OK(err, "lookup_elem on cpu"))
			break;

		ASSERT_EQ(*runp, ++run, "run");
		ASSERT_EQ(data->cpu_id[0], i, "cpu_id");
		ASSERT_EQ(data->data, 1, "data");
		ASSERT_TRUE(data->set, "set");
		ASSERT_EQ(data->nums[6], 0xc0de, "nums[6]");
		ASSERT_EQ(data->struct_data.i, 1, "struct_data.i");
		ASSERT_TRUE(data->struct_data.set, "struct_data.set");
		ASSERT_EQ(data->struct_data.nums[6], 0xc0de, "struct_data.nums[6]");
	}

out:
	free(data);
	free(online);
}

static void test_global_percpu_data_init(void)
{
	struct test_global_percpu_data__percpu init_value = {};
	struct test_global_percpu_data__percpu *init_data;
	const __u32 desired_sz = sysconf(_SC_PAGE_SIZE);
	struct test_global_percpu_data *skel = NULL;
	size_t init_data_sz;
	struct bpf_map *map;
	int prog_fd, err;

	skel = test_global_percpu_data__open();
	if (!ASSERT_OK_PTR(skel, "test_global_percpu_data__open"))
		goto out;
	if (!ASSERT_OK_PTR(skel->percpu, "skel->percpu"))
		goto out;
	if (!ASSERT_OK_PTR(skel->data_percpu, "skel->data_percpu"))
		goto out;
	if (!ASSERT_OK_PTR(skel->percpu_data, "skel->percpu_data"))
		goto out;
	if (!ASSERT_OK_PTR(skel->percpu_looooooooong, "skel->percpu_looooooooong"))
		goto out;

	ASSERT_STREQ(bpf_map__name(skel->maps.percpu_data), ".percpu.data",
		     ".percpu.data map name");
	ASSERT_STREQ(bpf_map__name(skel->maps.data_percpu), ".data.percpu",
		     ".data.percpu map name");
	ASSERT_STREQ(bpf_map__name(skel->maps.percpu_looooooooong), ".percpu.looooooooong",
		     "long map name");
	ASSERT_STREQ(bpf_map__name(skel->maps.percpu), ".percpu", "map name");
	ASSERT_EQ(skel->percpu->data, -1, "skel->percpu->data");
	ASSERT_FALSE(skel->percpu->set, "skel->percpu->set");
	ASSERT_EQ(skel->percpu->nums[6], 0, "skel->percpu->nums[6]");
	ASSERT_EQ(skel->percpu->struct_data.i, -1, "struct_data.i");
	ASSERT_FALSE(skel->percpu->struct_data.set, "struct_data.set");
	ASSERT_EQ(skel->percpu->struct_data.nums[6], 0, "struct_data.nums[6]");

	map = skel->maps.percpu;
	if (!ASSERT_EQ(bpf_map__type(map), BPF_MAP_TYPE_PERCPU_ARRAY, "bpf_map__type"))
		goto out;

	init_value.data = 2;
	init_value.nums[6] = -1;
	init_value.struct_data.i = 2;
	init_value.struct_data.nums[6] = -1;
	err = bpf_map__set_initial_value(map, &init_value, sizeof(init_value));
	if (!ASSERT_OK(err, "bpf_map__set_initial_value"))
		goto out;

	init_data = bpf_map__initial_value(map, &init_data_sz);
	if (!ASSERT_OK_PTR(init_data, "bpf_map__initial_value"))
		goto out;

	ASSERT_EQ(init_data->data, init_value.data, "init_value data");
	ASSERT_EQ(init_data->set, init_value.set, "init_value set");
	ASSERT_EQ(init_data->struct_data.i, init_value.struct_data.i, "init_value struct_data.i");
	ASSERT_EQ(init_data->struct_data.nums[6], init_value.struct_data.nums[6],
		  "init_value struct_data.nums[6]");
	ASSERT_EQ(init_data_sz, sizeof(init_value), "init_value size");
	ASSERT_EQ((void *) init_data, (void *) skel->percpu, "skel->percpu eq init_data");
	ASSERT_EQ(skel->percpu->data, init_value.data, "skel->percpu->data");
	ASSERT_EQ(skel->percpu->set, init_value.set, "skel->percpu->set");
	ASSERT_EQ(skel->percpu->struct_data.i, init_value.struct_data.i,
		  "skel->percpu->struct_data.i");
	ASSERT_EQ(skel->percpu->struct_data.nums[6], init_value.struct_data.nums[6],
		  "skel->percpu->struct_data.nums[6]");

	ASSERT_GT(desired_sz, sizeof(init_value), "desired_sz");
	err = bpf_map__set_value_size(map, desired_sz);
	if (!ASSERT_OK(err, "bpf_map__set_value_size"))
		goto out;
	if (!ASSERT_EQ(bpf_map__value_size(map), desired_sz, "percpu value size"))
		goto out;
	if (!ASSERT_NEQ(bpf_map__btf_value_type_id(map), 0, "percpu BTF value type"))
		goto out;

	init_data = bpf_map__initial_value(map, &init_data_sz);
	if (!ASSERT_OK_PTR(init_data, "resized bpf_map__initial_value"))
		goto out;
	if (!ASSERT_EQ(init_data_sz, desired_sz, "resized initial value size"))
		goto out;
	if (!ASSERT_EQ(init_data->data, init_value.data, "resized initial value data"))
		goto out;

	err = test_global_percpu_data__load(skel);
	if (!ASSERT_OK(err, "test_global_percpu_data__load"))
		goto out;

	ASSERT_OK_PTR(skel->percpu, "skel->percpu");

	prog_fd = bpf_program__fd(skel->progs.update_percpu_data);
	test_percpu_data_on_cpus(map, bpf_map__fd(map), prog_fd, &skel->bss->run);

out:
	test_global_percpu_data__destroy(skel);
}

static void test_global_percpu_data_lskel(void)
{
	struct test_global_percpu_data_lskel *lskel = NULL;
	int prog_fd, map_fd;

	lskel = test_global_percpu_data_lskel__open_and_load();
	if (!ASSERT_OK_PTR(lskel, "test_global_percpu_data_lskel__open_and_load"))
		goto out;

	map_fd = lskel->maps.percpu.map_fd;
	prog_fd = lskel->progs.update_percpu_data.prog_fd;
	test_percpu_data_on_cpus(NULL, map_fd, prog_fd, &lskel->bss->run);

out:
	test_global_percpu_data_lskel__destroy(lskel);
}

void test_global_percpu_data(void)
{
	if (!feat_supported(NULL, FEAT_PERCPU_DATA)) {
		test__skip();
		return;
	}

	if (test__start_subtest("init"))
		test_global_percpu_data_init();
	if (test__start_subtest("lskel"))
		test_global_percpu_data_lskel();
}
