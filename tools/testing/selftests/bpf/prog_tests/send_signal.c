// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "test_send_signal_kern.skel.h"

int sigusr1_received = 0;

static void sigusr1_handler(int signum)
{
	sigusr1_received++;
}

static void test_send_signal_common(struct perf_event_attr *attr,
				    bool signal_thread,
				    const char *test_name)
{
	struct test_send_signal_kern *skel;
	int pipe_c2p[2], pipe_p2c[2];
	int err = -1, pmu_fd = -1;
	__u32 duration = 0;
	char buf[256];
	pid_t pid;

	if (CHECK(pipe(pipe_c2p), test_name,
		  "pipe pipe_c2p error: %s\n", strerror(errno)))
		return;

	if (CHECK(pipe(pipe_p2c), test_name,
		  "pipe pipe_p2c error: %s\n", strerror(errno))) {
		close(pipe_c2p[0]);
		close(pipe_c2p[1]);
		return;
	}

	pid = fork();
	if (CHECK(pid < 0, test_name, "fork error: %s\n", strerror(errno))) {
		close(pipe_c2p[0]);
		close(pipe_c2p[1]);
		close(pipe_p2c[0]);
		close(pipe_p2c[1]);
		return;
	}

	if (pid == 0) {
		/* install signal handler and notify parent */
		signal(SIGUSR1, sigusr1_handler);

		close(pipe_c2p[0]); /* close read */
		close(pipe_p2c[1]); /* close write */

		/* notify parent signal handler is installed */
		CHECK(write(pipe_c2p[1], buf, 1) != 1, "pipe_write", "err %d\n", -errno);

		/* make sure parent enabled bpf program to send_signal */
		CHECK(read(pipe_p2c[0], buf, 1) != 1, "pipe_read", "err %d\n", -errno);

		/* wait a little for signal handler */
		sleep(1);

		buf[0] = sigusr1_received ? '2' : '0';
		CHECK(write(pipe_c2p[1], buf, 1) != 1, "pipe_write", "err %d\n", -errno);

		/* wait for parent notification and exit */
		CHECK(read(pipe_p2c[0], buf, 1) != 1, "pipe_read", "err %d\n", -errno);

		close(pipe_c2p[1]);
		close(pipe_p2c[0]);
		exit(0);
	}

	close(pipe_c2p[1]); /* close write */
	close(pipe_p2c[0]); /* close read */

	skel = test_send_signal_kern__open_and_load();
	if (CHECK(!skel, "skel_open_and_load", "skeleton open_and_load failed\n"))
		goto skel_open_load_failure;

	if (!attr) {
		err = test_send_signal_kern__attach(skel);
		if (CHECK(err, "skel_attach", "skeleton attach failed\n")) {
			err = -1;
			goto destroy_skel;
		}
	} else {
		pmu_fd = syscall(__NR_perf_event_open, attr, pid, -1,
				 -1 /* group id */, 0 /* flags */);
		if (CHECK(pmu_fd < 0, test_name, "perf_event_open error: %s\n",
			strerror(errno))) {
			err = -1;
			goto destroy_skel;
		}

		skel->links.send_signal_perf =
			bpf_program__attach_perf_event(skel->progs.send_signal_perf, pmu_fd);
		if (!ASSERT_OK_PTR(skel->links.send_signal_perf, "attach_perf_event"))
			goto disable_pmu;
	}

	/* wait until child signal handler installed */
	CHECK(read(pipe_c2p[0], buf, 1) != 1, "pipe_read", "err %d\n", -errno);

	/* trigger the bpf send_signal */
	skel->bss->pid = pid;
	skel->bss->sig = SIGUSR1;
	skel->bss->signal_thread = signal_thread;

	/* notify child that bpf program can send_signal now */
	CHECK(write(pipe_p2c[1], buf, 1) != 1, "pipe_write", "err %d\n", -errno);

	/* wait for result */
	err = read(pipe_c2p[0], buf, 1);
	if (CHECK(err < 0, test_name, "reading pipe error: %s\n", strerror(errno)))
		goto disable_pmu;
	if (CHECK(err == 0, test_name, "reading pipe error: size 0\n")) {
		err = -1;
		goto disable_pmu;
	}

	CHECK(buf[0] != '2', test_name, "incorrect result\n");

	/* notify child safe to exit */
	CHECK(write(pipe_p2c[1], buf, 1) != 1, "pipe_write", "err %d\n", -errno);

disable_pmu:
	close(pmu_fd);
destroy_skel:
	test_send_signal_kern__destroy(skel);
skel_open_load_failure:
	close(pipe_c2p[0]);
	close(pipe_p2c[1]);
	wait(NULL);
}

static void test_send_signal_tracepoint(bool signal_thread)
{
	test_send_signal_common(NULL, signal_thread, "tracepoint");
}

static void test_send_signal_perf(bool signal_thread)
{
	struct perf_event_attr attr = {
		.sample_period = 1,
		.type = PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_CPU_CLOCK,
	};

	test_send_signal_common(&attr, signal_thread, "perf_sw_event");
}

static void test_send_signal_nmi(bool signal_thread)
{
	struct perf_event_attr attr = {
		.sample_period = 1,
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_CPU_CYCLES,
	};
	int pmu_fd;

	/* Some setups (e.g. virtual machines) might run with hardware
	 * perf events disabled. If this is the case, skip this test.
	 */
	pmu_fd = syscall(__NR_perf_event_open, &attr, 0 /* pid */,
			 -1 /* cpu */, -1 /* group_fd */, 0 /* flags */);
	if (pmu_fd == -1) {
		if (errno == ENOENT) {
			printf("%s:SKIP:no PERF_COUNT_HW_CPU_CYCLES\n",
			       __func__);
			test__skip();
			return;
		}
		/* Let the test fail with a more informative message */
	} else {
		close(pmu_fd);
	}

	test_send_signal_common(&attr, signal_thread, "perf_hw_event");
}

void test_send_signal(void)
{
	if (test__start_subtest("send_signal_tracepoint"))
		test_send_signal_tracepoint(false);
	if (test__start_subtest("send_signal_perf"))
		test_send_signal_perf(false);
	if (test__start_subtest("send_signal_nmi"))
		test_send_signal_nmi(false);
	if (test__start_subtest("send_signal_tracepoint_thread"))
		test_send_signal_tracepoint(true);
	if (test__start_subtest("send_signal_perf_thread"))
		test_send_signal_perf(true);
	if (test__start_subtest("send_signal_nmi_thread"))
		test_send_signal_nmi(true);
}
