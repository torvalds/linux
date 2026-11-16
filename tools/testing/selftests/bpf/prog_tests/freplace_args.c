// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "freplace_args.skel.h"
#include "freplace_args_tgt.skel.h"

/*
 * An extension must not expect more of its arguments than the callers of the
 * function that it replaces provide. The progs are loaded and attached to each
 * other. None of them runs.
 */
static const struct test {
	const char *name;
	const char *ext;	/* prog of freplace_args.c */
	const char *tgt;	/* prog of freplace_args_tgt.c */
	const char *func;	/* function of tgt that ext is loaded for */
	const char *func2;	/* function of tgt that ext is attached to */
	int bad_reg;		/* argument that is refused, 0: none */
} tests[] = {
	{ "same_nonnull",		"ext_mem_nonnull",  "tc_prog", "mem_nonnull" },
	{ "same_trusted",		"ext_task_trusted", "tc_prog", "task_trusted" },
	{ "smaller_struct",		"ext_mem_smaller",  "tc_prog", "mem_smaller" },
	{ "larger_struct",		"ext_mem_larger",   "tc_prog", "mem_larger", NULL, 1 },
	{ "nullable_for_nonnull",	"ext_mem",	    "tc_prog", "mem_nonnull" },
	{ "nonnull_for_nullable",	"ext_mem_nonnull",  "tc_prog", "mem", NULL, 2 },
	{ "trusted_for_mem",		"ext_task_trusted", "tc_prog", "task_mem", NULL, 1 },
	{ "arg_ctx_for_ctx_type",	"ext_skb_ctx",	    "tc_prog", "skb_ctx" },
	{ "mem_for_arg_ctx",		"ext_tp_mem",	    "tp_prog", "tagged_ctx", NULL, 1 },
	{ "attach_nullable_to_nonnull",	"ext_mem",	    "tc_prog", "mem", "mem_nonnull" },
	{ "attach_nonnull_to_nullable",	"ext_mem_nonnull",  "tc_prog", "mem_nonnull", "mem", 2 },
};

static void run_test(struct freplace_args_tgt *tgt_skel, const struct test *t)
{
	struct bpf_program *ext, *tgt;
	struct freplace_args *skel;
	char log[4096], msg[128];
	struct bpf_link *link;
	int err;

	skel = freplace_args__open();
	if (!ASSERT_OK_PTR(skel, "open"))
		return;

	ext = bpf_object__find_program_by_name(skel->obj, t->ext);
	tgt = bpf_object__find_program_by_name(tgt_skel->obj, t->tgt);
	if (!ASSERT_OK_PTR(ext, "ext") || !ASSERT_OK_PTR(tgt, "tgt"))
		goto out;

	log[0] = '\0';
	bpf_program__set_autoload(ext, true);
	/* libbpf prints the log of a load that fails and must not */
	if (t->bad_reg && !t->func2)
		bpf_program__set_log_buf(ext, log, sizeof(log));
	err = bpf_program__set_attach_target(ext, bpf_program__fd(tgt), t->func);
	if (!ASSERT_OK(err, "set_attach_target"))
		goto out;

	err = freplace_args__load(skel);
	if (t->bad_reg && !t->func2) {
		snprintf(msg, sizeof(msg),
			 "Extension program's argument in R%d is incompatible with original",
			 t->bad_reg);
		if (ASSERT_ERR(err, "load"))
			ASSERT_HAS_SUBSTR(log, msg, "log");
		goto out;
	}
	if (!ASSERT_OK(err, "load") || !t->func2)
		goto out;

	link = bpf_program__attach_freplace(ext, bpf_program__fd(tgt), t->func2);
	ASSERT_EQ(libbpf_get_error(link), t->bad_reg ? -EINVAL : 0, "attach");
	bpf_link__destroy(link);
out:
	freplace_args__destroy(skel);
}

void test_freplace_args(void)
{
	struct freplace_args_tgt *tgt_skel;
	int i;

	tgt_skel = freplace_args_tgt__open_and_load();
	if (!ASSERT_OK_PTR(tgt_skel, "tgt_open_and_load"))
		return;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (test__start_subtest(tests[i].name))
			run_test(tgt_skel, &tests[i]);
	}

	freplace_args_tgt__destroy(tgt_skel);
}