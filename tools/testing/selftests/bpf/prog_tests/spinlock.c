// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_spinlock(void)
{
	const char *file = "./test_spin_lock.o";
	pthread_t thread_id[4];
	struct bpf_object *obj;
	int prog_fd;
	int err = 0, i;
	void *ret;

	err = bpf_prog_load(file, BPF_PROG_TYPE_CGROUP_SKB, &obj, &prog_fd);
	if (err) {
		printf("test_spin_lock:bpf_prog_load errno %d\n", errno);
		goto close_prog;
	}
	for (i = 0; i < 4; i++)
		assert(pthread_create(&thread_id[i], NULL,
				      &spin_lock_thread, &prog_fd) == 0);
	for (i = 0; i < 4; i++)
		assert(pthread_join(thread_id[i], &ret) == 0 &&
		       ret == (void *)&prog_fd);
	goto close_prog_noerr;
close_prog:
	error_cnt++;
close_prog_noerr:
	bpf_object__close(obj);
}
