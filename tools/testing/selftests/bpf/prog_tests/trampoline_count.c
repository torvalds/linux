// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <test_progs.h>

struct inst {
	struct bpf_object *obj;
	struct bpf_link   *link;
};

static struct bpf_program *load_prog(char *file, char *name, struct inst *inst)
{
	struct bpf_object *obj;
	struct bpf_program *prog;
	int err;

	obj = bpf_object__open_file(file, NULL);
	if (!ASSERT_OK_PTR(obj, "obj_open_file"))
		return NULL;

	inst->obj = obj;

	err = bpf_object__load(obj);
	if (!ASSERT_OK(err, "obj_load"))
		return NULL;

	prog = bpf_object__find_program_by_name(obj, name);
	if (!ASSERT_OK_PTR(prog, "obj_find_prog"))
		return NULL;

	return prog;
}

void test_trampoline_count(void)
{
	char *file = "test_trampoline_count.bpf.o";
	char *const progs[] = { "fentry_test", "fmod_ret_test", "fexit_test" };
	int bpf_max_tramp_links, i;
	struct bpf_program *prog;
	struct bpf_link *link;
	struct inst *inst;

	bpf_max_tramp_links = get_bpf_max_tramp_links();
	if (!ASSERT_GE(bpf_max_tramp_links, 1, "bpf_max_tramp_links"))
		return;
	inst = calloc(bpf_max_tramp_links + 1, sizeof(*inst));
	if (!ASSERT_OK_PTR(inst, "inst"))
		return;

	/* attach 'allowed' trampoline programs */
	for (i = 0; i < bpf_max_tramp_links; i++) {
		prog = load_prog(file, progs[i % ARRAY_SIZE(progs)], &inst[i]);
		if (!prog)
			goto cleanup;

		link = bpf_program__attach(prog);
		if (!ASSERT_OK_PTR(link, "attach_prog"))
			goto cleanup;

		inst[i].link = link;
	}

	/* and try 1 extra.. */
	prog = load_prog(file, "fmod_ret_test", &inst[i]);
	if (!prog)
		goto cleanup;

	/* ..that needs to fail */
	link = bpf_program__attach(prog);
	if (!ASSERT_ERR_PTR(link, "attach_prog")) {
		inst[i].link = link;
		goto cleanup;
	}

	/* with E2BIG error */
	if (!ASSERT_EQ(libbpf_get_error(link), -E2BIG, "E2BIG"))
		goto cleanup;
	if (!ASSERT_EQ(link, NULL, "ptr_is_null"))
		goto cleanup;

	/* and finally execute the probe */
	ASSERT_OK(trigger_module_test_read(256), "trigger_module_test_read");

cleanup:
	for (; i >= 0; i--) {
		bpf_link__destroy(inst[i].link);
		bpf_object__close(inst[i].obj);
	}
	free(inst);
}
