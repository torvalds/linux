// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "data_in_arena.skel.h"
#include "data_in_arena_decl.skel.h"
#include "data_in_arena_fail.skel.h"

static int run_prog(struct bpf_program *prog)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	if (!ASSERT_OK(bpf_prog_test_run_opts(bpf_program__fd(prog), &topts), "test_run"))
		return -1;
	return topts.retval;
}

static void run(struct data_in_arena *skel, int counter)
{
	int i;

	ASSERT_EQ(run_prog(skel->progs.use_data), counter + 7 + 1 + 2, "retval");
	ASSERT_EQ(skel->bss->sum, counter + 7 + 1 + 2, "sum");
	ASSERT_EQ(skel->data->counter, counter + 1, "counter");
	ASSERT_EQ(skel->data->pair[1], 5, "pair[1]");
	for (i = 0; i < 4; i++)
		ASSERT_EQ(skel->bss->table[i], 10 * (i + 1) + i, "table");
}

static void test_in_arena(void)
{
	struct bpf_map_info info = {};
	struct data_in_arena *skel;
	__u32 len = sizeof(info);
	struct bpf_map *arena;
	size_t sz;

	skel = data_in_arena__open();
	if (!ASSERT_OK_PTR(skel, "open"))
		return;

	arena = bpf_object__find_map_by_name(skel->obj, "arena");
	if (!ASSERT_OK_PTR(arena, "arena"))
		goto out;
	ASSERT_EQ(bpf_map__type(arena), BPF_MAP_TYPE_ARENA, "arena type");
	ASSERT_EQ(bpf_map__max_entries(arena), 1, "arena pages");
	ASSERT_OK(bpf_map__set_max_entries(arena, 8), "arena resize");
	ASSERT_FALSE(bpf_map__autocreate(skel->maps.data), "data autocreate");
	ASSERT_FALSE(bpf_map__autocreate(skel->maps.bss), "bss autocreate");
	ASSERT_FALSE(bpf_map__autocreate(skel->maps.rodata), "rodata autocreate");
	ASSERT_EQ(bpf_map__set_autocreate(skel->maps.data, true), -EOPNOTSUPP, "set_autocreate");
	ASSERT_EQ(bpf_map__set_value_size(skel->maps.bss, 4096), -EOPNOTSUPP, "set_value_size");
	ASSERT_EQ(bpf_map__initial_value(skel->maps.data, &sz), skel->data, "initial_value");
	ASSERT_EQ(sz, sizeof(*skel->data), "initial_value size");

	/* initial values are set the usual way */
	skel->data->counter = 100;

	if (!ASSERT_OK(data_in_arena__load(skel), "load"))
		goto out;
	/* there are no maps behind the sections */
	ASSERT_ERR(bpf_map_get_info_by_fd(bpf_map__fd(skel->maps.data), &info, &len), "data map");
	ASSERT_ERR(bpf_map_get_info_by_fd(bpf_map__fd(skel->maps.bss), &info, &len), "bss map");
	ASSERT_ERR(bpf_map_get_info_by_fd(bpf_map__fd(skel->maps.rodata), &info, &len),
		   "rodata map");
	run(skel, 100);

	/* pointers to data next to pointers to functions */
	ASSERT_EQ(run_prog(skel->progs.use_ops), 42 + 1 + 'e', "use_ops");
	ASSERT_EQ(skel->data->counter, 102, "counter");

	/* alignment of sections and pointers to data in data */
	ASSERT_EQ((unsigned long)&skel->bss->aligned64 % 64, 0, "alignment");
	ASSERT_EQ(run_prog(skel->progs.use_ptrs), 0, "use_ptrs");
	ASSERT_EQ(skel->data->x, 43, "x");
	ASSERT_EQ(skel->bss->aligned64.v[7], 7, "aligned64");
	ASSERT_EQ(skel->data->px, &skel->data->x, "px");

	/* format strings of bpf_printk() and BPF_SNPRINTF() */
	ASSERT_EQ(run_prog(skel->progs.use_printk), sizeof("43-7"), "use_printk");
	ASSERT_STREQ(skel->bss->out, "43-7", "out");
out:
	data_in_arena__destroy(skel);
}

/* The object has an arena map and __arena variables */
static void test_declared_arena(void)
{
	struct data_in_arena_decl *skel;
	struct bpf_map *map;
	int arenas = 0;

	skel = data_in_arena_decl__open();
	if (!ASSERT_OK_PTR(skel, "open"))
		return;
	bpf_object__for_each_map(map, skel->obj)
		arenas += bpf_map__type(map) == BPF_MAP_TYPE_ARENA;
	ASSERT_EQ(arenas, 1, "no second arena");
	skel->data->counter = 6;
	if (!ASSERT_OK(data_in_arena_decl__load(skel), "load"))
		goto out;
	ASSERT_EQ(run_prog(skel->progs.use_data), 6 + 7 + 11, "retval");
	ASSERT_EQ(run_prog(skel->progs.use_data), 7 + 7 + 12, "retval");
	ASSERT_EQ(skel->bss->sum, 7 + 7 + 12, "sum");
	ASSERT_EQ(skel->data->counter, 8, "counter");
out:
	data_in_arena_decl__destroy(skel);
}

/* A pointer in data that can't be made an address of arena fails the load */
static void test_ptr_to_map(void)
{
	struct data_in_arena_fail *skel;

	skel = data_in_arena_fail__open();
	if (!ASSERT_OK_PTR(skel, "open"))
		return;
	ASSERT_ERR(data_in_arena_fail__load(skel), "load");
	data_in_arena_fail__destroy(skel);
}

/* So does a pointer to a variable of the kernel. There is no skeleton: the open fails. */
static void test_ptr_to_extern(void)
{
	struct bpf_object *obj;

	obj = bpf_object__open_file("./data_in_arena_extern.bpf.o", NULL);
	if (!ASSERT_ERR_PTR(obj, "open"))
		bpf_object__close(obj);
}

/* __arena variables and no arena map. There is no skeleton: the open fails. */
static void test_arena_var_no_map(void)
{
	struct bpf_object *obj;

	obj = bpf_object__open_file("./data_in_arena_nomap.bpf.o", NULL);
	if (!ASSERT_ERR_PTR(obj, "open"))
		bpf_object__close(obj);
}

/* The address of an arena that libbpf doesn't create is not known. No pointers in data then. */
static void test_not_my_arena(bool pin)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts, .map_flags = BPF_F_MMAPABLE);
	struct data_in_arena *skel;
	struct bpf_map *arena;
	int fd = -1;

	skel = data_in_arena__open();
	if (!ASSERT_OK_PTR(skel, "open"))
		return;
	arena = bpf_object__find_map_by_name(skel->obj, "arena");
	if (!ASSERT_OK_PTR(arena, "arena"))
		goto out;
	if (pin) {
		ASSERT_OK(bpf_map__set_pin_path(arena, "/sys/fs/bpf/data_in_arena"), "pin_path");
	} else {
		fd = bpf_map_create(BPF_MAP_TYPE_ARENA, "arena", 0, 0, 1, &opts);
		if (!ASSERT_GE(fd, 0, "map_create"))
			goto out;
		ASSERT_OK(bpf_map__reuse_fd(arena, fd), "reuse_fd");
	}
	ASSERT_EQ(data_in_arena__load(skel), -ENOTSUP, "load");
out:
	if (fd >= 0)
		close(fd);
	data_in_arena__destroy(skel);
}

/* The object is there if rustc and clang can build it, see Makefile.buildvars */
static void test_rust(void)
{
	const char *file = "./data_in_arena_rust.bpf.o";
	struct bpf_object *obj;

	if (access(file, R_OK)) {
		test__skip();
		return;
	}
	obj = bpf_object__open_file(file, NULL);
	if (!ASSERT_OK_PTR(obj, "open"))
		return;
	if (!ASSERT_OK(bpf_object__load(obj), "load"))
		goto out;
	/* libbpf goes on without BTF when the kernel doesn't take it */
	ASSERT_GE(bpf_object__btf_fd(obj), 0, "btf_fd");
	ASSERT_EQ(run_prog(bpf_object__find_program_by_name(obj, "list_in_data")),
		  100 + 20 + 3, "retval");
out:
	bpf_object__close(obj);
}

void test_data_in_arena(void)
{
#if !defined(__x86_64__) && !defined(__aarch64__)
	/* other JITs don't take BPF_F_ARENA_SCALAR */
	test__skip();
	return;
#endif
	if (test__start_subtest("arena"))
		test_in_arena();
	if (test__start_subtest("declared_arena"))
		test_declared_arena();
	if (test__start_subtest("ptr_to_map"))
		test_ptr_to_map();
	if (test__start_subtest("ptr_to_extern"))
		test_ptr_to_extern();
	if (test__start_subtest("arena_var_no_map"))
		test_arena_var_no_map();
	if (test__start_subtest("pinned_arena"))
		test_not_my_arena(true);
	if (test__start_subtest("reused_arena"))
		test_not_my_arena(false);
	if (test__start_subtest("rust"))
		test_rust();
}
