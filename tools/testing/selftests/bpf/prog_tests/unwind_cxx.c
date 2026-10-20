// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <bpf/btf.h>

/* The progs are under #ifdef __BPF_FEATURE_UNWIND */
static bool llvm_lowers_unwinding(const char *file)
{
	struct btf *btf = btf__parse(file, NULL);
	bool ret;

	ret = btf && btf__find_by_name_kind(btf, "catches", BTF_KIND_FUNC) > 0;
	btf__free(btf);
	return ret;
}

static void run(struct bpf_object *obj, const char *name, int len, int retval, int dtors)
{
	struct bpf_program *prog = bpf_object__find_program_by_name(obj, name);
	struct bpf_map *map = bpf_object__find_map_by_name(obj, ".data.dtors");
	char data[256] = {};
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.data_in = data,
		.data_size_in = len,
		.repeat = 1,
	);
	int key = 0, val = 0;

	if (!ASSERT_OK_PTR(prog, name) || !ASSERT_OK_PTR(map, "dtors"))
		return;
	ASSERT_OK(bpf_map__update_elem(map, &key, sizeof(key), &val, sizeof(val), 0), "clear");
	ASSERT_OK(bpf_prog_test_run_opts(bpf_program__fd(prog), &opts), "test_run");
	ASSERT_EQ(opts.retval, retval, "retval");
	ASSERT_OK(bpf_map__lookup_elem(map, &key, sizeof(key), &val, sizeof(val), 0), "lookup");
	ASSERT_EQ(val, dtors, "dtors");
}

void test_unwind_cxx(void)
{
	const char *file = "./unwind_cxx.bpf.o";
	struct bpf_object *obj;
	struct bpf_program *prog;
	int err = -EINVAL;

	if (access(file, R_OK)) {
		printf("%s:SKIP:gcc doesn't build %s\n", __func__, file);
		test__skip();
		return;
	}
	if (!is_jit_enabled()) {
		printf("%s:SKIP:JIT is disabled\n", __func__);
		test__skip();
		return;
	}
	if (!llvm_lowers_unwinding(file)) {
		printf("%s:SKIP:llvm doesn't lower unwinding\n", __func__);
		test__skip();
		return;
	}
	obj = bpf_object__open_file(file, NULL);
	if (!ASSERT_OK_PTR(obj, "open"))
		return;
	prog = bpf_object__find_program_by_name(obj, "catches_aggregate");
	if (!ASSERT_OK_PTR(prog, "catches_aggregate"))
		goto out;
	bpf_program__set_autoload(prog, false);
	err = bpf_object__load(obj);
	if (err == -ENOTSUPP) {
		printf("%s:SKIP:JIT does not support unwinding\n", __func__);
		test__skip();
		goto out;
	}
	if (!ASSERT_OK(err, "load"))
		goto out;

	/* inner's destructor, then middle's */
	run(obj, "catches", 64, 65, 12);
	run(obj, "catches", 200, 42, 12);
	run(obj, "does_not_catch", 64, 65, 12);
	run(obj, "does_not_catch", 200, 0, 12);
out:
	bpf_object__close(obj);
	if (err)
		return;

	if (!test__start_subtest("aggregate_return"))
		return;
	obj = bpf_object__open_file(file, NULL);
	if (!ASSERT_OK_PTR(obj, "open_aggregate"))
		return;
	bpf_object__for_each_program(prog, obj)
		bpf_program__set_autoload(prog,
					 strcmp(bpf_program__name(prog), "catches_aggregate") == 0);
	if (!ASSERT_OK(bpf_object__load(obj), "load_aggregate"))
		goto out_aggregate;
	run(obj, "catches_aggregate", 64, 71, 0);
	run(obj, "catches_aggregate", 200, 42, 0);
out_aggregate:
	bpf_object__close(obj);
}
