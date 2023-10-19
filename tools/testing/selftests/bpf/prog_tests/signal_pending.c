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
	int prog_fd;
	int err;
	int i;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 0xffffffff,
	);

	for (i = 0; i < ARRAY_SIZE(prog); i++)
		prog[i] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0);
	prog[ARRAY_SIZE(prog) - 1] = BPF_EXIT_INSN();

	prog_fd = bpf_test_load_program(prog_type, prog, ARRAY_SIZE(prog),
				   "GPL", 0, NULL, 0);
	ASSERT_GE(prog_fd, 0, "test-run load");

	err = sigaction(SIGALRM, &sigalrm_action, NULL);
	ASSERT_OK(err, "test-run-signal-sigaction");

	err = setitimer(ITIMER_REAL, &timeo, NULL);
	ASSERT_OK(err, "test-run-signal-timer");

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_LE(topts.duration, 500000000 /* 500ms */,
		  "test-run-signal-duration");

	signal(SIGALRM, SIG_DFL);
}

void test_signal_pending(void)
{
	test_signal_pending_by_type(BPF_PROG_TYPE_SOCKET_FILTER);
	test_signal_pending_by_type(BPF_PROG_TYPE_FLOW_DISSECTOR);
}
