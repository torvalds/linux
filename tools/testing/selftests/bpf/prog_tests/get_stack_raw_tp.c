// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

#define MAX_CNT_RAWTP	10ull
#define MAX_STACK_RAWTP	100
struct get_stack_trace_t {
	int pid;
	int kern_stack_size;
	int user_stack_size;
	int user_stack_buildid_size;
	__u64 kern_stack[MAX_STACK_RAWTP];
	__u64 user_stack[MAX_STACK_RAWTP];
	struct bpf_stack_build_id user_stack_buildid[MAX_STACK_RAWTP];
};

static int get_stack_print_output(void *data, int size)
{
	bool good_kern_stack = false, good_user_stack = false;
	const char *nonjit_func = "___bpf_prog_run";
	struct get_stack_trace_t *e = data;
	int i, num_stack;
	static __u64 cnt;
	struct ksym *ks;

	cnt++;

	if (size < sizeof(struct get_stack_trace_t)) {
		__u64 *raw_data = data;
		bool found = false;

		num_stack = size / sizeof(__u64);
		/* If jit is enabled, we do not have a good way to
		 * verify the sanity of the kernel stack. So we
		 * just assume it is good if the stack is not empty.
		 * This could be improved in the future.
		 */
		if (jit_enabled) {
			found = num_stack > 0;
		} else {
			for (i = 0; i < num_stack; i++) {
				ks = ksym_search(raw_data[i]);
				if (ks && (strcmp(ks->name, nonjit_func) == 0)) {
					found = true;
					break;
				}
			}
		}
		if (found) {
			good_kern_stack = true;
			good_user_stack = true;
		}
	} else {
		num_stack = e->kern_stack_size / sizeof(__u64);
		if (jit_enabled) {
			good_kern_stack = num_stack > 0;
		} else {
			for (i = 0; i < num_stack; i++) {
				ks = ksym_search(e->kern_stack[i]);
				if (ks && (strcmp(ks->name, nonjit_func) == 0)) {
					good_kern_stack = true;
					break;
				}
			}
		}
		if (e->user_stack_size > 0 && e->user_stack_buildid_size > 0)
			good_user_stack = true;
	}
	if (!good_kern_stack || !good_user_stack)
		return LIBBPF_PERF_EVENT_ERROR;

	if (cnt == MAX_CNT_RAWTP)
		return LIBBPF_PERF_EVENT_DONE;

	return LIBBPF_PERF_EVENT_CONT;
}

void test_get_stack_raw_tp(void)
{
	const char *file = "./test_get_stack_rawtp.o";
	int i, efd, err, prog_fd, pmu_fd, perfmap_fd;
	struct perf_event_attr attr = {};
	struct timespec tv = {0, 10};
	__u32 key = 0, duration = 0;
	struct bpf_object *obj;

	err = bpf_prog_load(file, BPF_PROG_TYPE_RAW_TRACEPOINT, &obj, &prog_fd);
	if (CHECK(err, "prog_load raw tp", "err %d errno %d\n", err, errno))
		return;

	efd = bpf_raw_tracepoint_open("sys_enter", prog_fd);
	if (CHECK(efd < 0, "raw_tp_open", "err %d errno %d\n", efd, errno))
		goto close_prog;

	perfmap_fd = bpf_find_map(__func__, obj, "perfmap");
	if (CHECK(perfmap_fd < 0, "bpf_find_map", "err %d errno %d\n",
		  perfmap_fd, errno))
		goto close_prog;

	err = load_kallsyms();
	if (CHECK(err < 0, "load_kallsyms", "err %d errno %d\n", err, errno))
		goto close_prog;

	attr.sample_type = PERF_SAMPLE_RAW;
	attr.type = PERF_TYPE_SOFTWARE;
	attr.config = PERF_COUNT_SW_BPF_OUTPUT;
	pmu_fd = syscall(__NR_perf_event_open, &attr, getpid()/*pid*/, -1/*cpu*/,
			 -1/*group_fd*/, 0);
	if (CHECK(pmu_fd < 0, "perf_event_open", "err %d errno %d\n", pmu_fd,
		  errno))
		goto close_prog;

	err = bpf_map_update_elem(perfmap_fd, &key, &pmu_fd, BPF_ANY);
	if (CHECK(err < 0, "bpf_map_update_elem", "err %d errno %d\n", err,
		  errno))
		goto close_prog;

	err = ioctl(pmu_fd, PERF_EVENT_IOC_ENABLE, 0);
	if (CHECK(err < 0, "ioctl PERF_EVENT_IOC_ENABLE", "err %d errno %d\n",
		  err, errno))
		goto close_prog;

	err = perf_event_mmap(pmu_fd);
	if (CHECK(err < 0, "perf_event_mmap", "err %d errno %d\n", err, errno))
		goto close_prog;

	/* trigger some syscall action */
	for (i = 0; i < MAX_CNT_RAWTP; i++)
		nanosleep(&tv, NULL);

	err = perf_event_poller(pmu_fd, get_stack_print_output);
	if (CHECK(err < 0, "perf_event_poller", "err %d errno %d\n", err, errno))
		goto close_prog;

	goto close_prog_noerr;
close_prog:
	error_cnt++;
close_prog_noerr:
	bpf_object__close(obj);
}
