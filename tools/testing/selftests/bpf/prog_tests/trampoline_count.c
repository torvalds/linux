// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <test_progs.h>

#define MAX_TRAMP_PROGS 38

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

/* TODO: use different target function to run in concurrent mode */
void serial_test_trampoline_count(void)
{
	char *file = "test_trampoline_count.bpf.o";
	char *const progs[] = { "fentry_test", "fmod_ret_test", "fexit_test" };
	struct inst inst[MAX_TRAMP_PROGS + 1] = {};
	struct bpf_program *prog;
	struct bpf_link *link;
	int prog_fd, err, i;
	LIBBPF_OPTS(bpf_test_run_opts, opts);

	/* attach 'allowed' trampoline programs */
	for (i = 0; i < MAX_TRAMP_PROGS; i++) {
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

	/* and finaly execute the probe */
	prog_fd = bpf_program__fd(prog);
	if (!ASSERT_GE(prog_fd, 0, "bpf_program__fd"))
		goto cleanup;

	err = bpf_prog_test_run_opts(prog_fd, &opts);
	if (!ASSERT_OK(err, "bpf_prog_test_run_opts"))
		goto cleanup;

	ASSERT_EQ(opts.retval & 0xffff, 4, "bpf_modify_return_test.result");
	ASSERT_EQ(opts.retval >> 16, 1, "bpf_modify_return_test.side_effect");

cleanup:
	for (; i >= 0; i--) {
		bpf_link__destroy(inst[i].link);
		bpf_object__close(inst[i].obj);
	}
}
