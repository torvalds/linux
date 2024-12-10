// SPDX-License-Identifier: GPL-2.0
#include "bpf/libbpf.h"
#include "changes_pkt_data_freplace.skel.h"
#include "changes_pkt_data.skel.h"
#include <test_progs.h>

static void print_verifier_log(const char *log)
{
	if (env.verbosity >= VERBOSE_VERY)
		fprintf(stdout, "VERIFIER LOG:\n=============\n%s=============\n", log);
}

static void test_aux(const char *main_prog_name, const char *freplace_prog_name, bool expect_load)
{
	struct changes_pkt_data_freplace *freplace = NULL;
	struct bpf_program *freplace_prog = NULL;
	LIBBPF_OPTS(bpf_object_open_opts, opts);
	struct changes_pkt_data *main = NULL;
	char log[16*1024];
	int err;

	opts.kernel_log_buf = log;
	opts.kernel_log_size = sizeof(log);
	if (env.verbosity >= VERBOSE_SUPER)
		opts.kernel_log_level = 1 | 2 | 4;
	main = changes_pkt_data__open_opts(&opts);
	if (!ASSERT_OK_PTR(main, "changes_pkt_data__open"))
		goto out;
	err = changes_pkt_data__load(main);
	print_verifier_log(log);
	if (!ASSERT_OK(err, "changes_pkt_data__load"))
		goto out;
	freplace = changes_pkt_data_freplace__open_opts(&opts);
	if (!ASSERT_OK_PTR(freplace, "changes_pkt_data_freplace__open"))
		goto out;
	freplace_prog = bpf_object__find_program_by_name(freplace->obj, freplace_prog_name);
	if (!ASSERT_OK_PTR(freplace_prog, "freplace_prog"))
		goto out;
	bpf_program__set_autoload(freplace_prog, true);
	bpf_program__set_autoattach(freplace_prog, true);
	bpf_program__set_attach_target(freplace_prog,
				       bpf_program__fd(main->progs.dummy),
				       main_prog_name);
	err = changes_pkt_data_freplace__load(freplace);
	print_verifier_log(log);
	if (expect_load) {
		ASSERT_OK(err, "changes_pkt_data_freplace__load");
	} else {
		ASSERT_ERR(err, "changes_pkt_data_freplace__load");
		ASSERT_HAS_SUBSTR(log, "Extension program changes packet data", "error log");
	}

out:
	changes_pkt_data_freplace__destroy(freplace);
	changes_pkt_data__destroy(main);
}

/* There are two global subprograms in both changes_pkt_data.skel.h:
 * - one changes packet data;
 * - another does not.
 * It is ok to freplace subprograms that change packet data with those
 * that either do or do not. It is only ok to freplace subprograms
 * that do not change packet data with those that do not as well.
 * The below tests check outcomes for each combination of such freplace.
 */
void test_changes_pkt_data_freplace(void)
{
	if (test__start_subtest("changes_with_changes"))
		test_aux("changes_pkt_data", "changes_pkt_data", true);
	if (test__start_subtest("changes_with_doesnt_change"))
		test_aux("changes_pkt_data", "does_not_change_pkt_data", true);
	if (test__start_subtest("doesnt_change_with_changes"))
		test_aux("does_not_change_pkt_data", "changes_pkt_data", false);
	if (test__start_subtest("doesnt_change_with_doesnt_change"))
		test_aux("does_not_change_pkt_data", "does_not_change_pkt_data", true);
}
