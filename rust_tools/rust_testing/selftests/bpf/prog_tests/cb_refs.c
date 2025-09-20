// SPDX-License-Identifier: GPL-2.0
#include "bpf/libbpf.h"
#include <test_progs.h>
#include <network_helpers.h>

#include "cb_refs.skel.h"

static char log_buf[1024 * 1024];

struct {
	const char *prog_name;
	const char *err_msg;
} cb_refs_tests[] = {
	{ "underflow_prog", "must point to scalar, or struct with scalar" },
	{ "leak_prog", "Possibly NULL pointer passed to helper arg2" },
	{ "nested_cb", "Unreleased reference id=4 alloc_insn=2" }, /* alloc_insn=2{4,5} */
	{ "non_cb_transfer_ref", "Unreleased reference id=4 alloc_insn=1" }, /* alloc_insn=1{1,2} */
};

void test_cb_refs(void)
{
	LIBBPF_OPTS(bpf_object_open_opts, opts, .kernel_log_buf = log_buf,
						.kernel_log_size = sizeof(log_buf),
						.kernel_log_level = 1);
	struct bpf_program *prog;
	struct cb_refs *skel;
	int i;

	for (i = 0; i < ARRAY_SIZE(cb_refs_tests); i++) {
		LIBBPF_OPTS(bpf_test_run_opts, run_opts,
			.data_in = &pkt_v4,
			.data_size_in = sizeof(pkt_v4),
			.repeat = 1,
		);
		skel = cb_refs__open_opts(&opts);
		if (!ASSERT_OK_PTR(skel, "cb_refs__open_and_load"))
			return;
		prog = bpf_object__find_program_by_name(skel->obj, cb_refs_tests[i].prog_name);
		bpf_program__set_autoload(prog, true);
		if (!ASSERT_ERR(cb_refs__load(skel), "cb_refs__load"))
			bpf_prog_test_run_opts(bpf_program__fd(prog), &run_opts);
		if (!ASSERT_OK_PTR(strstr(log_buf, cb_refs_tests[i].err_msg), "expected error message")) {
			fprintf(stderr, "Expected: %s\n", cb_refs_tests[i].err_msg);
			fprintf(stderr, "Verifier: %s\n", log_buf);
		}
		cb_refs__destroy(skel);
	}
}
