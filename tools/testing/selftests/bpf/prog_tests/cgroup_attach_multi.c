// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>

#include "cgroup_helpers.h"

#define PING_CMD	"ping -q -c1 -w1 127.0.0.1 > /dev/null"

char bpf_log_buf[BPF_LOG_BUF_SIZE];

static int map_fd = -1;

static int prog_load_cnt(int verdict, int val)
{
	int cgroup_storage_fd, percpu_cgroup_storage_fd;

	if (map_fd < 0)
		map_fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, 4, 8, 1, 0);
	if (map_fd < 0) {
		printf("failed to create map '%s'\n", strerror(errno));
		return -1;
	}

	cgroup_storage_fd = bpf_create_map(BPF_MAP_TYPE_CGROUP_STORAGE,
				sizeof(struct bpf_cgroup_storage_key), 8, 0, 0);
	if (cgroup_storage_fd < 0) {
		printf("failed to create map '%s'\n", strerror(errno));
		return -1;
	}

	percpu_cgroup_storage_fd = bpf_create_map(
		BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE,
		sizeof(struct bpf_cgroup_storage_key), 8, 0, 0);
	if (percpu_cgroup_storage_fd < 0) {
		printf("failed to create map '%s'\n", strerror(errno));
		return -1;
	}

	struct bpf_insn prog[] = {
		BPF_MOV32_IMM(BPF_REG_0, 0),
		BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_0, -4), /* *(u32 *)(fp - 4) = r0 */
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4), /* r2 = fp - 4 */
		BPF_LD_MAP_FD(BPF_REG_1, map_fd),
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
		BPF_MOV64_IMM(BPF_REG_1, val), /* r1 = 1 */
		BPF_RAW_INSN(BPF_STX | BPF_XADD | BPF_DW, BPF_REG_0, BPF_REG_1, 0, 0), /* xadd r0 += r1 */

		BPF_LD_MAP_FD(BPF_REG_1, cgroup_storage_fd),
		BPF_MOV64_IMM(BPF_REG_2, 0),
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_local_storage),
		BPF_MOV64_IMM(BPF_REG_1, val),
		BPF_RAW_INSN(BPF_STX | BPF_XADD | BPF_W, BPF_REG_0, BPF_REG_1, 0, 0),

		BPF_LD_MAP_FD(BPF_REG_1, percpu_cgroup_storage_fd),
		BPF_MOV64_IMM(BPF_REG_2, 0),
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_local_storage),
		BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_0, 0),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_3, 0x1),
		BPF_STX_MEM(BPF_W, BPF_REG_0, BPF_REG_3, 0),

		BPF_MOV64_IMM(BPF_REG_0, verdict), /* r0 = verdict */
		BPF_EXIT_INSN(),
	};
	size_t insns_cnt = sizeof(prog) / sizeof(struct bpf_insn);
	int ret;

	ret = bpf_load_program(BPF_PROG_TYPE_CGROUP_SKB,
			       prog, insns_cnt, "GPL", 0,
			       bpf_log_buf, BPF_LOG_BUF_SIZE);

	close(cgroup_storage_fd);
	return ret;
}

void test_cgroup_attach_multi(void)
{
	__u32 prog_ids[4], prog_cnt = 0, attach_flags, saved_prog_id;
	int cg1 = 0, cg2 = 0, cg3 = 0, cg4 = 0, cg5 = 0, key = 0;
	DECLARE_LIBBPF_OPTS(bpf_prog_attach_opts, attach_opts);
	int allow_prog[7] = {-1};
	unsigned long long value;
	__u32 duration = 0;
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(allow_prog); i++) {
		allow_prog[i] = prog_load_cnt(1, 1 << i);
		if (CHECK(allow_prog[i] < 0, "prog_load",
			  "verifier output:\n%s\n-------\n", bpf_log_buf))
			goto err;
	}

	if (CHECK_FAIL(setup_cgroup_environment()))
		goto err;

	cg1 = create_and_get_cgroup("/cg1");
	if (CHECK_FAIL(cg1 < 0))
		goto err;
	cg2 = create_and_get_cgroup("/cg1/cg2");
	if (CHECK_FAIL(cg2 < 0))
		goto err;
	cg3 = create_and_get_cgroup("/cg1/cg2/cg3");
	if (CHECK_FAIL(cg3 < 0))
		goto err;
	cg4 = create_and_get_cgroup("/cg1/cg2/cg3/cg4");
	if (CHECK_FAIL(cg4 < 0))
		goto err;
	cg5 = create_and_get_cgroup("/cg1/cg2/cg3/cg4/cg5");
	if (CHECK_FAIL(cg5 < 0))
		goto err;

	if (CHECK_FAIL(join_cgroup("/cg1/cg2/cg3/cg4/cg5")))
		goto err;

	if (CHECK(bpf_prog_attach(allow_prog[0], cg1, BPF_CGROUP_INET_EGRESS,
				  BPF_F_ALLOW_MULTI),
		  "prog0_attach_to_cg1_multi", "errno=%d\n", errno))
		goto err;

	if (CHECK(!bpf_prog_attach(allow_prog[0], cg1, BPF_CGROUP_INET_EGRESS,
				   BPF_F_ALLOW_MULTI),
		  "fail_same_prog_attach_to_cg1", "unexpected success\n"))
		goto err;

	if (CHECK(bpf_prog_attach(allow_prog[1], cg1, BPF_CGROUP_INET_EGRESS,
				  BPF_F_ALLOW_MULTI),
		  "prog1_attach_to_cg1_multi", "errno=%d\n", errno))
		goto err;

	if (CHECK(bpf_prog_attach(allow_prog[2], cg2, BPF_CGROUP_INET_EGRESS,
				  BPF_F_ALLOW_OVERRIDE),
		  "prog2_attach_to_cg2_override", "errno=%d\n", errno))
		goto err;

	if (CHECK(bpf_prog_attach(allow_prog[3], cg3, BPF_CGROUP_INET_EGRESS,
				  BPF_F_ALLOW_MULTI),
		  "prog3_attach_to_cg3_multi", "errno=%d\n", errno))
		goto err;

	if (CHECK(bpf_prog_attach(allow_prog[4], cg4, BPF_CGROUP_INET_EGRESS,
			    BPF_F_ALLOW_OVERRIDE),
		  "prog4_attach_to_cg4_override", "errno=%d\n", errno))
		goto err;

	if (CHECK(bpf_prog_attach(allow_prog[5], cg5, BPF_CGROUP_INET_EGRESS, 0),
		  "prog5_attach_to_cg5_none", "errno=%d\n", errno))
		goto err;

	CHECK_FAIL(system(PING_CMD));
	CHECK_FAIL(bpf_map_lookup_elem(map_fd, &key, &value));
	CHECK_FAIL(value != 1 + 2 + 8 + 32);

	/* query the number of effective progs in cg5 */
	CHECK_FAIL(bpf_prog_query(cg5, BPF_CGROUP_INET_EGRESS,
				  BPF_F_QUERY_EFFECTIVE, NULL, NULL, &prog_cnt));
	CHECK_FAIL(prog_cnt != 4);
	/* retrieve prog_ids of effective progs in cg5 */
	CHECK_FAIL(bpf_prog_query(cg5, BPF_CGROUP_INET_EGRESS,
				  BPF_F_QUERY_EFFECTIVE, &attach_flags,
				  prog_ids, &prog_cnt));
	CHECK_FAIL(prog_cnt != 4);
	CHECK_FAIL(attach_flags != 0);
	saved_prog_id = prog_ids[0];
	/* check enospc handling */
	prog_ids[0] = 0;
	prog_cnt = 2;
	CHECK_FAIL(bpf_prog_query(cg5, BPF_CGROUP_INET_EGRESS,
				  BPF_F_QUERY_EFFECTIVE, &attach_flags,
				  prog_ids, &prog_cnt) != -1);
	CHECK_FAIL(errno != ENOSPC);
	CHECK_FAIL(prog_cnt != 4);
	/* check that prog_ids are returned even when buffer is too small */
	CHECK_FAIL(prog_ids[0] != saved_prog_id);
	/* retrieve prog_id of single attached prog in cg5 */
	prog_ids[0] = 0;
	CHECK_FAIL(bpf_prog_query(cg5, BPF_CGROUP_INET_EGRESS, 0, NULL,
				  prog_ids, &prog_cnt));
	CHECK_FAIL(prog_cnt != 1);
	CHECK_FAIL(prog_ids[0] != saved_prog_id);

	/* detach bottom program and ping again */
	if (CHECK(bpf_prog_detach2(-1, cg5, BPF_CGROUP_INET_EGRESS),
		  "prog_detach_from_cg5", "errno=%d\n", errno))
		goto err;

	value = 0;
	CHECK_FAIL(bpf_map_update_elem(map_fd, &key, &value, 0));
	CHECK_FAIL(system(PING_CMD));
	CHECK_FAIL(bpf_map_lookup_elem(map_fd, &key, &value));
	CHECK_FAIL(value != 1 + 2 + 8 + 16);

	/* test replace */

	attach_opts.flags = BPF_F_ALLOW_OVERRIDE | BPF_F_REPLACE;
	attach_opts.replace_prog_fd = allow_prog[0];
	if (CHECK(!bpf_prog_attach_xattr(allow_prog[6], cg1,
					 BPF_CGROUP_INET_EGRESS, &attach_opts),
		  "fail_prog_replace_override", "unexpected success\n"))
		goto err;
	CHECK_FAIL(errno != EINVAL);

	attach_opts.flags = BPF_F_REPLACE;
	if (CHECK(!bpf_prog_attach_xattr(allow_prog[6], cg1,
					 BPF_CGROUP_INET_EGRESS, &attach_opts),
		  "fail_prog_replace_no_multi", "unexpected success\n"))
		goto err;
	CHECK_FAIL(errno != EINVAL);

	attach_opts.flags = BPF_F_ALLOW_MULTI | BPF_F_REPLACE;
	attach_opts.replace_prog_fd = -1;
	if (CHECK(!bpf_prog_attach_xattr(allow_prog[6], cg1,
					 BPF_CGROUP_INET_EGRESS, &attach_opts),
		  "fail_prog_replace_bad_fd", "unexpected success\n"))
		goto err;
	CHECK_FAIL(errno != EBADF);

	/* replacing a program that is not attached to cgroup should fail  */
	attach_opts.replace_prog_fd = allow_prog[3];
	if (CHECK(!bpf_prog_attach_xattr(allow_prog[6], cg1,
					 BPF_CGROUP_INET_EGRESS, &attach_opts),
		  "fail_prog_replace_no_ent", "unexpected success\n"))
		goto err;
	CHECK_FAIL(errno != ENOENT);

	/* replace 1st from the top program */
	attach_opts.replace_prog_fd = allow_prog[0];
	if (CHECK(bpf_prog_attach_xattr(allow_prog[6], cg1,
					BPF_CGROUP_INET_EGRESS, &attach_opts),
		  "prog_replace", "errno=%d\n", errno))
		goto err;

	value = 0;
	CHECK_FAIL(bpf_map_update_elem(map_fd, &key, &value, 0));
	CHECK_FAIL(system(PING_CMD));
	CHECK_FAIL(bpf_map_lookup_elem(map_fd, &key, &value));
	CHECK_FAIL(value != 64 + 2 + 8 + 16);

	/* detach 3rd from bottom program and ping again */
	if (CHECK(!bpf_prog_detach2(0, cg3, BPF_CGROUP_INET_EGRESS),
		  "fail_prog_detach_from_cg3", "unexpected success\n"))
		goto err;

	if (CHECK(bpf_prog_detach2(allow_prog[3], cg3, BPF_CGROUP_INET_EGRESS),
		  "prog3_detach_from_cg3", "errno=%d\n", errno))
		goto err;

	value = 0;
	CHECK_FAIL(bpf_map_update_elem(map_fd, &key, &value, 0));
	CHECK_FAIL(system(PING_CMD));
	CHECK_FAIL(bpf_map_lookup_elem(map_fd, &key, &value));
	CHECK_FAIL(value != 64 + 2 + 16);

	/* detach 2nd from bottom program and ping again */
	if (CHECK(bpf_prog_detach2(-1, cg4, BPF_CGROUP_INET_EGRESS),
		  "prog_detach_from_cg4", "errno=%d\n", errno))
		goto err;

	value = 0;
	CHECK_FAIL(bpf_map_update_elem(map_fd, &key, &value, 0));
	CHECK_FAIL(system(PING_CMD));
	CHECK_FAIL(bpf_map_lookup_elem(map_fd, &key, &value));
	CHECK_FAIL(value != 64 + 2 + 4);

	prog_cnt = 4;
	CHECK_FAIL(bpf_prog_query(cg5, BPF_CGROUP_INET_EGRESS,
				  BPF_F_QUERY_EFFECTIVE, &attach_flags,
				  prog_ids, &prog_cnt));
	CHECK_FAIL(prog_cnt != 3);
	CHECK_FAIL(attach_flags != 0);
	CHECK_FAIL(bpf_prog_query(cg5, BPF_CGROUP_INET_EGRESS, 0, NULL,
				  prog_ids, &prog_cnt));
	CHECK_FAIL(prog_cnt != 0);

err:
	for (i = 0; i < ARRAY_SIZE(allow_prog); i++)
		if (allow_prog[i] >= 0)
			close(allow_prog[i]);
	close(cg1);
	close(cg2);
	close(cg3);
	close(cg4);
	close(cg5);
	cleanup_cgroup_environment();
}
