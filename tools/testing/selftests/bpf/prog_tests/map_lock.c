// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

static void *parallel_map_access(void *arg)
{
	int err, map_fd = *(u32 *) arg;
	int vars[17], i, j, rnd, key = 0;

	for (i = 0; i < 10000; i++) {
		err = bpf_map_lookup_elem_flags(map_fd, &key, vars, BPF_F_LOCK);
		if (CHECK_FAIL(err)) {
			printf("lookup failed\n");
			goto out;
		}
		if (CHECK_FAIL(vars[0] != 0)) {
			printf("lookup #%d var[0]=%d\n", i, vars[0]);
			goto out;
		}
		rnd = vars[1];
		for (j = 2; j < 17; j++) {
			if (vars[j] == rnd)
				continue;
			printf("lookup #%d var[1]=%d var[%d]=%d\n",
			       i, rnd, j, vars[j]);
			CHECK_FAIL(vars[j] != rnd);
			goto out;
		}
	}
out:
	pthread_exit(arg);
}

void test_map_lock(void)
{
	const char *file = "./test_map_lock.o";
	int prog_fd, map_fd[2], vars[17] = {};
	pthread_t thread_id[6];
	struct bpf_object *obj = NULL;
	int err = 0, key = 0, i;
	void *ret;

	err = bpf_prog_load(file, BPF_PROG_TYPE_CGROUP_SKB, &obj, &prog_fd);
	if (CHECK_FAIL(err)) {
		printf("test_map_lock:bpf_prog_load errno %d\n", errno);
		goto close_prog;
	}
	map_fd[0] = bpf_find_map(__func__, obj, "hash_map");
	if (CHECK_FAIL(map_fd[0] < 0))
		goto close_prog;
	map_fd[1] = bpf_find_map(__func__, obj, "array_map");
	if (CHECK_FAIL(map_fd[1] < 0))
		goto close_prog;

	bpf_map_update_elem(map_fd[0], &key, vars, BPF_F_LOCK);

	for (i = 0; i < 4; i++)
		if (CHECK_FAIL(pthread_create(&thread_id[i], NULL,
					      &spin_lock_thread, &prog_fd)))
			goto close_prog;
	for (i = 4; i < 6; i++)
		if (CHECK_FAIL(pthread_create(&thread_id[i], NULL,
					      &parallel_map_access,
					      &map_fd[i - 4])))
			goto close_prog;
	for (i = 0; i < 4; i++)
		if (CHECK_FAIL(pthread_join(thread_id[i], &ret) ||
			       ret != (void *)&prog_fd))
			goto close_prog;
	for (i = 4; i < 6; i++)
		if (CHECK_FAIL(pthread_join(thread_id[i], &ret) ||
			       ret != (void *)&map_fd[i - 4]))
			goto close_prog;
close_prog:
	bpf_object__close(obj);
}
