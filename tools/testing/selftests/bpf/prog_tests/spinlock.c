// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_spinlock(void)
{
	const char *file = "./test_spin_lock.o";
	pthread_t thread_id[4];
	struct bpf_object *obj = NULL;
	int prog_fd;
	int err = 0, i;
	void *ret;

	err = bpf_prog_load(file, BPF_PROG_TYPE_CGROUP_SKB, &obj, &prog_fd);
	if (CHECK_FAIL(err)) {
		printf("test_spin_lock:bpf_prog_load errno %d\n", errno);
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
