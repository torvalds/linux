// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

#include "test_spin_lock.skel.h"

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

void test_spinlock(void)
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
