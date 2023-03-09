// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

#include "test_spin_lock.skel.h"
#include "test_spin_lock_fail.skel.h"

static char log_buf[1024 * 1024];

static struct {
	const char *prog_name;
	const char *err_msg;
} spin_lock_fail_tests[] = {
	{ "lock_id_kptr_preserve",
	  "5: (bf) r1 = r0                       ; R0_w=ptr_foo(id=2,ref_obj_id=2,off=0,imm=0) "
	  "R1_w=ptr_foo(id=2,ref_obj_id=2,off=0,imm=0) refs=2\n6: (85) call bpf_this_cpu_ptr#154\n"
	  "R1 type=ptr_ expected=percpu_ptr_" },
	{ "lock_id_global_zero",
	  "; R1_w=map_value(off=0,ks=4,vs=4,imm=0)\n2: (85) call bpf_this_cpu_ptr#154\n"
	  "R1 type=map_value expected=percpu_ptr_" },
	{ "lock_id_mapval_preserve",
	  "8: (bf) r1 = r0                       ; R0_w=map_value(id=1,off=0,ks=4,vs=8,imm=0) "
	  "R1_w=map_value(id=1,off=0,ks=4,vs=8,imm=0)\n9: (85) call bpf_this_cpu_ptr#154\n"
	  "R1 type=map_value expected=percpu_ptr_" },
	{ "lock_id_innermapval_preserve",
	  "13: (bf) r1 = r0                      ; R0=map_value(id=2,off=0,ks=4,vs=8,imm=0) "
	  "R1_w=map_value(id=2,off=0,ks=4,vs=8,imm=0)\n14: (85) call bpf_this_cpu_ptr#154\n"
	  "R1 type=map_value expected=percpu_ptr_" },
	{ "lock_id_mismatch_kptr_kptr", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_kptr_global", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_kptr_mapval", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_kptr_innermapval", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_global_global", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_global_kptr", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_global_mapval", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_global_innermapval", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_mapval_mapval", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_mapval_kptr", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_mapval_global", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_mapval_innermapval", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_innermapval_innermapval1", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_innermapval_innermapval2", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_innermapval_kptr", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_innermapval_global", "bpf_spin_unlock of different lock" },
	{ "lock_id_mismatch_innermapval_mapval", "bpf_spin_unlock of different lock" },
};

static void test_spin_lock_fail_prog(const char *prog_name, const char *err_msg)
{
	LIBBPF_OPTS(bpf_object_open_opts, opts, .kernel_log_buf = log_buf,
						.kernel_log_size = sizeof(log_buf),
						.kernel_log_level = 1);
	struct test_spin_lock_fail *skel;
	struct bpf_program *prog;
	int ret;

	skel = test_spin_lock_fail__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "test_spin_lock_fail__open_opts"))
		return;

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
		goto end;

	bpf_program__set_autoload(prog, true);

	ret = test_spin_lock_fail__load(skel);
	if (!ASSERT_ERR(ret, "test_spin_lock_fail__load must fail"))
		goto end;

	/* Skip check if JIT does not support kfuncs */
	if (strstr(log_buf, "JIT does not support calling kernel function")) {
		test__skip();
		goto end;
	}

	if (!ASSERT_OK_PTR(strstr(log_buf, err_msg), "expected error message")) {
		fprintf(stderr, "Expected: %s\n", err_msg);
		fprintf(stderr, "Verifier: %s\n", log_buf);
	}

end:
	test_spin_lock_fail__destroy(skel);
}

static void *spin_lock_thread(void *arg)
{
	int err, prog_fd = *(u32 *) arg;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 10000,
	);

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_OK(topts.retval, "test_run retval");
	pthread_exit(arg);
}

void test_spin_lock_success(void)
{
	struct test_spin_lock *skel;
	pthread_t thread_id[4];
	int prog_fd, i;
	void *ret;

	skel = test_spin_lock__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_spin_lock__open_and_load"))
		return;
	prog_fd = bpf_program__fd(skel->progs.bpf_spin_lock_test);
	for (i = 0; i < 4; i++) {
		int err;

		err = pthread_create(&thread_id[i], NULL, &spin_lock_thread, &prog_fd);
		if (!ASSERT_OK(err, "pthread_create"))
			goto end;
	}

	for (i = 0; i < 4; i++) {
		if (!ASSERT_OK(pthread_join(thread_id[i], &ret), "pthread_join"))
			goto end;
		if (!ASSERT_EQ(ret, &prog_fd, "ret == prog_fd"))
			goto end;
	}
end:
	test_spin_lock__destroy(skel);
}

void test_spin_lock(void)
{
	int i;

	test_spin_lock_success();

	for (i = 0; i < ARRAY_SIZE(spin_lock_fail_tests); i++) {
		if (!test__start_subtest(spin_lock_fail_tests[i].prog_name))
			continue;
		test_spin_lock_fail_prog(spin_lock_fail_tests[i].prog_name,
					 spin_lock_fail_tests[i].err_msg);
	}
}
