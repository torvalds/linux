// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_global_data_init(void)
{
	const char *file = "./test_global_data.o";
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

	map = bpf_object__find_map_by_name(obj, "test_glo.rodata");
	if (CHECK_FAIL(!map || !bpf_map__is_internal(map)))
		goto out;

	sz = bpf_map__def(map)->value_size;
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
