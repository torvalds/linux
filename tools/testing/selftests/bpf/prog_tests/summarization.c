// SPDX-License-Identifier: GPL-2.0
#include "bpf/libbpf.h"
#include "summarization_freplace.skel.h"
#include "summarization.skel.h"
#include <test_progs.h>

static void print_verifier_log(const char *log)
{
	if (env.verbosity >= VERBOSE_VERY)
		fprintf(stdout, "VERIFIER LOG:\n=============\n%s=============\n", log);
}

static void test_aux(const char *main_prog_name,
		     const char *to_be_replaced,
		     const char *replacement,
		     bool expect_load,
		     const char *err_msg)
{
	struct summarization_freplace *freplace = NULL;
	struct bpf_program *freplace_prog = NULL;
	struct bpf_program *main_prog = NULL;
	LIBBPF_OPTS(bpf_object_open_opts, opts);
	struct summarization *main = NULL;
	char log[16*1024];
	int err;

	opts.kernel_log_buf = log;
	opts.kernel_log_size = sizeof(log);
	if (env.verbosity >= VERBOSE_SUPER)
		opts.kernel_log_level = 1 | 2 | 4;
	main = summarization__open_opts(&opts);
	if (!ASSERT_OK_PTR(main, "summarization__open"))
		goto out;
	main_prog = bpf_object__find_program_by_name(main->obj, main_prog_name);
	if (!ASSERT_OK_PTR(main_prog, "main_prog"))
		goto out;
	bpf_program__set_autoload(main_prog, true);
	err = summarization__load(main);
	print_verifier_log(log);
	if (!ASSERT_OK(err, "summarization__load"))
		goto out;
	freplace = summarization_freplace__open_opts(&opts);
	if (!ASSERT_OK_PTR(freplace, "summarization_freplace__open"))
		goto out;
	freplace_prog = bpf_object__find_program_by_name(freplace->obj, replacement);
	if (!ASSERT_OK_PTR(freplace_prog, "freplace_prog"))
		goto out;
	bpf_program__set_autoload(freplace_prog, true);
	bpf_program__set_autoattach(freplace_prog, true);
	bpf_program__set_attach_target(freplace_prog,
				       bpf_program__fd(main_prog),
				       to_be_replaced);
	err = summarization_freplace__load(freplace);
	print_verifier_log(log);

	/* The might_sleep extension doesn't work yet as sleepable calls are not
	 * allowed, but preserve the check in case it's supported later and then
	 * this particular combination can be enabled.
	 */
	if (!strcmp("might_sleep", replacement) && err) {
		ASSERT_HAS_SUBSTR(log, "helper call might sleep in a non-sleepable prog", "error log");
		ASSERT_EQ(err, -EINVAL, "err");
		test__skip();
		goto out;
	}

	if (expect_load) {
		ASSERT_OK(err, "summarization_freplace__load");
	} else {
		ASSERT_ERR(err, "summarization_freplace__load");
		ASSERT_HAS_SUBSTR(log, err_msg, "error log");
	}

out:
	summarization_freplace__destroy(freplace);
	summarization__destroy(main);
}

/* There are two global subprograms in both summarization.skel.h:
 * - one changes packet data;
 * - another does not.
 * It is ok to freplace subprograms that change packet data with those
 * that either do or do not. It is only ok to freplace subprograms
 * that do not change packet data with those that do not as well.
 * The below tests check outcomes for each combination of such freplace.
 * Also test a case when main subprogram itself is replaced and is a single
 * subprogram in a program.
 *
 * This holds for might_sleep programs. It is ok to replace might_sleep with
 * might_sleep and with does_not_sleep, but does_not_sleep cannot be replaced
 * with might_sleep.
 */
void test_summarization_freplace(void)
{
	struct {
		const char *main;
		const char *to_be_replaced;
		bool has_side_effect;
	} mains[2][4] = {
		{
			{ "main_changes_with_subprogs",		"changes_pkt_data",	    true },
			{ "main_changes_with_subprogs",		"does_not_change_pkt_data", false },
			{ "main_changes",			"main_changes",             true },
			{ "main_does_not_change",		"main_does_not_change",     false },
		},
		{
			{ "main_might_sleep_with_subprogs",	"might_sleep",		    true },
			{ "main_might_sleep_with_subprogs",	"does_not_sleep",	    false },
			{ "main_might_sleep",			"main_might_sleep",	    true },
			{ "main_does_not_sleep",		"main_does_not_sleep",	    false },
		},
	};
	const char *pkt_err = "Extension program changes packet data";
	const char *slp_err = "Extension program may sleep";
	struct {
		const char *func;
		bool has_side_effect;
		const char *err_msg;
	} replacements[2][2] = {
		{
			{ "changes_pkt_data",	      true,	pkt_err },
			{ "does_not_change_pkt_data", false,	pkt_err },
		},
		{
			{ "might_sleep",	      true,	slp_err },
			{ "does_not_sleep",	      false,	slp_err },
		},
	};
	char buf[64];

	for (int t = 0; t < 2; t++) {
		for (int i = 0; i < ARRAY_SIZE(mains); ++i) {
			for (int j = 0; j < ARRAY_SIZE(replacements); ++j) {
				snprintf(buf, sizeof(buf), "%s_with_%s",
					 mains[t][i].to_be_replaced, replacements[t][j].func);
				if (!test__start_subtest(buf))
					continue;
				test_aux(mains[t][i].main, mains[t][i].to_be_replaced, replacements[t][j].func,
					 mains[t][i].has_side_effect || !replacements[t][j].has_side_effect,
					 replacements[t][j].err_msg);
			}
		}
	}
}
