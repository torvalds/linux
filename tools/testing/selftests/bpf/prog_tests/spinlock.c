// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

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
	const char *file = "./test_spin_lock.o";
	pthread_t thread_id[4];
	struct bpf_object *obj = NULL;
	int prog_fd;
	int err = 0, i;
	void *ret;

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_CGROUP_SKB, &obj, &prog_fd);
	if (CHECK_FAIL(err)) {
		printf("test_spin_lock:bpf_prog_test_load errno %d\n", errno);
		goto close_prog;
	}
	for (i = 0; i < 4; i++)
		if (CHECK_FAIL(pthread_create(&thread_id[i], NULL,
					      &spin_lock_thread, &prog_fd)))
			goto close_prog;

	for (i = 0; i < 4; i++)
		if (CHECK_FAIL(pthread_join(thread_id[i], &ret) ||
			       ret != (void *)&prog_fd))
			goto close_prog;
close_prog:
	bpf_object__close(obj);
}
