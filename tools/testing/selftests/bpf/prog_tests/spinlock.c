// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

static void *spin_lock_thread(void *arg)
{
	__u32 duration, retval;
	int err, prog_fd = *(u32 *) arg;

	err = bpf_prog_test_run(prog_fd, 10000, &pkt_v4, sizeof(pkt_v4),
				NULL, NULL, &retval, &duration);
	CHECK(err || retval, "",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, retval, duration);
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
