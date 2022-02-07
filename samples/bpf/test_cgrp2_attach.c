/* eBPF example program:
 *
 * - Creates arraymap in kernel with 4 bytes keys and 8 byte values
 *
 * - Loads eBPF program
 *
 *   The eBPF program accesses the map passed in to store two pieces of
 *   information. The number of invocations of the program, which maps
 *   to the number of packets received, is stored to key 0. Key 1 is
 *   incremented on each iteration by the number of bytes stored in
 *   the skb.
 *
 * - Attaches the new program to a cgroup using BPF_PROG_ATTACH
 *
 * - Every second, reads map[0] and map[1] to see how many bytes and
 *   packets were seen on any socket of tasks in the given cgroup.
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

#include <linux/bpf.h>
#include <bpf/bpf.h>

#include "bpf_insn.h"

enum {
	MAP_KEY_PACKETS,
	MAP_KEY_BYTES,
};

char bpf_log_buf[BPF_LOG_BUF_SIZE];

static int prog_load(int map_fd, int verdict)
{
	struct bpf_insn prog[] = {
		BPF_MOV64_REG(BPF_REG_6, BPF_REG_1), /* save r6 so it's not clobbered by BPF_CALL */

		/* Count packets */
		BPF_MOV64_IMM(BPF_REG_0, MAP_KEY_PACKETS), /* r0 = 0 */
		BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_0, -4), /* *(u32 *)(fp - 4) = r0 */
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4), /* r2 = fp - 4 */
		BPF_LD_MAP_FD(BPF_REG_1, map_fd), /* load map fd to r1 */
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
		BPF_MOV64_IMM(BPF_REG_1, 1), /* r1 = 1 */
		BPF_ATOMIC_OP(BPF_DW, BPF_ADD, BPF_REG_0, BPF_REG_1, 0),

		/* Count bytes */
		BPF_MOV64_IMM(BPF_REG_0, MAP_KEY_BYTES), /* r0 = 1 */
		BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_0, -4), /* *(u32 *)(fp - 4) = r0 */
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4), /* r2 = fp - 4 */
		BPF_LD_MAP_FD(BPF_REG_1, map_fd),
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
		BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_6, offsetof(struct __sk_buff, len)), /* r1 = skb->len */

		BPF_ATOMIC_OP(BPF_DW, BPF_ADD, BPF_REG_0, BPF_REG_1, 0),

		BPF_MOV64_IMM(BPF_REG_0, verdict), /* r0 = verdict */
		BPF_EXIT_INSN(),
	};
	size_t insns_cnt = sizeof(prog) / sizeof(struct bpf_insn);
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		.log_buf = bpf_log_buf,
		.log_size = BPF_LOG_BUF_SIZE,
	);

	return bpf_prog_load(BPF_PROG_TYPE_CGROUP_SKB, NULL, "GPL",
			     prog, insns_cnt, &opts);
}

static int usage(const char *argv0)
{
	printf("Usage: %s [-d] [-D] <cg-path> <egress|ingress>\n", argv0);
	printf("	-d	Drop Traffic\n");
	printf("	-D	Detach filter, and exit\n");
	return EXIT_FAILURE;
}

static int attach_filter(int cg_fd, int type, int verdict)
{
	int prog_fd, map_fd, ret, key;
	long long pkt_cnt, byte_cnt;

	map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, NULL,
				sizeof(key), sizeof(byte_cnt),
				256, NULL);
	if (map_fd < 0) {
		printf("Failed to create map: '%s'\n", strerror(errno));
		return EXIT_FAILURE;
	}

	prog_fd = prog_load(map_fd, verdict);
	printf("Output from kernel verifier:\n%s\n-------\n", bpf_log_buf);

	if (prog_fd < 0) {
		printf("Failed to load prog: '%s'\n", strerror(errno));
		return EXIT_FAILURE;
	}

	ret = bpf_prog_attach(prog_fd, cg_fd, type, 0);
	if (ret < 0) {
		printf("Failed to attach prog to cgroup: '%s'\n",
		       strerror(errno));
		return EXIT_FAILURE;
	}
	while (1) {
		key = MAP_KEY_PACKETS;
		assert(bpf_map_lookup_elem(map_fd, &key, &pkt_cnt) == 0);

		key = MAP_KEY_BYTES;
		assert(bpf_map_lookup_elem(map_fd, &key, &byte_cnt) == 0);

		printf("cgroup received %lld packets, %lld bytes\n",
		       pkt_cnt, byte_cnt);
		sleep(1);
	}

	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	int detach_only = 0, verdict = 1;
	enum bpf_attach_type type;
	int opt, cg_fd, ret;

	while ((opt = getopt(argc, argv, "Dd")) != -1) {
		switch (opt) {
		case 'd':
			verdict = 0;
			break;
		case 'D':
			detach_only = 1;
			break;
		default:
			return usage(argv[0]);
		}
	}

	if (argc - optind < 2)
		return usage(argv[0]);

	if (strcmp(argv[optind + 1], "ingress") == 0)
		type = BPF_CGROUP_INET_INGRESS;
	else if (strcmp(argv[optind + 1], "egress") == 0)
		type = BPF_CGROUP_INET_EGRESS;
	else
		return usage(argv[0]);

	cg_fd = open(argv[optind], O_DIRECTORY | O_RDONLY);
	if (cg_fd < 0) {
		printf("Failed to open cgroup path: '%s'\n", strerror(errno));
		return EXIT_FAILURE;
	}

	if (detach_only) {
		ret = bpf_prog_detach(cg_fd, type);
		printf("bpf_prog_detach() returned '%s' (%d)\n",
		       strerror(errno), errno);
	} else
		ret = attach_filter(cg_fd, type, verdict);

	return ret;
}
