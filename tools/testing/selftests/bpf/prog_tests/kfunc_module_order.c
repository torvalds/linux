// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <testing_helpers.h>

#include "kfunc_module_order.skel.h"

static int test_run_prog(const struct bpf_program *prog,
			 struct bpf_test_run_opts *opts)
{
	int err;

	err = bpf_prog_test_run_opts(bpf_program__fd(prog), opts);
	if (!ASSERT_OK(err, "bpf_prog_test_run_opts"))
		return err;

	if (!ASSERT_EQ((int)opts->retval, 0, bpf_program__name(prog)))
		return -EINVAL;

	return 0;
}

void test_kfunc_module_order(void)
{
	struct kfunc_module_order *skel;
	char pkt_data[64] = {};
	int err = 0;

	DECLARE_LIBBPF_OPTS(bpf_test_run_opts, test_opts, .data_in = pkt_data,
			    .data_size_in = sizeof(pkt_data));

	err = load_module("bpf_test_modorder_x.ko",
			  env_verbosity > VERBOSE_NONE);
	if (!ASSERT_OK(err, "load bpf_test_modorder_x.ko"))
		return;

	err = load_module("bpf_test_modorder_y.ko",
			  env_verbosity > VERBOSE_NONE);
	if (!ASSERT_OK(err, "load bpf_test_modorder_y.ko"))
		goto exit_modx;

	skel = kfunc_module_order__open_and_load();
	if (!ASSERT_OK_PTR(skel, "kfunc_module_order__open_and_load()")) {
		err = -EINVAL;
		goto exit_mods;
	}

	test_run_prog(skel->progs.call_kfunc_xy, &test_opts);
	test_run_prog(skel->progs.call_kfunc_yx, &test_opts);

	kfunc_module_order__destroy(skel);
exit_mods:
	unload_module("bpf_test_modorder_y", env_verbosity > VERBOSE_NONE);
exit_modx:
	unload_module("bpf_test_modorder_x", env_verbosity > VERBOSE_NONE);
}
