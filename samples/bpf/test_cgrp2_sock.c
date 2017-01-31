/* eBPF example program:
 *
 * - Loads eBPF program
 *
 *   The eBPF program sets the sk_bound_dev_if index in new AF_INET{6}
 *   sockets opened by processes in the cgroup.
 *
 * - Attaches the new program to a cgroup using BPF_PROG_ATTACH
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <linux/bpf.h>

#include "libbpf.h"

char bpf_log_buf[BPF_LOG_BUF_SIZE];

static int prog_load(int idx)
{
	struct bpf_insn prog[] = {
		BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
		BPF_MOV64_IMM(BPF_REG_3, idx),
		BPF_MOV64_IMM(BPF_REG_2, offsetof(struct bpf_sock, bound_dev_if)),
		BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_3, offsetof(struct bpf_sock, bound_dev_if)),
		BPF_MOV64_IMM(BPF_REG_0, 1), /* r0 = verdict */
		BPF_EXIT_INSN(),
	};
	size_t insns_cnt = sizeof(prog) / sizeof(struct bpf_insn);

	return bpf_load_program(BPF_PROG_TYPE_CGROUP_SOCK, prog, insns_cnt,
				"GPL", 0, bpf_log_buf, BPF_LOG_BUF_SIZE);
}

static int usage(const char *argv0)
{
	printf("Usage: %s cg-path device-index\n", argv0);
	return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
	int cg_fd, prog_fd, ret;
	unsigned int idx;

	if (argc < 2)
		return usage(argv[0]);

	idx = if_nametoindex(argv[2]);
	if (!idx) {
		printf("Invalid device name\n");
		return EXIT_FAILURE;
	}

	cg_fd = open(argv[1], O_DIRECTORY | O_RDONLY);
	if (cg_fd < 0) {
		printf("Failed to open cgroup path: '%s'\n", strerror(errno));
		return EXIT_FAILURE;
	}

	prog_fd = prog_load(idx);
	printf("Output from kernel verifier:\n%s\n-------\n", bpf_log_buf);

	if (prog_fd < 0) {
		printf("Failed to load prog: '%s'\n", strerror(errno));
		return EXIT_FAILURE;
	}

	ret = bpf_prog_attach(prog_fd, cg_fd, BPF_CGROUP_INET_SOCK_CREATE);
	if (ret < 0) {
		printf("Failed to attach prog to cgroup: '%s'\n",
		       strerror(errno));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
