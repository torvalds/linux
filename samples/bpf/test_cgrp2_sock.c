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
#include <inttypes.h>
#include <linux/bpf.h>

#include "libbpf.h"

char bpf_log_buf[BPF_LOG_BUF_SIZE];

static int prog_load(__u32 idx, __u32 mark, __u32 prio)
{
	/* save pointer to context */
	struct bpf_insn prog_start[] = {
		BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
	};
	struct bpf_insn prog_end[] = {
		BPF_MOV64_IMM(BPF_REG_0, 1), /* r0 = verdict */
		BPF_EXIT_INSN(),
	};

	/* set sk_bound_dev_if on socket */
	struct bpf_insn prog_dev[] = {
		BPF_MOV64_IMM(BPF_REG_3, idx),
		BPF_MOV64_IMM(BPF_REG_2, offsetof(struct bpf_sock, bound_dev_if)),
		BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_3, offsetof(struct bpf_sock, bound_dev_if)),
	};

	/* set mark on socket */
	struct bpf_insn prog_mark[] = {
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
		BPF_MOV64_IMM(BPF_REG_3, mark),
		BPF_MOV64_IMM(BPF_REG_2, offsetof(struct bpf_sock, mark)),
		BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_3, offsetof(struct bpf_sock, mark)),
	};

	/* set priority on socket */
	struct bpf_insn prog_prio[] = {
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
		BPF_MOV64_IMM(BPF_REG_3, prio),
		BPF_MOV64_IMM(BPF_REG_2, offsetof(struct bpf_sock, priority)),
		BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_3, offsetof(struct bpf_sock, priority)),
	};

	struct bpf_insn *prog;
	size_t insns_cnt;
	void *p;
	int ret;

	insns_cnt = sizeof(prog_start) + sizeof(prog_end);
	if (idx)
		insns_cnt += sizeof(prog_dev);

	if (mark)
		insns_cnt += sizeof(prog_mark);

	if (prio)
		insns_cnt += sizeof(prog_prio);

	p = prog = malloc(insns_cnt);
	if (!prog) {
		fprintf(stderr, "Failed to allocate memory for instructions\n");
		return EXIT_FAILURE;
	}

	memcpy(p, prog_start, sizeof(prog_start));
	p += sizeof(prog_start);

	if (idx) {
		memcpy(p, prog_dev, sizeof(prog_dev));
		p += sizeof(prog_dev);
	}

	if (mark) {
		memcpy(p, prog_mark, sizeof(prog_mark));
		p += sizeof(prog_mark);
	}

	if (prio) {
		memcpy(p, prog_prio, sizeof(prog_prio));
		p += sizeof(prog_prio);
	}

	memcpy(p, prog_end, sizeof(prog_end));
	p += sizeof(prog_end);

	insns_cnt /= sizeof(struct bpf_insn);

	ret = bpf_load_program(BPF_PROG_TYPE_CGROUP_SOCK, prog, insns_cnt,
				"GPL", 0, bpf_log_buf, BPF_LOG_BUF_SIZE);

	free(prog);

	return ret;
}

static int usage(const char *argv0)
{
	printf("Usage: %s -b bind-to-dev -m mark -p prio cg-path\n", argv0);
	return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
	__u32 idx = 0, mark = 0, prio = 0;
	const char *cgrp_path = NULL;
	int cg_fd, prog_fd, ret;
	int rc;

	while ((rc = getopt(argc, argv, "b:m:p:")) != -1) {
		switch (rc) {
		case 'b':
			idx = if_nametoindex(optarg);
			if (!idx) {
				idx = strtoumax(optarg, NULL, 0);
				if (!idx) {
					printf("Invalid device name\n");
					return EXIT_FAILURE;
				}
			}
			break;
		case 'm':
			mark = strtoumax(optarg, NULL, 0);
			break;
		case 'p':
			prio = strtoumax(optarg, NULL, 0);
			break;
		default:
			return usage(argv[0]);
		}
	}

	if (optind == argc)
		return usage(argv[0]);

	cgrp_path = argv[optind];
	if (!cgrp_path) {
		fprintf(stderr, "cgroup path not given\n");
		return EXIT_FAILURE;
	}

	if (!idx && !mark && !prio) {
		fprintf(stderr,
			"One of device, mark or priority must be given\n");
		return EXIT_FAILURE;
	}

	cg_fd = open(cgrp_path, O_DIRECTORY | O_RDONLY);
	if (cg_fd < 0) {
		printf("Failed to open cgroup path: '%s'\n", strerror(errno));
		return EXIT_FAILURE;
	}

	prog_fd = prog_load(idx, mark, prio);
	if (prog_fd < 0) {
		printf("Failed to load prog: '%s'\n", strerror(errno));
		printf("Output from kernel verifier:\n%s\n-------\n",
		       bpf_log_buf);
		return EXIT_FAILURE;
	}

	ret = bpf_prog_attach(prog_fd, cg_fd, BPF_CGROUP_INET_SOCK_CREATE, 0);
	if (ret < 0) {
		printf("Failed to attach prog to cgroup: '%s'\n",
		       strerror(errno));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
