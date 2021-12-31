// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <test_progs.h>

#define MAX_CNT_RAWTP	10ull
#define MAX_STACK_RAWTP	100

static int duration = 0;

struct get_stack_trace_t {
	int pid;
	int kern_stack_size;
	int user_stack_size;
	int user_stack_buildid_size;
	__u64 kern_stack[MAX_STACK_RAWTP];
	__u64 user_stack[MAX_STACK_RAWTP];
	struct bpf_stack_build_id user_stack_buildid[MAX_STACK_RAWTP];
};

static void get_stack_print_output(void *ctx, int cpu, void *data, __u32 size)
{
	bool good_kern_stack = false, good_user_stack = false;
	const char *nonjit_func = "___bpf_prog_run";
	/* perfbuf-submitted data is 4-byte aligned, but we need 8-byte
	 * alignment, so copy data into a local variable, for simplicity
	 */
	struct get_stack_trace_t e;
	int i, num_stack;
	static __u64 cnt;
	struct ksym *ks;

	cnt++;

	memset(&e, 0, sizeof(e));
	memcpy(&e, data, size <= sizeof(e) ? size : sizeof(e));

	if (size < sizeof(struct get_stack_trace_t)) {
		__u64 *raw_data = data;
		bool found = false;

		num_stack = size / sizeof(__u64);
		/* If jit is enabled, we do not have a good way to
		 * verify the sanity of the kernel stack. So we
		 * just assume it is good if the stack is not empty.
		 * This could be improved in the future.
		 */
		if (env.jit_enabled) {
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
		num_stack = e.kern_stack_size / sizeof(__u64);
		if (env.jit_enabled) {
			good_kern_stack = num_stack > 0;
		} else {
			for (i = 0; i < num_stack; i++) {
				ks = ksym_search(e.kern_stack[i]);
				if (ks && (strcmp(ks->name, nonjit_func) == 0)) {
					good_kern_stack = true;
					break;
				}
			}
		}
		if (e.user_stack_size > 0 && e.user_stack_buildid_size > 0)
			good_user_stack = true;
	}

	if (!good_kern_stack)
	    CHECK(!good_kern_stack, "kern_stack", "corrupted kernel stack\n");
	if (!good_user_stack)
	    CHECK(!good_user_stack, "user_stack", "corrupted user stack\n");
}

void test_get_stack_raw_tp(void)
{
	const char *file = "./test_get_stack_rawtp.o";
	const char *file_err = "./test_get_stack_rawtp_err.o";
	const char *prog_name = "bpf_prog1";
	int i, err, prog_fd, exp_cnt = MAX_CNT_RAWTP;
	struct perf_buffer *pb = NULL;
	struct bpf_link *link = NULL;
	struct timespec tv = {0, 10};
	struct bpf_program *prog;
	struct bpf_object *obj;
	struct bpf_map *map;
	cpu_set_t cpu_set;

	err = bpf_prog_test_load(file_err, BPF_PROG_TYPE_RAW_TRACEPOINT, &obj, &prog_fd);
	if (CHECK(err >= 0, "prog_load raw tp", "err %d errno %d\n", err, errno))
		return;

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_RAW_TRACEPOINT, &obj, &prog_fd);
	if (CHECK(err, "prog_load raw tp", "err %d errno %d\n", err, errno))
		return;

	prog = bpf_object__find_program_by_name(obj, prog_name);
	if (CHECK(!prog, "find_probe", "prog '%s' not found\n", prog_name))
		goto close_prog;

	map = bpf_object__find_map_by_name(obj, "perfmap");
	if (CHECK(!map, "bpf_find_map", "not found\n"))
		goto close_prog;

	err = load_kallsyms();
	if (CHECK(err < 0, "load_kallsyms", "err %d errno %d\n", err, errno))
		goto close_prog;

	CPU_ZERO(&cpu_set);
	CPU_SET(0, &cpu_set);
	err = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set);
	if (CHECK(err, "set_affinity", "err %d, errno %d\n", err, errno))
		goto close_prog;

	link = bpf_program__attach_raw_tracepoint(prog, "sys_enter");
	if (!ASSERT_OK_PTR(link, "attach_raw_tp"))
		goto close_prog;

	pb = perf_buffer__new(bpf_map__fd(map), 8, get_stack_print_output,
			      NULL, NULL, NULL);
	if (!ASSERT_OK_PTR(pb, "perf_buf__new"))
		goto close_prog;

	/* trigger some syscall action */
	for (i = 0; i < MAX_CNT_RAWTP; i++)
		nanosleep(&tv, NULL);

	while (exp_cnt > 0) {
		err = perf_buffer__poll(pb, 100);
		if (err < 0 && CHECK(err < 0, "pb__poll", "err %d\n", err))
			goto close_prog;
		exp_cnt -= err;
	}

close_prog:
	bpf_link__destroy(link);
	perf_buffer__free(pb);
	bpf_object__close(obj);
}
