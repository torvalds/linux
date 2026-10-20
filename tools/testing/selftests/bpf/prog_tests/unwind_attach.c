// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "verifier_unwind.skel.h"
#include "unwind_attach.skel.h"

static char log_buf[16 * 1024];

static int run(int fd)
{
	char data[64] = {};
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.data_in = data,
		.data_size_in = sizeof(data),
		.repeat = 1,
	);

	ASSERT_OK(bpf_prog_test_run_opts(fd, &opts), "test_run");
	return opts.retval;
}

/*
 * msg: why it cannot be loaded. Otherwise retval is what the target returns
 * with the prog attached.
 */
static void attach(int tgt_fd, const char *func, const char *name, const char *msg, int retval)
{
	struct bpf_link *link = NULL;
	struct unwind_attach *skel;
	struct bpf_program *prog;
	int err;

	skel = unwind_attach__open();
	if (!ASSERT_OK_PTR(skel, "unwind_attach__open"))
		return;

	prog = bpf_object__find_program_by_name(skel->obj, name);
	if (!ASSERT_OK_PTR(prog, name))
		goto out;
	bpf_program__set_autoload(prog, true);
	log_buf[0] = 0;
	bpf_program__set_log_buf(prog, log_buf, sizeof(log_buf));
	err = bpf_program__set_attach_target(prog, tgt_fd, func);
	if (!ASSERT_OK(err, "set_attach_target"))
		goto out;

	err = unwind_attach__load(skel);
	if (msg) {
		ASSERT_ERR(err, name);
		ASSERT_HAS_SUBSTR(log_buf, msg, name);
		goto out;
	}
	if (!ASSERT_OK(err, name))
		goto out;

	link = bpf_program__attach(prog);
	if (!ASSERT_OK_PTR(link, "attach"))
		goto out;
	ASSERT_EQ(run(tgt_fd), retval, "retval");
	if (prog == skel->progs.fentry_unwind)
		ASSERT_EQ(skel->bss->fentry_cnt, 1, "fentry_cnt");
out:
	bpf_link__destroy(link);
	unwind_attach__destroy(skel);
}

void test_unwind_attach(void)
{
	struct verifier_unwind *tgt;
	struct bpf_program *prog;
	int tgt_fd, plain_fd, err;

	if (!is_jit_enabled()) {
		printf("%s:SKIP:JIT is disabled\n", __func__);
		test__skip();
		return;
	}
	tgt = verifier_unwind__open();
	if (!ASSERT_OK_PTR(tgt, "verifier_unwind__open"))
		return;
	bpf_object__for_each_program(prog, tgt->obj)
		bpf_program__set_autoload(prog, prog == tgt->progs.calls_global_unwinds ||
						prog == tgt->progs.calls_global_never_unwinds);
	err = verifier_unwind__load(tgt);
	if (err == -ENOTSUPP) {
		printf("%s:SKIP:JIT does not support unwinding\n", __func__);
		test__skip();
		goto out;
	}
	if (!ASSERT_OK(err, "verifier_unwind__load"))
		goto out;
	/*
	 * It calls global_unwinds(), which unwinds with r0 = 5, and returns r0 + 10
	 * from the pad.
	 */
	tgt_fd = bpf_program__fd(tgt->progs.calls_global_unwinds);
	plain_fd = bpf_program__fd(tgt->progs.calls_global_never_unwinds);
	ASSERT_EQ(run(tgt_fd), 15, "retval");

	attach(tgt_fd, "global_unwinds", "fentry_unwind", NULL, 15);
	/* unwind_exit would return to the trampoline */
	attach(tgt_fd, "global_unwinds", "fexit_unwind",
	       "Only fentry can attach to global_unwinds", 0);
	/* An extension leaves either way, like what it replaced */
	attach(tgt_fd, "global_unwinds", "freplace_returns", NULL, 9);
	attach(tgt_fd, "global_unwinds", "freplace_unwinds", NULL, 13);
	/* There is no unwind_goto after the call */
	attach(plain_fd, "global_never_unwinds", "freplace_unwinds",
	       "Extension program can unwind, while original does not", 0);
	ASSERT_EQ(run(tgt_fd), 15, "retval");
out:
	verifier_unwind__destroy(tgt);
}
