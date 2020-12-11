// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_reference_tracking(void)
{
	const char *file = "test_sk_lookup_kern.o";
	const char *obj_name = "ref_track";
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, open_opts,
		.object_name = obj_name,
		.relaxed_maps = true,
	);
	struct bpf_object *obj;
	struct bpf_program *prog;
	__u32 duration = 0;
	int err = 0;

	obj = bpf_object__open_file(file, &open_opts);
	if (CHECK_FAIL(IS_ERR(obj)))
		return;

	if (CHECK(strcmp(bpf_object__name(obj), obj_name), "obj_name",
		  "wrong obj name '%s', expected '%s'\n",
		  bpf_object__name(obj), obj_name))
		goto cleanup;

	bpf_object__for_each_program(prog, obj) {
		const char *title;

		/* Ignore .text sections */
		title = bpf_program__section_name(prog);
		if (strstr(title, ".text") != NULL)
			continue;

		if (!test__start_subtest(title))
			continue;

		/* Expect verifier failure if test name has 'fail' */
		if (strstr(title, "fail") != NULL) {
			libbpf_print_fn_t old_print_fn;

			old_print_fn = libbpf_set_print(NULL);
			err = !bpf_program__load(prog, "GPL", 0);
			libbpf_set_print(old_print_fn);
		} else {
			err = bpf_program__load(prog, "GPL", 0);
		}
		CHECK(err, title, "\n");
	}

cleanup:
	bpf_object__close(obj);
}
