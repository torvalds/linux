// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

static void sigalrm_handler(int s) {}
static struct sigaction sigalrm_action = {
	.sa_handler = sigalrm_handler,
};

static void test_signal_pending_by_type(enum bpf_prog_type prog_type)
{
	struct bpf_insn prog[4096];
	struct itimerval timeo = {
		.it_value.tv_usec = 100000, /* 100ms */
	};
	__u32 duration = 0, retval;
	int prog_fd;
	int err;
	int i;

	for (i = 0; i < ARRAY_SIZE(prog); i++)
		prog[i] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0);
	prog[ARRAY_SIZE(prog) - 1] = BPF_EXIT_INSN();

	prog_fd = bpf_load_program(prog_type, prog, ARRAY_SIZE(prog),
				   "GPL", 0, NULL, 0);
	CHECK(prog_fd < 0, "test-run", "errno %d\n", errno);

	err = sigaction(SIGALRM, &sigalrm_action, NULL);
	CHECK(err, "test-run-signal-sigaction", "errno %d\n", errno);

	err = setitimer(ITIMER_REAL, &timeo, NULL);
	CHECK(err, "test-run-signal-timer", "errno %d\n", errno);

	err = bpf_prog_test_run(prog_fd, 0xffffffff, &pkt_v4, sizeof(pkt_v4),
				NULL, NULL, &retval, &duration);
	CHECK(duration > 500000000, /* 500ms */
	      "test-run-signal-duration",
	      "duration %dns > 500ms\n",
	      duration);

	signal(SIGALRM, SIG_DFL);
}

void test_signal_pending(enum bpf_prog_type prog_type)
{
	test_signal_pending_by_type(BPF_PROG_TYPE_SOCKET_FILTER);
	test_signal_pending_by_type(BPF_PROG_TYPE_FLOW_DISSECTOR);
}
