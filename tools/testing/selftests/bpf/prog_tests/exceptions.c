// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

#include "exceptions.skel.h"
#include "exceptions_ext.skel.h"
#include "exceptions_fail.skel.h"
#include "exceptions_assert.skel.h"

static char log_buf[1024 * 1024];

static void test_exceptions_failure(void)
{
	RUN_TESTS(exceptions_fail);
}

static void test_exceptions_success(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, ropts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);
	struct exceptions_ext *eskel = NULL;
	struct exceptions *skel;
	int ret;

	skel = exceptions__open();
	if (!ASSERT_OK_PTR(skel, "exceptions__open"))
		return;

	ret = exceptions__load(skel);
	if (!ASSERT_OK(ret, "exceptions__load"))
		goto done;

	if (!ASSERT_OK(bpf_map_update_elem(bpf_map__fd(skel->maps.jmp_table), &(int){0},
					   &(int){bpf_program__fd(skel->progs.exception_tail_call_target)}, BPF_ANY),
		       "bpf_map_update_elem jmp_table"))
		goto done;

#define RUN_SUCCESS(_prog, return_val)						  \
	if (!test__start_subtest(#_prog)) goto _prog##_##return_val;		  \
	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs._prog), &ropts); \
	ASSERT_OK(ret, #_prog " prog run ret");					  \
	ASSERT_EQ(ropts.retval, return_val, #_prog " prog run retval");		  \
	_prog##_##return_val:

	RUN_SUCCESS(exception_throw_always_1, 64);
	RUN_SUCCESS(exception_throw_always_2, 32);
	RUN_SUCCESS(exception_throw_unwind_1, 16);
	RUN_SUCCESS(exception_throw_unwind_2, 32);
	RUN_SUCCESS(exception_throw_default, 0);
	RUN_SUCCESS(exception_throw_default_value, 5);
	RUN_SUCCESS(exception_tail_call, 24);
	RUN_SUCCESS(exception_ext, 0);
	RUN_SUCCESS(exception_ext_mod_cb_runtime, 35);
	RUN_SUCCESS(exception_throw_subprog, 1);
	RUN_SUCCESS(exception_assert_nz_gfunc, 1);
	RUN_SUCCESS(exception_assert_zero_gfunc, 1);
	RUN_SUCCESS(exception_assert_neg_gfunc, 1);
	RUN_SUCCESS(exception_assert_pos_gfunc, 1);
	RUN_SUCCESS(exception_assert_negeq_gfunc, 1);
	RUN_SUCCESS(exception_assert_poseq_gfunc, 1);
	RUN_SUCCESS(exception_assert_nz_gfunc_with, 1);
	RUN_SUCCESS(exception_assert_zero_gfunc_with, 1);
	RUN_SUCCESS(exception_assert_neg_gfunc_with, 1);
	RUN_SUCCESS(exception_assert_pos_gfunc_with, 1);
	RUN_SUCCESS(exception_assert_negeq_gfunc_with, 1);
	RUN_SUCCESS(exception_assert_poseq_gfunc_with, 1);
	RUN_SUCCESS(exception_bad_assert_nz_gfunc, 0);
	RUN_SUCCESS(exception_bad_assert_zero_gfunc, 0);
	RUN_SUCCESS(exception_bad_assert_neg_gfunc, 0);
	RUN_SUCCESS(exception_bad_assert_pos_gfunc, 0);
	RUN_SUCCESS(exception_bad_assert_negeq_gfunc, 0);
	RUN_SUCCESS(exception_bad_assert_poseq_gfunc, 0);
	RUN_SUCCESS(exception_bad_assert_nz_gfunc_with, 100);
	RUN_SUCCESS(exception_bad_assert_zero_gfunc_with, 105);
	RUN_SUCCESS(exception_bad_assert_neg_gfunc_with, 200);
	RUN_SUCCESS(exception_bad_assert_pos_gfunc_with, 0);
	RUN_SUCCESS(exception_bad_assert_negeq_gfunc_with, 101);
	RUN_SUCCESS(exception_bad_assert_poseq_gfunc_with, 99);
	RUN_SUCCESS(exception_assert_range, 1);
	RUN_SUCCESS(exception_assert_range_with, 1);
	RUN_SUCCESS(exception_bad_assert_range, 0);
	RUN_SUCCESS(exception_bad_assert_range_with, 10);

#define RUN_EXT(load_ret, attach_err, expr, msg, after_link)			  \
	{									  \
		LIBBPF_OPTS(bpf_object_open_opts, o, .kernel_log_buf = log_buf,		 \
						     .kernel_log_size = sizeof(log_buf), \
						     .kernel_log_level = 2);		 \
		exceptions_ext__destroy(eskel);					  \
		eskel = exceptions_ext__open_opts(&o);				  \
		struct bpf_program *prog = NULL;				  \
		struct bpf_link *link = NULL;					  \
		if (!ASSERT_OK_PTR(eskel, "exceptions_ext__open"))		  \
			goto done;						  \
		(expr);								  \
		ASSERT_OK_PTR(bpf_program__name(prog), bpf_program__name(prog));  \
		if (!ASSERT_EQ(exceptions_ext__load(eskel), load_ret,		  \
			       "exceptions_ext__load"))	{			  \
			printf("%s\n", log_buf);				  \
			goto done;						  \
		}								  \
		if (load_ret != 0) {						  \
			if (!ASSERT_OK_PTR(strstr(log_buf, msg), "strstr")) {	  \
				printf("%s\n", log_buf);			  \
				goto done;					  \
			}							  \
		}								  \
		if (!load_ret && attach_err) {					  \
			if (!ASSERT_ERR_PTR(link = bpf_program__attach(prog), "attach err")) \
				goto done;					  \
		} else if (!load_ret) {						  \
			if (!ASSERT_OK_PTR(link = bpf_program__attach(prog), "attach ok"))  \
				goto done;					  \
			(void)(after_link);					  \
			bpf_link__destroy(link);				  \
		}								  \
	}

	if (test__start_subtest("non-throwing fentry -> exception_cb"))
		RUN_EXT(-EINVAL, true, ({
			prog = eskel->progs.pfentry;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_ext_mod_cb_runtime),
				       "exception_cb_mod"), "set_attach_target"))
				goto done;
		}), "FENTRY/FEXIT programs cannot attach to exception callback", 0);

	if (test__start_subtest("throwing fentry -> exception_cb"))
		RUN_EXT(-EINVAL, true, ({
			prog = eskel->progs.throwing_fentry;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_ext_mod_cb_runtime),
				       "exception_cb_mod"), "set_attach_target"))
				goto done;
		}), "FENTRY/FEXIT programs cannot attach to exception callback", 0);

	if (test__start_subtest("non-throwing fexit -> exception_cb"))
		RUN_EXT(-EINVAL, true, ({
			prog = eskel->progs.pfexit;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_ext_mod_cb_runtime),
				       "exception_cb_mod"), "set_attach_target"))
				goto done;
		}), "FENTRY/FEXIT programs cannot attach to exception callback", 0);

	if (test__start_subtest("throwing fexit -> exception_cb"))
		RUN_EXT(-EINVAL, true, ({
			prog = eskel->progs.throwing_fexit;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_ext_mod_cb_runtime),
				       "exception_cb_mod"), "set_attach_target"))
				goto done;
		}), "FENTRY/FEXIT programs cannot attach to exception callback", 0);

	if (test__start_subtest("throwing extension (with custom cb) -> exception_cb"))
		RUN_EXT(-EINVAL, true, ({
			prog = eskel->progs.throwing_exception_cb_extension;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_ext_mod_cb_runtime),
				       "exception_cb_mod"), "set_attach_target"))
				goto done;
		}), "Extension programs cannot attach to exception callback", 0);

	if (test__start_subtest("throwing extension -> global func in exception_cb"))
		RUN_EXT(0, false, ({
			prog = eskel->progs.throwing_exception_cb_extension;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_ext_mod_cb_runtime),
				       "exception_cb_mod_global"), "set_attach_target"))
				goto done;
		}), "", ({ RUN_SUCCESS(exception_ext_mod_cb_runtime, 131); }));

	if (test__start_subtest("throwing extension (with custom cb) -> global func in exception_cb"))
		RUN_EXT(0, false, ({
			prog = eskel->progs.throwing_extension;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_ext),
				       "exception_ext_global"), "set_attach_target"))
				goto done;
		}), "", ({ RUN_SUCCESS(exception_ext, 128); }));

	if (test__start_subtest("non-throwing fentry -> non-throwing subprog"))
		/* non-throwing fentry -> non-throwing subprog : OK */
		RUN_EXT(0, false, ({
			prog = eskel->progs.pfentry;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "subprog"), "set_attach_target"))
				goto done;
		}), "", 0);

	if (test__start_subtest("throwing fentry -> non-throwing subprog"))
		/* throwing fentry -> non-throwing subprog : OK */
		RUN_EXT(0, false, ({
			prog = eskel->progs.throwing_fentry;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "subprog"), "set_attach_target"))
				goto done;
		}), "", 0);

	if (test__start_subtest("non-throwing fentry -> throwing subprog"))
		/* non-throwing fentry -> throwing subprog : OK */
		RUN_EXT(0, false, ({
			prog = eskel->progs.pfentry;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "throwing_subprog"), "set_attach_target"))
				goto done;
		}), "", 0);

	if (test__start_subtest("throwing fentry -> throwing subprog"))
		/* throwing fentry -> throwing subprog : OK */
		RUN_EXT(0, false, ({
			prog = eskel->progs.throwing_fentry;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "throwing_subprog"), "set_attach_target"))
				goto done;
		}), "", 0);

	if (test__start_subtest("non-throwing fexit -> non-throwing subprog"))
		/* non-throwing fexit -> non-throwing subprog : OK */
		RUN_EXT(0, false, ({
			prog = eskel->progs.pfexit;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "subprog"), "set_attach_target"))
				goto done;
		}), "", 0);

	if (test__start_subtest("throwing fexit -> non-throwing subprog"))
		/* throwing fexit -> non-throwing subprog : OK */
		RUN_EXT(0, false, ({
			prog = eskel->progs.throwing_fexit;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "subprog"), "set_attach_target"))
				goto done;
		}), "", 0);

	if (test__start_subtest("non-throwing fexit -> throwing subprog"))
		/* non-throwing fexit -> throwing subprog : OK */
		RUN_EXT(0, false, ({
			prog = eskel->progs.pfexit;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "throwing_subprog"), "set_attach_target"))
				goto done;
		}), "", 0);

	if (test__start_subtest("throwing fexit -> throwing subprog"))
		/* throwing fexit -> throwing subprog : OK */
		RUN_EXT(0, false, ({
			prog = eskel->progs.throwing_fexit;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "throwing_subprog"), "set_attach_target"))
				goto done;
		}), "", 0);

	/* fmod_ret not allowed for subprog - Check so we remember to handle its
	 * throwing specification compatibility with target when supported.
	 */
	if (test__start_subtest("non-throwing fmod_ret -> non-throwing subprog"))
		RUN_EXT(-EINVAL, true, ({
			prog = eskel->progs.pfmod_ret;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "subprog"), "set_attach_target"))
				goto done;
		}), "can't modify return codes of BPF program", 0);

	/* fmod_ret not allowed for subprog - Check so we remember to handle its
	 * throwing specification compatibility with target when supported.
	 */
	if (test__start_subtest("non-throwing fmod_ret -> non-throwing global subprog"))
		RUN_EXT(-EINVAL, true, ({
			prog = eskel->progs.pfmod_ret;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "global_subprog"), "set_attach_target"))
				goto done;
		}), "can't modify return codes of BPF program", 0);

	if (test__start_subtest("non-throwing extension -> non-throwing subprog"))
		/* non-throwing extension -> non-throwing subprog : BAD (!global) */
		RUN_EXT(-EINVAL, true, ({
			prog = eskel->progs.extension;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "subprog"), "set_attach_target"))
				goto done;
		}), "subprog() is not a global function", 0);

	if (test__start_subtest("non-throwing extension -> throwing subprog"))
		/* non-throwing extension -> throwing subprog : BAD (!global) */
		RUN_EXT(-EINVAL, true, ({
			prog = eskel->progs.extension;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "throwing_subprog"), "set_attach_target"))
				goto done;
		}), "throwing_subprog() is not a global function", 0);

	if (test__start_subtest("non-throwing extension -> non-throwing subprog"))
		/* non-throwing extension -> non-throwing global subprog : OK */
		RUN_EXT(0, false, ({
			prog = eskel->progs.extension;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "global_subprog"), "set_attach_target"))
				goto done;
		}), "", 0);

	if (test__start_subtest("non-throwing extension -> throwing global subprog"))
		/* non-throwing extension -> throwing global subprog : OK */
		RUN_EXT(0, false, ({
			prog = eskel->progs.extension;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "throwing_global_subprog"), "set_attach_target"))
				goto done;
		}), "", 0);

	if (test__start_subtest("throwing extension -> throwing global subprog"))
		/* throwing extension -> throwing global subprog : OK */
		RUN_EXT(0, false, ({
			prog = eskel->progs.throwing_extension;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "throwing_global_subprog"), "set_attach_target"))
				goto done;
		}), "", 0);

	if (test__start_subtest("throwing extension -> non-throwing global subprog"))
		/* throwing extension -> non-throwing global subprog : OK */
		RUN_EXT(0, false, ({
			prog = eskel->progs.throwing_extension;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "global_subprog"), "set_attach_target"))
				goto done;
		}), "", 0);

	if (test__start_subtest("non-throwing extension -> main subprog"))
		/* non-throwing extension -> main subprog : OK */
		RUN_EXT(0, false, ({
			prog = eskel->progs.extension;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "exception_throw_subprog"), "set_attach_target"))
				goto done;
		}), "", 0);

	if (test__start_subtest("throwing extension -> main subprog"))
		/* throwing extension -> main subprog : OK */
		RUN_EXT(0, false, ({
			prog = eskel->progs.throwing_extension;
			bpf_program__set_autoload(prog, true);
			if (!ASSERT_OK(bpf_program__set_attach_target(prog,
				       bpf_program__fd(skel->progs.exception_throw_subprog),
				       "exception_throw_subprog"), "set_attach_target"))
				goto done;
		}), "", 0);

done:
	exceptions_ext__destroy(eskel);
	exceptions__destroy(skel);
}

static void test_exceptions_assertions(void)
{
	RUN_TESTS(exceptions_assert);
}

void test_exceptions(void)
{
	test_exceptions_success();
	test_exceptions_failure();
	test_exceptions_assertions();
}
