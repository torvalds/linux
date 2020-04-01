// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019 Facebook */
#define _GNU_SOURCE
#include <sched.h>
#include <sys/prctl.h>
#include <test_progs.h>

#define MAX_CNT 100000

static __u64 time_get_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static int test_task_rename(const char *prog)
{
	int i, fd, duration = 0, err;
	char buf[] = "test_overhead";
	__u64 start_time;

	fd = open("/proc/self/comm", O_WRONLY|O_TRUNC);
	if (CHECK(fd < 0, "open /proc", "err %d", errno))
		return -1;
	start_time = time_get_ns();
	for (i = 0; i < MAX_CNT; i++) {
		err = write(fd, buf, sizeof(buf));
		if (err < 0) {
			CHECK(err < 0, "task rename", "err %d", errno);
			close(fd);
			return -1;
		}
	}
	printf("task_rename %s\t%lluK events per sec\n", prog,
	       MAX_CNT * 1000000ll / (time_get_ns() - start_time));
	close(fd);
	return 0;
}

static void test_run(const char *prog)
{
	test_task_rename(prog);
}

static void setaffinity(void)
{
	cpu_set_t cpuset;
	int cpu = 0;

	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);
	sched_setaffinity(0, sizeof(cpuset), &cpuset);
}

void test_test_overhead(void)
{
	const char *kprobe_name = "kprobe/__set_task_comm";
	const char *kretprobe_name = "kretprobe/__set_task_comm";
	const char *raw_tp_name = "raw_tp/task_rename";
	const char *fentry_name = "fentry/__set_task_comm";
	const char *fexit_name = "fexit/__set_task_comm";
	const char *kprobe_func = "__set_task_comm";
	struct bpf_program *kprobe_prog, *kretprobe_prog, *raw_tp_prog;
	struct bpf_program *fentry_prog, *fexit_prog;
	struct bpf_object *obj;
	struct bpf_link *link;
	int err, duration = 0;
	char comm[16] = {};

	if (CHECK_FAIL(prctl(PR_GET_NAME, comm, 0L, 0L, 0L)))
		return;

	obj = bpf_object__open_file("./test_overhead.o", NULL);
	if (CHECK(IS_ERR(obj), "obj_open_file", "err %ld\n", PTR_ERR(obj)))
		return;

	kprobe_prog = bpf_object__find_program_by_title(obj, kprobe_name);
	if (CHECK(!kprobe_prog, "find_probe",
		  "prog '%s' not found\n", kprobe_name))
		goto cleanup;
	kretprobe_prog = bpf_object__find_program_by_title(obj, kretprobe_name);
	if (CHECK(!kretprobe_prog, "find_probe",
		  "prog '%s' not found\n", kretprobe_name))
		goto cleanup;
	raw_tp_prog = bpf_object__find_program_by_title(obj, raw_tp_name);
	if (CHECK(!raw_tp_prog, "find_probe",
		  "prog '%s' not found\n", raw_tp_name))
		goto cleanup;
	fentry_prog = bpf_object__find_program_by_title(obj, fentry_name);
	if (CHECK(!fentry_prog, "find_probe",
		  "prog '%s' not found\n", fentry_name))
		goto cleanup;
	fexit_prog = bpf_object__find_program_by_title(obj, fexit_name);
	if (CHECK(!fexit_prog, "find_probe",
		  "prog '%s' not found\n", fexit_name))
		goto cleanup;

	err = bpf_object__load(obj);
	if (CHECK(err, "obj_load", "err %d\n", err))
		goto cleanup;

	setaffinity();

	/* base line run */
	test_run("base");

	/* attach kprobe */
	link = bpf_program__attach_kprobe(kprobe_prog, false /* retprobe */,
					  kprobe_func);
	if (CHECK(IS_ERR(link), "attach_kprobe", "err %ld\n", PTR_ERR(link)))
		goto cleanup;
	test_run("kprobe");
	bpf_link__destroy(link);

	/* attach kretprobe */
	link = bpf_program__attach_kprobe(kretprobe_prog, true /* retprobe */,
					  kprobe_func);
	if (CHECK(IS_ERR(link), "attach kretprobe", "err %ld\n", PTR_ERR(link)))
		goto cleanup;
	test_run("kretprobe");
	bpf_link__destroy(link);

	/* attach raw_tp */
	link = bpf_program__attach_raw_tracepoint(raw_tp_prog, "task_rename");
	if (CHECK(IS_ERR(link), "attach fentry", "err %ld\n", PTR_ERR(link)))
		goto cleanup;
	test_run("raw_tp");
	bpf_link__destroy(link);

	/* attach fentry */
	link = bpf_program__attach_trace(fentry_prog);
	if (CHECK(IS_ERR(link), "attach fentry", "err %ld\n", PTR_ERR(link)))
		goto cleanup;
	test_run("fentry");
	bpf_link__destroy(link);

	/* attach fexit */
	link = bpf_program__attach_trace(fexit_prog);
	if (CHECK(IS_ERR(link), "attach fexit", "err %ld\n", PTR_ERR(link)))
		goto cleanup;
	test_run("fexit");
	bpf_link__destroy(link);
cleanup:
	prctl(PR_SET_NAME, comm, 0L, 0L, 0L);
	bpf_object__close(obj);
}
