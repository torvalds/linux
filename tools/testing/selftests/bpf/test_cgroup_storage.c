// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <bpf/bpf.h>
#include <linux/filter.h>
#include <stdio.h>
#include <stdlib.h>

#include "bpf_rlimit.h"
#include "cgroup_helpers.h"

char bpf_log_buf[BPF_LOG_BUF_SIZE];

#define TEST_CGROUP "/test-bpf-cgroup-storage-buf/"

int main(int argc, char **argv)
{
	struct bpf_insn prog[] = {
		BPF_LD_MAP_FD(BPF_REG_1, 0), /* map fd */
		BPF_MOV64_IMM(BPF_REG_2, 0), /* flags, not used */
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
			     BPF_FUNC_get_local_storage),
		BPF_MOV64_IMM(BPF_REG_1, 1),
		BPF_STX_XADD(BPF_DW, BPF_REG_0, BPF_REG_1, 0),
		BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_0, 0),
		BPF_ALU64_IMM(BPF_AND, BPF_REG_1, 0x1),
		BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
		BPF_EXIT_INSN(),
	};
	size_t insns_cnt = sizeof(prog) / sizeof(struct bpf_insn);
	int error = EXIT_FAILURE;
	int map_fd, prog_fd, cgroup_fd;
	struct bpf_cgroup_storage_key key;
	unsigned long long value;

	map_fd = bpf_create_map(BPF_MAP_TYPE_CGROUP_STORAGE, sizeof(key),
				sizeof(value), 0, 0);
	if (map_fd < 0) {
		printf("Failed to create map: %s\n", strerror(errno));
		goto out;
	}

	prog[0].imm = map_fd;
	prog_fd = bpf_load_program(BPF_PROG_TYPE_CGROUP_SKB,
				   prog, insns_cnt, "GPL", 0,
				   bpf_log_buf, BPF_LOG_BUF_SIZE);
	if (prog_fd < 0) {
		printf("Failed to load bpf program: %s\n", bpf_log_buf);
		goto out;
	}

	if (setup_cgroup_environment()) {
		printf("Failed to setup cgroup environment\n");
		goto err;
	}

	/* Create a cgroup, get fd, and join it */
	cgroup_fd = create_and_get_cgroup(TEST_CGROUP);
	if (!cgroup_fd) {
		printf("Failed to create test cgroup\n");
		goto err;
	}

	if (join_cgroup(TEST_CGROUP)) {
		printf("Failed to join cgroup\n");
		goto err;
	}

	/* Attach the bpf program */
	if (bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_INET_EGRESS, 0)) {
		printf("Failed to attach bpf program\n");
		goto err;
	}

	if (bpf_map_get_next_key(map_fd, NULL, &key)) {
		printf("Failed to get the first key in cgroup storage\n");
		goto err;
	}

	if (bpf_map_lookup_elem(map_fd, &key, &value)) {
		printf("Failed to lookup cgroup storage\n");
		goto err;
	}

	/* Every second packet should be dropped */
	assert(system("ping localhost -c 1 -W 1 -q > /dev/null") == 0);
	assert(system("ping localhost -c 1 -W 1 -q > /dev/null"));
	assert(system("ping localhost -c 1 -W 1 -q > /dev/null") == 0);

	/* Check the counter in the cgroup local storage */
	if (bpf_map_lookup_elem(map_fd, &key, &value)) {
		printf("Failed to lookup cgroup storage\n");
		goto err;
	}

	if (value != 3) {
		printf("Unexpected data in the cgroup storage: %llu\n", value);
		goto err;
	}

	/* Bump the counter in the cgroup local storage */
	value++;
	if (bpf_map_update_elem(map_fd, &key, &value, 0)) {
		printf("Failed to update the data in the cgroup storage\n");
		goto err;
	}

	/* Every second packet should be dropped */
	assert(system("ping localhost -c 1 -W 1 -q > /dev/null") == 0);
	assert(system("ping localhost -c 1 -W 1 -q > /dev/null"));
	assert(system("ping localhost -c 1 -W 1 -q > /dev/null") == 0);

	/* Check the final value of the counter in the cgroup local storage */
	if (bpf_map_lookup_elem(map_fd, &key, &value)) {
		printf("Failed to lookup the cgroup storage\n");
		goto err;
	}

	if (value != 7) {
		printf("Unexpected data in the cgroup storage: %llu\n", value);
		goto err;
	}

	error = 0;
	printf("test_cgroup_storage:PASS\n");

err:
	cleanup_cgroup_environment();

out:
	return error;
}
