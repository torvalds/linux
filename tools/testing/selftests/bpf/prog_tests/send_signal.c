// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

static volatile int sigusr1_received = 0;

static void sigusr1_handler(int signum)
{
	sigusr1_received++;
}

static int test_send_signal_common(struct perf_event_attr *attr,
				    int prog_type,
				    const char *test_name)
{
	int err = -1, pmu_fd, prog_fd, info_map_fd, status_map_fd;
	const char *file = "./test_send_signal_kern.o";
	struct bpf_object *obj = NULL;
	int pipe_c2p[2], pipe_p2c[2];
	__u32 key = 0, duration = 0;
	char buf[256];
	pid_t pid;
	__u64 val;

	if (CHECK(pipe(pipe_c2p), test_name,
		  "pipe pipe_c2p error: %s\n", strerror(errno)))
		goto no_fork_done;

	if (CHECK(pipe(pipe_p2c), test_name,
		  "pipe pipe_p2c error: %s\n", strerror(errno))) {
		close(pipe_c2p[0]);
		close(pipe_c2p[1]);
		goto no_fork_done;
	}

	pid = fork();
	if (CHECK(pid < 0, test_name, "fork error: %s\n", strerror(errno))) {
		close(pipe_c2p[0]);
		close(pipe_c2p[1]);
		close(pipe_p2c[0]);
		close(pipe_p2c[1]);
		goto no_fork_done;
	}

	if (pid == 0) {
		/* install signal handler and notify parent */
		signal(SIGUSR1, sigusr1_handler);

		close(pipe_c2p[0]); /* close read */
		close(pipe_p2c[1]); /* close write */

		/* notify parent signal handler is installed */
		write(pipe_c2p[1], buf, 1);

		/* make sure parent enabled bpf program to send_signal */
		read(pipe_p2c[0], buf, 1);

		/* wait a little for signal handler */
		sleep(1);

		if (sigusr1_received)
			write(pipe_c2p[1], "2", 1);
		else
			write(pipe_c2p[1], "0", 1);

		/* wait for parent notification and exit */
		read(pipe_p2c[0], buf, 1);

		close(pipe_c2p[1]);
		close(pipe_p2c[0]);
		exit(0);
	}

	close(pipe_c2p[1]); /* close write */
	close(pipe_p2c[0]); /* close read */

	err = bpf_prog_load(file, prog_type, &obj, &prog_fd);
	if (CHECK(err < 0, test_name, "bpf_prog_load error: %s\n",
		  strerror(errno)))
		goto prog_load_failure;

	pmu_fd = syscall(__NR_perf_event_open, attr, pid, -1,
			 -1 /* group id */, 0 /* flags */);
	if (CHECK(pmu_fd < 0, test_name, "perf_event_open error: %s\n",
		  strerror(errno))) {
		err = -1;
		goto close_prog;
	}

	err = ioctl(pmu_fd, PERF_EVENT_IOC_ENABLE, 0);
	if (CHECK(err < 0, test_name, "ioctl perf_event_ioc_enable error: %s\n",
		  strerror(errno)))
		goto disable_pmu;

	err = ioctl(pmu_fd, PERF_EVENT_IOC_SET_BPF, prog_fd);
	if (CHECK(err < 0, test_name, "ioctl perf_event_ioc_set_bpf error: %s\n",
		  strerror(errno)))
		goto disable_pmu;

	err = -1;
	info_map_fd = bpf_object__find_map_fd_by_name(obj, "info_map");
	if (CHECK(info_map_fd < 0, test_name, "find map %s error\n", "info_map"))
		goto disable_pmu;

	status_map_fd = bpf_object__find_map_fd_by_name(obj, "status_map");
	if (CHECK(status_map_fd < 0, test_name, "find map %s error\n", "status_map"))
		goto disable_pmu;

	/* wait until child signal handler installed */
	read(pipe_c2p[0], buf, 1);

	/* trigger the bpf send_signal */
	key = 0;
	val = (((__u64)(SIGUSR1)) << 32) | pid;
	bpf_map_update_elem(info_map_fd, &key, &val, 0);

	/* notify child that bpf program can send_signal now */
	write(pipe_p2c[1], buf, 1);

	/* wait for result */
	err = read(pipe_c2p[0], buf, 1);
	if (CHECK(err < 0, test_name, "reading pipe error: %s\n", strerror(errno)))
		goto disable_pmu;
	if (CHECK(err == 0, test_name, "reading pipe error: size 0\n")) {
		err = -1;
		goto disable_pmu;
	}

	err = CHECK(buf[0] != '2', test_name, "incorrect result\n");

	/* notify child safe to exit */
	write(pipe_p2c[1], buf, 1);

disable_pmu:
	close(pmu_fd);
close_prog:
	bpf_object__close(obj);
prog_load_failure:
	close(pipe_c2p[0]);
	close(pipe_p2c[1]);
	wait(NULL);
no_fork_done:
	return err;
}

static int test_send_signal_tracepoint(void)
{
	const char *id_path = "/sys/kernel/debug/tracing/events/syscalls/sys_enter_nanosleep/id";
	struct perf_event_attr attr = {
		.type = PERF_TYPE_TRACEPOINT,
		.sample_type = PERF_SAMPLE_RAW | PERF_SAMPLE_CALLCHAIN,
		.sample_period = 1,
		.wakeup_events = 1,
	};
	__u32 duration = 0;
	int bytes, efd;
	char buf[256];

	efd = open(id_path, O_RDONLY, 0);
	if (CHECK(efd < 0, "tracepoint",
		  "open syscalls/sys_enter_nanosleep/id failure: %s\n",
		  strerror(errno)))
		return -1;

	bytes = read(efd, buf, sizeof(buf));
	close(efd);
	if (CHECK(bytes <= 0 || bytes >= sizeof(buf), "tracepoint",
		  "read syscalls/sys_enter_nanosleep/id failure: %s\n",
		  strerror(errno)))
		return -1;

	attr.config = strtol(buf, NULL, 0);

	return test_send_signal_common(&attr, BPF_PROG_TYPE_TRACEPOINT, "tracepoint");
}

static int test_send_signal_nmi(void)
{
	struct perf_event_attr attr = {
		.sample_freq = 50,
		.freq = 1,
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_CPU_CYCLES,
	};

	return test_send_signal_common(&attr, BPF_PROG_TYPE_PERF_EVENT, "perf_event");
}

void test_send_signal(void)
{
	int ret = 0;

	ret |= test_send_signal_tracepoint();
	ret |= test_send_signal_nmi();
	if (!ret)
		printf("test_send_signal:OK\n");
	else
		printf("test_send_signal:FAIL\n");
}
