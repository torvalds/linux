// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_reference_tracking(void)
{
	const char *file = "./test_sk_lookup_kern.o";
	struct bpf_object *obj;
	struct bpf_program *prog;
	__u32 duration = 0;
	int err = 0;

	obj = bpf_object__open(file);
	if (CHECK_FAIL(IS_ERR(obj)))
		return;

	bpf_object__for_each_program(prog, obj) {
		const char *title;

		/* Ignore .text sections */
		title = bpf_program__title(prog, false);
		if (strstr(title, ".text") != NULL)
			continue;

		bpf_program__set_type(prog, BPF_PROG_TYPE_SCHED_CLS);

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
	bpf_object__close(obj);
}
